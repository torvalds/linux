/*
 * WPA Supplicant / wrapper functions for crypto libraries
 * Copyright (c) 2004-2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * This file defines the cryptographic functions that need to be implemented
 * for wpa_supplicant and hostapd. When TLS is not used, internal
 * implementation of MD5, SHA1, and AES is used and no external libraries are
 * required. When TLS is enabled (e.g., by enabling EAP-TLS or EAP-PEAP), the
 * crypto library used by the TLS implementation is expected to be used for
 * non-TLS needs, too, in order to save space by not implementing these
 * functions twice.
 *
 * Wrapper code for using each crypto library is in its own file (crypto*.c)
 * and one of these files is build and linked in to provide the functions
 * defined here.
 */

#ifndef CRYPTO_H
#define CRYPTO_H

/**
 * md4_vector - MD4 hash for data vector
 * @num_elem: Number of elements in the data vector
 * @addr: Pointers to the data areas
 * @len: Lengths of the data blocks
 * @mac: Buffer for the hash
 * Returns: 0 on success, -1 on failure
 */
int md4_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac);

/**
 * md5_vector - MD5 hash for data vector
 * @num_elem: Number of elements in the data vector
 * @addr: Pointers to the data areas
 * @len: Lengths of the data blocks
 * @mac: Buffer for the hash
 * Returns: 0 on success, -1 on failure
 */
int md5_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac);

#ifdef CONFIG_FIPS
/**
 * md5_vector_non_fips_allow - MD5 hash for data vector (non-FIPS use allowed)
 * @num_elem: Number of elements in the data vector
 * @addr: Pointers to the data areas
 * @len: Lengths of the data blocks
 * @mac: Buffer for the hash
 * Returns: 0 on success, -1 on failure
 */
int md5_vector_non_fips_allow(size_t num_elem, const u8 *addr[],
			      const size_t *len, u8 *mac);
#else /* CONFIG_FIPS */
#define md5_vector_non_fips_allow md5_vector
#endif /* CONFIG_FIPS */


/**
 * sha1_vector - SHA-1 hash for data vector
 * @num_elem: Number of elements in the data vector
 * @addr: Pointers to the data areas
 * @len: Lengths of the data blocks
 * @mac: Buffer for the hash
 * Returns: 0 on success, -1 on failure
 */
int sha1_vector(size_t num_elem, const u8 *addr[], const size_t *len,
		u8 *mac);

/**
 * fips186_2-prf - NIST FIPS Publication 186-2 change notice 1 PRF
 * @seed: Seed/key for the PRF
 * @seed_len: Seed length in bytes
 * @x: Buffer for PRF output
 * @xlen: Output length in bytes
 * Returns: 0 on success, -1 on failure
 *
 * This function implements random number generation specified in NIST FIPS
 * Publication 186-2 for EAP-SIM. This PRF uses a function that is similar to
 * SHA-1, but has different message padding.
 */
int __must_check fips186_2_prf(const u8 *seed, size_t seed_len, u8 *x,
			       size_t xlen);

/**
 * sha256_vector - SHA256 hash for data vector
 * @num_elem: Number of elements in the data vector
 * @addr: Pointers to the data areas
 * @len: Lengths of the data blocks
 * @mac: Buffer for the hash
 * Returns: 0 on success, -1 on failure
 */
int sha256_vector(size_t num_elem, const u8 *addr[], const size_t *len,
		  u8 *mac);

/**
 * des_encrypt - Encrypt one block with DES
 * @clear: 8 octets (in)
 * @key: 7 octets (in) (no parity bits included)
 * @cypher: 8 octets (out)
 */
void des_encrypt(const u8 *clear, const u8 *key, u8 *cypher);

/**
 * aes_encrypt_init - Initialize AES for encryption
 * @key: Encryption key
 * @len: Key length in bytes (usually 16, i.e., 128 bits)
 * Returns: Pointer to context data or %NULL on failure
 */
void * aes_encrypt_init(const u8 *key, size_t len);

/**
 * aes_encrypt - Encrypt one AES block
 * @ctx: Context pointer from aes_encrypt_init()
 * @plain: Plaintext data to be encrypted (16 bytes)
 * @crypt: Buffer for the encrypted data (16 bytes)
 */
void aes_encrypt(void *ctx, const u8 *plain, u8 *crypt);

/**
 * aes_encrypt_deinit - Deinitialize AES encryption
 * @ctx: Context pointer from aes_encrypt_init()
 */
void aes_encrypt_deinit(void *ctx);

/**
 * aes_decrypt_init - Initialize AES for decryption
 * @key: Decryption key
 * @len: Key length in bytes (usually 16, i.e., 128 bits)
 * Returns: Pointer to context data or %NULL on failure
 */
void * aes_decrypt_init(const u8 *key, size_t len);

/**
 * aes_decrypt - Decrypt one AES block
 * @ctx: Context pointer from aes_encrypt_init()
 * @crypt: Encrypted data (16 bytes)
 * @plain: Buffer for the decrypted data (16 bytes)
 */
