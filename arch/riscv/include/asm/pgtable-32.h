/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_PGTABLE_32_H
#define _ASM_RISCV_PGTABLE_32_H

#include <asm-generic/pgtable-nopmd.h>
#include <linux/bits.h>
#include <linux/const.h>

/* Size of region mapped by a page global directory */
#define PGDIR_SHIFT     22
#define PGDIR_SIZE      (_AC(1, UL) << PGDIR_SHIFT)
#define PGDIR_MASK      (~(PGDIR_SIZE - 1))

#define MAX_POSSIBLE_PHYSMEM_BITS 34

/*
 * rv32 PTE format:
 * | XLEN-1  10 | 9             8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
 *       PFN      reserved for SW   D   A   G   U   X   W   R   V
 */
#define _PAGE_PFN_MASK  GENMASK(31, 10)

#endif /* _ASM_RISCV_PGTABLE_32_H */
