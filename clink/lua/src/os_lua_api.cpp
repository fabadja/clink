// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#include <core/base.h>
#include <core/globber.h>
#include <core/os.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/str.h>
#include <process/process.h>

//------------------------------------------------------------------------------
extern setting_bool g_glob_hidden;
extern setting_bool g_glob_system;



//------------------------------------------------------------------------------
static const char* get_string(lua_State* state, int index)
{
    if (lua_gettop(state) < index || !lua_isstring(state, index))
        return nullptr;

    return lua_tostring(state, index);
}

//------------------------------------------------------------------------------
static int set_current_dir(lua_State* state)
{
    bool ok = false;
    if (const char* dir = get_string(state, 1))
        ok = os::set_current_dir(dir);

    lua_pushboolean(state, (ok == true));
    return 1;
}

//------------------------------------------------------------------------------
static int get_current_dir(lua_State* state)
{
    str<288> dir;
    os::get_current_dir(dir);

    lua_pushstring(state, dir.c_str());
    return 1;
}

//------------------------------------------------------------------------------
static int make_dir(lua_State* state)
{
    bool ok = false;
    if (const char* dir = get_string(state, 1))
        ok = os::make_dir(dir);

    lua_pushboolean(state, (ok == true));
    return 1;
}

//------------------------------------------------------------------------------
static int remove_dir(lua_State* state)
{
    bool ok = false;
    if (const char* dir = get_string(state, 1))
        ok = os::remove_dir(dir);

    lua_pushboolean(state, (ok == true));
    return 1;
}

//------------------------------------------------------------------------------
static int is_dir(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    lua_pushboolean(state, (os::get_path_type(path) == os::path_type_dir));
    return 1;
}

//------------------------------------------------------------------------------
static int is_file(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    lua_pushboolean(state, (os::get_path_type(path) == os::path_type_file));
    return 1;
}

//------------------------------------------------------------------------------
static int unlink(lua_State* state)
{
    const char* path = get_string(state, 1);
    if (path == nullptr)
        return 0;

    if (os::unlink(path))
    {
        lua_pushboolean(state, 1);
        return 1;
    }

    lua_pushnil(state);
    lua_pushstring(state, "error");
    lua_pushinteger(state, 1);
    return 3;
}

//------------------------------------------------------------------------------
static int move(lua_State* state)
{
    const char* src = get_string(state, 1);
    const char* dest = get_string(state, 2);
    if (src != nullptr && dest != nullptr && os::move(src, dest))
    {
        lua_pushboolean(state, 1);
        return 1;
    }

    lua_pushnil(state);
    lua_pushstring(state, "error");
    lua_pushinteger(state, 1);
    return 3;
}

//------------------------------------------------------------------------------
static int copy(lua_State* state)
{
    const char* src = get_string(state, 1);
    const char* dest = get_string(state, 2);
    if (src == nullptr || dest == nullptr)
        return 0;

    lua_pushboolean(state, (os::copy(src, dest) == true));
    return 1;
}

//------------------------------------------------------------------------------
static int glob_impl(lua_State* state, bool dirs_only)
{
    const char* mask = get_string(state, 1);
    if (mask == nullptr)
        return 0;

    lua_createtable(state, 0, 0);

    globber globber(mask);
    globber.files(!dirs_only);
    globber.hidden(g_glob_hidden.get());
    globber.system(g_glob_system.get());

    int i = 1;
    str<288> file;
    while (globber.next(file, false))
    {
        lua_pushstring(state, file.c_str());
        lua_rawseti(state, -2, i++);
    }

    return 1;
}

//------------------------------------------------------------------------------
static int glob_dirs(lua_State* state)
{
    return glob_impl(state, true);
}

//------------------------------------------------------------------------------
static int glob_files(lua_State* state)
{
    return glob_impl(state, false);
}

//------------------------------------------------------------------------------
static int get_env(lua_State* state)
{
    const char* name = get_string(state, 1);
    if (name == nullptr)
        return 0;

    str<128> value;
    if (!os::get_env(name, value))
        return 0;

    lua_pushstring(state, value.c_str());
    return 1;
}

//------------------------------------------------------------------------------
static int get_env_names(lua_State* state)
{
    lua_createtable(state, 0, 0);

    char* root = GetEnvironmentStrings();
    if (root == nullptr)
        return 1;

    char* strings = root;
    int i = 1;
    while (*strings)
    {
        // Skip env vars that start with a '='. They're hidden ones.
        if (*strings == '=')
        {
            strings += strlen(strings) + 1;
            continue;
        }

        char* eq = strchr(strings, '=');
        if (eq == nullptr)
            break;

        *eq = '\0';

        lua_pushstring(state, strings);
        lua_rawseti(state, -2, i++);

        ++eq;
        strings = eq + strlen(eq) + 1;
    }

    FreeEnvironmentStrings(root);
    return 1;
}

//------------------------------------------------------------------------------
static int get_host(lua_State* state)
{
    str<280> host;
    if (process().get_file_name(host))
        return 0;

    lua_pushstring(state, host.c_str());
    return 1;
}

//------------------------------------------------------------------------------
static int get_aliases(lua_State* state)
{
    lua_createtable(state, 0, 0);

#if !defined(__MINGW32__) && !defined(__MINGW64__)
    str<280> path;
    if (!process().get_file_name(path))
        return 1;

    // Not const because Windows' alias API won't accept it.
    char* name = (char*)path::get_name(path.c_str());

    // Get the aliases (aka. doskey macros).
    int buffer_size = GetConsoleAliasesLength(name);
    if (buffer_size == 0)
        return 1;

    str<> buffer;
    buffer.reserve(buffer_size);
    if (GetConsoleAliases(buffer.data(), buffer.size(), name) == 0)
        return 1;

    // Parse the result into a lua table.
    const char* alias = buffer.c_str();
    for (int i = 1; int(alias - buffer.c_str()) < buffer_size; ++i)
    {
        const char* c = strchr(alias, '=');
        if (c == nullptr)
            break;

        lua_pushlstring(state, alias, size_t(c - alias));
        lua_rawseti(state, -2, i++);

        ++c;
        alias = c + strlen(c) + 1;
    }

#endif // __MINGW32__
    return 1;
}

//------------------------------------------------------------------------------
void os_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "chdir",       &set_current_dir },
        { "getcwd",      &get_current_dir },
        { "mkdir",       &make_dir },
        { "rmdir",       &remove_dir },
        { "isdir",       &is_dir },
        { "isfile",      &is_file },
        { "unlink",      &unlink },
        { "move",        &move },
        { "copy",        &copy },
        { "globdirs",    &glob_dirs },
        { "globfiles",   &glob_files },
        { "getenv",      &get_env },
        { "getenvnames", &get_env_names },
        { "gethost",     &get_host },
        { "getaliases",  &get_aliases },
    };

    lua_State* state = lua.get_state();

    lua_getglobal(state, "os");

    for (int i = 0; i < sizeof_array(methods); ++i)
    {
        lua_pushcfunction(state, methods[i].method);
        lua_setfield(state, -2, methods[i].name);
    }

    lua_pop(state, 1);
}
