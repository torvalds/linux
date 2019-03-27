/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/objects.h>
#include <openssl/buffer.h>
#include "internal/bn_int.h"

/* Number of octets per line */
#define ASN1_BUF_PRINT_WIDTH    15
/* Maximum indent */
#define ASN1_PRINT_MAX_INDENT 128

int ASN1_buf_print(BIO *bp, const unsigned char *buf, size_t buflen, int indent)
{
    size_t i;

    for (i = 0; i < buflen; i++) {
        if ((i % ASN1_BUF_PRINT_WIDTH) == 0) {
            if (i > 0 && BIO_puts(bp, "\n") <= 0)
                return 0;
            if (!BIO_indent(bp, indent, ASN1_PRINT_MAX_INDENT))
                return 0;
        }
        /*
         * Use colon separators for each octet for compatibility as
         * this function is used to print out key components.
         */
        if (BIO_printf(bp, "%02x%s", buf[i],
                       (i == buflen - 1) ? "" : ":") <= 0)
                return 0;
    }
    if (BIO_write(bp, "\n", 1) <= 0)
        return 0;
    return 1;
}

int ASN1_bn_print(BIO *bp, const char *number, const BIGNUM *num,
                  unsigned char *ign, int indent)
{
    int n, rv = 0;
    const char *neg;
    unsigned char *buf = NULL, *tmp = NULL;
    int buflen;

    if (num == NULL)
        return 1;
    neg = BN_is_negative(num) ? "-" : "";
    if (!BIO_indent(bp, indent, ASN1_PRINT_MAX_INDENT))
        return 0;
    if (BN_is_zero(num)) {
        if (BIO_printf(bp, "%s 0\n", number) <= 0)
            return 0;
        return 1;
    }

    if (BN_num_bytes(num) <= BN_BYTES) {
        if (BIO_printf(bp, "%s %s%lu (%s0x%lx)\n", number, neg,
                       (unsigned long)bn_get_words(num)[0], neg,
                       (unsigned long)bn_get_words(num)[0]) <= 0)
            return 0;
        return 1;
    }

    buflen = BN_num_bytes(num) + 1;
    buf = tmp = OPENSSL_malloc(buflen);
    if (buf == NULL)
        goto err;
    buf[0] = 0;
    if (BIO_printf(bp, "%s%s\n", number,
                   (neg[0] == '-') ? " (Negative)" : "") <= 0)
        goto err;
    n = BN_bn2bin(num, buf + 1);

    if (buf[1] & 0x80)
        n++;
    else
        tmp++;

    if (ASN1_buf_print(bp, tmp, n, indent + 4) == 0)
        goto err;
    rv = 1;
    err:
    OPENSSL_clear_free(buf, buflen);
    return rv;
}
