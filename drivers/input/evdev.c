/*
 * Event char devices, giving access to raw input device events.
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#define EVDEV_MINOR_BASE	64
#define EVDEV_MINORS		32
#define EVDEV_BUFFER_SIZE	64

#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/major.h>
#include <linux/smp_lock.h>
#include <linux/device.h>
#include <linux/compat.h>

struct evdev {
	int exist;
	int open;
	int minor;
	char name[16];
	struct input_handle handle;
	wait_queue_head_t wait;
	struct evdev_list *grab;
	struct list_head list;
};

struct evdev_list {
	struct input_event buffer[EVDEV_BUFFER_SIZE];
	int head;
	int tail;
	struct fasync_struct *fasync;
	struct evdev *evdev;
	struct list_head node;
};

static struct evdev *evdev_table[EVDEV_MINORS];

static void evdev_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	struct evdev *evdev = handle->private;
	struct evdev_list *list;

	if (evdev->grab) {
		list = evdev->grab;

		do_gettimeofday(&list->buffer[list->head].time);
		list->buffer[list->head].type = type;
		list->buffer[list->head].code = code;
		list->buffer[list->head].value = value;
		list->head = (list->head + 1) & (EVDEV_BUFFER_SIZE - 1);

		kill_fasync(&list->fasync, SIGIO, POLL_IN);
	} else
		list_for_each_entry(list, &evdev->list, node) {

			do_gettimeofday(&list->buffer[list->head].time);
			list->buffer[list->head].type = type;
			list->buffer[list->head].code = code;
			list->buffer[list->head].value = value;
			list->head = (list->head + 1) & (EVDEV_BUFFER_SIZE - 1);

			kill_fasync(&list->fasync, SIGIO, POLL_IN);
		}

	wake_up_interruptible(&evdev->wait);
}

static int evdev_fasync(int fd, struct file *file, int on)
{
	int retval;
	struct evdev_list *list = file->private_data;
	retval = fasync_helper(fd, file, on, &list->fasync);
	return retval < 0 ? retval : 0;
}

static int evdev_flush(struct file * file)
{
	struct evdev_list *list = file->private_data;
	if (!list->evdev->exist) return -ENODEV;
	return input_flush_device(&list->evdev->handle, file);
}

static void evdev_free(struct evdev *evdev)
{
	evdev_table[evdev->minor] = NULL;
	kfree(evdev);
}

static int evdev_release(struct inode * inode, struct file * file)
{
	struct evdev_list *list = file->private_data;

	if (list->evdev->grab == list) {
		input_release_device(&list->evdev->handle);
		list->evdev->grab = NULL;
	}

	evdev_fasync(-1, file, 0);
	list_del(&list->node);

	if (!--list->evdev->open) {
		if (list->evdev->exist)
			input_close_device(&list->evdev->handle);
		else
			evdev_free(list->evdev);
	}

	kfree(list);
	return 0;
}

static int evdev_open(struct inode * inode, struct file * file)
{
	struct evdev_list *list;
	int i = iminor(inode) - EVDEV_MINOR_BASE;
	int accept_err;

	if (i >= EVDEV_MINORS || !evdev_table[i] || !evdev_table[i]->exist)
		return -ENODEV;

	if ((accept_err = input_accept_process(&(evdev_table[i]->handle), file)))
		return accept_err;

	if (!(list = kmalloc(sizeof(struct evdev_list), GFP_KERNEL)))
		return -ENOMEM;
	memset(list, 0, sizeof(struct evdev_list));

	list->evdev = evdev_table[i];
	list_add_tail(&list->node, &evdev_table[i]->list);
	file->private_data = list;

	if (!list->evdev->open++)
		if (list->evdev->exist)
			input_open_device(&list->evdev->handle);

	return 0;
}

#ifdef CONFIG_COMPAT
struct input_event_compat {
	struct compat_timeval time;
	__u16 type;
	__u16 code;
	__s32 value;
};

#ifdef CONFIG_X86_64
#  define COMPAT_TEST test_thread_flag(TIF_IA32)
#elif defined(CONFIG_IA64)
#  define COMPAT_TEST IS_IA32_PROCESS(ia64_task_regs(current))
#elif defined(CONFIG_ARCH_S390)
#  define COMPAT_TEST test_thread_flag(TIF_31BIT)
#elif defined(CONFIG_MIPS)
#  define COMPAT_TEST (current->thread.mflags & MF_32BIT_ADDR)
#else
#  define COMPAT_TEST test_thread_flag(TIF_32BIT)
#endif

static ssize_t evdev_write_compat(struct file * file, const char __user * buffer, size_t count, loff_t *ppos)
{
	struct evdev_list *list = file->private_data;
	struct input_event_compat event;
	int retval = 0;

	while (retval < count) {
		if (copy_from_user(&event, buffer + retval, sizeof(struct input_event_compat)))
			return -EFAULT;
		input_event(list->evdev->handle.dev, event.type, event.code, event.value);
		retval += sizeof(struct input_event_compat);
	}

	return retval;
}
#endif

static ssize_t evdev_write(struct file * file, const char __user * buffer, size_t count, loff_t *ppos)
{
	struct evdev_list *list = file->private_data;
	struct input_event event;
	int retval = 0;

	if (!list->evdev->exist) return -ENODEV;

#ifdef CONFIG_COMPAT
	if (COMPAT_TEST)
		return evdev_write_compat(file, buffer, count, ppos);
#endif

	while (retval < count) {

		if (copy_from_user(&event, buffer + retval, sizeof(struct input_event)))
			return -EFAULT;
		input_event(list->evdev->handle.dev, event.type, event.code, event.value);
		retval += sizeof(struct input_event);
	}

	return retval;
}

#ifdef CONFIG_COMPAT
static ssize_t evdev_read_compat(struct file * file, char __user * buffer, size_t count, loff_t *ppos)
{
	struct evdev_list *list = file->private_data;
	int retval;

	if (count < sizeof(struct input_event_compat))
		return -EINVAL;

	if (list->head == list->tail && list->evdev->exist && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	retval = wait_event_interruptible(list->evdev->wait,
		list->head != list->tail || (!list->evdev->exist));

	if (retval)
		return retval;

	if (!list->evdev->exist)
		return -ENODEV;

	while (list->head != list->tail && retval + sizeof(struct input_event_compat) <= count) {
		struct input_event *event = (struct input_event *) list->buffer + list->tail;
		struct input_event_compat event_compat;
		event_compat.time.tv_sec = event->time.tv_sec;
		event_compat.time.tv_usec = event->time.tv_usec;
		event_compat.type = event->type;
		event_compat.code = event->code;
		event_compat.value = event->value;

		if (copy_to_user(buffer + retval, &event_compat,
			sizeof(struct input_event_compat))) return -EFAULT;
		list->tail = (list->tail + 1) & (EVDEV_BUFFER_SIZE - 1);
		retval += sizeof(struct input_event_compat);
	}

	return retval;
}
#endif

static ssize_t evdev_read(struct file * file, char __user * buffer, size_t count, loff_t *ppos)
{
	struct evdev_list *list = file->private_data;
	int retval;

#ifdef CONFIG_COMPAT
	if (COMPAT_TEST)
		return evdev_read_compat(file, buffer, count, ppos);
#endif

	if (count < sizeof(struct input_event))
		return -EINVAL;

	if (list->head == list->tail && list->evdev->exist && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	retval = wait_event_interruptible(list->evdev->wait,
		list->head != list->tail || (!list->evdev->exist));

	if (retval)
		return retval;

	if (!list->evdev->exist)
		return -ENODEV;

	while (list->head != list->tail && retval + sizeof(struct input_event) <= count) {
		if (copy_to_user(buffer + retval, list->buffer + list->tail,
			sizeof(struct input_event))) return -EFAULT;
		list->tail = (list->tail + 1) & (EVDEV_BUFFER_SIZE - 1);
		retval += sizeof(struct input_event);
	}

	return retval;
}

/* No kernel lock - fine */
static unsigned int evdev_poll(struct file *file, poll_table *wait)
{
	struct evdev_list *list = file->private_data;
	poll_wait(file, &list->evdev->wait, wait);
	return ((list->head == list->tail) ? 0 : (POLLIN | POLLRDNORM)) |
		(list->evdev->exist ? 0 : (POLLHUP | POLLERR));
}

