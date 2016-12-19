/*
 * HP WMI hotkeys
 *
 * Copyright (C) 2008 Red Hat <mjg@redhat.com>
 * Copyright (C) 2010, 2011 Anssi Hannula <anssi.hannula@iki.fi>
 *
 * Portions based on wistron_btns.c:
 * Copyright (C) 2005 Miloslav Trmac <mitr@volny.cz>
 * Copyright (C) 2005 Bernhard Rosenkraenzer <bero@arklinux.org>
 * Copyright (C) 2005 Dmitry Torokhov <dtor@mail.ru>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/rfkill.h>
#include <linux/string.h>

MODULE_AUTHOR("Matthew Garrett <mjg59@srcf.ucam.org>");
MODULE_DESCRIPTION("HP laptop WMI hotkeys driver");
MODULE_LICENSE("GPL");

MODULE_ALIAS("wmi:95F24279-4D7B-4334-9387-ACCDC67EF61C");
MODULE_ALIAS("wmi:5FB7F034-2C63-45e9-BE91-3D44E2C707E4");

#define HPWMI_EVENT_GUID "95F24279-4D7B-4334-9387-ACCDC67EF61C"
#define HPWMI_BIOS_GUID "5FB7F034-2C63-45e9-BE91-3D44E2C707E4"

#define HPWMI_DISPLAY_QUERY 0x1
#define HPWMI_HDDTEMP_QUERY 0x2
#define HPWMI_ALS_QUERY 0x3
#define HPWMI_HARDWARE_QUERY 0x4
#define HPWMI_WIRELESS_QUERY 0x5
#define HPWMI_BIOS_QUERY 0x9
#define HPWMI_FEATURE_QUERY 0xb
#define HPWMI_HOTKEY_QUERY 0xc
#define HPWMI_FEATURE2_QUERY 0xd
#define HPWMI_WIRELESS2_QUERY 0x1b
#define HPWMI_POSTCODEERROR_QUERY 0x2a

enum hp_wmi_radio {
	HPWMI_WIFI = 0,
	HPWMI_BLUETOOTH = 1,
	HPWMI_WWAN = 2,
	HPWMI_GPS = 3,
};

enum hp_wmi_event_ids {
	HPWMI_DOCK_EVENT = 1,
	HPWMI_PARK_HDD = 2,
	HPWMI_SMART_ADAPTER = 3,
	HPWMI_BEZEL_BUTTON = 4,
	HPWMI_WIRELESS = 5,
	HPWMI_CPU_BATTERY_THROTTLE = 6,
	HPWMI_LOCK_SWITCH = 7,
	HPWMI_LID_SWITCH = 8,
	HPWMI_SCREEN_ROTATION = 9,
	HPWMI_COOLSENSE_SYSTEM_MOBILE = 0x0A,
	HPWMI_COOLSENSE_SYSTEM_HOT = 0x0B,
	HPWMI_PROXIMITY_SENSOR = 0x0C,
	HPWMI_BACKLIT_KB_BRIGHTNESS = 0x0D,
	HPWMI_PEAKSHIFT_PERIOD = 0x0F,
	HPWMI_BATTERY_CHARGE_PERIOD = 0x10,
};

struct bios_args {
	u32 signature;
	u32 command;
	u32 commandtype;
	u32 datasize;
	u32 data;
};

struct bios_return {
	u32 sigpass;
	u32 return_code;
};

enum hp_return_value {
	HPWMI_RET_WRONG_SIGNATURE	= 0x02,
	HPWMI_RET_UNKNOWN_COMMAND	= 0x03,
	HPWMI_RET_UNKNOWN_CMDTYPE	= 0x04,
	HPWMI_RET_INVALID_PARAMETERS	= 0x05,
};

enum hp_wireless2_bits {
	HPWMI_POWER_STATE	= 0x01,
	HPWMI_POWER_SOFT	= 0x02,
	HPWMI_POWER_BIOS	= 0x04,
	HPWMI_POWER_HARD	= 0x08,
};

#define IS_HWBLOCKED(x) ((x & (HPWMI_POWER_BIOS | HPWMI_POWER_HARD)) \
			 != (HPWMI_POWER_BIOS | HPWMI_POWER_HARD))
#define IS_SWBLOCKED(x) !(x & HPWMI_POWER_SOFT)

struct bios_rfkill2_device_state {
	u8 radio_type;
	u8 bus_type;
	u16 vendor_id;
	u16 product_id;
	u16 subsys_vendor_id;
	u16 subsys_product_id;
	u8 rfkill_id;
	u8 power;
	u8 unknown[4];
};

/* 7 devices fit into the 128 byte buffer */
#define HPWMI_MAX_RFKILL2_DEVICES	7