void aes_decrypt(void *ctx, const u8 *crypt, u8 *plain);

/**
 * aes_decrypt_deinit - Deinitialize AES decryption
 * @ctx: Context pointer from aes_encrypt_init()
 */
void aes_decrypt_deinit(void *ctx);


enum crypto_hash_alg {
	CRYPTO_HASH_ALG_MD5, CRYPTO_HASH_ALG_SHA1,
	CRYPTO_HASH_ALG_HMAC_MD5, CRYPTO_HASH_ALG_HMAC_SHA1
};

struct crypto_hash;

/**
 * crypto_hash_init - Initialize hash/HMAC function
 * @alg: Hash algorithm
 * @key: Key for keyed hash (e.g., HMAC) or %NULL if not needed
 * @key_len: Length of the key in bytes
 * Returns: Pointer to hash context to use with other hash functions or %NULL
 * on failure
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
struct crypto_hash * crypto_hash_init(enum crypto_hash_alg alg, const u8 *key,
				      size_t key_len);

/**
 * crypto_hash_update - Add data to hash calculation
 * @ctx: Context pointer from crypto_hash_init()
 * @data: Data buffer to add
 * @len: Length of the buffer
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
void crypto_hash_update(struct crypto_hash *ctx, const u8 *data, size_t len);

/**
 * crypto_hash_finish - Complete hash calculation
 * @ctx: Context pointer from crypto_hash_init()
 * @hash: Buffer for hash value or %NULL if caller is just freeing the hash
 * context
 * @len: Pointer to length of the buffer or %NULL if caller is just freeing the
 * hash context; on return, this is set to the actual length of the hash value
 * Returns: 0 on success, -1 if buffer is too small (len set to needed length),
 * or -2 on other failures (including failed crypto_hash_update() operations)
 *
 * This function calculates the hash value and frees the context buffer that
 * was used for hash calculation.
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
int crypto_hash_finish(struct crypto_hash *ctx, u8 *hash, size_t *len);


enum crypto_cipher_alg {
	CRYPTO_CIPHER_NULL = 0, CRYPTO_CIPHER_ALG_AES, CRYPTO_CIPHER_ALG_3DES,
	CRYPTO_CIPHER_ALG_DES, CRYPTO_CIPHER_ALG_RC2, CRYPTO_CIPHER_ALG_RC4
};

struct crypto_cipher;

/**
 * crypto_cipher_init - Initialize block/stream cipher function
 * @alg: Cipher algorithm
 * @iv: Initialization vector for block ciphers or %NULL for stream ciphers
 * @key: Cipher key
 * @key_len: Length of key in bytes
 * Returns: Pointer to cipher context to use with other cipher functions or
 * %NULL on failure
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
struct crypto_cipher * crypto_cipher_init(enum crypto_cipher_alg alg,
					  const u8 *iv, const u8 *key,
					  size_t key_len);

/**
 * crypto_cipher_encrypt - Cipher encrypt
 * @ctx: Context pointer from crypto_cipher_init()
 * @plain: Plaintext to cipher
 * @crypt: Resulting ciphertext
 * @len: Length of the plaintext
 * Returns: 0 on success, -1 on failure
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
int __must_check crypto_cipher_encrypt(struct crypto_cipher *ctx,
				       const u8 *plain, u8 *crypt, size_t len);

/**
 * crypto_cipher_decrypt - Cipher decrypt
 * @ctx: Context pointer from crypto_cipher_init()
 * @crypt: Ciphertext to decrypt
 * @plain: Resulting plaintext
 * @len: Length of the cipher text
 * Returns: 0 on success, -1 on failure
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
int __must_check crypto_cipher_decrypt(struct crypto_cipher *ctx,
				       const u8 *crypt, u8 *plain, size_t len);

/**
 * crypto_cipher_decrypt - Free cipher context
 * @ctx: Context pointer from crypto_cipher_init()
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
void crypto_cipher_deinit(struct crypto_cipher *ctx);


struct crypto_public_key;
struct crypto_private_key;

/**
 * crypto_public_key_import - Import an RSA public key
 * @key: Key buffer (DER encoded RSA public key)
 * @len: Key buffer length in bytes
 * Returns: Pointer to the public key or %NULL on failure
 *
 * This function can just return %NULL if the crypto library supports X.509
 * parsing. In that case, crypto_public_key_from_cert() is used to import the
 * public key from a certificate.
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
struct crypto_public_key * crypto_public_key_import(const u8 *key, size_t len);

/**
 * crypto_private_key_import - Import an RSA private key
 * @key: Key buffer (DER encoded RSA private key)
 * @len: Key buffer length in bytes
 * @passwd: Key encryption password or %NULL if key is not encrypted
 * Returns: Pointer to the private key or %NULL on failure
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
struct crypto_private_key * crypto_private_key_import(const u8 *key,
						      size_t len,
						      const char *passwd);

/**
 * crypto_public_key_from_cert - Import an RSA public key from a certificate
 * @buf: DER encoded X.509 certificate
 * @len: Certificate buffer length in bytes
 * Returns: Pointer to public key or %NULL on failure
 *
 * This function can just return %NULL if the crypto library does not support
 * X.509 parsing. In that case, internal code will be used to parse the
 * certificate and public key is imported using crypto_public_key_import().
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
struct crypto_public_key * crypto_public_key_from_cert(const u8 *buf,
						       size_t len);

/**
 * crypto_public_key_encrypt_pkcs1_v15 - Public key encryption (PKCS #1 v1.5)
 * @key: Public key
 * @in: Plaintext buffer
 * @inlen: Length of plaintext buffer in bytes
 * @out: Output buffer for encrypted data
 * @outlen: Length of output buffer in bytes; set to used length on success
 * Returns: 0 on success, -1 on failure
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
int __must_check crypto_public_key_encrypt_pkcs1_v15(
	struct crypto_public_key *key, const u8 *in, size_t inlen,
	u8 *out, size_t *outlen);

/**
 * crypto_private_key_decrypt_pkcs1_v15 - Private key decryption (PKCS #1 v1.5)
 * @key: Private key
 * @in: Encrypted buffer
 * @inlen: Length of encrypted buffer in bytes
 * @out: Output buffer for encrypted data
 * @outlen: Length of output buffer in bytes; set to used length on success
 * Returns: 0 on success, -1 on failure
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
int __must_check crypto_private_key_decrypt_pkcs1_v15(
	struct crypto_private_key *key, const u8 *in, size_t inlen,
	u8 *out, size_t *outlen);

/**
 * crypto_private_key_sign_pkcs1 - Sign with private key (PKCS #1)
 * @key: Private key from crypto_private_key_import()
 * @in: Plaintext buffer
 * @inlen: Length of plaintext buffer in bytes
 * @out: Output buffer for encrypted (signed) data
 * @outlen: Length of output buffer in bytes; set to used length on success
 * Returns: 0 on success, -1 on failure
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
int __must_check crypto_private_key_sign_pkcs1(struct crypto_private_key *key,
					       const u8 *in, size_t inlen,
					       u8 *out, size_t *outlen);

/**
 * crypto_public_key_free - Free public key
 * @key: Public key
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
void crypto_public_key_free(struct crypto_public_key *key);

/**
 * crypto_private_key_free - Free private key
 * @key: Private key from crypto_private_key_import()
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
void crypto_private_key_free(struct crypto_private_key *key);

/**
 * crypto_public_key_decrypt_pkcs1 - Decrypt PKCS #1 signature
 * @key: Public key
 * @crypt: Encrypted signature data (using the private key)
 * @crypt_len: Encrypted signature data length
 * @plain: Buffer for plaintext (at least crypt_len bytes)
 * @plain_len: Plaintext length (max buffer size on input, real len on output);
 * Returns: 0 on success, -1 on failure
 */
