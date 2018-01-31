/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************************
 * driver/input/touchscreen/i2cpca955x.c
 *Copyright 	:ROCKCHIP  Inc
 *Author	: 	sfm
 *Date		:  2010.2.5
 *This driver use for rk28 chip extern touchscreen. Use i2c IF ,the chip is pca955x
 *description£º
 ********************************************************************************************/
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/async.h>
#include <linux/workqueue.h>
#include <mach/gpio.h>
#include <linux/irq.h>
#include <mach/board.h>

#ifdef CONFIG_ANDROID_POWER
#include <linux/android_power.h>
#endif

#define 	IT7260_DEBUG	 0

#if IT7260_DEBUG
#define it7250_debug(msg...) printk(msg)
#else
#define it7250_debug(msg...)
#endif

/******************************************
		DEBUG
*** ***************************************/
#define IT7260_IIC_SPEED 		200*1000

#define IT7260_MAX_X	 800//1024//1020//800
#define IT7260_MAX_Y	600//768//600//480

#define Mulitouch_Mode  1
#define Singltouch_Mode 0

struct touch_event{
	short x;
	short y;
};
struct MultiTouch_event{
	short x1;
	short y1;
	short x2;
	short y2;
	char p1_press;
	char p2_press;	
};
#define TS_POLL_DELAY	(10*1000000) /* ns delay before the first sample */
#define TS_POLL_PERIOD	(15*1000000) /* ns delay between samples */

struct it7260_dev{	
	struct i2c_client *client;
	struct input_dev *input;
	spinlock_t	lock;
	char	phys[32];
	int 		irq;
#if Singltouch_Mode
	struct touch_event  point;  
#else
	struct MultiTouch_event  point;
	char   P_state;
	char   p_DelayTime;
#endif

	struct delayed_work work;
	struct workqueue_struct *wq;
	bool		pendown;
	bool 	 status;
	bool    pass;
	struct timer_list timer;//hrtimer  timer;
	int has_relative_report;
};


#define COMMAND_BUFFER_INDEX 0x20
#define QUERY_BUFFER_INDEX 0x80
#define COMMAND_RESPONSE_BUFFER_INDEX 0xA0
#define POINT_BUFFER_INDEX 0xE0
#define QUERY_SUCCESS 0x00
#define QUERY_BUSY 0x01
#define QUERY_ERROR 0x02
#define QUERY_POINT 0x80


static char cal_status = 0;

/*read the it7260 register ,used i2c bus*/
static int it7260_read_regs(struct i2c_client *client, u8 reg, u8 buf[], unsigned len)
{
	int ret; 
	ret = i2c_master_reg8_recv(client, reg, buf, len, IT7260_IIC_SPEED);
	return ret; 
}


/* set the it7260 registe,used i2c bus*/
static int it7260_set_regs(struct i2c_client *client, u8 reg, u8 const buf[], unsigned short len)
{
	int ret; 
	ret = i2c_master_reg8_send(client, reg, buf, (int)len, IT7260_IIC_SPEED);
	return ret;
}

void ReadQueryBuffer(struct i2c_client *client,u8 pucData[])
{
	it7260_read_regs(client,QUERY_BUFFER_INDEX,pucData,1);
}

bool ReadCommandResponseBuffer(struct i2c_client *client,u8 pucData[], unsigned int unDataLength)
{
	return 	it7260_read_regs(client,COMMAND_RESPONSE_BUFFER_INDEX,pucData,unDataLength);

}

bool ReadPointBuffer(struct i2c_client *client,u8 pucData[])
{
	return 	it7260_read_regs(client,POINT_BUFFER_INDEX,pucData,14);

}


int WriteCommandBuffer(struct i2c_client *client,u8 pucData[], unsigned int unDataLength)
{
	return it7260_set_regs(client,COMMAND_BUFFER_INDEX,pucData,unDataLength);
}



