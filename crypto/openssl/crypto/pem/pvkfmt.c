/*
 * Copyright 2005-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * Support for PVK format keys and related structures (such a PUBLICKEYBLOB
 * and PRIVATEKEYBLOB).
 */

#include "internal/cryptlib.h"
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/bn.h>
#if !defined(OPENSSL_NO_RSA) && !defined(OPENSSL_NO_DSA)
# include <openssl/dsa.h>
# include <openssl/rsa.h>

/*
 * Utility function: read a DWORD (4 byte unsigned integer) in little endian
 * format
 */

static unsigned int read_ledword(const unsigned char **in)
{
    const unsigned char *p = *in;
    unsigned int ret;
    ret = *p++;
    ret |= (*p++ << 8);
    ret |= (*p++ << 16);
    ret |= (*p++ << 24);
    *in = p;
    return ret;
}

/*
 * Read a BIGNUM in little endian format. The docs say that this should take
 * up bitlen/8 bytes.
 */

static int read_lebn(const unsigned char **in, unsigned int nbyte, BIGNUM **r)
{
    *r = BN_lebin2bn(*in, nbyte, NULL);
    if (*r == NULL)
        return 0;
    *in += nbyte;
    return 1;
}

/* Convert private key blob to EVP_PKEY: RSA and DSA keys supported */

# define MS_PUBLICKEYBLOB        0x6
# define MS_PRIVATEKEYBLOB       0x7
# define MS_RSA1MAGIC            0x31415352L
# define MS_RSA2MAGIC            0x32415352L
# define MS_DSS1MAGIC            0x31535344L
# define MS_DSS2MAGIC            0x32535344L

# define MS_KEYALG_RSA_KEYX      0xa400
# define MS_KEYALG_DSS_SIGN      0x2200

# define MS_KEYTYPE_KEYX         0x1
# define MS_KEYTYPE_SIGN         0x2

/* Maximum length of a blob after header */
# define BLOB_MAX_LENGTH          102400

/* The PVK file magic number: seems to spell out "bobsfile", who is Bob? */
# define MS_PVKMAGIC             0xb0b5f11eL
/* Salt length for PVK files */
# define PVK_SALTLEN             0x10
/* Maximum length in PVK header */
# define PVK_MAX_KEYLEN          102400
/* Maximum salt length */
# define PVK_MAX_SALTLEN         10240

static EVP_PKEY *b2i_rsa(const unsigned char **in,
                         unsigned int bitlen, int ispub);
static EVP_PKEY *b2i_dss(const unsigned char **in,
                         unsigned int bitlen, int ispub);

static int do_blob_header(const unsigned char **in, unsigned int length,
                          unsigned int *pmagic, unsigned int *pbitlen,
                          int *pisdss, int *pispub)
{
    const unsigned char *p = *in;
    if (length < 16)
        return 0;
    /* bType */
    if (*p == MS_PUBLICKEYBLOB) {
        if (*pispub == 0) {
            PEMerr(PEM_F_DO_BLOB_HEADER, PEM_R_EXPECTING_PRIVATE_KEY_BLOB);
            return 0;
        }
        *pispub = 1;
    } else if (*p == MS_PRIVATEKEYBLOB) {
        if (*pispub == 1) {
            PEMerr(PEM_F_DO_BLOB_HEADER, PEM_R_EXPECTING_PUBLIC_KEY_BLOB);
            return 0;
        }
        *pispub = 0;
    } else
        return 0;
    p++;
    /* Version */
    if (*p++ != 0x2) {
        PEMerr(PEM_F_DO_BLOB_HEADER, PEM_R_BAD_VERSION_NUMBER);
        return 0;
    }
    /* Ignore reserved, aiKeyAlg */
    p += 6;
    *pmagic = read_ledword(&p);
    *pbitlen = read_ledword(&p);
    *pisdss = 0;
    switch (*pmagic) {

    case MS_DSS1MAGIC:
        *pisdss = 1;
        /* fall thru */
    case MS_RSA1MAGIC:
        if (*pispub == 0) {
            PEMerr(PEM_F_DO_BLOB_HEADER, PEM_R_EXPECTING_PRIVATE_KEY_BLOB);
            return 0;
        }
        break;

    case MS_DSS2MAGIC:
        *pisdss = 1;
        /* fall thru */
    case MS_RSA2MAGIC:
        if (*pispub == 1) {
            PEMerr(PEM_F_DO_BLOB_HEADER, PEM_R_EXPECTING_PUBLIC_KEY_BLOB);
            return 0;
        }
        break;

    default:
        PEMerr(PEM_F_DO_BLOB_HEADER, PEM_R_BAD_MAGIC_NUMBER);
        return -1;
    }
    *in = p;
    return 1;
}

