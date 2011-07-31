/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/leds.h>
#include <linux/max9635.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>

#define DEBUG	1

#define MAX9635_ALLOWED_R_BYTES 1
#define MAX9635_ALLOWED_W_BYTES 2
#define MAX9635_MAX_RW_RETRIES 5
#define MAX9635_I2C_RETRY_DELAY 10
#define AUTO_INCREMENT          0x0

#define MAX9635_INT_STATUS	0x00
#define MAX9635_INT_EN		0x01
#define MAX9635_CONFIGURE	0x02
#define MAX9635_ALS_DATA_H	0x03
#define MAX9635_ALS_DATA_L	0x04
#define MAX9635_ALS_THRESH_H	0x05
#define MAX9635_ALS_THRESH_L	0x06
#define MAX9635_THRESH_TIMER	0x07

struct max9635_zone_conv {
	int lower_threshold;
	int upper_threshold;
};

struct max9635_data {
	struct input_dev *idev;
	struct i2c_client *client;
	struct delayed_work working_queue;
	struct max9635_platform_data *als_pdata;
	struct max9635_zone_conv max9635_zone_info[255];
	atomic_t enabled;
	spinlock_t irq_lock;
	int cur_irq_state;
	struct regulator *regulator;
};

struct max9635_data *max9635_misc_data;

#ifdef DEBUG
struct max9635_reg {
	const char *name;
	uint8_t reg;
} max9635_regs[] = {
	{"INT_STATUS",		MAX9635_INT_STATUS},
	{"INT_ENABLE",		MAX9635_INT_EN},
	{"CONFIG",		MAX9635_CONFIGURE},
	{"ALS_DATA_HIGH",	MAX9635_ALS_DATA_H},
	{"ALS_DATA_LOW",	MAX9635_ALS_DATA_L},
	{"ALS_THRESH_H",	MAX9635_ALS_THRESH_H},
	{"ALS_THRESH_L",	MAX9635_ALS_THRESH_L},
	{"ALS_THRESH_TIMER",	MAX9635_THRESH_TIMER},
};
#endif

static uint32_t max9635_debug = 0x00;
module_param_named(als_debug, max9635_debug, uint, 0664);

