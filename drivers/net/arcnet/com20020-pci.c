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

#define pr_fmt(fmt) "arcnet:" KBUILD_MODNAME ": " fmt

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
#include <linux/list.h>
#include <linux/io.h>
#include <linux/leds.h>

#include "arcdevice.h"
#include "com20020.h"

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

static void led_tx_set(struct led_classdev *led_cdev,
			     enum led_brightness value)
{
	struct com20020_dev *card;
	struct com20020_priv *priv;
	struct com20020_pci_card_info *ci;

	card = container_of(led_cdev, struct com20020_dev, tx_led);

	priv = card->pci_priv;
	ci = priv->ci;

	outb(!!value, priv->misc + ci->leds[card->index].green);
}

static void led_recon_set(struct led_classdev *led_cdev,
			     enum led_brightness value)
{
	struct com20020_dev *card;
	struct com20020_priv *priv;
	struct com20020_pci_card_info *ci;

	card = container_of(led_cdev, struct com20020_dev, recon_led);

	priv = card->pci_priv;
	ci = priv->ci;

	outb(!!value, priv->misc + ci->leds[card->index].red);
}

static ssize_t backplane_mode_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct net_device *net_dev = to_net_dev(dev);
	struct arcnet_local *lp = netdev_priv(net_dev);

	return sprintf(buf, "%s\n", lp->backplane ? "true" : "false");
}
static DEVICE_ATTR_RO(backplane_mode);

static struct attribute *com20020_state_attrs[] = {
	&dev_attr_backplane_mode.attr,
	NULL,
};

static const struct attribute_group com20020_state_group = {
	.name = NULL,
	.attrs = com20020_state_attrs,
};

static void com20020pci_remove(struct pci_dev *pdev);

static int com20020pci_probe(struct pci_dev *pdev,
			     const struct pci_device_id *id)
{
	struct com20020_pci_card_info *ci;
	struct com20020_pci_channel_map *mm;
	struct net_device *dev;
	struct arcnet_local *lp;
	struct com20020_priv *priv;
	int i, ioaddr, ret;
	struct resource *r;

	ret = 0;

	if (pci_enable_device(pdev))
		return -EIO;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct com20020_priv),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ci = (struct com20020_pci_card_info *)id->driver_data;
	priv->ci = ci;
	mm = &ci->misc_map;

	pci_set_drvdata(pdev, priv);

	INIT_LIST_HEAD(&priv->list_dev);

	if (mm->size) {
		ioaddr = pci_resource_start(pdev, mm->bar) + mm->offset;
		r = devm_request_region(&pdev->dev, ioaddr, mm->size,
					"com20020-pci");
		if (!r) {
			pr_err("IO region %xh-%xh already allocated.\n",
			       ioaddr, ioaddr + mm->size - 1);
			return -EBUSY;
		}
		priv->misc = ioaddr;
	}

	for (i = 0; i < ci->devcount; i++) {
		struct com20020_pci_channel_map *cm = &ci->chan_map_tbl[i];
		struct com20020_dev *card;
		int dev_id_mask = 0xf;

		dev = alloc_arcdev(device);
		if (!dev) {
			ret = -ENOMEM;
			break;
		}
		dev->dev_port = i;

		dev->netdev_ops = &com20020_netdev_ops;

		lp = netdev_priv(dev);

		arc_printk(D_NORMAL, dev, "%s Controls\n", ci->name);
		ioaddr = pci_resource_start(pdev, cm->bar) + cm->offset;

		r = devm_request_region(&pdev->dev, ioaddr, cm->size,
					"com20020-pci");
		if (!r) {
			pr_err("IO region %xh-%xh already allocated\n",
			       ioaddr, ioaddr + cm->size - 1);
			ret = -EBUSY;
			goto err_free_arcdev;
		}

		/* Dummy access after Reset
		 * ARCNET controller needs
		 * this access to detect bustype
		 */
		arcnet_outb(0x00, ioaddr, COM20020_REG_W_COMMAND);
		arcnet_inb(ioaddr, COM20020_REG_R_DIAGSTAT);

		SET_NETDEV_DEV(dev, &pdev->dev);
		dev->base_addr = ioaddr;
		dev->dev_addr[0] = node;
		dev->sysfs_groups[0] = &com20020_state_group;
		dev->irq = pdev->irq;
		lp->card_name = "PCI COM20020";
		lp->card_flags = ci->flags;
		lp->backplane = backplane;
		lp->clockp = clockp & 7;
		lp->clockm = clockm & 3;
		lp->timeout = timeout;
		lp->hw.owner = THIS_MODULE;

		lp->backplane = (inb(priv->misc) >> (2 + i)) & 0x1;

		if (!strncmp(ci->name, "EAE PLX-PCI FB2", 15))
			lp->backplane = 1;

		/* Get the dev_id from the PLX rotary coder */
		if (!strncmp(ci->name, "EAE PLX-PCI MA1", 15))
			dev_id_mask = 0x3;
		dev->dev_id = (inb(priv->misc + ci->rotary) >> 4) & dev_id_mask;

		snprintf(dev->name, sizeof(dev->name), "arc%d-%d", dev->dev_id, i);

		if (arcnet_inb(ioaddr, COM20020_REG_R_STATUS) == 0xFF) {
			pr_err("IO address %Xh is empty!\n", ioaddr);
			ret = -EIO;
			goto err_free_arcdev;
		}
		if (com20020_check(dev)) {
			ret = -EIO;
			goto err_free_arcdev;
		}

		card = devm_kzalloc(&pdev->dev, sizeof(struct com20020_dev),
				    GFP_KERNEL);
		if (!card) {
			ret = -ENOMEM;
			goto err_free_arcdev;
		}

		card->index = i;
		card->pci_priv = priv;
		card->tx_led.brightness_set = led_tx_set;
		card->tx_led.default_trigger = devm_kasprintf(&pdev->dev,
						GFP_KERNEL, "arc%d-%d-tx",
						dev->dev_id, i);
		card->tx_led.name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
						"pci:green:tx:%d-%d",
						dev->dev_id, i);

		card->tx_led.dev = &dev->dev;
		card->recon_led.brightness_set = led_recon_set;
		card->recon_led.default_trigger = devm_kasprintf(&pdev->dev,
						GFP_KERNEL, "arc%d-%d-recon",
						dev->dev_id, i);
		card->recon_led.name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
						"pci:red:recon:%d-%d",
						dev->dev_id, i);
		card->recon_led.dev = &dev->dev;
		card->dev = dev;

		ret = devm_led_classdev_register(&pdev->dev, &card->tx_led);
		if (ret)
			goto err_free_arcdev;

		ret = devm_led_classdev_register(&pdev->dev, &card->recon_led);
		if (ret)
			goto err_free_arcdev;

		dev_set_drvdata(&dev->dev, card);

		ret = com20020_found(dev, IRQF_SHARED);
		if (ret)
			goto err_free_arcdev;

		devm_arcnet_led_init(dev, dev->dev_id, i);

		list_add(&card->list, &priv->list_dev);
		continue;

