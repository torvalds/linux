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
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/major.h>
#include <linux/proc_fs.h>
#include <linux/kobject_uevent.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/device.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("Input core");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(input_allocate_device);
EXPORT_SYMBOL(input_register_device);
EXPORT_SYMBOL(input_unregister_device);
EXPORT_SYMBOL(input_register_handler);
EXPORT_SYMBOL(input_unregister_handler);
EXPORT_SYMBOL(input_grab_device);
EXPORT_SYMBOL(input_release_device);
EXPORT_SYMBOL(input_open_device);
EXPORT_SYMBOL(input_close_device);
EXPORT_SYMBOL(input_accept_process);
EXPORT_SYMBOL(input_flush_device);
EXPORT_SYMBOL(input_event);
EXPORT_SYMBOL_GPL(input_class);

#define INPUT_DEVICES	256

static LIST_HEAD(input_dev_list);
static LIST_HEAD(input_handler_list);

static struct input_handler *input_table[8];

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
					if (dev->event) dev->event(dev, type, code, value);
					break;

				case SYN_REPORT:
					if (dev->sync) return;
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

			if (dev->event) dev->event(dev, type, code, value);

			break;

		case EV_LED:

			if (code > LED_MAX || !test_bit(code, dev->ledbit) || !!test_bit(code, dev->led) == value)
				return;

			change_bit(code, dev->led);
			if (dev->event) dev->event(dev, type, code, value);

			break;

		case EV_SND:

			if (code > SND_MAX || !test_bit(code, dev->sndbit))
				return;

			if (dev->event) dev->event(dev, type, code, value);

			break;

		case EV_REP:

			if (code > REP_MAX || value < 0 || dev->rep[code] == value) return;

			dev->rep[code] = value;
			if (dev->event) dev->event(dev, type, code, value);

			break;

		case EV_FF:
			if (dev->event) dev->event(dev, type, code, value);
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

int input_accept_process(struct input_handle *handle, struct file *file)
{
	if (handle->dev->accept)
		return handle->dev->accept(handle->dev, file);

	return 0;
}

int input_grab_device(struct input_handle *handle)
{
	if (handle->dev->grab)
		return -EBUSY;

	handle->dev->grab = handle;
	return 0;
}

void input_release_device(struct input_handle *handle)
{
	if (handle->dev->grab == handle)
		handle->dev->grab = NULL;
}

int input_open_device(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;
	int err;

	err = down_interruptible(&dev->sem);
	if (err)
		return err;

	handle->open++;

	if (!dev->users++ && dev->open)
		err = dev->open(dev);

	if (err)
		handle->open--;

	up(&dev->sem);

	return err;
}

int input_flush_device(struct input_handle* handle, struct file* file)
{
	if (handle->dev->flush)
		return handle->dev->flush(handle->dev, file);

	return 0;
}

void input_close_device(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;

	input_release_device(handle);

	down(&dev->sem);

	if (!--dev->users && dev->close)
		dev->close(dev);
	handle->open--;

	up(&dev->sem);
}

static void input_link_handle(struct input_handle *handle)
{
	list_add_tail(&handle->d_node, &handle->dev->h_list);
	list_add_tail(&handle->h_node, &handle->handler->h_list);
}

#define MATCH_BIT(bit, max) \
		for (i = 0; i < NBITS(max); i++) \
			if ((id->bit[i] & dev->bit[i]) != id->bit[i]) \
				break; \
		if (i != NBITS(max)) \
			continue;

static struct input_device_id *input_match_device(struct input_device_id *id, struct input_dev *dev)
{
	int i;

