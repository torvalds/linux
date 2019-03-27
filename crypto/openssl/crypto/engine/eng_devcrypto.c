/*
 * Copyright 2017-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "e_os.h"
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <assert.h>

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/engine.h>
#include <openssl/objects.h>
#include <crypto/cryptodev.h>

#include "internal/engine.h"

/* #define ENGINE_DEVCRYPTO_DEBUG */

#ifdef CRYPTO_ALGORITHM_MIN
# define CHECK_BSD_STYLE_MACROS
#endif

/*
 * ONE global file descriptor for all sessions.  This allows operations
 * such as digest session data copying (see digest_copy()), but is also
 * saner...  why re-open /dev/crypto for every session?
 */
static int cfd;

static int clean_devcrypto_session(struct session_op *sess) {
    if (ioctl(cfd, CIOCFSESSION, &sess->ses) < 0) {
        SYSerr(SYS_F_IOCTL, errno);
        return 0;
    }
    memset(sess, 0, sizeof(struct session_op));
    return 1;
}

/******************************************************************************
 *
 * Ciphers
 *
 * Because they all do the same basic operation, we have only one set of
 * method functions for them all to share, and a mapping table between
 * NIDs and cryptodev IDs, with all the necessary size data.
 *
 *****/

struct cipher_ctx {
    struct session_op sess;
    int op;                      /* COP_ENCRYPT or COP_DECRYPT */
    unsigned long mode;          /* EVP_CIPH_*_MODE */

    /* to handle ctr mode being a stream cipher */
    unsigned char partial[EVP_MAX_BLOCK_LENGTH];
    unsigned int blocksize, num;
};

static const struct cipher_data_st {
    int nid;
    int blocksize;
    int keylen;
    int ivlen;
    int flags;
    int devcryptoid;
} cipher_data[] = {
#ifndef OPENSSL_NO_DES
    { NID_des_cbc, 8, 8, 8, EVP_CIPH_CBC_MODE, CRYPTO_DES_CBC },
    { NID_des_ede3_cbc, 8, 24, 8, EVP_CIPH_CBC_MODE, CRYPTO_3DES_CBC },
#endif
#ifndef OPENSSL_NO_BF
    { NID_bf_cbc, 8, 16, 8, EVP_CIPH_CBC_MODE, CRYPTO_BLF_CBC },
#endif
#ifndef OPENSSL_NO_CAST
    { NID_cast5_cbc, 8, 16, 8, EVP_CIPH_CBC_MODE, CRYPTO_CAST_CBC },
#endif
    { NID_aes_128_cbc, 16, 128 / 8, 16, EVP_CIPH_CBC_MODE, CRYPTO_AES_CBC },
    { NID_aes_192_cbc, 16, 192 / 8, 16, EVP_CIPH_CBC_MODE, CRYPTO_AES_CBC },
    { NID_aes_256_cbc, 16, 256 / 8, 16, EVP_CIPH_CBC_MODE, CRYPTO_AES_CBC },
#ifndef OPENSSL_NO_RC4
    { NID_rc4, 1, 16, 0, EVP_CIPH_STREAM_CIPHER, CRYPTO_ARC4 },
#endif
#if !defined(CHECK_BSD_STYLE_MACROS) || defined(CRYPTO_AES_CTR)
    { NID_aes_128_ctr, 16, 128 / 8, 16, EVP_CIPH_CTR_MODE, CRYPTO_AES_CTR },
    { NID_aes_192_ctr, 16, 192 / 8, 16, EVP_CIPH_CTR_MODE, CRYPTO_AES_CTR },
    { NID_aes_256_ctr, 16, 256 / 8, 16, EVP_CIPH_CTR_MODE, CRYPTO_AES_CTR },
#endif
#if 0                            /* Not yet supported */
    { NID_aes_128_xts, 16, 128 / 8 * 2, 16, EVP_CIPH_XTS_MODE, CRYPTO_AES_XTS },
    { NID_aes_256_xts, 16, 256 / 8 * 2, 16, EVP_CIPH_XTS_MODE, CRYPTO_AES_XTS },
#endif
#if !defined(CHECK_BSD_STYLE_MACROS) || defined(CRYPTO_AES_ECB)
    { NID_aes_128_ecb, 16, 128 / 8, 0, EVP_CIPH_ECB_MODE, CRYPTO_AES_ECB },
    { NID_aes_192_ecb, 16, 192 / 8, 0, EVP_CIPH_ECB_MODE, CRYPTO_AES_ECB },
    { NID_aes_256_ecb, 16, 256 / 8, 0, EVP_CIPH_ECB_MODE, CRYPTO_AES_ECB },
#endif
#if 0                            /* Not yet supported */
    { NID_aes_128_gcm, 16, 128 / 8, 16, EVP_CIPH_GCM_MODE, CRYPTO_AES_GCM },
    { NID_aes_192_gcm, 16, 192 / 8, 16, EVP_CIPH_GCM_MODE, CRYPTO_AES_GCM },
    { NID_aes_256_gcm, 16, 256 / 8, 16, EVP_CIPH_GCM_MODE, CRYPTO_AES_GCM },
#endif
#ifndef OPENSSL_NO_CAMELLIA
    { NID_camellia_128_cbc, 16, 128 / 8, 16, EVP_CIPH_CBC_MODE,
      CRYPTO_CAMELLIA_CBC },
    { NID_camellia_192_cbc, 16, 192 / 8, 16, EVP_CIPH_CBC_MODE,
      CRYPTO_CAMELLIA_CBC },
    { NID_camellia_256_cbc, 16, 256 / 8, 16, EVP_CIPH_CBC_MODE,
      CRYPTO_CAMELLIA_CBC },
#endif
};

