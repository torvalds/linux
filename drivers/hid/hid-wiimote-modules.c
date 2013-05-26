/*
 * Device Modules for Nintendo Wii / Wii U HID Driver
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*
 * Wiimote Modules
 * Nintendo devices provide different peripherals and many new devices lack
 * initial features like the IR camera. Therefore, each peripheral device is
 * implemented as an independent module and we probe on each device only the
 * modules for the hardware that really is available.
 *
 * Module registration is sequential. Unregistration is done in reverse order.
 * After device detection, the needed modules are loaded. Users can trigger
 * re-detection which causes all modules to be unloaded and then reload the
 * modules for the new detected device.
 *
 * wdata->input is a shared input device. It is always initialized prior to
 * module registration. If at least one registered module is marked as
 * WIIMOD_FLAG_INPUT, then the input device will get registered after all
 * modules were registered.
 * Please note that it is unregistered _before_ the "remove" callbacks are
 * called. This guarantees that no input interaction is done, anymore. However,
 * the wiimote core keeps a reference to the input device so it is freed only
 * after all modules were removed. It is safe to send events to unregistered
 * input devices.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/spinlock.h>
#include "hid-wiimote.h"

/*
 * Keys
 * The initial Wii Remote provided a bunch of buttons that are reported as
 * part of the core protocol. Many later devices dropped these and report
 * invalid data in the core button reports. Load this only on devices which
 * correctly send button reports.
 * It uses the shared input device.
 */

