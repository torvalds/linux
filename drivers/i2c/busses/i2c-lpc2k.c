/*
 * Copyright (C) 2011 NXP Semiconductors
 *
 * Code portions referenced from the i2x-pxa and i2c-pnx drivers
 *
 * Make SMBus byte and word transactions work on LPC178x/7x
 * Copyright (c) 2012
 * Alexander Potashev, Emcraft Systems, aspotashev@emcraft.com
 * Anton Protopopov, Emcraft Systems, antonp@emcraft.com
 *
 * Copyright (C) 2015 Joachim Eastwood <manabian@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/time.h>

/* LPC24xx register offsets and bits */
#define LPC24XX_I2CONSET	0x00
#define LPC24XX_I2STAT		0x04
#define LPC24XX_I2DAT		0x08
#define LPC24XX_I2ADDR		0x0c
#define LPC24XX_I2SCLH		0x10
#define LPC24XX_I2SCLL		0x14
#define LPC24XX_I2CONCLR	0x18

#define LPC24XX_AA		BIT(2)
#define LPC24XX_SI		BIT(3)
#define LPC24XX_STO		BIT(4)
#define LPC24XX_STA		BIT(5)
#define LPC24XX_I2EN		BIT(6)

#define LPC24XX_STO_AA		(LPC24XX_STO | LPC24XX_AA)
#define LPC24XX_CLEAR_ALL	(LPC24XX_AA | LPC24XX_SI | LPC24XX_STO | \
				 LPC24XX_STA | LPC24XX_I2EN)

/* I2C SCL clock has different duty cycle depending on mode */
#define I2C_STD_MODE_DUTY		46
#define I2C_FAST_MODE_DUTY		36
#define I2C_FAST_MODE_PLUS_DUTY		38

/*
 * 26 possible I2C status codes, but codes applicable only
 * to master are listed here and used in this driver
 */
enum {
	M_BUS_ERROR		= 0x00,
	M_START			= 0x08,
	M_REPSTART		= 0x10,
	MX_ADDR_W_ACK		= 0x18,
	MX_ADDR_W_NACK		= 0x20,
	MX_DATA_W_ACK		= 0x28,
	MX_DATA_W_NACK		= 0x30,
	M_DATA_ARB_LOST		= 0x38,
	MR_ADDR_R_ACK		= 0x40,
	MR_ADDR_R_NACK		= 0x48,
	MR_DATA_R_ACK		= 0x50,
	MR_DATA_R_NACK		= 0x58,
	M_I2C_IDLE		= 0xf8,
};

struct lpc2k_i2c {
	void __iomem		*base;
	struct clk		*clk;
	int			irq;
	wait_queue_head_t	wait;
	struct i2c_adapter	adap;
	struct i2c_msg		*msg;
	int			msg_idx;
	int			msg_status;
	int			is_last;
};

static void i2c_lpc2k_reset(struct lpc2k_i2c *i2c)
{
	/* Will force clear all statuses */
	writel(LPC24XX_CLEAR_ALL, i2c->base + LPC24XX_I2CONCLR);
	writel(0, i2c->base + LPC24XX_I2ADDR);
	writel(LPC24XX_I2EN, i2c->base + LPC24XX_I2CONSET);
}

static int i2c_lpc2k_clear_arb(struct lpc2k_i2c *i2c)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	/*
	 * If the transfer needs to abort for some reason, we'll try to
	 * force a stop condition to clear any pending bus conditions
	 */
	writel(LPC24XX_STO, i2c->base + LPC24XX_I2CONSET);

	/* Wait for status change */
	while (readl(i2c->base + LPC24XX_I2STAT) != M_I2C_IDLE) {
		if (time_after(jiffies, timeout)) {
			/* Bus was not idle, try to reset adapter */
			i2c_lpc2k_reset(i2c);
			return -EBUSY;
		}

		cpu_relax();
	}

	return 0;
}

