// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Support for AES-NI and VAES instructions.  This file contains glue code.
 * The real AES implementations are in .S files.
 *
 * Copyright (C) 2008, Intel Corp.
 *    Author: Huang Ying <ying.huang@intel.com>
 *
 * Added RFC4106 AES-GCM support for 128-bit keys under the AEAD
 * interface for 64-bit kernels.
 *    Authors: Adrian Hoban <adrian.hoban@intel.com>
 *             Gabriele Paoloni <gabriele.paoloni@intel.com>
 *             Tadeusz Struk (tadeusz.struk@intel.com)
 *             Aidan O'Mahony (aidan.o.mahony@intel.com)
 *    Copyright (c) 2010, Intel Corporation.
 *
 * Copyright 2024 Google LLC
 */

#include <linux/hardirq.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/err.h>
#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/b128ops.h>
#include <crypto/gcm.h>
#include <crypto/gf128mul.h>
#include <asm/cpu_device_id.h>
#include <asm/simd.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <linux/jump_label.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/static_call.h>

/* The common part of the x86_64 AES-GCM key struct */
struct aes_gcm_key {
	/* Expanded AES key and the AES key length in bytes */
	struct aes_enckey aes_key;

	/* RFC4106 nonce (used only by the rfc4106 algorithms) */
	u32 rfc4106_nonce;
};

/* Key struct used by the AES-NI implementations of AES-GCM */
struct aes_gcm_key_aesni {
	/*
	 * Common part of the key.  16-byte alignment is required by the
	 * assembly code.
	 */
	struct aes_gcm_key base __aligned(16);

	/*
	 * Powers of the hash key H^8 through H^1.  These are 128-bit values.
	 * They all have an extra factor of x^-1 and are byte-reversed.  16-byte
	 * alignment is required by the assembly code.
	 */
	u64 h_powers[8][2] __aligned(16);

	/*
	 * h_powers_xored[i] contains the two 64-bit halves of h_powers[i] XOR'd
	 * together.  It's used for Karatsuba multiplication.  16-byte alignment
	 * is required by the assembly code.
	 */
	u64 h_powers_xored[8] __aligned(16);

	/*
	 * H^1 times x^64 (and also the usual extra factor of x^-1).  16-byte
	 * alignment is required by the assembly code.
	 */
	u64 h_times_x64[2] __aligned(16);
};
#define AES_GCM_KEY_AESNI(key)	\
	container_of((key), struct aes_gcm_key_aesni, base)
#define AES_GCM_KEY_AESNI_SIZE	\
	(sizeof(struct aes_gcm_key_aesni) + (15 & ~(CRYPTO_MINALIGN - 1)))

/* Key struct used by the VAES + AVX2 implementation of AES-GCM */
struct aes_gcm_key_vaes_avx2 {
	/*
	 * Common part of the key.  The assembly code prefers 16-byte alignment
	 * for this.
	 */
	struct aes_gcm_key base __aligned(16);

	/*
	 * Powers of the hash key H^8 through H^1.  These are 128-bit values.
	 * They all have an extra factor of x^-1 and are byte-reversed.
	 * The assembly code prefers 32-byte alignment for this.
	 */
	u64 h_powers[8][2] __aligned(32);

	/*
	 * Each entry in this array contains the two halves of an entry of
	 * h_powers XOR'd together, in the following order:
	 * H^8,H^6,H^7,H^5,H^4,H^2,H^3,H^1 i.e. indices 0,2,1,3,4,6,5,7.
	 * This is used for Karatsuba multiplication.
	 */
	u64 h_powers_xored[8];
};

#define AES_GCM_KEY_VAES_AVX2(key) \
	container_of((key), struct aes_gcm_key_vaes_avx2, base)
#define AES_GCM_KEY_VAES_AVX2_SIZE \
	(sizeof(struct aes_gcm_key_vaes_avx2) + (31 & ~(CRYPTO_MINALIGN - 1)))

/* Key struct used by the VAES + AVX512 implementation of AES-GCM */
struct aes_gcm_key_vaes_avx512 {
	/*
	 * Common part of the key.  The assembly code prefers 16-byte alignment
	 * for this.
	 */
	struct aes_gcm_key base __aligned(16);

	/*
	 * Powers of the hash key H^16 through H^1.  These are 128-bit values.
	 * They all have an extra factor of x^-1 and are byte-reversed.  This
	 * array is aligned to a 64-byte boundary to make it naturally aligned
	 * for 512-bit loads, which can improve performance.  (The assembly code
	 * doesn't *need* the alignment; this is just an optimization.)
	 */
	u64 h_powers[16][2] __aligned(64);

	/* Three padding blocks required by the assembly code */
	u64 padding[3][2];
};
#define AES_GCM_KEY_VAES_AVX512(key) \
	container_of((key), struct aes_gcm_key_vaes_avx512, base)
