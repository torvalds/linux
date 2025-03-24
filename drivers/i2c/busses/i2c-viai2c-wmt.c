// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Wondermedia I2C Controller Driver
 *
 *  Copyright (C) 2012 Tony Prisk <linux@prisktech.co.nz>
 *
 *  Derived from GPLv2+ licensed source:
 *  - Copyright (C) 2008 WonderMedia Technologies, Inc.
 */

#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include "i2c-viai2c-common.h"

#define REG_SLAVE_CR	0x10
#define REG_SLAVE_SR	0x12
#define REG_SLAVE_ISR	0x14
#define REG_SLAVE_IMR	0x16
#define REG_SLAVE_DR	0x18
#define REG_SLAVE_TR	0x1A

/* REG_TR */
#define SCL_TIMEOUT(x)		(((x) & 0xFF) << 8)
#define TR_STD			0x0064
#define TR_HS			0x0019

/* REG_MCR */
#define MCR_APB_96M		7
#define MCR_APB_166M		12

static u32 wmt_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_NOSTART;
}

static const struct i2c_algorithm wmt_i2c_algo = {
	.xfer = viai2c_xfer,
	.functionality = wmt_i2c_func,
};

static int wmt_i2c_reset_hardware(struct viai2c *i2c)
{
	int err;

	err = clk_prepare_enable(i2c->clk);
	if (err) {
		dev_err(i2c->dev, "failed to enable clock\n");
		return err;
	}

	err = clk_set_rate(i2c->clk, 20000000);
	if (err) {
		dev_err(i2c->dev, "failed to set clock = 20Mhz\n");
		clk_disable_unprepare(i2c->clk);
		return err;
	}

	writew(0, i2c->base + VIAI2C_REG_CR);
	writew(MCR_APB_166M, i2c->base + VIAI2C_REG_MCR);
	writew(VIAI2C_ISR_MASK_ALL, i2c->base + VIAI2C_REG_ISR);
	writew(VIAI2C_IMR_ENABLE_ALL, i2c->base + VIAI2C_REG_IMR);
	writew(VIAI2C_CR_ENABLE, i2c->base + VIAI2C_REG_CR);
	readw(i2c->base + VIAI2C_REG_CSR);		/* read clear */
	writew(VIAI2C_ISR_MASK_ALL, i2c->base + VIAI2C_REG_ISR);

	if (i2c->tcr == VIAI2C_TCR_FAST)
		writew(SCL_TIMEOUT(128) | TR_HS, i2c->base + VIAI2C_REG_TR);
	else
		writew(SCL_TIMEOUT(128) | TR_STD, i2c->base + VIAI2C_REG_TR);

	return 0;
}

static irqreturn_t wmt_i2c_isr(int irq, void *data)
{
	struct viai2c *i2c = data;
	u8 status;

	/* save the status and write-clear it */
	status = readw(i2c->base + VIAI2C_REG_ISR);
	writew(status, i2c->base + VIAI2C_REG_ISR);

	i2c->ret = 0;
	if (status & VIAI2C_ISR_NACK_ADDR)
		i2c->ret = -EIO;

	if (status & VIAI2C_ISR_SCL_TIMEOUT)
		i2c->ret = -ETIMEDOUT;

	if (!i2c->ret)
		i2c->ret = viai2c_irq_xfer(i2c);

	/* All the data has been successfully transferred or error occurred */
	if (i2c->ret)
		complete(&i2c->complete);

	return IRQ_HANDLED;
}

static int wmt_i2c_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct viai2c *i2c;
	struct i2c_adapter *adap;
	int err;
	u32 clk_rate;

	err = viai2c_init(pdev, &i2c, VIAI2C_PLAT_WMT);
	if (err)
		return err;

	i2c->irq = platform_get_irq(pdev, 0);
	if (i2c->irq < 0)
		return i2c->irq;

	err = devm_request_irq(&pdev->dev, i2c->irq, wmt_i2c_isr,
			       0, pdev->name, i2c);
	if (err)
		return dev_err_probe(&pdev->dev, err,
				"failed to request irq %i\n", i2c->irq);

	i2c->clk = of_clk_get(np, 0);
	if (IS_ERR(i2c->clk)) {
		dev_err(&pdev->dev, "unable to request clock\n");
		return PTR_ERR(i2c->clk);
	}

	err = of_property_read_u32(np, "clock-frequency", &clk_rate);
	if (!err && clk_rate == I2C_MAX_FAST_MODE_FREQ)
		i2c->tcr = VIAI2C_TCR_FAST;

	adap = &i2c->adapter;
	i2c_set_adapdata(adap, i2c);
	strscpy(adap->name, "WMT I2C adapter", sizeof(adap->name));
	adap->owner = THIS_MODULE;
	adap->algo = &wmt_i2c_algo;
	adap->dev.parent = &pdev->dev;
	adap->dev.of_node = pdev->dev.of_node;

	err = wmt_i2c_reset_hardware(i2c);
	if (err) {
		dev_err(&pdev->dev, "error initializing hardware\n");
		return err;
	}

	err = i2c_add_adapter(adap);
	if (err)
		/* wmt_i2c_reset_hardware() enables i2c_dev->clk */
		clk_disable_unprepare(i2c->clk);

	return err;
}

static void wmt_i2c_remove(struct platform_device *pdev)
{
	struct viai2c *i2c = platform_get_drvdata(pdev);

	/* Disable interrupts, clock and delete adapter */
	writew(0, i2c->base + VIAI2C_REG_IMR);
	clk_disable_unprepare(i2c->clk);
	i2c_del_adapter(&i2c->adapter);
}

static const struct of_device_id wmt_i2c_dt_ids[] = {
	{ .compatible = "wm,wm8505-i2c" },
	{ /* Sentinel */ },
};

static struct platform_driver wmt_i2c_driver = {
	.probe		= wmt_i2c_probe,
	.remove		= wmt_i2c_remove,
	.driver		= {
		.name	= "wmt-i2c",
		.of_match_table = wmt_i2c_dt_ids,
	},
};

module_platform_driver(wmt_i2c_driver);

MODULE_DESCRIPTION("Wondermedia I2C controller driver");
MODULE_AUTHOR("Tony Prisk <linux@prisktech.co.nz>");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, wmt_i2c_dt_ids);
