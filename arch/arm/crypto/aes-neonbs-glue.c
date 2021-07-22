// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bit sliced AES using NEON instructions
 *
 * Copyright (C) 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <asm/simd.h>
#include <crypto/aes.h>
#include <crypto/ctr.h>
#include <crypto/internal/cipher.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <crypto/xts.h>
#include <linux/module.h>

MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

MODULE_ALIAS_CRYPTO("ecb(aes)");
MODULE_ALIAS_CRYPTO("cbc(aes)-all");
MODULE_ALIAS_CRYPTO("ctr(aes)");
MODULE_ALIAS_CRYPTO("xts(aes)");

MODULE_IMPORT_NS(CRYPTO_INTERNAL);

asmlinkage void aesbs_convert_key(u8 out[], u32 const rk[], int rounds);

asmlinkage void aesbs_ecb_encrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks);
asmlinkage void aesbs_ecb_decrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks);

asmlinkage void aesbs_cbc_decrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[]);

asmlinkage void aesbs_ctr_encrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 ctr[], u8 final[]);

asmlinkage void aesbs_xts_encrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[], int);
asmlinkage void aesbs_xts_decrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[], int);

struct aesbs_ctx {
	int	rounds;
	u8	rk[13 * (8 * AES_BLOCK_SIZE) + 32] __aligned(AES_BLOCK_SIZE);
};

struct aesbs_cbc_ctx {
	struct aesbs_ctx	key;
	struct crypto_skcipher	*enc_tfm;
};

struct aesbs_xts_ctx {
	struct aesbs_ctx	key;
	struct crypto_cipher	*cts_tfm;
	struct crypto_cipher	*tweak_tfm;
};

struct aesbs_ctr_ctx {
	struct aesbs_ctx	key;		/* must be first member */
	struct crypto_aes_ctx	fallback;
};

static int aesbs_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			unsigned int key_len)
{
	struct aesbs_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_aes_ctx rk;
	int err;

	err = aes_expandkey(&rk, in_key, key_len);
	if (err)
		return err;

	ctx->rounds = 6 + key_len / 4;

	kernel_neon_begin();
	aesbs_convert_key(ctx->rk, rk.key_enc, ctx->rounds);
	kernel_neon_end();

	return 0;
}

static int __ecb_crypt(struct skcipher_request *req,
		       void (*fn)(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks))
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes >= AES_BLOCK_SIZE) {
		unsigned int blocks = walk.nbytes / AES_BLOCK_SIZE;

		if (walk.nbytes < walk.total)
			blocks = round_down(blocks,
					    walk.stride / AES_BLOCK_SIZE);

		kernel_neon_begin();
		fn(walk.dst.virt.addr, walk.src.virt.addr, ctx->rk,
		   ctx->rounds, blocks);
		kernel_neon_end();
		err = skcipher_walk_done(&walk,
					 walk.nbytes - blocks * AES_BLOCK_SIZE);
	}

	return err;
}

static int ecb_encrypt(struct skcipher_request *req)
{
	return __ecb_crypt(req, aesbs_ecb_encrypt);
}

static int ecb_decrypt(struct skcipher_request *req)
{
	return __ecb_crypt(req, aesbs_ecb_decrypt);
}

static int aesbs_cbc_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			    unsigned int key_len)
{
	struct aesbs_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_aes_ctx rk;
	int err;

	err = aes_expandkey(&rk, in_key, key_len);
	if (err)
		return err;

	ctx->key.rounds = 6 + key_len / 4;

	kernel_neon_begin();
	aesbs_convert_key(ctx->key.rk, rk.key_enc, ctx->key.rounds);
	kernel_neon_end();
	memzero_explicit(&rk, sizeof(rk));

	return crypto_skcipher_setkey(ctx->enc_tfm, in_key, key_len);
}

static int cbc_encrypt(struct skcipher_request *req)
{
	struct skcipher_request *subreq = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);

	skcipher_request_set_tfm(subreq, ctx->enc_tfm);
	skcipher_request_set_callback(subreq,
				      skcipher_request_flags(req),
				      NULL, NULL);
	skcipher_request_set_crypt(subreq, req->src, req->dst,
				   req->cryptlen, req->iv);

	return crypto_skcipher_encrypt(subreq);
}

