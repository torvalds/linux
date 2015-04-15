/*
 *  Wondermedia I2C Master Mode Driver
 *
 *  Copyright (C) 2012 Tony Prisk <linux@prisktech.co.nz>
 *
 *  Derived from GPLv2+ licensed source:
 *  - Copyright (C) 2008 WonderMedia Technologies, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2, or
 *  (at your option) any later version. as published by the Free Software
 *  Foundation
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#define REG_CR		0x00
#define REG_TCR		0x02
#define REG_CSR		0x04
#define REG_ISR		0x06
#define REG_IMR		0x08
#define REG_CDR		0x0A
#define REG_TR		0x0C
#define REG_MCR		0x0E
#define REG_SLAVE_CR	0x10
#define REG_SLAVE_SR	0x12
#define REG_SLAVE_ISR	0x14
#define REG_SLAVE_IMR	0x16
#define REG_SLAVE_DR	0x18
#define REG_SLAVE_TR	0x1A

/* REG_CR Bit fields */
#define CR_TX_NEXT_ACK		0x0000
#define CR_ENABLE		0x0001
#define CR_TX_NEXT_NO_ACK	0x0002
#define CR_TX_END		0x0004
#define CR_CPU_RDY		0x0008
#define SLAV_MODE_SEL		0x8000

/* REG_TCR Bit fields */
#define TCR_STANDARD_MODE	0x0000
#define TCR_MASTER_WRITE	0x0000
#define TCR_HS_MODE		0x2000
#define TCR_MASTER_READ		0x4000
#define TCR_FAST_MODE		0x8000
#define TCR_SLAVE_ADDR_MASK	0x007F

/* REG_ISR Bit fields */
#define ISR_NACK_ADDR		0x0001
#define ISR_BYTE_END		0x0002
#define ISR_SCL_TIMEOUT		0x0004
#define ISR_WRITE_ALL		0x0007

/* REG_IMR Bit fields */
#define IMR_ENABLE_ALL		0x0007

/* REG_CSR Bit fields */
#define CSR_RCV_NOT_ACK		0x0001
#define CSR_RCV_ACK_MASK	0x0001
#define CSR_READY_MASK		0x0002

/* REG_TR */
#define SCL_TIMEOUT(x)		(((x) & 0xFF) << 8)
#define TR_STD			0x0064
#define TR_HS			0x0019

/* REG_MCR */
#define MCR_APB_96M		7
#define MCR_APB_166M		12

#define I2C_MODE_STANDARD	0
#define I2C_MODE_FAST		1

#define WMT_I2C_TIMEOUT		(msecs_to_jiffies(1000))

struct wmt_i2c_dev {
	struct i2c_adapter	adapter;
	struct completion	complete;
	struct device		*dev;
	void __iomem		*base;
	struct clk		*clk;
	int			mode;
	int			irq;
	u16			cmd_status;
};

static int wmt_i2c_wait_bus_not_busy(struct wmt_i2c_dev *i2c_dev)
{
	unsigned long timeout;

	timeout = jiffies + WMT_I2C_TIMEOUT;
	while (!(readw(i2c_dev->base + REG_CSR) & CSR_READY_MASK)) {
		if (time_after(jiffies, timeout)) {
			dev_warn(i2c_dev->dev, "timeout waiting for bus ready\n");
			return -EBUSY;
		}
		msleep(20);
	}

	return 0;
}

static int wmt_check_status(struct wmt_i2c_dev *i2c_dev)
{
	int ret = 0;

	if (i2c_dev->cmd_status & ISR_NACK_ADDR)
		ret = -EIO;

	if (i2c_dev->cmd_status & ISR_SCL_TIMEOUT)
		ret = -ETIMEDOUT;

	return ret;
}

