/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
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
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_INFORM, OPT_IN, OPT_OUTFORM, OPT_OUT, OPT_KEYFORM, OPT_KEY,
    OPT_ISSUER, OPT_LASTUPDATE, OPT_NEXTUPDATE, OPT_FINGERPRINT,
    OPT_CRLNUMBER, OPT_BADSIG, OPT_GENDELTA, OPT_CAPATH, OPT_CAFILE,
    OPT_NOCAPATH, OPT_NOCAFILE, OPT_VERIFY, OPT_TEXT, OPT_HASH, OPT_HASH_OLD,
    OPT_NOOUT, OPT_NAMEOPT, OPT_MD
} OPTION_CHOICE;

const OPTIONS crl_options[] = {
    {"help", OPT_HELP, '-', "Display this summary"},
    {"inform", OPT_INFORM, 'F', "Input format; default PEM"},
    {"in", OPT_IN, '<', "Input file - default stdin"},
    {"outform", OPT_OUTFORM, 'F', "Output format - default PEM"},
    {"out", OPT_OUT, '>', "output file - default stdout"},
    {"keyform", OPT_KEYFORM, 'F', "Private key file format (PEM or ENGINE)"},
    {"key", OPT_KEY, '<', "CRL signing Private key to use"},
    {"issuer", OPT_ISSUER, '-', "Print issuer DN"},
    {"lastupdate", OPT_LASTUPDATE, '-', "Set lastUpdate field"},
    {"nextupdate", OPT_NEXTUPDATE, '-', "Set nextUpdate field"},
    {"noout", OPT_NOOUT, '-', "No CRL output"},
    {"fingerprint", OPT_FINGERPRINT, '-', "Print the crl fingerprint"},
    {"crlnumber", OPT_CRLNUMBER, '-', "Print CRL number"},
    {"badsig", OPT_BADSIG, '-', "Corrupt last byte of loaded CRL signature (for test)" },
    {"gendelta", OPT_GENDELTA, '<', "Other CRL to compare/diff to the Input one"},
    {"CApath", OPT_CAPATH, '/', "Verify CRL using certificates in dir"},
    {"CAfile", OPT_CAFILE, '<', "Verify CRL using certificates in file name"},
    {"no-CAfile", OPT_NOCAFILE, '-',
     "Do not load the default certificates file"},
    {"no-CApath", OPT_NOCAPATH, '-',
     "Do not load certificates from the default certificates directory"},
    {"verify", OPT_VERIFY, '-', "Verify CRL signature"},
    {"text", OPT_TEXT, '-', "Print out a text format version"},
    {"hash", OPT_HASH, '-', "Print hash value"},
    {"nameopt", OPT_NAMEOPT, 's', "Various certificate name options"},
    {"", OPT_MD, '-', "Any supported digest"},
#ifndef OPENSSL_NO_MD5
    {"hash_old", OPT_HASH_OLD, '-', "Print old-style (MD5) hash value"},
#endif
    {NULL}
};

int crl_main(int argc, char **argv)
{
    X509_CRL *x = NULL;
    BIO *out = NULL;
    X509_STORE *store = NULL;
    X509_STORE_CTX *ctx = NULL;
    X509_LOOKUP *lookup = NULL;
    X509_OBJECT *xobj = NULL;
    EVP_PKEY *pkey;
    const EVP_MD *digest = EVP_sha1();
    char *infile = NULL, *outfile = NULL, *crldiff = NULL, *keyfile = NULL;
    const char *CAfile = NULL, *CApath = NULL, *prog;
    OPTION_CHOICE o;
    int hash = 0, issuer = 0, lastupdate = 0, nextupdate = 0, noout = 0;
    int informat = FORMAT_PEM, outformat = FORMAT_PEM, keyformat = FORMAT_PEM;
    int ret = 1, num = 0, badsig = 0, fingerprint = 0, crlnumber = 0;
    int text = 0, do_ver = 0, noCAfile = 0, noCApath = 0;
    int i;
#ifndef OPENSSL_NO_MD5
    int hash_old = 0;
#endif

    prog = opt_init(argc, argv, crl_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(crl_options);
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
        case OPT_KEYFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER, &keyformat))
                goto opthelp;
            break;
        case OPT_KEY:
            keyfile = opt_arg();
            break;
        case OPT_GENDELTA:
            crldiff = opt_arg();
            break;
        case OPT_CAPATH:
            CApath = opt_arg();
            do_ver = 1;
            break;
        case OPT_CAFILE:
            CAfile = opt_arg();
            do_ver = 1;
            break;
        case OPT_NOCAPATH:
            noCApath =  1;
            break;
        case OPT_NOCAFILE:
            noCAfile =  1;
            break;
        case OPT_HASH_OLD:
#ifndef OPENSSL_NO_MD5
            hash_old = ++num;
