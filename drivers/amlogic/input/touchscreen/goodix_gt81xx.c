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
#include "linux/amlogic/input/common.h"
#include "linux/amlogic/input/goodix_gt81xx.h"

#define LATE_UPGRADE
static struct touch_pdata *g_pdata = NULL;
extern struct touch_pdata *ts_com;
static void gt81xx_power(struct touch_pdata *pdata, int on)
{
  if (pdata->gpio_power) {
    set_power_pin(pdata, on);
    msleep(5);
  }
}

static void gt81xx_reset(struct touch_pdata *pdata)
{
  if (pdata->gpio_reset) {
    set_reset_pin(pdata, 0);
    msleep(10);
		set_reset_pin(pdata, 1);
    msleep(2);
  }
}

int g_enter_isp = 0;
//static int RST_PORT = 0;

static struct workqueue_struct *goodix_wq;
static struct i2c_client * i2c_connect_client = NULL;
//static struct proc_dir_entry *goodix_proc_entry;
static struct kobject *goodix_debug_kobj;

/*******************************************************
Description:
	Read data from the i2c slave device;
	This operation consisted of 2 i2c_msgs,the first msg used
	to write the operate address,the second msg used to read data.

Parameter:
	client:	i2c device.
	buf[0]:operate address.
	buf[1]~buf[len]:read data buffer.
	len:operate length.

return:
	numbers of i2c_msgs to transfer
*********************************************************/
static int i2c_read_bytes(struct i2c_client *client, uint8_t *buf, int len)
{
	struct i2c_msg msgs[2];
	int ret=-1;
	int retries = 0;

	msgs[0].flags=!I2C_M_RD;
	msgs[0].addr=client->addr;
	msgs[0].len=1;
	msgs[0].buf=&buf[0];

	msgs[1].flags=I2C_M_RD;
	msgs[1].addr=client->addr;
	msgs[1].len=len-1;
	msgs[1].buf=&buf[1];

	while(retries<5)
	{
		ret=i2c_transfer(client->adapter,msgs, 2);
		if(!(ret <= 0))break;
		retries++;
	}
	return ret;
}

/*******************************************************
Description:
	write data to the i2c slave device.

Parameter:
	client:	i2c device.
	buf[0]:operate address.
	buf[1]~buf[len]:write data buffer.
	len:operate length.

return:
	numbers of i2c_msgs to transfer.
*********************************************************/
static int i2c_write_bytes(struct i2c_client *client,uint8_t *data,int len)
{
	struct i2c_msg msg;
	int ret=-1;
	int retries = 0;

	msg.flags=!I2C_M_RD;
	msg.addr=client->addr;
	msg.len=len;
	msg.buf=data;

	while(retries<5)
	{
		ret=i2c_transfer(client->adapter,&msg, 1);
		if(ret == 1)break;
		retries++;
	}
	return ret;
}

inline void int_wakeup_green(struct goodix_ts_data *ts,uint8_t disable)
{
//	int INT_PORT  = pdata->gpio_irq;

	if(!ts->green_wake_mode)return;
	printk("int wakeup green!\n");
	if(disable)
		disable_irq(ts->client->irq);
	//gpio_set_status(INT_PORT, gpio_status_out);
	aml_gpio_direction_output(g_pdata->gpio_interrupt, 0);
	msleep(5);
	//gpio_set_status(INT_PORT, gpio_status_in);
	aml_gpio_direction_input(g_pdata->gpio_interrupt);
	if(disable)
		enable_irq(ts->client->irq);
}

/*******************************************************
Description:
	Goodix touchscreen initialize function.

Parameter:
	ts:	i2c client private struct.

return:
	Executive outcomes.0---succeed.
*******************************************************/
#define READ_COUNT  5
static int goodix_init_panel(struct goodix_ts_data *ts)
{
	int ret=-1;
	uint8_t rd_cfg_buf[7] = {0x66,};
	u32 offset = 0,count = 0;
#ifdef DRIVER_SEND_CFG
#ifdef CONFIG_OF
	uint8_t *cfg_info_group1 = NULL;
#else
	uint8_t cfg_info_group1[] = {0x65,
							0x03,0x04,0x00,0x03,0x00,0x0A,0x21,0x1E,0xE7,0x32,0x02,0x05,0x10,0x4C,0x4F,0x4F,
							0x20,0x07,0x00,0x80,0x80,0x3C,0x5A,0x0E,0x0D,0x0C,0x0B,0x0A,0x09,0x08,0x07,0x06,
							0x05,0x04,0x03,0x02,0x01,0x00,0x1D,0x1C,0x1B,0x1A,0x19,0x18,0x17,0x16,0x15,0x14,
							0x13,0x12,0x11,0x10,0x0F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
							0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
							0x00,0x00,0x00,0x00,0x00};
#endif
	uint8_t cfg_info_group2[] = {
	//TODO puts your group2 config info here,if need.
	};
	uint8_t cfg_info_group3[] = {
	//TODO puts your group3 config info here,if need.
	};
	uint8_t cfg_info_group4[] = {
	//TODO puts your group4 config info here,if need.
	};
	uint8_t * send_cfg_buf[4] = {cfg_info_group1, cfg_info_group2, cfg_info_group3, cfg_info_group4};
	uint8_t cfg_info_len[4] = {count,
							   sizeof(cfg_info_group2)/sizeof(cfg_info_group2[0]),
							   sizeof(cfg_info_group3)/sizeof(cfg_info_group3[0]),
							   sizeof(cfg_info_group4)/sizeof(cfg_info_group4[0])};

#ifdef CONFIG_OF
	int file_size;
	uint8_t *config_info = NULL;
	uint8_t tmp[READ_COUNT];
	int i_ret;

	file_size = touch_open_fw(g_pdata->config_file);
	if(file_size < 0) {
			printk("%s: no fw file\n", ts_com->owner);
			return -1;
	}

	if (config_info == NULL)
		config_info = kzalloc(sizeof(*config_info)*(file_size/5), GFP_KERNEL);
	if (config_info == NULL) {
		printk("Insufficient memory in upgrade!\n");
		return -1;
	}

	while (offset < file_size) {
	memset(tmp, 0, READ_COUNT);
    touch_read_fw(offset, min_t(int,file_size-offset,READ_COUNT), &tmp[0]);
    i_ret = sscanf(&tmp[0],"0x%x,",(int *)(config_info + count));
    if (i_ret == 1) {
			count++;
			offset += READ_COUNT;
		}
	else
    offset++;
	}

	touch_close_fw();

	cfg_info_group1 = config_info;

	printk("cfg_info_len1 = %d\n",count);
//	int i=0;
//	for (i=0; i<80; i++ ) {
//		printk("%x ", cfg_info_group1[i]);
//	}
//	printk("\n");
	cfg_info_group1[2] = g_pdata->xres >> 8;
	cfg_info_group1[3] = g_pdata->xres & 0xff;
	cfg_info_group1[4] = g_pdata->yres >> 8;
	cfg_info_group1[5] = g_pdata->yres & 0xff;
	cfg_info_group1[6] = g_pdata->max_num;
	cfg_info_group1[7] = (g_pdata->irq_edge & 0x1) | (cfg_info_group1[7] & ~0x03);
	send_cfg_buf[0] = cfg_info_group1;
#else
	count	= sizeof(cfg_info_group1)/sizeof(cfg_info_group1[0]);
#endif
	cfg_info_len[0] = count;
	printk("len1=%d,len2=%d,len3=%d,len4=%d\n", cfg_info_len[0],cfg_info_len[1],cfg_info_len[2],cfg_info_len[3]);
	if((!cfg_info_len[1])&&(!cfg_info_len[2])&&(!cfg_info_len[3]))
	{
		rd_cfg_buf[1] = 0x00;
		goto send_cfg;
	}
	rd_cfg_buf[0] = 0x52;		//read SCREEN_FLAG(82:Bit1~Bit0)
	rd_cfg_buf[1] = 0x00;
	ret=i2c_read_bytes(ts->client, rd_cfg_buf, 2);
	if(ret <= 0)
	{
		printk("Read screen_flag failed,default use group1 config!\n");
		rd_cfg_buf[1] = 0x00;
	}
send_cfg:
	rd_cfg_buf[1] &= 0x03;
	printk("Screen Type:%d\n", rd_cfg_buf[1]);
	ret=i2c_write_bytes(ts->client, send_cfg_buf[rd_cfg_buf[1]], cfg_info_len[rd_cfg_buf[1]]+1);
#ifdef CONFIG_OF
	if (config_info != NULL) {
		kfree(config_info);
		config_info = NULL;
	}
#endif
	if (ret < 0)
		return ret;
	rd_cfg_buf[0] = 0x66;
#endif
	ret=i2c_read_bytes(ts->client, rd_cfg_buf, 7);
	if(ret <= 0)
	{
		dev_info(&ts->client->dev, "Read resolution & max_touch_num failed, use default value!\n");
		ts->abs_x_max = g_pdata->xres;
		ts->abs_y_max = g_pdata->yres;
		ts->max_touch_num = MAX_FINGER_NUM;
		ts->int_trigger_type = g_pdata->irq_edge & 0x1;
		return 0;
	}
	ts->abs_x_max = (rd_cfg_buf[1]<<8) + rd_cfg_buf[2];
	ts->abs_y_max = (rd_cfg_buf[3]<<8) + rd_cfg_buf[4];
	ts->max_touch_num = rd_cfg_buf[5];
	ts->int_trigger_type = rd_cfg_buf[6]&0x03;
	if((!ts->abs_x_max)||(!ts->abs_y_max)||(!ts->max_touch_num))
	{
		dev_info(&ts->client->dev, "Read invalid resolution & max_touch_num, use default value!\n");
		ts->abs_x_max = g_pdata->xres;
		ts->abs_y_max = g_pdata->yres;
		ts->max_touch_num = MAX_FINGER_NUM;
	}

	dev_info(&ts->client->dev,"X_MAX = %d,Y_MAX = %d,MAX_TOUCH_NUM = %d\n",ts->abs_x_max,ts->abs_y_max,ts->max_touch_num);
	//wake up mode from green mode
	rd_cfg_buf[0] = 0x6e;
	rd_cfg_buf[1] = 0x00;
	i2c_read_bytes(ts->client, rd_cfg_buf, 2);
	if((rd_cfg_buf[1]&0x0f)==0x0f)
	{
		dev_info(&ts->client->dev, "Touchscreen works in INT wake up green mode!\n");
		ts->green_wake_mode = 1;
	}
	else
	{
		dev_info(&ts->client->dev, "Touchscreen works in IIC wake up green mode!\n");
		ts->green_wake_mode = 0;
	}

	msleep(10);
	return 0;

}

