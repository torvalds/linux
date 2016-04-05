/*
 *  i2c-nintendo3ds.c
 *
 *  Copyright (C) 2016 Sergi Granell
 *  based on i2c-versatile.c and i2c-exynos5.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>

#define NINTENDO3DS_I2C_NAME "nintendo3ds-i2c"

/* I2C Registers */
#define I2C_REG_DATA_OFF	0x00
#define I2C_REG_CNT_OFF		0x01
#define I2C_REG_CNTEX_OFF	0x02
#define I2C_REG_SCL_OFF		0x04

/* CNT Register bits */
#define I2C_CNT_STOP	(1 << 0)
#define I2C_CNT_START	(1 << 1)
#define I2C_CNT_PAUSE	(1 << 2)
#define I2C_CNT_ACK	(1 << 4)
#define I2C_CNT_DATADIR	(1 << 5)
#define I2C_CNT_INTEN	(1 << 6)
#define I2C_CNT_STAT	(1 << 7)

/* CNT Register data direction bit */
#define I2C_CNT_DATADIR_WR (0 << 5)
#define I2C_CNT_DATADIR_RD (1 << 5)

/* CNT Register stat bit */
#define I2C_CNT_STAT_START (1 << 7)
#define I2C_CNT_STAT_BUSY  (1 << 7)

#define I2C_SET_DATA_REG(base,val)	(writeb(val, base + I2C_REG_DATA_OFF))
#define I2C_GET_DATA_REG(base)		(readb(base + I2C_REG_DATA_OFF))
#define I2C_SET_CNT_REG(base,val)	(writeb(val, base + I2C_REG_CNT_OFF))
#define I2C_GET_CNT_REG(base)		(readb(base + I2C_REG_CNT_OFF))
#define I2C_SET_CNTEX_REG(base,val)	(writeb(val, base + I2C_REG_CNTEX_OFF))
#define I2C_GET_CNTEX_REG(base)		(readb(base + I2C_REG_CNTEX_OFF))
#define I2C_SET_SCL_REG(base,val)	(writeb(val, base + I2C_REG_SCL_OFF))
#define I2C_GET_SCL_REG(base)		(readb(base + I2C_REG_SCL_OFF))

#define I2C_BUS_IS_BUSY(base)		(I2C_GET_CNT_REG(base) & I2C_CNT_STAT_BUSY)


struct nintendo3ds_i2c {
	struct i2c_adapter	 adap;
	void __iomem		 *base;
};

static inline void i2c_wait_busy(void __iomem *base)
{
	while (I2C_BUS_IS_BUSY(base))
		;
}

static inline void i2c_select_device(void __iomem *base, u8 addr)
{
	i2c_wait_busy(base);
	I2C_SET_DATA_REG(base, addr);
	I2C_SET_CNT_REG(base, I2C_CNT_STAT_START | I2C_CNT_START);
}

static inline void i2c_select_register(void __iomem *base, u8 reg)
{
	i2c_wait_busy(base);
	I2C_SET_DATA_REG(base, reg);
	I2C_SET_CNT_REG(base, I2C_CNT_STAT_START);
}

static int nintendo3ds_i2c_xfer_msg(struct nintendo3ds_i2c *i2c,
			struct i2c_msg *msg, bool first)
{
	void __iomem *base = i2c->base;
	int i;

