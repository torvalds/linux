/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_DH_H
# define HEADER_DH_H

# include <openssl/opensslconf.h>

# ifndef OPENSSL_NO_DH
# include <openssl/e_os2.h>
# include <openssl/bio.h>
# include <openssl/asn1.h>
# include <openssl/ossl_typ.h>
# if OPENSSL_API_COMPAT < 0x10100000L
#  include <openssl/bn.h>
# endif
# include <openssl/dherr.h>

# ifdef  __cplusplus
extern "C" {
# endif

# ifndef OPENSSL_DH_MAX_MODULUS_BITS
#  define OPENSSL_DH_MAX_MODULUS_BITS    10000
# endif

# define OPENSSL_DH_FIPS_MIN_MODULUS_BITS 1024

# define DH_FLAG_CACHE_MONT_P     0x01

# if OPENSSL_API_COMPAT < 0x10100000L
/*
 * Does nothing. Previously this switched off constant time behaviour.
 */
#  define DH_FLAG_NO_EXP_CONSTTIME 0x00
# endif

/*
 * If this flag is set the DH method is FIPS compliant and can be used in
 * FIPS mode. This is set in the validated module method. If an application
 * sets this flag in its own methods it is its responsibility to ensure the
 * result is compliant.
 */

# define DH_FLAG_FIPS_METHOD                     0x0400

/*
 * If this flag is set the operations normally disabled in FIPS mode are
 * permitted it is then the applications responsibility to ensure that the
 * usage is compliant.
 */

# define DH_FLAG_NON_FIPS_ALLOW                  0x0400

/* Already defined in ossl_typ.h */
/* typedef struct dh_st DH; */
/* typedef struct dh_method DH_METHOD; */

DECLARE_ASN1_ITEM(DHparams)

# define DH_GENERATOR_2          2
/* #define DH_GENERATOR_3       3 */
# define DH_GENERATOR_5          5

/* DH_check error codes */
# define DH_CHECK_P_NOT_PRIME            0x01
# define DH_CHECK_P_NOT_SAFE_PRIME       0x02
# define DH_UNABLE_TO_CHECK_GENERATOR    0x04
# define DH_NOT_SUITABLE_GENERATOR       0x08
# define DH_CHECK_Q_NOT_PRIME            0x10
# define DH_CHECK_INVALID_Q_VALUE        0x20
# define DH_CHECK_INVALID_J_VALUE        0x40

/* DH_check_pub_key error codes */
# define DH_CHECK_PUBKEY_TOO_SMALL       0x01
# define DH_CHECK_PUBKEY_TOO_LARGE       0x02
# define DH_CHECK_PUBKEY_INVALID         0x04

/*
 * primes p where (p-1)/2 is prime too are called "safe"; we define this for
 * backward compatibility:
 */
# define DH_CHECK_P_NOT_STRONG_PRIME     DH_CHECK_P_NOT_SAFE_PRIME

# define d2i_DHparams_fp(fp,x) \
    (DH *)ASN1_d2i_fp((char *(*)())DH_new, \
                      (char *(*)())d2i_DHparams, \
                      (fp), \
                      (unsigned char **)(x))
# define i2d_DHparams_fp(fp,x) \
    ASN1_i2d_fp(i2d_DHparams,(fp), (unsigned char *)(x))
# define d2i_DHparams_bio(bp,x) \
    ASN1_d2i_bio_of(DH, DH_new, d2i_DHparams, bp, x)
# define i2d_DHparams_bio(bp,x) \
    ASN1_i2d_bio_of_const(DH,i2d_DHparams,bp,x)

# define d2i_DHxparams_fp(fp,x) \
    (DH *)ASN1_d2i_fp((char *(*)())DH_new, \
                      (char *(*)())d2i_DHxparams, \
                      (fp), \
                      (unsigned char **)(x))
# define i2d_DHxparams_fp(fp,x) \
    ASN1_i2d_fp(i2d_DHxparams,(fp), (unsigned char *)(x))
# define d2i_DHxparams_bio(bp,x) \
    ASN1_d2i_bio_of(DH, DH_new, d2i_DHxparams, bp, x)
# define i2d_DHxparams_bio(bp,x) \
    ASN1_i2d_bio_of_const(DH, i2d_DHxparams, bp, x)

DH *DHparams_dup(DH *);

const DH_METHOD *DH_OpenSSL(void);

void DH_set_default_method(const DH_METHOD *meth);
const DH_METHOD *DH_get_default_method(void);
int DH_set_method(DH *dh, const DH_METHOD *meth);
DH *DH_new_method(ENGINE *engine);

DH *DH_new(void);
void DH_free(DH *dh);
int DH_up_ref(DH *dh);
int DH_bits(const DH *dh);
int DH_size(const DH *dh);
int DH_security_bits(const DH *dh);
#define DH_get_ex_new_index(l, p, newf, dupf, freef) \
    CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_DH, l, p, newf, dupf, freef)