/*******************************************************
Description:
	Read goodix touchscreen version function.

Parameter:
	ts:	i2c client private struct.

return:
	Executive outcomes.0---succeed.
*******************************************************/
static int  goodix_read_version(struct goodix_ts_data *ts, char **version)
{
	int ret = -1, count = 0;
	char *version_data;
	char *p;

	*version = (char *)vmalloc(18);
	version_data = *version;
	if(!version_data)
		return -ENOMEM;
	p = version_data;
	memset(version_data, 0, sizeof(version_data));
	version_data[0]=240;
	int_wakeup_green(ts, 1);				//wakeup green mode
	ret=i2c_read_bytes(ts->client,version_data, 17);
	if (ret <= 0)
		return ret;
	version_data[17]='\0';

	if(*p == '\0')
		return 0;
	do
	{
		if((*p > 122) || (*p < 48 && *p != 32) || (*p >57 && *p  < 65)
			||(*p > 90 && *p < 97 && *p  != '_'))		//check illeqal character
			count++;
	}while(*++p != '\0' );
	if(count > 2)
		return 0;
	else
		return 1;
}


/*******************************************************
Description:
	Goodix touchscreen work function.

Parameter:
	ts:	i2c client private struct.

return:
	Executive outcomes.0---succeed.
*******************************************************/
static void goodix_ts_work_func(struct work_struct *work)
{
	int ret=-1;
	int tmp = 0;
	uint8_t  point_data[(1-READ_COOR_ADDR)+1+2+5*MAX_FINGER_NUM+1]={ 0 };  //read address(1byte)+key index(1byte)+point mask(2bytes)+5bytes*MAX_FINGER_NUM+coor checksum(1byte)
	uint8_t  check_sum = 0;
	uint16_t  finger_current = 0;
	uint16_t  finger_bit = 0;
	unsigned int  count = 0, point_count = 0;
	unsigned int position = 0;
	uint8_t track_id[MAX_FINGER_NUM] = {0};
	unsigned int input_x = 0;
	unsigned int input_y = 0;
	unsigned int input_w = 0;
	unsigned char index = 0;
	unsigned char touch_num = 0;

	struct goodix_ts_data *ts = container_of(work, struct goodix_ts_data, work);

//	int INT_PORT  = pdata->gpio_irq;

	if(g_enter_isp)return;
COORDINATE_POLL:
	if((ts->int_trigger_type> 1))//&& (gpio_get_value(INT_PORT) != (ts->int_trigger_type&0x01)))
	{
		goto NO_ACTION;
	}

	if( tmp > 9) {
		dev_info(&(ts->client->dev), "I2C transfer error,touchscreen stop working.\n");
		goto XFER_ERROR ;
	}

	if(ts->bad_data)
		msleep(20);

	point_data[0] = READ_COOR_ADDR;		//read coor address
	ret=i2c_read_bytes(ts->client, point_data,
			((1-READ_COOR_ADDR)+1+2+5*ts->max_touch_num+1));
	if(ret <= 0)
	{
		dev_err(&(ts->client->dev),"I2C transfer error. Number:%d\n ", ret);
		ts->bad_data = 1;
		tmp ++;
		ts->retry++;

		if(ts->int_trigger_type> 1)
			goto COORDINATE_POLL;
		else
			goto XFER_ERROR;
	}
	ts->bad_data = 0;
	finger_current =  (point_data[3 - READ_COOR_ADDR]<<8) + point_data[2 - READ_COOR_ADDR];

	if(finger_current)
	{
		point_count = 0, finger_bit = finger_current;
		for(count = 0; (finger_bit != 0) && (count < ts->max_touch_num); count++)//cal how many point touch currntly
		{
			if(finger_bit & 0x01)
			{
				track_id[point_count] = count;
				point_count++;
			}
			finger_bit >>= 1;
		}
		touch_num = point_count;

		check_sum = point_data[2 - READ_COOR_ADDR] + point_data[3 - READ_COOR_ADDR]; 			//cal coor checksum
		count = 4 - READ_COOR_ADDR;
		for(point_count *= 5; point_count > 0; point_count--)
			check_sum += point_data[count++];
		check_sum += point_data[count];
		if(check_sum  != 0)			//checksum verify error
		{
		#if 0
			dev_info(&ts->client->dev, "Check_sum:%d,  Data:%d\n", check_sum, point_data[count]);
			printk(KERN_INFO "Finger Bit:%d\n",finger_current);
			for( ; count > 0; count--)
				printk(KERN_INFO "count=%d:%d  ",count, point_data[count]);
			printk(KERN_INFO "\n");
		#endif
			printk("coor checksum error!\n");
			if(ts->int_trigger_type> 1)
				goto COORDINATE_POLL;
			else
				goto XFER_ERROR;
		}
	}

	if(touch_num)
	{
		for(index=0; index<touch_num; index++)
		{
			position = 4 - READ_COOR_ADDR + 5*index;
			input_x = (unsigned int) (point_data[position]<<8) + (unsigned int)( point_data[position+1]);
			input_y = (unsigned int)(point_data[position+2]<<8) + (unsigned int) (point_data[position+3]);
			input_w =(unsigned int) (point_data[position+4]);
			//input_x = input_x *SCREEN_MAX_HEIGHT/(TOUCH_MAX_HEIGHT);
			//input_y = input_y *SCREEN_MAX_WIDTH/(TOUCH_MAX_WIDTH);
			touch_dbg("ORI: input_x = %d,input_y = %d, input_w = %d\n", input_x, input_y, input_w);

			if((input_x > ts->abs_x_max)||(input_y > ts->abs_y_max))continue;

			if (g_pdata->pol & 4)
				swap(input_x, input_y);
			if (g_pdata->pol & 1)
				input_x = ts->abs_x_max - input_x;
			if (g_pdata->pol & 2)
				input_y = ts->abs_y_max - input_y;
			touch_dbg("input_x = %d,input_y = %d, input_w = %d\n", input_x, input_y, input_w);
			//input_report_key(ts->input_dev, BTN_TOUCH, 1);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, input_x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, input_y);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, input_w);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, input_w);
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, track_id[index]);
			input_mt_sync(ts->input_dev);
		}
	}
	else
	{
		//input_report_key(ts->input_dev, BTN_TOUCH, 0);
		//input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
		//input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0);
		input_mt_sync(ts->input_dev);
	}

	#ifdef HAVE_TOUCH_KEY
	//printk(KERN_INFO"HAVE KEY DOWN!0x%x\n",point_data[1]);
	for(count = 0; count < MAX_KEY_NUM; count++)
	{
		input_report_key(ts->input_dev, touch_key_array[count], !!(point_data[1]&(0x01<<count)));
	}
	#endif
	input_sync(ts->input_dev);

	if(ts->int_trigger_type> 1)
	{
		msleep(POLL_TIME);
		goto COORDINATE_POLL;
	}
	goto END_WORK_FUNC;

