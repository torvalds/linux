/*
 * Elan I2C/SMBus Touchpad driver - I2C interface
 *
 * Copyright (c) 2013 ELAN Microelectronics Corp.
 *
 * Author: 林政維 (Duson Lin) <dusonlin@emc.com.tw>
 *
 * Based on cyapa driver:
 * copyright (c) 2011-2012 Cypress Semiconductor, Inc.
 * copyright (c) 2011-2012 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Trademarks are the property of their respective owners.
 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/unaligned.h>

#include "elan_i2c.h"

/* Elan i2c commands */
#define ETP_I2C_RESET			0x0100
#define ETP_I2C_WAKE_UP			0x0800
#define ETP_I2C_SLEEP			0x0801
#define ETP_I2C_DESC_CMD		0x0001
#define ETP_I2C_REPORT_DESC_CMD		0x0002
#define ETP_I2C_STAND_CMD		0x0005
#define ETP_I2C_UNIQUEID_CMD		0x0101
#define ETP_I2C_FW_VERSION_CMD		0x0102
#define ETP_I2C_SM_VERSION_CMD		0x0103
#define ETP_I2C_XY_TRACENUM_CMD		0x0105
#define ETP_I2C_MAX_X_AXIS_CMD		0x0106
#define ETP_I2C_MAX_Y_AXIS_CMD		0x0107
#define ETP_I2C_RESOLUTION_CMD		0x0108
#define ETP_I2C_IAP_VERSION_CMD		0x0110
#define ETP_I2C_SET_CMD			0x0300
#define ETP_I2C_POWER_CMD		0x0307
#define ETP_I2C_FW_CHECKSUM_CMD		0x030F
#define ETP_I2C_IAP_CTRL_CMD		0x0310
#define ETP_I2C_IAP_CMD			0x0311
#define ETP_I2C_IAP_RESET_CMD		0x0314
#define ETP_I2C_IAP_CHECKSUM_CMD	0x0315
#define ETP_I2C_CALIBRATE_CMD		0x0316
#define ETP_I2C_MAX_BASELINE_CMD	0x0317
#define ETP_I2C_MIN_BASELINE_CMD	0x0318

#define ETP_I2C_REPORT_LEN		34
#define ETP_I2C_DESC_LENGTH		30
#define ETP_I2C_REPORT_DESC_LENGTH	158
#define ETP_I2C_INF_LENGTH		2
#define ETP_I2C_IAP_PASSWORD		0x1EA5
#define ETP_I2C_IAP_RESET		0xF0F0
#define ETP_I2C_MAIN_MODE_ON		(1 << 9)
#define ETP_I2C_IAP_REG_L		0x01
#define ETP_I2C_IAP_REG_H		0x06

static int elan_i2c_read_block(struct i2c_client *client,
			       u16 reg, u8 *val, u16 len)
{
	__le16 buf[] = {
		cpu_to_le16(reg),
	};
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = client->flags & I2C_M_TEN,
			.len = sizeof(buf),
			.buf = (u8 *)buf,
		},
		{
			.addr = client->addr,
			.flags = (client->flags & I2C_M_TEN) | I2C_M_RD,
			.len = len,
			.buf = val,
		}
	};
	int ret;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	return ret == ARRAY_SIZE(msgs) ? 0 : (ret < 0 ? ret : -EIO);
}

static int elan_i2c_read_cmd(struct i2c_client *client, u16 reg, u8 *val)
{
	int retval;

	retval = elan_i2c_read_block(client, reg, val, ETP_I2C_INF_LENGTH);
	if (retval < 0) {
		dev_err(&client->dev, "reading cmd (0x%04x) fail.\n", reg);
		return retval;
	}

	return 0;
}

static int elan_i2c_write_cmd(struct i2c_client *client, u16 reg, u16 cmd)
{
	__le16 buf[] = {
		cpu_to_le16(reg),
		cpu_to_le16(cmd),
	};
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = client->flags & I2C_M_TEN,
		.len = sizeof(buf),
		.buf = (u8 *)buf,
	};
	int ret;

	ret = i2c_transfer(client->adapter, &msg, 1);
	return ret == 1 ? 0 : (ret < 0 ? ret : -EIO);
}

