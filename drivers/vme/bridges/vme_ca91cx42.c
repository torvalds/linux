/*
 * Support for the Tundra Universe I/II VME-PCI Bridge Chips
 *
 * Author: Martyn Welch <martyn.welch@ge.com>
 * Copyright 2008 GE Intelligent Platforms Embedded Systems, Inc.
 *
 * Based on work by Tom Armistead and Ajit Prem
 * Copyright 2004 Motorola Inc.
 *
 * Derived from ca91c042.c by Michael Wyrick
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/poll.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/vme.h>

#include "../vme_bridge.h"
#include "vme_ca91cx42.h"

static int ca91cx42_probe(struct pci_dev *, const struct pci_device_id *);
static void ca91cx42_remove(struct pci_dev *);

/* Module parameters */
static int geoid;

static const char driver_name[] = "vme_ca91cx42";

static const struct pci_device_id ca91cx42_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_TUNDRA, PCI_DEVICE_ID_TUNDRA_CA91C142) },
	{ },
};

static struct pci_driver ca91cx42_driver = {
	.name = driver_name,
	.id_table = ca91cx42_ids,
	.probe = ca91cx42_probe,
	.remove = ca91cx42_remove,
};

static u32 ca91cx42_DMA_irqhandler(struct ca91cx42_driver *bridge)
{
	wake_up(&bridge->dma_queue);

	return CA91CX42_LINT_DMA;
}

static u32 ca91cx42_LM_irqhandler(struct ca91cx42_driver *bridge, u32 stat)
{
	int i;
	u32 serviced = 0;

	for (i = 0; i < 4; i++) {
		if (stat & CA91CX42_LINT_LM[i]) {
			/* We only enable interrupts if the callback is set */
			bridge->lm_callback[i](i);
			serviced |= CA91CX42_LINT_LM[i];
		}
	}

	return serviced;
}

/* XXX This needs to be split into 4 queues */
static u32 ca91cx42_MB_irqhandler(struct ca91cx42_driver *bridge, int mbox_mask)
{
	wake_up(&bridge->mbox_queue);

	return CA91CX42_LINT_MBOX;
}

static u32 ca91cx42_IACK_irqhandler(struct ca91cx42_driver *bridge)
{
	wake_up(&bridge->iack_queue);

	return CA91CX42_LINT_SW_IACK;
}

static u32 ca91cx42_VERR_irqhandler(struct vme_bridge *ca91cx42_bridge)
{
	int val;
	struct ca91cx42_driver *bridge;

	bridge = ca91cx42_bridge->driver_priv;

	val = ioread32(bridge->base + DGCS);

	if (!(val & 0x00000800)) {
		dev_err(ca91cx42_bridge->parent, "ca91cx42_VERR_irqhandler DMA "
			"Read Error DGCS=%08X\n", val);
	}

	return CA91CX42_LINT_VERR;
}

static u32 ca91cx42_LERR_irqhandler(struct vme_bridge *ca91cx42_bridge)
{
	int val;
	struct ca91cx42_driver *bridge;

	bridge = ca91cx42_bridge->driver_priv;

	val = ioread32(bridge->base + DGCS);

	if (!(val & 0x00000800))
		dev_err(ca91cx42_bridge->parent, "ca91cx42_LERR_irqhandler DMA "
			"Read Error DGCS=%08X\n", val);

	return CA91CX42_LINT_LERR;
}


static u32 ca91cx42_VIRQ_irqhandler(struct vme_bridge *ca91cx42_bridge,
	int stat)
{
	int vec, i, serviced = 0;
	struct ca91cx42_driver *bridge;

	bridge = ca91cx42_bridge->driver_priv;


	for (i = 7; i > 0; i--) {
		if (stat & (1 << i)) {
			vec = ioread32(bridge->base +
				CA91CX42_V_STATID[i]) & 0xff;

			vme_irq_handler(ca91cx42_bridge, i, vec);

			serviced |= (1 << i);
		}
	}

	return serviced;
}

static irqreturn_t ca91cx42_irqhandler(int irq, void *ptr)
{
	u32 stat, enable, serviced = 0;
	struct vme_bridge *ca91cx42_bridge;
	struct ca91cx42_driver *bridge;

	ca91cx42_bridge = ptr;

	bridge = ca91cx42_bridge->driver_priv;

	enable = ioread32(bridge->base + LINT_EN);
	stat = ioread32(bridge->base + LINT_STAT);

	/* Only look at unmasked interrupts */
	stat &= enable;

	if (unlikely(!stat))
		return IRQ_NONE;

	if (stat & CA91CX42_LINT_DMA)
		serviced |= ca91cx42_DMA_irqhandler(bridge);
	if (stat & (CA91CX42_LINT_LM0 | CA91CX42_LINT_LM1 | CA91CX42_LINT_LM2 |
			CA91CX42_LINT_LM3))
		serviced |= ca91cx42_LM_irqhandler(bridge, stat);
	if (stat & CA91CX42_LINT_MBOX)
		serviced |= ca91cx42_MB_irqhandler(bridge, stat);
	if (stat & CA91CX42_LINT_SW_IACK)
		serviced |= ca91cx42_IACK_irqhandler(bridge);
	if (stat & CA91CX42_LINT_VERR)
		serviced |= ca91cx42_VERR_irqhandler(ca91cx42_bridge);
	if (stat & CA91CX42_LINT_LERR)
		serviced |= ca91cx42_LERR_irqhandler(ca91cx42_bridge);
	if (stat & (CA91CX42_LINT_VIRQ1 | CA91CX42_LINT_VIRQ2 |
			CA91CX42_LINT_VIRQ3 | CA91CX42_LINT_VIRQ4 |
			CA91CX42_LINT_VIRQ5 | CA91CX42_LINT_VIRQ6 |
			CA91CX42_LINT_VIRQ7))
		serviced |= ca91cx42_VIRQ_irqhandler(ca91cx42_bridge, stat);

	/* Clear serviced interrupts */
	iowrite32(serviced, bridge->base + LINT_STAT);

	return IRQ_HANDLED;
}

static int ca91cx42_irq_init(struct vme_bridge *ca91cx42_bridge)
{
	int result, tmp;
	struct pci_dev *pdev;
	struct ca91cx42_driver *bridge;

	bridge = ca91cx42_bridge->driver_priv;

	/* Need pdev */
	pdev = container_of(ca91cx42_bridge->parent, struct pci_dev, dev);

	/* Initialise list for VME bus errors */
	INIT_LIST_HEAD(&ca91cx42_bridge->vme_errors);

	mutex_init(&ca91cx42_bridge->irq_mtx);

	/* Disable interrupts from PCI to VME */
	iowrite32(0, bridge->base + VINT_EN);

	/* Disable PCI interrupts */
	iowrite32(0, bridge->base + LINT_EN);
	/* Clear Any Pending PCI Interrupts */
	iowrite32(0x00FFFFFF, bridge->base + LINT_STAT);

	result = request_irq(pdev->irq, ca91cx42_irqhandler, IRQF_SHARED,
			driver_name, ca91cx42_bridge);
	if (result) {
		dev_err(&pdev->dev, "Can't get assigned pci irq vector %02X\n",
		       pdev->irq);
		return result;
	}

	/* Ensure all interrupts are mapped to PCI Interrupt 0 */
	iowrite32(0, bridge->base + LINT_MAP0);
	iowrite32(0, bridge->base + LINT_MAP1);
	iowrite32(0, bridge->base + LINT_MAP2);

	/* Enable DMA, mailbox & LM Interrupts */
	tmp = CA91CX42_LINT_MBOX3 | CA91CX42_LINT_MBOX2 | CA91CX42_LINT_MBOX1 |
		CA91CX42_LINT_MBOX0 | CA91CX42_LINT_SW_IACK |
		CA91CX42_LINT_VERR | CA91CX42_LINT_LERR | CA91CX42_LINT_DMA;

	iowrite32(tmp, bridge->base + LINT_EN);

	return 0;
}

static void ca91cx42_irq_exit(struct ca91cx42_driver *bridge,
	struct pci_dev *pdev)
{
	struct vme_bridge *ca91cx42_bridge;

	/* Disable interrupts from PCI to VME */
	iowrite32(0, bridge->base + VINT_EN);

	/* Disable PCI interrupts */
	iowrite32(0, bridge->base + LINT_EN);
	/* Clear Any Pending PCI Interrupts */
	iowrite32(0x00FFFFFF, bridge->base + LINT_STAT);

