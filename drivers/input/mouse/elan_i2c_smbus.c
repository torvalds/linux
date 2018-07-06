/*
 * Elan I2C/SMBus Touchpad driver - SMBus interface
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

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include "elan_i2c.h"

/* Elan SMbus commands */
#define ETP_SMBUS_IAP_CMD		0x00
#define ETP_SMBUS_ENABLE_TP		0x20
#define ETP_SMBUS_SLEEP_CMD		0x21
#define ETP_SMBUS_IAP_PASSWORD_WRITE	0x29
#define ETP_SMBUS_IAP_PASSWORD_READ	0x80
#define ETP_SMBUS_WRITE_FW_BLOCK	0x2A
#define ETP_SMBUS_IAP_RESET_CMD		0x2B
#define ETP_SMBUS_RANGE_CMD		0xA0
#define ETP_SMBUS_FW_VERSION_CMD	0xA1
#define ETP_SMBUS_XY_TRACENUM_CMD	0xA2
#define ETP_SMBUS_SM_VERSION_CMD	0xA3
#define ETP_SMBUS_UNIQUEID_CMD		0xA3
#define ETP_SMBUS_RESOLUTION_CMD	0xA4
#define ETP_SMBUS_HELLOPACKET_CMD	0xA7
#define ETP_SMBUS_PACKET_QUERY		0xA8
#define ETP_SMBUS_IAP_VERSION_CMD	0xAC
#define ETP_SMBUS_IAP_CTRL_CMD		0xAD
#define ETP_SMBUS_IAP_CHECKSUM_CMD	0xAE
#define ETP_SMBUS_FW_CHECKSUM_CMD	0xAF
#define ETP_SMBUS_MAX_BASELINE_CMD	0xC3
#define ETP_SMBUS_MIN_BASELINE_CMD	0xC4
#define ETP_SMBUS_CALIBRATE_QUERY	0xC5

#define ETP_SMBUS_REPORT_LEN		32
#define ETP_SMBUS_REPORT_OFFSET		2
#define ETP_SMBUS_HELLOPACKET_LEN	5
#define ETP_SMBUS_IAP_PASSWORD		0x1234
#define ETP_SMBUS_IAP_MODE_ON		(1 << 6)

static int elan_smbus_initialize(struct i2c_client *client)
{
	u8 check[ETP_SMBUS_HELLOPACKET_LEN] = { 0x55, 0x55, 0x55, 0x55, 0x55 };
	u8 values[ETP_SMBUS_HELLOPACKET_LEN] = { 0, 0, 0, 0, 0 };
	int len, error;

	/* Get hello packet */
	len = i2c_smbus_read_block_data(client,
					ETP_SMBUS_HELLOPACKET_CMD, values);
	if (len != ETP_SMBUS_HELLOPACKET_LEN) {
		dev_err(&client->dev, "hello packet length fail: %d\n", len);
		error = len < 0 ? len : -EIO;
		return error;
	}

	/* compare hello packet */
	if (memcmp(values, check, ETP_SMBUS_HELLOPACKET_LEN)) {
		dev_err(&client->dev, "hello packet fail [%*ph]\n",
			ETP_SMBUS_HELLOPACKET_LEN, values);
		return -ENXIO;
	}

	/* enable tp */
	error = i2c_smbus_write_byte(client, ETP_SMBUS_ENABLE_TP);
	if (error) {
		dev_err(&client->dev, "failed to enable touchpad: %d\n", error);
		return error;
	}

	return 0;
}

static int elan_smbus_set_mode(struct i2c_client *client, u8 mode)
{
	u8 cmd[4] = { 0x00, 0x07, 0x00, mode };

	return i2c_smbus_write_block_data(client, ETP_SMBUS_IAP_CMD,
					  sizeof(cmd), cmd);
}

static int elan_smbus_sleep_control(struct i2c_client *client, bool sleep)
{
	if (sleep)
		return i2c_smbus_write_byte(client, ETP_SMBUS_SLEEP_CMD);
	else
		return 0; /* XXX should we send ETP_SMBUS_ENABLE_TP here? */
}

static int elan_smbus_power_control(struct i2c_client *client, bool enable)
{
	return 0; /* A no-op */
}

