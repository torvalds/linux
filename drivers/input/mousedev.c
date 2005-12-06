/*
 * Input driver to ExplorerPS/2 device driver module.
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 * Copyright (c) 2004      Dmitry Torokhov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#define MOUSEDEV_MINOR_BASE	32
#define MOUSEDEV_MINORS		32
#define MOUSEDEV_MIX		31

#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/config.h>
#include <linux/smp_lock.h>
#include <linux/random.h>
#include <linux/major.h>
#include <linux/device.h>
#ifdef CONFIG_INPUT_MOUSEDEV_PSAUX
#include <linux/miscdevice.h>
#endif

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Mouse (ExplorerPS/2) device interfaces");
MODULE_LICENSE("GPL");

#ifndef CONFIG_INPUT_MOUSEDEV_SCREEN_X
#define CONFIG_INPUT_MOUSEDEV_SCREEN_X	1024
#endif
#ifndef CONFIG_INPUT_MOUSEDEV_SCREEN_Y
#define CONFIG_INPUT_MOUSEDEV_SCREEN_Y	768
#endif

static int xres = CONFIG_INPUT_MOUSEDEV_SCREEN_X;
module_param(xres, uint, 0);
MODULE_PARM_DESC(xres, "Horizontal screen resolution");

static int yres = CONFIG_INPUT_MOUSEDEV_SCREEN_Y;
module_param(yres, uint, 0);
MODULE_PARM_DESC(yres, "Vertical screen resolution");

static unsigned tap_time = 200;
module_param(tap_time, uint, 0);
MODULE_PARM_DESC(tap_time, "Tap time for touchpads in absolute mode (msecs)");

struct mousedev_hw_data {
	int dx, dy, dz;
	int x, y;
	int abs_event;
	unsigned long buttons;
};

struct mousedev {
	int exist;
	int open;
	int minor;
	char name[16];
	wait_queue_head_t wait;
	struct list_head list;
	struct input_handle handle;

	struct mousedev_hw_data packet;
	unsigned int pkt_count;
	int old_x[4], old_y[4];
	int frac_dx, frac_dy;
	unsigned long touch;
};

enum mousedev_emul {
	MOUSEDEV_EMUL_PS2,
	MOUSEDEV_EMUL_IMPS,
	MOUSEDEV_EMUL_EXPS
};

struct mousedev_motion {
	int dx, dy, dz;
	unsigned long buttons;
};

#define PACKET_QUEUE_LEN	16
struct mousedev_list {
	struct fasync_struct *fasync;
	struct mousedev *mousedev;
	struct list_head node;

	struct mousedev_motion packets[PACKET_QUEUE_LEN];
	unsigned int head, tail;
	spinlock_t packet_lock;
	int pos_x, pos_y;

	signed char ps2[6];
	unsigned char ready, buffer, bufsiz;
	unsigned char imexseq, impsseq;
	enum mousedev_emul mode;
	unsigned long last_buttons;
};

#define MOUSEDEV_SEQ_LEN	6

static unsigned char mousedev_imps_seq[] = { 0xf3, 200, 0xf3, 100, 0xf3, 80 };
static unsigned char mousedev_imex_seq[] = { 0xf3, 200, 0xf3, 200, 0xf3, 80 };

static struct input_handler mousedev_handler;

static struct mousedev *mousedev_table[MOUSEDEV_MINORS];
static struct mousedev mousedev_mix;

#define fx(i)  (mousedev->old_x[(mousedev->pkt_count - (i)) & 03])
#define fy(i)  (mousedev->old_y[(mousedev->pkt_count - (i)) & 03])

static void mousedev_touchpad_event(struct input_dev *dev, struct mousedev *mousedev, unsigned int code, int value)
{
	int size, tmp;
	enum { FRACTION_DENOM = 128 };

	if (mousedev->touch) {
		size = dev->absmax[ABS_X] - dev->absmin[ABS_X];
		if (size == 0) size = 256 * 2;
		switch (code) {
			case ABS_X:
				fx(0) = value;
				if (mousedev->pkt_count >= 2) {
					tmp = ((value - fx(2)) * (256 * FRACTION_DENOM)) / size;
					tmp += mousedev->frac_dx;
					mousedev->packet.dx = tmp / FRACTION_DENOM;
					mousedev->frac_dx = tmp - mousedev->packet.dx * FRACTION_DENOM;
				}
				break;

			case ABS_Y:
				fy(0) = value;
				if (mousedev->pkt_count >= 2) {
					tmp = -((value - fy(2)) * (256 * FRACTION_DENOM)) / size;
					tmp += mousedev->frac_dy;
					mousedev->packet.dy = tmp / FRACTION_DENOM;
					mousedev->frac_dy = tmp - mousedev->packet.dy * FRACTION_DENOM;
				}
				break;
		}
	}
}

static void mousedev_abs_event(struct input_dev *dev, struct mousedev *mousedev, unsigned int code, int value)
{
	int size;

	switch (code) {
		case ABS_X:
			size = dev->absmax[ABS_X] - dev->absmin[ABS_X];
			if (size == 0) size = xres;
			if (value > dev->absmax[ABS_X]) value = dev->absmax[ABS_X];
			if (value < dev->absmin[ABS_X]) value = dev->absmin[ABS_X];
			mousedev->packet.x = ((value - dev->absmin[ABS_X]) * xres) / size;
			mousedev->packet.abs_event = 1;
			break;

		case ABS_Y:
			size = dev->absmax[ABS_Y] - dev->absmin[ABS_Y];
			if (size == 0) size = yres;
			if (value > dev->absmax[ABS_Y]) value = dev->absmax[ABS_Y];
			if (value < dev->absmin[ABS_Y]) value = dev->absmin[ABS_Y];
			mousedev->packet.y = yres - ((value - dev->absmin[ABS_Y]) * yres) / size;
			mousedev->packet.abs_event = 1;
			break;
	}
}

static void mousedev_rel_event(struct mousedev *mousedev, unsigned int code, int value)
{
	switch (code) {
		case REL_X:	mousedev->packet.dx += value; break;
		case REL_Y:	mousedev->packet.dy -= value; break;
		case REL_WHEEL:	mousedev->packet.dz -= value; break;
	}
}

static void mousedev_key_event(struct mousedev *mousedev, unsigned int code, int value)
{
	int index;

	switch (code) {
		case BTN_TOUCH:
		case BTN_0:
		case BTN_FORWARD:
		case BTN_LEFT:		index = 0; break;
		case BTN_STYLUS:
		case BTN_1:
		case BTN_RIGHT:		index = 1; break;
		case BTN_2:
		case BTN_STYLUS2:
		case BTN_MIDDLE:	index = 2; break;
		case BTN_3:
		case BTN_BACK:
		case BTN_SIDE:		index = 3; break;
		case BTN_4:
		case BTN_EXTRA:		index = 4; break;
		default: 		return;
	}

	if (value) {
		set_bit(index, &mousedev->packet.buttons);
		set_bit(index, &mousedev_mix.packet.buttons);
	} else {
		clear_bit(index, &mousedev->packet.buttons);
		clear_bit(index, &mousedev_mix.packet.buttons);
	}
}

static void mousedev_notify_readers(struct mousedev *mousedev, struct mousedev_hw_data *packet)
{
	struct mousedev_list *list;
	struct mousedev_motion *p;
	unsigned long flags;
	int wake_readers = 0;

	list_for_each_entry(list, &mousedev->list, node) {
		spin_lock_irqsave(&list->packet_lock, flags);

		p = &list->packets[list->head];
		if (list->ready && p->buttons != mousedev->packet.buttons) {
			unsigned int new_head = (list->head + 1) % PACKET_QUEUE_LEN;
			if (new_head != list->tail) {
				p = &list->packets[list->head = new_head];
				memset(p, 0, sizeof(struct mousedev_motion));
			}
		}

		if (packet->abs_event) {
			p->dx += packet->x - list->pos_x;
			p->dy += packet->y - list->pos_y;
			list->pos_x = packet->x;
			list->pos_y = packet->y;
		}

		list->pos_x += packet->dx;
		list->pos_x = list->pos_x < 0 ? 0 : (list->pos_x >= xres ? xres : list->pos_x);
		list->pos_y += packet->dy;
		list->pos_y = list->pos_y < 0 ? 0 : (list->pos_y >= yres ? yres : list->pos_y);

		p->dx += packet->dx;
		p->dy += packet->dy;
		p->dz += packet->dz;
		p->buttons = mousedev->packet.buttons;

		if (p->dx || p->dy || p->dz || p->buttons != list->last_buttons)
			list->ready = 1;

		spin_unlock_irqrestore(&list->packet_lock, flags);

		if (list->ready) {
			kill_fasync(&list->fasync, SIGIO, POLL_IN);
			wake_readers = 1;
		}
	}

	if (wake_readers)
		wake_up_interruptible(&mousedev->wait);
}

static void mousedev_touchpad_touch(struct mousedev *mousedev, int value)
{
	if (!value) {
		if (mousedev->touch &&
		    time_before(jiffies, mousedev->touch + msecs_to_jiffies(tap_time))) {
			/*
			 * Toggle left button to emulate tap.
			 * We rely on the fact that mousedev_mix always has 0
			 * motion packet so we won't mess current position.
			 */
			set_bit(0, &mousedev->packet.buttons);
			set_bit(0, &mousedev_mix.packet.buttons);
			mousedev_notify_readers(mousedev, &mousedev_mix.packet);
			mousedev_notify_readers(&mousedev_mix, &mousedev_mix.packet);
			clear_bit(0, &mousedev->packet.buttons);
			clear_bit(0, &mousedev_mix.packet.buttons);
		}
		mousedev->touch = mousedev->pkt_count = 0;
		mousedev->frac_dx = 0;
		mousedev->frac_dy = 0;
	}
	else
		if (!mousedev->touch)
			mousedev->touch = jiffies;
}

