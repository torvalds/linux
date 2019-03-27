/*
 * Copyright (c) 1997 - 2008 Kungliga Tekniska Högskolan
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

/*
 * copy the addresses from `input_chan_bindings' (if any) to
 * the auth context `ac'
 */

static OM_uint32
set_addresses (krb5_context context,
	       krb5_auth_context ac,
	       const gss_channel_bindings_t input_chan_bindings)
{
    /* Port numbers are expected to be in application_data.value,
     * initator's port first */

    krb5_address initiator_addr, acceptor_addr;
    krb5_error_code kret;

    if (input_chan_bindings == GSS_C_NO_CHANNEL_BINDINGS
	|| input_chan_bindings->application_data.length !=
	2 * sizeof(ac->local_port))
	return 0;

    memset(&initiator_addr, 0, sizeof(initiator_addr));
    memset(&acceptor_addr, 0, sizeof(acceptor_addr));

    ac->local_port =
	*(int16_t *) input_chan_bindings->application_data.value;

    ac->remote_port =
	*((int16_t *) input_chan_bindings->application_data.value + 1);

    kret = _gsskrb5i_address_to_krb5addr(context,
					 input_chan_bindings->acceptor_addrtype,
					 &input_chan_bindings->acceptor_address,
					 ac->remote_port,
					 &acceptor_addr);
    if (kret)
	return kret;

    kret = _gsskrb5i_address_to_krb5addr(context,
					 input_chan_bindings->initiator_addrtype,
					 &input_chan_bindings->initiator_address,
					 ac->local_port,
					 &initiator_addr);
    if (kret) {
	krb5_free_address (context, &acceptor_addr);
	return kret;
    }

    kret = krb5_auth_con_setaddrs(context,
				  ac,
				  &initiator_addr,  /* local address */
				  &acceptor_addr);  /* remote address */

    krb5_free_address (context, &initiator_addr);
    krb5_free_address (context, &acceptor_addr);

#if 0
    free(input_chan_bindings->application_data.value);
    input_chan_bindings->application_data.value = NULL;
    input_chan_bindings->application_data.length = 0;
#endif

    return kret;
}

OM_uint32
_gsskrb5_create_ctx(
        OM_uint32 * minor_status,
	gss_ctx_id_t * context_handle,
	krb5_context context,
 	const gss_channel_bindings_t input_chan_bindings,
 	enum gss_ctx_id_t_state state)
{
    krb5_error_code kret;
    gsskrb5_ctx ctx;

    *context_handle = NULL;

    ctx = malloc(sizeof(*ctx));
    if (ctx == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }
    ctx->auth_context		= NULL;
    ctx->deleg_auth_context	= NULL;
    ctx->source			= NULL;
    ctx->target			= NULL;
    ctx->kcred			= NULL;
    ctx->ccache			= NULL;
    ctx->state			= state;
    ctx->flags			= 0;
    ctx->more_flags		= 0;
    ctx->service_keyblock	= NULL;
    ctx->ticket			= NULL;
    krb5_data_zero(&ctx->fwd_data);
    ctx->lifetime		= GSS_C_INDEFINITE;
    ctx->order			= NULL;
    ctx->crypto			= NULL;
    HEIMDAL_MUTEX_init(&ctx->ctx_id_mutex);

    kret = krb5_auth_con_init (context, &ctx->auth_context);
    if (kret) {
	*minor_status = kret;
	HEIMDAL_MUTEX_destroy(&ctx->ctx_id_mutex);
	return GSS_S_FAILURE;
    }

    kret = krb5_auth_con_init (context, &ctx->deleg_auth_context);
    if (kret) {
	*minor_status = kret;
	krb5_auth_con_free(context, ctx->auth_context);
	HEIMDAL_MUTEX_destroy(&ctx->ctx_id_mutex);
	return GSS_S_FAILURE;
    }

    kret = set_addresses(context, ctx->auth_context, input_chan_bindings);
    if (kret) {
	*minor_status = kret;

	krb5_auth_con_free(context, ctx->auth_context);
	krb5_auth_con_free(context, ctx->deleg_auth_context);

	HEIMDAL_MUTEX_destroy(&ctx->ctx_id_mutex);

	return GSS_S_BAD_BINDINGS;
    }

