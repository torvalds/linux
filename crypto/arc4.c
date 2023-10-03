// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API
 *
 * ARC4 Cipher Algorithm
 *
 * Jon Oberheide <jon@oberheide.org>
 */

#include <crypto/arc4.h>
#include <crypto/internal/skcipher.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>

static int crypto_arc4_setkey(struct crypto_lskcipher *tfm, const u8 *in_key,
			      unsigned int key_len)
{
	struct arc4_ctx *ctx = crypto_lskcipher_ctx(tfm);

	return arc4_setkey(ctx, in_key, key_len);
}

static int crypto_arc4_crypt(struct crypto_lskcipher *tfm, const u8 *src,
			     u8 *dst, unsigned nbytes, u8 *iv, bool final)
{
	struct arc4_ctx *ctx = crypto_lskcipher_ctx(tfm);

	arc4_crypt(ctx, dst, src, nbytes);
	return 0;
}

static int crypto_arc4_init(struct crypto_lskcipher *tfm)
{
	pr_warn_ratelimited("\"%s\" (%ld) uses obsolete ecb(arc4) skcipher\n",
			    current->comm, (unsigned long)current->pid);

	return 0;
}

static struct lskcipher_alg arc4_alg = {
	.co.base.cra_name		=	"arc4",
	.co.base.cra_driver_name	=	"arc4-generic",
	.co.base.cra_priority		=	100,
	.co.base.cra_blocksize		=	ARC4_BLOCK_SIZE,
	.co.base.cra_ctxsize		=	sizeof(struct arc4_ctx),
	.co.base.cra_module		=	THIS_MODULE,
	.co.min_keysize			=	ARC4_MIN_KEY_SIZE,
	.co.max_keysize			=	ARC4_MAX_KEY_SIZE,
	.setkey				=	crypto_arc4_setkey,
	.encrypt			=	crypto_arc4_crypt,
	.decrypt			=	crypto_arc4_crypt,
	.init				=	crypto_arc4_init,
};

static int __init arc4_init(void)
{
	return crypto_register_lskcipher(&arc4_alg);
}

static void __exit arc4_exit(void)
{
	crypto_unregister_lskcipher(&arc4_alg);
}

subsys_initcall(arc4_init);
module_exit(arc4_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ARC4 Cipher Algorithm");
MODULE_AUTHOR("Jon Oberheide <jon@oberheide.org>");
MODULE_ALIAS_CRYPTO("ecb(arc4)");
