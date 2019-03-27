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
#include <openssl/ssl.h>

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP
} OPTION_CHOICE;

const OPTIONS errstr_options[] = {
    {OPT_HELP_STR, 1, '-', "Usage: %s [options] errnum...\n"},
    {OPT_HELP_STR, 1, '-', "  errnum  Error number\n"},
    {"help", OPT_HELP, '-', "Display this summary"},
    {NULL}
};

int errstr_main(int argc, char **argv)
{
    OPTION_CHOICE o;
    char buf[256], *prog;
    int ret = 1;
    unsigned long l;

    prog = opt_init(argc, argv, errstr_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(errstr_options);
            ret = 0;
            goto end;
        }
    }

    ret = 0;
    for (argv = opt_rest(); *argv; argv++) {
        if (sscanf(*argv, "%lx", &l) == 0) {
            ret++;
        } else {
            /* We're not really an SSL application so this won't auto-init, but
             * we're still interested in SSL error strings
             */
            OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS
                             | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
            ERR_error_string_n(l, buf, sizeof(buf));
            BIO_printf(bio_out, "%s\n", buf);
        }
    }
 end:
    return ret;
}
