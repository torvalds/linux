/*
 * Copyright 1995-2019 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright (c) 2002, Oracle and/or its affiliates. All rights reserved
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#undef SECONDS
#define SECONDS                 3
#define RSA_SECONDS             10
#define DSA_SECONDS             10
#define ECDSA_SECONDS   10
#define ECDH_SECONDS    10
#define EdDSA_SECONDS   10

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "apps.h"
#include "progs.h"
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/async.h>
#if !defined(OPENSSL_SYS_MSDOS)
# include OPENSSL_UNISTD
#endif

#if defined(_WIN32)
# include <windows.h>
#endif

#include <openssl/bn.h>
#ifndef OPENSSL_NO_DES
# include <openssl/des.h>
#endif
#include <openssl/aes.h>
#ifndef OPENSSL_NO_CAMELLIA
# include <openssl/camellia.h>
#endif
#ifndef OPENSSL_NO_MD2
# include <openssl/md2.h>
#endif
#ifndef OPENSSL_NO_MDC2
# include <openssl/mdc2.h>
#endif
#ifndef OPENSSL_NO_MD4
# include <openssl/md4.h>
#endif
#ifndef OPENSSL_NO_MD5
# include <openssl/md5.h>
#endif
#include <openssl/hmac.h>
#include <openssl/sha.h>
#ifndef OPENSSL_NO_RMD160
# include <openssl/ripemd.h>
#endif
#ifndef OPENSSL_NO_WHIRLPOOL
# include <openssl/whrlpool.h>
#endif
#ifndef OPENSSL_NO_RC4
# include <openssl/rc4.h>
#endif
#ifndef OPENSSL_NO_RC5
# include <openssl/rc5.h>
#endif
#ifndef OPENSSL_NO_RC2
# include <openssl/rc2.h>
#endif
#ifndef OPENSSL_NO_IDEA
# include <openssl/idea.h>
#endif
#ifndef OPENSSL_NO_SEED
# include <openssl/seed.h>
#endif
#ifndef OPENSSL_NO_BF
# include <openssl/blowfish.h>
#endif
#ifndef OPENSSL_NO_CAST
# include <openssl/cast.h>
#endif
#ifndef OPENSSL_NO_RSA
# include <openssl/rsa.h>
# include "./testrsa.h"
#endif
#include <openssl/x509.h>
#ifndef OPENSSL_NO_DSA
# include <openssl/dsa.h>
# include "./testdsa.h"
#endif
#ifndef OPENSSL_NO_EC
# include <openssl/ec.h>
#endif
#include <openssl/modes.h>

#ifndef HAVE_FORK
# if defined(OPENSSL_SYS_VMS) || defined(OPENSSL_SYS_WINDOWS) || defined(OPENSSL_SYS_VXWORKS)
#  define HAVE_FORK 0
# else
#  define HAVE_FORK 1
# endif
#endif

#if HAVE_FORK
# undef NO_FORK
#else
# define NO_FORK
#endif

#define MAX_MISALIGNMENT 63
#define MAX_ECDH_SIZE   256
#define MISALIGN        64

typedef struct openssl_speed_sec_st {
    int sym;
    int rsa;
    int dsa;
    int ecdsa;
    int ecdh;
    int eddsa;
} openssl_speed_sec_t;

static volatile int run = 0;

static int mr = 0;
static int usertime = 1;

#ifndef OPENSSL_NO_MD2
static int EVP_Digest_MD2_loop(void *args);
#endif

#ifndef OPENSSL_NO_MDC2
static int EVP_Digest_MDC2_loop(void *args);
#endif
#ifndef OPENSSL_NO_MD4
static int EVP_Digest_MD4_loop(void *args);
#endif
#ifndef OPENSSL_NO_MD5
static int MD5_loop(void *args);
static int HMAC_loop(void *args);
#endif
static int SHA1_loop(void *args);
static int SHA256_loop(void *args);
static int SHA512_loop(void *args);
#ifndef OPENSSL_NO_WHIRLPOOL
static int WHIRLPOOL_loop(void *args);
#endif
#ifndef OPENSSL_NO_RMD160
static int EVP_Digest_RMD160_loop(void *args);
#endif
#ifndef OPENSSL_NO_RC4
static int RC4_loop(void *args);
#endif
#ifndef OPENSSL_NO_DES
static int DES_ncbc_encrypt_loop(void *args);
static int DES_ede3_cbc_encrypt_loop(void *args);
#endif
static int AES_cbc_128_encrypt_loop(void *args);
static int AES_cbc_192_encrypt_loop(void *args);
static int AES_ige_128_encrypt_loop(void *args);
static int AES_cbc_256_encrypt_loop(void *args);
static int AES_ige_192_encrypt_loop(void *args);
static int AES_ige_256_encrypt_loop(void *args);
static int CRYPTO_gcm128_aad_loop(void *args);
static int RAND_bytes_loop(void *args);
static int EVP_Update_loop(void *args);
static int EVP_Update_loop_ccm(void *args);
static int EVP_Update_loop_aead(void *args);
static int EVP_Digest_loop(void *args);
#ifndef OPENSSL_NO_RSA
static int RSA_sign_loop(void *args);
static int RSA_verify_loop(void *args);
#endif
#ifndef OPENSSL_NO_DSA
static int DSA_sign_loop(void *args);
static int DSA_verify_loop(void *args);
#endif
#ifndef OPENSSL_NO_EC
static int ECDSA_sign_loop(void *args);
static int ECDSA_verify_loop(void *args);
static int EdDSA_sign_loop(void *args);
static int EdDSA_verify_loop(void *args);
#endif

static double Time_F(int s);
static void print_message(const char *s, long num, int length, int tm);
static void pkey_print_message(const char *str, const char *str2,
                               long num, unsigned int bits, int sec);
static void print_result(int alg, int run_no, int count, double time_used);
#ifndef NO_FORK
static int do_multi(int multi, int size_num);
#endif

static const int lengths_list[] = {
    16, 64, 256, 1024, 8 * 1024, 16 * 1024
};
static const int *lengths = lengths_list;

static const int aead_lengths_list[] = {
    2, 31, 136, 1024, 8 * 1024, 16 * 1024
};

#define START   0
#define STOP    1

#ifdef SIGALRM

static void alarmed(int sig)
{
    signal(SIGALRM, alarmed);
    run = 0;
}

static double Time_F(int s)
{
    double ret = app_tminterval(s, usertime);
    if (s == STOP)
        alarm(0);
    return ret;
}

#elif defined(_WIN32)

# define SIGALRM -1

static unsigned int lapse;
static volatile unsigned int schlock;
static void alarm_win32(unsigned int secs)
{
    lapse = secs * 1000;
}

# define alarm alarm_win32

static DWORD WINAPI sleepy(VOID * arg)
{
    schlock = 1;
    Sleep(lapse);
    run = 0;
    return 0;
}

static double Time_F(int s)
{
    double ret;
    static HANDLE thr;

    if (s == START) {
        schlock = 0;
        thr = CreateThread(NULL, 4096, sleepy, NULL, 0, NULL);
        if (thr == NULL) {
            DWORD err = GetLastError();
            BIO_printf(bio_err, "unable to CreateThread (%lu)", err);
            ExitProcess(err);
        }
        while (!schlock)
            Sleep(0);           /* scheduler spinlock */
        ret = app_tminterval(s, usertime);
    } else {
        ret = app_tminterval(s, usertime);
        if (run)
            TerminateThread(thr, 0);
        CloseHandle(thr);
    }

    return ret;
}
#else
static double Time_F(int s)
{
    return app_tminterval(s, usertime);
}
#endif

static void multiblock_speed(const EVP_CIPHER *evp_cipher, int lengths_single,
                             const openssl_speed_sec_t *seconds);

#define found(value, pairs, result)\
    opt_found(value, result, pairs, OSSL_NELEM(pairs))
static int opt_found(const char *name, unsigned int *result,
                     const OPT_PAIR pairs[], unsigned int nbelem)
{
    unsigned int idx;

    for (idx = 0; idx < nbelem; ++idx, pairs++)
        if (strcmp(name, pairs->name) == 0) {
            *result = pairs->retval;
            return 1;
        }
    return 0;
}

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_ELAPSED, OPT_EVP, OPT_DECRYPT, OPT_ENGINE, OPT_MULTI,
    OPT_MR, OPT_MB, OPT_MISALIGN, OPT_ASYNCJOBS, OPT_R_ENUM,
    OPT_PRIMES, OPT_SECONDS, OPT_BYTES, OPT_AEAD
} OPTION_CHOICE;

const OPTIONS speed_options[] = {
    {OPT_HELP_STR, 1, '-', "Usage: %s [options] ciphers...\n"},
    {OPT_HELP_STR, 1, '-', "Valid options are:\n"},
    {"help", OPT_HELP, '-', "Display this summary"},
    {"evp", OPT_EVP, 's', "Use EVP-named cipher or digest"},
    {"decrypt", OPT_DECRYPT, '-',
     "Time decryption instead of encryption (only EVP)"},
    {"aead", OPT_AEAD, '-',
     "Benchmark EVP-named AEAD cipher in TLS-like sequence"},
    {"mb", OPT_MB, '-',
     "Enable (tls1>=1) multi-block mode on EVP-named cipher"},
    {"mr", OPT_MR, '-', "Produce machine readable output"},
#ifndef NO_FORK
    {"multi", OPT_MULTI, 'p', "Run benchmarks in parallel"},
#endif
#ifndef OPENSSL_NO_ASYNC
    {"async_jobs", OPT_ASYNCJOBS, 'p',
     "Enable async mode and start specified number of jobs"},
#endif
    OPT_R_OPTIONS,
#ifndef OPENSSL_NO_ENGINE
    {"engine", OPT_ENGINE, 's', "Use engine, possibly a hardware device"},
#endif
    {"elapsed", OPT_ELAPSED, '-',
     "Use wall-clock time instead of CPU user time as divisor"},
    {"primes", OPT_PRIMES, 'p', "Specify number of primes (for RSA only)"},
    {"seconds", OPT_SECONDS, 'p',
     "Run benchmarks for specified amount of seconds"},
    {"bytes", OPT_BYTES, 'p',
     "Run [non-PKI] benchmarks on custom-sized buffer"},
    {"misalign", OPT_MISALIGN, 'p',
     "Use specified offset to mis-align buffers"},
    {NULL}
};

#define D_MD2           0
#define D_MDC2          1
#define D_MD4           2
#define D_MD5           3
#define D_HMAC          4
#define D_SHA1          5
#define D_RMD160        6
#define D_RC4           7
#define D_CBC_DES       8
#define D_EDE3_DES      9
#define D_CBC_IDEA      10
#define D_CBC_SEED      11
#define D_CBC_RC2       12
#define D_CBC_RC5       13
#define D_CBC_BF        14
#define D_CBC_CAST      15
#define D_CBC_128_AES   16
#define D_CBC_192_AES   17
#define D_CBC_256_AES   18
#define D_CBC_128_CML   19
#define D_CBC_192_CML   20
#define D_CBC_256_CML   21
#define D_EVP           22
#define D_SHA256        23
#define D_SHA512        24
#define D_WHIRLPOOL     25
#define D_IGE_128_AES   26
#define D_IGE_192_AES   27
#define D_IGE_256_AES   28
#define D_GHASH         29
#define D_RAND          30
/* name of algorithms to test */
static const char *names[] = {
    "md2", "mdc2", "md4", "md5", "hmac(md5)", "sha1", "rmd160", "rc4",
    "des cbc", "des ede3", "idea cbc", "seed cbc",
    "rc2 cbc", "rc5-32/12 cbc", "blowfish cbc", "cast cbc",
    "aes-128 cbc", "aes-192 cbc", "aes-256 cbc",
    "camellia-128 cbc", "camellia-192 cbc", "camellia-256 cbc",
    "evp", "sha256", "sha512", "whirlpool",
    "aes-128 ige", "aes-192 ige", "aes-256 ige", "ghash",
    "rand"
};
#define ALGOR_NUM       OSSL_NELEM(names)

/* list of configured algorithm (remaining) */
static const OPT_PAIR doit_choices[] = {
#ifndef OPENSSL_NO_MD2
    {"md2", D_MD2},
#endif
#ifndef OPENSSL_NO_MDC2
    {"mdc2", D_MDC2},
#endif
#ifndef OPENSSL_NO_MD4
    {"md4", D_MD4},
#endif
#ifndef OPENSSL_NO_MD5
    {"md5", D_MD5},
    {"hmac", D_HMAC},
#endif
    {"sha1", D_SHA1},
    {"sha256", D_SHA256},
    {"sha512", D_SHA512},
#ifndef OPENSSL_NO_WHIRLPOOL
    {"whirlpool", D_WHIRLPOOL},
#endif
#ifndef OPENSSL_NO_RMD160
    {"ripemd", D_RMD160},
    {"rmd160", D_RMD160},
    {"ripemd160", D_RMD160},
#endif
#ifndef OPENSSL_NO_RC4
    {"rc4", D_RC4},
#endif
#ifndef OPENSSL_NO_DES
    {"des-cbc", D_CBC_DES},
    {"des-ede3", D_EDE3_DES},
#endif
    {"aes-128-cbc", D_CBC_128_AES},
    {"aes-192-cbc", D_CBC_192_AES},
    {"aes-256-cbc", D_CBC_256_AES},
    {"aes-128-ige", D_IGE_128_AES},
    {"aes-192-ige", D_IGE_192_AES},
    {"aes-256-ige", D_IGE_256_AES},
#ifndef OPENSSL_NO_RC2
    {"rc2-cbc", D_CBC_RC2},
    {"rc2", D_CBC_RC2},
#endif
#ifndef OPENSSL_NO_RC5
    {"rc5-cbc", D_CBC_RC5},
    {"rc5", D_CBC_RC5},
#endif
#ifndef OPENSSL_NO_IDEA
    {"idea-cbc", D_CBC_IDEA},
    {"idea", D_CBC_IDEA},
#endif
#ifndef OPENSSL_NO_SEED
    {"seed-cbc", D_CBC_SEED},
    {"seed", D_CBC_SEED},
#endif
#ifndef OPENSSL_NO_BF
    {"bf-cbc", D_CBC_BF},
    {"blowfish", D_CBC_BF},
    {"bf", D_CBC_BF},
#endif
#ifndef OPENSSL_NO_CAST
    {"cast-cbc", D_CBC_CAST},
    {"cast", D_CBC_CAST},
    {"cast5", D_CBC_CAST},
#endif
    {"ghash", D_GHASH},
    {"rand", D_RAND}
};

static double results[ALGOR_NUM][OSSL_NELEM(lengths_list)];

#ifndef OPENSSL_NO_DSA
# define R_DSA_512       0
# define R_DSA_1024      1
# define R_DSA_2048      2
static const OPT_PAIR dsa_choices[] = {
    {"dsa512", R_DSA_512},
    {"dsa1024", R_DSA_1024},
    {"dsa2048", R_DSA_2048}
};
# define DSA_NUM         OSSL_NELEM(dsa_choices)

static double dsa_results[DSA_NUM][2];  /* 2 ops: sign then verify */
#endif  /* OPENSSL_NO_DSA */

#define R_RSA_512       0
#define R_RSA_1024      1
#define R_RSA_2048      2
#define R_RSA_3072      3
#define R_RSA_4096      4
#define R_RSA_7680      5
#define R_RSA_15360     6
#ifndef OPENSSL_NO_RSA
static const OPT_PAIR rsa_choices[] = {
    {"rsa512", R_RSA_512},
    {"rsa1024", R_RSA_1024},
    {"rsa2048", R_RSA_2048},
    {"rsa3072", R_RSA_3072},
    {"rsa4096", R_RSA_4096},
    {"rsa7680", R_RSA_7680},
    {"rsa15360", R_RSA_15360}
};
# define RSA_NUM OSSL_NELEM(rsa_choices)

static double rsa_results[RSA_NUM][2];  /* 2 ops: sign then verify */
#endif /* OPENSSL_NO_RSA */

