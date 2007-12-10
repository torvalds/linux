/*
 * drivers/net/ibm_newemac/zmii.c
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, ZMII bridge support.
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
 *      Armin Kuster <akuster@mvista.com>
 * 	Copyright 2001 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/kernel.h>
#include <linux/ethtool.h>
#include <asm/io.h>

#include "emac.h"
#include "core.h"

/* ZMIIx_FER */
#define ZMII_FER_MDI(idx)	(0x80000000 >> ((idx) * 4))
#define ZMII_FER_MDI_ALL	(ZMII_FER_MDI(0) | ZMII_FER_MDI(1) | \
				 ZMII_FER_MDI(2) | ZMII_FER_MDI(3))

#define ZMII_FER_SMII(idx)	(0x40000000 >> ((idx) * 4))
#define ZMII_FER_RMII(idx)	(0x20000000 >> ((idx) * 4))
#define ZMII_FER_MII(idx)	(0x10000000 >> ((idx) * 4))

/* ZMIIx_SSR */
#define ZMII_SSR_SCI(idx)	(0x40000000 >> ((idx) * 4))
#define ZMII_SSR_FSS(idx)	(0x20000000 >> ((idx) * 4))
#define ZMII_SSR_SP(idx)	(0x10000000 >> ((idx) * 4))

/* ZMII only supports MII, RMII and SMII
 * we also support autodetection for backward compatibility
 */
static inline int zmii_valid_mode(int mode)
{
	return  mode == PHY_MODE_MII ||
		mode == PHY_MODE_RMII ||
		mode == PHY_MODE_SMII ||
		mode == PHY_MODE_NA;
}

static inline const char *zmii_mode_name(int mode)
{
	switch (mode) {
	case PHY_MODE_MII:
		return "MII";
	case PHY_MODE_RMII:
		return "RMII";
	case PHY_MODE_SMII:
		return "SMII";
	default:
		BUG();
	}
}

static inline u32 zmii_mode_mask(int mode, int input)
{
	switch (mode) {
	case PHY_MODE_MII:
		return ZMII_FER_MII(input);
	case PHY_MODE_RMII:
		return ZMII_FER_RMII(input);
	case PHY_MODE_SMII:
		return ZMII_FER_SMII(input);
	default:
		return 0;
	}
}

int __devinit zmii_attach(struct of_device *ofdev, int input, int *mode)
{
	struct zmii_instance *dev = dev_get_drvdata(&ofdev->dev);
	struct zmii_regs __iomem *p = dev->base;

	ZMII_DBG(dev, "init(%d, %d)" NL, input, *mode);

	if (!zmii_valid_mode(*mode)) {
		/* Probably an EMAC connected to RGMII,
		 * but it still may need ZMII for MDIO so
		 * we don't fail here.
		 */
		dev->users++;
		return 0;
	}

	mutex_lock(&dev->lock);

	/* Autodetect ZMII mode if not specified.
	 * This is only for backward compatibility with the old driver.
	 * Please, always specify PHY mode in your board port to avoid
	 * any surprises.
	 */
	if (dev->mode == PHY_MODE_NA) {
		if (*mode == PHY_MODE_NA) {
			u32 r = dev->fer_save;

			ZMII_DBG(dev, "autodetecting mode, FER = 0x%08x" NL, r);

			if (r & (ZMII_FER_MII(0) | ZMII_FER_MII(1)))
				dev->mode = PHY_MODE_MII;
			else if (r & (ZMII_FER_RMII(0) | ZMII_FER_RMII(1)))
				dev->mode = PHY_MODE_RMII;
			else
				dev->mode = PHY_MODE_SMII;
		} else
			dev->mode = *mode;

		printk(KERN_NOTICE "%s: bridge in %s mode\n",
		       ofdev->node->full_name, zmii_mode_name(dev->mode));
	} else {
		/* All inputs must use the same mode */
		if (*mode != PHY_MODE_NA && *mode != dev->mode) {
			printk(KERN_ERR
			       "%s: invalid mode %d specified for input %d\n",
			       ofdev->node->full_name, *mode, input);
			mutex_unlock(&dev->lock);
			return -EINVAL;
		}
	}

	/* Report back correct PHY mode,
	 * it may be used during PHY initialization.
	 */
	*mode = dev->mode;

	/* Enable this input */
	out_be32(&p->fer, in_be32(&p->fer) | zmii_mode_mask(dev->mode, input));
	++dev->users;

	mutex_unlock(&dev->lock);

	return 0;
}

void zmii_get_mdio(struct of_device *ofdev, int input)
{
	struct zmii_instance *dev = dev_get_drvdata(&ofdev->dev);
	u32 fer;

	ZMII_DBG2(dev, "get_mdio(%d)" NL, input);

	mutex_lock(&dev->lock);

	fer = in_be32(&dev->base->fer) & ~ZMII_FER_MDI_ALL;
	out_be32(&dev->base->fer, fer | ZMII_FER_MDI(input));
}

