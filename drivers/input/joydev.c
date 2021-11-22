// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Joystick device driver for the input driver suite.
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 * Copyright (c) 1999 Colin Van Dyke
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/joystick.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/cdev.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Joystick device interfaces");
MODULE_LICENSE("GPL");

#define JOYDEV_MINOR_BASE	0
#define JOYDEV_MINORS		16
#define JOYDEV_BUFFER_SIZE	64

struct joydev {
	int open;
	struct input_handle handle;
	wait_queue_head_t wait;
	struct list_head client_list;
	spinlock_t client_lock; /* protects client_list */
	struct mutex mutex;
	struct device dev;
	struct cdev cdev;
	bool exist;

	struct js_corr corr[ABS_CNT];
	struct JS_DATA_SAVE_TYPE glue;
	int nabs;
	int nkey;
	__u16 keymap[KEY_MAX - BTN_MISC + 1];
	__u16 keypam[KEY_MAX - BTN_MISC + 1];
	__u8 absmap[ABS_CNT];
	__u8 abspam[ABS_CNT];
	__s16 abs[ABS_CNT];
};

struct joydev_client {
	struct js_event buffer[JOYDEV_BUFFER_SIZE];
	int head;
	int tail;
	int startup;
	spinlock_t buffer_lock; /* protects access to buffer, head and tail */
	struct fasync_struct *fasync;
	struct joydev *joydev;
	struct list_head node;
};

static int joydev_correct(int value, struct js_corr *corr)
{
	switch (corr->type) {

	case JS_CORR_NONE:
		break;

	case JS_CORR_BROKEN:
		value = value > corr->coef[0] ? (value < corr->coef[1] ? 0 :
			((corr->coef[3] * (value - corr->coef[1])) >> 14)) :
			((corr->coef[2] * (value - corr->coef[0])) >> 14);
		break;

	default:
		return 0;
	}

	return clamp(value, -32767, 32767);
}

static void joydev_pass_event(struct joydev_client *client,
			      struct js_event *event)
{
	struct joydev *joydev = client->joydev;

	/*
	 * IRQs already disabled, just acquire the lock
	 */
	spin_lock(&client->buffer_lock);

	client->buffer[client->head] = *event;

	if (client->startup == joydev->nabs + joydev->nkey) {
		client->head++;
		client->head &= JOYDEV_BUFFER_SIZE - 1;
		if (client->tail == client->head)
			client->startup = 0;
	}

	spin_unlock(&client->buffer_lock);

	kill_fasync(&client->fasync, SIGIO, POLL_IN);
}

static void joydev_event(struct input_handle *handle,
			 unsigned int type, unsigned int code, int value)
{
	struct joydev *joydev = handle->private;
	struct joydev_client *client;
	struct js_event event;

	switch (type) {

	case EV_KEY:
		if (code < BTN_MISC || value == 2)
			return;
		event.type = JS_EVENT_BUTTON;
		event.number = joydev->keymap[code - BTN_MISC];
		event.value = value;
		break;

	case EV_ABS:
		event.type = JS_EVENT_AXIS;
		event.number = joydev->absmap[code];
		event.value = joydev_correct(value,
					&joydev->corr[event.number]);
		if (event.value == joydev->abs[event.number])
			return;
		joydev->abs[event.number] = event.value;
		break;

	default:
		return;
	}

	event.time = jiffies_to_msecs(jiffies);

	rcu_read_lock();
	list_for_each_entry_rcu(client, &joydev->client_list, node)
		joydev_pass_event(client, &event);
	rcu_read_unlock();

	wake_up_interruptible(&joydev->wait);
}

static int joydev_fasync(int fd, struct file *file, int on)
{
	struct joydev_client *client = file->private_data;

	return fasync_helper(fd, file, on, &client->fasync);
}

static void joydev_free(struct device *dev)
{
	struct joydev *joydev = container_of(dev, struct joydev, dev);

	input_put_device(joydev->handle.dev);
	kfree(joydev);
}

static void joydev_attach_client(struct joydev *joydev,
				 struct joydev_client *client)
{
	spin_lock(&joydev->client_lock);
	list_add_tail_rcu(&client->node, &joydev->client_list);
	spin_unlock(&joydev->client_lock);
}

