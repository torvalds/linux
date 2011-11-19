/* drivers/input/touchscreen/gt818_ts.c
 *
 * Copyright (C) 2011 Rockcip, Inc.
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
 * Author: hhb@rock-chips.com
 * Date: 2011.06.20
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

#include <linux/gpio.h>
#include <mach/iomux.h>

#include <linux/irq.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <linux/proc_fs.h>

#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/completion.h>
#include <asm/uaccess.h>
#include <linux/input/mt.h>

#include "gt818_ts.h"



#if !defined(GT801_PLUS) && !defined(GT801_NUVOTON)
#error The code does not match this touchscreen.
#endif

static struct workqueue_struct *goodix_wq;

static const char *gt818_ts_name = "Goodix Capacitive TouchScreen";

static struct point_queue finger_list;

struct i2c_client * i2c_connect_client = NULL;

//EXPORT_SYMBOL(i2c_connect_client);

static struct proc_dir_entry *goodix_proc_entry;
	
#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_early_suspend(struct early_suspend *h);
static void goodix_ts_late_resume(struct early_suspend *h);
#endif

#ifdef HAVE_TOUCH_KEY
	const uint16_t gt818_key_array[]={
									  KEY_MENU,
									  KEY_HOME,
									  KEY_BACK,
									  KEY_SEARCH
									 };
	#define MAX_KEY_NUM	 (sizeof(gt818_key_array)/sizeof(gt818_key_array[0]))
#endif

unsigned int last_x[MAX_FINGER_NUM + 1]= {0};
unsigned int last_y[MAX_FINGER_NUM + 1]= {0};


/*Function as i2c_master_send */
static int i2c_read_bytes(struct i2c_client *client, u8 *buf, int len)
{
	struct i2c_msg msgs[2];
	int ret = -1;

	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;
	msgs[0].len = 2;
	msgs[0].buf = &buf[0];
	msgs[0].scl_rate = GT818_I2C_SCL;
	msgs[0].udelay = client->udelay;

	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = len-2;
	msgs[1].buf = &buf[2];
	msgs[1].scl_rate = GT818_I2C_SCL;
	msgs[1].udelay = client->udelay;

	ret = i2c_transfer(client->adapter, msgs, 2);
	if(ret < 0)
		printk("%s:i2c_transfer fail =%d\n",__func__, ret);

	return ret;
}

/*Function as i2c_master_send */
static int i2c_write_bytes(struct i2c_client *client,u8 *data,int len)
{
	struct i2c_msg msg;
	int ret = -1;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = len;
	msg.buf = data;
	msg.scl_rate = GT818_I2C_SCL;
	msg.udelay = client->udelay;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if(ret < 0)
		printk("%s:i2c_transfer fail =%d\n",__func__, ret);

	return ret;
}


static int i2c_pre_cmd(struct gt818_ts_data *ts)
{
	int ret;
	u8 pre_cmd_data[2] = {0};
	pre_cmd_data[0] = 0x0f;
	pre_cmd_data[1] = 0xff;
	ret = i2c_write_bytes(ts->client,pre_cmd_data,2);
	udelay(20);
	return ret;
}


static int i2c_end_cmd(struct gt818_ts_data *ts)
{
	int ret;
	u8 end_cmd_data[2] = {0};
	end_cmd_data[0] = 0x80;
	end_cmd_data[1] = 0x00;
	ret = i2c_write_bytes(ts->client,end_cmd_data,2);
	udelay(20);
	return ret;
}



