/* SPDX-License-Identifier: GPL-2.0 */
/*---------------------------------------------------------------------------------------------------------
 * driver/input/touchscreen/goodix_touch.c
 *
 * Copyright(c) 2010 Goodix Technology Corp. All rights reserved.      
 * Author: Eltonny
 * Date: 2010.11.11                                    
 *                                                                                                         
 *---------------------------------------------------------------------------------------------------------*/
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
//#include <mach/gpio.h>
//#include <plat/gpio-cfg.h>
//#include <plat/gpio-bank-l.h>
//#include <plat/gpio-bank-f.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <mach/iomux.h>
#include <linux/goodix_touch.h>
#include <linux/goodix_queue.h>

#ifndef GUITAR_GT80X
#error The code does not match the hardware version.
#endif

static struct workqueue_struct *goodix_wq;

/********************************************
*	管理当前手指状态的伪队列，对当前手指根据时间顺序排序
*	适用于Guitar小屏		*/
static struct point_queue  finger_list;	//record the fingers list 
/*************************************************/

const char *rk29_ts_name = "Goodix TouchScreen of GT80X";
/*used by guitar_update module */
struct i2c_client * i2c_connect_client = NULL;
EXPORT_SYMBOL(i2c_connect_client);
	
#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_early_suspend(struct early_suspend *h);
static void goodix_ts_late_resume(struct early_suspend *h);
#endif

/*read the gt80x register ,used i2c bus*/
static int gt80x_read_regs(struct i2c_client *client, u8 reg, u8 buf[], unsigned len)
{
	int ret;
	ret =i2c_master_reg8_recv(client, reg, buf, len, 200*1000);
	if(ret < 0)
		dev_err(&client->dev,"i2c_read fail =%d\n",ret);
	return ret;
}
/* set the gt80x registe,used i2c bus*/
static int gt80x_write_regs(struct i2c_client *client, u8 reg, u8 const buf[], unsigned short len)
{
	int ret;
	ret = i2c_master_reg8_send(client,reg, buf, len, 200*1000);
 	if (ret < 0) {
		dev_err(&client->dev,"i2c_write fail =%d\n",ret);
    }
	return ret;
}
#if 0
/*******************************************************	
功能：	
	读取从机数据
	每个读操作用两条i2c_msg组成，第1条消息用于发送从机地址，
	第2条用于发送读取地址和取回数据；每条消息前发送起始信号
参数：
	client:	i2c设备，包含设备地址
	buf[0]：	首字节为读取地址
	buf[1]~buf[len]：数据缓冲区
	len：	读取数据长度
return：
	执行消息数
*********************************************************/
/*Function as i2c_master_send */
static int i2c_read_bytes(struct i2c_client *client, uint8_t *buf, int len)
{
	struct i2c_msg msgs[2];
	int ret=-1;
	//发送写地址
	msgs[0].flags=!I2C_M_RD;//写消息
	msgs[0].addr=client->addr;
	msgs[0].len=1;
	msgs[0].buf=&buf[0];
	//接收数据
	msgs[1].flags=I2C_M_RD;//读消息
	msgs[1].addr=client->addr;
	msgs[1].len=len-1;
	msgs[1].buf=&buf[1];
	
	ret=i2c_transfer(client->adapter,msgs,2);
	return ret;
}

