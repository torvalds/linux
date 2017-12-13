/*
* Copyright (C) 2012 Invensense, Inc.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>

#include "inv_mpu_iio.h"
#ifdef INV_KERNEL_3_10
#include <linux/iio/sysfs.h>
#else
#include "sysfs.h"
#endif
#include "inv_counters.h"

/**
 *  inv_i2c_read_base() - Read one or more bytes from the device registers.
 *  @st:	Device driver instance.
 *  @reg:	First device register to be read from.
 *  @length:	Number of bytes to read.
 *  @data:	Data read from device.
 *  NOTE:This is not re-implementation of i2c_smbus_read because i2c
 *       address could be specified in this case. We could have two different
 *       i2c address due to secondary i2c interface.
 */
static int inv_i2c_read_base(struct inv_mpu_iio_s *st, u16 i2c_addr,
	u8 reg, u16 length, u8 *data)
{
	struct i2c_msg msgs[2];
	int res;

	if (!data)
		return -EINVAL;

	msgs[0].addr = i2c_addr;
	msgs[0].flags = 0;	/* write */
	msgs[0].buf = &reg;
	msgs[0].len = 1;
	/* msgs[0].scl_rate = 200*1000; */

	msgs[1].addr = i2c_addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].buf = data;
	msgs[1].len = length;
	/* msgs[1].scl_rate = 200*1000; */

	res = i2c_transfer(st->sl_handle, msgs, 2);

	if (res < 2) {
		if (res >= 0)
			res = -EIO;
	} else
		res = 0;

	INV_I2C_INC_MPUWRITE(3);
	INV_I2C_INC_MPUREAD(length);
#if 0
	{
		char *read = 0;
		pr_debug("%s RD%02X%02X%02X -> %s%s\n", st->hw->name,
			 i2c_addr, reg, length,
			 wr_pr_debug_begin(data, length, read),
			 wr_pr_debug_end(read));
	}
#endif
	return res;
}

/**
 *  inv_i2c_single_write_base() - Write a byte to a device register.
 *  @st:	Device driver instance.
 *  @reg:	Device register to be written to.
 *  @data:	Byte to write to device.
 *  NOTE:This is not re-implementation of i2c_smbus_write because i2c
 *       address could be specified in this case. We could have two different
 *       i2c address due to secondary i2c interface.
 */
static int inv_i2c_single_write_base(struct inv_mpu_iio_s *st,
	u16 i2c_addr, u8 reg, u8 data)
{
	u8 tmp[2];
	struct i2c_msg msg;
	int res;
	tmp[0] = reg;
	tmp[1] = data;

	msg.addr = i2c_addr;
	msg.flags = 0;	/* write */
	msg.buf = tmp;
	msg.len = 2;
	/* msg.scl_rate = 200*1000; */

	pr_debug("%s WR%02X%02X%02X\n", st->hw->name, i2c_addr, reg, data);
	INV_I2C_INC_MPUWRITE(3);

	res = i2c_transfer(st->sl_handle, &msg, 1);
	if (res < 1) {
		if (res == 0)
			res = -EIO;
		return res;
	} else
		return 0;
}

static int inv_i2c_single_write(struct inv_mpu_iio_s *st, u8 reg, u8 data)
{
	return inv_i2c_single_write_base(st, st->i2c_addr, reg, data);
}

static int inv_i2c_read(struct inv_mpu_iio_s *st, u8 reg, int len, u8 *data)
{
	return inv_i2c_read_base(st, st->i2c_addr, reg, len, data);
}

static int inv_i2c_secondary_read(struct inv_mpu_iio_s *st, u8 reg, int len, u8 *data)
{
	return inv_i2c_read_base(st, st->plat_data.secondary_i2c_addr, reg, len, data);
}

static int inv_i2c_secondary_write(struct inv_mpu_iio_s *st, u8 reg, u8 data)
{
	return inv_i2c_single_write_base(st, st->plat_data.secondary_i2c_addr, reg, data);
}

