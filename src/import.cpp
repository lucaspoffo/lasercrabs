#include <stdio.h>

#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags
#include "types.h"
#include "lmath.h"
#include "data/array.h"
#include <dirent.h>
#include <map>
#include <array>
#include <GL/glew.h>
#include <SDL.h>
#undef main
#include <cfloat>
#include "Recast.h"
#include "data/mesh.h"
#include <sstream>

namespace VI
{

const char* model_in_extension = ".blend";
const char* model_intermediate_extension = ".fbx";
const char* mesh_out_extension = ".msh";

const char* font_in_extension = ".ttf";
const char* font_in_extension_2 = ".otf"; // Must be same length
const char* font_out_extension = ".fnt";

const char* level_out_extension = ".lvl";
const char* anim_out_extension = ".anm";
const char* arm_out_extension = ".arm";
const char* texture_extension = ".png";
const char* shader_extension = ".glsl";
const char* asset_in_folder = "../assets/";
const char* asset_out_folder = "assets/";
const char* level_in_folder = "../assets/lvl/";
const char* level_out_folder = "assets/lvl/";

Quat import_rotation = Quat(PI * -0.5f, Vec3(1, 0, 0));
const int version = 9;

template <typename T>
T read(FILE* f)
{
	T t;
	fread(&t, sizeof(T), 1, f);
	return t;
}

std::string read_string(FILE* f)
{
	Array<char> buffer;
	int length = read<int>(f);
	buffer.resize(length + 1);
	fread(buffer.data, sizeof(char), length, f);

	return std::string(buffer.data);
}

void write_string(const std::string& str, FILE* f)
{
	int length = str.length();
	fwrite(&length, sizeof(int), 1, f);
	fwrite(str.c_str(), sizeof(char), length, f);
}

template<typename T>
using Map = std::map<std::string, T>;
template<typename T>
using Map2 = std::map<std::string, Map<T> >;

bool maps_equal(const Map<std::string>& a, const Map<std::string>& b)
{
	if (a.size() != b.size())
		return false;
	for (auto i = a.begin(); i != a.end(); i++)
	{
		auto j = b.find(i->first);
		if (j == b.end() || i->second.compare(j->second))
			return false;
	}
	return true;
}

bool maps_equal(const Map<int>& a, const Map<int>& b)
{
	if (a.size() != b.size())
		return false;
	for (auto i = a.begin(); i != a.end(); i++)
	{
		auto j = b.find(i->first);
		if (j == b.end() || i->second != j->second)
			return false;
	}
	return true;
}

template<typename T>
bool maps_equal2(const Map2<T>& a, const Map2<T>& b)
{
	if (a.size() != b.size())
		return false;
	for (auto i = a.begin(); i != a.end(); i++)
	{
		auto j = b.find(i->first);
		if (j == b.end() || !maps_equal(i->second, j->second))
			return false;
	}
	return true;
}

template<typename T>
void map_init(Map2<T>& map, const std::string& key)
{
	auto i = map.find(key);
	if (i == map.end())
		map[key] = Map<T>();
}

template<typename T>
void map_flatten(const Map2<T>& input, Map<T>& output)
{
	for (auto i = input.begin(); i != input.end(); i++)
	{
		for (auto j = i->second.begin(); j != i->second.end(); j++)
			output[j->first] = j->second;
	}
}

template<typename T>
T& map_get(Map<T>& map, const std::string& key)
{
	return map[key];
}

template<typename T>
int map_key_to_index(const Map<T>& map, const std::string& key)
{
	int index = 0;
	for (auto i : map)
	{
		if (!i.first.compare(key))
			return index;
		index++;
	}
	return AssetNull;
}

template<typename T>
void map_add(Map<T>& map, const std::string& key, const T& value)
{
	map[key] = value;
}

template<typename T>
void map_add(Map2<T>& map, const std::string& key, const std::string& key2, const T& value)
{
	auto i = map.find(key);
	if (i == map.end())
	{
		map[key] = Map<T>();
		i = map.find(key);
	}
	map_add(i->second, key2, value);
}

template<typename T>
void map_copy(const Map2<T>& src, const std::string& key, Map2<T>& dest)
{
	auto i = src.find(key);
	if (i != src.end())
		dest[key] = i->second;
}

void map_read(FILE* f, Map<std::string>& map)
{
	int count = read<int>(f);
	for (int i = 0; i < count; i++)
	{
		std::string key = read_string(f);
		std::string value = read_string(f);
		map[key] = value;
	}
}

void map_read(FILE* f, Map<int>& map)
{
	int count = read<int>(f);
	for (int i = 0; i < count; i++)
	{
		std::string key = read_string(f);
		map[key] = read<int>(f);
	}
}

template<typename T>
void map_read(FILE* f, Map2<T>& map)
{
	int count = read<int>(f);
	for (int i = 0; i < count; i++)
	{
		std::string key = read_string(f);
		map[key] = Map<T>();
		Map<T>& inner = map[key];
		map_read(f, inner);
	}
}

void map_write(Map<std::string>& map, FILE* f)
{
	int count = map.size();
	fwrite(&count, sizeof(int), 1, f);
	for (auto j = map.begin(); j != map.end(); j++)
	{
		int length = j->first.length();
		fwrite(&length, sizeof(int), 1, f);
		fwrite(j->first.c_str(), sizeof(char), length, f);

		length = j->second.length();
		fwrite(&length, sizeof(int), 1, f);
		fwrite(j->second.c_str(), sizeof(char), length, f);
	}
}

void map_write(Map<int>& map, FILE* f)
{
	int count = map.size();
	fwrite(&count, sizeof(int), 1, f);
	for (auto j = map.begin(); j != map.end(); j++)
	{
		int length = j->first.length();
		fwrite(&length, sizeof(int), 1, f);
		fwrite(j->first.c_str(), sizeof(char), length, f);

		fwrite(&j->second, sizeof(int), 1, f);
	}
}

template<typename T>
void map_write(Map2<T>& map, FILE* f)
{
	int count = map.size();
	fwrite(&count, sizeof(int), 1, f);
	for (auto i = map.begin(); i != map.end(); i++)
	{
		int length = i->first.length();
		fwrite(&length, sizeof(int), 1, f);
		fwrite(i->first.c_str(), sizeof(char), length, f);

		Map<T>& inner = map[i->first];
		map_write(inner, f);
	}
}

bool has_extension(const std::string& path, const char* extension)
{
	int extension_length = strlen(extension);
	if (path.length() > extension_length)
	{
		if (strcmp(path.c_str() + path.length() - extension_length, extension) == 0)
			return true;
	}
	return false;
}

void write_asset_header(FILE* file, const std::string& name, const Map<std::string>& assets)
{
	int asset_count = assets.size();
	fprintf(file, "\tnamespace %s\n\t{\n\t\tconst int count = %d;\n", name.c_str(), asset_count);
	int index = 0;
	for (auto i = assets.begin(); i != assets.end(); i++)
	{
		fprintf(file, "\t\tconst AssetID %s = %d;\n", i->first.c_str(), index);
		index++;
	}
	fprintf(file, "\t}\n");
}

void write_asset_header(FILE* file, const std::string& name, const Map<int>& assets)
{
	int asset_count = assets.size();
	fprintf(file, "\tnamespace %s\n\t{\n\t\tconst int count = %d;\n", name.c_str(), asset_count);
	for (auto i = assets.begin(); i != assets.end(); i++)
		fprintf(file, "\t\tconst AssetID %s = %d;\n", i->first.c_str(), i->second);
	fprintf(file, "\t}\n");
}

void write_asset_source(FILE* file, const std::string& name, const Map<std::string>& assets)
{
	fprintf(file, "\nconst char* AssetLookup::%s::values[] =\n{\n", name.c_str());
	for (auto i = assets.begin(); i != assets.end(); i++)
		fprintf(file, "\t\"%s\",\n", i->second.c_str());
	fprintf(file, "};\n\n");
}

std::string get_asset_name(const std::string& filename)
{
	size_t start = filename.find_last_of("/");
	if (start == std::string::npos)
		start = 0;
	else
		start += 1;
	size_t end = filename.find_last_of(".");
	if (end == std::string::npos)
		end = filename.length();
	return filename.substr(start, end - start);
}

#if defined WIN32
#include "windows.h"
LONGLONG filetime_to_posix(FILETIME ft)
{
	// takes the last modified date
	LARGE_INTEGER date, adjust;
	date.HighPart = ft.dwHighDateTime;
	date.LowPart = ft.dwLowDateTime;

	// 100-nanoseconds = milliseconds * 10000
	adjust.QuadPart = 11644473600000 * 10000;

	// removes the diff between 1970 and 1601
	date.QuadPart -= adjust.QuadPart;

	// converts back from 100-nanoseconds to seconds
	return date.QuadPart / 10000000;
}

LONGLONG filemtime(const std::string& file)
{
	WIN32_FIND_DATA FindFileData;
	HANDLE handle = FindFirstFile(file.c_str(), &FindFileData);
	if (handle == INVALID_HANDLE_VALUE)
		return 0;
	else
	{
		FindClose(handle);
		return filetime_to_posix(FindFileData.ftLastWriteTime);
	}
}

bool run_cmd(const std::string& cmd)
{
	PROCESS_INFORMATION piProcInfo; 
	STARTUPINFO siStartInfo;

	// Set up members of the PROCESS_INFORMATION structure. 

	ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

	// Set up members of the STARTUPINFO structure. 
	// This structure specifies the STDIN and STDOUT handles for redirection.

	ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	siStartInfo.cb = sizeof(STARTUPINFO); 

	SECURITY_ATTRIBUTES security_attributes;
	security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
	security_attributes.bInheritHandle = true;
	security_attributes.lpSecurityDescriptor = 0;

	HANDLE stdin_read, stdin_write;
	if (!CreatePipe(&stdin_read, &stdin_write, &security_attributes, 0))
		return false;
	siStartInfo.hStdInput = stdin_read;

	if (!SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0))
		return false;

