/* ASB2305 PCI I/O mapping handler
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/pci.h>
#include <linux/module.h>

/*
 * Create a virtual mapping cookie for a PCI BAR (memory or IO)
 */
void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	resource_size_t start = pci_resource_start(dev, bar);
	resource_size_t len = pci_resource_len(dev, bar);
	unsigned long flags = pci_resource_flags(dev, bar);

	if (!len || !start)
		return NULL;

	if ((flags & IORESOURCE_IO) || (flags & IORESOURCE_MEM)) {
		if (flags & IORESOURCE_CACHEABLE && !(flags & IORESOURCE_IO))
			return ioremap(start, len);
		else
			return ioremap_nocache(start, len);
	}

	return NULL;
}
EXPORT_SYMBOL(pci_iomap);
