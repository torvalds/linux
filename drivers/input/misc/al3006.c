/* drivers/input/misc/al3006.c
 *
 * Copyright (C) 2010 ROCK-CHIPS, Inc.
 * Author: eric <hc@rock-chips.com>
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
#include "al3006.h"

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <linux/delay.h>
#include <linux/wait.h>

#define AL3006_DBG 0

#if AL3006_DBG
#define AL3006_DEBUG(x...) printk(x)
#else
#define AL3006_DEBUG(x...)
#endif

#define CONFIG_REG        (0x00)
#define TIM_CTL_REG       (0x01)
#define ALS_CTL_REG       (0x02)
#define INT_STATUS_REG    (0x03)
#define PS_CTL_REG        (0x04)
#define PS_ALS_DATA_REG   (0x05)
#define ALS_WINDOWS_REG   (0x08)

//enable bit[ 0-1], in register CONFIG_REG
#define ONLY_ALS_EN       (0x00)
#define ONLY_PROX_EN      (0x01)
#define ALL_PROX_ALS_EN   (0x02)
#define ALL_IDLE          (0x03)

#define POWER_MODE_MASK   (0x0C)
#define POWER_UP_MODE     (0x00)
#define POWER_DOWN_MODE   (0x08)
#define POWER_RESET_MODE  (0x0C)

struct al3006_data {
	struct input_dev         *psensor_input_dev;
	struct input_dev         *lsensor_input_dev;
	struct i2c_client        *client;
	struct delayed_work      dwork; //for l/psensor
	//struct delayed_work 	 l_work; //for light sensor
	struct mutex lock;
	spinlock_t work_lock;
	int enabled;
	int irq;
};
static struct al3006_data al3006_struct_data;

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend al3006_early_suspend;
#endif

int g_lightlevel = 8;

static const int luxValues[8] = {
		10, 160, 225, 320,
		640, 1280, 2600, 4095
};


static int al3006_read_reg(struct i2c_client *client, char reg, char *value)
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
		AL3006_DEBUG("%s: read al3006 register  %#x failure\n", __FUNCTION__, reg);
		return -EIO;
	}

	return 1;
}

static int al3006_write_reg(struct i2c_client *client, char reg, char value)
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
		AL3006_DEBUG("%s: read al3006 register  %#x failure\n", __FUNCTION__, reg);
		return -EIO;
	}

	return 1;
}

static void al3006_change_ps_threshold(struct i2c_client *client)
{
	struct al3006_data *al3006 = i2c_get_clientdata(client);
	char reg, value;

	AL3006_DEBUG("%s:\n", __FUNCTION__);
	mutex_lock(&al3006->lock);
	reg = PS_ALS_DATA_REG;
	al3006_read_reg(client, reg, &value);
	mutex_unlock(&al3006->lock);

	value >>= 7;  //bit7 is ps data ; bit7 = 1, object is detected
	printk("%s: psensor's data is %#x\n", __FUNCTION__, value);

	input_report_abs(al3006->psensor_input_dev, ABS_DISTANCE, value?0:1);
	input_sync(al3006->psensor_input_dev);
}

static void al3006_change_ls_threshold(struct i2c_client *client)
{
	struct al3006_data *al3006 = i2c_get_clientdata(client);
	char reg, value;

	AL3006_DEBUG("%s:\n", __FUNCTION__);
	mutex_lock(&al3006->lock);
	reg = PS_ALS_DATA_REG;
	al3006_read_reg(client, reg, &value);
	mutex_unlock(&al3006->lock);

	value &= 0x3F; // bit0-5  is ls data;
	printk("%s: lightsensor's level is %#x\n", __FUNCTION__, value);

	if(value > 8) value = 8;
	input_report_abs(al3006->lsensor_input_dev, ABS_MISC, value);
	input_sync(al3006->lsensor_input_dev);
}

static void al3006_work_handler(struct work_struct *work)
{
	struct al3006_data *al3006 = (struct al3006_data *)container_of(work, struct al3006_data, dwork.work);
	char reg, value;

	mutex_lock(&al3006->lock);
	reg = INT_STATUS_REG;
	al3006_read_reg(al3006->client, reg, &value);
	mutex_unlock(&al3006->lock);
	AL3006_DEBUG("%s: INT_STATUS_REG is %#x\n", __FUNCTION__, value);

	value &= 0x03;
	if(value == 0x02) {        //ps int
		al3006_change_ps_threshold(al3006->client);
	}
	else if(value == 0x01) {   //ls int
		al3006_change_ls_threshold(al3006->client);
	}
	else if(value == 0x03) {   //ps and ls int
		al3006_change_ps_threshold(al3006->client);
		al3006_change_ls_threshold(al3006->client);
	}
	//enable_irq(al3006->irq);
}

static void al3006_reschedule_work(struct al3006_data *data,
					  unsigned long delay)
{
	unsigned long flags;

	spin_lock_irqsave(&data->work_lock, flags);

	/*
	 * If work is already scheduled then subsequent schedules will not
	 * change the scheduled time that's why we have to cancel it first.
	 */
	__cancel_delayed_work(&data->dwork);
	schedule_delayed_work(&data->dwork, delay);

	spin_unlock_irqrestore(&data->work_lock, flags);
}

