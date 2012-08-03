/*
 * tps65910.c  --  TI TPS6591x
 *
 * Copyright 2010 Texas Instruments Inc.
 *
 * Author: Graeme Gregory <gg@slimlogic.co.uk>
 * Author: Jorge Eduardo Candelaria <jedu@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tps65910.h>

struct tps65910 *g_tps65910;

static struct mfd_cell tps65910s[] = {
	{
		.name = "tps65910-pmic",
	},
	{
		.name = "tps65910-rtc",
	},
	{
		.name = "tps65910-power",
	},
};

#define TPS65910_SPEED 	200 * 1000

static int tps65910_i2c_read(struct tps65910 *tps65910, u8 reg,
				  int bytes, void *dest)
{
	struct i2c_client *i2c = tps65910->i2c_client;
	struct i2c_msg xfer[2];
	int ret;
	//int i;

	/* Write register */
	xfer[0].addr = i2c->addr;
	xfer[0].flags = 0;
	xfer[0].len = 1;
	xfer[0].buf = &reg;
	xfer[0].scl_rate = TPS65910_SPEED;

	/* Read data */
	xfer[1].addr = i2c->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = bytes;
	xfer[1].buf = dest;
	xfer[1].scl_rate = TPS65910_SPEED;

	ret = i2c_transfer(i2c->adapter, xfer, 2);
	//for(i=0;i<bytes;i++)
	//printk("%s:reg=0x%x,value=0x%x\n",__func__,reg+i,*(u8 *)dest++);
	if (ret == 2)
		ret = 0;
	else if (ret >= 0)
		ret = -EIO;
	
	return ret;
}

static int tps65910_i2c_write(struct tps65910 *tps65910, u8 reg,
				   int bytes, void *src)
{
	struct i2c_client *i2c = tps65910->i2c_client;
	/* we add 1 byte for device register */
	u8 msg[TPS65910_MAX_REGISTER + 1];
	int ret;
	//int i;
	
	if (bytes > TPS65910_MAX_REGISTER)
		return -EINVAL;
	
	msg[0] = reg;
	memcpy(&msg[1], src, bytes);

	//for(i=0;i<bytes;i++)
	//printk("%s:reg=0x%x,value=0x%x\n",__func__,reg+i,msg[i+1]);
	
	ret = i2c_master_send(i2c, msg, bytes + 1);
	if (ret < 0)
		return ret;
	if (ret != bytes + 1)
		return -EIO;

	return 0;
}

static inline int tps65910_read(struct tps65910 *tps65910, u8 reg)
{
	u8 val;
	int err;

	err = tps65910->read(tps65910, reg, 1, &val);
	if (err < 0)
		return err;

	return val;
}

static inline int tps65910_write(struct tps65910 *tps65910, u8 reg, u8 val)
{
	return tps65910->write(tps65910, reg, 1, &val);
}

int tps65910_reg_read(struct tps65910 *tps65910, u8 reg)
{
	int data;

	mutex_lock(&tps65910->io_mutex);

	data = tps65910_read(tps65910, reg);
	if (data < 0)
		dev_err(tps65910->dev, "Read from reg 0x%x failed\n", reg);

	mutex_unlock(&tps65910->io_mutex);
	return data;
}
EXPORT_SYMBOL_GPL(tps65910_reg_read);

