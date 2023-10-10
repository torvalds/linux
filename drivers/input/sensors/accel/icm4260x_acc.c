// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Rockchip Co.,Ltd.
 * Author: Wangqiang Guo <kay.guo@rock-chips.com>
 */
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/of_gpio.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/sensor-dev.h>
#include <linux/icm4260x.h>

/**
 * icm4260x_set_idle() - Set Idle bit in PWR_MGMT_0 register
 * @client: struct i2c_client..
 *
 * Set ACCEL_LP_CLK_SEL as well when necessary with a proper wait
 *
 * Return: 0 when successful.
 */
static int icm4260x_set_idle(struct i2c_client *client)
{
	u8 reg_pwr_mgmt_0;
	u8 d;
	int ret = 0;

	reg_pwr_mgmt_0 = sensor_read_reg(client, ICM4260X_PWR_MGMT_0);
	/* set Idle bit.
	 * when accel LPM is already enabled, set ACCEL_LP_CLK_SEL bit as well.
	 */
	d = reg_pwr_mgmt_0;
	d |= BIT_IDLE;
	if ((d & BIT_ACCEL_MODE_MASK) == BIT_ACCEL_MODE_LPM)
		d |= BIT_ACCEL_LP_CLK_SEL;

	ret = sensor_write_reg(client, ICM4260X_PWR_MGMT_0, d);
	usleep_range(20, 21);

	return ret;
}

/**
 * icm4260x_mreg_read() - Multiple byte read from MREG area.
 * @client: struct i2c_client.
 * @addr: MREG register start address including bank in upper byte.
 * @len: length to read in byte.
 * @data: pointer to store read data.
 *
 * Return: 0 when successful.
 */
static int icm4260x_mreg_read(struct i2c_client *client, int addr, int len, u8 *data)
{
	int ret;
	u8 reg_pwr_mgmt_0;

	reg_pwr_mgmt_0 = sensor_read_reg(client, ICM4260X_PWR_MGMT_0);

	ret = icm4260x_set_idle(client);
	if (ret)
		return ret;

	ret = sensor_write_reg(client, ICM4260X_BLK_SEL_R, (addr >> 8) & 0xff);
	usleep_range(INV_ICM42607_BLK_SEL_WAIT_US,
			INV_ICM42607_BLK_SEL_WAIT_US + 1);
	if (ret)
		goto restore_bank;

	ret = sensor_write_reg(client, ICM4260X_MADDR_R, addr & 0xff);
	usleep_range(INV_ICM42607_MADDR_WAIT_US,
			INV_ICM42607_MADDR_WAIT_US + 1);
	if (ret)
		goto restore_bank;

	*data = ICM4260X_M_R;
	ret = sensor_rx_data(client, data, len);
	usleep_range(INV_ICM42607_M_RW_WAIT_US,
			INV_ICM42607_M_RW_WAIT_US + 1);
	if (ret)
		goto restore_bank;

restore_bank:
	ret |= sensor_write_reg(client, ICM4260X_BLK_SEL_R, 0);
	usleep_range(INV_ICM42607_BLK_SEL_WAIT_US,
			INV_ICM42607_BLK_SEL_WAIT_US + 1);

	ret |= sensor_write_reg(client, ICM4260X_PWR_MGMT_0, reg_pwr_mgmt_0);

	return ret;
}

/**
 * icm4260x_mreg_single_write() - Single byte write to MREG area.
 * @client: struct i2c_client.
 * @addr: MREG register address including bank in upper byte.
 * @data: data to write.
 *
 * Return: 0 when successful.
 */