NO_ACTION:
#ifdef HAVE_TOUCH_KEY
	//printk(KERN_INFO"HAVE KEY DOWN!0x%x\n",point_data[1]);
	for(count = 0; count < MAX_KEY_NUM; count++)
	{
		input_report_key(ts->input_dev, touch_key_array[count], !!(point_data[1]&(0x01<<count)));
	}
	input_sync(ts->input_dev);
#endif
END_WORK_FUNC:
XFER_ERROR:
	if(ts->use_irq)
		enable_irq(ts->client->irq);
	return;
}

/*******************************************************
Description:
	Timer interrupt service routine.

Parameter:
	timer:	timer struct pointer.

return:
	Timer work mode. HRTIMER_NORESTART---not restart mode
*******************************************************/
//static enum hrtimer_restart goodix_ts_timer_func(struct hrtimer *timer)
//{
//	struct goodix_ts_data *ts = container_of(timer, struct goodix_ts_data, timer);
//	queue_work(goodix_wq, &ts->work);
//	hrtimer_start(&ts->timer, ktime_set(0, (POLL_TIME+6)*1000000), HRTIMER_MODE_REL);
//	return HRTIMER_NORESTART;
//}

/*******************************************************
Description:
	External interrupt service routine.

Parameter:
	irq:	interrupt number.
	dev_id: private data pointer.

return:
	irq execute status.
*******************************************************/
static irqreturn_t goodix_ts_irq_handler(int irq, void *dev_id)
{
	struct goodix_ts_data *ts = dev_id;
	static int irq_count = 0;

	touch_dbg("irq count: %d\n", irq_count++);
	disable_irq_nosync(ts->client->irq);
	queue_work(goodix_wq, &ts->work);

	return IRQ_HANDLED;
}

/*******************************************************
Description:
	Goodix touchscreen power manage function.

Parameter:
	on:	power status.0---suspend;1---resume.

return:
	Executive outcomes.-1---i2c transfer error;0---succeed.
*******************************************************/
static int goodix_ts_power(struct goodix_ts_data * ts, int on)
{
	int ret = -1;
	unsigned char i2c_control_buf[2] = {80,  1};		//suspend cmd
	int retry = 0;
//	int INT_PORT  = pdata->gpio_irq;

	gt81xx_power(g_pdata, 1);

	if(on != 0 && on !=1)
	{
		printk(KERN_DEBUG "%s: Cant't support this command.", GOODIX_I2C_NAME);
		return -EINVAL;
	}

	if(ts != NULL && !ts->use_irq)
		return -2;

	if(on == 0)		//suspend
	{
		int_wakeup_green(ts, 1);
		while(retry<5)
		{
			ret = i2c_write_bytes(ts->client, i2c_control_buf, 2);
			if(ret == 1)
			{
				printk(KERN_INFO"Send suspend cmd\n");
				break;
			}
			printk("Send cmd failed!\n");
			retry++;
			msleep(10);
		}
		if(ret > 0)
			ret = 0;
	}
	else if(on == 1)		//resume
	{
		printk(KERN_INFO"Int resume\n");
		//gpio_set_status(INT_PORT, gpio_status_out);
		aml_gpio_direction_output(g_pdata->gpio_interrupt, 0);
		msleep(20);
		//gpio_set_status(INT_PORT, gpio_status_in);
		aml_gpio_direction_input(g_pdata->gpio_interrupt);
		msleep(260);

		ret = 0;
	}
	return ret;
}

/*******************************************************
Description:
	Goodix debug sysfs cat version function.

Parameter:
	standard sysfs show param.

return:
	Executive outcomes. 0---failed.
*******************************************************/
static ssize_t goodix_debug_version_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
	char *version_info = NULL;
	struct goodix_ts_data *ts;

	ts = i2c_get_clientdata(i2c_connect_client);
	if(ts==NULL)
		return 0;

	ret = goodix_read_version(ts, &version_info);
	if(ret <= 0)
	{
		printk(KERN_INFO"Read version data failed!\n");
		vfree(version_info);
		return 0;
	}

	printk(KERN_INFO"Goodix TouchScreen Version:%s\n", (version_info+1));
	sprintf(buf,"Goodix TouchScreen Version:%s\n",(version_info+1));
	vfree(version_info);
	ret = strlen(buf);
	return ret;
}

/*******************************************************
Description:
	Goodix debug sysfs cat resolution function.

Parameter:
	standard sysfs show param.

return:
	Executive outcomes. 0---failed.
*******************************************************/
static ssize_t goodix_debug_resolution_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct goodix_ts_data *ts;
	ts = i2c_get_clientdata(i2c_connect_client);
	dev_info(&ts->client->dev,"ABS_X_MAX = %d,ABS_Y_MAX = %d\n",ts->abs_x_max,ts->abs_y_max);
	sprintf(buf,"ABS_X_MAX = %d,ABS_Y_MAX = %d\n",ts->abs_x_max,ts->abs_y_max);

	return strlen(buf);
}
/*******************************************************
Description:
	Goodix debug sysfs cat version function.

Parameter:
	standard sysfs show param.

return:
	Executive outcomes. 0---failed.
*******************************************************/
//static ssize_t goodix_debug_diffdata_show(struct device *dev,
//			struct device_attribute *attr, char *buf)
//{
//	//char diff_data[300];
//	unsigned char diff_data[2241] = {00,};
//	int ret = -1;
//	char diff_data_cmd[2] = {80, 202};
//	int i;
//	int short_tmp;
//	struct goodix_ts_data *ts = i2c_get_clientdata(i2c_connect_client);
////	int INT_PORT  = pdata->gpio_irq;
//
//
//	disable_irq(ts->client->irq);
//	//memset(diff_data, 0, sizeof(diff_data));
//	int_wakeup_green(ts, 0);
//	ret = i2c_write_bytes(ts->client, diff_data_cmd, 2);
//	if(ret != 1)
//	{
//		dev_info(&ts->client->dev, "Write diff data cmd failed!\n");
//		enable_irq(ts->client->irq);
//		return 0;
//	}
//
//#ifdef CONFIG_OF
//	aml_gpio_direction_input(g_pdata->gpio_interrupt);
//	while(aml_get_value(g_pdata->gpio_interrupt))
//#else
//	while(gpio_in_get(g_pdata->gpio_interrupt))
//#endif
//		msleep(1);
//	ret = i2c_read_bytes(ts->client, diff_data, sizeof(diff_data));
//	if(ret <= 0)
//	{
//		dev_info(&ts->client->dev, "Read diff data failed!\n");
//		enable_irq(ts->client->irq);
//		return 0;
//	}
//	for(i=1; i<sizeof(diff_data); i+=2)
//	{
//		short_tmp = diff_data[i] + (diff_data[i+1]<<8);
//		if(short_tmp&0x8000)
//			short_tmp -= 65535;
//		if(short_tmp == 512)continue;
//		sprintf(buf+strlen(buf)," %d",short_tmp);
//		//printk(" %d\n", short_tmp);
//	}
//
//	diff_data_cmd[1] = 0;
//	ret = i2c_write_bytes(ts->client, diff_data_cmd, 2);
//	if(ret != 1)
//	{
//		dev_info(&ts->client->dev, "Write diff data cmd failed!\n");
//		enable_irq(ts->client->irq);
//		return 0;
//	}
//	enable_irq(ts->client->irq);
//	/*for (i=0; i<1024; i++)
//	{
//		sprintf(buf+strlen(buf)," %d",i);
//	}*/
//
//	return strlen(buf);
//}


