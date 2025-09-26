// SPDX-License-Identifier: GPL-2.0+
/*
 * lg-laptop.c - LG Gram ACPI features and hotkeys Driver
 *
 * Copyright (C) 2018 Matan Ziv-Av <matan@svgalib.org>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/dmi.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <acpi/battery.h>

#define LED_DEVICE(_name, max, flag) struct led_classdev _name = { \
	.name           = __stringify(_name),   \
	.max_brightness = max,                  \
	.brightness_set = _name##_set,          \
	.brightness_get = _name##_get,          \
	.flags = flag,                          \
}

MODULE_AUTHOR("Matan Ziv-Av");
MODULE_DESCRIPTION("LG WMI Hotkey Driver");
MODULE_LICENSE("GPL");

static bool fw_debug;
module_param(fw_debug, bool, 0);
MODULE_PARM_DESC(fw_debug, "Enable printing of firmware debug messages");

#define LG_ADDRESS_SPACE_ID			0x8F

#define LG_ADDRESS_SPACE_DEBUG_FLAG_ADR		0x00
#define LG_ADDRESS_SPACE_FAN_MODE_ADR		0x03

#define LG_ADDRESS_SPACE_DTTM_FLAG_ADR		0x20
#define LG_ADDRESS_SPACE_CPU_TEMP_ADR		0x21
#define LG_ADDRESS_SPACE_CPU_TRIP_LOW_ADR	0x22
#define LG_ADDRESS_SPACE_CPU_TRIP_HIGH_ADR	0x23
#define LG_ADDRESS_SPACE_MB_TEMP_ADR		0x24
#define LG_ADDRESS_SPACE_MB_TRIP_LOW_ADR	0x25
#define LG_ADDRESS_SPACE_MB_TRIP_HIGH_ADR	0x26

#define LG_ADDRESS_SPACE_DEBUG_MSG_START_ADR	0x3E8
#define LG_ADDRESS_SPACE_DEBUG_MSG_END_ADR	0x5E8

#define WMI_EVENT_GUID0	"E4FB94F9-7F2B-4173-AD1A-CD1D95086248"
#define WMI_EVENT_GUID1	"023B133E-49D1-4E10-B313-698220140DC2"
#define WMI_EVENT_GUID2	"37BE1AC0-C3F2-4B1F-BFBE-8FDEAF2814D6"
#define WMI_EVENT_GUID3	"911BAD44-7DF8-4FBB-9319-BABA1C4B293B"
#define WMI_METHOD_WMAB "C3A72B38-D3EF-42D3-8CBB-D5A57049F66D"
#define WMI_METHOD_WMBB "2B4F501A-BD3C-4394-8DCF-00A7D2BC8210"
#define WMI_EVENT_GUID  WMI_EVENT_GUID0

#define SB_GGOV_METHOD  "\\_SB.GGOV"
#define GOV_TLED        0x2020008
#define WM_GET          1
#define WM_SET          2
#define WM_KEY_LIGHT    0x400
#define WM_TLED         0x404
#define WM_FN_LOCK      0x407
#define WM_BATT_LIMIT   0x61
#define WM_READER_MODE  0xBF
#define WM_FAN_MODE	0x33
#define WMBB_USB_CHARGE 0x10B
#define WMBB_BATT_LIMIT 0x10C

#define FAN_MODE_LOWER GENMASK(1, 0)
#define FAN_MODE_UPPER GENMASK(5, 4)

#define PLATFORM_NAME   "lg-laptop"

MODULE_ALIAS("wmi:" WMI_EVENT_GUID0);
MODULE_ALIAS("wmi:" WMI_EVENT_GUID1);
MODULE_ALIAS("wmi:" WMI_EVENT_GUID2);
MODULE_ALIAS("wmi:" WMI_EVENT_GUID3);
MODULE_ALIAS("wmi:" WMI_METHOD_WMAB);
MODULE_ALIAS("wmi:" WMI_METHOD_WMBB);

static struct platform_device *pf_device;
static struct input_dev *wmi_input_dev;

static u32 inited;
#define INIT_INPUT_WMI_0        0x01
#define INIT_INPUT_WMI_2        0x02
#define INIT_INPUT_ACPI         0x04
#define INIT_SPARSE_KEYMAP      0x80

static int battery_limit_use_wmbb;
static struct led_classdev kbd_backlight;
static enum led_brightness get_kbd_backlight_level(struct device *dev);

static const struct key_entry wmi_keymap[] = {
	{KE_KEY, 0x70, {KEY_F15} },	 /* LG control panel (F1) */
	{KE_KEY, 0x74, {KEY_F21} },	 /* Touchpad toggle (F5) */
	{KE_KEY, 0xf020000, {KEY_F14} }, /* Read mode (F9) */
	{KE_KEY, 0x10000000, {KEY_F16} },/* Keyboard backlight (F8) - pressing
					  * this key both sends an event and
					  * changes backlight level.
					  */
	{KE_END, 0}
};

