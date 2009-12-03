#ifndef _ASM_POWERPC_KMAP_TYPES_H
#define _ASM_POWERPC_KMAP_TYPES_H

#ifdef __KERNEL__

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

enum km_type {
	KM_BOUNCE_READ,
	KM_SKB_SUNRPC_DATA,
	KM_SKB_DATA_SOFTIRQ,
	KM_USER0,
	KM_USER1,
	KM_BIO_SRC_IRQ,
	KM_BIO_DST_IRQ,
	KM_PTE0,
	KM_PTE1,
	KM_IRQ0,
	KM_IRQ1,
	KM_SOFTIRQ0,
	KM_SOFTIRQ1,
	KM_PPC_SYNC_PAGE,
	KM_PPC_SYNC_ICACHE,
	KM_TYPE_NR
};

/*
 * This is a temporary build fix that (so they say on lkml....) should no longer
 * be required after 2.6.33, because of changes planned to the kmap code.
 * Let's try to remove this cruft then.
 */
#ifdef CONFIG_DEBUG_HIGHMEM
#define KM_NMI		(-1)
#define KM_NMI_PTE	(-1)
#define KM_IRQ_PTE	(-1)
#endif

#endif	/* __KERNEL__ */
#endif	/* _ASM_POWERPC_KMAP_TYPES_H */
