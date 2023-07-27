// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/net/ethernet/ibm/emac/rgmii.c
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, RGMII bridge support.
 *
 * Copyright 2007 Benjamin Herrenschmidt, IBM Corp.
 *                <benh@kernel.crashing.org>
 *
 * Based on the arch/ppc version of the driver:
 *
 * Copyright (c) 2004, 2005 Zultys Technologies.
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *
 * Based on original work by
 * 	Matt Porter <mporter@kernel.crashing.org>
 * 	Copyright 2004 MontaVista Software, Inc.
 */
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/ethtool.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <asm/io.h>

#include "emac.h"
#include "debug.h"

// XXX FIXME: Axon seems to support a subset of the RGMII, we
// thus need to take that into account and possibly change some
// of the bit settings below that don't seem to quite match the
// AXON spec

/* RGMIIx_FER */
#define RGMII_FER_MASK(idx)	(0x7 << ((idx) * 4))
#define RGMII_FER_RTBI(idx)	(0x4 << ((idx) * 4))
#define RGMII_FER_RGMII(idx)	(0x5 << ((idx) * 4))
#define RGMII_FER_TBI(idx)	(0x6 << ((idx) * 4))
#define RGMII_FER_GMII(idx)	(0x7 << ((idx) * 4))
#define RGMII_FER_MII(idx)	RGMII_FER_GMII(idx)

/* RGMIIx_SSR */
#define RGMII_SSR_MASK(idx)	(0x7 << ((idx) * 8))
#define RGMII_SSR_10(idx)	(0x1 << ((idx) * 8))
#define RGMII_SSR_100(idx)	(0x2 << ((idx) * 8))
#define RGMII_SSR_1000(idx)	(0x4 << ((idx) * 8))

/* RGMII bridge supports only GMII/TBI and RGMII/RTBI PHYs */
static inline int rgmii_valid_mode(int phy_mode)
{
	return  phy_interface_mode_is_rgmii(phy_mode) ||
		phy_mode == PHY_INTERFACE_MODE_GMII ||
		phy_mode == PHY_INTERFACE_MODE_MII ||
		phy_mode == PHY_INTERFACE_MODE_TBI ||
		phy_mode == PHY_INTERFACE_MODE_RTBI;
}

static inline u32 rgmii_mode_mask(int mode, int input)
{
	switch (mode) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		return RGMII_FER_RGMII(input);
	case PHY_INTERFACE_MODE_TBI:
		return RGMII_FER_TBI(input);
	case PHY_INTERFACE_MODE_GMII:
		return RGMII_FER_GMII(input);
	case PHY_INTERFACE_MODE_MII:
		return RGMII_FER_MII(input);
	case PHY_INTERFACE_MODE_RTBI:
		return RGMII_FER_RTBI(input);
	default:
		BUG();
	}
}

int rgmii_attach(struct platform_device *ofdev, int input, int mode)
{
	struct rgmii_instance *dev = platform_get_drvdata(ofdev);
	struct rgmii_regs __iomem *p = dev->base;

	RGMII_DBG(dev, "attach(%d)" NL, input);

	/* Check if we need to attach to a RGMII */
	if (input < 0 || !rgmii_valid_mode(mode)) {
		printk(KERN_ERR "%pOF: unsupported settings !\n",
		       ofdev->dev.of_node);
		return -ENODEV;
	}

	mutex_lock(&dev->lock);

	/* Enable this input */
	out_be32(&p->fer, in_be32(&p->fer) | rgmii_mode_mask(mode, input));

	printk(KERN_NOTICE "%pOF: input %d in %s mode\n",
	       ofdev->dev.of_node, input, phy_modes(mode));

	++dev->users;

	mutex_unlock(&dev->lock);

	return 0;
}

void rgmii_set_speed(struct platform_device *ofdev, int input, int speed)
{
	struct rgmii_instance *dev = platform_get_drvdata(ofdev);
	struct rgmii_regs __iomem *p = dev->base;
	u32 ssr;

	mutex_lock(&dev->lock);

	ssr = in_be32(&p->ssr) & ~RGMII_SSR_MASK(input);

	RGMII_DBG(dev, "speed(%d, %d)" NL, input, speed);

	if (speed == SPEED_1000)
		ssr |= RGMII_SSR_1000(input);
	else if (speed == SPEED_100)
		ssr |= RGMII_SSR_100(input);
	else if (speed == SPEED_10)
		ssr |= RGMII_SSR_10(input);

	out_be32(&p->ssr, ssr);

	mutex_unlock(&dev->lock);
}

void rgmii_get_mdio(struct platform_device *ofdev, int input)
{
	struct rgmii_instance *dev = platform_get_drvdata(ofdev);
	struct rgmii_regs __iomem *p = dev->base;
	u32 fer;

	RGMII_DBG2(dev, "get_mdio(%d)" NL, input);

	if (!(dev->flags & EMAC_RGMII_FLAG_HAS_MDIO))
		return;

	mutex_lock(&dev->lock);

	fer = in_be32(&p->fer);
	fer |= 0x00080000u >> input;
	out_be32(&p->fer, fer);
	(void)in_be32(&p->fer);

	DBG2(dev, " fer = 0x%08x\n", fer);
}

