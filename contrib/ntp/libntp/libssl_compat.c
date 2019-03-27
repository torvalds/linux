/*
 * libssl_compat.c -- OpenSSL v1.1 compatibility functions
 *
 * ---------------------------------------------------------------------
 * Written by Juergen Perlinger <perlinger@ntp.org> for the NTP project
 *
 * Based on an idea by Kurt Roeckx <kurt@roeckx.be>
 *
 * ---------------------------------------------------------------------
 * This is a clean room implementation of shim functions that have
 * counterparts in the OpenSSL v1.1 API but not in earlier versions. So
 * while OpenSSL broke binary compatibility with v1.1, this shim module
 * should provide the necessary source code compatibility with older
 * versions of OpenSSL.
 * ---------------------------------------------------------------------
 */
#include "config.h"
#include "ntp_types.h"

/* ----------------------------------------------------------------- */
#ifdef OPENSSL
# include <string.h>
# include <openssl/bn.h>
# include <openssl/evp.h>
#endif
/* ----------------------------------------------------------------- */

/* ----------------------------------------------------------------- */
#if defined(OPENSSL) && OPENSSL_VERSION_NUMBER < 0x10100000L
/* ----------------------------------------------------------------- */

#include "libssl_compat.h"
#include "ntp_assert.h"

/* --------------------------------------------------------------------
 * replace a BIGNUM owned by the caller with another one if it's not
 * NULL, taking over the ownership of the new value. This clears & frees
 * the old value -- the clear might be overkill, but it's better to err
 * on the side of paranoia here.
 */
static void
replace_bn_nn(
	BIGNUM **	ps,
	BIGNUM *	n
	)
{
	if (n) {
		REQUIRE(*ps != n);
		BN_clear_free(*ps);
		*ps = n;
	}
}

/* --------------------------------------------------------------------
 * allocation and deallocation of prime number callbacks
 */
BN_GENCB*
sslshimBN_GENCB_new(void)
{
	return calloc(1,sizeof(BN_GENCB));
}

void
sslshimBN_GENCB_free(
	BN_GENCB	*cb
	)
{
	free(cb);
}

/* --------------------------------------------------------------------
 * allocation and deallocation of message digests
 */
EVP_MD_CTX*
sslshim_EVP_MD_CTX_new(void)
{
	EVP_MD_CTX *	ctx;
	if (NULL != (ctx = calloc(1, sizeof(EVP_MD_CTX))))
		EVP_MD_CTX_init(ctx);
	return ctx;
}

void
sslshim_EVP_MD_CTX_free(
	EVP_MD_CTX *	pctx
	)
{
	free(pctx);
}

/* --------------------------------------------------------------------
 * get EVP keys and key type
 */
int
sslshim_EVP_PKEY_id(
	const EVP_PKEY *pkey
	)
{
	return (pkey) ? pkey->type : EVP_PKEY_NONE;
}

int
sslshim_EVP_PKEY_base_id(
	const EVP_PKEY *pkey
	)
{
	return (pkey) ? EVP_PKEY_type(pkey->type) : EVP_PKEY_NONE;
}

RSA*
sslshim_EVP_PKEY_get0_RSA(
	EVP_PKEY *	pkey
	)
{
	return (pkey) ? pkey->pkey.rsa : NULL;
}

DSA*
sslshim_EVP_PKEY_get0_DSA(
	EVP_PKEY *	pkey
	)
{
	return (pkey) ? pkey->pkey.dsa : NULL;
}

/* --------------------------------------------------------------------
 * set/get RSA params
 */
void
sslshim_RSA_get0_key(
	const RSA *	prsa,
	const BIGNUM **	pn,
	const BIGNUM **	pe,
	const BIGNUM **	pd
	)
{
	REQUIRE(prsa != NULL);

	if (pn)
		*pn = prsa->n;
	if (pe)
		*pe = prsa->e;
	if (pd)
		*pd = prsa->d;
}

int
sslshim_RSA_set0_key(
	RSA *		prsa,
	BIGNUM *	n,
	BIGNUM *	e,
	BIGNUM *	d
	)
{
	REQUIRE(prsa != NULL);
	if (!((prsa->n || n) && (prsa->e || e)))
		return 0;

	replace_bn_nn(&prsa->n, n);
	replace_bn_nn(&prsa->e, e);
	replace_bn_nn(&prsa->d, d);
	
	return 1;
}

