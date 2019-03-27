/*
 * Copyright (c) 2006 - 2008 Kungliga Tekniska Högskolan
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

#include "ntlm.h"

static int
from_file(const char *fn, const char *target_domain,
	  char **username, struct ntlm_buf *key)
{
    char *str, buf[1024];
    FILE *f;

    f = fopen(fn, "r");
    if (f == NULL)
	return ENOENT;
    rk_cloexec_file(f);

    while (fgets(buf, sizeof(buf), f) != NULL) {
	char *d, *u, *p;
	buf[strcspn(buf, "\r\n")] = '\0';
	if (buf[0] == '#')
	    continue;
	str = NULL;
	d = strtok_r(buf, ":", &str);
	if (d && strcasecmp(target_domain, d) != 0)
	    continue;
	u = strtok_r(NULL, ":", &str);
	p = strtok_r(NULL, ":", &str);
	if (u == NULL || p == NULL)
	    continue;

	*username = strdup(u);

	heim_ntlm_nt_key(p, key);

	memset(buf, 0, sizeof(buf));
	fclose(f);
	return 0;
    }
    memset(buf, 0, sizeof(buf));
    fclose(f);
    return ENOENT;
}

static int
get_user_file(const ntlm_name target_name,
	      char **username, struct ntlm_buf *key)
{
    const char *fn;

    if (issuid())
	return ENOENT;

    fn = getenv("NTLM_USER_FILE");
    if (fn == NULL)
	return ENOENT;
    if (from_file(fn, target_name->domain, username, key) == 0)
	return 0;

    return ENOENT;
}

/*
 * Pick up the ntlm cred from the default krb5 credential cache.
 */

static int
get_user_ccache(const ntlm_name name, char **username, struct ntlm_buf *key)
{
    krb5_context context = NULL;
    krb5_principal client;
    krb5_ccache id = NULL;
    krb5_error_code ret;
    char *confname;
    krb5_data data;

    *username = NULL;
    krb5_data_zero(&data);
    key->length = 0;
    key->data = NULL;

    ret = krb5_init_context(&context);
    if (ret)
	return ret;

    ret = krb5_cc_default(context, &id);
    if (ret)
	goto out;

    ret = krb5_cc_get_principal(context, id, &client);
    if (ret)
	goto out;

    ret = krb5_unparse_name_flags(context, client,
				  KRB5_PRINCIPAL_UNPARSE_NO_REALM,
				  username);
    krb5_free_principal(context, client);
    if (ret)
	goto out;

    asprintf(&confname, "ntlm-key-%s", name->domain);
    if (confname == NULL) {
	krb5_clear_error_message(context);
	ret = ENOMEM;
	goto out;
    }

    ret = krb5_cc_get_config(context, id, NULL,
			     confname, &data);
    if (ret)
	goto out;

    key->data = malloc(data.length);
    if (key->data == NULL) {
	ret = ENOMEM;
	goto out;
    }
    key->length = data.length;
    memcpy(key->data, data.data, data.length);

 out:
    krb5_data_free(&data);
    if (id)
	krb5_cc_close(context, id);

    krb5_free_context(context);

    return ret;
}

int
_gss_ntlm_get_user_cred(const ntlm_name target_name,
			ntlm_cred *rcred)
{
    ntlm_cred cred;
    int ret;

    cred = calloc(1, sizeof(*cred));
    if (cred == NULL)
	return ENOMEM;

    ret = get_user_file(target_name, &cred->username, &cred->key);
    if (ret)
	ret = get_user_ccache(target_name, &cred->username, &cred->key);
    if (ret) {
	free(cred);
	return ret;
    }

    cred->domain = strdup(target_name->domain);
    *rcred = cred;

    return ret;
}

static int
_gss_copy_cred(ntlm_cred from, ntlm_cred *to)
{
    *to = calloc(1, sizeof(**to));
    if (*to == NULL)
	return ENOMEM;
    (*to)->username = strdup(from->username);
    if ((*to)->username == NULL) {
	free(*to);
	return ENOMEM;
    }
    (*to)->domain = strdup(from->domain);
    if ((*to)->domain == NULL) {
	free((*to)->username);
	free(*to);
	return ENOMEM;
    }
    (*to)->key.data = malloc(from->key.length);
    if ((*to)->key.data == NULL) {
	free((*to)->domain);
	free((*to)->username);
	free(*to);
	return ENOMEM;
    }
    memcpy((*to)->key.data, from->key.data, from->key.length);
    (*to)->key.length = from->key.length;

    return 0;
}

