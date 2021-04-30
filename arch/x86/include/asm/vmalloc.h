#ifndef _ASM_X86_VMALLOC_H
#define _ASM_X86_VMALLOC_H

#include <asm/cpufeature.h>
#include <asm/page.h>
#include <asm/pgtable_areas.h>

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP
static inline bool arch_vmap_p4d_supported(pgprot_t prot)
{
	return false;
}

static inline bool arch_vmap_pud_supported(pgprot_t prot)
{
#ifdef CONFIG_X86_64
	return boot_cpu_has(X86_FEATURE_GBPAGES);
#else
	return false;
#endif
}

static inline bool arch_vmap_pmd_supported(pgprot_t prot)
{
	return boot_cpu_has(X86_FEATURE_PSE);
}
#endif

#endif /* _ASM_X86_VMALLOC_H */
