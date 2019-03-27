/*-
 * Copyright (c) 2005 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD: src/lib/libgssapi/gss_krb5.c,v 1.1 2005/12/29 14:40:20 dfr Exp $
 */

#include "mech_locl.h"

#include <krb5.h>
#include <roken.h>


GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_krb5_copy_ccache(OM_uint32 *minor_status,
		     gss_cred_id_t cred,
		     krb5_ccache out)
{
    gss_buffer_set_t data_set = GSS_C_NO_BUFFER_SET;
    krb5_context context;
    krb5_error_code kret;
    krb5_ccache id;
    OM_uint32 ret;
    char *str = NULL;

    ret = gss_inquire_cred_by_oid(minor_status,
				  cred,
				  GSS_KRB5_COPY_CCACHE_X,
				  &data_set);
    if (ret)
	return ret;

    if (data_set == GSS_C_NO_BUFFER_SET || data_set->count < 1) {
	gss_release_buffer_set(minor_status, &data_set);
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    kret = krb5_init_context(&context);
    if (kret) {
	*minor_status = kret;
	gss_release_buffer_set(minor_status, &data_set);
	return GSS_S_FAILURE;
    }

    kret = asprintf(&str, "%.*s", (int)data_set->elements[0].length,
		    (char *)data_set->elements[0].value);
    gss_release_buffer_set(minor_status, &data_set);
    if (kret < 0 || str == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    kret = krb5_cc_resolve(context, str, &id);
    free(str);
    if (kret) {
	*minor_status = kret;
	return GSS_S_FAILURE;
    }

    kret = krb5_cc_copy_cache(context, id, out);
    krb5_cc_close(context, id);
    krb5_free_context(context);
    if (kret) {
	*minor_status = kret;
	return GSS_S_FAILURE;
    }

    return ret;
}

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_krb5_import_cred(OM_uint32 *minor_status,
		     krb5_ccache id,
		     krb5_principal keytab_principal,
		     krb5_keytab keytab,
		     gss_cred_id_t *cred)
{
    gss_buffer_desc buffer;
    OM_uint32 major_status;
    krb5_context context;
    krb5_error_code ret;
    krb5_storage *sp;
    krb5_data data;
    char *str;

    *cred = GSS_C_NO_CREDENTIAL;

    ret = krb5_init_context(&context);
    if (ret) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    sp = krb5_storage_emem();
    if (sp == NULL) {
	*minor_status = ENOMEM;
	major_status = GSS_S_FAILURE;
	goto out;
    }

    if (id) {
	ret = krb5_cc_get_full_name(context, id, &str);
	if (ret == 0) {
	    ret = krb5_store_string(sp, str);
	    free(str);
	}
    } else
	ret = krb5_store_string(sp, "");
    if (ret) {
	*minor_status = ret;
	major_status = GSS_S_FAILURE;
	goto out;
    }

    if (keytab_principal) {
	ret = krb5_unparse_name(context, keytab_principal, &str);
	if (ret == 0) {
	    ret = krb5_store_string(sp, str);
	    free(str);
	}
    } else
	krb5_store_string(sp, "");
    if (ret) {
	*minor_status = ret;
	major_status = GSS_S_FAILURE;
	goto out;
    }


    if (keytab) {
	ret = krb5_kt_get_full_name(context, keytab, &str);
	if (ret == 0) {
	    ret = krb5_store_string(sp, str);
	    free(str);
	}
    } else
	krb5_store_string(sp, "");
    if (ret) {
	*minor_status = ret;
	major_status = GSS_S_FAILURE;
	goto out;
    }

    ret = krb5_storage_to_data(sp, &data);
    if (ret) {
	*minor_status = ret;
	major_status = GSS_S_FAILURE;
	goto out;
    }

    buffer.value = data.data;
    buffer.length = data.length;

    major_status = gss_set_cred_option(minor_status,
				       cred,
				       GSS_KRB5_IMPORT_CRED_X,
				       &buffer);
    krb5_data_free(&data);
out:
    if (sp)
	krb5_storage_free(sp);
    krb5_free_context(context);
    return major_status;
}

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_register_acceptor_identity(const char *identity)
{
	gssapi_mech_interface m;
	gss_buffer_desc buffer;
	OM_uint32 junk;

	_gss_load_mech();

	buffer.value = rk_UNCONST(identity);
	buffer.length = strlen(identity);

	m = __gss_get_mechanism(GSS_KRB5_MECHANISM);
	if (m == NULL || m->gm_set_sec_context_option == NULL)
	    return GSS_S_FAILURE;

	return m->gm_set_sec_context_option(&junk, NULL,
	        GSS_KRB5_REGISTER_ACCEPTOR_IDENTITY_X, &buffer);
}

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
krb5_gss_register_acceptor_identity(const char *identity)
{
    return gsskrb5_register_acceptor_identity(identity);
}


GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_set_dns_canonicalize(int flag)
{
        struct _gss_mech_switch	*m;
	gss_buffer_desc buffer;
	OM_uint32 junk;
	char b = (flag != 0);

	_gss_load_mech();

	buffer.value = &b;
	buffer.length = sizeof(b);

	HEIM_SLIST_FOREACH(m, &_gss_mechs, gm_link) {
		if (m->gm_mech.gm_set_sec_context_option == NULL)
			continue;
		m->gm_mech.gm_set_sec_context_option(&junk, NULL,
		    GSS_KRB5_SET_DNS_CANONICALIZE_X, &buffer);
	}

	return (GSS_S_COMPLETE);
}



static krb5_error_code
set_key(krb5_keyblock *keyblock, gss_krb5_lucid_key_t *key)
{
    key->type = keyblock->keytype;
    key->length = keyblock->keyvalue.length;
    key->data = malloc(key->length);
    if (key->data == NULL && key->length != 0)
	return ENOMEM;
    memcpy(key->data, keyblock->keyvalue.data, key->length);
    return 0;
}

static void
free_key(gss_krb5_lucid_key_t *key)
{
    memset(key->data, 0, key->length);
    free(key->data);
    memset(key, 0, sizeof(*key));
}

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_krb5_export_lucid_sec_context(OM_uint32 *minor_status,
				  gss_ctx_id_t *context_handle,
				  OM_uint32 version,
				  void **rctx)
{
    krb5_context context = NULL;
    krb5_error_code ret;
    gss_buffer_set_t data_set = GSS_C_NO_BUFFER_SET;
    OM_uint32 major_status;
    gss_krb5_lucid_context_v1_t *ctx = NULL;
    krb5_storage *sp = NULL;
    uint32_t num;

    if (context_handle == NULL
	|| *context_handle == GSS_C_NO_CONTEXT
	|| version != 1)
    {
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    major_status =
	gss_inquire_sec_context_by_oid (minor_status,
					*context_handle,
					GSS_KRB5_EXPORT_LUCID_CONTEXT_V1_X,
					&data_set);
    if (major_status)
	return major_status;

    if (data_set == GSS_C_NO_BUFFER_SET || data_set->count != 1) {
	gss_release_buffer_set(minor_status, &data_set);
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    ret = krb5_init_context(&context);
    if (ret)
	goto out;

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
	ret = ENOMEM;
	goto out;
    }

    sp = krb5_storage_from_mem(data_set->elements[0].value,
			       data_set->elements[0].length);
    if (sp == NULL) {
	ret = ENOMEM;
	goto out;
    }

    ret = krb5_ret_uint32(sp, &num);
    if (ret) goto out;
    if (num != 1) {
	ret = EINVAL;
	goto out;
    }
    ctx->version = 1;
    /* initiator */
    ret = krb5_ret_uint32(sp, &ctx->initiate);
    if (ret) goto out;
    /* endtime */
    ret = krb5_ret_uint32(sp, &ctx->endtime);
    if (ret) goto out;
    /* send_seq */
    ret = krb5_ret_uint32(sp, &num);
    if (ret) goto out;
    ctx->send_seq = ((uint64_t)num) << 32;
    ret = krb5_ret_uint32(sp, &num);
    if (ret) goto out;
    ctx->send_seq |= num;
    /* recv_seq */
    ret = krb5_ret_uint32(sp, &num);
    if (ret) goto out;
    ctx->recv_seq = ((uint64_t)num) << 32;
    ret = krb5_ret_uint32(sp, &num);
    if (ret) goto out;
    ctx->recv_seq |= num;
    /* protocol */
    ret = krb5_ret_uint32(sp, &ctx->protocol);
    if (ret) goto out;
    if (ctx->protocol == 0) {
	krb5_keyblock key;

	/* sign_alg */
	ret = krb5_ret_uint32(sp, &ctx->rfc1964_kd.sign_alg);
	if (ret) goto out;
	/* seal_alg */
	ret = krb5_ret_uint32(sp, &ctx->rfc1964_kd.seal_alg);
	if (ret) goto out;
	/* ctx_key */
	ret = krb5_ret_keyblock(sp, &key);
	if (ret) goto out;
	ret = set_key(&key, &ctx->rfc1964_kd.ctx_key);
	krb5_free_keyblock_contents(context, &key);
	if (ret) goto out;
    } else if (ctx->protocol == 1) {
	krb5_keyblock key;

	/* acceptor_subkey */
	ret = krb5_ret_uint32(sp, &ctx->cfx_kd.have_acceptor_subkey);
	if (ret) goto out;
	/* ctx_key */
	ret = krb5_ret_keyblock(sp, &key);
	if (ret) goto out;
	ret = set_key(&key, &ctx->cfx_kd.ctx_key);
	krb5_free_keyblock_contents(context, &key);
	if (ret) goto out;
	/* acceptor_subkey */
	if (ctx->cfx_kd.have_acceptor_subkey) {
	    ret = krb5_ret_keyblock(sp, &key);
	    if (ret) goto out;
	    ret = set_key(&key, &ctx->cfx_kd.acceptor_subkey);
	    krb5_free_keyblock_contents(context, &key);
	    if (ret) goto out;
	}
    } else {
	ret = EINVAL;
	goto out;
    }

    *rctx = ctx;

out:
    gss_release_buffer_set(minor_status, &data_set);
    if (sp)
	krb5_storage_free(sp);
    if (context)
	krb5_free_context(context);

    if (ret) {
	if (ctx)
	    gss_krb5_free_lucid_sec_context(NULL, ctx);

	*minor_status = ret;
	return GSS_S_FAILURE;
    }
    *minor_status = 0;
    return GSS_S_COMPLETE;
}

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_krb5_free_lucid_sec_context(OM_uint32 *minor_status, void *c)
{
    gss_krb5_lucid_context_v1_t *ctx = c;

    if (ctx->version != 1) {
	if (minor_status)
	    *minor_status = 0;
	return GSS_S_FAILURE;
    }

    if (ctx->protocol == 0) {
	free_key(&ctx->rfc1964_kd.ctx_key);
    } else if (ctx->protocol == 1) {
	free_key(&ctx->cfx_kd.ctx_key);
	if (ctx->cfx_kd.have_acceptor_subkey)
	    free_key(&ctx->cfx_kd.acceptor_subkey);
    }
    free(ctx);
    if (minor_status)
	*minor_status = 0;
    return GSS_S_COMPLETE;
}

/*
 *
 */

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_krb5_set_allowable_enctypes(OM_uint32 *minor_status,
				gss_cred_id_t cred,
				OM_uint32 num_enctypes,
				int32_t *enctypes)
{
    krb5_error_code ret;
    OM_uint32 maj_status;
    gss_buffer_desc buffer;
    krb5_storage *sp;
    krb5_data data;
    size_t i;

    sp = krb5_storage_emem();
    if (sp == NULL) {
	*minor_status = ENOMEM;
	maj_status = GSS_S_FAILURE;
	goto out;
    }

    for (i = 0; i < num_enctypes; i++) {
	ret = krb5_store_int32(sp, enctypes[i]);
	if (ret) {
	    *minor_status = ret;
	    maj_status = GSS_S_FAILURE;
	    goto out;
	}
    }

    ret = krb5_storage_to_data(sp, &data);
    if (ret) {
	*minor_status = ret;
	maj_status = GSS_S_FAILURE;
	goto out;
    }

    buffer.value = data.data;
    buffer.length = data.length;

    maj_status = gss_set_cred_option(minor_status,
				     &cred,
				     GSS_KRB5_SET_ALLOWABLE_ENCTYPES_X,
				     &buffer);
    krb5_data_free(&data);
out:
    if (sp)
	krb5_storage_free(sp);
    return maj_status;
}

/*
 *
 */

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_set_send_to_kdc(struct gsskrb5_send_to_kdc *c)
{
    struct _gss_mech_switch *m;
    gss_buffer_desc buffer;
    OM_uint32 junk;

    _gss_load_mech();

    if (c) {
	buffer.value = c;
	buffer.length = sizeof(*c);
    } else {
	buffer.value = NULL;
	buffer.length = 0;
    }

    HEIM_SLIST_FOREACH(m, &_gss_mechs, gm_link) {
	if (m->gm_mech.gm_set_sec_context_option == NULL)
	    continue;
	m->gm_mech.gm_set_sec_context_option(&junk, NULL,
	    GSS_KRB5_SEND_TO_KDC_X, &buffer);
    }

    return (GSS_S_COMPLETE);
}

/*
 *
 */

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_krb5_ccache_name(OM_uint32 *minor_status,
		     const char *name,
		     const char **out_name)
{
    struct _gss_mech_switch *m;
    gss_buffer_desc buffer;
    OM_uint32 junk;

    _gss_load_mech();

    if (out_name)
	*out_name = NULL;

    buffer.value = rk_UNCONST(name);
    buffer.length = strlen(name);

    HEIM_SLIST_FOREACH(m, &_gss_mechs, gm_link) {
	if (m->gm_mech.gm_set_sec_context_option == NULL)
	    continue;
	m->gm_mech.gm_set_sec_context_option(&junk, NULL,
	    GSS_KRB5_CCACHE_NAME_X, &buffer);
    }

    return (GSS_S_COMPLETE);
}


/*
 *
 */

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_extract_authtime_from_sec_context(OM_uint32 *minor_status,
					  gss_ctx_id_t context_handle,
					  time_t *authtime)
{
    gss_buffer_set_t data_set = GSS_C_NO_BUFFER_SET;
    OM_uint32 maj_stat;

    if (context_handle == GSS_C_NO_CONTEXT) {
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    maj_stat =
	gss_inquire_sec_context_by_oid (minor_status,
					context_handle,
					GSS_KRB5_GET_AUTHTIME_X,
					&data_set);
    if (maj_stat)
	return maj_stat;

    if (data_set == GSS_C_NO_BUFFER_SET) {
	gss_release_buffer_set(minor_status, &data_set);
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    if (data_set->count != 1) {
	gss_release_buffer_set(minor_status, &data_set);
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    if (data_set->elements[0].length != 4) {
	gss_release_buffer_set(minor_status, &data_set);
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    {
	unsigned char *buf = data_set->elements[0].value;
	*authtime = (buf[3] <<24) | (buf[2] << 16) |
	    (buf[1] << 8) | (buf[0] << 0);
    }

    gss_release_buffer_set(minor_status, &data_set);

    *minor_status = 0;
    return GSS_S_COMPLETE;
}

/*
 *
 */

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_extract_authz_data_from_sec_context(OM_uint32 *minor_status,
					    gss_ctx_id_t context_handle,
					    int ad_type,
					    gss_buffer_t ad_data)
{
    gss_buffer_set_t data_set = GSS_C_NO_BUFFER_SET;
    OM_uint32 maj_stat;
    gss_OID_desc oid_flat;
    heim_oid baseoid, oid;
    size_t size;

    if (context_handle == GSS_C_NO_CONTEXT) {
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    /* All this to append an integer to an oid... */

    if (der_get_oid(GSS_KRB5_EXTRACT_AUTHZ_DATA_FROM_SEC_CONTEXT_X->elements,
		    GSS_KRB5_EXTRACT_AUTHZ_DATA_FROM_SEC_CONTEXT_X->length,
		    &baseoid, NULL) != 0) {
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    oid.length = baseoid.length + 1;
    oid.components = calloc(oid.length, sizeof(*oid.components));
    if (oid.components == NULL) {
	der_free_oid(&baseoid);

	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    memcpy(oid.components, baseoid.components,
	   baseoid.length * sizeof(*baseoid.components));

    der_free_oid(&baseoid);

    oid.components[oid.length - 1] = ad_type;

    oid_flat.length = der_length_oid(&oid);
    oid_flat.elements = malloc(oid_flat.length);
    if (oid_flat.elements == NULL) {
	free(oid.components);
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    if (der_put_oid((unsigned char *)oid_flat.elements + oid_flat.length - 1,
		    oid_flat.length, &oid, &size) != 0) {
	free(oid.components);
	free(oid_flat.elements);
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }
    if (oid_flat.length != size)
	abort();

    free(oid.components);

    /* FINALLY, we have the OID */

    maj_stat = gss_inquire_sec_context_by_oid (minor_status,
					       context_handle,
					       &oid_flat,
					       &data_set);

    free(oid_flat.elements);

    if (maj_stat)
	return maj_stat;

    if (data_set == GSS_C_NO_BUFFER_SET || data_set->count != 1) {
	gss_release_buffer_set(minor_status, &data_set);
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    ad_data->value = malloc(data_set->elements[0].length);
    if (ad_data->value == NULL) {
	gss_release_buffer_set(minor_status, &data_set);
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    ad_data->length = data_set->elements[0].length;
    memcpy(ad_data->value, data_set->elements[0].value, ad_data->length);
    gss_release_buffer_set(minor_status, &data_set);

    *minor_status = 0;
    return GSS_S_COMPLETE;
}

/*
 *
 */

static OM_uint32
gsskrb5_extract_key(OM_uint32 *minor_status,
		    gss_ctx_id_t context_handle,
		    const gss_OID oid,
		    krb5_keyblock **keyblock)
{
    krb5_error_code ret;
    gss_buffer_set_t data_set = GSS_C_NO_BUFFER_SET;
    OM_uint32 major_status;
    krb5_context context = NULL;
    krb5_storage *sp = NULL;

    if (context_handle == GSS_C_NO_CONTEXT) {
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    ret = krb5_init_context(&context);
    if(ret) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    major_status =
	gss_inquire_sec_context_by_oid (minor_status,
					context_handle,
					oid,
					&data_set);
    if (major_status)
	return major_status;

    if (data_set == GSS_C_NO_BUFFER_SET || data_set->count != 1) {
	gss_release_buffer_set(minor_status, &data_set);
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    sp = krb5_storage_from_mem(data_set->elements[0].value,
			       data_set->elements[0].length);
    if (sp == NULL) {
	ret = ENOMEM;
	goto out;
    }

    *keyblock = calloc(1, sizeof(**keyblock));
    if (keyblock == NULL) {
	ret = ENOMEM;
	goto out;
    }

    ret = krb5_ret_keyblock(sp, *keyblock);

out:
    gss_release_buffer_set(minor_status, &data_set);
    if (sp)
	krb5_storage_free(sp);
    if (ret && keyblock) {
	krb5_free_keyblock(context, *keyblock);
	*keyblock = NULL;
    }
    if (context)
	krb5_free_context(context);

    *minor_status = ret;
    if (ret)
	return GSS_S_FAILURE;

    return GSS_S_COMPLETE;
}

/*
 *
 */

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_extract_service_keyblock(OM_uint32 *minor_status,
				 gss_ctx_id_t context_handle,
				 krb5_keyblock **keyblock)
{
    return gsskrb5_extract_key(minor_status,
			       context_handle,
			       GSS_KRB5_GET_SERVICE_KEYBLOCK_X,
			       keyblock);
}

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_get_initiator_subkey(OM_uint32 *minor_status,
			     gss_ctx_id_t context_handle,
			     krb5_keyblock **keyblock)
{
    return gsskrb5_extract_key(minor_status,
			       context_handle,
			       GSS_KRB5_GET_INITIATOR_SUBKEY_X,
			       keyblock);
}

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_get_subkey(OM_uint32 *minor_status,
		   gss_ctx_id_t context_handle,
		   krb5_keyblock **keyblock)
{
    return gsskrb5_extract_key(minor_status,
			       context_handle,
			       GSS_KRB5_GET_SUBKEY_X,
			       keyblock);
}

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_set_default_realm(const char *realm)
{
        struct _gss_mech_switch	*m;
	gss_buffer_desc buffer;
	OM_uint32 junk;

	_gss_load_mech();

	buffer.value = rk_UNCONST(realm);
	buffer.length = strlen(realm);

	HEIM_SLIST_FOREACH(m, &_gss_mechs, gm_link) {
		if (m->gm_mech.gm_set_sec_context_option == NULL)
			continue;
		m->gm_mech.gm_set_sec_context_option(&junk, NULL,
		    GSS_KRB5_SET_DEFAULT_REALM_X, &buffer);
	}

	return (GSS_S_COMPLETE);
}

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_krb5_get_tkt_flags(OM_uint32 *minor_status,
		       gss_ctx_id_t context_handle,
		       OM_uint32 *tkt_flags)
{

    OM_uint32 major_status;
    gss_buffer_set_t data_set = GSS_C_NO_BUFFER_SET;

    if (context_handle == GSS_C_NO_CONTEXT) {
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    major_status =
	gss_inquire_sec_context_by_oid (minor_status,
					context_handle,
					GSS_KRB5_GET_TKT_FLAGS_X,
					&data_set);
    if (major_status)
	return major_status;

    if (data_set == GSS_C_NO_BUFFER_SET ||
	data_set->count != 1 ||
	data_set->elements[0].length < 4) {
	gss_release_buffer_set(minor_status, &data_set);
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    {
	const u_char *p = data_set->elements[0].value;
	*tkt_flags = (p[0] << 0) | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    }

    gss_release_buffer_set(minor_status, &data_set);
    return GSS_S_COMPLETE;
}

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_set_time_offset(int offset)
{
        struct _gss_mech_switch	*m;
	gss_buffer_desc buffer;
	OM_uint32 junk;
	int32_t o = offset;

	_gss_load_mech();

	buffer.value = &o;
	buffer.length = sizeof(o);

	HEIM_SLIST_FOREACH(m, &_gss_mechs, gm_link) {
		if (m->gm_mech.gm_set_sec_context_option == NULL)
			continue;
		m->gm_mech.gm_set_sec_context_option(&junk, NULL,
		    GSS_KRB5_SET_TIME_OFFSET_X, &buffer);
	}

	return (GSS_S_COMPLETE);
}

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_get_time_offset(int *offset)
{
        struct _gss_mech_switch	*m;
	gss_buffer_desc buffer;
	OM_uint32 maj_stat, junk;
	int32_t o;

	_gss_load_mech();

	buffer.value = &o;
	buffer.length = sizeof(o);

	HEIM_SLIST_FOREACH(m, &_gss_mechs, gm_link) {
		if (m->gm_mech.gm_set_sec_context_option == NULL)
			continue;
		maj_stat = m->gm_mech.gm_set_sec_context_option(&junk, NULL,
		    GSS_KRB5_GET_TIME_OFFSET_X, &buffer);

		if (maj_stat == GSS_S_COMPLETE) {
			*offset = o;
			return maj_stat;
		}
	}

	return (GSS_S_UNAVAILABLE);
}

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_plugin_register(struct gsskrb5_krb5_plugin *c)
{
    struct _gss_mech_switch *m;
    gss_buffer_desc buffer;
    OM_uint32 junk;

    _gss_load_mech();

    buffer.value = c;
    buffer.length = sizeof(*c);

    HEIM_SLIST_FOREACH(m, &_gss_mechs, gm_link) {
	if (m->gm_mech.gm_set_sec_context_option == NULL)
	    continue;
	m->gm_mech.gm_set_sec_context_option(&junk, NULL,
	    GSS_KRB5_PLUGIN_REGISTER_X, &buffer);
    }

    return (GSS_S_COMPLETE);
}
