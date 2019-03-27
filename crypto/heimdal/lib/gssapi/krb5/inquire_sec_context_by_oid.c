/*
 * Copyright (c) 2004, PADL Software Pty Ltd.
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
 * 3. Neither the name of PADL Software nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PADL SOFTWARE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL PADL SOFTWARE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "gsskrb5_locl.h"

static int
oid_prefix_equal(gss_OID oid_enc, gss_OID prefix_enc, unsigned *suffix)
{
    int ret;
    heim_oid oid;
    heim_oid prefix;

    *suffix = 0;

    ret = der_get_oid(oid_enc->elements, oid_enc->length,
		      &oid, NULL);
    if (ret) {
	return 0;
    }

    ret = der_get_oid(prefix_enc->elements, prefix_enc->length,
		      &prefix, NULL);
    if (ret) {
	der_free_oid(&oid);
	return 0;
    }

    ret = 0;

    if (oid.length - 1 == prefix.length) {
	*suffix = oid.components[oid.length - 1];
	oid.length--;
	ret = (der_heim_oid_cmp(&oid, &prefix) == 0);
	oid.length++;
    }

    der_free_oid(&oid);
    der_free_oid(&prefix);

    return ret;
}

static OM_uint32 inquire_sec_context_tkt_flags
           (OM_uint32 *minor_status,
            const gsskrb5_ctx context_handle,
            gss_buffer_set_t *data_set)
{
    OM_uint32 tkt_flags;
    unsigned char buf[4];
    gss_buffer_desc value;

    HEIMDAL_MUTEX_lock(&context_handle->ctx_id_mutex);

    if (context_handle->ticket == NULL) {
	HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);
	_gsskrb5_set_status(EINVAL, "No ticket from which to obtain flags");
	*minor_status = EINVAL;
	return GSS_S_BAD_MECH;
    }

    tkt_flags = TicketFlags2int(context_handle->ticket->ticket.flags);
    HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);

    _gsskrb5_encode_om_uint32(tkt_flags, buf);
    value.length = sizeof(buf);
    value.value = buf;

    return gss_add_buffer_set_member(minor_status,
				     &value,
				     data_set);
}

enum keytype { ACCEPTOR_KEY, INITIATOR_KEY, TOKEN_KEY };

static OM_uint32 inquire_sec_context_get_subkey
           (OM_uint32 *minor_status,
            const gsskrb5_ctx context_handle,
	    krb5_context context,
	    enum keytype keytype,
            gss_buffer_set_t *data_set)
{
    krb5_keyblock *key = NULL;
    krb5_storage *sp = NULL;
    krb5_data data;
    OM_uint32 maj_stat = GSS_S_COMPLETE;
    krb5_error_code ret;

    krb5_data_zero(&data);

    sp = krb5_storage_emem();
    if (sp == NULL) {
	_gsskrb5_clear_status();
	ret = ENOMEM;
	goto out;
    }

    HEIMDAL_MUTEX_lock(&context_handle->ctx_id_mutex);
    switch(keytype) {
    case ACCEPTOR_KEY:
	ret = _gsskrb5i_get_acceptor_subkey(context_handle, context, &key);
	break;
    case INITIATOR_KEY:
	ret = _gsskrb5i_get_initiator_subkey(context_handle, context, &key);
	break;
    case TOKEN_KEY:
	ret = _gsskrb5i_get_token_key(context_handle, context, &key);
	break;
    default:
	_gsskrb5_set_status(EINVAL, "%d is not a valid subkey type", keytype);
	ret = EINVAL;
	break;
   }
    HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);
    if (ret)
	goto out;
    if (key == NULL) {
	_gsskrb5_set_status(EINVAL, "have no subkey of type %d", keytype);
	ret = EINVAL;
	goto out;
    }

    ret = krb5_store_keyblock(sp, *key);
    krb5_free_keyblock (context, key);
    if (ret)
	goto out;

    ret = krb5_storage_to_data(sp, &data);
    if (ret)
	goto out;

    {
	gss_buffer_desc value;

	value.length = data.length;
	value.value = data.data;

	maj_stat = gss_add_buffer_set_member(minor_status,
					     &value,
					     data_set);
    }

out:
    krb5_data_free(&data);
    if (sp)
	krb5_storage_free(sp);
    if (ret) {
	*minor_status = ret;
	maj_stat = GSS_S_FAILURE;
    }
    return maj_stat;
}

static OM_uint32 inquire_sec_context_get_sspi_session_key
            (OM_uint32 *minor_status,
             const gsskrb5_ctx context_handle,
             krb5_context context,
             gss_buffer_set_t *data_set)
{
    krb5_keyblock *key;
    OM_uint32 maj_stat = GSS_S_COMPLETE;
    krb5_error_code ret;
    gss_buffer_desc value;

    HEIMDAL_MUTEX_lock(&context_handle->ctx_id_mutex);
    ret = _gsskrb5i_get_token_key(context_handle, context, &key);
    HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);

    if (ret)
        goto out;
    if (key == NULL) {
        ret = EINVAL;
        goto out;
    }

    value.length = key->keyvalue.length;
    value.value = key->keyvalue.data;

    maj_stat = gss_add_buffer_set_member(minor_status,
                                         &value,
                                         data_set);
    krb5_free_keyblock(context, key);

    /* MIT also returns the enctype encoded as an OID in data_set[1] */