static int ggov(u32 arg0)
{
	union acpi_object args[1];
	union acpi_object *r;
	acpi_status status;
	acpi_handle handle;
	struct acpi_object_list arg;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	int res;

	args[0].type = ACPI_TYPE_INTEGER;
	args[0].integer.value = arg0;

	status = acpi_get_handle(NULL, (acpi_string) SB_GGOV_METHOD, &handle);
	if (ACPI_FAILURE(status)) {
		pr_err("Cannot get handle");
		return -ENODEV;
	}

	arg.count = 1;
	arg.pointer = args;

	status = acpi_evaluate_object(handle, NULL, &arg, &buffer);
	if (ACPI_FAILURE(status)) {
		acpi_handle_err(handle, "GGOV: call failed.\n");
		return -EINVAL;
	}

	r = buffer.pointer;
	if (r->type != ACPI_TYPE_INTEGER) {
		kfree(r);
		return -EINVAL;
	}

	res = r->integer.value;
	kfree(r);

	return res;
}

static union acpi_object *lg_wmab(struct device *dev, u32 method, u32 arg1, u32 arg2)
{
	union acpi_object args[3];
	acpi_status status;
	struct acpi_object_list arg;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	args[0].type = ACPI_TYPE_INTEGER;
	args[0].integer.value = method;
	args[1].type = ACPI_TYPE_INTEGER;
	args[1].integer.value = arg1;
	args[2].type = ACPI_TYPE_INTEGER;
	args[2].integer.value = arg2;

	arg.count = 3;
	arg.pointer = args;

	status = acpi_evaluate_object(ACPI_HANDLE(dev), "WMAB", &arg, &buffer);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "WMAB: call failed.\n");
		return NULL;
	}

	return buffer.pointer;
}

static union acpi_object *lg_wmbb(struct device *dev, u32 method_id, u32 arg1, u32 arg2)
{
	union acpi_object args[3];
	acpi_status status;
	struct acpi_object_list arg;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	u8 buf[32];

	*(u32 *)buf = method_id;
	*(u32 *)(buf + 4) = arg1;
	*(u32 *)(buf + 16) = arg2;
	args[0].type = ACPI_TYPE_INTEGER;
	args[0].integer.value = 0; /* ignored */
	args[1].type = ACPI_TYPE_INTEGER;
	args[1].integer.value = 1; /* Must be 1 or 2. Does not matter which */
	args[2].type = ACPI_TYPE_BUFFER;
	args[2].buffer.length = 32;
	args[2].buffer.pointer = buf;

	arg.count = 3;
	arg.pointer = args;

	status = acpi_evaluate_object(ACPI_HANDLE(dev), "WMBB", &arg, &buffer);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "WMBB: call failed.\n");
		return NULL;
	}

	return (union acpi_object *)buffer.pointer;
}

static void wmi_notify(union acpi_object *obj, void *context)
{
	long data = (long)context;

	pr_debug("event guid %li\n", data);
	if (!obj)
		return;

	if (obj->type == ACPI_TYPE_INTEGER) {
		int eventcode = obj->integer.value;
		struct key_entry *key;

		if (eventcode == 0x10000000) {
			led_classdev_notify_brightness_hw_changed(
				&kbd_backlight, get_kbd_backlight_level(kbd_backlight.dev->parent));
		} else {
			key = sparse_keymap_entry_from_scancode(
				wmi_input_dev, eventcode);
			if (key && key->type == KE_KEY)
				sparse_keymap_report_entry(wmi_input_dev,
							   key, 1, true);
		}
	}

	pr_debug("Type: %i    Eventcode: 0x%llx\n", obj->type,
		 obj->integer.value);
}

