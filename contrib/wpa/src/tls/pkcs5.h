/*
 * PKCS #5 (Password-based Encryption)
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef PKCS5_H
#define PKCS5_H

u8 * pkcs5_decrypt(const u8 *enc_alg, size_t enc_alg_len,
		   const u8 *enc_data, size_t enc_data_len,
		   const char *passwd, size_t *data_len);

#endif /* PKCS5_H */
