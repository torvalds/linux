/* drivers/input/touchscreen/goodix_touch.c
 *
 * Copyright (C) 2010 - 2011 Goodix, Inc.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <mach/gpio.h>
#include <linux/irq.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/completion.h>
#include <asm/uaccess.h>
#include <mach/board.h>

#define GOODIX_I2C_NAME "Goodix-TS"
//define default resolution of the touchscreen
#define GOODIX_MULTI_TOUCH
#define GT819_IIC_SPEED              400*1000    //400*1000
#define TOUCH_MAX_WIDTH              800
#define TOUCH_MAX_HEIGHT             480
#define TOUCH_MAJOR_MAX              200
#define WIDTH_MAJOR_MAX              200
#define MAX_POINT                    10
#define INT_TRIGGER_EDGE_RISING      0
#define INT_TRIGGER_EDGE_FALLING     1
#define INT_TRIGGER_EDGE_LOW         2
#define INT_TRIGGER_EDGE_HIGH        3
#define INT_TRIGGER                  INT_TRIGGER_EDGE_FALLING
#define I2C_DELAY                    0x0f
struct goodix_ts_data {
	struct workqueue_struct *goodix_wq;
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct work_struct  work;
	int irq;
	int irq_gpio;
	uint16_t abs_x_max;
	uint16_t abs_y_max;
	uint8_t max_touch_num;
	uint8_t int_trigger_type;
};

static const char *goodix_ts_name = "Goodix Capacitive TouchScreen";

static int gt819_read_regs(struct i2c_client *client, u8 reg, u8 buf[], unsigned len)
{
	int ret; 
	ret = i2c_master_reg8_recv(client, reg, buf, len, GT819_IIC_SPEED);
	return ret; 
}


static int gt819_set_regs(struct i2c_client *client, u8 reg, u8 const buf[], unsigned short len)
{
	int ret; 
	ret = i2c_master_reg8_send(client, reg, buf, (int)len, GT819_IIC_SPEED);
	return ret;
}

static void gt819_queue_work(struct work_struct *work)
{
	struct goodix_ts_data *ts = container_of(work, struct goodix_ts_data, work);
	uint8_t  point_data[53]={ 0 };
	int ret,i,offset,points;
	int x,y,w;
	
	ret = gt819_read_regs(ts->client,1, point_data, 1);
	if (ret < 0) {
		dev_err(&ts->client->dev, "i2c_read_bytes fail:%d!\n",ret);
		return;
	}
	
	points = point_data[0] & 0x1f;
	//dev_info(&ts->client->dev, "points = %d\n",points);
	if (points == 0) {
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0);
		//input_mt_sync(data->input_dev);
		input_sync(ts->input_dev);
		enable_irq(ts->irq);
		dev_info(&ts->client->dev, "touch release\n");
		return; 
	}
	for(i=0;0!=points;i++)
		points>>=1;
	points = i;
	ret = gt819_read_regs(ts->client,3, point_data, points*5);
	if (ret < 0) {
		dev_err(&ts->client->dev, "i2c_read_bytes fail:%d!\n",ret);
		return;
	}
	for(i=0;i<points;i++){
		offset = i*5;
		x = (((s16)(point_data[offset+0]))<<8) | ((s16)point_data[offset+1]);
		y = (((s16)(point_data[offset+2]))<<8) | ((s16)point_data[offset+3]);
		w = point_data[offset+4];
		//dev_info(&ts->client->dev, "goodix multiple report event[%d]:x = %d,y = %d,w = %d\n",i,x,y,w);
		if(x<=TOUCH_MAX_WIDTH && y<=TOUCH_MAX_HEIGHT){
			//dev_info(&ts->client->dev, "goodix multiple report event[%d]:x = %d,y = %d,w = %d\n",i,x,y,w);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, w);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X,  x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y,  y);
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, i);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, w);
			input_mt_sync(ts->input_dev);
		}
	}
	input_sync(ts->input_dev);
	enable_irq(ts->irq);
	return;
}

/*******************************************************
Description:
	External interrupt service routine.

Parameter:
	irq:	interrupt number.
	dev_id: private data pointer.
	
return:
	irq execute status.
*******************************************************/
static irqreturn_t gt819_irq_handler(int irq, void *dev_id)
{
	struct goodix_ts_data *ts = dev_id;

	disable_irq_nosync(ts->client->irq);
	queue_work(ts->goodix_wq, &ts->work);
	return IRQ_HANDLED;
}

static int gt819_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct goodix_platform_data *pdata = client->dev.platform_data;
	dev_info(&client->dev,"gt819_suspend\n");

	if (pdata->platform_sleep)                              
		pdata->platform_sleep();
	disable_irq(client->irq);
	return 0;
}

