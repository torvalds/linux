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
#include <linux/devfs_fs_kernel.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("Input core");
MODULE_LICENSE("GPL");

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
EXPORT_SYMBOL(input_class);

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


/*
 * Input hotplugging interface - loading event handlers based on
 * device bitfields.
 */

#ifdef CONFIG_HOTPLUG

/*
 * Input hotplugging invokes what /proc/sys/kernel/hotplug says
 * (normally /sbin/hotplug) when input devices get added or removed.
 *
 * This invokes a user mode policy agent, typically helping to load driver
 * or other modules, configure the device, and more.  Drivers can provide
 * a MODULE_DEVICE_TABLE to help with module loading subtasks.
 *
 */

#define SPRINTF_BIT_A(bit, name, max) \
	do { \
		envp[i++] = scratch; \
		scratch += sprintf(scratch, name); \
		for (j = NBITS(max) - 1; j >= 0; j--) \
			if (dev->bit[j]) break; \
		for (; j >= 0; j--) \
			scratch += sprintf(scratch, "%lx ", dev->bit[j]); \
		scratch++; \
	} while (0)

#define SPRINTF_BIT_A2(bit, name, max, ev) \
	do { \
		if (test_bit(ev, dev->evbit)) \
			SPRINTF_BIT_A(bit, name, max); \
	} while (0)