out:
    if (ret) {
        *minor_status = ret;
        maj_stat = GSS_S_FAILURE;
    }
    return maj_stat;
}

static OM_uint32 inquire_sec_context_authz_data
           (OM_uint32 *minor_status,
            const gsskrb5_ctx context_handle,
	    krb5_context context,
            unsigned ad_type,
            gss_buffer_set_t *data_set)
{
    krb5_data data;
    gss_buffer_desc ad_data;
    OM_uint32 ret;

    *minor_status = 0;
    *data_set = GSS_C_NO_BUFFER_SET;

    HEIMDAL_MUTEX_lock(&context_handle->ctx_id_mutex);
    if (context_handle->ticket == NULL) {
	HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);
	*minor_status = EINVAL;
	_gsskrb5_set_status(EINVAL, "No ticket to obtain authz data from");
	return GSS_S_NO_CONTEXT;
    }

    ret = krb5_ticket_get_authorization_data_type(context,
						  context_handle->ticket,
						  ad_type,
						  &data);
    HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);
    if (ret) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    ad_data.value = data.data;
    ad_data.length = data.length;

    ret = gss_add_buffer_set_member(minor_status,
				    &ad_data,
				    data_set);

    krb5_data_free(&data);

    return ret;
}

static OM_uint32 inquire_sec_context_has_updated_spnego
           (OM_uint32 *minor_status,
            const gsskrb5_ctx context_handle,
            gss_buffer_set_t *data_set)
{
    int is_updated = 0;

    *minor_status = 0;
    *data_set = GSS_C_NO_BUFFER_SET;

    /*
     * For Windows SPNEGO implementations, both the initiator and the
     * acceptor are assumed to have been updated if a "newer" [CLAR] or
     * different enctype is negotiated for use by the Kerberos GSS-API
     * mechanism.
     */
    HEIMDAL_MUTEX_lock(&context_handle->ctx_id_mutex);
    is_updated = (context_handle->more_flags & IS_CFX);
    if (is_updated == 0) {
	krb5_keyblock *acceptor_subkey;

	if (context_handle->more_flags & LOCAL)
	    acceptor_subkey = context_handle->auth_context->remote_subkey;
	else
	    acceptor_subkey = context_handle->auth_context->local_subkey;

	if (acceptor_subkey != NULL)
	    is_updated = (acceptor_subkey->keytype !=
			  context_handle->auth_context->keyblock->keytype);
    }
    HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);

    return is_updated ? GSS_S_COMPLETE : GSS_S_FAILURE;
}

/*
 *
 */

