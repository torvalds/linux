/*
 * Copyright (c) 1997, 1998, 1999 Kungliga Tekniska Högskolan
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

struct foreach_data {
    const char *exp;
    char *exp2;
    char **princs;
    int count;
};

static krb5_error_code
add_princ(struct foreach_data *d, char *princ)
{
    char **tmp;
    tmp = realloc(d->princs, (d->count + 1) * sizeof(*tmp));
    if(tmp == NULL)
	return ENOMEM;
    d->princs = tmp;
    d->princs[d->count++] = princ;
    return 0;
}

static krb5_error_code
foreach(krb5_context context, HDB *db, hdb_entry_ex *ent, void *data)
{
    struct foreach_data *d = data;
    char *princ;
    krb5_error_code ret;
    ret = krb5_unparse_name(context, ent->entry.principal, &princ);
    if(ret)
	return ret;
    if(d->exp){
	if(fnmatch(d->exp, princ, 0) == 0 || fnmatch(d->exp2, princ, 0) == 0)
	    ret = add_princ(d, princ);
	else
	    free(princ);
    }else{
	ret = add_princ(d, princ);
    }
    if(ret)
	free(princ);
    return ret;
}

kadm5_ret_t
kadm5_s_get_principals(void *server_handle,
		       const char *expression,
		       char ***princs,
		       int *count)
{
    struct foreach_data d;
    kadm5_server_context *context = server_handle;
    kadm5_ret_t ret;
    ret = context->db->hdb_open(context->context, context->db, O_RDWR, 0);
    if(ret) {
	krb5_warn(context->context, ret, "opening database");
	return ret;
    }
    d.exp = expression;
    {
	krb5_realm r;
	krb5_get_default_realm(context->context, &r);
	asprintf(&d.exp2, "%s@%s", expression, r);
	free(r);
    }
    d.princs = NULL;
    d.count = 0;
    ret = hdb_foreach(context->context, context->db, HDB_F_ADMIN_DATA, foreach, &d);
    context->db->hdb_close(context->context, context->db);
    if(ret == 0)
	ret = add_princ(&d, NULL);
    if(ret == 0){
	*princs = d.princs;
	*count = d.count - 1;
    }else
	kadm5_free_name_list(context, d.princs, &d.count);
    free(d.exp2);
    return _kadm5_error_code(ret);
}
