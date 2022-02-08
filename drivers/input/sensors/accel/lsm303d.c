/* drivers/input/sensors/access/kxtik.c
 *
 * Copyright (C) 2012-2015 ROCKCHIP.
 * Author: Bruins <xwj@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/of_gpio.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/sensor-dev.h>


#define LSM303D_WHO_AM_I		(0x0F)

/* full scale setting - register & mask */
#define LSM303D_CTRL_REG0		(0x1F)
#define LSM303D_CTRL_REG1		(0x20)
#define LSM303D_CTRL_REG2		(0x21)
#define LSM303D_CTRL_REG3		(0x22)
#define LSM303D_CTRL_REG4		(0x23)
#define LSM303D_CTRL_REG5		(0x24)
#define LSM303D_CTRL_REG6		(0x25)
#define LSM303D_CTRL_REG7		(0x26)
#define LSM303D_STATUS_REG		(0x27)
#define LSM303D_OUT_X_L			(0x28)
#define LSM303D_OUT_X_H			(0x29)
#define LSM303D_OUT_Y_L			(0x2a)
#define LSM303D_OUT_Y_H			(0x2b)
#define LSM303D_OUT_Z_L			(0x2c)
#define LSM303D_OUT_Z_H			(0x2d)
#define LSM303D_FIFO_CTRL_REG		(0x2E)
#define LSM303D_FIFO_SRC_REG		(0X2F)

#define LSM303D_IG_CFG1			(0x30)
#define LSM303D_IG_SRC1			(0x31)
#define LSM303D_IG_THS1			(0x32)
#define LSM303D_IG_DURATION1		(0x33)

#define LSM303D_IG_CFG2			(0x34)
#define LSM303D_IG_SRC2			(0x35)
#define LSM303D_IG_THS2			(0x36)
#define LSM303D_IG_DURATION2		(0x37)


#define LSM303D_DEVID			(0x49)	//chip id
#define LSM303D_ACC_DISABLE		(0x08)

#define LSM303D_RANGE			32768

/* LSM303D */
#define LSM303D_PRECISION		16
#define LSM303D_BOUNDARY			(0x1 << (LSM303D_PRECISION - 1))
#define LSM303D_GRAVITY_STEP		(LSM303D_RANGE / LSM303D_BOUNDARY)

#define ODR3P25				0x10  /* 3.25Hz output data rate */
#define ODR6P25				0x20  /* 6.25Hz output data rate */
#define ODR12P5				0x30  /* 12.5Hz output data rate */
#define ODR25				0x40  /* 25Hz output data rate */
#define ODR50				0x50  /* 50Hz output data rate */
#define ODR100				0x60  /* 100Hz output data rate */
#define ODR200				0x70  /* 200Hz output data rate */
#define ODR400				0x80  /* 400Hz output data rate */
#define ODR800				0x90  /* 800Hz output data rate */
#define ODR1600				0xA0  /* 1600Hz output data rate */


struct sensor_reg_data {
	char reg;
	char data;
};

/****************operate according to sensor chip:start************/
static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int status = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);

	sensor->ops->ctrl_data |= ODR100;	//100HZ,if 0 then power down

	//register setting according to chip datasheet
	if(!enable)
	{
		status = LSM303D_ACC_DISABLE;	//lis3dh
		sensor->ops->ctrl_data |= status;
	}
	else
	{
		status = ~LSM303D_ACC_DISABLE;	//lis3dh
		sensor->ops->ctrl_data &= status;
	}

	DBG("%s:reg=0x%x,reg_ctrl=0x%x,enable=%d\n",__func__,sensor->ops->ctrl_reg, sensor->ops->ctrl_data, enable);
	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if(result)
		printk("%s:fail to active sensor\n",__func__);

	return result;

}


static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int i;

	struct sensor_reg_data reg_data[] =
	{
		{LSM303D_CTRL_REG0,0x00},
		{LSM303D_CTRL_REG1,0x07},
		{LSM303D_CTRL_REG2,0x00},
		{LSM303D_CTRL_REG3,0x00},
		{LSM303D_CTRL_REG4,0x00},
		{LSM303D_CTRL_REG5,0x78},		//High resolution output mode:11,
		{LSM303D_CTRL_REG6,0x20},
		{LSM303D_CTRL_REG7,0x00},
		{LSM303D_FIFO_CTRL_REG,0x00},
		{LSM303D_IG_CFG1,0xFF},		//6 direction position recognition
		{LSM303D_IG_THS1,0x7F},		//Interrupt 1 threshold
		{LSM303D_IG_DURATION1,0x7F},	//Duration value 0x00->ox7f

	/*
		{LSM303D_CTRL_REG7,0x00},
		{LSM303D_CTRL_REG4,0x08},		//High resolution output mode: 1, Normal mode
		{LSM303D_CTRL_REG6,0x40},

		{LSM303D_FIFO_CTRL_REG,0x00},	//
		{LSM303D_IG_CFG1,0xFF},			//6 direction position recognition
		{LSM303D_IG_THS1,0x7F},			//Interrupt 1 threshold
		{LSM303D_IG_DURATION1,0x7F},	//Duration value 0x00->ox7f
		*/
	};

	result = sensor->ops->active(client,0,0);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	sensor->status_cur = SENSOR_OFF;

	for(i=0;i<(sizeof(reg_data)/sizeof(struct sensor_reg_data));i++)
	{
		result = sensor_write_reg(client, reg_data[i].reg, reg_data[i].data);
		if(result)
		{
			printk("%s:line=%d,i=%d,error\n",__func__,__LINE__,i);
			return result;
		}
	}


	if(sensor->pdata->irq_enable)
	{

		result = sensor_write_reg(client, LSM303D_CTRL_REG3, 0x20);
		if(result)
		{
			printk("%s:line=%d,error\n",__func__,__LINE__);
			return result;
		}

		i = sensor_read_reg(client,LSM303D_CTRL_REG5);

		result = sensor_write_reg(client, LSM303D_CTRL_REG5, (i|0x01));
		if(result)
		{
			printk("%s:line=%d,error\n",__func__,__LINE__);
			return result;
		}

	}

	return result;
}


