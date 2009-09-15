/*
 * Cryptographic API.
 *
 * s390 implementation of the SHA512 and SHA38 Secure Hash Algorithm.
 *
 * Copyright IBM Corp. 2007
 * Author(s): Jan Glauber (jang@de.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#include <crypto/internal/hash.h>
#include <crypto/sha.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "sha.h"
#include "crypt_s390.h"

static int sha512_init(struct shash_desc *desc)
{
	struct s390_sha_ctx *ctx = shash_desc_ctx(desc);

	*(__u64 *)&ctx->state[0] = 0x6a09e667f3bcc908ULL;
	*(__u64 *)&ctx->state[2] = 0xbb67ae8584caa73bULL;
	*(__u64 *)&ctx->state[4] = 0x3c6ef372fe94f82bULL;
	*(__u64 *)&ctx->state[6] = 0xa54ff53a5f1d36f1ULL;
	*(__u64 *)&ctx->state[8] = 0x510e527fade682d1ULL;
	*(__u64 *)&ctx->state[10] = 0x9b05688c2b3e6c1fULL;
	*(__u64 *)&ctx->state[12] = 0x1f83d9abfb41bd6bULL;
	*(__u64 *)&ctx->state[14] = 0x5be0cd19137e2179ULL;
	ctx->count = 0;
	ctx->func = KIMD_SHA_512;

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
	sctx->func = KIMD_SHA_512;
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
		.cra_priority	=	CRYPT_S390_PRIORITY,
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	SHA512_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

MODULE_ALIAS("sha512");

static int sha384_init(struct shash_desc *desc)
{
	struct s390_sha_ctx *ctx = shash_desc_ctx(desc);

	*(__u64 *)&ctx->state[0] = 0xcbbb9d5dc1059ed8ULL;
	*(__u64 *)&ctx->state[2] = 0x629a292a367cd507ULL;
	*(__u64 *)&ctx->state[4] = 0x9159015a3070dd17ULL;
	*(__u64 *)&ctx->state[6] = 0x152fecd8f70e5939ULL;
	*(__u64 *)&ctx->state[8] = 0x67332667ffc00b31ULL;
	*(__u64 *)&ctx->state[10] = 0x8eb44a8768581511ULL;
	*(__u64 *)&ctx->state[12] = 0xdb0c2e0d64f98fa7ULL;
	*(__u64 *)&ctx->state[14] = 0x47b5481dbefa4fa4ULL;
	ctx->count = 0;
	ctx->func = KIMD_SHA_512;

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
		.cra_priority	=	CRYPT_S390_PRIORITY,
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	SHA384_BLOCK_SIZE,
		.cra_ctxsize	=	sizeof(struct s390_sha_ctx),
		.cra_module	=	THIS_MODULE,
	}
};

MODULE_ALIAS("sha384");

static int __init init(void)
{
	int ret;

	if (!crypt_s390_func_available(KIMD_SHA_512))
		return -EOPNOTSUPP;
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

module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA512 and SHA-384 Secure Hash Algorithm");
