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
#include <linux/sensor-dev.h>


#define LIS3DH_INT_COUNT		(0x0E)
#define LIS3DH_WHO_AM_I			(0x0F)

/* full scale setting - register & mask */
#define LIS3DH_TEMP_CFG_REG		(0x1F)
#define LIS3DH_CTRL_REG1		(0x20)
#define LIS3DH_CTRL_REG2		(0x21)
#define LIS3DH_CTRL_REG3		(0x22)
#define LIS3DH_CTRL_REG4		(0x23)
#define LIS3DH_CTRL_REG5		(0x24)
#define LIS3DH_CTRL_REG6		(0x25)
#define LIS3DH_REFERENCE		(0x26)
#define LIS3DH_STATUS_REG		(0x27)
#define LIS3DH_OUT_X_L			(0x28)
#define LIS3DH_OUT_X_H			(0x29)
#define LIS3DH_OUT_Y_L			(0x2a)
#define LIS3DH_OUT_Y_H			(0x2b)
#define LIS3DH_OUT_Z_L			(0x2c)
#define LIS3DH_OUT_Z_H			(0x2d)
#define LIS3DH_FIFO_CTRL_REG		(0x2E)

#define LIS3DH_INT1_CFG			(0x30)
#define LIS3DH_INT1_SRC			(0x31)
#define LIS3DH_INT1_THS			(0x32)
#define LIS3DH_INT1_DURATION		(0x33)

#define LIS3DH_DEVID			(0x33)	//chip id
#define LIS3DH_ACC_DISABLE		(0x08)

#define LIS3DH_RANGE			2000000

/* LIS3DH */
#define LIS3DH_PRECISION		16
#define LIS3DH_BOUNDARY			(0x1 << (LIS3DH_PRECISION - 1))
#define LIS3DH_GRAVITY_STEP		(LIS3DH_RANGE / LIS3DH_BOUNDARY)

#define ODR1				0x10  /* 1Hz output data rate */
#define ODR10				0x20  /* 10Hz output data rate */
#define ODR25				0x30  /* 25Hz output data rate */
#define ODR50				0x40  /* 50Hz output data rate */
#define ODR100				0x50  /* 100Hz output data rate */
#define ODR200				0x60  /* 200Hz output data rate */
#define ODR400				0x70  /* 400Hz output data rate */
#define ODR1250				0x90  /* 1250Hz output data rate */



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
		status = LIS3DH_ACC_DISABLE;	//lis3dh	
		sensor->ops->ctrl_data |= status;	
	}
	else
	{
		status = ~LIS3DH_ACC_DISABLE;	//lis3dh
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
		{LIS3DH_CTRL_REG2,0X00},			
		{LIS3DH_CTRL_REG4,0x08},	//High resolution output mode: 1, Normal mode	
		{LIS3DH_CTRL_REG6,0x40},	
		{LIS3DH_TEMP_CFG_REG,0x00},	//
		{LIS3DH_FIFO_CTRL_REG,0x00},	//	
		{LIS3DH_INT1_CFG,0xFF},		//6 direction position recognition	
		{LIS3DH_INT1_THS,0x7F},		//Interrupt 1 threshold	
		{LIS3DH_INT1_DURATION,0x7F},	//Duration value 0x00->ox7f
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

		result = sensor_write_reg(client, LIS3DH_CTRL_REG3, 0x40);//I1_AOI1 =1  if motion	
		if(result)
		{
			printk("%s:line=%d,error\n",__func__,__LINE__);
			return result;
		}

		result = sensor_write_reg(client, LIS3DH_CTRL_REG5, 0x08);
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
	s64 result;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	/* int precision = sensor->ops->precision; */
	switch (sensor->devid) {	
		case LIS3DH_DEVID:		
			result = ((int)high_byte << 8) | (int)low_byte;
			if (result < LIS3DH_BOUNDARY)
				result = result * LIS3DH_GRAVITY_STEP;
			else
				result = ~(((~result & (0x7fff >> (16 - LIS3DH_PRECISION))) + 1)
						* LIS3DH_GRAVITY_STEP) + 1;
			break;

		default:
			printk(KERN_ERR "%s: devid wasn't set correctly\n",__func__);
			return -EFAULT;
    }

    return (int)result;
}

