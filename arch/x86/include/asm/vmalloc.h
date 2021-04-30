#ifndef _ASM_X86_VMALLOC_H
#define _ASM_X86_VMALLOC_H

#include <asm/page.h>
#include <asm/pgtable_areas.h>

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP
bool arch_vmap_p4d_supported(pgprot_t prot);
bool arch_vmap_pud_supported(pgprot_t prot);
bool arch_vmap_pmd_supported(pgprot_t prot);
#endif

#endif /* _ASM_X86_VMALLOC_H */
