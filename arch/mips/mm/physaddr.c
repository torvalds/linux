// SPDX-License-Identifier: GPL-2.0
#include <linux/bug.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/mmdebug.h>
#include <linux/mm.h>

#include <asm/addrspace.h>
#include <asm/sections.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/dma.h>

static inline bool __debug_virt_addr_valid(unsigned long x)
{
	/*
	 * MAX_DMA_ADDRESS is a virtual address that may not correspond to an
	 * actual physical address. Enough code relies on
	 * virt_to_phys(MAX_DMA_ADDRESS) that we just need to work around it
	 * and always return true.
	 */
	if (x == MAX_DMA_ADDRESS)
		return true;

	return x >= PAGE_OFFSET && (KSEGX(x) < KSEG2 ||
	       IS_ENABLED(CONFIG_EVA) ||
	       !IS_ENABLED(CONFIG_HIGHMEM));
}

phys_addr_t __virt_to_phys(volatile const void *x)
{
	WARN(!__debug_virt_addr_valid((unsigned long)x),
	     "virt_to_phys used for non-linear address: %p (%pS)\n",
	     x, x);

	return __virt_to_phys_nodebug(x);
}
EXPORT_SYMBOL(__virt_to_phys);

phys_addr_t __phys_addr_symbol(unsigned long x)
{
	/* This is bounds checking against the kernel image only.
	 * __pa_symbol should only be used on kernel symbol addresses.
	 */
	VIRTUAL_BUG_ON(x < (unsigned long)_text ||
		       x > (unsigned long)_end);

	return __pa_symbol_nodebug(x);
}
EXPORT_SYMBOL(__phys_addr_symbol);
