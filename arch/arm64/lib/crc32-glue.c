// SPDX-License-Identifier: GPL-2.0-only

#include <linux/crc32.h>
#include <linux/linkage.h>
#include <linux/module.h>

#include <asm/alternative.h>
#include <asm/cpufeature.h>
#include <asm/neon.h>
#include <asm/simd.h>

#include <crypto/internal/simd.h>

// The minimum input length to consider the 4-way interleaved code path
static const size_t min_len = 1024;

asmlinkage u32 crc32_le_arm64(u32 crc, unsigned char const *p, size_t len);
asmlinkage u32 crc32c_le_arm64(u32 crc, unsigned char const *p, size_t len);
asmlinkage u32 crc32_be_arm64(u32 crc, unsigned char const *p, size_t len);

asmlinkage u32 crc32_le_arm64_4way(u32 crc, unsigned char const *p, size_t len);
asmlinkage u32 crc32c_le_arm64_4way(u32 crc, unsigned char const *p, size_t len);
asmlinkage u32 crc32_be_arm64_4way(u32 crc, unsigned char const *p, size_t len);

u32 crc32_le_arch(u32 crc, const u8 *p, size_t len)
{
	if (!alternative_has_cap_likely(ARM64_HAS_CRC32))
		return crc32_le_base(crc, p, len);

	if (len >= min_len && cpu_have_named_feature(PMULL) && crypto_simd_usable()) {
		kernel_neon_begin();
		crc = crc32_le_arm64_4way(crc, p, len);
		kernel_neon_end();

		p += round_down(len, 64);
		len %= 64;

		if (!len)
			return crc;
	}

	return crc32_le_arm64(crc, p, len);
}
EXPORT_SYMBOL(crc32_le_arch);

u32 crc32c_arch(u32 crc, const u8 *p, size_t len)
{
	if (!alternative_has_cap_likely(ARM64_HAS_CRC32))
		return crc32c_base(crc, p, len);

	if (len >= min_len && cpu_have_named_feature(PMULL) && crypto_simd_usable()) {
		kernel_neon_begin();
		crc = crc32c_le_arm64_4way(crc, p, len);
		kernel_neon_end();

		p += round_down(len, 64);
		len %= 64;

		if (!len)
			return crc;
	}

	return crc32c_le_arm64(crc, p, len);
}
EXPORT_SYMBOL(crc32c_arch);

u32 crc32_be_arch(u32 crc, const u8 *p, size_t len)
{
	if (!alternative_has_cap_likely(ARM64_HAS_CRC32))
		return crc32_be_base(crc, p, len);

	if (len >= min_len && cpu_have_named_feature(PMULL) && crypto_simd_usable()) {
		kernel_neon_begin();
		crc = crc32_be_arm64_4way(crc, p, len);
		kernel_neon_end();

		p += round_down(len, 64);
		len %= 64;

		if (!len)
			return crc;
	}

	return crc32_be_arm64(crc, p, len);
}
EXPORT_SYMBOL(crc32_be_arch);

u32 crc32_optimizations(void)
{
	if (alternative_has_cap_likely(ARM64_HAS_CRC32))
		return CRC32_LE_OPTIMIZATION |
		       CRC32_BE_OPTIMIZATION |
		       CRC32C_OPTIMIZATION;
	return 0;
}
EXPORT_SYMBOL(crc32_optimizations);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("arm64-optimized CRC32 functions");
