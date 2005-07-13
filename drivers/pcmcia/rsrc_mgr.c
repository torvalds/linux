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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>
#include "cs_internal.h"


#ifdef CONFIG_PCMCIA_PROBE

static int adjust_irq(struct pcmcia_socket *s, adjust_t *adj)
{
	int irq;
	u32 mask;

	irq = adj->resource.irq.IRQ;
	if ((irq < 0) || (irq > 15))
		return CS_BAD_IRQ;

	if (adj->Action != REMOVE_MANAGED_RESOURCE)
		return 0;

	mask = 1 << irq;

	if (!(s->irq_mask & mask))
		return 0;

	s->irq_mask &= ~mask;

	return 0;
}

#else

static inline int adjust_irq(struct pcmcia_socket *s, adjust_t *adj) {
	return CS_SUCCESS;
}

#endif


int pcmcia_adjust_resource_info(adjust_t *adj)
{
	struct pcmcia_socket *s;
	int ret = CS_UNSUPPORTED_FUNCTION;
	unsigned long flags;

	down_read(&pcmcia_socket_list_rwsem);
	list_for_each_entry(s, &pcmcia_socket_list, socket_list) {

		if (adj->Resource == RES_IRQ)
			ret = adjust_irq(s, adj);

		else if (s->resource_ops->adjust_resource) {

			/* you can't use the old interface if the new
			 * one was used before */
			spin_lock_irqsave(&s->lock, flags);
			if ((s->resource_setup_new) &&
			    !(s->resource_setup_old)) {
				spin_unlock_irqrestore(&s->lock, flags);
				continue;
			} else if (!(s->resource_setup_old))
				s->resource_setup_old = 1;
			spin_unlock_irqrestore(&s->lock, flags);

			ret = s->resource_ops->adjust_resource(s, adj);
			if (!ret) {
				/* as there's no way we know this is the
				 * last call to adjust_resource_info, we
				 * always need to assume this is the latest
				 * one... */
				spin_lock_irqsave(&s->lock, flags);
				s->resource_setup_done = 1;
				spin_unlock_irqrestore(&s->lock, flags);
			}
		}
	}
	up_read(&pcmcia_socket_list_rwsem);

	return (ret);
}
EXPORT_SYMBOL(pcmcia_adjust_resource_info);

void pcmcia_validate_mem(struct pcmcia_socket *s)
{
	if (s->resource_ops->validate_mem)
		s->resource_ops->validate_mem(s);
}
EXPORT_SYMBOL(pcmcia_validate_mem);

int pcmcia_adjust_io_region(struct resource *res, unsigned long r_start,
		     unsigned long r_end, struct pcmcia_socket *s)
{
	if (s->resource_ops->adjust_io_region)
		return s->resource_ops->adjust_io_region(res, r_start, r_end, s);
	return -ENOMEM;
}
EXPORT_SYMBOL(pcmcia_adjust_io_region);

struct resource *pcmcia_find_io_region(unsigned long base, int num,
		   unsigned long align, struct pcmcia_socket *s)
{
	if (s->resource_ops->find_io)
		return s->resource_ops->find_io(base, num, align, s);
	return NULL;
}
EXPORT_SYMBOL(pcmcia_find_io_region);

struct resource *pcmcia_find_mem_region(u_long base, u_long num, u_long align,
				 int low, struct pcmcia_socket *s)
{
	if (s->resource_ops->find_mem)
		return s->resource_ops->find_mem(base, num, align, low, s);
	return NULL;
}
EXPORT_SYMBOL(pcmcia_find_mem_region);

void release_resource_db(struct pcmcia_socket *s)
{
	if (s->resource_ops->exit)
		s->resource_ops->exit(s);
}


static int static_init(struct pcmcia_socket *s)
{
	unsigned long flags;

	/* the good thing about SS_CAP_STATIC_MAP sockets is
	 * that they don't need a resource database */

	spin_lock_irqsave(&s->lock, flags);
	s->resource_setup_done = 1;
	spin_unlock_irqrestore(&s->lock, flags);

	return 0;
}


struct pccard_resource_ops pccard_static_ops = {
	.validate_mem = NULL,
	.adjust_io_region = NULL,
	.find_io = NULL,
	.find_mem = NULL,
	.adjust_resource = NULL,
	.init = static_init,
	.exit = NULL,
};
EXPORT_SYMBOL(pccard_static_ops);
