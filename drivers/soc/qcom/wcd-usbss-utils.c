// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/soc/qcom/wcd939x-i2c.h>
#include "wcd-usbss-priv.h"

#define REG_BYTES 2
#define VAL_BYTES 1
#define PAGE_REG_ADDR 0x00

static int wcd_usbss_i2c_write_device(struct wcd_usbss_ctxt *ctxt, u16 reg, u8 *value,
				u32 bytes)
{
	struct i2c_msg *msg, xfer_msg[2];
	int ret = 0;
	u8 reg_addr = 0;
	u8 *data = NULL;
	struct i2c_client *client = NULL;

	if (ctxt == NULL || ctxt->client == NULL)
		return -ENODEV;

	client = ctxt->client;
	data = kzalloc(bytes + 1, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	reg_addr = (u8)reg;
	msg = &xfer_msg[0];
	msg->addr = client->addr;
	msg->len = bytes + 1;
	msg->flags = 0;
	data[0] = reg;
	data[1] = *value;
	msg->buf = data;
	ret = i2c_transfer(client->adapter, xfer_msg, 1);
	/* Try again if the write fails */
	if (ret != 1) {
		ret = i2c_transfer(client->adapter, xfer_msg, 1);
		if (ret != 1) {
			pr_err("failed to write the device\n");
			goto fail;
		}
	}
	pr_debug("write success register = %x val = %x\n", reg, data[1]);
	ret = 0;
fail:
	kfree(data);
	return ret;
}


static int wcd_usbss_i2c_read_device(struct wcd_usbss_ctxt *ctxt, unsigned short reg,
				  int bytes, unsigned char *dest)
{
	struct i2c_msg *msg, xfer_msg[2];
	int ret = 0;
	u8 reg_addr = 0;
	u8 i = 0;
	struct i2c_client *client = NULL;

	if (ctxt == NULL || ctxt->client == NULL)
		return -ENODEV;

	client = ctxt->client;
	for (i = 0; i < bytes; i++) {
		reg_addr = (u8)reg++;
		msg = &xfer_msg[0];
		msg->addr = client->addr;
		msg->len = 1;
		msg->flags = 0;
		msg->buf = &reg_addr;

		msg = &xfer_msg[1];
		msg->addr = client->addr;
		msg->len = 1;
		msg->flags = I2C_M_RD;
		msg->buf = dest++;
		ret = i2c_transfer(client->adapter, xfer_msg, 2);

		/* Try again if read fails first time */
		if (ret != 2) {
			ret = i2c_transfer(client->adapter, xfer_msg, 2);
			if (ret != 2) {
				pr_err("failed to read wcd usbss register\n");
				return ret;
			}
		}
	}
	return 0;
}

/*
 * wcd_usbss_page_write:
 *	Retrieve page number from register and
 *	write that page number to the page address.
 *
 * @ctxt: pointer to wcd_usbss_ctxt
 * @reg: Register address from which page number is retrieved
 *
 * Returns 0 for success and negative error code for failure.
 */
static int wcd_usbss_page_write(struct wcd_usbss_ctxt *ctxt, unsigned short *reg)
{
	int ret = 0;
	unsigned short c_reg = 0, reg_addr = 0;
	u8 pg_num = 0, prev_pg_num = 0;

	c_reg = *reg;
	pg_num = c_reg >> 8;
	reg_addr = c_reg & 0xff;
	if (ctxt->prev_pg_valid) {
		prev_pg_num = ctxt->prev_pg;
		if (prev_pg_num != pg_num) {
			ret = wcd_usbss_i2c_write_device(
					ctxt, PAGE_REG_ADDR, (void *) &pg_num, 1);
			if (ret < 0) {
				dev_err(ctxt->dev, "page write error, pg_num: 0x%x\n", pg_num);
			} else {
				ctxt->prev_pg = pg_num;
				dev_dbg(ctxt->dev, "Page 0x%x Write to 0x00\n", pg_num);
			}
		}
	} else {
		ret = wcd_usbss_i2c_write_device(
				ctxt, PAGE_REG_ADDR, (void *) &pg_num, 1);
		if (ret < 0) {
			pr_err("page write error, pg_num: 0x%x\n", pg_num);
		} else {
			ctxt->prev_pg = pg_num;
			ctxt->prev_pg_valid = true;
			dev_dbg(ctxt->dev, "Page 0x%x Write to 0x00\n", pg_num);
		}
	}
	*reg = reg_addr;
	return ret;
}

static int regmap_bus_read(void *context, const void *reg, size_t reg_size,
			   void *val, size_t val_size)
{
	struct device *dev = context;
	struct wcd_usbss_ctxt *ctxt = dev_get_drvdata(dev);
	unsigned short c_reg = 0, rreg = 0;
	int ret = 0, i;

	if (!ctxt) {
		dev_err(dev, "%s: ctxt is NULL\n", __func__);
		return -EINVAL;
	}
	if (!reg || !val) {
		dev_err(dev, "%s: reg or val is NULL\n", __func__);
		return -EINVAL;
	}

	if (reg_size != REG_BYTES) {
		dev_err(dev, "%s: register size %zd bytes, not supported\n",
			__func__, reg_size);
		return -EINVAL;
	}

	mutex_lock(&ctxt->io_lock);
	c_reg = *(u16 *)reg;
	rreg = c_reg;

	ret = wcd_usbss_page_write(ctxt, &c_reg);
	if (ret)
		goto err;
	ret = wcd_usbss_i2c_read_device(ctxt, c_reg, val_size, val);
	if (ret < 0)
		dev_err(dev, "%s: Codec read failed (%d), reg: 0x%x, size:%zd\n",
			__func__, ret, rreg, val_size);
	else {
		for (i = 0; i < val_size; i++)
			dev_dbg(dev, "%s: Read 0x%02x from 0x%x\n",
				__func__, ((u8 *)val)[i], rreg + i);
	}
err:
	mutex_unlock(&ctxt->io_lock);

	return ret;
}

static int regmap_bus_gather_write(void *context,
				   const void *reg, size_t reg_size,
				   const void *val, size_t val_size)
{
	struct device *dev = context;
	struct wcd_usbss_ctxt *ctxt = dev_get_drvdata(dev);
	unsigned short c_reg, rreg;
	int ret, i;

	if (!ctxt) {
		dev_err(dev, "%s: ctxt is NULL\n", __func__);
		return -EINVAL;
	}
	if (!reg || !val) {
		dev_err(dev, "%s: reg or val is NULL\n", __func__);
		return -EINVAL;
	}
	if (reg_size != REG_BYTES) {
		dev_err(dev, "%s: register size %zd bytes, not supported\n",
			__func__, reg_size);
		return -EINVAL;
	}
	mutex_lock(&ctxt->io_lock);
	c_reg = *(u16 *)reg;
	rreg = c_reg;

	ret = wcd_usbss_page_write(ctxt, &c_reg);
	if (ret)
		goto err;

	for (i = 0; i < val_size; i++)
		dev_dbg(dev, "Write %02x to 0x%x\n", ((u8 *)val)[i],
			rreg + i);

	ret = wcd_usbss_i2c_write_device(ctxt, c_reg, (void *) val, val_size);
	if (ret < 0)
		dev_err(dev, "%s: Codec write failed (%d), reg:0x%x, size:%zd\n",
			__func__, ret, rreg, val_size);

err:
	mutex_unlock(&ctxt->io_lock);
	return ret;
}

static int regmap_bus_write(void *context, const void *data, size_t count)
{
	struct device *dev = context;
	struct wcd_usbss_ctxt *ctxt = dev_get_drvdata(dev);

	if (!ctxt)
		return -EINVAL;

	WARN_ON(count < REG_BYTES);
	WARN_ON(count > (REG_BYTES + VAL_BYTES));

	if (count <= (REG_BYTES + VAL_BYTES))
		return regmap_bus_gather_write(context, data, REG_BYTES,
						       data + REG_BYTES,
						       count - REG_BYTES);

	return -EINVAL;
}

static struct regmap_bus regmap_bus_config = {
	.write = regmap_bus_write,
	.gather_write = regmap_bus_gather_write,
	.read = regmap_bus_read,
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
};

/*
 * wcd_usbss_regmap_init:
 *	Initialize wcd_usbss register map
 *
 * @dev: pointer to wcd usbss i2c device
 * @config: pointer to register map config
 *
 * Returns pointer to regmap structure for success
 * or NULL in case of failure.
 */
struct regmap *wcd_usbss_regmap_init(struct device *dev,
				   const struct regmap_config *config)
{
	return devm_regmap_init(dev, &regmap_bus_config, dev, config);
}
EXPORT_SYMBOL(wcd_usbss_regmap_init);
