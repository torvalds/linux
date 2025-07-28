// SPDX-License-Identifier: GPL-2.0+
/*
 * Cryptographic API.
 *
 * s390 implementation of the SHA1 Secure Hash Algorithm.
 *
 * Derived from cryptoapi implementation, adapted for in-place
 * scatterlist interface.  Originally based on the public domain
 * implementation written by Steve Reid.
 *
 * s390 Version:
 *   Copyright IBM Corp. 2003, 2007
 *   Author(s): Thomas Spatzier
 *		Jan Glauber (jan.glauber@de.ibm.com)
 *
 * Derived from "crypto/sha1_generic.c"
 *   Copyright (c) Alan Smithee.
 *   Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 *   Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 */
#include <asm/cpacf.h>
#include <crypto/internal/hash.h>
#include <crypto/sha1.h>
#include <linux/cpufeature.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "sha.h"

static int s390_sha1_init(struct shash_desc *desc)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SHA1_H0;
	sctx->state[1] = SHA1_H1;
	sctx->state[2] = SHA1_H2;
	sctx->state[3] = SHA1_H3;
	sctx->state[4] = SHA1_H4;
	sctx->count = 0;
	sctx->func = CPACF_KIMD_SHA_1;
	sctx->first_message_part = 0;

	return 0;
}

static int s390_sha1_export(struct shash_desc *desc, void *out)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);
	struct sha1_state *octx = out;

	octx->count = sctx->count;
	memcpy(octx->state, sctx->state, sizeof(octx->state));
	return 0;
}

static int s390_sha1_import(struct shash_desc *desc, const void *in)
{
	struct s390_sha_ctx *sctx = shash_desc_ctx(desc);
	const struct sha1_state *ictx = in;

	sctx->count = ictx->count;
	memcpy(sctx->state, ictx->state, sizeof(ictx->state));
	sctx->func = CPACF_KIMD_SHA_1;
	sctx->first_message_part = 0;
	return 0;
}

static struct shash_alg alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	s390_sha1_init,
	.update		=	s390_sha_update_blocks,
	.finup		=	s390_sha_finup,
	.export		=	s390_sha1_export,
	.import		=	s390_sha1_import,
	.descsize	=	S390_SHA_CTX_SIZE,
	.statesize	=	SHA1_STATE_SIZE,
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name=	"sha1-s390",
		.cra_priority	=	300,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY |
					CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize	=	SHA1_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int __init sha1_s390_init(void)
{
	if (!cpacf_query_func(CPACF_KIMD, CPACF_KIMD_SHA_1))
		return -ENODEV;
	return crypto_register_shash(&alg);
}

static void __exit sha1_s390_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_cpu_feature_match(S390_CPU_FEATURE_MSA, sha1_s390_init);
module_exit(sha1_s390_fini);

MODULE_ALIAS_CRYPTO("sha1");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm");
