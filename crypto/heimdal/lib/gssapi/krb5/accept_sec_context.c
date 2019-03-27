/*
 * Copyright (c) 1997 - 2006 Kungliga Tekniska Högskolan
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

#include "gsskrb5_locl.h"

HEIMDAL_MUTEX gssapi_keytab_mutex = HEIMDAL_MUTEX_INITIALIZER;
krb5_keytab _gsskrb5_keytab;

static krb5_error_code
validate_keytab(krb5_context context, const char *name, krb5_keytab *id)
{
    krb5_error_code ret;

    ret = krb5_kt_resolve(context, name, id);
    if (ret)
	return ret;

    ret = krb5_kt_have_content(context, *id);
    if (ret) {
	krb5_kt_close(context, *id);
	*id = NULL;
    }

    return ret;
}

OM_uint32
_gsskrb5_register_acceptor_identity(OM_uint32 *min_stat, const char *identity)
{
    krb5_context context;
    krb5_error_code ret;

    *min_stat = 0;

    ret = _gsskrb5_init(&context);
    if(ret)
	return GSS_S_FAILURE;

    HEIMDAL_MUTEX_lock(&gssapi_keytab_mutex);

    if(_gsskrb5_keytab != NULL) {
	krb5_kt_close(context, _gsskrb5_keytab);
	_gsskrb5_keytab = NULL;
    }
    if (identity == NULL) {
	ret = krb5_kt_default(context, &_gsskrb5_keytab);
    } else {
	/*
	 * First check if we can the keytab as is and if it has content...
	 */
	ret = validate_keytab(context, identity, &_gsskrb5_keytab);
	/*
	 * if it doesn't, lets prepend FILE: and try again
	 */
	if (ret) {
	    char *p = NULL;
	    ret = asprintf(&p, "FILE:%s", identity);
	    if(ret < 0 || p == NULL) {
		HEIMDAL_MUTEX_unlock(&gssapi_keytab_mutex);
		return GSS_S_FAILURE;
	    }
	    ret = validate_keytab(context, p, &_gsskrb5_keytab);
	    free(p);
	}
    }
    HEIMDAL_MUTEX_unlock(&gssapi_keytab_mutex);
    if(ret) {
	*min_stat = ret;
	return GSS_S_FAILURE;
    }
    return GSS_S_COMPLETE;
}

void
_gsskrb5i_is_cfx(krb5_context context, gsskrb5_ctx ctx, int acceptor)
{
    krb5_error_code ret;
    krb5_keyblock *key;

    if (acceptor) {
	if (ctx->auth_context->local_subkey)
	    key = ctx->auth_context->local_subkey;
	else
	    key = ctx->auth_context->remote_subkey;
    } else {
	if (ctx->auth_context->remote_subkey)
	    key = ctx->auth_context->remote_subkey;
	else
	    key = ctx->auth_context->local_subkey;
    }
    if (key == NULL)
	key = ctx->auth_context->keyblock;

    if (key == NULL)
	return;

    switch (key->keytype) {
    case ETYPE_DES_CBC_CRC:
    case ETYPE_DES_CBC_MD4:
    case ETYPE_DES_CBC_MD5:
    case ETYPE_DES3_CBC_MD5:
    case ETYPE_OLD_DES3_CBC_SHA1:
    case ETYPE_DES3_CBC_SHA1:
    case ETYPE_ARCFOUR_HMAC_MD5:
    case ETYPE_ARCFOUR_HMAC_MD5_56:
	break;
    default :
        ctx->more_flags |= IS_CFX;

	if ((acceptor && ctx->auth_context->local_subkey) ||
	    (!acceptor && ctx->auth_context->remote_subkey))
	    ctx->more_flags |= ACCEPTOR_SUBKEY;
	break;
    }
    if (ctx->crypto)
        krb5_crypto_destroy(context, ctx->crypto);
    ret = krb5_crypto_init(context, key, 0, &ctx->crypto);
}


