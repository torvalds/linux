/*
 * Rockchip VR driver for Linux
 *
 * Copyright (C) ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*
 * Driver for Rockchip VR devices. Based on hidraw driver.
 */

#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include "hid-rkvr.h"
#include "hid-ids.h"

#define USB_TRACKER_INTERFACE_PROTOCOL	0
/* define rkvr interface number */
#define RKVR_INTERFACE_USB_AUDIO_ID 1
#define RKVR_INTERFACE_USB_SENSOR_ID 2
#define RKVR_INTERFACE_USB_AUDIO_KEY_ID 1
/* number of reports to buffer */
#define RKVR_HIDRAW_BUFFER_SIZE 64
#define RKVR_HIDRAW_MAX_DEVICES 8
#define RKVR_FIRST_MINOR 0
#define RK_HID_GEAR_TOUCH

static struct class *rkvr_class;

static struct hidraw *rkvr_hidraw_table[RKVR_HIDRAW_MAX_DEVICES];

static struct hid_capability
{
	__u8 suspend_notify;
} rkvr_hid_capability[RKVR_HIDRAW_MAX_DEVICES];
static DEFINE_MUTEX(minors_lock);

struct keymap_t {
	__u16 key_menu_up:1;
	__u16 key_menu_down:1;
	__u16 key_home_up:1;
	__u16 key_home_down:1;
	__u16 key_power_up:1;
	__u16 key_power_down:1;
	__u16 key_volup_up:1;
	__u16 key_volup_down:1;
	__u16 key_voldn_up:1;
	__u16 key_voldn_down:1;
	__u16 key_esc_up:1;
	__u16 key_esc_down:1;
	/*for touch panel **/
	__u16 key_up_pressed:1;
	__u16 key_up_released:1;
	__u16 key_down_pressed:1;
	__u16 key_down_released:1;
	__u16 key_left_pressed:1;
	__u16 key_left_released:1;
	__u16 key_right_pressed:1;
	__u16 key_right_released:1;
	__u16 key_enter_pressed:1;
	__u16 key_enter_released:1;
	__u16 key_pressed:1;
	__u16 psensor_on:1;
	__u16 psensor_off:1;
} __packed;

union rkvr_data_t {
	struct rkvr_data {
		__u8 buf_head[6];
		__u8 buf_sensortemperature[2];
		__u8 buf_sensor[40];
		__u8 buf_reserve[10];
		struct keymap_t key_map;
	} rkvr_data;
	__u8 buf[62];
} __packed;

static int rkvr_major;
static struct cdev rkvr_cdev;
static unsigned int count_array[15] = {0,};
static unsigned long old_jiffy_array[15] = {0,};
static int rkvr_index;
static int opens;

struct sensor_hid_data {
	void *priv;
	int (*send_event)(char *raw_data, size_t raw_len, void *priv);
} sensorData;

static DEFINE_MUTEX(device_list_lock);
static struct list_head rkvr_hid_hw_device_list = {
	.next = &rkvr_hid_hw_device_list,
	.prev = &rkvr_hid_hw_device_list
};

static struct rkvr_iio_hw_device *inv_hid_alloc(const char *name)
{
	struct rkvr_iio_hw_device *p;
	const char *s;

	if (!name)
		return 0;
	s = kstrdup_const(name, GFP_KERNEL);
	if (!s)
		goto error;
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		goto error;
	p->name = s;
	return p;
error:
	pr_err("%s error!\n", __func__);
	if (s)
		kfree_const(s);
	return 0;
}

static void inv_hid_free(struct rkvr_iio_hw_device *hw_device)
{
	kfree_const(hw_device->name);
	kfree(hw_device);
}

static int inv_hid_register_devcie(struct rkvr_iio_hw_device *hw_device)
{

	mutex_lock(&device_list_lock);
	if (hw_device->name && (!list_empty(&rkvr_hid_hw_device_list))) {
		struct rkvr_iio_hw_device *p;

		list_for_each_entry(p, &rkvr_hid_hw_device_list, l) {
			if (!strcmp(hw_device->name, p->name)) {
				pr_err("%s already exist ,abort\n", hw_device->name);
				mutex_unlock(&device_list_lock);
				return -1;
			}
		}
	}
	list_add_tail(&hw_device->l, &rkvr_hid_hw_device_list);
	mutex_unlock(&device_list_lock);
	return 0;
}

static void inv_hid_unregister_and_destroy_devcie_by_name(const char *name)
{
	struct rkvr_iio_hw_device *p = NULL;

	mutex_lock(&device_list_lock);
	list_for_each_entry(p, &rkvr_hid_hw_device_list, l) {
		if (!strcmp(name, p->name)) {
			list_del(&p->l);
			break;
		}
	}
	if (p) {
		pr_info("find dev with name %s,free now\n", name);
		inv_hid_free(p);
	}
	mutex_unlock(&device_list_lock);
}