void
sslshim_RSA_get0_factors(
	const RSA *	prsa,
	const BIGNUM **	pp,
	const BIGNUM **	pq
	)
{
	REQUIRE(prsa != NULL);

	if (pp)
		*pp = prsa->p;
	if (pq)
		*pq = prsa->q;
}

int
sslshim_RSA_set0_factors(
	RSA    *	prsa,
	BIGNUM *	p,
	BIGNUM *	q
	)
{
	REQUIRE(prsa != NULL);
	if (!((prsa->p || p) && (prsa->q || q)))
		return 0;

	replace_bn_nn(&prsa->p, p);
	replace_bn_nn(&prsa->q, q);

	return 1;
}

int
sslshim_RSA_set0_crt_params(
	RSA    *	prsa,
	BIGNUM *	dmp1,
	BIGNUM *	dmq1,
	BIGNUM *	iqmp
	)
{
	REQUIRE(prsa != NULL);
	if (!((prsa->dmp1 || dmp1) &&
	      (prsa->dmq1 || dmq1) &&
	      (prsa->iqmp || iqmp) ))
		return 0;

	replace_bn_nn(&prsa->dmp1, dmp1);
	replace_bn_nn(&prsa->dmq1, dmq1);
	replace_bn_nn(&prsa->iqmp, iqmp);
	
	return 1;
}

/* --------------------------------------------------------------------
 * set/get DSA signature parameters
 */
void
sslshim_DSA_SIG_get0(
	const DSA_SIG *	psig,
	const BIGNUM **	pr,
	const BIGNUM **	ps
	)
{
	REQUIRE(psig != NULL);

	if (pr != NULL)
		*pr = psig->r;
	if (ps != NULL)
		*ps = psig->s;
}

int
sslshim_DSA_SIG_set0(
	DSA_SIG *	psig,
	BIGNUM *	r,
	BIGNUM *	s
	)
{
	REQUIRE(psig != NULL);
	if (!(r && s))
		return 0;

	replace_bn_nn(&psig->r, r);
	replace_bn_nn(&psig->s, s);
	
	return 1;
}

/* --------------------------------------------------------------------
 * get/set DSA parameters
 */
void
sslshim_DSA_get0_pqg(
	const DSA *	pdsa,
	const BIGNUM **	pp,
	const BIGNUM **	pq,
	const BIGNUM **	pg
	)
{
	REQUIRE(pdsa != NULL);

	if (pp != NULL)
		*pp = pdsa->p;
	if (pq != NULL)
		*pq = pdsa->q;
	if (pg != NULL)
		*pg = pdsa->g;
}

int
sslshim_DSA_set0_pqg(
	DSA *		pdsa,
	BIGNUM *	p,
	BIGNUM *	q,
	BIGNUM *	g
	)
{
	if (!((pdsa->p || p) && (pdsa->q || q) && (pdsa->g || g)))
		return 0;

	replace_bn_nn(&pdsa->p, p);
	replace_bn_nn(&pdsa->q, q);
	replace_bn_nn(&pdsa->g, g);

	return 1;
}

void
sslshim_DSA_get0_key(
	const DSA *	pdsa,
	const BIGNUM **	ppub_key,
	const BIGNUM **	ppriv_key
	)
{
	REQUIRE(pdsa != NULL);

	if (ppub_key != NULL)
		*ppub_key = pdsa->pub_key;
	if (ppriv_key != NULL)
		*ppriv_key = pdsa->priv_key;
}

int
sslshim_DSA_set0_key(
	DSA *		pdsa,
	BIGNUM *	pub_key,
	BIGNUM *	priv_key
	)
{
	REQUIRE(pdsa != NULL);
	if (!(pdsa->pub_key || pub_key))
		return 0;

	replace_bn_nn(&pdsa->pub_key, pub_key);
	replace_bn_nn(&pdsa->priv_key, priv_key);

	return 1;
}

int
sslshim_X509_get_signature_nid(
	const X509 *x
	)
{
	return OBJ_obj2nid(x->sig_alg->algorithm);
}

/* ----------------------------------------------------------------- */
#else /* OPENSSL && OPENSSL_VERSION_NUMBER >= v1.1.0 */
/* ----------------------------------------------------------------- */

NONEMPTY_TRANSLATION_UNIT

/* ----------------------------------------------------------------- */
#endif
/* ----------------------------------------------------------------- */
