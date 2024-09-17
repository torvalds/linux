// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Support for the Tundra TSI148 VME-PCI Bridge Chip
 *
 * Author: Martyn Welch <martyn.welch@ge.com>
 * Copyright 2008 GE Intelligent Platforms Embedded Systems, Inc.
 *
 * Based on work by Tom Armistead and Ajit Prem
 * Copyright 2004 Motorola Inc.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/byteorder/generic.h>

#include "vme.h"
#include "vme_bridge.h"
#include "vme_tsi148.h"

static int tsi148_probe(struct pci_dev *, const struct pci_device_id *);
static void tsi148_remove(struct pci_dev *);


/* Module parameter */
static bool err_chk;
static int geoid;

static const char driver_name[] = "vme_tsi148";

static const struct pci_device_id tsi148_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_TUNDRA, PCI_DEVICE_ID_TUNDRA_TSI148) },
	{ },
};

MODULE_DEVICE_TABLE(pci, tsi148_ids);

static struct pci_driver tsi148_driver = {
	.name = driver_name,
	.id_table = tsi148_ids,
	.probe = tsi148_probe,
	.remove = tsi148_remove,
};

static void reg_join(unsigned int high, unsigned int low,
	unsigned long long *variable)
{
	*variable = (unsigned long long)high << 32;
	*variable |= (unsigned long long)low;
}

static void reg_split(unsigned long long variable, unsigned int *high,
	unsigned int *low)
{
	*low = (unsigned int)variable & 0xFFFFFFFF;
	*high = (unsigned int)(variable >> 32);
}

/*
 * Wakes up DMA queue.
 */
static u32 tsi148_DMA_irqhandler(struct tsi148_driver *bridge,
	int channel_mask)
{
	u32 serviced = 0;

	if (channel_mask & TSI148_LCSR_INTS_DMA0S) {
		wake_up(&bridge->dma_queue[0]);
		serviced |= TSI148_LCSR_INTC_DMA0C;
	}
	if (channel_mask & TSI148_LCSR_INTS_DMA1S) {
		wake_up(&bridge->dma_queue[1]);
		serviced |= TSI148_LCSR_INTC_DMA1C;
	}

	return serviced;
}

/*
 * Wake up location monitor queue
 */
static u32 tsi148_LM_irqhandler(struct tsi148_driver *bridge, u32 stat)
{
	int i;
	u32 serviced = 0;

	for (i = 0; i < 4; i++) {
		if (stat & TSI148_LCSR_INTS_LMS[i]) {
			/* We only enable interrupts if the callback is set */
			bridge->lm_callback[i](bridge->lm_data[i]);
			serviced |= TSI148_LCSR_INTC_LMC[i];
		}
	}

	return serviced;
}

/*
 * Wake up mail box queue.
 *
 * XXX This functionality is not exposed up though API.
 */
static u32 tsi148_MB_irqhandler(struct vme_bridge *tsi148_bridge, u32 stat)
{
	int i;
	u32 val;
	u32 serviced = 0;
	struct tsi148_driver *bridge;

	bridge = tsi148_bridge->driver_priv;

	for (i = 0; i < 4; i++) {
		if (stat & TSI148_LCSR_INTS_MBS[i]) {
			val = ioread32be(bridge->base +	TSI148_GCSR_MBOX[i]);
			dev_err(tsi148_bridge->parent, "VME Mailbox %d received: 0x%x\n",
				i, val);
			serviced |= TSI148_LCSR_INTC_MBC[i];
		}
	}

	return serviced;
}

/*
 * Display error & status message when PERR (PCI) exception interrupt occurs.
 */
static u32 tsi148_PERR_irqhandler(struct vme_bridge *tsi148_bridge)
{
	struct tsi148_driver *bridge;

	bridge = tsi148_bridge->driver_priv;

	dev_err(tsi148_bridge->parent, "PCI Exception at address: 0x%08x:%08x, attributes: %08x\n",
		ioread32be(bridge->base + TSI148_LCSR_EDPAU),
		ioread32be(bridge->base + TSI148_LCSR_EDPAL),
		ioread32be(bridge->base + TSI148_LCSR_EDPAT));

	dev_err(tsi148_bridge->parent, "PCI-X attribute reg: %08x, PCI-X split completion reg: %08x\n",
		ioread32be(bridge->base + TSI148_LCSR_EDPXA),
		ioread32be(bridge->base + TSI148_LCSR_EDPXS));

	iowrite32be(TSI148_LCSR_EDPAT_EDPCL, bridge->base + TSI148_LCSR_EDPAT);

	return TSI148_LCSR_INTC_PERRC;
}

/*
 * Save address and status when VME error interrupt occurs.
 */
static u32 tsi148_VERR_irqhandler(struct vme_bridge *tsi148_bridge)
{
	unsigned int error_addr_high, error_addr_low;
	unsigned long long error_addr;
	u32 error_attrib;
	int error_am;
	struct tsi148_driver *bridge;

	bridge = tsi148_bridge->driver_priv;

	error_addr_high = ioread32be(bridge->base + TSI148_LCSR_VEAU);
	error_addr_low = ioread32be(bridge->base + TSI148_LCSR_VEAL);
	error_attrib = ioread32be(bridge->base + TSI148_LCSR_VEAT);
	error_am = (error_attrib & TSI148_LCSR_VEAT_AM_M) >> 8;

	reg_join(error_addr_high, error_addr_low, &error_addr);

	/* Check for exception register overflow (we have lost error data) */
	if (error_attrib & TSI148_LCSR_VEAT_VEOF)
		dev_err(tsi148_bridge->parent, "VME Bus Exception Overflow Occurred\n");

	if (err_chk)
		vme_bus_error_handler(tsi148_bridge, error_addr, error_am);
	else
		dev_err(tsi148_bridge->parent,
			"VME Bus Error at address: 0x%llx, attributes: %08x\n",
			error_addr, error_attrib);

	/* Clear Status */
	iowrite32be(TSI148_LCSR_VEAT_VESCL, bridge->base + TSI148_LCSR_VEAT);

	return TSI148_LCSR_INTC_VERRC;
}

/*
 * Wake up IACK queue.
 */
static u32 tsi148_IACK_irqhandler(struct tsi148_driver *bridge)
{
	wake_up(&bridge->iack_queue);

	return TSI148_LCSR_INTC_IACKC;
}

/*
 * Calling VME bus interrupt callback if provided.
 */
static u32 tsi148_VIRQ_irqhandler(struct vme_bridge *tsi148_bridge,
	u32 stat)
{
	int vec, i, serviced = 0;
	struct tsi148_driver *bridge;

	bridge = tsi148_bridge->driver_priv;

	for (i = 7; i > 0; i--) {
		if (stat & (1 << i)) {
			/*
			 * Note: Even though the registers are defined as
			 * 32-bits in the spec, we only want to issue 8-bit
			 * IACK cycles on the bus, read from offset 3.
			 */
			vec = ioread8(bridge->base + TSI148_LCSR_VIACK[i] + 3);

			vme_irq_handler(tsi148_bridge, i, vec);

			serviced |= (1 << i);
		}
	}

	return serviced;
}

/*
 * Top level interrupt handler.  Clears appropriate interrupt status bits and
 * then calls appropriate sub handler(s).
 */
static irqreturn_t tsi148_irqhandler(int irq, void *ptr)
{
	u32 stat, enable, serviced = 0;
	struct vme_bridge *tsi148_bridge;
	struct tsi148_driver *bridge;

	tsi148_bridge = ptr;

	bridge = tsi148_bridge->driver_priv;

	/* Determine which interrupts are unmasked and set */
	enable = ioread32be(bridge->base + TSI148_LCSR_INTEO);
	stat = ioread32be(bridge->base + TSI148_LCSR_INTS);

	/* Only look at unmasked interrupts */
	stat &= enable;

	if (unlikely(!stat))
		return IRQ_NONE;

	/* Call subhandlers as appropriate */
	/* DMA irqs */
	if (stat & (TSI148_LCSR_INTS_DMA1S | TSI148_LCSR_INTS_DMA0S))
		serviced |= tsi148_DMA_irqhandler(bridge, stat);

	/* Location monitor irqs */
	if (stat & (TSI148_LCSR_INTS_LM3S | TSI148_LCSR_INTS_LM2S |
			TSI148_LCSR_INTS_LM1S | TSI148_LCSR_INTS_LM0S))
		serviced |= tsi148_LM_irqhandler(bridge, stat);

	/* Mail box irqs */
	if (stat & (TSI148_LCSR_INTS_MB3S | TSI148_LCSR_INTS_MB2S |
			TSI148_LCSR_INTS_MB1S | TSI148_LCSR_INTS_MB0S))
		serviced |= tsi148_MB_irqhandler(tsi148_bridge, stat);

	/* PCI bus error */
	if (stat & TSI148_LCSR_INTS_PERRS)
		serviced |= tsi148_PERR_irqhandler(tsi148_bridge);

	/* VME bus error */
	if (stat & TSI148_LCSR_INTS_VERRS)
		serviced |= tsi148_VERR_irqhandler(tsi148_bridge);

	/* IACK irq */
	if (stat & TSI148_LCSR_INTS_IACKS)
		serviced |= tsi148_IACK_irqhandler(bridge);

	/* VME bus irqs */
	if (stat & (TSI148_LCSR_INTS_IRQ7S | TSI148_LCSR_INTS_IRQ6S |
			TSI148_LCSR_INTS_IRQ5S | TSI148_LCSR_INTS_IRQ4S |
			TSI148_LCSR_INTS_IRQ3S | TSI148_LCSR_INTS_IRQ2S |
			TSI148_LCSR_INTS_IRQ1S))
		serviced |= tsi148_VIRQ_irqhandler(tsi148_bridge, stat);

	/* Clear serviced interrupts */
	iowrite32be(serviced, bridge->base + TSI148_LCSR_INTC);

	return IRQ_HANDLED;
}

static int tsi148_irq_init(struct vme_bridge *tsi148_bridge)
{
	int result;
	unsigned int tmp;
	struct pci_dev *pdev;
	struct tsi148_driver *bridge;

	pdev = to_pci_dev(tsi148_bridge->parent);

	bridge = tsi148_bridge->driver_priv;

	result = request_irq(pdev->irq,
			     tsi148_irqhandler,
			     IRQF_SHARED,
			     driver_name, tsi148_bridge);
	if (result) {
		dev_err(tsi148_bridge->parent, "Can't get assigned pci irq vector %02X\n",
			pdev->irq);
		return result;
	}

	/* Enable and unmask interrupts */
	tmp = TSI148_LCSR_INTEO_DMA1EO | TSI148_LCSR_INTEO_DMA0EO |
		TSI148_LCSR_INTEO_MB3EO | TSI148_LCSR_INTEO_MB2EO |
		TSI148_LCSR_INTEO_MB1EO | TSI148_LCSR_INTEO_MB0EO |
		TSI148_LCSR_INTEO_PERREO | TSI148_LCSR_INTEO_VERREO |
		TSI148_LCSR_INTEO_IACKEO;

	/* This leaves the following interrupts masked.
	 * TSI148_LCSR_INTEO_VIEEO
	 * TSI148_LCSR_INTEO_SYSFLEO
	 * TSI148_LCSR_INTEO_ACFLEO
	 */

	/* Don't enable Location Monitor interrupts here - they will be
	 * enabled when the location monitors are properly configured and
	 * a callback has been attached.
	 * TSI148_LCSR_INTEO_LM0EO
	 * TSI148_LCSR_INTEO_LM1EO
	 * TSI148_LCSR_INTEO_LM2EO
	 * TSI148_LCSR_INTEO_LM3EO
	 */

	/* Don't enable VME interrupts until we add a handler, else the board
	 * will respond to it and we don't want that unless it knows how to
	 * properly deal with it.
	 * TSI148_LCSR_INTEO_IRQ7EO
	 * TSI148_LCSR_INTEO_IRQ6EO
	 * TSI148_LCSR_INTEO_IRQ5EO
	 * TSI148_LCSR_INTEO_IRQ4EO
	 * TSI148_LCSR_INTEO_IRQ3EO
	 * TSI148_LCSR_INTEO_IRQ2EO
	 * TSI148_LCSR_INTEO_IRQ1EO
	 */

	iowrite32be(tmp, bridge->base + TSI148_LCSR_INTEO);
	iowrite32be(tmp, bridge->base + TSI148_LCSR_INTEN);

	return 0;
}

