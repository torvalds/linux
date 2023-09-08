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

int __set_memory(unsigned long addr, int numpages, unsigned long flags);

static inline int set_memory_ro(unsigned long addr, int numpages)
{
	return __set_memory(addr, numpages, SET_MEMORY_RO);
}

static inline int set_memory_rw(unsigned long addr, int numpages)
{
	return __set_memory(addr, numpages, SET_MEMORY_RW);
}

static inline int set_memory_nx(unsigned long addr, int numpages)
{
	return __set_memory(addr, numpages, SET_MEMORY_NX);
}

static inline int set_memory_x(unsigned long addr, int numpages)
{
	return __set_memory(addr, numpages, SET_MEMORY_X);
}

#define set_memory_rox set_memory_rox
static inline int set_memory_rox(unsigned long addr, int numpages)
{
	return __set_memory(addr, numpages, SET_MEMORY_RO | SET_MEMORY_X);
}

static inline int set_memory_rwnx(unsigned long addr, int numpages)
{
	return __set_memory(addr, numpages, SET_MEMORY_RW | SET_MEMORY_NX);
}

static inline int set_memory_4k(unsigned long addr, int numpages)
{
	return __set_memory(addr, numpages, SET_MEMORY_4K);
}

int set_direct_map_invalid_noflush(struct page *page);
int set_direct_map_default_noflush(struct page *page);

#endif