	ca91cx42_bridge = container_of((void *)bridge, struct vme_bridge,
				       driver_priv);
	free_irq(pdev->irq, ca91cx42_bridge);
}

static int ca91cx42_iack_received(struct ca91cx42_driver *bridge, int level)
{
	u32 tmp;

	tmp = ioread32(bridge->base + LINT_STAT);

	if (tmp & (1 << level))
		return 0;
	else
		return 1;
}

/*
 * Set up an VME interrupt
 */
static void ca91cx42_irq_set(struct vme_bridge *ca91cx42_bridge, int level,
	int state, int sync)

{
	struct pci_dev *pdev;
	u32 tmp;
	struct ca91cx42_driver *bridge;

	bridge = ca91cx42_bridge->driver_priv;

	/* Enable IRQ level */
	tmp = ioread32(bridge->base + LINT_EN);

	if (state == 0)
		tmp &= ~CA91CX42_LINT_VIRQ[level];
	else
		tmp |= CA91CX42_LINT_VIRQ[level];

	iowrite32(tmp, bridge->base + LINT_EN);

	if ((state == 0) && (sync != 0)) {
		pdev = container_of(ca91cx42_bridge->parent, struct pci_dev,
			dev);

		synchronize_irq(pdev->irq);
	}
}

static int ca91cx42_irq_generate(struct vme_bridge *ca91cx42_bridge, int level,
	int statid)
{
	u32 tmp;
	struct ca91cx42_driver *bridge;

	bridge = ca91cx42_bridge->driver_priv;

	/* Universe can only generate even vectors */
	if (statid & 1)
		return -EINVAL;

	mutex_lock(&bridge->vme_int);

	tmp = ioread32(bridge->base + VINT_EN);

	/* Set Status/ID */
	iowrite32(statid << 24, bridge->base + STATID);

	/* Assert VMEbus IRQ */
	tmp = tmp | (1 << (level + 24));
	iowrite32(tmp, bridge->base + VINT_EN);

	/* Wait for IACK */
	wait_event_interruptible(bridge->iack_queue,
				 ca91cx42_iack_received(bridge, level));

	/* Return interrupt to low state */
	tmp = ioread32(bridge->base + VINT_EN);
	tmp = tmp & ~(1 << (level + 24));
	iowrite32(tmp, bridge->base + VINT_EN);

	mutex_unlock(&bridge->vme_int);

	return 0;
}

static int ca91cx42_slave_set(struct vme_slave_resource *image, int enabled,
	unsigned long long vme_base, unsigned long long size,
	dma_addr_t pci_base, u32 aspace, u32 cycle)
{
	unsigned int i, addr = 0, granularity;
	unsigned int temp_ctl = 0;
	unsigned int vme_bound, pci_offset;
	struct vme_bridge *ca91cx42_bridge;
	struct ca91cx42_driver *bridge;

	ca91cx42_bridge = image->parent;

	bridge = ca91cx42_bridge->driver_priv;

	i = image->number;

	switch (aspace) {
	case VME_A16:
		addr |= CA91CX42_VSI_CTL_VAS_A16;
		break;
	case VME_A24:
		addr |= CA91CX42_VSI_CTL_VAS_A24;
		break;
	case VME_A32:
		addr |= CA91CX42_VSI_CTL_VAS_A32;
		break;
	case VME_USER1:
		addr |= CA91CX42_VSI_CTL_VAS_USER1;
		break;
	case VME_USER2:
		addr |= CA91CX42_VSI_CTL_VAS_USER2;
		break;
	case VME_A64:
	case VME_CRCSR:
	case VME_USER3:
	case VME_USER4:
	default:
		dev_err(ca91cx42_bridge->parent, "Invalid address space\n");
		return -EINVAL;
		break;
	}

	/*
	 * Bound address is a valid address for the window, adjust
	 * accordingly
	 */
	vme_bound = vme_base + size;
	pci_offset = pci_base - vme_base;

	if ((i == 0) || (i == 4))
		granularity = 0x1000;
	else
		granularity = 0x10000;

	if (vme_base & (granularity - 1)) {
		dev_err(ca91cx42_bridge->parent, "Invalid VME base "
			"alignment\n");
		return -EINVAL;
	}
	if (vme_bound & (granularity - 1)) {
		dev_err(ca91cx42_bridge->parent, "Invalid VME bound "
			"alignment\n");
		return -EINVAL;
	}
	if (pci_offset & (granularity - 1)) {
		dev_err(ca91cx42_bridge->parent, "Invalid PCI Offset "
			"alignment\n");
		return -EINVAL;
	}

	/* Disable while we are mucking around */
	temp_ctl = ioread32(bridge->base + CA91CX42_VSI_CTL[i]);
	temp_ctl &= ~CA91CX42_VSI_CTL_EN;
	iowrite32(temp_ctl, bridge->base + CA91CX42_VSI_CTL[i]);

	/* Setup mapping */
	iowrite32(vme_base, bridge->base + CA91CX42_VSI_BS[i]);
	iowrite32(vme_bound, bridge->base + CA91CX42_VSI_BD[i]);
	iowrite32(pci_offset, bridge->base + CA91CX42_VSI_TO[i]);

	/* Setup address space */
	temp_ctl &= ~CA91CX42_VSI_CTL_VAS_M;
	temp_ctl |= addr;

	/* Setup cycle types */
	temp_ctl &= ~(CA91CX42_VSI_CTL_PGM_M | CA91CX42_VSI_CTL_SUPER_M);
	if (cycle & VME_SUPER)
		temp_ctl |= CA91CX42_VSI_CTL_SUPER_SUPR;
	if (cycle & VME_USER)
		temp_ctl |= CA91CX42_VSI_CTL_SUPER_NPRIV;
	if (cycle & VME_PROG)
		temp_ctl |= CA91CX42_VSI_CTL_PGM_PGM;
	if (cycle & VME_DATA)
		temp_ctl |= CA91CX42_VSI_CTL_PGM_DATA;

	/* Write ctl reg without enable */
	iowrite32(temp_ctl, bridge->base + CA91CX42_VSI_CTL[i]);

	if (enabled)
		temp_ctl |= CA91CX42_VSI_CTL_EN;

	iowrite32(temp_ctl, bridge->base + CA91CX42_VSI_CTL[i]);

	return 0;
}

static int ca91cx42_slave_get(struct vme_slave_resource *image, int *enabled,
	unsigned long long *vme_base, unsigned long long *size,
	dma_addr_t *pci_base, u32 *aspace, u32 *cycle)
{
	unsigned int i, granularity = 0, ctl = 0;
	unsigned long long vme_bound, pci_offset;
	struct ca91cx42_driver *bridge;

	bridge = image->parent->driver_priv;

	i = image->number;

	if ((i == 0) || (i == 4))
		granularity = 0x1000;
	else
		granularity = 0x10000;

	/* Read Registers */
	ctl = ioread32(bridge->base + CA91CX42_VSI_CTL[i]);

	*vme_base = ioread32(bridge->base + CA91CX42_VSI_BS[i]);
	vme_bound = ioread32(bridge->base + CA91CX42_VSI_BD[i]);
	pci_offset = ioread32(bridge->base + CA91CX42_VSI_TO[i]);

	*pci_base = (dma_addr_t)vme_base + pci_offset;
	*size = (unsigned long long)((vme_bound - *vme_base) + granularity);

	*enabled = 0;
	*aspace = 0;
	*cycle = 0;

	if (ctl & CA91CX42_VSI_CTL_EN)
		*enabled = 1;

	if ((ctl & CA91CX42_VSI_CTL_VAS_M) == CA91CX42_VSI_CTL_VAS_A16)
		*aspace = VME_A16;
	if ((ctl & CA91CX42_VSI_CTL_VAS_M) == CA91CX42_VSI_CTL_VAS_A24)
		*aspace = VME_A24;
	if ((ctl & CA91CX42_VSI_CTL_VAS_M) == CA91CX42_VSI_CTL_VAS_A32)
		*aspace = VME_A32;
	if ((ctl & CA91CX42_VSI_CTL_VAS_M) == CA91CX42_VSI_CTL_VAS_USER1)
		*aspace = VME_USER1;
	if ((ctl & CA91CX42_VSI_CTL_VAS_M) == CA91CX42_VSI_CTL_VAS_USER2)
		*aspace = VME_USER2;

	if (ctl & CA91CX42_VSI_CTL_SUPER_SUPR)
		*cycle |= VME_SUPER;
	if (ctl & CA91CX42_VSI_CTL_SUPER_NPRIV)
		*cycle |= VME_USER;
	if (ctl & CA91CX42_VSI_CTL_PGM_PGM)
		*cycle |= VME_PROG;
	if (ctl & CA91CX42_VSI_CTL_PGM_DATA)
		*cycle |= VME_DATA;

	return 0;
}

