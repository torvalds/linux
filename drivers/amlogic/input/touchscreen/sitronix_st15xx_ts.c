/*
 * drivers/amlogic/input/touchscreen/st15xx_ts.c
 *
 * Sitronix st15xx TouchScreen driver.
 *
 * Copyright (c) 2011  Rich Power .
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 1, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * VERSION      	DATE			AUTHOR
 *    1.0		  2011-07-25			Rojam
 *
 * note: only support mulititouch	Rojam 2011-07-25
 */



//#include <linux/i2c.h>
//#include <linux/input.h>
//#include <linux/earlysuspend.h>
//#include <linux/interrupt.h>
//#include <linux/delay.h>
//#include <linux/st15xx_ts.h>

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/earlysuspend.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <mach/gpio.h>
#include <linux/st15xx_ts.h>

static struct i2c_client *this_client;
static struct st15xx_ts_platform_data *pdata;
#define READ_TOUCH_REG 	18
#define READ_TOUCH_START	0X02
//#define ST15XX_DEBUG 			1

#if (ST15XX_DEBUG)
static int st15xx_printk_enable_flag=1;
#else
static int st15xx_printk_enable_flag=0;
#endif
#define CONFIG_ST15xx_MULTITOUCH
struct st_event {
	u16	x1;
	u16	y1;
	u16	x2;
	u16	y2;
	u16	x3;
	u16	y3;
	u16	x4;
	u16	y4;
	u16	x5;
	u16	y5;
	u16	x6;
	u16	y6;
	u16	x7;
	u16	y7;
	u16	x8;
	u16	y8;
	u16	x9;
	u16	y9;
	u16	pressure;
    u8  touch_point;
};

struct st15xx_ts_data {
	struct input_dev	*input_dev;
	struct st_event		event;
	struct work_struct 	pen_event_work;
	struct workqueue_struct *ts_workqueue;
	struct early_suspend	early_suspend;
};

static ssize_t st15xx_read(struct device *dev, struct device_attribute *attr, char *buf)
{
    return 0;
}

static ssize_t st15xx_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret = 0;
    struct capts *ts = (struct capts *)dev_get_drvdata(dev);

    if (!strcmp(attr->attr.name, "st15xxPrintFlag")) {
		printk("buf[0]=%d, buf[1]=%d\n", buf[0], buf[1]);
		if (buf[0] == '0') st15xx_printk_enable_flag = 0;
		if (buf[0] == '1') st15xx_printk_enable_flag = 1;
    }

    return count;
}

static DEVICE_ATTR(st15xxPrintFlag, S_IRWXUGO, 0, st15xx_write);
static struct attribute *st15xx_attr[] = {
    &dev_attr_st15xxPrintFlag.attr,
    NULL
};

static struct attribute_group st15xx_attr_group = {
    .name = NULL,
    .attrs = st15xx_attr,
};


/***********************************************************************************************
Name	:	st15xx_i2c_rxdata

Input	:	*rxdata
                     *length

Output	:	ret

function	:

***********************************************************************************************/
static int st15xx_i2c_rxdata(char *rxdata, int length)
{
	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= rxdata,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= length,
			.buf	= rxdata,
		},
	};

    //msleep(1);
	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);

	return ret;
}
/***********************************************************************************************
Name	:

Input	:


Output	:

function	:

***********************************************************************************************/
static int st15xx_i2c_txdata(char *txdata, int length)
{
	int ret;

	struct i2c_msg msg[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= length,
			.buf	= txdata,
		},
	};

   	//msleep(1);
	ret = i2c_transfer(this_client->adapter, msg, 1);
	if (ret < 0)
		pr_err("%s i2c write error: %d\n", __func__, ret);

	return ret;
}
/***********************************************************************************************
Name	:	 st15xx_write_reg

Input	:	addr -- address
                     para -- parameter

Output	:

function	:	write register of st15xx

***********************************************************************************************/
static int st15xx_write_reg(u8 addr, u8 para)
{
    u8 buf[3];
    int ret = -1;

    buf[0] = addr;
    buf[1] = para;
    ret = st15xx_i2c_txdata(buf, 2);
    if (ret < 0) {
        pr_err("write reg failed! %#x ret: %d", buf[0], ret);
        return -1;
    }

    return 0;
}


