/* orinoco_tmd.c
 *
 * Driver for Prism II devices which would usually be driven by orinoco_cs,
 * but are connected to the PCI bus by a TMD7160. 
 *
 * Copyright (C) 2003 Joerg Dorchain <joerg AT dorchain.net>
 * based heavily upon orinoco_plx.c Copyright (C) 2001 Daniel Barlow
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
 *
 * The actual driving is done by orinoco.c, this is just resource
 * allocation stuff.
 *
 * This driver is modeled after the orinoco_plx driver. The main
 * difference is that the TMD chip has only IO port ranges and doesn't
 * provide access to the PCMCIA attribute space.
 *
 * Pheecom sells cards with the TMD chip as "ASIC version"
 */

#define DRIVER_NAME "orinoco_tmd"
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

#define COR_VALUE	(COR_LEVEL_REQ | COR_FUNC_ENA) /* Enable PC card with interrupt in level trigger */
#define COR_RESET     (0x80)	/* reset bit in the COR register */
#define TMD_RESET_TIME	(500)	/* milliseconds */

/*
 * Do a soft reset of the card using the Configuration Option Register
 */
static int orinoco_tmd_cor_reset(struct orinoco_private *priv)
{
	hermes_t *hw = &priv->hw;
	struct orinoco_pci_card *card = priv->card;
	unsigned long timeout;
	u16 reg;

	iowrite8(COR_VALUE | COR_RESET, card->bridge_io);
	mdelay(1);

	iowrite8(COR_VALUE, card->bridge_io);
	mdelay(1);

	/* Just in case, wait more until the card is no longer busy */
	timeout = jiffies + (TMD_RESET_TIME * HZ / 1000);
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


static int orinoco_tmd_init_one(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	int err;
	struct orinoco_private *priv;
	struct orinoco_pci_card *card;
	struct net_device *dev;
	void __iomem *hermes_io, *bridge_io;

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

	bridge_io = pci_iomap(pdev, 1, 0);
	if (!bridge_io) {
		printk(KERN_ERR PFX "Cannot map bridge registers\n");
		err = -EIO;
		goto fail_map_bridge;
	}

	hermes_io = pci_iomap(pdev, 2, 0);
	if (!hermes_io) {
		printk(KERN_ERR PFX "Cannot map chipset registers\n");
		err = -EIO;
		goto fail_map_hermes;
	}

	/* Allocate network device */
	dev = alloc_orinocodev(sizeof(*card), orinoco_tmd_cor_reset);
	if (!dev) {
		printk(KERN_ERR PFX "Cannot allocate network device\n");
		err = -ENOMEM;
		goto fail_alloc;
	}

	priv = netdev_priv(dev);
	card = priv->card;
	card->bridge_io = bridge_io;
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

	err = orinoco_tmd_cor_reset(priv);
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
	pci_iounmap(pdev, bridge_io);

 fail_map_bridge:
	pci_release_regions(pdev);

 fail_resources:
	pci_disable_device(pdev);

	return err;
}

static void __devexit orinoco_tmd_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct orinoco_private *priv = dev->priv;
	struct orinoco_pci_card *card = priv->card;

	unregister_netdev(dev);
	free_irq(dev->irq, dev);
	pci_set_drvdata(pdev, NULL);
	free_orinocodev(dev);
	pci_iounmap(pdev, priv->hw.iobase);
	pci_iounmap(pdev, card->bridge_io);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_device_id orinoco_tmd_id_table[] = {
	{0x15e8, 0x0131, PCI_ANY_ID, PCI_ANY_ID,},      /* NDC and OEMs, e.g. pheecom */
	{0,},
};

MODULE_DEVICE_TABLE(pci, orinoco_tmd_id_table);

static struct pci_driver orinoco_tmd_driver = {
	.name		= DRIVER_NAME,
	.id_table	= orinoco_tmd_id_table,
	.probe		= orinoco_tmd_init_one,
	.remove		= __devexit_p(orinoco_tmd_remove_one),
	.suspend	= orinoco_pci_suspend,
	.resume		= orinoco_pci_resume,
};

static char version[] __initdata = DRIVER_NAME " " DRIVER_VERSION
	" (Joerg Dorchain <joerg@dorchain.net>)";
MODULE_AUTHOR("Joerg Dorchain <joerg@dorchain.net>");
MODULE_DESCRIPTION("Driver for wireless LAN cards using the TMD7160 PCI bridge");
MODULE_LICENSE("Dual MPL/GPL");

static int __init orinoco_tmd_init(void)
{
	printk(KERN_DEBUG "%s\n", version);
	return pci_module_init(&orinoco_tmd_driver);
}

static void __exit orinoco_tmd_exit(void)
{
	pci_unregister_driver(&orinoco_tmd_driver);
}

module_init(orinoco_tmd_init);
module_exit(orinoco_tmd_exit);

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
