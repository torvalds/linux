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

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/spinlock.h>
#include "hid-ids.h"
#include "hid-wiimote.h"

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

static enum power_supply_property wiimote_battery_props[] = {
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_SCOPE,
};

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

/*
 * This sets the rumble bit on the given output report if rumble is
 * currently enabled.
 * \cmd1 must point to the second byte in the output report => &cmd[1]
 * This must be called on nearly every output report before passing it
 * into the output queue!
 */
static inline void wiiproto_keep_rumble(struct wiimote_data *wdata, __u8 *cmd1)
{
	if (wdata->state.flags & WIIPROTO_FLAG_RUMBLE)
		*cmd1 |= 0x01;
}

static void wiiproto_req_rumble(struct wiimote_data *wdata, __u8 rumble)
{
	__u8 cmd[2];

	rumble = !!rumble;
	if (rumble == !!(wdata->state.flags & WIIPROTO_FLAG_RUMBLE))
		return;

	if (rumble)
		wdata->state.flags |= WIIPROTO_FLAG_RUMBLE;
	else
		wdata->state.flags &= ~WIIPROTO_FLAG_RUMBLE;

	cmd[0] = WIIPROTO_REQ_RUMBLE;
	cmd[1] = 0;

	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
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

	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
}

/*
 * Check what peripherals of the wiimote are currently
 * active and select a proper DRM that supports all of
 * the requested data inputs.
 */
static __u8 select_drm(struct wiimote_data *wdata)
{
	__u8 ir = wdata->state.flags & WIIPROTO_FLAGS_IR;
	bool ext = wiiext_active(wdata);

	if (ir == WIIPROTO_FLAG_IR_BASIC) {
		if (wdata->state.flags & WIIPROTO_FLAG_ACCEL)
			return WIIPROTO_REQ_DRM_KAIE;
		else
			return WIIPROTO_REQ_DRM_KIE;
	} else if (ir == WIIPROTO_FLAG_IR_EXT) {
		return WIIPROTO_REQ_DRM_KAI;
	} else if (ir == WIIPROTO_FLAG_IR_FULL) {
		return WIIPROTO_REQ_DRM_SKAI1;
	} else {
		if (wdata->state.flags & WIIPROTO_FLAG_ACCEL) {
			if (ext)
				return WIIPROTO_REQ_DRM_KAE;
			else
				return WIIPROTO_REQ_DRM_KA;
		} else {
			if (ext)
				return WIIPROTO_REQ_DRM_KE;
			else
				return WIIPROTO_REQ_DRM_K;
		}
	}
}

void wiiproto_req_drm(struct wiimote_data *wdata, __u8 drm)
{
	__u8 cmd[3];

	if (drm == WIIPROTO_REQ_NULL)
		drm = select_drm(wdata);

	cmd[0] = WIIPROTO_REQ_DRM;
	cmd[1] = 0;
	cmd[2] = drm;

	wdata->state.drm = drm;
	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
}

static void wiiproto_req_status(struct wiimote_data *wdata)
{
	__u8 cmd[2];

	cmd[0] = WIIPROTO_REQ_SREQ;
	cmd[1] = 0;

	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
}

static void wiiproto_req_accel(struct wiimote_data *wdata, __u8 accel)
{
	accel = !!accel;
	if (accel == !!(wdata->state.flags & WIIPROTO_FLAG_ACCEL))
		return;

	if (accel)
		wdata->state.flags |= WIIPROTO_FLAG_ACCEL;
	else
		wdata->state.flags &= ~WIIPROTO_FLAG_ACCEL;

	wiiproto_req_drm(wdata, WIIPROTO_REQ_NULL);
}

static void wiiproto_req_ir1(struct wiimote_data *wdata, __u8 flags)
{
	__u8 cmd[2];

	cmd[0] = WIIPROTO_REQ_IR1;
	cmd[1] = flags;

	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
}

static void wiiproto_req_ir2(struct wiimote_data *wdata, __u8 flags)
{
	__u8 cmd[2];

	cmd[0] = WIIPROTO_REQ_IR2;
	cmd[1] = flags;

	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
}

#define wiiproto_req_wreg(wdata, os, buf, sz) \
			wiiproto_req_wmem((wdata), false, (os), (buf), (sz))

#define wiiproto_req_weeprom(wdata, os, buf, sz) \
			wiiproto_req_wmem((wdata), true, (os), (buf), (sz))

