/*
 * SHA384 hash implementation and interface functions
 * Copyright (c) 2015-2017, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef SHA384_H
#define SHA384_H

#define SHA384_MAC_LEN 48

int hmac_sha384_vector(const u8 *key, size_t key_len, size_t num_elem,
		       const u8 *addr[], const size_t *len, u8 *mac);
int hmac_sha384(const u8 *key, size_t key_len, const u8 *data,
		size_t data_len, u8 *mac);
int sha384_prf(const u8 *key, size_t key_len, const char *label,
	       const u8 *data, size_t data_len, u8 *buf, size_t buf_len);
int sha384_prf_bits(const u8 *key, size_t key_len, const char *label,
		    const u8 *data, size_t data_len, u8 *buf,
		    size_t buf_len_bits);
int hmac_sha384_kdf(const u8 *secret, size_t secret_len,
		    const char *label, const u8 *seed, size_t seed_len,
		    u8 *out, size_t outlen);

#endif /* SHA384_H */
