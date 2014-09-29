/*
 * Linux ARCnet driver - COM20020 PCI support
 * Contemporary Controls PCI20 and SOHARD SH-ARC PCI
 * 
 * Written 1994-1999 by Avery Pennarun,
 *    based on an ISA version by David Woodhouse.
 * Written 1999-2000 by Martin Mares <mj@ucw.cz>.
 * Derived from skeleton.c by Donald Becker.
 *
 * Special thanks to Contemporary Controls, Inc. (www.ccontrols.com)
 *  for sponsoring the further development of this driver.
 *
 * **********************
 *
 * The original copyright of skeleton.c was as follows:
 *
 * skeleton.c Written 1993 by Donald Becker.
 * Copyright 1993 United States Government as represented by the
 * Director, National Security Agency.  This software may only be used
 * and distributed according to the terms of the GNU General Public License as
 * modified by SRC, incorporated herein by reference.
 *
 * **********************
 *
 * For more details, see drivers/net/arcnet.c
 *
 * **********************
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/arcdevice.h>
#include <linux/com20020.h>
#include <linux/list.h>

#include <asm/io.h>


#define VERSION "arcnet: COM20020 PCI support\n"

/* Module parameters */

static int node;
static char device[9];		/* use eg. device="arc1" to change name */
static int timeout = 3;
static int backplane;
static int clockp;
static int clockm;

module_param(node, int, 0);
module_param_string(device, device, sizeof(device), 0);
module_param(timeout, int, 0);
module_param(backplane, int, 0);
module_param(clockp, int, 0);
module_param(clockm, int, 0);
MODULE_LICENSE("GPL");

static void com20020pci_remove(struct pci_dev *pdev);

static int com20020pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct com20020_pci_card_info *ci;
	struct net_device *dev;
	struct arcnet_local *lp;
	struct com20020_priv *priv;
	int i, ioaddr, ret;
	struct resource *r;

	if (pci_enable_device(pdev))
		return -EIO;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct com20020_priv),
			    GFP_KERNEL);
	ci = (struct com20020_pci_card_info *)id->driver_data;
	priv->ci = ci;

	INIT_LIST_HEAD(&priv->list_dev);


	for (i = 0; i < ci->devcount; i++) {
		struct com20020_pci_channel_map *cm = &ci->chan_map_tbl[i];
		struct com20020_dev *card;

		dev = alloc_arcdev(device);
		if (!dev) {
			ret = -ENOMEM;
			goto out_port;
		}

		dev->netdev_ops = &com20020_netdev_ops;

		lp = netdev_priv(dev);

		BUGMSG(D_NORMAL, "%s Controls\n", ci->name);
		ioaddr = pci_resource_start(pdev, cm->bar) + cm->offset;

		r = devm_request_region(&pdev->dev, ioaddr, cm->size,
					"com20020-pci");
		if (!r) {
			pr_err("IO region %xh-%xh already allocated.\n",
			       ioaddr, ioaddr + cm->size - 1);
			ret = -EBUSY;
			goto out_port;
		}

		/* Dummy access after Reset
		 * ARCNET controller needs
		 * this access to detect bustype
		 */
		outb(0x00, ioaddr + 1);
		inb(ioaddr + 1);

		dev->base_addr = ioaddr;
		dev->dev_addr[0] = node;
		dev->irq = pdev->irq;
		lp->card_name = "PCI COM20020";
		lp->card_flags = ci->flags;
		lp->backplane = backplane;
		lp->clockp = clockp & 7;
		lp->clockm = clockm & 3;
		lp->timeout = timeout;
		lp->hw.owner = THIS_MODULE;

		if (ASTATUS() == 0xFF) {
			pr_err("IO address %Xh is empty!\n", ioaddr);
			ret = -EIO;
			goto out_port;
		}
		if (com20020_check(dev)) {
			ret = -EIO;
			goto out_port;
		}

		card = devm_kzalloc(&pdev->dev, sizeof(struct com20020_dev),
				    GFP_KERNEL);
		if (!card) {
			pr_err("%s out of memory!\n", __func__);
			return -ENOMEM;
		}

		card->index = i;
		card->pci_priv = priv;
		card->dev = dev;

		dev_set_drvdata(&dev->dev, card);

		ret = com20020_found(dev, IRQF_SHARED);
		if (ret)
			goto out_port;

		list_add(&card->list, &priv->list_dev);
	}

	pci_set_drvdata(pdev, priv);

	return 0;