static long evdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct evdev_list *list = file->private_data;
	struct evdev *evdev = list->evdev;
	struct input_dev *dev = evdev->handle.dev;
	struct input_absinfo abs;
	void __user *p = (void __user *)arg;
	int __user *ip = (int __user *)arg;
	int i, t, u, v;

	if (!evdev->exist) return -ENODEV;

	switch (cmd) {

		case EVIOCGVERSION:
			return put_user(EV_VERSION, ip);

		case EVIOCGID:
			return copy_to_user(p, &dev->id, sizeof(struct input_id)) ? -EFAULT : 0;

		case EVIOCGKEYCODE:
			if (get_user(t, ip)) return -EFAULT;
			if (t < 0 || t >= dev->keycodemax || !dev->keycodesize) return -EINVAL;
			if (put_user(INPUT_KEYCODE(dev, t), ip + 1)) return -EFAULT;
			return 0;

		case EVIOCSKEYCODE:
			if (get_user(t, ip)) return -EFAULT;
			if (t < 0 || t >= dev->keycodemax || !dev->keycodesize) return -EINVAL;
			if (get_user(v, ip + 1)) return -EFAULT;
			if (v < 0 || v > KEY_MAX) return -EINVAL;
			if (dev->keycodesize < sizeof(v) && (v >> (dev->keycodesize * 8))) return -EINVAL;
			u = SET_INPUT_KEYCODE(dev, t, v);
			clear_bit(u, dev->keybit);
			set_bit(v, dev->keybit);
			for (i = 0; i < dev->keycodemax; i++)
				if (INPUT_KEYCODE(dev,i) == u)
					set_bit(u, dev->keybit);
			return 0;

		case EVIOCSFF:
			if (dev->upload_effect) {
				struct ff_effect effect;
				int err;

				if (copy_from_user(&effect, p, sizeof(effect)))
					return -EFAULT;
				err = dev->upload_effect(dev, &effect);
				if (put_user(effect.id, &(((struct ff_effect __user *)arg)->id)))
					return -EFAULT;
				return err;
			}
			else return -ENOSYS;

		case EVIOCRMFF:
			if (dev->erase_effect) {
				return dev->erase_effect(dev, (int)arg);
			}
			else return -ENOSYS;

		case EVIOCGEFFECTS:
			if (put_user(dev->ff_effects_max, ip))
				return -EFAULT;
			return 0;

		case EVIOCGRAB:
			if (arg) {
				if (evdev->grab)
					return -EBUSY;
				if (input_grab_device(&evdev->handle))
					return -EBUSY;
				evdev->grab = list;
				return 0;
			} else {
				if (evdev->grab != list)
					return -EINVAL;
				input_release_device(&evdev->handle);
				evdev->grab = NULL;
				return 0;
			}

		default:

			if (_IOC_TYPE(cmd) != 'E')
				return -EINVAL;

			if (_IOC_DIR(cmd) == _IOC_READ) {

				if ((_IOC_NR(cmd) & ~EV_MAX) == _IOC_NR(EVIOCGBIT(0,0))) {

					long *bits;
					int len;

					switch (_IOC_NR(cmd) & EV_MAX) {
						case      0: bits = dev->evbit;  len = EV_MAX;  break;
						case EV_KEY: bits = dev->keybit; len = KEY_MAX; break;
						case EV_REL: bits = dev->relbit; len = REL_MAX; break;
						case EV_ABS: bits = dev->absbit; len = ABS_MAX; break;
						case EV_MSC: bits = dev->mscbit; len = MSC_MAX; break;
						case EV_LED: bits = dev->ledbit; len = LED_MAX; break;
						case EV_SND: bits = dev->sndbit; len = SND_MAX; break;
						case EV_FF:  bits = dev->ffbit;  len = FF_MAX;  break;
						case EV_SW:  bits = dev->swbit;  len = SW_MAX;  break;
						default: return -EINVAL;
					}
					len = NBITS(len) * sizeof(long);
					if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
					return copy_to_user(p, bits, len) ? -EFAULT : len;
				}

				if (_IOC_NR(cmd) == _IOC_NR(EVIOCGKEY(0))) {
					int len;
					len = NBITS(KEY_MAX) * sizeof(long);
					if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
					return copy_to_user(p, dev->key, len) ? -EFAULT : len;
				}

				if (_IOC_NR(cmd) == _IOC_NR(EVIOCGLED(0))) {
					int len;
					len = NBITS(LED_MAX) * sizeof(long);
					if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
					return copy_to_user(p, dev->led, len) ? -EFAULT : len;
				}

				if (_IOC_NR(cmd) == _IOC_NR(EVIOCGSND(0))) {
					int len;
					len = NBITS(SND_MAX) * sizeof(long);
					if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
					return copy_to_user(p, dev->snd, len) ? -EFAULT : len;
				}

				if (_IOC_NR(cmd) == _IOC_NR(EVIOCGSW(0))) {
					int len;
					len = NBITS(SW_MAX) * sizeof(long);
					if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
					return copy_to_user(p, dev->sw, len) ? -EFAULT : len;
				}

				if (_IOC_NR(cmd) == _IOC_NR(EVIOCGNAME(0))) {
					int len;
					if (!dev->name) return -ENOENT;
					len = strlen(dev->name) + 1;
					if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
					return copy_to_user(p, dev->name, len) ? -EFAULT : len;
				}

				if (_IOC_NR(cmd) == _IOC_NR(EVIOCGPHYS(0))) {
					int len;
					if (!dev->phys) return -ENOENT;
					len = strlen(dev->phys) + 1;
					if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
					return copy_to_user(p, dev->phys, len) ? -EFAULT : len;
				}

				if (_IOC_NR(cmd) == _IOC_NR(EVIOCGUNIQ(0))) {
					int len;
					if (!dev->uniq) return -ENOENT;
					len = strlen(dev->uniq) + 1;
					if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
					return copy_to_user(p, dev->uniq, len) ? -EFAULT : len;
				}

				if ((_IOC_NR(cmd) & ~ABS_MAX) == _IOC_NR(EVIOCGABS(0))) {

					int t = _IOC_NR(cmd) & ABS_MAX;

					abs.value = dev->abs[t];
					abs.minimum = dev->absmin[t];
					abs.maximum = dev->absmax[t];
					abs.fuzz = dev->absfuzz[t];
					abs.flat = dev->absflat[t];

					if (copy_to_user(p, &abs, sizeof(struct input_absinfo)))
						return -EFAULT;

					return 0;
				}

			}

			if (_IOC_DIR(cmd) == _IOC_WRITE) {

				if ((_IOC_NR(cmd) & ~ABS_MAX) == _IOC_NR(EVIOCSABS(0))) {

					int t = _IOC_NR(cmd) & ABS_MAX;

					if (copy_from_user(&abs, p, sizeof(struct input_absinfo)))
						return -EFAULT;

					dev->abs[t] = abs.value;
					dev->absmin[t] = abs.minimum;
					dev->absmax[t] = abs.maximum;
					dev->absfuzz[t] = abs.fuzz;
					dev->absflat[t] = abs.flat;

					return 0;
				}
			}
	}
	return -EINVAL;
}