static OM_uint32
export_lucid_sec_context_v1(OM_uint32 *minor_status,
			    gsskrb5_ctx context_handle,
			    krb5_context context,
			    gss_buffer_set_t *data_set)
{
    krb5_storage *sp = NULL;
    OM_uint32 major_status = GSS_S_COMPLETE;
    krb5_error_code ret;
    krb5_keyblock *key = NULL;
    int32_t number;
    int is_cfx;
    krb5_data data;

    *minor_status = 0;

    HEIMDAL_MUTEX_lock(&context_handle->ctx_id_mutex);

    is_cfx = (context_handle->more_flags & IS_CFX);

    sp = krb5_storage_emem();
    if (sp == NULL) {
	_gsskrb5_clear_status();
	ret = ENOMEM;
	goto out;
    }

    ret = krb5_store_int32(sp, 1);
    if (ret) goto out;
    ret = krb5_store_int32(sp, (context_handle->more_flags & LOCAL) ? 1 : 0);
    if (ret) goto out;
    ret = krb5_store_int32(sp, context_handle->lifetime);
    if (ret) goto out;
    krb5_auth_con_getlocalseqnumber (context,
				     context_handle->auth_context,
				     &number);
    ret = krb5_store_uint32(sp, (uint32_t)0); /* store top half as zero */
    if (ret) goto out;
    ret = krb5_store_uint32(sp, (uint32_t)number);
    if (ret) goto out;
    krb5_auth_con_getremoteseqnumber (context,
				      context_handle->auth_context,
				      &number);
    ret = krb5_store_uint32(sp, (uint32_t)0); /* store top half as zero */
    if (ret) goto out;
    ret = krb5_store_uint32(sp, (uint32_t)number);
    if (ret) goto out;
    ret = krb5_store_int32(sp, (is_cfx) ? 1 : 0);
    if (ret) goto out;

    ret = _gsskrb5i_get_token_key(context_handle, context, &key);
    if (ret) goto out;

    if (is_cfx == 0) {
	int sign_alg, seal_alg;

	switch (key->keytype) {
	case ETYPE_DES_CBC_CRC:
	case ETYPE_DES_CBC_MD4:
	case ETYPE_DES_CBC_MD5:
	    sign_alg = 0;
	    seal_alg = 0;
	    break;
	case ETYPE_DES3_CBC_MD5:
	case ETYPE_DES3_CBC_SHA1:
	    sign_alg = 4;
	    seal_alg = 2;
	    break;
	case ETYPE_ARCFOUR_HMAC_MD5:
	case ETYPE_ARCFOUR_HMAC_MD5_56:
	    sign_alg = 17;
	    seal_alg = 16;
	    break;
	default:
	    sign_alg = -1;
	    seal_alg = -1;
	    break;
	}
	ret = krb5_store_int32(sp, sign_alg);
	if (ret) goto out;
	ret = krb5_store_int32(sp, seal_alg);
	if (ret) goto out;
	/* ctx_key */
	ret = krb5_store_keyblock(sp, *key);
	if (ret) goto out;
    } else {
	int subkey_p = (context_handle->more_flags & ACCEPTOR_SUBKEY) ? 1 : 0;

	/* have_acceptor_subkey */
	ret = krb5_store_int32(sp, subkey_p);
	if (ret) goto out;
	/* ctx_key */
	ret = krb5_store_keyblock(sp, *key);
	if (ret) goto out;
	/* acceptor_subkey */
	if (subkey_p) {
	    ret = krb5_store_keyblock(sp, *key);
	    if (ret) goto out;
	}
    }
    ret = krb5_storage_to_data(sp, &data);
    if (ret) goto out;

    {
	gss_buffer_desc ad_data;

	ad_data.value = data.data;
	ad_data.length = data.length;

	ret = gss_add_buffer_set_member(minor_status, &ad_data, data_set);
	krb5_data_free(&data);
	if (ret)
	    goto out;
    }

out:
    if (key)
	krb5_free_keyblock (context, key);
    if (sp)
	krb5_storage_free(sp);
    if (ret) {
	*minor_status = ret;
	major_status = GSS_S_FAILURE;
    }
    HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);
    return major_status;
}

static OM_uint32
get_authtime(OM_uint32 *minor_status,
	     gsskrb5_ctx ctx,
	     gss_buffer_set_t *data_set)

{
    gss_buffer_desc value;
    unsigned char buf[4];
    OM_uint32 authtime;

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);
    if (ctx->ticket == NULL) {
	HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	_gsskrb5_set_status(EINVAL, "No ticket to obtain auth time from");
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    authtime = ctx->ticket->ticket.authtime;

    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);

    _gsskrb5_encode_om_uint32(authtime, buf);
    value.length = sizeof(buf);
    value.value = buf;

    return gss_add_buffer_set_member(minor_status,
				     &value,
				     data_set);
}