static void mousedev_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	struct mousedev *mousedev = handle->private;

	switch (type) {
		case EV_ABS:
			/* Ignore joysticks */
			if (test_bit(BTN_TRIGGER, handle->dev->keybit))
				return;

			if (test_bit(BTN_TOOL_FINGER, handle->dev->keybit))
				mousedev_touchpad_event(handle->dev, mousedev, code, value);
			else
				mousedev_abs_event(handle->dev, mousedev, code, value);

			break;

		case EV_REL:
			mousedev_rel_event(mousedev, code, value);
			break;

		case EV_KEY:
			if (value != 2) {
				if (code == BTN_TOUCH && test_bit(BTN_TOOL_FINGER, handle->dev->keybit))
					mousedev_touchpad_touch(mousedev, value);
				else
					mousedev_key_event(mousedev, code, value);
			}
			break;

		case EV_SYN:
			if (code == SYN_REPORT) {
				if (mousedev->touch) {
					mousedev->pkt_count++;
					/* Input system eats duplicate events, but we need all of them
					 * to do correct averaging so apply present one forward
			 		 */
					fx(0) = fx(1);
					fy(0) = fy(1);
				}

				mousedev_notify_readers(mousedev, &mousedev->packet);
				mousedev_notify_readers(&mousedev_mix, &mousedev->packet);

				mousedev->packet.dx = mousedev->packet.dy = mousedev->packet.dz = 0;
				mousedev->packet.abs_event = 0;
			}
			break;
	}
}