#ifdef CONFIG_COMPAT

#define BITS_PER_LONG_COMPAT (sizeof(compat_long_t) * 8)
#define NBITS_COMPAT(x) ((((x)-1)/BITS_PER_LONG_COMPAT)+1)
#define OFF_COMPAT(x)  ((x)%BITS_PER_LONG_COMPAT)
#define BIT_COMPAT(x)  (1UL<<OFF_COMPAT(x))
#define LONG_COMPAT(x) ((x)/BITS_PER_LONG_COMPAT)
#define test_bit_compat(bit, array) ((array[LONG_COMPAT(bit)] >> OFF_COMPAT(bit)) & 1)

#ifdef __BIG_ENDIAN
#define bit_to_user(bit, max) \
do { \
	int i; \
	int len = NBITS_COMPAT((max)) * sizeof(compat_long_t); \
	if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd); \
	for (i = 0; i < len / sizeof(compat_long_t); i++) \
		if (copy_to_user((compat_long_t __user *) p + i, \
				 (compat_long_t*) (bit) + i + 1 - ((i % 2) << 1), \
				 sizeof(compat_long_t))) \
			return -EFAULT; \
	return len; \
} while (0)
#else
#define bit_to_user(bit, max) \
do { \
	int len = NBITS_COMPAT((max)) * sizeof(compat_long_t); \
	if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd); \
	return copy_to_user(p, (bit), len) ? -EFAULT : len; \
} while (0)
#endif