static void wiiproto_req_wmem(struct wiimote_data *wdata, bool eeprom,
				__u32 offset, const __u8 *buf, __u8 size)
{
	__u8 cmd[22];

	if (size > 16 || size == 0) {
		hid_warn(wdata->hdev, "Invalid length %d wmem request\n", size);
		return;
	}

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = WIIPROTO_REQ_WMEM;
	cmd[2] = (offset >> 16) & 0xff;
	cmd[3] = (offset >> 8) & 0xff;
	cmd[4] = offset & 0xff;
	cmd[5] = size;
	memcpy(&cmd[6], buf, size);

	if (!eeprom)
		cmd[1] |= 0x04;

	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
}

void wiiproto_req_rmem(struct wiimote_data *wdata, bool eeprom, __u32 offset,
								__u16 size)
{
	__u8 cmd[7];

	if (size == 0) {
		hid_warn(wdata->hdev, "Invalid length %d rmem request\n", size);
		return;
	}

	cmd[0] = WIIPROTO_REQ_RMEM;
	cmd[1] = 0;
	cmd[2] = (offset >> 16) & 0xff;
	cmd[3] = (offset >> 8) & 0xff;
	cmd[4] = offset & 0xff;
	cmd[5] = (size >> 8) & 0xff;
	cmd[6] = size & 0xff;

	if (!eeprom)
		cmd[1] |= 0x04;

	wiiproto_keep_rumble(wdata, &cmd[1]);
	wiimote_queue(wdata, cmd, sizeof(cmd));
}

/* requries the cmd-mutex to be held */
int wiimote_cmd_write(struct wiimote_data *wdata, __u32 offset,
						const __u8 *wmem, __u8 size)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wiimote_cmd_set(wdata, WIIPROTO_REQ_WMEM, 0);
	wiiproto_req_wreg(wdata, offset, wmem, size);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	ret = wiimote_cmd_wait(wdata);
	if (!ret && wdata->state.cmd_err)
		ret = -EIO;

	return ret;
}

/* requries the cmd-mutex to be held */
ssize_t wiimote_cmd_read(struct wiimote_data *wdata, __u32 offset, __u8 *rmem,
								__u8 size)
{
	unsigned long flags;
	ssize_t ret;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.cmd_read_size = size;
	wdata->state.cmd_read_buf = rmem;
	wiimote_cmd_set(wdata, WIIPROTO_REQ_RMEM, offset & 0xffff);
	wiiproto_req_rreg(wdata, offset, size);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	ret = wiimote_cmd_wait(wdata);

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.cmd_read_buf = NULL;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	if (!ret) {
		if (wdata->state.cmd_read_size == 0)
			ret = -EIO;
		else
			ret = wdata->state.cmd_read_size;
	}

	return ret;
}

static int wiimote_battery_get_property(struct power_supply *psy,
						enum power_supply_property psp,
						union power_supply_propval *val)
{
	struct wiimote_data *wdata = container_of(psy,
						struct wiimote_data, battery);
	int ret = 0, state;
	unsigned long flags;

	if (psp == POWER_SUPPLY_PROP_SCOPE) {
		val->intval = POWER_SUPPLY_SCOPE_DEVICE;
		return 0;
	}

	ret = wiimote_cmd_acquire(wdata);
	if (ret)
		return ret;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wiimote_cmd_set(wdata, WIIPROTO_REQ_SREQ, 0);
	wiiproto_req_status(wdata);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	ret = wiimote_cmd_wait(wdata);
	state = wdata->state.cmd_battery;
	wiimote_cmd_release(wdata);

	if (ret)
		return ret;