static ssize_t rkvr_hidraw_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct hidraw_list *list = file->private_data;
	int ret = 0, len;
	DECLARE_WAITQUEUE(wait, current);

	mutex_lock(&list->read_mutex);
	while (ret == 0) {
		if (list->head == list->tail) {
			add_wait_queue(&list->hidraw->wait, &wait);
			set_current_state(TASK_INTERRUPTIBLE);

			while (list->head == list->tail) {
				if (signal_pending(current)) {
					ret = -ERESTARTSYS;
					break;
				}
				if (!list->hidraw->exist) {
					ret = -EIO;
					break;
				}
				if (file->f_flags & O_NONBLOCK) {
					ret = -EAGAIN;
					break;
				}

				/* allow O_NONBLOCK to work well from other threads */
				mutex_unlock(&list->read_mutex);
				schedule();
				mutex_lock(&list->read_mutex);
				set_current_state(TASK_INTERRUPTIBLE);
			}

			set_current_state(TASK_RUNNING);
			remove_wait_queue(&list->hidraw->wait, &wait);
		}

		if (ret)
			goto out;

		len = list->buffer[list->tail].len > count ?
			count : list->buffer[list->tail].len;

		if (list->buffer[list->tail].value) {
			if (copy_to_user(buffer, list->buffer[list->tail].value, len)) {
				ret = -EFAULT;
				goto out;
			}
			ret = len;
			if (opens > 0 && rkvr_index < 15) {
				if (++count_array[rkvr_index] >= 1000) {
					unsigned long cur_jiffy = jiffies;

					hid_dbg(list->hidraw->hid, "rkvr: %d Hz, read(%d) (%d:%s)\n", (int)(1000 * HZ / (cur_jiffy - old_jiffy_array[rkvr_index])), rkvr_index, current->pid, current->comm);
					count_array[rkvr_index] = 0;
					old_jiffy_array[rkvr_index] = cur_jiffy;
				}
				if (++rkvr_index >= opens)
					rkvr_index = 0;
			} else {
				rkvr_index = 0;
			}
		}

		kfree(list->buffer[list->tail].value);
		list->buffer[list->tail].value = NULL;
		list->tail = (list->tail + 1) & (RKVR_HIDRAW_BUFFER_SIZE - 1);
	}
out:
	mutex_unlock(&list->read_mutex);
	return ret;
}

/* The first byte is expected to be a report number.
 * This function is to be called with the minors_lock mutex held
 */
static ssize_t rkvr_hidraw_send_report(struct file *file, const char __user *buffer, size_t count, unsigned char report_type)
{
	unsigned int minor = iminor(file_inode(file));
	struct hid_device *dev;
	__u8 *buf;
	int ret = 0;

	if (!rkvr_hidraw_table[minor] || !rkvr_hidraw_table[minor]->exist) {
		ret = -ENODEV;
		goto out;
	}

	dev = rkvr_hidraw_table[minor]->hid;

	if (count > HID_MAX_BUFFER_SIZE) {
		hid_warn(dev, "rkvr - pid %d passed too large report\n",
			 task_pid_nr(current));
		ret = -EINVAL;
		goto out;
	}

	if (count < 2) {
		hid_warn(dev, "rkvr - pid %d passed too short report\n",
			task_pid_nr(current));
		ret = -EINVAL;
		goto out;
	}

	buf = kmalloc(count * sizeof(__u8), GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}

	if (copy_from_user(buf, buffer, count)) {
		ret = -EFAULT;
		goto out_free;
	}

	if ((report_type == HID_OUTPUT_REPORT) &&
		!(dev->quirks & HID_QUIRK_NO_OUTPUT_REPORTS_ON_INTR_EP)) {
		ret = hid_hw_output_report(dev, buf, count);
		/*
		 * compatibility with old implementation of USB-HID and I2C-HID:
		 * if the device does not support receiving output reports,
		 * on an interrupt endpoint, fallback to SET_REPORT HID command.
		 */
		if (ret != -ENOSYS)
			goto out_free;
	}

	ret = hid_hw_raw_request(dev, buf[0], buf, count, report_type,
				HID_REQ_SET_REPORT);

out_free:
	kfree(buf);
out:
	return ret;
}

/* the first byte is expected to be a report number */
static ssize_t rkvr_hidraw_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	ssize_t ret;

	mutex_lock(&minors_lock);
	ret = rkvr_hidraw_send_report(file, buffer, count, HID_OUTPUT_REPORT);
	mutex_unlock(&minors_lock);
	return ret;
}

/* This function performs a Get_Report transfer over the control endpoint
 * per section 7.2.1 of the HID specification, version 1.1.  The first byte
 * of buffer is the report number to request, or 0x0 if the defice does not
 * use numbered reports. The report_type parameter can be HID_FEATURE_REPORT
 * or HID_INPUT_REPORT.  This function is to be called with the minors_lock
 *  mutex held.
 */
static ssize_t rkvr_hidraw_get_report(struct file *file, char __user *buffer, size_t count, unsigned char report_type)
{
	unsigned int minor = iminor(file_inode(file));
	struct hid_device *dev;
	__u8 *buf;
	int ret = 0, len;
	unsigned char report_number;

	dev = rkvr_hidraw_table[minor]->hid;

	if (!dev->ll_driver->raw_request) {
		ret = -ENODEV;
		goto out;
	}

	if (count > HID_MAX_BUFFER_SIZE) {
		hid_warn(dev, "rkvr - hidraw: pid %d passed too large report\n",
				task_pid_nr(current));
		ret = -EINVAL;
		goto out;
	}

	if (count < 2) {
		hid_warn(dev, "rkvr - hidraw: pid %d passed too short report\n",
				task_pid_nr(current));
		ret = -EINVAL;
		goto out;
	}

	buf = kmalloc(count * sizeof(__u8), GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	* Read the first byte from the user. This is the report number,
	* which is passed to hid_hw_raw_request().
	*/
	if (copy_from_user(&report_number, buffer, 1)) {
		ret = -EFAULT;
		goto out_free;
	}

	ret = hid_hw_raw_request(dev, report_number, buf, count, report_type,
				 HID_REQ_GET_REPORT);
	if (ret < 0)
		goto out_free;
	len = (ret < count) ? ret : count;

	if (copy_to_user(buffer, buf, len)) {
		ret = -EFAULT;
		goto out_free;
	}

	ret = len;

out_free:
	kfree(buf);
out:
	return ret;
}

static unsigned int rkvr_hidraw_poll(struct file *file, poll_table *wait)
{
	struct hidraw_list *list = file->private_data;

	poll_wait(file, &list->hidraw->wait, wait);
	if (list->head != list->tail)
		return POLLIN | POLLRDNORM;
	if (!list->hidraw->exist)
		return POLLERR | POLLHUP;

	return 0;
}

static int rkvr_hidraw_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	struct hidraw *dev;
	struct hidraw_list *list;
	unsigned long flags;
	int err = 0;

	list = kzalloc(sizeof(*list), GFP_KERNEL);
	if (!list) {
		err = -ENOMEM;
		goto out;
	}

	mutex_lock(&minors_lock);
	if (!rkvr_hidraw_table[minor] || !rkvr_hidraw_table[minor]->exist) {
		err = -ENODEV;
		goto out_unlock;
	}

	dev = rkvr_hidraw_table[minor];
	if (!dev->open++) {
		err = hid_hw_power(dev->hid, PM_HINT_FULLON);
		if (err < 0) {
			dev->open--;
			goto out_unlock;
		}

		err = hid_hw_open(dev->hid);

		if (err < 0) {
			hid_hw_power(dev->hid, PM_HINT_NORMAL);
			dev->open--;
			goto out_unlock;
		}
	}

	list->hidraw = rkvr_hidraw_table[minor];
	mutex_init(&list->read_mutex);
	spin_lock_irqsave(&rkvr_hidraw_table[minor]->list_lock, flags);
	list_add_tail(&list->node, &rkvr_hidraw_table[minor]->list);
	spin_unlock_irqrestore(&rkvr_hidraw_table[minor]->list_lock, flags);
	file->private_data = list;

	opens = dev->open;

out_unlock:
	mutex_unlock(&minors_lock);
out:
	if (err < 0)
		kfree(list);

	return err;
}

