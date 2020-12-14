/* drivers/input/sensors/access/dmard10.c
 *
 * Copyright (C) 2012-2015 ROCKCHIP.
 * Author: guoyi <gy@rock-chips.com>
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

/* Default register settings */
#define RBUFF_SIZE		12	/* Rx buffer size */

#define REG_ACTR 				0x00
#define REG_WDAL 				0x01
#define REG_TAPNS				0x0f
#define REG_MISC2				0x1f
#define REG_AFEM 				0x0c
#define REG_CKSEL 				0x0d
#define REG_INTC 				0x0e
#define REG_STADR 				0x12
#define REG_STAINT 				0x1C
#define REG_PD					0x21
#define REG_TCGYZ				0x26
#define REG_X_OUT 				0x41

#define MODE_Off				0x00
#define MODE_ResetAtOff			0x01
#define MODE_Standby			0x02
#define MODE_ResetAtStandby		0x03
#define MODE_Active				0x06
#define MODE_Trigger			0x0a
#define MODE_ReadOTP			0x12
#define MODE_WriteOTP			0x22
#define MODE_WriteOTPBuf		0x42
#define MODE_ResetDataPath		0x82

#define VALUE_STADR					0x55
#define VALUE_STAINT 				0xAA
#define VALUE_AFEM_AFEN_Normal		0x8f// AFEN set 1 , ATM[2:0]=b'000(normal),EN_Z/Y/X/T=1
#define VALUE_AFEM_Normal			0x0f// AFEN set 0 , ATM[2:0]=b'000(normal),EN_Z/Y/X/T=1
#define VALUE_INTC					0x00// INTC[6:5]=b'00
#define VALUE_INTC_Interrupt_En		0x20// INTC[6:5]=b'01 (Data ready interrupt enable, active high at INT0)
#define VALUE_CKSEL_ODR_0_204		0x04// ODR[3:0]=b'0000 (0.78125Hz), CCK[3:0]=b'0100 (204.8kHZ)
#define VALUE_CKSEL_ODR_1_204		0x14// ODR[3:0]=b'0001 (1.5625Hz), CCK[3:0]=b'0100 (204.8kHZ)
#define VALUE_CKSEL_ODR_3_204		0x24// ODR[3:0]=b'0010 (3.125Hz), CCK[3:0]=b'0100 (204.8kHZ)
#define VALUE_CKSEL_ODR_6_204		0x34// ODR[3:0]=b'0011 (6.25Hz), CCK[3:0]=b'0100 (204.8kHZ)
#define VALUE_CKSEL_ODR_12_204		0x44// ODR[3:0]=b'0100 (12.5Hz), CCK[3:0]=b'0100 (204.8kHZ)
#define VALUE_CKSEL_ODR_25_204		0x54// ODR[3:0]=b'0101 (25Hz), CCK[3:0]=b'0100 (204.8kHZ)
#define VALUE_CKSEL_ODR_50_204		0x64// ODR[3:0]=b'0110 (50Hz), CCK[3:0]=b'0100 (204.8kHZ)
#define VALUE_CKSEL_ODR_100_204		0x74// ODR[3:0]=b'0111 (100Hz), CCK[3:0]=b'0100 (204.8kHZ)

#define VALUE_TAPNS_NoFilter	0x00	// TAP1/TAP2	NO FILTER
#define VALUE_TAPNS_Ave_2		0x11	// TAP1/TAP2	Average 2
#define VALUE_TAPNS_Ave_4		0x22	// TAP1/TAP2	Average 4
#define VALUE_TAPNS_Ave_8		0x33	// TAP1/TAP2	Average 8
#define VALUE_TAPNS_Ave_16		0x44	// TAP1/TAP2	Average 16
#define VALUE_TAPNS_Ave_32		0x55	// TAP1/TAP2	Average 32
#define VALUE_MISC2_OSCA_EN		0x08
#define VALUE_PD_RST			0x52


//#define DMARD10_REG_INTSU        0x47
//#define DMARD10_REG_MODE        0x44
//#define DMARD10_REG_SR               0x44


#define DMARD10_REG_DS      0X49
#define DMARD10_REG_ID       0X0F
#define DMARD10_REG_IT       0X4D
#define DMARD10_REG_INTSRC1_C       0X4A
#define DMARD10_REG_INTSRC1_S       0X4B
#define MMAIO				0xA1

// IOCTLs for DMARD10 library
#define ECS_IOCTL_INIT                  _IO(MMAIO, 0x01)
#define ECS_IOCTL_RESET      	        _IO(MMAIO, 0x04)
#define ECS_IOCTL_CLOSE		        _IO(MMAIO, 0x02)
#define ECS_IOCTL_START		        _IO(MMAIO, 0x03)
#define ECS_IOCTL_GETDATA               _IOR(MMAIO, 0x08, char[RBUFF_SIZE+1])
#define SENSOR_CALIBRATION   		_IOWR(MMAIO, 0x05 , int[SENSOR_DATA_SIZE])

