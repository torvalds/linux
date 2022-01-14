/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PKRU_H
#define _ASM_X86_PKRU_H

#include <asm/fpu/xstate.h>

#define PKRU_AD_BIT 0x1u
#define PKRU_WD_BIT 0x2u
#define PKRU_BITS_PER_PKEY 2

#ifdef CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS
extern u32 init_pkru_value;
#define pkru_get_init_value()	READ_ONCE(init_pkru_value)
#else
#define init_pkru_value	0
#define pkru_get_init_value()	0
#endif

static inline bool __pkru_allows_read(u32 pkru, u16 pkey)
{
	int pkru_pkey_bits = pkey * PKRU_BITS_PER_PKEY;
	return !(pkru & (PKRU_AD_BIT << pkru_pkey_bits));
}

static inline bool __pkru_allows_write(u32 pkru, u16 pkey)
{
	int pkru_pkey_bits = pkey * PKRU_BITS_PER_PKEY;
	/*
	 * Access-disable disables writes too so we need to check
	 * both bits here.
	 */
	return !(pkru & ((PKRU_AD_BIT|PKRU_WD_BIT) << pkru_pkey_bits));
}

static inline u32 read_pkru(void)
{
	if (cpu_feature_enabled(X86_FEATURE_OSPKE))
		return rdpkru();
	return 0;
}

static inline void write_pkru(u32 pkru)
{
	if (!cpu_feature_enabled(X86_FEATURE_OSPKE))
		return;
	/*
	 * WRPKRU is relatively expensive compared to RDPKRU.
	 * Avoid WRPKRU when it would not change the value.
	 */
	if (pkru != rdpkru())
		wrpkru(pkru);
}

static inline void pkru_write_default(void)
{
	if (!cpu_feature_enabled(X86_FEATURE_OSPKE))
		return;

	wrpkru(pkru_get_init_value());
}

#endif
