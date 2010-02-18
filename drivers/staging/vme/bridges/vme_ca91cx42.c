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
#include <asm/time.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include "../vme.h"
#include "../vme_bridge.h"
#include "vme_ca91cx42.h"

static int __init ca91cx42_init(void);
static int ca91cx42_probe(struct pci_dev *, const struct pci_device_id *);
static void ca91cx42_remove(struct pci_dev *);
static void __exit ca91cx42_exit(void);

/* Module parameters */
static int geoid;

static char driver_name[] = "vme_ca91cx42";

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
	wake_up(&(bridge->dma_queue));

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
	wake_up(&(bridge->mbox_queue));

	return CA91CX42_LINT_MBOX;
}

static u32 ca91cx42_IACK_irqhandler(struct ca91cx42_driver *bridge)
{
	wake_up(&(bridge->iack_queue));

	return CA91CX42_LINT_SW_IACK;
}

#if 0
int ca91cx42_bus_error_chk(int clrflag)
{
	int tmp;
	tmp = ioread32(bridge->base + PCI_COMMAND);
	if (tmp & 0x08000000) {	/* S_TA is Set */
		if (clrflag)
			iowrite32(tmp | 0x08000000,
			       bridge->base + PCI_COMMAND);
		return 1;
	}
	return 0;
}
#endif

static u32 ca91cx42_VERR_irqhandler(struct ca91cx42_driver *bridge)
{
	int val;

	val = ioread32(bridge->base + DGCS);

	if (!(val & 0x00000800)) {
		printk(KERN_ERR "ca91c042: ca91cx42_VERR_irqhandler DMA Read "
			"Error DGCS=%08X\n", val);
	}

	return CA91CX42_LINT_VERR;
}

static u32 ca91cx42_LERR_irqhandler(struct ca91cx42_driver *bridge)
{
	int val;

	val = ioread32(bridge->base + DGCS);

	if (!(val & 0x00000800)) {
		printk(KERN_ERR "ca91c042: ca91cx42_LERR_irqhandler DMA Read "
			"Error DGCS=%08X\n", val);

	}

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
		serviced |= ca91cx42_VERR_irqhandler(bridge);
	if (stat & CA91CX42_LINT_LERR)
		serviced |= ca91cx42_LERR_irqhandler(bridge);
	if (stat & (CA91CX42_LINT_VIRQ1 | CA91CX42_LINT_VIRQ2 |
			CA91CX42_LINT_VIRQ3 | CA91CX42_LINT_VIRQ4 |
			CA91CX42_LINT_VIRQ5 | CA91CX42_LINT_VIRQ6 |
			CA91CX42_LINT_VIRQ7))
		serviced |= ca91cx42_VIRQ_irqhandler(ca91cx42_bridge, stat);

	/* Clear serviced interrupts */
	iowrite32(stat, bridge->base + LINT_STAT);

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
	INIT_LIST_HEAD(&(ca91cx42_bridge->vme_errors));

	mutex_init(&(ca91cx42_bridge->irq_mtx));

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
	/* Disable interrupts from PCI to VME */
	iowrite32(0, bridge->base + VINT_EN);

	/* Disable PCI interrupts */
	iowrite32(0, bridge->base + LINT_EN);
	/* Clear Any Pending PCI Interrupts */
	iowrite32(0x00FFFFFF, bridge->base + LINT_STAT);

	free_irq(pdev->irq, pdev);
}

/*
 * Set up an VME interrupt
 */
void ca91cx42_irq_set(struct vme_bridge *ca91cx42_bridge, int level, int state,
	int sync)

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

int ca91cx42_irq_generate(struct vme_bridge *ca91cx42_bridge, int level,
	int statid)
{
	u32 tmp;
	struct ca91cx42_driver *bridge;

	bridge = ca91cx42_bridge->driver_priv;

	/* Universe can only generate even vectors */
	if (statid & 1)
		return -EINVAL;

	mutex_lock(&(bridge->vme_int));

	tmp = ioread32(bridge->base + VINT_EN);

	/* Set Status/ID */
	iowrite32(statid << 24, bridge->base + STATID);

	/* Assert VMEbus IRQ */
	tmp = tmp | (1 << (level + 24));
	iowrite32(tmp, bridge->base + VINT_EN);

	/* Wait for IACK */
	wait_event_interruptible(bridge->iack_queue, 0);

	/* Return interrupt to low state */
	tmp = ioread32(bridge->base + VINT_EN);
	tmp = tmp & ~(1 << (level + 24));
	iowrite32(tmp, bridge->base + VINT_EN);

	mutex_unlock(&(bridge->vme_int));

	return 0;
}

