/*
 * manager.c - Resource Management, Conflict Resolution, Activation and Disabling of Devices
 *
 * based on isapnp.c resource management (c) Jaroslav Kysela <perex@perex.cz>
 * Copyright 2003 Adam Belay <ambx1@neo.rr.com>
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pnp.h>
#include <linux/slab.h>
#include <linux/bitmap.h>
#include <linux/mutex.h>
#include "base.h"

DEFINE_MUTEX(pnp_res_mutex);

static int pnp_assign_port(struct pnp_dev *dev, struct pnp_port *rule, int idx)
{
	struct pnp_resource *pnp_res;
	struct resource *res;

	pnp_res = pnp_get_pnp_resource(dev, IORESOURCE_IO, idx);
	if (!pnp_res) {
		dev_err(&dev->dev, "too many I/O port resources\n");
		/* pretend we were successful so at least the manager won't try again */
		return 1;
	}

	res = &pnp_res->res;

	/* check if this resource has been manually set, if so skip */
	if (!(res->flags & IORESOURCE_AUTO)) {
		dev_dbg(&dev->dev, "  io %d already set to %#llx-%#llx "
			"flags %#lx\n", idx, (unsigned long long) res->start,
			(unsigned long long) res->end, res->flags);
		return 1;
	}

	/* set the initial values */
	pnp_res->index = idx;
	res->flags |= rule->flags | IORESOURCE_IO;
	res->flags &= ~IORESOURCE_UNSET;

	if (!rule->size) {
		res->flags |= IORESOURCE_DISABLED;
		dev_dbg(&dev->dev, "  io %d disabled\n", idx);
		return 1;	/* skip disabled resource requests */
	}

	res->start = rule->min;
	res->end = res->start + rule->size - 1;

	/* run through until pnp_check_port is happy */
	while (!pnp_check_port(dev, res)) {
		res->start += rule->align;
		res->end = res->start + rule->size - 1;
		if (res->start > rule->max || !rule->align) {
			dev_dbg(&dev->dev, "  couldn't assign io %d\n", idx);
			return 0;
		}
	}
	dev_dbg(&dev->dev, "  assign io  %d %#llx-%#llx\n", idx,
		(unsigned long long) res->start, (unsigned long long) res->end);
	return 1;
}

static int pnp_assign_mem(struct pnp_dev *dev, struct pnp_mem *rule, int idx)
{
	struct pnp_resource *pnp_res;
	struct resource *res;

	pnp_res = pnp_get_pnp_resource(dev, IORESOURCE_MEM, idx);
	if (!pnp_res) {
		dev_err(&dev->dev, "too many memory resources\n");
		/* pretend we were successful so at least the manager won't try again */
		return 1;
	}

	res = &pnp_res->res;

	/* check if this resource has been manually set, if so skip */
	if (!(res->flags & IORESOURCE_AUTO)) {
		dev_dbg(&dev->dev, "  mem %d already set to %#llx-%#llx "
			"flags %#lx\n", idx, (unsigned long long) res->start,
			(unsigned long long) res->end, res->flags);
		return 1;
	}

	/* set the initial values */
	pnp_res->index = idx;
	res->flags |= rule->flags | IORESOURCE_MEM;
	res->flags &= ~IORESOURCE_UNSET;

	/* convert pnp flags to standard Linux flags */
	if (!(rule->flags & IORESOURCE_MEM_WRITEABLE))
		res->flags |= IORESOURCE_READONLY;
	if (rule->flags & IORESOURCE_MEM_CACHEABLE)
		res->flags |= IORESOURCE_CACHEABLE;
	if (rule->flags & IORESOURCE_MEM_RANGELENGTH)
		res->flags |= IORESOURCE_RANGELENGTH;
	if (rule->flags & IORESOURCE_MEM_SHADOWABLE)
		res->flags |= IORESOURCE_SHADOWABLE;

	if (!rule->size) {
		res->flags |= IORESOURCE_DISABLED;
		dev_dbg(&dev->dev, "  mem %d disabled\n", idx);
		return 1;	/* skip disabled resource requests */
	}

	res->start = rule->min;
	res->end = res->start + rule->size - 1;

	/* run through until pnp_check_mem is happy */
	while (!pnp_check_mem(dev, res)) {
		res->start += rule->align;
		res->end = res->start + rule->size - 1;
		if (res->start > rule->max || !rule->align) {
			dev_dbg(&dev->dev, "  couldn't assign mem %d\n", idx);
			return 0;
		}
	}
	dev_dbg(&dev->dev, "  assign mem %d %#llx-%#llx\n", idx,
		(unsigned long long) res->start, (unsigned long long) res->end);
	return 1;
}

