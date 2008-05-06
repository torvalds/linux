/*
 * resource.c - Contains functions for registering and analyzing resource information
 *
 * based on isapnp.c resource management (c) Jaroslav Kysela <perex@perex.cz>
 * Copyright 2003 Adam Belay <ambx1@neo.rr.com>
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

static int pnp_reserve_irq[16] = {[0 ... 15] = -1 };	/* reserve (don't use) some IRQ */
static int pnp_reserve_dma[8] = {[0 ... 7] = -1 };	/* reserve (don't use) some DMA */
static int pnp_reserve_io[16] = {[0 ... 15] = -1 };	/* reserve (don't use) some I/O region */
static int pnp_reserve_mem[16] = {[0 ... 15] = -1 };	/* reserve (don't use) some memory region */

/*
 * option registration
 */

static struct pnp_option *pnp_build_option(int priority)
{
	struct pnp_option *option = pnp_alloc(sizeof(struct pnp_option));

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

	option = pnp_build_option(PNP_RES_PRIORITY_PREFERRED);

	/* this should never happen but if it does we'll try to continue */
	if (dev->independent)
		dev_err(&dev->dev, "independent resource already registered\n");
	dev->independent = option;

	dev_dbg(&dev->dev, "new independent option\n");
	return option;
}

struct pnp_option *pnp_register_dependent_option(struct pnp_dev *dev,
						 int priority)
{
	struct pnp_option *option;

	option = pnp_build_option(priority);

	if (dev->dependent) {
		struct pnp_option *parent = dev->dependent;
		while (parent->next)
			parent = parent->next;
		parent->next = option;
	} else
		dev->dependent = option;

	dev_dbg(&dev->dev, "new dependent option (priority %#x)\n", priority);
	return option;
}

int pnp_register_irq_resource(struct pnp_dev *dev, struct pnp_option *option,
			      struct pnp_irq *data)
{
	struct pnp_irq *ptr;
#ifdef DEBUG
	char buf[PNP_IRQ_NR];   /* hex-encoded, so this is overkill but safe */
#endif

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

#ifdef DEBUG
	bitmap_scnprintf(buf, sizeof(buf), data->map, PNP_IRQ_NR);
	dev_dbg(&dev->dev, "  irq bitmask %s flags %#x\n", buf,
		data->flags);
#endif
	return 0;
}

int pnp_register_dma_resource(struct pnp_dev *dev, struct pnp_option *option,
			      struct pnp_dma *data)
{
	struct pnp_dma *ptr;

	ptr = option->dma;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = data;
	else
		option->dma = data;

	dev_dbg(&dev->dev, "  dma bitmask %#x flags %#x\n", data->map,
		data->flags);
	return 0;
}

int pnp_register_port_resource(struct pnp_dev *dev, struct pnp_option *option,
			       struct pnp_port *data)
{
	struct pnp_port *ptr;

	ptr = option->port;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = data;
	else
		option->port = data;

	dev_dbg(&dev->dev, "  io  "
		"min %#x max %#x align %d size %d flags %#x\n",
		data->min, data->max, data->align, data->size, data->flags);
	return 0;
}

int pnp_register_mem_resource(struct pnp_dev *dev, struct pnp_option *option,
			      struct pnp_mem *data)
{
	struct pnp_mem *ptr;

	ptr = option->mem;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = data;
	else
		option->mem = data;

