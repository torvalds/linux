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

struct arc4_ctx {
	u32 S[256];
	u32 x, y;
};

static int arc4_set_key(struct crypto_tfm *tfm, const u8 *in_key,
			unsigned int key_len)
{
	struct arc4_ctx *ctx = crypto_tfm_ctx(tfm);
	int i, j = 0, k = 0;

	ctx->x = 1;
	ctx->y = 0;

	for (i = 0; i < 256; i++)
		ctx->S[i] = i;

	for (i = 0; i < 256; i++) {
		u32 a = ctx->S[i];
		j = (j + in_key[k] + a) & 0xff;
		ctx->S[i] = ctx->S[j];
		ctx->S[j] = a;
		if (++k >= key_len)
			k = 0;
	}

	return 0;
}

static int arc4_set_key_skcipher(struct crypto_skcipher *tfm, const u8 *in_key,
				 unsigned int key_len)
{
	return arc4_set_key(&tfm->base, in_key, key_len);
}

static void arc4_crypt(struct arc4_ctx *ctx, u8 *out, const u8 *in,
		       unsigned int len)
{
	u32 *const S = ctx->S;
	u32 x, y, a, b;
	u32 ty, ta, tb;

	if (len == 0)
		return;

	x = ctx->x;
	y = ctx->y;

	a = S[x];
	y = (y + a) & 0xff;
	b = S[y];

	do {
		S[y] = a;
		a = (a + b) & 0xff;
		S[x] = b;
		x = (x + 1) & 0xff;
		ta = S[x];
		ty = (y + ta) & 0xff;
		tb = S[ty];
		*out++ = *in++ ^ S[a];
		if (--len == 0)
			break;
		y = ty;
		a = ta;
		b = tb;
	} while (true);

	ctx->x = x;
	ctx->y = y;
}

static void arc4_crypt_one(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	arc4_crypt(crypto_tfm_ctx(tfm), out, in, 1);
}

static int ecb_arc4_crypt(struct skcipher_request *req)
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

static struct crypto_alg arc4_cipher = {
	.cra_name		=	"arc4",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	ARC4_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct arc4_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	ARC4_MIN_KEY_SIZE,
			.cia_max_keysize	=	ARC4_MAX_KEY_SIZE,
			.cia_setkey		=	arc4_set_key,
			.cia_encrypt		=	arc4_crypt_one,
			.cia_decrypt		=	arc4_crypt_one,
		},
	},
};

static struct skcipher_alg arc4_skcipher = {
	.base.cra_name		=	"ecb(arc4)",
	.base.cra_priority	=	100,
	.base.cra_blocksize	=	ARC4_BLOCK_SIZE,
	.base.cra_ctxsize	=	sizeof(struct arc4_ctx),
	.base.cra_module	=	THIS_MODULE,
	.min_keysize		=	ARC4_MIN_KEY_SIZE,
	.max_keysize		=	ARC4_MAX_KEY_SIZE,
	.setkey			=	arc4_set_key_skcipher,
	.encrypt		=	ecb_arc4_crypt,
	.decrypt		=	ecb_arc4_crypt,
};

static int __init arc4_init(void)
{
	int err;

	err = crypto_register_alg(&arc4_cipher);
	if (err)
		return err;

	err = crypto_register_skcipher(&arc4_skcipher);
	if (err)
		crypto_unregister_alg(&arc4_cipher);
	return err;
}

static void __exit arc4_exit(void)
{
	crypto_unregister_alg(&arc4_cipher);
	crypto_unregister_skcipher(&arc4_skcipher);
}

subsys_initcall(arc4_init);
module_exit(arc4_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ARC4 Cipher Algorithm");
MODULE_AUTHOR("Jon Oberheide <jon@oberheide.org>");
MODULE_ALIAS_CRYPTO("arc4");
