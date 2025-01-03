/*
 * (C) Copyright 2009-2010
 * Nokia Siemens Networks, michael.lawnick.ext@nsn.com
 *
 * Portions Copyright (C) 2010 - 2016 Cavium, Inc.
 *
 * This is a driver for the i2c adapter in Cavium Networks' OCTEON processors.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/octeon/octeon.h>
#include "i2c-octeon-core.h"

#define DRV_NAME "i2c-octeon"

/**
 * octeon_i2c_int_enable - enable the CORE interrupt
 * @i2c: The struct octeon_i2c
 *
 * The interrupt will be asserted when there is non-STAT_IDLE state in
 * the SW_TWSI_EOP_TWSI_STAT register.
 */
static void octeon_i2c_int_enable(struct octeon_i2c *i2c)
{
	octeon_i2c_write_int(i2c, TWSI_INT_CORE_EN);
}

/* disable the CORE interrupt */
static void octeon_i2c_int_disable(struct octeon_i2c *i2c)
{
	/* clear TS/ST/IFLG events */
	octeon_i2c_write_int(i2c, 0);
}

/**
 * octeon_i2c_int_enable78 - enable the CORE interrupt
 * @i2c: The struct octeon_i2c
 *
 * The interrupt will be asserted when there is non-STAT_IDLE state in the
 * SW_TWSI_EOP_TWSI_STAT register.
 */
static void octeon_i2c_int_enable78(struct octeon_i2c *i2c)
{
	atomic_inc_return(&i2c->int_enable_cnt);
	enable_irq(i2c->irq);
}

static void __octeon_i2c_irq_disable(atomic_t *cnt, int irq)
{
	int count;

	/*
	 * The interrupt can be disabled in two places, but we only
	 * want to make the disable_irq_nosync() call once, so keep
	 * track with the atomic variable.
	 */
	count = atomic_dec_if_positive(cnt);
	if (count >= 0)
		disable_irq_nosync(irq);
}

/* disable the CORE interrupt */
static void octeon_i2c_int_disable78(struct octeon_i2c *i2c)
{
	__octeon_i2c_irq_disable(&i2c->int_enable_cnt, i2c->irq);
}

/**
 * octeon_i2c_hlc_int_enable78 - enable the ST interrupt
 * @i2c: The struct octeon_i2c
 *
 * The interrupt will be asserted when there is non-STAT_IDLE state in
 * the SW_TWSI_EOP_TWSI_STAT register.
 */
static void octeon_i2c_hlc_int_enable78(struct octeon_i2c *i2c)
{
	atomic_inc_return(&i2c->hlc_int_enable_cnt);
	enable_irq(i2c->hlc_irq);
}

/* disable the ST interrupt */
static void octeon_i2c_hlc_int_disable78(struct octeon_i2c *i2c)
{
	__octeon_i2c_irq_disable(&i2c->hlc_int_enable_cnt, i2c->hlc_irq);
}

/* HLC interrupt service routine */
static irqreturn_t octeon_i2c_hlc_isr78(int irq, void *dev_id)
{
	struct octeon_i2c *i2c = dev_id;

	i2c->hlc_int_disable(i2c);
	wake_up(&i2c->queue);

	return IRQ_HANDLED;
}

static void octeon_i2c_hlc_int_enable(struct octeon_i2c *i2c)
{
	octeon_i2c_write_int(i2c, TWSI_INT_ST_EN);
}

static u32 octeon_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK) |
	       I2C_FUNC_SMBUS_READ_BLOCK_DATA | I2C_SMBUS_BLOCK_PROC_CALL;
}

static const struct i2c_algorithm octeon_i2c_algo = {
	.xfer = octeon_i2c_xfer,
	.functionality = octeon_i2c_functionality,
};

static const struct i2c_adapter octeon_i2c_ops = {
	.owner = THIS_MODULE,
	.name = "OCTEON adapter",
	.algo = &octeon_i2c_algo,
};