	switch (psp) {
		case POWER_SUPPLY_PROP_CAPACITY:
			val->intval = state * 100 / 255;
			break;
		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

static int wiimote_init_ir(struct wiimote_data *wdata, __u16 mode)
{
	int ret;
	unsigned long flags;
	__u8 format = 0;
	static const __u8 data_enable[] = { 0x01 };
	static const __u8 data_sens1[] = { 0x02, 0x00, 0x00, 0x71, 0x01,
						0x00, 0xaa, 0x00, 0x64 };
	static const __u8 data_sens2[] = { 0x63, 0x03 };
	static const __u8 data_fin[] = { 0x08 };

	spin_lock_irqsave(&wdata->state.lock, flags);

	if (mode == (wdata->state.flags & WIIPROTO_FLAGS_IR)) {
		spin_unlock_irqrestore(&wdata->state.lock, flags);
		return 0;
	}

	if (mode == 0) {
		wdata->state.flags &= ~WIIPROTO_FLAGS_IR;
		wiiproto_req_ir1(wdata, 0);
		wiiproto_req_ir2(wdata, 0);
		wiiproto_req_drm(wdata, WIIPROTO_REQ_NULL);
		spin_unlock_irqrestore(&wdata->state.lock, flags);
		return 0;
	}

	spin_unlock_irqrestore(&wdata->state.lock, flags);

	ret = wiimote_cmd_acquire(wdata);
	if (ret)
		return ret;

	/* send PIXEL CLOCK ENABLE cmd first */
	spin_lock_irqsave(&wdata->state.lock, flags);
	wiimote_cmd_set(wdata, WIIPROTO_REQ_IR1, 0);
	wiiproto_req_ir1(wdata, 0x06);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	ret = wiimote_cmd_wait(wdata);
	if (ret)
		goto unlock;
	if (wdata->state.cmd_err) {
		ret = -EIO;
		goto unlock;
	}

	/* enable IR LOGIC */
	spin_lock_irqsave(&wdata->state.lock, flags);
	wiimote_cmd_set(wdata, WIIPROTO_REQ_IR2, 0);
	wiiproto_req_ir2(wdata, 0x06);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	ret = wiimote_cmd_wait(wdata);
	if (ret)
		goto unlock;
	if (wdata->state.cmd_err) {
		ret = -EIO;
		goto unlock;
	}

	/* enable IR cam but do not make it send data, yet */
	ret = wiimote_cmd_write(wdata, 0xb00030, data_enable,
							sizeof(data_enable));
	if (ret)
		goto unlock;

	/* write first sensitivity block */
	ret = wiimote_cmd_write(wdata, 0xb00000, data_sens1,
							sizeof(data_sens1));
	if (ret)
		goto unlock;

	/* write second sensitivity block */
	ret = wiimote_cmd_write(wdata, 0xb0001a, data_sens2,
							sizeof(data_sens2));
	if (ret)
		goto unlock;

	/* put IR cam into desired state */
	switch (mode) {
		case WIIPROTO_FLAG_IR_FULL:
			format = 5;
			break;
		case WIIPROTO_FLAG_IR_EXT:
			format = 3;
			break;
		case WIIPROTO_FLAG_IR_BASIC:
			format = 1;
			break;
	}
	ret = wiimote_cmd_write(wdata, 0xb00033, &format, sizeof(format));
	if (ret)
		goto unlock;

	/* make IR cam send data */
	ret = wiimote_cmd_write(wdata, 0xb00030, data_fin, sizeof(data_fin));
	if (ret)
		goto unlock;

	/* request new DRM mode compatible to IR mode */
	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.flags &= ~WIIPROTO_FLAGS_IR;
	wdata->state.flags |= mode & WIIPROTO_FLAGS_IR;
	wiiproto_req_drm(wdata, WIIPROTO_REQ_NULL);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

unlock:
	wiimote_cmd_release(wdata);
	return ret;
}

static enum led_brightness wiimote_leds_get(struct led_classdev *led_dev)
{
	struct wiimote_data *wdata;
	struct device *dev = led_dev->dev->parent;
	int i;
	unsigned long flags;
	bool value = false;

	wdata = hid_get_drvdata(container_of(dev, struct hid_device, dev));

	for (i = 0; i < 4; ++i) {
		if (wdata->leds[i] == led_dev) {
			spin_lock_irqsave(&wdata->state.lock, flags);
			value = wdata->state.flags & WIIPROTO_FLAG_LED(i + 1);
			spin_unlock_irqrestore(&wdata->state.lock, flags);
			break;
		}
	}

	return value ? LED_FULL : LED_OFF;
}

static void wiimote_leds_set(struct led_classdev *led_dev,
						enum led_brightness value)
{
	struct wiimote_data *wdata;
	struct device *dev = led_dev->dev->parent;
	int i;
	unsigned long flags;
	__u8 state, flag;

	wdata = hid_get_drvdata(container_of(dev, struct hid_device, dev));

	for (i = 0; i < 4; ++i) {
		if (wdata->leds[i] == led_dev) {
			flag = WIIPROTO_FLAG_LED(i + 1);
			spin_lock_irqsave(&wdata->state.lock, flags);
			state = wdata->state.flags;
			if (value == LED_OFF)
				wiiproto_req_leds(wdata, state & ~flag);
			else
				wiiproto_req_leds(wdata, state | flag);
			spin_unlock_irqrestore(&wdata->state.lock, flags);
			break;
		}
	}
}

static int wiimote_ff_play(struct input_dev *dev, void *data,
							struct ff_effect *eff)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);
	__u8 value;
	unsigned long flags;

