#ifndef _ASM_X86_PAGE_64_H
#define _ASM_X86_PAGE_64_H

#include <asm/page_64_types.h>

#ifndef __ASSEMBLY__

/* duplicated to the one in bootmem.h */
extern unsigned long max_pfn;
extern unsigned long phys_base;

extern unsigned long __phys_addr(unsigned long);

#define __phys_reloc_hide(x)	(x)

#ifdef CONFIG_FLATMEM
#define pfn_valid(pfn)          ((pfn) < max_pfn)
#endif

void clear_page(void *page);
void copy_page(void *to, void *from);

#endif	/* !__ASSEMBLY__ */

#endif /* _ASM_X86_PAGE_64_H */
