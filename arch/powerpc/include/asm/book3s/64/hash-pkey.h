/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_HASH_PKEY_H
#define _ASM_POWERPC_BOOK3S_64_HASH_PKEY_H

/*  We use key 3 for KERNEL */
#define HASH_DEFAULT_KERNEL_KEY (HPTE_R_KEY_BIT0 | HPTE_R_KEY_BIT1)

static inline u64 hash__vmflag_to_pte_pkey_bits(u64 vm_flags)
{
	return (((vm_flags & VM_PKEY_BIT0) ? H_PTE_PKEY_BIT0 : 0x0UL) |
		((vm_flags & VM_PKEY_BIT1) ? H_PTE_PKEY_BIT1 : 0x0UL) |
		((vm_flags & VM_PKEY_BIT2) ? H_PTE_PKEY_BIT2 : 0x0UL) |
		((vm_flags & VM_PKEY_BIT3) ? H_PTE_PKEY_BIT3 : 0x0UL) |
		((vm_flags & VM_PKEY_BIT4) ? H_PTE_PKEY_BIT4 : 0x0UL));
}

static inline u64 pte_to_hpte_pkey_bits(u64 pteflags, unsigned long flags)
{
	unsigned long pte_pkey;

	pte_pkey = (((pteflags & H_PTE_PKEY_BIT4) ? HPTE_R_KEY_BIT4 : 0x0UL) |
		    ((pteflags & H_PTE_PKEY_BIT3) ? HPTE_R_KEY_BIT3 : 0x0UL) |
		    ((pteflags & H_PTE_PKEY_BIT2) ? HPTE_R_KEY_BIT2 : 0x0UL) |
		    ((pteflags & H_PTE_PKEY_BIT1) ? HPTE_R_KEY_BIT1 : 0x0UL) |
		    ((pteflags & H_PTE_PKEY_BIT0) ? HPTE_R_KEY_BIT0 : 0x0UL));

	if (mmu_has_feature(MMU_FTR_BOOK3S_KUAP) ||
	    mmu_has_feature(MMU_FTR_BOOK3S_KUEP)) {
		if ((pte_pkey == 0) && (flags & HPTE_USE_KERNEL_KEY))
			return HASH_DEFAULT_KERNEL_KEY;
	}

	return pte_pkey;
}

static inline u16 hash__pte_to_pkey_bits(u64 pteflags)
{
	return (((pteflags & H_PTE_PKEY_BIT4) ? 0x10 : 0x0UL) |
		((pteflags & H_PTE_PKEY_BIT3) ? 0x8 : 0x0UL) |
		((pteflags & H_PTE_PKEY_BIT2) ? 0x4 : 0x0UL) |
		((pteflags & H_PTE_PKEY_BIT1) ? 0x2 : 0x0UL) |
		((pteflags & H_PTE_PKEY_BIT0) ? 0x1 : 0x0UL));
}

#endif
