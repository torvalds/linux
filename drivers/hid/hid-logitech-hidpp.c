// SPDX-License-Identifier: GPL-2.0-only
/*
 *  HIDPP protocol for Logitech receivers
 *
 *  Copyright (c) 2011 Logitech (c)
 *  Copyright (c) 2012-2013 Google (c)
 *  Copyright (c) 2013-2014 Red Hat Inc.
 */


#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/input.h>
#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/kfifo.h>
#include <linux/input/mt.h>
#include <linux/workqueue.h>
#include <linux/atomic.h>
#include <linux/fixp-arith.h>
#include <asm/unaligned.h>
#include "usbhid/usbhid.h"
#include "hid-ids.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Benjamin Tissoires <benjamin.tissoires@gmail.com>");
MODULE_AUTHOR("Nestor Lopez Casado <nlopezcasad@logitech.com>");

static bool disable_raw_mode;
module_param(disable_raw_mode, bool, 0644);
MODULE_PARM_DESC(disable_raw_mode,
	"Disable Raw mode reporting for touchpads and keep firmware gestures.");

static bool disable_tap_to_click;
module_param(disable_tap_to_click, bool, 0644);
MODULE_PARM_DESC(disable_tap_to_click,
	"Disable Tap-To-Click mode reporting for touchpads (only on the K400 currently).");

/* Define a non-zero software ID to identify our own requests */
#define LINUX_KERNEL_SW_ID			0x01

#define REPORT_ID_HIDPP_SHORT			0x10
#define REPORT_ID_HIDPP_LONG			0x11
#define REPORT_ID_HIDPP_VERY_LONG		0x12

#define HIDPP_REPORT_SHORT_LENGTH		7
#define HIDPP_REPORT_LONG_LENGTH		20
#define HIDPP_REPORT_VERY_LONG_MAX_LENGTH	64

#define HIDPP_REPORT_SHORT_SUPPORTED		BIT(0)
#define HIDPP_REPORT_LONG_SUPPORTED		BIT(1)
#define HIDPP_REPORT_VERY_LONG_SUPPORTED	BIT(2)

#define HIDPP_SUB_ID_CONSUMER_VENDOR_KEYS	0x03
#define HIDPP_SUB_ID_ROLLER			0x05
#define HIDPP_SUB_ID_MOUSE_EXTRA_BTNS		0x06
#define HIDPP_SUB_ID_USER_IFACE_EVENT		0x08
#define HIDPP_USER_IFACE_EVENT_ENCRYPTION_KEY_LOST	BIT(5)

#define HIDPP_QUIRK_CLASS_WTP			BIT(0)
#define HIDPP_QUIRK_CLASS_M560			BIT(1)
#define HIDPP_QUIRK_CLASS_K400			BIT(2)
#define HIDPP_QUIRK_CLASS_G920			BIT(3)
#define HIDPP_QUIRK_CLASS_K750			BIT(4)

/* bits 2..20 are reserved for classes */
/* #define HIDPP_QUIRK_CONNECT_EVENTS		BIT(21) disabled */
#define HIDPP_QUIRK_WTP_PHYSICAL_BUTTONS	BIT(22)
#define HIDPP_QUIRK_NO_HIDINPUT			BIT(23)
#define HIDPP_QUIRK_FORCE_OUTPUT_REPORTS	BIT(24)
#define HIDPP_QUIRK_UNIFYING			BIT(25)
#define HIDPP_QUIRK_HIDPP_WHEELS		BIT(26)
#define HIDPP_QUIRK_HIDPP_EXTRA_MOUSE_BTNS	BIT(27)
#define HIDPP_QUIRK_HIDPP_CONSUMER_VENDOR_KEYS	BIT(28)

/* These are just aliases for now */
#define HIDPP_QUIRK_KBD_SCROLL_WHEEL HIDPP_QUIRK_HIDPP_WHEELS
#define HIDPP_QUIRK_KBD_ZOOM_WHEEL   HIDPP_QUIRK_HIDPP_WHEELS

/* Convenience constant to check for any high-res support. */
#define HIDPP_CAPABILITY_HI_RES_SCROLL	(HIDPP_CAPABILITY_HIDPP10_FAST_SCROLL | \
					 HIDPP_CAPABILITY_HIDPP20_HI_RES_SCROLL | \
					 HIDPP_CAPABILITY_HIDPP20_HI_RES_WHEEL)

#define HIDPP_QUIRK_DELAYED_INIT		HIDPP_QUIRK_NO_HIDINPUT

#define HIDPP_CAPABILITY_HIDPP10_BATTERY	BIT(0)
#define HIDPP_CAPABILITY_HIDPP20_BATTERY	BIT(1)
#define HIDPP_CAPABILITY_BATTERY_MILEAGE	BIT(2)
#define HIDPP_CAPABILITY_BATTERY_LEVEL_STATUS	BIT(3)
#define HIDPP_CAPABILITY_BATTERY_VOLTAGE	BIT(4)
#define HIDPP_CAPABILITY_BATTERY_PERCENTAGE	BIT(5)
#define HIDPP_CAPABILITY_UNIFIED_BATTERY	BIT(6)
#define HIDPP_CAPABILITY_HIDPP20_HI_RES_WHEEL	BIT(7)
#define HIDPP_CAPABILITY_HIDPP20_HI_RES_SCROLL	BIT(8)
#define HIDPP_CAPABILITY_HIDPP10_FAST_SCROLL	BIT(9)

#define lg_map_key_clear(c)  hid_map_usage_clear(hi, usage, bit, max, EV_KEY, (c))

/*
 * There are two hidpp protocols in use, the first version hidpp10 is known
 * as register access protocol or RAP, the second version hidpp20 is known as
 * feature access protocol or FAP
 *
 * Most older devices (including the Unifying usb receiver) use the RAP protocol
 * where as most newer devices use the FAP protocol. Both protocols are
 * compatible with the underlying transport, which could be usb, Unifiying, or
 * bluetooth. The message lengths are defined by the hid vendor specific report
 * descriptor for the HIDPP_SHORT report type (total message lenth 7 bytes) and
 * the HIDPP_LONG report type (total message length 20 bytes)
 *
 * The RAP protocol uses both report types, whereas the FAP only uses HIDPP_LONG
 * messages. The Unifying receiver itself responds to RAP messages (device index
 * is 0xFF for the receiver), and all messages (short or long) with a device
 * index between 1 and 6 are passed untouched to the corresponding paired
 * Unifying device.
 *
 * The paired device can be RAP or FAP, it will receive the message untouched
 * from the Unifiying receiver.
 */

struct fap {
	u8 feature_index;
	u8 funcindex_clientid;
	u8 params[HIDPP_REPORT_VERY_LONG_MAX_LENGTH - 4U];
};

struct rap {
	u8 sub_id;
	u8 reg_address;
	u8 params[HIDPP_REPORT_VERY_LONG_MAX_LENGTH - 4U];
};

struct hidpp_report {
	u8 report_id;
	u8 device_index;
	union {
		struct fap fap;
		struct rap rap;
		u8 rawbytes[sizeof(struct fap)];
	};
} __packed;

struct hidpp_battery {
	u8 feature_index;
	u8 solar_feature_index;
	u8 voltage_feature_index;
	struct power_supply_desc desc;
	struct power_supply *ps;
	char name[64];
	int status;
	int capacity;
	int level;
	int voltage;
	int charge_type;
	bool online;
	u8 supported_levels_1004;
};

/**
 * struct hidpp_scroll_counter - Utility class for processing high-resolution
 *                             scroll events.
 * @dev: the input device for which events should be reported.
 * @wheel_multiplier: the scalar multiplier to be applied to each wheel event
 * @remainder: counts the number of high-resolution units moved since the last
 *             low-resolution event (REL_WHEEL or REL_HWHEEL) was sent. Should
 *             only be used by class methods.
 * @direction: direction of last movement (1 or -1)
 * @last_time: last event time, used to reset remainder after inactivity
 */
struct hidpp_scroll_counter {
	int wheel_multiplier;
	int remainder;
	int direction;
	unsigned long long last_time;
};

struct hidpp_device {
	struct hid_device *hid_dev;
	struct input_dev *input;
	struct mutex send_mutex;
	void *send_receive_buf;
	char *name;		/* will never be NULL and should not be freed */
	wait_queue_head_t wait;
	int very_long_report_length;
	bool answer_available;
	u8 protocol_major;
	u8 protocol_minor;

	void *private_data;

	struct work_struct work;
	struct kfifo delayed_work_fifo;
	atomic_t connected;
	struct input_dev *delayed_input;

	unsigned long quirks;
	unsigned long capabilities;
	u8 supported_reports;

	struct hidpp_battery battery;
	struct hidpp_scroll_counter vertical_wheel_counter;

	u8 wireless_feature_index;
};

/* HID++ 1.0 error codes */
#define HIDPP_ERROR				0x8f
#define HIDPP_ERROR_SUCCESS			0x00
#define HIDPP_ERROR_INVALID_SUBID		0x01
#define HIDPP_ERROR_INVALID_ADRESS		0x02
#define HIDPP_ERROR_INVALID_VALUE		0x03
#define HIDPP_ERROR_CONNECT_FAIL		0x04
#define HIDPP_ERROR_TOO_MANY_DEVICES		0x05
#define HIDPP_ERROR_ALREADY_EXISTS		0x06
#define HIDPP_ERROR_BUSY			0x07
#define HIDPP_ERROR_UNKNOWN_DEVICE		0x08
#define HIDPP_ERROR_RESOURCE_ERROR		0x09
#define HIDPP_ERROR_REQUEST_UNAVAILABLE		0x0a
#define HIDPP_ERROR_INVALID_PARAM_VALUE		0x0b
#define HIDPP_ERROR_WRONG_PIN_CODE		0x0c
/* HID++ 2.0 error codes */
#define HIDPP20_ERROR				0xff

static void hidpp_connect_event(struct hidpp_device *hidpp_dev);

static int __hidpp_send_report(struct hid_device *hdev,
				struct hidpp_report *hidpp_report)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);
	int fields_count, ret;

	switch (hidpp_report->report_id) {
	case REPORT_ID_HIDPP_SHORT:
		fields_count = HIDPP_REPORT_SHORT_LENGTH;
		break;
	case REPORT_ID_HIDPP_LONG:
		fields_count = HIDPP_REPORT_LONG_LENGTH;
		break;
	case REPORT_ID_HIDPP_VERY_LONG:
		fields_count = hidpp->very_long_report_length;
		break;
	default:
		return -ENODEV;
	}

	/*
	 * set the device_index as the receiver, it will be overwritten by
	 * hid_hw_request if needed
	 */
	hidpp_report->device_index = 0xff;

	if (hidpp->quirks & HIDPP_QUIRK_FORCE_OUTPUT_REPORTS) {
		ret = hid_hw_output_report(hdev, (u8 *)hidpp_report, fields_count);
	} else {
		ret = hid_hw_raw_request(hdev, hidpp_report->report_id,
			(u8 *)hidpp_report, fields_count, HID_OUTPUT_REPORT,
			HID_REQ_SET_REPORT);
	}

	return ret == fields_count ? 0 : -1;
}

/*
 * hidpp_send_message_sync() returns 0 in case of success, and something else
 * in case of a failure.
 * - If ' something else' is positive, that means that an error has been raised
 *   by the protocol itself.
 * - If ' something else' is negative, that means that we had a classic error
 *   (-ENOMEM, -EPIPE, etc...)
 */
static int hidpp_send_message_sync(struct hidpp_device *hidpp,
	struct hidpp_report *message,
	struct hidpp_report *response)
{
	int ret;

	mutex_lock(&hidpp->send_mutex);

	hidpp->send_receive_buf = response;
	hidpp->answer_available = false;

	/*
	 * So that we can later validate the answer when it arrives
	 * in hidpp_raw_event
	 */
	*response = *message;

	ret = __hidpp_send_report(hidpp->hid_dev, message);

	if (ret) {
		dbg_hid("__hidpp_send_report returned err: %d\n", ret);
		memset(response, 0, sizeof(struct hidpp_report));
		goto exit;
	}

	if (!wait_event_timeout(hidpp->wait, hidpp->answer_available,
				5*HZ)) {
		dbg_hid("%s:timeout waiting for response\n", __func__);
		memset(response, 0, sizeof(struct hidpp_report));
		ret = -ETIMEDOUT;
	}

	if (response->report_id == REPORT_ID_HIDPP_SHORT &&
	    response->rap.sub_id == HIDPP_ERROR) {
		ret = response->rap.params[1];
		dbg_hid("%s:got hidpp error %02X\n", __func__, ret);
		goto exit;
	}

	if ((response->report_id == REPORT_ID_HIDPP_LONG ||
			response->report_id == REPORT_ID_HIDPP_VERY_LONG) &&
			response->fap.feature_index == HIDPP20_ERROR) {
		ret = response->fap.params[1];
		dbg_hid("%s:got hidpp 2.0 error %02X\n", __func__, ret);
		goto exit;
	}

exit:
	mutex_unlock(&hidpp->send_mutex);
	return ret;

}

static int hidpp_send_fap_command_sync(struct hidpp_device *hidpp,
	u8 feat_index, u8 funcindex_clientid, u8 *params, int param_count,
	struct hidpp_report *response)
{
	struct hidpp_report *message;
	int ret;

	if (param_count > sizeof(message->fap.params))
		return -EINVAL;

	message = kzalloc(sizeof(struct hidpp_report), GFP_KERNEL);
	if (!message)
		return -ENOMEM;

	if (param_count > (HIDPP_REPORT_LONG_LENGTH - 4))
		message->report_id = REPORT_ID_HIDPP_VERY_LONG;
	else
		message->report_id = REPORT_ID_HIDPP_LONG;
	message->fap.feature_index = feat_index;
	message->fap.funcindex_clientid = funcindex_clientid | LINUX_KERNEL_SW_ID;
	memcpy(&message->fap.params, params, param_count);

	ret = hidpp_send_message_sync(hidpp, message, response);
	kfree(message);
	return ret;
}

static int hidpp_send_rap_command_sync(struct hidpp_device *hidpp_dev,
	u8 report_id, u8 sub_id, u8 reg_address, u8 *params, int param_count,
	struct hidpp_report *response)
{
	struct hidpp_report *message;
	int ret, max_count;

	/* Send as long report if short reports are not supported. */
	if (report_id == REPORT_ID_HIDPP_SHORT &&
	    !(hidpp_dev->supported_reports & HIDPP_REPORT_SHORT_SUPPORTED))
		report_id = REPORT_ID_HIDPP_LONG;

	switch (report_id) {
	case REPORT_ID_HIDPP_SHORT:
		max_count = HIDPP_REPORT_SHORT_LENGTH - 4;
		break;
	case REPORT_ID_HIDPP_LONG:
		max_count = HIDPP_REPORT_LONG_LENGTH - 4;
		break;
	case REPORT_ID_HIDPP_VERY_LONG:
		max_count = hidpp_dev->very_long_report_length - 4;
		break;
	default:
		return -EINVAL;
	}

	if (param_count > max_count)
		return -EINVAL;

	message = kzalloc(sizeof(struct hidpp_report), GFP_KERNEL);
	if (!message)
		return -ENOMEM;
	message->report_id = report_id;
	message->rap.sub_id = sub_id;
	message->rap.reg_address = reg_address;
	memcpy(&message->rap.params, params, param_count);

	ret = hidpp_send_message_sync(hidpp_dev, message, response);
	kfree(message);
	return ret;
}

static void delayed_work_cb(struct work_struct *work)
{
	struct hidpp_device *hidpp = container_of(work, struct hidpp_device,
							work);
	hidpp_connect_event(hidpp);
}

static inline bool hidpp_match_answer(struct hidpp_report *question,
		struct hidpp_report *answer)
{
	return (answer->fap.feature_index == question->fap.feature_index) &&
	   (answer->fap.funcindex_clientid == question->fap.funcindex_clientid);
}

static inline bool hidpp_match_error(struct hidpp_report *question,
		struct hidpp_report *answer)
{
	return ((answer->rap.sub_id == HIDPP_ERROR) ||
	    (answer->fap.feature_index == HIDPP20_ERROR)) &&
	    (answer->fap.funcindex_clientid == question->fap.feature_index) &&
	    (answer->fap.params[0] == question->fap.funcindex_clientid);
}

static inline bool hidpp_report_is_connect_event(struct hidpp_device *hidpp,
		struct hidpp_report *report)
{
	return (hidpp->wireless_feature_index &&
		(report->fap.feature_index == hidpp->wireless_feature_index)) ||
		((report->report_id == REPORT_ID_HIDPP_SHORT) &&
		(report->rap.sub_id == 0x41));
}

/*
 * hidpp_prefix_name() prefixes the current given name with "Logitech ".
 */
static void hidpp_prefix_name(char **name, int name_length)
{
#define PREFIX_LENGTH 9 /* "Logitech " */

	int new_length;
	char *new_name;

	if (name_length > PREFIX_LENGTH &&
	    strncmp(*name, "Logitech ", PREFIX_LENGTH) == 0)
		/* The prefix has is already in the name */
		return;

	new_length = PREFIX_LENGTH + name_length;
	new_name = kzalloc(new_length, GFP_KERNEL);
	if (!new_name)
		return;

	snprintf(new_name, new_length, "Logitech %s", *name);

	kfree(*name);

	*name = new_name;
}

/**
 * hidpp_scroll_counter_handle_scroll() - Send high- and low-resolution scroll
 *                                        events given a high-resolution wheel
 *                                        movement.
 * @input_dev: Pointer to the input device
 * @counter: a hid_scroll_counter struct describing the wheel.
 * @hi_res_value: the movement of the wheel, in the mouse's high-resolution
 *                units.
 *
 * Given a high-resolution movement, this function converts the movement into
 * fractions of 120 and emits high-resolution scroll events for the input
 * device. It also uses the multiplier from &struct hid_scroll_counter to
 * emit low-resolution scroll events when appropriate for
 * backwards-compatibility with userspace input libraries.
 */
static void hidpp_scroll_counter_handle_scroll(struct input_dev *input_dev,
					       struct hidpp_scroll_counter *counter,
					       int hi_res_value)
{
	int low_res_value, remainder, direction;
	unsigned long long now, previous;

	hi_res_value = hi_res_value * 120/counter->wheel_multiplier;
	input_report_rel(input_dev, REL_WHEEL_HI_RES, hi_res_value);

	remainder = counter->remainder;
	direction = hi_res_value > 0 ? 1 : -1;

	now = sched_clock();
	previous = counter->last_time;
	counter->last_time = now;
	/*
	 * Reset the remainder after a period of inactivity or when the
	 * direction changes. This prevents the REL_WHEEL emulation point
	 * from sliding for devices that don't always provide the same
	 * number of movements per detent.
	 */
	if (now - previous > 1000000000 || direction != counter->direction)
		remainder = 0;

	counter->direction = direction;
	remainder += hi_res_value;

	/* Some wheels will rest 7/8ths of a detent from the previous detent
	 * after slow movement, so we want the threshold for low-res events to
	 * be in the middle between two detents (e.g. after 4/8ths) as
	 * opposed to on the detents themselves (8/8ths).
	 */
	if (abs(remainder) >= 60) {
		/* Add (or subtract) 1 because we want to trigger when the wheel
		 * is half-way to the next detent (i.e. scroll 1 detent after a
		 * 1/2 detent movement, 2 detents after a 1 1/2 detent movement,
		 * etc.).
		 */
		low_res_value = remainder / 120;
		if (low_res_value == 0)
			low_res_value = (hi_res_value > 0 ? 1 : -1);
		input_report_rel(input_dev, REL_WHEEL, low_res_value);
		remainder -= low_res_value * 120;
	}
	counter->remainder = remainder;
}