#define AES_GCM_KEY_VAES_AVX512_SIZE \
	(sizeof(struct aes_gcm_key_vaes_avx512) + (63 & ~(CRYPTO_MINALIGN - 1)))

/*
 * These flags are passed to the AES-GCM helper functions to specify the
 * specific version of AES-GCM (RFC4106 or not), whether it's encryption or
 * decryption, and which assembly functions should be called.  Assembly
 * functions are selected using flags instead of function pointers to avoid
 * indirect calls (which are very expensive on x86) regardless of inlining.
 */
#define FLAG_RFC4106	BIT(0)
#define FLAG_ENC	BIT(1)
#define FLAG_AVX	BIT(2)
#define FLAG_VAES_AVX2	BIT(3)
#define FLAG_VAES_AVX512 BIT(4)

static inline struct aes_gcm_key *
aes_gcm_key_get(struct crypto_aead *tfm, int flags)
{
	if (flags & FLAG_VAES_AVX512)
		return PTR_ALIGN(crypto_aead_ctx(tfm), 64);
	else if (flags & FLAG_VAES_AVX2)
		return PTR_ALIGN(crypto_aead_ctx(tfm), 32);
	else
		return PTR_ALIGN(crypto_aead_ctx(tfm), 16);
}

asmlinkage void
aes_gcm_precompute_aesni(struct aes_gcm_key_aesni *key);
asmlinkage void
aes_gcm_precompute_aesni_avx(struct aes_gcm_key_aesni *key);
asmlinkage void
aes_gcm_precompute_vaes_avx2(struct aes_gcm_key_vaes_avx2 *key);
asmlinkage void
aes_gcm_precompute_vaes_avx512(struct aes_gcm_key_vaes_avx512 *key);

static void aes_gcm_precompute(struct aes_gcm_key *key, int flags)
{
	if (flags & FLAG_VAES_AVX512)
		aes_gcm_precompute_vaes_avx512(AES_GCM_KEY_VAES_AVX512(key));
	else if (flags & FLAG_VAES_AVX2)
		aes_gcm_precompute_vaes_avx2(AES_GCM_KEY_VAES_AVX2(key));
	else if (flags & FLAG_AVX)
		aes_gcm_precompute_aesni_avx(AES_GCM_KEY_AESNI(key));
	else
		aes_gcm_precompute_aesni(AES_GCM_KEY_AESNI(key));
}

asmlinkage void
aes_gcm_aad_update_aesni(const struct aes_gcm_key_aesni *key,
			 u8 ghash_acc[16], const u8 *aad, int aadlen);
asmlinkage void
aes_gcm_aad_update_aesni_avx(const struct aes_gcm_key_aesni *key,
			     u8 ghash_acc[16], const u8 *aad, int aadlen);
asmlinkage void
aes_gcm_aad_update_vaes_avx2(const struct aes_gcm_key_vaes_avx2 *key,
			     u8 ghash_acc[16], const u8 *aad, int aadlen);
asmlinkage void
aes_gcm_aad_update_vaes_avx512(const struct aes_gcm_key_vaes_avx512 *key,
			       u8 ghash_acc[16], const u8 *aad, int aadlen);

static void aes_gcm_aad_update(const struct aes_gcm_key *key, u8 ghash_acc[16],
			       const u8 *aad, int aadlen, int flags)
{
	if (flags & FLAG_VAES_AVX512)
		aes_gcm_aad_update_vaes_avx512(AES_GCM_KEY_VAES_AVX512(key),
					       ghash_acc, aad, aadlen);
	else if (flags & FLAG_VAES_AVX2)
		aes_gcm_aad_update_vaes_avx2(AES_GCM_KEY_VAES_AVX2(key),
					     ghash_acc, aad, aadlen);
	else if (flags & FLAG_AVX)
		aes_gcm_aad_update_aesni_avx(AES_GCM_KEY_AESNI(key), ghash_acc,
					     aad, aadlen);
	else
		aes_gcm_aad_update_aesni(AES_GCM_KEY_AESNI(key), ghash_acc,
					 aad, aadlen);
}

asmlinkage void
aes_gcm_enc_update_aesni(const struct aes_gcm_key_aesni *key,
			 const u32 le_ctr[4], u8 ghash_acc[16],
			 const u8 *src, u8 *dst, int datalen);
asmlinkage void
aes_gcm_enc_update_aesni_avx(const struct aes_gcm_key_aesni *key,
			     const u32 le_ctr[4], u8 ghash_acc[16],
			     const u8 *src, u8 *dst, int datalen);