static void i2c_lpc2k_pump_msg(struct lpc2k_i2c *i2c)
{
	unsigned char data;
	u32 status;

	/*
	 * I2C in the LPC2xxx series is basically a state machine.
	 * Just run through the steps based on the current status.
	 */
	status = readl(i2c->base + LPC24XX_I2STAT);

	switch (status) {
	case M_START:
	case M_REPSTART:
		/* Start bit was just sent out, send out addr and dir */
		data = i2c_8bit_addr_from_msg(i2c->msg);

		writel(data, i2c->base + LPC24XX_I2DAT);
		writel(LPC24XX_STA, i2c->base + LPC24XX_I2CONCLR);
		break;

	case MX_ADDR_W_ACK:
	case MX_DATA_W_ACK:
		/*
		 * Address or data was sent out with an ACK. If there is more
		 * data to send, send it now
		 */
		if (i2c->msg_idx < i2c->msg->len) {
			writel(i2c->msg->buf[i2c->msg_idx],
			       i2c->base + LPC24XX_I2DAT);
		} else if (i2c->is_last) {
			/* Last message, send stop */
			writel(LPC24XX_STO_AA, i2c->base + LPC24XX_I2CONSET);
			writel(LPC24XX_SI, i2c->base + LPC24XX_I2CONCLR);
			i2c->msg_status = 0;
			disable_irq_nosync(i2c->irq);
		} else {
			i2c->msg_status = 0;
			disable_irq_nosync(i2c->irq);
		}

		i2c->msg_idx++;
		break;

	case MR_ADDR_R_ACK:
		/* Receive first byte from slave */
		if (i2c->msg->len == 1) {
			/* Last byte, return NACK */
			writel(LPC24XX_AA, i2c->base + LPC24XX_I2CONCLR);
		} else {
			/* Not last byte, return ACK */
			writel(LPC24XX_AA, i2c->base + LPC24XX_I2CONSET);
		}

		writel(LPC24XX_STA, i2c->base + LPC24XX_I2CONCLR);
		break;

	case MR_DATA_R_NACK:
		/*
		 * The I2C shows NACK status on reads, so we need to accept
		 * the NACK as an ACK here. This should be ok, as the real
		 * BACK would of been caught on the address write.
		 */
	case MR_DATA_R_ACK:
		/* Data was received */
		if (i2c->msg_idx < i2c->msg->len) {
			i2c->msg->buf[i2c->msg_idx] =
					readl(i2c->base + LPC24XX_I2DAT);
		}

		/* If transfer is done, send STOP */
		if (i2c->msg_idx >= i2c->msg->len - 1 && i2c->is_last) {
			writel(LPC24XX_STO_AA, i2c->base + LPC24XX_I2CONSET);
			writel(LPC24XX_SI, i2c->base + LPC24XX_I2CONCLR);
			i2c->msg_status = 0;
		}

		/* Message is done */
		if (i2c->msg_idx >= i2c->msg->len - 1) {
			i2c->msg_status = 0;
			disable_irq_nosync(i2c->irq);
		}

		/*
		 * One pre-last data input, send NACK to tell the slave that
		 * this is going to be the last data byte to be transferred.
		 */
		if (i2c->msg_idx >= i2c->msg->len - 2) {
			/* One byte left to receive - NACK */
			writel(LPC24XX_AA, i2c->base + LPC24XX_I2CONCLR);
		} else {
			/* More than one byte left to receive - ACK */
			writel(LPC24XX_AA, i2c->base + LPC24XX_I2CONSET);
		}

		writel(LPC24XX_STA, i2c->base + LPC24XX_I2CONCLR);
		i2c->msg_idx++;
		break;

	case MX_ADDR_W_NACK:
	case MX_DATA_W_NACK:
	case MR_ADDR_R_NACK:
		/* NACK processing is done */
		writel(LPC24XX_STO_AA, i2c->base + LPC24XX_I2CONSET);
		i2c->msg_status = -ENXIO;
		disable_irq_nosync(i2c->irq);
		break;

	case M_DATA_ARB_LOST:
		/* Arbitration lost */
		i2c->msg_status = -EAGAIN;

		/* Release the I2C bus */
		writel(LPC24XX_STA | LPC24XX_STO, i2c->base + LPC24XX_I2CONCLR);
		disable_irq_nosync(i2c->irq);
		break;

	default:
		/* Unexpected statuses */
		i2c->msg_status = -EIO;
		disable_irq_nosync(i2c->irq);
		break;
	}

	/* Exit on failure or all bytes transferred */
	if (i2c->msg_status != -EBUSY)
		wake_up(&i2c->wait);

	/*
	 * If `msg_status` is zero, then `lpc2k_process_msg()`
	 * is responsible for clearing the SI flag.
	 */
	if (i2c->msg_status != 0)
		writel(LPC24XX_SI, i2c->base + LPC24XX_I2CONCLR);
}

