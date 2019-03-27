/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/cast.h>
#include "cast_lcl.h"

void CAST_encrypt(CAST_LONG *data, const CAST_KEY *key)
{
    CAST_LONG l, r, t;
    const CAST_LONG *k;

    k = &(key->data[0]);
    l = data[0];
    r = data[1];

    E_CAST(0, k, l, r, +, ^, -);
    E_CAST(1, k, r, l, ^, -, +);
    E_CAST(2, k, l, r, -, +, ^);
    E_CAST(3, k, r, l, +, ^, -);
    E_CAST(4, k, l, r, ^, -, +);
    E_CAST(5, k, r, l, -, +, ^);
    E_CAST(6, k, l, r, +, ^, -);
    E_CAST(7, k, r, l, ^, -, +);
    E_CAST(8, k, l, r, -, +, ^);
    E_CAST(9, k, r, l, +, ^, -);
    E_CAST(10, k, l, r, ^, -, +);
    E_CAST(11, k, r, l, -, +, ^);
    if (!key->short_key) {
        E_CAST(12, k, l, r, +, ^, -);
        E_CAST(13, k, r, l, ^, -, +);
        E_CAST(14, k, l, r, -, +, ^);
        E_CAST(15, k, r, l, +, ^, -);
    }

    data[1] = l & 0xffffffffL;
    data[0] = r & 0xffffffffL;
}

void CAST_decrypt(CAST_LONG *data, const CAST_KEY *key)
{
    CAST_LONG l, r, t;
    const CAST_LONG *k;

    k = &(key->data[0]);
    l = data[0];
    r = data[1];

    if (!key->short_key) {
        E_CAST(15, k, l, r, +, ^, -);
        E_CAST(14, k, r, l, -, +, ^);
        E_CAST(13, k, l, r, ^, -, +);
        E_CAST(12, k, r, l, +, ^, -);
    }
    E_CAST(11, k, l, r, -, +, ^);
    E_CAST(10, k, r, l, ^, -, +);
    E_CAST(9, k, l, r, +, ^, -);
    E_CAST(8, k, r, l, -, +, ^);
    E_CAST(7, k, l, r, ^, -, +);
    E_CAST(6, k, r, l, +, ^, -);
    E_CAST(5, k, l, r, -, +, ^);
    E_CAST(4, k, r, l, ^, -, +);
    E_CAST(3, k, l, r, +, ^, -);
    E_CAST(2, k, r, l, -, +, ^);
    E_CAST(1, k, l, r, ^, -, +);
    E_CAST(0, k, r, l, +, ^, -);

    data[1] = l & 0xffffffffL;
    data[0] = r & 0xffffffffL;
}

void CAST_cbc_encrypt(const unsigned char *in, unsigned char *out,
                      long length, const CAST_KEY *ks, unsigned char *iv,
                      int enc)
{
    register CAST_LONG tin0, tin1;
    register CAST_LONG tout0, tout1, xor0, xor1;
    register long l = length;
    CAST_LONG tin[2];

    if (enc) {
        n2l(iv, tout0);
        n2l(iv, tout1);
        iv -= 8;
        for (l -= 8; l >= 0; l -= 8) {
            n2l(in, tin0);
            n2l(in, tin1);
            tin0 ^= tout0;
            tin1 ^= tout1;
            tin[0] = tin0;
            tin[1] = tin1;
            CAST_encrypt(tin, ks);
            tout0 = tin[0];
            tout1 = tin[1];
            l2n(tout0, out);
            l2n(tout1, out);
        }
        if (l != -8) {
            n2ln(in, tin0, tin1, l + 8);
            tin0 ^= tout0;
            tin1 ^= tout1;
            tin[0] = tin0;
            tin[1] = tin1;
            CAST_encrypt(tin, ks);
            tout0 = tin[0];
            tout1 = tin[1];
            l2n(tout0, out);
            l2n(tout1, out);
        }
        l2n(tout0, iv);
        l2n(tout1, iv);
    } else {
        n2l(iv, xor0);
        n2l(iv, xor1);
        iv -= 8;
        for (l -= 8; l >= 0; l -= 8) {
            n2l(in, tin0);
            n2l(in, tin1);
            tin[0] = tin0;
            tin[1] = tin1;
            CAST_decrypt(tin, ks);
            tout0 = tin[0] ^ xor0;
            tout1 = tin[1] ^ xor1;
            l2n(tout0, out);
            l2n(tout1, out);
            xor0 = tin0;
            xor1 = tin1;
        }
        if (l != -8) {
            n2l(in, tin0);
            n2l(in, tin1);
            tin[0] = tin0;
            tin[1] = tin1;
            CAST_decrypt(tin, ks);
            tout0 = tin[0] ^ xor0;
            tout1 = tin[1] ^ xor1;
            l2nn(tout0, tout1, out, l + 8);
            xor0 = tin0;
            xor1 = tin1;
        }
        l2n(xor0, iv);
        l2n(xor1, iv);
    }
    tin0 = tin1 = tout0 = tout1 = xor0 = xor1 = 0;
    tin[0] = tin[1] = 0;
}