static const __u16 wiimod_keys_map[] = {
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

static void wiimod_keys_in_keys(struct wiimote_data *wdata, const __u8 *keys)
{
	input_report_key(wdata->input, wiimod_keys_map[WIIPROTO_KEY_LEFT],
							!!(keys[0] & 0x01));
	input_report_key(wdata->input, wiimod_keys_map[WIIPROTO_KEY_RIGHT],
							!!(keys[0] & 0x02));
	input_report_key(wdata->input, wiimod_keys_map[WIIPROTO_KEY_DOWN],
							!!(keys[0] & 0x04));
	input_report_key(wdata->input, wiimod_keys_map[WIIPROTO_KEY_UP],
							!!(keys[0] & 0x08));
	input_report_key(wdata->input, wiimod_keys_map[WIIPROTO_KEY_PLUS],
							!!(keys[0] & 0x10));
	input_report_key(wdata->input, wiimod_keys_map[WIIPROTO_KEY_TWO],
							!!(keys[1] & 0x01));
	input_report_key(wdata->input, wiimod_keys_map[WIIPROTO_KEY_ONE],
							!!(keys[1] & 0x02));
	input_report_key(wdata->input, wiimod_keys_map[WIIPROTO_KEY_B],
							!!(keys[1] & 0x04));
	input_report_key(wdata->input, wiimod_keys_map[WIIPROTO_KEY_A],
							!!(keys[1] & 0x08));
	input_report_key(wdata->input, wiimod_keys_map[WIIPROTO_KEY_MINUS],
							!!(keys[1] & 0x10));
	input_report_key(wdata->input, wiimod_keys_map[WIIPROTO_KEY_HOME],
							!!(keys[1] & 0x80));
	input_sync(wdata->input);
}

static int wiimod_keys_probe(const struct wiimod_ops *ops,
			     struct wiimote_data *wdata)
{
	unsigned int i;

	set_bit(EV_KEY, wdata->input->evbit);
	for (i = 0; i < WIIPROTO_KEY_COUNT; ++i)
		set_bit(wiimod_keys_map[i], wdata->input->keybit);

	return 0;
}

static const struct wiimod_ops wiimod_keys = {
	.flags = WIIMOD_FLAG_INPUT,
	.arg = 0,
	.probe = wiimod_keys_probe,
	.remove = NULL,
	.in_keys = wiimod_keys_in_keys,
};

/*
 * Rumble
 * Nearly all devices provide a rumble feature. A small motor for
 * force-feedback effects. We provide an FF_RUMBLE memless ff device on the
 * shared input device if this module is loaded.
 * The rumble motor is controlled via a flag on almost every output report so
 * the wiimote core handles the rumble flag. But if a device doesn't provide
 * the rumble motor, this flag shouldn't be set.
 */

static int wiimod_rumble_play(struct input_dev *dev, void *data,
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

static int wiimod_rumble_probe(const struct wiimod_ops *ops,
			       struct wiimote_data *wdata)
{
	set_bit(FF_RUMBLE, wdata->input->ffbit);
	if (input_ff_create_memless(wdata->input, NULL, wiimod_rumble_play))
		return -ENOMEM;

	return 0;
}

static void wiimod_rumble_remove(const struct wiimod_ops *ops,
				 struct wiimote_data *wdata)
{
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wiiproto_req_rumble(wdata, 0);
	spin_unlock_irqrestore(&wdata->state.lock, flags);
}

static const struct wiimod_ops wiimod_rumble = {
	.flags = WIIMOD_FLAG_INPUT,
	.arg = 0,
	.probe = wiimod_rumble_probe,
	.remove = wiimod_rumble_remove,
};

/*
 * Battery
 * 1 byte of battery capacity information is sent along every protocol status
 * report. The wiimote core caches it but we try to update it on every
 * user-space request.
 * This is supported by nearly every device so it's almost always enabled.
 */

static enum power_supply_property wiimod_battery_props[] = {
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_SCOPE,
};

static int wiimod_battery_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct wiimote_data *wdata = container_of(psy, struct wiimote_data,
						  battery);
	int ret = 0, state;
	unsigned long flags;

	if (psp == POWER_SUPPLY_PROP_SCOPE) {
		val->intval = POWER_SUPPLY_SCOPE_DEVICE;
		return 0;
	} else if (psp != POWER_SUPPLY_PROP_CAPACITY) {
		return -EINVAL;
	}

	ret = wiimote_cmd_acquire(wdata);
	if (ret)
		return ret;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wiimote_cmd_set(wdata, WIIPROTO_REQ_SREQ, 0);
	wiiproto_req_status(wdata);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	wiimote_cmd_wait(wdata);
	wiimote_cmd_release(wdata);

	spin_lock_irqsave(&wdata->state.lock, flags);
	state = wdata->state.cmd_battery;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	val->intval = state * 100 / 255;
	return ret;
}

static int wiimod_battery_probe(const struct wiimod_ops *ops,
				struct wiimote_data *wdata)
{
	int ret;

	wdata->battery.properties = wiimod_battery_props;
	wdata->battery.num_properties = ARRAY_SIZE(wiimod_battery_props);
	wdata->battery.get_property = wiimod_battery_get_property;
	wdata->battery.type = POWER_SUPPLY_TYPE_BATTERY;
	wdata->battery.use_for_apm = 0;
	wdata->battery.name = kasprintf(GFP_KERNEL, "wiimote_battery_%s",
					wdata->hdev->uniq);
	if (!wdata->battery.name)
		return -ENOMEM;

	ret = power_supply_register(&wdata->hdev->dev, &wdata->battery);
	if (ret) {
		hid_err(wdata->hdev, "cannot register battery device\n");
		goto err_free;
	}

	power_supply_powers(&wdata->battery, &wdata->hdev->dev);
	return 0;

err_free:
	kfree(wdata->battery.name);
	wdata->battery.name = NULL;
	return ret;
}

static void wiimod_battery_remove(const struct wiimod_ops *ops,
				  struct wiimote_data *wdata)
{
	if (!wdata->battery.name)
		return;

	power_supply_unregister(&wdata->battery);
	kfree(wdata->battery.name);
	wdata->battery.name = NULL;
}

static const struct wiimod_ops wiimod_battery = {
	.flags = 0,
	.arg = 0,
	.probe = wiimod_battery_probe,
	.remove = wiimod_battery_remove,
};

/*
 * LED
 * 0 to 4 player LEDs are supported by devices. The "arg" field of the
 * wiimod_ops structure specifies which LED this module controls. This allows
 * to register a limited number of LEDs.
 * State is managed by wiimote core.
 */

static enum led_brightness wiimod_led_get(struct led_classdev *led_dev)
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

static void wiimod_led_set(struct led_classdev *led_dev,
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

static int wiimod_led_probe(const struct wiimod_ops *ops,
			    struct wiimote_data *wdata)
{
	struct device *dev = &wdata->hdev->dev;
	size_t namesz = strlen(dev_name(dev)) + 9;
	struct led_classdev *led;
	unsigned long flags;
	char *name;
	int ret;

	led = kzalloc(sizeof(struct led_classdev) + namesz, GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	name = (void*)&led[1];
	snprintf(name, namesz, "%s:blue:p%lu", dev_name(dev), ops->arg);
	led->name = name;
	led->brightness = 0;
	led->max_brightness = 1;
	led->brightness_get = wiimod_led_get;
	led->brightness_set = wiimod_led_set;

	wdata->leds[ops->arg] = led;
	ret = led_classdev_register(dev, led);
	if (ret)
		goto err_free;

	/* enable LED1 to stop initial LED-blinking */
	if (ops->arg == 0) {
		spin_lock_irqsave(&wdata->state.lock, flags);
		wiiproto_req_leds(wdata, WIIPROTO_FLAG_LED1);
		spin_unlock_irqrestore(&wdata->state.lock, flags);
	}

	return 0;

err_free:
	wdata->leds[ops->arg] = NULL;
	kfree(led);
	return ret;
}

static void wiimod_led_remove(const struct wiimod_ops *ops,
			      struct wiimote_data *wdata)
{
	if (!wdata->leds[ops->arg])
		return;

	led_classdev_unregister(wdata->leds[ops->arg]);
	kfree(wdata->leds[ops->arg]);
	wdata->leds[ops->arg] = NULL;
}

static const struct wiimod_ops wiimod_leds[4] = {
	{
		.flags = 0,
		.arg = 0,
		.probe = wiimod_led_probe,
		.remove = wiimod_led_remove,
	},
	{
		.flags = 0,
		.arg = 1,
		.probe = wiimod_led_probe,
		.remove = wiimod_led_remove,
	},
	{
		.flags = 0,
		.arg = 2,
		.probe = wiimod_led_probe,
		.remove = wiimod_led_remove,
	},
	{
		.flags = 0,
		.arg = 3,
		.probe = wiimod_led_probe,
		.remove = wiimod_led_remove,
	},
};

/*
 * Accelerometer
 * 3 axis accelerometer data is part of nearly all DRMs. If not supported by a
 * device, it's mostly cleared to 0. This module parses this data and provides
 * it via a separate input device.
 */

static void wiimod_accel_in_accel(struct wiimote_data *wdata,
				  const __u8 *accel)
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

	x = accel[2] << 2;
	y = accel[3] << 2;
	z = accel[4] << 2;

	x |= (accel[0] >> 5) & 0x3;
	y |= (accel[1] >> 4) & 0x2;
	z |= (accel[1] >> 5) & 0x2;

	input_report_abs(wdata->accel, ABS_RX, x - 0x200);
	input_report_abs(wdata->accel, ABS_RY, y - 0x200);
	input_report_abs(wdata->accel, ABS_RZ, z - 0x200);
	input_sync(wdata->accel);
}

static int wiimod_accel_open(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wiiproto_req_accel(wdata, true);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;
}

static void wiimod_accel_close(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wiiproto_req_accel(wdata, false);
	spin_unlock_irqrestore(&wdata->state.lock, flags);
}

static int wiimod_accel_probe(const struct wiimod_ops *ops,
			      struct wiimote_data *wdata)
{
	int ret;

	wdata->accel = input_allocate_device();
	if (!wdata->accel)
		return -ENOMEM;

	input_set_drvdata(wdata->accel, wdata);
	wdata->accel->open = wiimod_accel_open;
	wdata->accel->close = wiimod_accel_close;
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

	ret = input_register_device(wdata->accel);
	if (ret) {
		hid_err(wdata->hdev, "cannot register input device\n");
		goto err_free;
	}

	return 0;

err_free:
	input_free_device(wdata->accel);
	wdata->accel = NULL;
	return ret;
}

static void wiimod_accel_remove(const struct wiimod_ops *ops,
				struct wiimote_data *wdata)
{
	if (!wdata->accel)
		return;

	input_unregister_device(wdata->accel);
	wdata->accel = NULL;
}

static const struct wiimod_ops wiimod_accel = {
	.flags = 0,
	.arg = 0,
	.probe = wiimod_accel_probe,
	.remove = wiimod_accel_remove,
	.in_accel = wiimod_accel_in_accel,
};

/*
 * IR Cam
 * Up to 4 IR sources can be tracked by a normal Wii Remote. The IR cam needs
 * to be initialized with a fairly complex procedure and consumes a lot of
 * power. Therefore, as long as no application uses the IR input device, it is
 * kept offline.
 * Nearly no other device than the normal Wii Remotes supports the IR cam so
 * you can disable this module for these devices.
 */

static void wiimod_ir_in_ir(struct wiimote_data *wdata, const __u8 *ir,
			    bool packed, unsigned int id)
{
	__u16 x, y;
	__u8 xid, yid;
	bool sync = false;

	if (!(wdata->state.flags & WIIPROTO_FLAGS_IR))
		return;

	switch (id) {
	case 0:
		xid = ABS_HAT0X;
		yid = ABS_HAT0Y;
		break;
	case 1:
		xid = ABS_HAT1X;
		yid = ABS_HAT1Y;
		break;
	case 2:
		xid = ABS_HAT2X;
		yid = ABS_HAT2Y;
		break;
	case 3:
		xid = ABS_HAT3X;
		yid = ABS_HAT3Y;
		sync = true;
		break;
	default:
		return;
	};

	/*
	 * Basic IR data is encoded into 3 bytes. The first two bytes are the
	 * lower 8 bit of the X/Y data, the 3rd byte contains the upper 2 bits
	 * of both.
	 * If data is packed, then the 3rd byte is put first and slightly
	 * reordered. This allows to interleave packed and non-packed data to
	 * have two IR sets in 5 bytes instead of 6.
	 * The resulting 10bit X/Y values are passed to the ABS_HAT? input dev.
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

	if (sync)
		input_sync(wdata->ir);
}

static int wiimod_ir_change(struct wiimote_data *wdata, __u16 mode)
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

static int wiimod_ir_open(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);

	return wiimod_ir_change(wdata, WIIPROTO_FLAG_IR_BASIC);
}

static void wiimod_ir_close(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);

	wiimod_ir_change(wdata, 0);
}

static int wiimod_ir_probe(const struct wiimod_ops *ops,
			   struct wiimote_data *wdata)
{
	int ret;

	wdata->ir = input_allocate_device();
	if (!wdata->ir)
		return -ENOMEM;

	input_set_drvdata(wdata->ir, wdata);
	wdata->ir->open = wiimod_ir_open;
	wdata->ir->close = wiimod_ir_close;
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

	ret = input_register_device(wdata->ir);
	if (ret) {
		hid_err(wdata->hdev, "cannot register input device\n");
		goto err_free;
	}

	return 0;

err_free:
	input_free_device(wdata->ir);
	wdata->ir = NULL;
	return ret;
}

static void wiimod_ir_remove(const struct wiimod_ops *ops,
			     struct wiimote_data *wdata)
{
	if (!wdata->ir)
		return;

	input_unregister_device(wdata->ir);
	wdata->ir = NULL;
}

static const struct wiimod_ops wiimod_ir = {
	.flags = 0,
	.arg = 0,
	.probe = wiimod_ir_probe,
	.remove = wiimod_ir_remove,
	.in_ir = wiimod_ir_in_ir,
};

/*
 * Nunchuk Extension
 * The Nintendo Wii Nunchuk was the first official extension published by
 * Nintendo. It provides two additional keys and a separate accelerometer. It
 * can be hotplugged to standard Wii Remotes.
 */

enum wiimod_nunchuk_keys {
	WIIMOD_NUNCHUK_KEY_C,
	WIIMOD_NUNCHUK_KEY_Z,
	WIIMOD_NUNCHUK_KEY_NUM,
};

static const __u16 wiimod_nunchuk_map[] = {
	BTN_C,		/* WIIMOD_NUNCHUK_KEY_C */
	BTN_Z,		/* WIIMOD_NUNCHUK_KEY_Z */
};

static void wiimod_nunchuk_in_ext(struct wiimote_data *wdata, const __u8 *ext)
{
	__s16 x, y, z, bx, by;

	/*   Byte |   8    7 |  6    5 |  4    3 |  2 |  1  |
	 *   -----+----------+---------+---------+----+-----+
	 *    1   |              Button X <7:0>             |
	 *    2   |              Button Y <7:0>             |
	 *   -----+----------+---------+---------+----+-----+
	 *    3   |               Speed X <9:2>             |
	 *    4   |               Speed Y <9:2>             |
	 *    5   |               Speed Z <9:2>             |
	 *   -----+----------+---------+---------+----+-----+
	 *    6   | Z <1:0>  | Y <1:0> | X <1:0> | BC | BZ  |
	 *   -----+----------+---------+---------+----+-----+
	 * Button X/Y is the analog stick. Speed X, Y and Z are the
	 * accelerometer data in the same format as the wiimote's accelerometer.
	 * The 6th byte contains the LSBs of the accelerometer data.
	 * BC and BZ are the C and Z buttons: 0 means pressed
	 *
	 * If reported interleaved with motionp, then the layout changes. The
	 * 5th and 6th byte changes to:
	 *   -----+-----------------------------------+-----+
	 *    5   |            Speed Z <9:3>          | EXT |
	 *   -----+--------+-----+-----+----+----+----+-----+
	 *    6   |Z <2:1> |Y <1>|X <1>| BC | BZ | 0  |  0  |
	 *   -----+--------+-----+-----+----+----+----+-----+
	 * All three accelerometer values lose their LSB. The other data is
	 * still available but slightly moved.
	 *
	 * Center data for button values is 128. Center value for accelerometer
	 * values it 512 / 0x200
	 */

	bx = ext[0];
	by = ext[1];
	bx -= 128;
	by -= 128;

	x = ext[2] << 2;
	y = ext[3] << 2;
	z = ext[4] << 2;

	if (wdata->state.flags & WIIPROTO_FLAG_MP_ACTIVE) {
		x |= (ext[5] >> 3) & 0x02;
		y |= (ext[5] >> 4) & 0x02;
		z &= ~0x4;
		z |= (ext[5] >> 5) & 0x06;
	} else {
		x |= (ext[5] >> 2) & 0x03;
		y |= (ext[5] >> 4) & 0x03;
		z |= (ext[5] >> 6) & 0x03;
	}

	x -= 0x200;
	y -= 0x200;
	z -= 0x200;

	input_report_abs(wdata->extension.input, ABS_HAT0X, bx);
	input_report_abs(wdata->extension.input, ABS_HAT0Y, by);

	input_report_abs(wdata->extension.input, ABS_RX, x);
	input_report_abs(wdata->extension.input, ABS_RY, y);
	input_report_abs(wdata->extension.input, ABS_RZ, z);

	if (wdata->state.flags & WIIPROTO_FLAG_MP_ACTIVE) {
		input_report_key(wdata->extension.input,
			wiimod_nunchuk_map[WIIMOD_NUNCHUK_KEY_Z],
			!(ext[5] & 0x04));
		input_report_key(wdata->extension.input,
			wiimod_nunchuk_map[WIIMOD_NUNCHUK_KEY_C],
			!(ext[5] & 0x08));
	} else {
		input_report_key(wdata->extension.input,
			wiimod_nunchuk_map[WIIMOD_NUNCHUK_KEY_Z],
			!(ext[5] & 0x01));
		input_report_key(wdata->extension.input,
			wiimod_nunchuk_map[WIIMOD_NUNCHUK_KEY_C],
			!(ext[5] & 0x02));
	}

	input_sync(wdata->extension.input);
}

static int wiimod_nunchuk_open(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.flags |= WIIPROTO_FLAG_EXT_USED;
	wiiproto_req_drm(wdata, WIIPROTO_REQ_NULL);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;
}

static void wiimod_nunchuk_close(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.flags &= ~WIIPROTO_FLAG_EXT_USED;
	wiiproto_req_drm(wdata, WIIPROTO_REQ_NULL);
	spin_unlock_irqrestore(&wdata->state.lock, flags);
}

static int wiimod_nunchuk_probe(const struct wiimod_ops *ops,
				struct wiimote_data *wdata)
{
	int ret, i;

	wdata->extension.input = input_allocate_device();
	if (!wdata->extension.input)
		return -ENOMEM;

	input_set_drvdata(wdata->extension.input, wdata);
	wdata->extension.input->open = wiimod_nunchuk_open;
	wdata->extension.input->close = wiimod_nunchuk_close;
	wdata->extension.input->dev.parent = &wdata->hdev->dev;
	wdata->extension.input->id.bustype = wdata->hdev->bus;
	wdata->extension.input->id.vendor = wdata->hdev->vendor;
	wdata->extension.input->id.product = wdata->hdev->product;
	wdata->extension.input->id.version = wdata->hdev->version;
	wdata->extension.input->name = WIIMOTE_NAME " Nunchuk";

	set_bit(EV_KEY, wdata->extension.input->evbit);
	for (i = 0; i < WIIMOD_NUNCHUK_KEY_NUM; ++i)
		set_bit(wiimod_nunchuk_map[i],
			wdata->extension.input->keybit);

	set_bit(EV_ABS, wdata->extension.input->evbit);
	set_bit(ABS_HAT0X, wdata->extension.input->absbit);
	set_bit(ABS_HAT0Y, wdata->extension.input->absbit);
	input_set_abs_params(wdata->extension.input,
			     ABS_HAT0X, -120, 120, 2, 4);
	input_set_abs_params(wdata->extension.input,
			     ABS_HAT0Y, -120, 120, 2, 4);
	set_bit(ABS_RX, wdata->extension.input->absbit);
	set_bit(ABS_RY, wdata->extension.input->absbit);
	set_bit(ABS_RZ, wdata->extension.input->absbit);
	input_set_abs_params(wdata->extension.input,
			     ABS_RX, -500, 500, 2, 4);
	input_set_abs_params(wdata->extension.input,
			     ABS_RY, -500, 500, 2, 4);
	input_set_abs_params(wdata->extension.input,
			     ABS_RZ, -500, 500, 2, 4);

	ret = input_register_device(wdata->extension.input);
	if (ret)
		goto err_free;

	return 0;

err_free:
	input_free_device(wdata->extension.input);
	wdata->extension.input = NULL;
	return ret;
}

static void wiimod_nunchuk_remove(const struct wiimod_ops *ops,
				  struct wiimote_data *wdata)
{
	if (!wdata->extension.input)
		return;

	input_unregister_device(wdata->extension.input);
	wdata->extension.input = NULL;
}

static const struct wiimod_ops wiimod_nunchuk = {
	.flags = 0,
	.arg = 0,
	.probe = wiimod_nunchuk_probe,
	.remove = wiimod_nunchuk_remove,
	.in_ext = wiimod_nunchuk_in_ext,
};

/*
 * Classic Controller
 * Another official extension from Nintendo. It provides a classic
 * gamecube-like controller that can be hotplugged on the Wii Remote.
 * It has several hardware buttons and switches that are all reported via
 * a normal extension device.
 */

enum wiimod_classic_keys {
	WIIMOD_CLASSIC_KEY_A,
	WIIMOD_CLASSIC_KEY_B,
	WIIMOD_CLASSIC_KEY_X,
	WIIMOD_CLASSIC_KEY_Y,
	WIIMOD_CLASSIC_KEY_ZL,
	WIIMOD_CLASSIC_KEY_ZR,
	WIIMOD_CLASSIC_KEY_PLUS,
	WIIMOD_CLASSIC_KEY_MINUS,
	WIIMOD_CLASSIC_KEY_HOME,
	WIIMOD_CLASSIC_KEY_LEFT,
	WIIMOD_CLASSIC_KEY_RIGHT,
	WIIMOD_CLASSIC_KEY_UP,
	WIIMOD_CLASSIC_KEY_DOWN,
	WIIMOD_CLASSIC_KEY_LT,
	WIIMOD_CLASSIC_KEY_RT,
	WIIMOD_CLASSIC_KEY_NUM,
};

static const __u16 wiimod_classic_map[] = {
	BTN_A,		/* WIIMOD_CLASSIC_KEY_A */
	BTN_B,		/* WIIMOD_CLASSIC_KEY_B */
	BTN_X,		/* WIIMOD_CLASSIC_KEY_X */
	BTN_Y,		/* WIIMOD_CLASSIC_KEY_Y */
	BTN_TL2,	/* WIIMOD_CLASSIC_KEY_ZL */
	BTN_TR2,	/* WIIMOD_CLASSIC_KEY_ZR */
	KEY_NEXT,	/* WIIMOD_CLASSIC_KEY_PLUS */
	KEY_PREVIOUS,	/* WIIMOD_CLASSIC_KEY_MINUS */
	BTN_MODE,	/* WIIMOD_CLASSIC_KEY_HOME */
	KEY_LEFT,	/* WIIMOD_CLASSIC_KEY_LEFT */
	KEY_RIGHT,	/* WIIMOD_CLASSIC_KEY_RIGHT */
	KEY_UP,		/* WIIMOD_CLASSIC_KEY_UP */
	KEY_DOWN,	/* WIIMOD_CLASSIC_KEY_DOWN */
	BTN_TL,		/* WIIMOD_CLASSIC_KEY_LT */
	BTN_TR,		/* WIIMOD_CLASSIC_KEY_RT */
};

static void wiimod_classic_in_ext(struct wiimote_data *wdata, const __u8 *ext)
{
	__s8 rx, ry, lx, ly, lt, rt;

	/*   Byte |  8  |  7  |  6  |  5  |  4  |  3  |  2  |  1  |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 *    1   | RX <5:4>  |              LX <5:0>             |
	 *    2   | RX <3:2>  |              LY <5:0>             |
	 *   -----+-----+-----+-----+-----------------------------+
	 *    3   |RX<1>| LT <5:4>  |         RY <5:1>            |
	 *   -----+-----+-----------+-----------------------------+
	 *    4   |     LT <3:1>    |         RT <5:1>            |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 *    5   | BDR | BDD | BLT | B-  | BH  | B+  | BRT |  1  |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 *    6   | BZL | BB  | BY  | BA  | BX  | BZR | BDL | BDU |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 * All buttons are 0 if pressed
	 * RX and RY are right analog stick
	 * LX and LY are left analog stick
	 * LT is left trigger, RT is right trigger
	 * BLT is 0 if left trigger is fully pressed
	 * BRT is 0 if right trigger is fully pressed
	 * BDR, BDD, BDL, BDU form the D-Pad with right, down, left, up buttons
	 * BZL is left Z button and BZR is right Z button
	 * B-, BH, B+ are +, HOME and - buttons
	 * BB, BY, BA, BX are A, B, X, Y buttons
	 * LSB of RX, RY, LT, and RT are not transmitted and always 0.
	 *
	 * With motionp enabled it changes slightly to this:
	 *   Byte |  8  |  7  |  6  |  5  |  4  |  3  |  2  |  1  |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 *    1   | RX <5:4>  |          LX <5:1>           | BDU |
	 *    2   | RX <3:2>  |          LY <5:1>           | BDL |
	 *   -----+-----+-----+-----+-----------------------+-----+
	 *    3   |RX<1>| LT <5:4>  |         RY <5:1>            |
	 *   -----+-----+-----------+-----------------------------+
	 *    4   |     LT <3:1>    |         RT <5:1>            |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 *    5   | BDR | BDD | BLT | B-  | BH  | B+  | BRT | EXT |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 *    6   | BZL | BB  | BY  | BA  | BX  | BZR |  0  |  0  |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 * Only the LSBs of LX and LY are lost. BDU and BDL are moved, the rest
	 * is the same as before.
	 */

	if (wdata->state.flags & WIIPROTO_FLAG_MP_ACTIVE) {
		lx = ext[0] & 0x3e;
		ly = ext[1] & 0x3e;
	} else {
		lx = ext[0] & 0x3f;
		ly = ext[1] & 0x3f;
	}

	rx = (ext[0] >> 3) & 0x18;
	rx |= (ext[1] >> 5) & 0x06;
	rx |= (ext[2] >> 7) & 0x01;
	ry = ext[2] & 0x1f;

	rt = ext[3] & 0x1f;
	lt = (ext[2] >> 2) & 0x18;
	lt |= (ext[3] >> 5) & 0x07;

	rx <<= 1;
	ry <<= 1;
	rt <<= 1;
	lt <<= 1;

	input_report_abs(wdata->extension.input, ABS_HAT1X, lx - 0x20);
	input_report_abs(wdata->extension.input, ABS_HAT1Y, ly - 0x20);
	input_report_abs(wdata->extension.input, ABS_HAT2X, rx - 0x20);
	input_report_abs(wdata->extension.input, ABS_HAT2Y, ry - 0x20);
	input_report_abs(wdata->extension.input, ABS_HAT3X, rt);
	input_report_abs(wdata->extension.input, ABS_HAT3Y, lt);

	input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_RIGHT],
			 !(ext[4] & 0x80));
	input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_DOWN],
			 !(ext[4] & 0x40));
	input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_LT],
			 !(ext[4] & 0x20));
	input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_MINUS],
			 !(ext[4] & 0x10));
	input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_HOME],
			 !(ext[4] & 0x08));
	input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_PLUS],
			 !(ext[4] & 0x04));
	input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_RT],
			 !(ext[4] & 0x02));
	input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_ZL],
			 !(ext[5] & 0x80));
	input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_B],
			 !(ext[5] & 0x40));
	input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_Y],
			 !(ext[5] & 0x20));
	input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_A],
			 !(ext[5] & 0x10));
	input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_X],
			 !(ext[5] & 0x08));
	input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_ZR],
			 !(ext[5] & 0x04));

	if (wdata->state.flags & WIIPROTO_FLAG_MP_ACTIVE) {
		input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_LEFT],
			 !(ext[1] & 0x01));
		input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_UP],
			 !(ext[0] & 0x01));
	} else {
		input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_LEFT],
			 !(ext[5] & 0x02));
		input_report_key(wdata->extension.input,
			 wiimod_classic_map[WIIMOD_CLASSIC_KEY_UP],
			 !(ext[5] & 0x01));
	}

	input_sync(wdata->extension.input);
}

