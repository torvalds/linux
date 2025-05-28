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

#include <crypto/internal/simd.h>

#include <asm/neon.h>
#include <asm/simd.h>

static DEFINE_STATIC_KEY_FALSE(have_neon);
static DEFINE_STATIC_KEY_FALSE(have_pmull);

#define CRC_T10DIF_PMULL_CHUNK_SIZE	16U

asmlinkage u16 crc_t10dif_pmull64(u16 init_crc, const u8 *buf, size_t len);
asmlinkage void crc_t10dif_pmull8(u16 init_crc, const u8 *buf, size_t len,
				  u8 out[16]);

u16 crc_t10dif_arch(u16 crc, const u8 *data, size_t length)
{
	if (length >= CRC_T10DIF_PMULL_CHUNK_SIZE) {
		if (static_branch_likely(&have_pmull)) {
			if (crypto_simd_usable()) {
				kernel_neon_begin();
				crc = crc_t10dif_pmull64(crc, data, length);
				kernel_neon_end();
				return crc;
			}
		} else if (length > CRC_T10DIF_PMULL_CHUNK_SIZE &&
			   static_branch_likely(&have_neon) &&
			   crypto_simd_usable()) {
			u8 buf[16] __aligned(16);

			kernel_neon_begin();
			crc_t10dif_pmull8(crc, data, length, buf);
			kernel_neon_end();

			return crc_t10dif_generic(0, buf, sizeof(buf));
		}
	}
	return crc_t10dif_generic(crc, data, length);
}
EXPORT_SYMBOL(crc_t10dif_arch);

static int __init crc_t10dif_arm_init(void)
{
	if (elf_hwcap & HWCAP_NEON) {
		static_branch_enable(&have_neon);
		if (elf_hwcap2 & HWCAP2_PMULL)
			static_branch_enable(&have_pmull);
	}
	return 0;
}
arch_initcall(crc_t10dif_arm_init);

static void __exit crc_t10dif_arm_exit(void)
{
}
module_exit(crc_t10dif_arm_exit);

MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_DESCRIPTION("Accelerated CRC-T10DIF using ARM NEON and Crypto Extensions");
MODULE_LICENSE("GPL v2");