static int goodix_init_panel(struct gt818_ts_data *ts)
{
	int ret = -1;
	int i = 0;
#if 1
	u8 config_info[] = {
	0x06,0xA2,
	0x00,0x02,0x04,0x06,0x08,0x0A,0x0C,0x0E,
	0x10,0x12,0x00,0x00,0x10,0x00,0x20,0x00,
	0x30,0x00,0x40,0x00,0x50,0x00,0x60,0x00,
	0xE0,0x00,0xD0,0x00,0xC0,0x00,0xB0,0x00,
	0xA0,0x00,0x90,0x00,0x80,0x00,0x70,0x00,
	0xF0,0x00,0x13,0x13,0x90,0x90,0x90,0x27,
	0x27,0x27,0x0F,0x0E,0x0A,0x40,0x30,0x01,
	0x03,0x00,MAX_FINGER_NUM,0x00,0x14,0xFA,0x1B,0x00,
	0x00,0x66,0x5A,0x6A,0x5E,0x00,0x00,0x05,
	0x14,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x14,0x10,0xEF,0x03,0x00,0x00,0x00,0x00,
	0x00,0x00,0x20,0x40,0x70,0x90,0x0F,0x40,
	0x30,0x3C,0x28,0x00,0x00,0x00,0x00,0x00,
	0x00,0x01
	};
#endif
	u8 read_config_info[sizeof(config_info)] = {0};
	read_config_info[0] = 0x06;
	read_config_info[1] = 0xa2;

	ret = i2c_write_bytes(ts->client, config_info, (sizeof(config_info)/sizeof(config_info[0])));
	if (ret < 0) {
		printk("config gt818 fail\n");
		return ret;
	}

	ret = i2c_read_bytes(ts->client, read_config_info, (sizeof(config_info)/sizeof(config_info[0])));
	if (ret < 0){
		printk("read gt818 config fail\n");
		return ret;
	}

	for(i = 2; i < 106; i++){
		if(read_config_info[i] != config_info[i]){
			printk("write gt818 config error\n");
			ret = -1;
			return ret;
		}
	}
	msleep(10);
	return 0;

}

static int  goodix_read_version(struct gt818_ts_data *ts)
{
	int ret;
	u8 version_data[5] = {0};	//store touchscreen version infomation
	memset(version_data, 0, 5);
	version_data[0] = 0x07;
	version_data[1] = 0x17;
	msleep(2);
	ret = i2c_read_bytes(ts->client, version_data, 4);
	if (ret < 0) 
		return ret;
	dev_info(&ts->client->dev," Guitar Version: %d.%d\n",version_data[3],version_data[2]);
	return 0;
	
}



static void goodix_ts_work_func(struct work_struct *work)
{	
	u8  touch_status[8*MAX_FINGER_NUM + 18] = {READ_TOUCH_ADDR_H, READ_TOUCH_ADDR_L, 0};
	u8  *key_value = NULL;
	u8  *point_data = NULL;
	static u8 finger_last[MAX_FINGER_NUM + 1]={0};
	u8  finger_current[MAX_FINGER_NUM + 1] = {0};
	u8  coor_data[6*MAX_FINGER_NUM] = {0};
	static u8  last_key = 0;

	u8  finger = 0;
	u8  key = 0;
	u8 retry = 0;
	unsigned int  count = 0;
	unsigned int position = 0;	
	int temp = 0;
	int x = 0, y = 0 , pressure;

	u16 *coor_point;

	int syn_flag = 0;

	struct gt818_ts_data *ts = container_of(work, struct gt818_ts_data, work);

	i2c_pre_cmd(ts);
	i2c_read_bytes(ts->client, touch_status, sizeof(touch_status)/sizeof(touch_status[0]));
	i2c_end_cmd(ts);

	//judge whether the data is ready
	if((touch_status[2] & 0x30) != 0x20)
	{
		printk("%s:DATA_NO_READY\n", __func__);
		goto DATA_NO_READY;
	}
	//judge whether it is large area touch
	if(touch_status[13] & 0x0f)
	{
		goto DATA_NO_READY;
	}

	ts->bad_data = 0;
	finger = touch_status[2] & 0x07;
	key_value = touch_status + 15;
	key = key_value[2] & 0x0f;

	if(finger > 0)
	{
		point_data = key_value + 3;

		for(position = 0; position < (finger*8); position += 8)
		{
			temp = point_data[position];
			//printk("track:%d\n", temp);
			if(temp < (MAX_FINGER_NUM + 1))
			{
				finger_current[temp] = 1;
				for(count = 0; count < 6; count++)
				{
					coor_data[(temp - 1) * 6 + count] = point_data[position+1+count];
				}
			}
			else
			{
				//dev_err(&(ts->client->dev),"Track Id error:%d\n ",);
				ts->bad_data = 1;
				ts->retry++;
				goto XFER_ERROR;
			}		
		}
	
	}
	
	else
	{
		for(position = 1; position < MAX_FINGER_NUM+1; position++)
		{
			finger_current[position] = 0;
		}
	}

	coor_point = (u16 *)coor_data;

	for(position = 1; position < MAX_FINGER_NUM + 1; position++)
	{
		if((finger_current[position] == 0) && (finger_last[position] != 0))
		{
			//printk("<<<<<<<<<<<<<<<<<<<%s:positon:%d (%d,%d)\n", __func__, position,last_x,last_y);
			//printk("<<<%d , %d ",finger_current[position],finger_last[position]);
			//input_mt_slot(ts->input_dev, position);
			//input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
			//input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
			//input_report_abs(ts->input_dev, ABS_MT_POSITION_X, last_x[position]);
			//input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, last_y[position]);
			//input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 100);
			//input_mt_sync(ts->input_dev);
			input_mt_slot(ts->input_dev, position);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
			syn_flag = 1;
		}
		else if(finger_current[position])
		{
			x = (*(coor_point+3*(position-1)))*SCREEN_MAX_WIDTH/(TOUCH_MAX_WIDTH);
			y = (*(coor_point+3*(position-1)+1))*SCREEN_MAX_HEIGHT/(TOUCH_MAX_HEIGHT);
			pressure = (*(coor_point+3*(position-1)+2));
			if(x < SCREEN_MAX_WIDTH){
				x = SCREEN_MAX_WIDTH - x;
			}

			if(y < SCREEN_MAX_HEIGHT){
			//	y = SCREEN_MAX_HEIGHT-y;
			}

			//printk(">>>>>>>>>>>>>>>>>%s:positon:%d (%d,%d)\n", __func__, position,x,y);
			input_mt_slot(ts->input_dev, position);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 1);
			input_report_abs(ts->input_dev, ABS_MT_PRESSURE, pressure);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);

			last_x[position] = x;
			last_y[position] = y;
			//input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, pressure);
			//input_mt_sync(ts->input_dev);
			syn_flag = 1;
		}

		input_sync(ts->input_dev);
	}