#endif
            break;
        case OPT_VERIFY:
            do_ver = 1;
            break;
        case OPT_TEXT:
            text = 1;
            break;
        case OPT_HASH:
            hash = ++num;
            break;
        case OPT_ISSUER:
            issuer = ++num;
            break;
        case OPT_LASTUPDATE:
            lastupdate = ++num;
            break;
        case OPT_NEXTUPDATE:
            nextupdate = ++num;
            break;
        case OPT_NOOUT:
            noout = ++num;
            break;
        case OPT_FINGERPRINT:
            fingerprint = ++num;
            break;
        case OPT_CRLNUMBER:
            crlnumber = ++num;
            break;
        case OPT_BADSIG:
            badsig = 1;
            break;
        case OPT_NAMEOPT:
            if (!set_nameopt(opt_arg()))
                goto opthelp;
            break;
        case OPT_MD:
            if (!opt_md(opt_unknown(), &digest))
                goto opthelp;
        }
    }
    argc = opt_num_rest();
    if (argc != 0)
        goto opthelp;

    x = load_crl(infile, informat);
    if (x == NULL)
        goto end;

    if (do_ver) {
        if ((store = setup_verify(CAfile, CApath, noCAfile, noCApath)) == NULL)
            goto end;
        lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file());
        if (lookup == NULL)
            goto end;
        ctx = X509_STORE_CTX_new();
        if (ctx == NULL || !X509_STORE_CTX_init(ctx, store, NULL, NULL)) {
            BIO_printf(bio_err, "Error initialising X509 store\n");
            goto end;
        }

        xobj = X509_STORE_CTX_get_obj_by_subject(ctx, X509_LU_X509,
                                                 X509_CRL_get_issuer(x));
        if (xobj == NULL) {
            BIO_printf(bio_err, "Error getting CRL issuer certificate\n");
            goto end;
        }
        pkey = X509_get_pubkey(X509_OBJECT_get0_X509(xobj));
        X509_OBJECT_free(xobj);
        if (!pkey) {
            BIO_printf(bio_err, "Error getting CRL issuer public key\n");
            goto end;
        }
        i = X509_CRL_verify(x, pkey);
        EVP_PKEY_free(pkey);
        if (i < 0)
            goto end;
        if (i == 0)
            BIO_printf(bio_err, "verify failure\n");
        else
            BIO_printf(bio_err, "verify OK\n");
    }

    if (crldiff) {
        X509_CRL *newcrl, *delta;
        if (!keyfile) {
            BIO_puts(bio_err, "Missing CRL signing key\n");
            goto end;
        }
        newcrl = load_crl(crldiff, informat);
        if (!newcrl)
            goto end;
        pkey = load_key(keyfile, keyformat, 0, NULL, NULL, "CRL signing key");
        if (!pkey) {
            X509_CRL_free(newcrl);
            goto end;
        }
        delta = X509_CRL_diff(x, newcrl, pkey, digest, 0);
        X509_CRL_free(newcrl);
        EVP_PKEY_free(pkey);
        if (delta) {
            X509_CRL_free(x);
            x = delta;
        } else {
            BIO_puts(bio_err, "Error creating delta CRL\n");
            goto end;
        }
    }

    if (badsig) {
        const ASN1_BIT_STRING *sig;

        X509_CRL_get0_signature(x, &sig, NULL);
        corrupt_signature(sig);
    }

    if (num) {
        for (i = 1; i <= num; i++) {
            if (issuer == i) {
                print_name(bio_out, "issuer=", X509_CRL_get_issuer(x),
                           get_nameopt());
            }
            if (crlnumber == i) {
                ASN1_INTEGER *crlnum;
                crlnum = X509_CRL_get_ext_d2i(x, NID_crl_number, NULL, NULL);
                BIO_printf(bio_out, "crlNumber=");
                if (crlnum) {
                    i2a_ASN1_INTEGER(bio_out, crlnum);
                    ASN1_INTEGER_free(crlnum);
                } else
                    BIO_puts(bio_out, "<NONE>");
                BIO_printf(bio_out, "\n");
            }
            if (hash == i) {
                BIO_printf(bio_out, "%08lx\n",
                           X509_NAME_hash(X509_CRL_get_issuer(x)));
            }
#ifndef OPENSSL_NO_MD5
            if (hash_old == i) {
                BIO_printf(bio_out, "%08lx\n",
                           X509_NAME_hash_old(X509_CRL_get_issuer(x)));
            }
#endif
            if (lastupdate == i) {
                BIO_printf(bio_out, "lastUpdate=");
                ASN1_TIME_print(bio_out, X509_CRL_get0_lastUpdate(x));
                BIO_printf(bio_out, "\n");
            }
            if (nextupdate == i) {
                BIO_printf(bio_out, "nextUpdate=");
                if (X509_CRL_get0_nextUpdate(x))
                    ASN1_TIME_print(bio_out, X509_CRL_get0_nextUpdate(x));
                else
                    BIO_printf(bio_out, "NONE");
                BIO_printf(bio_out, "\n");
            }
            if (fingerprint == i) {
                int j;
                unsigned int n;
                unsigned char md[EVP_MAX_MD_SIZE];

                if (!X509_CRL_digest(x, digest, md, &n)) {
                    BIO_printf(bio_err, "out of memory\n");
                    goto end;
                }
                BIO_printf(bio_out, "%s Fingerprint=",
                           OBJ_nid2sn(EVP_MD_type(digest)));
                for (j = 0; j < (int)n; j++) {
                    BIO_printf(bio_out, "%02X%c", md[j], (j + 1 == (int)n)
                               ? '\n' : ':');
                }
            }
        }
    }
    out = bio_open_default(outfile, 'w', outformat);
    if (out == NULL)
        goto end;

    if (text)
        X509_CRL_print_ex(out, x, get_nameopt());

    if (noout) {
        ret = 0;
        goto end;
    }

    if (outformat == FORMAT_ASN1)
        i = (int)i2d_X509_CRL_bio(out, x);
    else
        i = PEM_write_bio_X509_CRL(out, x);
    if (!i) {
        BIO_printf(bio_err, "unable to write CRL\n");
        goto end;
    }
    ret = 0;

 end:
    if (ret != 0)
        ERR_print_errors(bio_err);
    BIO_free_all(out);
    X509_CRL_free(x);
    X509_STORE_CTX_free(ctx);
    X509_STORE_free(store);
    return ret;
}
