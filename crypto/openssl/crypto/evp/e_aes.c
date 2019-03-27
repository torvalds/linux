/*
 * Copyright 2001-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslconf.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <string.h>
#include <assert.h>
#include <openssl/aes.h>
#include "internal/evp_int.h"
#include "modes_lcl.h"
#include <openssl/rand.h>
#include "evp_locl.h"

typedef struct {
    union {
        double align;
        AES_KEY ks;
    } ks;
    block128_f block;
    union {
        cbc128_f cbc;
        ctr128_f ctr;
    } stream;
} EVP_AES_KEY;

typedef struct {
    union {
        double align;
        AES_KEY ks;
    } ks;                       /* AES key schedule to use */
    int key_set;                /* Set if key initialised */
    int iv_set;                 /* Set if an iv is set */
    GCM128_CONTEXT gcm;
    unsigned char *iv;          /* Temporary IV store */
    int ivlen;                  /* IV length */
    int taglen;
    int iv_gen;                 /* It is OK to generate IVs */
    int tls_aad_len;            /* TLS AAD length */
    ctr128_f ctr;
} EVP_AES_GCM_CTX;

typedef struct {
    union {
        double align;
        AES_KEY ks;
    } ks1, ks2;                 /* AES key schedules to use */
    XTS128_CONTEXT xts;
    void (*stream) (const unsigned char *in,
                    unsigned char *out, size_t length,
                    const AES_KEY *key1, const AES_KEY *key2,
                    const unsigned char iv[16]);
} EVP_AES_XTS_CTX;

typedef struct {
    union {
        double align;
        AES_KEY ks;
    } ks;                       /* AES key schedule to use */
    int key_set;                /* Set if key initialised */
    int iv_set;                 /* Set if an iv is set */
    int tag_set;                /* Set if tag is valid */
    int len_set;                /* Set if message length set */
    int L, M;                   /* L and M parameters from RFC3610 */
    int tls_aad_len;            /* TLS AAD length */
    CCM128_CONTEXT ccm;
    ccm128_f str;
} EVP_AES_CCM_CTX;

#ifndef OPENSSL_NO_OCB
typedef struct {
    union {
        double align;
        AES_KEY ks;
    } ksenc;                    /* AES key schedule to use for encryption */
    union {
        double align;
        AES_KEY ks;
    } ksdec;                    /* AES key schedule to use for decryption */
    int key_set;                /* Set if key initialised */
    int iv_set;                 /* Set if an iv is set */
    OCB128_CONTEXT ocb;
    unsigned char *iv;          /* Temporary IV store */
    unsigned char tag[16];
    unsigned char data_buf[16]; /* Store partial data blocks */
    unsigned char aad_buf[16];  /* Store partial AAD blocks */
    int data_buf_len;
    int aad_buf_len;
    int ivlen;                  /* IV length */
    int taglen;
} EVP_AES_OCB_CTX;
#endif

#define MAXBITCHUNK     ((size_t)1<<(sizeof(size_t)*8-4))

#ifdef VPAES_ASM
int vpaes_set_encrypt_key(const unsigned char *userKey, int bits,
                          AES_KEY *key);
int vpaes_set_decrypt_key(const unsigned char *userKey, int bits,
                          AES_KEY *key);

void vpaes_encrypt(const unsigned char *in, unsigned char *out,
                   const AES_KEY *key);
void vpaes_decrypt(const unsigned char *in, unsigned char *out,
                   const AES_KEY *key);

void vpaes_cbc_encrypt(const unsigned char *in,
                       unsigned char *out,
                       size_t length,
                       const AES_KEY *key, unsigned char *ivec, int enc);
#endif
#ifdef BSAES_ASM
void bsaes_cbc_encrypt(const unsigned char *in, unsigned char *out,
                       size_t length, const AES_KEY *key,
                       unsigned char ivec[16], int enc);
void bsaes_ctr32_encrypt_blocks(const unsigned char *in, unsigned char *out,
                                size_t len, const AES_KEY *key,
                                const unsigned char ivec[16]);
void bsaes_xts_encrypt(const unsigned char *inp, unsigned char *out,
                       size_t len, const AES_KEY *key1,
                       const AES_KEY *key2, const unsigned char iv[16]);
void bsaes_xts_decrypt(const unsigned char *inp, unsigned char *out,
                       size_t len, const AES_KEY *key1,
                       const AES_KEY *key2, const unsigned char iv[16]);
#endif
#ifdef AES_CTR_ASM
void AES_ctr32_encrypt(const unsigned char *in, unsigned char *out,
                       size_t blocks, const AES_KEY *key,
                       const unsigned char ivec[AES_BLOCK_SIZE]);
#endif
#ifdef AES_XTS_ASM
void AES_xts_encrypt(const unsigned char *inp, unsigned char *out, size_t len,
                     const AES_KEY *key1, const AES_KEY *key2,
                     const unsigned char iv[16]);
void AES_xts_decrypt(const unsigned char *inp, unsigned char *out, size_t len,
                     const AES_KEY *key1, const AES_KEY *key2,
                     const unsigned char iv[16]);
#endif

/* increment counter (64-bit int) by 1 */
static void ctr64_inc(unsigned char *counter)
{
    int n = 8;
    unsigned char c;

    do {
        --n;
        c = counter[n];
        ++c;
        counter[n] = c;
        if (c)
            return;
    } while (n);
}

#if defined(OPENSSL_CPUID_OBJ) && (defined(__powerpc__) || defined(__ppc__) || defined(_ARCH_PPC))
# include "ppc_arch.h"
# ifdef VPAES_ASM
#  define VPAES_CAPABLE (OPENSSL_ppccap_P & PPC_ALTIVEC)
# endif
# define HWAES_CAPABLE  (OPENSSL_ppccap_P & PPC_CRYPTO207)
# define HWAES_set_encrypt_key aes_p8_set_encrypt_key
# define HWAES_set_decrypt_key aes_p8_set_decrypt_key
# define HWAES_encrypt aes_p8_encrypt
# define HWAES_decrypt aes_p8_decrypt
# define HWAES_cbc_encrypt aes_p8_cbc_encrypt
# define HWAES_ctr32_encrypt_blocks aes_p8_ctr32_encrypt_blocks
# define HWAES_xts_encrypt aes_p8_xts_encrypt
# define HWAES_xts_decrypt aes_p8_xts_decrypt
#endif

#if     defined(AES_ASM) && !defined(I386_ONLY) &&      (  \
        ((defined(__i386)       || defined(__i386__)    || \
          defined(_M_IX86)) && defined(OPENSSL_IA32_SSE2))|| \
        defined(__x86_64)       || defined(__x86_64__)  || \
        defined(_M_AMD64)       || defined(_M_X64)      )

extern unsigned int OPENSSL_ia32cap_P[];

# ifdef VPAES_ASM
#  define VPAES_CAPABLE   (OPENSSL_ia32cap_P[1]&(1<<(41-32)))
# endif
# ifdef BSAES_ASM
#  define BSAES_CAPABLE   (OPENSSL_ia32cap_P[1]&(1<<(41-32)))
# endif
/*
 * AES-NI section
 */
# define AESNI_CAPABLE   (OPENSSL_ia32cap_P[1]&(1<<(57-32)))

int aesni_set_encrypt_key(const unsigned char *userKey, int bits,
                          AES_KEY *key);
int aesni_set_decrypt_key(const unsigned char *userKey, int bits,
                          AES_KEY *key);

void aesni_encrypt(const unsigned char *in, unsigned char *out,
                   const AES_KEY *key);
void aesni_decrypt(const unsigned char *in, unsigned char *out,
                   const AES_KEY *key);

void aesni_ecb_encrypt(const unsigned char *in,
                       unsigned char *out,
                       size_t length, const AES_KEY *key, int enc);
void aesni_cbc_encrypt(const unsigned char *in,
                       unsigned char *out,
                       size_t length,
                       const AES_KEY *key, unsigned char *ivec, int enc);

void aesni_ctr32_encrypt_blocks(const unsigned char *in,
                                unsigned char *out,
                                size_t blocks,
                                const void *key, const unsigned char *ivec);

void aesni_xts_encrypt(const unsigned char *in,
                       unsigned char *out,
                       size_t length,
                       const AES_KEY *key1, const AES_KEY *key2,
                       const unsigned char iv[16]);

void aesni_xts_decrypt(const unsigned char *in,
                       unsigned char *out,
                       size_t length,
                       const AES_KEY *key1, const AES_KEY *key2,
                       const unsigned char iv[16]);

void aesni_ccm64_encrypt_blocks(const unsigned char *in,
                                unsigned char *out,
                                size_t blocks,
                                const void *key,
                                const unsigned char ivec[16],
                                unsigned char cmac[16]);

void aesni_ccm64_decrypt_blocks(const unsigned char *in,
                                unsigned char *out,
                                size_t blocks,
                                const void *key,
                                const unsigned char ivec[16],
                                unsigned char cmac[16]);

# if defined(__x86_64) || defined(__x86_64__) || defined(_M_AMD64) || defined(_M_X64)
size_t aesni_gcm_encrypt(const unsigned char *in,
                         unsigned char *out,
                         size_t len,
                         const void *key, unsigned char ivec[16], u64 *Xi);
#  define AES_gcm_encrypt aesni_gcm_encrypt
size_t aesni_gcm_decrypt(const unsigned char *in,
                         unsigned char *out,
                         size_t len,
                         const void *key, unsigned char ivec[16], u64 *Xi);
#  define AES_gcm_decrypt aesni_gcm_decrypt
void gcm_ghash_avx(u64 Xi[2], const u128 Htable[16], const u8 *in,
                   size_t len);
#  define AES_GCM_ASM(gctx)       (gctx->ctr==aesni_ctr32_encrypt_blocks && \
                                 gctx->gcm.ghash==gcm_ghash_avx)
#  define AES_GCM_ASM2(gctx)      (gctx->gcm.block==(block128_f)aesni_encrypt && \
                                 gctx->gcm.ghash==gcm_ghash_avx)
#  undef AES_GCM_ASM2          /* minor size optimization */
# endif

static int aesni_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                          const unsigned char *iv, int enc)
{
    int ret, mode;
    EVP_AES_KEY *dat = EVP_C_DATA(EVP_AES_KEY,ctx);

    mode = EVP_CIPHER_CTX_mode(ctx);
    if ((mode == EVP_CIPH_ECB_MODE || mode == EVP_CIPH_CBC_MODE)
        && !enc) {
        ret = aesni_set_decrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                    &dat->ks.ks);
        dat->block = (block128_f) aesni_decrypt;
        dat->stream.cbc = mode == EVP_CIPH_CBC_MODE ?
            (cbc128_f) aesni_cbc_encrypt : NULL;
    } else {
        ret = aesni_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                    &dat->ks.ks);
        dat->block = (block128_f) aesni_encrypt;
        if (mode == EVP_CIPH_CBC_MODE)
            dat->stream.cbc = (cbc128_f) aesni_cbc_encrypt;
        else if (mode == EVP_CIPH_CTR_MODE)
            dat->stream.ctr = (ctr128_f) aesni_ctr32_encrypt_blocks;
        else
            dat->stream.cbc = NULL;
    }

    if (ret < 0) {
        EVPerr(EVP_F_AESNI_INIT_KEY, EVP_R_AES_KEY_SETUP_FAILED);
        return 0;
    }

    return 1;
}

static int aesni_cbc_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                            const unsigned char *in, size_t len)
{
    aesni_cbc_encrypt(in, out, len, &EVP_C_DATA(EVP_AES_KEY,ctx)->ks.ks,
                      EVP_CIPHER_CTX_iv_noconst(ctx),
                      EVP_CIPHER_CTX_encrypting(ctx));

    return 1;
}

static int aesni_ecb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                            const unsigned char *in, size_t len)
{
    size_t bl = EVP_CIPHER_CTX_block_size(ctx);

    if (len < bl)
        return 1;

    aesni_ecb_encrypt(in, out, len, &EVP_C_DATA(EVP_AES_KEY,ctx)->ks.ks,
                      EVP_CIPHER_CTX_encrypting(ctx));

    return 1;
}

# define aesni_ofb_cipher aes_ofb_cipher
static int aesni_ofb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                            const unsigned char *in, size_t len);

# define aesni_cfb_cipher aes_cfb_cipher
static int aesni_cfb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                            const unsigned char *in, size_t len);

# define aesni_cfb8_cipher aes_cfb8_cipher
static int aesni_cfb8_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                             const unsigned char *in, size_t len);

# define aesni_cfb1_cipher aes_cfb1_cipher
static int aesni_cfb1_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                             const unsigned char *in, size_t len);

# define aesni_ctr_cipher aes_ctr_cipher
static int aesni_ctr_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                            const unsigned char *in, size_t len);

static int aesni_gcm_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                              const unsigned char *iv, int enc)
{
    EVP_AES_GCM_CTX *gctx = EVP_C_DATA(EVP_AES_GCM_CTX,ctx);
    if (!iv && !key)
        return 1;
    if (key) {
        aesni_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                              &gctx->ks.ks);
        CRYPTO_gcm128_init(&gctx->gcm, &gctx->ks, (block128_f) aesni_encrypt);
        gctx->ctr = (ctr128_f) aesni_ctr32_encrypt_blocks;
        /*
         * If we have an iv can set it directly, otherwise use saved IV.
         */
        if (iv == NULL && gctx->iv_set)
            iv = gctx->iv;
        if (iv) {
            CRYPTO_gcm128_setiv(&gctx->gcm, iv, gctx->ivlen);
            gctx->iv_set = 1;
        }
        gctx->key_set = 1;
    } else {
        /* If key set use IV, otherwise copy */
        if (gctx->key_set)
            CRYPTO_gcm128_setiv(&gctx->gcm, iv, gctx->ivlen);
        else
            memcpy(gctx->iv, iv, gctx->ivlen);
        gctx->iv_set = 1;
        gctx->iv_gen = 0;
    }
    return 1;
}

# define aesni_gcm_cipher aes_gcm_cipher
static int aesni_gcm_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                            const unsigned char *in, size_t len);

static int aesni_xts_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                              const unsigned char *iv, int enc)
{
    EVP_AES_XTS_CTX *xctx = EVP_C_DATA(EVP_AES_XTS_CTX,ctx);
    if (!iv && !key)
        return 1;

    if (key) {
        /* key_len is two AES keys */
        if (enc) {
            aesni_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 4,
                                  &xctx->ks1.ks);
            xctx->xts.block1 = (block128_f) aesni_encrypt;
            xctx->stream = aesni_xts_encrypt;
        } else {
            aesni_set_decrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 4,
                                  &xctx->ks1.ks);
            xctx->xts.block1 = (block128_f) aesni_decrypt;
            xctx->stream = aesni_xts_decrypt;
        }

        aesni_set_encrypt_key(key + EVP_CIPHER_CTX_key_length(ctx) / 2,
                              EVP_CIPHER_CTX_key_length(ctx) * 4,
                              &xctx->ks2.ks);
        xctx->xts.block2 = (block128_f) aesni_encrypt;

        xctx->xts.key1 = &xctx->ks1;
    }

    if (iv) {
        xctx->xts.key2 = &xctx->ks2;
        memcpy(EVP_CIPHER_CTX_iv_noconst(ctx), iv, 16);
    }

    return 1;
}

# define aesni_xts_cipher aes_xts_cipher
static int aesni_xts_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                            const unsigned char *in, size_t len);

static int aesni_ccm_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                              const unsigned char *iv, int enc)
{
    EVP_AES_CCM_CTX *cctx = EVP_C_DATA(EVP_AES_CCM_CTX,ctx);
    if (!iv && !key)
        return 1;
    if (key) {
        aesni_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                              &cctx->ks.ks);
        CRYPTO_ccm128_init(&cctx->ccm, cctx->M, cctx->L,
                           &cctx->ks, (block128_f) aesni_encrypt);
        cctx->str = enc ? (ccm128_f) aesni_ccm64_encrypt_blocks :
            (ccm128_f) aesni_ccm64_decrypt_blocks;
        cctx->key_set = 1;
    }
    if (iv) {
        memcpy(EVP_CIPHER_CTX_iv_noconst(ctx), iv, 15 - cctx->L);
        cctx->iv_set = 1;
    }
    return 1;
}

# define aesni_ccm_cipher aes_ccm_cipher
static int aesni_ccm_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                            const unsigned char *in, size_t len);

# ifndef OPENSSL_NO_OCB
void aesni_ocb_encrypt(const unsigned char *in, unsigned char *out,
                       size_t blocks, const void *key,
                       size_t start_block_num,
                       unsigned char offset_i[16],
                       const unsigned char L_[][16],
                       unsigned char checksum[16]);
void aesni_ocb_decrypt(const unsigned char *in, unsigned char *out,
                       size_t blocks, const void *key,
                       size_t start_block_num,
                       unsigned char offset_i[16],
                       const unsigned char L_[][16],
                       unsigned char checksum[16]);

static int aesni_ocb_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                              const unsigned char *iv, int enc)
{
    EVP_AES_OCB_CTX *octx = EVP_C_DATA(EVP_AES_OCB_CTX,ctx);
    if (!iv && !key)
        return 1;
    if (key) {
        do {
            /*
             * We set both the encrypt and decrypt key here because decrypt
             * needs both. We could possibly optimise to remove setting the
             * decrypt for an encryption operation.
             */
            aesni_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                  &octx->ksenc.ks);
            aesni_set_decrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                  &octx->ksdec.ks);
            if (!CRYPTO_ocb128_init(&octx->ocb,
                                    &octx->ksenc.ks, &octx->ksdec.ks,
                                    (block128_f) aesni_encrypt,
                                    (block128_f) aesni_decrypt,
                                    enc ? aesni_ocb_encrypt
                                        : aesni_ocb_decrypt))
                return 0;
        }
        while (0);

        /*
         * If we have an iv we can set it directly, otherwise use saved IV.
         */
        if (iv == NULL && octx->iv_set)
            iv = octx->iv;
        if (iv) {
            if (CRYPTO_ocb128_setiv(&octx->ocb, iv, octx->ivlen, octx->taglen)
                != 1)
                return 0;
            octx->iv_set = 1;
        }
        octx->key_set = 1;
    } else {
        /* If key set use IV, otherwise copy */
        if (octx->key_set)
            CRYPTO_ocb128_setiv(&octx->ocb, iv, octx->ivlen, octx->taglen);
        else
            memcpy(octx->iv, iv, octx->ivlen);
        octx->iv_set = 1;
    }
    return 1;
}

#  define aesni_ocb_cipher aes_ocb_cipher
static int aesni_ocb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                            const unsigned char *in, size_t len);
# endif                        /* OPENSSL_NO_OCB */

# define BLOCK_CIPHER_generic(nid,keylen,blocksize,ivlen,nmode,mode,MODE,flags) \
static const EVP_CIPHER aesni_##keylen##_##mode = { \
        nid##_##keylen##_##nmode,blocksize,keylen/8,ivlen, \
        flags|EVP_CIPH_##MODE##_MODE,   \
        aesni_init_key,                 \
        aesni_##mode##_cipher,          \
        NULL,                           \
        sizeof(EVP_AES_KEY),            \
        NULL,NULL,NULL,NULL }; \
static const EVP_CIPHER aes_##keylen##_##mode = { \
        nid##_##keylen##_##nmode,blocksize,     \
        keylen/8,ivlen, \
        flags|EVP_CIPH_##MODE##_MODE,   \
        aes_init_key,                   \
        aes_##mode##_cipher,            \
        NULL,                           \
        sizeof(EVP_AES_KEY),            \
        NULL,NULL,NULL,NULL }; \
