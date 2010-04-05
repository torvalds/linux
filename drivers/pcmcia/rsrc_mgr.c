/*
 * rsrc_mgr.c -- Resource management routines and/or wrappers
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

#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include "cs_internal.h"

static int static_init(struct pcmcia_socket *s)
{
	/* the good thing about SS_CAP_STATIC_MAP sockets is
	 * that they don't need a resource database */

	s->resource_setup_done = 1;

	return 0;
}


struct pccard_resource_ops pccard_static_ops = {
	.validate_mem = NULL,
	.adjust_io_region = NULL,
	.find_io = NULL,
	.find_mem = NULL,
	.add_io = NULL,
	.add_mem = NULL,
	.init = static_init,
	.exit = NULL,
};
EXPORT_SYMBOL(pccard_static_ops);


#ifdef CONFIG_PCCARD_IODYN

static struct resource *
make_resource(unsigned long b, unsigned long n, int flags, char *name)
{
	struct resource *res = kzalloc(sizeof(*res), GFP_KERNEL);

	if (res) {
		res->name = name;
		res->start = b;
		res->end = b + n - 1;
		res->flags = flags;
	}
	return res;
}

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


static int iodyn_adjust_io_region(struct resource *res, unsigned long r_start,
				      unsigned long r_end, struct pcmcia_socket *s)
{
	return adjust_resource(res, r_start, r_end - r_start + 1);
}


static struct resource *iodyn_find_io_region(unsigned long base, int num,
		unsigned long align, struct pcmcia_socket *s)
{
	struct resource *res = make_resource(0, num, IORESOURCE_IO,
					     dev_name(&s->dev));
	struct pcmcia_align_data data;
	unsigned long min = base;
	int ret;

	if (align == 0)
		align = 0x10000;

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

struct pccard_resource_ops pccard_iodyn_ops = {
	.validate_mem = NULL,
	.adjust_io_region = iodyn_adjust_io_region,
	.find_io = iodyn_find_io_region,
	.find_mem = NULL,
	.add_io = NULL,
	.add_mem = NULL,
	.init = static_init,
	.exit = NULL,
};
EXPORT_SYMBOL(pccard_iodyn_ops);

#endif /* CONFIG_PCCARD_IODYN */