asmlinkage void
aes_gcm_enc_update_vaes_avx2(const struct aes_gcm_key_vaes_avx2 *key,
			     const u32 le_ctr[4], u8 ghash_acc[16],
			     const u8 *src, u8 *dst, int datalen);
asmlinkage void
aes_gcm_enc_update_vaes_avx512(const struct aes_gcm_key_vaes_avx512 *key,
			       const u32 le_ctr[4], u8 ghash_acc[16],
			       const u8 *src, u8 *dst, int datalen);

asmlinkage void
aes_gcm_dec_update_aesni(const struct aes_gcm_key_aesni *key,
			 const u32 le_ctr[4], u8 ghash_acc[16],
			 const u8 *src, u8 *dst, int datalen);
asmlinkage void
aes_gcm_dec_update_aesni_avx(const struct aes_gcm_key_aesni *key,
			     const u32 le_ctr[4], u8 ghash_acc[16],
			     const u8 *src, u8 *dst, int datalen);
asmlinkage void
aes_gcm_dec_update_vaes_avx2(const struct aes_gcm_key_vaes_avx2 *key,
			     const u32 le_ctr[4], u8 ghash_acc[16],
			     const u8 *src, u8 *dst, int datalen);
asmlinkage void
aes_gcm_dec_update_vaes_avx512(const struct aes_gcm_key_vaes_avx512 *key,
			       const u32 le_ctr[4], u8 ghash_acc[16],
			       const u8 *src, u8 *dst, int datalen);

/* __always_inline to optimize out the branches based on @flags */
static __always_inline void
aes_gcm_update(const struct aes_gcm_key *key,
	       const u32 le_ctr[4], u8 ghash_acc[16],
	       const u8 *src, u8 *dst, int datalen, int flags)
{
	if (flags & FLAG_ENC) {
		if (flags & FLAG_VAES_AVX512)
			aes_gcm_enc_update_vaes_avx512(AES_GCM_KEY_VAES_AVX512(key),
						       le_ctr, ghash_acc,
						       src, dst, datalen);
		else if (flags & FLAG_VAES_AVX2)
			aes_gcm_enc_update_vaes_avx2(AES_GCM_KEY_VAES_AVX2(key),
						     le_ctr, ghash_acc,
						     src, dst, datalen);
		else if (flags & FLAG_AVX)
			aes_gcm_enc_update_aesni_avx(AES_GCM_KEY_AESNI(key),
						     le_ctr, ghash_acc,
						     src, dst, datalen);
		else
			aes_gcm_enc_update_aesni(AES_GCM_KEY_AESNI(key), le_ctr,
						 ghash_acc, src, dst, datalen);
	} else {
		if (flags & FLAG_VAES_AVX512)
			aes_gcm_dec_update_vaes_avx512(AES_GCM_KEY_VAES_AVX512(key),
						       le_ctr, ghash_acc,
						       src, dst, datalen);
		else if (flags & FLAG_VAES_AVX2)
			aes_gcm_dec_update_vaes_avx2(AES_GCM_KEY_VAES_AVX2(key),
						     le_ctr, ghash_acc,
						     src, dst, datalen);
		else if (flags & FLAG_AVX)
			aes_gcm_dec_update_aesni_avx(AES_GCM_KEY_AESNI(key),
						     le_ctr, ghash_acc,
						     src, dst, datalen);
		else
			aes_gcm_dec_update_aesni(AES_GCM_KEY_AESNI(key),
						 le_ctr, ghash_acc,
						 src, dst, datalen);
	}
}

asmlinkage void
aes_gcm_enc_final_aesni(const struct aes_gcm_key_aesni *key,
			const u32 le_ctr[4], u8 ghash_acc[16],
			u64 total_aadlen, u64 total_datalen);
asmlinkage void
aes_gcm_enc_final_aesni_avx(const struct aes_gcm_key_aesni *key,
			    const u32 le_ctr[4], u8 ghash_acc[16],
			    u64 total_aadlen, u64 total_datalen);
asmlinkage void
aes_gcm_enc_final_vaes_avx2(const struct aes_gcm_key_vaes_avx2 *key,
			    const u32 le_ctr[4], u8 ghash_acc[16],
			    u64 total_aadlen, u64 total_datalen);
asmlinkage void
aes_gcm_enc_final_vaes_avx512(const struct aes_gcm_key_vaes_avx512 *key,
			      const u32 le_ctr[4], u8 ghash_acc[16],
			      u64 total_aadlen, u64 total_datalen);

