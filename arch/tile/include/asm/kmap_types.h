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
 * In TILE Linux each set of four of these uses another 16MB chunk of
 * address space, given 64 tiles and 64KB pages, so we only enable
 * ones that are required by the kernel configuration.
 */
enum km_type {
	KM_BOUNCE_READ,
	KM_SKB_SUNRPC_DATA,
	KM_SKB_DATA_SOFTIRQ,
	KM_USER0,
	KM_USER1,
	KM_BIO_SRC_IRQ,
	KM_IRQ0,
	KM_IRQ1,
	KM_SOFTIRQ0,
	KM_SOFTIRQ1,
	KM_MEMCPY0,
	KM_MEMCPY1,
#if defined(CONFIG_HIGHPTE)
	KM_PTE0,
	KM_PTE1,
#endif
	KM_TYPE_NR
};

#endif /* _ASM_TILE_KMAP_TYPES_H */
