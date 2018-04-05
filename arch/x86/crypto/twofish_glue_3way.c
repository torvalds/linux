/*
 * Glue Code for 3-way parallel assembler optimized version of Twofish
 *
 * Copyright (c) 2011 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 */

#include <asm/crypto/glue_helper.h>
#include <asm/crypto/twofish.h>
#include <crypto/algapi.h>
#include <crypto/b128ops.h>
#include <crypto/internal/skcipher.h>
#include <crypto/twofish.h>
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>

EXPORT_SYMBOL_GPL(__twofish_enc_blk_3way);
EXPORT_SYMBOL_GPL(twofish_dec_blk_3way);

static int twofish_setkey_skcipher(struct crypto_skcipher *tfm,
				   const u8 *key, unsigned int keylen)
{
	return twofish_setkey(&tfm->base, key, keylen);
}

static inline void twofish_enc_blk_3way(struct twofish_ctx *ctx, u8 *dst,
					const u8 *src)
{
	__twofish_enc_blk_3way(ctx, dst, src, false);
}

static inline void twofish_enc_blk_xor_3way(struct twofish_ctx *ctx, u8 *dst,
					    const u8 *src)
{
	__twofish_enc_blk_3way(ctx, dst, src, true);
}

void twofish_dec_blk_cbc_3way(void *ctx, u128 *dst, const u128 *src)
{
	u128 ivs[2];

	ivs[0] = src[0];
	ivs[1] = src[1];

	twofish_dec_blk_3way(ctx, (u8 *)dst, (u8 *)src);

	u128_xor(&dst[1], &dst[1], &ivs[0]);
	u128_xor(&dst[2], &dst[2], &ivs[1]);
}
EXPORT_SYMBOL_GPL(twofish_dec_blk_cbc_3way);

void twofish_enc_blk_ctr(void *ctx, u128 *dst, const u128 *src, le128 *iv)
{
	be128 ctrblk;

	if (dst != src)
		*dst = *src;

	le128_to_be128(&ctrblk, iv);
	le128_inc(iv);

	twofish_enc_blk(ctx, (u8 *)&ctrblk, (u8 *)&ctrblk);
	u128_xor(dst, dst, (u128 *)&ctrblk);
}
EXPORT_SYMBOL_GPL(twofish_enc_blk_ctr);

void twofish_enc_blk_ctr_3way(void *ctx, u128 *dst, const u128 *src,
			      le128 *iv)
{
	be128 ctrblks[3];

	if (dst != src) {
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = src[2];
	}

	le128_to_be128(&ctrblks[0], iv);
	le128_inc(iv);
	le128_to_be128(&ctrblks[1], iv);
	le128_inc(iv);
	le128_to_be128(&ctrblks[2], iv);
	le128_inc(iv);

	twofish_enc_blk_xor_3way(ctx, (u8 *)dst, (u8 *)ctrblks);
}
EXPORT_SYMBOL_GPL(twofish_enc_blk_ctr_3way);

static const struct common_glue_ctx twofish_enc = {
	.num_funcs = 2,
	.fpu_blocks_limit = -1,

	.funcs = { {
		.num_blocks = 3,
		.fn_u = { .ecb = GLUE_FUNC_CAST(twofish_enc_blk_3way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(twofish_enc_blk) }
	} }
};

static const struct common_glue_ctx twofish_ctr = {
	.num_funcs = 2,
	.fpu_blocks_limit = -1,

	.funcs = { {
		.num_blocks = 3,
		.fn_u = { .ecb = GLUE_FUNC_CAST(twofish_enc_blk_ctr_3way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(twofish_enc_blk_ctr) }
	} }
};

static const struct common_glue_ctx twofish_dec = {
	.num_funcs = 2,
	.fpu_blocks_limit = -1,

	.funcs = { {
		.num_blocks = 3,
		.fn_u = { .ecb = GLUE_FUNC_CAST(twofish_dec_blk_3way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(twofish_dec_blk) }
	} }
};

static const struct common_glue_ctx twofish_dec_cbc = {
	.num_funcs = 2,
	.fpu_blocks_limit = -1,

	.funcs = { {
		.num_blocks = 3,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(twofish_dec_blk_cbc_3way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(twofish_dec_blk) }
	} }
};

static int ecb_encrypt(struct skcipher_request *req)
{
	return glue_ecb_req_128bit(&twofish_enc, req);
}

static int ecb_decrypt(struct skcipher_request *req)
{
	return glue_ecb_req_128bit(&twofish_dec, req);
}

static int cbc_encrypt(struct skcipher_request *req)
{
	return glue_cbc_encrypt_req_128bit(GLUE_FUNC_CAST(twofish_enc_blk),
					   req);
}

static int cbc_decrypt(struct skcipher_request *req)
{
	return glue_cbc_decrypt_req_128bit(&twofish_dec_cbc, req);
}

static int ctr_crypt(struct skcipher_request *req)
{
	return glue_ctr_req_128bit(&twofish_ctr, req);
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
	}, {
		.base.cra_name		= "ctr(twofish)",
		.base.cra_driver_name	= "ctr-twofish-3way",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct twofish_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= TF_MIN_KEY_SIZE,
		.max_keysize		= TF_MAX_KEY_SIZE,
		.ivsize			= TF_BLOCK_SIZE,
		.chunksize		= TF_BLOCK_SIZE,
		.setkey			= twofish_setkey_skcipher,
		.encrypt		= ctr_crypt,
		.decrypt		= ctr_crypt,
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
