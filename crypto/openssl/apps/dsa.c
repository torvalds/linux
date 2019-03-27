/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslconf.h>
#ifdef OPENSSL_NO_DSA
NON_EMPTY_TRANSLATION_UNIT
#else

# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <time.h>
# include "apps.h"
# include "progs.h"
# include <openssl/bio.h>
# include <openssl/err.h>
# include <openssl/dsa.h>
# include <openssl/evp.h>
# include <openssl/x509.h>
# include <openssl/pem.h>
# include <openssl/bn.h>

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_INFORM, OPT_OUTFORM, OPT_IN, OPT_OUT, OPT_ENGINE,
    /* Do not change the order here; see case statements below */
    OPT_PVK_NONE, OPT_PVK_WEAK, OPT_PVK_STRONG,
    OPT_NOOUT, OPT_TEXT, OPT_MODULUS, OPT_PUBIN,
    OPT_PUBOUT, OPT_CIPHER, OPT_PASSIN, OPT_PASSOUT
} OPTION_CHOICE;

const OPTIONS dsa_options[] = {
    {"help", OPT_HELP, '-', "Display this summary"},
    {"inform", OPT_INFORM, 'f', "Input format, DER PEM PVK"},
    {"outform", OPT_OUTFORM, 'f', "Output format, DER PEM PVK"},
    {"in", OPT_IN, 's', "Input key"},
    {"out", OPT_OUT, '>', "Output file"},
    {"noout", OPT_NOOUT, '-', "Don't print key out"},
    {"text", OPT_TEXT, '-', "Print the key in text"},
    {"modulus", OPT_MODULUS, '-', "Print the DSA public value"},
    {"pubin", OPT_PUBIN, '-', "Expect a public key in input file"},
    {"pubout", OPT_PUBOUT, '-', "Output public key, not private"},
    {"passin", OPT_PASSIN, 's', "Input file pass phrase source"},
    {"passout", OPT_PASSOUT, 's', "Output file pass phrase source"},
    {"", OPT_CIPHER, '-', "Any supported cipher"},
# ifndef OPENSSL_NO_RC4
    {"pvk-strong", OPT_PVK_STRONG, '-', "Enable 'Strong' PVK encoding level (default)"},
    {"pvk-weak", OPT_PVK_WEAK, '-', "Enable 'Weak' PVK encoding level"},
    {"pvk-none", OPT_PVK_NONE, '-', "Don't enforce PVK encoding"},
# endif
# ifndef OPENSSL_NO_ENGINE
    {"engine", OPT_ENGINE, 's', "Use engine e, possibly a hardware device"},
# endif
    {NULL}
};

