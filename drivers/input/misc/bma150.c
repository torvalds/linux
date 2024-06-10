// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2011 Bosch Sensortec GmbH
 * Copyright (c) 2011 Unixphere
 *
 * This driver adds support for Bosch Sensortec's digital acceleration
 * sensors BMA150 and SMB380.
 * The SMB380 is fully compatible with BMA150 and only differs in packaging.
 *
 * The datasheet for the BMA150 chip can be found here:
 * http://www.bosch-sensortec.com/content/language1/downloads/BST-BMA150-DS000-07.pdf
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/bma150.h>

#define ABSMAX_ACC_VAL		0x01FF
#define ABSMIN_ACC_VAL		-(ABSMAX_ACC_VAL)

/* Each axis is represented by a 2-byte data word */
#define BMA150_XYZ_DATA_SIZE	6

/* Input poll interval in milliseconds */
#define BMA150_POLL_INTERVAL	10
#define BMA150_POLL_MAX		200
#define BMA150_POLL_MIN		0

#define BMA150_MODE_NORMAL	0
#define BMA150_MODE_SLEEP	2
#define BMA150_MODE_WAKE_UP	3

/* Data register addresses */
#define BMA150_DATA_0_REG	0x00
#define BMA150_DATA_1_REG	0x01
#define BMA150_DATA_2_REG	0x02

/* Control register addresses */
#define BMA150_CTRL_0_REG	0x0A
#define BMA150_CTRL_1_REG	0x0B
#define BMA150_CTRL_2_REG	0x14
#define BMA150_CTRL_3_REG	0x15

/* Configuration/Setting register addresses */
#define BMA150_CFG_0_REG	0x0C
#define BMA150_CFG_1_REG	0x0D
#define BMA150_CFG_2_REG	0x0E
#define BMA150_CFG_3_REG	0x0F
#define BMA150_CFG_4_REG	0x10
#define BMA150_CFG_5_REG	0x11

#define BMA150_CHIP_ID		2
#define BMA150_CHIP_ID_REG	BMA150_DATA_0_REG

#define BMA150_ACC_X_LSB_REG	BMA150_DATA_2_REG

#define BMA150_SLEEP_POS	0
#define BMA150_SLEEP_MSK	0x01
#define BMA150_SLEEP_REG	BMA150_CTRL_0_REG

#define BMA150_BANDWIDTH_POS	0
#define BMA150_BANDWIDTH_MSK	0x07
#define BMA150_BANDWIDTH_REG	BMA150_CTRL_2_REG

#define BMA150_RANGE_POS	3
#define BMA150_RANGE_MSK	0x18
#define BMA150_RANGE_REG	BMA150_CTRL_2_REG

#define BMA150_WAKE_UP_POS	0
#define BMA150_WAKE_UP_MSK	0x01
#define BMA150_WAKE_UP_REG	BMA150_CTRL_3_REG

#define BMA150_SW_RES_POS	1
#define BMA150_SW_RES_MSK	0x02
#define BMA150_SW_RES_REG	BMA150_CTRL_0_REG

/* Any-motion interrupt register fields */
#define BMA150_ANY_MOTION_EN_POS	6
#define BMA150_ANY_MOTION_EN_MSK	0x40
#define BMA150_ANY_MOTION_EN_REG	BMA150_CTRL_1_REG

#define BMA150_ANY_MOTION_DUR_POS	6
#define BMA150_ANY_MOTION_DUR_MSK	0xC0
#define BMA150_ANY_MOTION_DUR_REG	BMA150_CFG_5_REG

#define BMA150_ANY_MOTION_THRES_REG	BMA150_CFG_4_REG

/* Advanced interrupt register fields */
#define BMA150_ADV_INT_EN_POS		6
#define BMA150_ADV_INT_EN_MSK		0x40
#define BMA150_ADV_INT_EN_REG		BMA150_CTRL_3_REG

/* High-G interrupt register fields */
#define BMA150_HIGH_G_EN_POS		1
#define BMA150_HIGH_G_EN_MSK		0x02
#define BMA150_HIGH_G_EN_REG		BMA150_CTRL_1_REG

#define BMA150_HIGH_G_HYST_POS		3
#define BMA150_HIGH_G_HYST_MSK		0x38
#define BMA150_HIGH_G_HYST_REG		BMA150_CFG_5_REG

#define BMA150_HIGH_G_DUR_REG		BMA150_CFG_3_REG
#define BMA150_HIGH_G_THRES_REG		BMA150_CFG_2_REG

/* Low-G interrupt register fields */
#define BMA150_LOW_G_EN_POS		0
#define BMA150_LOW_G_EN_MSK		0x01
#define BMA150_LOW_G_EN_REG		BMA150_CTRL_1_REG

