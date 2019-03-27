/*
 * Copyright (c) 2009 Kungliga Tekniska Högskolan
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

#include "mech_locl.h"

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_store_cred(OM_uint32         *minor_status,
	       gss_cred_id_t     input_cred_handle,
	       gss_cred_usage_t  cred_usage,
	       const gss_OID     desired_mech,
	       OM_uint32         overwrite_cred,
	       OM_uint32         default_cred,
	       gss_OID_set       *elements_stored,
	       gss_cred_usage_t  *cred_usage_stored)
{
    struct _gss_cred *cred = (struct _gss_cred *) input_cred_handle;
    struct _gss_mechanism_cred *mc;
    OM_uint32 maj, junk;

    if (minor_status == NULL)
	return GSS_S_FAILURE;
    if (elements_stored)
	*elements_stored = NULL;
    if (cred_usage_stored)
	*cred_usage_stored = 0;

    if (cred == NULL)
	return GSS_S_NO_CONTEXT;

    if (elements_stored) {
	maj = gss_create_empty_oid_set(minor_status, elements_stored);
	if (maj != GSS_S_COMPLETE)
	    return maj;
    }

    HEIM_SLIST_FOREACH(mc, &cred->gc_mc, gmc_link) {
	gssapi_mech_interface m = mc->gmc_mech;

	if (m == NULL || m->gm_store_cred == NULL)
	    continue;

	if (desired_mech) {
	    maj = gss_oid_equal(&m->gm_mech_oid, desired_mech);
	    if (maj != 0)
		continue;
	}

	maj = (m->gm_store_cred)(minor_status, mc->gmc_cred,
				 cred_usage, desired_mech, overwrite_cred,
				 default_cred, NULL, cred_usage_stored);
	if (maj != GSS_S_COMPLETE) {
	    gss_release_oid_set(&junk, elements_stored);
	    return maj;
	}

	if (elements_stored) {
	    gss_add_oid_set_member(&junk,
				   &m->gm_mech_oid,
				   elements_stored);
	}

    }
    return GSS_S_COMPLETE;
}
