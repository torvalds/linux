/*
 * Driver for Pixcir I2C touchscreen controllers.
 *
 * Copyright (C) 2010-2011 Pixcir, Inc.
 *
 * pixcir_i2c_ts.c V3.0  from v3.0 support TangoC solution and remove the previous soltutions
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
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <mach/iomux.h>
#include <linux/platform_device.h>

#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>

#include "pixcir_i2c_ts.h"

#define PIXCIR_DEBUG			0
#if PIXCIR_DEBUG
	#define pixcir_dbg(msg...)	printk(msg);
#else
	#define pixcir_dbg(msg...)
#endif

static int  ts_dbg_enable = 0;

#define DBG(msg...) \
	({if(ts_dbg_enable == 1) printk(msg);})
/*********************************Bee-0928-TOP****************************************/

#define SLAVE_ADDR		0x5c

#ifndef I2C_MAJOR
#define I2C_MAJOR 		125
#endif

#define I2C_MINORS 		256

#define  CALIBRATION_FLAG	1

static unsigned char status_reg = 0;
static struct workqueue_struct *pixcir_wq;
static struct pixcir_i2c_ts_data *this_data;

struct point_data{
	unsigned char	brn;  //broken line number
	unsigned char	brn_pre;
	unsigned char	id;    //finger ID
	int	posx;
	int	posy;
};

static struct point_data point[MAX_SUPPORT_POINT];


struct i2c_dev
{
	struct list_head list;
	struct i2c_adapter *adap;
	struct device *dev;
};

static struct i2c_driver pixcir_i2c_ts_driver;
static struct class *i2c_dev_class;
static LIST_HEAD( i2c_dev_list);
static DEFINE_SPINLOCK( i2c_dev_list_lock);

static void return_i2c_dev(struct i2c_dev *i2c_dev)
{
	spin_lock(&i2c_dev_list_lock);
	list_del(&i2c_dev->list);
	spin_unlock(&i2c_dev_list_lock);
	kfree(i2c_dev);
}

static struct i2c_dev *i2c_dev_get_by_minor(unsigned index)
{
	struct i2c_dev *i2c_dev;
	i2c_dev = NULL;

	spin_lock(&i2c_dev_list_lock);
	list_for_each_entry(i2c_dev, &i2c_dev_list, list)
	{
		if (i2c_dev->adap->nr == index)
			goto found;
	}
	i2c_dev = NULL;
	found: spin_unlock(&i2c_dev_list_lock);
	return i2c_dev;
}

static struct i2c_dev *get_free_i2c_dev(struct i2c_adapter *adap)
{
	struct i2c_dev *i2c_dev;

	if (adap->nr >= I2C_MINORS) {
		printk(KERN_ERR "i2c-dev: Out of device minors (%d)\n",
				adap->nr);
		return ERR_PTR(-ENODEV);
	}