static void joydev_detach_client(struct joydev *joydev,
				 struct joydev_client *client)
{
	spin_lock(&joydev->client_lock);
	list_del_rcu(&client->node);
	spin_unlock(&joydev->client_lock);
	synchronize_rcu();
}

static void joydev_refresh_state(struct joydev *joydev)
{
	struct input_dev *dev = joydev->handle.dev;
	int i, val;

	for (i = 0; i < joydev->nabs; i++) {
		val = input_abs_get_val(dev, joydev->abspam[i]);
		joydev->abs[i] = joydev_correct(val, &joydev->corr[i]);
	}
}

static int joydev_open_device(struct joydev *joydev)
{
	int retval;

	retval = mutex_lock_interruptible(&joydev->mutex);
	if (retval)
		return retval;

	if (!joydev->exist)
		retval = -ENODEV;
	else if (!joydev->open++) {
		retval = input_open_device(&joydev->handle);
		if (retval)
			joydev->open--;
		else
			joydev_refresh_state(joydev);
	}

	mutex_unlock(&joydev->mutex);
	return retval;
}

static void joydev_close_device(struct joydev *joydev)
{
	mutex_lock(&joydev->mutex);

	if (joydev->exist && !--joydev->open)
		input_close_device(&joydev->handle);

	mutex_unlock(&joydev->mutex);
}

/*
 * Wake up users waiting for IO so they can disconnect from
 * dead device.
 */
static void joydev_hangup(struct joydev *joydev)
{
	struct joydev_client *client;

	spin_lock(&joydev->client_lock);
	list_for_each_entry(client, &joydev->client_list, node)
		kill_fasync(&client->fasync, SIGIO, POLL_HUP);
	spin_unlock(&joydev->client_lock);

	wake_up_interruptible(&joydev->wait);
}

static int joydev_release(struct inode *inode, struct file *file)
{
	struct joydev_client *client = file->private_data;
	struct joydev *joydev = client->joydev;

	joydev_detach_client(joydev, client);
	kfree(client);

	joydev_close_device(joydev);

	return 0;
}