/* __always_inline to optimize out the branches based on @flags */
static __always_inline void
aes_gcm_enc_final(const struct aes_gcm_key *key,
		  const u32 le_ctr[4], u8 ghash_acc[16],
		  u64 total_aadlen, u64 total_datalen, int flags)
{
	if (flags & FLAG_VAES_AVX512)
		aes_gcm_enc_final_vaes_avx512(AES_GCM_KEY_VAES_AVX512(key),
					      le_ctr, ghash_acc,
					      total_aadlen, total_datalen);
	else if (flags & FLAG_VAES_AVX2)
		aes_gcm_enc_final_vaes_avx2(AES_GCM_KEY_VAES_AVX2(key),
					    le_ctr, ghash_acc,
					    total_aadlen, total_datalen);
	else if (flags & FLAG_AVX)
		aes_gcm_enc_final_aesni_avx(AES_GCM_KEY_AESNI(key),
					    le_ctr, ghash_acc,
					    total_aadlen, total_datalen);
	else
		aes_gcm_enc_final_aesni(AES_GCM_KEY_AESNI(key),
					le_ctr, ghash_acc,
					total_aadlen, total_datalen);
}

asmlinkage bool __must_check
aes_gcm_dec_final_aesni(const struct aes_gcm_key_aesni *key,
			const u32 le_ctr[4], const u8 ghash_acc[16],
			u64 total_aadlen, u64 total_datalen,
			const u8 tag[16], int taglen);
asmlinkage bool __must_check
aes_gcm_dec_final_aesni_avx(const struct aes_gcm_key_aesni *key,
			    const u32 le_ctr[4], const u8 ghash_acc[16],
			    u64 total_aadlen, u64 total_datalen,
			    const u8 tag[16], int taglen);
asmlinkage bool __must_check
aes_gcm_dec_final_vaes_avx2(const struct aes_gcm_key_vaes_avx2 *key,
			    const u32 le_ctr[4], const u8 ghash_acc[16],
			    u64 total_aadlen, u64 total_datalen,
			    const u8 tag[16], int taglen);
asmlinkage bool __must_check
aes_gcm_dec_final_vaes_avx512(const struct aes_gcm_key_vaes_avx512 *key,
			      const u32 le_ctr[4], const u8 ghash_acc[16],
			      u64 total_aadlen, u64 total_datalen,
			      const u8 tag[16], int taglen);

/* __always_inline to optimize out the branches based on @flags */
static __always_inline bool __must_check
aes_gcm_dec_final(const struct aes_gcm_key *key, const u32 le_ctr[4],
		  u8 ghash_acc[16], u64 total_aadlen, u64 total_datalen,
		  u8 tag[16], int taglen, int flags)
{
	if (flags & FLAG_VAES_AVX512)
		return aes_gcm_dec_final_vaes_avx512(AES_GCM_KEY_VAES_AVX512(key),
						     le_ctr, ghash_acc,
						     total_aadlen, total_datalen,
						     tag, taglen);
	else if (flags & FLAG_VAES_AVX2)
		return aes_gcm_dec_final_vaes_avx2(AES_GCM_KEY_VAES_AVX2(key),
						   le_ctr, ghash_acc,
						   total_aadlen, total_datalen,
						   tag, taglen);
	else if (flags & FLAG_AVX)
		return aes_gcm_dec_final_aesni_avx(AES_GCM_KEY_AESNI(key),
						   le_ctr, ghash_acc,
						   total_aadlen, total_datalen,
						   tag, taglen);
	else
		return aes_gcm_dec_final_aesni(AES_GCM_KEY_AESNI(key),
					       le_ctr, ghash_acc,
					       total_aadlen, total_datalen,
					       tag, taglen);
}

/*
 * This is the Integrity Check Value (aka the authentication tag) length and can
 * be 8, 12 or 16 bytes long.
 */
