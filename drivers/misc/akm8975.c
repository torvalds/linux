/* drivers/misc/akm8975.c - akm8975 compass driver
 *
 * Copyright (C) 2007-2008 HTC Corporation.
 * Author: Hou-Kun Chen <houkun.chen@gmail.com>
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

/*
 * Revised by AKM 2009/04/02
 * Revised by Motorola 2010/08/16
 *
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/akm8975.h>

#define AK8975DRV_CALL_DBG 0
#if AK8975DRV_CALL_DBG
#define FUNCDBG(msg)	pr_info("%s:%s\n", __func__, msg);
#else
#define FUNCDBG(msg)
#endif

#define AK8975DRV_DATA_DBG 0
#define MAX_FAILURE_COUNT 10

struct akm8975_data {
	struct i2c_client *this_client;
	struct input_dev *input_dev;
	struct work_struct work;
	struct mutex flags_lock;
	struct regulator *regulator;
};

/*
* Because misc devices can not carry a pointer from driver register to
* open, we keep this global. This limits the driver to a single instance.
*/
struct akm8975_data *akmd_data;

static DECLARE_WAIT_QUEUE_HEAD(open_wq);

static atomic_t open_flag;

static short m_flag;
static short mv_flag;

static short akmd_delay;

static ssize_t akm8975_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	return sprintf(buf, "%u\n", i2c_smbus_read_byte_data(client,
							     AK8975_REG_CNTL));
}
static ssize_t akm8975_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long val;
	strict_strtoul(buf, 10, &val);
	if (val > 0xff)
		return -EINVAL;
	i2c_smbus_write_byte_data(client, AK8975_REG_CNTL, val);
	return count;
}
static DEVICE_ATTR(akm_ms1, S_IWUSR | S_IRUGO, akm8975_show, akm8975_store);

static int akm8975_i2c_rxdata(struct akm8975_data *akm, char *buf, int length)
{
	struct i2c_msg msgs[] = {
		{
			.addr = akm->this_client->addr,
			.flags = 0,
			.len = 1,
			.buf = buf,
		},
		{
			.addr = akm->this_client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = buf,
		},
	};

	FUNCDBG("called");

	if (i2c_transfer(akm->this_client->adapter, msgs, 2) < 0) {
		pr_err("akm8975_i2c_rxdata: transfer error\n");
		return EIO;
	} else
		return 0;
}

static int akm8975_i2c_txdata(struct akm8975_data *akm, char *buf, int length)
{
	struct i2c_msg msgs[] = {
		{
			.addr = akm->this_client->addr,
			.flags = 0,
			.len = length,
			.buf = buf,
		},
	};

	FUNCDBG("called");

	if (i2c_transfer(akm->this_client->adapter, msgs, 1) < 0) {
		pr_err("akm8975_i2c_txdata: transfer error\n");
		return -EIO;
	} else
		return 0;
}

static void akm8975_ecs_report_value(struct akm8975_data *akm, short *rbuf)
{
	struct akm8975_data *data = i2c_get_clientdata(akm->this_client);

	FUNCDBG("called");

#if AK8975DRV_DATA_DBG
	pr_info("akm8975_ecs_report_value: yaw = %d, pitch = %d, roll = %d\n",
				 rbuf[0], rbuf[1], rbuf[2]);
	pr_info("tmp = %d, m_stat= %d, g_stat=%d\n", rbuf[3], rbuf[4], rbuf[5]);
	pr_info("Acceleration:	 x = %d LSB, y = %d LSB, z = %d LSB\n",
				 rbuf[6], rbuf[7], rbuf[8]);
	pr_info("Magnetic:	 x = %d LSB, y = %d LSB, z = %d LSB\n\n",
				 rbuf[9], rbuf[10], rbuf[11]);
#endif
	mutex_lock(&akm->flags_lock);
	/* Report magnetic sensor information */
	if (m_flag) {
		input_report_rel(data->input_dev, REL_RX, rbuf[0]);
		input_report_rel(data->input_dev, REL_RY, rbuf[1]);
		input_report_rel(data->input_dev, REL_RZ, rbuf[2]);
		input_report_rel(data->input_dev, REL_WHEEL, rbuf[4]);
	}

	if (mv_flag) {
		input_report_rel(data->input_dev, REL_DIAL, rbuf[9]);
		input_report_rel(data->input_dev, REL_HWHEEL, rbuf[10]);
		input_report_rel(data->input_dev, REL_MISC, rbuf[11]);
	}
	mutex_unlock(&akm->flags_lock);

	input_sync(data->input_dev);
}

