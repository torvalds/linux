/*
 * Cryptographic API.
 *
 * s390 implementation of the SHA512 Secure Hash Algorithm.
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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/crypto.h>

#include "sha.h"
#include "crypt_s390.h"

static void sha512_init(struct crypto_tfm *tfm)
{
	struct s390_sha_ctx *ctx = crypto_tfm_ctx(tfm);

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
}

static struct crypto_alg alg = {
	.cra_name	=	"sha512",
	.cra_driver_name =	"sha512-s390",
	.cra_priority	=	CRYPT_S390_PRIORITY,
	.cra_flags	=	CRYPTO_ALG_TYPE_DIGEST,
	.cra_blocksize	=	SHA512_BLOCK_SIZE,
	.cra_ctxsize	=	sizeof(struct s390_sha_ctx),
	.cra_module	=	THIS_MODULE,
	.cra_list	=	LIST_HEAD_INIT(alg.cra_list),
	.cra_u		=	{ .digest = {
	.dia_digestsize	=	SHA512_DIGEST_SIZE,
	.dia_init	=	sha512_init,
	.dia_update	=	s390_sha_update,
	.dia_final	=	s390_sha_final } }
};

static int __init init(void)
{
	if (!crypt_s390_func_available(KIMD_SHA_512))
		return -EOPNOTSUPP;
	return crypto_register_alg(&alg);
}

static void __exit fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(init);
module_exit(fini);

MODULE_ALIAS("sha512");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA512 Secure Hash Algorithm");
