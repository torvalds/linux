// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The MORUS-640 Authenticated-Encryption Algorithm
 *   Glue for SSE2 implementation
 *
 * Copyright (c) 2016-2018 Ondrej Mosnacek <omosnacek@gmail.com>
 * Copyright (C) 2017-2018 Red Hat, Inc. All rights reserved.
 */

#include <crypto/internal/aead.h>
#include <crypto/internal/simd.h>
#include <crypto/morus640_glue.h>
#include <linux/module.h>
#include <asm/fpu/api.h>
#include <asm/cpu_device_id.h>

asmlinkage void crypto_morus640_sse2_init(void *state, const void *key,
					  const void *iv);
asmlinkage void crypto_morus640_sse2_ad(void *state, const void *data,
					unsigned int length);

asmlinkage void crypto_morus640_sse2_enc(void *state, const void *src,
					 void *dst, unsigned int length);
asmlinkage void crypto_morus640_sse2_dec(void *state, const void *src,
					 void *dst, unsigned int length);

asmlinkage void crypto_morus640_sse2_enc_tail(void *state, const void *src,
					      void *dst, unsigned int length);
asmlinkage void crypto_morus640_sse2_dec_tail(void *state, const void *src,
					      void *dst, unsigned int length);

asmlinkage void crypto_morus640_sse2_final(void *state, void *tag_xor,
					   u64 assoclen, u64 cryptlen);

MORUS640_DECLARE_ALG(sse2, "morus640-sse2", 400);

static struct simd_aead_alg *simd_alg;

static int __init crypto_morus640_sse2_module_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_XMM2) ||
	    !cpu_has_xfeatures(XFEATURE_MASK_SSE, NULL))
		return -ENODEV;

	return simd_register_aeads_compat(&crypto_morus640_sse2_alg, 1,
					  &simd_alg);
}

static void __exit crypto_morus640_sse2_module_exit(void)
{
	simd_unregister_aeads(&crypto_morus640_sse2_alg, 1, &simd_alg);
}

module_init(crypto_morus640_sse2_module_init);
module_exit(crypto_morus640_sse2_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ondrej Mosnacek <omosnacek@gmail.com>");
MODULE_DESCRIPTION("MORUS-640 AEAD algorithm -- SSE2 implementation");
MODULE_ALIAS_CRYPTO("morus640");
MODULE_ALIAS_CRYPTO("morus640-sse2");
