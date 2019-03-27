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
 *	$FreeBSD: src/lib/libgssapi/cred.h,v 1.1 2005/12/29 14:40:20 dfr Exp $
 *	$Id$
 */

struct _gss_mechanism_cred {
	HEIM_SLIST_ENTRY(_gss_mechanism_cred) gmc_link;
	gssapi_mech_interface	gmc_mech;	/* mechanism ops for MC */
	gss_OID			gmc_mech_oid;	/* mechanism oid for MC */
	gss_cred_id_t		gmc_cred;	/* underlying MC */
};
HEIM_SLIST_HEAD(_gss_mechanism_cred_list, _gss_mechanism_cred);

struct _gss_cred {
	struct _gss_mechanism_cred_list gc_mc;
};

struct _gss_mechanism_cred *
_gss_copy_cred(struct _gss_mechanism_cred *mc);

struct _gss_mechanism_name;

OM_uint32
_gss_acquire_mech_cred(OM_uint32 *minor_status,
		       gssapi_mech_interface m,
		       const struct _gss_mechanism_name *mn,
		       gss_const_OID credential_type,
		       const void *credential_data,
		       OM_uint32 time_req,
		       gss_const_OID desired_mech,
		       gss_cred_usage_t cred_usage,
		       struct _gss_mechanism_cred **output_cred_handle);

