/*
 * Copyright 2017-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */
#ifndef HEADER_CURVE448_LCL_H
# define HEADER_CURVE448_LCL_H
# include "curve448utils.h"

int X448(uint8_t out_shared_key[56], const uint8_t private_key[56],
         const uint8_t peer_public_value[56]);

void X448_public_from_private(uint8_t out_public_value[56],
                              const uint8_t private_key[56]);

int ED448_sign(uint8_t *out_sig, const uint8_t *message, size_t message_len,
               const uint8_t public_key[57], const uint8_t private_key[57],
               const uint8_t *context, size_t context_len);

int ED448_verify(const uint8_t *message, size_t message_len,
                 const uint8_t signature[114], const uint8_t public_key[57],
                 const uint8_t *context, size_t context_len);

int ED448ph_sign(uint8_t *out_sig, const uint8_t hash[64],
                 const uint8_t public_key[57], const uint8_t private_key[57],
                 const uint8_t *context, size_t context_len);

int ED448ph_verify(const uint8_t hash[64], const uint8_t signature[114],
                   const uint8_t public_key[57], const uint8_t *context,
                   size_t context_len);

int ED448_public_from_private(uint8_t out_public_key[57],
                              const uint8_t private_key[57]);

#endif              /* HEADER_CURVE448_LCL_H */
