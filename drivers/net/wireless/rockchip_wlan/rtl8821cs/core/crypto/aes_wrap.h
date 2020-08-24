/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AES-based functions
 *
 * - AES Key Wrap Algorithm (RFC3394)
 * - One-Key CBC MAC (OMAC1) hash with AES-128 and AES-256
 * - AES-128/192/256 CTR mode encryption
 * - AES-128 EAX mode encryption/decryption
 * - AES-128 CBC
 * - AES-GCM
 * - AES-CCM
 *
 * Copyright (c) 2003-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef AES_WRAP_H
#define AES_WRAP_H

int __must_check aes_wrap(const u8 *kek, size_t kek_len, int n, const u8 *plain,
			  u8 *cipher);
int __must_check aes_unwrap(const u8 *kek, size_t kek_len, int n,
			    const u8 *cipher, u8 *plain);
int __must_check omac1_aes_vector(const u8 *key, size_t key_len,
				  size_t num_elem, const u8 *addr[],
				  const size_t *len, u8 *mac);
int __must_check omac1_aes_128_vector(const u8 *key, size_t num_elem,
				      const u8 *addr[], const size_t *len,
				      u8 *mac);
int __must_check omac1_aes_128(const u8 *key, const u8 *data, size_t data_len,
			       u8 *mac);
int __must_check omac1_aes_256(const u8 *key, const u8 *data, size_t data_len,
			       u8 *mac);
int __must_check aes_128_encrypt_block(const u8 *key, const u8 *in, u8 *out);
int __must_check aes_ctr_encrypt(const u8 *key, size_t key_len, const u8 *nonce,
				 u8 *data, size_t data_len);
int __must_check aes_128_ctr_encrypt(const u8 *key, const u8 *nonce,
				     u8 *data, size_t data_len);
int __must_check aes_128_eax_encrypt(const u8 *key,
				     const u8 *nonce, size_t nonce_len,
				     const u8 *hdr, size_t hdr_len,
				     u8 *data, size_t data_len, u8 *tag);
int __must_check aes_128_eax_decrypt(const u8 *key,
				     const u8 *nonce, size_t nonce_len,
				     const u8 *hdr, size_t hdr_len,
				     u8 *data, size_t data_len, const u8 *tag);
int __must_check aes_128_cbc_encrypt(const u8 *key, const u8 *iv, u8 *data,
				     size_t data_len);
int __must_check aes_128_cbc_decrypt(const u8 *key, const u8 *iv, u8 *data,
				     size_t data_len);
int __must_check aes_gcm_ae(const u8 *key, size_t key_len,
			    const u8 *iv, size_t iv_len,
			    const u8 *plain, size_t plain_len,
			    const u8 *aad, size_t aad_len,
			    u8 *crypt, u8 *tag);
int __must_check aes_gcm_ad(const u8 *key, size_t key_len,
			    const u8 *iv, size_t iv_len,
			    const u8 *crypt, size_t crypt_len,
			    const u8 *aad, size_t aad_len, const u8 *tag,
			    u8 *plain);
int __must_check aes_gmac(const u8 *key, size_t key_len,
			  const u8 *iv, size_t iv_len,
			  const u8 *aad, size_t aad_len, u8 *tag);
int __must_check aes_ccm_ae(const u8 *key, size_t key_len, const u8 *nonce,
			    size_t M, const u8 *plain, size_t plain_len,
			    const u8 *aad, size_t aad_len, u8 *crypt, u8 *auth);
int __must_check aes_ccm_ad(const u8 *key, size_t key_len, const u8 *nonce,
			    size_t M, const u8 *crypt, size_t crypt_len,
			    const u8 *aad, size_t aad_len, const u8 *auth,
			    u8 *plain);

#endif /* AES_WRAP_H */