struct bios_rfkill2_state {
	u8 unknown[7];
	u8 count;
	u8 pad[8];
	struct bios_rfkill2_device_state device[HPWMI_MAX_RFKILL2_DEVICES];
};

static const struct key_entry hp_wmi_keymap[] = {
	{ KE_KEY, 0x02,   { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 0x03,   { KEY_BRIGHTNESSDOWN } },
	{ KE_KEY, 0x20e6, { KEY_PROG1 } },
	{ KE_KEY, 0x20e8, { KEY_MEDIA } },
	{ KE_KEY, 0x2142, { KEY_MEDIA } },
	{ KE_KEY, 0x213b, { KEY_INFO } },
	{ KE_KEY, 0x2169, { KEY_ROTATE_DISPLAY } },
	{ KE_KEY, 0x216a, { KEY_SETUP } },
	{ KE_KEY, 0x231b, { KEY_HELP } },
	{ KE_END, 0 }
};

static struct input_dev *hp_wmi_input_dev;
static struct platform_device *hp_wmi_platform_dev;

static struct rfkill *wifi_rfkill;
static struct rfkill *bluetooth_rfkill;
static struct rfkill *wwan_rfkill;
static struct rfkill *gps_rfkill;

struct rfkill2_device {
	u8 id;
	int num;
	struct rfkill *rfkill;
};

static int rfkill2_count;
static struct rfkill2_device rfkill2[HPWMI_MAX_RFKILL2_DEVICES];

/*
 * hp_wmi_perform_query
 *
 * query:	The commandtype -> What should be queried
 * write:	The command -> 0 read, 1 write, 3 ODM specific
 * buffer:	Buffer used as input and/or output
 * insize:	Size of input buffer
 * outsize:	Size of output buffer
 *
 * returns zero on success
 *         an HP WMI query specific error code (which is positive)
 *         -EINVAL if the query was not successful at all
 *         -EINVAL if the output buffer size exceeds buffersize
 *
 * Note: The buffersize must at least be the maximum of the input and output
 *       size. E.g. Battery info query (0x7) is defined to have 1 byte input
 *       and 128 byte output. The caller would do:
 *       buffer = kzalloc(128, GFP_KERNEL);
 *       ret = hp_wmi_perform_query(0x7, 0, buffer, 1, 128)
 */
static int hp_wmi_perform_query(int query, int write, void *buffer,
				int insize, int outsize)
{
	struct bios_return *bios_return;
	int actual_outsize;
	union acpi_object *obj;
	struct bios_args args = {
		.signature = 0x55434553,
		.command = write ? 0x2 : 0x1,
		.commandtype = query,
		.datasize = insize,
		.data = 0,
	};
	struct acpi_buffer input = { sizeof(struct bios_args), &args };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	u32 rc;

	if (WARN_ON(insize > sizeof(args.data)))
		return -EINVAL;
	memcpy(&args.data, buffer, insize);

	wmi_evaluate_method(HPWMI_BIOS_GUID, 0, 0x3, &input, &output);

	obj = output.pointer;

	if (!obj)
		return -EINVAL;
	else if (obj->type != ACPI_TYPE_BUFFER) {
		kfree(obj);
		return -EINVAL;
	}

	bios_return = (struct bios_return *)obj->buffer.pointer;
	rc = bios_return->return_code;

	if (rc) {
		if (rc != HPWMI_RET_UNKNOWN_CMDTYPE)
			pr_warn("query 0x%x returned error 0x%x\n", query, rc);
		kfree(obj);
		return rc;
	}

	if (!outsize) {
		/* ignore output data */
		kfree(obj);
		return 0;
	}

	actual_outsize = min(outsize, (int)(obj->buffer.length - sizeof(*bios_return)));
	memcpy(buffer, obj->buffer.pointer + sizeof(*bios_return), actual_outsize);
	memset(buffer + actual_outsize, 0, outsize - actual_outsize);
	kfree(obj);
	return 0;
}

static int hp_wmi_display_state(void)
{
	int state = 0;
	int ret = hp_wmi_perform_query(HPWMI_DISPLAY_QUERY, 0, &state,
				       sizeof(state), sizeof(state));
	if (ret)
		return -EINVAL;
	return state;
}

static int hp_wmi_hddtemp_state(void)
{
	int state = 0;
	int ret = hp_wmi_perform_query(HPWMI_HDDTEMP_QUERY, 0, &state,
				       sizeof(state), sizeof(state));
	if (ret)
		return -EINVAL;
	return state;
}

static int hp_wmi_als_state(void)
{
	int state = 0;
	int ret = hp_wmi_perform_query(HPWMI_ALS_QUERY, 0, &state,
				       sizeof(state), sizeof(state));
	if (ret)
		return -EINVAL;
	return state;
}

static int hp_wmi_dock_state(void)
{
	int state = 0;
	int ret = hp_wmi_perform_query(HPWMI_HARDWARE_QUERY, 0, &state,
				       sizeof(state), sizeof(state));

	if (ret)
		return -EINVAL;

	return state & 0x1;
}

static int hp_wmi_tablet_state(void)
{
	int state = 0;
	int ret = hp_wmi_perform_query(HPWMI_HARDWARE_QUERY, 0, &state,
				       sizeof(state), sizeof(state));
	if (ret)
		return ret;

	return (state & 0x4) ? 1 : 0;
}

static int __init hp_wmi_bios_2008_later(void)
{
	int state = 0;
	int ret = hp_wmi_perform_query(HPWMI_FEATURE_QUERY, 0, &state,
				       sizeof(state), sizeof(state));
	if (!ret)
		return 1;

	return (ret == HPWMI_RET_UNKNOWN_CMDTYPE) ? 0 : -ENXIO;
}

static int __init hp_wmi_bios_2009_later(void)
{
	int state = 0;
	int ret = hp_wmi_perform_query(HPWMI_FEATURE2_QUERY, 0, &state,
				       sizeof(state), sizeof(state));
	if (!ret)
		return 1;

	return (ret == HPWMI_RET_UNKNOWN_CMDTYPE) ? 0 : -ENXIO;
}

static int __init hp_wmi_enable_hotkeys(void)
{
	int value = 0x6e;
	int ret = hp_wmi_perform_query(HPWMI_BIOS_QUERY, 1, &value,
				       sizeof(value), 0);
	if (ret)
		return -EINVAL;
	return 0;
}

static int hp_wmi_set_block(void *data, bool blocked)
{
	enum hp_wmi_radio r = (enum hp_wmi_radio) data;
	int query = BIT(r + 8) | ((!blocked) << r);
	int ret;

	ret = hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, 1,
				   &query, sizeof(query), 0);
	if (ret)
		return -EINVAL;
	return 0;
}

