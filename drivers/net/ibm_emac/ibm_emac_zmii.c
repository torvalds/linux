/*
 * drivers/net/ibm_emac/ibm_emac_zmii.c
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, ZMII bridge support.
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

#include "ibm_emac_core.h"
#include "ibm_emac_debug.h"

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

static int __init zmii_init(struct ocp_device *ocpdev, int input, int *mode)
{
	struct ibm_ocp_zmii *dev = ocp_get_drvdata(ocpdev);
	struct zmii_regs __iomem *p;

	ZMII_DBG("%d: init(%d, %d)" NL, ocpdev->def->index, input, *mode);

	if (!dev) {
		dev = kzalloc(sizeof(struct ibm_ocp_zmii), GFP_KERNEL);
		if (!dev) {
			printk(KERN_ERR
			       "zmii%d: couldn't allocate device structure!\n",
			       ocpdev->def->index);
			return -ENOMEM;
		}
		dev->mode = PHY_MODE_NA;

		p = ioremap(ocpdev->def->paddr, sizeof(struct zmii_regs));
		if (!p) {
			printk(KERN_ERR
			       "zmii%d: could not ioremap device registers!\n",
			       ocpdev->def->index);
			kfree(dev);
			return -ENOMEM;
		}
		dev->base = p;
		ocp_set_drvdata(ocpdev, dev);
		
		/* We may need FER value for autodetection later */
		dev->fer_save = in_be32(&p->fer);

		/* Disable all inputs by default */
		out_be32(&p->fer, 0);
	} else
		p = dev->base;

	if (!zmii_valid_mode(*mode)) {
		/* Probably an EMAC connected to RGMII, 
		 * but it still may need ZMII for MDIO 
		 */
		goto out;
	}

	/* Autodetect ZMII mode if not specified.
	 * This is only for backward compatibility with the old driver.
	 * Please, always specify PHY mode in your board port to avoid
	 * any surprises.
	 */
	if (dev->mode == PHY_MODE_NA) {
		if (*mode == PHY_MODE_NA) {
			u32 r = dev->fer_save;

			ZMII_DBG("%d: autodetecting mode, FER = 0x%08x" NL,
				 ocpdev->def->index, r);
			
			if (r & (ZMII_FER_MII(0) | ZMII_FER_MII(1)))
				dev->mode = PHY_MODE_MII;
			else if (r & (ZMII_FER_RMII(0) | ZMII_FER_RMII(1)))
				dev->mode = PHY_MODE_RMII;
			else
				dev->mode = PHY_MODE_SMII;
		} else
			dev->mode = *mode;

		printk(KERN_NOTICE "zmii%d: bridge in %s mode\n",
		       ocpdev->def->index, zmii_mode_name(dev->mode));
	} else {
		/* All inputs must use the same mode */
		if (*mode != PHY_MODE_NA && *mode != dev->mode) {
			printk(KERN_ERR
			       "zmii%d: invalid mode %d specified for input %d\n",
			       ocpdev->def->index, *mode, input);
			return -EINVAL;
		}
	}

	/* Report back correct PHY mode, 
	 * it may be used during PHY initialization.
	 */
	*mode = dev->mode;

	/* Enable this input */
	out_be32(&p->fer, in_be32(&p->fer) | zmii_mode_mask(dev->mode, input));
      out:
	++dev->users;
	return 0;
}

int __init zmii_attach(void *emac)
{
	struct ocp_enet_private *dev = emac;
	struct ocp_func_emac_data *emacdata = dev->def->additions;

	if (emacdata->zmii_idx >= 0) {
		dev->zmii_input = emacdata->zmii_mux;
		dev->zmii_dev =
		    ocp_find_device(OCP_VENDOR_IBM, OCP_FUNC_ZMII,
				    emacdata->zmii_idx);
		if (!dev->zmii_dev) {
			printk(KERN_ERR "emac%d: unknown zmii%d!\n",
			       dev->def->index, emacdata->zmii_idx);
			return -ENODEV;
		}
		if (zmii_init
		    (dev->zmii_dev, dev->zmii_input, &emacdata->phy_mode)) {
			printk(KERN_ERR
			       "emac%d: zmii%d initialization failed!\n",
			       dev->def->index, emacdata->zmii_idx);
			return -ENODEV;
		}
	}
	return 0;
}

void __zmii_enable_mdio(struct ocp_device *ocpdev, int input)
{
	struct ibm_ocp_zmii *dev = ocp_get_drvdata(ocpdev);
	u32 fer = in_be32(&dev->base->fer) & ~ZMII_FER_MDI_ALL;

	ZMII_DBG2("%d: mdio(%d)" NL, ocpdev->def->index, input);

	out_be32(&dev->base->fer, fer | ZMII_FER_MDI(input));
}

void __zmii_set_speed(struct ocp_device *ocpdev, int input, int speed)
{
	struct ibm_ocp_zmii *dev = ocp_get_drvdata(ocpdev);
	u32 ssr = in_be32(&dev->base->ssr);

	ZMII_DBG("%d: speed(%d, %d)" NL, ocpdev->def->index, input, speed);

	if (speed == SPEED_100)
		ssr |= ZMII_SSR_SP(input);
	else
		ssr &= ~ZMII_SSR_SP(input);

	out_be32(&dev->base->ssr, ssr);
}

void __zmii_fini(struct ocp_device *ocpdev, int input)
{
	struct ibm_ocp_zmii *dev = ocp_get_drvdata(ocpdev);
	BUG_ON(!dev || dev->users == 0);

	ZMII_DBG("%d: fini(%d)" NL, ocpdev->def->index, input);

	/* Disable this input */
	out_be32(&dev->base->fer,
		 in_be32(&dev->base->fer) & ~zmii_mode_mask(dev->mode, input));

	if (!--dev->users) {
		/* Free everything if this is the last user */
		ocp_set_drvdata(ocpdev, NULL);
		iounmap(dev->base);
		kfree(dev);
	}
}

int __zmii_get_regs_len(struct ocp_device *ocpdev)
{
	return sizeof(struct emac_ethtool_regs_subhdr) +
	    sizeof(struct zmii_regs);
}

void *zmii_dump_regs(struct ocp_device *ocpdev, void *buf)
{
	struct ibm_ocp_zmii *dev = ocp_get_drvdata(ocpdev);
	struct emac_ethtool_regs_subhdr *hdr = buf;
	struct zmii_regs *regs = (struct zmii_regs *)(hdr + 1);

	hdr->version = 0;
	hdr->index = ocpdev->def->index;
	memcpy_fromio(regs, dev->base, sizeof(struct zmii_regs));
	return regs + 1;
}
