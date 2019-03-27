/*
 * Copyright (c) 1997 - 2005 Kungliga Tekniska Högskolan
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

OM_uint32
__gsskrb5_ccache_lifetime(OM_uint32 *minor_status,
			  krb5_context context,
			  krb5_ccache id,
			  krb5_principal principal,
			  OM_uint32 *lifetime)
{
    krb5_creds in_cred, out_cred;
    krb5_const_realm realm;
    krb5_error_code kret;

    memset(&in_cred, 0, sizeof(in_cred));
    in_cred.client = principal;

    realm = krb5_principal_get_realm(context,  principal);
    if (realm == NULL) {
	_gsskrb5_clear_status ();
	*minor_status = KRB5_PRINC_NOMATCH; /* XXX */
	return GSS_S_FAILURE;
    }

    kret = krb5_make_principal(context, &in_cred.server,
			       realm, KRB5_TGS_NAME, realm, NULL);
    if (kret) {
	*minor_status = kret;
	return GSS_S_FAILURE;
    }

    kret = krb5_cc_retrieve_cred(context, id, 0, &in_cred, &out_cred);
    krb5_free_principal(context, in_cred.server);
    if (kret) {
	*minor_status = 0;
	*lifetime = 0;
	return GSS_S_COMPLETE;
    }

    *lifetime = out_cred.times.endtime;
    krb5_free_cred_contents(context, &out_cred);

    return GSS_S_COMPLETE;
}




static krb5_error_code
get_keytab(krb5_context context, krb5_keytab *keytab)
{
    krb5_error_code kret;

    HEIMDAL_MUTEX_lock(&gssapi_keytab_mutex);

    if (_gsskrb5_keytab != NULL) {
	char *name = NULL;

	kret = krb5_kt_get_full_name(context, _gsskrb5_keytab, &name);
	if (kret == 0) {
	    kret = krb5_kt_resolve(context, name, keytab);
	    krb5_xfree(name);
	}
    } else
	kret = krb5_kt_default(context, keytab);

    HEIMDAL_MUTEX_unlock(&gssapi_keytab_mutex);

    return (kret);
}

