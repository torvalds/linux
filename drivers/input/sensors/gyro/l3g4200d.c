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
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <mach/gpio.h>
#include <mach/board.h> 
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/l3g4200d.h>
#include <linux/sensor-dev.h>

#if 0
#define SENSOR_DEBUG_TYPE SENSOR_TYPE_GYROSCOPE
#define DBG(x...) if(sensor->pdata->type == SENSOR_DEBUG_TYPE) printk(x)
#else
#define DBG(x...)
#endif

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

	/* Report GYRO  information */
	input_report_rel(sensor->input_dev, ABS_RX, axis->x);
	input_report_rel(sensor->input_dev, ABS_RY, axis->y);
	input_report_rel(sensor->input_dev, ABS_RZ, axis->z);
	input_sync(sensor->input_dev);
	DBG("gyro x==%d  y==%d z==%d\n",axis->x,axis->y,axis->z);

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
	if(pdata && pdata->orientation)
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

	//filter gyro data
	if((abs(axis.x) > pdata->x_min)||(abs(axis.y) > pdata->y_min)||(abs(axis.z) > pdata->z_min))
	{	
		gyro_report_value(client, &axis);	

		 /* »¥³âµØ»º´æÊý¾Ý. */
		mutex_lock(&(sensor->data_mutex) );
		sensor->axis = axis;
		mutex_unlock(&(sensor->data_mutex) );
	}	

	if((sensor->pdata->irq_enable)&& (sensor->ops->int_status_reg >= 0))	//read sensor intterupt status register
	{
		
		value = sensor_read_reg(client, sensor->ops->int_status_reg);
		DBG("%s:sensor int status :0x%x\n",__func__,value);
	}
	
	return ret;
}


struct sensor_operate gyro_ops = {
	.name				= "l3g4200d",
	.type				= SENSOR_TYPE_GYROSCOPE,//sensor type and it should be correct
	.id_i2c				= GYRO_ID_L3G4200D,		//i2c id number
	.read_reg			= GYRO_DATA_REG,		//read data
	.read_len			= 6,				//data length
	.id_reg				= GYRO_WHO_AM_I,		//read device id from this register
	.id_data 			= GYRO_DEVID_L3G4200D,		//device id
	.precision			= 8,				//8 bits
	.ctrl_reg 			= GYRO_CTRL_REG1,		//enable or disable 
	.int_status_reg 		= GYRO_INT_SRC,			//intterupt status register,if no exist then -1
	.range				= {-32768,32768},		//range
	.trig				= IRQF_TRIGGER_LOW|IRQF_ONESHOT,		
	.active				= sensor_active,	
	.init				= sensor_init,
	.report				= sensor_report_value,
};

/****************operate according to sensor chip:end************/

//function name should not be changed
struct sensor_operate *gyro_get_ops(void)
{
	return &gyro_ops;
}

EXPORT_SYMBOL(gyro_get_ops);

static int __init gyro_init(void)
{
	struct sensor_operate *ops = gyro_get_ops();
	int result = 0;
	int type = ops->type;
	result = sensor_register_slave(type, NULL, NULL, gyro_get_ops);
	printk("%s\n",__func__);
	return result;
}

static void __exit gyro_exit(void)
{
	struct sensor_operate *ops = gyro_get_ops();
	int type = ops->type;
	sensor_unregister_slave(type, NULL, NULL, gyro_get_ops);
}


module_init(gyro_init);
module_exit(gyro_exit);


