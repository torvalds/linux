/*
 * Copyright 2004-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <string.h>

#include "apps.h"
#include "progs.h"
#include <openssl/bn.h>

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_HEX, OPT_GENERATE, OPT_BITS, OPT_SAFE, OPT_CHECKS
} OPTION_CHOICE;

const OPTIONS prime_options[] = {
    {OPT_HELP_STR, 1, '-', "Usage: %s [options] [number...]\n"},
    {OPT_HELP_STR, 1, '-',
        "  number Number to check for primality\n"},
    {"help", OPT_HELP, '-', "Display this summary"},
    {"hex", OPT_HEX, '-', "Hex output"},
    {"generate", OPT_GENERATE, '-', "Generate a prime"},
    {"bits", OPT_BITS, 'p', "Size of number in bits"},
    {"safe", OPT_SAFE, '-',
     "When used with -generate, generate a safe prime"},
    {"checks", OPT_CHECKS, 'p', "Number of checks"},
    {NULL}
};

int prime_main(int argc, char **argv)
{
    BIGNUM *bn = NULL;
    int hex = 0, checks = 20, generate = 0, bits = 0, safe = 0, ret = 1;
    char *prog;
    OPTION_CHOICE o;

    prog = opt_init(argc, argv, prime_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(prime_options);
            ret = 0;
            goto end;
        case OPT_HEX:
            hex = 1;
            break;
        case OPT_GENERATE:
            generate = 1;
            break;
        case OPT_BITS:
            bits = atoi(opt_arg());
            break;
        case OPT_SAFE:
            safe = 1;
            break;
        case OPT_CHECKS:
            checks = atoi(opt_arg());
            break;
        }
    }
    argc = opt_num_rest();
    argv = opt_rest();

    if (generate) {
        if (argc != 0) {
            BIO_printf(bio_err, "Extra arguments given.\n");
            goto opthelp;
        }
    } else if (argc == 0) {
        BIO_printf(bio_err, "%s: No prime specified\n", prog);
        goto opthelp;
    }

    if (generate) {
        char *s;

        if (!bits) {
            BIO_printf(bio_err, "Specify the number of bits.\n");
            goto end;
        }
        bn = BN_new();
        if (bn == NULL) {
            BIO_printf(bio_err, "Out of memory.\n");
            goto end;
        }
        if (!BN_generate_prime_ex(bn, bits, safe, NULL, NULL, NULL)) {
            BIO_printf(bio_err, "Failed to generate prime.\n");
            goto end;
        }
        s = hex ? BN_bn2hex(bn) : BN_bn2dec(bn);
        if (s == NULL) {
            BIO_printf(bio_err, "Out of memory.\n");
            goto end;
        }
        BIO_printf(bio_out, "%s\n", s);
        OPENSSL_free(s);
    } else {
        for ( ; *argv; argv++) {
            int r;

            if (hex)
                r = BN_hex2bn(&bn, argv[0]);
            else
                r = BN_dec2bn(&bn, argv[0]);

            if (!r) {
                BIO_printf(bio_err, "Failed to process value (%s)\n", argv[0]);
                goto end;
            }

            BN_print(bio_out, bn);
            BIO_printf(bio_out, " (%s) %s prime\n",
                       argv[0],
                       BN_is_prime_ex(bn, checks, NULL, NULL)
                           ? "is" : "is not");
        }
    }

    ret = 0;
 end:
    BN_free(bn);
    return ret;
}
