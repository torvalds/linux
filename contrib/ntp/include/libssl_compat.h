/*
 * libssl_compat.h -- OpenSSL v1.1 compatibility shims
 *
 * ---------------------------------------------------------------------
 *
 * Written by Juergen Perlinger <perlinger@ntp.org> for the NTP project
 *
 * Based on an idea by Kurt Roeckx <kurt@roeckx.be>
 *
 * ---------------------------------------------------------------------
 * This is a clean room implementation of shim functions that have
 * counterparts in the OpenSSL v1.1 API but not in earlier versions.
 *
 * If the OpenSSL version used for compilation needs the shims (that is,
 * does not provide the new functions) the names of these functions are
 * redirected to our shims.
 * ---------------------------------------------------------------------
 */

#ifndef NTP_LIBSSL_COMPAT_H
#define NTP_LIBSSL_COMPAT_H

#include "openssl/evp.h"
#include "openssl/dsa.h"
#include "openssl/rsa.h"

#ifndef OPENSSL_VERSION_NUMBER
#define OPENSSL_VERSION_NUMBER SSLEAY_VERSION_NUMBER
#endif

#ifndef OPENSSL_VERSION_TEXT
#define OPENSSL_VERSION_TEXT SSLEAY_VERSION_TEXT
#endif

#ifndef OPENSSL_VERSION
#define OPENSSL_VERSION SSLEAY_VERSION
#endif

/* ----------------------------------------------------------------- */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
/* ----------------------------------------------------------------- */

# include <openssl/objects.h>
# include <openssl/x509.h>

/* shim the new-style API on an old-style OpenSSL */

extern BN_GENCB*	sslshimBN_GENCB_new(void);
extern void		sslshimBN_GENCB_free(BN_GENCB*);

extern EVP_MD_CTX*	sslshim_EVP_MD_CTX_new(void);
extern void		sslshim_EVP_MD_CTX_free(EVP_MD_CTX *ctx);

extern int	sslshim_EVP_PKEY_id(const EVP_PKEY * pkey);
extern int	sslshim_EVP_PKEY_base_id(const EVP_PKEY * pkey);
extern RSA*	sslshim_EVP_PKEY_get0_RSA(EVP_PKEY * pkey);
extern DSA*	sslshim_EVP_PKEY_get0_DSA(EVP_PKEY * pkey);

extern void	sslshim_RSA_get0_key(const RSA *prsa, const BIGNUM **pn,
				     const BIGNUM **pe, const BIGNUM **pd);
extern int	sslshim_RSA_set0_key(RSA *prsa, BIGNUM *n,
				     BIGNUM *e, BIGNUM *d);
extern void	sslshim_RSA_get0_factors(const RSA *prsa, const BIGNUM **pp,
					 const BIGNUM **pq);
extern int 	sslshim_RSA_set0_factors(RSA *prsar, BIGNUM *p, BIGNUM *q);
extern int	sslshim_RSA_set0_crt_params(RSA *prsa, BIGNUM *dmp1,
					BIGNUM *dmq1, BIGNUM *iqmp);

extern void	sslshim_DSA_SIG_get0(const DSA_SIG *psig, const BIGNUM **pr,
				     const BIGNUM **ps);
extern int	sslshim_DSA_SIG_set0(DSA_SIG *psig, BIGNUM *r, BIGNUM *s);
extern void	sslshim_DSA_get0_pqg(const DSA *pdsa, const BIGNUM **pp,
				 const BIGNUM **pq, const BIGNUM **pg);
extern int	sslshim_DSA_set0_pqg(DSA *pdsa, BIGNUM *p, BIGNUM *q, BIGNUM *g);
extern void	sslshim_DSA_get0_key(const DSA *pdsa, const BIGNUM **ppub_key,
				 const BIGNUM **ppriv_key);
extern int	sslshim_DSA_set0_key(DSA *pdsa, BIGNUM *pub_key,
				     BIGNUM *priv_key);

extern int	sslshim_X509_get_signature_nid(const X509 *x);

#define	BN_GENCB_new		sslshimBN_GENCB_new
#define	BN_GENCB_free		sslshimBN_GENCB_free

#define EVP_MD_CTX_new		sslshim_EVP_MD_CTX_new
#define EVP_MD_CTX_free		sslshim_EVP_MD_CTX_free

#define EVP_PKEY_id		sslshim_EVP_PKEY_id
#define EVP_PKEY_base_id	sslshim_EVP_PKEY_base_id
#define EVP_PKEY_get0_RSA	sslshim_EVP_PKEY_get0_RSA
#define EVP_PKEY_get0_DSA	sslshim_EVP_PKEY_get0_DSA

#define RSA_get0_key		sslshim_RSA_get0_key
#define RSA_set0_key		sslshim_RSA_set0_key
#define RSA_get0_factors	sslshim_RSA_get0_factors
#define RSA_set0_factors	sslshim_RSA_set0_factors
#define RSA_set0_crt_params	sslshim_RSA_set0_crt_params

#define DSA_SIG_get0		sslshim_DSA_SIG_get0
#define DSA_SIG_set0		sslshim_DSA_SIG_set0
#define DSA_get0_pqg		sslshim_DSA_get0_pqg
#define DSA_set0_pqg		sslshim_DSA_set0_pqg
#define DSA_get0_key		sslshim_DSA_get0_key
#define DSA_set0_key		sslshim_DSA_set0_key

#define X509_get_signature_nid	sslshim_X509_get_signature_nid

#define OpenSSL_version_num	SSLeay
#define OpenSSL_version		SSLeay_version
#define X509_get0_notBefore	X509_get_notBefore
#define X509_getm_notBefore	X509_get_notBefore
#define X509_get0_notAfter	X509_get_notAfter
#define X509_getm_notAfter	X509_get_notAfter

/* ----------------------------------------------------------------- */
#endif /* OPENSSL_VERSION_NUMBER < v1.1.0 */
/* ----------------------------------------------------------------- */

#endif /* NTP_LIBSSL_COMPAT_H */
