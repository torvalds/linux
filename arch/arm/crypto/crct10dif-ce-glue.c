/*
 * Accelerated CRC-T10DIF using ARM NEON and Crypto Extensions instructions
 *
 * Copyright (C) 2016 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/crc-t10dif.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#include <crypto/internal/hash.h>

#include <asm/neon.h>
#include <asm/simd.h>

#define CRC_T10DIF_PMULL_CHUNK_SIZE	16U

asmlinkage u16 crc_t10dif_pmull(u16 init_crc, const u8 buf[], u32 len);

static int crct10dif_init(struct shash_desc *desc)
{
	u16 *crc = shash_desc_ctx(desc);

	*crc = 0;
	return 0;
}

static int crct10dif_update(struct shash_desc *desc, const u8 *data,
			    unsigned int length)
{
	u16 *crc = shash_desc_ctx(desc);

	if (length >= CRC_T10DIF_PMULL_CHUNK_SIZE && may_use_simd()) {
		kernel_neon_begin();
		*crc = crc_t10dif_pmull(*crc, data, length);
		kernel_neon_end();
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

static struct shash_alg crc_t10dif_alg = {
	.digestsize		= CRC_T10DIF_DIGEST_SIZE,
	.init			= crct10dif_init,
	.update			= crct10dif_update,
	.final			= crct10dif_final,
	.descsize		= CRC_T10DIF_DIGEST_SIZE,

	.base.cra_name		= "crct10dif",
	.base.cra_driver_name	= "crct10dif-arm-ce",
	.base.cra_priority	= 200,
	.base.cra_blocksize	= CRC_T10DIF_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
};

static int __init crc_t10dif_mod_init(void)
{
	if (!(elf_hwcap2 & HWCAP2_PMULL))
		return -ENODEV;

	return crypto_register_shash(&crc_t10dif_alg);
}

static void __exit crc_t10dif_mod_exit(void)
{
	crypto_unregister_shash(&crc_t10dif_alg);
}

module_init(crc_t10dif_mod_init);
module_exit(crc_t10dif_mod_exit);

MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("crct10dif");
