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
 *	$FreeBSD: src/lib/libgssapi/gss_accept_sec_context.c,v 1.1 2005/12/29 14:40:20 dfr Exp $
 */

#include "mech_locl.h"

static OM_uint32
parse_header(const gss_buffer_t input_token, gss_OID mech_oid)
{
	unsigned char *p = input_token->value;
	size_t len = input_token->length;
	size_t a, b;

	/*
	 * Token must start with [APPLICATION 0] SEQUENCE.
	 * But if it doesn't assume it is DCE-STYLE Kerberos!
	 */
	if (len == 0)
		return (GSS_S_DEFECTIVE_TOKEN);

	p++;
	len--;

	/*
	 * Decode the length and make sure it agrees with the
	 * token length.
	 */
	if (len == 0)
		return (GSS_S_DEFECTIVE_TOKEN);
	if ((*p & 0x80) == 0) {
		a = *p;
		p++;
		len--;
	} else {
		b = *p & 0x7f;
		p++;
		len--;
		if (len < b)
		    return (GSS_S_DEFECTIVE_TOKEN);
		a = 0;
		while (b) {
		    a = (a << 8) | *p;
		    p++;
		    len--;
		    b--;
		}
	}
	if (a != len)
		return (GSS_S_DEFECTIVE_TOKEN);

	/*
	 * Decode the OID for the mechanism. Simplify life by
	 * assuming that the OID length is less than 128 bytes.
	 */
	if (len < 2 || *p != 0x06)
		return (GSS_S_DEFECTIVE_TOKEN);
	if ((p[1] & 0x80) || p[1] > (len - 2))
		return (GSS_S_DEFECTIVE_TOKEN);
	mech_oid->length = p[1];
	p += 2;
	len -= 2;
	mech_oid->elements = p;

	return GSS_S_COMPLETE;
}

static gss_OID_desc krb5_mechanism =
    {9, rk_UNCONST("\x2a\x86\x48\x86\xf7\x12\x01\x02\x02")};
static gss_OID_desc ntlm_mechanism =
    {10, rk_UNCONST("\x2b\x06\x01\x04\x01\x82\x37\x02\x02\x0a")};
static gss_OID_desc spnego_mechanism =
    {6, rk_UNCONST("\x2b\x06\x01\x05\x05\x02")};

static OM_uint32
choose_mech(const gss_buffer_t input, gss_OID mech_oid)
{
	OM_uint32 status;

	/*
	 * First try to parse the gssapi token header and see if it's a
	 * correct header, use that in the first hand.
	 */

	status = parse_header(input, mech_oid);
	if (status == GSS_S_COMPLETE)
	    return GSS_S_COMPLETE;

	/*
	 * Lets guess what mech is really is, callback function to mech ??
	 */

	if (input->length > 8 &&
	    memcmp((const char *)input->value, "NTLMSSP\x00", 8) == 0)
	{
		*mech_oid = ntlm_mechanism;
		return GSS_S_COMPLETE;
	} else if (input->length != 0 &&
		   ((const char *)input->value)[0] == 0x6E)
	{
		/* Could be a raw AP-REQ (check for APPLICATION tag) */
		*mech_oid = krb5_mechanism;
		return GSS_S_COMPLETE;
	} else if (input->length == 0) {
		/*
		 * There is the a wierd mode of SPNEGO (in CIFS and
		 * SASL GSS-SPENGO where the first token is zero
		 * length and the acceptor returns a mech_list, lets
		 * hope that is what is happening now.
		 *
		 * http://msdn.microsoft.com/en-us/library/cc213114.aspx
		 * "NegTokenInit2 Variation for Server-Initiation"
		 */
		*mech_oid = spnego_mechanism;
		return GSS_S_COMPLETE;
	}
	return status;
}


GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_accept_sec_context(OM_uint32 *minor_status,
    gss_ctx_id_t *context_handle,
    const gss_cred_id_t acceptor_cred_handle,
    const gss_buffer_t input_token,
    const gss_channel_bindings_t input_chan_bindings,
    gss_name_t *src_name,
    gss_OID *mech_type,
    gss_buffer_t output_token,
    OM_uint32 *ret_flags,
    OM_uint32 *time_rec,
    gss_cred_id_t *delegated_cred_handle)
{
	OM_uint32 major_status, mech_ret_flags, junk;
	gssapi_mech_interface m;
	struct _gss_context *ctx = (struct _gss_context *) *context_handle;
	struct _gss_cred *cred = (struct _gss_cred *) acceptor_cred_handle;
	struct _gss_mechanism_cred *mc;
	gss_cred_id_t acceptor_mc, delegated_mc;
	gss_name_t src_mn;
	gss_OID mech_ret_type = NULL;

	*minor_status = 0;
	if (src_name)
	    *src_name = GSS_C_NO_NAME;
	if (mech_type)
	    *mech_type = GSS_C_NO_OID;
	if (ret_flags)
	    *ret_flags = 0;
	if (time_rec)
	    *time_rec = 0;
	if (delegated_cred_handle)
	    *delegated_cred_handle = GSS_C_NO_CREDENTIAL;
	_mg_buffer_zero(output_token);


	/*
	 * If this is the first call (*context_handle is NULL), we must
	 * parse the input token to figure out the mechanism to use.
	 */
	if (*context_handle == GSS_C_NO_CONTEXT) {
		gss_OID_desc mech_oid;

		major_status = choose_mech(input_token, &mech_oid);
		if (major_status != GSS_S_COMPLETE)
			return major_status;

		/*
		 * Now that we have a mechanism, we can find the
		 * implementation.
		 */
		ctx = malloc(sizeof(struct _gss_context));
		if (!ctx) {
			*minor_status = ENOMEM;
			return (GSS_S_DEFECTIVE_TOKEN);
		}
		memset(ctx, 0, sizeof(struct _gss_context));
		m = ctx->gc_mech = __gss_get_mechanism(&mech_oid);
		if (!m) {
			free(ctx);
			return (GSS_S_BAD_MECH);
		}
		*context_handle = (gss_ctx_id_t) ctx;
	} else {
		m = ctx->gc_mech;
	}

	if (cred) {
		HEIM_SLIST_FOREACH(mc, &cred->gc_mc, gmc_link)
			if (mc->gmc_mech == m)
				break;
		if (!mc) {
		        gss_delete_sec_context(&junk, context_handle, NULL);
			return (GSS_S_BAD_MECH);
		}
		acceptor_mc = mc->gmc_cred;
	} else {
		acceptor_mc = GSS_C_NO_CREDENTIAL;
	}
	delegated_mc = GSS_C_NO_CREDENTIAL;

	mech_ret_flags = 0;
	major_status = m->gm_accept_sec_context(minor_status,
	    &ctx->gc_ctx,
	    acceptor_mc,
	    input_token,
	    input_chan_bindings,
	    &src_mn,
	    &mech_ret_type,
	    output_token,
	    &mech_ret_flags,
	    time_rec,
	    &delegated_mc);
	if (major_status != GSS_S_COMPLETE &&
	    major_status != GSS_S_CONTINUE_NEEDED)
	{
		_gss_mg_error(m, major_status, *minor_status);
		gss_delete_sec_context(&junk, context_handle, NULL);
		return (major_status);
	}

	if (mech_type)
	    *mech_type = mech_ret_type;

	if (src_name && src_mn) {
		/*
		 * Make a new name and mark it as an MN.
		 */
		struct _gss_name *name = _gss_make_name(m, src_mn);

		if (!name) {
			m->gm_release_name(minor_status, &src_mn);
		        gss_delete_sec_context(&junk, context_handle, NULL);
			return (GSS_S_FAILURE);
		}
		*src_name = (gss_name_t) name;
	} else if (src_mn) {
		m->gm_release_name(minor_status, &src_mn);
	}

	if (mech_ret_flags & GSS_C_DELEG_FLAG) {
		if (!delegated_cred_handle) {
			m->gm_release_cred(minor_status, &delegated_mc);
			mech_ret_flags &=
			    ~(GSS_C_DELEG_FLAG|GSS_C_DELEG_POLICY_FLAG);
		} else if (gss_oid_equal(mech_ret_type, &m->gm_mech_oid) == 0) {
			/*
			 * If the returned mech_type is not the same
			 * as the mech, assume its pseudo mech type
			 * and the returned type is already a
			 * mech-glue object
			 */
			*delegated_cred_handle = delegated_mc;

		} else if (delegated_mc) {
			struct _gss_cred *dcred;
			struct _gss_mechanism_cred *dmc;

			dcred = malloc(sizeof(struct _gss_cred));
			if (!dcred) {
				*minor_status = ENOMEM;
				gss_delete_sec_context(&junk, context_handle, NULL);
				return (GSS_S_FAILURE);
			}
			HEIM_SLIST_INIT(&dcred->gc_mc);
			dmc = malloc(sizeof(struct _gss_mechanism_cred));
			if (!dmc) {
				free(dcred);
				*minor_status = ENOMEM;
				gss_delete_sec_context(&junk, context_handle, NULL);
				return (GSS_S_FAILURE);
			}
			dmc->gmc_mech = m;
			dmc->gmc_mech_oid = &m->gm_mech_oid;
			dmc->gmc_cred = delegated_mc;
			HEIM_SLIST_INSERT_HEAD(&dcred->gc_mc, dmc, gmc_link);

			*delegated_cred_handle = (gss_cred_id_t) dcred;
		}
	}

	if (ret_flags)
	    *ret_flags = mech_ret_flags;
	return (major_status);
}
