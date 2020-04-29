// SPDX-License-Identifier: GPL-2.0-or-later
/*** -*- linux-c -*- **********************************************************

     Driver for Atmel at76c502 at76c504 and at76c506 wireless cards.

         Copyright 2004 Simon Kelley.


******************************************************************************/
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include "atmel.h"

MODULE_AUTHOR("Simon Kelley");
MODULE_DESCRIPTION("Support for Atmel at76c50x 802.11 wireless ethernet cards.");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("Atmel at76c506 PCI wireless cards");

static const struct pci_device_id card_ids[] = {
	{ 0x1114, 0x0506, PCI_ANY_ID, PCI_ANY_ID },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, card_ids);

static int atmel_pci_probe(struct pci_dev *, const struct pci_device_id *);
static void atmel_pci_remove(struct pci_dev *);

static struct pci_driver atmel_driver = {
	.name     = "atmel",
	.id_table = card_ids,
	.probe    = atmel_pci_probe,
	.remove   = atmel_pci_remove,
};


static int atmel_pci_probe(struct pci_dev *pdev,
				     const struct pci_device_id *pent)
{
	struct net_device *dev;

	if (pci_enable_device(pdev))
		return -ENODEV;

	pci_set_master(pdev);

	dev = init_atmel_card(pdev->irq, pdev->resource[1].start,
			      ATMEL_FW_TYPE_506,
			      &pdev->dev, NULL, NULL);
	if (!dev) {
		pci_disable_device(pdev);
		return -ENODEV;
	}

	pci_set_drvdata(pdev, dev);
	return 0;
}

static void atmel_pci_remove(struct pci_dev *pdev)
{
	stop_atmel_card(pci_get_drvdata(pdev));
}

module_pci_driver(atmel_driver);
