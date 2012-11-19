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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "../w1.h"
#include "../w1_int.h"
#include "../w1_log.h"

/* According to the mx27 Datasheet the reset procedure should take up to about
 * 1350us. We set the timeout to 500*100us = 50ms for sure */
#define MXC_W1_RESET_TIMEOUT 500

/*
 * MXC W1 Register offsets
 */
#define MXC_W1_CONTROL          0x00
#define MXC_W1_TIME_DIVIDER     0x02
#define MXC_W1_RESET            0x04
#define MXC_W1_COMMAND          0x06
#define MXC_W1_TXRX             0x08
#define MXC_W1_INTERRUPT        0x0A
#define MXC_W1_INTERRUPT_EN     0x0C

struct mxc_w1_device {
	void __iomem *regs;
	unsigned int clkdiv;
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

	__raw_writeb(0x80, (dev->regs + MXC_W1_CONTROL));

	while (1) {
		reg_val = __raw_readb(dev->regs + MXC_W1_CONTROL);

		if (((reg_val >> 7) & 0x1) == 0 ||
		    timeout_cnt > MXC_W1_RESET_TIMEOUT)
			break;
		else
			timeout_cnt++;

		udelay(100);
	}
	return (reg_val >> 7) & 0x1;
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

	__raw_writeb((1 << (5 - bit)), ctrl_addr);

	while (timeout_cnt--) {
		if (!((__raw_readb(ctrl_addr) >> (5 - bit)) & 0x1))
			break;

		udelay(1);
	}

	return ((__raw_readb(ctrl_addr)) >> 3) & 0x1;
}

static int mxc_w1_probe(struct platform_device *pdev)
{
	struct mxc_w1_device *mdev;
	struct resource *res;
	int err = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	mdev = kzalloc(sizeof(struct mxc_w1_device), GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;

	mdev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(mdev->clk)) {
		err = PTR_ERR(mdev->clk);
		goto failed_clk;
	}

	mdev->clkdiv = (clk_get_rate(mdev->clk) / 1000000) - 1;

	res = request_mem_region(res->start, resource_size(res),
				"mxc_w1");
	if (!res) {
		err = -EBUSY;
		goto failed_req;
	}

	mdev->regs = ioremap(res->start, resource_size(res));
	if (!mdev->regs) {
		dev_err(&pdev->dev, "Cannot map mxc_w1 registers\n");
		goto failed_ioremap;
	}

	clk_prepare_enable(mdev->clk);
	__raw_writeb(mdev->clkdiv, mdev->regs + MXC_W1_TIME_DIVIDER);

	mdev->bus_master.data = mdev;
	mdev->bus_master.reset_bus = mxc_w1_ds2_reset_bus;
	mdev->bus_master.touch_bit = mxc_w1_ds2_touch_bit;

	err = w1_add_master_device(&mdev->bus_master);

	if (err)
		goto failed_add;

	platform_set_drvdata(pdev, mdev);
	return 0;

failed_add:
	iounmap(mdev->regs);
failed_ioremap:
	release_mem_region(res->start, resource_size(res));
failed_req:
	clk_put(mdev->clk);
failed_clk:
	kfree(mdev);
	return err;
}

/*
 * disassociate the w1 device from the driver
 */
static int __devexit mxc_w1_remove(struct platform_device *pdev)
{
	struct mxc_w1_device *mdev = platform_get_drvdata(pdev);
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	w1_remove_master_device(&mdev->bus_master);

	iounmap(mdev->regs);
	release_mem_region(res->start, resource_size(res));
	clk_disable_unprepare(mdev->clk);
	clk_put(mdev->clk);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver mxc_w1_driver = {
	.driver = {
		   .name = "mxc_w1",
	},
	.probe = mxc_w1_probe,
	.remove = __devexit_p(mxc_w1_remove),
};
module_platform_driver(mxc_w1_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Freescale Semiconductors Inc");
MODULE_DESCRIPTION("Driver for One-Wire on MXC");
