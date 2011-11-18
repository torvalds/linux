/* drivers/input/misc/isl29028.c
 *
 * Copyright (C) 2010 ROCK-CHIPS, Inc.
 * Author: eric <linjh@rock-chips.com>
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/circ_buf.h>
#include <linux/interrupt.h>
#include "isl29028.h"
#include <linux/slab.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <linux/delay.h>
#include <linux/wait.h>

#define ISL_DBG
#undef ISL_DBG

#ifdef ISL_DBG
 #define D(x...) printk(x)
#else
 #define D(x...)
#endif

#define ISL_REG_CONFIG    (0x01)
#define PROX_EN           (1 << 7)
#define PROX_SLP(x)       (x << 4)
#define PROX_DR_110       (0 << 3)
#define PROX_DR_220       (1 << 3)
#define ALS_EN            (1 << 2)
#define ALS_RANGE_H       (1 << 1)
#define ALS_RANGE_L       (0 << 1)
#define ALS_MODE          (1 << 0)

#define ISL_REG_INT   (0x02)
#define PROX_FLAG_MASK ~(1 << 7)
#define LS_FLAG_MASK ~(1 << 3)
#define PROX_PRST(x)  (x << 5)
#define ALS_PRST(x)   (x << 1)
#define INT_AND       (1 << 0)
#define INT_OR        (0 << 0)

#define ISL_REG_PROX_LT       (0x03)
#define ISL_REG_PROX_HT       (0x04)
#define PROX_LT               (0x90)
#define PROX_HT               (0xA0)

#define ISL_REG_PROX_DATA     (0x08)
#define ISL_REG_ALSIR_LDATA	  (0x09)
#define ISL_REG_ALSIR_HDATA	  (0x0a)


#define ISL_REG_ALSIR_TH1	(0x05)
#define ISL_REG_ALSIR_TH2	(0x06)
#define ISL_REG_ALSIR_TH3	(0x07)

struct isl29028_data {
	struct input_dev         *psensor_input_dev;
	struct input_dev         *lsensor_input_dev;
	struct i2c_client        *client;
	struct delayed_work      p_work; //for psensor
	struct delayed_work 	l_work;	//for light sensor
	struct mutex lock;
	int enabled;
	int irq;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend isl29028_early_suspend;
#endif

static irqreturn_t isl29028_psensor_irq_handler(int irq, void *data);


int g_lightlevel = 8;

static const int luxValues[8] = {
		10, 160, 225, 320,
		640, 1280, 2600, 4095
};


static int isl29028_read_reg(struct i2c_client *client, char reg, char *value)
{
	int ret = 0;
	struct i2c_msg msg[2];
	struct i2c_adapter *adap = client->adapter;

	msg[0].addr  = client->addr;
	msg[0].flags = client->flags;
	msg[0].len = 1;
	msg[0].buf = (char *)&reg;
	msg[0].scl_rate = 400 * 1000;

	msg[1].addr  = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = (char *)value;
	msg[1].scl_rate = 400 * 1000;

	if ((ret = i2c_transfer(adap, (struct i2c_msg *)&msg, 2)) < 2) {
		D("%s: read isl29028 register  %#x failure\n", __FUNCTION__, reg);
		return -EIO;
	}

	return 1;
}

static int isl29028_write_reg(struct i2c_client *client, char reg, char value)
{
	int ret = 0;
	char buf[2];
	struct i2c_msg msg;
	struct i2c_adapter *adap = client->adapter;

	buf[0] = reg;
	buf[1] = value;

	msg.addr  = client->addr;
	msg.flags = client->flags;
	msg.len = 2;
	msg.buf = (char *)&buf;
	msg.scl_rate = 400 * 1000;


	if ((ret = i2c_transfer(adap, (struct i2c_msg *)&msg, 1)) < 1) {
		D("%s: read isl29028 register  %#x failure\n", __FUNCTION__, reg);
		return -EIO;
	}

	return 1;
}

static void isl29028_psensor_work_handler(struct work_struct *work)
{
#if 1
	struct isl29028_data *isl = (struct isl29028_data *)container_of(work, struct isl29028_data, p_work.work);
	int rc;

	if (gpio_get_value(isl->client->irq)) {
		D("line %d, input_report_abs 0 \n", __LINE__);
		input_report_abs(isl->psensor_input_dev, ABS_DISTANCE, 1);
		input_sync(isl->psensor_input_dev);
		free_irq(isl->irq, (void *)isl);
		rc = request_irq(isl->irq, isl29028_psensor_irq_handler,
						IRQ_TYPE_EDGE_FALLING, isl->client->name, (void *)isl);
		if (rc < 0) {
			dev_err(&(isl->client->dev),"request_irq failed for gpio %d (%d)\n", isl->client->irq, rc);
		}
	}
	else {
		D("line %d, input_report_abs 0 \n", __LINE__);
		input_report_abs(isl->psensor_input_dev, ABS_DISTANCE, 0);
		input_sync(isl->psensor_input_dev);
		free_irq(isl->irq, (void *)isl);
		rc = request_irq(isl->irq, isl29028_psensor_irq_handler,
						IRQ_TYPE_EDGE_RISING, isl->client->name, (void *)isl);
		if (rc < 0) {
			dev_err(&(isl->client->dev),"request_irq failed for gpio %d (%d)\n", isl->client->irq, rc);
		}
	}

	return ;
#else
	struct isl29028_data *isl = (struct isl29028_data *)container_of(work, struct isl29028_data, p_work.work);
	char reg, value, int_flag;

	mutex_lock(&isl->lock);
	reg = ISL_REG_INT;
	isl29028_read_reg(isl->client, reg, (char *)&int_flag);

	if (!(int_flag >> 7)) {
		D("line %d: input_report_abs 1 \n", __LINE__);
		input_report_abs(isl->psensor_input_dev, ABS_DISTANCE, 1);
		input_sync(isl->psensor_input_dev);
	}

	if (int_flag & 0x08) {
		D("line %d; light sensor interrupt\n", __LINE__);
		isl29028_write_reg(isl->client, reg, int_flag & 0xf7);		
	}

	reg = ISL_REG_PROX_DATA;
	isl29028_read_reg(isl->client, reg, (char *)&value);
	//D("%s: int is %#x\n", __FUNCTION__, int_flag);
	D("%s: prox_int is %d\n", __FUNCTION__, (int_flag >> 7 ));
	D("%s: prox_data is %#x\n", __FUNCTION__, value);
	mutex_unlock(&isl->lock);

	enable_irq(isl->irq);

#endif
}

static irqreturn_t isl29028_psensor_irq_handler(int irq, void *data)
{
#if 1
	struct isl29028_data *isl = (struct isl29028_data *)data;
	//disable_irq_nosync(isl->irq);
	schedule_delayed_work(&isl->p_work, msecs_to_jiffies(420));
	return IRQ_HANDLED;
#else
	struct isl29028_data *isl = (struct isl29028_data *)data;
	D("line %d, input_report_abs 0 \n", __LINE__);
	input_report_abs(isl->psensor_input_dev, ABS_DISTANCE, 0);
	input_sync(isl->psensor_input_dev);

	disable_irq_nosync(isl->irq);
	schedule_delayed_work(&isl->p_work, msecs_to_jiffies(420));

	return IRQ_HANDLED;
#endif
}

static int isl29028_psensor_enable(struct i2c_client *client)
{
	char reg, value, int_flag;
	int ret;
	struct isl29028_data *isl = (struct isl29028_data *)i2c_get_clientdata(client);

	printk("line %d: enter func %s\n", __LINE__, __FUNCTION__);

	mutex_lock(&isl->lock);
	reg = ISL_REG_CONFIG;
	ret = isl29028_read_reg(client, reg, &value);
	value |= PROX_EN;
	ret = isl29028_write_reg(client, reg, value);
#ifdef ISL_DBG
	ret = isl29028_read_reg(client, reg, &value);
	D("%s: configure reg value %#x ...\n", __FUNCTION__, value);	
#endif

	reg = ISL_REG_INT;
	isl29028_read_reg(isl->client, reg, (char *)&int_flag);
	if (!(int_flag >> 7)) {
		printk("line %d: input_report_abs 1 \n", __LINE__);
		input_report_abs(isl->psensor_input_dev, ABS_DISTANCE, 1);
		input_sync(isl->psensor_input_dev);
	}
	mutex_unlock(&isl->lock);

	//enable_irq(isl->irq);

	return ret;
}

static int isl29028_psensor_disable(struct i2c_client *client)
{
	char ret, reg, reg2, value, value2;
	struct isl29028_data *isl = (struct isl29028_data *)i2c_get_clientdata(client);
	printk("line %d: enter func %s\n", __LINE__, __FUNCTION__);

	//disable_irq_nosync(isl->irq);

	mutex_lock(&isl->lock);

	reg = ISL_REG_CONFIG;
	ret = isl29028_read_reg(client, reg, &value);
	value &= ~PROX_EN;
	ret = isl29028_write_reg(client, reg, value);

	reg2 = ISL_REG_INT;
	ret = isl29028_read_reg(client, reg2, &value2);
	value2 &= PROX_FLAG_MASK;
	ret = isl29028_write_reg(client, reg2, value2);

#ifdef ISL_DBG
	ret = isl29028_read_reg(client, reg, &value);
	ret = isl29028_read_reg(client, reg2, &value2);
	D("%s: configure reg value %#x ...\n", __FUNCTION__, value);	
	D("%s: interrupt reg value %#x ...\n", __FUNCTION__, value2);	
#endif
	mutex_unlock(&isl->lock);

	//disable_irq(isl->irq);
	cancel_delayed_work_sync(&isl->p_work);
	//enable_irq(isl->irq);

	return ret;
}

static int isl29028_psensor_open(struct inode *inode, struct file *file);
static int isl29028_psensor_release(struct inode *inode, struct file *file);
static long isl29028_psensor_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static int misc_ps_opened = 0;
static struct file_operations isl29028_psensor_fops = {
	.owner = THIS_MODULE,
	.open = isl29028_psensor_open,
	.release = isl29028_psensor_release,
	.unlocked_ioctl = isl29028_psensor_ioctl
};

static struct miscdevice isl29028_psensor_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "psensor",
	.fops = &isl29028_psensor_fops
};

static int isl29028_psensor_open(struct inode *inode, struct file *file)
{
//	struct i2c_client *client = 
//		       container_of (isl29028_psensor_misc.parent, struct i2c_client, dev);
	D("%s\n", __func__);
	if (misc_ps_opened)
		return -EBUSY;
	misc_ps_opened = 1;
	return 0;
	//return isl29028_psensor_enable(client);
}

static int isl29028_psensor_release(struct inode *inode, struct file *file)
{	
//	struct i2c_client *client = 
//		       container_of (isl29028_psensor_misc.parent, struct i2c_client, dev);
	D("%s\n", __func__);
	misc_ps_opened = 0;
	return 0;
	//return isl29028_psensor_disable(client);
}

static long isl29028_psensor_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	char reg, val, enabled;
	struct i2c_client *client = 
		       container_of (isl29028_psensor_misc.parent, struct i2c_client, dev);
	struct isl29028_data *isl = (struct isl29028_data *)i2c_get_clientdata(client);

	D("%s cmd %d\n", __func__, _IOC_NR(cmd));
	switch (cmd) {
	case PSENSOR_IOCTL_ENABLE:
		if (get_user(val, (unsigned long __user *)arg))
			return -EFAULT;
		if (val)
			return isl29028_psensor_enable(client);
		else
			return isl29028_psensor_disable(client);
		break;
	case PSENSOR_IOCTL_GET_ENABLED:
		mutex_lock(&isl->lock);
		reg = ISL_REG_CONFIG;
		isl29028_read_reg(client, reg, &val);
		enabled = (val & (1 << 7)) ? 1 : 0;
		mutex_unlock(&isl->lock);
		return put_user(enabled, (unsigned long __user *)arg);
		break;
	default:
		pr_err("%s: invalid cmd %d\n", __func__, _IOC_NR(cmd));
		return -EINVAL;
	}
}

static int register_psensor_device(struct i2c_client *client, struct isl29028_data *isl)
{
	struct input_dev *input_dev;
	int rc;

	D("%s: allocating input device\n", __func__);
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev,"%s: could not allocate input device for psensor\n", __FUNCTION__);
		rc = -ENOMEM;
		goto done;
	}
	
	isl->psensor_input_dev = input_dev;
	input_set_drvdata(input_dev, isl);

	input_dev->name = "proximity";

	set_bit(EV_ABS, input_dev->evbit);
	input_set_abs_params(input_dev, ABS_DISTANCE, 0, 1, 0, 0);

	D("%s: registering input device\n", __func__);
	rc = input_register_device(input_dev);
	if (rc < 0) {
		pr_err("%s: could not register input device for psensor\n", __func__);
		goto err_free_input_device;
	}

	D("%s: registering misc device for psensor\n", __func__);
	rc = misc_register(&isl29028_psensor_misc);
	if (rc < 0) {
		pr_err("%s: could not register misc device\n", __func__);
		goto err_unregister_input_device;
	}
	
	isl29028_psensor_misc.parent = &client->dev;
	//misc_deregister(&isl29028_psensor_misc);
	
	INIT_DELAYED_WORK(&isl->p_work, isl29028_psensor_work_handler);

	rc = gpio_request(client->irq, "isl29028 irq");
	if (rc) {
		pr_err("%s: request gpio %d failed \n", __func__, client->irq);
		goto err_unregister_misc;
	}
	rc = gpio_direction_input(client->irq);
	if (rc) {
		pr_err("%s: failed set gpio input\n", __FUNCTION__);
	}

	gpio_pull_updown(client->irq, GPIOPullUp);
	isl->irq = gpio_to_irq(client->irq);
	//mdelay(1);
	rc = request_irq(isl->irq, isl29028_psensor_irq_handler,
					IRQ_TYPE_EDGE_FALLING, client->name, (void *)isl);
	if (rc < 0) {
		dev_err(&client->dev,"request_irq failed for gpio %d (%d)\n", client->irq, rc);
		goto err_free_gpio;
	}

	//disable_irq_nosync(isl->irq);
	
	return 0;

err_free_gpio:
	gpio_free(client->irq);
err_unregister_misc:
	misc_deregister(&isl29028_psensor_misc);
err_unregister_input_device:
	input_unregister_device(input_dev);
err_free_input_device:
	input_free_device(input_dev);
done:
	return rc;
}

static void unregister_psensor_device(struct i2c_client *client, struct isl29028_data *isl)
{
	misc_deregister(&isl29028_psensor_misc);
	input_unregister_device(isl->psensor_input_dev);
	input_free_device(isl->psensor_input_dev);
}

#define LSENSOR_POLL_PROMESHUTOK   1000

static int isl29028_lsensor_enable(struct i2c_client *client)
{
	char reg, value;
	int ret;
	struct isl29028_data *isl = (struct isl29028_data *)i2c_get_clientdata(client);

	mutex_lock(&isl->lock);

	reg = ISL_REG_CONFIG;
	ret = isl29028_read_reg(client, reg, &value);
	value |= ALS_EN;
	ret = isl29028_write_reg(client, reg, value);
	
#ifdef ISL_DBG
	ret = isl29028_read_reg(client, reg, &value);
	D("%s: configure reg value %#x ...\n", __FUNCTION__, value);	
#endif

	mutex_unlock(&isl->lock);

	schedule_delayed_work(&(isl->l_work), msecs_to_jiffies(LSENSOR_POLL_PROMESHUTOK));

	return ret;
}

static int isl29028_lsensor_disable(struct i2c_client *client)
{
	char ret, reg, reg2, value, value2;
	struct isl29028_data *isl = (struct isl29028_data *)i2c_get_clientdata(client);

	cancel_delayed_work_sync(&(isl->l_work));

	mutex_lock(&isl->lock);

	reg = ISL_REG_CONFIG;
	ret = isl29028_read_reg(client, reg, &value);
	value &= ~ALS_EN;
	ret = isl29028_write_reg(client, reg, value);

	reg2 = ISL_REG_INT;
	ret = isl29028_read_reg(client, reg2, &value2);
	value2 &= LS_FLAG_MASK;
	ret = isl29028_write_reg(client, reg2, value2);

#ifdef ISL_DBG
	ret = isl29028_read_reg(client, reg, &value);
	ret = isl29028_read_reg(client, reg2, &value2);
	D("%s: configure reg value %#x ...\n", __FUNCTION__, value);	
	D("%s: interrupt reg value %#x ...\n", __FUNCTION__, value2);	
#endif

	mutex_unlock(&isl->lock);

	return ret;
}

static int luxValue_to_level(int value)
{
	int i;
	if (value >= luxValues[7])
		return 7;
	if (value <= luxValues[0])
		return 0;
	for (i=0;i<7;i++)
		if (value>=luxValues[i] && value<luxValues[i+1])
			return i;
	return -1;
}

static void isl29028_lsensor_work_handler(struct work_struct *work)
{
	struct isl29028_data *isl = (struct isl29028_data *)container_of(work, struct isl29028_data, l_work.work);
	char reg, l_value, h_value;
	unsigned int als_value;
	int level;

	mutex_lock(&isl->lock);
	reg = ISL_REG_ALSIR_LDATA;
	isl29028_read_reg(isl->client, reg, (char *)&l_value);

	reg = ISL_REG_ALSIR_HDATA;
	isl29028_read_reg(isl->client, reg, (char *)&h_value);

	mutex_unlock(&isl->lock);

	als_value = h_value;
	als_value = (als_value << 8) | l_value;

#ifdef ISL_DBG 
	D("%s: ls_data is %#x\n", __FUNCTION__, als_value);
#endif

	level = luxValue_to_level(als_value);

#ifdef ISL_DBG 
	D("%s: ls_level is %d\n", __FUNCTION__, level);
#endif

	if (level != g_lightlevel) {
		g_lightlevel = level;
		input_report_abs(isl->lsensor_input_dev, ABS_MISC, level);
		input_sync(isl->lsensor_input_dev);
	}
	schedule_delayed_work(&(isl->l_work), msecs_to_jiffies(LSENSOR_POLL_PROMESHUTOK));
}

static int isl29028_lsensor_open(struct inode *inode, struct file *file);
static int isl29028_lsensor_release(struct inode *inode, struct file *file);
static long isl29028_lsensor_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static int misc_ls_opened = 0;
static struct file_operations isl29028_lsensor_fops = {
	.owner = THIS_MODULE,
	.open = isl29028_lsensor_open,
	.release = isl29028_lsensor_release,
	.unlocked_ioctl = isl29028_lsensor_ioctl
};

static struct miscdevice isl29028_lsensor_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lightsensor",
	.fops = &isl29028_lsensor_fops
};

static int isl29028_lsensor_open(struct inode *inode, struct file *file)
{
//	struct i2c_client *client = 
//		       container_of (isl29028_lsensor_misc.parent, struct i2c_client, dev);
	D("%s\n", __func__);
	if (misc_ls_opened)
		return -EBUSY;
	misc_ls_opened = 1;
	return 0;
	//return isl29028_lsensor_enable(client);
}

static int isl29028_lsensor_release(struct inode *inode, struct file *file)
{
	
//	struct i2c_client *client = 
//		       container_of (isl29028_lsensor_misc.parent, struct i2c_client, dev);
	D("%s\n", __func__);
	misc_ls_opened = 0;
	return 0;
	//return isl29028_lsensor_disable(client);
}

static long isl29028_lsensor_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	char reg, val, enabled;
	struct i2c_client *client = 
		       container_of (isl29028_lsensor_misc.parent, struct i2c_client, dev);
	struct isl29028_data *isl = (struct isl29028_data *)i2c_get_clientdata(client);

	D("%s cmd %d\n", __func__, _IOC_NR(cmd));
	switch (cmd) {
	case LIGHTSENSOR_IOCTL_ENABLE:
		if (get_user(val, (unsigned long __user *)arg))
			return -EFAULT;
		if (val)
			return isl29028_lsensor_enable(client);
		else
			return isl29028_lsensor_disable(client);
		break;
	case LIGHTSENSOR_IOCTL_GET_ENABLED:
		mutex_lock(&isl->lock);
		reg = ISL_REG_CONFIG;
		isl29028_read_reg(client, reg, &val);
		mutex_unlock(&isl->lock);
		enabled = (val & (1 << 2)) ? 1 : 0;
		return put_user(enabled, (unsigned long __user *)arg);
		break;
	default:
		pr_err("%s: invalid cmd %d\n", __func__, _IOC_NR(cmd));
		return -EINVAL;
	}
}

static int register_lsensor_device(struct i2c_client *client, struct isl29028_data *isl)
{
	struct input_dev *input_dev;
	int rc;

	D("%s: allocating input device\n", __func__);
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev,"%s: could not allocate input device forlsensor\n", __FUNCTION__);
		rc = -ENOMEM;
		goto done;
	}
	
	isl->lsensor_input_dev = input_dev;
	input_set_drvdata(input_dev, isl);

	input_dev->name = "lightsensor-level";

	set_bit(EV_ABS, input_dev->evbit);
	input_set_abs_params(input_dev, ABS_MISC, 0, 8, 0, 0);

	D("%s: registering input device\n", __func__);
	rc = input_register_device(input_dev);
	if (rc < 0) {
		pr_err("%s: could not register input device for lsensor\n", __func__);
		goto err_free_input_device;
	}

	D("%s: registering misc device for lsensor\n", __func__);
	rc = misc_register(&isl29028_lsensor_misc);
	if (rc < 0) {
		pr_err("%s: could not register misc device\n", __func__);
		goto err_unregister_input_device;
	}
	
	isl29028_lsensor_misc.parent = &client->dev;

	INIT_DELAYED_WORK(&isl->l_work, isl29028_lsensor_work_handler);
	
	return 0;

err_unregister_input_device:
	input_unregister_device(input_dev);
err_free_input_device:
	input_free_device(input_dev);
done:
	return rc;
}

static void unregister_lsensor_device(struct i2c_client *client, struct isl29028_data *isl)
{
	misc_deregister(&isl29028_lsensor_misc);
	input_unregister_device(isl->lsensor_input_dev);
	input_free_device(isl->lsensor_input_dev);
}

static int isl29028_config(struct i2c_client *client)
{
	int ret;
	char value, buf[2];
	struct isl29028_data *isl = (struct isl29028_data *)i2c_get_clientdata(client);

	D("%s: init isl29028 all register\n", __func__);

	mutex_lock(&isl->lock);

	/*********************** power on **************************/
	buf[0] = 0x0e;
	buf[1] = 0x00; 
	if ((ret = i2c_master_send(client, buf, 2)) < 2) {
		printk("%s: config isl29028 register %#x err %d\n", __FUNCTION__, buf[0], ret);
	}
	
	buf[0] = 0x0f;
	buf[1] = 0x00; 
	if ((ret = i2c_master_send(client, buf, 2)) < 2) {
		printk("%s: config isl29028 register %#x err %d\n", __FUNCTION__, buf[0], ret);
	}

	buf[0] = ISL_REG_CONFIG;
	buf[1] = 0x00; 
	if ((ret = i2c_master_send(client, buf, 2)) < 2) {
		printk("%s: config isl29028 register %#x err %d\n", __FUNCTION__, buf[0], ret);
	}

	if (ret < 2) {
		mutex_unlock(&isl->lock);
		return -1;
	}
	mdelay(2);
	
    /***********************config**************************/

	buf[0] = ISL_REG_CONFIG;
	buf[1] = /*PROX_EN | */PROX_SLP(4) | PROX_DR_220 | ALS_RANGE_H /*| ALS_EN */; 
	if ((ret = i2c_master_send(client, buf, 2)) < 2) {
		printk("%s: config isl29028 register %#x err %d\n", __FUNCTION__, buf[0], ret);
	}