/*******************************************************	
功能：
	向从机写数据
参数：
	client:	i2c设备，包含设备地址
	buf[0]：	首字节为写地址
	buf[1]~buf[len]：数据缓冲区
	len：	数据长度	
return：
	执行消息数
*******************************************************/
/*Function as i2c_master_send */
static int i2c_write_bytes(struct i2c_client *client,uint8_t *data,int len)
{
	struct i2c_msg msg;
	int ret=-1;
	//发送设备地址
	msg.flags=!I2C_M_RD;//写消息
	msg.addr=client->addr;
	msg.len=len;
	msg.buf=data;		
	
	ret=i2c_transfer(client->adapter,&msg,1);
	return ret;
}
#endif
/*******************************************************
功能：
	Guitar初始化函数，用于发送配置信息，获取版本信息
参数：
	ts:	client私有数据结构体
return：
	执行结果码，0表示正常执行
*******************************************************/
static int goodix_init_panel(struct goodix_ts_data *ts)
{
	int i, ret = -1;
	u8 start_reg = 0x30;
	u8 buf[53];
	uint8_t config_info[53] = {
		0x13,0x05,0x04,0x28,0x02,0x14,0x14,0x10,0x50,0xBA,
		0x14,0x00,0x1E,0x00,0x01,0x23,0x45,0x67,0x89,0xAB,
		0xCD,0xE1,0x00,0x00,0x00,0x00,0x4D,0xC1,0x20,0x01,
		0x01,0x83,0x50,0x3C,0x1E,0xB4,0x00,0x33,0x2C,0x01,
		0xEC,0x3C,0x64,0x32,0x71,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x01
	};

	ret = gt80x_write_regs(ts->client, start_reg, config_info, 53);
	if (ret < 0) {
		printk("download gt80x firmware err %d\n", ret);
		goto error_i2c_transfer;
	}
	msleep(10);
#if 0
	ret = gt80x_read_regs(ts->client, start_reg, buf, 53);
	if (ret < 0)
	{
		printk("\n--%s--Read Register values error !!!\n",__FUNCTION__);
	}
		
	for (i = 0; i < 53; i++)
	{
		if (buf[i] != config_info[i])
		{
			dev_err(&ts->client->dev,"may be i2c errorat reg_add[%d] = %#x current value = %#x expect value = %#x\n",
					 i, 0x30 + i, buf[i], config_info[i]);
			break;
		}
	}
#endif
	return 0;

error_i2c_transfer:
	return ret;
}
/*读取GT80X的版本号并打印*/
static int  goodix_read_version(struct goodix_ts_data *ts)
{
	int ret;
	uint8_t reg1 = 0x69;
	uint8_t reg2 = 0x6a;
	//uint8_t version[2]={0x69,0xff};	//command of reading Guitar's version 
	uint8_t version[1]={0xff};	//command of reading Guitar's version 
	//uint8_t version_data[41];		//store touchscreen version infomation
	uint8_t version_data[40];		//store touchscreen version infomation
	memset(version_data, 0 , sizeof(version_data));
	//version_data[0]=0x6A;
	//ret=i2c_write_bytes(ts->client,version,2);
	ret = gt80x_write_regs(ts->client, reg1, version, 1);
	if (ret < 0) 
		goto error_i2c_version;
	msleep(16);

	//ret=i2c_read_bytes(ts->client,version_data, 40);
	ret = gt80x_read_regs(ts->client, reg2, version_data, 40);
	if (ret < 0) 
		goto error_i2c_version;
	//dev_info(&ts->client->dev," Guitar Version: %s\n", &version_data[1]);
	printk("%s: Guitar Version: %s\n", __func__, version_data);

	version[0] = 0x00;				//cancel the command
	//i2c_write_bytes(ts->client, version, 2);
	ret = gt80x_write_regs(ts->client, reg1, version, 1);
	return 0;
	
error_i2c_version:
	return ret;
}

