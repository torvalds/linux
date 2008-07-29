/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/device.h>	/* devres_*(), devm_ioremap_release() */
#include <linux/io.h>		/* ioremap_flags() */
#include <linux/module.h>	/* EXPORT_SYMBOL() */

/**
 * devm_ioremap_prot - Managed ioremap_flags()
 * @dev: Generic device to remap IO address for
 * @offset: BUS offset to map
 * @size: Size of map
 * @flags: Page flags
 *
 * Managed ioremap_prot().  Map is automatically unmapped on driver
 * detach.
 */
void __iomem *devm_ioremap_prot(struct device *dev, resource_size_t offset,
				 size_t size, unsigned long flags)
{
	void __iomem **ptr, *addr;

	ptr = devres_alloc(devm_ioremap_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	addr = ioremap_flags(offset, size, flags);
	if (addr) {
		*ptr = addr;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return addr;
}
EXPORT_SYMBOL(devm_ioremap_prot);
