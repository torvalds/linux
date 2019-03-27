/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_RC2_H
# define HEADER_RC2_H

# include <openssl/opensslconf.h>

# ifndef OPENSSL_NO_RC2
# ifdef  __cplusplus
extern "C" {
# endif

typedef unsigned int RC2_INT;

# define RC2_ENCRYPT     1
# define RC2_DECRYPT     0

# define RC2_BLOCK       8
# define RC2_KEY_LENGTH  16

typedef struct rc2_key_st {
    RC2_INT data[64];
} RC2_KEY;

void RC2_set_key(RC2_KEY *key, int len, const unsigned char *data, int bits);
void RC2_ecb_encrypt(const unsigned char *in, unsigned char *out,
                     RC2_KEY *key, int enc);
void RC2_encrypt(unsigned long *data, RC2_KEY *key);
void RC2_decrypt(unsigned long *data, RC2_KEY *key);
void RC2_cbc_encrypt(const unsigned char *in, unsigned char *out, long length,
                     RC2_KEY *ks, unsigned char *iv, int enc);
void RC2_cfb64_encrypt(const unsigned char *in, unsigned char *out,
                       long length, RC2_KEY *schedule, unsigned char *ivec,
                       int *num, int enc);
void RC2_ofb64_encrypt(const unsigned char *in, unsigned char *out,
                       long length, RC2_KEY *schedule, unsigned char *ivec,
                       int *num);

# ifdef  __cplusplus
}
# endif
# endif

#endif