	i2c_dev = kzalloc(sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return ERR_PTR(-ENOMEM);

	i2c_dev->adap = adap;

	spin_lock(&i2c_dev_list_lock);
	list_add_tail(&i2c_dev->list, &i2c_dev_list);
	spin_unlock(&i2c_dev_list_lock);
	return i2c_dev;
}
/*********************************Bee-0928-bottom**************************************/

struct pixcir_i2c_ts_data {
	struct i2c_client *client;
	struct input_dev *input,*input_key_dev;
	int use_irq;
	int 	gpio_pendown;
	int 	gpio_reset;
	int 	gpio_reset_active_low;
	int		pendown_iomux_mode;	
	int		resetpin_iomux_mode;
	char	pendown_iomux_name[IOMUX_NAME_SIZE];	
	char	resetpin_iomux_name[IOMUX_NAME_SIZE];		
	struct 	work_struct  work;
	//const struct pixcir_ts_platform_data *chip;
	bool exiting;
    struct 	early_suspend early_suspend;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void pixcir_ts_early_suspend(struct early_suspend *h);
static void pixcir_ts_late_resume(struct early_suspend *h);
#endif

int tp_pixcir_write_reg(struct i2c_client *client,const char *buf ,int count)
{
  struct i2c_msg msg[] = {
    {
      .addr  = client->addr,
      .flags = 0,
      .len   = count,
      .buf   = (char *)buf,
    }
  };
  
	//ret = i2c_transfer(adap, &msg, 1);
	if (i2c_transfer(client->adapter, msg, 1) < 0)
	{
		printk("write the address (0x%x) of the ssd2533 fail.",buf[0]);
		return -1;
	}
	return 0;
}

int tp_pixcir_read_reg(struct i2c_client *client,u8 addr,u8 *buf,u8 len)
{
	u8 msgbuf[1] = { addr };
	struct i2c_msg msgs[] = {
		{
			.addr	 = client->addr,
			.flags = 0, //Write
			.len	 = 1,
			.buf	 = msgbuf,
		},
		{
			.addr	 = client->addr,
			.flags = I2C_M_RD,
			.len	 = len,
			.buf	 = buf,
		},
	};
	if (i2c_transfer(client->adapter, msgs, 2) < 0)
	{
		printk("read the address (0x%x) of the ssd2533 fail.",addr);
		return -1;
	}
	return 0;
}

static void pixcir_ts_poscheck(struct pixcir_i2c_ts_data *data)
{
	struct pixcir_i2c_ts_data *tsdata = data;
	
	u8 *p;
	u8 touch, button;
	u8 rdbuf[27], wrbuf[1] = { 0 };
	int ret, i;
	int ignore_cnt = 0;
	
	ret = i2c_master_send(tsdata->client, wrbuf, sizeof(wrbuf));
	if (ret != sizeof(wrbuf)) {
		dev_err(&tsdata->client->dev,
			"%s: i2c_master_send failed(), ret=%d\n",
			__func__, ret);
		return;
	}

	ret = i2c_master_recv(tsdata->client, rdbuf, sizeof(rdbuf));
	if (ret != sizeof(rdbuf)) {
		dev_err(&tsdata->client->dev,
			"%s: i2c_master_recv failed(), ret=%d\n",
			__func__, ret);
		return;
	}
	
	touch = rdbuf[0] & 0x07;
	button = rdbuf[1];
	p = &rdbuf[2];
	for (i = 0; i < touch; i++)	{
		point[i].brn = (*(p + 4)) >> 3;		
		point[i].id = (*(p + 4)) & 0x7;	
		point[i].posx = (*(p + 1) << 8) + (*(p));	
		point[i].posy = (*(p + 3) << 8) + (*(p + 2));
		p+=5;
	}

	if (touch) {
		for(i=0; i < touch; i++) {
			if (point[i].posy < 40 || point[i].posy > 520 || point[i].posx < 40) {
				ignore_cnt++;  //invalid point
				continue;
			}else {
				point[i].posy -= 40;
				point[i].posx -= 40;

				if(point[i].posy < 0)
					point[i].posy=1;

				if(point[i].posx < 0)
					point[i].posx=1;

				input_mt_slot(tsdata->input, 0);
				input_mt_report_slot_state(tsdata->input, MT_TOOL_FINGER, true);
				input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 1);
				input_report_abs(tsdata->input, ABS_MT_POSITION_X, point[i].posy);
				input_report_abs(tsdata->input, ABS_MT_POSITION_Y, point[i].posx);

				input_sync(tsdata->input);

				DBG("brn%d=%2d id%d=%1d x=%5d y=%5d \n",
					i,point[i].brn,i,point[i].id,point[i].posy,point[i].posx);
			}			
		}	       

		if (touch == ignore_cnt)
			return; //if all touchpoint are invalid, return

		input_sync(tsdata->input);
	} 
}

static void pixcir_ts_work_func(struct work_struct *work)
{
	struct pixcir_i2c_ts_data *tsdata = this_data;
	//DBG("%s\n",__FUNCTION__);

	while (!tsdata->exiting) {
	
		pixcir_ts_poscheck(tsdata);

		if (attb_read_val()){
			DBG("%s:  >>>>>touch release\n\n",__FUNCTION__);
			enable_irq(tsdata->client->irq);
			//input_report_key(tsdata->input, BTN_TOUCH, 0);
			input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 0);
			input_mt_slot(tsdata->input, 0);
			input_mt_report_slot_state(tsdata->input, MT_TOOL_FINGER, false);
			//input_report_key(tsdata->input, ABS_MT_WIDTH_MAJOR,0);
			input_sync(tsdata->input);
			break;
		}

		msleep(1);
	}