static void akm8975_ecs_close_done(struct akm8975_data *akm)
{
	FUNCDBG("called");
	mutex_lock(&akm->flags_lock);
	m_flag = 0;
	mv_flag = 0;
	mutex_unlock(&akm->flags_lock);
}

static int akm_aot_open(struct inode *inode, struct file *file)
{
	int ret = -1;

	FUNCDBG("called");
	if (atomic_cmpxchg(&open_flag, 0, 1) == 0) {
		wake_up(&open_wq);
		ret = 0;
	}

	ret = nonseekable_open(inode, file);
	if (ret)
		return ret;

	file->private_data = akmd_data;

	return ret;
}

static int akm_aot_release(struct inode *inode, struct file *file)
{
	FUNCDBG("called");
	atomic_set(&open_flag, 0);
	wake_up(&open_wq);
	return 0;
}

static long akm_aot_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *) arg;
	short flag;
	struct akm8975_data *akm = file->private_data;

	FUNCDBG("called");

	switch (cmd) {
	case ECS_IOCTL_APP_SET_MFLAG:
	case ECS_IOCTL_APP_SET_MVFLAG:
		if (copy_from_user(&flag, argp, sizeof(flag)))
			return -EFAULT;
		if (flag < 0 || flag > 1)
			return -EINVAL;
		break;
	case ECS_IOCTL_APP_SET_DELAY:
		if (copy_from_user(&flag, argp, sizeof(flag)))
			return -EFAULT;
		break;
	default:
		break;
	}

	mutex_lock(&akm->flags_lock);
	switch (cmd) {
	case ECS_IOCTL_APP_SET_MFLAG:
		m_flag = flag;
		break;
	case ECS_IOCTL_APP_GET_MFLAG:
		flag = m_flag;
		break;
	case ECS_IOCTL_APP_SET_MVFLAG:
		mv_flag = flag;
		break;
	case ECS_IOCTL_APP_GET_MVFLAG:
		flag = mv_flag;
		break;
	case ECS_IOCTL_APP_SET_DELAY:
		akmd_delay = flag;
		break;
	case ECS_IOCTL_APP_GET_DELAY:
		flag = akmd_delay;
		break;
	default:
		mutex_unlock(&akm->flags_lock);
		return -ENOTTY;
	}
	mutex_unlock(&akm->flags_lock);

	switch (cmd) {
	case ECS_IOCTL_APP_GET_MFLAG:
	case ECS_IOCTL_APP_GET_MVFLAG:
	case ECS_IOCTL_APP_GET_DELAY:
		if (copy_to_user(argp, &flag, sizeof(flag)))
			return -EFAULT;
		break;
	default:
		break;
	}

	return 0;
}

static int akmd_open(struct inode *inode, struct file *file)
{
	int err = 0;

	FUNCDBG("called");
	err = nonseekable_open(inode, file);
	if (err)
		return err;

	file->private_data = akmd_data;
	return 0;
}

static int akmd_release(struct inode *inode, struct file *file)
{
	struct akm8975_data *akm = file->private_data;

	FUNCDBG("called");
	akm8975_ecs_close_done(akm);
	return 0;
}

