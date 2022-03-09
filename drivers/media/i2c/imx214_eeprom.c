// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022 Fuzhou Rockchip Electronics Co., Ltd.

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/rk-camera-module.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include "imx214_eeprom_head.h"

#define DEVICE_NAME			"imx214_eeprom"

static inline struct imx214_eeprom_device
	*sd_to_imx214_eeprom(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct imx214_eeprom_device, sd);
}

/* Read registers up to 4 at a time */
static int imx214_read_reg_otp(struct i2c_client *client, u16 reg,
	unsigned int len, u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret;

	if (len > 4 || !len)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

static u8 get_vendor_flag(struct i2c_client *client)
{
	u8 vendor_flag = 0;

	if (client->addr == SLAVE_ADDRESS_GZ)
		vendor_flag |= 0x80;
	return vendor_flag;
}

static int imx214_otp_read_gz(struct imx214_eeprom_device *imx214_eeprom_dev)
{
	struct i2c_client *client = imx214_eeprom_dev->client;
	int otp_flag, i;
	struct imx214_otp_info *otp_ptr;
	struct device *dev = &imx214_eeprom_dev->client->dev;
	int ret = 0;
	u32 r_value, gr_value, gb_value, b_value;
	u32 temp = 0;
	u32 checksum = 0;

	otp_ptr = kzalloc(sizeof(*otp_ptr), GFP_KERNEL);
	if (!otp_ptr)
		return -ENOMEM;

	otp_flag = 0;
	/* OTP base information*/
	ret = imx214_read_reg_otp(client, GZ_INFO_FLAG_REG,
		1, &otp_flag);
	if (otp_flag == 0x01) {
		otp_ptr->flag = 0x80; /* valid INFO in OTP */
		ret |= imx214_read_reg_otp(client, GZ_ID_REG,
			1, &otp_ptr->module_id);
		ret |= imx214_read_reg_otp(client, GZ_LENS_ID_REG,
			1, &otp_ptr->lens_id);
		ret |= imx214_read_reg_otp(client, GZ_PRODUCT_YEAR_REG,
			1, &otp_ptr->year);
		ret |= imx214_read_reg_otp(client, GZ_PRODUCT_MONTH_REG,
			1, &otp_ptr->month);
		ret |= imx214_read_reg_otp(client, GZ_PRODUCT_DAY_REG,
			1, &otp_ptr->day);
		dev_dbg(dev, "fac info: module(0x%x) lens(0x%x) time(%d_%d_%d)!\n",
			otp_ptr->module_id,
			otp_ptr->lens_id,
			otp_ptr->year,
			otp_ptr->month,
			otp_ptr->day);
		if (ret)
			goto err;
	}

	/* OTP WB calibration data */
	ret = imx214_read_reg_otp(client, GZ_AWB_FLAG_REG,
		1, &otp_flag);
	if (otp_flag == 0x01) {
		otp_ptr->flag |= 0x40; /* valid AWB in OTP */
		ret |= imx214_read_reg_otp(client, GZ_CUR_R_REG,
			1, &r_value);
		checksum += r_value;
		ret |= imx214_read_reg_otp(client, GZ_CUR_GR_REG,
			1, &gr_value);
		checksum += gr_value;
		ret |= imx214_read_reg_otp(client, GZ_CUR_GB_REG,
			1, &gb_value);
		checksum += gb_value;
		ret |= imx214_read_reg_otp(client, GZ_CUR_B_REG,
			1, &b_value);
		checksum += b_value;
		otp_ptr->rg_ratio =
			r_value * 1024 / ((gr_value + gb_value) / 2);
		otp_ptr->bg_ratio =
			b_value * 1024 / ((gr_value + gb_value) / 2);
		ret |= imx214_read_reg_otp(client, GZ_GOLDEN_R_REG,
			1, &r_value);
		checksum += r_value;
		ret |= imx214_read_reg_otp(client, GZ_GOLDEN_GR_REG,
			1, &gr_value);
		checksum += gr_value;
		ret |= imx214_read_reg_otp(client, GZ_GOLDEN_GB_REG,
			1, &gb_value);
		checksum += gb_value;
		ret |= imx214_read_reg_otp(client, GZ_GOLDEN_B_REG,
			1, &b_value);
		checksum += b_value;
		otp_ptr->rg_golden =
			r_value * 1024 / ((gr_value + gb_value) / 2);
		otp_ptr->bg_golden =
			b_value * 1024 / ((gr_value + gb_value) / 2);
		ret |= imx214_read_reg_otp(client, GZ_AWB_CHECKSUM_REG,
			1, &temp);
		if (ret != 0 || (checksum % 0xff) != temp) {
			dev_err(dev, "otp awb info: check sum (%d,%d),ret = %d !\n",
				checksum,
				temp,
				ret);
			goto err;
		}
		dev_dbg(dev, "awb cur:(rg 0x%x, bg 0x%x,)\n",
			otp_ptr->rg_ratio, otp_ptr->bg_ratio);
		dev_dbg(dev, "awb gol:(rg 0x%x, bg 0x%x)\n",
			otp_ptr->rg_golden, otp_ptr->bg_golden);
	}

	checksum = 0;
	/* OTP LSC calibration data */
	ret = imx214_read_reg_otp(client, GZ_LSC_FLAG_REG,
		1, &otp_flag);
	if (otp_flag == 0x01) {
		otp_ptr->flag |= 0x10; /* valid LSC in OTP */
		for (i = 0; i < 504; i++) {
			ret |= imx214_read_reg_otp(client,
				GZ_LSC_DATA_START_REG + i,
				1, &temp);
			otp_ptr->lenc[i] = temp;
			checksum += temp;
			dev_dbg(dev,
				"otp read lsc addr = 0x%04x, lenc[%d] = %d\n",
				GZ_LSC_DATA_START_REG + i, i, temp);
		}
		ret |= imx214_read_reg_otp(client, GZ_LSC_CHECKSUM_REG,
			1, &temp);
		if (ret != 0 || (checksum % 0xff) != temp) {
			dev_err(dev,
				"otp lsc info: check sum (%d,%d),ret = %d !\n",
				checksum, temp, ret);
			goto err;
		}
	}

	checksum = 0;
	/* OTP VCM calibration data */
	ret = imx214_read_reg_otp(client, GZ_VCM_FLAG_REG,
		1, &otp_flag);
	if (otp_flag == 0x01) {
		otp_ptr->flag |= 0x20; /* valid VCM in OTP */
		ret |= imx214_read_reg_otp(client, GZ_VCM_DIR_REG,
			1, &otp_ptr->vcm_dir);
		checksum += otp_ptr->vcm_dir;
		ret |= imx214_read_reg_otp(client, GZ_VCM_START_REG,
			1, &temp);
		checksum += temp;
		ret |= imx214_read_reg_otp(client, GZ_VCM_START_REG + 1,
			1, &otp_ptr->vcm_start);
		checksum += otp_ptr->vcm_start;
		otp_ptr->vcm_start |= (temp << 8);
		ret |= imx214_read_reg_otp(client, GZ_VCM_END_REG,
			1, &temp);
		checksum += temp;
		ret |= imx214_read_reg_otp(client, GZ_VCM_END_REG + 1,
			1, &otp_ptr->vcm_end);
		checksum += otp_ptr->vcm_end;
		otp_ptr->vcm_end |= (temp << 8);
		ret |= imx214_read_reg_otp(client, GZ_VCM_CHECKSUM_REG,
			1, &temp);
		if (ret != 0 || (checksum % 0xff) != temp) {
			dev_err(dev,
				"otp VCM info: check sum (%d,%d),ret = %d !\n",
				checksum, temp, ret);
			goto err;
		}
		dev_dbg(dev, "vcm_info: 0x%x, 0x%x, 0x%x!\n",
			otp_ptr->vcm_start,
			otp_ptr->vcm_end,
			otp_ptr->vcm_dir);
	}

	checksum = 0;
	/* OTP SPC calibration data */
	ret = imx214_read_reg_otp(client, GZ_SPC_FLAG_REG,
		1, &otp_flag);
	if (otp_flag == 0x01) {
		otp_ptr->flag |= 0x08; /* valid LSC in OTP */
		for (i = 0; i < 126; i++) {
			ret |= imx214_read_reg_otp(client,
				GZ_SPC_DATA_START_REG + i,
				1, &temp);
			otp_ptr->spc[i] = (uint8_t)temp;
			checksum += temp;
			dev_dbg(dev,
				"otp read spc addr = 0x%04x, spc[%d] = %d\n",
				GZ_SPC_DATA_START_REG + i, i, temp);
		}
		ret |= imx214_read_reg_otp(client, GZ_SPC_CHECKSUM_REG,
			1, &temp);
		if (ret != 0 || (checksum % 0xff) != temp) {
			dev_err(dev,
				"otp spc info: check sum (%d,%d),ret = %d !\n",
				checksum, temp, ret);
			goto err;
		}
	}

	if (otp_ptr->flag) {
		imx214_eeprom_dev->otp = otp_ptr;
	} else {
		imx214_eeprom_dev->otp = NULL;
		kfree(otp_ptr);
	}

	return 0;
err:
	imx214_eeprom_dev->otp = NULL;
	kfree(otp_ptr);
	return -EINVAL;
}

static int imx214_otp_read(struct imx214_eeprom_device *imx214_eeprom_dev)
{
	u8 vendor_flag = 0;
	struct i2c_client *client = imx214_eeprom_dev->client;

	vendor_flag = get_vendor_flag(client);
	if (vendor_flag == 0x80)
		imx214_otp_read_gz(imx214_eeprom_dev);
	return 0;
}

static long imx214_eeprom_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, void *arg)
{
	struct imx214_eeprom_device *imx214_eeprom_dev =
		sd_to_imx214_eeprom(sd);
	imx214_otp_read(imx214_eeprom_dev);
	if (arg && imx214_eeprom_dev->otp)
		memcpy(arg, imx214_eeprom_dev->otp,
			sizeof(struct imx214_otp_info));
	return 0;
}

