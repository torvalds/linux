/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASMS390_SET_MEMORY_H
#define _ASMS390_SET_MEMORY_H

#include <linux/mutex.h>

extern struct mutex cpa_mutex;

enum {
	_SET_MEMORY_RO_BIT,
	_SET_MEMORY_RW_BIT,
	_SET_MEMORY_NX_BIT,
	_SET_MEMORY_X_BIT,
	_SET_MEMORY_4K_BIT,
	_SET_MEMORY_INV_BIT,
	_SET_MEMORY_DEF_BIT,
};

#define SET_MEMORY_RO	BIT(_SET_MEMORY_RO_BIT)
#define SET_MEMORY_RW	BIT(_SET_MEMORY_RW_BIT)
#define SET_MEMORY_NX	BIT(_SET_MEMORY_NX_BIT)
#define SET_MEMORY_X	BIT(_SET_MEMORY_X_BIT)
#define SET_MEMORY_4K	BIT(_SET_MEMORY_4K_BIT)
#define SET_MEMORY_INV	BIT(_SET_MEMORY_INV_BIT)
#define SET_MEMORY_DEF	BIT(_SET_MEMORY_DEF_BIT)

int __set_memory(unsigned long addr, unsigned long numpages, unsigned long flags);

#define set_memory_rox set_memory_rox

/*
 * Generate two variants of each set_memory() function:
 *
 * set_memory_yy(unsigned long addr, int numpages);
 * __set_memory_yy(void *start, void *end);
 *
 * The second variant exists for both convenience to avoid the usual
 * (unsigned long) casts, but unlike the first variant it can also be used
 * for areas larger than 8TB, which may happen at memory initialization.
 */
#define __SET_MEMORY_FUNC(fname, flags)					\
static inline int fname(unsigned long addr, int numpages)		\
{									\
	return __set_memory(addr, numpages, (flags));			\
}									\
									\
static inline int __##fname(void *start, void *end)			\
{									\
	unsigned long numpages;						\
									\
	numpages = (end - start) >> PAGE_SHIFT;				\
	return __set_memory((unsigned long)start, numpages, (flags));	\
}

__SET_MEMORY_FUNC(set_memory_ro, SET_MEMORY_RO)
__SET_MEMORY_FUNC(set_memory_rw, SET_MEMORY_RW)
__SET_MEMORY_FUNC(set_memory_nx, SET_MEMORY_NX)
__SET_MEMORY_FUNC(set_memory_x, SET_MEMORY_X)
__SET_MEMORY_FUNC(set_memory_rox, SET_MEMORY_RO | SET_MEMORY_X)
__SET_MEMORY_FUNC(set_memory_rwnx, SET_MEMORY_RW | SET_MEMORY_NX)
__SET_MEMORY_FUNC(set_memory_4k, SET_MEMORY_4K)

int set_direct_map_invalid_noflush(struct page *page);
int set_direct_map_default_noflush(struct page *page);
int set_direct_map_valid_noflush(struct page *page, unsigned nr, bool valid);
bool kernel_page_present(struct page *page);

#endif