/***********************************************************************************************
Name	:	st15xx_read_reg

Input	:	addr
                     pdata

Output	:

function	:	read register of st15xx

***********************************************************************************************/
static int st15xx_read_reg(u8 addr, u8 *pdata)
{
	int ret;
	u8 buf[2] = {0};

	buf[0] = addr;
	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= buf,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= buf,
		},
	};

    //msleep(1);
	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);

	*pdata = buf[0];
	return ret;

}


/***********************************************************************************************
Name	:	 st15xx_read_fw_ver

Input	:	 void


Output	:	 firmware version

function	:	 read TP firmware version

***********************************************************************************************/
static unsigned char st15xx_read_fw_ver(void)
{
	unsigned char ver;
	st15xx_read_reg(ST15xx_REG_FIRMID, &ver);
	return(ver);
}


/***********************************************************************************************
Name	:

Input	:


Output	:

function	:

***********************************************************************************************/
static void st15xx_ts_release(void)
{
	struct st15xx_ts_data *data = i2c_get_clientdata(this_client);
#ifdef CONFIG_ST15xx_MULTITOUCH
	input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
#else
	input_report_abs(data->input_dev, ABS_PRESSURE, 0);
	input_report_key(data->input_dev, BTN_TOUCH, 0);
#endif
	input_sync(data->input_dev);
}

