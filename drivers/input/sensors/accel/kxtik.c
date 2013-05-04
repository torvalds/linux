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
#include <linux/sensor-dev.h>

#define KXTIK_DEVID_1004		0x05	 //chip id
#define KXTIK_DEVID_J9_1005		0x07	 //chip id
#define KXTIK_DEVID_J2_1009		0x09	 //chip id
#define KXTIK_DEVID_1013        	0x11     //chip id
#define KXTIK_RANGE			2000000

#define KXTIK_XOUT_HPF_L                (0x00)	/* 0000 0000 */
#define KXTIK_XOUT_HPF_H                (0x01)	/* 0000 0001 */
#define KXTIK_YOUT_HPF_L                (0x02)	/* 0000 0010 */
#define KXTIK_YOUT_HPF_H                (0x03)	/* 0000 0011 */
#define KXTIK_ZOUT_HPF_L                (0x04)	/* 0001 0100 */
#define KXTIK_ZOUT_HPF_H                (0x05)	/* 0001 0101 */
#define KXTIK_XOUT_L                    (0x06)	/* 0000 0110 */
#define KXTIK_XOUT_H                    (0x07)	/* 0000 0111 */
#define KXTIK_YOUT_L                    (0x08)	/* 0000 1000 */
#define KXTIK_YOUT_H                    (0x09)	/* 0000 1001 */
#define KXTIK_ZOUT_L                    (0x0A)	/* 0001 1010 */
#define KXTIK_ZOUT_H                    (0x0B)	/* 0001 1011 */
#define KXTIK_ST_RESP                   (0x0C)	/* 0000 1100 */
#define KXTIK_WHO_AM_I                  (0x0F)	/* 0000 1111 */
#define KXTIK_TILT_POS_CUR              (0x10)	/* 0001 0000 */
#define KXTIK_TILT_POS_PRE              (0x11)	/* 0001 0001 */
#define KXTIK_INT_SRC_REG1              (0x15)	/* 0001 0101 */
#define KXTIK_INT_SRC_REG2              (0x16)	/* 0001 0110 */
#define KXTIK_STATUS_REG                (0x18)	/* 0001 1000 */
#define KXTIK_INT_REL                   (0x1A)	/* 0001 1010 */
#define KXTIK_CTRL_REG1                 (0x1B)	/* 0001 1011 */
#define KXTIK_CTRL_REG2                 (0x1C)	/* 0001 1100 */
#define KXTIK_CTRL_REG3                 (0x1D)	/* 0001 1101 */
#define KXTIK_INT_CTRL_REG1             (0x1E)	/* 0001 1110 */
#define KXTIK_INT_CTRL_REG2             (0x1F)	/* 0001 1111 */
#define KXTIK_INT_CTRL_REG3             (0x20)	/* 0010 0000 */
#define KXTIK_DATA_CTRL_REG             (0x21)	/* 0010 0001 */
#define KXTIK_TILT_TIMER                (0x28)	/* 0010 1000 */
#define KXTIK_WUF_TIMER                 (0x29)	/* 0010 1001 */
#define KXTIK_TDT_TIMER                 (0x2B)	/* 0010 1011 */
#define KXTIK_TDT_H_THRESH              (0x2C)	/* 0010 1100 */
#define KXTIK_TDT_L_THRESH              (0x2D)	/* 0010 1101 */
#define KXTIK_TDT_TAP_TIMER             (0x2E)	/* 0010 1110 */
#define KXTIK_TDT_TOTAL_TIMER           (0x2F)	/* 0010 1111 */
#define KXTIK_TDT_LATENCY_TIMER         (0x30)	/* 0011 0000 */
#define KXTIK_TDT_WINDOW_TIMER          (0x31)	/* 0011 0001 */
#define KXTIK_WUF_THRESH                (0x5A)	/* 0101 1010 */
#define KXTIK_TILT_ANGLE                (0x5C)	/* 0101 1100 */
#define KXTIK_HYST_SET                  (0x5F)	/* 0101 1111 */

/* CONTROL REGISTER 1 BITS */
#define KXTIK_DISABLE			0x7F
#define KXTIK_ENABLE			(1 << 7)
/* INPUT_ABS CONSTANTS */
#define FUZZ			3
#define FLAT			3
/* RESUME STATE INDICES */
#define RES_DATA_CTRL		0
#define RES_CTRL_REG1		1
#define RES_INT_CTRL1		2
#define RESUME_ENTRIES		3

/* CTRL_REG1: set resolution, g-range, data ready enable */
/* Output resolution: 8-bit valid or 12-bit valid */
#define KXTIK_RES_8BIT		0
#define KXTIK_RES_12BIT		(1 << 6)
/* Output g-range: +/-2g, 4g, or 8g */
#define KXTIK_G_2G		0
#define KXTIK_G_4G		(1 << 3)
#define KXTIK_G_8G		(1 << 4)

