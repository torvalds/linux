// SPDX-License-Identifier: GPL-2.0+
/*
 * Cryptographic API.
 *
 * s390 implementation of the SHA512 and SHA384 Secure Hash Algorithm.
 *
 * Copyright IBM Corp. 2019
 * Author(s): Joerg Schmidbauer (jschmidb@de.ibm.com)
 */
#include <asm/cpacf.h>
#include <crypto/internal/hash.h>
#include <crypto/sha3.h>
#include <linux/cpufeature.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#include "sha.h"

static int sha3_512_init(struct shash_desc *desc)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);

	sctx->first_message_part = test_facility(86);
	if (!sctx->first_message_part)
		memset(sctx->state, 0, sizeof(sctx->state));
	sctx->count = 0;
	sctx->func = CPACF_KIMD_SHA3_512;

	return 0;
}

static int sha3_512_export(struct shash_desc *desc, void *out)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);
	union {
		u8 *u8;
		u64 *u64;
	} p = { .u8 = out };
	int i;

	if (sctx->first_message_part) {
		memset(out, 0, SHA3_STATE_SIZE);
		return 0;
	}
	for (i = 0; i < SHA3_STATE_SIZE / 8; i++)
		put_unaligned(le64_to_cpu(sctx->sha3.state[i]), p.u64++);
	return 0;
}

static int sha3_512_import(struct shash_desc *desc, const void *in)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);
	union {
		const u8 *u8;
		const u64 *u64;
	} p = { .u8 = in };
	int i;

	for (i = 0; i < SHA3_STATE_SIZE / 8; i++)
		sctx->sha3.state[i] = cpu_to_le64(get_unaligned(p.u64++));
	sctx->count = 0;
	sctx->first_message_part = 0;
	sctx->func = CPACF_KIMD_SHA3_512;

	return 0;
}

static int sha3_384_import(struct shash_desc *desc, const void *in)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);

	sha3_512_import(desc, in);
	sctx->func = CPACF_KIMD_SHA3_384;
	return 0;
}

static struct shash_alg sha3_512_alg = {
	.digestsize	=	SHA3_512_DIGEST_SIZE,
	.init		=	sha3_512_init,
	.update		=	s390_sha_update_blocks,
	.finup		=	s390_sha_finup,
	.export		=	sha3_512_export,
	.import		=	sha3_512_import,
	.descsize	=	S390_SHA_CTX_SIZE,
	.statesize	=	SHA3_STATE_SIZE,
	.base		=	{
		.cra_name	 =	"sha3-512",
		.cra_driver_name =	"sha3-512-s390",
		.cra_priority	 =	300,
		.cra_flags	 =	CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize	 =	SHA3_512_BLOCK_SIZE,
		.cra_module	 =	THIS_MODULE,
	}
};

MODULE_ALIAS_CRYPTO("sha3-512");

static int sha3_384_init(struct shash_desc *desc)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);

	sha3_512_init(desc);
	sctx->func = CPACF_KIMD_SHA3_384;
	return 0;
}

static struct shash_alg sha3_384_alg = {
	.digestsize	=	SHA3_384_DIGEST_SIZE,
	.init		=	sha3_384_init,
	.update		=	s390_sha_update_blocks,
	.finup		=	s390_sha_finup,
	.export		=	sha3_512_export, /* same as for 512 */
	.import		=	sha3_384_import, /* function code different! */
	.descsize	=	S390_SHA_CTX_SIZE,
	.statesize	=	SHA3_STATE_SIZE,
	.base		=	{
		.cra_name	 =	"sha3-384",
		.cra_driver_name =	"sha3-384-s390",
		.cra_priority	 =	300,
		.cra_flags	 =	CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize	 =	SHA3_384_BLOCK_SIZE,
		.cra_ctxsize	 =	sizeof(struct s390_sha_ctx),
		.cra_module	 =	THIS_MODULE,
	}
};

MODULE_ALIAS_CRYPTO("sha3-384");

static int __init init(void)
{
	int ret;

	if (!cpacf_query_func(CPACF_KIMD, CPACF_KIMD_SHA3_512))
		return -ENODEV;
	ret = crypto_register_shash(&sha3_512_alg);
	if (ret < 0)
		goto out;
	ret = crypto_register_shash(&sha3_384_alg);
	if (ret < 0)
		crypto_unregister_shash(&sha3_512_alg);
out:
	return ret;
}

static void __exit fini(void)
{
	crypto_unregister_shash(&sha3_512_alg);
	crypto_unregister_shash(&sha3_384_alg);
}

module_cpu_feature_match(S390_CPU_FEATURE_MSA, init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA3-512 and SHA3-384 Secure Hash Algorithm");
