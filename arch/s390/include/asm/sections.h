/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _S390_SECTIONS_H
#define _S390_SECTIONS_H

#define arch_is_kernel_initmem_freed arch_is_kernel_initmem_freed

#include <asm-generic/sections.h>

extern bool initmem_freed;

static inline int arch_is_kernel_initmem_freed(unsigned long addr)
{
	if (!initmem_freed)
		return 0;
	return addr >= (unsigned long)__init_begin &&
	       addr < (unsigned long)__init_end;
}

/*
 * .boot.data section contains variables "shared" between the decompressor and
 * the decompressed kernel. The decompressor will store values in them, and
 * copy over to the decompressed image before starting it.
 *
 * Each variable end up in its own intermediate section .boot.data.<var name>,
 * those sections are later sorted by alignment + name and merged together into
 * final .boot.data section, which should be identical in the decompressor and
 * the decompressed kernel (that is checked during the build).
 */
#define __bootdata(var) __section(".boot.data." #var) var

/*
 * .boot.preserved.data is similar to .boot.data, but it is not part of the
 * .init section and thus will be preserved for later use in the decompressed
 * kernel.
 */
#define __bootdata_preserved(var) __section(".boot.preserved.data." #var) var

extern unsigned long __sdma, __edma;
extern unsigned long __stext_dma, __etext_dma;

#endif
