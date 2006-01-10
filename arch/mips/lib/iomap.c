/*
 *  iomap.c, Memory Mapped I/O routines for MIPS architecture.
 *
 *  This code is based on lib/iomap.c, by Linus Torvalds.
 *
 *  Copyright (C) 2004-2005  Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <asm/io.h>

void __iomem *ioport_map(unsigned long port, unsigned int nr)
{
	unsigned long end;

	end = port + nr - 1UL;
	if (ioport_resource.start > port ||
	    ioport_resource.end < end || port > end)
		return NULL;

	return (void __iomem *)(mips_io_port_base + port);
}

void ioport_unmap(void __iomem *addr)
{
}
EXPORT_SYMBOL(ioport_map);
EXPORT_SYMBOL(ioport_unmap);

void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	unsigned long start, len, flags;

	if (dev == NULL)
		return NULL;

	start = pci_resource_start(dev, bar);
	len = pci_resource_len(dev, bar);
	if (!start || !len)
		return NULL;

	if (maxlen != 0 && len > maxlen)
		len = maxlen;

	flags = pci_resource_flags(dev, bar);
	if (flags & IORESOURCE_IO)
		return ioport_map(start, len);
	if (flags & IORESOURCE_MEM) {
		if (flags & IORESOURCE_CACHEABLE)
			return ioremap_cacheable_cow(start, len);
		return ioremap_nocache(start, len);
	}

	return NULL;
}

void pci_iounmap(struct pci_dev *dev, void __iomem *addr)
{
	iounmap(addr);
}
EXPORT_SYMBOL(pci_iomap);
EXPORT_SYMBOL(pci_iounmap);