static int mousedev_fasync(int fd, struct file *file, int on)
{
	int retval;
	struct mousedev_list *list = file->private_data;
	retval = fasync_helper(fd, file, on, &list->fasync);
	return retval < 0 ? retval : 0;
}

static void mousedev_free(struct mousedev *mousedev)
{
	mousedev_table[mousedev->minor] = NULL;
	kfree(mousedev);
}

static int mixdev_release(void)
{
	struct input_handle *handle;

	list_for_each_entry(handle, &mousedev_handler.h_list, h_node) {
		struct mousedev *mousedev = handle->private;

		if (!mousedev->open) {
			if (mousedev->exist)
				input_close_device(&mousedev->handle);
			else
				mousedev_free(mousedev);
		}
	}

	return 0;
}

static int mousedev_release(struct inode * inode, struct file * file)
{
	struct mousedev_list *list = file->private_data;

	mousedev_fasync(-1, file, 0);

	list_del(&list->node);

	if (!--list->mousedev->open) {
		if (list->mousedev->minor == MOUSEDEV_MIX)
			return mixdev_release();

		if (!mousedev_mix.open) {
			if (list->mousedev->exist)
				input_close_device(&list->mousedev->handle);
			else
				mousedev_free(list->mousedev);
		}
	}

	kfree(list);
	return 0;
}

