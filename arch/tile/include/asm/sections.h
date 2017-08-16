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

extern char vdso_start[], vdso_end[];
#ifdef CONFIG_COMPAT
extern char vdso32_start[], vdso32_end[];
#endif

/* Not exactly sections, but PC comparison points in the code. */
extern char __rt_sigreturn[], __rt_sigreturn_end[];
#ifdef __tilegx__
extern char __start_unalign_asm_code[], __end_unalign_asm_code[];
#else
extern char sys_cmpxchg[], __sys_cmpxchg_end[];
extern char __sys_cmpxchg_grab_lock[];
extern char __start_atomic_asm_code[], __end_atomic_asm_code[];
#endif

/* Handle the discontiguity between _sdata and _text. */
static inline int arch_is_kernel_data(unsigned long addr)
{
	return addr >= (unsigned long)_sdata &&
		addr < (unsigned long)_end;
}

#endif /* _ASM_TILE_SECTIONS_H */
