/*
 * Copyright (C) 2010 MEMSIC, Inc.
 *
 * Initial Code:
 *	Robbie Cao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <asm/uaccess.h>

#include "mecs.h"

#define DEBUG			1

#define ECS_DATA_DEV_NAME	"ecompass_data"
#define ECS_CTRL_DEV_NAME	"ecompass_ctrl"

static int ecs_ctrl_open(struct inode *inode, struct file *file);
static int ecs_ctrl_release(struct inode *inode, struct file *file);
static long ecs_ctrl_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static DECLARE_WAIT_QUEUE_HEAD(open_wq);

static atomic_t	open_count;
static atomic_t	open_flag;
static atomic_t	reserve_open_flag;

static atomic_t	a_flag;
static atomic_t	m_flag;
static atomic_t	o_flag;

static short ecompass_delay = 0;


static struct input_dev *ecs_data_device;

static struct file_operations ecs_ctrl_fops = {
	.owner		= THIS_MODULE,
	.open		= ecs_ctrl_open,
	.release	= ecs_ctrl_release,
	.unlocked_ioctl		= ecs_ctrl_ioctl,
};

static struct miscdevice ecs_ctrl_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = ECS_CTRL_DEV_NAME,
	.fops = &ecs_ctrl_fops,
};

static int ecs_ctrl_open(struct inode *inode, struct file *file)
{
#if 1
	atomic_set(&reserve_open_flag, 1);
	atomic_set(&open_flag, 1);
	atomic_set(&open_count, 1);
	wake_up(&open_wq);

	return 0;
#else
	int ret = -1;

	if (atomic_cmpxchg(&open_count, 0, 1) == 0) {
		if (atomic_cmpxchg(&open_flag, 0, 1) == 0) {
			atomic_set(&reserve_open_flag, 1);
			wake_up(&open_wq);
			ret = 0;
		}
	}

	return ret;
#endif
}

static int ecs_ctrl_release(struct inode *inode, struct file *file)
{
	atomic_set(&reserve_open_flag, 0);
	atomic_set(&open_flag, 0);
	atomic_set(&open_count, 0);
	wake_up(&open_wq);

	return 0;
}

static long ecs_ctrl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *pa = (void __user *)arg;
	short flag;
	short delay;
	int parms[4];
	int ypr[12];

	switch (cmd) {
	case ECOMPASS_IOC_SET_MODE:
		break;
	case ECOMPASS_IOC_SET_DELAY:
		if (copy_from_user(&delay, pa, sizeof(delay)))
			return -EFAULT;
		ecompass_delay = delay;
		break;
	case ECOMPASS_IOC_GET_DELAY:
		delay = ecompass_delay;
		if (copy_to_user(pa, &delay, sizeof(delay)))
			return -EFAULT;
		break;

	case ECOMPASS_IOC_SET_AFLAG:
		if (copy_from_user(&flag, pa, sizeof(flag)))
			return -EFAULT;
		if (flag < 0 || flag > 1)
			return -EINVAL;
		atomic_set(&a_flag, flag);
		break;
	case ECOMPASS_IOC_GET_AFLAG:
		flag = atomic_read(&a_flag);
		if (copy_to_user(pa, &flag, sizeof(flag)))
			return -EFAULT;
		break;
	case ECOMPASS_IOC_SET_MFLAG:
		if (copy_from_user(&flag, pa, sizeof(flag)))
			return -EFAULT;
		if (flag < 0 || flag > 1)
			return -EINVAL;
		atomic_set(&m_flag, flag);
		break;
	case ECOMPASS_IOC_GET_MFLAG:
		flag = atomic_read(&m_flag);
		if (copy_to_user(pa, &flag, sizeof(flag)))
			return -EFAULT;
		break;
	case ECOMPASS_IOC_SET_OFLAG:
		if (copy_from_user(&flag, pa, sizeof(flag)))
			return -EFAULT;
		if (flag < 0 || flag > 1)
			return -EINVAL;
		atomic_set(&o_flag, flag);
		break;
	case ECOMPASS_IOC_GET_OFLAG:
		flag = atomic_read(&o_flag);
		if (copy_to_user(pa, &flag, sizeof(flag)))
			return -EFAULT;
		break;

	case ECOMPASS_IOC_SET_APARMS:
		if (copy_from_user(parms, pa, sizeof(parms)))
			return -EFAULT;
		/* acceleration x-axis */
		input_set_abs_params(ecs_data_device, ABS_X, 
			parms[0], parms[1], parms[2], parms[3]);
		/* acceleration y-axis */
		input_set_abs_params(ecs_data_device, ABS_Y, 
			parms[0], parms[1], parms[2], parms[3]);
		/* acceleration z-axis */
		input_set_abs_params(ecs_data_device, ABS_Z, 
			parms[0], parms[1], parms[2], parms[3]);
		break;
	case ECOMPASS_IOC_GET_APARMS:
		break;
	case ECOMPASS_IOC_SET_MPARMS:
		if (copy_from_user(parms, pa, sizeof(parms)))
			return -EFAULT;
		/* magnetic raw x-axis */
		input_set_abs_params(ecs_data_device, ABS_HAT0X, 
			parms[0], parms[1], parms[2], parms[3]);
		/* magnetic raw y-axis */
		input_set_abs_params(ecs_data_device, ABS_HAT0Y, 
			parms[0], parms[1], parms[2], parms[3]);
		/* magnetic raw z-axis */
		input_set_abs_params(ecs_data_device, ABS_BRAKE, 
			parms[0], parms[1], parms[2], parms[3]);
		break;
	case ECOMPASS_IOC_GET_MPARMS:
		break;
	case ECOMPASS_IOC_SET_OPARMS_YAW:
		if (copy_from_user(parms, pa, sizeof(parms)))
			return -EFAULT;
		/* orientation yaw */
		input_set_abs_params(ecs_data_device, ABS_RX, 
			parms[0], parms[1], parms[2], parms[3]);
		break;
	case ECOMPASS_IOC_GET_OPARMS_YAW:
		break;
	case ECOMPASS_IOC_SET_OPARMS_PITCH:
		if (copy_from_user(parms, pa, sizeof(parms)))
			return -EFAULT;
		/* orientation pitch */
		input_set_abs_params(ecs_data_device, ABS_RY, 
			parms[0], parms[1], parms[2], parms[3]);
		break;
	case ECOMPASS_IOC_GET_OPARMS_PITCH:
		break;
	case ECOMPASS_IOC_SET_OPARMS_ROLL:
		if (copy_from_user(parms, pa, sizeof(parms)))
			return -EFAULT;
		/* orientation roll */
		input_set_abs_params(ecs_data_device, ABS_RZ, 
			parms[0], parms[1], parms[2], parms[3]);
		break;
	case ECOMPASS_IOC_GET_OPARMS_ROLL:
		break;

	case ECOMPASS_IOC_SET_YPR:
		if (copy_from_user(ypr, pa, sizeof(ypr)))
			return -EFAULT;
		/* Report acceleration sensor information */
		if (atomic_read(&a_flag)) {
			input_report_abs(ecs_data_device, ABS_X, ypr[0]);
			input_report_abs(ecs_data_device, ABS_Y, ypr[1]);
			input_report_abs(ecs_data_device, ABS_Z, ypr[2]);
			input_report_abs(ecs_data_device, ABS_WHEEL, ypr[3]);
			//printk(KERN_INFO"acc x:%d  y:%d z:%d\n", ypr[0], ypr[1], ypr[2]);
		}

		/* Report magnetic sensor information */
		if (atomic_read(&m_flag)) {
			input_report_abs(ecs_data_device, ABS_HAT0X, ypr[4]);
			input_report_abs(ecs_data_device, ABS_HAT0Y, ypr[5]);
			input_report_abs(ecs_data_device, ABS_BRAKE, ypr[6]);
			input_report_abs(ecs_data_device, ABS_GAS, ypr[7]);
			//printk(KERN_INFO"mag x:%d  y:%d z:%d\n", ypr[4], ypr[5], ypr[6]);
		}

		/* Report orientation information */
		if (atomic_read(&o_flag)) {
			input_report_abs(ecs_data_device, ABS_RX, ypr[8]);
			input_report_abs(ecs_data_device, ABS_RY, ypr[9]);
			input_report_abs(ecs_data_device, ABS_RZ, ypr[10]);
			input_report_abs(ecs_data_device, ABS_RUDDER, ypr[11]);
			//printk(KERN_INFO"ori x:%d  y:%d z:%d\n", ypr[8], ypr[9], ypr[10]);
		}

		input_sync(ecs_data_device);
		break;

	default:
		break;
	}

	return 0;
}