static int rkvr_hidraw_fasync(int fd, struct file *file, int on)
{
	struct hidraw_list *list = file->private_data;

	return fasync_helper(fd, file, on, &list->fasync);
}

static void rkvr_drop_ref(struct hidraw *hidraw, int exists_bit)
{
	if (exists_bit) { /*hw removed**/
		hidraw->exist = 0;
		if (hidraw->open) {
			hid_hw_close(hidraw->hid);
			wake_up_interruptible(&hidraw->wait);
		}
	} else {
		--hidraw->open;
	}

	if (!hidraw->open) {
		if (!hidraw->exist) { /*no opened && no hardware,delete all**/
			rkvr_hidraw_table[hidraw->minor] = NULL;
			kfree(hidraw);
		} else {
			/* close device for last reader */
			hid_hw_power(hidraw->hid, PM_HINT_NORMAL);
			hid_hw_close(hidraw->hid);
		}
	}
}

static int rkvr_hidraw_release(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	struct hidraw_list *list = file->private_data;
	unsigned long flags;

	mutex_lock(&minors_lock);

	spin_lock_irqsave(&rkvr_hidraw_table[minor]->list_lock, flags);
	list_del(&list->node);
	spin_unlock_irqrestore(&rkvr_hidraw_table[minor]->list_lock, flags);

	kfree(list);
	rkvr_drop_ref(rkvr_hidraw_table[minor], 0);

	mutex_unlock(&minors_lock);

	return 0;
}

static void rkvr_send_key_event(struct input_dev *input, int key_value, int state)
{
	if (!input) {
		return;
	}
	if (state) {
		input_report_key(input, key_value, 1);
		input_sync(input);
	} else {
		input_report_key(input, key_value, 0);
		input_sync(input);
	}
}

static int rkvr_keys_event(struct hid_device *hdev, void *data, unsigned long len)
{
	struct input_dev *input = hdev->hiddev;
	union rkvr_data_t *rkvr_data = (union rkvr_data_t *)data;

	if (rkvr_data->rkvr_data.key_map.key_menu_up)
		rkvr_send_key_event(input, KEY_MENU, 0);
	else if (rkvr_data->rkvr_data.key_map.key_menu_down)
		rkvr_send_key_event(input, KEY_MENU, 1);
	else if (rkvr_data->rkvr_data.key_map.key_home_up)
		rkvr_send_key_event(input, KEY_HOME, 0);
	else if (rkvr_data->rkvr_data.key_map.key_home_down)
		rkvr_send_key_event(input, KEY_HOME, 1);
	else if (rkvr_data->rkvr_data.key_map.key_power_up)
		rkvr_send_key_event(input, KEY_POWER, 0);
	else if (rkvr_data->rkvr_data.key_map.key_power_down)
		rkvr_send_key_event(input, KEY_POWER, 1);
	else if (rkvr_data->rkvr_data.key_map.key_volup_up)
		rkvr_send_key_event(input, KEY_VOLUMEUP, 0);
	else if (rkvr_data->rkvr_data.key_map.key_volup_down)
		rkvr_send_key_event(input, KEY_VOLUMEUP, 1);
	else if (rkvr_data->rkvr_data.key_map.key_voldn_up)
		rkvr_send_key_event(input, KEY_VOLUMEDOWN, 0);
	else if (rkvr_data->rkvr_data.key_map.key_voldn_down)
		rkvr_send_key_event(input, KEY_VOLUMEDOWN, 1);
	else if (rkvr_data->rkvr_data.key_map.key_esc_up)
		rkvr_send_key_event(input, KEY_ESC, 0);
	else if (rkvr_data->rkvr_data.key_map.key_esc_down)
		rkvr_send_key_event(input, KEY_ESC, 1);
	else if (rkvr_data->rkvr_data.key_map.key_up_pressed) {
		rkvr_send_key_event(input, KEY_UP, 1);
		rkvr_send_key_event(input, KEY_UP, 0);
	} else if (rkvr_data->rkvr_data.key_map.key_down_pressed) {
		rkvr_send_key_event(input, KEY_DOWN, 1);
		rkvr_send_key_event(input, KEY_DOWN, 0);
	} else if (rkvr_data->rkvr_data.key_map.key_left_pressed) {
		rkvr_send_key_event(input, KEY_LEFT, 1);
		rkvr_send_key_event(input, KEY_LEFT, 0);
	} else if (rkvr_data->rkvr_data.key_map.key_right_pressed) {
		rkvr_send_key_event(input, KEY_RIGHT, 1);
		rkvr_send_key_event(input, KEY_RIGHT, 0);
	} else if (rkvr_data->rkvr_data.key_map.key_enter_pressed) {
		input_event(input, EV_MSC, MSC_SCAN, 0x90001);
		rkvr_send_key_event(input, BTN_MOUSE, 1);
		input_event(input, EV_MSC, MSC_SCAN, 0x90001);
		rkvr_send_key_event(input, BTN_MOUSE, 0);
	}

	if (rkvr_data->rkvr_data.key_map.psensor_on) {
		hid_info(hdev, "event: psensor_on\n");
		rkvr_send_key_event(input, KEY_POWER, 1);
		rkvr_send_key_event(input, KEY_POWER, 0);
	} else if (rkvr_data->rkvr_data.key_map.psensor_off) {
		hid_info(hdev, "event: psensor_off\n");
		rkvr_send_key_event(input, KEY_POWER, 1);
		rkvr_send_key_event(input, KEY_POWER, 0);
	}

	return 0;
}