static irqreturn_t al3006_irq_handler(int irq, void *data)
{
	struct al3006_data *al3006 = (struct al3006_data *)data;
	AL3006_DEBUG("%s\n", __FUNCTION__);
	//input_report_abs(al3006->psensor_input_dev, ABS_DISTANCE, 0);
	//input_sync(al3006->psensor_input_dev);

	//disable_irq_nosync(al3006->irq);
	al3006_reschedule_work(al3006, 0);//msecs_to_jiffies(420)

	return IRQ_HANDLED;
}

static int al3006_psensor_enable(struct i2c_client *client)
{
	char reg, value;
	int ret;
	struct al3006_data *al3006 = (struct al3006_data *)i2c_get_clientdata(client);

       AL3006_DEBUG("%s:\n", __FUNCTION__);
	mutex_lock(&al3006->lock);
	reg = CONFIG_REG;
	ret = al3006_read_reg(client, reg, &value);
	if( (value & 0x03) == ONLY_ALS_EN ){
		value &= ~0x03;
		value |= ALL_PROX_ALS_EN;
		ret = al3006_write_reg(client, reg, value);
	}
	else if( (value & 0x03) == ALL_IDLE ){
		value &= ~0x03;
		value |= ONLY_PROX_EN;
		ret = al3006_write_reg(client, reg, value);
	}
#ifdef AL3006_DBG
	ret = al3006_read_reg(client, reg, &value);
	AL3006_DEBUG("%s: configure reg value %#x ...\n", __FUNCTION__, value);
#endif

	reg = PS_ALS_DATA_REG;
	al3006_read_reg(client, reg, &value);

	value >>= 7;  //bit7 is ps data ; bit7 = 1, object is detected
	printk("%s: psensor's data is %#x\n", __FUNCTION__, value);

	input_report_abs(al3006->psensor_input_dev, ABS_DISTANCE, value?0:1);
	input_sync(al3006->psensor_input_dev);
	mutex_unlock(&al3006->lock);

	//enable_irq(al3006->irq);

	return ret;
}

static int al3006_psensor_disable(struct i2c_client *client)
{
	char ret, reg, value;
	struct al3006_data *al3006 = (struct al3006_data *)i2c_get_clientdata(client);

	mutex_lock(&al3006->lock);
	reg = CONFIG_REG;
	ret = al3006_read_reg(client, reg, &value);
	if( (value & 0x03) == ONLY_PROX_EN ){
		value &= ~0x03;
		value |= ALL_IDLE;
		ret = al3006_write_reg(client, reg, value);
	}
	else if( (value & 0x03) == ALL_PROX_ALS_EN ){
		value &= ~0x03;
		value |= ONLY_ALS_EN;
		ret = al3006_write_reg(client, reg, value);
	}
#ifdef AL3006_DBG
	ret = al3006_read_reg(client, reg, &value);
	AL3006_DEBUG("%s: configure reg value %#x ...\n", __FUNCTION__, value);
#endif
	mutex_unlock(&al3006->lock);

	//disable_irq(al3006->irq);
	//cancel_delayed_work_sync(&al3006->dwork);
	//enable_irq(al3006->irq);

	return ret;
}

static int misc_ps_opened = 0;

