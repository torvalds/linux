#ifndef __ASM_X86_HASH_H
#define __ASM_X86_HASH_H

#include <linux/cpufeature.h>
#include <asm/alternative.h>

u32 __intel_crc4_2_hash(const void *data, u32 len, u32 seed);
u32 __intel_crc4_2_hash2(const u32 *data, u32 len, u32 seed);

/*
 * non-inline versions of jhash so gcc does not need to generate
 * duplicate code in every object file
 */
u32 __jhash(const void *data, u32 len, u32 seed);
u32 __jhash2(const u32 *data, u32 len, u32 seed);

/*
 * for documentation of these functions please look into
 * <include/asm-generic/hash.h>
 */

static inline u32 arch_fast_hash(const void *data, u32 len, u32 seed)
{
	u32 hash;

	alternative_call(__jhash, __intel_crc4_2_hash, X86_FEATURE_XMM4_2,
#ifdef CONFIG_X86_64
			 "=a" (hash), "D" (data), "S" (len), "d" (seed));
#else
			 "=a" (hash), "a" (data), "d" (len), "c" (seed));
#endif
	return hash;
}

static inline u32 arch_fast_hash2(const u32 *data, u32 len, u32 seed)
{
	u32 hash;

	alternative_call(__jhash2, __intel_crc4_2_hash2, X86_FEATURE_XMM4_2,
#ifdef CONFIG_X86_64
			 "=a" (hash), "D" (data), "S" (len), "d" (seed));
#else
			 "=a" (hash), "a" (data), "d" (len), "c" (seed));
#endif
	return hash;
}

#endif /* __ASM_X86_HASH_H */