static int rkvr_report_event(struct hid_device *hid, u8 *data, int len)
{
	struct hidraw *dev = hid->hidraw;
	struct hidraw_list *list;
	int ret = 0;
	unsigned long flags;
	union rkvr_data_t *rkvr_data = (union rkvr_data_t *)data;
	struct sensor_hid_data *pdata = hid_get_drvdata(hid);

	spin_lock_irqsave(&dev->list_lock, flags);
	if (hid->hiddev) {
		rkvr_keys_event(hid, data, len);
	}
	if (pdata && pdata->priv && pdata->send_event) {
		pdata->send_event(rkvr_data->buf, len, pdata->priv);
		spin_unlock_irqrestore(&dev->list_lock, flags);
	} else {
		list_for_each_entry(list, &dev->list, node) {
			int new_head = (list->head + 1) & (RKVR_HIDRAW_BUFFER_SIZE - 1);

			if (new_head == list->tail)
				continue;

			list->buffer[list->head].value = kmemdup(data, len, GFP_ATOMIC);
			if (!list->buffer[list->head].value) {
				ret = -ENOMEM;
				break;
			}

			list->buffer[list->head].len = len;
			list->head = new_head;
			kill_fasync(&list->fasync, SIGIO, POLL_IN);
		}
		spin_unlock_irqrestore(&dev->list_lock, flags);
		wake_up_interruptible(&dev->wait);
	}
	return ret;
}

/******************************************
 *--------------------
 *| ID | BUF .....   |
 *--------------------
 *
 ******************************************/
static int rkvr_send_report(struct device *dev, unsigned char *data, size_t len)
{
	struct hid_device *hid = container_of(dev, struct hid_device, dev);
	unsigned char reportnum = HID_REPORT_ID_RKVR;
	unsigned char rtype = HID_OUTPUT_REPORT;
	int ret = -EINVAL;

	ret = hid_hw_raw_request(hid, reportnum, (unsigned char *)data, len, rtype, HID_REQ_SET_REPORT);
	if (ret != len) {
		hid_err(hid, "rkvr_send_report fail\n");
		ret = -EIO;
		goto fail;
	}
	hid_info(hid, "rkvr_send_report ok\n");
	ret = 0;
fail:
	return ret;
}

static int rkvr_recv_report(struct device *dev, u8 type, u8 *data, int len)
{
	struct hid_device *hid = container_of(dev, struct hid_device, dev);
	unsigned char report_number = type;
	unsigned char report_type = HID_MISC_REPORT;
	char buf[1 + sizeof(*data) * len];
	int readlen = 1 + sizeof(*data) * len;
	int ret;

	ret = hid_hw_raw_request(hid, report_number, (unsigned char *)buf, readlen, report_type, HID_REQ_GET_REPORT);
	if (ret != readlen) {
		hid_info(hid, "rkvr_recv_report fail\n");
		return -1;
	}
	memcpy(data, &buf[1], len);
	hid_info(hid, "rkvr_recv_report %02x\n", type);

	return 0;
}

/*
 * for enable sensor data
 ************************************
 * buf contents ---->
 * first 8 bytes :random digits
 * left bytes    :encryt data
 * eg:32654:3AA4618F6B455D37F06279EC2D6BC478C759443277F3E4E982203562E7ED
 ***********************************
 */

static int hid_report_sync(struct device *dev, const char *data, size_t count)
{
	struct hid_device *hid = container_of(dev, struct hid_device, dev);
	u64 *tmp;
	unsigned char buf[64] = {HID_REPORT_ID_RKVR, RKVR_ID_SYNC};
	unsigned char buf2[3] = {0};
	char *colon;
	int i, ret = 0;
	char *p;
	size_t len;

	p = kmalloc(sizeof(*p) * count, GFP_KERNEL);
	if (!p) {
		hid_err(hid, "no mem\n");
		return -ENOMEM;
	}
	memcpy(p, data, count);
	colon = strnchr(p, count, ':');
	if (!colon) {
		hid_err(hid, "must have conlon\n");
		ret = -EINVAL;
		goto fail;
	}
	if (colon - p + 1 >= count) {
		hid_err(hid, "must have sync string after conlon\n");
		ret = -EINVAL;
		goto fail;
	}
	colon[0] = 0;
	colon++;
	tmp = (u64 *)(buf + 2);
	if (kstrtoull(p, 10, tmp)) {
		hid_err(hid, "convert rand string fail,only decimal string allowed\n");
		ret = -EINVAL;
		goto fail;
	}
	hid_info(hid, "uint64 %llu\n", *(u64 *)(buf + 2));
	len = min((count - (colon - p)) / 2, sizeof(buf) - (sizeof(*tmp) + 2));
	for (i = 0; i < len; i++) {
		buf2[0] = colon[i * 2];
		buf2[1] = colon[i * 2 + 1];
		if (kstrtou8(buf2, 16, &buf[sizeof(*tmp) + 2 + i])) {
			hid_err(hid, "err sync string,only hex string allowed\n");
			ret = -EINVAL;
			goto fail;
		}
	}
	len = i + sizeof(*tmp) + 2;
	ret = rkvr_send_report(dev, (unsigned char *)buf, len);
	if (ret) {
		hid_err(hid, "hid_report_encrypt fail\n");
		ret = -EIO;
		goto fail;
	}
	hid_info(hid, "hid_report_encrypt ok\n");
	ret = count;
fail:
	kfree(p);

	return ret;
}

