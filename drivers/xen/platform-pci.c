// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
 * platform-pci.c
 *
 * Xen platform PCI device driver
 *
 * Authors: ssmith@xensource.com and stefano.stabellini@eu.citrix.com
 *
 * Copyright (c) 2005, Intel Corporation.
 * Copyright (c) 2007, XenSource Inc.
 * Copyright (c) 2010, Citrix
 */


#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <xen/platform_pci.h>
#include <xen/grant_table.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/hvm.h>
#include <xen/xen-ops.h>

#define DRV_NAME    "xen-platform-pci"

static unsigned long platform_mmio;
static unsigned long platform_mmio_alloc;
static unsigned long platform_mmiolen;
static uint64_t callback_via;

static unsigned long alloc_xen_mmio(unsigned long len)
{
	unsigned long addr;

	addr = platform_mmio + platform_mmio_alloc;
	platform_mmio_alloc += len;
	BUG_ON(platform_mmio_alloc > platform_mmiolen);

	return addr;
}

static uint64_t get_callback_via(struct pci_dev *pdev)
{
	u8 pin;
	int irq;

	irq = pdev->irq;
	if (irq < 16)
		return irq; /* ISA IRQ */

	pin = pdev->pin;

	/* We don't know the GSI. Specify the PCI INTx line instead. */
	return ((uint64_t)HVM_PARAM_CALLBACK_TYPE_PCI_INTX <<
			  HVM_CALLBACK_VIA_TYPE_SHIFT) |
		((uint64_t)pci_domain_nr(pdev->bus) << 32) |
		((uint64_t)pdev->bus->number << 16) |
		((uint64_t)(pdev->devfn & 0xff) << 8) |
		((uint64_t)(pin - 1) & 3);
}

static irqreturn_t do_hvm_evtchn_intr(int irq, void *dev_id)
{
	xen_hvm_evtchn_do_upcall();
	return IRQ_HANDLED;
}

static int xen_allocate_irq(struct pci_dev *pdev)
{
	return request_irq(pdev->irq, do_hvm_evtchn_intr,
			IRQF_NOBALANCING | IRQF_TRIGGER_RISING,
			"xen-platform-pci", pdev);
}

static int platform_pci_resume(struct device *dev)
{
	int err;

	if (xen_have_vector_callback)
		return 0;

	err = xen_set_callback_via(callback_via);
	if (err) {
		dev_err(dev, "platform_pci_resume failure!\n");
		return err;
	}
	return 0;
}

static int platform_pci_probe(struct pci_dev *pdev,
			      const struct pci_device_id *ent)
{
	int i, ret;
	long ioaddr;
	long mmio_addr, mmio_len;
	unsigned int max_nr_gframes;
	unsigned long grant_frames;

	if (!xen_domain())
		return -ENODEV;

	i = pci_enable_device(pdev);
	if (i)
		return i;

	ioaddr = pci_resource_start(pdev, 0);

	mmio_addr = pci_resource_start(pdev, 1);
	mmio_len = pci_resource_len(pdev, 1);

	if (mmio_addr == 0 || ioaddr == 0) {
		dev_err(&pdev->dev, "no resources found\n");
		ret = -ENOENT;
		goto pci_out;
	}

	ret = pci_request_region(pdev, 1, DRV_NAME);
	if (ret < 0)
		goto pci_out;

	ret = pci_request_region(pdev, 0, DRV_NAME);
	if (ret < 0)
		goto mem_out;

	platform_mmio = mmio_addr;
	platform_mmiolen = mmio_len;
	if (!xen_have_vector_callback) {
		ret = xen_allocate_irq(pdev);
		if (ret) {
			dev_warn(&pdev->dev, "request_irq failed err=%d\n", ret);
			goto out;
		}
		/*
		 * It doesn't strictly *have* to run on CPU0 but it sure
		 * as hell better process the event channel ports delivered
		 * to CPU0.
		 */
		irq_set_affinity(pdev->irq, cpumask_of(0));

		callback_via = get_callback_via(pdev);
		ret = xen_set_callback_via(callback_via);
		if (ret) {
			dev_warn(&pdev->dev, "Unable to set the evtchn callback "
					 "err=%d\n", ret);
			goto irq_out;
		}
	}

	max_nr_gframes = gnttab_max_grant_frames();
	grant_frames = alloc_xen_mmio(PAGE_SIZE * max_nr_gframes);
	ret = gnttab_setup_auto_xlat_frames(grant_frames);
	if (ret)
		goto irq_out;
	ret = gnttab_init();
	if (ret)
		goto grant_out;
	return 0;
grant_out:
	gnttab_free_auto_xlat_frames();
irq_out:
	if (!xen_have_vector_callback)
		free_irq(pdev->irq, pdev);
out:
	pci_release_region(pdev, 0);
mem_out:
	pci_release_region(pdev, 1);
pci_out:
	pci_disable_device(pdev);
	return ret;
}

static const struct pci_device_id platform_pci_tbl[] = {
	{PCI_VENDOR_ID_XEN, PCI_DEVICE_ID_XEN_PLATFORM,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};

static const struct dev_pm_ops platform_pm_ops = {
	.resume_noirq =   platform_pci_resume,
};

static struct pci_driver platform_driver = {
	.name =           DRV_NAME,
	.probe =          platform_pci_probe,
	.id_table =       platform_pci_tbl,
	.driver = {
		.pm =     &platform_pm_ops,
	},
};

builtin_pci_driver(platform_driver);
