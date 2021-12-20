// SPDX-License-Identifier: GPL-2.0+
/*
 * Cryptographic API.
 *
 * s390 implementation of the SHA512 and SHA38 Secure Hash Algorithm.
 *
 * Copyright IBM Corp. 2007
 * Author(s): Jan Glauber (jang@de.ibm.com)
 */
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cpufeature.h>
#include <asm/cpacf.h>

#include "sha.h"

static int sha512_init(struct shash_desc *desc)
{
	struct s390_sha_ctx *ctx = shash_desc_ctx(desc);

	*(__u64 *)&ctx->state[0] = SHA512_H0;
	*(__u64 *)&ctx->state[2] = SHA512_H1;
	*(__u64 *)&ctx->state[4] = SHA512_H2;
	*(__u64 *)&ctx->state[6] = SHA512_H3;
	*(__u64 *)&ctx->state[8] = SHA512_H4;
	*(__u64 *)&ctx->state[10] = SHA512_H5;
	*(__u64 *)&ctx->state[12] = SHA512_H6;
	*(__u64 *)&ctx->state[14] = SHA512_H7;
	ctx->count = 0;
	ctx->func = CPACF_KIMD_SHA_512;

	return 0;
}

static int sha512_export(struct shash_desc *desc, void *out)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);
	struct sha512_state *octx = out;

	octx->count[0] = sctx->count;
	octx->count[1] = 0;
	memcpy(octx->state, sctx->state, sizeof(octx->state));
	memcpy(octx->buf, sctx->buf, sizeof(octx->buf));
	return 0;
}

static int sha512_import(struct shash_desc *desc, const void *in)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);
	const struct sha512_state *ictx = in;

	if (unlikely(ictx->count[1]))
		return -ERANGE;
	sctx->count = ictx->count[0];

	memcpy(sctx->state, ictx->state, sizeof(ictx->state));
	memcpy(sctx->buf, ictx->buf, sizeof(ictx->buf));
	sctx->func = CPACF_KIMD_SHA_512;
	return 0;
}

static struct shash_alg sha512_alg = {
	.digestsize	=	SHA512_DIGEST_SIZE,
	.init		=	sha512_init,
	.update		=	s390_sha_update,
	.final		=	s390_sha_final,
	.export		=	sha512_export,
	.import		=	sha512_import,
	.descsize	=	sizeof(struct s390_sha_ctx),
	.statesize	=	sizeof(struct sha512_state),
	.base		=	{
		.cra_name	=	"sha512",
		.cra_driver_name=	"sha512-s390",
		.cra_priority	=	300,
		.cra_blocksize	=	SHA512_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

MODULE_ALIAS_CRYPTO("sha512");

static int sha384_init(struct shash_desc *desc)
{
	struct s390_sha_ctx *ctx = shash_desc_ctx(desc);

	*(__u64 *)&ctx->state[0] = SHA384_H0;
	*(__u64 *)&ctx->state[2] = SHA384_H1;
	*(__u64 *)&ctx->state[4] = SHA384_H2;
	*(__u64 *)&ctx->state[6] = SHA384_H3;
	*(__u64 *)&ctx->state[8] = SHA384_H4;
	*(__u64 *)&ctx->state[10] = SHA384_H5;
	*(__u64 *)&ctx->state[12] = SHA384_H6;
	*(__u64 *)&ctx->state[14] = SHA384_H7;
	ctx->count = 0;
	ctx->func = CPACF_KIMD_SHA_512;

	return 0;
}

static struct shash_alg sha384_alg = {
	.digestsize	=	SHA384_DIGEST_SIZE,
	.init		=	sha384_init,
	.update		=	s390_sha_update,
	.final		=	s390_sha_final,
	.export		=	sha512_export,
	.import		=	sha512_import,
	.descsize	=	sizeof(struct s390_sha_ctx),
	.statesize	=	sizeof(struct sha512_state),
	.base		=	{
		.cra_name	=	"sha384",
		.cra_driver_name=	"sha384-s390",
		.cra_priority	=	300,
		.cra_blocksize	=	SHA384_BLOCK_SIZE,
		.cra_ctxsize	=	sizeof(struct s390_sha_ctx),
		.cra_module	=	THIS_MODULE,
	}
};

MODULE_ALIAS_CRYPTO("sha384");

static int __init init(void)
{
	int ret;

	if (!cpacf_query_func(CPACF_KIMD, CPACF_KIMD_SHA_512))
		return -ENODEV;
	if ((ret = crypto_register_shash(&sha512_alg)) < 0)
		goto out;
	if ((ret = crypto_register_shash(&sha384_alg)) < 0)
		crypto_unregister_shash(&sha512_alg);
out:
	return ret;
}

static void __exit fini(void)
{
	crypto_unregister_shash(&sha512_alg);
	crypto_unregister_shash(&sha384_alg);
}

module_cpu_feature_match(MSA, init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA512 and SHA-384 Secure Hash Algorithm");
