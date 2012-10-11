/* drivers/input/ts/ts-auto.c - handle all touchscreen in this file
 *
 * Copyright (C) 2012-2015 ROCKCHIP.
 * Author: luowei <lw@rock-chips.com>
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

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/input/mt.h>
#include <mach/gpio.h>
#include <mach/board.h> 
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <linux/ts-auto.h>


#if 0
#define DBG(x...)  printk(x)
#else
#define DBG(x...)
#endif

struct ts_private_data *g_ts;
static struct class *g_ts_class;
static struct ts_operate *g_ts_ops[TS_NUM_ID]; 

static int ts_get_id(struct ts_operate *ops, struct i2c_client *client, int *value)
{
	struct ts_private_data *ts =
	    (struct ts_private_data *) i2c_get_clientdata(client);	
	int result = 0;
	char temp[4] = {ops->id_reg & 0xff};
	int i = 0;
	
	DBG("%s:start\n",__func__);
	if(ops->id_reg >= 0)
	{
		for(i=0; i<2; i++)
		{
			if(ops->reg_size == 2)
			{
				temp[0] = ops->id_reg >> 8;
				temp[1] = ops->id_reg & 0xff;
				result = ts_rx_data_word(client, &temp, 2);
				*value = (temp[0] << 8) | temp[1];
			}
			else
			{
				result = ts_rx_data(client, &temp, 1);
				*value = temp[0];
			}
			if(!result)
			break;

		}

		if(result)
			return result;
		
		if((ops->id_data != TS_UNKNOW_DATA)&&(ops->id_data != *value)) 
		{
			printk("%s:id=0x%x is not 0x%x\n",__func__,*value, ops->id_data);
			result = -1;
		}
			
		DBG("%s:devid=0x%x\n",__func__,*value);
	}

	return result;
}


static int ts_get_version(struct ts_operate *ops, struct i2c_client *client)
{
	struct ts_private_data *ts =
	    (struct ts_private_data *) i2c_get_clientdata(client);	
	int result = 0;
	char temp[TS_MAX_VER_LEN + 1] = {0};
	int i = 0;
	
	DBG("%s:start\n",__func__);
	
	if(ops->version_reg >= 0)
	{
		if((ops->version_len < 0) || (ops->version_len > TS_MAX_VER_LEN))
		{
			printk("%s:version_len is error\n",__func__,ops->version_len);
			ops->version_len = TS_MAX_VER_LEN;
		}
	
		if(ops->reg_size == 2)
		{
			result = ts_rx_data_word(client, temp, ops->version_len);
		}
		else
		{
			result = ts_rx_data(client, temp, ops->version_len);
		}
	

		if(result)
			return result;
		
		if(ops->version_data)
		{
			for(i=0; i<ops->version_len; i++)
			{
				if(temp[i] == ops->version_data[i])
					continue;
				printk("%s:version %s is not %s\n",__func__,temp, ops->version_data);
				result = -1;
			}
		}
			
		DBG("%s:%s version: %s\n",__func__,ops->name, temp);
	}

	return result;
}


static int ts_chip_init(struct i2c_client *client)
{
	struct ts_private_data *ts =
	    (struct ts_private_data *) i2c_get_clientdata(client);	
	struct ts_operate *ops = NULL;
	int result = 0;
	int i = 0;
	
	if(ts->pdata->init_platform_hw)
		ts->pdata->init_platform_hw();
	
	for(i=TS_ID_INVALID+1; i<TS_NUM_ID; i++)
	{
		ops = g_ts_ops[i];
		if(!ops)
		{
			printk("%s:error:%p\n",__func__,ops);
			result = -1;	
			continue;
		}
		
		if(!ops->init || !ops->report)
		{
			printk("%s:error:%p,%p\n",__func__,ops->init,ops->report);
			result = -1;
			continue;
		}

		client->addr = ops->slave_addr;	//use slave_addr of ops
#if 0		
		if(ops->active)
		{
			result = ops->active(client, TS_ENABLE);
			if(result < 0)
			{
				printk("%s:fail to active ts\n",__func__);
				continue;
			}
		}
#endif
		result = ts_get_id(ops, client, &ts->devid);//get id
		if(result < 0)
		{	
			printk("%s:fail to read %s devid:0x%x\n",__func__, ops->name, ts->devid);	
			continue;
		}

		result = ts_get_version(ops, client);	//get version
		if(result < 0)
		{	
			printk("%s:fail to read %s version\n",__func__, ops->name);	
			continue;
		}
	
		ts->ops = ops;	//save ops

		result = ops->init(client);
		if(result < 0)
		{
			printk("%s:fail to init ts\n",__func__);	
			continue;
		}

		if(ops->firmware)
		{
			result = ops->firmware(client);
			if(result < 0)
			{
				printk("%s:fail to updata firmware ts\n",__func__);
				return result;
			}
		}
	
		printk("%s:%s devid:0x%x\n",__func__, ts->ops->name, ts->devid);

		break;
					
	}

	
	return result;

}


static int ts_get_data(struct i2c_client *client)
{
	struct ts_private_data *ts =
	    (struct ts_private_data *) i2c_get_clientdata(client);	
	int result = 0;
	
	result = ts->ops->report(client);
	if(result)
		goto error;
	
error:		
	return result;
}


static void  ts_delaywork_func(struct work_struct *work)
{
	struct delayed_work *delaywork = container_of(work, struct delayed_work, work);
	struct ts_private_data *ts = container_of(delaywork, struct ts_private_data, delaywork);
	struct i2c_client *client = ts->client;

	mutex_lock(&ts->ts_mutex);	
	if (ts_get_data(client) < 0) 
		DBG(KERN_ERR "%s: Get data failed\n",__func__);
	
	if(!ts->ops->irq_enable)//restart work while polling
	schedule_delayed_work(&ts->delaywork, msecs_to_jiffies(ts->ops->poll_delay_ms));
	else
	{
		if(ts->ops->check_irq)
		{
			ts->ops->check_irq(client);		
		}
		else
		{
			if((ts->ops->trig & IRQF_TRIGGER_LOW) || (ts->ops->trig & IRQF_TRIGGER_HIGH))
			enable_irq(ts->client->irq);
		}
	}
	mutex_unlock(&ts->ts_mutex);
	
	DBG("%s:%s\n",__func__,ts->i2c_id->name);
}

/*
 * This is a threaded IRQ handler so can access I2C/SPI.  Since all
 * interrupts are clear on read the IRQ line will be reasserted and
 * the physical IRQ will be handled again if another interrupt is
 * asserted while we run - in the normal course of events this is a
 * rare occurrence so we save I2C/SPI reads.  We're also assuming that
 * it's rare to get lots of interrupts firing simultaneously so try to
 * minimise I/O.
 */