static void wmi_input_setup(void)
{
	acpi_status status;

	wmi_input_dev = input_allocate_device();
	if (wmi_input_dev) {
		wmi_input_dev->name = "LG WMI hotkeys";
		wmi_input_dev->phys = "wmi/input0";
		wmi_input_dev->id.bustype = BUS_HOST;

		if (sparse_keymap_setup(wmi_input_dev, wmi_keymap, NULL) ||
		    input_register_device(wmi_input_dev)) {
			pr_info("Cannot initialize input device");
			input_free_device(wmi_input_dev);
			return;
		}

		inited |= INIT_SPARSE_KEYMAP;
		status = wmi_install_notify_handler(WMI_EVENT_GUID0, wmi_notify,
						    (void *)0);
		if (ACPI_SUCCESS(status))
			inited |= INIT_INPUT_WMI_0;

		status = wmi_install_notify_handler(WMI_EVENT_GUID2, wmi_notify,
						    (void *)2);
		if (ACPI_SUCCESS(status))
			inited |= INIT_INPUT_WMI_2;
	} else {
		pr_info("Cannot allocate input device");
	}
}

static void acpi_notify(struct acpi_device *device, u32 event)
{
	acpi_handle_debug(device->handle, "notify: %d\n", event);
}

static ssize_t fan_mode_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buffer, size_t count)
{
	unsigned long value;
	union acpi_object *r;
	int ret;

	ret = kstrtoul(buffer, 10, &value);
	if (ret)
		return ret;
	if (value >= 3)
		return -EINVAL;

	r = lg_wmab(dev, WM_FAN_MODE, WM_SET,
		FIELD_PREP(FAN_MODE_LOWER, value) |
		FIELD_PREP(FAN_MODE_UPPER, value));
	kfree(r);

	return count;
}

static ssize_t fan_mode_show(struct device *dev,
			     struct device_attribute *attr, char *buffer)
{
	unsigned int mode;
	union acpi_object *r;

	r = lg_wmab(dev, WM_FAN_MODE, WM_GET, 0);
	if (!r)
		return -EIO;

	if (r->type != ACPI_TYPE_INTEGER) {
		kfree(r);
		return -EIO;
	}

	mode = FIELD_GET(FAN_MODE_LOWER, r->integer.value);
	kfree(r);

	return sysfs_emit(buffer, "%d\n", mode);
}

static ssize_t usb_charge_store(struct device *dev,
				struct device_attribute *attr,
				const char *buffer, size_t count)
{
	bool value;
	union acpi_object *r;
	int ret;

	ret = kstrtobool(buffer, &value);
	if (ret)
		return ret;

	r = lg_wmbb(dev, WMBB_USB_CHARGE, WM_SET, value);
	if (!r)
		return -EIO;

	kfree(r);
	return count;
}

static ssize_t usb_charge_show(struct device *dev,
			       struct device_attribute *attr, char *buffer)
{
	unsigned int status;
	union acpi_object *r;

	r = lg_wmbb(dev, WMBB_USB_CHARGE, WM_GET, 0);
	if (!r)
		return -EIO;

	if (r->type != ACPI_TYPE_BUFFER) {
		kfree(r);
		return -EIO;
	}

	status = !!r->buffer.pointer[0x10];

	kfree(r);

	return sysfs_emit(buffer, "%d\n", status);
}

static ssize_t reader_mode_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buffer, size_t count)
{
	bool value;
	union acpi_object *r;
	int ret;

	ret = kstrtobool(buffer, &value);
	if (ret)
		return ret;

	r = lg_wmab(dev, WM_READER_MODE, WM_SET, value);
	if (!r)
		return -EIO;

	kfree(r);
	return count;
}

static ssize_t reader_mode_show(struct device *dev,
				struct device_attribute *attr, char *buffer)
{
	unsigned int status;
	union acpi_object *r;

	r = lg_wmab(dev, WM_READER_MODE, WM_GET, 0);
	if (!r)
		return -EIO;

	if (r->type != ACPI_TYPE_INTEGER) {
		kfree(r);
		return -EIO;
	}

	status = !!r->integer.value;

	kfree(r);

	return sysfs_emit(buffer, "%d\n", status);
}

static ssize_t fn_lock_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buffer, size_t count)
{
	bool value;
	union acpi_object *r;
	int ret;

	ret = kstrtobool(buffer, &value);
	if (ret)
		return ret;

	r = lg_wmab(dev, WM_FN_LOCK, WM_SET, value);
	if (!r)
		return -EIO;

	kfree(r);
	return count;
}

static ssize_t fn_lock_show(struct device *dev,
			    struct device_attribute *attr, char *buffer)
{
	unsigned int status;
	union acpi_object *r;

