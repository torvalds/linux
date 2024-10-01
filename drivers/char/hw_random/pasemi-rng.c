// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2006-2007 PA Semi, Inc
 *
 * Maintained by: Olof Johansson <olof@lixom.net>
 *
 * Driver for the PWRficient onchip rng
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/hw_random.h>
#include <linux/delay.h>
#include <linux/io.h>

#define SDCRNG_CTL_REG			0x00
#define   SDCRNG_CTL_FVLD_M		0x0000f000
#define   SDCRNG_CTL_FVLD_S		12
#define   SDCRNG_CTL_KSZ		0x00000800
#define   SDCRNG_CTL_RSRC_CRG		0x00000010
#define   SDCRNG_CTL_RSRC_RRG		0x00000000
#define   SDCRNG_CTL_CE			0x00000004
#define   SDCRNG_CTL_RE			0x00000002
#define   SDCRNG_CTL_DR			0x00000001
#define   SDCRNG_CTL_SELECT_RRG_RNG	(SDCRNG_CTL_RE | SDCRNG_CTL_RSRC_RRG)
#define   SDCRNG_CTL_SELECT_CRG_RNG	(SDCRNG_CTL_CE | SDCRNG_CTL_RSRC_CRG)
#define SDCRNG_VAL_REG			0x20

#define MODULE_NAME "pasemi_rng"

static int pasemi_rng_data_present(struct hwrng *rng, int wait)
{
	void __iomem *rng_regs = (void __iomem *)rng->priv;
	int data, i;

	for (i = 0; i < 20; i++) {
		data = (in_le32(rng_regs + SDCRNG_CTL_REG)
			& SDCRNG_CTL_FVLD_M) ? 1 : 0;
		if (data || !wait)
			break;
		udelay(10);
	}
	return data;
}

static int pasemi_rng_data_read(struct hwrng *rng, u32 *data)
{
	void __iomem *rng_regs = (void __iomem *)rng->priv;
	*data = in_le32(rng_regs + SDCRNG_VAL_REG);
	return 4;
}

static int pasemi_rng_init(struct hwrng *rng)
{
	void __iomem *rng_regs = (void __iomem *)rng->priv;
	u32 ctl;

	ctl = SDCRNG_CTL_DR | SDCRNG_CTL_SELECT_RRG_RNG | SDCRNG_CTL_KSZ;
	out_le32(rng_regs + SDCRNG_CTL_REG, ctl);
	out_le32(rng_regs + SDCRNG_CTL_REG, ctl & ~SDCRNG_CTL_DR);

	return 0;
}

static void pasemi_rng_cleanup(struct hwrng *rng)
{
	void __iomem *rng_regs = (void __iomem *)rng->priv;
	u32 ctl;

	ctl = SDCRNG_CTL_RE | SDCRNG_CTL_CE;
	out_le32(rng_regs + SDCRNG_CTL_REG,
		 in_le32(rng_regs + SDCRNG_CTL_REG) & ~ctl);
}

static struct hwrng pasemi_rng = {
	.name		= MODULE_NAME,
	.init		= pasemi_rng_init,
	.cleanup	= pasemi_rng_cleanup,
	.data_present	= pasemi_rng_data_present,
	.data_read	= pasemi_rng_data_read,
};

static int rng_probe(struct platform_device *pdev)
{
	void __iomem *rng_regs;

	rng_regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rng_regs))
		return PTR_ERR(rng_regs);

	pasemi_rng.priv = (unsigned long)rng_regs;

	pr_info("Registering PA Semi RNG\n");
	return devm_hwrng_register(&pdev->dev, &pasemi_rng);
}

static const struct of_device_id rng_match[] = {
	{ .compatible      = "1682m-rng", },
	{ .compatible      = "pasemi,pwrficient-rng", },
	{ },
};
MODULE_DEVICE_TABLE(of, rng_match);

static struct platform_driver rng_driver = {
	.driver = {
		.name = "pasemi-rng",
		.of_match_table = rng_match,
	},
	.probe		= rng_probe,
};

module_platform_driver(rng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Egor Martovetsky <egor@pasemi.com>");
MODULE_DESCRIPTION("H/W RNG driver for PA Semi processor");
