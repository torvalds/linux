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
_gsskrb5_authorize_localname(OM_uint32 *minor_status,
                             const gss_name_t input_name,
                             gss_const_buffer_t user_name,
                             gss_const_OID user_name_type)
{
    krb5_context context;
    krb5_principal princ = (krb5_principal)input_name;
    char *user;
    int user_ok;

    if (!gss_oid_equal(user_name_type, GSS_C_NT_USER_NAME))
        return GSS_S_BAD_NAMETYPE;

    GSSAPI_KRB5_INIT(&context);

    user = malloc(user_name->length + 1);
    if (user == NULL) {
        *minor_status = ENOMEM;
        return GSS_S_FAILURE;
    }

    memcpy(user, user_name->value, user_name->length);
    user[user_name->length] = '\0';

    *minor_status = 0;
    user_ok = krb5_kuserok(context, princ, user);

    free(user);

    return user_ok ? GSS_S_COMPLETE : GSS_S_UNAUTHORIZED;
}