static int hid_get_capability(struct device *dev, struct hid_capability *caps)
{
	struct hid_device *hid = container_of(dev, struct hid_device, dev);
	u8 data = 0;

	caps->suspend_notify = 0;
	if (!rkvr_recv_report(dev, RKVR_ID_CAPS, &data, 1)) {
		hid_info(hid, "hid_get_capability %d\n", data);
		caps->suspend_notify = data;
		return 0;
	}
	return -1;
}

static void hid_report_fill_rw(unsigned char *buf, u8 reg, u8 *data, int len, int w)
{
	if (w)
		buf[0] = (1 << 7) | (len & 0x7f);
	else
		buf[0] = len & 0x7f;
	buf[1] = reg;
	memcpy(&buf[2], data, len);
}

#if DEBUG_SYS

static int hid_report_readreg(struct device *dev, u8 reg, u8 *data, int len)
{
	struct hid_device *hid = container_of(dev, struct hid_device, dev);
	unsigned char report_number = reg;
	unsigned char report_type = HID_REGR_REPORT;
	char buf[1 + sizeof(data) * len];
	int readlen = 1 + sizeof(data) * len;
	int ret;

	ret = hid_hw_raw_request(hid, report_number, (unsigned char *)buf, readlen, report_type, HID_REQ_GET_REPORT);
	if (ret != readlen) {
		hid_info(hid, "id_hw_raw_request fail\n");
	} else {
		memcpy(data, &buf[1], readlen);
		hid_info(hid, "hid_report_readreg %02x %02x\n", reg, data[0]);
	}

	return 0;
}

static int hid_report_writereg(struct device *dev, u8 reg, u8 data)
{
	struct hid_device *hid = container_of(dev, struct hid_device, dev);
	unsigned char report_number = HID_REPORT_ID_W;
	unsigned char report_type = HID_REGW_REPORT;
	char buf[3 + sizeof(data)];
	int ret;

	hid_report_fill_rw(&buf[1], reg, &data, sizeof(data), 1);
	ret = hid_hw_raw_request(hid, report_number, (unsigned char *)buf, 4, report_type, HID_REQ_SET_REPORT);
	if (ret != 4)
		hid_info(hid, "id_hw_raw_request fail\n");
	else
		hid_info(hid, "id_hw_raw_request ok\n");

	return 0;
}

static ssize_t rkvr_dev_attr_debug_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct hidraw *devraw;

	devraw = dev_get_drvdata(dev);
	if (0 == strncmp(buf, "write", 5))
		hid_report_writereg(&devraw->hid->dev, 0, 0);
	hid_info(devraw->hid, "%s\n", buf);

	return count;
}

static ssize_t rkvr_dev_attr_debug_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	size_t count = 0;
	u8 mpu6500_id = 0;
	struct hidraw *devraw;

	devraw = dev_get_drvdata(dev);
	if (!hid_report_readreg(&devraw->hid->dev, 0x75 | 0x80, &mpu6500_id, 1))
		count += sprintf(&buf[count], "reg value %d\n", mpu6500_id);
	else
		count += sprintf(&buf[count], "read fail\n");

	return count;
}
static DEVICE_ATTR(debug, 0664, rkvr_dev_attr_debug_show, rkvr_dev_attr_debug_store);

static ssize_t rkvr_dev_attr_sync_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct hidraw *devraw = dev_get_drvdata(dev);
	int ret;

	ret = hid_report_sync(&devraw->hid->dev, buf, count - 1);
	return ret > 0 ? count : ret;
}

static DEVICE_ATTR(sync, S_IWUSR, NULL, rkvr_dev_attr_sync_store);
#endif

static int rkvr_hid_read(struct rkvr_iio_hw_device *hdev, int reg, unsigned char *data, int len)
{
	struct hid_device *hid = container_of(hdev->dev, struct hid_device, dev);
	unsigned char report_number = reg;
	unsigned char report_type = HID_REGR_REPORT;
	char buf[1 + sizeof(*data) * len];
	int readlen = 1 + sizeof(*data) * len;
	int ret;

	ret = hid_hw_raw_request(hid, report_number, (unsigned char *)buf, readlen, report_type, HID_REQ_GET_REPORT);
	if (ret != readlen) {
		hid_err(hid, "id_hw_raw_request fail\n");
	} else {
		memcpy(data, &buf[1], sizeof(*data) * len);
	}

	return 0;
}

static int rkvr_hid_write(struct rkvr_iio_hw_device *hdev, int reg, unsigned char data)
{
	struct hid_device *hid = container_of(hdev->dev, struct hid_device, dev);
	unsigned char report_number = HID_REPORT_ID_W;
	unsigned char report_type = HID_REGW_REPORT;
	char buf[3 + sizeof(data)];
	int ret;

	hid_report_fill_rw(&buf[1], reg, &data, sizeof(data), 1);
	ret = hid_hw_raw_request(hid, report_number, (unsigned char *)buf, 4, report_type, HID_REQ_SET_REPORT);
	if (ret != 4)
		hid_info(hid, "id_hw_raw_request fail\n");
	else
		hid_info(hid, "id_hw_raw_request ok\n");

	return 0;
}

static int rkvr_hid_open(struct rkvr_iio_hw_device *hdev)
{
	struct hid_device *hid;
	int err;

	hid = container_of(hdev->dev, struct hid_device, dev);
	err = hid_hw_power(hid, PM_HINT_FULLON);
	if (err < 0)
		return err;
	err = hid_hw_open(hid);
	if (err < 0) {
		hid_hw_power(hid, PM_HINT_NORMAL);
		return err;
	}

	return 0;
}