static void tsi148_irq_exit(struct vme_bridge *tsi148_bridge,
	struct pci_dev *pdev)
{
	struct tsi148_driver *bridge = tsi148_bridge->driver_priv;

	/* Turn off interrupts */
	iowrite32be(0x0, bridge->base + TSI148_LCSR_INTEO);
	iowrite32be(0x0, bridge->base + TSI148_LCSR_INTEN);

	/* Clear all interrupts */
	iowrite32be(0xFFFFFFFF, bridge->base + TSI148_LCSR_INTC);

	/* Detach interrupt handler */
	free_irq(pdev->irq, tsi148_bridge);
}

/*
 * Check to see if an IACk has been received, return true (1) or false (0).
 */
static int tsi148_iack_received(struct tsi148_driver *bridge)
{
	u32 tmp;

	tmp = ioread32be(bridge->base + TSI148_LCSR_VICR);

	if (tmp & TSI148_LCSR_VICR_IRQS)
		return 0;
	else
		return 1;
}

/*
 * Configure VME interrupt
 */
static void tsi148_irq_set(struct vme_bridge *tsi148_bridge, int level,
	int state, int sync)
{
	struct pci_dev *pdev;
	u32 tmp;
	struct tsi148_driver *bridge;

	bridge = tsi148_bridge->driver_priv;

	/* We need to do the ordering differently for enabling and disabling */
	if (state == 0) {
		tmp = ioread32be(bridge->base + TSI148_LCSR_INTEN);
		tmp &= ~TSI148_LCSR_INTEN_IRQEN[level - 1];
		iowrite32be(tmp, bridge->base + TSI148_LCSR_INTEN);

		tmp = ioread32be(bridge->base + TSI148_LCSR_INTEO);
		tmp &= ~TSI148_LCSR_INTEO_IRQEO[level - 1];
		iowrite32be(tmp, bridge->base + TSI148_LCSR_INTEO);

		if (sync != 0) {
			pdev = to_pci_dev(tsi148_bridge->parent);
			synchronize_irq(pdev->irq);
		}
	} else {
		tmp = ioread32be(bridge->base + TSI148_LCSR_INTEO);
		tmp |= TSI148_LCSR_INTEO_IRQEO[level - 1];
		iowrite32be(tmp, bridge->base + TSI148_LCSR_INTEO);

		tmp = ioread32be(bridge->base + TSI148_LCSR_INTEN);
		tmp |= TSI148_LCSR_INTEN_IRQEN[level - 1];
		iowrite32be(tmp, bridge->base + TSI148_LCSR_INTEN);
	}
}

/*
 * Generate a VME bus interrupt at the requested level & vector. Wait for
 * interrupt to be acked.
 */
static int tsi148_irq_generate(struct vme_bridge *tsi148_bridge, int level,
	int statid)
{
	u32 tmp;
	struct tsi148_driver *bridge;

	bridge = tsi148_bridge->driver_priv;

	mutex_lock(&bridge->vme_int);

	/* Read VICR register */
	tmp = ioread32be(bridge->base + TSI148_LCSR_VICR);

	/* Set Status/ID */
	tmp = (tmp & ~TSI148_LCSR_VICR_STID_M) |
		(statid & TSI148_LCSR_VICR_STID_M);
	iowrite32be(tmp, bridge->base + TSI148_LCSR_VICR);

	/* Assert VMEbus IRQ */
	tmp = tmp | TSI148_LCSR_VICR_IRQL[level];
	iowrite32be(tmp, bridge->base + TSI148_LCSR_VICR);

	/* XXX Consider implementing a timeout? */
	wait_event_interruptible(bridge->iack_queue,
		tsi148_iack_received(bridge));

	mutex_unlock(&bridge->vme_int);

	return 0;
}

/*
 * Initialize a slave window with the requested attributes.
 */
static int tsi148_slave_set(struct vme_slave_resource *image, int enabled,
	unsigned long long vme_base, unsigned long long size,
	dma_addr_t pci_base, u32 aspace, u32 cycle)
{
	unsigned int i, addr = 0, granularity = 0;
	unsigned int temp_ctl = 0;
	unsigned int vme_base_low, vme_base_high;
	unsigned int vme_bound_low, vme_bound_high;
	unsigned int pci_offset_low, pci_offset_high;
	unsigned long long vme_bound, pci_offset;
	struct vme_bridge *tsi148_bridge;
	struct tsi148_driver *bridge;

	tsi148_bridge = image->parent;
	bridge = tsi148_bridge->driver_priv;

	i = image->number;

	switch (aspace) {
	case VME_A16:
		granularity = 0x10;
		addr |= TSI148_LCSR_ITAT_AS_A16;
		break;
	case VME_A24:
		granularity = 0x1000;
		addr |= TSI148_LCSR_ITAT_AS_A24;
		break;
	case VME_A32:
		granularity = 0x10000;
		addr |= TSI148_LCSR_ITAT_AS_A32;
		break;
	case VME_A64:
		granularity = 0x10000;
		addr |= TSI148_LCSR_ITAT_AS_A64;
		break;
	default:
		dev_err(tsi148_bridge->parent, "Invalid address space\n");
		return -EINVAL;
	}

	/* Convert 64-bit variables to 2x 32-bit variables */
	reg_split(vme_base, &vme_base_high, &vme_base_low);

	/*
	 * Bound address is a valid address for the window, adjust
	 * accordingly
	 */
	vme_bound = vme_base + size - granularity;
	reg_split(vme_bound, &vme_bound_high, &vme_bound_low);
	pci_offset = (unsigned long long)pci_base - vme_base;
	reg_split(pci_offset, &pci_offset_high, &pci_offset_low);

	if (vme_base_low & (granularity - 1)) {
		dev_err(tsi148_bridge->parent, "Invalid VME base alignment\n");
		return -EINVAL;
	}
	if (vme_bound_low & (granularity - 1)) {
		dev_err(tsi148_bridge->parent, "Invalid VME bound alignment\n");
		return -EINVAL;
	}
	if (pci_offset_low & (granularity - 1)) {
		dev_err(tsi148_bridge->parent, "Invalid PCI Offset alignment\n");
		return -EINVAL;
	}

	/*  Disable while we are mucking around */
	temp_ctl = ioread32be(bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITAT);
	temp_ctl &= ~TSI148_LCSR_ITAT_EN;
	iowrite32be(temp_ctl, bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITAT);

	/* Setup mapping */
	iowrite32be(vme_base_high, bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITSAU);
	iowrite32be(vme_base_low, bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITSAL);
	iowrite32be(vme_bound_high, bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITEAU);
	iowrite32be(vme_bound_low, bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITEAL);
	iowrite32be(pci_offset_high, bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITOFU);
	iowrite32be(pci_offset_low, bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITOFL);

	/* Setup 2eSST speeds */
	temp_ctl &= ~TSI148_LCSR_ITAT_2eSSTM_M;
	switch (cycle & (VME_2eSST160 | VME_2eSST267 | VME_2eSST320)) {
	case VME_2eSST160:
		temp_ctl |= TSI148_LCSR_ITAT_2eSSTM_160;
		break;
	case VME_2eSST267:
		temp_ctl |= TSI148_LCSR_ITAT_2eSSTM_267;
		break;
	case VME_2eSST320:
		temp_ctl |= TSI148_LCSR_ITAT_2eSSTM_320;
		break;
	}

	/* Setup cycle types */
	temp_ctl &= ~(0x1F << 7);
	if (cycle & VME_BLT)
		temp_ctl |= TSI148_LCSR_ITAT_BLT;
	if (cycle & VME_MBLT)
		temp_ctl |= TSI148_LCSR_ITAT_MBLT;
	if (cycle & VME_2eVME)
		temp_ctl |= TSI148_LCSR_ITAT_2eVME;
	if (cycle & VME_2eSST)
		temp_ctl |= TSI148_LCSR_ITAT_2eSST;
	if (cycle & VME_2eSSTB)
		temp_ctl |= TSI148_LCSR_ITAT_2eSSTB;

	/* Setup address space */
	temp_ctl &= ~TSI148_LCSR_ITAT_AS_M;
	temp_ctl |= addr;

	temp_ctl &= ~0xF;
	if (cycle & VME_SUPER)
		temp_ctl |= TSI148_LCSR_ITAT_SUPR;
	if (cycle & VME_USER)
		temp_ctl |= TSI148_LCSR_ITAT_NPRIV;
	if (cycle & VME_PROG)
		temp_ctl |= TSI148_LCSR_ITAT_PGM;
	if (cycle & VME_DATA)
		temp_ctl |= TSI148_LCSR_ITAT_DATA;

	/* Write ctl reg without enable */
	iowrite32be(temp_ctl, bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITAT);

	if (enabled)
		temp_ctl |= TSI148_LCSR_ITAT_EN;

	iowrite32be(temp_ctl, bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITAT);

	return 0;
}

/*
 * Get slave window configuration.
 */