void rgmii_put_mdio(struct platform_device *ofdev, int input)
{
	struct rgmii_instance *dev = platform_get_drvdata(ofdev);
	struct rgmii_regs __iomem *p = dev->base;
	u32 fer;

	RGMII_DBG2(dev, "put_mdio(%d)" NL, input);

	if (!(dev->flags & EMAC_RGMII_FLAG_HAS_MDIO))
		return;

	fer = in_be32(&p->fer);
	fer &= ~(0x00080000u >> input);
	out_be32(&p->fer, fer);
	(void)in_be32(&p->fer);

	DBG2(dev, " fer = 0x%08x\n", fer);

	mutex_unlock(&dev->lock);
}

void rgmii_detach(struct platform_device *ofdev, int input)
{
	struct rgmii_instance *dev = platform_get_drvdata(ofdev);
	struct rgmii_regs __iomem *p;

	BUG_ON(!dev || dev->users == 0);
	p = dev->base;

	mutex_lock(&dev->lock);

	RGMII_DBG(dev, "detach(%d)" NL, input);

	/* Disable this input */
	out_be32(&p->fer, in_be32(&p->fer) & ~RGMII_FER_MASK(input));

	--dev->users;

	mutex_unlock(&dev->lock);
}

int rgmii_get_regs_len(struct platform_device *ofdev)
{
	return sizeof(struct emac_ethtool_regs_subhdr) +
		sizeof(struct rgmii_regs);
}

void *rgmii_dump_regs(struct platform_device *ofdev, void *buf)
{
	struct rgmii_instance *dev = platform_get_drvdata(ofdev);
	struct emac_ethtool_regs_subhdr *hdr = buf;
	struct rgmii_regs *regs = (struct rgmii_regs *)(hdr + 1);

	hdr->version = 0;
	hdr->index = 0; /* for now, are there chips with more than one
			 * rgmii ? if yes, then we'll add a cell_index
			 * like we do for emac
			 */
	memcpy_fromio(regs, dev->base, sizeof(struct rgmii_regs));
	return regs + 1;
}


static int rgmii_probe(struct platform_device *ofdev)
{
	struct device_node *np = ofdev->dev.of_node;
	struct rgmii_instance *dev;
	struct resource regs;
	int rc;

	rc = -ENOMEM;
	dev = kzalloc(sizeof(struct rgmii_instance), GFP_KERNEL);
	if (dev == NULL)
		goto err_gone;

	mutex_init(&dev->lock);
	dev->ofdev = ofdev;

	rc = -ENXIO;
	if (of_address_to_resource(np, 0, &regs)) {
		printk(KERN_ERR "%pOF: Can't get registers address\n", np);
		goto err_free;
	}

	rc = -ENOMEM;
	dev->base = (struct rgmii_regs __iomem *)ioremap(regs.start,
						 sizeof(struct rgmii_regs));
	if (dev->base == NULL) {
		printk(KERN_ERR "%pOF: Can't map device registers!\n", np);
		goto err_free;
	}

	/* Check for RGMII flags */
	if (of_property_read_bool(ofdev->dev.of_node, "has-mdio"))
		dev->flags |= EMAC_RGMII_FLAG_HAS_MDIO;

	/* CAB lacks the right properties, fix this up */
	if (of_device_is_compatible(ofdev->dev.of_node, "ibm,rgmii-axon"))
		dev->flags |= EMAC_RGMII_FLAG_HAS_MDIO;

	DBG2(dev, " Boot FER = 0x%08x, SSR = 0x%08x\n",
	     in_be32(&dev->base->fer), in_be32(&dev->base->ssr));

	/* Disable all inputs by default */
	out_be32(&dev->base->fer, 0);

	printk(KERN_INFO
	       "RGMII %pOF initialized with%s MDIO support\n",
	       ofdev->dev.of_node,
	       (dev->flags & EMAC_RGMII_FLAG_HAS_MDIO) ? "" : "out");

	wmb();
	platform_set_drvdata(ofdev, dev);

	return 0;

 err_free:
	kfree(dev);
 err_gone:
	return rc;
}

static int rgmii_remove(struct platform_device *ofdev)
{
	struct rgmii_instance *dev = platform_get_drvdata(ofdev);

	WARN_ON(dev->users != 0);

	iounmap(dev->base);
	kfree(dev);

	return 0;
}

static const struct of_device_id rgmii_match[] =
{
	{
		.compatible	= "ibm,rgmii",
	},
	{
		.type		= "emac-rgmii",
	},
	{},
};

static struct platform_driver rgmii_driver = {
	.driver = {
		.name = "emac-rgmii",
		.of_match_table = rgmii_match,
	},
	.probe = rgmii_probe,
	.remove = rgmii_remove,
};

int __init rgmii_init(void)
{
	return platform_driver_register(&rgmii_driver);
}

void rgmii_exit(void)
{
	platform_driver_unregister(&rgmii_driver);
}