int DH_set_ex_data(DH *d, int idx, void *arg);
void *DH_get_ex_data(DH *d, int idx);

/* Deprecated version */
DEPRECATEDIN_0_9_8(DH *DH_generate_parameters(int prime_len, int generator,
                                              void (*callback) (int, int,
                                                                void *),
                                              void *cb_arg))

/* New version */
int DH_generate_parameters_ex(DH *dh, int prime_len, int generator,
                              BN_GENCB *cb);

int DH_check_params_ex(const DH *dh);
int DH_check_ex(const DH *dh);
int DH_check_pub_key_ex(const DH *dh, const BIGNUM *pub_key);
int DH_check_params(const DH *dh, int *ret);
int DH_check(const DH *dh, int *codes);
int DH_check_pub_key(const DH *dh, const BIGNUM *pub_key, int *codes);
int DH_generate_key(DH *dh);
int DH_compute_key(unsigned char *key, const BIGNUM *pub_key, DH *dh);
int DH_compute_key_padded(unsigned char *key, const BIGNUM *pub_key, DH *dh);
DH *d2i_DHparams(DH **a, const unsigned char **pp, long length);
int i2d_DHparams(const DH *a, unsigned char **pp);
DH *d2i_DHxparams(DH **a, const unsigned char **pp, long length);
int i2d_DHxparams(const DH *a, unsigned char **pp);
# ifndef OPENSSL_NO_STDIO
int DHparams_print_fp(FILE *fp, const DH *x);
# endif
int DHparams_print(BIO *bp, const DH *x);

/* RFC 5114 parameters */
DH *DH_get_1024_160(void);
DH *DH_get_2048_224(void);
DH *DH_get_2048_256(void);

/* Named parameters, currently RFC7919 */
DH *DH_new_by_nid(int nid);
int DH_get_nid(const DH *dh);

# ifndef OPENSSL_NO_CMS
/* RFC2631 KDF */
int DH_KDF_X9_42(unsigned char *out, size_t outlen,
                 const unsigned char *Z, size_t Zlen,
                 ASN1_OBJECT *key_oid,
                 const unsigned char *ukm, size_t ukmlen, const EVP_MD *md);
# endif

void DH_get0_pqg(const DH *dh,
                 const BIGNUM **p, const BIGNUM **q, const BIGNUM **g);
int DH_set0_pqg(DH *dh, BIGNUM *p, BIGNUM *q, BIGNUM *g);
void DH_get0_key(const DH *dh,
                 const BIGNUM **pub_key, const BIGNUM **priv_key);
int DH_set0_key(DH *dh, BIGNUM *pub_key, BIGNUM *priv_key);
const BIGNUM *DH_get0_p(const DH *dh);
const BIGNUM *DH_get0_q(const DH *dh);
const BIGNUM *DH_get0_g(const DH *dh);
const BIGNUM *DH_get0_priv_key(const DH *dh);
const BIGNUM *DH_get0_pub_key(const DH *dh);
void DH_clear_flags(DH *dh, int flags);
int DH_test_flags(const DH *dh, int flags);
void DH_set_flags(DH *dh, int flags);
ENGINE *DH_get0_engine(DH *d);
long DH_get_length(const DH *dh);
int DH_set_length(DH *dh, long length);

DH_METHOD *DH_meth_new(const char *name, int flags);
void DH_meth_free(DH_METHOD *dhm);
DH_METHOD *DH_meth_dup(const DH_METHOD *dhm);
const char *DH_meth_get0_name(const DH_METHOD *dhm);
int DH_meth_set1_name(DH_METHOD *dhm, const char *name);
int DH_meth_get_flags(const DH_METHOD *dhm);
int DH_meth_set_flags(DH_METHOD *dhm, int flags);
void *DH_meth_get0_app_data(const DH_METHOD *dhm);
int DH_meth_set0_app_data(DH_METHOD *dhm, void *app_data);
int (*DH_meth_get_generate_key(const DH_METHOD *dhm)) (DH *);
int DH_meth_set_generate_key(DH_METHOD *dhm, int (*generate_key) (DH *));
int (*DH_meth_get_compute_key(const DH_METHOD *dhm))
        (unsigned char *key, const BIGNUM *pub_key, DH *dh);