static int elan_smbus_calibrate(struct i2c_client *client)
{
	u8 cmd[4] = { 0x00, 0x08, 0x00, 0x01 };

	return i2c_smbus_write_block_data(client, ETP_SMBUS_IAP_CMD,
					  sizeof(cmd), cmd);
}

static int elan_smbus_calibrate_result(struct i2c_client *client, u8 *val)
{
	int error;

	error = i2c_smbus_read_block_data(client,
					  ETP_SMBUS_CALIBRATE_QUERY, val);
	if (error < 0)
		return error;

	return 0;
}

static int elan_smbus_get_baseline_data(struct i2c_client *client,
					bool max_baseline, u8 *value)
{
	int error;
	u8 val[I2C_SMBUS_BLOCK_MAX] = {0};

	error = i2c_smbus_read_block_data(client,
					  max_baseline ?
						ETP_SMBUS_MAX_BASELINE_CMD :
						ETP_SMBUS_MIN_BASELINE_CMD,
					  val);
	if (error < 0)
		return error;

	*value = be16_to_cpup((__be16 *)val);

	return 0;
}

static int elan_smbus_get_version(struct i2c_client *client,
				  bool iap, u8 *version)
{
	int error;
	u8 val[I2C_SMBUS_BLOCK_MAX] = {0};

	error = i2c_smbus_read_block_data(client,
					  iap ? ETP_SMBUS_IAP_VERSION_CMD :
						ETP_SMBUS_FW_VERSION_CMD,
					  val);
	if (error < 0) {
		dev_err(&client->dev, "failed to get %s version: %d\n",
			iap ? "IAP" : "FW", error);
		return error;
	}

	*version = val[2];
	return 0;
}

static int elan_smbus_get_sm_version(struct i2c_client *client,
				     u16 *ic_type, u8 *version,
				     u8 *clickpad)
{
	int error;
	u8 val[I2C_SMBUS_BLOCK_MAX] = {0};

	error = i2c_smbus_read_block_data(client,
					  ETP_SMBUS_SM_VERSION_CMD, val);
	if (error < 0) {
		dev_err(&client->dev, "failed to get SM version: %d\n", error);
		return error;
	}

	*version = val[0];
	*ic_type = val[1];
	*clickpad = val[0] & 0x10;
	return 0;
}

static int elan_smbus_get_product_id(struct i2c_client *client, u16 *id)
{
	int error;
	u8 val[I2C_SMBUS_BLOCK_MAX] = {0};

	error = i2c_smbus_read_block_data(client,
					  ETP_SMBUS_UNIQUEID_CMD, val);
	if (error < 0) {
		dev_err(&client->dev, "failed to get product ID: %d\n", error);
		return error;
	}

	*id = be16_to_cpup((__be16 *)val);
	return 0;
}

static int elan_smbus_get_checksum(struct i2c_client *client,
				   bool iap, u16 *csum)
{
	int error;
	u8 val[I2C_SMBUS_BLOCK_MAX] = {0};

	error = i2c_smbus_read_block_data(client,
					  iap ? ETP_SMBUS_FW_CHECKSUM_CMD :
						ETP_SMBUS_IAP_CHECKSUM_CMD,
					  val);
	if (error < 0) {
		dev_err(&client->dev, "failed to get %s checksum: %d\n",
			iap ? "IAP" : "FW", error);
		return error;
	}

	*csum = be16_to_cpup((__be16 *)val);
	return 0;
}

static int elan_smbus_get_max(struct i2c_client *client,
			      unsigned int *max_x, unsigned int *max_y)
{
	int ret;
	int error;
	u8 val[I2C_SMBUS_BLOCK_MAX] = {0};

	ret = i2c_smbus_read_block_data(client, ETP_SMBUS_RANGE_CMD, val);
	if (ret != 3) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&client->dev, "failed to get dimensions: %d\n", error);
		return error;
	}

	*max_x = (0x0f & val[0]) << 8 | val[1];
	*max_y = (0xf0 & val[0]) << 4 | val[2];

	return 0;
}

static int elan_smbus_get_resolution(struct i2c_client *client,
				     u8 *hw_res_x, u8 *hw_res_y)
{
	int ret;
	int error;
	u8 val[I2C_SMBUS_BLOCK_MAX] = {0};

	ret = i2c_smbus_read_block_data(client, ETP_SMBUS_RESOLUTION_CMD, val);
	if (ret != 3) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&client->dev, "failed to get resolution: %d\n", error);
		return error;
	}

	*hw_res_x = val[1] & 0x0F;
	*hw_res_y = (val[1] & 0xF0) >> 4;

	return 0;
}