static long evdev_ioctl_compat(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct evdev_list *list = file->private_data;
	struct evdev *evdev = list->evdev;
	struct input_dev *dev = evdev->handle.dev;
	struct input_absinfo abs;
	void __user *p = compat_ptr(arg);

	if (!evdev->exist) return -ENODEV;

	switch (cmd) {

		case EVIOCGVERSION:
		case EVIOCGID:
		case EVIOCGKEYCODE:
		case EVIOCSKEYCODE:
		case EVIOCSFF:
		case EVIOCRMFF:
		case EVIOCGEFFECTS:
		case EVIOCGRAB:
			return evdev_ioctl(file, cmd, (unsigned long) p);

		default:

			if (_IOC_TYPE(cmd) != 'E')
				return -EINVAL;

			if (_IOC_DIR(cmd) == _IOC_READ) {

				if ((_IOC_NR(cmd) & ~EV_MAX) == _IOC_NR(EVIOCGBIT(0,0))) {
					long *bits;
					int max;

					switch (_IOC_NR(cmd) & EV_MAX) {
						case      0: bits = dev->evbit;  max = EV_MAX;  break;
						case EV_KEY: bits = dev->keybit; max = KEY_MAX; break;
						case EV_REL: bits = dev->relbit; max = REL_MAX; break;
						case EV_ABS: bits = dev->absbit; max = ABS_MAX; break;
						case EV_MSC: bits = dev->mscbit; max = MSC_MAX; break;
						case EV_LED: bits = dev->ledbit; max = LED_MAX; break;
						case EV_SND: bits = dev->sndbit; max = SND_MAX; break;
						case EV_FF:  bits = dev->ffbit;  max = FF_MAX;  break;
						default: return -EINVAL;
					}
					bit_to_user(bits, max);
				}

				if (_IOC_NR(cmd) == _IOC_NR(EVIOCGKEY(0)))
					bit_to_user(dev->key, KEY_MAX);

				if (_IOC_NR(cmd) == _IOC_NR(EVIOCGLED(0)))
					bit_to_user(dev->led, LED_MAX);

				if (_IOC_NR(cmd) == _IOC_NR(EVIOCGSND(0)))
					bit_to_user(dev->snd, SND_MAX);

				if (_IOC_NR(cmd) == _IOC_NR(EVIOCGNAME(0))) {
					int len;
					if (!dev->name) return -ENOENT;
					len = strlen(dev->name) + 1;
					if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
					return copy_to_user(p, dev->name, len) ? -EFAULT : len;
				}

				if (_IOC_NR(cmd) == _IOC_NR(EVIOCGPHYS(0))) {
					int len;
					if (!dev->phys) return -ENOENT;
					len = strlen(dev->phys) + 1;
					if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
					return copy_to_user(p, dev->phys, len) ? -EFAULT : len;
				}

				if (_IOC_NR(cmd) == _IOC_NR(EVIOCGUNIQ(0))) {
					int len;
					if (!dev->uniq) return -ENOENT;
					len = strlen(dev->uniq) + 1;
					if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
					return copy_to_user(p, dev->uniq, len) ? -EFAULT : len;
				}

				if ((_IOC_NR(cmd) & ~ABS_MAX) == _IOC_NR(EVIOCGABS(0))) {

					int t = _IOC_NR(cmd) & ABS_MAX;

					abs.value = dev->abs[t];
					abs.minimum = dev->absmin[t];
					abs.maximum = dev->absmax[t];
					abs.fuzz = dev->absfuzz[t];
					abs.flat = dev->absflat[t];

					if (copy_to_user(p, &abs, sizeof(struct input_absinfo)))
						return -EFAULT;

					return 0;
				}
			}

			if (_IOC_DIR(cmd) == _IOC_WRITE) {

				if ((_IOC_NR(cmd) & ~ABS_MAX) == _IOC_NR(EVIOCSABS(0))) {

					int t = _IOC_NR(cmd) & ABS_MAX;

					if (copy_from_user(&abs, p, sizeof(struct input_absinfo)))
						return -EFAULT;

					dev->abs[t] = abs.value;
					dev->absmin[t] = abs.minimum;
					dev->absmax[t] = abs.maximum;
					dev->absfuzz[t] = abs.fuzz;
					dev->absflat[t] = abs.flat;

					return 0;
				}
			}
	}
	return -EINVAL;
}
#endif