static int tsi148_slave_get(struct vme_slave_resource *image, int *enabled,
	unsigned long long *vme_base, unsigned long long *size,
	dma_addr_t *pci_base, u32 *aspace, u32 *cycle)
{
	unsigned int i, granularity = 0, ctl = 0;
	unsigned int vme_base_low, vme_base_high;
	unsigned int vme_bound_low, vme_bound_high;
	unsigned int pci_offset_low, pci_offset_high;
	unsigned long long vme_bound, pci_offset;
	struct tsi148_driver *bridge;

	bridge = image->parent->driver_priv;

	i = image->number;

	/* Read registers */
	ctl = ioread32be(bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITAT);

	vme_base_high = ioread32be(bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITSAU);
	vme_base_low = ioread32be(bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITSAL);
	vme_bound_high = ioread32be(bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITEAU);
	vme_bound_low = ioread32be(bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITEAL);
	pci_offset_high = ioread32be(bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITOFU);
	pci_offset_low = ioread32be(bridge->base + TSI148_LCSR_IT[i] +
		TSI148_LCSR_OFFSET_ITOFL);

	/* Convert 64-bit variables to 2x 32-bit variables */
	reg_join(vme_base_high, vme_base_low, vme_base);
	reg_join(vme_bound_high, vme_bound_low, &vme_bound);
	reg_join(pci_offset_high, pci_offset_low, &pci_offset);

	*pci_base = (dma_addr_t)(*vme_base + pci_offset);

	*enabled = 0;
	*aspace = 0;
	*cycle = 0;

	if (ctl & TSI148_LCSR_ITAT_EN)
		*enabled = 1;

	if ((ctl & TSI148_LCSR_ITAT_AS_M) == TSI148_LCSR_ITAT_AS_A16) {
		granularity = 0x10;
		*aspace |= VME_A16;
	}
	if ((ctl & TSI148_LCSR_ITAT_AS_M) == TSI148_LCSR_ITAT_AS_A24) {
		granularity = 0x1000;
		*aspace |= VME_A24;
	}
	if ((ctl & TSI148_LCSR_ITAT_AS_M) == TSI148_LCSR_ITAT_AS_A32) {
		granularity = 0x10000;
		*aspace |= VME_A32;
	}
	if ((ctl & TSI148_LCSR_ITAT_AS_M) == TSI148_LCSR_ITAT_AS_A64) {
		granularity = 0x10000;
		*aspace |= VME_A64;
	}

	/* Need granularity before we set the size */
	*size = (unsigned long long)((vme_bound - *vme_base) + granularity);


	if ((ctl & TSI148_LCSR_ITAT_2eSSTM_M) == TSI148_LCSR_ITAT_2eSSTM_160)
		*cycle |= VME_2eSST160;
	if ((ctl & TSI148_LCSR_ITAT_2eSSTM_M) == TSI148_LCSR_ITAT_2eSSTM_267)
		*cycle |= VME_2eSST267;
	if ((ctl & TSI148_LCSR_ITAT_2eSSTM_M) == TSI148_LCSR_ITAT_2eSSTM_320)
		*cycle |= VME_2eSST320;

	if (ctl & TSI148_LCSR_ITAT_BLT)
		*cycle |= VME_BLT;
	if (ctl & TSI148_LCSR_ITAT_MBLT)
		*cycle |= VME_MBLT;
	if (ctl & TSI148_LCSR_ITAT_2eVME)
		*cycle |= VME_2eVME;
	if (ctl & TSI148_LCSR_ITAT_2eSST)
		*cycle |= VME_2eSST;
	if (ctl & TSI148_LCSR_ITAT_2eSSTB)
		*cycle |= VME_2eSSTB;

	if (ctl & TSI148_LCSR_ITAT_SUPR)
		*cycle |= VME_SUPER;
	if (ctl & TSI148_LCSR_ITAT_NPRIV)
		*cycle |= VME_USER;
	if (ctl & TSI148_LCSR_ITAT_PGM)
		*cycle |= VME_PROG;
	if (ctl & TSI148_LCSR_ITAT_DATA)
		*cycle |= VME_DATA;

	return 0;
}

/*
 * Allocate and map PCI Resource
 */
static int tsi148_alloc_resource(struct vme_master_resource *image,
	unsigned long long size)
{
	unsigned long long existing_size;
	int retval = 0;
	struct pci_dev *pdev;
	struct vme_bridge *tsi148_bridge;

	tsi148_bridge = image->parent;

	pdev = to_pci_dev(tsi148_bridge->parent);

	existing_size = (unsigned long long)(image->bus_resource.end -
		image->bus_resource.start);

	/* If the existing size is OK, return */
	if ((size != 0) && (existing_size == (size - 1)))
		return 0;

	if (existing_size != 0) {
		iounmap(image->kern_base);
		image->kern_base = NULL;
		kfree(image->bus_resource.name);
		release_resource(&image->bus_resource);
		memset(&image->bus_resource, 0, sizeof(image->bus_resource));
	}

	/* Exit here if size is zero */
	if (size == 0)
		return 0;

	if (!image->bus_resource.name) {
		image->bus_resource.name = kmalloc(VMENAMSIZ+3, GFP_ATOMIC);
		if (!image->bus_resource.name) {
			retval = -ENOMEM;
			goto err_name;
		}
	}

	sprintf((char *)image->bus_resource.name, "%s.%d", tsi148_bridge->name,
		image->number);

	image->bus_resource.start = 0;
	image->bus_resource.end = (unsigned long)size;
	image->bus_resource.flags = IORESOURCE_MEM;

	retval = pci_bus_alloc_resource(pdev->bus,
		&image->bus_resource, size, 0x10000, PCIBIOS_MIN_MEM,
		0, NULL, NULL);
	if (retval) {
		dev_err(tsi148_bridge->parent, "Failed to allocate mem resource for window %d size 0x%lx start 0x%lx\n",
			image->number, (unsigned long)size,
			(unsigned long)image->bus_resource.start);
		goto err_resource;
	}

	image->kern_base = ioremap(
		image->bus_resource.start, size);
	if (!image->kern_base) {
		dev_err(tsi148_bridge->parent, "Failed to remap resource\n");
		retval = -ENOMEM;
		goto err_remap;
	}

	return 0;

err_remap:
	release_resource(&image->bus_resource);
err_resource:
	kfree(image->bus_resource.name);
	memset(&image->bus_resource, 0, sizeof(image->bus_resource));
err_name:
	return retval;
}

/*
 * Free and unmap PCI Resource
 */
static void tsi148_free_resource(struct vme_master_resource *image)
{
	iounmap(image->kern_base);
	image->kern_base = NULL;
	release_resource(&image->bus_resource);
	kfree(image->bus_resource.name);
	memset(&image->bus_resource, 0, sizeof(image->bus_resource));
}

/*
 * Set the attributes of an outbound window.
 */
static int tsi148_master_set(struct vme_master_resource *image, int enabled,
	unsigned long long vme_base, unsigned long long size, u32 aspace,
	u32 cycle, u32 dwidth)
{
	int retval = 0;
	unsigned int i;
	unsigned int temp_ctl = 0;
	unsigned int pci_base_low, pci_base_high;
	unsigned int pci_bound_low, pci_bound_high;
	unsigned int vme_offset_low, vme_offset_high;
	unsigned long long pci_bound, vme_offset, pci_base;
	struct vme_bridge *tsi148_bridge;
	struct tsi148_driver *bridge;
	struct pci_bus_region region;
	struct pci_dev *pdev;

	tsi148_bridge = image->parent;

	bridge = tsi148_bridge->driver_priv;

	pdev = to_pci_dev(tsi148_bridge->parent);

	/* Verify input data */
	if (vme_base & 0xFFFF) {
		dev_err(tsi148_bridge->parent, "Invalid VME Window alignment\n");
		retval = -EINVAL;
		goto err_window;
	}

	if ((size == 0) && (enabled != 0)) {
		dev_err(tsi148_bridge->parent, "Size must be non-zero for enabled windows\n");
		retval = -EINVAL;
		goto err_window;
	}

	spin_lock(&image->lock);

	/* Let's allocate the resource here rather than further up the stack as
	 * it avoids pushing loads of bus dependent stuff up the stack. If size
	 * is zero, any existing resource will be freed.
	 */
	retval = tsi148_alloc_resource(image, size);
	if (retval) {
		spin_unlock(&image->lock);
		dev_err(tsi148_bridge->parent, "Unable to allocate memory for resource\n");
		goto err_res;
	}

	if (size == 0) {
		pci_base = 0;
		pci_bound = 0;
		vme_offset = 0;
	} else {
		pcibios_resource_to_bus(pdev->bus, &region,
					&image->bus_resource);
		pci_base = region.start;

		/*
		 * Bound address is a valid address for the window, adjust
		 * according to window granularity.
		 */
		pci_bound = pci_base + (size - 0x10000);
		vme_offset = vme_base - pci_base;
	}

	/* Convert 64-bit variables to 2x 32-bit variables */
	reg_split(pci_base, &pci_base_high, &pci_base_low);
	reg_split(pci_bound, &pci_bound_high, &pci_bound_low);
	reg_split(vme_offset, &vme_offset_high, &vme_offset_low);

	if (pci_base_low & 0xFFFF) {
		spin_unlock(&image->lock);
		dev_err(tsi148_bridge->parent, "Invalid PCI base alignment\n");
		retval = -EINVAL;
		goto err_gran;
	}
	if (pci_bound_low & 0xFFFF) {
		spin_unlock(&image->lock);
		dev_err(tsi148_bridge->parent, "Invalid PCI bound alignment\n");
		retval = -EINVAL;
		goto err_gran;
	}
	if (vme_offset_low & 0xFFFF) {
		spin_unlock(&image->lock);
		dev_err(tsi148_bridge->parent, "Invalid VME Offset alignment\n");
		retval = -EINVAL;
		goto err_gran;
	}

	i = image->number;

	/* Disable while we are mucking around */
	temp_ctl = ioread32be(bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTAT);
	temp_ctl &= ~TSI148_LCSR_OTAT_EN;
	iowrite32be(temp_ctl, bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTAT);

	/* Setup 2eSST speeds */
	temp_ctl &= ~TSI148_LCSR_OTAT_2eSSTM_M;
	switch (cycle & (VME_2eSST160 | VME_2eSST267 | VME_2eSST320)) {
	case VME_2eSST160:
		temp_ctl |= TSI148_LCSR_OTAT_2eSSTM_160;
		break;
	case VME_2eSST267:
		temp_ctl |= TSI148_LCSR_OTAT_2eSSTM_267;
		break;
	case VME_2eSST320:
		temp_ctl |= TSI148_LCSR_OTAT_2eSSTM_320;
		break;
	}

	/* Setup cycle types */
	if (cycle & VME_BLT) {
		temp_ctl &= ~TSI148_LCSR_OTAT_TM_M;
		temp_ctl |= TSI148_LCSR_OTAT_TM_BLT;
	}
	if (cycle & VME_MBLT) {
		temp_ctl &= ~TSI148_LCSR_OTAT_TM_M;
		temp_ctl |= TSI148_LCSR_OTAT_TM_MBLT;
	}
	if (cycle & VME_2eVME) {
		temp_ctl &= ~TSI148_LCSR_OTAT_TM_M;
		temp_ctl |= TSI148_LCSR_OTAT_TM_2eVME;
	}
	if (cycle & VME_2eSST) {
		temp_ctl &= ~TSI148_LCSR_OTAT_TM_M;
		temp_ctl |= TSI148_LCSR_OTAT_TM_2eSST;
	}
	if (cycle & VME_2eSSTB) {
		dev_warn(tsi148_bridge->parent, "Currently not setting Broadcast Select Registers\n");
		temp_ctl &= ~TSI148_LCSR_OTAT_TM_M;
		temp_ctl |= TSI148_LCSR_OTAT_TM_2eSSTB;
	}

	/* Setup data width */
	temp_ctl &= ~TSI148_LCSR_OTAT_DBW_M;
	switch (dwidth) {
	case VME_D16:
		temp_ctl |= TSI148_LCSR_OTAT_DBW_16;
		break;
	case VME_D32:
		temp_ctl |= TSI148_LCSR_OTAT_DBW_32;
		break;
	default:
		spin_unlock(&image->lock);
		dev_err(tsi148_bridge->parent, "Invalid data width\n");
		retval = -EINVAL;
		goto err_dwidth;
	}

	/* Setup address space */
	temp_ctl &= ~TSI148_LCSR_OTAT_AMODE_M;
	switch (aspace) {
	case VME_A16:
		temp_ctl |= TSI148_LCSR_OTAT_AMODE_A16;
		break;
	case VME_A24:
		temp_ctl |= TSI148_LCSR_OTAT_AMODE_A24;
		break;
	case VME_A32:
		temp_ctl |= TSI148_LCSR_OTAT_AMODE_A32;
		break;
	case VME_A64:
		temp_ctl |= TSI148_LCSR_OTAT_AMODE_A64;
		break;
	case VME_CRCSR:
		temp_ctl |= TSI148_LCSR_OTAT_AMODE_CRCSR;
		break;
	case VME_USER1:
		temp_ctl |= TSI148_LCSR_OTAT_AMODE_USER1;
		break;
	case VME_USER2:
		temp_ctl |= TSI148_LCSR_OTAT_AMODE_USER2;
		break;
	case VME_USER3:
		temp_ctl |= TSI148_LCSR_OTAT_AMODE_USER3;
		break;
	case VME_USER4:
		temp_ctl |= TSI148_LCSR_OTAT_AMODE_USER4;
		break;
	default:
		spin_unlock(&image->lock);
		dev_err(tsi148_bridge->parent, "Invalid address space\n");
		retval = -EINVAL;
		goto err_aspace;
	}

	temp_ctl &= ~(3<<4);
	if (cycle & VME_SUPER)
		temp_ctl |= TSI148_LCSR_OTAT_SUP;
	if (cycle & VME_PROG)
		temp_ctl |= TSI148_LCSR_OTAT_PGM;

	/* Setup mapping */
	iowrite32be(pci_base_high, bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTSAU);
	iowrite32be(pci_base_low, bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTSAL);
	iowrite32be(pci_bound_high, bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTEAU);
	iowrite32be(pci_bound_low, bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTEAL);
	iowrite32be(vme_offset_high, bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTOFU);
	iowrite32be(vme_offset_low, bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTOFL);

	/* Write ctl reg without enable */
	iowrite32be(temp_ctl, bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTAT);

	if (enabled)
		temp_ctl |= TSI148_LCSR_OTAT_EN;

	iowrite32be(temp_ctl, bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTAT);

	spin_unlock(&image->lock);
	return 0;

err_aspace:
err_dwidth:
err_gran:
	tsi148_free_resource(image);
err_res:
err_window:
	return retval;

}

