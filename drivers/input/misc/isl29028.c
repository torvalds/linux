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
#define PROX_PRST(x)  (x << 5)
#define ALS_PRST(x)   (x << 1)
#define INT_AND       (1 << 0)
#define INT_OR        (0 << 0)

#define ISL_REG_PROX_LT       (0x03)
#define ISL_REG_PROX_HT       (0x04)
#define PROX_LT               (0xe0)
#define PROX_HT               (0xf0)

#define ISL_REG_PROX_DATA     (0x08)

struct isl29028_data {
	struct input_dev         *input_dev;
	struct i2c_client        *client;
	struct workqueue_struct  *wq;
	struct work_struct		 work;
	struct delayed_work      d_work;
	//struct completion        completion;
	//wait_queue_head_t wait;

	int enabled;
	int irq;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend isl29028_early_suspend;
#endif
static struct miscdevice isl29028_misc;

static int misc_opened;
static int isl29028_open(struct inode *inode, struct file *file);
static int isl29028_release(struct inode *inode, struct file *file);
static long isl29028_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

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

static irqreturn_t isl29028_irq_handler(int irq, void *data)
{
	struct isl29028_data *isl = (struct isl29028_data *)data;
	input_report_abs(isl->input_dev, ABS_DISTANCE, 0);
	input_sync(isl->input_dev);

	disable_irq_nosync(isl->irq);
	//queue_work(isl->wq, &isl->work);
	schedule_delayed_work(&isl->d_work, msecs_to_jiffies(420));
	//wake_up_interruptible(&isl->wait);

	return IRQ_HANDLED;
}

static int isl29028_enable(struct i2c_client *client)
{
	char reg, value;
	int ret;
	struct isl29028_data *isl = (struct isl29028_data *)i2c_get_clientdata(client);

	reg = ISL_REG_CONFIG;
	ret = isl29028_read_reg(client, reg, &value);
	value |= PROX_EN;
	value |= ALS_EN; /* ZMF */
	ret = isl29028_write_reg(client, reg, value);
	//schedule_delayed_work(&isl->d_work, msecs_to_jiffies(420));
	//enable_irq(isl->irq);
#ifdef ISL_DBG
	ret = isl29028_read_reg(client, reg, &value);
	D("%s: configure reg value %#x ...\n", __FUNCTION__, value);	
#endif
	return ret;
}

static int isl29028_disable(struct i2c_client *client)
{
	char ret, reg, reg2, value, value2;
	struct isl29028_data *isl = (struct isl29028_data *)i2c_get_clientdata(client);

	reg = ISL_REG_CONFIG;
	ret = isl29028_read_reg(client, reg, &value);
	value &= ~PROX_EN;
	value &= ~ALS_EN;	/* ZMF */
	ret = isl29028_write_reg(client, reg, value);

	reg2 = ISL_REG_INT;
	ret = isl29028_read_reg(client, reg2, &value2);
	value2 &= PROX_FLAG_MASK;
	ret = isl29028_write_reg(client, reg2, value2);

	disable_irq(isl->irq);
	if (cancel_delayed_work_sync(&isl->d_work)) {
		enable_irq(isl->irq);
	}
	enable_irq(isl->irq);

#ifdef ISL_DBG
	ret = isl29028_read_reg(client, reg, &value);
	ret = isl29028_read_reg(client, reg2, &value2);
	D("%s: configure reg value %#x ...\n", __FUNCTION__, value);	
	D("%s: interrupt reg value %#x ...\n", __FUNCTION__, value2);	
#endif
	//disable_irq(isl->irq);
	//cancel_delayed_work_sync(&isl->d_work);

	return ret;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void isl29028_suspend(struct early_suspend *h)
{
	struct i2c_client *client = container_of(isl29028_misc.parent, struct i2c_client, dev);
    D("isl29028 early suspend ========================= \n"); 
	isl29028_disable(client);	
}

static void isl29028_resume(struct early_suspend *h)
{
	struct i2c_client *client = container_of(isl29028_misc.parent, struct i2c_client, dev);
    D("isl29028 early resume ======================== \n"); 
	isl29028_enable(client);

}

#else
#define isl29028_suspend NULL
#define isl29028_resume NULL
#endif

#if 0
static void isl29028_work_handler(struct work_struct *work)
{
	struct isl29028_data *isl = container_of(work, struct isl29028_data, work);
	char reg, value, int_flag;
	//int als_data;
		reg = ISL_REG_INT;
		isl29028_read_reg(isl->client, reg, (char *)&int_flag);

		if (int_flag & (1 << 7)) {
			reg = ISL_REG_PROX_DATA;
			isl29028_read_reg(isl->client, reg, (char *)&value);

			D("\n%s: isl29028  prox_data is %#x\n", __FUNCTION__, value);
			D("%s: isl29028  int_data is %#x\n", __FUNCTION__, int_flag);

			//input_report_abs(isl->input_dev, ABS_DISTANCE, 0);
			//input_sync(isl->input_dev);
		}

		if (!(int_flag & (1 << 7))) {

			reg = ISL_REG_PROX_DATA;
			isl29028_read_reg(isl->client, reg, (char *)&value);
			D("\n%s: isl29028  ................prox_data is %#x\n", __FUNCTION__, value);
			D("%s: isl29028  ................int_data is %#x\n", __FUNCTION__, int_flag);
			//input_report_abs(isl->input_dev, ABS_DISTANCE, 0);
			//input_sync(isl->input_dev);
		}
		enable_irq(isl->irq);
}
#endif

static void isl29028_dwork_handler(struct work_struct *work)
{
	struct isl29028_data *isl = (struct isl29028_data *)container_of(work, struct isl29028_data, d_work.work);
	char reg, value, int_flag;

	reg = ISL_REG_INT;
	isl29028_read_reg(isl->client, reg, (char *)&int_flag);

	if (!(int_flag >> 7)) {
		input_report_abs(isl->input_dev, ABS_DISTANCE, 1);
		input_sync(isl->input_dev);
	}

	reg = ISL_REG_PROX_DATA;
	isl29028_read_reg(isl->client, reg, (char *)&value);
	//D("%s: int is %#x\n", __FUNCTION__, int_flag);
	D("%s: prox_int is %d\n", __FUNCTION__, (int_flag >> 7 ));
	D("%s: prox_data is %#x\n", __FUNCTION__, value);

	enable_irq(isl->irq);
}

#if 0
static int isl29028_thread(void *data)
{
	char value, int_flag;
	int als_data;
	struct isl29028_data *isl = data;
	DECLARE_WAITQUEUE(wait, current);
	__add_wait_queue(&isl->wait, &wait);
	__set_current_state(TASK_INTERRUPTIBLE);
	complete(&isl->completion);
	while (1) {

		schedule();

		if (signal_pending(current)) {
			continue;
		}

		isl29028_read_reg(isl->client, 0x02, (char *)&int_flag);

		if (int_flag & (1 << 7)) {
			isl29028_read_reg(isl->client, 0x08, (char *)&value);

			printk("%s: isl29028  prox_data is %#x\n", __FUNCTION__, value);

			//input_report_abs(isl->input_dev, ABS_DISTANCE, 0);
			//input_sync(isl->input_dev);
		}
		if (int_flag & (1 << 3)) {
			isl29028_read_reg(isl->client, 0x09, (char *)&value);
			als_data = value;

			isl29028_read_reg(isl->client, 0x0a, (char *)&value);
			als_data |= value << 8;

			D("%s: isl29028 als_data is %#x\n", __FUNCTION__,  als_data);
			int_flag &= ~(1 << 3);
			isl29028_write_reg(isl->client, 0x02, int_flag);
		}

		enable_irq(isl->irq);
	}

	remove_wait_queue(&isl->wait, &wait);
	__set_current_state(TASK_RUNNING);

	return 0;
}
#endif


static struct file_operations isl29028_fops = {
	.owner = THIS_MODULE,
	.open = isl29028_open,
	.release = isl29028_release,
	.unlocked_ioctl = isl29028_ioctl
};

static struct miscdevice isl29028_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "psensor",
	.fops = &isl29028_fops
};

static int isl29028_open(struct inode *inode, struct file *file)
{
	struct i2c_client *client = 
		       container_of (isl29028_misc.parent, struct i2c_client, dev);
	D("%s\n", __func__);
	if (misc_opened)
		return -EBUSY;
	misc_opened = 1;
	
	return isl29028_enable(client);
}

static int isl29028_release(struct inode *inode, struct file *file)
{
	
	struct i2c_client *client = 
		       container_of (isl29028_misc.parent, struct i2c_client, dev);
	D("%s\n", __func__);
	misc_opened = 0;
	return isl29028_disable(client);
}

static long isl29028_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	char reg, val, enabled;
	struct i2c_client *client = 
		       container_of (isl29028_misc.parent, struct i2c_client, dev);

