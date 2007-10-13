/*
 * drivers/net/ibm_newemac/tah.c
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
#include <asm/io.h>

#include "emac.h"
#include "core.h"

int __devinit tah_attach(struct of_device *ofdev, int channel)
{
	struct tah_instance *dev = dev_get_drvdata(&ofdev->dev);

	mutex_lock(&dev->lock);
	/* Reset has been done at probe() time... nothing else to do for now */
	++dev->users;
	mutex_unlock(&dev->lock);

	return 0;
}

void __devexit tah_detach(struct of_device *ofdev, int channel)
{
	struct tah_instance *dev = dev_get_drvdata(&ofdev->dev);

	mutex_lock(&dev->lock);
	--dev->users;
	mutex_unlock(&dev->lock);
}

void tah_reset(struct of_device *ofdev)
{
	struct tah_instance *dev = dev_get_drvdata(&ofdev->dev);
	struct tah_regs *p = dev->base;
	int n;

	/* Reset TAH */
	out_be32(&p->mr, TAH_MR_SR);
	n = 100;
	while ((in_be32(&p->mr) & TAH_MR_SR) && n)
		--n;

	if (unlikely(!n))
		printk(KERN_ERR "%s: reset timeout\n", ofdev->node->full_name);

	/* 10KB TAH TX FIFO accomodates the max MTU of 9000 */
	out_be32(&p->mr,
		 TAH_MR_CVR | TAH_MR_ST_768 | TAH_MR_TFS_10KB | TAH_MR_DTFP |
		 TAH_MR_DIG);
}

int tah_get_regs_len(struct of_device *ofdev)
{
	return sizeof(struct emac_ethtool_regs_subhdr) +
		sizeof(struct tah_regs);
}

void *tah_dump_regs(struct of_device *ofdev, void *buf)
{
	struct tah_instance *dev = dev_get_drvdata(&ofdev->dev);
	struct emac_ethtool_regs_subhdr *hdr = buf;
	struct tah_regs *regs = (struct tah_regs *)(hdr + 1);

	hdr->version = 0;
	hdr->index = 0; /* for now, are there chips with more than one
			 * zmii ? if yes, then we'll add a cell_index
			 * like we do for emac
			 */
	memcpy_fromio(regs, dev->base, sizeof(struct tah_regs));
	return regs + 1;
}

static int __devinit tah_probe(struct of_device *ofdev,
			       const struct of_device_id *match)
{
	struct device_node *np = ofdev->node;
	struct tah_instance *dev;
	struct resource regs;
	int rc;

	rc = -ENOMEM;
	dev = kzalloc(sizeof(struct tah_instance), GFP_KERNEL);
	if (dev == NULL) {
		printk(KERN_ERR "%s: could not allocate TAH device!\n",
		       np->full_name);
		goto err_gone;
	}

	mutex_init(&dev->lock);
	dev->ofdev = ofdev;

	rc = -ENXIO;
	if (of_address_to_resource(np, 0, &regs)) {
		printk(KERN_ERR "%s: Can't get registers address\n",
		       np->full_name);
		goto err_free;
	}

	rc = -ENOMEM;
	dev->base = (struct tah_regs *)ioremap(regs.start,
					       sizeof(struct tah_regs));
	if (dev->base == NULL) {
		printk(KERN_ERR "%s: Can't map device registers!\n",
		       np->full_name);
		goto err_free;
	}

	/* Initialize TAH and enable IPv4 checksum verification, no TSO yet */
	tah_reset(ofdev);

	printk(KERN_INFO
	       "TAH %s initialized\n", ofdev->node->full_name);
	wmb();
	dev_set_drvdata(&ofdev->dev, dev);

	return 0;

 err_free:
	kfree(dev);
 err_gone:
	return rc;
}

static int __devexit tah_remove(struct of_device *ofdev)
{
	struct tah_instance *dev = dev_get_drvdata(&ofdev->dev);

	dev_set_drvdata(&ofdev->dev, NULL);

	WARN_ON(dev->users != 0);

	iounmap(dev->base);
	kfree(dev);

	return 0;
}

static struct of_device_id tah_match[] =
{
	{
		.type		= "tah",
	},
	{},
};

static struct of_platform_driver tah_driver = {
	.name = "emac-tah",
	.match_table = tah_match,

	.probe = tah_probe,
	.remove = tah_remove,
};

int __init tah_init(void)
{
	return of_register_platform_driver(&tah_driver);
}

void tah_exit(void)
{
	of_unregister_platform_driver(&tah_driver);
}
