/*
 * Copyright (c) 1998 - 2001 Kungliga Tekniska Högskolan
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

#include "ftpd_locl.h"
#include <gssapi/gssapi.h>

/* XXX sync with gssapi.c */
struct gssapi_data {
    gss_ctx_id_t context_hdl;
    gss_name_t client_name;
    gss_cred_id_t delegated_cred_handle;
    void *mech_data;
};

int gssapi_userok(void*, char*); /* to keep gcc happy */
int gssapi_session(void*, char*); /* to keep gcc happy */

int
gssapi_userok(void *app_data, char *username)
{
    struct gssapi_data *data = app_data;

    /* Yes, this logic really is inverted. */
    return !gss_userok(data->client_name, username);
}

int
gssapi_session(void *app_data, char *username)
{
    struct gssapi_data *data = app_data;
    OM_uint32 major, minor;
    int ret = 0;

    if (data->delegated_cred_handle != GSS_C_NO_CREDENTIAL) {
        major = gss_store_cred(&minor, data->delegated_cred_handle,
                               GSS_C_INITIATE, GSS_C_NO_OID,
                               1, 1, NULL, NULL);
        if (GSS_ERROR(major))
            ret = 1;
	afslog(NULL, 1);
    }

    gss_release_cred(&minor, &data->delegated_cred_handle);
    return ret;
}
