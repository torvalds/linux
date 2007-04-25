/*
 * The input core
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/major.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/mutex.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("Input core");
MODULE_LICENSE("GPL");

#define INPUT_DEVICES	256

static LIST_HEAD(input_dev_list);
static LIST_HEAD(input_handler_list);

static struct input_handler *input_table[8];

/**
 * input_event() - report new input event
 * @dev: device that generated the event
 * @type: type of the event
 * @code: event code
 * @value: value of the event
 *
 * This function should be used by drivers implementing various input devices
 * See also input_inject_event()
 */
void input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct input_handle *handle;

	if (type > EV_MAX || !test_bit(type, dev->evbit))
		return;

	add_input_randomness(type, code, value);

	switch (type) {

		case EV_SYN:
			switch (code) {
				case SYN_CONFIG:
					if (dev->event)
						dev->event(dev, type, code, value);
					break;

				case SYN_REPORT:
					if (dev->sync)
						return;
					dev->sync = 1;
					break;
			}
			break;

		case EV_KEY:

			if (code > KEY_MAX || !test_bit(code, dev->keybit) || !!test_bit(code, dev->key) == value)
				return;

			if (value == 2)
				break;

			change_bit(code, dev->key);

			if (test_bit(EV_REP, dev->evbit) && dev->rep[REP_PERIOD] && dev->rep[REP_DELAY] && dev->timer.data && value) {
				dev->repeat_key = code;
				mod_timer(&dev->timer, jiffies + msecs_to_jiffies(dev->rep[REP_DELAY]));
			}

			break;

		case EV_SW:

			if (code > SW_MAX || !test_bit(code, dev->swbit) || !!test_bit(code, dev->sw) == value)
				return;

			change_bit(code, dev->sw);

			break;

		case EV_ABS:

			if (code > ABS_MAX || !test_bit(code, dev->absbit))
				return;

			if (dev->absfuzz[code]) {
				if ((value > dev->abs[code] - (dev->absfuzz[code] >> 1)) &&
				    (value < dev->abs[code] + (dev->absfuzz[code] >> 1)))
					return;

				if ((value > dev->abs[code] - dev->absfuzz[code]) &&
				    (value < dev->abs[code] + dev->absfuzz[code]))
					value = (dev->abs[code] * 3 + value) >> 2;

				if ((value > dev->abs[code] - (dev->absfuzz[code] << 1)) &&
				    (value < dev->abs[code] + (dev->absfuzz[code] << 1)))
					value = (dev->abs[code] + value) >> 1;
			}

			if (dev->abs[code] == value)
				return;

			dev->abs[code] = value;
			break;

		case EV_REL:

			if (code > REL_MAX || !test_bit(code, dev->relbit) || (value == 0))
				return;

			break;

		case EV_MSC:

			if (code > MSC_MAX || !test_bit(code, dev->mscbit))
				return;

			if (dev->event)
				dev->event(dev, type, code, value);

			break;

		case EV_LED:

			if (code > LED_MAX || !test_bit(code, dev->ledbit) || !!test_bit(code, dev->led) == value)
				return;

			change_bit(code, dev->led);

			if (dev->event)
				dev->event(dev, type, code, value);

			break;

		case EV_SND:

			if (code > SND_MAX || !test_bit(code, dev->sndbit))
				return;

			if (!!test_bit(code, dev->snd) != !!value)
				change_bit(code, dev->snd);

			if (dev->event)
				dev->event(dev, type, code, value);

			break;

		case EV_REP:

			if (code > REP_MAX || value < 0 || dev->rep[code] == value)
				return;

			dev->rep[code] = value;
			if (dev->event)
				dev->event(dev, type, code, value);

			break;

		case EV_FF:

			if (value < 0)
				return;

			if (dev->event)
				dev->event(dev, type, code, value);
			break;
	}

	if (type != EV_SYN)
		dev->sync = 0;

	if (dev->grab)
		dev->grab->handler->event(dev->grab, type, code, value);
	else
		list_for_each_entry(handle, &dev->h_list, d_node)
			if (handle->open)
				handle->handler->event(handle, type, code, value);
}
EXPORT_SYMBOL(input_event);

/**
 * input_inject_event() - send input event from input handler
 * @handle: input handle to send event through
 * @type: type of the event
 * @code: event code
 * @value: value of the event
 *
 * Similar to input_event() but will ignore event if device is "grabbed" and handle
 * injecting event is not the one that owns the device.
 */
void input_inject_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	if (!handle->dev->grab || handle->dev->grab == handle)
		input_event(handle->dev, type, code, value);
}
EXPORT_SYMBOL(input_inject_event);