static int wmt_i2c_write(struct i2c_adapter *adap, struct i2c_msg *pmsg,
			 int last)
{
	struct wmt_i2c_dev *i2c_dev = i2c_get_adapdata(adap);
	u16 val, tcr_val;
	int ret;
	unsigned long wait_result;
	int xfer_len = 0;

	if (!(pmsg->flags & I2C_M_NOSTART)) {
		ret = wmt_i2c_wait_bus_not_busy(i2c_dev);
		if (ret < 0)
			return ret;
	}

	if (pmsg->len == 0) {
		/*
		 * We still need to run through the while (..) once, so
		 * start at -1 and break out early from the loop
		 */
		xfer_len = -1;
		writew(0, i2c_dev->base + REG_CDR);
	} else {
		writew(pmsg->buf[0] & 0xFF, i2c_dev->base + REG_CDR);
	}

	if (!(pmsg->flags & I2C_M_NOSTART)) {
		val = readw(i2c_dev->base + REG_CR);
		val &= ~CR_TX_END;
		writew(val, i2c_dev->base + REG_CR);

		val = readw(i2c_dev->base + REG_CR);
		val |= CR_CPU_RDY;
		writew(val, i2c_dev->base + REG_CR);
	}

	reinit_completion(&i2c_dev->complete);

	if (i2c_dev->mode == I2C_MODE_STANDARD)
		tcr_val = TCR_STANDARD_MODE;
	else
		tcr_val = TCR_FAST_MODE;

	tcr_val |= (TCR_MASTER_WRITE | (pmsg->addr & TCR_SLAVE_ADDR_MASK));

	writew(tcr_val, i2c_dev->base + REG_TCR);

	if (pmsg->flags & I2C_M_NOSTART) {
		val = readw(i2c_dev->base + REG_CR);
		val |= CR_CPU_RDY;
		writew(val, i2c_dev->base + REG_CR);
	}

	while (xfer_len < pmsg->len) {
		wait_result = wait_for_completion_timeout(&i2c_dev->complete,
							msecs_to_jiffies(500));

		if (wait_result == 0)
			return -ETIMEDOUT;

		ret = wmt_check_status(i2c_dev);
		if (ret)
			return ret;

		xfer_len++;

		val = readw(i2c_dev->base + REG_CSR);
		if ((val & CSR_RCV_ACK_MASK) == CSR_RCV_NOT_ACK) {
			dev_dbg(i2c_dev->dev, "write RCV NACK error\n");
			return -EIO;
		}

		if (pmsg->len == 0) {
			val = CR_TX_END | CR_CPU_RDY | CR_ENABLE;
			writew(val, i2c_dev->base + REG_CR);
			break;
		}

		if (xfer_len == pmsg->len) {
			if (last != 1)
				writew(CR_ENABLE, i2c_dev->base + REG_CR);
		} else {
			writew(pmsg->buf[xfer_len] & 0xFF, i2c_dev->base +
								REG_CDR);
			writew(CR_CPU_RDY | CR_ENABLE, i2c_dev->base + REG_CR);
		}
	}

	return 0;
}

static int wmt_i2c_read(struct i2c_adapter *adap, struct i2c_msg *pmsg,
			int last)
{
	struct wmt_i2c_dev *i2c_dev = i2c_get_adapdata(adap);
	u16 val, tcr_val;
	int ret;
	unsigned long wait_result;
	u32 xfer_len = 0;

	if (!(pmsg->flags & I2C_M_NOSTART)) {
		ret = wmt_i2c_wait_bus_not_busy(i2c_dev);
		if (ret < 0)
			return ret;
	}

	val = readw(i2c_dev->base + REG_CR);
	val &= ~CR_TX_END;
	writew(val, i2c_dev->base + REG_CR);

	val = readw(i2c_dev->base + REG_CR);
	val &= ~CR_TX_NEXT_NO_ACK;
	writew(val, i2c_dev->base + REG_CR);

	if (!(pmsg->flags & I2C_M_NOSTART)) {
		val = readw(i2c_dev->base + REG_CR);
		val |= CR_CPU_RDY;
		writew(val, i2c_dev->base + REG_CR);
	}

	if (pmsg->len == 1) {
		val = readw(i2c_dev->base + REG_CR);
		val |= CR_TX_NEXT_NO_ACK;
		writew(val, i2c_dev->base + REG_CR);
	}

	reinit_completion(&i2c_dev->complete);

	if (i2c_dev->mode == I2C_MODE_STANDARD)
		tcr_val = TCR_STANDARD_MODE;
	else
		tcr_val = TCR_FAST_MODE;

	tcr_val |= TCR_MASTER_READ | (pmsg->addr & TCR_SLAVE_ADDR_MASK);

	writew(tcr_val, i2c_dev->base + REG_TCR);

	if (pmsg->flags & I2C_M_NOSTART) {
		val = readw(i2c_dev->base + REG_CR);
		val |= CR_CPU_RDY;
		writew(val, i2c_dev->base + REG_CR);
	}

	while (xfer_len < pmsg->len) {
		wait_result = wait_for_completion_timeout(&i2c_dev->complete,
							msecs_to_jiffies(500));

		if (!wait_result)
			return -ETIMEDOUT;

		ret = wmt_check_status(i2c_dev);
		if (ret)
			return ret;

		pmsg->buf[xfer_len] = readw(i2c_dev->base + REG_CDR) >> 8;
		xfer_len++;

		if (xfer_len == pmsg->len - 1) {
			val = readw(i2c_dev->base + REG_CR);
			val |= (CR_TX_NEXT_NO_ACK | CR_CPU_RDY);
			writew(val, i2c_dev->base + REG_CR);
		} else {
			val = readw(i2c_dev->base + REG_CR);
			val |= CR_CPU_RDY;
			writew(val, i2c_dev->base + REG_CR);
		}
	}

	return 0;
}

static int wmt_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg msgs[],
			int num)
{
	struct i2c_msg *pmsg;
	int i, is_last;
	int ret = 0;

	for (i = 0; ret >= 0 && i < num; i++) {
		is_last = ((i + 1) == num);

		pmsg = &msgs[i];
		if (pmsg->flags & I2C_M_RD)
			ret = wmt_i2c_read(adap, pmsg, is_last);
		else
			ret = wmt_i2c_write(adap, pmsg, is_last);
	}

	return (ret < 0) ? ret : i;
}

