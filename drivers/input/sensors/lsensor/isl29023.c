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


#define ISL29023_REG_ADD_COMMAND1	0x00
#define COMMMAND1_OPMODE_SHIFT		5
#define COMMMAND1_OPMODE_MASK		(7 << COMMMAND1_OPMODE_SHIFT)
#define COMMMAND1_OPMODE_POWER_DOWN	(0 << COMMMAND1_OPMODE_SHIFT)
#define COMMMAND1_OPMODE_ALS_ONCE	(1 << COMMMAND1_OPMODE_SHIFT)
#define COMMMAND1_OPMODE_IR_ONCE	(2 << COMMMAND1_OPMODE_SHIFT)
#define COMMMAND1_OPMODE_ALS_CONTINUE	(5 << COMMMAND1_OPMODE_SHIFT)
#define COMMMAND1_OPMODE_IR_CONTINUE	(6 << COMMMAND1_OPMODE_SHIFT)


#define ISL29023_REG_ADD_COMMANDII	0x01
#define COMMANDII_RESOLUTION_SHIFT	2
#define COMMANDII_RESOLUTION_65536	(0x0 << COMMANDII_RESOLUTION_SHIFT)
#define COMMANDII_RESOLUTION_4096	(0x1 << COMMANDII_RESOLUTION_SHIFT)
#define COMMANDII_RESOLUTION_256	(0x2 << COMMANDII_RESOLUTION_SHIFT)
#define COMMANDII_RESOLUTION_16		(0x3 << COMMANDII_RESOLUTION_SHIFT)
#define COMMANDII_RESOLUTION_MASK	(0x3 << COMMANDII_RESOLUTION_SHIFT)

#define COMMANDII_RANGE_SHIFT		0
#define COMMANDII_RANGE_1000		(0x0 << COMMANDII_RANGE_SHIFT)
#define COMMANDII_RANGE_4000		(0x1 << COMMANDII_RANGE_SHIFT)
#define COMMANDII_RANGE_16000		(0x2 << COMMANDII_RANGE_SHIFT)
#define COMMANDII_RANGE_64000		(0x3 << COMMANDII_RANGE_SHIFT)
#define COMMANDII_RANGE_MASK		(0x3 << COMMANDII_RANGE_SHIFT)


#define COMMANDII_RANGE_MASK		(0x3 << COMMANDII_RANGE_SHIFT)

#define COMMANDII_SCHEME_SHIFT		7
#define COMMANDII_SCHEME_MASK		(0x1 << COMMANDII_SCHEME_SHIFT)

#define ISL29023_REG_ADD_DATA_LSB	0x02
#define ISL29023_REG_ADD_DATA_MSB	0x03
#define ISL29023_MAX_REGS		ISL29023_REG_ADD_DATA_MSB

#define ISL29023_REG_LT_LSB		0x04
#define ISL29023_REG_LT_MSB		0x05
#define ISL29023_REG_HT_LSB		0x06
#define ISL29023_REG_HT_MSB		0x07


/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	//int status = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);

	//register setting according to chip datasheet
	if(enable)
	{
		sensor->ops->ctrl_data &= 0x1f;
		sensor->ops->ctrl_data |= COMMMAND1_OPMODE_ALS_CONTINUE;
	}
	else
	{
		sensor->ops->ctrl_data &= 0x1f;
		//sensor->ops->ctrl_data |= COMMMAND1_OPMODE_POWER_DOWN;
	}

	DBG("%s:reg=0x%x,reg_ctrl=0x%x,enable=%d\n",__func__,sensor->ops->ctrl_reg, sensor->ops->ctrl_data, enable);
	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if(result)
		printk("%s:fail to active sensor\n",__func__);

	if(enable)
	sensor->ops->report(sensor->client);

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

	result = sensor_write_reg(client, ISL29023_REG_ADD_COMMANDII, COMMANDII_RANGE_4000 | COMMANDII_RESOLUTION_4096);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	return result;
}


static int light_report_value(struct input_dev *input, int data)
{
	unsigned char index = 0;

	if(data <= 2){
		index = 0;goto report;
	}
	else if(data <= 3){
		index = 2;goto report;
	}
	else if(data <= 5){
		index = 3;goto report;
	}
	else if(data <= 8){
		index = 4;goto report;
	}
	else if(data <= 11){
		index = 5;goto report;
	}
	else if(data <= 14){
		index = 6;goto report;
	}
	else if(data <= 17){
		index = 7;goto report;
	}
	else{
		index = 7;goto report;
	}

report:
	input_report_abs(input, ABS_MISC, index);
	input_sync(input);

	return index;
}


static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int value = 0;
	char buffer[2] = {0};
	char index = 0;

	if(sensor->ops->read_len < 2)	//sensor->ops->read_len = 2
	{
		printk("%s:lenth is error,len=%d\n",__func__,sensor->ops->read_len);
		return -1;
	}

	memset(buffer, 0, 2);

	buffer[0] = sensor->ops->read_reg;
	result = sensor_rx_data(client, buffer, sensor->ops->read_len);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	value = (buffer[1] << 8) | buffer[0];


	index = light_report_value(sensor->input_dev, value);

	DBG("%s:%s result=0x%x,index=%d\n",__func__,sensor->ops->name, value,index);

	if(sensor->pdata->irq_enable)
	{
		if(sensor->ops->int_status_reg)
		{
			value = sensor_read_reg(client, sensor->ops->int_status_reg);
		}

	}


	return result;
}

static struct sensor_operate light_isl29023_ops = {
	.name				= "ls_isl29023",
	.type				= SENSOR_TYPE_LIGHT,	//sensor type and it should be correct
	.id_i2c				= LIGHT_ID_ISL29023,	//i2c id number
	.read_reg			= ISL29023_REG_ADD_DATA_LSB,		//read data
	.read_len			= 2,			//data length
	.id_reg				= SENSOR_UNKNOW_DATA,	//read device id from this register
	.id_data 			= SENSOR_UNKNOW_DATA,	//device id
	.precision			= 16,			//8 bits
	.ctrl_reg 			= ISL29023_REG_ADD_COMMAND1,		//enable or disable
	.int_status_reg 		= ISL29023_REG_ADD_COMMAND1,		//intterupt status register
	.range				= {100,65535},		//range
	.brightness                     ={10,255},     //brightness
	.trig				= IRQF_TRIGGER_LOW | IRQF_ONESHOT,
	.active				= sensor_active,
	.init				= sensor_init,
	.report				= sensor_report_value,
};

/****************operate according to sensor chip:end************/
static int light_isl29023_probe(struct i2c_client *client,
				const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &light_isl29023_ops);
}

static int light_isl29023_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &light_isl29023_ops);
}

static const struct i2c_device_id light_isl29023_id[] = {
	{"ls_isl29023", LIGHT_ID_ISL29023},
	{}
};

static struct i2c_driver light_isl29023_driver = {
	.probe = light_isl29023_probe,
	.remove = light_isl29023_remove,
	.shutdown = sensor_shutdown,
	.id_table = light_isl29023_id,
	.driver = {
		.name = "light_isl29023",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(light_isl29023_driver);

MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_DESCRIPTION("isl29023 light driver");
MODULE_LICENSE("GPL");