int ca91cx42_slave_set(struct vme_slave_resource *image, int enabled,
	unsigned long long vme_base, unsigned long long size,
	dma_addr_t pci_base, vme_address_t aspace, vme_cycle_t cycle)
{
	unsigned int i, addr = 0, granularity = 0;
	unsigned int temp_ctl = 0;
	unsigned int vme_bound, pci_offset;
	struct ca91cx42_driver *bridge;

	bridge = image->parent->driver_priv;

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
		printk(KERN_ERR "Invalid address space\n");
		return -EINVAL;
		break;
	}

	/*
	 * Bound address is a valid address for the window, adjust
	 * accordingly
	 */
	vme_bound = vme_base + size - granularity;
	pci_offset = pci_base - vme_base;

	/* XXX Need to check that vme_base, vme_bound and pci_offset aren't
	 * too big for registers
	 */

	if ((i == 0) || (i == 4))
		granularity = 0x1000;
	else
		granularity = 0x10000;

	if (vme_base & (granularity - 1)) {
		printk(KERN_ERR "Invalid VME base alignment\n");
		return -EINVAL;
	}
	if (vme_bound & (granularity - 1)) {
		printk(KERN_ERR "Invalid VME bound alignment\n");
		return -EINVAL;
	}
	if (pci_offset & (granularity - 1)) {
		printk(KERN_ERR "Invalid PCI Offset alignment\n");
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

/* XXX Prefetch stuff currently unsupported */
#if 0
	if (vmeIn->wrPostEnable)
		temp_ctl |= CA91CX42_VSI_CTL_PWEN;
	if (vmeIn->prefetchEnable)
		temp_ctl |= CA91CX42_VSI_CTL_PREN;
	if (vmeIn->rmwLock)
		temp_ctl |= CA91CX42_VSI_CTL_LLRMW;
	if (vmeIn->data64BitCapable)
		temp_ctl |= CA91CX42_VSI_CTL_LD64EN;
#endif

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

int ca91cx42_slave_get(struct vme_slave_resource *image, int *enabled,
	unsigned long long *vme_base, unsigned long long *size,
	dma_addr_t *pci_base, vme_address_t *aspace, vme_cycle_t *cycle)
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
		printk(KERN_ERR "Dev entry NULL\n");
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
		if (image->bus_resource.name != NULL)
			kfree(image->bus_resource.name);
		release_resource(&(image->bus_resource));
		memset(&(image->bus_resource), 0, sizeof(struct resource));
	}

	if (image->bus_resource.name == NULL) {
		image->bus_resource.name = kmalloc(VMENAMSIZ+3, GFP_KERNEL);
		if (image->bus_resource.name == NULL) {
			printk(KERN_ERR "Unable to allocate memory for resource"
				" name\n");
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
		&(image->bus_resource), size, size, PCIBIOS_MIN_MEM,
		0, NULL, NULL);
	if (retval) {
		printk(KERN_ERR "Failed to allocate mem resource for "
			"window %d size 0x%lx start 0x%lx\n",
			image->number, (unsigned long)size,
			(unsigned long)image->bus_resource.start);
		goto err_resource;
	}

	image->kern_base = ioremap_nocache(
		image->bus_resource.start, size);
	if (image->kern_base == NULL) {
		printk(KERN_ERR "Failed to remap resource\n");
		retval = -ENOMEM;
		goto err_remap;
	}

	return 0;

	iounmap(image->kern_base);
	image->kern_base = NULL;
err_remap:
	release_resource(&(image->bus_resource));
err_resource:
	kfree(image->bus_resource.name);
	memset(&(image->bus_resource), 0, sizeof(struct resource));
err_name:
	return retval;
}

/*
 *  * Free and unmap PCI Resource
 *   */
static void ca91cx42_free_resource(struct vme_master_resource *image)
{
	iounmap(image->kern_base);
	image->kern_base = NULL;
	release_resource(&(image->bus_resource));
	kfree(image->bus_resource.name);
	memset(&(image->bus_resource), 0, sizeof(struct resource));
}


int ca91cx42_master_set(struct vme_master_resource *image, int enabled,
	unsigned long long vme_base, unsigned long long size,
	vme_address_t aspace, vme_cycle_t cycle, vme_width_t dwidth)
{
	int retval = 0;
	unsigned int i;
	unsigned int temp_ctl = 0;
	unsigned long long pci_bound, vme_offset, pci_base;
	struct ca91cx42_driver *bridge;

	bridge = image->parent->driver_priv;

	/* Verify input data */
	if (vme_base & 0xFFF) {
		printk(KERN_ERR "Invalid VME Window alignment\n");
		retval = -EINVAL;
		goto err_window;
	}
	if (size & 0xFFF) {
		printk(KERN_ERR "Invalid VME Window alignment\n");
		retval = -EINVAL;
		goto err_window;
	}

	spin_lock(&(image->lock));

	/* XXX We should do this much later, so that we can exit without
	 *     needing to redo the mapping...
	 */
	/*
	 * Let's allocate the resource here rather than further up the stack as
	 * it avoids pushing loads of bus dependant stuff up the stack
	 */
	retval = ca91cx42_alloc_resource(image, size);
	if (retval) {
		spin_unlock(&(image->lock));
		printk(KERN_ERR "Unable to allocate memory for resource "
			"name\n");
		retval = -ENOMEM;
		goto err_res;
	}

	pci_base = (unsigned long long)image->bus_resource.start;

	/*
	 * Bound address is a valid address for the window, adjust
	 * according to window granularity.
	 */
	pci_bound = pci_base + (size - 0x1000);
	vme_offset = vme_base - pci_base;

	i = image->number;

	/* Disable while we are mucking around */
	temp_ctl = ioread32(bridge->base + CA91CX42_LSI_CTL[i]);
	temp_ctl &= ~CA91CX42_LSI_CTL_EN;
	iowrite32(temp_ctl, bridge->base + CA91CX42_LSI_CTL[i]);

/* XXX Prefetch stuff currently unsupported */
#if 0
	if (vmeOut->wrPostEnable)
		temp_ctl |= 0x40000000;
#endif

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
		spin_unlock(&(image->lock));
		printk(KERN_ERR "Invalid data width\n");
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
		spin_unlock(&(image->lock));
		printk(KERN_ERR "Invalid address space\n");
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

	spin_unlock(&(image->lock));
	return 0;

err_aspace:
err_dwidth:
	ca91cx42_free_resource(image);
err_res:
err_window:
	return retval;
}