static int st15xx_read_data(void)
{
	struct st15xx_ts_data *data = i2c_get_clientdata(this_client);
	struct st_event *event = &data->event;
//	u8 buf[14] = {0};
	u8 buf[READ_TOUCH_REG] = {0};
	int ret = -1;
	int Touch_Point_Reg=0;
	int Cur_Touch_Point_Reg=0;

#ifdef CONFIG_ST15xx_MULTITOUCH
	buf[0]=0x10;
	//ret = st15xx_i2c_txdata(buf,1);
	//buf[0]=0x0;
	ret = st15xx_i2c_rxdata(buf, READ_TOUCH_REG);
#else
    ret = st15xx_i2c_rxdata(buf, 7);
#endif
    if (ret < 0) {
		printk("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
		return ret;
	}
#if (ST15XX_DEBUG)			//modify by Rojam 2011-07-27 09:5
	printk("===fingers= %d,st15xx_xyH 0 = %d ====\n",buf[0],buf[2]);
	printk("===st15xx_xL 0 = %d,st15xx_yL 0 = %d ====\n",buf[3],buf[4]);
	printk("===st15xx_z 0 = %d,st15xx_xyH 1 = %d ====\n",buf[5],buf[6]);
	printk("===st15xx_xL 1 = %d,st15xx_yL 1 = %d ====\n",buf[7],buf[8]);
#endif	//modify by Rojam 2011-07-27

	memset(event, 0, sizeof(struct st_event));
	event->touch_point = buf[0] & 0x0F;// 0000 1111

    if (event->touch_point == 0) {
        st15xx_ts_release();
        return 1;
    }
	Cur_Touch_Point_Reg=event->touch_point;

#ifdef CONFIG_ST15xx_MULTITOUCH
    switch (event->touch_point) {
#if(0)			//modify by Rojam 2011-07-29 10:48
		case 9:
			Touch_Point_Reg=0x12+(Cur_Touch_Point_Reg-1)*4;
			event->x9 =  (s16)(buf[Touch_Point_Reg] & 0x70)<<4 | (s16)buf[Touch_Point_Reg+1];
			event->y9 =  (s16)(buf[Touch_Point_Reg] & 0x0F)<<8 | (s16)buf[Touch_Point_Reg+2];
			Cur_Touch_Point_Reg--;
		case 8:
			Touch_Point_Reg=0x12+(Cur_Touch_Point_Reg-1)*4;
			event->x8 =  (s16)(buf[Touch_Point_Reg] & 0x70)<<4 | (s16)buf[Touch_Point_Reg+1];
			event->y8 =  (s16)(buf[Touch_Point_Reg] & 0x0F)<<8 | (s16)buf[Touch_Point_Reg+2];
			Cur_Touch_Point_Reg--;
		case 7:
			Touch_Point_Reg=0x12+(Cur_Touch_Point_Reg-1)*4;
			event->x7 =  (s16)(buf[Touch_Point_Reg] & 0x70)<<4 | (s16)buf[Touch_Point_Reg+1];
			event->y7 =  (s16)(buf[Touch_Point_Reg] & 0x0F)<<8 | (s16)buf[Touch_Point_Reg+2];
			Cur_Touch_Point_Reg--;
		case 6:
			Touch_Point_Reg=0x12+(Cur_Touch_Point_Reg-1)*4;
			event->x6 =  (s16)(buf[Touch_Point_Reg] & 0x70)<<4 | (s16)buf[Touch_Point_Reg+1];
			event->y6 =  (s16)(buf[Touch_Point_Reg] & 0x0F)<<8 | (s16)buf[Touch_Point_Reg+2];
			Cur_Touch_Point_Reg--;
		case 5:
			Touch_Point_Reg=0x12+(Cur_Touch_Point_Reg-1)*4;
			event->x5 =  (s16)(buf[Touch_Point_Reg] & 0x70)<<4 | (s16)buf[Touch_Point_Reg+1];
			event->y5 =  (s16)(buf[Touch_Point_Reg] & 0x0F)<<8 | (s16)buf[Touch_Point_Reg+2];
			Cur_Touch_Point_Reg--;
#endif	//modify by Rojam 2011-07-29
		case 4:
			Touch_Point_Reg=READ_TOUCH_START+(Cur_Touch_Point_Reg-1)*4;
			event->x4 =  (s16)(buf[Touch_Point_Reg] & 0xF0)<<4 | (s16)buf[Touch_Point_Reg+1];
			event->y4 =  (s16)(buf[Touch_Point_Reg] & 0x0F)<<8 | (s16)buf[Touch_Point_Reg+2];
			Cur_Touch_Point_Reg--;
		case 3:
			Touch_Point_Reg=READ_TOUCH_START+(Cur_Touch_Point_Reg-1)*4;
			event->x3 =  (s16)(buf[Touch_Point_Reg] & 0xF0)<<4 | (s16)buf[Touch_Point_Reg+1];
			event->y3 =  (s16)(buf[Touch_Point_Reg] & 0x0F)<<8 | (s16)buf[Touch_Point_Reg+2];
			Cur_Touch_Point_Reg--;
		case 2:
			Touch_Point_Reg=READ_TOUCH_START+(Cur_Touch_Point_Reg-1)*4;
			event->x2 =  (s16)(buf[Touch_Point_Reg] & 0xF0)<<4 | (s16)buf[Touch_Point_Reg+1];
			event->y2 =  (s16)(buf[Touch_Point_Reg] & 0x0F)<<8 | (s16)buf[Touch_Point_Reg+2];
			Cur_Touch_Point_Reg--;
		case 1:
			Touch_Point_Reg=READ_TOUCH_START+(Cur_Touch_Point_Reg-1)*4;
			event->x1 = (s16)(buf[Touch_Point_Reg] & 0xF0)<<4 | (s16)buf[Touch_Point_Reg+1];
			event->y1 = (s16)(buf[Touch_Point_Reg] & 0x0F)<<8 | (s16)buf[Touch_Point_Reg+2];
			Cur_Touch_Point_Reg--;
            		break;
		default:
		    return -1;
	}
#else
    if (event->touch_point == 1) {
    	event->x1 = (s16)(buf[3] & 0x0F)<<8 | (s16)buf[4];
		event->y1 = (s16)(buf[5] & 0x0F)<<8 | (s16)buf[6];
    }
#endif
    event->pressure = 200;
#if(ST15XX_DEBUG)			//modify by Rojam 2011-07-27 09:5
	Touch_Point_Reg = event->x1 & 0X7FF;
	Cur_Touch_Point_Reg = event->x2 & 0X7FF;
	printk("%s: 1:%d %d 2:%d %d \n", __func__,
		Touch_Point_Reg, event->y1, Cur_Touch_Point_Reg, event->y2);
#endif	//modify by Rojam 2011-07-27

	dev_dbg(&this_client->dev, "%s: 1:%d %d 2:%d %d \n", __func__,
		event->x1, event->y1, event->x2, event->y2);

    return 0;
}
/***********************************************************************************************
Name	:

Input	:


Output	:

function	:

***********************************************************************************************/
static void st15xx_report_value(void)
{
	struct st15xx_ts_data *data = i2c_get_clientdata(this_client);
	struct st_event *event = &data->event;
	u8 uVersion;
	char Valid;

#if(ST15XX_DEBUG)			//modify by Rojam 2011-07-27 09:6
	printk("==st15xx_report_value =\n");
#endif	//modify by Rojam 2011-07-27

#ifdef CONFIG_ST15xx_MULTITOUCH
	switch(event->touch_point) {
#if(0)			//modify by Rojam 2011-07-29 10:47
		case 9:
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x9);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y9);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input_dev);
			if(st15xx_printk_enable_flag)
			printk("st15xx===x9 = %d,y9 = %d ====\n",event->x2,event->y2);
		case 8:
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x8);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y8);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input_dev);
			if(st15xx_printk_enable_flag)
			printk("st15xx===x8 = %d,y8 = %d ====\n",event->x2,event->y2);
		case 7:
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x7);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y7);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input_dev);
			if(st15xx_printk_enable_flag)
			printk("st15xx===x7 = %d,y7 = %d ====\n",event->x2,event->y2);
		case 6:
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x6);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y6);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input_dev);
			if(st15xx_printk_enable_flag)
			printk("st15xx===x6 = %d,y6 = %d ====\n",event->x2,event->y2);
		case 5:
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x5);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y5);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input_dev);
			if(st15xx_printk_enable_flag)
			printk("st15xx===x5 = %d,y5 = %d ====\n",event->x2,event->y2);
