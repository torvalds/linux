/* drivers/input/touchscreen/goodix_touch.c
 *
 * Copyright (C) 2010 Goodix, Inc.
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
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/earlysuspend.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/goodix_touch.h>
#include "goodix_queue.h"
#include <mach/am_regs.h>
#include <mach/gpio.h>

#ifndef GUITAR_SMALL
#error The code does not match the hardware version.
#endif

static int dbg_en = 0;
#define goodix_dbg(level, fmt, args...)  { if(level) \
					printk("[goodix]: " fmt, ## args); }

struct goodix_ts_data {
	uint16_t addr;
	struct i2c_client *client;
	struct input_dev *input_dev;
	bool use_irq;
	bool init_finished;
	struct hrtimer timer;
	struct work_struct  work;
	char phys[32];
	int (*power)(int on);
	struct early_suspend early_suspend;
	int xmax;
	int ymax;
	bool swap_xy;
	bool xpol;
	bool ypol;
	bool suspend_state;
}; 

static struct workqueue_struct *goodix_wq;

static struct point_queue  finger_list;	//record the fingers list 
/*************************************************/

const char *s3c_ts_name = "Goodix TouchScreen of Guitar";
struct i2c_client * i2c_connect_client = NULL;
EXPORT_SYMBOL(i2c_connect_client);
	
#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_early_suspend(struct early_suspend *h);
static void goodix_ts_late_resume(struct early_suspend *h);
#endif

/*Function as i2c_master_send */
static int i2c_read_bytes(struct i2c_client *client, uint8_t *buf, int len)
{
	struct i2c_msg msgs[2];
	int ret=-1;
	msgs[0].flags=!I2C_M_RD;
	msgs[0].addr=client->addr;
	msgs[0].len=1;
	msgs[0].buf=&buf[0];

	msgs[1].flags=I2C_M_RD;
	msgs[1].addr=client->addr;
	msgs[1].len=len-1;
	msgs[1].buf=&buf[1];
	
	ret=i2c_transfer(client->adapter,msgs,2);
	return ret;
}


/*Function as i2c_master_send */
static int i2c_write_bytes(struct i2c_client *client,uint8_t *data,int len)
{
	struct i2c_msg msg;
	int ret=-1;
	msg.flags=!I2C_M_RD;
	msg.addr=client->addr;
	msg.len=len;
	msg.buf=data;		
	
	ret=i2c_transfer(client->adapter,&msg,1);
	return ret;
}

static int goodix_init_panel(struct goodix_ts_data *ts)
{
	int ret=-1;
	struct goodix_i2c_rmi_platform_data *pdata = ts->client->dev.platform_data;

	printk(KERN_ALERT"goodix init panel\n");
//	int i;
//	for (i=0; i<pdata->config_info_len; i++){
//		printk("0x%x\n", pdata->config_info[i]);
//	}
	ret=i2c_write_bytes(ts->client,pdata->config_info,pdata->config_info_len);
	if (ret < 0) 
		goto error_i2c_transfer;
	msleep(1);
	return 0;

error_i2c_transfer:
	return ret;
}

static int  goodix_read_version(struct goodix_ts_data *ts)
{
	int ret;
	uint8_t version[2]={0x69,0xff};	//command of reading Guitar's version 
	uint8_t version_data[41];		//store touchscreen version infomation
	memset(version_data, 0 , sizeof(version_data));
	version_data[0]=0x6A;
	ret=i2c_write_bytes(ts->client,version,2);
	if (ret < 0) 
		goto error_i2c_version;
	msleep(16);
	ret=i2c_read_bytes(ts->client,version_data, 40);
	if (ret < 0) 
		goto error_i2c_version;
	dev_info(&ts->client->dev," Guitar Version: %s\n", &version_data[1]);
	version[1] = 0x00;				//cancel the command
	i2c_write_bytes(ts->client, version, 2);
	return 0;
	
error_i2c_version:
	return ret;
}


static ssize_t goodix_read(struct device *dev, struct device_attribute *attr, char *buf)
{
		int i = 0;
    struct goodix_ts_data *ts = (struct goodix_ts_data *)dev_get_drvdata(dev);
    unsigned char config_data[54];
  	
  	memset(config_data, 0, sizeof(config_data));
    if (!strcmp(attr->attr.name, "ctpconfig")) {
    	config_data[0] = 0x30;
 			i2c_read_bytes(ts->client,&config_data[0],sizeof(config_data));
			memcpy(buf, &config_data[0], sizeof(config_data));
			for (i=0; i<sizeof(config_data); i++){
				printk("%x\n", config_data[i]);
			}
			return sizeof(config_data);
    }
    return 0;
}

