/* 
 * drivers/input/touchscreen/ft5x0x_ts.c
 *
 * FocalTech ft5x0x TouchScreen driver. 
 *
 * Copyright (c) 2010  Focal tech Ltd.
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
 *
 *	note: only support mulititouch	Wenfs 2010-10-01
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/async.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <mach/gpio.h>
#include <linux/irq.h>
#include <mach/board.h>
#include <linux/input/mt.h>


#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif


#define CONFIG_FT5X0X_MULTITOUCH 1
#define CONFIG_TOUCH_PANEL_KEY	  1
#define NEW_PAL_DRV
#define CHECK_KEY
#define LONGPRESS_LOCK_SPECKEY	 //是否使用长按某键(如search键)锁住功能键的功能
#ifdef LONGPRESS_LOCK_SPECKEY
#define KEY_LOCK 195
#define ORIGIN_KEY KEY_SEARCH
#define LOCK_LONG_PRESS_CNT 100
static int Origin2LockPressCnt = 0;
static int lockflag =0;
static int touch_key_hold_press = 0;

ssize_t glock_status_show(struct device *dev, char *buf)
{
         printk("lockflag === %d\n",lockflag);
	return sprintf(buf, "%d", lockflag);
}
#endif


static int err_ft5X06 = 0;	//w++记载有没有此设备
static int debug1=1;
module_param(debug1, int, S_IRUGO|S_IWUSR);

static int gpress = 0;
extern void gpio_enable1();
static int Motoenble = 0;
module_param(Motoenble, int, S_IRUGO|S_IWUSR);
static int MotoStart = 0;

/*
 * Added by yick @RockChip
 * Compatible with both types of firmware
 * default: point - only pressdown finger num
 * 			event - both down and up event
 */
#define USE_POINT 1
#if USE_POINT
uint16_t down_table	= 0;
uint16_t up_table	= ~0;
#endif

#define SCREEN_MAX_X    480
#define SCREEN_MAX_Y    800
#define PRESS_MAX       255

#define FT5X0X_NAME	"ft5x0x_ts"
#define MAX_CONTACTS 5
enum ft5x0x_ts_regs {
	FT5X0X_REG_PMODE	= 0xA5,	/* Power Consume Mode		*/
};

#define KEY_MIN_X	800
#define KEY_NUM 	4

int touch_key_press[] = {0, 0, 0, 0};
//int touch_key_code[] = { KEY_BACK,KEY_HOME, KEY_MENU};
//int touch_key_min[KEY_NUM] ={-1,59,105};
//int touch_key_max[KEY_NUM] ={2,73,121};

int touch_key_code[] = {KEY_MENU, KEY_HOMEPAGE, KEY_BACK, KEY_SEARCH};
int touch_key_min[KEY_NUM] ={30,150,270,390};
int touch_key_max[KEY_NUM] ={90,210,330,450};

//FT5X0X_REG_PMODE
#define PMODE_ACTIVE        0x00
#define PMODE_MONITOR       0x01
#define PMODE_STANDBY       0x02
#define PMODE_HIBERNATE     0x03

#ifndef ABS_MT_TOUCH_MAJOR
#define ABS_MT_TOUCH_MAJOR	0x30	/* touching ellipse */
#define ABS_MT_TOUCH_MINOR	0x31	/* (omit if circular) */
#define ABS_MT_WIDTH_MAJOR	0x32	/* approaching ellipse */
#define ABS_MT_WIDTH_MINOR	0x33	/* (omit if circular) */
#define ABS_MT_ORIENTATION	0x34	/* Ellipse orientation */
#define ABS_MT_POSITION_X	  0x35	/* Center X ellipse position */
#define ABS_MT_POSITION_Y	  0x36	/* Center Y ellipse position */
#define ABS_MT_TOOL_TYPE	  0x37	/* Type of touching device */
#define ABS_MT_BLOB_ID		  0x38	/* Group set of pkts as blob */
#endif /* ABS_MT_TOUCH_MAJOR */

struct point_data {
	u8 status;
	u8 id;
	u16 x;
	u16 y;
};

struct ts_event {
  u16  touch_point;
  struct point_data point[5];
};