static size_t get_cipher_data_index(int nid)
{
    size_t i;

    for (i = 0; i < OSSL_NELEM(cipher_data); i++)
        if (nid == cipher_data[i].nid)
            return i;

    /*
     * Code further down must make sure that only NIDs in the table above
     * are used.  If any other NID reaches this function, there's a grave
     * coding error further down.
     */
    assert("Code that never should be reached" == NULL);
    return -1;
}

static const struct cipher_data_st *get_cipher_data(int nid)
{
    return &cipher_data[get_cipher_data_index(nid)];
}

/*
 * Following are the three necessary functions to map OpenSSL functionality
 * with cryptodev.
 */

static int cipher_init(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                       const unsigned char *iv, int enc)
{
    struct cipher_ctx *cipher_ctx =
        (struct cipher_ctx *)EVP_CIPHER_CTX_get_cipher_data(ctx);
    const struct cipher_data_st *cipher_d =
        get_cipher_data(EVP_CIPHER_CTX_nid(ctx));

    /* cleanup a previous session */
    if (cipher_ctx->sess.ses != 0 &&
        clean_devcrypto_session(&cipher_ctx->sess) == 0)
        return 0;

    cipher_ctx->sess.cipher = cipher_d->devcryptoid;
    cipher_ctx->sess.keylen = cipher_d->keylen;
    cipher_ctx->sess.key = (void *)key;
    cipher_ctx->op = enc ? COP_ENCRYPT : COP_DECRYPT;
    cipher_ctx->mode = cipher_d->flags & EVP_CIPH_MODE;
    cipher_ctx->blocksize = cipher_d->blocksize;
    if (ioctl(cfd, CIOCGSESSION, &cipher_ctx->sess) < 0) {
        SYSerr(SYS_F_IOCTL, errno);
        return 0;
    }

    return 1;
}

