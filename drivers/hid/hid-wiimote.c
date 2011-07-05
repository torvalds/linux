/*
 * HID driver for Nintendo Wiimote devices
 * Copyright (c) 2011 David Herrmann
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include "hid-ids.h"

#define WIIMOTE_VERSION "0.1"
#define WIIMOTE_NAME "Nintendo Wii Remote"
#define WIIMOTE_BUFSIZE 32

struct wiimote_buf {
	__u8 data[HID_MAX_BUFFER_SIZE];
	size_t size;
};

struct wiimote_state {
	spinlock_t lock;
	__u8 flags;
};

struct wiimote_data {
	atomic_t ready;
	struct hid_device *hdev;
	struct input_dev *input;

	spinlock_t qlock;
	__u8 head;
	__u8 tail;
	struct wiimote_buf outq[WIIMOTE_BUFSIZE];
	struct work_struct worker;

	struct wiimote_state state;
};

#define WIIPROTO_FLAG_LED1 0x01
#define WIIPROTO_FLAG_LED2 0x02
#define WIIPROTO_FLAG_LED3 0x04
#define WIIPROTO_FLAG_LED4 0x08
#define WIIPROTO_FLAGS_LEDS (WIIPROTO_FLAG_LED1 | WIIPROTO_FLAG_LED2 | \
					WIIPROTO_FLAG_LED3 | WIIPROTO_FLAG_LED4)

enum wiiproto_reqs {
	WIIPROTO_REQ_LED = 0x11,
	WIIPROTO_REQ_DRM_K = 0x30,
};

enum wiiproto_keys {
	WIIPROTO_KEY_LEFT,
	WIIPROTO_KEY_RIGHT,
	WIIPROTO_KEY_UP,
	WIIPROTO_KEY_DOWN,
	WIIPROTO_KEY_PLUS,
	WIIPROTO_KEY_MINUS,
	WIIPROTO_KEY_ONE,
	WIIPROTO_KEY_TWO,
	WIIPROTO_KEY_A,
	WIIPROTO_KEY_B,
	WIIPROTO_KEY_HOME,
	WIIPROTO_KEY_COUNT
};

static __u16 wiiproto_keymap[] = {
	KEY_LEFT,	/* WIIPROTO_KEY_LEFT */
	KEY_RIGHT,	/* WIIPROTO_KEY_RIGHT */
	KEY_UP,		/* WIIPROTO_KEY_UP */
	KEY_DOWN,	/* WIIPROTO_KEY_DOWN */
	KEY_NEXT,	/* WIIPROTO_KEY_PLUS */
	KEY_PREVIOUS,	/* WIIPROTO_KEY_MINUS */
	BTN_1,		/* WIIPROTO_KEY_ONE */
	BTN_2,		/* WIIPROTO_KEY_TWO */
	BTN_A,		/* WIIPROTO_KEY_A */
	BTN_B,		/* WIIPROTO_KEY_B */
	BTN_MODE,	/* WIIPROTO_KEY_HOME */
};

#define dev_to_wii(pdev) hid_get_drvdata(container_of(pdev, struct hid_device, \
									dev))