    kret = set_addresses(context, ctx->deleg_auth_context, input_chan_bindings);
    if (kret) {
	*minor_status = kret;

	krb5_auth_con_free(context, ctx->auth_context);
	krb5_auth_con_free(context, ctx->deleg_auth_context);

	HEIMDAL_MUTEX_destroy(&ctx->ctx_id_mutex);

	return GSS_S_BAD_BINDINGS;
    }

    /*
     * We need a sequence number
     */

    krb5_auth_con_addflags(context,
			   ctx->auth_context,
			   KRB5_AUTH_CONTEXT_DO_SEQUENCE |
			   KRB5_AUTH_CONTEXT_CLEAR_FORWARDED_CRED,
			   NULL);

    /*
     * We need a sequence number
     */

    krb5_auth_con_addflags(context,
			   ctx->deleg_auth_context,
			   KRB5_AUTH_CONTEXT_DO_SEQUENCE |
			   KRB5_AUTH_CONTEXT_CLEAR_FORWARDED_CRED,
			   NULL);

    *context_handle = (gss_ctx_id_t)ctx;

    return GSS_S_COMPLETE;
}


static OM_uint32
gsskrb5_get_creds(
        OM_uint32 * minor_status,
	krb5_context context,
	krb5_ccache ccache,
	gsskrb5_ctx ctx,
	const gss_name_t target_name,
	int use_dns,
	OM_uint32 time_req,
	OM_uint32 * time_rec)
{
    OM_uint32 ret;
    krb5_error_code kret;
    krb5_creds this_cred;
    OM_uint32 lifetime_rec;

    if (ctx->target) {
	krb5_free_principal(context, ctx->target);
	ctx->target = NULL;
    }
    if (ctx->kcred) {
	krb5_free_creds(context, ctx->kcred);
	ctx->kcred = NULL;
    }

    ret = _gsskrb5_canon_name(minor_status, context, use_dns,
			      ctx->source, target_name, &ctx->target);
    if (ret)
	return ret;

    memset(&this_cred, 0, sizeof(this_cred));
    this_cred.client = ctx->source;
    this_cred.server = ctx->target;

    if (time_req && time_req != GSS_C_INDEFINITE) {
	krb5_timestamp ts;

	krb5_timeofday (context, &ts);
	this_cred.times.endtime = ts + time_req;
    } else {
	this_cred.times.endtime   = 0;
    }

    this_cred.session.keytype = KEYTYPE_NULL;

    kret = krb5_get_credentials(context,
				0,
				ccache,
				&this_cred,
				&ctx->kcred);
    if (kret) {
	*minor_status = kret;
	return GSS_S_FAILURE;
    }

    ctx->lifetime = ctx->kcred->times.endtime;

    ret = _gsskrb5_lifetime_left(minor_status, context,
				 ctx->lifetime, &lifetime_rec);
    if (ret) return ret;

    if (lifetime_rec == 0) {
	*minor_status = 0;
	return GSS_S_CONTEXT_EXPIRED;
    }

    if (time_rec) *time_rec = lifetime_rec;

    return GSS_S_COMPLETE;
}

static OM_uint32
gsskrb5_initiator_ready(
	OM_uint32 * minor_status,
	gsskrb5_ctx ctx,
	krb5_context context)
{
    OM_uint32 ret;
    int32_t seq_number;
    int is_cfx = 0;
    OM_uint32 flags = ctx->flags;

    krb5_free_creds(context, ctx->kcred);
    ctx->kcred = NULL;

    if (ctx->more_flags & CLOSE_CCACHE)
	krb5_cc_close(context, ctx->ccache);
    ctx->ccache = NULL;

    krb5_auth_con_getremoteseqnumber (context, ctx->auth_context, &seq_number);

    _gsskrb5i_is_cfx(context, ctx, 0);
    is_cfx = (ctx->more_flags & IS_CFX);

    ret = _gssapi_msg_order_create(minor_status,
				   &ctx->order,
				   _gssapi_msg_order_f(flags),
				   seq_number, 0, is_cfx);
    if (ret) return ret;

    ctx->state	= INITIATOR_READY;
    ctx->more_flags	|= OPEN;

    return GSS_S_COMPLETE;
}

