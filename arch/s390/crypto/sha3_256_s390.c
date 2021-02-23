// SPDX-License-Identifier: GPL-2.0+
/*
 * Cryptographic API.
 *
 * s390 implementation of the SHA256 and SHA224 Secure Hash Algorithm.
 *
 * s390 Version:
 *   Copyright IBM Corp. 2019
 *   Author(s): Joerg Schmidbauer (jschmidb@de.ibm.com)
 */
#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cpufeature.h>
#include <crypto/sha3.h>
#include <asm/cpacf.h>

#include "sha.h"

static int sha3_256_init(struct shash_desc *desc)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);

	memset(sctx->state, 0, sizeof(sctx->state));
	sctx->count = 0;
	sctx->func = CPACF_KIMD_SHA3_256;

	return 0;
}

static int sha3_256_export(struct shash_desc *desc, void *out)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);
	struct sha3_state *octx = out;

	octx->rsiz = sctx->count;
	memcpy(octx->st, sctx->state, sizeof(octx->st));
	memcpy(octx->buf, sctx->buf, sizeof(octx->buf));

	return 0;
}

static int sha3_256_import(struct shash_desc *desc, const void *in)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);
	const struct sha3_state *ictx = in;

	sctx->count = ictx->rsiz;
	memcpy(sctx->state, ictx->st, sizeof(ictx->st));
	memcpy(sctx->buf, ictx->buf, sizeof(ictx->buf));
	sctx->func = CPACF_KIMD_SHA3_256;

	return 0;
}

static int sha3_224_import(struct shash_desc *desc, const void *in)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);
	const struct sha3_state *ictx = in;

	sctx->count = ictx->rsiz;
	memcpy(sctx->state, ictx->st, sizeof(ictx->st));
	memcpy(sctx->buf, ictx->buf, sizeof(ictx->buf));
	sctx->func = CPACF_KIMD_SHA3_224;

	return 0;
}

static struct shash_alg sha3_256_alg = {
	.digestsize	=	SHA3_256_DIGEST_SIZE,	   /* = 32 */
	.init		=	sha3_256_init,
	.update		=	s390_sha_update,
	.final		=	s390_sha_final,
	.export		=	sha3_256_export,
	.import		=	sha3_256_import,
	.descsize	=	sizeof(struct s390_sha_ctx),
	.statesize	=	sizeof(struct sha3_state),
	.base		=	{
		.cra_name	 =	"sha3-256",
		.cra_driver_name =	"sha3-256-s390",
		.cra_priority	 =	300,
		.cra_blocksize	 =	SHA3_256_BLOCK_SIZE,
		.cra_module	 =	THIS_MODULE,
	}
};

static int sha3_224_init(struct shash_desc *desc)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);

	memset(sctx->state, 0, sizeof(sctx->state));
	sctx->count = 0;
	sctx->func = CPACF_KIMD_SHA3_224;

	return 0;
}

static struct shash_alg sha3_224_alg = {
	.digestsize	=	SHA3_224_DIGEST_SIZE,
	.init		=	sha3_224_init,
	.update		=	s390_sha_update,
	.final		=	s390_sha_final,
	.export		=	sha3_256_export, /* same as for 256 */
	.import		=	sha3_224_import, /* function code different! */
	.descsize	=	sizeof(struct s390_sha_ctx),
	.statesize	=	sizeof(struct sha3_state),
	.base		=	{
		.cra_name	 =	"sha3-224",
		.cra_driver_name =	"sha3-224-s390",
		.cra_priority	 =	300,
		.cra_blocksize	 =	SHA3_224_BLOCK_SIZE,
		.cra_module	 =	THIS_MODULE,
	}
};

static int __init sha3_256_s390_init(void)
{
	int ret;

	if (!cpacf_query_func(CPACF_KIMD, CPACF_KIMD_SHA3_256))
		return -ENODEV;

	ret = crypto_register_shash(&sha3_256_alg);
	if (ret < 0)
		goto out;

	ret = crypto_register_shash(&sha3_224_alg);
	if (ret < 0)
		crypto_unregister_shash(&sha3_256_alg);
out:
	return ret;
}

static void __exit sha3_256_s390_fini(void)
{
	crypto_unregister_shash(&sha3_224_alg);
	crypto_unregister_shash(&sha3_256_alg);
}

module_cpu_feature_match(MSA, sha3_256_s390_init);
module_exit(sha3_256_s390_fini);

MODULE_ALIAS_CRYPTO("sha3-256");
MODULE_ALIAS_CRYPTO("sha3-224");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA3-256 and SHA3-224 Secure Hash Algorithm");