int __ca91cx42_master_get(struct vme_master_resource *image, int *enabled,
	unsigned long long *vme_base, unsigned long long *size,
	vme_address_t *aspace, vme_cycle_t *cycle, vme_width_t *dwidth)
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
	*size = (pci_bound - pci_base) + 0x1000;

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

/* XXX Prefetch stuff currently unsupported */
#if 0
	if (ctl & 0x40000000)
		vmeOut->wrPostEnable = 1;
#endif

	return 0;
}

int ca91cx42_master_get(struct vme_master_resource *image, int *enabled,
	unsigned long long *vme_base, unsigned long long *size,
	vme_address_t *aspace, vme_cycle_t *cycle, vme_width_t *dwidth)
{
	int retval;

	spin_lock(&(image->lock));

	retval = __ca91cx42_master_get(image, enabled, vme_base, size, aspace,
		cycle, dwidth);

	spin_unlock(&(image->lock));

	return retval;
}

ssize_t ca91cx42_master_read(struct vme_master_resource *image, void *buf,
	size_t count, loff_t offset)
{
	int retval;

	spin_lock(&(image->lock));

	memcpy_fromio(buf, image->kern_base + offset, (unsigned int)count);
	retval = count;

	spin_unlock(&(image->lock));

	return retval;
}

ssize_t ca91cx42_master_write(struct vme_master_resource *image, void *buf,
	size_t count, loff_t offset)
{
	int retval = 0;

	spin_lock(&(image->lock));

	memcpy_toio(image->kern_base + offset, buf, (unsigned int)count);
	retval = count;

	spin_unlock(&(image->lock));

	return retval;
}

int ca91cx42_slot_get(struct vme_bridge *ca91cx42_bridge)
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

static int __init ca91cx42_init(void)
{
	return pci_register_driver(&ca91cx42_driver);
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

/* XXX We may need to set this somehow as the Universe II does not support
 *     geographical addressing.
 */
#if 0
	if (vme_slotnum != -1)
		iowrite32(vme_slotnum << 27, bridge->base + VCSR_BS);
#endif
	slot = ca91cx42_slot_get(ca91cx42_bridge);
	dev_info(&pdev->dev, "CR/CSR Offset: %d\n", slot);
	if (slot == 0) {
		dev_err(&pdev->dev, "Slot number is unset, not configuring "
			"CR/CSR space\n");
		return -EINVAL;
	}

	/* Allocate mem for CR/CSR image */
	bridge->crcsr_kernel = pci_alloc_consistent(pdev, VME_CRCSR_BUF_SIZE,
		&(bridge->crcsr_bus));
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
	struct list_head *pos = NULL;
	struct vme_bridge *ca91cx42_bridge;
	struct ca91cx42_driver *ca91cx42_device;
	struct vme_master_resource *master_image;
	struct vme_slave_resource *slave_image;
#if 0
	struct vme_dma_resource *dma_ctrlr;
#endif
	struct vme_lm_resource *lm;

	/* We want to support more than one of each bridge so we need to
	 * dynamically allocate the bridge structure
	 */
	ca91cx42_bridge = kmalloc(sizeof(struct vme_bridge), GFP_KERNEL);

	if (ca91cx42_bridge == NULL) {
		dev_err(&pdev->dev, "Failed to allocate memory for device "
			"structure\n");
		retval = -ENOMEM;
		goto err_struct;
	}

	memset(ca91cx42_bridge, 0, sizeof(struct vme_bridge));

	ca91cx42_device = kmalloc(sizeof(struct ca91cx42_driver), GFP_KERNEL);

	if (ca91cx42_device == NULL) {
		dev_err(&pdev->dev, "Failed to allocate memory for device "
			"structure\n");
		retval = -ENOMEM;
		goto err_driver;
	}

	memset(ca91cx42_device, 0, sizeof(struct ca91cx42_driver));

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
	init_waitqueue_head(&(ca91cx42_device->dma_queue));
	init_waitqueue_head(&(ca91cx42_device->iack_queue));
	mutex_init(&(ca91cx42_device->vme_int));
	mutex_init(&(ca91cx42_device->vme_rmw));

	ca91cx42_bridge->parent = &(pdev->dev);
	strcpy(ca91cx42_bridge->name, driver_name);

	/* Setup IRQ */
	retval = ca91cx42_irq_init(ca91cx42_bridge);
	if (retval != 0) {
		dev_err(&pdev->dev, "Chip Initialization failed.\n");
		goto err_irq;
	}

	/* Add master windows to list */
	INIT_LIST_HEAD(&(ca91cx42_bridge->master_resources));
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
		spin_lock_init(&(master_image->lock));
		master_image->locked = 0;
		master_image->number = i;
		master_image->address_attr = VME_A16 | VME_A24 | VME_A32 |
			VME_CRCSR | VME_USER1 | VME_USER2;
		master_image->cycle_attr = VME_SCT | VME_BLT | VME_MBLT |
			VME_SUPER | VME_USER | VME_PROG | VME_DATA;
		master_image->width_attr = VME_D8 | VME_D16 | VME_D32 | VME_D64;
		memset(&(master_image->bus_resource), 0,
			sizeof(struct resource));
		master_image->kern_base  = NULL;
		list_add_tail(&(master_image->list),
			&(ca91cx42_bridge->master_resources));
	}

	/* Add slave windows to list */
	INIT_LIST_HEAD(&(ca91cx42_bridge->slave_resources));
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
		mutex_init(&(slave_image->mtx));
		slave_image->locked = 0;
		slave_image->number = i;
		slave_image->address_attr = VME_A24 | VME_A32 | VME_USER1 |
			VME_USER2;

		/* Only windows 0 and 4 support A16 */
		if (i == 0 || i == 4)
			slave_image->address_attr |= VME_A16;

		slave_image->cycle_attr = VME_SCT | VME_BLT | VME_MBLT |
			VME_SUPER | VME_USER | VME_PROG | VME_DATA;
		list_add_tail(&(slave_image->list),
			&(ca91cx42_bridge->slave_resources));
	}
