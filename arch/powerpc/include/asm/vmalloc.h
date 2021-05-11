#ifndef _ASM_POWERPC_VMALLOC_H
#define _ASM_POWERPC_VMALLOC_H

#include <asm/mmu.h>
#include <asm/page.h>

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP

#define arch_vmap_pud_supported arch_vmap_pud_supported
static inline bool arch_vmap_pud_supported(pgprot_t prot)
{
	/* HPT does not cope with large pages in the vmalloc area */
	return radix_enabled();
}

#define arch_vmap_pmd_supported arch_vmap_pmd_supported
static inline bool arch_vmap_pmd_supported(pgprot_t prot)
{
	return radix_enabled();
}

#endif

#endif /* _ASM_POWERPC_VMALLOC_H */
