/*
 * Copyright (c) 2006 - 2007 Kungliga Tekniska Högskolan
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

#ifdef DIGEST

/*
 *
 */

struct ntlmkrb5 {
    krb5_context context;
    krb5_ntlm ntlm;
    krb5_realm kerberos_realm;
    krb5_ccache id;
    krb5_data opaque;
    int destroy;
    OM_uint32 flags;
    struct ntlm_buf key;
    krb5_data sessionkey;
};

static OM_uint32 kdc_destroy(OM_uint32 *, void *);

/*
 * Get credential cache that the ntlm code can use to talk to the KDC
 * using the digest API.
 */

static krb5_error_code
get_ccache(krb5_context context, int *destroy, krb5_ccache *id)
{
    krb5_principal principal = NULL;
    krb5_error_code ret;
    krb5_keytab kt = NULL;

    *id = NULL;

    if (!issuid()) {
	const char *cache;

	cache = getenv("NTLM_ACCEPTOR_CCACHE");
	if (cache) {
	    ret = krb5_cc_resolve(context, cache, id);
	    if (ret)
		goto out;
	    return 0;
	}
    }

    ret = krb5_sname_to_principal(context, NULL, "host",
				  KRB5_NT_SRV_HST, &principal);
    if (ret)
	goto out;

    ret = krb5_cc_cache_match(context, principal, id);
    if (ret == 0)
	return 0;

    /* did not find in default credcache, lets try default keytab */
    ret = krb5_kt_default(context, &kt);
    if (ret)
	goto out;

    /* XXX check in keytab */
    {
	krb5_get_init_creds_opt *opt;
	krb5_creds cred;

	memset(&cred, 0, sizeof(cred));

	ret = krb5_cc_new_unique(context, "MEMORY", NULL, id);
	if (ret)
	    goto out;
	*destroy = 1;
	ret = krb5_get_init_creds_opt_alloc(context, &opt);
	if (ret)
	    goto out;
	ret = krb5_get_init_creds_keytab (context,
					  &cred,
					  principal,
					  kt,
					  0,
					  NULL,
					  opt);
	krb5_get_init_creds_opt_free(context, opt);
	if (ret)
	    goto out;
	ret = krb5_cc_initialize (context, *id, cred.client);
	if (ret) {
	    krb5_free_cred_contents (context, &cred);
	    goto out;
	}
	ret = krb5_cc_store_cred (context, *id, &cred);
	krb5_free_cred_contents (context, &cred);
	if (ret)
	    goto out;
    }

    krb5_kt_close(context, kt);

    return 0;

out:
    if (*id) {
	if (*destroy)
	    krb5_cc_destroy(context, *id);
	else
	    krb5_cc_close(context, *id);
	*id = NULL;
    }

    if (kt)
	krb5_kt_close(context, kt);

    if (principal)
	krb5_free_principal(context, principal);
    return ret;
}

/*
 *
 */

static OM_uint32
kdc_alloc(OM_uint32 *minor, void **ctx)
{
    krb5_error_code ret;
    struct ntlmkrb5 *c;
    OM_uint32 junk;

    c = calloc(1, sizeof(*c));
    if (c == NULL) {
	*minor = ENOMEM;
	return GSS_S_FAILURE;
    }

    ret = krb5_init_context(&c->context);
    if (ret) {
	kdc_destroy(&junk, c);
	*minor = ret;
	return GSS_S_FAILURE;
    }

    ret = get_ccache(c->context, &c->destroy, &c->id);
    if (ret) {
	kdc_destroy(&junk, c);
	*minor = ret;
	return GSS_S_FAILURE;
    }

    ret = krb5_ntlm_alloc(c->context, &c->ntlm);
    if (ret) {
	kdc_destroy(&junk, c);
	*minor = ret;
	return GSS_S_FAILURE;
    }

    *ctx = c;

    return GSS_S_COMPLETE;
}

static int
kdc_probe(OM_uint32 *minor, void *ctx, const char *realm)
{
    struct ntlmkrb5 *c = ctx;
    krb5_error_code ret;
    unsigned flags;

    ret = krb5_digest_probe(c->context, rk_UNCONST(realm), c->id, &flags);
    if (ret)
	return ret;

    if ((flags & (1|2|4)) == 0)
	return EINVAL;

    return 0;
}

/*
 *
 */

static OM_uint32
kdc_destroy(OM_uint32 *minor, void *ctx)
{
    struct ntlmkrb5 *c = ctx;
    krb5_data_free(&c->opaque);
    krb5_data_free(&c->sessionkey);
    if (c->ntlm)
	krb5_ntlm_free(c->context, c->ntlm);
    if (c->id) {
	if (c->destroy)
	    krb5_cc_destroy(c->context, c->id);
	else
	    krb5_cc_close(c->context, c->id);
    }
    if (c->context)
	krb5_free_context(c->context);
    memset(c, 0, sizeof(*c));
    free(c);

    return GSS_S_COMPLETE;
}

