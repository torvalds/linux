/* $OpenBSD: dsa_lib.c,v 1.29 2018/04/14 07:09:21 tb Exp $ */
/* $OpenBSD: rsa_lib.c,v 1.37 2018/04/14 07:09:21 tb Exp $ */
/* $OpenBSD: evp_lib.c,v 1.17 2018/09/12 06:35:38 djm Exp $ */
/* $OpenBSD: dh_lib.c,v 1.32 2018/05/02 15:48:38 tb Exp $ */
/* $OpenBSD: p_lib.c,v 1.24 2018/05/30 15:40:50 tb Exp $ */
/* $OpenBSD: digest.c,v 1.30 2018/04/14 07:09:21 tb Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 * 
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 * 
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from 
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 * 
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

/* $OpenBSD: dsa_asn1.c,v 1.22 2018/06/14 17:03:19 jsing Exp $ */
/* $OpenBSD: ecs_asn1.c,v 1.9 2018/03/17 15:24:44 tb Exp $ */
/* $OpenBSD: digest.c,v 1.30 2018/04/14 07:09:21 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2000.
 */
/* ====================================================================
 * Copyright (c) 2000-2005 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

/*	$OpenBSD: rsa_meth.c,v 1.2 2018/09/12 06:35:38 djm Exp $	*/
/*
 * Copyright (c) 2018 Theo Buehler <tb@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "includes.h"

#ifdef WITH_OPENSSL

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include <openssl/err.h>
#include <openssl/bn.h>
#include <openssl/dsa.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/ecdsa.h>
#include <openssl/dh.h>

#ifndef HAVE_DSA_GET0_PQG
void
DSA_get0_pqg(const DSA *d, const BIGNUM **p, const BIGNUM **q, const BIGNUM **g)
{
	if (p != NULL)
		*p = d->p;
	if (q != NULL)
		*q = d->q;
	if (g != NULL)
		*g = d->g;
}
#endif /* HAVE_DSA_GET0_PQG */

#ifndef HAVE_DSA_SET0_PQG
int
DSA_set0_pqg(DSA *d, BIGNUM *p, BIGNUM *q, BIGNUM *g)
{
	if ((d->p == NULL && p == NULL) || (d->q == NULL && q == NULL) ||
	    (d->g == NULL && g == NULL))
		return 0;

	if (p != NULL) {
		BN_free(d->p);
		d->p = p;
	}
	if (q != NULL) {
		BN_free(d->q);
		d->q = q;
	}
	if (g != NULL) {
		BN_free(d->g);
		d->g = g;
	}

	return 1;
}
#endif /* HAVE_DSA_SET0_PQG */

#ifndef HAVE_DSA_GET0_KEY
void
DSA_get0_key(const DSA *d, const BIGNUM **pub_key, const BIGNUM **priv_key)
{
	if (pub_key != NULL)
		*pub_key = d->pub_key;
	if (priv_key != NULL)
		*priv_key = d->priv_key;
}
#endif /* HAVE_DSA_GET0_KEY */

#ifndef HAVE_DSA_SET0_KEY
int
DSA_set0_key(DSA *d, BIGNUM *pub_key, BIGNUM *priv_key)
{
	if (d->pub_key == NULL && pub_key == NULL)
		return 0;

	if (pub_key != NULL) {
		BN_free(d->pub_key);
		d->pub_key = pub_key;
	}
	if (priv_key != NULL) {
		BN_free(d->priv_key);
		d->priv_key = priv_key;
	}

	return 1;
}
#endif /* HAVE_DSA_SET0_KEY */

#ifndef HAVE_RSA_GET0_KEY
void
RSA_get0_key(const RSA *r, const BIGNUM **n, const BIGNUM **e, const BIGNUM **d)
{
	if (n != NULL)
		*n = r->n;
	if (e != NULL)
		*e = r->e;
	if (d != NULL)
		*d = r->d;
}
#endif /* HAVE_RSA_GET0_KEY */