static int joydev_open(struct inode *inode, struct file *file)
{
	struct joydev *joydev =
			container_of(inode->i_cdev, struct joydev, cdev);
	struct joydev_client *client;
	int error;

	client = kzalloc(sizeof(struct joydev_client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	spin_lock_init(&client->buffer_lock);
	client->joydev = joydev;
	joydev_attach_client(joydev, client);

	error = joydev_open_device(joydev);
	if (error)
		goto err_free_client;

	file->private_data = client;
	stream_open(inode, file);

	return 0;

 err_free_client:
	joydev_detach_client(joydev, client);
	kfree(client);
	return error;
}

static int joydev_generate_startup_event(struct joydev_client *client,
					 struct input_dev *input,
					 struct js_event *event)
{
	struct joydev *joydev = client->joydev;
	int have_event;

	spin_lock_irq(&client->buffer_lock);

	have_event = client->startup < joydev->nabs + joydev->nkey;

	if (have_event) {

		event->time = jiffies_to_msecs(jiffies);
		if (client->startup < joydev->nkey) {
			event->type = JS_EVENT_BUTTON | JS_EVENT_INIT;
			event->number = client->startup;
			event->value = !!test_bit(joydev->keypam[event->number],
						  input->key);
		} else {
			event->type = JS_EVENT_AXIS | JS_EVENT_INIT;
			event->number = client->startup - joydev->nkey;
			event->value = joydev->abs[event->number];
		}
		client->startup++;
	}

	spin_unlock_irq(&client->buffer_lock);

	return have_event;
}

static int joydev_fetch_next_event(struct joydev_client *client,
				   struct js_event *event)
{
	int have_event;

	spin_lock_irq(&client->buffer_lock);

	have_event = client->head != client->tail;
	if (have_event) {
		*event = client->buffer[client->tail++];
		client->tail &= JOYDEV_BUFFER_SIZE - 1;
	}

	spin_unlock_irq(&client->buffer_lock);

	return have_event;
}

/*
 * Old joystick interface
 */
static ssize_t joydev_0x_read(struct joydev_client *client,
			      struct input_dev *input,
			      char __user *buf)
{
	struct joydev *joydev = client->joydev;
	struct JS_DATA_TYPE data;
	int i;

	spin_lock_irq(&input->event_lock);

	/*
	 * Get device state
	 */
	for (data.buttons = i = 0; i < 32 && i < joydev->nkey; i++)
		data.buttons |=
			test_bit(joydev->keypam[i], input->key) ? (1 << i) : 0;
	data.x = (joydev->abs[0] / 256 + 128) >> joydev->glue.JS_CORR.x;
	data.y = (joydev->abs[1] / 256 + 128) >> joydev->glue.JS_CORR.y;

	/*
	 * Reset reader's event queue
	 */
	spin_lock(&client->buffer_lock);
	client->startup = 0;
	client->tail = client->head;
	spin_unlock(&client->buffer_lock);

	spin_unlock_irq(&input->event_lock);

	if (copy_to_user(buf, &data, sizeof(struct JS_DATA_TYPE)))
		return -EFAULT;

	return sizeof(struct JS_DATA_TYPE);
}

static inline int joydev_data_pending(struct joydev_client *client)
{
	struct joydev *joydev = client->joydev;

	return client->startup < joydev->nabs + joydev->nkey ||
		client->head != client->tail;
}

static ssize_t joydev_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct joydev_client *client = file->private_data;
	struct joydev *joydev = client->joydev;
	struct input_dev *input = joydev->handle.dev;
	struct js_event event;
	int retval;

	if (!joydev->exist)
		return -ENODEV;

	if (count < sizeof(struct js_event))
		return -EINVAL;

	if (count == sizeof(struct JS_DATA_TYPE))
		return joydev_0x_read(client, input, buf);

	if (!joydev_data_pending(client) && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	retval = wait_event_interruptible(joydev->wait,
			!joydev->exist || joydev_data_pending(client));
	if (retval)
		return retval;

	if (!joydev->exist)
		return -ENODEV;

	while (retval + sizeof(struct js_event) <= count &&
	       joydev_generate_startup_event(client, input, &event)) {

		if (copy_to_user(buf + retval, &event, sizeof(struct js_event)))
			return -EFAULT;

		retval += sizeof(struct js_event);
	}

	while (retval + sizeof(struct js_event) <= count &&
	       joydev_fetch_next_event(client, &event)) {

		if (copy_to_user(buf + retval, &event, sizeof(struct js_event)))
			return -EFAULT;

		retval += sizeof(struct js_event);
	}

	return retval;
}

/* No kernel lock - fine */
static __poll_t joydev_poll(struct file *file, poll_table *wait)
{
	struct joydev_client *client = file->private_data;
	struct joydev *joydev = client->joydev;

	poll_wait(file, &joydev->wait, wait);
	return (joydev_data_pending(client) ? (EPOLLIN | EPOLLRDNORM) : 0) |
		(joydev->exist ?  0 : (EPOLLHUP | EPOLLERR));
}

static int joydev_handle_JSIOCSAXMAP(struct joydev *joydev,
				     void __user *argp, size_t len)
{
	__u8 *abspam;
	int i;
	int retval = 0;

	len = min(len, sizeof(joydev->abspam));

	/* Validate the map. */
	abspam = memdup_user(argp, len);
	if (IS_ERR(abspam))
		return PTR_ERR(abspam);

	for (i = 0; i < len && i < joydev->nabs; i++) {
		if (abspam[i] > ABS_MAX) {
			retval = -EINVAL;
			goto out;
		}
	}

	memcpy(joydev->abspam, abspam, len);

	for (i = 0; i < joydev->nabs; i++)
		joydev->absmap[joydev->abspam[i]] = i;

 out:
	kfree(abspam);
	return retval;
}

static int joydev_handle_JSIOCSBTNMAP(struct joydev *joydev,
				      void __user *argp, size_t len)
{
	__u16 *keypam;
	int i;
	int retval = 0;

	if (len % sizeof(*keypam))
		return -EINVAL;

	len = min(len, sizeof(joydev->keypam));

	/* Validate the map. */
	keypam = memdup_user(argp, len);
	if (IS_ERR(keypam))
		return PTR_ERR(keypam);

	for (i = 0; i < (len / 2) && i < joydev->nkey; i++) {
		if (keypam[i] > KEY_MAX || keypam[i] < BTN_MISC) {
			retval = -EINVAL;
			goto out;
		}
	}

	memcpy(joydev->keypam, keypam, len);

	for (i = 0; i < joydev->nkey; i++)
		joydev->keymap[joydev->keypam[i] - BTN_MISC] = i;

 out:
	kfree(keypam);
	return retval;
}


static int joydev_ioctl_common(struct joydev *joydev,
				unsigned int cmd, void __user *argp)
{
	struct input_dev *dev = joydev->handle.dev;
	size_t len;
	int i;
	const char *name;

	/* Process fixed-sized commands. */
	switch (cmd) {

	case JS_SET_CAL:
		return copy_from_user(&joydev->glue.JS_CORR, argp,
				sizeof(joydev->glue.JS_CORR)) ? -EFAULT : 0;

	case JS_GET_CAL:
		return copy_to_user(argp, &joydev->glue.JS_CORR,
				sizeof(joydev->glue.JS_CORR)) ? -EFAULT : 0;

	case JS_SET_TIMEOUT:
		return get_user(joydev->glue.JS_TIMEOUT, (s32 __user *) argp);

	case JS_GET_TIMEOUT:
		return put_user(joydev->glue.JS_TIMEOUT, (s32 __user *) argp);

	case JSIOCGVERSION:
		return put_user(JS_VERSION, (__u32 __user *) argp);

	case JSIOCGAXES:
		return put_user(joydev->nabs, (__u8 __user *) argp);

	case JSIOCGBUTTONS:
		return put_user(joydev->nkey, (__u8 __user *) argp);

	case JSIOCSCORR:
		if (copy_from_user(joydev->corr, argp,
			      sizeof(joydev->corr[0]) * joydev->nabs))
			return -EFAULT;

		for (i = 0; i < joydev->nabs; i++) {
			int val = input_abs_get_val(dev, joydev->abspam[i]);
			joydev->abs[i] = joydev_correct(val, &joydev->corr[i]);
		}
		return 0;

	case JSIOCGCORR:
		return copy_to_user(argp, joydev->corr,
			sizeof(joydev->corr[0]) * joydev->nabs) ? -EFAULT : 0;

	}

	/*
	 * Process variable-sized commands (the axis and button map commands
	 * are considered variable-sized to decouple them from the values of
	 * ABS_MAX and KEY_MAX).
	 */
	switch (cmd & ~IOCSIZE_MASK) {

	case (JSIOCSAXMAP & ~IOCSIZE_MASK):
		return joydev_handle_JSIOCSAXMAP(joydev, argp, _IOC_SIZE(cmd));

	case (JSIOCGAXMAP & ~IOCSIZE_MASK):
		len = min_t(size_t, _IOC_SIZE(cmd), sizeof(joydev->abspam));
		return copy_to_user(argp, joydev->abspam, len) ? -EFAULT : len;

	case (JSIOCSBTNMAP & ~IOCSIZE_MASK):
		return joydev_handle_JSIOCSBTNMAP(joydev, argp, _IOC_SIZE(cmd));

	case (JSIOCGBTNMAP & ~IOCSIZE_MASK):
		len = min_t(size_t, _IOC_SIZE(cmd), sizeof(joydev->keypam));
		return copy_to_user(argp, joydev->keypam, len) ? -EFAULT : len;

	case JSIOCGNAME(0):
		name = dev->name;
		if (!name)
			return 0;

		len = min_t(size_t, _IOC_SIZE(cmd), strlen(name) + 1);
		return copy_to_user(argp, name, len) ? -EFAULT : len;
	}

	return -EINVAL;
}

#ifdef CONFIG_COMPAT
static long joydev_compat_ioctl(struct file *file,
				unsigned int cmd, unsigned long arg)
{
	struct joydev_client *client = file->private_data;
	struct joydev *joydev = client->joydev;
	void __user *argp = (void __user *)arg;
	s32 tmp32;
	struct JS_DATA_SAVE_TYPE_32 ds32;
	int retval;

	retval = mutex_lock_interruptible(&joydev->mutex);
	if (retval)
		return retval;

	if (!joydev->exist) {
		retval = -ENODEV;
		goto out;
	}

	switch (cmd) {

	case JS_SET_TIMELIMIT:
		retval = get_user(tmp32, (s32 __user *) arg);
		if (retval == 0)
			joydev->glue.JS_TIMELIMIT = tmp32;
		break;

	case JS_GET_TIMELIMIT:
		tmp32 = joydev->glue.JS_TIMELIMIT;
		retval = put_user(tmp32, (s32 __user *) arg);
		break;

	case JS_SET_ALL:
		retval = copy_from_user(&ds32, argp,
					sizeof(ds32)) ? -EFAULT : 0;
		if (retval == 0) {
			joydev->glue.JS_TIMEOUT    = ds32.JS_TIMEOUT;
			joydev->glue.BUSY          = ds32.BUSY;
			joydev->glue.JS_EXPIRETIME = ds32.JS_EXPIRETIME;
			joydev->glue.JS_TIMELIMIT  = ds32.JS_TIMELIMIT;
			joydev->glue.JS_SAVE       = ds32.JS_SAVE;
			joydev->glue.JS_CORR       = ds32.JS_CORR;
		}
		break;

	case JS_GET_ALL:
		ds32.JS_TIMEOUT    = joydev->glue.JS_TIMEOUT;
		ds32.BUSY          = joydev->glue.BUSY;
		ds32.JS_EXPIRETIME = joydev->glue.JS_EXPIRETIME;
		ds32.JS_TIMELIMIT  = joydev->glue.JS_TIMELIMIT;
		ds32.JS_SAVE       = joydev->glue.JS_SAVE;
		ds32.JS_CORR       = joydev->glue.JS_CORR;

		retval = copy_to_user(argp, &ds32, sizeof(ds32)) ? -EFAULT : 0;
		break;

	default:
		retval = joydev_ioctl_common(joydev, cmd, argp);
		break;
	}

 out:
	mutex_unlock(&joydev->mutex);
	return retval;
}
#endif /* CONFIG_COMPAT */

static long joydev_ioctl(struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	struct joydev_client *client = file->private_data;
	struct joydev *joydev = client->joydev;
	void __user *argp = (void __user *)arg;
	int retval;

	retval = mutex_lock_interruptible(&joydev->mutex);
	if (retval)
		return retval;

	if (!joydev->exist) {
		retval = -ENODEV;
		goto out;
	}

	switch (cmd) {

	case JS_SET_TIMELIMIT:
		retval = get_user(joydev->glue.JS_TIMELIMIT,
				  (long __user *) arg);
		break;

	case JS_GET_TIMELIMIT:
		retval = put_user(joydev->glue.JS_TIMELIMIT,
				  (long __user *) arg);
		break;

	case JS_SET_ALL:
		retval = copy_from_user(&joydev->glue, argp,
					sizeof(joydev->glue)) ? -EFAULT : 0;
		break;

	case JS_GET_ALL:
		retval = copy_to_user(argp, &joydev->glue,
				      sizeof(joydev->glue)) ? -EFAULT : 0;
		break;

	default:
		retval = joydev_ioctl_common(joydev, cmd, argp);
		break;
	}
 out:
	mutex_unlock(&joydev->mutex);
	return retval;
}

static const struct file_operations joydev_fops = {
	.owner		= THIS_MODULE,
	.read		= joydev_read,
	.poll		= joydev_poll,
	.open		= joydev_open,
	.release	= joydev_release,
	.unlocked_ioctl	= joydev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= joydev_compat_ioctl,
#endif
	.fasync		= joydev_fasync,
	.llseek		= no_llseek,
};

/*
 * Mark device non-existent. This disables writes, ioctls and
 * prevents new users from opening the device. Already posted
 * blocking reads will stay, however new ones will fail.
 */
static void joydev_mark_dead(struct joydev *joydev)
{
	mutex_lock(&joydev->mutex);
	joydev->exist = false;
	mutex_unlock(&joydev->mutex);
}

static void joydev_cleanup(struct joydev *joydev)
{
	struct input_handle *handle = &joydev->handle;

	joydev_mark_dead(joydev);
	joydev_hangup(joydev);

	/* joydev is marked dead so no one else accesses joydev->open */
	if (joydev->open)
		input_close_device(handle);
}

/*
 * These codes are copied from from hid-ids.h, unfortunately there is no common
 * usb_ids/bt_ids.h header.
 */
#define USB_VENDOR_ID_SONY			0x054c
#define USB_DEVICE_ID_SONY_PS3_CONTROLLER		0x0268
#define USB_DEVICE_ID_SONY_PS4_CONTROLLER		0x05c4
#define USB_DEVICE_ID_SONY_PS4_CONTROLLER_2		0x09cc
#define USB_DEVICE_ID_SONY_PS4_CONTROLLER_DONGLE	0x0ba0

#define USB_VENDOR_ID_THQ			0x20d6
#define USB_DEVICE_ID_THQ_PS3_UDRAW			0xcb17

#define USB_VENDOR_ID_NINTENDO		0x057e
#define USB_DEVICE_ID_NINTENDO_JOYCONL	0x2006
#define USB_DEVICE_ID_NINTENDO_JOYCONR	0x2007
#define USB_DEVICE_ID_NINTENDO_PROCON	0x2009
#define USB_DEVICE_ID_NINTENDO_CHRGGRIP	0x200E

#define ACCEL_DEV(vnd, prd)						\
	{								\
		.flags = INPUT_DEVICE_ID_MATCH_VENDOR |			\
				INPUT_DEVICE_ID_MATCH_PRODUCT |		\
				INPUT_DEVICE_ID_MATCH_PROPBIT,		\
		.vendor = (vnd),					\
		.product = (prd),					\
		.propbit = { BIT_MASK(INPUT_PROP_ACCELEROMETER) },	\
	}

static const struct input_device_id joydev_blacklist[] = {
	/* Avoid touchpads and touchscreens */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
				INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
	},
	/* Avoid tablets, digitisers and similar devices */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
				INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(BTN_DIGI)] = BIT_MASK(BTN_DIGI) },
	},
	/* Disable accelerometers on composite devices */
	ACCEL_DEV(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS3_CONTROLLER),
	ACCEL_DEV(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS4_CONTROLLER),
	ACCEL_DEV(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS4_CONTROLLER_2),
	ACCEL_DEV(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS4_CONTROLLER_DONGLE),
	ACCEL_DEV(USB_VENDOR_ID_THQ, USB_DEVICE_ID_THQ_PS3_UDRAW),
	ACCEL_DEV(USB_VENDOR_ID_NINTENDO, USB_DEVICE_ID_NINTENDO_PROCON),
	ACCEL_DEV(USB_VENDOR_ID_NINTENDO, USB_DEVICE_ID_NINTENDO_CHRGGRIP),
	ACCEL_DEV(USB_VENDOR_ID_NINTENDO, USB_DEVICE_ID_NINTENDO_JOYCONL),
	ACCEL_DEV(USB_VENDOR_ID_NINTENDO, USB_DEVICE_ID_NINTENDO_JOYCONR),
	{ /* sentinel */ }
};

