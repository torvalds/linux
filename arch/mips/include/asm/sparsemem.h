#ifndef _MIPS_SPARSEMEM_H
#define _MIPS_SPARSEMEM_H
#ifdef CONFIG_SPARSEMEM

/*
 * SECTION_SIZE_BITS		2^N: how big each section will be
 * MAX_PHYSMEM_BITS		2^N: how much memory we can have in that space
 */
#define SECTION_SIZE_BITS       28
#define MAX_PHYSMEM_BITS        35

#endif /* CONFIG_SPARSEMEM */
#endif /* _MIPS_SPARSEMEM_H */