static OM_uint32
gsskrb5_accept_delegated_token
(OM_uint32 * minor_status,
 gsskrb5_ctx ctx,
 krb5_context context,
 gss_cred_id_t * delegated_cred_handle
    )
{
    krb5_ccache ccache = NULL;
    krb5_error_code kret;
    int32_t ac_flags, ret = GSS_S_COMPLETE;

    *minor_status = 0;

    /* XXX Create a new delegated_cred_handle? */
    if (delegated_cred_handle == NULL) {
	kret = krb5_cc_default (context, &ccache);
    } else {
	*delegated_cred_handle = NULL;
	kret = krb5_cc_new_unique (context, krb5_cc_type_memory,
				   NULL, &ccache);
    }
    if (kret) {
	ctx->flags &= ~GSS_C_DELEG_FLAG;
	goto out;
    }

    kret = krb5_cc_initialize(context, ccache, ctx->source);
    if (kret) {
	ctx->flags &= ~GSS_C_DELEG_FLAG;
	goto out;
    }

    krb5_auth_con_removeflags(context,
			      ctx->auth_context,
			      KRB5_AUTH_CONTEXT_DO_TIME,
			      &ac_flags);
    kret = krb5_rd_cred2(context,
			 ctx->auth_context,
			 ccache,
			 &ctx->fwd_data);
    krb5_auth_con_setflags(context,
			   ctx->auth_context,
			   ac_flags);
    if (kret) {
	ctx->flags &= ~GSS_C_DELEG_FLAG;
	ret = GSS_S_FAILURE;
	*minor_status = kret;
	goto out;
    }

    if (delegated_cred_handle) {
	gsskrb5_cred handle;

	ret = _gsskrb5_krb5_import_cred(minor_status,
					ccache,
					NULL,
					NULL,
					delegated_cred_handle);
	if (ret != GSS_S_COMPLETE)
	    goto out;

	handle = (gsskrb5_cred) *delegated_cred_handle;

	handle->cred_flags |= GSS_CF_DESTROY_CRED_ON_RELEASE;
	krb5_cc_close(context, ccache);
	ccache = NULL;
    }

out:
    if (ccache) {
	/* Don't destroy the default cred cache */
	if (delegated_cred_handle == NULL)
	    krb5_cc_close(context, ccache);
	else
	    krb5_cc_destroy(context, ccache);
    }
    return ret;
}

static OM_uint32
gsskrb5_acceptor_ready(OM_uint32 * minor_status,
		       gsskrb5_ctx ctx,
		       krb5_context context,
		       gss_cred_id_t *delegated_cred_handle)
{
    OM_uint32 ret;
    int32_t seq_number;
    int is_cfx = 0;

    krb5_auth_con_getremoteseqnumber (context,
				      ctx->auth_context,
				      &seq_number);

    _gsskrb5i_is_cfx(context, ctx, 1);
    is_cfx = (ctx->more_flags & IS_CFX);

    ret = _gssapi_msg_order_create(minor_status,
				   &ctx->order,
				   _gssapi_msg_order_f(ctx->flags),
				   seq_number, 0, is_cfx);
    if (ret)
	return ret;

    /*
     * If requested, set local sequence num to remote sequence if this
     * isn't a mutual authentication context
     */
    if (!(ctx->flags & GSS_C_MUTUAL_FLAG) && _gssapi_msg_order_f(ctx->flags)) {
	krb5_auth_con_setlocalseqnumber(context,
					ctx->auth_context,
					seq_number);
    }

    /*
     * We should handle the delegation ticket, in case it's there
     */
    if (ctx->fwd_data.length > 0 && (ctx->flags & GSS_C_DELEG_FLAG)) {
	ret = gsskrb5_accept_delegated_token(minor_status,
					     ctx,
					     context,
					     delegated_cred_handle);
	if (ret)
	    return ret;
    } else {
	/* Well, looks like it wasn't there after all */
	ctx->flags &= ~GSS_C_DELEG_FLAG;
    }

    ctx->state = ACCEPTOR_READY;
    ctx->more_flags |= OPEN;

    return GSS_S_COMPLETE;
}

