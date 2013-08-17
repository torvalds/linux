/*
 *  arch/arm/include/asm/rodata.h
 *
 *  Copyright (C) 2011 Google, Inc.
 *
 *  Author: Colin Cross <ccross@android.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _ASMARM_RODATA_H
#define _ASMARM_RODATA_H

#ifndef __ASSEMBLY__

#ifdef CONFIG_DEBUG_RODATA

int set_memory_rw(unsigned long virt, int numpages);
int set_memory_ro(unsigned long virt, int numpages);

void mark_rodata_ro(void);
void set_kernel_text_rw(void);
void set_kernel_text_ro(void);
#else
static inline void set_kernel_text_rw(void) { }
static inline void set_kernel_text_ro(void) { }
#endif

#endif

#endif