/* -------------------------------------------------------------------------- */
/* HIDP++ 1.0 commands                                                        */
/* -------------------------------------------------------------------------- */

#define HIDPP_SET_REGISTER				0x80
#define HIDPP_GET_REGISTER				0x81
#define HIDPP_SET_LONG_REGISTER				0x82
#define HIDPP_GET_LONG_REGISTER				0x83

/**
 * hidpp10_set_register - Modify a HID++ 1.0 register.
 * @hidpp_dev: the device to set the register on.
 * @register_address: the address of the register to modify.
 * @byte: the byte of the register to modify. Should be less than 3.
 * @mask: mask of the bits to modify
 * @value: new values for the bits in mask
 * Return: 0 if successful, otherwise a negative error code.
 */
static int hidpp10_set_register(struct hidpp_device *hidpp_dev,
	u8 register_address, u8 byte, u8 mask, u8 value)
{
	struct hidpp_report response;
	int ret;
	u8 params[3] = { 0 };

	ret = hidpp_send_rap_command_sync(hidpp_dev,
					  REPORT_ID_HIDPP_SHORT,
					  HIDPP_GET_REGISTER,
					  register_address,
					  NULL, 0, &response);
	if (ret)
		return ret;

	memcpy(params, response.rap.params, 3);

	params[byte] &= ~mask;
	params[byte] |= value & mask;

	return hidpp_send_rap_command_sync(hidpp_dev,
					   REPORT_ID_HIDPP_SHORT,
					   HIDPP_SET_REGISTER,
					   register_address,
					   params, 3, &response);
}

#define HIDPP_REG_ENABLE_REPORTS			0x00
#define HIDPP_ENABLE_CONSUMER_REPORT			BIT(0)
#define HIDPP_ENABLE_WHEEL_REPORT			BIT(2)
#define HIDPP_ENABLE_MOUSE_EXTRA_BTN_REPORT		BIT(3)
#define HIDPP_ENABLE_BAT_REPORT				BIT(4)
#define HIDPP_ENABLE_HWHEEL_REPORT			BIT(5)

static int hidpp10_enable_battery_reporting(struct hidpp_device *hidpp_dev)
{
	return hidpp10_set_register(hidpp_dev, HIDPP_REG_ENABLE_REPORTS, 0,
			  HIDPP_ENABLE_BAT_REPORT, HIDPP_ENABLE_BAT_REPORT);
}

#define HIDPP_REG_FEATURES				0x01
#define HIDPP_ENABLE_SPECIAL_BUTTON_FUNC		BIT(1)
#define HIDPP_ENABLE_FAST_SCROLL			BIT(6)

/* On HID++ 1.0 devices, high-res scroll was called "scrolling acceleration". */
static int hidpp10_enable_scrolling_acceleration(struct hidpp_device *hidpp_dev)
{
	return hidpp10_set_register(hidpp_dev, HIDPP_REG_FEATURES, 0,
			  HIDPP_ENABLE_FAST_SCROLL, HIDPP_ENABLE_FAST_SCROLL);
}

#define HIDPP_REG_BATTERY_STATUS			0x07

static int hidpp10_battery_status_map_level(u8 param)
{
	int level;

	switch (param) {
	case 1 ... 2:
		level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		break;
	case 3 ... 4:
		level = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		break;
	case 5 ... 6:
		level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		break;
	case 7:
		level = POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
		break;
	default:
		level = POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
	}

	return level;
}

static int hidpp10_battery_status_map_status(u8 param)
{
	int status;

	switch (param) {
	case 0x00:
		/* discharging (in use) */
		status = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case 0x21: /* (standard) charging */
	case 0x24: /* fast charging */
	case 0x25: /* slow charging */
		status = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case 0x26: /* topping charge */
	case 0x22: /* charge complete */
		status = POWER_SUPPLY_STATUS_FULL;
		break;
	case 0x20: /* unknown */
		status = POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	/*
	 * 0x01...0x1F = reserved (not charging)
	 * 0x23 = charging error
	 * 0x27..0xff = reserved
	 */
	default:
		status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	}

	return status;
}

static int hidpp10_query_battery_status(struct hidpp_device *hidpp)
{
	struct hidpp_report response;
	int ret, status;

	ret = hidpp_send_rap_command_sync(hidpp,
					REPORT_ID_HIDPP_SHORT,
					HIDPP_GET_REGISTER,
					HIDPP_REG_BATTERY_STATUS,
					NULL, 0, &response);
	if (ret)
		return ret;

	hidpp->battery.level =
		hidpp10_battery_status_map_level(response.rap.params[0]);
	status = hidpp10_battery_status_map_status(response.rap.params[1]);
	hidpp->battery.status = status;
	/* the capacity is only available when discharging or full */
	hidpp->battery.online = status == POWER_SUPPLY_STATUS_DISCHARGING ||
				status == POWER_SUPPLY_STATUS_FULL;

	return 0;
}

#define HIDPP_REG_BATTERY_MILEAGE			0x0D

static int hidpp10_battery_mileage_map_status(u8 param)
{
	int status;

	switch (param >> 6) {
	case 0x00:
		/* discharging (in use) */
		status = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case 0x01: /* charging */
		status = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case 0x02: /* charge complete */
		status = POWER_SUPPLY_STATUS_FULL;
		break;
	/*
	 * 0x03 = charging error
	 */
	default:
		status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	}

	return status;
}

static int hidpp10_query_battery_mileage(struct hidpp_device *hidpp)
{
	struct hidpp_report response;
	int ret, status;

	ret = hidpp_send_rap_command_sync(hidpp,
					REPORT_ID_HIDPP_SHORT,
					HIDPP_GET_REGISTER,
					HIDPP_REG_BATTERY_MILEAGE,
					NULL, 0, &response);
	if (ret)
		return ret;

	hidpp->battery.capacity = response.rap.params[0];
	status = hidpp10_battery_mileage_map_status(response.rap.params[2]);
	hidpp->battery.status = status;
	/* the capacity is only available when discharging or full */
	hidpp->battery.online = status == POWER_SUPPLY_STATUS_DISCHARGING ||
				status == POWER_SUPPLY_STATUS_FULL;

	return 0;
}

static int hidpp10_battery_event(struct hidpp_device *hidpp, u8 *data, int size)
{
	struct hidpp_report *report = (struct hidpp_report *)data;
	int status, capacity, level;
	bool changed;

	if (report->report_id != REPORT_ID_HIDPP_SHORT)
		return 0;

	switch (report->rap.sub_id) {
	case HIDPP_REG_BATTERY_STATUS:
		capacity = hidpp->battery.capacity;
		level = hidpp10_battery_status_map_level(report->rawbytes[1]);
		status = hidpp10_battery_status_map_status(report->rawbytes[2]);
		break;
	case HIDPP_REG_BATTERY_MILEAGE:
		capacity = report->rap.params[0];
		level = hidpp->battery.level;
		status = hidpp10_battery_mileage_map_status(report->rawbytes[3]);
		break;
	default:
		return 0;
	}

	changed = capacity != hidpp->battery.capacity ||
		  level != hidpp->battery.level ||
		  status != hidpp->battery.status;

	/* the capacity is only available when discharging or full */
	hidpp->battery.online = status == POWER_SUPPLY_STATUS_DISCHARGING ||
				status == POWER_SUPPLY_STATUS_FULL;

	if (changed) {
		hidpp->battery.level = level;
		hidpp->battery.status = status;
		if (hidpp->battery.ps)
			power_supply_changed(hidpp->battery.ps);
	}

	return 0;
}

#define HIDPP_REG_PAIRING_INFORMATION			0xB5
#define HIDPP_EXTENDED_PAIRING				0x30
#define HIDPP_DEVICE_NAME				0x40

static char *hidpp_unifying_get_name(struct hidpp_device *hidpp_dev)
{
	struct hidpp_report response;
	int ret;
	u8 params[1] = { HIDPP_DEVICE_NAME };
	char *name;
	int len;

	ret = hidpp_send_rap_command_sync(hidpp_dev,
					REPORT_ID_HIDPP_SHORT,
					HIDPP_GET_LONG_REGISTER,
					HIDPP_REG_PAIRING_INFORMATION,
					params, 1, &response);
	if (ret)
		return NULL;

	len = response.rap.params[1];

	if (2 + len > sizeof(response.rap.params))
		return NULL;

	if (len < 4) /* logitech devices are usually at least Xddd */
		return NULL;

	name = kzalloc(len + 1, GFP_KERNEL);
	if (!name)
		return NULL;

	memcpy(name, &response.rap.params[2], len);

	/* include the terminating '\0' */
	hidpp_prefix_name(&name, len + 1);

	return name;
}

static int hidpp_unifying_get_serial(struct hidpp_device *hidpp, u32 *serial)
{
	struct hidpp_report response;
	int ret;
	u8 params[1] = { HIDPP_EXTENDED_PAIRING };

	ret = hidpp_send_rap_command_sync(hidpp,
					REPORT_ID_HIDPP_SHORT,
					HIDPP_GET_LONG_REGISTER,
					HIDPP_REG_PAIRING_INFORMATION,
					params, 1, &response);
	if (ret)
		return ret;

	/*
	 * We don't care about LE or BE, we will output it as a string
	 * with %4phD, so we need to keep the order.
	 */
	*serial = *((u32 *)&response.rap.params[1]);
	return 0;
}

