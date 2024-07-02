// SPDX-License-Identifier: GPL-2.0-only
/*
 * AES using the RISC-V vector crypto extensions.  Includes the bare block
 * cipher and the ECB, CBC, CBC-CTS, CTR, and XTS modes.
 *
 * Copyright (C) 2023 VRULL GmbH
 * Author: Heiko Stuebner <heiko.stuebner@vrull.eu>
 *
 * Copyright (C) 2023 SiFive, Inc.
 * Author: Jerry Shih <jerry.shih@sifive.com>
 *
 * Copyright 2024 Google LLC
 */

#include <asm/simd.h>
#include <asm/vector.h>
#include <crypto/aes.h>
#include <crypto/internal/cipher.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <crypto/xts.h>
#include <linux/linkage.h>
#include <linux/module.h>

asmlinkage void aes_encrypt_zvkned(const struct crypto_aes_ctx *key,
				   const u8 in[AES_BLOCK_SIZE],
				   u8 out[AES_BLOCK_SIZE]);
asmlinkage void aes_decrypt_zvkned(const struct crypto_aes_ctx *key,
				   const u8 in[AES_BLOCK_SIZE],
				   u8 out[AES_BLOCK_SIZE]);

asmlinkage void aes_ecb_encrypt_zvkned(const struct crypto_aes_ctx *key,
				       const u8 *in, u8 *out, size_t len);
asmlinkage void aes_ecb_decrypt_zvkned(const struct crypto_aes_ctx *key,
				       const u8 *in, u8 *out, size_t len);

asmlinkage void aes_cbc_encrypt_zvkned(const struct crypto_aes_ctx *key,
				       const u8 *in, u8 *out, size_t len,
				       u8 iv[AES_BLOCK_SIZE]);
asmlinkage void aes_cbc_decrypt_zvkned(const struct crypto_aes_ctx *key,
				       const u8 *in, u8 *out, size_t len,
				       u8 iv[AES_BLOCK_SIZE]);

asmlinkage void aes_cbc_cts_crypt_zvkned(const struct crypto_aes_ctx *key,
					 const u8 *in, u8 *out, size_t len,
					 const u8 iv[AES_BLOCK_SIZE], bool enc);

asmlinkage void aes_ctr32_crypt_zvkned_zvkb(const struct crypto_aes_ctx *key,
					    const u8 *in, u8 *out, size_t len,
					    u8 iv[AES_BLOCK_SIZE]);

asmlinkage void aes_xts_encrypt_zvkned_zvbb_zvkg(
			const struct crypto_aes_ctx *key,
			const u8 *in, u8 *out, size_t len,
			u8 tweak[AES_BLOCK_SIZE]);

asmlinkage void aes_xts_decrypt_zvkned_zvbb_zvkg(
			const struct crypto_aes_ctx *key,
			const u8 *in, u8 *out, size_t len,
			u8 tweak[AES_BLOCK_SIZE]);

static int riscv64_aes_setkey(struct crypto_aes_ctx *ctx,
			      const u8 *key, unsigned int keylen)
{
	/*
	 * For now we just use the generic key expansion, for these reasons:
	 *
	 * - zvkned's key expansion instructions don't support AES-192.
	 *   So, non-zvkned fallback code would be needed anyway.
	 *
	 * - Users of AES in Linux usually don't change keys frequently.
	 *   So, key expansion isn't performance-critical.
	 *
	 * - For single-block AES exposed as a "cipher" algorithm, it's
	 *   necessary to use struct crypto_aes_ctx and initialize its 'key_dec'
	 *   field with the round keys for the Equivalent Inverse Cipher.  This
	 *   is because with "cipher", decryption can be requested from a
	 *   context where the vector unit isn't usable, necessitating a
	 *   fallback to aes_decrypt().  But, zvkned can only generate and use
	 *   the normal round keys.  Of course, it's preferable to not have
	 *   special code just for "cipher", as e.g. XTS also uses a
	 *   single-block AES encryption.  It's simplest to just use
	 *   struct crypto_aes_ctx and aes_expandkey() everywhere.
	 */
	return aes_expandkey(ctx, key, keylen);
}