/*
 * Set the attributes of an outbound window.
 *
 * XXX Not parsing prefetch information.
 */
static int __tsi148_master_get(struct vme_master_resource *image, int *enabled,
	unsigned long long *vme_base, unsigned long long *size, u32 *aspace,
	u32 *cycle, u32 *dwidth)
{
	unsigned int i, ctl;
	unsigned int pci_base_low, pci_base_high;
	unsigned int pci_bound_low, pci_bound_high;
	unsigned int vme_offset_low, vme_offset_high;

	unsigned long long pci_base, pci_bound, vme_offset;
	struct tsi148_driver *bridge;

	bridge = image->parent->driver_priv;

	i = image->number;

	ctl = ioread32be(bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTAT);

	pci_base_high = ioread32be(bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTSAU);
	pci_base_low = ioread32be(bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTSAL);
	pci_bound_high = ioread32be(bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTEAU);
	pci_bound_low = ioread32be(bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTEAL);
	vme_offset_high = ioread32be(bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTOFU);
	vme_offset_low = ioread32be(bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTOFL);

	/* Convert 64-bit variables to 2x 32-bit variables */
	reg_join(pci_base_high, pci_base_low, &pci_base);
	reg_join(pci_bound_high, pci_bound_low, &pci_bound);
	reg_join(vme_offset_high, vme_offset_low, &vme_offset);

	*vme_base = pci_base + vme_offset;
	*size = (unsigned long long)(pci_bound - pci_base) + 0x10000;

	*enabled = 0;
	*aspace = 0;
	*cycle = 0;
	*dwidth = 0;

	if (ctl & TSI148_LCSR_OTAT_EN)
		*enabled = 1;

	/* Setup address space */
	if ((ctl & TSI148_LCSR_OTAT_AMODE_M) == TSI148_LCSR_OTAT_AMODE_A16)
		*aspace |= VME_A16;
	if ((ctl & TSI148_LCSR_OTAT_AMODE_M) == TSI148_LCSR_OTAT_AMODE_A24)
		*aspace |= VME_A24;
	if ((ctl & TSI148_LCSR_OTAT_AMODE_M) == TSI148_LCSR_OTAT_AMODE_A32)
		*aspace |= VME_A32;
	if ((ctl & TSI148_LCSR_OTAT_AMODE_M) == TSI148_LCSR_OTAT_AMODE_A64)
		*aspace |= VME_A64;
	if ((ctl & TSI148_LCSR_OTAT_AMODE_M) == TSI148_LCSR_OTAT_AMODE_CRCSR)
		*aspace |= VME_CRCSR;
	if ((ctl & TSI148_LCSR_OTAT_AMODE_M) == TSI148_LCSR_OTAT_AMODE_USER1)
		*aspace |= VME_USER1;
	if ((ctl & TSI148_LCSR_OTAT_AMODE_M) == TSI148_LCSR_OTAT_AMODE_USER2)
		*aspace |= VME_USER2;
	if ((ctl & TSI148_LCSR_OTAT_AMODE_M) == TSI148_LCSR_OTAT_AMODE_USER3)
		*aspace |= VME_USER3;
	if ((ctl & TSI148_LCSR_OTAT_AMODE_M) == TSI148_LCSR_OTAT_AMODE_USER4)
		*aspace |= VME_USER4;

	/* Setup 2eSST speeds */
	if ((ctl & TSI148_LCSR_OTAT_2eSSTM_M) == TSI148_LCSR_OTAT_2eSSTM_160)
		*cycle |= VME_2eSST160;
	if ((ctl & TSI148_LCSR_OTAT_2eSSTM_M) == TSI148_LCSR_OTAT_2eSSTM_267)
		*cycle |= VME_2eSST267;
	if ((ctl & TSI148_LCSR_OTAT_2eSSTM_M) == TSI148_LCSR_OTAT_2eSSTM_320)
		*cycle |= VME_2eSST320;

	/* Setup cycle types */
	if ((ctl & TSI148_LCSR_OTAT_TM_M) == TSI148_LCSR_OTAT_TM_SCT)
		*cycle |= VME_SCT;
	if ((ctl & TSI148_LCSR_OTAT_TM_M) == TSI148_LCSR_OTAT_TM_BLT)
		*cycle |= VME_BLT;
	if ((ctl & TSI148_LCSR_OTAT_TM_M) == TSI148_LCSR_OTAT_TM_MBLT)
		*cycle |= VME_MBLT;
	if ((ctl & TSI148_LCSR_OTAT_TM_M) == TSI148_LCSR_OTAT_TM_2eVME)
		*cycle |= VME_2eVME;
	if ((ctl & TSI148_LCSR_OTAT_TM_M) == TSI148_LCSR_OTAT_TM_2eSST)
		*cycle |= VME_2eSST;
	if ((ctl & TSI148_LCSR_OTAT_TM_M) == TSI148_LCSR_OTAT_TM_2eSSTB)
		*cycle |= VME_2eSSTB;

	if (ctl & TSI148_LCSR_OTAT_SUP)
		*cycle |= VME_SUPER;
	else
		*cycle |= VME_USER;

	if (ctl & TSI148_LCSR_OTAT_PGM)
		*cycle |= VME_PROG;
	else
		*cycle |= VME_DATA;

	/* Setup data width */
	if ((ctl & TSI148_LCSR_OTAT_DBW_M) == TSI148_LCSR_OTAT_DBW_16)
		*dwidth = VME_D16;
	if ((ctl & TSI148_LCSR_OTAT_DBW_M) == TSI148_LCSR_OTAT_DBW_32)
		*dwidth = VME_D32;

	return 0;
}


static int tsi148_master_get(struct vme_master_resource *image, int *enabled,
	unsigned long long *vme_base, unsigned long long *size, u32 *aspace,
	u32 *cycle, u32 *dwidth)
{
	int retval;

	spin_lock(&image->lock);

	retval = __tsi148_master_get(image, enabled, vme_base, size, aspace,
		cycle, dwidth);

	spin_unlock(&image->lock);

	return retval;
}