const EVP_CIPHER *EVP_aes_##keylen##_##mode(void) \
{ return AESNI_CAPABLE?&aesni_##keylen##_##mode:&aes_##keylen##_##mode; }

# define BLOCK_CIPHER_custom(nid,keylen,blocksize,ivlen,mode,MODE,flags) \
static const EVP_CIPHER aesni_##keylen##_##mode = { \
        nid##_##keylen##_##mode,blocksize, \
        (EVP_CIPH_##MODE##_MODE==EVP_CIPH_XTS_MODE?2:1)*keylen/8, ivlen, \
        flags|EVP_CIPH_##MODE##_MODE,   \
        aesni_##mode##_init_key,        \
        aesni_##mode##_cipher,          \
        aes_##mode##_cleanup,           \
        sizeof(EVP_AES_##MODE##_CTX),   \
        NULL,NULL,aes_##mode##_ctrl,NULL }; \
static const EVP_CIPHER aes_##keylen##_##mode = { \
        nid##_##keylen##_##mode,blocksize, \
        (EVP_CIPH_##MODE##_MODE==EVP_CIPH_XTS_MODE?2:1)*keylen/8, ivlen, \
        flags|EVP_CIPH_##MODE##_MODE,   \
        aes_##mode##_init_key,          \
        aes_##mode##_cipher,            \
        aes_##mode##_cleanup,           \
        sizeof(EVP_AES_##MODE##_CTX),   \
        NULL,NULL,aes_##mode##_ctrl,NULL }; \
const EVP_CIPHER *EVP_aes_##keylen##_##mode(void) \
{ return AESNI_CAPABLE?&aesni_##keylen##_##mode:&aes_##keylen##_##mode; }

#elif   defined(AES_ASM) && (defined(__sparc) || defined(__sparc__))

# include "sparc_arch.h"

extern unsigned int OPENSSL_sparcv9cap_P[];

/*
 * Initial Fujitsu SPARC64 X support
 */
# define HWAES_CAPABLE           (OPENSSL_sparcv9cap_P[0] & SPARCV9_FJAESX)
# define HWAES_set_encrypt_key aes_fx_set_encrypt_key
# define HWAES_set_decrypt_key aes_fx_set_decrypt_key
# define HWAES_encrypt aes_fx_encrypt
# define HWAES_decrypt aes_fx_decrypt
# define HWAES_cbc_encrypt aes_fx_cbc_encrypt
# define HWAES_ctr32_encrypt_blocks aes_fx_ctr32_encrypt_blocks

# define SPARC_AES_CAPABLE       (OPENSSL_sparcv9cap_P[1] & CFR_AES)

void aes_t4_set_encrypt_key(const unsigned char *key, int bits, AES_KEY *ks);
void aes_t4_set_decrypt_key(const unsigned char *key, int bits, AES_KEY *ks);
void aes_t4_encrypt(const unsigned char *in, unsigned char *out,
                    const AES_KEY *key);
void aes_t4_decrypt(const unsigned char *in, unsigned char *out,
                    const AES_KEY *key);
/*
 * Key-length specific subroutines were chosen for following reason.
 * Each SPARC T4 core can execute up to 8 threads which share core's
 * resources. Loading as much key material to registers allows to
 * minimize references to shared memory interface, as well as amount
 * of instructions in inner loops [much needed on T4]. But then having
 * non-key-length specific routines would require conditional branches
 * either in inner loops or on subroutines' entries. Former is hardly
 * acceptable, while latter means code size increase to size occupied
 * by multiple key-length specific subroutines, so why fight?
 */
void aes128_t4_cbc_encrypt(const unsigned char *in, unsigned char *out,
                           size_t len, const AES_KEY *key,
                           unsigned char *ivec);
void aes128_t4_cbc_decrypt(const unsigned char *in, unsigned char *out,
                           size_t len, const AES_KEY *key,
                           unsigned char *ivec);
void aes192_t4_cbc_encrypt(const unsigned char *in, unsigned char *out,
                           size_t len, const AES_KEY *key,
                           unsigned char *ivec);
void aes192_t4_cbc_decrypt(const unsigned char *in, unsigned char *out,
                           size_t len, const AES_KEY *key,
                           unsigned char *ivec);
void aes256_t4_cbc_encrypt(const unsigned char *in, unsigned char *out,
                           size_t len, const AES_KEY *key,
                           unsigned char *ivec);
void aes256_t4_cbc_decrypt(const unsigned char *in, unsigned char *out,
                           size_t len, const AES_KEY *key,
                           unsigned char *ivec);
void aes128_t4_ctr32_encrypt(const unsigned char *in, unsigned char *out,
                             size_t blocks, const AES_KEY *key,
                             unsigned char *ivec);
void aes192_t4_ctr32_encrypt(const unsigned char *in, unsigned char *out,
                             size_t blocks, const AES_KEY *key,
                             unsigned char *ivec);
void aes256_t4_ctr32_encrypt(const unsigned char *in, unsigned char *out,
                             size_t blocks, const AES_KEY *key,
                             unsigned char *ivec);
void aes128_t4_xts_encrypt(const unsigned char *in, unsigned char *out,
                           size_t blocks, const AES_KEY *key1,
                           const AES_KEY *key2, const unsigned char *ivec);
void aes128_t4_xts_decrypt(const unsigned char *in, unsigned char *out,
                           size_t blocks, const AES_KEY *key1,
                           const AES_KEY *key2, const unsigned char *ivec);
void aes256_t4_xts_encrypt(const unsigned char *in, unsigned char *out,
                           size_t blocks, const AES_KEY *key1,
                           const AES_KEY *key2, const unsigned char *ivec);
void aes256_t4_xts_decrypt(const unsigned char *in, unsigned char *out,
                           size_t blocks, const AES_KEY *key1,
                           const AES_KEY *key2, const unsigned char *ivec);

static int aes_t4_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                           const unsigned char *iv, int enc)
{
    int ret, mode, bits;
    EVP_AES_KEY *dat = EVP_C_DATA(EVP_AES_KEY,ctx);

    mode = EVP_CIPHER_CTX_mode(ctx);
    bits = EVP_CIPHER_CTX_key_length(ctx) * 8;
    if ((mode == EVP_CIPH_ECB_MODE || mode == EVP_CIPH_CBC_MODE)
        && !enc) {
        ret = 0;
        aes_t4_set_decrypt_key(key, bits, &dat->ks.ks);
        dat->block = (block128_f) aes_t4_decrypt;
        switch (bits) {
        case 128:
            dat->stream.cbc = mode == EVP_CIPH_CBC_MODE ?
                (cbc128_f) aes128_t4_cbc_decrypt : NULL;
            break;
        case 192:
            dat->stream.cbc = mode == EVP_CIPH_CBC_MODE ?
                (cbc128_f) aes192_t4_cbc_decrypt : NULL;
            break;
        case 256:
            dat->stream.cbc = mode == EVP_CIPH_CBC_MODE ?
                (cbc128_f) aes256_t4_cbc_decrypt : NULL;
            break;
        default:
            ret = -1;
        }
    } else {
        ret = 0;
        aes_t4_set_encrypt_key(key, bits, &dat->ks.ks);
        dat->block = (block128_f) aes_t4_encrypt;
        switch (bits) {
        case 128:
            if (mode == EVP_CIPH_CBC_MODE)
                dat->stream.cbc = (cbc128_f) aes128_t4_cbc_encrypt;
            else if (mode == EVP_CIPH_CTR_MODE)
                dat->stream.ctr = (ctr128_f) aes128_t4_ctr32_encrypt;
            else
                dat->stream.cbc = NULL;
            break;
        case 192:
            if (mode == EVP_CIPH_CBC_MODE)
                dat->stream.cbc = (cbc128_f) aes192_t4_cbc_encrypt;
            else if (mode == EVP_CIPH_CTR_MODE)
                dat->stream.ctr = (ctr128_f) aes192_t4_ctr32_encrypt;
            else
                dat->stream.cbc = NULL;
            break;
        case 256:
            if (mode == EVP_CIPH_CBC_MODE)
                dat->stream.cbc = (cbc128_f) aes256_t4_cbc_encrypt;
            else if (mode == EVP_CIPH_CTR_MODE)
                dat->stream.ctr = (ctr128_f) aes256_t4_ctr32_encrypt;
            else
                dat->stream.cbc = NULL;
            break;
        default:
            ret = -1;
        }
    }

    if (ret < 0) {
        EVPerr(EVP_F_AES_T4_INIT_KEY, EVP_R_AES_KEY_SETUP_FAILED);
        return 0;
    }

    return 1;
}

# define aes_t4_cbc_cipher aes_cbc_cipher
static int aes_t4_cbc_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                             const unsigned char *in, size_t len);

# define aes_t4_ecb_cipher aes_ecb_cipher
static int aes_t4_ecb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                             const unsigned char *in, size_t len);

# define aes_t4_ofb_cipher aes_ofb_cipher
static int aes_t4_ofb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                             const unsigned char *in, size_t len);

# define aes_t4_cfb_cipher aes_cfb_cipher
static int aes_t4_cfb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                             const unsigned char *in, size_t len);

# define aes_t4_cfb8_cipher aes_cfb8_cipher
static int aes_t4_cfb8_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                              const unsigned char *in, size_t len);

# define aes_t4_cfb1_cipher aes_cfb1_cipher
static int aes_t4_cfb1_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                              const unsigned char *in, size_t len);

# define aes_t4_ctr_cipher aes_ctr_cipher
static int aes_t4_ctr_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                             const unsigned char *in, size_t len);

static int aes_t4_gcm_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                               const unsigned char *iv, int enc)
{
    EVP_AES_GCM_CTX *gctx = EVP_C_DATA(EVP_AES_GCM_CTX,ctx);
    if (!iv && !key)
        return 1;
    if (key) {
        int bits = EVP_CIPHER_CTX_key_length(ctx) * 8;
        aes_t4_set_encrypt_key(key, bits, &gctx->ks.ks);
        CRYPTO_gcm128_init(&gctx->gcm, &gctx->ks,
                           (block128_f) aes_t4_encrypt);
        switch (bits) {
        case 128:
            gctx->ctr = (ctr128_f) aes128_t4_ctr32_encrypt;
            break;
        case 192:
            gctx->ctr = (ctr128_f) aes192_t4_ctr32_encrypt;
            break;
        case 256:
            gctx->ctr = (ctr128_f) aes256_t4_ctr32_encrypt;
            break;
        default:
            return 0;
        }
        /*
         * If we have an iv can set it directly, otherwise use saved IV.
         */
        if (iv == NULL && gctx->iv_set)
            iv = gctx->iv;
        if (iv) {
            CRYPTO_gcm128_setiv(&gctx->gcm, iv, gctx->ivlen);
            gctx->iv_set = 1;
        }
        gctx->key_set = 1;
    } else {
        /* If key set use IV, otherwise copy */
        if (gctx->key_set)
            CRYPTO_gcm128_setiv(&gctx->gcm, iv, gctx->ivlen);
        else
            memcpy(gctx->iv, iv, gctx->ivlen);
        gctx->iv_set = 1;
        gctx->iv_gen = 0;
    }
    return 1;
}

# define aes_t4_gcm_cipher aes_gcm_cipher
static int aes_t4_gcm_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                             const unsigned char *in, size_t len);

static int aes_t4_xts_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                               const unsigned char *iv, int enc)
{
    EVP_AES_XTS_CTX *xctx = EVP_C_DATA(EVP_AES_XTS_CTX,ctx);
    if (!iv && !key)
        return 1;

    if (key) {
        int bits = EVP_CIPHER_CTX_key_length(ctx) * 4;
        xctx->stream = NULL;
        /* key_len is two AES keys */
        if (enc) {
            aes_t4_set_encrypt_key(key, bits, &xctx->ks1.ks);
            xctx->xts.block1 = (block128_f) aes_t4_encrypt;
            switch (bits) {
            case 128:
                xctx->stream = aes128_t4_xts_encrypt;
                break;
            case 256:
                xctx->stream = aes256_t4_xts_encrypt;
                break;
            default:
                return 0;
            }
        } else {
            aes_t4_set_decrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 4,
                                   &xctx->ks1.ks);
            xctx->xts.block1 = (block128_f) aes_t4_decrypt;
            switch (bits) {
            case 128:
                xctx->stream = aes128_t4_xts_decrypt;
                break;
            case 256:
                xctx->stream = aes256_t4_xts_decrypt;
                break;
            default:
                return 0;
            }
        }

        aes_t4_set_encrypt_key(key + EVP_CIPHER_CTX_key_length(ctx) / 2,
                               EVP_CIPHER_CTX_key_length(ctx) * 4,
                               &xctx->ks2.ks);
        xctx->xts.block2 = (block128_f) aes_t4_encrypt;

        xctx->xts.key1 = &xctx->ks1;
    }

    if (iv) {
        xctx->xts.key2 = &xctx->ks2;
        memcpy(EVP_CIPHER_CTX_iv_noconst(ctx), iv, 16);
    }

    return 1;
}

# define aes_t4_xts_cipher aes_xts_cipher
static int aes_t4_xts_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                             const unsigned char *in, size_t len);

static int aes_t4_ccm_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                               const unsigned char *iv, int enc)
{
    EVP_AES_CCM_CTX *cctx = EVP_C_DATA(EVP_AES_CCM_CTX,ctx);
    if (!iv && !key)
        return 1;
    if (key) {
        int bits = EVP_CIPHER_CTX_key_length(ctx) * 8;
        aes_t4_set_encrypt_key(key, bits, &cctx->ks.ks);
        CRYPTO_ccm128_init(&cctx->ccm, cctx->M, cctx->L,
                           &cctx->ks, (block128_f) aes_t4_encrypt);
        cctx->str = NULL;
        cctx->key_set = 1;
    }
    if (iv) {
        memcpy(EVP_CIPHER_CTX_iv_noconst(ctx), iv, 15 - cctx->L);
        cctx->iv_set = 1;
    }
    return 1;
}

# define aes_t4_ccm_cipher aes_ccm_cipher
static int aes_t4_ccm_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                             const unsigned char *in, size_t len);

# ifndef OPENSSL_NO_OCB
static int aes_t4_ocb_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                               const unsigned char *iv, int enc)
{
    EVP_AES_OCB_CTX *octx = EVP_C_DATA(EVP_AES_OCB_CTX,ctx);
    if (!iv && !key)
        return 1;
    if (key) {
        do {
            /*
             * We set both the encrypt and decrypt key here because decrypt
             * needs both. We could possibly optimise to remove setting the
             * decrypt for an encryption operation.
             */
            aes_t4_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                   &octx->ksenc.ks);
            aes_t4_set_decrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                   &octx->ksdec.ks);
            if (!CRYPTO_ocb128_init(&octx->ocb,
                                    &octx->ksenc.ks, &octx->ksdec.ks,
                                    (block128_f) aes_t4_encrypt,
                                    (block128_f) aes_t4_decrypt,
                                    NULL))
                return 0;
        }
        while (0);

        /*
         * If we have an iv we can set it directly, otherwise use saved IV.
         */
        if (iv == NULL && octx->iv_set)
            iv = octx->iv;
        if (iv) {
            if (CRYPTO_ocb128_setiv(&octx->ocb, iv, octx->ivlen, octx->taglen)
                != 1)
                return 0;
            octx->iv_set = 1;
        }
        octx->key_set = 1;
    } else {
        /* If key set use IV, otherwise copy */
        if (octx->key_set)
            CRYPTO_ocb128_setiv(&octx->ocb, iv, octx->ivlen, octx->taglen);
        else
            memcpy(octx->iv, iv, octx->ivlen);
        octx->iv_set = 1;
    }
    return 1;
}

#  define aes_t4_ocb_cipher aes_ocb_cipher
static int aes_t4_ocb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                             const unsigned char *in, size_t len);
# endif                        /* OPENSSL_NO_OCB */

# define BLOCK_CIPHER_generic(nid,keylen,blocksize,ivlen,nmode,mode,MODE,flags) \
static const EVP_CIPHER aes_t4_##keylen##_##mode = { \
        nid##_##keylen##_##nmode,blocksize,keylen/8,ivlen, \
        flags|EVP_CIPH_##MODE##_MODE,   \
        aes_t4_init_key,                \
        aes_t4_##mode##_cipher,         \
        NULL,                           \
        sizeof(EVP_AES_KEY),            \
        NULL,NULL,NULL,NULL }; \
static const EVP_CIPHER aes_##keylen##_##mode = { \
        nid##_##keylen##_##nmode,blocksize,     \
        keylen/8,ivlen, \
        flags|EVP_CIPH_##MODE##_MODE,   \
        aes_init_key,                   \
        aes_##mode##_cipher,            \
        NULL,                           \
        sizeof(EVP_AES_KEY),            \
        NULL,NULL,NULL,NULL }; \
const EVP_CIPHER *EVP_aes_##keylen##_##mode(void) \
{ return SPARC_AES_CAPABLE?&aes_t4_##keylen##_##mode:&aes_##keylen##_##mode; }

# define BLOCK_CIPHER_custom(nid,keylen,blocksize,ivlen,mode,MODE,flags) \
static const EVP_CIPHER aes_t4_##keylen##_##mode = { \
        nid##_##keylen##_##mode,blocksize, \
        (EVP_CIPH_##MODE##_MODE==EVP_CIPH_XTS_MODE?2:1)*keylen/8, ivlen, \
        flags|EVP_CIPH_##MODE##_MODE,   \
        aes_t4_##mode##_init_key,       \
        aes_t4_##mode##_cipher,         \
        aes_##mode##_cleanup,           \
        sizeof(EVP_AES_##MODE##_CTX),   \
        NULL,NULL,aes_##mode##_ctrl,NULL }; \
static const EVP_CIPHER aes_##keylen##_##mode = { \
        nid##_##keylen##_##mode,blocksize, \
        (EVP_CIPH_##MODE##_MODE==EVP_CIPH_XTS_MODE?2:1)*keylen/8, ivlen, \
        flags|EVP_CIPH_##MODE##_MODE,   \
        aes_##mode##_init_key,          \
        aes_##mode##_cipher,            \
        aes_##mode##_cleanup,           \
        sizeof(EVP_AES_##MODE##_CTX),   \
        NULL,NULL,aes_##mode##_ctrl,NULL }; \
const EVP_CIPHER *EVP_aes_##keylen##_##mode(void) \
{ return SPARC_AES_CAPABLE?&aes_t4_##keylen##_##mode:&aes_##keylen##_##mode; }

#elif defined(OPENSSL_CPUID_OBJ) && defined(__s390__)
/*
 * IBM S390X support
 */
# include "s390x_arch.h"

typedef struct {
    union {
        double align;
        /*-
         * KM-AES parameter block - begin
         * (see z/Architecture Principles of Operation >= SA22-7832-06)
         */
        struct {
            unsigned char k[32];
        } param;
        /* KM-AES parameter block - end */
    } km;
    unsigned int fc;
} S390X_AES_ECB_CTX;

typedef struct {
    union {
        double align;
        /*-
         * KMO-AES parameter block - begin
         * (see z/Architecture Principles of Operation >= SA22-7832-08)
         */
        struct {
            unsigned char cv[16];
            unsigned char k[32];
        } param;
        /* KMO-AES parameter block - end */
    } kmo;
    unsigned int fc;

    int res;
} S390X_AES_OFB_CTX;

typedef struct {
    union {
        double align;
        /*-
         * KMF-AES parameter block - begin
         * (see z/Architecture Principles of Operation >= SA22-7832-08)
         */
        struct {
            unsigned char cv[16];
            unsigned char k[32];
        } param;
        /* KMF-AES parameter block - end */
    } kmf;
    unsigned int fc;

    int res;
} S390X_AES_CFB_CTX;

typedef struct {
    union {
        double align;
        /*-
         * KMA-GCM-AES parameter block - begin
         * (see z/Architecture Principles of Operation >= SA22-7832-11)
         */
        struct {
            unsigned char reserved[12];
            union {
                unsigned int w;
                unsigned char b[4];
            } cv;
            union {
                unsigned long long g[2];
                unsigned char b[16];
            } t;
            unsigned char h[16];
            unsigned long long taadl;
            unsigned long long tpcl;
            union {
                unsigned long long g[2];
                unsigned int w[4];
            } j0;
            unsigned char k[32];
        } param;
        /* KMA-GCM-AES parameter block - end */
    } kma;
    unsigned int fc;
    int key_set;

    unsigned char *iv;
    int ivlen;
    int iv_set;
    int iv_gen;

    int taglen;

    unsigned char ares[16];
    unsigned char mres[16];
    unsigned char kres[16];
    int areslen;
    int mreslen;
    int kreslen;

    int tls_aad_len;
} S390X_AES_GCM_CTX;

typedef struct {
    union {
        double align;
        /*-
         * Padding is chosen so that ccm.kmac_param.k overlaps with key.k and
         * ccm.fc with key.k.rounds. Remember that on s390x, an AES_KEY's
         * rounds field is used to store the function code and that the key
         * schedule is not stored (if aes hardware support is detected).
         */
        struct {
            unsigned char pad[16];
            AES_KEY k;
        } key;

        struct {
            /*-
             * KMAC-AES parameter block - begin
             * (see z/Architecture Principles of Operation >= SA22-7832-08)
             */
            struct {
                union {
                    unsigned long long g[2];
                    unsigned char b[16];
                } icv;
                unsigned char k[32];
            } kmac_param;
            /* KMAC-AES paramater block - end */

            union {
                unsigned long long g[2];
                unsigned char b[16];
            } nonce;
            union {
                unsigned long long g[2];
                unsigned char b[16];
            } buf;

            unsigned long long blocks;
            int l;
            int m;
            int tls_aad_len;
            int iv_set;
            int tag_set;
            int len_set;
            int key_set;

            unsigned char pad[140];
            unsigned int fc;
        } ccm;
    } aes;
} S390X_AES_CCM_CTX;

/* Convert key size to function code: [16,24,32] -> [18,19,20]. */
# define S390X_AES_FC(keylen)  (S390X_AES_128 + ((((keylen) << 3) - 128) >> 6))

/* Most modes of operation need km for partial block processing. */
# define S390X_aes_128_CAPABLE (OPENSSL_s390xcap_P.km[0] &	\
                                S390X_CAPBIT(S390X_AES_128))
# define S390X_aes_192_CAPABLE (OPENSSL_s390xcap_P.km[0] &	\
                                S390X_CAPBIT(S390X_AES_192))
# define S390X_aes_256_CAPABLE (OPENSSL_s390xcap_P.km[0] &	\
                                S390X_CAPBIT(S390X_AES_256))

# define s390x_aes_init_key aes_init_key
static int s390x_aes_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                              const unsigned char *iv, int enc);

# define S390X_aes_128_cbc_CAPABLE	1	/* checked by callee */
# define S390X_aes_192_cbc_CAPABLE	1
# define S390X_aes_256_cbc_CAPABLE	1
# define S390X_AES_CBC_CTX		EVP_AES_KEY

# define s390x_aes_cbc_init_key aes_init_key

# define s390x_aes_cbc_cipher aes_cbc_cipher
static int s390x_aes_cbc_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                const unsigned char *in, size_t len);

# define S390X_aes_128_ecb_CAPABLE	S390X_aes_128_CAPABLE
# define S390X_aes_192_ecb_CAPABLE	S390X_aes_192_CAPABLE
# define S390X_aes_256_ecb_CAPABLE	S390X_aes_256_CAPABLE

static int s390x_aes_ecb_init_key(EVP_CIPHER_CTX *ctx,
                                  const unsigned char *key,
                                  const unsigned char *iv, int enc)
{
    S390X_AES_ECB_CTX *cctx = EVP_C_DATA(S390X_AES_ECB_CTX, ctx);
    const int keylen = EVP_CIPHER_CTX_key_length(ctx);

    cctx->fc = S390X_AES_FC(keylen);
    if (!enc)
        cctx->fc |= S390X_DECRYPT;

    memcpy(cctx->km.param.k, key, keylen);
    return 1;
}

static int s390x_aes_ecb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                const unsigned char *in, size_t len)
{
    S390X_AES_ECB_CTX *cctx = EVP_C_DATA(S390X_AES_ECB_CTX, ctx);

    s390x_km(in, len, out, cctx->fc, &cctx->km.param);
    return 1;
}

