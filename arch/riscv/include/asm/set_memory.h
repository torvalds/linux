/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019 SiFive
 */

#ifndef _ASM_RISCV_SET_MEMORY_H
#define _ASM_RISCV_SET_MEMORY_H

/*
 * Functions to change memory attributes.
 */
#ifdef CONFIG_MMU
int set_memory_ro(unsigned long addr, int numpages);
int set_memory_rw(unsigned long addr, int numpages);
int set_memory_x(unsigned long addr, int numpages);
int set_memory_nx(unsigned long addr, int numpages);
#else
static inline int set_memory_ro(unsigned long addr, int numpages) { return 0; }
static inline int set_memory_rw(unsigned long addr, int numpages) { return 0; }
static inline int set_memory_x(unsigned long addr, int numpages) { return 0; }
static inline int set_memory_nx(unsigned long addr, int numpages) { return 0; }
#endif

int set_direct_map_invalid_noflush(struct page *page);
int set_direct_map_default_noflush(struct page *page);

#endif /* _ASM_RISCV_SET_MEMORY_H */