static int elan_i2c_initialize(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	int error;
	u8 val[256];

	error = elan_i2c_write_cmd(client, ETP_I2C_STAND_CMD, ETP_I2C_RESET);
	if (error) {
		dev_err(dev, "device reset failed: %d\n", error);
		return error;
	}

	/* Wait for the device to reset */
	msleep(100);

	/* get reset acknowledgement 0000 */
	error = i2c_master_recv(client, val, ETP_I2C_INF_LENGTH);
	if (error < 0) {
		dev_err(dev, "failed to read reset response: %d\n", error);
		return error;
	}

	error = elan_i2c_read_block(client, ETP_I2C_DESC_CMD,
				    val, ETP_I2C_DESC_LENGTH);
	if (error) {
		dev_err(dev, "cannot get device descriptor: %d\n", error);
		return error;
	}

	error = elan_i2c_read_block(client, ETP_I2C_REPORT_DESC_CMD,
				    val, ETP_I2C_REPORT_DESC_LENGTH);
	if (error) {
		dev_err(dev, "fetching report descriptor failed.: %d\n", error);
		return error;
	}

	return 0;
}

static int elan_i2c_sleep_control(struct i2c_client *client, bool sleep)
{
	return elan_i2c_write_cmd(client, ETP_I2C_STAND_CMD,
				  sleep ? ETP_I2C_SLEEP : ETP_I2C_WAKE_UP);
}

static int elan_i2c_power_control(struct i2c_client *client, bool enable)
{
	u8 val[2];
	u16 reg;
	int error;

	error = elan_i2c_read_cmd(client, ETP_I2C_POWER_CMD, val);
	if (error) {
		dev_err(&client->dev,
			"failed to read current power state: %d\n",
			error);
		return error;
	}

	reg = le16_to_cpup((__le16 *)val);
	if (enable)
		reg &= ~ETP_DISABLE_POWER;
	else
		reg |= ETP_DISABLE_POWER;

	error = elan_i2c_write_cmd(client, ETP_I2C_POWER_CMD, reg);
	if (error) {
		dev_err(&client->dev,
			"failed to write current power state: %d\n",
			error);
		return error;
	}

	return 0;
}

static int elan_i2c_set_mode(struct i2c_client *client, u8 mode)
{
	return elan_i2c_write_cmd(client, ETP_I2C_SET_CMD, mode);
}


static int elan_i2c_calibrate(struct i2c_client *client)
{
	return elan_i2c_write_cmd(client, ETP_I2C_CALIBRATE_CMD, 1);
}

static int elan_i2c_calibrate_result(struct i2c_client *client, u8 *val)
{
	return elan_i2c_read_block(client, ETP_I2C_CALIBRATE_CMD, val, 1);
}

static int elan_i2c_get_baseline_data(struct i2c_client *client,
				      bool max_baseline, u8 *value)
{
	int error;
	u8 val[3];

	error = elan_i2c_read_cmd(client,
				  max_baseline ? ETP_I2C_MAX_BASELINE_CMD :
						 ETP_I2C_MIN_BASELINE_CMD,
				  val);
	if (error)
		return error;

	*value = le16_to_cpup((__le16 *)val);

	return 0;
}

static int elan_i2c_get_version(struct i2c_client *client,
				bool iap, u8 *version)
{
	int error;
	u8 val[3];

	error = elan_i2c_read_cmd(client,
				  iap ? ETP_I2C_IAP_VERSION_CMD :
					ETP_I2C_FW_VERSION_CMD,
				  val);
	if (error) {
		dev_err(&client->dev, "failed to get %s version: %d\n",
			iap ? "IAP" : "FW", error);
		return error;
	}

	*version = val[0];
	return 0;
}

static int elan_i2c_get_sm_version(struct i2c_client *client, u8 *version)
{
	int error;
	u8 val[3];

	error = elan_i2c_read_cmd(client, ETP_I2C_SM_VERSION_CMD, val);
	if (error) {
		dev_err(&client->dev, "failed to get SM version: %d\n", error);
		return error;
	}

	*version = val[0];
	return 0;
}

static int elan_i2c_get_product_id(struct i2c_client *client, u8 *id)
{
	int error;
	u8 val[3];

	error = elan_i2c_read_cmd(client, ETP_I2C_UNIQUEID_CMD, val);
	if (error) {
		dev_err(&client->dev, "failed to get product ID: %d\n", error);
		return error;
	}

	*id = val[0];
	return 0;
}

static int elan_i2c_get_checksum(struct i2c_client *client,
				 bool iap, u16 *csum)
{
	int error;
	u8 val[3];

	error = elan_i2c_read_cmd(client,
				  iap ? ETP_I2C_IAP_CHECKSUM_CMD :
					ETP_I2C_FW_CHECKSUM_CMD,
				  val);
	if (error) {
		dev_err(&client->dev, "failed to get %s checksum: %d\n",
			iap ? "IAP" : "FW", error);
		return error;
	}

	*csum = le16_to_cpup((__le16 *)val);
	return 0;
}