#define R_EC_P160    0
#define R_EC_P192    1
#define R_EC_P224    2
#define R_EC_P256    3
#define R_EC_P384    4
#define R_EC_P521    5
#define R_EC_K163    6
#define R_EC_K233    7
#define R_EC_K283    8
#define R_EC_K409    9
#define R_EC_K571    10
#define R_EC_B163    11
#define R_EC_B233    12
#define R_EC_B283    13
#define R_EC_B409    14
#define R_EC_B571    15
#define R_EC_BRP256R1  16
#define R_EC_BRP256T1  17
#define R_EC_BRP384R1  18
#define R_EC_BRP384T1  19
#define R_EC_BRP512R1  20
#define R_EC_BRP512T1  21
#define R_EC_X25519  22
#define R_EC_X448    23
#ifndef OPENSSL_NO_EC
static OPT_PAIR ecdsa_choices[] = {
    {"ecdsap160", R_EC_P160},
    {"ecdsap192", R_EC_P192},
    {"ecdsap224", R_EC_P224},
    {"ecdsap256", R_EC_P256},
    {"ecdsap384", R_EC_P384},
    {"ecdsap521", R_EC_P521},
    {"ecdsak163", R_EC_K163},
    {"ecdsak233", R_EC_K233},
    {"ecdsak283", R_EC_K283},
    {"ecdsak409", R_EC_K409},
    {"ecdsak571", R_EC_K571},
    {"ecdsab163", R_EC_B163},
    {"ecdsab233", R_EC_B233},
    {"ecdsab283", R_EC_B283},
    {"ecdsab409", R_EC_B409},
    {"ecdsab571", R_EC_B571},
    {"ecdsabrp256r1", R_EC_BRP256R1},
    {"ecdsabrp256t1", R_EC_BRP256T1},
    {"ecdsabrp384r1", R_EC_BRP384R1},
    {"ecdsabrp384t1", R_EC_BRP384T1},
    {"ecdsabrp512r1", R_EC_BRP512R1},
    {"ecdsabrp512t1", R_EC_BRP512T1}
};
# define ECDSA_NUM       OSSL_NELEM(ecdsa_choices)

static double ecdsa_results[ECDSA_NUM][2];    /* 2 ops: sign then verify */

static const OPT_PAIR ecdh_choices[] = {
    {"ecdhp160", R_EC_P160},
    {"ecdhp192", R_EC_P192},
    {"ecdhp224", R_EC_P224},
    {"ecdhp256", R_EC_P256},
    {"ecdhp384", R_EC_P384},
    {"ecdhp521", R_EC_P521},
    {"ecdhk163", R_EC_K163},
    {"ecdhk233", R_EC_K233},
    {"ecdhk283", R_EC_K283},
    {"ecdhk409", R_EC_K409},
    {"ecdhk571", R_EC_K571},
    {"ecdhb163", R_EC_B163},
    {"ecdhb233", R_EC_B233},
    {"ecdhb283", R_EC_B283},
    {"ecdhb409", R_EC_B409},
    {"ecdhb571", R_EC_B571},
    {"ecdhbrp256r1", R_EC_BRP256R1},
    {"ecdhbrp256t1", R_EC_BRP256T1},
    {"ecdhbrp384r1", R_EC_BRP384R1},
    {"ecdhbrp384t1", R_EC_BRP384T1},
    {"ecdhbrp512r1", R_EC_BRP512R1},
    {"ecdhbrp512t1", R_EC_BRP512T1},
    {"ecdhx25519", R_EC_X25519},
    {"ecdhx448", R_EC_X448}
};
# define EC_NUM       OSSL_NELEM(ecdh_choices)

static double ecdh_results[EC_NUM][1];  /* 1 op: derivation */

#define R_EC_Ed25519    0
#define R_EC_Ed448      1
static OPT_PAIR eddsa_choices[] = {
    {"ed25519", R_EC_Ed25519},
    {"ed448", R_EC_Ed448}
};
# define EdDSA_NUM       OSSL_NELEM(eddsa_choices)

static double eddsa_results[EdDSA_NUM][2];    /* 2 ops: sign then verify */
#endif /* OPENSSL_NO_EC */

#ifndef SIGALRM
# define COND(d) (count < (d))
# define COUNT(d) (d)
#else
# define COND(unused_cond) (run && count<0x7fffffff)
# define COUNT(d) (count)
#endif                          /* SIGALRM */

typedef struct loopargs_st {
    ASYNC_JOB *inprogress_job;
    ASYNC_WAIT_CTX *wait_ctx;
    unsigned char *buf;
    unsigned char *buf2;
    unsigned char *buf_malloc;
    unsigned char *buf2_malloc;
    unsigned char *key;
    unsigned int siglen;
    size_t sigsize;
#ifndef OPENSSL_NO_RSA
    RSA *rsa_key[RSA_NUM];
#endif
#ifndef OPENSSL_NO_DSA
    DSA *dsa_key[DSA_NUM];
#endif
#ifndef OPENSSL_NO_EC
    EC_KEY *ecdsa[ECDSA_NUM];
    EVP_PKEY_CTX *ecdh_ctx[EC_NUM];
    EVP_MD_CTX *eddsa_ctx[EdDSA_NUM];
    unsigned char *secret_a;
    unsigned char *secret_b;
    size_t outlen[EC_NUM];
#endif
    EVP_CIPHER_CTX *ctx;
    HMAC_CTX *hctx;
    GCM128_CONTEXT *gcm_ctx;
} loopargs_t;
static int run_benchmark(int async_jobs, int (*loop_function) (void *),
                         loopargs_t * loopargs);

static unsigned int testnum;

/* Nb of iterations to do per algorithm and key-size */
static long c[ALGOR_NUM][OSSL_NELEM(lengths_list)];

#ifndef OPENSSL_NO_MD2
static int EVP_Digest_MD2_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char md2[MD2_DIGEST_LENGTH];
    int count;

    for (count = 0; COND(c[D_MD2][testnum]); count++) {
        if (!EVP_Digest(buf, (size_t)lengths[testnum], md2, NULL, EVP_md2(),
                        NULL))
            return -1;
    }
    return count;
}
#endif

#ifndef OPENSSL_NO_MDC2
static int EVP_Digest_MDC2_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char mdc2[MDC2_DIGEST_LENGTH];
    int count;

    for (count = 0; COND(c[D_MDC2][testnum]); count++) {
        if (!EVP_Digest(buf, (size_t)lengths[testnum], mdc2, NULL, EVP_mdc2(),
                        NULL))
            return -1;
    }
    return count;
}
#endif

#ifndef OPENSSL_NO_MD4
static int EVP_Digest_MD4_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char md4[MD4_DIGEST_LENGTH];
    int count;

    for (count = 0; COND(c[D_MD4][testnum]); count++) {
        if (!EVP_Digest(buf, (size_t)lengths[testnum], md4, NULL, EVP_md4(),
                        NULL))
            return -1;
    }
    return count;
}
#endif

#ifndef OPENSSL_NO_MD5
static int MD5_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char md5[MD5_DIGEST_LENGTH];
    int count;
    for (count = 0; COND(c[D_MD5][testnum]); count++)
        MD5(buf, lengths[testnum], md5);
    return count;
}

static int HMAC_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    HMAC_CTX *hctx = tempargs->hctx;
    unsigned char hmac[MD5_DIGEST_LENGTH];
    int count;

    for (count = 0; COND(c[D_HMAC][testnum]); count++) {
        HMAC_Init_ex(hctx, NULL, 0, NULL, NULL);
        HMAC_Update(hctx, buf, lengths[testnum]);
        HMAC_Final(hctx, hmac, NULL);
    }
    return count;
}
#endif

static int SHA1_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char sha[SHA_DIGEST_LENGTH];
    int count;
    for (count = 0; COND(c[D_SHA1][testnum]); count++)
        SHA1(buf, lengths[testnum], sha);
    return count;
}

static int SHA256_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char sha256[SHA256_DIGEST_LENGTH];
    int count;
    for (count = 0; COND(c[D_SHA256][testnum]); count++)
        SHA256(buf, lengths[testnum], sha256);
    return count;
}

static int SHA512_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char sha512[SHA512_DIGEST_LENGTH];
    int count;
    for (count = 0; COND(c[D_SHA512][testnum]); count++)
        SHA512(buf, lengths[testnum], sha512);
    return count;
}

#ifndef OPENSSL_NO_WHIRLPOOL
static int WHIRLPOOL_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char whirlpool[WHIRLPOOL_DIGEST_LENGTH];
    int count;
    for (count = 0; COND(c[D_WHIRLPOOL][testnum]); count++)
        WHIRLPOOL(buf, lengths[testnum], whirlpool);
    return count;
}
#endif

#ifndef OPENSSL_NO_RMD160
static int EVP_Digest_RMD160_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char rmd160[RIPEMD160_DIGEST_LENGTH];
    int count;
    for (count = 0; COND(c[D_RMD160][testnum]); count++) {
        if (!EVP_Digest(buf, (size_t)lengths[testnum], &(rmd160[0]),
                        NULL, EVP_ripemd160(), NULL))
            return -1;
    }
    return count;
}
#endif

#ifndef OPENSSL_NO_RC4
static RC4_KEY rc4_ks;
static int RC4_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    int count;
    for (count = 0; COND(c[D_RC4][testnum]); count++)
        RC4(&rc4_ks, (size_t)lengths[testnum], buf, buf);
    return count;
}
#endif

#ifndef OPENSSL_NO_DES
static unsigned char DES_iv[8];
static DES_key_schedule sch;
static DES_key_schedule sch2;
static DES_key_schedule sch3;
static int DES_ncbc_encrypt_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    int count;
    for (count = 0; COND(c[D_CBC_DES][testnum]); count++)
        DES_ncbc_encrypt(buf, buf, lengths[testnum], &sch,
                         &DES_iv, DES_ENCRYPT);
    return count;
}

static int DES_ede3_cbc_encrypt_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    int count;
    for (count = 0; COND(c[D_EDE3_DES][testnum]); count++)
        DES_ede3_cbc_encrypt(buf, buf, lengths[testnum],
                             &sch, &sch2, &sch3, &DES_iv, DES_ENCRYPT);
    return count;
}
#endif

#define MAX_BLOCK_SIZE 128

static unsigned char iv[2 * MAX_BLOCK_SIZE / 8];
static AES_KEY aes_ks1, aes_ks2, aes_ks3;
static int AES_cbc_128_encrypt_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    int count;
    for (count = 0; COND(c[D_CBC_128_AES][testnum]); count++)
        AES_cbc_encrypt(buf, buf,
                        (size_t)lengths[testnum], &aes_ks1, iv, AES_ENCRYPT);
    return count;
}

static int AES_cbc_192_encrypt_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    int count;
    for (count = 0; COND(c[D_CBC_192_AES][testnum]); count++)
        AES_cbc_encrypt(buf, buf,
                        (size_t)lengths[testnum], &aes_ks2, iv, AES_ENCRYPT);
    return count;
}

static int AES_cbc_256_encrypt_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    int count;
    for (count = 0; COND(c[D_CBC_256_AES][testnum]); count++)
        AES_cbc_encrypt(buf, buf,
                        (size_t)lengths[testnum], &aes_ks3, iv, AES_ENCRYPT);
    return count;
}

static int AES_ige_128_encrypt_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char *buf2 = tempargs->buf2;
    int count;
    for (count = 0; COND(c[D_IGE_128_AES][testnum]); count++)
        AES_ige_encrypt(buf, buf2,
                        (size_t)lengths[testnum], &aes_ks1, iv, AES_ENCRYPT);
    return count;
}

static int AES_ige_192_encrypt_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char *buf2 = tempargs->buf2;
    int count;
    for (count = 0; COND(c[D_IGE_192_AES][testnum]); count++)
        AES_ige_encrypt(buf, buf2,
                        (size_t)lengths[testnum], &aes_ks2, iv, AES_ENCRYPT);
    return count;
}

static int AES_ige_256_encrypt_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char *buf2 = tempargs->buf2;
    int count;
    for (count = 0; COND(c[D_IGE_256_AES][testnum]); count++)
        AES_ige_encrypt(buf, buf2,
                        (size_t)lengths[testnum], &aes_ks3, iv, AES_ENCRYPT);
    return count;
}

static int CRYPTO_gcm128_aad_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    GCM128_CONTEXT *gcm_ctx = tempargs->gcm_ctx;
    int count;
    for (count = 0; COND(c[D_GHASH][testnum]); count++)
        CRYPTO_gcm128_aad(gcm_ctx, buf, lengths[testnum]);
    return count;
}

static int RAND_bytes_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    int count;

    for (count = 0; COND(c[D_RAND][testnum]); count++)
        RAND_bytes(buf, lengths[testnum]);
    return count;
}

static long save_count = 0;
static int decrypt = 0;
static int EVP_Update_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    EVP_CIPHER_CTX *ctx = tempargs->ctx;
    int outl, count, rc;
#ifndef SIGALRM
    int nb_iter = save_count * 4 * lengths[0] / lengths[testnum];
#endif
    if (decrypt) {
        for (count = 0; COND(nb_iter); count++) {
            rc = EVP_DecryptUpdate(ctx, buf, &outl, buf, lengths[testnum]);
            if (rc != 1) {
                /* reset iv in case of counter overflow */
                EVP_CipherInit_ex(ctx, NULL, NULL, NULL, iv, -1);
            }
        }
    } else {
        for (count = 0; COND(nb_iter); count++) {
            rc = EVP_EncryptUpdate(ctx, buf, &outl, buf, lengths[testnum]);
            if (rc != 1) {
                /* reset iv in case of counter overflow */
                EVP_CipherInit_ex(ctx, NULL, NULL, NULL, iv, -1);
            }
        }
    }
    if (decrypt)
        EVP_DecryptFinal_ex(ctx, buf, &outl);
    else
        EVP_EncryptFinal_ex(ctx, buf, &outl);
    return count;
}

/*
 * CCM does not support streaming. For the purpose of performance measurement,
 * each message is encrypted using the same (key,iv)-pair. Do not use this
 * code in your application.
 */
static int EVP_Update_loop_ccm(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    EVP_CIPHER_CTX *ctx = tempargs->ctx;
    int outl, count;
    unsigned char tag[12];
#ifndef SIGALRM
    int nb_iter = save_count * 4 * lengths[0] / lengths[testnum];
#endif
    if (decrypt) {
        for (count = 0; COND(nb_iter); count++) {
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, sizeof(tag), tag);
            /* reset iv */
            EVP_DecryptInit_ex(ctx, NULL, NULL, NULL, iv);
            /* counter is reset on every update */
            EVP_DecryptUpdate(ctx, buf, &outl, buf, lengths[testnum]);
        }
    } else {
        for (count = 0; COND(nb_iter); count++) {
            /* restore iv length field */
            EVP_EncryptUpdate(ctx, NULL, &outl, NULL, lengths[testnum]);
            /* counter is reset on every update */
            EVP_EncryptUpdate(ctx, buf, &outl, buf, lengths[testnum]);
        }
    }
    if (decrypt)
        EVP_DecryptFinal_ex(ctx, buf, &outl);
    else
        EVP_EncryptFinal_ex(ctx, buf, &outl);
    return count;
}

/*
 * To make AEAD benchmarking more relevant perform TLS-like operations,
 * 13-byte AAD followed by payload. But don't use TLS-formatted AAD, as
 * payload length is not actually limited by 16KB...
 */
static int EVP_Update_loop_aead(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    EVP_CIPHER_CTX *ctx = tempargs->ctx;
    int outl, count;
    unsigned char aad[13] = { 0xcc };
    unsigned char faketag[16] = { 0xcc };
#ifndef SIGALRM
    int nb_iter = save_count * 4 * lengths[0] / lengths[testnum];
#endif
    if (decrypt) {
        for (count = 0; COND(nb_iter); count++) {
            EVP_DecryptInit_ex(ctx, NULL, NULL, NULL, iv);
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG,
                                sizeof(faketag), faketag);
            EVP_DecryptUpdate(ctx, NULL, &outl, aad, sizeof(aad));
            EVP_DecryptUpdate(ctx, buf, &outl, buf, lengths[testnum]);
            EVP_DecryptFinal_ex(ctx, buf + outl, &outl);
        }
    } else {
        for (count = 0; COND(nb_iter); count++) {
            EVP_EncryptInit_ex(ctx, NULL, NULL, NULL, iv);
            EVP_EncryptUpdate(ctx, NULL, &outl, aad, sizeof(aad));
            EVP_EncryptUpdate(ctx, buf, &outl, buf, lengths[testnum]);
            EVP_EncryptFinal_ex(ctx, buf + outl, &outl);
        }
    }
    return count;
}