static void input_repeat_key(unsigned long data)
{
	struct input_dev *dev = (void *) data;

	if (!test_bit(dev->repeat_key, dev->key))
		return;

	input_event(dev, EV_KEY, dev->repeat_key, 2);
	input_sync(dev);

	if (dev->rep[REP_PERIOD])
		mod_timer(&dev->timer, jiffies + msecs_to_jiffies(dev->rep[REP_PERIOD]));
}

int input_grab_device(struct input_handle *handle)
{
	if (handle->dev->grab)
		return -EBUSY;

	handle->dev->grab = handle;
	return 0;
}
EXPORT_SYMBOL(input_grab_device);

void input_release_device(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;

	if (dev->grab == handle) {
		dev->grab = NULL;

		list_for_each_entry(handle, &dev->h_list, d_node)
			if (handle->handler->start)
				handle->handler->start(handle);
	}
}
EXPORT_SYMBOL(input_release_device);

int input_open_device(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;
	int err;

	err = mutex_lock_interruptible(&dev->mutex);
	if (err)
		return err;

	handle->open++;

	if (!dev->users++ && dev->open)
		err = dev->open(dev);

	if (err)
		handle->open--;

	mutex_unlock(&dev->mutex);

	return err;
}
EXPORT_SYMBOL(input_open_device);

int input_flush_device(struct input_handle* handle, struct file* file)
{
	if (handle->dev->flush)
		return handle->dev->flush(handle->dev, file);

	return 0;
}
EXPORT_SYMBOL(input_flush_device);

void input_close_device(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;

	input_release_device(handle);

	mutex_lock(&dev->mutex);

	if (!--dev->users && dev->close)
		dev->close(dev);
	handle->open--;

	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL(input_close_device);

static int input_fetch_keycode(struct input_dev *dev, int scancode)
{
	switch (dev->keycodesize) {
		case 1:
			return ((u8 *)dev->keycode)[scancode];

		case 2:
			return ((u16 *)dev->keycode)[scancode];

		default:
			return ((u32 *)dev->keycode)[scancode];
	}
}

static int input_default_getkeycode(struct input_dev *dev,
				    int scancode, int *keycode)
{
	if (!dev->keycodesize)
		return -EINVAL;

	if (scancode < 0 || scancode >= dev->keycodemax)
		return -EINVAL;

	*keycode = input_fetch_keycode(dev, scancode);

	return 0;
}

static int input_default_setkeycode(struct input_dev *dev,
				    int scancode, int keycode)
{
	int old_keycode;
	int i;

	if (scancode < 0 || scancode >= dev->keycodemax)
		return -EINVAL;

	if (keycode < 0 || keycode > KEY_MAX)
		return -EINVAL;

	if (!dev->keycodesize)
		return -EINVAL;

	if (dev->keycodesize < sizeof(keycode) && (keycode >> (dev->keycodesize * 8)))
		return -EINVAL;

	switch (dev->keycodesize) {
		case 1: {
			u8 *k = (u8 *)dev->keycode;
			old_keycode = k[scancode];
			k[scancode] = keycode;
			break;
		}
		case 2: {
			u16 *k = (u16 *)dev->keycode;
			old_keycode = k[scancode];
			k[scancode] = keycode;
			break;
		}
		default: {
			u32 *k = (u32 *)dev->keycode;
			old_keycode = k[scancode];
			k[scancode] = keycode;
			break;
		}
	}

	clear_bit(old_keycode, dev->keybit);
	set_bit(keycode, dev->keybit);

	for (i = 0; i < dev->keycodemax; i++) {
		if (input_fetch_keycode(dev, i) == old_keycode) {
			set_bit(old_keycode, dev->keybit);
			break; /* Setting the bit twice is useless, so break */
		}
	}

	return 0;
}


#define MATCH_BIT(bit, max) \
		for (i = 0; i < NBITS(max); i++) \
			if ((id->bit[i] & dev->bit[i]) != id->bit[i]) \
				break; \
		if (i != NBITS(max)) \
			continue;

static const struct input_device_id *input_match_device(const struct input_device_id *id,
							struct input_dev *dev)
{
	int i;

	for (; id->flags || id->driver_info; id++) {

		if (id->flags & INPUT_DEVICE_ID_MATCH_BUS)
			if (id->bustype != dev->id.bustype)
				continue;

		if (id->flags & INPUT_DEVICE_ID_MATCH_VENDOR)
			if (id->vendor != dev->id.vendor)
				continue;

		if (id->flags & INPUT_DEVICE_ID_MATCH_PRODUCT)
			if (id->product != dev->id.product)
				continue;

		if (id->flags & INPUT_DEVICE_ID_MATCH_VERSION)
			if (id->version != dev->id.version)
				continue;

		MATCH_BIT(evbit,  EV_MAX);
		MATCH_BIT(keybit, KEY_MAX);
		MATCH_BIT(relbit, REL_MAX);
		MATCH_BIT(absbit, ABS_MAX);
		MATCH_BIT(mscbit, MSC_MAX);
		MATCH_BIT(ledbit, LED_MAX);
		MATCH_BIT(sndbit, SND_MAX);
		MATCH_BIT(ffbit,  FF_MAX);
		MATCH_BIT(swbit,  SW_MAX);

		return id;
	}

	return NULL;
}

static int input_attach_handler(struct input_dev *dev, struct input_handler *handler)
{
	const struct input_device_id *id;
	int error;

	if (handler->blacklist && input_match_device(handler->blacklist, dev))
		return -ENODEV;

	id = input_match_device(handler->id_table, dev);
	if (!id)
		return -ENODEV;

	error = handler->connect(handler, dev, id);
	if (error && error != -ENODEV)
		printk(KERN_ERR
			"input: failed to attach handler %s to device %s, "
			"error: %d\n",
			handler->name, kobject_name(&dev->cdev.kobj), error);

	return error;
}


#ifdef CONFIG_PROC_FS

static struct proc_dir_entry *proc_bus_input_dir;
static DECLARE_WAIT_QUEUE_HEAD(input_devices_poll_wait);
static int input_devices_state;

static inline void input_wakeup_procfs_readers(void)
{
	input_devices_state++;
	wake_up(&input_devices_poll_wait);
}

static unsigned int input_proc_devices_poll(struct file *file, poll_table *wait)
{
	int state = input_devices_state;

	poll_wait(file, &input_devices_poll_wait, wait);
	if (state != input_devices_state)
		return POLLIN | POLLRDNORM;

	return 0;
}

static struct list_head *list_get_nth_element(struct list_head *list, loff_t *pos)
{
	struct list_head *node;
	loff_t i = 0;

	list_for_each(node, list)
		if (i++ == *pos)
			return node;

	return NULL;
}

static struct list_head *list_get_next_element(struct list_head *list, struct list_head *element, loff_t *pos)
{
	if (element->next == list)
		return NULL;

	++(*pos);
	return element->next;
}

static void *input_devices_seq_start(struct seq_file *seq, loff_t *pos)
{
	/* acquire lock here ... Yes, we do need locking, I knowi, I know... */

	return list_get_nth_element(&input_dev_list, pos);
}

static void *input_devices_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return list_get_next_element(&input_dev_list, v, pos);
}