static void rkvr_hid_close(struct rkvr_iio_hw_device *hdev)
{
	struct hid_device *hid;

	hid = container_of(hdev->dev, struct hid_device, dev);
	hid_hw_power(hid, PM_HINT_NORMAL);
	hid_hw_close(hid);
}

#if DYNAMIC_LOAD_MPU6500
static int register_mpu6500;
struct platform_device mpu6500_dev = {
	.name = "mpu6500",
};
#endif

static int rkvr_connect(struct hid_device *hid)
{
	int minor, result;
	struct hidraw *dev;

	/* we accept any HID device, no matter the applications */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	result = -EINVAL;
	mutex_lock(&minors_lock);
	for (minor = 0; minor < RKVR_HIDRAW_MAX_DEVICES; minor++) {
		if (rkvr_hidraw_table[minor])
			continue;
		rkvr_hidraw_table[minor] = dev;
		result = 0;
		break;
	}
	if (result) {
		mutex_unlock(&minors_lock);
		kfree(dev);
		goto out;
	}

	dev->dev = device_create(rkvr_class, &hid->dev, MKDEV(rkvr_major, minor),
					NULL, "%s%d", "rkvr", minor);

	if (IS_ERR(dev->dev)) {
		rkvr_hidraw_table[minor] = NULL;
		mutex_unlock(&minors_lock);
		result = PTR_ERR(dev->dev);
		kfree(dev);
		goto out;
	}

	dev_set_drvdata(dev->dev, dev);
#if DEBUG_SYS
	device_create_file(dev->dev, &dev_attr_debug);
	device_create_file(dev->dev, &dev_attr_sync);
#endif

	{
		struct rkvr_iio_hw_device *hw_device;

		hw_device = inv_hid_alloc("hid-rkvr");
		if (!hw_device) {
			hid_err(hid, "inv_hid_alloc(\"hid-rkvr\") fail\n");
			rkvr_hidraw_table[minor] = NULL;
			mutex_unlock(&minors_lock);
			result = PTR_ERR(dev->dev);
			kfree(dev);
			goto out;
		}
		hw_device->dev = &hid->dev;
		hw_device->open = rkvr_hid_open;
		hw_device->close = rkvr_hid_close;
		hw_device->read = rkvr_hid_read;
		hw_device->write = rkvr_hid_write;
		if (inv_hid_register_devcie(hw_device)) {
			hid_err(hid, "inv_hid_register_devcie(\"hid-rkvr\") fail\n");
			inv_hid_free(hw_device);
			rkvr_hidraw_table[minor] = NULL;
			mutex_unlock(&minors_lock);
			result = PTR_ERR(dev->dev);
			kfree(dev);
			goto out;
		}
	}

#if DYNAMIC_LOAD_MPU6500
	if (!register_mpu6500) {
		register_mpu6500 = 1;
		hid_info(hid, "--->platform_device_register-->\n");
		platform_device_register(&mpu6500_dev);
	}
#endif

	if (hid_hw_open(hid)) {
		rkvr_hidraw_table[minor] = NULL;
		mutex_unlock(&minors_lock);
		result = PTR_ERR(dev->dev);
		kfree(dev);
		hid_err(hid, "rkvr_connect:hid_hw_open fail\n");
		goto out;
	}

	init_waitqueue_head(&dev->wait);
	spin_lock_init(&dev->list_lock);
	INIT_LIST_HEAD(&dev->list);

	dev->hid = hid;
	dev->minor = minor;
	dev->exist = 1;
	hid->hidraw = dev; /*struct hidraw * **/

	hid_get_capability(&hid->dev, &rkvr_hid_capability[minor]);

	mutex_unlock(&minors_lock);
out:
	return result;
}

static int rkvr_keys_remove(struct hid_device *hdev)
{
	struct input_dev *input = hdev->hiddev;

	input_unregister_device(input);
	return 0;
}

static unsigned int key_codes[] = {
	KEY_MENU,
	KEY_HOME,
	KEY_POWER,
	KEY_VOLUMEUP,
	KEY_VOLUMEDOWN,
	KEY_WAKEUP,
	KEY_ESC,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_UP,
	KEY_DOWN,
	KEY_ENTER,
	BTN_MOUSE
};

static int __must_check rkvr_keys_probe(struct hid_device *hdev)
{

	struct device *dev = &hdev->dev;
	struct input_dev *input = NULL;
	int i, error = 0;

	input = devm_input_allocate_device(dev);
	if (!input) {
		hid_err(hdev, "input_allocate_device fail\n");
		return -ENOMEM;
	}
	input->name = "rkvr-keypad";
	input->phys = "rkvr-keys/input0";
	input->dev.parent = dev;
	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x071b;
	input->id.product = 0x3205;
	input->id.version = 0x0001;

	for (i = 0; i < sizeof(key_codes) / sizeof(key_codes[0]); i++) {
		hid_info(hdev, "input_set_capability %d\n", key_codes[i]);
		input_set_capability(input, EV_KEY, key_codes[i]);
	}

#ifdef RK_HID_GEAR_TOUCH
	set_bit(EV_REL, input->evbit);
	input_set_capability(input, EV_REL, REL_X);
	input_set_capability(input, EV_REL, REL_Y);
	input_set_capability(input, EV_MSC, MSC_SCAN);
	input_set_capability(input, EV_KEY, 0x110);
#endif

	error = input_register_device(input);
	if (error) {
		hid_err(hdev, "rkvr-s: Unable to register input device, error: %d\n", error);
		return error;
	}
	hdev->hiddev = input;

	return 0;
}

static inline int __must_check rkvr_hw_start(struct hid_device *hdev, unsigned int connect_mask)
{
	int ret = hdev->ll_driver->start(hdev);

	if (ret)
		return ret;
	ret = rkvr_connect(hdev);
	if (ret)
		hdev->ll_driver->stop(hdev);

	return ret;
}

