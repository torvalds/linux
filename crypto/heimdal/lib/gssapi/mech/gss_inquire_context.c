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
 *	$FreeBSD: src/lib/libgssapi/gss_inquire_context.c,v 1.1 2005/12/29 14:40:20 dfr Exp $
 */

#include "mech_locl.h"

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_inquire_context(OM_uint32 *minor_status,
    const gss_ctx_id_t context_handle,
    gss_name_t *src_name,
    gss_name_t *targ_name,
    OM_uint32 *lifetime_rec,
    gss_OID *mech_type,
    OM_uint32 *ctx_flags,
    int *locally_initiated,
    int *xopen)
{
	OM_uint32 major_status;
	struct _gss_context *ctx = (struct _gss_context *) context_handle;
	gssapi_mech_interface m = ctx->gc_mech;
	struct _gss_name *name;
	gss_name_t src_mn, targ_mn;

	if (locally_initiated)
	    *locally_initiated = 0;
	if (xopen)
	    *xopen = 0;
	if (lifetime_rec)
	    *lifetime_rec = 0;

	if (src_name)
	    *src_name = GSS_C_NO_NAME;
	if (targ_name)
	    *targ_name = GSS_C_NO_NAME;
	if (mech_type)
	    *mech_type = GSS_C_NO_OID;
	src_mn = targ_mn = GSS_C_NO_NAME;

	major_status = m->gm_inquire_context(minor_status,
	    ctx->gc_ctx,
	    src_name ? &src_mn : NULL,
	    targ_name ? &targ_mn : NULL,
	    lifetime_rec,
	    mech_type,
	    ctx_flags,
	    locally_initiated,
	    xopen);

	if (major_status != GSS_S_COMPLETE) {
		_gss_mg_error(m, major_status, *minor_status);
		return (major_status);
	}

	if (src_name) {
		name = _gss_make_name(m, src_mn);
		if (!name) {
			if (mech_type)
				*mech_type = GSS_C_NO_OID;
			m->gm_release_name(minor_status, &src_mn);
			*minor_status = 0;
			return (GSS_S_FAILURE);
		}
		*src_name = (gss_name_t) name;
	}

	if (targ_name) {
		name = _gss_make_name(m, targ_mn);
		if (!name) {
			if (mech_type)
				*mech_type = GSS_C_NO_OID;
			if (src_name)
				gss_release_name(minor_status, src_name);
			m->gm_release_name(minor_status, &targ_mn);
			*minor_status = 0;
			return (GSS_S_FAILURE);
		}
		*targ_name = (gss_name_t) name;
	}

	return (GSS_S_COMPLETE);
}