/*
sleep
*/
static void it7260_chip_sleep(void)
{ 	
/*
	u8 pucPoint1[12] ={0x12,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
	u8 pucPoint2[12] ={0x11,0x01,0x01};
	int aaa;
	aaa = WriteCommandBuffer(ts_dev->client,pucPoint1, 10);
	if(aaa <0)
	{
		printk("set mode err\n");
	}
	aaa = WriteCommandBuffer(ts_dev->client,pucPoint2, 3);
*/
}
/*
wake up
*/
static void it7260_chip_wakeup(void)
{

}


struct it7260_dev *g_dev;
#if 1
void ite_ts_test()
{

	u8 ucWriteLength, ucReadLength;
	u8 pucData[128];
	u8 ucQuery;
	ucWriteLength = 1;
	ucReadLength = 0x0A;
	pucData[0] = 0x00;
	
	// Query
	
	do
	{
		ReadQueryBuffer(g_dev->client,&ucQuery);
		if(ucQuery == 0)
			break;
	}while(ucQuery & QUERY_BUSY);

       //IdentifyCapSensor 
       WriteCommandBuffer(g_dev->client,pucData, ucWriteLength);

	it7260_read_regs(g_dev->client,COMMAND_RESPONSE_BUFFER_INDEX,pucData,10);
	printk("[%c][%c][%c][%c][%c][%c][%c]\n",pucData[1],pucData[2],pucData[3],pucData[4],pucData[5],pucData[6],pucData[7]);

       // 
       ucWriteLength = 2;
	pucData[0] = 0x01;
	pucData[1] = 0x04;
       WriteCommandBuffer(g_dev->client,pucData, ucWriteLength);
	   
       it7260_read_regs(g_dev->client,COMMAND_RESPONSE_BUFFER_INDEX,pucData,2);
       printk("[%x][%x]\n",pucData[0],pucData[1]);

	ucWriteLength = 4;
	pucData[0] = 0x02;
	pucData[1] = 0x04;
	pucData[2] = 0x01;
	pucData[3] = 0x00;
       WriteCommandBuffer(g_dev->client,pucData, ucWriteLength);

	ucWriteLength = 2;
	pucData[0] = 0x01;
	pucData[1] = 0x04;
       WriteCommandBuffer(g_dev->client,pucData, ucWriteLength);
	   
       it7260_read_regs(g_dev->client,COMMAND_RESPONSE_BUFFER_INDEX,pucData,2);
	   
       printk("[%x][%x]\n",pucData[0],pucData[1]);
//	printk("[%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x][%x]\n",pucData[0],pucData[1],pucData[2],pucData[3],pucData[4],pucData[5],pucData[6],
//		pucData[7],pucData[8],pucData[9],pucData[10],pucData[11],pucData[12],pucData[13]);
      

/*	if(pucData[1] != 'I'
	|| pucData[2] != 'T'
	|| pucData[3] != 'E'
	|| pucData[4] != '7'
	|| pucData[5] != '2'
	|| pucData[6] != '6'
	|| pucData[7] != '0')
	{
		// firmware signature is not match
		return false;
	}*/

	return ;

}
#endif




