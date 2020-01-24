// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API
 *
 * ARC4 Cipher Algorithm
 *
 * Jon Oberheide <jon@oberheide.org>
 */

#include <crypto/algapi.h>
#include <crypto/arc4.h>
#include <crypto/internal/skcipher.h>
#include <linux/init.h>
#include <linux/module.h>

static int crypto_arc4_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			      unsigned int key_len)
{
	struct arc4_ctx *ctx = crypto_skcipher_ctx(tfm);

	return arc4_setkey(ctx, in_key, key_len);
}

static int crypto_arc4_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct arc4_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes > 0) {
		arc4_crypt(ctx, walk.dst.virt.addr, walk.src.virt.addr,
			   walk.nbytes);
		err = skcipher_walk_done(&walk, 0);
	}

	return err;
}

static struct skcipher_alg arc4_alg = {
	/*
	 * For legacy reasons, this is named "ecb(arc4)", not "arc4".
	 * Nevertheless it's actually a stream cipher, not a block cipher.
	 */
	.base.cra_name		=	"ecb(arc4)",
	.base.cra_driver_name	=	"ecb(arc4)-generic",
	.base.cra_priority	=	100,
	.base.cra_blocksize	=	ARC4_BLOCK_SIZE,
	.base.cra_ctxsize	=	sizeof(struct arc4_ctx),
	.base.cra_module	=	THIS_MODULE,
	.min_keysize		=	ARC4_MIN_KEY_SIZE,
	.max_keysize		=	ARC4_MAX_KEY_SIZE,
	.setkey			=	crypto_arc4_setkey,
	.encrypt		=	crypto_arc4_crypt,
	.decrypt		=	crypto_arc4_crypt,
};

static int __init arc4_init(void)
{
	return crypto_register_skcipher(&arc4_alg);
}

static void __exit arc4_exit(void)
{
	crypto_unregister_skcipher(&arc4_alg);
}

subsys_initcall(arc4_init);
module_exit(arc4_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ARC4 Cipher Algorithm");
MODULE_AUTHOR("Jon Oberheide <jon@oberheide.org>");
MODULE_ALIAS_CRYPTO("ecb(arc4)");
