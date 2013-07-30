/*
 * CBUS I2C driver for Nokia Internet Tablets.
 *
 * Copyright (C) 2004-2010 Nokia Corporation
 *
 * Based on code written by Juha Yrjölä, David Weinehall, Mikko Ylinen and
 * Felipe Balbi. Converted to I2C driver by Aaro Koskinen.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/platform_data/i2c-cbus-gpio.h>

/*
 * Bit counts are derived from Nokia implementation. These should be checked
 * if other CBUS implementations appear.
 */
#define CBUS_ADDR_BITS	3
#define CBUS_REG_BITS	5

struct cbus_host {
	spinlock_t	lock;		/* host lock */
	struct device	*dev;
	int		clk_gpio;
	int		dat_gpio;
	int		sel_gpio;
};

/**
 * cbus_send_bit - sends one bit over the bus
 * @host: the host we're using
 * @bit: one bit of information to send
 */
static void cbus_send_bit(struct cbus_host *host, unsigned bit)
{
	gpio_set_value(host->dat_gpio, bit ? 1 : 0);
	gpio_set_value(host->clk_gpio, 1);
	gpio_set_value(host->clk_gpio, 0);
}

/**
 * cbus_send_data - sends @len amount of data over the bus
 * @host: the host we're using
 * @data: the data to send
 * @len: size of the transfer
 */
static void cbus_send_data(struct cbus_host *host, unsigned data, unsigned len)
{
	int i;

	for (i = len; i > 0; i--)
		cbus_send_bit(host, data & (1 << (i - 1)));
}

/**
 * cbus_receive_bit - receives one bit from the bus
 * @host: the host we're using
 */
static int cbus_receive_bit(struct cbus_host *host)
{
	int ret;

	gpio_set_value(host->clk_gpio, 1);
	ret = gpio_get_value(host->dat_gpio);
	gpio_set_value(host->clk_gpio, 0);
	return ret;
}

/**
 * cbus_receive_word - receives 16-bit word from the bus
 * @host: the host we're using
 */
static int cbus_receive_word(struct cbus_host *host)
{
	int ret = 0;
	int i;

	for (i = 16; i > 0; i--) {
		int bit = cbus_receive_bit(host);

		if (bit < 0)
			return bit;

		if (bit)
			ret |= 1 << (i - 1);
	}
	return ret;
}

/**
 * cbus_transfer - transfers data over the bus
 * @host: the host we're using
 * @rw: read/write flag
 * @dev: device address
 * @reg: register address
 * @data: if @rw == I2C_SBUS_WRITE data to send otherwise 0
 */
static int cbus_transfer(struct cbus_host *host, char rw, unsigned dev,
			 unsigned reg, unsigned data)
{
	unsigned long flags;
	int ret;

	/* We don't want interrupts disturbing our transfer */
	spin_lock_irqsave(&host->lock, flags);

	/* Reset state and start of transfer, SEL stays down during transfer */
	gpio_set_value(host->sel_gpio, 0);

	/* Set the DAT pin to output */
	gpio_direction_output(host->dat_gpio, 1);

	/* Send the device address */
	cbus_send_data(host, dev, CBUS_ADDR_BITS);

	/* Send the rw flag */
	cbus_send_bit(host, rw == I2C_SMBUS_READ);

	/* Send the register address */
	cbus_send_data(host, reg, CBUS_REG_BITS);

	if (rw == I2C_SMBUS_WRITE) {
		cbus_send_data(host, data, 16);
		ret = 0;
	} else {
		ret = gpio_direction_input(host->dat_gpio);
		if (ret) {
			dev_dbg(host->dev, "failed setting direction\n");
			goto out;
		}
		gpio_set_value(host->clk_gpio, 1);

		ret = cbus_receive_word(host);
		if (ret < 0) {
			dev_dbg(host->dev, "failed receiving data\n");
			goto out;
		}
	}

	/* Indicate end of transfer, SEL goes up until next transfer */
	gpio_set_value(host->sel_gpio, 1);
	gpio_set_value(host->clk_gpio, 1);
	gpio_set_value(host->clk_gpio, 0);

out:
	spin_unlock_irqrestore(&host->lock, flags);

	return ret;
}

static int cbus_i2c_smbus_xfer(struct i2c_adapter	*adapter,
			       u16			addr,
			       unsigned short		flags,
			       char			read_write,
			       u8			command,
			       int			size,
			       union i2c_smbus_data	*data)
{
	struct cbus_host *chost = i2c_get_adapdata(adapter);
	int ret;

