/*
 * Copyright (c) 2003 Kungliga Tekniska Högskolan
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

static gss_OID name_list[] = {
    GSS_C_NT_HOSTBASED_SERVICE,
    GSS_C_NT_USER_NAME,
    GSS_KRB5_NT_PRINCIPAL_NAME,
    GSS_C_NT_EXPORT_NAME,
    NULL
};

OM_uint32 GSSAPI_CALLCONV _gsskrb5_inquire_names_for_mech (
            OM_uint32 * minor_status,
            const gss_OID mechanism,
            gss_OID_set * name_types
           )
{
    OM_uint32 ret;
    int i;

    *minor_status = 0;

    if (gss_oid_equal(mechanism, GSS_KRB5_MECHANISM) == 0 &&
	gss_oid_equal(mechanism, GSS_C_NULL_OID) == 0) {
	*name_types = GSS_C_NO_OID_SET;
	return GSS_S_BAD_MECH;
    }

    ret = gss_create_empty_oid_set(minor_status, name_types);
    if (ret != GSS_S_COMPLETE)
	return ret;

    for (i = 0; name_list[i] != NULL; i++) {
	ret = gss_add_oid_set_member(minor_status,
				     name_list[i],
				     name_types);
	if (ret != GSS_S_COMPLETE)
	    break;
    }

    if (ret != GSS_S_COMPLETE)
	gss_release_oid_set(NULL, name_types);

    return GSS_S_COMPLETE;
}
