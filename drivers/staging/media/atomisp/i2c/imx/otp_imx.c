/*
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <media/v4l2-device.h>
#include <asm/intel-mid.h>
#include "common.h"

/* Defines for OTP Data Registers */
#define IMX_OTP_START_ADDR		0x3B04
#define IMX_OTP_PAGE_SIZE		64
#define IMX_OTP_READY_REG		0x3B01
#define IMX_OTP_PAGE_REG		0x3B02
#define IMX_OTP_MODE_REG		0x3B00
#define IMX_OTP_PAGE_MAX		20
#define IMX_OTP_READY_REG_DONE		1
#define IMX_OTP_READ_ONETIME		32
#define IMX_OTP_MODE_READ		1
#define IMX227_OTP_START_ADDR           0x0A04
#define IMX227_OTP_ENABLE_REG           0x0A00
#define IMX227_OTP_READY_REG            0x0A01
#define IMX227_OTP_PAGE_REG             0x0A02
#define IMX227_OTP_READY_REG_DONE       1
#define IMX227_OTP_MODE_READ            1

static int
imx_read_otp_data(struct i2c_client *client, u16 len, u16 reg, void *val)
{
	struct i2c_msg msg[2];
	u16 data[IMX_SHORT_MAX] = { 0 };
	int err;

	if (len > IMX_BYTE_MAX) {
		dev_err(&client->dev, "%s error, invalid data length\n",
			__func__);
		return -EINVAL;
	}

	memset(msg, 0 , sizeof(msg));
	memset(data, 0 , sizeof(data));

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = I2C_MSG_LENGTH;
	msg[0].buf = (u8 *)data;
	/* high byte goes first */
	data[0] = cpu_to_be16(reg);

	msg[1].addr = client->addr;
	msg[1].len = len;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = (u8 *)data;

	err = i2c_transfer(client->adapter, msg, 2);
	if (err != 2) {
		if (err >= 0)
			err = -EIO;
		goto error;
	}

	memcpy(val, data, len);
	return 0;

error:
	dev_err(&client->dev, "read from offset 0x%x error %d", reg, err);
	return err;
}

static int imx_read_otp_reg_array(struct i2c_client *client, u16 size, u16 addr,
				  u8 *buf)
{
	u16 index;
	int ret;

	for (index = 0; index + IMX_OTP_READ_ONETIME <= size;
					index += IMX_OTP_READ_ONETIME) {
		ret = imx_read_otp_data(client, IMX_OTP_READ_ONETIME,
					addr + index, &buf[index]);
		if (ret)
			return ret;
	}
	return 0;
}

void *imx_otp_read(struct v4l2_subdev *sd, u8 dev_addr,
	u32 start_addr, u32 size)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 *buf;
	int ret;
	int i;

	buf = devm_kzalloc(&client->dev, size, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < IMX_OTP_PAGE_MAX; i++) {

		/*set page NO.*/
		ret = imx_write_reg(client, IMX_8BIT,
			       IMX_OTP_PAGE_REG, i & 0xff);
		if (ret)
			goto fail;

		/*set read mode*/
		ret = imx_write_reg(client, IMX_8BIT,
			       IMX_OTP_MODE_REG, IMX_OTP_MODE_READ);
		if (ret)
			goto fail;

		/* Reading the OTP data array */
		ret = imx_read_otp_reg_array(client, IMX_OTP_PAGE_SIZE,
			IMX_OTP_START_ADDR, buf + i * IMX_OTP_PAGE_SIZE);
		if (ret)
			goto fail;
	}

	return buf;
fail:
	/* Driver has failed to find valid data */
	dev_err(&client->dev, "sensor found no valid OTP data\n");
	return ERR_PTR(ret);
}

void *imx227_otp_read(struct v4l2_subdev *sd, u8 dev_addr,
	u32 start_addr, u32 size)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 *buf;
	int ret;
	int i;

	buf = devm_kzalloc(&client->dev, size, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < IMX_OTP_PAGE_MAX; i++) {

		/*set page NO.*/
		ret = imx_write_reg(client, IMX_8BIT,
			       IMX227_OTP_PAGE_REG, i & 0xff);
		if (ret)
			goto fail;

		/*set read mode*/
		ret = imx_write_reg(client, IMX_8BIT,
			       IMX227_OTP_ENABLE_REG, IMX227_OTP_MODE_READ);
		if (ret)
			goto fail;

		/* Reading the OTP data array */
		ret = imx_read_otp_reg_array(client, IMX_OTP_PAGE_SIZE,
			IMX227_OTP_START_ADDR, buf + i * IMX_OTP_PAGE_SIZE);
		if (ret)
			goto fail;
	}

	return buf;
fail:
	/* Driver has failed to find valid data */
	dev_err(&client->dev, "sensor found no valid OTP data\n");
	return ERR_PTR(ret);
}

