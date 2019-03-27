/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_CAST_H
# define HEADER_CAST_H

# include <openssl/opensslconf.h>

# ifndef OPENSSL_NO_CAST
# ifdef  __cplusplus
extern "C" {
# endif

# define CAST_ENCRYPT    1
# define CAST_DECRYPT    0

# define CAST_LONG unsigned int

# define CAST_BLOCK      8
# define CAST_KEY_LENGTH 16

typedef struct cast_key_st {
    CAST_LONG data[32];
    int short_key;              /* Use reduced rounds for short key */
} CAST_KEY;

void CAST_set_key(CAST_KEY *key, int len, const unsigned char *data);
void CAST_ecb_encrypt(const unsigned char *in, unsigned char *out,
                      const CAST_KEY *key, int enc);
void CAST_encrypt(CAST_LONG *data, const CAST_KEY *key);
void CAST_decrypt(CAST_LONG *data, const CAST_KEY *key);
void CAST_cbc_encrypt(const unsigned char *in, unsigned char *out,
                      long length, const CAST_KEY *ks, unsigned char *iv,
                      int enc);
void CAST_cfb64_encrypt(const unsigned char *in, unsigned char *out,
                        long length, const CAST_KEY *schedule,
                        unsigned char *ivec, int *num, int enc);
void CAST_ofb64_encrypt(const unsigned char *in, unsigned char *out,
                        long length, const CAST_KEY *schedule,
                        unsigned char *ivec, int *num);

# ifdef  __cplusplus
}
# endif
# endif

#endif