static const struct rfkill_ops hp_wmi_rfkill_ops = {
	.set_block = hp_wmi_set_block,
};

static bool hp_wmi_get_sw_state(enum hp_wmi_radio r)
{
	int wireless = 0;
	int mask;
	hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, 0,
			     &wireless, sizeof(wireless),
			     sizeof(wireless));
	/* TBD: Pass error */

	mask = 0x200 << (r * 8);

	if (wireless & mask)
		return false;
	else
		return true;
}

static bool hp_wmi_get_hw_state(enum hp_wmi_radio r)
{
	int wireless = 0;
	int mask;
	hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, 0,
			     &wireless, sizeof(wireless),
			     sizeof(wireless));
	/* TBD: Pass error */

	mask = 0x800 << (r * 8);

	if (wireless & mask)
		return false;
	else
		return true;
}

static int hp_wmi_rfkill2_set_block(void *data, bool blocked)
{
	int rfkill_id = (int)(long)data;
	char buffer[4] = { 0x01, 0x00, rfkill_id, !blocked };

	if (hp_wmi_perform_query(HPWMI_WIRELESS2_QUERY, 1,
				   buffer, sizeof(buffer), 0))
		return -EINVAL;
	return 0;
}

static const struct rfkill_ops hp_wmi_rfkill2_ops = {
	.set_block = hp_wmi_rfkill2_set_block,
};

