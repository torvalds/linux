/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_RISCV_VMALLOC_H
#define _ASM_RISCV_VMALLOC_H

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP

extern bool pgtable_l4_enabled, pgtable_l5_enabled;

#define IOREMAP_MAX_ORDER (PUD_SHIFT)

#define arch_vmap_pud_supported arch_vmap_pud_supported
static inline bool arch_vmap_pud_supported(pgprot_t prot)
{
	return pgtable_l4_enabled || pgtable_l5_enabled;
}

#define arch_vmap_pmd_supported arch_vmap_pmd_supported
static inline bool arch_vmap_pmd_supported(pgprot_t prot)
{
	return true;
}

#endif

#endif /* _ASM_RISCV_VMALLOC_H */
