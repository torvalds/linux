/*
 * ddbridge.c: Digital Devices PCIe bridge driver
 *
 * Copyright (C) 2010-2017 Digital Devices GmbH
 *                         Ralph Metzler <rjkm@metzlerbros.de>
 *                         Marcus Metzler <mocm@metzlerbros.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/timer.h>
#include <linux/i2c.h>
#include <linux/swab.h>
#include <linux/vmalloc.h>

#include "ddbridge.h"
#include "ddbridge-i2c.h"
#include "ddbridge-regs.h"
#include "ddbridge-hw.h"
#include "ddbridge-io.h"

/****************************************************************************/
/* module parameters */

#ifdef CONFIG_PCI_MSI
#ifdef CONFIG_DVB_DDBRIDGE_MSIENABLE
static int msi = 1;
#else
static int msi;
#endif
module_param(msi, int, 0444);
#ifdef CONFIG_DVB_DDBRIDGE_MSIENABLE
MODULE_PARM_DESC(msi, "Control MSI interrupts: 0-disable, 1-enable (default)");
#else
MODULE_PARM_DESC(msi, "Control MSI interrupts: 0-disable (default), 1-enable");
#endif
#endif

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static void ddb_irq_disable(struct ddb *dev)
{
	ddbwritel(dev, 0, INTERRUPT_ENABLE);
	ddbwritel(dev, 0, MSI1_ENABLE);
}

static void ddb_msi_exit(struct ddb *dev)
{
#ifdef CONFIG_PCI_MSI
	if (dev->msi)
		pci_free_irq_vectors(dev->pdev);
#endif
}

static void ddb_irq_exit(struct ddb *dev)
{
	ddb_irq_disable(dev);
	if (dev->msi == 2)
		free_irq(pci_irq_vector(dev->pdev, 1), dev);
	free_irq(pci_irq_vector(dev->pdev, 0), dev);
}

static void ddb_remove(struct pci_dev *pdev)
{
	struct ddb *dev = (struct ddb *)pci_get_drvdata(pdev);

	ddb_device_destroy(dev);
	ddb_ports_detach(dev);
	ddb_i2c_release(dev);

	ddb_irq_exit(dev);
	ddb_msi_exit(dev);
	ddb_ports_release(dev);
	ddb_buffers_free(dev);

	ddb_unmap(dev);
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
}

#ifdef CONFIG_PCI_MSI
static void ddb_irq_msi(struct ddb *dev, int nr)
{
	int stat;

	if (msi && pci_msi_enabled()) {
		stat = pci_alloc_irq_vectors(dev->pdev, 1, nr,
					     PCI_IRQ_MSI | PCI_IRQ_MSIX);
		if (stat >= 1) {
			dev->msi = stat;
			dev_info(dev->dev, "using %d MSI interrupt(s)\n",
				 dev->msi);
		} else {
			dev_info(dev->dev, "MSI not available.\n");
		}
	}
}
#endif

static int ddb_irq_init(struct ddb *dev)
{
	int stat;
	int irq_flag = IRQF_SHARED;

	ddbwritel(dev, 0x00000000, INTERRUPT_ENABLE);
	ddbwritel(dev, 0x00000000, MSI1_ENABLE);
	ddbwritel(dev, 0x00000000, MSI2_ENABLE);
	ddbwritel(dev, 0x00000000, MSI3_ENABLE);
	ddbwritel(dev, 0x00000000, MSI4_ENABLE);
	ddbwritel(dev, 0x00000000, MSI5_ENABLE);
	ddbwritel(dev, 0x00000000, MSI6_ENABLE);
	ddbwritel(dev, 0x00000000, MSI7_ENABLE);

#ifdef CONFIG_PCI_MSI
	ddb_irq_msi(dev, 2);

	if (dev->msi)
		irq_flag = 0;
	if (dev->msi == 2) {
		stat = request_irq(pci_irq_vector(dev->pdev, 0),
				   ddb_irq_handler0, irq_flag, "ddbridge",
				   (void *)dev);
		if (stat < 0)
			return stat;
		stat = request_irq(pci_irq_vector(dev->pdev, 1),
				   ddb_irq_handler1, irq_flag, "ddbridge",
				   (void *)dev);
		if (stat < 0) {
			free_irq(pci_irq_vector(dev->pdev, 0), dev);
			return stat;
		}
	} else
#endif
	{
		stat = request_irq(pci_irq_vector(dev->pdev, 0),
				   ddb_irq_handler, irq_flag, "ddbridge",
				   (void *)dev);
		if (stat < 0)
			return stat;
	}
	if (dev->msi == 2) {
		ddbwritel(dev, 0x0fffff00, INTERRUPT_ENABLE);
		ddbwritel(dev, 0x0000000f, MSI1_ENABLE);
	} else {
		ddbwritel(dev, 0x0fffff0f, INTERRUPT_ENABLE);
		ddbwritel(dev, 0x00000000, MSI1_ENABLE);
	}
	return stat;
}