static OM_uint32
send_error_token(OM_uint32 *minor_status,
		 krb5_context context,
		 krb5_error_code kret,
		 krb5_principal server,
		 krb5_data *indata,
		 gss_buffer_t output_token)
{
    krb5_principal ap_req_server = NULL;
    krb5_error_code ret;
    krb5_data outbuf;
    /* this e_data value encodes KERB_AP_ERR_TYPE_SKEW_RECOVERY which
       tells windows to try again with the corrected timestamp. See
       [MS-KILE] 2.2.1 KERB-ERROR-DATA */
    krb5_data e_data = { 7, rk_UNCONST("\x30\x05\xa1\x03\x02\x01\x02") };

    /* build server from request if the acceptor had not selected one */
    if (server == NULL) {
	AP_REQ ap_req;

	ret = krb5_decode_ap_req(context, indata, &ap_req);
	if (ret) {
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}
	ret = _krb5_principalname2krb5_principal(context,
						  &ap_req_server,
						  ap_req.ticket.sname,
						  ap_req.ticket.realm);
	free_AP_REQ(&ap_req);
	if (ret) {
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}
	server = ap_req_server;
    }

    ret = krb5_mk_error(context, kret, NULL, &e_data, NULL,
			server, NULL, NULL, &outbuf);
    if (ap_req_server)
	krb5_free_principal(context, ap_req_server);
    if (ret) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    ret = _gsskrb5_encapsulate(minor_status,
			       &outbuf,
			       output_token,
			       "\x03\x00",
			       GSS_KRB5_MECHANISM);
    krb5_data_free (&outbuf);
    if (ret)
	return ret;

    *minor_status = 0;
    return GSS_S_CONTINUE_NEEDED;
}


