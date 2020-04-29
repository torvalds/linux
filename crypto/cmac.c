// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CMAC: Cipher Block Mode for Authentication
 *
 * Copyright © 2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 *
 * Based on work by:
 *  Copyright © 2013 Tom St Denis <tstdenis@elliptictech.com>
 * Based on crypto/xcbc.c:
 *  Copyright © 2006 USAGI/WIDE Project,
 *   Author: Kazunori Miyazawa <miyazawa@linux-ipv6.org>
 */

#include <crypto/internal/hash.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>

/*
 * +------------------------
 * | <parent tfm>
 * +------------------------
 * | cmac_tfm_ctx
 * +------------------------
 * | consts (block size * 2)
 * +------------------------
 */
struct cmac_tfm_ctx {
	struct crypto_cipher *child;
	u8 ctx[];
};

/*
 * +------------------------
 * | <shash desc>
 * +------------------------
 * | cmac_desc_ctx
 * +------------------------
 * | odds (block size)
 * +------------------------
 * | prev (block size)
 * +------------------------
 */
struct cmac_desc_ctx {
	unsigned int len;
	u8 ctx[];
};

static int crypto_cmac_digest_setkey(struct crypto_shash *parent,
				     const u8 *inkey, unsigned int keylen)
{
	unsigned long alignmask = crypto_shash_alignmask(parent);
	struct cmac_tfm_ctx *ctx = crypto_shash_ctx(parent);
	unsigned int bs = crypto_shash_blocksize(parent);
	__be64 *consts = PTR_ALIGN((void *)ctx->ctx,
				   (alignmask | (__alignof__(__be64) - 1)) + 1);
	u64 _const[2];
	int i, err = 0;
	u8 msb_mask, gfmask;

	err = crypto_cipher_setkey(ctx->child, inkey, keylen);
	if (err)
		return err;

	/* encrypt the zero block */
	memset(consts, 0, bs);
	crypto_cipher_encrypt_one(ctx->child, (u8 *)consts, (u8 *)consts);

	switch (bs) {
	case 16:
		gfmask = 0x87;
		_const[0] = be64_to_cpu(consts[1]);
		_const[1] = be64_to_cpu(consts[0]);

		/* gf(2^128) multiply zero-ciphertext with u and u^2 */
		for (i = 0; i < 4; i += 2) {
			msb_mask = ((s64)_const[1] >> 63) & gfmask;
			_const[1] = (_const[1] << 1) | (_const[0] >> 63);
			_const[0] = (_const[0] << 1) ^ msb_mask;

			consts[i + 0] = cpu_to_be64(_const[1]);
			consts[i + 1] = cpu_to_be64(_const[0]);
		}

		break;
	case 8:
		gfmask = 0x1B;
		_const[0] = be64_to_cpu(consts[0]);

		/* gf(2^64) multiply zero-ciphertext with u and u^2 */
		for (i = 0; i < 2; i++) {
			msb_mask = ((s64)_const[0] >> 63) & gfmask;
			_const[0] = (_const[0] << 1) ^ msb_mask;

			consts[i] = cpu_to_be64(_const[0]);
		}

		break;
	}

	return 0;
}

static int crypto_cmac_digest_init(struct shash_desc *pdesc)
{
	unsigned long alignmask = crypto_shash_alignmask(pdesc->tfm);
	struct cmac_desc_ctx *ctx = shash_desc_ctx(pdesc);
	int bs = crypto_shash_blocksize(pdesc->tfm);
	u8 *prev = PTR_ALIGN((void *)ctx->ctx, alignmask + 1) + bs;

	ctx->len = 0;
	memset(prev, 0, bs);

	return 0;
}

static int crypto_cmac_digest_update(struct shash_desc *pdesc, const u8 *p,
				     unsigned int len)
{
	struct crypto_shash *parent = pdesc->tfm;
	unsigned long alignmask = crypto_shash_alignmask(parent);
	struct cmac_tfm_ctx *tctx = crypto_shash_ctx(parent);
	struct cmac_desc_ctx *ctx = shash_desc_ctx(pdesc);
	struct crypto_cipher *tfm = tctx->child;
	int bs = crypto_shash_blocksize(parent);
	u8 *odds = PTR_ALIGN((void *)ctx->ctx, alignmask + 1);
	u8 *prev = odds + bs;

	/* checking the data can fill the block */
	if ((ctx->len + len) <= bs) {
		memcpy(odds + ctx->len, p, len);
		ctx->len += len;
		return 0;
	}

	/* filling odds with new data and encrypting it */
	memcpy(odds + ctx->len, p, bs - ctx->len);
	len -= bs - ctx->len;
	p += bs - ctx->len;

	crypto_xor(prev, odds, bs);
	crypto_cipher_encrypt_one(tfm, prev, prev);

	/* clearing the length */
	ctx->len = 0;

	/* encrypting the rest of data */
	while (len > bs) {
		crypto_xor(prev, p, bs);
		crypto_cipher_encrypt_one(tfm, prev, prev);
		p += bs;
		len -= bs;
	}

	/* keeping the surplus of blocksize */
	if (len) {
		memcpy(odds, p, len);
		ctx->len = len;
	}

	return 0;
}