static int al3006_psensor_open(struct inode *inode, struct file *file)
{
//	struct i2c_client *client =
//		       container_of (al3006_psensor_misc.parent, struct i2c_client, dev);
	printk("%s\n", __func__);
	if (misc_ps_opened)
		return -EBUSY;
	misc_ps_opened = 1;
	return 0;
}

static int al3006_psensor_release(struct inode *inode, struct file *file)
{
//	struct i2c_client *client =
//		       container_of (al3006_psensor_misc.parent, struct i2c_client, dev);
	printk("%s\n", __func__);
	misc_ps_opened = 0;
	return 0;
}

static long al3006_psensor_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	char reg, val, enabled;
	struct al3006_data *al3006 = &al3006_struct_data;
	struct i2c_client *client = al3006->client;

	printk("%s cmd %d\n", __func__, _IOC_NR(cmd));
	switch (cmd) {
	case PSENSOR_IOCTL_ENABLE:
		if (get_user(val, (unsigned long __user *)arg))
			return -EFAULT;
		if (val)
			return al3006_psensor_enable(client);
		else
			return al3006_psensor_disable(client);
		break;
	case PSENSOR_IOCTL_GET_ENABLED:
		mutex_lock(&al3006->lock);
		reg = CONFIG_REG;
		al3006_read_reg(client, reg, &val);
		mutex_unlock(&al3006->lock);
		val &= 0x03;
		if(val == ONLY_PROX_EN || val == ALL_PROX_ALS_EN)
			enabled = 1;
		else
			enabled = 0;
		return put_user(enabled, (unsigned long __user *)arg);
		break;
	default:
		pr_err("%s: invalid cmd %d\n", __func__, _IOC_NR(cmd));
		return -EINVAL;
	}
}

static struct file_operations al3006_psensor_fops = {
	.owner = THIS_MODULE,
	.open = al3006_psensor_open,
	.release = al3006_psensor_release,
	.unlocked_ioctl = al3006_psensor_ioctl
};

static struct miscdevice al3006_psensor_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "psensor",
	.fops = &al3006_psensor_fops
};

static int register_psensor_device(struct i2c_client *client, struct al3006_data *data)
{
	struct input_dev *input_dev = data->psensor_input_dev;
	int rc;

	AL3006_DEBUG("%s: allocating input device psensor\n", __func__);
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev,"%s: could not allocate input device for psensor\n", __FUNCTION__);
		rc = -ENOMEM;
		goto done;
	}
	data->psensor_input_dev = input_dev;
	input_set_drvdata(input_dev, data);

	input_set_drvdata(input_dev, data);
	input_dev->name = "proximity";
	set_bit(EV_ABS, input_dev->evbit);
	input_set_abs_params(input_dev, ABS_DISTANCE, 0, 1, 0, 0);

	AL3006_DEBUG("%s: registering input device psensor\n", __FUNCTION__);
	rc = input_register_device(input_dev);
	if (rc < 0) {
		pr_err("%s: could not register input device for psensor\n", __FUNCTION__);
		goto done;
	}

	AL3006_DEBUG("%s: registering misc device for psensor\n", __FUNCTION__);
	rc = misc_register(&al3006_psensor_misc);
	if (rc < 0) {
		pr_err("%s: could not register misc device psensor\n", __FUNCTION__);
		goto err_unregister_input_device;
	}
	al3006_psensor_misc.parent = &client->dev;

	//INIT_DELAYED_WORK(&data->p_work, al3006_psensor_work_handler);

	return 0;

err_unregister_input_device:
	input_unregister_device(input_dev);
done:
	return rc;
}

static void unregister_psensor_device(struct i2c_client *client, struct al3006_data *data)
{
	misc_deregister(&al3006_psensor_misc);
	input_unregister_device(data->psensor_input_dev);
}

#define LSENSOR_POLL_PROMESHUTOK   1000

