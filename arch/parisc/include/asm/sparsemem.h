/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASM_PARISC_SPARSEMEM_H
#define ASM_PARISC_SPARSEMEM_H

/* We have these possible memory map layouts:
 * Astro: 0-3.75, 67.75-68, 4-64
 * zx1: 0-1, 257-260, 4-256
 * Stretch (N-class): 0-2, 4-32, 34-xxx
 */

#define MAX_PHYSMEM_BITS	39	/* 512 GB */
#define SECTION_SIZE_BITS	27	/* 128 MB */

#endif
