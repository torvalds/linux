// SPDX-License-Identifier: GPL-2.0
/*
 * CRC32 and CRC32C using LoongArch crc* instructions
 *
 * Module based on mips/crypto/crc32-mips.c
 *
 * Copyright (C) 2014 Linaro Ltd <yazen.ghannam@linaro.org>
 * Copyright (C) 2018 MIPS Tech, LLC
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <asm/cpu-features.h>
#include <linux/crc32.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/unaligned.h>

#define _CRC32(crc, value, size, type)			\
do {							\
	__asm__ __volatile__(				\
		#type ".w." #size ".w" " %0, %1, %0\n\t"\
		: "+r" (crc)				\
		: "r" (value)				\
		: "memory");				\
} while (0)

#define CRC32(crc, value, size)		_CRC32(crc, value, size, crc)
#define CRC32C(crc, value, size)	_CRC32(crc, value, size, crcc)

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_crc32);

u32 crc32_le_arch(u32 crc, const u8 *p, size_t len)
{
	if (!static_branch_likely(&have_crc32))
		return crc32_le_base(crc, p, len);

	while (len >= sizeof(u64)) {
		u64 value = get_unaligned_le64(p);

		CRC32(crc, value, d);
		p += sizeof(u64);
		len -= sizeof(u64);
	}

	if (len & sizeof(u32)) {
		u32 value = get_unaligned_le32(p);

		CRC32(crc, value, w);
		p += sizeof(u32);
	}

	if (len & sizeof(u16)) {
		u16 value = get_unaligned_le16(p);

		CRC32(crc, value, h);
		p += sizeof(u16);
	}

	if (len & sizeof(u8)) {
		u8 value = *p++;

		CRC32(crc, value, b);
	}

	return crc;
}
EXPORT_SYMBOL(crc32_le_arch);

u32 crc32c_arch(u32 crc, const u8 *p, size_t len)
{
	if (!static_branch_likely(&have_crc32))
		return crc32c_base(crc, p, len);

	while (len >= sizeof(u64)) {
		u64 value = get_unaligned_le64(p);

		CRC32C(crc, value, d);
		p += sizeof(u64);
		len -= sizeof(u64);
	}

	if (len & sizeof(u32)) {
		u32 value = get_unaligned_le32(p);

		CRC32C(crc, value, w);
		p += sizeof(u32);
	}

	if (len & sizeof(u16)) {
		u16 value = get_unaligned_le16(p);

		CRC32C(crc, value, h);
		p += sizeof(u16);
	}

	if (len & sizeof(u8)) {
		u8 value = *p++;

		CRC32C(crc, value, b);
	}

	return crc;
}
EXPORT_SYMBOL(crc32c_arch);

u32 crc32_be_arch(u32 crc, const u8 *p, size_t len)
{
	return crc32_be_base(crc, p, len);
}
EXPORT_SYMBOL(crc32_be_arch);

static int __init crc32_loongarch_init(void)
{
	if (cpu_has_crc32)
		static_branch_enable(&have_crc32);
	return 0;
}
subsys_initcall(crc32_loongarch_init);

static void __exit crc32_loongarch_exit(void)
{
}
module_exit(crc32_loongarch_exit);

u32 crc32_optimizations(void)
{
	if (static_key_enabled(&have_crc32))
		return CRC32_LE_OPTIMIZATION | CRC32C_OPTIMIZATION;
	return 0;
}
EXPORT_SYMBOL(crc32_optimizations);

MODULE_AUTHOR("Min Zhou <zhoumin@loongson.cn>");
MODULE_AUTHOR("Huacai Chen <chenhuacai@loongson.cn>");
MODULE_DESCRIPTION("CRC32 and CRC32C using LoongArch crc* instructions");
MODULE_LICENSE("GPL v2");
