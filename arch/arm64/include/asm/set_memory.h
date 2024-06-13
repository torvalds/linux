/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ASM_ARM64_SET_MEMORY_H
#define _ASM_ARM64_SET_MEMORY_H

#include <asm-generic/set_memory.h>

bool can_set_direct_map(void);
#define can_set_direct_map can_set_direct_map

int set_memory_valid(unsigned long addr, int numpages, int enable);

int arch_set_direct_map_range_uncached(unsigned long addr, unsigned long numpages);
int set_direct_map_invalid_noflush(struct page *page);
int set_direct_map_default_noflush(struct page *page);
bool kernel_page_present(struct page *page);

#endif /* _ASM_ARM64_SET_MEMORY_H */
