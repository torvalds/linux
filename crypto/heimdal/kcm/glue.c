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

/*
 * Server-side loopback glue for credentials cache operations; this
 * must be initialized with kcm_internal_ccache(), it is not for real
 * use. This entire file assumes the cache is locked, it does not do
 * any concurrency checking for multithread applications.
 */

#define KCMCACHE(X)	((kcm_ccache)(X)->data.data)
#define CACHENAME(X)	(KCMCACHE(X)->name)

static const char *
kcmss_get_name(krb5_context context,
	       krb5_ccache id)
{
    return CACHENAME(id);
}

static krb5_error_code
kcmss_resolve(krb5_context context, krb5_ccache *id, const char *res)
{
    return KRB5_FCC_INTERNAL;
}

static krb5_error_code
kcmss_gen_new(krb5_context context, krb5_ccache *id)
{
    return KRB5_FCC_INTERNAL;
}

static krb5_error_code
kcmss_initialize(krb5_context context,
		 krb5_ccache id,
		 krb5_principal primary_principal)
{
    krb5_error_code ret;
    kcm_ccache c = KCMCACHE(id);

    KCM_ASSERT_VALID(c);

    ret = kcm_zero_ccache_data_internal(context, c);
    if (ret)
	return ret;

    ret = krb5_copy_principal(context, primary_principal,
			      &c->client);

    return ret;
}

static krb5_error_code
kcmss_close(krb5_context context,
	    krb5_ccache id)
{
    kcm_ccache c = KCMCACHE(id);

    KCM_ASSERT_VALID(c);

    id->data.data = NULL;
    id->data.length = 0;

    return 0;
}

static krb5_error_code
kcmss_destroy(krb5_context context,
	      krb5_ccache id)
{
    krb5_error_code ret;
    kcm_ccache c = KCMCACHE(id);

    KCM_ASSERT_VALID(c);

    ret = kcm_ccache_destroy(context, CACHENAME(id));

    return ret;
}

static krb5_error_code
kcmss_store_cred(krb5_context context,
		 krb5_ccache id,
		 krb5_creds *creds)
{
    krb5_error_code ret;
    kcm_ccache c = KCMCACHE(id);
    krb5_creds *tmp;

    KCM_ASSERT_VALID(c);

    ret = kcm_ccache_store_cred_internal(context, c, creds, 1, &tmp);

    return ret;
}

static krb5_error_code
kcmss_retrieve(krb5_context context,
	       krb5_ccache id,
	       krb5_flags which,
	       const krb5_creds *mcred,
	       krb5_creds *creds)
{
    krb5_error_code ret;
    kcm_ccache c = KCMCACHE(id);
    krb5_creds *credp;

    KCM_ASSERT_VALID(c);

    ret = kcm_ccache_retrieve_cred_internal(context, c, which,
					    mcred, &credp);
    if (ret)
	return ret;

    ret = krb5_copy_creds_contents(context, credp, creds);
    if (ret)
	return ret;

    return 0;
}

static krb5_error_code
kcmss_get_principal(krb5_context context,
		    krb5_ccache id,
		    krb5_principal *principal)
{
    krb5_error_code ret;
    kcm_ccache c = KCMCACHE(id);

    KCM_ASSERT_VALID(c);

    ret = krb5_copy_principal(context, c->client,
			      principal);

    return ret;
}

static krb5_error_code
kcmss_get_first (krb5_context context,
		 krb5_ccache id,
		 krb5_cc_cursor *cursor)
{
    kcm_ccache c = KCMCACHE(id);

    KCM_ASSERT_VALID(c);

    *cursor = c->creds;

    return (*cursor == NULL) ? KRB5_CC_END : 0;
}

static krb5_error_code
kcmss_get_next (krb5_context context,
		krb5_ccache id,
		krb5_cc_cursor *cursor,
		krb5_creds *creds)
{
    krb5_error_code ret;
    kcm_ccache c = KCMCACHE(id);

    KCM_ASSERT_VALID(c);

    ret = krb5_copy_creds_contents(context,
				   &((struct kcm_creds *)cursor)->cred,
				   creds);
    if (ret)
	return ret;

    *cursor = ((struct kcm_creds *)cursor)->next;
    if (*cursor == 0)
	ret = KRB5_CC_END;

    return ret;
}

static krb5_error_code
kcmss_end_get (krb5_context context,
	       krb5_ccache id,
	       krb5_cc_cursor *cursor)
{
    *cursor = NULL;
    return 0;
}

static krb5_error_code
kcmss_remove_cred(krb5_context context,
		  krb5_ccache id,
		  krb5_flags which,
		  krb5_creds *cred)
{
    krb5_error_code ret;
    kcm_ccache c = KCMCACHE(id);

    KCM_ASSERT_VALID(c);

    ret = kcm_ccache_remove_cred_internal(context, c, which, cred);

    return ret;
}

static krb5_error_code
kcmss_set_flags(krb5_context context,
		krb5_ccache id,
		krb5_flags flags)
{
    return 0;
}

static krb5_error_code
kcmss_get_version(krb5_context context,
		  krb5_ccache id)
{
    return 0;
}

static const krb5_cc_ops krb5_kcmss_ops = {
    KRB5_CC_OPS_VERSION,
    "KCM",
    kcmss_get_name,
    kcmss_resolve,
    kcmss_gen_new,
    kcmss_initialize,
    kcmss_destroy,
    kcmss_close,
    kcmss_store_cred,
    kcmss_retrieve,
    kcmss_get_principal,
    kcmss_get_first,
    kcmss_get_next,
    kcmss_end_get,
    kcmss_remove_cred,
    kcmss_set_flags,
    kcmss_get_version
};

krb5_error_code
kcm_internal_ccache(krb5_context context,
		    kcm_ccache c,
		    krb5_ccache id)
{
    id->ops = &krb5_kcmss_ops;
    id->data.length = sizeof(*c);
    id->data.data = c;

    return 0;
}

