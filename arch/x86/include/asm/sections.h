/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SECTIONS_H
#define _ASM_X86_SECTIONS_H

#define arch_is_kernel_initmem_freed arch_is_kernel_initmem_freed

#include <asm-generic/sections.h>
#include <asm/extable.h>

extern char __brk_base[], __brk_limit[];
extern char __end_rodata_aligned[];

#if defined(CONFIG_X86_64)
extern char __end_rodata_hpage_align[];
#endif

extern char __end_of_kernel_reserve[];

extern unsigned long _brk_start, _brk_end;

static inline bool arch_is_kernel_initmem_freed(unsigned long addr)
{
	/*
	 * If _brk_start has not been cleared, brk allocation is incomplete,
	 * and we can not make assumptions about its use.
	 */
	if (_brk_start)
		return 0;

	/*
	 * After brk allocation is complete, space between _brk_end and _end
	 * is available for allocation.
	 */
	return addr >= _brk_end && addr < (unsigned long)&_end;
}

#endif	/* _ASM_X86_SECTIONS_H */
