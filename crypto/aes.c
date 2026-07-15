// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Crypto API support for AES block cipher
 *
 * Copyright 2026 Google LLC
 */

#include <crypto/aes-cbc-macs.h>
#include <crypto/aes-cbc.h>
#include <crypto/aes-ccm.h>
#include <crypto/aes-ctr.h>
#include <crypto/aes-ecb.h>
#include <crypto/aes-gcm.h>
#include <crypto/aes-xts.h>
#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/module.h>

static_assert(__alignof__(struct aes_key) <= CRYPTO_MINALIGN);
static_assert(__alignof__(struct aes_enckey) <= CRYPTO_MINALIGN);

static int crypto_aes_setkey(struct crypto_tfm *tfm, const u8 *in_key,
			     unsigned int key_len)
{
	struct aes_key *key = crypto_tfm_ctx(tfm);

	return aes_preparekey(key, in_key, key_len);
}

static void crypto_aes_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	const struct aes_key *key = crypto_tfm_ctx(tfm);

	aes_encrypt(key, out, in);
}

static void crypto_aes_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	const struct aes_key *key = crypto_tfm_ctx(tfm);

	aes_decrypt(key, out, in);
}

static_assert(__alignof__(struct aes_cmac_key) <= CRYPTO_MINALIGN);
#define AES_CMAC_KEY(tfm) ((struct aes_cmac_key *)crypto_shash_ctx(tfm))
#define AES_CMAC_CTX(desc) ((struct aes_cmac_ctx *)shash_desc_ctx(desc))

static int __maybe_unused crypto_aes_cmac_setkey(struct crypto_shash *tfm,
						 const u8 *in_key,
						 unsigned int key_len)
{
	return aes_cmac_preparekey(AES_CMAC_KEY(tfm), in_key, key_len);
}

static int __maybe_unused crypto_aes_xcbc_setkey(struct crypto_shash *tfm,
						 const u8 *in_key,
						 unsigned int key_len)
{
	if (key_len != AES_KEYSIZE_128)
		return -EINVAL;
	aes_xcbcmac_preparekey(AES_CMAC_KEY(tfm), in_key);
	return 0;
}

static int __maybe_unused crypto_aes_cmac_init(struct shash_desc *desc)
{
	aes_cmac_init(AES_CMAC_CTX(desc), AES_CMAC_KEY(desc->tfm));
	return 0;
}

static int __maybe_unused crypto_aes_cmac_update(struct shash_desc *desc,
						 const u8 *data,
						 unsigned int len)
{
	aes_cmac_update(AES_CMAC_CTX(desc), data, len);
	return 0;
}

static int __maybe_unused crypto_aes_cmac_final(struct shash_desc *desc,
						u8 *out)
{
	aes_cmac_final(AES_CMAC_CTX(desc), out);
	return 0;
}

static int __maybe_unused crypto_aes_cmac_digest(struct shash_desc *desc,
						 const u8 *data,
						 unsigned int len, u8 *out)
{
	aes_cmac(AES_CMAC_KEY(desc->tfm), data, len, out);
	return 0;
}

#define AES_CBCMAC_KEY(tfm) ((struct aes_enckey *)crypto_shash_ctx(tfm))
#define AES_CBCMAC_CTX(desc) ((struct aes_cbcmac_ctx *)shash_desc_ctx(desc))

static int __maybe_unused crypto_aes_cbcmac_setkey(struct crypto_shash *tfm,
						   const u8 *in_key,
						   unsigned int key_len)
{
	return aes_prepareenckey(AES_CBCMAC_KEY(tfm), in_key, key_len);
}

static int __maybe_unused crypto_aes_cbcmac_init(struct shash_desc *desc)
{
	aes_cbcmac_init(AES_CBCMAC_CTX(desc), AES_CBCMAC_KEY(desc->tfm));
	return 0;
}

static int __maybe_unused crypto_aes_cbcmac_update(struct shash_desc *desc,
						   const u8 *data,
						   unsigned int len)
{
	aes_cbcmac_update(AES_CBCMAC_CTX(desc), data, len);
	return 0;
}

static int __maybe_unused crypto_aes_cbcmac_final(struct shash_desc *desc,
						  u8 *out)
{
	aes_cbcmac_final(AES_CBCMAC_CTX(desc), out);
	return 0;
}

static int __maybe_unused crypto_aes_cbcmac_digest(struct shash_desc *desc,
						   const u8 *data,
						   unsigned int len, u8 *out)
{
	aes_cbcmac_init(AES_CBCMAC_CTX(desc), AES_CBCMAC_KEY(desc->tfm));
	aes_cbcmac_update(AES_CBCMAC_CTX(desc), data, len);
	aes_cbcmac_final(AES_CBCMAC_CTX(desc), out);
	return 0;
}

static struct crypto_alg alg = {
	.cra_name = "aes",
	.cra_driver_name = "aes-lib",
	.cra_priority = 100,
	.cra_flags = CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize = AES_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct aes_key),
	.cra_module = THIS_MODULE,
	.cra_u = { .cipher = { .cia_min_keysize = AES_MIN_KEY_SIZE,
			       .cia_max_keysize = AES_MAX_KEY_SIZE,
			       .cia_setkey = crypto_aes_setkey,
			       .cia_encrypt = crypto_aes_encrypt,
			       .cia_decrypt = crypto_aes_decrypt } }
};

