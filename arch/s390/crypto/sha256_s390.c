// SPDX-License-Identifier: GPL-2.0+
/*
 * Cryptographic API.
 *
 * s390 implementation of the SHA256 and SHA224 Secure Hash Algorithm.
 *
 * s390 Version:
 *   Copyright IBM Corp. 2005, 2011
 *   Author(s): Jan Glauber (jang@de.ibm.com)
 */
#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cpufeature.h>
#include <crypto/sha2.h>
#include <asm/cpacf.h>

#include "sha.h"

static int s390_sha256_init(struct shash_desc *desc)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SHA256_H0;
	sctx->state[1] = SHA256_H1;
	sctx->state[2] = SHA256_H2;
	sctx->state[3] = SHA256_H3;
	sctx->state[4] = SHA256_H4;
	sctx->state[5] = SHA256_H5;
	sctx->state[6] = SHA256_H6;
	sctx->state[7] = SHA256_H7;
	sctx->count = 0;
	sctx->func = CPACF_KIMD_SHA_256;

	return 0;
}

static int sha256_export(struct shash_desc *desc, void *out)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);
	struct sha256_state *octx = out;

	octx->count = sctx->count;
	memcpy(octx->state, sctx->state, sizeof(octx->state));
	memcpy(octx->buf, sctx->buf, sizeof(octx->buf));
	return 0;
}

static int sha256_import(struct shash_desc *desc, const void *in)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);
	const struct sha256_state *ictx = in;

	sctx->count = ictx->count;
	memcpy(sctx->state, ictx->state, sizeof(ictx->state));
	memcpy(sctx->buf, ictx->buf, sizeof(ictx->buf));
	sctx->func = CPACF_KIMD_SHA_256;
	return 0;
}

static struct shash_alg sha256_alg = {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	s390_sha256_init,
	.update		=	s390_sha_update,
	.final		=	s390_sha_final,
	.export		=	sha256_export,
	.import		=	sha256_import,
	.descsize	=	sizeof(struct s390_sha_ctx),
	.statesize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name=	"sha256-s390",
		.cra_priority	=	300,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int s390_sha224_init(struct shash_desc *desc)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SHA224_H0;
	sctx->state[1] = SHA224_H1;
	sctx->state[2] = SHA224_H2;
	sctx->state[3] = SHA224_H3;
	sctx->state[4] = SHA224_H4;
	sctx->state[5] = SHA224_H5;
	sctx->state[6] = SHA224_H6;
	sctx->state[7] = SHA224_H7;
	sctx->count = 0;
	sctx->func = CPACF_KIMD_SHA_256;

	return 0;
}

static struct shash_alg sha224_alg = {
	.digestsize	=	SHA224_DIGEST_SIZE,
	.init		=	s390_sha224_init,
	.update		=	s390_sha_update,
	.final		=	s390_sha_final,
	.export		=	sha256_export,
	.import		=	sha256_import,
	.descsize	=	sizeof(struct s390_sha_ctx),
	.statesize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha224",
		.cra_driver_name=	"sha224-s390",
		.cra_priority	=	300,
		.cra_blocksize	=	SHA224_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int __init sha256_s390_init(void)
{
	int ret;

	if (!cpacf_query_func(CPACF_KIMD, CPACF_KIMD_SHA_256))
		return -ENODEV;
	ret = crypto_register_shash(&sha256_alg);
	if (ret < 0)
		goto out;
	ret = crypto_register_shash(&sha224_alg);
	if (ret < 0)
		crypto_unregister_shash(&sha256_alg);
out:
	return ret;
}

static void __exit sha256_s390_fini(void)
{
	crypto_unregister_shash(&sha224_alg);
	crypto_unregister_shash(&sha256_alg);
}

module_cpu_feature_match(S390_CPU_FEATURE_MSA, sha256_s390_init);
module_exit(sha256_s390_fini);

MODULE_ALIAS_CRYPTO("sha256");
MODULE_ALIAS_CRYPTO("sha224");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA256 and SHA224 Secure Hash Algorithm");