#define BMA150_LOW_G_HYST_POS		0
#define BMA150_LOW_G_HYST_MSK		0x07
#define BMA150_LOW_G_HYST_REG		BMA150_CFG_5_REG

#define BMA150_LOW_G_DUR_REG		BMA150_CFG_1_REG
#define BMA150_LOW_G_THRES_REG		BMA150_CFG_0_REG

struct bma150_data {
	struct i2c_client *client;
	struct input_dev *input;
	u8 mode;
};

/*
 * The settings for the given range, bandwidth and interrupt features
 * are stated and verified by Bosch Sensortec where they are configured
 * to provide a generic sensitivity performance.
 */
static const struct bma150_cfg default_cfg = {
	.any_motion_int = 1,
	.hg_int = 1,
	.lg_int = 1,
	.any_motion_dur = 0,
	.any_motion_thres = 0,
	.hg_hyst = 0,
	.hg_dur = 150,
	.hg_thres = 160,
	.lg_hyst = 0,
	.lg_dur = 150,
	.lg_thres = 20,
	.range = BMA150_RANGE_2G,
	.bandwidth = BMA150_BW_50HZ
};

static int bma150_write_byte(struct i2c_client *client, u8 reg, u8 val)
{
	s32 ret;

	/* As per specification, disable irq in between register writes */
	if (client->irq)
		disable_irq_nosync(client->irq);

	ret = i2c_smbus_write_byte_data(client, reg, val);

	if (client->irq)
		enable_irq(client->irq);

	return ret;
}

static int bma150_set_reg_bits(struct i2c_client *client,
					int val, int shift, u8 mask, u8 reg)
{
	int data;

	data = i2c_smbus_read_byte_data(client, reg);
	if (data < 0)
		return data;

	data = (data & ~mask) | ((val << shift) & mask);
	return bma150_write_byte(client, reg, data);
}

static int bma150_set_mode(struct bma150_data *bma150, u8 mode)
{
	int error;

	error = bma150_set_reg_bits(bma150->client, mode, BMA150_WAKE_UP_POS,
				BMA150_WAKE_UP_MSK, BMA150_WAKE_UP_REG);
	if (error)
		return error;

	error = bma150_set_reg_bits(bma150->client, mode, BMA150_SLEEP_POS,
				BMA150_SLEEP_MSK, BMA150_SLEEP_REG);
	if (error)
		return error;

	if (mode == BMA150_MODE_NORMAL)
		usleep_range(2000, 2100);

	bma150->mode = mode;
	return 0;
}

static int bma150_soft_reset(struct bma150_data *bma150)
{
	int error;

	error = bma150_set_reg_bits(bma150->client, 1, BMA150_SW_RES_POS,
				BMA150_SW_RES_MSK, BMA150_SW_RES_REG);
	if (error)
		return error;

	usleep_range(2000, 2100);
	return 0;
}

static int bma150_set_range(struct bma150_data *bma150, u8 range)
{
	return bma150_set_reg_bits(bma150->client, range, BMA150_RANGE_POS,
				BMA150_RANGE_MSK, BMA150_RANGE_REG);
}

static int bma150_set_bandwidth(struct bma150_data *bma150, u8 bw)
{
	return bma150_set_reg_bits(bma150->client, bw, BMA150_BANDWIDTH_POS,
				BMA150_BANDWIDTH_MSK, BMA150_BANDWIDTH_REG);
}

static int bma150_set_low_g_interrupt(struct bma150_data *bma150,
					u8 enable, u8 hyst, u8 dur, u8 thres)
{
	int error;

	error = bma150_set_reg_bits(bma150->client, hyst,
				BMA150_LOW_G_HYST_POS, BMA150_LOW_G_HYST_MSK,
				BMA150_LOW_G_HYST_REG);
	if (error)
		return error;

	error = bma150_write_byte(bma150->client, BMA150_LOW_G_DUR_REG, dur);
	if (error)
		return error;

	error = bma150_write_byte(bma150->client, BMA150_LOW_G_THRES_REG, thres);
	if (error)
		return error;

	return bma150_set_reg_bits(bma150->client, !!enable,
				BMA150_LOW_G_EN_POS, BMA150_LOW_G_EN_MSK,
				BMA150_LOW_G_EN_REG);
}