static int icm4260x_mreg_single_write(struct i2c_client *client, int addr, u8 data)
{
	int ret;
	u8 reg_pwr_mgmt_0;

	reg_pwr_mgmt_0 = sensor_read_reg(client, ICM4260X_PWR_MGMT_0);

	ret = icm4260x_set_idle(client);
	if (ret)
		return ret;

	ret = sensor_write_reg(client, ICM4260X_BLK_SEL_W, (addr >> 8) & 0xff);
	usleep_range(INV_ICM42607_BLK_SEL_WAIT_US,
			INV_ICM42607_BLK_SEL_WAIT_US + 1);
	if (ret)
		goto restore_bank;

	ret = sensor_write_reg(client, ICM4260X_MADDR_W, addr & 0xff);
	usleep_range(INV_ICM42607_MADDR_WAIT_US,
			INV_ICM42607_MADDR_WAIT_US + 1);
	if (ret)
		goto restore_bank;

	ret = sensor_write_reg(client, ICM4260X_M_W, data);
	usleep_range(INV_ICM42607_M_RW_WAIT_US,
			INV_ICM42607_M_RW_WAIT_US + 1);
	if (ret)
		goto restore_bank;

restore_bank:
	ret |= sensor_write_reg(client, ICM4260X_BLK_SEL_W, 0);
	usleep_range(INV_ICM42607_BLK_SEL_WAIT_US,
			INV_ICM42607_BLK_SEL_WAIT_US + 1);

	ret |= sensor_write_reg(client, ICM4260X_PWR_MGMT_0, reg_pwr_mgmt_0);

	return ret;
}

/*
 * OTP reload procedure.
 */
static int icm4260x_otp_reload(struct i2c_client *client)
{
	int ret;
	u8 rb = 0;

	/* set idle bit */
	ret = icm4260x_set_idle(client);
	if (ret)
		return ret;

	/* Set OTP_COPY_MODE to 2'b01 */
	ret = icm4260x_mreg_read(client, ICM4260X_OTP_CONFIG_MREG_TOP1, 1, &rb);
	if (ret)
		return ret;
	rb &= ~OTP_COPY_MODE_MASK;
	rb |= BIT_OTP_COPY_NORMAL;
	ret = icm4260x_mreg_single_write(client, ICM4260X_OTP_CONFIG_MREG_TOP1, rb);
	if (ret)
		return ret;

	/* set OTP_PWR_DOWN to 1'b0 and wait for 300us */
	ret = icm4260x_mreg_read(client, ICM4260X_OTP_CTRL7_MREG_OTP, 1, &rb);
	if (ret)
		return ret;
	rb &= ~BIT_OTP_PWR_DOWN;
	ret = icm4260x_mreg_single_write(client, ICM4260X_OTP_CTRL7_MREG_OTP, rb);
	if (ret)
		return ret;
	usleep_range(300, 400);

	/* set OTP_RELOAD to 1'b1 and wait for 280us */
	ret = icm4260x_mreg_read(client, ICM4260X_OTP_CTRL7_MREG_OTP, 1, &rb);
	if (ret)
		return ret;
	rb |= BIT_OTP_RELOAD;
	ret = icm4260x_mreg_single_write(client, ICM4260X_OTP_CTRL7_MREG_OTP, rb);
	if (ret)
		return ret;
	usleep_range(280, 380);

	return 0;
}

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	u8 status = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);

	if (!enable) {
		status = (0xff & ~BIT_ACCEL_MODE_MASK);
		sensor->ops->ctrl_data &= status;
	} else {
		status = BIT_ACCEL_MODE_LNM;
		sensor->ops->ctrl_data |= status;
		sensor->ops->ctrl_data &= ~BIT_IDLE;
	}

	result = sensor_write_reg(client, sensor->ops->ctrl_reg,
						sensor->ops->ctrl_data);
	if (result) {
		dev_err(&client->dev,
			"%s: fail to set pwr_mgmt0(%d)\n", __func__, result);
		return result;
	}
	usleep_range(250, 260);

	return result;
}

/*
 * write POR value
 */