static void rkvr_disconnect(struct hid_device *hid)
{
	struct hidraw *hidraw = hid->hidraw;

	mutex_lock(&minors_lock);
	/* always unregistering inv_hid_device when hardware disconnect */
	inv_hid_unregister_and_destroy_devcie_by_name("hid-rkvr");
#if DEBUG_SYS
	device_remove_file(hidraw->dev, &dev_attr_debug);
	device_remove_file(hidraw->dev, &dev_attr_sync);
#endif

	device_destroy(rkvr_class, MKDEV(rkvr_major, hidraw->minor));
	rkvr_drop_ref(hidraw, 1);
	mutex_unlock(&minors_lock);
}

static void rkvr_hw_stop(struct hid_device *hdev)
{
	rkvr_disconnect(hdev);
	hdev->ll_driver->stop(hdev);
}

static long rkvr_hidraw_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(file);
	unsigned int minor = iminor(inode);
	long ret = 0;
	struct hidraw *dev;
	void __user *user_arg = (void __user *)arg;

	mutex_lock(&minors_lock);
	dev = rkvr_hidraw_table[minor];
	if (!dev) {
		ret = -ENODEV;
		goto out;
	}

	switch (cmd) {
	case HIDIOCGRDESCSIZE:
		if (put_user(dev->hid->rsize, (int __user *)arg))
			ret = -EFAULT;
		break;

	case HIDIOCGRDESC:
		{
			__u32 len;

			if (get_user(len, (int __user *)arg))
				ret = -EFAULT;
			else if (len > HID_MAX_DESCRIPTOR_SIZE - 1)
				ret = -EINVAL;
			else if (copy_to_user(user_arg + offsetof(
				struct hidraw_report_descriptor,
				value[0]),
				dev->hid->rdesc,
				min(dev->hid->rsize, len)))
				ret = -EFAULT;
			break;
		}
	case HIDIOCGRAWINFO:
		{
			struct hidraw_devinfo dinfo;

			dinfo.bustype = dev->hid->bus;
			dinfo.vendor = dev->hid->vendor;
			dinfo.product = dev->hid->product;
			if (copy_to_user(user_arg, &dinfo, sizeof(dinfo)))
				ret = -EFAULT;
			break;
		}
	default:
		{
			struct hid_device *hid = dev->hid;

			if (_IOC_TYPE(cmd) != 'H') {
				ret = -EINVAL;
				break;
			}

			if (_IOC_NR(cmd) == _IOC_NR(HIDIOCSFEATURE(0))) {
				int len = _IOC_SIZE(cmd);

				ret = rkvr_hidraw_send_report(file, user_arg, len, HID_FEATURE_REPORT);
				break;
			}
			if (_IOC_NR(cmd) == _IOC_NR(HIDIOCGFEATURE(0))) {
				int len = _IOC_SIZE(cmd);

				ret = rkvr_hidraw_get_report(file, user_arg, len, HID_FEATURE_REPORT);
				break;
			}

			if (_IOC_NR(cmd) == _IOC_NR(HIDRKVRHANDSHAKE(0))) {
				int len = _IOC_SIZE(cmd);
				char *buf;

				buf = kzalloc(len + 1, GFP_KERNEL);
				if (!buf) {
					ret = -ENOMEM;
					break;
				}
				if (copy_from_user(buf, user_arg, len)) {
					ret = -EFAULT;
					kfree(buf);
					break;
				}
				ret = hid_report_sync(&hid->dev, buf, len);
				kfree(buf);
				break;
			}

			/* Begin Read-only ioctls. */
			if (_IOC_DIR(cmd) != _IOC_READ) {
				ret = -EINVAL;
				break;
			}

			if (_IOC_NR(cmd) == _IOC_NR(HIDIOCGRAWNAME(0))) {
				int len = strlen(hid->name) + 1;

				if (len > _IOC_SIZE(cmd))
					len = _IOC_SIZE(cmd);
				ret = copy_to_user(user_arg, hid->name, len) ?
					-EFAULT : len;
				break;
			}

			if (_IOC_NR(cmd) == _IOC_NR(HIDIOCGRAWPHYS(0))) {
				int len = strlen(hid->phys) + 1;

				if (len > _IOC_SIZE(cmd))
					len = _IOC_SIZE(cmd);
				ret = copy_to_user(user_arg, hid->phys, len) ?
					-EFAULT : len;
				break;
			}
		}

	ret = -ENOTTY;
	}
out:
	mutex_unlock(&minors_lock);
	return ret;
}

static const struct file_operations rkvr_ops = {
	.owner = THIS_MODULE,
	.read = rkvr_hidraw_read,
	.write = rkvr_hidraw_write,
	.poll = rkvr_hidraw_poll,
	.open = rkvr_hidraw_open,
	.release = rkvr_hidraw_release,
	.unlocked_ioctl = rkvr_hidraw_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = rkvr_hidraw_ioctl,
#endif
	.fasync = rkvr_hidraw_fasync,
	.llseek = noop_llseek,
};

int rkvr_sensor_register_callback(int (*callback)(char *, size_t, void *), void *priv)
{
	sensorData.priv = priv;
	sensorData.send_event = callback;

	return 0;
}
EXPORT_SYMBOL_GPL(rkvr_sensor_register_callback);

