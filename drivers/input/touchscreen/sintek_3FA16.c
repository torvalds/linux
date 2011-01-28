/****************************************************************************************
 * driver/input/touchscreen/hannstar_p1003.c
 *Copyright 	:ROCKCHIP  Inc
 *Author	: 	sfm
 *Date		:  2010.2.5
 *This driver use for rk28 chip extern touchscreen. Use i2c IF ,the chip is Hannstar
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
#include <mach/gpio.h>
#include <linux/irq.h>
#include <mach/board.h>
#include "malata.h"


#define MAX_SUPPORT_POINT	2// //  4
#define PACKGE_BUFLEN		10

#if 0
#define DBG(x...) printk(KERN_INFO x)
#else
#define DBG(x...) do { } while (0)
#endif
//#define Singltouch_Mode
#define SAKURA_DBG                  0
#if SAKURA_DBG 
#define sakura_dbg_msg(fmt,...)       do {                                      \
                                   printk("sakura dbg msg------>"                       \
                                          " (func-->%s ; line-->%d) " fmt, __func__, __LINE__ , ##__VA_ARGS__); \
                                  } while(0)
#define sakura_dbg_report_key_msg(fmt,...)      do{                                                     \
                                                    printk("sakura report " fmt,##__VA_ARGS__);          \
                                                }while(0)
#else
#define sakura_dbg_msg(fmt,...)       do {} while(0)
#define sakura_dbg_report_key_msg(fmt,...)      do{}while(0)
#endif
struct point_data {	
	short status;	
	short x;	
	short y;
    short z;
};

struct multitouch_event{
	struct point_data point_data[MAX_SUPPORT_POINT];
	int contactid;
    int validtouch;
};

struct ts_p1003 {
	struct input_dev	*input;
	char			phys[32];
	struct delayed_work	work;
	struct workqueue_struct *wq;	
	struct i2c_client	*client;
    struct multitouch_event mt_event;
	u16			model;
	spinlock_t 	lock;
	bool		pendown;
	bool 	 	status;
	int			irq;
	int         delayed_work_tp;
	int 		has_relative_report;
	int			(*get_pendown_state)(void);
	void		(*clear_penirq)(void);
};

int p1003_get_pendown_state(void)
{
	return 0;
}

static void p1003_report_event(struct ts_p1003 *ts,struct multitouch_event *tc)
{
	struct input_dev *input = ts->input;
    int i,pandown = 0;
	dev_dbg(&ts->client->dev, "UP\n");
	DBG("Enter:%s %d\n",__FUNCTION__,__LINE__);	
    for(i=0; i<MAX_SUPPORT_POINT;i++){			
        if(tc->point_data[i].status >= 0){
            pandown |= tc->point_data[i].status;
            input_report_abs(input, ABS_MT_TRACKING_ID, i);							
            input_report_abs(input, ABS_MT_TOUCH_MAJOR, tc->point_data[i].status);				
            input_report_abs(input, ABS_MT_WIDTH_MAJOR, 0);	
            input_report_abs(input, ABS_MT_POSITION_X, tc->point_data[i].x);				
            input_report_abs(input, ABS_MT_POSITION_Y, tc->point_data[i].y);				
            input_mt_sync(input);	

            sakura_dbg_report_key_msg("ABS_MT_TRACKING_ID = %x, ABS_MT_TOUCH_MAJOR = %x\n ABS_MT_POSITION_X = %x, ABS_MT_POSITION_Y = %x\n",i,tc->point_data[i].status,tc->point_data[i].x,tc->point_data[i].y);
#if defined(CONFIG_HANNSTAR_DEBUG)
			//printk("hannstar p1003 Px = [%d],Py = [%d] \n",tc->point_data[i].x,tc->point_data[i].y);
#endif

            if(tc->point_data[i].status == 0)					
            	tc->point_data[i].status--;			
        }
        
    }	

    ts->pendown = pandown;
    input_sync(input);
}

#if defined (Singltouch_Mode)
static void p1003_report_single_event(struct ts_p1003 *ts,struct multitouch_event *tc)
{
	struct input_dev *input = ts->input;
    int cid;

    cid = tc->contactid;
    if (ts->status) {
        input_report_abs(input, ABS_X, tc->point_data[cid].x);
        input_report_abs(input, ABS_Y, tc->point_data[cid].y);
        input_sync(input);
    }
    if(ts->pendown != ts->status){
        ts->pendown = ts->status;
        input_report_key(input, BTN_TOUCH, ts->status);
        input_sync(input);
        sakura_dbg_report_key_msg("%s x =0x%x,y = 0x%x \n",ts->status?"down":"up",tc->point_data[cid].x,tc->point_data[cid].y);
    }
}
#endif

static inline int p1003_check_firmwork(struct ts_p1003 *ts)
{
    int data;
    char buf[10];
	data = i2c_master_reg8_recv(ts->client, 26, buf, 6, 100*1000);
	if(data < 0){
		dev_err(&ts->client->dev, "i2c io error %d \n", data);
		return data;
	}
	//printk("sintek reg[0] = %x ,reg[1] = %x, reg[2] = %x, reg[3] = %x\n" , buf[0],buf[1],buf[2],buf[3]);
	//printk("sintek reg[4] = %x ,reg[5] = %x, reg[6] = %x, reg[7] = %x\n" , buf[4],buf[5],buf[6],buf[7]);
	buf[0] = 0xa4; /*automatically jump sleep mode*/
	//buf[0] =  0x0;
	data = i2c_master_reg8_send(ts->client, MALATA_POWER_MODE, buf, 1, 100*1000);
	if(data < 0){
		printk("i2c io error %d line=%d\n", data,__LINE__);
		return data;
	}
	buf[0] = 0x03;
	data = i2c_master_reg8_send(ts->client, MALATA_SPECOP, buf, 1, 100*1000);
	if(data < 0){
		printk( "i2c io error %d line=%d\n", data,__LINE__);
		return data;
	}
	#if 0
	buf[0] = 0x80;
	data = i2c_master_reg8_send(ts->client, MALATA_INT_MODE, buf, 1, 200*1000);
	if(data < 0){
		printk( "i2c io error %d line=%d\n", data,__LINE__);
		return data;
	}
	#endif
	data = i2c_master_reg8_recv(ts->client, MALATA_DATA_INFO, buf, 4, 100*1000);
	if(data < 0){
		printk( "i2c io error %d line=%d\n", data,__LINE__);
		return data;
	}
	//printk("sintek reg[0] = %x ,reg[1] = %x, reg[2] = %x, reg[3] = %x\n" , buf[0],buf[1],buf[2],buf[3]);
	//printk("sintek reg[4] = %x ,reg[5] = %x, reg[6] = %x, reg[7] = %x\n" , buf[4],buf[5],buf[6],buf[7]);
    return data;
}