static bool joydev_dev_is_blacklisted(struct input_dev *dev)
{
	const struct input_device_id *id;

	for (id = joydev_blacklist; id->flags; id++) {
		if (input_match_device_id(dev, id)) {
			dev_dbg(&dev->dev,
				"joydev: blacklisting '%s'\n", dev->name);
			return true;
		}
	}

	return false;
}

static bool joydev_dev_is_absolute_mouse(struct input_dev *dev)
{
	DECLARE_BITMAP(jd_scratch, KEY_CNT);
	bool ev_match = false;

	BUILD_BUG_ON(ABS_CNT > KEY_CNT || EV_CNT > KEY_CNT);

	/*
	 * Virtualization (VMware, etc) and remote management (HP
	 * ILO2) solutions use absolute coordinates for their virtual
	 * pointing devices so that there is one-to-one relationship
	 * between pointer position on the host screen and virtual
	 * guest screen, and so their mice use ABS_X, ABS_Y and 3
	 * primary button events. This clashes with what joydev
	 * considers to be joysticks (a device with at minimum ABS_X
	 * axis).
	 *
	 * Here we are trying to separate absolute mice from
	 * joysticks. A device is, for joystick detection purposes,
	 * considered to be an absolute mouse if the following is
	 * true:
	 *
	 * 1) Event types are exactly
	 *      EV_ABS, EV_KEY and EV_SYN
	 *    or
	 *      EV_ABS, EV_KEY, EV_SYN and EV_MSC
	 *    or
	 *      EV_ABS, EV_KEY, EV_SYN, EV_MSC and EV_REL.
	 * 2) Absolute events are exactly ABS_X and ABS_Y.
	 * 3) Keys are exactly BTN_LEFT, BTN_RIGHT and BTN_MIDDLE.
	 * 4) Device is not on "Amiga" bus.
	 */

	bitmap_zero(jd_scratch, EV_CNT);
	/* VMware VMMouse, HP ILO2 */
	__set_bit(EV_ABS, jd_scratch);
	__set_bit(EV_KEY, jd_scratch);
	__set_bit(EV_SYN, jd_scratch);
	if (bitmap_equal(jd_scratch, dev->evbit, EV_CNT))
		ev_match = true;

	/* HP ILO2, AMI BMC firmware */
	__set_bit(EV_MSC, jd_scratch);
	if (bitmap_equal(jd_scratch, dev->evbit, EV_CNT))
		ev_match = true;

	/* VMware Virtual USB Mouse, QEMU USB Tablet, ATEN BMC firmware */
	__set_bit(EV_REL, jd_scratch);
	if (bitmap_equal(jd_scratch, dev->evbit, EV_CNT))
		ev_match = true;

	if (!ev_match)
		return false;

	bitmap_zero(jd_scratch, ABS_CNT);
	__set_bit(ABS_X, jd_scratch);
	__set_bit(ABS_Y, jd_scratch);
	if (!bitmap_equal(dev->absbit, jd_scratch, ABS_CNT))
		return false;

	bitmap_zero(jd_scratch, KEY_CNT);
	__set_bit(BTN_LEFT, jd_scratch);
	__set_bit(BTN_RIGHT, jd_scratch);
	__set_bit(BTN_MIDDLE, jd_scratch);

	if (!bitmap_equal(dev->keybit, jd_scratch, KEY_CNT))
		return false;

	/*
	 * Amiga joystick (amijoy) historically uses left/middle/right
	 * button events.
	 */
	if (dev->id.bustype == BUS_AMIGA)
		return false;

	return true;
}