static int wiimod_classic_open(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.flags |= WIIPROTO_FLAG_EXT_USED;
	wiiproto_req_drm(wdata, WIIPROTO_REQ_NULL);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;
}

static void wiimod_classic_close(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.flags &= ~WIIPROTO_FLAG_EXT_USED;
	wiiproto_req_drm(wdata, WIIPROTO_REQ_NULL);
	spin_unlock_irqrestore(&wdata->state.lock, flags);
}

static int wiimod_classic_probe(const struct wiimod_ops *ops,
				struct wiimote_data *wdata)
{
	int ret, i;

	wdata->extension.input = input_allocate_device();
	if (!wdata->extension.input)
		return -ENOMEM;

	input_set_drvdata(wdata->extension.input, wdata);
	wdata->extension.input->open = wiimod_classic_open;
	wdata->extension.input->close = wiimod_classic_close;
	wdata->extension.input->dev.parent = &wdata->hdev->dev;
	wdata->extension.input->id.bustype = wdata->hdev->bus;
	wdata->extension.input->id.vendor = wdata->hdev->vendor;
	wdata->extension.input->id.product = wdata->hdev->product;
	wdata->extension.input->id.version = wdata->hdev->version;
	wdata->extension.input->name = WIIMOTE_NAME " Classic Controller";