	/*
	 * The wiimote supports only a single rumble motor so if any magnitude
	 * is set to non-zero then we start the rumble motor. If both are set to
	 * zero, we stop the rumble motor.
	 */

	if (eff->u.rumble.strong_magnitude || eff->u.rumble.weak_magnitude)
		value = 1;
	else
		value = 0;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wiiproto_req_rumble(wdata, value);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;
}

static int wiimote_input_open(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);

	return hid_hw_open(wdata->hdev);
}

static void wiimote_input_close(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);

	hid_hw_close(wdata->hdev);
}

static int wiimote_accel_open(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);
	int ret;
	unsigned long flags;

	ret = hid_hw_open(wdata->hdev);
	if (ret)
		return ret;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wiiproto_req_accel(wdata, true);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;
}

static void wiimote_accel_close(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wiiproto_req_accel(wdata, false);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	hid_hw_close(wdata->hdev);
}

static int wiimote_ir_open(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);
	int ret;

	ret = hid_hw_open(wdata->hdev);
	if (ret)
		return ret;

	ret = wiimote_init_ir(wdata, WIIPROTO_FLAG_IR_BASIC);
	if (ret) {
		hid_hw_close(wdata->hdev);
		return ret;
	}

	return 0;
}

static void wiimote_ir_close(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);

	wiimote_init_ir(wdata, 0);
	hid_hw_close(wdata->hdev);
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

static void handler_accel(struct wiimote_data *wdata, const __u8 *payload)
{
	__u16 x, y, z;

	if (!(wdata->state.flags & WIIPROTO_FLAG_ACCEL))
		return;

	/*
	 * payload is: BB BB XX YY ZZ
	 * Accelerometer data is encoded into 3 10bit values. XX, YY and ZZ
	 * contain the upper 8 bits of each value. The lower 2 bits are
	 * contained in the buttons data BB BB.
	 * Bits 6 and 7 of the first buttons byte BB is the lower 2 bits of the
	 * X accel value. Bit 5 of the second buttons byte is the 2nd bit of Y
	 * accel value and bit 6 is the second bit of the Z value.
	 * The first bit of Y and Z values is not available and always set to 0.
	 * 0x200 is returned on no movement.
	 */

	x = payload[2] << 2;
	y = payload[3] << 2;
	z = payload[4] << 2;

	x |= (payload[0] >> 5) & 0x3;
	y |= (payload[1] >> 4) & 0x2;
	z |= (payload[1] >> 5) & 0x2;

	input_report_abs(wdata->accel, ABS_RX, x - 0x200);
	input_report_abs(wdata->accel, ABS_RY, y - 0x200);
	input_report_abs(wdata->accel, ABS_RZ, z - 0x200);
	input_sync(wdata->accel);
}

#define ir_to_input0(wdata, ir, packed) __ir_to_input((wdata), (ir), (packed), \
							ABS_HAT0X, ABS_HAT0Y)
#define ir_to_input1(wdata, ir, packed) __ir_to_input((wdata), (ir), (packed), \
							ABS_HAT1X, ABS_HAT1Y)
#define ir_to_input2(wdata, ir, packed) __ir_to_input((wdata), (ir), (packed), \
							ABS_HAT2X, ABS_HAT2Y)
#define ir_to_input3(wdata, ir, packed) __ir_to_input((wdata), (ir), (packed), \
							ABS_HAT3X, ABS_HAT3Y)

static void __ir_to_input(struct wiimote_data *wdata, const __u8 *ir,
						bool packed, __u8 xid, __u8 yid)
{
	__u16 x, y;

	if (!(wdata->state.flags & WIIPROTO_FLAGS_IR))
		return;

	/*
	 * Basic IR data is encoded into 3 bytes. The first two bytes are the
	 * lower 8 bit of the X/Y data, the 3rd byte contains the upper 2 bits
	 * of both.
	 * If data is packed, then the 3rd byte is put first and slightly
	 * reordered. This allows to interleave packed and non-packed data to
	 * have two IR sets in 5 bytes instead of 6.
	 * The resulting 10bit X/Y values are passed to the ABS_HATXY input dev.
	 */

	if (packed) {
		x = ir[1] | ((ir[0] & 0x03) << 8);
		y = ir[2] | ((ir[0] & 0x0c) << 6);
	} else {
		x = ir[0] | ((ir[2] & 0x30) << 4);
		y = ir[1] | ((ir[2] & 0xc0) << 2);
	}

	input_report_abs(wdata->ir, xid, x);
	input_report_abs(wdata->ir, yid, y);
}

