/* drivers/mfd/rt5025-i2c.c
 * I2C Driver for Richtek RT5025
 * Multi function device - multi functional baseband PMIC
 *
 * Copyright (C) 2013
 * Author: CY Huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/mfd/rt5025.h>

#define ROCKCHIP_I2C_RATE (200*1000)

static inline int rt5025_read_device(struct i2c_client *i2c,
				      int reg, int bytes, void *dest)
{
	#if 1
	int ret;
	unsigned char reg_addr = reg;
	struct i2c_msg i2c_msg[2];
	i2c_msg[0].addr = i2c->addr;
	i2c_msg[0].flags = i2c->flags;
	i2c_msg[0].len = 1;
	i2c_msg[0].buf = &reg_addr;
	i2c_msg[0].scl_rate = ROCKCHIP_I2C_RATE;
	i2c_msg[1].addr = i2c->addr;
	i2c_msg[1].flags = i2c->flags | I2C_M_RD;
	i2c_msg[1].len = bytes;
	i2c_msg[1].buf = dest;
	i2c_msg[1].scl_rate = ROCKCHIP_I2C_RATE;
	ret = i2c_transfer(i2c->adapter, i2c_msg, 2);
	#else
	int ret;
	if (bytes > 1)
		ret = i2c_smbus_read_i2c_block_data(i2c, reg, bytes, dest);
	else {
		ret = i2c_smbus_read_byte_data(i2c, reg);
		if (ret < 0)
			return ret;
		*(unsigned char *)dest = (unsigned char)ret;
	}
	#endif
	return ret;
}

int rt5025_reg_block_read(struct i2c_client *i2c, \
			int reg, int bytes, void *dest)
{
	return rt5025_read_device(i2c, reg, bytes, dest);
}
EXPORT_SYMBOL(rt5025_reg_block_read);

static inline int rt5025_write_device(struct i2c_client *i2c,
				      int reg, int bytes, void *dest)
{
	#if 1
	int ret;
	struct i2c_msg i2c_msg;
	char *tx_buf = (char*)kmalloc(bytes+1, GFP_KERNEL);
	tx_buf[0] = reg;
	memcpy(tx_buf+1, dest, bytes);
	i2c_msg.addr = i2c->addr;
	i2c_msg.flags = i2c->flags;
	i2c_msg.len = bytes + 1;
	i2c_msg.buf = tx_buf;
	i2c_msg.scl_rate = ROCKCHIP_I2C_RATE;
	ret = i2c_transfer(i2c->adapter, &i2c_msg, 1);
	kfree(tx_buf);
	#else
	int ret;
	if (bytes > 1)
		ret = i2c_smbus_write_i2c_block_data(i2c, reg, bytes, dest);
	else {
		ret = i2c_smbus_write_byte_data(i2c, reg, dest);
		if (ret < 0)
			return ret;
		*(unsigned char *)dest = (unsigned char)ret;
	}
	#endif
	return ret;
}

int rt5025_reg_block_write(struct i2c_client *i2c, \
			int reg, int bytes, void *dest)
{
	return rt5025_write_device(i2c, reg, bytes, dest);
}
EXPORT_SYMBOL(rt5025_reg_block_write);

int rt5025_reg_read(struct i2c_client *i2c, int reg)
{
	struct rt5025_chip* chip = i2c_get_clientdata(i2c);
	int ret;
	#if 1
	unsigned char reg_addr = reg;
	unsigned char reg_data = 0;
	struct i2c_msg i2c_msg[2];
	RTINFO("I2C Read (client : 0x%x) reg = 0x%x\n",
           (unsigned int)i2c,(unsigned int)reg);
	mutex_lock(&chip->io_lock);
	i2c_msg[0].addr = i2c->addr;
	i2c_msg[0].flags = i2c->flags;
	i2c_msg[0].len = 1;
	i2c_msg[0].buf = &reg_addr;
	i2c_msg[0].scl_rate = ROCKCHIP_I2C_RATE;
	i2c_msg[1].addr = i2c->addr;
	i2c_msg[1].flags = i2c->flags | I2C_M_RD;
	i2c_msg[1].len = 1;
	i2c_msg[1].buf = &reg_data;
	i2c_msg[1].scl_rate = ROCKCHIP_I2C_RATE;
	ret = i2c_transfer(i2c->adapter, i2c_msg, 2);
	mutex_unlock(&chip->io_lock);
	#else
	RTINFO("I2C Read (client : 0x%x) reg = 0x%x\n",
           (unsigned int)i2c,(unsigned int)reg);
	mutex_lock(&chip->io_lock);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	mutex_unlock(&chip->io_lock);
	#endif
	return reg_data;
}
EXPORT_SYMBOL(rt5025_reg_read);

int rt5025_reg_write(struct i2c_client *i2c, int reg, unsigned char data)
{
	struct rt5025_chip* chip = i2c_get_clientdata(i2c);
	int ret;
	#if 1
	unsigned char xfer_data[2];
	struct i2c_msg i2c_msg;
	RTINFO("I2C Write (client : 0x%x) reg = 0x%x, data = 0x%x\n",
           (unsigned int)i2c,(unsigned int)reg,(unsigned int)data);
	xfer_data[0] = reg;
	xfer_data[1] = data;
	mutex_lock(&chip->io_lock);
	i2c_msg.addr = i2c->addr;
	i2c_msg.flags = i2c->flags;
	i2c_msg.len = 2;
	i2c_msg.buf = xfer_data;
	i2c_msg.scl_rate = ROCKCHIP_I2C_RATE;
	ret = i2c_transfer(i2c->adapter, &i2c_msg, 1);
	mutex_unlock(&chip->io_lock);
	#else
	RTINFO("I2C Write (client : 0x%x) reg = 0x%x, data = 0x%x\n",
           (unsigned int)i2c,(unsigned int)reg,(unsigned int)data);
	mutex_lock(&chip->io_lock);
	ret = i2c_smbus_write_byte_data(i2c, reg, data);
	mutex_unlock(&chip->io_lock);
	#endif

	return ret;
}
EXPORT_SYMBOL(rt5025_reg_write);

int rt5025_assign_bits(struct i2c_client *i2c, int reg,
		unsigned char mask, unsigned char data)
{
	struct rt5025_chip *chip = i2c_get_clientdata(i2c);
	unsigned char value;
	int ret;
	#if 1
	struct i2c_msg i2c_msg;
	u8 xfer_data[2] = {0};
	mutex_lock(&chip->io_lock);
	ret = rt5025_read_device(i2c, reg, 1, &value);
	if (ret < 0)
		goto out;
	
	value &= ~mask;
	value |= (data&mask);
	xfer_data[0] = reg;
	xfer_data[1] = value;
	i2c_msg.addr = i2c->addr;
	i2c_msg.flags = i2c->flags;
	i2c_msg.len = 2;
	i2c_msg.buf = xfer_data;
	i2c_msg.scl_rate = ROCKCHIP_I2C_RATE;
	ret = i2c_transfer(i2c->adapter, &i2c_msg, 1);
	#else
	mutex_lock(&chip->io_lock);

	ret = rt5025_read_device(i2c, reg, 1, &value);

	if (ret < 0)
		goto out;
	value &= ~mask;
	value |= (data&mask);
	ret = i2c_smbus_write_byte_data(i2c,reg,value);
	#endif
out:
	mutex_unlock(&chip->io_lock);
	return ret;
}
EXPORT_SYMBOL(rt5025_assign_bits);

int rt5025_set_bits(struct i2c_client *i2c, int reg,
		unsigned char mask)
{
	return rt5025_assign_bits(i2c,reg,mask,mask);
}
EXPORT_SYMBOL(rt5025_set_bits);

int rt5025_clr_bits(struct i2c_client *i2c, int reg,
		unsigned char mask)
{
	return rt5025_assign_bits(i2c,reg,mask,0);
}
EXPORT_SYMBOL(rt5025_clr_bits);

static int __devinit rt5025_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct rt5025_platform_data *pdata = client->dev.platform_data;
	struct rt5025_chip *chip;
	int ret = 0;
	u8 val;

	if (!pdata)
		return -EINVAL;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->irq = client->irq;
	chip->i2c = client;
	chip->dev = &client->dev;

#if 0
	if (pdata->event_callback)
	{
		chip->event_callback = kzalloc(sizeof(struct rt5025_event_callback), GFP_KERNEL);
		memcpy(chip->event_callback, pdata->event_callback, sizeof(struct rt5025_event_callback));
	}
#endif /* #if 0 */

	i2c_set_clientdata(client, chip);
	mutex_init(&chip->io_lock);
	
	rt5025_read_device(client,0x00,1,&val);
	if (val != 0x81){
		printk("The PMIC is not RT5025\n");
		return -ENODEV;
	}
	ret = rt5025_core_init(chip, pdata);
	if (ret < 0)
		dev_err(chip->dev, "rt5025_core_init_fail\n");
	else
		pr_info("RT5025 Initialize successfully\n");

	if (pdata && pdata->pre_init) {
		ret = pdata->pre_init(chip);
		if (ret != 0) {
			dev_err(chip->dev, "pre_init() failed: %d\n", ret);
		}
	}
	
	if (pdata && pdata->post_init) {
		ret = pdata->post_init();
		if (ret != 0) {
			dev_err(chip->dev, "post_init() failed: %d\n", ret);
		}
	}

	return ret;

}

