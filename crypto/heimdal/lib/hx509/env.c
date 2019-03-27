/*
 * Copyright (c) 2007 - 2008 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "hx_locl.h"

/**
 * @page page_env Hx509 enviroment functions
 *
 * See the library functions here: @ref hx509_env
 */

/**
 * Add a new key/value pair to the hx509_env.
 *
 * @param context A hx509 context.
 * @param env enviroment to add the enviroment variable too.
 * @param key key to add
 * @param value value to add
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_env
 */

int
hx509_env_add(hx509_context context, hx509_env *env,
	      const char *key, const char *value)
{
    hx509_env n;

    n = malloc(sizeof(*n));
    if (n == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }

    n->type = env_string;
    n->next = NULL;
    n->name = strdup(key);
    if (n->name == NULL) {
	free(n);
	return ENOMEM;
    }
    n->u.string = strdup(value);
    if (n->u.string == NULL) {
	free(n->name);
	free(n);
	return ENOMEM;
    }

    /* add to tail */
    if (*env) {
	hx509_env e = *env;
	while (e->next)
	    e = e->next;
	e->next = n;
    } else
	*env = n;

    return 0;
}

/**
 * Add a new key/binding pair to the hx509_env.
 *
 * @param context A hx509 context.
 * @param env enviroment to add the enviroment variable too.
 * @param key key to add
 * @param list binding list to add
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_env
 */

int
hx509_env_add_binding(hx509_context context, hx509_env *env,
		      const char *key, hx509_env list)
{
    hx509_env n;

    n = malloc(sizeof(*n));
    if (n == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }

    n->type = env_list;
    n->next = NULL;
    n->name = strdup(key);
    if (n->name == NULL) {
	free(n);
	return ENOMEM;
    }
    n->u.list = list;

    /* add to tail */
    if (*env) {
	hx509_env e = *env;
	while (e->next)
	    e = e->next;
	e->next = n;
    } else
	*env = n;

    return 0;
}


/**
 * Search the hx509_env for a length based key.
 *
 * @param context A hx509 context.
 * @param env enviroment to add the enviroment variable too.
 * @param key key to search for.
 * @param len length of key.
 *
 * @return the value if the key is found, NULL otherwise.
 *
 * @ingroup hx509_env
 */

const char *
hx509_env_lfind(hx509_context context, hx509_env env,
		const char *key, size_t len)
{
    while(env) {
	if (strncmp(key, env->name ,len) == 0
	    && env->name[len] == '\0' && env->type == env_string)
	    return env->u.string;
	env = env->next;
    }
    return NULL;
}

/**
 * Search the hx509_env for a key.
 *
 * @param context A hx509 context.
 * @param env enviroment to add the enviroment variable too.
 * @param key key to search for.
 *
 * @return the value if the key is found, NULL otherwise.
 *
 * @ingroup hx509_env
 */

const char *
hx509_env_find(hx509_context context, hx509_env env, const char *key)
{
    while(env) {
	if (strcmp(key, env->name) == 0 && env->type == env_string)
	    return env->u.string;
	env = env->next;
    }
    return NULL;
}

/**
 * Search the hx509_env for a binding.
 *
 * @param context A hx509 context.
 * @param env enviroment to add the enviroment variable too.
 * @param key key to search for.
 *
 * @return the binding if the key is found, NULL if not found.
 *
 * @ingroup hx509_env
 */

hx509_env
hx509_env_find_binding(hx509_context context,
		       hx509_env env,
		       const char *key)
{
    while(env) {
	if (strcmp(key, env->name) == 0 && env->type == env_list)
	    return env->u.list;
	env = env->next;
    }
    return NULL;
}

static void
env_free(hx509_env b)
{
    while(b) {
	hx509_env next = b->next;

	if (b->type == env_string)
	    free(b->u.string);
	else if (b->type == env_list)
	    env_free(b->u.list);

	free(b->name);
	free(b);
	b = next;
    }
}

/**
 * Free an hx509_env enviroment context.
 *
 * @param env the enviroment to free.
 *
 * @ingroup hx509_env
 */

void
hx509_env_free(hx509_env *env)
{
    if (*env)
	env_free(*env);
    *env = NULL;
}