#endif	//modify by Rojam 2011-07-29
		case 4:
			Valid=event->x4 & 0X800;
			event->x4=event->x4 & 0x7ff;
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x4 );
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y4);
			if(Valid==1)
				input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			else
				input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 0);
			input_mt_sync(data->input_dev);
			if(st15xx_printk_enable_flag)
			printk("st15xx===x4 = %d,y4 = %d ====\n",event->x2,event->y2);
		case 3:

			Valid=event->x3 & 0X800;
			event->x3=event->x3 & 0x7ff;
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x3);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y3);
			if(Valid==1)
				input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			else
				input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 0);
			input_mt_sync(data->input_dev);
			if(st15xx_printk_enable_flag)
			printk("st15xx===x3 = %d,y3 = %d ====\n",event->x2,event->y2);
		case 2:

			Valid=event->x2 & 0X800;
			event->x2=event->x2 & 0x7ff;
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x2 );
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y2);
			if(Valid==1)
				input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			else
				input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 0);
			input_mt_sync(data->input_dev);
			if(st15xx_printk_enable_flag)
			printk("st15xx===x2 = %d,y2 = %d ====\n",event->x2,event->y2);
		case 1:
			Valid=event->x1 & 0X800;
			event->x1=event->x1 & 0x7ff;
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x1 );
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y1);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			if(Valid==1)
				input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			else
				input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 0);
			input_mt_sync(data->input_dev);
			if(st15xx_printk_enable_flag)
			printk("st15xx===x1 = %d,y1 = %d ====\n",event->x1,event->y1);

		default:
//			printk("==touch_point default =\n");
			break;
	}
#else	/* CONFIG_ST15xx_MULTITOUCH*/
	if (event->touch_point == 1) {
		input_report_abs(data->input_dev, ABS_X, event->x1);
		input_report_abs(data->input_dev, ABS_Y, event->y1);
		input_report_abs(data->input_dev, ABS_PRESSURE, event->pressure);
	}
	input_report_key(data->input_dev, BTN_TOUCH, 1);