	dev_dbg(&dev->dev, "  mem "
		"min %#x max %#x align %d size %d flags %#x\n",
		data->min, data->max, data->align, data->size, data->flags);
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

int pnp_check_port(struct pnp_dev *dev, struct resource *res)
{
	int i;
	struct pnp_dev *tdev;
	struct resource *tres;
	resource_size_t *port, *end, *tport, *tend;

	port = &res->start;
	end = &res->end;

	/* if the resource doesn't exist, don't complain about it */
	if (cannot_compare(res->flags))
		return 1;

	/* check if the resource is already in use, skip if the
	 * device is active because it itself may be in use */
	if (!dev->active) {
		if (__check_region(&ioport_resource, *port, length(port, end)))
			return 0;
	}

	/* check if the resource is reserved */
	for (i = 0; i < 8; i++) {
		int rport = pnp_reserve_io[i << 1];
		int rend = pnp_reserve_io[(i << 1) + 1] + rport - 1;
		if (ranged_conflict(port, end, &rport, &rend))
			return 0;
	}

	/* check for internal conflicts */
	for (i = 0; (tres = pnp_get_resource(dev, IORESOURCE_IO, i)); i++) {
		if (tres != res && tres->flags & IORESOURCE_IO) {
			tport = &tres->start;
			tend = &tres->end;
			if (ranged_conflict(port, end, tport, tend))
				return 0;
		}
	}

	/* check for conflicts with other pnp devices */
	pnp_for_each_dev(tdev) {
		if (tdev == dev)
			continue;
		for (i = 0;
		     (tres = pnp_get_resource(tdev, IORESOURCE_IO, i));
		     i++) {
			if (tres->flags & IORESOURCE_IO) {
				if (cannot_compare(tres->flags))
					continue;
				tport = &tres->start;
				tend = &tres->end;
				if (ranged_conflict(port, end, tport, tend))
					return 0;
			}
		}
	}

	return 1;
}

int pnp_check_mem(struct pnp_dev *dev, struct resource *res)
{
	int i;
	struct pnp_dev *tdev;
	struct resource *tres;
	resource_size_t *addr, *end, *taddr, *tend;

	addr = &res->start;
	end = &res->end;

	/* if the resource doesn't exist, don't complain about it */
	if (cannot_compare(res->flags))
		return 1;

	/* check if the resource is already in use, skip if the
	 * device is active because it itself may be in use */
	if (!dev->active) {
		if (check_mem_region(*addr, length(addr, end)))
			return 0;
	}

	/* check if the resource is reserved */
	for (i = 0; i < 8; i++) {
		int raddr = pnp_reserve_mem[i << 1];
		int rend = pnp_reserve_mem[(i << 1) + 1] + raddr - 1;
		if (ranged_conflict(addr, end, &raddr, &rend))
			return 0;
	}

	/* check for internal conflicts */
	for (i = 0; (tres = pnp_get_resource(dev, IORESOURCE_MEM, i)); i++) {
		if (tres != res && tres->flags & IORESOURCE_MEM) {
			taddr = &tres->start;
			tend = &tres->end;
			if (ranged_conflict(addr, end, taddr, tend))
				return 0;
		}
	}

	/* check for conflicts with other pnp devices */
	pnp_for_each_dev(tdev) {
		if (tdev == dev)
			continue;
		for (i = 0;
		     (tres = pnp_get_resource(tdev, IORESOURCE_MEM, i));
		     i++) {
			if (tres->flags & IORESOURCE_MEM) {
				if (cannot_compare(tres->flags))
					continue;
				taddr = &tres->start;
				tend = &tres->end;
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

int pnp_check_irq(struct pnp_dev *dev, struct resource *res)
{
	int i;
	struct pnp_dev *tdev;
	struct resource *tres;
	resource_size_t *irq;

	irq = &res->start;

	/* if the resource doesn't exist, don't complain about it */
	if (cannot_compare(res->flags))
		return 1;

	/* check if the resource is valid */
	if (*irq < 0 || *irq > 15)
		return 0;

	/* check if the resource is reserved */
	for (i = 0; i < 16; i++) {
		if (pnp_reserve_irq[i] == *irq)
			return 0;
	}

	/* check for internal conflicts */
	for (i = 0; (tres = pnp_get_resource(dev, IORESOURCE_IRQ, i)); i++) {
		if (tres != res && tres->flags & IORESOURCE_IRQ) {
			if (tres->start == *irq)
				return 0;
		}
	}

#ifdef CONFIG_PCI
	/* check if the resource is being used by a pci device */
	{
		struct pci_dev *pci = NULL;
		for_each_pci_dev(pci) {
			if (pci->irq == *irq) {
				pci_dev_put(pci);
				return 0;
			}
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
		for (i = 0;
		     (tres = pnp_get_resource(tdev, IORESOURCE_IRQ, i));
		     i++) {
			if (tres->flags & IORESOURCE_IRQ) {
				if (cannot_compare(tres->flags))
					continue;
				if (tres->start == *irq)
					return 0;
			}
		}
	}

	return 1;
}

int pnp_check_dma(struct pnp_dev *dev, struct resource *res)
{
#ifndef CONFIG_IA64
	int i;
	struct pnp_dev *tdev;
	struct resource *tres;
	resource_size_t *dma;

	dma = &res->start;

	/* if the resource doesn't exist, don't complain about it */
	if (cannot_compare(res->flags))
		return 1;

	/* check if the resource is valid */
	if (*dma < 0 || *dma == 4 || *dma > 7)
		return 0;

	/* check if the resource is reserved */
	for (i = 0; i < 8; i++) {
		if (pnp_reserve_dma[i] == *dma)
			return 0;
	}

	/* check for internal conflicts */
	for (i = 0; (tres = pnp_get_resource(dev, IORESOURCE_DMA, i)); i++) {
		if (tres != res && tres->flags & IORESOURCE_DMA) {
			if (tres->start == *dma)
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
		for (i = 0;
		     (tres = pnp_get_resource(tdev, IORESOURCE_DMA, i));
		     i++) {
			if (tres->flags & IORESOURCE_DMA) {
				if (cannot_compare(tres->flags))
					continue;
				if (tres->start == *dma)
					return 0;
			}
		}
	}

	return 1;
#else
	/* IA64 does not have legacy DMA */
	return 0;
#endif
}

struct pnp_resource *pnp_get_pnp_resource(struct pnp_dev *dev,
					  unsigned int type, unsigned int num)
{
	struct pnp_resource_table *res = dev->res;

	switch (type) {
	case IORESOURCE_IO:
		if (num >= PNP_MAX_PORT)
			return NULL;
		return &res->port[num];
	case IORESOURCE_MEM:
		if (num >= PNP_MAX_MEM)
			return NULL;
		return &res->mem[num];
	case IORESOURCE_IRQ:
		if (num >= PNP_MAX_IRQ)
			return NULL;
		return &res->irq[num];
	case IORESOURCE_DMA:
		if (num >= PNP_MAX_DMA)
			return NULL;
		return &res->dma[num];
	}
	return NULL;
}

struct resource *pnp_get_resource(struct pnp_dev *dev,
				  unsigned int type, unsigned int num)
{
	struct pnp_resource *pnp_res;

	pnp_res = pnp_get_pnp_resource(dev, type, num);
	if (pnp_res)
		return &pnp_res->res;

	return NULL;
}
EXPORT_SYMBOL(pnp_get_resource);

static struct pnp_resource *pnp_new_resource(struct pnp_dev *dev, int type)
{
	struct pnp_resource *pnp_res;
	int i;

	switch (type) {
	case IORESOURCE_IO:
		for (i = 0; i < PNP_MAX_PORT; i++) {
			pnp_res = pnp_get_pnp_resource(dev, IORESOURCE_IO, i);
			if (pnp_res && !pnp_resource_valid(&pnp_res->res))
				return pnp_res;
		}
		break;
	case IORESOURCE_MEM:
		for (i = 0; i < PNP_MAX_MEM; i++) {
			pnp_res = pnp_get_pnp_resource(dev, IORESOURCE_MEM, i);
			if (pnp_res && !pnp_resource_valid(&pnp_res->res))
				return pnp_res;
		}
		break;
	case IORESOURCE_IRQ:
		for (i = 0; i < PNP_MAX_IRQ; i++) {
			pnp_res = pnp_get_pnp_resource(dev, IORESOURCE_IRQ, i);
			if (pnp_res && !pnp_resource_valid(&pnp_res->res))
				return pnp_res;
		}
		break;
	case IORESOURCE_DMA:
		for (i = 0; i < PNP_MAX_DMA; i++) {
			pnp_res = pnp_get_pnp_resource(dev, IORESOURCE_DMA, i);
			if (pnp_res && !pnp_resource_valid(&pnp_res->res))
				return pnp_res;
		}
		break;
	}
	return NULL;
}

struct pnp_resource *pnp_add_irq_resource(struct pnp_dev *dev, int irq,
					  int flags)
{
	struct pnp_resource *pnp_res;
	struct resource *res;
	static unsigned char warned;

	pnp_res = pnp_new_resource(dev, IORESOURCE_IRQ);
	if (!pnp_res) {
		if (!warned) {
			dev_err(&dev->dev, "can't add resource for IRQ %d\n",
				irq);
			warned = 1;
		}
		return NULL;
	}

	res = &pnp_res->res;
	res->flags = IORESOURCE_IRQ | flags;
	res->start = irq;
	res->end = irq;

	dev_dbg(&dev->dev, "  add irq %d flags %#x\n", irq, flags);
	return pnp_res;
}

struct pnp_resource *pnp_add_dma_resource(struct pnp_dev *dev, int dma,
					  int flags)
{
	struct pnp_resource *pnp_res;
	struct resource *res;
	static unsigned char warned;

	pnp_res = pnp_new_resource(dev, IORESOURCE_DMA);
	if (!pnp_res) {
		if (!warned) {
			dev_err(&dev->dev, "can't add resource for DMA %d\n",
				dma);
			warned = 1;
		}
		return NULL;
	}

	res = &pnp_res->res;
	res->flags = IORESOURCE_DMA | flags;
	res->start = dma;
	res->end = dma;

	dev_dbg(&dev->dev, "  add dma %d flags %#x\n", dma, flags);
	return pnp_res;
}

struct pnp_resource *pnp_add_io_resource(struct pnp_dev *dev,
					 resource_size_t start,
					 resource_size_t end, int flags)
{
	struct pnp_resource *pnp_res;
	struct resource *res;
	static unsigned char warned;

	pnp_res = pnp_new_resource(dev, IORESOURCE_IO);
	if (!pnp_res) {
		if (!warned) {
			dev_err(&dev->dev, "can't add resource for IO "
				"%#llx-%#llx\n",(unsigned long long) start,
				(unsigned long long) end);
			warned = 1;
		}
		return NULL;
	}

	res = &pnp_res->res;
	res->flags = IORESOURCE_IO | flags;
	res->start = start;
	res->end = end;

	dev_dbg(&dev->dev, "  add io  %#llx-%#llx flags %#x\n",
		(unsigned long long) start, (unsigned long long) end, flags);
	return pnp_res;
}

struct pnp_resource *pnp_add_mem_resource(struct pnp_dev *dev,
					  resource_size_t start,
					  resource_size_t end, int flags)
{
	struct pnp_resource *pnp_res;
	struct resource *res;
	static unsigned char warned;

	pnp_res = pnp_new_resource(dev, IORESOURCE_MEM);
	if (!pnp_res) {
		if (!warned) {
			dev_err(&dev->dev, "can't add resource for MEM "
				"%#llx-%#llx\n",(unsigned long long) start,
				(unsigned long long) end);
			warned = 1;
		}
		return NULL;
	}

	res = &pnp_res->res;
	res->flags = IORESOURCE_MEM | flags;
	res->start = start;
	res->end = end;

	dev_dbg(&dev->dev, "  add mem %#llx-%#llx flags %#x\n",
		(unsigned long long) start, (unsigned long long) end, flags);
	return pnp_res;
}

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
