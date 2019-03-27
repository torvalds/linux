/*
 * Copyright (c) 1997 - 2003 Kungliga Tekniska Högskolan
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

#include "ntlm.h"

OM_uint32 GSSAPI_CALLCONV
_gss_ntlm_import_name
           (OM_uint32 * minor_status,
            const gss_buffer_t input_name_buffer,
            const gss_OID input_name_type,
            gss_name_t * output_name
           )
{
    char *name, *p, *p2;
    int is_hostnamed;
    int is_username;
    ntlm_name n;

    *minor_status = 0;

    if (output_name == NULL)
	return GSS_S_CALL_INACCESSIBLE_WRITE;

    *output_name = GSS_C_NO_NAME;

    is_hostnamed = gss_oid_equal(input_name_type, GSS_C_NT_HOSTBASED_SERVICE);
    is_username = gss_oid_equal(input_name_type, GSS_C_NT_USER_NAME);

    if (!is_hostnamed && !is_username)
	return GSS_S_BAD_NAMETYPE;

    name = malloc(input_name_buffer->length + 1);
    if (name == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }
    memcpy(name, input_name_buffer->value, input_name_buffer->length);
    name[input_name_buffer->length] = '\0';

    /* find "domain" part of the name and uppercase it */
    p = strchr(name, '@');
    if (p == NULL) {
        free(name);
	return GSS_S_BAD_NAME;
    }
    p[0] = '\0';
    p++;
    p2 = strchr(p, '.');
    if (p2 && p2[1] != '\0') {
	if (is_hostnamed) {
	    p = p2 + 1;
	    p2 = strchr(p, '.');
	}
	if (p2)
	    *p2 = '\0';
    }
    strupr(p);

    n = calloc(1, sizeof(*n));
    if (n == NULL) {
	free(name);
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    n->user = strdup(name);
    n->domain = strdup(p);

    free(name);

    if (n->user == NULL || n->domain == NULL) {
	free(n->user);
	free(n->domain);
	free(n);
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    *output_name = (gss_name_t)n;

    return GSS_S_COMPLETE;
}