static OM_uint32
gsskrb5_acceptor_start(OM_uint32 * minor_status,
		       gsskrb5_ctx ctx,
		       krb5_context context,
		       const gss_cred_id_t acceptor_cred_handle,
		       const gss_buffer_t input_token_buffer,
		       const gss_channel_bindings_t input_chan_bindings,
		       gss_name_t * src_name,
		       gss_OID * mech_type,
		       gss_buffer_t output_token,
		       OM_uint32 * ret_flags,
		       OM_uint32 * time_rec,
		       gss_cred_id_t * delegated_cred_handle)
{
    krb5_error_code kret;
    OM_uint32 ret = GSS_S_COMPLETE;
    krb5_data indata;
    krb5_flags ap_options;
    krb5_keytab keytab = NULL;
    int is_cfx = 0;
    const gsskrb5_cred acceptor_cred = (gsskrb5_cred)acceptor_cred_handle;

    /*
     * We may, or may not, have an escapsulation.
     */
    ret = _gsskrb5_decapsulate (minor_status,
				input_token_buffer,
				&indata,
				"\x01\x00",
				GSS_KRB5_MECHANISM);

    if (ret) {
	/* Assume that there is no OID wrapping. */
	indata.length	= input_token_buffer->length;
	indata.data	= input_token_buffer->value;
    }

    /*
     * We need to get our keytab
     */
    if (acceptor_cred == NULL) {
	if (_gsskrb5_keytab != NULL)
	    keytab = _gsskrb5_keytab;
    } else if (acceptor_cred->keytab != NULL) {
	keytab = acceptor_cred->keytab;
    }

    /*
     * We need to check the ticket and create the AP-REP packet
     */

    {
	krb5_rd_req_in_ctx in = NULL;
	krb5_rd_req_out_ctx out = NULL;
	krb5_principal server = NULL;

	if (acceptor_cred)
	    server = acceptor_cred->principal;

	kret = krb5_rd_req_in_ctx_alloc(context, &in);
	if (kret == 0)
	    kret = krb5_rd_req_in_set_keytab(context, in, keytab);
	if (kret) {
	    if (in)
		krb5_rd_req_in_ctx_free(context, in);
	    *minor_status = kret;
	    return GSS_S_FAILURE;
	}

	kret = krb5_rd_req_ctx(context,
			       &ctx->auth_context,
			       &indata,
			       server,
			       in, &out);
	krb5_rd_req_in_ctx_free(context, in);
	if (kret == KRB5KRB_AP_ERR_SKEW || kret == KRB5KRB_AP_ERR_TKT_NYV) {
	    /*
	     * No reply in non-MUTUAL mode, but we don't know that its
	     * non-MUTUAL mode yet, thats inside the 8003 checksum, so
	     * lets only send the error token on clock skew, that
	     * limit when send error token for non-MUTUAL.
	     */
	    return send_error_token(minor_status, context, kret,
				    server, &indata, output_token);
	} else if (kret) {
	    *minor_status = kret;
	    return GSS_S_FAILURE;
	}

	/*
	 * we need to remember some data on the context_handle.
	 */
	kret = krb5_rd_req_out_get_ap_req_options(context, out,
						  &ap_options);
	if (kret == 0)
	    kret = krb5_rd_req_out_get_ticket(context, out,
					      &ctx->ticket);
	if (kret == 0)
	    kret = krb5_rd_req_out_get_keyblock(context, out,
						&ctx->service_keyblock);
	ctx->lifetime = ctx->ticket->ticket.endtime;

	krb5_rd_req_out_ctx_free(context, out);
	if (kret) {
	    ret = GSS_S_FAILURE;
	    *minor_status = kret;
	    return ret;
	}
    }


    /*
     * We need to copy the principal names to the context and the
     * calling layer.
     */
    kret = krb5_copy_principal(context,
			       ctx->ticket->client,
			       &ctx->source);
    if (kret) {
	ret = GSS_S_FAILURE;
	*minor_status = kret;
    }

    kret = krb5_copy_principal(context,
			       ctx->ticket->server,
			       &ctx->target);
    if (kret) {
	ret = GSS_S_FAILURE;
	*minor_status = kret;
	return ret;
    }

    /*
     * We need to setup some compat stuff, this assumes that
     * context_handle->target is already set.
     */
    ret = _gss_DES3_get_mic_compat(minor_status, ctx, context);
    if (ret)
	return ret;

    if (src_name != NULL) {
	kret = krb5_copy_principal (context,
				    ctx->ticket->client,
				    (gsskrb5_name*)src_name);
	if (kret) {
	    ret = GSS_S_FAILURE;
	    *minor_status = kret;
	    return ret;
	}
    }

    /*
     * We need to get the flags out of the 8003 checksum.
     */

    {
	krb5_authenticator authenticator;

	kret = krb5_auth_con_getauthenticator(context,
					      ctx->auth_context,
					      &authenticator);
	if(kret) {
	    ret = GSS_S_FAILURE;
	    *minor_status = kret;
	    return ret;
	}

	if (authenticator->cksum == NULL) {
	    krb5_free_authenticator(context, &authenticator);
	    *minor_status = 0;
	    return GSS_S_BAD_BINDINGS;
	}

        if (authenticator->cksum->cksumtype == CKSUMTYPE_GSSAPI) {
            ret = _gsskrb5_verify_8003_checksum(minor_status,
						input_chan_bindings,
						authenticator->cksum,
						&ctx->flags,
						&ctx->fwd_data);

	    krb5_free_authenticator(context, &authenticator);
	    if (ret) {
		return ret;
	    }
        } else {
	    krb5_crypto crypto;

	    kret = krb5_crypto_init(context,
				    ctx->auth_context->keyblock,
				    0, &crypto);
	    if(kret) {
		krb5_free_authenticator(context, &authenticator);

		ret = GSS_S_FAILURE;
		*minor_status = kret;
		return ret;
	    }

	    /*
	     * Windows accepts Samba3's use of a kerberos, rather than
	     * GSSAPI checksum here
	     */

	    kret = krb5_verify_checksum(context,
					crypto, KRB5_KU_AP_REQ_AUTH_CKSUM, NULL, 0,
					authenticator->cksum);
	    krb5_free_authenticator(context, &authenticator);
	    krb5_crypto_destroy(context, crypto);

	    if(kret) {
		ret = GSS_S_BAD_SIG;
		*minor_status = kret;
		return ret;
	    }

	    /*
	     * Samba style get some flags (but not DCE-STYLE), use
	     * ap_options to guess the mutual flag.
	     */
 	    ctx->flags = GSS_C_REPLAY_FLAG | GSS_C_SEQUENCE_FLAG;
	    if (ap_options & AP_OPTS_MUTUAL_REQUIRED)
		ctx->flags |= GSS_C_MUTUAL_FLAG;
        }
    }

    if(ctx->flags & GSS_C_MUTUAL_FLAG) {
	krb5_data outbuf;
	int use_subkey = 0;

	_gsskrb5i_is_cfx(context, ctx, 1);
	is_cfx = (ctx->more_flags & IS_CFX);

	if (is_cfx || (ap_options & AP_OPTS_USE_SUBKEY)) {
	    use_subkey = 1;
	} else {
	    krb5_keyblock *rkey;

	    /*
	     * If there is a initiator subkey, copy that to acceptor
	     * subkey to match Windows behavior
	     */
	    kret = krb5_auth_con_getremotesubkey(context,
						 ctx->auth_context,
						 &rkey);
	    if (kret == 0) {
		kret = krb5_auth_con_setlocalsubkey(context,
						    ctx->auth_context,
						    rkey);
		if (kret == 0)
		    use_subkey = 1;
		krb5_free_keyblock(context, rkey);
	    }
	}
	if (use_subkey) {
	    ctx->more_flags |= ACCEPTOR_SUBKEY;
	    krb5_auth_con_addflags(context, ctx->auth_context,
				   KRB5_AUTH_CONTEXT_USE_SUBKEY,
				   NULL);
	}

	kret = krb5_mk_rep(context,
			   ctx->auth_context,
			   &outbuf);
	if (kret) {
	    *minor_status = kret;
	    return GSS_S_FAILURE;
	}

	if (IS_DCE_STYLE(ctx)) {
	    output_token->length = outbuf.length;
	    output_token->value = outbuf.data;
	} else {
	    ret = _gsskrb5_encapsulate(minor_status,
				       &outbuf,
				       output_token,
				       "\x02\x00",
				       GSS_KRB5_MECHANISM);
	    krb5_data_free (&outbuf);
	    if (ret)
		return ret;
	}
    }

    ctx->flags |= GSS_C_TRANS_FLAG;

    /* Remember the flags */

    ctx->lifetime = ctx->ticket->ticket.endtime;
    ctx->more_flags |= OPEN;

    if (mech_type)
	*mech_type = GSS_KRB5_MECHANISM;

    if (time_rec) {
	ret = _gsskrb5_lifetime_left(minor_status,
				     context,
				     ctx->lifetime,
				     time_rec);
	if (ret) {
	    return ret;
	}
    }

    /*
     * When GSS_C_DCE_STYLE is in use, we need ask for a AP-REP from
     * the client.
     */
    if (IS_DCE_STYLE(ctx)) {
	/*
	 * Return flags to caller, but we haven't processed
	 * delgations yet
	 */
	if (ret_flags)
	    *ret_flags = (ctx->flags & ~GSS_C_DELEG_FLAG);

	ctx->state = ACCEPTOR_WAIT_FOR_DCESTYLE;
	return GSS_S_CONTINUE_NEEDED;
    }

    ret = gsskrb5_acceptor_ready(minor_status, ctx, context,
				 delegated_cred_handle);

    if (ret_flags)
	*ret_flags = ctx->flags;

    return ret;
}