static int common_rfc4106_set_authsize(struct crypto_aead *aead,
				       unsigned int authsize)
{
	switch (authsize) {
	case 8:
	case 12:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int generic_gcmaes_set_authsize(struct crypto_aead *tfm,
				       unsigned int authsize)
{
	switch (authsize) {
	case 4:
	case 8:
	case 12:
	case 13:
	case 14:
	case 15:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * This is the setkey function for the x86_64 implementations of AES-GCM.  It
 * saves the RFC4106 nonce if applicable, expands the AES key, and precomputes
 * powers of the hash key.
 *
 * To comply with the crypto_aead API, this has to be usable in no-SIMD context.
 * For that reason, this function includes a portable C implementation of the
 * needed logic.  However, the portable C implementation is very slow, taking
 * about the same time as encrypting 37 KB of data.  To be ready for users that
 * may set a key even somewhat frequently, we therefore also include a SIMD
 * assembly implementation, expanding the AES key using AES-NI and precomputing
 * the hash key powers using PCLMULQDQ or VPCLMULQDQ.
 */
static int gcm_setkey(struct crypto_aead *tfm, const u8 *raw_key,
		      unsigned int keylen, int flags)
{
	struct aes_gcm_key *key = aes_gcm_key_get(tfm, flags);
	int err;

	if (flags & FLAG_RFC4106) {
		if (keylen < 4)
			return -EINVAL;
		keylen -= 4;
		key->rfc4106_nonce = get_unaligned_be32(raw_key + keylen);
	}

	/* The assembly code assumes the following offsets. */
	static_assert(offsetof(struct aes_gcm_key_aesni, base.aes_key.len) == 0);
	static_assert(offsetof(struct aes_gcm_key_aesni, base.aes_key.k.rndkeys) == 16);
	static_assert(offsetof(struct aes_gcm_key_aesni, h_powers) == 272);
	static_assert(offsetof(struct aes_gcm_key_aesni, h_powers_xored) == 400);
	static_assert(offsetof(struct aes_gcm_key_aesni, h_times_x64) == 464);
	static_assert(offsetof(struct aes_gcm_key_vaes_avx2, base.aes_key.len) == 0);
	static_assert(offsetof(struct aes_gcm_key_vaes_avx2, base.aes_key.k.rndkeys) == 16);
	static_assert(offsetof(struct aes_gcm_key_vaes_avx2, h_powers) == 288);
	static_assert(offsetof(struct aes_gcm_key_vaes_avx2, h_powers_xored) == 416);
	static_assert(offsetof(struct aes_gcm_key_vaes_avx512, base.aes_key.len) == 0);
	static_assert(offsetof(struct aes_gcm_key_vaes_avx512, base.aes_key.k.rndkeys) == 16);
	static_assert(offsetof(struct aes_gcm_key_vaes_avx512, h_powers) == 320);
	static_assert(offsetof(struct aes_gcm_key_vaes_avx512, padding) == 576);

	err = aes_prepareenckey(&key->aes_key, raw_key, keylen);
	if (err)
		return err;

	if (likely(crypto_simd_usable())) {
		kernel_fpu_begin();
		aes_gcm_precompute(key, flags);
		kernel_fpu_end();
	} else {
		static const u8 x_to_the_minus1[16] __aligned(__alignof__(be128)) = {
			[0] = 0xc2, [15] = 1
		};
		static const u8 x_to_the_63[16] __aligned(__alignof__(be128)) = {
			[7] = 1,
		};
		be128 h1 = {};
		be128 h;
		int i;

		/* Encrypt the all-zeroes block to get the hash key H^1 */
		aes_encrypt(&key->aes_key, (u8 *)&h1, (u8 *)&h1);

		/* Compute H^1 * x^-1 */
		h = h1;
		gf128mul_lle(&h, (const be128 *)x_to_the_minus1);

		/* Compute the needed key powers */
		if (flags & FLAG_VAES_AVX512) {
			struct aes_gcm_key_vaes_avx512 *k =
				AES_GCM_KEY_VAES_AVX512(key);

			for (i = ARRAY_SIZE(k->h_powers) - 1; i >= 0; i--) {
				k->h_powers[i][0] = be64_to_cpu(h.b);
				k->h_powers[i][1] = be64_to_cpu(h.a);
				gf128mul_lle(&h, &h1);
			}
			memset(k->padding, 0, sizeof(k->padding));
		} else if (flags & FLAG_VAES_AVX2) {
			struct aes_gcm_key_vaes_avx2 *k =
				AES_GCM_KEY_VAES_AVX2(key);
			static const u8 indices[8] = { 0, 2, 1, 3, 4, 6, 5, 7 };

			for (i = ARRAY_SIZE(k->h_powers) - 1; i >= 0; i--) {
				k->h_powers[i][0] = be64_to_cpu(h.b);
				k->h_powers[i][1] = be64_to_cpu(h.a);
				gf128mul_lle(&h, &h1);
			}
			for (i = 0; i < ARRAY_SIZE(k->h_powers_xored); i++) {
				int j = indices[i];

				k->h_powers_xored[i] = k->h_powers[j][0] ^
						       k->h_powers[j][1];
			}
		} else {
			struct aes_gcm_key_aesni *k = AES_GCM_KEY_AESNI(key);

			for (i = ARRAY_SIZE(k->h_powers) - 1; i >= 0; i--) {
				k->h_powers[i][0] = be64_to_cpu(h.b);
				k->h_powers[i][1] = be64_to_cpu(h.a);
				k->h_powers_xored[i] = k->h_powers[i][0] ^
						       k->h_powers[i][1];
				gf128mul_lle(&h, &h1);
			}
			gf128mul_lle(&h1, (const be128 *)x_to_the_63);
			k->h_times_x64[0] = be64_to_cpu(h1.b);
			k->h_times_x64[1] = be64_to_cpu(h1.a);
		}
	}
	return 0;
}

/*
 * Initialize @ghash_acc, then pass all @assoclen bytes of associated data
 * (a.k.a. additional authenticated data) from @sg_src through the GHASH update
 * assembly function.  kernel_fpu_begin() must have already been called.
 */
static void gcm_process_assoc(const struct aes_gcm_key *key, u8 ghash_acc[16],
			      struct scatterlist *sg_src, unsigned int assoclen,
			      int flags)
{
	struct scatter_walk walk;
	/*
	 * The assembly function requires that the length of any non-last
	 * segment of associated data be a multiple of 16 bytes, so this
	 * function does the buffering needed to achieve that.
	 */
	unsigned int pos = 0;
	u8 buf[16];

	memset(ghash_acc, 0, 16);
	scatterwalk_start(&walk, sg_src);

	while (assoclen) {
		unsigned int orig_len_this_step = scatterwalk_next(
			&walk, assoclen);
		unsigned int len_this_step = orig_len_this_step;
		unsigned int len;
		const u8 *src = walk.addr;

		assoclen -= orig_len_this_step;
		if (unlikely(pos)) {
			len = min(len_this_step, 16 - pos);
			memcpy(&buf[pos], src, len);
			pos += len;
			src += len;
			len_this_step -= len;
			if (pos < 16)
				goto next;
			aes_gcm_aad_update(key, ghash_acc, buf, 16, flags);
			pos = 0;
		}
		len = len_this_step;
		if (unlikely(assoclen)) /* Not the last segment yet? */
			len = round_down(len, 16);
		aes_gcm_aad_update(key, ghash_acc, src, len, flags);
		src += len;
		len_this_step -= len;
		if (unlikely(len_this_step)) {
			memcpy(buf, src, len_this_step);
			pos = len_this_step;
		}
next:
		scatterwalk_done_src(&walk, orig_len_this_step);
		if (need_resched()) {
			kernel_fpu_end();
			kernel_fpu_begin();
		}
	}
	if (unlikely(pos))
		aes_gcm_aad_update(key, ghash_acc, buf, pos, flags);
}


/* __always_inline to optimize out the branches based on @flags */
static __always_inline int
gcm_crypt(struct aead_request *req, int flags)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	const struct aes_gcm_key *key = aes_gcm_key_get(tfm, flags);
	unsigned int assoclen = req->assoclen;
	struct skcipher_walk walk;
	unsigned int nbytes;
	u8 ghash_acc[16]; /* GHASH accumulator */
	u32 le_ctr[4]; /* Counter in little-endian format */
	int taglen;
	int err;

	/* Initialize the counter and determine the associated data length. */
	le_ctr[0] = 2;
	if (flags & FLAG_RFC4106) {
		if (unlikely(assoclen != 16 && assoclen != 20))
			return -EINVAL;
		assoclen -= 8;
		le_ctr[1] = get_unaligned_be32(req->iv + 4);
		le_ctr[2] = get_unaligned_be32(req->iv + 0);
		le_ctr[3] = key->rfc4106_nonce; /* already byte-swapped */
	} else {
		le_ctr[1] = get_unaligned_be32(req->iv + 8);
		le_ctr[2] = get_unaligned_be32(req->iv + 4);
		le_ctr[3] = get_unaligned_be32(req->iv + 0);
	}

	/* Begin walking through the plaintext or ciphertext. */
	if (flags & FLAG_ENC)
		err = skcipher_walk_aead_encrypt(&walk, req, false);
	else
		err = skcipher_walk_aead_decrypt(&walk, req, false);
	if (err)
		return err;

	/*
	 * Since the AES-GCM assembly code requires that at least three assembly
	 * functions be called to process any message (this is needed to support
	 * incremental updates cleanly), to reduce overhead we try to do all
	 * three calls in the same kernel FPU section if possible.  We close the
	 * section and start a new one if there are multiple data segments or if
	 * rescheduling is needed while processing the associated data.
	 */
	kernel_fpu_begin();

	/* Pass the associated data through GHASH. */
	gcm_process_assoc(key, ghash_acc, req->src, assoclen, flags);

	/* En/decrypt the data and pass the ciphertext through GHASH. */
	while (unlikely((nbytes = walk.nbytes) < walk.total)) {
		/*
		 * Non-last segment.  In this case, the assembly function
		 * requires that the length be a multiple of 16 (AES_BLOCK_SIZE)
		 * bytes.  The needed buffering of up to 16 bytes is handled by
		 * the skcipher_walk.  Here we just need to round down to a
		 * multiple of 16.
		 */
		nbytes = round_down(nbytes, AES_BLOCK_SIZE);
		aes_gcm_update(key, le_ctr, ghash_acc, walk.src.virt.addr,
			       walk.dst.virt.addr, nbytes, flags);
		le_ctr[0] += nbytes / AES_BLOCK_SIZE;
		kernel_fpu_end();
		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
		if (err)
			return err;
		kernel_fpu_begin();
	}
	/* Last segment: process all remaining data. */
	aes_gcm_update(key, le_ctr, ghash_acc, walk.src.virt.addr,
		       walk.dst.virt.addr, nbytes, flags);
	/*
	 * The low word of the counter isn't used by the finalize, so there's no
	 * need to increment it here.
	 */

	/* Finalize */
	taglen = crypto_aead_authsize(tfm);
	if (flags & FLAG_ENC) {
		/* Finish computing the auth tag. */
		aes_gcm_enc_final(key, le_ctr, ghash_acc, assoclen,
				  req->cryptlen, flags);

		/* Store the computed auth tag in the dst scatterlist. */
		scatterwalk_map_and_copy(ghash_acc, req->dst, req->assoclen +
					 req->cryptlen, taglen, 1);
	} else {
		unsigned int datalen = req->cryptlen - taglen;
		u8 tag[16];

		/* Get the transmitted auth tag from the src scatterlist. */
		scatterwalk_map_and_copy(tag, req->src, req->assoclen + datalen,
					 taglen, 0);
		/*
		 * Finish computing the auth tag and compare it to the
		 * transmitted one.  The assembly function does the actual tag
		 * comparison.  Here, just check the boolean result.
		 */
		if (!aes_gcm_dec_final(key, le_ctr, ghash_acc, assoclen,
				       datalen, tag, taglen, flags))
			err = -EBADMSG;
	}
	kernel_fpu_end();
	if (nbytes)
		skcipher_walk_done(&walk, 0);
	return err;
}

#define DEFINE_GCM_ALGS(suffix, flags, generic_driver_name, rfc_driver_name,   \
			ctxsize, priority)				       \
									       \
static int gcm_setkey_##suffix(struct crypto_aead *tfm, const u8 *raw_key,     \
			       unsigned int keylen)			       \
{									       \
	return gcm_setkey(tfm, raw_key, keylen, (flags));		       \
}									       \
									       \
static int gcm_encrypt_##suffix(struct aead_request *req)		       \
{									       \
	return gcm_crypt(req, (flags) | FLAG_ENC);			       \
}									       \
									       \
static int gcm_decrypt_##suffix(struct aead_request *req)		       \
{									       \
	return gcm_crypt(req, (flags));					       \
}									       \
									       \
static int rfc4106_setkey_##suffix(struct crypto_aead *tfm, const u8 *raw_key, \
				   unsigned int keylen)			       \
{									       \
	return gcm_setkey(tfm, raw_key, keylen, (flags) | FLAG_RFC4106);       \
}									       \
									       \
