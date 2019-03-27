/*
 * Copyright (c) 1997-2003 Kungliga Tekniska Högskolan
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

OM_uint32 GSSAPI_CALLCONV _gsskrb5_release_cred
           (OM_uint32 * minor_status,
            gss_cred_id_t * cred_handle
           )
{
    krb5_context context;
    gsskrb5_cred cred;
    OM_uint32 junk;

    *minor_status = 0;

    if (*cred_handle == NULL)
        return GSS_S_COMPLETE;

    cred = (gsskrb5_cred)*cred_handle;
    *cred_handle = GSS_C_NO_CREDENTIAL;

    GSSAPI_KRB5_INIT (&context);

    HEIMDAL_MUTEX_lock(&cred->cred_id_mutex);

    if (cred->principal != NULL)
        krb5_free_principal(context, cred->principal);
    if (cred->keytab != NULL)
	krb5_kt_close(context, cred->keytab);
    if (cred->ccache != NULL) {
	if (cred->cred_flags & GSS_CF_DESTROY_CRED_ON_RELEASE)
	    krb5_cc_destroy(context, cred->ccache);
	else
	    krb5_cc_close(context, cred->ccache);
    }
    gss_release_oid_set(&junk, &cred->mechanisms);
    if (cred->enctypes)
	free(cred->enctypes);
    HEIMDAL_MUTEX_unlock(&cred->cred_id_mutex);
    HEIMDAL_MUTEX_destroy(&cred->cred_id_mutex);
    memset(cred, 0, sizeof(*cred));
    free(cred);
    return GSS_S_COMPLETE;
}

