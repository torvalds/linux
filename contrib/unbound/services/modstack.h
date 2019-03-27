/*
 * services/modstack.h - stack of modules
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

#ifndef SERVICES_MODSTACK_H
#define SERVICES_MODSTACK_H
struct module_func_block;
struct module_env;

/**
 * Stack of modules.
 */
struct module_stack {
	/** the number of modules */
	int num;
	/** the module callbacks, array of num_modules length (ref only) */
	struct module_func_block** mod;
};

/**
 * Init a stack of modules
 * @param stack: initialised as empty.
 */
void modstack_init(struct module_stack* stack);

/**
 * Read config file module settings and set up the modfunc block
 * @param stack: the stack of modules (empty before call). 
 * @param module_conf: string what modules to insert.
 * @return false on error
 */
int modstack_config(struct module_stack* stack, const char* module_conf);

/**
 * Get funcblock for module name
 * @param str: string with module name. Advanced to next value on success.
 *	The string is assumed whitespace separated list of module names.
 * @return funcblock or NULL on error.
 */
struct module_func_block* module_factory(const char** str);

/**
 * Get list of modules available.
 * @return list of modules available. Static strings, ends with NULL.
 */
const char** module_list_avail(void);

/**
 * Setup modules. Assigns ids and calls module_init.
 * @param stack: if not empty beforehand, it will be desetup()ed.
 *	It is then modstack_configged().
 * @param module_conf: string what modules to insert.
 * @param env: module environment which is inited by the modules.
 *	environment should have a superalloc, cfg,
 *	env.need_to_validate is set by the modules.
 * @return on false a module init failed.
 */
int modstack_setup(struct module_stack* stack, const char* module_conf,
	struct module_env* env);

/**
 * Desetup the modules, deinit, delete.
 * @param stack: made empty.
 * @param env: module env for module deinit() calls.
 */
void modstack_desetup(struct module_stack* stack, struct module_env* env);

/**
 * Find index of module by name.
 * @param stack: to look in
 * @param name: the name to look for
 * @return -1 on failure, otherwise index number.
 */
int modstack_find(struct module_stack* stack, const char* name);

/** fetch memory for a module by name, returns 0 if module not there */
size_t mod_get_mem(struct module_env* env, const char* name);

#endif /* SERVICES_MODSTACK_H */