static int al3006_lsensor_enable(struct i2c_client *client)
{
	char reg, value;
	int ret;
	struct al3006_data *al3006 = (struct al3006_data *)i2c_get_clientdata(client);

	mutex_lock(&al3006->lock);

	reg = CONFIG_REG;
	ret = al3006_read_reg(client, reg, &value);
	if( (value & 0x03) == ONLY_PROX_EN ){
		value &= ~0x03;
		value |= ALL_PROX_ALS_EN;
		ret = al3006_write_reg(client, reg, value);
	}
	else if( (value & 0x03) == ALL_IDLE ){
		value &= ~0x03;
		value |= ONLY_ALS_EN;
		ret = al3006_write_reg(client, reg, value);
	}
#ifdef AL3006_DBG
	ret = al3006_read_reg(client, reg, &value);
	AL3006_DEBUG("%s: configure reg value %#x ...\n", __FUNCTION__, value);
#endif

	mutex_unlock(&al3006->lock);

	//schedule_delayed_work(&(al3006->l_work), msecs_to_jiffies(LSENSOR_POLL_PROMESHUTOK));

	return ret;
}

static int al3006_lsensor_disable(struct i2c_client *client)
{
	char ret, reg, value;
	struct al3006_data *al3006 = (struct al3006_data *)i2c_get_clientdata(client);

	//cancel_delayed_work_sync(&(al3006->l_work));

	mutex_lock(&al3006->lock);
	reg = CONFIG_REG;
	ret = al3006_read_reg(client, reg, &value);
	if( (value & 0x03) == ONLY_ALS_EN ){
		value &= ~0x03;
		value |= ALL_IDLE;
		ret = al3006_write_reg(client, reg, value);
	}
	else if( (value & 0x03) == ALL_PROX_ALS_EN ){
		value &= ~0x03;
		value |= ONLY_PROX_EN;
		ret = al3006_write_reg(client, reg, value);
	}
#ifdef AL3006_DBG
	ret = al3006_read_reg(client, reg, &value);
	AL3006_DEBUG("%s: configure reg value %#x ...\n", __FUNCTION__, value);
#endif
	mutex_unlock(&al3006->lock);

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

static int misc_ls_opened = 0;

static int al3006_lsensor_open(struct inode *inode, struct file *file)
{
//	struct i2c_client *client =
//		       container_of (al3006_lsensor_misc.parent, struct i2c_client, dev);
	printk("%s\n", __func__);
	if (misc_ls_opened)
		return -EBUSY;
	misc_ls_opened = 1;
	return 0;
}

static int al3006_lsensor_release(struct inode *inode, struct file *file)
{

//	struct i2c_client *client =
//		       container_of (al3006_lsensor_misc.parent, struct i2c_client, dev);
	printk("%s\n", __func__);
	misc_ls_opened = 0;
	return 0;
}

static long al3006_lsensor_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	char reg, val, enabled;
	struct al3006_data *al3006 = &al3006_struct_data;
	struct i2c_client *client = al3006->client;

	printk("%s cmd %d\n", __FUNCTION__, _IOC_NR(cmd));
	switch (cmd) {
	case LIGHTSENSOR_IOCTL_ENABLE:
		if (get_user(val, (unsigned long __user *)arg))
			return -EFAULT;
		if (val)
			return al3006_lsensor_enable(client);
		else
			return al3006_lsensor_disable(client);
		break;
	case LIGHTSENSOR_IOCTL_GET_ENABLED:
		mutex_lock(&al3006->lock);
		reg =CONFIG_REG;
		al3006_read_reg(client, reg, &val);
		mutex_unlock(&al3006->lock);
		val &= 0x03;
		if(val == ONLY_ALS_EN || val == ALL_PROX_ALS_EN)
			enabled = 1;
		else
			enabled = 0;
		return put_user(enabled, (unsigned long __user *)arg);
		break;
	default:
		pr_err("%s: invalid cmd %d\n", __func__, _IOC_NR(cmd));
		return -EINVAL;
	}
}

static struct file_operations al3006_lsensor_fops = {
	.owner = THIS_MODULE,
	.open = al3006_lsensor_open,
	.release = al3006_lsensor_release,
	.unlocked_ioctl = al3006_lsensor_ioctl
};

static struct miscdevice al3006_lsensor_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lightsensor",
	.fops = &al3006_lsensor_fops
};