	set_bit(EV_KEY, wdata->extension.input->evbit);
	for (i = 0; i < WIIMOD_CLASSIC_KEY_NUM; ++i)
		set_bit(wiimod_classic_map[i],
			wdata->extension.input->keybit);

	set_bit(EV_ABS, wdata->extension.input->evbit);
	set_bit(ABS_HAT1X, wdata->extension.input->absbit);
	set_bit(ABS_HAT1Y, wdata->extension.input->absbit);
	set_bit(ABS_HAT2X, wdata->extension.input->absbit);
	set_bit(ABS_HAT2Y, wdata->extension.input->absbit);
	set_bit(ABS_HAT3X, wdata->extension.input->absbit);
	set_bit(ABS_HAT3Y, wdata->extension.input->absbit);
	input_set_abs_params(wdata->extension.input,
			     ABS_HAT1X, -30, 30, 1, 1);
	input_set_abs_params(wdata->extension.input,
			     ABS_HAT1Y, -30, 30, 1, 1);
	input_set_abs_params(wdata->extension.input,
			     ABS_HAT2X, -30, 30, 1, 1);
	input_set_abs_params(wdata->extension.input,
			     ABS_HAT2Y, -30, 30, 1, 1);
	input_set_abs_params(wdata->extension.input,
			     ABS_HAT3X, -30, 30, 1, 1);
	input_set_abs_params(wdata->extension.input,
			     ABS_HAT3Y, -30, 30, 1, 1);