static int mousedev_open(struct inode * inode, struct file * file)
{
	struct mousedev_list *list;
	struct input_handle *handle;
	struct mousedev *mousedev;
	int i;

#ifdef CONFIG_INPUT_MOUSEDEV_PSAUX
	if (imajor(inode) == MISC_MAJOR)
		i = MOUSEDEV_MIX;
	else
#endif
		i = iminor(inode) - MOUSEDEV_MINOR_BASE;

	if (i >= MOUSEDEV_MINORS || !mousedev_table[i])
		return -ENODEV;

	if (!(list = kmalloc(sizeof(struct mousedev_list), GFP_KERNEL)))
		return -ENOMEM;
	memset(list, 0, sizeof(struct mousedev_list));

	spin_lock_init(&list->packet_lock);
	list->pos_x = xres / 2;
	list->pos_y = yres / 2;
	list->mousedev = mousedev_table[i];
	list_add_tail(&list->node, &mousedev_table[i]->list);
	file->private_data = list;

	if (!list->mousedev->open++) {
		if (list->mousedev->minor == MOUSEDEV_MIX) {
			list_for_each_entry(handle, &mousedev_handler.h_list, h_node) {
				mousedev = handle->private;
				if (!mousedev->open && mousedev->exist)
					input_open_device(handle);
			}
		} else
			if (!mousedev_mix.open && list->mousedev->exist)
				input_open_device(&list->mousedev->handle);
	}

	return 0;
}

static inline int mousedev_limit_delta(int delta, int limit)
{
	return delta > limit ? limit : (delta < -limit ? -limit : delta);
}

static void mousedev_packet(struct mousedev_list *list, signed char *ps2_data)
{
	struct mousedev_motion *p;
	unsigned long flags;

	spin_lock_irqsave(&list->packet_lock, flags);
	p = &list->packets[list->tail];

	ps2_data[0] = 0x08 | ((p->dx < 0) << 4) | ((p->dy < 0) << 5) | (p->buttons & 0x07);
	ps2_data[1] = mousedev_limit_delta(p->dx, 127);
	ps2_data[2] = mousedev_limit_delta(p->dy, 127);
	p->dx -= ps2_data[1];
	p->dy -= ps2_data[2];

	switch (list->mode) {
		case MOUSEDEV_EMUL_EXPS:
			ps2_data[3] = mousedev_limit_delta(p->dz, 7);
			p->dz -= ps2_data[3];
			ps2_data[3] = (ps2_data[3] & 0x0f) | ((p->buttons & 0x18) << 1);
			list->bufsiz = 4;
			break;

		case MOUSEDEV_EMUL_IMPS:
			ps2_data[0] |= ((p->buttons & 0x10) >> 3) | ((p->buttons & 0x08) >> 1);
			ps2_data[3] = mousedev_limit_delta(p->dz, 127);
			p->dz -= ps2_data[3];
			list->bufsiz = 4;
			break;

		case MOUSEDEV_EMUL_PS2:
		default:
			ps2_data[0] |= ((p->buttons & 0x10) >> 3) | ((p->buttons & 0x08) >> 1);
			p->dz = 0;
			list->bufsiz = 3;
			break;
	}

	if (!p->dx && !p->dy && !p->dz) {
		if (list->tail == list->head) {
			list->ready = 0;
			list->last_buttons = p->buttons;
		} else
			list->tail = (list->tail + 1) % PACKET_QUEUE_LEN;
	}

	spin_unlock_irqrestore(&list->packet_lock, flags);
}


