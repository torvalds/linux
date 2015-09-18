/*
 * manager.c - Resource Management, Conflict Resolution, Activation and Disabling of Devices
 *
 * based on isapnp.c resource management (c) Jaroslav Kysela <perex@perex.cz>
 * Copyright 2003 Adam Belay <ambx1@neo.rr.com>
 * Copyright (C) 2008 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pnp.h>
#include <linux/bitmap.h>
#include <linux/mutex.h>
#include "base.h"

DEFINE_MUTEX(pnp_res_mutex);

static struct resource *pnp_find_resource(struct pnp_dev *dev,
					  unsigned char rule,
					  unsigned long type,
					  unsigned int bar)
{
	struct resource *res = pnp_get_resource(dev, type, bar);

	/* when the resource already exists, set its resource bits from rule */
	if (res) {
		res->flags &= ~IORESOURCE_BITS;
		res->flags |= rule & IORESOURCE_BITS;
	}

	return res;
}

static int pnp_assign_port(struct pnp_dev *dev, struct pnp_port *rule, int idx)
{
	struct resource *res, local_res;

	res = pnp_find_resource(dev, rule->flags, IORESOURCE_IO, idx);
	if (res) {
		pnp_dbg(&dev->dev, "  io %d already set to %#llx-%#llx "
			"flags %#lx\n", idx, (unsigned long long) res->start,
			(unsigned long long) res->end, res->flags);
		return 0;
	}

	res = &local_res;
	res->flags = rule->flags | IORESOURCE_AUTO;
	res->start = 0;
	res->end = 0;

	if (!rule->size) {
		res->flags |= IORESOURCE_DISABLED;
		pnp_dbg(&dev->dev, "  io %d disabled\n", idx);
		goto __add;
	}

	res->start = rule->min;
	res->end = res->start + rule->size - 1;

	while (!pnp_check_port(dev, res)) {
		res->start += rule->align;
		res->end = res->start + rule->size - 1;
		if (res->start > rule->max || !rule->align) {
			pnp_dbg(&dev->dev, "  couldn't assign io %d "
				"(min %#llx max %#llx)\n", idx,
				(unsigned long long) rule->min,
				(unsigned long long) rule->max);
			return -EBUSY;
		}
	}

__add:
	pnp_add_io_resource(dev, res->start, res->end, res->flags);
	return 0;
}

static int pnp_assign_mem(struct pnp_dev *dev, struct pnp_mem *rule, int idx)
{
	struct resource *res, local_res;

	res = pnp_find_resource(dev, rule->flags, IORESOURCE_MEM, idx);
	if (res) {
		pnp_dbg(&dev->dev, "  mem %d already set to %#llx-%#llx "
			"flags %#lx\n", idx, (unsigned long long) res->start,
			(unsigned long long) res->end, res->flags);
		return 0;
	}

	res = &local_res;
	res->flags = rule->flags | IORESOURCE_AUTO;
	res->start = 0;
	res->end = 0;

	/* ??? rule->flags restricted to 8 bits, all tests bogus ??? */
	if (!(rule->flags & IORESOURCE_MEM_WRITEABLE))
		res->flags |= IORESOURCE_READONLY;
	if (rule->flags & IORESOURCE_MEM_RANGELENGTH)
		res->flags |= IORESOURCE_RANGELENGTH;
	if (rule->flags & IORESOURCE_MEM_SHADOWABLE)
		res->flags |= IORESOURCE_SHADOWABLE;

	if (!rule->size) {
		res->flags |= IORESOURCE_DISABLED;
		pnp_dbg(&dev->dev, "  mem %d disabled\n", idx);
		goto __add;
	}

	res->start = rule->min;
	res->end = res->start + rule->size - 1;

	while (!pnp_check_mem(dev, res)) {
		res->start += rule->align;
		res->end = res->start + rule->size - 1;
		if (res->start > rule->max || !rule->align) {
			pnp_dbg(&dev->dev, "  couldn't assign mem %d "
				"(min %#llx max %#llx)\n", idx,
				(unsigned long long) rule->min,
				(unsigned long long) rule->max);
			return -EBUSY;
		}
	}

__add:
	pnp_add_mem_resource(dev, res->start, res->end, res->flags);
	return 0;
}

static int pnp_assign_irq(struct pnp_dev *dev, struct pnp_irq *rule, int idx)
{
	struct resource *res, local_res;
	int i;

	/* IRQ priority: this table is good for i386 */
	static unsigned short xtab[16] = {
		5, 10, 11, 12, 9, 14, 15, 7, 3, 4, 13, 0, 1, 6, 8, 2
	};

	res = pnp_find_resource(dev, rule->flags, IORESOURCE_IRQ, idx);
	if (res) {
		pnp_dbg(&dev->dev, "  irq %d already set to %d flags %#lx\n",
			idx, (int) res->start, res->flags);
		return 0;
	}

	res = &local_res;
	res->flags = rule->flags | IORESOURCE_AUTO;
	res->start = -1;
	res->end = -1;

	if (bitmap_empty(rule->map.bits, PNP_IRQ_NR)) {
		res->flags |= IORESOURCE_DISABLED;
		pnp_dbg(&dev->dev, "  irq %d disabled\n", idx);
		goto __add;
	}

	/* TBD: need check for >16 IRQ */
	res->start = find_next_bit(rule->map.bits, PNP_IRQ_NR, 16);
	if (res->start < PNP_IRQ_NR) {
		res->end = res->start;
		goto __add;
	}
	for (i = 0; i < 16; i++) {
		if (test_bit(xtab[i], rule->map.bits)) {
			res->start = res->end = xtab[i];
			if (pnp_check_irq(dev, res))
				goto __add;
		}
	}

	if (rule->flags & IORESOURCE_IRQ_OPTIONAL) {
		res->start = -1;
		res->end = -1;
		res->flags |= IORESOURCE_DISABLED;
		pnp_dbg(&dev->dev, "  irq %d disabled (optional)\n", idx);
		goto __add;
	}

	pnp_dbg(&dev->dev, "  couldn't assign irq %d\n", idx);
	return -EBUSY;

__add:
	pnp_add_irq_resource(dev, res->start, res->flags);
	return 0;
}

