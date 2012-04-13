/*
 * wm831x-i2c.c  --  I2C access for Wolfson WM831x PMICs
 *
 * Copyright 2009,2010 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/mfd/core.h>
#include <linux/slab.h>
#include <linux/gpio.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/pdata.h>

static int wm831x_i2c_read_device(struct wm831x *wm831x, unsigned short reg,
				  int bytes, void *dest)
{
#if defined(CONFIG_ARCH_RK30)
	const struct i2c_client *client = wm831x->control_data;
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msgs[2];
	int ret;
	char reg_buf[2];
	const short regs = reg;
	int scl_rate= 100 * 1000;
	short *buf = dest;
	int count = bytes/2;

        reg_buf[0] = (regs & 0xff00) >> 8;
        reg_buf[1] = regs & 0x00ff;

	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;
	msgs[0].len = 2;
	msgs[0].buf = reg_buf;
	msgs[0].scl_rate = scl_rate;

	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = count * 2;
	msgs[1].buf = (char *)buf;
	msgs[1].scl_rate = scl_rate;

	ret = i2c_transfer(adap, msgs, 2);

	return (ret == 2)? count : ret;
#else
	struct i2c_client *i2c = wm831x->control_data;
	int ret;
	u16 r = cpu_to_be16(reg);

	ret = i2c_master_send(i2c, (unsigned char *)&r, 2);
	if (ret < 0)
		return ret;
	if (ret != 2)
		return -EIO;

	ret = i2c_master_recv(i2c, dest, bytes);
	if (ret < 0)
		return ret;
	if (ret != bytes)
		return -EIO;
	return 0;
#endif
}

/* Currently we allocate the write buffer on the stack; this is OK for
 * small writes - if we need to do large writes this will need to be
 * revised.
 */
static int wm831x_i2c_write_device(struct wm831x *wm831x, unsigned short reg,
				   int bytes, void *src)
{
#if defined(CONFIG_ARCH_RK30)
	const struct i2c_client *client = wm831x->control_data;
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msg;
	int ret;
	const short regs = reg;
	const short *buf = src;
	int count = bytes/2;
	int scl_rate = 100 * 1000;
	int i;
	
	char *tx_buf = (char *)kmalloc(2 * (count + 1), GFP_KERNEL);
	if(!tx_buf)
		return -ENOMEM;
        tx_buf[0] = (regs & 0xff00) >> 8;
        tx_buf[1] = regs & 0x00ff;
        for(i = 0; i < count; i++){
                tx_buf[i*2+3] = (buf[i] & 0xff00) >> 8;
                tx_buf[i*2+2] = buf[i] & 0x00ff;
        }

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = 2 * (count + 1);
	msg.buf = (char *)tx_buf;
	msg.scl_rate = scl_rate;

	ret = i2c_transfer(adap, &msg, 1);
	kfree(tx_buf);
	return (ret == 1) ? 0 : ret;
#else
	struct i2c_client *i2c = wm831x->control_data;
	unsigned char msg[bytes + 2];
	int ret;

	reg = cpu_to_be16(reg);
	memcpy(&msg[0], &reg, 2);
	memcpy(&msg[2], src, bytes);

	ret = i2c_master_send(i2c, msg, bytes + 2);
	if (ret < 0)
		return ret;
	if (ret < bytes + 2)
		return -EIO;

	return 0;
#endif 
}

static int wm831x_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm831x *wm831x;
	int ret,gpio,irq;

	wm831x = kzalloc(sizeof(struct wm831x), GFP_KERNEL);
	if (wm831x == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, wm831x);
	
	gpio = i2c->irq;
	ret = gpio_request(gpio, "wm831x");
	if (ret) {
		printk( "failed to request rk gpio irq for wm831x \n");
		return ret;
	}
	gpio_pull_updown(gpio, GPIOPullUp);
	if (ret) {
	    printk("failed to pull up gpio irq for wm831x \n");
		return ret;
	}	
	irq = gpio_to_irq(gpio);
	
	wm831x->dev = &i2c->dev;
	wm831x->control_data = i2c;
	wm831x->read_dev = wm831x_i2c_read_device;
	wm831x->write_dev = wm831x_i2c_write_device;

	return wm831x_device_init(wm831x, id->driver_data, irq);
}

static int wm831x_i2c_remove(struct i2c_client *i2c)
{
	struct wm831x *wm831x = i2c_get_clientdata(i2c);

	wm831x_device_exit(wm831x);

	return 0;
}

static int wm831x_i2c_suspend(struct device *dev)
{
	struct wm831x *wm831x = dev_get_drvdata(dev);

	return wm831x_device_suspend(wm831x);
}

static int wm831x_i2c_resume(struct device *dev)
{
	struct wm831x *wm831x = dev_get_drvdata(dev);
	int i;
	//set some intterupt again while resume 
	for (i = 0; i < ARRAY_SIZE(wm831x->irq_masks_cur); i++) {
		//printk("irq_masks_cur[%d]=0x%x\n",i,wm831x->irq_masks_cur[i]);

		if (wm831x->irq_masks_cur[i] != wm831x->irq_masks_cache[i]) {
			wm831x->irq_masks_cache[i] = wm831x->irq_masks_cur[i];
			wm831x_reg_write(wm831x,
					 WM831X_INTERRUPT_STATUS_1_MASK + i,
					 wm831x->irq_masks_cur[i]);
		}
	
	}

	return 0;
}

void wm831x_i2c_shutdown(struct i2c_client *i2c)
{
	struct wm831x *wm831x = i2c_get_clientdata(i2c);
//	printk("%s\n", __FUNCTION__);
//	wm831x_device_shutdown(wm831x);
}

static const struct i2c_device_id wm831x_i2c_id[] = {
	{ "wm8310", WM8310 },
	{ "wm8311", WM8311 },
	{ "wm8312", WM8312 },
	{ "wm8320", WM8320 },
	{ "wm8321", WM8321 },
	{ "wm8325", WM8325 },
	{ "wm8326", WM8326 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm831x_i2c_id);

static const struct dev_pm_ops wm831x_pm_ops = {
	.suspend = wm831x_i2c_suspend,
	.resume = wm831x_i2c_resume,
};

static struct i2c_driver wm831x_i2c_driver = {
	.driver = {
		.name = "wm831x",
		.owner = THIS_MODULE,
		.pm = &wm831x_pm_ops,
	},
	.probe = wm831x_i2c_probe,
	.remove = wm831x_i2c_remove,
	.shutdown = wm831x_i2c_shutdown,
	.id_table = wm831x_i2c_id,
};

static int __init wm831x_i2c_init(void)
{
	int ret;

	printk("%s\n", __FUNCTION__);
	ret = i2c_add_driver(&wm831x_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register wm831x I2C driver: %d\n", ret);

	return ret;
}
subsys_initcall_sync(wm831x_i2c_init);

static void __exit wm831x_i2c_exit(void)
{
	i2c_del_driver(&wm831x_i2c_driver);
}
module_exit(wm831x_i2c_exit);