static unsigned int blob_length(unsigned bitlen, int isdss, int ispub)
{
    unsigned int nbyte, hnbyte;
    nbyte = (bitlen + 7) >> 3;
    hnbyte = (bitlen + 15) >> 4;
    if (isdss) {

        /*
         * Expected length: 20 for q + 3 components bitlen each + 24 for seed
         * structure.
         */
        if (ispub)
            return 44 + 3 * nbyte;
        /*
         * Expected length: 20 for q, priv, 2 bitlen components + 24 for seed
         * structure.
         */
        else
            return 64 + 2 * nbyte;
    } else {
        /* Expected length: 4 for 'e' + 'n' */
        if (ispub)
            return 4 + nbyte;
        else
            /*
             * Expected length: 4 for 'e' and 7 other components. 2
             * components are bitlen size, 5 are bitlen/2
             */
            return 4 + 2 * nbyte + 5 * hnbyte;
    }

}

static EVP_PKEY *do_b2i(const unsigned char **in, unsigned int length,
                        int ispub)
{
    const unsigned char *p = *in;
    unsigned int bitlen, magic;
    int isdss;
    if (do_blob_header(&p, length, &magic, &bitlen, &isdss, &ispub) <= 0) {
        PEMerr(PEM_F_DO_B2I, PEM_R_KEYBLOB_HEADER_PARSE_ERROR);
        return NULL;
    }
    length -= 16;
    if (length < blob_length(bitlen, isdss, ispub)) {
        PEMerr(PEM_F_DO_B2I, PEM_R_KEYBLOB_TOO_SHORT);
        return NULL;
    }
    if (isdss)
        return b2i_dss(&p, bitlen, ispub);
    else
        return b2i_rsa(&p, bitlen, ispub);
}