static int pnp_assign_irq(struct pnp_dev *dev, struct pnp_irq *rule, int idx)
{
	struct pnp_resource *pnp_res;
	struct resource *res;
	int i;

	/* IRQ priority: this table is good for i386 */
	static unsigned short xtab[16] = {
		5, 10, 11, 12, 9, 14, 15, 7, 3, 4, 13, 0, 1, 6, 8, 2
	};

	pnp_res = pnp_get_pnp_resource(dev, IORESOURCE_IRQ, idx);
	if (!pnp_res) {
		dev_err(&dev->dev, "too many IRQ resources\n");
		/* pretend we were successful so at least the manager won't try again */
		return 1;
	}

	res = &pnp_res->res;

	/* check if this resource has been manually set, if so skip */
	if (!(res->flags & IORESOURCE_AUTO)) {
		dev_dbg(&dev->dev, "  irq %d already set to %d flags %#lx\n",
			idx, (int) res->start, res->flags);
		return 1;
	}

	/* set the initial values */
	pnp_res->index = idx;
	res->flags |= rule->flags | IORESOURCE_IRQ;
	res->flags &= ~IORESOURCE_UNSET;

	if (bitmap_empty(rule->map, PNP_IRQ_NR)) {
		res->flags |= IORESOURCE_DISABLED;
		dev_dbg(&dev->dev, "  irq %d disabled\n", idx);
		return 1;	/* skip disabled resource requests */
	}

	/* TBD: need check for >16 IRQ */
	res->start = find_next_bit(rule->map, PNP_IRQ_NR, 16);
	if (res->start < PNP_IRQ_NR) {
		res->end = res->start;
		dev_dbg(&dev->dev, "  assign irq %d %d\n", idx,
			(int) res->start);
		return 1;
	}
	for (i = 0; i < 16; i++) {
		if (test_bit(xtab[i], rule->map)) {
			res->start = res->end = xtab[i];
			if (pnp_check_irq(dev, res)) {
				dev_dbg(&dev->dev, "  assign irq %d %d\n", idx,
					(int) res->start);
				return 1;
			}
		}
	}
	dev_dbg(&dev->dev, "  couldn't assign irq %d\n", idx);
	return 0;
}

static void pnp_assign_dma(struct pnp_dev *dev, struct pnp_dma *rule, int idx)
{
	struct pnp_resource *pnp_res;
	struct resource *res;
	int i;

	/* DMA priority: this table is good for i386 */
	static unsigned short xtab[8] = {
		1, 3, 5, 6, 7, 0, 2, 4
	};

	pnp_res = pnp_get_pnp_resource(dev, IORESOURCE_DMA, idx);
	if (!pnp_res) {
		dev_err(&dev->dev, "too many DMA resources\n");
		return;
	}

	res = &pnp_res->res;

	/* check if this resource has been manually set, if so skip */
	if (!(res->flags & IORESOURCE_AUTO)) {
		dev_dbg(&dev->dev, "  dma %d already set to %d flags %#lx\n",
			idx, (int) res->start, res->flags);
		return;
	}

	/* set the initial values */
	pnp_res->index = idx;
	res->flags |= rule->flags | IORESOURCE_DMA;
	res->flags &= ~IORESOURCE_UNSET;

	for (i = 0; i < 8; i++) {
		if (rule->map & (1 << xtab[i])) {
			res->start = res->end = xtab[i];
			if (pnp_check_dma(dev, res)) {
				dev_dbg(&dev->dev, "  assign dma %d %d\n", idx,
					(int) res->start);
				return;
			}
		}
	}
#ifdef MAX_DMA_CHANNELS
	res->start = res->end = MAX_DMA_CHANNELS;
#endif
	res->flags |= IORESOURCE_UNSET | IORESOURCE_DISABLED;
	dev_dbg(&dev->dev, "  disable dma %d\n", idx);
}

void pnp_init_resource(struct resource *res)
{
	unsigned long type;

	type = res->flags & (IORESOURCE_IO  | IORESOURCE_MEM |
			     IORESOURCE_IRQ | IORESOURCE_DMA);

	res->name = NULL;
	res->flags = type | IORESOURCE_AUTO | IORESOURCE_UNSET;
	if (type == IORESOURCE_IRQ || type == IORESOURCE_DMA) {
		res->start = -1;
		res->end = -1;
	} else {
		res->start = 0;
		res->end = 0;
	}
}