static int max9635_read_reg(struct max9635_data *als_data, u8 * buf, int len)
{
	int err;
	int tries = 0;
	struct i2c_msg msgs[] = {
		{
		 .addr = als_data->client->addr,
		 .flags = als_data->client->flags & I2C_M_TEN,
		 .len = 1,
		 .buf = buf,
		 },
		{
		 .addr = als_data->client->addr,
		 .flags = (als_data->client->flags & I2C_M_TEN) | I2C_M_RD,
		 .len = len,
		 .buf = buf,
		 },
	};

	do {
		err = i2c_transfer(als_data->client->adapter, msgs, 2);
		if (err != 2)
			msleep_interruptible(MAX9635_I2C_RETRY_DELAY);
	} while ((err != 2) && (++tries < MAX9635_MAX_RW_RETRIES));

	if (err != 2) {
		pr_err("%s:read transfer error\n", __func__);
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int max9635_write_reg(struct max9635_data *als_data, u8 * buf, int len)
{
	int err;
	int tries = 0;
	struct i2c_msg msgs[] = {
		{
		 .addr = als_data->client->addr,
		 .flags = als_data->client->flags & I2C_M_TEN,
		 .len = len + 1,
		 .buf = buf,
		 },
	};

	do {
		err = i2c_transfer(als_data->client->adapter, msgs, 1);
		if (err != 1)
			msleep_interruptible(MAX9635_I2C_RETRY_DELAY);
	} while ((err != 1) && (++tries < MAX9635_MAX_RW_RETRIES));

	if (err != 1) {
		pr_err("%s:write transfer error\n", __func__);
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int max9635_init_registers(struct max9635_data *als_data)
{
	u8 buf[2];

	buf[0] = (AUTO_INCREMENT | MAX9635_CONFIGURE);
	buf[1] = als_data->als_pdata->configure;
	if (max9635_write_reg(als_data, buf, 1))
		goto init_failed;

	buf[0] = (AUTO_INCREMENT | MAX9635_ALS_THRESH_H);
	buf[1] = als_data->als_pdata->def_high_threshold;
	if (max9635_write_reg(als_data, buf, 1))
		goto init_failed;

	buf[0] = (AUTO_INCREMENT | MAX9635_ALS_THRESH_L);
	buf[1] = als_data->als_pdata->def_low_threshold;
	if (max9635_write_reg(als_data, buf, 1))
		goto init_failed;

	buf[0] = (AUTO_INCREMENT | MAX9635_THRESH_TIMER);
	buf[1] = als_data->als_pdata->threshold_timer;
	if (max9635_write_reg(als_data, buf, 1))
		goto init_failed;

	return 0;

init_failed:
	pr_err("%s:Register 0x%d initialization failed\n", __func__, buf[0]);
	return -EINVAL;
}

static void max9635_irq_enable(struct max9635_data *als_data, int enable)
{
	unsigned long flags;

	spin_lock_irqsave(&als_data->irq_lock, flags);
	if (als_data->cur_irq_state != enable) {
		if (enable)
			enable_irq(als_data->client->irq);
		else
			disable_irq_nosync(als_data->client->irq);
		als_data->cur_irq_state = enable;
	}
	spin_unlock_irqrestore(&als_data->irq_lock, flags);
}

static irqreturn_t max9635_irq_handler(int irq, void *dev)
{
	struct max9635_data *als_data = dev;

	max9635_irq_enable(als_data, 0);
	schedule_delayed_work(&als_data->working_queue, 0);

	return IRQ_HANDLED;
}

static int max9635_read_adj_als(struct max9635_data *als_data)
{
	int ret;
	int lux = 0;
	u8 low_buf = MAX9635_ALS_DATA_L;
	u8 high_buf = MAX9635_ALS_DATA_H;
	u8 exponent;
	u16 mantissa;

	ret = max9635_read_reg(als_data, &high_buf, 1);
	if (ret != 0) {
		pr_err("%s: Unable to read lux high byte register: %d\n",
		       __func__, ret);
		return -1;
	}
	ret = max9635_read_reg(als_data, &low_buf, 1);
	if (ret != 0) {
		pr_err("%s: Unable to read lux low byte register: %d\n",
		       __func__, ret);
		return -1;
	}

	exponent = (high_buf & 0xf0) >> 4;
	mantissa = ((high_buf & 0x0f) << 4) | (low_buf & 0x0f);

	lux = ((0001 << exponent) * mantissa) / 20;
	if (max9635_debug & 1)
		pr_info("exp = 0x%X, mant = 0x%X, lux = %d\n",
			exponent, mantissa, lux);

	lux = lux * (exponent + als_data->als_pdata->lens_coeff);

	if (max9635_debug & 1)
		pr_info("%s:Reporting LUX %d\n", __func__, lux);
	return lux;
}

static int max9635_report_input(struct max9635_data *als_data)
{
	int ret = 0;
	int lux_val;
	u8 buf[2] = { MAX9635_INT_STATUS, 0x00 };

	lux_val = max9635_read_adj_als(als_data);
	if (lux_val >= 0) {
		input_event(als_data->idev, EV_MSC, MSC_RAW, lux_val);
		input_sync(als_data->idev);
	}

	/* Clear the interrupt status register */
	ret = max9635_read_reg(als_data, buf, 1);
	if (ret != 0) {
		pr_err("%s:Unable to read interrupt register: %d\n",
		       __func__, ret);
		return -1;
	}
	max9635_irq_enable(als_data, 1);
	return ret;
}

static int max9635_device_power(struct max9635_data *als_data, u8 state)
{
	int err;
	u8 buf[2] = { (AUTO_INCREMENT | MAX9635_INT_EN) };

	buf[1] = state;
	err = max9635_write_reg(als_data, buf, 1);
	if (err)
		pr_err("%s:Unable to turn off prox: %d\n", __func__, err);

	return err;
}

static int max9635_enable(struct max9635_data *als_data)
{
	int err;

	if (!atomic_cmpxchg(&als_data->enabled, 0, 1)) {
		if (!IS_ERR_OR_NULL(als_data->regulator))
			regulator_enable(als_data->regulator);
		err = max9635_device_power(als_data, 0x01);
		if (err) {
			atomic_set(&als_data->enabled, 0);
			return err;
		}
	}
	return 0;
}

static int max9635_disable(struct max9635_data *als_data)
{
	if (atomic_cmpxchg(&als_data->enabled, 1, 0)) {
		if (!IS_ERR_OR_NULL(als_data->regulator))
			regulator_disable(als_data->regulator);
		max9635_device_power(als_data, 0x00);
	}
	cancel_delayed_work_sync(&als_data->working_queue);

	return 0;
}

static int max9635_misc_open(struct inode *inode, struct file *file)
{
	int err;
	err = nonseekable_open(inode, file);
	if (err < 0)
		return err;

	file->private_data = max9635_misc_data;

	return 0;
}

static long max9635_misc_ioctl(struct file *file,
			       unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	u8 enable;
	struct max9635_data *als_data = file->private_data;

	switch (cmd) {
	case MAX9635_IOCTL_SET_ENABLE:
		if (copy_from_user(&enable, argp, 1))
			return -EFAULT;
		if (enable > 1)
			return -EINVAL;

		if (enable != 0)
			max9635_enable(als_data);
		else
			max9635_disable(als_data);

		break;

	case MAX9635_IOCTL_GET_ENABLE:
		enable = atomic_read(&als_data->enabled);
		if (copy_to_user(argp, &enable, 1))
			return -EINVAL;

		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations max9635_misc_fops = {
	.owner = THIS_MODULE,
	.open = max9635_misc_open,
	.unlocked_ioctl = max9635_misc_ioctl,
};

static struct miscdevice max9635_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MAX9635_NAME,
	.fops = &max9635_misc_fops,
};
#ifdef DEBUG
static ssize_t max9635_registers_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = container_of(dev, struct i2c_client,
						 dev);
	struct max9635_data *als_data = i2c_get_clientdata(client);
	unsigned i, n, reg_count;
	u8 als_reg[2];

	reg_count = sizeof(max9635_regs) / sizeof(max9635_regs[0]);
	for (i = 0, n = 0; i < reg_count; i++) {
		als_reg[0] = (AUTO_INCREMENT | max9635_regs[i].reg);
		max9635_read_reg(als_data, als_reg, 1);
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "%-20s = 0x%02X\n",
			       max9635_regs[i].name, als_reg[0]);
	}

	return n;
}

static ssize_t max9635_registers_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct i2c_client *client = container_of(dev, struct i2c_client,
						 dev);
	struct max9635_data *als_data = i2c_get_clientdata(client);
	unsigned i, reg_count, value;
	int error;
	u8 als_reg[2];
	char name[30];

	if (count >= 30) {
		pr_err("%s:input too long\n", __func__);
		return -1;
	}

	if (sscanf(buf, "%s %x", name, &value) != 2) {
		pr_err("%s:unable to parse input\n", __func__);
		return -1;
	}

	reg_count = sizeof(max9635_regs) / sizeof(max9635_regs[0]);
	for (i = 0; i < reg_count; i++) {
		if (!strcmp(name, max9635_regs[i].name)) {
			als_reg[0] = (AUTO_INCREMENT | max9635_regs[i].reg);
			als_reg[1] = value;
			error = max9635_write_reg(als_data, als_reg, 1);
			if (error) {
				pr_err("%s:Failed to write register %s\n",
				       __func__, name);
				return -1;
			}
			return count;
		}
	}
	if (!strcmp("Go", name)) {
		max9635_enable(als_data);
		return 0;
	}
	if (!strcmp("Stop", name)) {
		max9635_disable(als_data);
		return 0;
	}
	pr_err("%s:no such register %s\n", __func__, name);
	return -1;
}

static DEVICE_ATTR(registers, 0644, max9635_registers_show,
		   max9635_registers_store);
#endif

static void max9635_work_queue(struct work_struct *work)
{
	struct max9635_data *als_data =
		container_of((struct delayed_work *)work, struct max9635_data,
			     working_queue);

	max9635_report_input(als_data);
}

static int max9635_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct max9635_platform_data *pdata = client->dev.platform_data;
	struct max9635_data *als_data;
	int error = 0;

	if (pdata == NULL) {
		pr_err("%s: platform data required\n", __func__);
		return -ENODEV;
	} else if (!client->irq) {
		pr_err("%s: polling mode currently not supported\n", __func__);
		return -ENODEV;
	}
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s:I2C_FUNC_I2C not supported\n", __func__);
		return -ENODEV;
	}

	als_data = kzalloc(sizeof(struct max9635_data), GFP_KERNEL);
	if (als_data == NULL) {
		error = -ENOMEM;
		goto err_alloc_data_failed;
	}

	als_data->client = client;
	als_data->als_pdata = pdata;
	max9635_misc_data = als_data;

	als_data->idev = input_allocate_device();
	if (!als_data->idev) {
		error = -ENOMEM;
		pr_err("%s: input device allocate failed: %d\n", __func__,
		       error);
		goto error_input_allocate_failed;
	}

	als_data->idev->name = "max9635_als";
	input_set_capability(als_data->idev, EV_MSC, MSC_RAW);

	error = misc_register(&max9635_misc_device);
	if (error < 0) {
		pr_err("%s: max9635 register failed\n", __func__);
		goto error_misc_register_failed;
	}

	atomic_set(&als_data->enabled, 0);

	INIT_DELAYED_WORK(&als_data->working_queue, max9635_work_queue);

	error = input_register_device(als_data->idev);
	if (error) {
		pr_err("%s: input device register failed:%d\n", __func__,
		       error);
		goto error_input_register_failed;
	}

	error = max9635_init_registers(als_data);
	if (error < 0) {
		pr_err("%s: Register Initialization failed: %d\n",
		       __func__, error);
		error = -ENODEV;
		goto err_reg_init_failed;
	}

	spin_lock_init(&als_data->irq_lock);
	als_data->cur_irq_state = 1;

	error = request_irq(als_data->client->irq, max9635_irq_handler,
			    IRQF_TRIGGER_FALLING, MAX9635_NAME, als_data);
	if (error != 0) {
		pr_err("%s: irq request failed: %d\n", __func__, error);
		error = -ENODEV;
		goto err_req_irq_failed;
	}

	i2c_set_clientdata(client, als_data);

	als_data->regulator = regulator_get(&client->dev, "vio");

#ifdef DEBUG
	error = device_create_file(&als_data->client->dev, &dev_attr_registers);
	if (error < 0) {
		pr_err("%s:File device creation failed: %d\n", __func__, error);
		error = -ENODEV;
		goto err_create_registers_file_failed;
	}
#endif

	return 0;

#ifdef DEBUG
err_create_registers_file_failed:
	free_irq(als_data->client->irq, als_data);
#endif
err_req_irq_failed:
err_reg_init_failed:
	input_unregister_device(als_data->idev);
error_input_register_failed:
	misc_deregister(&max9635_misc_device);
error_misc_register_failed:
	input_free_device(als_data->idev);
error_input_allocate_failed:
	kfree(als_data);
err_alloc_data_failed:
	return error;
}

