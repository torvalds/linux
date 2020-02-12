// SPDX-License-Identifier: GPL-2.0+
/*
 * HID driver for Valve Steam Controller
 *
 * Copyright (c) 2018 Rodrigo Rivas Costa <rodrigorivascosta@gmail.com>
 *
 * Supports both the wired and wireless interfaces.
 *
 * This controller has a builtin emulation of mouse and keyboard: the right pad
 * can be used as a mouse, the shoulder buttons are mouse buttons, A and B
 * buttons are ENTER and ESCAPE, and so on. This is implemented as additional
 * HID interfaces.
 *
 * This is known as the "lizard mode", because apparently lizards like to use
 * the computer from the coach, without a proper mouse and keyboard.
 *
 * This driver will disable the lizard mode when the input device is opened
 * and re-enable it when the input device is closed, so as not to break user
 * mode behaviour. The lizard_mode parameter can be used to change that.
 *
 * There are a few user space applications (notably Steam Client) that use
 * the hidraw interface directly to create input devices (XTest, uinput...).
 * In order to avoid breaking them this driver creates a layered hidraw device,
 * so it can detect when the client is running and then:
 *  - it will not send any command to the controller.
 *  - this input device will be removed, to avoid double input of the same
 *    user action.
 * When the client is closed, this input device will be created again.
 *
 * For additional functions, such as changing the right-pad margin or switching
 * the led, you can use the user-space tool at:
 *
 *   https://github.com/rodrigorc/steamctrl
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include "hid-ids.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rodrigo Rivas Costa <rodrigorivascosta@gmail.com>");

static bool lizard_mode = true;

static DEFINE_MUTEX(steam_devices_lock);
static LIST_HEAD(steam_devices);

#define STEAM_QUIRK_WIRELESS		BIT(0)

/* Touch pads are 40 mm in diameter and 65535 units */
#define STEAM_PAD_RESOLUTION 1638
/* Trigger runs are about 5 mm and 256 units */
#define STEAM_TRIGGER_RESOLUTION 51
/* Joystick runs are about 5 mm and 256 units */
#define STEAM_JOYSTICK_RESOLUTION 51

#define STEAM_PAD_FUZZ 256

/*
 * Commands that can be sent in a feature report.
 * Thanks to Valve for some valuable hints.
 */
#define STEAM_CMD_SET_MAPPINGS		0x80
#define STEAM_CMD_CLEAR_MAPPINGS	0x81
#define STEAM_CMD_GET_MAPPINGS		0x82
#define STEAM_CMD_GET_ATTRIB		0x83
#define STEAM_CMD_GET_ATTRIB_LABEL	0x84
#define STEAM_CMD_DEFAULT_MAPPINGS	0x85
#define STEAM_CMD_FACTORY_RESET		0x86
#define STEAM_CMD_WRITE_REGISTER	0x87
#define STEAM_CMD_CLEAR_REGISTER	0x88
#define STEAM_CMD_READ_REGISTER		0x89
#define STEAM_CMD_GET_REGISTER_LABEL	0x8a
#define STEAM_CMD_GET_REGISTER_MAX	0x8b
#define STEAM_CMD_GET_REGISTER_DEFAULT	0x8c
#define STEAM_CMD_SET_MODE		0x8d
#define STEAM_CMD_DEFAULT_MOUSE		0x8e
#define STEAM_CMD_FORCEFEEDBAK		0x8f
#define STEAM_CMD_REQUEST_COMM_STATUS	0xb4
#define STEAM_CMD_GET_SERIAL		0xae

/* Some useful register ids */
#define STEAM_REG_LPAD_MODE		0x07
#define STEAM_REG_RPAD_MODE		0x08
#define STEAM_REG_RPAD_MARGIN		0x18
#define STEAM_REG_LED			0x2d
#define STEAM_REG_GYRO_MODE		0x30

/* Raw event identifiers */
#define STEAM_EV_INPUT_DATA		0x01
#define STEAM_EV_CONNECT		0x03
#define STEAM_EV_BATTERY		0x04

/* Values for GYRO_MODE (bitmask) */
#define STEAM_GYRO_MODE_OFF		0x0000
#define STEAM_GYRO_MODE_STEERING	0x0001
#define STEAM_GYRO_MODE_TILT		0x0002
#define STEAM_GYRO_MODE_SEND_ORIENTATION	0x0004
#define STEAM_GYRO_MODE_SEND_RAW_ACCEL		0x0008
#define STEAM_GYRO_MODE_SEND_RAW_GYRO		0x0010

/* Other random constants */
#define STEAM_SERIAL_LEN 10

struct steam_device {
	struct list_head list;
	spinlock_t lock;
	struct hid_device *hdev, *client_hdev;
	struct mutex mutex;
	bool client_opened;
	struct input_dev __rcu *input;
	unsigned long quirks;
	struct work_struct work_connect;
	bool connected;
	char serial_no[STEAM_SERIAL_LEN + 1];
	struct power_supply_desc battery_desc;
	struct power_supply __rcu *battery;
	u8 battery_charge;
	u16 voltage;
};