int __must_check crypto_public_key_decrypt_pkcs1(
	struct crypto_public_key *key, const u8 *crypt, size_t crypt_len,
	u8 *plain, size_t *plain_len);

/**
 * crypto_global_init - Initialize crypto wrapper
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
int __must_check crypto_global_init(void);

/**
 * crypto_global_deinit - Deinitialize crypto wrapper
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
void crypto_global_deinit(void);

/**
 * crypto_mod_exp - Modular exponentiation of large integers
 * @base: Base integer (big endian byte array)
 * @base_len: Length of base integer in bytes
 * @power: Power integer (big endian byte array)
 * @power_len: Length of power integer in bytes
 * @modulus: Modulus integer (big endian byte array)
 * @modulus_len: Length of modulus integer in bytes
 * @result: Buffer for the result
 * @result_len: Result length (max buffer size on input, real len on output)
 * Returns: 0 on success, -1 on failure
 *
 * This function calculates result = base ^ power mod modulus. modules_len is
 * used as the maximum size of modulus buffer. It is set to the used size on
 * success.
 *
 * This function is only used with internal TLSv1 implementation
 * (CONFIG_TLS=internal). If that is not used, the crypto wrapper does not need
 * to implement this.
 */
int __must_check crypto_mod_exp(const u8 *base, size_t base_len,
				const u8 *power, size_t power_len,
				const u8 *modulus, size_t modulus_len,
				u8 *result, size_t *result_len);

/**
 * rc4_skip - XOR RC4 stream to given data with skip-stream-start
 * @key: RC4 key
 * @keylen: RC4 key length
 * @skip: number of bytes to skip from the beginning of the RC4 stream
 * @data: data to be XOR'ed with RC4 stream
 * @data_len: buf length
 * Returns: 0 on success, -1 on failure
 *
 * Generate RC4 pseudo random stream for the given key, skip beginning of the
 * stream, and XOR the end result with the data buffer to perform RC4
 * encryption/decryption.
 */
int rc4_skip(const u8 *key, size_t keylen, size_t skip,
	     u8 *data, size_t data_len);

#endif /* CRYPTO_H */