static int riscv64_aes_setkey_cipher(struct crypto_tfm *tfm,
				     const u8 *key, unsigned int keylen)
{
	struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	return riscv64_aes_setkey(ctx, key, keylen);
}

static int riscv64_aes_setkey_skcipher(struct crypto_skcipher *tfm,
				       const u8 *key, unsigned int keylen)
{
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);

	return riscv64_aes_setkey(ctx, key, keylen);
}

/* Bare AES, without a mode of operation */

static void riscv64_aes_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	const struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	if (crypto_simd_usable()) {
		kernel_vector_begin();
		aes_encrypt_zvkned(ctx, src, dst);
		kernel_vector_end();
	} else {
		aes_encrypt(ctx, dst, src);
	}
}

static void riscv64_aes_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	const struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	if (crypto_simd_usable()) {
		kernel_vector_begin();
		aes_decrypt_zvkned(ctx, src, dst);
		kernel_vector_end();
	} else {
		aes_decrypt(ctx, dst, src);
	}
}

/* AES-ECB */

static inline int riscv64_aes_ecb_crypt(struct skcipher_request *req, bool enc)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);
	while ((nbytes = walk.nbytes) != 0) {
		kernel_vector_begin();
		if (enc)
			aes_ecb_encrypt_zvkned(ctx, walk.src.virt.addr,
					       walk.dst.virt.addr,
					       nbytes & ~(AES_BLOCK_SIZE - 1));
		else
			aes_ecb_decrypt_zvkned(ctx, walk.src.virt.addr,
					       walk.dst.virt.addr,
					       nbytes & ~(AES_BLOCK_SIZE - 1));
		kernel_vector_end();
		err = skcipher_walk_done(&walk, nbytes & (AES_BLOCK_SIZE - 1));
	}

	return err;
}

static int riscv64_aes_ecb_encrypt(struct skcipher_request *req)
{
	return riscv64_aes_ecb_crypt(req, true);
}

static int riscv64_aes_ecb_decrypt(struct skcipher_request *req)
{
	return riscv64_aes_ecb_crypt(req, false);
}

/* AES-CBC */

static int riscv64_aes_cbc_crypt(struct skcipher_request *req, bool enc)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);
	while ((nbytes = walk.nbytes) != 0) {
		kernel_vector_begin();
		if (enc)
			aes_cbc_encrypt_zvkned(ctx, walk.src.virt.addr,
					       walk.dst.virt.addr,
					       nbytes & ~(AES_BLOCK_SIZE - 1),
					       walk.iv);
		else
			aes_cbc_decrypt_zvkned(ctx, walk.src.virt.addr,
					       walk.dst.virt.addr,
					       nbytes & ~(AES_BLOCK_SIZE - 1),
					       walk.iv);
		kernel_vector_end();
		err = skcipher_walk_done(&walk, nbytes & (AES_BLOCK_SIZE - 1));
	}

	return err;
}

static int riscv64_aes_cbc_encrypt(struct skcipher_request *req)
{
	return riscv64_aes_cbc_crypt(req, true);
}

static int riscv64_aes_cbc_decrypt(struct skcipher_request *req)
{
	return riscv64_aes_cbc_crypt(req, false);
}

/* AES-CBC-CTS */