static void handler_status(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);

	/* on status reports the drm is reset so we need to resend the drm */
	wiiproto_req_drm(wdata, WIIPROTO_REQ_NULL);

	wiiext_event(wdata, payload[2] & 0x02);

	if (wiimote_cmd_pending(wdata, WIIPROTO_REQ_SREQ, 0)) {
		wdata->state.cmd_battery = payload[5];
		wiimote_cmd_complete(wdata);
	}
}

static void handler_data(struct wiimote_data *wdata, const __u8 *payload)
{
	__u16 offset = payload[3] << 8 | payload[4];
	__u8 size = (payload[2] >> 4) + 1;
	__u8 err = payload[2] & 0x0f;

	handler_keys(wdata, payload);

	if (wiimote_cmd_pending(wdata, WIIPROTO_REQ_RMEM, offset)) {
		if (err)
			size = 0;
		else if (size > wdata->state.cmd_read_size)
			size = wdata->state.cmd_read_size;

		wdata->state.cmd_read_size = size;
		if (wdata->state.cmd_read_buf)
			memcpy(wdata->state.cmd_read_buf, &payload[5], size);
		wiimote_cmd_complete(wdata);
	}
}

static void handler_return(struct wiimote_data *wdata, const __u8 *payload)
{
	__u8 err = payload[3];
	__u8 cmd = payload[2];

	handler_keys(wdata, payload);

	if (wiimote_cmd_pending(wdata, cmd, 0)) {
		wdata->state.cmd_err = err;
		wiimote_cmd_complete(wdata);
	} else if (err) {
		hid_warn(wdata->hdev, "Remote error %hhu on req %hhu\n", err,
									cmd);
	}
}

static void handler_drm_KA(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);
	handler_accel(wdata, payload);
}

static void handler_drm_KE(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);
	wiiext_handle(wdata, &payload[2]);
}

static void handler_drm_KAI(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);
	handler_accel(wdata, payload);
	ir_to_input0(wdata, &payload[5], false);
	ir_to_input1(wdata, &payload[8], false);
	ir_to_input2(wdata, &payload[11], false);
	ir_to_input3(wdata, &payload[14], false);
	input_sync(wdata->ir);
}

static void handler_drm_KEE(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);
	wiiext_handle(wdata, &payload[2]);
}

static void handler_drm_KIE(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);
	ir_to_input0(wdata, &payload[2], false);
	ir_to_input1(wdata, &payload[4], true);
	ir_to_input2(wdata, &payload[7], false);
	ir_to_input3(wdata, &payload[9], true);
	input_sync(wdata->ir);
	wiiext_handle(wdata, &payload[12]);
}

static void handler_drm_KAE(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);
	handler_accel(wdata, payload);
	wiiext_handle(wdata, &payload[5]);
}

static void handler_drm_KAIE(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);
	handler_accel(wdata, payload);
	ir_to_input0(wdata, &payload[5], false);
	ir_to_input1(wdata, &payload[7], true);
	ir_to_input2(wdata, &payload[10], false);
	ir_to_input3(wdata, &payload[12], true);
	input_sync(wdata->ir);
	wiiext_handle(wdata, &payload[15]);
}

static void handler_drm_E(struct wiimote_data *wdata, const __u8 *payload)
{
	wiiext_handle(wdata, payload);
}

static void handler_drm_SKAI1(struct wiimote_data *wdata, const __u8 *payload)
{
	handler_keys(wdata, payload);

	wdata->state.accel_split[0] = payload[2];
	wdata->state.accel_split[1] = (payload[0] >> 1) & (0x10 | 0x20);
	wdata->state.accel_split[1] |= (payload[1] << 1) & (0x40 | 0x80);

	ir_to_input0(wdata, &payload[3], false);
	ir_to_input1(wdata, &payload[12], false);
	input_sync(wdata->ir);
}

