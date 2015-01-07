/*
 * Driver for the Diolan DLN-2 USB-I2C adapter
 *
 * Copyright (c) 2014 Intel Corporation
 *
 * Derived from:
 *  i2c-diolan-u2c.c
 *  Copyright (c) 2010-2011 Ericsson AB
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/mfd/dln2.h>

#define DLN2_I2C_MODULE_ID		0x03
#define DLN2_I2C_CMD(cmd)		DLN2_CMD(cmd, DLN2_I2C_MODULE_ID)

/* I2C commands */
#define DLN2_I2C_GET_PORT_COUNT		DLN2_I2C_CMD(0x00)
#define DLN2_I2C_ENABLE			DLN2_I2C_CMD(0x01)
#define DLN2_I2C_DISABLE		DLN2_I2C_CMD(0x02)
#define DLN2_I2C_IS_ENABLED		DLN2_I2C_CMD(0x03)
#define DLN2_I2C_WRITE			DLN2_I2C_CMD(0x06)
#define DLN2_I2C_READ			DLN2_I2C_CMD(0x07)
#define DLN2_I2C_SCAN_DEVICES		DLN2_I2C_CMD(0x08)
#define DLN2_I2C_PULLUP_ENABLE		DLN2_I2C_CMD(0x09)
#define DLN2_I2C_PULLUP_DISABLE		DLN2_I2C_CMD(0x0A)
#define DLN2_I2C_PULLUP_IS_ENABLED	DLN2_I2C_CMD(0x0B)
#define DLN2_I2C_TRANSFER		DLN2_I2C_CMD(0x0C)
#define DLN2_I2C_SET_MAX_REPLY_COUNT	DLN2_I2C_CMD(0x0D)
#define DLN2_I2C_GET_MAX_REPLY_COUNT	DLN2_I2C_CMD(0x0E)

#define DLN2_I2C_MAX_XFER_SIZE		256
#define DLN2_I2C_BUF_SIZE		(DLN2_I2C_MAX_XFER_SIZE + 16)

struct dln2_i2c {
	struct platform_device *pdev;
	struct i2c_adapter adapter;
	u8 port;
	/*
	 * Buffer to hold the packet for read or write transfers. One is enough
	 * since we can't have multiple transfers in parallel on the i2c bus.
	 */
	void *buf;
};

static int dln2_i2c_enable(struct dln2_i2c *dln2, bool enable)
{
	u16 cmd;
	struct {
		u8 port;
	} tx;

	tx.port = dln2->port;

	if (enable)
		cmd = DLN2_I2C_ENABLE;
	else
		cmd = DLN2_I2C_DISABLE;

	return dln2_transfer_tx(dln2->pdev, cmd, &tx, sizeof(tx));
}

static int dln2_i2c_write(struct dln2_i2c *dln2, u8 addr,
			  u8 *data, u16 data_len)
{
	int ret;
	struct {
		u8 port;
		u8 addr;
		u8 mem_addr_len;
		__le32 mem_addr;
		__le16 buf_len;
		u8 buf[DLN2_I2C_MAX_XFER_SIZE];
	} __packed *tx = dln2->buf;
	unsigned len;

	BUILD_BUG_ON(sizeof(*tx) > DLN2_I2C_BUF_SIZE);

	tx->port = dln2->port;
	tx->addr = addr;
	tx->mem_addr_len = 0;
	tx->mem_addr = 0;
	tx->buf_len = cpu_to_le16(data_len);
	memcpy(tx->buf, data, data_len);

	len = sizeof(*tx) + data_len - DLN2_I2C_MAX_XFER_SIZE;
	ret = dln2_transfer_tx(dln2->pdev, DLN2_I2C_WRITE, tx, len);
	if (ret < 0)
		return ret;

	return data_len;
}

static int dln2_i2c_read(struct dln2_i2c *dln2, u16 addr, u8 *data,
			 u16 data_len)
{
	int ret;
	struct {
		u8 port;
		u8 addr;
		u8 mem_addr_len;
		__le32 mem_addr;
		__le16 buf_len;
	} __packed tx;
	struct {
		__le16 buf_len;
		u8 buf[DLN2_I2C_MAX_XFER_SIZE];
	} __packed *rx = dln2->buf;
	unsigned rx_len = sizeof(*rx);

	BUILD_BUG_ON(sizeof(*rx) > DLN2_I2C_BUF_SIZE);

	tx.port = dln2->port;
	tx.addr = addr;
	tx.mem_addr_len = 0;
	tx.mem_addr = 0;
	tx.buf_len = cpu_to_le16(data_len);

	ret = dln2_transfer(dln2->pdev, DLN2_I2C_READ, &tx, sizeof(tx),
			    rx, &rx_len);
	if (ret < 0)
		return ret;
	if (rx_len < sizeof(rx->buf_len) + data_len)
		return -EPROTO;
	if (le16_to_cpu(rx->buf_len) != data_len)
		return -EPROTO;

	memcpy(data, rx->buf, data_len);

	return data_len;
}