struct ft5x0x_ts_dev {
  struct i2c_client *client;
	struct input_dev	*input_dev;
	int    irq;
	struct ts_event		event;
	struct work_struct 	pen_event_work;
	struct workqueue_struct *ts_workqueue;
	struct early_suspend	early_suspend;

#ifdef CHECK_KEY
			struct timer_list timer;
#endif
};

static struct ft5x0x_ts_dev *g_dev;
static bool rember_point_into = true;


static ssize_t Moto_status(struct device_driver *_drv,char *_buf)
{
	//printk("Moto_status Motoenble==%d\n", Motoenble);
   if(Motoenble)
       return sprintf(_buf, "Ledlevel is Low\n");
   else
   	   return sprintf(_buf, "Ledlevel is High\n");
}
static ssize_t Moto_control(struct device_driver *_drv, const char *_buf, size_t _count)
{
		char temp[5];
		//printk("Read data from Android: %s\n", _buf);
		strncpy(temp, _buf, 1);
		Motoenble = simple_strtol(temp, NULL, 10);
		//printk("Moto_control Motoenble=%d\n", Motoenble);
}


static int ft5x0x_i2c_rxdata(char *rxdata, int length)
{
	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr	= g_dev->client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= rxdata,
			.scl_rate = 200 * 1000,
		},
		{
			.addr	= g_dev->client->addr,
			.flags	= I2C_M_RD,
			.len	= length,
			.buf	= rxdata,
			.scl_rate = 200 * 1000,
		},
	};

	ret = i2c_transfer(g_dev->client->adapter, msgs, 2);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);

	return ret;
}

static int ft5x0x_i2c_txdata(char *txdata, int length)
{
	int ret;

	struct i2c_msg msg[] = {
		{
			.addr	= g_dev->client->addr,
			.flags	= 0,
			.len	= length,
			.buf	= txdata,
			.scl_rate = 200 * 1000,
		},
	};

	ret = i2c_transfer(g_dev->client->adapter, msg, 1);
	if (ret < 0)
		pr_err("%s i2c write error: %d\n", __func__, ret);

	return ret;
}

static int ft5x0x_set_reg(u8 addr, u8 para)
{
    u8 buf[3];
    int ret = -1;

    buf[0] = addr;
    buf[1] = para;
    ret = ft5x0x_i2c_txdata(buf, 2);
    if (ret < 0) {
        pr_err("write reg failed! %#x ret: %d", buf[0], ret);
        return -1;
    }
    
    return 0;
}