static struct shash_alg mac_algs[] = {
#if IS_ENABLED(CONFIG_CRYPTO_CMAC)
	{
		.base.cra_name = "cmac(aes)",
		.base.cra_driver_name = "cmac-aes-lib",
		.base.cra_priority = 300,
		.base.cra_blocksize = AES_BLOCK_SIZE,
		.base.cra_ctxsize = sizeof(struct aes_cmac_key),
		.base.cra_module = THIS_MODULE,
		.digestsize = AES_BLOCK_SIZE,
		.setkey = crypto_aes_cmac_setkey,
		.init = crypto_aes_cmac_init,
		.update = crypto_aes_cmac_update,
		.final = crypto_aes_cmac_final,
		.digest = crypto_aes_cmac_digest,
		.descsize = sizeof(struct aes_cmac_ctx),
	},
#endif
#if IS_ENABLED(CONFIG_CRYPTO_XCBC)
	{
		/*
		 * Note that the only difference between xcbc(aes) and cmac(aes)
		 * is the preparekey function.
		 */
		.base.cra_name = "xcbc(aes)",
		.base.cra_driver_name = "xcbc-aes-lib",
		.base.cra_priority = 300,
		.base.cra_blocksize = AES_BLOCK_SIZE,
		.base.cra_ctxsize = sizeof(struct aes_cmac_key),
		.base.cra_module = THIS_MODULE,
		.digestsize = AES_BLOCK_SIZE,
		.setkey = crypto_aes_xcbc_setkey,
		.init = crypto_aes_cmac_init,
		.update = crypto_aes_cmac_update,
		.final = crypto_aes_cmac_final,
		.digest = crypto_aes_cmac_digest,
		.descsize = sizeof(struct aes_cmac_ctx),
	},
#endif
#if IS_ENABLED(CONFIG_CRYPTO_CCM)
	{
		.base.cra_name = "cbcmac(aes)",
		.base.cra_driver_name = "cbcmac-aes-lib",
		.base.cra_priority = 300,
		.base.cra_blocksize = AES_BLOCK_SIZE,
		.base.cra_ctxsize = sizeof(struct aes_enckey),
		.base.cra_module = THIS_MODULE,
		.digestsize = AES_BLOCK_SIZE,
		.setkey = crypto_aes_cbcmac_setkey,
		.init = crypto_aes_cbcmac_init,
		.update = crypto_aes_cbcmac_update,
		.final = crypto_aes_cbcmac_final,
		.digest = crypto_aes_cbcmac_digest,
		.descsize = sizeof(struct aes_cbcmac_ctx),
	},
#endif
};

static __maybe_unused int
crypto_aes_skcipher_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			   unsigned int key_len)
{
	struct aes_key *key = crypto_skcipher_ctx(tfm);

	return aes_preparekey(key, in_key, key_len);
}

static __maybe_unused int
crypto_aes_skcipher_setenckey(struct crypto_skcipher *tfm, const u8 *in_key,
			      unsigned int key_len)
{
	struct aes_enckey *key = crypto_skcipher_ctx(tfm);

	return aes_prepareenckey(key, in_key, key_len);
}

/*
 * Return true if the request uses only a single scatterlist element and high
 * memory isn't enabled.  This assumes that both scatterlists are non-NULL, i.e.
 * the caller must have handled the cryptlen == 0 case already.
 */
static inline bool
skcipher_request_is_linear_lowmem(const struct skcipher_request *req)
{
	return !IS_ENABLED(CONFIG_HIGHMEM) &&
	       req->dst->length >= req->cryptlen &&
	       req->src->length >= req->cryptlen;
}

/*
 * Call crypt_func() (a function that operates on simple virtual addresses) zero
 * or more times to en/decrypt 'cryptlen' bytes of data from the source
 * scatterlist 'src' and write it into the destination scatterlist 'dst',
 * starting at 'start_pos' bytes into both.
 *
 * This always calls crypt_func() with a length that's a multiple of
 * AES_BLOCK_SIZE, except the last call which includes any remainder.  This is
 * implemented by using an on-stack bounce buffer when necessary.  The current
 * implementation also tries to prefer passing at least 4 blocks, so e.g.
 * scatterlist entries [16,16,16,16] result in a single 64-byte call.
 *
 * The scatterlists must describe either entirely different memory
 * (out-of-place) or entirely the same memory (in-place).  In the latter case,
 * crypt_func() is always called with the source and dest pointers the same.
 */