/*
 * handle delegated creds in init-sec-context
 */

static void
do_delegation (krb5_context context,
	       krb5_auth_context ac,
	       krb5_ccache ccache,
	       krb5_creds *cred,
	       krb5_const_principal name,
	       krb5_data *fwd_data,
	       uint32_t flagmask,
	       uint32_t *flags)
{
    krb5_creds creds;
    KDCOptions fwd_flags;
    krb5_error_code kret;

    memset (&creds, 0, sizeof(creds));
    krb5_data_zero (fwd_data);

    kret = krb5_cc_get_principal(context, ccache, &creds.client);
    if (kret)
	goto out;

    kret = krb5_make_principal(context,
			       &creds.server,
			       creds.client->realm,
			       KRB5_TGS_NAME,
			       creds.client->realm,
			       NULL);
    if (kret)
	goto out;

    creds.times.endtime = 0;

    memset(&fwd_flags, 0, sizeof(fwd_flags));
    fwd_flags.forwarded = 1;
    fwd_flags.forwardable = 1;

    if ( /*target_name->name.name_type != KRB5_NT_SRV_HST ||*/
	name->name.name_string.len < 2)
	goto out;

    kret = krb5_get_forwarded_creds(context,
				    ac,
				    ccache,
				    KDCOptions2int(fwd_flags),
				    name->name.name_string.val[1],
				    &creds,
				    fwd_data);

 out:
    if (kret)
	*flags &= ~flagmask;
    else
	*flags |= flagmask;

    if (creds.client)
	krb5_free_principal(context, creds.client);
    if (creds.server)
	krb5_free_principal(context, creds.server);
}

/*
 * first stage of init-sec-context
 */

static OM_uint32
init_auth
(OM_uint32 * minor_status,
 gsskrb5_cred cred,
 gsskrb5_ctx ctx,
 krb5_context context,
 gss_name_t name,
 const gss_OID mech_type,
 OM_uint32 req_flags,
 OM_uint32 time_req,
 const gss_buffer_t input_token,
 gss_OID * actual_mech_type,
 gss_buffer_t output_token,
 OM_uint32 * ret_flags,
 OM_uint32 * time_rec
    )
{
    OM_uint32 ret = GSS_S_FAILURE;
    krb5_error_code kret;
    krb5_data outbuf;
    krb5_data fwd_data;
    OM_uint32 lifetime_rec;
    int allow_dns = 1;

    krb5_data_zero(&outbuf);
    krb5_data_zero(&fwd_data);

    *minor_status = 0;

    if (actual_mech_type)
	*actual_mech_type = GSS_KRB5_MECHANISM;

    if (cred == NULL) {
	kret = krb5_cc_default (context, &ctx->ccache);
	if (kret) {
	    *minor_status = kret;
	    ret = GSS_S_FAILURE;
	    goto failure;
	}
	ctx->more_flags |= CLOSE_CCACHE;
    } else
	ctx->ccache = cred->ccache;

    kret = krb5_cc_get_principal (context, ctx->ccache, &ctx->source);
    if (kret) {
	*minor_status = kret;
	ret = GSS_S_FAILURE;
	goto failure;
    }

    /*
     * This is hideous glue for (NFS) clients that wants to limit the
     * available enctypes to what it can support (encryption in
     * kernel). If there is no enctypes selected for this credential,
     * reset it to the default set of enctypes.
     */
    {
	krb5_enctype *enctypes = NULL;

	if (cred && cred->enctypes)
	    enctypes = cred->enctypes;
	krb5_set_default_in_tkt_etypes(context, enctypes);
    }

    /* canon name if needed for client + target realm */
    kret = krb5_cc_get_config(context, ctx->ccache, NULL,
			      "realm-config", &outbuf);
    if (kret == 0) {
	/* XXX 2 is no server canon */
	if (outbuf.length < 1 || ((((unsigned char *)outbuf.data)[0]) & 2))
	    allow_dns = 0;
	krb5_data_free(&outbuf);
    }

    /*
     * First we try w/o dns, hope that the KDC have register alias
     * (and referrals if cross realm) for this principal. If that
     * fails and if we are allowed to using this realm try again with
     * DNS canonicalizion.
     */
    ret = gsskrb5_get_creds(minor_status, context, ctx->ccache,
			    ctx, name, 0, time_req,
			    time_rec);
    if (ret && allow_dns)
	ret = gsskrb5_get_creds(minor_status, context, ctx->ccache,
				ctx, name, 1, time_req,
				time_rec);
    if (ret)
	goto failure;

    ctx->lifetime = ctx->kcred->times.endtime;

    ret = _gss_DES3_get_mic_compat(minor_status, ctx, context);
    if (ret)
	goto failure;

    ret = _gsskrb5_lifetime_left(minor_status,
				 context,
				 ctx->lifetime,
				 &lifetime_rec);
    if (ret)
	goto failure;

    if (lifetime_rec == 0) {
	*minor_status = 0;
	ret = GSS_S_CONTEXT_EXPIRED;
	goto failure;
    }

    krb5_auth_con_setkey(context,
			 ctx->auth_context,
			 &ctx->kcred->session);

    kret = krb5_auth_con_generatelocalsubkey(context,
					     ctx->auth_context,
					     &ctx->kcred->session);
    if(kret) {
	*minor_status = kret;
	ret = GSS_S_FAILURE;
	goto failure;
    }

    return GSS_S_COMPLETE;

failure:
    if (ctx->ccache && (ctx->more_flags & CLOSE_CCACHE))
	krb5_cc_close(context, ctx->ccache);
    ctx->ccache = NULL;

    return ret;

}

