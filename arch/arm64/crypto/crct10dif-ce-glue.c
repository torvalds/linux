// SPDX-License-Identifier: GPL-2.0-only
/*
 * Accelerated CRC-T10DIF using arm64 NEON and Crypto Extensions instructions
 *
 * Copyright (C) 2016 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/cpufeature.h>
#include <linux/crc-t10dif.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>

#include <asm/neon.h>
#include <asm/simd.h>

#define CRC_T10DIF_PMULL_CHUNK_SIZE	16U

asmlinkage u16 crc_t10dif_pmull_p8(u16 init_crc, const u8 *buf, size_t len);
asmlinkage u16 crc_t10dif_pmull_p64(u16 init_crc, const u8 *buf, size_t len);

static int crct10dif_init(struct shash_desc *desc)
{
	u16 *crc = shash_desc_ctx(desc);

	*crc = 0;
	return 0;
}

static int crct10dif_update_pmull_p8(struct shash_desc *desc, const u8 *data,
			    unsigned int length)
{
	u16 *crc = shash_desc_ctx(desc);

	if (length >= CRC_T10DIF_PMULL_CHUNK_SIZE && crypto_simd_usable()) {
		do {
			unsigned int chunk = length;

			if (chunk > SZ_4K + CRC_T10DIF_PMULL_CHUNK_SIZE)
				chunk = SZ_4K;

			kernel_neon_begin();
			*crc = crc_t10dif_pmull_p8(*crc, data, chunk);
			kernel_neon_end();
			data += chunk;
			length -= chunk;
		} while (length);
	} else {
		*crc = crc_t10dif_generic(*crc, data, length);
	}

	return 0;
}

static int crct10dif_update_pmull_p64(struct shash_desc *desc, const u8 *data,
			    unsigned int length)
{
	u16 *crc = shash_desc_ctx(desc);

	if (length >= CRC_T10DIF_PMULL_CHUNK_SIZE && crypto_simd_usable()) {
		do {
			unsigned int chunk = length;

			if (chunk > SZ_4K + CRC_T10DIF_PMULL_CHUNK_SIZE)
				chunk = SZ_4K;

			kernel_neon_begin();
			*crc = crc_t10dif_pmull_p64(*crc, data, chunk);
			kernel_neon_end();
			data += chunk;
			length -= chunk;
		} while (length);
	} else {
		*crc = crc_t10dif_generic(*crc, data, length);
	}

	return 0;
}

static int crct10dif_final(struct shash_desc *desc, u8 *out)
{
	u16 *crc = shash_desc_ctx(desc);

	*(u16 *)out = *crc;
	return 0;
}

static struct shash_alg crc_t10dif_alg[] = {{
	.digestsize		= CRC_T10DIF_DIGEST_SIZE,
	.init			= crct10dif_init,
	.update			= crct10dif_update_pmull_p8,
	.final			= crct10dif_final,
	.descsize		= CRC_T10DIF_DIGEST_SIZE,

	.base.cra_name		= "crct10dif",
	.base.cra_driver_name	= "crct10dif-arm64-neon",
	.base.cra_priority	= 100,
	.base.cra_blocksize	= CRC_T10DIF_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
}, {
	.digestsize		= CRC_T10DIF_DIGEST_SIZE,
	.init			= crct10dif_init,
	.update			= crct10dif_update_pmull_p64,
	.final			= crct10dif_final,
	.descsize		= CRC_T10DIF_DIGEST_SIZE,

	.base.cra_name		= "crct10dif",
	.base.cra_driver_name	= "crct10dif-arm64-ce",
	.base.cra_priority	= 200,
	.base.cra_blocksize	= CRC_T10DIF_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
}};

static int __init crc_t10dif_mod_init(void)
{
	if (cpu_have_named_feature(PMULL))
		return crypto_register_shashes(crc_t10dif_alg,
					       ARRAY_SIZE(crc_t10dif_alg));
	else
		/* only register the first array element */
		return crypto_register_shash(crc_t10dif_alg);
}

static void __exit crc_t10dif_mod_exit(void)
{
	if (cpu_have_named_feature(PMULL))
		crypto_unregister_shashes(crc_t10dif_alg,
					  ARRAY_SIZE(crc_t10dif_alg));
	else
		crypto_unregister_shash(crc_t10dif_alg);
}

module_cpu_feature_match(ASIMD, crc_t10dif_mod_init);
module_exit(crc_t10dif_mod_exit);

MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("crct10dif");
MODULE_ALIAS_CRYPTO("crct10dif-arm64-ce");
