// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PowerPC P10 (ppc64le) accelerated ChaCha and XChaCha stream ciphers,
 * including ChaCha20 (RFC7539)
 *
 * Copyright 2023- IBM Corp. All rights reserved.
 */

#include <crypto/algapi.h>
#include <crypto/internal/chacha.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cpufeature.h>
#include <linux/sizes.h>
#include <asm/simd.h>
#include <asm/switch_to.h>

asmlinkage void chacha_p10le_8x(u32 *state, u8 *dst, const u8 *src,
				unsigned int len, int nrounds);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_p10);

static void vsx_begin(void)
{
	preempt_disable();
	enable_kernel_vsx();
}

static void vsx_end(void)
{
	disable_kernel_vsx();
	preempt_enable();
}

static void chacha_p10_do_8x(u32 *state, u8 *dst, const u8 *src,
			     unsigned int bytes, int nrounds)
{
	unsigned int l = bytes & ~0x0FF;

	if (l > 0) {
		chacha_p10le_8x(state, dst, src, l, nrounds);
		bytes -= l;
		src += l;
		dst += l;
		state[12] += l / CHACHA_BLOCK_SIZE;
	}

	if (bytes > 0)
		chacha_crypt_generic(state, dst, src, bytes, nrounds);
}

void hchacha_block_arch(const u32 *state, u32 *stream, int nrounds)
{
	hchacha_block_generic(state, stream, nrounds);
}
EXPORT_SYMBOL(hchacha_block_arch);

void chacha_crypt_arch(u32 *state, u8 *dst, const u8 *src, unsigned int bytes,
		       int nrounds)
{
	if (!static_branch_likely(&have_p10) || bytes <= CHACHA_BLOCK_SIZE ||
	    !crypto_simd_usable())
		return chacha_crypt_generic(state, dst, src, bytes, nrounds);

	do {
		unsigned int todo = min_t(unsigned int, bytes, SZ_4K);

		vsx_begin();
		chacha_p10_do_8x(state, dst, src, todo, nrounds);
		vsx_end();

		bytes -= todo;
		src += todo;
		dst += todo;
	} while (bytes);
}
EXPORT_SYMBOL(chacha_crypt_arch);

static int chacha_p10_stream_xor(struct skcipher_request *req,
				 const struct chacha_ctx *ctx, const u8 *iv)
{
	struct skcipher_walk walk;
	u32 state[16];
	int err;

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		return err;

	chacha_init(state, ctx->key, iv);

	while (walk.nbytes > 0) {
		unsigned int nbytes = walk.nbytes;

		if (nbytes < walk.total)
			nbytes = rounddown(nbytes, walk.stride);

		if (!crypto_simd_usable()) {
			chacha_crypt_generic(state, walk.dst.virt.addr,
					     walk.src.virt.addr, nbytes,
					     ctx->nrounds);
		} else {
			vsx_begin();
			chacha_p10_do_8x(state, walk.dst.virt.addr,
				      walk.src.virt.addr, nbytes, ctx->nrounds);
			vsx_end();
		}
		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
		if (err)
			break;
	}

	return err;
}

static int chacha_p10(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);

	return chacha_p10_stream_xor(req, ctx, req->iv);
}

static int xchacha_p10(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct chacha_ctx subctx;
	u32 state[16];
	u8 real_iv[16];

	chacha_init(state, ctx->key, req->iv);
	hchacha_block_arch(state, subctx.key, ctx->nrounds);
	subctx.nrounds = ctx->nrounds;

	memcpy(&real_iv[0], req->iv + 24, 8);
	memcpy(&real_iv[8], req->iv + 16, 8);
	return chacha_p10_stream_xor(req, &subctx, real_iv);
}

static struct skcipher_alg algs[] = {
	{
		.base.cra_name		= "chacha20",
		.base.cra_driver_name	= "chacha20-p10",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= CHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha20_setkey,
		.encrypt		= chacha_p10,
		.decrypt		= chacha_p10,
	}, {
		.base.cra_name		= "xchacha20",
		.base.cra_driver_name	= "xchacha20-p10",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= XCHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha20_setkey,
		.encrypt		= xchacha_p10,
		.decrypt		= xchacha_p10,
	}, {
		.base.cra_name		= "xchacha12",
		.base.cra_driver_name	= "xchacha12-p10",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= XCHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha12_setkey,
		.encrypt		= xchacha_p10,
		.decrypt		= xchacha_p10,
	}
};

static int __init chacha_p10_init(void)
{
	if (!cpu_has_feature(CPU_FTR_ARCH_31))
		return 0;

	static_branch_enable(&have_p10);

	return crypto_register_skciphers(algs, ARRAY_SIZE(algs));
}

static void __exit chacha_p10_exit(void)
{
	if (!static_branch_likely(&have_p10))
		return;

	crypto_unregister_skciphers(algs, ARRAY_SIZE(algs));
}

module_init(chacha_p10_init);
module_exit(chacha_p10_exit);

MODULE_DESCRIPTION("ChaCha and XChaCha stream ciphers (P10 accelerated)");
MODULE_AUTHOR("Danny Tsen <dtsen@linux.ibm.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("chacha20");
MODULE_ALIAS_CRYPTO("chacha20-p10");
MODULE_ALIAS_CRYPTO("xchacha20");
MODULE_ALIAS_CRYPTO("xchacha20-p10");
MODULE_ALIAS_CRYPTO("xchacha12");
MODULE_ALIAS_CRYPTO("xchacha12-p10");