static OM_uint32
init_auth_restart
(OM_uint32 * minor_status,
 gsskrb5_cred cred,
 gsskrb5_ctx ctx,
 krb5_context context,
 OM_uint32 req_flags,
 const gss_channel_bindings_t input_chan_bindings,
 const gss_buffer_t input_token,
 gss_OID * actual_mech_type,
 gss_buffer_t output_token,
 OM_uint32 * ret_flags,
 OM_uint32 * time_rec
    )
{
    OM_uint32 ret = GSS_S_FAILURE;
    krb5_error_code kret;
    krb5_flags ap_options;
    krb5_data outbuf;
    uint32_t flags;
    krb5_data authenticator;
    Checksum cksum;
    krb5_enctype enctype;
    krb5_data fwd_data, timedata;
    int32_t offset = 0, oldoffset = 0;
    uint32_t flagmask;

    krb5_data_zero(&outbuf);
    krb5_data_zero(&fwd_data);

    *minor_status = 0;

    /*
     * If the credential doesn't have ok-as-delegate, check if there
     * is a realm setting and use that.
     */
    if (!ctx->kcred->flags.b.ok_as_delegate) {
	krb5_data data;

	ret = krb5_cc_get_config(context, ctx->ccache, NULL,
				 "realm-config", &data);
	if (ret == 0) {
	    /* XXX 1 is use ok-as-delegate */
	    if (data.length < 1 || ((((unsigned char *)data.data)[0]) & 1) == 0)
		req_flags &= ~(GSS_C_DELEG_FLAG|GSS_C_DELEG_POLICY_FLAG);
	    krb5_data_free(&data);
	}
    }

    flagmask = 0;

    /* if we used GSS_C_DELEG_POLICY_FLAG, trust KDC */
    if ((req_flags & GSS_C_DELEG_POLICY_FLAG)
	&& ctx->kcred->flags.b.ok_as_delegate)
	flagmask |= GSS_C_DELEG_FLAG | GSS_C_DELEG_POLICY_FLAG;
    /* if there still is a GSS_C_DELEG_FLAG, use that */
    if (req_flags & GSS_C_DELEG_FLAG)
	flagmask |= GSS_C_DELEG_FLAG;


    flags = 0;
    ap_options = 0;
    if (flagmask & GSS_C_DELEG_FLAG) {
	do_delegation (context,
		       ctx->deleg_auth_context,
		       ctx->ccache, ctx->kcred, ctx->target,
		       &fwd_data, flagmask, &flags);
    }

    if (req_flags & GSS_C_MUTUAL_FLAG) {
	flags |= GSS_C_MUTUAL_FLAG;
	ap_options |= AP_OPTS_MUTUAL_REQUIRED;
    }

    if (req_flags & GSS_C_REPLAY_FLAG)
	flags |= GSS_C_REPLAY_FLAG;
    if (req_flags & GSS_C_SEQUENCE_FLAG)
	flags |= GSS_C_SEQUENCE_FLAG;
#if 0
    if (req_flags & GSS_C_ANON_FLAG)
	;                               /* XXX */
#endif
    if (req_flags & GSS_C_DCE_STYLE) {
	/* GSS_C_DCE_STYLE implies GSS_C_MUTUAL_FLAG */
	flags |= GSS_C_DCE_STYLE | GSS_C_MUTUAL_FLAG;
	ap_options |= AP_OPTS_MUTUAL_REQUIRED;
    }
    if (req_flags & GSS_C_IDENTIFY_FLAG)
	flags |= GSS_C_IDENTIFY_FLAG;
    if (req_flags & GSS_C_EXTENDED_ERROR_FLAG)
	flags |= GSS_C_EXTENDED_ERROR_FLAG;

    if (req_flags & GSS_C_CONF_FLAG) {
	flags |= GSS_C_CONF_FLAG;
    }
    if (req_flags & GSS_C_INTEG_FLAG) {
	flags |= GSS_C_INTEG_FLAG;
    }
    if (cred == NULL || !(cred->cred_flags & GSS_CF_NO_CI_FLAGS)) {
	flags |= GSS_C_CONF_FLAG;
	flags |= GSS_C_INTEG_FLAG;
    }
    flags |= GSS_C_TRANS_FLAG;

    if (ret_flags)
	*ret_flags = flags;
    ctx->flags = flags;
    ctx->more_flags |= LOCAL;

    ret = _gsskrb5_create_8003_checksum (minor_status,
					 input_chan_bindings,
					 flags,
					 &fwd_data,
					 &cksum);
    krb5_data_free (&fwd_data);
    if (ret)
	goto failure;

    enctype = ctx->auth_context->keyblock->keytype;

    ret = krb5_cc_get_config(context, ctx->ccache, ctx->target,
			     "time-offset", &timedata);
    if (ret == 0) {
	if (timedata.length == 4) {
	    const u_char *p = timedata.data;
	    offset = (p[0] <<24) | (p[1] << 16) | (p[2] << 8) | (p[3] << 0);
	}
	krb5_data_free(&timedata);
    }

    if (offset) {
	krb5_get_kdc_sec_offset (context, &oldoffset, NULL);
	krb5_set_kdc_sec_offset (context, offset, -1);
    }

    kret = _krb5_build_authenticator(context,
				     ctx->auth_context,
				     enctype,
				     ctx->kcred,
				     &cksum,
				     &authenticator,
				     KRB5_KU_AP_REQ_AUTH);

    if (kret) {
	if (offset)
	    krb5_set_kdc_sec_offset (context, oldoffset, -1);
	*minor_status = kret;
	ret = GSS_S_FAILURE;
	goto failure;
    }

    kret = krb5_build_ap_req (context,
			      enctype,
			      ctx->kcred,
			      ap_options,
			      authenticator,
			      &outbuf);
    if (offset)
	krb5_set_kdc_sec_offset (context, oldoffset, -1);
    if (kret) {
	*minor_status = kret;
	ret = GSS_S_FAILURE;
	goto failure;
    }

    if (flags & GSS_C_DCE_STYLE) {
	output_token->value = outbuf.data;
	output_token->length = outbuf.length;
    } else {
        ret = _gsskrb5_encapsulate (minor_status, &outbuf, output_token,
				    (u_char *)(intptr_t)"\x01\x00",
				    GSS_KRB5_MECHANISM);
	krb5_data_free (&outbuf);
	if (ret)
	    goto failure;
    }

    free_Checksum(&cksum);

    if (flags & GSS_C_MUTUAL_FLAG) {
	ctx->state = INITIATOR_WAIT_FOR_MUTAL;
	return GSS_S_CONTINUE_NEEDED;
    }

    return gsskrb5_initiator_ready(minor_status, ctx, context);
failure:
    if (ctx->ccache && (ctx->more_flags & CLOSE_CCACHE))
	krb5_cc_close(context, ctx->ccache);
    ctx->ccache = NULL;

    return ret;
}