static int elan_smbus_get_num_traces(struct i2c_client *client,
				     unsigned int *x_traces,
				     unsigned int *y_traces)
{
	int ret;
	int error;
	u8 val[I2C_SMBUS_BLOCK_MAX] = {0};

	ret = i2c_smbus_read_block_data(client, ETP_SMBUS_XY_TRACENUM_CMD, val);
	if (ret != 3) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&client->dev, "failed to get trace info: %d\n", error);
		return error;
	}

	*x_traces = val[1];
	*y_traces = val[2];

	return 0;
}

static int elan_smbus_get_pressure_adjustment(struct i2c_client *client,
					      int *adjustment)
{
	*adjustment = ETP_PRESSURE_OFFSET;
	return 0;
}

static int elan_smbus_iap_get_mode(struct i2c_client *client,
				   enum tp_mode *mode)
{
	int error;
	u16 constant;
	u8 val[I2C_SMBUS_BLOCK_MAX] = {0};

	error = i2c_smbus_read_block_data(client, ETP_SMBUS_IAP_CTRL_CMD, val);
	if (error < 0) {
		dev_err(&client->dev, "failed to read iap ctrol register: %d\n",
			error);
		return error;
	}

	constant = be16_to_cpup((__be16 *)val);
	dev_dbg(&client->dev, "iap control reg: 0x%04x.\n", constant);

	*mode = (constant & ETP_SMBUS_IAP_MODE_ON) ? IAP_MODE : MAIN_MODE;

	return 0;
}

static int elan_smbus_iap_reset(struct i2c_client *client)
{
	int error;

	error = i2c_smbus_write_byte(client, ETP_SMBUS_IAP_RESET_CMD);
	if (error) {
		dev_err(&client->dev, "cannot reset IC: %d\n", error);
		return error;
	}

	return 0;
}

static int elan_smbus_set_flash_key(struct i2c_client *client)
{
	int error;
	u8 cmd[4] = { 0x00, 0x0B, 0x00, 0x5A };

	error = i2c_smbus_write_block_data(client, ETP_SMBUS_IAP_CMD,
					   sizeof(cmd), cmd);
	if (error) {
		dev_err(&client->dev, "cannot set flash key: %d\n", error);
		return error;
	}

	return 0;
}

static int elan_smbus_prepare_fw_update(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	int len;
	int error;
	enum tp_mode mode;
	u8 val[I2C_SMBUS_BLOCK_MAX] = {0};
	u8 cmd[4] = {0x0F, 0x78, 0x00, 0x06};
	u16 password;

	/* Get FW in which mode	(IAP_MODE/MAIN_MODE)  */
	error = elan_smbus_iap_get_mode(client, &mode);
	if (error)
		return error;

	if (mode == MAIN_MODE) {

		/* set flash key */
		error = elan_smbus_set_flash_key(client);
		if (error)
			return error;

		/* write iap password */
		if (i2c_smbus_write_byte(client,
					 ETP_SMBUS_IAP_PASSWORD_WRITE) < 0) {
			dev_err(dev, "cannot write iap password\n");
			return -EIO;
		}

		error = i2c_smbus_write_block_data(client, ETP_SMBUS_IAP_CMD,
						   sizeof(cmd), cmd);
		if (error) {
			dev_err(dev, "failed to write iap password: %d\n",
				error);
			return error;
		}

		/*
		 * Read back password to make sure we enabled flash
		 * successfully.
		 */
		len = i2c_smbus_read_block_data(client,
						ETP_SMBUS_IAP_PASSWORD_READ,
						val);
		if (len < sizeof(u16)) {
			error = len < 0 ? len : -EIO;
			dev_err(dev, "failed to read iap password: %d\n",
				error);
			return error;
		}

		password = be16_to_cpup((__be16 *)val);
		if (password != ETP_SMBUS_IAP_PASSWORD) {
			dev_err(dev, "wrong iap password = 0x%X\n", password);
			return -EIO;
		}

		/* Wait 30ms for MAIN_MODE change to IAP_MODE */
		msleep(30);
	}

	error = elan_smbus_set_flash_key(client);
	if (error)
		return error;

	/* Reset IC */
	error = elan_smbus_iap_reset(client);
	if (error)
		return error;

	return 0;
}


