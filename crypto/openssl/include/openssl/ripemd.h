/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_RIPEMD_H
# define HEADER_RIPEMD_H

# include <openssl/opensslconf.h>

#ifndef OPENSSL_NO_RMD160
# include <openssl/e_os2.h>
# include <stddef.h>
# ifdef  __cplusplus
extern "C" {
# endif

# define RIPEMD160_LONG unsigned int

# define RIPEMD160_CBLOCK        64
# define RIPEMD160_LBLOCK        (RIPEMD160_CBLOCK/4)
# define RIPEMD160_DIGEST_LENGTH 20

typedef struct RIPEMD160state_st {
    RIPEMD160_LONG A, B, C, D, E;
    RIPEMD160_LONG Nl, Nh;
    RIPEMD160_LONG data[RIPEMD160_LBLOCK];
    unsigned int num;
} RIPEMD160_CTX;

int RIPEMD160_Init(RIPEMD160_CTX *c);
int RIPEMD160_Update(RIPEMD160_CTX *c, const void *data, size_t len);
int RIPEMD160_Final(unsigned char *md, RIPEMD160_CTX *c);
unsigned char *RIPEMD160(const unsigned char *d, size_t n, unsigned char *md);
void RIPEMD160_Transform(RIPEMD160_CTX *c, const unsigned char *b);

# ifdef  __cplusplus
}
# endif
# endif


#endif