static int dln2_i2c_xfer(struct i2c_adapter *adapter,
			 struct i2c_msg *msgs, int num)
{
	struct dln2_i2c *dln2 = i2c_get_adapdata(adapter);
	struct i2c_msg *pmsg;
	int i;

	for (i = 0; i < num; i++) {
		int ret;

		pmsg = &msgs[i];

		if (pmsg->flags & I2C_M_RD) {
			ret = dln2_i2c_read(dln2, pmsg->addr, pmsg->buf,
					    pmsg->len);
			if (ret < 0)
				return ret;

			pmsg->len = ret;
		} else {
			ret = dln2_i2c_write(dln2, pmsg->addr, pmsg->buf,
					     pmsg->len);
			if (ret != pmsg->len)
				return -EPROTO;
		}
	}

	return num;
}

static u32 dln2_i2c_func(struct i2c_adapter *a)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_BLOCK_PROC_CALL |
		I2C_FUNC_SMBUS_I2C_BLOCK;
}

static const struct i2c_algorithm dln2_i2c_usb_algorithm = {
	.master_xfer = dln2_i2c_xfer,
	.functionality = dln2_i2c_func,
};

static struct i2c_adapter_quirks dln2_i2c_quirks = {
	.max_read_len = DLN2_I2C_MAX_XFER_SIZE,
	.max_write_len = DLN2_I2C_MAX_XFER_SIZE,
};

static int dln2_i2c_probe(struct platform_device *pdev)
{
	int ret;
	struct dln2_i2c *dln2;
	struct device *dev = &pdev->dev;
	struct dln2_platform_data *pdata = dev_get_platdata(&pdev->dev);

	dln2 = devm_kzalloc(dev, sizeof(*dln2), GFP_KERNEL);
	if (!dln2)
		return -ENOMEM;

	dln2->buf = devm_kmalloc(dev, DLN2_I2C_BUF_SIZE, GFP_KERNEL);
	if (!dln2->buf)
		return -ENOMEM;

	dln2->pdev = pdev;
	dln2->port = pdata->port;

	/* setup i2c adapter description */
	dln2->adapter.owner = THIS_MODULE;
	dln2->adapter.class = I2C_CLASS_HWMON;
	dln2->adapter.algo = &dln2_i2c_usb_algorithm;
	dln2->adapter.quirks = &dln2_i2c_quirks;
	dln2->adapter.dev.parent = dev;
	i2c_set_adapdata(&dln2->adapter, dln2);
	snprintf(dln2->adapter.name, sizeof(dln2->adapter.name), "%s-%s-%d",
		 "dln2-i2c", dev_name(pdev->dev.parent), dln2->port);

	platform_set_drvdata(pdev, dln2);

	/* initialize the i2c interface */
	ret = dln2_i2c_enable(dln2, true);
	if (ret < 0) {
		dev_err(dev, "failed to initialize adapter: %d\n", ret);
		return ret;
	}

	/* and finally attach to i2c layer */
	ret = i2c_add_adapter(&dln2->adapter);
	if (ret < 0) {
		dev_err(dev, "failed to add I2C adapter: %d\n", ret);
		goto out_disable;
	}

	return 0;

out_disable:
	dln2_i2c_enable(dln2, false);

	return ret;
}

static int dln2_i2c_remove(struct platform_device *pdev)
{
	struct dln2_i2c *dln2 = platform_get_drvdata(pdev);

	i2c_del_adapter(&dln2->adapter);
	dln2_i2c_enable(dln2, false);

	return 0;
}

static struct platform_driver dln2_i2c_driver = {
	.driver.name	= "dln2-i2c",
	.probe		= dln2_i2c_probe,
	.remove		= dln2_i2c_remove,
};

module_platform_driver(dln2_i2c_driver);

MODULE_AUTHOR("Laurentiu Palcu <laurentiu.palcu@intel.com>");
MODULE_DESCRIPTION("Driver for the Diolan DLN2 I2C master interface");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dln2-i2c");
