/*
 * drivers/input/touchscreen/gt801_ts.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include <linux/module.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <mach/iomux.h>
#include <linux/platform_device.h>

#include "gt801_ts.h"

#define GT801_DEBUG			0
#if GT801_DEBUG
	#define gt801printk(msg...)	printk(msg);
#else
	#define gt801printk(msg...)
#endif

#define SINGLTOUCH_MODE 0 
#define GT801_REGS_NUM 53

#if SINGLTOUCH_MODE
	#define TOUCH_NUMBER 1
#else
    #define TOUCH_NUMBER 2
#endif

#define TOUCH_REG_NUM 5 //ÿ�������Ҫ�ļĴ�����Ŀ

const unsigned char GT801_RegData[GT801_REGS_NUM]={	
	0x19,0x05,0x06,0x28,0x02,0x14,0x14,0x10,0x40,0xB0,0x01,0xE0,0x03,0x4C,0x78,
	0x9A,0xBC,0xDE,0x65,0x43,0x20,0x11,0x00,0x00,0x00,0x00,0x05,0xCF,0x20,0x0B,
	0x0D,0x8D,0x32,0x3C,0x1E,0x28,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01
};

struct gt801_ts_data {
	u16		model;			/* 801. */	
	bool	swap_xy;		/* swap x and y axes */	
	u16		x_min, x_max;	
	u16		y_min, y_max;
    uint16_t addr;
    int 	use_irq;
	int 	gpio_pendown;
	int 	gpio_reset;
	int 	gpio_reset_active_low;
	int		pendown_iomux_mode;	
	int		resetpin_iomux_mode;
	char	pendown_iomux_name[IOMUX_NAME_SIZE];	
	char	resetpin_iomux_name[IOMUX_NAME_SIZE];	
	char	phys[32];
	char	name[32];
	struct 	i2c_client *client;
    struct 	input_dev *input_dev;
    struct 	hrtimer timer;
    struct 	work_struct  work;
    struct 	early_suspend early_suspend;
};
/*tochscreen private data*/
static int touch_state[TOUCH_NUMBER] = {TOUCH_UP,TOUCH_UP};
static struct workqueue_struct *gt801_wq;

#ifdef CONFIG_HAS_EARLYSUSPEND
static void gt801_ts_early_suspend(struct early_suspend *h);
static void gt801_ts_late_resume(struct early_suspend *h);
#endif

static int verify_coord(struct gt801_ts_data *ts,unsigned short *x,unsigned short *y)
{

	gt801printk("%s:(%d/%d)\n",__FUNCTION__,*x, *y);
	if((*x< ts->x_min) || (*x > ts->x_max))
		return -1;

	if((*y< ts->y_min) || (*y > ts->y_max))
		return -1;
//	*x = ts->x_max - *x;
	//if(*y <780)
	*y = ts->y_max -*y;

	return 0;
}

/*read the gt801 register ,used i2c bus*/
static int gt801_read_regs(struct i2c_client *client, u8 reg, u8 buf[], unsigned len)
{
	int ret;
	ret =i2c_master_reg8_recv(client, reg, buf, len, 200*1000);
	if(ret < 0)
		printk("gt801_ts_work_func:i2c_transfer fail =%d\n",ret);
	return ret;
}
/* set the gt801 registe,used i2c bus*/
static int gt801_write_regs(struct i2c_client *client, u8 reg, u8 const buf[], unsigned short len)
{
	int ret;
	ret = i2c_master_reg8_send(client,reg, buf, len, 200*1000);
 	if (ret < 0) {
	  printk("gt801_ts_work_func:i2c_transfer fail =%d\n",ret);
    }
	return ret;
}
static int gt801_init_panel(struct gt801_ts_data *ts)
{
    return 0;
}