/*******************************************************	
功能：
	触摸屏工作函数
	由中断触发，接受1组坐标数据，校验后再分析输出
参数：
	ts:	client私有数据结构体
return：
	执行结果码，0表示正常执行
********************************************************/
static void goodix_ts_work_func(struct work_struct *work)
{	
	static uint8_t finger_bit = 0;	//last time fingers' state
	uint8_t read_position = 0;
	uint8_t point_data[35] = {0};
	uint8_t finger = 0;				//record which finger is changed
	int ret = -1; 
	int count = 0;
	int check_sum = 0;
	//unsigned char start_reg = 0x00;
	struct goodix_ts_data *ts = container_of(work, struct goodix_ts_data, work);
	struct goodix_i2c_rmi_platform_data *pdata = ts->client->dev.platform_data;

//#ifdef SHUTDOWN_PORT
	if (pdata && pdata->shutdown_pin) {	
		//if (gpio_get_value(SHUTDOWN_PORT))
		if (gpio_get_value(pdata->shutdown_pin))
		{
			//printk(KERN_ALERT  "Guitar stop working.The data is invalid. \n");
			goto NO_ACTION;
		}
	}
//#endif

	//if i2c transfer is failed, let it restart less than 10 times
	if (ts->retry > 9) {
		if(!ts->use_irq && (ts->timer.state != HRTIMER_STATE_INACTIVE))		
			hrtimer_cancel(&ts->timer);
		dev_info(&(ts->client->dev), "Because of transfer error, %s stop working.\n",rk29_ts_name);
		return ;
	}
	if(ts->bad_data) 
		msleep(16);

	ret = gt80x_read_regs(ts->client, point_data[0], &point_data[1], 34);
	if(ret <= 0)	
	{
		//dev_err(&(ts->client->dev),"I2C transfer error. Number:%d\n ", ret);
		ts->bad_data = 1;
		ts->retry++;
		if(ts->power)
		{
			ts->power(ts, 0);
			ts->power(ts, 1);
		}
		else
		{
			goodix_init_panel(ts);
			msleep(500);
		}
		goto XFER_ERROR;
	}	
	ts->bad_data = 0; 
	
	//The bit indicate which fingers pressed down
	switch (point_data[1] & 0x1f)
	{
		case 0:
		case 1:
			for (count = 1; count < 8; count++)
				check_sum += (int)point_data[count];
			if ((check_sum % 256) != point_data[8])
				goto XFER_ERROR;
			break;
		case 2:
		case 3:
			for ( count = 1; count < 13; count++)
				check_sum += (int)point_data[count];
			if ((check_sum % 256) != point_data[13])
				goto XFER_ERROR;
			break;	
		default:		//(point_data[1]& 0x1f) > 3
			for (count = 1; count < 34; count++)
				check_sum += (int)point_data[count];
			if ((check_sum % 256) != point_data[34])
				goto XFER_ERROR;
	}
	
	point_data[1] &= 0x1f;
	finger = finger_bit ^ point_data[1];
	if (finger == 0 && point_data[1] == 0)			
		goto NO_ACTION;						//no fingers and no action
	else if(finger == 0)							//the same as last time
		goto BIT_NO_CHANGE;					
	//check which point(s) DOWN or UP
	for (count = 0; (finger != 0) && (count < MAX_FINGER_NUM);  count++)
	{
		if ((finger & 0x01) == 1)		//current bit is 1, so NO.postion finger is change
		{							
			if (((finger_bit >> count) & 0x01) ==1 )	//NO.postion finger is UP
				set_up_point(&finger_list, count);
			else 
				add_point(&finger_list, count);
		}
		finger >>= 1;
	}

BIT_NO_CHANGE:
	for(count = 0; count < finger_list.length; count++)
	{	
		if(finger_list.pointer[count].state == FLAG_UP)
		{
			finger_list.pointer[count].x = finger_list.pointer[count].y = 0;
			finger_list.pointer[count].pressure = 0;
			continue;
		}
		
		if(finger_list.pointer[count].num < 3)
			read_position = finger_list.pointer[count].num*5 + 3;
		else if (finger_list.pointer[count].num == 4)
			read_position = 29;

		if(finger_list.pointer[count].num != 3)
		{
			finger_list.pointer[count].x = (unsigned int) (point_data[read_position]<<8) + (unsigned int)( point_data[read_position+1]);
			finger_list.pointer[count].y = (unsigned int)(point_data[read_position+2]<<8) + (unsigned int) (point_data[read_position+3]);
			finger_list.pointer[count].pressure = (unsigned int) (point_data[read_position+4]);
		}
		else 
		{
			finger_list.pointer[count].x = (unsigned int) (point_data[18]<<8) + (unsigned int)( point_data[25]);
			finger_list.pointer[count].y = (unsigned int)(point_data[26]<<8) + (unsigned int) (point_data[27]);
			finger_list.pointer[count].pressure = (unsigned int) (point_data[28]);
		}

		// 将触摸屏的坐标映射到LCD坐标上. 触摸屏短边为X轴，LCD坐标一般长边为X轴，可能需要调整原点位置
		finger_list.pointer[count].x = (TOUCH_MAX_WIDTH - finger_list.pointer[count].x)*SCREEN_MAX_WIDTH/TOUCH_MAX_WIDTH;//y
		finger_list.pointer[count].y =  finger_list.pointer[count].y*SCREEN_MAX_HEIGHT/TOUCH_MAX_HEIGHT ;				//x
		gt80xy_swap(finger_list.pointer[count].x, finger_list.pointer[count].y); 

		//printk("%s: dx = %d dy = %d\n", __func__,
		//	   finger_list.pointer[count].x, finger_list.pointer[count].y);

	}

#ifndef GOODIX_MULTI_TOUCH	
		if(finger_list.pointer[0].state == FLAG_DOWN)
		{
			input_report_abs(ts->input_dev, ABS_X, finger_list.pointer[0].x);
			input_report_abs(ts->input_dev, ABS_Y, finger_list.pointer[0].y);	
		} 
		input_report_abs(ts->input_dev, ABS_PRESSURE, pressure[0]);
		input_report_key(ts->input_dev, BTN_TOUCH, finger_list.pointer[0].state);   
#else

	/* ABS_MT_TOUCH_MAJOR is used as ABS_MT_PRESSURE in android. */
	for(count = 0; count < (finger_list.length); count++)
	{
		if(finger_list.pointer[count].state == FLAG_DOWN)
		{
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, finger_list.pointer[count].x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, finger_list.pointer[count].y);
		} 
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, finger_list.pointer[count].pressure);
		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, finger_list.pointer[count].pressure);
		input_mt_sync(ts->input_dev);	
	}
	#if 0
	read_position = finger_list.length-1;
	if(finger_list.length > 0 && finger_list.pointer[read_position].state == FLAG_DOWN)
	{
		input_report_abs(ts->input_dev, ABS_MT_POSITION_X, finger_list.pointer[read_position].x);
		input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, finger_list.pointer[read_position].y);
	}	
	input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, finger_list.pointer[read_position].pressure);
	input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, finger_list.pointer[read_position].pressure);
	input_mt_sync(ts->input_dev);
	#endif	