	HANDLE stderr_read, stderr_write;

	if (!CreatePipe(&stderr_read, &stderr_write, &security_attributes, 0))
		return false;

	if (!SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0))
		return false;

	siStartInfo.hStdError = stderr_write;

	siStartInfo.hStdOutput = NULL;

	siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

	// Create the child process. 

	
	if (!CreateProcess(NULL,
		const_cast<char*>(cmd.c_str()),     // command line 
		NULL,          // process security attributes 
		NULL,          // primary thread security attributes 
		TRUE,          // handles are inherited 
		NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW,             // creation flags 
		NULL,          // use parent's environment 
		NULL,          // use parent's current directory 
		&siStartInfo,  // STARTUPINFO pointer 
		&piProcInfo))  // receives PROCESS_INFORMATION 
	{
		return false;
	}

	if (WaitForSingleObject(piProcInfo.hProcess, INFINITE) == WAIT_FAILED)
		return false;

	DWORD exit_code;

	bool success;
	if (GetExitCodeProcess(piProcInfo.hProcess, &exit_code))
	{
		success = exit_code == 0;
		if (!success)
		{
			// Copy child stderr to our stderr
			DWORD dwRead, dwWritten; 
			const int BUFSIZE = 4096;
			CHAR chBuf[BUFSIZE]; 
			BOOL bSuccess = FALSE;
			HANDLE hParentStdErr = GetStdHandle(STD_ERROR_HANDLE);

			for (;;) 
			{ 
				bSuccess = ReadFile(stderr_read, chBuf, BUFSIZE, &dwRead, NULL);
				if (!bSuccess || dwRead == 0)
					break;

				bSuccess = WriteFile(hParentStdErr, chBuf, dwRead, &dwWritten, NULL);
				if (!bSuccess)
					break;
			}
		}
	}
	else
		success = false;

	CloseHandle(stdin_read);
	CloseHandle(stdin_write);
	CloseHandle(stderr_read);
	CloseHandle(stderr_write);
	CloseHandle(piProcInfo.hProcess);
	CloseHandle(piProcInfo.hThread);

	return success;
}
#else
#include <sys/stat.h>
long long filemtime(const std::string& file)
{
	struct stat st;
	if (stat(file.c_str(), &st))
		return 0;
	return st.st_mtime;
}

bool run_cmd(const std::string& cmd)
{
	return system(cmd.c_str()) == 0;
}
#endif

bool cp(const std::string& from, const std::string& to)
{
	char buf[4096];
	size_t size;

	FILE* source = fopen(from.c_str(), "rb");
	if (!source)
		return false;
	FILE* dest = fopen(to.c_str(), "w+b");
	if (!dest)
	{
		fclose(source);
		return false;
	}

	while ((size = fread(buf, 1, 4096, source)))
		fwrite(buf, 1, size, dest);

	fclose(source);
	fclose(dest);
	return true;
}

long long asset_mtime(const Map<std::string>& map, const std::string& asset_name)
{
	auto entry = map.find(asset_name);
	if (entry == map.end())
		return 0;
	else
		return filemtime(entry->second);
}

long long asset_mtime(const Map2<std::string>& map, const std::string& asset_name)
{
	auto entry = map.find(asset_name);
	if (entry == map.end())
		return 0;
	else
	{
		long long mtime = LLONG_MAX;
		for (auto i = entry->second.begin(); i != entry->second.end(); i++)
		{
			long long t = filemtime(i->second);
			mtime = t < mtime ? t : mtime;
		}
		return mtime;
	}
}

struct Manifest
{
	Map2<std::string> meshes;
	Map2<std::string> animations;
	Map2<std::string> armatures;
	Map2<int> bones;
	Map<std::string> textures;
	Map<std::string> shaders;
	Map2<std::string> uniforms;
	Map<std::string> fonts;
	Map<std::string> levels;

	Manifest()
		: meshes(),
		animations(),
		armatures(),
		bones(),
		textures(),
		shaders(),
		uniforms(),
		fonts(),
		levels()
	{

	}
};

bool manifests_equal(const Manifest& a, const Manifest& b)
{
	return maps_equal2(a.meshes, b.meshes)
		&& maps_equal2(a.animations, b.animations)
		&& maps_equal2(a.armatures, b.armatures)
		&& maps_equal2(a.bones, b.bones)
		&& maps_equal(a.textures, b.textures)
		&& maps_equal(a.shaders, b.shaders)
		&& maps_equal2(a.uniforms, b.uniforms)
		&& maps_equal(a.fonts, b.fonts)
		&& maps_equal(a.levels, b.levels);
}