static int hidpp_unifying_init(struct hidpp_device *hidpp)
{
	struct hid_device *hdev = hidpp->hid_dev;
	const char *name;
	u32 serial;
	int ret;

	ret = hidpp_unifying_get_serial(hidpp, &serial);
	if (ret)
		return ret;

	snprintf(hdev->uniq, sizeof(hdev->uniq), "%04x-%4phD",
		 hdev->product, &serial);
	dbg_hid("HID++ Unifying: Got serial: %s\n", hdev->uniq);

	name = hidpp_unifying_get_name(hidpp);
	if (!name)
		return -EIO;

	snprintf(hdev->name, sizeof(hdev->name), "%s", name);
	dbg_hid("HID++ Unifying: Got name: %s\n", name);

	kfree(name);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0x0000: Root                                                               */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_ROOT					0x0000
#define HIDPP_PAGE_ROOT_IDX				0x00

#define CMD_ROOT_GET_FEATURE				0x00
#define CMD_ROOT_GET_PROTOCOL_VERSION			0x10

static int hidpp_root_get_feature(struct hidpp_device *hidpp, u16 feature,
	u8 *feature_index, u8 *feature_type)
{
	struct hidpp_report response;
	int ret;
	u8 params[2] = { feature >> 8, feature & 0x00FF };

	ret = hidpp_send_fap_command_sync(hidpp,
			HIDPP_PAGE_ROOT_IDX,
			CMD_ROOT_GET_FEATURE,
			params, 2, &response);
	if (ret)
		return ret;

	if (response.fap.params[0] == 0)
		return -ENOENT;

	*feature_index = response.fap.params[0];
	*feature_type = response.fap.params[1];

	return ret;
}

static int hidpp_root_get_protocol_version(struct hidpp_device *hidpp)
{
	const u8 ping_byte = 0x5a;
	u8 ping_data[3] = { 0, 0, ping_byte };
	struct hidpp_report response;
	int ret;

	ret = hidpp_send_rap_command_sync(hidpp,
			REPORT_ID_HIDPP_SHORT,
			HIDPP_PAGE_ROOT_IDX,
			CMD_ROOT_GET_PROTOCOL_VERSION,
			ping_data, sizeof(ping_data), &response);

	if (ret == HIDPP_ERROR_INVALID_SUBID) {
		hidpp->protocol_major = 1;
		hidpp->protocol_minor = 0;
		goto print_version;
	}

	/* the device might not be connected */
	if (ret == HIDPP_ERROR_RESOURCE_ERROR)
		return -EIO;

	if (ret > 0) {
		hid_err(hidpp->hid_dev, "%s: received protocol error 0x%02x\n",
			__func__, ret);
		return -EPROTO;
	}
	if (ret)
		return ret;

	if (response.rap.params[2] != ping_byte) {
		hid_err(hidpp->hid_dev, "%s: ping mismatch 0x%02x != 0x%02x\n",
			__func__, response.rap.params[2], ping_byte);
		return -EPROTO;
	}

	hidpp->protocol_major = response.rap.params[0];
	hidpp->protocol_minor = response.rap.params[1];

print_version:
	hid_info(hidpp->hid_dev, "HID++ %u.%u device connected.\n",
		 hidpp->protocol_major, hidpp->protocol_minor);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0x0005: GetDeviceNameType                                                  */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_GET_DEVICE_NAME_TYPE			0x0005

#define CMD_GET_DEVICE_NAME_TYPE_GET_COUNT		0x00
#define CMD_GET_DEVICE_NAME_TYPE_GET_DEVICE_NAME	0x10
#define CMD_GET_DEVICE_NAME_TYPE_GET_TYPE		0x20

static int hidpp_devicenametype_get_count(struct hidpp_device *hidpp,
	u8 feature_index, u8 *nameLength)
{
	struct hidpp_report response;
	int ret;

	ret = hidpp_send_fap_command_sync(hidpp, feature_index,
		CMD_GET_DEVICE_NAME_TYPE_GET_COUNT, NULL, 0, &response);

	if (ret > 0) {
		hid_err(hidpp->hid_dev, "%s: received protocol error 0x%02x\n",
			__func__, ret);
		return -EPROTO;
	}
	if (ret)
		return ret;

	*nameLength = response.fap.params[0];

	return ret;
}

static int hidpp_devicenametype_get_device_name(struct hidpp_device *hidpp,
	u8 feature_index, u8 char_index, char *device_name, int len_buf)
{
	struct hidpp_report response;
	int ret, i;
	int count;

	ret = hidpp_send_fap_command_sync(hidpp, feature_index,
		CMD_GET_DEVICE_NAME_TYPE_GET_DEVICE_NAME, &char_index, 1,
		&response);

	if (ret > 0) {
		hid_err(hidpp->hid_dev, "%s: received protocol error 0x%02x\n",
			__func__, ret);
		return -EPROTO;
	}
	if (ret)
		return ret;

	switch (response.report_id) {
	case REPORT_ID_HIDPP_VERY_LONG:
		count = hidpp->very_long_report_length - 4;
		break;
	case REPORT_ID_HIDPP_LONG:
		count = HIDPP_REPORT_LONG_LENGTH - 4;
		break;
	case REPORT_ID_HIDPP_SHORT:
		count = HIDPP_REPORT_SHORT_LENGTH - 4;
		break;
	default:
		return -EPROTO;
	}

	if (len_buf < count)
		count = len_buf;

	for (i = 0; i < count; i++)
		device_name[i] = response.fap.params[i];

	return count;
}

static char *hidpp_get_device_name(struct hidpp_device *hidpp)
{
	u8 feature_type;
	u8 feature_index;
	u8 __name_length;
	char *name;
	unsigned index = 0;
	int ret;

	ret = hidpp_root_get_feature(hidpp, HIDPP_PAGE_GET_DEVICE_NAME_TYPE,
		&feature_index, &feature_type);
	if (ret)
		return NULL;

	ret = hidpp_devicenametype_get_count(hidpp, feature_index,
		&__name_length);
	if (ret)
		return NULL;

	name = kzalloc(__name_length + 1, GFP_KERNEL);
	if (!name)
		return NULL;

	while (index < __name_length) {
		ret = hidpp_devicenametype_get_device_name(hidpp,
			feature_index, index, name + index,
			__name_length - index);
		if (ret <= 0) {
			kfree(name);
			return NULL;
		}
		index += ret;
	}

	/* include the terminating '\0' */
	hidpp_prefix_name(&name, __name_length + 1);

	return name;
}

/* -------------------------------------------------------------------------- */
/* 0x1000: Battery level status                                               */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_BATTERY_LEVEL_STATUS				0x1000

#define CMD_BATTERY_LEVEL_STATUS_GET_BATTERY_LEVEL_STATUS	0x00
#define CMD_BATTERY_LEVEL_STATUS_GET_BATTERY_CAPABILITY		0x10

#define EVENT_BATTERY_LEVEL_STATUS_BROADCAST			0x00

#define FLAG_BATTERY_LEVEL_DISABLE_OSD				BIT(0)
#define FLAG_BATTERY_LEVEL_MILEAGE				BIT(1)
#define FLAG_BATTERY_LEVEL_RECHARGEABLE				BIT(2)

static int hidpp_map_battery_level(int capacity)
{
	if (capacity < 11)
		return POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	/*
	 * The spec says this should be < 31 but some devices report 30
	 * with brand new batteries and Windows reports 30 as "Good".
	 */
	else if (capacity < 30)
		return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else if (capacity < 81)
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	return POWER_SUPPLY_CAPACITY_LEVEL_FULL;
}

static int hidpp20_batterylevel_map_status_capacity(u8 data[3], int *capacity,
						    int *next_capacity,
						    int *level)
{
	int status;

	*capacity = data[0];
	*next_capacity = data[1];
	*level = POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;

	/* When discharging, we can rely on the device reported capacity.
	 * For all other states the device reports 0 (unknown).
	 */
	switch (data[2]) {
		case 0: /* discharging (in use) */
			status = POWER_SUPPLY_STATUS_DISCHARGING;
			*level = hidpp_map_battery_level(*capacity);
			break;
		case 1: /* recharging */
			status = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case 2: /* charge in final stage */
			status = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case 3: /* charge complete */
			status = POWER_SUPPLY_STATUS_FULL;
			*level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
			*capacity = 100;
			break;
		case 4: /* recharging below optimal speed */
			status = POWER_SUPPLY_STATUS_CHARGING;
			break;
		/* 5 = invalid battery type
		   6 = thermal error
		   7 = other charging error */
		default:
			status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
	}

	return status;
}

static int hidpp20_batterylevel_get_battery_capacity(struct hidpp_device *hidpp,
						     u8 feature_index,
						     int *status,
						     int *capacity,
						     int *next_capacity,
						     int *level)
{
	struct hidpp_report response;
	int ret;
	u8 *params = (u8 *)response.fap.params;

	ret = hidpp_send_fap_command_sync(hidpp, feature_index,
					  CMD_BATTERY_LEVEL_STATUS_GET_BATTERY_LEVEL_STATUS,
					  NULL, 0, &response);
	/* Ignore these intermittent errors */
	if (ret == HIDPP_ERROR_RESOURCE_ERROR)
		return -EIO;
	if (ret > 0) {
		hid_err(hidpp->hid_dev, "%s: received protocol error 0x%02x\n",
			__func__, ret);
		return -EPROTO;
	}
	if (ret)
		return ret;

	*status = hidpp20_batterylevel_map_status_capacity(params, capacity,
							   next_capacity,
							   level);

	return 0;
}

static int hidpp20_batterylevel_get_battery_info(struct hidpp_device *hidpp,
						  u8 feature_index)
{
	struct hidpp_report response;
	int ret;
	u8 *params = (u8 *)response.fap.params;
	unsigned int level_count, flags;

	ret = hidpp_send_fap_command_sync(hidpp, feature_index,
					  CMD_BATTERY_LEVEL_STATUS_GET_BATTERY_CAPABILITY,
					  NULL, 0, &response);
	if (ret > 0) {
		hid_err(hidpp->hid_dev, "%s: received protocol error 0x%02x\n",
			__func__, ret);
		return -EPROTO;
	}
	if (ret)
		return ret;

	level_count = params[0];
	flags = params[1];

	if (level_count < 10 || !(flags & FLAG_BATTERY_LEVEL_MILEAGE))
		hidpp->capabilities |= HIDPP_CAPABILITY_BATTERY_LEVEL_STATUS;
	else
		hidpp->capabilities |= HIDPP_CAPABILITY_BATTERY_MILEAGE;

	return 0;
}

static int hidpp20_query_battery_info_1000(struct hidpp_device *hidpp)
{
	u8 feature_type;
	int ret;
	int status, capacity, next_capacity, level;

	if (hidpp->battery.feature_index == 0xff) {
		ret = hidpp_root_get_feature(hidpp,
					     HIDPP_PAGE_BATTERY_LEVEL_STATUS,
					     &hidpp->battery.feature_index,
					     &feature_type);
		if (ret)
			return ret;
	}

	ret = hidpp20_batterylevel_get_battery_capacity(hidpp,
						hidpp->battery.feature_index,
						&status, &capacity,
						&next_capacity, &level);
	if (ret)
		return ret;

	ret = hidpp20_batterylevel_get_battery_info(hidpp,
						hidpp->battery.feature_index);
	if (ret)
		return ret;

	hidpp->battery.status = status;
	hidpp->battery.capacity = capacity;
	hidpp->battery.level = level;
	/* the capacity is only available when discharging or full */
	hidpp->battery.online = status == POWER_SUPPLY_STATUS_DISCHARGING ||
				status == POWER_SUPPLY_STATUS_FULL;

	return 0;
}

static int hidpp20_battery_event_1000(struct hidpp_device *hidpp,
				 u8 *data, int size)
{
	struct hidpp_report *report = (struct hidpp_report *)data;
	int status, capacity, next_capacity, level;
	bool changed;

	if (report->fap.feature_index != hidpp->battery.feature_index ||
	    report->fap.funcindex_clientid != EVENT_BATTERY_LEVEL_STATUS_BROADCAST)
		return 0;

	status = hidpp20_batterylevel_map_status_capacity(report->fap.params,
							  &capacity,
							  &next_capacity,
							  &level);

	/* the capacity is only available when discharging or full */
	hidpp->battery.online = status == POWER_SUPPLY_STATUS_DISCHARGING ||
				status == POWER_SUPPLY_STATUS_FULL;

	changed = capacity != hidpp->battery.capacity ||
		  level != hidpp->battery.level ||
		  status != hidpp->battery.status;

	if (changed) {
		hidpp->battery.level = level;
		hidpp->battery.capacity = capacity;
		hidpp->battery.status = status;
		if (hidpp->battery.ps)
			power_supply_changed(hidpp->battery.ps);
	}

	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0x1001: Battery voltage                                                    */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_BATTERY_VOLTAGE 0x1001

#define CMD_BATTERY_VOLTAGE_GET_BATTERY_VOLTAGE 0x00

#define EVENT_BATTERY_VOLTAGE_STATUS_BROADCAST 0x00

static int hidpp20_battery_map_status_voltage(u8 data[3], int *voltage,
						int *level, int *charge_type)
{
	int status;

	long flags = (long) data[2];
	*level = POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;

	if (flags & 0x80)
		switch (flags & 0x07) {
		case 0:
			status = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case 1:
			status = POWER_SUPPLY_STATUS_FULL;
			*level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
			break;
		case 2:
			status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
		default:
			status = POWER_SUPPLY_STATUS_UNKNOWN;
			break;
		}
	else
		status = POWER_SUPPLY_STATUS_DISCHARGING;

	*charge_type = POWER_SUPPLY_CHARGE_TYPE_STANDARD;
	if (test_bit(3, &flags)) {
		*charge_type = POWER_SUPPLY_CHARGE_TYPE_FAST;
	}
	if (test_bit(4, &flags)) {
		*charge_type = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	}
	if (test_bit(5, &flags)) {
		*level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	}

	*voltage = get_unaligned_be16(data);

	return status;
}

static int hidpp20_battery_get_battery_voltage(struct hidpp_device *hidpp,
						 u8 feature_index,
						 int *status, int *voltage,
						 int *level, int *charge_type)
{
	struct hidpp_report response;
	int ret;
	u8 *params = (u8 *)response.fap.params;

	ret = hidpp_send_fap_command_sync(hidpp, feature_index,
					  CMD_BATTERY_VOLTAGE_GET_BATTERY_VOLTAGE,
					  NULL, 0, &response);

	if (ret > 0) {
		hid_err(hidpp->hid_dev, "%s: received protocol error 0x%02x\n",
			__func__, ret);
		return -EPROTO;
	}
	if (ret)
		return ret;

	hidpp->capabilities |= HIDPP_CAPABILITY_BATTERY_VOLTAGE;

	*status = hidpp20_battery_map_status_voltage(params, voltage,
						     level, charge_type);

	return 0;
}

static int hidpp20_map_battery_capacity(struct hid_device *hid_dev, int voltage)
{
	/* NB: This voltage curve doesn't necessarily map perfectly to all
	 * devices that implement the BATTERY_VOLTAGE feature. This is because
	 * there are a few devices that use different battery technology.
	 */

	static const int voltages[] = {
		4186, 4156, 4143, 4133, 4122, 4113, 4103, 4094, 4086, 4075,
		4067, 4059, 4051, 4043, 4035, 4027, 4019, 4011, 4003, 3997,
		3989, 3983, 3976, 3969, 3961, 3955, 3949, 3942, 3935, 3929,
		3922, 3916, 3909, 3902, 3896, 3890, 3883, 3877, 3870, 3865,
		3859, 3853, 3848, 3842, 3837, 3833, 3828, 3824, 3819, 3815,
		3811, 3808, 3804, 3800, 3797, 3793, 3790, 3787, 3784, 3781,
		3778, 3775, 3772, 3770, 3767, 3764, 3762, 3759, 3757, 3754,
		3751, 3748, 3744, 3741, 3737, 3734, 3730, 3726, 3724, 3720,
		3717, 3714, 3710, 3706, 3702, 3697, 3693, 3688, 3683, 3677,
		3671, 3666, 3662, 3658, 3654, 3646, 3633, 3612, 3579, 3537
	};

	int i;

	BUILD_BUG_ON(ARRAY_SIZE(voltages) != 100);

	if (unlikely(voltage < 3500 || voltage >= 5000))
		hid_warn_once(hid_dev,
			      "%s: possibly using the wrong voltage curve\n",
			      __func__);

	for (i = 0; i < ARRAY_SIZE(voltages); i++) {
		if (voltage >= voltages[i])
			return ARRAY_SIZE(voltages) - i;
	}

	return 0;
}

static int hidpp20_query_battery_voltage_info(struct hidpp_device *hidpp)
{
	u8 feature_type;
	int ret;
	int status, voltage, level, charge_type;

	if (hidpp->battery.voltage_feature_index == 0xff) {
		ret = hidpp_root_get_feature(hidpp, HIDPP_PAGE_BATTERY_VOLTAGE,
					     &hidpp->battery.voltage_feature_index,
					     &feature_type);
		if (ret)
			return ret;
	}

	ret = hidpp20_battery_get_battery_voltage(hidpp,
						  hidpp->battery.voltage_feature_index,
						  &status, &voltage, &level, &charge_type);

	if (ret)
		return ret;

	hidpp->battery.status = status;
	hidpp->battery.voltage = voltage;
	hidpp->battery.capacity = hidpp20_map_battery_capacity(hidpp->hid_dev,
							       voltage);
	hidpp->battery.level = level;
	hidpp->battery.charge_type = charge_type;
	hidpp->battery.online = status != POWER_SUPPLY_STATUS_NOT_CHARGING;

	return 0;
}

static int hidpp20_battery_voltage_event(struct hidpp_device *hidpp,
					    u8 *data, int size)
{
	struct hidpp_report *report = (struct hidpp_report *)data;
	int status, voltage, level, charge_type;

	if (report->fap.feature_index != hidpp->battery.voltage_feature_index ||
		report->fap.funcindex_clientid != EVENT_BATTERY_VOLTAGE_STATUS_BROADCAST)
		return 0;

	status = hidpp20_battery_map_status_voltage(report->fap.params, &voltage,
						    &level, &charge_type);

	hidpp->battery.online = status != POWER_SUPPLY_STATUS_NOT_CHARGING;

	if (voltage != hidpp->battery.voltage || status != hidpp->battery.status) {
		hidpp->battery.voltage = voltage;
		hidpp->battery.capacity = hidpp20_map_battery_capacity(hidpp->hid_dev,
								       voltage);
		hidpp->battery.status = status;
		hidpp->battery.level = level;
		hidpp->battery.charge_type = charge_type;
		if (hidpp->battery.ps)
			power_supply_changed(hidpp->battery.ps);
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0x1004: Unified battery                                                    */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_UNIFIED_BATTERY				0x1004

#define CMD_UNIFIED_BATTERY_GET_CAPABILITIES			0x00
#define CMD_UNIFIED_BATTERY_GET_STATUS				0x10

#define EVENT_UNIFIED_BATTERY_STATUS_EVENT			0x00

#define FLAG_UNIFIED_BATTERY_LEVEL_CRITICAL			BIT(0)
#define FLAG_UNIFIED_BATTERY_LEVEL_LOW				BIT(1)
#define FLAG_UNIFIED_BATTERY_LEVEL_GOOD				BIT(2)
#define FLAG_UNIFIED_BATTERY_LEVEL_FULL				BIT(3)

#define FLAG_UNIFIED_BATTERY_FLAGS_RECHARGEABLE			BIT(0)
#define FLAG_UNIFIED_BATTERY_FLAGS_STATE_OF_CHARGE		BIT(1)

static int hidpp20_unifiedbattery_get_capabilities(struct hidpp_device *hidpp,
						   u8 feature_index)
{
	struct hidpp_report response;
	int ret;
	u8 *params = (u8 *)response.fap.params;

	if (hidpp->capabilities & HIDPP_CAPABILITY_BATTERY_LEVEL_STATUS ||
	    hidpp->capabilities & HIDPP_CAPABILITY_BATTERY_PERCENTAGE) {
		/* we have already set the device capabilities, so let's skip */
		return 0;
	}

	ret = hidpp_send_fap_command_sync(hidpp, feature_index,
					  CMD_UNIFIED_BATTERY_GET_CAPABILITIES,
					  NULL, 0, &response);
	/* Ignore these intermittent errors */
	if (ret == HIDPP_ERROR_RESOURCE_ERROR)
		return -EIO;
	if (ret > 0) {
		hid_err(hidpp->hid_dev, "%s: received protocol error 0x%02x\n",
			__func__, ret);
		return -EPROTO;
	}
	if (ret)
		return ret;

	/*
	 * If the device supports state of charge (battery percentage) we won't
	 * export the battery level information. there are 4 possible battery
	 * levels and they all are optional, this means that the device might
	 * not support any of them, we are just better off with the battery
	 * percentage.
	 */
	if (params[1] & FLAG_UNIFIED_BATTERY_FLAGS_STATE_OF_CHARGE) {
		hidpp->capabilities |= HIDPP_CAPABILITY_BATTERY_PERCENTAGE;
		hidpp->battery.supported_levels_1004 = 0;
	} else {
		hidpp->capabilities |= HIDPP_CAPABILITY_BATTERY_LEVEL_STATUS;
		hidpp->battery.supported_levels_1004 = params[0];
	}

	return 0;
}

static int hidpp20_unifiedbattery_map_status(struct hidpp_device *hidpp,
					     u8 charging_status,
					     u8 external_power_status)
{
	int status;

	switch (charging_status) {
		case 0: /* discharging */
			status = POWER_SUPPLY_STATUS_DISCHARGING;
			break;
		case 1: /* charging */
		case 2: /* charging slow */
			status = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case 3: /* complete */
			status = POWER_SUPPLY_STATUS_FULL;
			break;
		case 4: /* error */
			status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			hid_info(hidpp->hid_dev, "%s: charging error",
				 hidpp->name);
			break;
		default:
			status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
	}

	return status;
}

static int hidpp20_unifiedbattery_map_level(struct hidpp_device *hidpp,
					    u8 battery_level)
{
	/* cler unsupported level bits */
	battery_level &= hidpp->battery.supported_levels_1004;

	if (battery_level & FLAG_UNIFIED_BATTERY_LEVEL_FULL)
		return POWER_SUPPLY_CAPACITY_LEVEL_FULL;
	else if (battery_level & FLAG_UNIFIED_BATTERY_LEVEL_GOOD)
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	else if (battery_level & FLAG_UNIFIED_BATTERY_LEVEL_LOW)
		return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else if (battery_level & FLAG_UNIFIED_BATTERY_LEVEL_CRITICAL)
		return POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;

	return POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
}

static int hidpp20_unifiedbattery_get_status(struct hidpp_device *hidpp,
					     u8 feature_index,
					     u8 *state_of_charge,
					     int *status,
					     int *level)
{
	struct hidpp_report response;
	int ret;
	u8 *params = (u8 *)response.fap.params;

	ret = hidpp_send_fap_command_sync(hidpp, feature_index,
					  CMD_UNIFIED_BATTERY_GET_STATUS,
					  NULL, 0, &response);
	/* Ignore these intermittent errors */
	if (ret == HIDPP_ERROR_RESOURCE_ERROR)
		return -EIO;
	if (ret > 0) {
		hid_err(hidpp->hid_dev, "%s: received protocol error 0x%02x\n",
			__func__, ret);
		return -EPROTO;
	}
	if (ret)
		return ret;

	*state_of_charge = params[0];
	*status = hidpp20_unifiedbattery_map_status(hidpp, params[2], params[3]);
	*level = hidpp20_unifiedbattery_map_level(hidpp, params[1]);

	return 0;
}

static int hidpp20_query_battery_info_1004(struct hidpp_device *hidpp)
{
	u8 feature_type;
	int ret;
	u8 state_of_charge;
	int status, level;

	if (hidpp->battery.feature_index == 0xff) {
		ret = hidpp_root_get_feature(hidpp,
					     HIDPP_PAGE_UNIFIED_BATTERY,
					     &hidpp->battery.feature_index,
					     &feature_type);
		if (ret)
			return ret;
	}

	ret = hidpp20_unifiedbattery_get_capabilities(hidpp,
					hidpp->battery.feature_index);
	if (ret)
		return ret;

	ret = hidpp20_unifiedbattery_get_status(hidpp,
						hidpp->battery.feature_index,
						&state_of_charge,
						&status,
						&level);
	if (ret)
		return ret;

	hidpp->capabilities |= HIDPP_CAPABILITY_UNIFIED_BATTERY;
	hidpp->battery.capacity = state_of_charge;
	hidpp->battery.status = status;
	hidpp->battery.level = level;
	hidpp->battery.online = true;

	return 0;
}

static int hidpp20_battery_event_1004(struct hidpp_device *hidpp,
				 u8 *data, int size)
{
	struct hidpp_report *report = (struct hidpp_report *)data;
	u8 *params = (u8 *)report->fap.params;
	int state_of_charge, status, level;
	bool changed;

	if (report->fap.feature_index != hidpp->battery.feature_index ||
	    report->fap.funcindex_clientid != EVENT_UNIFIED_BATTERY_STATUS_EVENT)
		return 0;

	state_of_charge = params[0];
	status = hidpp20_unifiedbattery_map_status(hidpp, params[2], params[3]);
	level = hidpp20_unifiedbattery_map_level(hidpp, params[1]);

	changed = status != hidpp->battery.status ||
		  (state_of_charge != hidpp->battery.capacity &&
		   hidpp->capabilities & HIDPP_CAPABILITY_BATTERY_PERCENTAGE) ||
		  (level != hidpp->battery.level &&
		   hidpp->capabilities & HIDPP_CAPABILITY_BATTERY_LEVEL_STATUS);

	if (changed) {
		hidpp->battery.capacity = state_of_charge;
		hidpp->battery.status = status;
		hidpp->battery.level = level;
		if (hidpp->battery.ps)
			power_supply_changed(hidpp->battery.ps);
	}

	return 0;
}

/* -------------------------------------------------------------------------- */
/* Battery feature helpers                                                    */
/* -------------------------------------------------------------------------- */

static enum power_supply_property hidpp_battery_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
	0, /* placeholder for POWER_SUPPLY_PROP_CAPACITY, */
	0, /* placeholder for POWER_SUPPLY_PROP_CAPACITY_LEVEL, */
	0, /* placeholder for POWER_SUPPLY_PROP_VOLTAGE_NOW, */
};

static int hidpp_battery_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct hidpp_device *hidpp = power_supply_get_drvdata(psy);
	int ret = 0;

	switch(psp) {
		case POWER_SUPPLY_PROP_STATUS:
			val->intval = hidpp->battery.status;
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			val->intval = hidpp->battery.capacity;
			break;
		case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
			val->intval = hidpp->battery.level;
			break;
		case POWER_SUPPLY_PROP_SCOPE:
			val->intval = POWER_SUPPLY_SCOPE_DEVICE;
			break;
		case POWER_SUPPLY_PROP_ONLINE:
			val->intval = hidpp->battery.online;
			break;
		case POWER_SUPPLY_PROP_MODEL_NAME:
			if (!strncmp(hidpp->name, "Logitech ", 9))
				val->strval = hidpp->name + 9;
			else
				val->strval = hidpp->name;
			break;
		case POWER_SUPPLY_PROP_MANUFACTURER:
			val->strval = "Logitech";
			break;
		case POWER_SUPPLY_PROP_SERIAL_NUMBER:
			val->strval = hidpp->hid_dev->uniq;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			/* hardware reports voltage in mV. sysfs expects uV */
			val->intval = hidpp->battery.voltage * 1000;
			break;
		case POWER_SUPPLY_PROP_CHARGE_TYPE:
			val->intval = hidpp->battery.charge_type;
			break;
		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

/* -------------------------------------------------------------------------- */
/* 0x1d4b: Wireless device status                                             */
/* -------------------------------------------------------------------------- */
#define HIDPP_PAGE_WIRELESS_DEVICE_STATUS			0x1d4b

static int hidpp_set_wireless_feature_index(struct hidpp_device *hidpp)
{
	u8 feature_type;
	int ret;

	ret = hidpp_root_get_feature(hidpp,
				     HIDPP_PAGE_WIRELESS_DEVICE_STATUS,
				     &hidpp->wireless_feature_index,
				     &feature_type);

	return ret;
}

/* -------------------------------------------------------------------------- */
/* 0x2120: Hi-resolution scrolling                                            */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_HI_RESOLUTION_SCROLLING			0x2120

#define CMD_HI_RESOLUTION_SCROLLING_SET_HIGHRES_SCROLLING_MODE	0x10

static int hidpp_hrs_set_highres_scrolling_mode(struct hidpp_device *hidpp,
	bool enabled, u8 *multiplier)
{
	u8 feature_index;
	u8 feature_type;
	int ret;
	u8 params[1];
	struct hidpp_report response;

	ret = hidpp_root_get_feature(hidpp,
				     HIDPP_PAGE_HI_RESOLUTION_SCROLLING,
				     &feature_index,
				     &feature_type);
	if (ret)
		return ret;

	params[0] = enabled ? BIT(0) : 0;
	ret = hidpp_send_fap_command_sync(hidpp, feature_index,
					  CMD_HI_RESOLUTION_SCROLLING_SET_HIGHRES_SCROLLING_MODE,
					  params, sizeof(params), &response);
	if (ret)
		return ret;
	*multiplier = response.fap.params[1];
	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0x2121: HiRes Wheel                                                        */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_HIRES_WHEEL		0x2121

#define CMD_HIRES_WHEEL_GET_WHEEL_CAPABILITY	0x00
#define CMD_HIRES_WHEEL_SET_WHEEL_MODE		0x20

static int hidpp_hrw_get_wheel_capability(struct hidpp_device *hidpp,
	u8 *multiplier)
{
	u8 feature_index;
	u8 feature_type;
	int ret;
	struct hidpp_report response;

	ret = hidpp_root_get_feature(hidpp, HIDPP_PAGE_HIRES_WHEEL,
				     &feature_index, &feature_type);
	if (ret)
		goto return_default;

	ret = hidpp_send_fap_command_sync(hidpp, feature_index,
					  CMD_HIRES_WHEEL_GET_WHEEL_CAPABILITY,
					  NULL, 0, &response);
	if (ret)
		goto return_default;

	*multiplier = response.fap.params[0];
	return 0;
return_default:
	hid_warn(hidpp->hid_dev,
		 "Couldn't get wheel multiplier (error %d)\n", ret);
	return ret;
}

static int hidpp_hrw_set_wheel_mode(struct hidpp_device *hidpp, bool invert,
	bool high_resolution, bool use_hidpp)
{
	u8 feature_index;
	u8 feature_type;
	int ret;
	u8 params[1];
	struct hidpp_report response;

	ret = hidpp_root_get_feature(hidpp, HIDPP_PAGE_HIRES_WHEEL,
				     &feature_index, &feature_type);
	if (ret)
		return ret;

	params[0] = (invert          ? BIT(2) : 0) |
		    (high_resolution ? BIT(1) : 0) |
		    (use_hidpp       ? BIT(0) : 0);

	return hidpp_send_fap_command_sync(hidpp, feature_index,
					   CMD_HIRES_WHEEL_SET_WHEEL_MODE,
					   params, sizeof(params), &response);
}

/* -------------------------------------------------------------------------- */
/* 0x4301: Solar Keyboard                                                     */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_SOLAR_KEYBOARD			0x4301

#define CMD_SOLAR_SET_LIGHT_MEASURE			0x00

#define EVENT_SOLAR_BATTERY_BROADCAST			0x00
#define EVENT_SOLAR_BATTERY_LIGHT_MEASURE		0x10
#define EVENT_SOLAR_CHECK_LIGHT_BUTTON			0x20

static int hidpp_solar_request_battery_event(struct hidpp_device *hidpp)
{
	struct hidpp_report response;
	u8 params[2] = { 1, 1 };
	u8 feature_type;
	int ret;

	if (hidpp->battery.feature_index == 0xff) {
		ret = hidpp_root_get_feature(hidpp,
					     HIDPP_PAGE_SOLAR_KEYBOARD,
					     &hidpp->battery.solar_feature_index,
					     &feature_type);
		if (ret)
			return ret;
	}

	ret = hidpp_send_fap_command_sync(hidpp,
					  hidpp->battery.solar_feature_index,
					  CMD_SOLAR_SET_LIGHT_MEASURE,
					  params, 2, &response);
	if (ret > 0) {
		hid_err(hidpp->hid_dev, "%s: received protocol error 0x%02x\n",
			__func__, ret);
		return -EPROTO;
	}
	if (ret)
		return ret;

	hidpp->capabilities |= HIDPP_CAPABILITY_BATTERY_MILEAGE;

	return 0;
}

static int hidpp_solar_battery_event(struct hidpp_device *hidpp,
				     u8 *data, int size)
{
	struct hidpp_report *report = (struct hidpp_report *)data;
	int capacity, lux, status;
	u8 function;

	function = report->fap.funcindex_clientid;


	if (report->fap.feature_index != hidpp->battery.solar_feature_index ||
	    !(function == EVENT_SOLAR_BATTERY_BROADCAST ||
	      function == EVENT_SOLAR_BATTERY_LIGHT_MEASURE ||
	      function == EVENT_SOLAR_CHECK_LIGHT_BUTTON))
		return 0;

	capacity = report->fap.params[0];

	switch (function) {
	case EVENT_SOLAR_BATTERY_LIGHT_MEASURE:
		lux = (report->fap.params[1] << 8) | report->fap.params[2];
		if (lux > 200)
			status = POWER_SUPPLY_STATUS_CHARGING;
		else
			status = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case EVENT_SOLAR_CHECK_LIGHT_BUTTON:
	default:
		if (capacity < hidpp->battery.capacity)
			status = POWER_SUPPLY_STATUS_DISCHARGING;
		else
			status = POWER_SUPPLY_STATUS_CHARGING;

	}

	if (capacity == 100)
		status = POWER_SUPPLY_STATUS_FULL;

	hidpp->battery.online = true;
	if (capacity != hidpp->battery.capacity ||
	    status != hidpp->battery.status) {
		hidpp->battery.capacity = capacity;
		hidpp->battery.status = status;
		if (hidpp->battery.ps)
			power_supply_changed(hidpp->battery.ps);
	}

	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0x6010: Touchpad FW items                                                  */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_TOUCHPAD_FW_ITEMS			0x6010

#define CMD_TOUCHPAD_FW_ITEMS_SET			0x10

struct hidpp_touchpad_fw_items {
	uint8_t presence;
	uint8_t desired_state;
	uint8_t state;
	uint8_t persistent;
};

/*
 * send a set state command to the device by reading the current items->state
 * field. items is then filled with the current state.
 */
static int hidpp_touchpad_fw_items_set(struct hidpp_device *hidpp,
				       u8 feature_index,
				       struct hidpp_touchpad_fw_items *items)
{
	struct hidpp_report response;
	int ret;
	u8 *params = (u8 *)response.fap.params;

	ret = hidpp_send_fap_command_sync(hidpp, feature_index,
		CMD_TOUCHPAD_FW_ITEMS_SET, &items->state, 1, &response);

	if (ret > 0) {
		hid_err(hidpp->hid_dev, "%s: received protocol error 0x%02x\n",
			__func__, ret);
		return -EPROTO;
	}
	if (ret)
		return ret;

	items->presence = params[0];
	items->desired_state = params[1];
	items->state = params[2];
	items->persistent = params[3];

	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0x6100: TouchPadRawXY                                                      */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_TOUCHPAD_RAW_XY			0x6100

#define CMD_TOUCHPAD_GET_RAW_INFO			0x00
#define CMD_TOUCHPAD_SET_RAW_REPORT_STATE		0x20

#define EVENT_TOUCHPAD_RAW_XY				0x00

#define TOUCHPAD_RAW_XY_ORIGIN_LOWER_LEFT		0x01
#define TOUCHPAD_RAW_XY_ORIGIN_UPPER_LEFT		0x03

struct hidpp_touchpad_raw_info {
	u16 x_size;
	u16 y_size;
	u8 z_range;
	u8 area_range;
	u8 timestamp_unit;
	u8 maxcontacts;
	u8 origin;
	u16 res;
};

struct hidpp_touchpad_raw_xy_finger {
	u8 contact_type;
	u8 contact_status;
	u16 x;
	u16 y;
	u8 z;
	u8 area;
	u8 finger_id;
};

struct hidpp_touchpad_raw_xy {
	u16 timestamp;
	struct hidpp_touchpad_raw_xy_finger fingers[2];
	u8 spurious_flag;
	u8 end_of_frame;
	u8 finger_count;
	u8 button;
};

static int hidpp_touchpad_get_raw_info(struct hidpp_device *hidpp,
	u8 feature_index, struct hidpp_touchpad_raw_info *raw_info)
{
	struct hidpp_report response;
	int ret;
	u8 *params = (u8 *)response.fap.params;

	ret = hidpp_send_fap_command_sync(hidpp, feature_index,
		CMD_TOUCHPAD_GET_RAW_INFO, NULL, 0, &response);

	if (ret > 0) {
		hid_err(hidpp->hid_dev, "%s: received protocol error 0x%02x\n",
			__func__, ret);
		return -EPROTO;
	}
	if (ret)
		return ret;

	raw_info->x_size = get_unaligned_be16(&params[0]);
	raw_info->y_size = get_unaligned_be16(&params[2]);
	raw_info->z_range = params[4];
	raw_info->area_range = params[5];
	raw_info->maxcontacts = params[7];
	raw_info->origin = params[8];
	/* res is given in unit per inch */
	raw_info->res = get_unaligned_be16(&params[13]) * 2 / 51;

	return ret;
}

static int hidpp_touchpad_set_raw_report_state(struct hidpp_device *hidpp_dev,
		u8 feature_index, bool send_raw_reports,
		bool sensor_enhanced_settings)
{
	struct hidpp_report response;

	/*
	 * Params:
	 *   bit 0 - enable raw
	 *   bit 1 - 16bit Z, no area
	 *   bit 2 - enhanced sensitivity
	 *   bit 3 - width, height (4 bits each) instead of area
	 *   bit 4 - send raw + gestures (degrades smoothness)
	 *   remaining bits - reserved
	 */
	u8 params = send_raw_reports | (sensor_enhanced_settings << 2);

	return hidpp_send_fap_command_sync(hidpp_dev, feature_index,
		CMD_TOUCHPAD_SET_RAW_REPORT_STATE, &params, 1, &response);
}

static void hidpp_touchpad_touch_event(u8 *data,
	struct hidpp_touchpad_raw_xy_finger *finger)
{
	u8 x_m = data[0] << 2;
	u8 y_m = data[2] << 2;

	finger->x = x_m << 6 | data[1];
	finger->y = y_m << 6 | data[3];

	finger->contact_type = data[0] >> 6;
	finger->contact_status = data[2] >> 6;

	finger->z = data[4];
	finger->area = data[5];
	finger->finger_id = data[6] >> 4;
}

static void hidpp_touchpad_raw_xy_event(struct hidpp_device *hidpp_dev,
		u8 *data, struct hidpp_touchpad_raw_xy *raw_xy)
{
	memset(raw_xy, 0, sizeof(struct hidpp_touchpad_raw_xy));
	raw_xy->end_of_frame = data[8] & 0x01;
	raw_xy->spurious_flag = (data[8] >> 1) & 0x01;
	raw_xy->finger_count = data[15] & 0x0f;
	raw_xy->button = (data[8] >> 2) & 0x01;

	if (raw_xy->finger_count) {
		hidpp_touchpad_touch_event(&data[2], &raw_xy->fingers[0]);
		hidpp_touchpad_touch_event(&data[9], &raw_xy->fingers[1]);
	}
}

/* -------------------------------------------------------------------------- */
/* 0x8123: Force feedback support                                             */
/* -------------------------------------------------------------------------- */

#define HIDPP_FF_GET_INFO		0x01
#define HIDPP_FF_RESET_ALL		0x11
#define HIDPP_FF_DOWNLOAD_EFFECT	0x21
#define HIDPP_FF_SET_EFFECT_STATE	0x31
#define HIDPP_FF_DESTROY_EFFECT		0x41
#define HIDPP_FF_GET_APERTURE		0x51
#define HIDPP_FF_SET_APERTURE		0x61
#define HIDPP_FF_GET_GLOBAL_GAINS	0x71
#define HIDPP_FF_SET_GLOBAL_GAINS	0x81

#define HIDPP_FF_EFFECT_STATE_GET	0x00
#define HIDPP_FF_EFFECT_STATE_STOP	0x01
#define HIDPP_FF_EFFECT_STATE_PLAY	0x02
#define HIDPP_FF_EFFECT_STATE_PAUSE	0x03

#define HIDPP_FF_EFFECT_CONSTANT	0x00
#define HIDPP_FF_EFFECT_PERIODIC_SINE		0x01
#define HIDPP_FF_EFFECT_PERIODIC_SQUARE		0x02
#define HIDPP_FF_EFFECT_PERIODIC_TRIANGLE	0x03
#define HIDPP_FF_EFFECT_PERIODIC_SAWTOOTHUP	0x04
#define HIDPP_FF_EFFECT_PERIODIC_SAWTOOTHDOWN	0x05
#define HIDPP_FF_EFFECT_SPRING		0x06
#define HIDPP_FF_EFFECT_DAMPER		0x07
#define HIDPP_FF_EFFECT_FRICTION	0x08
#define HIDPP_FF_EFFECT_INERTIA		0x09
#define HIDPP_FF_EFFECT_RAMP		0x0A

#define HIDPP_FF_EFFECT_AUTOSTART	0x80

#define HIDPP_FF_EFFECTID_NONE		-1
#define HIDPP_FF_EFFECTID_AUTOCENTER	-2
#define HIDPP_AUTOCENTER_PARAMS_LENGTH	18

#define HIDPP_FF_MAX_PARAMS	20
#define HIDPP_FF_RESERVED_SLOTS	1

struct hidpp_ff_private_data {
	struct hidpp_device *hidpp;
	u8 feature_index;
	u8 version;
	u16 gain;
	s16 range;
	u8 slot_autocenter;
	u8 num_effects;
	int *effect_ids;
	struct workqueue_struct *wq;
	atomic_t workqueue_size;
};

struct hidpp_ff_work_data {
	struct work_struct work;
	struct hidpp_ff_private_data *data;
	int effect_id;
	u8 command;
	u8 params[HIDPP_FF_MAX_PARAMS];
	u8 size;
};

static const signed short hidpp_ff_effects[] = {
	FF_CONSTANT,
	FF_PERIODIC,
	FF_SINE,
	FF_SQUARE,
	FF_SAW_UP,
	FF_SAW_DOWN,
	FF_TRIANGLE,
	FF_SPRING,
	FF_DAMPER,
	FF_AUTOCENTER,
	FF_GAIN,
	-1
};

static const signed short hidpp_ff_effects_v2[] = {
	FF_RAMP,
	FF_FRICTION,
	FF_INERTIA,
	-1
};

static const u8 HIDPP_FF_CONDITION_CMDS[] = {
	HIDPP_FF_EFFECT_SPRING,
	HIDPP_FF_EFFECT_FRICTION,
	HIDPP_FF_EFFECT_DAMPER,
	HIDPP_FF_EFFECT_INERTIA
};

static const char *HIDPP_FF_CONDITION_NAMES[] = {
	"spring",
	"friction",
	"damper",
	"inertia"
};


static u8 hidpp_ff_find_effect(struct hidpp_ff_private_data *data, int effect_id)
{
	int i;

	for (i = 0; i < data->num_effects; i++)
		if (data->effect_ids[i] == effect_id)
			return i+1;

	return 0;
}

static void hidpp_ff_work_handler(struct work_struct *w)
{
	struct hidpp_ff_work_data *wd = container_of(w, struct hidpp_ff_work_data, work);
	struct hidpp_ff_private_data *data = wd->data;
	struct hidpp_report response;
	u8 slot;
	int ret;

	/* add slot number if needed */
	switch (wd->effect_id) {
	case HIDPP_FF_EFFECTID_AUTOCENTER:
		wd->params[0] = data->slot_autocenter;
		break;
	case HIDPP_FF_EFFECTID_NONE:
		/* leave slot as zero */
		break;
	default:
		/* find current slot for effect */
		wd->params[0] = hidpp_ff_find_effect(data, wd->effect_id);
		break;
	}

	/* send command and wait for reply */
	ret = hidpp_send_fap_command_sync(data->hidpp, data->feature_index,
		wd->command, wd->params, wd->size, &response);

	if (ret) {
		hid_err(data->hidpp->hid_dev, "Failed to send command to device!\n");
		goto out;
	}

	/* parse return data */
	switch (wd->command) {
	case HIDPP_FF_DOWNLOAD_EFFECT:
		slot = response.fap.params[0];
		if (slot > 0 && slot <= data->num_effects) {
			if (wd->effect_id >= 0)
				/* regular effect uploaded */
				data->effect_ids[slot-1] = wd->effect_id;
			else if (wd->effect_id >= HIDPP_FF_EFFECTID_AUTOCENTER)
				/* autocenter spring uploaded */
				data->slot_autocenter = slot;
		}
		break;
	case HIDPP_FF_DESTROY_EFFECT:
		if (wd->effect_id >= 0)
			/* regular effect destroyed */
			data->effect_ids[wd->params[0]-1] = -1;
		else if (wd->effect_id >= HIDPP_FF_EFFECTID_AUTOCENTER)
			/* autocenter spring destoyed */
			data->slot_autocenter = 0;
		break;
	case HIDPP_FF_SET_GLOBAL_GAINS:
		data->gain = (wd->params[0] << 8) + wd->params[1];
		break;
	case HIDPP_FF_SET_APERTURE:
		data->range = (wd->params[0] << 8) + wd->params[1];
		break;
	default:
		/* no action needed */
		break;
	}

out:
	atomic_dec(&data->workqueue_size);
	kfree(wd);
}

static int hidpp_ff_queue_work(struct hidpp_ff_private_data *data, int effect_id, u8 command, u8 *params, u8 size)
{
	struct hidpp_ff_work_data *wd = kzalloc(sizeof(*wd), GFP_KERNEL);
	int s;

	if (!wd)
		return -ENOMEM;

	INIT_WORK(&wd->work, hidpp_ff_work_handler);

	wd->data = data;
	wd->effect_id = effect_id;
	wd->command = command;
	wd->size = size;
	memcpy(wd->params, params, size);

	s = atomic_inc_return(&data->workqueue_size);
	queue_work(data->wq, &wd->work);

	/* warn about excessive queue size */
	if (s >= 20 && s % 20 == 0)
		hid_warn(data->hidpp->hid_dev, "Force feedback command queue contains %d commands, causing substantial delays!", s);

	return 0;
}

static int hidpp_ff_upload_effect(struct input_dev *dev, struct ff_effect *effect, struct ff_effect *old)
{
	struct hidpp_ff_private_data *data = dev->ff->private;
	u8 params[20];
	u8 size;
	int force;

	/* set common parameters */
	params[2] = effect->replay.length >> 8;
	params[3] = effect->replay.length & 255;
	params[4] = effect->replay.delay >> 8;
	params[5] = effect->replay.delay & 255;

	switch (effect->type) {
	case FF_CONSTANT:
		force = (effect->u.constant.level * fixp_sin16((effect->direction * 360) >> 16)) >> 15;
		params[1] = HIDPP_FF_EFFECT_CONSTANT;
		params[6] = force >> 8;
		params[7] = force & 255;
		params[8] = effect->u.constant.envelope.attack_level >> 7;
		params[9] = effect->u.constant.envelope.attack_length >> 8;
		params[10] = effect->u.constant.envelope.attack_length & 255;
		params[11] = effect->u.constant.envelope.fade_level >> 7;
		params[12] = effect->u.constant.envelope.fade_length >> 8;
		params[13] = effect->u.constant.envelope.fade_length & 255;
		size = 14;
		dbg_hid("Uploading constant force level=%d in dir %d = %d\n",
				effect->u.constant.level,
				effect->direction, force);
		dbg_hid("          envelope attack=(%d, %d ms) fade=(%d, %d ms)\n",
				effect->u.constant.envelope.attack_level,
				effect->u.constant.envelope.attack_length,
				effect->u.constant.envelope.fade_level,
				effect->u.constant.envelope.fade_length);
		break;
	case FF_PERIODIC:
	{
		switch (effect->u.periodic.waveform) {
		case FF_SINE:
			params[1] = HIDPP_FF_EFFECT_PERIODIC_SINE;
			break;
		case FF_SQUARE:
			params[1] = HIDPP_FF_EFFECT_PERIODIC_SQUARE;
			break;
		case FF_SAW_UP:
			params[1] = HIDPP_FF_EFFECT_PERIODIC_SAWTOOTHUP;
			break;
		case FF_SAW_DOWN:
			params[1] = HIDPP_FF_EFFECT_PERIODIC_SAWTOOTHDOWN;
			break;
		case FF_TRIANGLE:
			params[1] = HIDPP_FF_EFFECT_PERIODIC_TRIANGLE;
			break;
		default:
			hid_err(data->hidpp->hid_dev, "Unexpected periodic waveform type %i!\n", effect->u.periodic.waveform);
			return -EINVAL;
		}
		force = (effect->u.periodic.magnitude * fixp_sin16((effect->direction * 360) >> 16)) >> 15;
		params[6] = effect->u.periodic.magnitude >> 8;
		params[7] = effect->u.periodic.magnitude & 255;
		params[8] = effect->u.periodic.offset >> 8;
		params[9] = effect->u.periodic.offset & 255;
		params[10] = effect->u.periodic.period >> 8;
		params[11] = effect->u.periodic.period & 255;
		params[12] = effect->u.periodic.phase >> 8;
		params[13] = effect->u.periodic.phase & 255;
		params[14] = effect->u.periodic.envelope.attack_level >> 7;
		params[15] = effect->u.periodic.envelope.attack_length >> 8;
		params[16] = effect->u.periodic.envelope.attack_length & 255;
		params[17] = effect->u.periodic.envelope.fade_level >> 7;
		params[18] = effect->u.periodic.envelope.fade_length >> 8;
		params[19] = effect->u.periodic.envelope.fade_length & 255;
		size = 20;
		dbg_hid("Uploading periodic force mag=%d/dir=%d, offset=%d, period=%d ms, phase=%d\n",
				effect->u.periodic.magnitude, effect->direction,
				effect->u.periodic.offset,
				effect->u.periodic.period,
				effect->u.periodic.phase);
		dbg_hid("          envelope attack=(%d, %d ms) fade=(%d, %d ms)\n",
				effect->u.periodic.envelope.attack_level,
				effect->u.periodic.envelope.attack_length,
				effect->u.periodic.envelope.fade_level,
				effect->u.periodic.envelope.fade_length);
		break;
	}
	case FF_RAMP:
		params[1] = HIDPP_FF_EFFECT_RAMP;
		force = (effect->u.ramp.start_level * fixp_sin16((effect->direction * 360) >> 16)) >> 15;
		params[6] = force >> 8;
		params[7] = force & 255;
		force = (effect->u.ramp.end_level * fixp_sin16((effect->direction * 360) >> 16)) >> 15;
		params[8] = force >> 8;
		params[9] = force & 255;
		params[10] = effect->u.ramp.envelope.attack_level >> 7;
		params[11] = effect->u.ramp.envelope.attack_length >> 8;
		params[12] = effect->u.ramp.envelope.attack_length & 255;
		params[13] = effect->u.ramp.envelope.fade_level >> 7;
		params[14] = effect->u.ramp.envelope.fade_length >> 8;
		params[15] = effect->u.ramp.envelope.fade_length & 255;
		size = 16;
		dbg_hid("Uploading ramp force level=%d -> %d in dir %d = %d\n",
				effect->u.ramp.start_level,
				effect->u.ramp.end_level,
				effect->direction, force);
		dbg_hid("          envelope attack=(%d, %d ms) fade=(%d, %d ms)\n",
				effect->u.ramp.envelope.attack_level,
				effect->u.ramp.envelope.attack_length,
				effect->u.ramp.envelope.fade_level,
				effect->u.ramp.envelope.fade_length);
		break;
	case FF_FRICTION:
	case FF_INERTIA:
	case FF_SPRING:
	case FF_DAMPER:
		params[1] = HIDPP_FF_CONDITION_CMDS[effect->type - FF_SPRING];
		params[6] = effect->u.condition[0].left_saturation >> 9;
		params[7] = (effect->u.condition[0].left_saturation >> 1) & 255;
		params[8] = effect->u.condition[0].left_coeff >> 8;
		params[9] = effect->u.condition[0].left_coeff & 255;
		params[10] = effect->u.condition[0].deadband >> 9;
		params[11] = (effect->u.condition[0].deadband >> 1) & 255;
		params[12] = effect->u.condition[0].center >> 8;
		params[13] = effect->u.condition[0].center & 255;
		params[14] = effect->u.condition[0].right_coeff >> 8;
		params[15] = effect->u.condition[0].right_coeff & 255;
		params[16] = effect->u.condition[0].right_saturation >> 9;
		params[17] = (effect->u.condition[0].right_saturation >> 1) & 255;
		size = 18;
		dbg_hid("Uploading %s force left coeff=%d, left sat=%d, right coeff=%d, right sat=%d\n",
				HIDPP_FF_CONDITION_NAMES[effect->type - FF_SPRING],
				effect->u.condition[0].left_coeff,
				effect->u.condition[0].left_saturation,
				effect->u.condition[0].right_coeff,
				effect->u.condition[0].right_saturation);
		dbg_hid("          deadband=%d, center=%d\n",
				effect->u.condition[0].deadband,
				effect->u.condition[0].center);
		break;
	default:
		hid_err(data->hidpp->hid_dev, "Unexpected force type %i!\n", effect->type);
		return -EINVAL;
	}

	return hidpp_ff_queue_work(data, effect->id, HIDPP_FF_DOWNLOAD_EFFECT, params, size);
}

static int hidpp_ff_playback(struct input_dev *dev, int effect_id, int value)
{
	struct hidpp_ff_private_data *data = dev->ff->private;
	u8 params[2];

	params[1] = value ? HIDPP_FF_EFFECT_STATE_PLAY : HIDPP_FF_EFFECT_STATE_STOP;

	dbg_hid("St%sing playback of effect %d.\n", value?"art":"opp", effect_id);

	return hidpp_ff_queue_work(data, effect_id, HIDPP_FF_SET_EFFECT_STATE, params, ARRAY_SIZE(params));
}

static int hidpp_ff_erase_effect(struct input_dev *dev, int effect_id)
{
	struct hidpp_ff_private_data *data = dev->ff->private;
	u8 slot = 0;

	dbg_hid("Erasing effect %d.\n", effect_id);

	return hidpp_ff_queue_work(data, effect_id, HIDPP_FF_DESTROY_EFFECT, &slot, 1);
}

static void hidpp_ff_set_autocenter(struct input_dev *dev, u16 magnitude)
{
	struct hidpp_ff_private_data *data = dev->ff->private;
	u8 params[HIDPP_AUTOCENTER_PARAMS_LENGTH];

	dbg_hid("Setting autocenter to %d.\n", magnitude);

	/* start a standard spring effect */
	params[1] = HIDPP_FF_EFFECT_SPRING | HIDPP_FF_EFFECT_AUTOSTART;
	/* zero delay and duration */
	params[2] = params[3] = params[4] = params[5] = 0;
	/* set coeff to 25% of saturation */
	params[8] = params[14] = magnitude >> 11;
	params[9] = params[15] = (magnitude >> 3) & 255;
	params[6] = params[16] = magnitude >> 9;
	params[7] = params[17] = (magnitude >> 1) & 255;
	/* zero deadband and center */
	params[10] = params[11] = params[12] = params[13] = 0;

	hidpp_ff_queue_work(data, HIDPP_FF_EFFECTID_AUTOCENTER, HIDPP_FF_DOWNLOAD_EFFECT, params, ARRAY_SIZE(params));
}

static void hidpp_ff_set_gain(struct input_dev *dev, u16 gain)
{
	struct hidpp_ff_private_data *data = dev->ff->private;
	u8 params[4];

	dbg_hid("Setting gain to %d.\n", gain);

	params[0] = gain >> 8;
	params[1] = gain & 255;
	params[2] = 0; /* no boost */
	params[3] = 0;

	hidpp_ff_queue_work(data, HIDPP_FF_EFFECTID_NONE, HIDPP_FF_SET_GLOBAL_GAINS, params, ARRAY_SIZE(params));
}

static ssize_t hidpp_ff_range_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hid_device *hid = to_hid_device(dev);
	struct hid_input *hidinput = list_entry(hid->inputs.next, struct hid_input, list);
	struct input_dev *idev = hidinput->input;
	struct hidpp_ff_private_data *data = idev->ff->private;

	return scnprintf(buf, PAGE_SIZE, "%u\n", data->range);
}

static ssize_t hidpp_ff_range_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hid = to_hid_device(dev);
	struct hid_input *hidinput = list_entry(hid->inputs.next, struct hid_input, list);
	struct input_dev *idev = hidinput->input;
	struct hidpp_ff_private_data *data = idev->ff->private;
	u8 params[2];
	int range = simple_strtoul(buf, NULL, 10);

	range = clamp(range, 180, 900);

	params[0] = range >> 8;
	params[1] = range & 0x00FF;

	hidpp_ff_queue_work(data, -1, HIDPP_FF_SET_APERTURE, params, ARRAY_SIZE(params));

	return count;
}

static DEVICE_ATTR(range, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH, hidpp_ff_range_show, hidpp_ff_range_store);

static void hidpp_ff_destroy(struct ff_device *ff)
{
	struct hidpp_ff_private_data *data = ff->private;
	struct hid_device *hid = data->hidpp->hid_dev;

	hid_info(hid, "Unloading HID++ force feedback.\n");

	device_remove_file(&hid->dev, &dev_attr_range);
	destroy_workqueue(data->wq);
	kfree(data->effect_ids);
}

static int hidpp_ff_init(struct hidpp_device *hidpp,
			 struct hidpp_ff_private_data *data)
{
	struct hid_device *hid = hidpp->hid_dev;
	struct hid_input *hidinput;
	struct input_dev *dev;
	const struct usb_device_descriptor *udesc = &(hid_to_usb_dev(hid)->descriptor);
	const u16 bcdDevice = le16_to_cpu(udesc->bcdDevice);
	struct ff_device *ff;
	int error, j, num_slots = data->num_effects;
	u8 version;

	if (list_empty(&hid->inputs)) {
		hid_err(hid, "no inputs found\n");
		return -ENODEV;
	}
	hidinput = list_entry(hid->inputs.next, struct hid_input, list);
	dev = hidinput->input;

	if (!dev) {
		hid_err(hid, "Struct input_dev not set!\n");
		return -EINVAL;
	}

	/* Get firmware release */
	version = bcdDevice & 255;

	/* Set supported force feedback capabilities */
	for (j = 0; hidpp_ff_effects[j] >= 0; j++)
		set_bit(hidpp_ff_effects[j], dev->ffbit);
	if (version > 1)
		for (j = 0; hidpp_ff_effects_v2[j] >= 0; j++)
			set_bit(hidpp_ff_effects_v2[j], dev->ffbit);

	error = input_ff_create(dev, num_slots);

	if (error) {
		hid_err(dev, "Failed to create FF device!\n");
		return error;
	}
	/*
	 * Create a copy of passed data, so we can transfer memory
	 * ownership to FF core
	 */
	data = kmemdup(data, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->effect_ids = kcalloc(num_slots, sizeof(int), GFP_KERNEL);
	if (!data->effect_ids) {
		kfree(data);
		return -ENOMEM;
	}
	data->wq = create_singlethread_workqueue("hidpp-ff-sendqueue");
	if (!data->wq) {
		kfree(data->effect_ids);
		kfree(data);
		return -ENOMEM;
	}

	data->hidpp = hidpp;
	data->version = version;
	for (j = 0; j < num_slots; j++)
		data->effect_ids[j] = -1;

	ff = dev->ff;
	ff->private = data;

	ff->upload = hidpp_ff_upload_effect;
	ff->erase = hidpp_ff_erase_effect;
	ff->playback = hidpp_ff_playback;
	ff->set_gain = hidpp_ff_set_gain;
	ff->set_autocenter = hidpp_ff_set_autocenter;
	ff->destroy = hidpp_ff_destroy;

	/* Create sysfs interface */
	error = device_create_file(&(hidpp->hid_dev->dev), &dev_attr_range);
	if (error)
		hid_warn(hidpp->hid_dev, "Unable to create sysfs interface for \"range\", errno %d!\n", error);

	/* init the hardware command queue */
	atomic_set(&data->workqueue_size, 0);

	hid_info(hid, "Force feedback support loaded (firmware release %d).\n",
		 version);

	return 0;
}

/* ************************************************************************** */
/*                                                                            */
/* Device Support                                                             */
/*                                                                            */
/* ************************************************************************** */

/* -------------------------------------------------------------------------- */
/* Touchpad HID++ devices                                                     */
/* -------------------------------------------------------------------------- */

#define WTP_MANUAL_RESOLUTION				39

struct wtp_data {
	u16 x_size, y_size;
	u8 finger_count;
	u8 mt_feature_index;
	u8 button_feature_index;
	u8 maxcontacts;
	bool flip_y;
	unsigned int resolution;
};

static int wtp_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	return -1;
}

static void wtp_populate_input(struct hidpp_device *hidpp,
			       struct input_dev *input_dev)
{
	struct wtp_data *wd = hidpp->private_data;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__clear_bit(EV_REL, input_dev->evbit);
	__clear_bit(EV_LED, input_dev->evbit);

	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, wd->x_size, 0, 0);
	input_abs_set_res(input_dev, ABS_MT_POSITION_X, wd->resolution);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, wd->y_size, 0, 0);
	input_abs_set_res(input_dev, ABS_MT_POSITION_Y, wd->resolution);

	/* Max pressure is not given by the devices, pick one */
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 50, 0, 0);

	input_set_capability(input_dev, EV_KEY, BTN_LEFT);

	if (hidpp->quirks & HIDPP_QUIRK_WTP_PHYSICAL_BUTTONS)
		input_set_capability(input_dev, EV_KEY, BTN_RIGHT);
	else
		__set_bit(INPUT_PROP_BUTTONPAD, input_dev->propbit);

	input_mt_init_slots(input_dev, wd->maxcontacts, INPUT_MT_POINTER |
		INPUT_MT_DROP_UNUSED);
}

static void wtp_touch_event(struct hidpp_device *hidpp,
	struct hidpp_touchpad_raw_xy_finger *touch_report)
{
	struct wtp_data *wd = hidpp->private_data;
	int slot;

	if (!touch_report->finger_id || touch_report->contact_type)
		/* no actual data */
		return;

	slot = input_mt_get_slot_by_key(hidpp->input, touch_report->finger_id);

	input_mt_slot(hidpp->input, slot);
	input_mt_report_slot_state(hidpp->input, MT_TOOL_FINGER,
					touch_report->contact_status);
	if (touch_report->contact_status) {
		input_event(hidpp->input, EV_ABS, ABS_MT_POSITION_X,
				touch_report->x);
		input_event(hidpp->input, EV_ABS, ABS_MT_POSITION_Y,
				wd->flip_y ? wd->y_size - touch_report->y :
					     touch_report->y);
		input_event(hidpp->input, EV_ABS, ABS_MT_PRESSURE,
				touch_report->area);
	}
}

static void wtp_send_raw_xy_event(struct hidpp_device *hidpp,
		struct hidpp_touchpad_raw_xy *raw)
{
	int i;

	for (i = 0; i < 2; i++)
		wtp_touch_event(hidpp, &(raw->fingers[i]));

	if (raw->end_of_frame &&
	    !(hidpp->quirks & HIDPP_QUIRK_WTP_PHYSICAL_BUTTONS))
		input_event(hidpp->input, EV_KEY, BTN_LEFT, raw->button);

	if (raw->end_of_frame || raw->finger_count <= 2) {
		input_mt_sync_frame(hidpp->input);
		input_sync(hidpp->input);
	}
}

static int wtp_mouse_raw_xy_event(struct hidpp_device *hidpp, u8 *data)
{
	struct wtp_data *wd = hidpp->private_data;
	u8 c1_area = ((data[7] & 0xf) * (data[7] & 0xf) +
		      (data[7] >> 4) * (data[7] >> 4)) / 2;
	u8 c2_area = ((data[13] & 0xf) * (data[13] & 0xf) +
		      (data[13] >> 4) * (data[13] >> 4)) / 2;
	struct hidpp_touchpad_raw_xy raw = {
		.timestamp = data[1],
		.fingers = {
			{
				.contact_type = 0,
				.contact_status = !!data[7],
				.x = get_unaligned_le16(&data[3]),
				.y = get_unaligned_le16(&data[5]),
				.z = c1_area,
				.area = c1_area,
				.finger_id = data[2],
			}, {
				.contact_type = 0,
				.contact_status = !!data[13],
				.x = get_unaligned_le16(&data[9]),
				.y = get_unaligned_le16(&data[11]),
				.z = c2_area,
				.area = c2_area,
				.finger_id = data[8],
			}
		},
		.finger_count = wd->maxcontacts,
		.spurious_flag = 0,
		.end_of_frame = (data[0] >> 7) == 0,
		.button = data[0] & 0x01,
	};

	wtp_send_raw_xy_event(hidpp, &raw);

	return 1;
}

static int wtp_raw_event(struct hid_device *hdev, u8 *data, int size)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);
	struct wtp_data *wd = hidpp->private_data;
	struct hidpp_report *report = (struct hidpp_report *)data;
	struct hidpp_touchpad_raw_xy raw;

	if (!wd || !hidpp->input)
		return 1;

	switch (data[0]) {
	case 0x02:
		if (size < 2) {
			hid_err(hdev, "Received HID report of bad size (%d)",
				size);
			return 1;
		}
		if (hidpp->quirks & HIDPP_QUIRK_WTP_PHYSICAL_BUTTONS) {
			input_event(hidpp->input, EV_KEY, BTN_LEFT,
					!!(data[1] & 0x01));
			input_event(hidpp->input, EV_KEY, BTN_RIGHT,
					!!(data[1] & 0x02));
			input_sync(hidpp->input);
			return 0;
		} else {
			if (size < 21)
				return 1;
			return wtp_mouse_raw_xy_event(hidpp, &data[7]);
		}
	case REPORT_ID_HIDPP_LONG:
		/* size is already checked in hidpp_raw_event. */
		if ((report->fap.feature_index != wd->mt_feature_index) ||
		    (report->fap.funcindex_clientid != EVENT_TOUCHPAD_RAW_XY))
			return 1;
		hidpp_touchpad_raw_xy_event(hidpp, data + 4, &raw);

		wtp_send_raw_xy_event(hidpp, &raw);
		return 0;
	}

	return 0;
}

static int wtp_get_config(struct hidpp_device *hidpp)
{
	struct wtp_data *wd = hidpp->private_data;
	struct hidpp_touchpad_raw_info raw_info = {0};
	u8 feature_type;
	int ret;

	ret = hidpp_root_get_feature(hidpp, HIDPP_PAGE_TOUCHPAD_RAW_XY,
		&wd->mt_feature_index, &feature_type);
	if (ret)
		/* means that the device is not powered up */
		return ret;

	ret = hidpp_touchpad_get_raw_info(hidpp, wd->mt_feature_index,
		&raw_info);
	if (ret)
		return ret;

	wd->x_size = raw_info.x_size;
	wd->y_size = raw_info.y_size;
	wd->maxcontacts = raw_info.maxcontacts;
	wd->flip_y = raw_info.origin == TOUCHPAD_RAW_XY_ORIGIN_LOWER_LEFT;
	wd->resolution = raw_info.res;
	if (!wd->resolution)
		wd->resolution = WTP_MANUAL_RESOLUTION;

	return 0;
}

static int wtp_allocate(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);
	struct wtp_data *wd;

	wd = devm_kzalloc(&hdev->dev, sizeof(struct wtp_data),
			GFP_KERNEL);
	if (!wd)
		return -ENOMEM;

	hidpp->private_data = wd;

	return 0;
};