	ret = input_register_device(wdata->extension.input);
	if (ret)
		goto err_free;

	return 0;

err_free:
	input_free_device(wdata->extension.input);
	wdata->extension.input = NULL;
	return ret;
}

static void wiimod_classic_remove(const struct wiimod_ops *ops,
				  struct wiimote_data *wdata)
{
	if (!wdata->extension.input)
		return;

	input_unregister_device(wdata->extension.input);
	wdata->extension.input = NULL;
}

static const struct wiimod_ops wiimod_classic = {
	.flags = 0,
	.arg = 0,
	.probe = wiimod_classic_probe,
	.remove = wiimod_classic_remove,
	.in_ext = wiimod_classic_in_ext,
};

/*
 * Balance Board Extension
 * The Nintendo Wii Balance Board provides four hardware weight sensor plus a
 * single push button. No other peripherals are available. However, the
 * balance-board data is sent via a standard Wii Remote extension. All other
 * data for non-present hardware is zeroed out.
 * Some 3rd party devices react allergic if we try to access normal Wii Remote
 * hardware, so this extension module should be the only module that is loaded
 * on balance boards.
 * The balance board needs 8 bytes extension data instead of basic 6 bytes so
 * it needs the WIIMOD_FLAG_EXT8 flag.
 */

static void wiimod_bboard_in_keys(struct wiimote_data *wdata, const __u8 *keys)
{
	input_report_key(wdata->extension.input, BTN_A,
			 !!(keys[1] & 0x08));
	input_sync(wdata->extension.input);
}