static ssize_t wiimote_hid_send(struct hid_device *hdev, __u8 *buffer,
								size_t count)
{
	__u8 *buf;
	ssize_t ret;

	if (!hdev->hid_output_raw_report)
		return -ENODEV;

	buf = kmemdup(buffer, count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = hdev->hid_output_raw_report(hdev, buf, count, HID_OUTPUT_REPORT);

	kfree(buf);
	return ret;
}

static void wiimote_worker(struct work_struct *work)
{
	struct wiimote_data *wdata = container_of(work, struct wiimote_data,
									worker);
	unsigned long flags;

	spin_lock_irqsave(&wdata->qlock, flags);

	while (wdata->head != wdata->tail) {
		spin_unlock_irqrestore(&wdata->qlock, flags);
		wiimote_hid_send(wdata->hdev, wdata->outq[wdata->tail].data,
						wdata->outq[wdata->tail].size);
		spin_lock_irqsave(&wdata->qlock, flags);

		wdata->tail = (wdata->tail + 1) % WIIMOTE_BUFSIZE;
	}

	spin_unlock_irqrestore(&wdata->qlock, flags);
}

static void wiimote_queue(struct wiimote_data *wdata, const __u8 *buffer,
								size_t count)
{
	unsigned long flags;
	__u8 newhead;

	if (count > HID_MAX_BUFFER_SIZE) {
		hid_warn(wdata->hdev, "Sending too large output report\n");
		return;
	}

	/*
	 * Copy new request into our output queue and check whether the
	 * queue is full. If it is full, discard this request.
	 * If it is empty we need to start a new worker that will
	 * send out the buffer to the hid device.
	 * If the queue is not empty, then there must be a worker
	 * that is currently sending out our buffer and this worker
	 * will reschedule itself until the queue is empty.
	 */

	spin_lock_irqsave(&wdata->qlock, flags);

	memcpy(wdata->outq[wdata->head].data, buffer, count);
	wdata->outq[wdata->head].size = count;
	newhead = (wdata->head + 1) % WIIMOTE_BUFSIZE;

	if (wdata->head == wdata->tail) {
		wdata->head = newhead;
		schedule_work(&wdata->worker);
	} else if (newhead != wdata->tail) {
		wdata->head = newhead;
	} else {
		hid_warn(wdata->hdev, "Output queue is full");
	}

	spin_unlock_irqrestore(&wdata->qlock, flags);
}

static void wiiproto_req_leds(struct wiimote_data *wdata, int leds)
{
	__u8 cmd[2];

	leds &= WIIPROTO_FLAGS_LEDS;
	if ((wdata->state.flags & WIIPROTO_FLAGS_LEDS) == leds)
		return;
	wdata->state.flags = (wdata->state.flags & ~WIIPROTO_FLAGS_LEDS) | leds;

	cmd[0] = WIIPROTO_REQ_LED;
	cmd[1] = 0;

	if (leds & WIIPROTO_FLAG_LED1)
		cmd[1] |= 0x10;
	if (leds & WIIPROTO_FLAG_LED2)
		cmd[1] |= 0x20;
	if (leds & WIIPROTO_FLAG_LED3)
		cmd[1] |= 0x40;
	if (leds & WIIPROTO_FLAG_LED4)
		cmd[1] |= 0x80;

	wiimote_queue(wdata, cmd, sizeof(cmd));
}

#define wiifs_led_show_set(num)						\
static ssize_t wiifs_led_show_##num(struct device *dev,			\
			struct device_attribute *attr, char *buf)	\
{									\
	struct wiimote_data *wdata = dev_to_wii(dev);			\
	unsigned long flags;						\
	int state;							\
									\
	if (!atomic_read(&wdata->ready))				\
		return -EBUSY;						\
									\
	spin_lock_irqsave(&wdata->state.lock, flags);			\
	state = !!(wdata->state.flags & WIIPROTO_FLAG_LED##num);	\
	spin_unlock_irqrestore(&wdata->state.lock, flags);		\
									\
	return sprintf(buf, "%d\n", state);				\
}									\
static ssize_t wiifs_led_set_##num(struct device *dev,			\
	struct device_attribute *attr, const char *buf, size_t count)	\
{									\
	struct wiimote_data *wdata = dev_to_wii(dev);			\
	int tmp = simple_strtoul(buf, NULL, 10);			\
	unsigned long flags;						\
	__u8 state;							\
									\
	if (!atomic_read(&wdata->ready))				\
		return -EBUSY;						\
									\
	spin_lock_irqsave(&wdata->state.lock, flags);			\
									\
	state = wdata->state.flags;					\
									\
	if (tmp)							\
		wiiproto_req_leds(wdata, state | WIIPROTO_FLAG_LED##num);\
	else								\
		wiiproto_req_leds(wdata, state & ~WIIPROTO_FLAG_LED##num);\
									\
	spin_unlock_irqrestore(&wdata->state.lock, flags);		\
									\
	return count;							\
}									\
static DEVICE_ATTR(led##num, S_IRUGO | S_IWUSR, wiifs_led_show_##num,	\
						wiifs_led_set_##num)

wiifs_led_show_set(1);
wiifs_led_show_set(2);
wiifs_led_show_set(3);
wiifs_led_show_set(4);

static int wiimote_input_event(struct input_dev *dev, unsigned int type,
						unsigned int code, int value)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);

	if (!atomic_read(&wdata->ready))
		return -EBUSY;
	/* smp_rmb: Make sure wdata->xy is available when wdata->ready is 1 */
	smp_rmb();

	return 0;
}

static void handler_keys(struct wiimote_data *wdata, const __u8 *payload)
{
	input_report_key(wdata->input, wiiproto_keymap[WIIPROTO_KEY_LEFT],
							!!(payload[0] & 0x01));
	input_report_key(wdata->input, wiiproto_keymap[WIIPROTO_KEY_RIGHT],
							!!(payload[0] & 0x02));
	input_report_key(wdata->input, wiiproto_keymap[WIIPROTO_KEY_DOWN],
							!!(payload[0] & 0x04));
	input_report_key(wdata->input, wiiproto_keymap[WIIPROTO_KEY_UP],
							!!(payload[0] & 0x08));
	input_report_key(wdata->input, wiiproto_keymap[WIIPROTO_KEY_PLUS],
							!!(payload[0] & 0x10));
	input_report_key(wdata->input, wiiproto_keymap[WIIPROTO_KEY_TWO],
							!!(payload[1] & 0x01));
	input_report_key(wdata->input, wiiproto_keymap[WIIPROTO_KEY_ONE],
							!!(payload[1] & 0x02));
	input_report_key(wdata->input, wiiproto_keymap[WIIPROTO_KEY_B],
							!!(payload[1] & 0x04));
	input_report_key(wdata->input, wiiproto_keymap[WIIPROTO_KEY_A],
							!!(payload[1] & 0x08));
	input_report_key(wdata->input, wiiproto_keymap[WIIPROTO_KEY_MINUS],
							!!(payload[1] & 0x10));
	input_report_key(wdata->input, wiiproto_keymap[WIIPROTO_KEY_HOME],
							!!(payload[1] & 0x80));
	input_sync(wdata->input);
}

struct wiiproto_handler {
	__u8 id;
	size_t size;
	void (*func)(struct wiimote_data *wdata, const __u8 *payload);
};

static struct wiiproto_handler handlers[] = {
	{ .id = WIIPROTO_REQ_DRM_K, .size = 2, .func = handler_keys },
	{ .id = 0 }
};

static int wiimote_hid_event(struct hid_device *hdev, struct hid_report *report,
							u8 *raw_data, int size)
{
	struct wiimote_data *wdata = hid_get_drvdata(hdev);
	struct wiiproto_handler *h;
	int i;
	unsigned long flags;

	if (!atomic_read(&wdata->ready))
		return -EBUSY;
	/* smp_rmb: Make sure wdata->xy is available when wdata->ready is 1 */
	smp_rmb();

	if (size < 1)
		return -EINVAL;

	spin_lock_irqsave(&wdata->state.lock, flags);

	for (i = 0; handlers[i].id; ++i) {
		h = &handlers[i];
		if (h->id == raw_data[0] && h->size < size)
			h->func(wdata, &raw_data[1]);
	}

	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;
}

static struct wiimote_data *wiimote_create(struct hid_device *hdev)
{
	struct wiimote_data *wdata;
	int i;

