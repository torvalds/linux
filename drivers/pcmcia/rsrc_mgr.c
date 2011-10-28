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

#include <pcmcia/ss.h>
#include <pcmcia/cistpl.h>
#include "cs_internal.h"

int static_init(struct pcmcia_socket *s)
{
	/* the good thing about SS_CAP_STATIC_MAP sockets is
	 * that they don't need a resource database */

	s->resource_setup_done = 1;

	return 0;
}

struct resource *pcmcia_make_resource(unsigned long start, unsigned long end,
				int flags, const char *name)
{
	struct resource *res = kzalloc(sizeof(*res), GFP_KERNEL);

	if (res) {
		res->name = name;
		res->start = start;
		res->end = start + end - 1;
		res->flags = flags;
	}
	return res;
}

static int static_find_io(struct pcmcia_socket *s, unsigned int attr,
			unsigned int *base, unsigned int num,
			unsigned int align, struct resource **parent)
{
	if (!s->io_offset)
		return -EINVAL;
	*base = s->io_offset | (*base & 0x0fff);
	*parent = NULL;

	return 0;
}


struct pccard_resource_ops pccard_static_ops = {
	.validate_mem = NULL,
	.find_io = static_find_io,
	.find_mem = NULL,
	.init = static_init,
	.exit = NULL,
};
EXPORT_SYMBOL(pccard_static_ops);


MODULE_AUTHOR("David A. Hinds, Dominik Brodowski");
MODULE_LICENSE("GPL");
MODULE_ALIAS("rsrc_nonstatic");
