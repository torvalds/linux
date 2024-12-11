// SPDX-License-Identifier: GPL-2.0-only
/*
 * Accelerated CRC-T10DIF using ARM NEON and Crypto Extensions instructions
 *
 * Copyright (C) 2016 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

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

asmlinkage u16 crc_t10dif_pmull64(u16 init_crc, const u8 *buf, size_t len);
asmlinkage void crc_t10dif_pmull8(u16 init_crc, const u8 *buf, size_t len,
				  u8 out[16]);

static int crct10dif_init(struct shash_desc *desc)
{
	u16 *crc = shash_desc_ctx(desc);

	*crc = 0;
	return 0;
}

static int crct10dif_update_ce(struct shash_desc *desc, const u8 *data,
			       unsigned int length)
{
	u16 *crc = shash_desc_ctx(desc);

	if (length >= CRC_T10DIF_PMULL_CHUNK_SIZE && crypto_simd_usable()) {
		kernel_neon_begin();
		*crc = crc_t10dif_pmull64(*crc, data, length);
		kernel_neon_end();
	} else {
		*crc = crc_t10dif_generic(*crc, data, length);
	}

	return 0;
}

static int crct10dif_update_neon(struct shash_desc *desc, const u8 *data,
			         unsigned int length)
{
	u16 *crcp = shash_desc_ctx(desc);
	u8 buf[16] __aligned(16);
	u16 crc = *crcp;

	if (length > CRC_T10DIF_PMULL_CHUNK_SIZE && crypto_simd_usable()) {
		kernel_neon_begin();
		crc_t10dif_pmull8(crc, data, length, buf);
		kernel_neon_end();

		crc = 0;
		data = buf;
		length = sizeof(buf);
	}

	*crcp = crc_t10dif_generic(crc, data, length);
	return 0;
}

static int crct10dif_final(struct shash_desc *desc, u8 *out)
{
	u16 *crc = shash_desc_ctx(desc);

	*(u16 *)out = *crc;
	return 0;
}

static struct shash_alg algs[] = {{
	.digestsize		= CRC_T10DIF_DIGEST_SIZE,
	.init			= crct10dif_init,
	.update			= crct10dif_update_neon,
	.final			= crct10dif_final,
	.descsize		= CRC_T10DIF_DIGEST_SIZE,

	.base.cra_name		= "crct10dif",
	.base.cra_driver_name	= "crct10dif-arm-neon",
	.base.cra_priority	= 150,
	.base.cra_blocksize	= CRC_T10DIF_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
}, {
	.digestsize		= CRC_T10DIF_DIGEST_SIZE,
	.init			= crct10dif_init,
	.update			= crct10dif_update_ce,
	.final			= crct10dif_final,
	.descsize		= CRC_T10DIF_DIGEST_SIZE,

	.base.cra_name		= "crct10dif",
	.base.cra_driver_name	= "crct10dif-arm-ce",
	.base.cra_priority	= 200,
	.base.cra_blocksize	= CRC_T10DIF_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
}};

static int __init crc_t10dif_mod_init(void)
{
	if (!(elf_hwcap & HWCAP_NEON))
		return -ENODEV;

	return crypto_register_shashes(algs, 1 + !!(elf_hwcap2 & HWCAP2_PMULL));
}

static void __exit crc_t10dif_mod_exit(void)
{
	crypto_unregister_shashes(algs, 1 + !!(elf_hwcap2 & HWCAP2_PMULL));
}

module_init(crc_t10dif_mod_init);
module_exit(crc_t10dif_mod_exit);

MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_DESCRIPTION("Accelerated CRC-T10DIF using ARM NEON and Crypto Extensions");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("crct10dif");