/*******************************************************
Description:
	Goodix debug sysfs echo calibration function.

Parameter:
	standard sysfs store param.

return:
	Executive outcomes..
*******************************************************/
static ssize_t goodix_debug_calibration_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = -1;
	char cal_cmd_buf[] = {110,1};
	struct goodix_ts_data *ts;

	ts = i2c_get_clientdata(i2c_connect_client);
	dev_info(&ts->client->dev,"Begin calibration......\n");
	if((*buf == 10)||(*buf == 49))
	{
		int_wakeup_green(ts, 1);
		ret = i2c_write_bytes(ts->client,cal_cmd_buf,2);
		if(ret!=1)
		{
			dev_info(&ts->client->dev,"Calibration failed!\n");
			return count;
		}
		else
		{
			dev_info(&ts->client->dev,"Calibration succeed!\n");
		}
	}
	return count;
}


static DEVICE_ATTR(version, S_IRUGO, goodix_debug_version_show, NULL);
static DEVICE_ATTR(resolution, S_IRUGO, goodix_debug_resolution_show, NULL);
//static DEVICE_ATTR(diffdata, S_IRUGO, goodix_debug_diffdata_show, NULL);
static DEVICE_ATTR(calibration, S_IWUSR , NULL, goodix_debug_calibration_store);