	r = lg_wmab(dev, WM_FN_LOCK, WM_GET, 0);
	if (!r)
		return -EIO;

	if (r->type != ACPI_TYPE_BUFFER) {
		kfree(r);
		return -EIO;
	}

	status = !!r->buffer.pointer[0];
	kfree(r);

	return sysfs_emit(buffer, "%d\n", status);
}

static ssize_t charge_control_end_threshold_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t count)
{
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 10, &value);
	if (ret)
		return ret;

	if (value == 100 || value == 80) {
		union acpi_object *r;

		if (battery_limit_use_wmbb)
			r = lg_wmbb(&pf_device->dev, WMBB_BATT_LIMIT, WM_SET, value);
		else
			r = lg_wmab(&pf_device->dev, WM_BATT_LIMIT, WM_SET, value);
		if (!r)
			return -EIO;

		kfree(r);
		return count;
	}

	return -EINVAL;
}

static ssize_t charge_control_end_threshold_show(struct device *device,
						 struct device_attribute *attr,
						 char *buf)
{
	unsigned int status;
	union acpi_object *r;

	if (battery_limit_use_wmbb) {
		r = lg_wmbb(&pf_device->dev, WMBB_BATT_LIMIT, WM_GET, 0);
		if (!r)
			return -EIO;

		if (r->type != ACPI_TYPE_BUFFER) {
			kfree(r);
			return -EIO;
		}

		status = r->buffer.pointer[0x10];
	} else {
		r = lg_wmab(&pf_device->dev, WM_BATT_LIMIT, WM_GET, 0);
		if (!r)
			return -EIO;

		if (r->type != ACPI_TYPE_INTEGER) {
			kfree(r);
			return -EIO;
		}

		status = r->integer.value;
	}
	kfree(r);
	if (status != 80 && status != 100)
		status = 0;

	return sysfs_emit(buf, "%d\n", status);
}

static ssize_t battery_care_limit_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buffer)
{
	return charge_control_end_threshold_show(dev, attr, buffer);
}

static ssize_t battery_care_limit_store(struct device *dev,
					struct device_attribute *attr,
					const char *buffer, size_t count)
{
	return charge_control_end_threshold_store(dev, attr, buffer, count);
}

static DEVICE_ATTR_RW(fan_mode);
static DEVICE_ATTR_RW(usb_charge);
static DEVICE_ATTR_RW(reader_mode);
static DEVICE_ATTR_RW(fn_lock);
static DEVICE_ATTR_RW(charge_control_end_threshold);
static DEVICE_ATTR_RW(battery_care_limit);

static int lg_battery_add(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	if (device_create_file(&battery->dev,
			       &dev_attr_charge_control_end_threshold))
		return -ENODEV;

	return 0;
}

static int lg_battery_remove(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	device_remove_file(&battery->dev,
			   &dev_attr_charge_control_end_threshold);
	return 0;
}

static struct acpi_battery_hook battery_hook = {
	.add_battery = lg_battery_add,
	.remove_battery = lg_battery_remove,
	.name = "LG Battery Extension",
};

static struct attribute *dev_attributes[] = {
	&dev_attr_fan_mode.attr,
	&dev_attr_usb_charge.attr,
	&dev_attr_reader_mode.attr,
	&dev_attr_fn_lock.attr,
	&dev_attr_battery_care_limit.attr,
	NULL
};

static const struct attribute_group dev_attribute_group = {
	.attrs = dev_attributes,
};

static void tpad_led_set(struct led_classdev *cdev,
			 enum led_brightness brightness)
{
	union acpi_object *r;

	r = lg_wmab(cdev->dev->parent, WM_TLED, WM_SET, brightness > LED_OFF);
	kfree(r);
}

static enum led_brightness tpad_led_get(struct led_classdev *cdev)
{
	return ggov(GOV_TLED) > 0 ? LED_ON : LED_OFF;
}

static LED_DEVICE(tpad_led, 1, 0);

static void kbd_backlight_set(struct led_classdev *cdev,
			      enum led_brightness brightness)
{
	u32 val;
	union acpi_object *r;

	val = 0x22;
	if (brightness <= LED_OFF)
		val = 0;
	if (brightness >= LED_FULL)
		val = 0x24;
	r = lg_wmab(cdev->dev->parent, WM_KEY_LIGHT, WM_SET, val);
	kfree(r);
}

static enum led_brightness get_kbd_backlight_level(struct device *dev)
{
	union acpi_object *r;
	int val;