#ifdef HAVE_TOUCH_KEY
	if((last_key == 0) && (key == 0)){
		goto NO_KEY_PRESS;
	}
	else{
		syn_flag = 1;
		switch(key){
			case 1:
				key = 4;
				break;
			case 2:
				key = 3;
				break;
			case 4:
				key = 2;
				break;
			case 8:
				key = 1;
				break;
			default:
				key = 0;
				break;
		}
		if(key != 0){
			input_report_key(ts->input_dev, gt818_key_array[key - 1], 1);
		}
		else{
			input_report_key(ts->input_dev, gt818_key_array[last_key - 1], 0);
		}
		last_key = key;
	}		

#endif


NO_KEY_PRESS:
	if(syn_flag){
		input_sync(ts->input_dev);
	}

	for(position = 1; position < MAX_FINGER_NUM + 1; position++)
	{
		finger_last[position] = finger_current[position];
	}

DATA_NO_READY:
XFER_ERROR:
//	i2c_end_cmd(ts);
	if(ts->use_irq)
		enable_irq(ts->client->irq);

}

static int test_suspend_resume(struct gt818_ts_data *ts){
	while(1){
		ts->power(ts, 0);
		msleep(5000);
		ts->power(ts, 1);
		msleep(5000);
	}
	return 0;
}


