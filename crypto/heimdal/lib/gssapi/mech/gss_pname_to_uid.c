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

static OM_uint32
mech_pname_to_uid(OM_uint32 *minor_status,
                  struct _gss_mechanism_name *mn,
                  uid_t *uidp)
{
    OM_uint32 major_status = GSS_S_UNAVAILABLE;

    *minor_status = 0;

    if (mn->gmn_mech->gm_pname_to_uid == NULL)
        return GSS_S_UNAVAILABLE;

    major_status = mn->gmn_mech->gm_pname_to_uid(minor_status,
                                                 mn->gmn_name,
                                                 mn->gmn_mech_oid,
                                                 uidp);
    if (GSS_ERROR(major_status))
        _gss_mg_error(mn->gmn_mech, major_status, *minor_status);

    return major_status;
}

static OM_uint32
attr_pname_to_uid(OM_uint32 *minor_status,
                  struct _gss_mechanism_name *mn,
                  uid_t *uidp)
{
#ifdef NO_LOCALNAME
    return GSS_S_UNAVAILABLE;
#else
    OM_uint32 major_status = GSS_S_UNAVAILABLE;
    OM_uint32 tmpMinor;
    int more = -1;

    *minor_status = 0;

    if (mn->gmn_mech->gm_get_name_attribute == NULL)
        return GSS_S_UNAVAILABLE;

    while (more != 0) {
        gss_buffer_desc value;
        gss_buffer_desc display_value;
        int authenticated = 0, complete = 0;
#ifdef POSIX_GETPWNAM_R
        char pwbuf[2048];
        struct passwd pw, *pwd;
#else
        struct passwd *pwd;
#endif
        char *localname;

        major_status = mn->gmn_mech->gm_get_name_attribute(minor_status,
                                                           mn->gmn_name,
                                                           GSS_C_ATTR_LOCAL_LOGIN_USER,
                                                           &authenticated,
                                                           &complete,
                                                           &value,
                                                           &display_value,
                                                           &more);
        if (GSS_ERROR(major_status)) {
            _gss_mg_error(mn->gmn_mech, major_status, *minor_status);
            break;
        }

        localname = malloc(value.length + 1);
        if (localname == NULL) {
            major_status = GSS_S_FAILURE;
            *minor_status = ENOMEM;
            break;
        }

        memcpy(localname, value.value, value.length);
        localname[value.length] = '\0';

#ifdef POSIX_GETPWNAM_R
        if (getpwnam_r(localname, &pw, pwbuf, sizeof(pwbuf), &pwd) != 0)
            pwd = NULL;
#else
        pwd = getpwnam(localname);
#endif

        free(localname);
        gss_release_buffer(&tmpMinor, &value);
        gss_release_buffer(&tmpMinor, &display_value);

        if (pwd != NULL) {
            *uidp = pwd->pw_uid;
            major_status = GSS_S_COMPLETE;
            *minor_status = 0;
            break;
        } else
            major_status = GSS_S_UNAVAILABLE;
    }

    return major_status;
#endif /* NO_LOCALNAME */
}

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_pname_to_uid(OM_uint32 *minor_status,
                 const gss_name_t pname,
                 const gss_OID mech_type,
                 uid_t *uidp)
{
    OM_uint32 major_status = GSS_S_UNAVAILABLE;
    struct _gss_name *name = (struct _gss_name *) pname;
    struct _gss_mechanism_name *mn = NULL;

    *minor_status = 0;

    if (mech_type != GSS_C_NO_OID) {
        major_status = _gss_find_mn(minor_status, name, mech_type, &mn);
        if (GSS_ERROR(major_status))
            return major_status;

        major_status = mech_pname_to_uid(minor_status, mn, uidp);
        if (major_status != GSS_S_COMPLETE)
            major_status = attr_pname_to_uid(minor_status, mn, uidp);
    } else {
        HEIM_SLIST_FOREACH(mn, &name->gn_mn, gmn_link) {
            major_status = mech_pname_to_uid(minor_status, mn, uidp);
            if (major_status != GSS_S_COMPLETE)
                major_status = attr_pname_to_uid(minor_status, mn, uidp);
            if (major_status != GSS_S_UNAVAILABLE)
                break;
        }
    }

    if (major_status != GSS_S_COMPLETE && mn != NULL)
        _gss_mg_error(mn->gmn_mech, major_status, *minor_status);

    return major_status;
}