static int angle_report_value(struct i2c_client *client, struct sensor_axis *axis)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(client);	

	/* Report acceleration sensor information */
	input_report_abs(sensor->input_dev, ABS_X, axis->x);
	input_report_abs(sensor->input_dev, ABS_Y, axis->y);
	input_report_abs(sensor->input_dev, ABS_Z, axis->z);
	input_sync(sensor->input_dev);
	DBG("Gsensor x==%d  y==%d z==%d\n",axis->x,axis->y,axis->z);

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

	value = sensor_read_reg(client, LIS3DH_STATUS_REG);
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

	//this angle need 6 bytes buffer
	x = sensor_convert_data(sensor->client, buffer[1], buffer[0]);	//buffer[1]:high bit 
	y = sensor_convert_data(sensor->client, buffer[3], buffer[2]);
	z = sensor_convert_data(sensor->client, buffer[5], buffer[4]);		

	axis.x = (pdata->orientation[0])*x + (pdata->orientation[1])*y + (pdata->orientation[2])*z;
	axis.y = (pdata->orientation[3])*x + (pdata->orientation[4])*y + (pdata->orientation[5])*z;	
	axis.z = (pdata->orientation[6])*x + (pdata->orientation[7])*y + (pdata->orientation[8])*z;

	DBG( "%s: axis = %d  %d  %d \n", __func__, axis.x, axis.y, axis.z);

	//Report event  only while value is changed to save some power
	if((abs(sensor->axis.x - axis.x) > GSENSOR_MIN) || (abs(sensor->axis.y - axis.y) > GSENSOR_MIN) || (abs(sensor->axis.z - axis.z) > GSENSOR_MIN))
	{
		angle_report_value(client, &axis);

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

struct sensor_operate angle_lis3dh_ops = {
	.name				= "angle_lis3dh",
	.type				= SENSOR_TYPE_ANGLE,		//sensor type and it should be correct
	.id_i2c				= ANGLE_ID_LIS3DH,		//i2c id number
	.read_reg			= (LIS3DH_OUT_X_L | 0x80),	//read data
	.read_len			= 6,				//data length
	.id_reg				= LIS3DH_WHO_AM_I,		//read device id from this register
	.id_data 			= LIS3DH_DEVID,			//device id
	.precision			= LIS3DH_PRECISION,		//12 bits
	.ctrl_reg 			= LIS3DH_CTRL_REG1,		//enable or disable 
	.int_status_reg 		= LIS3DH_INT1_SRC,		//intterupt status register
	.range				= {-LIS3DH_RANGE,LIS3DH_RANGE},	//range
	.trig				= (IRQF_TRIGGER_LOW|IRQF_ONESHOT),		
	.active				= sensor_active,	
	.init				= sensor_init,
	.report				= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int angle_lis3dh_probe(struct i2c_client *client,
			      const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &angle_lis3dh_ops);
}

static int angle_lis3dh_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &angle_lis3dh_ops);
}

static const struct i2c_device_id angle_lis3dh_id[] = {
	{"angle_lis3dh", ANGLE_ID_LIS3DH},
	{}
};

static struct i2c_driver angle_lis3dh_driver = {
	.probe = angle_lis3dh_probe,
	.remove = angle_lis3dh_remove,
	.shutdown = sensor_shutdown,
	.id_table = angle_lis3dh_id,
	.driver = {
		.name = "angle_lis3dh",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(angle_lis3dh_driver);

MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_DESCRIPTION("lis3dh angle driver");
MODULE_LICENSE("GPL");