static void wiimod_bboard_in_ext(struct wiimote_data *wdata,
				 const __u8 *ext)
{
	__s32 val[4], tmp, div;
	unsigned int i;
	struct wiimote_state *s = &wdata->state;

	/*
	 * Balance board data layout:
	 *
	 *   Byte |  8  7  6  5  4  3  2  1  |
	 *   -----+--------------------------+
	 *    1   |    Top Right <15:8>      |
	 *    2   |    Top Right  <7:0>      |
	 *   -----+--------------------------+
	 *    3   | Bottom Right <15:8>      |
	 *    4   | Bottom Right  <7:0>      |
	 *   -----+--------------------------+
	 *    5   |     Top Left <15:8>      |
	 *    6   |     Top Left  <7:0>      |
	 *   -----+--------------------------+
	 *    7   |  Bottom Left <15:8>      |
	 *    8   |  Bottom Left  <7:0>      |
	 *   -----+--------------------------+
	 *
	 * These values represent the weight-measurements of the Wii-balance
	 * board with 16bit precision.
	 *
	 * The balance-board is never reported interleaved with motionp.
	 */

	val[0] = ext[0];
	val[0] <<= 8;
	val[0] |= ext[1];

	val[1] = ext[2];
	val[1] <<= 8;
	val[1] |= ext[3];

	val[2] = ext[4];
	val[2] <<= 8;
	val[2] |= ext[5];

	val[3] = ext[6];
	val[3] <<= 8;
	val[3] |= ext[7];

	/* apply calibration data */
	for (i = 0; i < 4; i++) {
		if (val[i] <= s->calib_bboard[i][0]) {
			tmp = 0;
		} else if (val[i] < s->calib_bboard[i][1]) {
			tmp = val[i] - s->calib_bboard[i][0];
			tmp *= 1700;
			div = s->calib_bboard[i][1] - s->calib_bboard[i][0];
			tmp /= div ? div : 1;
		} else {
			tmp = val[i] - s->calib_bboard[i][1];
			tmp *= 1700;
			div = s->calib_bboard[i][2] - s->calib_bboard[i][1];
			tmp /= div ? div : 1;
			tmp += 1700;
		}
		val[i] = tmp;
	}

	input_report_abs(wdata->extension.input, ABS_HAT0X, val[0]);
	input_report_abs(wdata->extension.input, ABS_HAT0Y, val[1]);
	input_report_abs(wdata->extension.input, ABS_HAT1X, val[2]);
	input_report_abs(wdata->extension.input, ABS_HAT1Y, val[3]);
	input_sync(wdata->extension.input);
}

static int wiimod_bboard_open(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.flags |= WIIPROTO_FLAG_EXT_USED;
	wiiproto_req_drm(wdata, WIIPROTO_REQ_NULL);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;
}

static void wiimod_bboard_close(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.flags &= ~WIIPROTO_FLAG_EXT_USED;
	wiiproto_req_drm(wdata, WIIPROTO_REQ_NULL);
	spin_unlock_irqrestore(&wdata->state.lock, flags);
}

static ssize_t wiimod_bboard_calib_show(struct device *dev,
					struct device_attribute *attr,
					char *out)
{
	struct wiimote_data *wdata = dev_to_wii(dev);
	int i, j, ret;
	__u16 val;
	__u8 buf[24], offs;

	ret = wiimote_cmd_acquire(wdata);
	if (ret)
		return ret;

	ret = wiimote_cmd_read(wdata, 0xa40024, buf, 12);
	if (ret != 12) {
		wiimote_cmd_release(wdata);
		return ret < 0 ? ret : -EIO;
	}
	ret = wiimote_cmd_read(wdata, 0xa40024 + 12, buf + 12, 12);
	if (ret != 12) {
		wiimote_cmd_release(wdata);
		return ret < 0 ? ret : -EIO;
	}

	wiimote_cmd_release(wdata);

	spin_lock_irq(&wdata->state.lock);
	offs = 0;
	for (i = 0; i < 3; ++i) {
		for (j = 0; j < 4; ++j) {
			wdata->state.calib_bboard[j][i] = buf[offs];
			wdata->state.calib_bboard[j][i] <<= 8;
			wdata->state.calib_bboard[j][i] |= buf[offs + 1];
			offs += 2;
		}
	}
	spin_unlock_irq(&wdata->state.lock);

	ret = 0;
	for (i = 0; i < 3; ++i) {
		for (j = 0; j < 4; ++j) {
			val = wdata->state.calib_bboard[j][i];
			if (i == 2 && j == 3)
				ret += sprintf(&out[ret], "%04x\n", val);
			else
				ret += sprintf(&out[ret], "%04x:", val);
		}
	}

	return ret;
}

