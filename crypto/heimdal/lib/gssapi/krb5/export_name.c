/*
 * Copyright (c) 1997, 1999, 2003 Kungliga Tekniska Högskolan
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

OM_uint32 GSSAPI_CALLCONV _gsskrb5_export_name
           (OM_uint32  * minor_status,
            const gss_name_t input_name,
            gss_buffer_t exported_name
           )
{
    krb5_context context;
    krb5_const_principal princ = (krb5_const_principal)input_name;
    krb5_error_code kret;
    char *buf, *name;
    size_t len;

    GSSAPI_KRB5_INIT (&context);

    kret = krb5_unparse_name (context, princ, &name);
    if (kret) {
	*minor_status = kret;
	return GSS_S_FAILURE;
    }
    len = strlen (name);

    exported_name->length = 10 + len + GSS_KRB5_MECHANISM->length;
    exported_name->value  = malloc(exported_name->length);
    if (exported_name->value == NULL) {
	free (name);
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    /* TOK, MECH_OID_LEN, DER(MECH_OID), NAME_LEN, NAME */

    buf = exported_name->value;
    memcpy(buf, "\x04\x01", 2);
    buf += 2;
    buf[0] = ((GSS_KRB5_MECHANISM->length + 2) >> 8) & 0xff;
    buf[1] = (GSS_KRB5_MECHANISM->length + 2) & 0xff;
    buf+= 2;
    buf[0] = 0x06;
    buf[1] = (GSS_KRB5_MECHANISM->length) & 0xFF;
    buf+= 2;

    memcpy(buf, GSS_KRB5_MECHANISM->elements, GSS_KRB5_MECHANISM->length);
    buf += GSS_KRB5_MECHANISM->length;

    buf[0] = (len >> 24) & 0xff;
    buf[1] = (len >> 16) & 0xff;
    buf[2] = (len >> 8) & 0xff;
    buf[3] = (len) & 0xff;
    buf += 4;

    memcpy (buf, name, len);

    free (name);

    *minor_status = 0;
    return GSS_S_COMPLETE;
}
