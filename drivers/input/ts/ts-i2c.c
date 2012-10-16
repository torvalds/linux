/* drivers/input/ts/ts-i2c.c - touchscreen i2c handle
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
#include <mach/gpio.h>
#include <mach/board.h> 
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/ts-auto.h>

#define TS_I2C_RATE 200*1000

#if 0
#define TS_DEBUG_ENABLE
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif


static int ts_i2c_read_device(struct ts_private_data *ts, unsigned short reg,
				  int bytes, void *dest, int reg_size)
{
	const struct i2c_client *client = ts->control_data;
	struct i2c_adapter *i2c_adap = client->adapter;
	struct i2c_msg msgs[2];
	int i,res;
	
	if (!dest || !i2c_adap) {
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return -EINVAL;
	}

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;	/* write */
	msgs[0].buf = (unsigned char *)&reg;
	if(reg_size == 2)		
	msgs[0].len = 2;
	else	
	msgs[0].len = 1;
	msgs[0].scl_rate = TS_I2C_RATE;
	
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].buf = dest;
	msgs[1].len = bytes;
	msgs[1].scl_rate = TS_I2C_RATE; 

	res = i2c_transfer(i2c_adap, msgs, 2);
	if (res == 2)
		return 0;
	else if(res == 0)
		return -EBUSY;
	else
		return res;
	
#ifdef TS_DEBUG_ENABLE
	DBG("%s:reg=0x%x,len=%d,rxdata:",__func__, reg, bytes);
	for(i=0; i<bytes; i++)
		DBG("0x%x,",(unsigned char *)dest[i]);
	DBG("\n");
#endif	

	
}

/* Currently we allocate the write buffer on the stack; this is OK for
 * small writes - if we need to do large writes this will need to be
 * revised.
 */
static int ts_i2c_write_device(struct ts_private_data *ts, unsigned short reg,
				   int bytes, void *src, int reg_size)
{
	const struct i2c_client *client = ts->control_data;
	struct i2c_adapter *i2c_adap = client->adapter;
	struct i2c_msg msgs[1];
	int res;
	unsigned char buf[bytes + 2];
	
	if (!src || !i2c_adap) {
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return -EINVAL;
	}
	
	if(ts->ops->reg_size == 2)
	{
		buf[0] = (reg & 0xff00) >> 8;
		buf[1] = (reg & 0x00ff) & 0xff;
		memcpy(&buf[2], src, bytes);
	}
	else
	{
		buf[0] = reg & 0xff;
		memcpy(&buf[1], src, bytes);
	}

#ifdef TS_DEBUG_ENABLE
	int i = 0;
	DBG("%s:reg=0x%x,len=%d,txdata:",__func__, reg, bytes);
	for(i=0; i<length; i++)
		DBG("0x%x,",buf[i]);
	DBG("\n");
#endif	

	if (!src || !i2c_adap) {
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return -EINVAL;
	}

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;	/* write */
	msgs[0].buf = buf;
	msgs[0].len = bytes;
	msgs[0].scl_rate = TS_I2C_RATE;

	res = i2c_transfer(i2c_adap, msgs, 1);
	if (res == 1)
		return 0;
	else if(res == 0)
		return -EBUSY;
	else
		return res;
			
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ts_suspend(struct early_suspend *h)
{
	struct ts_private_data *ts = 
			container_of(h, struct ts_private_data, early_suspend);
	
	return ts_device_suspend(ts);
}

static void ts_resume(struct early_suspend *h)
{
	struct ts_private_data *ts = 
			container_of(h, struct ts_private_data, early_suspend);

	return ts_device_resume(ts);
}
#endif

static int ts_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct ts_private_data *ts;
	int ret,gpio,irq;
	int type = TS_BUS_TYPE_I2C;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		dev_err(&i2c->adapter->dev, "%s failed\n", __func__);
		return -ENODEV;
	}
	
	ts = kzalloc(sizeof(struct ts_private_data), GFP_KERNEL);
	if (ts == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, ts);
	
	ts->irq = i2c->irq;
	ts->dev = &i2c->dev;
	ts->control_data = i2c;
	ts->read_dev = ts_i2c_read_device;
	ts->write_dev = ts_i2c_write_device;

	ret = ts_device_init(ts, type, ts->irq);
	if(ret)
	{
		printk("%s:fail to regist touch, type is %d\n",__func__, type);
		return -1;
	}
	
#ifdef CONFIG_HAS_EARLYSUSPEND
	if((ts->ops->suspend) && (ts->ops->resume))
	{
		ts->early_suspend.suspend = ts_suspend;
		ts->early_suspend.resume = ts_resume;
		ts->early_suspend.level = 0x02;
		register_early_suspend(&ts->early_suspend);
	}
#endif

	return 0;
}

static int ts_i2c_remove(struct i2c_client *i2c)
{
	struct ts_private_data *ts = i2c_get_clientdata(i2c);

	ts_device_exit(ts);

	return 0;
}


static const struct i2c_device_id ts_i2c_id[] = {
	{"auto_ts_i2c", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, ts_i2c_id);

static struct i2c_driver ts_i2c_driver = {
	.driver = {
		.name = "auto_ts_i2c",
		.owner = THIS_MODULE,
	},
	.probe = ts_i2c_probe,
	.remove = ts_i2c_remove,
	.id_table = ts_i2c_id,
};

static int __init ts_i2c_init(void)
{
	int ret;

	printk("%s\n", __FUNCTION__);
	ret = i2c_add_driver(&ts_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register ts I2C driver: %d\n", ret);

	return ret;
}
subsys_initcall_sync(ts_i2c_init);

static void __exit ts_i2c_exit(void)
{
	i2c_del_driver(&ts_i2c_driver);
}
module_exit(ts_i2c_exit);

