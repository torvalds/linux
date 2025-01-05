/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_PAGE_H
#define _ASM_LKL_PAGE_H

#ifndef CONFIG_MMU
#define ARCH_PFN_OFFSET	(memory_start >> PAGE_SHIFT)
#include <asm-generic/page.h>
#else // CONFIG_MMU
#include <asm/page-mmu.h>
#endif // CONFIG_MMU

#ifndef __ASSEMBLY__
void free_mem(void);
void bootmem_init(unsigned long mem_size);
#endif

#undef PAGE_OFFSET
#define PAGE_OFFSET memory_start

#endif /* _ASM_LKL_PAGE_H */
