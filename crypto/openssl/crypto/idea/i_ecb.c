/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/idea.h>
#include "idea_lcl.h"
#include <openssl/opensslv.h>

const char *IDEA_options(void)
{
    return "idea(int)";
}

void IDEA_ecb_encrypt(const unsigned char *in, unsigned char *out,
                      IDEA_KEY_SCHEDULE *ks)
{
    unsigned long l0, l1, d[2];

    n2l(in, l0);
    d[0] = l0;
    n2l(in, l1);
    d[1] = l1;
    IDEA_encrypt(d, ks);
    l0 = d[0];
    l2n(l0, out);
    l1 = d[1];
    l2n(l1, out);
    l0 = l1 = d[0] = d[1] = 0;
}