static int mpu_i2c_memory_write(struct inv_mpu_iio_s *st, u8 mpu_addr, u16 mem_addr,
		     u32 len, u8 const *data)
{
	u8 bank[2];
	u8 addr[2];
	u8 buf[513];

	struct i2c_msg msg;
	int res;

	if (!data || !st)
		return -EINVAL;

	if (len >= (sizeof(buf) - 1))
		return -ENOMEM;

	bank[0] = REG_BANK_SEL;
	bank[1] = mem_addr >> 8;

	addr[0] = REG_MEM_START_ADDR;
	addr[1] = mem_addr & 0xFF;

	buf[0] = REG_MEM_RW;
	memcpy(buf + 1, data, len);

	/* write message */
	msg.addr = mpu_addr;
	msg.flags = 0;
	msg.buf = bank;
	msg.len = sizeof(bank);
	/* msg.scl_rate = 200*1000; */
	INV_I2C_INC_MPUWRITE(3);
	res = i2c_transfer(st->sl_handle, &msg, 1);
	if (res < 1) {
		if (res >= 0)
			res = -EIO;
		return res;
	}

	msg.addr = mpu_addr;
	msg.flags = 0;
	msg.buf = addr;
	msg.len = sizeof(addr);
	/* msg.scl_rate = 200*1000; */
	INV_I2C_INC_MPUWRITE(3);
	res = i2c_transfer(st->sl_handle, &msg, 1);
	if (res < 1) {
		if (res >= 0)
			res = -EIO;
		return res;
	}

	msg.addr = mpu_addr;
	msg.flags = 0;
	msg.buf = (u8 *)buf;
	msg.len = len + 1;
	/* msg.scl_rate = 200*1000; */
	INV_I2C_INC_MPUWRITE(2+len);
	res = i2c_transfer(st->sl_handle, &msg, 1);
	if (res < 1) {
		if (res >= 0)
			res = -EIO;
		return res;
	}
#if 0
	{
		char *write = 0;
		pr_debug("%s WM%02X%02X%02X%s%s - %d\n", st->hw->name,
			 mpu_addr, bank[1], addr[1],
			 wr_pr_debug_begin(data, len, write),
			 wr_pr_debug_end(write),
			 len);
	}
#endif
	return 0;
}

static int mpu_i2c_memory_read(struct inv_mpu_iio_s *st, u8 mpu_addr, u16 mem_addr,
		    u32 len, u8 *data)
{
	u8 bank[2];
	u8 addr[2];
	u8 buf;

	struct i2c_msg msg;
	int res;

	if (!data || !st)
		return -EINVAL;

	bank[0] = REG_BANK_SEL;
	bank[1] = mem_addr >> 8;

	addr[0] = REG_MEM_START_ADDR;
	addr[1] = mem_addr & 0xFF;

	buf = REG_MEM_RW;

	/* write message */
	msg.addr = mpu_addr;
	msg.flags = 0;
	msg.buf = bank;
	msg.len = sizeof(bank);
	/* msg.scl_rate = 200*1000; */
	INV_I2C_INC_MPUWRITE(3);
	res = i2c_transfer(st->sl_handle, &msg, 1);
	if (res < 1) {
		if (res >= 0)
			res = -EIO;
		return res;
	}

	msg.addr = mpu_addr;
	msg.flags = 0;
	msg.buf = addr;
	msg.len = sizeof(addr);
	/* msg.scl_rate = 200*1000; */
	INV_I2C_INC_MPUWRITE(3);
	res = i2c_transfer(st->sl_handle, &msg, 1);
	if (res < 1) {
		if (res >= 0)
			res = -EIO;
		return res;
	}

	msg.addr = mpu_addr;
	msg.flags = 0;
	msg.buf = &buf;
	msg.len = 1;
	/* msg.scl_rate = 200*1000; */
	INV_I2C_INC_MPUWRITE(3);
	res = i2c_transfer(st->sl_handle, &msg, 1);
	if (res < 1) {
		if (res >= 0)
			res = -EIO;
		return res;
	}

	msg.addr = mpu_addr;
	msg.flags = I2C_M_RD;
	msg.buf = data;
	msg.len = len;
	/* msg.scl_rate = 200*1000; */
	INV_I2C_INC_MPUREAD(len);
	res = i2c_transfer(st->sl_handle, &msg, 1);
	if (res < 1) {
		if (res >= 0)
			res = -EIO;
		return res;
	}
#if 0
	{
		char *read = 0;
		pr_debug("%s RM%02X%02X%02X%02X - %s%s\n", st->hw->name,
			 mpu_addr, bank[1], addr[1], len,
			 wr_pr_debug_begin(data, len, read),
			 wr_pr_debug_end(read));
	}
#endif
	return 0;
}

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of.h>

static struct mpu_platform_data mpu_data = {
	.int_config  = 0x00,
	.level_shifter = 0,
	.orientation = {
			0,  1,  0,
			-1,  0,  0,
			0,  0, -1,
	},
/*
	.sec_slave_type = SECONDARY_SLAVE_TYPE_COMPASS,
	.sec_slave_id = COMPASS_ID_AK8963,
	.secondary_i2c_addr = 0x0d,
	.secondary_orientation = {
			-1,  0,  0,
			0,  1,  0,
			0,  0, -1,
	},
	.key = {
		221,  22, 205,   7, 217, 186, 151, 55,
		206, 254,  35, 144, 225, 102,  47, 50,
	},
*/
};