static OM_uint32
acceptor_wait_for_dcestyle(OM_uint32 * minor_status,
			   gsskrb5_ctx ctx,
			   krb5_context context,
			   const gss_cred_id_t acceptor_cred_handle,
			   const gss_buffer_t input_token_buffer,
			   const gss_channel_bindings_t input_chan_bindings,
			   gss_name_t * src_name,
			   gss_OID * mech_type,
			   gss_buffer_t output_token,
			   OM_uint32 * ret_flags,
			   OM_uint32 * time_rec,
			   gss_cred_id_t * delegated_cred_handle)
{
    OM_uint32 ret;
    krb5_error_code kret;
    krb5_data inbuf;
    int32_t r_seq_number, l_seq_number;

    /*
     * We know it's GSS_C_DCE_STYLE so we don't need to decapsulate the AP_REP
     */

    inbuf.length = input_token_buffer->length;
    inbuf.data = input_token_buffer->value;

    /*
     * We need to remeber the old remote seq_number, then check if the
     * client has replied with our local seq_number, and then reset
     * the remote seq_number to the old value
     */
    {
	kret = krb5_auth_con_getlocalseqnumber(context,
					       ctx->auth_context,
					       &l_seq_number);
	if (kret) {
	    *minor_status = kret;
	    return GSS_S_FAILURE;
	}

	kret = krb5_auth_con_getremoteseqnumber(context,
						ctx->auth_context,
						&r_seq_number);
	if (kret) {
	    *minor_status = kret;
	    return GSS_S_FAILURE;
	}

	kret = krb5_auth_con_setremoteseqnumber(context,
						ctx->auth_context,
						l_seq_number);
	if (kret) {
	    *minor_status = kret;
	    return GSS_S_FAILURE;
	}
    }

    /*
     * We need to verify the AP_REP, but we need to flag that this is
     * DCE_STYLE, so don't check the timestamps this time, but put the
     * flag DO_TIME back afterward.
    */
    {
	krb5_ap_rep_enc_part *repl;
	int32_t auth_flags;

	krb5_auth_con_removeflags(context,
				  ctx->auth_context,
				  KRB5_AUTH_CONTEXT_DO_TIME,
				  &auth_flags);

	kret = krb5_rd_rep(context, ctx->auth_context, &inbuf, &repl);
	if (kret) {
	    *minor_status = kret;
	    return GSS_S_FAILURE;
	}
	krb5_free_ap_rep_enc_part(context, repl);
	krb5_auth_con_setflags(context, ctx->auth_context, auth_flags);
    }

    /* We need to check the liftime */
    {
	OM_uint32 lifetime_rec;

	ret = _gsskrb5_lifetime_left(minor_status,
				     context,
				     ctx->lifetime,
				     &lifetime_rec);
	if (ret) {
	    return ret;
	}
	if (lifetime_rec == 0) {
	    return GSS_S_CONTEXT_EXPIRED;
	}

	if (time_rec) *time_rec = lifetime_rec;
    }

    /* We need to give the caller the flags which are in use */
    if (ret_flags) *ret_flags = ctx->flags;

    if (src_name) {
	kret = krb5_copy_principal(context,
				   ctx->source,
				   (gsskrb5_name*)src_name);
	if (kret) {
	    *minor_status = kret;
	    return GSS_S_FAILURE;
	}
    }

    /*
     * After the krb5_rd_rep() the remote and local seq_number should
     * be the same, because the client just replies the seq_number
     * from our AP-REP in its AP-REP, but then the client uses the
     * seq_number from its AP-REQ for GSS_wrap()
     */
    {
	int32_t tmp_r_seq_number, tmp_l_seq_number;

	kret = krb5_auth_con_getremoteseqnumber(context,
						ctx->auth_context,
						&tmp_r_seq_number);
	if (kret) {
	    *minor_status = kret;
	    return GSS_S_FAILURE;
	}

	kret = krb5_auth_con_getlocalseqnumber(context,
					       ctx->auth_context,
					       &tmp_l_seq_number);
	if (kret) {

	    *minor_status = kret;
	    return GSS_S_FAILURE;
	}

	/*
	 * Here we check if the client has responsed with our local seq_number,
	 */
	if (tmp_r_seq_number != tmp_l_seq_number) {
	    return GSS_S_UNSEQ_TOKEN;
	}
    }

    /*
     * We need to reset the remote seq_number, because the client will use,
     * the old one for the GSS_wrap() calls
     */
    {
	kret = krb5_auth_con_setremoteseqnumber(context,
						ctx->auth_context,
						r_seq_number);
	if (kret) {
	    *minor_status = kret;
	    return GSS_S_FAILURE;
	}
    }

    return gsskrb5_acceptor_ready(minor_status, ctx, context,
				  delegated_cred_handle);
}