static u32 wmt_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_NOSTART;
}

static const struct i2c_algorithm wmt_i2c_algo = {
	.master_xfer	= wmt_i2c_xfer,
	.functionality	= wmt_i2c_func,
};

static irqreturn_t wmt_i2c_isr(int irq, void *data)
{
	struct wmt_i2c_dev *i2c_dev = data;

	/* save the status and write-clear it */
	i2c_dev->cmd_status = readw(i2c_dev->base + REG_ISR);
	writew(i2c_dev->cmd_status, i2c_dev->base + REG_ISR);

	complete(&i2c_dev->complete);

	return IRQ_HANDLED;
}

static int wmt_i2c_reset_hardware(struct wmt_i2c_dev *i2c_dev)
{
	int err;

	err = clk_prepare_enable(i2c_dev->clk);
	if (err) {
		dev_err(i2c_dev->dev, "failed to enable clock\n");
		return err;
	}

	err = clk_set_rate(i2c_dev->clk, 20000000);
	if (err) {
		dev_err(i2c_dev->dev, "failed to set clock = 20Mhz\n");
		clk_disable_unprepare(i2c_dev->clk);
		return err;
	}

	writew(0, i2c_dev->base + REG_CR);
	writew(MCR_APB_166M, i2c_dev->base + REG_MCR);
	writew(ISR_WRITE_ALL, i2c_dev->base + REG_ISR);
	writew(IMR_ENABLE_ALL, i2c_dev->base + REG_IMR);
	writew(CR_ENABLE, i2c_dev->base + REG_CR);
	readw(i2c_dev->base + REG_CSR);		/* read clear */
	writew(ISR_WRITE_ALL, i2c_dev->base + REG_ISR);

	if (i2c_dev->mode == I2C_MODE_STANDARD)
		writew(SCL_TIMEOUT(128) | TR_STD, i2c_dev->base + REG_TR);
	else
		writew(SCL_TIMEOUT(128) | TR_HS, i2c_dev->base + REG_TR);

	return 0;
}

static int wmt_i2c_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct wmt_i2c_dev *i2c_dev;
	struct i2c_adapter *adap;
	struct resource *res;
	int err;
	u32 clk_rate;

	i2c_dev = devm_kzalloc(&pdev->dev, sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2c_dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2c_dev->base))
		return PTR_ERR(i2c_dev->base);

	i2c_dev->irq = irq_of_parse_and_map(np, 0);
	if (!i2c_dev->irq) {
		dev_err(&pdev->dev, "irq missing or invalid\n");
		return -EINVAL;
	}

	i2c_dev->clk = of_clk_get(np, 0);
	if (IS_ERR(i2c_dev->clk)) {
		dev_err(&pdev->dev, "unable to request clock\n");
		return PTR_ERR(i2c_dev->clk);
	}

	i2c_dev->mode = I2C_MODE_STANDARD;
	err = of_property_read_u32(np, "clock-frequency", &clk_rate);
	if ((!err) && (clk_rate == 400000))
		i2c_dev->mode = I2C_MODE_FAST;

	i2c_dev->dev = &pdev->dev;

	err = devm_request_irq(&pdev->dev, i2c_dev->irq, wmt_i2c_isr, 0,
							"i2c", i2c_dev);
	if (err) {
		dev_err(&pdev->dev, "failed to request irq %i\n", i2c_dev->irq);
		return err;
	}

	adap = &i2c_dev->adapter;
	i2c_set_adapdata(adap, i2c_dev);
	strlcpy(adap->name, "WMT I2C adapter", sizeof(adap->name));
	adap->owner = THIS_MODULE;
	adap->algo = &wmt_i2c_algo;
	adap->dev.parent = &pdev->dev;
	adap->dev.of_node = pdev->dev.of_node;

	init_completion(&i2c_dev->complete);

	err = wmt_i2c_reset_hardware(i2c_dev);
	if (err) {
		dev_err(&pdev->dev, "error initializing hardware\n");
		return err;
	}

	err = i2c_add_adapter(adap);
	if (err) {
		dev_err(&pdev->dev, "failed to add adapter\n");
		return err;
	}

	platform_set_drvdata(pdev, i2c_dev);

	return 0;
}

static int wmt_i2c_remove(struct platform_device *pdev)
{
	struct wmt_i2c_dev *i2c_dev = platform_get_drvdata(pdev);

	/* Disable interrupts, clock and delete adapter */
	writew(0, i2c_dev->base + REG_IMR);
	clk_disable_unprepare(i2c_dev->clk);
	i2c_del_adapter(&i2c_dev->adapter);

	return 0;
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

MODULE_DESCRIPTION("Wondermedia I2C master-mode bus adapter");
MODULE_AUTHOR("Tony Prisk <linux@prisktech.co.nz>");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, wmt_i2c_dt_ids);