static int set_mode(struct it7260_dev *ts_dev)
{
	u8 pucPoint[12] ={0x13,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
	int ret;
	printk("start to calibration......\n");
	ret = WriteCommandBuffer(ts_dev->client,pucPoint, 12);
	if(ret <0)
	{
		printk("set mode err\n");
		cal_status = 0;
	}
	else
	{
		printk("it7260 set mode ok\n");
		cal_status = 1;
	}
	return ret;
}

static int set_sleep_mode(struct it7260_dev *ts_dev)
{
	u8 pucPoint[3] ={0x04,0x00,0x02};
	u8 pucPoint1[2] ={0x11,0x01};
	int aaa;
	aaa = WriteCommandBuffer(ts_dev->client,pucPoint1, 2);
	//aaa = WriteCommandBuffer(ts_dev->client,pucPoint, 3);
	if(aaa <0)
	{
		printk("set mode err\n");
	}

}

static int set_active_mode(struct it7260_dev *ts_dev)
{
	u8 pucPoint[12] ={0x04,0x00,0x00};
	int aaa;
	aaa = WriteCommandBuffer(ts_dev->client,pucPoint, 3);
	if(aaa <0)
	{
		printk("set mode err\n");
	}
}


static int read_point(struct it7260_dev *ts_dev )
{


	u8 pucPoint[20];
	u8 ucQuery =0;
	u8 readbuf[2];
	u8 i;
	int xraw, yraw, xtmp, ytmp;
	char pressure_point,z,w;
	int finger2_pressed=0;


	ReadQueryBuffer(ts_dev->client,&ucQuery);
	it7250_debug("ucQuery = 0x%x\n",ucQuery);
	if(ucQuery == 0 )
	{	
		it7250_debug("ucQuery == 0  return \n");

		return 0;
	}

	
	if(ucQuery == 2)
	{
		ReadCommandResponseBuffer(ts_dev->client,readbuf, 2);
		it7250_debug(" read buf [0] = 0x0%x , buf [1] = 0x0%x\n",readbuf[0],readbuf[1]);
		it7260_read_regs(ts_dev->client,POINT_BUFFER_INDEX,pucPoint,14);

		return 0;
	}
	else
	{
		if(ucQuery & 0x80)
		{
			it7260_read_regs(ts_dev->client,POINT_BUFFER_INDEX,pucPoint,14);

			if(pucPoint[0] & 0xF0)				
			{
				it7250_debug("a\n");
				return 0;
			}				
			else	{					
				if(pucPoint[1] & 0x01)					
				{
					it7250_debug("b\n");
					return 0;
				}			
			}


			if(((pucPoint[0] & 0x07)==0)/*|| GPIOGetPinLevel(it7260_IRQ_PIN)*/)				
			{				
				//sisdbg("=Read_Point pull=  [%d][%d]\n",pucPoint[0] & 0x07,GPIOGetPinLevel(it7260_IRQ_PIN));
#if Singltouch_Mode					
				ts_dev->status = 0;
				ts_dev->pass = 1;

				input_report_key(ts_dev->input, BTN_TOUCH, 0);					
				//input_report_abs(ts_dev->input, ABS_PRESSURE, 0);					
				input_sync(ts_dev->input);
				return;
#else					
				ts_dev->pass = 1;	
				input_report_abs(ts_dev->input, ABS_MT_TOUCH_MAJOR, 0);					
				input_report_abs(ts_dev->input, ABS_MT_WIDTH_MAJOR, 15);	
				input_report_abs(ts_dev->input, ABS_MT_POSITION_X, ts_dev->point.x1);					
				input_report_abs(ts_dev->input, ABS_MT_POSITION_Y, ts_dev->point.y1);
				input_report_key(ts_dev->input, BTN_TOUCH, 0);
				input_mt_sync(ts_dev->input);
				it7250_debug("TP up\n");
				if(ts_dev->has_relative_report ==2)
				{
					ts_dev->has_relative_report = 0;
					input_report_abs(ts_dev->input, ABS_MT_TOUCH_MAJOR, 0);					
					input_report_abs(ts_dev->input, ABS_MT_WIDTH_MAJOR, 15);	
					input_report_abs(ts_dev->input, ABS_MT_POSITION_X, ts_dev->point.x2);					
					input_report_abs(ts_dev->input, ABS_MT_POSITION_Y, ts_dev->point.y2);	
					input_report_key(ts_dev->input, BTN_2, 0);
					it7250_debug("TP up\n");
					input_mt_sync(ts_dev->input);
				}
				input_sync(ts_dev->input);
				
				return 0;
#endif			
	
			}	
			else
			{
					if(pucPoint[0] & 0x01)				
					{										
											
						xraw = ((pucPoint[3] & 0x0F) << 8) + pucPoint[2];					
						yraw = ((pucPoint[3] & 0xF0) << 4) + pucPoint[4];					
						pressure_point=pucPoint[5]&0x0f;										
						xtmp = xraw;				
						ytmp = yraw;		
#if Mulitouch_Mode
						ts_dev->point.x1 = xtmp;
						ts_dev->point.y1 = ytmp;
						ts_dev->has_relative_report = 1;
#endif
						if(pressure_point==4)					
						{						
							z=10;						
							w=15;					
						}					
						else					
						{						
							z=10;						
							w=15;					
						}					
						it7250_debug("=Read_Point1 x=%d y=%d p=%d=\n",xtmp,ytmp,pressure_point);
						
#if Singltouch_Mode		
						if(ts_dev->pass == 1)
						{
							ts_dev->pass = 0;
							return 0;
						}///

						if(ts_dev->status == 0)
						{
							ts_dev->status = 1;
							input_report_abs(ts_dev->input, ABS_X, xtmp);					
							input_report_abs(ts_dev->input, ABS_Y, ytmp);	
							input_report_key(ts_dev->input, BTN_TOUCH, 1);		
						}else{
							input_report_abs(ts_dev->input, ABS_X, xtmp);					
							input_report_abs(ts_dev->input, ABS_Y, ytmp);
						}
					
						input_report_abs(ts_dev->input, ABS_PRESSURE, 1);					
						ts_dev->pendown = 1;
						input_sync(ts_dev->input);
						
#else										
						if(ts_dev->pass == 1)
						{
							ts_dev->pass = 0;
							return 0;
						}///

						input_report_abs(ts_dev->input, ABS_MT_TOUCH_MAJOR, z);					
						input_report_abs(ts_dev->input, ABS_MT_WIDTH_MAJOR, w);										
						input_report_abs(ts_dev->input, ABS_MT_POSITION_X, xtmp);					
						input_report_abs(ts_dev->input, ABS_MT_POSITION_Y, ytmp);		
						input_report_key(ts_dev->input, BTN_TOUCH, 1);
						ts_dev->pendown = 1;
						it7250_debug("TP down\n");
						input_mt_sync(ts_dev->input);
						
#endif												
					}
#if Mulitouch_Mode

					 if(pucPoint[0] & 0x02)				
					 {					
					 	xraw = ((pucPoint[7] & 0x0F) << 8) + pucPoint[6];					
						yraw = ((pucPoint[7] & 0xF0) << 4) + pucPoint[8];					
						pressure_point=pucPoint[9]&0x0f;					
						xtmp = xraw;					
						ytmp = yraw;          
						ts_dev->point.x2 = xtmp;
						ts_dev->point.y2 = ytmp;
						ts_dev->has_relative_report = 2;
						it7250_debug("=Read_Point2 x=%d y=%d p=%d=\n",xtmp,ytmp,pressure_point);					
						if(pressure_point==4)					
						{						
							z=10;						
							w=15;					
						}					
						else					
							{						
							z=10;						
							w=15;					
						}
#if Singltouch_Mode		
						if(ts_dev->pass == 1)
						{
							ts_dev->pass = 0;
							return 0;
						}

						input_report_abs(ts_dev->input, ABS_X, xtmp);					
						input_report_abs(ts_dev->input, ABS_Y, ytmp);					
						input_report_key(ts_dev->input, BTN_TOUCH, 1);					
						ts_dev->pendown = 1;
						input_sync(ts_dev->input);
#else					
						if(ts_dev->pass == 1)
						{
							ts_dev->pass = 0;
							return 0;
						}

						input_report_abs(ts_dev->input, ABS_MT_TOUCH_MAJOR, z);					
						input_report_abs(ts_dev->input, ABS_MT_WIDTH_MAJOR, w);					
						input_report_abs(ts_dev->input, ABS_MT_POSITION_X, xtmp);					
						input_report_abs(ts_dev->input, ABS_MT_POSITION_Y, ytmp);	
						input_report_key(ts_dev->input, BTN_2, 1);
						ts_dev->pendown = 1;
						it7250_debug("TP down\n");
						input_mt_sync(ts_dev->input);
#endif
					
					}		
#endif
	
				}
			input_sync(ts_dev->input);	
		}
		
	}
	return 0;
}

 static void it7260_dostimer(unsigned long data)
 {
	 struct it7260_dev *ts_dev = (struct it7260_dev *)data;	 
	 read_point(ts_dev);
 }


 static void it7260_work(struct work_struct *work)
{
	struct it7260_dev *ts_dev =
		container_of(to_delayed_work(work), struct it7260_dev, work);
	read_point(ts_dev);

out:               
	if (ts_dev->pendown){
		queue_delayed_work(ts_dev->wq, &ts_dev->work, msecs_to_jiffies(10));
		ts_dev->pendown = 0;
	}
	else{
		enable_irq(ts_dev->irq);
	}
	
}
static irqreturn_t it7260_irq_hander(int irq, void *handle)
{
	struct it7260_dev *ts_dev = handle;

	if (1/*!ts_dev->get_pendown || likely(ts_dev->get_pendown_state())*/) {
		disable_irq_nosync(ts_dev->irq);
		queue_delayed_work(ts_dev->wq, &ts_dev->work, 0);
	}

	return IRQ_HANDLED;
}

static int it7260_detach_client(struct i2c_client *client)
{
	printk("************>%s.....%s.....\n",__FILE__,__FUNCTION__);
	return 0;
}

static void it7260_shutdown(struct i2c_client *client)
{
	printk("************>%s.....%s.....\n",__FILE__,__FUNCTION__);
}
#ifdef CONFIG_ANDROID_POWER
static void suspend(android_early_suspend_t *h)
{
	printk("************>%s.....%s.....\n",__FILE__,__FUNCTION__);
}
static void resume(android_early_suspend_t *h)
{
	printk("************>%s.....%s.....\n",__FILE__,__FUNCTION__);
}
static android_early_suspend_t ts_early_suspend;
#endif

ssize_t tp_cal_show(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
    
	if(cal_status)
		return sprintf(buf,"successful");
	else
		return sprintf(buf,"fail");
}

ssize_t tp_cal_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
    
    if( !strncmp(buf,"tp_cal" , strlen("tp_cal")) )
    {
		set_mode(g_dev);
    }

    return count;
}