static int of_inv_parse_platform_data(struct i2c_client *client,
				      struct mpu_platform_data *pdata)
{
	int ret;
	int length = 0, size = 0;
	struct property *prop;
	u32 orientation[9];
	int orig_x, orig_y, orig_z;
	int i;
	struct device_node *np = client->dev.of_node;
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);
	unsigned long irq_flags;
	int irq_pin;
	int gpio_pin;
	int debug;
	int hw_pwoff;

	gpio_pin = of_get_named_gpio_flags(np, "irq-gpio", 0, (enum of_gpio_flags *)&irq_flags);
	gpio_request(gpio_pin, "mpu6500");
	irq_pin = gpio_to_irq(gpio_pin);
	client->irq = irq_pin;

	ret = of_property_read_u8(np, "mpu-int_config", &mpu_data.int_config);
	if (ret != 0) {
		dev_err(&client->dev, "get mpu-int_config error\n");
		return -EIO;
	}

	ret = of_property_read_u8(np, "mpu-level_shifter", &mpu_data.level_shifter);
	if (ret != 0) {
		dev_err(&client->dev, "get mpu-level_shifter error\n");
		return -EIO;
	}

	prop = of_find_property(np, "mpu-orientation", &length);
	if (!prop) {
		dev_err(&client->dev, "get mpu-orientation length error\n");
		return -EINVAL;
	}
	size = length / sizeof(u32);
	if ((size > 0) && (size < 10)) {
		ret = of_property_read_u32_array(np, "mpu-orientation", orientation, size);
		if (ret < 0) {
			dev_err(&client->dev, "get mpu-orientation data error\n");
			return -EINVAL;
		}
		for (i = 0; i < 9; i++)
			mpu_data.orientation[i] = orientation[i];
	} else {
		dev_info(&client->dev, "use default orientation\n");
	}

	ret = of_property_read_u32(np, "orientation-x", &orig_x);
	if (ret != 0) {
		dev_err(&client->dev, "get orientation-x error\n");
		return -EIO;
	}
	if (orig_x > 0) {
		for (i = 0; i < 3; i++)
			if (mpu_data.orientation[i])
				mpu_data.orientation[i] = -1;
	}

	ret = of_property_read_u32(np, "orientation-y", &orig_y);
	if (ret != 0) {
		dev_err(&client->dev, "get orientation-y error\n");
		return -EIO;
	}
	if (orig_y > 0) {
		for (i = 3; i < 6; i++)
			if (mpu_data.orientation[i])
				mpu_data.orientation[i] = -1;
	}

	ret = of_property_read_u32(np, "orientation-z", &orig_z);
	if (ret != 0) {
		dev_err(&client->dev, "get orientation-z error\n");
		return -EIO;
	}
	if (orig_z > 0) {
		for (i = 6; i < 9; i++)
			if (mpu_data.orientation[i])
				mpu_data.orientation[i] = -1;
	}

	ret = of_property_read_u32(np, "support-hw-poweroff", &hw_pwoff);
	if (ret != 0) {
		st->support_hw_poweroff = 0;
	}
	st->support_hw_poweroff = hw_pwoff;

	ret = of_property_read_u32(np, "mpu-debug", &debug);
	if (ret != 0) {
		dev_err(&client->dev, "get mpu-debug error\n");
		return -EINVAL;
	}
	if (debug) {
		dev_info(&client->dev, "int_config=%d,level_shifter=%d,client.addr=%x,client.irq=%x\n",
							mpu_data.int_config,
							mpu_data.level_shifter,
							client->addr,
							client->irq);
		for (i = 0; i < size; i++)
			dev_info(&client->dev, "%d ", mpu_data.orientation[i]);
		dev_info(&client->dev, "\n");
	}

	return 0;
}

/**
 *  inv_mpu_probe() - probe function.
 */
static int inv_mpu_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct inv_mpu_iio_s *st;
	struct iio_dev *indio_dev;
	int result;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		result = -ENOSYS;
		pr_err("I2c function error\n");
		goto out_no_free;
	}
#ifdef INV_KERNEL_3_10
    indio_dev = iio_device_alloc(sizeof(*st));
#else
	indio_dev = iio_allocate_device(sizeof(*st));
