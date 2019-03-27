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

#include "mech_locl.h"

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_set_cred_option (OM_uint32 *minor_status,
		     gss_cred_id_t *cred_handle,
		     const gss_OID object,
		     const gss_buffer_t value)
{
	struct _gss_cred *cred = (struct _gss_cred *) *cred_handle;
	OM_uint32	major_status = GSS_S_COMPLETE;
	struct _gss_mechanism_cred *mc;
	int one_ok = 0;

	*minor_status = 0;

	_gss_load_mech();

	if (cred == NULL) {
		struct _gss_mech_switch *m;

		cred = malloc(sizeof(*cred));
		if (cred == NULL)
		    return GSS_S_FAILURE;

		HEIM_SLIST_INIT(&cred->gc_mc);

		HEIM_SLIST_FOREACH(m, &_gss_mechs, gm_link) {

			if (m->gm_mech.gm_set_cred_option == NULL)
				continue;

			mc = malloc(sizeof(*mc));
			if (mc == NULL) {
			    *cred_handle = (gss_cred_id_t)cred;
			    gss_release_cred(minor_status, cred_handle);
			    *minor_status = ENOMEM;
			    return GSS_S_FAILURE;
			}

			mc->gmc_mech = &m->gm_mech;
			mc->gmc_mech_oid = &m->gm_mech_oid;
			mc->gmc_cred = GSS_C_NO_CREDENTIAL;

			major_status = m->gm_mech.gm_set_cred_option(
			    minor_status, &mc->gmc_cred, object, value);

			if (major_status) {
				free(mc);
				continue;
			}
			one_ok = 1;
			HEIM_SLIST_INSERT_HEAD(&cred->gc_mc, mc, gmc_link);
		}
		*cred_handle = (gss_cred_id_t)cred;
		if (!one_ok) {
			OM_uint32 junk;
			gss_release_cred(&junk, cred_handle);
		}
	} else {
		gssapi_mech_interface	m;

		HEIM_SLIST_FOREACH(mc, &cred->gc_mc, gmc_link) {
			m = mc->gmc_mech;

			if (m == NULL)
				return GSS_S_BAD_MECH;

			if (m->gm_set_cred_option == NULL)
				continue;

			major_status = m->gm_set_cred_option(minor_status,
			    &mc->gmc_cred, object, value);
			if (major_status == GSS_S_COMPLETE)
				one_ok = 1;
			else
				_gss_mg_error(m, major_status, *minor_status);

		}
	}
	if (one_ok) {
		*minor_status = 0;
		return GSS_S_COMPLETE;
	}
	return major_status;
}