out_port:
	com20020pci_remove(pdev);
	return ret;
}

static void com20020pci_remove(struct pci_dev *pdev)
{
	struct com20020_dev *card, *tmpcard;
	struct com20020_priv *priv;

	priv = pci_get_drvdata(pdev);

	list_for_each_entry_safe(card, tmpcard, &priv->list_dev, list) {
		struct net_device *dev = card->dev;

		unregister_netdev(dev);
		free_irq(dev->irq, dev);
		free_netdev(dev);
	}
}

static struct com20020_pci_card_info card_info_10mbit = {
	.name = "ARC-PCI",
	.devcount = 1,
	.chan_map_tbl = {
		{ 2, 0x00, 0x08 },
	},
	.flags = ARC_CAN_10MBIT,
};

static struct com20020_pci_card_info card_info_5mbit = {
	.name = "ARC-PCI",
	.devcount = 1,
	.chan_map_tbl = {
		{ 2, 0x00, 0x08 },
	},
	.flags = ARC_IS_5MBIT,
};

static struct com20020_pci_card_info card_info_sohard = {
	.name = "PLX-PCI",
	.devcount = 1,
	/* SOHARD needs PCI base addr 4 */
	.chan_map_tbl = {
		{4, 0x00, 0x08},
	},
	.flags = ARC_CAN_10MBIT,
};

static const struct pci_device_id com20020pci_id_table[] = {
	{
		0x1571, 0xa001,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		0,
	},
	{
		0x1571, 0xa002,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		0,
	},
	{
		0x1571, 0xa003,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		0
	},
	{
		0x1571, 0xa004,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		0,
	},
	{
		0x1571, 0xa005,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		0
	},
	{
		0x1571, 0xa006,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		0
	},
	{
		0x1571, 0xa007,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		0
	},
	{
		0x1571, 0xa008,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		0
	},
	{
		0x1571, 0xa009,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&card_info_5mbit
	},
	{
		0x1571, 0xa00a,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&card_info_5mbit
	},
	{
		0x1571, 0xa00b,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&card_info_5mbit
	},
	{
		0x1571, 0xa00c,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&card_info_5mbit
	},
	{
		0x1571, 0xa00d,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&card_info_5mbit
	},
	{
		0x1571, 0xa00e,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&card_info_5mbit
	},
	{
		0x1571, 0xa201,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&card_info_10mbit
	},
	{
		0x1571, 0xa202,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&card_info_10mbit
	},
	{
		0x1571, 0xa203,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&card_info_10mbit
	},
	{
		0x1571, 0xa204,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&card_info_10mbit
	},
	{
		0x1571, 0xa205,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&card_info_10mbit
	},
	{
		0x1571, 0xa206,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&card_info_10mbit
	},
	{
		0x10B5, 0x9030,
		0x10B5, 0x2978,
		0, 0,
		(kernel_ulong_t)&card_info_sohard
	},
	{
		0x10B5, 0x9050,
		0x10B5, 0x2273,
		0, 0,
		(kernel_ulong_t)&card_info_sohard
	},
	{
		0x14BA, 0x6000,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&card_info_10mbit
	},
	{
		0x10B5, 0x2200,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&card_info_10mbit
	},
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, com20020pci_id_table);

static struct pci_driver com20020pci_driver = {
	.name		= "com20020",
	.id_table	= com20020pci_id_table,
	.probe		= com20020pci_probe,
	.remove		= com20020pci_remove,
};

static int __init com20020pci_init(void)
{
	BUGLVL(D_NORMAL) printk(VERSION);
	return pci_register_driver(&com20020pci_driver);
}

static void __exit com20020pci_cleanup(void)
{
	pci_unregister_driver(&com20020pci_driver);
}

module_init(com20020pci_init)
module_exit(com20020pci_cleanup)