/*******************************************************
Description:
	Goodix debug sysfs init function.

Parameter:
	none.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int goodix_debug_sysfs_init(void)
{
	int ret ;
	struct goodix_ts_data *ts;
	ts = i2c_get_clientdata(i2c_connect_client);

	goodix_debug_kobj = kobject_create_and_add("goodix_debug", NULL) ;
	if (goodix_debug_kobj == NULL) {
		printk(KERN_ERR "%s: subsystem_register failed\n", __func__);
		ret = -ENOMEM;
		return ret;
	}
	ret = sysfs_create_file(goodix_debug_kobj, &dev_attr_version.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_version_file failed\n", __func__);
		return ret;
	}
	ret = sysfs_create_file(goodix_debug_kobj, &dev_attr_calibration.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_calibration_file failed\n", __func__);
		return ret;
	}
//	ret = sysfs_create_file(goodix_debug_kobj, &dev_attr_diffdata.attr);
//	if (ret)
//	{
//		printk(KERN_ERR "%s: sysfs_create_diffdata_file failed\n", __func__);
//		return ret;
//	}
	ret = sysfs_create_file(goodix_debug_kobj, &dev_attr_resolution.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_resolution_file failed\n", __func__);
		return ret;
	}
	dev_info(&ts->client->dev,"Goodix debug sysfs create success!\n");
	return 0 ;
}

static void goodix_debug_sysfs_deinit(void)
{
	sysfs_remove_file(goodix_debug_kobj, &dev_attr_version.attr);
	sysfs_remove_file(goodix_debug_kobj, &dev_attr_resolution.attr);
//	sysfs_remove_file(goodix_debug_kobj, &dev_attr_diffdata.attr);
	sysfs_remove_file(goodix_debug_kobj, &dev_attr_calibration.attr);
	kobject_del(goodix_debug_kobj);
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

//Test i2c to check device. Before it SHUTDOWN port Must be low state 30ms or more.
static bool goodix_i2c_test(struct i2c_client *client)
{
	int ret, retry;
	uint8_t test_data[1] = { 0 };	//only write a data address.

	for(retry=0; retry < 5; retry++)
	{
		ret =i2c_write_bytes(client, test_data, 1);	//Test i2c.
		if (ret == 1)
			break;
		msleep(5);
	}

	return ret==1 ? true : false;
}
static void gt81xx_read_version(char* ver)
{
	int ret = 0;
	char *version_info = NULL;
	struct goodix_ts_data *ts;

	ts = i2c_get_clientdata(i2c_connect_client);
	if(ts==NULL)
		return;

	ret = goodix_read_version(ts, &version_info);
	if(ret <= 0)
	{
		printk(KERN_INFO"Read version data failed!\n");
		vfree(version_info);
		return;
	}

	if (ver != NULL)
		sprintf(ver,"Goodix TouchScreen Version:%s\n",(version_info+1));
	else
		printk(KERN_INFO"Goodix TouchScreen Version:%s\n", (version_info+1));
	vfree(version_info);
}
static void gt81xx_upgrade_touch(void)
{
	int retry, ret;
	struct goodix_ts_data *ts = i2c_get_clientdata(i2c_connect_client);
	for(retry=0; retry<3; retry++)
	{
		ret=goodix_init_panel(ts);
		msleep(2);
		if(ret != 0)
			continue;
		else
			break;
	}
	if(ret != 0) {
		ts->bad_data=1;
		printk("%s :fail goodix_init_panel\n", g_pdata->owner);
	}
}
#ifdef LATE_UPGRADE
static int gt81xx_late_upgrade(void *p)
{
	int file_size;
	struct goodix_ts_data *ts;
	ts = i2c_get_clientdata(i2c_connect_client);
	//static int count;
	while(1) {
		file_size = touch_open_fw(g_pdata->config_file);
		if(file_size < 0) {
			//printk("%s: %d\n", __func__, count++);
			msleep(10);
		}
		else break;
	}
	touch_close_fw();
	gt81xx_upgrade_touch();
	printk("%s: load firmware\n", g_pdata->owner);
	if(ts->use_irq)
		enable_irq(i2c_connect_client->irq);
	return 0;	
	//do_exit(0);
}
#endif
static int goodix_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	int retry = 0;
	struct goodix_ts_data *ts;
	char *version_info = NULL;
	char test_data = 1;

	printk("==%s==\n", __func__);

	dev_dbg(&client->dev,"Install touch driver.\n");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
	{
		dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}
	if (ts_com->owner != NULL) return -ENODEV;
	memset(ts_com, 0 ,sizeof(struct touch_pdata));
	g_pdata = (struct touch_pdata*)client->dev.platform_data;
	ts_com = g_pdata;
	printk("ts_com->owner = %s\n", ts_com->owner);
	if (request_touch_gpio(g_pdata) != ERR_NO)
		goto err_goodix_is_not_exist;
	ts_com->hardware_reset = gt81xx_reset;
	ts_com->read_version = gt81xx_read_version;
	ts_com->upgrade_touch = gt81xx_upgrade_touch;

	//int PWR_PORT  = pdata->gpio_pwr;
//	RST_PORT  = pdata->gpio_rst;
//	int INT_PORT  = pdata->gpio_irq;
//	//if (!PWR_PORT  || !RST_PORT || !INT_PORT) {
//	if ( !RST_PORT || !INT_PORT) {
//	    ret = -1;
//	    printk(KERN_ALERT  "goodix platform data error\n");
//	    goto err_check_functionality_failed ;
//	}

	gt81xx_power(g_pdata, 1);
	gt81xx_reset(g_pdata);
	//gpio_direction_output(PWR_PORT, 0);
//	gpio_out(RST_PORT, 0);
//	msleep(2);
//	gpio_out(RST_PORT, 1);
//	msleep(2);
    //gpio_set_status(RST_PORT, gpio_status_in);

	printk("===== goodix TP test start =======\n");
	ret = goodix_i2c_test(client);
	if (!ret)
	{
	    printk("!!! goodix TP is not exist !!!\n");
	    goto err_goodix_is_not_exist;
	}
	printk("===== goodix TP test ok=======\n");

	goodix_wq = create_singlethread_workqueue("goodix_wq");		//create a work queue and worker thread
	if (!goodix_wq) {
		printk(KERN_ALERT "creat workqueue failed\n");
		goto err_goodix_is_not_exist;
	}
	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		ret = -ENOMEM;
		goto err_goodix_is_not_exist;
	}

	INIT_WORK(&ts->work, goodix_ts_work_func);
	ts->client = client;
	i2c_set_clientdata(client, ts);
//	pdata = client->dev.platform_data;

//err_gpio_request:
  i2c_connect_client = client;
	for(retry=0;retry < 5; retry++)
	{
		ts->green_wake_mode = 1;			//init enable wakeup
		int_wakeup_green(ts, 0);			//maybe wakeup
		ret =i2c_write_bytes(client, &test_data, 1);
		if (ret > 0)
			break;
		printk("GOODiX i2c test failed!\n");
	}
	if(ret <= 0)
	{
		dev_err(&client->dev, "I2C communication ERROR!Goodix touchscreen driver become invalid\n");
		goto err_i2c_failed;
	}
#ifdef LATE_UPGRADE
	g_pdata->upgrade_task = kthread_run(gt81xx_late_upgrade,(void *) NULL, "gt81xx_late_upgrade");
	if (!g_pdata->upgrade_task) {
		printk("%s creat upgrade process failed\n", __func__);
		goto err_init_goodix_ts;
	}
	else
		printk("%s creat upgrade process sucessful\n", __func__);
#else
	for(retry=0; retry<3; retry++)
	{
		ret=goodix_init_panel(ts);
		msleep(2);
		if(ret != 0)
			continue;
		else
			break;
	}
	if(ret != 0) {
		ts->bad_data=1;
		goto err_init_goodix_ts;
	}
#endif
	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		dev_dbg(&client->dev,"Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}

	ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) ;
	//ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	ts->input_dev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE); 						// absolute coor (x,y)
#ifdef HAVE_TOUCH_KEY
	for(retry = 0; retry < MAX_KEY_NUM; retry++)
	{
		input_set_capability(ts->input_dev,EV_KEY,touch_key_array[retry]);
	}
#endif

	input_set_abs_params(ts->input_dev, ABS_X, 0, g_pdata->xres, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, 0, g_pdata->yres, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, 0, 0);

#ifdef GOODIX_MULTI_TOUCH
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, g_pdata->xres, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, g_pdata->yres, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, MAX_FINGER_NUM, 0, 0);
#endif

	sprintf(ts->phys, "input/ts");
	ts->input_dev->name = GOODIX_I2C_NAME;
	ts->input_dev->phys = ts->phys;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->id.vendor = 0xDEAD;
	ts->input_dev->id.product = 0xBEEF;
	ts->input_dev->id.version = 10427;	//screen firmware version

	ret = input_register_device(ts->input_dev);
	if (ret) {
		dev_err(&client->dev,"Probe: Unable to register %s input device\n", ts->input_dev->name);
		goto err_input_register_device_failed;
	}
	ts->bad_data = 0;

	if (client->irq)
	{
		//ret = gpio_request(INT_PORT, "TS_INT");
		//if (ret < 0)
		//{
			//dev_err(&client->dev, "Failed to request GPIO:%d, ERRNO:%d\n",(int)INT_PORT,ret);
			//client->irq = 0;
			//goto err_gpio_request;
		//}

//		gpio_set_status(INT_PORT, gpio_status_in);
//        gpio_irq_set(170, GPIO_IRQ( (INT_GPIO_0 -INT_GPIO_0), GPIO_IRQ_FALLING));
//		ret  = request_irq(client->irq, goodix_ts_irq_handler,
//			IRQF_DISABLED,
//			client->name, ts);

//		aml_gpio_direction_input(g_pdata->gpio_interrupt);
//		aml_gpio_to_irq(g_pdata->gpio_interrupt, g_pdata->irq-INT_GPIO_0, g_pdata->irq_edge);
		ret = request_irq(client->irq, goodix_ts_irq_handler, IRQF_DISABLED, client->name, ts);
		if (ret < 0) {
			dev_err(&client->dev,"Cannot allocate ts INT!ERRNO:%d\n", ret);
			if (g_pdata->gpio_interrupt) {
				aml_gpio_free(g_pdata->gpio_interrupt);
				g_pdata->gpio_interrupt = 0;
			}
			//gpio_direction_input(INT_PORT);
			//gpio_free(INT_PORT);
			goto err_input_register_device_failed;
		}
		else
		{
			disable_irq(client->irq);
			ts->use_irq = 1;
			dev_dbg(&client->dev,"Reques EIRQ %d succesd on GPIO:%d\n",client->irq,g_pdata->gpio_interrupt);
		}
	}

//works_in_polling_mode:

	if (!ts->use_irq)
	{
		//hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		//ts->timer.function = goodix_ts_timer_func;
		//hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}

	if(ts->use_irq)
	{
	#ifndef LATE_UPGRADE
		enable_irq(client->irq);
	#endif
		ts->power = goodix_ts_power;
	}
	ret = goodix_read_version(ts, &version_info);
	if(ret <= 0)
	{
		printk(KERN_INFO"Read version data failed!\n");
	}
	else
	{
		printk(KERN_INFO"Goodix TouchScreen Version:%s\n", (version_info+1));
	}
	vfree(version_info);

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = goodix_ts_early_suspend;
	ts->early_suspend.resume = goodix_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
	//goodix_proc_entry = create_proc_entry("goodix-update", 0666, NULL);
	goodix_proc_entry = proc_create("goodix-update", 0666, NULL, NULL);
	if(goodix_proc_entry == NULL)
	{
		dev_info(&client->dev, "Couldn't create proc entry!\n");
		ret = -ENOMEM;
		goto err_create_proc_entry;
	}
	else
	{
		dev_info(&client->dev, "Create proc entry success!\n");
		goodix_proc_entry->write_proc = goodix_update_write;
		goodix_proc_entry->read_proc = goodix_update_read;
		goodix_proc_entry->owner = THIS_MODULE;
	}
#endif

	goodix_debug_sysfs_init();
	dev_info(&client->dev,"Start %s in %s mode\n",
		ts->input_dev->name, ts->use_irq ? "interrupt" : "polling");
	dev_info(&client->dev, "Driver Modify Date:2011-08-11\n");
	create_init(client->dev, g_pdata);
	return 0;

//err_create_proc_entry:
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&ts->early_suspend);
#endif
err_input_register_device_failed:
	input_free_device(ts->input_dev);
err_init_goodix_ts:
	if(ts->use_irq)
	{
		ts->use_irq = 0;
		free_irq(client->irq,ts);
//	#ifdef INT_PORT
//		//gpio_direction_input(INT_PORT);
//		//gpio_free(INT_PORT);
//	#endif
	}
	else
		hrtimer_cancel(&ts->timer);
err_input_dev_alloc_failed:
	i2c_set_clientdata(client, NULL);
err_i2c_failed:
	kfree(ts);
err_goodix_is_not_exist:
	free_touch_gpio(g_pdata);
	ts_com->owner = NULL;
//err_alloc_data_failed:
err_check_functionality_failed:
	return ret;
}


/*******************************************************
Description:
	Goodix touchscreen driver release function.

Parameter:
	client:	i2c device struct.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int goodix_ts_remove(struct i2c_client *client)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(client);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
	remove_proc_entry("goodix-update", NULL);
#endif
	goodix_debug_sysfs_deinit();

	if (ts && ts->use_irq)
	{
	#ifdef INT_PORT
		//gpio_direction_input(INT_PORT);
		//gpio_free(INT_PORT);
	#endif
		free_irq(client->irq, ts);
	}
	else if(ts)
		hrtimer_cancel(&ts->timer);

	dev_notice(&client->dev,"The driver is removing...\n");
	i2c_set_clientdata(client, NULL);
	input_unregister_device(ts->input_dev);
	kfree(ts);
	destroy_remove(client->dev, g_pdata);
	free_touch_gpio(g_pdata);
	ts_com->owner = NULL;
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
	//ret = cancel_work_sync(&ts->work);
	//if(ret && ts->use_irq)
		//enable_irq(client->irq);
	if (ts->power) {
		ret = ts->power(ts, 0);
		if (ret < 0)
			printk(KERN_ERR "goodix_ts_suspend power off failed\n");
	}
	return 0;
}

static int goodix_ts_resume(struct i2c_client *client)
{
	int ret;
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	if (ts->power) {
		ret = ts->power(ts, 1);
		if (ret < 0)
			printk(KERN_ERR "goodix_ts_resume power on failed\n");
	}

	if (ts->use_irq)
		enable_irq(client->irq);
	else
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);

//    gpio_out(RST_PORT, 0);
//    msleep(10);
//    gpio_out(RST_PORT, 1);
	gt81xx_reset(g_pdata);

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

//******************************Begin of firmware update surpport*******************************
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
/**
@brief CRC cal proc,include : Reflect,init_crc32_table,GenerateCRC32
@param global var oldcrc32
@return states
*/
static unsigned int Reflect(unsigned long int ref, char ch)
{
	unsigned int value=0;
	int i;
	for(i = 1; i < (ch + 1); i++)
	{
		if(ref & 1)
			value |= 1 << (ch - i);
		ref >>= 1;
	}
	return value;
}
/*---------------------------------------------------------------------------------------------------------*/
/*  CRC Check Program INIT								                                           		   */
/*---------------------------------------------------------------------------------------------------------*/
static void init_crc32_table(void)
{
	unsigned int temp;
	unsigned int t1,t2;
	unsigned int flag;
	int i,j;
	for(i = 0; i <= 0xFF; i++)
	{
		temp=Reflect(i, 8);
		crc32_table[i]= temp<< 24;
		for (j = 0; j < 8; j++)
		{

			flag=crc32_table[i]&0x80000000;
			t1=(crc32_table[i] << 1);
			if(flag==0)
				t2=0;
			else
				t2=ulPolynomial;
			crc32_table[i] =t1^t2 ;

		}
		crc32_table[i] = Reflect(crc32_table[i], 32);
	}
}
/*---------------------------------------------------------------------------------------------------------*/
/*  CRC main Program									                                           		   */
/*---------------------------------------------------------------------------------------------------------*/
static void GenerateCRC32(unsigned char * buf, unsigned int len)
{
	unsigned int i;
	unsigned int t;

	for (i = 0; i != len; ++i)
	{
		t = (oldcrc32 ^ buf[i]) & 0xFF;
		oldcrc32 = ((oldcrc32 >> 8) & 0xFFFFFF) ^ crc32_table[t];
	}
}