#endif
	input_sync(ts->input_dev);

	del_point(&finger_list);
	finger_bit=point_data[1];
	
XFER_ERROR:	
NO_ACTION:
	if(ts->use_irq)
		enable_irq(ts->client->irq);

}

/*******************************************************	
功能：
	计时器响应函数
	由计时器触发，调度触摸屏工作函数运行；之后重新计时
参数：
	timer：函数关联的计时器	
return：
	计时器工作模式，HRTIMER_NORESTART表示不需要自动重启
********************************************************/
static enum hrtimer_restart goodix_ts_timer_func(struct hrtimer *timer)
{
	struct goodix_ts_data *ts = container_of(timer, struct goodix_ts_data, timer);
	queue_work(goodix_wq, &ts->work);
	if(ts->timer.state != HRTIMER_STATE_INACTIVE)
		hrtimer_start(&ts->timer, ktime_set(0, 16000000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

/*******************************************************	
功能：
	中断响应函数
	由中断触发，调度触摸屏处理函数运行
参数：
	timer：函数关联的计时器	
return：
	计时器工作模式，HRTIMER_NORESTART表示不需要自动重启
********************************************************/
//#if defined(INT_PORT)
static irqreturn_t goodix_ts_irq_handler(int irq, void *dev_id)
{
	struct goodix_ts_data *ts = dev_id;
	
	disable_irq_nosync(ts->client->irq);
	queue_work(goodix_wq, &ts->work);
	
	return IRQ_HANDLED;
}
//#endif

/*******************************************************	
功能：
	GT80X的电源管理
参数：
	on:设置GT80X运行模式，0为进入Sleep模式
return：
	是否设置成功，小于0表示设置失败
********************************************************/
//#if defined(SHUTDOWN_PORT)
static int goodix_ts_power(struct goodix_ts_data * ts, int on)
{
	int ret = -1;
	struct goodix_i2c_rmi_platform_data *pdata = NULL;

	if(ts == NULL || (ts && !ts->use_shutdown))
		return -1;

	pdata = ts->client->dev.platform_data;

	switch(on) 
	{
		case 0:
			gpio_set_value(pdata->shutdown_pin, 1);
			msleep(5);
			if(gpio_get_value(pdata->shutdown_pin))	//has been suspend
				ret = 0;
			break;
		case 1:
			gpio_set_value(pdata->shutdown_pin, 0);
			msleep(5);
			if(gpio_get_value(pdata->shutdown_pin))	//has been suspend
				ret = -1;
			else 
			{
				ret = goodix_init_panel(ts);
                if(!ret)
					msleep(500);
			}
			break;	
		//default:printk("Command ERROR.\n");
	}
	printk(on?"Set Guitar's Shutdown LOW\n":"Set Guitar's Shutdown HIGH\n");
	return 0;
}
//#endif

/*******************************************************	
功能：
	触摸屏探测函数
	在注册驱动时调用（要求存在对应的client）；
	用于IO,中断等资源申请；设备注册；触摸屏初始化等工作
参数：
	client：待驱动的设备结构体
	id：设备ID
return：
	执行结果码，0表示正常执行
********************************************************/
static int goodix_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct goodix_ts_data *ts;
	int ret = 0;
	int retry=0;
	int count=0;
	u8 test_reg = 0x30;
	u8 test_buf[] = {0x01};

	struct goodix_i2c_rmi_platform_data *pdata;
	pdata = client->dev.platform_data;

	dev_dbg(&client->dev,"Install touchscreen driver for guitar.\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&client->dev, "System need I2C function.\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}
	
	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}
	//Check I2C function
//#ifdef SHUTDOWN_PORT	
	if (pdata && pdata->shutdown_pin) {
		//ret = gpio_request(SHUTDOWN_PORT, "TS_SHUTDOWN");	//Request IO
		ret = gpio_request(pdata->shutdown_pin, "TS_SHUTDOWN");	//Request IO
		if (ret < 0) 
		{
			//printk(KERN_ALERT "Failed to request GPIO:%d, ERRNO:%d\n",(int)SHUTDOWN_PORT,ret);
			printk(KERN_ALERT "Failed to request GPIO:%d, ERRNO:%d\n",(int)pdata->shutdown_pin,ret);
			goto err_gpio_request;
		}	
		//gpio_direction_output(SHUTDOWN_PORT, 0);	//Touchscreen is waiting to wakeup
		//ret = gpio_get_value(SHUTDOWN_PORT);
		gpio_direction_output(pdata->shutdown_pin, 0);	//Touchscreen is waiting to wakeup
		ret = gpio_get_value(pdata->shutdown_pin);
		if (ret)
		{
			printk(KERN_ALERT  "Cannot set touchscreen to work.\n");
			goto err_i2c_failed;
		}
	}
//#endif		
	
	i2c_connect_client = client;				//used by Guitar Updating.
	msleep(16);

	for (retry = 0; retry < 5; retry++)
	{
		ret = gt80x_write_regs(client, test_reg, test_buf, 1);	//Test i2c.
		if (ret > 0)
			break;
	}
	if (ret < 0)
	{
		dev_err(&client->dev, "Warnning: I2C connection might be something wrong!\n");
		goto err_i2c_failed;
	}

//#ifdef SHUTDOWN_PORT	
	if (pdata && pdata->shutdown_pin) {
		ts->use_shutdown = 1;
		//gpio_set_value(SHUTDOWN_PORT, 1);
		gpio_set_value(pdata->shutdown_pin, 1);
	}//suspend
//#endif	
	
	INIT_WORK(&ts->work, goodix_ts_work_func);
	ts->client = client;
	i2c_set_clientdata(client, ts);
	//pdata = client->dev.platform_data;
	
	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		dev_dbg(&client->dev,"Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}

	ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) ;
#ifndef GOODIX_MULTI_TOUCH	
	ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	ts->input_dev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y);
	//	| BIT_MASK(ABS_MT_TOUCH_MAJOR)| BIT_MASK(ABS_MT_WIDTH_MAJOR)
  	//	BIT_MASK(ABS_MT_POSITION_X) |
  	//	BIT_MASK(ABS_MT_POSITION_Y); 	// for android

