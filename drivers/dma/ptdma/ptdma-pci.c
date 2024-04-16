// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Passthru DMA device driver
 * -- Based on the CCP driver
 *
 * Copyright (C) 2016,2021 Advanced Micro Devices, Inc.
 *
 * Author: Sanjay R Mehta <sanju.mehta@amd.com>
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 * Author: Gary R Hook <gary.hook@amd.com>
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/pci_ids.h>
#include <linux/pci.h>
#include <linux/spinlock.h>

#include "ptdma.h"

struct pt_msix {
	int msix_count;
	struct msix_entry msix_entry;
};

/*
 * pt_alloc_struct - allocate and initialize the pt_device struct
 *
 * @dev: device struct of the PTDMA
 */
static struct pt_device *pt_alloc_struct(struct device *dev)
{
	struct pt_device *pt;

	pt = devm_kzalloc(dev, sizeof(*pt), GFP_KERNEL);

	if (!pt)
		return NULL;
	pt->dev = dev;

	INIT_LIST_HEAD(&pt->cmd);

	return pt;
}

static int pt_get_msix_irqs(struct pt_device *pt)
{
	struct pt_msix *pt_msix = pt->pt_msix;
	struct device *dev = pt->dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	pt_msix->msix_entry.entry = 0;

	ret = pci_enable_msix_range(pdev, &pt_msix->msix_entry, 1, 1);
	if (ret < 0)
		return ret;

	pt_msix->msix_count = ret;

	pt->pt_irq = pt_msix->msix_entry.vector;

	return 0;
}

static int pt_get_msi_irq(struct pt_device *pt)
{
	struct device *dev = pt->dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	ret = pci_enable_msi(pdev);
	if (ret)
		return ret;

	pt->pt_irq = pdev->irq;

	return 0;
}

static int pt_get_irqs(struct pt_device *pt)
{
	struct device *dev = pt->dev;
	int ret;

	ret = pt_get_msix_irqs(pt);
	if (!ret)
		return 0;

	/* Couldn't get MSI-X vectors, try MSI */
	dev_err(dev, "could not enable MSI-X (%d), trying MSI\n", ret);
	ret = pt_get_msi_irq(pt);
	if (!ret)
		return 0;

	/* Couldn't get MSI interrupt */
	dev_err(dev, "could not enable MSI (%d)\n", ret);

	return ret;
}

static void pt_free_irqs(struct pt_device *pt)
{
	struct pt_msix *pt_msix = pt->pt_msix;
	struct device *dev = pt->dev;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (pt_msix->msix_count)
		pci_disable_msix(pdev);
	else if (pt->pt_irq)
		pci_disable_msi(pdev);

	pt->pt_irq = 0;
}

static int pt_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct pt_device *pt;
	struct pt_msix *pt_msix;
	struct device *dev = &pdev->dev;
	void __iomem * const *iomap_table;
	int bar_mask;
	int ret = -ENOMEM;

	pt = pt_alloc_struct(dev);
	if (!pt)
		goto e_err;

	pt_msix = devm_kzalloc(dev, sizeof(*pt_msix), GFP_KERNEL);
	if (!pt_msix)
		goto e_err;

	pt->pt_msix = pt_msix;
	pt->dev_vdata = (struct pt_dev_vdata *)id->driver_data;
	if (!pt->dev_vdata) {
		ret = -ENODEV;
		dev_err(dev, "missing driver data\n");
		goto e_err;
	}

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(dev, "pcim_enable_device failed (%d)\n", ret);
		goto e_err;
	}

	bar_mask = pci_select_bars(pdev, IORESOURCE_MEM);
	ret = pcim_iomap_regions(pdev, bar_mask, "ptdma");
	if (ret) {
		dev_err(dev, "pcim_iomap_regions failed (%d)\n", ret);
		goto e_err;
	}

	iomap_table = pcim_iomap_table(pdev);
	if (!iomap_table) {
		dev_err(dev, "pcim_iomap_table failed\n");
		ret = -ENOMEM;
		goto e_err;
	}

	pt->io_regs = iomap_table[pt->dev_vdata->bar];
	if (!pt->io_regs) {
		dev_err(dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto e_err;
	}

	ret = pt_get_irqs(pt);
	if (ret)
		goto e_err;

	pci_set_master(pdev);

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));
	if (ret) {
		ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
		if (ret) {
			dev_err(dev, "dma_set_mask_and_coherent failed (%d)\n",
				ret);
			goto e_err;
		}
	}

	dev_set_drvdata(dev, pt);

	if (pt->dev_vdata)
		ret = pt_core_init(pt);

	if (ret)
		goto e_err;

	return 0;

e_err:
	dev_err(dev, "initialization failed ret = %d\n", ret);

	return ret;
}

static void pt_pci_remove(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct pt_device *pt = dev_get_drvdata(dev);

	if (!pt)
		return;

	if (pt->dev_vdata)
		pt_core_destroy(pt);

	pt_free_irqs(pt);
}

static const struct pt_dev_vdata dev_vdata[] = {
	{
		.bar = 2,
	},
};

static const struct pci_device_id pt_pci_table[] = {
	{ PCI_VDEVICE(AMD, 0x1498), (kernel_ulong_t)&dev_vdata[0] },
	/* Last entry must be zero */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pt_pci_table);

static struct pci_driver pt_pci_driver = {
	.name = "ptdma",
	.id_table = pt_pci_table,
	.probe = pt_pci_probe,
	.remove = pt_pci_remove,
};

module_pci_driver(pt_pci_driver);

MODULE_AUTHOR("Sanjay R Mehta <sanju.mehta@amd.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMD PassThru DMA driver");