	return;
}

static irqreturn_t pixcir_ts_isr(int irq, void *dev_id)
{
    struct pixcir_i2c_ts_data *ts = dev_id;
    DBG("%s: >>>>>>>>>\n",__FUNCTION__);
	
	if(ts->use_irq){
    	disable_irq_nosync(ts->client->irq);
	}
	queue_work(pixcir_wq, &ts->work);

    return IRQ_HANDLED;
}

#if 0
#ifdef CONFIG_PM_SLEEP
static int pixcir_i2c_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	pixcir_dbg("%s\n",__FUNCTION__);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);

	return 0;
}

static int pixcir_i2c_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	pixcir_dbg("%s\n",__FUNCTION__);

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);

	return 0;
}
#endif
static SIMPLE_DEV_PM_OPS(pixcir_dev_pm_ops,
			 pixcir_i2c_ts_suspend, pixcir_i2c_ts_resume);

static int __devinit setup_resetPin(struct i2c_client *client, struct pixcir_i2c_ts_data *ts)
{
	struct pixcir_platform_data	*pdata = client->dev.platform_data;
	int err;
	
	pixcir_dbg("%s\n",__FUNCTION__);

	
	ts->gpio_reset = pdata->gpio_reset;
    ts->gpio_reset_active_low = pdata->gpio_reset_active_low;
    ts->resetpin_iomux_mode = pdata->resetpin_iomux_mode;
	///*

    if(pdata->resetpin_iomux_name != NULL)
	    strcpy(ts->resetpin_iomux_name,pdata->resetpin_iomux_name);
	
	//pixcir_dbg("%s=%d,%s,%d,%d\n",__FUNCTION__,ts->gpio_reset,ts->resetpin_iomux_name,ts->resetpin_iomux_mode,ts->gpio_reset_active_low);
	if (!gpio_is_valid(ts->gpio_reset)) {
		dev_err(&client->dev, "no gpio_reset?\n");
		return -EINVAL;
	}

    rk29_mux_api_set(ts->resetpin_iomux_name,ts->resetpin_iomux_mode); 
	//*/

	err = gpio_request(ts->gpio_reset, "pixcir_resetPin");
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

static int __devinit setup_pendown(struct i2c_client *client, struct pixcir_i2c_ts_data *ts)
{
	int err;
	struct pixcir_i2c_ts_data	*pdata = client->dev.platform_data;

	pixcir_dbg("%s\n",__FUNCTION__);
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
	
	pixcir_dbg("%s=%d,%s,%d\n",__FUNCTION__,ts->gpio_pendown,ts->pendown_iomux_name,ts->pendown_iomux_mode);
	
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
#endif

static ssize_t pixcir_proc_write(struct file *file, const char __user *buffer,
			   size_t count, loff_t *data)
{
	char c;
	int rc;
	
	rc = get_user(c, buffer);
	if (rc)
		return rc; 
	
	if (c == '1')
		ts_dbg_enable = 1; 
	else if (c == '0')
		ts_dbg_enable = 0; 

	return count; 
}

static const struct file_operations pixcir_proc_fops = {
	.owner		= THIS_MODULE, 
	.write		= pixcir_proc_write,
}; 
static int __devinit pixcir_i2c_ts_probe(struct i2c_client *client,
					 const struct i2c_device_id *id)
{
	//const struct pixcir_ts_platform_data *pdata = client->dev.platform_data;
	struct pixcir_i2c_ts_data *tsdata;
	struct pixcir_platform_data *pdata;
	struct device *dev;
	struct i2c_dev *i2c_dev;
	int error = 0;
	struct proc_dir_entry *pixcir_proc_entry;	

	//if (!pdata) {
	//	dev_err(&client->dev, "platform data not defined\n");
	//	return -EINVAL;
	//}
	pixcir_dbg("%s\n",__FUNCTION__);

	tsdata = kzalloc(sizeof(*tsdata), GFP_KERNEL);
	tsdata->input = input_allocate_device();
	if (!tsdata || !(tsdata->input)) {
		dev_err(&client->dev, "Failed to allocate driver data!\n");
		error = -ENOMEM;
		goto err_free_mem;
	}
	
	pixcir_wq = create_singlethread_workqueue("pixcir_tp_wq");
	if (!pixcir_wq) {
		printk(KERN_ERR"%s: create workqueue failed\n", __FUNCTION__);
		error = -ENOMEM;
		goto err_free_mem;
	}
	INIT_WORK(&tsdata->work, pixcir_ts_work_func);
	
	this_data = tsdata;
	tsdata->exiting = false;
	//tsdata->input = input;
	//tsdata->chip = pdata;
	
	tsdata->client = client;
	i2c_set_clientdata(client, tsdata);
	pdata = client->dev.platform_data;

	//error = setup_resetPin(client,tsdata);
	if(error)
	{
		 printk("%s:setup_resetPin fail\n",__FUNCTION__);
		 goto err_free_mem;
	}
	tsdata->input->phys = "/dev/input/event2";
	tsdata->input->name = "pixcir_ts-touchscreen";//client->name;
	tsdata->input->id.bustype = BUS_I2C;
	tsdata->input->dev.parent = &client->dev;
	
	
	///*
	/*set_bit(EV_SYN,    tsdata->input->evbit);
	set_bit(EV_KEY,    tsdata->input->evbit);
	set_bit(EV_ABS,    tsdata->input->evbit);
	set_bit(BTN_TOUCH, tsdata->input->keybit);
	set_bit(BTN_2,     tsdata->input->keybit);//*/
	
	//tsdata->input->evbit[0] = BIT_MASK(EV_SYN) |  BIT_MASK(EV_ABS) ;
	//tsdata->input->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	/*tsdata->input->absbit[0] = BIT_MASK(ABS_MT_POSITION_X) | BIT_MASK(ABS_MT_POSITION_Y) |
			BIT_MASK(ABS_MT_TOUCH_MAJOR) | BIT_MASK(ABS_MT_WIDTH_MAJOR);  // for android*/
	//tsdata->input->keybit[BIT_WORD(BTN_START)] = BIT_MASK(BTN_START);

	//input_set_abs_params(input, ABS_X, 0, X_MAX, 0, 0);
	//input_set_abs_params(input, ABS_Y, 0, Y_MAX, 0, 0);

	__set_bit(INPUT_PROP_DIRECT, tsdata->input->propbit);
	__set_bit(EV_ABS, tsdata->input->evbit);

	input_mt_init_slots(tsdata->input, MAX_SUPPORT_POINT);
	input_set_abs_params(tsdata->input, ABS_MT_POSITION_X,  pdata->x_min,  pdata->x_max, 0, 0);
	input_set_abs_params(tsdata->input, ABS_MT_POSITION_Y,  pdata->y_min, pdata->y_max, 0, 0);
	//input_set_abs_params(tsdata->input, ABS_MT_WIDTH_MAJOR, 0,   16, 0, 0);
	//input_set_abs_params(tsdata->input, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(tsdata->input, ABS_MT_TOUCH_MAJOR, 0,    1, 0, 0);
	//input_set_abs_params(tsdata->input, ABS_MT_TRACKING_ID, 0,    5, 0, 0);
	input_set_drvdata(tsdata->input, tsdata);
	//init int and reset ports
	error = gpio_request(client->irq, "TS_INT");	//Request IO
	if (error){
		dev_err(&client->dev, "Failed to request GPIO:%d, ERRNO:%d\n",(int)client->irq, error);
		goto err_free_mem;
	}
	rk29_mux_api_set(pdata->pendown_iomux_name, pdata->pendown_iomux_mode);
	
	gpio_direction_input(client->irq);
	gpio_set_value(client->irq,GPIO_HIGH);
	gpio_pull_updown(client->irq, 0);

	error = gpio_request(pdata->gpio_reset, "pixcir_resetPin");
	if(error){
		dev_err(&client->dev, "failed to request resetPin GPIO%d\n", pdata->gpio_reset);
		goto err_free_mem;
	}
	rk29_mux_api_set(pdata->resetpin_iomux_name, pdata->resetpin_iomux_mode);
	/*{
		gpio_pull_updown(pdata->gpio_reset, 1);
		gpio_direction_output(pdata->gpio_reset, 0);
		msleep(20);     //delay at least 1ms
		gpio_direction_input(pdata->gpio_reset);
		gpio_pull_updown(pdata->gpio_reset, 0);
		msleep(120);
	}*/
    gpio_pull_updown(pdata->gpio_reset, 1);
    mdelay(20);
    gpio_direction_output(pdata->gpio_reset, 0);
    gpio_set_value(pdata->gpio_reset,GPIO_HIGH);//GPIO_LOW
    mdelay(100);
    gpio_set_value(pdata->gpio_reset,GPIO_LOW);//GPIO_HIGH
    mdelay(120);
    // gpio_direction_input(pdata->gpio_reset);
	printk("pdata->gpio_reset = %d\n",gpio_get_value(pdata->gpio_reset));
    //printk("ts->gpio_irq = %d\n",gpio_get_value(pdata->gpio_pendown));
	printk("pdata->gpio_pendown = %d\n",gpio_get_value(client->irq));

#if 0
	//**********************************************
   char buffer[2];
	buffer[0] = 0x3A;
	buffer[1] = 0x03;
	tp_pixcir_write_reg(client,buffer,2);
	ssleep(6);
	//********************************************************//
#endif
	client->irq = gpio_to_irq(client->irq);	
  	error = request_irq(client->irq, pixcir_ts_isr, IRQF_TRIGGER_FALLING, client->name, (void *)tsdata);
	if (error)
		dev_err(&client->dev, "request_irq failed\n");
/*
	error = request_threaded_irq(client->irq, NULL, pixcir_ts_isr,
				     IRQF_TRIGGER_FALLING,
				     client->name, tsdata);*/
	tsdata->use_irq = 1;
	if (error) {
		dev_err(&client->dev, "Unable to request touchscreen IRQ.\n");
		tsdata->use_irq = 0;
		goto err_free_mem;
	}

	error = input_register_device(tsdata->input);
	if (error)
		goto err_free_irq;

	i2c_set_clientdata(client, tsdata);
	device_init_wakeup(&client->dev, 1);

	/*********************************Bee-0928-TOP****************************************/
	i2c_dev = get_free_i2c_dev(client->adapter);
	if (IS_ERR(i2c_dev)) {
		error = PTR_ERR(i2c_dev);
		return error;
	}

	dev = device_create(i2c_dev_class, &client->adapter->dev, MKDEV(I2C_MAJOR,
			client->adapter->nr), NULL, "pixcir_i2c_ts%d", 0);
	if (IS_ERR(dev)) {
		error = PTR_ERR(dev);
		return error;
	}
	/*********************************Bee-0928-BOTTOM****************************************/
#ifdef CONFIG_HAS_EARLYSUSPEND
    tsdata->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
    tsdata->early_suspend.suspend = pixcir_ts_early_suspend;
    tsdata->early_suspend.resume = pixcir_ts_late_resume;
    register_early_suspend(&tsdata->early_suspend);
#endif
	pixcir_proc_entry = proc_create("driver/pixcir", 0777, NULL, &pixcir_proc_fops); 

	dev_err(&tsdata->client->dev, "insmod successfully!\n");

	return 0;

err_free_irq:
	free_irq(client->irq, tsdata);
err_free_mem:
	input_free_device(tsdata->input);
	kfree(tsdata);
	return error;
}

static int __devexit pixcir_i2c_ts_remove(struct i2c_client *client)
{
	int error;
	struct i2c_dev *i2c_dev;
	struct pixcir_i2c_ts_data *tsdata = i2c_get_clientdata(client);

    unregister_early_suspend(&tsdata->early_suspend);
	device_init_wakeup(&client->dev, 0);

	tsdata->exiting = true;
	mb();
	free_irq(client->irq, tsdata);

	/*********************************Bee-0928-TOP****************************************/
	i2c_dev = get_free_i2c_dev(client->adapter);
	if (IS_ERR(i2c_dev)) {
		error = PTR_ERR(i2c_dev);
		return error;
	}

	return_i2c_dev(i2c_dev);
	device_destroy(i2c_dev_class, MKDEV(I2C_MAJOR, client->adapter->nr));
	/*********************************Bee-0928-BOTTOM****************************************/

	input_unregister_device(tsdata->input);
	kfree(tsdata);

	return 0;
}

/*************************************Bee-0928****************************************/
/*                        	     pixcir_open                                     */
/*************************************Bee-0928****************************************/
static int pixcir_open(struct inode *inode, struct file *file)
{
	int subminor;
	struct i2c_client *client;
	struct i2c_adapter *adapter;
	struct i2c_dev *i2c_dev;
	int ret = 0;
#if PIXCIR_DEBUG
	printk("enter pixcir_open function\n");
#endif
	subminor = iminor(inode);

	//lock_kernel();
	i2c_dev = i2c_dev_get_by_minor(subminor);
	if (!i2c_dev) {
		printk("error i2c_dev\n");
		return -ENODEV;
	}

	adapter = i2c_get_adapter(i2c_dev->adap->nr);
	if (!adapter) {
		return -ENODEV;
	}
	
	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client) {
		i2c_put_adapter(adapter);
		ret = -ENOMEM;
	}

	snprintf(client->name, I2C_NAME_SIZE, "pixcir_i2c_ts%d", adapter->nr);
	client->driver = &pixcir_i2c_ts_driver;
	client->adapter = adapter;
	
	file->private_data = client;

	return 0;
}

/*************************************Bee-0928****************************************/
/*                        	     pixcir_ioctl                                    */
/*************************************Bee-0928****************************************/
static long pixcir_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	//printk("ioctl function\n");
	struct i2c_client *client = (struct i2c_client *) file->private_data;
#if PIXCIR_DEBUG
	printk("cmd = %d,arg = %d\n", cmd, arg);
#endif

	switch (cmd)
	{
	case CALIBRATION_FLAG: //CALIBRATION_FLAG = 1
#if PIXCIR_DEBUG
		printk("CALIBRATION\n");
#endif
		client->addr = SLAVE_ADDR;
		status_reg = 0;
		status_reg = CALIBRATION_FLAG;
		break;

	default:
		break;//return -ENOTTY;
	}
	return 0;
}