#ifdef ISL_DBG
	isl29028_read_reg(client, 0x01, &value);
	printk("%s: config isl29028 CONFIGURE(0x01) reg %#x \n", __FUNCTION__, value);
#endif

	buf[0] = ISL_REG_INT;
	buf[1] = PROX_PRST(1) | ALS_PRST(3); 
	if ((ret = i2c_master_send(client, buf, 2)) < 2) {
		printk("%s: config isl29028 register %#x err %d\n", __FUNCTION__, buf[0], ret);
	}
#ifdef ISL_DBG
	isl29028_read_reg(client, 0x02, &value);
	printk("%s: config isl29028 INTERRUPT(0x02) reg %#x \n", __FUNCTION__, value);
#endif

	buf[0] = ISL_REG_PROX_LT;
	buf[1] = PROX_LT; 
	if ((ret = i2c_master_send(client, buf, 2)) < 2) {
		printk("%s: config isl29028 register %#x err %d\n", __FUNCTION__, buf[0], ret);
	}
#ifdef ISL_DBG
	isl29028_read_reg(client, 0x03, &value);
	printk("%s: config isl29028 PROX_LT(0x03) reg %#x \n", __FUNCTION__, value);
#endif

	buf[0] = ISL_REG_PROX_HT;
	buf[1] = PROX_HT; 
	if ((ret = i2c_master_send(client, buf, 2)) < 2) {
		printk("%s: config isl29028 register %#x err %d\n", __FUNCTION__, buf[0], ret);
	}