static int cipher_do_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                            const unsigned char *in, size_t inl)
{
    struct cipher_ctx *cipher_ctx =
        (struct cipher_ctx *)EVP_CIPHER_CTX_get_cipher_data(ctx);
    struct crypt_op cryp;
    unsigned char *iv = EVP_CIPHER_CTX_iv_noconst(ctx);
#if !defined(COP_FLAG_WRITE_IV)
    unsigned char saved_iv[EVP_MAX_IV_LENGTH];
    const unsigned char *ivptr;
    size_t nblocks, ivlen;
#endif

    memset(&cryp, 0, sizeof(cryp));
    cryp.ses = cipher_ctx->sess.ses;
    cryp.len = inl;
    cryp.src = (void *)in;
    cryp.dst = (void *)out;
    cryp.iv = (void *)iv;
    cryp.op = cipher_ctx->op;
#if !defined(COP_FLAG_WRITE_IV)
    cryp.flags = 0;

    ivlen = EVP_CIPHER_CTX_iv_length(ctx);
    if (ivlen > 0)
        switch (cipher_ctx->mode) {
        case EVP_CIPH_CBC_MODE:
            assert(inl >= ivlen);
            if (!EVP_CIPHER_CTX_encrypting(ctx)) {
                ivptr = in + inl - ivlen;
                memcpy(saved_iv, ivptr, ivlen);
            }
            break;

        case EVP_CIPH_CTR_MODE:
            break;

        default: /* should not happen */
            return 0;
        }
#else
    cryp.flags = COP_FLAG_WRITE_IV;
#endif

    if (ioctl(cfd, CIOCCRYPT, &cryp) < 0) {
        SYSerr(SYS_F_IOCTL, errno);
        return 0;
    }

#if !defined(COP_FLAG_WRITE_IV)
    if (ivlen > 0)
        switch (cipher_ctx->mode) {
        case EVP_CIPH_CBC_MODE:
            assert(inl >= ivlen);
            if (EVP_CIPHER_CTX_encrypting(ctx))
                ivptr = out + inl - ivlen;
            else
                ivptr = saved_iv;

            memcpy(iv, ivptr, ivlen);
            break;

        case EVP_CIPH_CTR_MODE:
            nblocks = (inl + cipher_ctx->blocksize - 1)
                      / cipher_ctx->blocksize;
            do {
                ivlen--;
                nblocks += iv[ivlen];
                iv[ivlen] = (uint8_t) nblocks;
                nblocks >>= 8;
            } while (ivlen);
            break;

        default: /* should not happen */
            return 0;
        }
#endif

    return 1;
}

static int ctr_do_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                         const unsigned char *in, size_t inl)
{
    struct cipher_ctx *cipher_ctx =
        (struct cipher_ctx *)EVP_CIPHER_CTX_get_cipher_data(ctx);
    size_t nblocks, len;

    /* initial partial block */
    while (cipher_ctx->num && inl) {
        (*out++) = *(in++) ^ cipher_ctx->partial[cipher_ctx->num];
        --inl;
        cipher_ctx->num = (cipher_ctx->num + 1) % cipher_ctx->blocksize;
    }

    /* full blocks */
    if (inl > (unsigned int) cipher_ctx->blocksize) {
        nblocks = inl/cipher_ctx->blocksize;
        len = nblocks * cipher_ctx->blocksize;
        if (cipher_do_cipher(ctx, out, in, len) < 1)
            return 0;
        inl -= len;
        out += len;
        in += len;
    }

    /* final partial block */
    if (inl) {
        memset(cipher_ctx->partial, 0, cipher_ctx->blocksize);
        if (cipher_do_cipher(ctx, cipher_ctx->partial, cipher_ctx->partial,
            cipher_ctx->blocksize) < 1)
            return 0;
        while (inl--) {
            out[cipher_ctx->num] = in[cipher_ctx->num]
                                   ^ cipher_ctx->partial[cipher_ctx->num];
            cipher_ctx->num++;
        }
    }

    return 1;
}

static int cipher_ctrl(EVP_CIPHER_CTX *ctx, int type, int p1, void* p2)
{
    struct cipher_ctx *cipher_ctx =
        (struct cipher_ctx *)EVP_CIPHER_CTX_get_cipher_data(ctx);
    EVP_CIPHER_CTX *to_ctx = (EVP_CIPHER_CTX *)p2;
    struct cipher_ctx *to_cipher_ctx;

    switch (type) {
    case EVP_CTRL_COPY:
        if (cipher_ctx == NULL)
            return 1;
        /* when copying the context, a new session needs to be initialized */
        to_cipher_ctx =
            (struct cipher_ctx *)EVP_CIPHER_CTX_get_cipher_data(to_ctx);
        memset(&to_cipher_ctx->sess, 0, sizeof(to_cipher_ctx->sess));
        return cipher_init(to_ctx, cipher_ctx->sess.key, EVP_CIPHER_CTX_iv(ctx),
                           (cipher_ctx->op == COP_ENCRYPT));

    case EVP_CTRL_INIT:
        memset(&cipher_ctx->sess, 0, sizeof(cipher_ctx->sess));
        return 1;

    default:
        break;
    }

    return -1;
}

static int cipher_cleanup(EVP_CIPHER_CTX *ctx)
{
    struct cipher_ctx *cipher_ctx =
        (struct cipher_ctx *)EVP_CIPHER_CTX_get_cipher_data(ctx);

    return clean_devcrypto_session(&cipher_ctx->sess);
}

