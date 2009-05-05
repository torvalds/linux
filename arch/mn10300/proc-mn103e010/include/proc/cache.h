/* MN103E010 Cache specification
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_PROC_CACHE_H
#define _ASM_PROC_CACHE_H

/* L1 cache */

#define L1_CACHE_NWAYS		4	/* number of ways in caches */
#define L1_CACHE_NENTRIES	256	/* number of entries in each way */
#define L1_CACHE_BYTES		16	/* bytes per entry */
#define L1_CACHE_SHIFT		4	/* shift for bytes per entry */
#define L1_CACHE_WAYDISP	0x1000	/* displacement of one way from the next */

#define L1_CACHE_TAG_VALID	0x00000001	/* cache tag valid bit */
#define L1_CACHE_TAG_DIRTY	0x00000008	/* data cache tag dirty bit */
#define L1_CACHE_TAG_ENTRY	0x00000ff0	/* cache tag entry address mask */
#define L1_CACHE_TAG_ADDRESS	0xfffff000	/* cache tag line address mask */

/*
 * specification of the interval between interrupt checking intervals whilst
 * managing the cache with the interrupts disabled
 */
#define MN10300_DCACHE_INV_RANGE_INTR_LOG2_INTERVAL	4

#endif /* _ASM_PROC_CACHE_H */
