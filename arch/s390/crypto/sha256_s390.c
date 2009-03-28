/*
 * Cryptographic API.
 *
 * s390 implementation of the SHA256 Secure Hash Algorithm.
 *
 * s390 Version:
 *   Copyright IBM Corp. 2005,2007
 *   Author(s): Jan Glauber (jang@de.ibm.com)
 *
 * Derived from "crypto/sha256_generic.c"
 * and "arch/s390/crypto/sha1_s390.c"
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <crypto/sha.h>

#include "crypt_s390.h"
#include "sha.h"

static int sha256_init(struct shash_desc *desc)
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
	sctx->func = KIMD_SHA_256;

	return 0;
}

static struct shash_alg alg = {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	sha256_init,
	.update		=	s390_sha_update,
	.final		=	s390_sha_final,
	.descsize	=	sizeof(struct s390_sha_ctx),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name=	"sha256-s390",
		.cra_priority	=	CRYPT_S390_PRIORITY,
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int sha256_s390_init(void)
{
	if (!crypt_s390_func_available(KIMD_SHA_256))
		return -EOPNOTSUPP;

	return crypto_register_shash(&alg);
}

static void __exit sha256_s390_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(sha256_s390_init);
module_exit(sha256_s390_fini);

MODULE_ALIAS("sha256");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA256 Secure Hash Algorithm");