/*
 * Keep a table of known nids and associated methods.
 * Note that known_cipher_nids[] isn't necessarily indexed the same way as
 * cipher_data[] above, which known_cipher_methods[] is.
 */
static int known_cipher_nids[OSSL_NELEM(cipher_data)];
static int known_cipher_nids_amount = -1; /* -1 indicates not yet initialised */
static EVP_CIPHER *known_cipher_methods[OSSL_NELEM(cipher_data)] = { NULL, };

static void prepare_cipher_methods(void)
{
    size_t i;
    struct session_op sess;
    unsigned long cipher_mode;

    memset(&sess, 0, sizeof(sess));
    sess.key = (void *)"01234567890123456789012345678901234567890123456789";

    for (i = 0, known_cipher_nids_amount = 0;
         i < OSSL_NELEM(cipher_data); i++) {

        /*
         * Check that the algo is really availably by trying to open and close
         * a session.
         */
        sess.cipher = cipher_data[i].devcryptoid;
        sess.keylen = cipher_data[i].keylen;
        if (ioctl(cfd, CIOCGSESSION, &sess) < 0
            || ioctl(cfd, CIOCFSESSION, &sess.ses) < 0)
            continue;

        cipher_mode = cipher_data[i].flags & EVP_CIPH_MODE;

        if ((known_cipher_methods[i] =
                 EVP_CIPHER_meth_new(cipher_data[i].nid,
                                     cipher_mode == EVP_CIPH_CTR_MODE ? 1 :
                                                    cipher_data[i].blocksize,
                                     cipher_data[i].keylen)) == NULL
            || !EVP_CIPHER_meth_set_iv_length(known_cipher_methods[i],
                                              cipher_data[i].ivlen)
            || !EVP_CIPHER_meth_set_flags(known_cipher_methods[i],
                                          cipher_data[i].flags
                                          | EVP_CIPH_CUSTOM_COPY
                                          | EVP_CIPH_CTRL_INIT
                                          | EVP_CIPH_FLAG_DEFAULT_ASN1)
            || !EVP_CIPHER_meth_set_init(known_cipher_methods[i], cipher_init)
            || !EVP_CIPHER_meth_set_do_cipher(known_cipher_methods[i],
                                     cipher_mode == EVP_CIPH_CTR_MODE ?
                                              ctr_do_cipher :
                                              cipher_do_cipher)
            || !EVP_CIPHER_meth_set_ctrl(known_cipher_methods[i], cipher_ctrl)
            || !EVP_CIPHER_meth_set_cleanup(known_cipher_methods[i],
                                            cipher_cleanup)
            || !EVP_CIPHER_meth_set_impl_ctx_size(known_cipher_methods[i],
                                                  sizeof(struct cipher_ctx))) {
            EVP_CIPHER_meth_free(known_cipher_methods[i]);
            known_cipher_methods[i] = NULL;
        } else {
            known_cipher_nids[known_cipher_nids_amount++] =
                cipher_data[i].nid;
        }
    }
}

static const EVP_CIPHER *get_cipher_method(int nid)
{
    size_t i = get_cipher_data_index(nid);

    if (i == (size_t)-1)
        return NULL;
    return known_cipher_methods[i];
}

static int get_cipher_nids(const int **nids)
{
    *nids = known_cipher_nids;
    return known_cipher_nids_amount;
}

static void destroy_cipher_method(int nid)
{
    size_t i = get_cipher_data_index(nid);

    EVP_CIPHER_meth_free(known_cipher_methods[i]);
    known_cipher_methods[i] = NULL;
}

static void destroy_all_cipher_methods(void)
{
    size_t i;

    for (i = 0; i < OSSL_NELEM(cipher_data); i++)
        destroy_cipher_method(cipher_data[i].nid);
}

static int devcrypto_ciphers(ENGINE *e, const EVP_CIPHER **cipher,
                             const int **nids, int nid)
{
    if (cipher == NULL)
        return get_cipher_nids(nids);

    *cipher = get_cipher_method(nid);

    return *cipher != NULL;
}

/*
 * We only support digests if the cryptodev implementation supports multiple
 * data updates and session copying.  Otherwise, we would be forced to maintain
 * a cache, which is perilous if there's a lot of data coming in (if someone
 * wants to checksum an OpenSSL tarball, for example).
 */
#if defined(CIOCCPHASH) && defined(COP_FLAG_UPDATE) && defined(COP_FLAG_FINAL)
#define IMPLEMENT_DIGEST