#endif	/* CONFIG_ST15xx_MULTITOUCH*/
	input_sync(data->input_dev);

	dev_dbg(&this_client->dev, "%s: 1:%d %d 2:%d %d \n", __func__,
		event->x1, event->y1, event->x2, event->y2);
}	/*end st15xx_report_value*/
/***********************************************************************************************
Name	:

Input	:


Output	:

function	:

***********************************************************************************************/
static void st15xx_ts_pen_irq_work(struct work_struct *work)
{
#if (ST15XX_DEBUG)
	printk("%s  \n", __func__);
#endif
	int ret = -1;
	ret = st15xx_read_data();
	if (ret == 0) {
		st15xx_report_value();
	}
	enable_irq(this_client->irq);
}
/***********************************************************************************************
Name	:

Input	:


Output	:

function	:

***********************************************************************************************/
static irqreturn_t st15xx_ts_interrupt(int irq, void *dev_id)
{
	struct st15xx_ts_data *st15xx_ts = dev_id;
    	disable_irq_nosync(this_client->irq);
#if ST15XX_DEBUG
	printk("enter irq(%d, %d)\n",irq, this_client->irq);
#endif
	if (!work_pending(&st15xx_ts->pen_event_work)) {
		queue_work(st15xx_ts->ts_workqueue, &st15xx_ts->pen_event_work);
	}
	return IRQ_HANDLED;
}
#ifdef CONFIG_HAS_EARLYSUSPEND
/***********************************************************************************************
Name	:

Input	:


Output	:

function	:

***********************************************************************************************/
static void st15xx_ts_suspend(struct early_suspend *handler)
{
//	struct st15xx_ts_data *ts;
//	ts =  container_of(handler, struct st15xx_ts_data, early_suspend);

	printk("==st15xx_ts_suspend=\n");
//	disable_irq(this_client->irq);
//	disable_irq(IRQ_EINT(6));
//	cancel_work_sync(&ts->pen_event_work);
//	flush_workqueue(ts->ts_workqueue);
	// ==set mode ==,
//    	st15xx_set_reg(ST15xx_REG_PMODE, PMODE_HIBERNATE);
}
/***********************************************************************************************
Name	:

Input	:


Output	:

function	:

***********************************************************************************************/
static void st15xx_ts_resume(struct early_suspend *handler)
{
	printk("==st15xx_ts_resume=\n");
	// wake the mode
//	__gpio_as_output(GPIO_ST15xx_WAKE);
//	__gpio_clear_pin(GPIO_ST15xx_WAKE);		//set wake = 0,base on system
//	 msleep(100);
//	__gpio_set_pin(GPIO_ST15xx_WAKE);			//set wake = 1,base on system
//	msleep(100);
//	enable_irq(this_client->irq);
//	enable_irq(IRQ_EINT(6));
}
#endif  //CONFIG_HAS_EARLYSUSPEND
/***********************************************************************************************
Name	:

Input	:


Output	:

function	:

***********************************************************************************************/
static int
st15xx_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct st15xx_ts_data *st15xx_ts;
	struct input_dev *input_dev;
	int err = 0;
	unsigned char uc_reg_value;
	struct ts_platform_data *pdata = client->dev.platform_data;
		if(pdata&&pdata->power_off&&pdata->power_on){
		pdata->power_off();
		mdelay(50);
		pdata->power_on();
		mdelay(200);
	}
	printk("==st15xx_ts_probe=\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}
	//client->irq =  client->dev.platform_data->irq;
	client->irq =pdata->irq;
	printk("==kzalloc=\n");
	st15xx_ts = kzalloc(sizeof(*st15xx_ts), GFP_KERNEL);
	if (!st15xx_ts)	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	printk("==kzalloc success=\n");
	this_client = client;
	i2c_set_clientdata(client, st15xx_ts);

	INIT_WORK(&st15xx_ts->pen_event_work, st15xx_ts_pen_irq_work);
	st15xx_ts->ts_workqueue = create_singlethread_workqueue(dev_name(&client->dev));
	if (!st15xx_ts->ts_workqueue) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}


//	__gpio_as_irq_fall_edge(pdata->intr);		//
printk("==enable Irq=\n");
    if (pdata->init_irq) {
        pdata->init_irq();
    }
printk("==enable Irq success=\n");

	disable_irq_nosync(this_client->irq);
//	disable_irq(IRQ_EINT(6));

	printk("==input_allocate_device=\n");
	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}

	st15xx_ts->input_dev = input_dev;

#ifdef CONFIG_ST15xx_MULTITOUCH
	set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
	set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);

	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);
#else
	set_bit(ABS_X, input_dev->absbit);
	set_bit(ABS_Y, input_dev->absbit);
	set_bit(ABS_PRESSURE, input_dev->absbit);
	set_bit(BTN_TOUCH, input_dev->keybit);

	input_set_abs_params(input_dev, ABS_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_PRESSURE, 0, PRESS_MAX, 0 , 0);
#endif

	set_bit(EV_ABS, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);

	input_dev->name		= ST15xx_NAME;		//dev_name(&client->dev)
	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev,
		"st15xx_ts_probe: failed to register input device: %s\n",
		dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	printk("==register_early_suspend =\n");
	st15xx_ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	st15xx_ts->early_suspend.suspend = st15xx_ts_suspend;
	st15xx_ts->early_suspend.resume	= st15xx_ts_resume;
	register_early_suspend(&st15xx_ts->early_suspend);
