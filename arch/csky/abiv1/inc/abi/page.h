/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

extern unsigned long shm_align_mask;
extern void flush_dcache_page(struct page *page);

static inline unsigned long pages_do_alias(unsigned long addr1,
					   unsigned long addr2)
{
	return (addr1 ^ addr2) & shm_align_mask;
}

static inline void clear_user_page(void *addr, unsigned long vaddr,
				   struct page *page)
{
	clear_page(addr);
	if (pages_do_alias((unsigned long) addr, vaddr & PAGE_MASK))
		flush_dcache_page(page);
}

static inline void copy_user_page(void *to, void *from, unsigned long vaddr,
				  struct page *page)
{
	copy_page(to, from);
	if (pages_do_alias((unsigned long) to, vaddr & PAGE_MASK))
		flush_dcache_page(page);
}
