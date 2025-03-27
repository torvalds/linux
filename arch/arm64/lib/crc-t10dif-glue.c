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

#include <crypto/internal/simd.h>

#include <asm/neon.h>
#include <asm/simd.h>

static DEFINE_STATIC_KEY_FALSE(have_asimd);
static DEFINE_STATIC_KEY_FALSE(have_pmull);

#define CRC_T10DIF_PMULL_CHUNK_SIZE	16U

asmlinkage void crc_t10dif_pmull_p8(u16 init_crc, const u8 *buf, size_t len,
				    u8 out[16]);
asmlinkage u16 crc_t10dif_pmull_p64(u16 init_crc, const u8 *buf, size_t len);

u16 crc_t10dif_arch(u16 crc, const u8 *data, size_t length)
{
	if (length >= CRC_T10DIF_PMULL_CHUNK_SIZE) {
		if (static_branch_likely(&have_pmull)) {
			if (crypto_simd_usable()) {
				kernel_neon_begin();
				crc = crc_t10dif_pmull_p64(crc, data, length);
				kernel_neon_end();
				return crc;
			}
		} else if (length > CRC_T10DIF_PMULL_CHUNK_SIZE &&
			   static_branch_likely(&have_asimd) &&
			   crypto_simd_usable()) {
			u8 buf[16];

			kernel_neon_begin();
			crc_t10dif_pmull_p8(crc, data, length, buf);
			kernel_neon_end();

			crc = 0;
			data = buf;
			length = sizeof(buf);
		}
	}
	return crc_t10dif_generic(crc, data, length);
}
EXPORT_SYMBOL(crc_t10dif_arch);

static int __init crc_t10dif_arm64_init(void)
{
	if (cpu_have_named_feature(ASIMD)) {
		static_branch_enable(&have_asimd);
		if (cpu_have_named_feature(PMULL))
			static_branch_enable(&have_pmull);
	}
	return 0;
}
arch_initcall(crc_t10dif_arm64_init);

static void __exit crc_t10dif_arm64_exit(void)
{
}
module_exit(crc_t10dif_arm64_exit);

MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_DESCRIPTION("CRC-T10DIF using arm64 NEON and Crypto Extensions");
MODULE_LICENSE("GPL v2");