static void handler_drm_SKAI2(struct wiimote_data *wdata, const __u8 *payload)
{
	__u8 buf[5];

	handler_keys(wdata, payload);

	wdata->state.accel_split[1] |= (payload[0] >> 5) & (0x01 | 0x02);
	wdata->state.accel_split[1] |= (payload[1] >> 3) & (0x04 | 0x08);

	buf[0] = 0;
	buf[1] = 0;
	buf[2] = wdata->state.accel_split[0];
	buf[3] = payload[2];
	buf[4] = wdata->state.accel_split[1];
	handler_accel(wdata, buf);

	ir_to_input2(wdata, &payload[3], false);
	ir_to_input3(wdata, &payload[12], false);
	input_sync(wdata->ir);
}

struct wiiproto_handler {
	__u8 id;
	size_t size;
	void (*func)(struct wiimote_data *wdata, const __u8 *payload);
};

static struct wiiproto_handler handlers[] = {
	{ .id = WIIPROTO_REQ_STATUS, .size = 6, .func = handler_status },
	{ .id = WIIPROTO_REQ_DATA, .size = 21, .func = handler_data },
	{ .id = WIIPROTO_REQ_RETURN, .size = 4, .func = handler_return },
	{ .id = WIIPROTO_REQ_DRM_K, .size = 2, .func = handler_keys },
	{ .id = WIIPROTO_REQ_DRM_KA, .size = 5, .func = handler_drm_KA },
	{ .id = WIIPROTO_REQ_DRM_KE, .size = 10, .func = handler_drm_KE },
	{ .id = WIIPROTO_REQ_DRM_KAI, .size = 17, .func = handler_drm_KAI },
	{ .id = WIIPROTO_REQ_DRM_KEE, .size = 21, .func = handler_drm_KEE },
	{ .id = WIIPROTO_REQ_DRM_KAE, .size = 21, .func = handler_drm_KAE },
	{ .id = WIIPROTO_REQ_DRM_KIE, .size = 21, .func = handler_drm_KIE },
	{ .id = WIIPROTO_REQ_DRM_KAIE, .size = 21, .func = handler_drm_KAIE },
	{ .id = WIIPROTO_REQ_DRM_E, .size = 21, .func = handler_drm_E },
	{ .id = WIIPROTO_REQ_DRM_SKAI1, .size = 21, .func = handler_drm_SKAI1 },
	{ .id = WIIPROTO_REQ_DRM_SKAI2, .size = 21, .func = handler_drm_SKAI2 },
	{ .id = 0 }
};

static int wiimote_hid_event(struct hid_device *hdev, struct hid_report *report,
							u8 *raw_data, int size)
{
	struct wiimote_data *wdata = hid_get_drvdata(hdev);
	struct wiiproto_handler *h;
	int i;
	unsigned long flags;
	bool handled = false;

	if (size < 1)
		return -EINVAL;

	spin_lock_irqsave(&wdata->state.lock, flags);

	for (i = 0; handlers[i].id; ++i) {
		h = &handlers[i];
		if (h->id == raw_data[0] && h->size < size) {
			h->func(wdata, &raw_data[1]);
			handled = true;
		}
	}

	if (!handled)
		hid_warn(hdev, "Unhandled report %hhu size %d\n", raw_data[0],
									size);

	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;
}

static void wiimote_leds_destroy(struct wiimote_data *wdata)
{
	int i;
	struct led_classdev *led;

	for (i = 0; i < 4; ++i) {
		if (wdata->leds[i]) {
			led = wdata->leds[i];
			wdata->leds[i] = NULL;
			led_classdev_unregister(led);
			kfree(led);
		}
	}
}

static int wiimote_leds_create(struct wiimote_data *wdata)
{
	int i, ret;
	struct device *dev = &wdata->hdev->dev;
	size_t namesz = strlen(dev_name(dev)) + 9;
	struct led_classdev *led;
	char *name;

	for (i = 0; i < 4; ++i) {
		led = kzalloc(sizeof(struct led_classdev) + namesz, GFP_KERNEL);
		if (!led) {
			ret = -ENOMEM;
			goto err;
		}
		name = (void*)&led[1];
		snprintf(name, namesz, "%s:blue:p%d", dev_name(dev), i);
		led->name = name;
		led->brightness = 0;
		led->max_brightness = 1;
		led->brightness_get = wiimote_leds_get;
		led->brightness_set = wiimote_leds_set;

		ret = led_classdev_register(dev, led);
		if (ret) {
			kfree(led);
			goto err;
		}
		wdata->leds[i] = led;
	}

	return 0;

err:
	wiimote_leds_destroy(wdata);
	return ret;
}

