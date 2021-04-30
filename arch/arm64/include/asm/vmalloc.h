#ifndef _ASM_ARM64_VMALLOC_H
#define _ASM_ARM64_VMALLOC_H

#include <asm/page.h>

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP
static inline bool arch_vmap_p4d_supported(pgprot_t prot)
{
	return false;
}

static inline bool arch_vmap_pud_supported(pgprot_t prot)
{
	/*
	 * Only 4k granule supports level 1 block mappings.
	 * SW table walks can't handle removal of intermediate entries.
	 */
	return IS_ENABLED(CONFIG_ARM64_4K_PAGES) &&
	       !IS_ENABLED(CONFIG_PTDUMP_DEBUGFS);
}

static inline bool arch_vmap_pmd_supported(pgprot_t prot)
{
	/* See arch_vmap_pud_supported() */
	return !IS_ENABLED(CONFIG_PTDUMP_DEBUGFS);
}
#endif

#endif /* _ASM_ARM64_VMALLOC_H */