# define S390X_aes_128_ofb_CAPABLE (S390X_aes_128_CAPABLE &&		\
                                    (OPENSSL_s390xcap_P.kmo[0] &	\
                                     S390X_CAPBIT(S390X_AES_128)))
# define S390X_aes_192_ofb_CAPABLE (S390X_aes_192_CAPABLE &&		\
                                    (OPENSSL_s390xcap_P.kmo[0] &	\
                                     S390X_CAPBIT(S390X_AES_192)))
# define S390X_aes_256_ofb_CAPABLE (S390X_aes_256_CAPABLE &&		\
                                    (OPENSSL_s390xcap_P.kmo[0] &	\
                                     S390X_CAPBIT(S390X_AES_256)))

static int s390x_aes_ofb_init_key(EVP_CIPHER_CTX *ctx,
                                  const unsigned char *key,
                                  const unsigned char *ivec, int enc)
{
    S390X_AES_OFB_CTX *cctx = EVP_C_DATA(S390X_AES_OFB_CTX, ctx);
    const unsigned char *iv = EVP_CIPHER_CTX_original_iv(ctx);
    const int keylen = EVP_CIPHER_CTX_key_length(ctx);
    const int ivlen = EVP_CIPHER_CTX_iv_length(ctx);

    memcpy(cctx->kmo.param.cv, iv, ivlen);
    memcpy(cctx->kmo.param.k, key, keylen);
    cctx->fc = S390X_AES_FC(keylen);
    cctx->res = 0;
    return 1;
}

static int s390x_aes_ofb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                const unsigned char *in, size_t len)
{
    S390X_AES_OFB_CTX *cctx = EVP_C_DATA(S390X_AES_OFB_CTX, ctx);
    int n = cctx->res;
    int rem;

    while (n && len) {
        *out = *in ^ cctx->kmo.param.cv[n];
        n = (n + 1) & 0xf;
        --len;
        ++in;
        ++out;
    }

    rem = len & 0xf;

    len &= ~(size_t)0xf;
    if (len) {
        s390x_kmo(in, len, out, cctx->fc, &cctx->kmo.param);

        out += len;
        in += len;
    }

    if (rem) {
        s390x_km(cctx->kmo.param.cv, 16, cctx->kmo.param.cv, cctx->fc,
                 cctx->kmo.param.k);

        while (rem--) {
            out[n] = in[n] ^ cctx->kmo.param.cv[n];
            ++n;
        }
    }

    cctx->res = n;
    return 1;
}

# define S390X_aes_128_cfb_CAPABLE (S390X_aes_128_CAPABLE &&		\
                                    (OPENSSL_s390xcap_P.kmf[0] &	\
                                     S390X_CAPBIT(S390X_AES_128)))
# define S390X_aes_192_cfb_CAPABLE (S390X_aes_192_CAPABLE &&		\
                                    (OPENSSL_s390xcap_P.kmf[0] &	\
                                     S390X_CAPBIT(S390X_AES_192)))
# define S390X_aes_256_cfb_CAPABLE (S390X_aes_256_CAPABLE &&		\
                                    (OPENSSL_s390xcap_P.kmf[0] &	\
                                     S390X_CAPBIT(S390X_AES_256)))

static int s390x_aes_cfb_init_key(EVP_CIPHER_CTX *ctx,
                                  const unsigned char *key,
                                  const unsigned char *ivec, int enc)
{
    S390X_AES_CFB_CTX *cctx = EVP_C_DATA(S390X_AES_CFB_CTX, ctx);
    const unsigned char *iv = EVP_CIPHER_CTX_original_iv(ctx);
    const int keylen = EVP_CIPHER_CTX_key_length(ctx);
    const int ivlen = EVP_CIPHER_CTX_iv_length(ctx);

    cctx->fc = S390X_AES_FC(keylen);
    cctx->fc |= 16 << 24;   /* 16 bytes cipher feedback */
    if (!enc)
        cctx->fc |= S390X_DECRYPT;

    cctx->res = 0;
    memcpy(cctx->kmf.param.cv, iv, ivlen);
    memcpy(cctx->kmf.param.k, key, keylen);
    return 1;
}

static int s390x_aes_cfb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                const unsigned char *in, size_t len)
{
    S390X_AES_CFB_CTX *cctx = EVP_C_DATA(S390X_AES_CFB_CTX, ctx);
    const int keylen = EVP_CIPHER_CTX_key_length(ctx);
    const int enc = EVP_CIPHER_CTX_encrypting(ctx);
    int n = cctx->res;
    int rem;
    unsigned char tmp;

    while (n && len) {
        tmp = *in;
        *out = cctx->kmf.param.cv[n] ^ tmp;
        cctx->kmf.param.cv[n] = enc ? *out : tmp;
        n = (n + 1) & 0xf;
        --len;
        ++in;
        ++out;
    }

    rem = len & 0xf;

    len &= ~(size_t)0xf;
    if (len) {
        s390x_kmf(in, len, out, cctx->fc, &cctx->kmf.param);

        out += len;
        in += len;
    }

    if (rem) {
        s390x_km(cctx->kmf.param.cv, 16, cctx->kmf.param.cv,
                 S390X_AES_FC(keylen), cctx->kmf.param.k);

        while (rem--) {
            tmp = in[n];
            out[n] = cctx->kmf.param.cv[n] ^ tmp;
            cctx->kmf.param.cv[n] = enc ? out[n] : tmp;
            ++n;
        }
    }

    cctx->res = n;
    return 1;
}

# define S390X_aes_128_cfb8_CAPABLE (OPENSSL_s390xcap_P.kmf[0] &	\
                                     S390X_CAPBIT(S390X_AES_128))
# define S390X_aes_192_cfb8_CAPABLE (OPENSSL_s390xcap_P.kmf[0] &	\
                                     S390X_CAPBIT(S390X_AES_192))
# define S390X_aes_256_cfb8_CAPABLE (OPENSSL_s390xcap_P.kmf[0] &	\
                                     S390X_CAPBIT(S390X_AES_256))

static int s390x_aes_cfb8_init_key(EVP_CIPHER_CTX *ctx,
                                   const unsigned char *key,
                                   const unsigned char *ivec, int enc)
{
    S390X_AES_CFB_CTX *cctx = EVP_C_DATA(S390X_AES_CFB_CTX, ctx);
    const unsigned char *iv = EVP_CIPHER_CTX_original_iv(ctx);
    const int keylen = EVP_CIPHER_CTX_key_length(ctx);
    const int ivlen = EVP_CIPHER_CTX_iv_length(ctx);

    cctx->fc = S390X_AES_FC(keylen);
    cctx->fc |= 1 << 24;   /* 1 byte cipher feedback */
    if (!enc)
        cctx->fc |= S390X_DECRYPT;

    memcpy(cctx->kmf.param.cv, iv, ivlen);
    memcpy(cctx->kmf.param.k, key, keylen);
    return 1;
}

static int s390x_aes_cfb8_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                 const unsigned char *in, size_t len)
{
    S390X_AES_CFB_CTX *cctx = EVP_C_DATA(S390X_AES_CFB_CTX, ctx);

    s390x_kmf(in, len, out, cctx->fc, &cctx->kmf.param);
    return 1;
}

# define S390X_aes_128_cfb1_CAPABLE	0
# define S390X_aes_192_cfb1_CAPABLE	0
# define S390X_aes_256_cfb1_CAPABLE	0

# define s390x_aes_cfb1_init_key aes_init_key

# define s390x_aes_cfb1_cipher aes_cfb1_cipher
static int s390x_aes_cfb1_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                 const unsigned char *in, size_t len);

# define S390X_aes_128_ctr_CAPABLE	1	/* checked by callee */
# define S390X_aes_192_ctr_CAPABLE	1
# define S390X_aes_256_ctr_CAPABLE	1
# define S390X_AES_CTR_CTX		EVP_AES_KEY

# define s390x_aes_ctr_init_key aes_init_key

# define s390x_aes_ctr_cipher aes_ctr_cipher
static int s390x_aes_ctr_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                const unsigned char *in, size_t len);

# define S390X_aes_128_gcm_CAPABLE (S390X_aes_128_CAPABLE &&		\
                                    (OPENSSL_s390xcap_P.kma[0] &	\
                                     S390X_CAPBIT(S390X_AES_128)))
# define S390X_aes_192_gcm_CAPABLE (S390X_aes_192_CAPABLE &&		\
                                    (OPENSSL_s390xcap_P.kma[0] &	\
                                     S390X_CAPBIT(S390X_AES_192)))
# define S390X_aes_256_gcm_CAPABLE (S390X_aes_256_CAPABLE &&		\
                                    (OPENSSL_s390xcap_P.kma[0] &	\
                                     S390X_CAPBIT(S390X_AES_256)))

/* iv + padding length for iv lenghts != 12 */
# define S390X_gcm_ivpadlen(i)	((((i) + 15) >> 4 << 4) + 16)

/*-
 * Process additional authenticated data. Returns 0 on success. Code is
 * big-endian.
 */
static int s390x_aes_gcm_aad(S390X_AES_GCM_CTX *ctx, const unsigned char *aad,
                             size_t len)
{
    unsigned long long alen;
    int n, rem;

    if (ctx->kma.param.tpcl)
        return -2;

    alen = ctx->kma.param.taadl + len;
    if (alen > (U64(1) << 61) || (sizeof(len) == 8 && alen < len))
        return -1;
    ctx->kma.param.taadl = alen;

    n = ctx->areslen;
    if (n) {
        while (n && len) {
            ctx->ares[n] = *aad;
            n = (n + 1) & 0xf;
            ++aad;
            --len;
        }
        /* ctx->ares contains a complete block if offset has wrapped around */
        if (!n) {
            s390x_kma(ctx->ares, 16, NULL, 0, NULL, ctx->fc, &ctx->kma.param);
            ctx->fc |= S390X_KMA_HS;
        }
        ctx->areslen = n;
    }

    rem = len & 0xf;

    len &= ~(size_t)0xf;
    if (len) {
        s390x_kma(aad, len, NULL, 0, NULL, ctx->fc, &ctx->kma.param);
        aad += len;
        ctx->fc |= S390X_KMA_HS;
    }

    if (rem) {
        ctx->areslen = rem;

        do {
            --rem;
            ctx->ares[rem] = aad[rem];
        } while (rem);
    }
    return 0;
}

/*-
 * En/de-crypt plain/cipher-text and authenticate ciphertext. Returns 0 for
 * success. Code is big-endian.
 */
static int s390x_aes_gcm(S390X_AES_GCM_CTX *ctx, const unsigned char *in,
                         unsigned char *out, size_t len)
{
    const unsigned char *inptr;
    unsigned long long mlen;
    union {
        unsigned int w[4];
        unsigned char b[16];
    } buf;
    size_t inlen;
    int n, rem, i;

    mlen = ctx->kma.param.tpcl + len;
    if (mlen > ((U64(1) << 36) - 32) || (sizeof(len) == 8 && mlen < len))
        return -1;
    ctx->kma.param.tpcl = mlen;

    n = ctx->mreslen;
    if (n) {
        inptr = in;
        inlen = len;
        while (n && inlen) {
            ctx->mres[n] = *inptr;
            n = (n + 1) & 0xf;
            ++inptr;
            --inlen;
        }
        /* ctx->mres contains a complete block if offset has wrapped around */
        if (!n) {
            s390x_kma(ctx->ares, ctx->areslen, ctx->mres, 16, buf.b,
                      ctx->fc | S390X_KMA_LAAD, &ctx->kma.param);
            ctx->fc |= S390X_KMA_HS;
            ctx->areslen = 0;

            /* previous call already encrypted/decrypted its remainder,
             * see comment below */
            n = ctx->mreslen;
            while (n) {
                *out = buf.b[n];
                n = (n + 1) & 0xf;
                ++out;
                ++in;
                --len;
            }
            ctx->mreslen = 0;
        }
    }

    rem = len & 0xf;

    len &= ~(size_t)0xf;
    if (len) {
        s390x_kma(ctx->ares, ctx->areslen, in, len, out,
                  ctx->fc | S390X_KMA_LAAD, &ctx->kma.param);
        in += len;
        out += len;
        ctx->fc |= S390X_KMA_HS;
        ctx->areslen = 0;
    }

    /*-
     * If there is a remainder, it has to be saved such that it can be
     * processed by kma later. However, we also have to do the for-now
     * unauthenticated encryption/decryption part here and now...
     */
    if (rem) {
        if (!ctx->mreslen) {
            buf.w[0] = ctx->kma.param.j0.w[0];
            buf.w[1] = ctx->kma.param.j0.w[1];
            buf.w[2] = ctx->kma.param.j0.w[2];
            buf.w[3] = ctx->kma.param.cv.w + 1;
            s390x_km(buf.b, 16, ctx->kres, ctx->fc & 0x1f, &ctx->kma.param.k);
        }

        n = ctx->mreslen;
        for (i = 0; i < rem; i++) {
            ctx->mres[n + i] = in[i];
            out[i] = in[i] ^ ctx->kres[n + i];
        }

        ctx->mreslen += rem;
    }
    return 0;
}

/*-
 * Initialize context structure. Code is big-endian.
 */
static void s390x_aes_gcm_setiv(S390X_AES_GCM_CTX *ctx,
                                const unsigned char *iv)
{
    ctx->kma.param.t.g[0] = 0;
    ctx->kma.param.t.g[1] = 0;
    ctx->kma.param.tpcl = 0;
    ctx->kma.param.taadl = 0;
    ctx->mreslen = 0;
    ctx->areslen = 0;
    ctx->kreslen = 0;

    if (ctx->ivlen == 12) {
        memcpy(&ctx->kma.param.j0, iv, ctx->ivlen);
        ctx->kma.param.j0.w[3] = 1;
        ctx->kma.param.cv.w = 1;
    } else {
        /* ctx->iv has the right size and is already padded. */
        memcpy(ctx->iv, iv, ctx->ivlen);
        s390x_kma(ctx->iv, S390X_gcm_ivpadlen(ctx->ivlen), NULL, 0, NULL,
                  ctx->fc, &ctx->kma.param);
        ctx->fc |= S390X_KMA_HS;

        ctx->kma.param.j0.g[0] = ctx->kma.param.t.g[0];
        ctx->kma.param.j0.g[1] = ctx->kma.param.t.g[1];
        ctx->kma.param.cv.w = ctx->kma.param.j0.w[3];
        ctx->kma.param.t.g[0] = 0;
        ctx->kma.param.t.g[1] = 0;
    }
}

/*-
 * Performs various operations on the context structure depending on control
 * type. Returns 1 for success, 0 for failure and -1 for unknown control type.
 * Code is big-endian.
 */