	input_set_abs_params(ts->input_dev, ABS_X, 0, SCREEN_MAX_HEIGHT, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, 0, SCREEN_MAX_WIDTH, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, 0, 0);	
#else
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, SCREEN_MAX_HEIGHT, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, SCREEN_MAX_WIDTH, 0, 0);	
#endif	

	sprintf(ts->phys, "input/ts)");
	ts->input_dev->name = rk29_ts_name;
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

	ts->use_irq = 0;
	ts->retry = 0;
	ts->bad_data = 0;
//#if defined(INT_PORT)	
	//client->irq=TS_INT;
	//gpio_set_value(TS_INT, 0);		
	
	if (pdata && pdata->irq_pin) {
		client->irq = gpio_to_irq(client->irq);
		if (client->irq)
		{
			//ret = gpio_request(INT_PORT, "TS_INT");	//Request IO
			ret = gpio_request(pdata->irq_pin, "TS_INT");	//Request IO
			if (ret < 0) 
			{
				//dev_err(&client->dev, "Failed to request GPIO:%d, ERRNO:%d\n",(int)INT_PORT,ret);
				dev_err(&client->dev, "Failed to request GPIO:%d, ERRNO:%d\n", (int)pdata->irq_pin, ret);
				goto err_int_request_failed;
			}
			//ret = s3c_gpio_cfgpin(INT_PORT, INT_CFG);	//Set IO port function
			ret  = request_irq(client->irq, goodix_ts_irq_handler ,  IRQ_TYPE_EDGE_RISING,
				client->name, ts);
			if (ret != 0) {
				dev_err(&client->dev,"Can't allocate touchscreen's interrupt!ERRNO:%d\n", ret);
				//gpio_direction_input(INT_PORT);
				//gpio_free(INT_PORT);
				gpio_direction_input(pdata->irq_pin);
				gpio_free(pdata->irq_pin);
				goto err_int_request_failed;
			}
			else 
			{	
				//disable_irq(TS_INT);
				disable_irq(client->irq);
				ts->use_irq = 1;
				dev_dbg(&client->dev,"Reques EIRQ %d succesd on GPIO:%d\n",client->irq, pdata->irq_pin);
			}
		}
	}
