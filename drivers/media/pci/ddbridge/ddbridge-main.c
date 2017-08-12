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

int adapter_alloc;
module_param(adapter_alloc, int, 0444);
MODULE_PARM_DESC(adapter_alloc,
		 "0-one adapter per io, 1-one per tab with io, 2-one per tab, 3-one for all");

#ifdef CONFIG_PCI_MSI
#ifdef CONFIG_DVB_DDBRIDGE_MSIENABLE
int msi = 1;
#else
int msi;
#endif
module_param(msi, int, 0444);
#ifdef CONFIG_DVB_DDBRIDGE_MSIENABLE
MODULE_PARM_DESC(msi, "Control MSI interrupts: 0-disable, 1-enable (default)");
#else
MODULE_PARM_DESC(msi, "Control MSI interrupts: 0-disable (default), 1-enable");
#endif
#endif

int ci_bitrate = 70000;
module_param(ci_bitrate, int, 0444);
MODULE_PARM_DESC(ci_bitrate, " Bitrate in KHz for output to CI.");

int ts_loop = -1;
module_param(ts_loop, int, 0444);
MODULE_PARM_DESC(ts_loop, "TS in/out test loop on port ts_loop");

int xo2_speed = 2;
module_param(xo2_speed, int, 0444);
MODULE_PARM_DESC(xo2_speed, "default transfer speed for xo2 based duoflex, 0=55,1=75,2=90,3=104 MBit/s, default=2, use attribute to change for individual cards");

#ifdef __arm__
int alt_dma = 1;
#else
int alt_dma;
#endif
module_param(alt_dma, int, 0444);
MODULE_PARM_DESC(alt_dma, "use alternative DMA buffer handling");

int no_init;
module_param(no_init, int, 0444);
MODULE_PARM_DESC(no_init, "do not initialize most devices");

int stv0910_single;
module_param(stv0910_single, int, 0444);
MODULE_PARM_DESC(stv0910_single, "use stv0910 cards as single demods");

/****************************************************************************/

struct workqueue_struct *ddb_wq;

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static void ddb_unmap(struct ddb *dev)
{
	if (dev->regs)
		iounmap(dev->regs);
	vfree(dev);
}

static void ddb_irq_disable(struct ddb *dev)
{
	ddbwritel(dev, 0, INTERRUPT_ENABLE);
	ddbwritel(dev, 0, MSI1_ENABLE);
}

static void ddb_irq_exit(struct ddb *dev)
{
	ddb_irq_disable(dev);
	if (dev->msi == 2)
		free_irq(dev->pdev->irq + 1, dev);
	free_irq(dev->pdev->irq, dev);
#ifdef CONFIG_PCI_MSI
	if (dev->msi)
		pci_disable_msi(dev->pdev);
#endif
}

static void ddb_remove(struct pci_dev *pdev)
{
	struct ddb *dev = (struct ddb *) pci_get_drvdata(pdev);

	ddb_device_destroy(dev);
	ddb_ports_detach(dev);
	ddb_i2c_release(dev);

	ddb_irq_exit(dev);
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
		stat = pci_alloc_irq_vectors(dev->pdev, 1, nr, PCI_IRQ_MSI);
		if (stat >= 1) {
			dev->msi = stat;
			dev_info(dev->dev, "using %d MSI interrupt(s)\n",
				dev->msi);
		} else
			dev_info(dev->dev, "MSI not available.\n");
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
		stat = request_irq(dev->pdev->irq, ddb_irq_handler0,
				   irq_flag, "ddbridge", (void *) dev);
		if (stat < 0)
			return stat;
		stat = request_irq(dev->pdev->irq + 1, ddb_irq_handler1,
				   irq_flag, "ddbridge", (void *) dev);
		if (stat < 0) {
			free_irq(dev->pdev->irq, dev);
			return stat;
		}
	} else