#endif
	if (indio_dev == NULL) {
		pr_err("memory allocation failed\n");
		result =  -ENOMEM;
		goto out_no_free;
	}
	st = iio_priv(indio_dev);
	st->client = client;
	st->sl_handle = client->adapter;
	st->i2c_addr = client->addr;
	i2c_set_clientdata(client, indio_dev);
	if (client->dev.of_node) {
		result = of_inv_parse_platform_data(client, &st->plat_data);
		if (result)
			goto out_free;
		st->plat_data = mpu_data;
		pr_info("secondary_i2c_addr=%x\n", st->plat_data.secondary_i2c_addr);
	} else
		st->plat_data =
			*(struct mpu_platform_data *)dev_get_platdata(&client->dev);

	st->irq = client->irq;
	st->plat_read = inv_i2c_read;
	st->plat_single_write = inv_i2c_single_write;
	st->secondary_read = inv_i2c_secondary_read;
	st->secondary_write = inv_i2c_secondary_write;
	st->memory_read = mpu_i2c_memory_read;
	st->memory_write = mpu_i2c_memory_write;

	/* power is turned on inside check chip type*/
	result = inv_check_chip_type(st, id->name);
	if (result)
		goto out_free;

	result = st->init_config(indio_dev);
	if (result) {
		dev_err(&client->adapter->dev,
			"Could not initialize device.\n");
		goto out_free;
	}
	result = st->set_power_state(st, false);
	if (result) {
		dev_err(&client->adapter->dev,
			"%s could not be turned off.\n", st->hw->name);
		goto out_free;
	}

	/* Make state variables available to all _show and _store functions. */
	indio_dev->dev.parent = &client->dev;
	if (!strcmp(id->name, "mpu6xxx"))
		indio_dev->name = st->name;
	else
		indio_dev->name = id->name;

	inv_set_iio_info(st, indio_dev);

	result = inv_mpu_configure_ring(indio_dev);
	if (result) {
		pr_err("configure ring buffer fail\n");
		goto out_free;
	}
	st->dev = &client->dev;
	result = inv_mpu_probe_trigger(indio_dev);
	if (result) {
		pr_err("trigger probe fail\n");
		goto out_unreg_ring;
	}

	/* Tell the i2c counter, we have an IRQ */
	INV_I2C_SETIRQ(IRQ_MPU, client->irq);

	result = iio_device_register(indio_dev);
	if (result) {
		pr_err("IIO device register fail\n");
		goto out_remove_trigger;
	}

	if (INV_MPU6050 == st->chip_type ||
	    INV_MPU6500 == st->chip_type) {
		result = inv_create_dmp_sysfs(indio_dev);
		if (result) {
			pr_err("create dmp sysfs failed\n");
			goto out_unreg_iio;
		}
	}

	INIT_KFIFO(st->timestamps);
	spin_lock_init(&st->time_stamp_lock);
	dev_info(&client->dev, "%s is ready to go!\n",
					indio_dev->name);

	return 0;
out_unreg_iio:
	iio_device_unregister(indio_dev);
out_remove_trigger:
	if (indio_dev->modes & INDIO_BUFFER_TRIGGERED)
		inv_mpu_remove_trigger(indio_dev);
out_unreg_ring:
	inv_mpu_unconfigure_ring(indio_dev);
out_free:
#ifdef INV_KERNEL_3_10
    iio_device_free(indio_dev);
#else
	iio_free_device(indio_dev);
#endif
out_no_free:
	dev_err(&client->adapter->dev, "%s failed %d\n", __func__, result);

	return -EIO;
}

static void inv_mpu_shutdown(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);
	struct inv_reg_map_s *reg;
	int result;

	reg = &st->reg;
	dev_dbg(&client->adapter->dev, "Shutting down %s...\n", st->hw->name);

	/* reset to make sure previous state are not there */
	result = inv_plat_single_write(st, reg->pwr_mgmt_1, BIT_H_RESET);
	if (result)
		dev_err(&client->adapter->dev, "Failed to reset %s\n",
			st->hw->name);
	msleep(POWER_UP_TIME);
	/* turn off power to ensure gyro engine is off */
	result = st->set_power_state(st, false);
	if (result)
		dev_err(&client->adapter->dev, "Failed to turn off %s\n",
			st->hw->name);
}

/**
 *  inv_mpu_remove() - remove function.
 */
static int inv_mpu_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);
	kfifo_free(&st->timestamps);
	iio_device_unregister(indio_dev);
	if (indio_dev->modes & INDIO_BUFFER_TRIGGERED)
		inv_mpu_remove_trigger(indio_dev);
	inv_mpu_unconfigure_ring(indio_dev);
#ifdef INV_KERNEL_3_10
    iio_device_free(indio_dev);
