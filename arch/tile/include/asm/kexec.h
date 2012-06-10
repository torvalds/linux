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
 *
 * based on kexec.h from other architectures in linux-2.6.18
 */

#ifndef _ASM_TILE_KEXEC_H
#define _ASM_TILE_KEXEC_H

#include <asm/page.h>

#ifndef __tilegx__
/* Maximum physical address we can use pages from. */
#define KEXEC_SOURCE_MEMORY_LIMIT TASK_SIZE
/* Maximum address we can reach in physical address mode. */
#define KEXEC_DESTINATION_MEMORY_LIMIT TASK_SIZE
/* Maximum address we can use for the control code buffer. */
#define KEXEC_CONTROL_MEMORY_LIMIT TASK_SIZE
#else
/* We need to limit the memory below PGDIR_SIZE since
 * we only setup page table for [0, PGDIR_SIZE) before final kexec.
 */
/* Maximum physical address we can use pages from. */
#define KEXEC_SOURCE_MEMORY_LIMIT PGDIR_SIZE
/* Maximum address we can reach in physical address mode. */
#define KEXEC_DESTINATION_MEMORY_LIMIT PGDIR_SIZE
/* Maximum address we can use for the control code buffer. */
#define KEXEC_CONTROL_MEMORY_LIMIT PGDIR_SIZE
#endif

#define KEXEC_CONTROL_PAGE_SIZE	PAGE_SIZE

/*
 * We don't bother to provide a unique identifier, since we can only
 * reboot with a single type of kernel image anyway.
 */
#define KEXEC_ARCH KEXEC_ARCH_DEFAULT

/* Use the tile override for the page allocator. */
struct page *kimage_alloc_pages_arch(gfp_t gfp_mask, unsigned int order);
#define kimage_alloc_pages_arch kimage_alloc_pages_arch

#define MAX_NOTE_BYTES 1024

/* Defined in arch/tile/kernel/relocate_kernel.S */
extern const unsigned char relocate_new_kernel[];
extern const unsigned long relocate_new_kernel_size;
extern void relocate_new_kernel_end(void);

/* Provide a dummy definition to avoid build failures. */
static inline void crash_setup_regs(struct pt_regs *n, struct pt_regs *o)
{
}

#endif /* _ASM_TILE_KEXEC_H */
