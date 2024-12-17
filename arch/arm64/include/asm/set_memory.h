/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ASM_ARM64_SET_MEMORY_H
#define _ASM_ARM64_SET_MEMORY_H

#include <asm/mem_encrypt.h>
#include <asm-generic/set_memory.h>

bool can_set_direct_map(void);
#define can_set_direct_map can_set_direct_map

int set_memory_valid(unsigned long addr, int numpages, int enable);

int set_direct_map_invalid_noflush(struct page *page);
int set_direct_map_default_noflush(struct page *page);
int set_direct_map_valid_noflush(struct page *page, unsigned nr, bool valid);
bool kernel_page_present(struct page *page);

int set_memory_encrypted(unsigned long addr, int numpages);
int set_memory_decrypted(unsigned long addr, int numpages);

#endif /* _ASM_ARM64_SET_MEMORY_H */
