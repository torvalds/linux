/* Copyright (c) 2014, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file lua ucl bindings
 */

#include "ucl.h"
#include "ucl_internal.h"
#include "lua_ucl.h"
#include <strings.h>

/***
 * @module ucl
 * This lua module allows to parse objects from strings and to store data into
 * ucl objects. It uses `libucl` C library to parse and manipulate with ucl objects.
 * @example
local ucl = require("ucl")

local parser = ucl.parser()
local res,err = parser:parse_string('{key=value}')

if not res then
	print('parser error: ' .. err)
else
	local obj = parser:get_object()
	local got = ucl.to_format(obj, 'json')
endif

local table = {
  str = 'value',
  num = 100500,
  null = ucl.null,
  func = function ()
    return 'huh'
  end
}

print(ucl.to_format(table, 'ucl'))
-- Output:
--[[
num = 100500;
str = "value";
null = null;
func = "huh";
--]]
 */

#define PARSER_META "ucl.parser.meta"
#define EMITTER_META "ucl.emitter.meta"
#define NULL_META "null.emitter.meta"
#define OBJECT_META "ucl.object.meta"

static int ucl_object_lua_push_array (lua_State *L, const ucl_object_t *obj);
static int ucl_object_lua_push_scalar (lua_State *L, const ucl_object_t *obj, bool allow_array);
static ucl_object_t* ucl_object_lua_fromtable (lua_State *L, int idx);
static ucl_object_t* ucl_object_lua_fromelt (lua_State *L, int idx);

static void *ucl_null;

/**
 * Push a single element of an object to lua
 * @param L
 * @param key
 * @param obj
 */
static void
ucl_object_lua_push_element (lua_State *L, const char *key,
		const ucl_object_t *obj)
{
	lua_pushstring (L, key);
	ucl_object_push_lua (L, obj, true);
	lua_settable (L, -3);
}

static void
lua_ucl_userdata_dtor (void *ud)
{
	struct ucl_lua_funcdata *fd = (struct ucl_lua_funcdata *)ud;

	luaL_unref (fd->L, LUA_REGISTRYINDEX, fd->idx);
	if (fd->ret != NULL) {
		free (fd->ret);
	}
	free (fd);
}

static const char *
lua_ucl_userdata_emitter (void *ud)
{
	struct ucl_lua_funcdata *fd = (struct ucl_lua_funcdata *)ud;
	const char *out = "";

	lua_rawgeti (fd->L, LUA_REGISTRYINDEX, fd->idx);

	lua_pcall (fd->L, 0, 1, 0);
	out = lua_tostring (fd->L, -1);

	if (out != NULL) {
		/* We need to store temporary string in a more appropriate place */
		if (fd->ret) {
			free (fd->ret);
		}
		fd->ret = strdup (out);
	}

	lua_settop (fd->L, 0);

	return fd->ret;
}

/**
 * Push a single object to lua
 * @param L
 * @param obj
 * @return
 */
static int
ucl_object_lua_push_object (lua_State *L, const ucl_object_t *obj,
		bool allow_array)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	int nelt = 0;

	if (allow_array && obj->next != NULL) {
		/* Actually we need to push this as an array */
		return ucl_object_lua_push_array (L, obj);
	}

	/* Optimize allocation by preallocation of table */
	while (ucl_object_iterate (obj, &it, true) != NULL) {
		nelt ++;
	}

	lua_createtable (L, 0, nelt);
	it = NULL;

	while ((cur = ucl_object_iterate (obj, &it, true)) != NULL) {
		ucl_object_lua_push_element (L, ucl_object_key (cur), cur);
	}

	return 1;
}

/**
 * Push an array to lua as table indexed by integers
 * @param L
 * @param obj
 * @return
 */
