/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASMARM_SPARSEMEM_H
#define ASMARM_SPARSEMEM_H

#include <asm/memory.h>

/*
 * Two definitions are required for sparsemem:
 *
 * MAX_PHYSMEM_BITS: The number of physical address bits required
 *   to address the last byte of memory.
 *
 * SECTION_SIZE_BITS: The number of physical address bits to cover
 *   the maximum amount of memory in a section.
 *
 * Eg, if you have 2 banks of up to 64MB at 0x80000000, 0x84000000,
 * then MAX_PHYSMEM_BITS is 32, SECTION_SIZE_BITS is 26.
 *
 * These can be overridden in your mach/memory.h.
 */
#if !defined(MAX_PHYSMEM_BITS) || !defined(SECTION_SIZE_BITS)
#define MAX_PHYSMEM_BITS	36
#define SECTION_SIZE_BITS	28
#endif

#endif