static EVP_PKEY *do_b2i_bio(BIO *in, int ispub)
{
    const unsigned char *p;
    unsigned char hdr_buf[16], *buf = NULL;
    unsigned int bitlen, magic, length;
    int isdss;
    EVP_PKEY *ret = NULL;
    if (BIO_read(in, hdr_buf, 16) != 16) {
        PEMerr(PEM_F_DO_B2I_BIO, PEM_R_KEYBLOB_TOO_SHORT);
        return NULL;
    }
    p = hdr_buf;
    if (do_blob_header(&p, 16, &magic, &bitlen, &isdss, &ispub) <= 0)
        return NULL;

    length = blob_length(bitlen, isdss, ispub);
    if (length > BLOB_MAX_LENGTH) {
        PEMerr(PEM_F_DO_B2I_BIO, PEM_R_HEADER_TOO_LONG);
        return NULL;
    }
    buf = OPENSSL_malloc(length);
    if (buf == NULL) {
        PEMerr(PEM_F_DO_B2I_BIO, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    p = buf;
    if (BIO_read(in, buf, length) != (int)length) {
        PEMerr(PEM_F_DO_B2I_BIO, PEM_R_KEYBLOB_TOO_SHORT);
        goto err;
    }

    if (isdss)
        ret = b2i_dss(&p, bitlen, ispub);
    else
        ret = b2i_rsa(&p, bitlen, ispub);

 err:
    OPENSSL_free(buf);
    return ret;
}

static EVP_PKEY *b2i_dss(const unsigned char **in,
                         unsigned int bitlen, int ispub)
{
    const unsigned char *p = *in;
    EVP_PKEY *ret = NULL;
    DSA *dsa = NULL;
    BN_CTX *ctx = NULL;
    unsigned int nbyte;
    BIGNUM *pbn = NULL, *qbn = NULL, *gbn = NULL, *priv_key = NULL;
    BIGNUM *pub_key = NULL;

    nbyte = (bitlen + 7) >> 3;

    dsa = DSA_new();
    ret = EVP_PKEY_new();
    if (dsa == NULL || ret == NULL)
        goto memerr;
    if (!read_lebn(&p, nbyte, &pbn))
        goto memerr;

    if (!read_lebn(&p, 20, &qbn))
        goto memerr;

    if (!read_lebn(&p, nbyte, &gbn))
        goto memerr;

    if (ispub) {
        if (!read_lebn(&p, nbyte, &pub_key))
            goto memerr;
    } else {
        if (!read_lebn(&p, 20, &priv_key))
            goto memerr;

        /* Calculate public key */
        pub_key = BN_new();
        if (pub_key == NULL)
            goto memerr;
        if ((ctx = BN_CTX_new()) == NULL)
            goto memerr;

        if (!BN_mod_exp(pub_key, gbn, priv_key, pbn, ctx))
            goto memerr;

        BN_CTX_free(ctx);
        ctx = NULL;
    }
    if (!DSA_set0_pqg(dsa, pbn, qbn, gbn))
        goto memerr;
    pbn = qbn = gbn = NULL;
    if (!DSA_set0_key(dsa, pub_key, priv_key))
        goto memerr;
    pub_key = priv_key = NULL;

    if (!EVP_PKEY_set1_DSA(ret, dsa))
        goto memerr;
    DSA_free(dsa);
    *in = p;
    return ret;

 memerr:
    PEMerr(PEM_F_B2I_DSS, ERR_R_MALLOC_FAILURE);
    DSA_free(dsa);
    BN_free(pbn);
    BN_free(qbn);
    BN_free(gbn);
    BN_free(pub_key);
    BN_free(priv_key);
    EVP_PKEY_free(ret);
    BN_CTX_free(ctx);
    return NULL;
}

static EVP_PKEY *b2i_rsa(const unsigned char **in,
                         unsigned int bitlen, int ispub)
{
    const unsigned char *pin = *in;
    EVP_PKEY *ret = NULL;
    BIGNUM *e = NULL, *n = NULL, *d = NULL;
    BIGNUM *p = NULL, *q = NULL, *dmp1 = NULL, *dmq1 = NULL, *iqmp = NULL;
    RSA *rsa = NULL;
    unsigned int nbyte, hnbyte;
    nbyte = (bitlen + 7) >> 3;
    hnbyte = (bitlen + 15) >> 4;
    rsa = RSA_new();
    ret = EVP_PKEY_new();
    if (rsa == NULL || ret == NULL)
        goto memerr;
    e = BN_new();
    if (e == NULL)
        goto memerr;
    if (!BN_set_word(e, read_ledword(&pin)))
        goto memerr;
    if (!read_lebn(&pin, nbyte, &n))
        goto memerr;
    if (!ispub) {
        if (!read_lebn(&pin, hnbyte, &p))
            goto memerr;
        if (!read_lebn(&pin, hnbyte, &q))
            goto memerr;
        if (!read_lebn(&pin, hnbyte, &dmp1))
            goto memerr;
        if (!read_lebn(&pin, hnbyte, &dmq1))
            goto memerr;
        if (!read_lebn(&pin, hnbyte, &iqmp))
            goto memerr;
        if (!read_lebn(&pin, nbyte, &d))
            goto memerr;
        if (!RSA_set0_factors(rsa, p, q))
            goto memerr;
        p = q = NULL;
        if (!RSA_set0_crt_params(rsa, dmp1, dmq1, iqmp))
            goto memerr;
        dmp1 = dmq1 = iqmp = NULL;
    }
    if (!RSA_set0_key(rsa, n, e, d))
        goto memerr;
    n = e = d = NULL;

    if (!EVP_PKEY_set1_RSA(ret, rsa))
        goto memerr;
    RSA_free(rsa);
    *in = pin;
    return ret;
 memerr:
    PEMerr(PEM_F_B2I_RSA, ERR_R_MALLOC_FAILURE);
    BN_free(e);
    BN_free(n);
    BN_free(p);
    BN_free(q);
    BN_free(dmp1);
    BN_free(dmq1);
    BN_free(iqmp);
    BN_free(d);
    RSA_free(rsa);
    EVP_PKEY_free(ret);
    return NULL;
}

EVP_PKEY *b2i_PrivateKey(const unsigned char **in, long length)
{
    return do_b2i(in, length, 0);
}

EVP_PKEY *b2i_PublicKey(const unsigned char **in, long length)
{
    return do_b2i(in, length, 1);
}

EVP_PKEY *b2i_PrivateKey_bio(BIO *in)
{
    return do_b2i_bio(in, 0);
}

EVP_PKEY *b2i_PublicKey_bio(BIO *in)
{
    return do_b2i_bio(in, 1);
}

static void write_ledword(unsigned char **out, unsigned int dw)
{
    unsigned char *p = *out;
    *p++ = dw & 0xff;
    *p++ = (dw >> 8) & 0xff;
    *p++ = (dw >> 16) & 0xff;
    *p++ = (dw >> 24) & 0xff;
    *out = p;
}

static void write_lebn(unsigned char **out, const BIGNUM *bn, int len)
{
    BN_bn2lebinpad(bn, *out, len);
    *out += len;
}

static int check_bitlen_rsa(RSA *rsa, int ispub, unsigned int *magic);
static int check_bitlen_dsa(DSA *dsa, int ispub, unsigned int *magic);

static void write_rsa(unsigned char **out, RSA *rsa, int ispub);
static void write_dsa(unsigned char **out, DSA *dsa, int ispub);

static int do_i2b(unsigned char **out, EVP_PKEY *pk, int ispub)
{
    unsigned char *p;
    unsigned int bitlen, magic = 0, keyalg;
    int outlen, noinc = 0;
    int pktype = EVP_PKEY_id(pk);
    if (pktype == EVP_PKEY_DSA) {
        bitlen = check_bitlen_dsa(EVP_PKEY_get0_DSA(pk), ispub, &magic);
        keyalg = MS_KEYALG_DSS_SIGN;
    } else if (pktype == EVP_PKEY_RSA) {
        bitlen = check_bitlen_rsa(EVP_PKEY_get0_RSA(pk), ispub, &magic);
        keyalg = MS_KEYALG_RSA_KEYX;
    } else
        return -1;
    if (bitlen == 0)
        return -1;
    outlen = 16 + blob_length(bitlen,
                              keyalg == MS_KEYALG_DSS_SIGN ? 1 : 0, ispub);
    if (out == NULL)
        return outlen;
    if (*out)
        p = *out;
    else {
        if ((p = OPENSSL_malloc(outlen)) == NULL) {
            PEMerr(PEM_F_DO_I2B, ERR_R_MALLOC_FAILURE);
            return -1;
        }
        *out = p;
        noinc = 1;
    }
    if (ispub)
        *p++ = MS_PUBLICKEYBLOB;
    else
        *p++ = MS_PRIVATEKEYBLOB;
    *p++ = 0x2;
    *p++ = 0;
    *p++ = 0;
    write_ledword(&p, keyalg);
    write_ledword(&p, magic);
    write_ledword(&p, bitlen);
    if (keyalg == MS_KEYALG_DSS_SIGN)
        write_dsa(&p, EVP_PKEY_get0_DSA(pk), ispub);
    else
        write_rsa(&p, EVP_PKEY_get0_RSA(pk), ispub);
    if (!noinc)
        *out += outlen;
    return outlen;
}

static int do_i2b_bio(BIO *out, EVP_PKEY *pk, int ispub)
{
    unsigned char *tmp = NULL;
    int outlen, wrlen;
    outlen = do_i2b(&tmp, pk, ispub);
    if (outlen < 0)
        return -1;
    wrlen = BIO_write(out, tmp, outlen);
    OPENSSL_free(tmp);
    if (wrlen == outlen)
        return outlen;
    return -1;
}

static int check_bitlen_dsa(DSA *dsa, int ispub, unsigned int *pmagic)
{
    int bitlen;
    const BIGNUM *p = NULL, *q = NULL, *g = NULL;
    const BIGNUM *pub_key = NULL, *priv_key = NULL;

    DSA_get0_pqg(dsa, &p, &q, &g);
    DSA_get0_key(dsa, &pub_key, &priv_key);
    bitlen = BN_num_bits(p);
    if ((bitlen & 7) || (BN_num_bits(q) != 160)
        || (BN_num_bits(g) > bitlen))
        goto badkey;
    if (ispub) {
        if (BN_num_bits(pub_key) > bitlen)
            goto badkey;
        *pmagic = MS_DSS1MAGIC;
    } else {
        if (BN_num_bits(priv_key) > 160)
            goto badkey;
        *pmagic = MS_DSS2MAGIC;
    }

    return bitlen;
 badkey:
    PEMerr(PEM_F_CHECK_BITLEN_DSA, PEM_R_UNSUPPORTED_KEY_COMPONENTS);
    return 0;
}

static int check_bitlen_rsa(RSA *rsa, int ispub, unsigned int *pmagic)
{
    int nbyte, hnbyte, bitlen;
    const BIGNUM *e;

    RSA_get0_key(rsa, NULL, &e, NULL);
    if (BN_num_bits(e) > 32)
        goto badkey;
    bitlen = RSA_bits(rsa);
    nbyte = RSA_size(rsa);
    hnbyte = (bitlen + 15) >> 4;
    if (ispub) {
        *pmagic = MS_RSA1MAGIC;
        return bitlen;
    } else {
        const BIGNUM *d, *p, *q, *iqmp, *dmp1, *dmq1;

        *pmagic = MS_RSA2MAGIC;

        /*
         * For private key each component must fit within nbyte or hnbyte.
         */
        RSA_get0_key(rsa, NULL, NULL, &d);
        if (BN_num_bytes(d) > nbyte)
            goto badkey;
        RSA_get0_factors(rsa, &p, &q);
        RSA_get0_crt_params(rsa, &dmp1, &dmq1, &iqmp);
        if ((BN_num_bytes(iqmp) > hnbyte)
            || (BN_num_bytes(p) > hnbyte)
            || (BN_num_bytes(q) > hnbyte)
            || (BN_num_bytes(dmp1) > hnbyte)
            || (BN_num_bytes(dmq1) > hnbyte))
            goto badkey;
    }
    return bitlen;
 badkey:
    PEMerr(PEM_F_CHECK_BITLEN_RSA, PEM_R_UNSUPPORTED_KEY_COMPONENTS);
    return 0;
}

static void write_rsa(unsigned char **out, RSA *rsa, int ispub)
{
    int nbyte, hnbyte;
    const BIGNUM *n, *d, *e, *p, *q, *iqmp, *dmp1, *dmq1;

    nbyte = RSA_size(rsa);
    hnbyte = (RSA_bits(rsa) + 15) >> 4;
    RSA_get0_key(rsa, &n, &e, &d);
    write_lebn(out, e, 4);
    write_lebn(out, n, nbyte);
    if (ispub)
        return;
    RSA_get0_factors(rsa, &p, &q);
    RSA_get0_crt_params(rsa, &dmp1, &dmq1, &iqmp);
    write_lebn(out, p, hnbyte);
    write_lebn(out, q, hnbyte);
    write_lebn(out, dmp1, hnbyte);
    write_lebn(out, dmq1, hnbyte);
    write_lebn(out, iqmp, hnbyte);
    write_lebn(out, d, nbyte);
}

static void write_dsa(unsigned char **out, DSA *dsa, int ispub)
{
    int nbyte;
    const BIGNUM *p = NULL, *q = NULL, *g = NULL;
    const BIGNUM *pub_key = NULL, *priv_key = NULL;

    DSA_get0_pqg(dsa, &p, &q, &g);
    DSA_get0_key(dsa, &pub_key, &priv_key);
    nbyte = BN_num_bytes(p);
    write_lebn(out, p, nbyte);
    write_lebn(out, q, 20);
    write_lebn(out, g, nbyte);
    if (ispub)
        write_lebn(out, pub_key, nbyte);
    else
        write_lebn(out, priv_key, 20);
    /* Set "invalid" for seed structure values */
    memset(*out, 0xff, 24);
    *out += 24;
    return;
}

int i2b_PrivateKey_bio(BIO *out, EVP_PKEY *pk)
{
    return do_i2b_bio(out, pk, 0);
}

int i2b_PublicKey_bio(BIO *out, EVP_PKEY *pk)
{
    return do_i2b_bio(out, pk, 1);
}

# ifndef OPENSSL_NO_RC4

static int do_PVK_header(const unsigned char **in, unsigned int length,
                         int skip_magic,
                         unsigned int *psaltlen, unsigned int *pkeylen)
{
    const unsigned char *p = *in;
    unsigned int pvk_magic, is_encrypted;
    if (skip_magic) {
        if (length < 20) {
            PEMerr(PEM_F_DO_PVK_HEADER, PEM_R_PVK_TOO_SHORT);
            return 0;
        }
    } else {
        if (length < 24) {
            PEMerr(PEM_F_DO_PVK_HEADER, PEM_R_PVK_TOO_SHORT);
            return 0;
        }
        pvk_magic = read_ledword(&p);
        if (pvk_magic != MS_PVKMAGIC) {
            PEMerr(PEM_F_DO_PVK_HEADER, PEM_R_BAD_MAGIC_NUMBER);
            return 0;
        }
    }
    /* Skip reserved */
    p += 4;
    /*
     * keytype =
     */ read_ledword(&p);
    is_encrypted = read_ledword(&p);
    *psaltlen = read_ledword(&p);
    *pkeylen = read_ledword(&p);

    if (*pkeylen > PVK_MAX_KEYLEN || *psaltlen > PVK_MAX_SALTLEN)
        return 0;

    if (is_encrypted && !*psaltlen) {
        PEMerr(PEM_F_DO_PVK_HEADER, PEM_R_INCONSISTENT_HEADER);
        return 0;
    }

    *in = p;
    return 1;
}

static int derive_pvk_key(unsigned char *key,
                          const unsigned char *salt, unsigned int saltlen,
                          const unsigned char *pass, int passlen)
{
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    int rv = 1;
    if (mctx == NULL
        || !EVP_DigestInit_ex(mctx, EVP_sha1(), NULL)
        || !EVP_DigestUpdate(mctx, salt, saltlen)
        || !EVP_DigestUpdate(mctx, pass, passlen)
        || !EVP_DigestFinal_ex(mctx, key, NULL))
        rv = 0;

    EVP_MD_CTX_free(mctx);
    return rv;
}

static EVP_PKEY *do_PVK_body(const unsigned char **in,
                             unsigned int saltlen, unsigned int keylen,
                             pem_password_cb *cb, void *u)
{
    EVP_PKEY *ret = NULL;
    const unsigned char *p = *in;
    unsigned int magic;
    unsigned char *enctmp = NULL, *q;
    unsigned char keybuf[20];

    EVP_CIPHER_CTX *cctx = EVP_CIPHER_CTX_new();
    if (saltlen) {
        char psbuf[PEM_BUFSIZE];
        int enctmplen, inlen;
        if (cb)
            inlen = cb(psbuf, PEM_BUFSIZE, 0, u);
        else
            inlen = PEM_def_callback(psbuf, PEM_BUFSIZE, 0, u);
        if (inlen < 0) {
            PEMerr(PEM_F_DO_PVK_BODY, PEM_R_BAD_PASSWORD_READ);
            goto err;
        }
        enctmp = OPENSSL_malloc(keylen + 8);
        if (enctmp == NULL) {
            PEMerr(PEM_F_DO_PVK_BODY, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        if (!derive_pvk_key(keybuf, p, saltlen,
                            (unsigned char *)psbuf, inlen))
            goto err;
        p += saltlen;
        /* Copy BLOBHEADER across, decrypt rest */
        memcpy(enctmp, p, 8);
        p += 8;
        if (keylen < 8) {
            PEMerr(PEM_F_DO_PVK_BODY, PEM_R_PVK_TOO_SHORT);
            goto err;
        }
        inlen = keylen - 8;
        q = enctmp + 8;
        if (!EVP_DecryptInit_ex(cctx, EVP_rc4(), NULL, keybuf, NULL))
            goto err;
        if (!EVP_DecryptUpdate(cctx, q, &enctmplen, p, inlen))
            goto err;
        if (!EVP_DecryptFinal_ex(cctx, q + enctmplen, &enctmplen))
            goto err;
        magic = read_ledword((const unsigned char **)&q);
        if (magic != MS_RSA2MAGIC && magic != MS_DSS2MAGIC) {
            q = enctmp + 8;
            memset(keybuf + 5, 0, 11);
            if (!EVP_DecryptInit_ex(cctx, EVP_rc4(), NULL, keybuf, NULL))
                goto err;
            if (!EVP_DecryptUpdate(cctx, q, &enctmplen, p, inlen))
                goto err;
            if (!EVP_DecryptFinal_ex(cctx, q + enctmplen, &enctmplen))
                goto err;
            magic = read_ledword((const unsigned char **)&q);
            if (magic != MS_RSA2MAGIC && magic != MS_DSS2MAGIC) {
                PEMerr(PEM_F_DO_PVK_BODY, PEM_R_BAD_DECRYPT);
                goto err;
            }
        }
        p = enctmp;
    }

    ret = b2i_PrivateKey(&p, keylen);
 err:
    EVP_CIPHER_CTX_free(cctx);
    if (enctmp != NULL) {
        OPENSSL_cleanse(keybuf, sizeof(keybuf));
        OPENSSL_free(enctmp);
    }
    return ret;
}

EVP_PKEY *b2i_PVK_bio(BIO *in, pem_password_cb *cb, void *u)
{
    unsigned char pvk_hdr[24], *buf = NULL;
    const unsigned char *p;
    int buflen;
    EVP_PKEY *ret = NULL;
    unsigned int saltlen, keylen;
    if (BIO_read(in, pvk_hdr, 24) != 24) {
        PEMerr(PEM_F_B2I_PVK_BIO, PEM_R_PVK_DATA_TOO_SHORT);
        return NULL;
    }
    p = pvk_hdr;

    if (!do_PVK_header(&p, 24, 0, &saltlen, &keylen))
        return 0;
    buflen = (int)keylen + saltlen;
    buf = OPENSSL_malloc(buflen);
    if (buf == NULL) {
        PEMerr(PEM_F_B2I_PVK_BIO, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    p = buf;
    if (BIO_read(in, buf, buflen) != buflen) {
        PEMerr(PEM_F_B2I_PVK_BIO, PEM_R_PVK_DATA_TOO_SHORT);
        goto err;
    }
    ret = do_PVK_body(&p, saltlen, keylen, cb, u);

 err:
    OPENSSL_clear_free(buf, buflen);
    return ret;
}

static int i2b_PVK(unsigned char **out, EVP_PKEY *pk, int enclevel,
                   pem_password_cb *cb, void *u)
{
    int outlen = 24, pklen;
    unsigned char *p = NULL, *start = NULL, *salt = NULL;
    EVP_CIPHER_CTX *cctx = NULL;
    if (enclevel)
        outlen += PVK_SALTLEN;
    pklen = do_i2b(NULL, pk, 0);
    if (pklen < 0)
        return -1;
    outlen += pklen;
    if (out == NULL)
        return outlen;
    if (*out != NULL) {
        p = *out;
    } else {
        start = p = OPENSSL_malloc(outlen);
        if (p == NULL) {
            PEMerr(PEM_F_I2B_PVK, ERR_R_MALLOC_FAILURE);
            return -1;
        }
    }

    cctx = EVP_CIPHER_CTX_new();
    if (cctx == NULL)
        goto error;

    write_ledword(&p, MS_PVKMAGIC);
    write_ledword(&p, 0);
    if (EVP_PKEY_id(pk) == EVP_PKEY_DSA)
        write_ledword(&p, MS_KEYTYPE_SIGN);
    else
        write_ledword(&p, MS_KEYTYPE_KEYX);
    write_ledword(&p, enclevel ? 1 : 0);
    write_ledword(&p, enclevel ? PVK_SALTLEN : 0);
    write_ledword(&p, pklen);
    if (enclevel) {
        if (RAND_bytes(p, PVK_SALTLEN) <= 0)
            goto error;
        salt = p;
        p += PVK_SALTLEN;
    }
    do_i2b(&p, pk, 0);
    if (enclevel != 0) {
        char psbuf[PEM_BUFSIZE];
        unsigned char keybuf[20];
        int enctmplen, inlen;
        if (cb)
            inlen = cb(psbuf, PEM_BUFSIZE, 1, u);
        else
            inlen = PEM_def_callback(psbuf, PEM_BUFSIZE, 1, u);
        if (inlen <= 0) {
            PEMerr(PEM_F_I2B_PVK, PEM_R_BAD_PASSWORD_READ);
            goto error;
        }
        if (!derive_pvk_key(keybuf, salt, PVK_SALTLEN,
                            (unsigned char *)psbuf, inlen))
            goto error;
        if (enclevel == 1)
            memset(keybuf + 5, 0, 11);
        p = salt + PVK_SALTLEN + 8;
        if (!EVP_EncryptInit_ex(cctx, EVP_rc4(), NULL, keybuf, NULL))
            goto error;
        OPENSSL_cleanse(keybuf, 20);
        if (!EVP_DecryptUpdate(cctx, p, &enctmplen, p, pklen - 8))
            goto error;
        if (!EVP_DecryptFinal_ex(cctx, p + enctmplen, &enctmplen))
            goto error;
    }

    EVP_CIPHER_CTX_free(cctx);

    if (*out == NULL)
        *out = start;

    return outlen;

 error:
    EVP_CIPHER_CTX_free(cctx);
    if (*out == NULL)
        OPENSSL_free(start);
    return -1;
}

int i2b_PVK_bio(BIO *out, EVP_PKEY *pk, int enclevel,
                pem_password_cb *cb, void *u)
{
    unsigned char *tmp = NULL;
    int outlen, wrlen;
    outlen = i2b_PVK(&tmp, pk, enclevel, cb, u);
    if (outlen < 0)
        return -1;
    wrlen = BIO_write(out, tmp, outlen);
    OPENSSL_free(tmp);
    if (wrlen == outlen) {
        PEMerr(PEM_F_I2B_PVK_BIO, PEM_R_BIO_WRITE_FAILURE);
        return outlen;
    }
    return -1;
}

# endif

#endif