static struct wiimote_data *wiimote_create(struct hid_device *hdev)
{
	struct wiimote_data *wdata;
	int i;

	wdata = kzalloc(sizeof(*wdata), GFP_KERNEL);
	if (!wdata)
		return NULL;

	wdata->input = input_allocate_device();
	if (!wdata->input)
		goto err;

	wdata->hdev = hdev;
	hid_set_drvdata(hdev, wdata);

	input_set_drvdata(wdata->input, wdata);
	wdata->input->open = wiimote_input_open;
	wdata->input->close = wiimote_input_close;
	wdata->input->dev.parent = &wdata->hdev->dev;
	wdata->input->id.bustype = wdata->hdev->bus;
	wdata->input->id.vendor = wdata->hdev->vendor;
	wdata->input->id.product = wdata->hdev->product;
	wdata->input->id.version = wdata->hdev->version;
	wdata->input->name = WIIMOTE_NAME;

	set_bit(EV_KEY, wdata->input->evbit);
	for (i = 0; i < WIIPROTO_KEY_COUNT; ++i)
		set_bit(wiiproto_keymap[i], wdata->input->keybit);

	set_bit(FF_RUMBLE, wdata->input->ffbit);
	if (input_ff_create_memless(wdata->input, NULL, wiimote_ff_play))
		goto err_input;

	wdata->accel = input_allocate_device();
	if (!wdata->accel)
		goto err_input;

	input_set_drvdata(wdata->accel, wdata);
	wdata->accel->open = wiimote_accel_open;
	wdata->accel->close = wiimote_accel_close;
	wdata->accel->dev.parent = &wdata->hdev->dev;
	wdata->accel->id.bustype = wdata->hdev->bus;
	wdata->accel->id.vendor = wdata->hdev->vendor;
	wdata->accel->id.product = wdata->hdev->product;
	wdata->accel->id.version = wdata->hdev->version;
	wdata->accel->name = WIIMOTE_NAME " Accelerometer";

	set_bit(EV_ABS, wdata->accel->evbit);
	set_bit(ABS_RX, wdata->accel->absbit);
	set_bit(ABS_RY, wdata->accel->absbit);
	set_bit(ABS_RZ, wdata->accel->absbit);
	input_set_abs_params(wdata->accel, ABS_RX, -500, 500, 2, 4);
	input_set_abs_params(wdata->accel, ABS_RY, -500, 500, 2, 4);
	input_set_abs_params(wdata->accel, ABS_RZ, -500, 500, 2, 4);

	wdata->ir = input_allocate_device();
	if (!wdata->ir)
		goto err_ir;

	input_set_drvdata(wdata->ir, wdata);
	wdata->ir->open = wiimote_ir_open;
	wdata->ir->close = wiimote_ir_close;
	wdata->ir->dev.parent = &wdata->hdev->dev;
	wdata->ir->id.bustype = wdata->hdev->bus;
	wdata->ir->id.vendor = wdata->hdev->vendor;
	wdata->ir->id.product = wdata->hdev->product;
	wdata->ir->id.version = wdata->hdev->version;
	wdata->ir->name = WIIMOTE_NAME " IR";

	set_bit(EV_ABS, wdata->ir->evbit);
	set_bit(ABS_HAT0X, wdata->ir->absbit);
	set_bit(ABS_HAT0Y, wdata->ir->absbit);
	set_bit(ABS_HAT1X, wdata->ir->absbit);
	set_bit(ABS_HAT1Y, wdata->ir->absbit);
	set_bit(ABS_HAT2X, wdata->ir->absbit);
	set_bit(ABS_HAT2Y, wdata->ir->absbit);
	set_bit(ABS_HAT3X, wdata->ir->absbit);
	set_bit(ABS_HAT3Y, wdata->ir->absbit);
	input_set_abs_params(wdata->ir, ABS_HAT0X, 0, 1023, 2, 4);
	input_set_abs_params(wdata->ir, ABS_HAT0Y, 0, 767, 2, 4);
	input_set_abs_params(wdata->ir, ABS_HAT1X, 0, 1023, 2, 4);
	input_set_abs_params(wdata->ir, ABS_HAT1Y, 0, 767, 2, 4);
	input_set_abs_params(wdata->ir, ABS_HAT2X, 0, 1023, 2, 4);
	input_set_abs_params(wdata->ir, ABS_HAT2Y, 0, 767, 2, 4);
	input_set_abs_params(wdata->ir, ABS_HAT3X, 0, 1023, 2, 4);
	input_set_abs_params(wdata->ir, ABS_HAT3Y, 0, 767, 2, 4);