static int octeon_i2c_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	int irq, result = 0, hlc_irq = 0;
	struct octeon_i2c *i2c;
	bool cn78xx_style;

	cn78xx_style = of_device_is_compatible(node, "cavium,octeon-7890-twsi");
	if (cn78xx_style) {
		hlc_irq = platform_get_irq(pdev, 0);
		if (hlc_irq < 0)
			return hlc_irq;

		irq = platform_get_irq(pdev, 2);
		if (irq < 0)
			return irq;
	} else {
		/* All adaptors have an irq.  */
		irq = platform_get_irq(pdev, 0);
		if (irq < 0)
			return irq;
	}

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c) {
		result = -ENOMEM;
		goto out;
	}
	i2c->dev = &pdev->dev;

	i2c->roff.sw_twsi = 0x00;
	i2c->roff.twsi_int = 0x10;
	i2c->roff.sw_twsi_ext = 0x18;

	i2c->twsi_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(i2c->twsi_base)) {
		result = PTR_ERR(i2c->twsi_base);
		goto out;
	}

	/*
	 * "clock-rate" is a legacy binding, the official binding is
	 * "clock-frequency".  Try the official one first and then
	 * fall back if it doesn't exist.
	 */
	if (of_property_read_u32(node, "clock-frequency", &i2c->twsi_freq) &&
	    of_property_read_u32(node, "clock-rate", &i2c->twsi_freq)) {
		dev_err(i2c->dev,
			"no I2C 'clock-rate' or 'clock-frequency' property\n");
		result = -ENXIO;
		goto out;
	}

	i2c->sys_freq = octeon_get_io_clock_rate();

	init_waitqueue_head(&i2c->queue);

	i2c->irq = irq;

	if (cn78xx_style) {
		i2c->hlc_irq = hlc_irq;

		i2c->int_enable = octeon_i2c_int_enable78;
		i2c->int_disable = octeon_i2c_int_disable78;
		i2c->hlc_int_enable = octeon_i2c_hlc_int_enable78;
		i2c->hlc_int_disable = octeon_i2c_hlc_int_disable78;

		irq_set_status_flags(i2c->irq, IRQ_NOAUTOEN);
		irq_set_status_flags(i2c->hlc_irq, IRQ_NOAUTOEN);

		result = devm_request_irq(&pdev->dev, i2c->hlc_irq,
					  octeon_i2c_hlc_isr78, 0,
					  DRV_NAME, i2c);
		if (result < 0) {
			dev_err(i2c->dev, "failed to attach interrupt\n");
			goto out;
		}
	} else {
		i2c->int_enable = octeon_i2c_int_enable;
		i2c->int_disable = octeon_i2c_int_disable;
		i2c->hlc_int_enable = octeon_i2c_hlc_int_enable;
		i2c->hlc_int_disable = octeon_i2c_int_disable;
	}

	result = devm_request_irq(&pdev->dev, i2c->irq,
				  octeon_i2c_isr, 0, DRV_NAME, i2c);
	if (result < 0) {
		dev_err(i2c->dev, "failed to attach interrupt\n");
		goto out;
	}

	if (OCTEON_IS_MODEL(OCTEON_CN38XX))
		i2c->broken_irq_check = true;

	result = octeon_i2c_init_lowlevel(i2c);
	if (result) {
		dev_err(i2c->dev, "init low level failed\n");
		goto  out;
	}

	octeon_i2c_set_clock(i2c);

	i2c->adap = octeon_i2c_ops;
	i2c->adap.timeout = msecs_to_jiffies(2);
	i2c->adap.retries = 5;
	i2c->adap.bus_recovery_info = &octeon_i2c_recovery_info;
	i2c->adap.dev.parent = &pdev->dev;
	i2c->adap.dev.of_node = node;
	i2c_set_adapdata(&i2c->adap, i2c);
	platform_set_drvdata(pdev, i2c);

	result = i2c_add_adapter(&i2c->adap);
	if (result < 0)
		goto out;
	dev_info(i2c->dev, "probed\n");
	return 0;

out:
	return result;
};

static void octeon_i2c_remove(struct platform_device *pdev)
{
	struct octeon_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adap);
};

static const struct of_device_id octeon_i2c_match[] = {
	{ .compatible = "cavium,octeon-3860-twsi", },
	{ .compatible = "cavium,octeon-7890-twsi", },
	{},
};
MODULE_DEVICE_TABLE(of, octeon_i2c_match);

static struct platform_driver octeon_i2c_driver = {
	.probe		= octeon_i2c_probe,
	.remove		= octeon_i2c_remove,
	.driver		= {
		.name	= DRV_NAME,
		.of_match_table = octeon_i2c_match,
	},
};

module_platform_driver(octeon_i2c_driver);

MODULE_AUTHOR("Michael Lawnick <michael.lawnick.ext@nsn.com>");
MODULE_DESCRIPTION("I2C-Bus adapter for Cavium OCTEON processors");
MODULE_LICENSE("GPL");