	if (msg->len == 1 && first) {
		/* Only select device register */
		i2c_select_device(base, msg->addr & 0xFF);
		i2c_select_register(base, msg->buf[0]);
	} else if (msg->flags & I2C_M_RD) {
		i2c_select_device(base, (msg->addr & 0xFF) | 1);

		for (i = 0; i < msg->len - 1; i++) {
			i2c_wait_busy(base);
			I2C_SET_CNT_REG(base, I2C_CNT_STAT_START
				| I2C_CNT_INTEN | I2C_CNT_DATADIR_RD | I2C_CNT_ACK);
			i2c_wait_busy(base);
			msg->buf[i] = I2C_GET_DATA_REG(base);
		}
		/* Last byte */
		i2c_wait_busy(base);
		I2C_SET_CNT_REG(base, I2C_CNT_STOP | I2C_CNT_STAT_START
			| I2C_CNT_INTEN | I2C_CNT_DATADIR_RD);
		i2c_wait_busy(base);
		msg->buf[i] = I2C_GET_DATA_REG(base);
	} else {
		for (i = 0; i < msg->len - 1; i++) {
			i2c_wait_busy(base);
			I2C_SET_DATA_REG(base, msg->buf[i]);
			i2c_wait_busy(base);
			I2C_SET_CNT_REG(base, I2C_CNT_STAT_START
				| I2C_CNT_INTEN | I2C_CNT_DATADIR_WR);
		}
		/* Last byte */
		i2c_wait_busy(base);
		I2C_SET_DATA_REG(base, msg->buf[i]);
		i2c_wait_busy(base);
		I2C_SET_CNT_REG(base, I2C_CNT_STOP | I2C_CNT_STAT_START
			| I2C_CNT_INTEN | I2C_CNT_DATADIR_WR);
	}

	return 0;
}

static int nintendo3ds_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msgs, int num)
{
	struct nintendo3ds_i2c *i2c = adap->algo_data;
	int i, ret = 0;

	for (i = 0; i < num; i++, msgs++) {
		ret = nintendo3ds_i2c_xfer_msg(i2c, msgs, (i == 0));
		if (ret < 0)
			return ret;
	}

	return i;
}

static u32 nintendo3ds_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm nintendo3ds_i2c_algorithm = {
	.master_xfer	= nintendo3ds_i2c_xfer,
	.functionality	= nintendo3ds_i2c_func,
};

static int nintendo3ds_i2c_probe(struct platform_device *pdev)
{
	struct nintendo3ds_i2c *i2c;
	struct resource *mem;
	int ret;

	i2c = devm_kzalloc(&pdev->dev, sizeof(struct nintendo3ds_i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	platform_set_drvdata(pdev, i2c);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem)
		return -EINVAL;

	i2c->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(i2c->base))
		return PTR_ERR(i2c->base);


	/* Disable any possibly running I2C xfer */
	I2C_SET_CNT_REG(i2c->base, 0);

	/* Setup the i2c_adapter */
	i2c->adap.owner		= THIS_MODULE;
	strlcpy(i2c->adap.name, "Nintendo 3DS I2C adapter",
		sizeof(i2c->adap.name));
	i2c->adap.dev.parent 	= &pdev->dev;
	i2c->adap.dev.of_node	= pdev->dev.of_node;
	i2c->adap.algo		= &nintendo3ds_i2c_algorithm;
	i2c->adap.algo_data	= i2c;

	ret = i2c_add_adapter(&i2c->adap);
	if (ret < 0)
		return ret;

	return 0;
}

static int nintendo3ds_i2c_remove(struct platform_device *pdev)
{
	struct nintendo3ds_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adap);

	return 0;
}

static const struct of_device_id nintendo3ds_i2c_match[] = {
	{ .compatible = "nintendo3ds,nintendo3ds-i2c", },
	{ },
};
MODULE_DEVICE_TABLE(of, nintendo3ds_i2c_match);

static struct platform_driver nintendo3ds_i2c_driver = {
	.probe		= nintendo3ds_i2c_probe,
	.remove		= nintendo3ds_i2c_remove,
	.driver		= {
		.name	= NINTENDO3DS_I2C_NAME,
		.of_match_table = of_match_ptr(nintendo3ds_i2c_match),
	},
};

module_platform_driver(nintendo3ds_i2c_driver);

MODULE_DESCRIPTION("Nintendo 3DS I2C bus driver");
MODULE_AUTHOR("Sergi Granell, <xerpi.g.12@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" NINTENDO3DS_I2C_NAME);
