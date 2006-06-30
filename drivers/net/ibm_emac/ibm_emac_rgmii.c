/*
 * drivers/net/ibm_emac/ibm_emac_rgmii.c
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, RGMII bridge support.
 *
 * Copyright (c) 2004, 2005 Zultys Technologies.
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *
 * Based on original work by
 * 	Matt Porter <mporter@kernel.crashing.org>
 * 	Copyright 2004 MontaVista Software, Inc.
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

#include "ibm_emac_core.h"
#include "ibm_emac_debug.h"

/* RGMIIx_FER */
#define RGMII_FER_MASK(idx)	(0x7 << ((idx) * 4))
#define RGMII_FER_RTBI(idx)	(0x4 << ((idx) * 4))
#define RGMII_FER_RGMII(idx)	(0x5 << ((idx) * 4))
#define RGMII_FER_TBI(idx)	(0x6 << ((idx) * 4))
#define RGMII_FER_GMII(idx)	(0x7 << ((idx) * 4))

/* RGMIIx_SSR */
#define RGMII_SSR_MASK(idx)	(0x7 << ((idx) * 8))
#define RGMII_SSR_100(idx)	(0x2 << ((idx) * 8))
#define RGMII_SSR_1000(idx)	(0x4 << ((idx) * 8))

/* RGMII bridge supports only GMII/TBI and RGMII/RTBI PHYs */
static inline int rgmii_valid_mode(int phy_mode)
{
	return  phy_mode == PHY_MODE_GMII ||
		phy_mode == PHY_MODE_RGMII ||
		phy_mode == PHY_MODE_TBI ||
		phy_mode == PHY_MODE_RTBI;
}

static inline const char *rgmii_mode_name(int mode)
{
	switch (mode) {
	case PHY_MODE_RGMII:
		return "RGMII";
	case PHY_MODE_TBI:
		return "TBI";
	case PHY_MODE_GMII:
		return "GMII";
	case PHY_MODE_RTBI:
		return "RTBI";
	default:
		BUG();
	}
}

static inline u32 rgmii_mode_mask(int mode, int input)
{
	switch (mode) {
	case PHY_MODE_RGMII:
		return RGMII_FER_RGMII(input);
	case PHY_MODE_TBI:
		return RGMII_FER_TBI(input);
	case PHY_MODE_GMII:
		return RGMII_FER_GMII(input);
	case PHY_MODE_RTBI:
		return RGMII_FER_RTBI(input);
	default:
		BUG();
	}
}

static int __init rgmii_init(struct ocp_device *ocpdev, int input, int mode)
{
	struct ibm_ocp_rgmii *dev = ocp_get_drvdata(ocpdev);
	struct rgmii_regs *p;

	RGMII_DBG("%d: init(%d, %d)" NL, ocpdev->def->index, input, mode);

	if (!dev) {
		dev = kzalloc(sizeof(struct ibm_ocp_rgmii), GFP_KERNEL);
		if (!dev) {
			printk(KERN_ERR
			       "rgmii%d: couldn't allocate device structure!\n",
			       ocpdev->def->index);
			return -ENOMEM;
		}

		p = (struct rgmii_regs *)ioremap(ocpdev->def->paddr,
						 sizeof(struct rgmii_regs));
		if (!p) {
			printk(KERN_ERR
			       "rgmii%d: could not ioremap device registers!\n",
			       ocpdev->def->index);
			kfree(dev);
			return -ENOMEM;
		}

		dev->base = p;
		ocp_set_drvdata(ocpdev, dev);

		/* Disable all inputs by default */
		out_be32(&p->fer, 0);
	} else
		p = dev->base;

	/* Enable this input */
	out_be32(&p->fer, in_be32(&p->fer) | rgmii_mode_mask(mode, input));

	printk(KERN_NOTICE "rgmii%d: input %d in %s mode\n",
	       ocpdev->def->index, input, rgmii_mode_name(mode));

	++dev->users;
	return 0;
}

int __init rgmii_attach(void *emac)
{
	struct ocp_enet_private *dev = emac;
	struct ocp_func_emac_data *emacdata = dev->def->additions;

	/* Check if we need to attach to a RGMII */
	if (emacdata->rgmii_idx >= 0 && rgmii_valid_mode(emacdata->phy_mode)) {
		dev->rgmii_input = emacdata->rgmii_mux;
		dev->rgmii_dev =
		    ocp_find_device(OCP_VENDOR_IBM, OCP_FUNC_RGMII,
				    emacdata->rgmii_idx);
		if (!dev->rgmii_dev) {
			printk(KERN_ERR "emac%d: unknown rgmii%d!\n",
			       dev->def->index, emacdata->rgmii_idx);
			return -ENODEV;
		}
		if (rgmii_init
		    (dev->rgmii_dev, dev->rgmii_input, emacdata->phy_mode)) {
			printk(KERN_ERR
			       "emac%d: rgmii%d initialization failed!\n",
			       dev->def->index, emacdata->rgmii_idx);
			return -ENODEV;
		}
	}
	return 0;
}

void rgmii_set_speed(struct ocp_device *ocpdev, int input, int speed)
{
	struct ibm_ocp_rgmii *dev = ocp_get_drvdata(ocpdev);
	u32 ssr = in_be32(&dev->base->ssr) & ~RGMII_SSR_MASK(input);

	RGMII_DBG("%d: speed(%d, %d)" NL, ocpdev->def->index, input, speed);

	if (speed == SPEED_1000)
		ssr |= RGMII_SSR_1000(input);
	else if (speed == SPEED_100)
		ssr |= RGMII_SSR_100(input);

	out_be32(&dev->base->ssr, ssr);
}

void __exit __rgmii_fini(struct ocp_device *ocpdev, int input)
{
	struct ibm_ocp_rgmii *dev = ocp_get_drvdata(ocpdev);
	BUG_ON(!dev || dev->users == 0);

	RGMII_DBG("%d: fini(%d)" NL, ocpdev->def->index, input);

	/* Disable this input */
	out_be32(&dev->base->fer,
		 in_be32(&dev->base->fer) & ~RGMII_FER_MASK(input));

	if (!--dev->users) {
		/* Free everything if this is the last user */
		ocp_set_drvdata(ocpdev, NULL);
		iounmap((void *)dev->base);
		kfree(dev);
	}
}

int __rgmii_get_regs_len(struct ocp_device *ocpdev)
{
	return sizeof(struct emac_ethtool_regs_subhdr) +
	    sizeof(struct rgmii_regs);
}

void *rgmii_dump_regs(struct ocp_device *ocpdev, void *buf)
{
	struct ibm_ocp_rgmii *dev = ocp_get_drvdata(ocpdev);
	struct emac_ethtool_regs_subhdr *hdr = buf;
	struct rgmii_regs *regs = (struct rgmii_regs *)(hdr + 1);

	hdr->version = 0;
	hdr->index = ocpdev->def->index;
	memcpy_fromio(regs, dev->base, sizeof(struct rgmii_regs));
	return regs + 1;
}