int dsa_main(int argc, char **argv)
{
    BIO *out = NULL;
    DSA *dsa = NULL;
    ENGINE *e = NULL;
    const EVP_CIPHER *enc = NULL;
    char *infile = NULL, *outfile = NULL, *prog;
    char *passin = NULL, *passout = NULL, *passinarg = NULL, *passoutarg = NULL;
    OPTION_CHOICE o;
    int informat = FORMAT_PEM, outformat = FORMAT_PEM, text = 0, noout = 0;
    int i, modulus = 0, pubin = 0, pubout = 0, ret = 1;
# ifndef OPENSSL_NO_RC4
    int pvk_encr = 2;
# endif
    int private = 0;

    prog = opt_init(argc, argv, dsa_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opthelp:
            ret = 0;
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(dsa_options);
            ret = 0;
            goto end;
        case OPT_INFORM:
            if (!opt_format(opt_arg(), OPT_FMT_ANY, &informat))
                goto opthelp;
            break;
        case OPT_IN:
            infile = opt_arg();
            break;
        case OPT_OUTFORM:
            if (!opt_format(opt_arg(), OPT_FMT_ANY, &outformat))
                goto opthelp;
            break;
        case OPT_OUT:
            outfile = opt_arg();
            break;
        case OPT_ENGINE:
            e = setup_engine(opt_arg(), 0);
            break;
        case OPT_PASSIN:
            passinarg = opt_arg();
            break;
        case OPT_PASSOUT:
            passoutarg = opt_arg();
            break;
        case OPT_PVK_STRONG:    /* pvk_encr:= 2 */
        case OPT_PVK_WEAK:      /* pvk_encr:= 1 */
        case OPT_PVK_NONE:      /* pvk_encr:= 0 */
#ifndef OPENSSL_NO_RC4
            pvk_encr = (o - OPT_PVK_NONE);
#endif
            break;
        case OPT_NOOUT:
            noout = 1;
            break;
        case OPT_TEXT:
            text = 1;
            break;
        case OPT_MODULUS:
            modulus = 1;
            break;
        case OPT_PUBIN:
            pubin = 1;
            break;
        case OPT_PUBOUT:
            pubout = 1;
            break;
        case OPT_CIPHER:
            if (!opt_cipher(opt_unknown(), &enc))
                goto end;
            break;
        }
    }
    argc = opt_num_rest();
    if (argc != 0)
        goto opthelp;

    private = pubin || pubout ? 0 : 1;
    if (text && !pubin)
        private = 1;

    if (!app_passwd(passinarg, passoutarg, &passin, &passout)) {
        BIO_printf(bio_err, "Error getting passwords\n");
        goto end;
    }

    BIO_printf(bio_err, "read DSA key\n");
    {
        EVP_PKEY *pkey;

        if (pubin)
            pkey = load_pubkey(infile, informat, 1, passin, e, "Public Key");
        else
            pkey = load_key(infile, informat, 1, passin, e, "Private Key");

        if (pkey != NULL) {
            dsa = EVP_PKEY_get1_DSA(pkey);
            EVP_PKEY_free(pkey);
        }
    }
    if (dsa == NULL) {
        BIO_printf(bio_err, "unable to load Key\n");
        ERR_print_errors(bio_err);
        goto end;
    }

    out = bio_open_owner(outfile, outformat, private);
    if (out == NULL)
        goto end;

    if (text) {
        assert(pubin || private);
        if (!DSA_print(out, dsa, 0)) {
            perror(outfile);
            ERR_print_errors(bio_err);
            goto end;
        }
    }

    if (modulus) {
        const BIGNUM *pub_key = NULL;
        DSA_get0_key(dsa, &pub_key, NULL);
        BIO_printf(out, "Public Key=");
        BN_print(out, pub_key);
        BIO_printf(out, "\n");
    }

    if (noout) {
        ret = 0;
        goto end;
    }
    BIO_printf(bio_err, "writing DSA key\n");
    if (outformat == FORMAT_ASN1) {
        if (pubin || pubout) {
            i = i2d_DSA_PUBKEY_bio(out, dsa);
        } else {
            assert(private);
            i = i2d_DSAPrivateKey_bio(out, dsa);
        }
    } else if (outformat == FORMAT_PEM) {
        if (pubin || pubout) {
            i = PEM_write_bio_DSA_PUBKEY(out, dsa);
        } else {
            assert(private);
            i = PEM_write_bio_DSAPrivateKey(out, dsa, enc,
                                            NULL, 0, NULL, passout);
        }
# ifndef OPENSSL_NO_RSA
    } else if (outformat == FORMAT_MSBLOB || outformat == FORMAT_PVK) {
        EVP_PKEY *pk;
        pk = EVP_PKEY_new();
        if (pk == NULL)
           goto end;

        EVP_PKEY_set1_DSA(pk, dsa);
        if (outformat == FORMAT_PVK) {
            if (pubin) {
                BIO_printf(bio_err, "PVK form impossible with public key input\n");
                EVP_PKEY_free(pk);
                goto end;
            }
            assert(private);
#  ifdef OPENSSL_NO_RC4
            BIO_printf(bio_err, "PVK format not supported\n");
            EVP_PKEY_free(pk);
            goto end;
#  else
            i = i2b_PVK_bio(out, pk, pvk_encr, 0, passout);
#  endif
        } else if (pubin || pubout) {
            i = i2b_PublicKey_bio(out, pk);
        } else {
            assert(private);
            i = i2b_PrivateKey_bio(out, pk);
        }
        EVP_PKEY_free(pk);
# endif
    } else {
        BIO_printf(bio_err, "bad output format specified for outfile\n");
        goto end;
    }
    if (i <= 0) {
        BIO_printf(bio_err, "unable to write private key\n");
        ERR_print_errors(bio_err);
        goto end;
    }
    ret = 0;
 end:
    BIO_free_all(out);
    DSA_free(dsa);
    release_engine(e);
    OPENSSL_free(passin);
    OPENSSL_free(passout);
    return ret;
}
#endif
