/*
 * Architecture-specific kernel symbols
 */

#ifdef CONFIG_VIRTUAL_MEM_MAP
#include <linux/compiler.h>
#include <linux/export.h>
#include <linux/bootmem.h>
EXPORT_SYMBOL(min_low_pfn);	/* defined by bootmem.c, but not exported by generic code */
EXPORT_SYMBOL(max_low_pfn);	/* defined by bootmem.c, but not exported by generic code */
#endif