// IOCTLs for APPs
#define ECS_IOCTL_APP_SET_RATE		_IOW(MMAIO, 0x10, char)

 //rate
#define DMARD10_RATE_32         32
/*
#define DMARD10_RATE_64         64
#define DMARD10_RATE_120        128
#define DMARD10_RATE_MIN		DMARD10_RATE_1
#define DMARD10_RATE_MAX		DMARD10_RATE_120
*/
/*status*/
#define DMARD10_OPEN               1
#define DMARD10_CLOSE              0
#define DMARD10_NORMAL	      	   2
#define DMARD10_LOWPOWER  	   3



#define DMARD10_IIC_ADDR 	    0x18
#define DMARD10_REG_LEN         11


#define DMARD10_FATOR	15


#define DMARD10_X_OUT 		0x41
#define SENSOR_DATA_SIZE 3
#define DMARD10_SENSOR_RATE_1   0
#define DMARD10_SENSOR_RATE_2   1
#define DMARD10_SENSOR_RATE_3   2
#define DMARD10_SENSOR_RATE_4   3

#define POWER_OR_RATE 1
#define SW_RESET 1
#define DMARD10_INTERRUPUT 1
#define DMARD10_POWERDOWN 0
#define DMARD10_POWERON 1

//g-senor layout configuration, choose one of the following configuration

#define AVG_NUM 			16
#define SENSOR_DATA_SIZE 		3
#define DEFAULT_SENSITIVITY 		1024



#define DMARD10_ENABLE		1

#define DMARD10_REG_X_OUT       0x12
#define DMARD10_REG_Y_OUT       0x1
#define DMARD10_REG_Z_OUT       0x2
#define DMARD10_REG_TILT        0x3
#define DMARD10_REG_SRST        0x4
#define DMARD10_REG_SPCNT       0x5
#define DMARD10_REG_INTSU       0x6
#define DMARD10_REG_MODE        0x7
#define DMARD10_REG_SR          0x8
#define DMARD10_REG_PDET        0x9
#define DMARD10_REG_PD          0xa

#define DMARD10_RANGE			4000000
#define DMARD10_PRECISION       10
#define DMARD10_BOUNDARY        (0x1 << (DMARD10_PRECISION  - 1))
#define DMARD10_GRAVITY_STEP    (DMARD10_RANGE / DMARD10_BOUNDARY)


struct sensor_axis_average {
		int x_average;
		int y_average;
		int z_average;
		int count;
};

static struct sensor_axis_average axis_average;
int gsensor_reset(struct i2c_client *client){
	char buffer[7], buffer2[2];
	/* 1. check D10 , VALUE_STADR = 0x55 , VALUE_STAINT = 0xAA */
	buffer[0] = REG_STADR;
	buffer2[0] = REG_STAINT;

	sensor_rx_data(client, buffer, 2);
	sensor_rx_data(client, buffer2, 2);

	if( buffer[0] == VALUE_STADR || buffer2[0] == VALUE_STAINT){
		DBG(KERN_INFO " REG_STADR_VALUE = %d , REG_STAINT_VALUE = %d\n", buffer[0], buffer2[0]);
		DBG(KERN_INFO " %s DMT_DEVICE_NAME registered I2C driver!\n",__FUNCTION__);
	}
	else{
		DBG(KERN_INFO " %s gsensor I2C err @@@ REG_STADR_VALUE = %d , REG_STAINT_VALUE = %d \n", __func__, buffer[0], buffer2[0]);
		return -1;
	}
	/* 2. Powerdown reset */
	buffer[0] = REG_PD;
	buffer[1] = VALUE_PD_RST;
	sensor_tx_data(client, buffer, 2);
	/* 3. ACTR => Standby mode => Download OTP to parameter reg => Standby mode => Reset data path => Standby mode */
	buffer[0] = REG_ACTR;
	buffer[1] = MODE_Standby;
	buffer[2] = MODE_ReadOTP;
	buffer[3] = MODE_Standby;
	buffer[4] = MODE_ResetDataPath;
	buffer[5] = MODE_Standby;
	sensor_tx_data(client, buffer, 6);
	/* 4. OSCA_EN = 1 ,TSTO = b'000(INT1 = normal, TEST0 = normal) */
	buffer[0] = REG_MISC2;
	buffer[1] = VALUE_MISC2_OSCA_EN;
	sensor_tx_data(client, buffer, 2);
	/* 5. AFEN = 1(AFE will powerdown after ADC) */
	buffer[0] = REG_AFEM;
	buffer[1] = VALUE_AFEM_AFEN_Normal;
	buffer[2] = VALUE_CKSEL_ODR_100_204;
	buffer[3] = VALUE_INTC;
	buffer[4] = VALUE_TAPNS_Ave_2;
	buffer[5] = 0x00;	// DLYC, no delay timing
	buffer[6] = 0x07;	// INTD=1 (push-pull), INTA=1 (active high), AUTOT=1 (enable T)
	sensor_tx_data(client, buffer, 7);
	/* 6. write TCGYZ & TCGX */
	buffer[0] = REG_WDAL;	// REG:0x01
	buffer[1] = 0x00;		// set TC of Y,Z gain value
	buffer[2] = 0x00;		// set TC of X gain value
	buffer[3] = 0x03;		// Temperature coefficient of X,Y,Z gain
	sensor_tx_data(client, buffer, 4);

	buffer[0] = REG_ACTR;			// REG:0x00
	buffer[1] = MODE_Standby;		// Standby
	buffer[2] = MODE_WriteOTPBuf;	// WriteOTPBuf
	buffer[3] = MODE_Standby;		// Standby

	/* 7. Activation mode */
	buffer[0] = REG_ACTR;
	buffer[1] = MODE_Active;
	sensor_tx_data(client, buffer, 2);
	printk("\n dmard10 gsensor _reset SUCCESS!!\n");
	return 0;
}