static int rfc4106_encrypt_##suffix(struct aead_request *req)		       \
{									       \
	return gcm_crypt(req, (flags) | FLAG_RFC4106 | FLAG_ENC);	       \
}									       \
									       \
static int rfc4106_decrypt_##suffix(struct aead_request *req)		       \
{									       \
	return gcm_crypt(req, (flags) | FLAG_RFC4106);			       \
}									       \
									       \
static struct aead_alg aes_gcm_algs_##suffix[] = { {			       \
	.setkey			= gcm_setkey_##suffix,			       \
	.setauthsize		= generic_gcmaes_set_authsize,		       \
	.encrypt		= gcm_encrypt_##suffix,			       \
	.decrypt		= gcm_decrypt_##suffix,			       \
	.ivsize			= GCM_AES_IV_SIZE,			       \
	.chunksize		= AES_BLOCK_SIZE,			       \
	.maxauthsize		= 16,					       \
	.base = {							       \
		.cra_name		= "gcm(aes)",			       \
		.cra_driver_name	= generic_driver_name,		       \
		.cra_priority		= (priority),			       \
		.cra_blocksize		= 1,				       \
		.cra_ctxsize		= (ctxsize),			       \
		.cra_module		= THIS_MODULE,			       \
	},								       \
}, {									       \
	.setkey			= rfc4106_setkey_##suffix,		       \
	.setauthsize		= common_rfc4106_set_authsize,		       \
	.encrypt		= rfc4106_encrypt_##suffix,		       \
	.decrypt		= rfc4106_decrypt_##suffix,		       \
	.ivsize			= GCM_RFC4106_IV_SIZE,			       \
	.chunksize		= AES_BLOCK_SIZE,			       \
	.maxauthsize		= 16,					       \
	.base = {							       \
		.cra_name		= "rfc4106(gcm(aes))",		       \
		.cra_driver_name	= rfc_driver_name,		       \
		.cra_priority		= (priority),			       \
		.cra_blocksize		= 1,				       \
		.cra_ctxsize		= (ctxsize),			       \
		.cra_module		= THIS_MODULE,			       \
	},								       \
} }

