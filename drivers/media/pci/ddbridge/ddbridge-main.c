/*
 * ddbridge.c: Digital Devices PCIe bridge driver
 *
 * Copyright (C) 2010-2011 Digital Devices GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * To obtain the license, point your browser to
 * http://www.gnu.org/copyleft/gpl.html
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

int xo2_speed = 2;
module_param(xo2_speed, int, 0444);
MODULE_PARM_DESC(xo2_speed, "default transfer speed for xo2 based duoflex, 0=55,1=75,2=90,3=104 MBit/s, default=2, use attribute to change for individual cards");

int stv0910_single;
module_param(stv0910_single, int, 0444);
MODULE_PARM_DESC(stv0910_single, "use stv0910 cards as single demods");

/******************************************************************************/

static void ddb_unmap(struct ddb *dev)
{
	if (dev->regs)
		iounmap(dev->regs);
	vfree(dev);
}


static void ddb_remove(struct pci_dev *pdev)
{
	struct ddb *dev = pci_get_drvdata(pdev);

	ddb_ports_detach(dev);
	ddb_i2c_release(dev);

	ddbwritel(0, INTERRUPT_ENABLE);
	free_irq(dev->pdev->irq, dev);
#ifdef CONFIG_PCI_MSI
	if (dev->msi)
		pci_disable_msi(dev->pdev);
#endif
	ddb_ports_release(dev);
	ddb_buffers_free(dev);
	ddb_device_destroy(dev);

	ddb_unmap(dev);
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
}


static int ddb_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ddb *dev;
	int stat = 0;
	int irq_flag = IRQF_SHARED;

	if (pci_enable_device(pdev) < 0)
		return -ENODEV;

	dev = vzalloc(sizeof(struct ddb));
	if (dev == NULL)
		return -ENOMEM;

	dev->pdev = pdev;
	pci_set_drvdata(pdev, dev);
	dev->info = (struct ddb_info *) id->driver_data;
	dev_info(&pdev->dev, "Detected %s\n", dev->info->name);

	dev->regs = ioremap(pci_resource_start(dev->pdev, 0),
			    pci_resource_len(dev->pdev, 0));
	if (!dev->regs) {
		stat = -ENOMEM;
		goto fail;
	}
	dev_info(&pdev->dev, "HW %08x FW %08x\n", ddbreadl(0), ddbreadl(4));

#ifdef CONFIG_PCI_MSI
	if (pci_msi_enabled())
		stat = pci_enable_msi(dev->pdev);
	if (stat) {
		dev_info(&pdev->dev, "MSI not available.\n");
	} else {
		irq_flag = 0;
		dev->msi = 1;
	}
#endif
	stat = request_irq(dev->pdev->irq, irq_handler,
			   irq_flag, "DDBridge", (void *) dev);
	if (stat < 0)
		goto fail1;
	ddbwritel(0, DMA_BASE_WRITE);
	ddbwritel(0, DMA_BASE_READ);
	ddbwritel(0xffffffff, INTERRUPT_ACK);
	ddbwritel(0xfff0f, INTERRUPT_ENABLE);
	ddbwritel(0, MSI1_ENABLE);

	/* board control */
	if (dev->info->board_control) {
		ddbwritel(0, DDB_LINK_TAG(0) | BOARD_CONTROL);
		msleep(100);
		ddbwritel(dev->info->board_control_2,
			DDB_LINK_TAG(0) | BOARD_CONTROL);
		usleep_range(2000, 3000);
		ddbwritel(dev->info->board_control_2
			| dev->info->board_control,
			DDB_LINK_TAG(0) | BOARD_CONTROL);
		usleep_range(2000, 3000);
	}

	if (ddb_i2c_init(dev) < 0)
		goto fail1;
	ddb_ports_init(dev);
	if (ddb_buffers_alloc(dev) < 0) {
		dev_err(&pdev->dev, "Could not allocate buffer memory\n");
		goto fail2;
	}
	if (ddb_ports_attach(dev) < 0)
		goto fail3;
	ddb_device_create(dev);
	return 0;

fail3:
	ddb_ports_detach(dev);
	dev_err(&pdev->dev, "fail3\n");
	ddb_ports_release(dev);
fail2:
	dev_err(&pdev->dev, "fail2\n");
	ddb_buffers_free(dev);
fail1:
	dev_err(&pdev->dev, "fail1\n");
	if (dev->msi)
		pci_disable_msi(dev->pdev);
	if (stat == 0)
		free_irq(dev->pdev->irq, dev);
fail:
	dev_err(&pdev->dev, "fail\n");
	ddb_unmap(dev);
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
	return -1;
}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/

static const struct ddb_info ddb_none = {
	.type     = DDB_NONE,
	.name     = "Digital Devices PCIe bridge",
};