static bool joydev_match(struct input_handler *handler, struct input_dev *dev)
{
	/* Disable blacklisted devices */
	if (joydev_dev_is_blacklisted(dev))
		return false;

	/* Avoid absolute mice */
	if (joydev_dev_is_absolute_mouse(dev))
		return false;

	return true;
}

static int joydev_connect(struct input_handler *handler, struct input_dev *dev,
			  const struct input_device_id *id)
{
	struct joydev *joydev;
	int i, j, t, minor, dev_no;
	int error;

	minor = input_get_new_minor(JOYDEV_MINOR_BASE, JOYDEV_MINORS, true);
	if (minor < 0) {
		error = minor;
		pr_err("failed to reserve new minor: %d\n", error);
		return error;
	}

	joydev = kzalloc(sizeof(struct joydev), GFP_KERNEL);
	if (!joydev) {
		error = -ENOMEM;
		goto err_free_minor;
	}

	INIT_LIST_HEAD(&joydev->client_list);
	spin_lock_init(&joydev->client_lock);
	mutex_init(&joydev->mutex);
	init_waitqueue_head(&joydev->wait);
	joydev->exist = true;

	dev_no = minor;
	/* Normalize device number if it falls into legacy range */
	if (dev_no < JOYDEV_MINOR_BASE + JOYDEV_MINORS)
		dev_no -= JOYDEV_MINOR_BASE;
	dev_set_name(&joydev->dev, "js%d", dev_no);

	joydev->handle.dev = input_get_device(dev);
	joydev->handle.name = dev_name(&joydev->dev);
	joydev->handle.handler = handler;
	joydev->handle.private = joydev;

	for_each_set_bit(i, dev->absbit, ABS_CNT) {
		joydev->absmap[i] = joydev->nabs;
		joydev->abspam[joydev->nabs] = i;
		joydev->nabs++;
	}

	for (i = BTN_JOYSTICK - BTN_MISC; i < KEY_MAX - BTN_MISC + 1; i++)
		if (test_bit(i + BTN_MISC, dev->keybit)) {
			joydev->keymap[i] = joydev->nkey;
			joydev->keypam[joydev->nkey] = i + BTN_MISC;
			joydev->nkey++;
		}

	for (i = 0; i < BTN_JOYSTICK - BTN_MISC; i++)
		if (test_bit(i + BTN_MISC, dev->keybit)) {
			joydev->keymap[i] = joydev->nkey;
			joydev->keypam[joydev->nkey] = i + BTN_MISC;
			joydev->nkey++;
		}

	for (i = 0; i < joydev->nabs; i++) {
		j = joydev->abspam[i];
		if (input_abs_get_max(dev, j) == input_abs_get_min(dev, j)) {
			joydev->corr[i].type = JS_CORR_NONE;
			continue;
		}
		joydev->corr[i].type = JS_CORR_BROKEN;
		joydev->corr[i].prec = input_abs_get_fuzz(dev, j);

		t = (input_abs_get_max(dev, j) + input_abs_get_min(dev, j)) / 2;
		joydev->corr[i].coef[0] = t - input_abs_get_flat(dev, j);
		joydev->corr[i].coef[1] = t + input_abs_get_flat(dev, j);

		t = (input_abs_get_max(dev, j) - input_abs_get_min(dev, j)) / 2
			- 2 * input_abs_get_flat(dev, j);
		if (t) {
			joydev->corr[i].coef[2] = (1 << 29) / t;
			joydev->corr[i].coef[3] = (1 << 29) / t;
		}
	}

	joydev->dev.devt = MKDEV(INPUT_MAJOR, minor);
	joydev->dev.class = &input_class;
	joydev->dev.parent = &dev->dev;
	joydev->dev.release = joydev_free;
	device_initialize(&joydev->dev);

	error = input_register_handle(&joydev->handle);
	if (error)
		goto err_free_joydev;

	cdev_init(&joydev->cdev, &joydev_fops);

	error = cdev_device_add(&joydev->cdev, &joydev->dev);
	if (error)
		goto err_cleanup_joydev;

	return 0;

 err_cleanup_joydev:
	joydev_cleanup(joydev);
	input_unregister_handle(&joydev->handle);
 err_free_joydev:
	put_device(&joydev->dev);
 err_free_minor:
	input_free_minor(minor);
	return error;
}