static int register_lsensor_device(struct i2c_client *client, struct al3006_data *data)
{
	struct input_dev *input_dev = data->lsensor_input_dev;
	int rc;

	AL3006_DEBUG("%s: allocating input device lsensor\n", __func__);
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev,"%s: could not allocate input device for lsensor\n", __FUNCTION__);
		rc = -ENOMEM;
		goto done;
	}
	data->lsensor_input_dev = input_dev;
	input_set_drvdata(input_dev, data);

	input_set_drvdata(input_dev, data);
	input_dev->name = "lightsensor-level";
	set_bit(EV_ABS, input_dev->evbit);
	input_set_abs_params(input_dev, ABS_MISC, 0, 8, 0, 0);

	AL3006_DEBUG("%s: registering input device al3006 lsensor\n", __FUNCTION__);
	rc = input_register_device(input_dev);
	if (rc < 0) {
		pr_err("%s: could not register input device for lsensor\n", __FUNCTION__);
		goto done;
	}

	AL3006_DEBUG("%s: registering misc device for al3006's lsensor\n", __FUNCTION__);
	rc = misc_register(&al3006_lsensor_misc);
	if (rc < 0) {
		pr_err("%s: could not register misc device lsensor\n", __FUNCTION__);
		goto err_unregister_input_device;
	}

	al3006_lsensor_misc.parent = &client->dev;

	//INIT_DELAYED_WORK(&data->l_work, al3006_lsensor_work_handler);

	return 0;

err_unregister_input_device:
	input_unregister_device(input_dev);
done:
	return rc;
}

static void unregister_lsensor_device(struct i2c_client *client, struct al3006_data *al3006)
{
	misc_deregister(&al3006_lsensor_misc);
	input_unregister_device(al3006->lsensor_input_dev);
}

static int al3006_config(struct i2c_client *client)
{
	char value;
	//struct al3006_data *al3006 = (struct al3006_data *)i2c_get_clientdata(client);

	AL3006_DEBUG("%s: init al3006 all register\n", __FUNCTION__);

    /***********************config**************************/
	value = 0x41;//The ADC effective resolution = 9;  Low lux threshold level = 1;
	//value = 0x69; //The ADC effective resolution = 17;  Low lux threshold level = 9;
	al3006_write_reg(client, ALS_CTL_REG, value);

	//value = 0x04;//0x01-0x0f; 17%->93.5% if value = 0x04,then Compensate Loss 52%
	value = 0x02;//0x01-0x0f; 17%->93.5% if value = 0x02,then Compensate Loss 31%
	al3006_write_reg(client, ALS_WINDOWS_REG, value);

	return 0;
}
void disable_al3006_device(struct i2c_client *client)
{
	char value;
	struct al3006_data *al3006 = (struct al3006_data *)i2c_get_clientdata(client);

#if 0
	mutex_lock(&al3006->lock);
	al3006_read_reg(client, CONFIG_REG, &value);
	value &= ~POWER_MODE_MASK;
	value |= POWER_DOWN_MODE;
	al3006_write_reg(client, CONFIG_REG, value);
	mutex_unlock(&al3006->lock);
#endif
	mutex_lock(&al3006->lock);
	al3006_write_reg(client, CONFIG_REG, 0x0B);
	al3006_read_reg(client, CONFIG_REG, &value);
	mutex_unlock(&al3006->lock);
	AL3006_DEBUG("%s: value = 0x%x\n", __FUNCTION__,value);
}

void enable_al3006_device(struct i2c_client *client)
{
	char value;
	struct al3006_data *al3006 = (struct al3006_data *)i2c_get_clientdata(client);

	mutex_lock(&al3006->lock);
	al3006_read_reg(client, CONFIG_REG, &value);
	value &= ~POWER_MODE_MASK;
	value |= POWER_UP_MODE;
	al3006_write_reg(client, CONFIG_REG, value);
	al3006_read_reg(client, CONFIG_REG, &value);
	mutex_unlock(&al3006->lock);

	AL3006_DEBUG("%s: value = 0x%x\n", __FUNCTION__,value);
#if 0
	mutex_lock(&al3006->lock);
	al3006_write_reg(client, CONFIG_REG, 0x03);
	mutex_unlock(&al3006->lock);
#endif

}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void al3006_suspend(struct early_suspend *h)
{
	struct i2c_client *client = container_of(al3006_psensor_misc.parent, struct i2c_client, dev);
	struct al3006_data *al3006 = (struct al3006_data *)i2c_get_clientdata(client);
	printk("al3006 early suspend ========================= \n");

	if (misc_ls_opened)
		al3006_lsensor_disable(client);
	if (misc_ps_opened)
		//al3006_psensor_disable(client);
		enable_irq_wake(al3006->irq);
	else
		disable_al3006_device(client);


	//disable_al3006_device(client);
}

