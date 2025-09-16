// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)2006 USAGI/WIDE Project
 *
 * Author:
 * 	Kazunori Miyazawa <miyazawa@linux-ipv6.org>
 */

#include <crypto/internal/cipher.h>
#include <crypto/internal/hash.h>
#include <crypto/utils.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

static u_int32_t ks[12] = {0x01010101, 0x01010101, 0x01010101, 0x01010101,
			   0x02020202, 0x02020202, 0x02020202, 0x02020202,
			   0x03030303, 0x03030303, 0x03030303, 0x03030303};

/*
 * +------------------------
 * | <parent tfm>
 * +------------------------
 * | xcbc_tfm_ctx
 * +------------------------
 * | consts (block size * 2)
 * +------------------------
 */
struct xcbc_tfm_ctx {
	struct crypto_cipher *child;
	u8 consts[];
};

#define XCBC_BLOCKSIZE	16

static int crypto_xcbc_digest_setkey(struct crypto_shash *parent,
				     const u8 *inkey, unsigned int keylen)
{
	struct xcbc_tfm_ctx *ctx = crypto_shash_ctx(parent);
	u8 *consts = ctx->consts;
	int err = 0;
	u8 key1[XCBC_BLOCKSIZE];
	int bs = sizeof(key1);

	if ((err = crypto_cipher_setkey(ctx->child, inkey, keylen)))
		return err;

	crypto_cipher_encrypt_one(ctx->child, consts, (u8 *)ks + bs);
	crypto_cipher_encrypt_one(ctx->child, consts + bs, (u8 *)ks + bs * 2);
	crypto_cipher_encrypt_one(ctx->child, key1, (u8 *)ks);

	return crypto_cipher_setkey(ctx->child, key1, bs);

}

static int crypto_xcbc_digest_init(struct shash_desc *pdesc)
{
	int bs = crypto_shash_blocksize(pdesc->tfm);
	u8 *prev = shash_desc_ctx(pdesc);

	memset(prev, 0, bs);
	return 0;
}

static int crypto_xcbc_digest_update(struct shash_desc *pdesc, const u8 *p,
				     unsigned int len)
{
	struct crypto_shash *parent = pdesc->tfm;
	struct xcbc_tfm_ctx *tctx = crypto_shash_ctx(parent);
	struct crypto_cipher *tfm = tctx->child;
	int bs = crypto_shash_blocksize(parent);
	u8 *prev = shash_desc_ctx(pdesc);

	do {
		crypto_xor(prev, p, bs);
		crypto_cipher_encrypt_one(tfm, prev, prev);
		p += bs;
		len -= bs;
	} while (len >= bs);
	return len;
}

static int crypto_xcbc_digest_finup(struct shash_desc *pdesc, const u8 *src,
				    unsigned int len, u8 *out)
{
	struct crypto_shash *parent = pdesc->tfm;
	struct xcbc_tfm_ctx *tctx = crypto_shash_ctx(parent);
	struct crypto_cipher *tfm = tctx->child;
	int bs = crypto_shash_blocksize(parent);
	u8 *prev = shash_desc_ctx(pdesc);
	unsigned int offset = 0;

	crypto_xor(prev, src, len);
	if (len != bs) {
		prev[len] ^= 0x80;
		offset += bs;
	}
	crypto_xor(prev, &tctx->consts[offset], bs);
	crypto_cipher_encrypt_one(tfm, out, prev);
	return 0;
}

static int xcbc_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_cipher *cipher;
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_cipher_spawn *spawn = crypto_instance_ctx(inst);
	struct xcbc_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	cipher = crypto_spawn_cipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;

	return 0;
};

static void xcbc_exit_tfm(struct crypto_tfm *tfm)
{
	struct xcbc_tfm_ctx *ctx = crypto_tfm_ctx(tfm);
	crypto_free_cipher(ctx->child);
}

static int xcbc_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct shash_instance *inst;
	struct crypto_cipher_spawn *spawn;
	struct crypto_alg *alg;
	u32 mask;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SHASH, &mask);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;
	spawn = shash_instance_ctx(inst);

	err = crypto_grab_cipher(spawn, shash_crypto_instance(inst),
				 crypto_attr_alg_name(tb[1]), 0, mask);
	if (err)
		goto err_free_inst;
	alg = crypto_spawn_cipher_alg(spawn);

	err = -EINVAL;
	if (alg->cra_blocksize != XCBC_BLOCKSIZE)
		goto err_free_inst;

	err = crypto_inst_setname(shash_crypto_instance(inst), tmpl->name, alg);
	if (err)
		goto err_free_inst;

	inst->alg.base.cra_priority = alg->cra_priority;
	inst->alg.base.cra_blocksize = alg->cra_blocksize;
	inst->alg.base.cra_ctxsize = sizeof(struct xcbc_tfm_ctx) +
				     alg->cra_blocksize * 2;
	inst->alg.base.cra_flags = CRYPTO_AHASH_ALG_BLOCK_ONLY |
				   CRYPTO_AHASH_ALG_FINAL_NONZERO;

	inst->alg.digestsize = alg->cra_blocksize;
	inst->alg.descsize = alg->cra_blocksize;

	inst->alg.base.cra_init = xcbc_init_tfm;
	inst->alg.base.cra_exit = xcbc_exit_tfm;

	inst->alg.init = crypto_xcbc_digest_init;
	inst->alg.update = crypto_xcbc_digest_update;
	inst->alg.finup = crypto_xcbc_digest_finup;
	inst->alg.setkey = crypto_xcbc_digest_setkey;

	inst->free = shash_free_singlespawn_instance;

	err = shash_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		shash_free_singlespawn_instance(inst);
	}
	return err;
}

static struct crypto_template crypto_xcbc_tmpl = {
	.name = "xcbc",
	.create = xcbc_create,
	.module = THIS_MODULE,
};

static int __init crypto_xcbc_module_init(void)
{
	return crypto_register_template(&crypto_xcbc_tmpl);
}

static void __exit crypto_xcbc_module_exit(void)
{
	crypto_unregister_template(&crypto_xcbc_tmpl);
}

module_init(crypto_xcbc_module_init);
module_exit(crypto_xcbc_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("XCBC keyed hash algorithm");
MODULE_ALIAS_CRYPTO("xcbc");
MODULE_IMPORT_NS("CRYPTO_INTERNAL");