static int steam_recv_report(struct steam_device *steam,
		u8 *data, int size)
{
	struct hid_report *r;
	u8 *buf;
	int ret;

	r = steam->hdev->report_enum[HID_FEATURE_REPORT].report_id_hash[0];
	if (hid_report_len(r) < 64)
		return -EINVAL;

	buf = hid_alloc_report_buf(r, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/*
	 * The report ID is always 0, so strip the first byte from the output.
	 * hid_report_len() is not counting the report ID, so +1 to the length
	 * or else we get a EOVERFLOW. We are safe from a buffer overflow
	 * because hid_alloc_report_buf() allocates +7 bytes.
	 */
	ret = hid_hw_raw_request(steam->hdev, 0x00,
			buf, hid_report_len(r) + 1,
			HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret > 0)
		memcpy(data, buf + 1, min(size, ret - 1));
	kfree(buf);
	return ret;
}

static int steam_send_report(struct steam_device *steam,
		u8 *cmd, int size)
{
	struct hid_report *r;
	u8 *buf;
	unsigned int retries = 50;
	int ret;

	r = steam->hdev->report_enum[HID_FEATURE_REPORT].report_id_hash[0];
	if (hid_report_len(r) < 64)
		return -EINVAL;

	buf = hid_alloc_report_buf(r, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* The report ID is always 0 */
	memcpy(buf + 1, cmd, size);

	/*
	 * Sometimes the wireless controller fails with EPIPE
	 * when sending a feature report.
	 * Doing a HID_REQ_GET_REPORT and waiting for a while
	 * seems to fix that.
	 */
	do {
		ret = hid_hw_raw_request(steam->hdev, 0,
				buf, size + 1,
				HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
		if (ret != -EPIPE)
			break;
		msleep(20);
	} while (--retries);

	kfree(buf);
	if (ret < 0)
		hid_err(steam->hdev, "%s: error %d (%*ph)\n", __func__,
				ret, size, cmd);
	return ret;
}

static inline int steam_send_report_byte(struct steam_device *steam, u8 cmd)
{
	return steam_send_report(steam, &cmd, 1);
}

static int steam_write_registers(struct steam_device *steam,
		/* u8 reg, u16 val */...)
{
	/* Send: 0x87 len (reg valLo valHi)* */
	u8 reg;
	u16 val;
	u8 cmd[64] = {STEAM_CMD_WRITE_REGISTER, 0x00};
	va_list args;

	va_start(args, steam);
	for (;;) {
		reg = va_arg(args, int);
		if (reg == 0)
			break;
		val = va_arg(args, int);
		cmd[cmd[1] + 2] = reg;
		cmd[cmd[1] + 3] = val & 0xff;
		cmd[cmd[1] + 4] = val >> 8;
		cmd[1] += 3;
	}
	va_end(args);

	return steam_send_report(steam, cmd, 2 + cmd[1]);
}

static int steam_get_serial(struct steam_device *steam)
{
	/*
	 * Send: 0xae 0x15 0x01
	 * Recv: 0xae 0x15 0x01 serialnumber (10 chars)
	 */
	int ret;
	u8 cmd[] = {STEAM_CMD_GET_SERIAL, 0x15, 0x01};
	u8 reply[3 + STEAM_SERIAL_LEN + 1];

	ret = steam_send_report(steam, cmd, sizeof(cmd));
	if (ret < 0)
		return ret;
	ret = steam_recv_report(steam, reply, sizeof(reply));
	if (ret < 0)
		return ret;
	if (reply[0] != 0xae || reply[1] != 0x15 || reply[2] != 0x01)
		return -EIO;
	reply[3 + STEAM_SERIAL_LEN] = 0;
	strlcpy(steam->serial_no, reply + 3, sizeof(steam->serial_no));
	return 0;
}

/*
 * This command requests the wireless adaptor to post an event
 * with the connection status. Useful if this driver is loaded when
 * the controller is already connected.
 */
static inline int steam_request_conn_status(struct steam_device *steam)
{
	return steam_send_report_byte(steam, STEAM_CMD_REQUEST_COMM_STATUS);
}

static void steam_set_lizard_mode(struct steam_device *steam, bool enable)
{
	if (enable) {
		/* enable esc, enter, cursors */
		steam_send_report_byte(steam, STEAM_CMD_DEFAULT_MAPPINGS);
		/* enable mouse */
		steam_send_report_byte(steam, STEAM_CMD_DEFAULT_MOUSE);
		steam_write_registers(steam,
			STEAM_REG_RPAD_MARGIN, 0x01, /* enable margin */
			0);
	} else {
		/* disable esc, enter, cursor */
		steam_send_report_byte(steam, STEAM_CMD_CLEAR_MAPPINGS);
		steam_write_registers(steam,
			STEAM_REG_RPAD_MODE, 0x07, /* disable mouse */
			STEAM_REG_RPAD_MARGIN, 0x00, /* disable margin */
			0);
	}
}

static int steam_input_open(struct input_dev *dev)
{
	struct steam_device *steam = input_get_drvdata(dev);

	mutex_lock(&steam->mutex);
	if (!steam->client_opened && lizard_mode)
		steam_set_lizard_mode(steam, false);
	mutex_unlock(&steam->mutex);
	return 0;
}

static void steam_input_close(struct input_dev *dev)
{
	struct steam_device *steam = input_get_drvdata(dev);

	mutex_lock(&steam->mutex);
	if (!steam->client_opened && lizard_mode)
		steam_set_lizard_mode(steam, true);
	mutex_unlock(&steam->mutex);
}

static enum power_supply_property steam_battery_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
};

static int steam_battery_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct steam_device *steam = power_supply_get_drvdata(psy);
	unsigned long flags;
	s16 volts;
	u8 batt;
	int ret = 0;

	spin_lock_irqsave(&steam->lock, flags);
	volts = steam->voltage;
	batt = steam->battery_charge;
	spin_unlock_irqrestore(&steam->lock, flags);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_DEVICE;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = volts * 1000; /* mV -> uV */
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = batt;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int steam_battery_register(struct steam_device *steam)
{
	struct power_supply *battery;
	struct power_supply_config battery_cfg = { .drv_data = steam, };
	unsigned long flags;
	int ret;

	steam->battery_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	steam->battery_desc.properties = steam_battery_props;
	steam->battery_desc.num_properties = ARRAY_SIZE(steam_battery_props);
	steam->battery_desc.get_property = steam_battery_get_property;
	steam->battery_desc.name = devm_kasprintf(&steam->hdev->dev,
			GFP_KERNEL, "steam-controller-%s-battery",
			steam->serial_no);
	if (!steam->battery_desc.name)
		return -ENOMEM;

	/* avoid the warning of 0% battery while waiting for the first info */
	spin_lock_irqsave(&steam->lock, flags);
	steam->voltage = 3000;
	steam->battery_charge = 100;
	spin_unlock_irqrestore(&steam->lock, flags);

	battery = power_supply_register(&steam->hdev->dev,
			&steam->battery_desc, &battery_cfg);
	if (IS_ERR(battery)) {
		ret = PTR_ERR(battery);
		hid_err(steam->hdev,
				"%s:power_supply_register failed with error %d\n",
				__func__, ret);
		return ret;
	}
	rcu_assign_pointer(steam->battery, battery);
	power_supply_powers(battery, &steam->hdev->dev);
	return 0;
}

static int steam_input_register(struct steam_device *steam)
{
	struct hid_device *hdev = steam->hdev;
	struct input_dev *input;
	int ret;

	rcu_read_lock();
	input = rcu_dereference(steam->input);
	rcu_read_unlock();
	if (input) {
		dbg_hid("%s: already connected\n", __func__);
		return 0;
	}

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	input_set_drvdata(input, steam);
	input->dev.parent = &hdev->dev;
	input->open = steam_input_open;
	input->close = steam_input_close;

	input->name = (steam->quirks & STEAM_QUIRK_WIRELESS) ?
		"Wireless Steam Controller" :
		"Steam Controller";
	input->phys = hdev->phys;
	input->uniq = steam->serial_no;
	input->id.bustype = hdev->bus;
	input->id.vendor = hdev->vendor;
	input->id.product = hdev->product;
	input->id.version = hdev->version;

	input_set_capability(input, EV_KEY, BTN_TR2);
	input_set_capability(input, EV_KEY, BTN_TL2);
	input_set_capability(input, EV_KEY, BTN_TR);
	input_set_capability(input, EV_KEY, BTN_TL);
	input_set_capability(input, EV_KEY, BTN_Y);
	input_set_capability(input, EV_KEY, BTN_B);
	input_set_capability(input, EV_KEY, BTN_X);
	input_set_capability(input, EV_KEY, BTN_A);
	input_set_capability(input, EV_KEY, BTN_DPAD_UP);
	input_set_capability(input, EV_KEY, BTN_DPAD_RIGHT);
	input_set_capability(input, EV_KEY, BTN_DPAD_LEFT);
	input_set_capability(input, EV_KEY, BTN_DPAD_DOWN);
	input_set_capability(input, EV_KEY, BTN_SELECT);
	input_set_capability(input, EV_KEY, BTN_MODE);
	input_set_capability(input, EV_KEY, BTN_START);
	input_set_capability(input, EV_KEY, BTN_GEAR_DOWN);
	input_set_capability(input, EV_KEY, BTN_GEAR_UP);
	input_set_capability(input, EV_KEY, BTN_THUMBR);
	input_set_capability(input, EV_KEY, BTN_THUMBL);
	input_set_capability(input, EV_KEY, BTN_THUMB);
	input_set_capability(input, EV_KEY, BTN_THUMB2);

	input_set_abs_params(input, ABS_HAT2Y, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_HAT2X, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_X, -32767, 32767, 0, 0);
	input_set_abs_params(input, ABS_Y, -32767, 32767, 0, 0);
	input_set_abs_params(input, ABS_RX, -32767, 32767,
			STEAM_PAD_FUZZ, 0);
	input_set_abs_params(input, ABS_RY, -32767, 32767,
			STEAM_PAD_FUZZ, 0);
	input_set_abs_params(input, ABS_HAT0X, -32767, 32767,
			STEAM_PAD_FUZZ, 0);
	input_set_abs_params(input, ABS_HAT0Y, -32767, 32767,
			STEAM_PAD_FUZZ, 0);
	input_abs_set_res(input, ABS_X, STEAM_JOYSTICK_RESOLUTION);
	input_abs_set_res(input, ABS_Y, STEAM_JOYSTICK_RESOLUTION);
	input_abs_set_res(input, ABS_RX, STEAM_PAD_RESOLUTION);
	input_abs_set_res(input, ABS_RY, STEAM_PAD_RESOLUTION);
	input_abs_set_res(input, ABS_HAT0X, STEAM_PAD_RESOLUTION);
	input_abs_set_res(input, ABS_HAT0Y, STEAM_PAD_RESOLUTION);
	input_abs_set_res(input, ABS_HAT2Y, STEAM_TRIGGER_RESOLUTION);
	input_abs_set_res(input, ABS_HAT2X, STEAM_TRIGGER_RESOLUTION);

	ret = input_register_device(input);
	if (ret)
		goto input_register_fail;

	rcu_assign_pointer(steam->input, input);
	return 0;

input_register_fail:
	input_free_device(input);
	return ret;
}

static void steam_input_unregister(struct steam_device *steam)
{
	struct input_dev *input;
	rcu_read_lock();
	input = rcu_dereference(steam->input);
	rcu_read_unlock();
	if (!input)
		return;
	RCU_INIT_POINTER(steam->input, NULL);
	synchronize_rcu();
	input_unregister_device(input);
}

static void steam_battery_unregister(struct steam_device *steam)
{
	struct power_supply *battery;

	rcu_read_lock();
	battery = rcu_dereference(steam->battery);
	rcu_read_unlock();

	if (!battery)
		return;
	RCU_INIT_POINTER(steam->battery, NULL);
	synchronize_rcu();
	power_supply_unregister(battery);
}

static int steam_register(struct steam_device *steam)
{
	int ret;
	bool client_opened;

	/*
	 * This function can be called several times in a row with the
	 * wireless adaptor, without steam_unregister() between them, because
	 * another client send a get_connection_status command, for example.
	 * The battery and serial number are set just once per device.
	 */
	if (!steam->serial_no[0]) {
		/*
		 * Unlikely, but getting the serial could fail, and it is not so
		 * important, so make up a serial number and go on.
		 */
		mutex_lock(&steam->mutex);
		if (steam_get_serial(steam) < 0)
			strlcpy(steam->serial_no, "XXXXXXXXXX",
					sizeof(steam->serial_no));
		mutex_unlock(&steam->mutex);

		hid_info(steam->hdev, "Steam Controller '%s' connected",
				steam->serial_no);

		/* ignore battery errors, we can live without it */
		if (steam->quirks & STEAM_QUIRK_WIRELESS)
			steam_battery_register(steam);

		mutex_lock(&steam_devices_lock);
		list_add(&steam->list, &steam_devices);
		mutex_unlock(&steam_devices_lock);
	}

	mutex_lock(&steam->mutex);
	client_opened = steam->client_opened;
	if (!client_opened)
		steam_set_lizard_mode(steam, lizard_mode);
	mutex_unlock(&steam->mutex);

	if (!client_opened)
		ret = steam_input_register(steam);
	else
		ret = 0;

	return ret;
}

static void steam_unregister(struct steam_device *steam)
{
	steam_battery_unregister(steam);
	steam_input_unregister(steam);
	if (steam->serial_no[0]) {
		hid_info(steam->hdev, "Steam Controller '%s' disconnected",
				steam->serial_no);
		mutex_lock(&steam_devices_lock);
		list_del(&steam->list);
		mutex_unlock(&steam_devices_lock);
		steam->serial_no[0] = 0;
	}
}

static void steam_work_connect_cb(struct work_struct *work)
{
	struct steam_device *steam = container_of(work, struct steam_device,
							work_connect);
	unsigned long flags;
	bool connected;
	int ret;

	spin_lock_irqsave(&steam->lock, flags);
	connected = steam->connected;
	spin_unlock_irqrestore(&steam->lock, flags);

	if (connected) {
		ret = steam_register(steam);
		if (ret) {
			hid_err(steam->hdev,
				"%s:steam_register failed with error %d\n",
				__func__, ret);
		}
	} else {
		steam_unregister(steam);
	}
}

static bool steam_is_valve_interface(struct hid_device *hdev)
{
	struct hid_report_enum *rep_enum;

	/*
	 * The wired device creates 3 interfaces:
	 *  0: emulated mouse.
	 *  1: emulated keyboard.
	 *  2: the real game pad.
	 * The wireless device creates 5 interfaces:
	 *  0: emulated keyboard.
	 *  1-4: slots where up to 4 real game pads will be connected to.
	 * We know which one is the real gamepad interface because they are the
	 * only ones with a feature report.
	 */
	rep_enum = &hdev->report_enum[HID_FEATURE_REPORT];
	return !list_empty(&rep_enum->report_list);
}

static int steam_client_ll_parse(struct hid_device *hdev)
{
	struct steam_device *steam = hdev->driver_data;

	return hid_parse_report(hdev, steam->hdev->dev_rdesc,
			steam->hdev->dev_rsize);
}

static int steam_client_ll_start(struct hid_device *hdev)
{
	return 0;
}

static void steam_client_ll_stop(struct hid_device *hdev)
{
}

static int steam_client_ll_open(struct hid_device *hdev)
{
	struct steam_device *steam = hdev->driver_data;

	mutex_lock(&steam->mutex);
	steam->client_opened = true;
	mutex_unlock(&steam->mutex);

	steam_input_unregister(steam);

	return 0;
}

static void steam_client_ll_close(struct hid_device *hdev)
{
	struct steam_device *steam = hdev->driver_data;

	unsigned long flags;
	bool connected;

	spin_lock_irqsave(&steam->lock, flags);
	connected = steam->connected;
	spin_unlock_irqrestore(&steam->lock, flags);

	mutex_lock(&steam->mutex);
	steam->client_opened = false;
	if (connected)
		steam_set_lizard_mode(steam, lizard_mode);
	mutex_unlock(&steam->mutex);

	if (connected)
		steam_input_register(steam);
}

static int steam_client_ll_raw_request(struct hid_device *hdev,
				unsigned char reportnum, u8 *buf,
				size_t count, unsigned char report_type,
				int reqtype)
{
	struct steam_device *steam = hdev->driver_data;

	return hid_hw_raw_request(steam->hdev, reportnum, buf, count,
			report_type, reqtype);
}

static struct hid_ll_driver steam_client_ll_driver = {
	.parse = steam_client_ll_parse,
	.start = steam_client_ll_start,
	.stop = steam_client_ll_stop,
	.open = steam_client_ll_open,
	.close = steam_client_ll_close,
	.raw_request = steam_client_ll_raw_request,
};

static struct hid_device *steam_create_client_hid(struct hid_device *hdev)
{
	struct hid_device *client_hdev;

	client_hdev = hid_allocate_device();
	if (IS_ERR(client_hdev))
		return client_hdev;

	client_hdev->ll_driver = &steam_client_ll_driver;
	client_hdev->dev.parent = hdev->dev.parent;
	client_hdev->bus = hdev->bus;
	client_hdev->vendor = hdev->vendor;
	client_hdev->product = hdev->product;
	client_hdev->version = hdev->version;
	client_hdev->type = hdev->type;
	client_hdev->country = hdev->country;
	strlcpy(client_hdev->name, hdev->name,
			sizeof(client_hdev->name));
	strlcpy(client_hdev->phys, hdev->phys,
			sizeof(client_hdev->phys));
	/*
	 * Since we use the same device info than the real interface to
	 * trick userspace, we will be calling steam_probe recursively.
	 * We need to recognize the client interface somehow.
	 */
	client_hdev->group = HID_GROUP_STEAM;
	return client_hdev;
}

static int steam_probe(struct hid_device *hdev,
				const struct hid_device_id *id)
{
	struct steam_device *steam;
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev,
			"%s:parse of hid interface failed\n", __func__);
		return ret;
	}

	/*
	 * The virtual client_dev is only used for hidraw.
	 * Also avoid the recursive probe.
	 */
	if (hdev->group == HID_GROUP_STEAM)
		return hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	/*
	 * The non-valve interfaces (mouse and keyboard emulation) are
	 * connected without changes.
	 */
	if (!steam_is_valve_interface(hdev))
		return hid_hw_start(hdev, HID_CONNECT_DEFAULT);

	steam = devm_kzalloc(&hdev->dev, sizeof(*steam), GFP_KERNEL);
	if (!steam) {
		ret = -ENOMEM;
		goto steam_alloc_fail;
	}
	steam->hdev = hdev;
	hid_set_drvdata(hdev, steam);
	spin_lock_init(&steam->lock);
	mutex_init(&steam->mutex);
	steam->quirks = id->driver_data;
	INIT_WORK(&steam->work_connect, steam_work_connect_cb);

	steam->client_hdev = steam_create_client_hid(hdev);
	if (IS_ERR(steam->client_hdev)) {
		ret = PTR_ERR(steam->client_hdev);
		goto client_hdev_fail;
	}
	steam->client_hdev->driver_data = steam;

	/*
	 * With the real steam controller interface, do not connect hidraw.
	 * Instead, create the client_hid and connect that.
	 */
	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_HIDRAW);
	if (ret)
		goto hid_hw_start_fail;

	ret = hid_add_device(steam->client_hdev);
	if (ret)
		goto client_hdev_add_fail;

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev,
			"%s:hid_hw_open\n",
			__func__);
		goto hid_hw_open_fail;
	}

	if (steam->quirks & STEAM_QUIRK_WIRELESS) {
		hid_info(hdev, "Steam wireless receiver connected");
		/* If using a wireless adaptor ask for connection status */
		steam->connected = false;
		steam_request_conn_status(steam);
	} else {
		/* A wired connection is always present */
		steam->connected = true;
		ret = steam_register(steam);
		if (ret) {
			hid_err(hdev,
				"%s:steam_register failed with error %d\n",
				__func__, ret);
			goto input_register_fail;
		}
	}

	return 0;

