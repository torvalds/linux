/*
 * Copyright (c) 1997 - 2004 Kungliga Tekniska Högskolan
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

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_mk_req_exact(krb5_context context,
		  krb5_auth_context *auth_context,
		  const krb5_flags ap_req_options,
		  const krb5_principal server,
		  krb5_data *in_data,
		  krb5_ccache ccache,
		  krb5_data *outbuf)
{
    krb5_error_code ret;
    krb5_creds this_cred, *cred;

    memset(&this_cred, 0, sizeof(this_cred));

    ret = krb5_cc_get_principal(context, ccache, &this_cred.client);

    if(ret)
	return ret;

    ret = krb5_copy_principal (context, server, &this_cred.server);
    if (ret) {
	krb5_free_cred_contents (context, &this_cred);
	return ret;
    }

    this_cred.times.endtime = 0;
    if (auth_context && *auth_context && (*auth_context)->keytype)
	this_cred.session.keytype = (*auth_context)->keytype;

    ret = krb5_get_credentials (context, 0, ccache, &this_cred, &cred);
    krb5_free_cred_contents(context, &this_cred);
    if (ret)
	return ret;

    ret = krb5_mk_req_extended (context,
				auth_context,
				ap_req_options,
				in_data,
				cred,
				outbuf);
    krb5_free_creds(context, cred);
    return ret;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_mk_req(krb5_context context,
	    krb5_auth_context *auth_context,
	    const krb5_flags ap_req_options,
	    const char *service,
	    const char *hostname,
	    krb5_data *in_data,
	    krb5_ccache ccache,
	    krb5_data *outbuf)
{
    krb5_error_code ret;
    char **realms;
    char *real_hostname;
    krb5_principal server;

    ret = krb5_expand_hostname_realms (context, hostname,
				       &real_hostname, &realms);
    if (ret)
	return ret;

    ret = krb5_build_principal (context, &server,
				strlen(*realms),
				*realms,
				service,
				real_hostname,
				NULL);
    free (real_hostname);
    krb5_free_host_realm (context, realms);
    if (ret)
	return ret;
    ret = krb5_mk_req_exact (context, auth_context, ap_req_options,
			     server, in_data, ccache, outbuf);
    krb5_free_principal (context, server);
    return ret;
}