static ssize_t mousedev_write(struct file * file, const char __user * buffer, size_t count, loff_t *ppos)
{
	struct mousedev_list *list = file->private_data;
	unsigned char c;
	unsigned int i;

	for (i = 0; i < count; i++) {

		if (get_user(c, buffer + i))
			return -EFAULT;

		if (c == mousedev_imex_seq[list->imexseq]) {
			if (++list->imexseq == MOUSEDEV_SEQ_LEN) {
				list->imexseq = 0;
				list->mode = MOUSEDEV_EMUL_EXPS;
			}
		} else list->imexseq = 0;

		if (c == mousedev_imps_seq[list->impsseq]) {
			if (++list->impsseq == MOUSEDEV_SEQ_LEN) {
				list->impsseq = 0;
				list->mode = MOUSEDEV_EMUL_IMPS;
			}
		} else list->impsseq = 0;

		list->ps2[0] = 0xfa;

		switch (c) {

			case 0xeb: /* Poll */
				mousedev_packet(list, &list->ps2[1]);
				list->bufsiz++; /* account for leading ACK */
				break;

			case 0xf2: /* Get ID */
				switch (list->mode) {
					case MOUSEDEV_EMUL_PS2:  list->ps2[1] = 0; break;
					case MOUSEDEV_EMUL_IMPS: list->ps2[1] = 3; break;
					case MOUSEDEV_EMUL_EXPS: list->ps2[1] = 4; break;
				}
				list->bufsiz = 2;
				break;

			case 0xe9: /* Get info */
				list->ps2[1] = 0x60; list->ps2[2] = 3; list->ps2[3] = 200;
				list->bufsiz = 4;
				break;

			case 0xff: /* Reset */
				list->impsseq = list->imexseq = 0;
				list->mode = MOUSEDEV_EMUL_PS2;
				list->ps2[1] = 0xaa; list->ps2[2] = 0x00;
				list->bufsiz = 3;
				break;

			default:
				list->bufsiz = 1;
				break;
		}

		list->buffer = list->bufsiz;
	}

	kill_fasync(&list->fasync, SIGIO, POLL_IN);

	wake_up_interruptible(&list->mousedev->wait);

	return count;
}

static ssize_t mousedev_read(struct file * file, char __user * buffer, size_t count, loff_t *ppos)
{
	struct mousedev_list *list = file->private_data;
	int retval = 0;

	if (!list->ready && !list->buffer && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	retval = wait_event_interruptible(list->mousedev->wait,
					  !list->mousedev->exist || list->ready || list->buffer);

	if (retval)
		return retval;

	if (!list->mousedev->exist)
		return -ENODEV;

	if (!list->buffer && list->ready) {
		mousedev_packet(list, list->ps2);
		list->buffer = list->bufsiz;
	}

	if (count > list->buffer)
		count = list->buffer;

	list->buffer -= count;

	if (copy_to_user(buffer, list->ps2 + list->bufsiz - list->buffer - count, count))
		return -EFAULT;

	return count;
}

/* No kernel lock - fine */
static unsigned int mousedev_poll(struct file *file, poll_table *wait)
{
	struct mousedev_list *list = file->private_data;
	poll_wait(file, &list->mousedev->wait, wait);
	return ((list->ready || list->buffer) ? (POLLIN | POLLRDNORM) : 0) |
		(list->mousedev->exist ? 0 : (POLLHUP | POLLERR));
}

static struct file_operations mousedev_fops = {
	.owner =	THIS_MODULE,
	.read =		mousedev_read,
	.write =	mousedev_write,
	.poll =		mousedev_poll,
	.open =		mousedev_open,
	.release =	mousedev_release,
	.fasync =	mousedev_fasync,
};

static struct input_handle *mousedev_connect(struct input_handler *handler, struct input_dev *dev, struct input_device_id *id)
{
	struct mousedev *mousedev;
	struct class_device *cdev;
	int minor = 0;

	for (minor = 0; minor < MOUSEDEV_MINORS && mousedev_table[minor]; minor++);
	if (minor == MOUSEDEV_MINORS) {
		printk(KERN_ERR "mousedev: no more free mousedev devices\n");
		return NULL;
	}

	if (!(mousedev = kmalloc(sizeof(struct mousedev), GFP_KERNEL)))
		return NULL;
	memset(mousedev, 0, sizeof(struct mousedev));

	INIT_LIST_HEAD(&mousedev->list);
	init_waitqueue_head(&mousedev->wait);

	mousedev->minor = minor;
	mousedev->exist = 1;
	mousedev->handle.dev = dev;
	mousedev->handle.name = mousedev->name;
	mousedev->handle.handler = handler;
	mousedev->handle.private = mousedev;
	sprintf(mousedev->name, "mouse%d", minor);

	if (mousedev_mix.open)
		input_open_device(&mousedev->handle);

	mousedev_table[minor] = mousedev;

	cdev = class_device_create(&input_class, &dev->cdev,
			MKDEV(INPUT_MAJOR, MOUSEDEV_MINOR_BASE + minor),
			dev->cdev.dev, mousedev->name);

	/* temporary symlink to keep userspace happy */
	sysfs_create_link(&input_class.subsys.kset.kobj, &cdev->kobj,
			  mousedev->name);

	return &mousedev->handle;
}

static void mousedev_disconnect(struct input_handle *handle)
{
	struct mousedev *mousedev = handle->private;
	struct mousedev_list *list;

	sysfs_remove_link(&input_class.subsys.kset.kobj, mousedev->name);
	class_device_destroy(&input_class,
			MKDEV(INPUT_MAJOR, MOUSEDEV_MINOR_BASE + mousedev->minor));
	mousedev->exist = 0;

	if (mousedev->open) {
		input_close_device(handle);
		wake_up_interruptible(&mousedev->wait);
		list_for_each_entry(list, &mousedev->list, node)
			kill_fasync(&list->fasync, SIGIO, POLL_HUP);
	} else {
		if (mousedev_mix.open)
			input_close_device(handle);
		mousedev_free(mousedev);
	}
}

static struct input_device_id mousedev_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT | INPUT_DEVICE_ID_MATCH_RELBIT,
		.evbit = { BIT(EV_KEY) | BIT(EV_REL) },
		.keybit = { [LONG(BTN_LEFT)] = BIT(BTN_LEFT) },
		.relbit = { BIT(REL_X) | BIT(REL_Y) },
	},	/* A mouse like device, at least one button, two relative axes */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_RELBIT,
		.evbit = { BIT(EV_KEY) | BIT(EV_REL) },
		.relbit = { BIT(REL_WHEEL) },
	},	/* A separate scrollwheel */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT | INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT(EV_KEY) | BIT(EV_ABS) },
		.keybit = { [LONG(BTN_TOUCH)] = BIT(BTN_TOUCH) },
		.absbit = { BIT(ABS_X) | BIT(ABS_Y) },
	},	/* A tablet like device, at least touch detection, two absolute axes */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT | INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT(EV_KEY) | BIT(EV_ABS) },
		.keybit = { [LONG(BTN_TOOL_FINGER)] = BIT(BTN_TOOL_FINGER) },
		.absbit = { BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE) | BIT(ABS_TOOL_WIDTH) },
	},	/* A touchpad */

	{ }, 	/* Terminating entry */
};

