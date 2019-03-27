/*
 * services/modstack.c - stack of modules
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains functions to help maintain a stack of modules.
 */
#include "config.h"
#include <ctype.h>
#include "services/modstack.h"
#include "util/module.h"
#include "util/fptr_wlist.h"
#include "dns64/dns64.h"
#include "iterator/iterator.h"
#include "validator/validator.h"
#include "respip/respip.h"

#ifdef WITH_PYTHONMODULE
#include "pythonmod/pythonmod.h"
#endif
#ifdef USE_CACHEDB
#include "cachedb/cachedb.h"
#endif
#ifdef USE_IPSECMOD
#include "ipsecmod/ipsecmod.h"
#endif
#ifdef CLIENT_SUBNET
#include "edns-subnet/subnetmod.h"
#endif

/** count number of modules (words) in the string */
static int
count_modules(const char* s)
{
        int num = 0;
        if(!s)
                return 0;
        while(*s) {
                /* skip whitespace */
                while(*s && isspace((unsigned char)*s))
                        s++;
                if(*s && !isspace((unsigned char)*s)) {
                        /* skip identifier */
                        num++;
                        while(*s && !isspace((unsigned char)*s))
                                s++;
                }
        }
        return num;
}

void 
modstack_init(struct module_stack* stack)
{
	stack->num = 0;
	stack->mod = NULL;
}

int 
modstack_config(struct module_stack* stack, const char* module_conf)
{
        int i;
        verbose(VERB_QUERY, "module config: \"%s\"", module_conf);
        stack->num = count_modules(module_conf);
        if(stack->num == 0) {
                log_err("error: no modules specified");
                return 0;
        }
        if(stack->num > MAX_MODULE) {
                log_err("error: too many modules (%d max %d)",
                        stack->num, MAX_MODULE);
                return 0;
        }
        stack->mod = (struct module_func_block**)calloc((size_t)
                stack->num, sizeof(struct module_func_block*));
        if(!stack->mod) {
                log_err("out of memory");
                return 0;
        }
        for(i=0; i<stack->num; i++) {
                stack->mod[i] = module_factory(&module_conf);
                if(!stack->mod[i]) {
                        log_err("Unknown value for next module: '%s'",
                                module_conf);
                        return 0;
                }
        }
        return 1;
}

/** The list of module names */
const char**
module_list_avail(void)
{
        /* these are the modules available */
        static const char* names[] = {
		"dns64",
#ifdef WITH_PYTHONMODULE
		"python",
#endif
#ifdef USE_CACHEDB
		"cachedb",
#endif
#ifdef USE_IPSECMOD
		"ipsecmod",
#endif
#ifdef CLIENT_SUBNET
		"subnetcache",
#endif
		"respip",
		"validator",
		"iterator",
		NULL};
	return names;
}

/** func block get function type */
typedef struct module_func_block* (*fbgetfunctype)(void);

/** The list of module func blocks */
static fbgetfunctype*
module_funcs_avail(void)
{
        static struct module_func_block* (*fb[])(void) = {
		&dns64_get_funcblock,
#ifdef WITH_PYTHONMODULE
		&pythonmod_get_funcblock,
#endif
#ifdef USE_CACHEDB
		&cachedb_get_funcblock,
#endif
#ifdef USE_IPSECMOD
		&ipsecmod_get_funcblock,
#endif
#ifdef CLIENT_SUBNET
		&subnetmod_get_funcblock,
#endif
		&respip_get_funcblock,
		&val_get_funcblock,
		&iter_get_funcblock,
		NULL};
	return fb;
}

struct
module_func_block* module_factory(const char** str)
{
        int i = 0;
        const char* s = *str;
	const char** names = module_list_avail();
	fbgetfunctype* fb = module_funcs_avail();
        while(*s && isspace((unsigned char)*s))
                s++;
	while(names[i]) {
                if(strncmp(names[i], s, strlen(names[i])) == 0) {
                        s += strlen(names[i]);
                        *str = s;
                        return (*fb[i])();
                }
		i++;
        }
        return NULL;
}

int 
modstack_setup(struct module_stack* stack, const char* module_conf,
	struct module_env* env)
{
        int i;
        if(stack->num != 0)
                modstack_desetup(stack, env);
        /* fixed setup of the modules */
        if(!modstack_config(stack, module_conf)) {
		return 0;
        }
        env->need_to_validate = 0; /* set by module init below */
        for(i=0; i<stack->num; i++) {
                verbose(VERB_OPS, "init module %d: %s",
                        i, stack->mod[i]->name);
                fptr_ok(fptr_whitelist_mod_init(stack->mod[i]->init));
                if(!(*stack->mod[i]->init)(env, i)) {
                        log_err("module init for module %s failed",
                                stack->mod[i]->name);
			return 0;
                }
        }
	return 1;
}

void 
modstack_desetup(struct module_stack* stack, struct module_env* env)
{
        int i;
        for(i=0; i<stack->num; i++) {
                fptr_ok(fptr_whitelist_mod_deinit(stack->mod[i]->deinit));
                (*stack->mod[i]->deinit)(env, i);
        }
        stack->num = 0;
        free(stack->mod);
        stack->mod = NULL;
}

int 
modstack_find(struct module_stack* stack, const char* name)
{
	int i;
	for(i=0; i<stack->num; i++) {
		if(strcmp(stack->mod[i]->name, name) == 0)
			return i;
	}
	return -1;
}

size_t
mod_get_mem(struct module_env* env, const char* name)
{
	int m = modstack_find(&env->mesh->mods, name);
	if(m != -1) {
		fptr_ok(fptr_whitelist_mod_get_mem(env->mesh->
			mods.mod[m]->get_mem));
		return (*env->mesh->mods.mod[m]->get_mem)(env, m);
	}
	return 0;
}