/**
 * pnp_init_resources - Resets a resource table to default values.
 * @table: pointer to the desired resource table
 */
void pnp_init_resources(struct pnp_dev *dev)
{
	struct resource *res;
	int idx;

	for (idx = 0; idx < PNP_MAX_IRQ; idx++) {
		res = &dev->res->irq[idx].res;
		res->flags = IORESOURCE_IRQ;
		pnp_init_resource(res);
	}
	for (idx = 0; idx < PNP_MAX_DMA; idx++) {
		res = &dev->res->dma[idx].res;
		res->flags = IORESOURCE_DMA;
		pnp_init_resource(res);
	}
	for (idx = 0; idx < PNP_MAX_PORT; idx++) {
		res = &dev->res->port[idx].res;
		res->flags = IORESOURCE_IO;
		pnp_init_resource(res);
	}
	for (idx = 0; idx < PNP_MAX_MEM; idx++) {
		res = &dev->res->mem[idx].res;
		res->flags = IORESOURCE_MEM;
		pnp_init_resource(res);
	}
}

/**
 * pnp_clean_resources - clears resources that were not manually set
 * @res: the resources to clean
 */
static void pnp_clean_resource_table(struct pnp_dev *dev)
{
	struct resource *res;
	int idx;

	for (idx = 0; idx < PNP_MAX_IRQ; idx++) {
		res = &dev->res->irq[idx].res;
		if (res->flags & IORESOURCE_AUTO) {
			res->flags = IORESOURCE_IRQ;
			pnp_init_resource(res);
		}
	}
	for (idx = 0; idx < PNP_MAX_DMA; idx++) {
		res = &dev->res->dma[idx].res;
		if (res->flags & IORESOURCE_AUTO) {
			res->flags = IORESOURCE_DMA;
			pnp_init_resource(res);
		}
	}
	for (idx = 0; idx < PNP_MAX_PORT; idx++) {
		res = &dev->res->port[idx].res;
		if (res->flags & IORESOURCE_AUTO) {
			res->flags = IORESOURCE_IO;
			pnp_init_resource(res);
		}
	}
	for (idx = 0; idx < PNP_MAX_MEM; idx++) {
		res = &dev->res->mem[idx].res;
		if (res->flags & IORESOURCE_AUTO) {
			res->flags = IORESOURCE_MEM;
			pnp_init_resource(res);
		}
	}
}

/**
 * pnp_assign_resources - assigns resources to the device based on the specified dependent number
 * @dev: pointer to the desired device
 * @depnum: the dependent function number
 *
 * Only set depnum to 0 if the device does not have dependent options.
 */
static int pnp_assign_resources(struct pnp_dev *dev, int depnum)
{
	struct pnp_port *port;
	struct pnp_mem *mem;
	struct pnp_irq *irq;
	struct pnp_dma *dma;
	int nport = 0, nmem = 0, nirq = 0, ndma = 0;

	if (!pnp_can_configure(dev))
		return -ENODEV;

	dbg_pnp_show_resources(dev, "before pnp_assign_resources");
	mutex_lock(&pnp_res_mutex);
	pnp_clean_resource_table(dev);
	if (dev->independent) {
		dev_dbg(&dev->dev, "assigning independent options\n");
		port = dev->independent->port;
		mem = dev->independent->mem;
		irq = dev->independent->irq;
		dma = dev->independent->dma;
		while (port) {
			if (!pnp_assign_port(dev, port, nport))
				goto fail;
			nport++;
			port = port->next;
		}
		while (mem) {
			if (!pnp_assign_mem(dev, mem, nmem))
				goto fail;
			nmem++;
			mem = mem->next;
		}
		while (irq) {
			if (!pnp_assign_irq(dev, irq, nirq))
				goto fail;
			nirq++;
			irq = irq->next;
		}
		while (dma) {
			pnp_assign_dma(dev, dma, ndma);
			ndma++;
			dma = dma->next;
		}
	}

	if (depnum) {
		struct pnp_option *dep;
		int i;

		dev_dbg(&dev->dev, "assigning dependent option %d\n", depnum);
		for (i = 1, dep = dev->dependent; i < depnum;
		     i++, dep = dep->next)
			if (!dep)
				goto fail;
		port = dep->port;
		mem = dep->mem;
		irq = dep->irq;
		dma = dep->dma;
		while (port) {
			if (!pnp_assign_port(dev, port, nport))
				goto fail;
			nport++;
			port = port->next;
		}
		while (mem) {
			if (!pnp_assign_mem(dev, mem, nmem))
				goto fail;
			nmem++;
			mem = mem->next;
		}
		while (irq) {
			if (!pnp_assign_irq(dev, irq, nirq))
				goto fail;
			nirq++;
			irq = irq->next;
		}
		while (dma) {
			pnp_assign_dma(dev, dma, ndma);
			ndma++;
			dma = dma->next;
		}
	} else if (dev->dependent)
		goto fail;

	mutex_unlock(&pnp_res_mutex);
	dbg_pnp_show_resources(dev, "after pnp_assign_resources");
	return 1;

fail:
	pnp_clean_resource_table(dev);
	mutex_unlock(&pnp_res_mutex);
	dbg_pnp_show_resources(dev, "after pnp_assign_resources (failed)");
	return 0;
}

