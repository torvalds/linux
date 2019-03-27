/*
 * Copyright (c) 1997 - 2008 Kungliga Tekniska Högskolan
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

#include "krb5_locl.h"

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_verify_init_creds_opt_init(krb5_verify_init_creds_opt *options)
{
    memset (options, 0, sizeof(*options));
}

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_verify_init_creds_opt_set_ap_req_nofail(krb5_verify_init_creds_opt *options,
					     int ap_req_nofail)
{
    options->flags |= KRB5_VERIFY_INIT_CREDS_OPT_AP_REQ_NOFAIL;
    options->ap_req_nofail = ap_req_nofail;
}

/*
 *
 */

static krb5_boolean
fail_verify_is_ok (krb5_context context,
		   krb5_verify_init_creds_opt *options)
{
    if ((options->flags & KRB5_VERIFY_INIT_CREDS_OPT_AP_REQ_NOFAIL
	 && options->ap_req_nofail != 0)
	|| krb5_config_get_bool (context,
				 NULL,
				 "libdefaults",
				 "verify_ap_req_nofail",
				 NULL))
	return FALSE;
    else
	return TRUE;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_verify_init_creds(krb5_context context,
		       krb5_creds *creds,
		       krb5_principal ap_req_server,
		       krb5_keytab ap_req_keytab,
		       krb5_ccache *ccache,
		       krb5_verify_init_creds_opt *options)
{
    krb5_error_code ret;
    krb5_data req;
    krb5_ccache local_ccache = NULL;
    krb5_creds *new_creds = NULL;
    krb5_auth_context auth_context = NULL;
    krb5_principal server = NULL;
    krb5_keytab keytab = NULL;

    krb5_data_zero (&req);

    if (ap_req_server == NULL) {
	char local_hostname[MAXHOSTNAMELEN];

	if (gethostname (local_hostname, sizeof(local_hostname)) < 0) {
	    ret = errno;
	    krb5_set_error_message (context, ret, "gethostname: %s",
				    strerror(ret));
	    return ret;
	}

	ret = krb5_sname_to_principal (context,
				       local_hostname,
				       "host",
				       KRB5_NT_SRV_HST,
				       &server);
	if (ret)
	    goto cleanup;
    } else
	server = ap_req_server;

    if (ap_req_keytab == NULL) {
	ret = krb5_kt_default (context, &keytab);
	if (ret)
	    goto cleanup;
    } else
	keytab = ap_req_keytab;

    if (ccache && *ccache)
	local_ccache = *ccache;
    else {
	ret = krb5_cc_new_unique(context, krb5_cc_type_memory,
				 NULL, &local_ccache);
	if (ret)
	    goto cleanup;
	ret = krb5_cc_initialize (context,
				  local_ccache,
				  creds->client);
	if (ret)
	    goto cleanup;
	ret = krb5_cc_store_cred (context,
				  local_ccache,
				  creds);
	if (ret)
	    goto cleanup;
    }

    if (!krb5_principal_compare (context, server, creds->server)) {
	krb5_creds match_cred;

	memset (&match_cred, 0, sizeof(match_cred));

	match_cred.client = creds->client;
	match_cred.server = server;

	ret = krb5_get_credentials (context,
				    0,
				    local_ccache,
				    &match_cred,
				    &new_creds);
	if (ret) {
	    if (fail_verify_is_ok (context, options))
		ret = 0;
	    goto cleanup;
	}
	creds = new_creds;
    }

    ret = krb5_mk_req_extended (context,
				&auth_context,
				0,
				NULL,
				creds,
				&req);

    krb5_auth_con_free (context, auth_context);
    auth_context = NULL;

    if (ret)
	goto cleanup;

    ret = krb5_rd_req (context,
		       &auth_context,
		       &req,
		       server,
		       keytab,
		       0,
		       NULL);

    if (ret == KRB5_KT_NOTFOUND && fail_verify_is_ok (context, options))
	ret = 0;
cleanup:
    if (auth_context)
	krb5_auth_con_free (context, auth_context);
    krb5_data_free (&req);
    if (new_creds != NULL)
	krb5_free_creds (context, new_creds);
    if (ap_req_server == NULL && server)
	krb5_free_principal (context, server);
    if (ap_req_keytab == NULL && keytab)
	krb5_kt_close (context, keytab);
    if (local_ccache != NULL
	&&
	(ccache == NULL
	 || (ret != 0 && *ccache == NULL)))
	krb5_cc_destroy (context, local_ccache);

    if (ret == 0 && ccache != NULL && *ccache == NULL)
	*ccache = local_ccache;

    return ret;
}

/**
 * Validate the newly fetch credential, see also krb5_verify_init_creds().
 *
 * @param context a Kerberos 5 context
 * @param creds the credentials to verify
 * @param client the client name to match up
 * @param ccache the credential cache to use
 * @param service a service name to use, used with
 *        krb5_sname_to_principal() to build a hostname to use to
 *        verify.
 *
 * @ingroup krb5_ccache
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_validated_creds(krb5_context context,
			 krb5_creds *creds,
			 krb5_principal client,
			 krb5_ccache ccache,
			 char *service)
{
    krb5_verify_init_creds_opt vopt;
    krb5_principal server;
    krb5_error_code ret;

    if (krb5_principal_compare(context, creds->client, client) != TRUE) {
	krb5_set_error_message(context, KRB5_PRINC_NOMATCH,
			       N_("Validation credentials and client "
				  "doesn't match", ""));
	return KRB5_PRINC_NOMATCH;
    }

    ret = krb5_sname_to_principal (context, NULL, service,
				   KRB5_NT_SRV_HST, &server);
    if(ret)
	return ret;

    krb5_verify_init_creds_opt_init(&vopt);

    ret = krb5_verify_init_creds(context, creds, server, NULL, NULL, &vopt);
    krb5_free_principal(context, server);

    return ret;
}