static void input_devices_seq_stop(struct seq_file *seq, void *v)
{
	/* release lock here */
}

static void input_seq_print_bitmap(struct seq_file *seq, const char *name,
				   unsigned long *bitmap, int max)
{
	int i;

	for (i = NBITS(max) - 1; i > 0; i--)
		if (bitmap[i])
			break;

	seq_printf(seq, "B: %s=", name);
	for (; i >= 0; i--)
		seq_printf(seq, "%lx%s", bitmap[i], i > 0 ? " " : "");
	seq_putc(seq, '\n');
}

static int input_devices_seq_show(struct seq_file *seq, void *v)
{
	struct input_dev *dev = container_of(v, struct input_dev, node);
	const char *path = kobject_get_path(&dev->cdev.kobj, GFP_KERNEL);
	struct input_handle *handle;

	seq_printf(seq, "I: Bus=%04x Vendor=%04x Product=%04x Version=%04x\n",
		   dev->id.bustype, dev->id.vendor, dev->id.product, dev->id.version);

	seq_printf(seq, "N: Name=\"%s\"\n", dev->name ? dev->name : "");
	seq_printf(seq, "P: Phys=%s\n", dev->phys ? dev->phys : "");
	seq_printf(seq, "S: Sysfs=%s\n", path ? path : "");
	seq_printf(seq, "U: Uniq=%s\n", dev->uniq ? dev->uniq : "");
	seq_printf(seq, "H: Handlers=");

	list_for_each_entry(handle, &dev->h_list, d_node)
		seq_printf(seq, "%s ", handle->name);
	seq_putc(seq, '\n');

	input_seq_print_bitmap(seq, "EV", dev->evbit, EV_MAX);
	if (test_bit(EV_KEY, dev->evbit))
		input_seq_print_bitmap(seq, "KEY", dev->keybit, KEY_MAX);
	if (test_bit(EV_REL, dev->evbit))
		input_seq_print_bitmap(seq, "REL", dev->relbit, REL_MAX);
	if (test_bit(EV_ABS, dev->evbit))
		input_seq_print_bitmap(seq, "ABS", dev->absbit, ABS_MAX);
	if (test_bit(EV_MSC, dev->evbit))
		input_seq_print_bitmap(seq, "MSC", dev->mscbit, MSC_MAX);
	if (test_bit(EV_LED, dev->evbit))
		input_seq_print_bitmap(seq, "LED", dev->ledbit, LED_MAX);
	if (test_bit(EV_SND, dev->evbit))
		input_seq_print_bitmap(seq, "SND", dev->sndbit, SND_MAX);
	if (test_bit(EV_FF, dev->evbit))
		input_seq_print_bitmap(seq, "FF", dev->ffbit, FF_MAX);
	if (test_bit(EV_SW, dev->evbit))
		input_seq_print_bitmap(seq, "SW", dev->swbit, SW_MAX);

	seq_putc(seq, '\n');

	kfree(path);
	return 0;
}

