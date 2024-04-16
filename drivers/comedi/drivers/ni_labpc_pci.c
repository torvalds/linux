// SPDX-License-Identifier: GPL-2.0+
/*
 * comedi/drivers/ni_labpc_pci.c
 * Driver for National Instruments Lab-PC PCI-1200
 * Copyright (C) 2001, 2002, 2003 Frank Mori Hess <fmhess@users.sourceforge.net>
 */

/*
 * Driver: ni_labpc_pci
 * Description: National Instruments Lab-PC PCI-1200
 * Devices: [National Instruments] PCI-1200 (ni_pci-1200)
 * Author: Frank Mori Hess <fmhess@users.sourceforge.net>
 * Status: works
 *
 * This is the PCI-specific support split off from the ni_labpc driver.
 *
 * Configuration Options: not applicable, uses PCI auto config
 *
 * NI manuals:
 * 340914a (pci-1200)
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/comedi/comedi_pci.h>

#include "ni_labpc.h"

enum labpc_pci_boardid {
	BOARD_NI_PCI1200,
};

static const struct labpc_boardinfo labpc_pci_boards[] = {
	[BOARD_NI_PCI1200] = {
		.name			= "ni_pci-1200",
		.ai_speed		= 10000,
		.ai_scan_up		= 1,
		.has_ao			= 1,
		.is_labpc1200		= 1,
	},
};

/* ripped from mite.h and mite_setup2() to avoid mite dependency */
#define MITE_IODWBSR	0xc0	/* IO Device Window Base Size Register */
#define WENAB		BIT(7)	/* window enable */

static int labpc_pci_mite_init(struct pci_dev *pcidev)
{
	void __iomem *mite_base;
	u32 main_phys_addr;

	/* ioremap the MITE registers (BAR 0) temporarily */
	mite_base = pci_ioremap_bar(pcidev, 0);
	if (!mite_base)
		return -ENOMEM;

	/* set data window to main registers (BAR 1) */
	main_phys_addr = pci_resource_start(pcidev, 1);
	writel(main_phys_addr | WENAB, mite_base + MITE_IODWBSR);

	/* finished with MITE registers */
	iounmap(mite_base);
	return 0;
}

static int labpc_pci_auto_attach(struct comedi_device *dev,
				 unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct labpc_boardinfo *board = NULL;
	int ret;

	if (context < ARRAY_SIZE(labpc_pci_boards))
		board = &labpc_pci_boards[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	ret = labpc_pci_mite_init(pcidev);
	if (ret)
		return ret;

	dev->mmio = pci_ioremap_bar(pcidev, 1);
	if (!dev->mmio)
		return -ENOMEM;

	return labpc_common_attach(dev, pcidev->irq, IRQF_SHARED);
}

static void labpc_pci_detach(struct comedi_device *dev)
{
	labpc_common_detach(dev);
	comedi_pci_detach(dev);
}

static struct comedi_driver labpc_pci_comedi_driver = {
	.driver_name	= "labpc_pci",
	.module		= THIS_MODULE,
	.auto_attach	= labpc_pci_auto_attach,
	.detach		= labpc_pci_detach,
};

static const struct pci_device_id labpc_pci_table[] = {
	{ PCI_VDEVICE(NI, 0x161), BOARD_NI_PCI1200 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, labpc_pci_table);

static int labpc_pci_probe(struct pci_dev *dev,
			   const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &labpc_pci_comedi_driver,
				      id->driver_data);
}

static struct pci_driver labpc_pci_driver = {
	.name		= "labpc_pci",
	.id_table	= labpc_pci_table,
	.probe		= labpc_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(labpc_pci_comedi_driver, labpc_pci_driver);

MODULE_DESCRIPTION("Comedi: National Instruments Lab-PC PCI-1200 driver");
MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_LICENSE("GPL");