	wdata = kzalloc(sizeof(*wdata), GFP_KERNEL);
	if (!wdata)
		return NULL;

	wdata->input = input_allocate_device();
	if (!wdata->input) {
		kfree(wdata);
		return NULL;
	}

	wdata->hdev = hdev;
	hid_set_drvdata(hdev, wdata);

	input_set_drvdata(wdata->input, wdata);
	wdata->input->event = wiimote_input_event;
	wdata->input->dev.parent = &wdata->hdev->dev;
	wdata->input->id.bustype = wdata->hdev->bus;
	wdata->input->id.vendor = wdata->hdev->vendor;
	wdata->input->id.product = wdata->hdev->product;
	wdata->input->id.version = wdata->hdev->version;
	wdata->input->name = WIIMOTE_NAME;

	set_bit(EV_KEY, wdata->input->evbit);
	for (i = 0; i < WIIPROTO_KEY_COUNT; ++i)
		set_bit(wiiproto_keymap[i], wdata->input->keybit);

	spin_lock_init(&wdata->qlock);
	INIT_WORK(&wdata->worker, wiimote_worker);

	spin_lock_init(&wdata->state.lock);

	return wdata;
}

static void wiimote_destroy(struct wiimote_data *wdata)
{
	kfree(wdata);
}

static int wiimote_hid_probe(struct hid_device *hdev,
				const struct hid_device_id *id)
{
	struct wiimote_data *wdata;
	int ret;

	wdata = wiimote_create(hdev);
	if (!wdata) {
		hid_err(hdev, "Can't alloc device\n");
		return -ENOMEM;
	}

	ret = device_create_file(&hdev->dev, &dev_attr_led1);
	if (ret)
		goto err;
	ret = device_create_file(&hdev->dev, &dev_attr_led2);
	if (ret)
		goto err;
	ret = device_create_file(&hdev->dev, &dev_attr_led3);
	if (ret)
		goto err;
	ret = device_create_file(&hdev->dev, &dev_attr_led4);
	if (ret)
		goto err;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "HID parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		hid_err(hdev, "HW start failed\n");
		goto err;
	}