input_register_fail:
hid_hw_open_fail:
client_hdev_add_fail:
	hid_hw_stop(hdev);
hid_hw_start_fail:
	hid_destroy_device(steam->client_hdev);
client_hdev_fail:
	cancel_work_sync(&steam->work_connect);
steam_alloc_fail:
	hid_err(hdev, "%s: failed with error %d\n",
			__func__, ret);
	return ret;
}

static void steam_remove(struct hid_device *hdev)
{
	struct steam_device *steam = hid_get_drvdata(hdev);

	if (!steam || hdev->group == HID_GROUP_STEAM) {
		hid_hw_stop(hdev);
		return;
	}

	hid_destroy_device(steam->client_hdev);
	steam->client_opened = false;
	cancel_work_sync(&steam->work_connect);
	if (steam->quirks & STEAM_QUIRK_WIRELESS) {
		hid_info(hdev, "Steam wireless receiver disconnected");
	}
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
	steam_unregister(steam);
}

static void steam_do_connect_event(struct steam_device *steam, bool connected)
{
	unsigned long flags;
	bool changed;

	spin_lock_irqsave(&steam->lock, flags);
	changed = steam->connected != connected;
	steam->connected = connected;
	spin_unlock_irqrestore(&steam->lock, flags);

	if (changed && schedule_work(&steam->work_connect) == 0)
		dbg_hid("%s: connected=%d event already queued\n",
				__func__, connected);
}