static int lpc2k_process_msg(struct lpc2k_i2c *i2c, int msgidx)
{
	/* A new transfer is kicked off by initiating a start condition */
	if (!msgidx) {
		writel(LPC24XX_STA, i2c->base + LPC24XX_I2CONSET);
	} else {
		/*
		 * A multi-message I2C transfer continues where the
		 * previous I2C transfer left off and uses the
		 * current condition of the I2C adapter.
		 */
		if (unlikely(i2c->msg->flags & I2C_M_NOSTART)) {
			WARN_ON(i2c->msg->len == 0);

			if (!(i2c->msg->flags & I2C_M_RD)) {
				/* Start transmit of data */
				writel(i2c->msg->buf[0],
				       i2c->base + LPC24XX_I2DAT);
				i2c->msg_idx++;
			}
		} else {
			/* Start or repeated start */
			writel(LPC24XX_STA, i2c->base + LPC24XX_I2CONSET);
		}

		writel(LPC24XX_SI, i2c->base + LPC24XX_I2CONCLR);
	}

	enable_irq(i2c->irq);

	/* Wait for transfer completion */
	if (wait_event_timeout(i2c->wait, i2c->msg_status != -EBUSY,
			       msecs_to_jiffies(1000)) == 0) {
		disable_irq_nosync(i2c->irq);

		return -ETIMEDOUT;
	}

	return i2c->msg_status;
}

static int i2c_lpc2k_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			  int msg_num)
{
	struct lpc2k_i2c *i2c = i2c_get_adapdata(adap);
	int ret, i;
	u32 stat;

	/* Check for bus idle condition */
	stat = readl(i2c->base + LPC24XX_I2STAT);
	if (stat != M_I2C_IDLE) {
		/* Something is holding the bus, try to clear it */
		return i2c_lpc2k_clear_arb(i2c);
	}

	/* Process a single message at a time */
	for (i = 0; i < msg_num; i++) {
		/* Save message pointer and current message data index */
		i2c->msg = &msgs[i];
		i2c->msg_idx = 0;
		i2c->msg_status = -EBUSY;
		i2c->is_last = (i == (msg_num - 1));

		ret = lpc2k_process_msg(i2c, i);
		if (ret)
			return ret;
	}

	return msg_num;
}