#else
	iio_free_device(indio_dev);
#endif

	dev_info(&client->adapter->dev, "inv-mpu-iio module removed.\n");

	return 0;
}

#ifdef CONFIG_PM
static int __maybe_unused inv_mpu_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);
	int result;

	pr_debug("%s inv_mpu_resume\n", st->hw->name);

	mutex_lock(&indio_dev->mlock);
	if (st->support_hw_poweroff) {
		/* reset to make sure previous state are not there */
		result = inv_plat_single_write(st, st->reg.pwr_mgmt_1, BIT_H_RESET);
		if (result) {
			pr_err("%s, reset failed\n", __func__);
			goto rw_err;
		}
		msleep(POWER_UP_TIME);
		/* toggle power state */
		result = st->set_power_state(st, false);
		if (result) {
			pr_err("%s, set_power_state false failed\n", __func__);
			goto rw_err;
		}
		result = st->set_power_state(st, true);
		if (result) {
			pr_err("%s, set_power_state true failed\n", __func__);
			goto rw_err;
		}
		result = inv_plat_single_write(st, st->reg.user_ctrl, st->i2c_dis);
		if (result) {
			pr_err("%s, set user_ctrl failed\n", __func__);
			goto rw_err;
		}
		inv_resume_recover_setting(st);
	} else {
		result = st->set_power_state(st, true);
	}

rw_err:
	mutex_unlock(&indio_dev->mlock);
	return result;
}

static int __maybe_unused inv_mpu_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);
	int result = 0;

	pr_debug("%s inv_mpu_suspend\n", st->hw->name);

	mutex_lock(&indio_dev->mlock);
	if ((!st->chip_config.dmp_on) ||
		(!st->chip_config.enable) ||
		(!st->chip_config.dmp_event_int_on))
		result = st->set_power_state(st, false);
	mutex_unlock(&indio_dev->mlock);

	return result;
}
static const struct dev_pm_ops inv_mpu_pmops = {
	SET_SYSTEM_SLEEP_PM_OPS(inv_mpu_suspend, inv_mpu_resume)
};
#define INV_MPU_PMOPS (&inv_mpu_pmops)
#else
#define INV_MPU_PMOPS NULL
#endif /* CONFIG_PM */

static const u16 normal_i2c[] = { I2C_CLIENT_END };
/* device id table is used to identify what device can be
 * supported by this driver
 */
static const struct i2c_device_id inv_mpu_id[] = {
	{"itg3500", INV_ITG3500},
	{"mpu3050", INV_MPU3050},
	{"mpu6050", INV_MPU6050},
	{"mpu9150", INV_MPU9150},
	{"mpu6500", INV_MPU6500},
	{"mpu9250", INV_MPU9250},
	{"mpu6xxx", INV_MPU6XXX},
	{"mpu6515", INV_MPU6515},
	{}
};

MODULE_DEVICE_TABLE(i2c, inv_mpu_id);

static const struct of_device_id inv_mpu_of_match[] = {
	{ .compatible = "invensense,itg3500", },
	{ .compatible = "invensense,mpu3050", },
	{ .compatible = "invensense,mpu6050", },
	{ .compatible = "invensense,mpu9150", },
	{ .compatible = "invensense,mpu6500", },
	{ .compatible = "invensense,mpu9250", },
	{ .compatible = "invensense,mpu6xxx", },
	{ .compatible = "invensense,mpu9350", },
	{ .compatible = "invensense,mpu6515", },
	{}
};

MODULE_DEVICE_TABLE(of, inv_mpu_of_match);

static struct i2c_driver inv_mpu_driver = {
	.class = I2C_CLASS_HWMON,
	.probe		=	inv_mpu_probe,
	.remove		=	inv_mpu_remove,
	.shutdown	=	inv_mpu_shutdown,
	.id_table	=	inv_mpu_id,
	.driver = {
		.owner	=	THIS_MODULE,
		.name	=	"inv-mpu-iio",
		.pm     =       INV_MPU_PMOPS,
		.of_match_table = of_match_ptr(inv_mpu_of_match),
	},
	.address_list = normal_i2c,
};

static int __init inv_mpu_init(void)
{
	int result = i2c_add_driver(&inv_mpu_driver);
    pr_info("%s:%d\n", __func__, __LINE__);
	if (result) {
		pr_err("failed\n");
		return result;
	}
	return 0;
}

static void __exit inv_mpu_exit(void)
{
	i2c_del_driver(&inv_mpu_driver);
}

module_init(inv_mpu_init);
module_exit(inv_mpu_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Invensense device driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("inv-mpu-iio");

/**
 *  @}
 */