static int ddb_probe(struct pci_dev *pdev,
		     const struct pci_device_id *id)
{
	struct ddb *dev;
	int stat = 0;

	if (pci_enable_device(pdev) < 0)
		return -ENODEV;

	pci_set_master(pdev);

	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(64)))
		if (pci_set_dma_mask(pdev, DMA_BIT_MASK(32)))
			return -ENODEV;

	dev = vzalloc(sizeof(*dev));
	if (!dev)
		return -ENOMEM;

	mutex_init(&dev->mutex);
	dev->has_dma = 1;
	dev->pdev = pdev;
	dev->dev = &pdev->dev;
	pci_set_drvdata(pdev, dev);

	dev->link[0].ids.vendor = id->vendor;
	dev->link[0].ids.device = id->device;
	dev->link[0].ids.subvendor = id->subvendor;
	dev->link[0].ids.subdevice = pdev->subsystem_device;
	dev->link[0].ids.devid = (id->device << 16) | id->vendor;

	dev->link[0].dev = dev;
	dev->link[0].info = get_ddb_info(id->vendor, id->device,
					 id->subvendor, pdev->subsystem_device);

	dev_info(&pdev->dev, "detected %s\n", dev->link[0].info->name);

	dev->regs_len = pci_resource_len(dev->pdev, 0);
	dev->regs = ioremap(pci_resource_start(dev->pdev, 0),
			    pci_resource_len(dev->pdev, 0));

	if (!dev->regs) {
		dev_err(&pdev->dev, "not enough memory for register map\n");
		stat = -ENOMEM;
		goto fail;
	}
	if (ddbreadl(dev, 0) == 0xffffffff) {
		dev_err(&pdev->dev, "cannot read registers\n");
		stat = -ENODEV;
		goto fail;
	}

	dev->link[0].ids.hwid = ddbreadl(dev, 0);
	dev->link[0].ids.regmapid = ddbreadl(dev, 4);

	dev_info(&pdev->dev, "HW %08x REGMAP %08x\n",
		 dev->link[0].ids.hwid, dev->link[0].ids.regmapid);

	ddbwritel(dev, 0, DMA_BASE_READ);
	ddbwritel(dev, 0, DMA_BASE_WRITE);

	stat = ddb_irq_init(dev);
	if (stat < 0)
		goto fail0;

	if (ddb_init(dev) == 0)
		return 0;

	ddb_irq_exit(dev);
fail0:
	dev_err(&pdev->dev, "fail0\n");
	ddb_msi_exit(dev);
fail:
	dev_err(&pdev->dev, "fail\n");

	ddb_unmap(dev);
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
	return -1;
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

#define DDB_DEVICE_ANY(_device) \
		{ PCI_DEVICE_SUB(DDVID, _device, DDVID, PCI_ANY_ID) }

static const struct pci_device_id ddb_id_table[] = {
	DDB_DEVICE_ANY(0x0002),
	DDB_DEVICE_ANY(0x0003),
	DDB_DEVICE_ANY(0x0005),
	DDB_DEVICE_ANY(0x0006),
	DDB_DEVICE_ANY(0x0007),
	DDB_DEVICE_ANY(0x0008),
	DDB_DEVICE_ANY(0x0011),
	DDB_DEVICE_ANY(0x0012),
	DDB_DEVICE_ANY(0x0013),
	DDB_DEVICE_ANY(0x0201),
	DDB_DEVICE_ANY(0x0203),
	DDB_DEVICE_ANY(0x0210),
	DDB_DEVICE_ANY(0x0220),
	DDB_DEVICE_ANY(0x0320),
	DDB_DEVICE_ANY(0x0321),
	DDB_DEVICE_ANY(0x0322),
	DDB_DEVICE_ANY(0x0323),
	DDB_DEVICE_ANY(0x0328),
	DDB_DEVICE_ANY(0x0329),
	{0}
};

MODULE_DEVICE_TABLE(pci, ddb_id_table);

static struct pci_driver ddb_pci_driver = {
	.name        = "ddbridge",
	.id_table    = ddb_id_table,
	.probe       = ddb_probe,
	.remove      = ddb_remove,
};

static __init int module_init_ddbridge(void)
{
	int stat;

	pr_info("Digital Devices PCIE bridge driver "
		DDBRIDGE_VERSION
		", Copyright (C) 2010-17 Digital Devices GmbH\n");
	stat = ddb_init_ddbridge();
	if (stat < 0)
		return stat;
	stat = pci_register_driver(&ddb_pci_driver);
	if (stat < 0)
		ddb_exit_ddbridge(0, stat);

	return stat;
}

static __exit void module_exit_ddbridge(void)
{
	pci_unregister_driver(&ddb_pci_driver);
	ddb_exit_ddbridge(0, 0);
}

module_init(module_init_ddbridge);
module_exit(module_exit_ddbridge);

MODULE_DESCRIPTION("Digital Devices PCIe Bridge");
MODULE_AUTHOR("Ralph and Marcus Metzler, Metzler Brothers Systementwicklung GbR");
MODULE_LICENSE("GPL");
MODULE_VERSION(DDBRIDGE_VERSION);
