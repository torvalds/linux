/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_AGP_H
#define _ASM_X86_AGP_H

#include <linux/pgtable.h>
#include <asm/cacheflush.h>

/*
 * Functions to keep the agpgart mappings coherent with the MMU. The
 * GART gives the CPU a physical alias of pages in memory. The alias
 * region is mapped uncacheable. Make sure there are no conflicting
 * mappings with different cacheability attributes for the same
 * page. This avoids data corruption on some CPUs.
 */

#define map_page_into_agp(page) set_pages_uc(page, 1)
#define unmap_page_from_agp(page) set_pages_wb(page, 1)

/*
 * Could use CLFLUSH here if the cpu supports it. But then it would
 * need to be called for each cacheline of the whole page so it may
 * not be worth it. Would need a page for it.
 */
#define flush_agp_cache() wbinvd()

#endif /* _ASM_X86_AGP_H */