bool manifest_read(const char* path, Manifest& manifest)
{
	FILE* f = fopen(path, "rb");
	if (f)
	{
		int read_version = read<int>(f);
		if (version != read_version)
		{
			fclose(f);
			return false;
		}
		else
		{
			map_read(f, manifest.meshes);
			map_read(f, manifest.animations);
			map_read(f, manifest.armatures);
			map_read(f, manifest.bones);
			map_read(f, manifest.textures);
			map_read(f, manifest.shaders);
			map_read(f, manifest.uniforms);
			map_read(f, manifest.fonts);
			map_read(f, manifest.levels);
			fclose(f);
			return true;
		}
	}
	else
		return false;
}

bool manifest_write(Manifest& manifest, const char* path)
{
	FILE* f = fopen(path, "w+b");
	if (!f)
	{
		fprintf(stderr, "Error: failed to open asset cache file %s for writing.\n", path);
		return false;
	}
	fwrite(&version, sizeof(int), 1, f);
	map_write(manifest.meshes, f);
	map_write(manifest.animations, f);
	map_write(manifest.armatures, f);
	map_write(manifest.bones, f);
	map_write(manifest.textures, f);
	map_write(manifest.shaders, f);
	map_write(manifest.uniforms, f);
	map_write(manifest.fonts, f);
	map_write(manifest.levels, f);
	fclose(f);
	return true;
}

struct ImporterState
{
	Manifest cached_manifest;
	Manifest manifest;

	bool rebuild;
	bool error;

	long long manifest_mtime;

	ImporterState()
		: cached_manifest(),
		manifest(),
		rebuild(),
		error(),
		manifest_mtime()
	{

	}
};

int exit_error()
{
	SDL_Quit();
	return 1;
}

const aiNode* find_mesh_node(const aiScene* scene, const aiNode* node, const aiMesh* mesh)
{
	for (int i = 0; i < node->mNumMeshes; i++)
	{
		if (scene->mMeshes[node->mMeshes[i]] == mesh)
			return node;
	}

	for (int i = 0; i < node->mNumChildren; i++)
	{
		const aiNode* found = find_mesh_node(scene, node->mChildren[i], mesh);
		if (found)
			return found;
	}

	return 0;
}

template<typename T>
void clean_name(T& name)
{
	for (int i = 0; ; i++)
	{
		char c = name[i];
		if (c == 0)
			break;
		if ((c < 'A' || c > 'Z')
			&& (c < 'a' || c > 'z')
			&& (i == 0 || c < '0' || c > '9')
			&& c != '_')
			name[i] = '_';
	}
}

std::string get_mesh_name(const aiScene* scene, const std::string& asset_name, const aiMesh* ai_mesh, const aiNode* mesh_node)
{
	std::ostringstream name_builder;
	if (scene->mNumMeshes > 1)
		name_builder << asset_name << "_";
	if (ai_mesh->mMaterialIndex > 0)
		name_builder << mesh_node->mName.C_Str() << "_" << ai_mesh->mMaterialIndex;
	else
		name_builder << mesh_node->mName.C_Str();
	std::string name = name_builder.str();
	clean_name(name);
	return name;
}

bool load_anim(const Armature& armature, const aiAnimation* in, Animation* out, const Map<int>& bone_map)
{
	out->duration = (float)(in->mDuration / in->mTicksPerSecond);
	out->channels.reserve(in->mNumChannels);
	for (unsigned int i = 0; i < in->mNumChannels; i++)
	{
		aiNodeAnim* in_channel = in->mChannels[i];
		auto bone_index_entry = bone_map.find(in_channel->mNodeName.C_Str());
		if (bone_index_entry != bone_map.end())
		{
			int bone_index = bone_index_entry->second;
			Channel* out_channel = out->channels.add();
			out_channel->bone_index = bone_index;

			out_channel->positions.resize(in_channel->mNumPositionKeys);

			for (unsigned int j = 0; j < in_channel->mNumPositionKeys; j++)
			{
				out_channel->positions[j].time = (float)(in_channel->mPositionKeys[j].mTime / in->mTicksPerSecond);
				aiVector3D value = in_channel->mPositionKeys[j].mValue;
				out_channel->positions[j].value = Vec3(value.y, value.z, value.x);
			}

			out_channel->rotations.resize(in_channel->mNumRotationKeys);
			for (unsigned int j = 0; j < in_channel->mNumRotationKeys; j++)
			{
				out_channel->rotations[j].time = (float)(in_channel->mRotationKeys[j].mTime / in->mTicksPerSecond);
				aiQuaternion value = in_channel->mRotationKeys[j].mValue;
				Quat q = Quat(value.w, value.x, value.y, value.z);
				Vec3 axis;
				float angle;
				q.to_angle_axis(angle, axis);
				Vec3 corrected_axis = Vec3(axis.y, axis.z, axis.x);
				out_channel->rotations[j].value = Quat(angle, corrected_axis);
			}

			out_channel->scales.resize(in_channel->mNumScalingKeys);
			for (unsigned int j = 0; j < in_channel->mNumScalingKeys; j++)
			{
				out_channel->scales[j].time = (float)(in_channel->mScalingKeys[j].mTime / in->mTicksPerSecond);
				aiVector3D value = in_channel->mScalingKeys[j].mValue;
				out_channel->scales[j].value = Vec3(value.y, value.z, value.x);
			}
		}
	}
	return true;
}

const aiScene* load_fbx(Assimp::Importer& importer, const std::string& path)
{
	const aiScene* scene = importer.ReadFile
	(
		path,
		aiProcess_JoinIdenticalVertices
		| aiProcess_Triangulate
		| aiProcess_GenNormals
		| aiProcess_ValidateDataStructure
	);
	if (!scene)
		fprintf(stderr, "%s\n", importer.GetErrorString());
	return scene;
}

bool load_mesh(const aiMesh* mesh, Mesh* out)
{
	out->bounds_min = Vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	out->bounds_max = Vec3(FLT_MIN, FLT_MIN, FLT_MIN);
	// Fill vertices positions
	out->vertices.reserve(mesh->mNumVertices);
	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		aiVector3D pos = mesh->mVertices[i];
		Vec3 v = Vec3(pos.y, pos.z, pos.x);
		out->bounds_min.x = fmin(v.x, out->bounds_min.x);
		out->bounds_min.y = fmin(v.y, out->bounds_min.y);
		out->bounds_min.z = fmin(v.z, out->bounds_min.z);
		out->bounds_max.x = fmax(v.x, out->bounds_max.x);
		out->bounds_max.y = fmax(v.y, out->bounds_max.y);
		out->bounds_max.z = fmax(v.z, out->bounds_max.z);
		out->vertices.add(v);
	}

	// Fill vertices normals
	if (mesh->HasNormals())
	{
		out->normals.reserve(mesh->mNumVertices);
		for (unsigned int i = 0; i < mesh->mNumVertices; i++)
		{
			aiVector3D n = mesh->mNormals[i];
			Vec3 v = Vec3(n.y, n.z, n.x);
			out->normals.add(v);
		}
	}
	else
	{
		fprintf(stderr, "Error: mesh has no normals.\n");
		return false;
	}

	// Fill face indices
	out->indices.reserve(3 * mesh->mNumFaces);
	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		// Assume the mesh has only triangles.
		int j = mesh->mFaces[i].mIndices[0];
		out->indices.add(j);
		j = mesh->mFaces[i].mIndices[1];
		out->indices.add(j);
		j = mesh->mFaces[i].mIndices[2];
		out->indices.add(j);
	}

	return true;
}