#define AES_CRYPT_SG(crypt_func, dst, src, cryptlen, start_pos, ...)           \
	({                                                                     \
		unsigned int remaining = (cryptlen);                           \
		unsigned int spos = (start_pos);                               \
                                                                               \
		if (remaining != 0) {                                          \
			struct scatter_walk dst_walk, src_walk;                \
			u8 tmp[4 * AES_BLOCK_SIZE] __aligned(                  \
				__alignof__(long));                            \
                                                                               \
			scatterwalk_start_at_pos(&dst_walk, (dst), spos);      \
			scatterwalk_start_at_pos(&src_walk, (src), spos);      \
			do {                                                   \
				unsigned int dst_avail = scatterwalk_clamp(    \
					&dst_walk, remaining);                 \
				unsigned int src_avail = scatterwalk_clamp(    \
					&src_walk, remaining);                 \
				unsigned int n = min(dst_avail, src_avail);    \
				u8 *dst_virt;                                  \
				const u8 *src_virt;                            \
                                                                               \
				if (n < remaining) {                           \
					if (n < sizeof(tmp)) {                 \
						n = min(remaining,             \
							sizeof(tmp));          \
						memcpy_from_scatterwalk(       \
							tmp, &src_walk, n);    \
						crypt_func(tmp, tmp, n,        \
							   ##__VA_ARGS__);     \
						memcpy_to_scatterwalk(         \
							&dst_walk, tmp, n);    \
						remaining -= n;                \
						continue;                      \
					}                                      \
					n = round_down(n, AES_BLOCK_SIZE);     \
				}                                              \
                                                                               \
				scatterwalk_map(&dst_walk);                    \
				dst_virt = dst_walk.addr;                      \
				if (IS_ENABLED(CONFIG_HIGHMEM) &&              \
				    offset_in_page(src_walk.offset) ==         \
					    offset_in_page(dst_walk.offset) && \
				    sg_page(src_walk.sg) + (src_walk.offset /  \
							    PAGE_SIZE) ==      \
					    sg_page(dst_walk.sg) +             \
						    (dst_walk.offset /         \
						     PAGE_SIZE)) {             \
					src_virt = dst_virt;                   \
				} else {                                       \
					scatterwalk_map(&src_walk);            \
					src_virt = src_walk.addr;              \
				}                                              \
				crypt_func(dst_virt, src_virt, n,              \
					   ##__VA_ARGS__);                     \
				if (src_virt != dst_virt)                      \
					scatterwalk_unmap(&src_walk);          \
				scatterwalk_advance(&src_walk, n);             \
				scatterwalk_done_dst(&dst_walk, n);            \
				remaining -= n;                                \
			} while (remaining);                                   \
			memzero_explicit(tmp, sizeof(tmp));                    \
		}                                                              \
	})

/*
 * Call ad_func() as needed to process the associated data in the first
 * 'assoclen' bytes of the scatterlist 'src'.
 */
#define AES_PROCESS_ASSOC_DATA(ad_func, src, assoclen, ctx)                 \
	({                                                                  \
		unsigned int remaining = (assoclen);                        \
                                                                            \
		if (remaining != 0) {                                       \
			struct scatter_walk walk;                           \
                                                                            \
			scatterwalk_start(&walk, (src));                    \
			do {                                                \
				unsigned int n =                            \
					scatterwalk_next(&walk, remaining); \
                                                                            \
				ad_func((ctx), walk.addr, n);               \
				scatterwalk_done_src(&walk, n);             \
				remaining -= n;                             \
			} while (remaining);                                \
		}                                                           \
	})

/* AES-ECB */

static __maybe_unused int crypto_aes_ecb_encrypt(struct skcipher_request *req)
{
	const struct aes_key *key =
		crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));

	if (unlikely(req->cryptlen % AES_BLOCK_SIZE))
		return -EINVAL;
	AES_CRYPT_SG(aes_ecb_encrypt, req->dst, req->src, req->cryptlen, 0,
		     key);
	return 0;
}

static __maybe_unused int crypto_aes_ecb_decrypt(struct skcipher_request *req)
{
	const struct aes_key *key =
		crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));

	if (unlikely(req->cryptlen % AES_BLOCK_SIZE))
		return -EINVAL;
	AES_CRYPT_SG(aes_ecb_decrypt, req->dst, req->src, req->cryptlen, 0,
		     key);
	return 0;
}

/* AES-CBC */

static void crypto_aes_cbc_encrypt_sg(struct skcipher_request *req,
				      unsigned int cryptlen,
				      const struct aes_key *key)
{
	AES_CRYPT_SG(aes_cbc_encrypt, req->dst, req->src, cryptlen, 0, req->iv,
		     key);
}

static void crypto_aes_cbc_decrypt_sg(struct skcipher_request *req,
				      unsigned int cryptlen,
				      const struct aes_key *key)
{
	AES_CRYPT_SG(aes_cbc_decrypt, req->dst, req->src, cryptlen, 0, req->iv,
		     key);
}

static __maybe_unused int crypto_aes_cbc_encrypt(struct skcipher_request *req)
{
	const struct aes_key *key =
		crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));

	if (unlikely(req->cryptlen % AES_BLOCK_SIZE))
		return -EINVAL;
	crypto_aes_cbc_encrypt_sg(req, req->cryptlen, key);
	return 0;
}

static __maybe_unused int crypto_aes_cbc_decrypt(struct skcipher_request *req)
{
	const struct aes_key *key =
		crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));

	if (unlikely(req->cryptlen % AES_BLOCK_SIZE))
		return -EINVAL;
	crypto_aes_cbc_decrypt_sg(req, req->cryptlen, key);
	return 0;
}

/* AES-CBC-CTS */

/*
 * This handles AES-CBC-CTS en/decryption requests that use a nonlinear
 * scatterlist layout or where HIGHMEM is enabled.  It is explicitly 'noinline'
 * to keep the temporary buffer out of the stack frame of the fast path.
 */