int DH_meth_set_compute_key(DH_METHOD *dhm,
        int (*compute_key) (unsigned char *key, const BIGNUM *pub_key, DH *dh));
int (*DH_meth_get_bn_mod_exp(const DH_METHOD *dhm))
    (const DH *, BIGNUM *, const BIGNUM *, const BIGNUM *, const BIGNUM *,
     BN_CTX *, BN_MONT_CTX *);
int DH_meth_set_bn_mod_exp(DH_METHOD *dhm,
    int (*bn_mod_exp) (const DH *, BIGNUM *, const BIGNUM *, const BIGNUM *,
                       const BIGNUM *, BN_CTX *, BN_MONT_CTX *));
int (*DH_meth_get_init(const DH_METHOD *dhm))(DH *);
int DH_meth_set_init(DH_METHOD *dhm, int (*init)(DH *));
int (*DH_meth_get_finish(const DH_METHOD *dhm)) (DH *);
int DH_meth_set_finish(DH_METHOD *dhm, int (*finish) (DH *));
int (*DH_meth_get_generate_params(const DH_METHOD *dhm))
        (DH *, int, int, BN_GENCB *);
int DH_meth_set_generate_params(DH_METHOD *dhm,
        int (*generate_params) (DH *, int, int, BN_GENCB *));


# define EVP_PKEY_CTX_set_dh_paramgen_prime_len(ctx, len) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DH, EVP_PKEY_OP_PARAMGEN, \
                        EVP_PKEY_CTRL_DH_PARAMGEN_PRIME_LEN, len, NULL)

# define EVP_PKEY_CTX_set_dh_paramgen_subprime_len(ctx, len) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DH, EVP_PKEY_OP_PARAMGEN, \
                        EVP_PKEY_CTRL_DH_PARAMGEN_SUBPRIME_LEN, len, NULL)

# define EVP_PKEY_CTX_set_dh_paramgen_type(ctx, typ) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DH, EVP_PKEY_OP_PARAMGEN, \
                        EVP_PKEY_CTRL_DH_PARAMGEN_TYPE, typ, NULL)

# define EVP_PKEY_CTX_set_dh_paramgen_generator(ctx, gen) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DH, EVP_PKEY_OP_PARAMGEN, \
                        EVP_PKEY_CTRL_DH_PARAMGEN_GENERATOR, gen, NULL)

# define EVP_PKEY_CTX_set_dh_rfc5114(ctx, gen) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DHX, EVP_PKEY_OP_PARAMGEN, \
                        EVP_PKEY_CTRL_DH_RFC5114, gen, NULL)

# define EVP_PKEY_CTX_set_dhx_rfc5114(ctx, gen) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DHX, EVP_PKEY_OP_PARAMGEN, \
                        EVP_PKEY_CTRL_DH_RFC5114, gen, NULL)

# define EVP_PKEY_CTX_set_dh_nid(ctx, nid) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DH, \
                        EVP_PKEY_OP_PARAMGEN | EVP_PKEY_OP_KEYGEN, \
                        EVP_PKEY_CTRL_DH_NID, nid, NULL)

# define EVP_PKEY_CTX_set_dh_pad(ctx, pad) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DH, EVP_PKEY_OP_DERIVE, \
                          EVP_PKEY_CTRL_DH_PAD, pad, NULL)

# define EVP_PKEY_CTX_set_dh_kdf_type(ctx, kdf) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DHX, \
                                EVP_PKEY_OP_DERIVE, \
                                EVP_PKEY_CTRL_DH_KDF_TYPE, kdf, NULL)

# define EVP_PKEY_CTX_get_dh_kdf_type(ctx) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DHX, \
                                EVP_PKEY_OP_DERIVE, \
                                EVP_PKEY_CTRL_DH_KDF_TYPE, -2, NULL)