// Build armature for skinned model
bool build_armature(Armature& armature, Map<int>& bone_map, aiNode* node, int parent_index, int& counter)
{
	int current_bone_index;
	Map<int>::iterator bone_index_entry = bone_map.find(node->mName.C_Str());
	if (bone_index_entry != bone_map.end())
	{
		bone_map[node->mName.C_Str()] = counter;
		if (counter >= armature.hierarchy.length)
		{
			armature.hierarchy.resize(counter + 1);
			armature.bind_pose.resize(counter + 1);
		}
		armature.hierarchy[counter] = parent_index;
		aiVector3D scale;
		aiVector3D pos;
		aiQuaternion rot;
		node->mTransformation.Decompose(scale, rot, pos);
		armature.bind_pose[counter].pos = Vec3(pos.y, pos.z, pos.x);
		Quat q = Quat(rot.w, rot.x, rot.y, rot.z);
		Vec3 axis;
		float angle;
		q.to_angle_axis(angle, axis);
		Vec3 corrected_axis = Vec3(axis.y, axis.z, axis.x);
		armature.bind_pose[counter].rot = Quat(angle, corrected_axis);
		current_bone_index = counter;
		counter++;
	}
	else if (parent_index == -1)
		current_bone_index = -1;
	else
		current_bone_index = counter;
	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		if (!build_armature(armature, bone_map, node->mChildren[i], current_bone_index, counter))
			return false;
	}

	return true;
}

// Build armature for a skinned mesh
bool build_armature_skinned(const aiScene* scene, const aiMesh* ai_mesh, Mesh& mesh, Armature& armature, Map<int>& bone_map)
{
	if (ai_mesh->HasBones())
	{
		if (scene->mNumMeshes > 1)
		{
			fprintf(stderr, "Animated scene contains a skinned mesh.\n");
			return false;
		}
	
		mesh.inverse_bind_pose.resize(ai_mesh->mNumBones);

		// Build the bone hierarchy.
		// First we fill the bone map with all the bones,
		// so that build_armature can tell which nodes are bones.
		for (unsigned int bone_index = 0; bone_index < ai_mesh->mNumBones; bone_index++)
		{
			aiBone* bone = ai_mesh->mBones[bone_index];
			bone_map[bone->mName.C_Str()] = -1;
		}
		armature.hierarchy.resize(ai_mesh->mNumBones);
		armature.bind_pose.resize(ai_mesh->mNumBones);
		int node_hierarchy_counter = 0;
		if (!build_armature(armature, bone_map, scene->mRootNode, -1, node_hierarchy_counter))
			return false;

		for (unsigned int i = 0; i < ai_mesh->mNumBones; i++)
		{
			aiBone* bone = ai_mesh->mBones[i];
			int bone_index = bone_map[bone->mName.C_Str()];

			aiVector3D ai_position;
			aiQuaternion ai_rotation;
			aiVector3D ai_scale;
			bone->mOffsetMatrix.Decompose(ai_scale, ai_rotation, ai_position);
			
			Vec3 position = Vec3(ai_position.y, ai_position.z, ai_position.x);
			Vec3 scale = Vec3(ai_scale.y, ai_scale.z, ai_scale.x);
			mesh.inverse_bind_pose[bone_index].make_transform(position, scale, Quat::euler(PI * -0.5f, 0, 0));
		}
	}
	
	return true;
}

bool write_armature(const Armature& armature, const std::string& path)
{
	FILE* f = fopen(path.c_str(), "w+b");
	if (f)
	{
		fwrite(&armature.hierarchy.length, sizeof(int), 1, f);
		fwrite(armature.hierarchy.data, sizeof(int), armature.hierarchy.length, f);
		fwrite(armature.bind_pose.data, sizeof(Bone), armature.hierarchy.length, f);
		fclose(f);
		return true;
	}
	else
	{
		fprintf(stderr, "Error: failed to open %s for writing.\n", path.c_str());
		return false;
	}
}

const aiScene* load_blend(ImporterState& state, Assimp::Importer& importer, const std::string& asset_in_path, const std::string& out_folder)
{
	// Export to FBX first
	std::string asset_intermediate_path = out_folder + get_asset_name(asset_in_path) + model_intermediate_extension;

	std::ostringstream cmdbuilder;
	cmdbuilder << "blender " << asset_in_path << " --background --factory-startup --python " << asset_in_folder << "blend_to_fbx.py -- ";
	cmdbuilder << asset_intermediate_path;
	std::string cmd = cmdbuilder.str();

	if (!run_cmd(cmd))
	{
		fprintf(stderr, "Error: failed to export Blender model %s to FBX.\n", asset_in_path.c_str());
		fprintf(stderr, "Command: %s.\n", cmd.c_str());
		state.error = true;
		return 0;
	}

	const aiScene* scene = load_fbx(importer, asset_intermediate_path);

	if (remove(asset_intermediate_path.c_str()))
	{
		fprintf(stderr, "Error: failed to remove intermediate file %s.\n", asset_intermediate_path.c_str());
		state.error = true;
		return 0;
	}

	return scene;
}