static krb5_error_code
handle_error_packet(krb5_context context,
		    gsskrb5_ctx ctx,
		    krb5_data indata)
{
    krb5_error_code kret;
    KRB_ERROR error;

    kret = krb5_rd_error(context, &indata, &error);
    if (kret == 0) {
	kret = krb5_error_from_rd_error(context, &error, NULL);

	/* save the time skrew for this host */
	if (kret == KRB5KRB_AP_ERR_SKEW) {
	    krb5_data timedata;
	    unsigned char p[4];
	    int32_t t = error.stime - time(NULL);

	    p[0] = (t >> 24) & 0xFF;
	    p[1] = (t >> 16) & 0xFF;
	    p[2] = (t >> 8)  & 0xFF;
	    p[3] = (t >> 0)  & 0xFF;

	    timedata.data = p;
	    timedata.length = sizeof(p);

	    krb5_cc_set_config(context, ctx->ccache, ctx->target,
			       "time-offset", &timedata);

	    if ((ctx->more_flags & RETRIED) == 0)
		 ctx->state = INITIATOR_RESTART;
	    ctx->more_flags |= RETRIED;
	}
	free_KRB_ERROR (&error);
    }
    return kret;
}


static OM_uint32
repl_mutual
(OM_uint32 * minor_status,
 gsskrb5_ctx ctx,
 krb5_context context,
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
    OM_uint32 ret;
    krb5_error_code kret;
    krb5_data indata;
    krb5_ap_rep_enc_part *repl;

    output_token->length = 0;
    output_token->value = NULL;

    if (actual_mech_type)
	*actual_mech_type = GSS_KRB5_MECHANISM;

    if (IS_DCE_STYLE(ctx)) {
	/* There is no OID wrapping. */
	indata.length	= input_token->length;
	indata.data	= input_token->value;
	kret = krb5_rd_rep(context,
			   ctx->auth_context,
			   &indata,
			   &repl);
	if (kret) {
	    ret = _gsskrb5_decapsulate(minor_status,
				       input_token,
				       &indata,
				       "\x03\x00",
				       GSS_KRB5_MECHANISM);
	    if (ret == GSS_S_COMPLETE) {
		*minor_status = handle_error_packet(context, ctx, indata);
	    } else {
		*minor_status = kret;
	    }
	    return GSS_S_FAILURE;
	}
    } else {
	ret = _gsskrb5_decapsulate (minor_status,
				    input_token,
				    &indata,
				    "\x02\x00",
				    GSS_KRB5_MECHANISM);
	if (ret == GSS_S_DEFECTIVE_TOKEN) {
	    /* check if there is an error token sent instead */
	    ret = _gsskrb5_decapsulate (minor_status,
					input_token,
					&indata,
					"\x03\x00",
					GSS_KRB5_MECHANISM);
	    if (ret == GSS_S_COMPLETE) {
		*minor_status = handle_error_packet(context, ctx, indata);
		return GSS_S_FAILURE;
	    }
	}
	kret = krb5_rd_rep (context,
			    ctx->auth_context,
			    &indata,
			    &repl);
	if (kret) {
	    *minor_status = kret;
	    return GSS_S_FAILURE;
	}
    }

    krb5_free_ap_rep_enc_part (context,
			       repl);

    *minor_status = 0;
    if (time_rec) {
	ret = _gsskrb5_lifetime_left(minor_status,
				     context,
				     ctx->lifetime,
				     time_rec);
    } else {
	ret = GSS_S_COMPLETE;
    }
    if (ret_flags)
	*ret_flags = ctx->flags;

    if (req_flags & GSS_C_DCE_STYLE) {
	int32_t local_seq, remote_seq;
	krb5_data outbuf;

	/*
	 * So DCE_STYLE is strange. The client echos the seq number
	 * that the server used in the server's mk_rep in its own
	 * mk_rep(). After when done, it resets to it's own seq number
	 * for the gss_wrap calls.
	 */

	krb5_auth_con_getremoteseqnumber(context, ctx->auth_context, &remote_seq);
	krb5_auth_con_getlocalseqnumber(context, ctx->auth_context, &local_seq);
	krb5_auth_con_setlocalseqnumber(context, ctx->auth_context, remote_seq);

	kret = krb5_mk_rep(context, ctx->auth_context, &outbuf);
	if (kret) {
	    *minor_status = kret;
	    return GSS_S_FAILURE;
	}

	/* reset local seq number */
	krb5_auth_con_setlocalseqnumber(context, ctx->auth_context, local_seq);

	output_token->length = outbuf.length;
	output_token->value  = outbuf.data;
    }

    return gsskrb5_initiator_ready(minor_status, ctx, context);
}