	r = lg_wmab(dev, WM_KEY_LIGHT, WM_GET, 0);

	if (!r)
		return LED_OFF;

	if (r->type != ACPI_TYPE_BUFFER || r->buffer.pointer[1] != 0x05) {
		kfree(r);
		return LED_OFF;
	}

	switch (r->buffer.pointer[0] & 0x27) {
	case 0x24:
		val = LED_FULL;
		break;
	case 0x22:
		val = LED_HALF;
		break;
	default:
		val = LED_OFF;
	}

	kfree(r);

	return val;
}

static enum led_brightness kbd_backlight_get(struct led_classdev *cdev)
{
	return get_kbd_backlight_level(cdev->dev->parent);
}

static LED_DEVICE(kbd_backlight, 255, LED_BRIGHT_HW_CHANGED);

static void wmi_input_destroy(void)
{
	if (inited & INIT_INPUT_WMI_2)
		wmi_remove_notify_handler(WMI_EVENT_GUID2);

	if (inited & INIT_INPUT_WMI_0)
		wmi_remove_notify_handler(WMI_EVENT_GUID0);

	if (inited & INIT_SPARSE_KEYMAP)
		input_unregister_device(wmi_input_dev);

	inited &= ~(INIT_INPUT_WMI_0 | INIT_INPUT_WMI_2 | INIT_SPARSE_KEYMAP);
}

static struct platform_driver pf_driver = {
	.driver = {
		   .name = PLATFORM_NAME,
	}
};

static acpi_status lg_laptop_address_space_write(struct device *dev, acpi_physical_address address,
						 size_t size, u64 value)
{
	u8 byte;

	/* Ignore any debug messages */
	if (address >= LG_ADDRESS_SPACE_DEBUG_MSG_START_ADR &&
	    address <= LG_ADDRESS_SPACE_DEBUG_MSG_END_ADR)
		return AE_OK;

	if (size != sizeof(byte))
		return AE_BAD_PARAMETER;

	byte = value & 0xFF;

	switch (address) {
	case LG_ADDRESS_SPACE_FAN_MODE_ADR:
		/*
		 * The fan mode field is not affected by the DTTM flag, so we
		 * have to manually check fw_debug.
		 */
		if (fw_debug)
			dev_dbg(dev, "Fan mode set to mode %u\n", byte);

		return AE_OK;
	case LG_ADDRESS_SPACE_CPU_TEMP_ADR:
		dev_dbg(dev, "CPU temperature is %u °C\n", byte);
		return AE_OK;
	case LG_ADDRESS_SPACE_CPU_TRIP_LOW_ADR:
		dev_dbg(dev, "CPU lower trip point set to %u °C\n", byte);
		return AE_OK;
	case LG_ADDRESS_SPACE_CPU_TRIP_HIGH_ADR:
		dev_dbg(dev, "CPU higher trip point set to %u °C\n", byte);
		return AE_OK;
	case LG_ADDRESS_SPACE_MB_TEMP_ADR:
		dev_dbg(dev, "Motherboard temperature is %u °C\n", byte);
		return AE_OK;
	case LG_ADDRESS_SPACE_MB_TRIP_LOW_ADR:
		dev_dbg(dev, "Motherboard lower trip point set to %u °C\n", byte);
		return AE_OK;
	case LG_ADDRESS_SPACE_MB_TRIP_HIGH_ADR:
		dev_dbg(dev, "Motherboard higher trip point set to %u °C\n", byte);
		return AE_OK;
	default:
		dev_notice_ratelimited(dev, "Ignoring write to unknown opregion address %llu\n",
				       address);
		return AE_OK;
	}
}

static acpi_status lg_laptop_address_space_read(struct device *dev, acpi_physical_address address,
						size_t size, u64 *value)
{
	if (size != 1)
		return AE_BAD_PARAMETER;

	switch (address) {
	case LG_ADDRESS_SPACE_DEBUG_FLAG_ADR:
		/* Debug messages are already printed using the standard ACPI Debug object */
		*value = 0x00;
		return AE_OK;
	case LG_ADDRESS_SPACE_DTTM_FLAG_ADR:
		*value = fw_debug;
		return AE_OK;
	default:
		dev_notice_ratelimited(dev, "Attempt to read unknown opregion address %llu\n",
				       address);
		return AE_BAD_PARAMETER;
	}
}