/***********************************Bee-0928****************************************/
/*                        	  pixcir_write                                     */
/***********************************Bee-0928****************************************/
static ssize_t pixcir_write(struct file *file,const char __user *buf,size_t count, loff_t *ppos)
{
	struct i2c_client *client;
	char *tmp;
	static int ret=0;
	
	pixcir_dbg("%s\n",__FUNCTION__);

	client = file->private_data;

	//printk("pixcir_write function\n");
	switch(status_reg)
	{
		case CALIBRATION_FLAG: //CALIBRATION_FLAG=1
		tmp = kmalloc(count,GFP_KERNEL);
		if (tmp==NULL)
			return -ENOMEM;

		if (copy_from_user(tmp,buf,count)) { 	
			printk("CALIBRATION_FLAG copy_from_user error\n");
			kfree(tmp);
			return -EFAULT;
		}

		ret = i2c_master_send(client,tmp,count);
		if (ret!=count ) {
			dev_err(&client->dev,
				"%s: i2c_master_recv failed(), ret=%d\n",
				__func__, ret);
		}

		while(!attb_read_val());//waiting to finish the calibration.(pixcir application_note_710_v3 p43)

		kfree(tmp);

		status_reg = 0;
		break;

		default:
		break;
	}
	return ret;
}

/***********************************Bee-0928****************************************/
/*                        	  pixcir_release                                   */
/***********************************Bee-0928****************************************/
static int pixcir_release(struct inode *inode, struct file *file)
{
	struct i2c_client *client = file->private_data;
   #if PIXCIR_DEBUG
	printk("enter pixcir_release funtion\n");
   #endif
	i2c_put_adapter(client->adapter);
	kfree(client);
	file->private_data = NULL;

	return 0;
}