static int riscv64_aes_cbc_cts_crypt(struct skcipher_request *req, bool enc)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct scatterlist sg_src[2], sg_dst[2];
	struct skcipher_request subreq;
	struct scatterlist *src, *dst;
	struct skcipher_walk walk;
	unsigned int cbc_len;
	int err;

	if (req->cryptlen < AES_BLOCK_SIZE)
		return -EINVAL;

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		return err;
	/*
	 * If the full message is available in one step, decrypt it in one call
	 * to the CBC-CTS assembly function.  This reduces overhead, especially
	 * on short messages.  Otherwise, fall back to doing CBC up to the last
	 * two blocks, then invoke CTS just for the ciphertext stealing.
	 */
	if (unlikely(walk.nbytes != req->cryptlen)) {
		cbc_len = round_down(req->cryptlen - AES_BLOCK_SIZE - 1,
				     AES_BLOCK_SIZE);
		skcipher_walk_abort(&walk);
		skcipher_request_set_tfm(&subreq, tfm);
		skcipher_request_set_callback(&subreq,
					      skcipher_request_flags(req),
					      NULL, NULL);
		skcipher_request_set_crypt(&subreq, req->src, req->dst,
					   cbc_len, req->iv);
		err = riscv64_aes_cbc_crypt(&subreq, enc);
		if (err)
			return err;
		dst = src = scatterwalk_ffwd(sg_src, req->src, cbc_len);
		if (req->dst != req->src)
			dst = scatterwalk_ffwd(sg_dst, req->dst, cbc_len);
		skcipher_request_set_crypt(&subreq, src, dst,
					   req->cryptlen - cbc_len, req->iv);
		err = skcipher_walk_virt(&walk, &subreq, false);
		if (err)
			return err;
	}
	kernel_vector_begin();
	aes_cbc_cts_crypt_zvkned(ctx, walk.src.virt.addr, walk.dst.virt.addr,
				 walk.nbytes, req->iv, enc);
	kernel_vector_end();
	return skcipher_walk_done(&walk, 0);
}

static int riscv64_aes_cbc_cts_encrypt(struct skcipher_request *req)
{
	return riscv64_aes_cbc_cts_crypt(req, true);
}

static int riscv64_aes_cbc_cts_decrypt(struct skcipher_request *req)
{
	return riscv64_aes_cbc_cts_crypt(req, false);
}

/* AES-CTR */

static int riscv64_aes_ctr_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	unsigned int nbytes, p1_nbytes;
	struct skcipher_walk walk;
	u32 ctr32, nblocks;
	int err;

	/* Get the low 32-bit word of the 128-bit big endian counter. */
	ctr32 = get_unaligned_be32(req->iv + 12);

	err = skcipher_walk_virt(&walk, req, false);
	while ((nbytes = walk.nbytes) != 0) {
		if (nbytes < walk.total) {
			/* Not the end yet, so keep the length block-aligned. */
			nbytes = round_down(nbytes, AES_BLOCK_SIZE);
			nblocks = nbytes / AES_BLOCK_SIZE;
		} else {
			/* It's the end, so include any final partial block. */
			nblocks = DIV_ROUND_UP(nbytes, AES_BLOCK_SIZE);
		}
		ctr32 += nblocks;

		kernel_vector_begin();
		if (ctr32 >= nblocks) {
			/* The low 32-bit word of the counter won't overflow. */
			aes_ctr32_crypt_zvkned_zvkb(ctx, walk.src.virt.addr,
						    walk.dst.virt.addr, nbytes,
						    req->iv);
		} else {
			/*
			 * The low 32-bit word of the counter will overflow.
			 * The assembly doesn't handle this case, so split the
			 * operation into two at the point where the overflow
			 * will occur.  After the first part, add the carry bit.
			 */
			p1_nbytes = min_t(unsigned int, nbytes,
					  (nblocks - ctr32) * AES_BLOCK_SIZE);
			aes_ctr32_crypt_zvkned_zvkb(ctx, walk.src.virt.addr,
						    walk.dst.virt.addr,
						    p1_nbytes, req->iv);
			crypto_inc(req->iv, 12);

			if (ctr32) {
				aes_ctr32_crypt_zvkned_zvkb(
					ctx,
					walk.src.virt.addr + p1_nbytes,
					walk.dst.virt.addr + p1_nbytes,
					nbytes - p1_nbytes, req->iv);
			}
		}
		kernel_vector_end();

		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}

	return err;
}

