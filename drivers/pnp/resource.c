/*
 * resource.c - Contains functions for registering and analyzing resource information
 *
 * based on isapnp.c resource management (c) Jaroslav Kysela <perex@suse.cz>
 * Copyright 2003 Adam Belay <ambx1@neo.rr.com>
 *
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <linux/pnp.h>
#include "base.h"

static int pnp_reserve_irq[16] = {[0...15] = -1 };	/* reserve (don't use) some IRQ */
static int pnp_reserve_dma[8] = {[0...7] = -1 };	/* reserve (don't use) some DMA */
static int pnp_reserve_io[16] = {[0...15] = -1 };	/* reserve (don't use) some I/O region */
static int pnp_reserve_mem[16] = {[0...15] = -1 };	/* reserve (don't use) some memory region */

/*
 * option registration
 */

static struct pnp_option *pnp_build_option(int priority)
{
	struct pnp_option *option = pnp_alloc(sizeof(struct pnp_option));

	/* check if pnp_alloc ran out of memory */
	if (!option)
		return NULL;

	option->priority = priority & 0xff;
	/* make sure the priority is valid */
	if (option->priority > PNP_RES_PRIORITY_FUNCTIONAL)
		option->priority = PNP_RES_PRIORITY_INVALID;

	return option;
}

struct pnp_option *pnp_register_independent_option(struct pnp_dev *dev)
{
	struct pnp_option *option;
	if (!dev)
		return NULL;

	option = pnp_build_option(PNP_RES_PRIORITY_PREFERRED);

	/* this should never happen but if it does we'll try to continue */
	if (dev->independent)
		pnp_err("independent resource already registered");
	dev->independent = option;
	return option;
}

struct pnp_option *pnp_register_dependent_option(struct pnp_dev *dev,
						 int priority)
{
	struct pnp_option *option;
	if (!dev)
		return NULL;

	option = pnp_build_option(priority);

	if (dev->dependent) {
		struct pnp_option *parent = dev->dependent;
		while (parent->next)
			parent = parent->next;
		parent->next = option;
	} else
		dev->dependent = option;
	return option;
}

int pnp_register_irq_resource(struct pnp_option *option, struct pnp_irq *data)
{
	struct pnp_irq *ptr;
	if (!option)
		return -EINVAL;
	if (!data)
		return -EINVAL;

	ptr = option->irq;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = data;
	else
		option->irq = data;

#ifdef CONFIG_PCI
	{
		int i;

		for (i = 0; i < 16; i++)
			if (test_bit(i, data->map))
				pcibios_penalize_isa_irq(i, 0);
	}
#endif
	return 0;
}

int pnp_register_dma_resource(struct pnp_option *option, struct pnp_dma *data)
{
	struct pnp_dma *ptr;
	if (!option)
		return -EINVAL;
	if (!data)
		return -EINVAL;

	ptr = option->dma;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = data;
	else
		option->dma = data;

	return 0;
}

int pnp_register_port_resource(struct pnp_option *option, struct pnp_port *data)
{
	struct pnp_port *ptr;
	if (!option)
		return -EINVAL;
	if (!data)
		return -EINVAL;

	ptr = option->port;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = data;
	else
		option->port = data;

	return 0;
}

int pnp_register_mem_resource(struct pnp_option *option, struct pnp_mem *data)
{
	struct pnp_mem *ptr;
	if (!option)
		return -EINVAL;
	if (!data)
		return -EINVAL;

	ptr = option->mem;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = data;
	else
		option->mem = data;
	return 0;
}

static void pnp_free_port(struct pnp_port *port)
{
	struct pnp_port *next;

	while (port) {
		next = port->next;
		kfree(port);
		port = next;
	}
}

static void pnp_free_irq(struct pnp_irq *irq)
{
	struct pnp_irq *next;

	while (irq) {
		next = irq->next;
		kfree(irq);
		irq = next;
	}
}

static void pnp_free_dma(struct pnp_dma *dma)
{
	struct pnp_dma *next;

	while (dma) {
		next = dma->next;
		kfree(dma);
		dma = next;
	}
}

static void pnp_free_mem(struct pnp_mem *mem)
{
	struct pnp_mem *next;

	while (mem) {
		next = mem->next;
		kfree(mem);
		mem = next;
	}
}

void pnp_free_option(struct pnp_option *option)
{
	struct pnp_option *next;

	while (option) {
		next = option->next;
		pnp_free_port(option->port);
		pnp_free_irq(option->irq);
		pnp_free_dma(option->dma);
		pnp_free_mem(option->mem);
		kfree(option);
		option = next;
	}
}

/*
 * resource validity checking
 */

#define length(start, end) (*(end) - *(start) + 1)

/* Two ranges conflict if one doesn't end before the other starts */
#define ranged_conflict(starta, enda, startb, endb) \
	!((*(enda) < *(startb)) || (*(endb) < *(starta)))

#define cannot_compare(flags) \
((flags) & (IORESOURCE_UNSET | IORESOURCE_DISABLED))