static irqreturn_t ts_interrupt(int irq, void *dev_id)
{
	struct ts_private_data *ts = (struct ts_private_data *)dev_id;

	//use threaded IRQ
	//if (ts_get_data(ts->client) < 0) 
	//	DBG(KERN_ERR "%s: Get data failed\n",__func__);
	//msleep(ts->ops->poll_delay_ms);
	if(ts->ops->check_irq)
	{
		disable_irq_nosync(irq);
	}
	else
	{
		if((ts->ops->trig & IRQF_TRIGGER_LOW) || (ts->ops->trig & IRQF_TRIGGER_HIGH))
		disable_irq_nosync(irq);
	}
	schedule_delayed_work(&ts->delaywork, msecs_to_jiffies(ts->ops->poll_delay_ms));
	DBG("%s:irq=%d\n",__func__,irq);
	return IRQ_HANDLED;
}


static int ts_irq_init(struct i2c_client *client)
{
	struct ts_private_data *ts =
	    (struct ts_private_data *) i2c_get_clientdata(client);	
	int result = 0;
	int irq;
	if((ts->ops->irq_enable)&&(ts->ops->trig != TS_UNKNOW_DATA))
	{
		INIT_DELAYED_WORK(&ts->delaywork, ts_delaywork_func);
		if(ts->ops->poll_delay_ms < 0)
			ts->ops->poll_delay_ms = 30;
		
		result = gpio_request(client->irq, ts->i2c_id->name);
		if (result)
		{
			printk("%s:fail to request gpio :%d\n",__func__,client->irq);
		}
	
		gpio_pull_updown(client->irq, PullEnable);
		irq = gpio_to_irq(client->irq);
		result = request_irq(irq, ts_interrupt, ts->ops->trig, ts->ops->name, ts);
		//result = request_threaded_irq(irq, NULL, ts_interrupt, ts->ops->trig, ts->ops->name, ts);
		if (result) {
			printk(KERN_ERR "%s:fail to request irq = %d, ret = 0x%x\n",__func__, irq, result);	       
			goto error;	       
		}
		client->irq = irq;
		printk("%s:use irq=%d\n",__func__,irq);
	}
	else if(!ts->ops->irq_enable)
	{		
		INIT_DELAYED_WORK(&ts->delaywork, ts_delaywork_func);
		if(ts->ops->poll_delay_ms < 0)
			ts->ops->poll_delay_ms = 30;
		
		schedule_delayed_work(&ts->delaywork, msecs_to_jiffies(ts->ops->poll_delay_ms));
		printk("%s:use polling,delay=%d ms\n",__func__,ts->ops->poll_delay_ms);
	}

error:	
	return result;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ts_suspend(struct early_suspend *h)
{
	struct ts_private_data *ts = 
			container_of(h, struct ts_private_data, early_suspend);
	
	if(ts->ops->suspend)
		ts->ops->suspend(ts->client);

	if(ts->ops->irq_enable)	
		disable_irq_nosync(ts->client->irq);
	else
		cancel_delayed_work_sync(&ts->delaywork);	

}

static void ts_resume(struct early_suspend *h)
{
	struct ts_private_data *ts = 
			container_of(h, struct ts_private_data, early_suspend);

	if(ts->ops->resume)
		ts->ops->resume(ts->client);

	if(ts->ops->irq_enable)	
		enable_irq(ts->client->irq);
	else
	{
		PREPARE_DELAYED_WORK(&ts->delaywork, ts_delaywork_func);
		schedule_delayed_work(&ts->delaywork, msecs_to_jiffies(ts->ops->poll_delay_ms));
	}
}
#endif



int ts_register_slave(struct i2c_client *client,
			struct ts_platform_data *slave_pdata,
			struct ts_operate *(*get_ts_ops)(void))
{
	int result = 0;
	struct ts_operate *ops = get_ts_ops();
	if((ops->id_i2c >= TS_NUM_ID) || (ops->id_i2c <= TS_ID_INVALID))
	{	
		printk("%s:%s id is error %d\n", __func__, ops->name, ops->id_i2c);
		return -1;	
	}
	g_ts_ops[ops->id_i2c] = ops;
	printk("%s:%s,id=%d\n",__func__,g_ts_ops[ops->id_i2c]->name, ops->id_i2c);
	return result;
}


int ts_unregister_slave(struct i2c_client *client,
			struct ts_platform_data *slave_pdata,
			struct ts_operate *(*get_ts_ops)(void))
{
	int result = 0;
	struct ts_operate *ops = get_ts_ops();
	if((ops->id_i2c >= TS_NUM_ID) || (ops->id_i2c <= TS_ID_INVALID))
	{	
		printk("%s:%s id is error %d\n", __func__, ops->name, ops->id_i2c);
		return -1;	
	}
	printk("%s:%s,id=%d\n",__func__,g_ts_ops[ops->id_i2c]->name, ops->id_i2c);
	g_ts_ops[ops->id_i2c] = NULL;	
	return result;
}


int ts_probe(struct i2c_client *client, const struct i2c_device_id *devid)
{
	struct ts_private_data *ts =
	    (struct ts_private_data *) i2c_get_clientdata(client);
	struct ts_platform_data *pdata;
	int result = 0;
	dev_info(&client->adapter->dev, "%s: %s,0x%x\n", __func__, devid->name,(unsigned int)client);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		result = -ENODEV;
		goto out_no_free;
	}

	pdata = client->dev.platform_data;
	if (!pdata) {
		dev_err(&client->adapter->dev,
			"Missing platform data for slave %s\n", devid->name);
		result = -EFAULT;
		goto out_no_free;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (!ts) {
		result = -ENOMEM;
		goto out_no_free;
	}
	
	i2c_set_clientdata(client, ts);
	ts->client = client;	
	ts->pdata = pdata;	
	ts->i2c_id = (struct i2c_device_id *)devid;

	mutex_init(&ts->data_mutex);	
	mutex_init(&ts->ts_mutex);
	mutex_init(&ts->i2c_mutex);
	
	result = ts_chip_init(ts->client);
	if(result < 0)
		goto out_free_memory;
	
	ts->client->addr = ts->ops->slave_addr;		
	
	ts->input_dev = input_allocate_device();
	if (!ts->input_dev) {
		result = -ENOMEM;
		dev_err(&client->dev,
			"Failed to allocate input device %s\n", ts->input_dev->name);
		goto out_free_memory;
	}	

	ts->input_dev->dev.parent = &client->dev;
	ts->input_dev->name = ts->ops->name;
	
	result = input_register_device(ts->input_dev);
	if (result) {
		dev_err(&client->dev,
			"Unable to register input device %s\n", ts->input_dev->name);
		goto out_input_register_device_failed;
	}
	
	result = ts_irq_init(ts->client);
	if (result) {
		dev_err(&client->dev,
			"fail to init ts irq,ret=%d\n",result);
		goto out_input_register_device_failed;
	}
	
	__set_bit(EV_ABS, ts->input_dev->evbit);
	__set_bit(EV_KEY, ts->input_dev->evbit);
	__set_bit(EV_REP,  ts->input_dev->evbit);
	__set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
	set_bit(ABS_MT_POSITION_X, ts->input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, ts->input_dev->absbit);
	set_bit(ABS_MT_TOUCH_MAJOR, ts->input_dev->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, ts->input_dev->absbit);

	if(ts->ops->max_point <= 0)
		ts->ops->max_point = 1;
	
	input_mt_init_slots(ts->input_dev, ts->ops->max_point);

	if((ts->ops->pixel.max_x <= 0) || (ts->ops->pixel.max_y <= 0))
	{
		ts->ops->pixel.max_x = 1024;
		ts->ops->pixel.max_y = 600;
	}
	
	input_set_abs_params(ts->input_dev,ABS_MT_POSITION_X, 0, ts->ops->range[0], 0, 0);
	input_set_abs_params(ts->input_dev,ABS_MT_POSITION_Y, 0, ts->ops->range[1], 0, 0);
	input_set_abs_params(ts->input_dev,ABS_MT_TOUCH_MAJOR, 0, 10, 0, 0);
	input_set_abs_params(ts->input_dev,ABS_MT_WIDTH_MAJOR, 0, 10, 0, 0);
	
	g_ts = ts;
	
#ifdef CONFIG_HAS_EARLYSUSPEND
	if((ts->ops->suspend) && (ts->ops->resume))
	{
		ts->early_suspend.suspend = ts_suspend;
		ts->early_suspend.resume = ts_resume;
		ts->early_suspend.level = 0x02;
		register_early_suspend(&ts->early_suspend);
	}
#endif

	printk("%s:initialized ok,ts name:%s,devid=%d\n\n",__func__,ts->ops->name,ts->devid);

	return result;
	
out_misc_device_register_device_failed:
	input_unregister_device(ts->input_dev);	
out_input_register_device_failed:
	input_free_device(ts->input_dev);	
out_free_memory:	
	kfree(ts);
out_no_free:
	dev_err(&client->adapter->dev, "%s failed %d\n", __func__, result);
	return result;

}

static void ts_shut_down(struct i2c_client *client)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct ts_private_data *ts =
	    (struct ts_private_data *) i2c_get_clientdata(client);
	if((ts->ops->suspend) && (ts->ops->resume))		
		unregister_early_suspend(&ts->early_suspend);
	DBG("%s:%s\n",__func__,ts->i2c_id->name);
#endif
}