/* AES-XTS */

struct riscv64_aes_xts_ctx {
	struct crypto_aes_ctx ctx1;
	struct crypto_aes_ctx ctx2;
};

static int riscv64_aes_xts_setkey(struct crypto_skcipher *tfm, const u8 *key,
				  unsigned int keylen)
{
	struct riscv64_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);

	return xts_verify_key(tfm, key, keylen) ?:
	       riscv64_aes_setkey(&ctx->ctx1, key, keylen / 2) ?:
	       riscv64_aes_setkey(&ctx->ctx2, key + keylen / 2, keylen / 2);
}

static int riscv64_aes_xts_crypt(struct skcipher_request *req, bool enc)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct riscv64_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int tail = req->cryptlen % AES_BLOCK_SIZE;
	struct scatterlist sg_src[2], sg_dst[2];
	struct skcipher_request subreq;
	struct scatterlist *src, *dst;
	struct skcipher_walk walk;
	int err;

	if (req->cryptlen < AES_BLOCK_SIZE)
		return -EINVAL;

	/* Encrypt the IV with the tweak key to get the first tweak. */
	kernel_vector_begin();
	aes_encrypt_zvkned(&ctx->ctx2, req->iv, req->iv);
	kernel_vector_end();

	err = skcipher_walk_virt(&walk, req, false);

	/*
	 * If the message length isn't divisible by the AES block size and the
	 * full message isn't available in one step of the scatterlist walk,
	 * then separate off the last full block and the partial block.  This
	 * ensures that they are processed in the same call to the assembly
	 * function, which is required for ciphertext stealing.
	 */
	if (unlikely(tail > 0 && walk.nbytes < walk.total)) {
		skcipher_walk_abort(&walk);

		skcipher_request_set_tfm(&subreq, tfm);
		skcipher_request_set_callback(&subreq,
					      skcipher_request_flags(req),
					      NULL, NULL);
		skcipher_request_set_crypt(&subreq, req->src, req->dst,
					   req->cryptlen - tail - AES_BLOCK_SIZE,
					   req->iv);
		req = &subreq;
		err = skcipher_walk_virt(&walk, req, false);
	} else {
		tail = 0;
	}

	while (walk.nbytes) {
		unsigned int nbytes = walk.nbytes;

		if (nbytes < walk.total)
			nbytes = round_down(nbytes, AES_BLOCK_SIZE);

		kernel_vector_begin();
		if (enc)
			aes_xts_encrypt_zvkned_zvbb_zvkg(
				&ctx->ctx1, walk.src.virt.addr,
				walk.dst.virt.addr, nbytes, req->iv);
		else
			aes_xts_decrypt_zvkned_zvbb_zvkg(
				&ctx->ctx1, walk.src.virt.addr,
				walk.dst.virt.addr, nbytes, req->iv);
		kernel_vector_end();
		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}

	if (err || likely(!tail))
		return err;

	/* Do ciphertext stealing with the last full block and partial block. */

	dst = src = scatterwalk_ffwd(sg_src, req->src, req->cryptlen);
	if (req->dst != req->src)
		dst = scatterwalk_ffwd(sg_dst, req->dst, req->cryptlen);

	skcipher_request_set_crypt(req, src, dst, AES_BLOCK_SIZE + tail,
				   req->iv);

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		return err;

	kernel_vector_begin();
	if (enc)
		aes_xts_encrypt_zvkned_zvbb_zvkg(
			&ctx->ctx1, walk.src.virt.addr,
			walk.dst.virt.addr, walk.nbytes, req->iv);
	else
		aes_xts_decrypt_zvkned_zvbb_zvkg(
			&ctx->ctx1, walk.src.virt.addr,
			walk.dst.virt.addr, walk.nbytes, req->iv);
	kernel_vector_end();

	return skcipher_walk_done(&walk, 0);
}