static int __devexit rt5025_i2c_remove(struct i2c_client *client)
{
	struct rt5025_chip *chip = i2c_get_clientdata(client);
	rt5025_core_deinit(chip);
	#if 0
	if (chip->event_callback)
		kfree(chip->event_callback);
	#endif
	kfree(chip);
	return 0;
}

static int rt5025_i2c_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct rt5025_chip *chip = i2c_get_clientdata(client);
	chip->suspend = 1;
	RTINFO("\n");
	return 0;
}

static int rt5025_i2c_resume(struct i2c_client *client)
{
	struct rt5025_chip *chip = i2c_get_clientdata(client);
	chip->suspend = 0;
	RTINFO("\n");
	return 0;
}

static const struct i2c_device_id rt5025_id_table[] = {
	{ RT5025_DEVICE_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, rt5025_id_table);

static struct i2c_driver rt5025_driver = {
	.driver	= {
		.name	= RT5025_DEVICE_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= rt5025_i2c_probe,
	.remove		= __devexit_p(rt5025_i2c_remove),
	.suspend	= rt5025_i2c_suspend,
	.resume		= rt5025_i2c_resume,
	.id_table	= rt5025_id_table,
};

static int __init rt5025_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&rt5025_driver);
	if (ret != 0)
		pr_err("Failed to register RT5025 I2C driver: %d\n", ret);
	return ret;
}
subsys_initcall_sync(rt5025_i2c_init);

static void __exit rt5025_i2c_exit(void)
{
	i2c_del_driver(&rt5025_driver);
}
module_exit(rt5025_i2c_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("I2C Driver for Richtek RT5025");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com>");
MODULE_VERSION(RT5025_DRV_VER);