static int wtp_connect(struct hid_device *hdev, bool connected)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);
	struct wtp_data *wd = hidpp->private_data;
	int ret;

	if (!wd->x_size) {
		ret = wtp_get_config(hidpp);
		if (ret) {
			hid_err(hdev, "Can not get wtp config: %d\n", ret);
			return ret;
		}
	}

	return hidpp_touchpad_set_raw_report_state(hidpp, wd->mt_feature_index,
			true, true);
}

/* ------------------------------------------------------------------------- */
/* Logitech M560 devices                                                     */
/* ------------------------------------------------------------------------- */

/*
 * Logitech M560 protocol overview
 *
 * The Logitech M560 mouse, is designed for windows 8. When the middle and/or
 * the sides buttons are pressed, it sends some keyboard keys events
 * instead of buttons ones.
 * To complicate things further, the middle button keys sequence
 * is different from the odd press and the even press.
 *
 * forward button -> Super_R
 * backward button -> Super_L+'d' (press only)
 * middle button -> 1st time: Alt_L+SuperL+XF86TouchpadOff (press only)
 *                  2nd time: left-click (press only)
 * NB: press-only means that when the button is pressed, the
 * KeyPress/ButtonPress and KeyRelease/ButtonRelease events are generated
 * together sequentially; instead when the button is released, no event is
 * generated !
 *
 * With the command
 *	10<xx>0a 3500af03 (where <xx> is the mouse id),
 * the mouse reacts differently:
 * - it never sends a keyboard key event
 * - for the three mouse button it sends:
 *	middle button               press   11<xx>0a 3500af00...
 *	side 1 button (forward)     press   11<xx>0a 3500b000...
 *	side 2 button (backward)    press   11<xx>0a 3500ae00...
 *	middle/side1/side2 button   release 11<xx>0a 35000000...
 */