static int hp_wmi_rfkill2_refresh(void)
{
	int err, i;
	struct bios_rfkill2_state state;

	err = hp_wmi_perform_query(HPWMI_WIRELESS2_QUERY, 0, &state,
				   0, sizeof(state));
	if (err)
		return err;

	for (i = 0; i < rfkill2_count; i++) {
		int num = rfkill2[i].num;
		struct bios_rfkill2_device_state *devstate;
		devstate = &state.device[num];

		if (num >= state.count ||
		    devstate->rfkill_id != rfkill2[i].id) {
			pr_warn("power configuration of the wireless devices unexpectedly changed\n");
			continue;
		}

		rfkill_set_states(rfkill2[i].rfkill,
				  IS_SWBLOCKED(devstate->power),
				  IS_HWBLOCKED(devstate->power));
	}

	return 0;
}

static int hp_wmi_post_code_state(void)
{
	int state = 0;
	int ret = hp_wmi_perform_query(HPWMI_POSTCODEERROR_QUERY, 0, &state,
				       sizeof(state), sizeof(state));
	if (ret)
		return -EINVAL;
	return state;
}

static ssize_t show_display(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int value = hp_wmi_display_state();
	if (value < 0)
		return -EINVAL;
	return sprintf(buf, "%d\n", value);
}

static ssize_t show_hddtemp(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int value = hp_wmi_hddtemp_state();
	if (value < 0)
		return -EINVAL;
	return sprintf(buf, "%d\n", value);
}

static ssize_t show_als(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int value = hp_wmi_als_state();
	if (value < 0)
		return -EINVAL;
	return sprintf(buf, "%d\n", value);
}

static ssize_t show_dock(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	int value = hp_wmi_dock_state();
	if (value < 0)
		return -EINVAL;
	return sprintf(buf, "%d\n", value);
}

static ssize_t show_tablet(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	int value = hp_wmi_tablet_state();
	if (value < 0)
		return -EINVAL;
	return sprintf(buf, "%d\n", value);
}

static ssize_t show_postcode(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	/* Get the POST error code of previous boot failure. */
	int value = hp_wmi_post_code_state();
	if (value < 0)
		return -EINVAL;
	return sprintf(buf, "0x%x\n", value);
}

static ssize_t set_als(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	u32 tmp = simple_strtoul(buf, NULL, 10);
	int ret = hp_wmi_perform_query(HPWMI_ALS_QUERY, 1, &tmp,
				       sizeof(tmp), sizeof(tmp));
	if (ret)
		return -EINVAL;

	return count;
}

static ssize_t set_postcode(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	int ret;
	u32 tmp;
	long unsigned int tmp2;

	ret = kstrtoul(buf, 10, &tmp2);
	if (ret || tmp2 != 1)
		return -EINVAL;

	/* Clear the POST error code. It is kept until until cleared. */
	tmp = (u32) tmp2;
	ret = hp_wmi_perform_query(HPWMI_POSTCODEERROR_QUERY, 1, &tmp,
				       sizeof(tmp), sizeof(tmp));
	if (ret)
		return -EINVAL;

	return count;
}

static DEVICE_ATTR(display, S_IRUGO, show_display, NULL);
static DEVICE_ATTR(hddtemp, S_IRUGO, show_hddtemp, NULL);
static DEVICE_ATTR(als, S_IRUGO | S_IWUSR, show_als, set_als);
static DEVICE_ATTR(dock, S_IRUGO, show_dock, NULL);
static DEVICE_ATTR(tablet, S_IRUGO, show_tablet, NULL);
static DEVICE_ATTR(postcode, S_IRUGO | S_IWUSR, show_postcode, set_postcode);