static int
ucl_object_lua_push_array (lua_State *L, const ucl_object_t *obj)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it;
	int i = 1, nelt = 0;

	if (obj->type == UCL_ARRAY) {
		nelt = obj->len;
		it = ucl_object_iterate_new (obj);
		lua_createtable (L, nelt, 0);

		while ((cur = ucl_object_iterate_safe (it, true))) {
			ucl_object_push_lua (L, cur, false);
			lua_rawseti (L, -2, i);
			i ++;
		}

		ucl_object_iterate_free (it);
	}
	else {
		/* Optimize allocation by preallocation of table */
		LL_FOREACH (obj, cur) {
			nelt ++;
		}

		lua_createtable (L, nelt, 0);

		LL_FOREACH (obj, cur) {
			ucl_object_push_lua (L, cur, false);
			lua_rawseti (L, -2, i);
			i ++;
		}
	}

	return 1;
}

/**
 * Push a simple object to lua depending on its actual type
 */
static int
ucl_object_lua_push_scalar (lua_State *L, const ucl_object_t *obj,
		bool allow_array)
{
	struct ucl_lua_funcdata *fd;

	if (allow_array && obj->next != NULL) {
		/* Actually we need to push this as an array */
		return ucl_object_lua_push_array (L, obj);
	}

	switch (obj->type) {
	case UCL_BOOLEAN:
		lua_pushboolean (L, ucl_obj_toboolean (obj));
		break;
	case UCL_STRING:
		lua_pushstring (L, ucl_obj_tostring (obj));
		break;
	case UCL_INT:
#if LUA_VERSION_NUM >= 501
		lua_pushinteger (L, ucl_obj_toint (obj));
#else
		lua_pushnumber (L, ucl_obj_toint (obj));
#endif
		break;
	case UCL_FLOAT:
	case UCL_TIME:
		lua_pushnumber (L, ucl_obj_todouble (obj));
		break;
	case UCL_NULL:
		lua_getfield (L, LUA_REGISTRYINDEX, "ucl.null");
		break;
	case UCL_USERDATA:
		fd = (struct ucl_lua_funcdata *)obj->value.ud;
		lua_rawgeti (L, LUA_REGISTRYINDEX, fd->idx);
		break;
	default:
		lua_pushnil (L);
		break;
	}

	return 1;
}

/***
 * @function ucl_object_push_lua(L, obj, allow_array)
 * This is a `C` function to push `UCL` object as lua variable. This function
 * converts `obj` to lua representation using the following conversions:
 *
 * - *scalar* values are directly presented by lua objects
 * - *userdata* values are converted to lua function objects using `LUA_REGISTRYINDEX`,
 * this can be used to pass functions from lua to c and vice-versa
 * - *arrays* are converted to lua tables with numeric indicies suitable for `ipairs` iterations
 * - *objects* are converted to lua tables with string indicies
 * @param {lua_State} L lua state pointer
 * @param {ucl_object_t} obj object to push
 * @param {bool} allow_array expand implicit arrays (should be true for all but partial arrays)
 * @return {int} `1` if an object is pushed to lua
 */
int
ucl_object_push_lua (lua_State *L, const ucl_object_t *obj, bool allow_array)
{
	switch (obj->type) {
	case UCL_OBJECT:
		return ucl_object_lua_push_object (L, obj, allow_array);
	case UCL_ARRAY:
		return ucl_object_lua_push_array (L, obj);
	default:
		return ucl_object_lua_push_scalar (L, obj, allow_array);
	}
}

/**
 * Parse lua table into object top
 * @param L
 * @param top
 * @param idx
 */
