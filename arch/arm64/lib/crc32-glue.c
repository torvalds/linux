// SPDX-License-Identifier: GPL-2.0-only

#include <linux/crc32.h>
#include <linux/linkage.h>

#include <asm/alternative.h>

asmlinkage u32 crc32_le_arm64(u32 crc, unsigned char const *p, size_t len);
asmlinkage u32 crc32c_le_arm64(u32 crc, unsigned char const *p, size_t len);
asmlinkage u32 crc32_be_arm64(u32 crc, unsigned char const *p, size_t len);

u32 __pure crc32_le(u32 crc, unsigned char const *p, size_t len)
{
	if (!alternative_has_cap_likely(ARM64_HAS_CRC32))
		return crc32_le_base(crc, p, len);

	return crc32_le_arm64(crc, p, len);
}

u32 __pure __crc32c_le(u32 crc, unsigned char const *p, size_t len)
{
	if (!alternative_has_cap_likely(ARM64_HAS_CRC32))
		return __crc32c_le_base(crc, p, len);

	return crc32c_le_arm64(crc, p, len);
}

u32 __pure crc32_be(u32 crc, unsigned char const *p, size_t len)
{
	if (!alternative_has_cap_likely(ARM64_HAS_CRC32))
		return crc32_be_base(crc, p, len);

	return crc32_be_arm64(crc, p, len);
}