static const struct ddb_info ddb_octopus = {
	.type     = DDB_OCTOPUS,
	.name     = "Digital Devices Octopus DVB adapter",
	.port_num = 4,
};

static const struct ddb_info ddb_octopus_le = {
	.type     = DDB_OCTOPUS,
	.name     = "Digital Devices Octopus LE DVB adapter",
	.port_num = 2,
};

static const struct ddb_info ddb_octopus_oem = {
	.type     = DDB_OCTOPUS,
	.name     = "Digital Devices Octopus OEM",
	.port_num = 4,
};

static const struct ddb_info ddb_octopus_mini = {
	.type     = DDB_OCTOPUS,
	.name     = "Digital Devices Octopus Mini",
	.port_num = 4,
};

static const struct ddb_info ddb_v6 = {
	.type     = DDB_OCTOPUS,
	.name     = "Digital Devices Cine S2 V6 DVB adapter",
	.port_num = 3,
};
static const struct ddb_info ddb_v6_5 = {
	.type     = DDB_OCTOPUS,
	.name     = "Digital Devices Cine S2 V6.5 DVB adapter",
	.port_num = 4,
};

static const struct ddb_info ddb_v7 = {
	.type     = DDB_OCTOPUS,
	.name     = "Digital Devices Cine S2 V7 DVB adapter",
	.port_num = 4,
	.board_control   = 2,
	.board_control_2 = 4,
	.ts_quirks = TS_QUIRK_REVERSED,
};

static const struct ddb_info ddb_v7a = {
	.type     = DDB_OCTOPUS,
	.name     = "Digital Devices Cine S2 V7 Advanced DVB adapter",
	.port_num = 4,
	.board_control   = 2,
	.board_control_2 = 4,
	.ts_quirks = TS_QUIRK_REVERSED,
};

static const struct ddb_info ddb_dvbct = {
	.type     = DDB_OCTOPUS,
	.name     = "Digital Devices DVBCT V6.1 DVB adapter",
	.port_num = 3,
};

static const struct ddb_info ddb_ctv7 = {
	.type     = DDB_OCTOPUS,
	.name     = "Digital Devices Cine CT V7 DVB adapter",
	.port_num = 4,
	.board_control   = 3,
	.board_control_2 = 4,
};

static const struct ddb_info ddb_satixS2v3 = {
	.type     = DDB_OCTOPUS,
	.name     = "Mystique SaTiX-S2 V3 DVB adapter",
	.port_num = 3,
};

static const struct ddb_info ddb_octopusv3 = {
	.type     = DDB_OCTOPUS,
	.name     = "Digital Devices Octopus V3 DVB adapter",
	.port_num = 4,
};

/*** MaxA8 adapters ***********************************************************/

static struct ddb_info ddb_ct2_8 = {
	.type     = DDB_OCTOPUS_MAX_CT,
	.name     = "Digital Devices MAX A8 CT2",
	.port_num = 4,
	.board_control   = 0x0ff,
	.board_control_2 = 0xf00,
	.ts_quirks = TS_QUIRK_SERIAL,
};

static struct ddb_info ddb_c2t2_8 = {
	.type     = DDB_OCTOPUS_MAX_CT,
	.name     = "Digital Devices MAX A8 C2T2",
	.port_num = 4,
	.board_control   = 0x0ff,
	.board_control_2 = 0xf00,
	.ts_quirks = TS_QUIRK_SERIAL,
};

static struct ddb_info ddb_isdbt_8 = {
	.type     = DDB_OCTOPUS_MAX_CT,
	.name     = "Digital Devices MAX A8 ISDBT",
	.port_num = 4,
	.board_control   = 0x0ff,
	.board_control_2 = 0xf00,
	.ts_quirks = TS_QUIRK_SERIAL,
};

static struct ddb_info ddb_c2t2i_v0_8 = {
	.type     = DDB_OCTOPUS_MAX_CT,
	.name     = "Digital Devices MAX A8 C2T2I V0",
	.port_num = 4,
	.board_control   = 0x0ff,
	.board_control_2 = 0xf00,
	.ts_quirks = TS_QUIRK_SERIAL | TS_QUIRK_ALT_OSC,
};

static struct ddb_info ddb_c2t2i_8 = {
	.type     = DDB_OCTOPUS_MAX_CT,
	.name     = "Digital Devices MAX A8 C2T2I",
	.port_num = 4,
	.board_control   = 0x0ff,
	.board_control_2 = 0xf00,
	.ts_quirks = TS_QUIRK_SERIAL,
};

/******************************************************************************/

#define DDVID 0xdd01 /* Digital Devices Vendor ID */

