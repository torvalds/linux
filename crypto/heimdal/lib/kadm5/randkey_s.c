/*
 * Copyright (c) 1997-2001, 2003-2006 Kungliga Tekniska Högskolan
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
 * Set the keys of `princ' to random values, returning the random keys
 * in `new_keys', `n_keys'.
 */

kadm5_ret_t
kadm5_s_randkey_principal(void *server_handle,
			  krb5_principal princ,
			  krb5_keyblock **new_keys,
			  int *n_keys)
{
    kadm5_server_context *context = server_handle;
    hdb_entry_ex ent;
    kadm5_ret_t ret;

    memset(&ent, 0, sizeof(ent));
    ret = context->db->hdb_open(context->context, context->db, O_RDWR, 0);
    if(ret)
	return ret;
    ret = context->db->hdb_fetch_kvno(context->context, context->db, princ,
				      HDB_F_GET_ANY|HDB_F_ADMIN_DATA, 0, &ent);
    if(ret)
	goto out;

    ret = _kadm5_set_keys_randomly (context,
				    &ent.entry,
				    new_keys,
				    n_keys);
    if (ret)
	goto out2;
    ent.entry.kvno++;

    ret = _kadm5_set_modifier(context, &ent.entry);
    if(ret)
	goto out3;
    ret = _kadm5_bump_pw_expire(context, &ent.entry);
    if (ret)
	goto out2;

    ret = hdb_seal_keys(context->context, context->db, &ent.entry);
    if (ret)
	goto out2;

    ret = context->db->hdb_store(context->context, context->db,
				 HDB_F_REPLACE, &ent);
    if (ret)
	goto out2;

    kadm5_log_modify (context,
		      &ent.entry,
		      KADM5_PRINCIPAL | KADM5_MOD_NAME | KADM5_MOD_TIME |
		      KADM5_KEY_DATA | KADM5_KVNO | KADM5_PW_EXPIRATION |
		      KADM5_TL_DATA);

out3:
    if (ret) {
	int i;

	for (i = 0; i < *n_keys; ++i)
	    krb5_free_keyblock_contents (context->context, &(*new_keys)[i]);
	free (*new_keys);
	*new_keys = NULL;
	*n_keys = 0;
    }
out2:
    hdb_free_entry(context->context, &ent);
out:
    context->db->hdb_close(context->context, context->db);
    return _kadm5_error_code(ret);
}