static acpi_status lg_laptop_address_space_handler(u32 function, acpi_physical_address address,
						   u32 bits, u64 *value, void *handler_context,
						   void *region_context)
{
	struct device *dev = handler_context;
	size_t size;

	if (bits % BITS_PER_BYTE)
		return AE_BAD_PARAMETER;

	size = bits / BITS_PER_BYTE;

	switch (function) {
	case ACPI_READ:
		return lg_laptop_address_space_read(dev, address, size, value);
	case ACPI_WRITE:
		return lg_laptop_address_space_write(dev, address, size, *value);
	default:
		return AE_BAD_PARAMETER;
	}
}

static void lg_laptop_remove_address_space_handler(void *data)
{
	struct acpi_device *device = data;

	acpi_remove_address_space_handler(device->handle, LG_ADDRESS_SPACE_ID,
					  &lg_laptop_address_space_handler);
}

static int acpi_add(struct acpi_device *device)
{
	struct platform_device_info pdev_info = {
		.fwnode = acpi_fwnode_handle(device),
		.name = PLATFORM_NAME,
		.id = PLATFORM_DEVID_NONE,
	};
	acpi_status status;
	int ret;
	const char *product;
	int year = 2017;

	if (pf_device)
		return 0;

	status = acpi_install_address_space_handler(device->handle, LG_ADDRESS_SPACE_ID,
						    &lg_laptop_address_space_handler,
						    NULL, &device->dev);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	ret = devm_add_action_or_reset(&device->dev, lg_laptop_remove_address_space_handler,
				       device);
	if (ret < 0)
		return ret;

	ret = platform_driver_register(&pf_driver);
	if (ret)
		return ret;

	pf_device = platform_device_register_full(&pdev_info);
	if (IS_ERR(pf_device)) {
		ret = PTR_ERR(pf_device);
		pf_device = NULL;
		pr_err("unable to register platform device\n");
		goto out_platform_registered;
	}
	product = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (product && strlen(product) > 4)
		switch (product[4]) {
		case '5':
			if (strlen(product) > 5)
				switch (product[5]) {
				case 'N':
					year = 2021;
					break;
				case '0':
					year = 2016;
					break;
				default:
					year = 2022;
				}
			break;
		case '6':
			year = 2016;
			break;
		case '7':
			year = 2017;
			break;
		case '8':
			year = 2018;
			break;
		case '9':
			year = 2019;
			break;
		case '0':
			if (strlen(product) > 5)
				switch (product[5]) {
				case 'N':
					year = 2020;
					break;
				case 'P':
					year = 2021;
					break;
				default:
					year = 2022;
				}
			break;
		default:
			year = 2019;
		}
	pr_info("product: %s  year: %d\n", product ?: "unknown", year);

	if (year >= 2019)
		battery_limit_use_wmbb = 1;

	ret = sysfs_create_group(&pf_device->dev.kobj, &dev_attribute_group);
	if (ret)
		goto out_platform_device;

	/* LEDs are optional */
	led_classdev_register(&pf_device->dev, &kbd_backlight);
	led_classdev_register(&pf_device->dev, &tpad_led);

	wmi_input_setup();
	battery_hook_register(&battery_hook);

	return 0;

out_platform_device:
	platform_device_unregister(pf_device);
out_platform_registered:
	platform_driver_unregister(&pf_driver);
	return ret;
}

static void acpi_remove(struct acpi_device *device)
{
	sysfs_remove_group(&pf_device->dev.kobj, &dev_attribute_group);

	led_classdev_unregister(&tpad_led);
	led_classdev_unregister(&kbd_backlight);

	battery_hook_unregister(&battery_hook);
	wmi_input_destroy();
	platform_device_unregister(pf_device);
	pf_device = NULL;
	platform_driver_unregister(&pf_driver);
}

static const struct acpi_device_id device_ids[] = {
	{"LGEX0820", 0},
	{"", 0}
};
MODULE_DEVICE_TABLE(acpi, device_ids);

static struct acpi_driver acpi_driver = {
	.name = "LG Gram Laptop Support",
	.class = "lg-laptop",
	.ids = device_ids,
	.ops = {
		.add = acpi_add,
		.remove = acpi_remove,
		.notify = acpi_notify,
		},
};

static int __init acpi_init(void)
{
	int result;

	result = acpi_bus_register_driver(&acpi_driver);
	if (result < 0) {
		pr_debug("Error registering driver\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit acpi_exit(void)
{
	acpi_bus_unregister_driver(&acpi_driver);
}

module_init(acpi_init);
module_exit(acpi_exit);
