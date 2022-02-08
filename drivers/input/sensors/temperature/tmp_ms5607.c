/* drivers/input/sensors/temperature/tmp_ms5607.c
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


#define CMD_RESET   0x1E  // ADC reset command
#define CMD_ADC_READ 0x00  // ADC read command
#define CMD_ADC_CONV 0x40  // ADC conversion command
#define CMD_ADC_D1   0x00    // ADC D1 conversion
#define CMD_ADC_D2   0x10    // ADC D2 conversion
#define CMD_ADC_256  0x00    // ADC OSR=256
#define CMD_ADC_512  0x02    // ADC OSR=512
#define CMD_ADC_1024 0x04    // ADC OSR=1024
#define CMD_ADC_2048 0x06    // ADC OSR=2048
#define CMD_ADC_4096 0x08    // ADC OSR=4096
#define CMD_PROM_RD  0xA0  // Prom read command

#if defined(CONFIG_PR_MS5607)
extern int g_ms5607_temp;
extern int g_ms5607_pr_status;
#else
static int g_ms5607_temp = 0;
static int g_ms5607_pr_status = SENSOR_OFF;
#endif

int g_ms5607_temp_status;
static int C[8] = {0};

/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	int result = 0;
	int i = 0;
	char prom[16];

	if((enable)&&(g_ms5607_pr_status == SENSOR_OFF))
	{
		result = sensor_write_reg_normal(client, CMD_RESET);
		if(result)
		printk("%s:line=%d,error\n",__func__,__LINE__);

		//Read PROM (128 bit of calibration words)
		memset(prom, 0, 16);
		prom[0]= CMD_PROM_RD;//CMD_PROM_RD;
		for(i=0; i<8; i++)
		{
			prom[i*2]= CMD_PROM_RD + i*2;
			result = sensor_rx_data(client, &prom[i*2], 2);
			if(result)
			{
				printk("%s:line=%d,error\n",__func__,__LINE__);
				return result;
			}
		}

		for (i=0;i<8;i++)
		{
			C[i] = prom[2*i] << 8 | prom[2*i + 1];
			//printk("prom[%d]=0x%x,prom[%d]=0x%x",2*i,prom[2*i],(2*i + 1),prom[2*i + 1]);
			//printk("\nC[%d]=%d,",i+1,C[i]);
		}

	}

	g_ms5607_temp_status = enable;

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
	g_ms5607_temp_status = sensor->status_cur;
	//Reset
	//result = sensor_write_reg_normal(client, CMD_RESET);
	//if(result)
	//printk("%s:line=%d,error\n",__func__,__LINE__);

	return result;
}



static int temperature_report_value(struct input_dev *input, int data)
{
	//get temperature, high and temperature from register data

	input_report_abs(input, ABS_THROTTLE, data);
	input_sync(input);

	return 0;
}


static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	int result = 0;
	char buffer[3];
	char index = 0;
	unsigned int  D1=0, D2=0;

	int T2 = 0;
	long long OFF = 0;	// offset at actual temperature
	long long SENS = 0;	// sensitivity at actual temperature
	int dT = 0;		// difference between actual and measured temperature
	long long OFF2 = 0;
	long long SENS2 = 0;
	int P = 0;		// compensated pressure value


	memset(buffer, 0, 3);
	if(sensor->ops->read_len < 3)	//sensor->ops->read_len = 3
	{
		printk("%s:lenth is error,len=%d\n",__func__,sensor->ops->read_len);
		return -1;
	}

	if(g_ms5607_pr_status == 	SENSOR_OFF)
	{

		//D1 conversion
		sensor_write_reg_normal(client,  CMD_ADC_CONV + CMD_ADC_D1 + CMD_ADC_4096);
		msleep(10);

		memset(buffer, 0, 3);
		buffer[0] = CMD_ADC_READ;
		result = sensor_rx_data(client, &buffer[0], 3);
		if(result)
		{
			printk("%s:line=%d,error\n",__func__,__LINE__);
			return result;
		}

		D1 = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
		DBG("\nD1=%d :buffer[0]=0x%x,buffer[1]=0x%x,buffer2]=0x%x\n",D1,buffer[0],buffer[1],buffer[2]);

		//D2 conversion
		sensor_write_reg_normal(client,  CMD_ADC_CONV + CMD_ADC_D2 + CMD_ADC_4096);
		msleep(10);

		memset(buffer, 0, 3);
		buffer[0] = CMD_ADC_READ;
		result = sensor_rx_data(client, &buffer[0], 3);
		if(result)
		{
			printk("%s:line=%d,error\n",__func__,__LINE__);
			return result;
		}

		D2 = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
		DBG("D2=%d:buffer[0]=0x%x,buffer[1]=0x%x,buffer2]=0x%x\n",D2,buffer[0],buffer[1],buffer[2]);

		dT = D2 - ((unsigned int)C[5] << 8);

		g_ms5607_temp = (int)(2000 + ((long long)dT * C[6] >> 23));

		OFF = ((unsigned long long)C[2] << 17) + (C[4] * (long long)dT >> 6);

		SENS = ((long long)C[1] << 16) + (C[3] * (long long)dT >> 7);

		/*calcualte 2nd order pressure and temperature (BP5607 2nd order algorithm)*/
		if (g_ms5607_temp < -4000 || g_ms5607_temp > 8500)
		{
			printk("%s:temperature is error\n",__func__);
			return -1;
		}

		if (g_ms5607_temp < 2000)
		{
			int tmp;
			tmp = (g_ms5607_temp - 2000) * (g_ms5607_temp - 2000);

			T2 = (int)((long long)(dT * dT) >> 31);
			OFF2 = (((long long)tmp * 61)*((long long)tmp * 61)) >> 4;
			SENS2 = (long long)((tmp*tmp) << 1);

			if (g_ms5607_temp < -1500)
			{
				tmp = (g_ms5607_temp + 1500) * (g_ms5607_temp + 1500);
				OFF2 += 15 * tmp;
				SENS2 += 8 * tmp;
			}
		}
		else
		{
			T2=0;
			OFF2 = 0;
			SENS2 = 0;
		}

		g_ms5607_temp -= T2;
		OFF -= OFF2;
		SENS -= SENS2;
		P = (int)((((D1 * SENS) >> 21) - OFF) >> 15);

		index = temperature_report_value(sensor->input_dev, g_ms5607_temp);

		DBG("%s:pressure=%d,temperature=%d\n",__func__,P,g_ms5607_temp);

	}
	else
	{
		index = temperature_report_value(sensor->input_dev, g_ms5607_temp);

		#if defined(CONFIG_PR_MS5607)
		DBG("%s:pressure=%d,temperature=%d\n",__func__,P,g_ms5607_temp);
		#else
		printk("%s:errror,need pr_ms5607\n",__func__);
		#endif
	}


	return result;
}


