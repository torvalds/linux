/*
 * dns64/dns64.h - DNS64 module
 *
 * Copyright (c) 2009, Viagénie. All rights reserved.
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
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains a module that performs DNS64 query processing.
 */

#ifndef DNS64_DNS64_H
#define DNS64_DNS64_H
#include "util/module.h"

/**
 * Get the dns64 function block.
 * @return: function block with function pointers to dns64 methods.
 */
struct module_func_block *dns64_get_funcblock(void);

/** dns64 init */
int dns64_init(struct module_env* env, int id);

/** dns64 deinit */
void dns64_deinit(struct module_env* env, int id);

/** dns64 operate on a query */
void dns64_operate(struct module_qstate* qstate, enum module_ev event, int id,
		struct outbound_entry* outbound);

void dns64_inform_super(struct module_qstate* qstate, int id,
    struct module_qstate* super);

/** dns64 cleanup query state */
void dns64_clear(struct module_qstate* qstate, int id);

/** dns64 alloc size routine */
size_t dns64_get_mem(struct module_env* env, int id);

#endif /* DNS64_DNS64_H */
