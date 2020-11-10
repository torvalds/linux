/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Kerberos5 crypto internals
 *
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <crypto/krb5.h>

/*
 * Profile used for key derivation and encryption.
 */
struct krb5_crypto_profile {
	 /* Pseudo-random function */
	int (*calc_PRF)(const struct krb5_enctype *krb5,
			const struct krb5_buffer *protocol_key,
			const struct krb5_buffer *octet_string,
			struct krb5_buffer *result,
			gfp_t gfp);

	/* Checksum key derivation */
	int (*calc_Kc)(const struct krb5_enctype *krb5,
		       const struct krb5_buffer *TK,
		       const struct krb5_buffer *usage_constant,
		       struct krb5_buffer *Kc,
		       gfp_t gfp);

	/* Encryption key derivation */
	int (*calc_Ke)(const struct krb5_enctype *krb5,
		       const struct krb5_buffer *TK,
		       const struct krb5_buffer *usage_constant,
		       struct krb5_buffer *Ke,
		       gfp_t gfp);

	 /* Integrity key derivation */
	int (*calc_Ki)(const struct krb5_enctype *krb5,
		       const struct krb5_buffer *TK,
		       const struct krb5_buffer *usage_constant,
		       struct krb5_buffer *Ki,
		       gfp_t gfp);

	/* Derive the keys needed for an encryption AEAD object. */
	int (*derive_encrypt_keys)(const struct krb5_enctype *krb5,
				   const struct krb5_buffer *TK,
				   unsigned int usage,
				   struct krb5_buffer *setkey,
				   gfp_t gfp);

	/* Directly load the keys needed for an encryption AEAD object. */
	int (*load_encrypt_keys)(const struct krb5_enctype *krb5,
				 const struct krb5_buffer *Ke,
				 const struct krb5_buffer *Ki,
				 struct krb5_buffer *setkey,
				 gfp_t gfp);

	/* Derive the key needed for a checksum hash object. */
	int (*derive_checksum_key)(const struct krb5_enctype *krb5,
				   const struct krb5_buffer *TK,
				   unsigned int usage,
				   struct krb5_buffer *setkey,
				   gfp_t gfp);

	/* Directly load the keys needed for a checksum hash object. */
	int (*load_checksum_key)(const struct krb5_enctype *krb5,
				 const struct krb5_buffer *Kc,
				 struct krb5_buffer *setkey,
				 gfp_t gfp);

	/* Encrypt data in-place, inserting confounder and checksum. */
	ssize_t (*encrypt)(const struct krb5_enctype *krb5,
			   struct crypto_aead *aead,
			   struct scatterlist *sg, unsigned int nr_sg,
			   size_t sg_len,
			   size_t data_offset, size_t data_len,
			   bool preconfounded);

	/* Decrypt data in-place, removing confounder and checksum */
	int (*decrypt)(const struct krb5_enctype *krb5,
		       struct crypto_aead *aead,
		       struct scatterlist *sg, unsigned int nr_sg,
		       size_t *_offset, size_t *_len);

	/* Generate a MIC on part of a packet, inserting the checksum */
	ssize_t (*get_mic)(const struct krb5_enctype *krb5,
			   struct crypto_shash *shash,
			   const struct krb5_buffer *metadata,
			   struct scatterlist *sg, unsigned int nr_sg,
			   size_t sg_len,
			   size_t data_offset, size_t data_len);

	/* Verify the MIC on a piece of data, removing the checksum */
	int (*verify_mic)(const struct krb5_enctype *krb5,
			  struct crypto_shash *shash,
			  const struct krb5_buffer *metadata,
			  struct scatterlist *sg, unsigned int nr_sg,
			  size_t *_offset, size_t *_len);
};

/*
 * Crypto size/alignment rounding convenience macros.
 */
#define crypto_roundup(X) ((unsigned int)round_up((X), CRYPTO_MINALIGN))

#define krb5_aead_size(TFM) \
	crypto_roundup(sizeof(struct aead_request) + crypto_aead_reqsize(TFM))
#define krb5_aead_ivsize(TFM) \
	crypto_roundup(crypto_aead_ivsize(TFM))
#define krb5_shash_size(TFM) \
	crypto_roundup(sizeof(struct shash_desc) + crypto_shash_descsize(TFM))
#define krb5_digest_size(TFM) \
	crypto_roundup(crypto_shash_digestsize(TFM))
#define round16(x) (((x) + 15) & ~15)