/******************************************************************************
 *
 * Digests
 *
 * Because they all do the same basic operation, we have only one set of
 * method functions for them all to share, and a mapping table between
 * NIDs and cryptodev IDs, with all the necessary size data.
 *
 *****/

struct digest_ctx {
    struct session_op sess;
    /* This signals that the init function was called, not that it succeeded. */
    int init_called;
};

static const struct digest_data_st {
    int nid;
    int blocksize;
    int digestlen;
    int devcryptoid;
} digest_data[] = {
#ifndef OPENSSL_NO_MD5
    { NID_md5, /* MD5_CBLOCK */ 64, 16, CRYPTO_MD5 },
#endif
    { NID_sha1, SHA_CBLOCK, 20, CRYPTO_SHA1 },
#ifndef OPENSSL_NO_RMD160
# if !defined(CHECK_BSD_STYLE_MACROS) || defined(CRYPTO_RIPEMD160)
    { NID_ripemd160, /* RIPEMD160_CBLOCK */ 64, 20, CRYPTO_RIPEMD160 },
# endif
#endif
#if !defined(CHECK_BSD_STYLE_MACROS) || defined(CRYPTO_SHA2_224)
    { NID_sha224, SHA256_CBLOCK, 224 / 8, CRYPTO_SHA2_224 },
#endif
#if !defined(CHECK_BSD_STYLE_MACROS) || defined(CRYPTO_SHA2_256)
    { NID_sha256, SHA256_CBLOCK, 256 / 8, CRYPTO_SHA2_256 },
#endif
#if !defined(CHECK_BSD_STYLE_MACROS) || defined(CRYPTO_SHA2_384)
    { NID_sha384, SHA512_CBLOCK, 384 / 8, CRYPTO_SHA2_384 },
#endif
#if !defined(CHECK_BSD_STYLE_MACROS) || defined(CRYPTO_SHA2_512)
    { NID_sha512, SHA512_CBLOCK, 512 / 8, CRYPTO_SHA2_512 },
#endif
};

static size_t get_digest_data_index(int nid)
{
    size_t i;

    for (i = 0; i < OSSL_NELEM(digest_data); i++)
        if (nid == digest_data[i].nid)
            return i;

    /*
     * Code further down must make sure that only NIDs in the table above
     * are used.  If any other NID reaches this function, there's a grave
     * coding error further down.
     */
    assert("Code that never should be reached" == NULL);
    return -1;
}

static const struct digest_data_st *get_digest_data(int nid)
{
    return &digest_data[get_digest_data_index(nid)];
}

/*
 * Following are the four necessary functions to map OpenSSL functionality
 * with cryptodev.
 */

static int digest_init(EVP_MD_CTX *ctx)
{
    struct digest_ctx *digest_ctx =
        (struct digest_ctx *)EVP_MD_CTX_md_data(ctx);
    const struct digest_data_st *digest_d =
        get_digest_data(EVP_MD_CTX_type(ctx));

    digest_ctx->init_called = 1;

    memset(&digest_ctx->sess, 0, sizeof(digest_ctx->sess));
    digest_ctx->sess.mac = digest_d->devcryptoid;
    if (ioctl(cfd, CIOCGSESSION, &digest_ctx->sess) < 0) {
        SYSerr(SYS_F_IOCTL, errno);
        return 0;
    }

    return 1;
}

static int digest_op(struct digest_ctx *ctx, const void *src, size_t srclen,
                     void *res, unsigned int flags)
{
    struct crypt_op cryp;

    memset(&cryp, 0, sizeof(cryp));
    cryp.ses = ctx->sess.ses;
    cryp.len = srclen;
    cryp.src = (void *)src;
    cryp.dst = NULL;
    cryp.mac = res;
    cryp.flags = flags;
    return ioctl(cfd, CIOCCRYPT, &cryp);
}

static int digest_update(EVP_MD_CTX *ctx, const void *data, size_t count)
{
    struct digest_ctx *digest_ctx =
        (struct digest_ctx *)EVP_MD_CTX_md_data(ctx);

    if (count == 0)
        return 1;

    if (digest_ctx == NULL)
        return 0;

    if (digest_op(digest_ctx, data, count, NULL, COP_FLAG_UPDATE) < 0) {
        SYSerr(SYS_F_IOCTL, errno);
        return 0;
    }

    return 1;
}