static int s390x_aes_gcm_ctrl(EVP_CIPHER_CTX *c, int type, int arg, void *ptr)
{
    S390X_AES_GCM_CTX *gctx = EVP_C_DATA(S390X_AES_GCM_CTX, c);
    S390X_AES_GCM_CTX *gctx_out;
    EVP_CIPHER_CTX *out;
    unsigned char *buf, *iv;
    int ivlen, enc, len;

    switch (type) {
    case EVP_CTRL_INIT:
        ivlen = EVP_CIPHER_CTX_iv_length(c);
        iv = EVP_CIPHER_CTX_iv_noconst(c);
        gctx->key_set = 0;
        gctx->iv_set = 0;
        gctx->ivlen = ivlen;
        gctx->iv = iv;
        gctx->taglen = -1;
        gctx->iv_gen = 0;
        gctx->tls_aad_len = -1;
        return 1;

    case EVP_CTRL_AEAD_SET_IVLEN:
        if (arg <= 0)
            return 0;

        if (arg != 12) {
            iv = EVP_CIPHER_CTX_iv_noconst(c);
            len = S390X_gcm_ivpadlen(arg);

            /* Allocate memory for iv if needed. */
            if (gctx->ivlen == 12 || len > S390X_gcm_ivpadlen(gctx->ivlen)) {
                if (gctx->iv != iv)
                    OPENSSL_free(gctx->iv);

                if ((gctx->iv = OPENSSL_malloc(len)) == NULL) {
                    EVPerr(EVP_F_S390X_AES_GCM_CTRL, ERR_R_MALLOC_FAILURE);
                    return 0;
                }
            }
            /* Add padding. */
            memset(gctx->iv + arg, 0, len - arg - 8);
            *((unsigned long long *)(gctx->iv + len - 8)) = arg << 3;
        }
        gctx->ivlen = arg;
        return 1;

    case EVP_CTRL_AEAD_SET_TAG:
        buf = EVP_CIPHER_CTX_buf_noconst(c);
        enc = EVP_CIPHER_CTX_encrypting(c);
        if (arg <= 0 || arg > 16 || enc)
            return 0;

        memcpy(buf, ptr, arg);
        gctx->taglen = arg;
        return 1;

    case EVP_CTRL_AEAD_GET_TAG:
        enc = EVP_CIPHER_CTX_encrypting(c);
        if (arg <= 0 || arg > 16 || !enc || gctx->taglen < 0)
            return 0;

        memcpy(ptr, gctx->kma.param.t.b, arg);
        return 1;

    case EVP_CTRL_GCM_SET_IV_FIXED:
        /* Special case: -1 length restores whole iv */
        if (arg == -1) {
            memcpy(gctx->iv, ptr, gctx->ivlen);
            gctx->iv_gen = 1;
            return 1;
        }
        /*
         * Fixed field must be at least 4 bytes and invocation field at least
         * 8.
         */
        if ((arg < 4) || (gctx->ivlen - arg) < 8)
            return 0;

        if (arg)
            memcpy(gctx->iv, ptr, arg);

        enc = EVP_CIPHER_CTX_encrypting(c);
        if (enc && RAND_bytes(gctx->iv + arg, gctx->ivlen - arg) <= 0)
            return 0;

        gctx->iv_gen = 1;
        return 1;

    case EVP_CTRL_GCM_IV_GEN:
        if (gctx->iv_gen == 0 || gctx->key_set == 0)
            return 0;

        s390x_aes_gcm_setiv(gctx, gctx->iv);

        if (arg <= 0 || arg > gctx->ivlen)
            arg = gctx->ivlen;

        memcpy(ptr, gctx->iv + gctx->ivlen - arg, arg);
        /*
         * Invocation field will be at least 8 bytes in size and so no need
         * to check wrap around or increment more than last 8 bytes.
         */
        ctr64_inc(gctx->iv + gctx->ivlen - 8);
        gctx->iv_set = 1;
        return 1;

    case EVP_CTRL_GCM_SET_IV_INV:
        enc = EVP_CIPHER_CTX_encrypting(c);
        if (gctx->iv_gen == 0 || gctx->key_set == 0 || enc)
            return 0;

        memcpy(gctx->iv + gctx->ivlen - arg, ptr, arg);
        s390x_aes_gcm_setiv(gctx, gctx->iv);
        gctx->iv_set = 1;
        return 1;

    case EVP_CTRL_AEAD_TLS1_AAD:
        /* Save the aad for later use. */
        if (arg != EVP_AEAD_TLS1_AAD_LEN)
            return 0;

        buf = EVP_CIPHER_CTX_buf_noconst(c);
        memcpy(buf, ptr, arg);
        gctx->tls_aad_len = arg;

        len = buf[arg - 2] << 8 | buf[arg - 1];
        /* Correct length for explicit iv. */
        if (len < EVP_GCM_TLS_EXPLICIT_IV_LEN)
            return 0;
        len -= EVP_GCM_TLS_EXPLICIT_IV_LEN;

        /* If decrypting correct for tag too. */
        enc = EVP_CIPHER_CTX_encrypting(c);
        if (!enc) {
            if (len < EVP_GCM_TLS_TAG_LEN)
                return 0;
            len -= EVP_GCM_TLS_TAG_LEN;
        }
        buf[arg - 2] = len >> 8;
        buf[arg - 1] = len & 0xff;
        /* Extra padding: tag appended to record. */
        return EVP_GCM_TLS_TAG_LEN;

    case EVP_CTRL_COPY:
        out = ptr;
        gctx_out = EVP_C_DATA(S390X_AES_GCM_CTX, out);
        iv = EVP_CIPHER_CTX_iv_noconst(c);

        if (gctx->iv == iv) {
            gctx_out->iv = EVP_CIPHER_CTX_iv_noconst(out);
        } else {
            len = S390X_gcm_ivpadlen(gctx->ivlen);

            if ((gctx_out->iv = OPENSSL_malloc(len)) == NULL) {
                EVPerr(EVP_F_S390X_AES_GCM_CTRL, ERR_R_MALLOC_FAILURE);
                return 0;
            }

            memcpy(gctx_out->iv, gctx->iv, len);
        }
        return 1;

    default:
        return -1;
    }
}

/*-
 * Set key and/or iv. Returns 1 on success. Otherwise 0 is returned.
 */
static int s390x_aes_gcm_init_key(EVP_CIPHER_CTX *ctx,
                                  const unsigned char *key,
                                  const unsigned char *iv, int enc)
{
    S390X_AES_GCM_CTX *gctx = EVP_C_DATA(S390X_AES_GCM_CTX, ctx);
    int keylen;

    if (iv == NULL && key == NULL)
        return 1;

    if (key != NULL) {
        keylen = EVP_CIPHER_CTX_key_length(ctx);
        memcpy(&gctx->kma.param.k, key, keylen);

        gctx->fc = S390X_AES_FC(keylen);
        if (!enc)
            gctx->fc |= S390X_DECRYPT;

        if (iv == NULL && gctx->iv_set)
            iv = gctx->iv;

        if (iv != NULL) {
            s390x_aes_gcm_setiv(gctx, iv);
            gctx->iv_set = 1;
        }
        gctx->key_set = 1;
    } else {
        if (gctx->key_set)
            s390x_aes_gcm_setiv(gctx, iv);
        else
            memcpy(gctx->iv, iv, gctx->ivlen);

        gctx->iv_set = 1;
        gctx->iv_gen = 0;
    }
    return 1;
}

/*-
 * En/de-crypt and authenticate TLS packet. Returns the number of bytes written
 * if successful. Otherwise -1 is returned. Code is big-endian.
 */
static int s390x_aes_gcm_tls_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                    const unsigned char *in, size_t len)
{
    S390X_AES_GCM_CTX *gctx = EVP_C_DATA(S390X_AES_GCM_CTX, ctx);
    const unsigned char *buf = EVP_CIPHER_CTX_buf_noconst(ctx);
    const int enc = EVP_CIPHER_CTX_encrypting(ctx);
    int rv = -1;

    if (out != in || len < (EVP_GCM_TLS_EXPLICIT_IV_LEN + EVP_GCM_TLS_TAG_LEN))
        return -1;

    if (EVP_CIPHER_CTX_ctrl(ctx, enc ? EVP_CTRL_GCM_IV_GEN
                                     : EVP_CTRL_GCM_SET_IV_INV,
                            EVP_GCM_TLS_EXPLICIT_IV_LEN, out) <= 0)
        goto err;

    in += EVP_GCM_TLS_EXPLICIT_IV_LEN;
    out += EVP_GCM_TLS_EXPLICIT_IV_LEN;
    len -= EVP_GCM_TLS_EXPLICIT_IV_LEN + EVP_GCM_TLS_TAG_LEN;

    gctx->kma.param.taadl = gctx->tls_aad_len << 3;
    gctx->kma.param.tpcl = len << 3;
    s390x_kma(buf, gctx->tls_aad_len, in, len, out,
              gctx->fc | S390X_KMA_LAAD | S390X_KMA_LPC, &gctx->kma.param);

    if (enc) {
        memcpy(out + len, gctx->kma.param.t.b, EVP_GCM_TLS_TAG_LEN);
        rv = len + EVP_GCM_TLS_EXPLICIT_IV_LEN + EVP_GCM_TLS_TAG_LEN;
    } else {
        if (CRYPTO_memcmp(gctx->kma.param.t.b, in + len,
                          EVP_GCM_TLS_TAG_LEN)) {
            OPENSSL_cleanse(out, len);
            goto err;
        }
        rv = len;
    }
err:
    gctx->iv_set = 0;
    gctx->tls_aad_len = -1;
    return rv;
}

/*-
 * Called from EVP layer to initialize context, process additional
 * authenticated data, en/de-crypt plain/cipher-text and authenticate
 * ciphertext or process a TLS packet, depending on context. Returns bytes
 * written on success. Otherwise -1 is returned. Code is big-endian.
 */
static int s390x_aes_gcm_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                const unsigned char *in, size_t len)
{
    S390X_AES_GCM_CTX *gctx = EVP_C_DATA(S390X_AES_GCM_CTX, ctx);
    unsigned char *buf, tmp[16];
    int enc;

    if (!gctx->key_set)
        return -1;

    if (gctx->tls_aad_len >= 0)
        return s390x_aes_gcm_tls_cipher(ctx, out, in, len);

    if (!gctx->iv_set)
        return -1;

    if (in != NULL) {
        if (out == NULL) {
            if (s390x_aes_gcm_aad(gctx, in, len))
                return -1;
        } else {
            if (s390x_aes_gcm(gctx, in, out, len))
                return -1;
        }
        return len;
    } else {
        gctx->kma.param.taadl <<= 3;
        gctx->kma.param.tpcl <<= 3;
        s390x_kma(gctx->ares, gctx->areslen, gctx->mres, gctx->mreslen, tmp,
                  gctx->fc | S390X_KMA_LAAD | S390X_KMA_LPC, &gctx->kma.param);
        /* recall that we already did en-/decrypt gctx->mres
         * and returned it to caller... */
        OPENSSL_cleanse(tmp, gctx->mreslen);
        gctx->iv_set = 0;

        enc = EVP_CIPHER_CTX_encrypting(ctx);
        if (enc) {
            gctx->taglen = 16;
        } else {
            if (gctx->taglen < 0)
                return -1;

            buf = EVP_CIPHER_CTX_buf_noconst(ctx);
            if (CRYPTO_memcmp(buf, gctx->kma.param.t.b, gctx->taglen))
                return -1;
        }
        return 0;
    }
}

static int s390x_aes_gcm_cleanup(EVP_CIPHER_CTX *c)
{
    S390X_AES_GCM_CTX *gctx = EVP_C_DATA(S390X_AES_GCM_CTX, c);
    const unsigned char *iv;

    if (gctx == NULL)
        return 0;

    iv = EVP_CIPHER_CTX_iv(c);
    if (iv != gctx->iv)
        OPENSSL_free(gctx->iv);

    OPENSSL_cleanse(gctx, sizeof(*gctx));
    return 1;
}

# define S390X_AES_XTS_CTX		EVP_AES_XTS_CTX
# define S390X_aes_128_xts_CAPABLE	1	/* checked by callee */
# define S390X_aes_256_xts_CAPABLE	1

# define s390x_aes_xts_init_key aes_xts_init_key
static int s390x_aes_xts_init_key(EVP_CIPHER_CTX *ctx,
                                  const unsigned char *key,
                                  const unsigned char *iv, int enc);
# define s390x_aes_xts_cipher aes_xts_cipher
static int s390x_aes_xts_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                const unsigned char *in, size_t len);
# define s390x_aes_xts_ctrl aes_xts_ctrl
static int s390x_aes_xts_ctrl(EVP_CIPHER_CTX *, int type, int arg, void *ptr);
# define s390x_aes_xts_cleanup aes_xts_cleanup

# define S390X_aes_128_ccm_CAPABLE (S390X_aes_128_CAPABLE &&		\
                                    (OPENSSL_s390xcap_P.kmac[0] &	\
                                     S390X_CAPBIT(S390X_AES_128)))
# define S390X_aes_192_ccm_CAPABLE (S390X_aes_192_CAPABLE &&		\
                                    (OPENSSL_s390xcap_P.kmac[0] &	\
                                     S390X_CAPBIT(S390X_AES_192)))
# define S390X_aes_256_ccm_CAPABLE (S390X_aes_256_CAPABLE &&		\
                                    (OPENSSL_s390xcap_P.kmac[0] &	\
                                     S390X_CAPBIT(S390X_AES_256)))

# define S390X_CCM_AAD_FLAG	0x40

/*-
 * Set nonce and length fields. Code is big-endian.
 */
static inline void s390x_aes_ccm_setiv(S390X_AES_CCM_CTX *ctx,
                                          const unsigned char *nonce,
                                          size_t mlen)
{
    ctx->aes.ccm.nonce.b[0] &= ~S390X_CCM_AAD_FLAG;
    ctx->aes.ccm.nonce.g[1] = mlen;
    memcpy(ctx->aes.ccm.nonce.b + 1, nonce, 15 - ctx->aes.ccm.l);
}

/*-
 * Process additional authenticated data. Code is big-endian.
 */
static void s390x_aes_ccm_aad(S390X_AES_CCM_CTX *ctx, const unsigned char *aad,
                              size_t alen)
{
    unsigned char *ptr;
    int i, rem;

    if (!alen)
        return;

    ctx->aes.ccm.nonce.b[0] |= S390X_CCM_AAD_FLAG;

    /* Suppress 'type-punned pointer dereference' warning. */
    ptr = ctx->aes.ccm.buf.b;

    if (alen < ((1 << 16) - (1 << 8))) {
        *(uint16_t *)ptr = alen;
        i = 2;
    } else if (sizeof(alen) == 8
               && alen >= (size_t)1 << (32 % (sizeof(alen) * 8))) {
        *(uint16_t *)ptr = 0xffff;
        *(uint64_t *)(ptr + 2) = alen;
        i = 10;
    } else {
        *(uint16_t *)ptr = 0xfffe;
        *(uint32_t *)(ptr + 2) = alen;
        i = 6;
    }

    while (i < 16 && alen) {
        ctx->aes.ccm.buf.b[i] = *aad;
        ++aad;
        --alen;
        ++i;
    }
    while (i < 16) {
        ctx->aes.ccm.buf.b[i] = 0;
        ++i;
    }

    ctx->aes.ccm.kmac_param.icv.g[0] = 0;
    ctx->aes.ccm.kmac_param.icv.g[1] = 0;
    s390x_kmac(ctx->aes.ccm.nonce.b, 32, ctx->aes.ccm.fc,
               &ctx->aes.ccm.kmac_param);
    ctx->aes.ccm.blocks += 2;

    rem = alen & 0xf;
    alen &= ~(size_t)0xf;
    if (alen) {
        s390x_kmac(aad, alen, ctx->aes.ccm.fc, &ctx->aes.ccm.kmac_param);
        ctx->aes.ccm.blocks += alen >> 4;
        aad += alen;
    }
    if (rem) {
        for (i = 0; i < rem; i++)
            ctx->aes.ccm.kmac_param.icv.b[i] ^= aad[i];

        s390x_km(ctx->aes.ccm.kmac_param.icv.b, 16,
                 ctx->aes.ccm.kmac_param.icv.b, ctx->aes.ccm.fc,
                 ctx->aes.ccm.kmac_param.k);
        ctx->aes.ccm.blocks++;
    }
}

/*-
 * En/de-crypt plain/cipher-text. Compute tag from plaintext. Returns 0 for
 * success.
 */
static int s390x_aes_ccm(S390X_AES_CCM_CTX *ctx, const unsigned char *in,
                         unsigned char *out, size_t len, int enc)
{
    size_t n, rem;
    unsigned int i, l, num;
    unsigned char flags;

    flags = ctx->aes.ccm.nonce.b[0];
    if (!(flags & S390X_CCM_AAD_FLAG)) {
        s390x_km(ctx->aes.ccm.nonce.b, 16, ctx->aes.ccm.kmac_param.icv.b,
                 ctx->aes.ccm.fc, ctx->aes.ccm.kmac_param.k);
        ctx->aes.ccm.blocks++;
    }
    l = flags & 0x7;
    ctx->aes.ccm.nonce.b[0] = l;

    /*-
     * Reconstruct length from encoded length field
     * and initialize it with counter value.
     */
    n = 0;
    for (i = 15 - l; i < 15; i++) {
        n |= ctx->aes.ccm.nonce.b[i];
        ctx->aes.ccm.nonce.b[i] = 0;
        n <<= 8;
    }
    n |= ctx->aes.ccm.nonce.b[15];
    ctx->aes.ccm.nonce.b[15] = 1;

    if (n != len)
        return -1;		/* length mismatch */

    if (enc) {
        /* Two operations per block plus one for tag encryption */
        ctx->aes.ccm.blocks += (((len + 15) >> 4) << 1) + 1;
        if (ctx->aes.ccm.blocks > (1ULL << 61))
            return -2;		/* too much data */
    }

    num = 0;
    rem = len & 0xf;
    len &= ~(size_t)0xf;

    if (enc) {
        /* mac-then-encrypt */
        if (len)
            s390x_kmac(in, len, ctx->aes.ccm.fc, &ctx->aes.ccm.kmac_param);
        if (rem) {
            for (i = 0; i < rem; i++)
                ctx->aes.ccm.kmac_param.icv.b[i] ^= in[len + i];

            s390x_km(ctx->aes.ccm.kmac_param.icv.b, 16,
                     ctx->aes.ccm.kmac_param.icv.b, ctx->aes.ccm.fc,
                     ctx->aes.ccm.kmac_param.k);
        }

        CRYPTO_ctr128_encrypt_ctr32(in, out, len + rem, &ctx->aes.key.k,
                                    ctx->aes.ccm.nonce.b, ctx->aes.ccm.buf.b,
                                    &num, (ctr128_f)AES_ctr32_encrypt);
    } else {
        /* decrypt-then-mac */
        CRYPTO_ctr128_encrypt_ctr32(in, out, len + rem, &ctx->aes.key.k,
                                    ctx->aes.ccm.nonce.b, ctx->aes.ccm.buf.b,
                                    &num, (ctr128_f)AES_ctr32_encrypt);

        if (len)
            s390x_kmac(out, len, ctx->aes.ccm.fc, &ctx->aes.ccm.kmac_param);
        if (rem) {
            for (i = 0; i < rem; i++)
                ctx->aes.ccm.kmac_param.icv.b[i] ^= out[len + i];

            s390x_km(ctx->aes.ccm.kmac_param.icv.b, 16,
                     ctx->aes.ccm.kmac_param.icv.b, ctx->aes.ccm.fc,
                     ctx->aes.ccm.kmac_param.k);
        }
    }
    /* encrypt tag */
    for (i = 15 - l; i < 16; i++)
        ctx->aes.ccm.nonce.b[i] = 0;

    s390x_km(ctx->aes.ccm.nonce.b, 16, ctx->aes.ccm.buf.b, ctx->aes.ccm.fc,
             ctx->aes.ccm.kmac_param.k);
    ctx->aes.ccm.kmac_param.icv.g[0] ^= ctx->aes.ccm.buf.g[0];
    ctx->aes.ccm.kmac_param.icv.g[1] ^= ctx->aes.ccm.buf.g[1];

    ctx->aes.ccm.nonce.b[0] = flags;	/* restore flags field */
    return 0;
}

/*-
 * En/de-crypt and authenticate TLS packet. Returns the number of bytes written
 * if successful. Otherwise -1 is returned.
 */
static int s390x_aes_ccm_tls_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                    const unsigned char *in, size_t len)
{
    S390X_AES_CCM_CTX *cctx = EVP_C_DATA(S390X_AES_CCM_CTX, ctx);
    unsigned char *ivec = EVP_CIPHER_CTX_iv_noconst(ctx);
    unsigned char *buf = EVP_CIPHER_CTX_buf_noconst(ctx);
    const int enc = EVP_CIPHER_CTX_encrypting(ctx);

    if (out != in
            || len < (EVP_CCM_TLS_EXPLICIT_IV_LEN + (size_t)cctx->aes.ccm.m))
        return -1;

    if (enc) {
        /* Set explicit iv (sequence number). */
        memcpy(out, buf, EVP_CCM_TLS_EXPLICIT_IV_LEN);
    }

    len -= EVP_CCM_TLS_EXPLICIT_IV_LEN + cctx->aes.ccm.m;
    /*-
     * Get explicit iv (sequence number). We already have fixed iv
     * (server/client_write_iv) here.
     */
    memcpy(ivec + EVP_CCM_TLS_FIXED_IV_LEN, in, EVP_CCM_TLS_EXPLICIT_IV_LEN);
    s390x_aes_ccm_setiv(cctx, ivec, len);

    /* Process aad (sequence number|type|version|length) */
    s390x_aes_ccm_aad(cctx, buf, cctx->aes.ccm.tls_aad_len);

    in += EVP_CCM_TLS_EXPLICIT_IV_LEN;
    out += EVP_CCM_TLS_EXPLICIT_IV_LEN;

    if (enc) {
        if (s390x_aes_ccm(cctx, in, out, len, enc))
            return -1;

        memcpy(out + len, cctx->aes.ccm.kmac_param.icv.b, cctx->aes.ccm.m);
        return len + EVP_CCM_TLS_EXPLICIT_IV_LEN + cctx->aes.ccm.m;
    } else {
        if (!s390x_aes_ccm(cctx, in, out, len, enc)) {
            if (!CRYPTO_memcmp(cctx->aes.ccm.kmac_param.icv.b, in + len,
                               cctx->aes.ccm.m))
                return len;
        }

        OPENSSL_cleanse(out, len);
        return -1;
    }
}

