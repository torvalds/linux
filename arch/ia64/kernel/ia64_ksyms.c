// SPDX-License-Identifier: GPL-2.0
/*
 * Architecture-specific kernel symbols
 */

#if defined(CONFIG_VIRTUAL_MEM_MAP) || defined(CONFIG_DISCONTIGMEM)
#include <linux/compiler.h>
#include <linux/export.h>
#include <linux/memblock.h>
EXPORT_SYMBOL(min_low_pfn);	/* defined by bootmem.c, but not exported by generic code */
EXPORT_SYMBOL(max_low_pfn);	/* defined by bootmem.c, but not exported by generic code */
#endif
