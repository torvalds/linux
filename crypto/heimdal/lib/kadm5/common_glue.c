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

#define __CALL(F, P) (*((kadm5_common_context*)server_handle)->funcs.F)P;

kadm5_ret_t
kadm5_chpass_principal(void *server_handle,
		       krb5_principal princ,
		       const char *password)
{
    return __CALL(chpass_principal, (server_handle, princ, password));
}

kadm5_ret_t
kadm5_chpass_principal_with_key(void *server_handle,
				krb5_principal princ,
				int n_key_data,
				krb5_key_data *key_data)
{
    return __CALL(chpass_principal_with_key,
		  (server_handle, princ, n_key_data, key_data));
}

kadm5_ret_t
kadm5_create_principal(void *server_handle,
		       kadm5_principal_ent_t princ,
		       uint32_t mask,
		       const char *password)
{
    return __CALL(create_principal, (server_handle, princ, mask, password));
}

kadm5_ret_t
kadm5_delete_principal(void *server_handle,
		       krb5_principal princ)
{
    return __CALL(delete_principal, (server_handle, princ));
}

kadm5_ret_t
kadm5_destroy (void *server_handle)
{
    return __CALL(destroy, (server_handle));
}

kadm5_ret_t
kadm5_flush (void *server_handle)
{
    return __CALL(flush, (server_handle));
}

kadm5_ret_t
kadm5_get_principal(void *server_handle,
		    krb5_principal princ,
		    kadm5_principal_ent_t out,
		    uint32_t mask)
{
    return __CALL(get_principal, (server_handle, princ, out, mask));
}

kadm5_ret_t
kadm5_modify_principal(void *server_handle,
		       kadm5_principal_ent_t princ,
		       uint32_t mask)
{
    return __CALL(modify_principal, (server_handle, princ, mask));
}

kadm5_ret_t
kadm5_randkey_principal(void *server_handle,
			krb5_principal princ,
			krb5_keyblock **new_keys,
			int *n_keys)
{
    return __CALL(randkey_principal, (server_handle, princ, new_keys, n_keys));
}

kadm5_ret_t
kadm5_rename_principal(void *server_handle,
		       krb5_principal source,
		       krb5_principal target)
{
    return __CALL(rename_principal, (server_handle, source, target));
}

kadm5_ret_t
kadm5_get_principals(void *server_handle,
		     const char *expression,
		     char ***princs,
		     int *count)
{
    return __CALL(get_principals, (server_handle, expression, princs, count));
}

kadm5_ret_t
kadm5_get_privs(void *server_handle,
		uint32_t *privs)
{
    return __CALL(get_privs, (server_handle, privs));
}