static noinline int
crypto_aes_cbc_cts_crypt_nonlinear(struct skcipher_request *req, bool enc)
{
	const struct aes_key *key =
		crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	unsigned int main_len = req->cryptlen;
	unsigned int tail_len;
	u8 tmp[2 * AES_BLOCK_SIZE] __aligned(__alignof__(long));

	if (main_len == AES_BLOCK_SIZE) {
		/* Single block is a special case that just does CBC. */
		if (enc)
			crypto_aes_cbc_encrypt_sg(req, main_len, key);
		else
			crypto_aes_cbc_decrypt_sg(req, main_len, key);
		return 0;
	}
	/* Just do the last two blocks separately. */
	tail_len = AES_BLOCK_SIZE + ((main_len - 1) % AES_BLOCK_SIZE) + 1;
	main_len -= tail_len;
	if (enc)
		crypto_aes_cbc_encrypt_sg(req, main_len, key);
	else
		crypto_aes_cbc_decrypt_sg(req, main_len, key);
	memcpy_from_sglist(tmp, req->src, main_len, tail_len);
	if (enc)
		aes_cbc_cts_encrypt(tmp, tmp, tail_len, req->iv, key);
	else
		aes_cbc_cts_decrypt(tmp, tmp, tail_len, req->iv, key);
	memcpy_to_sglist(req->dst, main_len, tmp, tail_len);
	memzero_explicit(tmp, sizeof(tmp));
	return 0;
}

static __maybe_unused int
crypto_aes_cbc_cts_encrypt(struct skcipher_request *req)
{
	const struct aes_key *key =
		crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));

	if (unlikely(req->cryptlen < AES_BLOCK_SIZE))
		return -EINVAL;
	if (likely(skcipher_request_is_linear_lowmem(req))) {
		/* Fast path */
		aes_cbc_cts_encrypt(sg_virt(req->dst), sg_virt(req->src),
				    req->cryptlen, req->iv, key);
		return 0;
	}
	return crypto_aes_cbc_cts_crypt_nonlinear(req, /* enc= */ true);
}

static __maybe_unused int
crypto_aes_cbc_cts_decrypt(struct skcipher_request *req)
{
	const struct aes_key *key =
		crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));

	if (unlikely(req->cryptlen < AES_BLOCK_SIZE))
		return -EINVAL;
	if (likely(skcipher_request_is_linear_lowmem(req))) {
		/* Fast path */
		aes_cbc_cts_decrypt(sg_virt(req->dst), sg_virt(req->src),
				    req->cryptlen, req->iv, key);
		return 0;
	}
	return crypto_aes_cbc_cts_crypt_nonlinear(req, /* enc= */ false);
}

/* AES-CTR */

static __maybe_unused int crypto_aes_ctr_crypt(struct skcipher_request *req)
{
	const struct aes_enckey *key =
		crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));

	AES_CRYPT_SG(aes_ctr, req->dst, req->src, req->cryptlen, 0, req->iv,
		     key);
	return 0;
}

/* AES-XCTR */

static __maybe_unused int crypto_aes_xctr_crypt(struct skcipher_request *req)
{
	const struct aes_enckey *key =
		crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	u64 ctr = 1;

	AES_CRYPT_SG(aes_xctr, req->dst, req->src, req->cryptlen, 0, &ctr,
		     req->iv, key);
	return 0;
}

/* AES-XTS */

static __maybe_unused int crypto_aes_xts_setkey(struct crypto_skcipher *tfm,
						const u8 *in_key,
						unsigned int key_len)
{
	struct aes_xts_key *key = crypto_skcipher_ctx(tfm);
	int flags = (crypto_skcipher_get_flags(tfm) &
		     CRYPTO_TFM_REQ_FORBID_WEAK_KEYS) ?
			    XTS_FORBID_WEAK_KEYS :
			    0;

	return aes_xts_preparekey(key, in_key, key_len, flags);
}

static void aes_xts_crypt_wrapper(u8 *dst, const u8 *src, size_t len,
				  u8 iv[AES_BLOCK_SIZE],
				  const struct aes_xts_key *key, bool enc,
				  bool *cont)
{
	if (enc)
		aes_xts_encrypt(dst, src, len, iv, key, *cont);
	else
		aes_xts_decrypt(dst, src, len, iv, key, *cont);
	*cont = true;
}

/*
 * This handles AES-XTS en/decryption requests that use a nonlinear scatterlist
 * layout or where HIGHMEM is enabled.  It is explicitly 'noinline' to keep the
 * temporary buffer out of the stack frame of the fast path.
 */
static noinline int crypto_aes_xts_crypt_nonlinear(struct skcipher_request *req,
						   bool enc)
{
	const struct aes_xts_key *key =
		crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	u8 tmp[2 * AES_BLOCK_SIZE] __aligned(__alignof__(long));
	unsigned int main_len = req->cryptlen;
	unsigned int tail_len = main_len % AES_BLOCK_SIZE;
	bool cont = false;

	if (unlikely(tail_len)) {
		/*
		 * Ciphertext stealing is needed.
		 * Just do the last two blocks separately.
		 */
		tail_len += AES_BLOCK_SIZE;
		main_len -= tail_len;
	}

	AES_CRYPT_SG(aes_xts_crypt_wrapper, req->dst, req->src, main_len, 0,
		     req->iv, key, enc, &cont);

	if (unlikely(tail_len)) {
		memcpy_from_sglist(tmp, req->src, main_len, tail_len);
		aes_xts_crypt_wrapper(tmp, tmp, tail_len, req->iv, key, enc,
				      &cont);
		memcpy_to_sglist(req->dst, main_len, tmp, tail_len);
		memzero_explicit(tmp, sizeof(tmp));
	}
	return 0;
}

static __maybe_unused int crypto_aes_xts_encrypt(struct skcipher_request *req)
{
	const struct aes_xts_key *key =
		crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));

	if (unlikely(req->cryptlen < AES_BLOCK_SIZE))
		return -EINVAL;
	if (likely(skcipher_request_is_linear_lowmem(req))) {
		/* Fast path */
		aes_xts_encrypt(sg_virt(req->dst), sg_virt(req->src),
				req->cryptlen, req->iv, key, /* cont= */ false);
		return 0;
	}
	return crypto_aes_xts_crypt_nonlinear(req, /* enc= */ true);
}