//#endif

err_int_request_failed:	
	if (!ts->use_irq) 
	{
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = goodix_ts_timer_func;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}

//#ifdef SHUTDOWN_PORT		
	if (pdata && pdata->shutdown_pin) {
		//gpio_set_value(SHUTDOWN_PORT, 0);
		gpio_set_value(pdata->shutdown_pin, 0);
		msleep(10);
		ts->power = goodix_ts_power;
	}
//#endif	
	for(count = 0; count < 3; count++)
	{
		ret = goodix_init_panel(ts);	
		if(ret != 0)		//Initiall failed
			continue;
		else
		{
			if(ts->use_irq)
				enable_irq(client->irq);
				//enable_irq(TS_INT);
			break;
		}
	}
	if(ret != 0) {
		ts->bad_data = 1;
		goto err_init_godix_ts;
	}
	goodix_read_version(ts);
	msleep(500);
	
#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = goodix_ts_early_suspend;
	ts->early_suspend.resume = goodix_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif
	dev_info(&client->dev,"Start  %s in %s mode\n", 
		ts->input_dev->name, ts->use_irq ? "Interrupt" : "Polling");
	return 0;

err_init_godix_ts:
	if(ts->use_irq)
	{
		//free_irq(TS_INT,ts);
		free_irq(client->irq,ts);
//#if defined(INT_PORT)		
		if (pdata && pdata->irq_pin)
			gpio_free(pdata->irq_pin);
//#endif
	}

err_input_register_device_failed:
	input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
	i2c_set_clientdata(client, NULL);
