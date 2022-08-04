/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _ASM_POWERPC_BOOK3S_64_PKEYS_H
#define _ASM_POWERPC_BOOK3S_64_PKEYS_H

#include <asm/book3s/64/hash-pkey.h>

static inline u64 vmflag_to_pte_pkey_bits(u64 vm_flags)
{
	if (!mmu_has_feature(MMU_FTR_PKEY))
		return 0x0UL;

	if (radix_enabled())
		BUG();
	return hash__vmflag_to_pte_pkey_bits(vm_flags);
}

static inline u16 pte_to_pkey_bits(u64 pteflags)
{
	if (radix_enabled())
		BUG();
	return hash__pte_to_pkey_bits(pteflags);
}

#endif /*_ASM_POWERPC_KEYS_H */