#ifdef ISL_DBG
	isl29028_read_reg(client, 0x04, &value);
	printk("%s: config isl29028 PROX_HT(0x04) reg %#x \n", __FUNCTION__, value);
#endif

	buf[0] = ISL_REG_ALSIR_TH1;
	buf[1] = 0x0;
	if ((ret = i2c_master_send(client, buf, 2)) < 2) {
		printk("%s: config isl29028 register %#x err %d\n", __FUNCTION__, buf[0], ret);
	}

	buf[0] = ISL_REG_ALSIR_TH2;
	buf[1] = 0xF0;
	if ((ret = i2c_master_send(client, buf, 2)) < 2) {
		printk("%s: config isl29028 register %#x err %d\n", __FUNCTION__, buf[0], ret);
	}
	
	buf[0] = ISL_REG_ALSIR_TH3;
	buf[1] = 0xFF;
	if ((ret = i2c_master_send(client, buf, 2)) < 2) {
		printk("%s: config isl29028 register %#x err %d\n", __FUNCTION__, buf[0], ret);
	}
	
	mutex_unlock(&isl->lock);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void isl29028_suspend(struct early_suspend *h)
{
	struct i2c_client *client = container_of(isl29028_psensor_misc.parent, struct i2c_client, dev);
	struct isl29028_data *isl = (struct isl29028_data *)i2c_get_clientdata(client);
	
	D("isl29028 early suspend ========================= \n");
	if (misc_ps_opened)
		enable_irq_wake(isl->irq);
//		isl29028_psensor_disable(client);	
	if (misc_ls_opened)
		isl29028_lsensor_disable(client);	
}

