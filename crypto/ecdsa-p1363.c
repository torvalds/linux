// SPDX-License-Identifier: GPL-2.0
/*
 * ECDSA P1363 signature encoding
 *
 * Copyright (c) 2024 Intel Corporation
 */

#include <linux/err.h>
#include <linux/module.h>
#include <crypto/algapi.h>
#include <crypto/sig.h>
#include <crypto/internal/ecc.h>
#include <crypto/internal/sig.h>

struct ecdsa_p1363_ctx {
	struct crypto_sig *child;
};

static int ecdsa_p1363_verify(struct crypto_sig *tfm,
			      const void *src, unsigned int slen,
			      const void *digest, unsigned int dlen)
{
	struct ecdsa_p1363_ctx *ctx = crypto_sig_ctx(tfm);
	unsigned int keylen = crypto_sig_keysize(ctx->child);
	unsigned int ndigits = DIV_ROUND_UP_POW2(keylen, sizeof(u64));
	struct ecdsa_raw_sig sig;

	if (slen != 2 * keylen)
		return -EINVAL;

	ecc_digits_from_bytes(src, keylen, sig.r, ndigits);
	ecc_digits_from_bytes(src + keylen, keylen, sig.s, ndigits);

	return crypto_sig_verify(ctx->child, &sig, sizeof(sig), digest, dlen);
}

static unsigned int ecdsa_p1363_key_size(struct crypto_sig *tfm)
{
	struct ecdsa_p1363_ctx *ctx = crypto_sig_ctx(tfm);

	return crypto_sig_keysize(ctx->child);
}

static unsigned int ecdsa_p1363_max_size(struct crypto_sig *tfm)
{
	struct ecdsa_p1363_ctx *ctx = crypto_sig_ctx(tfm);

	return 2 * crypto_sig_keysize(ctx->child);
}

static unsigned int ecdsa_p1363_digest_size(struct crypto_sig *tfm)
{
	struct ecdsa_p1363_ctx *ctx = crypto_sig_ctx(tfm);

	return crypto_sig_digestsize(ctx->child);
}

static int ecdsa_p1363_set_pub_key(struct crypto_sig *tfm,
				   const void *key, unsigned int keylen)
{
	struct ecdsa_p1363_ctx *ctx = crypto_sig_ctx(tfm);

	return crypto_sig_set_pubkey(ctx->child, key, keylen);
}

static int ecdsa_p1363_init_tfm(struct crypto_sig *tfm)
{
	struct sig_instance *inst = sig_alg_instance(tfm);
	struct crypto_sig_spawn *spawn = sig_instance_ctx(inst);
	struct ecdsa_p1363_ctx *ctx = crypto_sig_ctx(tfm);
	struct crypto_sig *child_tfm;

	child_tfm = crypto_spawn_sig(spawn);
	if (IS_ERR(child_tfm))
		return PTR_ERR(child_tfm);

	ctx->child = child_tfm;

	return 0;
}

static void ecdsa_p1363_exit_tfm(struct crypto_sig *tfm)
{
	struct ecdsa_p1363_ctx *ctx = crypto_sig_ctx(tfm);

	crypto_free_sig(ctx->child);
}

static void ecdsa_p1363_free(struct sig_instance *inst)
{
	struct crypto_sig_spawn *spawn = sig_instance_ctx(inst);

	crypto_drop_sig(spawn);
	kfree(inst);
}

static int ecdsa_p1363_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct crypto_sig_spawn *spawn;
	struct sig_instance *inst;
	struct sig_alg *ecdsa_alg;
	u32 mask;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SIG, &mask);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	spawn = sig_instance_ctx(inst);

	err = crypto_grab_sig(spawn, sig_crypto_instance(inst),
			      crypto_attr_alg_name(tb[1]), 0, mask);
	if (err)
		goto err_free_inst;

	ecdsa_alg = crypto_spawn_sig_alg(spawn);

	err = -EINVAL;
	if (strncmp(ecdsa_alg->base.cra_name, "ecdsa", 5) != 0)
		goto err_free_inst;

	err = crypto_inst_setname(sig_crypto_instance(inst), tmpl->name,
				  &ecdsa_alg->base);
	if (err)
		goto err_free_inst;

	inst->alg.base.cra_priority = ecdsa_alg->base.cra_priority;
	inst->alg.base.cra_ctxsize = sizeof(struct ecdsa_p1363_ctx);

	inst->alg.init = ecdsa_p1363_init_tfm;
	inst->alg.exit = ecdsa_p1363_exit_tfm;

	inst->alg.verify = ecdsa_p1363_verify;
	inst->alg.key_size = ecdsa_p1363_key_size;
	inst->alg.max_size = ecdsa_p1363_max_size;
	inst->alg.digest_size = ecdsa_p1363_digest_size;
	inst->alg.set_pub_key = ecdsa_p1363_set_pub_key;

	inst->free = ecdsa_p1363_free;

	err = sig_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		ecdsa_p1363_free(inst);
	}
	return err;
}

struct crypto_template ecdsa_p1363_tmpl = {
	.name = "p1363",
	.create = ecdsa_p1363_create,
	.module = THIS_MODULE,
};

MODULE_ALIAS_CRYPTO("p1363");