	for (; id->flags || id->driver_info; id++) {

		if (id->flags & INPUT_DEVICE_ID_MATCH_BUS)
			if (id->id.bustype != dev->id.bustype)
				continue;

		if (id->flags & INPUT_DEVICE_ID_MATCH_VENDOR)
			if (id->id.vendor != dev->id.vendor)
				continue;

		if (id->flags & INPUT_DEVICE_ID_MATCH_PRODUCT)
			if (id->id.product != dev->id.product)
				continue;

		if (id->flags & INPUT_DEVICE_ID_MATCH_VERSION)
			if (id->id.version != dev->id.version)
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

static int input_print_bitmap(char *buf, int buf_size, unsigned long *bitmap, int max)
{
	int i;
	int len = 0;

	for (i = NBITS(max) - 1; i > 0; i--)
		if (bitmap[i])
			break;

	for (; i >= 0; i--)
		len += snprintf(buf + len, max(buf_size - len, 0),
				"%lx%s", bitmap[i], i > 0 ? " " : "");
	return len;
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

static unsigned int input_devices_poll(struct file *file, poll_table *wait)
{
	int state = input_devices_state;
	poll_wait(file, &input_devices_poll_wait, wait);
	if (state != input_devices_state)
		return POLLIN | POLLRDNORM;
	return 0;
}

#define SPRINTF_BIT(ev, bm)						\
	do {								\
		len += sprintf(buf + len, "B: %s=", #ev);		\
		len += input_print_bitmap(buf + len, INT_MAX,		\
					dev->bm##bit, ev##_MAX);	\
		len += sprintf(buf + len, "\n");			\
	} while (0)

#define TEST_AND_SPRINTF_BIT(ev, bm)					\
	do {								\
		if (test_bit(EV_##ev, dev->evbit))			\
			SPRINTF_BIT(ev, bm);				\
	} while (0)

static int input_devices_read(char *buf, char **start, off_t pos, int count, int *eof, void *data)
{
	struct input_dev *dev;
	struct input_handle *handle;
	const char *path;

	off_t at = 0;
	int len, cnt = 0;

	list_for_each_entry(dev, &input_dev_list, node) {

		path = dev->dynalloc ? kobject_get_path(&dev->cdev.kobj, GFP_KERNEL) : NULL;

		len = sprintf(buf, "I: Bus=%04x Vendor=%04x Product=%04x Version=%04x\n",
			dev->id.bustype, dev->id.vendor, dev->id.product, dev->id.version);

		len += sprintf(buf + len, "N: Name=\"%s\"\n", dev->name ? dev->name : "");
		len += sprintf(buf + len, "P: Phys=%s\n", dev->phys ? dev->phys : "");
		len += sprintf(buf + len, "S: Sysfs=%s\n", path ? path : "");
		len += sprintf(buf + len, "H: Handlers=");

		list_for_each_entry(handle, &dev->h_list, d_node)
			len += sprintf(buf + len, "%s ", handle->name);

		len += sprintf(buf + len, "\n");

		SPRINTF_BIT(EV, ev);
		TEST_AND_SPRINTF_BIT(KEY, key);
		TEST_AND_SPRINTF_BIT(REL, rel);
		TEST_AND_SPRINTF_BIT(ABS, abs);
		TEST_AND_SPRINTF_BIT(MSC, msc);
		TEST_AND_SPRINTF_BIT(LED, led);
		TEST_AND_SPRINTF_BIT(SND, snd);
		TEST_AND_SPRINTF_BIT(FF, ff);
		TEST_AND_SPRINTF_BIT(SW, sw);

		len += sprintf(buf + len, "\n");

		at += len;

		if (at >= pos) {
			if (!*start) {
				*start = buf + (pos - (at - len));
				cnt = at - pos;
			} else  cnt += len;
			buf += len;
			if (cnt >= count)
				break;
		}

		kfree(path);
	}

	if (&dev->node == &input_dev_list)
		*eof = 1;

	return (count > cnt) ? cnt : count;
}

static int input_handlers_read(char *buf, char **start, off_t pos, int count, int *eof, void *data)
{
	struct input_handler *handler;

	off_t at = 0;
	int len = 0, cnt = 0;
	int i = 0;

	list_for_each_entry(handler, &input_handler_list, node) {

		if (handler->fops)
			len = sprintf(buf, "N: Number=%d Name=%s Minor=%d\n",
				i++, handler->name, handler->minor);
		else
			len = sprintf(buf, "N: Number=%d Name=%s\n",
				i++, handler->name);

		at += len;

		if (at >= pos) {
			if (!*start) {
				*start = buf + (pos - (at - len));
				cnt = at - pos;
			} else  cnt += len;
			buf += len;
			if (cnt >= count)
				break;
		}
	}
	if (&handler->node == &input_handler_list)
		*eof = 1;

	return (count > cnt) ? cnt : count;
}

static struct file_operations input_fileops;

static int __init input_proc_init(void)
{
	struct proc_dir_entry *entry;

	proc_bus_input_dir = proc_mkdir("input", proc_bus);
	if (!proc_bus_input_dir)
		return -ENOMEM;

	proc_bus_input_dir->owner = THIS_MODULE;

	entry = create_proc_read_entry("devices", 0, proc_bus_input_dir, input_devices_read, NULL);
	if (!entry)
		goto fail1;

	entry->owner = THIS_MODULE;
	input_fileops = *entry->proc_fops;
	entry->proc_fops = &input_fileops;
	entry->proc_fops->poll = input_devices_poll;

	entry = create_proc_read_entry("handlers", 0, proc_bus_input_dir, input_handlers_read, NULL);
	if (!entry)
		goto fail2;

	entry->owner = THIS_MODULE;

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
	int retval;								\
										\
	retval = down_interruptible(&input_dev->sem);				\
	if (retval)								\
		return retval;							\
										\
	retval = sprintf(buf, "%s\n", input_dev->name ? input_dev->name : "");	\
										\
	up(&input_dev->sem);							\
										\
	return retval;								\
}										\
static CLASS_DEVICE_ATTR(name, S_IRUGO, input_dev_show_##name, NULL);

INPUT_DEV_STRING_ATTR_SHOW(name);
INPUT_DEV_STRING_ATTR_SHOW(phys);
INPUT_DEV_STRING_ATTR_SHOW(uniq);

static struct attribute *input_dev_attrs[] = {
	&class_device_attr_name.attr,
	&class_device_attr_phys.attr,
	&class_device_attr_uniq.attr,
	NULL
};

static struct attribute_group input_dev_group = {
	.attrs	= input_dev_attrs,
};

#define INPUT_DEV_ID_ATTR(name)							\
static ssize_t input_dev_show_id_##name(struct class_device *dev, char *buf)	\
{										\
	struct input_dev *input_dev = to_input_dev(dev);			\
	return sprintf(buf, "%04x\n", input_dev->id.name);			\
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

#define INPUT_DEV_CAP_ATTR(ev, bm)						\
static ssize_t input_dev_show_cap_##bm(struct class_device *dev, char *buf)	\
{										\
	struct input_dev *input_dev = to_input_dev(dev);			\
	return input_print_bitmap(buf, PAGE_SIZE, input_dev->bm##bit, ev##_MAX);\
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

static void input_dev_release(struct class_device *class_dev)
{
	struct input_dev *dev = to_input_dev(class_dev);

	kfree(dev);
	module_put(THIS_MODULE);
}

/*
 * Input hotplugging interface - loading event handlers based on
 * device bitfields.
 */
static int input_add_hotplug_bm_var(char **envp, int num_envp, int *cur_index,
				    char *buffer, int buffer_size, int *cur_len,
				    const char *name, unsigned long *bitmap, int max)
{
	if (*cur_index >= num_envp - 1)
		return -ENOMEM;

	envp[*cur_index] = buffer + *cur_len;

	*cur_len += snprintf(buffer + *cur_len, max(buffer_size - *cur_len, 0), name);
	if (*cur_len > buffer_size)
		return -ENOMEM;

	*cur_len += input_print_bitmap(buffer + *cur_len,
					max(buffer_size - *cur_len, 0),
					bitmap, max) + 1;
	if (*cur_len > buffer_size)
		return -ENOMEM;

	(*cur_index)++;
	return 0;
}

#define INPUT_ADD_HOTPLUG_VAR(fmt, val...)				\
	do {								\
		int err = add_hotplug_env_var(envp, num_envp, &i,	\
					buffer, buffer_size, &len,	\
					fmt, val);			\
		if (err)						\
			return err;					\
	} while (0)

#define INPUT_ADD_HOTPLUG_BM_VAR(name, bm, max)				\
	do {								\
		int err = input_add_hotplug_bm_var(envp, num_envp, &i,	\
					buffer, buffer_size, &len,	\
					name, bm, max);			\
		if (err)						\
			return err;					\
	} while (0)

static int input_dev_hotplug(struct class_device *cdev, char **envp,
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
	if (dev->phys)
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

	envp[i] = NULL;

	return 0;
}

struct class input_class = {
	.name			= "input",
	.release		= input_dev_release,
	.hotplug		= input_dev_hotplug,
};

struct input_dev *input_allocate_device(void)
{
	struct input_dev *dev;

	dev = kzalloc(sizeof(struct input_dev), GFP_KERNEL);
	if (dev) {
		dev->dynalloc = 1;
		dev->cdev.class = &input_class;
		class_device_initialize(&dev->cdev);
		INIT_LIST_HEAD(&dev->h_list);
		INIT_LIST_HEAD(&dev->node);
	}

	return dev;
}

static void input_register_classdevice(struct input_dev *dev)
{
	static atomic_t input_no = ATOMIC_INIT(0);
	const char *path;

	__module_get(THIS_MODULE);

	dev->dev = dev->cdev.dev;

	snprintf(dev->cdev.class_id, sizeof(dev->cdev.class_id),
		 "input%ld", (unsigned long) atomic_inc_return(&input_no) - 1);

	path = kobject_get_path(&dev->cdev.class->subsys.kset.kobj, GFP_KERNEL);
	printk(KERN_INFO "input: %s/%s as %s\n",
		dev->name ? dev->name : "Unspecified device",
		path ? path : "", dev->cdev.class_id);
	kfree(path);

	class_device_add(&dev->cdev);
	sysfs_create_group(&dev->cdev.kobj, &input_dev_group);
	sysfs_create_group(&dev->cdev.kobj, &input_dev_id_attr_group);
	sysfs_create_group(&dev->cdev.kobj, &input_dev_caps_attr_group);
}

void input_register_device(struct input_dev *dev)
{
	struct input_handle *handle;
	struct input_handler *handler;
	struct input_device_id *id;

	set_bit(EV_SYN, dev->evbit);

	init_MUTEX(&dev->sem);

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

	INIT_LIST_HEAD(&dev->h_list);
	list_add_tail(&dev->node, &input_dev_list);

	if (dev->dynalloc)
		input_register_classdevice(dev);

	list_for_each_entry(handler, &input_handler_list, node)
		if (!handler->blacklist || !input_match_device(handler->blacklist, dev))
			if ((id = input_match_device(handler->id_table, dev)))
				if ((handle = handler->connect(handler, dev, id)))
					input_link_handle(handle);


	input_wakeup_procfs_readers();
}

void input_unregister_device(struct input_dev *dev)
{
	struct list_head * node, * next;

	if (!dev) return;

	del_timer_sync(&dev->timer);

	list_for_each_safe(node, next, &dev->h_list) {
		struct input_handle * handle = to_handle(node);
		list_del_init(&handle->d_node);
		list_del_init(&handle->h_node);
		handle->handler->disconnect(handle);
	}

	list_del_init(&dev->node);

	if (dev->dynalloc) {
		sysfs_remove_group(&dev->cdev.kobj, &input_dev_caps_attr_group);
		sysfs_remove_group(&dev->cdev.kobj, &input_dev_id_attr_group);
		class_device_unregister(&dev->cdev);
	}

	input_wakeup_procfs_readers();
}

void input_register_handler(struct input_handler *handler)
{
	struct input_dev *dev;
	struct input_handle *handle;
	struct input_device_id *id;

	if (!handler) return;

	INIT_LIST_HEAD(&handler->h_list);

	if (handler->fops != NULL)
		input_table[handler->minor >> 5] = handler;

	list_add_tail(&handler->node, &input_handler_list);

	list_for_each_entry(dev, &input_dev_list, node)
		if (!handler->blacklist || !input_match_device(handler->blacklist, dev))
			if ((id = input_match_device(handler->id_table, dev)))
				if ((handle = handler->connect(handler, dev, id)))
					input_link_handle(handle);

	input_wakeup_procfs_readers();
}

void input_unregister_handler(struct input_handler *handler)
{
	struct list_head * node, * next;

	list_for_each_safe(node, next, &handler->h_list) {
		struct input_handle * handle = to_handle_h(node);
		list_del_init(&handle->h_node);
		list_del_init(&handle->d_node);
		handler->disconnect(handle);
	}

	list_del_init(&handler->node);

	if (handler->fops != NULL)
		input_table[handler->minor >> 5] = NULL;

	input_wakeup_procfs_readers();
}

static int input_open_file(struct inode *inode, struct file *file)
{
	struct input_handler *handler = input_table[iminor(inode) >> 5];
	struct file_operations *old_fops, *new_fops = NULL;
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

static struct file_operations input_fops = {
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
