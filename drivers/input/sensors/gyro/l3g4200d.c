/* drivers/input/sensors/access/kxtik.c
 *
 * Copyright (C) 2012-2015 ROCKCHIP.
 * Author: luowei <lw@rock-chips.com>
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
#include <linux/l3g4200d.h>
#include <linux/sensor-dev.h>


#define L3G4200D_ENABLE			0x08

/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int status = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);

	//register setting according to chip datasheet
	if(enable)
	{
		status = L3G4200D_ENABLE;	//l3g4200d
		sensor->ops->ctrl_data |= status;
	}
	else
	{
		status = ~L3G4200D_ENABLE;	//l3g4200d
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
	unsigned char buf[5];
	unsigned char data = 0;
	int i = 0;

	result = sensor->ops->active(client,0,0);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	sensor->status_cur = SENSOR_OFF;

	buf[0] = 0x07;	//27
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x20;	//0x00
	buf[4] = 0x00;
	for(i=0; i<5; i++)
	{
		result = sensor_write_reg(client, sensor->ops->ctrl_reg+i, buf[i]);
		if(result)
		{
			printk("%s:line=%d,error\n",__func__,__LINE__);
			return result;
		}
	}

	result = sensor_read_reg(client, sensor->ops->ctrl_reg);
	if (result >= 0)
		data = result & 0x000F;

	sensor->ops->ctrl_data = data + ODR100_BW12_5;
	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	return result;
}


static int gyro_report_value(struct i2c_client *client, struct sensor_axis *axis)
{
	struct sensor_private_data *sensor =
	    	(struct sensor_private_data *) i2c_get_clientdata(client);

	if (sensor->status_cur == SENSOR_ON) {
		/* Report GYRO  information */
		input_report_rel(sensor->input_dev, ABS_RX, axis->x);
		input_report_rel(sensor->input_dev, ABS_RY, axis->y);
		input_report_rel(sensor->input_dev, ABS_RZ, axis->z);
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
	int x = 0, y = 0, z = 0;
	struct sensor_axis axis;
	char buffer[6] = {0};
	int i = 0;
	int value = 0;

	if(sensor->ops->read_len < 6)	//sensor->ops->read_len = 6
	{
		printk("%s:lenth is error,len=%d\n",__func__,sensor->ops->read_len);
		return -1;
	}

	memset(buffer, 0, 6);
#if 0
	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	do {
		buffer[0] = sensor->ops->read_reg;
		ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
		if (ret < 0)
		return ret;
	} while (0);
#else

	for(i=0; i<6; i++)
	{
		//buffer[i] = sensor->ops->read_reg + i;
		buffer[i] = sensor_read_reg(client, sensor->ops->read_reg + i);
	}
#endif
	x = (short) (((buffer[1]) << 8) | buffer[0]);
	y = (short) (((buffer[3]) << 8) | buffer[2]);
	z = (short) (((buffer[5]) << 8) | buffer[4]);

	DBG("%s: x=%d  y=%d z=%d \n",__func__, x,y,z);
	if (pdata)
	{
		axis.x = (pdata->orientation[0])*x + (pdata->orientation[1])*y + (pdata->orientation[2])*z;
		axis.y = (pdata->orientation[3])*x + (pdata->orientation[4])*y + (pdata->orientation[5])*z;
		axis.z = (pdata->orientation[6])*x + (pdata->orientation[7])*y + (pdata->orientation[8])*z;
	}
	else
	{
		axis.x = x;
		axis.y = y;
		axis.z = z;
	}

	gyro_report_value(client, &axis);

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


struct sensor_operate gyro_l3g4200d_ops = {
	.name			= "l3g4200d",
	.type			= SENSOR_TYPE_GYROSCOPE,
	.id_i2c			= GYRO_ID_L3G4200D,
	.read_reg			= GYRO_DATA_REG,
	.read_len			= 6,
	.id_reg			= GYRO_WHO_AM_I,
	.id_data			= GYRO_DEVID_L3G4200D,
	.precision			= 16,
	.ctrl_reg			= GYRO_CTRL_REG1,
	.int_status_reg	= GYRO_INT_SRC,
	.range			= {-32768, 32768},
	.trig				= IRQF_TRIGGER_LOW | IRQF_ONESHOT,
	.active			= sensor_active,
	.init				= sensor_init,
	.report			= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int gyro_l3g4200d_probe(struct i2c_client *client,
			       const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &gyro_l3g4200d_ops);
}

static int gyro_l3g4200d_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &gyro_l3g4200d_ops);
}

static const struct i2c_device_id gyro_l3g4200d_id[] = {
	{"l3g4200d_gyro", GYRO_ID_L3G4200D},
	{}
};

static struct i2c_driver gyro_l3g4200d_driver = {
	.probe = gyro_l3g4200d_probe,
	.remove = gyro_l3g4200d_remove,
	.shutdown = sensor_shutdown,
	.id_table = gyro_l3g4200d_id,
	.driver = {
		.name = "gyro_l3g4200d",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(gyro_l3g4200d_driver);

MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_DESCRIPTION("l3g4200d 3-Axis Gyroscope driver");
MODULE_LICENSE("GPL");