static irqreturn_t i2c_lpc2k_handler(int irq, void *dev_id)
{
	struct lpc2k_i2c *i2c = dev_id;

	if (readl(i2c->base + LPC24XX_I2CONSET) & LPC24XX_SI) {
		i2c_lpc2k_pump_msg(i2c);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static u32 i2c_lpc2k_functionality(struct i2c_adapter *adap)
{
	/* Only emulated SMBus for now */
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm i2c_lpc2k_algorithm = {
	.master_xfer	= i2c_lpc2k_xfer,
	.functionality	= i2c_lpc2k_functionality,
};

static int i2c_lpc2k_probe(struct platform_device *pdev)
{
	struct lpc2k_i2c *i2c;
	struct resource *res;
	u32 bus_clk_rate;
	u32 scl_high;
	u32 clkrate;
	int ret;

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2c->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2c->base))
		return PTR_ERR(i2c->base);

	i2c->irq = platform_get_irq(pdev, 0);
	if (i2c->irq < 0) {
		dev_err(&pdev->dev, "can't get interrupt resource\n");
		return i2c->irq;
	}

	init_waitqueue_head(&i2c->wait);

	i2c->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2c->clk)) {
		dev_err(&pdev->dev, "error getting clock\n");
		return PTR_ERR(i2c->clk);
	}

	ret = clk_prepare_enable(i2c->clk);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable clock.\n");
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, i2c->irq, i2c_lpc2k_handler, 0,
			       dev_name(&pdev->dev), i2c);
	if (ret < 0) {
		dev_err(&pdev->dev, "can't request interrupt.\n");
		goto fail_clk;
	}

	disable_irq_nosync(i2c->irq);

	/* Place controller is a known state */
	i2c_lpc2k_reset(i2c);

	ret = of_property_read_u32(pdev->dev.of_node, "clock-frequency",
				   &bus_clk_rate);
	if (ret)
		bus_clk_rate = 100000; /* 100 kHz default clock rate */

	clkrate = clk_get_rate(i2c->clk);
	if (clkrate == 0) {
		dev_err(&pdev->dev, "can't get I2C base clock\n");
		ret = -EINVAL;
		goto fail_clk;
	}

	/* Setup I2C dividers to generate clock with proper duty cycle */
	clkrate = clkrate / bus_clk_rate;
	if (bus_clk_rate <= 100000)
		scl_high = (clkrate * I2C_STD_MODE_DUTY) / 100;
	else if (bus_clk_rate <= 400000)
		scl_high = (clkrate * I2C_FAST_MODE_DUTY) / 100;
	else
		scl_high = (clkrate * I2C_FAST_MODE_PLUS_DUTY) / 100;

	writel(scl_high, i2c->base + LPC24XX_I2SCLH);
	writel(clkrate - scl_high, i2c->base + LPC24XX_I2SCLL);

	platform_set_drvdata(pdev, i2c);

	i2c_set_adapdata(&i2c->adap, i2c);
	i2c->adap.owner = THIS_MODULE;
	strlcpy(i2c->adap.name, "LPC2K I2C adapter", sizeof(i2c->adap.name));
	i2c->adap.algo = &i2c_lpc2k_algorithm;
	i2c->adap.dev.parent = &pdev->dev;
	i2c->adap.dev.of_node = pdev->dev.of_node;

	ret = i2c_add_adapter(&i2c->adap);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add adapter!\n");
		goto fail_clk;
	}

	dev_info(&pdev->dev, "LPC2K I2C adapter\n");

	return 0;

fail_clk:
	clk_disable_unprepare(i2c->clk);
	return ret;
}

static int i2c_lpc2k_remove(struct platform_device *dev)
{
	struct lpc2k_i2c *i2c = platform_get_drvdata(dev);

	i2c_del_adapter(&i2c->adap);
	clk_disable_unprepare(i2c->clk);

	return 0;
}

#ifdef CONFIG_PM
static int i2c_lpc2k_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lpc2k_i2c *i2c = platform_get_drvdata(pdev);

	clk_disable(i2c->clk);

	return 0;
}

static int i2c_lpc2k_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lpc2k_i2c *i2c = platform_get_drvdata(pdev);

	clk_enable(i2c->clk);
	i2c_lpc2k_reset(i2c);

	return 0;
}

static const struct dev_pm_ops i2c_lpc2k_dev_pm_ops = {
	.suspend_noirq = i2c_lpc2k_suspend,
	.resume_noirq = i2c_lpc2k_resume,
};

#define I2C_LPC2K_DEV_PM_OPS (&i2c_lpc2k_dev_pm_ops)
#else
#define I2C_LPC2K_DEV_PM_OPS NULL
#endif

static const struct of_device_id lpc2k_i2c_match[] = {
	{ .compatible = "nxp,lpc1788-i2c" },
	{},
};
MODULE_DEVICE_TABLE(of, lpc2k_i2c_match);

static struct platform_driver i2c_lpc2k_driver = {
	.probe	= i2c_lpc2k_probe,
	.remove	= i2c_lpc2k_remove,
	.driver	= {
		.name		= "lpc2k-i2c",
		.pm		= I2C_LPC2K_DEV_PM_OPS,
		.of_match_table	= lpc2k_i2c_match,
	},
};
module_platform_driver(i2c_lpc2k_driver);

MODULE_AUTHOR("Kevin Wells <kevin.wells@nxp.com>");
MODULE_DESCRIPTION("I2C driver for LPC2xxx devices");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lpc2k-i2c");
