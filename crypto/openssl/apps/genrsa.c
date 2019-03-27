/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslconf.h>
#ifdef OPENSSL_NO_RSA
NON_EMPTY_TRANSLATION_UNIT
#else

# include <stdio.h>
# include <string.h>
# include <sys/types.h>
# include <sys/stat.h>
# include "apps.h"
# include "progs.h"
# include <openssl/bio.h>
# include <openssl/err.h>
# include <openssl/bn.h>
# include <openssl/rsa.h>
# include <openssl/evp.h>
# include <openssl/x509.h>
# include <openssl/pem.h>
# include <openssl/rand.h>

# define DEFBITS 2048
# define DEFPRIMES 2

static int genrsa_cb(int p, int n, BN_GENCB *cb);

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_3, OPT_F4, OPT_ENGINE,
    OPT_OUT, OPT_PASSOUT, OPT_CIPHER, OPT_PRIMES,
    OPT_R_ENUM
} OPTION_CHOICE;

const OPTIONS genrsa_options[] = {
    {"help", OPT_HELP, '-', "Display this summary"},
    {"3", OPT_3, '-', "Use 3 for the E value"},
    {"F4", OPT_F4, '-', "Use F4 (0x10001) for the E value"},
    {"f4", OPT_F4, '-', "Use F4 (0x10001) for the E value"},
    {"out", OPT_OUT, '>', "Output the key to specified file"},
    OPT_R_OPTIONS,
    {"passout", OPT_PASSOUT, 's', "Output file pass phrase source"},
    {"", OPT_CIPHER, '-', "Encrypt the output with any supported cipher"},
# ifndef OPENSSL_NO_ENGINE
    {"engine", OPT_ENGINE, 's', "Use engine, possibly a hardware device"},
# endif
    {"primes", OPT_PRIMES, 'p', "Specify number of primes"},
    {NULL}
};

int genrsa_main(int argc, char **argv)
{
    BN_GENCB *cb = BN_GENCB_new();
    PW_CB_DATA cb_data;
    ENGINE *eng = NULL;
    BIGNUM *bn = BN_new();
    BIO *out = NULL;
    const BIGNUM *e;
    RSA *rsa = NULL;
    const EVP_CIPHER *enc = NULL;
    int ret = 1, num = DEFBITS, private = 0, primes = DEFPRIMES;
    unsigned long f4 = RSA_F4;
    char *outfile = NULL, *passoutarg = NULL, *passout = NULL;
    char *prog, *hexe, *dece;
    OPTION_CHOICE o;

    if (bn == NULL || cb == NULL)
        goto end;

    BN_GENCB_set(cb, genrsa_cb, bio_err);

    prog = opt_init(argc, argv, genrsa_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            ret = 0;
            opt_help(genrsa_options);
            goto end;
        case OPT_3:
            f4 = 3;
            break;
        case OPT_F4:
            f4 = RSA_F4;
            break;
        case OPT_OUT:
            outfile = opt_arg();
            break;
        case OPT_ENGINE:
            eng = setup_engine(opt_arg(), 0);
            break;
        case OPT_R_CASES:
            if (!opt_rand(o))
                goto end;
            break;
        case OPT_PASSOUT:
            passoutarg = opt_arg();
            break;
        case OPT_CIPHER:
            if (!opt_cipher(opt_unknown(), &enc))
                goto end;
            break;
        case OPT_PRIMES:
            if (!opt_int(opt_arg(), &primes))
                goto end;
            break;
        }
    }
    argc = opt_num_rest();
    argv = opt_rest();

    if (argc == 1) {
        if (!opt_int(argv[0], &num) || num <= 0)
            goto end;
        if (num > OPENSSL_RSA_MAX_MODULUS_BITS)
            BIO_printf(bio_err,
                       "Warning: It is not recommended to use more than %d bit for RSA keys.\n"
                       "         Your key size is %d! Larger key size may behave not as expected.\n",
                       OPENSSL_RSA_MAX_MODULUS_BITS, num);
    } else if (argc > 0) {
        BIO_printf(bio_err, "Extra arguments given.\n");
        goto opthelp;
    }

    private = 1;
    if (!app_passwd(NULL, passoutarg, NULL, &passout)) {
        BIO_printf(bio_err, "Error getting password\n");
        goto end;
    }

    out = bio_open_owner(outfile, FORMAT_PEM, private);
    if (out == NULL)
        goto end;

    BIO_printf(bio_err, "Generating RSA private key, %d bit long modulus (%d primes)\n",
               num, primes);
    rsa = eng ? RSA_new_method(eng) : RSA_new();
    if (rsa == NULL)
        goto end;

    if (!BN_set_word(bn, f4)
        || !RSA_generate_multi_prime_key(rsa, num, primes, bn, cb))
        goto end;

    RSA_get0_key(rsa, NULL, &e, NULL);
    hexe = BN_bn2hex(e);
    dece = BN_bn2dec(e);
    if (hexe && dece) {
        BIO_printf(bio_err, "e is %s (0x%s)\n", dece, hexe);
    }
    OPENSSL_free(hexe);
    OPENSSL_free(dece);
    cb_data.password = passout;
    cb_data.prompt_info = outfile;
    assert(private);
    if (!PEM_write_bio_RSAPrivateKey(out, rsa, enc, NULL, 0,
                                     (pem_password_cb *)password_callback,
                                     &cb_data))
        goto end;

    ret = 0;
 end:
    BN_free(bn);
    BN_GENCB_free(cb);
    RSA_free(rsa);
    BIO_free_all(out);
    release_engine(eng);
    OPENSSL_free(passout);
    if (ret != 0)
        ERR_print_errors(bio_err);
    return ret;
}

static int genrsa_cb(int p, int n, BN_GENCB *cb)
{
    char c = '*';

    if (p == 0)
        c = '.';
    if (p == 1)
        c = '+';
    if (p == 2)
        c = '*';
    if (p == 3)
        c = '\n';
    BIO_write(BN_GENCB_get_arg(cb), &c, 1);
    (void)BIO_flush(BN_GENCB_get_arg(cb));
    return 1;
}
#endif
