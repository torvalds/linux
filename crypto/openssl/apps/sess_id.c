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
#include <openssl/pem.h>
#include <openssl/ssl.h>

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_INFORM, OPT_OUTFORM, OPT_IN, OPT_OUT,
    OPT_TEXT, OPT_CERT, OPT_NOOUT, OPT_CONTEXT
} OPTION_CHOICE;

const OPTIONS sess_id_options[] = {
    {"help", OPT_HELP, '-', "Display this summary"},
    {"inform", OPT_INFORM, 'F', "Input format - default PEM (DER or PEM)"},
    {"outform", OPT_OUTFORM, 'f',
     "Output format - default PEM (PEM, DER or NSS)"},
    {"in", OPT_IN, 's', "Input file - default stdin"},
    {"out", OPT_OUT, '>', "Output file - default stdout"},
    {"text", OPT_TEXT, '-', "Print ssl session id details"},
    {"cert", OPT_CERT, '-', "Output certificate "},
    {"noout", OPT_NOOUT, '-', "Don't output the encoded session info"},
    {"context", OPT_CONTEXT, 's', "Set the session ID context"},
    {NULL}
};

static SSL_SESSION *load_sess_id(char *file, int format);

int sess_id_main(int argc, char **argv)
{
    SSL_SESSION *x = NULL;
    X509 *peer = NULL;
    BIO *out = NULL;
    char *infile = NULL, *outfile = NULL, *context = NULL, *prog;
    int informat = FORMAT_PEM, outformat = FORMAT_PEM;
    int cert = 0, noout = 0, text = 0, ret = 1, i, num = 0;
    OPTION_CHOICE o;

    prog = opt_init(argc, argv, sess_id_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(sess_id_options);
            ret = 0;
            goto end;
        case OPT_INFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER, &informat))
                goto opthelp;
            break;
        case OPT_OUTFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER | OPT_FMT_NSS,
                            &outformat))
                goto opthelp;
            break;
        case OPT_IN:
            infile = opt_arg();
            break;
        case OPT_OUT:
            outfile = opt_arg();
            break;
        case OPT_TEXT:
            text = ++num;
            break;
        case OPT_CERT:
            cert = ++num;
            break;
        case OPT_NOOUT:
            noout = ++num;
            break;
        case OPT_CONTEXT:
            context = opt_arg();
            break;
        }
    }
    argc = opt_num_rest();
    if (argc != 0)
        goto opthelp;

    x = load_sess_id(infile, informat);
    if (x == NULL) {
        goto end;
    }
    peer = SSL_SESSION_get0_peer(x);

    if (context != NULL) {
        size_t ctx_len = strlen(context);
        if (ctx_len > SSL_MAX_SID_CTX_LENGTH) {
            BIO_printf(bio_err, "Context too long\n");
            goto end;
        }
        if (!SSL_SESSION_set1_id_context(x, (unsigned char *)context,
                                         ctx_len)) {
            BIO_printf(bio_err, "Error setting id context\n");
            goto end;
        }
    }

    if (!noout || text) {
        out = bio_open_default(outfile, 'w', outformat);
        if (out == NULL)
            goto end;
    }

    if (text) {
        SSL_SESSION_print(out, x);

        if (cert) {
            if (peer == NULL)
                BIO_puts(out, "No certificate present\n");
            else
                X509_print(out, peer);
        }
    }

    if (!noout && !cert) {
        if (outformat == FORMAT_ASN1) {
            i = i2d_SSL_SESSION_bio(out, x);
        } else if (outformat == FORMAT_PEM) {
            i = PEM_write_bio_SSL_SESSION(out, x);
        } else if (outformat == FORMAT_NSS) {
            i = SSL_SESSION_print_keylog(out, x);
        } else {
            BIO_printf(bio_err, "bad output format specified for outfile\n");
            goto end;
        }
        if (!i) {
            BIO_printf(bio_err, "unable to write SSL_SESSION\n");
            goto end;
        }
    } else if (!noout && (peer != NULL)) { /* just print the certificate */
        if (outformat == FORMAT_ASN1) {
            i = (int)i2d_X509_bio(out, peer);
        } else if (outformat == FORMAT_PEM) {
            i = PEM_write_bio_X509(out, peer);
        } else {
            BIO_printf(bio_err, "bad output format specified for outfile\n");
            goto end;
        }
        if (!i) {
            BIO_printf(bio_err, "unable to write X509\n");
            goto end;
        }
    }
    ret = 0;
 end:
    BIO_free_all(out);
    SSL_SESSION_free(x);
    return ret;
}

static SSL_SESSION *load_sess_id(char *infile, int format)
{
    SSL_SESSION *x = NULL;
    BIO *in = NULL;

    in = bio_open_default(infile, 'r', format);
    if (in == NULL)
        goto end;
    if (format == FORMAT_ASN1)
        x = d2i_SSL_SESSION_bio(in, NULL);
    else
        x = PEM_read_bio_SSL_SESSION(in, NULL, NULL, NULL);
    if (x == NULL) {
        BIO_printf(bio_err, "unable to load SSL_SESSION\n");
        ERR_print_errors(bio_err);
        goto end;
    }

 end:
    BIO_free(in);
    return x;
}