static __maybe_unused int crypto_aes_xts_decrypt(struct skcipher_request *req)
{
	const struct aes_xts_key *key =
		crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));

	if (unlikely(req->cryptlen < AES_BLOCK_SIZE))
		return -EINVAL;
	if (likely(skcipher_request_is_linear_lowmem(req))) {
		/* Fast path */
		aes_xts_decrypt(sg_virt(req->dst), sg_virt(req->src),
				req->cryptlen, req->iv, key, /* cont= */ false);
		return 0;
	}
	return crypto_aes_xts_crypt_nonlinear(req, /* enc= */ false);
}

static struct skcipher_alg skcipher_algs[] = {
#if IS_ENABLED(CONFIG_CRYPTO_ECB)
	{
		.base.cra_name = "ecb(aes)",
		.base.cra_driver_name = "ecb-aes-lib",
		.base.cra_priority = 110,
		.base.cra_blocksize = AES_BLOCK_SIZE,
		.base.cra_ctxsize = sizeof(struct aes_key),
		.base.cra_module = THIS_MODULE,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.setkey = crypto_aes_skcipher_setkey,
		.encrypt = crypto_aes_ecb_encrypt,
		.decrypt = crypto_aes_ecb_decrypt,
	},
#endif
#if IS_ENABLED(CONFIG_CRYPTO_CBC)
	{
		.base.cra_name = "cbc(aes)",
		.base.cra_driver_name = "cbc-aes-lib",
		.base.cra_priority = 110,
		.base.cra_blocksize = AES_BLOCK_SIZE,
		.base.cra_ctxsize = sizeof(struct aes_key),
		.base.cra_module = THIS_MODULE,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.setkey = crypto_aes_skcipher_setkey,
		.encrypt = crypto_aes_cbc_encrypt,
		.decrypt = crypto_aes_cbc_decrypt,
	},
#endif
#if IS_ENABLED(CONFIG_CRYPTO_CTS)
	{
		.base.cra_name = "cts(cbc(aes))",
		.base.cra_driver_name = "cts-cbc-aes-lib",
		.base.cra_priority = 110,
		.base.cra_blocksize = AES_BLOCK_SIZE,
		.base.cra_ctxsize = sizeof(struct aes_key),
		.base.cra_module = THIS_MODULE,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.setkey = crypto_aes_skcipher_setkey,
		.encrypt = crypto_aes_cbc_cts_encrypt,
		.decrypt = crypto_aes_cbc_cts_decrypt,
	},
#endif
#if IS_ENABLED(CONFIG_CRYPTO_CTR)
	{
		.base.cra_name = "ctr(aes)",
		.base.cra_driver_name = "ctr-aes-lib",
		.base.cra_priority = 110,
		.base.cra_blocksize = 1,
		.base.cra_ctxsize = sizeof(struct aes_enckey),
		.base.cra_module = THIS_MODULE,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.chunksize = AES_BLOCK_SIZE,
		.setkey = crypto_aes_skcipher_setenckey,
		.encrypt = crypto_aes_ctr_crypt,
		.decrypt = crypto_aes_ctr_crypt,
	},
#endif
#if IS_ENABLED(CONFIG_CRYPTO_XCTR)
	{
		.base.cra_name = "xctr(aes)",
		.base.cra_driver_name = "xctr-aes-lib",
		.base.cra_priority = 110,
		.base.cra_blocksize = 1,
		.base.cra_ctxsize = sizeof(struct aes_enckey),
		.base.cra_module = THIS_MODULE,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.chunksize = AES_BLOCK_SIZE,
		.setkey = crypto_aes_skcipher_setenckey,
		.encrypt = crypto_aes_xctr_crypt,
		.decrypt = crypto_aes_xctr_crypt,
	},
#endif
#if IS_ENABLED(CONFIG_CRYPTO_XTS)
	{
		.base.cra_name = "xts(aes)",
		.base.cra_driver_name = "xts-aes-lib",
		.base.cra_priority = 110,
		.base.cra_blocksize = AES_BLOCK_SIZE,
		.base.cra_ctxsize = sizeof(struct aes_xts_key),
		.base.cra_module = THIS_MODULE,
		.min_keysize = 2 * AES_MIN_KEY_SIZE,
		.max_keysize = 2 * AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.setkey = crypto_aes_xts_setkey,
		.encrypt = crypto_aes_xts_encrypt,
		.decrypt = crypto_aes_xts_decrypt,
	},
#endif
};

/* AES-GCM */

static __maybe_unused int crypto_aes_gcm_setkey(struct crypto_aead *tfm,
						const u8 *in_key,
						unsigned int key_len)
{
	struct aes_gcm_key *key = crypto_aead_ctx(tfm);

	return aes_gcm_preparekey(key, in_key, key_len,
				  crypto_aead_authsize(tfm));
}

static __maybe_unused int crypto_aes_gcm_setauthsize(struct crypto_aead *tfm,
						     unsigned int authsize)
{
	struct aes_gcm_key *key = crypto_aead_ctx(tfm);

	if (crypto_gcm_check_authsize(authsize) != 0)
		return -EINVAL;
	/* Synchronize the tag length to the struct aes_gcm_key. */
	key->authtag_len = authsize;
	return 0;
}