static DEVICE_ATTR(bboard_calib, S_IRUGO, wiimod_bboard_calib_show, NULL);

static int wiimod_bboard_probe(const struct wiimod_ops *ops,
			       struct wiimote_data *wdata)
{
	int ret, i, j;
	__u8 buf[24], offs;

	wiimote_cmd_acquire_noint(wdata);

	ret = wiimote_cmd_read(wdata, 0xa40024, buf, 12);
	if (ret != 12) {
		wiimote_cmd_release(wdata);
		return ret < 0 ? ret : -EIO;
	}
	ret = wiimote_cmd_read(wdata, 0xa40024 + 12, buf + 12, 12);
	if (ret != 12) {
		wiimote_cmd_release(wdata);
		return ret < 0 ? ret : -EIO;
	}

	wiimote_cmd_release(wdata);

	offs = 0;
	for (i = 0; i < 3; ++i) {
		for (j = 0; j < 4; ++j) {
			wdata->state.calib_bboard[j][i] = buf[offs];
			wdata->state.calib_bboard[j][i] <<= 8;
			wdata->state.calib_bboard[j][i] |= buf[offs + 1];
			offs += 2;
		}
	}

	wdata->extension.input = input_allocate_device();
	if (!wdata->extension.input)
		return -ENOMEM;

	ret = device_create_file(&wdata->hdev->dev,
				 &dev_attr_bboard_calib);
	if (ret) {
		hid_err(wdata->hdev, "cannot create sysfs attribute\n");
		goto err_free;
	}

	input_set_drvdata(wdata->extension.input, wdata);
	wdata->extension.input->open = wiimod_bboard_open;
	wdata->extension.input->close = wiimod_bboard_close;
	wdata->extension.input->dev.parent = &wdata->hdev->dev;
	wdata->extension.input->id.bustype = wdata->hdev->bus;
	wdata->extension.input->id.vendor = wdata->hdev->vendor;
	wdata->extension.input->id.product = wdata->hdev->product;
	wdata->extension.input->id.version = wdata->hdev->version;
	wdata->extension.input->name = WIIMOTE_NAME " Balance Board";

	set_bit(EV_KEY, wdata->extension.input->evbit);
	set_bit(BTN_A, wdata->extension.input->keybit);

	set_bit(EV_ABS, wdata->extension.input->evbit);
	set_bit(ABS_HAT0X, wdata->extension.input->absbit);
	set_bit(ABS_HAT0Y, wdata->extension.input->absbit);
	set_bit(ABS_HAT1X, wdata->extension.input->absbit);
	set_bit(ABS_HAT1Y, wdata->extension.input->absbit);
	input_set_abs_params(wdata->extension.input,
			     ABS_HAT0X, 0, 65535, 2, 4);
	input_set_abs_params(wdata->extension.input,
			     ABS_HAT0Y, 0, 65535, 2, 4);
	input_set_abs_params(wdata->extension.input,
			     ABS_HAT1X, 0, 65535, 2, 4);
	input_set_abs_params(wdata->extension.input,
			     ABS_HAT1Y, 0, 65535, 2, 4);

	ret = input_register_device(wdata->extension.input);
	if (ret)
		goto err_file;

	return 0;

err_file:
	device_remove_file(&wdata->hdev->dev,
			   &dev_attr_bboard_calib);
err_free:
	input_free_device(wdata->extension.input);
	wdata->extension.input = NULL;
	return ret;
}

static void wiimod_bboard_remove(const struct wiimod_ops *ops,
				 struct wiimote_data *wdata)
{
	if (!wdata->extension.input)
		return;

	input_unregister_device(wdata->extension.input);
	wdata->extension.input = NULL;
	device_remove_file(&wdata->hdev->dev,
			   &dev_attr_bboard_calib);
}

static const struct wiimod_ops wiimod_bboard = {
	.flags = WIIMOD_FLAG_EXT8,
	.arg = 0,
	.probe = wiimod_bboard_probe,
	.remove = wiimod_bboard_remove,
	.in_keys = wiimod_bboard_in_keys,
	.in_ext = wiimod_bboard_in_ext,
};

/*
 * Builtin Motion Plus
 * This module simply sets the WIIPROTO_FLAG_BUILTIN_MP protocol flag which
 * disables polling for Motion-Plus. This should be set only for devices which
 * don't allow MP hotplugging.
 */

static int wiimod_builtin_mp_probe(const struct wiimod_ops *ops,
				   struct wiimote_data *wdata)
{
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.flags |= WIIPROTO_FLAG_BUILTIN_MP;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;
}

static void wiimod_builtin_mp_remove(const struct wiimod_ops *ops,
				     struct wiimote_data *wdata)
{
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.flags |= WIIPROTO_FLAG_BUILTIN_MP;
	spin_unlock_irqrestore(&wdata->state.lock, flags);
}

static const struct wiimod_ops wiimod_builtin_mp = {
	.flags = 0,
	.arg = 0,
	.probe = wiimod_builtin_mp_probe,
	.remove = wiimod_builtin_mp_remove,
};

/*
 * No Motion Plus
 * This module simply sets the WIIPROTO_FLAG_NO_MP protocol flag which
 * disables motion-plus. This is needed for devices that advertise this but we
 * don't know how to use it (or whether it is actually present).
 */

static int wiimod_no_mp_probe(const struct wiimod_ops *ops,
			      struct wiimote_data *wdata)
{
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.flags |= WIIPROTO_FLAG_NO_MP;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;
}

static void wiimod_no_mp_remove(const struct wiimod_ops *ops,
				struct wiimote_data *wdata)
{
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.flags |= WIIPROTO_FLAG_NO_MP;
	spin_unlock_irqrestore(&wdata->state.lock, flags);
}

static const struct wiimod_ops wiimod_no_mp = {
	.flags = 0,
	.arg = 0,
	.probe = wiimod_no_mp_probe,
	.remove = wiimod_no_mp_remove,
};

