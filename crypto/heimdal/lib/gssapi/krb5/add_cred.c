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

OM_uint32 GSSAPI_CALLCONV _gsskrb5_add_cred (
     OM_uint32           *minor_status,
     const gss_cred_id_t input_cred_handle,
     const gss_name_t    desired_name,
     const gss_OID       desired_mech,
     gss_cred_usage_t    cred_usage,
     OM_uint32           initiator_time_req,
     OM_uint32           acceptor_time_req,
     gss_cred_id_t       *output_cred_handle,
     gss_OID_set         *actual_mechs,
     OM_uint32           *initiator_time_rec,
     OM_uint32           *acceptor_time_rec)
{
    krb5_context context;
    OM_uint32 ret, lifetime;
    gsskrb5_cred cred, handle;
    krb5_const_principal dname;

    handle = NULL;
    cred = (gsskrb5_cred)input_cred_handle;
    dname = (krb5_const_principal)desired_name;

    GSSAPI_KRB5_INIT (&context);

    if (gss_oid_equal(desired_mech, GSS_KRB5_MECHANISM) == 0) {
	*minor_status = 0;
	return GSS_S_BAD_MECH;
    }

    if (cred == NULL && output_cred_handle == NULL) {
	*minor_status = 0;
	return GSS_S_NO_CRED;
    }

    if (cred == NULL) { /* XXX standard conformance failure */
	*minor_status = 0;
	return GSS_S_NO_CRED;
    }

    /* check if requested output usage is compatible with output usage */
    if (output_cred_handle != NULL) {
	HEIMDAL_MUTEX_lock(&cred->cred_id_mutex);
	if (cred->usage != cred_usage && cred->usage != GSS_C_BOTH) {
	    HEIMDAL_MUTEX_unlock(&cred->cred_id_mutex);
	    *minor_status = GSS_KRB5_S_G_BAD_USAGE;
	    return(GSS_S_FAILURE);
	}
    }

    /* check that we have the same name */
    if (dname != NULL &&
	krb5_principal_compare(context, dname,
			       cred->principal) != FALSE) {
	if (output_cred_handle)
	    HEIMDAL_MUTEX_unlock(&cred->cred_id_mutex);
	*minor_status = 0;
	return GSS_S_BAD_NAME;
    }

    /* make a copy */
    if (output_cred_handle) {
	krb5_error_code kret;

	handle = calloc(1, sizeof(*handle));
	if (handle == NULL) {
	    HEIMDAL_MUTEX_unlock(&cred->cred_id_mutex);
	    *minor_status = ENOMEM;
	    return (GSS_S_FAILURE);
	}

	handle->usage = cred_usage;
	handle->lifetime = cred->lifetime;
	handle->principal = NULL;
	handle->keytab = NULL;
	handle->ccache = NULL;
	handle->mechanisms = NULL;
	HEIMDAL_MUTEX_init(&handle->cred_id_mutex);

	ret = GSS_S_FAILURE;

	kret = krb5_copy_principal(context, cred->principal,
				  &handle->principal);
	if (kret) {
	    HEIMDAL_MUTEX_unlock(&cred->cred_id_mutex);
	    free(handle);
	    *minor_status = kret;
	    return GSS_S_FAILURE;
	}

	if (cred->keytab) {
	    char *name = NULL;

	    ret = GSS_S_FAILURE;

	    kret = krb5_kt_get_full_name(context, cred->keytab, &name);
	    if (kret) {
		*minor_status = kret;
		goto failure;
	    }

	    kret = krb5_kt_resolve(context, name,
				   &handle->keytab);
	    krb5_xfree(name);
	    if (kret){
		*minor_status = kret;
		goto failure;
	    }
	}

	if (cred->ccache) {
	    const char *type, *name;
	    char *type_name = NULL;

	    ret = GSS_S_FAILURE;

	    type = krb5_cc_get_type(context, cred->ccache);
	    if (type == NULL){
		*minor_status = ENOMEM;
		goto failure;
	    }

	    if (strcmp(type, "MEMORY") == 0) {
		ret = krb5_cc_new_unique(context, type,
					 NULL, &handle->ccache);
		if (ret) {
		    *minor_status = ret;
		    goto failure;
		}

		ret = krb5_cc_copy_cache(context, cred->ccache,
					 handle->ccache);
		if (ret) {
		    *minor_status = ret;
		    goto failure;
		}

	    } else {
		name = krb5_cc_get_name(context, cred->ccache);
		if (name == NULL) {
		    *minor_status = ENOMEM;
		    goto failure;
		}

		kret = asprintf(&type_name, "%s:%s", type, name);
		if (kret < 0 || type_name == NULL) {
		    *minor_status = ENOMEM;
		    goto failure;
		}

		kret = krb5_cc_resolve(context, type_name,
				       &handle->ccache);
		free(type_name);
		if (kret) {
		    *minor_status = kret;
		    goto failure;
		}
	    }
	}
	ret = gss_create_empty_oid_set(minor_status, &handle->mechanisms);
	if (ret)
	    goto failure;

	ret = gss_add_oid_set_member(minor_status, GSS_KRB5_MECHANISM,
				     &handle->mechanisms);
	if (ret)
	    goto failure;
    }

    HEIMDAL_MUTEX_unlock(&cred->cred_id_mutex);

    ret = _gsskrb5_inquire_cred(minor_status, (gss_cred_id_t)cred,
				NULL, &lifetime, NULL, actual_mechs);
    if (ret)
	goto failure;

    if (initiator_time_rec)
	*initiator_time_rec = lifetime;
    if (acceptor_time_rec)
	*acceptor_time_rec = lifetime;

    if (output_cred_handle) {
	*output_cred_handle = (gss_cred_id_t)handle;
    }

    *minor_status = 0;
    return ret;

 failure:

    if (handle) {
	if (handle->principal)
	    krb5_free_principal(context, handle->principal);
	if (handle->keytab)
	    krb5_kt_close(context, handle->keytab);
	if (handle->ccache)
	    krb5_cc_destroy(context, handle->ccache);
	if (handle->mechanisms)
	    gss_release_oid_set(NULL, &handle->mechanisms);
	free(handle);
    }
    if (output_cred_handle)
	HEIMDAL_MUTEX_unlock(&cred->cred_id_mutex);
    return ret;
}