static struct file_operations evdev_fops = {
	.owner =	THIS_MODULE,
	.read =		evdev_read,
	.write =	evdev_write,
	.poll =		evdev_poll,
	.open =		evdev_open,
	.release =	evdev_release,
	.unlocked_ioctl = evdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl =	evdev_ioctl_compat,
#endif
	.fasync =	evdev_fasync,
	.flush =	evdev_flush
};

static struct input_handle *evdev_connect(struct input_handler *handler, struct input_dev *dev, struct input_device_id *id)
{
	struct evdev *evdev;
	struct class_device *cdev;
	int minor;

	for (minor = 0; minor < EVDEV_MINORS && evdev_table[minor]; minor++);
	if (minor == EVDEV_MINORS) {
		printk(KERN_ERR "evdev: no more free evdev devices\n");
		return NULL;
	}

	if (!(evdev = kmalloc(sizeof(struct evdev), GFP_KERNEL)))
		return NULL;
	memset(evdev, 0, sizeof(struct evdev));

	INIT_LIST_HEAD(&evdev->list);
	init_waitqueue_head(&evdev->wait);

	evdev->exist = 1;
	evdev->minor = minor;
	evdev->handle.dev = dev;
	evdev->handle.name = evdev->name;
	evdev->handle.handler = handler;
	evdev->handle.private = evdev;
	sprintf(evdev->name, "event%d", minor);