/*********************************Bee-0928-TOP****************************************/
static const struct file_operations pixcir_i2c_ts_fops =
{	.owner		= THIS_MODULE,
	.write		= pixcir_write,
	.open		= pixcir_open,
	.unlocked_ioctl = pixcir_ioctl,
	.release	= pixcir_release,
};
/*********************************Bee-0928-BOTTOM****************************************/

static int pixcir_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
    struct pixcir_i2c_ts_data *ts = i2c_get_clientdata(client);
	DBG("%s\n",__FUNCTION__);

	if (ts->use_irq)
		disable_irq(client->irq);
	//gpio_set_value(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_LOW:GPIO_HIGH);
	return 0;
}

static int pixcir_ts_resume(struct i2c_client *client)
{
    struct pixcir_i2c_ts_data *ts = i2c_get_clientdata(client);
	DBG("%s\n",__FUNCTION__);

	if (ts->use_irq)
		enable_irq(client->irq);
    //gpio_set_value(ts->gpio_reset, ts->gpio_reset_active_low? GPIO_HIGH:GPIO_LOW);
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void pixcir_ts_early_suspend(struct early_suspend *h)
{
    struct pixcir_i2c_ts_data *ts;
	DBG("%s\n",__FUNCTION__);
    ts = container_of(h, struct pixcir_i2c_ts_data, early_suspend);
    pixcir_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void pixcir_ts_late_resume(struct early_suspend *h)
{
    struct pixcir_i2c_ts_data *ts;
	DBG("%s\n",__FUNCTION__);
    ts = container_of(h, struct pixcir_i2c_ts_data, early_suspend);
    pixcir_ts_resume(ts->client);
}
#endif

static const struct i2c_device_id pixcir_i2c_ts_id[] = {
	{ "pixcir_ts", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pixcir_i2c_ts_id);

static struct i2c_driver pixcir_i2c_ts_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "pixcir_ts",
		//.pm	= &pixcir_dev_pm_ops,
	},
#ifndef CONFIG_HAS_EARLYSUSPEND
    .suspend    = pixcir_ts_suspend,
    .resume     = pixcir_ts_resume,
#endif
	.probe		= pixcir_i2c_ts_probe,
	.remove		= __devexit_p(pixcir_i2c_ts_remove),
	.id_table	= pixcir_i2c_ts_id,
};

static int __init pixcir_i2c_ts_init(void)
{
	int ret;

	pixcir_dbg("%s\n",__FUNCTION__);
	
    pixcir_wq = create_singlethread_workqueue("pixcir_wq");
    if (!pixcir_wq)
        return -ENOMEM;

	/*********************************Bee-0928-TOP****************************************/
	ret = register_chrdev(I2C_MAJOR,"pixcir_i2c_ts",&pixcir_i2c_ts_fops);
	if (ret) {
		printk(KERN_ERR "%s:register chrdev failed\n",__FILE__);
		return ret;
	}

	i2c_dev_class = class_create(THIS_MODULE, "pixcir_i2c_dev");
	if (IS_ERR(i2c_dev_class)) {
		ret = PTR_ERR(i2c_dev_class);
		class_destroy(i2c_dev_class);
	}
	/********************************Bee-0928-BOTTOM******************************************/

	//tangoC_init();

	return i2c_add_driver(&pixcir_i2c_ts_driver);
}
module_init(pixcir_i2c_ts_init);

static void __exit pixcir_i2c_ts_exit(void)
{
	i2c_del_driver(&pixcir_i2c_ts_driver);
    if (pixcir_wq)
        destroy_workqueue(pixcir_wq);
	/********************************Bee-0928-TOP******************************************/
	class_destroy(i2c_dev_class);
	unregister_chrdev(I2C_MAJOR,"pixcir_i2c_ts");
	/********************************Bee-0928-BOTTOM******************************************/
}
module_exit(pixcir_i2c_ts_exit);

MODULE_AUTHOR("Jianchun Bian <jcbian@pixcir.com.cn>");
MODULE_DESCRIPTION("Pixcir I2C Touchscreen Driver");
MODULE_LICENSE("GPL");