static int elan_smbus_write_fw_block(struct i2c_client *client,
				     const u8 *page, u16 checksum, int idx)
{
	struct device *dev = &client->dev;
	int error;
	u16 result;
	u8 val[I2C_SMBUS_BLOCK_MAX] = {0};

	/*
	 * Due to the limitation of smbus protocol limiting
	 * transfer to 32 bytes at a time, we must split block
	 * in 2 transfers.
	 */
	error = i2c_smbus_write_block_data(client,
					   ETP_SMBUS_WRITE_FW_BLOCK,
					   ETP_FW_PAGE_SIZE / 2,
					   page);
	if (error) {
		dev_err(dev, "Failed to write page %d (part %d): %d\n",
			idx, 1, error);
		return error;
	}

	error = i2c_smbus_write_block_data(client,
					   ETP_SMBUS_WRITE_FW_BLOCK,
					   ETP_FW_PAGE_SIZE / 2,
					   page + ETP_FW_PAGE_SIZE / 2);
	if (error) {
		dev_err(dev, "Failed to write page %d (part %d): %d\n",
			idx, 2, error);
		return error;
	}


	/* Wait for F/W to update one page ROM data. */
	usleep_range(8000, 10000);

	error = i2c_smbus_read_block_data(client,
					  ETP_SMBUS_IAP_CTRL_CMD, val);
	if (error < 0) {
		dev_err(dev, "Failed to read IAP write result: %d\n",
			error);
		return error;
	}

	result = be16_to_cpup((__be16 *)val);
	if (result & (ETP_FW_IAP_PAGE_ERR | ETP_FW_IAP_INTF_ERR)) {
		dev_err(dev, "IAP reports failed write: %04hx\n",
			result);
		return -EIO;
	}

	return 0;
}

static int elan_smbus_get_report(struct i2c_client *client, u8 *report)
{
	int len;

	len = i2c_smbus_read_block_data(client,
					ETP_SMBUS_PACKET_QUERY,
					&report[ETP_SMBUS_REPORT_OFFSET]);
	if (len < 0) {
		dev_err(&client->dev, "failed to read report data: %d\n", len);
		return len;
	}

	if (len != ETP_SMBUS_REPORT_LEN) {
		dev_err(&client->dev,
			"wrong report length (%d vs %d expected)\n",
			len, ETP_SMBUS_REPORT_LEN);
		return -EIO;
	}

	return 0;
}

static int elan_smbus_finish_fw_update(struct i2c_client *client,
				       struct completion *fw_completion)
{
	/* No special handling unlike I2C transport */
	return 0;
}

static int elan_smbus_get_pattern(struct i2c_client *client, u8 *pattern)
{
	*pattern = 0;
	return 0;
}

const struct elan_transport_ops elan_smbus_ops = {
	.initialize		= elan_smbus_initialize,
	.sleep_control		= elan_smbus_sleep_control,
	.power_control		= elan_smbus_power_control,
	.set_mode		= elan_smbus_set_mode,

	.calibrate		= elan_smbus_calibrate,
	.calibrate_result	= elan_smbus_calibrate_result,

	.get_baseline_data	= elan_smbus_get_baseline_data,

	.get_version		= elan_smbus_get_version,
	.get_sm_version		= elan_smbus_get_sm_version,
	.get_product_id		= elan_smbus_get_product_id,
	.get_checksum		= elan_smbus_get_checksum,
	.get_pressure_adjustment = elan_smbus_get_pressure_adjustment,

	.get_max		= elan_smbus_get_max,
	.get_resolution		= elan_smbus_get_resolution,
	.get_num_traces		= elan_smbus_get_num_traces,

	.iap_get_mode		= elan_smbus_iap_get_mode,
	.iap_reset		= elan_smbus_iap_reset,

	.prepare_fw_update	= elan_smbus_prepare_fw_update,
	.write_fw_block		= elan_smbus_write_fw_block,
	.finish_fw_update	= elan_smbus_finish_fw_update,

	.get_report		= elan_smbus_get_report,
	.get_pattern		= elan_smbus_get_pattern,
};