/*
 * Some input data in the protocol has the opposite sign.
 * Clamp the values to 32767..-32767 so that the range is
 * symmetrical and can be negated safely.
 */
static inline s16 steam_le16(u8 *data)
{
	s16 x = (s16) le16_to_cpup((__le16 *)data);

	return x == -32768 ? -32767 : x;
}

/*
 * The size for this message payload is 60.
 * The known values are:
 *  (* values are not sent through wireless)
 *  (* accelerator/gyro is disabled by default)
 *  Offset| Type  | Mapped to |Meaning
 * -------+-------+-----------+--------------------------
 *  4-7   | u32   | --        | sequence number
 *  8-10  | 24bit | see below | buttons
 *  11    | u8    | ABS_HAT2Y | left trigger
 *  12    | u8    | ABS_HAT2X | right trigger
 *  13-15 | --    | --        | always 0
 *  16-17 | s16   | ABS_X/ABS_HAT0X     | X value
 *  18-19 | s16   | ABS_Y/ABS_HAT0Y     | Y value
 *  20-21 | s16   | ABS_RX    | right-pad X value
 *  22-23 | s16   | ABS_RY    | right-pad Y value
 *  24-25 | s16   | --        | * left trigger
 *  26-27 | s16   | --        | * right trigger
 *  28-29 | s16   | --        | * accelerometer X value
 *  30-31 | s16   | --        | * accelerometer Y value
 *  32-33 | s16   | --        | * accelerometer Z value
 *  34-35 | s16   | --        | gyro X value
 *  36-36 | s16   | --        | gyro Y value
 *  38-39 | s16   | --        | gyro Z value
 *  40-41 | s16   | --        | quaternion W value
 *  42-43 | s16   | --        | quaternion X value
 *  44-45 | s16   | --        | quaternion Y value
 *  46-47 | s16   | --        | quaternion Z value
 *  48-49 | --    | --        | always 0
 *  50-51 | s16   | --        | * left trigger (uncalibrated)
 *  52-53 | s16   | --        | * right trigger (uncalibrated)
 *  54-55 | s16   | --        | * joystick X value (uncalibrated)
 *  56-57 | s16   | --        | * joystick Y value (uncalibrated)
 *  58-59 | s16   | --        | * left-pad X value
 *  60-61 | s16   | --        | * left-pad Y value
 *  62-63 | u16   | --        | * battery voltage
 *
 * The buttons are:
 *  Bit  | Mapped to  | Description
 * ------+------------+--------------------------------
 *  8.0  | BTN_TR2    | right trigger fully pressed
 *  8.1  | BTN_TL2    | left trigger fully pressed
 *  8.2  | BTN_TR     | right shoulder
 *  8.3  | BTN_TL     | left shoulder
 *  8.4  | BTN_Y      | button Y
 *  8.5  | BTN_B      | button B
 *  8.6  | BTN_X      | button X
 *  8.7  | BTN_A      | button A
 *  9.0  | BTN_DPAD_UP    | lef-pad up
 *  9.1  | BTN_DPAD_RIGHT | lef-pad right
 *  9.2  | BTN_DPAD_LEFT  | lef-pad left
 *  9.3  | BTN_DPAD_DOWN  | lef-pad down
 *  9.4  | BTN_SELECT | menu left
 *  9.5  | BTN_MODE   | steam logo
 *  9.6  | BTN_START  | menu right
 *  9.7  | BTN_GEAR_DOWN | left back lever
 * 10.0  | BTN_GEAR_UP   | right back lever
 * 10.1  | --         | left-pad clicked
 * 10.2  | BTN_THUMBR | right-pad clicked
 * 10.3  | BTN_THUMB  | left-pad touched (but see explanation below)
 * 10.4  | BTN_THUMB2 | right-pad touched
 * 10.5  | --         | unknown
 * 10.6  | BTN_THUMBL | joystick clicked
 * 10.7  | --         | lpad_and_joy
 */

