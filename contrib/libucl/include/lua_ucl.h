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
#ifndef LUA_UCL_H_
#define LUA_UCL_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "ucl.h"

/**
 * Closure structure for lua function storing inside UCL
 */
struct ucl_lua_funcdata {
	lua_State *L;
	int idx;
	char *ret;
};

/**
 * Initialize lua UCL API
 */
UCL_EXTERN int luaopen_ucl (lua_State *L);

/**
 * Import UCL object from lua state
 * @param L lua state
 * @param idx index of object at the lua stack to convert to UCL
 * @return new UCL object or NULL, the caller should unref object after using
 */
UCL_EXTERN ucl_object_t* ucl_object_lua_import (lua_State *L, int idx);

/**
 * Push an object to lua
 * @param L lua state
 * @param obj object to push
 * @param allow_array traverse over implicit arrays
 */
UCL_EXTERN int ucl_object_push_lua (lua_State *L,
		const ucl_object_t *obj, bool allow_array);

UCL_EXTERN struct ucl_lua_funcdata* ucl_object_toclosure (
		const ucl_object_t *obj);

#endif /* LUA_UCL_H_ */