static int cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes >= AES_BLOCK_SIZE) {
		unsigned int blocks = walk.nbytes / AES_BLOCK_SIZE;

		if (walk.nbytes < walk.total)
			blocks = round_down(blocks,
					    walk.stride / AES_BLOCK_SIZE);

		kernel_neon_begin();
		aesbs_cbc_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
				  ctx->key.rk, ctx->key.rounds, blocks,
				  walk.iv);
		kernel_neon_end();
		err = skcipher_walk_done(&walk,
					 walk.nbytes - blocks * AES_BLOCK_SIZE);
	}

	return err;
}

static int cbc_init(struct crypto_skcipher *tfm)
{
	struct aesbs_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	unsigned int reqsize;

	ctx->enc_tfm = crypto_alloc_skcipher("cbc(aes)", 0, CRYPTO_ALG_ASYNC |
					     CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->enc_tfm))
		return PTR_ERR(ctx->enc_tfm);

	reqsize = sizeof(struct skcipher_request);
	reqsize += crypto_skcipher_reqsize(ctx->enc_tfm);
	crypto_skcipher_set_reqsize(tfm, reqsize);

	return 0;
}

static void cbc_exit(struct crypto_skcipher *tfm)
{
	struct aesbs_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_skcipher(ctx->enc_tfm);
}

static int aesbs_ctr_setkey_sync(struct crypto_skcipher *tfm, const u8 *in_key,
				 unsigned int key_len)
{
	struct aesbs_ctr_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err;

	err = aes_expandkey(&ctx->fallback, in_key, key_len);
	if (err)
		return err;

	ctx->key.rounds = 6 + key_len / 4;

	kernel_neon_begin();
	aesbs_convert_key(ctx->key.rk, ctx->fallback.key_enc, ctx->key.rounds);
	kernel_neon_end();

	return 0;
}

static int ctr_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	u8 buf[AES_BLOCK_SIZE];
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes > 0) {
		unsigned int blocks = walk.nbytes / AES_BLOCK_SIZE;
		u8 *final = (walk.total % AES_BLOCK_SIZE) ? buf : NULL;

		if (walk.nbytes < walk.total) {
			blocks = round_down(blocks,
					    walk.stride / AES_BLOCK_SIZE);
			final = NULL;
		}

		kernel_neon_begin();
		aesbs_ctr_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				  ctx->rk, ctx->rounds, blocks, walk.iv, final);
		kernel_neon_end();

		if (final) {
			u8 *dst = walk.dst.virt.addr + blocks * AES_BLOCK_SIZE;
			u8 *src = walk.src.virt.addr + blocks * AES_BLOCK_SIZE;

			crypto_xor_cpy(dst, src, final,
				       walk.total % AES_BLOCK_SIZE);

			err = skcipher_walk_done(&walk, 0);
			break;
		}
		err = skcipher_walk_done(&walk,
					 walk.nbytes - blocks * AES_BLOCK_SIZE);
	}

	return err;
}

static void ctr_encrypt_one(struct crypto_skcipher *tfm, const u8 *src, u8 *dst)
{
	struct aesbs_ctr_ctx *ctx = crypto_skcipher_ctx(tfm);
	unsigned long flags;

	/*
	 * Temporarily disable interrupts to avoid races where
	 * cachelines are evicted when the CPU is interrupted
	 * to do something else.
	 */
	local_irq_save(flags);
	aes_encrypt(&ctx->fallback, dst, src);
	local_irq_restore(flags);
}

static int ctr_encrypt_sync(struct skcipher_request *req)
{
	if (!crypto_simd_usable())
		return crypto_ctr_encrypt_walk(req, ctr_encrypt_one);

	return ctr_encrypt(req);
}