static int riscv64_aes_xts_encrypt(struct skcipher_request *req)
{
	return riscv64_aes_xts_crypt(req, true);
}

static int riscv64_aes_xts_decrypt(struct skcipher_request *req)
{
	return riscv64_aes_xts_crypt(req, false);
}

/* Algorithm definitions */

static struct crypto_alg riscv64_zvkned_aes_cipher_alg = {
	.cra_flags = CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize = AES_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct crypto_aes_ctx),
	.cra_priority = 300,
	.cra_name = "aes",
	.cra_driver_name = "aes-riscv64-zvkned",
	.cra_cipher = {
		.cia_min_keysize = AES_MIN_KEY_SIZE,
		.cia_max_keysize = AES_MAX_KEY_SIZE,
		.cia_setkey = riscv64_aes_setkey_cipher,
		.cia_encrypt = riscv64_aes_encrypt,
		.cia_decrypt = riscv64_aes_decrypt,
	},
	.cra_module = THIS_MODULE,
};

static struct skcipher_alg riscv64_zvkned_aes_skcipher_algs[] = {
	{
		.setkey = riscv64_aes_setkey_skcipher,
		.encrypt = riscv64_aes_ecb_encrypt,
		.decrypt = riscv64_aes_ecb_decrypt,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.walksize = 8 * AES_BLOCK_SIZE, /* matches LMUL=8 */
		.base = {
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct crypto_aes_ctx),
			.cra_priority = 300,
			.cra_name = "ecb(aes)",
			.cra_driver_name = "ecb-aes-riscv64-zvkned",
			.cra_module = THIS_MODULE,
		},
	}, {
		.setkey = riscv64_aes_setkey_skcipher,
		.encrypt = riscv64_aes_cbc_encrypt,
		.decrypt = riscv64_aes_cbc_decrypt,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.base = {
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct crypto_aes_ctx),
			.cra_priority = 300,
			.cra_name = "cbc(aes)",
			.cra_driver_name = "cbc-aes-riscv64-zvkned",
			.cra_module = THIS_MODULE,
		},
	}, {
		.setkey = riscv64_aes_setkey_skcipher,
		.encrypt = riscv64_aes_cbc_cts_encrypt,
		.decrypt = riscv64_aes_cbc_cts_decrypt,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.walksize = 4 * AES_BLOCK_SIZE, /* matches LMUL=4 */
		.base = {
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct crypto_aes_ctx),
			.cra_priority = 300,
			.cra_name = "cts(cbc(aes))",
			.cra_driver_name = "cts-cbc-aes-riscv64-zvkned",
			.cra_module = THIS_MODULE,
		},
	}
};

static struct skcipher_alg riscv64_zvkned_zvkb_aes_skcipher_alg = {
	.setkey = riscv64_aes_setkey_skcipher,
	.encrypt = riscv64_aes_ctr_crypt,
	.decrypt = riscv64_aes_ctr_crypt,
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
	.ivsize = AES_BLOCK_SIZE,
	.chunksize = AES_BLOCK_SIZE,
	.walksize = 4 * AES_BLOCK_SIZE, /* matches LMUL=4 */
	.base = {
		.cra_blocksize = 1,
		.cra_ctxsize = sizeof(struct crypto_aes_ctx),
		.cra_priority = 300,
		.cra_name = "ctr(aes)",
		.cra_driver_name = "ctr-aes-riscv64-zvkned-zvkb",
		.cra_module = THIS_MODULE,
	},
};