/* aes_gcm_algs_aesni */
DEFINE_GCM_ALGS(aesni, /* no flags */ 0,
		"generic-gcm-aesni", "rfc4106-gcm-aesni",
		AES_GCM_KEY_AESNI_SIZE, 400);

/* aes_gcm_algs_aesni_avx */
DEFINE_GCM_ALGS(aesni_avx, FLAG_AVX,
		"generic-gcm-aesni-avx", "rfc4106-gcm-aesni-avx",
		AES_GCM_KEY_AESNI_SIZE, 500);

/* aes_gcm_algs_vaes_avx2 */
DEFINE_GCM_ALGS(vaes_avx2, FLAG_VAES_AVX2,
		"generic-gcm-vaes-avx2", "rfc4106-gcm-vaes-avx2",
		AES_GCM_KEY_VAES_AVX2_SIZE, 600);

/* aes_gcm_algs_vaes_avx512 */
DEFINE_GCM_ALGS(vaes_avx512, FLAG_VAES_AVX512,
		"generic-gcm-vaes-avx512", "rfc4106-gcm-vaes-avx512",
		AES_GCM_KEY_VAES_AVX512_SIZE, 800);

static int __init register_avx_algs(void)
{
	int err;

	if (!boot_cpu_has(X86_FEATURE_AVX))
		return 0;
	err = crypto_register_aeads(aes_gcm_algs_aesni_avx,
				    ARRAY_SIZE(aes_gcm_algs_aesni_avx));
	if (err)
		return err;
	/*
	 * Note: not all the algorithms registered below actually require
	 * VPCLMULQDQ.  But in practice every CPU with VAES also has VPCLMULQDQ.
	 * Similarly, the assembler support was added at about the same time.
	 * For simplicity, just always check for VAES and VPCLMULQDQ together.
	 */
	if (!boot_cpu_has(X86_FEATURE_AVX2) ||
	    !boot_cpu_has(X86_FEATURE_VAES) ||
	    !boot_cpu_has(X86_FEATURE_VPCLMULQDQ) ||
	    !boot_cpu_has(X86_FEATURE_PCLMULQDQ))
		return 0;
	err = crypto_register_aeads(aes_gcm_algs_vaes_avx2,
				    ARRAY_SIZE(aes_gcm_algs_vaes_avx2));
	if (err)
		return err;

	if (!boot_cpu_has(X86_FEATURE_AVX512BW) ||
	    !boot_cpu_has(X86_FEATURE_AVX512VL) ||
	    !boot_cpu_has(X86_FEATURE_BMI2))
		return 0;

	if (boot_cpu_has(X86_FEATURE_PREFER_YMM)) {
		int i;

		for (i = 0; i < ARRAY_SIZE(aes_gcm_algs_vaes_avx512); i++)
			aes_gcm_algs_vaes_avx512[i].base.cra_priority = 1;
	}

	err = crypto_register_aeads(aes_gcm_algs_vaes_avx512,
				    ARRAY_SIZE(aes_gcm_algs_vaes_avx512));
	if (err)
		return err;

	return 0;
}