static const EVP_MD *evp_md = NULL;
static int EVP_Digest_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char md[EVP_MAX_MD_SIZE];
    int count;
#ifndef SIGALRM
    int nb_iter = save_count * 4 * lengths[0] / lengths[testnum];
#endif

    for (count = 0; COND(nb_iter); count++) {
        if (!EVP_Digest(buf, lengths[testnum], md, NULL, evp_md, NULL))
            return -1;
    }
    return count;
}

#ifndef OPENSSL_NO_RSA
static long rsa_c[RSA_NUM][2];  /* # RSA iteration test */

static int RSA_sign_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char *buf2 = tempargs->buf2;
    unsigned int *rsa_num = &tempargs->siglen;
    RSA **rsa_key = tempargs->rsa_key;
    int ret, count;
    for (count = 0; COND(rsa_c[testnum][0]); count++) {
        ret = RSA_sign(NID_md5_sha1, buf, 36, buf2, rsa_num, rsa_key[testnum]);
        if (ret == 0) {
            BIO_printf(bio_err, "RSA sign failure\n");
            ERR_print_errors(bio_err);
            count = -1;
            break;
        }
    }
    return count;
}

static int RSA_verify_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char *buf2 = tempargs->buf2;
    unsigned int rsa_num = tempargs->siglen;
    RSA **rsa_key = tempargs->rsa_key;
    int ret, count;
    for (count = 0; COND(rsa_c[testnum][1]); count++) {
        ret =
            RSA_verify(NID_md5_sha1, buf, 36, buf2, rsa_num, rsa_key[testnum]);
        if (ret <= 0) {
            BIO_printf(bio_err, "RSA verify failure\n");
            ERR_print_errors(bio_err);
            count = -1;
            break;
        }
    }
    return count;
}
#endif

#ifndef OPENSSL_NO_DSA
static long dsa_c[DSA_NUM][2];
static int DSA_sign_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char *buf2 = tempargs->buf2;
    DSA **dsa_key = tempargs->dsa_key;
    unsigned int *siglen = &tempargs->siglen;
    int ret, count;
    for (count = 0; COND(dsa_c[testnum][0]); count++) {
        ret = DSA_sign(0, buf, 20, buf2, siglen, dsa_key[testnum]);
        if (ret == 0) {
            BIO_printf(bio_err, "DSA sign failure\n");
            ERR_print_errors(bio_err);
            count = -1;
            break;
        }
    }
    return count;
}

static int DSA_verify_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    unsigned char *buf2 = tempargs->buf2;
    DSA **dsa_key = tempargs->dsa_key;
    unsigned int siglen = tempargs->siglen;
    int ret, count;
    for (count = 0; COND(dsa_c[testnum][1]); count++) {
        ret = DSA_verify(0, buf, 20, buf2, siglen, dsa_key[testnum]);
        if (ret <= 0) {
            BIO_printf(bio_err, "DSA verify failure\n");
            ERR_print_errors(bio_err);
            count = -1;
            break;
        }
    }
    return count;
}
#endif

#ifndef OPENSSL_NO_EC
static long ecdsa_c[ECDSA_NUM][2];
static int ECDSA_sign_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    EC_KEY **ecdsa = tempargs->ecdsa;
    unsigned char *ecdsasig = tempargs->buf2;
    unsigned int *ecdsasiglen = &tempargs->siglen;
    int ret, count;
    for (count = 0; COND(ecdsa_c[testnum][0]); count++) {
        ret = ECDSA_sign(0, buf, 20, ecdsasig, ecdsasiglen, ecdsa[testnum]);
        if (ret == 0) {
            BIO_printf(bio_err, "ECDSA sign failure\n");
            ERR_print_errors(bio_err);
            count = -1;
            break;
        }
    }
    return count;
}

static int ECDSA_verify_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    EC_KEY **ecdsa = tempargs->ecdsa;
    unsigned char *ecdsasig = tempargs->buf2;
    unsigned int ecdsasiglen = tempargs->siglen;
    int ret, count;
    for (count = 0; COND(ecdsa_c[testnum][1]); count++) {
        ret = ECDSA_verify(0, buf, 20, ecdsasig, ecdsasiglen, ecdsa[testnum]);
        if (ret != 1) {
            BIO_printf(bio_err, "ECDSA verify failure\n");
            ERR_print_errors(bio_err);
            count = -1;
            break;
        }
    }
    return count;
}

/* ******************************************************************** */
static long ecdh_c[EC_NUM][1];

static int ECDH_EVP_derive_key_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    EVP_PKEY_CTX *ctx = tempargs->ecdh_ctx[testnum];
    unsigned char *derived_secret = tempargs->secret_a;
    int count;
    size_t *outlen = &(tempargs->outlen[testnum]);

    for (count = 0; COND(ecdh_c[testnum][0]); count++)
        EVP_PKEY_derive(ctx, derived_secret, outlen);

    return count;
}

static long eddsa_c[EdDSA_NUM][2];
static int EdDSA_sign_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    EVP_MD_CTX **edctx = tempargs->eddsa_ctx;
    unsigned char *eddsasig = tempargs->buf2;
    size_t *eddsasigsize = &tempargs->sigsize;
    int ret, count;

    for (count = 0; COND(eddsa_c[testnum][0]); count++) {
        ret = EVP_DigestSign(edctx[testnum], eddsasig, eddsasigsize, buf, 20);
        if (ret == 0) {
            BIO_printf(bio_err, "EdDSA sign failure\n");
            ERR_print_errors(bio_err);
            count = -1;
            break;
        }
    }
    return count;
}

static int EdDSA_verify_loop(void *args)
{
    loopargs_t *tempargs = *(loopargs_t **) args;
    unsigned char *buf = tempargs->buf;
    EVP_MD_CTX **edctx = tempargs->eddsa_ctx;
    unsigned char *eddsasig = tempargs->buf2;
    size_t eddsasigsize = tempargs->sigsize;
    int ret, count;

    for (count = 0; COND(eddsa_c[testnum][1]); count++) {
        ret = EVP_DigestVerify(edctx[testnum], eddsasig, eddsasigsize, buf, 20);
        if (ret != 1) {
            BIO_printf(bio_err, "EdDSA verify failure\n");
            ERR_print_errors(bio_err);
            count = -1;
            break;
        }
    }
    return count;
}
#endif                          /* OPENSSL_NO_EC */

static int run_benchmark(int async_jobs,
                         int (*loop_function) (void *), loopargs_t * loopargs)
{
    int job_op_count = 0;
    int total_op_count = 0;
    int num_inprogress = 0;
    int error = 0, i = 0, ret = 0;
    OSSL_ASYNC_FD job_fd = 0;
    size_t num_job_fds = 0;

    run = 1;

    if (async_jobs == 0) {
        return loop_function((void *)&loopargs);
    }

    for (i = 0; i < async_jobs && !error; i++) {
        loopargs_t *looparg_item = loopargs + i;

        /* Copy pointer content (looparg_t item address) into async context */
        ret = ASYNC_start_job(&loopargs[i].inprogress_job, loopargs[i].wait_ctx,
                              &job_op_count, loop_function,
                              (void *)&looparg_item, sizeof(looparg_item));
        switch (ret) {
        case ASYNC_PAUSE:
            ++num_inprogress;
            break;
        case ASYNC_FINISH:
            if (job_op_count == -1) {
                error = 1;
            } else {
                total_op_count += job_op_count;
            }
            break;
        case ASYNC_NO_JOBS:
        case ASYNC_ERR:
            BIO_printf(bio_err, "Failure in the job\n");
            ERR_print_errors(bio_err);
            error = 1;
            break;
        }
    }

    while (num_inprogress > 0) {
#if defined(OPENSSL_SYS_WINDOWS)
        DWORD avail = 0;
#elif defined(OPENSSL_SYS_UNIX)
        int select_result = 0;
        OSSL_ASYNC_FD max_fd = 0;
        fd_set waitfdset;

        FD_ZERO(&waitfdset);

        for (i = 0; i < async_jobs && num_inprogress > 0; i++) {
            if (loopargs[i].inprogress_job == NULL)
                continue;

            if (!ASYNC_WAIT_CTX_get_all_fds
                (loopargs[i].wait_ctx, NULL, &num_job_fds)
                || num_job_fds > 1) {
                BIO_printf(bio_err, "Too many fds in ASYNC_WAIT_CTX\n");
                ERR_print_errors(bio_err);
                error = 1;
                break;
            }
            ASYNC_WAIT_CTX_get_all_fds(loopargs[i].wait_ctx, &job_fd,
                                       &num_job_fds);
            FD_SET(job_fd, &waitfdset);
            if (job_fd > max_fd)
                max_fd = job_fd;
        }

        if (max_fd >= (OSSL_ASYNC_FD)FD_SETSIZE) {
            BIO_printf(bio_err,
                       "Error: max_fd (%d) must be smaller than FD_SETSIZE (%d). "
                       "Decrease the value of async_jobs\n",
                       max_fd, FD_SETSIZE);
            ERR_print_errors(bio_err);
            error = 1;
            break;
        }

        select_result = select(max_fd + 1, &waitfdset, NULL, NULL, NULL);
        if (select_result == -1 && errno == EINTR)
            continue;

        if (select_result == -1) {
            BIO_printf(bio_err, "Failure in the select\n");
            ERR_print_errors(bio_err);
            error = 1;
            break;
        }

        if (select_result == 0)
            continue;
#endif

        for (i = 0; i < async_jobs; i++) {
            if (loopargs[i].inprogress_job == NULL)
                continue;

            if (!ASYNC_WAIT_CTX_get_all_fds
                (loopargs[i].wait_ctx, NULL, &num_job_fds)
                || num_job_fds > 1) {
                BIO_printf(bio_err, "Too many fds in ASYNC_WAIT_CTX\n");
                ERR_print_errors(bio_err);
                error = 1;
                break;
            }
            ASYNC_WAIT_CTX_get_all_fds(loopargs[i].wait_ctx, &job_fd,
                                       &num_job_fds);

#if defined(OPENSSL_SYS_UNIX)
            if (num_job_fds == 1 && !FD_ISSET(job_fd, &waitfdset))
                continue;
#elif defined(OPENSSL_SYS_WINDOWS)
            if (num_job_fds == 1
                && !PeekNamedPipe(job_fd, NULL, 0, NULL, &avail, NULL)
                && avail > 0)
                continue;
#endif

            ret = ASYNC_start_job(&loopargs[i].inprogress_job,
                                  loopargs[i].wait_ctx, &job_op_count,
                                  loop_function, (void *)(loopargs + i),
                                  sizeof(loopargs_t));
            switch (ret) {
            case ASYNC_PAUSE:
                break;
            case ASYNC_FINISH:
                if (job_op_count == -1) {
                    error = 1;
                } else {
                    total_op_count += job_op_count;
                }
                --num_inprogress;
                loopargs[i].inprogress_job = NULL;
                break;
            case ASYNC_NO_JOBS:
            case ASYNC_ERR:
                --num_inprogress;
                loopargs[i].inprogress_job = NULL;
                BIO_printf(bio_err, "Failure in the job\n");
                ERR_print_errors(bio_err);
                error = 1;
                break;
            }
        }
    }

    return error ? -1 : total_op_count;
}