/*
 * Allocate and map PCI Resource
 */
static int ca91cx42_alloc_resource(struct vme_master_resource *image,
	unsigned long long size)
{
	unsigned long long existing_size;
	int retval = 0;
	struct pci_dev *pdev;
	struct vme_bridge *ca91cx42_bridge;

	ca91cx42_bridge = image->parent;

	/* Find pci_dev container of dev */
	if (ca91cx42_bridge->parent == NULL) {
		dev_err(ca91cx42_bridge->parent, "Dev entry NULL\n");
		return -EINVAL;
	}
	pdev = container_of(ca91cx42_bridge->parent, struct pci_dev, dev);

	existing_size = (unsigned long long)(image->bus_resource.end -
		image->bus_resource.start);

	/* If the existing size is OK, return */
	if (existing_size == (size - 1))
		return 0;

	if (existing_size != 0) {
		iounmap(image->kern_base);
		image->kern_base = NULL;
		kfree(image->bus_resource.name);
		release_resource(&image->bus_resource);
		memset(&image->bus_resource, 0, sizeof(struct resource));
	}

	if (image->bus_resource.name == NULL) {
		image->bus_resource.name = kmalloc(VMENAMSIZ+3, GFP_ATOMIC);
		if (image->bus_resource.name == NULL) {
			dev_err(ca91cx42_bridge->parent, "Unable to allocate "
				"memory for resource name\n");
			retval = -ENOMEM;
			goto err_name;
		}
	}

	sprintf((char *)image->bus_resource.name, "%s.%d",
		ca91cx42_bridge->name, image->number);

	image->bus_resource.start = 0;
	image->bus_resource.end = (unsigned long)size;
	image->bus_resource.flags = IORESOURCE_MEM;

	retval = pci_bus_alloc_resource(pdev->bus,
		&image->bus_resource, size, size, PCIBIOS_MIN_MEM,
		0, NULL, NULL);
	if (retval) {
		dev_err(ca91cx42_bridge->parent, "Failed to allocate mem "
			"resource for window %d size 0x%lx start 0x%lx\n",
			image->number, (unsigned long)size,
			(unsigned long)image->bus_resource.start);
		goto err_resource;
	}

	image->kern_base = ioremap_nocache(
		image->bus_resource.start, size);
	if (image->kern_base == NULL) {
		dev_err(ca91cx42_bridge->parent, "Failed to remap resource\n");
		retval = -ENOMEM;
		goto err_remap;
	}

	return 0;

err_remap:
	release_resource(&image->bus_resource);
err_resource:
	kfree(image->bus_resource.name);
	memset(&image->bus_resource, 0, sizeof(struct resource));
err_name:
	return retval;
}

/*
 * Free and unmap PCI Resource
 */
static void ca91cx42_free_resource(struct vme_master_resource *image)
{
	iounmap(image->kern_base);
	image->kern_base = NULL;
	release_resource(&image->bus_resource);
	kfree(image->bus_resource.name);
	memset(&image->bus_resource, 0, sizeof(struct resource));
}


static int ca91cx42_master_set(struct vme_master_resource *image, int enabled,
	unsigned long long vme_base, unsigned long long size, u32 aspace,
	u32 cycle, u32 dwidth)
{
	int retval = 0;
	unsigned int i, granularity = 0;
	unsigned int temp_ctl = 0;
	unsigned long long pci_bound, vme_offset, pci_base;
	struct vme_bridge *ca91cx42_bridge;
	struct ca91cx42_driver *bridge;

	ca91cx42_bridge = image->parent;

	bridge = ca91cx42_bridge->driver_priv;

	i = image->number;

	if ((i == 0) || (i == 4))
		granularity = 0x1000;
	else
		granularity = 0x10000;

	/* Verify input data */
	if (vme_base & (granularity - 1)) {
		dev_err(ca91cx42_bridge->parent, "Invalid VME Window "
			"alignment\n");
		retval = -EINVAL;
		goto err_window;
	}
	if (size & (granularity - 1)) {
		dev_err(ca91cx42_bridge->parent, "Invalid VME Window "
			"alignment\n");
		retval = -EINVAL;
		goto err_window;
	}

	spin_lock(&image->lock);

	/*
	 * Let's allocate the resource here rather than further up the stack as
	 * it avoids pushing loads of bus dependent stuff up the stack
	 */
	retval = ca91cx42_alloc_resource(image, size);
	if (retval) {
		spin_unlock(&image->lock);
		dev_err(ca91cx42_bridge->parent, "Unable to allocate memory "
			"for resource name\n");
		retval = -ENOMEM;
		goto err_res;
	}

	pci_base = (unsigned long long)image->bus_resource.start;

	/*
	 * Bound address is a valid address for the window, adjust
	 * according to window granularity.
	 */
	pci_bound = pci_base + size;
	vme_offset = vme_base - pci_base;

	/* Disable while we are mucking around */
	temp_ctl = ioread32(bridge->base + CA91CX42_LSI_CTL[i]);
	temp_ctl &= ~CA91CX42_LSI_CTL_EN;
	iowrite32(temp_ctl, bridge->base + CA91CX42_LSI_CTL[i]);

	/* Setup cycle types */
	temp_ctl &= ~CA91CX42_LSI_CTL_VCT_M;
	if (cycle & VME_BLT)
		temp_ctl |= CA91CX42_LSI_CTL_VCT_BLT;
	if (cycle & VME_MBLT)
		temp_ctl |= CA91CX42_LSI_CTL_VCT_MBLT;

	/* Setup data width */
	temp_ctl &= ~CA91CX42_LSI_CTL_VDW_M;
	switch (dwidth) {
	case VME_D8:
		temp_ctl |= CA91CX42_LSI_CTL_VDW_D8;
		break;
	case VME_D16:
		temp_ctl |= CA91CX42_LSI_CTL_VDW_D16;
		break;
	case VME_D32:
		temp_ctl |= CA91CX42_LSI_CTL_VDW_D32;
		break;
	case VME_D64:
		temp_ctl |= CA91CX42_LSI_CTL_VDW_D64;
		break;
	default:
		spin_unlock(&image->lock);
		dev_err(ca91cx42_bridge->parent, "Invalid data width\n");
		retval = -EINVAL;
		goto err_dwidth;
		break;
	}

	/* Setup address space */
	temp_ctl &= ~CA91CX42_LSI_CTL_VAS_M;
	switch (aspace) {
	case VME_A16:
		temp_ctl |= CA91CX42_LSI_CTL_VAS_A16;
		break;
	case VME_A24:
		temp_ctl |= CA91CX42_LSI_CTL_VAS_A24;
		break;
	case VME_A32:
		temp_ctl |= CA91CX42_LSI_CTL_VAS_A32;
		break;
	case VME_CRCSR:
		temp_ctl |= CA91CX42_LSI_CTL_VAS_CRCSR;
		break;
	case VME_USER1:
		temp_ctl |= CA91CX42_LSI_CTL_VAS_USER1;
		break;
	case VME_USER2:
		temp_ctl |= CA91CX42_LSI_CTL_VAS_USER2;
		break;
	case VME_A64:
	case VME_USER3:
	case VME_USER4:
	default:
		spin_unlock(&image->lock);
		dev_err(ca91cx42_bridge->parent, "Invalid address space\n");
		retval = -EINVAL;
		goto err_aspace;
		break;
	}

	temp_ctl &= ~(CA91CX42_LSI_CTL_PGM_M | CA91CX42_LSI_CTL_SUPER_M);
	if (cycle & VME_SUPER)
		temp_ctl |= CA91CX42_LSI_CTL_SUPER_SUPR;
	if (cycle & VME_PROG)
		temp_ctl |= CA91CX42_LSI_CTL_PGM_PGM;

	/* Setup mapping */
	iowrite32(pci_base, bridge->base + CA91CX42_LSI_BS[i]);
	iowrite32(pci_bound, bridge->base + CA91CX42_LSI_BD[i]);
	iowrite32(vme_offset, bridge->base + CA91CX42_LSI_TO[i]);

	/* Write ctl reg without enable */
	iowrite32(temp_ctl, bridge->base + CA91CX42_LSI_CTL[i]);

	if (enabled)
		temp_ctl |= CA91CX42_LSI_CTL_EN;

	iowrite32(temp_ctl, bridge->base + CA91CX42_LSI_CTL[i]);

	spin_unlock(&image->lock);
	return 0;

err_aspace:
err_dwidth:
	ca91cx42_free_resource(image);
err_res:
err_window:
	return retval;
}