static ssize_t tsi148_master_read(struct vme_master_resource *image, void *buf,
	size_t count, loff_t offset)
{
	int retval, enabled;
	unsigned long long vme_base, size;
	u32 aspace, cycle, dwidth;
	struct vme_error_handler *handler = NULL;
	struct vme_bridge *tsi148_bridge;
	void __iomem *addr = image->kern_base + offset;
	unsigned int done = 0;
	unsigned int count32;

	tsi148_bridge = image->parent;

	spin_lock(&image->lock);

	if (err_chk) {
		__tsi148_master_get(image, &enabled, &vme_base, &size, &aspace,
				    &cycle, &dwidth);
		handler = vme_register_error_handler(tsi148_bridge, aspace,
						     vme_base + offset, count);
		if (!handler) {
			spin_unlock(&image->lock);
			return -ENOMEM;
		}
	}

	/* The following code handles VME address alignment. We cannot use
	 * memcpy_xxx here because it may cut data transfers in to 8-bit
	 * cycles when D16 or D32 cycles are required on the VME bus.
	 * On the other hand, the bridge itself assures that the maximum data
	 * cycle configured for the transfer is used and splits it
	 * automatically for non-aligned addresses, so we don't want the
	 * overhead of needlessly forcing small transfers for the entire cycle.
	 */
	if ((uintptr_t)addr & 0x1) {
		*(u8 *)buf = ioread8(addr);
		done += 1;
		if (done == count)
			goto out;
	}
	if ((uintptr_t)(addr + done) & 0x2) {
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
	while (done < count32) {
		*(u32 *)(buf + done) = ioread32(addr + done);
		done += 4;
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

	if (err_chk) {
		if (handler->num_errors) {
			dev_err(image->parent->parent,
				"First VME read error detected an at address 0x%llx\n",
				handler->first_error);
			retval = handler->first_error - (vme_base + offset);
		}
		vme_unregister_error_handler(handler);
	}

	spin_unlock(&image->lock);

	return retval;
}


static ssize_t tsi148_master_write(struct vme_master_resource *image, void *buf,
	size_t count, loff_t offset)
{
	int retval = 0, enabled;
	unsigned long long vme_base, size;
	u32 aspace, cycle, dwidth;
	void __iomem *addr = image->kern_base + offset;
	unsigned int done = 0;
	unsigned int count32;

	struct vme_error_handler *handler = NULL;
	struct vme_bridge *tsi148_bridge;
	struct tsi148_driver *bridge;

	tsi148_bridge = image->parent;

	bridge = tsi148_bridge->driver_priv;

	spin_lock(&image->lock);

	if (err_chk) {
		__tsi148_master_get(image, &enabled, &vme_base, &size, &aspace,
				    &cycle, &dwidth);
		handler = vme_register_error_handler(tsi148_bridge, aspace,
						     vme_base + offset, count);
		if (!handler) {
			spin_unlock(&image->lock);
			return -ENOMEM;
		}
	}

	/* Here we apply for the same strategy we do in master_read
	 * function in order to assure the correct cycles.
	 */
	if ((uintptr_t)addr & 0x1) {
		iowrite8(*(u8 *)buf, addr);
		done += 1;
		if (done == count)
			goto out;
	}
	if ((uintptr_t)(addr + done) & 0x2) {
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
	while (done < count32) {
		iowrite32(*(u32 *)(buf + done), addr + done);
		done += 4;
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

	/*
	 * Writes are posted. We need to do a read on the VME bus to flush out
	 * all of the writes before we check for errors. We can't guarantee
	 * that reading the data we have just written is safe. It is believed
	 * that there isn't any read, write re-ordering, so we can read any
	 * location in VME space, so lets read the Device ID from the tsi148's
	 * own registers as mapped into CR/CSR space.
	 *
	 * We check for saved errors in the written address range/space.
	 */

	if (err_chk) {
		ioread16(bridge->flush_image->kern_base + 0x7F000);

		if (handler->num_errors) {
			dev_warn(tsi148_bridge->parent,
				 "First VME write error detected an at address 0x%llx\n",
				 handler->first_error);
			retval = handler->first_error - (vme_base + offset);
		}
		vme_unregister_error_handler(handler);
	}

	spin_unlock(&image->lock);

	return retval;
}

/*
 * Perform an RMW cycle on the VME bus.
 *
 * Requires a previously configured master window, returns final value.
 */
static unsigned int tsi148_master_rmw(struct vme_master_resource *image,
	unsigned int mask, unsigned int compare, unsigned int swap,
	loff_t offset)
{
	unsigned long long pci_addr;
	unsigned int pci_addr_high, pci_addr_low;
	u32 tmp, result;
	int i;
	struct tsi148_driver *bridge;

	bridge = image->parent->driver_priv;

	/* Find the PCI address that maps to the desired VME address */
	i = image->number;

	/* Locking as we can only do one of these at a time */
	mutex_lock(&bridge->vme_rmw);

	/* Lock image */
	spin_lock(&image->lock);

	pci_addr_high = ioread32be(bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTSAU);
	pci_addr_low = ioread32be(bridge->base + TSI148_LCSR_OT[i] +
		TSI148_LCSR_OFFSET_OTSAL);

	reg_join(pci_addr_high, pci_addr_low, &pci_addr);
	reg_split(pci_addr + offset, &pci_addr_high, &pci_addr_low);

	/* Configure registers */
	iowrite32be(mask, bridge->base + TSI148_LCSR_RMWEN);
	iowrite32be(compare, bridge->base + TSI148_LCSR_RMWC);
	iowrite32be(swap, bridge->base + TSI148_LCSR_RMWS);
	iowrite32be(pci_addr_high, bridge->base + TSI148_LCSR_RMWAU);
	iowrite32be(pci_addr_low, bridge->base + TSI148_LCSR_RMWAL);

	/* Enable RMW */
	tmp = ioread32be(bridge->base + TSI148_LCSR_VMCTRL);
	tmp |= TSI148_LCSR_VMCTRL_RMWEN;
	iowrite32be(tmp, bridge->base + TSI148_LCSR_VMCTRL);

	/* Kick process off with a read to the required address. */
	result = ioread32be(image->kern_base + offset);

	/* Disable RMW */
	tmp = ioread32be(bridge->base + TSI148_LCSR_VMCTRL);
	tmp &= ~TSI148_LCSR_VMCTRL_RMWEN;
	iowrite32be(tmp, bridge->base + TSI148_LCSR_VMCTRL);

	spin_unlock(&image->lock);

	mutex_unlock(&bridge->vme_rmw);

	return result;
}

static int tsi148_dma_set_vme_src_attributes(struct device *dev, __be32 *attr,
	u32 aspace, u32 cycle, u32 dwidth)
{
	u32 val;

	val = be32_to_cpu(*attr);

	/* Setup 2eSST speeds */
	switch (cycle & (VME_2eSST160 | VME_2eSST267 | VME_2eSST320)) {
	case VME_2eSST160:
		val |= TSI148_LCSR_DSAT_2eSSTM_160;
		break;
	case VME_2eSST267:
		val |= TSI148_LCSR_DSAT_2eSSTM_267;
		break;
	case VME_2eSST320:
		val |= TSI148_LCSR_DSAT_2eSSTM_320;
		break;
	}

	/* Setup cycle types */
	if (cycle & VME_SCT)
		val |= TSI148_LCSR_DSAT_TM_SCT;

	if (cycle & VME_BLT)
		val |= TSI148_LCSR_DSAT_TM_BLT;

	if (cycle & VME_MBLT)
		val |= TSI148_LCSR_DSAT_TM_MBLT;

	if (cycle & VME_2eVME)
		val |= TSI148_LCSR_DSAT_TM_2eVME;

	if (cycle & VME_2eSST)
		val |= TSI148_LCSR_DSAT_TM_2eSST;

	if (cycle & VME_2eSSTB) {
		dev_err(dev, "Currently not setting Broadcast Select Registers\n");
		val |= TSI148_LCSR_DSAT_TM_2eSSTB;
	}

	/* Setup data width */
	switch (dwidth) {
	case VME_D16:
		val |= TSI148_LCSR_DSAT_DBW_16;
		break;
	case VME_D32:
		val |= TSI148_LCSR_DSAT_DBW_32;
		break;
	default:
		dev_err(dev, "Invalid data width\n");
		return -EINVAL;
	}

	/* Setup address space */
	switch (aspace) {
	case VME_A16:
		val |= TSI148_LCSR_DSAT_AMODE_A16;
		break;
	case VME_A24:
		val |= TSI148_LCSR_DSAT_AMODE_A24;
		break;
	case VME_A32:
		val |= TSI148_LCSR_DSAT_AMODE_A32;
		break;
	case VME_A64:
		val |= TSI148_LCSR_DSAT_AMODE_A64;
		break;
	case VME_CRCSR:
		val |= TSI148_LCSR_DSAT_AMODE_CRCSR;
		break;
	case VME_USER1:
		val |= TSI148_LCSR_DSAT_AMODE_USER1;
		break;
	case VME_USER2:
		val |= TSI148_LCSR_DSAT_AMODE_USER2;
		break;
	case VME_USER3:
		val |= TSI148_LCSR_DSAT_AMODE_USER3;
		break;
	case VME_USER4:
		val |= TSI148_LCSR_DSAT_AMODE_USER4;
		break;
	default:
		dev_err(dev, "Invalid address space\n");
		return -EINVAL;
	}

	if (cycle & VME_SUPER)
		val |= TSI148_LCSR_DSAT_SUP;
	if (cycle & VME_PROG)
		val |= TSI148_LCSR_DSAT_PGM;

	*attr = cpu_to_be32(val);

	return 0;
}

static int tsi148_dma_set_vme_dest_attributes(struct device *dev, __be32 *attr,
	u32 aspace, u32 cycle, u32 dwidth)
{
	u32 val;

	val = be32_to_cpu(*attr);

	/* Setup 2eSST speeds */
	switch (cycle & (VME_2eSST160 | VME_2eSST267 | VME_2eSST320)) {
	case VME_2eSST160:
		val |= TSI148_LCSR_DDAT_2eSSTM_160;
		break;
	case VME_2eSST267:
		val |= TSI148_LCSR_DDAT_2eSSTM_267;
		break;
	case VME_2eSST320:
		val |= TSI148_LCSR_DDAT_2eSSTM_320;
		break;
	}

	/* Setup cycle types */
	if (cycle & VME_SCT)
		val |= TSI148_LCSR_DDAT_TM_SCT;

	if (cycle & VME_BLT)
		val |= TSI148_LCSR_DDAT_TM_BLT;

	if (cycle & VME_MBLT)
		val |= TSI148_LCSR_DDAT_TM_MBLT;

	if (cycle & VME_2eVME)
		val |= TSI148_LCSR_DDAT_TM_2eVME;

	if (cycle & VME_2eSST)
		val |= TSI148_LCSR_DDAT_TM_2eSST;

	if (cycle & VME_2eSSTB) {
		dev_err(dev, "Currently not setting Broadcast Select Registers\n");
		val |= TSI148_LCSR_DDAT_TM_2eSSTB;
	}

	/* Setup data width */
	switch (dwidth) {
	case VME_D16:
		val |= TSI148_LCSR_DDAT_DBW_16;
		break;
	case VME_D32:
		val |= TSI148_LCSR_DDAT_DBW_32;
		break;
	default:
		dev_err(dev, "Invalid data width\n");
		return -EINVAL;
	}

	/* Setup address space */
	switch (aspace) {
	case VME_A16:
		val |= TSI148_LCSR_DDAT_AMODE_A16;
		break;
	case VME_A24:
		val |= TSI148_LCSR_DDAT_AMODE_A24;
		break;
	case VME_A32:
		val |= TSI148_LCSR_DDAT_AMODE_A32;
		break;
	case VME_A64:
		val |= TSI148_LCSR_DDAT_AMODE_A64;
		break;
	case VME_CRCSR:
		val |= TSI148_LCSR_DDAT_AMODE_CRCSR;
		break;
	case VME_USER1:
		val |= TSI148_LCSR_DDAT_AMODE_USER1;
		break;
	case VME_USER2:
		val |= TSI148_LCSR_DDAT_AMODE_USER2;
		break;
	case VME_USER3:
		val |= TSI148_LCSR_DDAT_AMODE_USER3;
		break;
	case VME_USER4:
		val |= TSI148_LCSR_DDAT_AMODE_USER4;
		break;
	default:
		dev_err(dev, "Invalid address space\n");
		return -EINVAL;
	}

	if (cycle & VME_SUPER)
		val |= TSI148_LCSR_DDAT_SUP;
	if (cycle & VME_PROG)
		val |= TSI148_LCSR_DDAT_PGM;

	*attr = cpu_to_be32(val);

	return 0;
}

/*
 * Add a link list descriptor to the list
 *
 * Note: DMA engine expects the DMA descriptor to be big endian.
 */
static int tsi148_dma_list_add(struct vme_dma_list *list,
	struct vme_dma_attr *src, struct vme_dma_attr *dest, size_t count)
{
	struct tsi148_dma_entry *entry, *prev;
	u32 address_high, address_low, val;
	struct vme_dma_pattern *pattern_attr;
	struct vme_dma_pci *pci_attr;
	struct vme_dma_vme *vme_attr;
	int retval = 0;
	struct vme_bridge *tsi148_bridge;

	tsi148_bridge = list->parent->parent;

	/* Descriptor must be aligned on 64-bit boundaries */
	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		retval = -ENOMEM;
		goto err_mem;
	}

	/* Test descriptor alignment */
	if ((unsigned long)&entry->descriptor & 0x7) {
		dev_err(tsi148_bridge->parent, "Descriptor not aligned to 8 byte boundary as required: %p\n",
			&entry->descriptor);
		retval = -EINVAL;
		goto err_align;
	}

	/* Given we are going to fill out the structure, we probably don't
	 * need to zero it, but better safe than sorry for now.
	 */
	memset(&entry->descriptor, 0, sizeof(entry->descriptor));

	/* Fill out source part */
	switch (src->type) {
	case VME_DMA_PATTERN:
		pattern_attr = src->private;

		entry->descriptor.dsal = cpu_to_be32(pattern_attr->pattern);

		val = TSI148_LCSR_DSAT_TYP_PAT;

		/* Default behaviour is 32 bit pattern */
		if (pattern_attr->type & VME_DMA_PATTERN_BYTE)
			val |= TSI148_LCSR_DSAT_PSZ;

		/* It seems that the default behaviour is to increment */
		if ((pattern_attr->type & VME_DMA_PATTERN_INCREMENT) == 0)
			val |= TSI148_LCSR_DSAT_NIN;
		entry->descriptor.dsat = cpu_to_be32(val);
		break;
	case VME_DMA_PCI:
		pci_attr = src->private;

		reg_split((unsigned long long)pci_attr->address, &address_high,
			&address_low);
		entry->descriptor.dsau = cpu_to_be32(address_high);
		entry->descriptor.dsal = cpu_to_be32(address_low);
		entry->descriptor.dsat = cpu_to_be32(TSI148_LCSR_DSAT_TYP_PCI);
		break;
	case VME_DMA_VME:
		vme_attr = src->private;

		reg_split((unsigned long long)vme_attr->address, &address_high,
			&address_low);
		entry->descriptor.dsau = cpu_to_be32(address_high);
		entry->descriptor.dsal = cpu_to_be32(address_low);
		entry->descriptor.dsat = cpu_to_be32(TSI148_LCSR_DSAT_TYP_VME);

		retval = tsi148_dma_set_vme_src_attributes(
			tsi148_bridge->parent, &entry->descriptor.dsat,
			vme_attr->aspace, vme_attr->cycle, vme_attr->dwidth);
		if (retval < 0)
			goto err_source;
		break;
	default:
		dev_err(tsi148_bridge->parent, "Invalid source type\n");
		retval = -EINVAL;
		goto err_source;
	}

	/* Assume last link - this will be over-written by adding another */
	entry->descriptor.dnlau = cpu_to_be32(0);
	entry->descriptor.dnlal = cpu_to_be32(TSI148_LCSR_DNLAL_LLA);

	/* Fill out destination part */
	switch (dest->type) {
	case VME_DMA_PCI:
		pci_attr = dest->private;

		reg_split((unsigned long long)pci_attr->address, &address_high,
			&address_low);
		entry->descriptor.ddau = cpu_to_be32(address_high);
		entry->descriptor.ddal = cpu_to_be32(address_low);
		entry->descriptor.ddat = cpu_to_be32(TSI148_LCSR_DDAT_TYP_PCI);
		break;
	case VME_DMA_VME:
		vme_attr = dest->private;

		reg_split((unsigned long long)vme_attr->address, &address_high,
			&address_low);
		entry->descriptor.ddau = cpu_to_be32(address_high);
		entry->descriptor.ddal = cpu_to_be32(address_low);
		entry->descriptor.ddat = cpu_to_be32(TSI148_LCSR_DDAT_TYP_VME);

		retval = tsi148_dma_set_vme_dest_attributes(
			tsi148_bridge->parent, &entry->descriptor.ddat,
			vme_attr->aspace, vme_attr->cycle, vme_attr->dwidth);
		if (retval < 0)
			goto err_dest;
		break;
	default:
		dev_err(tsi148_bridge->parent, "Invalid destination type\n");
		retval = -EINVAL;
		goto err_dest;
	}

	/* Fill out count */
	entry->descriptor.dcnt = cpu_to_be32((u32)count);

	/* Add to list */
	list_add_tail(&entry->list, &list->entries);

	entry->dma_handle = dma_map_single(tsi148_bridge->parent,
					   &entry->descriptor,
					   sizeof(entry->descriptor),
					   DMA_TO_DEVICE);
	if (dma_mapping_error(tsi148_bridge->parent, entry->dma_handle)) {
		dev_err(tsi148_bridge->parent, "DMA mapping error\n");
		retval = -EINVAL;
		goto err_dma;
	}

	/* Fill out previous descriptors "Next Address" */
	if (entry->list.prev != &list->entries) {
		reg_split((unsigned long long)entry->dma_handle, &address_high,
			&address_low);
		prev = list_entry(entry->list.prev, struct tsi148_dma_entry,
				  list);
		prev->descriptor.dnlau = cpu_to_be32(address_high);
		prev->descriptor.dnlal = cpu_to_be32(address_low);

	}

	return 0;

err_dma:
	list_del(&entry->list);
err_dest:
err_source:
err_align:
		kfree(entry);
err_mem:
	return retval;
}

/*
 * Check to see if the provided DMA channel is busy.
 */
static int tsi148_dma_busy(struct vme_bridge *tsi148_bridge, int channel)
{
	u32 tmp;
	struct tsi148_driver *bridge;

	bridge = tsi148_bridge->driver_priv;

	tmp = ioread32be(bridge->base + TSI148_LCSR_DMA[channel] +
		TSI148_LCSR_OFFSET_DSTA);

	if (tmp & TSI148_LCSR_DSTA_BSY)
		return 0;
	else
		return 1;

}

/*
 * Execute a previously generated link list
 *
 * XXX Need to provide control register configuration.
 */
static int tsi148_dma_list_exec(struct vme_dma_list *list)
{
	struct vme_dma_resource *ctrlr;
	int channel, retval;
	struct tsi148_dma_entry *entry;
	u32 bus_addr_high, bus_addr_low;
	u32 val, dctlreg = 0;
	struct vme_bridge *tsi148_bridge;
	struct tsi148_driver *bridge;

	ctrlr = list->parent;

	tsi148_bridge = ctrlr->parent;

	bridge = tsi148_bridge->driver_priv;

	mutex_lock(&ctrlr->mtx);

	channel = ctrlr->number;

	if (!list_empty(&ctrlr->running)) {
		/*
		 * XXX We have an active DMA transfer and currently haven't
		 *     sorted out the mechanism for "pending" DMA transfers.
		 *     Return busy.
		 */
		/* Need to add to pending here */
		mutex_unlock(&ctrlr->mtx);
		return -EBUSY;
	}

	list_add(&list->list, &ctrlr->running);

	/* Get first bus address and write into registers */
	entry = list_first_entry(&list->entries, struct tsi148_dma_entry,
		list);

	mutex_unlock(&ctrlr->mtx);

	reg_split(entry->dma_handle, &bus_addr_high, &bus_addr_low);

	iowrite32be(bus_addr_high, bridge->base +
		TSI148_LCSR_DMA[channel] + TSI148_LCSR_OFFSET_DNLAU);
	iowrite32be(bus_addr_low, bridge->base +
		TSI148_LCSR_DMA[channel] + TSI148_LCSR_OFFSET_DNLAL);

	dctlreg = ioread32be(bridge->base + TSI148_LCSR_DMA[channel] +
		TSI148_LCSR_OFFSET_DCTL);

	/* Start the operation */
	iowrite32be(dctlreg | TSI148_LCSR_DCTL_DGO, bridge->base +
		TSI148_LCSR_DMA[channel] + TSI148_LCSR_OFFSET_DCTL);

	retval = wait_event_interruptible(bridge->dma_queue[channel],
		tsi148_dma_busy(ctrlr->parent, channel));

	if (retval) {
		iowrite32be(dctlreg | TSI148_LCSR_DCTL_ABT, bridge->base +
			TSI148_LCSR_DMA[channel] + TSI148_LCSR_OFFSET_DCTL);
		/* Wait for the operation to abort */
		wait_event(bridge->dma_queue[channel],
			   tsi148_dma_busy(ctrlr->parent, channel));
		retval = -EINTR;
		goto exit;
	}

	/*
	 * Read status register, this register is valid until we kick off a
	 * new transfer.
	 */
	val = ioread32be(bridge->base + TSI148_LCSR_DMA[channel] +
		TSI148_LCSR_OFFSET_DSTA);

	if (val & TSI148_LCSR_DSTA_VBE) {
		dev_err(tsi148_bridge->parent, "DMA Error. DSTA=%08X\n", val);
		retval = -EIO;
	}

exit:
	/* Remove list from running list */
	mutex_lock(&ctrlr->mtx);
	list_del(&list->list);
	mutex_unlock(&ctrlr->mtx);

	return retval;
}

/*
 * Clean up a previously generated link list
 *
 * We have a separate function, don't assume that the chain can't be reused.
 */
static int tsi148_dma_list_empty(struct vme_dma_list *list)
{
	struct list_head *pos, *temp;
	struct tsi148_dma_entry *entry;

	struct vme_bridge *tsi148_bridge = list->parent->parent;

	/* detach and free each entry */
	list_for_each_safe(pos, temp, &list->entries) {
		list_del(pos);
		entry = list_entry(pos, struct tsi148_dma_entry, list);

		dma_unmap_single(tsi148_bridge->parent, entry->dma_handle,
			sizeof(struct tsi148_dma_descriptor), DMA_TO_DEVICE);
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
static int tsi148_lm_set(struct vme_lm_resource *lm, unsigned long long lm_base,
	u32 aspace, u32 cycle)
{
	u32 lm_base_high, lm_base_low, lm_ctl = 0;
	int i;
	struct vme_bridge *tsi148_bridge;
	struct tsi148_driver *bridge;

	tsi148_bridge = lm->parent;

	bridge = tsi148_bridge->driver_priv;

	mutex_lock(&lm->mtx);

	/* If we already have a callback attached, we can't move it! */
	for (i = 0; i < lm->monitors; i++) {
		if (bridge->lm_callback[i]) {
			mutex_unlock(&lm->mtx);
			dev_err(tsi148_bridge->parent, "Location monitor callback attached, can't reset\n");
			return -EBUSY;
		}
	}

	switch (aspace) {
	case VME_A16:
		lm_ctl |= TSI148_LCSR_LMAT_AS_A16;
		break;
	case VME_A24:
		lm_ctl |= TSI148_LCSR_LMAT_AS_A24;
		break;
	case VME_A32:
		lm_ctl |= TSI148_LCSR_LMAT_AS_A32;
		break;
	case VME_A64:
		lm_ctl |= TSI148_LCSR_LMAT_AS_A64;
		break;
	default:
		mutex_unlock(&lm->mtx);
		dev_err(tsi148_bridge->parent, "Invalid address space\n");
		return -EINVAL;
	}

	if (cycle & VME_SUPER)
		lm_ctl |= TSI148_LCSR_LMAT_SUPR;
	if (cycle & VME_USER)
		lm_ctl |= TSI148_LCSR_LMAT_NPRIV;
	if (cycle & VME_PROG)
		lm_ctl |= TSI148_LCSR_LMAT_PGM;
	if (cycle & VME_DATA)
		lm_ctl |= TSI148_LCSR_LMAT_DATA;

	reg_split(lm_base, &lm_base_high, &lm_base_low);

	iowrite32be(lm_base_high, bridge->base + TSI148_LCSR_LMBAU);
	iowrite32be(lm_base_low, bridge->base + TSI148_LCSR_LMBAL);
	iowrite32be(lm_ctl, bridge->base + TSI148_LCSR_LMAT);

	mutex_unlock(&lm->mtx);

	return 0;
}

/* Get configuration of the callback monitor and return whether it is enabled
 * or disabled.
 */
static int tsi148_lm_get(struct vme_lm_resource *lm,
	unsigned long long *lm_base, u32 *aspace, u32 *cycle)
{
	u32 lm_base_high, lm_base_low, lm_ctl, enabled = 0;
	struct tsi148_driver *bridge;

	bridge = lm->parent->driver_priv;

	mutex_lock(&lm->mtx);

	lm_base_high = ioread32be(bridge->base + TSI148_LCSR_LMBAU);
	lm_base_low = ioread32be(bridge->base + TSI148_LCSR_LMBAL);
	lm_ctl = ioread32be(bridge->base + TSI148_LCSR_LMAT);

	reg_join(lm_base_high, lm_base_low, lm_base);

	if (lm_ctl & TSI148_LCSR_LMAT_EN)
		enabled = 1;

	if ((lm_ctl & TSI148_LCSR_LMAT_AS_M) == TSI148_LCSR_LMAT_AS_A16)
		*aspace |= VME_A16;

	if ((lm_ctl & TSI148_LCSR_LMAT_AS_M) == TSI148_LCSR_LMAT_AS_A24)
		*aspace |= VME_A24;

	if ((lm_ctl & TSI148_LCSR_LMAT_AS_M) == TSI148_LCSR_LMAT_AS_A32)
		*aspace |= VME_A32;

	if ((lm_ctl & TSI148_LCSR_LMAT_AS_M) == TSI148_LCSR_LMAT_AS_A64)
		*aspace |= VME_A64;


	if (lm_ctl & TSI148_LCSR_LMAT_SUPR)
		*cycle |= VME_SUPER;
	if (lm_ctl & TSI148_LCSR_LMAT_NPRIV)
		*cycle |= VME_USER;
	if (lm_ctl & TSI148_LCSR_LMAT_PGM)
		*cycle |= VME_PROG;
	if (lm_ctl & TSI148_LCSR_LMAT_DATA)
		*cycle |= VME_DATA;

	mutex_unlock(&lm->mtx);

	return enabled;
}

/*
 * Attach a callback to a specific location monitor.
 *
 * Callback will be passed the monitor triggered.
 */
static int tsi148_lm_attach(struct vme_lm_resource *lm, int monitor,
	void (*callback)(void *), void *data)
{
	u32 lm_ctl, tmp;
	struct vme_bridge *tsi148_bridge;
	struct tsi148_driver *bridge;

	tsi148_bridge = lm->parent;

	bridge = tsi148_bridge->driver_priv;

	mutex_lock(&lm->mtx);

	/* Ensure that the location monitor is configured - need PGM or DATA */
	lm_ctl = ioread32be(bridge->base + TSI148_LCSR_LMAT);
	if ((lm_ctl & (TSI148_LCSR_LMAT_PGM | TSI148_LCSR_LMAT_DATA)) == 0) {
		mutex_unlock(&lm->mtx);
		dev_err(tsi148_bridge->parent, "Location monitor not properly configured\n");
		return -EINVAL;
	}

	/* Check that a callback isn't already attached */
	if (bridge->lm_callback[monitor]) {
		mutex_unlock(&lm->mtx);
		dev_err(tsi148_bridge->parent, "Existing callback attached\n");
		return -EBUSY;
	}

	/* Attach callback */
	bridge->lm_callback[monitor] = callback;
	bridge->lm_data[monitor] = data;

	/* Enable Location Monitor interrupt */
	tmp = ioread32be(bridge->base + TSI148_LCSR_INTEN);
	tmp |= TSI148_LCSR_INTEN_LMEN[monitor];
	iowrite32be(tmp, bridge->base + TSI148_LCSR_INTEN);

	tmp = ioread32be(bridge->base + TSI148_LCSR_INTEO);
	tmp |= TSI148_LCSR_INTEO_LMEO[monitor];
	iowrite32be(tmp, bridge->base + TSI148_LCSR_INTEO);

	/* Ensure that global Location Monitor Enable set */
	if ((lm_ctl & TSI148_LCSR_LMAT_EN) == 0) {
		lm_ctl |= TSI148_LCSR_LMAT_EN;
		iowrite32be(lm_ctl, bridge->base + TSI148_LCSR_LMAT);
	}

	mutex_unlock(&lm->mtx);

	return 0;
}

/*
 * Detach a callback function forn a specific location monitor.
 */
static int tsi148_lm_detach(struct vme_lm_resource *lm, int monitor)
{
	u32 lm_en, tmp;
	struct tsi148_driver *bridge;

	bridge = lm->parent->driver_priv;

	mutex_lock(&lm->mtx);

	/* Disable Location Monitor and ensure previous interrupts are clear */
	lm_en = ioread32be(bridge->base + TSI148_LCSR_INTEN);
	lm_en &= ~TSI148_LCSR_INTEN_LMEN[monitor];
	iowrite32be(lm_en, bridge->base + TSI148_LCSR_INTEN);

	tmp = ioread32be(bridge->base + TSI148_LCSR_INTEO);
	tmp &= ~TSI148_LCSR_INTEO_LMEO[monitor];
	iowrite32be(tmp, bridge->base + TSI148_LCSR_INTEO);

	iowrite32be(TSI148_LCSR_INTC_LMC[monitor],
		 bridge->base + TSI148_LCSR_INTC);

	/* Detach callback */
	bridge->lm_callback[monitor] = NULL;
	bridge->lm_data[monitor] = NULL;

	/* If all location monitors disabled, disable global Location Monitor */
	if ((lm_en & (TSI148_LCSR_INTS_LM0S | TSI148_LCSR_INTS_LM1S |
			TSI148_LCSR_INTS_LM2S | TSI148_LCSR_INTS_LM3S)) == 0) {
		tmp = ioread32be(bridge->base + TSI148_LCSR_LMAT);
		tmp &= ~TSI148_LCSR_LMAT_EN;
		iowrite32be(tmp, bridge->base + TSI148_LCSR_LMAT);
	}

	mutex_unlock(&lm->mtx);

	return 0;
}

/*
 * Determine Geographical Addressing
 */
static int tsi148_slot_get(struct vme_bridge *tsi148_bridge)
{
	u32 slot = 0;
	struct tsi148_driver *bridge;

	bridge = tsi148_bridge->driver_priv;

	if (!geoid) {
		slot = ioread32be(bridge->base + TSI148_LCSR_VSTAT);
		slot = slot & TSI148_LCSR_VSTAT_GA_M;
	} else
		slot = geoid;

	return (int)slot;
}

static void *tsi148_alloc_consistent(struct device *parent, size_t size,
	dma_addr_t *dma)
{
	struct pci_dev *pdev;

	/* Find pci_dev container of dev */
	pdev = to_pci_dev(parent);

	return dma_alloc_coherent(&pdev->dev, size, dma, GFP_KERNEL);
}

static void tsi148_free_consistent(struct device *parent, size_t size,
	void *vaddr, dma_addr_t dma)
{
	struct pci_dev *pdev;

	/* Find pci_dev container of dev */
	pdev = to_pci_dev(parent);

	dma_free_coherent(&pdev->dev, size, vaddr, dma);
}

/*
 * Configure CR/CSR space
 *
 * Access to the CR/CSR can be configured at power-up. The location of the
 * CR/CSR registers in the CR/CSR address space is determined by the boards
 * Auto-ID or Geographic address. This function ensures that the window is
 * enabled at an offset consistent with the boards geopgraphic address.
 *
 * Each board has a 512kB window, with the highest 4kB being used for the
 * boards registers, this means there is a fix length 508kB window which must
 * be mapped onto PCI memory.
 */
static int tsi148_crcsr_init(struct vme_bridge *tsi148_bridge,
	struct pci_dev *pdev)
{
	u32 cbar, crat, vstat;
	u32 crcsr_bus_high, crcsr_bus_low;
	int retval;
	struct tsi148_driver *bridge;

	bridge = tsi148_bridge->driver_priv;

	/* Allocate mem for CR/CSR image */
	bridge->crcsr_kernel = dma_alloc_coherent(&pdev->dev,
						  VME_CRCSR_BUF_SIZE,
						  &bridge->crcsr_bus, GFP_KERNEL);
	if (!bridge->crcsr_kernel) {
		dev_err(tsi148_bridge->parent, "Failed to allocate memory for CR/CSR image\n");
		return -ENOMEM;
	}

	reg_split(bridge->crcsr_bus, &crcsr_bus_high, &crcsr_bus_low);

	iowrite32be(crcsr_bus_high, bridge->base + TSI148_LCSR_CROU);
	iowrite32be(crcsr_bus_low, bridge->base + TSI148_LCSR_CROL);

	/* Ensure that the CR/CSR is configured at the correct offset */
	cbar = ioread32be(bridge->base + TSI148_CBAR);
	cbar = (cbar & TSI148_CRCSR_CBAR_M)>>3;

	vstat = tsi148_slot_get(tsi148_bridge);

	if (cbar != vstat) {
		cbar = vstat;
		dev_info(tsi148_bridge->parent, "Setting CR/CSR offset\n");
		iowrite32be(cbar<<3, bridge->base + TSI148_CBAR);
	}
	dev_info(tsi148_bridge->parent, "CR/CSR Offset: %d\n", cbar);

	crat = ioread32be(bridge->base + TSI148_LCSR_CRAT);
	if (crat & TSI148_LCSR_CRAT_EN)
		dev_info(tsi148_bridge->parent, "CR/CSR already enabled\n");
	else {
		dev_info(tsi148_bridge->parent, "Enabling CR/CSR space\n");
		iowrite32be(crat | TSI148_LCSR_CRAT_EN,
			bridge->base + TSI148_LCSR_CRAT);
	}

	/* If we want flushed, error-checked writes, set up a window
	 * over the CR/CSR registers. We read from here to safely flush
	 * through VME writes.
	 */
	if (err_chk) {
		retval = tsi148_master_set(bridge->flush_image, 1,
			(vstat * 0x80000), 0x80000, VME_CRCSR, VME_SCT,
			VME_D16);
		if (retval)
			dev_err(tsi148_bridge->parent, "Configuring flush image failed\n");
	}

	return 0;

}

static void tsi148_crcsr_exit(struct vme_bridge *tsi148_bridge,
	struct pci_dev *pdev)
{
	u32 crat;
	struct tsi148_driver *bridge;

	bridge = tsi148_bridge->driver_priv;

	/* Turn off CR/CSR space */
	crat = ioread32be(bridge->base + TSI148_LCSR_CRAT);
	iowrite32be(crat & ~TSI148_LCSR_CRAT_EN,
		bridge->base + TSI148_LCSR_CRAT);

	/* Free image */
	iowrite32be(0, bridge->base + TSI148_LCSR_CROU);
	iowrite32be(0, bridge->base + TSI148_LCSR_CROL);

	dma_free_coherent(&pdev->dev, VME_CRCSR_BUF_SIZE,
			  bridge->crcsr_kernel, bridge->crcsr_bus);
}

static int tsi148_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int retval, i, master_num;
	u32 data;
	struct list_head *pos = NULL, *n;
	struct vme_bridge *tsi148_bridge;
	struct tsi148_driver *tsi148_device;
	struct vme_master_resource *master_image;
	struct vme_slave_resource *slave_image;
	struct vme_dma_resource *dma_ctrlr;
	struct vme_lm_resource *lm;

	/* If we want to support more than one of each bridge, we need to
	 * dynamically generate this so we get one per device
	 */
	tsi148_bridge = kzalloc(sizeof(*tsi148_bridge), GFP_KERNEL);
	if (!tsi148_bridge) {
		retval = -ENOMEM;
		goto err_struct;
	}
	vme_init_bridge(tsi148_bridge);

	tsi148_device = kzalloc(sizeof(*tsi148_device), GFP_KERNEL);
	if (!tsi148_device) {
		retval = -ENOMEM;
		goto err_driver;
	}

	tsi148_bridge->driver_priv = tsi148_device;

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
	tsi148_device->base = ioremap(pci_resource_start(pdev, 0),
		4096);
	if (!tsi148_device->base) {
		dev_err(&pdev->dev, "Unable to remap CRG region\n");
		retval = -EIO;
		goto err_remap;
	}

	/* Check to see if the mapping worked out */
	data = ioread32(tsi148_device->base + TSI148_PCFS_ID) & 0x0000FFFF;
	if (data != PCI_VENDOR_ID_TUNDRA) {
		dev_err(&pdev->dev, "CRG region check failed\n");
		retval = -EIO;
		goto err_test;
	}

	/* Initialize wait queues & mutual exclusion flags */
	init_waitqueue_head(&tsi148_device->dma_queue[0]);
	init_waitqueue_head(&tsi148_device->dma_queue[1]);
	init_waitqueue_head(&tsi148_device->iack_queue);
	mutex_init(&tsi148_device->vme_int);
	mutex_init(&tsi148_device->vme_rmw);

	tsi148_bridge->parent = &pdev->dev;
	strcpy(tsi148_bridge->name, driver_name);

	/* Setup IRQ */
	retval = tsi148_irq_init(tsi148_bridge);
	if (retval != 0) {
		dev_err(&pdev->dev, "Chip Initialization failed.\n");
		goto err_irq;
	}

	/* If we are going to flush writes, we need to read from the VME bus.
	 * We need to do this safely, thus we read the devices own CR/CSR
	 * register. To do this we must set up a window in CR/CSR space and
	 * hence have one less master window resource available.
	 */
	master_num = TSI148_MAX_MASTER;
	if (err_chk) {
		master_num--;

		tsi148_device->flush_image =
			kmalloc(sizeof(*tsi148_device->flush_image),
				GFP_KERNEL);
		if (!tsi148_device->flush_image) {
			retval = -ENOMEM;
			goto err_master;
		}
		tsi148_device->flush_image->parent = tsi148_bridge;
		spin_lock_init(&tsi148_device->flush_image->lock);
		tsi148_device->flush_image->locked = 1;
		tsi148_device->flush_image->number = master_num;
		memset(&tsi148_device->flush_image->bus_resource, 0,
		       sizeof(tsi148_device->flush_image->bus_resource));
		tsi148_device->flush_image->kern_base  = NULL;
	}

	/* Add master windows to list */
	for (i = 0; i < master_num; i++) {
		master_image = kmalloc(sizeof(*master_image), GFP_KERNEL);
		if (!master_image) {
			retval = -ENOMEM;
			goto err_master;
		}
		master_image->parent = tsi148_bridge;
		spin_lock_init(&master_image->lock);
		master_image->locked = 0;
		master_image->number = i;
		master_image->address_attr = VME_A16 | VME_A24 | VME_A32 |
			VME_A64 | VME_CRCSR | VME_USER1 | VME_USER2 |
			VME_USER3 | VME_USER4;
		master_image->cycle_attr = VME_SCT | VME_BLT | VME_MBLT |
			VME_2eVME | VME_2eSST | VME_2eSSTB | VME_2eSST160 |
			VME_2eSST267 | VME_2eSST320 | VME_SUPER | VME_USER |
			VME_PROG | VME_DATA;
		master_image->width_attr = VME_D16 | VME_D32;
		memset(&master_image->bus_resource, 0,
		       sizeof(master_image->bus_resource));
		master_image->kern_base  = NULL;
		list_add_tail(&master_image->list,
			&tsi148_bridge->master_resources);
	}

	/* Add slave windows to list */
	for (i = 0; i < TSI148_MAX_SLAVE; i++) {
		slave_image = kmalloc(sizeof(*slave_image), GFP_KERNEL);
		if (!slave_image) {
			retval = -ENOMEM;
			goto err_slave;
		}
		slave_image->parent = tsi148_bridge;
		mutex_init(&slave_image->mtx);
		slave_image->locked = 0;
		slave_image->number = i;
		slave_image->address_attr = VME_A16 | VME_A24 | VME_A32 |
			VME_A64;
		slave_image->cycle_attr = VME_SCT | VME_BLT | VME_MBLT |
			VME_2eVME | VME_2eSST | VME_2eSSTB | VME_2eSST160 |
			VME_2eSST267 | VME_2eSST320 | VME_SUPER | VME_USER |
			VME_PROG | VME_DATA;
		list_add_tail(&slave_image->list,
			&tsi148_bridge->slave_resources);
	}

	/* Add dma engines to list */
	for (i = 0; i < TSI148_MAX_DMA; i++) {
		dma_ctrlr = kmalloc(sizeof(*dma_ctrlr), GFP_KERNEL);
		if (!dma_ctrlr) {
			retval = -ENOMEM;
			goto err_dma;
		}
		dma_ctrlr->parent = tsi148_bridge;
		mutex_init(&dma_ctrlr->mtx);
		dma_ctrlr->locked = 0;
		dma_ctrlr->number = i;
		dma_ctrlr->route_attr = VME_DMA_VME_TO_MEM |
			VME_DMA_MEM_TO_VME | VME_DMA_VME_TO_VME |
			VME_DMA_MEM_TO_MEM | VME_DMA_PATTERN_TO_VME |
			VME_DMA_PATTERN_TO_MEM;
		INIT_LIST_HEAD(&dma_ctrlr->pending);
		INIT_LIST_HEAD(&dma_ctrlr->running);
		list_add_tail(&dma_ctrlr->list,
			&tsi148_bridge->dma_resources);
	}

	/* Add location monitor to list */
	lm = kmalloc(sizeof(*lm), GFP_KERNEL);
	if (!lm) {
		retval = -ENOMEM;
		goto err_lm;
	}
	lm->parent = tsi148_bridge;
	mutex_init(&lm->mtx);
	lm->locked = 0;
	lm->number = 1;
	lm->monitors = 4;
	list_add_tail(&lm->list, &tsi148_bridge->lm_resources);

	tsi148_bridge->slave_get = tsi148_slave_get;
	tsi148_bridge->slave_set = tsi148_slave_set;
	tsi148_bridge->master_get = tsi148_master_get;
	tsi148_bridge->master_set = tsi148_master_set;
	tsi148_bridge->master_read = tsi148_master_read;
	tsi148_bridge->master_write = tsi148_master_write;
	tsi148_bridge->master_rmw = tsi148_master_rmw;
	tsi148_bridge->dma_list_add = tsi148_dma_list_add;
	tsi148_bridge->dma_list_exec = tsi148_dma_list_exec;
	tsi148_bridge->dma_list_empty = tsi148_dma_list_empty;
	tsi148_bridge->irq_set = tsi148_irq_set;
	tsi148_bridge->irq_generate = tsi148_irq_generate;
	tsi148_bridge->lm_set = tsi148_lm_set;
	tsi148_bridge->lm_get = tsi148_lm_get;
	tsi148_bridge->lm_attach = tsi148_lm_attach;
	tsi148_bridge->lm_detach = tsi148_lm_detach;
	tsi148_bridge->slot_get = tsi148_slot_get;
	tsi148_bridge->alloc_consistent = tsi148_alloc_consistent;
	tsi148_bridge->free_consistent = tsi148_free_consistent;

	data = ioread32be(tsi148_device->base + TSI148_LCSR_VSTAT);
	dev_info(&pdev->dev, "Board is%s the VME system controller\n",
		(data & TSI148_LCSR_VSTAT_SCONS) ? "" : " not");
	if (!geoid)
		dev_info(&pdev->dev, "VME geographical address is %d\n",
			data & TSI148_LCSR_VSTAT_GA_M);
	else
		dev_info(&pdev->dev, "VME geographical address is set to %d\n",
			geoid);

	dev_info(&pdev->dev, "VME Write and flush and error check is %s\n",
		err_chk ? "enabled" : "disabled");

	retval = tsi148_crcsr_init(tsi148_bridge, pdev);
	if (retval) {
		dev_err(&pdev->dev, "CR/CSR configuration failed.\n");
		goto err_crcsr;
	}

	retval = vme_register_bridge(tsi148_bridge);
	if (retval != 0) {
		dev_err(&pdev->dev, "Chip Registration failed.\n");
		goto err_reg;
	}

	pci_set_drvdata(pdev, tsi148_bridge);

	/* Clear VME bus "board fail", and "power-up reset" lines */
	data = ioread32be(tsi148_device->base + TSI148_LCSR_VSTAT);
	data &= ~TSI148_LCSR_VSTAT_BRDFL;
	data |= TSI148_LCSR_VSTAT_CPURST;
	iowrite32be(data, tsi148_device->base + TSI148_LCSR_VSTAT);

	return 0;

err_reg:
	tsi148_crcsr_exit(tsi148_bridge, pdev);
err_crcsr:
err_lm:
	/* resources are stored in link list */
	list_for_each_safe(pos, n, &tsi148_bridge->lm_resources) {
		lm = list_entry(pos, struct vme_lm_resource, list);
		list_del(pos);
		kfree(lm);
	}
err_dma:
	/* resources are stored in link list */
	list_for_each_safe(pos, n, &tsi148_bridge->dma_resources) {
		dma_ctrlr = list_entry(pos, struct vme_dma_resource, list);
		list_del(pos);
		kfree(dma_ctrlr);
	}
err_slave:
	/* resources are stored in link list */
	list_for_each_safe(pos, n, &tsi148_bridge->slave_resources) {
		slave_image = list_entry(pos, struct vme_slave_resource, list);
		list_del(pos);
		kfree(slave_image);
	}
err_master:
	/* resources are stored in link list */
	list_for_each_safe(pos, n, &tsi148_bridge->master_resources) {
		master_image = list_entry(pos, struct vme_master_resource,
			list);
		list_del(pos);
		kfree(master_image);
	}

	tsi148_irq_exit(tsi148_bridge, pdev);
err_irq:
err_test:
	iounmap(tsi148_device->base);
err_remap:
	pci_release_regions(pdev);
err_resource:
	pci_disable_device(pdev);
err_enable:
	kfree(tsi148_device);
err_driver:
	kfree(tsi148_bridge);
err_struct:
	return retval;

}

static void tsi148_remove(struct pci_dev *pdev)
{
	struct list_head *pos = NULL;
	struct list_head *tmplist;
	struct vme_master_resource *master_image;
	struct vme_slave_resource *slave_image;
	struct vme_dma_resource *dma_ctrlr;
	int i;
	struct tsi148_driver *bridge;
	struct vme_bridge *tsi148_bridge = pci_get_drvdata(pdev);

	bridge = tsi148_bridge->driver_priv;


	dev_dbg(&pdev->dev, "Driver is being unloaded.\n");

	/*
	 *  Shutdown all inbound and outbound windows.
	 */
	for (i = 0; i < 8; i++) {
		iowrite32be(0, bridge->base + TSI148_LCSR_IT[i] +
			TSI148_LCSR_OFFSET_ITAT);
		iowrite32be(0, bridge->base + TSI148_LCSR_OT[i] +
			TSI148_LCSR_OFFSET_OTAT);
	}

	/*
	 *  Shutdown Location monitor.
	 */
	iowrite32be(0, bridge->base + TSI148_LCSR_LMAT);

	/*
	 *  Shutdown CRG map.
	 */
	iowrite32be(0, bridge->base + TSI148_LCSR_CSRAT);

	/*
	 *  Clear error status.
	 */
	iowrite32be(0xFFFFFFFF, bridge->base + TSI148_LCSR_EDPAT);
	iowrite32be(0xFFFFFFFF, bridge->base + TSI148_LCSR_VEAT);
	iowrite32be(0x07000700, bridge->base + TSI148_LCSR_PSTAT);

	/*
	 *  Remove VIRQ interrupt (if any)
	 */
	if (ioread32be(bridge->base + TSI148_LCSR_VICR) & 0x800)
		iowrite32be(0x8000, bridge->base + TSI148_LCSR_VICR);

	/*
	 *  Map all Interrupts to PCI INTA
	 */
	iowrite32be(0x0, bridge->base + TSI148_LCSR_INTM1);
	iowrite32be(0x0, bridge->base + TSI148_LCSR_INTM2);

	tsi148_irq_exit(tsi148_bridge, pdev);

	vme_unregister_bridge(tsi148_bridge);

	tsi148_crcsr_exit(tsi148_bridge, pdev);

	/* resources are stored in link list */
	list_for_each_safe(pos, tmplist, &tsi148_bridge->dma_resources) {
		dma_ctrlr = list_entry(pos, struct vme_dma_resource, list);
		list_del(pos);
		kfree(dma_ctrlr);
	}

	/* resources are stored in link list */
	list_for_each_safe(pos, tmplist, &tsi148_bridge->slave_resources) {
		slave_image = list_entry(pos, struct vme_slave_resource, list);
		list_del(pos);
		kfree(slave_image);
	}

	/* resources are stored in link list */
	list_for_each_safe(pos, tmplist, &tsi148_bridge->master_resources) {
		master_image = list_entry(pos, struct vme_master_resource,
			list);
		list_del(pos);
		kfree(master_image);
	}

	iounmap(bridge->base);

	pci_release_regions(pdev);

	pci_disable_device(pdev);

	kfree(tsi148_bridge->driver_priv);

	kfree(tsi148_bridge);
}

module_pci_driver(tsi148_driver);

MODULE_PARM_DESC(err_chk, "Check for VME errors on reads and writes");
module_param(err_chk, bool, 0);

MODULE_PARM_DESC(geoid, "Override geographical addressing");
module_param(geoid, int, 0);

MODULE_DESCRIPTION("VME driver for the Tundra Tempe VME bridge");
MODULE_LICENSE("GPL");