static void crypto_aes_gcm_auth_update(struct aes_gcm_ctx *ctx,
				       struct scatterlist *src,
				       unsigned int assoclen)
{
	AES_PROCESS_ASSOC_DATA(aes_gcm_auth_update, src, assoclen, ctx);
}

static void aes_gcm_encrypt_update_helper(u8 *dst, const u8 *src,
					  unsigned int len,
					  struct aes_gcm_ctx *ctx)
{
	aes_gcm_encrypt_update(ctx, dst, src, len);
}

static void aes_gcm_decrypt_update_helper(u8 *dst, const u8 *src,
					  unsigned int len,
					  struct aes_gcm_ctx *ctx)
{
	aes_gcm_decrypt_update(ctx, dst, src, len);
}

static int crypto_aes_gcm_encrypt_common(struct aead_request *req,
					 const struct aes_gcm_key *key,
					 u8 iv[12], unsigned int assoclen)
{
	struct aes_gcm_ctx ctx;
	u8 authtag[16];

	aes_gcm_init(&ctx, iv, key);
	crypto_aes_gcm_auth_update(&ctx, req->src, assoclen);
	AES_CRYPT_SG(aes_gcm_encrypt_update_helper, req->dst, req->src,
		     req->cryptlen, req->assoclen, &ctx);
	aes_gcm_encrypt_final(&ctx, authtag);
	memcpy_to_sglist(req->dst, req->assoclen + req->cryptlen, authtag,
			 key->authtag_len);
	memzero_explicit(authtag, sizeof(authtag));
	return 0;
}

static int crypto_aes_gcm_decrypt_common(struct aead_request *req,
					 const struct aes_gcm_key *key,
					 u8 iv[12], unsigned int assoclen)
{
	struct aes_gcm_ctx ctx;
	unsigned int data_len;
	u8 authtag[16];
	int err;

	aes_gcm_init(&ctx, iv, key);
	crypto_aes_gcm_auth_update(&ctx, req->src, assoclen);

	/* crypto_aead_decrypt() already checked cryptlen >= authtag_len. */
	data_len = req->cryptlen - key->authtag_len;
	AES_CRYPT_SG(aes_gcm_decrypt_update_helper, req->dst, req->src,
		     data_len, req->assoclen, &ctx);

	memcpy_from_sglist(authtag, req->src, req->assoclen + data_len,
			   key->authtag_len);
	err = aes_gcm_decrypt_final(&ctx, authtag);
	memzero_explicit(authtag, sizeof(authtag));
	return err;
}

static __maybe_unused int crypto_aes_gcm_encrypt(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	const struct aes_gcm_key *key = crypto_aead_ctx(tfm);

	return crypto_aes_gcm_encrypt_common(req, key, req->iv, req->assoclen);
}

static __maybe_unused int crypto_aes_gcm_decrypt(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	const struct aes_gcm_key *key = crypto_aead_ctx(tfm);

	return crypto_aes_gcm_decrypt_common(req, key, req->iv, req->assoclen);
}

struct aes_rfc4106_key {
	struct aes_gcm_key gcm;
	u8 nonce[4];
};

static __maybe_unused int crypto_aes_rfc4106_setkey(struct crypto_aead *tfm,
						    const u8 *in_key,
						    unsigned int key_len)
{
	struct aes_rfc4106_key *key = crypto_aead_ctx(tfm);

	if (key_len < 4)
		return -EINVAL;

	key_len -= 4;
	memcpy(key->nonce, in_key + key_len, 4);

	return aes_gcm_preparekey(&key->gcm, in_key, key_len,
				  crypto_aead_authsize(tfm));
}

static __maybe_unused int
crypto_aes_rfc4106_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	struct aes_rfc4106_key *key = crypto_aead_ctx(tfm);

	if (crypto_rfc4106_check_authsize(authsize) != 0)
		return -EINVAL;

	/* Synchronize the tag length to the struct aes_gcm_key. */
	key->gcm.authtag_len = authsize;
	return 0;
}

static __maybe_unused int crypto_aes_rfc4106_encrypt(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	const struct aes_rfc4106_key *key = crypto_aead_ctx(tfm);
	u8 iv[12];

	if (crypto_ipsec_check_assoclen(req->assoclen) != 0)
		return -EINVAL;
	memcpy(iv, key->nonce, 4);
	memcpy(&iv[4], req->iv, 8);

	return crypto_aes_gcm_encrypt_common(req, &key->gcm, iv,
					     req->assoclen - 8);
}

static __maybe_unused int crypto_aes_rfc4106_decrypt(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	const struct aes_rfc4106_key *key = crypto_aead_ctx(tfm);
	u8 iv[12];

	if (crypto_ipsec_check_assoclen(req->assoclen) != 0)
		return -EINVAL;
	memcpy(iv, key->nonce, 4);
	memcpy(&iv[4], req->iv, 8);

	return crypto_aes_gcm_decrypt_common(req, &key->gcm, iv,
					     req->assoclen - 8);
}

/* AES-CCM */

static __maybe_unused int crypto_aes_ccm_setkey(struct crypto_aead *tfm,
						const u8 *in_key,
						unsigned int key_len)
{
	struct aes_ccm_key *key = crypto_aead_ctx(tfm);

	return aes_ccm_preparekey(key, in_key, key_len,
				  crypto_aead_authsize(tfm));
}