#ifndef HAVE_RSA_SET0_KEY
int
RSA_set0_key(RSA *r, BIGNUM *n, BIGNUM *e, BIGNUM *d)
{
	if ((r->n == NULL && n == NULL) || (r->e == NULL && e == NULL))
		return 0;

	if (n != NULL) {
		BN_free(r->n);
		r->n = n;
	}
	if (e != NULL) {
		BN_free(r->e);
		r->e = e;
	}
	if (d != NULL) {
		BN_free(r->d);
		r->d = d;
	}

	return 1;
}
#endif /* HAVE_RSA_SET0_KEY */

#ifndef HAVE_RSA_GET0_CRT_PARAMS
void
RSA_get0_crt_params(const RSA *r, const BIGNUM **dmp1, const BIGNUM **dmq1,
    const BIGNUM **iqmp)
{
	if (dmp1 != NULL)
		*dmp1 = r->dmp1;
	if (dmq1 != NULL)
		*dmq1 = r->dmq1;
	if (iqmp != NULL)
		*iqmp = r->iqmp;
}
#endif /* HAVE_RSA_GET0_CRT_PARAMS */

#ifndef HAVE_RSA_SET0_CRT_PARAMS
int
RSA_set0_crt_params(RSA *r, BIGNUM *dmp1, BIGNUM *dmq1, BIGNUM *iqmp)
{
	if ((r->dmp1 == NULL && dmp1 == NULL) ||
	    (r->dmq1 == NULL && dmq1 == NULL) ||
	    (r->iqmp == NULL && iqmp == NULL))
	       	return 0;

	if (dmp1 != NULL) {
		BN_free(r->dmp1);
		r->dmp1 = dmp1;
	}
	if (dmq1 != NULL) {
		BN_free(r->dmq1);
		r->dmq1 = dmq1;
	}
	if (iqmp != NULL) {
		BN_free(r->iqmp);
		r->iqmp = iqmp;
	}

	return 1;
}
#endif /* HAVE_RSA_SET0_CRT_PARAMS */

#ifndef HAVE_RSA_GET0_FACTORS
void
RSA_get0_factors(const RSA *r, const BIGNUM **p, const BIGNUM **q)
{
	if (p != NULL)
		*p = r->p;
	if (q != NULL)
		*q = r->q;
}
#endif /* HAVE_RSA_GET0_FACTORS */

#ifndef HAVE_RSA_SET0_FACTORS
int
RSA_set0_factors(RSA *r, BIGNUM *p, BIGNUM *q)
{
	if ((r->p == NULL && p == NULL) || (r->q == NULL && q == NULL))
		return 0;

	if (p != NULL) {
		BN_free(r->p);
		r->p = p;
	}
	if (q != NULL) {
		BN_free(r->q);
		r->q = q;
	}

	return 1;
}
#endif /* HAVE_RSA_SET0_FACTORS */

#ifndef HAVE_EVP_CIPHER_CTX_GET_IV
int
EVP_CIPHER_CTX_get_iv(const EVP_CIPHER_CTX *ctx, unsigned char *iv, size_t len)
{
	if (ctx == NULL)
		return 0;
	if (EVP_CIPHER_CTX_iv_length(ctx) < 0)
		return 0;
	if (len != (size_t)EVP_CIPHER_CTX_iv_length(ctx))
		return 0;
	if (len > EVP_MAX_IV_LENGTH)
		return 0; /* sanity check; shouldn't happen */
	/*
	 * Skip the memcpy entirely when the requested IV length is zero,
	 * since the iv pointer may be NULL or invalid.
	 */
	if (len != 0) {
		if (iv == NULL)
			return 0;
# ifdef HAVE_EVP_CIPHER_CTX_IV
		memcpy(iv, EVP_CIPHER_CTX_iv(ctx), len);
# else
		memcpy(iv, ctx->iv, len);
# endif /* HAVE_EVP_CIPHER_CTX_IV */
	}
	return 1;
}
#endif /* HAVE_EVP_CIPHER_CTX_GET_IV */

