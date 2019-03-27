/*
 * Copyright 1999-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apps.h"
#include "progs.h"
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pkcs12.h>

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_INFORM, OPT_OUTFORM, OPT_ENGINE, OPT_IN, OPT_OUT,
    OPT_TOPK8, OPT_NOITER, OPT_NOCRYPT,
#ifndef OPENSSL_NO_SCRYPT
    OPT_SCRYPT, OPT_SCRYPT_N, OPT_SCRYPT_R, OPT_SCRYPT_P,
#endif
    OPT_V2, OPT_V1, OPT_V2PRF, OPT_ITER, OPT_PASSIN, OPT_PASSOUT,
    OPT_TRADITIONAL,
    OPT_R_ENUM
} OPTION_CHOICE;

const OPTIONS pkcs8_options[] = {
    {"help", OPT_HELP, '-', "Display this summary"},
    {"inform", OPT_INFORM, 'F', "Input format (DER or PEM)"},
    {"outform", OPT_OUTFORM, 'F', "Output format (DER or PEM)"},
    {"in", OPT_IN, '<', "Input file"},
    {"out", OPT_OUT, '>', "Output file"},
    {"topk8", OPT_TOPK8, '-', "Output PKCS8 file"},
    {"noiter", OPT_NOITER, '-', "Use 1 as iteration count"},
    {"nocrypt", OPT_NOCRYPT, '-', "Use or expect unencrypted private key"},
    OPT_R_OPTIONS,
    {"v2", OPT_V2, 's', "Use PKCS#5 v2.0 and cipher"},
    {"v1", OPT_V1, 's', "Use PKCS#5 v1.5 and cipher"},
    {"v2prf", OPT_V2PRF, 's', "Set the PRF algorithm to use with PKCS#5 v2.0"},
    {"iter", OPT_ITER, 'p', "Specify the iteration count"},
    {"passin", OPT_PASSIN, 's', "Input file pass phrase source"},
    {"passout", OPT_PASSOUT, 's', "Output file pass phrase source"},
    {"traditional", OPT_TRADITIONAL, '-', "use traditional format private key"},
#ifndef OPENSSL_NO_ENGINE
    {"engine", OPT_ENGINE, 's', "Use engine, possibly a hardware device"},
#endif
#ifndef OPENSSL_NO_SCRYPT
    {"scrypt", OPT_SCRYPT, '-', "Use scrypt algorithm"},
    {"scrypt_N", OPT_SCRYPT_N, 's', "Set scrypt N parameter"},
    {"scrypt_r", OPT_SCRYPT_R, 's', "Set scrypt r parameter"},
    {"scrypt_p", OPT_SCRYPT_P, 's', "Set scrypt p parameter"},
#endif
    {NULL}
};

int pkcs8_main(int argc, char **argv)
{
    BIO *in = NULL, *out = NULL;
    ENGINE *e = NULL;
    EVP_PKEY *pkey = NULL;
    PKCS8_PRIV_KEY_INFO *p8inf = NULL;
    X509_SIG *p8 = NULL;
    const EVP_CIPHER *cipher = NULL;
    char *infile = NULL, *outfile = NULL;
    char *passinarg = NULL, *passoutarg = NULL, *prog;
#ifndef OPENSSL_NO_UI_CONSOLE
    char pass[APP_PASS_LEN];
#endif
    char *passin = NULL, *passout = NULL, *p8pass = NULL;
    OPTION_CHOICE o;
    int nocrypt = 0, ret = 1, iter = PKCS12_DEFAULT_ITER;
    int informat = FORMAT_PEM, outformat = FORMAT_PEM, topk8 = 0, pbe_nid = -1;
    int private = 0, traditional = 0;
#ifndef OPENSSL_NO_SCRYPT
    long scrypt_N = 0, scrypt_r = 0, scrypt_p = 0;
#endif

    prog = opt_init(argc, argv, pkcs8_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(pkcs8_options);
            ret = 0;
            goto end;
        case OPT_INFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER, &informat))
                goto opthelp;
            break;
        case OPT_IN:
            infile = opt_arg();
            break;
        case OPT_OUTFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER, &outformat))
                goto opthelp;
            break;
        case OPT_OUT:
            outfile = opt_arg();
            break;
        case OPT_TOPK8:
            topk8 = 1;
            break;
        case OPT_NOITER:
            iter = 1;
            break;
        case OPT_NOCRYPT:
            nocrypt = 1;
            break;
        case OPT_R_CASES:
            if (!opt_rand(o))
                goto end;
            break;
        case OPT_TRADITIONAL:
            traditional = 1;
            break;
        case OPT_V2:
            if (!opt_cipher(opt_arg(), &cipher))
                goto opthelp;
            break;
        case OPT_V1:
            pbe_nid = OBJ_txt2nid(opt_arg());
            if (pbe_nid == NID_undef) {
                BIO_printf(bio_err,
                           "%s: Unknown PBE algorithm %s\n", prog, opt_arg());
                goto opthelp;
            }
            break;
        case OPT_V2PRF:
            pbe_nid = OBJ_txt2nid(opt_arg());
            if (!EVP_PBE_find(EVP_PBE_TYPE_PRF, pbe_nid, NULL, NULL, 0)) {
                BIO_printf(bio_err,
                           "%s: Unknown PRF algorithm %s\n", prog, opt_arg());
                goto opthelp;
            }
            if (cipher == NULL)
                cipher = EVP_aes_256_cbc();
            break;
        case OPT_ITER:
            if (!opt_int(opt_arg(), &iter))
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
#ifndef OPENSSL_NO_SCRYPT
        case OPT_SCRYPT:
            scrypt_N = 16384;
            scrypt_r = 8;
            scrypt_p = 1;
            if (cipher == NULL)
                cipher = EVP_aes_256_cbc();
            break;
        case OPT_SCRYPT_N:
            if (!opt_long(opt_arg(), &scrypt_N) || scrypt_N <= 0)
                goto opthelp;
            break;
        case OPT_SCRYPT_R:
            if (!opt_long(opt_arg(), &scrypt_r) || scrypt_r <= 0)
                goto opthelp;
            break;
        case OPT_SCRYPT_P:
            if (!opt_long(opt_arg(), &scrypt_p) || scrypt_p <= 0)
                goto opthelp;
            break;
#endif
        }
    }
    argc = opt_num_rest();
    if (argc != 0)
        goto opthelp;

    private = 1;

    if (!app_passwd(passinarg, passoutarg, &passin, &passout)) {
        BIO_printf(bio_err, "Error getting passwords\n");
        goto end;
    }

    if ((pbe_nid == -1) && cipher == NULL)
        cipher = EVP_aes_256_cbc();

    in = bio_open_default(infile, 'r', informat);
    if (in == NULL)
        goto end;
    out = bio_open_owner(outfile, outformat, private);
    if (out == NULL)
        goto end;

    if (topk8) {
        pkey = load_key(infile, informat, 1, passin, e, "key");
        if (pkey == NULL)
            goto end;
        if ((p8inf = EVP_PKEY2PKCS8(pkey)) == NULL) {
            BIO_printf(bio_err, "Error converting key\n");
            ERR_print_errors(bio_err);
            goto end;
        }
        if (nocrypt) {
            assert(private);
            if (outformat == FORMAT_PEM) {
                PEM_write_bio_PKCS8_PRIV_KEY_INFO(out, p8inf);
            } else if (outformat == FORMAT_ASN1) {
                i2d_PKCS8_PRIV_KEY_INFO_bio(out, p8inf);
            } else {
                BIO_printf(bio_err, "Bad format specified for key\n");
                goto end;
            }
        } else {
            X509_ALGOR *pbe;
            if (cipher) {
#ifndef OPENSSL_NO_SCRYPT
                if (scrypt_N && scrypt_r && scrypt_p)
                    pbe = PKCS5_pbe2_set_scrypt(cipher, NULL, 0, NULL,
                                                scrypt_N, scrypt_r, scrypt_p);
                else
#endif
                    pbe = PKCS5_pbe2_set_iv(cipher, iter, NULL, 0, NULL,
                                            pbe_nid);
            } else {
                pbe = PKCS5_pbe_set(pbe_nid, iter, NULL, 0);
            }
            if (pbe == NULL) {
                BIO_printf(bio_err, "Error setting PBE algorithm\n");
                ERR_print_errors(bio_err);
                goto end;
            }
            if (passout != NULL) {
                p8pass = passout;
            } else if (1) {
                /* To avoid bit rot */