OM_uint32 GSSAPI_CALLCONV
_gsskrb5_accept_sec_context(OM_uint32 * minor_status,
			    gss_ctx_id_t * context_handle,
			    const gss_cred_id_t acceptor_cred_handle,
			    const gss_buffer_t input_token_buffer,
			    const gss_channel_bindings_t input_chan_bindings,
			    gss_name_t * src_name,
			    gss_OID * mech_type,
			    gss_buffer_t output_token,
			    OM_uint32 * ret_flags,
			    OM_uint32 * time_rec,
			    gss_cred_id_t * delegated_cred_handle)
{
    krb5_context context;
    OM_uint32 ret;
    gsskrb5_ctx ctx;

    GSSAPI_KRB5_INIT(&context);

    output_token->length = 0;
    output_token->value = NULL;

    if (src_name != NULL)
	*src_name = NULL;
    if (mech_type)
	*mech_type = GSS_KRB5_MECHANISM;

    if (*context_handle == GSS_C_NO_CONTEXT) {
	ret = _gsskrb5_create_ctx(minor_status,
				  context_handle,
				  context,
				  input_chan_bindings,
				  ACCEPTOR_START);
	if (ret)
	    return ret;
    }

    ctx = (gsskrb5_ctx)*context_handle;


    /*
     * TODO: check the channel_bindings
     * (above just sets them to krb5 layer)
     */

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);

    switch (ctx->state) {
    case ACCEPTOR_START:
	ret = gsskrb5_acceptor_start(minor_status,
				     ctx,
				     context,
				     acceptor_cred_handle,
				     input_token_buffer,
				     input_chan_bindings,
				     src_name,
				     mech_type,
				     output_token,
				     ret_flags,
				     time_rec,
				     delegated_cred_handle);
	break;
    case ACCEPTOR_WAIT_FOR_DCESTYLE:
	ret = acceptor_wait_for_dcestyle(minor_status,
					 ctx,
					 context,
					 acceptor_cred_handle,
					 input_token_buffer,
					 input_chan_bindings,
					 src_name,
					 mech_type,
					 output_token,
					 ret_flags,
					 time_rec,
					 delegated_cred_handle);
	break;
    case ACCEPTOR_READY:
	/*
	 * If we get there, the caller have called
	 * gss_accept_sec_context() one time too many.
	 */
	ret =  GSS_S_BAD_STATUS;
	break;
    default:
	/* TODO: is this correct here? --metze */
	ret =  GSS_S_BAD_STATUS;
	break;
    }

    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);

    if (GSS_ERROR(ret)) {
	OM_uint32 min2;
	_gsskrb5_delete_sec_context(&min2, context_handle, GSS_C_NO_BUFFER);
    }

    return ret;
}