static enum hrtimer_restart goodix_ts_timer_func(struct hrtimer *timer)
{
	struct gt818_ts_data *ts = container_of(timer, struct gt818_ts_data, timer);
	queue_work(goodix_wq, &ts->work);
	hrtimer_start(&ts->timer, ktime_set(0, (POLL_TIME+6)*1000000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}


static irqreturn_t goodix_ts_irq_handler(int irq, void *dev_id)
{
	struct gt818_ts_data *ts = dev_id;
	disable_irq_nosync(ts->client->irq);
	queue_work(goodix_wq, &ts->work);
	return IRQ_HANDLED;
}

static int goodix_ts_power(struct gt818_ts_data * ts, int on)
{
	int ret = -1;
	struct gt818_platform_data	*pdata = ts->client->dev.platform_data;
	unsigned char i2c_control_buf[3] = {0x06,0x92,0x01};		//suspend cmd
	if(ts != NULL && !ts->use_irq)
		return -2;
	switch(on)
	{
		case 0:
			i2c_pre_cmd(ts);
			// set the io port high level to avoid level change which might stop gt818 from sleeping
			gpio_direction_output(pdata->gpio_reset, 1);
			gpio_direction_output(pdata->gpio_pendown, 1);
			msleep(5);
			ret = i2c_write_bytes(ts->client, i2c_control_buf, 3);
			if(ret < 0)
			{
				printk(KERN_INFO"**gt818 suspend fail**\n");
			}
			else
			{
				//printk(KERN_INFO"**gt818 suspend**\n");
				ret = 0;
			}
//			i2c_end_cmd(ts);
			return ret;
			
		case 1:

			gpio_pull_updown(pdata->gpio_pendown, 1);
			gpio_direction_output(pdata->gpio_pendown, 0);
			msleep(1);
			gpio_direction_output(pdata->gpio_pendown, 1);
			msleep(1);
			gpio_direction_input(pdata->gpio_pendown);
			gpio_pull_updown(pdata->gpio_pendown, 0);

/*
			msleep(2);
			gpio_pull_updown(pdata->gpio_reset, 1);
			gpio_direction_output(pdata->gpio_reset, 0);
			msleep(2);
			gpio_direction_input(pdata->gpio_reset);
			gpio_pull_updown(pdata->gpio_reset, 0);
			msleep(30);
*/
			msleep(1);
			ret = i2c_pre_cmd(ts);
			//printk(KERN_INFO"**gt818 reusme**\n");
			ret = i2c_end_cmd(ts);

			return ret;
				
		default:
			printk(KERN_DEBUG "%s: Cant't support this command.", gt818_ts_name);
			return -EINVAL;
	}

}


static int goodix_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	int retry=0;
	u8 goodix_id[3] = {0,0xff,0};
	struct gt818_ts_data *ts;

	struct gt818_platform_data *pdata;
	dev_info(&client->dev,"Install touch driver.\n");
	printk("gt818: Install touch driver.\n");
	//Check I2C function
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}

	i2c_connect_client = client;	//used by Guitar_Update
	pdata = client->dev.platform_data;
	ts->client = client;
	i2c_set_clientdata(client, ts);

	//init int and reset ports
	ret = gpio_request(pdata->gpio_pendown, "TS_INT");	//Request IO
	if (ret){
		dev_err(&client->dev, "Failed to request GPIO:%d, ERRNO:%d\n",(int)pdata->gpio_pendown, ret);
		goto err_gpio_request_failed;
	}
	rk29_mux_api_set(pdata->pendown_iomux_name, pdata->pendown_iomux_mode);
	gpio_direction_input(pdata->gpio_pendown);
	gpio_pull_updown(pdata->gpio_pendown, 0);

	ret = gpio_request(pdata->gpio_reset, "gt818_resetPin");
	if(ret){
		dev_err(&client->dev, "failed to request resetPin GPIO%d\n", pdata->gpio_reset);
		goto err_gpio_request_failed;
	}
	rk29_mux_api_set(pdata->resetpin_iomux_name, pdata->resetpin_iomux_mode);

#if 1
	for(retry = 0; retry < 4; retry++)
	{
		gpio_pull_updown(pdata->gpio_reset, 1);
		gpio_direction_output(pdata->gpio_reset, 0);
		msleep(1);     //delay at least 1ms
		gpio_direction_input(pdata->gpio_reset);
		gpio_pull_updown(pdata->gpio_reset, 0);
		msleep(25);   //delay at least 20ms
		ret = i2c_pre_cmd(ts);
		if (ret > 0)
			break;
		msleep(50);
	}

	if(ret <= 0)
	{
		dev_err(&client->dev, "Warnning: I2C communication might be ERROR!\n");
		goto err_i2c_failed;
	}	
#endif

	for(retry = 0; retry < 3; retry++)
	{
		ret = goodix_init_panel(ts);

		dev_info(&client->dev,"the config ret is :%d\n", ret);
		msleep(20);
		if(ret < 0)	//Initiall failed
			continue;
		else
			break;
	}

