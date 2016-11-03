#ifndef _ASM_LKL_PAGE_H
#define _ASM_LKL_PAGE_H

#define CONFIG_KERNEL_RAM_BASE_ADDRESS memory_start

#ifndef __ASSEMBLY__
void free_mem(void);
void bootmem_init(unsigned long mem_size);
#endif

#include <asm-generic/page.h>

#endif /* _ASM_LKL_PAGE_H */