/* DATA_CTRL_REG: controls the output data rate of the part */
#define KXTIK_ODR12_5F		0
#define KXTIK_ODR25F			1
#define KXTIK_ODR50F			2
#define KXTIK_ODR100F			3
#define KXTIK_ODR200F			4
#define KXTIK_ODR400F			5
#define KXTIK_ODR800F			6

/* kxtik */
#define KXTIK_PRECISION       12
#define KXTIK_BOUNDARY        (0x1 << (KXTIK_PRECISION - 1))
#define KXTIK_GRAVITY_STEP    KXTIK_RANGE / KXTIK_BOUNDARY


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
		status = KXTIK_ENABLE;	//kxtik	
		sensor->ops->ctrl_data |= status;	
	}
	else
	{
		status = ~KXTIK_ENABLE;	//kxtik
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
	int i = 0;	
	unsigned char id_reg = KXTIK_WHO_AM_I;
	unsigned char id_data = 0;
	
	result = sensor->ops->active(client,0,0);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}
	
	sensor->status_cur = SENSOR_OFF;

	for(i=0; i<3; i++)
	{
		result = sensor_rx_data(client, &id_reg, 1);
		id_data = id_reg;
		if(!result)
		break;
	}

	if(result)
	{
		printk("%s:fail to read id,result=%d\n",__func__, result);
		return result;
	}

	sensor->devid = id_data;
	
	result = sensor_write_reg(client, KXTIK_DATA_CTRL_REG, KXTIK_ODR400F);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	if(sensor->pdata->irq_enable)	//open interrupt
	{

		if (id_data == KXTIK_DEVID_1004)
		result = sensor_write_reg(client, KXTIK_INT_CTRL_REG1, 0x34);//enable int,active high,need read INT_REL
		else
		result = sensor_write_reg(client, KXTIK_INT_CTRL_REG1, 0x30);//enable int,active high,need read INT_REL
		
		if(result)
		{
			printk("%s:line=%d,error\n",__func__,__LINE__);
			return result;
		}
	}
	
	sensor->ops->ctrl_data = (KXTIK_RES_12BIT | KXTIK_G_2G);
	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	printk("%s:%s id=0x%x\n",__func__,sensor->ops->name, id_data);
	return result;
}

static int sensor_convert_data(struct i2c_client *client, char high_byte, char low_byte)
{
    s64 result;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	//int precision = sensor->ops->precision;
	switch (sensor->devid) {	
		case KXTIK_DEVID_1004:	
		case KXTIK_DEVID_1013:
		case KXTIK_DEVID_J9_1005:
		case KXTIK_DEVID_J2_1009:
			result = (((int)high_byte << 8) | ((int)low_byte ))>>4;
			if (result < KXTIK_BOUNDARY)
       			result = result* KXTIK_GRAVITY_STEP;
    		else
       			result = ~( ((~result & (0x7fff>>(16-KXTIK_PRECISION)) ) + 1) 
			   			* KXTIK_GRAVITY_STEP) + 1;
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

	DBG( "%s: axis = %d  %d  %d \n", __func__, axis.x, axis.y, axis.z);

	//Report event  only while value is changed to save some power
	if((abs(sensor->axis.x - axis.x) > GSENSOR_MIN) || (abs(sensor->axis.y - axis.y) > GSENSOR_MIN) || (abs(sensor->axis.z - axis.z) > GSENSOR_MIN))
	{
		gsensor_report_value(client, &axis);

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

struct sensor_operate gsensor_kxtik_ops = {
	.name				= "kxtik",
	.type				= SENSOR_TYPE_ACCEL,		//sensor type and it should be correct
	.id_i2c				= ACCEL_ID_KXTIK,		//i2c id number
	.read_reg			= KXTIK_XOUT_L,			//read data
	.read_len			= 6,				//data length
	.id_reg				= SENSOR_UNKNOW_DATA,		//read device id from this register
	.id_data			= SENSOR_UNKNOW_DATA,
	.precision			= KXTIK_PRECISION,		//12 bits
	.ctrl_reg 			= KXTIK_CTRL_REG1,		//enable or disable 
	.int_status_reg 		= KXTIK_INT_REL,		//intterupt status register
	.range				= {-KXTIK_RANGE,KXTIK_RANGE},	//range
	.trig				= IRQF_TRIGGER_LOW|IRQF_ONESHOT,		
	.active				= sensor_active,	
	.init				= sensor_init,
	.report				= sensor_report_value,
};

/****************operate according to sensor chip:end************/

//function name should not be changed
static struct sensor_operate *gsensor_get_ops(void)
{
	return &gsensor_kxtik_ops;
}


static int __init gsensor_kxtik_init(void)
{
	struct sensor_operate *ops = gsensor_get_ops();
	int result = 0;
	int type = ops->type;
	result = sensor_register_slave(type, NULL, NULL, gsensor_get_ops);
	return result;
}

static void __exit gsensor_kxtik_exit(void)
{
	struct sensor_operate *ops = gsensor_get_ops();
	int type = ops->type;
	sensor_unregister_slave(type, NULL, NULL, gsensor_get_ops);
}


module_init(gsensor_kxtik_init);
module_exit(gsensor_kxtik_exit);