static void isl29028_resume(struct early_suspend *h)
{
	struct i2c_client *client = container_of(isl29028_psensor_misc.parent, struct i2c_client, dev);
	struct isl29028_data *isl = (struct isl29028_data *)i2c_get_clientdata(client);
	
    D("isl29028 early resume ======================== \n"); 
	if (misc_ps_opened)
		disable_irq_wake(isl->irq);
//		isl29028_psensor_enable(client);	
	if (misc_ls_opened)
		isl29028_lsensor_enable(client);	
}
#else
#define isl29028_suspend NULL
#define isl29028_resume NULL
#endif

static int isl29028_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int rc = -EIO;
	struct isl29028_data *isl;

	D("\n%s: isl29028 i2c client probe\n\n", __func__);

	isl = kzalloc(sizeof(struct isl29028_data), GFP_KERNEL);
	if (isl == NULL) {
		rc = -ENOMEM;
		dev_err(&client->dev, "failed to allocate driver data\n");
		goto done;
	}

	isl->client = client;
	i2c_set_clientdata(client, isl);

	
	mutex_init(&isl->lock);

	rc = register_psensor_device(client, isl);
	if (rc) {
		dev_err(&client->dev, "failed to register_psensor_device\n");
		goto err_free_mem;
	}
	
	rc = register_lsensor_device(client, isl);
	if (rc) {
		dev_err(&client->dev, "failed to register_lsensor_device\n");
		goto unregister_device1;
	}

	rc = isl29028_config(client);
	if (rc) {
		dev_err(&client->dev, "failed to isl29028_config\n");
		goto unregister_device2;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	isl29028_early_suspend.suspend = isl29028_suspend;
	isl29028_early_suspend.resume  = isl29028_resume;
	isl29028_early_suspend.level   = 0x02;
	register_early_suspend(&isl29028_early_suspend);
#endif

	//isl29028_psensor_enable(client);
	//isl29028_lsensor_enable(client);

	return 0;

unregister_device2:
	unregister_lsensor_device(client, isl);
unregister_device1:
	unregister_psensor_device(client, isl);
err_free_mem:
	kfree(isl);
done:
	return rc;
}

static int isl29028_remove(struct i2c_client *client)
{
	struct isl29028_data *isl29028 = i2c_get_clientdata(client);
	
	unregister_lsensor_device(client, isl29028);
	unregister_psensor_device(client, isl29028);
    kfree(isl29028); 
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&isl29028_early_suspend);
#endif      
	return 0;
}

static const struct i2c_device_id isl29028_id[] = {
		{"isl29028", 0},
		{ }
};

static struct i2c_driver isl29028_driver = {
	.driver = {
		.name = "isl29028",
	},
	.probe    = isl29028_probe,
	.remove   = isl29028_remove,
	.id_table = isl29028_id,

};

static int __init isl29028_init(void)
{

	return i2c_add_driver(&isl29028_driver);
}

static void __exit isl29028_exit(void)
{
	return i2c_del_driver(&isl29028_driver);
}

module_init(isl29028_init);
module_exit(isl29028_exit);