static ssize_t goodix_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int i = 0;
    struct goodix_ts_data *ts = (struct goodix_ts_data *)dev_get_drvdata(dev);
    unsigned char config_data[100];
		struct goodix_i2c_rmi_platform_data *pdata = ts->client->dev.platform_data;
		int SHUTDOWN_PORT  = pdata->gpio_shutdown;
    
    if (!strcmp(attr->attr.name, "ctpconfig")) {
			printk(KERN_ALERT"goodix set panel config\n");
			gpio_set_value(SHUTDOWN_PORT, 1);
			msleep(100);
			gpio_set_value(SHUTDOWN_PORT, 0);
			msleep(100);

			for (i=0; i<100; i++){
				if (sscanf(buf, "%x", &config_data[i])== 1) {
					printk("%x\n", config_data[i]);
					buf += 5;
				}
				else
					break;
			}
			printk(KERN_ALERT"total data = %d\n", i);
			i2c_write_bytes(ts->client,&config_data[0],i);
    }

    if (!strcmp(attr->attr.name, "debug")) {
			dbg_en = !dbg_en;
			printk(KERN_ALERT"%s debug\n", dbg_en ? "enable" : "disable");
  	}
    return count;
}

static DEVICE_ATTR(ctpconfig, S_IRWXUGO, goodix_read, goodix_write);
static DEVICE_ATTR(debug, S_IRWXUGO, goodix_read, goodix_write);

static struct attribute *goodix_attr[] = {
    &dev_attr_ctpconfig.attr,
    &dev_attr_debug.attr,
    NULL
};

static struct attribute_group goodix_attr_group = {
    .name = NULL,
    .attrs = goodix_attr,
};

