/*
 * Copyright (c) 2011, PADL Software Pty Ltd.
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

#include "mech_locl.h"

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_acquire_cred_with_password(OM_uint32 *minor_status,
			       const gss_name_t desired_name,
			       const gss_buffer_t password,
			       OM_uint32 time_req,
			       const gss_OID_set desired_mechs,
			       gss_cred_usage_t cred_usage,
			       gss_cred_id_t *output_cred_handle,
			       gss_OID_set *actual_mechs,
			       OM_uint32 *time_rec)
{
    OM_uint32 major_status, tmp_minor;

    if (desired_mechs == GSS_C_NO_OID_SET) {
	major_status = _gss_acquire_cred_ext(minor_status,
					     desired_name,
					     GSS_C_CRED_PASSWORD,
					     password,
					     time_req,
					     GSS_C_NO_OID,
					     cred_usage,
					     output_cred_handle);
	if (GSS_ERROR(major_status))
	    return major_status;
    } else {
	size_t i;
	struct _gss_cred *new_cred;

	new_cred = calloc(1, sizeof(*new_cred));
	if (new_cred == NULL) {
	    *minor_status = ENOMEM;
	    return GSS_S_FAILURE;
	}
	HEIM_SLIST_INIT(&new_cred->gc_mc);

	for (i = 0; i < desired_mechs->count; i++) {
	    struct _gss_cred *tmp_cred = NULL;
	    struct _gss_mechanism_cred *mc;

	    major_status = _gss_acquire_cred_ext(minor_status,
						 desired_name,
						 GSS_C_CRED_PASSWORD,
						 password,
						 time_req,
						 &desired_mechs->elements[i],
						 cred_usage,
						 (gss_cred_id_t *)&tmp_cred);
	    if (GSS_ERROR(major_status))
		continue;

	    mc = HEIM_SLIST_FIRST(&tmp_cred->gc_mc);
	    if (mc) {
		HEIM_SLIST_REMOVE_HEAD(&tmp_cred->gc_mc, gmc_link);
		HEIM_SLIST_INSERT_HEAD(&new_cred->gc_mc, mc, gmc_link);
	    }

	    gss_release_cred(&tmp_minor, (gss_cred_id_t *)&tmp_cred);
	}

	if (!HEIM_SLIST_FIRST(&new_cred->gc_mc)) {
	    free(new_cred);
	    *minor_status = 0;
	    return GSS_S_NO_CRED;
	}

	*output_cred_handle = (gss_cred_id_t)new_cred;
    }

    if (actual_mechs != NULL || time_rec != NULL) {
	major_status = gss_inquire_cred(minor_status,
					*output_cred_handle,
					NULL,
					time_rec,
					NULL,
					actual_mechs);
	if (GSS_ERROR(major_status)) {
	    gss_release_cred(&tmp_minor, output_cred_handle);
	    return major_status;
	}
    }

    *minor_status = 0;
    return GSS_S_COMPLETE;
}
