/*
 * Copyright (c) 2009 Kungliga Tekniska Högskolan
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

OM_uint32 GSSAPI_CALLCONV
_gsskrb5_export_cred(OM_uint32 *minor_status,
		     gss_cred_id_t cred_handle,
		     gss_buffer_t cred_token)
{
    gsskrb5_cred handle = (gsskrb5_cred)cred_handle;
    krb5_context context;
    krb5_error_code ret;
    krb5_storage *sp;
    krb5_data data, mech;
    const char *type;
    char *str;

    GSSAPI_KRB5_INIT (&context);

    if (handle->usage != GSS_C_INITIATE && handle->usage != GSS_C_BOTH) {
	*minor_status = GSS_KRB5_S_G_BAD_USAGE;
	return GSS_S_FAILURE;
    }

    sp = krb5_storage_emem();
    if (sp == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    type = krb5_cc_get_type(context, handle->ccache);
    if (strcmp(type, "MEMORY") == 0) {
	krb5_creds *creds;
	ret = krb5_store_uint32(sp, 0);
	if (ret) {
	    krb5_storage_free(sp);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	ret = _krb5_get_krbtgt(context, handle->ccache,
			       handle->principal->realm,
			       &creds);
	if (ret) {
	    krb5_storage_free(sp);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	ret = krb5_store_creds(sp, creds);
	krb5_free_creds(context, creds);
	if (ret) {
	    krb5_storage_free(sp);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

    } else {
	ret = krb5_store_uint32(sp, 1);
	if (ret) {
	    krb5_storage_free(sp);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	ret = krb5_cc_get_full_name(context, handle->ccache, &str);
	if (ret) {
	    krb5_storage_free(sp);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	ret = krb5_store_string(sp, str);
	free(str);
	if (ret) {
	    krb5_storage_free(sp);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}
    }
    ret = krb5_storage_to_data(sp, &data);
    krb5_storage_free(sp);
    if (ret) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }
    sp = krb5_storage_emem();
    if (sp == NULL) {
	krb5_data_free(&data);
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    mech.data = GSS_KRB5_MECHANISM->elements;
    mech.length = GSS_KRB5_MECHANISM->length;

    ret = krb5_store_data(sp, mech);
    if (ret) {
	krb5_data_free(&data);
	krb5_storage_free(sp);
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    ret = krb5_store_data(sp, data);
    krb5_data_free(&data);
    if (ret) {
	krb5_storage_free(sp);
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    ret = krb5_storage_to_data(sp, &data);
    krb5_storage_free(sp);
    if (ret) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    cred_token->value = data.data;
    cred_token->length = data.length;

    return GSS_S_COMPLETE;
}

OM_uint32 GSSAPI_CALLCONV
_gsskrb5_import_cred(OM_uint32 * minor_status,
		     gss_buffer_t cred_token,
		     gss_cred_id_t * cred_handle)
{
    krb5_context context;
    krb5_error_code ret;
    gsskrb5_cred handle;
    krb5_ccache id;
    krb5_storage *sp;
    char *str;
    uint32_t type;
    int flags = 0;

    *cred_handle = GSS_C_NO_CREDENTIAL;

    GSSAPI_KRB5_INIT (&context);

    sp = krb5_storage_from_mem(cred_token->value, cred_token->length);
    if (sp == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    ret = krb5_ret_uint32(sp, &type);
    if (ret) {
	krb5_storage_free(sp);
	*minor_status = ret;
	return GSS_S_FAILURE;
    }
    switch (type) {
    case 0: {
	krb5_creds creds;

	ret = krb5_ret_creds(sp, &creds);
	krb5_storage_free(sp);
	if (ret) {
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	ret = krb5_cc_new_unique(context, "MEMORY", NULL, &id);
	if (ret) {
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	ret = krb5_cc_initialize(context, id, creds.client);
	if (ret) {
	    krb5_cc_destroy(context, id);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	ret = krb5_cc_store_cred(context, id, &creds);
	krb5_free_cred_contents(context, &creds);

	flags |= GSS_CF_DESTROY_CRED_ON_RELEASE;

	break;
    }
    case 1:
	ret = krb5_ret_string(sp, &str);
	krb5_storage_free(sp);
	if (ret) {
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	ret = krb5_cc_resolve(context, str, &id);
	krb5_xfree(str);
	if (ret) {
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}
	break;

    default:
	krb5_storage_free(sp);
	*minor_status = 0;
	return GSS_S_NO_CRED;
    }

    handle = calloc(1, sizeof(*handle));
    if (handle == NULL) {
	krb5_cc_close(context, id);
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    handle->usage = GSS_C_INITIATE;
    krb5_cc_get_principal(context, id, &handle->principal);
    handle->ccache = id;
    handle->cred_flags = flags;

    *cred_handle = (gss_cred_id_t)handle;

    return GSS_S_COMPLETE;
}
