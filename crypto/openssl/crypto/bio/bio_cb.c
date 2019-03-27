/*
 * Copyright 1995-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bio_lcl.h"
#include "internal/cryptlib.h"
#include <openssl/err.h>

long BIO_debug_callback(BIO *bio, int cmd, const char *argp,
                        int argi, long argl, long ret)
{
    BIO *b;
    char buf[256];
    char *p;
    long r = 1;
    int len, left;

    if (BIO_CB_RETURN & cmd)
        r = ret;

    len = BIO_snprintf(buf, sizeof(buf), "BIO[%p]: ", (void *)bio);

    /* Ignore errors and continue printing the other information. */
    if (len < 0)
        len = 0;
    p = buf + len;
    left = sizeof(buf) - len;

    switch (cmd) {
    case BIO_CB_FREE:
        BIO_snprintf(p, left, "Free - %s\n", bio->method->name);
        break;
    case BIO_CB_READ:
        if (bio->method->type & BIO_TYPE_DESCRIPTOR)
            BIO_snprintf(p, left, "read(%d,%lu) - %s fd=%d\n",
                         bio->num, (unsigned long)argi,
                         bio->method->name, bio->num);
        else
            BIO_snprintf(p, left, "read(%d,%lu) - %s\n",
                    bio->num, (unsigned long)argi, bio->method->name);
        break;
    case BIO_CB_WRITE:
        if (bio->method->type & BIO_TYPE_DESCRIPTOR)
            BIO_snprintf(p, left, "write(%d,%lu) - %s fd=%d\n",
                         bio->num, (unsigned long)argi,
                         bio->method->name, bio->num);
        else
            BIO_snprintf(p, left, "write(%d,%lu) - %s\n",
                         bio->num, (unsigned long)argi, bio->method->name);
        break;
    case BIO_CB_PUTS:
        BIO_snprintf(p, left, "puts() - %s\n", bio->method->name);
        break;
    case BIO_CB_GETS:
        BIO_snprintf(p, left, "gets(%lu) - %s\n", (unsigned long)argi,
                     bio->method->name);
        break;
    case BIO_CB_CTRL:
        BIO_snprintf(p, left, "ctrl(%lu) - %s\n", (unsigned long)argi,
                     bio->method->name);
        break;
    case BIO_CB_RETURN | BIO_CB_READ:
        BIO_snprintf(p, left, "read return %ld\n", ret);
        break;
    case BIO_CB_RETURN | BIO_CB_WRITE:
        BIO_snprintf(p, left, "write return %ld\n", ret);
        break;
    case BIO_CB_RETURN | BIO_CB_GETS:
        BIO_snprintf(p, left, "gets return %ld\n", ret);
        break;
    case BIO_CB_RETURN | BIO_CB_PUTS:
        BIO_snprintf(p, left, "puts return %ld\n", ret);
        break;
    case BIO_CB_RETURN | BIO_CB_CTRL:
        BIO_snprintf(p, left, "ctrl return %ld\n", ret);
        break;
    default:
        BIO_snprintf(p, left, "bio callback - unknown type (%d)\n", cmd);
        break;
    }

    b = (BIO *)bio->cb_arg;
    if (b != NULL)
        BIO_write(b, buf, strlen(buf));
#if !defined(OPENSSL_NO_STDIO)
    else
        fputs(buf, stderr);
#endif
    return r;
}