static const struct v4l2_subdev_core_ops imx214_eeprom_core_ops = {
	.ioctl = imx214_eeprom_ioctl,
};

static const struct v4l2_subdev_ops imx214_eeprom_ops = {
	.core = &imx214_eeprom_core_ops,
};

static void imx214_eeprom_subdev_cleanup(struct imx214_eeprom_device *dev)
{
	v4l2_device_unregister_subdev(&dev->sd);
	media_entity_cleanup(&dev->sd.entity);
}

static int imx214_eeprom_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct imx214_eeprom_device *imx214_eeprom_dev;

	dev_info(&client->dev, "probing...\n");
	imx214_eeprom_dev = devm_kzalloc(&client->dev,
		sizeof(*imx214_eeprom_dev),
		GFP_KERNEL);

	if (!imx214_eeprom_dev) {
		dev_err(&client->dev, "Probe failed\n");
		return -ENOMEM;
	}
	v4l2_i2c_subdev_init(&imx214_eeprom_dev->sd,
		client, &imx214_eeprom_ops);
	imx214_eeprom_dev->client = client;
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	dev_info(&client->dev, "probing successful\n");

	return 0;
}

static int imx214_eeprom_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx214_eeprom_device *imx214_eeprom_dev =
		sd_to_imx214_eeprom(sd);
	kfree(imx214_eeprom_dev->otp);
	pm_runtime_disable(&client->dev);
	imx214_eeprom_subdev_cleanup(imx214_eeprom_dev);

	return 0;
}

static int __maybe_unused imx214_eeprom_suspend(struct device *dev)
{
	return 0;
}

static int  __maybe_unused imx214_eeprom_resume(struct device *dev)
{
	return 0;
}

static const struct i2c_device_id imx214_eeprom_id_table[] = {
	{ DEVICE_NAME, 0 },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(i2c, imx214_eeprom_id_table);

static const struct of_device_id imx214_eeprom_of_table[] = {
	{ .compatible = "sony,imx214_eeprom" },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(of, imx214_eeprom_of_table);

static const struct dev_pm_ops imx214_eeprom_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(imx214_eeprom_suspend, imx214_eeprom_resume)
	SET_RUNTIME_PM_OPS(imx214_eeprom_suspend, imx214_eeprom_resume, NULL)
};

static struct i2c_driver imx214_eeprom_i2c_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.pm = &imx214_eeprom_pm_ops,
		.of_match_table = imx214_eeprom_of_table,
	},
	.probe = &imx214_eeprom_probe,
	.remove = &imx214_eeprom_remove,
	.id_table = imx214_eeprom_id_table,
};

module_i2c_driver(imx214_eeprom_i2c_driver);

MODULE_DESCRIPTION("IMX214 OTP driver");
MODULE_LICENSE("GPL v2");