static void gt801_ts_work_func(struct work_struct *work)
{
#if  SINGLTOUCH_MODE
  
#else
	int  touch_state_index = 0;
#endif

	unsigned char start_reg = 0x02;
    unsigned char buf[TOUCH_NUMBER*TOUCH_REG_NUM];
	unsigned short x;
	unsigned short y;
    int i,ret;
	int syn_flag = 0;
	int bufLen = TOUCH_NUMBER*TOUCH_REG_NUM;

    struct gt801_ts_data *ts = container_of(work, struct gt801_ts_data, work);
	
	gt801printk("%s\n",__FUNCTION__);
    
	ret=gt801_read_regs(ts->client, start_reg, buf,bufLen);
	if (ret < 0) {
	  	printk("%s:i2c_transfer fail =%d\n",__FUNCTION__,ret);
		if (ts->use_irq) 
   	  		enable_irq(ts->client->irq);
		
		return;
    }
	       
#if  SINGLTOUCH_MODE 
	i = 0;
	if(buf[i+ptpressure] == 0)
	{
		gt801printk(" realse ts_dev->point.x=%d ,ts_dev->point.y=%d \n",ts->point.x,ts->point.y);

		if (touch_state[i] == TOUCH_DOWN)
		{
			input_report_key(ts->input_dev,BTN_TOUCH,0);
			syn_flag = 1;
			touch_state[i] = TOUCH_UP;
			gt801printk("SINGLTOUCH_MODE up\n");
		}
	}
	else
	{
		x = ((( ((unsigned short)buf[i+ptxh] )<< 8) ) | buf[i+ptxl]);
		y= (((((unsigned short)buf[i+ptyh] )<< 8) )| buf[i+ptyl]);	
		
		if (ts->swap_xy)
			swap(x, y);
		
		if (verify_coord(ts,&x,&y))
			goto out;
		
		if (touch_state[i] == TOUCH_UP)
		{
			gt801printk("SINGLTOUCH_MODE down\n");
			input_report_key(ts->input_dev,BTN_TOUCH,1);
			touch_state[i] = TOUCH_DOWN;
		}
		
		gt801printk("input_report_abs(%d/%d)\n",x,y);
		input_report_abs(ts->input_dev,ABS_X,x );
		input_report_abs(ts->input_dev,ABS_Y,y );
		syn_flag = 1;
	}

#else   

    for(i=0; i<bufLen; i+=TOUCH_REG_NUM)
	{
		if(buf[i+ptpressure] == 0){
			gt801printk("%s:buf=%d touch up\n",__FUNCTION__,buf[i+ptpressure]);
			if (touch_state[touch_state_index] == TOUCH_DOWN)
			{
				gt801printk("%s:%d touch up\n",__FUNCTION__,i);
				input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0); //Finger Size
				input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0); //Touch Size
				input_mt_sync(ts->input_dev);
				syn_flag =1;
				touch_state[touch_state_index] = TOUCH_UP;
			}	  
		}
		else{
			x = ((( ((unsigned short)buf[i+ptxh] )<< 8) ) | buf[i+ptxl]);
			y = (((((unsigned short)buf[i+ptyh] )<< 8) )| buf[i+ptyl]);
			/* adjust the x and y to proper value  added by hhb@rock-chips.com*/
			if(x < 480){
				x = 480-x;
			}

			if(y < 800){
				y = 800-y;
			}

			if (ts->swap_xy){
				swap(x, y);
			}
			
			if (verify_coord(ts,&x,&y));//goto out;
			
			gt801printk("input_report_abs--%d-%d-(%d/%d)\n", i,touch_state_index, x, y);	
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 1); //Finger Size
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 5); //Touch Size
			input_mt_sync(ts->input_dev);
			syn_flag = 1;
			touch_state[touch_state_index] = TOUCH_DOWN;
		}
		
		touch_state_index++;
    }
	
#endif

	if(syn_flag){
		input_sync(ts->input_dev);
	}

out:
   	if (ts->use_irq) {
   		enable_irq(ts->client->irq);
   	}
	return;
}
static enum hrtimer_restart gt801_ts_timer_func(struct hrtimer *timer)
{
    struct gt801_ts_data *ts = container_of(timer, struct gt801_ts_data, timer);
    gt801printk("%s\n",__FUNCTION__); 

    queue_work(gt801_wq, &ts->work);

    hrtimer_start(&ts->timer, ktime_set(0, 12500000), HRTIMER_MODE_REL);
    return HRTIMER_NORESTART;
}

