/*
 * Copyright (c) 1997 - 2000 Kungliga Tekniska Högskolan
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

#include "kadm5_locl.h"

RCSID("$Id$");

/*
 * dealloc a `kadm5_config_params'
 */

static void
destroy_config (kadm5_config_params *c)
{
    free (c->realm);
    free (c->dbname);
    free (c->acl_file);
    free (c->stash_file);
}

/*
 * dealloc a kadm5_log_context
 */

static void
destroy_kadm5_log_context (kadm5_log_context *c)
{
    free (c->log_file);
    rk_closesocket (c->socket_fd);
#ifdef NO_UNIX_SOCKETS
    if (c->socket_info) {
	freeaddrinfo(c->socket_info);
	c->socket_info = NULL;
    }
#endif
}

/*
 * destroy a kadm5 handle
 */

kadm5_ret_t
kadm5_s_destroy(void *server_handle)
{
    kadm5_ret_t ret;
    kadm5_server_context *context = server_handle;
    krb5_context kcontext = context->context;

    ret = context->db->hdb_destroy(kcontext, context->db);
    destroy_kadm5_log_context (&context->log_context);
    destroy_config (&context->config);
    krb5_free_principal (kcontext, context->caller);
    if(context->my_context)
	krb5_free_context(kcontext);
    free (context);
    return ret;
}