static void hp_wmi_notify(u32 value, void *context)
{
	struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	u32 event_id, event_data;
	int key_code = 0, ret;
	u32 *location;
	acpi_status status;

	status = wmi_get_event_data(value, &response);
	if (status != AE_OK) {
		pr_info("bad event status 0x%x\n", status);
		return;
	}

	obj = (union acpi_object *)response.pointer;

	if (!obj)
		return;
	if (obj->type != ACPI_TYPE_BUFFER) {
		pr_info("Unknown response received %d\n", obj->type);
		kfree(obj);
		return;
	}

	/*
	 * Depending on ACPI version the concatenation of id and event data
	 * inside _WED function will result in a 8 or 16 byte buffer.
	 */
	location = (u32 *)obj->buffer.pointer;
	if (obj->buffer.length == 8) {
		event_id = *location;
		event_data = *(location + 1);
	} else if (obj->buffer.length == 16) {
		event_id = *location;
		event_data = *(location + 2);
	} else {
		pr_info("Unknown buffer length %d\n", obj->buffer.length);
		kfree(obj);
		return;
	}
	kfree(obj);

	switch (event_id) {
	case HPWMI_DOCK_EVENT:
		input_report_switch(hp_wmi_input_dev, SW_DOCK,
				    hp_wmi_dock_state());
		input_report_switch(hp_wmi_input_dev, SW_TABLET_MODE,
				    hp_wmi_tablet_state());
		input_sync(hp_wmi_input_dev);
		break;
	case HPWMI_PARK_HDD:
		break;
	case HPWMI_SMART_ADAPTER:
		break;
	case HPWMI_BEZEL_BUTTON:
		ret = hp_wmi_perform_query(HPWMI_HOTKEY_QUERY, 0,
					   &key_code,
					   sizeof(key_code),
					   sizeof(key_code));
		if (ret)
			break;

		if (!sparse_keymap_report_event(hp_wmi_input_dev,
						key_code, 1, true))
			pr_info("Unknown key code - 0x%x\n", key_code);
		break;
	case HPWMI_WIRELESS:
		if (rfkill2_count) {
			hp_wmi_rfkill2_refresh();
			break;
		}

		if (wifi_rfkill)
			rfkill_set_states(wifi_rfkill,
					  hp_wmi_get_sw_state(HPWMI_WIFI),
					  hp_wmi_get_hw_state(HPWMI_WIFI));
		if (bluetooth_rfkill)
			rfkill_set_states(bluetooth_rfkill,
					  hp_wmi_get_sw_state(HPWMI_BLUETOOTH),
					  hp_wmi_get_hw_state(HPWMI_BLUETOOTH));
		if (wwan_rfkill)
			rfkill_set_states(wwan_rfkill,
					  hp_wmi_get_sw_state(HPWMI_WWAN),
					  hp_wmi_get_hw_state(HPWMI_WWAN));
		if (gps_rfkill)
			rfkill_set_states(gps_rfkill,
					  hp_wmi_get_sw_state(HPWMI_GPS),
					  hp_wmi_get_hw_state(HPWMI_GPS));
		break;
	case HPWMI_CPU_BATTERY_THROTTLE:
		pr_info("Unimplemented CPU throttle because of 3 Cell battery event detected\n");
		break;
	case HPWMI_LOCK_SWITCH:
		break;
	case HPWMI_LID_SWITCH:
		break;
	case HPWMI_SCREEN_ROTATION:
		break;
	case HPWMI_COOLSENSE_SYSTEM_MOBILE:
		break;
	case HPWMI_COOLSENSE_SYSTEM_HOT:
		break;
	case HPWMI_PROXIMITY_SENSOR:
		break;
	case HPWMI_BACKLIT_KB_BRIGHTNESS:
		break;
	case HPWMI_PEAKSHIFT_PERIOD:
		break;
	case HPWMI_BATTERY_CHARGE_PERIOD:
		break;
	default:
		pr_info("Unknown event_id - %d - 0x%x\n", event_id, event_data);
		break;
	}
}

static int __init hp_wmi_input_setup(void)
{
	acpi_status status;
	int err;

	hp_wmi_input_dev = input_allocate_device();
	if (!hp_wmi_input_dev)
		return -ENOMEM;

	hp_wmi_input_dev->name = "HP WMI hotkeys";
	hp_wmi_input_dev->phys = "wmi/input0";
	hp_wmi_input_dev->id.bustype = BUS_HOST;

	__set_bit(EV_SW, hp_wmi_input_dev->evbit);
	__set_bit(SW_DOCK, hp_wmi_input_dev->swbit);
	__set_bit(SW_TABLET_MODE, hp_wmi_input_dev->swbit);

	err = sparse_keymap_setup(hp_wmi_input_dev, hp_wmi_keymap, NULL);
	if (err)
		goto err_free_dev;

	/* Set initial hardware state */
	input_report_switch(hp_wmi_input_dev, SW_DOCK, hp_wmi_dock_state());
	input_report_switch(hp_wmi_input_dev, SW_TABLET_MODE,
			    hp_wmi_tablet_state());
	input_sync(hp_wmi_input_dev);

	if (!hp_wmi_bios_2009_later() && hp_wmi_bios_2008_later())
		hp_wmi_enable_hotkeys();

	status = wmi_install_notify_handler(HPWMI_EVENT_GUID, hp_wmi_notify, NULL);
	if (ACPI_FAILURE(status)) {
		err = -EIO;
		goto err_free_keymap;
	}

	err = input_register_device(hp_wmi_input_dev);
	if (err)
		goto err_uninstall_notifier;

	return 0;

 err_uninstall_notifier:
	wmi_remove_notify_handler(HPWMI_EVENT_GUID);
 err_free_keymap:
	sparse_keymap_free(hp_wmi_input_dev);
 err_free_dev:
	input_free_device(hp_wmi_input_dev);
	return err;
}

