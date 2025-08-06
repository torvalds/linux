// SPDX-License-Identifier: GPL-2.0+
/*
 * Cryptographic API.
 *
 * s390 implementation of the SHA512 and SHA38 Secure Hash Algorithm.
 *
 * Copyright IBM Corp. 2007
 * Author(s): Jan Glauber (jang@de.ibm.com)
 */
#include <asm/cpacf.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <linux/cpufeature.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "sha.h"

static int sha512_init(struct shash_desc *desc)
{
	struct s390_sha_ctx *ctx = shash_desc_ctx(desc);

	ctx->sha512.state[0] = SHA512_H0;
	ctx->sha512.state[1] = SHA512_H1;
	ctx->sha512.state[2] = SHA512_H2;
	ctx->sha512.state[3] = SHA512_H3;
	ctx->sha512.state[4] = SHA512_H4;
	ctx->sha512.state[5] = SHA512_H5;
	ctx->sha512.state[6] = SHA512_H6;
	ctx->sha512.state[7] = SHA512_H7;
	ctx->count = 0;
	ctx->sha512.count_hi = 0;
	ctx->func = CPACF_KIMD_SHA_512;

	return 0;
}

static int sha512_export(struct shash_desc *desc, void *out)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);
	struct sha512_state *octx = out;

	octx->count[0] = sctx->count;
	octx->count[1] = sctx->sha512.count_hi;
	memcpy(octx->state, sctx->state, sizeof(octx->state));
	return 0;
}

static int sha512_import(struct shash_desc *desc, const void *in)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);
	const struct sha512_state *ictx = in;

	sctx->count = ictx->count[0];
	sctx->sha512.count_hi = ictx->count[1];

	memcpy(sctx->state, ictx->state, sizeof(ictx->state));
	sctx->func = CPACF_KIMD_SHA_512;
	return 0;
}

static struct shash_alg sha512_alg = {
	.digestsize	=	SHA512_DIGEST_SIZE,
	.init		=	sha512_init,
	.update		=	s390_sha_update_blocks,
	.finup		=	s390_sha_finup,
	.export		=	sha512_export,
	.import		=	sha512_import,
	.descsize	=	sizeof(struct s390_sha_ctx),
	.statesize	=	SHA512_STATE_SIZE,
	.base		=	{
		.cra_name	=	"sha512",
		.cra_driver_name=	"sha512-s390",
		.cra_priority	=	300,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY |
					CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize	=	SHA512_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

MODULE_ALIAS_CRYPTO("sha512");

static int sha384_init(struct shash_desc *desc)
{
	struct s390_sha_ctx *ctx = shash_desc_ctx(desc);

	ctx->sha512.state[0] = SHA384_H0;
	ctx->sha512.state[1] = SHA384_H1;
	ctx->sha512.state[2] = SHA384_H2;
	ctx->sha512.state[3] = SHA384_H3;
	ctx->sha512.state[4] = SHA384_H4;
	ctx->sha512.state[5] = SHA384_H5;
	ctx->sha512.state[6] = SHA384_H6;
	ctx->sha512.state[7] = SHA384_H7;
	ctx->count = 0;
	ctx->sha512.count_hi = 0;
	ctx->func = CPACF_KIMD_SHA_512;

	return 0;
}

static struct shash_alg sha384_alg = {
	.digestsize	=	SHA384_DIGEST_SIZE,
	.init		=	sha384_init,
	.update		=	s390_sha_update_blocks,
	.finup		=	s390_sha_finup,
	.export		=	sha512_export,
	.import		=	sha512_import,
	.descsize	=	sizeof(struct s390_sha_ctx),
	.statesize	=	SHA512_STATE_SIZE,
	.base		=	{
		.cra_name	=	"sha384",
		.cra_driver_name=	"sha384-s390",
		.cra_priority	=	300,
		.cra_blocksize	=	SHA384_BLOCK_SIZE,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY |
					CRYPTO_AHASH_ALG_FINUP_MAX,
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

module_cpu_feature_match(S390_CPU_FEATURE_MSA, init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA512 and SHA-384 Secure Hash Algorithm");
