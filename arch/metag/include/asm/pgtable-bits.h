/*
 * Meta page table definitions.
 */

#ifndef _METAG_PGTABLE_BITS_H
#define _METAG_PGTABLE_BITS_H

#include <asm/metag_mem.h>

/*
 * Definitions for MMU descriptors
 *
 * These are the hardware bits in the MMCU pte entries.
 * Derived from the Meta toolkit headers.
 */
#define _PAGE_PRESENT		MMCU_ENTRY_VAL_BIT
#define _PAGE_WRITE		MMCU_ENTRY_WR_BIT
#define _PAGE_PRIV		MMCU_ENTRY_PRIV_BIT
/* Write combine bit - this can cause writes to occur out of order */
#define _PAGE_WR_COMBINE	MMCU_ENTRY_WRC_BIT
/* Sys coherent bit - this bit is never used by Linux */
#define _PAGE_SYS_COHERENT	MMCU_ENTRY_SYS_BIT
#define _PAGE_ALWAYS_ZERO_1	0x020
#define _PAGE_CACHE_CTRL0	0x040
#define _PAGE_CACHE_CTRL1	0x080
#define _PAGE_ALWAYS_ZERO_2	0x100
#define _PAGE_ALWAYS_ZERO_3	0x200
#define _PAGE_ALWAYS_ZERO_4	0x400
#define _PAGE_ALWAYS_ZERO_5	0x800

/* These are software bits that we stuff into the gaps in the hardware
 * pte entries that are not used.  Note, these DO get stored in the actual
 * hardware, but the hardware just does not use them.
 */
#define _PAGE_ACCESSED		_PAGE_ALWAYS_ZERO_1
#define _PAGE_DIRTY		_PAGE_ALWAYS_ZERO_2

/* Pages owned, and protected by, the kernel. */
#define _PAGE_KERNEL		_PAGE_PRIV

/* No cacheing of this page */
#define _PAGE_CACHE_WIN0	(MMCU_CWIN_UNCACHED << MMCU_ENTRY_CWIN_S)
/* burst cacheing - good for data streaming */
#define _PAGE_CACHE_WIN1	(MMCU_CWIN_BURST << MMCU_ENTRY_CWIN_S)
/* One cache way per thread */
#define _PAGE_CACHE_WIN2	(MMCU_CWIN_C1SET << MMCU_ENTRY_CWIN_S)
/* Full on cacheing */
#define _PAGE_CACHE_WIN3	(MMCU_CWIN_CACHED << MMCU_ENTRY_CWIN_S)

#define _PAGE_CACHEABLE		(_PAGE_CACHE_WIN3 | _PAGE_WR_COMBINE)

/* which bits are used for cache control ... */
#define _PAGE_CACHE_MASK	(_PAGE_CACHE_CTRL0 | _PAGE_CACHE_CTRL1 | \
				 _PAGE_WR_COMBINE)

/* This is a mask of the bits that pte_modify is allowed to change. */
#define _PAGE_CHG_MASK		(PAGE_MASK)

#define _PAGE_SZ_SHIFT		1
#define _PAGE_SZ_4K		(0x0)
#define _PAGE_SZ_8K		(0x1 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_16K		(0x2 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_32K		(0x3 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_64K		(0x4 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_128K		(0x5 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_256K		(0x6 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_512K		(0x7 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_1M		(0x8 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_2M		(0x9 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_4M		(0xa << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_MASK		(0xf << _PAGE_SZ_SHIFT)

#if defined(CONFIG_PAGE_SIZE_4K)
#define _PAGE_SZ		(_PAGE_SZ_4K)
#elif defined(CONFIG_PAGE_SIZE_8K)
#define _PAGE_SZ		(_PAGE_SZ_8K)
#elif defined(CONFIG_PAGE_SIZE_16K)
#define _PAGE_SZ		(_PAGE_SZ_16K)
#endif
#define _PAGE_TABLE		(_PAGE_SZ | _PAGE_PRESENT)

#if defined(CONFIG_HUGETLB_PAGE_SIZE_8K)
# define _PAGE_SZHUGE		(_PAGE_SZ_8K)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_16K)
# define _PAGE_SZHUGE		(_PAGE_SZ_16K)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_32K)
# define _PAGE_SZHUGE		(_PAGE_SZ_32K)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_64K)
# define _PAGE_SZHUGE		(_PAGE_SZ_64K)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_128K)
# define _PAGE_SZHUGE		(_PAGE_SZ_128K)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_256K)
# define _PAGE_SZHUGE		(_PAGE_SZ_256K)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_512K)
# define _PAGE_SZHUGE		(_PAGE_SZ_512K)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_1M)
# define _PAGE_SZHUGE		(_PAGE_SZ_1M)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_2M)
# define _PAGE_SZHUGE		(_PAGE_SZ_2M)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_4M)
# define _PAGE_SZHUGE		(_PAGE_SZ_4M)
#endif

#endif /* _METAG_PGTABLE_BITS_H */