static int __ca91cx42_master_get(struct vme_master_resource *image,
	int *enabled, unsigned long long *vme_base, unsigned long long *size,
	u32 *aspace, u32 *cycle, u32 *dwidth)
{
	unsigned int i, ctl;
	unsigned long long pci_base, pci_bound, vme_offset;
	struct ca91cx42_driver *bridge;

	bridge = image->parent->driver_priv;

	i = image->number;

	ctl = ioread32(bridge->base + CA91CX42_LSI_CTL[i]);

	pci_base = ioread32(bridge->base + CA91CX42_LSI_BS[i]);
	vme_offset = ioread32(bridge->base + CA91CX42_LSI_TO[i]);
	pci_bound = ioread32(bridge->base + CA91CX42_LSI_BD[i]);

	*vme_base = pci_base + vme_offset;
	*size = (unsigned long long)(pci_bound - pci_base);

	*enabled = 0;
	*aspace = 0;
	*cycle = 0;
	*dwidth = 0;

	if (ctl & CA91CX42_LSI_CTL_EN)
		*enabled = 1;

	/* Setup address space */
	switch (ctl & CA91CX42_LSI_CTL_VAS_M) {
	case CA91CX42_LSI_CTL_VAS_A16:
		*aspace = VME_A16;
		break;
	case CA91CX42_LSI_CTL_VAS_A24:
		*aspace = VME_A24;
		break;
	case CA91CX42_LSI_CTL_VAS_A32:
		*aspace = VME_A32;
		break;
	case CA91CX42_LSI_CTL_VAS_CRCSR:
		*aspace = VME_CRCSR;
		break;
	case CA91CX42_LSI_CTL_VAS_USER1:
		*aspace = VME_USER1;
		break;
	case CA91CX42_LSI_CTL_VAS_USER2:
		*aspace = VME_USER2;
		break;
	}

	/* XXX Not sure howto check for MBLT */
	/* Setup cycle types */
	if (ctl & CA91CX42_LSI_CTL_VCT_BLT)
		*cycle |= VME_BLT;
	else
		*cycle |= VME_SCT;

	if (ctl & CA91CX42_LSI_CTL_SUPER_SUPR)
		*cycle |= VME_SUPER;
	else
		*cycle |= VME_USER;

	if (ctl & CA91CX42_LSI_CTL_PGM_PGM)
		*cycle = VME_PROG;
	else
		*cycle = VME_DATA;

	/* Setup data width */
	switch (ctl & CA91CX42_LSI_CTL_VDW_M) {
	case CA91CX42_LSI_CTL_VDW_D8:
		*dwidth = VME_D8;
		break;
	case CA91CX42_LSI_CTL_VDW_D16:
		*dwidth = VME_D16;
		break;
	case CA91CX42_LSI_CTL_VDW_D32:
		*dwidth = VME_D32;
		break;
	case CA91CX42_LSI_CTL_VDW_D64:
		*dwidth = VME_D64;
		break;
	}

	return 0;
}

static int ca91cx42_master_get(struct vme_master_resource *image, int *enabled,
	unsigned long long *vme_base, unsigned long long *size, u32 *aspace,
	u32 *cycle, u32 *dwidth)
{
	int retval;

	spin_lock(&image->lock);

	retval = __ca91cx42_master_get(image, enabled, vme_base, size, aspace,
		cycle, dwidth);

	spin_unlock(&image->lock);

	return retval;
}

static ssize_t ca91cx42_master_read(struct vme_master_resource *image,
	void *buf, size_t count, loff_t offset)
{
	ssize_t retval;
	void __iomem *addr = image->kern_base + offset;
	unsigned int done = 0;
	unsigned int count32;

	if (count == 0)
		return 0;

	spin_lock(&image->lock);

	/* The following code handles VME address alignment problem
	 * in order to assure the maximal data width cycle.
	 * We cannot use memcpy_xxx directly here because it
	 * may cut data transfer in 8-bits cycles, thus making
	 * D16 cycle impossible.
	 * From the other hand, the bridge itself assures that
	 * maximal configured data cycle is used and splits it
	 * automatically for non-aligned addresses.
	 */
	if ((uintptr_t)addr & 0x1) {
		*(u8 *)buf = ioread8(addr);
		done += 1;
		if (done == count)
			goto out;
	}
	if ((uintptr_t)addr & 0x2) {
		if ((count - done) < 2) {
			*(u8 *)(buf + done) = ioread8(addr + done);
			done += 1;
			goto out;
		} else {
			*(u16 *)(buf + done) = ioread16(addr + done);
			done += 2;
		}
	}

	count32 = (count - done) & ~0x3;
	if (count32 > 0) {
		memcpy_fromio(buf + done, addr + done, (unsigned int)count);
		done += count32;
	}

	if ((count - done) & 0x2) {
		*(u16 *)(buf + done) = ioread16(addr + done);
		done += 2;
	}
	if ((count - done) & 0x1) {
		*(u8 *)(buf + done) = ioread8(addr + done);
		done += 1;
	}
out:
	retval = count;
	spin_unlock(&image->lock);

	return retval;
}

static ssize_t ca91cx42_master_write(struct vme_master_resource *image,
	void *buf, size_t count, loff_t offset)
{
	ssize_t retval;
	void __iomem *addr = image->kern_base + offset;
	unsigned int done = 0;
	unsigned int count32;

	if (count == 0)
		return 0;

	spin_lock(&image->lock);

	/* Here we apply for the same strategy we do in master_read
	 * function in order to assure D16 cycle when required.
	 */
	if ((uintptr_t)addr & 0x1) {
		iowrite8(*(u8 *)buf, addr);
		done += 1;
		if (done == count)
			goto out;
	}
	if ((uintptr_t)addr & 0x2) {
		if ((count - done) < 2) {
			iowrite8(*(u8 *)(buf + done), addr + done);
			done += 1;
			goto out;
		} else {
			iowrite16(*(u16 *)(buf + done), addr + done);
			done += 2;
		}
	}

	count32 = (count - done) & ~0x3;
	if (count32 > 0) {
		memcpy_toio(addr + done, buf + done, count32);
		done += count32;
	}

	if ((count - done) & 0x2) {
		iowrite16(*(u16 *)(buf + done), addr + done);
		done += 2;
	}
	if ((count - done) & 0x1) {
		iowrite8(*(u8 *)(buf + done), addr + done);
		done += 1;
	}
out:
	retval = count;

	spin_unlock(&image->lock);

	return retval;
}

static unsigned int ca91cx42_master_rmw(struct vme_master_resource *image,
	unsigned int mask, unsigned int compare, unsigned int swap,
	loff_t offset)
{
	u32 result;
	uintptr_t pci_addr;
	int i;
	struct ca91cx42_driver *bridge;
	struct device *dev;

	bridge = image->parent->driver_priv;
	dev = image->parent->parent;

	/* Find the PCI address that maps to the desired VME address */
	i = image->number;

	/* Locking as we can only do one of these at a time */
	mutex_lock(&bridge->vme_rmw);

	/* Lock image */
	spin_lock(&image->lock);

	pci_addr = (uintptr_t)image->kern_base + offset;

	/* Address must be 4-byte aligned */
	if (pci_addr & 0x3) {
		dev_err(dev, "RMW Address not 4-byte aligned\n");
		result = -EINVAL;
		goto out;
	}

	/* Ensure RMW Disabled whilst configuring */
	iowrite32(0, bridge->base + SCYC_CTL);

	/* Configure registers */
	iowrite32(mask, bridge->base + SCYC_EN);
	iowrite32(compare, bridge->base + SCYC_CMP);
	iowrite32(swap, bridge->base + SCYC_SWP);
	iowrite32(pci_addr, bridge->base + SCYC_ADDR);

	/* Enable RMW */
	iowrite32(CA91CX42_SCYC_CTL_CYC_RMW, bridge->base + SCYC_CTL);

	/* Kick process off with a read to the required address. */
	result = ioread32(image->kern_base + offset);

	/* Disable RMW */
	iowrite32(0, bridge->base + SCYC_CTL);

out:
	spin_unlock(&image->lock);

	mutex_unlock(&bridge->vme_rmw);

	return result;
}