# define EVP_PKEY_CTX_set0_dh_kdf_oid(ctx, oid) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DHX, \
                                EVP_PKEY_OP_DERIVE, \
                                EVP_PKEY_CTRL_DH_KDF_OID, 0, (void *)(oid))

# define EVP_PKEY_CTX_get0_dh_kdf_oid(ctx, poid) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DHX, \
                                EVP_PKEY_OP_DERIVE, \
                                EVP_PKEY_CTRL_GET_DH_KDF_OID, 0, (void *)(poid))

# define EVP_PKEY_CTX_set_dh_kdf_md(ctx, md) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DHX, \
                                EVP_PKEY_OP_DERIVE, \
                                EVP_PKEY_CTRL_DH_KDF_MD, 0, (void *)(md))

# define EVP_PKEY_CTX_get_dh_kdf_md(ctx, pmd) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DHX, \
                                EVP_PKEY_OP_DERIVE, \
                                EVP_PKEY_CTRL_GET_DH_KDF_MD, 0, (void *)(pmd))

# define EVP_PKEY_CTX_set_dh_kdf_outlen(ctx, len) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DHX, \
                                EVP_PKEY_OP_DERIVE, \
                                EVP_PKEY_CTRL_DH_KDF_OUTLEN, len, NULL)

# define EVP_PKEY_CTX_get_dh_kdf_outlen(ctx, plen) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DHX, \
                                EVP_PKEY_OP_DERIVE, \
                        EVP_PKEY_CTRL_GET_DH_KDF_OUTLEN, 0, (void *)(plen))

# define EVP_PKEY_CTX_set0_dh_kdf_ukm(ctx, p, plen) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DHX, \
                                EVP_PKEY_OP_DERIVE, \
                                EVP_PKEY_CTRL_DH_KDF_UKM, plen, (void *)(p))

# define EVP_PKEY_CTX_get0_dh_kdf_ukm(ctx, p) \
        EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DHX, \
                                EVP_PKEY_OP_DERIVE, \
                                EVP_PKEY_CTRL_GET_DH_KDF_UKM, 0, (void *)(p))

# define EVP_PKEY_CTRL_DH_PARAMGEN_PRIME_LEN     (EVP_PKEY_ALG_CTRL + 1)
# define EVP_PKEY_CTRL_DH_PARAMGEN_GENERATOR     (EVP_PKEY_ALG_CTRL + 2)
# define EVP_PKEY_CTRL_DH_RFC5114                (EVP_PKEY_ALG_CTRL + 3)
# define EVP_PKEY_CTRL_DH_PARAMGEN_SUBPRIME_LEN  (EVP_PKEY_ALG_CTRL + 4)
# define EVP_PKEY_CTRL_DH_PARAMGEN_TYPE          (EVP_PKEY_ALG_CTRL + 5)
# define EVP_PKEY_CTRL_DH_KDF_TYPE               (EVP_PKEY_ALG_CTRL + 6)
# define EVP_PKEY_CTRL_DH_KDF_MD                 (EVP_PKEY_ALG_CTRL + 7)
# define EVP_PKEY_CTRL_GET_DH_KDF_MD             (EVP_PKEY_ALG_CTRL + 8)
# define EVP_PKEY_CTRL_DH_KDF_OUTLEN             (EVP_PKEY_ALG_CTRL + 9)
# define EVP_PKEY_CTRL_GET_DH_KDF_OUTLEN         (EVP_PKEY_ALG_CTRL + 10)
# define EVP_PKEY_CTRL_DH_KDF_UKM                (EVP_PKEY_ALG_CTRL + 11)
# define EVP_PKEY_CTRL_GET_DH_KDF_UKM            (EVP_PKEY_ALG_CTRL + 12)
# define EVP_PKEY_CTRL_DH_KDF_OID                (EVP_PKEY_ALG_CTRL + 13)
# define EVP_PKEY_CTRL_GET_DH_KDF_OID            (EVP_PKEY_ALG_CTRL + 14)
# define EVP_PKEY_CTRL_DH_NID                    (EVP_PKEY_ALG_CTRL + 15)
# define EVP_PKEY_CTRL_DH_PAD                    (EVP_PKEY_ALG_CTRL + 16)

/* KDF types */
# define EVP_PKEY_DH_KDF_NONE                            1
# ifndef OPENSSL_NO_CMS
# define EVP_PKEY_DH_KDF_X9_42                           2
# endif


#  ifdef  __cplusplus
}
#  endif
# endif
#endif