#ifndef OPENSSL_NO_UI_CONSOLE
                p8pass = pass;
                if (EVP_read_pw_string
                    (pass, sizeof(pass), "Enter Encryption Password:", 1)) {
                    X509_ALGOR_free(pbe);
                    goto end;
                }
            } else {
#endif
                BIO_printf(bio_err, "Password required\n");
                goto end;
            }
            p8 = PKCS8_set0_pbe(p8pass, strlen(p8pass), p8inf, pbe);
            if (p8 == NULL) {
                X509_ALGOR_free(pbe);
                BIO_printf(bio_err, "Error encrypting key\n");
                ERR_print_errors(bio_err);
                goto end;
            }
            assert(private);
            if (outformat == FORMAT_PEM)
                PEM_write_bio_PKCS8(out, p8);
            else if (outformat == FORMAT_ASN1)
                i2d_PKCS8_bio(out, p8);
            else {
                BIO_printf(bio_err, "Bad format specified for key\n");
                goto end;
            }
        }

        ret = 0;
        goto end;
    }

    if (nocrypt) {
        if (informat == FORMAT_PEM) {
            p8inf = PEM_read_bio_PKCS8_PRIV_KEY_INFO(in, NULL, NULL, NULL);
        } else if (informat == FORMAT_ASN1) {
            p8inf = d2i_PKCS8_PRIV_KEY_INFO_bio(in, NULL);
        } else {
            BIO_printf(bio_err, "Bad format specified for key\n");
            goto end;
        }
    } else {
        if (informat == FORMAT_PEM) {
            p8 = PEM_read_bio_PKCS8(in, NULL, NULL, NULL);
        } else if (informat == FORMAT_ASN1) {
            p8 = d2i_PKCS8_bio(in, NULL);
        } else {
            BIO_printf(bio_err, "Bad format specified for key\n");
            goto end;
        }

        if (p8 == NULL) {
            BIO_printf(bio_err, "Error reading key\n");
            ERR_print_errors(bio_err);
            goto end;
        }
        if (passin != NULL) {
            p8pass = passin;
        } else if (1) {
#ifndef OPENSSL_NO_UI_CONSOLE
            p8pass = pass;
            if (EVP_read_pw_string(pass, sizeof(pass), "Enter Password:", 0)) {
                BIO_printf(bio_err, "Can't read Password\n");
                goto end;
            }
        } else {
#endif
            BIO_printf(bio_err, "Password required\n");
            goto end;
        }
        p8inf = PKCS8_decrypt(p8, p8pass, strlen(p8pass));
    }

    if (p8inf == NULL) {
        BIO_printf(bio_err, "Error decrypting key\n");
        ERR_print_errors(bio_err);
        goto end;
    }

    if ((pkey = EVP_PKCS82PKEY(p8inf)) == NULL) {
        BIO_printf(bio_err, "Error converting key\n");
        ERR_print_errors(bio_err);
        goto end;
    }

    assert(private);
    if (outformat == FORMAT_PEM) {
        if (traditional)
            PEM_write_bio_PrivateKey_traditional(out, pkey, NULL, NULL, 0,
                                                 NULL, passout);
        else
            PEM_write_bio_PrivateKey(out, pkey, NULL, NULL, 0, NULL, passout);
    } else if (outformat == FORMAT_ASN1) {
        i2d_PrivateKey_bio(out, pkey);
    } else {
        BIO_printf(bio_err, "Bad format specified for key\n");
        goto end;
    }
    ret = 0;

 end:
    X509_SIG_free(p8);
    PKCS8_PRIV_KEY_INFO_free(p8inf);
    EVP_PKEY_free(pkey);
    release_engine(e);
    BIO_free_all(out);
    BIO_free(in);
    OPENSSL_free(passin);
    OPENSSL_free(passout);

    return ret;
}