#define DATA_START	MALATA_DATA_INFO
#define DATA_END	MALATA_DATA_END
#define DATA_LEN	(DATA_END - DATA_START)
#define DATA_OFF(x) ((x) - DATA_START)


static inline int p1003_read_values(struct ts_p1003 *ts, struct multitouch_event *tc)
{
       int data=0;
       char buf[10];
	data = i2c_master_reg8_recv(ts->client, 0x00, buf, 10, 100*1000);
	if(data < 0){
		printk("-->%s i2c io error %d line=%d\n",__FUNCTION__, data,__LINE__);
		return data;
	}
#if 0
	if(buf[0]==0xff){
		printk("MALATA_TOUCH_NUM is 0xff full\n");
		return -1;
	}
#endif
	///printk("MALATA_TOUCH_NUM = %x\n",buf[0]);
	if(buf[0] == 1)
	{
		ts->pendown = 1;
		tc->point_data[0].status = 1;
		tc->point_data[1].status = 0;
	}
	else if (buf[0] == 2){
		ts->pendown = 1;
		tc->point_data[0].status = 1;
		tc->point_data[1].status = 1;
	}
	else {
		ts->pendown = 0;
		tc->point_data[0].status = 0;
		tc->point_data[1].status = 0;
		//return 1;
	}
	//data = i2c_master_reg8_recv(ts->client, DATA_START, buf, DATA_LEN, 100*1000);
	//if(data < 0){
		//printk("-->%s i2c io error %d line=%d\n",__FUNCTION__, data,__LINE__);
		//return data;
	//}
	tc->point_data[0].x = buf[DATA_OFF(MALATA_POS_X0_HI+2)] << 8;
	tc->point_data[0].x |= buf[DATA_OFF(MALATA_POS_X0_LO+2)];
	tc->point_data[0].y = buf[DATA_OFF(MALATA_POS_Y0_HI+2) ]<< 8;
	tc->point_data[0].y |= buf[DATA_OFF(MALATA_POS_Y0_LO+2)];
	tc->point_data[1].x = buf[DATA_OFF(MALATA_POS_X1_HI+2) ]<< 8;
	tc->point_data[1].x |= buf[DATA_OFF(MALATA_POS_X1_LO+2)];
	tc->point_data[1].y = buf[DATA_OFF(MALATA_POS_Y1_HI+2) ]<< 8;
	tc->point_data[1].y |= buf[DATA_OFF(MALATA_POS_Y1_LO+2)];
	//printk("sintek tc->point_data[0].x= %d tc->point_data[0].y=%d\n ",tc->point_data[0].x,tc->point_data[0].y);
	//printk("sintek tc->point_data[1].x= %d tc->point_data[1].y=%d\n ",tc->point_data[1].x,tc->point_data[1].y);
//	tc->point_data[0].status = 0;
//	tc->point_data[1].status = 0;
//	if (tc->point_data[0].x ||tc->point_data[0].y)
//		tc->point_data[0].status = 1;
//	if (tc->point_data[1].x ||tc->point_data[1].y)
//		tc->point_data[1].status = 1;
    return 10;
}


