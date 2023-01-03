#ifndef _ASM_RISCV_VMALLOC_H
#define _ASM_RISCV_VMALLOC_H

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP

#define IOREMAP_MAX_ORDER (PUD_SHIFT)

#define arch_vmap_pud_supported arch_vmap_pud_supported
static inline bool arch_vmap_pud_supported(pgprot_t prot)
{
	return true;
}

#define arch_vmap_pmd_supported arch_vmap_pmd_supported
static inline bool arch_vmap_pmd_supported(pgprot_t prot)
{
	return true;
}

#endif

#endif /* _ASM_RISCV_VMALLOC_H */
