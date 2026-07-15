// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Crypto API support for AES block cipher
 *
 * Copyright 2026 Google LLC
 */

#include <crypto/aes-cbc-macs.h>
#include <crypto/aes-cbc.h>
#include <crypto/aes-ctr.h>
#include <crypto/aes-ecb.h>
#include <crypto/aes.h>
#include <crypto/algapi.h>
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
	return 0;

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
