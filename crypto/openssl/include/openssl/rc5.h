/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_RC5_H
# define HEADER_RC5_H

# include <openssl/opensslconf.h>

# ifndef OPENSSL_NO_RC5
# ifdef  __cplusplus
extern "C" {
# endif

# define RC5_ENCRYPT     1
# define RC5_DECRYPT     0

# define RC5_32_INT unsigned int

# define RC5_32_BLOCK            8
# define RC5_32_KEY_LENGTH       16/* This is a default, max is 255 */

/*
 * This are the only values supported.  Tweak the code if you want more The
 * most supported modes will be RC5-32/12/16 RC5-32/16/8
 */
# define RC5_8_ROUNDS    8
# define RC5_12_ROUNDS   12
# define RC5_16_ROUNDS   16

typedef struct rc5_key_st {
    /* Number of rounds */
    int rounds;
    RC5_32_INT data[2 * (RC5_16_ROUNDS + 1)];
} RC5_32_KEY;

void RC5_32_set_key(RC5_32_KEY *key, int len, const unsigned char *data,
                    int rounds);
void RC5_32_ecb_encrypt(const unsigned char *in, unsigned char *out,
                        RC5_32_KEY *key, int enc);
void RC5_32_encrypt(unsigned long *data, RC5_32_KEY *key);
void RC5_32_decrypt(unsigned long *data, RC5_32_KEY *key);
void RC5_32_cbc_encrypt(const unsigned char *in, unsigned char *out,
                        long length, RC5_32_KEY *ks, unsigned char *iv,
                        int enc);
void RC5_32_cfb64_encrypt(const unsigned char *in, unsigned char *out,
                          long length, RC5_32_KEY *schedule,
                          unsigned char *ivec, int *num, int enc);
void RC5_32_ofb64_encrypt(const unsigned char *in, unsigned char *out,
                          long length, RC5_32_KEY *schedule,
                          unsigned char *ivec, int *num);

# ifdef  __cplusplus
}
# endif
# endif

#endif