int pnp_check_port(struct pnp_dev *dev, int idx)
{
	int tmp;
	struct pnp_dev *tdev;
	resource_size_t *port, *end, *tport, *tend;
	port = &dev->res.port_resource[idx].start;
	end = &dev->res.port_resource[idx].end;

	/* if the resource doesn't exist, don't complain about it */
	if (cannot_compare(dev->res.port_resource[idx].flags))
		return 1;

	/* check if the resource is already in use, skip if the
	 * device is active because it itself may be in use */
	if (!dev->active) {
		if (__check_region(&ioport_resource, *port, length(port, end)))
			return 0;
	}

	/* check if the resource is reserved */
	for (tmp = 0; tmp < 8; tmp++) {
		int rport = pnp_reserve_io[tmp << 1];
		int rend = pnp_reserve_io[(tmp << 1) + 1] + rport - 1;
		if (ranged_conflict(port, end, &rport, &rend))
			return 0;
	}

	/* check for internal conflicts */
	for (tmp = 0; tmp < PNP_MAX_PORT && tmp != idx; tmp++) {
		if (dev->res.port_resource[tmp].flags & IORESOURCE_IO) {
			tport = &dev->res.port_resource[tmp].start;
			tend = &dev->res.port_resource[tmp].end;
			if (ranged_conflict(port, end, tport, tend))
				return 0;
		}
	}

	/* check for conflicts with other pnp devices */
	pnp_for_each_dev(tdev) {
		if (tdev == dev)
			continue;
		for (tmp = 0; tmp < PNP_MAX_PORT; tmp++) {
			if (tdev->res.port_resource[tmp].flags & IORESOURCE_IO) {
				if (cannot_compare
				    (tdev->res.port_resource[tmp].flags))
					continue;
				tport = &tdev->res.port_resource[tmp].start;
				tend = &tdev->res.port_resource[tmp].end;
				if (ranged_conflict(port, end, tport, tend))
					return 0;
			}
		}
	}

	return 1;
}

int pnp_check_mem(struct pnp_dev *dev, int idx)
{
	int tmp;
	struct pnp_dev *tdev;
	resource_size_t *addr, *end, *taddr, *tend;
	addr = &dev->res.mem_resource[idx].start;
	end = &dev->res.mem_resource[idx].end;

	/* if the resource doesn't exist, don't complain about it */
	if (cannot_compare(dev->res.mem_resource[idx].flags))
		return 1;

	/* check if the resource is already in use, skip if the
	 * device is active because it itself may be in use */
	if (!dev->active) {
		if (check_mem_region(*addr, length(addr, end)))
			return 0;
	}

	/* check if the resource is reserved */
	for (tmp = 0; tmp < 8; tmp++) {
		int raddr = pnp_reserve_mem[tmp << 1];
		int rend = pnp_reserve_mem[(tmp << 1) + 1] + raddr - 1;
		if (ranged_conflict(addr, end, &raddr, &rend))
			return 0;
	}

	/* check for internal conflicts */
	for (tmp = 0; tmp < PNP_MAX_MEM && tmp != idx; tmp++) {
		if (dev->res.mem_resource[tmp].flags & IORESOURCE_MEM) {
			taddr = &dev->res.mem_resource[tmp].start;
			tend = &dev->res.mem_resource[tmp].end;
			if (ranged_conflict(addr, end, taddr, tend))
				return 0;
		}
	}

	/* check for conflicts with other pnp devices */
	pnp_for_each_dev(tdev) {
		if (tdev == dev)
			continue;
		for (tmp = 0; tmp < PNP_MAX_MEM; tmp++) {
			if (tdev->res.mem_resource[tmp].flags & IORESOURCE_MEM) {
				if (cannot_compare
				    (tdev->res.mem_resource[tmp].flags))
					continue;
				taddr = &tdev->res.mem_resource[tmp].start;
				tend = &tdev->res.mem_resource[tmp].end;
				if (ranged_conflict(addr, end, taddr, tend))
					return 0;
			}
		}
	}

	return 1;
}