static int icm4260x_set_default_register(struct i2c_client *client)
{
	int status = 0;

	status |= sensor_write_reg(client, ICM4260X_GYRO_CONFIG0, 0x06);
	status |= sensor_write_reg(client, ICM4260X_ACCEL_CONFIG0, 0x06);
	status |= sensor_write_reg(client, ICM4260X_APEX_CONFIG0, 0x08);
	status |= sensor_write_reg(client, ICM4260X_APEX_CONFIG1, 0x02);
	status |= sensor_write_reg(client, ICM4260X_WOM_CONFIG, 0);
	status |= sensor_write_reg(client, ICM4260X_FIFO_CONFIG1, 0x01);
	status |= sensor_write_reg(client, ICM4260X_FIFO_CONFIG2, 0);
	status |= sensor_write_reg(client, ICM4260X_FIFO_CONFIG3, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_FIFO_CONFIG5_MREG_TOP1, 0x20);
	status |= icm4260x_mreg_single_write(client, ICM4260X_ST_CONFIG_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_INT_SOURCE7_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_INT_SOURCE8_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_INT_SOURCE9_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_INT_SOURCE10_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_APEX_CONFIG2_MREG_TOP1, 0xA2);
	status |= icm4260x_mreg_single_write(client, ICM4260X_APEX_CONFIG3_MREG_TOP1, 0x85);
	status |= icm4260x_mreg_single_write(client, ICM4260X_APEX_CONFIG4_MREG_TOP1, 0x51);
	status |= icm4260x_mreg_single_write(client, ICM4260X_APEX_CONFIG5_MREG_TOP1, 0x80);
	status |= icm4260x_mreg_single_write(client, ICM4260X_APEX_CONFIG9_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_APEX_CONFIG10_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_APEX_CONFIG11_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_ACCEL_WOM_X_THR_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_ACCEL_WOM_Y_THR_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_ACCEL_WOM_Z_THR_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_GOS_USER0_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_GOS_USER1_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_GOS_USER2_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_GOS_USER3_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_GOS_USER4_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_GOS_USER5_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_GOS_USER6_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_GOS_USER7_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_GOS_USER8_MREG_TOP1, 0);
	status |= icm4260x_mreg_single_write(client, ICM4260X_APEX_CONFIG12_MREG_TOP1, 0);

	if (status)
		return -EIO;

	return 0;
}

static int sensor_init(struct i2c_client *client)
{
	int ret = 0;
	u8 device_id = 0, value = 0;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	device_id = sensor_read_reg(client, ICM4260X_WHO_AM_I);
	if (device_id != ICM42607_DEVICE_ID) {
		dev_err(&client->dev, "%s: check id err, read_id: %d\n",
			__func__, device_id);
		return -1;
	}

	ret = icm4260x_otp_reload(client);
	if (ret) {
		dev_err(&client->dev,
			"ICM4260X OTP reload error,ret: %d!\n", ret);
		return ret;
	}


	ret = icm4260x_set_default_register(client);
	if (ret) {
		dev_err(&client->dev,
			"set ICM4260X default_register error,ret: %d!\n", ret);
		return ret;
	}

	/* SPI or I2C only
	 * FIFO count  : byte mode, big endian
	 * sensor data : big endian
	 */
	value |= BIT_FIFO_COUNT_ENDIAN;
	value |= BIT_SENSOR_DATA_ENDIAN;
	ret = sensor_write_reg(client, ICM4260X_INTF_CONFIG0, value);
	if (ret)
		return ret;

	/* configure clock */
	value = BIT_CLK_SEL_PLL | BIT_I3C_SDR_EN | BIT_I3C_DDR_EN;
	ret = sensor_write_reg(client, ICM4260X_INTF_CONFIG1, value);
	if (ret)
		return ret;

	/* INT pin configuration */
	/*
	 * value = (INT_POLARITY << SHIFT_INT1_POLARITY) |
	 *	(INT_DRIVE_CIRCUIT << SHIFT_INT1_DRIVE_CIRCUIT) |
	 *	(INT_MODE << SHIFT_INT1_MODE);
	 * ret = sensor_write_reg(client, ICM4260X_INT_CONFIG_REG, value);
	 * if (ret)
	 *	return ret;
	 */

	/* disable sensors */
	ret = sensor_write_reg(client, ICM4260X_PWR_MGMT_0, 0);
	if (ret)
		return ret;

	/* set Full scale select for accelerometer UI interface output*/
	value = sensor_read_reg(client, ICM4260X_ACCEL_CONFIG0);
	value &= ~BIT_ACCEL_FSR;
	value |= ACCEL_FS_SEL << SHIFT_ACCEL_FS_SEL;
	ret = sensor_write_reg(client, ICM4260X_ACCEL_CONFIG0, value);
	if (ret)
		return ret;

	/* turn on accelerometer*/
	ret = sensor->ops->active(client, 0, sensor->pdata->poll_delay_ms);
	if (ret) {
		dev_err(&client->dev,
			"%s: fail to active sensor(%d)\n", __func__, ret);
		return ret;
	}

	return ret;
}