/*-
 * Set key and flag field and/or iv. Returns 1 if successful. Otherwise 0 is
 * returned.
 */
static int s390x_aes_ccm_init_key(EVP_CIPHER_CTX *ctx,
                                  const unsigned char *key,
                                  const unsigned char *iv, int enc)
{
    S390X_AES_CCM_CTX *cctx = EVP_C_DATA(S390X_AES_CCM_CTX, ctx);
    unsigned char *ivec;
    int keylen;

    if (iv == NULL && key == NULL)
        return 1;

    if (key != NULL) {
        keylen = EVP_CIPHER_CTX_key_length(ctx);
        cctx->aes.ccm.fc = S390X_AES_FC(keylen);
        memcpy(cctx->aes.ccm.kmac_param.k, key, keylen);

        /* Store encoded m and l. */
        cctx->aes.ccm.nonce.b[0] = ((cctx->aes.ccm.l - 1) & 0x7)
                                 | (((cctx->aes.ccm.m - 2) >> 1) & 0x7) << 3;
        memset(cctx->aes.ccm.nonce.b + 1, 0,
               sizeof(cctx->aes.ccm.nonce.b));
        cctx->aes.ccm.blocks = 0;

        cctx->aes.ccm.key_set = 1;
    }

    if (iv != NULL) {
        ivec = EVP_CIPHER_CTX_iv_noconst(ctx);
        memcpy(ivec, iv, 15 - cctx->aes.ccm.l);

        cctx->aes.ccm.iv_set = 1;
    }

    return 1;
}

/*-
 * Called from EVP layer to initialize context, process additional
 * authenticated data, en/de-crypt plain/cipher-text and authenticate
 * plaintext or process a TLS packet, depending on context. Returns bytes
 * written on success. Otherwise -1 is returned.
 */
static int s390x_aes_ccm_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                const unsigned char *in, size_t len)
{
    S390X_AES_CCM_CTX *cctx = EVP_C_DATA(S390X_AES_CCM_CTX, ctx);
    const int enc = EVP_CIPHER_CTX_encrypting(ctx);
    int rv;
    unsigned char *buf, *ivec;

    if (!cctx->aes.ccm.key_set)
        return -1;

    if (cctx->aes.ccm.tls_aad_len >= 0)
        return s390x_aes_ccm_tls_cipher(ctx, out, in, len);

    /*-
     * Final(): Does not return any data. Recall that ccm is mac-then-encrypt
     * so integrity must be checked already at Update() i.e., before
     * potentially corrupted data is output.
     */
    if (in == NULL && out != NULL)
        return 0;

    if (!cctx->aes.ccm.iv_set)
        return -1;

    if (!enc && !cctx->aes.ccm.tag_set)
        return -1;

    if (out == NULL) {
        /* Update(): Pass message length. */
        if (in == NULL) {
            ivec = EVP_CIPHER_CTX_iv_noconst(ctx);
            s390x_aes_ccm_setiv(cctx, ivec, len);

            cctx->aes.ccm.len_set = 1;
            return len;
        }

        /* Update(): Process aad. */
        if (!cctx->aes.ccm.len_set && len)
            return -1;

        s390x_aes_ccm_aad(cctx, in, len);
        return len;
    }

    /* Update(): Process message. */

    if (!cctx->aes.ccm.len_set) {
        /*-
         * In case message length was not previously set explicitly via
         * Update(), set it now.
         */
        ivec = EVP_CIPHER_CTX_iv_noconst(ctx);
        s390x_aes_ccm_setiv(cctx, ivec, len);

        cctx->aes.ccm.len_set = 1;
    }

    if (enc) {
        if (s390x_aes_ccm(cctx, in, out, len, enc))
            return -1;

        cctx->aes.ccm.tag_set = 1;
        return len;
    } else {
        rv = -1;

        if (!s390x_aes_ccm(cctx, in, out, len, enc)) {
            buf = EVP_CIPHER_CTX_buf_noconst(ctx);
            if (!CRYPTO_memcmp(cctx->aes.ccm.kmac_param.icv.b, buf,
                               cctx->aes.ccm.m))
                rv = len;
        }

        if (rv == -1)
            OPENSSL_cleanse(out, len);

        cctx->aes.ccm.iv_set = 0;
        cctx->aes.ccm.tag_set = 0;
        cctx->aes.ccm.len_set = 0;
        return rv;
    }
}

/*-
 * Performs various operations on the context structure depending on control
 * type. Returns 1 for success, 0 for failure and -1 for unknown control type.
 * Code is big-endian.
 */
static int s390x_aes_ccm_ctrl(EVP_CIPHER_CTX *c, int type, int arg, void *ptr)
{
    S390X_AES_CCM_CTX *cctx = EVP_C_DATA(S390X_AES_CCM_CTX, c);
    unsigned char *buf, *iv;
    int enc, len;

    switch (type) {
    case EVP_CTRL_INIT:
        cctx->aes.ccm.key_set = 0;
        cctx->aes.ccm.iv_set = 0;
        cctx->aes.ccm.l = 8;
        cctx->aes.ccm.m = 12;
        cctx->aes.ccm.tag_set = 0;
        cctx->aes.ccm.len_set = 0;
        cctx->aes.ccm.tls_aad_len = -1;
        return 1;

    case EVP_CTRL_AEAD_TLS1_AAD:
        if (arg != EVP_AEAD_TLS1_AAD_LEN)
            return 0;

        /* Save the aad for later use. */
        buf = EVP_CIPHER_CTX_buf_noconst(c);
        memcpy(buf, ptr, arg);
        cctx->aes.ccm.tls_aad_len = arg;

        len = buf[arg - 2] << 8 | buf[arg - 1];
        if (len < EVP_CCM_TLS_EXPLICIT_IV_LEN)
            return 0;

        /* Correct length for explicit iv. */
        len -= EVP_CCM_TLS_EXPLICIT_IV_LEN;

        enc = EVP_CIPHER_CTX_encrypting(c);
        if (!enc) {
            if (len < cctx->aes.ccm.m)
                return 0;

            /* Correct length for tag. */
            len -= cctx->aes.ccm.m;
        }

        buf[arg - 2] = len >> 8;
        buf[arg - 1] = len & 0xff;

        /* Extra padding: tag appended to record. */
        return cctx->aes.ccm.m;

    case EVP_CTRL_CCM_SET_IV_FIXED:
        if (arg != EVP_CCM_TLS_FIXED_IV_LEN)
            return 0;

        /* Copy to first part of the iv. */
        iv = EVP_CIPHER_CTX_iv_noconst(c);
        memcpy(iv, ptr, arg);
        return 1;

    case EVP_CTRL_AEAD_SET_IVLEN:
        arg = 15 - arg;
        /* fall-through */

    case EVP_CTRL_CCM_SET_L:
        if (arg < 2 || arg > 8)
            return 0;

        cctx->aes.ccm.l = arg;
        return 1;

    case EVP_CTRL_AEAD_SET_TAG:
        if ((arg & 1) || arg < 4 || arg > 16)
            return 0;

        enc = EVP_CIPHER_CTX_encrypting(c);
        if (enc && ptr)
            return 0;

        if (ptr) {
            cctx->aes.ccm.tag_set = 1;
            buf = EVP_CIPHER_CTX_buf_noconst(c);
            memcpy(buf, ptr, arg);
        }

        cctx->aes.ccm.m = arg;
        return 1;

    case EVP_CTRL_AEAD_GET_TAG:
        enc = EVP_CIPHER_CTX_encrypting(c);
        if (!enc || !cctx->aes.ccm.tag_set)
            return 0;

        if(arg < cctx->aes.ccm.m)
            return 0;

        memcpy(ptr, cctx->aes.ccm.kmac_param.icv.b, cctx->aes.ccm.m);
        cctx->aes.ccm.tag_set = 0;
        cctx->aes.ccm.iv_set = 0;
        cctx->aes.ccm.len_set = 0;
        return 1;

    case EVP_CTRL_COPY:
        return 1;

    default:
        return -1;
    }
}

# define s390x_aes_ccm_cleanup aes_ccm_cleanup

# ifndef OPENSSL_NO_OCB
#  define S390X_AES_OCB_CTX		EVP_AES_OCB_CTX
#  define S390X_aes_128_ocb_CAPABLE	0
#  define S390X_aes_192_ocb_CAPABLE	0
#  define S390X_aes_256_ocb_CAPABLE	0

#  define s390x_aes_ocb_init_key aes_ocb_init_key
static int s390x_aes_ocb_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                                  const unsigned char *iv, int enc);
#  define s390x_aes_ocb_cipher aes_ocb_cipher
static int s390x_aes_ocb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                const unsigned char *in, size_t len);
#  define s390x_aes_ocb_cleanup aes_ocb_cleanup
static int s390x_aes_ocb_cleanup(EVP_CIPHER_CTX *);
#  define s390x_aes_ocb_ctrl aes_ocb_ctrl
static int s390x_aes_ocb_ctrl(EVP_CIPHER_CTX *, int type, int arg, void *ptr);
# endif

# define BLOCK_CIPHER_generic(nid,keylen,blocksize,ivlen,nmode,mode,	\
                              MODE,flags)				\
static const EVP_CIPHER s390x_aes_##keylen##_##mode = {			\
    nid##_##keylen##_##nmode,blocksize,					\
    keylen / 8,								\
    ivlen,								\
    flags | EVP_CIPH_##MODE##_MODE,					\
    s390x_aes_##mode##_init_key,					\
    s390x_aes_##mode##_cipher,						\
    NULL,								\
    sizeof(S390X_AES_##MODE##_CTX),					\
    NULL,								\
    NULL,								\
    NULL,								\
    NULL								\
};									\
static const EVP_CIPHER aes_##keylen##_##mode = {			\
    nid##_##keylen##_##nmode,						\
    blocksize,								\
    keylen / 8,								\
    ivlen,								\
    flags | EVP_CIPH_##MODE##_MODE,					\
    aes_init_key,							\
    aes_##mode##_cipher,						\
    NULL,								\
    sizeof(EVP_AES_KEY),						\
    NULL,								\
    NULL,								\
    NULL,								\
    NULL								\
};									\
const EVP_CIPHER *EVP_aes_##keylen##_##mode(void)			\
{									\
    return S390X_aes_##keylen##_##mode##_CAPABLE ?			\
           &s390x_aes_##keylen##_##mode : &aes_##keylen##_##mode;	\
}

# define BLOCK_CIPHER_custom(nid,keylen,blocksize,ivlen,mode,MODE,flags)\
static const EVP_CIPHER s390x_aes_##keylen##_##mode = {			\
    nid##_##keylen##_##mode,						\
    blocksize,								\
    (EVP_CIPH_##MODE##_MODE == EVP_CIPH_XTS_MODE ? 2 : 1) * keylen / 8,	\
    ivlen,								\
    flags | EVP_CIPH_##MODE##_MODE,					\
    s390x_aes_##mode##_init_key,					\
    s390x_aes_##mode##_cipher,						\
    s390x_aes_##mode##_cleanup,						\
    sizeof(S390X_AES_##MODE##_CTX),					\
    NULL,								\
    NULL,								\
    s390x_aes_##mode##_ctrl,						\
    NULL								\
};									\
static const EVP_CIPHER aes_##keylen##_##mode = {			\
    nid##_##keylen##_##mode,blocksize,					\
    (EVP_CIPH_##MODE##_MODE == EVP_CIPH_XTS_MODE ? 2 : 1) * keylen / 8,	\
    ivlen,								\
    flags | EVP_CIPH_##MODE##_MODE,					\
    aes_##mode##_init_key,						\
    aes_##mode##_cipher,						\
    aes_##mode##_cleanup,						\
    sizeof(EVP_AES_##MODE##_CTX),					\
    NULL,								\
    NULL,								\
    aes_##mode##_ctrl,							\
    NULL								\
};									\
const EVP_CIPHER *EVP_aes_##keylen##_##mode(void)			\
{									\
    return S390X_aes_##keylen##_##mode##_CAPABLE ?			\
           &s390x_aes_##keylen##_##mode : &aes_##keylen##_##mode;	\
}

#else

# define BLOCK_CIPHER_generic(nid,keylen,blocksize,ivlen,nmode,mode,MODE,flags) \
static const EVP_CIPHER aes_##keylen##_##mode = { \
        nid##_##keylen##_##nmode,blocksize,keylen/8,ivlen, \
        flags|EVP_CIPH_##MODE##_MODE,   \
        aes_init_key,                   \
        aes_##mode##_cipher,            \
        NULL,                           \
        sizeof(EVP_AES_KEY),            \
        NULL,NULL,NULL,NULL }; \
const EVP_CIPHER *EVP_aes_##keylen##_##mode(void) \
{ return &aes_##keylen##_##mode; }

# define BLOCK_CIPHER_custom(nid,keylen,blocksize,ivlen,mode,MODE,flags) \
static const EVP_CIPHER aes_##keylen##_##mode = { \
        nid##_##keylen##_##mode,blocksize, \
        (EVP_CIPH_##MODE##_MODE==EVP_CIPH_XTS_MODE?2:1)*keylen/8, ivlen, \
        flags|EVP_CIPH_##MODE##_MODE,   \
        aes_##mode##_init_key,          \
        aes_##mode##_cipher,            \
        aes_##mode##_cleanup,           \
        sizeof(EVP_AES_##MODE##_CTX),   \
        NULL,NULL,aes_##mode##_ctrl,NULL }; \
const EVP_CIPHER *EVP_aes_##keylen##_##mode(void) \
{ return &aes_##keylen##_##mode; }

#endif

#if defined(OPENSSL_CPUID_OBJ) && (defined(__arm__) || defined(__arm) || defined(__aarch64__))
# include "arm_arch.h"
# if __ARM_MAX_ARCH__>=7
#  if defined(BSAES_ASM)
#   define BSAES_CAPABLE (OPENSSL_armcap_P & ARMV7_NEON)
#  endif
#  if defined(VPAES_ASM)
#   define VPAES_CAPABLE (OPENSSL_armcap_P & ARMV7_NEON)
#  endif
#  define HWAES_CAPABLE (OPENSSL_armcap_P & ARMV8_AES)
#  define HWAES_set_encrypt_key aes_v8_set_encrypt_key
#  define HWAES_set_decrypt_key aes_v8_set_decrypt_key
#  define HWAES_encrypt aes_v8_encrypt
#  define HWAES_decrypt aes_v8_decrypt
#  define HWAES_cbc_encrypt aes_v8_cbc_encrypt
#  define HWAES_ctr32_encrypt_blocks aes_v8_ctr32_encrypt_blocks
# endif
#endif

#if defined(HWAES_CAPABLE)
int HWAES_set_encrypt_key(const unsigned char *userKey, const int bits,
                          AES_KEY *key);
int HWAES_set_decrypt_key(const unsigned char *userKey, const int bits,
                          AES_KEY *key);
void HWAES_encrypt(const unsigned char *in, unsigned char *out,
                   const AES_KEY *key);
void HWAES_decrypt(const unsigned char *in, unsigned char *out,
                   const AES_KEY *key);
void HWAES_cbc_encrypt(const unsigned char *in, unsigned char *out,
                       size_t length, const AES_KEY *key,
                       unsigned char *ivec, const int enc);
void HWAES_ctr32_encrypt_blocks(const unsigned char *in, unsigned char *out,
                                size_t len, const AES_KEY *key,
                                const unsigned char ivec[16]);
void HWAES_xts_encrypt(const unsigned char *inp, unsigned char *out,
                       size_t len, const AES_KEY *key1,
                       const AES_KEY *key2, const unsigned char iv[16]);
void HWAES_xts_decrypt(const unsigned char *inp, unsigned char *out,
                       size_t len, const AES_KEY *key1,
                       const AES_KEY *key2, const unsigned char iv[16]);
#endif

#define BLOCK_CIPHER_generic_pack(nid,keylen,flags)             \
        BLOCK_CIPHER_generic(nid,keylen,16,16,cbc,cbc,CBC,flags|EVP_CIPH_FLAG_DEFAULT_ASN1)     \
        BLOCK_CIPHER_generic(nid,keylen,16,0,ecb,ecb,ECB,flags|EVP_CIPH_FLAG_DEFAULT_ASN1)      \
        BLOCK_CIPHER_generic(nid,keylen,1,16,ofb128,ofb,OFB,flags|EVP_CIPH_FLAG_DEFAULT_ASN1)   \
        BLOCK_CIPHER_generic(nid,keylen,1,16,cfb128,cfb,CFB,flags|EVP_CIPH_FLAG_DEFAULT_ASN1)   \
        BLOCK_CIPHER_generic(nid,keylen,1,16,cfb1,cfb1,CFB,flags)       \
        BLOCK_CIPHER_generic(nid,keylen,1,16,cfb8,cfb8,CFB,flags)       \
        BLOCK_CIPHER_generic(nid,keylen,1,16,ctr,ctr,CTR,flags)

static int aes_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                        const unsigned char *iv, int enc)
{
    int ret, mode;
    EVP_AES_KEY *dat = EVP_C_DATA(EVP_AES_KEY,ctx);

    mode = EVP_CIPHER_CTX_mode(ctx);
    if ((mode == EVP_CIPH_ECB_MODE || mode == EVP_CIPH_CBC_MODE)
        && !enc) {
#ifdef HWAES_CAPABLE
        if (HWAES_CAPABLE) {
            ret = HWAES_set_decrypt_key(key,
                                        EVP_CIPHER_CTX_key_length(ctx) * 8,
                                        &dat->ks.ks);
            dat->block = (block128_f) HWAES_decrypt;
            dat->stream.cbc = NULL;
# ifdef HWAES_cbc_encrypt
            if (mode == EVP_CIPH_CBC_MODE)
                dat->stream.cbc = (cbc128_f) HWAES_cbc_encrypt;
# endif
        } else
#endif
#ifdef BSAES_CAPABLE
        if (BSAES_CAPABLE && mode == EVP_CIPH_CBC_MODE) {
            ret = AES_set_decrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                      &dat->ks.ks);
            dat->block = (block128_f) AES_decrypt;
            dat->stream.cbc = (cbc128_f) bsaes_cbc_encrypt;
        } else
#endif
#ifdef VPAES_CAPABLE
        if (VPAES_CAPABLE) {
            ret = vpaes_set_decrypt_key(key,
                                        EVP_CIPHER_CTX_key_length(ctx) * 8,
                                        &dat->ks.ks);
            dat->block = (block128_f) vpaes_decrypt;
            dat->stream.cbc = mode == EVP_CIPH_CBC_MODE ?
                (cbc128_f) vpaes_cbc_encrypt : NULL;
        } else
#endif
        {
            ret = AES_set_decrypt_key(key,
                                      EVP_CIPHER_CTX_key_length(ctx) * 8,
                                      &dat->ks.ks);
            dat->block = (block128_f) AES_decrypt;
            dat->stream.cbc = mode == EVP_CIPH_CBC_MODE ?
                (cbc128_f) AES_cbc_encrypt : NULL;
        }
    } else
#ifdef HWAES_CAPABLE
    if (HWAES_CAPABLE) {
        ret = HWAES_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                    &dat->ks.ks);
        dat->block = (block128_f) HWAES_encrypt;
        dat->stream.cbc = NULL;
# ifdef HWAES_cbc_encrypt
        if (mode == EVP_CIPH_CBC_MODE)
            dat->stream.cbc = (cbc128_f) HWAES_cbc_encrypt;
        else
# endif
# ifdef HWAES_ctr32_encrypt_blocks
        if (mode == EVP_CIPH_CTR_MODE)
            dat->stream.ctr = (ctr128_f) HWAES_ctr32_encrypt_blocks;
        else
# endif
            (void)0;            /* terminate potentially open 'else' */
    } else
#endif
#ifdef BSAES_CAPABLE
    if (BSAES_CAPABLE && mode == EVP_CIPH_CTR_MODE) {
        ret = AES_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                  &dat->ks.ks);
        dat->block = (block128_f) AES_encrypt;
        dat->stream.ctr = (ctr128_f) bsaes_ctr32_encrypt_blocks;
    } else
#endif
#ifdef VPAES_CAPABLE
    if (VPAES_CAPABLE) {
        ret = vpaes_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                    &dat->ks.ks);
        dat->block = (block128_f) vpaes_encrypt;
        dat->stream.cbc = mode == EVP_CIPH_CBC_MODE ?
            (cbc128_f) vpaes_cbc_encrypt : NULL;
    } else
#endif
    {
        ret = AES_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                  &dat->ks.ks);
        dat->block = (block128_f) AES_encrypt;
        dat->stream.cbc = mode == EVP_CIPH_CBC_MODE ?
            (cbc128_f) AES_cbc_encrypt : NULL;
#ifdef AES_CTR_ASM
        if (mode == EVP_CIPH_CTR_MODE)
            dat->stream.ctr = (ctr128_f) AES_ctr32_encrypt;
#endif
    }

    if (ret < 0) {
        EVPerr(EVP_F_AES_INIT_KEY, EVP_R_AES_KEY_SETUP_FAILED);
        return 0;
    }

    return 1;
}