static int ft5x0x_read_data(void)
{
	struct ft5x0x_ts_dev *data = i2c_get_clientdata(g_dev->client);
	struct ft5x0x_platform_data *pdata = g_dev->client->dev.platform_data;
	struct ts_event *event = &data->event;

	u8 buf[32]= {0};//set send addr to 0x00 *important*
	int ret = -1;
       int key;


	if(Motoenble)
	{
		if(!MotoStart)
		{
			//printk("the moto is enable!\n");
			//gpio_enable1();
			MotoStart =1;
		}
	}


		ret = ft5x0x_i2c_rxdata(buf, 32);

    	if (ret < 0) {
			printk("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
			return ret;
		}
#if 0	// Just for debug
		u8 uc_ecc;
		int i;
		uc_ecc = buf[2];
		for (i=0; i<5; i++)
		{
			uc_ecc ^= buf[3+6*i];
			uc_ecc ^= buf[4+6*i];
			uc_ecc ^= buf[5+6*i];
			uc_ecc ^= buf[6+6*i];
		}
//		if (uc_ecc == buf[1])  break;
//	}


	if (uc_ecc != buf[1])
	{
		printk("ecc check error uc_ecc=0x%x, buf[1]=0x%x.\n",uc_ecc, buf[1]);
		return 1;
	}
#endif


	memset(event, ~0x00, sizeof(struct ts_event));

#if USE_POINT
	event->touch_point = buf[2] & 0x07;// 0000 1111
#else
	event->touch_point = buf[2] >>4;// 0000 1111
#endif
   if (event->touch_point == 0) {
   	        rember_point_into = false;
#ifdef LONGPRESS_LOCK_SPECKEY
		if(Origin2LockPressCnt)
		{//说明按到了search键
			if(Origin2LockPressCnt < LOCK_LONG_PRESS_CNT)
			{//没有长按
				if(lockflag ==0)
				{//键盘没锁
					input_report_key(data->input_dev,KEY_SEARCH,1);	  //158 //MENU
					input_sync(data->input_dev);
					input_report_key(data->input_dev,KEY_SEARCH,0);	  //158 //MENU
					input_sync(data->input_dev);
					//printk("menu is up ==========================\n");
				}
			}
			else
			{
				//printk("release long press !!!!!!!!!!!!!!!!!\n");
				input_report_key(data->input_dev, KEY_LOCK, 0);
				//printk("up::KEY_LOCK: %d\n", KEY_LOCK);
				input_sync(data->input_dev);
			}
			Origin2LockPressCnt = 0;
			touch_key_hold_press = 0;
		}
#endif
#ifdef NEW_PAL_DRV
		if(touch_key_hold_press)
		{
			touch_key_hold_press = 0;
			for(key=0; key<sizeof(touch_key_code)/sizeof(touch_key_code[0]); key++)
			{
				if(touch_key_press[key])
				{
					input_report_key(data->input_dev, touch_key_code[key], 0);
					touch_key_press[key] = 0;
					 input_sync(data->input_dev);
					//printk("up::KEY: %d\n", touch_key_code[key]);
				}
			}
		}
#endif
		gpress = 0;
		MotoStart =0;
		//printk("release point !!!!!!!!!!!!!!!!!\n");
		//ft5x0x_ts_release(data);
		#ifdef CHECK_KEY
		//event->penddown = Release;
		#endif
		return 0;
	}

#ifdef CONFIG_FT5X0X_MULTITOUCH
    switch (event->touch_point) {
		case 5:
			event->point[4].status = (buf[0x1b] & 0xc0)>>6;
			event->point[4].id = (buf[0x1d] & 0xf0)>>4;
			event->point[4].x = (s16)(buf[0x1b] & 0x07)<<8 | (s16)buf[0x1c];
			event->point[4].y = (s16)(buf[0x1d] & 0x07)<<8 | (s16)buf[0x1e];
		case 4:
			event->point[3].status = (buf[0x15] & 0xc0)>>6;
			event->point[3].id = (buf[0x17] & 0xf0)>>4;
			event->point[3].x = (s16)(buf[0x15] & 0x07)<<8 | (s16)buf[0x16];
			event->point[3].y = (s16)(buf[0x17] & 0x07)<<8 | (s16)buf[0x18];
		case 3:
			event->point[2].status = (buf[0x0f] & 0xc0)>>6;
			event->point[2].id = (buf[0x11] & 0xf0)>>4;
			event->point[2].x = (s16)(buf[0x0f] & 0x07)<<8 | (s16)buf[0x10];
			event->point[2].y = (s16)(buf[0x11] & 0x07)<<8 | (s16)buf[0x12];
		case 2:
			event->point[1].status = (buf[0x09] & 0xc0)>>6;
			event->point[1].id = (buf[0x0b] & 0xf0)>>4;
			event->point[1].x = (s16)(buf[0x09] & 0x07)<<8 | (s16)buf[0x0a];
			event->point[1].y = (s16)(buf[0x0b] & 0x07)<<8 | (s16)buf[0x0c];
		case 1:
			event->point[0].status = (buf[0x03] & 0xc0)>>6;
			event->point[0].id = (buf[0x05] & 0xf0)>>4;

			event->point[0].y = (s16)(buf[0x03] & 0x0f)<<8 | (s16)buf[0x04];
			event->point[0].x = (s16)(buf[0x05] & 0x0f)<<8 | (s16)buf[0x06];
			
			event->point[0].x = pdata->max_x - event->point[0].x;
			if(event->point[0].x < 0){
				event->point[0].x = 0;
			}

        default:
		    return 0;
	}
#endif
}

static void ft5x0x_report_value(void)
{
	struct ft5x0x_ts_dev *data = i2c_get_clientdata(g_dev->client);
	struct ft5x0x_platform_data *pdata = g_dev->client->dev.platform_data;
	struct ts_event *event = &data->event;
	u8 j;
	u8 i = 0;
	int key_id = 0xff;
#if 0
	printk("point is %d x0 is %d y0 is %d\n",
			//P1 status is %x ID1 is %x x1 is 0x%x y1 is 0x%x\n\n",
					event->touch_point,
					//event->point[0].status,	//event->point[0].id,
					event->point[0].y, event->point[0].x);
					//event->point[1].status, event->point[1].id,
					//event->point[1].x, event->point[1].y);
#endif
#if USE_POINT
	down_table = 0;

		for(i=0; i<event->touch_point; i++) {
		//================
		//printk("event->x[%d]:%d,event->y[%d]:%d\n", i,event->x[i],i,event->y[i]);
		if((event->point[i].y > pdata->key_min_x) && (debug1)/*&&(posx[i]<KEY_MAX_X)*/)
				{
				 for(j=0;j<KEY_NUM;j++)
				   {
						//printk("i === %d,event->point[i].x==%d,event->point[i].y===%d\n",i,event->point[i].x,event->point[i].y);
				      if((event->point[i].x >touch_key_min[j])&&(event->point[i].x <touch_key_max[j]))
				      {
				          key_id = j;
						//  printk("touch_key_hold_press = %d\n",touch_key_hold_press);
					      if((touch_key_press[key_id] == 0) && (touch_key_hold_press == 0))
					      {
					 #ifdef  LONGPRESS_LOCK_SPECKEY
					      //	printk("touch_key_code[%d]:%d\n",key_id,touch_key_code[key_id]);

					      	if(touch_key_code[key_id] == ORIGIN_KEY)
					      		{      /*
								if(++Origin2LockPressCnt>LOCK_LONG_PRESS_CNT)
									{
									Origin2LockPressCnt = LOCK_LONG_PRESS_CNT + 2;
									input_report_key(data->input_dev, KEY_LOCK, 1);
									touch_key_hold_press = 1;
									if(lockflag)
										lockflag =0;
									else
										lockflag =1;
									}*/
									Origin2LockPressCnt=1;
								break;
								}
						   //if(lockflag)
						   		//goto out;
						#endif
					       input_report_key(data->input_dev, touch_key_code[key_id], 1);
					       touch_key_press[key_id] = 1;
					       touch_key_hold_press = 1;
					     //  printk("down::KEY: %d\n", touch_key_code[key_id]);
					      }
						  break;
				      }
				   }
				}
		else
				{
				input_mt_slot(data->input_dev, event->point[i].id);

					input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->point[i].id);

					down_table |= 1 << event->point[i].id;
					input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 100);
					input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->point[i].x);
					input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->point[i].y);
					input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 100);
				   //printk("ABS_MT_TRACKING_ID == %d, ABS_MT_POSITION_X == %d, ABS_MT_POSITION_Y == %d\n",event->point[i].id,event->point[i].x,event->point[i].y);

				}


			}

	for(i=0; i<MAX_CONTACTS; i++) {
		if( ( (~down_table) & 1<<i) && !(up_table & 1<<i) )
		{
			input_mt_slot(data->input_dev, i);
			input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, -1);
                         /*printk("ABS_MT_TRACKING_ID == %d, ABS_MT_POSITION_X == %d, ABS_MT_POSITION_Y == %d\n",event->point[i]
.id,event->point[i].y,event->point[i].x);*/
		}
	}
	up_table = ~down_table;
	 input_sync(data->input_dev);