static int bma150_set_high_g_interrupt(struct bma150_data *bma150,
					u8 enable, u8 hyst, u8 dur, u8 thres)
{
	int error;

	error = bma150_set_reg_bits(bma150->client, hyst,
				BMA150_HIGH_G_HYST_POS, BMA150_HIGH_G_HYST_MSK,
				BMA150_HIGH_G_HYST_REG);
	if (error)
		return error;

	error = bma150_write_byte(bma150->client,
				BMA150_HIGH_G_DUR_REG, dur);
	if (error)
		return error;

	error = bma150_write_byte(bma150->client,
				BMA150_HIGH_G_THRES_REG, thres);
	if (error)
		return error;

	return bma150_set_reg_bits(bma150->client, !!enable,
				BMA150_HIGH_G_EN_POS, BMA150_HIGH_G_EN_MSK,
				BMA150_HIGH_G_EN_REG);
}


static int bma150_set_any_motion_interrupt(struct bma150_data *bma150,
						u8 enable, u8 dur, u8 thres)
{
	int error;

	error = bma150_set_reg_bits(bma150->client, dur,
				BMA150_ANY_MOTION_DUR_POS,
				BMA150_ANY_MOTION_DUR_MSK,
				BMA150_ANY_MOTION_DUR_REG);
	if (error)
		return error;

	error = bma150_write_byte(bma150->client,
				BMA150_ANY_MOTION_THRES_REG, thres);
	if (error)
		return error;

	error = bma150_set_reg_bits(bma150->client, !!enable,
				BMA150_ADV_INT_EN_POS, BMA150_ADV_INT_EN_MSK,
				BMA150_ADV_INT_EN_REG);
	if (error)
		return error;

	return bma150_set_reg_bits(bma150->client, !!enable,
				BMA150_ANY_MOTION_EN_POS,
				BMA150_ANY_MOTION_EN_MSK,
				BMA150_ANY_MOTION_EN_REG);
}

static void bma150_report_xyz(struct bma150_data *bma150)
{
	u8 data[BMA150_XYZ_DATA_SIZE];
	s16 x, y, z;
	s32 ret;

	ret = i2c_smbus_read_i2c_block_data(bma150->client,
			BMA150_ACC_X_LSB_REG, BMA150_XYZ_DATA_SIZE, data);
	if (ret != BMA150_XYZ_DATA_SIZE)
		return;

	x = ((0xc0 & data[0]) >> 6) | (data[1] << 2);
	y = ((0xc0 & data[2]) >> 6) | (data[3] << 2);
	z = ((0xc0 & data[4]) >> 6) | (data[5] << 2);

	x = sign_extend32(x, 9);
	y = sign_extend32(y, 9);
	z = sign_extend32(z, 9);

	input_report_abs(bma150->input, ABS_X, x);
	input_report_abs(bma150->input, ABS_Y, y);
	input_report_abs(bma150->input, ABS_Z, z);
	input_sync(bma150->input);
}

static irqreturn_t bma150_irq_thread(int irq, void *dev)
{
	bma150_report_xyz(dev);

	return IRQ_HANDLED;
}

static void bma150_poll(struct input_dev *input)
{
	struct bma150_data *bma150 = input_get_drvdata(input);

	bma150_report_xyz(bma150);
}

static int bma150_open(struct input_dev *input)
{
	struct bma150_data *bma150 = input_get_drvdata(input);
	int error;

	error = pm_runtime_get_sync(&bma150->client->dev);
	if (error < 0 && error != -ENOSYS)
		return error;

	/*
	 * See if runtime PM woke up the device. If runtime PM
	 * is disabled we need to do it ourselves.
	 */
	if (bma150->mode != BMA150_MODE_NORMAL) {
		error = bma150_set_mode(bma150, BMA150_MODE_NORMAL);
		if (error)
			return error;
	}

	return 0;
}

static void bma150_close(struct input_dev *input)
{
	struct bma150_data *bma150 = input_get_drvdata(input);

	pm_runtime_put_sync(&bma150->client->dev);

	if (bma150->mode != BMA150_MODE_SLEEP)
		bma150_set_mode(bma150, BMA150_MODE_SLEEP);
}

static int bma150_initialize(struct bma150_data *bma150,
			     const struct bma150_cfg *cfg)
{
	int error;

	error = bma150_soft_reset(bma150);
	if (error)
		return error;

	error = bma150_set_bandwidth(bma150, cfg->bandwidth);
	if (error)
		return error;

	error = bma150_set_range(bma150, cfg->range);
	if (error)
		return error;

	if (bma150->client->irq) {
		error = bma150_set_any_motion_interrupt(bma150,
					cfg->any_motion_int,
					cfg->any_motion_dur,
					cfg->any_motion_thres);
		if (error)
			return error;

		error = bma150_set_high_g_interrupt(bma150,
					cfg->hg_int, cfg->hg_hyst,
					cfg->hg_dur, cfg->hg_thres);
		if (error)
			return error;

		error = bma150_set_low_g_interrupt(bma150,
					cfg->lg_int, cfg->lg_hyst,
					cfg->lg_dur, cfg->lg_thres);
		if (error)
			return error;
	}

	return bma150_set_mode(bma150, BMA150_MODE_SLEEP);
}