static irqreturn_t pnp_test_handler(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

int pnp_check_irq(struct pnp_dev *dev, int idx)
{
	int tmp;
	struct pnp_dev *tdev;
	resource_size_t *irq = &dev->res.irq_resource[idx].start;

	/* if the resource doesn't exist, don't complain about it */
	if (cannot_compare(dev->res.irq_resource[idx].flags))
		return 1;

	/* check if the resource is valid */
	if (*irq < 0 || *irq > 15)
		return 0;

	/* check if the resource is reserved */
	for (tmp = 0; tmp < 16; tmp++) {
		if (pnp_reserve_irq[tmp] == *irq)
			return 0;
	}

	/* check for internal conflicts */
	for (tmp = 0; tmp < PNP_MAX_IRQ && tmp != idx; tmp++) {
		if (dev->res.irq_resource[tmp].flags & IORESOURCE_IRQ) {
			if (dev->res.irq_resource[tmp].start == *irq)
				return 0;
		}
	}

#ifdef CONFIG_PCI
	/* check if the resource is being used by a pci device */
	{
		struct pci_dev *pci = NULL;
		for_each_pci_dev(pci) {
			if (pci->irq == *irq)
				return 0;
		}
	}
#endif

	/* check if the resource is already in use, skip if the
	 * device is active because it itself may be in use */
	if (!dev->active) {
		if (request_irq(*irq, pnp_test_handler,
				IRQF_DISABLED | IRQF_PROBE_SHARED, "pnp", NULL))
			return 0;
		free_irq(*irq, NULL);
	}

	/* check for conflicts with other pnp devices */
	pnp_for_each_dev(tdev) {
		if (tdev == dev)
			continue;
		for (tmp = 0; tmp < PNP_MAX_IRQ; tmp++) {
			if (tdev->res.irq_resource[tmp].flags & IORESOURCE_IRQ) {
				if (cannot_compare
				    (tdev->res.irq_resource[tmp].flags))
					continue;
				if ((tdev->res.irq_resource[tmp].start == *irq))
					return 0;
			}
		}
	}

	return 1;
}

int pnp_check_dma(struct pnp_dev *dev, int idx)
{
#ifndef CONFIG_IA64
	int tmp;
	struct pnp_dev *tdev;
	resource_size_t *dma = &dev->res.dma_resource[idx].start;

	/* if the resource doesn't exist, don't complain about it */
	if (cannot_compare(dev->res.dma_resource[idx].flags))
		return 1;

	/* check if the resource is valid */
	if (*dma < 0 || *dma == 4 || *dma > 7)
		return 0;

	/* check if the resource is reserved */
	for (tmp = 0; tmp < 8; tmp++) {
		if (pnp_reserve_dma[tmp] == *dma)
			return 0;
	}

	/* check for internal conflicts */
	for (tmp = 0; tmp < PNP_MAX_DMA && tmp != idx; tmp++) {
		if (dev->res.dma_resource[tmp].flags & IORESOURCE_DMA) {
			if (dev->res.dma_resource[tmp].start == *dma)
				return 0;
		}
	}

	/* check if the resource is already in use, skip if the
	 * device is active because it itself may be in use */
	if (!dev->active) {
		if (request_dma(*dma, "pnp"))
			return 0;
		free_dma(*dma);
	}

	/* check for conflicts with other pnp devices */
	pnp_for_each_dev(tdev) {
		if (tdev == dev)
			continue;
		for (tmp = 0; tmp < PNP_MAX_DMA; tmp++) {
			if (tdev->res.dma_resource[tmp].flags & IORESOURCE_DMA) {
				if (cannot_compare
				    (tdev->res.dma_resource[tmp].flags))
					continue;
				if ((tdev->res.dma_resource[tmp].start == *dma))
					return 0;
			}
		}
	}

	return 1;
#else
	/* IA64 hasn't legacy DMA */
	return 0;
#endif
}

#if 0
EXPORT_SYMBOL(pnp_register_dependent_option);
EXPORT_SYMBOL(pnp_register_independent_option);
EXPORT_SYMBOL(pnp_register_irq_resource);
EXPORT_SYMBOL(pnp_register_dma_resource);
EXPORT_SYMBOL(pnp_register_port_resource);
EXPORT_SYMBOL(pnp_register_mem_resource);
#endif /*  0  */

/* format is: pnp_reserve_irq=irq1[,irq2] .... */

static int __init pnp_setup_reserve_irq(char *str)
{
	int i;

	for (i = 0; i < 16; i++)
		if (get_option(&str, &pnp_reserve_irq[i]) != 2)
			break;
	return 1;
}

__setup("pnp_reserve_irq=", pnp_setup_reserve_irq);

/* format is: pnp_reserve_dma=dma1[,dma2] .... */

static int __init pnp_setup_reserve_dma(char *str)
{
	int i;

	for (i = 0; i < 8; i++)
		if (get_option(&str, &pnp_reserve_dma[i]) != 2)
			break;
	return 1;
}

__setup("pnp_reserve_dma=", pnp_setup_reserve_dma);

/* format is: pnp_reserve_io=io1,size1[,io2,size2] .... */

static int __init pnp_setup_reserve_io(char *str)
{
	int i;

	for (i = 0; i < 16; i++)
		if (get_option(&str, &pnp_reserve_io[i]) != 2)
			break;
	return 1;
}

__setup("pnp_reserve_io=", pnp_setup_reserve_io);

/* format is: pnp_reserve_mem=mem1,size1[,mem2,size2] .... */

static int __init pnp_setup_reserve_mem(char *str)
{
	int i;

	for (i = 0; i < 16; i++)
		if (get_option(&str, &pnp_reserve_mem[i]) != 2)
			break;
	return 1;
}

__setup("pnp_reserve_mem=", pnp_setup_reserve_mem);