static OM_uint32
get_service_keyblock
        (OM_uint32 *minor_status,
	 gsskrb5_ctx ctx,
	 gss_buffer_set_t *data_set)
{
    krb5_storage *sp = NULL;
    krb5_data data;
    OM_uint32 maj_stat = GSS_S_COMPLETE;
    krb5_error_code ret = EINVAL;

    sp = krb5_storage_emem();
    if (sp == NULL) {
	_gsskrb5_clear_status();
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);
    if (ctx->service_keyblock == NULL) {
	HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	krb5_storage_free(sp);
	_gsskrb5_set_status(EINVAL, "No service keyblock on gssapi context");
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    krb5_data_zero(&data);

    ret = krb5_store_keyblock(sp, *ctx->service_keyblock);

    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);

    if (ret)
	goto out;

    ret = krb5_storage_to_data(sp, &data);
    if (ret)
	goto out;

    {
	gss_buffer_desc value;

	value.length = data.length;
	value.value = data.data;

	maj_stat = gss_add_buffer_set_member(minor_status,
					     &value,
					     data_set);
    }

out:
    krb5_data_free(&data);
    if (sp)
	krb5_storage_free(sp);
    if (ret) {
	*minor_status = ret;
	maj_stat = GSS_S_FAILURE;
    }
    return maj_stat;
}
/*
 *
 */

OM_uint32 GSSAPI_CALLCONV _gsskrb5_inquire_sec_context_by_oid
           (OM_uint32 *minor_status,
            const gss_ctx_id_t context_handle,
            const gss_OID desired_object,
            gss_buffer_set_t *data_set)
{
    krb5_context context;
    const gsskrb5_ctx ctx = (const gsskrb5_ctx) context_handle;
    unsigned suffix;

    if (ctx == NULL) {
	*minor_status = EINVAL;
	return GSS_S_NO_CONTEXT;
    }

    GSSAPI_KRB5_INIT (&context);

    if (gss_oid_equal(desired_object, GSS_KRB5_GET_TKT_FLAGS_X)) {
	return inquire_sec_context_tkt_flags(minor_status,
					     ctx,
					     data_set);
    } else if (gss_oid_equal(desired_object, GSS_C_PEER_HAS_UPDATED_SPNEGO)) {
	return inquire_sec_context_has_updated_spnego(minor_status,
						      ctx,
						      data_set);
    } else if (gss_oid_equal(desired_object, GSS_KRB5_GET_SUBKEY_X)) {
	return inquire_sec_context_get_subkey(minor_status,
					      ctx,
					      context,
					      TOKEN_KEY,
					      data_set);
    } else if (gss_oid_equal(desired_object, GSS_KRB5_GET_INITIATOR_SUBKEY_X)) {
	return inquire_sec_context_get_subkey(minor_status,
					      ctx,
					      context,
					      INITIATOR_KEY,
					      data_set);
    } else if (gss_oid_equal(desired_object, GSS_KRB5_GET_ACCEPTOR_SUBKEY_X)) {
	return inquire_sec_context_get_subkey(minor_status,
					      ctx,
					      context,
					      ACCEPTOR_KEY,
					      data_set);
    } else if (gss_oid_equal(desired_object, GSS_C_INQ_SSPI_SESSION_KEY)) {
        return inquire_sec_context_get_sspi_session_key(minor_status,
                                                        ctx,
                                                        context,
                                                        data_set);
    } else if (gss_oid_equal(desired_object, GSS_KRB5_GET_AUTHTIME_X)) {
	return get_authtime(minor_status, ctx, data_set);
    } else if (oid_prefix_equal(desired_object,
				GSS_KRB5_EXTRACT_AUTHZ_DATA_FROM_SEC_CONTEXT_X,
				&suffix)) {
	return inquire_sec_context_authz_data(minor_status,
					      ctx,
					      context,
					      suffix,
					      data_set);
    } else if (oid_prefix_equal(desired_object,
				GSS_KRB5_EXPORT_LUCID_CONTEXT_X,
				&suffix)) {
	if (suffix == 1)
	    return export_lucid_sec_context_v1(minor_status,
					       ctx,
					       context,
					       data_set);
	*minor_status = 0;
	return GSS_S_FAILURE;
    } else if (gss_oid_equal(desired_object, GSS_KRB5_GET_SERVICE_KEYBLOCK_X)) {
	return get_service_keyblock(minor_status, ctx, data_set);
    } else {
	*minor_status = 0;
	return GSS_S_FAILURE;
    }
}