#if 0
	/* Add dma engines to list */
	INIT_LIST_HEAD(&(ca91cx42_bridge->dma_resources));
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
		mutex_init(&(dma_ctrlr->mtx));
		dma_ctrlr->locked = 0;
		dma_ctrlr->number = i;
		dma_ctrlr->route_attr = VME_DMA_VME_TO_MEM |
			VME_DMA_MEM_TO_VME;
		INIT_LIST_HEAD(&(dma_ctrlr->pending));
		INIT_LIST_HEAD(&(dma_ctrlr->running));
		list_add_tail(&(dma_ctrlr->list),
			&(ca91cx42_bridge->dma_resources));
	}
#endif
	/* Add location monitor to list */
	INIT_LIST_HEAD(&(ca91cx42_bridge->lm_resources));
	lm = kmalloc(sizeof(struct vme_lm_resource), GFP_KERNEL);
	if (lm == NULL) {
		dev_err(&pdev->dev, "Failed to allocate memory for "
		"location monitor resource structure\n");
		retval = -ENOMEM;
		goto err_lm;
	}
	lm->parent = ca91cx42_bridge;
	mutex_init(&(lm->mtx));
	lm->locked = 0;
	lm->number = 1;
	lm->monitors = 4;
	list_add_tail(&(lm->list), &(ca91cx42_bridge->lm_resources));

	ca91cx42_bridge->slave_get = ca91cx42_slave_get;
	ca91cx42_bridge->slave_set = ca91cx42_slave_set;
	ca91cx42_bridge->master_get = ca91cx42_master_get;
	ca91cx42_bridge->master_set = ca91cx42_master_set;
	ca91cx42_bridge->master_read = ca91cx42_master_read;
	ca91cx42_bridge->master_write = ca91cx42_master_write;
#if 0
	ca91cx42_bridge->master_rmw = ca91cx42_master_rmw;
	ca91cx42_bridge->dma_list_add = ca91cx42_dma_list_add;
	ca91cx42_bridge->dma_list_exec = ca91cx42_dma_list_exec;
	ca91cx42_bridge->dma_list_empty = ca91cx42_dma_list_empty;
#endif
	ca91cx42_bridge->irq_set = ca91cx42_irq_set;
	ca91cx42_bridge->irq_generate = ca91cx42_irq_generate;
#if 0
	ca91cx42_bridge->lm_set = ca91cx42_lm_set;
	ca91cx42_bridge->lm_get = ca91cx42_lm_get;
	ca91cx42_bridge->lm_attach = ca91cx42_lm_attach;
	ca91cx42_bridge->lm_detach = ca91cx42_lm_detach;
#endif
	ca91cx42_bridge->slot_get = ca91cx42_slot_get;

	data = ioread32(ca91cx42_device->base + MISC_CTL);
	dev_info(&pdev->dev, "Board is%s the VME system controller\n",
		(data & CA91CX42_MISC_CTL_SYSCON) ? "" : " not");
	dev_info(&pdev->dev, "Slot ID is %d\n",
		ca91cx42_slot_get(ca91cx42_bridge));

	if (ca91cx42_crcsr_init(ca91cx42_bridge, pdev)) {
		dev_err(&pdev->dev, "CR/CSR configuration failed.\n");
		retval = -EINVAL;
#if 0
		goto err_crcsr;
#endif
	}

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

	vme_unregister_bridge(ca91cx42_bridge);
err_reg:
	ca91cx42_crcsr_exit(ca91cx42_bridge, pdev);
#if 0
err_crcsr:
#endif
err_lm:
	/* resources are stored in link list */
	list_for_each(pos, &(ca91cx42_bridge->lm_resources)) {
		lm = list_entry(pos, struct vme_lm_resource, list);
		list_del(pos);
		kfree(lm);
	}
#if 0
err_dma:
	/* resources are stored in link list */
	list_for_each(pos, &(ca91cx42_bridge->dma_resources)) {
		dma_ctrlr = list_entry(pos, struct vme_dma_resource, list);
		list_del(pos);
		kfree(dma_ctrlr);
	}
#endif
err_slave:
	/* resources are stored in link list */
	list_for_each(pos, &(ca91cx42_bridge->slave_resources)) {
		slave_image = list_entry(pos, struct vme_slave_resource, list);
		list_del(pos);
		kfree(slave_image);
	}