void zmii_put_mdio(struct of_device *ofdev, int input)
{
	struct zmii_instance *dev = dev_get_drvdata(&ofdev->dev);

	ZMII_DBG2(dev, "put_mdio(%d)" NL, input);
	mutex_unlock(&dev->lock);
}


void zmii_set_speed(struct of_device *ofdev, int input, int speed)
{
	struct zmii_instance *dev = dev_get_drvdata(&ofdev->dev);
	u32 ssr;

	mutex_lock(&dev->lock);

	ssr = in_be32(&dev->base->ssr);

	ZMII_DBG(dev, "speed(%d, %d)" NL, input, speed);

	if (speed == SPEED_100)
		ssr |= ZMII_SSR_SP(input);
	else
		ssr &= ~ZMII_SSR_SP(input);

	out_be32(&dev->base->ssr, ssr);

	mutex_unlock(&dev->lock);
}

void __devexit zmii_detach(struct of_device *ofdev, int input)
{
	struct zmii_instance *dev = dev_get_drvdata(&ofdev->dev);

	BUG_ON(!dev || dev->users == 0);

	mutex_lock(&dev->lock);

	ZMII_DBG(dev, "detach(%d)" NL, input);

	/* Disable this input */
	out_be32(&dev->base->fer,
		 in_be32(&dev->base->fer) & ~zmii_mode_mask(dev->mode, input));

	--dev->users;

	mutex_unlock(&dev->lock);
}

int zmii_get_regs_len(struct of_device *ofdev)
{
	return sizeof(struct emac_ethtool_regs_subhdr) +
		sizeof(struct zmii_regs);
}

void *zmii_dump_regs(struct of_device *ofdev, void *buf)
{
	struct zmii_instance *dev = dev_get_drvdata(&ofdev->dev);
	struct emac_ethtool_regs_subhdr *hdr = buf;
	struct zmii_regs *regs = (struct zmii_regs *)(hdr + 1);

	hdr->version = 0;
	hdr->index = 0; /* for now, are there chips with more than one
			 * zmii ? if yes, then we'll add a cell_index
			 * like we do for emac
			 */
	memcpy_fromio(regs, dev->base, sizeof(struct zmii_regs));
	return regs + 1;
}

static int __devinit zmii_probe(struct of_device *ofdev,
				const struct of_device_id *match)
{
	struct device_node *np = ofdev->node;
	struct zmii_instance *dev;
	struct resource regs;
	int rc;

	rc = -ENOMEM;
	dev = kzalloc(sizeof(struct zmii_instance), GFP_KERNEL);
	if (dev == NULL) {
		printk(KERN_ERR "%s: could not allocate ZMII device!\n",
		       np->full_name);
		goto err_gone;
	}

	mutex_init(&dev->lock);
	dev->ofdev = ofdev;
	dev->mode = PHY_MODE_NA;

	rc = -ENXIO;
	if (of_address_to_resource(np, 0, &regs)) {
		printk(KERN_ERR "%s: Can't get registers address\n",
		       np->full_name);
		goto err_free;
	}

	rc = -ENOMEM;
	dev->base = (struct zmii_regs __iomem *)ioremap(regs.start,
						sizeof(struct zmii_regs));
	if (dev->base == NULL) {
		printk(KERN_ERR "%s: Can't map device registers!\n",
		       np->full_name);
		goto err_free;
	}

	/* We may need FER value for autodetection later */
	dev->fer_save = in_be32(&dev->base->fer);

	/* Disable all inputs by default */
	out_be32(&dev->base->fer, 0);

	printk(KERN_INFO
	       "ZMII %s initialized\n", ofdev->node->full_name);
	wmb();
	dev_set_drvdata(&ofdev->dev, dev);

	return 0;

 err_free:
	kfree(dev);
 err_gone:
	return rc;
}

static int __devexit zmii_remove(struct of_device *ofdev)
{
	struct zmii_instance *dev = dev_get_drvdata(&ofdev->dev);

	dev_set_drvdata(&ofdev->dev, NULL);

	WARN_ON(dev->users != 0);

	iounmap(dev->base);
	kfree(dev);

	return 0;
}

static struct of_device_id zmii_match[] =
{
	{
		.compatible	= "ibm,zmii",
	},
	/* For backward compat with old DT */
	{
		.type		= "emac-zmii",
	},
	{},
};

static struct of_platform_driver zmii_driver = {
	.name = "emac-zmii",
	.match_table = zmii_match,

	.probe = zmii_probe,
	.remove = zmii_remove,
};

int __init zmii_init(void)
{
	return of_register_platform_driver(&zmii_driver);
}

void zmii_exit(void)
{
	of_unregister_platform_driver(&zmii_driver);
}
