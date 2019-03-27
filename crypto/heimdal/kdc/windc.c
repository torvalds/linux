/*
 * Copyright (c) 2007 Kungliga Tekniska Högskolan
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

#include "kdc_locl.h"

static krb5plugin_windc_ftable *windcft;
static void *windcctx;

/*
 * Pick the first WINDC module that we find.
 */

krb5_error_code
krb5_kdc_windc_init(krb5_context context)
{
    struct krb5_plugin *list = NULL, *e;
    krb5_error_code ret;

    ret = _krb5_plugin_find(context, PLUGIN_TYPE_DATA, "windc", &list);
    if(ret != 0 || list == NULL)
	return 0;

    for (e = list; e != NULL; e = _krb5_plugin_get_next(e)) {

	windcft = _krb5_plugin_get_symbol(e);
	if (windcft->minor_version < KRB5_WINDC_PLUGIN_MINOR)
	    continue;

	(*windcft->init)(context, &windcctx);
	break;
    }
    _krb5_plugin_free(list);
    if (e == NULL) {
	krb5_set_error_message(context, ENOENT, "Did not find any WINDC plugin");
	windcft = NULL;
	return ENOENT;
    }

    return 0;
}


krb5_error_code
_kdc_pac_generate(krb5_context context,
		  hdb_entry_ex *client,
		  krb5_pac *pac)
{
    *pac = NULL;
    if (windcft == NULL)
	return 0;
    return (windcft->pac_generate)(windcctx, context, client, pac);
}

krb5_error_code
_kdc_pac_verify(krb5_context context,
		const krb5_principal client_principal,
		const krb5_principal delegated_proxy_principal,
		hdb_entry_ex *client,
		hdb_entry_ex *server,
		hdb_entry_ex *krbtgt,
		krb5_pac *pac,
		int *verified)
{
    krb5_error_code ret;

    if (windcft == NULL)
	return 0;

    ret = windcft->pac_verify(windcctx, context,
			      client_principal,
			      delegated_proxy_principal,
			      client, server, krbtgt, pac);
    if (ret == 0)
	*verified = 1;
    return ret;
}

krb5_error_code
_kdc_check_access(krb5_context context,
		  krb5_kdc_configuration *config,
		  hdb_entry_ex *client_ex, const char *client_name,
		  hdb_entry_ex *server_ex, const char *server_name,
		  KDC_REQ *req,
		  krb5_data *e_data)
{
    if (windcft == NULL)
	    return kdc_check_flags(context, config,
				   client_ex, client_name,
				   server_ex, server_name,
				   req->msg_type == krb_as_req);

    return (windcft->client_access)(windcctx,
				    context, config,
				    client_ex, client_name,
				    server_ex, server_name,
				    req, e_data);
}
