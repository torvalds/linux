/* Highmem related constants */

#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

#include <asm/kmap_types.h>

enum fixed_addresses {
	FIX_HOLE,
#ifdef CONFIG_HIGHMEM
	FIX_KMAP_BEGIN,
	FIX_KMAP_END = (KM_TYPE_NR * NR_CPUS),
#endif
	__end_of_fixed_addresses
};

/* Leave one empty page between IO pages at 0xfd000000 and
 * the top of the fixmap.
 */
#define FIXADDR_TOP	(0xfcfff000UL)
#define FIXADDR_SIZE	((FIX_KMAP_END + 1) << PAGE_SHIFT)
#define FIXADDR_START	(FIXADDR_TOP - FIXADDR_SIZE)

#define __fix_to_virt(x)	(FIXADDR_TOP - ((x) << PAGE_SHIFT))

#endif