static int elan_i2c_get_max(struct i2c_client *client,
			    unsigned int *max_x, unsigned int *max_y)
{
	int error;
	u8 val[3];

	error = elan_i2c_read_cmd(client, ETP_I2C_MAX_X_AXIS_CMD, val);
	if (error) {
		dev_err(&client->dev, "failed to get X dimension: %d\n", error);
		return error;
	}

	*max_x = le16_to_cpup((__le16 *)val) & 0x0fff;

	error = elan_i2c_read_cmd(client, ETP_I2C_MAX_Y_AXIS_CMD, val);
	if (error) {
		dev_err(&client->dev, "failed to get Y dimension: %d\n", error);
		return error;
	}

	*max_y = le16_to_cpup((__le16 *)val) & 0x0fff;

	return 0;
}

static int elan_i2c_get_resolution(struct i2c_client *client,
				   u8 *hw_res_x, u8 *hw_res_y)
{
	int error;
	u8 val[3];

	error = elan_i2c_read_cmd(client, ETP_I2C_RESOLUTION_CMD, val);
	if (error) {
		dev_err(&client->dev, "failed to get resolution: %d\n", error);
		return error;
	}

	*hw_res_x = val[0];
	*hw_res_y = val[1];

	return 0;
}

static int elan_i2c_get_num_traces(struct i2c_client *client,
				   unsigned int *x_traces,
				   unsigned int *y_traces)
{
	int error;
	u8 val[3];

	error = elan_i2c_read_cmd(client, ETP_I2C_XY_TRACENUM_CMD, val);
	if (error) {
		dev_err(&client->dev, "failed to get trace info: %d\n", error);
		return error;
	}

	*x_traces = val[0] - 1;
	*y_traces = val[1] - 1;

	return 0;
}

static int elan_i2c_iap_get_mode(struct i2c_client *client, enum tp_mode *mode)
{
	int error;
	u16 constant;
	u8 val[3];

	error = elan_i2c_read_cmd(client, ETP_I2C_IAP_CTRL_CMD, val);
	if (error) {
		dev_err(&client->dev,
			"failed to read iap control register: %d\n",
			error);
		return error;
	}

	constant = le16_to_cpup((__le16 *)val);
	dev_dbg(&client->dev, "iap control reg: 0x%04x.\n", constant);

	*mode = (constant & ETP_I2C_MAIN_MODE_ON) ? MAIN_MODE : IAP_MODE;

	return 0;
}

static int elan_i2c_iap_reset(struct i2c_client *client)
{
	int error;

	error = elan_i2c_write_cmd(client, ETP_I2C_IAP_RESET_CMD,
				   ETP_I2C_IAP_RESET);
	if (error) {
		dev_err(&client->dev, "cannot reset IC: %d\n", error);
		return error;
	}

	return 0;
}

static int elan_i2c_set_flash_key(struct i2c_client *client)
{
	int error;

	error = elan_i2c_write_cmd(client, ETP_I2C_IAP_CMD,
				   ETP_I2C_IAP_PASSWORD);
	if (error) {
		dev_err(&client->dev, "cannot set flash key: %d\n", error);
		return error;
	}

	return 0;
}

static int elan_i2c_prepare_fw_update(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	int error;
	enum tp_mode mode;
	u8 val[3];
	u16 password;

	/* Get FW in which mode	(IAP_MODE/MAIN_MODE)  */
	error = elan_i2c_iap_get_mode(client, &mode);
	if (error)
		return error;

	if (mode == IAP_MODE) {
		/* Reset IC */
		error = elan_i2c_iap_reset(client);
		if (error)
			return error;

		msleep(30);
	}

	/* Set flash key*/
	error = elan_i2c_set_flash_key(client);
	if (error)
		return error;

	/* Wait for F/W IAP initialization */
	msleep(mode == MAIN_MODE ? 100 : 30);

	/* Check if we are in IAP mode or not */
	error = elan_i2c_iap_get_mode(client, &mode);
	if (error)
		return error;

	if (mode == MAIN_MODE) {
		dev_err(dev, "wrong mode: %d\n", mode);
		return -EIO;
	}

	/* Set flash key again */
	error = elan_i2c_set_flash_key(client);
	if (error)
		return error;

	/* Wait for F/W IAP initialization */
	msleep(30);

	/* read back to check we actually enabled successfully. */
	error = elan_i2c_read_cmd(client, ETP_I2C_IAP_CMD, val);
	if (error) {
		dev_err(dev, "cannot read iap password: %d\n",
			error);
		return error;
	}

	password = le16_to_cpup((__le16 *)val);
	if (password != ETP_I2C_IAP_PASSWORD) {
		dev_err(dev, "wrong iap password: 0x%X\n", password);
		return -EIO;
	}

	return 0;
}