static OM_uint32 acquire_initiator_cred
		  (OM_uint32 * minor_status,
		   krb5_context context,
		   gss_const_OID credential_type,
		   const void *credential_data,
		   const gss_name_t desired_name,
		   OM_uint32 time_req,
		   gss_const_OID desired_mech,
		   gss_cred_usage_t cred_usage,
		   gsskrb5_cred handle
		  )
{
    OM_uint32 ret;
    krb5_creds cred;
    krb5_principal def_princ;
    krb5_get_init_creds_opt *opt;
    krb5_ccache ccache;
    krb5_keytab keytab;
    krb5_error_code kret;

    keytab = NULL;
    ccache = NULL;
    def_princ = NULL;
    ret = GSS_S_FAILURE;
    memset(&cred, 0, sizeof(cred));

    /*
     * If we have a preferred principal, lets try to find it in all
     * caches, otherwise, fall back to default cache, ignore all
     * errors while searching.
     */

    if (credential_type != GSS_C_NO_OID &&
	!gss_oid_equal(credential_type, GSS_C_CRED_PASSWORD)) {
	kret = KRB5_NOCREDS_SUPPLIED; /* XXX */
	goto end;
    }

    if (handle->principal) {
	kret = krb5_cc_cache_match (context,
				    handle->principal,
				    &ccache);
	if (kret == 0) {
	    ret = GSS_S_COMPLETE;
	    goto found;
	}
    }

    if (ccache == NULL) {
	kret = krb5_cc_default(context, &ccache);
	if (kret)
	    goto end;
    }
    kret = krb5_cc_get_principal(context, ccache, &def_princ);
    if (kret != 0) {
	/* we'll try to use a keytab below */
	krb5_cc_close(context, ccache);
	def_princ = NULL;
	kret = 0;
    } else if (handle->principal == NULL)  {
	kret = krb5_copy_principal(context, def_princ, &handle->principal);
	if (kret)
	    goto end;
    } else if (handle->principal != NULL &&
	       krb5_principal_compare(context, handle->principal,
				      def_princ) == FALSE) {
	krb5_free_principal(context, def_princ);
	def_princ = NULL;
	krb5_cc_close(context, ccache);
	ccache = NULL;
    }
    if (def_princ == NULL) {
	/* We have no existing credentials cache,
	 * so attempt to get a TGT using a keytab.
	 */
	if (handle->principal == NULL) {
	    kret = krb5_get_default_principal(context, &handle->principal);
	    if (kret)
		goto end;
	}
	kret = krb5_get_init_creds_opt_alloc(context, &opt);
	if (kret)
	    goto end;
	if (credential_type != GSS_C_NO_OID &&
	    gss_oid_equal(credential_type, GSS_C_CRED_PASSWORD)) {
	    gss_buffer_t password = (gss_buffer_t)credential_data;

	    /* XXX are we requiring password to be NUL terminated? */

	    kret = krb5_get_init_creds_password(context, &cred,
						handle->principal,
						password->value,
						NULL, NULL, 0, NULL, opt);
	} else {
	    kret = get_keytab(context, &keytab);
	    if (kret) {
		krb5_get_init_creds_opt_free(context, opt);
		goto end;
	    }
	    kret = krb5_get_init_creds_keytab(context, &cred,
					      handle->principal, keytab,
					      0, NULL, opt);
	}
	krb5_get_init_creds_opt_free(context, opt);
	if (kret)
	    goto end;
	kret = krb5_cc_new_unique(context, krb5_cc_type_memory,
				  NULL, &ccache);
	if (kret)
	    goto end;
	kret = krb5_cc_initialize(context, ccache, cred.client);
	if (kret) {
	    krb5_cc_destroy(context, ccache);
	    goto end;
	}
	kret = krb5_cc_store_cred(context, ccache, &cred);
	if (kret) {
	    krb5_cc_destroy(context, ccache);
	    goto end;
	}
	handle->lifetime = cred.times.endtime;
	handle->cred_flags |= GSS_CF_DESTROY_CRED_ON_RELEASE;
    } else {

	ret = __gsskrb5_ccache_lifetime(minor_status,
					context,
					ccache,
					handle->principal,
					&handle->lifetime);
	if (ret != GSS_S_COMPLETE) {
	    krb5_cc_close(context, ccache);
	    goto end;
	}
	kret = 0;
    }
 found:
    handle->ccache = ccache;
    ret = GSS_S_COMPLETE;

end:
    if (cred.client != NULL)
	krb5_free_cred_contents(context, &cred);
    if (def_princ != NULL)
	krb5_free_principal(context, def_princ);
    if (keytab != NULL)
	krb5_kt_close(context, keytab);
    if (ret != GSS_S_COMPLETE && kret != 0)
	*minor_status = kret;
    return (ret);
}

static OM_uint32 acquire_acceptor_cred
		  (OM_uint32 * minor_status,
		   krb5_context context,
		   gss_const_OID credential_type,
		   const void *credential_data,
		   const gss_name_t desired_name,
		   OM_uint32 time_req,
		   gss_const_OID desired_mech,
		   gss_cred_usage_t cred_usage,
		   gsskrb5_cred handle
		  )
{
    OM_uint32 ret;
    krb5_error_code kret;

    ret = GSS_S_FAILURE;

    if (credential_type != GSS_C_NO_OID) {
	kret = EINVAL;
	goto end;
    }

    kret = get_keytab(context, &handle->keytab);
    if (kret)
	goto end;

    /* check that the requested principal exists in the keytab */
    if (handle->principal) {
	krb5_keytab_entry entry;

	kret = krb5_kt_get_entry(context, handle->keytab,
				 handle->principal, 0, 0, &entry);
	if (kret)
	    goto end;
	krb5_kt_free_entry(context, &entry);
	ret = GSS_S_COMPLETE;
    } else {
	/*
	 * Check if there is at least one entry in the keytab before
	 * declaring it as an useful keytab.
	 */
	krb5_keytab_entry tmp;
	krb5_kt_cursor c;

	kret = krb5_kt_start_seq_get (context, handle->keytab, &c);
	if (kret)
	    goto end;
	if (krb5_kt_next_entry(context, handle->keytab, &tmp, &c) == 0) {
	    krb5_kt_free_entry(context, &tmp);
	    ret = GSS_S_COMPLETE; /* ok found one entry */
	}
	krb5_kt_end_seq_get (context, handle->keytab, &c);
    }
end:
    if (ret != GSS_S_COMPLETE) {
	if (handle->keytab != NULL)
	    krb5_kt_close(context, handle->keytab);
	if (kret != 0) {
	    *minor_status = kret;
	}
    }
    return (ret);
}

