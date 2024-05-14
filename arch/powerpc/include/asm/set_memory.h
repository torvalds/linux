/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_SET_MEMORY_H
#define _ASM_POWERPC_SET_MEMORY_H

#define SET_MEMORY_RO	0
#define SET_MEMORY_RW	1
#define SET_MEMORY_NX	2
#define SET_MEMORY_X	3
#define SET_MEMORY_NP	4	/* Set memory non present */
#define SET_MEMORY_P	5	/* Set memory present */

int change_memory_attr(unsigned long addr, int numpages, long action);

static inline int set_memory_ro(unsigned long addr, int numpages)
{
	return change_memory_attr(addr, numpages, SET_MEMORY_RO);
}

static inline int set_memory_rw(unsigned long addr, int numpages)
{
	return change_memory_attr(addr, numpages, SET_MEMORY_RW);
}

static inline int set_memory_nx(unsigned long addr, int numpages)
{
	return change_memory_attr(addr, numpages, SET_MEMORY_NX);
}

static inline int set_memory_x(unsigned long addr, int numpages)
{
	return change_memory_attr(addr, numpages, SET_MEMORY_X);
}

static inline int set_memory_np(unsigned long addr, int numpages)
{
	return change_memory_attr(addr, numpages, SET_MEMORY_NP);
}

static inline int set_memory_p(unsigned long addr, int numpages)
{
	return change_memory_attr(addr, numpages, SET_MEMORY_P);
}

#endif