err_free_arcdev:
		free_arcdev(dev);
		break;
	}
	if (ret)
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
		free_arcdev(dev);
	}
}

static struct com20020_pci_card_info card_info_10mbit = {
	.name = "ARC-PCI",
	.devcount = 1,
	.chan_map_tbl = {
		{
			.bar = 2,
			.offset = 0x00,
			.size = 0x08,
		},
	},
	.flags = ARC_CAN_10MBIT,
};

static struct com20020_pci_card_info card_info_5mbit = {
	.name = "ARC-PCI",
	.devcount = 1,
	.chan_map_tbl = {
		{
			.bar = 2,
			.offset = 0x00,
			.size = 0x08,
		},
	},
	.flags = ARC_IS_5MBIT,
};

static struct com20020_pci_card_info card_info_sohard = {
	.name = "PLX-PCI",
	.devcount = 1,
	/* SOHARD needs PCI base addr 4 */
	.chan_map_tbl = {
		{
			.bar = 4,
			.offset = 0x00,
			.size = 0x08
		},
	},
	.flags = ARC_CAN_10MBIT,
};

static struct com20020_pci_card_info card_info_eae_arc1 = {
	.name = "EAE PLX-PCI ARC1",
	.devcount = 1,
	.chan_map_tbl = {
		{
			.bar = 2,
			.offset = 0x00,
			.size = 0x08,
		},
	},
	.misc_map = {
		.bar = 2,
		.offset = 0x10,
		.size = 0x04,
	},
	.leds = {
		{
			.green = 0x0,
			.red = 0x1,
		},
	},
	.rotary = 0x0,
	.flags = ARC_CAN_10MBIT,
};

static struct com20020_pci_card_info card_info_eae_ma1 = {
	.name = "EAE PLX-PCI MA1",
	.devcount = 2,
	.chan_map_tbl = {
		{
			.bar = 2,
			.offset = 0x00,
			.size = 0x08,
		}, {
			.bar = 2,
			.offset = 0x08,
			.size = 0x08,
		}
	},
	.misc_map = {
		.bar = 2,
		.offset = 0x10,
		.size = 0x04,
	},
	.leds = {
		{
			.green = 0x0,
			.red = 0x1,
		}, {
			.green = 0x2,
			.red = 0x3,
		},
	},
	.rotary = 0x0,
	.flags = ARC_CAN_10MBIT,
};

static struct com20020_pci_card_info card_info_eae_fb2 = {
	.name = "EAE PLX-PCI FB2",
	.devcount = 1,
	.chan_map_tbl = {
		{
			.bar = 2,
			.offset = 0x00,
			.size = 0x08,
		},
	},
	.misc_map = {
		.bar = 2,
		.offset = 0x10,
		.size = 0x04,
	},
	.leds = {
		{
			.green = 0x0,
			.red = 0x1,
		},
	},
	.rotary = 0x0,
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
		0x10B5, 0x9050,
		0x10B5, 0x3263,
		0, 0,
		(kernel_ulong_t)&card_info_eae_arc1
	},
	{
		0x10B5, 0x9050,
		0x10B5, 0x3292,
		0, 0,
		(kernel_ulong_t)&card_info_eae_ma1
	},
	{
		0x10B5, 0x9050,
		0x10B5, 0x3294,
		0, 0,
		(kernel_ulong_t)&card_info_eae_fb2
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
	if (BUGLVL(D_NORMAL))
		pr_info("%s\n", "COM20020 PCI support");
	return pci_register_driver(&com20020pci_driver);
}

static void __exit com20020pci_cleanup(void)
{
	pci_unregister_driver(&com20020pci_driver);
}

module_init(com20020pci_init)
module_exit(com20020pci_cleanup)