OM_uint32 GSSAPI_CALLCONV _gsskrb5_acquire_cred
(OM_uint32 * minor_status,
 const gss_name_t desired_name,
 OM_uint32 time_req,
 const gss_OID_set desired_mechs,
 gss_cred_usage_t cred_usage,
 gss_cred_id_t * output_cred_handle,
 gss_OID_set * actual_mechs,
 OM_uint32 * time_rec
    )
{
    OM_uint32 ret;

    if (desired_mechs) {
	int present = 0;

	ret = gss_test_oid_set_member(minor_status, GSS_KRB5_MECHANISM,
				      desired_mechs, &present);
	if (ret)
	    return ret;
	if (!present) {
	    *minor_status = 0;
	    return GSS_S_BAD_MECH;
	}
    }

    ret = _gsskrb5_acquire_cred_ext(minor_status,
				    desired_name,
				    GSS_C_NO_OID,
				    NULL,
				    time_req,
				    GSS_KRB5_MECHANISM,
				    cred_usage,
				    output_cred_handle);
    if (ret)
	return ret;


    ret = _gsskrb5_inquire_cred(minor_status, *output_cred_handle,
				NULL, time_rec, NULL, actual_mechs);
    if (ret) {
	OM_uint32 tmp;
	_gsskrb5_release_cred(&tmp, output_cred_handle);
    }

    return ret;
}

OM_uint32 GSSAPI_CALLCONV _gsskrb5_acquire_cred_ext
(OM_uint32 * minor_status,
 const gss_name_t desired_name,
 gss_const_OID credential_type,
 const void *credential_data,
 OM_uint32 time_req,
 gss_const_OID desired_mech,
 gss_cred_usage_t cred_usage,
 gss_cred_id_t * output_cred_handle
    )
{
    krb5_context context;
    gsskrb5_cred handle;
    OM_uint32 ret;

    cred_usage &= GSS_C_OPTION_MASK;

    if (cred_usage != GSS_C_ACCEPT && cred_usage != GSS_C_INITIATE && cred_usage != GSS_C_BOTH) {
	*minor_status = GSS_KRB5_S_G_BAD_USAGE;
	return GSS_S_FAILURE;
    }

    GSSAPI_KRB5_INIT(&context);

    *output_cred_handle = NULL;

    handle = calloc(1, sizeof(*handle));
    if (handle == NULL) {
	*minor_status = ENOMEM;
        return (GSS_S_FAILURE);
    }

    HEIMDAL_MUTEX_init(&handle->cred_id_mutex);

    if (desired_name != GSS_C_NO_NAME) {
	ret = _gsskrb5_canon_name(minor_status, context, 1, NULL,
				  desired_name, &handle->principal);
	if (ret) {
	    HEIMDAL_MUTEX_destroy(&handle->cred_id_mutex);
	    free(handle);
	    return ret;
	}
    }
    if (cred_usage == GSS_C_INITIATE || cred_usage == GSS_C_BOTH) {
	ret = acquire_initiator_cred(minor_status, context,
				     credential_type, credential_data,
				     desired_name, time_req,
				     desired_mech, cred_usage, handle);
    	if (ret != GSS_S_COMPLETE) {
	    HEIMDAL_MUTEX_destroy(&handle->cred_id_mutex);
	    krb5_free_principal(context, handle->principal);
	    free(handle);
	    return (ret);
	}
    }
    if (cred_usage == GSS_C_ACCEPT || cred_usage == GSS_C_BOTH) {
	ret = acquire_acceptor_cred(minor_status, context,
				    credential_type, credential_data,
				    desired_name, time_req,
				    desired_mech, cred_usage, handle);
	if (ret != GSS_S_COMPLETE) {
	    HEIMDAL_MUTEX_destroy(&handle->cred_id_mutex);
	    krb5_free_principal(context, handle->principal);
	    free(handle);
	    return (ret);
	}
    }
    ret = gss_create_empty_oid_set(minor_status, &handle->mechanisms);
    if (ret == GSS_S_COMPLETE)
    	ret = gss_add_oid_set_member(minor_status, GSS_KRB5_MECHANISM,
				     &handle->mechanisms);
    if (ret != GSS_S_COMPLETE) {
	if (handle->mechanisms != NULL)
	    gss_release_oid_set(NULL, &handle->mechanisms);
	HEIMDAL_MUTEX_destroy(&handle->cred_id_mutex);
	krb5_free_principal(context, handle->principal);
	free(handle);
	return (ret);
    }
    handle->usage = cred_usage;
    *minor_status = 0;
    *output_cred_handle = (gss_cred_id_t)handle;
    return (GSS_S_COMPLETE);
}