int speed_main(int argc, char **argv)
{
    ENGINE *e = NULL;
    loopargs_t *loopargs = NULL;
    const char *prog;
    const char *engine_id = NULL;
    const EVP_CIPHER *evp_cipher = NULL;
    double d = 0.0;
    OPTION_CHOICE o;
    int async_init = 0, multiblock = 0, pr_header = 0;
    int doit[ALGOR_NUM] = { 0 };
    int ret = 1, misalign = 0, lengths_single = 0, aead = 0;
    long count = 0;
    unsigned int size_num = OSSL_NELEM(lengths_list);
    unsigned int i, k, loop, loopargs_len = 0, async_jobs = 0;
    int keylen;
    int buflen;
#ifndef NO_FORK
    int multi = 0;
#endif
#if !defined(OPENSSL_NO_RSA) || !defined(OPENSSL_NO_DSA) \
    || !defined(OPENSSL_NO_EC)
    long rsa_count = 1;
#endif
    openssl_speed_sec_t seconds = { SECONDS, RSA_SECONDS, DSA_SECONDS,
                                    ECDSA_SECONDS, ECDH_SECONDS,
                                    EdDSA_SECONDS };

    /* What follows are the buffers and key material. */
#ifndef OPENSSL_NO_RC5
    RC5_32_KEY rc5_ks;
#endif
#ifndef OPENSSL_NO_RC2
    RC2_KEY rc2_ks;
#endif
#ifndef OPENSSL_NO_IDEA
    IDEA_KEY_SCHEDULE idea_ks;
#endif
#ifndef OPENSSL_NO_SEED
    SEED_KEY_SCHEDULE seed_ks;
#endif
#ifndef OPENSSL_NO_BF
    BF_KEY bf_ks;
#endif
#ifndef OPENSSL_NO_CAST
    CAST_KEY cast_ks;
#endif
    static const unsigned char key16[16] = {
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
        0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12
    };
    static const unsigned char key24[24] = {
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
        0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12,
        0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34
    };
    static const unsigned char key32[32] = {
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
        0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12,
        0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34,
        0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56
    };
#ifndef OPENSSL_NO_CAMELLIA
    static const unsigned char ckey24[24] = {
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
        0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12,
        0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34
    };
    static const unsigned char ckey32[32] = {
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
        0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12,
        0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34,
        0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56
    };
    CAMELLIA_KEY camellia_ks1, camellia_ks2, camellia_ks3;
#endif
#ifndef OPENSSL_NO_DES
    static DES_cblock key = {
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0
    };
    static DES_cblock key2 = {
        0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12
    };
    static DES_cblock key3 = {
        0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34
    };
#endif
#ifndef OPENSSL_NO_RSA
    static const unsigned int rsa_bits[RSA_NUM] = {
        512, 1024, 2048, 3072, 4096, 7680, 15360
    };
    static const unsigned char *rsa_data[RSA_NUM] = {
        test512, test1024, test2048, test3072, test4096, test7680, test15360
    };
    static const int rsa_data_length[RSA_NUM] = {
        sizeof(test512), sizeof(test1024),
        sizeof(test2048), sizeof(test3072),
        sizeof(test4096), sizeof(test7680),
        sizeof(test15360)
    };
    int rsa_doit[RSA_NUM] = { 0 };
    int primes = RSA_DEFAULT_PRIME_NUM;
#endif
#ifndef OPENSSL_NO_DSA
    static const unsigned int dsa_bits[DSA_NUM] = { 512, 1024, 2048 };
    int dsa_doit[DSA_NUM] = { 0 };
#endif
#ifndef OPENSSL_NO_EC
    /*
     * We only test over the following curves as they are representative, To
     * add tests over more curves, simply add the curve NID and curve name to
     * the following arrays and increase the |ecdh_choices| list accordingly.
     */
    static const struct {
        const char *name;
        unsigned int nid;
        unsigned int bits;
    } test_curves[] = {
        /* Prime Curves */
        {"secp160r1", NID_secp160r1, 160},
        {"nistp192", NID_X9_62_prime192v1, 192},
        {"nistp224", NID_secp224r1, 224},
        {"nistp256", NID_X9_62_prime256v1, 256},
        {"nistp384", NID_secp384r1, 384},
        {"nistp521", NID_secp521r1, 521},
        /* Binary Curves */
        {"nistk163", NID_sect163k1, 163},
        {"nistk233", NID_sect233k1, 233},
        {"nistk283", NID_sect283k1, 283},
        {"nistk409", NID_sect409k1, 409},
        {"nistk571", NID_sect571k1, 571},
        {"nistb163", NID_sect163r2, 163},
        {"nistb233", NID_sect233r1, 233},
        {"nistb283", NID_sect283r1, 283},
        {"nistb409", NID_sect409r1, 409},
        {"nistb571", NID_sect571r1, 571},
        {"brainpoolP256r1", NID_brainpoolP256r1, 256},
        {"brainpoolP256t1", NID_brainpoolP256t1, 256},
        {"brainpoolP384r1", NID_brainpoolP384r1, 384},
        {"brainpoolP384t1", NID_brainpoolP384t1, 384},
        {"brainpoolP512r1", NID_brainpoolP512r1, 512},
        {"brainpoolP512t1", NID_brainpoolP512t1, 512},
        /* Other and ECDH only ones */
        {"X25519", NID_X25519, 253},
        {"X448", NID_X448, 448}
    };
    static const struct {
        const char *name;
        unsigned int nid;
        unsigned int bits;
        size_t sigsize;
    } test_ed_curves[] = {
        /* EdDSA */
        {"Ed25519", NID_ED25519, 253, 64},
        {"Ed448", NID_ED448, 456, 114}
    };
    int ecdsa_doit[ECDSA_NUM] = { 0 };
    int ecdh_doit[EC_NUM] = { 0 };
    int eddsa_doit[EdDSA_NUM] = { 0 };
    OPENSSL_assert(OSSL_NELEM(test_curves) >= EC_NUM);
    OPENSSL_assert(OSSL_NELEM(test_ed_curves) >= EdDSA_NUM);
#endif                          /* ndef OPENSSL_NO_EC */

    prog = opt_init(argc, argv, speed_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opterr:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(speed_options);
            ret = 0;
            goto end;
        case OPT_ELAPSED:
            usertime = 0;
            break;
        case OPT_EVP:
            evp_md = NULL;
            evp_cipher = EVP_get_cipherbyname(opt_arg());
            if (evp_cipher == NULL)
                evp_md = EVP_get_digestbyname(opt_arg());
            if (evp_cipher == NULL && evp_md == NULL) {
                BIO_printf(bio_err,
                           "%s: %s is an unknown cipher or digest\n",
                           prog, opt_arg());
                goto end;
            }
            doit[D_EVP] = 1;
            break;
        case OPT_DECRYPT:
            decrypt = 1;
            break;
        case OPT_ENGINE:
            /*
             * In a forked execution, an engine might need to be
             * initialised by each child process, not by the parent.
             * So store the name here and run setup_engine() later on.
             */
            engine_id = opt_arg();
            break;
        case OPT_MULTI:
#ifndef NO_FORK
            multi = atoi(opt_arg());
#endif
            break;
        case OPT_ASYNCJOBS:
#ifndef OPENSSL_NO_ASYNC
            async_jobs = atoi(opt_arg());
            if (!ASYNC_is_capable()) {
                BIO_printf(bio_err,
                           "%s: async_jobs specified but async not supported\n",
                           prog);
                goto opterr;
            }
            if (async_jobs > 99999) {
                BIO_printf(bio_err, "%s: too many async_jobs\n", prog);
                goto opterr;
            }
#endif
            break;
        case OPT_MISALIGN:
            if (!opt_int(opt_arg(), &misalign))
                goto end;
            if (misalign > MISALIGN) {
                BIO_printf(bio_err,
                           "%s: Maximum offset is %d\n", prog, MISALIGN);
                goto opterr;
            }
            break;
        case OPT_MR:
            mr = 1;
            break;
        case OPT_MB:
            multiblock = 1;
#ifdef OPENSSL_NO_MULTIBLOCK
            BIO_printf(bio_err,
                       "%s: -mb specified but multi-block support is disabled\n",
                       prog);
            goto end;
#endif
            break;
        case OPT_R_CASES:
            if (!opt_rand(o))
                goto end;
            break;
        case OPT_PRIMES:
            if (!opt_int(opt_arg(), &primes))
                goto end;
            break;
        case OPT_SECONDS:
            seconds.sym = seconds.rsa = seconds.dsa = seconds.ecdsa
                        = seconds.ecdh = seconds.eddsa = atoi(opt_arg());
            break;
        case OPT_BYTES:
            lengths_single = atoi(opt_arg());
            lengths = &lengths_single;
            size_num = 1;
            break;
        case OPT_AEAD:
            aead = 1;
            break;
        }
    }
    argc = opt_num_rest();
    argv = opt_rest();

    /* Remaining arguments are algorithms. */
    for (; *argv; argv++) {
        if (found(*argv, doit_choices, &i)) {
            doit[i] = 1;
            continue;
        }
#ifndef OPENSSL_NO_DES
        if (strcmp(*argv, "des") == 0) {
            doit[D_CBC_DES] = doit[D_EDE3_DES] = 1;
            continue;
        }
#endif
        if (strcmp(*argv, "sha") == 0) {
            doit[D_SHA1] = doit[D_SHA256] = doit[D_SHA512] = 1;
            continue;
        }
#ifndef OPENSSL_NO_RSA
        if (strcmp(*argv, "openssl") == 0)
            continue;
        if (strcmp(*argv, "rsa") == 0) {
            for (loop = 0; loop < OSSL_NELEM(rsa_doit); loop++)
                rsa_doit[loop] = 1;
            continue;
        }
        if (found(*argv, rsa_choices, &i)) {
            rsa_doit[i] = 1;
            continue;
        }
#endif
#ifndef OPENSSL_NO_DSA
        if (strcmp(*argv, "dsa") == 0) {
            dsa_doit[R_DSA_512] = dsa_doit[R_DSA_1024] =
                dsa_doit[R_DSA_2048] = 1;
            continue;
        }
        if (found(*argv, dsa_choices, &i)) {
            dsa_doit[i] = 2;
            continue;
        }
#endif
        if (strcmp(*argv, "aes") == 0) {
            doit[D_CBC_128_AES] = doit[D_CBC_192_AES] = doit[D_CBC_256_AES] = 1;
            continue;
        }
#ifndef OPENSSL_NO_CAMELLIA
        if (strcmp(*argv, "camellia") == 0) {
            doit[D_CBC_128_CML] = doit[D_CBC_192_CML] = doit[D_CBC_256_CML] = 1;
            continue;
        }
#endif
#ifndef OPENSSL_NO_EC
        if (strcmp(*argv, "ecdsa") == 0) {
            for (loop = 0; loop < OSSL_NELEM(ecdsa_doit); loop++)
                ecdsa_doit[loop] = 1;
            continue;
        }
        if (found(*argv, ecdsa_choices, &i)) {
            ecdsa_doit[i] = 2;
            continue;
        }
        if (strcmp(*argv, "ecdh") == 0) {
            for (loop = 0; loop < OSSL_NELEM(ecdh_doit); loop++)
                ecdh_doit[loop] = 1;
            continue;
        }
        if (found(*argv, ecdh_choices, &i)) {
            ecdh_doit[i] = 2;
            continue;
        }
        if (strcmp(*argv, "eddsa") == 0) {
            for (loop = 0; loop < OSSL_NELEM(eddsa_doit); loop++)
                eddsa_doit[loop] = 1;
            continue;
        }
        if (found(*argv, eddsa_choices, &i)) {
            eddsa_doit[i] = 2;
            continue;
        }
#endif
        BIO_printf(bio_err, "%s: Unknown algorithm %s\n", prog, *argv);
        goto end;
    }

    /* Sanity checks */
    if (aead) {
        if (evp_cipher == NULL) {
            BIO_printf(bio_err, "-aead can be used only with an AEAD cipher\n");
            goto end;
        } else if (!(EVP_CIPHER_flags(evp_cipher) &
                     EVP_CIPH_FLAG_AEAD_CIPHER)) {
            BIO_printf(bio_err, "%s is not an AEAD cipher\n",
                       OBJ_nid2ln(EVP_CIPHER_nid(evp_cipher)));
            goto end;
        }
    }
    if (multiblock) {
        if (evp_cipher == NULL) {
            BIO_printf(bio_err,"-mb can be used only with a multi-block"
                               " capable cipher\n");
            goto end;
        } else if (!(EVP_CIPHER_flags(evp_cipher) &
                     EVP_CIPH_FLAG_TLS1_1_MULTIBLOCK)) {
            BIO_printf(bio_err, "%s is not a multi-block capable\n",
                       OBJ_nid2ln(EVP_CIPHER_nid(evp_cipher)));
            goto end;
        } else if (async_jobs > 0) {
            BIO_printf(bio_err, "Async mode is not supported with -mb");
            goto end;
        }
    }

    /* Initialize the job pool if async mode is enabled */
    if (async_jobs > 0) {
        async_init = ASYNC_init_thread(async_jobs, async_jobs);
        if (!async_init) {
            BIO_printf(bio_err, "Error creating the ASYNC job pool\n");
            goto end;
        }
    }

    loopargs_len = (async_jobs == 0 ? 1 : async_jobs);
    loopargs =
        app_malloc(loopargs_len * sizeof(loopargs_t), "array of loopargs");
    memset(loopargs, 0, loopargs_len * sizeof(loopargs_t));

    for (i = 0; i < loopargs_len; i++) {
        if (async_jobs > 0) {
            loopargs[i].wait_ctx = ASYNC_WAIT_CTX_new();
            if (loopargs[i].wait_ctx == NULL) {
                BIO_printf(bio_err, "Error creating the ASYNC_WAIT_CTX\n");
                goto end;
            }
        }

        buflen = lengths[size_num - 1];
        if (buflen < 36)    /* size of random vector in RSA bencmark */
            buflen = 36;
        buflen += MAX_MISALIGNMENT + 1;
        loopargs[i].buf_malloc = app_malloc(buflen, "input buffer");
        loopargs[i].buf2_malloc = app_malloc(buflen, "input buffer");
        memset(loopargs[i].buf_malloc, 0, buflen);
        memset(loopargs[i].buf2_malloc, 0, buflen);

        /* Align the start of buffers on a 64 byte boundary */
        loopargs[i].buf = loopargs[i].buf_malloc + misalign;
        loopargs[i].buf2 = loopargs[i].buf2_malloc + misalign;
#ifndef OPENSSL_NO_EC
        loopargs[i].secret_a = app_malloc(MAX_ECDH_SIZE, "ECDH secret a");
        loopargs[i].secret_b = app_malloc(MAX_ECDH_SIZE, "ECDH secret b");
#endif
    }

#ifndef NO_FORK
    if (multi && do_multi(multi, size_num))
        goto show_res;
#endif

    /* Initialize the engine after the fork */
    e = setup_engine(engine_id, 0);

    /* No parameters; turn on everything. */
    if ((argc == 0) && !doit[D_EVP]) {
        for (i = 0; i < ALGOR_NUM; i++)
            if (i != D_EVP)
                doit[i] = 1;
#ifndef OPENSSL_NO_RSA
        for (i = 0; i < RSA_NUM; i++)
            rsa_doit[i] = 1;
#endif
#ifndef OPENSSL_NO_DSA
        for (i = 0; i < DSA_NUM; i++)
            dsa_doit[i] = 1;
#endif
#ifndef OPENSSL_NO_EC
        for (loop = 0; loop < OSSL_NELEM(ecdsa_doit); loop++)
            ecdsa_doit[loop] = 1;
        for (loop = 0; loop < OSSL_NELEM(ecdh_doit); loop++)
            ecdh_doit[loop] = 1;
        for (loop = 0; loop < OSSL_NELEM(eddsa_doit); loop++)
            eddsa_doit[loop] = 1;
#endif
    }
    for (i = 0; i < ALGOR_NUM; i++)
        if (doit[i])
            pr_header++;

    if (usertime == 0 && !mr)
        BIO_printf(bio_err,
                   "You have chosen to measure elapsed time "
                   "instead of user CPU time.\n");

#ifndef OPENSSL_NO_RSA
    for (i = 0; i < loopargs_len; i++) {
        if (primes > RSA_DEFAULT_PRIME_NUM) {
            /* for multi-prime RSA, skip this */
            break;
        }
        for (k = 0; k < RSA_NUM; k++) {
            const unsigned char *p;

            p = rsa_data[k];
            loopargs[i].rsa_key[k] =
                d2i_RSAPrivateKey(NULL, &p, rsa_data_length[k]);
            if (loopargs[i].rsa_key[k] == NULL) {
                BIO_printf(bio_err,
                           "internal error loading RSA key number %d\n", k);
                goto end;
            }
        }
    }
#endif
#ifndef OPENSSL_NO_DSA
    for (i = 0; i < loopargs_len; i++) {
        loopargs[i].dsa_key[0] = get_dsa(512);
        loopargs[i].dsa_key[1] = get_dsa(1024);
        loopargs[i].dsa_key[2] = get_dsa(2048);
    }
#endif
#ifndef OPENSSL_NO_DES
    DES_set_key_unchecked(&key, &sch);
    DES_set_key_unchecked(&key2, &sch2);
    DES_set_key_unchecked(&key3, &sch3);
#endif
    AES_set_encrypt_key(key16, 128, &aes_ks1);
    AES_set_encrypt_key(key24, 192, &aes_ks2);
    AES_set_encrypt_key(key32, 256, &aes_ks3);
#ifndef OPENSSL_NO_CAMELLIA
    Camellia_set_key(key16, 128, &camellia_ks1);
    Camellia_set_key(ckey24, 192, &camellia_ks2);
    Camellia_set_key(ckey32, 256, &camellia_ks3);
#endif
#ifndef OPENSSL_NO_IDEA
    IDEA_set_encrypt_key(key16, &idea_ks);
#endif
#ifndef OPENSSL_NO_SEED
    SEED_set_key(key16, &seed_ks);
#endif
#ifndef OPENSSL_NO_RC4
    RC4_set_key(&rc4_ks, 16, key16);
#endif
#ifndef OPENSSL_NO_RC2
    RC2_set_key(&rc2_ks, 16, key16, 128);
#endif
#ifndef OPENSSL_NO_RC5
    RC5_32_set_key(&rc5_ks, 16, key16, 12);
#endif
#ifndef OPENSSL_NO_BF
    BF_set_key(&bf_ks, 16, key16);
#endif
#ifndef OPENSSL_NO_CAST
    CAST_set_key(&cast_ks, 16, key16);