static ucl_object_t *
ucl_object_lua_fromtable (lua_State *L, int idx)
{
	ucl_object_t *obj, *top = NULL;
	size_t keylen;
	const char *k;
	bool is_array = true;
	int max = INT_MIN;

	if (idx < 0) {
		/* For negative indicies we want to invert them */
		idx = lua_gettop (L) + idx + 1;
	}
	/* Check for array */
	lua_pushnil (L);
	while (lua_next (L, idx) != 0) {
		if (lua_type (L, -2) == LUA_TNUMBER) {
			double num = lua_tonumber (L, -2);
			if (num == (int)num) {
				if (num > max) {
					max = num;
				}
			}
			else {
				/* Keys are not integer */
				lua_pop (L, 2);
				is_array = false;
				break;
			}
		}
		else {
			/* Keys are not numeric */
			lua_pop (L, 2);
			is_array = false;
			break;
		}
		lua_pop (L, 1);
	}

	/* Table iterate */
	if (is_array) {
		int i;

		top = ucl_object_typed_new (UCL_ARRAY);
		for (i = 1; i <= max; i ++) {
			lua_pushinteger (L, i);
			lua_gettable (L, idx);
			obj = ucl_object_lua_fromelt (L, lua_gettop (L));
			if (obj != NULL) {
				ucl_array_append (top, obj);
			}
			lua_pop (L, 1);
		}
	}
	else {
		lua_pushnil (L);
		top = ucl_object_typed_new (UCL_OBJECT);
		while (lua_next (L, idx) != 0) {
			/* copy key to avoid modifications */
			k = lua_tolstring (L, -2, &keylen);
			obj = ucl_object_lua_fromelt (L, lua_gettop (L));

			if (obj != NULL) {
				ucl_object_insert_key (top, obj, k, keylen, true);
			}
			lua_pop (L, 1);
		}
	}

	return top;
}

/**
 * Get a single element from lua to object obj
 * @param L
 * @param obj
 * @param idx
 */
static ucl_object_t *
ucl_object_lua_fromelt (lua_State *L, int idx)
{
	int type;
	double num;
	ucl_object_t *obj = NULL;
	struct ucl_lua_funcdata *fd;

	type = lua_type (L, idx);

	switch (type) {
	case LUA_TSTRING:
		obj = ucl_object_fromstring_common (lua_tostring (L, idx), 0, 0);
		break;
	case LUA_TNUMBER:
		num = lua_tonumber (L, idx);
		if (num == (int64_t)num) {
			obj = ucl_object_fromint (num);
		}
		else {
			obj = ucl_object_fromdouble (num);
		}
		break;
	case LUA_TBOOLEAN:
		obj = ucl_object_frombool (lua_toboolean (L, idx));
		break;
	case LUA_TUSERDATA:
		if (lua_topointer (L, idx) == ucl_null) {
			obj = ucl_object_typed_new (UCL_NULL);
		}
		break;
	case LUA_TTABLE:
	case LUA_TFUNCTION:
	case LUA_TTHREAD:
		if (luaL_getmetafield (L, idx, "__gen_ucl")) {
			if (lua_isfunction (L, -1)) {
				lua_settop (L, 3); /* gen, obj, func */
				lua_insert (L, 1); /* func, gen, obj */
				lua_insert (L, 2); /* func, obj, gen */
				lua_call(L, 2, 1);
				obj = ucl_object_lua_fromelt (L, 1);
			}
			lua_pop (L, 2);
		}
		else {
			if (type == LUA_TTABLE) {
				obj = ucl_object_lua_fromtable (L, idx);
			}
			else if (type == LUA_TFUNCTION) {
				fd = malloc (sizeof (*fd));
				if (fd != NULL) {
					lua_pushvalue (L, idx);
					fd->L = L;
					fd->ret = NULL;
					fd->idx = luaL_ref (L, LUA_REGISTRYINDEX);

					obj = ucl_object_new_userdata (lua_ucl_userdata_dtor,
							lua_ucl_userdata_emitter, (void *)fd);
				}
			}
		}
		break;
	}

	return obj;
}

/**
 * @function ucl_object_lua_import(L, idx)
 * Extracts ucl object from lua variable at `idx` position,
 * @see ucl_object_push_lua for conversion definitions
 * @param {lua_state} L lua state machine pointer
 * @param {int} idx index where the source variable is placed
 * @return {ucl_object_t} new ucl object extracted from lua variable. Reference count of this object is 1,
 * this object thus needs to be unref'ed after usage.
 */
ucl_object_t *
ucl_object_lua_import (lua_State *L, int idx)
{
	ucl_object_t *obj;
	int t;

	t = lua_type (L, idx);
	switch (t) {
	case LUA_TTABLE:
		obj = ucl_object_lua_fromtable (L, idx);
		break;
	default:
		obj = ucl_object_lua_fromelt (L, idx);
		break;
	}

	return obj;
}