static int aesbs_xts_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			    unsigned int key_len)
{
	struct aesbs_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err;

	err = xts_verify_key(tfm, in_key, key_len);
	if (err)
		return err;

	key_len /= 2;
	err = crypto_cipher_setkey(ctx->cts_tfm, in_key, key_len);
	if (err)
		return err;
	err = crypto_cipher_setkey(ctx->tweak_tfm, in_key + key_len, key_len);
	if (err)
		return err;

	return aesbs_setkey(tfm, in_key, key_len);
}

static int xts_init(struct crypto_skcipher *tfm)
{
	struct aesbs_xts_ctx *ctx = crypto_skcipher_ctx(tfm);

	ctx->cts_tfm = crypto_alloc_cipher("aes", 0, 0);
	if (IS_ERR(ctx->cts_tfm))
		return PTR_ERR(ctx->cts_tfm);

	ctx->tweak_tfm = crypto_alloc_cipher("aes", 0, 0);
	if (IS_ERR(ctx->tweak_tfm))
		crypto_free_cipher(ctx->cts_tfm);

	return PTR_ERR_OR_ZERO(ctx->tweak_tfm);
}

static void xts_exit(struct crypto_skcipher *tfm)
{
	struct aesbs_xts_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_cipher(ctx->tweak_tfm);
	crypto_free_cipher(ctx->cts_tfm);
}

static int __xts_crypt(struct skcipher_request *req, bool encrypt,
		       void (*fn)(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[], int))
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int tail = req->cryptlen % AES_BLOCK_SIZE;
	struct skcipher_request subreq;
	u8 buf[2 * AES_BLOCK_SIZE];
	struct skcipher_walk walk;
	int err;

	if (req->cryptlen < AES_BLOCK_SIZE)
		return -EINVAL;

	if (unlikely(tail)) {
		skcipher_request_set_tfm(&subreq, tfm);
		skcipher_request_set_callback(&subreq,
					      skcipher_request_flags(req),
					      NULL, NULL);
		skcipher_request_set_crypt(&subreq, req->src, req->dst,
					   req->cryptlen - tail, req->iv);
		req = &subreq;
	}

	err = skcipher_walk_virt(&walk, req, true);
	if (err)
		return err;

	crypto_cipher_encrypt_one(ctx->tweak_tfm, walk.iv, walk.iv);

	while (walk.nbytes >= AES_BLOCK_SIZE) {
		unsigned int blocks = walk.nbytes / AES_BLOCK_SIZE;
		int reorder_last_tweak = !encrypt && tail > 0;

		if (walk.nbytes < walk.total) {
			blocks = round_down(blocks,
					    walk.stride / AES_BLOCK_SIZE);
			reorder_last_tweak = 0;
		}

		kernel_neon_begin();
		fn(walk.dst.virt.addr, walk.src.virt.addr, ctx->key.rk,
		   ctx->key.rounds, blocks, walk.iv, reorder_last_tweak);
		kernel_neon_end();
		err = skcipher_walk_done(&walk,
					 walk.nbytes - blocks * AES_BLOCK_SIZE);
	}

	if (err || likely(!tail))
		return err;

	/* handle ciphertext stealing */
	scatterwalk_map_and_copy(buf, req->dst, req->cryptlen - AES_BLOCK_SIZE,
				 AES_BLOCK_SIZE, 0);
	memcpy(buf + AES_BLOCK_SIZE, buf, tail);
	scatterwalk_map_and_copy(buf, req->src, req->cryptlen, tail, 0);

	crypto_xor(buf, req->iv, AES_BLOCK_SIZE);

	if (encrypt)
		crypto_cipher_encrypt_one(ctx->cts_tfm, buf, buf);
	else
		crypto_cipher_decrypt_one(ctx->cts_tfm, buf, buf);

	crypto_xor(buf, req->iv, AES_BLOCK_SIZE);

	scatterwalk_map_and_copy(buf, req->dst, req->cryptlen - AES_BLOCK_SIZE,
				 AES_BLOCK_SIZE + tail, 1);
	return 0;
}

static int xts_encrypt(struct skcipher_request *req)
{
	return __xts_crypt(req, true, aesbs_xts_encrypt);
}

static int xts_decrypt(struct skcipher_request *req)
{
	return __xts_crypt(req, false, aesbs_xts_decrypt);
}

