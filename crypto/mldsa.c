// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * crypto_sig wrapper around ML-DSA library.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <crypto/internal/sig.h>
#include <crypto/mldsa.h>

struct crypto_mldsa_ctx {
	u8 pk[MAX(MAX(MLDSA44_PUBLIC_KEY_SIZE,
		      MLDSA65_PUBLIC_KEY_SIZE),
		  MLDSA87_PUBLIC_KEY_SIZE)];
	unsigned int pk_len;
	enum mldsa_alg strength;
	bool key_set;
};

static int crypto_mldsa_sign(struct crypto_sig *tfm,
			     const void *msg, unsigned int msg_len,
			     void *sig, unsigned int sig_len)
{
	return -EOPNOTSUPP;
}

static int crypto_mldsa_verify(struct crypto_sig *tfm,
			       const void *sig, unsigned int sig_len,
			       const void *msg, unsigned int msg_len)
{
	const struct crypto_mldsa_ctx *ctx = crypto_sig_ctx(tfm);

	if (unlikely(!ctx->key_set))
		return -EINVAL;

	return mldsa_verify(ctx->strength, sig, sig_len, msg, msg_len,
			    ctx->pk, ctx->pk_len);
}

static unsigned int crypto_mldsa_key_size(struct crypto_sig *tfm)
{
	struct crypto_mldsa_ctx *ctx = crypto_sig_ctx(tfm);

	switch (ctx->strength) {
	case MLDSA44:
		return MLDSA44_PUBLIC_KEY_SIZE;
	case MLDSA65:
		return MLDSA65_PUBLIC_KEY_SIZE;
	case MLDSA87:
		return MLDSA87_PUBLIC_KEY_SIZE;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}
}

static int crypto_mldsa_set_pub_key(struct crypto_sig *tfm,
				    const void *key, unsigned int keylen)
{
	struct crypto_mldsa_ctx *ctx = crypto_sig_ctx(tfm);
	unsigned int expected_len = crypto_mldsa_key_size(tfm);

	if (keylen != expected_len)
		return -EINVAL;

	ctx->pk_len = keylen;
	memcpy(ctx->pk, key, keylen);
	ctx->key_set = true;
	return 0;
}

static int crypto_mldsa_set_priv_key(struct crypto_sig *tfm,
				     const void *key, unsigned int keylen)
{
	return -EOPNOTSUPP;
}

static unsigned int crypto_mldsa_max_size(struct crypto_sig *tfm)
{
	struct crypto_mldsa_ctx *ctx = crypto_sig_ctx(tfm);

	switch (ctx->strength) {
	case MLDSA44:
		return MLDSA44_SIGNATURE_SIZE;
	case MLDSA65:
		return MLDSA65_SIGNATURE_SIZE;
	case MLDSA87:
		return MLDSA87_SIGNATURE_SIZE;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}
}

static int crypto_mldsa44_alg_init(struct crypto_sig *tfm)
{
	struct crypto_mldsa_ctx *ctx = crypto_sig_ctx(tfm);

	ctx->strength = MLDSA44;
	ctx->key_set = false;
	return 0;
}

static int crypto_mldsa65_alg_init(struct crypto_sig *tfm)
{
	struct crypto_mldsa_ctx *ctx = crypto_sig_ctx(tfm);

	ctx->strength = MLDSA65;
	ctx->key_set = false;
	return 0;
}

static int crypto_mldsa87_alg_init(struct crypto_sig *tfm)
{
	struct crypto_mldsa_ctx *ctx = crypto_sig_ctx(tfm);

	ctx->strength = MLDSA87;
	ctx->key_set = false;
	return 0;
}

static void crypto_mldsa_alg_exit(struct crypto_sig *tfm)
{
}

static struct sig_alg crypto_mldsa_algs[] = {
	{
		.sign			= crypto_mldsa_sign,
		.verify			= crypto_mldsa_verify,
		.set_pub_key		= crypto_mldsa_set_pub_key,
		.set_priv_key		= crypto_mldsa_set_priv_key,
		.key_size		= crypto_mldsa_key_size,
		.max_size		= crypto_mldsa_max_size,
		.init			= crypto_mldsa44_alg_init,
		.exit			= crypto_mldsa_alg_exit,
		.base.cra_name		= "mldsa44",
		.base.cra_driver_name	= "mldsa44-lib",
		.base.cra_ctxsize	= sizeof(struct crypto_mldsa_ctx),
		.base.cra_module	= THIS_MODULE,
		.base.cra_priority	= 5000,
	}, {
		.sign			= crypto_mldsa_sign,
		.verify			= crypto_mldsa_verify,
		.set_pub_key		= crypto_mldsa_set_pub_key,
		.set_priv_key		= crypto_mldsa_set_priv_key,
		.key_size		= crypto_mldsa_key_size,
		.max_size		= crypto_mldsa_max_size,
		.init			= crypto_mldsa65_alg_init,
		.exit			= crypto_mldsa_alg_exit,
		.base.cra_name		= "mldsa65",
		.base.cra_driver_name	= "mldsa65-lib",
		.base.cra_ctxsize	= sizeof(struct crypto_mldsa_ctx),
		.base.cra_module	= THIS_MODULE,
		.base.cra_priority	= 5000,
	}, {
		.sign			= crypto_mldsa_sign,
		.verify			= crypto_mldsa_verify,
		.set_pub_key		= crypto_mldsa_set_pub_key,
		.set_priv_key		= crypto_mldsa_set_priv_key,
		.key_size		= crypto_mldsa_key_size,
		.max_size		= crypto_mldsa_max_size,
		.init			= crypto_mldsa87_alg_init,
		.exit			= crypto_mldsa_alg_exit,
		.base.cra_name		= "mldsa87",
		.base.cra_driver_name	= "mldsa87-lib",
		.base.cra_ctxsize	= sizeof(struct crypto_mldsa_ctx),
		.base.cra_module	= THIS_MODULE,
		.base.cra_priority	= 5000,
	},
};

static int __init mldsa_init(void)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(crypto_mldsa_algs); i++) {
		ret = crypto_register_sig(&crypto_mldsa_algs[i]);
		if (ret < 0)
			goto error;
	}
	return 0;

error:
	pr_err("Failed to register (%d)\n", ret);
	for (i--; i >= 0; i--)
		crypto_unregister_sig(&crypto_mldsa_algs[i]);
	return ret;
}
module_init(mldsa_init);

static void mldsa_exit(void)
{
	for (int i = 0; i < ARRAY_SIZE(crypto_mldsa_algs); i++)
		crypto_unregister_sig(&crypto_mldsa_algs[i]);
}
module_exit(mldsa_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Crypto API support for ML-DSA signature verification");
MODULE_ALIAS_CRYPTO("mldsa44");
MODULE_ALIAS_CRYPTO("mldsa65");
MODULE_ALIAS_CRYPTO("mldsa87");