static int aes_cbc_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                          const unsigned char *in, size_t len)
{
    EVP_AES_KEY *dat = EVP_C_DATA(EVP_AES_KEY,ctx);

    if (dat->stream.cbc)
        (*dat->stream.cbc) (in, out, len, &dat->ks,
                            EVP_CIPHER_CTX_iv_noconst(ctx),
                            EVP_CIPHER_CTX_encrypting(ctx));
    else if (EVP_CIPHER_CTX_encrypting(ctx))
        CRYPTO_cbc128_encrypt(in, out, len, &dat->ks,
                              EVP_CIPHER_CTX_iv_noconst(ctx), dat->block);
    else
        CRYPTO_cbc128_decrypt(in, out, len, &dat->ks,
                              EVP_CIPHER_CTX_iv_noconst(ctx), dat->block);

    return 1;
}

static int aes_ecb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                          const unsigned char *in, size_t len)
{
    size_t bl = EVP_CIPHER_CTX_block_size(ctx);
    size_t i;
    EVP_AES_KEY *dat = EVP_C_DATA(EVP_AES_KEY,ctx);

    if (len < bl)
        return 1;

    for (i = 0, len -= bl; i <= len; i += bl)
        (*dat->block) (in + i, out + i, &dat->ks);

    return 1;
}

static int aes_ofb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                          const unsigned char *in, size_t len)
{
    EVP_AES_KEY *dat = EVP_C_DATA(EVP_AES_KEY,ctx);

    int num = EVP_CIPHER_CTX_num(ctx);
    CRYPTO_ofb128_encrypt(in, out, len, &dat->ks,
                          EVP_CIPHER_CTX_iv_noconst(ctx), &num, dat->block);
    EVP_CIPHER_CTX_set_num(ctx, num);
    return 1;
}

static int aes_cfb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                          const unsigned char *in, size_t len)
{
    EVP_AES_KEY *dat = EVP_C_DATA(EVP_AES_KEY,ctx);

    int num = EVP_CIPHER_CTX_num(ctx);
    CRYPTO_cfb128_encrypt(in, out, len, &dat->ks,
                          EVP_CIPHER_CTX_iv_noconst(ctx), &num,
                          EVP_CIPHER_CTX_encrypting(ctx), dat->block);
    EVP_CIPHER_CTX_set_num(ctx, num);
    return 1;
}

static int aes_cfb8_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                           const unsigned char *in, size_t len)
{
    EVP_AES_KEY *dat = EVP_C_DATA(EVP_AES_KEY,ctx);

    int num = EVP_CIPHER_CTX_num(ctx);
    CRYPTO_cfb128_8_encrypt(in, out, len, &dat->ks,
                            EVP_CIPHER_CTX_iv_noconst(ctx), &num,
                            EVP_CIPHER_CTX_encrypting(ctx), dat->block);
    EVP_CIPHER_CTX_set_num(ctx, num);
    return 1;
}

static int aes_cfb1_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                           const unsigned char *in, size_t len)
{
    EVP_AES_KEY *dat = EVP_C_DATA(EVP_AES_KEY,ctx);

    if (EVP_CIPHER_CTX_test_flags(ctx, EVP_CIPH_FLAG_LENGTH_BITS)) {
        int num = EVP_CIPHER_CTX_num(ctx);
        CRYPTO_cfb128_1_encrypt(in, out, len, &dat->ks,
                                EVP_CIPHER_CTX_iv_noconst(ctx), &num,
                                EVP_CIPHER_CTX_encrypting(ctx), dat->block);
        EVP_CIPHER_CTX_set_num(ctx, num);
        return 1;
    }

    while (len >= MAXBITCHUNK) {
        int num = EVP_CIPHER_CTX_num(ctx);
        CRYPTO_cfb128_1_encrypt(in, out, MAXBITCHUNK * 8, &dat->ks,
                                EVP_CIPHER_CTX_iv_noconst(ctx), &num,
                                EVP_CIPHER_CTX_encrypting(ctx), dat->block);
        EVP_CIPHER_CTX_set_num(ctx, num);
        len -= MAXBITCHUNK;
        out += MAXBITCHUNK;
        in  += MAXBITCHUNK;
    }
    if (len) {
        int num = EVP_CIPHER_CTX_num(ctx);
        CRYPTO_cfb128_1_encrypt(in, out, len * 8, &dat->ks,
                                EVP_CIPHER_CTX_iv_noconst(ctx), &num,
                                EVP_CIPHER_CTX_encrypting(ctx), dat->block);
        EVP_CIPHER_CTX_set_num(ctx, num);
    }

    return 1;
}

static int aes_ctr_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                          const unsigned char *in, size_t len)
{
    unsigned int num = EVP_CIPHER_CTX_num(ctx);
    EVP_AES_KEY *dat = EVP_C_DATA(EVP_AES_KEY,ctx);

    if (dat->stream.ctr)
        CRYPTO_ctr128_encrypt_ctr32(in, out, len, &dat->ks,
                                    EVP_CIPHER_CTX_iv_noconst(ctx),
                                    EVP_CIPHER_CTX_buf_noconst(ctx),
                                    &num, dat->stream.ctr);
    else
        CRYPTO_ctr128_encrypt(in, out, len, &dat->ks,
                              EVP_CIPHER_CTX_iv_noconst(ctx),
                              EVP_CIPHER_CTX_buf_noconst(ctx), &num,
                              dat->block);
    EVP_CIPHER_CTX_set_num(ctx, num);
    return 1;
}

BLOCK_CIPHER_generic_pack(NID_aes, 128, 0)
    BLOCK_CIPHER_generic_pack(NID_aes, 192, 0)
    BLOCK_CIPHER_generic_pack(NID_aes, 256, 0)

static int aes_gcm_cleanup(EVP_CIPHER_CTX *c)
{
    EVP_AES_GCM_CTX *gctx = EVP_C_DATA(EVP_AES_GCM_CTX,c);
    if (gctx == NULL)
        return 0;
    OPENSSL_cleanse(&gctx->gcm, sizeof(gctx->gcm));
    if (gctx->iv != EVP_CIPHER_CTX_iv_noconst(c))
        OPENSSL_free(gctx->iv);
    return 1;
}

static int aes_gcm_ctrl(EVP_CIPHER_CTX *c, int type, int arg, void *ptr)
{
    EVP_AES_GCM_CTX *gctx = EVP_C_DATA(EVP_AES_GCM_CTX,c);
    switch (type) {
    case EVP_CTRL_INIT:
        gctx->key_set = 0;
        gctx->iv_set = 0;
        gctx->ivlen = c->cipher->iv_len;
        gctx->iv = c->iv;
        gctx->taglen = -1;
        gctx->iv_gen = 0;
        gctx->tls_aad_len = -1;
        return 1;

    case EVP_CTRL_AEAD_SET_IVLEN:
        if (arg <= 0)
            return 0;
        /* Allocate memory for IV if needed */
        if ((arg > EVP_MAX_IV_LENGTH) && (arg > gctx->ivlen)) {
            if (gctx->iv != c->iv)
                OPENSSL_free(gctx->iv);
            if ((gctx->iv = OPENSSL_malloc(arg)) == NULL) {
                EVPerr(EVP_F_AES_GCM_CTRL, ERR_R_MALLOC_FAILURE);
                return 0;
            }
        }
        gctx->ivlen = arg;
        return 1;

    case EVP_CTRL_AEAD_SET_TAG:
        if (arg <= 0 || arg > 16 || c->encrypt)
            return 0;
        memcpy(c->buf, ptr, arg);
        gctx->taglen = arg;
        return 1;

    case EVP_CTRL_AEAD_GET_TAG:
        if (arg <= 0 || arg > 16 || !c->encrypt
            || gctx->taglen < 0)
            return 0;
        memcpy(ptr, c->buf, arg);
        return 1;

    case EVP_CTRL_GCM_SET_IV_FIXED:
        /* Special case: -1 length restores whole IV */
        if (arg == -1) {
            memcpy(gctx->iv, ptr, gctx->ivlen);
            gctx->iv_gen = 1;
            return 1;
        }
        /*
         * Fixed field must be at least 4 bytes and invocation field at least
         * 8.
         */
        if ((arg < 4) || (gctx->ivlen - arg) < 8)
            return 0;
        if (arg)
            memcpy(gctx->iv, ptr, arg);
        if (c->encrypt && RAND_bytes(gctx->iv + arg, gctx->ivlen - arg) <= 0)
            return 0;
        gctx->iv_gen = 1;
        return 1;

    case EVP_CTRL_GCM_IV_GEN:
        if (gctx->iv_gen == 0 || gctx->key_set == 0)
            return 0;
        CRYPTO_gcm128_setiv(&gctx->gcm, gctx->iv, gctx->ivlen);
        if (arg <= 0 || arg > gctx->ivlen)
            arg = gctx->ivlen;
        memcpy(ptr, gctx->iv + gctx->ivlen - arg, arg);
        /*
         * Invocation field will be at least 8 bytes in size and so no need
         * to check wrap around or increment more than last 8 bytes.
         */
        ctr64_inc(gctx->iv + gctx->ivlen - 8);
        gctx->iv_set = 1;
        return 1;

    case EVP_CTRL_GCM_SET_IV_INV:
        if (gctx->iv_gen == 0 || gctx->key_set == 0 || c->encrypt)
            return 0;
        memcpy(gctx->iv + gctx->ivlen - arg, ptr, arg);
        CRYPTO_gcm128_setiv(&gctx->gcm, gctx->iv, gctx->ivlen);
        gctx->iv_set = 1;
        return 1;

    case EVP_CTRL_AEAD_TLS1_AAD:
        /* Save the AAD for later use */
        if (arg != EVP_AEAD_TLS1_AAD_LEN)
            return 0;
        memcpy(c->buf, ptr, arg);
        gctx->tls_aad_len = arg;
        {
            unsigned int len = c->buf[arg - 2] << 8 | c->buf[arg - 1];
            /* Correct length for explicit IV */
            if (len < EVP_GCM_TLS_EXPLICIT_IV_LEN)
                return 0;
            len -= EVP_GCM_TLS_EXPLICIT_IV_LEN;
            /* If decrypting correct for tag too */
            if (!c->encrypt) {
                if (len < EVP_GCM_TLS_TAG_LEN)
                    return 0;
                len -= EVP_GCM_TLS_TAG_LEN;
            }
            c->buf[arg - 2] = len >> 8;
            c->buf[arg - 1] = len & 0xff;
        }
        /* Extra padding: tag appended to record */
        return EVP_GCM_TLS_TAG_LEN;

    case EVP_CTRL_COPY:
        {
            EVP_CIPHER_CTX *out = ptr;
            EVP_AES_GCM_CTX *gctx_out = EVP_C_DATA(EVP_AES_GCM_CTX,out);
            if (gctx->gcm.key) {
                if (gctx->gcm.key != &gctx->ks)
                    return 0;
                gctx_out->gcm.key = &gctx_out->ks;
            }
            if (gctx->iv == c->iv)
                gctx_out->iv = out->iv;
            else {
                if ((gctx_out->iv = OPENSSL_malloc(gctx->ivlen)) == NULL) {
                    EVPerr(EVP_F_AES_GCM_CTRL, ERR_R_MALLOC_FAILURE);
                    return 0;
                }
                memcpy(gctx_out->iv, gctx->iv, gctx->ivlen);
            }
            return 1;
        }

    default:
        return -1;

    }
}

static int aes_gcm_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                            const unsigned char *iv, int enc)
{
    EVP_AES_GCM_CTX *gctx = EVP_C_DATA(EVP_AES_GCM_CTX,ctx);
    if (!iv && !key)
        return 1;
    if (key) {
        do {
#ifdef HWAES_CAPABLE
            if (HWAES_CAPABLE) {
                HWAES_set_encrypt_key(key, ctx->key_len * 8, &gctx->ks.ks);
                CRYPTO_gcm128_init(&gctx->gcm, &gctx->ks,
                                   (block128_f) HWAES_encrypt);
# ifdef HWAES_ctr32_encrypt_blocks
                gctx->ctr = (ctr128_f) HWAES_ctr32_encrypt_blocks;
# else
                gctx->ctr = NULL;
# endif
                break;
            } else
#endif
#ifdef BSAES_CAPABLE
            if (BSAES_CAPABLE) {
                AES_set_encrypt_key(key, ctx->key_len * 8, &gctx->ks.ks);
                CRYPTO_gcm128_init(&gctx->gcm, &gctx->ks,
                                   (block128_f) AES_encrypt);
                gctx->ctr = (ctr128_f) bsaes_ctr32_encrypt_blocks;
                break;
            } else
#endif
#ifdef VPAES_CAPABLE
            if (VPAES_CAPABLE) {
                vpaes_set_encrypt_key(key, ctx->key_len * 8, &gctx->ks.ks);
                CRYPTO_gcm128_init(&gctx->gcm, &gctx->ks,
                                   (block128_f) vpaes_encrypt);
                gctx->ctr = NULL;
                break;
            } else
#endif
                (void)0;        /* terminate potentially open 'else' */

            AES_set_encrypt_key(key, ctx->key_len * 8, &gctx->ks.ks);
            CRYPTO_gcm128_init(&gctx->gcm, &gctx->ks,
                               (block128_f) AES_encrypt);
#ifdef AES_CTR_ASM
            gctx->ctr = (ctr128_f) AES_ctr32_encrypt;
#else
            gctx->ctr = NULL;
#endif
        } while (0);

        /*
         * If we have an iv can set it directly, otherwise use saved IV.
         */
        if (iv == NULL && gctx->iv_set)
            iv = gctx->iv;
        if (iv) {
            CRYPTO_gcm128_setiv(&gctx->gcm, iv, gctx->ivlen);
            gctx->iv_set = 1;
        }
        gctx->key_set = 1;
    } else {
        /* If key set use IV, otherwise copy */
        if (gctx->key_set)
            CRYPTO_gcm128_setiv(&gctx->gcm, iv, gctx->ivlen);
        else
            memcpy(gctx->iv, iv, gctx->ivlen);
        gctx->iv_set = 1;
        gctx->iv_gen = 0;
    }
    return 1;
}

/*
 * Handle TLS GCM packet format. This consists of the last portion of the IV
 * followed by the payload and finally the tag. On encrypt generate IV,
 * encrypt payload and write the tag. On verify retrieve IV, decrypt payload
 * and verify tag.
 */

static int aes_gcm_tls_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                              const unsigned char *in, size_t len)
{
    EVP_AES_GCM_CTX *gctx = EVP_C_DATA(EVP_AES_GCM_CTX,ctx);
    int rv = -1;
    /* Encrypt/decrypt must be performed in place */
    if (out != in
        || len < (EVP_GCM_TLS_EXPLICIT_IV_LEN + EVP_GCM_TLS_TAG_LEN))
        return -1;
    /*
     * Set IV from start of buffer or generate IV and write to start of
     * buffer.
     */
    if (EVP_CIPHER_CTX_ctrl(ctx, ctx->encrypt ? EVP_CTRL_GCM_IV_GEN
                                              : EVP_CTRL_GCM_SET_IV_INV,
                            EVP_GCM_TLS_EXPLICIT_IV_LEN, out) <= 0)
        goto err;
    /* Use saved AAD */
    if (CRYPTO_gcm128_aad(&gctx->gcm, ctx->buf, gctx->tls_aad_len))
        goto err;
    /* Fix buffer and length to point to payload */
    in += EVP_GCM_TLS_EXPLICIT_IV_LEN;
    out += EVP_GCM_TLS_EXPLICIT_IV_LEN;
    len -= EVP_GCM_TLS_EXPLICIT_IV_LEN + EVP_GCM_TLS_TAG_LEN;
    if (ctx->encrypt) {
        /* Encrypt payload */
        if (gctx->ctr) {
            size_t bulk = 0;
#if defined(AES_GCM_ASM)
            if (len >= 32 && AES_GCM_ASM(gctx)) {
                if (CRYPTO_gcm128_encrypt(&gctx->gcm, NULL, NULL, 0))
                    return -1;

                bulk = AES_gcm_encrypt(in, out, len,
                                       gctx->gcm.key,
                                       gctx->gcm.Yi.c, gctx->gcm.Xi.u);
                gctx->gcm.len.u[1] += bulk;
            }
#endif
            if (CRYPTO_gcm128_encrypt_ctr32(&gctx->gcm,
                                            in + bulk,
                                            out + bulk,
                                            len - bulk, gctx->ctr))
                goto err;
        } else {
            size_t bulk = 0;
#if defined(AES_GCM_ASM2)
            if (len >= 32 && AES_GCM_ASM2(gctx)) {
                if (CRYPTO_gcm128_encrypt(&gctx->gcm, NULL, NULL, 0))
                    return -1;

                bulk = AES_gcm_encrypt(in, out, len,
                                       gctx->gcm.key,
                                       gctx->gcm.Yi.c, gctx->gcm.Xi.u);
                gctx->gcm.len.u[1] += bulk;
            }
#endif
            if (CRYPTO_gcm128_encrypt(&gctx->gcm,
                                      in + bulk, out + bulk, len - bulk))
                goto err;
        }
        out += len;
        /* Finally write tag */
        CRYPTO_gcm128_tag(&gctx->gcm, out, EVP_GCM_TLS_TAG_LEN);
        rv = len + EVP_GCM_TLS_EXPLICIT_IV_LEN + EVP_GCM_TLS_TAG_LEN;
    } else {
        /* Decrypt */
        if (gctx->ctr) {
            size_t bulk = 0;
#if defined(AES_GCM_ASM)
            if (len >= 16 && AES_GCM_ASM(gctx)) {
                if (CRYPTO_gcm128_decrypt(&gctx->gcm, NULL, NULL, 0))
                    return -1;

                bulk = AES_gcm_decrypt(in, out, len,
                                       gctx->gcm.key,
                                       gctx->gcm.Yi.c, gctx->gcm.Xi.u);
                gctx->gcm.len.u[1] += bulk;
            }
#endif
            if (CRYPTO_gcm128_decrypt_ctr32(&gctx->gcm,
                                            in + bulk,
                                            out + bulk,
                                            len - bulk, gctx->ctr))
                goto err;
        } else {
            size_t bulk = 0;
#if defined(AES_GCM_ASM2)
            if (len >= 16 && AES_GCM_ASM2(gctx)) {
                if (CRYPTO_gcm128_decrypt(&gctx->gcm, NULL, NULL, 0))
                    return -1;

                bulk = AES_gcm_decrypt(in, out, len,
                                       gctx->gcm.key,
                                       gctx->gcm.Yi.c, gctx->gcm.Xi.u);
                gctx->gcm.len.u[1] += bulk;
            }
#endif
            if (CRYPTO_gcm128_decrypt(&gctx->gcm,
                                      in + bulk, out + bulk, len - bulk))
                goto err;
        }
        /* Retrieve tag */
        CRYPTO_gcm128_tag(&gctx->gcm, ctx->buf, EVP_GCM_TLS_TAG_LEN);
        /* If tag mismatch wipe buffer */
        if (CRYPTO_memcmp(ctx->buf, in + len, EVP_GCM_TLS_TAG_LEN)) {
            OPENSSL_cleanse(out, len);
            goto err;
        }
        rv = len;
    }

 err:
    gctx->iv_set = 0;
    gctx->tls_aad_len = -1;
    return rv;
}