static void steam_do_input_event(struct steam_device *steam,
		struct input_dev *input, u8 *data)
{
	/* 24 bits of buttons */
	u8 b8, b9, b10;
	s16 x, y;
	bool lpad_touched, lpad_and_joy;

	b8 = data[8];
	b9 = data[9];
	b10 = data[10];

	input_report_abs(input, ABS_HAT2Y, data[11]);
	input_report_abs(input, ABS_HAT2X, data[12]);

	/*
	 * These two bits tells how to interpret the values X and Y.
	 * lpad_and_joy tells that the joystick and the lpad are used at the
	 * same time.
	 * lpad_touched tells whether X/Y are to be read as lpad coord or
	 * joystick values.
	 * (lpad_touched || lpad_and_joy) tells if the lpad is really touched.
	 */
	lpad_touched = b10 & BIT(3);
	lpad_and_joy = b10 & BIT(7);
	x = steam_le16(data + 16);
	y = -steam_le16(data + 18);

	input_report_abs(input, lpad_touched ? ABS_HAT0X : ABS_X, x);
	input_report_abs(input, lpad_touched ? ABS_HAT0Y : ABS_Y, y);
	/* Check if joystick is centered */
	if (lpad_touched && !lpad_and_joy) {
		input_report_abs(input, ABS_X, 0);
		input_report_abs(input, ABS_Y, 0);
	}
	/* Check if lpad is untouched */
	if (!(lpad_touched || lpad_and_joy)) {
		input_report_abs(input, ABS_HAT0X, 0);
		input_report_abs(input, ABS_HAT0Y, 0);
	}

