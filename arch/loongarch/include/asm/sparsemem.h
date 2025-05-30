/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LOONGARCH_SPARSEMEM_H
#define _LOONGARCH_SPARSEMEM_H

#ifdef CONFIG_SPARSEMEM

/*
 * SECTION_SIZE_BITS		2^N: how big each section will be
 * MAX_PHYSMEM_BITS		2^N: how much memory we can have in that space
 */
#define SECTION_SIZE_BITS	29 /* 2^29 = Largest Huge Page Size */
#define MAX_PHYSMEM_BITS	48

#ifdef CONFIG_SPARSEMEM_VMEMMAP
#define VMEMMAP_SIZE	(sizeof(struct page) * (1UL << (cpu_pabits + 1 - PAGE_SHIFT)))
#endif

#endif /* CONFIG_SPARSEMEM */

#ifndef VMEMMAP_SIZE
#define VMEMMAP_SIZE	0	/* 1, For FLATMEM; 2, For SPARSEMEM without VMEMMAP. */
#endif

#define INIT_MEMBLOCK_RESERVED_REGIONS	(INIT_MEMBLOCK_REGIONS + NR_CPUS)

#endif /* _LOONGARCH_SPARSEMEM_H */
