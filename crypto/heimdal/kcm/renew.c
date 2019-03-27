/*
 * Copyright (c) 2005, PADL Software Pty Ltd.
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

#include "kcm_locl.h"

RCSID("$Id$");

krb5_error_code
kcm_ccache_refresh(krb5_context context,
		   kcm_ccache ccache,
		   krb5_creds **credp)
{
    krb5_error_code ret;
    krb5_creds in, *out;
    krb5_kdc_flags flags;
    krb5_const_realm realm;
    krb5_ccache_data ccdata;

    memset(&in, 0, sizeof(in));

    KCM_ASSERT_VALID(ccache);

    if (ccache->client == NULL) {
	/* no primary principal */
	kcm_log(0, "Refresh credentials requested but no client principal");
	return KRB5_CC_NOTFOUND;
    }

    HEIMDAL_MUTEX_lock(&ccache->mutex);

    /* Fake up an internal ccache */
    kcm_internal_ccache(context, ccache, &ccdata);

    /* Find principal */
    in.client = ccache->client;

    if (ccache->server != NULL) {
	ret = krb5_copy_principal(context, ccache->server, &in.server);
	if (ret) {
	    kcm_log(0, "Failed to copy service principal: %s",
		    krb5_get_err_text(context, ret));
	    goto out;
	}
    } else {
	realm = krb5_principal_get_realm(context, in.client);
	ret = krb5_make_principal(context, &in.server, realm,
				  KRB5_TGS_NAME, realm, NULL);
	if (ret) {
	    kcm_log(0, "Failed to make TGS principal for realm %s: %s",
		    realm, krb5_get_err_text(context, ret));
	    goto out;
	}
    }

    if (ccache->tkt_life)
	in.times.endtime = time(NULL) + ccache->tkt_life;
    if (ccache->renew_life)
	in.times.renew_till = time(NULL) + ccache->renew_life;

    flags.i = 0;
    flags.b.renewable = TRUE;
    flags.b.renew = TRUE;

    ret = krb5_get_kdc_cred(context,
			    &ccdata,
			    flags,
			    NULL,
			    NULL,
			    &in,
			    &out);
    if (ret) {
	kcm_log(0, "Failed to renew credentials for cache %s: %s",
		ccache->name, krb5_get_err_text(context, ret));
	goto out;
    }

    /* Swap them in */
    kcm_ccache_remove_creds_internal(context, ccache);

    ret = kcm_ccache_store_cred_internal(context, ccache, out, 0, credp);
    if (ret) {
	kcm_log(0, "Failed to store credentials for cache %s: %s",
		ccache->name, krb5_get_err_text(context, ret));
	krb5_free_creds(context, out);
	goto out;
    }

    free(out); /* but not contents */

out:
    HEIMDAL_MUTEX_unlock(&ccache->mutex);

    return ret;
}

