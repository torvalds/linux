/*
 * drivers/char/hw_random/ixp4xx-rng.c
 *
 * RNG driver for Intel IXP4xx family of NPUs
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2005 (c) MontaVista Software, Inc.
 *
 * Fixes by Michael Buesch
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/hw_random.h>

#include <asm/io.h>
#include <asm/hardware.h>


static int ixp4xx_rng_data_read(struct hwrng *rng, u32 *buffer)
{
	void __iomem * rng_base = (void __iomem *)rng->priv;

	*buffer = __raw_readl(rng_base);

	return 4;
}

static struct hwrng ixp4xx_rng_ops = {
	.name		= "ixp4xx",
	.data_read	= ixp4xx_rng_data_read,
};

static int __init ixp4xx_rng_init(void)
{
	void __iomem * rng_base;
	int err;

	rng_base = ioremap(0x70002100, 4);
	if (!rng_base)
		return -ENOMEM;
	ixp4xx_rng_ops.priv = (unsigned long)rng_base;
	err = hwrng_register(&ixp4xx_rng_ops);
	if (err)
		iounmap(rng_base);

	return err;
}

static void __exit ixp4xx_rng_exit(void)
{
	void __iomem * rng_base = (void __iomem *)ixp4xx_rng_ops.priv;

	hwrng_unregister(&ixp4xx_rng_ops);
	iounmap(rng_base);
}

subsys_initcall(ixp4xx_rng_init);
module_exit(ixp4xx_rng_exit);

MODULE_AUTHOR("Deepak Saxena <dsaxena@plexity.net>");
MODULE_DESCRIPTION("H/W Random Number Generator (RNG) driver for IXP4xx");
MODULE_LICENSE("GPL");