static int ts_remove(struct i2c_client *client)
{
	struct ts_private_data *ts =
	    (struct ts_private_data *) i2c_get_clientdata(client);
	int result = 0;
	
	cancel_delayed_work_sync(&ts->delaywork);
	input_unregister_device(ts->input_dev);	
	input_free_device(ts->input_dev);	
	kfree(ts);
#ifdef CONFIG_HAS_EARLYSUSPEND
	if((ts->ops->suspend) && (ts->ops->resume))
		unregister_early_suspend(&ts->early_suspend);
#endif  
	return result;
}

static const struct i2c_device_id ts_id_table[] = {
	{"auto_ts", 0},
	{},
};


static struct i2c_driver ts_driver = {
	.probe = ts_probe,
	.remove = ts_remove,
	.shutdown = ts_shut_down,
	.id_table = ts_id_table,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "auto_ts",
		   },
};

static int __init ts_init(void)
{
	int res = i2c_add_driver(&ts_driver);
	pr_info("%s: Probe name %s\n", __func__, ts_driver.driver.name);
	if (res)
		pr_err("%s failed\n", __func__);
	return res;
}

static void __exit ts_exit(void)
{
	pr_info("%s\n", __func__);
	i2c_del_driver(&ts_driver);
}

subsys_initcall_sync(ts_init);
module_exit(ts_exit);

MODULE_AUTHOR("ROCKCHIP Corporation:lw@rock-chips.com");
MODULE_DESCRIPTION("User space character device interface for tss");
MODULE_LICENSE("GPL");