#define unregister_aeads(A) \
	if (refcount_read(&(A)[0].base.cra_refcnt) != 0) \
		crypto_unregister_aeads((A), ARRAY_SIZE(A))

static void unregister_avx_algs(void)
{
	unregister_aeads(aes_gcm_algs_aesni_avx);
	unregister_aeads(aes_gcm_algs_vaes_avx2);
	unregister_aeads(aes_gcm_algs_vaes_avx512);
}

static const struct x86_cpu_id aesni_cpu_id[] = {
	X86_MATCH_FEATURE(X86_FEATURE_AES, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, aesni_cpu_id);

static int __init aesni_init(void)
{
	int err;

	if (!x86_match_cpu(aesni_cpu_id))
		return -ENODEV;

	err = crypto_register_aeads(aes_gcm_algs_aesni,
				    ARRAY_SIZE(aes_gcm_algs_aesni));
	if (err)
		return err;

	err = register_avx_algs();
	if (err)
		goto unregister_avx;

	return 0;

unregister_avx:
	unregister_avx_algs();
	crypto_unregister_aeads(aes_gcm_algs_aesni,
				ARRAY_SIZE(aes_gcm_algs_aesni));
	return err;
}

static void __exit aesni_exit(void)
{
	crypto_unregister_aeads(aes_gcm_algs_aesni,
				ARRAY_SIZE(aes_gcm_algs_aesni));
	unregister_avx_algs();
}

module_init(aesni_init);
module_exit(aesni_exit);

MODULE_DESCRIPTION("AES-GCM, optimized with AES-NI or VAES instructions");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("gcm(aes)");
MODULE_ALIAS_CRYPTO("rfc4106(gcm(aes))");