static void hp_wmi_input_destroy(void)
{
	wmi_remove_notify_handler(HPWMI_EVENT_GUID);
	sparse_keymap_free(hp_wmi_input_dev);
	input_unregister_device(hp_wmi_input_dev);
}

static void cleanup_sysfs(struct platform_device *device)
{
	device_remove_file(&device->dev, &dev_attr_display);
	device_remove_file(&device->dev, &dev_attr_hddtemp);
	device_remove_file(&device->dev, &dev_attr_als);
	device_remove_file(&device->dev, &dev_attr_dock);
	device_remove_file(&device->dev, &dev_attr_tablet);
	device_remove_file(&device->dev, &dev_attr_postcode);
}

static int __init hp_wmi_rfkill_setup(struct platform_device *device)
{
	int err;
	int wireless = 0;

	err = hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, 0, &wireless,
				   sizeof(wireless), sizeof(wireless));
	if (err)
		return err;

	err = hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, 1, &wireless,
				   sizeof(wireless), 0);
	if (err)
		return err;

	if (wireless & 0x1) {
		wifi_rfkill = rfkill_alloc("hp-wifi", &device->dev,
					   RFKILL_TYPE_WLAN,
					   &hp_wmi_rfkill_ops,
					   (void *) HPWMI_WIFI);
		if (!wifi_rfkill)
			return -ENOMEM;
		rfkill_init_sw_state(wifi_rfkill,
				     hp_wmi_get_sw_state(HPWMI_WIFI));
		rfkill_set_hw_state(wifi_rfkill,
				    hp_wmi_get_hw_state(HPWMI_WIFI));
		err = rfkill_register(wifi_rfkill);
		if (err)
			goto register_wifi_error;
	}

	if (wireless & 0x2) {
		bluetooth_rfkill = rfkill_alloc("hp-bluetooth", &device->dev,
						RFKILL_TYPE_BLUETOOTH,
						&hp_wmi_rfkill_ops,
						(void *) HPWMI_BLUETOOTH);
		if (!bluetooth_rfkill) {
			err = -ENOMEM;
			goto register_wifi_error;
		}
		rfkill_init_sw_state(bluetooth_rfkill,
				     hp_wmi_get_sw_state(HPWMI_BLUETOOTH));
		rfkill_set_hw_state(bluetooth_rfkill,
				    hp_wmi_get_hw_state(HPWMI_BLUETOOTH));
		err = rfkill_register(bluetooth_rfkill);
		if (err)
			goto register_bluetooth_error;
	}

	if (wireless & 0x4) {
		wwan_rfkill = rfkill_alloc("hp-wwan", &device->dev,
					   RFKILL_TYPE_WWAN,
					   &hp_wmi_rfkill_ops,
					   (void *) HPWMI_WWAN);
		if (!wwan_rfkill) {
			err = -ENOMEM;
			goto register_bluetooth_error;
		}
		rfkill_init_sw_state(wwan_rfkill,
				     hp_wmi_get_sw_state(HPWMI_WWAN));
		rfkill_set_hw_state(wwan_rfkill,
				    hp_wmi_get_hw_state(HPWMI_WWAN));
		err = rfkill_register(wwan_rfkill);
		if (err)
			goto register_wwan_error;
	}

	if (wireless & 0x8) {
		gps_rfkill = rfkill_alloc("hp-gps", &device->dev,
						RFKILL_TYPE_GPS,
						&hp_wmi_rfkill_ops,
						(void *) HPWMI_GPS);
		if (!gps_rfkill) {
			err = -ENOMEM;
			goto register_wwan_error;
		}
		rfkill_init_sw_state(gps_rfkill,
				     hp_wmi_get_sw_state(HPWMI_GPS));
		rfkill_set_hw_state(gps_rfkill,
				    hp_wmi_get_hw_state(HPWMI_GPS));
		err = rfkill_register(gps_rfkill);
		if (err)
			goto register_gps_error;
	}

	return 0;