static __maybe_unused int crypto_aes_ccm_setauthsize(struct crypto_aead *tfm,
						     unsigned int authsize)
{
	struct aes_ccm_key *key = crypto_aead_ctx(tfm);

	if (authsize < 4 || authsize > 16 || authsize % 2)
		return -EINVAL;
	/* Synchronize the tag length to the struct aes_ccm_key. */
	key->authtag_len = authsize;
	return 0;
}

static int crypto_aes_ccm_init(struct aes_ccm_ctx *ctx,
			       struct aead_request *req, unsigned int data_len,
			       const struct aes_ccm_key *key)
{
	int nonce_len;
	const u8 *nonce;
	int err;

	/*
	 * CCM accepts a variable-length nonce between 7 and 13 bytes
	 * inclusively, while crypto_aead assumes a fixed-length IV.  This is
	 * worked around by requiring that iv[0] contain '14 - nonce_len' and
	 * iv[1..] contain the actual nonce.  Extra bytes at the end are unused.
	 */
	nonce_len = 14 - (int)req->iv[0];
	if (unlikely(nonce_len < 7 || nonce_len > 13))
		return -EINVAL;
	nonce = &req->iv[1];
	err = aes_ccm_init(ctx, data_len, req->assoclen, nonce, nonce_len, key);
	if (unlikely(err))
		return err;
	AES_PROCESS_ASSOC_DATA(aes_ccm_auth_update, req->src, req->assoclen,
			       ctx);
	return 0;
}

static void aes_ccm_encrypt_update_helper(u8 *dst, const u8 *src,
					  unsigned int len,
					  struct aes_ccm_ctx *ctx)
{
	aes_ccm_encrypt_update(ctx, dst, src, len);
}

static void aes_ccm_decrypt_update_helper(u8 *dst, const u8 *src,
					  unsigned int len,
					  struct aes_ccm_ctx *ctx)
{
	aes_ccm_decrypt_update(ctx, dst, src, len);
}

static __maybe_unused int crypto_aes_ccm_encrypt(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	const struct aes_ccm_key *key = crypto_aead_ctx(tfm);
	struct aes_ccm_ctx ctx;
	u8 authtag[16];
	int err;

	err = crypto_aes_ccm_init(&ctx, req, req->cryptlen, key);
	if (unlikely(err))
		return err;
	AES_CRYPT_SG(aes_ccm_encrypt_update_helper, req->dst, req->src,
		     req->cryptlen, req->assoclen, &ctx);
	aes_ccm_encrypt_final(&ctx, authtag);
	memcpy_to_sglist(req->dst, req->assoclen + req->cryptlen, authtag,
			 key->authtag_len);
	memzero_explicit(authtag, sizeof(authtag));
	return 0;
}

static __maybe_unused int crypto_aes_ccm_decrypt(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	const struct aes_ccm_key *key = crypto_aead_ctx(tfm);
	unsigned int data_len;
	struct aes_ccm_ctx ctx;
	u8 authtag[16];
	int err;

	/* crypto_aead_decrypt() already checked cryptlen >= authtag_len. */
	data_len = req->cryptlen - key->authtag_len;
	err = crypto_aes_ccm_init(&ctx, req, data_len, key);
	if (unlikely(err))
		return err;
	AES_CRYPT_SG(aes_ccm_decrypt_update_helper, req->dst, req->src,
		     data_len, req->assoclen, &ctx);
	memcpy_from_sglist(authtag, req->src, req->assoclen + data_len,
			   key->authtag_len);
	err = aes_ccm_decrypt_final(&ctx, authtag);
	memzero_explicit(authtag, sizeof(authtag));
	return err;
}

static struct aead_alg aead_algs[] = {
#if IS_ENABLED(CONFIG_CRYPTO_GCM)
	{
		.base.cra_name = "gcm(aes)",
		.base.cra_driver_name = "gcm-aes-lib",
		.base.cra_priority = 110,
		.base.cra_blocksize = 1,
		.base.cra_ctxsize = sizeof(struct aes_gcm_key),
		.base.cra_module = THIS_MODULE,
		.setkey = crypto_aes_gcm_setkey,
		.setauthsize = crypto_aes_gcm_setauthsize,
		.encrypt = crypto_aes_gcm_encrypt,
		.decrypt = crypto_aes_gcm_decrypt,
		.ivsize = GCM_AES_IV_SIZE,
		.maxauthsize = AES_BLOCK_SIZE,
		.chunksize = AES_BLOCK_SIZE,
	},
	{
		.base.cra_name = "rfc4106(gcm(aes))",
		.base.cra_driver_name = "rfc4106-gcm-aes-lib",
		.base.cra_priority = 110,
		.base.cra_blocksize = 1,
		.base.cra_ctxsize = sizeof(struct aes_rfc4106_key),
		.base.cra_module = THIS_MODULE,
		.setkey = crypto_aes_rfc4106_setkey,
		.setauthsize = crypto_aes_rfc4106_setauthsize,
		.encrypt = crypto_aes_rfc4106_encrypt,
		.decrypt = crypto_aes_rfc4106_decrypt,
		.ivsize = GCM_RFC4106_IV_SIZE,
		.maxauthsize = AES_BLOCK_SIZE,
		.chunksize = AES_BLOCK_SIZE,
	},
#endif /* CONFIG_CRYPTO_GCM */
#if IS_ENABLED(CONFIG_CRYPTO_CCM)
	{
		.base.cra_name = "ccm(aes)",
		.base.cra_driver_name = "ccm-aes-lib",
		.base.cra_priority = 110,
		.base.cra_blocksize = 1,
		.base.cra_ctxsize = sizeof(struct aes_ccm_key),
		.base.cra_module = THIS_MODULE,
		.setkey = crypto_aes_ccm_setkey,
		.setauthsize = crypto_aes_ccm_setauthsize,
		.encrypt = crypto_aes_ccm_encrypt,
		.decrypt = crypto_aes_ccm_decrypt,
		.ivsize = 16,
		.maxauthsize = 16,
		.chunksize = AES_BLOCK_SIZE,
	},
#endif /* CONFIG_CRYPTO_CCM */
};