static struct file * update_file_open(char * path, mm_segment_t * old_fs_p)
{
	struct file * filp = NULL;
	int errno = -1;

	filp = filp_open(path, O_RDONLY, 0644);

	if(!filp || IS_ERR(filp))
	{
		if(!filp)
			errno = -ENOENT;
		else
			errno = PTR_ERR(filp);
		printk(KERN_ERR "The update file for Guitar open error.\n");
		return NULL;
	}
	*old_fs_p = get_fs();
	set_fs(get_ds());

	filp->f_op->llseek(filp,0,0);
	return filp ;
}

static void update_file_close(struct file * filp, mm_segment_t old_fs)
{
	set_fs(old_fs);
	if(filp)
		filp_close(filp, NULL);
}
static int update_get_flen(char * path)
{
	struct file * file_ck = NULL;
	mm_segment_t old_fs;
	int length ;

	file_ck = update_file_open(path, &old_fs);
	if(file_ck == NULL)
		return 0;

	length = file_ck->f_op->llseek(file_ck, 0, SEEK_END);
	//printk("File length: %d\n", length);
	if(length < 0)
		length = 0;
	update_file_close(file_ck, old_fs);
	return length;
}
static int update_file_check(char * path)
{
	unsigned char buffer[64] = { 0 } ;
	struct file * file_ck = NULL;
	mm_segment_t old_fs;
	int count, ret, length ;

	file_ck = update_file_open(path, &old_fs);

	if(path != NULL)
		printk("File Path:%s\n", path);

	if(file_ck == NULL)
		return -ERROR_NO_FILE;

	length = file_ck->f_op->llseek(file_ck, 0, SEEK_END);
#ifdef GUITAR_MESSAGE
	printk(KERN_INFO "gt801 update: File length: %d\n",length);
#endif
	if(length <= 0 || (length%4) != 0)
	{
		update_file_close(file_ck, old_fs);
		return -ERROR_FILE_TYPE;
	}

	//set file point to the begining of the file
	file_ck->f_op->llseek(file_ck, 0, SEEK_SET);
	oldcrc32 = 0xFFFFFFFF;
	init_crc32_table();
	while(length > 0)
	{
		ret = file_ck->f_op->read(file_ck, buffer, sizeof(buffer), &file_ck->f_pos);
		if(ret > 0)
		{
			for(count = 0; count < ret;  count++)
				GenerateCRC32(&buffer[count],1);
		}
		else
		{
			update_file_close(file_ck, old_fs);
			return -ERROR_FILE_READ;
		}
		length -= ret;
	}
	oldcrc32 = ~oldcrc32;
#ifdef GUITAR_MESSAGE
	printk("CRC_Check: %u\n", oldcrc32);
#endif
	update_file_close(file_ck, old_fs);
	return 1;
}

unsigned char wait_slave_ready(struct goodix_ts_data *ts, unsigned short *timeout)
{
	unsigned char i2c_state_buf[2] = {ADDR_STA, UNKNOWN_ERROR};
	int ret;
	while(*timeout < MAX_TIMEOUT)
	{
		ret = i2c_read_bytes(ts->client, i2c_state_buf, 2);
		if(ret <= 0)
			return ERROR_I2C_TRANSFER;
		if(i2c_state_buf[1] & SLAVE_READY)
		{
			return i2c_state_buf[1];
			//return 1;
		}
		msleep(10);
		*timeout += 5;
	}
	return 0;
}

