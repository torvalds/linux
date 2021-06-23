/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PKRU_H
#define _ASM_X86_PKRU_H

#include <asm/fpu/xstate.h>

#define PKRU_AD_BIT 0x1
#define PKRU_WD_BIT 0x2
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
	struct pkru_state *pk;

	if (!cpu_feature_enabled(X86_FEATURE_OSPKE))
		return;

	pk = get_xsave_addr(&current->thread.fpu.state.xsave, XFEATURE_PKRU);

	/*
	 * The PKRU value in xstate needs to be in sync with the value that is
	 * written to the CPU. The FPU restore on return to userland would
	 * otherwise load the previous value again.
	 */
	fpregs_lock();
	if (pk)
		pk->pkru = pkru;
	__write_pkru(pkru);
	fpregs_unlock();
}

#endif
