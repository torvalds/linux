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
#include <linux/of_gpio.h> 
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/sensor-dev.h>

#define LENSFACTOR 1000

/* SMBus ARA Address */
#define	CM3218_ADDR_ARA			0x0C

/* CM3218 CMD Registers */
#define	CM3218_REG_ADDR_CMD		0x00
#define	CM3218_CMD_ALS_SD		0x0001
#define	CM3218_CMD_ALS_INT_EN		0x0002
#define	CM3218_CMD_ALS_IT_SHIFT		6
#define	CM3218_CMD_ALS_IT_MASK		(3 << CM3218_CMD_ALS_IT_SHIFT)
#define	CM3218_CMD_ALS_IT_05T		(0 << CM3218_CMD_ALS_IT_SHIFT)
#define	CM3218_CMD_ALS_IT_1T		(1 << CM3218_CMD_ALS_IT_SHIFT)
#define	CM3218_CMD_ALS_IT_2T		(2 << CM3218_CMD_ALS_IT_SHIFT)
#define	CM3218_CMD_ALS_IT_4T		(3 << CM3218_CMD_ALS_IT_SHIFT)
#define	CM3218_DEFAULT_CMD		(CM3218_CMD_ALS_IT_1T)

#define	CM3218_REG_ADDR_ALS_WH		0x01
#define	CM3218_DEFAULT_ALS_WH		0xFFFF

#define	CM3218_REG_ADDR_ALS_WL		0x02
#define	CM3218_DEFAULT_ALS_WL		0x0000

#define	CM3218_REG_ADDR_ALS		0x04

#define	CM3218_REG_ADDR_STATUS		0x06

#define	CM3218_REG_ADDR_ID		0x07

/* Software parameters */
#define	CM3218_MAX_CACHE_REGS		(0x03+1)	/* Reg.0x00 to 0x03 */


static int cm3218_read_ara(struct i2c_client *client)
{
	int status;
	unsigned short addr;

	addr = client->addr;
	client->addr = CM3218_ADDR_ARA;
	status = i2c_smbus_read_byte(client);
	client->addr = addr;

	if (status < 0)
		return -ENODEV;

	return 0;
}

static int cm3218_write(struct i2c_client *client, u8 reg, u16 value)
{
	u16 regval;
	int ret;

	dev_dbg(&client->dev,
		"Write to device register 0x%02X with 0x%04X\n", reg, value);
	regval = cpu_to_le16(value);
	ret = i2c_smbus_write_word_data(client, reg, regval);
	if (ret) {
		dev_err(&client->dev, "Write to device fails: 0x%x\n", ret);
	} else {
		
	}

	return ret;
}

static int cm3218_read(struct i2c_client *client, u8 reg)
{
	int regval;
	int status;
	
	status = i2c_smbus_read_word_data(client, reg);
	if (status < 0) {
		dev_err(&client->dev,
			"Error in reading Reg.0x%02X\n", reg);
		return status;
	}
	regval = le16_to_cpu(status);

	dev_dbg(&client->dev,
		"Read from device register 0x%02X = 0x%04X\n",
		reg, regval);

	return regval;
}

/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	int result = 0;
	int status = 0;
	
	sensor->client->addr = sensor->ops->ctrl_reg;	
	sensor->ops->ctrl_data = cm3218_read(client,sensor->client->addr);
	
	//register setting according to chip datasheet		
	if(!enable)
	{	
		status = CM3218_CMD_ALS_SD;	//cm3218	
		sensor->ops->ctrl_data |= status;	
	}
	else
	{
		status = ~CM3218_CMD_ALS_SD;	//cm3218
		sensor->ops->ctrl_data &= status;
	}

	DBG("%s:reg=0x%x,reg_ctrl=0x%x,enable=%d\n",__func__,sensor->ops->ctrl_reg, sensor->ops->ctrl_data, enable);
	result = cm3218_write(client,sensor->client->addr, sensor->ops->ctrl_data);
	if(result)
		printk("%s:fail to active sensor\n",__func__);
	
	return result;

}


