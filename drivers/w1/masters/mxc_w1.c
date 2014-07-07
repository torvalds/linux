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
#include <linux/module.h>
#include <linux/platform_device.h>

#include "../w1.h"
#include "../w1_int.h"

/* According to the mx27 Datasheet the reset procedure should take up to about
 * 1350us. We set the timeout to 500*100us = 50ms for sure */
#define MXC_W1_RESET_TIMEOUT 500

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
	u8 reg_val;
	unsigned int timeout_cnt = 0;
	struct mxc_w1_device *dev = data;

	writeb(MXC_W1_CONTROL_RPP, (dev->regs + MXC_W1_CONTROL));

	while (1) {
		reg_val = readb(dev->regs + MXC_W1_CONTROL);

		if (!(reg_val & MXC_W1_CONTROL_RPP) ||
		    timeout_cnt > MXC_W1_RESET_TIMEOUT)
			break;
		else
			timeout_cnt++;

		udelay(100);
	}
	return !(reg_val & MXC_W1_CONTROL_PST);
}

/*
 * this is the low level routine to read/write a bit on the One Wire
 * interface on the hardware. It does write 0 if parameter bit is set
 * to 0, otherwise a write 1/read.
 */
static u8 mxc_w1_ds2_touch_bit(void *data, u8 bit)
{
	struct mxc_w1_device *mdev = data;
	void __iomem *ctrl_addr = mdev->regs + MXC_W1_CONTROL;
	unsigned int timeout_cnt = 400; /* Takes max. 120us according to
					 * datasheet.
					 */

	writeb(MXC_W1_CONTROL_WR(bit), ctrl_addr);

	while (timeout_cnt--) {
		if (!(readb(ctrl_addr) & MXC_W1_CONTROL_WR(bit)))
			break;

		udelay(1);
	}

	return !!(readb(ctrl_addr) & MXC_W1_CONTROL_RDST);
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
		.owner = THIS_MODULE,
		.of_match_table = mxc_w1_dt_ids,
	},
	.probe = mxc_w1_probe,
	.remove = mxc_w1_remove,
};
module_platform_driver(mxc_w1_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Freescale Semiconductors Inc");
MODULE_DESCRIPTION("Driver for One-Wire on MXC");
