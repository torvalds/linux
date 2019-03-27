/*
 * Copyright (c) 2004, PADL Software Pty Ltd.
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

static OM_uint32
import_cred(OM_uint32 *minor_status,
	    krb5_context context,
            gss_cred_id_t *cred_handle,
            const gss_buffer_t value)
{
    OM_uint32 major_stat;
    krb5_error_code ret;
    krb5_principal keytab_principal = NULL;
    krb5_keytab keytab = NULL;
    krb5_storage *sp = NULL;
    krb5_ccache id = NULL;
    char *str;

    if (cred_handle == NULL || *cred_handle != GSS_C_NO_CREDENTIAL) {
	*minor_status = 0;
	return GSS_S_FAILURE;
    }

    sp = krb5_storage_from_mem(value->value, value->length);
    if (sp == NULL) {
	*minor_status = 0;
	return GSS_S_FAILURE;
    }

    /* credential cache name */
    ret = krb5_ret_string(sp, &str);
    if (ret) {
	*minor_status = ret;
	major_stat =  GSS_S_FAILURE;
	goto out;
    }
    if (str[0]) {
	ret = krb5_cc_resolve(context, str, &id);
	if (ret) {
	    *minor_status = ret;
	    major_stat =  GSS_S_FAILURE;
	    goto out;
	}
    }
    free(str);
    str = NULL;

    /* keytab principal name */
    ret = krb5_ret_string(sp, &str);
    if (ret == 0 && str[0])
	ret = krb5_parse_name(context, str, &keytab_principal);
    if (ret) {
	*minor_status = ret;
	major_stat = GSS_S_FAILURE;
	goto out;
    }
    free(str);
    str = NULL;

    /* keytab principal */
    ret = krb5_ret_string(sp, &str);
    if (ret) {
	*minor_status = ret;
	major_stat =  GSS_S_FAILURE;
	goto out;
    }
    if (str[0]) {
	ret = krb5_kt_resolve(context, str, &keytab);
	if (ret) {
	    *minor_status = ret;
	    major_stat =  GSS_S_FAILURE;
	    goto out;
	}
    }
    free(str);
    str = NULL;

    major_stat = _gsskrb5_krb5_import_cred(minor_status, id, keytab_principal,
					   keytab, cred_handle);
out:
    if (id)
	krb5_cc_close(context, id);
    if (keytab_principal)
	krb5_free_principal(context, keytab_principal);
    if (keytab)
	krb5_kt_close(context, keytab);
    if (str)
	free(str);
    if (sp)
	krb5_storage_free(sp);

    return major_stat;
}


static OM_uint32
allowed_enctypes(OM_uint32 *minor_status,
		 krb5_context context,
		 gss_cred_id_t *cred_handle,
		 const gss_buffer_t value)
{
    OM_uint32 major_stat;
    krb5_error_code ret;
    size_t len, i;
    krb5_enctype *enctypes = NULL;
    krb5_storage *sp = NULL;
    gsskrb5_cred cred;

    if (cred_handle == NULL || *cred_handle == GSS_C_NO_CREDENTIAL) {
	*minor_status = 0;
	return GSS_S_FAILURE;
    }

    cred = (gsskrb5_cred)*cred_handle;

    if ((value->length % 4) != 0) {
	*minor_status = 0;
	major_stat = GSS_S_FAILURE;
	goto out;
    }

    len = value->length / 4;
    enctypes = malloc((len + 1) * 4);
    if (enctypes == NULL) {
	*minor_status = ENOMEM;
	major_stat = GSS_S_FAILURE;
	goto out;
    }

    sp = krb5_storage_from_mem(value->value, value->length);
    if (sp == NULL) {
	*minor_status = ENOMEM;
	major_stat = GSS_S_FAILURE;
	goto out;
    }

    for (i = 0; i < len; i++) {
	uint32_t e;

	ret = krb5_ret_uint32(sp, &e);
	if (ret) {
	    *minor_status = ret;
	    major_stat =  GSS_S_FAILURE;
	    goto out;
	}
	enctypes[i] = e;
    }
    enctypes[i] = 0;

    if (cred->enctypes)
	free(cred->enctypes);
    cred->enctypes = enctypes;

    krb5_storage_free(sp);

    return GSS_S_COMPLETE;

out:
    if (sp)
	krb5_storage_free(sp);
    if (enctypes)
	free(enctypes);

    return major_stat;
}

static OM_uint32
no_ci_flags(OM_uint32 *minor_status,
	    krb5_context context,
	    gss_cred_id_t *cred_handle,
	    const gss_buffer_t value)
{
    gsskrb5_cred cred;

    if (cred_handle == NULL || *cred_handle == GSS_C_NO_CREDENTIAL) {
	*minor_status = 0;
	return GSS_S_FAILURE;
    }

    cred = (gsskrb5_cred)*cred_handle;
    cred->cred_flags |= GSS_CF_NO_CI_FLAGS;

    *minor_status = 0;
    return GSS_S_COMPLETE;

}


OM_uint32 GSSAPI_CALLCONV
_gsskrb5_set_cred_option
           (OM_uint32 *minor_status,
            gss_cred_id_t *cred_handle,
            const gss_OID desired_object,
            const gss_buffer_t value)
{
    krb5_context context;

    GSSAPI_KRB5_INIT (&context);

    if (value == GSS_C_NO_BUFFER) {
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    if (gss_oid_equal(desired_object, GSS_KRB5_IMPORT_CRED_X))
	return import_cred(minor_status, context, cred_handle, value);

    if (gss_oid_equal(desired_object, GSS_KRB5_SET_ALLOWABLE_ENCTYPES_X))
	return allowed_enctypes(minor_status, context, cred_handle, value);

    if (gss_oid_equal(desired_object, GSS_KRB5_CRED_NO_CI_FLAGS_X)) {
	return no_ci_flags(minor_status, context, cred_handle, value);
    }


    *minor_status = EINVAL;
    return GSS_S_FAILURE;
}