/**
 * pnp_auto_config_dev - automatically assigns resources to a device
 * @dev: pointer to the desired device
 */
int pnp_auto_config_dev(struct pnp_dev *dev)
{
	struct pnp_option *dep;
	int i = 1;

	if (!pnp_can_configure(dev)) {
		dev_dbg(&dev->dev, "configuration not supported\n");
		return -ENODEV;
	}

	if (!dev->dependent) {
		if (pnp_assign_resources(dev, 0))
			return 0;
	} else {
		dep = dev->dependent;
		do {
			if (pnp_assign_resources(dev, i))
				return 0;
			dep = dep->next;
			i++;
		} while (dep);
	}

	dev_err(&dev->dev, "unable to assign resources\n");
	return -EBUSY;
}

/**
 * pnp_start_dev - low-level start of the PnP device
 * @dev: pointer to the desired device
 *
 * assumes that resources have already been allocated
 */
int pnp_start_dev(struct pnp_dev *dev)
{
	if (!pnp_can_write(dev)) {
		dev_dbg(&dev->dev, "activation not supported\n");
		return -EINVAL;
	}

	dbg_pnp_show_resources(dev, "pnp_start_dev");
	if (dev->protocol->set(dev) < 0) {
		dev_err(&dev->dev, "activation failed\n");
		return -EIO;
	}

	dev_info(&dev->dev, "activated\n");
	return 0;
}

/**
 * pnp_stop_dev - low-level disable of the PnP device
 * @dev: pointer to the desired device
 *
 * does not free resources
 */
int pnp_stop_dev(struct pnp_dev *dev)
{
	if (!pnp_can_disable(dev)) {
		dev_dbg(&dev->dev, "disabling not supported\n");
		return -EINVAL;
	}
	if (dev->protocol->disable(dev) < 0) {
		dev_err(&dev->dev, "disable failed\n");
		return -EIO;
	}

	dev_info(&dev->dev, "disabled\n");
	return 0;
}

/**
 * pnp_activate_dev - activates a PnP device for use
 * @dev: pointer to the desired device
 *
 * does not validate or set resources so be careful.
 */
int pnp_activate_dev(struct pnp_dev *dev)
{
	int error;

	if (dev->active)
		return 0;

	/* ensure resources are allocated */
	if (pnp_auto_config_dev(dev))
		return -EBUSY;

	error = pnp_start_dev(dev);
	if (error)
		return error;

	dev->active = 1;
	return 0;
}

/**
 * pnp_disable_dev - disables device
 * @dev: pointer to the desired device
 *
 * inform the correct pnp protocol so that resources can be used by other devices
 */
int pnp_disable_dev(struct pnp_dev *dev)
{
	int error;

	if (!dev->active)
		return 0;

	error = pnp_stop_dev(dev);
	if (error)
		return error;

	dev->active = 0;

	/* release the resources so that other devices can use them */
	mutex_lock(&pnp_res_mutex);
	pnp_clean_resource_table(dev);
	mutex_unlock(&pnp_res_mutex);

	return 0;
}

EXPORT_SYMBOL(pnp_start_dev);
EXPORT_SYMBOL(pnp_stop_dev);
EXPORT_SYMBOL(pnp_activate_dev);
EXPORT_SYMBOL(pnp_disable_dev);