static const u8 m560_config_parameter[] = {0x00, 0xaf, 0x03};

/* how buttons are mapped in the report */
#define M560_MOUSE_BTN_LEFT		0x01
#define M560_MOUSE_BTN_RIGHT		0x02
#define M560_MOUSE_BTN_WHEEL_LEFT	0x08
#define M560_MOUSE_BTN_WHEEL_RIGHT	0x10

#define M560_SUB_ID			0x0a
#define M560_BUTTON_MODE_REGISTER	0x35

static int m560_send_config_command(struct hid_device *hdev, bool connected)
{
	struct hidpp_report response;
	struct hidpp_device *hidpp_dev;

	hidpp_dev = hid_get_drvdata(hdev);

	return hidpp_send_rap_command_sync(
		hidpp_dev,
		REPORT_ID_HIDPP_SHORT,
		M560_SUB_ID,
		M560_BUTTON_MODE_REGISTER,
		(u8 *)m560_config_parameter,
		sizeof(m560_config_parameter),
		&response
	);
}

static int m560_raw_event(struct hid_device *hdev, u8 *data, int size)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);

	/* sanity check */
	if (!hidpp->input) {
		hid_err(hdev, "error in parameter\n");
		return -EINVAL;
	}

	if (size < 7) {
		hid_err(hdev, "error in report\n");
		return 0;
	}

	if (data[0] == REPORT_ID_HIDPP_LONG &&
	    data[2] == M560_SUB_ID && data[6] == 0x00) {
		/*
		 * m560 mouse report for middle, forward and backward button
		 *
		 * data[0] = 0x11
		 * data[1] = device-id
		 * data[2] = 0x0a
		 * data[5] = 0xaf -> middle
		 *	     0xb0 -> forward
		 *	     0xae -> backward
		 *	     0x00 -> release all
		 * data[6] = 0x00
		 */

		switch (data[5]) {
		case 0xaf:
			input_report_key(hidpp->input, BTN_MIDDLE, 1);
			break;
		case 0xb0:
			input_report_key(hidpp->input, BTN_FORWARD, 1);
			break;
		case 0xae:
			input_report_key(hidpp->input, BTN_BACK, 1);
			break;
		case 0x00:
			input_report_key(hidpp->input, BTN_BACK, 0);
			input_report_key(hidpp->input, BTN_FORWARD, 0);
			input_report_key(hidpp->input, BTN_MIDDLE, 0);
			break;
		default:
			hid_err(hdev, "error in report\n");
			return 0;
		}
		input_sync(hidpp->input);

	} else if (data[0] == 0x02) {
		/*
		 * Logitech M560 mouse report
		 *
		 * data[0] = type (0x02)
		 * data[1..2] = buttons
		 * data[3..5] = xy
		 * data[6] = wheel
		 */

		int v;

		input_report_key(hidpp->input, BTN_LEFT,
			!!(data[1] & M560_MOUSE_BTN_LEFT));
		input_report_key(hidpp->input, BTN_RIGHT,
			!!(data[1] & M560_MOUSE_BTN_RIGHT));

		if (data[1] & M560_MOUSE_BTN_WHEEL_LEFT) {
			input_report_rel(hidpp->input, REL_HWHEEL, -1);
			input_report_rel(hidpp->input, REL_HWHEEL_HI_RES,
					 -120);
		} else if (data[1] & M560_MOUSE_BTN_WHEEL_RIGHT) {
			input_report_rel(hidpp->input, REL_HWHEEL, 1);
			input_report_rel(hidpp->input, REL_HWHEEL_HI_RES,
					 120);
		}

		v = hid_snto32(hid_field_extract(hdev, data+3, 0, 12), 12);
		input_report_rel(hidpp->input, REL_X, v);

		v = hid_snto32(hid_field_extract(hdev, data+3, 12, 12), 12);
		input_report_rel(hidpp->input, REL_Y, v);

		v = hid_snto32(data[6], 8);
		if (v != 0)
			hidpp_scroll_counter_handle_scroll(hidpp->input,
					&hidpp->vertical_wheel_counter, v);

		input_sync(hidpp->input);
	}

	return 1;
}