#ifndef HAVE_EVP_CIPHER_CTX_SET_IV
int
EVP_CIPHER_CTX_set_iv(EVP_CIPHER_CTX *ctx, const unsigned char *iv, size_t len)
{
	if (ctx == NULL)
		return 0;
	if (EVP_CIPHER_CTX_iv_length(ctx) < 0)
		return 0;
	if (len != (size_t)EVP_CIPHER_CTX_iv_length(ctx))
		return 0;
	if (len > EVP_MAX_IV_LENGTH)
		return 0; /* sanity check; shouldn't happen */
	/*
	 * Skip the memcpy entirely when the requested IV length is zero,
	 * since the iv pointer may be NULL or invalid.
	 */
	if (len != 0) {
		if (iv == NULL)
			return 0;
# ifdef HAVE_EVP_CIPHER_CTX_IV_NOCONST
		memcpy(EVP_CIPHER_CTX_iv_noconst(ctx), iv, len);
# else
		memcpy(ctx->iv, iv, len);
# endif /* HAVE_EVP_CIPHER_CTX_IV_NOCONST */
	}
	return 1;
}
#endif /* HAVE_EVP_CIPHER_CTX_SET_IV */

#ifndef HAVE_DSA_SIG_GET0
void
DSA_SIG_get0(const DSA_SIG *sig, const BIGNUM **pr, const BIGNUM **ps)
{
	if (pr != NULL)
		*pr = sig->r;
	if (ps != NULL)
		*ps = sig->s;
}
#endif /* HAVE_DSA_SIG_GET0 */

#ifndef HAVE_DSA_SIG_SET0
int
DSA_SIG_set0(DSA_SIG *sig, BIGNUM *r, BIGNUM *s)
{
	if (r == NULL || s == NULL)
		return 0;

	BN_clear_free(sig->r);
	sig->r = r;
	BN_clear_free(sig->s);
	sig->s = s;

	return 1;
}
#endif /* HAVE_DSA_SIG_SET0 */

#ifndef HAVE_ECDSA_SIG_GET0
void
ECDSA_SIG_get0(const ECDSA_SIG *sig, const BIGNUM **pr, const BIGNUM **ps)
{
	if (pr != NULL)
		*pr = sig->r;
	if (ps != NULL)
		*ps = sig->s;
}
#endif /* HAVE_ECDSA_SIG_GET0 */

#ifndef HAVE_ECDSA_SIG_SET0
int
ECDSA_SIG_set0(ECDSA_SIG *sig, BIGNUM *r, BIGNUM *s)
{
	if (r == NULL || s == NULL)
		return 0;

	BN_clear_free(sig->r);
	BN_clear_free(sig->s);
	sig->r = r;
	sig->s = s;
	return 1;
}
#endif /* HAVE_ECDSA_SIG_SET0 */

#ifndef HAVE_DH_GET0_PQG
void
DH_get0_pqg(const DH *dh, const BIGNUM **p, const BIGNUM **q, const BIGNUM **g)
{
	if (p != NULL)
		*p = dh->p;
	if (q != NULL)
		*q = dh->q;
	if (g != NULL)
		*g = dh->g;
}
#endif /* HAVE_DH_GET0_PQG */

#ifndef HAVE_DH_SET0_PQG
int
DH_set0_pqg(DH *dh, BIGNUM *p, BIGNUM *q, BIGNUM *g)
{
	if ((dh->p == NULL && p == NULL) || (dh->g == NULL && g == NULL))
		return 0;

	if (p != NULL) {
		BN_free(dh->p);
		dh->p = p;
	}
	if (q != NULL) {
		BN_free(dh->q);
		dh->q = q;
	}
	if (g != NULL) {
		BN_free(dh->g);
		dh->g = g;
	}

	return 1;
}
#endif /* HAVE_DH_SET0_PQG */

#ifndef HAVE_DH_GET0_KEY
void
DH_get0_key(const DH *dh, const BIGNUM **pub_key, const BIGNUM **priv_key)
{
	if (pub_key != NULL)
		*pub_key = dh->pub_key;
	if (priv_key != NULL)
		*priv_key = dh->priv_key;
}
#endif /* HAVE_DH_GET0_KEY */