static struct seq_operations input_devices_seq_ops = {
	.start	= input_devices_seq_start,
	.next	= input_devices_seq_next,
	.stop	= input_devices_seq_stop,
	.show	= input_devices_seq_show,
};

static int input_proc_devices_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &input_devices_seq_ops);
}

static const struct file_operations input_devices_fileops = {
	.owner		= THIS_MODULE,
	.open		= input_proc_devices_open,
	.poll		= input_proc_devices_poll,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static void *input_handlers_seq_start(struct seq_file *seq, loff_t *pos)
{
	/* acquire lock here ... Yes, we do need locking, I knowi, I know... */
	seq->private = (void *)(unsigned long)*pos;
	return list_get_nth_element(&input_handler_list, pos);
}

static void *input_handlers_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	seq->private = (void *)(unsigned long)(*pos + 1);
	return list_get_next_element(&input_handler_list, v, pos);
}

static void input_handlers_seq_stop(struct seq_file *seq, void *v)
{
	/* release lock here */
}

static int input_handlers_seq_show(struct seq_file *seq, void *v)
{
	struct input_handler *handler = container_of(v, struct input_handler, node);

	seq_printf(seq, "N: Number=%ld Name=%s",
		   (unsigned long)seq->private, handler->name);
	if (handler->fops)
		seq_printf(seq, " Minor=%d", handler->minor);
	seq_putc(seq, '\n');

	return 0;
}
static struct seq_operations input_handlers_seq_ops = {
	.start	= input_handlers_seq_start,
	.next	= input_handlers_seq_next,
	.stop	= input_handlers_seq_stop,
	.show	= input_handlers_seq_show,
};

static int input_proc_handlers_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &input_handlers_seq_ops);
}