	if (size != I2C_SMBUS_WORD_DATA)
		return -EINVAL;

	ret = cbus_transfer(chost, read_write == I2C_SMBUS_READ, addr,
			    command, data->word);
	if (ret < 0)
		return ret;

	if (read_write == I2C_SMBUS_READ)
		data->word = ret;

	return 0;
}

static u32 cbus_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_READ_WORD_DATA | I2C_FUNC_SMBUS_WRITE_WORD_DATA;
}

static const struct i2c_algorithm cbus_i2c_algo = {
	.smbus_xfer	= cbus_i2c_smbus_xfer,
	.functionality	= cbus_i2c_func,
};

static int cbus_i2c_remove(struct platform_device *pdev)
{
	struct i2c_adapter *adapter = platform_get_drvdata(pdev);

	i2c_del_adapter(adapter);

	return 0;
}

static int cbus_i2c_probe(struct platform_device *pdev)
{
	struct i2c_adapter *adapter;
	struct cbus_host *chost;
	int ret;

	adapter = devm_kzalloc(&pdev->dev, sizeof(struct i2c_adapter),
			       GFP_KERNEL);
	if (!adapter)
		return -ENOMEM;

	chost = devm_kzalloc(&pdev->dev, sizeof(*chost), GFP_KERNEL);
	if (!chost)
		return -ENOMEM;

	if (pdev->dev.of_node) {
		struct device_node *dnode = pdev->dev.of_node;
		if (of_gpio_count(dnode) != 3)
			return -ENODEV;
		chost->clk_gpio = of_get_gpio(dnode, 0);
		chost->dat_gpio = of_get_gpio(dnode, 1);
		chost->sel_gpio = of_get_gpio(dnode, 2);
	} else if (dev_get_platdata(&pdev->dev)) {
		struct i2c_cbus_platform_data *pdata =
			dev_get_platdata(&pdev->dev);
		chost->clk_gpio = pdata->clk_gpio;
		chost->dat_gpio = pdata->dat_gpio;
		chost->sel_gpio = pdata->sel_gpio;
	} else {
		return -ENODEV;
	}

	adapter->owner		= THIS_MODULE;
	adapter->class		= I2C_CLASS_HWMON;
	adapter->dev.parent	= &pdev->dev;
	adapter->nr		= pdev->id;
	adapter->timeout	= HZ;
	adapter->algo		= &cbus_i2c_algo;
	strlcpy(adapter->name, "CBUS I2C adapter", sizeof(adapter->name));

	spin_lock_init(&chost->lock);
	chost->dev = &pdev->dev;

	ret = devm_gpio_request_one(&pdev->dev, chost->clk_gpio,
				    GPIOF_OUT_INIT_LOW, "CBUS clk");
	if (ret)
		return ret;

	ret = devm_gpio_request_one(&pdev->dev, chost->dat_gpio, GPIOF_IN,
				    "CBUS data");
	if (ret)
		return ret;

	ret = devm_gpio_request_one(&pdev->dev, chost->sel_gpio,
				    GPIOF_OUT_INIT_HIGH, "CBUS sel");
	if (ret)
		return ret;

	i2c_set_adapdata(adapter, chost);
	platform_set_drvdata(pdev, adapter);

	return i2c_add_numbered_adapter(adapter);
}

#if defined(CONFIG_OF)
static const struct of_device_id i2c_cbus_dt_ids[] = {
	{ .compatible = "i2c-cbus-gpio", },
	{ }
};
MODULE_DEVICE_TABLE(of, i2c_cbus_dt_ids);
#endif

static struct platform_driver cbus_i2c_driver = {
	.probe	= cbus_i2c_probe,
	.remove	= cbus_i2c_remove,
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "i2c-cbus-gpio",
	},
};
module_platform_driver(cbus_i2c_driver);

MODULE_ALIAS("platform:i2c-cbus-gpio");
MODULE_DESCRIPTION("CBUS I2C driver");
MODULE_AUTHOR("Juha Yrjölä");
MODULE_AUTHOR("David Weinehall");
MODULE_AUTHOR("Mikko Ylinen");
MODULE_AUTHOR("Felipe Balbi");
MODULE_AUTHOR("Aaro Koskinen <aaro.koskinen@iki.fi>");
MODULE_LICENSE("GPL");
