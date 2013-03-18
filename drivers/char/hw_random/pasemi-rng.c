/*
 * Copyright (C) 2006-2007 PA Semi, Inc
 *
 * Maintained by: Olof Johansson <olof@lixom.net>
 *
 * Driver for the PWRficient onchip rng
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/hw_random.h>
#include <linux/delay.h>
#include <linux/of_platform.h>
#include <asm/io.h>

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

static int rng_probe(struct platform_device *ofdev)
{
	void __iomem *rng_regs;
	struct device_node *rng_np = ofdev->dev.of_node;
	struct resource res;
	int err = 0;

	err = of_address_to_resource(rng_np, 0, &res);
	if (err)
		return -ENODEV;

	rng_regs = ioremap(res.start, 0x100);

	if (!rng_regs)
		return -ENOMEM;

	pasemi_rng.priv = (unsigned long)rng_regs;

	printk(KERN_INFO "Registering PA Semi RNG\n");

	err = hwrng_register(&pasemi_rng);

	if (err)
		iounmap(rng_regs);

	return err;
}

static int rng_remove(struct platform_device *dev)
{
	void __iomem *rng_regs = (void __iomem *)pasemi_rng.priv;

	hwrng_unregister(&pasemi_rng);
	iounmap(rng_regs);

	return 0;
}

static struct of_device_id rng_match[] = {
	{ .compatible      = "1682m-rng", },
	{ .compatible      = "pasemi,pwrficient-rng", },
	{ },
};

static struct platform_driver rng_driver = {
	.driver = {
		.name = "pasemi-rng",
		.owner = THIS_MODULE,
		.of_match_table = rng_match,
	},
	.probe		= rng_probe,
	.remove		= rng_remove,
};

module_platform_driver(rng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Egor Martovetsky <egor@pasemi.com>");
MODULE_DESCRIPTION("H/W RNG driver for PA Semi processor");