#endif
#ifndef SIGALRM
# ifndef OPENSSL_NO_DES
    BIO_printf(bio_err, "First we calculate the approximate speed ...\n");
    count = 10;
    do {
        long it;
        count *= 2;
        Time_F(START);
        for (it = count; it; it--)
            DES_ecb_encrypt((DES_cblock *)loopargs[0].buf,
                            (DES_cblock *)loopargs[0].buf, &sch, DES_ENCRYPT);
        d = Time_F(STOP);
    } while (d < 3);
    save_count = count;
    c[D_MD2][0] = count / 10;
    c[D_MDC2][0] = count / 10;
    c[D_MD4][0] = count;
    c[D_MD5][0] = count;
    c[D_HMAC][0] = count;
    c[D_SHA1][0] = count;
    c[D_RMD160][0] = count;
    c[D_RC4][0] = count * 5;
    c[D_CBC_DES][0] = count;
    c[D_EDE3_DES][0] = count / 3;
    c[D_CBC_IDEA][0] = count;
    c[D_CBC_SEED][0] = count;
    c[D_CBC_RC2][0] = count;
    c[D_CBC_RC5][0] = count;
    c[D_CBC_BF][0] = count;
    c[D_CBC_CAST][0] = count;
    c[D_CBC_128_AES][0] = count;
    c[D_CBC_192_AES][0] = count;
    c[D_CBC_256_AES][0] = count;
    c[D_CBC_128_CML][0] = count;
    c[D_CBC_192_CML][0] = count;
    c[D_CBC_256_CML][0] = count;
    c[D_SHA256][0] = count;
    c[D_SHA512][0] = count;
    c[D_WHIRLPOOL][0] = count;
    c[D_IGE_128_AES][0] = count;
    c[D_IGE_192_AES][0] = count;
    c[D_IGE_256_AES][0] = count;
    c[D_GHASH][0] = count;
    c[D_RAND][0] = count;

    for (i = 1; i < size_num; i++) {
        long l0, l1;

        l0 = (long)lengths[0];
        l1 = (long)lengths[i];

        c[D_MD2][i] = c[D_MD2][0] * 4 * l0 / l1;
        c[D_MDC2][i] = c[D_MDC2][0] * 4 * l0 / l1;
        c[D_MD4][i] = c[D_MD4][0] * 4 * l0 / l1;
        c[D_MD5][i] = c[D_MD5][0] * 4 * l0 / l1;
        c[D_HMAC][i] = c[D_HMAC][0] * 4 * l0 / l1;
        c[D_SHA1][i] = c[D_SHA1][0] * 4 * l0 / l1;
        c[D_RMD160][i] = c[D_RMD160][0] * 4 * l0 / l1;
        c[D_SHA256][i] = c[D_SHA256][0] * 4 * l0 / l1;
        c[D_SHA512][i] = c[D_SHA512][0] * 4 * l0 / l1;
        c[D_WHIRLPOOL][i] = c[D_WHIRLPOOL][0] * 4 * l0 / l1;
        c[D_GHASH][i] = c[D_GHASH][0] * 4 * l0 / l1;
        c[D_RAND][i] = c[D_RAND][0] * 4 * l0 / l1;

        l0 = (long)lengths[i - 1];

        c[D_RC4][i] = c[D_RC4][i - 1] * l0 / l1;
        c[D_CBC_DES][i] = c[D_CBC_DES][i - 1] * l0 / l1;
        c[D_EDE3_DES][i] = c[D_EDE3_DES][i - 1] * l0 / l1;
        c[D_CBC_IDEA][i] = c[D_CBC_IDEA][i - 1] * l0 / l1;
        c[D_CBC_SEED][i] = c[D_CBC_SEED][i - 1] * l0 / l1;
        c[D_CBC_RC2][i] = c[D_CBC_RC2][i - 1] * l0 / l1;
        c[D_CBC_RC5][i] = c[D_CBC_RC5][i - 1] * l0 / l1;
        c[D_CBC_BF][i] = c[D_CBC_BF][i - 1] * l0 / l1;
        c[D_CBC_CAST][i] = c[D_CBC_CAST][i - 1] * l0 / l1;
        c[D_CBC_128_AES][i] = c[D_CBC_128_AES][i - 1] * l0 / l1;
        c[D_CBC_192_AES][i] = c[D_CBC_192_AES][i - 1] * l0 / l1;
        c[D_CBC_256_AES][i] = c[D_CBC_256_AES][i - 1] * l0 / l1;
        c[D_CBC_128_CML][i] = c[D_CBC_128_CML][i - 1] * l0 / l1;
        c[D_CBC_192_CML][i] = c[D_CBC_192_CML][i - 1] * l0 / l1;
        c[D_CBC_256_CML][i] = c[D_CBC_256_CML][i - 1] * l0 / l1;
        c[D_IGE_128_AES][i] = c[D_IGE_128_AES][i - 1] * l0 / l1;
        c[D_IGE_192_AES][i] = c[D_IGE_192_AES][i - 1] * l0 / l1;
        c[D_IGE_256_AES][i] = c[D_IGE_256_AES][i - 1] * l0 / l1;
    }

#  ifndef OPENSSL_NO_RSA
    rsa_c[R_RSA_512][0] = count / 2000;
    rsa_c[R_RSA_512][1] = count / 400;
    for (i = 1; i < RSA_NUM; i++) {
        rsa_c[i][0] = rsa_c[i - 1][0] / 8;
        rsa_c[i][1] = rsa_c[i - 1][1] / 4;
        if (rsa_doit[i] <= 1 && rsa_c[i][0] == 0)
            rsa_doit[i] = 0;
        else {
            if (rsa_c[i][0] == 0) {
                rsa_c[i][0] = 1; /* Set minimum iteration Nb to 1. */
                rsa_c[i][1] = 20;
            }
        }
    }
#  endif

#  ifndef OPENSSL_NO_DSA
    dsa_c[R_DSA_512][0] = count / 1000;
    dsa_c[R_DSA_512][1] = count / 1000 / 2;
    for (i = 1; i < DSA_NUM; i++) {
        dsa_c[i][0] = dsa_c[i - 1][0] / 4;
        dsa_c[i][1] = dsa_c[i - 1][1] / 4;
        if (dsa_doit[i] <= 1 && dsa_c[i][0] == 0)
            dsa_doit[i] = 0;
        else {
            if (dsa_c[i][0] == 0) {
                dsa_c[i][0] = 1; /* Set minimum iteration Nb to 1. */
                dsa_c[i][1] = 1;
            }
        }
    }
#  endif

#  ifndef OPENSSL_NO_EC
    ecdsa_c[R_EC_P160][0] = count / 1000;
    ecdsa_c[R_EC_P160][1] = count / 1000 / 2;
    for (i = R_EC_P192; i <= R_EC_P521; i++) {
        ecdsa_c[i][0] = ecdsa_c[i - 1][0] / 2;
        ecdsa_c[i][1] = ecdsa_c[i - 1][1] / 2;
        if (ecdsa_doit[i] <= 1 && ecdsa_c[i][0] == 0)
            ecdsa_doit[i] = 0;
        else {
            if (ecdsa_c[i][0] == 0) {
                ecdsa_c[i][0] = 1;
                ecdsa_c[i][1] = 1;
            }
        }
    }
    ecdsa_c[R_EC_K163][0] = count / 1000;
    ecdsa_c[R_EC_K163][1] = count / 1000 / 2;
    for (i = R_EC_K233; i <= R_EC_K571; i++) {
        ecdsa_c[i][0] = ecdsa_c[i - 1][0] / 2;
        ecdsa_c[i][1] = ecdsa_c[i - 1][1] / 2;
        if (ecdsa_doit[i] <= 1 && ecdsa_c[i][0] == 0)
            ecdsa_doit[i] = 0;
        else {
            if (ecdsa_c[i][0] == 0) {
                ecdsa_c[i][0] = 1;
                ecdsa_c[i][1] = 1;
            }
        }
    }
    ecdsa_c[R_EC_B163][0] = count / 1000;
    ecdsa_c[R_EC_B163][1] = count / 1000 / 2;
    for (i = R_EC_B233; i <= R_EC_B571; i++) {
        ecdsa_c[i][0] = ecdsa_c[i - 1][0] / 2;
        ecdsa_c[i][1] = ecdsa_c[i - 1][1] / 2;
        if (ecdsa_doit[i] <= 1 && ecdsa_c[i][0] == 0)
            ecdsa_doit[i] = 0;
        else {
            if (ecdsa_c[i][0] == 0) {
                ecdsa_c[i][0] = 1;
                ecdsa_c[i][1] = 1;
            }
        }
    }

    ecdh_c[R_EC_P160][0] = count / 1000;
    for (i = R_EC_P192; i <= R_EC_P521; i++) {
        ecdh_c[i][0] = ecdh_c[i - 1][0] / 2;
        if (ecdh_doit[i] <= 1 && ecdh_c[i][0] == 0)
            ecdh_doit[i] = 0;
        else {
            if (ecdh_c[i][0] == 0) {
                ecdh_c[i][0] = 1;
            }
        }
    }
    ecdh_c[R_EC_K163][0] = count / 1000;
    for (i = R_EC_K233; i <= R_EC_K571; i++) {
        ecdh_c[i][0] = ecdh_c[i - 1][0] / 2;
        if (ecdh_doit[i] <= 1 && ecdh_c[i][0] == 0)
            ecdh_doit[i] = 0;
        else {
            if (ecdh_c[i][0] == 0) {
                ecdh_c[i][0] = 1;
            }
        }
    }
    ecdh_c[R_EC_B163][0] = count / 1000;
    for (i = R_EC_B233; i <= R_EC_B571; i++) {
        ecdh_c[i][0] = ecdh_c[i - 1][0] / 2;
        if (ecdh_doit[i] <= 1 && ecdh_c[i][0] == 0)
            ecdh_doit[i] = 0;
        else {
            if (ecdh_c[i][0] == 0) {
                ecdh_c[i][0] = 1;
            }
        }
    }
    /* repeated code good to factorize */
    ecdh_c[R_EC_BRP256R1][0] = count / 1000;
    for (i = R_EC_BRP384R1; i <= R_EC_BRP512R1; i += 2) {
        ecdh_c[i][0] = ecdh_c[i - 2][0] / 2;
        if (ecdh_doit[i] <= 1 && ecdh_c[i][0] == 0)
            ecdh_doit[i] = 0;
        else {
            if (ecdh_c[i][0] == 0) {
                ecdh_c[i][0] = 1;
            }
        }
    }
    ecdh_c[R_EC_BRP256T1][0] = count / 1000;
    for (i = R_EC_BRP384T1; i <= R_EC_BRP512T1; i += 2) {
        ecdh_c[i][0] = ecdh_c[i - 2][0] / 2;
        if (ecdh_doit[i] <= 1 && ecdh_c[i][0] == 0)
            ecdh_doit[i] = 0;
        else {
            if (ecdh_c[i][0] == 0) {
                ecdh_c[i][0] = 1;
            }
        }
    }
    /* default iteration count for the last two EC Curves */
    ecdh_c[R_EC_X25519][0] = count / 1800;
    ecdh_c[R_EC_X448][0] = count / 7200;

    eddsa_c[R_EC_Ed25519][0] = count / 1800;
    eddsa_c[R_EC_Ed448][0] = count / 7200;
#  endif

# else
/* not worth fixing */
#  error "You cannot disable DES on systems without SIGALRM."
# endif                         /* OPENSSL_NO_DES */
#elif SIGALRM > 0
    signal(SIGALRM, alarmed);
#endif                          /* SIGALRM */

#ifndef OPENSSL_NO_MD2
    if (doit[D_MD2]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_MD2], c[D_MD2][testnum], lengths[testnum],
                          seconds.sym);
            Time_F(START);
            count = run_benchmark(async_jobs, EVP_Digest_MD2_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_MD2, testnum, count, d);
        }
    }
#endif
#ifndef OPENSSL_NO_MDC2
    if (doit[D_MDC2]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_MDC2], c[D_MDC2][testnum], lengths[testnum],
                          seconds.sym);
            Time_F(START);
            count = run_benchmark(async_jobs, EVP_Digest_MDC2_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_MDC2, testnum, count, d);
        }
    }
#endif

#ifndef OPENSSL_NO_MD4
    if (doit[D_MD4]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_MD4], c[D_MD4][testnum], lengths[testnum],
                          seconds.sym);
            Time_F(START);
            count = run_benchmark(async_jobs, EVP_Digest_MD4_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_MD4, testnum, count, d);
        }
    }
#endif

#ifndef OPENSSL_NO_MD5
    if (doit[D_MD5]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_MD5], c[D_MD5][testnum], lengths[testnum],
                          seconds.sym);
            Time_F(START);
            count = run_benchmark(async_jobs, MD5_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_MD5, testnum, count, d);
        }
    }

    if (doit[D_HMAC]) {
        static const char hmac_key[] = "This is a key...";
        int len = strlen(hmac_key);

        for (i = 0; i < loopargs_len; i++) {
            loopargs[i].hctx = HMAC_CTX_new();
            if (loopargs[i].hctx == NULL) {
                BIO_printf(bio_err, "HMAC malloc failure, exiting...");
                exit(1);
            }

            HMAC_Init_ex(loopargs[i].hctx, hmac_key, len, EVP_md5(), NULL);
        }
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_HMAC], c[D_HMAC][testnum], lengths[testnum],
                          seconds.sym);
            Time_F(START);
            count = run_benchmark(async_jobs, HMAC_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_HMAC, testnum, count, d);
        }
        for (i = 0; i < loopargs_len; i++) {
            HMAC_CTX_free(loopargs[i].hctx);
        }
    }
#endif
    if (doit[D_SHA1]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_SHA1], c[D_SHA1][testnum], lengths[testnum],
                          seconds.sym);
            Time_F(START);
            count = run_benchmark(async_jobs, SHA1_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_SHA1, testnum, count, d);
        }
    }
    if (doit[D_SHA256]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_SHA256], c[D_SHA256][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            count = run_benchmark(async_jobs, SHA256_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_SHA256, testnum, count, d);
        }
    }
    if (doit[D_SHA512]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_SHA512], c[D_SHA512][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            count = run_benchmark(async_jobs, SHA512_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_SHA512, testnum, count, d);
        }
    }
#ifndef OPENSSL_NO_WHIRLPOOL
    if (doit[D_WHIRLPOOL]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_WHIRLPOOL], c[D_WHIRLPOOL][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            count = run_benchmark(async_jobs, WHIRLPOOL_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_WHIRLPOOL, testnum, count, d);
        }
    }
#endif

#ifndef OPENSSL_NO_RMD160
    if (doit[D_RMD160]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_RMD160], c[D_RMD160][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            count = run_benchmark(async_jobs, EVP_Digest_RMD160_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_RMD160, testnum, count, d);
        }
    }
#endif
#ifndef OPENSSL_NO_RC4
    if (doit[D_RC4]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_RC4], c[D_RC4][testnum], lengths[testnum],
                          seconds.sym);
            Time_F(START);
            count = run_benchmark(async_jobs, RC4_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_RC4, testnum, count, d);
        }
    }
#endif
#ifndef OPENSSL_NO_DES
    if (doit[D_CBC_DES]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_CBC_DES], c[D_CBC_DES][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            count = run_benchmark(async_jobs, DES_ncbc_encrypt_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_CBC_DES, testnum, count, d);
        }
    }

    if (doit[D_EDE3_DES]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_EDE3_DES], c[D_EDE3_DES][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            count =
                run_benchmark(async_jobs, DES_ede3_cbc_encrypt_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_EDE3_DES, testnum, count, d);
        }
    }
#endif

    if (doit[D_CBC_128_AES]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_CBC_128_AES], c[D_CBC_128_AES][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            count =
                run_benchmark(async_jobs, AES_cbc_128_encrypt_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_CBC_128_AES, testnum, count, d);
        }
    }
    if (doit[D_CBC_192_AES]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_CBC_192_AES], c[D_CBC_192_AES][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            count =
                run_benchmark(async_jobs, AES_cbc_192_encrypt_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_CBC_192_AES, testnum, count, d);
        }
    }
    if (doit[D_CBC_256_AES]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_CBC_256_AES], c[D_CBC_256_AES][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            count =
                run_benchmark(async_jobs, AES_cbc_256_encrypt_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_CBC_256_AES, testnum, count, d);
        }
    }

    if (doit[D_IGE_128_AES]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_IGE_128_AES], c[D_IGE_128_AES][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            count =
                run_benchmark(async_jobs, AES_ige_128_encrypt_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_IGE_128_AES, testnum, count, d);
        }
    }
    if (doit[D_IGE_192_AES]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_IGE_192_AES], c[D_IGE_192_AES][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            count =
                run_benchmark(async_jobs, AES_ige_192_encrypt_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_IGE_192_AES, testnum, count, d);
        }
    }
    if (doit[D_IGE_256_AES]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_IGE_256_AES], c[D_IGE_256_AES][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            count =
                run_benchmark(async_jobs, AES_ige_256_encrypt_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_IGE_256_AES, testnum, count, d);
        }
    }
    if (doit[D_GHASH]) {
        for (i = 0; i < loopargs_len; i++) {
            loopargs[i].gcm_ctx =
                CRYPTO_gcm128_new(&aes_ks1, (block128_f) AES_encrypt);
            CRYPTO_gcm128_setiv(loopargs[i].gcm_ctx,
                                (unsigned char *)"0123456789ab", 12);
        }

        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_GHASH], c[D_GHASH][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            count = run_benchmark(async_jobs, CRYPTO_gcm128_aad_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_GHASH, testnum, count, d);
        }
        for (i = 0; i < loopargs_len; i++)
            CRYPTO_gcm128_release(loopargs[i].gcm_ctx);
    }