#ifdef CONFIG_ISA_DMA_API
static int pnp_assign_dma(struct pnp_dev *dev, struct pnp_dma *rule, int idx)
{
	struct resource *res, local_res;
	int i;

	/* DMA priority: this table is good for i386 */
	static unsigned short xtab[8] = {
		1, 3, 5, 6, 7, 0, 2, 4
	};

	res = pnp_find_resource(dev, rule->flags, IORESOURCE_DMA, idx);
	if (res) {
		pnp_dbg(&dev->dev, "  dma %d already set to %d flags %#lx\n",
			idx, (int) res->start, res->flags);
		return 0;
	}

	res = &local_res;
	res->flags = rule->flags | IORESOURCE_AUTO;
	res->start = -1;
	res->end = -1;

	if (!rule->map) {
		res->flags |= IORESOURCE_DISABLED;
		pnp_dbg(&dev->dev, "  dma %d disabled\n", idx);
		goto __add;
	}

	for (i = 0; i < 8; i++) {
		if (rule->map & (1 << xtab[i])) {
			res->start = res->end = xtab[i];
			if (pnp_check_dma(dev, res))
				goto __add;
		}
	}

	pnp_dbg(&dev->dev, "  couldn't assign dma %d\n", idx);
	return -EBUSY;

__add:
	pnp_add_dma_resource(dev, res->start, res->flags);
	return 0;
}
#endif /* CONFIG_ISA_DMA_API */

void pnp_init_resources(struct pnp_dev *dev)
{
	pnp_free_resources(dev);
}

static void pnp_clean_resource_table(struct pnp_dev *dev)
{
	struct pnp_resource *pnp_res, *tmp;

	list_for_each_entry_safe(pnp_res, tmp, &dev->resources, list) {
		if (pnp_res->res.flags & IORESOURCE_AUTO)
			pnp_free_resource(pnp_res);
	}
}

/**
 * pnp_assign_resources - assigns resources to the device based on the specified dependent number
 * @dev: pointer to the desired device
 * @set: the dependent function number
 */
static int pnp_assign_resources(struct pnp_dev *dev, int set)
{
	struct pnp_option *option;
	int nport = 0, nmem = 0, nirq = 0;
	int ndma __maybe_unused = 0;
	int ret = 0;

	pnp_dbg(&dev->dev, "pnp_assign_resources, try dependent set %d\n", set);
	mutex_lock(&pnp_res_mutex);
	pnp_clean_resource_table(dev);

	list_for_each_entry(option, &dev->options, list) {
		if (pnp_option_is_dependent(option) &&
		    pnp_option_set(option) != set)
				continue;

		switch (option->type) {
		case IORESOURCE_IO:
			ret = pnp_assign_port(dev, &option->u.port, nport++);
			break;
		case IORESOURCE_MEM:
			ret = pnp_assign_mem(dev, &option->u.mem, nmem++);
			break;
		case IORESOURCE_IRQ:
			ret = pnp_assign_irq(dev, &option->u.irq, nirq++);
			break;
#ifdef CONFIG_ISA_DMA_API
		case IORESOURCE_DMA:
			ret = pnp_assign_dma(dev, &option->u.dma, ndma++);
			break;
#endif
		default:
			ret = -EINVAL;
			break;
		}
		if (ret < 0)
			break;
	}

	mutex_unlock(&pnp_res_mutex);
	if (ret < 0) {
		pnp_dbg(&dev->dev, "pnp_assign_resources failed (%d)\n", ret);
		pnp_clean_resource_table(dev);
	} else
		dbg_pnp_show_resources(dev, "pnp_assign_resources succeeded");
	return ret;
}

/**
 * pnp_auto_config_dev - automatically assigns resources to a device
 * @dev: pointer to the desired device
 */
int pnp_auto_config_dev(struct pnp_dev *dev)
{
	int i, ret;

	if (!pnp_can_configure(dev)) {
		pnp_dbg(&dev->dev, "configuration not supported\n");
		return -ENODEV;
	}

	ret = pnp_assign_resources(dev, 0);
	if (ret == 0)
		return 0;

	for (i = 1; i < dev->num_dependent_sets; i++) {
		ret = pnp_assign_resources(dev, i);
		if (ret == 0)
			return 0;
	}

	dev_err(&dev->dev, "unable to assign resources\n");
	return ret;
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
		pnp_dbg(&dev->dev, "activation not supported\n");
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
		pnp_dbg(&dev->dev, "disabling not supported\n");
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
