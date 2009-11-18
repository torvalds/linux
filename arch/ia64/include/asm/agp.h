#ifndef _ASM_IA64_AGP_H
#define _ASM_IA64_AGP_H

/*
 * IA-64 specific AGP definitions.
 *
 * Copyright (C) 2002-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

/*
 * To avoid memory-attribute aliasing issues, we require that the AGPGART engine operate
 * in coherent mode, which lets us map the AGP memory as normal (write-back) memory
 * (unlike x86, where it gets mapped "write-coalescing").
 */
#define map_page_into_agp(page)		/* nothing */
#define unmap_page_from_agp(page)	/* nothing */
#define flush_agp_cache()		mb()

/* GATT allocation. Returns/accepts GATT kernel virtual address. */
#define alloc_gatt_pages(order)		\
	((char *)__get_free_pages(GFP_KERNEL, (order)))
#define free_gatt_pages(table, order)	\
	free_pages((unsigned long)(table), (order))

#endif /* _ASM_IA64_AGP_H */