static void p1003_work(struct work_struct *work)
{
	struct ts_p1003 *ts =
		container_of(to_delayed_work(work), struct ts_p1003, work);
	struct multitouch_event *tc = &ts->mt_event;
#if 0   
   int data;
    char buf[10];	
	buf[0] = 0x03;
	DBG("Enter:%s %d\n",__FUNCTION__,__LINE__);
	if(ts->delayed_work_tp == 1){
		data = i2c_master_reg8_send(ts->client, MALATA_SPECOP, buf, 1, 100*1000);
		if(data < 0){
			printk( "i2c io error %d line=%d\n", data,__LINE__);
		}
		ts->delayed_work_tp = 0;
		return ;
		}
	#endif
		if( p1003_read_values(ts,tc)<0)
		{
			printk("-->%s p1003_read_values error  line=%d\n",__FUNCTION__,__LINE__);
			goto out ;
		}

#if defined (Singltouch_Mode)
    p1003_report_single_event(ts,tc);
#else
    p1003_report_event(ts,tc);
#endif

out:               
	if (ts->pendown){
		schedule_delayed_work(&ts->work, msecs_to_jiffies(21));
		ts->pendown = 0;
	}
	else{
		enable_irq(ts->irq);
	}

}

static irqreturn_t p1003_irq(int irq, void *handle)
{
	struct ts_p1003 *ts = handle;
	unsigned long flags;
	DBG("Enter:%s %d\n",__FUNCTION__,__LINE__);
	
	spin_lock_irqsave(&ts->lock,flags);
	if (!ts->get_pendown_state || likely(ts->get_pendown_state())) {
		disable_irq_nosync(ts->irq);
		schedule_delayed_work(&ts->work,msecs_to_jiffies(2));
	}
	spin_unlock_irqrestore(&ts->lock,flags);
	return IRQ_HANDLED;
}

static void p1003_free_irq(struct ts_p1003 *ts)
{
	free_irq(ts->irq, ts);
	if (cancel_delayed_work_sync(&ts->work)) {
		/*
		 * Work was pending, therefore we need to enable
		 * IRQ here to balance the disable_irq() done in the
		 * interrupt handler.
		 */
		enable_irq(ts->irq);
	}
}

static void Sintek_work_delay(struct work_struct *work)
{
	struct ts_p1003 *ts = container_of(to_delayed_work(work), struct ts_p1003, work);
	int data;
    char buf[10];	
	buf[0] = 0x03;
	data = i2c_master_reg8_send(ts->client, MALATA_SPECOP, buf, 1, 100*1000);
	if(data < 0)
		printk( "i2c io error %d line=%d\n", data,__LINE__);
	return;
}
struct ts_p1003  *ts_pub;
static ssize_t pc1003_touchdebug_show(struct device *dev,struct device_attribute *attr,char *_buf)
{
	// struct 	ts_p103 *ts  = dev_get_drvdata(dev);
	 int data;
   char buf[10];	
	 buf[0] = 0x03;
	 printk("Enter:%s %d\n",__FUNCTION__,__LINE__);
	 printk("Touchscreen correct!!\n");
	 data = i2c_master_reg8_send(ts_pub->client, MALATA_SPECOP, buf, 1, 100*1000);
		 if(data < 0){
			printk( "i2c io error %d line=%d\n", data,__LINE__);
	 }
	 return sprintf(_buf, "successful\n");
}

static DEVICE_ATTR(touchdebug, 0666, pc1003_touchdebug_show, NULL);