static void goodix_ts_work_func(struct work_struct *work)
{	
	uint8_t  point_data[35]={ 0 };
	static uint8_t  finger_bit=0;
	uint8_t finger=0;
	int ret=-1;
	unsigned int pressure[MAX_FINGER_NUM];	
	int pos[MAX_FINGER_NUM][2];
	int position =0;
	int count = 0, read_position = 0;
	int check_sum = 0;
	
	struct goodix_ts_data *ts = container_of(work, struct goodix_ts_data, work);
	struct goodix_i2c_rmi_platform_data *pdata = ts->client->dev.platform_data;
	int SHUTDOWN_PORT  = pdata->gpio_shutdown;
	static int work_count = 0;
	goodix_dbg(dbg_en, "work count=%d\n", work_count++);

	if (gpio_get_value(SHUTDOWN_PORT))
	{
		printk(KERN_ALERT  "Guitar stop working.The data is invalid. \n");
		goto NO_ACTION;
	}	

	int retry = 0;
	while(1) {
		ret=i2c_read_bytes(ts->client, point_data, 35);
		if (ret > 0)
			break;
		else {
			dev_err(&(ts->client->dev),"%dth I2C transfer error. Number:%d\n ", retry, ret);
			msleep(20);
			goodix_init_panel(ts);
			msleep(20);
			if (++retry > 3) {
				dev_info(&(ts->client->dev), "Because of transfer error, Guitar's driver stop working.\n");
				goto XFER_ERROR;
			}
		}
	}
	
	//The bit indicate which fingers pressed down
	switch(point_data[1]& 0x1f)
	{
		case 0:
		case 1:
			for(count=1; count<8; count++)
				check_sum += (int)point_data[count];
			if((check_sum%256) != point_data[8])
				goto XFER_ERROR;
			break;
		case 2:
		case 3:
			for(count=1; count<13;count++)
				check_sum += (int)point_data[count];
			if((check_sum%256) != point_data[13])
				goto XFER_ERROR;
			break;	
		default:		//(point_data[1]& 0x1f) > 3
			for(count=1; count<34;count++)
				check_sum += (int)point_data[count];
			if((check_sum%256) != point_data[34])
				goto XFER_ERROR;
	}
	
	point_data[1]&=0x1f;
	finger = finger_bit^point_data[1];
	if(finger == 0 && point_data[1] == 0)			
		goto NO_ACTION;						//no fingers and no action
	else if(finger == 0)							//the same as last time
		goto BIT_NO_CHANGE;					
	//check which point(s) DOWN or UP
	for(position = 0; (finger !=0)&& (position < MAX_FINGER_NUM);  position++)
	{
		if((finger&0x01) == 1)		//current bit is 1?
		{							//NO.postion finger is change
			if(((finger_bit>>position)&0x01)==1)	//NO.postion finger is UP
				set_up_point(&finger_list, position);
			else 
				add_point(&finger_list, position);
		}
		finger>>=1;
	}

BIT_NO_CHANGE:
	for(count = 0; count < finger_list.length; count++)
	{	
		if(finger_list.pointer[count].state == FLAG_UP)
		{
			pos[count][0] = pos[count][1] = 0;	
			pressure[count] = 0;
			continue;
		}
		
		if(finger_list.pointer[count].num < 3)
			read_position = finger_list.pointer[count].num*5 + 3;
		else if (finger_list.pointer[count].num == 4)
			read_position = 29;
		if(finger_list.pointer[count].num != 3)
		{
			pos[count][0] = (unsigned int) (point_data[read_position]<<8) + (unsigned int)( point_data[read_position+1]);
			pos[count][1] = (unsigned int)(point_data[read_position+2]<<8) + (unsigned int) (point_data[read_position+3]);
			pressure[count] = (unsigned int) (point_data[read_position+4]);
		}
		else 
		{
			pos[count][0] = (unsigned int) (point_data[18]<<8) + (unsigned int)( point_data[25]);
			pos[count][1] = (unsigned int)(point_data[26]<<8) + (unsigned int) (point_data[27]);
			pressure[count] = (unsigned int) (point_data[28]);
		}
		if (pdata->swap_xy)
			swap(pos[count][0] ,pos[count][1]); 
		if (pdata->xpol)
			pos[count][0] = pdata->xmax - pos[count][0];
		if (pdata->ypol)
			pos[count][1] = pdata->ymax - pos[count][1];
		goodix_dbg(dbg_en, "Count:%d Index:%d X:%d Y:%d\n",count, finger_list.pointer[count].num, pos[count][0], pos[count][1]);
	}

	if(finger_list.length > 0 && finger_list.pointer[0].state == FLAG_DOWN)
	{
		input_report_abs(ts->input_dev, ABS_X, pos[0][0]);
		input_report_abs(ts->input_dev, ABS_Y, pos[0][1]);	
	} 
	if(finger_list.length > 0)
	{
		input_report_abs(ts->input_dev, ABS_PRESSURE, pressure[0]);
		input_report_key(ts->input_dev, BTN_TOUCH,  finger_list.pointer[0].state);                              
	}
#ifdef GOODIX_MULTI_TOUCH
	if(finger_list.length > 0 && finger_list.pointer[0].state == FLAG_DOWN)
	{
		input_report_abs(ts->input_dev, ABS_MT_POSITION_X, pos[0][0]);
		input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, pos[0][1]);
	}	
	input_report_abs(ts->input_dev, ABS_MT_PRESSURE, pressure[0]);
	input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, pressure[0]);
	input_mt_sync(ts->input_dev);

	for(count = 1; count < MAX_FINGER_NUM; count++)
	{
		if (finger_list.length > count)
		{
			if(finger_list.pointer[count].state == FLAG_DOWN)
			{
				input_report_abs(ts->input_dev, ABS_MT_POSITION_X, pos[count][0]);
				input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, pos[count][1]);
			} 
			input_report_abs(ts->input_dev, ABS_MT_PRESSURE, pressure[count]);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, pressure[count]);
			input_mt_sync(ts->input_dev);
		}
	}
#endif
	input_sync(ts->input_dev);
	del_point(&finger_list);
	finger_bit=point_data[1];
	
XFER_ERROR:	
NO_ACTION:
	if(ts->use_irq)
		enable_irq(ts->client->irq);

}