err_master:
	/* resources are stored in link list */
	list_for_each(pos, &(ca91cx42_bridge->master_resources)) {
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

void ca91cx42_remove(struct pci_dev *pdev)
{
	struct list_head *pos = NULL;
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
#if 0
	ca91cx42_crcsr_exit(pdev);
#endif
	/* resources are stored in link list */
	list_for_each(pos, &(ca91cx42_bridge->lm_resources)) {
		lm = list_entry(pos, struct vme_lm_resource, list);
		list_del(pos);
		kfree(lm);
	}

	/* resources are stored in link list */
	list_for_each(pos, &(ca91cx42_bridge->dma_resources)) {
		dma_ctrlr = list_entry(pos, struct vme_dma_resource, list);
		list_del(pos);
		kfree(dma_ctrlr);
	}

	/* resources are stored in link list */
	list_for_each(pos, &(ca91cx42_bridge->slave_resources)) {
		slave_image = list_entry(pos, struct vme_slave_resource, list);
		list_del(pos);
		kfree(slave_image);
	}

	/* resources are stored in link list */
	list_for_each(pos, &(ca91cx42_bridge->master_resources)) {
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

static void __exit ca91cx42_exit(void)
{
	pci_unregister_driver(&ca91cx42_driver);
}

MODULE_PARM_DESC(geoid, "Override geographical addressing");
module_param(geoid, int, 0);

MODULE_DESCRIPTION("VME driver for the Tundra Universe II VME bridge");
MODULE_LICENSE("GPL");

module_init(ca91cx42_init);
module_exit(ca91cx42_exit);

/*----------------------------------------------------------------------------
 * STAGING
 *--------------------------------------------------------------------------*/

#if 0
#define	SWIZZLE(X) ( ((X & 0xFF000000) >> 24) | ((X & 0x00FF0000) >>  8) | ((X & 0x0000FF00) <<  8) | ((X & 0x000000FF) << 24))

int ca91cx42_master_rmw(vmeRmwCfg_t *vmeRmw)
{
	int temp_ctl = 0;
	int tempBS = 0;
	int tempBD = 0;
	int tempTO = 0;
	int vmeBS = 0;
	int vmeBD = 0;
	int *rmw_pci_data_ptr = NULL;
	int *vaDataPtr = NULL;
	int i;
	vmeOutWindowCfg_t vmeOut;
	if (vmeRmw->maxAttempts < 1) {
		return -EINVAL;
	}
	if (vmeRmw->targetAddrU) {
		return -EINVAL;
	}
	/* Find the PCI address that maps to the desired VME address */
	for (i = 0; i < 8; i++) {
		temp_ctl = ioread32(bridge->base +
			CA91CX42_LSI_CTL[i]);
		if ((temp_ctl & 0x80000000) == 0) {
			continue;
		}
		memset(&vmeOut, 0, sizeof(vmeOut));
		vmeOut.windowNbr = i;
		ca91cx42_get_out_bound(&vmeOut);
		if (vmeOut.addrSpace != vmeRmw->addrSpace) {
			continue;
		}
		tempBS = ioread32(bridge->base + CA91CX42_LSI_BS[i]);
		tempBD = ioread32(bridge->base + CA91CX42_LSI_BD[i]);
		tempTO = ioread32(bridge->base + CA91CX42_LSI_TO[i]);
		vmeBS = tempBS + tempTO;
		vmeBD = tempBD + tempTO;
		if ((vmeRmw->targetAddr >= vmeBS) &&
		    (vmeRmw->targetAddr < vmeBD)) {
			rmw_pci_data_ptr =
			    (int *)(tempBS + (vmeRmw->targetAddr - vmeBS));
			vaDataPtr =
			    (int *)(out_image_va[i] +
				    (vmeRmw->targetAddr - vmeBS));
			break;
		}
	}

	/* If no window - fail. */
	if (rmw_pci_data_ptr == NULL) {
		return -EINVAL;
	}
	/* Setup the RMW registers. */
	iowrite32(0, bridge->base + SCYC_CTL);
	iowrite32(SWIZZLE(vmeRmw->enableMask), bridge->base + SCYC_EN);
	iowrite32(SWIZZLE(vmeRmw->compareData), bridge->base +
		SCYC_CMP);
	iowrite32(SWIZZLE(vmeRmw->swapData), bridge->base + SCYC_SWP);
	iowrite32((int)rmw_pci_data_ptr, bridge->base + SCYC_ADDR);
	iowrite32(1, bridge->base + SCYC_CTL);

	/* Run the RMW cycle until either success or max attempts. */
	vmeRmw->numAttempts = 1;
	while (vmeRmw->numAttempts <= vmeRmw->maxAttempts) {

		if ((ioread32(vaDataPtr) & vmeRmw->enableMask) ==
		    (vmeRmw->swapData & vmeRmw->enableMask)) {

			iowrite32(0, bridge->base + SCYC_CTL);
			break;

		}
		vmeRmw->numAttempts++;
	}

	/* If no success, set num Attempts to be greater than max attempts */
	if (vmeRmw->numAttempts > vmeRmw->maxAttempts) {
		vmeRmw->numAttempts = vmeRmw->maxAttempts + 1;
	}

	return 0;
}

int uniSetupDctlReg(vmeDmaPacket_t * vmeDma, int *dctlregreturn)
{
	unsigned int dctlreg = 0x80;
	struct vmeAttr *vmeAttr;

	if (vmeDma->srcBus == VME_DMA_VME) {
		dctlreg = 0;
		vmeAttr = &vmeDma->srcVmeAttr;
	} else {
		dctlreg = 0x80000000;
		vmeAttr = &vmeDma->dstVmeAttr;
	}

	switch (vmeAttr->maxDataWidth) {
	case VME_D8:
		break;
	case VME_D16:
		dctlreg |= 0x00400000;
		break;
	case VME_D32:
		dctlreg |= 0x00800000;
		break;
	case VME_D64:
		dctlreg |= 0x00C00000;
		break;
	}

	switch (vmeAttr->addrSpace) {
	case VME_A16:
		break;
	case VME_A24:
		dctlreg |= 0x00010000;
		break;
	case VME_A32:
		dctlreg |= 0x00020000;
		break;
	case VME_USER1:
		dctlreg |= 0x00060000;
		break;
	case VME_USER2:
		dctlreg |= 0x00070000;
		break;

	case VME_A64:		/* not supported in Universe DMA */
	case VME_CRCSR:
	case VME_USER3:
	case VME_USER4:
		return -EINVAL;
		break;
	}
	if (vmeAttr->userAccessType == VME_PROG) {
		dctlreg |= 0x00004000;
	}
	if (vmeAttr->dataAccessType == VME_SUPER) {
		dctlreg |= 0x00001000;
	}
	if (vmeAttr->xferProtocol != VME_SCT) {
		dctlreg |= 0x00000100;
	}
	*dctlregreturn = dctlreg;
	return 0;
}

unsigned int
ca91cx42_start_dma(int channel, unsigned int dgcsreg, TDMA_Cmd_Packet *vmeLL)
{
	unsigned int val;

	/* Setup registers as needed for direct or chained. */
	if (dgcsreg & 0x8000000) {
		iowrite32(0, bridge->base + DTBC);
		iowrite32((unsigned int)vmeLL, bridge->base + DCPP);
	} else {
#if	0
		printk(KERN_ERR "Starting: DGCS = %08x\n", dgcsreg);
		printk(KERN_ERR "Starting: DVA  = %08x\n",
			ioread32(&vmeLL->dva));
		printk(KERN_ERR "Starting: DLV  = %08x\n",
			ioread32(&vmeLL->dlv));
		printk(KERN_ERR "Starting: DTBC = %08x\n",
			ioread32(&vmeLL->dtbc));
		printk(KERN_ERR "Starting: DCTL = %08x\n",
			ioread32(&vmeLL->dctl));
#endif
		/* Write registers */
		iowrite32(ioread32(&vmeLL->dva), bridge->base + DVA);
		iowrite32(ioread32(&vmeLL->dlv), bridge->base + DLA);
		iowrite32(ioread32(&vmeLL->dtbc), bridge->base + DTBC);
		iowrite32(ioread32(&vmeLL->dctl), bridge->base + DCTL);
		iowrite32(0, bridge->base + DCPP);
	}

	/* Start the operation */
	iowrite32(dgcsreg, bridge->base + DGCS);
	val = get_tbl();
	iowrite32(dgcsreg | 0x8000000F, bridge->base + DGCS);
	return val;
}

TDMA_Cmd_Packet *ca91cx42_setup_dma(vmeDmaPacket_t * vmeDma)
{
	vmeDmaPacket_t *vmeCur;
	int maxPerPage;
	int currentLLcount;
	TDMA_Cmd_Packet *startLL;
	TDMA_Cmd_Packet *currentLL;
	TDMA_Cmd_Packet *nextLL;
	unsigned int dctlreg = 0;

	maxPerPage = PAGESIZE / sizeof(TDMA_Cmd_Packet) - 1;
	startLL = (TDMA_Cmd_Packet *) __get_free_pages(GFP_KERNEL, 0);
	if (startLL == 0) {
		return startLL;
	}
	/* First allocate pages for descriptors and create linked list */
	vmeCur = vmeDma;
	currentLL = startLL;
	currentLLcount = 0;
	while (vmeCur != 0) {
		if (vmeCur->pNextPacket != 0) {
			currentLL->dcpp = (unsigned int)(currentLL + 1);
			currentLLcount++;
			if (currentLLcount >= maxPerPage) {
				currentLL->dcpp =
				    __get_free_pages(GFP_KERNEL, 0);
				currentLLcount = 0;
			}
			currentLL = (TDMA_Cmd_Packet *) currentLL->dcpp;
		} else {
			currentLL->dcpp = (unsigned int)0;
		}
		vmeCur = vmeCur->pNextPacket;
	}

	/* Next fill in information for each descriptor */
	vmeCur = vmeDma;
	currentLL = startLL;
	while (vmeCur != 0) {
		if (vmeCur->srcBus == VME_DMA_VME) {
			iowrite32(vmeCur->srcAddr, &currentLL->dva);
			iowrite32(vmeCur->dstAddr, &currentLL->dlv);
		} else {
			iowrite32(vmeCur->srcAddr, &currentLL->dlv);
			iowrite32(vmeCur->dstAddr, &currentLL->dva);
		}
		uniSetupDctlReg(vmeCur, &dctlreg);
		iowrite32(dctlreg, &currentLL->dctl);
		iowrite32(vmeCur->byteCount, &currentLL->dtbc);

		currentLL = (TDMA_Cmd_Packet *) currentLL->dcpp;
		vmeCur = vmeCur->pNextPacket;
	}

	/* Convert Links to PCI addresses. */
	currentLL = startLL;
	while (currentLL != 0) {
		nextLL = (TDMA_Cmd_Packet *) currentLL->dcpp;
		if (nextLL == 0) {
			iowrite32(1, &currentLL->dcpp);
		} else {
			iowrite32((unsigned int)virt_to_bus(nextLL),
			       &currentLL->dcpp);
		}
		currentLL = nextLL;
	}

	/* Return pointer to descriptors list */
	return startLL;
}

int ca91cx42_free_dma(TDMA_Cmd_Packet *startLL)
{
	TDMA_Cmd_Packet *currentLL;
	TDMA_Cmd_Packet *prevLL;
	TDMA_Cmd_Packet *nextLL;
	unsigned int dcppreg;

	/* Convert Links to virtual addresses. */
	currentLL = startLL;
	while (currentLL != 0) {
		dcppreg = ioread32(&currentLL->dcpp);
		dcppreg &= ~6;
		if (dcppreg & 1) {
			currentLL->dcpp = 0;
		} else {
			currentLL->dcpp = (unsigned int)bus_to_virt(dcppreg);
		}
		currentLL = (TDMA_Cmd_Packet *) currentLL->dcpp;
	}

	/* Free all pages associated with the descriptors. */
	currentLL = startLL;
	prevLL = currentLL;
	while (currentLL != 0) {
		nextLL = (TDMA_Cmd_Packet *) currentLL->dcpp;
		if (currentLL + 1 != nextLL) {
			free_pages((int)prevLL, 0);
			prevLL = nextLL;
		}
		currentLL = nextLL;
	}

	/* Return pointer to descriptors list */
	return 0;
}

int ca91cx42_do_dma(vmeDmaPacket_t *vmeDma)
{
	unsigned int dgcsreg = 0;
	unsigned int dctlreg = 0;
	int val;
	int channel, x;
	vmeDmaPacket_t *curDma;
	TDMA_Cmd_Packet *dmaLL;

	/* Sanity check the VME chain. */
	channel = vmeDma->channel_number;
	if (channel > 0) {
		return -EINVAL;
	}
	curDma = vmeDma;
	while (curDma != 0) {
		if (curDma->byteCount == 0) {
			return -EINVAL;
		}
		if (curDma->byteCount >= 0x1000000) {
			return -EINVAL;
		}
		if ((curDma->srcAddr & 7) != (curDma->dstAddr & 7)) {
			return -EINVAL;
		}
		switch (curDma->srcBus) {
		case VME_DMA_PCI:
			if (curDma->dstBus != VME_DMA_VME) {
				return -EINVAL;
			}
			break;
		case VME_DMA_VME:
			if (curDma->dstBus != VME_DMA_PCI) {
				return -EINVAL;
			}
			break;
		default:
			return -EINVAL;
			break;
		}
		if (uniSetupDctlReg(curDma, &dctlreg) < 0) {
			return -EINVAL;
		}

		curDma = curDma->pNextPacket;
		if (curDma == vmeDma) {	/* Endless Loop! */
			return -EINVAL;
		}
	}

	/* calculate control register */
	if (vmeDma->pNextPacket != 0) {
		dgcsreg = 0x8000000;
	} else {
		dgcsreg = 0;
	}

	for (x = 0; x < 8; x++) {	/* vme block size */
		if ((256 << x) >= vmeDma->maxVmeBlockSize) {
			break;
		}
	}
	if (x == 8)
		x = 7;
	dgcsreg |= (x << 20);

	if (vmeDma->vmeBackOffTimer) {
		for (x = 1; x < 8; x++) {	/* vme timer */
			if ((16 << (x - 1)) >= vmeDma->vmeBackOffTimer) {
				break;
			}
		}
		if (x == 8)
			x = 7;
		dgcsreg |= (x << 16);
	}
	/*` Setup the dma chain */
	dmaLL = ca91cx42_setup_dma(vmeDma);

	/* Start the DMA */
	if (dgcsreg & 0x8000000) {
		vmeDma->vmeDmaStartTick =
		    ca91cx42_start_dma(channel, dgcsreg,
				  (TDMA_Cmd_Packet *) virt_to_phys(dmaLL));
	} else {
		vmeDma->vmeDmaStartTick =
		    ca91cx42_start_dma(channel, dgcsreg, dmaLL);
	}

	wait_event_interruptible(dma_queue,
		ioread32(bridge->base + DGCS) & 0x800);

	val = ioread32(bridge->base + DGCS);
	iowrite32(val | 0xF00, bridge->base + DGCS);

	vmeDma->vmeDmaStatus = 0;

	if (!(val & 0x00000800)) {
		vmeDma->vmeDmaStatus = val & 0x700;
		printk(KERN_ERR "ca91c042: DMA Error in ca91cx42_DMA_irqhandler"
			" DGCS=%08X\n", val);
		val = ioread32(bridge->base + DCPP);
		printk(KERN_ERR "ca91c042: DCPP=%08X\n", val);
		val = ioread32(bridge->base + DCTL);
		printk(KERN_ERR "ca91c042: DCTL=%08X\n", val);
		val = ioread32(bridge->base + DTBC);
		printk(KERN_ERR "ca91c042: DTBC=%08X\n", val);
		val = ioread32(bridge->base + DLA);
		printk(KERN_ERR "ca91c042: DLA=%08X\n", val);
		val = ioread32(bridge->base + DVA);
		printk(KERN_ERR "ca91c042: DVA=%08X\n", val);

	}
	/* Free the dma chain */
	ca91cx42_free_dma(dmaLL);

	return 0;
}

int ca91cx42_lm_set(vmeLmCfg_t *vmeLm)
{
	int temp_ctl = 0;

	if (vmeLm->addrU)
		return -EINVAL;

	switch (vmeLm->addrSpace) {
	case VME_A64:
	case VME_USER3:
	case VME_USER4:
		return -EINVAL;
	case VME_A16:
		temp_ctl |= 0x00000;
		break;
	case VME_A24:
		temp_ctl |= 0x10000;
		break;
	case VME_A32:
		temp_ctl |= 0x20000;
		break;
	case VME_CRCSR:
		temp_ctl |= 0x50000;
		break;
	case VME_USER1:
		temp_ctl |= 0x60000;
		break;
	case VME_USER2:
		temp_ctl |= 0x70000;
		break;
	}

	/* Disable while we are mucking around */
	iowrite32(0x00000000, bridge->base + LM_CTL);

	iowrite32(vmeLm->addr, bridge->base + LM_BS);

	/* Setup CTL register. */
	if (vmeLm->userAccessType & VME_SUPER)
		temp_ctl |= 0x00200000;
	if (vmeLm->userAccessType & VME_USER)
		temp_ctl |= 0x00100000;
	if (vmeLm->dataAccessType & VME_PROG)
		temp_ctl |= 0x00800000;
	if (vmeLm->dataAccessType & VME_DATA)
		temp_ctl |= 0x00400000;


	/* Write ctl reg and enable */
	iowrite32(0x80000000 | temp_ctl, bridge->base + LM_CTL);
	temp_ctl = ioread32(bridge->base + LM_CTL);

	return 0;
}

int ca91cx42_wait_lm(vmeLmCfg_t *vmeLm)
{
	unsigned long flags;
	unsigned int tmp;

	spin_lock_irqsave(&lm_lock, flags);
	spin_unlock_irqrestore(&lm_lock, flags);
	if (tmp == 0) {
		if (vmeLm->lmWait < 10)
			vmeLm->lmWait = 10;
		interruptible_sleep_on_timeout(&lm_queue, vmeLm->lmWait);
	}
	iowrite32(0x00000000, bridge->base + LM_CTL);

	return 0;
}



int ca91cx42_set_arbiter(vmeArbiterCfg_t *vmeArb)
{
	int temp_ctl = 0;
	int vbto = 0;

	temp_ctl = ioread32(bridge->base + MISC_CTL);
	temp_ctl &= 0x00FFFFFF;

	if (vmeArb->globalTimeoutTimer == 0xFFFFFFFF) {
		vbto = 7;
	} else if (vmeArb->globalTimeoutTimer > 1024) {
		return -EINVAL;
	} else if (vmeArb->globalTimeoutTimer == 0) {
		vbto = 0;
	} else {
		vbto = 1;
		while ((16 * (1 << (vbto - 1))) < vmeArb->globalTimeoutTimer)
			vbto += 1;
	}
	temp_ctl |= (vbto << 28);

	if (vmeArb->arbiterMode == VME_PRIORITY_MODE)
		temp_ctl |= 1 << 26;

	if (vmeArb->arbiterTimeoutFlag)
		temp_ctl |= 2 << 24;

	iowrite32(temp_ctl, bridge->base + MISC_CTL);
	return 0;
}

int ca91cx42_get_arbiter(vmeArbiterCfg_t *vmeArb)
{
	int temp_ctl = 0;
	int vbto = 0;

	temp_ctl = ioread32(bridge->base + MISC_CTL);

	vbto = (temp_ctl >> 28) & 0xF;
	if (vbto != 0)
		vmeArb->globalTimeoutTimer = (16 * (1 << (vbto - 1)));

	if (temp_ctl & (1 << 26))
		vmeArb->arbiterMode = VME_PRIORITY_MODE;
	else
		vmeArb->arbiterMode = VME_R_ROBIN_MODE;

	if (temp_ctl & (3 << 24))
		vmeArb->arbiterTimeoutFlag = 1;

	return 0;
}

int ca91cx42_set_requestor(vmeRequesterCfg_t *vmeReq)
{
	int temp_ctl = 0;

	temp_ctl = ioread32(bridge->base + MAST_CTL);
	temp_ctl &= 0xFF0FFFFF;

	if (vmeReq->releaseMode == 1)
		temp_ctl |= (1 << 20);

	if (vmeReq->fairMode == 1)
		temp_ctl |= (1 << 21);

	temp_ctl |= (vmeReq->requestLevel << 22);

	iowrite32(temp_ctl, bridge->base + MAST_CTL);
	return 0;
}

int ca91cx42_get_requestor(vmeRequesterCfg_t *vmeReq)
{
	int temp_ctl = 0;

	temp_ctl = ioread32(bridge->base + MAST_CTL);

	if (temp_ctl & (1 << 20))
		vmeReq->releaseMode = 1;

	if (temp_ctl & (1 << 21))
		vmeReq->fairMode = 1;

	vmeReq->requestLevel = (temp_ctl & 0xC00000) >> 22;

	return 0;
}


#endif