static void m560_populate_input(struct hidpp_device *hidpp,
				struct input_dev *input_dev)
{
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_MIDDLE, input_dev->keybit);
	__set_bit(BTN_RIGHT, input_dev->keybit);
	__set_bit(BTN_LEFT, input_dev->keybit);
	__set_bit(BTN_BACK, input_dev->keybit);
	__set_bit(BTN_FORWARD, input_dev->keybit);

	__set_bit(EV_REL, input_dev->evbit);
	__set_bit(REL_X, input_dev->relbit);
	__set_bit(REL_Y, input_dev->relbit);
	__set_bit(REL_WHEEL, input_dev->relbit);
	__set_bit(REL_HWHEEL, input_dev->relbit);
	__set_bit(REL_WHEEL_HI_RES, input_dev->relbit);
	__set_bit(REL_HWHEEL_HI_RES, input_dev->relbit);
}

static int m560_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	return -1;
}

/* ------------------------------------------------------------------------- */
/* Logitech K400 devices                                                     */
/* ------------------------------------------------------------------------- */

/*
 * The Logitech K400 keyboard has an embedded touchpad which is seen
 * as a mouse from the OS point of view. There is a hardware shortcut to disable
 * tap-to-click but the setting is not remembered accross reset, annoying some
 * users.
 *
 * We can toggle this feature from the host by using the feature 0x6010:
 * Touchpad FW items
 */

struct k400_private_data {
	u8 feature_index;
};

static int k400_disable_tap_to_click(struct hidpp_device *hidpp)
{
	struct k400_private_data *k400 = hidpp->private_data;
	struct hidpp_touchpad_fw_items items = {};
	int ret;
	u8 feature_type;

	if (!k400->feature_index) {
		ret = hidpp_root_get_feature(hidpp,
			HIDPP_PAGE_TOUCHPAD_FW_ITEMS,
			&k400->feature_index, &feature_type);
		if (ret)
			/* means that the device is not powered up */
			return ret;
	}

	ret = hidpp_touchpad_fw_items_set(hidpp, k400->feature_index, &items);
	if (ret)
		return ret;

	return 0;
}

static int k400_allocate(struct hid_device *hdev)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);
	struct k400_private_data *k400;

	k400 = devm_kzalloc(&hdev->dev, sizeof(struct k400_private_data),
			    GFP_KERNEL);
	if (!k400)
		return -ENOMEM;

	hidpp->private_data = k400;

	return 0;
};

static int k400_connect(struct hid_device *hdev, bool connected)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);

	if (!disable_tap_to_click)
		return 0;

	return k400_disable_tap_to_click(hidpp);
}

/* ------------------------------------------------------------------------- */
/* Logitech G920 Driving Force Racing Wheel for Xbox One                     */
/* ------------------------------------------------------------------------- */

#define HIDPP_PAGE_G920_FORCE_FEEDBACK			0x8123

static int g920_ff_set_autocenter(struct hidpp_device *hidpp,
				  struct hidpp_ff_private_data *data)
{
	struct hidpp_report response;
	u8 params[HIDPP_AUTOCENTER_PARAMS_LENGTH] = {
		[1] = HIDPP_FF_EFFECT_SPRING | HIDPP_FF_EFFECT_AUTOSTART,
	};
	int ret;

	/* initialize with zero autocenter to get wheel in usable state */

	dbg_hid("Setting autocenter to 0.\n");
	ret = hidpp_send_fap_command_sync(hidpp, data->feature_index,
					  HIDPP_FF_DOWNLOAD_EFFECT,
					  params, ARRAY_SIZE(params),
					  &response);
	if (ret)
		hid_warn(hidpp->hid_dev, "Failed to autocenter device!\n");
	else
		data->slot_autocenter = response.fap.params[0];

	return ret;
}

static int g920_get_config(struct hidpp_device *hidpp,
			   struct hidpp_ff_private_data *data)
{
	struct hidpp_report response;
	u8 feature_type;
	int ret;

	memset(data, 0, sizeof(*data));

	/* Find feature and store for later use */
	ret = hidpp_root_get_feature(hidpp, HIDPP_PAGE_G920_FORCE_FEEDBACK,
				     &data->feature_index, &feature_type);
	if (ret)
		return ret;

	/* Read number of slots available in device */
	ret = hidpp_send_fap_command_sync(hidpp, data->feature_index,
					  HIDPP_FF_GET_INFO,
					  NULL, 0,
					  &response);
	if (ret) {
		if (ret < 0)
			return ret;
		hid_err(hidpp->hid_dev,
			"%s: received protocol error 0x%02x\n", __func__, ret);
		return -EPROTO;
	}

	data->num_effects = response.fap.params[0] - HIDPP_FF_RESERVED_SLOTS;

	/* reset all forces */
	ret = hidpp_send_fap_command_sync(hidpp, data->feature_index,
					  HIDPP_FF_RESET_ALL,
					  NULL, 0,
					  &response);
	if (ret)
		hid_warn(hidpp->hid_dev, "Failed to reset all forces!\n");

	ret = hidpp_send_fap_command_sync(hidpp, data->feature_index,
					  HIDPP_FF_GET_APERTURE,
					  NULL, 0,
					  &response);
	if (ret) {
		hid_warn(hidpp->hid_dev,
			 "Failed to read range from device!\n");
	}
	data->range = ret ?
		900 : get_unaligned_be16(&response.fap.params[0]);

	/* Read the current gain values */
	ret = hidpp_send_fap_command_sync(hidpp, data->feature_index,
					  HIDPP_FF_GET_GLOBAL_GAINS,
					  NULL, 0,
					  &response);
	if (ret)
		hid_warn(hidpp->hid_dev,
			 "Failed to read gain values from device!\n");
	data->gain = ret ?
		0xffff : get_unaligned_be16(&response.fap.params[0]);

	/* ignore boost value at response.fap.params[2] */

	return g920_ff_set_autocenter(hidpp, data);
}

/* -------------------------------------------------------------------------- */
/* Logitech Dinovo Mini keyboard with builtin touchpad                        */
/* -------------------------------------------------------------------------- */
#define DINOVO_MINI_PRODUCT_ID		0xb30c

static int lg_dinovo_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_LOGIVENDOR)
		return 0;

	switch (usage->hid & HID_USAGE) {
	case 0x00d: lg_map_key_clear(KEY_MEDIA);	break;
	default:
		return 0;
	}
	return 1;
}

/* -------------------------------------------------------------------------- */
/* HID++1.0 devices which use HID++ reports for their wheels                  */
/* -------------------------------------------------------------------------- */
static int hidpp10_wheel_connect(struct hidpp_device *hidpp)
{
	return hidpp10_set_register(hidpp, HIDPP_REG_ENABLE_REPORTS, 0,
			HIDPP_ENABLE_WHEEL_REPORT | HIDPP_ENABLE_HWHEEL_REPORT,
			HIDPP_ENABLE_WHEEL_REPORT | HIDPP_ENABLE_HWHEEL_REPORT);
}

static int hidpp10_wheel_raw_event(struct hidpp_device *hidpp,
				   u8 *data, int size)
{
	s8 value, hvalue;

	if (!hidpp->input)
		return -EINVAL;

	if (size < 7)
		return 0;

	if (data[0] != REPORT_ID_HIDPP_SHORT || data[2] != HIDPP_SUB_ID_ROLLER)
		return 0;

	value = data[3];
	hvalue = data[4];

	input_report_rel(hidpp->input, REL_WHEEL, value);
	input_report_rel(hidpp->input, REL_WHEEL_HI_RES, value * 120);
	input_report_rel(hidpp->input, REL_HWHEEL, hvalue);
	input_report_rel(hidpp->input, REL_HWHEEL_HI_RES, hvalue * 120);
	input_sync(hidpp->input);

	return 1;
}

static void hidpp10_wheel_populate_input(struct hidpp_device *hidpp,
					 struct input_dev *input_dev)
{
	__set_bit(EV_REL, input_dev->evbit);
	__set_bit(REL_WHEEL, input_dev->relbit);
	__set_bit(REL_WHEEL_HI_RES, input_dev->relbit);
	__set_bit(REL_HWHEEL, input_dev->relbit);
	__set_bit(REL_HWHEEL_HI_RES, input_dev->relbit);
}

/* -------------------------------------------------------------------------- */
/* HID++1.0 mice which use HID++ reports for extra mouse buttons              */
/* -------------------------------------------------------------------------- */
static int hidpp10_extra_mouse_buttons_connect(struct hidpp_device *hidpp)
{
	return hidpp10_set_register(hidpp, HIDPP_REG_ENABLE_REPORTS, 0,
				    HIDPP_ENABLE_MOUSE_EXTRA_BTN_REPORT,
				    HIDPP_ENABLE_MOUSE_EXTRA_BTN_REPORT);
}

static int hidpp10_extra_mouse_buttons_raw_event(struct hidpp_device *hidpp,
				    u8 *data, int size)
{
	int i;

	if (!hidpp->input)
		return -EINVAL;

	if (size < 7)
		return 0;

	if (data[0] != REPORT_ID_HIDPP_SHORT ||
	    data[2] != HIDPP_SUB_ID_MOUSE_EXTRA_BTNS)
		return 0;

	/*
	 * Buttons are either delivered through the regular mouse report *or*
	 * through the extra buttons report. At least for button 6 how it is
	 * delivered differs per receiver firmware version. Even receivers with
	 * the same usb-id show different behavior, so we handle both cases.
	 */
	for (i = 0; i < 8; i++)
		input_report_key(hidpp->input, BTN_MOUSE + i,
				 (data[3] & (1 << i)));

	/* Some mice report events on button 9+, use BTN_MISC */
	for (i = 0; i < 8; i++)
		input_report_key(hidpp->input, BTN_MISC + i,
				 (data[4] & (1 << i)));

	input_sync(hidpp->input);
	return 1;
}

static void hidpp10_extra_mouse_buttons_populate_input(
			struct hidpp_device *hidpp, struct input_dev *input_dev)
{
	/* BTN_MOUSE - BTN_MOUSE+7 are set already by the descriptor */
	__set_bit(BTN_0, input_dev->keybit);
	__set_bit(BTN_1, input_dev->keybit);
	__set_bit(BTN_2, input_dev->keybit);
	__set_bit(BTN_3, input_dev->keybit);
	__set_bit(BTN_4, input_dev->keybit);
	__set_bit(BTN_5, input_dev->keybit);
	__set_bit(BTN_6, input_dev->keybit);
	__set_bit(BTN_7, input_dev->keybit);
}

/* -------------------------------------------------------------------------- */
/* HID++1.0 kbds which only report 0x10xx consumer usages through sub-id 0x03 */
/* -------------------------------------------------------------------------- */

