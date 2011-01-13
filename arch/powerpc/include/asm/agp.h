#ifndef _ASM_POWERPC_AGP_H
#define _ASM_POWERPC_AGP_H
#ifdef __KERNEL__

#include <asm/io.h>

#define map_page_into_agp(page)
#define unmap_page_from_agp(page)
#define flush_agp_cache() mb()

/* GATT allocation. Returns/accepts GATT kernel virtual address. */
#define alloc_gatt_pages(order)		\
	((char *)__get_free_pages(GFP_KERNEL, (order)))
#define free_gatt_pages(table, order)	\
	free_pages((unsigned long)(table), (order))

#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_AGP_H */