	if(ret < 0) {
		ts->bad_data = 1;
		goto err_init_godix_ts;
	}

	goodix_read_version(ts);


	INIT_WORK(&ts->work, goodix_ts_work_func);		//init work_struct
	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		dev_dbg(&client->dev,"goodix_ts_probe: Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}

	//ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) ;
	//ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	//ts->input_dev->absbit[0] = BIT_MASK(ABS_MT_POSITION_X) | BIT_MASK(ABS_MT_POSITION_Y) |
	//		BIT_MASK(ABS_MT_TOUCH_MAJOR) | BIT_MASK(ABS_MT_WIDTH_MAJOR);  // for android


#ifdef HAVE_TOUCH_KEY
	for(retry = 0; retry < MAX_KEY_NUM; retry++)
	{
		input_set_capability(ts->input_dev, EV_KEY, gt818_key_array[retry]);
	}
#endif

	snprintf(ts->phys, sizeof(ts->phys), "%s/input0", dev_name(&client->dev));
	snprintf(ts->name, sizeof(ts->name), "gt818-touchscreen");

	ts->input_dev->name = "gt818_ts";//ts->name;
	ts->input_dev->phys = ts->phys;
	ts->input_dev->dev.parent = &client->dev;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->id.vendor = 0xDEAD;
	ts->input_dev->id.product = 0xBEEF;
	ts->input_dev->id.version = 10427;	//screen firmware version

	__set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
	__set_bit(EV_ABS, ts->input_dev->evbit);
#ifdef GOODIX_MULTI_TOUCH
	input_mt_init_slots(ts->input_dev, MAX_FINGER_NUM);
	//input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, SCREEN_MAX_WIDTH, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, SCREEN_MAX_HEIGHT, 0, 0);
	//input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, MAX_FINGER_NUM, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
#else
	input_set_abs_params(ts->input_dev, ABS_X, 0, SCREEN_MAX_HEIGHT, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, 0, SCREEN_MAX_WIDTH, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, 0, 0);
#endif	
	
	ret = input_register_device(ts->input_dev);
	if (ret) {
		dev_err(&client->dev,"Probe: Unable to register %s input device\n", ts->input_dev->name);
		goto err_input_register_device_failed;
	}
	ts->bad_data = 0;
//	16finger_list.length = 0;

	client->irq = gpio_to_irq(pdata->gpio_pendown);		//If not defined in client
	if (client->irq)
	{

	#if INT_TRIGGER==0
		#define GT801_PLUS_IRQ_TYPE IRQ_TYPE_EDGE_RISING
	#elif INT_TRIGGER==1
		#define GT801_PLUS_IRQ_TYPE IRQ_TYPE_EDGE_FALLING
	#elif INT_TRIGGER==2
		#define GT801_PLUS_IRQ_TYPE IRQ_TYPE_LEVEL_LOW
	#elif INT_TRIGGER==3
		#define GT801_PLUS_IRQ_TYPE IRQ_TYPE_LEVEL_HIGH
	#endif

		ret = request_irq(client->irq, goodix_ts_irq_handler, GT801_PLUS_IRQ_TYPE,
			client->name, ts);
		if (ret != 0) {
			dev_err(&client->dev,"Cannot allocate ts INT!ERRNO:%d\n", ret);
			gpio_direction_input(pdata->gpio_pendown);
			gpio_free(pdata->gpio_pendown);
			goto err_gpio_request_failed;
		}
		else 
		{	
			disable_irq(client->irq);
			ts->use_irq = 1;
			dev_dbg(&client->dev,"Reques EIRQ %d succesd on GPIO:%d\n", client->irq, pdata->gpio_pendown);
		}	
	}

err_gpio_request_failed:
	ts->power = goodix_ts_power;
#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = goodix_ts_early_suspend;
	ts->early_suspend.resume = goodix_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif
	dev_info(&client->dev,"Start %s in %s mode\n",
		ts->input_dev->name, ts->use_irq ? "interrupt" : "polling");

	if (ts->use_irq)
	{
		enable_irq(client->irq);
	}
	else
	{
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = goodix_ts_timer_func;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}

	i2c_end_cmd(ts);
	return 0;

err_init_godix_ts:
	i2c_end_cmd(ts);
	if(ts->use_irq)
	{
		ts->use_irq = 0;
		free_irq(client->irq,ts);
		gpio_direction_input(pdata->gpio_pendown);
		gpio_free(pdata->gpio_pendown);
	}
	else 
		hrtimer_cancel(&ts->timer);

err_input_register_device_failed:
	input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
	i2c_set_clientdata(client, NULL);
err_i2c_failed:	
	kfree(ts);
err_alloc_data_failed:
err_check_functionality_failed:
err_create_proc_entry:
	return ret;
}


static int goodix_ts_remove(struct i2c_client *client)
{
	struct gt818_ts_data *ts = i2c_get_clientdata(client);
	struct gt818_platform_data	*pdata = client->dev.platform_data;

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
	remove_proc_entry("goodix-update", NULL);
#endif
	if (ts && ts->use_irq) 
	{
		gpio_direction_input(pdata->gpio_pendown);
		gpio_free(pdata->gpio_pendown);
		free_irq(client->irq, ts);
	}	
	else if(ts)
		hrtimer_cancel(&ts->timer);
	
	dev_notice(&client->dev,"The driver is removing...\n");
	i2c_set_clientdata(client, NULL);
	input_unregister_device(ts->input_dev);
	kfree(ts);
	return 0;
}


static int goodix_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	struct gt818_ts_data *ts = i2c_get_clientdata(client);