register_gps_error:
	rfkill_destroy(gps_rfkill);
	gps_rfkill = NULL;
	if (bluetooth_rfkill)
		rfkill_unregister(bluetooth_rfkill);
register_wwan_error:
	rfkill_destroy(wwan_rfkill);
	wwan_rfkill = NULL;
	if (gps_rfkill)
		rfkill_unregister(gps_rfkill);
register_bluetooth_error:
	rfkill_destroy(bluetooth_rfkill);
	bluetooth_rfkill = NULL;
	if (wifi_rfkill)
		rfkill_unregister(wifi_rfkill);
register_wifi_error:
	rfkill_destroy(wifi_rfkill);
	wifi_rfkill = NULL;
	return err;
}

static int __init hp_wmi_rfkill2_setup(struct platform_device *device)
{
	int err, i;
	struct bios_rfkill2_state state;
	err = hp_wmi_perform_query(HPWMI_WIRELESS2_QUERY, 0, &state,
				   0, sizeof(state));
	if (err)
		return err;

	if (state.count > HPWMI_MAX_RFKILL2_DEVICES) {
		pr_warn("unable to parse 0x1b query output\n");
		return -EINVAL;
	}

	for (i = 0; i < state.count; i++) {
		struct rfkill *rfkill;
		enum rfkill_type type;
		char *name;
		switch (state.device[i].radio_type) {
		case HPWMI_WIFI:
			type = RFKILL_TYPE_WLAN;
			name = "hp-wifi";
			break;
		case HPWMI_BLUETOOTH:
			type = RFKILL_TYPE_BLUETOOTH;
			name = "hp-bluetooth";
			break;
		case HPWMI_WWAN:
			type = RFKILL_TYPE_WWAN;
			name = "hp-wwan";
			break;
		case HPWMI_GPS:
			type = RFKILL_TYPE_GPS;
			name = "hp-gps";
			break;
		default:
			pr_warn("unknown device type 0x%x\n",
				state.device[i].radio_type);
			continue;
		}

		if (!state.device[i].vendor_id) {
			pr_warn("zero device %d while %d reported\n",
				i, state.count);
			continue;
		}

		rfkill = rfkill_alloc(name, &device->dev, type,
				      &hp_wmi_rfkill2_ops, (void *)(long)i);
		if (!rfkill) {
			err = -ENOMEM;
			goto fail;
		}

		rfkill2[rfkill2_count].id = state.device[i].rfkill_id;
		rfkill2[rfkill2_count].num = i;
		rfkill2[rfkill2_count].rfkill = rfkill;

		rfkill_init_sw_state(rfkill,
				     IS_SWBLOCKED(state.device[i].power));
		rfkill_set_hw_state(rfkill,
				    IS_HWBLOCKED(state.device[i].power));

		if (!(state.device[i].power & HPWMI_POWER_BIOS))
			pr_info("device %s blocked by BIOS\n", name);

		err = rfkill_register(rfkill);
		if (err) {
			rfkill_destroy(rfkill);
			goto fail;
		}

		rfkill2_count++;
	}

	return 0;
fail:
	for (; rfkill2_count > 0; rfkill2_count--) {
		rfkill_unregister(rfkill2[rfkill2_count - 1].rfkill);
		rfkill_destroy(rfkill2[rfkill2_count - 1].rfkill);
	}
	return err;
}

static int __init hp_wmi_bios_setup(struct platform_device *device)
{
	int err;

	/* clear detected rfkill devices */
	wifi_rfkill = NULL;
	bluetooth_rfkill = NULL;
	wwan_rfkill = NULL;
	gps_rfkill = NULL;
	rfkill2_count = 0;

	if (hp_wmi_rfkill_setup(device))
		hp_wmi_rfkill2_setup(device);

	err = device_create_file(&device->dev, &dev_attr_display);
	if (err)
		goto add_sysfs_error;
	err = device_create_file(&device->dev, &dev_attr_hddtemp);
	if (err)
		goto add_sysfs_error;
	err = device_create_file(&device->dev, &dev_attr_als);
	if (err)
		goto add_sysfs_error;
	err = device_create_file(&device->dev, &dev_attr_dock);
	if (err)
		goto add_sysfs_error;
	err = device_create_file(&device->dev, &dev_attr_tablet);
	if (err)
		goto add_sysfs_error;
	err = device_create_file(&device->dev, &dev_attr_postcode);
	if (err)
		goto add_sysfs_error;
	return 0;

add_sysfs_error:
	cleanup_sysfs(device);
	return err;
}