static void input_call_hotplug(char *verb, struct input_dev *dev)
{
	char *argv[3], **envp, *buf, *scratch;
	int i = 0, j, value;

	if (!hotplug_path[0]) {
		printk(KERN_ERR "input.c: calling hotplug without a hotplug agent defined\n");
		return;
	}
	if (in_interrupt()) {
		printk(KERN_ERR "input.c: calling hotplug from interrupt\n");
		return;
	}
	if (!current->fs->root) {
		printk(KERN_WARNING "input.c: calling hotplug without valid filesystem\n");
		return;
	}
	if (!(envp = (char **) kmalloc(20 * sizeof(char *), GFP_KERNEL))) {
		printk(KERN_ERR "input.c: not enough memory allocating hotplug environment\n");
		return;
	}
	if (!(buf = kmalloc(1024, GFP_KERNEL))) {
		kfree (envp);
		printk(KERN_ERR "input.c: not enough memory allocating hotplug environment\n");
		return;
	}

	argv[0] = hotplug_path;
	argv[1] = "input";
	argv[2] = NULL;

	envp[i++] = "HOME=/";
	envp[i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";

	scratch = buf;

	envp[i++] = scratch;
	scratch += sprintf(scratch, "ACTION=%s", verb) + 1;

	envp[i++] = scratch;
	scratch += sprintf(scratch, "PRODUCT=%x/%x/%x/%x",
		dev->id.bustype, dev->id.vendor, dev->id.product, dev->id.version) + 1;

	if (dev->name) {
		envp[i++] = scratch;
		scratch += sprintf(scratch, "NAME=%s", dev->name) + 1;
	}

	if (dev->phys) {
		envp[i++] = scratch;
		scratch += sprintf(scratch, "PHYS=%s", dev->phys) + 1;
	}

	SPRINTF_BIT_A(evbit, "EV=", EV_MAX);
	SPRINTF_BIT_A2(keybit, "KEY=", KEY_MAX, EV_KEY);
	SPRINTF_BIT_A2(relbit, "REL=", REL_MAX, EV_REL);
	SPRINTF_BIT_A2(absbit, "ABS=", ABS_MAX, EV_ABS);
	SPRINTF_BIT_A2(mscbit, "MSC=", MSC_MAX, EV_MSC);
	SPRINTF_BIT_A2(ledbit, "LED=", LED_MAX, EV_LED);
	SPRINTF_BIT_A2(sndbit, "SND=", SND_MAX, EV_SND);
	SPRINTF_BIT_A2(ffbit,  "FF=",  FF_MAX, EV_FF);
	SPRINTF_BIT_A2(swbit,  "SW=",  SW_MAX, EV_SW);

	envp[i++] = NULL;

#ifdef INPUT_DEBUG
	printk(KERN_DEBUG "input.c: calling %s %s [%s %s %s %s %s]\n",
		argv[0], argv[1], envp[0], envp[1], envp[2], envp[3], envp[4]);
#endif

	value = call_usermodehelper(argv [0], argv, envp, 0);

	kfree(buf);
	kfree(envp);

#ifdef INPUT_DEBUG
	if (value != 0)
		printk(KERN_DEBUG "input.c: hotplug returned %d\n", value);
#endif
}

#endif

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

#define SPRINTF_BIT_B(bit, name, max) \
	do { \
		len += sprintf(buf + len, "B: %s", name); \
		for (i = NBITS(max) - 1; i >= 0; i--) \
			if (dev->bit[i]) break; \
		for (; i >= 0; i--) \
			len += sprintf(buf + len, "%lx ", dev->bit[i]); \
		len += sprintf(buf + len, "\n"); \
	} while (0)

#define SPRINTF_BIT_B2(bit, name, max, ev) \
	do { \
		if (test_bit(ev, dev->evbit)) \
			SPRINTF_BIT_B(bit, name, max); \
	} while (0)

static int input_devices_read(char *buf, char **start, off_t pos, int count, int *eof, void *data)
{
	struct input_dev *dev;
	struct input_handle *handle;

	off_t at = 0;
	int i, len, cnt = 0;

	list_for_each_entry(dev, &input_dev_list, node) {

		len = sprintf(buf, "I: Bus=%04x Vendor=%04x Product=%04x Version=%04x\n",
			dev->id.bustype, dev->id.vendor, dev->id.product, dev->id.version);

		len += sprintf(buf + len, "N: Name=\"%s\"\n", dev->name ? dev->name : "");
		len += sprintf(buf + len, "P: Phys=%s\n", dev->phys ? dev->phys : "");
		len += sprintf(buf + len, "H: Handlers=");

		list_for_each_entry(handle, &dev->h_list, d_node)
			len += sprintf(buf + len, "%s ", handle->name);

		len += sprintf(buf + len, "\n");

		SPRINTF_BIT_B(evbit, "EV=", EV_MAX);
		SPRINTF_BIT_B2(keybit, "KEY=", KEY_MAX, EV_KEY);
		SPRINTF_BIT_B2(relbit, "REL=", REL_MAX, EV_REL);
		SPRINTF_BIT_B2(absbit, "ABS=", ABS_MAX, EV_ABS);
		SPRINTF_BIT_B2(mscbit, "MSC=", MSC_MAX, EV_MSC);
		SPRINTF_BIT_B2(ledbit, "LED=", LED_MAX, EV_LED);
		SPRINTF_BIT_B2(sndbit, "SND=", SND_MAX, EV_SND);
		SPRINTF_BIT_B2(ffbit,  "FF=",  FF_MAX, EV_FF);
		SPRINTF_BIT_B2(swbit,  "SW=",  SW_MAX, EV_SW);

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

	list_for_each_entry(handler, &input_handler_list, node)
		if (!handler->blacklist || !input_match_device(handler->blacklist, dev))
			if ((id = input_match_device(handler->id_table, dev)))
				if ((handle = handler->connect(handler, dev, id)))
					input_link_handle(handle);

#ifdef CONFIG_HOTPLUG
	input_call_hotplug("add", dev);
#endif

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

#ifdef CONFIG_HOTPLUG
	input_call_hotplug("remove", dev);
#endif

	list_del_init(&dev->node);

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

struct class *input_class;

static int __init input_init(void)
{
	int err;

	input_class = class_create(THIS_MODULE, "input");
	if (IS_ERR(input_class)) {
		printk(KERN_ERR "input: unable to register input class\n");
		return PTR_ERR(input_class);
	}

	err = input_proc_init();
	if (err)
		goto fail1;

	err = register_chrdev(INPUT_MAJOR, "input", &input_fops);
	if (err) {
		printk(KERN_ERR "input: unable to register char major %d", INPUT_MAJOR);
		goto fail2;
	}

	err = devfs_mk_dir("input");
	if (err)
		goto fail3;

	return 0;

 fail3:	unregister_chrdev(INPUT_MAJOR, "input");
 fail2:	input_proc_exit();
 fail1:	class_destroy(input_class);
	return err;
}

static void __exit input_exit(void)
{
	input_proc_exit();
	devfs_remove("input");
	unregister_chrdev(INPUT_MAJOR, "input");
	class_destroy(input_class);
}

subsys_initcall(input_init);
module_exit(input_exit);
