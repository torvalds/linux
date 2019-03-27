/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * Stolen from tjh's ssl/ssl_trc.c stuff.
 */

#include <stdio.h>
#include "bio_lcl.h"

#define DUMP_WIDTH      16
#define DUMP_WIDTH_LESS_INDENT(i) (DUMP_WIDTH - ((i - (i > 6 ? 6 : i) + 3) / 4))

#define SPACE(buf, pos, n)   (sizeof(buf) - (pos) > (n))

int BIO_dump_cb(int (*cb) (const void *data, size_t len, void *u),
                void *u, const char *s, int len)
{
    return BIO_dump_indent_cb(cb, u, s, len, 0);
}

int BIO_dump_indent_cb(int (*cb) (const void *data, size_t len, void *u),
                       void *u, const char *s, int len, int indent)
{
    int ret = 0;
    char buf[288 + 1];
    int i, j, rows, n;
    unsigned char ch;
    int dump_width;

    if (indent < 0)
        indent = 0;
    else if (indent > 128)
        indent = 128;

    dump_width = DUMP_WIDTH_LESS_INDENT(indent);
    rows = len / dump_width;
    if ((rows * dump_width) < len)
        rows++;
    for (i = 0; i < rows; i++) {
        n = BIO_snprintf(buf, sizeof(buf), "%*s%04x - ", indent, "",
                         i * dump_width);
        for (j = 0; j < dump_width; j++) {
            if (SPACE(buf, n, 3)) {
                if (((i * dump_width) + j) >= len) {
                    strcpy(buf + n, "   ");
                } else {
                    ch = ((unsigned char)*(s + i * dump_width + j)) & 0xff;
                    BIO_snprintf(buf + n, 4, "%02x%c", ch,
                                 j == 7 ? '-' : ' ');
                }
                n += 3;
            }
        }
        if (SPACE(buf, n, 2)) {
            strcpy(buf + n, "  ");
            n += 2;
        }
        for (j = 0; j < dump_width; j++) {
            if (((i * dump_width) + j) >= len)
                break;
            if (SPACE(buf, n, 1)) {
                ch = ((unsigned char)*(s + i * dump_width + j)) & 0xff;
#ifndef CHARSET_EBCDIC
                buf[n++] = ((ch >= ' ') && (ch <= '~')) ? ch : '.';
#else
                buf[n++] = ((ch >= os_toascii[' ']) && (ch <= os_toascii['~']))
                           ? os_toebcdic[ch]
                           : '.';
#endif
                buf[n] = '\0';
            }
        }
        if (SPACE(buf, n, 1)) {
            buf[n++] = '\n';
            buf[n] = '\0';
        }
        /*
         * if this is the last call then update the ddt_dump thing so that we
         * will move the selection point in the debug window
         */
        ret += cb((void *)buf, n, u);
    }
    return ret;
}

#ifndef OPENSSL_NO_STDIO
static int write_fp(const void *data, size_t len, void *fp)
{
    return UP_fwrite(data, len, 1, fp);
}

int BIO_dump_fp(FILE *fp, const char *s, int len)
{
    return BIO_dump_cb(write_fp, fp, s, len);
}

int BIO_dump_indent_fp(FILE *fp, const char *s, int len, int indent)
{
    return BIO_dump_indent_cb(write_fp, fp, s, len, indent);
}
#endif

static int write_bio(const void *data, size_t len, void *bp)
{
    return BIO_write((BIO *)bp, (const char *)data, len);
}

int BIO_dump(BIO *bp, const char *s, int len)
{
    return BIO_dump_cb(write_bio, bp, s, len);
}

int BIO_dump_indent(BIO *bp, const char *s, int len, int indent)
{
    return BIO_dump_indent_cb(write_bio, bp, s, len, indent);
}

int BIO_hex_string(BIO *out, int indent, int width, unsigned char *data,
                   int datalen)
{
    int i, j = 0;

    if (datalen < 1)
        return 1;

    for (i = 0; i < datalen - 1; i++) {
        if (i && !j)
            BIO_printf(out, "%*s", indent, "");

        BIO_printf(out, "%02X:", data[i]);

        j = (j + 1) % width;
        if (!j)
            BIO_printf(out, "\n");
    }

    if (i && !j)
        BIO_printf(out, "%*s", indent, "");
    BIO_printf(out, "%02X", data[datalen - 1]);
    return 1;
}