static int digest_final(EVP_MD_CTX *ctx, unsigned char *md)
{
    struct digest_ctx *digest_ctx =
        (struct digest_ctx *)EVP_MD_CTX_md_data(ctx);

    if (md == NULL || digest_ctx == NULL)
        return 0;
    if (digest_op(digest_ctx, NULL, 0, md, COP_FLAG_FINAL) < 0) {
        SYSerr(SYS_F_IOCTL, errno);
        return 0;
    }

    return 1;
}

static int digest_copy(EVP_MD_CTX *to, const EVP_MD_CTX *from)
{
    struct digest_ctx *digest_from =
        (struct digest_ctx *)EVP_MD_CTX_md_data(from);
    struct digest_ctx *digest_to =
        (struct digest_ctx *)EVP_MD_CTX_md_data(to);
    struct cphash_op cphash;

    if (digest_from == NULL || digest_from->init_called != 1)
        return 1;

    if (!digest_init(to)) {
        SYSerr(SYS_F_IOCTL, errno);
        return 0;
    }

    cphash.src_ses = digest_from->sess.ses;
    cphash.dst_ses = digest_to->sess.ses;
    if (ioctl(cfd, CIOCCPHASH, &cphash) < 0) {
        SYSerr(SYS_F_IOCTL, errno);
        return 0;
    }
    return 1;
}

static int digest_cleanup(EVP_MD_CTX *ctx)
{
    struct digest_ctx *digest_ctx =
        (struct digest_ctx *)EVP_MD_CTX_md_data(ctx);

    if (digest_ctx == NULL)
        return 1;

    return clean_devcrypto_session(&digest_ctx->sess);
}

static int devcrypto_test_digest(size_t digest_data_index)
{
    struct session_op sess1, sess2;
    struct cphash_op cphash;
    int ret=0;

    memset(&sess1, 0, sizeof(sess1));
    memset(&sess2, 0, sizeof(sess2));
    sess1.mac = digest_data[digest_data_index].devcryptoid;
    if (ioctl(cfd, CIOCGSESSION, &sess1) < 0)
        return 0;
    /* Make sure the driver is capable of hash state copy */
    sess2.mac = sess1.mac;
    if (ioctl(cfd, CIOCGSESSION, &sess2) >= 0) {
        cphash.src_ses = sess1.ses;
        cphash.dst_ses = sess2.ses;
        if (ioctl(cfd, CIOCCPHASH, &cphash) >= 0)
            ret = 1;
        ioctl(cfd, CIOCFSESSION, &sess2.ses);
    }
    ioctl(cfd, CIOCFSESSION, &sess1.ses);
    return ret;
}

/*
 * Keep a table of known nids and associated methods.
 * Note that known_digest_nids[] isn't necessarily indexed the same way as
 * digest_data[] above, which known_digest_methods[] is.
 */
static int known_digest_nids[OSSL_NELEM(digest_data)];
static int known_digest_nids_amount = -1; /* -1 indicates not yet initialised */
static EVP_MD *known_digest_methods[OSSL_NELEM(digest_data)] = { NULL, };

static void prepare_digest_methods(void)
{
    size_t i;

    for (i = 0, known_digest_nids_amount = 0; i < OSSL_NELEM(digest_data);
         i++) {

        /*
         * Check that the algo is usable
         */
        if (!devcrypto_test_digest(i))
            continue;

        if ((known_digest_methods[i] = EVP_MD_meth_new(digest_data[i].nid,
                                                       NID_undef)) == NULL
            || !EVP_MD_meth_set_input_blocksize(known_digest_methods[i],
                                                digest_data[i].blocksize)
            || !EVP_MD_meth_set_result_size(known_digest_methods[i],
                                            digest_data[i].digestlen)
            || !EVP_MD_meth_set_init(known_digest_methods[i], digest_init)
            || !EVP_MD_meth_set_update(known_digest_methods[i], digest_update)
            || !EVP_MD_meth_set_final(known_digest_methods[i], digest_final)
            || !EVP_MD_meth_set_copy(known_digest_methods[i], digest_copy)
            || !EVP_MD_meth_set_cleanup(known_digest_methods[i], digest_cleanup)
            || !EVP_MD_meth_set_app_datasize(known_digest_methods[i],
                                             sizeof(struct digest_ctx))) {
            EVP_MD_meth_free(known_digest_methods[i]);
            known_digest_methods[i] = NULL;
        } else {
            known_digest_nids[known_digest_nids_amount++] = digest_data[i].nid;
        }
    }
}

