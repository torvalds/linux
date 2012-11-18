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


/**
 * ts_reg_read: Read a single ts register.
 *
 * @ts: Device to read from.
 * @reg: Register to read.
 */
int ts_reg_read(struct ts_private_data *ts, unsigned short reg)
{
	unsigned short val;
	int ret;

	mutex_lock(&ts->io_lock);

	ret = ts->read_dev(ts, reg, ts->ops->reg_size, &val, ts->ops->reg_size);

	mutex_unlock(&ts->io_lock);

	if (ret < 0)
		return ret;
	else
		return val;
}
EXPORT_SYMBOL_GPL(ts_reg_read);

/**
 * ts_bulk_read: Read multiple ts registers
 *
 * @ts: Device to read from
 * @reg: First register
 * @count: Number of registers
 * @buf: Buffer to fill.
 */
int ts_bulk_read(struct ts_private_data *ts, unsigned short reg,
		     int count, unsigned char *buf)
{
	int ret;

	mutex_lock(&ts->io_lock);

	ret = ts->read_dev(ts, reg, count, buf, ts->ops->reg_size);

	mutex_unlock(&ts->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(ts_bulk_read);


/**
 * ts_reg_write: Write a single ts register.
 *
 * @ts: Device to write to.
 * @reg: Register to write to.
 * @val: Value to write.
 */
int ts_reg_write(struct ts_private_data *ts, unsigned short reg,
		     unsigned short val)
{
	int ret;

	mutex_lock(&ts->io_lock);

	ret = ts->write_dev(ts, reg, ts->ops->reg_size, &val, ts->ops->reg_size);

	mutex_unlock(&ts->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(ts_reg_write);


int ts_bulk_write(struct ts_private_data *ts, unsigned short reg,
		     int count, unsigned char *buf)
{
	int ret;

	mutex_lock(&ts->io_lock);

	ret = ts->write_dev(ts, reg, count, buf, ts->ops->reg_size);

	mutex_unlock(&ts->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(ts_bulk_write);



/**
 * ts_set_bits: Set the value of a bitfield in a ts register
 *
 * @ts: Device to write to.
 * @reg: Register to write to.
 * @mask: Mask of bits to set.
 * @val: Value to set (unshifted)
 */
int ts_set_bits(struct ts_private_data *ts, unsigned short reg,
		    unsigned short mask, unsigned short val)
{
	int ret;
	u16 r;

	mutex_lock(&ts->io_lock);

	ret = ts->read_dev(ts, reg, ts->ops->reg_size, &r, ts->ops->reg_size);
	if (ret < 0)
		goto out;

	r &= ~mask;
	r |= val;

	ret = ts->write_dev(ts, reg, ts->ops->reg_size, &r, ts->ops->reg_size);

out:
	mutex_unlock(&ts->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(ts_set_bits);

static int ts_get_id(struct ts_operate *ops, struct ts_private_data *ts, int *value)
{	
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
				result = ts->read_dev(ts, ops->id_reg, 2, temp, ops->reg_size);
				*value = (temp[0] << 8) | temp[1];
			}
			else
			{
				result = ts->read_dev(ts, ops->id_reg, 1, temp, ops->reg_size);
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


static int ts_get_version(struct ts_operate *ops, struct ts_private_data *ts)
{
	int result = 0;
	char temp[TS_MAX_VER_LEN + 1] = {0};
	int i = 0;
	
	DBG("%s:start\n",__func__);
	
	if(ops->version_reg >= 0)
	{
		if((ops->version_len < 0) || (ops->version_len > TS_MAX_VER_LEN))
		{
			printk("%s:version_len %d is error\n",__func__,ops->version_len);
			ops->version_len = TS_MAX_VER_LEN;
		}
	
		result = ts->read_dev(ts, ops->version_reg, ops->version_len, temp, ops->reg_size);
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


static int ts_chip_init(struct ts_private_data *ts, int type)
{
	struct ts_operate *ops = NULL;
	int result = 0;
	int i = 0;

	if((type <= TS_BUS_TYPE_INVALID) || (type >= TS_BUS_TYPE_NUM_ID))
	{
		printk("%s:type=%d is error\n",__func__,type);
		return -1;	
	}
	
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
		
		if(ops->bus_type == type)
		{
		
			if(!ops->init || !ops->report)
			{
				printk("%s:error:%p,%p\n",__func__,ops->init,ops->report);
				result = -1;
				continue;
			}
				
			ts->ops = ops;	//save ops

			result = ts_get_id(ops, ts, &ts->devid);//get id
			if(result < 0)
			{	
				printk("%s:fail to read %s devid:0x%x\n",__func__, ops->name, ts->devid);	
				continue;
			}
			
			result = ts_get_version(ops, ts);	//get version
			if(result < 0)
			{	
				printk("%s:fail to read %s version\n",__func__, ops->name);	
				continue;
			}
			
			result = ops->init(ts);
			if(result < 0)
			{
				printk("%s:fail to init ts\n",__func__);	
				continue;
			}
			
			if(ops->firmware)
			{
				result = ops->firmware(ts);
				if(result < 0)
				{
					printk("%s:fail to updata firmware ts\n",__func__);
					return result;
				}
			}
		
			printk("%s:%s devid:0x%x\n",__func__, ts->ops->name, ts->devid);
		}

		break;
					
	}

	
	return result;

}

static int ts_get_data(struct ts_private_data *ts)
{		
	return ts->ops->report(ts);	
}


static void  ts_delaywork_func(struct work_struct *work)
{
	struct delayed_work *delaywork = container_of(work, struct delayed_work, work);
	struct ts_private_data *ts = container_of(delaywork, struct ts_private_data, delaywork);

	mutex_lock(&ts->ts_lock);	
	if (ts_get_data(ts) < 0) 
		DBG(KERN_ERR "%s: Get data failed\n",__func__);
	
	if(!ts->ops->irq_enable)//restart work while polling
	schedule_delayed_work(&ts->delaywork, msecs_to_jiffies(ts->ops->poll_delay_ms));
	else
	{
		if(ts->ops->check_irq)
		{
			ts->ops->check_irq(ts);		
		}
		else
		{
			if((ts->ops->trig & IRQF_TRIGGER_LOW) || (ts->ops->trig & IRQF_TRIGGER_HIGH))
			enable_irq(ts->irq);
		}
	}
	mutex_unlock(&ts->ts_lock);
	
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


static int ts_irq_init(struct ts_private_data *ts)
{
	int result = 0;
	int irq;
	if((ts->ops->irq_enable)&&(ts->ops->trig != TS_UNKNOW_DATA))
	{
		INIT_DELAYED_WORK(&ts->delaywork, ts_delaywork_func);
		if(ts->ops->poll_delay_ms < 0)
			ts->ops->poll_delay_ms = 30;
		
		//result = gpio_request(ts->irq, ts->i2c_id->name);
		//if (result)
		//{
		//	printk("%s:fail to request gpio :%d\n",__func__,ts->irq);
		//}
	
		gpio_pull_updown(ts->irq, PullEnable);
		irq = gpio_to_irq(ts->irq);
		result = request_irq(irq, ts_interrupt, ts->ops->trig, ts->ops->name, ts);
		//result = request_threaded_irq(irq, NULL, ts_interrupt, ts->ops->trig, ts->ops->name, ts);
		if (result) {
			printk(KERN_ERR "%s:fail to request irq = %d, ret = 0x%x\n",__func__, irq, result);	       
			goto error;	       
		}
		ts->irq = irq;
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



int ts_device_init(struct ts_private_data *ts, int type, int irq)
{
	struct ts_platform_data *pdata = ts->dev->platform_data;
	int result = -1, i;

	mutex_init(&ts->io_lock);
	mutex_init(&ts->ts_lock);
	dev_set_drvdata(ts->dev, ts);
	
	ts->pdata = pdata;
	result = ts_chip_init(ts, type);
	if(result < 0)
	{
		printk("%s:touch screen with bus type %d is not exist\n",__func__,type);
		goto out_free_memory;
	}

	ts->input_dev = input_allocate_device();
	if (!ts->input_dev) {
		result = -ENOMEM;
		dev_err(ts->dev,
			"Failed to allocate input device %s\n", ts->input_dev->name);
		goto out_free_memory;
	}	
	
	ts->input_dev->dev.parent = ts->dev;
	ts->input_dev->name = ts->ops->name;
	
	result = input_register_device(ts->input_dev);
	if (result) {
		dev_err(ts->dev,
			"Unable to register input device %s\n", ts->input_dev->name);
		goto out_input_register_device_failed;
	}
	
	result = ts_irq_init(ts);
	if (result) {
		dev_err(ts->dev,
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

	if((ts->ops->range[0] <= 0) || (ts->ops->range[1] <= 0))
	{
		ts->ops->range[0] = 1024;
		ts->ops->range[1] = 600;
	}
	
	input_set_abs_params(ts->input_dev,ABS_MT_POSITION_X, 0, ts->ops->range[0], 0, 0);
	input_set_abs_params(ts->input_dev,ABS_MT_POSITION_Y, 0, ts->ops->range[1], 0, 0);
	input_set_abs_params(ts->input_dev,ABS_MT_TOUCH_MAJOR, 0, 10, 0, 0);
	input_set_abs_params(ts->input_dev,ABS_MT_WIDTH_MAJOR, 0, 10, 0, 0);
	
	g_ts = ts;
	
	printk("%s:initialized ok,ts name:%s,devid=%d\n\n",__func__,ts->ops->name,ts->devid);

	return result;
	
out_misc_device_register_device_failed:
	input_unregister_device(ts->input_dev);	
out_input_register_device_failed:
	input_free_device(ts->input_dev);	
out_free_memory:	
	kfree(ts);
	
	printk("%s:line=%d\n",__func__,__LINE__);
	return result;
}


void ts_device_exit(struct ts_private_data *ts)
{
	if(!ts->ops->irq_enable)	
	cancel_delayed_work_sync(&ts->delaywork);
	input_unregister_device(ts->input_dev);	
	input_free_device(ts->input_dev);	
#ifdef CONFIG_HAS_EARLYSUSPEND
	if((ts->ops->suspend) && (ts->ops->resume))
		unregister_early_suspend(&ts->early_suspend);
#endif  
	kfree(ts);	
}


int ts_device_suspend(struct ts_private_data *ts)
{
	if(ts->ops->suspend)
		ts->ops->suspend(ts);

	if(ts->ops->irq_enable)	
		disable_irq_nosync(ts->irq);
	else
		cancel_delayed_work_sync(&ts->delaywork);

	return 0;
}


int ts_device_resume(struct ts_private_data *ts)
{
	if(ts->ops->resume)
		ts->ops->resume(ts);

	if(ts->ops->irq_enable)	
		enable_irq(ts->irq);
	else
	{
		PREPARE_DELAYED_WORK(&ts->delaywork, ts_delaywork_func);
		schedule_delayed_work(&ts->delaywork, msecs_to_jiffies(ts->ops->poll_delay_ms));
	}
	
	return 0;
}



int ts_register_slave(struct ts_private_data *ts,
			struct ts_platform_data *slave_pdata,
			struct ts_operate *(*get_ts_ops)(void))
{
	int result = 0;
	struct ts_operate *ops = get_ts_ops();
	if((ops->ts_id >= TS_NUM_ID) || (ops->ts_id <= TS_ID_INVALID))
	{	
		printk("%s:%s id is error %d\n", __func__, ops->name, ops->ts_id);
		return -1;	
	}
	g_ts_ops[ops->ts_id] = ops;
	printk("%s:%s,id=%d\n",__func__,g_ts_ops[ops->ts_id]->name, ops->ts_id);
	return result;
}


int ts_unregister_slave(struct ts_private_data *ts,
			struct ts_platform_data *slave_pdata,
			struct ts_operate *(*get_ts_ops)(void))
{
	int result = 0;
	struct ts_operate *ops = get_ts_ops();
	if((ops->ts_id >= TS_NUM_ID) || (ops->ts_id <= TS_ID_INVALID))
	{	
		printk("%s:%s id is error %d\n", __func__, ops->name, ops->ts_id);
		return -1;	
	}
	printk("%s:%s,id=%d\n",__func__,g_ts_ops[ops->ts_id]->name, ops->ts_id);
	g_ts_ops[ops->ts_id] = NULL;	
	return result;
}


MODULE_AUTHOR("ROCKCHIP Corporation:lw@rock-chips.com");
MODULE_DESCRIPTION("device interface for auto touch screen");
MODULE_LICENSE("GPL");


