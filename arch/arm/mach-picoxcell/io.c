/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>

#include <asm/mach/map.h>

#include <mach/map.h>
#include <mach/picoxcell_soc.h>

#include "common.h"

void __init picoxcell_map_io(void)
{
	struct map_desc io_map = {
		.virtual	= PHYS_TO_IO(PICOXCELL_PERIPH_BASE),
		.pfn		= __phys_to_pfn(PICOXCELL_PERIPH_BASE),
		.length		= PICOXCELL_PERIPH_LENGTH,
		.type		= MT_DEVICE,
	};

	iotable_init(&io_map, 1);
}

void __iomem *picoxcell_ioremap(unsigned long p, size_t size,
				unsigned int type)
{
	if (unlikely(size == 0))
		return NULL;

	if (p >= PICOXCELL_PERIPH_BASE &&
	    p < PICOXCELL_PERIPH_BASE + PICOXCELL_PERIPH_LENGTH)
		return IO_ADDRESS(p);

	return __arm_ioremap_caller(p, size, type,
				    __builtin_return_address(0));
}
EXPORT_SYMBOL_GPL(picoxcell_ioremap);

void picoxcell_iounmap(volatile void __iomem *addr)
{
	unsigned long virt = (unsigned long)addr;

	if (virt >= VMALLOC_START && virt < VMALLOC_END)
		__iounmap(addr);
}
EXPORT_SYMBOL_GPL(picoxcell_iounmap);
