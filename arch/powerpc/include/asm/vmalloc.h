#ifndef _ASM_POWERPC_VMALLOC_H
#define _ASM_POWERPC_VMALLOC_H

#include <asm/mmu.h>
#include <asm/page.h>

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP
static inline bool arch_vmap_p4d_supported(pgprot_t prot)
{
	return false;
}

static inline bool arch_vmap_pud_supported(pgprot_t prot)
{
	/* HPT does not cope with large pages in the vmalloc area */
	return radix_enabled();
}

static inline bool arch_vmap_pmd_supported(pgprot_t prot)
{
	return radix_enabled();
}
#endif

#endif /* _ASM_POWERPC_VMALLOC_H */