static int
lua_ucl_to_string (lua_State *L, const ucl_object_t *obj, enum ucl_emitter type)
{
	unsigned char *result;

	result = ucl_object_emit (obj, type);

	if (result != NULL) {
		lua_pushstring (L, (const char *)result);
		free (result);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

static int
lua_ucl_parser_init (lua_State *L)
{
	struct ucl_parser *parser, **pparser;
	int flags = UCL_PARSER_NO_FILEVARS;

	if (lua_gettop (L) >= 1) {
		flags = lua_tonumber (L, 1);
	}

	parser = ucl_parser_new (flags);
	if (parser == NULL) {
		lua_pushnil (L);
	}

	pparser = lua_newuserdata (L, sizeof (parser));
	*pparser = parser;
	luaL_getmetatable (L, PARSER_META);
	lua_setmetatable (L, -2);

	return 1;
}

static struct ucl_parser *
lua_ucl_parser_get (lua_State *L, int index)
{
	return *((struct ucl_parser **) luaL_checkudata(L, index, PARSER_META));
}

static ucl_object_t *
lua_ucl_object_get (lua_State *L, int index)
{
	return *((ucl_object_t **) luaL_checkudata(L, index, OBJECT_META));
}

static void
lua_ucl_push_opaque (lua_State *L, ucl_object_t *obj)
{
	ucl_object_t **pobj;

	pobj = lua_newuserdata (L, sizeof (*pobj));
	*pobj = obj;
	luaL_getmetatable (L, OBJECT_META);
	lua_setmetatable (L, -2);
}

static inline enum ucl_parse_type
lua_ucl_str_to_parse_type (const char *str)
{
	enum ucl_parse_type type = UCL_PARSE_UCL;

	if (str != NULL) {
		if (strcasecmp (str, "msgpack") == 0) {
			type = UCL_PARSE_MSGPACK;
		}
		else if (strcasecmp (str, "sexp") == 0 ||
				strcasecmp (str, "csexp") == 0) {
			type = UCL_PARSE_CSEXP;
		}
		else if (strcasecmp (str, "auto") == 0) {
			type = UCL_PARSE_AUTO;
		}
	}

	return type;
}

/***
 * @method parser:parse_file(name)
 * Parse UCL object from file.
 * @param {string} name filename to parse
 * @return {bool[, string]} if res is `true` then file has been parsed successfully, otherwise an error string is also returned
@example
local parser = ucl.parser()
local res,err = parser:parse_file('/some/file.conf')

if not res then
	print('parser error: ' .. err)
else
	-- Do something with object
end
 */
static int
lua_ucl_parser_parse_file (lua_State *L)
{
	struct ucl_parser *parser;
	const char *file;
	int ret = 2;

	parser = lua_ucl_parser_get (L, 1);
	file = luaL_checkstring (L, 2);

	if (parser != NULL && file != NULL) {
		if (ucl_parser_add_file (parser, file)) {
			lua_pushboolean (L, true);
			ret = 1;
		}
		else {
			lua_pushboolean (L, false);
			lua_pushstring (L, ucl_parser_get_error (parser));
		}
	}
	else {
		lua_pushboolean (L, false);
		lua_pushstring (L, "invalid arguments");
	}

	return ret;
}

/***
 * @method parser:parse_string(input)
 * Parse UCL object from file.
 * @param {string} input string to parse
 * @return {bool[, string]} if res is `true` then file has been parsed successfully, otherwise an error string is also returned
 */
static int
lua_ucl_parser_parse_string (lua_State *L)
{
	struct ucl_parser *parser;
	const char *string;
	size_t llen;
	enum ucl_parse_type type = UCL_PARSE_UCL;
	int ret = 2;

	parser = lua_ucl_parser_get (L, 1);
	string = luaL_checklstring (L, 2, &llen);

	if (lua_type (L, 3) == LUA_TSTRING) {
		type = lua_ucl_str_to_parse_type (lua_tostring (L, 3));
	}

	if (parser != NULL && string != NULL) {
		if (ucl_parser_add_chunk_full (parser, (const unsigned char *)string,
				llen, 0, UCL_DUPLICATE_APPEND, type)) {
			lua_pushboolean (L, true);
			ret = 1;
		}
		else {
			lua_pushboolean (L, false);
			lua_pushstring (L, ucl_parser_get_error (parser));
		}
	}
	else {
		lua_pushboolean (L, false);
		lua_pushstring (L, "invalid arguments");
	}

	return ret;
}

/***
 * @method parser:get_object()
 * Get top object from parser and export it to lua representation.
 * @return {variant or nil} ucl object as lua native variable
 */
static int
lua_ucl_parser_get_object (lua_State *L)
{
	struct ucl_parser *parser;
	ucl_object_t *obj;
	int ret = 1;

	parser = lua_ucl_parser_get (L, 1);
	obj = ucl_parser_get_object (parser);

	if (obj != NULL) {
		ret = ucl_object_push_lua (L, obj, false);
		/* no need to keep reference */
		ucl_object_unref (obj);
	}
	else {
		lua_pushnil (L);
	}

	return ret;
}

/***
 * @method parser:get_object_wrapped()
 * Get top object from parser and export it to userdata object without
 * unwrapping to lua.
 * @return {ucl.object or nil} ucl object wrapped variable
 */
static int
lua_ucl_parser_get_object_wrapped (lua_State *L)
{
	struct ucl_parser *parser;
	ucl_object_t *obj;
	int ret = 1;

	parser = lua_ucl_parser_get (L, 1);
	obj = ucl_parser_get_object (parser);

	if (obj != NULL) {
		lua_ucl_push_opaque (L, obj);
	}
	else {
		lua_pushnil (L);
	}

	return ret;
}

/***
 * @method parser:validate(schema)
 * Validates the top object in the parser against schema. Schema might be
 * another object or a string that represents file to load schema from.
 *
 * @param {string/table} schema input schema
 * @return {result,err} two values: boolean result and the corresponding error
 *
 */
static int
lua_ucl_parser_validate (lua_State *L)
{
	struct ucl_parser *parser, *schema_parser;
	ucl_object_t *schema;
	const char *schema_file;
	struct ucl_schema_error err;

	parser = lua_ucl_parser_get (L, 1);

	if (parser && parser->top_obj) {
		if (lua_type (L, 2) == LUA_TTABLE) {
			schema = ucl_object_lua_import (L, 2);

			if (schema == NULL) {
				lua_pushboolean (L, false);
				lua_pushstring (L, "cannot load schema from lua table");

				return 2;
			}
		}
		else if (lua_type (L, 2) == LUA_TSTRING) {
			schema_parser = ucl_parser_new (0);
			schema_file = luaL_checkstring (L, 2);

			if (!ucl_parser_add_file (schema_parser, schema_file)) {
				lua_pushboolean (L, false);
				lua_pushfstring (L, "cannot parse schema file \"%s\": "
						"%s", schema_file, ucl_parser_get_error (parser));
				ucl_parser_free (schema_parser);

				return 2;
			}

			schema = ucl_parser_get_object (schema_parser);
			ucl_parser_free (schema_parser);
		}
		else {
			lua_pushboolean (L, false);
			lua_pushstring (L, "invalid schema argument");

			return 2;
		}

		if (!ucl_object_validate (schema, parser->top_obj, &err)) {
			lua_pushboolean (L, false);
			lua_pushfstring (L, "validation error: "
					"%s", err.msg);
		}
		else {
			lua_pushboolean (L, true);
			lua_pushnil (L);
		}

		ucl_object_unref (schema);
	}
	else {
		lua_pushboolean (L, false);
		lua_pushstring (L, "invalid parser or empty top object");
	}

	return 2;
}

static int
lua_ucl_parser_gc (lua_State *L)
{
	struct ucl_parser *parser;

	parser = lua_ucl_parser_get (L, 1);
	ucl_parser_free (parser);

	return 0;
}

/***
 * @method object:unwrap()
 * Unwraps opaque ucl object to the native lua object (performing copying)
 * @return {variant} any lua object
 */
static int
lua_ucl_object_unwrap (lua_State *L)
{
	ucl_object_t *obj;

	obj = lua_ucl_object_get (L, 1);

	if (obj) {
		ucl_object_push_lua (L, obj, true);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

static inline enum ucl_emitter
lua_ucl_str_to_emit_type (const char *strtype)
{
	enum ucl_emitter format = UCL_EMIT_JSON_COMPACT;

	if (strcasecmp (strtype, "json") == 0) {
		format = UCL_EMIT_JSON;
	}
	else if (strcasecmp (strtype, "json-compact") == 0) {
		format = UCL_EMIT_JSON_COMPACT;
	}
	else if (strcasecmp (strtype, "yaml") == 0) {
		format = UCL_EMIT_YAML;
	}
	else if (strcasecmp (strtype, "config") == 0 ||
			strcasecmp (strtype, "ucl") == 0) {
		format = UCL_EMIT_CONFIG;
	}

	return format;
}

/***
 * @method object:tostring(type)
 * Unwraps opaque ucl object to string (json by default). Optionally you can
 * specify output format:
 *
 * - `json` - fine printed json
 * - `json-compact` - compacted json
 * - `config` - fine printed configuration
 * - `ucl` - same as `config`
 * - `yaml` - embedded yaml
 * @param {string} type optional
 * @return {string} string representation of the opaque ucl object
 */
static int
lua_ucl_object_tostring (lua_State *L)
{
	ucl_object_t *obj;
	enum ucl_emitter format = UCL_EMIT_JSON_COMPACT;

	obj = lua_ucl_object_get (L, 1);

	if (obj) {
		if (lua_gettop (L) > 1) {
			if (lua_type (L, 2) == LUA_TSTRING) {
				const char *strtype = lua_tostring (L, 2);

				format = lua_ucl_str_to_emit_type (strtype);
			}
		}

		return lua_ucl_to_string (L, obj, format);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/***
 * @method object:validate(schema[, path[, ext_refs]])
 * Validates the given ucl object using schema object represented as another
 * opaque ucl object. You can also specify path in the form `#/path/def` to
 * specify the specific schema element to perform validation.
 *
 * @param {ucl.object} schema schema object
 * @param {string} path optional path for validation procedure
 * @return {result,err} two values: boolean result and the corresponding
 * error, if `ext_refs` are also specified, then they are returned as opaque
 * ucl object as {result,err,ext_refs}
 */
static int
lua_ucl_object_validate (lua_State *L)
{
	ucl_object_t *obj, *schema, *ext_refs = NULL;
	const ucl_object_t *schema_elt;
	bool res = false;
	struct ucl_schema_error err;
	const char *path = NULL;

	obj = lua_ucl_object_get (L, 1);
	schema = lua_ucl_object_get (L, 2);

	if (schema && obj && ucl_object_type (schema) == UCL_OBJECT) {
		if (lua_gettop (L) > 2) {
			if (lua_type (L, 3) == LUA_TSTRING) {
				path = lua_tostring (L, 3);
				if (path[0] == '#') {
					path++;
				}
			}
			else if (lua_type (L, 3) == LUA_TUSERDATA || lua_type (L, 3) ==
						LUA_TTABLE) {
				/* External refs */
				ext_refs = lua_ucl_object_get (L, 3);
			}

			if (lua_gettop (L) > 3) {
				if (lua_type (L, 4) == LUA_TUSERDATA || lua_type (L, 4) ==
						LUA_TTABLE) {
					/* External refs */
					ext_refs = lua_ucl_object_get (L, 4);
				}
			}
		}

		if (path) {
			schema_elt = ucl_object_lookup_path_char (schema, path, '/');
		}
		else {
			/* Use the top object */
			schema_elt = schema;
		}

		if (schema_elt) {
			res = ucl_object_validate_root_ext (schema_elt, obj, schema,
					ext_refs, &err);

			if (res) {
				lua_pushboolean (L, res);
				lua_pushnil (L);

				if (ext_refs) {
					lua_ucl_push_opaque (L, ext_refs);
				}
			}
			else {
				lua_pushboolean (L, res);
				lua_pushfstring (L, "validation error: %s", err.msg);

				if (ext_refs) {
					lua_ucl_push_opaque (L, ext_refs);
				}
			}
		}
		else {
			lua_pushboolean (L, res);

			lua_pushfstring (L, "cannot find the requested path: %s", path);

			if (ext_refs) {
				lua_ucl_push_opaque (L, ext_refs);
			}
		}
	}
	else {
		lua_pushboolean (L, res);
		lua_pushstring (L, "invalid object or schema");
	}

	if (ext_refs) {
		return 3;
	}

	return 2;
}

static int
lua_ucl_object_gc (lua_State *L)
{
	ucl_object_t *obj;

	obj = lua_ucl_object_get (L, 1);

	ucl_object_unref (obj);

	return 0;
}

static void
lua_ucl_parser_mt (lua_State *L)
{
	luaL_newmetatable (L, PARSER_META);

	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	lua_pushcfunction (L, lua_ucl_parser_gc);
	lua_setfield (L, -2, "__gc");

	lua_pushcfunction (L, lua_ucl_parser_parse_file);
	lua_setfield (L, -2, "parse_file");

	lua_pushcfunction (L, lua_ucl_parser_parse_string);
	lua_setfield (L, -2, "parse_string");

	lua_pushcfunction (L, lua_ucl_parser_get_object);
	lua_setfield (L, -2, "get_object");

	lua_pushcfunction (L, lua_ucl_parser_get_object_wrapped);
	lua_setfield (L, -2, "get_object_wrapped");

	lua_pushcfunction (L, lua_ucl_parser_validate);
	lua_setfield (L, -2, "validate");

	lua_pop (L, 1);
}

static void
lua_ucl_object_mt (lua_State *L)
{
	luaL_newmetatable (L, OBJECT_META);

	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	lua_pushcfunction (L, lua_ucl_object_gc);
	lua_setfield (L, -2, "__gc");

	lua_pushcfunction (L, lua_ucl_object_tostring);
	lua_setfield (L, -2, "__tostring");

	lua_pushcfunction (L, lua_ucl_object_tostring);
	lua_setfield (L, -2, "tostring");

	lua_pushcfunction (L, lua_ucl_object_unwrap);
	lua_setfield (L, -2, "unwrap");

	lua_pushcfunction (L, lua_ucl_object_unwrap);
	lua_setfield (L, -2, "tolua");

	lua_pushcfunction (L, lua_ucl_object_validate);
	lua_setfield (L, -2, "validate");

	lua_pushstring (L, OBJECT_META);
	lua_setfield (L, -2, "class");

	lua_pop (L, 1);
}

static int
lua_ucl_to_json (lua_State *L)
{
	ucl_object_t *obj;
	int format = UCL_EMIT_JSON;

	if (lua_gettop (L) > 1) {
		if (lua_toboolean (L, 2)) {
			format = UCL_EMIT_JSON_COMPACT;
		}
	}

	obj = ucl_object_lua_import (L, 1);
	if (obj != NULL) {
		lua_ucl_to_string (L, obj, format);
		ucl_object_unref (obj);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

static int
lua_ucl_to_config (lua_State *L)
{
	ucl_object_t *obj;

	obj = ucl_object_lua_import (L, 1);
	if (obj != NULL) {
		lua_ucl_to_string (L, obj, UCL_EMIT_CONFIG);
		ucl_object_unref (obj);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/***
 * @function ucl.to_format(var, format)
 * Converts lua variable `var` to the specified `format`. Formats supported are:
 *
 * - `json` - fine printed json
 * - `json-compact` - compacted json
 * - `config` - fine printed configuration
 * - `ucl` - same as `config`
 * - `yaml` - embedded yaml
 *
 * If `var` contains function, they are called during output formatting and if
 * they return string value, then this value is used for ouptut.
 * @param {variant} var any sort of lua variable (if userdata then metafield `__to_ucl` is searched for output)
 * @param {string} format any available format
 * @return {string} string representation of `var` in the specific `format`.
 * @example
local table = {
  str = 'value',
  num = 100500,
  null = ucl.null,
  func = function ()
    return 'huh'
  end
}

print(ucl.to_format(table, 'ucl'))
-- Output:
--[[
num = 100500;
str = "value";
null = null;
func = "huh";
--]]
 */
static int
lua_ucl_to_format (lua_State *L)
{
	ucl_object_t *obj;
	int format = UCL_EMIT_JSON;

	if (lua_gettop (L) > 1) {
		if (lua_type (L, 2) == LUA_TNUMBER) {
			format = lua_tonumber (L, 2);
			if (format < 0 || format >= UCL_EMIT_YAML) {
				lua_pushnil (L);
				return 1;
			}
		}
		else if (lua_type (L, 2) == LUA_TSTRING) {
			const char *strtype = lua_tostring (L, 2);

			if (strcasecmp (strtype, "json") == 0) {
				format = UCL_EMIT_JSON;
			}
			else if (strcasecmp (strtype, "json-compact") == 0) {
				format = UCL_EMIT_JSON_COMPACT;
			}
			else if (strcasecmp (strtype, "yaml") == 0) {
				format = UCL_EMIT_YAML;
			}
			else if (strcasecmp (strtype, "config") == 0 ||
				strcasecmp (strtype, "ucl") == 0) {
				format = UCL_EMIT_CONFIG;
			}
			else if (strcasecmp (strtype, "msgpack") == 0) {
				format = UCL_EMIT_MSGPACK;
			}
		}
	}

	obj = ucl_object_lua_import (L, 1);
	if (obj != NULL) {
		lua_ucl_to_string (L, obj, format);
		ucl_object_unref (obj);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

static int
lua_ucl_null_tostring (lua_State* L)
{
	lua_pushstring (L, "null");
	return 1;
}

static void
lua_ucl_null_mt (lua_State *L)
{
	luaL_newmetatable (L, NULL_META);

	lua_pushcfunction (L, lua_ucl_null_tostring);
	lua_setfield (L, -2, "__tostring");

	lua_pop (L, 1);
}

int
luaopen_ucl (lua_State *L)
{
	lua_ucl_parser_mt (L);
	lua_ucl_null_mt (L);
	lua_ucl_object_mt (L);

	/* Create the refs weak table: */
	lua_createtable (L, 0, 2);
	lua_pushliteral (L, "v"); /* tbl, "v" */
	lua_setfield (L, -2, "__mode");
	lua_pushvalue (L, -1); /* tbl, tbl */
	lua_setmetatable (L, -2); /* tbl */
	lua_setfield (L, LUA_REGISTRYINDEX, "ucl.refs");

	lua_newtable (L);

	lua_pushcfunction (L, lua_ucl_parser_init);
	lua_setfield (L, -2, "parser");

	lua_pushcfunction (L, lua_ucl_to_json);
	lua_setfield (L, -2, "to_json");

	lua_pushcfunction (L, lua_ucl_to_config);
	lua_setfield (L, -2, "to_config");

	lua_pushcfunction (L, lua_ucl_to_format);
	lua_setfield (L, -2, "to_format");

	ucl_null = lua_newuserdata (L, 0);
	luaL_getmetatable (L, NULL_META);
	lua_setmetatable (L, -2);

	lua_pushvalue (L, -1);
	lua_setfield (L, LUA_REGISTRYINDEX, "ucl.null");

	lua_setfield (L, -2, "null");

	return 1;
}

struct ucl_lua_funcdata*
ucl_object_toclosure (const ucl_object_t *obj)
{
	if (obj == NULL || obj->type != UCL_USERDATA) {
		return NULL;
	}

	return (struct ucl_lua_funcdata*)obj->value.ud;
}
