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
#include <linux/timer.h>
#include <linux/slab.h>

#include <linux/mpu.h>
#include "mltypes.h"
#include "timerirq.h"

/* function which gets timer data and sends it to TIMER */
struct timerirq_data {
	int pid;
	int data_ready;
	int run;
	int timeout;
	unsigned long period;
	struct mpuirq_data data;
	struct completion timer_done;
	wait_queue_head_t timerirq_wait;
	struct timer_list timer;
	struct miscdevice *dev;
};

static struct miscdevice *timerirq_dev_data;

static void timerirq_handler(unsigned long arg)
{
	struct timerirq_data *data = (struct timerirq_data *)arg;
	struct timeval irqtime;

	data->data.interruptcount++;

	data->data_ready = 1;

	do_gettimeofday(&irqtime);
	data->data.irqtime = (((long long)irqtime.tv_sec) << 32);
	data->data.irqtime += irqtime.tv_usec;
	data->data.data_type |= 1;
	
	dev_dbg(data->dev->this_device,
		"%s, %lld, %ld\n", __func__, data->data.irqtime,
		(unsigned long)data);

	wake_up_interruptible(&data->timerirq_wait);

	if (data->run)
		mod_timer(&data->timer,
			  jiffies + msecs_to_jiffies(data->period));
	else
		complete(&data->timer_done);
}

static int start_timerirq(struct timerirq_data *data)
{
	dev_dbg(data->dev->this_device,
		"%s current->pid %d\n", __func__, current->pid);

	/* Timer already running... success */
	if (data->run)
		return 0;

	/* Don't allow a period of 0 since this would fire constantly */
	if (!data->period)
		return -EINVAL;

	data->run = true;
	data->data_ready = false;

	init_completion(&data->timer_done);

	return mod_timer(&data->timer,
			 jiffies + msecs_to_jiffies(data->period));
}

static int stop_timerirq(struct timerirq_data *data)
{
	dev_dbg(data->dev->this_device,
		"%s current->pid %lx\n", __func__, (unsigned long)data);

	if (data->run) {
		data->run = false;
		mod_timer(&data->timer, jiffies + 1);
		wait_for_completion(&data->timer_done);
	}
	return 0;
}

/* The following depends on patch fa1f68db6ca7ebb6fc4487ac215bffba06c01c28
 * drivers: misc: pass miscdevice pointer via file private data
 */
static int timerirq_open(struct inode *inode, struct file *file)
{
	/* Device node is availabe in the file->private_data, this is
	 * exactly what we want so we leave it there */
	struct miscdevice *dev_data = file->private_data;
	struct timerirq_data *data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev_data;
	file->private_data = data;
	data->pid = current->pid;
	init_waitqueue_head(&data->timerirq_wait);

	setup_timer(&data->timer, timerirq_handler, (unsigned long)data);
	dev_dbg(data->dev->this_device,
		"%s current->pid %d\n", __func__, current->pid);
	return 0;
}

static int timerirq_release(struct inode *inode, struct file *file)
{
	struct timerirq_data *data = file->private_data;
	dev_dbg(data->dev->this_device, "timerirq_release\n");
	if (data->run)
		stop_timerirq(data);
	kfree(data);
	return 0;
}

/* read function called when from /dev/timerirq is read */
static ssize_t timerirq_read(struct file *file,
			     char *buf, size_t count, loff_t *ppos)
{
	int len, err;
	struct timerirq_data *data = file->private_data;

	if (!data->data_ready && data->timeout &&
	    !(file->f_flags & O_NONBLOCK)) {
		wait_event_interruptible_timeout(data->timerirq_wait,
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
		dev_err(data->dev->this_device,
			"Copy to user returned %d\n", err);
		return -EFAULT;
	}
	data->data_ready = 0;
	len = sizeof(data->data);
	return len;
}

static unsigned int timerirq_poll(struct file *file,
				  struct poll_table_struct *poll)
{
	int mask = 0;
	struct timerirq_data *data = file->private_data;

	poll_wait(file, &data->timerirq_wait, poll);
	if (data->data_ready)
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

/* ioctl - I/O control */
static long timerirq_ioctl(struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	int tmp;
	struct timerirq_data *data = file->private_data;

	dev_dbg(data->dev->this_device,
		"%s current->pid %d, %d, %ld\n",
		__func__, current->pid, cmd, arg);

	
	if (!data)
		return -EFAULT;

	switch (cmd) {
	case TIMERIRQ_SET_TIMEOUT:
		data->timeout = arg;
		break;
	case TIMERIRQ_GET_INTERRUPT_CNT:
		tmp = data->data.interruptcount - 1;
		if (data->data.interruptcount > 1)
			data->data.interruptcount = 1;

		if (copy_to_user((int *)arg, &tmp, sizeof(int)))
			return -EFAULT;
		break;
	case TIMERIRQ_START:
		data->period = arg;
		retval = start_timerirq(data);
		break;
	case TIMERIRQ_STOP:
		retval = stop_timerirq(data);
		break;
	default:
		retval = -EINVAL;
	}
	return retval;
}

/* define which file operations are supported */
static const struct file_operations timerirq_fops = {
	.owner = THIS_MODULE,
	.read = timerirq_read,
	.poll = timerirq_poll,

#if HAVE_COMPAT_IOCTL
	.compat_ioctl = timerirq_ioctl,
#endif
#if HAVE_UNLOCKED_IOCTL
	.unlocked_ioctl = timerirq_ioctl,
#endif
	.open = timerirq_open,
	.release = timerirq_release,
};

static int __init timerirq_init(void)
{

	int res;
	static struct miscdevice *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	timerirq_dev_data = data;
	data->minor = MISC_DYNAMIC_MINOR;
	data->name = "timerirq";
	data->fops = &timerirq_fops;

	res = misc_register(data);
	if (res < 0) {
		dev_err(data->this_device, "misc_register returned %d\n", res);
		return res;
	}

	return res;
}

module_init(timerirq_init);

static void __exit timerirq_exit(void)
{
	struct miscdevice *data = timerirq_dev_data;

	dev_info(data->this_device, "Unregistering %s\n", data->name);

	misc_deregister(data);
	kfree(data);

	timerirq_dev_data = NULL;
}

module_exit(timerirq_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Timer IRQ device driver.");
MODULE_LICENSE("GPL");
MODULE_ALIAS("timerirq");