static int sensor_init(struct i2c_client *client)
{	
	int status, i;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	for (i = 0; i < 5; i++) {
		status = cm3218_write(client, CM3218_REG_ADDR_CMD,
				CM3218_CMD_ALS_SD);
		if (status >= 0)
			break;
		cm3218_read_ara(client);
	}

	status = cm3218_write(client, CM3218_REG_ADDR_CMD, CM3218_DEFAULT_CMD);
	if (status < 0) {
		dev_err(&client->dev, "Init CM3218 CMD fails\n");
		return status;
	}

	if(sensor->pdata->irq_enable)
	{
		status = cm3218_write(client, CM3218_REG_ADDR_CMD, CM3218_DEFAULT_CMD | CM3218_CMD_ALS_INT_EN);
		if (status < 0) {
			dev_err(&client->dev, "Init CM3218 CMD fails\n");
			return status;
		}
	}

	/* Clean interrupt status */
	cm3218_read(client, CM3218_REG_ADDR_STATUS);
	
	return status;
}


static int light_report_value(struct input_dev *input, int data)
{
	unsigned char index = 0;
	
	if(data <= 700){
		index = 0;goto report;
	}
	else if(data <= 1400){
		index = 1;goto report;
	}
	else if(data <= 2800){
		index = 2;goto report;
	}
	else if(data <= 5600){
		index = 3;goto report;
	}
	else if(data <= 11200){
		index = 4;goto report;
	}
	else if(data <= 22400){
		index = 5;goto report;
	}
	else if(data <= 44800){
		index = 6;goto report;
	}
	else{
		index = 7;goto report;
	}

report:
	input_report_abs(input, ABS_MISC, index);
	input_sync(input);

	return index;
}

static int cm3218_read_lux(struct i2c_client *client, int *lux)
{
	int lux_data;

	lux_data = cm3218_read(client, CM3218_REG_ADDR_ALS);
	if (lux_data < 0) {
		dev_err(&client->dev, "Error in reading Lux DATA\n");
		return lux_data;
	}

	dev_vdbg(&client->dev, "lux = %u\n", lux_data);

	if (lux_data < 0)
		return lux_data;

	*lux  = lux_data * LENSFACTOR;
	*lux /= 1000;
	return 0;
}

static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	int result = 0;
	int index = 0;

	cm3218_read_lux(client,&result);
	
	index = light_report_value(sensor->input_dev, result);
	DBG("%s:%s result=0x%x,index=%d\n",__func__,sensor->ops->name, result,index);
	
	if((sensor->pdata->irq_enable)&& (sensor->ops->int_status_reg >= 0))	//read sensor intterupt status register
	{
		
		result= sensor_read_reg(client, sensor->ops->int_status_reg);
		if(result)
		{
			printk("%s:fail to clear sensor int status,ret=0x%x\n",__func__,result);
		}
	}
	
	return result;
}


struct sensor_operate light_cm3218_ops = {
	.name				= "cm3218",
	.type				= SENSOR_TYPE_LIGHT,	//sensor type and it should be correct
	.id_i2c				= LIGHT_ID_CM3218,	//i2c id number
	.read_reg			= CM3218_REG_ADDR_ALS,	//read data
	.read_len			= 2,			//data length
	.id_reg				= SENSOR_UNKNOW_DATA,	//read device id from this register
	.id_data 			= SENSOR_UNKNOW_DATA,	//device id
	.precision			= 16,			//8 bits
	.ctrl_reg 			= CM3218_REG_ADDR_CMD,	//enable or disable 
	.int_status_reg 		= CM3218_REG_ADDR_STATUS,	//intterupt status register
	.range				= {0,65535},		//range
	.brightness                                        ={10,255},                          // brightness
	.trig				= SENSOR_UNKNOW_DATA,		
	.active				= sensor_active,	
	.init				= sensor_init,
	.report				= sensor_report_value,
};

/****************operate according to sensor chip:end************/

//function name should not be changed
static struct sensor_operate *light_get_ops(void)
{
	return &light_cm3218_ops;
}


static int __init light_cm3218_init(void)
{
	struct sensor_operate *ops = light_get_ops();
	int result = 0;
	int type = ops->type;
	result = sensor_register_slave(type, NULL, NULL, light_get_ops);
	return result;
}

static void __exit light_cm3218_exit(void)
{
	struct sensor_operate *ops = light_get_ops();
	int type = ops->type;
	sensor_unregister_slave(type, NULL, NULL, light_get_ops);
}


module_init(light_cm3218_init);
module_exit(light_cm3218_exit);


