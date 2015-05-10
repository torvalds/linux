/*
 *  i2c-nintendo3ds.c
 *
 *  Copyright (C) 2015 Sergi Granell.
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

/* I2C Registers */
#define I2C_REG_DATA	0x00
#define I2C_REG_CNT	0x01
#define I2C_REG_CNTEX	0x02
#define I2C_REG_SCL	0x04

/* CNT Register bits */
#define I2C_CNT_STOP	(1 << 0)
#define I2C_CNT_START	(1 << 1)
#define I2C_CNT_PAUSE	(1 << 2)
#define I2C_CNT_ACK	(1 << 4)
#define I2C_CNT_DATADIR	(1 << 5)
#define I2C_CNT_INTEN	(1 << 6)
#define I2C_CNT_STAT	(1 << 7)

#define I2C_BUS_IS_BUSY(base)		(readb(base + I2C_REG_CNT) & I2C_CNT_STAT)
#define I2C_SET_DATA_REG(base,val)	(writeb(val, base + I2C_REG_DATA))
#define I2C_SET_CNT_REG(base,val)	(writeb(val, base + I2C_REG_CNT))
#define I2C_GET_DATA_REG(base)		(readb(base + I2C_REG_DATA))


struct i2c_nintendo3ds {
	struct i2c_adapter	 adap;
	void __iomem		 *base;
};

static inline void i2c_wait_busy(void __iomem *base)
{
	while (I2C_BUS_IS_BUSY(base))
		;
}

static int i2c_nintendo3ds_xfer_msg(struct i2c_nintendo3ds *i2c,
			struct i2c_msg *msg)
{
	void __iomem *base = i2c->base;
	int i;

	i2c_wait_busy(base);

	/* Select the device */
	I2C_SET_DATA_REG(base, msg->addr & 0xFF);
	I2C_SET_CNT_REG(base, I2C_CNT_START|I2C_CNT_STAT);

	if (msg->flags & I2C_M_RD) {
		for (i = 0; i < msg->len - 1; i++) {
			i2c_wait_busy(base);
			I2C_SET_CNT_REG(base, 0xF0);
			i2c_wait_busy(base);
			msg->buf[i] = I2C_GET_DATA_REG(base);
		}
	} else {
		for (i = 0; i < msg->len - 1; i++) {
			i2c_wait_busy(base);
			I2C_SET_DATA_REG(base, msg->buf[i]);
			i2c_wait_busy(base);
			I2C_SET_CNT_REG(base, 0xD0);
		}
	}

	/* Last byte */
	i2c_wait_busy(base);
	I2C_SET_CNT_REG(base, 0xE1);
	i2c_wait_busy(base);

	if (msg->flags & I2C_M_RD) {
		msg->buf[i] = I2C_GET_DATA_REG(base);
	} else {
		I2C_SET_DATA_REG(base, msg->buf[i]);
	}

	return 0;
}

static int i2c_nintendo3ds_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msgs, int num)
{
	struct i2c_nintendo3ds *i2c = adap->algo_data;
	int i, ret = 0;

	for (i = 0; i < num; i++, msgs++) {

		ret = i2c_nintendo3ds_xfer_msg(i2c, msgs);

		if (ret < 0)
			return ret;
	}

	return ret;
}

static u32 i2c_nintendo3ds_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm i2c_nintendo3ds_algorithm = {
	.master_xfer	= i2c_nintendo3ds_xfer,
	.functionality	= i2c_nintendo3ds_func,
};

static int i2c_nintendo3ds_probe(struct platform_device *dev)
{
	struct i2c_nintendo3ds *i2c;
	struct resource *r;
	int ret;

	if (dev->dev.of_node) {
		dev->id = of_alias_get_id(dev->dev.of_node, "i2c");
		if (dev->id < 0) {
			dev_err(&dev->dev, "nintendo3ds-i2c: alias is missing\n");
			return -EINVAL;
		}
	}

	r = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!r) {
		ret = -EINVAL;
		goto err_out;
	}

	if (!request_mem_region(r->start, resource_size(r), "nintendo3ds-i2c")) {
		ret = -EBUSY;
		goto err_out;
	}

	i2c = kzalloc(sizeof(struct i2c_nintendo3ds), GFP_KERNEL);
	if (!i2c) {
		ret = -ENOMEM;
		goto err_release;
	}

	i2c->base = ioremap(r->start, resource_size(r));
	if (!i2c->base) {
		ret = -ENOMEM;
		goto err_free;
	}

	/* Disable any possibly running I2C xfer */
	I2C_SET_CNT_REG(i2c->base, 0);

	/* Setup the i2c_adapter */
	i2c->adap.owner		= THIS_MODULE;
	strlcpy(i2c->adap.name, "Nintendo 3DS I2C adapter", sizeof(i2c->adap.name));
	i2c->adap.dev.parent 	= &dev->dev;
	i2c->adap.dev.of_node	= dev->dev.of_node;
	i2c->adap.algo		= &i2c_nintendo3ds_algorithm;
	i2c->adap.algo_data	= i2c;
	i2c->adap.nr		= dev->id;

	pr_info("Registering Nintendo 3DS I2C adapter %d\n", dev->id);

	ret = i2c_add_numbered_adapter(&i2c->adap);
	if (ret >= 0) {
		platform_set_drvdata(dev, i2c);
		return 0;
	}

	iounmap(i2c->base);
 err_free:
	kfree(i2c);
 err_release:
	release_mem_region(r->start, resource_size(r));
 err_out:
	return ret;
}

static int i2c_nintendo3ds_remove(struct platform_device *dev)
{
	struct i2c_nintendo3ds *i2c = platform_get_drvdata(dev);

	i2c_del_adapter(&i2c->adap);
	return 0;
}

static const struct of_device_id i2c_nintendo3ds_match[] = {
	{ .compatible = "arm,nintendo3ds-i2c", },
	{},
};
MODULE_DEVICE_TABLE(of, i2c_nintendo3ds_match);

static struct platform_driver i2c_nintendo3ds_driver = {
	.probe		= i2c_nintendo3ds_probe,
	.remove		= i2c_nintendo3ds_remove,
	.driver		= {
		.name	= "nintendo3ds-i2c",
		.of_match_table = i2c_nintendo3ds_match,
	},
};

static int __init i2c_nintendo3ds_init(void)
{
	return platform_driver_register(&i2c_nintendo3ds_driver);
}

static void __exit i2c_nintendo3ds_exit(void)
{
	platform_driver_unregister(&i2c_nintendo3ds_driver);
}

subsys_initcall(i2c_nintendo3ds_init);
module_exit(i2c_nintendo3ds_exit);

MODULE_DESCRIPTION("ARM Nintendo 3DS I2C bus driver");
MODULE_AUTHOR("Sergi Granell, <xerpi.g.12@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nintendo3ds-i2c");