static int gsensor_report_value(struct i2c_client *client,
				struct sensor_axis *axis)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	if (sensor->status_cur == SENSOR_ON) {
		/* Report acceleration sensor information */
		input_report_abs(sensor->input_dev, ABS_X, axis->x);
		input_report_abs(sensor->input_dev, ABS_Y, axis->y);
		input_report_abs(sensor->input_dev, ABS_Z, axis->z);
		input_sync(sensor->input_dev);
	}

	return 0;
}

static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(client);
	struct sensor_platform_data *pdata = sensor->pdata;
	int ret = 0;
	short x, y, z;
	struct sensor_axis axis;
	u8 buffer[6] = {0};

	if (sensor->ops->read_len < 6) {
		dev_err(&client->dev, "%s: length is error, len = %d\n",
			__func__, sensor->ops->read_len);
		return -EINVAL;
	}

	/* Data bytes from hardware xH, xL, yH, yL, zH, zL */
	*buffer = sensor->ops->read_reg;
	ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
	if (ret < 0) {
		dev_err(&client->dev,
			"%s: read data failed, ret = %d\n", __func__, ret);
		return ret;
	}
	x = ((buffer[0] << 8) & 0xff00) + (buffer[1] & 0xFF);
	y = ((buffer[2] << 8) & 0xff00) + (buffer[3] & 0xFF);
	z = ((buffer[4] << 8) & 0xff00) + (buffer[5] & 0xFF);

	//printk("%s,x:%d, y:%d, z:%d\n", __func__, x, y, z);
	axis.x = (pdata->orientation[0]) * x + (pdata->orientation[1]) * y +
		 (pdata->orientation[2]) * z;
	axis.y = (pdata->orientation[3]) * x + (pdata->orientation[4]) * y +
		 (pdata->orientation[5]) * z;
	axis.z = (pdata->orientation[6]) * x + (pdata->orientation[7]) * y +
		 (pdata->orientation[8]) * z;

	gsensor_report_value(client, &axis);

	mutex_lock(&(sensor->data_mutex));
	sensor->axis = axis;
	mutex_unlock(&(sensor->data_mutex));

	return ret;
}

static struct sensor_operate gsensor_icm4260x_ops = {
	.name		= "icm4260x_acc",
	.type		= SENSOR_TYPE_ACCEL,
	.id_i2c		= ACCEL_ID_ICM4260X,
	.read_reg	= ICM4260X_ACCEL_DATA_X0,
	.read_len	= 6,
	.id_reg		= SENSOR_UNKNOW_DATA,
	.id_data	= SENSOR_UNKNOW_DATA,
	.precision	= ICM4260X_PRECISION,
	.ctrl_reg	= ICM4260X_PWR_MGMT_0,
	.int_status_reg = ICM4260X_INT_STATUS,
	.range		= {-32768, 32768},
	.trig		= IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
	.active		= sensor_active,
	.init		= sensor_init,
	.report		= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int gsensor_icm4260x_probe(struct i2c_client *client,
				 const struct i2c_device_id *devid)
{
	client->addr = ICM42607_ADDR;
	return sensor_register_device(client, NULL, devid, &gsensor_icm4260x_ops);
}

static int gsensor_icm4260x_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &gsensor_icm4260x_ops);
}

static const struct i2c_device_id gsensor_icm4260x_id[] = {
	{"icm42607_acc", ACCEL_ID_ICM4260X},
	{}
};

static struct i2c_driver gsensor_icm4260x_driver = {
	.probe = gsensor_icm4260x_probe,
	.remove = gsensor_icm4260x_remove,
	.shutdown = sensor_shutdown,
	.id_table = gsensor_icm4260x_id,
	.driver = {
		.name = "gsensor_icm4260x",
#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
#endif
	},
};

module_i2c_driver(gsensor_icm4260x_driver);

MODULE_AUTHOR("Wangqiang Guo <dave.wang@rock-chips.com>");
MODULE_DESCRIPTION("icm4260x_acc 3-Axis accelerometer driver");
MODULE_LICENSE("GPL");

