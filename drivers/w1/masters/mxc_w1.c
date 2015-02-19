/*
 * Copyright 2005-2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Luotao Fu, kernel@pengutronix.de
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "../w1.h"
#include "../w1_int.h"

/*
 * MXC W1 Register offsets
 */
#define MXC_W1_CONTROL		0x00
# define MXC_W1_CONTROL_RDST	BIT(3)
# define MXC_W1_CONTROL_WR(x)	BIT(5 - (x))
# define MXC_W1_CONTROL_PST	BIT(6)
# define MXC_W1_CONTROL_RPP	BIT(7)
#define MXC_W1_TIME_DIVIDER	0x02
#define MXC_W1_RESET		0x04
# define MXC_W1_RESET_RST	BIT(0)

struct mxc_w1_device {
	void __iomem *regs;
	struct clk *clk;
	struct w1_bus_master bus_master;
};

/*
 * this is the low level routine to
 * reset the device on the One Wire interface
 * on the hardware
 */
static u8 mxc_w1_ds2_reset_bus(void *data)
{
	struct mxc_w1_device *dev = data;
	unsigned long timeout;

	writeb(MXC_W1_CONTROL_RPP, dev->regs + MXC_W1_CONTROL);

	/* Wait for reset sequence 511+512us, use 1500us for sure */
	timeout = jiffies + usecs_to_jiffies(1500);

	udelay(511 + 512);

	do {
		u8 ctrl = readb(dev->regs + MXC_W1_CONTROL);

		/* PST bit is valid after the RPP bit is self-cleared */
		if (!(ctrl & MXC_W1_CONTROL_RPP))
			return !(ctrl & MXC_W1_CONTROL_PST);
	} while (time_is_after_jiffies(timeout));

	return 1;
}

/*
 * this is the low level routine to read/write a bit on the One Wire
 * interface on the hardware. It does write 0 if parameter bit is set
 * to 0, otherwise a write 1/read.
 */
static u8 mxc_w1_ds2_touch_bit(void *data, u8 bit)
{
	struct mxc_w1_device *dev = data;
	unsigned long timeout;

	writeb(MXC_W1_CONTROL_WR(bit), dev->regs + MXC_W1_CONTROL);

	/* Wait for read/write bit (60us, Max 120us), use 200us for sure */
	timeout = jiffies + usecs_to_jiffies(200);

	udelay(60);

	do {
		u8 ctrl = readb(dev->regs + MXC_W1_CONTROL);

		/* RDST bit is valid after the WR1/RD bit is self-cleared */
		if (!(ctrl & MXC_W1_CONTROL_WR(bit)))
			return !!(ctrl & MXC_W1_CONTROL_RDST);
	} while (time_is_after_jiffies(timeout));

	return 0;
}

static int mxc_w1_probe(struct platform_device *pdev)
{
	struct mxc_w1_device *mdev;
	unsigned long clkrate;
	struct resource *res;
	unsigned int clkdiv;
	int err;

	mdev = devm_kzalloc(&pdev->dev, sizeof(struct mxc_w1_device),
			    GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;

	mdev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(mdev->clk))
		return PTR_ERR(mdev->clk);

	clkrate = clk_get_rate(mdev->clk);
	if (clkrate < 10000000)
		dev_warn(&pdev->dev,
			 "Low clock frequency causes improper function\n");

	clkdiv = DIV_ROUND_CLOSEST(clkrate, 1000000);
	clkrate /= clkdiv;
	if ((clkrate < 980000) || (clkrate > 1020000))
		dev_warn(&pdev->dev,
			 "Incorrect time base frequency %lu Hz\n", clkrate);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mdev->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mdev->regs))
		return PTR_ERR(mdev->regs);

	err = clk_prepare_enable(mdev->clk);
	if (err)
		return err;

	/* Software reset 1-Wire module */
	writeb(MXC_W1_RESET_RST, mdev->regs + MXC_W1_RESET);
	writeb(0, mdev->regs + MXC_W1_RESET);

	writeb(clkdiv - 1, mdev->regs + MXC_W1_TIME_DIVIDER);

	mdev->bus_master.data = mdev;
	mdev->bus_master.reset_bus = mxc_w1_ds2_reset_bus;
	mdev->bus_master.touch_bit = mxc_w1_ds2_touch_bit;

	platform_set_drvdata(pdev, mdev);

	err = w1_add_master_device(&mdev->bus_master);
	if (err)
		clk_disable_unprepare(mdev->clk);

	return err;
}

/*
 * disassociate the w1 device from the driver
 */
static int mxc_w1_remove(struct platform_device *pdev)
{
	struct mxc_w1_device *mdev = platform_get_drvdata(pdev);

	w1_remove_master_device(&mdev->bus_master);

	clk_disable_unprepare(mdev->clk);

	return 0;
}

static struct of_device_id mxc_w1_dt_ids[] = {
	{ .compatible = "fsl,imx21-owire" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxc_w1_dt_ids);

static struct platform_driver mxc_w1_driver = {
	.driver = {
		.name = "mxc_w1",
		.of_match_table = mxc_w1_dt_ids,
	},
	.probe = mxc_w1_probe,
	.remove = mxc_w1_remove,
};
module_platform_driver(mxc_w1_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Freescale Semiconductors Inc");
MODULE_DESCRIPTION("Driver for One-Wire on MXC");