	input_report_abs(input, ABS_RX, steam_le16(data + 20));
	input_report_abs(input, ABS_RY, -steam_le16(data + 22));

	input_event(input, EV_KEY, BTN_TR2, !!(b8 & BIT(0)));
	input_event(input, EV_KEY, BTN_TL2, !!(b8 & BIT(1)));
	input_event(input, EV_KEY, BTN_TR, !!(b8 & BIT(2)));
	input_event(input, EV_KEY, BTN_TL, !!(b8 & BIT(3)));
	input_event(input, EV_KEY, BTN_Y, !!(b8 & BIT(4)));
	input_event(input, EV_KEY, BTN_B, !!(b8 & BIT(5)));
	input_event(input, EV_KEY, BTN_X, !!(b8 & BIT(6)));
	input_event(input, EV_KEY, BTN_A, !!(b8 & BIT(7)));
	input_event(input, EV_KEY, BTN_SELECT, !!(b9 & BIT(4)));
	input_event(input, EV_KEY, BTN_MODE, !!(b9 & BIT(5)));
	input_event(input, EV_KEY, BTN_START, !!(b9 & BIT(6)));
	input_event(input, EV_KEY, BTN_GEAR_DOWN, !!(b9 & BIT(7)));
	input_event(input, EV_KEY, BTN_GEAR_UP, !!(b10 & BIT(0)));
	input_event(input, EV_KEY, BTN_THUMBR, !!(b10 & BIT(2)));
	input_event(input, EV_KEY, BTN_THUMBL, !!(b10 & BIT(6)));
	input_event(input, EV_KEY, BTN_THUMB, lpad_touched || lpad_and_joy);
	input_event(input, EV_KEY, BTN_THUMB2, !!(b10 & BIT(4)));
	input_event(input, EV_KEY, BTN_DPAD_UP, !!(b9 & BIT(0)));
	input_event(input, EV_KEY, BTN_DPAD_RIGHT, !!(b9 & BIT(1)));
	input_event(input, EV_KEY, BTN_DPAD_LEFT, !!(b9 & BIT(2)));
	input_event(input, EV_KEY, BTN_DPAD_DOWN, !!(b9 & BIT(3)));

