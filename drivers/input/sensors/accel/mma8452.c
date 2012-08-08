/* drivers/input/sensors/access/mma8452.c
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
#include <linux/mma8452.h>
#include <linux/sensor-dev.h>

#if 0
#define SENSOR_DEBUG_TYPE SENSOR_TYPE_ACCEL
#define DBG(x...) if(sensor->pdata->type == SENSOR_DEBUG_TYPE) printk(x)
#else
#define DBG(x...)
#endif


#define MMA8451_DEVID		0x1a
#define MMA8452_DEVID		0x2a
#define MMA8453_DEVID		0x3a

#define MMA8452_ENABLE		1


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
		status = MMA8452_ENABLE;	//mma8452
		sensor->ops->ctrl_data |= status;	
	}
	else
	{
		status = ~MMA8452_ENABLE;	//mma8452
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
	int tmp;
	int ret = 0;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	
	ret = sensor->ops->active(client,0,0);
	if(ret)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return ret;
	}
	
	sensor->status_cur = SENSOR_OFF;

	/* disable FIFO  FMODE = 0*/
	ret = sensor_write_reg(client,MMA8452_REG_F_SETUP,0);
	DBG("%s: MMA8452_REG_F_SETUP:%x\n",__func__, sensor_read_reg(client,MMA8452_REG_F_SETUP));

	/* set full scale range to 2g */
	ret = sensor_write_reg(client,MMA8452_REG_XYZ_DATA_CFG,0);
	DBG("%s: MMA8452_REG_XYZ_DATA_CFG:%x\n",__func__, sensor_read_reg(client,MMA8452_REG_XYZ_DATA_CFG));

	/* set bus 8bit/14bit(FREAD = 1,FMODE = 0) ,data rate*/
	tmp = (MMA8452_RATE_12P5<< MMA8452_RATE_SHIFT) | FREAD_MASK;
	ret = sensor_write_reg(client,MMA8452_REG_CTRL_REG1,tmp);

	sensor->ops->ctrl_data = tmp;
	
	DBG("mma8452 MMA8452_REG_CTRL_REG1:%x\n",sensor_read_reg(client,MMA8452_REG_CTRL_REG1));
	
	DBG("mma8452 MMA8452_REG_SYSMOD:%x\n",sensor_read_reg(client,MMA8452_REG_SYSMOD));

	ret = sensor_write_reg(client,MMA8452_REG_CTRL_REG3,5);
	DBG("mma8452 MMA8452_REG_CTRL_REG3:%x\n",sensor_read_reg(client,MMA8452_REG_CTRL_REG3));
	
	ret = sensor_write_reg(client,MMA8452_REG_CTRL_REG4,1);
	DBG("mma8452 MMA8452_REG_CTRL_REG4:%x\n",sensor_read_reg(client,MMA8452_REG_CTRL_REG4));

	ret = sensor_write_reg(client,MMA8452_REG_CTRL_REG5,1);
	DBG("mma8452 MMA8452_REG_CTRL_REG5:%x\n",sensor_read_reg(client,MMA8452_REG_CTRL_REG5));	

	DBG("mma8452 MMA8452_REG_SYSMOD:%x\n",sensor_read_reg(client,MMA8452_REG_SYSMOD));

	return ret;
}

static int sensor_convert_data(struct i2c_client *client, char high_byte, char low_byte)
{
    s64 result;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	//int precision = sensor->ops->precision;
	switch (sensor->devid) {
		case MMA8451_DEVID:	
			swap(high_byte,low_byte);
			result = ((int)high_byte << (MMA8451_PRECISION-8)) 
					| ((int)low_byte >> (16-MMA8451_PRECISION));
			if (result < MMA8451_BOUNDARY)
				result = result* MMA8451_GRAVITY_STEP;
			else
				result = ~( ((~result & (0x7fff>>(16-MMA8451_PRECISION)) ) + 1) 
						* MMA8451_GRAVITY_STEP) + 1;
			break;

		case MMA8452_DEVID:			
			swap(high_byte,low_byte);
			result = ((int)high_byte << (MMA8452_PRECISION-8)) 
					| ((int)low_byte >> (16-MMA8452_PRECISION));
			if (result < MMA8452_BOUNDARY)
				result = result* MMA8452_GRAVITY_STEP;
			else
				result = ~( ((~result & (0x7fff>>(16-MMA8452_PRECISION)) ) + 1) 
						* MMA8452_GRAVITY_STEP) + 1;
			break;
			
		case MMA8453_DEVID:		
			swap(high_byte,low_byte);
			result = ((int)high_byte << (MMA8453_PRECISION-8)) 
					| ((int)low_byte >> (16-MMA8453_PRECISION));
			if (result < MMA8453_BOUNDARY)
				result = result* MMA8453_GRAVITY_STEP;
			else
				result = ~( ((~result & (0x7fff>>(16-MMA8453_PRECISION)) ) + 1) 
						* MMA8453_GRAVITY_STEP) + 1;
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

#define GSENSOR_MIN  		10
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

	//Report event only while value is changed to save some power
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


struct sensor_operate gsensor_mma8452_ops = {
	.name				= "mma8452",
	.type				= SENSOR_TYPE_ACCEL,			//sensor type and it should be correct
	.id_i2c				= ACCEL_ID_MMA845X,			//i2c id number
	.read_reg			= MMA8452_REG_X_OUT_MSB,		//read data
	.read_len			= 6,					//data length
	.id_reg				= MMA8452_REG_WHO_AM_I,			//read device id from this register
	.id_data 			= MMA8452_DEVID,			//device id
	.precision			= MMA8452_PRECISION,			//12 bit
	.ctrl_reg 			= MMA8452_REG_CTRL_REG1,		//enable or disable 	
	.int_status_reg 		= MMA8452_REG_INTSRC,			//intterupt status register
	.range				= {-MMA845X_RANGE,MMA845X_RANGE},	//range
	.trig				= IRQF_TRIGGER_LOW|IRQF_ONESHOT,		
	.active				= sensor_active,	
	.init				= sensor_init,
	.report 			= sensor_report_value,
};

/****************operate according to sensor chip:end************/

//function name should not be changed
static struct sensor_operate *gsensor_get_ops(void)
{
	return &gsensor_mma8452_ops;
}


static int __init gsensor_mma8452_init(void)
{
	struct sensor_operate *ops = gsensor_get_ops();
	int result = 0;
	int type = ops->type;
	result = sensor_register_slave(type, NULL, NULL, gsensor_get_ops);	
	printk("%s\n",__func__);
	return result;
}

static void __exit gsensor_mma8452_exit(void)
{
	struct sensor_operate *ops = gsensor_get_ops();
	int type = ops->type;
	sensor_unregister_slave(type, NULL, NULL, gsensor_get_ops);
}


module_init(gsensor_mma8452_init);
module_exit(gsensor_mma8452_exit);