	if (ts->use_irq)
		disable_irq(client->irq);
	else
		hrtimer_cancel(&ts->timer);
	//ret = cancel_work_sync(&ts->work);
	//if(ret && ts->use_irq)	
		//enable_irq(client->irq);
	if (ts->power) {
		ret = ts->power(ts, 0);
		if (ret < 0)
			printk(KERN_ERR "goodix_ts_resume power off failed\n");
	}
	return 0;
}


static int goodix_ts_resume(struct i2c_client *client)
{
	int ret;
	struct gt818_ts_data *ts = i2c_get_clientdata(client);

	if (ts->power) {
		ret = ts->power(ts, 1);
		if (ret < 0)
			printk(KERN_ERR "goodix_ts_resume power on failed\n");
	}

	if (ts->use_irq)
		enable_irq(client->irq);
	else
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_early_suspend(struct early_suspend *h)
{
	struct gt818_ts_data *ts;
	ts = container_of(h, struct gt818_ts_data, early_suspend);
	goodix_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void goodix_ts_late_resume(struct early_suspend *h)
{
	struct gt818_ts_data *ts;
	ts = container_of(h, struct gt818_ts_data, early_suspend);
	goodix_ts_resume(ts->client);
}
#endif


//only one client
static const struct i2c_device_id goodix_ts_id[] = {
	{ GOODIX_I2C_NAME, 0 },
	{ }
};


static struct i2c_driver goodix_ts_driver = {
	.probe		= goodix_ts_probe,
	.remove		= goodix_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= goodix_ts_suspend,
	.resume		= goodix_ts_resume,
#endif
	.id_table	= goodix_ts_id,
	.driver = {
		.name	= GOODIX_I2C_NAME,
		.owner = THIS_MODULE,
	},
};


static int __devinit goodix_ts_init(void)
{
	int ret;
	goodix_wq = create_singlethread_workqueue("goodix_wq");		//create a work queue and worker thread
	if (!goodix_wq) {
		printk(KERN_ALERT "creat workqueue faiked\n");
		return -ENOMEM;
	}
	ret = i2c_add_driver(&goodix_ts_driver);
	return ret; 
}


static void __exit goodix_ts_exit(void)
{
	printk(KERN_ALERT "Touchscreen driver of guitar exited.\n");
	i2c_del_driver(&goodix_ts_driver);
	if (goodix_wq)
		destroy_workqueue(goodix_wq);		//release our work queue
}

late_initcall(goodix_ts_init);
module_exit(goodix_ts_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Driver");
MODULE_AUTHOR("hhb@rock-chips.com");
MODULE_LICENSE("GPL");

