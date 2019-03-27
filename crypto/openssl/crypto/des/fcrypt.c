/*
 * Copyright 1998-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* NOCW */
#include <stdio.h>
#ifdef _OSD_POSIX
# ifndef CHARSET_EBCDIC
#  define CHARSET_EBCDIC 1
# endif
#endif
#ifdef CHARSET_EBCDIC
# include <openssl/ebcdic.h>
#endif

#include <openssl/crypto.h>
#include "des_locl.h"

/*
 * Added more values to handle illegal salt values the way normal crypt()
 * implementations do.
 */
static unsigned const char con_salt[128] = {
    0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9,
    0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
    0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1,
    0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9,
    0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF, 0x00, 0x01,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0A, 0x0B, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
    0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A,
    0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22,
    0x23, 0x24, 0x25, 0x20, 0x21, 0x22, 0x23, 0x24,
    0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C,
    0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34,
    0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C,
    0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44,
};

static unsigned const char cov_2char[64] = {
    0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
    0x36, 0x37, 0x38, 0x39, 0x41, 0x42, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C,
    0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54,
    0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x61, 0x62,
    0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A,
    0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72,
    0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A
};

char *DES_crypt(const char *buf, const char *salt)
{
    static char buff[14];

#ifndef CHARSET_EBCDIC
    return DES_fcrypt(buf, salt, buff);
#else
    char e_salt[2 + 1];
    char e_buf[32 + 1];         /* replace 32 by 8 ? */
    char *ret;

    if (salt[0] == '\0' || salt[1] == '\0')
        return NULL;

    /* Copy salt, convert to ASCII. */
    e_salt[0] = salt[0];
    e_salt[1] = salt[1];
    e_salt[2] = '\0';
    ebcdic2ascii(e_salt, e_salt, sizeof(e_salt));

    /* Convert password to ASCII. */
    OPENSSL_strlcpy(e_buf, buf, sizeof(e_buf));
    ebcdic2ascii(e_buf, e_buf, sizeof(e_buf));

    /* Encrypt it (from/to ASCII); if it worked, convert back. */
    ret = DES_fcrypt(e_buf, e_salt, buff);
    if (ret != NULL)
        ascii2ebcdic(ret, ret, strlen(ret));

    return ret;
#endif
}

char *DES_fcrypt(const char *buf, const char *salt, char *ret)
{
    unsigned int i, j, x, y;
    DES_LONG Eswap0, Eswap1;
    DES_LONG out[2], ll;
    DES_cblock key;
    DES_key_schedule ks;
    unsigned char bb[9];
    unsigned char *b = bb;
    unsigned char c, u;

    x = ret[0] = salt[0];
    if (x == 0 || x >= sizeof(con_salt))
        return NULL;
    Eswap0 = con_salt[x] << 2;
    x = ret[1] = salt[1];
    if (x == 0 || x >= sizeof(con_salt))
        return NULL;
    Eswap1 = con_salt[x] << 6;

    /*
     * EAY r=strlen(buf); r=(r+7)/8;
     */
    for (i = 0; i < 8; i++) {
        c = *(buf++);
        if (!c)
            break;
        key[i] = (c << 1);
    }
    for (; i < 8; i++)
        key[i] = 0;

    DES_set_key_unchecked(&key, &ks);
    fcrypt_body(&(out[0]), &ks, Eswap0, Eswap1);

    ll = out[0];
    l2c(ll, b);
    ll = out[1];
    l2c(ll, b);
    y = 0;
    u = 0x80;
    bb[8] = 0;
    for (i = 2; i < 13; i++) {
        c = 0;
        for (j = 0; j < 6; j++) {
            c <<= 1;
            if (bb[y] & u)
                c |= 1;
            u >>= 1;
            if (!u) {
                y++;
                u = 0x80;
            }
        }
        ret[i] = cov_2char[c];
    }
    ret[13] = '\0';
    return ret;
}