static long akmd_ioctl(struct file *file, unsigned int cmd,
		       unsigned long arg)
{
	void __user *argp = (void __user *) arg;

	char rwbuf[16];
	int ret = -1;
	int status;
	short value[12];
	short delay;
	struct akm8975_data *akm = file->private_data;

	FUNCDBG("called");

	switch (cmd) {
	case ECS_IOCTL_READ:
	case ECS_IOCTL_WRITE:
		if (copy_from_user(&rwbuf, argp, sizeof(rwbuf)))
			return -EFAULT;
		break;

	case ECS_IOCTL_SET_YPR:
		if (copy_from_user(&value, argp, sizeof(value)))
			return -EFAULT;
		break;

	default:
		break;
	}

	switch (cmd) {
	case ECS_IOCTL_READ:
		if (rwbuf[0] < 1)
			return -EINVAL;

		ret = akm8975_i2c_rxdata(akm, &rwbuf[1], rwbuf[0]);
		if (ret < 0)
			return ret;
		break;

	case ECS_IOCTL_WRITE:
		if (rwbuf[0] < 2)
			return -EINVAL;

		ret = akm8975_i2c_txdata(akm, &rwbuf[1], rwbuf[0]);
		if (ret < 0)
			return ret;
		break;
	case ECS_IOCTL_SET_YPR:
		akm8975_ecs_report_value(akm, value);
		break;

	case ECS_IOCTL_GET_OPEN_STATUS:
		wait_event_interruptible(open_wq,
					 (atomic_read(&open_flag) != 0));
		status = atomic_read(&open_flag);
		break;
	case ECS_IOCTL_GET_CLOSE_STATUS:
		wait_event_interruptible(open_wq,
					 (atomic_read(&open_flag) == 0));
		status = atomic_read(&open_flag);
		break;

	case ECS_IOCTL_GET_DELAY:
		delay = akmd_delay;
		break;

	default:
		FUNCDBG("Unknown cmd\n");
		return -ENOTTY;
	}

	switch (cmd) {
	case ECS_IOCTL_READ:
		if (copy_to_user(argp, &rwbuf, sizeof(rwbuf)))
			return -EFAULT;
		break;
	case ECS_IOCTL_GET_OPEN_STATUS:
	case ECS_IOCTL_GET_CLOSE_STATUS:
		if (copy_to_user(argp, &status, sizeof(status)))
			return -EFAULT;
		break;
	case ECS_IOCTL_GET_DELAY:
		if (copy_to_user(argp, &delay, sizeof(delay)))
			return -EFAULT;
		break;
	default:
		break;
	}

	return 0;
}

/* needed to clear the int. pin */
static void akm_work_func(struct work_struct *work)
{
	struct akm8975_data *akm =
	    container_of(work, struct akm8975_data, work);

	FUNCDBG("called");
	enable_irq(akm->this_client->irq);
}

static irqreturn_t akm8975_interrupt(int irq, void *dev_id)
{
	struct akm8975_data *akm = dev_id;
	FUNCDBG("called");

	disable_irq_nosync(akm->this_client->irq);
	schedule_work(&akm->work);
	return IRQ_HANDLED;
}

static int akm8975_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct akm8975_data *akm = i2c_get_clientdata(client);
	int ret = 0;

#if AK8975DRV_CALL_DBG
	pr_info("%s\n", __func__);
#endif
	/* TO DO: might need more work after power mgmt
	   is enabled */
	if (akm->regulator)
		ret = regulator_disable(akm->regulator);
	return ret;
}

static int akm8975_resume(struct i2c_client *client)
{
	struct akm8975_data *akm = i2c_get_clientdata(client);
	int ret = 0;

#if AK8975DRV_CALL_DBG
	pr_info("%s\n", __func__);
#endif
	/* TO DO: might need more work after power mgmt
	   is enabled */
	if (akm->regulator)
		ret = regulator_enable(akm->regulator);
	return ret;
}

static int akm8975_init_client(struct i2c_client *client)
{
	struct akm8975_data *data;
	int ret;

	data = i2c_get_clientdata(client);

	ret = request_irq(client->irq, akm8975_interrupt, IRQF_TRIGGER_RISING,
				"akm8975", data);

	if (ret < 0) {
		pr_err("akm8975_init_client: request irq failed\n");
		goto err;
	}

	init_waitqueue_head(&open_wq);

	mutex_lock(&data->flags_lock);
	m_flag = 0;
	mv_flag = 0;
	mutex_unlock(&data->flags_lock);

	return 0;
err:
	return ret;
}

static const struct file_operations akmd_fops = {
	.owner = THIS_MODULE,
	.open = akmd_open,
	.release = akmd_release,
	.unlocked_ioctl = akmd_ioctl,
};

static const struct file_operations akm_aot_fops = {
	.owner = THIS_MODULE,
	.open = akm_aot_open,
	.release = akm_aot_release,
	.unlocked_ioctl = akm_aot_ioctl,
};

static struct miscdevice akm_aot_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "akm8975_aot",
	.fops = &akm_aot_fops,
};

static struct miscdevice akmd_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "akm8975_dev",
	.fops = &akmd_fops,
};