/* Find the consumer-page input report desc and change Maximums to 0x107f */
static u8 *hidpp10_consumer_keys_report_fixup(struct hidpp_device *hidpp,
					      u8 *_rdesc, unsigned int *rsize)
{
	/* Note 0 terminated so we can use strnstr to search for this. */
	static const char consumer_rdesc_start[] = {
		0x05, 0x0C,	/* USAGE_PAGE (Consumer Devices)       */
		0x09, 0x01,	/* USAGE (Consumer Control)            */
		0xA1, 0x01,	/* COLLECTION (Application)            */
		0x85, 0x03,	/* REPORT_ID = 3                       */
		0x75, 0x10,	/* REPORT_SIZE (16)                    */
		0x95, 0x02,	/* REPORT_COUNT (2)                    */
		0x15, 0x01,	/* LOGICAL_MIN (1)                     */
		0x26, 0x00	/* LOGICAL_MAX (...                    */
	};
	char *consumer_rdesc, *rdesc = (char *)_rdesc;
	unsigned int size;

	consumer_rdesc = strnstr(rdesc, consumer_rdesc_start, *rsize);
	size = *rsize - (consumer_rdesc - rdesc);
	if (consumer_rdesc && size >= 25) {
		consumer_rdesc[15] = 0x7f;
		consumer_rdesc[16] = 0x10;
		consumer_rdesc[20] = 0x7f;
		consumer_rdesc[21] = 0x10;
	}
	return _rdesc;
}

static int hidpp10_consumer_keys_connect(struct hidpp_device *hidpp)
{
	return hidpp10_set_register(hidpp, HIDPP_REG_ENABLE_REPORTS, 0,
				    HIDPP_ENABLE_CONSUMER_REPORT,
				    HIDPP_ENABLE_CONSUMER_REPORT);
}

static int hidpp10_consumer_keys_raw_event(struct hidpp_device *hidpp,
					   u8 *data, int size)
{
	u8 consumer_report[5];

	if (size < 7)
		return 0;

	if (data[0] != REPORT_ID_HIDPP_SHORT ||
	    data[2] != HIDPP_SUB_ID_CONSUMER_VENDOR_KEYS)
		return 0;

	/*
	 * Build a normal consumer report (3) out of the data, this detour
	 * is necessary to get some keyboards to report their 0x10xx usages.
	 */
	consumer_report[0] = 0x03;
	memcpy(&consumer_report[1], &data[3], 4);
	/* We are called from atomic context */
	hid_report_raw_event(hidpp->hid_dev, HID_INPUT_REPORT,
			     consumer_report, 5, 1);

	return 1;
}

/* -------------------------------------------------------------------------- */
/* High-resolution scroll wheels                                              */
/* -------------------------------------------------------------------------- */

static int hi_res_scroll_enable(struct hidpp_device *hidpp)
{
	int ret;
	u8 multiplier = 1;

	if (hidpp->capabilities & HIDPP_CAPABILITY_HIDPP20_HI_RES_WHEEL) {
		ret = hidpp_hrw_set_wheel_mode(hidpp, false, true, false);
		if (ret == 0)
			ret = hidpp_hrw_get_wheel_capability(hidpp, &multiplier);
	} else if (hidpp->capabilities & HIDPP_CAPABILITY_HIDPP20_HI_RES_SCROLL) {
		ret = hidpp_hrs_set_highres_scrolling_mode(hidpp, true,
							   &multiplier);
	} else /* if (hidpp->capabilities & HIDPP_CAPABILITY_HIDPP10_FAST_SCROLL) */ {
		ret = hidpp10_enable_scrolling_acceleration(hidpp);
		multiplier = 8;
	}
	if (ret)
		return ret;

	if (multiplier == 0)
		multiplier = 1;

	hidpp->vertical_wheel_counter.wheel_multiplier = multiplier;
	hid_dbg(hidpp->hid_dev, "wheel multiplier = %d\n", multiplier);
	return 0;
}

static int hidpp_initialize_hires_scroll(struct hidpp_device *hidpp)
{
	int ret;
	unsigned long capabilities;

	capabilities = hidpp->capabilities;

	if (hidpp->protocol_major >= 2) {
		u8 feature_index;
		u8 feature_type;

		ret = hidpp_root_get_feature(hidpp, HIDPP_PAGE_HIRES_WHEEL,
					     &feature_index, &feature_type);
		if (!ret) {
			hidpp->capabilities |= HIDPP_CAPABILITY_HIDPP20_HI_RES_WHEEL;
			hid_dbg(hidpp->hid_dev, "Detected HID++ 2.0 hi-res scroll wheel\n");
			return 0;
		}
		ret = hidpp_root_get_feature(hidpp, HIDPP_PAGE_HI_RESOLUTION_SCROLLING,
					     &feature_index, &feature_type);
		if (!ret) {
			hidpp->capabilities |= HIDPP_CAPABILITY_HIDPP20_HI_RES_SCROLL;
			hid_dbg(hidpp->hid_dev, "Detected HID++ 2.0 hi-res scrolling\n");
		}
	} else {
		struct hidpp_report response;

		ret = hidpp_send_rap_command_sync(hidpp,
						  REPORT_ID_HIDPP_SHORT,
						  HIDPP_GET_REGISTER,
						  HIDPP_ENABLE_FAST_SCROLL,
						  NULL, 0, &response);
		if (!ret) {
			hidpp->capabilities |= HIDPP_CAPABILITY_HIDPP10_FAST_SCROLL;
			hid_dbg(hidpp->hid_dev, "Detected HID++ 1.0 fast scroll\n");
		}
	}

	if (hidpp->capabilities == capabilities)
		hid_dbg(hidpp->hid_dev, "Did not detect HID++ hi-res scrolling hardware support\n");
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Generic HID++ devices                                                      */
/* -------------------------------------------------------------------------- */

static u8 *hidpp_report_fixup(struct hid_device *hdev, u8 *rdesc,
			      unsigned int *rsize)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);

	if (!hidpp)
		return rdesc;

	/* For 27 MHz keyboards the quirk gets set after hid_parse. */
	if (hdev->group == HID_GROUP_LOGITECH_27MHZ_DEVICE ||
	    (hidpp->quirks & HIDPP_QUIRK_HIDPP_CONSUMER_VENDOR_KEYS))
		rdesc = hidpp10_consumer_keys_report_fixup(hidpp, rdesc, rsize);

	return rdesc;
}

static int hidpp_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);

	if (!hidpp)
		return 0;

	if (hidpp->quirks & HIDPP_QUIRK_CLASS_WTP)
		return wtp_input_mapping(hdev, hi, field, usage, bit, max);
	else if (hidpp->quirks & HIDPP_QUIRK_CLASS_M560 &&
			field->application != HID_GD_MOUSE)
		return m560_input_mapping(hdev, hi, field, usage, bit, max);

	if (hdev->product == DINOVO_MINI_PRODUCT_ID)
		return lg_dinovo_input_mapping(hdev, hi, field, usage, bit, max);

	return 0;
}

static int hidpp_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);

	if (!hidpp)
		return 0;

	/* Ensure that Logitech G920 is not given a default fuzz/flat value */
	if (hidpp->quirks & HIDPP_QUIRK_CLASS_G920) {
		if (usage->type == EV_ABS && (usage->code == ABS_X ||
				usage->code == ABS_Y || usage->code == ABS_Z ||
				usage->code == ABS_RZ)) {
			field->application = HID_GD_MULTIAXIS;
		}
	}

	return 0;
}


static void hidpp_populate_input(struct hidpp_device *hidpp,
				 struct input_dev *input)
{
	hidpp->input = input;

	if (hidpp->quirks & HIDPP_QUIRK_CLASS_WTP)
		wtp_populate_input(hidpp, input);
	else if (hidpp->quirks & HIDPP_QUIRK_CLASS_M560)
		m560_populate_input(hidpp, input);

	if (hidpp->quirks & HIDPP_QUIRK_HIDPP_WHEELS)
		hidpp10_wheel_populate_input(hidpp, input);

	if (hidpp->quirks & HIDPP_QUIRK_HIDPP_EXTRA_MOUSE_BTNS)
		hidpp10_extra_mouse_buttons_populate_input(hidpp, input);
}

static int hidpp_input_configured(struct hid_device *hdev,
				struct hid_input *hidinput)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);
	struct input_dev *input = hidinput->input;

	if (!hidpp)
		return 0;

	hidpp_populate_input(hidpp, input);

	return 0;
}

static int hidpp_raw_hidpp_event(struct hidpp_device *hidpp, u8 *data,
		int size)
{
	struct hidpp_report *question = hidpp->send_receive_buf;
	struct hidpp_report *answer = hidpp->send_receive_buf;
	struct hidpp_report *report = (struct hidpp_report *)data;
	int ret;

	/*
	 * If the mutex is locked then we have a pending answer from a
	 * previously sent command.
	 */
	if (unlikely(mutex_is_locked(&hidpp->send_mutex))) {
		/*
		 * Check for a correct hidpp20 answer or the corresponding
		 * error
		 */
		if (hidpp_match_answer(question, report) ||
				hidpp_match_error(question, report)) {
			*answer = *report;
			hidpp->answer_available = true;
			wake_up(&hidpp->wait);
			/*
			 * This was an answer to a command that this driver sent
			 * We return 1 to hid-core to avoid forwarding the
			 * command upstream as it has been treated by the driver
			 */

			return 1;
		}
	}

	if (unlikely(hidpp_report_is_connect_event(hidpp, report))) {
		atomic_set(&hidpp->connected,
				!(report->rap.params[0] & (1 << 6)));
		if (schedule_work(&hidpp->work) == 0)
			dbg_hid("%s: connect event already queued\n", __func__);
		return 1;
	}

	if (hidpp->hid_dev->group == HID_GROUP_LOGITECH_27MHZ_DEVICE &&
	    data[0] == REPORT_ID_HIDPP_SHORT &&
	    data[2] == HIDPP_SUB_ID_USER_IFACE_EVENT &&
	    (data[3] & HIDPP_USER_IFACE_EVENT_ENCRYPTION_KEY_LOST)) {
		dev_err_ratelimited(&hidpp->hid_dev->dev,
			"Error the keyboard's wireless encryption key has been lost, your keyboard will not work unless you re-configure encryption.\n");
		dev_err_ratelimited(&hidpp->hid_dev->dev,
			"See: https://gitlab.freedesktop.org/jwrdegoede/logitech-27mhz-keyboard-encryption-setup/\n");
	}

	if (hidpp->capabilities & HIDPP_CAPABILITY_HIDPP20_BATTERY) {
		ret = hidpp20_battery_event_1000(hidpp, data, size);
		if (ret != 0)
			return ret;
		ret = hidpp20_battery_event_1004(hidpp, data, size);
		if (ret != 0)
			return ret;
		ret = hidpp_solar_battery_event(hidpp, data, size);
		if (ret != 0)
			return ret;
		ret = hidpp20_battery_voltage_event(hidpp, data, size);
		if (ret != 0)
			return ret;
	}

	if (hidpp->capabilities & HIDPP_CAPABILITY_HIDPP10_BATTERY) {
		ret = hidpp10_battery_event(hidpp, data, size);
		if (ret != 0)
			return ret;
	}

	if (hidpp->quirks & HIDPP_QUIRK_HIDPP_WHEELS) {
		ret = hidpp10_wheel_raw_event(hidpp, data, size);
		if (ret != 0)
			return ret;
	}

	if (hidpp->quirks & HIDPP_QUIRK_HIDPP_EXTRA_MOUSE_BTNS) {
		ret = hidpp10_extra_mouse_buttons_raw_event(hidpp, data, size);
		if (ret != 0)
			return ret;
	}

	if (hidpp->quirks & HIDPP_QUIRK_HIDPP_CONSUMER_VENDOR_KEYS) {
		ret = hidpp10_consumer_keys_raw_event(hidpp, data, size);
		if (ret != 0)
			return ret;
	}

	return 0;
}

static int hidpp_raw_event(struct hid_device *hdev, struct hid_report *report,
		u8 *data, int size)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);
	int ret = 0;

	if (!hidpp)
		return 0;

	/* Generic HID++ processing. */
	switch (data[0]) {
	case REPORT_ID_HIDPP_VERY_LONG:
		if (size != hidpp->very_long_report_length) {
			hid_err(hdev, "received hid++ report of bad size (%d)",
				size);
			return 1;
		}
		ret = hidpp_raw_hidpp_event(hidpp, data, size);
		break;
	case REPORT_ID_HIDPP_LONG:
		if (size != HIDPP_REPORT_LONG_LENGTH) {
			hid_err(hdev, "received hid++ report of bad size (%d)",
				size);
			return 1;
		}
		ret = hidpp_raw_hidpp_event(hidpp, data, size);
		break;
	case REPORT_ID_HIDPP_SHORT:
		if (size != HIDPP_REPORT_SHORT_LENGTH) {
			hid_err(hdev, "received hid++ report of bad size (%d)",
				size);
			return 1;
		}
		ret = hidpp_raw_hidpp_event(hidpp, data, size);
		break;
	}

	/* If no report is available for further processing, skip calling
	 * raw_event of subclasses. */
	if (ret != 0)
		return ret;

	if (hidpp->quirks & HIDPP_QUIRK_CLASS_WTP)
		return wtp_raw_event(hdev, data, size);
	else if (hidpp->quirks & HIDPP_QUIRK_CLASS_M560)
		return m560_raw_event(hdev, data, size);

	return 0;
}

static int hidpp_event(struct hid_device *hdev, struct hid_field *field,
	struct hid_usage *usage, __s32 value)
{
	/* This function will only be called for scroll events, due to the
	 * restriction imposed in hidpp_usages.
	 */
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);
	struct hidpp_scroll_counter *counter;

	if (!hidpp)
		return 0;

	counter = &hidpp->vertical_wheel_counter;
	/* A scroll event may occur before the multiplier has been retrieved or
	 * the input device set, or high-res scroll enabling may fail. In such
	 * cases we must return early (falling back to default behaviour) to
	 * avoid a crash in hidpp_scroll_counter_handle_scroll.
	 */
	if (!(hidpp->capabilities & HIDPP_CAPABILITY_HI_RES_SCROLL)
	    || value == 0 || hidpp->input == NULL
	    || counter->wheel_multiplier == 0)
		return 0;

	hidpp_scroll_counter_handle_scroll(hidpp->input, counter, value);
	return 1;
}

static int hidpp_initialize_battery(struct hidpp_device *hidpp)
{
	static atomic_t battery_no = ATOMIC_INIT(0);
	struct power_supply_config cfg = { .drv_data = hidpp };
	struct power_supply_desc *desc = &hidpp->battery.desc;
	enum power_supply_property *battery_props;
	struct hidpp_battery *battery;
	unsigned int num_battery_props;
	unsigned long n;
	int ret;

	if (hidpp->battery.ps)
		return 0;

	hidpp->battery.feature_index = 0xff;
	hidpp->battery.solar_feature_index = 0xff;
	hidpp->battery.voltage_feature_index = 0xff;

	if (hidpp->protocol_major >= 2) {
		if (hidpp->quirks & HIDPP_QUIRK_CLASS_K750)
			ret = hidpp_solar_request_battery_event(hidpp);
		else {
			/* we only support one battery feature right now, so let's
			   first check the ones that support battery level first
			   and leave voltage for last */
			ret = hidpp20_query_battery_info_1000(hidpp);
			if (ret)
				ret = hidpp20_query_battery_info_1004(hidpp);
			if (ret)
				ret = hidpp20_query_battery_voltage_info(hidpp);
		}

		if (ret)
			return ret;
		hidpp->capabilities |= HIDPP_CAPABILITY_HIDPP20_BATTERY;
	} else {
		ret = hidpp10_query_battery_status(hidpp);
		if (ret) {
			ret = hidpp10_query_battery_mileage(hidpp);
			if (ret)
				return -ENOENT;
			hidpp->capabilities |= HIDPP_CAPABILITY_BATTERY_MILEAGE;
		} else {
			hidpp->capabilities |= HIDPP_CAPABILITY_BATTERY_LEVEL_STATUS;
		}
		hidpp->capabilities |= HIDPP_CAPABILITY_HIDPP10_BATTERY;
	}

	battery_props = devm_kmemdup(&hidpp->hid_dev->dev,
				     hidpp_battery_props,
				     sizeof(hidpp_battery_props),
				     GFP_KERNEL);
	if (!battery_props)
		return -ENOMEM;

	num_battery_props = ARRAY_SIZE(hidpp_battery_props) - 3;

	if (hidpp->capabilities & HIDPP_CAPABILITY_BATTERY_MILEAGE ||
	    hidpp->capabilities & HIDPP_CAPABILITY_BATTERY_PERCENTAGE ||
	    hidpp->capabilities & HIDPP_CAPABILITY_BATTERY_VOLTAGE)
		battery_props[num_battery_props++] =
				POWER_SUPPLY_PROP_CAPACITY;

	if (hidpp->capabilities & HIDPP_CAPABILITY_BATTERY_LEVEL_STATUS)
		battery_props[num_battery_props++] =
				POWER_SUPPLY_PROP_CAPACITY_LEVEL;

	if (hidpp->capabilities & HIDPP_CAPABILITY_BATTERY_VOLTAGE)
		battery_props[num_battery_props++] =
			POWER_SUPPLY_PROP_VOLTAGE_NOW;

	battery = &hidpp->battery;

	n = atomic_inc_return(&battery_no) - 1;
	desc->properties = battery_props;
	desc->num_properties = num_battery_props;
	desc->get_property = hidpp_battery_get_property;
	sprintf(battery->name, "hidpp_battery_%ld", n);
	desc->name = battery->name;
	desc->type = POWER_SUPPLY_TYPE_BATTERY;
	desc->use_for_apm = 0;

	battery->ps = devm_power_supply_register(&hidpp->hid_dev->dev,
						 &battery->desc,
						 &cfg);
	if (IS_ERR(battery->ps))
		return PTR_ERR(battery->ps);

	power_supply_powers(battery->ps, &hidpp->hid_dev->dev);

	return ret;
}

static void hidpp_overwrite_name(struct hid_device *hdev)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);
	char *name;

	if (hidpp->protocol_major < 2)
		return;

	name = hidpp_get_device_name(hidpp);

	if (!name) {
		hid_err(hdev, "unable to retrieve the name of the device");
	} else {
		dbg_hid("HID++: Got name: %s\n", name);
		snprintf(hdev->name, sizeof(hdev->name), "%s", name);
	}

	kfree(name);
}

static int hidpp_input_open(struct input_dev *dev)
{
	struct hid_device *hid = input_get_drvdata(dev);

	return hid_hw_open(hid);
}

static void hidpp_input_close(struct input_dev *dev)
{
	struct hid_device *hid = input_get_drvdata(dev);

	hid_hw_close(hid);
}

static struct input_dev *hidpp_allocate_input(struct hid_device *hdev)
{
	struct input_dev *input_dev = devm_input_allocate_device(&hdev->dev);
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);

	if (!input_dev)
		return NULL;

	input_set_drvdata(input_dev, hdev);
	input_dev->open = hidpp_input_open;
	input_dev->close = hidpp_input_close;

	input_dev->name = hidpp->name;
	input_dev->phys = hdev->phys;
	input_dev->uniq = hdev->uniq;
	input_dev->id.bustype = hdev->bus;
	input_dev->id.vendor  = hdev->vendor;
	input_dev->id.product = hdev->product;
	input_dev->id.version = hdev->version;
	input_dev->dev.parent = &hdev->dev;

	return input_dev;
}