#define DDB_ID(_vend, _dev, _subvend, _subdev, _driverdata) {	\
	.vendor      = _vend,    .device    = _dev, \
	.subvendor   = _subvend, .subdevice = _subdev, \
	.driver_data = (unsigned long)&_driverdata }

static const struct pci_device_id ddb_id_tbl[] = {
	DDB_ID(DDVID, 0x0002, DDVID, 0x0001, ddb_octopus),
	DDB_ID(DDVID, 0x0003, DDVID, 0x0001, ddb_octopus),
	DDB_ID(DDVID, 0x0005, DDVID, 0x0004, ddb_octopusv3),
	DDB_ID(DDVID, 0x0003, DDVID, 0x0002, ddb_octopus_le),
	DDB_ID(DDVID, 0x0003, DDVID, 0x0003, ddb_octopus_oem),
	DDB_ID(DDVID, 0x0003, DDVID, 0x0010, ddb_octopus_mini),
	DDB_ID(DDVID, 0x0005, DDVID, 0x0011, ddb_octopus_mini),
	DDB_ID(DDVID, 0x0003, DDVID, 0x0020, ddb_v6),
	DDB_ID(DDVID, 0x0003, DDVID, 0x0021, ddb_v6_5),
	DDB_ID(DDVID, 0x0006, DDVID, 0x0022, ddb_v7),
	DDB_ID(DDVID, 0x0006, DDVID, 0x0024, ddb_v7a),
	DDB_ID(DDVID, 0x0003, DDVID, 0x0030, ddb_dvbct),
	DDB_ID(DDVID, 0x0003, DDVID, 0xdb03, ddb_satixS2v3),
	DDB_ID(DDVID, 0x0006, DDVID, 0x0031, ddb_ctv7),
	DDB_ID(DDVID, 0x0006, DDVID, 0x0032, ddb_ctv7),
	DDB_ID(DDVID, 0x0006, DDVID, 0x0033, ddb_ctv7),
	DDB_ID(DDVID, 0x0008, DDVID, 0x0034, ddb_ct2_8),
	DDB_ID(DDVID, 0x0008, DDVID, 0x0035, ddb_c2t2_8),
	DDB_ID(DDVID, 0x0008, DDVID, 0x0036, ddb_isdbt_8),
	DDB_ID(DDVID, 0x0008, DDVID, 0x0037, ddb_c2t2i_v0_8),
	DDB_ID(DDVID, 0x0008, DDVID, 0x0038, ddb_c2t2i_8),
	DDB_ID(DDVID, 0x0006, DDVID, 0x0039, ddb_ctv7),
	/* in case sub-ids got deleted in flash */
	DDB_ID(DDVID, 0x0003, PCI_ANY_ID, PCI_ANY_ID, ddb_none),
	DDB_ID(DDVID, 0x0005, PCI_ANY_ID, PCI_ANY_ID, ddb_none),
	DDB_ID(DDVID, 0x0006, PCI_ANY_ID, PCI_ANY_ID, ddb_none),
	DDB_ID(DDVID, 0x0007, PCI_ANY_ID, PCI_ANY_ID, ddb_none),
	DDB_ID(DDVID, 0x0008, PCI_ANY_ID, PCI_ANY_ID, ddb_none),
	DDB_ID(DDVID, 0x0011, PCI_ANY_ID, PCI_ANY_ID, ddb_none),
	DDB_ID(DDVID, 0x0013, PCI_ANY_ID, PCI_ANY_ID, ddb_none),
	DDB_ID(DDVID, 0x0201, PCI_ANY_ID, PCI_ANY_ID, ddb_none),
	DDB_ID(DDVID, 0x0320, PCI_ANY_ID, PCI_ANY_ID, ddb_none),
	{0}
};
MODULE_DEVICE_TABLE(pci, ddb_id_tbl);


static struct pci_driver ddb_pci_driver = {
	.name        = "DDBridge",
	.id_table    = ddb_id_tbl,
	.probe       = ddb_probe,
	.remove      = ddb_remove,
};

static __init int module_init_ddbridge(void)
{
	int ret;

	pr_info("Digital Devices PCIE bridge driver, Copyright (C) 2010-11 Digital Devices GmbH\n");

	ret = ddb_class_create();
	if (ret < 0)
		return ret;
	ret = pci_register_driver(&ddb_pci_driver);
	if (ret < 0)
		ddb_class_destroy();
	return ret;
}

static __exit void module_exit_ddbridge(void)
{
	pci_unregister_driver(&ddb_pci_driver);
	ddb_class_destroy();
}

module_init(module_init_ddbridge);
module_exit(module_exit_ddbridge);

MODULE_DESCRIPTION("Digital Devices PCIe Bridge");
MODULE_AUTHOR("Ralph Metzler");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.5");
