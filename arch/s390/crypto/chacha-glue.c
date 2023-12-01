// SPDX-License-Identifier: GPL-2.0
/*
 * s390 ChaCha stream cipher.
 *
 * Copyright IBM Corp. 2021
 */

#define KMSG_COMPONENT "chacha_s390"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <crypto/internal/chacha.h>
#include <crypto/internal/skcipher.h>
#include <crypto/algapi.h>
#include <linux/cpufeature.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <asm/fpu/api.h>
#include "chacha-s390.h"

static void chacha20_crypt_s390(u32 *state, u8 *dst, const u8 *src,
				unsigned int nbytes, const u32 *key,
				u32 *counter)
{
	struct kernel_fpu vxstate;

	kernel_fpu_begin(&vxstate, KERNEL_VXR);
	chacha20_vx(dst, src, nbytes, key, counter);
	kernel_fpu_end(&vxstate, KERNEL_VXR);

	*counter += round_up(nbytes, CHACHA_BLOCK_SIZE) / CHACHA_BLOCK_SIZE;
}

static int chacha20_s390(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);
	u32 state[CHACHA_STATE_WORDS] __aligned(16);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int rc;

	rc = skcipher_walk_virt(&walk, req, false);
	chacha_init_generic(state, ctx->key, req->iv);

	while (walk.nbytes > 0) {
		nbytes = walk.nbytes;
		if (nbytes < walk.total)
			nbytes = round_down(nbytes, walk.stride);

		if (nbytes <= CHACHA_BLOCK_SIZE) {
			chacha_crypt_generic(state, walk.dst.virt.addr,
					     walk.src.virt.addr, nbytes,
					     ctx->nrounds);
		} else {
			chacha20_crypt_s390(state, walk.dst.virt.addr,
					    walk.src.virt.addr, nbytes,
					    &state[4], &state[12]);
		}
		rc = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}
	return rc;
}

void hchacha_block_arch(const u32 *state, u32 *stream, int nrounds)
{
	/* TODO: implement hchacha_block_arch() in assembly */
	hchacha_block_generic(state, stream, nrounds);
}
EXPORT_SYMBOL(hchacha_block_arch);

void chacha_init_arch(u32 *state, const u32 *key, const u8 *iv)
{
	chacha_init_generic(state, key, iv);
}
EXPORT_SYMBOL(chacha_init_arch);

void chacha_crypt_arch(u32 *state, u8 *dst, const u8 *src,
		       unsigned int bytes, int nrounds)
{
	/* s390 chacha20 implementation has 20 rounds hard-coded,
	 * it cannot handle a block of data or less, but otherwise
	 * it can handle data of arbitrary size
	 */
	if (bytes <= CHACHA_BLOCK_SIZE || nrounds != 20 || !cpu_has_vx())
		chacha_crypt_generic(state, dst, src, bytes, nrounds);
	else
		chacha20_crypt_s390(state, dst, src, bytes,
				    &state[4], &state[12]);
}
EXPORT_SYMBOL(chacha_crypt_arch);

static struct skcipher_alg chacha_algs[] = {
	{
		.base.cra_name		= "chacha20",
		.base.cra_driver_name	= "chacha20-s390",
		.base.cra_priority	= 900,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= CHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha20_setkey,
		.encrypt		= chacha20_s390,
		.decrypt		= chacha20_s390,
	}
};

static int __init chacha_mod_init(void)
{
	return IS_REACHABLE(CONFIG_CRYPTO_SKCIPHER) ?
		crypto_register_skciphers(chacha_algs, ARRAY_SIZE(chacha_algs)) : 0;
}

static void __exit chacha_mod_fini(void)
{
	if (IS_REACHABLE(CONFIG_CRYPTO_SKCIPHER))
		crypto_unregister_skciphers(chacha_algs, ARRAY_SIZE(chacha_algs));
}

module_cpu_feature_match(S390_CPU_FEATURE_VXRS, chacha_mod_init);
module_exit(chacha_mod_fini);

MODULE_DESCRIPTION("ChaCha20 stream cipher");
MODULE_LICENSE("GPL v2");

MODULE_ALIAS_CRYPTO("chacha20");