static int aes_gcm_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                          const unsigned char *in, size_t len)
{
    EVP_AES_GCM_CTX *gctx = EVP_C_DATA(EVP_AES_GCM_CTX,ctx);
    /* If not set up, return error */
    if (!gctx->key_set)
        return -1;

    if (gctx->tls_aad_len >= 0)
        return aes_gcm_tls_cipher(ctx, out, in, len);

    if (!gctx->iv_set)
        return -1;
    if (in) {
        if (out == NULL) {
            if (CRYPTO_gcm128_aad(&gctx->gcm, in, len))
                return -1;
        } else if (ctx->encrypt) {
            if (gctx->ctr) {
                size_t bulk = 0;
#if defined(AES_GCM_ASM)
                if (len >= 32 && AES_GCM_ASM(gctx)) {
                    size_t res = (16 - gctx->gcm.mres) % 16;

                    if (CRYPTO_gcm128_encrypt(&gctx->gcm, in, out, res))
                        return -1;

                    bulk = AES_gcm_encrypt(in + res,
                                           out + res, len - res,
                                           gctx->gcm.key, gctx->gcm.Yi.c,
                                           gctx->gcm.Xi.u);
                    gctx->gcm.len.u[1] += bulk;
                    bulk += res;
                }
#endif
                if (CRYPTO_gcm128_encrypt_ctr32(&gctx->gcm,
                                                in + bulk,
                                                out + bulk,
                                                len - bulk, gctx->ctr))
                    return -1;
            } else {
                size_t bulk = 0;
#if defined(AES_GCM_ASM2)
                if (len >= 32 && AES_GCM_ASM2(gctx)) {
                    size_t res = (16 - gctx->gcm.mres) % 16;

                    if (CRYPTO_gcm128_encrypt(&gctx->gcm, in, out, res))
                        return -1;

                    bulk = AES_gcm_encrypt(in + res,
                                           out + res, len - res,
                                           gctx->gcm.key, gctx->gcm.Yi.c,
                                           gctx->gcm.Xi.u);
                    gctx->gcm.len.u[1] += bulk;
                    bulk += res;
                }
#endif
                if (CRYPTO_gcm128_encrypt(&gctx->gcm,
                                          in + bulk, out + bulk, len - bulk))
                    return -1;
            }
        } else {
            if (gctx->ctr) {
                size_t bulk = 0;
#if defined(AES_GCM_ASM)
                if (len >= 16 && AES_GCM_ASM(gctx)) {
                    size_t res = (16 - gctx->gcm.mres) % 16;

                    if (CRYPTO_gcm128_decrypt(&gctx->gcm, in, out, res))
                        return -1;

                    bulk = AES_gcm_decrypt(in + res,
                                           out + res, len - res,
                                           gctx->gcm.key,
                                           gctx->gcm.Yi.c, gctx->gcm.Xi.u);
                    gctx->gcm.len.u[1] += bulk;
                    bulk += res;
                }
#endif
                if (CRYPTO_gcm128_decrypt_ctr32(&gctx->gcm,
                                                in + bulk,
                                                out + bulk,
                                                len - bulk, gctx->ctr))
                    return -1;
            } else {
                size_t bulk = 0;
#if defined(AES_GCM_ASM2)
                if (len >= 16 && AES_GCM_ASM2(gctx)) {
                    size_t res = (16 - gctx->gcm.mres) % 16;

                    if (CRYPTO_gcm128_decrypt(&gctx->gcm, in, out, res))
                        return -1;

                    bulk = AES_gcm_decrypt(in + res,
                                           out + res, len - res,
                                           gctx->gcm.key,
                                           gctx->gcm.Yi.c, gctx->gcm.Xi.u);
                    gctx->gcm.len.u[1] += bulk;
                    bulk += res;
                }
#endif
                if (CRYPTO_gcm128_decrypt(&gctx->gcm,
                                          in + bulk, out + bulk, len - bulk))
                    return -1;
            }
        }
        return len;
    } else {
        if (!ctx->encrypt) {
            if (gctx->taglen < 0)
                return -1;
            if (CRYPTO_gcm128_finish(&gctx->gcm, ctx->buf, gctx->taglen) != 0)
                return -1;
            gctx->iv_set = 0;
            return 0;
        }
        CRYPTO_gcm128_tag(&gctx->gcm, ctx->buf, 16);
        gctx->taglen = 16;
        /* Don't reuse the IV */
        gctx->iv_set = 0;
        return 0;
    }

}

#define CUSTOM_FLAGS    (EVP_CIPH_FLAG_DEFAULT_ASN1 \
                | EVP_CIPH_CUSTOM_IV | EVP_CIPH_FLAG_CUSTOM_CIPHER \
                | EVP_CIPH_ALWAYS_CALL_INIT | EVP_CIPH_CTRL_INIT \
                | EVP_CIPH_CUSTOM_COPY)

BLOCK_CIPHER_custom(NID_aes, 128, 1, 12, gcm, GCM,
                    EVP_CIPH_FLAG_AEAD_CIPHER | CUSTOM_FLAGS)
    BLOCK_CIPHER_custom(NID_aes, 192, 1, 12, gcm, GCM,
                    EVP_CIPH_FLAG_AEAD_CIPHER | CUSTOM_FLAGS)
    BLOCK_CIPHER_custom(NID_aes, 256, 1, 12, gcm, GCM,
                    EVP_CIPH_FLAG_AEAD_CIPHER | CUSTOM_FLAGS)

static int aes_xts_ctrl(EVP_CIPHER_CTX *c, int type, int arg, void *ptr)
{
    EVP_AES_XTS_CTX *xctx = EVP_C_DATA(EVP_AES_XTS_CTX,c);
    if (type == EVP_CTRL_COPY) {
        EVP_CIPHER_CTX *out = ptr;
        EVP_AES_XTS_CTX *xctx_out = EVP_C_DATA(EVP_AES_XTS_CTX,out);
        if (xctx->xts.key1) {
            if (xctx->xts.key1 != &xctx->ks1)
                return 0;
            xctx_out->xts.key1 = &xctx_out->ks1;
        }
        if (xctx->xts.key2) {
            if (xctx->xts.key2 != &xctx->ks2)
                return 0;
            xctx_out->xts.key2 = &xctx_out->ks2;
        }
        return 1;
    } else if (type != EVP_CTRL_INIT)
        return -1;
    /* key1 and key2 are used as an indicator both key and IV are set */
    xctx->xts.key1 = NULL;
    xctx->xts.key2 = NULL;
    return 1;
}

static int aes_xts_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                            const unsigned char *iv, int enc)
{
    EVP_AES_XTS_CTX *xctx = EVP_C_DATA(EVP_AES_XTS_CTX,ctx);
    if (!iv && !key)
        return 1;

    if (key)
        do {
#ifdef AES_XTS_ASM
            xctx->stream = enc ? AES_xts_encrypt : AES_xts_decrypt;
#else
            xctx->stream = NULL;
#endif
            /* key_len is two AES keys */
#ifdef HWAES_CAPABLE
            if (HWAES_CAPABLE) {
                if (enc) {
                    HWAES_set_encrypt_key(key,
                                          EVP_CIPHER_CTX_key_length(ctx) * 4,
                                          &xctx->ks1.ks);
                    xctx->xts.block1 = (block128_f) HWAES_encrypt;
# ifdef HWAES_xts_encrypt
                    xctx->stream = HWAES_xts_encrypt;
# endif
                } else {
                    HWAES_set_decrypt_key(key,
                                          EVP_CIPHER_CTX_key_length(ctx) * 4,
                                          &xctx->ks1.ks);
                    xctx->xts.block1 = (block128_f) HWAES_decrypt;
# ifdef HWAES_xts_decrypt
                    xctx->stream = HWAES_xts_decrypt;
#endif
                }

                HWAES_set_encrypt_key(key + EVP_CIPHER_CTX_key_length(ctx) / 2,
                                      EVP_CIPHER_CTX_key_length(ctx) * 4,
                                      &xctx->ks2.ks);
                xctx->xts.block2 = (block128_f) HWAES_encrypt;

                xctx->xts.key1 = &xctx->ks1;
                break;
            } else
#endif
#ifdef BSAES_CAPABLE
            if (BSAES_CAPABLE)
                xctx->stream = enc ? bsaes_xts_encrypt : bsaes_xts_decrypt;
            else
#endif
#ifdef VPAES_CAPABLE
            if (VPAES_CAPABLE) {
                if (enc) {
                    vpaes_set_encrypt_key(key,
                                          EVP_CIPHER_CTX_key_length(ctx) * 4,
                                          &xctx->ks1.ks);
                    xctx->xts.block1 = (block128_f) vpaes_encrypt;
                } else {
                    vpaes_set_decrypt_key(key,
                                          EVP_CIPHER_CTX_key_length(ctx) * 4,
                                          &xctx->ks1.ks);
                    xctx->xts.block1 = (block128_f) vpaes_decrypt;
                }

                vpaes_set_encrypt_key(key + EVP_CIPHER_CTX_key_length(ctx) / 2,
                                      EVP_CIPHER_CTX_key_length(ctx) * 4,
                                      &xctx->ks2.ks);
                xctx->xts.block2 = (block128_f) vpaes_encrypt;

                xctx->xts.key1 = &xctx->ks1;
                break;
            } else
#endif
                (void)0;        /* terminate potentially open 'else' */

            if (enc) {
                AES_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 4,
                                    &xctx->ks1.ks);
                xctx->xts.block1 = (block128_f) AES_encrypt;
            } else {
                AES_set_decrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 4,
                                    &xctx->ks1.ks);
                xctx->xts.block1 = (block128_f) AES_decrypt;
            }

            AES_set_encrypt_key(key + EVP_CIPHER_CTX_key_length(ctx) / 2,
                                EVP_CIPHER_CTX_key_length(ctx) * 4,
                                &xctx->ks2.ks);
            xctx->xts.block2 = (block128_f) AES_encrypt;

            xctx->xts.key1 = &xctx->ks1;
        } while (0);

    if (iv) {
        xctx->xts.key2 = &xctx->ks2;
        memcpy(EVP_CIPHER_CTX_iv_noconst(ctx), iv, 16);
    }

    return 1;
}

static int aes_xts_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                          const unsigned char *in, size_t len)
{
    EVP_AES_XTS_CTX *xctx = EVP_C_DATA(EVP_AES_XTS_CTX,ctx);
    if (!xctx->xts.key1 || !xctx->xts.key2)
        return 0;
    if (!out || !in || len < AES_BLOCK_SIZE)
        return 0;
    if (xctx->stream)
        (*xctx->stream) (in, out, len,
                         xctx->xts.key1, xctx->xts.key2,
                         EVP_CIPHER_CTX_iv_noconst(ctx));
    else if (CRYPTO_xts128_encrypt(&xctx->xts, EVP_CIPHER_CTX_iv_noconst(ctx),
                                   in, out, len,
                                   EVP_CIPHER_CTX_encrypting(ctx)))
        return 0;
    return 1;
}

#define aes_xts_cleanup NULL

#define XTS_FLAGS       (EVP_CIPH_FLAG_DEFAULT_ASN1 | EVP_CIPH_CUSTOM_IV \
                         | EVP_CIPH_ALWAYS_CALL_INIT | EVP_CIPH_CTRL_INIT \
                         | EVP_CIPH_CUSTOM_COPY)

BLOCK_CIPHER_custom(NID_aes, 128, 1, 16, xts, XTS, XTS_FLAGS)
    BLOCK_CIPHER_custom(NID_aes, 256, 1, 16, xts, XTS, XTS_FLAGS)

static int aes_ccm_ctrl(EVP_CIPHER_CTX *c, int type, int arg, void *ptr)
{
    EVP_AES_CCM_CTX *cctx = EVP_C_DATA(EVP_AES_CCM_CTX,c);
    switch (type) {
    case EVP_CTRL_INIT:
        cctx->key_set = 0;
        cctx->iv_set = 0;
        cctx->L = 8;
        cctx->M = 12;
        cctx->tag_set = 0;
        cctx->len_set = 0;
        cctx->tls_aad_len = -1;
        return 1;

    case EVP_CTRL_AEAD_TLS1_AAD:
        /* Save the AAD for later use */
        if (arg != EVP_AEAD_TLS1_AAD_LEN)
            return 0;
        memcpy(EVP_CIPHER_CTX_buf_noconst(c), ptr, arg);
        cctx->tls_aad_len = arg;
        {
            uint16_t len =
                EVP_CIPHER_CTX_buf_noconst(c)[arg - 2] << 8
                | EVP_CIPHER_CTX_buf_noconst(c)[arg - 1];
            /* Correct length for explicit IV */
            if (len < EVP_CCM_TLS_EXPLICIT_IV_LEN)
                return 0;
            len -= EVP_CCM_TLS_EXPLICIT_IV_LEN;
            /* If decrypting correct for tag too */
            if (!EVP_CIPHER_CTX_encrypting(c)) {
                if (len < cctx->M)
                    return 0;
                len -= cctx->M;
            }
            EVP_CIPHER_CTX_buf_noconst(c)[arg - 2] = len >> 8;
            EVP_CIPHER_CTX_buf_noconst(c)[arg - 1] = len & 0xff;
        }
        /* Extra padding: tag appended to record */
        return cctx->M;

    case EVP_CTRL_CCM_SET_IV_FIXED:
        /* Sanity check length */
        if (arg != EVP_CCM_TLS_FIXED_IV_LEN)
            return 0;
        /* Just copy to first part of IV */
        memcpy(EVP_CIPHER_CTX_iv_noconst(c), ptr, arg);
        return 1;

    case EVP_CTRL_AEAD_SET_IVLEN:
        arg = 15 - arg;
        /* fall thru */
    case EVP_CTRL_CCM_SET_L:
        if (arg < 2 || arg > 8)
            return 0;
        cctx->L = arg;
        return 1;

    case EVP_CTRL_AEAD_SET_TAG:
        if ((arg & 1) || arg < 4 || arg > 16)
            return 0;
        if (EVP_CIPHER_CTX_encrypting(c) && ptr)
            return 0;
        if (ptr) {
            cctx->tag_set = 1;
            memcpy(EVP_CIPHER_CTX_buf_noconst(c), ptr, arg);
        }
        cctx->M = arg;
        return 1;

    case EVP_CTRL_AEAD_GET_TAG:
        if (!EVP_CIPHER_CTX_encrypting(c) || !cctx->tag_set)
            return 0;
        if (!CRYPTO_ccm128_tag(&cctx->ccm, ptr, (size_t)arg))
            return 0;
        cctx->tag_set = 0;
        cctx->iv_set = 0;
        cctx->len_set = 0;
        return 1;

    case EVP_CTRL_COPY:
        {
            EVP_CIPHER_CTX *out = ptr;
            EVP_AES_CCM_CTX *cctx_out = EVP_C_DATA(EVP_AES_CCM_CTX,out);
            if (cctx->ccm.key) {
                if (cctx->ccm.key != &cctx->ks)
                    return 0;
                cctx_out->ccm.key = &cctx_out->ks;
            }
            return 1;
        }

    default:
        return -1;

    }
}

static int aes_ccm_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                            const unsigned char *iv, int enc)
{
    EVP_AES_CCM_CTX *cctx = EVP_C_DATA(EVP_AES_CCM_CTX,ctx);
    if (!iv && !key)
        return 1;
    if (key)
        do {
#ifdef HWAES_CAPABLE
            if (HWAES_CAPABLE) {
                HWAES_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                      &cctx->ks.ks);

                CRYPTO_ccm128_init(&cctx->ccm, cctx->M, cctx->L,
                                   &cctx->ks, (block128_f) HWAES_encrypt);
                cctx->str = NULL;
                cctx->key_set = 1;
                break;
            } else
#endif
#ifdef VPAES_CAPABLE
            if (VPAES_CAPABLE) {
                vpaes_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                      &cctx->ks.ks);
                CRYPTO_ccm128_init(&cctx->ccm, cctx->M, cctx->L,
                                   &cctx->ks, (block128_f) vpaes_encrypt);
                cctx->str = NULL;
                cctx->key_set = 1;
                break;
            }
#endif
            AES_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                &cctx->ks.ks);
            CRYPTO_ccm128_init(&cctx->ccm, cctx->M, cctx->L,
                               &cctx->ks, (block128_f) AES_encrypt);
            cctx->str = NULL;
            cctx->key_set = 1;
        } while (0);
    if (iv) {
        memcpy(EVP_CIPHER_CTX_iv_noconst(ctx), iv, 15 - cctx->L);
        cctx->iv_set = 1;
    }
    return 1;
}

static int aes_ccm_tls_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                              const unsigned char *in, size_t len)
{
    EVP_AES_CCM_CTX *cctx = EVP_C_DATA(EVP_AES_CCM_CTX,ctx);
    CCM128_CONTEXT *ccm = &cctx->ccm;
    /* Encrypt/decrypt must be performed in place */
    if (out != in || len < (EVP_CCM_TLS_EXPLICIT_IV_LEN + (size_t)cctx->M))
        return -1;
    /* If encrypting set explicit IV from sequence number (start of AAD) */
    if (EVP_CIPHER_CTX_encrypting(ctx))
        memcpy(out, EVP_CIPHER_CTX_buf_noconst(ctx),
               EVP_CCM_TLS_EXPLICIT_IV_LEN);
    /* Get rest of IV from explicit IV */
    memcpy(EVP_CIPHER_CTX_iv_noconst(ctx) + EVP_CCM_TLS_FIXED_IV_LEN, in,
           EVP_CCM_TLS_EXPLICIT_IV_LEN);
    /* Correct length value */
    len -= EVP_CCM_TLS_EXPLICIT_IV_LEN + cctx->M;
    if (CRYPTO_ccm128_setiv(ccm, EVP_CIPHER_CTX_iv_noconst(ctx), 15 - cctx->L,
                            len))
            return -1;
    /* Use saved AAD */
    CRYPTO_ccm128_aad(ccm, EVP_CIPHER_CTX_buf_noconst(ctx), cctx->tls_aad_len);
    /* Fix buffer to point to payload */
    in += EVP_CCM_TLS_EXPLICIT_IV_LEN;
    out += EVP_CCM_TLS_EXPLICIT_IV_LEN;
    if (EVP_CIPHER_CTX_encrypting(ctx)) {
        if (cctx->str ? CRYPTO_ccm128_encrypt_ccm64(ccm, in, out, len,
                                                    cctx->str) :
            CRYPTO_ccm128_encrypt(ccm, in, out, len))
            return -1;
        if (!CRYPTO_ccm128_tag(ccm, out + len, cctx->M))
            return -1;
        return len + EVP_CCM_TLS_EXPLICIT_IV_LEN + cctx->M;
    } else {
        if (cctx->str ? !CRYPTO_ccm128_decrypt_ccm64(ccm, in, out, len,
                                                     cctx->str) :
            !CRYPTO_ccm128_decrypt(ccm, in, out, len)) {
            unsigned char tag[16];
            if (CRYPTO_ccm128_tag(ccm, tag, cctx->M)) {
                if (!CRYPTO_memcmp(tag, in + len, cctx->M))
                    return len;
            }
        }
        OPENSSL_cleanse(out, len);
        return -1;
    }
}