static int __init crypto_aes_mod_init(void)
{
	int err = crypto_register_alg(&alg);

	if (err)
		return err;

	if (ARRAY_SIZE(mac_algs) > 0) {
		err = crypto_register_shashes(mac_algs, ARRAY_SIZE(mac_algs));
		if (err)
			goto err_unregister_alg;
	} /* Else, CONFIG_CRYPTO_HASH might not be enabled. */

	if (ARRAY_SIZE(skcipher_algs) > 0) {
		err = crypto_register_skciphers(skcipher_algs,
						ARRAY_SIZE(skcipher_algs));
		if (err)
			goto err_unregister_macs;
	}

	if (ARRAY_SIZE(aead_algs) > 0) {
		err = crypto_register_aeads(aead_algs, ARRAY_SIZE(aead_algs));
		if (err)
			goto err_unregister_skciphers;
	} /* Else, CONFIG_CRYPTO_AEAD might not be enabled. */
	return 0;

err_unregister_skciphers:
	if (ARRAY_SIZE(skcipher_algs) > 0)
		crypto_unregister_skciphers(skcipher_algs,
					    ARRAY_SIZE(skcipher_algs));
err_unregister_macs:
	if (ARRAY_SIZE(mac_algs) > 0)
		crypto_unregister_shashes(mac_algs, ARRAY_SIZE(mac_algs));
err_unregister_alg:
	crypto_unregister_alg(&alg);
	return err;
}
module_init(crypto_aes_mod_init);

static void __exit crypto_aes_mod_exit(void)
{
	if (ARRAY_SIZE(aead_algs) > 0)
		crypto_unregister_aeads(aead_algs, ARRAY_SIZE(aead_algs));
	if (ARRAY_SIZE(skcipher_algs) > 0)
		crypto_unregister_skciphers(skcipher_algs,
					    ARRAY_SIZE(skcipher_algs));
	if (ARRAY_SIZE(mac_algs) > 0)
		crypto_unregister_shashes(mac_algs, ARRAY_SIZE(mac_algs));
	crypto_unregister_alg(&alg);
}
module_exit(crypto_aes_mod_exit);

MODULE_DESCRIPTION("Crypto API support for AES block cipher");
MODULE_IMPORT_NS("CRYPTO_INTERNAL");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("aes");
MODULE_ALIAS_CRYPTO("aes-lib");
#if IS_ENABLED(CONFIG_CRYPTO_CMAC)
MODULE_ALIAS_CRYPTO("cmac(aes)");
MODULE_ALIAS_CRYPTO("cmac-aes-lib");
#endif
#if IS_ENABLED(CONFIG_CRYPTO_XCBC)
MODULE_ALIAS_CRYPTO("xcbc(aes)");
MODULE_ALIAS_CRYPTO("xcbc-aes-lib");
#endif
#if IS_ENABLED(CONFIG_CRYPTO_CCM)
MODULE_ALIAS_CRYPTO("cbcmac(aes)");
MODULE_ALIAS_CRYPTO("cbcmac-aes-lib");
#endif
#if IS_ENABLED(CONFIG_CRYPTO_ECB)
MODULE_ALIAS_CRYPTO("ecb(aes)");
MODULE_ALIAS_CRYPTO("ecb-aes-lib");
#endif
#if IS_ENABLED(CONFIG_CRYPTO_CBC)
MODULE_ALIAS_CRYPTO("cbc(aes)");
MODULE_ALIAS_CRYPTO("cbc-aes-lib");
#endif
#if IS_ENABLED(CONFIG_CRYPTO_CTS)
MODULE_ALIAS_CRYPTO("cts(cbc(aes))");
MODULE_ALIAS_CRYPTO("cts-cbc-aes-lib");
#endif
#if IS_ENABLED(CONFIG_CRYPTO_CTR)
MODULE_ALIAS_CRYPTO("ctr(aes)");
MODULE_ALIAS_CRYPTO("ctr-aes-lib");
#endif
#if IS_ENABLED(CONFIG_CRYPTO_XCTR)
MODULE_ALIAS_CRYPTO("xctr(aes)");
MODULE_ALIAS_CRYPTO("xctr-aes-lib");
#endif
#if IS_ENABLED(CONFIG_CRYPTO_XTS)
MODULE_ALIAS_CRYPTO("xts(aes)");
MODULE_ALIAS_CRYPTO("xts-aes-lib");
#endif
#if IS_ENABLED(CONFIG_CRYPTO_GCM)
MODULE_ALIAS_CRYPTO("gcm(aes)");
MODULE_ALIAS_CRYPTO("gcm-aes-lib");
MODULE_ALIAS_CRYPTO("rfc4106(gcm(aes))");
MODULE_ALIAS_CRYPTO("rfc4106-gcm-aes-lib");
#endif
#if IS_ENABLED(CONFIG_CRYPTO_CCM)
MODULE_ALIAS_CRYPTO("ccm(aes)");
MODULE_ALIAS_CRYPTO("ccm-aes-lib");
#endif