/*
 *
 */

static OM_uint32
kdc_type2(OM_uint32 *minor_status,
	  void *ctx,
	  uint32_t flags,
	  const char *hostname,
	  const char *domain,
	  uint32_t *ret_flags,
	  struct ntlm_buf *out)
{
    struct ntlmkrb5 *c = ctx;
    krb5_error_code ret;
    struct ntlm_type2 type2;
    krb5_data challange;
    struct ntlm_buf data;
    krb5_data ti;

    memset(&type2, 0, sizeof(type2));

    /*
     * Request data for type 2 packet from the KDC.
     */
    ret = krb5_ntlm_init_request(c->context,
				 c->ntlm,
				 NULL,
				 c->id,
				 flags,
				 hostname,
				 domain);
    if (ret) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    /*
     *
     */

    ret = krb5_ntlm_init_get_opaque(c->context, c->ntlm, &c->opaque);
    if (ret) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    /*
     *
     */

    ret = krb5_ntlm_init_get_flags(c->context, c->ntlm, &type2.flags);
    if (ret) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }
    *ret_flags = type2.flags;

    ret = krb5_ntlm_init_get_challange(c->context, c->ntlm, &challange);
    if (ret) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    if (challange.length != sizeof(type2.challenge)) {
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }
    memcpy(type2.challenge, challange.data, sizeof(type2.challenge));
    krb5_data_free(&challange);

    ret = krb5_ntlm_init_get_targetname(c->context, c->ntlm,
					&type2.targetname);
    if (ret) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    ret = krb5_ntlm_init_get_targetinfo(c->context, c->ntlm, &ti);
    if (ret) {
	free(type2.targetname);
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    type2.targetinfo.data = ti.data;
    type2.targetinfo.length = ti.length;

    ret = heim_ntlm_encode_type2(&type2, &data);
    free(type2.targetname);
    krb5_data_free(&ti);
    if (ret) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    out->data = data.data;
    out->length = data.length;

    return GSS_S_COMPLETE;
}

/*
 *
 */

static OM_uint32
kdc_type3(OM_uint32 *minor_status,
	  void *ctx,
	  const struct ntlm_type3 *type3,
	  struct ntlm_buf *sessionkey)
{
    struct ntlmkrb5 *c = ctx;
    krb5_error_code ret;

    sessionkey->data = NULL;
    sessionkey->length = 0;

    ret = krb5_ntlm_req_set_flags(c->context, c->ntlm, type3->flags);
    if (ret) goto out;
    ret = krb5_ntlm_req_set_username(c->context, c->ntlm, type3->username);
    if (ret) goto out;
    ret = krb5_ntlm_req_set_targetname(c->context, c->ntlm,
				       type3->targetname);
    if (ret) goto out;
    ret = krb5_ntlm_req_set_lm(c->context, c->ntlm,
			       type3->lm.data, type3->lm.length);
    if (ret) goto out;
    ret = krb5_ntlm_req_set_ntlm(c->context, c->ntlm,
				 type3->ntlm.data, type3->ntlm.length);
    if (ret) goto out;
    ret = krb5_ntlm_req_set_opaque(c->context, c->ntlm, &c->opaque);
    if (ret) goto out;

    if (type3->sessionkey.length) {
	ret = krb5_ntlm_req_set_session(c->context, c->ntlm,
					type3->sessionkey.data,
					type3->sessionkey.length);
	if (ret) goto out;
    }

    /*
     * Verify with the KDC the type3 packet is ok
     */
    ret = krb5_ntlm_request(c->context,
			    c->ntlm,
			    NULL,
			    c->id);
    if (ret)
	goto out;

    if (krb5_ntlm_rep_get_status(c->context, c->ntlm) != TRUE) {
	ret = EINVAL;
	goto out;
    }

    if (type3->sessionkey.length) {
	ret = krb5_ntlm_rep_get_sessionkey(c->context,
					   c->ntlm,
					   &c->sessionkey);
	if (ret)
	    goto out;

	sessionkey->data = c->sessionkey.data;
	sessionkey->length = c->sessionkey.length;
    }

    return 0;

 out:
    *minor_status = ret;
    return GSS_S_FAILURE;
}

/*
 *
 */

static void
kdc_free_buffer(struct ntlm_buf *sessionkey)
{
    if (sessionkey->data)
	free(sessionkey->data);
    sessionkey->data = NULL;
    sessionkey->length = 0;
}

/*
 *
 */

struct ntlm_server_interface ntlmsspi_kdc_digest = {
    kdc_alloc,
    kdc_destroy,
    kdc_probe,
    kdc_type2,
    kdc_type3,
    kdc_free_buffer
};

#endif /* DIGEST */