static int ca91cx42_dma_list_add(struct vme_dma_list *list,
	struct vme_dma_attr *src, struct vme_dma_attr *dest, size_t count)
{
	struct ca91cx42_dma_entry *entry, *prev;
	struct vme_dma_pci *pci_attr;
	struct vme_dma_vme *vme_attr;
	dma_addr_t desc_ptr;
	int retval = 0;
	struct device *dev;

	dev = list->parent->parent->parent;

	/* XXX descriptor must be aligned on 64-bit boundaries */
	entry = kmalloc(sizeof(struct ca91cx42_dma_entry), GFP_KERNEL);
	if (entry == NULL) {
		dev_err(dev, "Failed to allocate memory for dma resource "
			"structure\n");
		retval = -ENOMEM;
		goto err_mem;
	}

	/* Test descriptor alignment */
	if ((unsigned long)&entry->descriptor & CA91CX42_DCPP_M) {
		dev_err(dev, "Descriptor not aligned to 16 byte boundary as "
			"required: %p\n", &entry->descriptor);
		retval = -EINVAL;
		goto err_align;
	}

	memset(&entry->descriptor, 0, sizeof(struct ca91cx42_dma_descriptor));

	if (dest->type == VME_DMA_VME) {
		entry->descriptor.dctl |= CA91CX42_DCTL_L2V;
		vme_attr = dest->private;
		pci_attr = src->private;
	} else {
		vme_attr = src->private;
		pci_attr = dest->private;
	}

	/* Check we can do fulfill required attributes */
	if ((vme_attr->aspace & ~(VME_A16 | VME_A24 | VME_A32 | VME_USER1 |
		VME_USER2)) != 0) {

		dev_err(dev, "Unsupported cycle type\n");
		retval = -EINVAL;
		goto err_aspace;
	}

	if ((vme_attr->cycle & ~(VME_SCT | VME_BLT | VME_SUPER | VME_USER |
		VME_PROG | VME_DATA)) != 0) {

		dev_err(dev, "Unsupported cycle type\n");
		retval = -EINVAL;
		goto err_cycle;
	}

	/* Check to see if we can fulfill source and destination */
	if (!(((src->type == VME_DMA_PCI) && (dest->type == VME_DMA_VME)) ||
		((src->type == VME_DMA_VME) && (dest->type == VME_DMA_PCI)))) {

		dev_err(dev, "Cannot perform transfer with this "
			"source-destination combination\n");
		retval = -EINVAL;
		goto err_direct;
	}

	/* Setup cycle types */
	if (vme_attr->cycle & VME_BLT)
		entry->descriptor.dctl |= CA91CX42_DCTL_VCT_BLT;

	/* Setup data width */
	switch (vme_attr->dwidth) {
	case VME_D8:
		entry->descriptor.dctl |= CA91CX42_DCTL_VDW_D8;
		break;
	case VME_D16:
		entry->descriptor.dctl |= CA91CX42_DCTL_VDW_D16;
		break;
	case VME_D32:
		entry->descriptor.dctl |= CA91CX42_DCTL_VDW_D32;
		break;
	case VME_D64:
		entry->descriptor.dctl |= CA91CX42_DCTL_VDW_D64;
		break;
	default:
		dev_err(dev, "Invalid data width\n");
		return -EINVAL;
	}

	/* Setup address space */
	switch (vme_attr->aspace) {
	case VME_A16:
		entry->descriptor.dctl |= CA91CX42_DCTL_VAS_A16;
		break;
	case VME_A24:
		entry->descriptor.dctl |= CA91CX42_DCTL_VAS_A24;
		break;
	case VME_A32:
		entry->descriptor.dctl |= CA91CX42_DCTL_VAS_A32;
		break;
	case VME_USER1:
		entry->descriptor.dctl |= CA91CX42_DCTL_VAS_USER1;
		break;
	case VME_USER2:
		entry->descriptor.dctl |= CA91CX42_DCTL_VAS_USER2;
		break;
	default:
		dev_err(dev, "Invalid address space\n");
		return -EINVAL;
		break;
	}

	if (vme_attr->cycle & VME_SUPER)
		entry->descriptor.dctl |= CA91CX42_DCTL_SUPER_SUPR;
	if (vme_attr->cycle & VME_PROG)
		entry->descriptor.dctl |= CA91CX42_DCTL_PGM_PGM;

	entry->descriptor.dtbc = count;
	entry->descriptor.dla = pci_attr->address;
	entry->descriptor.dva = vme_attr->address;
	entry->descriptor.dcpp = CA91CX42_DCPP_NULL;

	/* Add to list */
	list_add_tail(&entry->list, &list->entries);

	/* Fill out previous descriptors "Next Address" */
	if (entry->list.prev != &list->entries) {
		prev = list_entry(entry->list.prev, struct ca91cx42_dma_entry,
			list);
		/* We need the bus address for the pointer */
		desc_ptr = virt_to_bus(&entry->descriptor);
		prev->descriptor.dcpp = desc_ptr & ~CA91CX42_DCPP_M;
	}

	return 0;

err_cycle:
err_aspace:
err_direct:
err_align:
	kfree(entry);
err_mem:
	return retval;
}

static int ca91cx42_dma_busy(struct vme_bridge *ca91cx42_bridge)
{
	u32 tmp;
	struct ca91cx42_driver *bridge;

	bridge = ca91cx42_bridge->driver_priv;

	tmp = ioread32(bridge->base + DGCS);

	if (tmp & CA91CX42_DGCS_ACT)
		return 0;
	else
		return 1;
}

static int ca91cx42_dma_list_exec(struct vme_dma_list *list)
{
	struct vme_dma_resource *ctrlr;
	struct ca91cx42_dma_entry *entry;
	int retval = 0;
	dma_addr_t bus_addr;
	u32 val;
	struct device *dev;
	struct ca91cx42_driver *bridge;

	ctrlr = list->parent;

	bridge = ctrlr->parent->driver_priv;
	dev = ctrlr->parent->parent;

	mutex_lock(&ctrlr->mtx);

	if (!(list_empty(&ctrlr->running))) {
		/*
		 * XXX We have an active DMA transfer and currently haven't
		 *     sorted out the mechanism for "pending" DMA transfers.
		 *     Return busy.
		 */
		/* Need to add to pending here */
		mutex_unlock(&ctrlr->mtx);
		return -EBUSY;
	} else {
		list_add(&list->list, &ctrlr->running);
	}

	/* Get first bus address and write into registers */
	entry = list_first_entry(&list->entries, struct ca91cx42_dma_entry,
		list);

	bus_addr = virt_to_bus(&entry->descriptor);

	mutex_unlock(&ctrlr->mtx);

	iowrite32(0, bridge->base + DTBC);
	iowrite32(bus_addr & ~CA91CX42_DCPP_M, bridge->base + DCPP);

	/* Start the operation */
	val = ioread32(bridge->base + DGCS);

	/* XXX Could set VMEbus On and Off Counters here */
	val &= (CA91CX42_DGCS_VON_M | CA91CX42_DGCS_VOFF_M);

	val |= (CA91CX42_DGCS_CHAIN | CA91CX42_DGCS_STOP | CA91CX42_DGCS_HALT |
		CA91CX42_DGCS_DONE | CA91CX42_DGCS_LERR | CA91CX42_DGCS_VERR |
		CA91CX42_DGCS_PERR);

	iowrite32(val, bridge->base + DGCS);

	val |= CA91CX42_DGCS_GO;

	iowrite32(val, bridge->base + DGCS);

	wait_event_interruptible(bridge->dma_queue,
		ca91cx42_dma_busy(ctrlr->parent));

	/*
	 * Read status register, this register is valid until we kick off a
	 * new transfer.
	 */
	val = ioread32(bridge->base + DGCS);

	if (val & (CA91CX42_DGCS_LERR | CA91CX42_DGCS_VERR |
		CA91CX42_DGCS_PERR)) {

		dev_err(dev, "ca91c042: DMA Error. DGCS=%08X\n", val);
		val = ioread32(bridge->base + DCTL);
	}

	/* Remove list from running list */
	mutex_lock(&ctrlr->mtx);
	list_del(&list->list);
	mutex_unlock(&ctrlr->mtx);

	return retval;

}