static int aes_ccm_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                          const unsigned char *in, size_t len)
{
    EVP_AES_CCM_CTX *cctx = EVP_C_DATA(EVP_AES_CCM_CTX,ctx);
    CCM128_CONTEXT *ccm = &cctx->ccm;
    /* If not set up, return error */
    if (!cctx->key_set)
        return -1;

    if (cctx->tls_aad_len >= 0)
        return aes_ccm_tls_cipher(ctx, out, in, len);

    /* EVP_*Final() doesn't return any data */
    if (in == NULL && out != NULL)
        return 0;

    if (!cctx->iv_set)
        return -1;

    if (!EVP_CIPHER_CTX_encrypting(ctx) && !cctx->tag_set)
        return -1;
    if (!out) {
        if (!in) {
            if (CRYPTO_ccm128_setiv(ccm, EVP_CIPHER_CTX_iv_noconst(ctx),
                                    15 - cctx->L, len))
                return -1;
            cctx->len_set = 1;
            return len;
        }
        /* If have AAD need message length */
        if (!cctx->len_set && len)
            return -1;
        CRYPTO_ccm128_aad(ccm, in, len);
        return len;
    }
    /* If not set length yet do it */
    if (!cctx->len_set) {
        if (CRYPTO_ccm128_setiv(ccm, EVP_CIPHER_CTX_iv_noconst(ctx),
                                15 - cctx->L, len))
            return -1;
        cctx->len_set = 1;
    }
    if (EVP_CIPHER_CTX_encrypting(ctx)) {
        if (cctx->str ? CRYPTO_ccm128_encrypt_ccm64(ccm, in, out, len,
                                                    cctx->str) :
            CRYPTO_ccm128_encrypt(ccm, in, out, len))
            return -1;
        cctx->tag_set = 1;
        return len;
    } else {
        int rv = -1;
        if (cctx->str ? !CRYPTO_ccm128_decrypt_ccm64(ccm, in, out, len,
                                                     cctx->str) :
            !CRYPTO_ccm128_decrypt(ccm, in, out, len)) {
            unsigned char tag[16];
            if (CRYPTO_ccm128_tag(ccm, tag, cctx->M)) {
                if (!CRYPTO_memcmp(tag, EVP_CIPHER_CTX_buf_noconst(ctx),
                                   cctx->M))
                    rv = len;
            }
        }
        if (rv == -1)
            OPENSSL_cleanse(out, len);
        cctx->iv_set = 0;
        cctx->tag_set = 0;
        cctx->len_set = 0;
        return rv;
    }
}

#define aes_ccm_cleanup NULL

BLOCK_CIPHER_custom(NID_aes, 128, 1, 12, ccm, CCM,
                    EVP_CIPH_FLAG_AEAD_CIPHER | CUSTOM_FLAGS)
    BLOCK_CIPHER_custom(NID_aes, 192, 1, 12, ccm, CCM,
                        EVP_CIPH_FLAG_AEAD_CIPHER | CUSTOM_FLAGS)
    BLOCK_CIPHER_custom(NID_aes, 256, 1, 12, ccm, CCM,
                        EVP_CIPH_FLAG_AEAD_CIPHER | CUSTOM_FLAGS)

typedef struct {
    union {
        double align;
        AES_KEY ks;
    } ks;
    /* Indicates if IV has been set */
    unsigned char *iv;
} EVP_AES_WRAP_CTX;

static int aes_wrap_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                             const unsigned char *iv, int enc)
{
    EVP_AES_WRAP_CTX *wctx = EVP_C_DATA(EVP_AES_WRAP_CTX,ctx);
    if (!iv && !key)
        return 1;
    if (key) {
        if (EVP_CIPHER_CTX_encrypting(ctx))
            AES_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                &wctx->ks.ks);
        else
            AES_set_decrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                &wctx->ks.ks);
        if (!iv)
            wctx->iv = NULL;
    }
    if (iv) {
        memcpy(EVP_CIPHER_CTX_iv_noconst(ctx), iv, EVP_CIPHER_CTX_iv_length(ctx));
        wctx->iv = EVP_CIPHER_CTX_iv_noconst(ctx);
    }
    return 1;
}

static int aes_wrap_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                           const unsigned char *in, size_t inlen)
{
    EVP_AES_WRAP_CTX *wctx = EVP_C_DATA(EVP_AES_WRAP_CTX,ctx);
    size_t rv;
    /* AES wrap with padding has IV length of 4, without padding 8 */
    int pad = EVP_CIPHER_CTX_iv_length(ctx) == 4;
    /* No final operation so always return zero length */
    if (!in)
        return 0;
    /* Input length must always be non-zero */
    if (!inlen)
        return -1;
    /* If decrypting need at least 16 bytes and multiple of 8 */
    if (!EVP_CIPHER_CTX_encrypting(ctx) && (inlen < 16 || inlen & 0x7))
        return -1;
    /* If not padding input must be multiple of 8 */
    if (!pad && inlen & 0x7)
        return -1;
    if (is_partially_overlapping(out, in, inlen)) {
        EVPerr(EVP_F_AES_WRAP_CIPHER, EVP_R_PARTIALLY_OVERLAPPING);
        return 0;
    }
    if (!out) {
        if (EVP_CIPHER_CTX_encrypting(ctx)) {
            /* If padding round up to multiple of 8 */
            if (pad)
                inlen = (inlen + 7) / 8 * 8;
            /* 8 byte prefix */
            return inlen + 8;
        } else {
            /*
             * If not padding output will be exactly 8 bytes smaller than
             * input. If padding it will be at least 8 bytes smaller but we
             * don't know how much.
             */
            return inlen - 8;
        }
    }
    if (pad) {
        if (EVP_CIPHER_CTX_encrypting(ctx))
            rv = CRYPTO_128_wrap_pad(&wctx->ks.ks, wctx->iv,
                                     out, in, inlen,
                                     (block128_f) AES_encrypt);
        else
            rv = CRYPTO_128_unwrap_pad(&wctx->ks.ks, wctx->iv,
                                       out, in, inlen,
                                       (block128_f) AES_decrypt);
    } else {
        if (EVP_CIPHER_CTX_encrypting(ctx))
            rv = CRYPTO_128_wrap(&wctx->ks.ks, wctx->iv,
                                 out, in, inlen, (block128_f) AES_encrypt);
        else
            rv = CRYPTO_128_unwrap(&wctx->ks.ks, wctx->iv,
                                   out, in, inlen, (block128_f) AES_decrypt);
    }
    return rv ? (int)rv : -1;
}

#define WRAP_FLAGS      (EVP_CIPH_WRAP_MODE \
                | EVP_CIPH_CUSTOM_IV | EVP_CIPH_FLAG_CUSTOM_CIPHER \
                | EVP_CIPH_ALWAYS_CALL_INIT | EVP_CIPH_FLAG_DEFAULT_ASN1)

static const EVP_CIPHER aes_128_wrap = {
    NID_id_aes128_wrap,
    8, 16, 8, WRAP_FLAGS,
    aes_wrap_init_key, aes_wrap_cipher,
    NULL,
    sizeof(EVP_AES_WRAP_CTX),
    NULL, NULL, NULL, NULL
};

const EVP_CIPHER *EVP_aes_128_wrap(void)
{
    return &aes_128_wrap;
}

static const EVP_CIPHER aes_192_wrap = {
    NID_id_aes192_wrap,
    8, 24, 8, WRAP_FLAGS,
    aes_wrap_init_key, aes_wrap_cipher,
    NULL,
    sizeof(EVP_AES_WRAP_CTX),
    NULL, NULL, NULL, NULL
};

const EVP_CIPHER *EVP_aes_192_wrap(void)
{
    return &aes_192_wrap;
}

static const EVP_CIPHER aes_256_wrap = {
    NID_id_aes256_wrap,
    8, 32, 8, WRAP_FLAGS,
    aes_wrap_init_key, aes_wrap_cipher,
    NULL,
    sizeof(EVP_AES_WRAP_CTX),
    NULL, NULL, NULL, NULL
};

const EVP_CIPHER *EVP_aes_256_wrap(void)
{
    return &aes_256_wrap;
}

static const EVP_CIPHER aes_128_wrap_pad = {
    NID_id_aes128_wrap_pad,
    8, 16, 4, WRAP_FLAGS,
    aes_wrap_init_key, aes_wrap_cipher,
    NULL,
    sizeof(EVP_AES_WRAP_CTX),
    NULL, NULL, NULL, NULL
};

const EVP_CIPHER *EVP_aes_128_wrap_pad(void)
{
    return &aes_128_wrap_pad;
}

static const EVP_CIPHER aes_192_wrap_pad = {
    NID_id_aes192_wrap_pad,
    8, 24, 4, WRAP_FLAGS,
    aes_wrap_init_key, aes_wrap_cipher,
    NULL,
    sizeof(EVP_AES_WRAP_CTX),
    NULL, NULL, NULL, NULL
};

const EVP_CIPHER *EVP_aes_192_wrap_pad(void)
{
    return &aes_192_wrap_pad;
}

static const EVP_CIPHER aes_256_wrap_pad = {
    NID_id_aes256_wrap_pad,
    8, 32, 4, WRAP_FLAGS,
    aes_wrap_init_key, aes_wrap_cipher,
    NULL,
    sizeof(EVP_AES_WRAP_CTX),
    NULL, NULL, NULL, NULL
};

const EVP_CIPHER *EVP_aes_256_wrap_pad(void)
{
    return &aes_256_wrap_pad;
}

#ifndef OPENSSL_NO_OCB
static int aes_ocb_ctrl(EVP_CIPHER_CTX *c, int type, int arg, void *ptr)
{
    EVP_AES_OCB_CTX *octx = EVP_C_DATA(EVP_AES_OCB_CTX,c);
    EVP_CIPHER_CTX *newc;
    EVP_AES_OCB_CTX *new_octx;

    switch (type) {
    case EVP_CTRL_INIT:
        octx->key_set = 0;
        octx->iv_set = 0;
        octx->ivlen = EVP_CIPHER_CTX_iv_length(c);
        octx->iv = EVP_CIPHER_CTX_iv_noconst(c);
        octx->taglen = 16;
        octx->data_buf_len = 0;
        octx->aad_buf_len = 0;
        return 1;

    case EVP_CTRL_AEAD_SET_IVLEN:
        /* IV len must be 1 to 15 */
        if (arg <= 0 || arg > 15)
            return 0;

        octx->ivlen = arg;
        return 1;

    case EVP_CTRL_AEAD_SET_TAG:
        if (!ptr) {
            /* Tag len must be 0 to 16 */
            if (arg < 0 || arg > 16)
                return 0;

            octx->taglen = arg;
            return 1;
        }
        if (arg != octx->taglen || EVP_CIPHER_CTX_encrypting(c))
            return 0;
        memcpy(octx->tag, ptr, arg);
        return 1;

    case EVP_CTRL_AEAD_GET_TAG:
        if (arg != octx->taglen || !EVP_CIPHER_CTX_encrypting(c))
            return 0;

        memcpy(ptr, octx->tag, arg);
        return 1;

    case EVP_CTRL_COPY:
        newc = (EVP_CIPHER_CTX *)ptr;
        new_octx = EVP_C_DATA(EVP_AES_OCB_CTX,newc);
        return CRYPTO_ocb128_copy_ctx(&new_octx->ocb, &octx->ocb,
                                      &new_octx->ksenc.ks,
                                      &new_octx->ksdec.ks);

    default:
        return -1;

    }
}

# ifdef HWAES_CAPABLE
#  ifdef HWAES_ocb_encrypt
void HWAES_ocb_encrypt(const unsigned char *in, unsigned char *out,
                       size_t blocks, const void *key,
                       size_t start_block_num,
                       unsigned char offset_i[16],
                       const unsigned char L_[][16],
                       unsigned char checksum[16]);
#  else
#    define HWAES_ocb_encrypt ((ocb128_f)NULL)
#  endif
#  ifdef HWAES_ocb_decrypt
void HWAES_ocb_decrypt(const unsigned char *in, unsigned char *out,
                       size_t blocks, const void *key,
                       size_t start_block_num,
                       unsigned char offset_i[16],
                       const unsigned char L_[][16],
                       unsigned char checksum[16]);
#  else
#    define HWAES_ocb_decrypt ((ocb128_f)NULL)
#  endif
# endif

static int aes_ocb_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                            const unsigned char *iv, int enc)
{
    EVP_AES_OCB_CTX *octx = EVP_C_DATA(EVP_AES_OCB_CTX,ctx);
    if (!iv && !key)
        return 1;
    if (key) {
        do {
            /*
             * We set both the encrypt and decrypt key here because decrypt
             * needs both. We could possibly optimise to remove setting the
             * decrypt for an encryption operation.
             */
# ifdef HWAES_CAPABLE
            if (HWAES_CAPABLE) {
                HWAES_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                      &octx->ksenc.ks);
                HWAES_set_decrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                      &octx->ksdec.ks);
                if (!CRYPTO_ocb128_init(&octx->ocb,
                                        &octx->ksenc.ks, &octx->ksdec.ks,
                                        (block128_f) HWAES_encrypt,
                                        (block128_f) HWAES_decrypt,
                                        enc ? HWAES_ocb_encrypt
                                            : HWAES_ocb_decrypt))
                    return 0;
                break;
            }
# endif
# ifdef VPAES_CAPABLE
            if (VPAES_CAPABLE) {
                vpaes_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                      &octx->ksenc.ks);
                vpaes_set_decrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                      &octx->ksdec.ks);
                if (!CRYPTO_ocb128_init(&octx->ocb,
                                        &octx->ksenc.ks, &octx->ksdec.ks,
                                        (block128_f) vpaes_encrypt,
                                        (block128_f) vpaes_decrypt,
                                        NULL))
                    return 0;
                break;
            }
# endif
            AES_set_encrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                &octx->ksenc.ks);
            AES_set_decrypt_key(key, EVP_CIPHER_CTX_key_length(ctx) * 8,
                                &octx->ksdec.ks);
            if (!CRYPTO_ocb128_init(&octx->ocb,
                                    &octx->ksenc.ks, &octx->ksdec.ks,
                                    (block128_f) AES_encrypt,
                                    (block128_f) AES_decrypt,
                                    NULL))
                return 0;
        }
        while (0);

        /*
         * If we have an iv we can set it directly, otherwise use saved IV.
         */
        if (iv == NULL && octx->iv_set)
            iv = octx->iv;
        if (iv) {
            if (CRYPTO_ocb128_setiv(&octx->ocb, iv, octx->ivlen, octx->taglen)
                != 1)
                return 0;
            octx->iv_set = 1;
        }
        octx->key_set = 1;
    } else {
        /* If key set use IV, otherwise copy */
        if (octx->key_set)
            CRYPTO_ocb128_setiv(&octx->ocb, iv, octx->ivlen, octx->taglen);
        else
            memcpy(octx->iv, iv, octx->ivlen);
        octx->iv_set = 1;
    }
    return 1;
}

static int aes_ocb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                          const unsigned char *in, size_t len)
{
    unsigned char *buf;
    int *buf_len;
    int written_len = 0;
    size_t trailing_len;
    EVP_AES_OCB_CTX *octx = EVP_C_DATA(EVP_AES_OCB_CTX,ctx);

    /* If IV or Key not set then return error */
    if (!octx->iv_set)
        return -1;

    if (!octx->key_set)
        return -1;

    if (in != NULL) {
        /*
         * Need to ensure we are only passing full blocks to low level OCB
         * routines. We do it here rather than in EVP_EncryptUpdate/
         * EVP_DecryptUpdate because we need to pass full blocks of AAD too
         * and those routines don't support that
         */

        /* Are we dealing with AAD or normal data here? */
        if (out == NULL) {
            buf = octx->aad_buf;
            buf_len = &(octx->aad_buf_len);
        } else {
            buf = octx->data_buf;
            buf_len = &(octx->data_buf_len);

            if (is_partially_overlapping(out + *buf_len, in, len)) {
                EVPerr(EVP_F_AES_OCB_CIPHER, EVP_R_PARTIALLY_OVERLAPPING);
                return 0;
            }
        }

        /*
         * If we've got a partially filled buffer from a previous call then
         * use that data first
         */
        if (*buf_len > 0) {
            unsigned int remaining;

            remaining = AES_BLOCK_SIZE - (*buf_len);
            if (remaining > len) {
                memcpy(buf + (*buf_len), in, len);
                *(buf_len) += len;
                return 0;
            }
            memcpy(buf + (*buf_len), in, remaining);

            /*
             * If we get here we've filled the buffer, so process it
             */
            len -= remaining;
            in += remaining;
            if (out == NULL) {
                if (!CRYPTO_ocb128_aad(&octx->ocb, buf, AES_BLOCK_SIZE))
                    return -1;
            } else if (EVP_CIPHER_CTX_encrypting(ctx)) {
                if (!CRYPTO_ocb128_encrypt(&octx->ocb, buf, out,
                                           AES_BLOCK_SIZE))
                    return -1;
            } else {
                if (!CRYPTO_ocb128_decrypt(&octx->ocb, buf, out,
                                           AES_BLOCK_SIZE))
                    return -1;
            }
            written_len = AES_BLOCK_SIZE;
            *buf_len = 0;
            if (out != NULL)
                out += AES_BLOCK_SIZE;
        }

        /* Do we have a partial block to handle at the end? */
        trailing_len = len % AES_BLOCK_SIZE;

        /*
         * If we've got some full blocks to handle, then process these first
         */
        if (len != trailing_len) {
            if (out == NULL) {
                if (!CRYPTO_ocb128_aad(&octx->ocb, in, len - trailing_len))
                    return -1;
            } else if (EVP_CIPHER_CTX_encrypting(ctx)) {
                if (!CRYPTO_ocb128_encrypt
                    (&octx->ocb, in, out, len - trailing_len))
                    return -1;
            } else {
                if (!CRYPTO_ocb128_decrypt
                    (&octx->ocb, in, out, len - trailing_len))
                    return -1;
            }
            written_len += len - trailing_len;
            in += len - trailing_len;
        }

        /* Handle any trailing partial block */
        if (trailing_len > 0) {
            memcpy(buf, in, trailing_len);
            *buf_len = trailing_len;
        }

        return written_len;
    } else {
        /*
         * First of all empty the buffer of any partial block that we might
         * have been provided - both for data and AAD
         */
        if (octx->data_buf_len > 0) {
            if (EVP_CIPHER_CTX_encrypting(ctx)) {
                if (!CRYPTO_ocb128_encrypt(&octx->ocb, octx->data_buf, out,
                                           octx->data_buf_len))
                    return -1;
            } else {
                if (!CRYPTO_ocb128_decrypt(&octx->ocb, octx->data_buf, out,
                                           octx->data_buf_len))
                    return -1;
            }
            written_len = octx->data_buf_len;
            octx->data_buf_len = 0;
        }
        if (octx->aad_buf_len > 0) {
            if (!CRYPTO_ocb128_aad
                (&octx->ocb, octx->aad_buf, octx->aad_buf_len))
                return -1;
            octx->aad_buf_len = 0;
        }
        /* If decrypting then verify */
        if (!EVP_CIPHER_CTX_encrypting(ctx)) {
            if (octx->taglen < 0)
                return -1;
            if (CRYPTO_ocb128_finish(&octx->ocb,
                                     octx->tag, octx->taglen) != 0)
                return -1;
            octx->iv_set = 0;
            return written_len;
        }
        /* If encrypting then just get the tag */
        if (CRYPTO_ocb128_tag(&octx->ocb, octx->tag, 16) != 1)
            return -1;
        /* Don't reuse the IV */
        octx->iv_set = 0;
        return written_len;
    }
}

static int aes_ocb_cleanup(EVP_CIPHER_CTX *c)
{
    EVP_AES_OCB_CTX *octx = EVP_C_DATA(EVP_AES_OCB_CTX,c);
    CRYPTO_ocb128_cleanup(&octx->ocb);
    return 1;
}

BLOCK_CIPHER_custom(NID_aes, 128, 16, 12, ocb, OCB,
                    EVP_CIPH_FLAG_AEAD_CIPHER | CUSTOM_FLAGS)
BLOCK_CIPHER_custom(NID_aes, 192, 16, 12, ocb, OCB,
                    EVP_CIPH_FLAG_AEAD_CIPHER | CUSTOM_FLAGS)
BLOCK_CIPHER_custom(NID_aes, 256, 16, 12, ocb, OCB,
                    EVP_CIPH_FLAG_AEAD_CIPHER | CUSTOM_FLAGS)
#endif                         /* OPENSSL_NO_OCB */