MODULE_DEVICE_TABLE(input, mousedev_ids);

static struct input_handler mousedev_handler = {
	.event =	mousedev_event,
	.connect =	mousedev_connect,
	.disconnect =	mousedev_disconnect,
	.fops =		&mousedev_fops,
	.minor =	MOUSEDEV_MINOR_BASE,
	.name =		"mousedev",
	.id_table =	mousedev_ids,
};

#ifdef CONFIG_INPUT_MOUSEDEV_PSAUX
static struct miscdevice psaux_mouse = {
	PSMOUSE_MINOR, "psaux", &mousedev_fops
};
static int psaux_registered;
#endif

static int __init mousedev_init(void)
{
	input_register_handler(&mousedev_handler);

	memset(&mousedev_mix, 0, sizeof(struct mousedev));
	INIT_LIST_HEAD(&mousedev_mix.list);
	init_waitqueue_head(&mousedev_mix.wait);
	mousedev_table[MOUSEDEV_MIX] = &mousedev_mix;
	mousedev_mix.exist = 1;
	mousedev_mix.minor = MOUSEDEV_MIX;

	class_device_create(&input_class, NULL,
			MKDEV(INPUT_MAJOR, MOUSEDEV_MINOR_BASE + MOUSEDEV_MIX), NULL, "mice");

#ifdef CONFIG_INPUT_MOUSEDEV_PSAUX
	if (!(psaux_registered = !misc_register(&psaux_mouse)))
		printk(KERN_WARNING "mice: could not misc_register the device\n");
#endif

	printk(KERN_INFO "mice: PS/2 mouse device common for all mice\n");

	return 0;
}

static void __exit mousedev_exit(void)
{
#ifdef CONFIG_INPUT_MOUSEDEV_PSAUX
	if (psaux_registered)
		misc_deregister(&psaux_mouse);
#endif
	class_device_destroy(&input_class,
			MKDEV(INPUT_MAJOR, MOUSEDEV_MINOR_BASE + MOUSEDEV_MIX));
	input_unregister_handler(&mousedev_handler);
}

module_init(mousedev_init);
module_exit(mousedev_exit);