static enum hrtimer_restart goodix_ts_timer_func(struct hrtimer *timer)
{
	struct goodix_ts_data *ts = container_of(timer, struct goodix_ts_data, timer);
	static int timer_count = 0;
	goodix_dbg(dbg_en, "timer count=%d\n", timer_count++);
	queue_work(goodix_wq, &ts->work);
	hrtimer_start(&ts->timer, ktime_set(0, 16000000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

static irqreturn_t goodix_ts_irq_handler(int irq, void *dev_id)
{
	struct goodix_ts_data *ts = dev_id;
	struct goodix_i2c_rmi_platform_data *pdata = ts->client->dev.platform_data;
	int level = gpio_get_value(pdata->gpio_irq);
	static int irq_count = 0;	
	goodix_dbg(dbg_en, "irq level=%d, irq count=%d \n", level, irq_count++);
	if (ts->init_finished && (!ts->suspend_state)) {
		disable_irq_nosync(ts->client->irq);
		queue_work(goodix_wq, &ts->work);
	}
	else {
		ts->init_finished = 1;
		printk(KERN_ALERT"discard first irq\n");			
	}
	return IRQ_HANDLED;
}

static int goodix_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct goodix_ts_data *ts;
	int ret = 0;
	int retry=0;
	int count=0;

	struct goodix_i2c_rmi_platform_data *pdata = client->dev.platform_data;
	int INT_PORT  = pdata ->gpio_irq;
	int SHUTDOWN_PORT  = pdata->gpio_shutdown;
	if (!SHUTDOWN_PORT  || !INT_PORT) {
	    ret = -1;
	    printk(KERN_ALERT  "goodix platform data error\n");
	    goto err_check_functionality_failed ;
	}
	printk("Install touchscreen driver for guitar(fixed suspend issue).\n");
	//Check I2C function
	ret = gpio_request(SHUTDOWN_PORT, "TS_SHUTDOWN");	//Request IO
	if (ret < 0) 
	{
		printk(KERN_ALERT "Failed to request GPIO:%d, ERRNO:%d\n",(int)SHUTDOWN_PORT,ret);
		goto err_check_functionality_failed;
	}	
	gpio_direction_output(SHUTDOWN_PORT, 0);	//Touchscreen is waiting to wakeup
	ret = gpio_get_value(SHUTDOWN_PORT);
	if (ret)
	{
		printk(KERN_ALERT  "Cannot set touchscreen to work.\n");
		goto err_check_functionality_failed;
	}								//waite guitar start
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&client->dev, "System need I2C function.\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	i2c_connect_client = client;	//used by Guitar Updating.
	msleep(16);
	for(retry=0; retry < 5; retry++)
	{
		ret =i2c_write_bytes(client, NULL, 0);	//Test i2c.
		if (ret > 0)
			break;
	}
	if(ret < 0)
	{
		dev_err(&client->dev, "Warnning: I2C connection might be something wrong!\n");
		ret = -ENOSYS;
		goto err_i2c_failed;
	}

//	gpio_set_value(SHUTDOWN_PORT, 1);		//suspend
	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}
	
	INIT_WORK(&ts->work, goodix_ts_work_func);
	ts->client = client;
	i2c_set_clientdata(client, ts);
	
	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		dev_dbg(&client->dev," In File:%s Function:%s Failed to allocate input device\n", __FILE__, __func__);
		goto err_input_dev_alloc_failed;
	}

	ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) ;
	ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	ts->input_dev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y); // for android

	//#ifdef GOODIX_MULTI_TOUCH	//used by android 1.x for multi-touch, not realized
	//ts->input_dev->absbit[0]=BIT(ABS_HAT0X) |BIT(ABS_HAT0Y);	
	//ts->input_dev->keybit[BIT_WORD(BTN_2)] = BIT_MASK(BTN_2);
	//#endif

	input_set_abs_params(ts->input_dev, ABS_X, 0, pdata->xmax, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, 0, pdata->ymax, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, 0, 0);
	
#ifdef GOODIX_MULTI_TOUCH
	input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, pdata->xmax, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, pdata->ymax, 0, 0);	
#endif	
	sprintf(ts->phys, "input/ts)");

	ts->input_dev->name = s3c_ts_name;
	ts->input_dev->phys = ts->phys;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->id.vendor = 0xDEAD;
	ts->input_dev->id.product = 0xBEEF;
	ts->input_dev->id.version = 10427;	

	finger_list.length = 0;
	ret = input_register_device(ts->input_dev);
	if (ret) {
		dev_err(&client->dev,"Probe: Unable to register %s input device\n", ts->input_dev->name);
		goto err_input_register_device_failed;
	}

	gpio_set_value(SHUTDOWN_PORT, 1);
	msleep(100);
	gpio_set_value(SHUTDOWN_PORT, 0);
	msleep(100);
	goodix_init_panel(ts);
	goodix_read_version(ts);
	msleep(500);
	struct device *dev = &client->dev;
  sysfs_create_group(&dev->kobj, &goodix_attr_group);
	dev_set_drvdata(dev, ts);

	ts->init_finished = 0;
	ts->use_irq = 0;
	ts->suspend_state = 0;
	if (client->irq)
	{
		ret = gpio_request(INT_PORT, "TS_INT");	//Request IO
		if (ret < 0) 
		{
			dev_err(&client->dev, "Failed to request GPIO:%d, ERRNO:%d\n",(int)INT_PORT,ret);
			goto err_gpio_request_failed;
		}
		/* set input mode */
		gpio_direction_input(INT_PORT);
		gpio_enable_edge_int(gpio_to_idx(INT_PORT),
		          pdata->irq_edge, client->irq - INT_GPIO_0);
		ret  = request_irq(client->irq, goodix_ts_irq_handler,
			pdata->irq_edge ? IRQ_TYPE_EDGE_FALLING : IRQ_TYPE_EDGE_RISING,
			client->name, ts);
		if (ret != 0) {
			dev_err(&client->dev,"Can't allocate touchscreen's interrupt!ERRNO:%d\n", ret);
			gpio_free( INT_PORT);
			goto err_gpio_request_failed;
		}
		else 
		{	
//			disable_irq(client->irq);
			ts->use_irq = 1;
			dev_info(&client->dev,"Reques EIRQ %d succesd on GPIO:%d\n",client->irq,INT_PORT);
		}
	}
	