static int sensor_convert_data(struct i2c_client *client, char high_byte, char low_byte)
{
	int result;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	switch (sensor->devid) {
		case LSM303D_DEVID:
			result = ((int)high_byte << 8) | (int)low_byte;
			if (result < LSM303D_BOUNDARY)
				result = result * LSM303D_GRAVITY_STEP;
			else
				result = ~(((~result & (0x7fff >> (16 - LSM303D_PRECISION))) + 1)
						* LSM303D_GRAVITY_STEP) + 1;
			break;

		default:
			printk(KERN_ERR "%s: devid wasn't set correctly\n",__func__);
			return -EFAULT;
    }

    return (int)result;
}

static int gsensor_report_value(struct i2c_client *client, struct sensor_axis *axis)
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

#define GSENSOR_MIN  10
static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
			(struct sensor_private_data *) i2c_get_clientdata(client);
    	struct sensor_platform_data *pdata = sensor->pdata;
	int ret = 0;
	int x,y,z;
	struct sensor_axis axis;
	char buffer[6] = {0};
	char value = 0;

	if(sensor->ops->read_len < 6)	//sensor->ops->read_len = 6
	{
		printk("%s:lenth is error,len=%d\n",__func__,sensor->ops->read_len);
		return -1;
	}

	memset(buffer, 0, 6);

	value = sensor_read_reg(client, LSM303D_STATUS_REG);
	if((value & 0x0f) == 0)
	{
		printk("%s:line=%d,value=0x%x,data is not ready\n",__func__,__LINE__,value);
		return -1;
	}


	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	do {
		*buffer = sensor->ops->read_reg;
		ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
		if (ret < 0)
		return ret;
	} while (0);

	//this gsensor need 6 bytes buffer
	x = sensor_convert_data(sensor->client, buffer[1], buffer[0]);	//buffer[1]:high bit
	y = sensor_convert_data(sensor->client, buffer[3], buffer[2]);
	z = sensor_convert_data(sensor->client, buffer[5], buffer[4]);

	axis.x = (pdata->orientation[0])*x + (pdata->orientation[1])*y + (pdata->orientation[2])*z;
	axis.y = (pdata->orientation[3])*x + (pdata->orientation[4])*y + (pdata->orientation[5])*z;
	axis.z = (pdata->orientation[6])*x + (pdata->orientation[7])*y + (pdata->orientation[8])*z;

	gsensor_report_value(client, &axis);

	mutex_lock(&sensor->data_mutex);
	sensor->axis = axis;
	mutex_unlock(&sensor->data_mutex);

	if((sensor->pdata->irq_enable)&& (sensor->ops->int_status_reg >= 0))	//read sensor intterupt status register
	{

		value = sensor_read_reg(client, sensor->ops->int_status_reg);
		DBG("%s:sensor int status :0x%x\n",__func__,value);
	}

	return ret;
}

static struct sensor_operate gsensor_lsm303d_ops = {
	.name			= "lsm303d",
	.type			= SENSOR_TYPE_ACCEL,
	.id_i2c			= ACCEL_ID_LSM303D,
	.read_reg			= (LSM303D_OUT_X_L | 0x80),
	.read_len			= 6,
	.id_reg			= LSM303D_WHO_AM_I,
	.id_data			= LSM303D_DEVID,
	.precision			= LSM303D_PRECISION,
	.ctrl_reg			= LSM303D_CTRL_REG1,
	.int_status_reg	= LSM303D_IG_SRC1,
	.range			= {-LSM303D_RANGE, LSM303D_RANGE},
	.trig				= (IRQF_TRIGGER_LOW | IRQF_ONESHOT),
	.active			= sensor_active,
	.init				= sensor_init,
	.report			= sensor_report_value,
};


/****************operate according to sensor chip:end************/
static int gsensor_lsm303d_probe(struct i2c_client *client,
				const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &gsensor_lsm303d_ops);
}

static int gsensor_lsm303d_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &gsensor_lsm303d_ops);
}

static const struct i2c_device_id gsensor_lsm303d_id[] = {
	{"gs_lsm303d", ACCEL_ID_LSM303D},
	{}
};

static struct i2c_driver gsensor_lsm303d_driver = {
	.probe = gsensor_lsm303d_probe,
	.remove = gsensor_lsm303d_remove,
	.shutdown = sensor_shutdown,
	.id_table = gsensor_lsm303d_id,
	.driver = {
		.name = "gsensor_lsm303d",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(gsensor_lsm303d_driver);

MODULE_AUTHOR("xwj <xwj@rock-chips.com>");
MODULE_DESCRIPTION("lsm303d 3-Axis accelerometer driver");
MODULE_LICENSE("GPL");