static int ca91cx42_dma_list_empty(struct vme_dma_list *list)
{
	struct list_head *pos, *temp;
	struct ca91cx42_dma_entry *entry;

	/* detach and free each entry */
	list_for_each_safe(pos, temp, &list->entries) {
		list_del(pos);
		entry = list_entry(pos, struct ca91cx42_dma_entry, list);
		kfree(entry);
	}

	return 0;
}

/*
 * All 4 location monitors reside at the same base - this is therefore a
 * system wide configuration.
 *
 * This does not enable the LM monitor - that should be done when the first
 * callback is attached and disabled when the last callback is removed.
 */
static int ca91cx42_lm_set(struct vme_lm_resource *lm,
	unsigned long long lm_base, u32 aspace, u32 cycle)
{
	u32 temp_base, lm_ctl = 0;
	int i;
	struct ca91cx42_driver *bridge;
	struct device *dev;

	bridge = lm->parent->driver_priv;
	dev = lm->parent->parent;

	/* Check the alignment of the location monitor */
	temp_base = (u32)lm_base;
	if (temp_base & 0xffff) {
		dev_err(dev, "Location monitor must be aligned to 64KB "
			"boundary");
		return -EINVAL;
	}

	mutex_lock(&lm->mtx);

	/* If we already have a callback attached, we can't move it! */
	for (i = 0; i < lm->monitors; i++) {
		if (bridge->lm_callback[i] != NULL) {
			mutex_unlock(&lm->mtx);
			dev_err(dev, "Location monitor callback attached, "
				"can't reset\n");
			return -EBUSY;
		}
	}

	switch (aspace) {
	case VME_A16:
		lm_ctl |= CA91CX42_LM_CTL_AS_A16;
		break;
	case VME_A24:
		lm_ctl |= CA91CX42_LM_CTL_AS_A24;
		break;
	case VME_A32:
		lm_ctl |= CA91CX42_LM_CTL_AS_A32;
		break;
	default:
		mutex_unlock(&lm->mtx);
		dev_err(dev, "Invalid address space\n");
		return -EINVAL;
		break;
	}

	if (cycle & VME_SUPER)
		lm_ctl |= CA91CX42_LM_CTL_SUPR;
	if (cycle & VME_USER)
		lm_ctl |= CA91CX42_LM_CTL_NPRIV;
	if (cycle & VME_PROG)
		lm_ctl |= CA91CX42_LM_CTL_PGM;
	if (cycle & VME_DATA)
		lm_ctl |= CA91CX42_LM_CTL_DATA;

	iowrite32(lm_base, bridge->base + LM_BS);
	iowrite32(lm_ctl, bridge->base + LM_CTL);

	mutex_unlock(&lm->mtx);

	return 0;
}

/* Get configuration of the callback monitor and return whether it is enabled
 * or disabled.
 */
static int ca91cx42_lm_get(struct vme_lm_resource *lm,
	unsigned long long *lm_base, u32 *aspace, u32 *cycle)
{
	u32 lm_ctl, enabled = 0;
	struct ca91cx42_driver *bridge;

	bridge = lm->parent->driver_priv;

	mutex_lock(&lm->mtx);

	*lm_base = (unsigned long long)ioread32(bridge->base + LM_BS);
	lm_ctl = ioread32(bridge->base + LM_CTL);

	if (lm_ctl & CA91CX42_LM_CTL_EN)
		enabled = 1;

	if ((lm_ctl & CA91CX42_LM_CTL_AS_M) == CA91CX42_LM_CTL_AS_A16)
		*aspace = VME_A16;
	if ((lm_ctl & CA91CX42_LM_CTL_AS_M) == CA91CX42_LM_CTL_AS_A24)
		*aspace = VME_A24;
	if ((lm_ctl & CA91CX42_LM_CTL_AS_M) == CA91CX42_LM_CTL_AS_A32)
		*aspace = VME_A32;

	*cycle = 0;
	if (lm_ctl & CA91CX42_LM_CTL_SUPR)
		*cycle |= VME_SUPER;
	if (lm_ctl & CA91CX42_LM_CTL_NPRIV)
		*cycle |= VME_USER;
	if (lm_ctl & CA91CX42_LM_CTL_PGM)
		*cycle |= VME_PROG;
	if (lm_ctl & CA91CX42_LM_CTL_DATA)
		*cycle |= VME_DATA;

	mutex_unlock(&lm->mtx);

	return enabled;
}

/*
 * Attach a callback to a specific location monitor.
 *
 * Callback will be passed the monitor triggered.
 */
static int ca91cx42_lm_attach(struct vme_lm_resource *lm, int monitor,
	void (*callback)(int))
{
	u32 lm_ctl, tmp;
	struct ca91cx42_driver *bridge;
	struct device *dev;

	bridge = lm->parent->driver_priv;
	dev = lm->parent->parent;

	mutex_lock(&lm->mtx);

	/* Ensure that the location monitor is configured - need PGM or DATA */
	lm_ctl = ioread32(bridge->base + LM_CTL);
	if ((lm_ctl & (CA91CX42_LM_CTL_PGM | CA91CX42_LM_CTL_DATA)) == 0) {
		mutex_unlock(&lm->mtx);
		dev_err(dev, "Location monitor not properly configured\n");
		return -EINVAL;
	}

	/* Check that a callback isn't already attached */
	if (bridge->lm_callback[monitor] != NULL) {
		mutex_unlock(&lm->mtx);
		dev_err(dev, "Existing callback attached\n");
		return -EBUSY;
	}

	/* Attach callback */
	bridge->lm_callback[monitor] = callback;

	/* Enable Location Monitor interrupt */
	tmp = ioread32(bridge->base + LINT_EN);
	tmp |= CA91CX42_LINT_LM[monitor];
	iowrite32(tmp, bridge->base + LINT_EN);

	/* Ensure that global Location Monitor Enable set */
	if ((lm_ctl & CA91CX42_LM_CTL_EN) == 0) {
		lm_ctl |= CA91CX42_LM_CTL_EN;
		iowrite32(lm_ctl, bridge->base + LM_CTL);
	}

	mutex_unlock(&lm->mtx);

	return 0;
}

/*
 * Detach a callback function forn a specific location monitor.
 */
static int ca91cx42_lm_detach(struct vme_lm_resource *lm, int monitor)
{
	u32 tmp;
	struct ca91cx42_driver *bridge;

	bridge = lm->parent->driver_priv;

	mutex_lock(&lm->mtx);

	/* Disable Location Monitor and ensure previous interrupts are clear */
	tmp = ioread32(bridge->base + LINT_EN);
	tmp &= ~CA91CX42_LINT_LM[monitor];
	iowrite32(tmp, bridge->base + LINT_EN);

	iowrite32(CA91CX42_LINT_LM[monitor],
		 bridge->base + LINT_STAT);

	/* Detach callback */
	bridge->lm_callback[monitor] = NULL;

	/* If all location monitors disabled, disable global Location Monitor */
	if ((tmp & (CA91CX42_LINT_LM0 | CA91CX42_LINT_LM1 | CA91CX42_LINT_LM2 |
			CA91CX42_LINT_LM3)) == 0) {
		tmp = ioread32(bridge->base + LM_CTL);
		tmp &= ~CA91CX42_LM_CTL_EN;
		iowrite32(tmp, bridge->base + LM_CTL);
	}

	mutex_unlock(&lm->mtx);

	return 0;
}

static int ca91cx42_slot_get(struct vme_bridge *ca91cx42_bridge)
{
	u32 slot = 0;
	struct ca91cx42_driver *bridge;

	bridge = ca91cx42_bridge->driver_priv;

	if (!geoid) {
		slot = ioread32(bridge->base + VCSR_BS);
		slot = ((slot & CA91CX42_VCSR_BS_SLOT_M) >> 27);
	} else
		slot = geoid;

	return (int)slot;

}

static void *ca91cx42_alloc_consistent(struct device *parent, size_t size,
	dma_addr_t *dma)
{
	struct pci_dev *pdev;

	/* Find pci_dev container of dev */
	pdev = container_of(parent, struct pci_dev, dev);

	return pci_alloc_consistent(pdev, size, dma);
}

static void ca91cx42_free_consistent(struct device *parent, size_t size,
	void *vaddr, dma_addr_t dma)
{
	struct pci_dev *pdev;

	/* Find pci_dev container of dev */
	pdev = container_of(parent, struct pci_dev, dev);

	pci_free_consistent(pdev, size, vaddr, dma);
}

