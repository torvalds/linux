/*
 * Copyright 2015-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stddef.h>

#define POLY1305_BLOCK_SIZE  16
#define POLY1305_DIGEST_SIZE 16
#define POLY1305_KEY_SIZE    32

typedef struct poly1305_context POLY1305;

size_t Poly1305_ctx_size(void);
void Poly1305_Init(POLY1305 *ctx, const unsigned char key[32]);
void Poly1305_Update(POLY1305 *ctx, const unsigned char *inp, size_t len);
void Poly1305_Final(POLY1305 *ctx, unsigned char mac[16]);