#endif

	err = request_irq(client->irq, st15xx_ts_interrupt, IRQF_DISABLED, "st15xx_ts", st15xx_ts);
//	err = request_irq(IRQ_EINT(6), st15xx_ts_interrupt, IRQF_TRIGGER_FALLING, "st15xx_ts", st15xx_ts);
	if (err < 0) {
		dev_err(&client->dev, "st15xx_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}

    msleep(50);
    //get some register information
    uc_reg_value = st15xx_read_fw_ver();
    printk("[FST] Firmware version = 0x%x\n", uc_reg_value);


//    fts_ctpm_fw_upgrade_with_i_file();



//wake the CTPM
//	__gpio_as_output(GPIO_ST15xx_WAKE);
//	__gpio_clear_pin(GPIO_ST15xx_WAKE);		//set wake = 0,base on system
//	 msleep(100);
//	__gpio_set_pin(GPIO_ST15xx_WAKE);			//set wake = 1,base on system
//	msleep(100);
//	st15xx_set_reg(0x88, 0x05); //5, 6,7,8
//	st15xx_set_reg(0x80, 30);
//	msleep(50);
//  enable_irq(this_client->irq);
   // enable_irq(IRQ_EINT(6));

    err = sysfs_create_group(&client->dev.kobj, &st15xx_attr_group);

	printk("==probe over =\n");
    return 0;

exit_input_register_device_failed:
	input_free_device(input_dev);
exit_input_dev_alloc_failed:
	free_irq(client->irq, st15xx_ts);
//	free_irq(IRQ_EINT(6), st15xx_ts);
exit_irq_request_failed:
exit_platform_data_null:
	cancel_work_sync(&st15xx_ts->pen_event_work);
	destroy_workqueue(st15xx_ts->ts_workqueue);
exit_create_singlethread:
	printk("==singlethread error =\n");
	i2c_set_clientdata(client, NULL);
	kfree(st15xx_ts);
exit_alloc_data_failed:
exit_check_functionality_failed:
	return err;
}
/***********************************************************************************************
Name	:

Input	:


Output	:

function	:

***********************************************************************************************/
static int __devexit st15xx_ts_remove(struct i2c_client *client)
{
	printk("==st15xx_ts_remove=\n");
	struct st15xx_ts_data *st15xx_ts = i2c_get_clientdata(client);
	unregister_early_suspend(&st15xx_ts->early_suspend);
	free_irq(client->irq, st15xx_ts);
//	free_irq(IRQ_EINT(6), st15xx_ts);
	input_unregister_device(st15xx_ts->input_dev);
	kfree(st15xx_ts);
	cancel_work_sync(&st15xx_ts->pen_event_work);
	destroy_workqueue(st15xx_ts->ts_workqueue);
	i2c_set_clientdata(client, NULL);
	return 0;
}

static const struct i2c_device_id st15xx_ts_id[] = {
	{ ST15xx_NAME, 0 },{ }
};


MODULE_DEVICE_TABLE(i2c, st15xx_ts_id);

static struct i2c_driver st15xx_ts_driver = {
	.probe		= st15xx_ts_probe,
	.remove		= __devexit_p(st15xx_ts_remove),
	.id_table	= st15xx_ts_id,
	.driver	= {
		.name	= ST15xx_NAME,
		.owner	= THIS_MODULE,
	},
};

/***********************************************************************************************
Name	:

Input	:


Output	:

function	:

***********************************************************************************************/
static int __init st15xx_ts_init(void)
{
	int ret;
	printk("==st15xx_ts_init==\n");
	ret = i2c_add_driver(&st15xx_ts_driver);
	printk("ret=%d\n",ret);
	return ret;
//	return i2c_add_driver(&st15xx_ts_driver);
}

/***********************************************************************************************
Name	:

Input	:


Output	:

function	:

***********************************************************************************************/
static void __exit st15xx_ts_exit(void)
{
	printk("==st15xx_ts_exit==\n");
	i2c_del_driver(&st15xx_ts_driver);
}

#ifdef CONFIG_DEFERRED_MODULE_INIT
deferred_module_init(st15xx_ts_init);
#else
module_init(st15xx_ts_init);
#endif
module_exit(st15xx_ts_exit);

MODULE_AUTHOR("<rojam.luo@rich-power.com.cn>");
MODULE_DESCRIPTION("Sitronix st15xx TouchScreen driver");
MODULE_LICENSE("GPL");