static irqreturn_t gt801_ts_irq_handler(int irq, void *dev_id)
{
    struct gt801_ts_data *ts = dev_id;
    gt801printk("%s=%d,%d\n",__FUNCTION__,ts->client->irq,ts->use_irq);
	
	if(ts->use_irq){
    	disable_irq_nosync(ts->client->irq);
	}
	queue_work(gt801_wq, &ts->work);
    return IRQ_HANDLED;
}
static int __devinit setup_resetPin(struct i2c_client *client, struct gt801_ts_data *ts)
{
	struct gt801_platform_data	*pdata = client->dev.platform_data;
	int err;
	
	ts->gpio_reset = pdata->gpio_reset;
    ts->gpio_reset_active_low = pdata->gpio_reset_active_low;
    ts->resetpin_iomux_mode = pdata->resetpin_iomux_mode;

    if(pdata->resetpin_iomux_name != NULL)
	    strcpy(ts->resetpin_iomux_name,pdata->resetpin_iomux_name);
		 
	gt801printk("%s=%d,%s,%d,%d\n",__FUNCTION__,ts->gpio_reset,ts->resetpin_iomux_name,ts->resetpin_iomux_mode,ts->gpio_reset_active_low);
	if (!gpio_is_valid(ts->gpio_reset)) {
		dev_err(&client->dev, "no gpio_reset?\n");
		return -EINVAL;
	}

    rk29_mux_api_set(ts->resetpin_iomux_name,ts->resetpin_iomux_mode); 

	err = gpio_request(ts->gpio_reset, "gt801_resetPin");
	if (err) {
		dev_err(&client->dev, "failed to request resetPin GPIO%d\n",
				ts->gpio_reset);
		return err;
	}
	
	err = gpio_direction_output(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_LOW:GPIO_HIGH);
	if (err) {
		dev_err(&client->dev, "failed to pulldown resetPin GPIO%d,err%d\n",
				ts->gpio_reset,err);
		gpio_free(ts->gpio_reset);
		return err;
	}
	mdelay(100);
	gpio_set_value(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_HIGH:GPIO_LOW);
	mdelay(100);

	return 0;
}

static int __devinit setup_pendown(struct i2c_client *client, struct gt801_ts_data *ts)
{
	int err;
	struct gt801_platform_data	*pdata = client->dev.platform_data;
	
	if (!client->irq) {
		dev_dbg(&client->dev, "no IRQ?\n");
		return -ENODEV;
	}
	
	if (!gpio_is_valid(pdata->gpio_pendown)) {
		dev_err(&client->dev, "no gpio_pendown?\n");
		return -EINVAL;
	}
	
	ts->gpio_pendown = pdata->gpio_pendown;
	strcpy(ts->pendown_iomux_name,pdata->pendown_iomux_name);
	ts->pendown_iomux_mode = pdata->pendown_iomux_mode;
	
	gt801printk("%s=%d,%s,%d\n",__FUNCTION__,ts->gpio_pendown,ts->pendown_iomux_name,ts->pendown_iomux_mode);
	
	if (!gpio_is_valid(ts->gpio_pendown)) {
		dev_err(&client->dev, "no gpio_pendown?\n");
		return -EINVAL;
	}
	
    rk29_mux_api_set(ts->pendown_iomux_name,ts->pendown_iomux_mode); 
	err = gpio_request(ts->gpio_pendown, "gt801_pendown");
	if (err) {
		dev_err(&client->dev, "failed to request pendown GPIO%d\n",
				ts->gpio_pendown);
		return err;
	}
	
	err = gpio_pull_updown(ts->gpio_pendown, GPIOPullUp);
	if (err) {
		dev_err(&client->dev, "failed to pullup pendown GPIO%d\n",
				ts->gpio_pendown);
		gpio_free(ts->gpio_pendown);
		return err;
	}
	return 0;
}

static int gt801_chip_Init(struct i2c_client *client)
{
	u8 i,j;
	int ret=0;
	u8 start_reg=0x30;
	u8 buf[GT801_REGS_NUM];
	
	gt801printk("enter gt801_chip_Init!!!!\n");

	for(j=0;j<2;j++)
	{
		ret=gt801_write_regs(client,start_reg, GT801_RegData,GT801_REGS_NUM);	
		if(ret<0)
		{
			printk("\n--%s--Set Register values error !!!\n",__FUNCTION__);
		}
		
		ret=gt801_read_regs(client, start_reg, buf,GT801_REGS_NUM);
		if(ret<0)
		{
			printk("\n--%s--Read Register values error !!!\n",__FUNCTION__);
		}
			
	 	for(i=0;i<GT801_REGS_NUM-1;i++)
		{
			if(buf[i]!=GT801_RegData[i])
			{
				printk("!!!!!!!!gt801_chip_Init err may be i2c errorat adress=%x var=%x i=%x\n",0x30+i, buf[i],i);
				break;
			}
		}
		if(i==GT801_REGS_NUM-1)
			break;
		else if(j==1)
			return -1;
		
		mdelay(500);
	}
	mdelay(100);
	
	return ret;
}

