#ifndef _ASM_X86_VMALLOC_H
#define _ASM_X86_VMALLOC_H

#include <asm/cpufeature.h>
#include <asm/page.h>
#include <asm/pgtable_areas.h>

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP

#ifdef CONFIG_X86_64
#define arch_vmap_pud_supported arch_vmap_pud_supported
static inline bool arch_vmap_pud_supported(pgprot_t prot)
{
	return boot_cpu_has(X86_FEATURE_GBPAGES);
}
#endif

#define arch_vmap_pmd_supported arch_vmap_pmd_supported
static inline bool arch_vmap_pmd_supported(pgprot_t prot)
{
	return boot_cpu_has(X86_FEATURE_PSE);
}

#endif

#endif /* _ASM_X86_VMALLOC_H */