err_i2c_failed:
//#ifdef SHUTDOWN_PORT	
	if (pdata && pdata->shutdown_pin) {
		//gpio_direction_input(SHUTDOWN_PORT);
		//gpio_free(SHUTDOWN_PORT);
		gpio_direction_input(pdata->shutdown_pin);
		gpio_free(pdata->shutdown_pin);
	}
//#endif
err_gpio_request:
	kfree(ts);
err_alloc_data_failed:
err_check_functionality_failed:
	return ret;
}


/*******************************************************	
功能：
	驱动资源释放
参数：
	client：设备结构体
return：
	执行结果码，0表示正常执行
********************************************************/
static int goodix_ts_remove(struct i2c_client *client)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(client);
	struct goodix_i2c_rmi_platform_data *pdata;
	pdata = client->dev.platform_data;
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
	if (ts->use_irq)
	{
		free_irq(client->irq, ts);
//#if defined(INT_PORT)		
		if (pdata && pdata->irq_pin)
			gpio_free(pdata->irq_pin);
			//gpio_free(INT_PORT);
//#endif
	}	
	else
		hrtimer_cancel(&ts->timer);

//#ifdef SHUTDOWN_PORT		
	if (pdata && pdata->shutdown_pin) {
		if(ts->use_shutdown)
		{
			//gpio_direction_input(SHUTDOWN_PORT);
			//gpio_free(SHUTDOWN_PORT);
			gpio_direction_input(pdata->shutdown_pin);
			gpio_free(pdata->shutdown_pin);
		}
	}
//#endif
	dev_notice(&client->dev,"The driver is removing...\n");
	i2c_set_clientdata(client, NULL);
	input_unregister_device(ts->input_dev);
	if(ts->input_dev)
		kfree(ts->input_dev);
	kfree(ts);
	return 0;
}

//停用设备
static int goodix_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	if (ts->use_irq)
		disable_irq(client->irq);
	else if(ts->timer.state)
		hrtimer_cancel(&ts->timer);
	ret = cancel_work_sync(&ts->work);	
	if(ret && ts->use_irq)
		enable_irq(client->irq);
	if (ts->power) {
		ret = ts->power(ts,0);
		if (ret < 0)
			printk(KERN_ERR "%s power off failed\n", rk29_ts_name);
	}
	return 0;
}
//重新唤醒
static int goodix_ts_resume(struct i2c_client *client)
{
	int ret;
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	if (ts->power) {
		ret = ts->power(ts, 1);
		if (ret < 0)
			printk(KERN_ERR "%s power on failed\n", rk29_ts_name);
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
	struct goodix_ts_data *ts;
	ts = container_of(h, struct goodix_ts_data, early_suspend);
	goodix_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void goodix_ts_late_resume(struct early_suspend *h)
{
	struct goodix_ts_data *ts;
	ts = container_of(h, struct goodix_ts_data, early_suspend);
	goodix_ts_resume(ts->client);
}
#endif

//可用于该驱动的 设备名—设备ID 列表
//only one client
static const struct i2c_device_id goodix_ts_id[] = {
	{ GOODIX_I2C_NAME, 0 },
	{ }
};

//设备驱动结构体
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

/*******************************************************	
功能：
	驱动加载函数
return：
	执行结果码，0表示正常执行
********************************************************/
static int __devinit goodix_ts_init(void)
{
	int ret;
	//printk(KERN_DEBUG "%s is installing...\n", rk29_ts_name);
	goodix_wq = create_workqueue("goodix_wq");
	if (!goodix_wq) {
		printk(KERN_ALERT "Creat workqueue faiked\n");
		return -ENOMEM;
		
	}
	ret=i2c_add_driver(&goodix_ts_driver);
	return ret; 
}

/*******************************************************	
功能：
	驱动卸载函数
参数：
	client：设备结构体
********************************************************/
static void __exit goodix_ts_exit(void)
{
	printk(KERN_DEBUG "%s is exiting...\n", rk29_ts_name);
	i2c_del_driver(&goodix_ts_driver);
	if (goodix_wq)
		destroy_workqueue(goodix_wq);
}

late_initcall(goodix_ts_init);
module_exit(goodix_ts_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Driver");
MODULE_LICENSE("GPL");
