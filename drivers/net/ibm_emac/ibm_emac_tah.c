/*
 * drivers/net/ibm_emac/ibm_emac_tah.c
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, TAH support.
 *
 * Copyright 2004 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright (c) 2005 Eugene Surovegin <ebs@ebshome.net>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/config.h>
#include <asm/io.h>

#include "ibm_emac_core.h"

static int __init tah_init(struct ocp_device *ocpdev)
{
	struct tah_regs *p;

	if (ocp_get_drvdata(ocpdev)) {
		printk(KERN_ERR "tah%d: already in use!\n", ocpdev->def->index);
		return -EBUSY;
	}

	/* Initialize TAH and enable IPv4 checksum verification, no TSO yet */
	p = (struct tah_regs *)ioremap(ocpdev->def->paddr, sizeof(*p));
	if (!p) {
		printk(KERN_ERR "tah%d: could not ioremap device registers!\n",
		       ocpdev->def->index);
		return -ENOMEM;
	}
	ocp_set_drvdata(ocpdev, p);
	__tah_reset(ocpdev);

	return 0;
}

int __init tah_attach(void *emac)
{
	struct ocp_enet_private *dev = emac;
	struct ocp_func_emac_data *emacdata = dev->def->additions;

	/* Check if we need to attach to a TAH */
	if (emacdata->tah_idx >= 0) {
		dev->tah_dev = ocp_find_device(OCP_ANY_ID, OCP_FUNC_TAH,
					       emacdata->tah_idx);
		if (!dev->tah_dev) {
			printk(KERN_ERR "emac%d: unknown tah%d!\n",
			       dev->def->index, emacdata->tah_idx);
			return -ENODEV;
		}
		if (tah_init(dev->tah_dev)) {
			printk(KERN_ERR
			       "emac%d: tah%d initialization failed!\n",
			       dev->def->index, emacdata->tah_idx);
			return -ENODEV;
		}
	}
	return 0;
}

void __exit __tah_fini(struct ocp_device *ocpdev)
{
	struct tah_regs *p = ocp_get_drvdata(ocpdev);
	BUG_ON(!p);
	ocp_set_drvdata(ocpdev, NULL);
	iounmap((void *)p);
}

void __tah_reset(struct ocp_device *ocpdev)
{
	struct tah_regs *p = ocp_get_drvdata(ocpdev);
	int n;

	/* Reset TAH */
	out_be32(&p->mr, TAH_MR_SR);
	n = 100;
	while ((in_be32(&p->mr) & TAH_MR_SR) && n)
		--n;

	if (unlikely(!n))
		printk(KERN_ERR "tah%d: reset timeout\n", ocpdev->def->index);

	/* 10KB TAH TX FIFO accomodates the max MTU of 9000 */
	out_be32(&p->mr,
		 TAH_MR_CVR | TAH_MR_ST_768 | TAH_MR_TFS_10KB | TAH_MR_DTFP |
		 TAH_MR_DIG);
}

int __tah_get_regs_len(struct ocp_device *ocpdev)
{
	return sizeof(struct emac_ethtool_regs_subhdr) +
	    sizeof(struct tah_regs);
}

void *tah_dump_regs(struct ocp_device *ocpdev, void *buf)
{
	struct tah_regs *dev = ocp_get_drvdata(ocpdev);
	struct emac_ethtool_regs_subhdr *hdr = buf;
	struct tah_regs *regs = (struct tah_regs *)(hdr + 1);

	hdr->version = 0;
	hdr->index = ocpdev->def->index;
	memcpy_fromio(regs, dev, sizeof(struct tah_regs));
	return regs + 1;
}
