/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_KMAP_TYPES_H
#define _ASM_TILE_KMAP_TYPES_H

/*
 * In 32-bit TILE Linux we have to balance the desire to have a lot of
 * nested atomic mappings with the fact that large page sizes and many
 * processors chew up address space quickly.  In a typical
 * 64-processor, 64KB-page layout build, making KM_TYPE_NR one larger
 * adds 4MB of required address-space.  For now we leave KM_TYPE_NR
 * set to depth 8.
 */
enum km_type {
	KM_TYPE_NR = 8
};

/*
 * We provide dummy definitions of all the stray values that used to be
 * required for kmap_atomic() and no longer are.
 */
enum {
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
	KM_SYNC_ICACHE,
	KM_SYNC_DCACHE,
	KM_UML_USERCOPY,
	KM_IRQ_PTE,
	KM_NMI,
	KM_NMI_PTE,
	KM_KDB
};

#endif /* _ASM_TILE_KMAP_TYPES_H */
