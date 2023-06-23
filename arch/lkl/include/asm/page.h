#ifndef _ASM_LKL_PAGE_H
#define _ASM_LKL_PAGE_H

#define ARCH_PFN_OFFSET	(memory_start >> PAGE_SHIFT)

#ifndef __ASSEMBLY__
void free_mem(void);
void bootmem_init(unsigned long mem_size);
#endif

#include <asm-generic/page.h>

#undef PAGE_OFFSET
#define PAGE_OFFSET memory_start

#endif /* _ASM_LKL_PAGE_H */