static int goodix_update_write(struct file *filp, const char __user *buff, unsigned long len, void *data)
{
	unsigned char cmd[220];
	int ret = -1;

	static unsigned char update_path[100];
	static unsigned short time_count = 0;
	static unsigned int file_len = 0;

	unsigned char i2c_control_buf[2] = {ADDR_CMD, 0};
	unsigned char i2c_states_buf[2] = {ADDR_STA, 0};
	unsigned char i2c_data_buf[PACK_SIZE+1+8] = {ADDR_DAT,};
	//unsigned char i2c_rd_buf[1+4+PACK_SIZE+4];
	unsigned char i2c_rd_buf[160];
	unsigned char retries = 0;
	unsigned int rd_len;
	unsigned char i = 0;
	static unsigned char update_need_config = 0;

	unsigned char checksum_error_times = 0;
#ifdef UPDATE_NEW_PROTOCOL
	unsigned int frame_checksum = 0;
	unsigned int frame_number = 0;
#else
	unsigned char send_crc = 0;
#endif

	struct file * file_data = NULL;
	mm_segment_t old_fs;
	struct goodix_ts_data *ts;

	ts = i2c_get_clientdata(i2c_connect_client);
	if(ts==NULL)
		return 0;

	if(copy_from_user(&cmd, buff, len))
	{
		return -EFAULT;
	}
	switch(cmd[0])
	{
		case STEP_SET_PATH:
			printk(KERN_INFO"Write cmd is:%d,cmd arg is:%s,write len is:%ld\n",cmd[0], &cmd[1], len);
			memset(update_path, 0, 100);
			strncpy(update_path, cmd+1, 100);
			if(update_path[0] == 0)
				return 0;
			else
				return 1;
		case STEP_CHECK_FILE:
			printk(KERN_INFO"Begin to firmware update ......\n");
			ret = update_file_check(update_path);
			if(ret <= 0)
			{
				printk(KERN_INFO"fialed to check update file!\n");
				return ret;
			}
			msleep(500);
			printk(KERN_INFO"Update check file success!\n");
			return 1;
		case STEP_WRITE_SYN:
			printk(KERN_INFO"STEP1:Write synchronization signal!\n");
			i2c_control_buf[1] = UPDATE_START;
			int_wakeup_green(ts, 0);
			ret = i2c_write_bytes(ts->client, i2c_control_buf, 2);
			if(ret <= 0)
			{
				ret = ERROR_I2C_TRANSFER;
				return ret;
			}
			//the time include time(APROM -> LDROM) and time(LDROM init)
			msleep(1000);
			return 1;
		case STEP_WAIT_SYN:
			printk(KERN_INFO"STEP2:Wait synchronization signal!\n");
			while(retries < MAX_I2C_RETRIES)
			{
				i2c_states_buf[1] = UNKNOWN_ERROR;
				ret = i2c_read_bytes(ts->client, i2c_states_buf, 2);
				printk(KERN_INFO"The read byte is:%d\n", i2c_states_buf[1]);
				if(i2c_states_buf[1] & UPDATE_START)
				{
					if(i2c_states_buf[1] & NEW_UPDATE_START)
					{
					#ifdef UPDATE_NEW_PROTOCOL
						update_need_config = 1;
						return 2;
					#else
						return 1;
					#endif
					}
					break;
				}
				msleep(5);
				retries++;
				time_count += 10;
			}
			if((retries >= MAX_I2C_RETRIES) && (!(i2c_states_buf[1] & UPDATE_START)))
			{
				if(ret <= 0)
					return 0;
				else
					return -1;
			}
			return 1;
		case STEP_WRITE_LENGTH:
			printk(KERN_INFO"STEP3:Write total update file length!\n");
			file_len = update_get_flen(update_path);
			if(file_len <= 0)
			{
				printk(KERN_INFO"get update file length failed!\n");
				return -1;
			}
			file_len += 4;
			i2c_data_buf[1] = (file_len>>24) & 0xff;
			i2c_data_buf[2] = (file_len>>16) & 0xff;
			i2c_data_buf[3] = (file_len>>8) & 0xff;
			i2c_data_buf[4] = file_len & 0xff;
			file_len -= 4;
			ret = i2c_write_bytes(ts->client, i2c_data_buf, 5);
			if(ret <= 0)
			{
				ret = ERROR_I2C_TRANSFER;
				return 0;
			}
			return 1;
		case STEP_WAIT_READY:
			printk(KERN_INFO"STEP4:Wait slave ready!\n");
			ret = wait_slave_ready(ts, &time_count);
			if(ret == ERROR_I2C_TRANSFER)
				return 0;
			if(!ret)
			{
				return -1;
			}
			printk(KERN_INFO"Slave ready!\n");
			return 1;
		case STEP_WRITE_DATA:
#ifdef UPDATE_NEW_PROTOCOL
			printk(KERN_INFO"STEP5:Begin to send file data use NEW protocol!\n");
			file_data = update_file_open(update_path, &old_fs);
			if(file_data == NULL)
			{
				return -1;
			}
			frame_number = 0;
			while(file_len >= 0)
			{
				i2c_data_buf[0] = ADDR_DAT;
				rd_len = (file_len >= PACK_SIZE) ? PACK_SIZE : file_len;
				frame_checksum = 0;
				if(file_len)
				{
					ret = file_data->f_op->read(file_data, i2c_data_buf+1+4, rd_len, &file_data->f_pos);
					if(ret <= 0)
					{
						printk("[GOODiX_ISP_NEW]:Read File Data Failed!\n");
						return -1;
					}
					i2c_data_buf[1] = (frame_number>>24)&0xff;
					i2c_data_buf[2] = (frame_number>>16)&0xff;
					i2c_data_buf[3] = (frame_number>>8)&0xff;
					i2c_data_buf[4] = frame_number&0xff;
					frame_number++;
					frame_checksum = 0;
					for(i=0; i<rd_len; i++)
					{
						frame_checksum += i2c_data_buf[5+i];
					}
					frame_checksum = 0 - frame_checksum;
					i2c_data_buf[5+rd_len+0] = frame_checksum&0xff;
					i2c_data_buf[5+rd_len+1] = (frame_checksum>>8)&0xff;
					i2c_data_buf[5+rd_len+2] = (frame_checksum>>16)&0xff;
					i2c_data_buf[5+rd_len+3] = (frame_checksum>>24)&0xff;
				}
rewrite:
				printk(KERN_INFO"[GOODiX_ISP_NEW]:%d\n", file_len);
				ret = i2c_write_bytes(ts->client, i2c_data_buf, 1+4+rd_len+4);
					//if(ret <= 0)
				if(ret != 1)
				{
					printk("[GOODiX_ISP_NEW]:Write File Data Failed!Return:%d\n", ret);
					return 0;
				}

				memset(i2c_rd_buf, 0x00, 1+4+rd_len+4);
				ret = i2c_read_bytes(ts->client, i2c_rd_buf, 1+4+rd_len+4);
				if(ret <= 0)
				{
					printk("[GOODiX_ISP_NEW]:Read File Data Failed!Return:%d\n", ret);
					return 0;
				}
				for(i=1; i<(1+4+rd_len+4); i++)						//check communication
				{
					if(i2c_rd_buf[i] != i2c_data_buf[i])
					{
						i = 0;
						break;
					}
				}
				if(!i)
				{
					i2c_control_buf[0] = ADDR_CMD;
					i2c_control_buf[1] = 0x03;
					i2c_write_bytes(ts->client, i2c_control_buf, 2);		//communication error
					printk("[GOODiX_ISP_NEW]:File Data Frame readback check Error!\n");
				}
				else
				{
					i2c_control_buf[1] = 0x04;													//let LDROM write flash
					i2c_write_bytes(ts->client, i2c_control_buf, 2);
				}

				//Wait for slave ready signal.and read the checksum
				ret = wait_slave_ready(ts, &time_count);
				if((ret & CHECKSUM_ERROR)||(!i))
				{
					if(i)
					{
						printk("[GOODiX_ISP_NEW]:File Data Frame checksum Error!\n");
					}
					checksum_error_times++;
					msleep(20);
					if(checksum_error_times > 20)				//max retry times.
						return 0;
					goto rewrite;
				}
				checksum_error_times = 0;
				if(ret & (FRAME_ERROR))
				{
					printk("[GOODiX_ISP_NEW]:File Data Frame Miss!\n");
					return 0;
				}
				if(ret == ERROR_I2C_TRANSFER)
					return 0;
				if(!ret)
				{
					return -1;
				}
				if(file_len < PACK_SIZE)
				{
					update_file_close(file_data, old_fs);
					break;
				}
				file_len -= rd_len;
			}//end of while((file_len >= 0))
			return 1;
#else
			printk(KERN_INFO"STEP5:Begin to send file data use OLD protocol!\n");
			file_data = update_file_open(update_path, &old_fs);
			if(file_data == NULL)	//file_data has been opened at the last time
			{
				return -1;
			}
			while((file_len >= 0) && (!send_crc))
			{
				printk(KERN_INFO"[GOODiX_ISP_OLD]:%d\n", file_len);
				i2c_data_buf[0] = ADDR_DAT;
				rd_len = (file_len >= PACK_SIZE) ? PACK_SIZE : file_len;
				if(file_len)
				{
					ret = file_data->f_op->read(file_data, i2c_data_buf+1, rd_len, &file_data->f_pos);
					if(ret <= 0)
					{
						return -1;
					}
				}
				if(file_len < PACK_SIZE)
				{
					send_crc = 1;
					update_file_close(file_data, old_fs);
					i2c_data_buf[file_len+1] = oldcrc32&0xff;
					i2c_data_buf[file_len+2] = (oldcrc32>>8)&0xff;
					i2c_data_buf[file_len+3] = (oldcrc32>>16)&0xff;
					i2c_data_buf[file_len+4] = (oldcrc32>>24)&0xff;
					ret = i2c_write_bytes(ts->client, i2c_data_buf, (file_len+1+4));
					//if(ret <= 0)
					if(ret != 1)
					{
						printk("[GOODiX_ISP_OLD]:Write File Data Failed!Return:%d\n", ret);
						return 0;
					}
					break;
				}
				else
				{
					ret = i2c_write_bytes(ts->client, i2c_data_buf, PACK_SIZE+1);
					//if(ret <= 0)
					if(ret != 1)
					{
						printk("[GOODiX_ISP_OLD]:Write File Data Failed!Return:%d\n", ret);
						return 0;
					}
				}
				file_len -= rd_len;

				//Wait for slave ready signal.
				ret = wait_slave_ready(ts, &time_count);
				if(ret == ERROR_I2C_TRANSFER)
					return 0;
				if(!ret)
				{
					return -1;
				}
				//Slave is ready.
			}//end of while((file_len >= 0) && (!send_crc))
			return 1;
#endif
		case STEP_READ_STATUS:
			printk(KERN_INFO"STEP6:Read update status!\n");
			while(time_count < MAX_TIMEOUT)
			{
				ret = i2c_read_bytes(ts->client, i2c_states_buf, 2);
				if(ret <= 0)
				{
					return 0;
				}
				if(i2c_states_buf[1] & SLAVE_READY)
				{
					if(!(i2c_states_buf[1] &0xf0))
					{
						printk(KERN_INFO"The firmware updating succeed!update state:0x%x\n",i2c_states_buf[1]);
						return 1;
					}
					else
					{
						printk(KERN_INFO"The firmware updating failed!update state:0x%x\n",i2c_states_buf[1]);
						return 0;

					}
				}
				msleep(1);
				time_count += 5;
			}
			return -1;
		case FUN_CLR_VAL:								//clear the static val
			time_count = 0;
			file_len = 0;
			update_need_config = 0;
			return 1;
		case FUN_CMD:							//functional command
			if(cmd[1] == CMD_DISABLE_TP)
			{
				printk(KERN_INFO"Disable TS int!\n");
				g_enter_isp = 1;
				if(ts->use_irq)
					disable_irq(ts->client->irq);
			}
			else if(cmd[1] == CMD_ENABLE_TP)
			{
				printk(KERN_INFO"Enable TS int!\n");
				g_enter_isp = 0;
				if(ts->use_irq)
					enable_irq(ts->client->irq);
			}
			else if(cmd[1] == CMD_READ_VER)
			{
				printk(KERN_INFO"Read version!\n");
				ts->read_mode = MODE_RD_VER;
			}
			else if(cmd[1] == CMD_READ_RAW)
			{
				printk(KERN_INFO"Read raw data!\n");
				ts->read_mode = MODE_RD_RAW;
				i2c_control_buf[1] = 201;
				ret = i2c_write_bytes(ts->client, i2c_control_buf, 2);			//read raw data cmd
				if(ret <= 0)
				{
					printk(KERN_INFO"Write read raw data cmd failed!\n");
					return 0;
				}
				msleep(200);
			}
			else if(cmd[1] == CMD_READ_DIF)
			{
				printk(KERN_INFO"Read diff data!\n");
				ts->read_mode = MODE_RD_DIF;
				i2c_control_buf[1] = 202;
				ret = i2c_write_bytes(ts->client, i2c_control_buf, 2);			//read diff data cmd
				if(ret <= 0)
				{
					printk(KERN_INFO"Write read raw data cmd failed!\n");
					return 0;
				}
				msleep(200);
			}
			else if(cmd[1] == CMD_READ_CFG)
			{
				printk(KERN_INFO"Read config info!\n");
				ts->read_mode = MODE_RD_CFG;
				rd_cfg_addr = cmd[2];
				rd_cfg_len = cmd[3];
			}
			else if(cmd[1] == CMD_SYS_REBOOT)
			{
				printk(KERN_INFO"System reboot!\n");
				sys_sync();
				msleep(200);
				kernel_restart(NULL);
			}
			return 1;
		case FUN_WRITE_CONFIG:

			printk(KERN_INFO"Begin write config info!Config length:%d\n",cmd[1]);
			for(i=3; i<cmd[1];i++)
			{
				//if((i-3)%5 == 0)printk("\n");
				printk("(%d):0x%x ", i-3, cmd[i]);
			}
			printk("\n");

			if((cmd[2]>83)&&(cmd[2]<240)&&cmd[1])
			{
				checksum_error_times = 0;
				if(!update_need_config)
					disable_irq(ts->client->irq);
				int_wakeup_green(ts, 0);
				if(!update_need_config)
					enable_irq(ts->client->irq);
reconfig:
				ret = i2c_write_bytes(ts->client, cmd+2, cmd[1]);
				if(ret != 1)
				{
					printk("Write Config failed!return:%d\n",ret);
					return -1;
				}
				if(!update_need_config)return 1;

				i2c_rd_buf[0] = cmd[2];
				ret = i2c_read_bytes(ts->client, i2c_rd_buf, cmd[1]);
				if(ret <= 0)
				{
					printk("Read Config failed!return:%d\n",ret);
					return -1;
				}
				for(i=0; i<cmd[1]; i++)
				{
					if(i2c_rd_buf[i] != cmd[i+2])
					{
						printk("Config readback check failed!\n");
						i = 0;
						break;
					}
				}
				if(!i)
				{
					i2c_control_buf[0] = ADDR_CMD;
					i2c_control_buf[1] = 0x03;
					i2c_write_bytes(ts->client, i2c_control_buf, 2);		//communication error
					checksum_error_times++;
					msleep(20);
					if(checksum_error_times > 20)				//max retry times.
						return 0;
					goto reconfig;
				}
				else
				{
					i2c_control_buf[0] = ADDR_CMD;
					i2c_control_buf[1] = 0x04;					//let LDROM write flash
					i2c_write_bytes(ts->client, i2c_control_buf, 2);
					return 1;
				}

			}
			else
			{
				printk(KERN_INFO"Invalid config addr!\n");
				return -1;
			}
		default:
			return -ENOSYS;
	}
	return 0;
}