static const struct file_operations input_handlers_fileops = {
	.owner		= THIS_MODULE,
	.open		= input_proc_handlers_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init input_proc_init(void)
{
	struct proc_dir_entry *entry;

	proc_bus_input_dir = proc_mkdir("input", proc_bus);
	if (!proc_bus_input_dir)
		return -ENOMEM;

	proc_bus_input_dir->owner = THIS_MODULE;

	entry = create_proc_entry("devices", 0, proc_bus_input_dir);
	if (!entry)
		goto fail1;

	entry->owner = THIS_MODULE;
	entry->proc_fops = &input_devices_fileops;

	entry = create_proc_entry("handlers", 0, proc_bus_input_dir);
	if (!entry)
		goto fail2;

	entry->owner = THIS_MODULE;
	entry->proc_fops = &input_handlers_fileops;

	return 0;

 fail2:	remove_proc_entry("devices", proc_bus_input_dir);
 fail1: remove_proc_entry("input", proc_bus);
	return -ENOMEM;
}

static void input_proc_exit(void)
{
	remove_proc_entry("devices", proc_bus_input_dir);
	remove_proc_entry("handlers", proc_bus_input_dir);
	remove_proc_entry("input", proc_bus);
}

#else /* !CONFIG_PROC_FS */
static inline void input_wakeup_procfs_readers(void) { }
static inline int input_proc_init(void) { return 0; }
static inline void input_proc_exit(void) { }
#endif

#define INPUT_DEV_STRING_ATTR_SHOW(name)					\
static ssize_t input_dev_show_##name(struct class_device *dev, char *buf)	\
{										\
	struct input_dev *input_dev = to_input_dev(dev);			\
										\
	return scnprintf(buf, PAGE_SIZE, "%s\n",				\
			 input_dev->name ? input_dev->name : "");		\
}										\
static CLASS_DEVICE_ATTR(name, S_IRUGO, input_dev_show_##name, NULL);

INPUT_DEV_STRING_ATTR_SHOW(name);
INPUT_DEV_STRING_ATTR_SHOW(phys);
INPUT_DEV_STRING_ATTR_SHOW(uniq);

static int input_print_modalias_bits(char *buf, int size,
				     char name, unsigned long *bm,
				     unsigned int min_bit, unsigned int max_bit)
{
	int len = 0, i;

	len += snprintf(buf, max(size, 0), "%c", name);
	for (i = min_bit; i < max_bit; i++)
		if (bm[LONG(i)] & BIT(i))
			len += snprintf(buf + len, max(size - len, 0), "%X,", i);
	return len;
}

static int input_print_modalias(char *buf, int size, struct input_dev *id,
				int add_cr)
{
	int len;

	len = snprintf(buf, max(size, 0),
		       "input:b%04Xv%04Xp%04Xe%04X-",
		       id->id.bustype, id->id.vendor,
		       id->id.product, id->id.version);

	len += input_print_modalias_bits(buf + len, size - len,
				'e', id->evbit, 0, EV_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				'k', id->keybit, KEY_MIN_INTERESTING, KEY_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				'r', id->relbit, 0, REL_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				'a', id->absbit, 0, ABS_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				'm', id->mscbit, 0, MSC_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				'l', id->ledbit, 0, LED_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				's', id->sndbit, 0, SND_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				'f', id->ffbit, 0, FF_MAX);
	len += input_print_modalias_bits(buf + len, size - len,
				'w', id->swbit, 0, SW_MAX);

	if (add_cr)
		len += snprintf(buf + len, max(size - len, 0), "\n");

	return len;
}

static ssize_t input_dev_show_modalias(struct class_device *dev, char *buf)
{
	struct input_dev *id = to_input_dev(dev);
	ssize_t len;

	len = input_print_modalias(buf, PAGE_SIZE, id, 1);

	return min_t(int, len, PAGE_SIZE);
}
static CLASS_DEVICE_ATTR(modalias, S_IRUGO, input_dev_show_modalias, NULL);

static struct attribute *input_dev_attrs[] = {
	&class_device_attr_name.attr,
	&class_device_attr_phys.attr,
	&class_device_attr_uniq.attr,
	&class_device_attr_modalias.attr,
	NULL
};

static struct attribute_group input_dev_attr_group = {
	.attrs	= input_dev_attrs,
};

#define INPUT_DEV_ID_ATTR(name)							\
static ssize_t input_dev_show_id_##name(struct class_device *dev, char *buf)	\
{										\
	struct input_dev *input_dev = to_input_dev(dev);			\
	return scnprintf(buf, PAGE_SIZE, "%04x\n", input_dev->id.name);		\
}										\
static CLASS_DEVICE_ATTR(name, S_IRUGO, input_dev_show_id_##name, NULL);

INPUT_DEV_ID_ATTR(bustype);
INPUT_DEV_ID_ATTR(vendor);
INPUT_DEV_ID_ATTR(product);
INPUT_DEV_ID_ATTR(version);

static struct attribute *input_dev_id_attrs[] = {
	&class_device_attr_bustype.attr,
	&class_device_attr_vendor.attr,
	&class_device_attr_product.attr,
	&class_device_attr_version.attr,
	NULL
};

static struct attribute_group input_dev_id_attr_group = {
	.name	= "id",
	.attrs	= input_dev_id_attrs,
};

static int input_print_bitmap(char *buf, int buf_size, unsigned long *bitmap,
			      int max, int add_cr)
{
	int i;
	int len = 0;

	for (i = NBITS(max) - 1; i > 0; i--)
		if (bitmap[i])
			break;

	for (; i >= 0; i--)
		len += snprintf(buf + len, max(buf_size - len, 0),
				"%lx%s", bitmap[i], i > 0 ? " " : "");

	if (add_cr)
		len += snprintf(buf + len, max(buf_size - len, 0), "\n");

	return len;
}

#define INPUT_DEV_CAP_ATTR(ev, bm)						\
static ssize_t input_dev_show_cap_##bm(struct class_device *dev, char *buf)	\
{										\
	struct input_dev *input_dev = to_input_dev(dev);			\
	int len = input_print_bitmap(buf, PAGE_SIZE,				\
				     input_dev->bm##bit, ev##_MAX, 1);		\
	return min_t(int, len, PAGE_SIZE);					\
}										\
static CLASS_DEVICE_ATTR(bm, S_IRUGO, input_dev_show_cap_##bm, NULL);

INPUT_DEV_CAP_ATTR(EV, ev);
INPUT_DEV_CAP_ATTR(KEY, key);
INPUT_DEV_CAP_ATTR(REL, rel);
INPUT_DEV_CAP_ATTR(ABS, abs);
INPUT_DEV_CAP_ATTR(MSC, msc);
INPUT_DEV_CAP_ATTR(LED, led);
INPUT_DEV_CAP_ATTR(SND, snd);
INPUT_DEV_CAP_ATTR(FF, ff);
INPUT_DEV_CAP_ATTR(SW, sw);

static struct attribute *input_dev_caps_attrs[] = {
	&class_device_attr_ev.attr,
	&class_device_attr_key.attr,
	&class_device_attr_rel.attr,
	&class_device_attr_abs.attr,
	&class_device_attr_msc.attr,
	&class_device_attr_led.attr,
	&class_device_attr_snd.attr,
	&class_device_attr_ff.attr,
	&class_device_attr_sw.attr,
	NULL
};

static struct attribute_group input_dev_caps_attr_group = {
	.name	= "capabilities",
	.attrs	= input_dev_caps_attrs,
};

static struct attribute_group *input_dev_attr_groups[] = {
	&input_dev_attr_group,
	&input_dev_id_attr_group,
	&input_dev_caps_attr_group,
	NULL
};

static void input_dev_release(struct class_device *class_dev)
{
	struct input_dev *dev = to_input_dev(class_dev);

	input_ff_destroy(dev);
	kfree(dev);

	module_put(THIS_MODULE);
}

/*
 * Input uevent interface - loading event handlers based on
 * device bitfields.
 */
static int input_add_uevent_bm_var(char **envp, int num_envp, int *cur_index,
				   char *buffer, int buffer_size, int *cur_len,
				   const char *name, unsigned long *bitmap, int max)
{
	if (*cur_index >= num_envp - 1)
		return -ENOMEM;

	envp[*cur_index] = buffer + *cur_len;

	*cur_len += snprintf(buffer + *cur_len, max(buffer_size - *cur_len, 0), name);
	if (*cur_len >= buffer_size)
		return -ENOMEM;

	*cur_len += input_print_bitmap(buffer + *cur_len,
					max(buffer_size - *cur_len, 0),
					bitmap, max, 0) + 1;
	if (*cur_len > buffer_size)
		return -ENOMEM;

	(*cur_index)++;
	return 0;
}

static int input_add_uevent_modalias_var(char **envp, int num_envp, int *cur_index,
					 char *buffer, int buffer_size, int *cur_len,
					 struct input_dev *dev)
{
	if (*cur_index >= num_envp - 1)
		return -ENOMEM;

	envp[*cur_index] = buffer + *cur_len;

	*cur_len += snprintf(buffer + *cur_len, max(buffer_size - *cur_len, 0),
			     "MODALIAS=");
	if (*cur_len >= buffer_size)
		return -ENOMEM;

	*cur_len += input_print_modalias(buffer + *cur_len,
					 max(buffer_size - *cur_len, 0),
					 dev, 0) + 1;
	if (*cur_len > buffer_size)
		return -ENOMEM;

	(*cur_index)++;
	return 0;
}

#define INPUT_ADD_HOTPLUG_VAR(fmt, val...)				\
	do {								\
		int err = add_uevent_var(envp, num_envp, &i,		\
					buffer, buffer_size, &len,	\
					fmt, val);			\
		if (err)						\
			return err;					\
	} while (0)

#define INPUT_ADD_HOTPLUG_BM_VAR(name, bm, max)				\
	do {								\
		int err = input_add_uevent_bm_var(envp, num_envp, &i,	\
					buffer, buffer_size, &len,	\
					name, bm, max);			\
		if (err)						\
			return err;					\
	} while (0)

#define INPUT_ADD_HOTPLUG_MODALIAS_VAR(dev)				\
	do {								\
		int err = input_add_uevent_modalias_var(envp,		\
					num_envp, &i,			\
					buffer, buffer_size, &len,	\
					dev);				\
		if (err)						\
			return err;					\
	} while (0)

static int input_dev_uevent(struct class_device *cdev, char **envp,
			    int num_envp, char *buffer, int buffer_size)
{
	struct input_dev *dev = to_input_dev(cdev);
	int i = 0;
	int len = 0;

	INPUT_ADD_HOTPLUG_VAR("PRODUCT=%x/%x/%x/%x",
				dev->id.bustype, dev->id.vendor,
				dev->id.product, dev->id.version);
	if (dev->name)
		INPUT_ADD_HOTPLUG_VAR("NAME=\"%s\"", dev->name);
	if (dev->phys)
		INPUT_ADD_HOTPLUG_VAR("PHYS=\"%s\"", dev->phys);
	if (dev->uniq)
		INPUT_ADD_HOTPLUG_VAR("UNIQ=\"%s\"", dev->uniq);

	INPUT_ADD_HOTPLUG_BM_VAR("EV=", dev->evbit, EV_MAX);
	if (test_bit(EV_KEY, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("KEY=", dev->keybit, KEY_MAX);
	if (test_bit(EV_REL, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("REL=", dev->relbit, REL_MAX);
	if (test_bit(EV_ABS, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("ABS=", dev->absbit, ABS_MAX);
	if (test_bit(EV_MSC, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("MSC=", dev->mscbit, MSC_MAX);
	if (test_bit(EV_LED, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("LED=", dev->ledbit, LED_MAX);
	if (test_bit(EV_SND, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("SND=", dev->sndbit, SND_MAX);
	if (test_bit(EV_FF, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("FF=", dev->ffbit, FF_MAX);
	if (test_bit(EV_SW, dev->evbit))
		INPUT_ADD_HOTPLUG_BM_VAR("SW=", dev->swbit, SW_MAX);

	INPUT_ADD_HOTPLUG_MODALIAS_VAR(dev);

	envp[i] = NULL;
	return 0;
}

struct class input_class = {
	.name			= "input",
	.release		= input_dev_release,
	.uevent			= input_dev_uevent,
};
EXPORT_SYMBOL_GPL(input_class);

/**
 * input_allocate_device - allocate memory for new input device
 *
 * Returns prepared struct input_dev or NULL.
 *
 * NOTE: Use input_free_device() to free devices that have not been
 * registered; input_unregister_device() should be used for already
 * registered devices.
 */
struct input_dev *input_allocate_device(void)
{
	struct input_dev *dev;

	dev = kzalloc(sizeof(struct input_dev), GFP_KERNEL);
	if (dev) {
		dev->cdev.class = &input_class;
		dev->cdev.groups = input_dev_attr_groups;
		class_device_initialize(&dev->cdev);
		mutex_init(&dev->mutex);
		INIT_LIST_HEAD(&dev->h_list);
		INIT_LIST_HEAD(&dev->node);

		__module_get(THIS_MODULE);
	}

	return dev;
}
EXPORT_SYMBOL(input_allocate_device);

/**
 * input_free_device - free memory occupied by input_dev structure
 * @dev: input device to free
 *
 * This function should only be used if input_register_device()
 * was not called yet or if it failed. Once device was registered
 * use input_unregister_device() and memory will be freed once last
 * refrence to the device is dropped.
 *
 * Device should be allocated by input_allocate_device().
 *
 * NOTE: If there are references to the input device then memory
 * will not be freed until last reference is dropped.
 */
void input_free_device(struct input_dev *dev)
{
	if (dev)
		input_put_device(dev);
}
EXPORT_SYMBOL(input_free_device);

/**
 * input_set_capability - mark device as capable of a certain event
 * @dev: device that is capable of emitting or accepting event
 * @type: type of the event (EV_KEY, EV_REL, etc...)
 * @code: event code
 *
 * In addition to setting up corresponding bit in appropriate capability
 * bitmap the function also adjusts dev->evbit.
 */
void input_set_capability(struct input_dev *dev, unsigned int type, unsigned int code)
{
	switch (type) {
	case EV_KEY:
		__set_bit(code, dev->keybit);
		break;

	case EV_REL:
		__set_bit(code, dev->relbit);
		break;

	case EV_ABS:
		__set_bit(code, dev->absbit);
		break;

	case EV_MSC:
		__set_bit(code, dev->mscbit);
		break;

	case EV_SW:
		__set_bit(code, dev->swbit);
		break;

	case EV_LED:
		__set_bit(code, dev->ledbit);
		break;

	case EV_SND:
		__set_bit(code, dev->sndbit);
		break;

	case EV_FF:
		__set_bit(code, dev->ffbit);
		break;

	default:
		printk(KERN_ERR
			"input_set_capability: unknown type %u (code %u)\n",
			type, code);
		dump_stack();
		return;
	}

	__set_bit(type, dev->evbit);
}
EXPORT_SYMBOL(input_set_capability);

int input_register_device(struct input_dev *dev)
{
	static atomic_t input_no = ATOMIC_INIT(0);
	struct input_handler *handler;
	const char *path;
	int error;

	set_bit(EV_SYN, dev->evbit);

	/*
	 * If delay and period are pre-set by the driver, then autorepeating
	 * is handled by the driver itself and we don't do it in input.c.
	 */

	init_timer(&dev->timer);
	if (!dev->rep[REP_DELAY] && !dev->rep[REP_PERIOD]) {
		dev->timer.data = (long) dev;
		dev->timer.function = input_repeat_key;
		dev->rep[REP_DELAY] = 250;
		dev->rep[REP_PERIOD] = 33;
	}

	if (!dev->getkeycode)
		dev->getkeycode = input_default_getkeycode;

	if (!dev->setkeycode)
		dev->setkeycode = input_default_setkeycode;

	list_add_tail(&dev->node, &input_dev_list);

	snprintf(dev->cdev.class_id, sizeof(dev->cdev.class_id),
		 "input%ld", (unsigned long) atomic_inc_return(&input_no) - 1);

	if (!dev->cdev.dev)
		dev->cdev.dev = dev->dev.parent;

	error = class_device_add(&dev->cdev);
	if (error)
		return error;

	path = kobject_get_path(&dev->cdev.kobj, GFP_KERNEL);
	printk(KERN_INFO "input: %s as %s\n",
		dev->name ? dev->name : "Unspecified device", path ? path : "N/A");
	kfree(path);

	list_for_each_entry(handler, &input_handler_list, node)
		input_attach_handler(dev, handler);

	input_wakeup_procfs_readers();

	return 0;
}
EXPORT_SYMBOL(input_register_device);

void input_unregister_device(struct input_dev *dev)
{
	struct input_handle *handle, *next;
	int code;

	for (code = 0; code <= KEY_MAX; code++)
		if (test_bit(code, dev->key))
			input_report_key(dev, code, 0);
	input_sync(dev);

	del_timer_sync(&dev->timer);

	list_for_each_entry_safe(handle, next, &dev->h_list, d_node)
		handle->handler->disconnect(handle);
	WARN_ON(!list_empty(&dev->h_list));

	list_del_init(&dev->node);

	class_device_unregister(&dev->cdev);

	input_wakeup_procfs_readers();
}
EXPORT_SYMBOL(input_unregister_device);

int input_register_handler(struct input_handler *handler)
{
	struct input_dev *dev;

	INIT_LIST_HEAD(&handler->h_list);

	if (handler->fops != NULL) {
		if (input_table[handler->minor >> 5])
			return -EBUSY;

		input_table[handler->minor >> 5] = handler;
	}

	list_add_tail(&handler->node, &input_handler_list);

	list_for_each_entry(dev, &input_dev_list, node)
		input_attach_handler(dev, handler);

	input_wakeup_procfs_readers();
	return 0;
}
EXPORT_SYMBOL(input_register_handler);

void input_unregister_handler(struct input_handler *handler)
{
	struct input_handle *handle, *next;

	list_for_each_entry_safe(handle, next, &handler->h_list, h_node)
		handler->disconnect(handle);
	WARN_ON(!list_empty(&handler->h_list));

	list_del_init(&handler->node);

	if (handler->fops != NULL)
		input_table[handler->minor >> 5] = NULL;

	input_wakeup_procfs_readers();
}
EXPORT_SYMBOL(input_unregister_handler);

int input_register_handle(struct input_handle *handle)
{
	struct input_handler *handler = handle->handler;

	list_add_tail(&handle->d_node, &handle->dev->h_list);
	list_add_tail(&handle->h_node, &handler->h_list);

	if (handler->start)
		handler->start(handle);

	return 0;
}
EXPORT_SYMBOL(input_register_handle);

void input_unregister_handle(struct input_handle *handle)
{
	list_del_init(&handle->h_node);
	list_del_init(&handle->d_node);
}
EXPORT_SYMBOL(input_unregister_handle);

static int input_open_file(struct inode *inode, struct file *file)
{
	struct input_handler *handler = input_table[iminor(inode) >> 5];
	const struct file_operations *old_fops, *new_fops = NULL;
	int err;

	/* No load-on-demand here? */
	if (!handler || !(new_fops = fops_get(handler->fops)))
		return -ENODEV;

	/*
	 * That's _really_ odd. Usually NULL ->open means "nothing special",
	 * not "no device". Oh, well...
	 */
	if (!new_fops->open) {
		fops_put(new_fops);
		return -ENODEV;
	}
	old_fops = file->f_op;
	file->f_op = new_fops;

	err = new_fops->open(inode, file);

	if (err) {
		fops_put(file->f_op);
		file->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);
	return err;
}

static const struct file_operations input_fops = {
	.owner = THIS_MODULE,
	.open = input_open_file,
};

static int __init input_init(void)
{
	int err;

	err = class_register(&input_class);
	if (err) {
		printk(KERN_ERR "input: unable to register input_dev class\n");
		return err;
	}

	err = input_proc_init();
	if (err)
		goto fail1;

	err = register_chrdev(INPUT_MAJOR, "input", &input_fops);
	if (err) {
		printk(KERN_ERR "input: unable to register char major %d", INPUT_MAJOR);
		goto fail2;
	}

	return 0;

 fail2:	input_proc_exit();
 fail1:	class_unregister(&input_class);
	return err;
}

static void __exit input_exit(void)
{
	input_proc_exit();
	unregister_chrdev(INPUT_MAJOR, "input");
	class_unregister(&input_class);
}

subsys_initcall(input_init);
module_exit(input_exit);
