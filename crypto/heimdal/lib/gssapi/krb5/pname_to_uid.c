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

#include "gsskrb5_locl.h"

OM_uint32 GSSAPI_CALLCONV
_gsskrb5_pname_to_uid(OM_uint32 *minor_status,
                      const gss_name_t pname,
                      const gss_OID mech_type,
                      uid_t *uidp)
{
#ifdef NO_LOCALNAME
    *minor_status = KRB5_NO_LOCALNAME;
    return GSS_S_FAILURE;
#else
    krb5_error_code ret;
    krb5_context context;
    krb5_const_principal princ = (krb5_const_principal)pname;
    char localname[256];
#ifdef POSIX_GETPWNAM_R
    char pwbuf[2048];
    struct passwd pw, *pwd;
#else
    struct passwd *pwd;
#endif

    GSSAPI_KRB5_INIT(&context);

    *minor_status = 0;

    ret = krb5_aname_to_localname(context, princ,
                                  sizeof(localname), localname);
    if (ret != 0) {
        *minor_status = ret;
        return GSS_S_FAILURE;
    }

#ifdef POSIX_GETPWNAM_R
    if (getpwnam_r(localname, &pw, pwbuf, sizeof(pwbuf), &pwd) != 0) {
        *minor_status = KRB5_NO_LOCALNAME;
        return GSS_S_FAILURE;
    }
#else
    pwd = getpwnam(localname);
#endif

    if (pwd == NULL) {
        *minor_status = KRB5_NO_LOCALNAME;
        return GSS_S_FAILURE;
    }

    *uidp = pwd->pw_uid;

    return GSS_S_COMPLETE;
#endif /* NO_LOCALNAME */
}