static void al3006_resume(struct early_suspend *h)
{
	struct i2c_client *client = container_of(al3006_psensor_misc.parent, struct i2c_client, dev);
	struct al3006_data *al3006 = (struct al3006_data *)i2c_get_clientdata(client);
    printk("al3006 early resume ======================== \n");

	if (misc_ps_opened)
		//al3006_psensor_enable(client);
		disable_irq_wake(al3006->irq);
	if (misc_ls_opened)
		al3006_lsensor_enable(client);

	enable_al3006_device(client);
}
#else
#define al3006_suspend NULL
#define al3006_resume NULL
#endif

static int al3006_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct al3006_data *al3006 = &al3006_struct_data;
	int rc = -EIO;
	char value = 0;

	printk("\n%s: al3006 i2c client probe\n\n", __FUNCTION__);
	al3006_read_reg(client, CONFIG_REG, &value);
	printk("\n%s: al3006's CONFIG_REG value =  0x%x\n", __FUNCTION__, value);

	al3006->client = client;
	i2c_set_clientdata(client, al3006);
	mutex_init(&al3006->lock);

	rc = register_psensor_device(client, al3006);
	if (rc) {
		dev_err(&client->dev, "failed to register_psensor_device\n");
		goto done;
	}

	rc = register_lsensor_device(client, al3006);
	if (rc) {
		dev_err(&client->dev, "failed to register_lsensor_device\n");
		goto unregister_device1;
	}

	rc = al3006_config(client);
	if (rc) {
		dev_err(&client->dev, "failed to al3006_config\n");
		goto unregister_device2;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	al3006_early_suspend.suspend = al3006_suspend;
	al3006_early_suspend.resume  = al3006_resume;
	al3006_early_suspend.level   = 0x02;
	register_early_suspend(&al3006_early_suspend);
#endif

	INIT_DELAYED_WORK(&al3006->dwork, al3006_work_handler);

	rc = gpio_request(client->irq, "al3006 irq");
	if (rc) {
		pr_err("%s: request gpio %d for al3006 irq failed \n", __FUNCTION__, client->irq);
		goto unregister_device2;
	}
	rc = gpio_direction_input(client->irq);
	if (rc) {
		pr_err("%s: failed set gpio input\n", __FUNCTION__);
	}
	gpio_pull_updown(client->irq, GPIOPullUp);
	al3006->irq = gpio_to_irq(client->irq);
	mdelay(1);
	rc = request_irq(al3006->irq, al3006_irq_handler,
					IRQ_TYPE_EDGE_FALLING, client->name, (void *)al3006);//IRQ_TYPE_LEVEL_LOW
	if (rc < 0) {
		dev_err(&client->dev,"request_irq failed for gpio %d (%d)\n", client->irq, rc);
		goto err_free_gpio;
	}

	//al3006_psensor_enable(client);
	//al3006_lsensor_enable(client);

	return 0;

err_free_gpio:
	gpio_free(client->irq);
unregister_device2:
	unregister_lsensor_device(client, &al3006_struct_data);
unregister_device1:
	unregister_psensor_device(client, &al3006_struct_data);
done:
	return rc;
}

static int al3006_remove(struct i2c_client *client)
{
	struct al3006_data *data = i2c_get_clientdata(client);

	unregister_psensor_device(client, data);
	unregister_lsensor_device(client, data);
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&al3006_early_suspend);
#endif
	return 0;
}

static const struct i2c_device_id al3006_id[] = {
		{"al3006", 0},
		{ }
};

static struct i2c_driver al3006_driver = {
	.driver = {
		.name = "al3006",
	},
	.probe    = al3006_probe,
	.remove   = al3006_remove,
	.id_table = al3006_id,

};

static int __init al3006_init(void)
{

	return i2c_add_driver(&al3006_driver);
}

static void __exit al3006_exit(void)
{
	return i2c_del_driver(&al3006_driver);
}

module_init(al3006_init);
module_exit(al3006_exit);
