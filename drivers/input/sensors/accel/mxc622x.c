/* drivers/input/sensors/access/mxc6225.c
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
#include <linux/sensor-dev.h>


#define MXC6225_ENABLE		1

#define MXC6225_REG_DATA       0x00
#define MXC6225_REG_TILT        0x3
#define MXC6225_REG_SRST        0x4
#define MXC6225_REG_SPCNT       0x5
#define MXC6225_REG_INTSU      	0x6
#define MXC6225_REG_MODE        0x7
#define MXC6225_REG_SR          0x8
#define MXC6225_REG_PDET        0x9
#define MXC6225_REG_PD          0xa


#define MXC6225_RANGE			2000000//1500000

/* LIS3DH */
#define MXC6225_PRECISION       8 // 8bit data
#define MXC6225_BOUNDARY		(0x1 << (MXC6225_PRECISION - 1))
#define MXC6225_GRAVITY_STEP	15625 //	(MXC6225_RANGE / MXC6225_BOUNDARY)

#define MXC6225_COUNT_AVERAGE	2

struct sensor_axis_average {
		int x_average;
		int y_average;
		int z_average;
		int count;
};

static struct sensor_axis_average axis_average;

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
		status = MXC6225_ENABLE;	//mxc6225
		sensor->ops->ctrl_data |= status;	
	}
	else
	{
		status = ~MXC6225_ENABLE;	//mxc6225
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
	
	result = sensor->ops->active(client,0,0);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}
	
	sensor->status_cur = SENSOR_OFF;

	//DBG("%s:MXC6225_REG_TILT=0x%x\n",__func__,sensor_read_reg(client, MXC6225_REG_TILT));

	result = sensor_write_reg(client, MXC6225_REG_SR, (0x01<<5)| 0x02);	//32 Samples/Second Active and Auto-Sleep Mode
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	if(sensor->pdata->irq_enable)	//open interrupt
	{
		result = sensor_write_reg(client, MXC6225_REG_INTSU, 1<<4);//enable int,GINT=1
		if(result)
		{
			printk("%s:line=%d,error\n",__func__,__LINE__);
			return result;
		}
	}
	
	sensor->ops->ctrl_data = 1<<8;	//Interrupt output INT is push-pull
	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	
	memset(&axis_average, 0, sizeof(struct sensor_axis_average));

	return result;
}


static int sensor_convert_data(struct i2c_client *client, char high_byte, char low_byte)
{
    s64 result;
	//struct sensor_private_data *sensor =
	//    (struct sensor_private_data *) i2c_get_clientdata(client);	
	//int precision = sensor->ops->precision;
		
	result = (int)low_byte;
	result *= 256;

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

#define GSENSOR_MIN  		2
static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(client);	
	struct sensor_platform_data *pdata = sensor->pdata;
	int ret = 0;
	int x,y,z;
	struct sensor_axis axis;
	char buffer[3] = {0};	
	char value = 0;
	
	if(sensor->ops->read_len < 3)	//sensor->ops->read_len = 3
	{
		printk("%s:lenth is error,len=%d\n",__func__,sensor->ops->read_len);
		return -1;
	}
	
	memset(buffer, 0, 3);
	
	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */	
	do {
		*buffer = sensor->ops->read_reg;
		ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
		if (ret < 0)
		return ret;
	} while (0);


	//this gsensor need 6 bytes buffer
	x = sensor_convert_data(sensor->client, 0, buffer[0]);	//buffer[1]:high bit 
	y = sensor_convert_data(sensor->client, 0, buffer[1]);
	z = sensor_convert_data(sensor->client, 0, buffer[2]);		

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


struct sensor_operate gsensor_mxc6225_ops = {
	.name			= "mxc6225",
	.type			= SENSOR_TYPE_ACCEL,
	.id_i2c			= ACCEL_ID_MXC6225,
	.read_reg			= MXC6225_REG_DATA,
	.read_len			= 3,
	.id_reg			= SENSOR_UNKNOW_DATA,
	.id_data			= SENSOR_UNKNOW_DATA,
	.precision			= MXC6225_PRECISION,
	.ctrl_reg			= MXC6225_REG_MODE,
	.int_status_reg	= SENSOR_UNKNOW_DATA,
	.range			= {-32768, 32768},
	.trig				= IRQF_TRIGGER_LOW | IRQF_ONESHOT,
	.active			= sensor_active,
	.init				= sensor_init,
	.report 			= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int gsensor_mxc6225_probe(struct i2c_client *client,
				const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &gsensor_mxc6225_ops);
}

static int gsensor_mxc6225_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &gsensor_mxc6225_ops);
}

static const struct i2c_device_id gsensor_mxc6225_id[] = {
	{"gs_mxc6225", ACCEL_ID_MXC6225},
	{}
};

static struct i2c_driver gsensor_mxc6225_driver = {
	.probe = gsensor_mxc6225_probe,
	.remove = gsensor_mxc6225_remove,
	.shutdown = sensor_shutdown,
	.id_table = gsensor_mxc6225_id,
	.driver = {
		.name = "gsensor_mxc6225",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(gsensor_mxc6225_driver);

MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_DESCRIPTION("mxc6225 3-Axis accelerometer driver");
MODULE_LICENSE("GPL");
