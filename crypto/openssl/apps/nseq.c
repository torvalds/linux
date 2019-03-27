/*
 * Copyright 1999-2018 The OpenSSL Project Authors. All Rights Reserved.
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

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_TOSEQ, OPT_IN, OPT_OUT
} OPTION_CHOICE;

const OPTIONS nseq_options[] = {
    {"help", OPT_HELP, '-', "Display this summary"},
    {"toseq", OPT_TOSEQ, '-', "Output NS Sequence file"},
    {"in", OPT_IN, '<', "Input file"},
    {"out", OPT_OUT, '>', "Output file"},
    {NULL}
};

int nseq_main(int argc, char **argv)
{
    BIO *in = NULL, *out = NULL;
    X509 *x509 = NULL;
    NETSCAPE_CERT_SEQUENCE *seq = NULL;
    OPTION_CHOICE o;
    int toseq = 0, ret = 1, i;
    char *infile = NULL, *outfile = NULL, *prog;

    prog = opt_init(argc, argv, nseq_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            ret = 0;
            opt_help(nseq_options);
            goto end;
        case OPT_TOSEQ:
            toseq = 1;
            break;
        case OPT_IN:
            infile = opt_arg();
            break;
        case OPT_OUT:
            outfile = opt_arg();
            break;
        }
    }
    argc = opt_num_rest();
    if (argc != 0)
        goto opthelp;

    in = bio_open_default(infile, 'r', FORMAT_PEM);
    if (in == NULL)
        goto end;
    out = bio_open_default(outfile, 'w', FORMAT_PEM);
    if (out == NULL)
        goto end;

    if (toseq) {
        seq = NETSCAPE_CERT_SEQUENCE_new();
        if (seq == NULL)
            goto end;
        seq->certs = sk_X509_new_null();
        if (seq->certs == NULL)
            goto end;
        while ((x509 = PEM_read_bio_X509(in, NULL, NULL, NULL)))
            sk_X509_push(seq->certs, x509);

        if (!sk_X509_num(seq->certs)) {
            BIO_printf(bio_err, "%s: Error reading certs file %s\n",
                       prog, infile);
            ERR_print_errors(bio_err);
            goto end;
        }
        PEM_write_bio_NETSCAPE_CERT_SEQUENCE(out, seq);
        ret = 0;
        goto end;
    }

    seq = PEM_read_bio_NETSCAPE_CERT_SEQUENCE(in, NULL, NULL, NULL);
    if (seq == NULL) {
        BIO_printf(bio_err, "%s: Error reading sequence file %s\n",
                   prog, infile);
        ERR_print_errors(bio_err);
        goto end;
    }

    for (i = 0; i < sk_X509_num(seq->certs); i++) {
        x509 = sk_X509_value(seq->certs, i);
        dump_cert_text(out, x509);
        PEM_write_bio_X509(out, x509);
    }
    ret = 0;
 end:
    BIO_free(in);
    BIO_free_all(out);
    NETSCAPE_CERT_SEQUENCE_free(seq);

    return ret;
}
