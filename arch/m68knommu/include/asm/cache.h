#ifndef __ARCH_M68KNOMMU_CACHE_H
#define __ARCH_M68KNOMMU_CACHE_H

/* bytes per L1 cache line */
#define        L1_CACHE_BYTES  16	/* this need to be at least 1 */

/* m68k-elf-gcc  2.95.2 doesn't like these */

#define __cacheline_aligned
#define ____cacheline_aligned

#endif