static void hidpp_connect_event(struct hidpp_device *hidpp)
{
	struct hid_device *hdev = hidpp->hid_dev;
	int ret = 0;
	bool connected = atomic_read(&hidpp->connected);
	struct input_dev *input;
	char *name, *devm_name;

	if (!connected) {
		if (hidpp->battery.ps) {
			hidpp->battery.online = false;
			hidpp->battery.status = POWER_SUPPLY_STATUS_UNKNOWN;
			hidpp->battery.level = POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
			power_supply_changed(hidpp->battery.ps);
		}
		return;
	}

	if (hidpp->quirks & HIDPP_QUIRK_CLASS_WTP) {
		ret = wtp_connect(hdev, connected);
		if (ret)
			return;
	} else if (hidpp->quirks & HIDPP_QUIRK_CLASS_M560) {
		ret = m560_send_config_command(hdev, connected);
		if (ret)
			return;
	} else if (hidpp->quirks & HIDPP_QUIRK_CLASS_K400) {
		ret = k400_connect(hdev, connected);
		if (ret)
			return;
	}

	if (hidpp->quirks & HIDPP_QUIRK_HIDPP_WHEELS) {
		ret = hidpp10_wheel_connect(hidpp);
		if (ret)
			return;
	}

	if (hidpp->quirks & HIDPP_QUIRK_HIDPP_EXTRA_MOUSE_BTNS) {
		ret = hidpp10_extra_mouse_buttons_connect(hidpp);
		if (ret)
			return;
	}

	if (hidpp->quirks & HIDPP_QUIRK_HIDPP_CONSUMER_VENDOR_KEYS) {
		ret = hidpp10_consumer_keys_connect(hidpp);
		if (ret)
			return;
	}

	/* the device is already connected, we can ask for its name and
	 * protocol */
	if (!hidpp->protocol_major) {
		ret = hidpp_root_get_protocol_version(hidpp);
		if (ret) {
			hid_err(hdev, "Can not get the protocol version.\n");
			return;
		}
	}

	if (hidpp->name == hdev->name && hidpp->protocol_major >= 2) {
		name = hidpp_get_device_name(hidpp);
		if (name) {
			devm_name = devm_kasprintf(&hdev->dev, GFP_KERNEL,
						   "%s", name);
			kfree(name);
			if (!devm_name)
				return;

			hidpp->name = devm_name;
		}
	}

	hidpp_initialize_battery(hidpp);
	hidpp_initialize_hires_scroll(hidpp);

	/* forward current battery state */
	if (hidpp->capabilities & HIDPP_CAPABILITY_HIDPP10_BATTERY) {
		hidpp10_enable_battery_reporting(hidpp);
		if (hidpp->capabilities & HIDPP_CAPABILITY_BATTERY_MILEAGE)
			hidpp10_query_battery_mileage(hidpp);
		else
			hidpp10_query_battery_status(hidpp);
	} else if (hidpp->capabilities & HIDPP_CAPABILITY_HIDPP20_BATTERY) {
		if (hidpp->capabilities & HIDPP_CAPABILITY_BATTERY_VOLTAGE)
			hidpp20_query_battery_voltage_info(hidpp);
		else if (hidpp->capabilities & HIDPP_CAPABILITY_UNIFIED_BATTERY)
			hidpp20_query_battery_info_1004(hidpp);
		else
			hidpp20_query_battery_info_1000(hidpp);
	}
	if (hidpp->battery.ps)
		power_supply_changed(hidpp->battery.ps);

	if (hidpp->capabilities & HIDPP_CAPABILITY_HI_RES_SCROLL)
		hi_res_scroll_enable(hidpp);

	if (!(hidpp->quirks & HIDPP_QUIRK_NO_HIDINPUT) || hidpp->delayed_input)
		/* if the input nodes are already created, we can stop now */
		return;

	input = hidpp_allocate_input(hdev);
	if (!input) {
		hid_err(hdev, "cannot allocate new input device: %d\n", ret);
		return;
	}

	hidpp_populate_input(hidpp, input);

	ret = input_register_device(input);
	if (ret) {
		input_free_device(input);
		return;
	}

	hidpp->delayed_input = input;
}

static DEVICE_ATTR(builtin_power_supply, 0000, NULL, NULL);

static struct attribute *sysfs_attrs[] = {
	&dev_attr_builtin_power_supply.attr,
	NULL
};

static const struct attribute_group ps_attribute_group = {
	.attrs = sysfs_attrs
};

static int hidpp_get_report_length(struct hid_device *hdev, int id)
{
	struct hid_report_enum *re;
	struct hid_report *report;

	re = &(hdev->report_enum[HID_OUTPUT_REPORT]);
	report = re->report_id_hash[id];
	if (!report)
		return 0;

	return report->field[0]->report_count + 1;
}

static u8 hidpp_validate_device(struct hid_device *hdev)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);
	int id, report_length;
	u8 supported_reports = 0;

	id = REPORT_ID_HIDPP_SHORT;
	report_length = hidpp_get_report_length(hdev, id);
	if (report_length) {
		if (report_length < HIDPP_REPORT_SHORT_LENGTH)
			goto bad_device;

		supported_reports |= HIDPP_REPORT_SHORT_SUPPORTED;
	}

	id = REPORT_ID_HIDPP_LONG;
	report_length = hidpp_get_report_length(hdev, id);
	if (report_length) {
		if (report_length < HIDPP_REPORT_LONG_LENGTH)
			goto bad_device;

		supported_reports |= HIDPP_REPORT_LONG_SUPPORTED;
	}

	id = REPORT_ID_HIDPP_VERY_LONG;
	report_length = hidpp_get_report_length(hdev, id);
	if (report_length) {
		if (report_length < HIDPP_REPORT_LONG_LENGTH ||
		    report_length > HIDPP_REPORT_VERY_LONG_MAX_LENGTH)
			goto bad_device;

		supported_reports |= HIDPP_REPORT_VERY_LONG_SUPPORTED;
		hidpp->very_long_report_length = report_length;
	}

	return supported_reports;

bad_device:
	hid_warn(hdev, "not enough values in hidpp report %d\n", id);
	return false;
}

static bool hidpp_application_equals(struct hid_device *hdev,
				     unsigned int application)
{
	struct list_head *report_list;
	struct hid_report *report;

	report_list = &hdev->report_enum[HID_INPUT_REPORT].report_list;
	report = list_first_entry_or_null(report_list, struct hid_report, list);
	return report && report->application == application;
}

static int hidpp_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct hidpp_device *hidpp;
	int ret;
	bool connected;
	unsigned int connect_mask = HID_CONNECT_DEFAULT;
	struct hidpp_ff_private_data data;

	/* report_fixup needs drvdata to be set before we call hid_parse */
	hidpp = devm_kzalloc(&hdev->dev, sizeof(*hidpp), GFP_KERNEL);
	if (!hidpp)
		return -ENOMEM;

	hidpp->hid_dev = hdev;
	hidpp->name = hdev->name;
	hidpp->quirks = id->driver_data;
	hid_set_drvdata(hdev, hidpp);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "%s:parse failed\n", __func__);
		return ret;
	}

	/*
	 * Make sure the device is HID++ capable, otherwise treat as generic HID
	 */
	hidpp->supported_reports = hidpp_validate_device(hdev);

	if (!hidpp->supported_reports) {
		hid_set_drvdata(hdev, NULL);
		devm_kfree(&hdev->dev, hidpp);
		return hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	}

	if (id->group == HID_GROUP_LOGITECH_DJ_DEVICE)
		hidpp->quirks |= HIDPP_QUIRK_UNIFYING;

	if (id->group == HID_GROUP_LOGITECH_27MHZ_DEVICE &&
	    hidpp_application_equals(hdev, HID_GD_MOUSE))
		hidpp->quirks |= HIDPP_QUIRK_HIDPP_WHEELS |
				 HIDPP_QUIRK_HIDPP_EXTRA_MOUSE_BTNS;

	if (id->group == HID_GROUP_LOGITECH_27MHZ_DEVICE &&
	    hidpp_application_equals(hdev, HID_GD_KEYBOARD))
		hidpp->quirks |= HIDPP_QUIRK_HIDPP_CONSUMER_VENDOR_KEYS;

	if (disable_raw_mode) {
		hidpp->quirks &= ~HIDPP_QUIRK_CLASS_WTP;
		hidpp->quirks &= ~HIDPP_QUIRK_NO_HIDINPUT;
	}

	if (hidpp->quirks & HIDPP_QUIRK_CLASS_WTP) {
		ret = wtp_allocate(hdev, id);
		if (ret)
			return ret;
	} else if (hidpp->quirks & HIDPP_QUIRK_CLASS_K400) {
		ret = k400_allocate(hdev);
		if (ret)
			return ret;
	}

	INIT_WORK(&hidpp->work, delayed_work_cb);
	mutex_init(&hidpp->send_mutex);
	init_waitqueue_head(&hidpp->wait);

	/* indicates we are handling the battery properties in the kernel */
	ret = sysfs_create_group(&hdev->dev.kobj, &ps_attribute_group);
	if (ret)
		hid_warn(hdev, "Cannot allocate sysfs group for %s\n",
			 hdev->name);

	/*
	 * Plain USB connections need to actually call start and open
	 * on the transport driver to allow incoming data.
	 */
	ret = hid_hw_start(hdev, 0);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto hid_hw_start_fail;
	}

	ret = hid_hw_open(hdev);
	if (ret < 0) {
		dev_err(&hdev->dev, "%s:hid_hw_open returned error:%d\n",
			__func__, ret);
		goto hid_hw_open_fail;
	}

	/* Allow incoming packets */
	hid_device_io_start(hdev);

	if (hidpp->quirks & HIDPP_QUIRK_UNIFYING)
		hidpp_unifying_init(hidpp);

	connected = hidpp_root_get_protocol_version(hidpp) == 0;
	atomic_set(&hidpp->connected, connected);
	if (!(hidpp->quirks & HIDPP_QUIRK_UNIFYING)) {
		if (!connected) {
			ret = -ENODEV;
			hid_err(hdev, "Device not connected");
			goto hid_hw_init_fail;
		}

		hidpp_overwrite_name(hdev);
	}

	if (connected && hidpp->protocol_major >= 2) {
		ret = hidpp_set_wireless_feature_index(hidpp);
		if (ret == -ENOENT)
			hidpp->wireless_feature_index = 0;
		else if (ret)
			goto hid_hw_init_fail;
	}

	if (connected && (hidpp->quirks & HIDPP_QUIRK_CLASS_WTP)) {
		ret = wtp_get_config(hidpp);
		if (ret)
			goto hid_hw_init_fail;
	} else if (connected && (hidpp->quirks & HIDPP_QUIRK_CLASS_G920)) {
		ret = g920_get_config(hidpp, &data);
		if (ret)
			goto hid_hw_init_fail;
	}

	hidpp_connect_event(hidpp);

	/* Reset the HID node state */
	hid_device_io_stop(hdev);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);

	if (hidpp->quirks & HIDPP_QUIRK_NO_HIDINPUT)
		connect_mask &= ~HID_CONNECT_HIDINPUT;

	/* Now export the actual inputs and hidraw nodes to the world */
	ret = hid_hw_start(hdev, connect_mask);
	if (ret) {
		hid_err(hdev, "%s:hid_hw_start returned error\n", __func__);
		goto hid_hw_start_fail;
	}

	if (hidpp->quirks & HIDPP_QUIRK_CLASS_G920) {
		ret = hidpp_ff_init(hidpp, &data);
		if (ret)
			hid_warn(hidpp->hid_dev,
		     "Unable to initialize force feedback support, errno %d\n",
				 ret);
	}

	return ret;

hid_hw_init_fail:
	hid_hw_close(hdev);
hid_hw_open_fail:
	hid_hw_stop(hdev);
hid_hw_start_fail:
	sysfs_remove_group(&hdev->dev.kobj, &ps_attribute_group);
	cancel_work_sync(&hidpp->work);
	mutex_destroy(&hidpp->send_mutex);
	return ret;
}

static void hidpp_remove(struct hid_device *hdev)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);

	if (!hidpp)
		return hid_hw_stop(hdev);

	sysfs_remove_group(&hdev->dev.kobj, &ps_attribute_group);

	hid_hw_stop(hdev);
	cancel_work_sync(&hidpp->work);
	mutex_destroy(&hidpp->send_mutex);
}

static const struct hid_device_id unhandled_hidpp_devices[] = {
	/* Logitech Harmony Adapter for PS3, handled in hid-sony */
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_HARMONY_PS3) },
	/* Handled in hid-generic */
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_DINOVO_EDGE_KBD) },
	{}
};

static bool hidpp_match(struct hid_device *hdev,
			bool ignore_special_driver)
{
	/* Refuse to handle devices handled by other HID drivers */
	return !hid_match_id(hdev, unhandled_hidpp_devices);
}

#define LDJ_DEVICE(product) \
	HID_DEVICE(BUS_USB, HID_GROUP_LOGITECH_DJ_DEVICE, \
		   USB_VENDOR_ID_LOGITECH, (product))

#define L27MHZ_DEVICE(product) \
	HID_DEVICE(BUS_USB, HID_GROUP_LOGITECH_27MHZ_DEVICE, \
		   USB_VENDOR_ID_LOGITECH, (product))

static const struct hid_device_id hidpp_devices[] = {
	{ /* wireless touchpad */
	  LDJ_DEVICE(0x4011),
	  .driver_data = HIDPP_QUIRK_CLASS_WTP | HIDPP_QUIRK_DELAYED_INIT |
			 HIDPP_QUIRK_WTP_PHYSICAL_BUTTONS },
	{ /* wireless touchpad T650 */
	  LDJ_DEVICE(0x4101),
	  .driver_data = HIDPP_QUIRK_CLASS_WTP | HIDPP_QUIRK_DELAYED_INIT },
	{ /* wireless touchpad T651 */
	  HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_LOGITECH,
		USB_DEVICE_ID_LOGITECH_T651),
	  .driver_data = HIDPP_QUIRK_CLASS_WTP },
	{ /* Mouse logitech M560 */
	  LDJ_DEVICE(0x402d),
	  .driver_data = HIDPP_QUIRK_DELAYED_INIT | HIDPP_QUIRK_CLASS_M560 },
	{ /* Keyboard logitech K400 */
	  LDJ_DEVICE(0x4024),
	  .driver_data = HIDPP_QUIRK_CLASS_K400 },
	{ /* Solar Keyboard Logitech K750 */
	  LDJ_DEVICE(0x4002),
	  .driver_data = HIDPP_QUIRK_CLASS_K750 },
	{ /* Keyboard MX5000 (Bluetooth-receiver in HID proxy mode) */
	  LDJ_DEVICE(0xb305),
	  .driver_data = HIDPP_QUIRK_HIDPP_CONSUMER_VENDOR_KEYS },
	{ /* Dinovo Edge (Bluetooth-receiver in HID proxy mode) */
	  LDJ_DEVICE(0xb309),
	  .driver_data = HIDPP_QUIRK_HIDPP_CONSUMER_VENDOR_KEYS },
	{ /* Keyboard MX5500 (Bluetooth-receiver in HID proxy mode) */
	  LDJ_DEVICE(0xb30b),
	  .driver_data = HIDPP_QUIRK_HIDPP_CONSUMER_VENDOR_KEYS },

	{ LDJ_DEVICE(HID_ANY_ID) },

	{ /* Keyboard LX501 (Y-RR53) */
	  L27MHZ_DEVICE(0x0049),
	  .driver_data = HIDPP_QUIRK_KBD_ZOOM_WHEEL },
	{ /* Keyboard MX3000 (Y-RAM74) */
	  L27MHZ_DEVICE(0x0057),
	  .driver_data = HIDPP_QUIRK_KBD_SCROLL_WHEEL },
	{ /* Keyboard MX3200 (Y-RAV80) */
	  L27MHZ_DEVICE(0x005c),
	  .driver_data = HIDPP_QUIRK_KBD_ZOOM_WHEEL },
	{ /* S510 Media Remote */
	  L27MHZ_DEVICE(0x00fe),
	  .driver_data = HIDPP_QUIRK_KBD_SCROLL_WHEEL },

	{ L27MHZ_DEVICE(HID_ANY_ID) },

	{ /* Logitech G403 Wireless Gaming Mouse over USB */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, 0xC082) },
	{ /* Logitech G703 Gaming Mouse over USB */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, 0xC087) },
	{ /* Logitech G703 Hero Gaming Mouse over USB */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, 0xC090) },
	{ /* Logitech G900 Gaming Mouse over USB */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, 0xC081) },
	{ /* Logitech G903 Gaming Mouse over USB */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, 0xC086) },
	{ /* Logitech G903 Hero Gaming Mouse over USB */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, 0xC091) },
	{ /* Logitech G920 Wheel over USB */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_G920_WHEEL),
		.driver_data = HIDPP_QUIRK_CLASS_G920 | HIDPP_QUIRK_FORCE_OUTPUT_REPORTS},
	{ /* Logitech G Pro Gaming Mouse over USB */
	  HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, 0xC088) },

	{ /* MX5000 keyboard over Bluetooth */
	  HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_LOGITECH, 0xb305),
	  .driver_data = HIDPP_QUIRK_HIDPP_CONSUMER_VENDOR_KEYS },
	{ /* Dinovo Edge keyboard over Bluetooth */
	  HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_LOGITECH, 0xb309),
	  .driver_data = HIDPP_QUIRK_HIDPP_CONSUMER_VENDOR_KEYS },
	{ /* MX5500 keyboard over Bluetooth */
	  HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_LOGITECH, 0xb30b),
	  .driver_data = HIDPP_QUIRK_HIDPP_CONSUMER_VENDOR_KEYS },

	{ /* And try to enable HID++ for all the Logitech Bluetooth devices */
	  HID_DEVICE(BUS_BLUETOOTH, HID_GROUP_ANY, USB_VENDOR_ID_LOGITECH, HID_ANY_ID) },
	{}
};

MODULE_DEVICE_TABLE(hid, hidpp_devices);

static const struct hid_usage_id hidpp_usages[] = {
	{ HID_GD_WHEEL, EV_REL, REL_WHEEL_HI_RES },
	{ HID_ANY_ID - 1, HID_ANY_ID - 1, HID_ANY_ID - 1}
};

static struct hid_driver hidpp_driver = {
	.name = "logitech-hidpp-device",
	.id_table = hidpp_devices,
	.match = hidpp_match,
	.report_fixup = hidpp_report_fixup,
	.probe = hidpp_probe,
	.remove = hidpp_remove,
	.raw_event = hidpp_raw_event,
	.usage_table = hidpp_usages,
	.event = hidpp_event,
	.input_configured = hidpp_input_configured,
	.input_mapping = hidpp_input_mapping,
	.input_mapped = hidpp_input_mapped,
};

module_hid_driver(hidpp_driver);
