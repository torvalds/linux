/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_SECTIONS_H
#define _ASM_TILE_SECTIONS_H

#define arch_is_kernel_data arch_is_kernel_data

#include <asm-generic/sections.h>

/* Text and data are at different areas in the kernel VA space. */
extern char _sinitdata[], _einitdata[];

/* Write-once data is writable only till the end of initialization. */
extern char __w1data_begin[], __w1data_end[];

extern char __feedback_section_start[], __feedback_section_end[];

/* Handle the discontiguity between _sdata and _stext. */
static inline int arch_is_kernel_data(unsigned long addr)
{
	return addr >= (unsigned long)_sdata &&
		addr < (unsigned long)_end;
}

#endif /* _ASM_TILE_SECTIONS_H */