#endif
	{
		stat = request_irq(dev->pdev->irq, ddb_irq_handler,
				   irq_flag, "ddbridge", (void *) dev);
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

	dev = vzalloc(sizeof(struct ddb));
	if (dev == NULL)
		return -ENOMEM;

	mutex_init(&dev->mutex);
	dev->has_dma = 1;
	dev->pdev = pdev;
	dev->dev = &pdev->dev;
	pci_set_drvdata(pdev, dev);

	dev->link[0].ids.vendor = id->vendor;
	dev->link[0].ids.device = id->device;
	dev->link[0].ids.subvendor = id->subvendor;
	dev->link[0].ids.subdevice = id->subdevice;

	dev->link[0].dev = dev;
	dev->link[0].info = (struct ddb_info *) id->driver_data;
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
	if (dev->msi)
		pci_disable_msi(dev->pdev);
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

#define DDVID 0xdd01 /* Digital Devices Vendor ID */

#define DDB_DEVICE(_device, _subdevice, _driver_data) { \
		PCI_DEVICE_SUB(DDVID, _device, DDVID, _subdevice), \
			.driver_data = (kernel_ulong_t) &_driver_data }

#define DDB_DEVICE_ANY(_device) { \
		PCI_DEVICE_SUB(DDVID, _device, DDVID, PCI_ANY_ID), \
			.driver_data = (kernel_ulong_t) &ddb_none }

static const struct pci_device_id ddb_id_table[] = {
	DDB_DEVICE(0x0002, 0x0001, ddb_octopus),
	DDB_DEVICE(0x0003, 0x0001, ddb_octopus),
	DDB_DEVICE(0x0005, 0x0004, ddb_octopusv3),
	DDB_DEVICE(0x0003, 0x0002, ddb_octopus_le),
	DDB_DEVICE(0x0003, 0x0003, ddb_octopus_oem),
	DDB_DEVICE(0x0003, 0x0010, ddb_octopus_mini),
	DDB_DEVICE(0x0005, 0x0011, ddb_octopus_mini),
	DDB_DEVICE(0x0003, 0x0020, ddb_v6),
	DDB_DEVICE(0x0003, 0x0021, ddb_v6_5),
	DDB_DEVICE(0x0006, 0x0022, ddb_v7),
	DDB_DEVICE(0x0006, 0x0024, ddb_v7a),
	DDB_DEVICE(0x0003, 0x0030, ddb_dvbct),
	DDB_DEVICE(0x0003, 0xdb03, ddb_satixS2v3),
	DDB_DEVICE(0x0006, 0x0031, ddb_ctv7),
	DDB_DEVICE(0x0006, 0x0032, ddb_ctv7),
	DDB_DEVICE(0x0006, 0x0033, ddb_ctv7),
	DDB_DEVICE(0x0008, 0x0034, ddb_ct2_8),
	DDB_DEVICE(0x0008, 0x0035, ddb_c2t2_8),
	DDB_DEVICE(0x0008, 0x0036, ddb_isdbt_8),
	DDB_DEVICE(0x0008, 0x0037, ddb_c2t2i_v0_8),
	DDB_DEVICE(0x0008, 0x0038, ddb_c2t2i_8),
	DDB_DEVICE(0x0006, 0x0039, ddb_ctv7),
	DDB_DEVICE(0x0011, 0x0040, ddb_ci),
	DDB_DEVICE(0x0011, 0x0041, ddb_cis),
	DDB_DEVICE(0x0012, 0x0042, ddb_ci),
	DDB_DEVICE(0x0013, 0x0043, ddb_ci_s2_pro),
	DDB_DEVICE(0x0013, 0x0044, ddb_ci_s2_pro_a),
	/* in case sub-ids got deleted in flash */
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
	int stat = -1;

	pr_info("Digital Devices PCIE bridge driver "
		DDBRIDGE_VERSION
		", Copyright (C) 2010-17 Digital Devices GmbH\n");
	if (ddb_class_create() < 0)
		return -1;
	ddb_wq = create_workqueue("ddbridge");
	if (ddb_wq == NULL)
		goto exit1;
	stat = pci_register_driver(&ddb_pci_driver);
	if (stat < 0)
		goto exit2;
	return stat;
exit2:
	destroy_workqueue(ddb_wq);
exit1:
	ddb_class_destroy();
	return stat;
}

static __exit void module_exit_ddbridge(void)
{
	pci_unregister_driver(&ddb_pci_driver);
	destroy_workqueue(ddb_wq);
	ddb_class_destroy();
}

module_init(module_init_ddbridge);
module_exit(module_exit_ddbridge);

MODULE_DESCRIPTION("Digital Devices PCIe Bridge");
MODULE_AUTHOR("Ralph and Marcus Metzler, Metzler Brothers Systementwicklung GbR");
MODULE_LICENSE("GPL");
MODULE_VERSION(DDBRIDGE_VERSION);
