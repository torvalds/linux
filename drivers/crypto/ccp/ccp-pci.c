/*
 * AMD Cryptographic Coprocessor (CCP) driver
 *
 * Copyright (C) 2013,2016 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 * Author: Gary R Hook <gary.hook@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/ccp.h>

#include "ccp-dev.h"

#define MSIX_VECTORS			2

struct ccp_msix {
	u32 vector;
	char name[16];
};

struct ccp_pci {
	int msix_count;
	struct ccp_msix msix[MSIX_VECTORS];
};

static int ccp_get_msix_irqs(struct ccp_device *ccp)
{
	struct sp_device *sp = ccp->sp;
	struct ccp_pci *ccp_pci = sp->dev_specific;
	struct device *dev = ccp->dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct msix_entry msix_entry[MSIX_VECTORS];
	unsigned int name_len = sizeof(ccp_pci->msix[0].name) - 1;
	int v, ret;

	for (v = 0; v < ARRAY_SIZE(msix_entry); v++)
		msix_entry[v].entry = v;

	ret = pci_enable_msix_range(pdev, msix_entry, 1, v);
	if (ret < 0)
		return ret;

	ccp_pci->msix_count = ret;
	for (v = 0; v < ccp_pci->msix_count; v++) {
		/* Set the interrupt names and request the irqs */
		snprintf(ccp_pci->msix[v].name, name_len, "%s-%u",
			 sp->name, v);
		ccp_pci->msix[v].vector = msix_entry[v].vector;
		ret = request_irq(ccp_pci->msix[v].vector,
				  ccp->vdata->perform->irqhandler,
				  0, ccp_pci->msix[v].name, ccp);
		if (ret) {
			dev_notice(dev, "unable to allocate MSI-X IRQ (%d)\n",
				   ret);
			goto e_irq;
		}
	}
	ccp->use_tasklet = true;

	return 0;

e_irq:
	while (v--)
		free_irq(ccp_pci->msix[v].vector, dev);

	pci_disable_msix(pdev);

	ccp_pci->msix_count = 0;

	return ret;
}

static int ccp_get_msi_irq(struct ccp_device *ccp)
{
	struct sp_device *sp = ccp->sp;
	struct device *dev = ccp->dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	ret = pci_enable_msi(pdev);
	if (ret)
		return ret;

	ccp->irq = pdev->irq;
	ret = request_irq(ccp->irq, ccp->vdata->perform->irqhandler, 0,
			  sp->name, ccp);
	if (ret) {
		dev_notice(dev, "unable to allocate MSI IRQ (%d)\n", ret);
		goto e_msi;
	}
	ccp->use_tasklet = true;

	return 0;

e_msi:
	pci_disable_msi(pdev);

	return ret;
}

static int ccp_get_irqs(struct ccp_device *ccp)
{
	struct device *dev = ccp->dev;
	int ret;

	ret = ccp_get_msix_irqs(ccp);
	if (!ret)
		return 0;

	/* Couldn't get MSI-X vectors, try MSI */
	dev_notice(dev, "could not enable MSI-X (%d), trying MSI\n", ret);
	ret = ccp_get_msi_irq(ccp);
	if (!ret)
		return 0;

	/* Couldn't get MSI interrupt */
	dev_notice(dev, "could not enable MSI (%d)\n", ret);

	return ret;
}

static void ccp_free_irqs(struct ccp_device *ccp)
{
	struct sp_device *sp = ccp->sp;
	struct ccp_pci *ccp_pci = sp->dev_specific;
	struct device *dev = ccp->dev;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (ccp_pci->msix_count) {
		while (ccp_pci->msix_count--)
			free_irq(ccp_pci->msix[ccp_pci->msix_count].vector,
				 ccp);
		pci_disable_msix(pdev);
	} else if (ccp->irq) {
		free_irq(ccp->irq, ccp);
		pci_disable_msi(pdev);
	}
	ccp->irq = 0;
}

static int ccp_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct sp_device *sp;
	struct ccp_pci *ccp_pci;
	struct device *dev = &pdev->dev;
	void __iomem * const *iomap_table;
	int bar_mask;
	int ret;

	ret = -ENOMEM;
	sp = sp_alloc_struct(dev);
	if (!sp)
		goto e_err;

	ccp_pci = devm_kzalloc(dev, sizeof(*ccp_pci), GFP_KERNEL);
	if (!ccp_pci)
		goto e_err;

	sp->dev_specific = ccp_pci;
	sp->dev_vdata = (struct sp_dev_vdata *)id->driver_data;
	if (!sp->dev_vdata) {
		ret = -ENODEV;
		dev_err(dev, "missing driver data\n");
		goto e_err;
	}
	sp->get_irq = ccp_get_irqs;
	sp->free_irq = ccp_free_irqs;

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(dev, "pcim_enable_device failed (%d)\n", ret);
		goto e_err;
	}

	bar_mask = pci_select_bars(pdev, IORESOURCE_MEM);
	ret = pcim_iomap_regions(pdev, bar_mask, "ccp");
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

	sp->io_map = iomap_table[sp->dev_vdata->bar];
	if (!sp->io_map) {
		dev_err(dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto e_err;
	}

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

	dev_set_drvdata(dev, sp);

	ret = sp_init(sp);
	if (ret)
		goto e_err;

	dev_notice(dev, "enabled\n");

	return 0;

e_err:
	dev_notice(dev, "initialization failed\n");
	return ret;
}

static void ccp_pci_remove(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct sp_device *sp = dev_get_drvdata(dev);

	if (!sp)
		return;

	sp_destroy(sp);

	dev_notice(dev, "disabled\n");
}

#ifdef CONFIG_PM
static int ccp_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct device *dev = &pdev->dev;
	struct sp_device *sp = dev_get_drvdata(dev);

	return sp_suspend(sp, state);
}

static int ccp_pci_resume(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct sp_device *sp = dev_get_drvdata(dev);

	return sp_resume(sp);
}
#endif

static const struct sp_dev_vdata dev_vdata[] = {
	{
		.bar = 2,
#ifdef CONFIG_CRYPTO_DEV_SP_CCP
		.ccp_vdata = &ccpv3,
#endif
	},
	{
		.bar = 2,
#ifdef CONFIG_CRYPTO_DEV_SP_CCP
		.ccp_vdata = &ccpv5a,
#endif
	},
	{
		.bar = 2,
#ifdef CONFIG_CRYPTO_DEV_SP_CCP
		.ccp_vdata = &ccpv5b,
#endif
	},
};
static const struct pci_device_id ccp_pci_table[] = {
	{ PCI_VDEVICE(AMD, 0x1537), (kernel_ulong_t)&dev_vdata[0] },
	{ PCI_VDEVICE(AMD, 0x1456), (kernel_ulong_t)&dev_vdata[1] },
	{ PCI_VDEVICE(AMD, 0x1468), (kernel_ulong_t)&dev_vdata[2] },
	/* Last entry must be zero */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ccp_pci_table);

static struct pci_driver ccp_pci_driver = {
	.name = "ccp",
	.id_table = ccp_pci_table,
	.probe = ccp_pci_probe,
	.remove = ccp_pci_remove,
#ifdef CONFIG_PM
	.suspend = ccp_pci_suspend,
	.resume = ccp_pci_resume,
#endif
};

int ccp_pci_init(void)
{
	return pci_register_driver(&ccp_pci_driver);
}

void ccp_pci_exit(void)
{
	pci_unregister_driver(&ccp_pci_driver);
}