/*
 * Configure CR/CSR space
 *
 * Access to the CR/CSR can be configured at power-up. The location of the
 * CR/CSR registers in the CR/CSR address space is determined by the boards
 * Auto-ID or Geographic address. This function ensures that the window is
 * enabled at an offset consistent with the boards geopgraphic address.
 */
static int ca91cx42_crcsr_init(struct vme_bridge *ca91cx42_bridge,
	struct pci_dev *pdev)
{
	unsigned int crcsr_addr;
	int tmp, slot;
	struct ca91cx42_driver *bridge;

	bridge = ca91cx42_bridge->driver_priv;

	slot = ca91cx42_slot_get(ca91cx42_bridge);

	/* Write CSR Base Address if slot ID is supplied as a module param */
	if (geoid)
		iowrite32(geoid << 27, bridge->base + VCSR_BS);

	dev_info(&pdev->dev, "CR/CSR Offset: %d\n", slot);
	if (slot == 0) {
		dev_err(&pdev->dev, "Slot number is unset, not configuring "
			"CR/CSR space\n");
		return -EINVAL;
	}

	/* Allocate mem for CR/CSR image */
	bridge->crcsr_kernel = pci_alloc_consistent(pdev, VME_CRCSR_BUF_SIZE,
		&bridge->crcsr_bus);
	if (bridge->crcsr_kernel == NULL) {
		dev_err(&pdev->dev, "Failed to allocate memory for CR/CSR "
			"image\n");
		return -ENOMEM;
	}

	memset(bridge->crcsr_kernel, 0, VME_CRCSR_BUF_SIZE);

	crcsr_addr = slot * (512 * 1024);
	iowrite32(bridge->crcsr_bus - crcsr_addr, bridge->base + VCSR_TO);

	tmp = ioread32(bridge->base + VCSR_CTL);
	tmp |= CA91CX42_VCSR_CTL_EN;
	iowrite32(tmp, bridge->base + VCSR_CTL);

	return 0;
}

static void ca91cx42_crcsr_exit(struct vme_bridge *ca91cx42_bridge,
	struct pci_dev *pdev)
{
	u32 tmp;
	struct ca91cx42_driver *bridge;

	bridge = ca91cx42_bridge->driver_priv;

	/* Turn off CR/CSR space */
	tmp = ioread32(bridge->base + VCSR_CTL);
	tmp &= ~CA91CX42_VCSR_CTL_EN;
	iowrite32(tmp, bridge->base + VCSR_CTL);

	/* Free image */
	iowrite32(0, bridge->base + VCSR_TO);

	pci_free_consistent(pdev, VME_CRCSR_BUF_SIZE, bridge->crcsr_kernel,
		bridge->crcsr_bus);
}

static int ca91cx42_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int retval, i;
	u32 data;
	struct list_head *pos = NULL, *n;
	struct vme_bridge *ca91cx42_bridge;
	struct ca91cx42_driver *ca91cx42_device;
	struct vme_master_resource *master_image;
	struct vme_slave_resource *slave_image;
	struct vme_dma_resource *dma_ctrlr;
	struct vme_lm_resource *lm;

	/* We want to support more than one of each bridge so we need to
	 * dynamically allocate the bridge structure
	 */
	ca91cx42_bridge = kzalloc(sizeof(struct vme_bridge), GFP_KERNEL);

	if (ca91cx42_bridge == NULL) {
		dev_err(&pdev->dev, "Failed to allocate memory for device "
			"structure\n");
		retval = -ENOMEM;
		goto err_struct;
	}

	ca91cx42_device = kzalloc(sizeof(struct ca91cx42_driver), GFP_KERNEL);

	if (ca91cx42_device == NULL) {
		dev_err(&pdev->dev, "Failed to allocate memory for device "
			"structure\n");
		retval = -ENOMEM;
		goto err_driver;
	}

	ca91cx42_bridge->driver_priv = ca91cx42_device;

	/* Enable the device */
	retval = pci_enable_device(pdev);
	if (retval) {
		dev_err(&pdev->dev, "Unable to enable device\n");
		goto err_enable;
	}

	/* Map Registers */
	retval = pci_request_regions(pdev, driver_name);
	if (retval) {
		dev_err(&pdev->dev, "Unable to reserve resources\n");
		goto err_resource;
	}

	/* map registers in BAR 0 */
	ca91cx42_device->base = ioremap_nocache(pci_resource_start(pdev, 0),
		4096);
	if (!ca91cx42_device->base) {
		dev_err(&pdev->dev, "Unable to remap CRG region\n");
		retval = -EIO;
		goto err_remap;
	}

	/* Check to see if the mapping worked out */
	data = ioread32(ca91cx42_device->base + CA91CX42_PCI_ID) & 0x0000FFFF;
	if (data != PCI_VENDOR_ID_TUNDRA) {
		dev_err(&pdev->dev, "PCI_ID check failed\n");
		retval = -EIO;
		goto err_test;
	}

	/* Initialize wait queues & mutual exclusion flags */
	init_waitqueue_head(&ca91cx42_device->dma_queue);
	init_waitqueue_head(&ca91cx42_device->iack_queue);
	mutex_init(&ca91cx42_device->vme_int);
	mutex_init(&ca91cx42_device->vme_rmw);

	ca91cx42_bridge->parent = &pdev->dev;
	strcpy(ca91cx42_bridge->name, driver_name);

	/* Setup IRQ */
	retval = ca91cx42_irq_init(ca91cx42_bridge);
	if (retval != 0) {
		dev_err(&pdev->dev, "Chip Initialization failed.\n");
		goto err_irq;
	}

	/* Add master windows to list */
	INIT_LIST_HEAD(&ca91cx42_bridge->master_resources);
	for (i = 0; i < CA91C142_MAX_MASTER; i++) {
		master_image = kmalloc(sizeof(struct vme_master_resource),
			GFP_KERNEL);
		if (master_image == NULL) {
			dev_err(&pdev->dev, "Failed to allocate memory for "
			"master resource structure\n");
			retval = -ENOMEM;
			goto err_master;
		}
		master_image->parent = ca91cx42_bridge;
		spin_lock_init(&master_image->lock);
		master_image->locked = 0;
		master_image->number = i;
		master_image->address_attr = VME_A16 | VME_A24 | VME_A32 |
			VME_CRCSR | VME_USER1 | VME_USER2;
		master_image->cycle_attr = VME_SCT | VME_BLT | VME_MBLT |
			VME_SUPER | VME_USER | VME_PROG | VME_DATA;
		master_image->width_attr = VME_D8 | VME_D16 | VME_D32 | VME_D64;
		memset(&master_image->bus_resource, 0,
			sizeof(struct resource));
		master_image->kern_base  = NULL;
		list_add_tail(&master_image->list,
			&ca91cx42_bridge->master_resources);
	}

	/* Add slave windows to list */
	INIT_LIST_HEAD(&ca91cx42_bridge->slave_resources);
	for (i = 0; i < CA91C142_MAX_SLAVE; i++) {
		slave_image = kmalloc(sizeof(struct vme_slave_resource),
			GFP_KERNEL);
		if (slave_image == NULL) {
			dev_err(&pdev->dev, "Failed to allocate memory for "
			"slave resource structure\n");
			retval = -ENOMEM;
			goto err_slave;
		}
		slave_image->parent = ca91cx42_bridge;
		mutex_init(&slave_image->mtx);
		slave_image->locked = 0;
		slave_image->number = i;
		slave_image->address_attr = VME_A24 | VME_A32 | VME_USER1 |
			VME_USER2;

		/* Only windows 0 and 4 support A16 */
		if (i == 0 || i == 4)
			slave_image->address_attr |= VME_A16;

		slave_image->cycle_attr = VME_SCT | VME_BLT | VME_MBLT |
			VME_SUPER | VME_USER | VME_PROG | VME_DATA;
		list_add_tail(&slave_image->list,
			&ca91cx42_bridge->slave_resources);
	}

	/* Add dma engines to list */
	INIT_LIST_HEAD(&ca91cx42_bridge->dma_resources);
	for (i = 0; i < CA91C142_MAX_DMA; i++) {
		dma_ctrlr = kmalloc(sizeof(struct vme_dma_resource),
			GFP_KERNEL);
		if (dma_ctrlr == NULL) {
			dev_err(&pdev->dev, "Failed to allocate memory for "
			"dma resource structure\n");
			retval = -ENOMEM;
			goto err_dma;
		}
		dma_ctrlr->parent = ca91cx42_bridge;
		mutex_init(&dma_ctrlr->mtx);
		dma_ctrlr->locked = 0;
		dma_ctrlr->number = i;
		dma_ctrlr->route_attr = VME_DMA_VME_TO_MEM |
			VME_DMA_MEM_TO_VME;
		INIT_LIST_HEAD(&dma_ctrlr->pending);
		INIT_LIST_HEAD(&dma_ctrlr->running);
		list_add_tail(&dma_ctrlr->list,
			&ca91cx42_bridge->dma_resources);
	}

	/* Add location monitor to list */
	INIT_LIST_HEAD(&ca91cx42_bridge->lm_resources);
	lm = kmalloc(sizeof(struct vme_lm_resource), GFP_KERNEL);
	if (lm == NULL) {
		dev_err(&pdev->dev, "Failed to allocate memory for "
		"location monitor resource structure\n");
		retval = -ENOMEM;
		goto err_lm;
	}
	lm->parent = ca91cx42_bridge;
	mutex_init(&lm->mtx);
	lm->locked = 0;
	lm->number = 1;
	lm->monitors = 4;
	list_add_tail(&lm->list, &ca91cx42_bridge->lm_resources);

	ca91cx42_bridge->slave_get = ca91cx42_slave_get;
	ca91cx42_bridge->slave_set = ca91cx42_slave_set;
	ca91cx42_bridge->master_get = ca91cx42_master_get;
	ca91cx42_bridge->master_set = ca91cx42_master_set;
	ca91cx42_bridge->master_read = ca91cx42_master_read;
	ca91cx42_bridge->master_write = ca91cx42_master_write;
	ca91cx42_bridge->master_rmw = ca91cx42_master_rmw;
	ca91cx42_bridge->dma_list_add = ca91cx42_dma_list_add;
	ca91cx42_bridge->dma_list_exec = ca91cx42_dma_list_exec;
	ca91cx42_bridge->dma_list_empty = ca91cx42_dma_list_empty;
	ca91cx42_bridge->irq_set = ca91cx42_irq_set;
	ca91cx42_bridge->irq_generate = ca91cx42_irq_generate;
	ca91cx42_bridge->lm_set = ca91cx42_lm_set;
	ca91cx42_bridge->lm_get = ca91cx42_lm_get;
	ca91cx42_bridge->lm_attach = ca91cx42_lm_attach;
	ca91cx42_bridge->lm_detach = ca91cx42_lm_detach;
	ca91cx42_bridge->slot_get = ca91cx42_slot_get;
	ca91cx42_bridge->alloc_consistent = ca91cx42_alloc_consistent;
	ca91cx42_bridge->free_consistent = ca91cx42_free_consistent;

	data = ioread32(ca91cx42_device->base + MISC_CTL);
	dev_info(&pdev->dev, "Board is%s the VME system controller\n",
		(data & CA91CX42_MISC_CTL_SYSCON) ? "" : " not");
	dev_info(&pdev->dev, "Slot ID is %d\n",
		ca91cx42_slot_get(ca91cx42_bridge));

	if (ca91cx42_crcsr_init(ca91cx42_bridge, pdev))
		dev_err(&pdev->dev, "CR/CSR configuration failed.\n");

	/* Need to save ca91cx42_bridge pointer locally in link list for use in
	 * ca91cx42_remove()
	 */
	retval = vme_register_bridge(ca91cx42_bridge);
	if (retval != 0) {
		dev_err(&pdev->dev, "Chip Registration failed.\n");
		goto err_reg;
	}

	pci_set_drvdata(pdev, ca91cx42_bridge);

	return 0;