OM_uint32 GSSAPI_CALLCONV
_gss_ntlm_init_sec_context
           (OM_uint32 * minor_status,
            const gss_cred_id_t initiator_cred_handle,
            gss_ctx_id_t * context_handle,
            const gss_name_t target_name,
            const gss_OID mech_type,
            OM_uint32 req_flags,
            OM_uint32 time_req,
            const gss_channel_bindings_t input_chan_bindings,
            const gss_buffer_t input_token,
            gss_OID * actual_mech_type,
            gss_buffer_t output_token,
            OM_uint32 * ret_flags,
            OM_uint32 * time_rec
	   )
{
    ntlm_ctx ctx;
    ntlm_name name = (ntlm_name)target_name;

    *minor_status = 0;

    if (ret_flags)
	*ret_flags = 0;
    if (time_rec)
	*time_rec = 0;
    if (actual_mech_type)
	*actual_mech_type = GSS_C_NO_OID;

    if (*context_handle == GSS_C_NO_CONTEXT) {
	struct ntlm_type1 type1;
	struct ntlm_buf data;
	uint32_t flags = 0;
	int ret;

	ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL) {
	    *minor_status = EINVAL;
	    return GSS_S_FAILURE;
	}
	*context_handle = (gss_ctx_id_t)ctx;

	if (initiator_cred_handle != GSS_C_NO_CREDENTIAL) {
	    ntlm_cred cred = (ntlm_cred)initiator_cred_handle;
	    ret = _gss_copy_cred(cred, &ctx->client);
	} else
	    ret = _gss_ntlm_get_user_cred(name, &ctx->client);

	if (ret) {
	    _gss_ntlm_delete_sec_context(minor_status, context_handle, NULL);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	if (req_flags & GSS_C_CONF_FLAG)
	    flags |= NTLM_NEG_SEAL;
	if (req_flags & GSS_C_INTEG_FLAG)
	    flags |= NTLM_NEG_SIGN;
	else
	    flags |= NTLM_NEG_ALWAYS_SIGN;

	flags |= NTLM_NEG_UNICODE;
	flags |= NTLM_NEG_NTLM;
	flags |= NTLM_NEG_NTLM2_SESSION;
	flags |= NTLM_NEG_KEYEX;

	memset(&type1, 0, sizeof(type1));

	type1.flags = flags;
	type1.domain = name->domain;
	type1.hostname = NULL;
	type1.os[0] = 0;
	type1.os[1] = 0;

	ret = heim_ntlm_encode_type1(&type1, &data);
	if (ret) {
	    _gss_ntlm_delete_sec_context(minor_status, context_handle, NULL);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	output_token->value = data.data;
	output_token->length = data.length;

	return GSS_S_CONTINUE_NEEDED;
    } else {
	krb5_error_code ret;
	struct ntlm_type2 type2;
	struct ntlm_type3 type3;
	struct ntlm_buf data;

	ctx = (ntlm_ctx)*context_handle;

	data.data = input_token->value;
	data.length = input_token->length;

	ret = heim_ntlm_decode_type2(&data, &type2);
	if (ret) {
	    _gss_ntlm_delete_sec_context(minor_status, context_handle, NULL);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	ctx->flags = type2.flags;

	/* XXX check that type2.targetinfo matches `target_name´ */
	/* XXX check verify targetinfo buffer */

	memset(&type3, 0, sizeof(type3));

	type3.username = ctx->client->username;
	type3.flags = type2.flags;
	type3.targetname = type2.targetname;
	type3.ws = rk_UNCONST("workstation");

	/*
	 * NTLM Version 1 if no targetinfo buffer.
	 */

	if (1 || type2.targetinfo.length == 0) {
	    struct ntlm_buf sessionkey;

	    if (type2.flags & NTLM_NEG_NTLM2_SESSION) {
		unsigned char nonce[8];

		if (RAND_bytes(nonce, sizeof(nonce)) != 1) {
		    _gss_ntlm_delete_sec_context(minor_status,
						 context_handle, NULL);
		    *minor_status = EINVAL;
		    return GSS_S_FAILURE;
		}

		ret = heim_ntlm_calculate_ntlm2_sess(nonce,
						     type2.challenge,
						     ctx->client->key.data,
						     &type3.lm,
						     &type3.ntlm);
	    } else {
		ret = heim_ntlm_calculate_ntlm1(ctx->client->key.data,
						ctx->client->key.length,
						type2.challenge,
						&type3.ntlm);

	    }
	    if (ret) {
		_gss_ntlm_delete_sec_context(minor_status,context_handle,NULL);
		*minor_status = ret;
		return GSS_S_FAILURE;
	    }

	    ret = heim_ntlm_build_ntlm1_master(ctx->client->key.data,
					       ctx->client->key.length,
					       &sessionkey,
					       &type3.sessionkey);
	    if (ret) {
		if (type3.lm.data)
		    free(type3.lm.data);
		if (type3.ntlm.data)
		    free(type3.ntlm.data);
		_gss_ntlm_delete_sec_context(minor_status,context_handle,NULL);
		*minor_status = ret;
		return GSS_S_FAILURE;
	    }

	    ret = krb5_data_copy(&ctx->sessionkey,
				 sessionkey.data, sessionkey.length);
	    free(sessionkey.data);
	    if (ret) {
		if (type3.lm.data)
		    free(type3.lm.data);
		if (type3.ntlm.data)
		    free(type3.ntlm.data);
		_gss_ntlm_delete_sec_context(minor_status,context_handle,NULL);
		*minor_status = ret;
		return GSS_S_FAILURE;
	    }
	    ctx->status |= STATUS_SESSIONKEY;

	} else {
	    struct ntlm_buf sessionkey;
	    unsigned char ntlmv2[16];
	    struct ntlm_targetinfo ti;

	    /* verify infotarget */

	    ret = heim_ntlm_decode_targetinfo(&type2.targetinfo, 1, &ti);
	    if(ret) {
		_gss_ntlm_delete_sec_context(minor_status,
					     context_handle, NULL);
		*minor_status = ret;
		return GSS_S_FAILURE;
	    }

	    if (ti.domainname && strcmp(ti.domainname, name->domain) != 0) {
		_gss_ntlm_delete_sec_context(minor_status,
					     context_handle, NULL);
		*minor_status = EINVAL;
		return GSS_S_FAILURE;
	    }

	    ret = heim_ntlm_calculate_ntlm2(ctx->client->key.data,
					    ctx->client->key.length,
					    ctx->client->username,
					    name->domain,
					    type2.challenge,
					    &type2.targetinfo,
					    ntlmv2,
					    &type3.ntlm);
	    if (ret) {
		_gss_ntlm_delete_sec_context(minor_status,
					     context_handle, NULL);
		*minor_status = ret;
		return GSS_S_FAILURE;
	    }

	    ret = heim_ntlm_build_ntlm1_master(ntlmv2, sizeof(ntlmv2),
					       &sessionkey,
					       &type3.sessionkey);
	    memset(ntlmv2, 0, sizeof(ntlmv2));
	    if (ret) {
		_gss_ntlm_delete_sec_context(minor_status,
					     context_handle, NULL);
		*minor_status = ret;
		return GSS_S_FAILURE;
	    }

	    ctx->flags |= NTLM_NEG_NTLM2_SESSION;

	    ret = krb5_data_copy(&ctx->sessionkey,
				 sessionkey.data, sessionkey.length);
	    free(sessionkey.data);
	    if (ret) {
		_gss_ntlm_delete_sec_context(minor_status,
					     context_handle, NULL);
		*minor_status = ret;
		return GSS_S_FAILURE;
	    }
	}

	if (ctx->flags & NTLM_NEG_NTLM2_SESSION) {
	    ctx->status |= STATUS_SESSIONKEY;
	    _gss_ntlm_set_key(&ctx->u.v2.send, 0, (ctx->flags & NTLM_NEG_KEYEX),
			      ctx->sessionkey.data,
			      ctx->sessionkey.length);
	    _gss_ntlm_set_key(&ctx->u.v2.recv, 1, (ctx->flags & NTLM_NEG_KEYEX),
			      ctx->sessionkey.data,
			      ctx->sessionkey.length);
	} else {
	    ctx->status |= STATUS_SESSIONKEY;
	    RC4_set_key(&ctx->u.v1.crypto_recv.key,
			ctx->sessionkey.length,
			ctx->sessionkey.data);
	    RC4_set_key(&ctx->u.v1.crypto_send.key,
			ctx->sessionkey.length,
			ctx->sessionkey.data);
	}



	ret = heim_ntlm_encode_type3(&type3, &data);
	free(type3.sessionkey.data);
	if (type3.lm.data)
	    free(type3.lm.data);
	if (type3.ntlm.data)
	    free(type3.ntlm.data);
	if (ret) {
	    _gss_ntlm_delete_sec_context(minor_status, context_handle, NULL);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	output_token->length = data.length;
	output_token->value = data.data;

	if (actual_mech_type)
	    *actual_mech_type = GSS_NTLM_MECHANISM;
	if (ret_flags)
	    *ret_flags = 0;
	if (time_rec)
	    *time_rec = GSS_C_INDEFINITE;

	ctx->status |= STATUS_OPEN;

	return GSS_S_COMPLETE;
    }
}