static int max9635_remove(struct i2c_client *client)
{
	struct max9635_data *als_data = i2c_get_clientdata(client);
#ifdef DEBUG
	device_remove_file(&als_data->client->dev, &dev_attr_registers);
#endif
	if (!IS_ERR_OR_NULL(als_data->regulator))
		regulator_put(als_data->regulator);
	free_irq(als_data->client->irq, als_data);
	input_unregister_device(als_data->idev);
	input_free_device(als_data->idev);
	misc_deregister(&max9635_misc_device);
	kfree(als_data);
	return 0;
}

static int max9635_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct max9635_data *als_data = i2c_get_clientdata(client);

	if (max9635_debug)
		pr_info("%s: Suspending\n", __func__);

	max9635_irq_enable(als_data, 0);
	cancel_delayed_work_sync(&als_data->working_queue);

	return 0;
}

static int max9635_resume(struct i2c_client *client)
{
	struct max9635_data *als_data = i2c_get_clientdata(client);

	if (max9635_debug)
		pr_info("%s: Resuming\n", __func__);

	max9635_irq_enable(als_data, 1);

	return 0;
}

static const struct i2c_device_id max9635_id[] = {
	{MAX9635_NAME, 0},
	{}
};

static struct i2c_driver max9635_i2c_driver = {
	.probe = max9635_probe,
	.remove = max9635_remove,
	.suspend = max9635_suspend,
	.resume = max9635_resume,
	.id_table = max9635_id,
	.driver = {
	   .name = MAX9635_NAME,
	   .owner = THIS_MODULE,
	},
};

static int __init max9635_init(void)
{
	return i2c_add_driver(&max9635_i2c_driver);
}

static void __exit max9635_exit(void)
{
	i2c_del_driver(&max9635_i2c_driver);
}

module_init(max9635_init);
module_exit(max9635_exit);

MODULE_DESCRIPTION("ALS driver for Maxim 9635");
MODULE_AUTHOR("Dan Murphy <D.Murphy@motorola.com>");
MODULE_LICENSE("GPL");
