/*
 * rsrc_iodyn.c -- Resource management routines for MEM-static sockets.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * (C) 1999		David A. Hinds
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <pcmcia/ss.h>
#include <pcmcia/cistpl.h>
#include "cs_internal.h"


struct pcmcia_align_data {
	unsigned long	mask;
	unsigned long	offset;
};

static resource_size_t pcmcia_align(void *align_data,
				const struct resource *res,
				resource_size_t size, resource_size_t align)
{
	struct pcmcia_align_data *data = align_data;
	resource_size_t start;

	start = (res->start & ~data->mask) + data->offset;
	if (start < res->start)
		start += data->mask + 1;

#ifdef CONFIG_X86
	if (res->flags & IORESOURCE_IO) {
		if (start & 0x300)
			start = (start + 0x3ff) & ~0x3ff;
	}
#endif

#ifdef CONFIG_M68K
	if (res->flags & IORESOURCE_IO) {
		if ((res->start + size - 1) >= 1024)
			start = res->end;
	}
#endif

	return start;
}


static struct resource *__iodyn_find_io_region(struct pcmcia_socket *s,
					unsigned long base, int num,
					unsigned long align)
{
	struct resource *res = pcmcia_make_resource(0, num, IORESOURCE_IO,
						dev_name(&s->dev));
	struct pcmcia_align_data data;
	unsigned long min = base;
	int ret;

	data.mask = align - 1;
	data.offset = base & data.mask;

#ifdef CONFIG_PCI
	if (s->cb_dev) {
		ret = pci_bus_alloc_resource(s->cb_dev->bus, res, num, 1,
					     min, 0, pcmcia_align, &data);
	} else
#endif
		ret = allocate_resource(&ioport_resource, res, num, min, ~0UL,
					1, pcmcia_align, &data);

	if (ret != 0) {
		kfree(res);
		res = NULL;
	}
	return res;
}

static int iodyn_find_io(struct pcmcia_socket *s, unsigned int attr,
			unsigned int *base, unsigned int num,
			unsigned int align, struct resource **parent)
{
	int i, ret = 0;

	/* Check for an already-allocated window that must conflict with
	 * what was asked for.  It is a hack because it does not catch all
	 * potential conflicts, just the most obvious ones.
	 */
	for (i = 0; i < MAX_IO_WIN; i++) {
		if (!s->io[i].res)
			continue;

		if (!*base)
			continue;

		if ((s->io[i].res->start & (align-1)) == *base)
			return -EBUSY;
	}

	for (i = 0; i < MAX_IO_WIN; i++) {
		struct resource *res = s->io[i].res;
		unsigned int try;

		if (res && (res->flags & IORESOURCE_BITS) !=
			(attr & IORESOURCE_BITS))
			continue;

		if (!res) {
			if (align == 0)
				align = 0x10000;

			res = s->io[i].res = __iodyn_find_io_region(s, *base,
								num, align);
			if (!res)
				return -EINVAL;

			*base = res->start;
			s->io[i].res->flags =
				((res->flags & ~IORESOURCE_BITS) |
					(attr & IORESOURCE_BITS));
			s->io[i].InUse = num;
			*parent = res;
			return 0;
		}

		/* Try to extend top of window */
		try = res->end + 1;
		if ((*base == 0) || (*base == try)) {
			if (adjust_resource(s->io[i].res, res->start,
					res->end - res->start + num + 1))
				continue;
			*base = try;
			s->io[i].InUse += num;
			*parent = res;
			return 0;
		}

		/* Try to extend bottom of window */
		try = res->start - num;
		if ((*base == 0) || (*base == try)) {
			if (adjust_resource(s->io[i].res,
					res->start - num,
					res->end - res->start + num + 1))
				continue;
			*base = try;
			s->io[i].InUse += num;
			*parent = res;
			return 0;
		}
	}

	return -EINVAL;
}


struct pccard_resource_ops pccard_iodyn_ops = {
	.validate_mem = NULL,
	.find_io = iodyn_find_io,
	.find_mem = NULL,
	.init = static_init,
	.exit = NULL,
};
EXPORT_SYMBOL(pccard_iodyn_ops);