/*
 * gss_init_sec_context
 */

OM_uint32 GSSAPI_CALLCONV _gsskrb5_init_sec_context
(OM_uint32 * minor_status,
 const gss_cred_id_t cred_handle,
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
    krb5_context context;
    gsskrb5_cred cred = (gsskrb5_cred)cred_handle;
    gsskrb5_ctx ctx;
    OM_uint32 ret;

    GSSAPI_KRB5_INIT (&context);

    output_token->length = 0;
    output_token->value  = NULL;

    if (context_handle == NULL) {
	*minor_status = 0;
	return GSS_S_FAILURE | GSS_S_CALL_BAD_STRUCTURE;
    }

    if (ret_flags)
	*ret_flags = 0;
    if (time_rec)
	*time_rec = 0;

    if (target_name == GSS_C_NO_NAME) {
	if (actual_mech_type)
	    *actual_mech_type = GSS_C_NO_OID;
	*minor_status = 0;
	return GSS_S_BAD_NAME;
    }

    if (mech_type != GSS_C_NO_OID &&
	!gss_oid_equal(mech_type, GSS_KRB5_MECHANISM))
	return GSS_S_BAD_MECH;

    if (input_token == GSS_C_NO_BUFFER || input_token->length == 0) {
	OM_uint32 ret1;

	if (*context_handle != GSS_C_NO_CONTEXT) {
	    *minor_status = 0;
	    return GSS_S_FAILURE | GSS_S_CALL_BAD_STRUCTURE;
	}

	ret1 = _gsskrb5_create_ctx(minor_status,
				  context_handle,
				  context,
				  input_chan_bindings,
				  INITIATOR_START);
	if (ret1)
	    return ret1;
    }

    if (*context_handle == GSS_C_NO_CONTEXT) {
	*minor_status = 0;
	return GSS_S_FAILURE | GSS_S_CALL_BAD_STRUCTURE;
    }

    ctx = (gsskrb5_ctx) *context_handle;

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);

 again:
    switch (ctx->state) {
    case INITIATOR_START:
	ret = init_auth(minor_status,
			cred,
			ctx,
			context,
			target_name,
			mech_type,
			req_flags,
			time_req,
			input_token,
			actual_mech_type,
			output_token,
			ret_flags,
			time_rec);
	if (ret != GSS_S_COMPLETE)
	    break;
	/* FALL THOUGH */
    case INITIATOR_RESTART:
	ret = init_auth_restart(minor_status,
				cred,
				ctx,
				context,
				req_flags,
				input_chan_bindings,
				input_token,
				actual_mech_type,
				output_token,
				ret_flags,
				time_rec);
	break;
    case INITIATOR_WAIT_FOR_MUTAL:
	ret = repl_mutual(minor_status,
			  ctx,
			  context,
			  mech_type,
			  req_flags,
			  time_req,
			  input_chan_bindings,
			  input_token,
			  actual_mech_type,
			  output_token,
			  ret_flags,
			  time_rec);
	if (ctx->state == INITIATOR_RESTART)
	    goto again;
	break;
    case INITIATOR_READY:
	/*
	 * If we get there, the caller have called
	 * gss_init_sec_context() one time too many.
	 */
	_gsskrb5_set_status(EINVAL, "init_sec_context "
			    "called one time too many");
	*minor_status = EINVAL;
	ret = GSS_S_BAD_STATUS;
	break;
    default:
	_gsskrb5_set_status(EINVAL, "init_sec_context "
			    "invalid state %d for client",
			    (int)ctx->state);
	*minor_status = EINVAL;
	ret = GSS_S_BAD_STATUS;
	break;
    }
    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);

    /* destroy context in case of error */
    if (GSS_ERROR(ret)) {
	OM_uint32 min2;
	_gsskrb5_delete_sec_context(&min2, context_handle, GSS_C_NO_BUFFER);
    }

    return ret;

}
