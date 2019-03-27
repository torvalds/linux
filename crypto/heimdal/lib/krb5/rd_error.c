/*
 * Copyright (c) 1997 - 2001 Kungliga Tekniska Högskolan
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
krb5_rd_error(krb5_context context,
	      const krb5_data *msg,
	      KRB_ERROR *result)
{

    size_t len;
    krb5_error_code ret;

    ret = decode_KRB_ERROR(msg->data, msg->length, result, &len);
    if(ret) {
	krb5_clear_error_message(context);
	return ret;
    }
    result->error_code += KRB5KDC_ERR_NONE;
    return 0;
}

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_error_contents (krb5_context context,
			  krb5_error *error)
{
    free_KRB_ERROR(error);
    memset(error, 0, sizeof(*error));
}

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_error (krb5_context context,
		 krb5_error *error)
{
    krb5_free_error_contents (context, error);
    free (error);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_error_from_rd_error(krb5_context context,
			 const krb5_error *error,
			 const krb5_creds *creds)
{
    krb5_error_code ret;

    ret = error->error_code;
    if (error->e_text != NULL) {
	krb5_set_error_message(context, ret, "%s", *error->e_text);
    } else {
	char clientname[256], servername[256];

	if (creds != NULL) {
	    krb5_unparse_name_fixed(context, creds->client,
				    clientname, sizeof(clientname));
	    krb5_unparse_name_fixed(context, creds->server,
				    servername, sizeof(servername));
	}

	switch (ret) {
	case KRB5KDC_ERR_NAME_EXP :
	    krb5_set_error_message(context, ret,
				   N_("Client %s%s%s expired", ""),
				   creds ? "(" : "",
				   creds ? clientname : "",
				   creds ? ")" : "");
	    break;
	case KRB5KDC_ERR_SERVICE_EXP :
	    krb5_set_error_message(context, ret,
				   N_("Server %s%s%s expired", ""),
				   creds ? "(" : "",
				   creds ? servername : "",
				   creds ? ")" : "");
	    break;
	case KRB5KDC_ERR_C_PRINCIPAL_UNKNOWN :
	    krb5_set_error_message(context, ret,
				   N_("Client %s%s%s unknown", ""),
				   creds ? "(" : "",
				   creds ? clientname : "",
				   creds ? ")" : "");
	    break;
	case KRB5KDC_ERR_S_PRINCIPAL_UNKNOWN :
	    krb5_set_error_message(context, ret,
				   N_("Server %s%s%s unknown", ""),
				   creds ? "(" : "",
				   creds ? servername : "",
				   creds ? ")" : "");
	    break;
	default :
	    krb5_clear_error_message(context);
	    break;
	}
    }
    return ret;
}
