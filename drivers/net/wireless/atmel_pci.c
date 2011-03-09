/*** -*- linux-c -*- **********************************************************

     Driver for Atmel at76c502 at76c504 and at76c506 wireless cards.

         Copyright 2004 Simon Kelley.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This software is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Atmel wireless lan drivers; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

******************************************************************************/
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include "atmel.h"

MODULE_AUTHOR("Simon Kelley");
MODULE_DESCRIPTION("Support for Atmel at76c50x 802.11 wireless ethernet cards.");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("Atmel at76c506 PCI wireless cards");

static DEFINE_PCI_DEVICE_TABLE(card_ids) = {
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
	.remove   = __devexit_p(atmel_pci_remove),
};


static int __devinit atmel_pci_probe(struct pci_dev *pdev,
				     const struct pci_device_id *pent)
{
	struct net_device *dev;

	if (pci_enable_device(pdev))
		return -ENODEV;

	pci_set_master(pdev);

	dev = init_atmel_card(pdev->irq, pdev->resource[1].start,
			      ATMEL_FW_TYPE_506,
			      &pdev->dev, NULL, NULL);
	if (!dev)
		return -ENODEV;

	pci_set_drvdata(pdev, dev);
	return 0;
}

static void __devexit atmel_pci_remove(struct pci_dev *pdev)
{
	stop_atmel_card(pci_get_drvdata(pdev));
}

static int __init atmel_init_module(void)
{
	return pci_register_driver(&atmel_driver);
}

static void __exit atmel_cleanup_module(void)
{
	pci_unregister_driver(&atmel_driver);
}

module_init(atmel_init_module);
module_exit(atmel_cleanup_module);
