#ifndef _ASM_ARM64_VMALLOC_H
#define _ASM_ARM64_VMALLOC_H

#include <asm/page.h>
#include <asm/pgtable.h>

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP

#define arch_vmap_pud_supported arch_vmap_pud_supported
static inline bool arch_vmap_pud_supported(pgprot_t prot)
{
	/*
	 * SW table walks can't handle removal of intermediate entries.
	 */
	return pud_sect_supported() &&
	       !IS_ENABLED(CONFIG_PTDUMP_DEBUGFS);
}

#define arch_vmap_pmd_supported arch_vmap_pmd_supported
static inline bool arch_vmap_pmd_supported(pgprot_t prot)
{
	/* See arch_vmap_pud_supported() */
	return !IS_ENABLED(CONFIG_PTDUMP_DEBUGFS);
}

#endif

#define arch_vmap_pgprot_tagged arch_vmap_pgprot_tagged
static inline pgprot_t arch_vmap_pgprot_tagged(pgprot_t prot)
{
	return pgprot_tagged(prot);
}

#endif /* _ASM_ARM64_VMALLOC_H */