static struct skcipher_alg aes_algs[] = { {
	.base.cra_name		= "__ecb(aes)",
	.base.cra_driver_name	= "__ecb-aes-neonbs",
	.base.cra_priority	= 250,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct aesbs_ctx),
	.base.cra_module	= THIS_MODULE,
	.base.cra_flags		= CRYPTO_ALG_INTERNAL,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.setkey			= aesbs_setkey,
	.encrypt		= ecb_encrypt,
	.decrypt		= ecb_decrypt,
}, {
	.base.cra_name		= "__cbc(aes)",
	.base.cra_driver_name	= "__cbc-aes-neonbs",
	.base.cra_priority	= 250,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct aesbs_cbc_ctx),
	.base.cra_module	= THIS_MODULE,
	.base.cra_flags		= CRYPTO_ALG_INTERNAL |
				  CRYPTO_ALG_NEED_FALLBACK,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= aesbs_cbc_setkey,
	.encrypt		= cbc_encrypt,
	.decrypt		= cbc_decrypt,
	.init			= cbc_init,
	.exit			= cbc_exit,
}, {
	.base.cra_name		= "__ctr(aes)",
	.base.cra_driver_name	= "__ctr-aes-neonbs",
	.base.cra_priority	= 250,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct aesbs_ctx),
	.base.cra_module	= THIS_MODULE,
	.base.cra_flags		= CRYPTO_ALG_INTERNAL,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.chunksize		= AES_BLOCK_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= aesbs_setkey,
	.encrypt		= ctr_encrypt,
	.decrypt		= ctr_encrypt,
}, {
	.base.cra_name		= "ctr(aes)",
	.base.cra_driver_name	= "ctr-aes-neonbs-sync",
	.base.cra_priority	= 250 - 1,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct aesbs_ctr_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.chunksize		= AES_BLOCK_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= aesbs_ctr_setkey_sync,
	.encrypt		= ctr_encrypt_sync,
	.decrypt		= ctr_encrypt_sync,
}, {
	.base.cra_name		= "__xts(aes)",
	.base.cra_driver_name	= "__xts-aes-neonbs",
	.base.cra_priority	= 250,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct aesbs_xts_ctx),
	.base.cra_module	= THIS_MODULE,
	.base.cra_flags		= CRYPTO_ALG_INTERNAL,

	.min_keysize		= 2 * AES_MIN_KEY_SIZE,
	.max_keysize		= 2 * AES_MAX_KEY_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= aesbs_xts_setkey,
	.encrypt		= xts_encrypt,
	.decrypt		= xts_decrypt,
	.init			= xts_init,
	.exit			= xts_exit,
} };

static struct simd_skcipher_alg *aes_simd_algs[ARRAY_SIZE(aes_algs)];

static void aes_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(aes_simd_algs); i++)
		if (aes_simd_algs[i])
			simd_skcipher_free(aes_simd_algs[i]);

	crypto_unregister_skciphers(aes_algs, ARRAY_SIZE(aes_algs));
}

static int __init aes_init(void)
{
	struct simd_skcipher_alg *simd;
	const char *basename;
	const char *algname;
	const char *drvname;
	int err;
	int i;

	if (!(elf_hwcap & HWCAP_NEON))
		return -ENODEV;

	err = crypto_register_skciphers(aes_algs, ARRAY_SIZE(aes_algs));
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(aes_algs); i++) {
		if (!(aes_algs[i].base.cra_flags & CRYPTO_ALG_INTERNAL))
			continue;

		algname = aes_algs[i].base.cra_name + 2;
		drvname = aes_algs[i].base.cra_driver_name + 2;
		basename = aes_algs[i].base.cra_driver_name;
		simd = simd_skcipher_create_compat(algname, drvname, basename);
		err = PTR_ERR(simd);
		if (IS_ERR(simd))
			goto unregister_simds;

		aes_simd_algs[i] = simd;
	}
	return 0;

unregister_simds:
	aes_exit();
	return err;
}

late_initcall(aes_init);
module_exit(aes_exit);
