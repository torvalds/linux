/*
 * The MORUS-1280 Authenticated-Encryption Algorithm
 *   Glue for SSE2 implementation
 *
 * Copyright (c) 2016-2018 Ondrej Mosnacek <omosnacek@gmail.com>
 * Copyright (C) 2017-2018 Red Hat, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <crypto/internal/aead.h>
#include <crypto/morus1280_glue.h>
#include <linux/module.h>
#include <asm/fpu/api.h>
#include <asm/cpu_device_id.h>

asmlinkage void crypto_morus1280_sse2_init(void *state, const void *key,
					   const void *iv);
asmlinkage void crypto_morus1280_sse2_ad(void *state, const void *data,
					 unsigned int length);

asmlinkage void crypto_morus1280_sse2_enc(void *state, const void *src,
					  void *dst, unsigned int length);
asmlinkage void crypto_morus1280_sse2_dec(void *state, const void *src,
					  void *dst, unsigned int length);

asmlinkage void crypto_morus1280_sse2_enc_tail(void *state, const void *src,
					       void *dst, unsigned int length);
asmlinkage void crypto_morus1280_sse2_dec_tail(void *state, const void *src,
					       void *dst, unsigned int length);

asmlinkage void crypto_morus1280_sse2_final(void *state, void *tag_xor,
					    u64 assoclen, u64 cryptlen);

MORUS1280_DECLARE_ALGS(sse2, "morus1280-sse2", 350);

static const struct x86_cpu_id sse2_cpu_id[] = {
    X86_FEATURE_MATCH(X86_FEATURE_XMM2),
    {}
};
MODULE_DEVICE_TABLE(x86cpu, sse2_cpu_id);

static int __init crypto_morus1280_sse2_module_init(void)
{
	if (!x86_match_cpu(sse2_cpu_id))
		return -ENODEV;

	return crypto_register_aeads(crypto_morus1280_sse2_algs,
				     ARRAY_SIZE(crypto_morus1280_sse2_algs));
}

static void __exit crypto_morus1280_sse2_module_exit(void)
{
	crypto_unregister_aeads(crypto_morus1280_sse2_algs,
				ARRAY_SIZE(crypto_morus1280_sse2_algs));
}

module_init(crypto_morus1280_sse2_module_init);
module_exit(crypto_morus1280_sse2_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ondrej Mosnacek <omosnacek@gmail.com>");
MODULE_DESCRIPTION("MORUS-1280 AEAD algorithm -- SSE2 implementation");
MODULE_ALIAS_CRYPTO("morus1280");
MODULE_ALIAS_CRYPTO("morus1280-sse2");
