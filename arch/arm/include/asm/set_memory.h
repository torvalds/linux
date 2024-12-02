/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 1999-2002 Russell King
 */

#ifndef _ASMARM_SET_MEMORY_H
#define _ASMARM_SET_MEMORY_H

#ifdef CONFIG_MMU
int set_memory_ro(unsigned long addr, int numpages);
int set_memory_rw(unsigned long addr, int numpages);
int set_memory_x(unsigned long addr, int numpages);
int set_memory_nx(unsigned long addr, int numpages);
int set_memory_valid(unsigned long addr, int numpages, int enable);
#else
static inline int set_memory_ro(unsigned long addr, int numpages) { return 0; }
static inline int set_memory_rw(unsigned long addr, int numpages) { return 0; }
static inline int set_memory_x(unsigned long addr, int numpages) { return 0; }
static inline int set_memory_nx(unsigned long addr, int numpages) { return 0; }
#endif

#endif