#else

	for(i=0; i<event->touch_point; i++) {
		if(event->point[i].status == 0 || event->point[i].status == 2 ) {
			input_mt_slot(data->input_dev, event->point[i].id);
			input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->point[i].id);
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 200);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->point[i].x);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->point[i].y);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 100);
		}
		else if(event->point[i].status == 1) {
			input_mt_slot(data->input_dev, event->point[i].id);
			input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, -1);
		}
	}
	input_sync(data->input_dev);
#endif




 out:
 ;
#ifdef CHECK_KEY
					 data->timer.expires	= jiffies +8;
					add_timer(&data->timer);
					down_table = 0;

#endif




}	/*end ft5x0x_report_value*/

static void ft5x0x_ts_pen_irq_work(struct work_struct *work)
{
	int ret = -1;
	//printk("==ft5x0x_ts_pen_work =\n");
	ret = ft5x0x_read_data();
	if (ret == 0) {
		ft5x0x_report_value();
	}
  enable_irq(g_dev->irq);
}

static irqreturn_t ft5x0x_ts_interrupt(int irq, void *dev_id)
{
	struct ft5x0x_ts_dev *ft5x0x_ts = dev_id;

#ifdef CHECK_KEY

			del_timer(&ft5x0x_ts->timer);
#endif
	disable_irq_nosync(g_dev->irq);
  //printk("==ft5x0x_ts_interrupt =\n");
	queue_work(ft5x0x_ts->ts_workqueue, &ft5x0x_ts->pen_event_work);

	return IRQ_HANDLED;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ft5x0x_ts_suspend(struct early_suspend *handler)
{
	int ret;
	struct ft5x0x_ts_dev *ts;
	ts =  container_of(handler, struct ft5x0x_ts_dev, early_suspend);
	
	if(ts->irq)
		disable_irq_nosync(ts->irq);

	ret = cancel_work_sync(&ts->pen_event_work);
	if (ret && ts->irq) /* if work was pending disable-count is now 2 */
		enable_irq(ts->irq);
	// ==set mode ==,
//  ft5x0x_set_reg(FT5X0X_REG_PMODE, PMODE_HIBERNATE);
}

static void ft5x0x_ts_resume(struct early_suspend *handler)
{
	struct ft5x0x_ts_dev *ts;
	ts =  container_of(handler, struct ft5x0x_ts_dev, early_suspend);
	// wake the mode
//	gpio_direction_output(RK29_PIN6_PC3, 0);
//	gpio_set_value(RK29_PIN6_PC3,GPIO_LOW);
//	msleep(50);
//	gpio_set_value(RK29_PIN6_PC3,GPIO_HIGH);

#if USE_POINT
	down_table	= 0;
	up_table	= ~0;
#endif

	if(ts->irq)
		enable_irq(ts->irq);
}
#endif  //CONFIG_HAS_EARLYSUSPEND
#ifdef CHECK_KEY
static void Touch_timer_release(unsigned long ft_ts_pdev)
{

	struct ft5x0x_ts_dev *data = ft_ts_pdev;
	int key, i=0;
	int inflag =0;
	/*if(rember_point_into)
	{
	       for(i=0;i<MAX_CONTACTS;i++)
		   {
		      printk("hand to modefy with up !!!!!!\n");
#if   1
		       input_mt_slot(data->input_dev, i);
		        input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, -1);
#endif
		  }

	input_sync(data->input_dev);
	rember_point_into = true;
	}*/

  for(i=0; i<MAX_CONTACTS; i++) {
		if( ( (~down_table) & 1<<i) && !(up_table & 1<<i) )
		{
		      printk("%d is up !!!!!!\n",i);
			input_mt_slot(data->input_dev, i);
			input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, -1);
				inflag =1;
		}
		}
  if(inflag==1)input_sync(data->input_dev);
    inflag = 0;
//===================================
#ifdef LONGPRESS_LOCK_SPECKEY

		if(Origin2LockPressCnt)
		{//说明按到了search键
			if(Origin2LockPressCnt < LOCK_LONG_PRESS_CNT)
			{//没有长按
				//if(lockflag ==0)
				{//键盘没锁
					input_report_key(data->input_dev,KEY_SEARCH,1);	  //158 //MENU
					input_sync(data->input_dev);
					input_report_key(data->input_dev,KEY_SEARCH,0);	  //158 //MENU
					input_sync(data->input_dev);
					//printk("menu is up ==========================\n");
				}
			}
			else
			{
				//printk("release long press !!!!!!!!!!!!!!!!!\n");
				//input_report_key(data->input_dev, KEY_LOCK, 0);
				//printk("up::KEY_LOCK: %d\n", KEY_LOCK);
				//input_sync(data->input_dev);
			}
			Origin2LockPressCnt = 0;
			touch_key_hold_press = 0;
		}
#endif
#ifdef NEW_PAL_DRV
		if((touch_key_hold_press)&&(debug1))
		{
			touch_key_hold_press = 0;
			for(key=0; key<sizeof(touch_key_code)/sizeof(touch_key_code[0]); key++)
			{
				if(touch_key_press[key])
				{
					input_report_key(data->input_dev, touch_key_code[key], 0);
					touch_key_press[key] = 0;
					input_sync(data->input_dev);
					//printk("up::KEY: %d\n", touch_key_code[key]);
				}
			}
		}
#endif

//===================================
	
	
	
}

