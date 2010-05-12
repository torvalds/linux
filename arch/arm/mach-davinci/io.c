/*
 * DaVinci I/O mapping code
 *
 * Copyright (C) 2005-2006 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/io.h>

#include <asm/tlb.h>

#define BETWEEN(p, st, sz)	((p) >= (st) && (p) < ((st) + (sz)))
#define XLATE(p, pst, vst)	((void __iomem *)((p) - (pst) + (vst)))

/*
 * Intercept ioremap() requests for addresses in our fixed mapping regions.
 */
void __iomem *davinci_ioremap(unsigned long p, size_t size, unsigned int type)
{
	if (BETWEEN(p, IO_PHYS, IO_SIZE))
		return XLATE(p, IO_PHYS, IO_VIRT);

	return __arm_ioremap_caller(p, size, type, __builtin_return_address(0));
}
EXPORT_SYMBOL(davinci_ioremap);

void davinci_iounmap(volatile void __iomem *addr)
{
	unsigned long virt = (unsigned long)addr;

	if (virt >= VMALLOC_START && virt < VMALLOC_END)
		__iounmap(addr);
}
EXPORT_SYMBOL(davinci_iounmap);
