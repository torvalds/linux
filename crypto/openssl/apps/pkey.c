/*
 * Copyright 2006-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <string.h>
#include "apps.h"
#include "progs.h"
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/evp.h>

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_INFORM, OPT_OUTFORM, OPT_PASSIN, OPT_PASSOUT, OPT_ENGINE,
    OPT_IN, OPT_OUT, OPT_PUBIN, OPT_PUBOUT, OPT_TEXT_PUB,
    OPT_TEXT, OPT_NOOUT, OPT_MD, OPT_TRADITIONAL, OPT_CHECK, OPT_PUB_CHECK
} OPTION_CHOICE;

const OPTIONS pkey_options[] = {
    {"help", OPT_HELP, '-', "Display this summary"},
    {"inform", OPT_INFORM, 'f', "Input format (DER or PEM)"},
    {"outform", OPT_OUTFORM, 'F', "Output format (DER or PEM)"},
    {"passin", OPT_PASSIN, 's', "Input file pass phrase source"},
    {"passout", OPT_PASSOUT, 's', "Output file pass phrase source"},
    {"in", OPT_IN, 's', "Input key"},
    {"out", OPT_OUT, '>', "Output file"},
    {"pubin", OPT_PUBIN, '-',
     "Read public key from input (default is private key)"},
    {"pubout", OPT_PUBOUT, '-', "Output public key, not private"},
    {"text_pub", OPT_TEXT_PUB, '-', "Only output public key components"},
    {"text", OPT_TEXT, '-', "Output in plaintext as well"},
    {"noout", OPT_NOOUT, '-', "Don't output the key"},
    {"", OPT_MD, '-', "Any supported cipher"},
    {"traditional", OPT_TRADITIONAL, '-',
     "Use traditional format for private keys"},
#ifndef OPENSSL_NO_ENGINE
    {"engine", OPT_ENGINE, 's', "Use engine, possibly a hardware device"},
#endif
    {"check", OPT_CHECK, '-', "Check key consistency"},
    {"pubcheck", OPT_PUB_CHECK, '-', "Check public key consistency"},
    {NULL}
};

int pkey_main(int argc, char **argv)
{
    BIO *in = NULL, *out = NULL;
    ENGINE *e = NULL;
    EVP_PKEY *pkey = NULL;
    const EVP_CIPHER *cipher = NULL;
    char *infile = NULL, *outfile = NULL, *passin = NULL, *passout = NULL;
    char *passinarg = NULL, *passoutarg = NULL, *prog;
    OPTION_CHOICE o;
    int informat = FORMAT_PEM, outformat = FORMAT_PEM;
    int pubin = 0, pubout = 0, pubtext = 0, text = 0, noout = 0, ret = 1;
    int private = 0, traditional = 0, check = 0, pub_check = 0;

    prog = opt_init(argc, argv, pkey_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(pkey_options);
            ret = 0;
            goto end;
        case OPT_INFORM:
            if (!opt_format(opt_arg(), OPT_FMT_ANY, &informat))
                goto opthelp;
            break;
        case OPT_OUTFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER, &outformat))
                goto opthelp;
            break;
        case OPT_PASSIN:
            passinarg = opt_arg();
            break;
        case OPT_PASSOUT:
            passoutarg = opt_arg();
            break;
        case OPT_ENGINE:
            e = setup_engine(opt_arg(), 0);
            break;
        case OPT_IN:
            infile = opt_arg();
            break;
        case OPT_OUT:
            outfile = opt_arg();
            break;
        case OPT_PUBIN:
            pubin = pubout = pubtext = 1;
            break;
        case OPT_PUBOUT:
            pubout = 1;
            break;
        case OPT_TEXT_PUB:
            pubtext = text = 1;
            break;
        case OPT_TEXT:
            text = 1;
            break;
        case OPT_NOOUT:
            noout = 1;
            break;
        case OPT_TRADITIONAL:
            traditional = 1;
            break;
        case OPT_CHECK:
            check = 1;
            break;
        case OPT_PUB_CHECK:
            pub_check = 1;
            break;
        case OPT_MD:
            if (!opt_cipher(opt_unknown(), &cipher))
                goto opthelp;
        }
    }
    argc = opt_num_rest();
    if (argc != 0)
        goto opthelp;

    private = !noout && !pubout ? 1 : 0;
    if (text && !pubtext)
        private = 1;

    if (!app_passwd(passinarg, passoutarg, &passin, &passout)) {
        BIO_printf(bio_err, "Error getting passwords\n");
        goto end;
    }

    out = bio_open_owner(outfile, outformat, private);
    if (out == NULL)
        goto end;

    if (pubin)
        pkey = load_pubkey(infile, informat, 1, passin, e, "Public Key");
    else
        pkey = load_key(infile, informat, 1, passin, e, "key");
    if (pkey == NULL)
        goto end;

    if (check || pub_check) {
        int r;
        EVP_PKEY_CTX *ctx;

        ctx = EVP_PKEY_CTX_new(pkey, e);
        if (ctx == NULL) {
            ERR_print_errors(bio_err);
            goto end;
        }

        if (check)
            r = EVP_PKEY_check(ctx);
        else
            r = EVP_PKEY_public_check(ctx);

        if (r == 1) {
            BIO_printf(out, "Key is valid\n");
        } else {
            /*
             * Note: at least for RSA keys if this function returns
             * -1, there will be no error reasons.
             */
            unsigned long err;

            BIO_printf(out, "Key is invalid\n");

            while ((err = ERR_peek_error()) != 0) {
                BIO_printf(out, "Detailed error: %s\n",
                           ERR_reason_error_string(err));
                ERR_get_error(); /* remove err from error stack */
            }
        }
        EVP_PKEY_CTX_free(ctx);
    }

    if (!noout) {
        if (outformat == FORMAT_PEM) {
            if (pubout) {
                if (!PEM_write_bio_PUBKEY(out, pkey))
                    goto end;
            } else {
                assert(private);
                if (traditional) {
                    if (!PEM_write_bio_PrivateKey_traditional(out, pkey, cipher,
                                                              NULL, 0, NULL,
                                                              passout))
                        goto end;
                } else {
                    if (!PEM_write_bio_PrivateKey(out, pkey, cipher,
                                                  NULL, 0, NULL, passout))
                        goto end;
                }
            }
        } else if (outformat == FORMAT_ASN1) {
            if (pubout) {
                if (!i2d_PUBKEY_bio(out, pkey))
                    goto end;
            } else {
                assert(private);
                if (!i2d_PrivateKey_bio(out, pkey))
                    goto end;
            }
        } else {
            BIO_printf(bio_err, "Bad format specified for key\n");
            goto end;
        }
    }

    if (text) {
        if (pubtext) {
            if (EVP_PKEY_print_public(out, pkey, 0, NULL) <= 0)
                goto end;
        } else {
            assert(private);
            if (EVP_PKEY_print_private(out, pkey, 0, NULL) <= 0)
                goto end;
        }
    }

    ret = 0;

 end:
    if (ret != 0)
        ERR_print_errors(bio_err);
    EVP_PKEY_free(pkey);
    release_engine(e);
    BIO_free_all(out);
    BIO_free(in);
    OPENSSL_free(passin);
    OPENSSL_free(passout);

    return ret;
}
