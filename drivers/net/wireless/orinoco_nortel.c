/* orinoco_nortel.c
 *
 * Driver for Prism II devices which would usually be driven by orinoco_cs,
 * but are connected to the PCI bus by a PCI-to-PCMCIA adapter used in
 * Nortel emobility, Symbol LA-4113 and Symbol LA-4123.
 * but are connected to the PCI bus by a Nortel PCI-PCMCIA-Adapter. 
 *
 * Copyright (C) 2002 Tobias Hoffmann
 *           (C) 2003 Christoph Jungegger <disdos@traum404.de>
 *
 * Some of this code is borrowed from orinoco_plx.c
 *	Copyright (C) 2001 Daniel Barlow
 * Some of this code is borrowed from orinoco_pci.c 
 *  Copyright (C) 2001 Jean Tourrilhes
 * Some of this code is "inspired" by linux-wlan-ng-0.1.10, but nothing
 * has been copied from it. linux-wlan-ng-0.1.10 is originally :
 *	Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
 * 
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 */

#define DRIVER_NAME "orinoco_nortel"
#define PFX DRIVER_NAME ": "

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <pcmcia/cisreg.h>

#include "orinoco.h"
#include "orinoco_pci.h"

#define COR_OFFSET    (0xe0)	/* COR attribute offset of Prism2 PC card */
#define COR_VALUE     (COR_LEVEL_REQ | COR_FUNC_ENA)	/* Enable PC card with interrupt in level trigger */


/*
 * Do a soft reset of the PCI card using the Configuration Option Register
 * We need this to get going...
 * This is the part of the code that is strongly inspired from wlan-ng
 *
 * Note bis : Don't try to access HERMES_CMD during the reset phase.
 * It just won't work !
 */
static int orinoco_nortel_cor_reset(struct orinoco_private *priv)
{
	struct orinoco_pci_card *card = priv->card;

	/* Assert the reset until the card notice */
	iowrite16(8, card->bridge_io + 2);
	ioread16(card->attr_io + COR_OFFSET);
	iowrite16(0x80, card->attr_io + COR_OFFSET);
	mdelay(1);

	/* Give time for the card to recover from this hard effort */
	iowrite16(0, card->attr_io + COR_OFFSET);
	iowrite16(0, card->attr_io + COR_OFFSET);
	mdelay(1);

	/* Set COR as usual */
	iowrite16(COR_VALUE, card->attr_io + COR_OFFSET);
	iowrite16(COR_VALUE, card->attr_io + COR_OFFSET);
	mdelay(1);

	iowrite16(0x228, card->bridge_io + 2);

	return 0;
}

static int orinoco_nortel_hw_init(struct orinoco_pci_card *card)
{
	int i;
	u32 reg;

	/* Setup bridge */
	if (ioread16(card->bridge_io) & 1) {
		printk(KERN_ERR PFX "brg1 answer1 wrong\n");
		return -EBUSY;
	}
	iowrite16(0x118, card->bridge_io + 2);
	iowrite16(0x108, card->bridge_io + 2);
	mdelay(30);
	iowrite16(0x8, card->bridge_io + 2);
	for (i = 0; i < 30; i++) {
		mdelay(30);
		if (ioread16(card->bridge_io) & 0x10) {
			break;
		}
	}
	if (i == 30) {
		printk(KERN_ERR PFX "brg1 timed out\n");
		return -EBUSY;
	}
	if (ioread16(card->attr_io + COR_OFFSET) & 1) {
		printk(KERN_ERR PFX "brg2 answer1 wrong\n");
		return -EBUSY;
	}
	if (ioread16(card->attr_io + COR_OFFSET + 2) & 1) {
		printk(KERN_ERR PFX "brg2 answer2 wrong\n");
		return -EBUSY;
	}
	if (ioread16(card->attr_io + COR_OFFSET + 4) & 1) {
		printk(KERN_ERR PFX "brg2 answer3 wrong\n");
		return -EBUSY;
	}

	/* Set the PCMCIA COR-Register */
	iowrite16(COR_VALUE, card->attr_io + COR_OFFSET);
	mdelay(1);
	reg = ioread16(card->attr_io + COR_OFFSET);
	if (reg != COR_VALUE) {
		printk(KERN_ERR PFX "Error setting COR value (reg=%x)\n",
		       reg);
		return -EBUSY;
	}

	/* Set LEDs */
	iowrite16(1, card->bridge_io + 10);
	return 0;
}