static int __devinit sintek_touch_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct ts_p1003 *ts;
	struct p1003_platform_data *pdata = pdata = client->dev.platform_data;
	struct input_dev *input_dev;
	int err;
		printk(">--------%s\n",__FUNCTION__);
	if (!pdata) {
		dev_err(&client->dev, "platform data is required!\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;

	ts = kzalloc(sizeof(struct ts_p1003), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ts || !input_dev) {
		err = -ENOMEM;
		goto err_free_mem;
	}

	ts->client = client;
	ts->irq = client->irq;
	ts->input = input_dev;
	ts->status =0 ;// fjp add by 2010-9-30
	ts->pendown = 0; // fjp add by 2010-10-06
	ts->delayed_work_tp = 0; ///1;
	//ts->wq = create_rt_workqueue("p1003_wq");
	INIT_DELAYED_WORK(&ts->work, p1003_work);

	ts->model             = pdata->model;

	snprintf(ts->phys, sizeof(ts->phys),
		 "%s/input0", dev_name(&client->dev));

	input_dev->name = "p1003 Touchscreen";
	input_dev->phys = ts->phys;
	input_dev->id.bustype = BUS_I2C;

#if defined (Singltouch_Mode)
	set_bit(EV_SYN, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);
	set_bit(BTN_TOUCH, input_dev->keybit);
	set_bit(BTN_2, input_dev->keybit);
	set_bit(EV_ABS, input_dev->evbit);
	input_set_abs_params(input_dev,ABS_X,0,CONFIG_HANNSTAR_MAX_X,0,0);
	input_set_abs_params(input_dev,ABS_Y,0,CONFIG_HANNSTAR_MAX_Y,0,0);
#else
	ts->has_relative_report = 0;
	input_dev->evbit[0] = BIT_MASK(EV_ABS)|BIT_MASK(EV_KEY)|BIT_MASK(EV_SYN);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_dev->keybit[BIT_WORD(BTN_2)] = BIT_MASK(BTN_2); //jaocbchen for dual
	input_set_abs_params(input_dev, ABS_X, 7, 1020/*CONFIG_HANNSTAR_MAX_X*/, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 10, 586/* CONFIG_HANNSTAR_MAX_Y*/, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_TOOL_WIDTH, 0, 15, 0, 0);
	input_set_abs_params(input_dev, ABS_HAT0X, 7, 1020/*CONFIG_HANNSTAR_MAX_X*/, 0, 0);
	input_set_abs_params(input_dev, ABS_HAT0Y, 10,586/* CONFIG_HANNSTAR_MAX_Y*/, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X,7, 1020/*CONFIG_HANNSTAR_MAX_X*/, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 10, 586/* CONFIG_HANNSTAR_MAX_Y*/, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, 10, 0, 0);   
#endif

	if (pdata->init_platform_hw)
		pdata->init_platform_hw();

	
#if 0
	err = set_irq_type(ts->irq,IRQ_TYPE_LEVEL_LOW);
	if (err < 0) {
		dev_err(&client->dev, "irq %d busy?\n", ts->irq);
		goto err_free_mem;
	}
	if (err < 0)
		goto err_free_irq;
#endif
	err = input_register_device(input_dev);
	if (err)
		goto err_free_irq;

	i2c_set_clientdata(client, ts);

	p1003_check_firmwork(ts);
	//schedule_delayed_work(&ts->work, msecs_to_jiffies(8 * 1000));	
	if (!ts->irq) {
		dev_dbg(&ts->client->dev, "no IRQ?\n");
		return -ENODEV;
	}else{
		ts->irq = gpio_to_irq(ts->irq);
	}

	err = request_irq(ts->irq, p1003_irq, IRQF_TRIGGER_LOW,
			client->dev.driver->name, ts);
	
	if (err < 0) {
		dev_err(&client->dev, "irq %d busy?\n", ts->irq);
		goto err_free_mem;
	}

	
	if (err < 0)
		goto err_free_irq;
	ts_pub = ts;
	err = device_create_file(&ts->client->dev,&dev_attr_touchdebug);
	if(err)
		printk("%s->%d cannot create status attribute\n",__FUNCTION__,__LINE__);
	return 0;

 err_free_irq:
	p1003_free_irq(ts);
	if (pdata->exit_platform_hw)
		pdata->exit_platform_hw();
 err_free_mem:
	input_free_device(input_dev);
	kfree(ts);
	return err;
}

static int __devexit p1003_remove(struct i2c_client *client)
{
	struct ts_p1003 *ts = i2c_get_clientdata(client);
	struct p1003_platform_data *pdata = client->dev.platform_data;

	p1003_free_irq(ts);

	if (pdata->exit_platform_hw)
		pdata->exit_platform_hw();

	input_unregister_device(ts->input);
	kfree(ts);

	return 0;
}

static struct i2c_device_id sintek_idtable[] = {
	{ "sintek_touch", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, sintek_idtable);

static struct i2c_driver sintek_touch_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "sintek_touch"
	},
	.id_table	= sintek_idtable,
	.probe		= sintek_touch_probe,
	.remove		= __devexit_p(p1003_remove),
};

static void __init sintek_touch_init_async(void *unused, async_cookie_t cookie)
{
	printk("--------> %s <-------------\n",__func__);
	i2c_add_driver(&sintek_touch_driver);
}

static int __init sintek_touch_init(void)
{
	async_schedule(sintek_touch_init_async, NULL);
	return 0;
}

static void __exit sintek_touch_exit(void)
{
	return i2c_del_driver(&sintek_touch_driver);
}
module_init(sintek_touch_init);
module_exit(sintek_touch_exit);
MODULE_LICENSE("GPL");