static void joydev_disconnect(struct input_handle *handle)
{
	struct joydev *joydev = handle->private;

	cdev_device_del(&joydev->cdev, &joydev->dev);
	joydev_cleanup(joydev);
	input_free_minor(MINOR(joydev->dev.devt));
	input_unregister_handle(handle);
	put_device(&joydev->dev);
}

static const struct input_device_id joydev_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
				INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { BIT_MASK(ABS_X) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
				INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { BIT_MASK(ABS_Z) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
				INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { BIT_MASK(ABS_WHEEL) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
				INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { BIT_MASK(ABS_THROTTLE) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
				INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT_MASK(EV_KEY) },
		.keybit = {[BIT_WORD(BTN_JOYSTICK)] = BIT_MASK(BTN_JOYSTICK) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
				INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(BTN_GAMEPAD)] = BIT_MASK(BTN_GAMEPAD) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
				INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(BTN_TRIGGER_HAPPY)] = BIT_MASK(BTN_TRIGGER_HAPPY) },
	},
	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(input, joydev_ids);

static struct input_handler joydev_handler = {
	.event		= joydev_event,
	.match		= joydev_match,
	.connect	= joydev_connect,
	.disconnect	= joydev_disconnect,
	.legacy_minors	= true,
	.minor		= JOYDEV_MINOR_BASE,
	.name		= "joydev",
	.id_table	= joydev_ids,
};

static int __init joydev_init(void)
{
	return input_register_handler(&joydev_handler);
}

static void __exit joydev_exit(void)
{
	input_unregister_handler(&joydev_handler);
}

module_init(joydev_init);
module_exit(joydev_exit);