/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int status = 0;
		gsensor_reset(client);
	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	//register setting according to chip datasheet
	if(enable)
	{
		status = DMARD10_ENABLE;	//dmard10
		sensor->ops->ctrl_data |= status;
	}
	else
	{
		status = ~DMARD10_ENABLE;	//dmard10
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

	DBG("%s:DMARD10_REG_TILT=0x%x\n",__func__,sensor_read_reg(client, DMARD10_REG_TILT));

	result = sensor_write_reg(client, DMARD10_REG_SR, (0x01<<5)| 0x02);	//32 Samples/Second Active and Auto-Sleep Mode
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	if(sensor->pdata->irq_enable)	//open interrupt
	{
		result = sensor_write_reg(client, DMARD10_REG_INTSU, 1<<4);//enable int,GINT=1
		if(result)
		{
			printk("%s:line=%d,error\n",__func__,__LINE__);
			return result;
		}
	}

	sensor->ops->ctrl_data = 1<<6;	//Interrupt output INT is push-pull
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

	result = ((int)high_byte << 8) | ((int)low_byte);

	return result * 128;
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

#define DMARD10_COUNT_AVERAGE 2
#define GSENSOR_MIN  		2
static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *) i2c_get_clientdata(client);
	struct sensor_platform_data *pdata = sensor->pdata;
	int ret = 0;
	int x,y,z;
	struct sensor_axis axis;
	char buffer[8] = {0};
	char value = 0;

	if(sensor->ops->read_len < 3)	//sensor->ops->read_len = 3
	{
		printk("%s:lenth is error,len=%d\n",__func__,sensor->ops->read_len);
		return -1;
	}

	memset(buffer, 0, 8);
	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	do {
		*buffer = sensor->ops->read_reg;
		ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
		if (ret < 0)
		return ret;
	} while (0);

	//this gsensor need 6 bytes buffer
	x = sensor_convert_data(sensor->client, buffer[3], buffer[2]);	//buffer[1]:high bit
	y = sensor_convert_data(sensor->client, buffer[5], buffer[4]);
	z = sensor_convert_data(sensor->client, buffer[7], buffer[6]);

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


struct sensor_operate gsensor_dmard10_ops = {
	.name			= "gs_dmard10",
	.type			= SENSOR_TYPE_ACCEL,
	.id_i2c			= ACCEL_ID_DMARD10,
	.read_reg			= DMARD10_REG_X_OUT,
	.read_len			= 8,
	.id_reg			= SENSOR_UNKNOW_DATA,
	.id_data			= SENSOR_UNKNOW_DATA,
	.precision			= DMARD10_PRECISION,
	.ctrl_reg			= DMARD10_REG_MODE,
	.int_status_reg	= SENSOR_UNKNOW_DATA,
	.range			= {-65536, 65536},
	.trig				= IRQF_TRIGGER_LOW | IRQF_ONESHOT,
	.active			= sensor_active,
	.init				= sensor_init,
	.report 			= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int gsensor_dmard10_probe(struct i2c_client *client,
				 const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &gsensor_dmard10_ops);
}

static int gsensor_dmard10_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &gsensor_dmard10_ops);
}

static const struct i2c_device_id gsensor_dmard10_id[] = {
	{"gs_dmard10", ACCEL_ID_DMARD10},
	{}
};

static struct i2c_driver gsensor_dmard10_driver = {
	.probe = gsensor_dmard10_probe,
	.remove = gsensor_dmard10_remove,
	.shutdown = sensor_shutdown,
	.id_table = gsensor_dmard10_id,
	.driver = {
		.name = "gsensor_dmard10",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(gsensor_dmard10_driver);

MODULE_AUTHOR("guoyi <gy@rock-chips.com>");
MODULE_DESCRIPTION("dmard10 3-Axis accelerometer driver");
MODULE_LICENSE("GPL");

