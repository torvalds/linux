// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue Code for 3-way parallel assembler optimized version of Twofish
 *
 * Copyright (c) 2011 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 */

#include <crypto/algapi.h>
#include <crypto/twofish.h>
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>

#include "twofish.h"
#include "ecb_cbc_helpers.h"

EXPORT_SYMBOL_GPL(__twofish_enc_blk_3way);
EXPORT_SYMBOL_GPL(twofish_dec_blk_3way);

static int twofish_setkey_skcipher(struct crypto_skcipher *tfm,
				   const u8 *key, unsigned int keylen)
{
	return twofish_setkey(&tfm->base, key, keylen);
}

static inline void twofish_enc_blk_3way(const void *ctx, u8 *dst, const u8 *src)
{
	__twofish_enc_blk_3way(ctx, dst, src, false);
}

void twofish_dec_blk_cbc_3way(const void *ctx, u8 *dst, const u8 *src)
{
	u8 buf[2][TF_BLOCK_SIZE];
	const u8 *s = src;

	if (dst == src)
		s = memcpy(buf, src, sizeof(buf));
	twofish_dec_blk_3way(ctx, dst, src);
	crypto_xor(dst + TF_BLOCK_SIZE, s, sizeof(buf));

}
EXPORT_SYMBOL_GPL(twofish_dec_blk_cbc_3way);

static int ecb_encrypt(struct skcipher_request *req)
{
	ECB_WALK_START(req, TF_BLOCK_SIZE, -1);
	ECB_BLOCK(3, twofish_enc_blk_3way);
	ECB_BLOCK(1, twofish_enc_blk);
	ECB_WALK_END();
}

static int ecb_decrypt(struct skcipher_request *req)
{
	ECB_WALK_START(req, TF_BLOCK_SIZE, -1);
	ECB_BLOCK(3, twofish_dec_blk_3way);
	ECB_BLOCK(1, twofish_dec_blk);
	ECB_WALK_END();
}

static int cbc_encrypt(struct skcipher_request *req)
{
	CBC_WALK_START(req, TF_BLOCK_SIZE, -1);
	CBC_ENC_BLOCK(twofish_enc_blk);
	CBC_WALK_END();
}

static int cbc_decrypt(struct skcipher_request *req)
{
	CBC_WALK_START(req, TF_BLOCK_SIZE, -1);
	CBC_DEC_BLOCK(3, twofish_dec_blk_cbc_3way);
	CBC_DEC_BLOCK(1, twofish_dec_blk);
	CBC_WALK_END();
}

static struct skcipher_alg tf_skciphers[] = {
	{
		.base.cra_name		= "ecb(twofish)",
		.base.cra_driver_name	= "ecb-twofish-3way",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= TF_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct twofish_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= TF_MIN_KEY_SIZE,
		.max_keysize		= TF_MAX_KEY_SIZE,
		.setkey			= twofish_setkey_skcipher,
		.encrypt		= ecb_encrypt,
		.decrypt		= ecb_decrypt,
	}, {
		.base.cra_name		= "cbc(twofish)",
		.base.cra_driver_name	= "cbc-twofish-3way",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= TF_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct twofish_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= TF_MIN_KEY_SIZE,
		.max_keysize		= TF_MAX_KEY_SIZE,
		.ivsize			= TF_BLOCK_SIZE,
		.setkey			= twofish_setkey_skcipher,
		.encrypt		= cbc_encrypt,
		.decrypt		= cbc_decrypt,
	},
};

static bool is_blacklisted_cpu(void)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return false;

	if (boot_cpu_data.x86 == 0x06 &&
		(boot_cpu_data.x86_model == 0x1c ||
		 boot_cpu_data.x86_model == 0x26 ||
		 boot_cpu_data.x86_model == 0x36)) {
		/*
		 * On Atom, twofish-3way is slower than original assembler
		 * implementation. Twofish-3way trades off some performance in
		 * storing blocks in 64bit registers to allow three blocks to
		 * be processed parallel. Parallel operation then allows gaining
		 * more performance than was trade off, on out-of-order CPUs.
		 * However Atom does not benefit from this parallellism and
		 * should be blacklisted.
		 */
		return true;
	}

	if (boot_cpu_data.x86 == 0x0f) {
		/*
		 * On Pentium 4, twofish-3way is slower than original assembler
		 * implementation because excessive uses of 64bit rotate and
		 * left-shifts (which are really slow on P4) needed to store and
		 * handle 128bit block in two 64bit registers.
		 */
		return true;
	}

	return false;
}

static int force;
module_param(force, int, 0);
MODULE_PARM_DESC(force, "Force module load, ignore CPU blacklist");

static int __init init(void)
{
	if (!force && is_blacklisted_cpu()) {
		printk(KERN_INFO
			"twofish-x86_64-3way: performance on this CPU "
			"would be suboptimal: disabling "
			"twofish-x86_64-3way.\n");
		return -ENODEV;
	}

	return crypto_register_skciphers(tf_skciphers,
					 ARRAY_SIZE(tf_skciphers));
}

static void __exit fini(void)
{
	crypto_unregister_skciphers(tf_skciphers, ARRAY_SIZE(tf_skciphers));
}

module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Twofish Cipher Algorithm, 3-way parallel asm optimized");
MODULE_ALIAS_CRYPTO("twofish");
MODULE_ALIAS_CRYPTO("twofish-asm");