#endif

static int ft5x0x_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ft5x0x_ts_dev *ft5x0x_ts;
	struct ft5x0x_platform_data *pdata = client->dev.platform_data;
	struct input_dev *input_dev;
	int err = 0;
	u8 buf_w[1];
	u8 buf_r[1];
	u8 buf[3]={0};  //w++


	if (pdata == NULL) {
		dev_err(&client->dev, "%s: platform data is null\n", __func__);
		goto exit_platform_data_null;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	if (pdata->init_platform_hw)                              
		pdata->init_platform_hw();


       err_ft5X06=i2c_master_reg8_recv(client, 0x02, buf, 2, 200*1000);  //w++6
	//err_ft5X06 = i2c_master_reg8_recv(client, 0x00, buf, 2, 200 * 1000);  //w++5

	//buf[0] = 0x00;
	//err_ft5X06 = ft5x0x_i2c_rxdata(buf,1);
	if(err_ft5X06<0){
		printk("%s:i2c_transfer fail =%d\n",__FUNCTION__,err);
		return err;
	}
	
	/*pdata->max_x and pdata->max_y should be read from touch*/

	ft5x0x_ts = (struct ft5x0x_ts_dev *)kzalloc(sizeof(*ft5x0x_ts), GFP_KERNEL);
	if (!ft5x0x_ts)	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}

	ft5x0x_ts->input_dev = input_dev;
	ft5x0x_ts->client = client;
	ft5x0x_ts->irq = client->irq;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_REP,  input_dev->evbit);
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
	set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);

	input_mt_init_slots(input_dev, MAX_CONTACTS);

	if((pdata->max_x <= 0) || (pdata->max_y <= 0))
	{
		pdata->max_x = SCREEN_MAX_X;
		pdata->max_y = SCREEN_MAX_Y;
	}

	if(pdata->key_min_x <= 0)
	{
		pdata->key_min_x = KEY_MIN_X;
	}

	input_set_abs_params(input_dev,ABS_MT_POSITION_X, 0, pdata->max_x, 0, 0);
	input_set_abs_params(input_dev,ABS_MT_POSITION_Y, 0, pdata->max_y, 0, 0);
	input_set_abs_params(input_dev,ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_dev,ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);