static int gt819_resume(struct i2c_client *client)
{
	struct goodix_platform_data *pdata = client->dev.platform_data;
	dev_info(&client->dev,"gt819_resume\n");

	enable_irq(client->irq);
	if (pdata->platform_wakeup)                              
		pdata->platform_wakeup();
	return 0;
}


/*******************************************************
Description:
	Goodix touchscreen driver release function.

Parameter:
	client:	i2c device struct.
	
return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int gt819_remove(struct i2c_client *client)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(client);
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
	remove_proc_entry("goodix-update", NULL);
#endif
	//goodix_debug_sysfs_deinit();
		gpio_direction_input(ts->irq_gpio);
		gpio_free(ts->irq_gpio);
		free_irq(client->irq, ts);
	if(ts->goodix_wq)
		destroy_workqueue(ts->goodix_wq); 
	dev_notice(&client->dev,"The driver is removing...\n");
	i2c_set_clientdata(client, NULL);
	input_unregister_device(ts->input_dev);
	kfree(ts);
	return 0;
}

static int gt819_init_panel(struct goodix_ts_data *ts)
{
	int ret,I2cDelay;
	uint8_t rd_cfg_buf[10];
	#if 0
	int i;
	uint8_t config_info[] = {
	0x02,(TOUCH_MAX_WIDTH>>8),(TOUCH_MAX_WIDTH&0xff),
	(TOUCH_MAX_HEIGHT>>8),(TOUCH_MAX_HEIGHT&0xff),MAX_POINT,(0xa0 | INT_TRIGGER),
	0x20,0x00,0x32,0x0f,0x20,0x08,0x14,0x00,
	0x00,0x20,0x00,0x00,0x88,0x88,0x88,0x00,0x37,0x00,0x00,0x00,0x01,0x02,0x03,0x04,
	0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0xff,0xff,0x00,0x01,0x02,0x03,0x04,
	0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0xff,0xff,0xff,0x00,0x00,0x3c,0x64,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00
	};
	ret = gt819_set_regs(ts->client, 101, config_info, sizeof(config_info));
	for (i=0; i<sizeof(config_info); i++) {
		//printk("buf[%d] = 0x%x \n", i, config_info[i]);
	}
	#endif
	ret = gt819_read_regs(ts->client, 101, rd_cfg_buf, 10);
	if (ret < 0)
		return ret;
	ts->abs_x_max = ((((uint16_t)rd_cfg_buf[1])<<8)|rd_cfg_buf[2]);
	ts->abs_y_max = ((((uint16_t)rd_cfg_buf[3])<<8)|rd_cfg_buf[4]);
	ts->max_touch_num = rd_cfg_buf[5];
	ts->int_trigger_type = rd_cfg_buf[6]&0x03;
	I2cDelay = rd_cfg_buf[9]&0x0f;
	dev_info(&ts->client->dev,"X_MAX = %d,Y_MAX = %d,MAX_TOUCH_NUM = %d,INT_TRIGGER = %d,I2cDelay = %x\n",
		ts->abs_x_max,ts->abs_y_max,ts->max_touch_num,ts->int_trigger_type,I2cDelay);
	if((ts->abs_x_max!=TOUCH_MAX_WIDTH)||(ts->abs_y_max!=TOUCH_MAX_HEIGHT)||
		(MAX_POINT!=ts->max_touch_num)||INT_TRIGGER!=ts->int_trigger_type || I2C_DELAY!=I2cDelay){
		ts->abs_x_max = TOUCH_MAX_WIDTH;
		ts->abs_y_max = TOUCH_MAX_HEIGHT;
		ts->max_touch_num = MAX_POINT;
		ts->int_trigger_type = INT_TRIGGER;
		rd_cfg_buf[1] = ts->abs_x_max>>8;
		rd_cfg_buf[2] = ts->abs_x_max&0xff;
		rd_cfg_buf[3] = ts->abs_y_max>>8;
		rd_cfg_buf[4] = ts->abs_y_max&0xff;
		rd_cfg_buf[5] = ts->max_touch_num;
		rd_cfg_buf[6] = ((rd_cfg_buf[6]&0xfc) | INT_TRIGGER);
		rd_cfg_buf[9] = ((rd_cfg_buf[9]&0xf0) | I2C_DELAY);
		ret = gt819_set_regs(ts->client, 101, rd_cfg_buf, 10);
		if (ret < 0)
			return ret;
		dev_info(&ts->client->dev,"set config\n");
	}
	return 0;
}

/*******************************************************
Description:
	Goodix touchscreen probe function.

Parameter:
	client:	i2c device struct.
	id:device id.
	
return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int gt819_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	struct goodix_ts_data *ts;
	struct goodix_platform_data *pdata = client->dev.platform_data;
	const char irq_table[4] = {IRQ_TYPE_EDGE_RISING,
							   IRQ_TYPE_EDGE_FALLING,
							   IRQ_TYPE_LEVEL_LOW,
							   IRQ_TYPE_LEVEL_HIGH};

	dev_info(&client->dev,"Install touch driver\n");

	if (!pdata) {
		dev_err(&client->dev, "platform data is required!\n");
		return -EINVAL;
	}
	
	if (pdata->init_platform_hw)
		pdata->init_platform_hw();

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
		return -ENODEV;
	}
	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		return -ENOMEM;
	}
	ts->client = client;

	ret = gt819_init_panel(ts);
	if(ret != 0){
	  dev_err(&client->dev,"init panel fail,ret = %d\n",ret);
	  goto err_init_panel_fail;
	}
	
	if (!client->irq){
		dev_err(&client->dev,"no irq fail\n");
		ret = -ENODEV;
		goto err_no_irq_fail;
	}
	ts->irq_gpio = client->irq;
	ts->irq = client->irq = gpio_to_irq(client->irq);
	ret  = request_irq(client->irq, gt819_irq_handler, irq_table[ts->int_trigger_type],client->name, ts);
	if (ret != 0) {
		dev_err(&client->dev,"request_irq fail:%d\n", ret);
		goto err_irq_request_fail;
	}
	
	ts->goodix_wq = create_workqueue("goodix_wq");
	if (!ts->goodix_wq) {
		printk(KERN_ALERT "creat workqueue faiked\n");
		ret = -ENOMEM;
		goto err_create_work_queue_fail;
	}
	//INIT_WORK(&ts->work, goodix_ts_work_func);
	INIT_WORK(&ts->work, gt819_queue_work);

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		dev_err(&client->dev,"Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}
	
	set_bit(ABS_MT_POSITION_X, ts->input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, ts->input_dev->absbit);
	set_bit(ABS_MT_TOUCH_MAJOR, ts->input_dev->absbit);
	set_bit(ABS_MT_TRACKING_ID, ts->input_dev->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, ts->input_dev->absbit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);
	input_set_abs_params(ts->input_dev,ABS_MT_POSITION_X, 0, TOUCH_MAX_WIDTH, 0, 0);
	input_set_abs_params(ts->input_dev,ABS_MT_POSITION_Y, 0, TOUCH_MAX_HEIGHT, 0, 0);
	input_set_abs_params(ts->input_dev,ABS_MT_TOUCH_MAJOR, 0, TOUCH_MAJOR_MAX, 0, 0);
	input_set_abs_params(ts->input_dev,ABS_MT_TRACKING_ID, 0, MAX_POINT, 0, 0);
	input_set_abs_params(ts->input_dev,ABS_MT_WIDTH_MAJOR, 0, WIDTH_MAJOR_MAX, 0, 0);

	ts->input_dev->name = goodix_ts_name;
	ret = input_register_device(ts->input_dev);
	if (ret) {
		dev_err(&client->dev,"Probe: Unable to register %s input device\n", ts->input_dev->name);
		goto err_input_register_device_failed;
	}
	i2c_set_clientdata(client, ts);
	
	return 0;
	i2c_set_clientdata(client, NULL);
	input_unregister_device(ts->input_dev);
err_input_register_device_failed:
	input_free_device(ts->input_dev);
err_input_dev_alloc_failed:
	destroy_workqueue(ts->goodix_wq); 
err_create_work_queue_fail:
	free_irq(client->irq,ts);
err_irq_request_fail:
err_no_irq_fail:
err_init_panel_fail:
	if (pdata->exit_platform_hw)
		pdata->exit_platform_hw();
	kfree(ts);
	return ret;
}



static const struct i2c_device_id gt819_id[] = {
	{ GOODIX_I2C_NAME, 0 },
	{ }
};

static struct i2c_driver gt819_driver = {
	.probe		= gt819_probe,
	.remove		= gt819_remove,
	.suspend	= gt819_suspend,
	.resume	    = gt819_resume,
	.id_table	= gt819_id,
	.driver = {
		.name	= GOODIX_I2C_NAME,
		.owner = THIS_MODULE,
	},
};

/*******************************************************	
Description:
	Driver Install function.
return:
	Executive Outcomes. 0---succeed.
********************************************************/
static int __devinit gt819_init(void)
{
	int ret;
	
	ret=i2c_add_driver(&gt819_driver);
	return ret; 
}

/*******************************************************	
Description:
	Driver uninstall function.
return:
	Executive Outcomes. 0---succeed.
********************************************************/
static void __exit gt819_exit(void)
{
	printk(KERN_ALERT "Touchscreen driver of guitar exited.\n");
	i2c_del_driver(&gt819_driver);
}

late_initcall(gt819_init);
module_exit(gt819_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Driver");
MODULE_LICENSE("GPL");
