/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2001-2008 Silicon Graphics, Inc.  All rights reserved.
 *
 * Prototypes for the uncached page allocator
 */

extern unsigned long uncached_alloc_page(int starting_nid, int n_pages);
extern void uncached_free_page(unsigned long uc_addr, int n_pages);
