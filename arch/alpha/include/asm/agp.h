/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AGP_H
#define AGP_H 1

#include <asm/io.h>

/* dummy for now */

#define map_page_into_agp(page)		do { } while (0)
#define unmap_page_from_agp(page)	do { } while (0)
#define flush_agp_cache() mb()

#endif