#ifndef OPENSSL_NO_CAMELLIA
    if (doit[D_CBC_128_CML]) {
        if (async_jobs > 0) {
            BIO_printf(bio_err, "Async mode is not supported with %s\n",
                       names[D_CBC_128_CML]);
            doit[D_CBC_128_CML] = 0;
        }
        for (testnum = 0; testnum < size_num && async_init == 0; testnum++) {
            print_message(names[D_CBC_128_CML], c[D_CBC_128_CML][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            for (count = 0, run = 1; COND(c[D_CBC_128_CML][testnum]); count++)
                Camellia_cbc_encrypt(loopargs[0].buf, loopargs[0].buf,
                                     (size_t)lengths[testnum], &camellia_ks1,
                                     iv, CAMELLIA_ENCRYPT);
            d = Time_F(STOP);
            print_result(D_CBC_128_CML, testnum, count, d);
        }
    }
    if (doit[D_CBC_192_CML]) {
        if (async_jobs > 0) {
            BIO_printf(bio_err, "Async mode is not supported with %s\n",
                       names[D_CBC_192_CML]);
            doit[D_CBC_192_CML] = 0;
        }
        for (testnum = 0; testnum < size_num && async_init == 0; testnum++) {
            print_message(names[D_CBC_192_CML], c[D_CBC_192_CML][testnum],
                          lengths[testnum], seconds.sym);
            if (async_jobs > 0) {
                BIO_printf(bio_err, "Async mode is not supported, exiting...");
                exit(1);
            }
            Time_F(START);
            for (count = 0, run = 1; COND(c[D_CBC_192_CML][testnum]); count++)
                Camellia_cbc_encrypt(loopargs[0].buf, loopargs[0].buf,
                                     (size_t)lengths[testnum], &camellia_ks2,
                                     iv, CAMELLIA_ENCRYPT);
            d = Time_F(STOP);
            print_result(D_CBC_192_CML, testnum, count, d);
        }
    }
    if (doit[D_CBC_256_CML]) {
        if (async_jobs > 0) {
            BIO_printf(bio_err, "Async mode is not supported with %s\n",
                       names[D_CBC_256_CML]);
            doit[D_CBC_256_CML] = 0;
        }
        for (testnum = 0; testnum < size_num && async_init == 0; testnum++) {
            print_message(names[D_CBC_256_CML], c[D_CBC_256_CML][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            for (count = 0, run = 1; COND(c[D_CBC_256_CML][testnum]); count++)
                Camellia_cbc_encrypt(loopargs[0].buf, loopargs[0].buf,
                                     (size_t)lengths[testnum], &camellia_ks3,
                                     iv, CAMELLIA_ENCRYPT);
            d = Time_F(STOP);
            print_result(D_CBC_256_CML, testnum, count, d);
        }
    }
#endif
#ifndef OPENSSL_NO_IDEA
    if (doit[D_CBC_IDEA]) {
        if (async_jobs > 0) {
            BIO_printf(bio_err, "Async mode is not supported with %s\n",
                       names[D_CBC_IDEA]);
            doit[D_CBC_IDEA] = 0;
        }
        for (testnum = 0; testnum < size_num && async_init == 0; testnum++) {
            print_message(names[D_CBC_IDEA], c[D_CBC_IDEA][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            for (count = 0, run = 1; COND(c[D_CBC_IDEA][testnum]); count++)
                IDEA_cbc_encrypt(loopargs[0].buf, loopargs[0].buf,
                                 (size_t)lengths[testnum], &idea_ks,
                                 iv, IDEA_ENCRYPT);
            d = Time_F(STOP);
            print_result(D_CBC_IDEA, testnum, count, d);
        }
    }
#endif
#ifndef OPENSSL_NO_SEED
    if (doit[D_CBC_SEED]) {
        if (async_jobs > 0) {
            BIO_printf(bio_err, "Async mode is not supported with %s\n",
                       names[D_CBC_SEED]);
            doit[D_CBC_SEED] = 0;
        }
        for (testnum = 0; testnum < size_num && async_init == 0; testnum++) {
            print_message(names[D_CBC_SEED], c[D_CBC_SEED][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            for (count = 0, run = 1; COND(c[D_CBC_SEED][testnum]); count++)
                SEED_cbc_encrypt(loopargs[0].buf, loopargs[0].buf,
                                 (size_t)lengths[testnum], &seed_ks, iv, 1);
            d = Time_F(STOP);
            print_result(D_CBC_SEED, testnum, count, d);
        }
    }
#endif
#ifndef OPENSSL_NO_RC2
    if (doit[D_CBC_RC2]) {
        if (async_jobs > 0) {
            BIO_printf(bio_err, "Async mode is not supported with %s\n",
                       names[D_CBC_RC2]);
            doit[D_CBC_RC2] = 0;
        }
        for (testnum = 0; testnum < size_num && async_init == 0; testnum++) {
            print_message(names[D_CBC_RC2], c[D_CBC_RC2][testnum],
                          lengths[testnum], seconds.sym);
            if (async_jobs > 0) {
                BIO_printf(bio_err, "Async mode is not supported, exiting...");
                exit(1);
            }
            Time_F(START);
            for (count = 0, run = 1; COND(c[D_CBC_RC2][testnum]); count++)
                RC2_cbc_encrypt(loopargs[0].buf, loopargs[0].buf,
                                (size_t)lengths[testnum], &rc2_ks,
                                iv, RC2_ENCRYPT);
            d = Time_F(STOP);
            print_result(D_CBC_RC2, testnum, count, d);
        }
    }
#endif
#ifndef OPENSSL_NO_RC5
    if (doit[D_CBC_RC5]) {
        if (async_jobs > 0) {
            BIO_printf(bio_err, "Async mode is not supported with %s\n",
                       names[D_CBC_RC5]);
            doit[D_CBC_RC5] = 0;
        }
        for (testnum = 0; testnum < size_num && async_init == 0; testnum++) {
            print_message(names[D_CBC_RC5], c[D_CBC_RC5][testnum],
                          lengths[testnum], seconds.sym);
            if (async_jobs > 0) {
                BIO_printf(bio_err, "Async mode is not supported, exiting...");
                exit(1);
            }
            Time_F(START);
            for (count = 0, run = 1; COND(c[D_CBC_RC5][testnum]); count++)
                RC5_32_cbc_encrypt(loopargs[0].buf, loopargs[0].buf,
                                   (size_t)lengths[testnum], &rc5_ks,
                                   iv, RC5_ENCRYPT);
            d = Time_F(STOP);
            print_result(D_CBC_RC5, testnum, count, d);
        }
    }
#endif
#ifndef OPENSSL_NO_BF
    if (doit[D_CBC_BF]) {
        if (async_jobs > 0) {
            BIO_printf(bio_err, "Async mode is not supported with %s\n",
                       names[D_CBC_BF]);
            doit[D_CBC_BF] = 0;
        }
        for (testnum = 0; testnum < size_num && async_init == 0; testnum++) {
            print_message(names[D_CBC_BF], c[D_CBC_BF][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            for (count = 0, run = 1; COND(c[D_CBC_BF][testnum]); count++)
                BF_cbc_encrypt(loopargs[0].buf, loopargs[0].buf,
                               (size_t)lengths[testnum], &bf_ks,
                               iv, BF_ENCRYPT);
            d = Time_F(STOP);
            print_result(D_CBC_BF, testnum, count, d);
        }
    }
#endif
#ifndef OPENSSL_NO_CAST
    if (doit[D_CBC_CAST]) {
        if (async_jobs > 0) {
            BIO_printf(bio_err, "Async mode is not supported with %s\n",
                       names[D_CBC_CAST]);
            doit[D_CBC_CAST] = 0;
        }
        for (testnum = 0; testnum < size_num && async_init == 0; testnum++) {
            print_message(names[D_CBC_CAST], c[D_CBC_CAST][testnum],
                          lengths[testnum], seconds.sym);
            Time_F(START);
            for (count = 0, run = 1; COND(c[D_CBC_CAST][testnum]); count++)
                CAST_cbc_encrypt(loopargs[0].buf, loopargs[0].buf,
                                 (size_t)lengths[testnum], &cast_ks,
                                 iv, CAST_ENCRYPT);
            d = Time_F(STOP);
            print_result(D_CBC_CAST, testnum, count, d);
        }
    }
#endif
    if (doit[D_RAND]) {
        for (testnum = 0; testnum < size_num; testnum++) {
            print_message(names[D_RAND], c[D_RAND][testnum], lengths[testnum],
                          seconds.sym);
            Time_F(START);
            count = run_benchmark(async_jobs, RAND_bytes_loop, loopargs);
            d = Time_F(STOP);
            print_result(D_RAND, testnum, count, d);
        }
    }

    if (doit[D_EVP]) {
        if (evp_cipher != NULL) {
            int (*loopfunc)(void *args) = EVP_Update_loop;

            if (multiblock && (EVP_CIPHER_flags(evp_cipher) &
                               EVP_CIPH_FLAG_TLS1_1_MULTIBLOCK)) {
                multiblock_speed(evp_cipher, lengths_single, &seconds);
                ret = 0;
                goto end;
            }

            names[D_EVP] = OBJ_nid2ln(EVP_CIPHER_nid(evp_cipher));

            if (EVP_CIPHER_mode(evp_cipher) == EVP_CIPH_CCM_MODE) {
                loopfunc = EVP_Update_loop_ccm;
            } else if (aead && (EVP_CIPHER_flags(evp_cipher) &
                                EVP_CIPH_FLAG_AEAD_CIPHER)) {
                loopfunc = EVP_Update_loop_aead;
                if (lengths == lengths_list) {
                    lengths = aead_lengths_list;
                    size_num = OSSL_NELEM(aead_lengths_list);
                }
            }

            for (testnum = 0; testnum < size_num; testnum++) {
                print_message(names[D_EVP], save_count, lengths[testnum],
                              seconds.sym);

                for (k = 0; k < loopargs_len; k++) {
                    loopargs[k].ctx = EVP_CIPHER_CTX_new();
                    EVP_CipherInit_ex(loopargs[k].ctx, evp_cipher, NULL, NULL,
                                      iv, decrypt ? 0 : 1);

                    EVP_CIPHER_CTX_set_padding(loopargs[k].ctx, 0);

                    keylen = EVP_CIPHER_CTX_key_length(loopargs[k].ctx);
                    loopargs[k].key = app_malloc(keylen, "evp_cipher key");
                    EVP_CIPHER_CTX_rand_key(loopargs[k].ctx, loopargs[k].key);
                    EVP_CipherInit_ex(loopargs[k].ctx, NULL, NULL,
                                      loopargs[k].key, NULL, -1);
                    OPENSSL_clear_free(loopargs[k].key, keylen);
                }

                Time_F(START);
                count = run_benchmark(async_jobs, loopfunc, loopargs);
                d = Time_F(STOP);
                for (k = 0; k < loopargs_len; k++) {
                    EVP_CIPHER_CTX_free(loopargs[k].ctx);
                }
                print_result(D_EVP, testnum, count, d);
            }
        } else if (evp_md != NULL) {
            names[D_EVP] = OBJ_nid2ln(EVP_MD_type(evp_md));

            for (testnum = 0; testnum < size_num; testnum++) {
                print_message(names[D_EVP], save_count, lengths[testnum],
                              seconds.sym);
                Time_F(START);
                count = run_benchmark(async_jobs, EVP_Digest_loop, loopargs);
                d = Time_F(STOP);
                print_result(D_EVP, testnum, count, d);
            }
        }
    }

    for (i = 0; i < loopargs_len; i++)
        if (RAND_bytes(loopargs[i].buf, 36) <= 0)
            goto end;

#ifndef OPENSSL_NO_RSA
    for (testnum = 0; testnum < RSA_NUM; testnum++) {
        int st = 0;
        if (!rsa_doit[testnum])
            continue;
        for (i = 0; i < loopargs_len; i++) {
            if (primes > 2) {
                /* we haven't set keys yet,  generate multi-prime RSA keys */
                BIGNUM *bn = BN_new();

                if (bn == NULL)
                    goto end;
                if (!BN_set_word(bn, RSA_F4)) {
                    BN_free(bn);
                    goto end;
                }

                BIO_printf(bio_err, "Generate multi-prime RSA key for %s\n",
                           rsa_choices[testnum].name);

                loopargs[i].rsa_key[testnum] = RSA_new();
                if (loopargs[i].rsa_key[testnum] == NULL) {
                    BN_free(bn);
                    goto end;
                }

                if (!RSA_generate_multi_prime_key(loopargs[i].rsa_key[testnum],
                                                  rsa_bits[testnum],
                                                  primes, bn, NULL)) {
                    BN_free(bn);
                    goto end;
                }
                BN_free(bn);
            }
            st = RSA_sign(NID_md5_sha1, loopargs[i].buf, 36, loopargs[i].buf2,
                          &loopargs[i].siglen, loopargs[i].rsa_key[testnum]);
            if (st == 0)
                break;
        }
        if (st == 0) {
            BIO_printf(bio_err,
                       "RSA sign failure.  No RSA sign will be done.\n");
            ERR_print_errors(bio_err);
            rsa_count = 1;
        } else {
            pkey_print_message("private", "rsa",
                               rsa_c[testnum][0], rsa_bits[testnum],
                               seconds.rsa);
            /* RSA_blinding_on(rsa_key[testnum],NULL); */
            Time_F(START);
            count = run_benchmark(async_jobs, RSA_sign_loop, loopargs);
            d = Time_F(STOP);
            BIO_printf(bio_err,
                       mr ? "+R1:%ld:%d:%.2f\n"
                       : "%ld %u bits private RSA's in %.2fs\n",
                       count, rsa_bits[testnum], d);
            rsa_results[testnum][0] = (double)count / d;
            rsa_count = count;
        }

        for (i = 0; i < loopargs_len; i++) {
            st = RSA_verify(NID_md5_sha1, loopargs[i].buf, 36, loopargs[i].buf2,
                            loopargs[i].siglen, loopargs[i].rsa_key[testnum]);
            if (st <= 0)
                break;
        }
        if (st <= 0) {
            BIO_printf(bio_err,
                       "RSA verify failure.  No RSA verify will be done.\n");
            ERR_print_errors(bio_err);
            rsa_doit[testnum] = 0;
        } else {
            pkey_print_message("public", "rsa",
                               rsa_c[testnum][1], rsa_bits[testnum],
                               seconds.rsa);
            Time_F(START);
            count = run_benchmark(async_jobs, RSA_verify_loop, loopargs);
            d = Time_F(STOP);
            BIO_printf(bio_err,
                       mr ? "+R2:%ld:%d:%.2f\n"
                       : "%ld %u bits public RSA's in %.2fs\n",
                       count, rsa_bits[testnum], d);
            rsa_results[testnum][1] = (double)count / d;
        }

        if (rsa_count <= 1) {
            /* if longer than 10s, don't do any more */
            for (testnum++; testnum < RSA_NUM; testnum++)
                rsa_doit[testnum] = 0;
        }
    }
#endif                          /* OPENSSL_NO_RSA */

    for (i = 0; i < loopargs_len; i++)
        if (RAND_bytes(loopargs[i].buf, 36) <= 0)
            goto end;

#ifndef OPENSSL_NO_DSA
    for (testnum = 0; testnum < DSA_NUM; testnum++) {
        int st = 0;
        if (!dsa_doit[testnum])
            continue;

        /* DSA_generate_key(dsa_key[testnum]); */
        /* DSA_sign_setup(dsa_key[testnum],NULL); */
        for (i = 0; i < loopargs_len; i++) {
            st = DSA_sign(0, loopargs[i].buf, 20, loopargs[i].buf2,
                          &loopargs[i].siglen, loopargs[i].dsa_key[testnum]);
            if (st == 0)
                break;
        }
        if (st == 0) {
            BIO_printf(bio_err,
                       "DSA sign failure.  No DSA sign will be done.\n");
            ERR_print_errors(bio_err);
            rsa_count = 1;
        } else {
            pkey_print_message("sign", "dsa",
                               dsa_c[testnum][0], dsa_bits[testnum],
                               seconds.dsa);
            Time_F(START);
            count = run_benchmark(async_jobs, DSA_sign_loop, loopargs);
            d = Time_F(STOP);
            BIO_printf(bio_err,
                       mr ? "+R3:%ld:%u:%.2f\n"
                       : "%ld %u bits DSA signs in %.2fs\n",
                       count, dsa_bits[testnum], d);
            dsa_results[testnum][0] = (double)count / d;
            rsa_count = count;
        }

        for (i = 0; i < loopargs_len; i++) {
            st = DSA_verify(0, loopargs[i].buf, 20, loopargs[i].buf2,
                            loopargs[i].siglen, loopargs[i].dsa_key[testnum]);
            if (st <= 0)
                break;
        }
        if (st <= 0) {
            BIO_printf(bio_err,
                       "DSA verify failure.  No DSA verify will be done.\n");
            ERR_print_errors(bio_err);
            dsa_doit[testnum] = 0;
        } else {
            pkey_print_message("verify", "dsa",
                               dsa_c[testnum][1], dsa_bits[testnum],
                               seconds.dsa);
            Time_F(START);
            count = run_benchmark(async_jobs, DSA_verify_loop, loopargs);
            d = Time_F(STOP);
            BIO_printf(bio_err,
                       mr ? "+R4:%ld:%u:%.2f\n"
                       : "%ld %u bits DSA verify in %.2fs\n",
                       count, dsa_bits[testnum], d);
            dsa_results[testnum][1] = (double)count / d;
        }

        if (rsa_count <= 1) {
            /* if longer than 10s, don't do any more */
            for (testnum++; testnum < DSA_NUM; testnum++)
                dsa_doit[testnum] = 0;
        }
    }
#endif                          /* OPENSSL_NO_DSA */

#ifndef OPENSSL_NO_EC
    for (testnum = 0; testnum < ECDSA_NUM; testnum++) {
        int st = 1;

        if (!ecdsa_doit[testnum])
            continue;           /* Ignore Curve */
        for (i = 0; i < loopargs_len; i++) {
            loopargs[i].ecdsa[testnum] =
                EC_KEY_new_by_curve_name(test_curves[testnum].nid);
            if (loopargs[i].ecdsa[testnum] == NULL) {
                st = 0;
                break;
            }
        }
        if (st == 0) {
            BIO_printf(bio_err, "ECDSA failure.\n");
            ERR_print_errors(bio_err);
            rsa_count = 1;
        } else {
            for (i = 0; i < loopargs_len; i++) {
                EC_KEY_precompute_mult(loopargs[i].ecdsa[testnum], NULL);
                /* Perform ECDSA signature test */
                EC_KEY_generate_key(loopargs[i].ecdsa[testnum]);
                st = ECDSA_sign(0, loopargs[i].buf, 20, loopargs[i].buf2,
                                &loopargs[i].siglen,
                                loopargs[i].ecdsa[testnum]);
                if (st == 0)
                    break;
            }
            if (st == 0) {
                BIO_printf(bio_err,
                           "ECDSA sign failure.  No ECDSA sign will be done.\n");
                ERR_print_errors(bio_err);
                rsa_count = 1;
            } else {
                pkey_print_message("sign", "ecdsa",
                                   ecdsa_c[testnum][0],
                                   test_curves[testnum].bits, seconds.ecdsa);
                Time_F(START);
                count = run_benchmark(async_jobs, ECDSA_sign_loop, loopargs);
                d = Time_F(STOP);

                BIO_printf(bio_err,
                           mr ? "+R5:%ld:%u:%.2f\n" :
                           "%ld %u bits ECDSA signs in %.2fs \n",
                           count, test_curves[testnum].bits, d);
                ecdsa_results[testnum][0] = (double)count / d;
                rsa_count = count;
            }

            /* Perform ECDSA verification test */
            for (i = 0; i < loopargs_len; i++) {
                st = ECDSA_verify(0, loopargs[i].buf, 20, loopargs[i].buf2,
                                  loopargs[i].siglen,
                                  loopargs[i].ecdsa[testnum]);
                if (st != 1)
                    break;
            }
            if (st != 1) {
                BIO_printf(bio_err,
                           "ECDSA verify failure.  No ECDSA verify will be done.\n");
                ERR_print_errors(bio_err);
                ecdsa_doit[testnum] = 0;
            } else {
                pkey_print_message("verify", "ecdsa",
                                   ecdsa_c[testnum][1],
                                   test_curves[testnum].bits, seconds.ecdsa);
                Time_F(START);
                count = run_benchmark(async_jobs, ECDSA_verify_loop, loopargs);
                d = Time_F(STOP);
                BIO_printf(bio_err,
                           mr ? "+R6:%ld:%u:%.2f\n"
                           : "%ld %u bits ECDSA verify in %.2fs\n",
                           count, test_curves[testnum].bits, d);
                ecdsa_results[testnum][1] = (double)count / d;
            }

            if (rsa_count <= 1) {
                /* if longer than 10s, don't do any more */
                for (testnum++; testnum < ECDSA_NUM; testnum++)
                    ecdsa_doit[testnum] = 0;
            }
        }
    }

    for (testnum = 0; testnum < EC_NUM; testnum++) {
        int ecdh_checks = 1;

        if (!ecdh_doit[testnum])
            continue;

        for (i = 0; i < loopargs_len; i++) {
            EVP_PKEY_CTX *kctx = NULL;
            EVP_PKEY_CTX *test_ctx = NULL;
            EVP_PKEY_CTX *ctx = NULL;
            EVP_PKEY *key_A = NULL;
            EVP_PKEY *key_B = NULL;
            size_t outlen;
            size_t test_outlen;

            /* Ensure that the error queue is empty */
            if (ERR_peek_error()) {
                BIO_printf(bio_err,
                           "WARNING: the error queue contains previous unhandled errors.\n");
                ERR_print_errors(bio_err);
            }

            /* Let's try to create a ctx directly from the NID: this works for
             * curves like Curve25519 that are not implemented through the low
             * level EC interface.
             * If this fails we try creating a EVP_PKEY_EC generic param ctx,
             * then we set the curve by NID before deriving the actual keygen
             * ctx for that specific curve. */
            kctx = EVP_PKEY_CTX_new_id(test_curves[testnum].nid, NULL); /* keygen ctx from NID */
            if (!kctx) {
                EVP_PKEY_CTX *pctx = NULL;
                EVP_PKEY *params = NULL;

                /* If we reach this code EVP_PKEY_CTX_new_id() failed and a
                 * "int_ctx_new:unsupported algorithm" error was added to the
                 * error queue.
                 * We remove it from the error queue as we are handling it. */
                unsigned long error = ERR_peek_error(); /* peek the latest error in the queue */
                if (error == ERR_peek_last_error() && /* oldest and latest errors match */
                    /* check that the error origin matches */
                    ERR_GET_LIB(error) == ERR_LIB_EVP &&
                    ERR_GET_FUNC(error) == EVP_F_INT_CTX_NEW &&
                    ERR_GET_REASON(error) == EVP_R_UNSUPPORTED_ALGORITHM)
                    ERR_get_error(); /* pop error from queue */
                if (ERR_peek_error()) {
                    BIO_printf(bio_err,
                               "Unhandled error in the error queue during ECDH init.\n");
                    ERR_print_errors(bio_err);
                    rsa_count = 1;
                    break;
                }

                if (            /* Create the context for parameter generation */
                       !(pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL)) ||
                       /* Initialise the parameter generation */
                       !EVP_PKEY_paramgen_init(pctx) ||
                       /* Set the curve by NID */
                       !EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx,
                                                               test_curves
                                                               [testnum].nid) ||
                       /* Create the parameter object params */
                       !EVP_PKEY_paramgen(pctx, &params)) {
                    ecdh_checks = 0;
                    BIO_printf(bio_err, "ECDH EC params init failure.\n");
                    ERR_print_errors(bio_err);
                    rsa_count = 1;
                    break;
                }
                /* Create the context for the key generation */
                kctx = EVP_PKEY_CTX_new(params, NULL);

                EVP_PKEY_free(params);
                params = NULL;
                EVP_PKEY_CTX_free(pctx);
                pctx = NULL;
            }
            if (kctx == NULL ||      /* keygen ctx is not null */
                !EVP_PKEY_keygen_init(kctx) /* init keygen ctx */ ) {
                ecdh_checks = 0;
                BIO_printf(bio_err, "ECDH keygen failure.\n");
                ERR_print_errors(bio_err);
                rsa_count = 1;
                break;
            }

            if (!EVP_PKEY_keygen(kctx, &key_A) || /* generate secret key A */
                !EVP_PKEY_keygen(kctx, &key_B) || /* generate secret key B */
                !(ctx = EVP_PKEY_CTX_new(key_A, NULL)) || /* derivation ctx from skeyA */
                !EVP_PKEY_derive_init(ctx) || /* init derivation ctx */
                !EVP_PKEY_derive_set_peer(ctx, key_B) || /* set peer pubkey in ctx */
                !EVP_PKEY_derive(ctx, NULL, &outlen) || /* determine max length */
                outlen == 0 ||  /* ensure outlen is a valid size */
                outlen > MAX_ECDH_SIZE /* avoid buffer overflow */ ) {
                ecdh_checks = 0;
                BIO_printf(bio_err, "ECDH key generation failure.\n");
                ERR_print_errors(bio_err);
                rsa_count = 1;
                break;
            }

            /* Here we perform a test run, comparing the output of a*B and b*A;
             * we try this here and assume that further EVP_PKEY_derive calls
             * never fail, so we can skip checks in the actually benchmarked
             * code, for maximum performance. */
            if (!(test_ctx = EVP_PKEY_CTX_new(key_B, NULL)) || /* test ctx from skeyB */
                !EVP_PKEY_derive_init(test_ctx) || /* init derivation test_ctx */
                !EVP_PKEY_derive_set_peer(test_ctx, key_A) || /* set peer pubkey in test_ctx */
                !EVP_PKEY_derive(test_ctx, NULL, &test_outlen) || /* determine max length */
                !EVP_PKEY_derive(ctx, loopargs[i].secret_a, &outlen) || /* compute a*B */
                !EVP_PKEY_derive(test_ctx, loopargs[i].secret_b, &test_outlen) || /* compute b*A */
                test_outlen != outlen /* compare output length */ ) {
                ecdh_checks = 0;
                BIO_printf(bio_err, "ECDH computation failure.\n");
                ERR_print_errors(bio_err);
                rsa_count = 1;
                break;
            }

            /* Compare the computation results: CRYPTO_memcmp() returns 0 if equal */
            if (CRYPTO_memcmp(loopargs[i].secret_a,
                              loopargs[i].secret_b, outlen)) {
                ecdh_checks = 0;
                BIO_printf(bio_err, "ECDH computations don't match.\n");
                ERR_print_errors(bio_err);
                rsa_count = 1;
                break;
            }

            loopargs[i].ecdh_ctx[testnum] = ctx;
            loopargs[i].outlen[testnum] = outlen;

            EVP_PKEY_free(key_A);
            EVP_PKEY_free(key_B);
            EVP_PKEY_CTX_free(kctx);
            kctx = NULL;
            EVP_PKEY_CTX_free(test_ctx);
            test_ctx = NULL;
        }
        if (ecdh_checks != 0) {
            pkey_print_message("", "ecdh",
                               ecdh_c[testnum][0],
                               test_curves[testnum].bits, seconds.ecdh);
            Time_F(START);
            count =
                run_benchmark(async_jobs, ECDH_EVP_derive_key_loop, loopargs);
            d = Time_F(STOP);
            BIO_printf(bio_err,
                       mr ? "+R7:%ld:%d:%.2f\n" :
                       "%ld %u-bits ECDH ops in %.2fs\n", count,
                       test_curves[testnum].bits, d);
            ecdh_results[testnum][0] = (double)count / d;
            rsa_count = count;
        }

        if (rsa_count <= 1) {
            /* if longer than 10s, don't do any more */
            for (testnum++; testnum < OSSL_NELEM(ecdh_doit); testnum++)
                ecdh_doit[testnum] = 0;
        }
    }

    for (testnum = 0; testnum < EdDSA_NUM; testnum++) {
        int st = 1;
        EVP_PKEY *ed_pkey = NULL;
        EVP_PKEY_CTX *ed_pctx = NULL;

        if (!eddsa_doit[testnum])
            continue;           /* Ignore Curve */
        for (i = 0; i < loopargs_len; i++) {
            loopargs[i].eddsa_ctx[testnum] = EVP_MD_CTX_new();
            if (loopargs[i].eddsa_ctx[testnum] == NULL) {
                st = 0;
                break;
            }

            if ((ed_pctx = EVP_PKEY_CTX_new_id(test_ed_curves[testnum].nid, NULL))
                    == NULL
                || !EVP_PKEY_keygen_init(ed_pctx)
                || !EVP_PKEY_keygen(ed_pctx, &ed_pkey)) {
                st = 0;
                EVP_PKEY_CTX_free(ed_pctx);
                break;
            }
            EVP_PKEY_CTX_free(ed_pctx);

            if (!EVP_DigestSignInit(loopargs[i].eddsa_ctx[testnum], NULL, NULL,
                                    NULL, ed_pkey)) {
                st = 0;
                EVP_PKEY_free(ed_pkey);
                break;
            }
            EVP_PKEY_free(ed_pkey);
        }
        if (st == 0) {
            BIO_printf(bio_err, "EdDSA failure.\n");
            ERR_print_errors(bio_err);
            rsa_count = 1;
        } else {
            for (i = 0; i < loopargs_len; i++) {
                /* Perform EdDSA signature test */
                loopargs[i].sigsize = test_ed_curves[testnum].sigsize;
                st = EVP_DigestSign(loopargs[i].eddsa_ctx[testnum],
                                    loopargs[i].buf2, &loopargs[i].sigsize,
                                    loopargs[i].buf, 20);
                if (st == 0)
                    break;
            }
            if (st == 0) {
                BIO_printf(bio_err,
                           "EdDSA sign failure.  No EdDSA sign will be done.\n");
                ERR_print_errors(bio_err);
                rsa_count = 1;
            } else {
                pkey_print_message("sign", test_ed_curves[testnum].name,
                                   eddsa_c[testnum][0],
                                   test_ed_curves[testnum].bits, seconds.eddsa);
                Time_F(START);
                count = run_benchmark(async_jobs, EdDSA_sign_loop, loopargs);
                d = Time_F(STOP);

                BIO_printf(bio_err,
                           mr ? "+R8:%ld:%u:%s:%.2f\n" :
                           "%ld %u bits %s signs in %.2fs \n",
                           count, test_ed_curves[testnum].bits,
                           test_ed_curves[testnum].name, d);
                eddsa_results[testnum][0] = (double)count / d;
                rsa_count = count;
            }

            /* Perform EdDSA verification test */
            for (i = 0; i < loopargs_len; i++) {
                st = EVP_DigestVerify(loopargs[i].eddsa_ctx[testnum],
                                      loopargs[i].buf2, loopargs[i].sigsize,
                                      loopargs[i].buf, 20);
                if (st != 1)
                    break;
            }
            if (st != 1) {
                BIO_printf(bio_err,
                           "EdDSA verify failure.  No EdDSA verify will be done.\n");
                ERR_print_errors(bio_err);
                eddsa_doit[testnum] = 0;
            } else {
                pkey_print_message("verify", test_ed_curves[testnum].name,
                                   eddsa_c[testnum][1],
                                   test_ed_curves[testnum].bits, seconds.eddsa);
                Time_F(START);
                count = run_benchmark(async_jobs, EdDSA_verify_loop, loopargs);
                d = Time_F(STOP);
                BIO_printf(bio_err,
                           mr ? "+R9:%ld:%u:%s:%.2f\n"
                           : "%ld %u bits %s verify in %.2fs\n",
                           count, test_ed_curves[testnum].bits,
                           test_ed_curves[testnum].name, d);
                eddsa_results[testnum][1] = (double)count / d;
            }

            if (rsa_count <= 1) {
                /* if longer than 10s, don't do any more */
                for (testnum++; testnum < EdDSA_NUM; testnum++)
                    eddsa_doit[testnum] = 0;
            }
        }
    }

#endif                          /* OPENSSL_NO_EC */
#ifndef NO_FORK
 show_res:
#endif
    if (!mr) {
        printf("%s\n", OpenSSL_version(OPENSSL_VERSION));
        printf("%s\n", OpenSSL_version(OPENSSL_BUILT_ON));
        printf("options:");
        printf("%s ", BN_options());
#ifndef OPENSSL_NO_MD2
        printf("%s ", MD2_options());
#endif
#ifndef OPENSSL_NO_RC4
        printf("%s ", RC4_options());
#endif
#ifndef OPENSSL_NO_DES
        printf("%s ", DES_options());
#endif
        printf("%s ", AES_options());
#ifndef OPENSSL_NO_IDEA
        printf("%s ", IDEA_options());
#endif
#ifndef OPENSSL_NO_BF
        printf("%s ", BF_options());
#endif
        printf("\n%s\n", OpenSSL_version(OPENSSL_CFLAGS));
    }

    if (pr_header) {
        if (mr)
            printf("+H");
        else {
            printf
                ("The 'numbers' are in 1000s of bytes per second processed.\n");
            printf("type        ");
        }
        for (testnum = 0; testnum < size_num; testnum++)
            printf(mr ? ":%d" : "%7d bytes", lengths[testnum]);
        printf("\n");
    }

    for (k = 0; k < ALGOR_NUM; k++) {
        if (!doit[k])
            continue;
        if (mr)
            printf("+F:%u:%s", k, names[k]);
        else
            printf("%-13s", names[k]);
        for (testnum = 0; testnum < size_num; testnum++) {
            if (results[k][testnum] > 10000 && !mr)
                printf(" %11.2fk", results[k][testnum] / 1e3);
            else
                printf(mr ? ":%.2f" : " %11.2f ", results[k][testnum]);
        }
        printf("\n");
    }
#ifndef OPENSSL_NO_RSA
    testnum = 1;
    for (k = 0; k < RSA_NUM; k++) {
        if (!rsa_doit[k])
            continue;
        if (testnum && !mr) {
            printf("%18ssign    verify    sign/s verify/s\n", " ");
            testnum = 0;
        }
        if (mr)
            printf("+F2:%u:%u:%f:%f\n",
                   k, rsa_bits[k], rsa_results[k][0], rsa_results[k][1]);
        else
            printf("rsa %4u bits %8.6fs %8.6fs %8.1f %8.1f\n",
                   rsa_bits[k], 1.0 / rsa_results[k][0], 1.0 / rsa_results[k][1],
                   rsa_results[k][0], rsa_results[k][1]);
    }
#endif
#ifndef OPENSSL_NO_DSA
    testnum = 1;
    for (k = 0; k < DSA_NUM; k++) {
        if (!dsa_doit[k])
            continue;
        if (testnum && !mr) {
            printf("%18ssign    verify    sign/s verify/s\n", " ");
            testnum = 0;
        }
        if (mr)
            printf("+F3:%u:%u:%f:%f\n",
                   k, dsa_bits[k], dsa_results[k][0], dsa_results[k][1]);
        else
            printf("dsa %4u bits %8.6fs %8.6fs %8.1f %8.1f\n",
                   dsa_bits[k], 1.0 / dsa_results[k][0], 1.0 / dsa_results[k][1],
                   dsa_results[k][0], dsa_results[k][1]);
    }
#endif
#ifndef OPENSSL_NO_EC
    testnum = 1;
    for (k = 0; k < OSSL_NELEM(ecdsa_doit); k++) {
        if (!ecdsa_doit[k])
            continue;
        if (testnum && !mr) {
            printf("%30ssign    verify    sign/s verify/s\n", " ");
            testnum = 0;
        }

        if (mr)
            printf("+F4:%u:%u:%f:%f\n",
                   k, test_curves[k].bits,
                   ecdsa_results[k][0], ecdsa_results[k][1]);
        else
            printf("%4u bits ecdsa (%s) %8.4fs %8.4fs %8.1f %8.1f\n",
                   test_curves[k].bits, test_curves[k].name,
                   1.0 / ecdsa_results[k][0], 1.0 / ecdsa_results[k][1],
                   ecdsa_results[k][0], ecdsa_results[k][1]);
    }

    testnum = 1;
    for (k = 0; k < EC_NUM; k++) {
        if (!ecdh_doit[k])
            continue;
        if (testnum && !mr) {
            printf("%30sop      op/s\n", " ");
            testnum = 0;
        }
        if (mr)
            printf("+F5:%u:%u:%f:%f\n",
                   k, test_curves[k].bits,
                   ecdh_results[k][0], 1.0 / ecdh_results[k][0]);

        else
            printf("%4u bits ecdh (%s) %8.4fs %8.1f\n",
                   test_curves[k].bits, test_curves[k].name,
                   1.0 / ecdh_results[k][0], ecdh_results[k][0]);
    }

    testnum = 1;
    for (k = 0; k < OSSL_NELEM(eddsa_doit); k++) {
        if (!eddsa_doit[k])
            continue;
        if (testnum && !mr) {
            printf("%30ssign    verify    sign/s verify/s\n", " ");
            testnum = 0;
        }

        if (mr)
            printf("+F6:%u:%u:%s:%f:%f\n",
                   k, test_ed_curves[k].bits, test_ed_curves[k].name,
                   eddsa_results[k][0], eddsa_results[k][1]);
        else
            printf("%4u bits EdDSA (%s) %8.4fs %8.4fs %8.1f %8.1f\n",
                   test_ed_curves[k].bits, test_ed_curves[k].name,
                   1.0 / eddsa_results[k][0], 1.0 / eddsa_results[k][1],
                   eddsa_results[k][0], eddsa_results[k][1]);
    }
#endif

    ret = 0;

 end:
    ERR_print_errors(bio_err);
    for (i = 0; i < loopargs_len; i++) {
        OPENSSL_free(loopargs[i].buf_malloc);
        OPENSSL_free(loopargs[i].buf2_malloc);

#ifndef OPENSSL_NO_RSA
        for (k = 0; k < RSA_NUM; k++)
            RSA_free(loopargs[i].rsa_key[k]);
#endif
#ifndef OPENSSL_NO_DSA
        for (k = 0; k < DSA_NUM; k++)
            DSA_free(loopargs[i].dsa_key[k]);
#endif
#ifndef OPENSSL_NO_EC
        for (k = 0; k < ECDSA_NUM; k++)
            EC_KEY_free(loopargs[i].ecdsa[k]);
        for (k = 0; k < EC_NUM; k++)
            EVP_PKEY_CTX_free(loopargs[i].ecdh_ctx[k]);
        for (k = 0; k < EdDSA_NUM; k++)
            EVP_MD_CTX_free(loopargs[i].eddsa_ctx[k]);
        OPENSSL_free(loopargs[i].secret_a);
        OPENSSL_free(loopargs[i].secret_b);
#endif
    }

    if (async_jobs > 0) {
        for (i = 0; i < loopargs_len; i++)
            ASYNC_WAIT_CTX_free(loopargs[i].wait_ctx);
    }

    if (async_init) {
        ASYNC_cleanup_thread();
    }
    OPENSSL_free(loopargs);
    release_engine(e);
    return ret;
}

static void print_message(const char *s, long num, int length, int tm)
{
#ifdef SIGALRM
    BIO_printf(bio_err,
               mr ? "+DT:%s:%d:%d\n"
               : "Doing %s for %ds on %d size blocks: ", s, tm, length);
    (void)BIO_flush(bio_err);
    alarm(tm);
#else
    BIO_printf(bio_err,
               mr ? "+DN:%s:%ld:%d\n"
               : "Doing %s %ld times on %d size blocks: ", s, num, length);
    (void)BIO_flush(bio_err);
#endif
}

static void pkey_print_message(const char *str, const char *str2, long num,
                               unsigned int bits, int tm)
{
#ifdef SIGALRM
    BIO_printf(bio_err,
               mr ? "+DTP:%d:%s:%s:%d\n"
               : "Doing %u bits %s %s's for %ds: ", bits, str, str2, tm);
    (void)BIO_flush(bio_err);
    alarm(tm);
#else
    BIO_printf(bio_err,
               mr ? "+DNP:%ld:%d:%s:%s\n"
               : "Doing %ld %u bits %s %s's: ", num, bits, str, str2);
    (void)BIO_flush(bio_err);
#endif
}

static void print_result(int alg, int run_no, int count, double time_used)
{
    if (count == -1) {
        BIO_puts(bio_err, "EVP error!\n");
        exit(1);
    }
    BIO_printf(bio_err,
               mr ? "+R:%d:%s:%f\n"
               : "%d %s's in %.2fs\n", count, names[alg], time_used);
    results[alg][run_no] = ((double)count) / time_used * lengths[run_no];
}

#ifndef NO_FORK
static char *sstrsep(char **string, const char *delim)
{
    char isdelim[256];
    char *token = *string;

    if (**string == 0)
        return NULL;

    memset(isdelim, 0, sizeof(isdelim));
    isdelim[0] = 1;

    while (*delim) {
        isdelim[(unsigned char)(*delim)] = 1;
        delim++;
    }

    while (!isdelim[(unsigned char)(**string)]) {
        (*string)++;
    }

    if (**string) {
        **string = 0;
        (*string)++;
    }

    return token;
}

static int do_multi(int multi, int size_num)
{
    int n;
    int fd[2];
    int *fds;
    static char sep[] = ":";

    fds = app_malloc(sizeof(*fds) * multi, "fd buffer for do_multi");
    for (n = 0; n < multi; ++n) {
        if (pipe(fd) == -1) {
            BIO_printf(bio_err, "pipe failure\n");
            exit(1);
        }
        fflush(stdout);
        (void)BIO_flush(bio_err);
        if (fork()) {
            close(fd[1]);
            fds[n] = fd[0];
        } else {
            close(fd[0]);
            close(1);
            if (dup(fd[1]) == -1) {
                BIO_printf(bio_err, "dup failed\n");
                exit(1);
            }
            close(fd[1]);
            mr = 1;
            usertime = 0;
            free(fds);
            return 0;
        }
        printf("Forked child %d\n", n);
    }

    /* for now, assume the pipe is long enough to take all the output */
    for (n = 0; n < multi; ++n) {
        FILE *f;
        char buf[1024];
        char *p;

        f = fdopen(fds[n], "r");
        while (fgets(buf, sizeof(buf), f)) {
            p = strchr(buf, '\n');
            if (p)
                *p = '\0';
            if (buf[0] != '+') {
                BIO_printf(bio_err,
                           "Don't understand line '%s' from child %d\n", buf,
                           n);
                continue;
            }
            printf("Got: %s from %d\n", buf, n);
            if (strncmp(buf, "+F:", 3) == 0) {
                int alg;
                int j;

                p = buf + 3;
                alg = atoi(sstrsep(&p, sep));
                sstrsep(&p, sep);
                for (j = 0; j < size_num; ++j)
                    results[alg][j] += atof(sstrsep(&p, sep));
            } else if (strncmp(buf, "+F2:", 4) == 0) {
                int k;
                double d;

                p = buf + 4;
                k = atoi(sstrsep(&p, sep));
                sstrsep(&p, sep);

                d = atof(sstrsep(&p, sep));
                rsa_results[k][0] += d;

                d = atof(sstrsep(&p, sep));
                rsa_results[k][1] += d;
            }
# ifndef OPENSSL_NO_DSA
            else if (strncmp(buf, "+F3:", 4) == 0) {
                int k;
                double d;

                p = buf + 4;
                k = atoi(sstrsep(&p, sep));
                sstrsep(&p, sep);

                d = atof(sstrsep(&p, sep));
                dsa_results[k][0] += d;

                d = atof(sstrsep(&p, sep));
                dsa_results[k][1] += d;
            }
# endif
# ifndef OPENSSL_NO_EC
            else if (strncmp(buf, "+F4:", 4) == 0) {
                int k;
                double d;

                p = buf + 4;
                k = atoi(sstrsep(&p, sep));
                sstrsep(&p, sep);

                d = atof(sstrsep(&p, sep));
                ecdsa_results[k][0] += d;

                d = atof(sstrsep(&p, sep));
                ecdsa_results[k][1] += d;
            } else if (strncmp(buf, "+F5:", 4) == 0) {
                int k;
                double d;

                p = buf + 4;
                k = atoi(sstrsep(&p, sep));
                sstrsep(&p, sep);

                d = atof(sstrsep(&p, sep));
                ecdh_results[k][0] += d;
            } else if (strncmp(buf, "+F6:", 4) == 0) {
                int k;
                double d;

                p = buf + 4;
                k = atoi(sstrsep(&p, sep));
                sstrsep(&p, sep);

                d = atof(sstrsep(&p, sep));
                eddsa_results[k][0] += d;

                d = atof(sstrsep(&p, sep));
                eddsa_results[k][1] += d;
            }
# endif

            else if (strncmp(buf, "+H:", 3) == 0) {
                ;
            } else
                BIO_printf(bio_err, "Unknown type '%s' from child %d\n", buf,
                           n);
        }

        fclose(f);
    }
    free(fds);
    return 1;
}
#endif

static void multiblock_speed(const EVP_CIPHER *evp_cipher, int lengths_single,
                             const openssl_speed_sec_t *seconds)
{
    static const int mblengths_list[] =
        { 8 * 1024, 2 * 8 * 1024, 4 * 8 * 1024, 8 * 8 * 1024, 8 * 16 * 1024 };
    const int *mblengths = mblengths_list;
    int j, count, keylen, num = OSSL_NELEM(mblengths_list);
    const char *alg_name;
    unsigned char *inp, *out, *key, no_key[32], no_iv[16];
    EVP_CIPHER_CTX *ctx;
    double d = 0.0;

    if (lengths_single) {
        mblengths = &lengths_single;
        num = 1;
    }

    inp = app_malloc(mblengths[num - 1], "multiblock input buffer");
    out = app_malloc(mblengths[num - 1] + 1024, "multiblock output buffer");
    ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, evp_cipher, NULL, NULL, no_iv);

    keylen = EVP_CIPHER_CTX_key_length(ctx);
    key = app_malloc(keylen, "evp_cipher key");
    EVP_CIPHER_CTX_rand_key(ctx, key);
    EVP_EncryptInit_ex(ctx, NULL, NULL, key, NULL);
    OPENSSL_clear_free(key, keylen);

    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_MAC_KEY, sizeof(no_key), no_key);
    alg_name = OBJ_nid2ln(EVP_CIPHER_nid(evp_cipher));

    for (j = 0; j < num; j++) {
        print_message(alg_name, 0, mblengths[j], seconds->sym);
        Time_F(START);
        for (count = 0, run = 1; run && count < 0x7fffffff; count++) {
            unsigned char aad[EVP_AEAD_TLS1_AAD_LEN];
            EVP_CTRL_TLS1_1_MULTIBLOCK_PARAM mb_param;
            size_t len = mblengths[j];
            int packlen;

            memset(aad, 0, 8);  /* avoid uninitialized values */
            aad[8] = 23;        /* SSL3_RT_APPLICATION_DATA */
            aad[9] = 3;         /* version */
            aad[10] = 2;
            aad[11] = 0;        /* length */
            aad[12] = 0;
            mb_param.out = NULL;
            mb_param.inp = aad;
            mb_param.len = len;
            mb_param.interleave = 8;

            packlen = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_TLS1_1_MULTIBLOCK_AAD,
                                          sizeof(mb_param), &mb_param);

            if (packlen > 0) {
                mb_param.out = out;
                mb_param.inp = inp;
                mb_param.len = len;
                EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_TLS1_1_MULTIBLOCK_ENCRYPT,
                                    sizeof(mb_param), &mb_param);
            } else {
                int pad;

                RAND_bytes(out, 16);
                len += 16;
                aad[11] = (unsigned char)(len >> 8);
                aad[12] = (unsigned char)(len);
                pad = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_TLS1_AAD,
                                          EVP_AEAD_TLS1_AAD_LEN, aad);
                EVP_Cipher(ctx, out, inp, len + pad);
            }
        }
        d = Time_F(STOP);
        BIO_printf(bio_err, mr ? "+R:%d:%s:%f\n"
                   : "%d %s's in %.2fs\n", count, "evp", d);
        results[D_EVP][j] = ((double)count) / d * mblengths[j];
    }

    if (mr) {
        fprintf(stdout, "+H");
        for (j = 0; j < num; j++)
            fprintf(stdout, ":%d", mblengths[j]);
        fprintf(stdout, "\n");
        fprintf(stdout, "+F:%d:%s", D_EVP, alg_name);
        for (j = 0; j < num; j++)
            fprintf(stdout, ":%.2f", results[D_EVP][j]);
        fprintf(stdout, "\n");
    } else {
        fprintf(stdout,
                "The 'numbers' are in 1000s of bytes per second processed.\n");
        fprintf(stdout, "type                    ");
        for (j = 0; j < num; j++)
            fprintf(stdout, "%7d bytes", mblengths[j]);
        fprintf(stdout, "\n");
        fprintf(stdout, "%-24s", alg_name);

        for (j = 0; j < num; j++) {
            if (results[D_EVP][j] > 10000)
                fprintf(stdout, " %11.2fk", results[D_EVP][j] / 1e3);
            else
                fprintf(stdout, " %11.2f ", results[D_EVP][j]);
        }
        fprintf(stdout, "\n");
    }

    OPENSSL_free(inp);
    OPENSSL_free(out);
    EVP_CIPHER_CTX_free(ctx);
}
