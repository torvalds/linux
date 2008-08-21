/* orinoco_pci.c
 * 
 * Driver for Prism 2.5/3 devices that have a direct PCI interface
 * (i.e. these are not PCMCIA cards in a PCMCIA-to-PCI bridge).
 * The card contains only one PCI region, which contains all the usual
 * hermes registers, as well as the COR register.
 *
 * Current maintainers are:
 * 	Pavel Roskin <proski AT gnu.org>
 * and	David Gibson <hermes AT gibson.dropbear.id.au>
 *
 * Some of this code is borrowed from orinoco_plx.c
 *	Copyright (C) 2001 Daniel Barlow <dan AT telent.net>
 * Some of this code is "inspired" by linux-wlan-ng-0.1.10, but nothing
 * has been copied from it. linux-wlan-ng-0.1.10 is originally :
 *	Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
 * This file originally written by:
 *	Copyright (C) 2001 Jean Tourrilhes <jt AT hpl.hp.com>
 * And is now maintained by:
 *	(C) Copyright David Gibson, IBM Corp. 2002-2003.
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

#define DRIVER_NAME "orinoco_pci"
#define PFX DRIVER_NAME ": "

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "orinoco.h"
#include "orinoco_pci.h"

/* Offset of the COR register of the PCI card */
#define HERMES_PCI_COR		(0x26)

/* Bitmask to reset the card */
#define HERMES_PCI_COR_MASK	(0x0080)

/* Magic timeouts for doing the reset.
 * Those times are straight from wlan-ng, and it is claimed that they
 * are necessary. Alan will kill me. Take your time and grab a coffee. */
#define HERMES_PCI_COR_ONT	(250)		/* ms */
#define HERMES_PCI_COR_OFFT	(500)		/* ms */
#define HERMES_PCI_COR_BUSYT	(500)		/* ms */

/*
 * Do a soft reset of the card using the Configuration Option Register
 * We need this to get going...
 * This is the part of the code that is strongly inspired from wlan-ng
 *
 * Note : This code is done with irq enabled. This mean that many
 * interrupts will occur while we are there. This is why we use the
 * jiffies to regulate time instead of a straight mdelay(). Usually we
 * need only around 245 iteration of the loop to do 250 ms delay.
 *
 * Note bis : Don't try to access HERMES_CMD during the reset phase.
 * It just won't work !
 */
static int orinoco_pci_cor_reset(struct orinoco_private *priv)
{
	hermes_t *hw = &priv->hw;
	unsigned long timeout;
	u16 reg;

	/* Assert the reset until the card notices */
	hermes_write_regn(hw, PCI_COR, HERMES_PCI_COR_MASK);
	mdelay(HERMES_PCI_COR_ONT);

	/* Give time for the card to recover from this hard effort */
	hermes_write_regn(hw, PCI_COR, 0x0000);
	mdelay(HERMES_PCI_COR_OFFT);

	/* The card is ready when it's no longer busy */
	timeout = jiffies + (HERMES_PCI_COR_BUSYT * HZ / 1000);
	reg = hermes_read_regn(hw, CMD);
	while (time_before(jiffies, timeout) && (reg & HERMES_CMD_BUSY)) {
		mdelay(1);
		reg = hermes_read_regn(hw, CMD);
	}

	/* Still busy? */
	if (reg & HERMES_CMD_BUSY) {
		printk(KERN_ERR PFX "Busy timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int orinoco_pci_init_one(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	int err;
	struct orinoco_private *priv;
	struct orinoco_pci_card *card;
	struct net_device *dev;
	void __iomem *hermes_io;

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

	hermes_io = pci_iomap(pdev, 0, 0);
	if (!hermes_io) {
		printk(KERN_ERR PFX "Cannot remap chipset registers\n");
		err = -EIO;
		goto fail_map_hermes;
	}

	/* Allocate network device */
	dev = alloc_orinocodev(sizeof(*card), &pdev->dev,
			       orinoco_pci_cor_reset, NULL);
	if (!dev) {
		printk(KERN_ERR PFX "Cannot allocate network device\n");
		err = -ENOMEM;
		goto fail_alloc;
	}

	priv = netdev_priv(dev);
	card = priv->card;
	SET_NETDEV_DEV(dev, &pdev->dev);

	hermes_struct_init(&priv->hw, hermes_io, HERMES_32BIT_REGSPACING);

	err = request_irq(pdev->irq, orinoco_interrupt, IRQF_SHARED,
			  dev->name, dev);
	if (err) {
		printk(KERN_ERR PFX "Cannot allocate IRQ %d\n", pdev->irq);
		err = -EBUSY;
		goto fail_irq;
	}

	err = orinoco_pci_cor_reset(priv);
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
	printk(KERN_DEBUG "%s: " DRIVER_NAME " at %s\n", dev->name,
	       pci_name(pdev));

	return 0;

 fail:
	free_irq(pdev->irq, dev);

 fail_irq:
	pci_set_drvdata(pdev, NULL);
	free_orinocodev(dev);

 fail_alloc:
	pci_iounmap(pdev, hermes_io);

 fail_map_hermes:
	pci_release_regions(pdev);

 fail_resources:
	pci_disable_device(pdev);

	return err;
}

static void __devexit orinoco_pci_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct orinoco_private *priv = netdev_priv(dev);

	unregister_netdev(dev);
	free_irq(pdev->irq, dev);
	pci_set_drvdata(pdev, NULL);
	free_orinocodev(dev);
	pci_iounmap(pdev, priv->hw.iobase);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_device_id orinoco_pci_id_table[] = {
	/* Intersil Prism 3 */
	{0x1260, 0x3872, PCI_ANY_ID, PCI_ANY_ID,},
	/* Intersil Prism 2.5 */
	{0x1260, 0x3873, PCI_ANY_ID, PCI_ANY_ID,},
	/* Samsung MagicLAN SWL-2210P */
	{0x167d, 0xa000, PCI_ANY_ID, PCI_ANY_ID,},
	{0,},
};

MODULE_DEVICE_TABLE(pci, orinoco_pci_id_table);

static struct pci_driver orinoco_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= orinoco_pci_id_table,
	.probe		= orinoco_pci_init_one,
	.remove		= __devexit_p(orinoco_pci_remove_one),
	.suspend	= orinoco_pci_suspend,
	.resume		= orinoco_pci_resume,
};

static char version[] __initdata = DRIVER_NAME " " DRIVER_VERSION
	" (Pavel Roskin <proski@gnu.org>,"
	" David Gibson <hermes@gibson.dropbear.id.au> &"
	" Jean Tourrilhes <jt@hpl.hp.com>)";
MODULE_AUTHOR("Pavel Roskin <proski@gnu.org> & David Gibson <hermes@gibson.dropbear.id.au>");
MODULE_DESCRIPTION("Driver for wireless LAN cards using direct PCI interface");
MODULE_LICENSE("Dual MPL/GPL");

static int __init orinoco_pci_init(void)
{
	printk(KERN_DEBUG "%s\n", version);
	return pci_register_driver(&orinoco_pci_driver);
}

static void __exit orinoco_pci_exit(void)
{
	pci_unregister_driver(&orinoco_pci_driver);
}

module_init(orinoco_pci_init);
module_exit(orinoco_pci_exit);

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