int akm8975_probe(struct i2c_client *client,
		  const struct i2c_device_id *devid)
{
	struct akm8975_data *akm;
	int err;
	FUNCDBG("called");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "platform data is NULL. exiting.\n");
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	akm = kzalloc(sizeof(struct akm8975_data), GFP_KERNEL);
	if (!akm) {
		dev_err(&client->dev,
			"failed to allocate memory for module data\n");
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	mutex_init(&akm->flags_lock);
	INIT_WORK(&akm->work, akm_work_func);
	i2c_set_clientdata(client, akm);

	akm->regulator = regulator_get(&client->dev, "vcc");
	if (IS_ERR_OR_NULL(akm->regulator)) {
		dev_err(&client->dev, "unable to get regulator %s\n",
				 dev_name(&client->dev));
		akm->regulator = NULL;
	} else {
		regulator_enable(akm->regulator);
	}

	akm8975_init_client(client);
	akm->this_client = client;
	akmd_data = akm;

	akm->input_dev = input_allocate_device();
	if (!akm->input_dev) {
		err = -ENOMEM;
		dev_err(&akm->this_client->dev,
			"input device allocate failed\n");
		goto exit_input_dev_alloc_failed;
	}

	/* orientation: yaw */
	input_set_capability(akm->input_dev, EV_REL, REL_RX);
	/* orientation: pitch */
	input_set_capability(akm->input_dev, EV_REL, REL_RY);
	/* orientation: roll */
	input_set_capability(akm->input_dev, EV_REL, REL_RZ);

	/* status of orientation sensor */
	input_set_capability(akm->input_dev, EV_REL, REL_WHEEL);

	/* x-axis of raw magnetic vector */
	input_set_capability(akm->input_dev, EV_REL, REL_DIAL);
	/* y-axis of raw magnetic vector */
	input_set_capability(akm->input_dev, EV_REL, REL_HWHEEL);
	/* z-axis of raw magnetic vector */
	input_set_capability(akm->input_dev, EV_REL, REL_MISC);

	akm->input_dev->name = "compass";

	err = input_register_device(akm->input_dev);
	if (err) {
		pr_err("akm8975_probe: Unable to register input device: %s\n",
					 akm->input_dev->name);
		goto exit_input_register_device_failed;
	}

	err = misc_register(&akmd_device);
	if (err) {
		pr_err("akm8975_probe: akmd_device register failed\n");
		goto exit_misc_device_register_failed;
	}

	err = misc_register(&akm_aot_device);
	if (err) {
		pr_err("akm8975_probe: akm_aot_device register failed\n");
		goto exit_misc_device_register_failed;
	}

	err = device_create_file(&client->dev, &dev_attr_akm_ms1);
	return 0;

exit_misc_device_register_failed:
exit_input_register_device_failed:
	input_free_device(akm->input_dev);
exit_input_dev_alloc_failed:
	if (akm->regulator)
		regulator_put(akm->regulator);
	kfree(akm);
exit_alloc_data_failed:
exit_check_functionality_failed:
	return err;
}

static int __devexit akm8975_remove(struct i2c_client *client)
{
	struct akm8975_data *akm = i2c_get_clientdata(client);
	FUNCDBG("called");
	free_irq(client->irq, NULL);
	input_unregister_device(akm->input_dev);
	misc_deregister(&akmd_device);
	misc_deregister(&akm_aot_device);
	if (akm->regulator) {
		regulator_disable(akm->regulator);
		regulator_put(akm->regulator);
	}
	kfree(akm);
	return 0;
}

static const struct i2c_device_id akm8975_id[] = {
	{ "akm8975", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, akm8975_id);

static struct i2c_driver akm8975_driver = {
	.probe = akm8975_probe,
	.remove = akm8975_remove,
	.resume = akm8975_resume,
	.suspend = akm8975_suspend,
	.id_table = akm8975_id,
	.driver = {
		.name = "akm8975",
	},
};

static int __init akm8975_init(void)
{
	pr_info("AK8975 compass driver: init\n");
	return i2c_add_driver(&akm8975_driver);
}

static void __exit akm8975_exit(void)
{
	i2c_del_driver(&akm8975_driver);
}

module_init(akm8975_init);
module_exit(akm8975_exit);

MODULE_AUTHOR("Hou-Kun Chen <hk_chen@htc.com>");
MODULE_DESCRIPTION("AK8975 compass driver");
MODULE_LICENSE("GPL");