static struct sensor_operate temperature_ms5607_ops = {
	.name				= "tmp_ms5607",
	.type				= SENSOR_TYPE_TEMPERATURE,	//sensor type and it should be correct
	.id_i2c				= TEMPERATURE_ID_MS5607,	//i2c id number
	.read_reg			= SENSOR_UNKNOW_DATA,	//read data
	.read_len			= 3,			//data length
	.id_reg				= SENSOR_UNKNOW_DATA,	//read device id from this register
	.id_data 			= SENSOR_UNKNOW_DATA,	//device id
	.precision			= 24,			//8 bits
	.ctrl_reg 			= SENSOR_UNKNOW_DATA,	//enable or disable
	.int_status_reg 		= SENSOR_UNKNOW_DATA,	//intterupt status register
	.range				= {100,65535},		//range
	.trig				= IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_SHARED,
	.active				= sensor_active,
	.init				= sensor_init,
	.report				= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int temperature_ms5607_probe(struct i2c_client *client, const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &temperature_ms5607_ops);
}

static int temperature_ms5607_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &temperature_ms5607_ops);
}

static const struct i2c_device_id temperature_ms5607_id[] = {
	{"tmp_ms5607", TEMPERATURE_ID_MS5607},
	{}
};

static struct i2c_driver temperature_ms5607_driver = {
	.probe = temperature_ms5607_probe,
	.remove = temperature_ms5607_remove,
	.shutdown = sensor_shutdown,
	.id_table = temperature_ms5607_id,
	.driver = {
		.name = "temperature_ms5607",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(temperature_ms5607_driver);

MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_DESCRIPTION("ms5607 temperature driver");
MODULE_LICENSE("GPL");