static int crypto_cmac_digest_final(struct shash_desc *pdesc, u8 *out)
{
	struct crypto_shash *parent = pdesc->tfm;
	unsigned long alignmask = crypto_shash_alignmask(parent);
	struct cmac_tfm_ctx *tctx = crypto_shash_ctx(parent);
	struct cmac_desc_ctx *ctx = shash_desc_ctx(pdesc);
	struct crypto_cipher *tfm = tctx->child;
	int bs = crypto_shash_blocksize(parent);
	u8 *consts = PTR_ALIGN((void *)tctx->ctx,
			       (alignmask | (__alignof__(__be64) - 1)) + 1);
	u8 *odds = PTR_ALIGN((void *)ctx->ctx, alignmask + 1);
	u8 *prev = odds + bs;
	unsigned int offset = 0;

	if (ctx->len != bs) {
		unsigned int rlen;
		u8 *p = odds + ctx->len;

		*p = 0x80;
		p++;

		rlen = bs - ctx->len - 1;
		if (rlen)
			memset(p, 0, rlen);

		offset += bs;
	}

	crypto_xor(prev, odds, bs);
	crypto_xor(prev, consts + offset, bs);

	crypto_cipher_encrypt_one(tfm, out, prev);

	return 0;
}

static int cmac_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_cipher *cipher;
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_cipher_spawn *spawn = crypto_instance_ctx(inst);
	struct cmac_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	cipher = crypto_spawn_cipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;

	return 0;
};

static void cmac_exit_tfm(struct crypto_tfm *tfm)
{
	struct cmac_tfm_ctx *ctx = crypto_tfm_ctx(tfm);
	crypto_free_cipher(ctx->child);
}

static int cmac_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct shash_instance *inst;
	struct crypto_cipher_spawn *spawn;
	struct crypto_alg *alg;
	unsigned long alignmask;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SHASH);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;
	spawn = shash_instance_ctx(inst);

	err = crypto_grab_cipher(spawn, shash_crypto_instance(inst),
				 crypto_attr_alg_name(tb[1]), 0, 0);
	if (err)
		goto err_free_inst;
	alg = crypto_spawn_cipher_alg(spawn);

	switch (alg->cra_blocksize) {
	case 16:
	case 8:
		break;
	default:
		err = -EINVAL;
		goto err_free_inst;
	}

	err = crypto_inst_setname(shash_crypto_instance(inst), tmpl->name, alg);
	if (err)
		goto err_free_inst;

	alignmask = alg->cra_alignmask;
	inst->alg.base.cra_alignmask = alignmask;
	inst->alg.base.cra_priority = alg->cra_priority;
	inst->alg.base.cra_blocksize = alg->cra_blocksize;

	inst->alg.digestsize = alg->cra_blocksize;
	inst->alg.descsize =
		ALIGN(sizeof(struct cmac_desc_ctx), crypto_tfm_ctx_alignment())
		+ (alignmask & ~(crypto_tfm_ctx_alignment() - 1))
		+ alg->cra_blocksize * 2;

	inst->alg.base.cra_ctxsize =
		ALIGN(sizeof(struct cmac_tfm_ctx), crypto_tfm_ctx_alignment())
		+ ((alignmask | (__alignof__(__be64) - 1)) &
		   ~(crypto_tfm_ctx_alignment() - 1))
		+ alg->cra_blocksize * 2;

	inst->alg.base.cra_init = cmac_init_tfm;
	inst->alg.base.cra_exit = cmac_exit_tfm;

	inst->alg.init = crypto_cmac_digest_init;
	inst->alg.update = crypto_cmac_digest_update;
	inst->alg.final = crypto_cmac_digest_final;
	inst->alg.setkey = crypto_cmac_digest_setkey;

	inst->free = shash_free_singlespawn_instance;

	err = shash_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		shash_free_singlespawn_instance(inst);
	}
	return err;
}

static struct crypto_template crypto_cmac_tmpl = {
	.name = "cmac",
	.create = cmac_create,
	.module = THIS_MODULE,
};

static int __init crypto_cmac_module_init(void)
{
	return crypto_register_template(&crypto_cmac_tmpl);
}

static void __exit crypto_cmac_module_exit(void)
{
	crypto_unregister_template(&crypto_cmac_tmpl);
}

subsys_initcall(crypto_cmac_module_init);
module_exit(crypto_cmac_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CMAC keyed hash algorithm");
MODULE_ALIAS_CRYPTO("cmac");