err_reg:
	ca91cx42_crcsr_exit(ca91cx42_bridge, pdev);
err_lm:
	/* resources are stored in link list */
	list_for_each_safe(pos, n, &ca91cx42_bridge->lm_resources) {
		lm = list_entry(pos, struct vme_lm_resource, list);
		list_del(pos);
		kfree(lm);
	}
err_dma:
	/* resources are stored in link list */
	list_for_each_safe(pos, n, &ca91cx42_bridge->dma_resources) {
		dma_ctrlr = list_entry(pos, struct vme_dma_resource, list);
		list_del(pos);
		kfree(dma_ctrlr);
	}
err_slave:
	/* resources are stored in link list */
	list_for_each_safe(pos, n, &ca91cx42_bridge->slave_resources) {
		slave_image = list_entry(pos, struct vme_slave_resource, list);
		list_del(pos);
		kfree(slave_image);
	}
err_master:
	/* resources are stored in link list */
	list_for_each_safe(pos, n, &ca91cx42_bridge->master_resources) {
		master_image = list_entry(pos, struct vme_master_resource,
			list);
		list_del(pos);
		kfree(master_image);
	}

	ca91cx42_irq_exit(ca91cx42_device, pdev);
err_irq:
err_test:
	iounmap(ca91cx42_device->base);
err_remap:
	pci_release_regions(pdev);
err_resource:
	pci_disable_device(pdev);
err_enable:
	kfree(ca91cx42_device);
err_driver:
	kfree(ca91cx42_bridge);
err_struct:
	return retval;

}

static void ca91cx42_remove(struct pci_dev *pdev)
{
	struct list_head *pos = NULL, *n;
	struct vme_master_resource *master_image;
	struct vme_slave_resource *slave_image;
	struct vme_dma_resource *dma_ctrlr;
	struct vme_lm_resource *lm;
	struct ca91cx42_driver *bridge;
	struct vme_bridge *ca91cx42_bridge = pci_get_drvdata(pdev);

	bridge = ca91cx42_bridge->driver_priv;


	/* Turn off Ints */
	iowrite32(0, bridge->base + LINT_EN);

	/* Turn off the windows */
	iowrite32(0x00800000, bridge->base + LSI0_CTL);
	iowrite32(0x00800000, bridge->base + LSI1_CTL);
	iowrite32(0x00800000, bridge->base + LSI2_CTL);
	iowrite32(0x00800000, bridge->base + LSI3_CTL);
	iowrite32(0x00800000, bridge->base + LSI4_CTL);
	iowrite32(0x00800000, bridge->base + LSI5_CTL);
	iowrite32(0x00800000, bridge->base + LSI6_CTL);
	iowrite32(0x00800000, bridge->base + LSI7_CTL);
	iowrite32(0x00F00000, bridge->base + VSI0_CTL);
	iowrite32(0x00F00000, bridge->base + VSI1_CTL);
	iowrite32(0x00F00000, bridge->base + VSI2_CTL);
	iowrite32(0x00F00000, bridge->base + VSI3_CTL);
	iowrite32(0x00F00000, bridge->base + VSI4_CTL);
	iowrite32(0x00F00000, bridge->base + VSI5_CTL);
	iowrite32(0x00F00000, bridge->base + VSI6_CTL);
	iowrite32(0x00F00000, bridge->base + VSI7_CTL);

	vme_unregister_bridge(ca91cx42_bridge);

	ca91cx42_crcsr_exit(ca91cx42_bridge, pdev);

	/* resources are stored in link list */
	list_for_each_safe(pos, n, &ca91cx42_bridge->lm_resources) {
		lm = list_entry(pos, struct vme_lm_resource, list);
		list_del(pos);
		kfree(lm);
	}

	/* resources are stored in link list */
	list_for_each_safe(pos, n, &ca91cx42_bridge->dma_resources) {
		dma_ctrlr = list_entry(pos, struct vme_dma_resource, list);
		list_del(pos);
		kfree(dma_ctrlr);
	}

	/* resources are stored in link list */
	list_for_each_safe(pos, n, &ca91cx42_bridge->slave_resources) {
		slave_image = list_entry(pos, struct vme_slave_resource, list);
		list_del(pos);
		kfree(slave_image);
	}

	/* resources are stored in link list */
	list_for_each_safe(pos, n, &ca91cx42_bridge->master_resources) {
		master_image = list_entry(pos, struct vme_master_resource,
			list);
		list_del(pos);
		kfree(master_image);
	}

	ca91cx42_irq_exit(bridge, pdev);

	iounmap(bridge->base);

	pci_release_regions(pdev);

	pci_disable_device(pdev);

	kfree(ca91cx42_bridge);
}

module_pci_driver(ca91cx42_driver);

MODULE_PARM_DESC(geoid, "Override geographical addressing");
module_param(geoid, int, 0);

MODULE_DESCRIPTION("VME driver for the Tundra Universe II VME bridge");
MODULE_LICENSE("GPL");