static int elan_i2c_write_fw_block(struct i2c_client *client,
				   const u8 *page, u16 checksum, int idx)
{
	struct device *dev = &client->dev;
	u8 page_store[ETP_FW_PAGE_SIZE + 4];
	u8 val[3];
	u16 result;
	int ret, error;

	page_store[0] = ETP_I2C_IAP_REG_L;
	page_store[1] = ETP_I2C_IAP_REG_H;
	memcpy(&page_store[2], page, ETP_FW_PAGE_SIZE);
	/* recode checksum at last two bytes */
	put_unaligned_le16(checksum, &page_store[ETP_FW_PAGE_SIZE + 2]);

	ret = i2c_master_send(client, page_store, sizeof(page_store));
	if (ret != sizeof(page_store)) {
		error = ret < 0 ? ret : -EIO;
		dev_err(dev, "Failed to write page %d: %d\n", idx, error);
		return error;
	}

	/* Wait for F/W to update one page ROM data. */
	msleep(20);

	error = elan_i2c_read_cmd(client, ETP_I2C_IAP_CTRL_CMD, val);
	if (error) {
		dev_err(dev, "Failed to read IAP write result: %d\n", error);
		return error;
	}

	result = le16_to_cpup((__le16 *)val);
	if (result & (ETP_FW_IAP_PAGE_ERR | ETP_FW_IAP_INTF_ERR)) {
		dev_err(dev, "IAP reports failed write: %04hx\n",
			result);
		return -EIO;
	}

	return 0;
}

static int elan_i2c_finish_fw_update(struct i2c_client *client,
				     struct completion *completion)
{
	struct device *dev = &client->dev;
	long ret;
	int error;
	int len;
	u8 buffer[ETP_I2C_INF_LENGTH];

	reinit_completion(completion);
	enable_irq(client->irq);

	error = elan_i2c_write_cmd(client, ETP_I2C_STAND_CMD, ETP_I2C_RESET);
	if (!error)
		ret = wait_for_completion_interruptible_timeout(completion,
							msecs_to_jiffies(300));
	disable_irq(client->irq);

	if (error) {
		dev_err(dev, "device reset failed: %d\n", error);
		return error;
	} else if (ret == 0) {
		dev_err(dev, "timeout waiting for device reset\n");
		return -ETIMEDOUT;
	} else if (ret < 0) {
		error = ret;
		dev_err(dev, "error waiting for device reset: %d\n", error);
		return error;
	}

	len = i2c_master_recv(client, buffer, ETP_I2C_INF_LENGTH);
	if (len != ETP_I2C_INF_LENGTH) {
		error = len < 0 ? len : -EIO;
		dev_err(dev, "failed to read INT signal: %d (%d)\n",
			error, len);
		return error;
	}

	return 0;
}

static int elan_i2c_get_report(struct i2c_client *client, u8 *report)
{
	int len;

	len = i2c_master_recv(client, report, ETP_I2C_REPORT_LEN);
	if (len < 0) {
		dev_err(&client->dev, "failed to read report data: %d\n", len);
		return len;
	}

	if (len != ETP_I2C_REPORT_LEN) {
		dev_err(&client->dev,
			"wrong report length (%d vs %d expected)\n",
			len, ETP_I2C_REPORT_LEN);
		return -EIO;
	}

	return 0;
}

const struct elan_transport_ops elan_i2c_ops = {
	.initialize		= elan_i2c_initialize,
	.sleep_control		= elan_i2c_sleep_control,
	.power_control		= elan_i2c_power_control,
	.set_mode		= elan_i2c_set_mode,

	.calibrate		= elan_i2c_calibrate,
	.calibrate_result	= elan_i2c_calibrate_result,

	.get_baseline_data	= elan_i2c_get_baseline_data,

	.get_version		= elan_i2c_get_version,
	.get_sm_version		= elan_i2c_get_sm_version,
	.get_product_id		= elan_i2c_get_product_id,
	.get_checksum		= elan_i2c_get_checksum,

	.get_max		= elan_i2c_get_max,
	.get_resolution		= elan_i2c_get_resolution,
	.get_num_traces		= elan_i2c_get_num_traces,

	.iap_get_mode		= elan_i2c_iap_get_mode,
	.iap_reset		= elan_i2c_iap_reset,

	.prepare_fw_update	= elan_i2c_prepare_fw_update,
	.write_fw_block		= elan_i2c_write_fw_block,
	.finish_fw_update	= elan_i2c_finish_fw_update,

	.get_report		= elan_i2c_get_report,
};