#if  CONFIG_TOUCH_PANEL_KEY
	set_bit(KEY_HOMEPAGE, input_dev->keybit);
	set_bit(KEY_MENU, input_dev->keybit);
	set_bit(KEY_BACK, input_dev->keybit);
	set_bit(KEY_SEARCH, input_dev->keybit);
#ifdef LONGPRESS_LOCK_SPECKEY
	//	set_bit(KEY_SEARCH, input_dev->keybit);
	set_bit(KEY_LOCK, input_dev->keybit);
#endif
#endif

	input_dev->name		= FT5X0X_NAME;		//dev_name(&client->dev)
	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev,
		"ft5x0x_ts_probe: failed to register input device: %s\n",
		dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}

	g_dev = ft5x0x_ts;

	i2c_set_clientdata(client, ft5x0x_ts);
	INIT_WORK(&ft5x0x_ts->pen_event_work, ft5x0x_ts_pen_irq_work);
	ft5x0x_ts->ts_workqueue = create_workqueue(FT5X0X_NAME);
	if (!ft5x0x_ts->ts_workqueue) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}

  //if(pdata->init_platform_hw)
     // pdata->init_platform_hw();

  //ft5x0x_set_reg(0x80,0x64);

  if(!ft5x0x_ts->irq)
  {
    dev_dbg(&ft5x0x_ts->client->dev, "no IRQ?\n");
    return -ENODEV;
  }
  else
  {
    ft5x0x_ts->irq = gpio_to_irq(ft5x0x_ts->irq);
  }

  err = request_irq(ft5x0x_ts->irq, ft5x0x_ts_interrupt, IRQF_TRIGGER_FALLING/*IRQF_DISABLED*/, "ft5x0x_ts", ft5x0x_ts);
	if (err < 0) {
		dev_err(&client->dev, "ft5x0x_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}

	disable_irq_nosync(g_dev->irq);

#ifdef CONFIG_HAS_EARLYSUSPEND
	ft5x0x_ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ft5x0x_ts->early_suspend.suspend = ft5x0x_ts_suspend;
	ft5x0x_ts->early_suspend.resume	= ft5x0x_ts_resume;
	register_early_suspend(&ft5x0x_ts->early_suspend);
#endif
#ifdef CHECK_KEY
		setup_timer(&ft5x0x_ts->timer, Touch_timer_release, (unsigned long)ft5x0x_ts);
#endif

//wake the CTPM
//	__gpio_as_output(GPIO_FT5X0X_WAKE);
//	__gpio_clear_pin(GPIO_FT5X0X_WAKE);		//set wake = 0,base on system
//	 msleep(100);
//	__gpio_set_pin(GPIO_FT5X0X_WAKE);			//set wake = 1,base on system
//	msleep(100);
//	ft5x0x_set_reg(0x88, 0x05); //5, 6,7,8
//	ft5x0x_set_reg(0x80, 30);
//	msleep(50);

	buf_w[0] = 6;
	err = ft5x0x_set_reg(0x88,6);
	//ft5x0x_i2c_txdata(0x88, buf_w, 1);    /* adjust frequency 60Hz */

	buf_r[0] = 0x88;
	err = ft5x0x_i2c_rxdata(buf_r,1);

    enable_irq(g_dev->irq);

	printk("==ft5x0x_ts_probe = %0x,max_x=%d,max_y=%d\n", buf_r[0],pdata->max_x,pdata->max_y);

    return 0;

exit_input_register_device_failed:
	input_free_device(input_dev);
exit_input_dev_alloc_failed:
	free_irq(client->irq, ft5x0x_ts);
	//free_irq(IRQ_EINT(6), ft5x0x_ts);
exit_irq_request_failed:
exit_platform_data_null:
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
exit_create_singlethread:
	printk("==singlethread error =\n");
	i2c_set_clientdata(client, NULL);
	kfree(ft5x0x_ts);
exit_alloc_data_failed:
exit_check_functionality_failed:
	return err;
}

static int __devexit ft5x0x_ts_remove(struct i2c_client *client)
{
	struct ft5x0x_ts_dev *ft5x0x_ts = i2c_get_clientdata(client);
	unregister_early_suspend(&ft5x0x_ts->early_suspend);
	free_irq(client->irq, ft5x0x_ts);
	input_unregister_device(ft5x0x_ts->input_dev);
	kfree(ft5x0x_ts);
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
	i2c_set_clientdata(client, NULL);
	return 0;
}

static const struct i2c_device_id ft5x0x_ts_id[] = {
	{ FT5X0X_NAME, 0 },{ }
};
MODULE_DEVICE_TABLE(i2c, ft5x0x_ts_id);

static struct i2c_driver ft5x0x_ts_driver = {
	.probe		= ft5x0x_ts_probe,
	.remove		= __devexit_p(ft5x0x_ts_remove),
	.id_table	= ft5x0x_ts_id,
	.driver	= {
		.name	= FT5X0X_NAME,
		.owner	= THIS_MODULE,
	},
};
#ifdef LONGPRESS_LOCK_SPECKEY
static DRIVER_ATTR(get_lock_status, 0777, glock_status_show, NULL);
#endif
static DRIVER_ATTR(MOTOenable, 0666, Moto_status, Moto_control);

static int __init ft5x0x_ts_init(void)
{
        int ret;
ret = i2c_add_driver(&ft5x0x_ts_driver);
        if (ret)
        {
            printk("Register 5406_ts driver failed.\n");
            return ret;
        }
#ifdef LONGPRESS_LOCK_SPECKEY
        if(err_ft5X06>=0)   //w++
          ret =driver_create_file(&ft5x0x_ts_driver.driver, &driver_attr_get_lock_status);
#endif
	if(err_ft5X06>=0)   //
	ret =driver_create_file(&ft5x0x_ts_driver.driver, &driver_attr_MOTOenable);
           return ret;
}

static void __exit ft5x0x_ts_exit(void)
{
	i2c_del_driver(&ft5x0x_ts_driver);
#ifdef LONGPRESS_LOCK_SPECKEY
        driver_remove_file(&ft5x0x_ts_driver.driver, &driver_attr_get_lock_status);
#endif
	driver_remove_file(&ft5x0x_ts_driver.driver, &driver_attr_MOTOenable);
}

module_init(ft5x0x_ts_init);
module_exit(ft5x0x_ts_exit);

MODULE_AUTHOR("<wenfs@Focaltech-systems.com>");
MODULE_DESCRIPTION("FocalTech ft5x0x TouchScreen driver");
MODULE_LICENSE("GPL");

