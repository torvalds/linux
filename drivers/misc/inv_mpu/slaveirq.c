/*
	$License:
	Copyright (C) 2011 InvenSense Corporation, All Rights Reserved.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
	$
 */
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/irq.h>
#include <linux/signal.h>
#include <linux/miscdevice.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/poll.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/slab.h>

#include <linux/mpu.h>
#include "slaveirq.h"
#include "mldl_cfg.h"

/* function which gets slave data and sends it to SLAVE */

struct slaveirq_dev_data {
	struct miscdevice dev;
	struct i2c_client *slave_client;
	struct mpuirq_data data;
	wait_queue_head_t slaveirq_wait;
	int irq;
	int pid;
	int data_ready;
	int timeout;
};

/* The following depends on patch fa1f68db6ca7ebb6fc4487ac215bffba06c01c28
 * drivers: misc: pass miscdevice pointer via file private data
 */
static int slaveirq_open(struct inode *inode, struct file *file)
{
	/* Device node is availabe in the file->private_data, this is
	 * exactly what we want so we leave it there */
	struct slaveirq_dev_data *data =
	    container_of(file->private_data, struct slaveirq_dev_data, dev);

	dev_dbg(data->dev.this_device,
		"%s current->pid %d\n", __func__, current->pid);
	data->pid = current->pid;
	return 0;
}

static int slaveirq_release(struct inode *inode, struct file *file)
{
	struct slaveirq_dev_data *data =
	    container_of(file->private_data, struct slaveirq_dev_data, dev);
	dev_dbg(data->dev.this_device, "slaveirq_release\n");
	return 0;
}

/* read function called when from /dev/slaveirq is read */
static ssize_t slaveirq_read(struct file *file,
			     char *buf, size_t count, loff_t *ppos)
{
	int len, err;
	struct slaveirq_dev_data *data =
	    container_of(file->private_data, struct slaveirq_dev_data, dev);

	if (!data->data_ready && data->timeout &&
	    !(file->f_flags & O_NONBLOCK)) {
		wait_event_interruptible_timeout(data->slaveirq_wait,
						 data->data_ready,
						 data->timeout);
	}

	if (data->data_ready && NULL != buf && count >= sizeof(data->data)) {
		err = copy_to_user(buf, &data->data, sizeof(data->data));
		data->data.data_type = 0;
	} else {
		return 0;
	}
	if (err != 0) {
		dev_err(data->dev.this_device,
			"Copy to user returned %d\n", err);
		return -EFAULT;
	}
	data->data_ready = 0;
	len = sizeof(data->data);
	return len;
}

static unsigned int slaveirq_poll(struct file *file,
				  struct poll_table_struct *poll)
{
	int mask = 0;
	struct slaveirq_dev_data *data =
	    container_of(file->private_data, struct slaveirq_dev_data, dev);

	poll_wait(file, &data->slaveirq_wait, poll);
	if (data->data_ready)
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

/* ioctl - I/O control */
static long slaveirq_ioctl(struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	int tmp;
	struct slaveirq_dev_data *data =
	    container_of(file->private_data, struct slaveirq_dev_data, dev);

	switch (cmd) {
	case SLAVEIRQ_SET_TIMEOUT:
		data->timeout = arg;
		break;

	case SLAVEIRQ_GET_INTERRUPT_CNT:
		tmp = data->data.interruptcount - 1;
		if (data->data.interruptcount > 1)
			data->data.interruptcount = 1;

		if (copy_to_user((int *)arg, &tmp, sizeof(int)))
			return -EFAULT;
		break;
	case SLAVEIRQ_GET_IRQ_TIME:
		if (copy_to_user((int *)arg, &data->data.irqtime,
				 sizeof(data->data.irqtime)))
			return -EFAULT;
		data->data.irqtime = 0;
		break;
	default:
		retval = -EINVAL;
	}
	return retval;
}

static irqreturn_t slaveirq_handler(int irq, void *dev_id)
{
	struct slaveirq_dev_data *data = (struct slaveirq_dev_data *)dev_id;
	static int mycount;
	struct timeval irqtime;
	mycount++;

	data->data.interruptcount++;

	/* wake up (unblock) for reading data from userspace */
	data->data_ready = 1;

	do_gettimeofday(&irqtime);
	data->data.irqtime = (((long long)irqtime.tv_sec) << 32);
	data->data.irqtime += irqtime.tv_usec;
	data->data.data_type |= 1;

	wake_up_interruptible(&data->slaveirq_wait);

	return IRQ_HANDLED;

}

/* define which file operations are supported */
static const struct file_operations slaveirq_fops = {
	.owner = THIS_MODULE,
	.read = slaveirq_read,
	.poll = slaveirq_poll,

#if HAVE_COMPAT_IOCTL
	.compat_ioctl = slaveirq_ioctl,
#endif
#if HAVE_UNLOCKED_IOCTL
	.unlocked_ioctl = slaveirq_ioctl,
#endif
	.open = slaveirq_open,
	.release = slaveirq_release,
};

int slaveirq_init(struct i2c_adapter *slave_adapter,
		  struct ext_slave_platform_data *pdata, char *name)
{

	int res;
	struct slaveirq_dev_data *data;

	if (!pdata->irq)
		return -EINVAL;

	pdata->irq_data = kzalloc(sizeof(*data), GFP_KERNEL);
	data = (struct slaveirq_dev_data *)pdata->irq_data;
	if (!data)
		return -ENOMEM;

	data->dev.minor = MISC_DYNAMIC_MINOR;
	data->dev.name = name;
	data->dev.fops = &slaveirq_fops;
	data->irq = pdata->irq;
	data->pid = 0;
	data->data_ready = 0;
	data->timeout = 0;

	init_waitqueue_head(&data->slaveirq_wait);

	res = request_irq(data->irq, slaveirq_handler,
			IRQF_TRIGGER_RISING | IRQF_SHARED,
			  data->dev.name, data);

	if (res) {
		dev_err(&slave_adapter->dev,
			"myirqtest: cannot register IRQ %d\n", data->irq);
		goto out_request_irq;
	}

	res = misc_register(&data->dev);
	if (res < 0) {
		dev_err(&slave_adapter->dev,
			"misc_register returned %d\n", res);
		goto out_misc_register;
	}

	return res;

out_misc_register:
	free_irq(data->irq, data);
out_request_irq:
	kfree(pdata->irq_data);
	pdata->irq_data = NULL;

	return res;
}

void slaveirq_exit(struct ext_slave_platform_data *pdata)
{
	struct slaveirq_dev_data *data = pdata->irq_data;

	if (!pdata->irq_data || data->irq <= 0)
		return;

	dev_info(data->dev.this_device, "Unregistering %s\n", data->dev.name);

	free_irq(data->irq, data);
	misc_deregister(&data->dev);
	kfree(pdata->irq_data);
	pdata->irq_data = NULL;
}