static struct skcipher_alg riscv64_zvkned_zvbb_zvkg_aes_skcipher_alg = {
	.setkey = riscv64_aes_xts_setkey,
	.encrypt = riscv64_aes_xts_encrypt,
	.decrypt = riscv64_aes_xts_decrypt,
	.min_keysize = 2 * AES_MIN_KEY_SIZE,
	.max_keysize = 2 * AES_MAX_KEY_SIZE,
	.ivsize = AES_BLOCK_SIZE,
	.chunksize = AES_BLOCK_SIZE,
	.walksize = 4 * AES_BLOCK_SIZE, /* matches LMUL=4 */
	.base = {
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct riscv64_aes_xts_ctx),
		.cra_priority = 300,
		.cra_name = "xts(aes)",
		.cra_driver_name = "xts-aes-riscv64-zvkned-zvbb-zvkg",
		.cra_module = THIS_MODULE,
	},
};

static inline bool riscv64_aes_xts_supported(void)
{
	return riscv_isa_extension_available(NULL, ZVBB) &&
	       riscv_isa_extension_available(NULL, ZVKG) &&
	       riscv_vector_vlen() < 2048 /* Implementation limitation */;
}

static int __init riscv64_aes_mod_init(void)
{
	int err = -ENODEV;

	if (riscv_isa_extension_available(NULL, ZVKNED) &&
	    riscv_vector_vlen() >= 128) {
		err = crypto_register_alg(&riscv64_zvkned_aes_cipher_alg);
		if (err)
			return err;

		err = crypto_register_skciphers(
			riscv64_zvkned_aes_skcipher_algs,
			ARRAY_SIZE(riscv64_zvkned_aes_skcipher_algs));
		if (err)
			goto unregister_zvkned_cipher_alg;

		if (riscv_isa_extension_available(NULL, ZVKB)) {
			err = crypto_register_skcipher(
				&riscv64_zvkned_zvkb_aes_skcipher_alg);
			if (err)
				goto unregister_zvkned_skcipher_algs;
		}

		if (riscv64_aes_xts_supported()) {
			err = crypto_register_skcipher(
				&riscv64_zvkned_zvbb_zvkg_aes_skcipher_alg);
			if (err)
				goto unregister_zvkned_zvkb_skcipher_alg;
		}
	}

	return err;

unregister_zvkned_zvkb_skcipher_alg:
	if (riscv_isa_extension_available(NULL, ZVKB))
		crypto_unregister_skcipher(&riscv64_zvkned_zvkb_aes_skcipher_alg);
unregister_zvkned_skcipher_algs:
	crypto_unregister_skciphers(riscv64_zvkned_aes_skcipher_algs,
				    ARRAY_SIZE(riscv64_zvkned_aes_skcipher_algs));
unregister_zvkned_cipher_alg:
	crypto_unregister_alg(&riscv64_zvkned_aes_cipher_alg);
	return err;
}

static void __exit riscv64_aes_mod_exit(void)
{
	if (riscv64_aes_xts_supported())
		crypto_unregister_skcipher(&riscv64_zvkned_zvbb_zvkg_aes_skcipher_alg);
	if (riscv_isa_extension_available(NULL, ZVKB))
		crypto_unregister_skcipher(&riscv64_zvkned_zvkb_aes_skcipher_alg);
	crypto_unregister_skciphers(riscv64_zvkned_aes_skcipher_algs,
				    ARRAY_SIZE(riscv64_zvkned_aes_skcipher_algs));
	crypto_unregister_alg(&riscv64_zvkned_aes_cipher_alg);
}

module_init(riscv64_aes_mod_init);
module_exit(riscv64_aes_mod_exit);

MODULE_DESCRIPTION("AES-ECB/CBC/CTS/CTR/XTS (RISC-V accelerated)");
MODULE_AUTHOR("Jerry Shih <jerry.shih@sifive.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("aes");
MODULE_ALIAS_CRYPTO("ecb(aes)");
MODULE_ALIAS_CRYPTO("cbc(aes)");
MODULE_ALIAS_CRYPTO("cts(cbc(aes))");
MODULE_ALIAS_CRYPTO("ctr(aes)");
MODULE_ALIAS_CRYPTO("xts(aes)");