	input_sync(input);
}

/*
 * The size for this message payload is 11.
 * The known values are:
 *  Offset| Type  | Meaning
 * -------+-------+---------------------------
 *  4-7   | u32   | sequence number
 *  8-11  | --    | always 0
 *  12-13 | u16   | voltage (mV)
 *  14    | u8    | battery percent
 */
static void steam_do_battery_event(struct steam_device *steam,
		struct power_supply *battery, u8 *data)
{
	unsigned long flags;

	s16 volts = steam_le16(data + 12);
	u8 batt = data[14];

	/* Creating the battery may have failed */
	rcu_read_lock();
	battery = rcu_dereference(steam->battery);
	if (likely(battery)) {
		spin_lock_irqsave(&steam->lock, flags);
		steam->voltage = volts;
		steam->battery_charge = batt;
		spin_unlock_irqrestore(&steam->lock, flags);
		power_supply_changed(battery);
	}
	rcu_read_unlock();
}

static int steam_raw_event(struct hid_device *hdev,
			struct hid_report *report, u8 *data,
			int size)
{
	struct steam_device *steam = hid_get_drvdata(hdev);
	struct input_dev *input;
	struct power_supply *battery;

	if (!steam)
		return 0;

	if (steam->client_opened)
		hid_input_report(steam->client_hdev, HID_FEATURE_REPORT,
				data, size, 0);
	/*
	 * All messages are size=64, all values little-endian.
	 * The format is:
	 *  Offset| Meaning
	 * -------+--------------------------------------------
	 *  0-1   | always 0x01, 0x00, maybe protocol version?
	 *  2     | type of message
	 *  3     | length of the real payload (not checked)
	 *  4-n   | payload data, depends on the type
	 *
	 * There are these known types of message:
	 *  0x01: input data (60 bytes)
	 *  0x03: wireless connect/disconnect (1 byte)
	 *  0x04: battery status (11 bytes)
	 */

	if (size != 64 || data[0] != 1 || data[1] != 0)
		return 0;

	switch (data[2]) {
	case STEAM_EV_INPUT_DATA:
		if (steam->client_opened)
			return 0;
		rcu_read_lock();
		input = rcu_dereference(steam->input);
		if (likely(input))
			steam_do_input_event(steam, input, data);
		rcu_read_unlock();
		break;
	case STEAM_EV_CONNECT:
		/*
		 * The payload of this event is a single byte:
		 *  0x01: disconnected.
		 *  0x02: connected.
		 */
		switch (data[4]) {
		case 0x01:
			steam_do_connect_event(steam, false);
			break;
		case 0x02:
			steam_do_connect_event(steam, true);
			break;
		}
		break;
	case STEAM_EV_BATTERY:
		if (steam->quirks & STEAM_QUIRK_WIRELESS) {
			rcu_read_lock();
			battery = rcu_dereference(steam->battery);
			if (likely(battery)) {
				steam_do_battery_event(steam, battery, data);
			} else {
				dbg_hid(
					"%s: battery data without connect event\n",
					__func__);
				steam_do_connect_event(steam, true);
			}
			rcu_read_unlock();
		}
		break;
	}
	return 0;
}