/*
 * Motion Plus
 * The Motion Plus extension provides rotation sensors (gyro) as a small
 * extension device for Wii Remotes. Many devices have them built-in so
 * you cannot see them from the outside.
 * Motion Plus extensions are special because they are on a separate extension
 * port and allow other extensions to be used simultaneously. This is all
 * handled by the Wiimote Core so we don't have to deal with it.
 */

static void wiimod_mp_in_mp(struct wiimote_data *wdata, const __u8 *ext)
{
	__s32 x, y, z;

	/*        |   8    7    6    5    4    3 |  2  |  1  |
	 *   -----+------------------------------+-----+-----+
	 *    1   |               Yaw Speed <7:0>            |
	 *    2   |              Roll Speed <7:0>            |
	 *    3   |             Pitch Speed <7:0>            |
	 *   -----+------------------------------+-----+-----+
	 *    4   |       Yaw Speed <13:8>       | Yaw |Pitch|
	 *   -----+------------------------------+-----+-----+
	 *    5   |      Roll Speed <13:8>       |Roll | Ext |
	 *   -----+------------------------------+-----+-----+
	 *    6   |     Pitch Speed <13:8>       |  1  |  0  |
	 *   -----+------------------------------+-----+-----+
	 * The single bits Yaw, Roll, Pitch in the lower right corner specify
	 * whether the wiimote is rotating fast (0) or slow (1). Speed for slow
	 * roation is 440 deg/s and for fast rotation 2000 deg/s. To get a
	 * linear scale we multiply by 2000/440 = ~4.5454 which is 18 for fast
	 * and 9 for slow.
	 * If the wiimote is not rotating the sensor reports 2^13 = 8192.
	 * Ext specifies whether an extension is connected to the motionp.
	 * which is parsed by wiimote-core.
	 */

	x = ext[0];
	y = ext[1];
	z = ext[2];

	x |= (((__u16)ext[3]) << 6) & 0xff00;
	y |= (((__u16)ext[4]) << 6) & 0xff00;
	z |= (((__u16)ext[5]) << 6) & 0xff00;

	x -= 8192;
	y -= 8192;
	z -= 8192;

	if (!(ext[3] & 0x02))
		x *= 18;
	else
		x *= 9;
	if (!(ext[4] & 0x02))
		y *= 18;
	else
		y *= 9;
	if (!(ext[3] & 0x01))
		z *= 18;
	else
		z *= 9;

	input_report_abs(wdata->mp, ABS_RX, x);
	input_report_abs(wdata->mp, ABS_RY, y);
	input_report_abs(wdata->mp, ABS_RZ, z);
	input_sync(wdata->mp);
}

static int wiimod_mp_open(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.flags |= WIIPROTO_FLAG_MP_USED;
	wiiproto_req_drm(wdata, WIIPROTO_REQ_NULL);
	__wiimote_schedule(wdata);
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;
}

static void wiimod_mp_close(struct input_dev *dev)
{
	struct wiimote_data *wdata = input_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->state.flags &= ~WIIPROTO_FLAG_MP_USED;
	wiiproto_req_drm(wdata, WIIPROTO_REQ_NULL);
	__wiimote_schedule(wdata);
	spin_unlock_irqrestore(&wdata->state.lock, flags);
}

static int wiimod_mp_probe(const struct wiimod_ops *ops,
			   struct wiimote_data *wdata)
{
	int ret;

	wdata->mp = input_allocate_device();
	if (!wdata->mp)
		return -ENOMEM;

	input_set_drvdata(wdata->mp, wdata);
	wdata->mp->open = wiimod_mp_open;
	wdata->mp->close = wiimod_mp_close;
	wdata->mp->dev.parent = &wdata->hdev->dev;
	wdata->mp->id.bustype = wdata->hdev->bus;
	wdata->mp->id.vendor = wdata->hdev->vendor;
	wdata->mp->id.product = wdata->hdev->product;
	wdata->mp->id.version = wdata->hdev->version;
	wdata->mp->name = WIIMOTE_NAME " Motion Plus";

	set_bit(EV_ABS, wdata->mp->evbit);
	set_bit(ABS_RX, wdata->mp->absbit);
	set_bit(ABS_RY, wdata->mp->absbit);
	set_bit(ABS_RZ, wdata->mp->absbit);
	input_set_abs_params(wdata->mp,
			     ABS_RX, -16000, 16000, 4, 8);
	input_set_abs_params(wdata->mp,
			     ABS_RY, -16000, 16000, 4, 8);
	input_set_abs_params(wdata->mp,
			     ABS_RZ, -16000, 16000, 4, 8);

	ret = input_register_device(wdata->mp);
	if (ret)
		goto err_free;

	return 0;

err_free:
	input_free_device(wdata->mp);
	wdata->mp = NULL;
	return ret;
}

static void wiimod_mp_remove(const struct wiimod_ops *ops,
			     struct wiimote_data *wdata)
{
	if (!wdata->mp)
		return;

	input_unregister_device(wdata->mp);
	wdata->mp = NULL;
}

const struct wiimod_ops wiimod_mp = {
	.flags = 0,
	.arg = 0,
	.probe = wiimod_mp_probe,
	.remove = wiimod_mp_remove,
	.in_mp = wiimod_mp_in_mp,
};

/* module table */

static const struct wiimod_ops wiimod_dummy;

const struct wiimod_ops *wiimod_table[WIIMOD_NUM] = {
	[WIIMOD_KEYS] = &wiimod_keys,
	[WIIMOD_RUMBLE] = &wiimod_rumble,
	[WIIMOD_BATTERY] = &wiimod_battery,
	[WIIMOD_LED1] = &wiimod_leds[0],
	[WIIMOD_LED2] = &wiimod_leds[1],
	[WIIMOD_LED3] = &wiimod_leds[2],
	[WIIMOD_LED4] = &wiimod_leds[3],
	[WIIMOD_ACCEL] = &wiimod_accel,
	[WIIMOD_IR] = &wiimod_ir,
	[WIIMOD_BUILTIN_MP] = &wiimod_builtin_mp,
	[WIIMOD_NO_MP] = &wiimod_no_mp,
};

const struct wiimod_ops *wiimod_ext_table[WIIMOTE_EXT_NUM] = {
	[WIIMOTE_EXT_NONE] = &wiimod_dummy,
	[WIIMOTE_EXT_UNKNOWN] = &wiimod_dummy,
	[WIIMOTE_EXT_NUNCHUK] = &wiimod_nunchuk,
	[WIIMOTE_EXT_CLASSIC_CONTROLLER] = &wiimod_classic,
	[WIIMOTE_EXT_BALANCE_BOARD] = &wiimod_bboard,
};