err_gpio_request_failed:	
	if (!ts->use_irq) 
	{
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = goodix_ts_timer_func;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}
	
#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = goodix_ts_early_suspend;
	ts->early_suspend.resume = goodix_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif
	dev_info(&client->dev,"Start  %s in %s mode\n", 
		ts->input_dev->name, ts->use_irq ? "Interrupt" : "Polling\n");
	return 0;

err_init_godix_ts:
	if(ts->use_irq)
		free_irq(client->irq,ts);
	gpio_request(INT_PORT,"TS_INT");	
	gpio_free(INT_PORT);

err_input_register_device_failed:
	input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
	i2c_set_clientdata(client, NULL);
	kfree(ts);
err_i2c_failed:
	gpio_direction_input(SHUTDOWN_PORT);
	gpio_free(SHUTDOWN_PORT);
err_alloc_data_failed:
err_check_functionality_failed:
	return ret;
}


static int goodix_ts_remove(struct i2c_client *client)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(client);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
	if (ts->use_irq)
		free_irq(client->irq, ts);
	else
		hrtimer_cancel(&ts->timer);
	
	struct goodix_i2c_rmi_platform_data *pdata = client->dev.platform_data;
	int INT_PORT  = pdata->gpio_irq;
	int SHUTDOWN_PORT  = pdata->gpio_shutdown;
	gpio_direction_input(SHUTDOWN_PORT);
	gpio_free(SHUTDOWN_PORT);

	if (ts->use_irq)
		gpio_free(INT_PORT);
	
	dev_notice(&client->dev,"The driver is removing...\n");
	i2c_set_clientdata(client, NULL);
	input_unregister_device(ts->input_dev);
	if(ts->input_dev)
		kfree(ts->input_dev);
	kfree(ts);
	return 0;
}

static int goodix_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	if (ts->use_irq)
		disable_irq(client->irq);
	else
		hrtimer_cancel(&ts->timer);
	ret = cancel_work_sync(&ts->work);	
	if (ts->power) {
		ret = ts->power(0);
		if (ret < 0)
			printk(KERN_ERR "goodix_ts_resume power off failed\n");
	}
	return 0;
}

static int goodix_ts_resume(struct i2c_client *client)
{
	int ret;
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	if (ts->power) {
		ret = ts->power(1);
		if (ret < 0)
			printk(KERN_ERR "goodix_ts_resume power on failed\n");
	}

	if (ts->use_irq)
		enable_irq(client->irq);
	if (!ts->use_irq)
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_early_suspend(struct early_suspend *h)
{
	struct goodix_ts_data *ts;
	ts = container_of(h, struct goodix_ts_data, early_suspend);
	ts->suspend_state = 1;
	goodix_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void goodix_ts_late_resume(struct early_suspend *h)
{
	struct goodix_ts_data *ts;
	ts = container_of(h, struct goodix_ts_data, early_suspend);
	ts->suspend_state = 0;
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
	printk(KERN_DEBUG "Touchscreen driver of guitar is installing...\n");
	goodix_wq = create_workqueue("goodix_wq");
	if (!goodix_wq) {
		printk(KERN_ALERT "creat workqueue faiked\n");
		return -ENOMEM;
		
	}
	ret=i2c_add_driver(&goodix_ts_driver);
	return ret; 
}

static void __exit goodix_ts_exit(void)
{
	printk(KERN_DEBUG "Touchscreen driver of guitar is exiting...\n");
	i2c_del_driver(&goodix_ts_driver);
	if (goodix_wq)
		destroy_workqueue(goodix_wq);
}

#ifdef CONFIG_DEFERRED_MODULE_INIT
deferred_late_init(goodix_ts_init);
#else
late_initcall(goodix_ts_init);
#endif
module_exit(goodix_ts_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Driver");
MODULE_LICENSE("GPL");