int tps65910_reg_write(struct tps65910 *tps65910, u8 reg, u8 val)
{
	int err;

	mutex_lock(&tps65910->io_mutex);

	err = tps65910_write(tps65910, reg, val);
	if (err < 0)
		dev_err(tps65910->dev, "Write for reg 0x%x failed\n", reg);

	mutex_unlock(&tps65910->io_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(tps65910_reg_write);

/**
 * tps65910_bulk_read: Read multiple tps65910 registers
 *
 * @tps65910: Device to read from
 * @reg: First register
 * @count: Number of registers
 * @buf: Buffer to fill.
 */
int tps65910_bulk_read(struct tps65910 *tps65910, u8 reg,
		     int count, u8 *buf)
{
	int ret;

	mutex_lock(&tps65910->io_mutex);
	
	ret = tps65910->read(tps65910, reg, count, buf);

	mutex_unlock(&tps65910->io_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(tps65910_bulk_read);

int tps65910_bulk_write(struct tps65910 *tps65910, u8 reg,
		     int count, u8 *buf)
{
	int ret;

	mutex_lock(&tps65910->io_mutex);
	
	ret = tps65910->write(tps65910, reg, count, buf);

	mutex_unlock(&tps65910->io_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(tps65910_bulk_write);




int tps65910_set_bits(struct tps65910 *tps65910, u8 reg, u8 mask)
{
	u8 data;
	int err;

	mutex_lock(&tps65910->io_mutex);
	err = tps65910_i2c_read(tps65910, reg, 1, &data);
	if (err) {
		dev_err(tps65910->dev, "%s:read from reg %x failed\n", __func__,reg);
		goto out;
	}

	data |= mask;
	err = tps65910_i2c_write(tps65910, reg, 1, &data);
	if (err)
		dev_err(tps65910->dev, "%s:write to reg %x failed\n", __func__,reg);

out:
	mutex_unlock(&tps65910->io_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(tps65910_set_bits);

int tps65910_clear_bits(struct tps65910 *tps65910, u8 reg, u8 mask)
{
	u8 data;
	int err;

	mutex_lock(&tps65910->io_mutex);
	err = tps65910_i2c_read(tps65910, reg, 1, &data);
	if (err) {
		dev_err(tps65910->dev, "read from reg %x failed\n", reg);
		goto out;
	}

	data &= ~mask;
	err = tps65910_i2c_write(tps65910, reg, 1, &data);
	if (err)
		dev_err(tps65910->dev, "write to reg %x failed\n", reg);

out:
	mutex_unlock(&tps65910->io_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(tps65910_clear_bits);

static int tps65910_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct tps65910 *tps65910;
	struct tps65910_board *pmic_plat_data;
	struct tps65910_platform_data *init_data;
	int ret = 0;

	pmic_plat_data = dev_get_platdata(&i2c->dev);
	if (!pmic_plat_data)
		return -EINVAL;

	init_data = kzalloc(sizeof(struct tps65910_platform_data), GFP_KERNEL);
	if (init_data == NULL)
		return -ENOMEM;

	init_data->irq = pmic_plat_data->irq;
	init_data->irq_base = pmic_plat_data->irq_base;

	tps65910 = kzalloc(sizeof(struct tps65910), GFP_KERNEL);
	if (tps65910 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, tps65910);
	tps65910->dev = &i2c->dev;
	tps65910->i2c_client = i2c;
	tps65910->id = id->driver_data;
	tps65910->read = tps65910_i2c_read;
	tps65910->write = tps65910_i2c_write;
	mutex_init(&tps65910->io_mutex);

	ret = mfd_add_devices(tps65910->dev, -1,
			      tps65910s, ARRAY_SIZE(tps65910s),
			      NULL, 0);
	if (ret < 0)
		goto err;
	
	g_tps65910 = tps65910;
	
	if (pmic_plat_data && pmic_plat_data->pre_init) {
		ret = pmic_plat_data->pre_init(tps65910);
		if (ret != 0) {
			dev_err(tps65910->dev, "pre_init() failed: %d\n", ret);
			goto err;
		}
	}

	tps65910_gpio_init(tps65910, pmic_plat_data->gpio_base);

	ret = tps65910_irq_init(tps65910, init_data->irq, init_data);
	if (ret < 0)
		goto err;

	if (pmic_plat_data && pmic_plat_data->post_init) {
		ret = pmic_plat_data->post_init(tps65910);
		if (ret != 0) {
			dev_err(tps65910->dev, "post_init() failed: %d\n", ret);
			goto err;
		}
	}
	printk("%s:irq=%d,irq_base=%d,gpio_base=%d\n",__func__,init_data->irq,init_data->irq_base,pmic_plat_data->gpio_base);
	return ret;

err:
	mfd_remove_devices(tps65910->dev);
	kfree(tps65910);
	return ret;
}


int tps65910_device_shutdown(void)
{
	int val = 0;
	int err = -1;
	struct tps65910 *tps65910 = g_tps65910;
	
	printk("%s\n",__func__);

	val = tps65910_reg_read(tps65910, TPS65910_REG_DEVCTRL);
        if (val<0) {
                printk(KERN_ERR "Unable to read TPS65910_REG_DCDCCTRL reg\n");
                return -EIO;
        }
	
	val |= (1 << 3)|(1 << 0);
	val &= ~(1 << 6);	//keep rtc
	err = tps65910_reg_write(tps65910, TPS65910_REG_DEVCTRL, val);
	if (err) {
		printk(KERN_ERR "Unable to read TPS65910 Reg at offset 0x%x= \
				\n", TPS65910_REG_VDIG1);
		return err;
	}
	
	return 0;	
}
EXPORT_SYMBOL_GPL(tps65910_device_shutdown);



static int tps65910_i2c_remove(struct i2c_client *i2c)
{
	struct tps65910 *tps65910 = i2c_get_clientdata(i2c);

	mfd_remove_devices(tps65910->dev);
	kfree(tps65910);

	return 0;
}

static const struct i2c_device_id tps65910_i2c_id[] = {
       { "tps65910", TPS65910 },
       { "tps65911", TPS65911 },
       { }
};
MODULE_DEVICE_TABLE(i2c, tps65910_i2c_id);


static struct i2c_driver tps65910_i2c_driver = {
	.driver = {
		   .name = "tps65910",
		   .owner = THIS_MODULE,
	},
	.probe = tps65910_i2c_probe,
	.remove = tps65910_i2c_remove,
	.id_table = tps65910_i2c_id,
};

static int __init tps65910_i2c_init(void)
{
	return i2c_add_driver(&tps65910_i2c_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall_sync(tps65910_i2c_init);

static void __exit tps65910_i2c_exit(void)
{
	i2c_del_driver(&tps65910_i2c_driver);
}
module_exit(tps65910_i2c_exit);

MODULE_AUTHOR("Graeme Gregory <gg@slimlogic.co.uk>");
MODULE_AUTHOR("Jorge Eduardo Candelaria <jedu@slimlogic.co.uk>");
MODULE_DESCRIPTION("TPS6591x chip family multi-function driver");
MODULE_LICENSE("GPL");