	D("%s cmd %d\n", __func__, _IOC_NR(cmd));
	switch (cmd) {
	case ISL29028_IOCTL_ENABLE:
		if (get_user(val, (unsigned long __user *)arg))
			return -EFAULT;
		if (val)
			return isl29028_enable(client);
		else
			return isl29028_disable(client);
		break;
	case ISL29028_IOCTL_GET_ENABLED:
		reg = ISL_REG_CONFIG;
		isl29028_read_reg(client, reg, &val);
		enabled = (val & ~(1 << 7)) ? 1 : 0;
		return put_user(enabled, (unsigned long __user *)arg);
		break;
	default:
		pr_err("%s: invalid cmd %d\n", __func__, _IOC_NR(cmd));
		return -EINVAL;
	}
}

static void isl29028_config(struct i2c_client *client)
{
	int ret;
	char value, buf[2];

	D("%s: init isl29028 all register\n", __func__);

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
	mdelay(2);
    /*************************************************/

	buf[0] = ISL_REG_CONFIG;
	buf[1] = /*PROX_EN | */PROX_SLP(4) | PROX_DR_220 /*| ALS_EN | ALS_RANGE_L*/; 
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
}


static int isl29028_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int rc = -EIO;
	struct input_dev *input_dev;
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


	D("%s: allocating input device\n", __func__);
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev,"%s: could not allocate input device\n", __FUNCTION__);
		rc = -ENOMEM;
		goto done;
	}
	isl->input_dev = input_dev;
	input_set_drvdata(input_dev, isl);

	input_dev->name = "proximity";

	set_bit(EV_ABS, input_dev->evbit);
	input_set_abs_params(input_dev, ABS_DISTANCE, 0, 1, 0, 0);

	D("%s: registering input device\n", __func__);
	rc = input_register_device(input_dev);
	if (rc < 0) {
		pr_err("%s: could not register input device\n", __func__);
		goto err_free_input_device;
	}

	D("%s: registering misc device\n", __func__);
	rc = misc_register(&isl29028_misc);
	if (rc < 0) {
		pr_err("%s: could not register misc device\n", __func__);
		goto err_unregister_input_device;
	}
	
	isl29028_misc.parent = &client->dev;
	misc_deregister(&isl29028_misc);

	//isl->wq = create_freezeable_workqueue("isl29028_wq");
	//INIT_WORK(&isl->work, isl29028_work_handler);
	INIT_DELAYED_WORK(&isl->d_work, isl29028_dwork_handler);

	rc = gpio_request(client->irq, "isl29028 irq");
	if (rc) {
		pr_err("%s: request gpio %d failed \n",	__func__, client->irq);
		return rc;
	}
	rc = gpio_direction_input(client->irq);
	if (rc) {
		pr_err("%s: failed set gpio input\n", __FUNCTION__);
	}

	gpio_pull_updown(client->irq, GPIOPullUp);
	isl->irq = gpio_to_irq(client->irq);
	mdelay(1);
	rc = request_irq(isl->irq, isl29028_irq_handler,
			        IRQ_TYPE_LEVEL_LOW, client->name, (void *)isl);
	if (rc < 0) {
		dev_err(&client->dev,"request_irq failed for gpio %d (%d)\n", client->irq, rc);
		goto err_unregister_input_device;
	}

	//init_completion(&isl->completion);
	//init_waitqueue_head(&isl->wait);
	//kernel_thread(isl29028_thread, (void*)isl, CLONE_FILES | CLONE_SIGHAND | CLONE_FS);
	//wait_for_completion(&isl->completion);
	isl29028_config(client);

#ifdef CONFIG_HAS_EARLYSUSPEND
	isl29028_early_suspend.suspend = isl29028_suspend;
	isl29028_early_suspend.resume  = isl29028_resume;
	isl29028_early_suspend.level   = 0x02;
	register_early_suspend(&isl29028_early_suspend);
#endif

err_unregister_input_device:
	input_unregister_device(input_dev);
	goto done;
err_free_input_device:
	input_free_device(input_dev);
done:
	return rc;
}

static int isl29028_remove(struct i2c_client *client)
{
	struct isl29028_data *isl29028 = i2c_get_clientdata(client);
	
    misc_deregister(&isl29028_misc);
    input_unregister_device(isl29028->input_dev);
    input_free_device(isl29028->input_dev);
    free_irq(client->irq, isl29028);
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