static int gt801_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct gt801_ts_data *ts;
	struct gt801_platform_data	*pdata = client->dev.platform_data;
    int ret = 0;

    gt801printk("%s \n",__FUNCTION__);
	
    if (!pdata) {
		dev_err(&client->dev, "empty platform_data\n");
		goto err_check_functionality_failed;
    }
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        printk(KERN_ERR "gt801_ts_probe: need I2C_FUNC_I2C\n");
        ret = -ENODEV;
        goto err_check_functionality_failed;
    }
	
    ts = kzalloc(sizeof(*ts), GFP_KERNEL);
    if (ts == NULL) {
        ret = -ENOMEM;
        goto err_alloc_data_failed;
    }
    INIT_WORK(&ts->work, gt801_ts_work_func);
    ts->client = client;
    i2c_set_clientdata(client, ts);

	ret = setup_resetPin(client,ts);
	if(ret)
	{
		 printk("%s:setup_resetPin fail\n",__FUNCTION__);
		 goto err_input_dev_alloc_failed;
	}
	
	ret=gt801_chip_Init(ts->client);
	if(ret<0)
	{
		printk("%s:chips init failed\n",__FUNCTION__);
		goto err_resetpin_failed;
	}
	
    /* allocate input device */
    ts->input_dev = input_allocate_device();
    if (ts->input_dev == NULL) {
        ret = -ENOMEM;
        printk(KERN_ERR "%s: Failed to allocate input device\n",__FUNCTION__);
        goto err_input_dev_alloc_failed;
    }
	
	ts->model = pdata->model ? : 801;
	ts->swap_xy = pdata->swap_xy;
	ts->x_min = pdata->x_min;
	ts->x_max = pdata->x_max;
	ts->y_min = pdata->y_min;
	ts->y_max = pdata->y_max;
	snprintf(ts->phys, sizeof(ts->phys), "%s/input0", dev_name(&client->dev));
	snprintf(ts->name, sizeof(ts->name), "gt%d-touchscreen", ts->model);
	ts->input_dev->phys = ts->phys;
	ts->input_dev->name = ts->name;
	ts->input_dev->dev.parent = &client->dev;

#if SINGLTOUCH_MODE
	ts->input_dev->evbit[0] = BIT_MASK(EV_ABS)|BIT_MASK(EV_KEY);
	ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(ts->input_dev,ABS_X,
		    ts->x_min ? : 0,
			ts->x_max ? : 480,
			0, 0);
	input_set_abs_params(ts->input_dev,ABS_Y,
			ts->y_min ? : 0,
			ts->y_max ? : 800,
			0, 0);

#else
    ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_ABS);
  //  ts->input_dev->absbit[0] = 
	//	BIT(ABS_MT_POSITION_X) | BIT(ABS_MT_POSITION_Y) | 
	//	BIT(ABS_MT_TOUCH_MAJOR) | BIT(ABS_MT_WIDTH_MAJOR);  // for android
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 
		    ts->x_min ? : 0,
			ts->x_max ? : 480,
			0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y,
			ts->y_min ? : 0,
			ts->y_max ? : 800,
			0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 1, 0, 0); //Finger Size
    input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 10, 0, 0); //Touch Size
#endif
    ret = input_register_device(ts->input_dev);
    if (ret) {
        printk(KERN_ERR "%s: Unable to register %s input device\n", __FUNCTION__,ts->input_dev->name);
        goto err_input_register_device_failed;
    }
	
	client->irq = gpio_to_irq(client->irq);
    if (client->irq) {
		ret = setup_pendown(client,ts);
		if(ret)
		{
			 printk("%s:setup_pendown fail\n",__FUNCTION__);
			 goto err_input_register_device_failed;
		}
		
        ret = request_irq(client->irq, gt801_ts_irq_handler, IRQF_DISABLED | IRQF_TRIGGER_LOW, client->name, ts);
        if (ret == 0) {
            gt801printk("%s:register ISR (irq=%d)\n", __FUNCTION__,client->irq);
            ts->use_irq = 1;
        }
        else 
			dev_err(&client->dev, "request_irq failed\n");
    }

    if (!ts->use_irq) {
        hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        ts->timer.function = gt801_ts_timer_func;
        hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
    }