static int steam_param_set_lizard_mode(const char *val,
					const struct kernel_param *kp)
{
	struct steam_device *steam;
	int ret;

	ret = param_set_bool(val, kp);
	if (ret)
		return ret;

	mutex_lock(&steam_devices_lock);
	list_for_each_entry(steam, &steam_devices, list) {
		mutex_lock(&steam->mutex);
		if (!steam->client_opened)
			steam_set_lizard_mode(steam, lizard_mode);
		mutex_unlock(&steam->mutex);
	}
	mutex_unlock(&steam_devices_lock);
	return 0;
}

static const struct kernel_param_ops steam_lizard_mode_ops = {
	.set	= steam_param_set_lizard_mode,
	.get	= param_get_bool,
};

module_param_cb(lizard_mode, &steam_lizard_mode_ops, &lizard_mode, 0644);
MODULE_PARM_DESC(lizard_mode,
	"Enable mouse and keyboard emulation (lizard mode) when the gamepad is not in use");

static const struct hid_device_id steam_controllers[] = {
	{ /* Wired Steam Controller */
	  HID_USB_DEVICE(USB_VENDOR_ID_VALVE,
		USB_DEVICE_ID_STEAM_CONTROLLER)
	},
	{ /* Wireless Steam Controller */
	  HID_USB_DEVICE(USB_VENDOR_ID_VALVE,
		USB_DEVICE_ID_STEAM_CONTROLLER_WIRELESS),
	  .driver_data = STEAM_QUIRK_WIRELESS
	},
	{}
};

MODULE_DEVICE_TABLE(hid, steam_controllers);

static struct hid_driver steam_controller_driver = {
	.name = "hid-steam",
	.id_table = steam_controllers,
	.probe = steam_probe,
	.remove = steam_remove,
	.raw_event = steam_raw_event,
};

module_hid_driver(steam_controller_driver);