static int bma150_probe(struct i2c_client *client)
{
	const struct bma150_platform_data *pdata =
			dev_get_platdata(&client->dev);
	const struct bma150_cfg *cfg;
	struct bma150_data *bma150;
	struct input_dev *idev;
	int chip_id;
	int error;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "i2c_check_functionality error\n");
		return -EIO;
	}

	chip_id = i2c_smbus_read_byte_data(client, BMA150_CHIP_ID_REG);
	if (chip_id != BMA150_CHIP_ID) {
		dev_err(&client->dev, "BMA150 chip id error: %d\n", chip_id);
		return -EINVAL;
	}

	bma150 = devm_kzalloc(&client->dev, sizeof(*bma150), GFP_KERNEL);
	if (!bma150)
		return -ENOMEM;

	bma150->client = client;

	if (pdata) {
		if (pdata->irq_gpio_cfg) {
			error = pdata->irq_gpio_cfg();
			if (error) {
				dev_err(&client->dev,
					"IRQ GPIO conf. error %d, error %d\n",
					client->irq, error);
				return error;
			}
		}
		cfg = &pdata->cfg;
	} else {
		cfg = &default_cfg;
	}

	error = bma150_initialize(bma150, cfg);
	if (error)
		return error;

	idev = devm_input_allocate_device(&bma150->client->dev);
	if (!idev)
		return -ENOMEM;

	input_set_drvdata(idev, bma150);
	bma150->input = idev;

	idev->name = BMA150_DRIVER;
	idev->phys = BMA150_DRIVER "/input0";
	idev->id.bustype = BUS_I2C;

	idev->open = bma150_open;
	idev->close = bma150_close;

	input_set_abs_params(idev, ABS_X, ABSMIN_ACC_VAL, ABSMAX_ACC_VAL, 0, 0);
	input_set_abs_params(idev, ABS_Y, ABSMIN_ACC_VAL, ABSMAX_ACC_VAL, 0, 0);
	input_set_abs_params(idev, ABS_Z, ABSMIN_ACC_VAL, ABSMAX_ACC_VAL, 0, 0);

	if (client->irq <= 0) {
		error = input_setup_polling(idev, bma150_poll);
		if (error)
			return error;

		input_set_poll_interval(idev, BMA150_POLL_INTERVAL);
		input_set_min_poll_interval(idev, BMA150_POLL_MIN);
		input_set_max_poll_interval(idev, BMA150_POLL_MAX);
	}

	error = input_register_device(idev);
	if (error)
		return error;

	if (client->irq > 0) {
		error = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, bma150_irq_thread,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					BMA150_DRIVER, bma150);
		if (error) {
			dev_err(&client->dev,
				"irq request failed %d, error %d\n",
				client->irq, error);
			return error;
		}
	}

	i2c_set_clientdata(client, bma150);

	pm_runtime_enable(&client->dev);

	return 0;
}

static void bma150_remove(struct i2c_client *client)
{
	pm_runtime_disable(&client->dev);
}

static int __maybe_unused bma150_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma150_data *bma150 = i2c_get_clientdata(client);

	return bma150_set_mode(bma150, BMA150_MODE_SLEEP);
}

static int __maybe_unused bma150_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma150_data *bma150 = i2c_get_clientdata(client);

	return bma150_set_mode(bma150, BMA150_MODE_NORMAL);
}

static UNIVERSAL_DEV_PM_OPS(bma150_pm, bma150_suspend, bma150_resume, NULL);

static const struct i2c_device_id bma150_id[] = {
	{ "bma150" },
	{ "smb380" },
	{ "bma023" },
	{ }
};

MODULE_DEVICE_TABLE(i2c, bma150_id);

static struct i2c_driver bma150_driver = {
	.driver = {
		.name	= BMA150_DRIVER,
		.pm	= &bma150_pm,
	},
	.class		= I2C_CLASS_HWMON,
	.id_table	= bma150_id,
	.probe		= bma150_probe,
	.remove		= bma150_remove,
};

module_i2c_driver(bma150_driver);

MODULE_AUTHOR("Albert Zhang <xu.zhang@bosch-sensortec.com>");
MODULE_DESCRIPTION("BMA150 driver");
MODULE_LICENSE("GPL");