static int orinoco_nortel_init_one(struct pci_dev *pdev,
				   const struct pci_device_id *ent)
{
	int err;
	struct orinoco_private *priv;
	struct orinoco_pci_card *card;
	struct net_device *dev;
	void __iomem *hermes_io, *bridge_io, *attr_io;

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR PFX "Cannot enable PCI device\n");
		return err;
	}

	err = pci_request_regions(pdev, DRIVER_NAME);
	if (err) {
		printk(KERN_ERR PFX "Cannot obtain PCI resources\n");
		goto fail_resources;
	}

	bridge_io = pci_iomap(pdev, 0, 0);
	if (!bridge_io) {
		printk(KERN_ERR PFX "Cannot map bridge registers\n");
		err = -EIO;
		goto fail_map_bridge;
	}

	attr_io = pci_iomap(pdev, 1, 0);
	if (!attr_io) {
		printk(KERN_ERR PFX "Cannot map PCMCIA attributes\n");
		err = -EIO;
		goto fail_map_attr;
	}

	hermes_io = pci_iomap(pdev, 2, 0);
	if (!hermes_io) {
		printk(KERN_ERR PFX "Cannot map chipset registers\n");
		err = -EIO;
		goto fail_map_hermes;
	}

	/* Allocate network device */
	dev = alloc_orinocodev(sizeof(*card), orinoco_nortel_cor_reset);
	if (!dev) {
		printk(KERN_ERR PFX "Cannot allocate network device\n");
		err = -ENOMEM;
		goto fail_alloc;
	}

	priv = netdev_priv(dev);
	card = priv->card;
	card->bridge_io = bridge_io;
	card->attr_io = attr_io;
	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	hermes_struct_init(&priv->hw, hermes_io, HERMES_16BIT_REGSPACING);

	err = request_irq(pdev->irq, orinoco_interrupt, SA_SHIRQ,
			  dev->name, dev);
	if (err) {
		printk(KERN_ERR PFX "Cannot allocate IRQ %d\n", pdev->irq);
		err = -EBUSY;
		goto fail_irq;
	}
	orinoco_pci_setup_netdev(dev, pdev, 2);

	err = orinoco_nortel_hw_init(card);
	if (err) {
		printk(KERN_ERR PFX "Hardware initialization failed\n");
		goto fail;
	}

	err = orinoco_nortel_cor_reset(priv);
	if (err) {
		printk(KERN_ERR PFX "Initial reset failed\n");
		goto fail;
	}

	err = register_netdev(dev);
	if (err) {
		printk(KERN_ERR PFX "Cannot register network device\n");
		goto fail;
	}

	pci_set_drvdata(pdev, dev);

	return 0;

 fail:
	free_irq(pdev->irq, dev);

 fail_irq:
	pci_set_drvdata(pdev, NULL);
	free_orinocodev(dev);

 fail_alloc:
	pci_iounmap(pdev, hermes_io);

 fail_map_hermes:
	pci_iounmap(pdev, attr_io);

 fail_map_attr:
	pci_iounmap(pdev, bridge_io);

 fail_map_bridge:
	pci_release_regions(pdev);

 fail_resources:
	pci_disable_device(pdev);

	return err;
}

static void __devexit orinoco_nortel_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct orinoco_private *priv = netdev_priv(dev);
	struct orinoco_pci_card *card = priv->card;

	/* Clear LEDs */
	iowrite16(0, card->bridge_io + 10);

	unregister_netdev(dev);
	free_irq(dev->irq, dev);
	pci_set_drvdata(pdev, NULL);
	free_orinocodev(dev);
	pci_iounmap(pdev, priv->hw.iobase);
	pci_iounmap(pdev, card->attr_io);
	pci_iounmap(pdev, card->bridge_io);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_device_id orinoco_nortel_id_table[] = {
	/* Nortel emobility PCI */
	{0x126c, 0x8030, PCI_ANY_ID, PCI_ANY_ID,},
	/* Symbol LA-4123 PCI */
	{0x1562, 0x0001, PCI_ANY_ID, PCI_ANY_ID,},
	{0,},
};

MODULE_DEVICE_TABLE(pci, orinoco_nortel_id_table);

static struct pci_driver orinoco_nortel_driver = {
	.name		= DRIVER_NAME,
	.id_table	= orinoco_nortel_id_table,
	.probe		= orinoco_nortel_init_one,
	.remove		= __devexit_p(orinoco_nortel_remove_one),
	.suspend	= orinoco_pci_suspend,
	.resume		= orinoco_pci_resume,
};

static char version[] __initdata = DRIVER_NAME " " DRIVER_VERSION
	" (Tobias Hoffmann & Christoph Jungegger <disdos@traum404.de>)";
MODULE_AUTHOR("Christoph Jungegger <disdos@traum404.de>");
MODULE_DESCRIPTION
    ("Driver for wireless LAN cards using the Nortel PCI bridge");
MODULE_LICENSE("Dual MPL/GPL");

static int __init orinoco_nortel_init(void)
{
	printk(KERN_DEBUG "%s\n", version);
	return pci_module_init(&orinoco_nortel_driver);
}

static void __exit orinoco_nortel_exit(void)
{
	pci_unregister_driver(&orinoco_nortel_driver);
}

module_init(orinoco_nortel_init);
module_exit(orinoco_nortel_exit);

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