static const EVP_MD *get_digest_method(int nid)
{
    size_t i = get_digest_data_index(nid);

    if (i == (size_t)-1)
        return NULL;
    return known_digest_methods[i];
}

static int get_digest_nids(const int **nids)
{
    *nids = known_digest_nids;
    return known_digest_nids_amount;
}

static void destroy_digest_method(int nid)
{
    size_t i = get_digest_data_index(nid);

    EVP_MD_meth_free(known_digest_methods[i]);
    known_digest_methods[i] = NULL;
}

static void destroy_all_digest_methods(void)
{
    size_t i;

    for (i = 0; i < OSSL_NELEM(digest_data); i++)
        destroy_digest_method(digest_data[i].nid);
}

static int devcrypto_digests(ENGINE *e, const EVP_MD **digest,
                             const int **nids, int nid)
{
    if (digest == NULL)
        return get_digest_nids(nids);

    *digest = get_digest_method(nid);

    return *digest != NULL;
}

#endif

/******************************************************************************
 *
 * LOAD / UNLOAD
 *
 *****/

static int devcrypto_unload(ENGINE *e)
{
    destroy_all_cipher_methods();
#ifdef IMPLEMENT_DIGEST
    destroy_all_digest_methods();
#endif

    close(cfd);

    return 1;
}
/*
 * This engine is always built into libcrypto, so it doesn't offer any
 * ability to be dynamically loadable.
 */
void engine_load_devcrypto_int()
{
    ENGINE *e = NULL;

    if ((cfd = open("/dev/crypto", O_RDWR, 0)) < 0) {
#ifndef ENGINE_DEVCRYPTO_DEBUG
        if (errno != ENOENT)
#endif
            fprintf(stderr, "Could not open /dev/crypto: %s\n", strerror(errno));
        return;
    }

    if ((e = ENGINE_new()) == NULL
        || !ENGINE_set_destroy_function(e, devcrypto_unload)) {
        ENGINE_free(e);
        /*
         * We know that devcrypto_unload() won't be called when one of the
         * above two calls have failed, so we close cfd explicitly here to
         * avoid leaking resources.
         */
        close(cfd);
        return;
    }

    prepare_cipher_methods();
#ifdef IMPLEMENT_DIGEST
    prepare_digest_methods();
#endif

    if (!ENGINE_set_id(e, "devcrypto")
        || !ENGINE_set_name(e, "/dev/crypto engine")

/*
 * Asymmetric ciphers aren't well supported with /dev/crypto.  Among the BSD
 * implementations, it seems to only exist in FreeBSD, and regarding the
 * parameters in its crypt_kop, the manual crypto(4) has this to say:
 *
 *    The semantics of these arguments are currently undocumented.
 *
 * Reading through the FreeBSD source code doesn't give much more than
 * their CRK_MOD_EXP implementation for ubsec.
 *
 * It doesn't look much better with cryptodev-linux.  They have the crypt_kop
 * structure as well as the command (CRK_*) in cryptodev.h, but no support
 * seems to be implemented at all for the moment.
 *
 * At the time of writing, it seems impossible to write proper support for
 * FreeBSD's asym features without some very deep knowledge and access to
 * specific kernel modules.
 *
 * /Richard Levitte, 2017-05-11
 */
#if 0
# ifndef OPENSSL_NO_RSA
        || !ENGINE_set_RSA(e, devcrypto_rsa)
# endif
# ifndef OPENSSL_NO_DSA
        || !ENGINE_set_DSA(e, devcrypto_dsa)
# endif
# ifndef OPENSSL_NO_DH
        || !ENGINE_set_DH(e, devcrypto_dh)
# endif
# ifndef OPENSSL_NO_EC
        || !ENGINE_set_EC(e, devcrypto_ec)
# endif
#endif
        || !ENGINE_set_ciphers(e, devcrypto_ciphers)
#ifdef IMPLEMENT_DIGEST
        || !ENGINE_set_digests(e, devcrypto_digests)
#endif
        ) {
        ENGINE_free(e);
        return;
    }

    ENGINE_add(e);
    ENGINE_free(e);          /* Loose our local reference */
    ERR_clear_error();
}