static int rkvr_fb_event_notify(struct notifier_block *self,
					   unsigned long action, void *data)
{
	int i;
	unsigned char buf[3] = {HID_REPORT_ID_RKVR, RKVR_ID_IDLE, 0};
	struct hid_device *hid;
	struct fb_event *event = data;
	int blank_mode;

	if (action != FB_EARLY_EVENT_BLANK && action != FB_EVENT_BLANK)
		return NOTIFY_OK;

	blank_mode = *((int *)event->data);

	mutex_lock(&minors_lock);
	for (i = 0; i < RKVR_HIDRAW_MAX_DEVICES; i++) {
		if (!rkvr_hidraw_table[i] || !rkvr_hidraw_table[i]->exist)
			continue;
		if (!rkvr_hid_capability[i].suspend_notify) {
			continue;
		}
		hid = rkvr_hidraw_table[i]->hid;
		if (action == FB_EARLY_EVENT_BLANK) {
			switch (blank_mode) {
			case FB_BLANK_UNBLANK:
				break;
			default:
				rkvr_send_report(&hid->dev, buf, 3);
				break;
			}
		} else if (action == FB_EVENT_BLANK) {
			switch (blank_mode) {
			case FB_BLANK_UNBLANK:
				buf[2] = 1;
				rkvr_send_report(&hid->dev, buf, 3);
				break;
			default:
				break;
			}
		}
	}
	mutex_unlock(&minors_lock);
	return NOTIFY_OK;
}

static struct notifier_block rkvr_fb_notifier = {
	.notifier_call = rkvr_fb_event_notify,
};

static int rkvr_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int retval;
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);

	retval = hid_parse(hdev);
	if (retval) {
		hid_err(hdev, "rkvr - parse failed\n");
		goto exit;
	}
	hid_set_drvdata(hdev, &sensorData);
	if (intf->cur_altsetting->desc.bInterfaceNumber == RKVR_INTERFACE_USB_SENSOR_ID) {
		retval = rkvr_keys_probe(hdev);
		if (retval) {
			hid_err(hdev, "rkvr_keys_probe failed\n");
			goto exit_stop;
		}
		retval = rkvr_hw_start(hdev, 0);
		if (retval) {
			hid_err(hdev, "rkvr - rkvr hw start failed\n");
			rkvr_keys_remove(hdev);
			goto exit_stop;
		}
	} else {
		retval = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
		if (retval) {
			hid_err(hdev, "rkvr - hid hw start failed\n");
			goto exit;
		}
	}

	return 0;

exit_stop:
	hid_hw_stop(hdev);
exit:
	return retval;
}

static void rkvr_remove(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);

	if (intf->cur_altsetting->desc.bInterfaceNumber == RKVR_INTERFACE_USB_SENSOR_ID) {
		rkvr_hw_stop(hdev);
		rkvr_keys_remove(hdev);
	} else {
		hid_hw_stop(hdev);
	}
}

static int rkvr_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size)
{
	int retval = 0;
	static unsigned int count;
	static unsigned long old_jiffy;

	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);

	if (intf->cur_altsetting->desc.bInterfaceNumber != RKVR_INTERFACE_USB_SENSOR_ID) {
		hid_info(hdev, "%s,ignored interface number is %d\n", __func__,
			intf->cur_altsetting->desc.bInterfaceNumber);
		return 0;
	}

	/* print sensor poll frequency */
	if (++count >= 1000) {
		unsigned long cur_jiffy = jiffies;

		hid_dbg(hdev, "rkvr: %d Hz, hidrkvr %d\n", (int)(1000 * HZ / (cur_jiffy - old_jiffy)), (hdev->hidraw ? 1 : 0));
		count = 0;
		old_jiffy = cur_jiffy;
	}

	if (hdev->hidraw || hdev->hiddev) {
		retval = rkvr_report_event(hdev, data, size);
		if (retval < 0)
			hid_info(hdev, "rkvr: raw event err %d\n", retval);
	}

	return retval;
}

static const struct hid_device_id rkvr_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCKCHIP, USB_DEVICE_ID_NANOC) },
	{ }
};

MODULE_DEVICE_TABLE(hid, rkvr_devices);

static struct hid_driver rkvr_driver = {
	.name = "rkvr",
	.id_table = rkvr_devices,
	.probe = rkvr_probe,
	.remove = rkvr_remove,
	.raw_event = rkvr_raw_event
};

static int __init rkvr_init(void)
{
	int retval;
	dev_t dev_id;

	rkvr_class = class_create(THIS_MODULE, "rkvr");
	if (IS_ERR(rkvr_class))
		return PTR_ERR(rkvr_class);

	retval = hid_register_driver(&rkvr_driver);
	if (retval < 0) {
		pr_warn("rkvr_init - Can't register drive.\n");
		goto out_class;
	}

	retval = alloc_chrdev_region(&dev_id, RKVR_FIRST_MINOR,
					RKVR_HIDRAW_MAX_DEVICES, "rkvr");
	if (retval < 0) {
		pr_warn("rkvr_init - Can't allocate chrdev region.\n");
		goto out_register;
	}

	rkvr_major = MAJOR(dev_id);
	cdev_init(&rkvr_cdev, &rkvr_ops);
	cdev_add(&rkvr_cdev, dev_id, RKVR_HIDRAW_MAX_DEVICES);

	retval = fb_register_client(&rkvr_fb_notifier);
	if (retval) {
		pr_warn("rkvr_init - Can't register fb notifier\n");
		goto out_chardev;
	}
	return 0;
out_chardev:
	unregister_chrdev_region(dev_id, RKVR_HIDRAW_MAX_DEVICES);
out_register:
	hid_unregister_driver(&rkvr_driver);
out_class:
	class_destroy(rkvr_class);

	return retval;
}

static void __exit rkvr_exit(void)
{
	dev_t dev_id = MKDEV(rkvr_major, 0);

	fb_unregister_client(&rkvr_fb_notifier);
	cdev_del(&rkvr_cdev);

	unregister_chrdev_region(dev_id, RKVR_HIDRAW_MAX_DEVICES);

	hid_unregister_driver(&rkvr_driver);
	class_destroy(rkvr_class);
}

module_init(rkvr_init);
module_exit(rkvr_exit);

MODULE_AUTHOR("zwp");
MODULE_DESCRIPTION("USB ROCKCHIP VR char device driver.");
MODULE_LICENSE("GPL v2");
