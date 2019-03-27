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
 *	$FreeBSD: src/lib/libgssapi/gss_add_cred.c,v 1.1 2005/12/29 14:40:20 dfr Exp $
 */

#include "mech_locl.h"

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_add_cred_with_password(OM_uint32 *minor_status,
    const gss_cred_id_t input_cred_handle,
    const gss_name_t desired_name,
    const gss_OID desired_mech,
    const gss_buffer_t password,
    gss_cred_usage_t cred_usage,
    OM_uint32 initiator_time_req,
    OM_uint32 acceptor_time_req,
    gss_cred_id_t *output_cred_handle,
    gss_OID_set *actual_mechs,
    OM_uint32 *initiator_time_rec,
    OM_uint32 *acceptor_time_rec)
{
	OM_uint32 major_status;
	gssapi_mech_interface m;
	struct _gss_cred *cred = (struct _gss_cred *) input_cred_handle;
	struct _gss_cred *new_cred;
	struct _gss_mechanism_cred *mc;
	struct _gss_mechanism_name *mn = NULL;
	OM_uint32 junk, time_req;

	*minor_status = 0;
	*output_cred_handle = GSS_C_NO_CREDENTIAL;
	if (initiator_time_rec)
	    *initiator_time_rec = 0;
	if (acceptor_time_rec)
	    *acceptor_time_rec = 0;
	if (actual_mechs)
	    *actual_mechs = GSS_C_NO_OID_SET;

	m = __gss_get_mechanism(desired_mech);
	if (m == NULL) {
		*minor_status = 0;
		return (GSS_S_BAD_MECH);
	}

	new_cred = calloc(1, sizeof(struct _gss_cred));
	if (new_cred == NULL) {
		*minor_status = ENOMEM;
		return (GSS_S_FAILURE);
	}
	HEIM_SLIST_INIT(&new_cred->gc_mc);

	/*
	 * Copy credentials from un-desired mechanisms to the new credential.
	 */
	if (cred) {
		HEIM_SLIST_FOREACH(mc, &cred->gc_mc, gmc_link) {
			struct _gss_mechanism_cred *copy_mc;

			if (gss_oid_equal(mc->gmc_mech_oid, desired_mech)) {
				continue;
			}
			copy_mc = _gss_copy_cred(mc);
			if (copy_mc == NULL) {
				gss_release_cred(&junk, (gss_cred_id_t *)&new_cred);
				*minor_status = ENOMEM;
				return (GSS_S_FAILURE);
			}
			HEIM_SLIST_INSERT_HEAD(&new_cred->gc_mc, copy_mc, gmc_link);
		}
	}

	/*
	 * Figure out a suitable mn, if any.
	 */
	if (desired_name != GSS_C_NO_NAME) {
		major_status = _gss_find_mn(minor_status,
					    (struct _gss_name *) desired_name,
					    desired_mech,
					    &mn);
		if (major_status != GSS_S_COMPLETE) {
			gss_release_cred(&junk, (gss_cred_id_t *)&new_cred);
			return (major_status);
		}
	}

	if (cred_usage == GSS_C_BOTH)
		time_req = initiator_time_req > acceptor_time_req ? acceptor_time_req : initiator_time_req;
	else if (cred_usage == GSS_C_INITIATE)
		time_req = initiator_time_req;
	else
		time_req = acceptor_time_req;

	major_status = _gss_acquire_mech_cred(minor_status, m, mn,
					      GSS_C_CRED_PASSWORD, password,
					      time_req, desired_mech,
					      cred_usage, &mc);
	if (major_status != GSS_S_COMPLETE) {
		gss_release_cred(&junk, (gss_cred_id_t *)&new_cred);
		return (major_status);
	}

	HEIM_SLIST_INSERT_HEAD(&new_cred->gc_mc, mc, gmc_link);

	if (actual_mechs || initiator_time_rec || acceptor_time_rec) {
		OM_uint32 time_rec;

		major_status = gss_inquire_cred(minor_status,
						(gss_cred_id_t)new_cred,
						NULL,
						&time_rec,
						NULL,
						actual_mechs);
		if (GSS_ERROR(major_status)) {
			gss_release_cred(&junk, (gss_cred_id_t *)&new_cred);
			return (major_status);
		}
		if (initiator_time_rec &&
		    (cred_usage == GSS_C_INITIATE || cred_usage == GSS_C_BOTH))
			*initiator_time_rec = time_rec;
		if (acceptor_time_rec &&
		    (cred_usage == GSS_C_ACCEPT || cred_usage == GSS_C_BOTH))
			*acceptor_time_rec = time_rec;
	}

	*output_cred_handle = (gss_cred_id_t) new_cred;
	return (GSS_S_COMPLETE);
}
