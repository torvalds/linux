/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_AGP_H
#define _ASM_PARISC_AGP_H

/*
 * PARISC specific AGP definitions.
 * Copyright (c) 2006 Kyle McMartin <kyle@parisc-linux.org>
 *
 */

#define map_page_into_agp(page)		do { } while (0)
#define unmap_page_from_agp(page)	do { } while (0)
#define flush_agp_cache()		mb()

#endif /* _ASM_PARISC_AGP_H */