#ifndef HAVE_DH_SET0_KEY
int
DH_set0_key(DH *dh, BIGNUM *pub_key, BIGNUM *priv_key)
{
	if (pub_key != NULL) {
		BN_free(dh->pub_key);
		dh->pub_key = pub_key;
	}
	if (priv_key != NULL) {
		BN_free(dh->priv_key);
		dh->priv_key = priv_key;
	}

	return 1;
}
#endif /* HAVE_DH_SET0_KEY */

#ifndef HAVE_DH_SET_LENGTH
int
DH_set_length(DH *dh, long length)
{
	if (length < 0 || length > INT_MAX)
		return 0;

	dh->length = length;
	return 1;
}
#endif /* HAVE_DH_SET_LENGTH */

#ifndef HAVE_RSA_METH_FREE
void
RSA_meth_free(RSA_METHOD *meth)
{
	if (meth != NULL) {
		free((char *)meth->name);
		free(meth);
	}
}
#endif /* HAVE_RSA_METH_FREE */

#ifndef HAVE_RSA_METH_DUP
RSA_METHOD *
RSA_meth_dup(const RSA_METHOD *meth)
{
	RSA_METHOD *copy;

	if ((copy = calloc(1, sizeof(*copy))) == NULL)
		return NULL;
	memcpy(copy, meth, sizeof(*copy));
	if ((copy->name = strdup(meth->name)) == NULL) {
		free(copy);
		return NULL;
	}

	return copy;
}
#endif /* HAVE_RSA_METH_DUP */

#ifndef HAVE_RSA_METH_SET1_NAME
int
RSA_meth_set1_name(RSA_METHOD *meth, const char *name)
{
	char *copy;

	if ((copy = strdup(name)) == NULL)
		return 0;
	free((char *)meth->name);
	meth->name = copy;
	return 1;
}
#endif /* HAVE_RSA_METH_SET1_NAME */

#ifndef HAVE_RSA_METH_GET_FINISH
int
(*RSA_meth_get_finish(const RSA_METHOD *meth))(RSA *rsa)
{
	return meth->finish;
}
#endif /* HAVE_RSA_METH_GET_FINISH */

#ifndef HAVE_RSA_METH_SET_PRIV_ENC
int
RSA_meth_set_priv_enc(RSA_METHOD *meth, int (*priv_enc)(int flen,
    const unsigned char *from, unsigned char *to, RSA *rsa, int padding))
{
	meth->rsa_priv_enc = priv_enc;
	return 1;
}
#endif /* HAVE_RSA_METH_SET_PRIV_ENC */

#ifndef HAVE_RSA_METH_SET_PRIV_DEC
int
RSA_meth_set_priv_dec(RSA_METHOD *meth, int (*priv_dec)(int flen,
    const unsigned char *from, unsigned char *to, RSA *rsa, int padding))
{
	meth->rsa_priv_dec = priv_dec;
	return 1;
}
#endif /* HAVE_RSA_METH_SET_PRIV_DEC */

#ifndef HAVE_RSA_METH_SET_FINISH
int
RSA_meth_set_finish(RSA_METHOD *meth, int (*finish)(RSA *rsa))
{
	meth->finish = finish;
	return 1;
}
#endif /* HAVE_RSA_METH_SET_FINISH */

#ifndef HAVE_EVP_PKEY_GET0_RSA
RSA *
EVP_PKEY_get0_RSA(EVP_PKEY *pkey)
{
	if (pkey->type != EVP_PKEY_RSA) {
		/* EVPerror(EVP_R_EXPECTING_AN_RSA_KEY); */
		return NULL;
	}
	return pkey->pkey.rsa;
}
#endif /* HAVE_EVP_PKEY_GET0_RSA */

#ifndef HAVE_EVP_MD_CTX_NEW
EVP_MD_CTX *
EVP_MD_CTX_new(void)
{
	return calloc(1, sizeof(EVP_MD_CTX));
}
#endif /* HAVE_EVP_MD_CTX_NEW */

#ifndef HAVE_EVP_MD_CTX_FREE
void
EVP_MD_CTX_free(EVP_MD_CTX *ctx)
{
	if (ctx == NULL)
		return;

	EVP_MD_CTX_cleanup(ctx);

	free(ctx);
}
#endif /* HAVE_EVP_MD_CTX_FREE */

#endif /* WITH_OPENSSL */