struct kobj_attribute tp_cal_attrs = 
{
        .attr = {
                .name = "tp_calibration",
                .mode = 0777},
        .show = tp_cal_show,
        .store = tp_cal_store,
};

struct attribute *tp_attrs[] = 
{
        &tp_cal_attrs.attr,
        NULL
};

static struct kobj_type tp_kset_ktype = {
	.sysfs_ops	= &kobj_sysfs_ops,
	.default_attrs = &tp_attrs[0],
};
static int tp_cal_add_attr(struct it7260_dev *ts_dev)
{
	int result;
	struct input_dev *input;
	struct kobject *parentkobject; 
	struct kobject * me = kmalloc(sizeof(struct kobject) , GFP_KERNEL );
	if( !me )
		return -ENOMEM;
	memset(me ,0,sizeof(struct kobject));
	kobject_init( me , &tp_kset_ktype );
	parentkobject = &ts_dev->input->dev.kobj ;
	result = kobject_add( me , parentkobject->parent->parent->parent, "%s", "tp_calibration" );	
	return result;
}

static void it7260_remove(struct i2c_client * client)
{

}
static int it7260_probe(struct i2c_client *client ,const struct i2c_device_id *id)
{
	struct it7260_dev *ts_dev;
	struct input_dev *input;
	struct it7260_platform_data *pdata = pdata = client->dev.platform_data;
	int i, ret=0;

	
	if (!pdata) {
		dev_err(&client->dev, "platform data is required!\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;

	ts_dev=kzalloc(sizeof(struct it7260_dev), GFP_KERNEL);
	if(!ts_dev)
	{
		printk("it7260 failed to allocate memory!!\n");
		goto nomem;
	}

	input = input_allocate_device();
	if(!input)
	{
		printk("it7260 allocate input device failed!!!\n"); 
		goto fail1;
	}	

	ts_dev->client = client;
	ts_dev->status = 0;
	ts_dev->pendown = 0;
	ts_dev->pass = 0;
	ts_dev->input = input;		
	ts_dev->irq = client->irq;
	ts_dev->has_relative_report = 0;
	snprintf(ts_dev->phys, sizeof(ts_dev->phys),
	 "%s/input0", dev_name(&client->dev));
	input->name = "it7260 touchscreen";
	input->phys = ts_dev->phys;
	input->id.bustype = BUS_I2C;

	
	ts_dev->wq = create_rt_workqueue("it7260_wq");
	INIT_DELAYED_WORK(&ts_dev->work, it7260_work);
	
#if Singltouch_Mode
	input->evbit[0] = BIT_MASK(EV_ABS)|BIT_MASK(EV_KEY)|BIT_MASK(EV_SYN);
	input->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(input, ABS_X, 0, IT7260_MAX_X, 0, 0);
  	input_set_abs_params(input, ABS_Y, 35, IT7260_MAX_Y	, 0, 0);
#else
	input->evbit[0] = BIT_MASK(EV_ABS)|BIT_MASK(EV_KEY)|BIT_MASK(EV_SYN);
	input->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input->keybit[BIT_WORD(BTN_2)] = BIT_MASK(BTN_2); //jaocbchen for dual

	input_set_abs_params(input, ABS_X, 0, IT7260_MAX_X, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, IT7260_MAX_Y, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_TOOL_WIDTH, 0, 15, 0, 0);
	input_set_abs_params(input, ABS_HAT0X, 0, IT7260_MAX_X, 0, 0);
	input_set_abs_params(input, ABS_HAT0Y, 0, IT7260_MAX_Y, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_X,0, IT7260_MAX_X, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, IT7260_MAX_Y, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_MT_WIDTH_MAJOR, 0, 15, 0, 0);

	for (i = 0; i < (BITS_TO_LONGS(ABS_CNT)); i++)
		printk("%s::input->absbit[%d] = 0x%x \n",__FUNCTION__,i,input->absbit[i]);
#endif

	if (pdata->init_platform_hw)
		pdata->init_platform_hw();

	if (!ts_dev->irq) {
		dev_dbg(&ts_dev->client->dev, "no IRQ?\n");
		return -ENODEV;
	}else{
		ts_dev->irq = gpio_to_irq(ts_dev->irq);
	}

	printk("client->dev.driver->name %s\n",client->dev.driver->name);
	ret = request_irq(ts_dev->irq, it7260_irq_hander, IRQF_TRIGGER_LOW,
			client->dev.driver->name, ts_dev);
	
	if (ret < 0) {
		dev_err(&client->dev, "irq %d busy?\n", ts_dev->irq);
		goto fail3;
	}
	
	ret = input_register_device(input);	
	if(ret<0)
	{
		printk("it7260 register input device failed!!!!\n");
		goto fail2;
	}
	
#ifdef CONFIG_ANDROID_POWER
   	ts_early_suspend.suspend = suspend;
    ts_early_suspend.resume = resume;
    android_register_early_suspend(&ts_early_suspend);
#endif	
	g_dev = ts_dev;
//	ite_ts_test();
	set_mode(ts_dev);
     
	printk("it7260 register input device ok!!!!\n");
	return 0;

fail3:
	free_irq(ts_dev->irq,ts_dev);
fail2:

	input_unregister_device(input);
	input = NULL;
fail1:
	input_free_device(input);
nomem:
	kfree(ts_dev);
	return ret;

}


static struct i2c_device_id it7260_idtable[] = {
	{ "it7260_touch", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, it7260_idtable);

static struct i2c_driver it7260_driver  = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "it7260_touch"
	},
	.id_table	= it7260_idtable,
	.probe = it7260_probe,
	.remove 	= __devexit_p(it7260_remove),
};

static int __init it7260_init(void)
{ 
	return i2c_add_driver(&it7260_driver);
}

static void __exit it7260_exit(void)
{
	i2c_del_driver(&it7260_driver);
}
module_init(it7260_init);
module_exit(it7260_exit);
MODULE_DESCRIPTION ("it7260 touchscreen driver");
MODULE_AUTHOR("llx<llx@rockchip.com>");
MODULE_LICENSE("GPL");