static int goodix_update_read( char *page, char **start, off_t off, int count, int *eof, void *data )
{
	int ret = -1;
	struct goodix_ts_data *ts;
	int len = 0;
	char *version_info = NULL;
	unsigned char read_data[1201] = {80, };

	ts = i2c_get_clientdata(i2c_connect_client);
	if(ts==NULL)
		return 0;

	if(ts->read_mode == MODE_RD_VER)		//read version data
	{
		ret = goodix_read_version(ts, &version_info);
		if(ret <= 0)
		{
			printk(KERN_INFO"Read version data failed!\n");
			vfree(version_info);
			return 0;
		}

		for(len=0;len<100;len++)
		{
			if(*(version_info + len) == '\0')
				break;
		}
		printk(KERN_INFO"GOODiX Touchscreen Version is:%s\n", (version_info+1));
		strncpy(page, version_info+1, len + 1);
		vfree(version_info);
		*eof = 1;
		return len+1;
	}
	else if((ts->read_mode == MODE_RD_RAW)||(ts->read_mode == MODE_RD_DIF))		//read raw data or diff
	{
		//printk(KERN_INFO"Read raw data\n");
		ret = i2c_read_bytes(ts->client, read_data, 1201);
		if(ret <= 0)
		{
			if(ts->read_mode == 2)
				printk(KERN_INFO"Read raw data failed!\n");
			if(ts->read_mode == 3)
				printk(KERN_INFO"Read diff data failed!\n");
			return 0;
		}
		memcpy(page, read_data+1, 1200);
		*eof = 1;
		*start = NULL;
		return 1200;
	}
	else if(ts->read_mode == MODE_RD_CFG)
	{
		if((rd_cfg_addr>83)&&(rd_cfg_addr<240))
		{
			read_data[0] = rd_cfg_addr;
			printk("read config addr is:%d\n", rd_cfg_addr);
		}
		else
		{
			read_data[0] = 101;
			printk("invalid read config addr,use default!\n");
		}
		if((rd_cfg_len<0)||(rd_cfg_len>156))
		{
			printk("invalid read config length,use default!\n");
			rd_cfg_len = 239 - read_data[0];
		}
		printk("read config length is:%d\n", rd_cfg_len);
		int_wakeup_green(ts, 1);
		ret = i2c_read_bytes(ts->client, read_data, rd_cfg_len);
		if(ret <= 0)
		{
			printk(KERN_INFO"Read config info failed!\n");
			return 0;
		}
		memcpy(page, read_data+1, rd_cfg_len);
		return rd_cfg_len;
	}
	return len;
}

#endif
//******************************End of firmware update surpport*******************************

static const struct i2c_device_id goodix_ts_id[] = {
	{ GOODIX_I2C_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c,goodix_ts_id);
static struct i2c_driver goodix_ts_driver = {
	.probe		= goodix_ts_probe,
	.remove		= goodix_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	//.suspend	= goodix_ts_suspend,
	//.resume		= goodix_ts_resume,
#endif
	.id_table	= goodix_ts_id,
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
static int goodix_ts_init(void)
{
	int ret;
	printk("==%s==\n", __func__);
	ret=i2c_add_driver(&goodix_ts_driver);

	return ret;
}

/*******************************************************
Description:
	Driver uninstall function.
return:
	Executive Outcomes. 0---succeed.
********************************************************/
static void __exit goodix_ts_exit(void)
{
	printk(KERN_ALERT "Touchscreen driver of guitar exited.\n");
	i2c_del_driver(&goodix_ts_driver);
	if (goodix_wq)
		destroy_workqueue(goodix_wq);		//release our work queue
}

//#ifdef CONFIG_DEFERRED_MODULE_INIT
//deferred_late_init(goodix_ts_init);
//#else
//late_initcall(goodix_ts_init);
//#endif
module_init(goodix_ts_init);
module_exit(goodix_ts_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Driver");
MODULE_LICENSE("GPL");
