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
 *	$FreeBSD: src/lib/libgssapi/gss_release_cred.c,v 1.1 2005/12/29 14:40:20 dfr Exp $
 */

#include "mech_locl.h"

/**
 * Release a credentials
 *
 * Its ok to release the GSS_C_NO_CREDENTIAL/NULL credential, it will
 * return a GSS_S_COMPLETE error code. On return cred_handle is set ot
 * GSS_C_NO_CREDENTIAL.
 *
 * Example:
 *
 * @code
 * gss_cred_id_t cred = GSS_C_NO_CREDENTIAL;
 * major = gss_release_cred(&minor, &cred);
 * @endcode
 *
 * @param minor_status minor status return code, mech specific
 * @param cred_handle a pointer to the credential too release
 *
 * @return an gssapi error code
 *
 * @ingroup gssapi
 */

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_release_cred(OM_uint32 *minor_status, gss_cred_id_t *cred_handle)
{
	struct _gss_cred *cred = (struct _gss_cred *) *cred_handle;
	struct _gss_mechanism_cred *mc;

	if (*cred_handle == GSS_C_NO_CREDENTIAL)
	    return (GSS_S_COMPLETE);

	while (HEIM_SLIST_FIRST(&cred->gc_mc)) {
		mc = HEIM_SLIST_FIRST(&cred->gc_mc);
		HEIM_SLIST_REMOVE_HEAD(&cred->gc_mc, gmc_link);
		mc->gmc_mech->gm_release_cred(minor_status, &mc->gmc_cred);
		free(mc);
	}
	free(cred);

	*minor_status = 0;
	*cred_handle = GSS_C_NO_CREDENTIAL;
	return (GSS_S_COMPLETE);
}