	ret = input_register_device(wdata->input);
	if (ret) {
		hid_err(hdev, "Cannot register input device\n");
		goto err_stop;
	}

	/* smp_wmb: Write wdata->xy first before wdata->ready is set to 1 */
	smp_wmb();
	atomic_set(&wdata->ready, 1);
	hid_info(hdev, "New device registered\n");

	/* by default set led1 after device initialization */
	spin_lock_irq(&wdata->state.lock);
	wiiproto_req_leds(wdata, WIIPROTO_FLAG_LED1);
	spin_unlock_irq(&wdata->state.lock);

	return 0;

err_stop:
	hid_hw_stop(hdev);
err:
	input_free_device(wdata->input);
	device_remove_file(&hdev->dev, &dev_attr_led1);
	device_remove_file(&hdev->dev, &dev_attr_led2);
	device_remove_file(&hdev->dev, &dev_attr_led3);
	device_remove_file(&hdev->dev, &dev_attr_led4);
	wiimote_destroy(wdata);
	return ret;
}

static void wiimote_hid_remove(struct hid_device *hdev)
{
	struct wiimote_data *wdata = hid_get_drvdata(hdev);

	hid_info(hdev, "Device removed\n");

	device_remove_file(&hdev->dev, &dev_attr_led1);
	device_remove_file(&hdev->dev, &dev_attr_led2);
	device_remove_file(&hdev->dev, &dev_attr_led3);
	device_remove_file(&hdev->dev, &dev_attr_led4);

	hid_hw_stop(hdev);
	input_unregister_device(wdata->input);

	cancel_work_sync(&wdata->worker);
	wiimote_destroy(wdata);
}

static const struct hid_device_id wiimote_hid_devices[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_NINTENDO,
				USB_DEVICE_ID_NINTENDO_WIIMOTE) },
	{ }
};
MODULE_DEVICE_TABLE(hid, wiimote_hid_devices);

static struct hid_driver wiimote_hid_driver = {
	.name = "wiimote",
	.id_table = wiimote_hid_devices,
	.probe = wiimote_hid_probe,
	.remove = wiimote_hid_remove,
	.raw_event = wiimote_hid_event,
};

static int __init wiimote_init(void)
{
	int ret;

	ret = hid_register_driver(&wiimote_hid_driver);
	if (ret)
		pr_err("Can't register wiimote hid driver\n");

	return ret;
}

static void __exit wiimote_exit(void)
{
	hid_unregister_driver(&wiimote_hid_driver);
}

module_init(wiimote_init);
module_exit(wiimote_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Herrmann <dh.herrmann@gmail.com>");
MODULE_DESCRIPTION(WIIMOTE_NAME " Device Driver");
MODULE_VERSION(WIIMOTE_VERSION);