static int __exit hp_wmi_bios_remove(struct platform_device *device)
{
	int i;
	cleanup_sysfs(device);

	for (i = 0; i < rfkill2_count; i++) {
		rfkill_unregister(rfkill2[i].rfkill);
		rfkill_destroy(rfkill2[i].rfkill);
	}

	if (wifi_rfkill) {
		rfkill_unregister(wifi_rfkill);
		rfkill_destroy(wifi_rfkill);
	}
	if (bluetooth_rfkill) {
		rfkill_unregister(bluetooth_rfkill);
		rfkill_destroy(bluetooth_rfkill);
	}
	if (wwan_rfkill) {
		rfkill_unregister(wwan_rfkill);
		rfkill_destroy(wwan_rfkill);
	}
	if (gps_rfkill) {
		rfkill_unregister(gps_rfkill);
		rfkill_destroy(gps_rfkill);
	}

	return 0;
}

static int hp_wmi_resume_handler(struct device *device)
{
	/*
	 * Hardware state may have changed while suspended, so trigger
	 * input events for the current state. As this is a switch,
	 * the input layer will only actually pass it on if the state
	 * changed.
	 */
	if (hp_wmi_input_dev) {
		input_report_switch(hp_wmi_input_dev, SW_DOCK,
				    hp_wmi_dock_state());
		input_report_switch(hp_wmi_input_dev, SW_TABLET_MODE,
				    hp_wmi_tablet_state());
		input_sync(hp_wmi_input_dev);
	}

	if (rfkill2_count)
		hp_wmi_rfkill2_refresh();

	if (wifi_rfkill)
		rfkill_set_states(wifi_rfkill,
				  hp_wmi_get_sw_state(HPWMI_WIFI),
				  hp_wmi_get_hw_state(HPWMI_WIFI));
	if (bluetooth_rfkill)
		rfkill_set_states(bluetooth_rfkill,
				  hp_wmi_get_sw_state(HPWMI_BLUETOOTH),
				  hp_wmi_get_hw_state(HPWMI_BLUETOOTH));
	if (wwan_rfkill)
		rfkill_set_states(wwan_rfkill,
				  hp_wmi_get_sw_state(HPWMI_WWAN),
				  hp_wmi_get_hw_state(HPWMI_WWAN));
	if (gps_rfkill)
		rfkill_set_states(gps_rfkill,
				  hp_wmi_get_sw_state(HPWMI_GPS),
				  hp_wmi_get_hw_state(HPWMI_GPS));

	return 0;
}

static const struct dev_pm_ops hp_wmi_pm_ops = {
	.resume  = hp_wmi_resume_handler,
	.restore  = hp_wmi_resume_handler,
};

static struct platform_driver hp_wmi_driver = {
	.driver = {
		.name = "hp-wmi",
		.pm = &hp_wmi_pm_ops,
	},
	.remove = __exit_p(hp_wmi_bios_remove),
};

static int __init hp_wmi_init(void)
{
	int err;
	int event_capable = wmi_has_guid(HPWMI_EVENT_GUID);
	int bios_capable = wmi_has_guid(HPWMI_BIOS_GUID);

	if (!bios_capable && !event_capable)
		return -ENODEV;

	if (event_capable) {
		err = hp_wmi_input_setup();
		if (err)
			return err;
	}

	if (bios_capable) {
		hp_wmi_platform_dev =
			platform_device_register_simple("hp-wmi", -1, NULL, 0);
		if (IS_ERR(hp_wmi_platform_dev)) {
			err = PTR_ERR(hp_wmi_platform_dev);
			goto err_destroy_input;
		}

		err = platform_driver_probe(&hp_wmi_driver, hp_wmi_bios_setup);
		if (err)
			goto err_unregister_device;
	}

	return 0;

err_unregister_device:
	platform_device_unregister(hp_wmi_platform_dev);
err_destroy_input:
	if (event_capable)
		hp_wmi_input_destroy();

	return err;
}
module_init(hp_wmi_init);

static void __exit hp_wmi_exit(void)
{
	if (wmi_has_guid(HPWMI_EVENT_GUID))
		hp_wmi_input_destroy();

	if (hp_wmi_platform_dev) {
		platform_device_unregister(hp_wmi_platform_dev);
		platform_driver_unregister(&hp_wmi_driver);
	}
}
module_exit(hp_wmi_exit);