	evdev_table[minor] = evdev;

	cdev = class_device_create(&input_class, &dev->cdev,
			MKDEV(INPUT_MAJOR, EVDEV_MINOR_BASE + minor),
			dev->cdev.dev, evdev->name);

	/* temporary symlink to keep userspace happy */
	sysfs_create_link(&input_class.subsys.kset.kobj, &cdev->kobj,
			  evdev->name);

	return &evdev->handle;
}

static void evdev_disconnect(struct input_handle *handle)
{
	struct evdev *evdev = handle->private;
	struct evdev_list *list;

	sysfs_remove_link(&input_class.subsys.kset.kobj, evdev->name);
	class_device_destroy(&input_class,
			MKDEV(INPUT_MAJOR, EVDEV_MINOR_BASE + evdev->minor));
	evdev->exist = 0;

	if (evdev->open) {
		input_close_device(handle);
		wake_up_interruptible(&evdev->wait);
		list_for_each_entry(list, &evdev->list, node)
			kill_fasync(&list->fasync, SIGIO, POLL_HUP);
	} else
		evdev_free(evdev);
}

static struct input_device_id evdev_ids[] = {
	{ .driver_info = 1 },	/* Matches all devices */
	{ },			/* Terminating zero entry */
};

MODULE_DEVICE_TABLE(input, evdev_ids);

static struct input_handler evdev_handler = {
	.event =	evdev_event,
	.connect =	evdev_connect,
	.disconnect =	evdev_disconnect,
	.fops =		&evdev_fops,
	.minor =	EVDEV_MINOR_BASE,
	.name =		"evdev",
	.id_table =	evdev_ids,
};

static int __init evdev_init(void)
{
	input_register_handler(&evdev_handler);
	return 0;
}

static void __exit evdev_exit(void)
{
	input_unregister_handler(&evdev_handler);
}

module_init(evdev_init);
module_exit(evdev_exit);

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Input driver event char devices");
MODULE_LICENSE("GPL");
