/*
 * userio kernel serio device emulation module
 * Copyright (C) 2015 Red Hat
 * Copyright (C) 2015 Stephen Chandler Paul <thatslyude@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser
 * General Public License for more details.
 */

#include <linux/circ_buf.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/serio.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <uapi/linux/userio.h>

#define USERIO_NAME		"userio"
#define USERIO_BUFSIZE		16

static struct miscdevice userio_misc;

struct userio_device {
	struct serio *serio;
	struct mutex mutex;

	bool running;

	u8 head;
	u8 tail;

	spinlock_t buf_lock;
	unsigned char buf[USERIO_BUFSIZE];

	wait_queue_head_t waitq;
};

/**
 * userio_device_write - Write data from serio to a userio device in userspace
 * @id: The serio port for the userio device
 * @val: The data to write to the device
 */
static int userio_device_write(struct serio *id, unsigned char val)
{
	struct userio_device *userio = id->port_data;

	scoped_guard(spinlock_irqsave, &userio->buf_lock) {
		userio->buf[userio->head] = val;
		userio->head = (userio->head + 1) % USERIO_BUFSIZE;

		if (userio->head == userio->tail)
			dev_warn(userio_misc.this_device,
				 "Buffer overflowed, userio client isn't keeping up");
	}

	wake_up_interruptible(&userio->waitq);

	return 0;
}

static int userio_char_open(struct inode *inode, struct file *file)
{
	struct userio_device *userio __free(kfree) =
			kzalloc(sizeof(*userio), GFP_KERNEL);
	if (!userio)
		return -ENOMEM;

	mutex_init(&userio->mutex);
	spin_lock_init(&userio->buf_lock);
	init_waitqueue_head(&userio->waitq);

	userio->serio = kzalloc(sizeof(*userio->serio), GFP_KERNEL);
	if (!userio->serio)
		return -ENOMEM;

	userio->serio->write = userio_device_write;
	userio->serio->port_data = userio;

	file->private_data = no_free_ptr(userio);

	return 0;
}

static int userio_char_release(struct inode *inode, struct file *file)
{
	struct userio_device *userio = file->private_data;

	if (userio->running) {
		/*
		 * Don't free the serio port here, serio_unregister_port()
		 * does it for us.
		 */
		serio_unregister_port(userio->serio);
	} else {
		kfree(userio->serio);
	}

	kfree(userio);

	return 0;
}

static size_t userio_fetch_data(struct userio_device *userio, u8 *buf,
				size_t count, size_t *copylen)
{
	size_t available, len;

	guard(spinlock_irqsave)(&userio->buf_lock);

	available = CIRC_CNT_TO_END(userio->head, userio->tail,
				    USERIO_BUFSIZE);
	len = min(available, count);
	if (len) {
		memcpy(buf, &userio->buf[userio->tail], len);
		userio->tail = (userio->tail + len) % USERIO_BUFSIZE;
	}

	*copylen = len;
	return available;
}

static ssize_t userio_char_read(struct file *file, char __user *user_buffer,
				size_t count, loff_t *ppos)
{
	struct userio_device *userio = file->private_data;
	int error;
	size_t available, copylen;
	u8 buf[USERIO_BUFSIZE];

	/*
	 * By the time we get here, the data that was waiting might have
	 * been taken by another thread. Grab the buffer lock and check if
	 * there's still any data waiting, otherwise repeat this process
	 * until we have data (unless the file descriptor is non-blocking
	 * of course).
	 */
	for (;;) {
		available = userio_fetch_data(userio, buf, count, &copylen);
		if (available)
			break;

		/* buffer was/is empty */
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		/*
		 * count == 0 is special - no IO is done but we check
		 * for error conditions (see above).
		 */
		if (count == 0)
			return 0;

		error = wait_event_interruptible(userio->waitq,
						 userio->head != userio->tail);
		if (error)
			return error;
	}

	if (copylen)
		if (copy_to_user(user_buffer, buf, copylen))
			return -EFAULT;

	return copylen;
}

static int userio_execute_cmd(struct userio_device *userio,
			      const struct userio_cmd *cmd)
{
	switch (cmd->type) {
	case USERIO_CMD_REGISTER:
		if (!userio->serio->id.type) {
			dev_warn(userio_misc.this_device,
				 "No port type given on /dev/userio\n");
			return -EINVAL;
		}

		if (userio->running) {
			dev_warn(userio_misc.this_device,
				 "Begin command sent, but we're already running\n");
			return -EBUSY;
		}

		userio->running = true;
		serio_register_port(userio->serio);
		break;

	case USERIO_CMD_SET_PORT_TYPE:
		if (userio->running) {
			dev_warn(userio_misc.this_device,
				 "Can't change port type on an already running userio instance\n");
			return -EBUSY;
		}

		userio->serio->id.type = cmd->data;
		break;

	case USERIO_CMD_SEND_INTERRUPT:
		if (!userio->running) {
			dev_warn(userio_misc.this_device,
				 "The device must be registered before sending interrupts\n");
			return -ENODEV;
		}

		serio_interrupt(userio->serio, cmd->data, 0);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static ssize_t userio_char_write(struct file *file, const char __user *buffer,
				 size_t count, loff_t *ppos)
{
	struct userio_device *userio = file->private_data;
	struct userio_cmd cmd;
	int error;

	if (count != sizeof(cmd)) {
		dev_warn(userio_misc.this_device, "Invalid payload size\n");
		return -EINVAL;
	}

	if (copy_from_user(&cmd, buffer, sizeof(cmd)))
		return -EFAULT;

	scoped_cond_guard(mutex_intr, return -EINTR, &userio->mutex) {
		error = userio_execute_cmd(userio, &cmd);
		if (error)
			return error;
	}

	return count;
}

static __poll_t userio_char_poll(struct file *file, poll_table *wait)
{
	struct userio_device *userio = file->private_data;

	poll_wait(file, &userio->waitq, wait);

	if (userio->head != userio->tail)
		return EPOLLIN | EPOLLRDNORM;

	return 0;
}

static const struct file_operations userio_fops = {
	.owner		= THIS_MODULE,
	.open		= userio_char_open,
	.release	= userio_char_release,
	.read		= userio_char_read,
	.write		= userio_char_write,
	.poll		= userio_char_poll,
};

static struct miscdevice userio_misc = {
	.fops	= &userio_fops,
	.minor	= USERIO_MINOR,
	.name	= USERIO_NAME,
};
module_driver(userio_misc, misc_register, misc_deregister);

MODULE_ALIAS_MISCDEV(USERIO_MINOR);
MODULE_ALIAS("devname:" USERIO_NAME);

MODULE_AUTHOR("Stephen Chandler Paul <thatslyude@gmail.com>");
MODULE_DESCRIPTION("Virtual Serio Device Support");
MODULE_LICENSE("GPL");