void import_meshes(ImporterState& state, const std::string& asset_in_path, const std::string& out_folder)
{
	std::string asset_name = get_asset_name(asset_in_path);
	std::string asset_out_path = out_folder + asset_name + mesh_out_extension;

	long long mtime = filemtime(asset_in_path);
	if (state.rebuild
		|| mtime > asset_mtime(state.cached_manifest.meshes, asset_name)
		|| mtime > asset_mtime(state.cached_manifest.armatures, asset_name)
		|| mtime > asset_mtime(state.cached_manifest.animations, asset_name))
	{
		Assimp::Importer importer;
		const aiScene* scene = load_blend(state, importer, asset_in_path, out_folder);
		map_init(state.manifest.meshes, asset_name);
		map_init(state.manifest.armatures, asset_name);
		map_init(state.manifest.animations, asset_name);
		map_init(state.manifest.bones, asset_name);

		Map<int> bone_map;
		Armature armature;

		for (int i = 0; i < scene->mNumMeshes; i++)
		{
			aiMesh* ai_mesh = scene->mMeshes[i];
			Mesh mesh;
			mesh.color = Vec4(1, 1, 1, 1);
			if (ai_mesh->mMaterialIndex < scene->mNumMaterials)
			{
				aiColor4D color;
				if (scene->mMaterials[ai_mesh->mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
					mesh.color = Vec4(color.r, color.g, color.b, color.a);
			}
			const aiNode* mesh_node = find_mesh_node(scene, scene->mRootNode, ai_mesh);
			std::string mesh_name = get_mesh_name(scene, asset_name, ai_mesh, mesh_node);

			if (load_mesh(ai_mesh, &mesh))
			{
				std::string mesh_out_filename = out_folder + mesh_name + mesh_out_extension;

				map_add(state.manifest.meshes, asset_name, mesh_name, mesh_out_filename);

				printf("%s Indices: %d Vertices: %d\n", mesh_name.c_str(), mesh.indices.length, mesh.vertices.length);

				Array<Array<Vec2>> uv_layers;
				for (int j = 0; j < 8; j++)
				{
					if (ai_mesh->mNumUVComponents[j] == 2)
					{
						Array<Vec2>* uvs = uv_layers.add();
						uvs->reserve(ai_mesh->mNumVertices);
						for (unsigned int k = 0; k < ai_mesh->mNumVertices; k++)
						{
							aiVector3D UVW = ai_mesh->mTextureCoords[j][k];
							uvs->add(Vec2(1.0f - UVW.x, 1.0f - UVW.y));
						}
					}
				}

				Array<std::array<float, MAX_BONE_WEIGHTS> > bone_weights;
				Array<std::array<int, MAX_BONE_WEIGHTS> > bone_indices;
				if (i == 0)
				{
					if (!build_armature_skinned(scene, ai_mesh, mesh, armature, bone_map))
					{
						fprintf(stderr, "Error: failed to process armature for %s.\n", asset_in_path.c_str());
						state.error = true;
						return;
					}

					for (auto bone : bone_map)
					{
						std::string bone_name = asset_name + "_" + bone.first;
						clean_name(bone_name);
						map_add(state.manifest.bones, asset_name, bone_name, bone.second);
					}

					if (armature.hierarchy.length > 0)
					{
						printf("Bones: %d\n", armature.hierarchy.length);
						bone_weights.resize(ai_mesh->mNumVertices);
						bone_indices.resize(ai_mesh->mNumVertices);

						for (unsigned int i = 0; i < ai_mesh->mNumBones; i++)
						{
							aiBone* bone = ai_mesh->mBones[i];
							int bone_index = bone_map[bone->mName.C_Str()];
							for (unsigned int bone_weight_index = 0; bone_weight_index < bone->mNumWeights; bone_weight_index++)
							{
								int vertex_id = bone->mWeights[bone_weight_index].mVertexId;
								float weight = bone->mWeights[bone_weight_index].mWeight;
								for (int weight_index = 0; weight_index < MAX_BONE_WEIGHTS; weight_index++)
								{
									if (bone_weights[vertex_id][weight_index] == 0)
									{
										bone_weights[vertex_id][weight_index] = weight;
										bone_indices[vertex_id][weight_index] = bone_index;
										break;
									}
									else if (weight_index == MAX_BONE_WEIGHTS - 1)
										fprintf(stderr, "Warning: vertex affected by more than %d bones.\n", MAX_BONE_WEIGHTS);
								}
							}
						}
					}
				}

				{
					FILE* f = fopen(mesh_out_filename.c_str(), "w+b");
					if (f)
					{
						fwrite(&mesh.color, sizeof(Vec4), 1, f);
						fwrite(&mesh.bounds_min, sizeof(Vec3), 1, f);
						fwrite(&mesh.bounds_max, sizeof(Vec3), 1, f);
						fwrite(&mesh.indices.length, sizeof(int), 1, f);
						fwrite(mesh.indices.data, sizeof(int), mesh.indices.length, f);
						fwrite(&mesh.vertices.length, sizeof(int), 1, f);
						fwrite(mesh.vertices.data, sizeof(Vec3), mesh.vertices.length, f);
						fwrite(mesh.normals.data, sizeof(Vec3), mesh.vertices.length, f);
						int num_extra_attribs = uv_layers.length + (mesh.inverse_bind_pose.length > 0 ? 2 : 0);
						fwrite(&num_extra_attribs, sizeof(int), 1, f);
						for (int i = 0; i < uv_layers.length; i++)
						{
							RenderDataType type = RenderDataType_Vec2;
							fwrite(&type, sizeof(RenderDataType), 1, f);
							int count = 1;
							fwrite(&count, sizeof(int), 1, f);
							fwrite(uv_layers[i].data, sizeof(Vec2), mesh.vertices.length, f);
						}
						if (mesh.inverse_bind_pose.length > 0)
						{
							RenderDataType type = RenderDataType_Int;
							fwrite(&type, sizeof(RenderDataType), 1, f);
							int count = MAX_BONE_WEIGHTS;
							fwrite(&count, sizeof(int), 1, f);
							fwrite(bone_indices.data, sizeof(int[MAX_BONE_WEIGHTS]), mesh.vertices.length, f);

							type = RenderDataType_Float;
							fwrite(&type, sizeof(RenderDataType), 1, f);
							count = MAX_BONE_WEIGHTS;
							fwrite(&count, sizeof(int), 1, f);
							fwrite(bone_weights.data, sizeof(float[MAX_BONE_WEIGHTS]), mesh.vertices.length, f);
						}
						fwrite(&mesh.inverse_bind_pose.length, sizeof(int), 1, f);
						if (mesh.inverse_bind_pose.length > 0)
							fwrite(mesh.inverse_bind_pose.data, sizeof(Mat4), mesh.inverse_bind_pose.length, f);
						fclose(f);
					}
					else
					{
						fprintf(stderr, "Error: failed to open %s for writing.\n", mesh_out_filename.c_str());
						state.error = true;
						return;
					}
				}
				
				if (armature.hierarchy.length > 0)
				{
					std::string armature_out_filename = asset_out_folder + mesh_name + arm_out_extension;
					map_add(state.manifest.armatures, asset_name, mesh_name, armature_out_filename);
					if (!write_armature(armature, armature_out_filename))
					{
						state.error = true;
						return;
					}
				}
			}
			else
			{
				fprintf(stderr, "Error: failed to load model %s.\n", asset_in_path.c_str());
				state.error = true;
				return;
			}
		}

		for (unsigned int j = 0; j < scene->mNumAnimations; j++)
		{
			aiAnimation* ai_anim = scene->mAnimations[j];
			Animation anim;
			if (load_anim(armature, ai_anim, &anim, bone_map))
			{
				printf("%s Duration: %f Channels: %d\n", ai_anim->mName.C_Str(), anim.duration, anim.channels.length);

				std::string anim_name(ai_anim->mName.C_Str());
				if (anim_name.find("AnimStack") == 0)
				{
					size_t pipe = anim_name.find("|");
					if (pipe != std::string::npos && pipe < anim_name.length() - 1)
						anim_name = anim_name.substr(pipe + 1);
				}
				clean_name(anim_name);

				std::string anim_out_path = asset_out_folder + anim_name + anim_out_extension;

				map_add(state.manifest.animations, asset_name, anim_name, anim_out_path);

				FILE* f = fopen(anim_out_path.c_str(), "w+b");
				if (f)
				{
					fwrite(&anim.duration, sizeof(float), 1, f);
					fwrite(&anim.channels.length, sizeof(int), 1, f);
					for (unsigned int i = 0; i < anim.channels.length; i++)
					{
						Channel* channel = &anim.channels[i];
						fwrite(&channel->bone_index, sizeof(int), 1, f);
						fwrite(&channel->positions.length, sizeof(int), 1, f);
						fwrite(channel->positions.data, sizeof(Keyframe<Vec3>), channel->positions.length, f);
						fwrite(&channel->rotations.length, sizeof(int), 1, f);
						fwrite(channel->rotations.data, sizeof(Keyframe<Quat>), channel->rotations.length, f);
						fwrite(&channel->scales.length, sizeof(int), 1, f);
						fwrite(channel->scales.data, sizeof(Keyframe<Vec3>), channel->scales.length, f);
					}
					fclose(f);
				}
				else
				{
					fprintf(stderr, "Error: failed to open %s for writing.\n", anim_out_path.c_str());
					state.error = true;
					return;
				}
			}
			else
			{
				fprintf(stderr, "Error: failed to load animation %s.\n", ai_anim->mName.C_Str());
				state.error = true;
				return;
			}
		}
	}
	else
	{
		map_copy(state.cached_manifest.meshes, asset_name, state.manifest.meshes);
		map_copy(state.cached_manifest.armatures, asset_name, state.manifest.armatures);
		map_copy(state.cached_manifest.animations, asset_name, state.manifest.animations);
		map_copy(state.cached_manifest.bones, asset_name, state.manifest.bones);
	}
}

void import_level(ImporterState& state, const std::string& asset_in_path, const std::string& out_folder)
{
	import_meshes(state, asset_in_path, out_folder);
	if (state.error)
		return;

	std::string asset_name = get_asset_name(asset_in_path);
	std::string asset_out_path = out_folder + asset_name + level_out_extension;

	map_add(state.manifest.levels, asset_name, asset_out_path);

	long long mtime = filemtime(asset_in_path);
	if (state.rebuild
		|| mtime > asset_mtime(state.cached_manifest.levels, asset_name))
	{
		std::ostringstream cmdbuilder;
		cmdbuilder << "blender " << asset_in_path;
		cmdbuilder << " --background --factory-startup --python " << asset_in_folder << "blend_to_lvl.py -- ";
		cmdbuilder << asset_out_path;
		std::string cmd = cmdbuilder.str();

		if (!run_cmd(cmd))
		{
			fprintf(stderr, "Error: failed to export %s to lvl.\n", asset_in_path.c_str());
			fprintf(stderr, "Command: %s.\n", cmd.c_str());
			state.error = true;
			return;
		}
	}
}

void import_copy(ImporterState& state, Map<std::string>& manifest, const std::string& asset_in_path, const std::string& out_folder, const std::string& extension)
{
	std::string asset_name = get_asset_name(asset_in_path);
	std::string asset_out_path = out_folder + asset_name + extension;
	map_add(manifest, asset_name, asset_out_path);
	long long mtime = filemtime(asset_in_path);
	if (state.rebuild
		|| mtime > asset_mtime(manifest, asset_name))
	{
		printf("%s\n", asset_out_path.c_str());
		if (!cp(asset_in_path, asset_out_path))
		{
			fprintf(stderr, "Error: failed to copy %s to %s.\n", asset_in_path.c_str(), asset_out_path.c_str());
			state.error = true;
		}
	}
}

void import_shader(ImporterState& state, const std::string& asset_in_path, const std::string& out_folder)
{
	std::string asset_name = get_asset_name(asset_in_path);
	std::string asset_out_path = out_folder + asset_name + shader_extension;
	map_add(state.manifest.shaders, asset_name, asset_out_path);
	long long mtime = filemtime(asset_in_path);
	if (state.rebuild
		|| mtime > asset_mtime(state.cached_manifest.shaders, asset_name))
	{
		printf("%s\n", asset_out_path.c_str());

		FILE* f = fopen(asset_in_path.c_str(), "rb");
		if (!f)
		{
			fprintf(stderr, "Error: failed to open %s.\n", asset_in_path.c_str());
			state.error = true;
			return;
		}

		fseek(f, 0, SEEK_END);
		long fsize = ftell(f);
		fseek(f, 0, SEEK_SET);

		Array<char> code;
		code.resize(fsize + 1); // One extra character for the null terminator
		fread(code.data, fsize, 1, f);
		fclose(f);

		// Create the shaders
		GLuint vertex_id = glCreateShader(GL_VERTEX_SHADER);
		GLuint frag_id = glCreateShader(GL_FRAGMENT_SHADER);

		// Compile Vertex Shader
		char const* vertex_code[] = { "#version 330 core\n#define VERTEX\n", code.data };
		const GLint vertex_code_length[] = { 33, (GLint)code.length };
		glShaderSource(vertex_id, 2, vertex_code, vertex_code_length);
		glCompileShader(vertex_id);

		// Check Vertex Shader
		GLint result;
		glGetShaderiv(vertex_id, GL_COMPILE_STATUS, &result);
		int msg_length;
		glGetShaderiv(vertex_id, GL_INFO_LOG_LENGTH, &msg_length);
		if (msg_length > 1)
		{
			Array<char> msg(msg_length);
			glGetShaderInfoLog(vertex_id, msg_length, NULL, msg.data);
			fprintf(stderr, "Vertex shader error in '%s': %s\n", asset_in_path.c_str(), msg.data);
			state.error = true;
			return;
		}

		// Compile Fragment Shader
		const char* frag_code[2] = { "#version 330 core\n", code.data };
		const GLint frag_code_length[] = { 18, (GLint)code.length };
		glShaderSource(frag_id, 2, frag_code, frag_code_length);
		glCompileShader(frag_id);

		// Check Fragment Shader
		glGetShaderiv(frag_id, GL_COMPILE_STATUS, &result);
		glGetShaderiv(frag_id, GL_INFO_LOG_LENGTH, &msg_length);
		if (msg_length > 1)
		{
			Array<char> msg(msg_length + 1);
			glGetShaderInfoLog(frag_id, msg_length, NULL, msg.data);
			fprintf(stderr, "Fragment shader error in '%s': %s\n", asset_in_path.c_str(), msg.data);
			state.error = true;
			return;
		}

		// Link the program
		GLuint program_id = glCreateProgram();
		glAttachShader(program_id, vertex_id);
		glAttachShader(program_id, frag_id);
		glLinkProgram(program_id);

		glDeleteShader(vertex_id);
		glDeleteShader(frag_id);

		// Get uniforms
		GLint uniform_count;
		glGetProgramiv(program_id, GL_ACTIVE_UNIFORMS, &uniform_count);
		for (int i = 0; i < uniform_count; i++)
		{
			char name[128 + 1];
			memset(name, 0, 128 + 1);
			int name_length;
			glGetActiveUniformName(program_id, i, 128, &name_length, name);

			char* bracket_character = strchr(name, '[');
			if (bracket_character)
				*bracket_character = '\0'; // Remove array brackets

			map_add(state.manifest.uniforms, asset_name, name, std::string(name));
		}

		glDeleteProgram(program_id);

		if (!cp(asset_in_path, asset_out_path))
		{
			fprintf(stderr, "Error: failed to copy %s to %s.\n", asset_in_path.c_str(), asset_out_path.c_str());
			state.error = true;
			return;
		}
	}
	else
		map_copy(state.cached_manifest.uniforms, asset_name, state.manifest.uniforms);
}

bool load_font(const aiScene* scene, Font& font)
{
	int current_mesh_vertex = 0;
	int current_mesh_index = 0;

	// Determine tallest character
	float scale = 1.0f;
	{
		float min_height = FLT_MAX, max_height = FLT_MIN;
		for (unsigned int i = 0; i < scene->mNumMeshes; i++)
		{
			aiMesh* ai_mesh = scene->mMeshes[i];
			font.vertices.reserve(current_mesh_vertex + ai_mesh->mNumVertices);
			for (unsigned int j = 0; j < ai_mesh->mNumVertices; j++)
			{
				aiVector3D pos = ai_mesh->mVertices[j];
				min_height = fmin(min_height, pos.y);
				max_height = fmax(max_height, pos.y);
			}
		}
		if (max_height > min_height)
			scale = 1.0f / (max_height - min_height);
	}

	for (unsigned int i = 0; i < scene->mNumMeshes; i++)
	{
		aiMesh* ai_mesh = scene->mMeshes[i];
		font.vertices.reserve(current_mesh_vertex + ai_mesh->mNumVertices);
		Vec2 min_vertex(FLT_MAX, FLT_MAX), max_vertex(FLT_MIN, FLT_MIN);
		for (unsigned int j = 0; j < ai_mesh->mNumVertices; j++)
		{
			aiVector3D pos = ai_mesh->mVertices[j];
			Vec3 vertex = Vec3(pos.x * scale, pos.y * scale, pos.z * scale);
			min_vertex.x = fmin(min_vertex.x, vertex.x);
			min_vertex.y = fmin(min_vertex.y, vertex.y);
			max_vertex.x = fmax(max_vertex.x, vertex.x);
			max_vertex.y = fmax(max_vertex.y, vertex.y);
			font.vertices.add(vertex);
		}

		font.indices.reserve(current_mesh_index + ai_mesh->mNumFaces * 3);
		for (unsigned int j = 0; j < ai_mesh->mNumFaces; j++)
		{
			// Assume the model has only triangles.
			font.indices.add(current_mesh_vertex + ai_mesh->mFaces[j].mIndices[0]);
			font.indices.add(current_mesh_vertex + ai_mesh->mFaces[j].mIndices[1]);
			font.indices.add(current_mesh_vertex + ai_mesh->mFaces[j].mIndices[2]);
		}

		Font::Character c;
		c.code = ai_mesh->mName.data[0];
		c.vertex_start = current_mesh_vertex;
		c.vertex_count = ai_mesh->mNumVertices;
		c.index_start = current_mesh_index;
		c.index_count = ai_mesh->mNumFaces * 3;
		c.min = min_vertex;
		c.max = max_vertex;
		font.characters.add(c);

		current_mesh_vertex = font.vertices.length;
		current_mesh_index = font.indices.length;
	}
	return true;
}

void import_font(ImporterState& state, const std::string& asset_in_path, const std::string& out_folder)
{
	std::string asset_name = get_asset_name(asset_in_path);
	std::string asset_out_path = out_folder + asset_name + font_out_extension;

	map_add(state.manifest.fonts, asset_name, asset_out_path);

	long long mtime = filemtime(asset_in_path);
	if (state.rebuild
		|| mtime > asset_mtime(state.cached_manifest.fonts, asset_name))
	{
		std::string asset_intermediate_path = asset_out_folder + asset_name + model_intermediate_extension;

		// Export to FBX first
		std::ostringstream cmdbuilder;
		cmdbuilder << "blender --background --factory-startup --python " << asset_in_folder << "ttf_to_fbx.py -- ";
		cmdbuilder << asset_in_path << " " << asset_intermediate_path;
		std::string cmd = cmdbuilder.str();

		if (!run_cmd(cmd))
		{
			fprintf(stderr, "Error: failed to export TTF font %s to FBX.\n", asset_in_path.c_str());
			fprintf(stderr, "Command: %s.\n", cmd.c_str());
			state.error = true;
			return;
		}

		Assimp::Importer importer;
		const aiScene* scene = load_fbx(importer, asset_intermediate_path);

		remove(asset_intermediate_path.c_str());

		Font font;
		if (load_font(scene, font))
		{
			FILE* f = fopen(asset_out_path.c_str(), "w+b");
			if (f)
			{
				fwrite(&font.vertices.length, sizeof(int), 1, f);
				fwrite(font.vertices.data, sizeof(Vec3), font.vertices.length, f);
				fwrite(&font.indices.length, sizeof(int), 1, f);
				fwrite(font.indices.data, sizeof(int), font.indices.length, f);
				fwrite(&font.characters.length, sizeof(int), 1, f);
				fwrite(font.characters.data, sizeof(Font::Character), font.characters.length, f);
				fclose(f);
			}
			else
			{
				fprintf(stderr, "Error: failed to open %s for writing.\n", asset_out_path.c_str());
				state.error = true;
				return;
			}
		}
		else
		{
			fprintf(stderr, "Error: failed to load font %s.\n", asset_in_path.c_str());
			state.error = true;
			return;
		}
	}
}

FILE* open_asset_header(const char* path)
{
	FILE* f = fopen(path, "w+");
	if (!f)
	{
		fprintf(stderr, "Error: failed to open asset header file %s for writing.\n", path);
		return 0;
	}
	fprintf(f, "#pragma once\n#include \"types.h\"\n\nnamespace VI\n{\n\nnamespace Asset\n{\n");
	return f;
}

void close_asset_header(FILE* f)
{
	fprintf(f, "}\n\n}");
	fclose(f);
}

int proc(int argc, char* argv[])
{
	const char* manifest_path = ".manifest";
	const char* asset_src_path = "../src/asset/values.cpp";
	const char* mesh_header_path = "../src/asset/mesh.h";
	const char* animation_header_path = "../src/asset/animation.h";
	const char* texture_header_path = "../src/asset/texture.h";
	const char* shader_header_path = "../src/asset/shader.h";
	const char* armature_header_path = "../src/asset/armature.h";
	const char* font_header_path = "../src/asset/font.h";
	const char* level_header_path = "../src/asset/level.h";

	// Initialise SDL
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		fprintf(stderr, "Failed to initialize SDL\n");
		return 1;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

	SDL_Window* window = SDL_CreateWindow("", 0, 0, 1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);

	// Open a window and create its OpenGL context
	if (!window)
	{
		fprintf(stderr, "Failed to open GLFW window. Most likely your GPU is out of date!\n");
		return exit_error();
	}
	SDL_GLContext context = SDL_GL_CreateContext(window);
	glewExperimental = true; // Needed for core profile
	if (glewInit() != GLEW_OK)
	{
		fprintf(stderr, "Failed to initialize GLEW\n");
		return exit_error();
	}

	ImporterState state;
	state.manifest_mtime = filemtime(manifest_path);

	if (!manifest_read(manifest_path, state.cached_manifest))
		state.rebuild = true;

	{
		DIR* dir = opendir(asset_in_folder);
		if (!dir)
		{
			fprintf(stderr, "Failed to open asset directory.\n");
			return exit_error();
		}
		struct dirent* entry;
		while ((entry = readdir(dir)))
		{
			if (entry->d_type != DT_REG)
				continue; // Not a file

			std::string asset_in_path = asset_in_folder + std::string(entry->d_name);

			if (has_extension(asset_in_path, texture_extension))
				import_copy(state, state.manifest.textures, asset_in_path, asset_out_folder, texture_extension);
			else if (has_extension(asset_in_path, shader_extension))
				import_shader(state, asset_in_path, asset_out_folder);
			else if (has_extension(asset_in_path, model_in_extension))
				import_meshes(state, asset_in_path, asset_out_folder);
			else if (has_extension(asset_in_path, font_in_extension) || has_extension(asset_in_path, font_in_extension_2))
				import_font(state, asset_in_path, asset_out_folder);
			if (state.error)
				break;
		}
		closedir(dir);
	}

	if (state.error)
		return exit_error();

	{
		DIR* dir = opendir(level_in_folder);
		if (!dir)
		{
			fprintf(stderr, "Failed to open level directory.\n");
			return exit_error();
		}
		struct dirent* entry;
		while ((entry = readdir(dir)))
		{
			if (entry->d_type != DT_REG)
				continue; // Not a file

			std::string asset_in_path = level_in_folder + std::string(entry->d_name);

			if (has_extension(asset_in_path, model_in_extension))
				import_level(state, asset_in_path, level_out_folder);
			if (state.error)
				break;
		}
		closedir(dir);
	}

	if (state.error)
		return exit_error();
	
	bool modified = !manifests_equal(state.cached_manifest, state.manifest);
	if (state.rebuild || modified)
	{
		if (!manifest_write(state.manifest, manifest_path))
			return exit_error();
	}

	{
		Map<std::string> flattened_meshes;
		map_flatten(state.manifest.meshes, flattened_meshes);
		Map<std::string> flattened_uniforms;
		map_flatten(state.manifest.uniforms, flattened_uniforms);
		Map<std::string> flattened_animations;
		map_flatten(state.manifest.animations, flattened_animations);
		Map<std::string> flattened_armatures;
		map_flatten(state.manifest.armatures, flattened_armatures);
		Map<int> flattened_bones;
		map_flatten(state.manifest.bones, flattened_bones);
		Map<int> flattened_model_indices;
		{
			int index = 0;
			for (auto i : flattened_meshes)
			{
				flattened_model_indices[i.first] = index;
				index++;
			}
		}

		if (state.rebuild
			|| !maps_equal2(state.manifest.meshes, state.cached_manifest.meshes)
			|| !maps_equal(state.manifest.levels, state.cached_manifest.levels)
			|| filemtime(mesh_header_path) == 0)
		{
			printf("Writing mesh header\n");
			FILE* f = open_asset_header(mesh_header_path);
			if (!f)
				return exit_error();
			write_asset_header(f, "Mesh", flattened_meshes);

			int max_mesh_count = 0;
			for (auto model : state.manifest.meshes)
				max_mesh_count = model.second.size() > max_mesh_count ? model.second.size() : max_mesh_count;
			fprintf(f, "\tconst AssetID mesh_refs[%lu][%d] =\n\t{\n", state.manifest.levels.size(), max_mesh_count);

			for (auto level : state.manifest.levels)
			{
				fprintf(f, "\t\t{\n");
				Map<std::string>& meshes = state.manifest.meshes[level.first];
				for (auto mesh : meshes)
				{
					int mesh_asset_id = flattened_model_indices[mesh.first];
					fprintf(f, "\t\t\t%d,\n", mesh_asset_id);
				}
				fprintf(f, "\t\t},\n");
			}
			fprintf(f, "\t};\n");

			close_asset_header(f);
		}

		if (state.rebuild
			|| !maps_equal2(state.manifest.animations, state.cached_manifest.animations)
			|| filemtime(animation_header_path) == 0)
		{
			printf("Writing animation header\n");
			FILE* f = open_asset_header(animation_header_path);
			if (!f)
				return exit_error();
			write_asset_header(f, "Animation", flattened_animations);
			close_asset_header(f);
		}

		if (state.rebuild
			|| !maps_equal2(state.manifest.armatures, state.cached_manifest.armatures)
			|| !maps_equal2(state.manifest.bones, state.cached_manifest.bones)
			|| filemtime(armature_header_path) == 0)
		{
			printf("Writing armature header\n");
			FILE* f = open_asset_header(armature_header_path);
			if (!f)
				return exit_error();

			write_asset_header(f, "Armature", flattened_armatures);
			write_asset_header(f, "Bone", flattened_bones);

			close_asset_header(f);
		}

		if (state.rebuild
			|| !maps_equal(state.manifest.textures, state.cached_manifest.textures)
			|| filemtime(texture_header_path) == 0)
		{
			printf("Writing texture header\n");
			FILE* f = open_asset_header(texture_header_path);
			if (!f)
				return exit_error();

			write_asset_header(f, "Texture", state.manifest.textures);
			close_asset_header(f);
		}

		if (state.rebuild
			|| !maps_equal2(state.manifest.uniforms, state.cached_manifest.uniforms)
			|| !maps_equal(state.manifest.shaders, state.cached_manifest.shaders)
			|| filemtime(shader_header_path) == 0)
		{
			printf("Writing shader header\n");
			FILE* f = open_asset_header(shader_header_path);
			if (!f)
				return exit_error();
			
			write_asset_header(f, "Uniform", flattened_uniforms);
			write_asset_header(f, "Shader", state.manifest.shaders);
			close_asset_header(f);
		}

		if (state.rebuild
			|| !maps_equal(state.manifest.fonts, state.cached_manifest.fonts)
			|| filemtime(font_header_path) == 0)
		{
			printf("Writing font header\n");
			FILE* f = open_asset_header(font_header_path);
			if (!f)
				return exit_error();

			write_asset_header(f, "Font", state.manifest.fonts);
			close_asset_header(f);
		}

		if (state.rebuild
			|| !maps_equal(state.manifest.levels, state.cached_manifest.levels)
			|| filemtime(level_header_path) == 0)
		{
			printf("Writing level header\n");
			FILE* f = open_asset_header(level_header_path);
			if (!f)
				return exit_error();

			write_asset_header(f, "Level", state.manifest.levels);

			close_asset_header(f);
		}

		if (state.rebuild || modified || filemtime(asset_src_path) < state.manifest_mtime)
		{
			printf("Writing asset values\n");
			FILE* f = fopen(asset_src_path, "w+");
			if (!f)
			{
				fprintf(stderr, "Error: failed to open asset source file %s for writing.\n", asset_src_path);
				return exit_error();
			}
			fprintf(f, "#include \"animation.h\"\n");
			fprintf(f, "#include \"armature.h\"\n");
			fprintf(f, "#include \"font.h\"\n");
			fprintf(f, "#include \"mesh.h\"\n");
			fprintf(f, "#include \"shader.h\"\n");
			fprintf(f, "#include \"level.h\"\n");
			fprintf(f, "#include \"lookup.h\"\n");
			fprintf(f, "\nnamespace VI\n{ \n\n");
			write_asset_source(f, "Mesh", flattened_meshes);
			write_asset_source(f, "Animation", flattened_animations);
			write_asset_source(f, "Armature", flattened_armatures);
			write_asset_source(f, "Texture", state.manifest.textures);
			write_asset_source(f, "Shader", state.manifest.shaders);
			write_asset_source(f, "Uniform", flattened_uniforms);
			write_asset_source(f, "Font", state.manifest.fonts);
			write_asset_source(f, "Level", state.manifest.levels);

			fprintf(f, "\n}");
			fclose(f);
		}
	}

	SDL_GL_DeleteContext(context);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}

}

int main(int argc, char* argv[])
{
	return VI::proc(argc, argv);
}
