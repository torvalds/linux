/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019 SiFive
 */

#ifndef _ASM_RISCV_SET_MEMORY_H
#define _ASM_RISCV_SET_MEMORY_H

#ifndef __ASSEMBLY__
/*
 * Functions to change memory attributes.
 */
#ifdef CONFIG_MMU
int set_memory_ro(unsigned long addr, int numpages);
int set_memory_rw(unsigned long addr, int numpages);
int set_memory_x(unsigned long addr, int numpages);
int set_memory_nx(unsigned long addr, int numpages);
int set_memory_rw_nx(unsigned long addr, int numpages);
static __always_inline int set_kernel_memory(char *startp, char *endp,
					     int (*set_memory)(unsigned long start,
							       int num_pages))
{
	unsigned long start = (unsigned long)startp;
	unsigned long end = (unsigned long)endp;
	int num_pages = PAGE_ALIGN(end - start) >> PAGE_SHIFT;

	return set_memory(start, num_pages);
}
#else
static inline int set_memory_ro(unsigned long addr, int numpages) { return 0; }
static inline int set_memory_rw(unsigned long addr, int numpages) { return 0; }
static inline int set_memory_x(unsigned long addr, int numpages) { return 0; }
static inline int set_memory_nx(unsigned long addr, int numpages) { return 0; }
static inline int set_memory_rw_nx(unsigned long addr, int numpages) { return 0; }
static inline int set_kernel_memory(char *startp, char *endp,
				    int (*set_memory)(unsigned long start,
						      int num_pages))
{
	return 0;
}
#endif

int set_direct_map_invalid_noflush(struct page *page);
int set_direct_map_default_noflush(struct page *page);
int set_direct_map_valid_noflush(struct page *page, unsigned nr, bool valid);
bool kernel_page_present(struct page *page);

#endif /* __ASSEMBLY__ */

#if defined(CONFIG_STRICT_KERNEL_RWX) || defined(CONFIG_XIP_KERNEL)
#ifdef CONFIG_64BIT
#define SECTION_ALIGN (1 << 21)
#else
#define SECTION_ALIGN (1 << 22)
#endif
#else /* !CONFIG_STRICT_KERNEL_RWX */
#define SECTION_ALIGN L1_CACHE_BYTES
#endif /* CONFIG_STRICT_KERNEL_RWX */

#define PECOFF_SECTION_ALIGNMENT        0x1000
#define PECOFF_FILE_ALIGNMENT           0x200

#endif /* _ASM_RISCV_SET_MEMORY_H */