static ssize_t ecs_ctrl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	sprintf(buf, "ecompass_ctrl");
	ret = strlen(buf) + 1;

	return ret;
}

static DEVICE_ATTR(ecs_ctrl, S_IRUGO, ecs_ctrl_show, NULL);

static int __init ecompass_init(void)
{
	int res = 0;

	pr_info("ecompass driver: init\n");

	ecs_data_device = input_allocate_device();
	if (!ecs_data_device) {
		res = -ENOMEM;
		pr_err("%s: failed to allocate input device\n", __FUNCTION__);
		goto out;
	}

	set_bit(EV_ABS, ecs_data_device->evbit);

	/* 32768 == 1g, range -4g ~ +4g */
	/* acceleration x-axis */
	input_set_abs_params(ecs_data_device, ABS_X, 
		-32768*4, 32768*4, 0, 0);
	/* acceleration y-axis */
	input_set_abs_params(ecs_data_device, ABS_Y, 
		-32768*4, 32768*4, 0, 0);
	/* acceleration z-axis */
	input_set_abs_params(ecs_data_device, ABS_Z, 
		-32768*4, 32768*4, 0, 0);

	/* 32768 == 1gauss, range -4gauss ~ +4gauss */
	/* magnetic raw x-axis */
	input_set_abs_params(ecs_data_device, ABS_HAT0X, 
		-32768*4, 32768*4, 0, 0);
	/* magnetic raw y-axis */
	input_set_abs_params(ecs_data_device, ABS_HAT0Y, 
		-32768*4, 32768*4, 0, 0);
	/* magnetic raw z-axis */
	input_set_abs_params(ecs_data_device, ABS_BRAKE, 
		-32768*4, 32768*4, 0, 0);

	/* 65536 == 360degree */
	/* orientation yaw, 0 ~ 360 */
	input_set_abs_params(ecs_data_device, ABS_RX, 
		0, 65536, 0, 0);
	/* orientation pitch, -180 ~ 180 */
	input_set_abs_params(ecs_data_device, ABS_RY, 
		-65536/2, 65536/2, 0, 0);
	/* orientation roll, -90 ~ 90 */
	input_set_abs_params(ecs_data_device, ABS_RZ, 
		-65536/4, 65536/4, 0, 0);

	ecs_data_device->name = ECS_DATA_DEV_NAME;
	res = input_register_device(ecs_data_device);
	if (res) {
		pr_err("%s: unable to register input device: %s\n",
			__FUNCTION__, ecs_data_device->name);
		goto out_free_input;
	}

	res = misc_register(&ecs_ctrl_device);
	if (res) {
		pr_err("%s: ecs_ctrl_device register failed\n", __FUNCTION__);
		goto out_free_input;
	}
	res = device_create_file(ecs_ctrl_device.this_device, &dev_attr_ecs_ctrl);
	if (res) {
		pr_err("%s: device_create_file failed\n", __FUNCTION__);
		goto out_deregister_misc;
	}

	return 0;

out_deregister_misc:
	misc_deregister(&ecs_ctrl_device);
out_free_input:
	input_free_device(ecs_data_device);
out:
	return res;
}

static void __exit ecompass_exit(void)
{
	pr_info("ecompass driver: exit\n");
	device_remove_file(ecs_ctrl_device.this_device, &dev_attr_ecs_ctrl);
	misc_deregister(&ecs_ctrl_device);
	input_free_device(ecs_data_device);
}

module_init(ecompass_init);
module_exit(ecompass_exit);

MODULE_AUTHOR("Robbie Cao<hjcao@memsic.com>");
MODULE_DESCRIPTION("MEMSIC eCompass Driver");
MODULE_LICENSE("GPL");