#ifdef CONFIG_HAS_EARLYSUSPEND
    ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
    ts->early_suspend.suspend = gt801_ts_early_suspend;
    ts->early_suspend.resume = gt801_ts_late_resume;
    register_early_suspend(&ts->early_suspend);
#endif

    printk(KERN_INFO "%s: Start touchscreen %s in %s mode\n", __FUNCTION__,ts->input_dev->name, ts->use_irq ? "interrupt" : "polling");

    return 0;

err_input_register_device_failed:
    input_free_device(ts->input_dev);
err_resetpin_failed:
	gpio_free(ts->gpio_reset);
err_input_dev_alloc_failed:
	kfree(ts);
err_alloc_data_failed:
err_check_functionality_failed:
	
    return ret;
}

static int gt801_ts_remove(struct i2c_client *client)
{
    struct gt801_ts_data *ts = i2c_get_clientdata(client);
    unregister_early_suspend(&ts->early_suspend);
    if (ts->use_irq)
        free_irq(client->irq, ts);
    else
        hrtimer_cancel(&ts->timer);
    input_unregister_device(ts->input_dev);
	gpio_free(ts->gpio_pendown);
	gpio_free(ts->gpio_reset);
    kfree(ts);
    return 0;
}

static int gt801_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
    int ret;
    struct gt801_ts_data *ts = i2c_get_clientdata(client);

    printk("gt801 TS Suspend\n");
    
    if (ts->use_irq)
        disable_irq(client->irq);
    else
        hrtimer_cancel(&ts->timer);

    ret = cancel_work_sync(&ts->work);
    if (ret && ts->use_irq) /* if work was pending disable-count is now 2 */
        enable_irq(client->irq);

	gpio_set_value(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_LOW:GPIO_HIGH);
	
    return 0;
}


static void gt801_ts_resume_work_func(struct work_struct *work)
{
	struct gt801_ts_data *ts = container_of(work, struct gt801_ts_data, work);
	msleep(50);    //touch panel will generate an interrupt when it sleeps out,so as to avoid tihs by delaying 50ms
	enable_irq(ts->client->irq);
	PREPARE_WORK(&ts->work, gt801_ts_work_func);
	printk("enabling gt801_ts IRQ %d\n", ts->client->irq);
}


static int gt801_ts_resume(struct i2c_client *client)
{
    struct gt801_ts_data *ts = i2c_get_clientdata(client);

    gt801_init_panel(ts);
    
    printk("gt801 TS Resume\n");
	
    gpio_set_value(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_HIGH:GPIO_LOW);
	
    if (ts->use_irq) {
        if(!work_pending(&ts->work)){
        	PREPARE_WORK(&ts->work, gt801_ts_resume_work_func);
        	queue_work(gt801_wq, &ts->work);
        }
    }
    else {
        hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
    }

    return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void gt801_ts_early_suspend(struct early_suspend *h)
{
    struct gt801_ts_data *ts;
    ts = container_of(h, struct gt801_ts_data, early_suspend);
    gt801_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void gt801_ts_late_resume(struct early_suspend *h)
{
    struct gt801_ts_data *ts;
    ts = container_of(h, struct gt801_ts_data, early_suspend);
    gt801_ts_resume(ts->client);
}
#endif

#define gt801_TS_NAME "gt801_ts"

static const struct i2c_device_id gt801_ts_id[] = {
    { gt801_TS_NAME, 0 },
    { }
};

static struct i2c_driver gt801_ts_driver = {
    .probe      = gt801_ts_probe,
    .remove     = gt801_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
    .suspend    = gt801_ts_suspend,
    .resume     = gt801_ts_resume,
#endif
    .id_table   = gt801_ts_id,
    .driver = {
        .name   = gt801_TS_NAME,
    },
};

static int __devinit gt801_ts_init(void)
{
    printk("%s\n",__FUNCTION__);
    gt801_wq = create_singlethread_workqueue("gt801_wq");
    if (!gt801_wq)
        return -ENOMEM;
    return i2c_add_driver(&gt801_ts_driver);
}

static void __exit gt801_ts_exit(void)
{
    printk("%s\n",__FUNCTION__);
    i2c_del_driver(&gt801_ts_driver);
    if (gt801_wq)
        destroy_workqueue(gt801_wq);
}

module_init(gt801_ts_init);
module_exit(gt801_ts_exit);

MODULE_DESCRIPTION("gt801 Touchscreen Driver");
MODULE_LICENSE("GPL");
