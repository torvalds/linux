/*
 * PKCS #8 (Private-key information syntax)
 * Copyright (c) 2006-2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef PKCS8_H
#define PKCS8_H

struct crypto_private_key * pkcs8_key_import(const u8 *buf, size_t len);
struct crypto_private_key *
pkcs8_enc_key_import(const u8 *buf, size_t len, const char *passwd);

#endif /* PKCS8_H */