	spin_lock_init(&wdata->qlock);
	INIT_WORK(&wdata->worker, wiimote_worker);

	spin_lock_init(&wdata->state.lock);
	init_completion(&wdata->state.ready);
	mutex_init(&wdata->state.sync);
	wdata->state.drm = WIIPROTO_REQ_DRM_K;

	return wdata;

err_ir:
	input_free_device(wdata->accel);
err_input:
	input_free_device(wdata->input);
err:
	kfree(wdata);
	return NULL;
}

static void wiimote_destroy(struct wiimote_data *wdata)
{
	wiidebug_deinit(wdata);
	wiiext_deinit(wdata);
	wiimote_leds_destroy(wdata);

	power_supply_unregister(&wdata->battery);
	input_unregister_device(wdata->accel);
	input_unregister_device(wdata->ir);
	input_unregister_device(wdata->input);
	cancel_work_sync(&wdata->worker);
	hid_hw_stop(wdata->hdev);

	kfree(wdata);
}

static int wiimote_hid_probe(struct hid_device *hdev,
				const struct hid_device_id *id)
{
	struct wiimote_data *wdata;
	int ret;

	hdev->quirks |= HID_QUIRK_NO_INIT_REPORTS;

	wdata = wiimote_create(hdev);
	if (!wdata) {
		hid_err(hdev, "Can't alloc device\n");
		return -ENOMEM;
	}

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

	ret = input_register_device(wdata->accel);
	if (ret) {
		hid_err(hdev, "Cannot register input device\n");
		goto err_stop;
	}

	ret = input_register_device(wdata->ir);
	if (ret) {
		hid_err(hdev, "Cannot register input device\n");
		goto err_ir;
	}

	ret = input_register_device(wdata->input);
	if (ret) {
		hid_err(hdev, "Cannot register input device\n");
		goto err_input;
	}

	wdata->battery.properties = wiimote_battery_props;
	wdata->battery.num_properties = ARRAY_SIZE(wiimote_battery_props);
	wdata->battery.get_property = wiimote_battery_get_property;
	wdata->battery.name = "wiimote_battery";
	wdata->battery.type = POWER_SUPPLY_TYPE_BATTERY;
	wdata->battery.use_for_apm = 0;

	ret = power_supply_register(&wdata->hdev->dev, &wdata->battery);
	if (ret) {
		hid_err(hdev, "Cannot register battery device\n");
		goto err_battery;
	}

	power_supply_powers(&wdata->battery, &hdev->dev);

	ret = wiimote_leds_create(wdata);
	if (ret)
		goto err_free;

	ret = wiiext_init(wdata);
	if (ret)
		goto err_free;

	ret = wiidebug_init(wdata);
	if (ret)
		goto err_free;

	hid_info(hdev, "New device registered\n");

	/* by default set led1 after device initialization */
	spin_lock_irq(&wdata->state.lock);
	wiiproto_req_leds(wdata, WIIPROTO_FLAG_LED1);
	spin_unlock_irq(&wdata->state.lock);

	return 0;

err_free:
	wiimote_destroy(wdata);
	return ret;

err_battery:
	input_unregister_device(wdata->input);
	wdata->input = NULL;
err_input:
	input_unregister_device(wdata->ir);
	wdata->ir = NULL;
err_ir:
	input_unregister_device(wdata->accel);
	wdata->accel = NULL;
err_stop:
	hid_hw_stop(hdev);
err:
	input_free_device(wdata->ir);
	input_free_device(wdata->accel);
	input_free_device(wdata->input);
	kfree(wdata);
	return ret;
}

static void wiimote_hid_remove(struct hid_device *hdev)
{
	struct wiimote_data *wdata = hid_get_drvdata(hdev);

	hid_info(hdev, "Device removed\n");
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
module_hid_driver(wiimote_hid_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Herrmann <dh.herrmann@gmail.com>");
MODULE_DESCRIPTION(WIIMOTE_NAME " Device Driver");
