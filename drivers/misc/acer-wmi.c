/*
 *  Acer WMI Laptop Extras
 *
 *  Copyright (C) 2007-2008	Carlos Corbacho <carlos@strangeworlds.co.uk>
 *
 *  Based on acer_acpi:
 *    Copyright (C) 2005-2007	E.M. Smith
 *    Copyright (C) 2007-2008	Carlos Corbacho <cathectic@gmail.com>
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

#define ACER_WMI_VERSION	"0.1"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/dmi.h>
#include <linux/backlight.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/i8042.h>

#include <acpi/acpi_drivers.h>

MODULE_AUTHOR("Carlos Corbacho");
MODULE_DESCRIPTION("Acer Laptop WMI Extras Driver");
MODULE_LICENSE("GPL");

#define ACER_LOGPREFIX "acer-wmi: "
#define ACER_ERR KERN_ERR ACER_LOGPREFIX
#define ACER_NOTICE KERN_NOTICE ACER_LOGPREFIX
#define ACER_INFO KERN_INFO ACER_LOGPREFIX

/*
 * The following defines quirks to get some specific functions to work
 * which are known to not be supported over ACPI-WMI (such as the mail LED
 * on WMID based Acer's)
 */
struct acer_quirks {
	const char *vendor;
	const char *model;
	u16 quirks;
};

/*
 * Magic Number
 * Meaning is unknown - this number is required for writing to ACPI for AMW0
 * (it's also used in acerhk when directly accessing the BIOS)
 */
#define ACER_AMW0_WRITE	0x9610

/*
 * Bit masks for the AMW0 interface
 */
#define ACER_AMW0_WIRELESS_MASK  0x35
#define ACER_AMW0_BLUETOOTH_MASK 0x34
#define ACER_AMW0_MAILLED_MASK   0x31

/*
 * Method IDs for WMID interface
 */
#define ACER_WMID_GET_WIRELESS_METHODID		1
#define ACER_WMID_GET_BLUETOOTH_METHODID	2
#define ACER_WMID_GET_BRIGHTNESS_METHODID	3
#define ACER_WMID_SET_WIRELESS_METHODID		4
#define ACER_WMID_SET_BLUETOOTH_METHODID	5
#define ACER_WMID_SET_BRIGHTNESS_METHODID	6
#define ACER_WMID_GET_THREEG_METHODID		10
#define ACER_WMID_SET_THREEG_METHODID		11

/*
 * Acer ACPI method GUIDs
 */
#define AMW0_GUID1		"67C3371D-95A3-4C37-BB61-DD47B491DAAB"
#define WMID_GUID1		"6AF4F258-B401-42fd-BE91-3D4AC2D7C0D3"
#define WMID_GUID2		"95764E09-FB56-4e83-B31A-37761F60994A"

MODULE_ALIAS("wmi:67C3371D-95A3-4C37-BB61-DD47B491DAAB");
MODULE_ALIAS("wmi:6AF4F258-B401-42fd-BE91-3D4AC2D7C0D3");

/* Temporary workaround until the WMI sysfs interface goes in */
MODULE_ALIAS("dmi:*:*Acer*:*:");

/*
 * Interface capability flags
 */
#define ACER_CAP_MAILLED		(1<<0)
#define ACER_CAP_WIRELESS		(1<<1)
#define ACER_CAP_BLUETOOTH		(1<<2)
#define ACER_CAP_BRIGHTNESS		(1<<3)
#define ACER_CAP_THREEG			(1<<4)
#define ACER_CAP_ANY			(0xFFFFFFFF)

/*
 * Interface type flags
 */
enum interface_flags {
	ACER_AMW0,
	ACER_AMW0_V2,
	ACER_WMID,
};

#define ACER_DEFAULT_WIRELESS  0
#define ACER_DEFAULT_BLUETOOTH 0
#define ACER_DEFAULT_MAILLED   0
#define ACER_DEFAULT_THREEG    0

static int max_brightness = 0xF;

static int wireless = -1;
static int bluetooth = -1;
static int mailled = -1;
static int brightness = -1;
static int threeg = -1;
static int force_series;

module_param(mailled, int, 0444);
module_param(wireless, int, 0444);
module_param(bluetooth, int, 0444);
module_param(brightness, int, 0444);
module_param(threeg, int, 0444);
module_param(force_series, int, 0444);
MODULE_PARM_DESC(wireless, "Set initial state of Wireless hardware");
MODULE_PARM_DESC(bluetooth, "Set initial state of Bluetooth hardware");
MODULE_PARM_DESC(mailled, "Set initial state of Mail LED");
MODULE_PARM_DESC(brightness, "Set initial LCD backlight brightness");
MODULE_PARM_DESC(threeg, "Set initial state of 3G hardware");
MODULE_PARM_DESC(force_series, "Force a different laptop series");

struct acer_data {
	int mailled;
	int wireless;
	int bluetooth;
	int threeg;
	int brightness;
};

/* Each low-level interface must define at least some of the following */
struct wmi_interface {
	/* The WMI device type */
	u32 type;

	/* The capabilities this interface provides */
	u32 capability;

	/* Private data for the current interface */
	struct acer_data data;
};

/* The static interface pointer, points to the currently detected interface */
static struct wmi_interface *interface;

/*
 * Embedded Controller quirks
 * Some laptops require us to directly access the EC to either enable or query
 * features that are not available through WMI.
 */

struct quirk_entry {
	u8 wireless;
	u8 mailled;
	u8 brightness;
	u8 bluetooth;
};

static struct quirk_entry *quirks;

static void set_quirks(void)
{
	if (quirks->mailled)
		interface->capability |= ACER_CAP_MAILLED;

	if (quirks->brightness)
		interface->capability |= ACER_CAP_BRIGHTNESS;
}

static int dmi_matched(const struct dmi_system_id *dmi)
{
	quirks = dmi->driver_data;
	return 0;
}

static struct quirk_entry quirk_unknown = {
};

static struct quirk_entry quirk_acer_travelmate_2490 = {
	.mailled = 1,
};

/* This AMW0 laptop has no bluetooth */
static struct quirk_entry quirk_medion_md_98300 = {
	.wireless = 1,
};

static struct dmi_system_id acer_quirks[] = {
	{
		.callback = dmi_matched,
		.ident = "Acer Aspire 3100",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 3100"),
		},
		.driver_data = &quirk_acer_travelmate_2490,
	},
	{
		.callback = dmi_matched,
		.ident = "Acer Aspire 5100",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5100"),
		},
		.driver_data = &quirk_acer_travelmate_2490,
	},
	{
		.callback = dmi_matched,
		.ident = "Acer Aspire 5630",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5630"),
		},
		.driver_data = &quirk_acer_travelmate_2490,
	},
	{
		.callback = dmi_matched,
		.ident = "Acer Aspire 5650",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5650"),
		},
		.driver_data = &quirk_acer_travelmate_2490,
	},
	{
		.callback = dmi_matched,
		.ident = "Acer Aspire 5680",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5680"),
		},
		.driver_data = &quirk_acer_travelmate_2490,
	},
	{
		.callback = dmi_matched,
		.ident = "Acer Aspire 9110",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 9110"),
		},
		.driver_data = &quirk_acer_travelmate_2490,
	},
	{
		.callback = dmi_matched,
		.ident = "Acer TravelMate 2490",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 2490"),
		},
		.driver_data = &quirk_acer_travelmate_2490,
	},
	{
		.callback = dmi_matched,
		.ident = "Acer TravelMate 4200",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 4200"),
		},
		.driver_data = &quirk_acer_travelmate_2490,
	},
	{
		.callback = dmi_matched,
		.ident = "Medion MD 98300",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDION"),
			DMI_MATCH(DMI_PRODUCT_NAME, "WAM2030"),
		},
		.driver_data = &quirk_medion_md_98300,
	},
	{}
};

/* Find which quirks are needed for a particular vendor/ model pair */
static void find_quirks(void)
{
	if (!force_series) {
		dmi_check_system(acer_quirks);
	} else if (force_series == 2490) {
		quirks = &quirk_acer_travelmate_2490;
	}

	if (quirks == NULL)
		quirks = &quirk_unknown;

	set_quirks();
}

/*
 * General interface convenience methods
 */

static bool has_cap(u32 cap)
{
	if ((interface->capability & cap) != 0)
		return 1;

	return 0;
}

/*
 * AMW0 (V1) interface
 */
struct wmab_args {
	u32 eax;
	u32 ebx;
	u32 ecx;
	u32 edx;
};

struct wmab_ret {
	u32 eax;
	u32 ebx;
	u32 ecx;
	u32 edx;
	u32 eex;
};

static acpi_status wmab_execute(struct wmab_args *regbuf,
struct acpi_buffer *result)
{
	struct acpi_buffer input;
	acpi_status status;
	input.length = sizeof(struct wmab_args);
	input.pointer = (u8 *)regbuf;

	status = wmi_evaluate_method(AMW0_GUID1, 1, 1, &input, result);

	return status;
}

static acpi_status AMW0_get_u32(u32 *value, u32 cap,
struct wmi_interface *iface)
{
	int err;
	u8 result;

	switch (cap) {
	case ACER_CAP_MAILLED:
		switch (quirks->mailled) {
		default:
			err = ec_read(0xA, &result);
			if (err)
				return AE_ERROR;
			*value = (result >> 7) & 0x1;
			return AE_OK;
		}
		break;
	case ACER_CAP_WIRELESS:
		switch (quirks->wireless) {
		case 1:
			err = ec_read(0x7B, &result);
			if (err)
				return AE_ERROR;
			*value = result & 0x1;
			return AE_OK;
		default:
			err = ec_read(0xA, &result);
			if (err)
				return AE_ERROR;
			*value = (result >> 2) & 0x1;
			return AE_OK;
		}
		break;
	case ACER_CAP_BLUETOOTH:
		switch (quirks->bluetooth) {
		default:
			err = ec_read(0xA, &result);
			if (err)
				return AE_ERROR;
			*value = (result >> 4) & 0x1;
			return AE_OK;
		}
		break;
	case ACER_CAP_BRIGHTNESS:
		switch (quirks->brightness) {
		default:
			err = ec_read(0x83, &result);
			if (err)
				return AE_ERROR;
			*value = result;
			return AE_OK;
		}
		break;
	default:
		return AE_BAD_ADDRESS;
	}
	return AE_OK;
}

static acpi_status AMW0_set_u32(u32 value, u32 cap, struct wmi_interface *iface)
{
	struct wmab_args args;

	args.eax = ACER_AMW0_WRITE;
	args.ebx = value ? (1<<8) : 0;
	args.ecx = args.edx = 0;

	switch (cap) {
	case ACER_CAP_MAILLED:
		if (value > 1)
			return AE_BAD_PARAMETER;
		args.ebx |= ACER_AMW0_MAILLED_MASK;
		break;
	case ACER_CAP_WIRELESS:
		if (value > 1)
			return AE_BAD_PARAMETER;
		args.ebx |= ACER_AMW0_WIRELESS_MASK;
		break;
	case ACER_CAP_BLUETOOTH:
		if (value > 1)
			return AE_BAD_PARAMETER;
		args.ebx |= ACER_AMW0_BLUETOOTH_MASK;
		break;
	case ACER_CAP_BRIGHTNESS:
		if (value > max_brightness)
			return AE_BAD_PARAMETER;
		switch (quirks->brightness) {
		default:
			return ec_write(0x83, value);
			break;
		}
	default:
		return AE_BAD_ADDRESS;
	}

	/* Actually do the set */
	return wmab_execute(&args, NULL);
}

static acpi_status AMW0_find_mailled(void)
{
	struct wmab_args args;
	struct wmab_ret ret;
	acpi_status status = AE_OK;
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;

	args.eax = 0x86;
	args.ebx = args.ecx = args.edx = 0;

	status = wmab_execute(&args, &out);
	if (ACPI_FAILURE(status))
		return status;

	obj = (union acpi_object *) out.pointer;
	if (obj && obj->type == ACPI_TYPE_BUFFER &&
	obj->buffer.length == sizeof(struct wmab_ret)) {
		ret = *((struct wmab_ret *) obj->buffer.pointer);
	} else {
		return AE_ERROR;
	}

	if (ret.eex & 0x1)
		interface->capability |= ACER_CAP_MAILLED;

	kfree(out.pointer);

	return AE_OK;
}

static acpi_status AMW0_set_capabilities(void)
{
	struct wmab_args args;
	struct wmab_ret ret;
	acpi_status status = AE_OK;
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;

	args.eax = ACER_AMW0_WRITE;
	args.ecx = args.edx = 0;

	args.ebx = 0xa2 << 8;
	args.ebx |= ACER_AMW0_WIRELESS_MASK;

	status = wmab_execute(&args, &out);
	if (ACPI_FAILURE(status))
		return status;

	obj = (union acpi_object *) out.pointer;
	if (obj && obj->type == ACPI_TYPE_BUFFER &&
	obj->buffer.length == sizeof(struct wmab_ret)) {
		ret = *((struct wmab_ret *) obj->buffer.pointer);
	} else {
		return AE_ERROR;
	}

	if (ret.eax & 0x1)
		interface->capability |= ACER_CAP_WIRELESS;

	args.ebx = 2 << 8;
	args.ebx |= ACER_AMW0_BLUETOOTH_MASK;

	status = wmab_execute(&args, &out);
	if (ACPI_FAILURE(status))
		return status;

	obj = (union acpi_object *) out.pointer;
	if (obj && obj->type == ACPI_TYPE_BUFFER
	&& obj->buffer.length == sizeof(struct wmab_ret)) {
		ret = *((struct wmab_ret *) obj->buffer.pointer);
	} else {
		return AE_ERROR;
	}

	if (ret.eax & 0x1)
		interface->capability |= ACER_CAP_BLUETOOTH;

	kfree(out.pointer);

	/*
	 * This appears to be safe to enable, since all Wistron based laptops
	 * appear to use the same EC register for brightness, even if they
	 * differ for wireless, etc
	 */
	interface->capability |= ACER_CAP_BRIGHTNESS;

	return AE_OK;
}

static struct wmi_interface AMW0_interface = {
	.type = ACER_AMW0,
};

static struct wmi_interface AMW0_V2_interface = {
	.type = ACER_AMW0_V2,
};

/*
 * New interface (The WMID interface)
 */
static acpi_status
WMI_execute_u32(u32 method_id, u32 in, u32 *out)
{
	struct acpi_buffer input = { (acpi_size) sizeof(u32), (void *)(&in) };
	struct acpi_buffer result = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	u32 tmp;
	acpi_status status;

	status = wmi_evaluate_method(WMID_GUID1, 1, method_id, &input, &result);

	if (ACPI_FAILURE(status))
		return status;

	obj = (union acpi_object *) result.pointer;
	if (obj && obj->type == ACPI_TYPE_BUFFER &&
		obj->buffer.length == sizeof(u32)) {
		tmp = *((u32 *) obj->buffer.pointer);
	} else {
		tmp = 0;
	}

	if (out)
		*out = tmp;

	kfree(result.pointer);

	return status;
}

static acpi_status WMID_get_u32(u32 *value, u32 cap,
struct wmi_interface *iface)
{
	acpi_status status;
	u8 tmp;
	u32 result, method_id = 0;

	switch (cap) {
	case ACER_CAP_WIRELESS:
		method_id = ACER_WMID_GET_WIRELESS_METHODID;
		break;
	case ACER_CAP_BLUETOOTH:
		method_id = ACER_WMID_GET_BLUETOOTH_METHODID;
		break;
	case ACER_CAP_BRIGHTNESS:
		method_id = ACER_WMID_GET_BRIGHTNESS_METHODID;
		break;
	case ACER_CAP_THREEG:
		method_id = ACER_WMID_GET_THREEG_METHODID;
		break;
	case ACER_CAP_MAILLED:
		if (quirks->mailled == 1) {
			ec_read(0x9f, &tmp);
			*value = tmp & 0x1;
			return 0;
		}
	default:
		return AE_BAD_ADDRESS;
	}
	status = WMI_execute_u32(method_id, 0, &result);

	if (ACPI_SUCCESS(status))
		*value = (u8)result;

	return status;
}

static acpi_status WMID_set_u32(u32 value, u32 cap, struct wmi_interface *iface)
{
	u32 method_id = 0;
	char param;

	switch (cap) {
	case ACER_CAP_BRIGHTNESS:
		if (value > max_brightness)
			return AE_BAD_PARAMETER;
		method_id = ACER_WMID_SET_BRIGHTNESS_METHODID;
		break;
	case ACER_CAP_WIRELESS:
		if (value > 1)
			return AE_BAD_PARAMETER;
		method_id = ACER_WMID_SET_WIRELESS_METHODID;
		break;
	case ACER_CAP_BLUETOOTH:
		if (value > 1)
			return AE_BAD_PARAMETER;
		method_id = ACER_WMID_SET_BLUETOOTH_METHODID;
		break;
	case ACER_CAP_THREEG:
		if (value > 1)
			return AE_BAD_PARAMETER;
		method_id = ACER_WMID_SET_THREEG_METHODID;
		break;
	case ACER_CAP_MAILLED:
		if (value > 1)
			return AE_BAD_PARAMETER;
		if (quirks->mailled == 1) {
			param = value ? 0x92 : 0x93;
			i8042_command(&param, 0x1059);
			return 0;
		}
		break;
	default:
		return AE_BAD_ADDRESS;
	}
	return WMI_execute_u32(method_id, (u32)value, NULL);
}

static acpi_status WMID_set_capabilities(void)
{
	struct acpi_buffer out = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *obj;
	acpi_status status;
	u32 devices;

	status = wmi_query_block(WMID_GUID2, 1, &out);
	if (ACPI_FAILURE(status))
		return status;

	obj = (union acpi_object *) out.pointer;
	if (obj && obj->type == ACPI_TYPE_BUFFER &&
		obj->buffer.length == sizeof(u32)) {
		devices = *((u32 *) obj->buffer.pointer);
	} else {
		return AE_ERROR;
	}

	/* Not sure on the meaning of the relevant bits yet to detect these */
	interface->capability |= ACER_CAP_WIRELESS;
	interface->capability |= ACER_CAP_THREEG;

	/* WMID always provides brightness methods */
	interface->capability |= ACER_CAP_BRIGHTNESS;

	if (devices & 0x10)
		interface->capability |= ACER_CAP_BLUETOOTH;

	if (!(devices & 0x20))
		max_brightness = 0x9;

	return status;
}

static struct wmi_interface wmid_interface = {
	.type = ACER_WMID,
};

/*
 * Generic Device (interface-independent)
 */

static acpi_status get_u32(u32 *value, u32 cap)
{
	acpi_status status = AE_BAD_ADDRESS;

	switch (interface->type) {
	case ACER_AMW0:
		status = AMW0_get_u32(value, cap, interface);
		break;
	case ACER_AMW0_V2:
		if (cap == ACER_CAP_MAILLED) {
			status = AMW0_get_u32(value, cap, interface);
			break;
		}
	case ACER_WMID:
		status = WMID_get_u32(value, cap, interface);
		break;
	}

	return status;
}

static acpi_status set_u32(u32 value, u32 cap)
{
	if (interface->capability & cap) {
		switch (interface->type) {
		case ACER_AMW0:
			return AMW0_set_u32(value, cap, interface);
		case ACER_AMW0_V2:
		case ACER_WMID:
			return WMID_set_u32(value, cap, interface);
		default:
			return AE_BAD_PARAMETER;
		}
	}
	return AE_BAD_PARAMETER;
}

static void __init acer_commandline_init(void)
{
	/*
	 * These will all fail silently if the value given is invalid, or the
	 * capability isn't available on the given interface
	 */
	set_u32(mailled, ACER_CAP_MAILLED);
	set_u32(wireless, ACER_CAP_WIRELESS);
	set_u32(bluetooth, ACER_CAP_BLUETOOTH);
	set_u32(threeg, ACER_CAP_THREEG);
	set_u32(brightness, ACER_CAP_BRIGHTNESS);
}

/*
 * LED device (Mail LED only, no other LEDs known yet)
 */
static void mail_led_set(struct led_classdev *led_cdev,
enum led_brightness value)
{
	set_u32(value, ACER_CAP_MAILLED);
}

static struct led_classdev mail_led = {
	.name = "acer-mail:green",
	.brightness_set = mail_led_set,
};

static int __init acer_led_init(struct device *dev)
{
	return led_classdev_register(dev, &mail_led);
}

static void acer_led_exit(void)
{
	led_classdev_unregister(&mail_led);
}

/*
 * Backlight device
 */
static struct backlight_device *acer_backlight_device;

static int read_brightness(struct backlight_device *bd)
{
	u32 value;
	get_u32(&value, ACER_CAP_BRIGHTNESS);
	return value;
}

static int update_bl_status(struct backlight_device *bd)
{
	set_u32(bd->props.brightness, ACER_CAP_BRIGHTNESS);
	return 0;
}

static struct backlight_ops acer_bl_ops = {
	.get_brightness = read_brightness,
	.update_status = update_bl_status,
};

static int __init acer_backlight_init(struct device *dev)
{
	struct backlight_device *bd;

	bd = backlight_device_register("acer-wmi", dev, NULL, &acer_bl_ops);
	if (IS_ERR(bd)) {
		printk(ACER_ERR "Could not register Acer backlight device\n");
		acer_backlight_device = NULL;
		return PTR_ERR(bd);
	}

	acer_backlight_device = bd;

	bd->props.max_brightness = max_brightness;
	bd->props.brightness = read_brightness(NULL);
	backlight_update_status(bd);
	return 0;
}

static void __exit acer_backlight_exit(void)
{
	backlight_device_unregister(acer_backlight_device);
}

/*
 * Read/ write bool sysfs macro
 */
#define show_set_bool(value, cap) \
static ssize_t \
show_bool_##value(struct device *dev, struct device_attribute *attr, \
	char *buf) \
{ \
	u32 result; \
	acpi_status status = get_u32(&result, cap); \
	if (ACPI_SUCCESS(status)) \
		return sprintf(buf, "%u\n", result); \
	return sprintf(buf, "Read error\n"); \
} \
\
static ssize_t \
set_bool_##value(struct device *dev, struct device_attribute *attr, \
	const char *buf, size_t count) \
{ \
	u32 tmp = simple_strtoul(buf, NULL, 10); \
	acpi_status status = set_u32(tmp, cap); \
		if (ACPI_FAILURE(status)) \
			return -EINVAL; \
	return count; \
} \
static DEVICE_ATTR(value, S_IWUGO | S_IRUGO | S_IWUSR, \
	show_bool_##value, set_bool_##value);

show_set_bool(wireless, ACER_CAP_WIRELESS);
show_set_bool(bluetooth, ACER_CAP_BLUETOOTH);
show_set_bool(threeg, ACER_CAP_THREEG);

/*
 * Read interface sysfs macro
 */
static ssize_t show_interface(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	switch (interface->type) {
	case ACER_AMW0:
		return sprintf(buf, "AMW0\n");
	case ACER_AMW0_V2:
		return sprintf(buf, "AMW0 v2\n");
	case ACER_WMID:
		return sprintf(buf, "WMID\n");
	default:
		return sprintf(buf, "Error!\n");
	}
}

static DEVICE_ATTR(interface, S_IWUGO | S_IRUGO | S_IWUSR,
	show_interface, NULL);

/*
 * Platform device
 */
static int __devinit acer_platform_probe(struct platform_device *device)
{
	int err;

	if (has_cap(ACER_CAP_MAILLED)) {
		err = acer_led_init(&device->dev);
		if (err)
			goto error_mailled;
	}

	if (has_cap(ACER_CAP_BRIGHTNESS)) {
		err = acer_backlight_init(&device->dev);
		if (err)
			goto error_brightness;
	}

	return 0;

error_brightness:
	acer_led_exit();
error_mailled:
	return err;
}

static int acer_platform_remove(struct platform_device *device)
{
	if (has_cap(ACER_CAP_MAILLED))
		acer_led_exit();
	if (has_cap(ACER_CAP_BRIGHTNESS))
		acer_backlight_exit();
	return 0;
}

static int acer_platform_suspend(struct platform_device *dev,
pm_message_t state)
{
	u32 value;
	struct acer_data *data = &interface->data;

	if (!data)
		return -ENOMEM;

	if (has_cap(ACER_CAP_WIRELESS)) {
		get_u32(&value, ACER_CAP_WIRELESS);
		data->wireless = value;
	}

	if (has_cap(ACER_CAP_BLUETOOTH)) {
		get_u32(&value, ACER_CAP_BLUETOOTH);
		data->bluetooth = value;
	}

	if (has_cap(ACER_CAP_MAILLED)) {
		get_u32(&value, ACER_CAP_MAILLED);
		data->mailled = value;
	}

	if (has_cap(ACER_CAP_BRIGHTNESS)) {
		get_u32(&value, ACER_CAP_BRIGHTNESS);
		data->brightness = value;
	}

	return 0;
}

static int acer_platform_resume(struct platform_device *device)
{
	struct acer_data *data = &interface->data;

	if (!data)
		return -ENOMEM;

	if (has_cap(ACER_CAP_WIRELESS))
		set_u32(data->wireless, ACER_CAP_WIRELESS);

	if (has_cap(ACER_CAP_BLUETOOTH))
		set_u32(data->bluetooth, ACER_CAP_BLUETOOTH);

	if (has_cap(ACER_CAP_THREEG))
		set_u32(data->threeg, ACER_CAP_THREEG);

	if (has_cap(ACER_CAP_MAILLED))
		set_u32(data->mailled, ACER_CAP_MAILLED);

	if (has_cap(ACER_CAP_BRIGHTNESS))
		set_u32(data->brightness, ACER_CAP_BRIGHTNESS);

	return 0;
}

static struct platform_driver acer_platform_driver = {
	.driver = {
		.name = "acer-wmi",
		.owner = THIS_MODULE,
	},
	.probe = acer_platform_probe,
	.remove = acer_platform_remove,
	.suspend = acer_platform_suspend,
	.resume = acer_platform_resume,
};

static struct platform_device *acer_platform_device;

static int remove_sysfs(struct platform_device *device)
{
	if (has_cap(ACER_CAP_WIRELESS))
		device_remove_file(&device->dev, &dev_attr_wireless);

	if (has_cap(ACER_CAP_BLUETOOTH))
		device_remove_file(&device->dev, &dev_attr_bluetooth);

	if (has_cap(ACER_CAP_THREEG))
		device_remove_file(&device->dev, &dev_attr_threeg);

	device_remove_file(&device->dev, &dev_attr_interface);

	return 0;
}

static int create_sysfs(void)
{
	int retval = -ENOMEM;

	if (has_cap(ACER_CAP_WIRELESS)) {
		retval = device_create_file(&acer_platform_device->dev,
			&dev_attr_wireless);
		if (retval)
			goto error_sysfs;
	}

	if (has_cap(ACER_CAP_BLUETOOTH)) {
		retval = device_create_file(&acer_platform_device->dev,
			&dev_attr_bluetooth);
		if (retval)
			goto error_sysfs;
	}

	if (has_cap(ACER_CAP_THREEG)) {
		retval = device_create_file(&acer_platform_device->dev,
			&dev_attr_threeg);
		if (retval)
			goto error_sysfs;
	}

	retval = device_create_file(&acer_platform_device->dev,
		&dev_attr_interface);
	if (retval)
		goto error_sysfs;

	return 0;

error_sysfs:
		remove_sysfs(acer_platform_device);
	return retval;
}

static int __init acer_wmi_init(void)
{
	int err;

	printk(ACER_INFO "Acer Laptop ACPI-WMI Extras version %s\n",
			ACER_WMI_VERSION);

	/*
	 * Detect which ACPI-WMI interface we're using.
	 */
	if (wmi_has_guid(AMW0_GUID1) && wmi_has_guid(WMID_GUID1))
		interface = &AMW0_V2_interface;

	if (!wmi_has_guid(AMW0_GUID1) && wmi_has_guid(WMID_GUID1))
		interface = &wmid_interface;

	if (wmi_has_guid(WMID_GUID2) && interface) {
		if (ACPI_FAILURE(WMID_set_capabilities())) {
			printk(ACER_ERR "Unable to detect available devices\n");
			return -ENODEV;
		}
	} else if (!wmi_has_guid(WMID_GUID2) && interface) {
		printk(ACER_ERR "Unable to detect available devices\n");
		return -ENODEV;
	}

	if (wmi_has_guid(AMW0_GUID1) && !wmi_has_guid(WMID_GUID1)) {
		interface = &AMW0_interface;

		if (ACPI_FAILURE(AMW0_set_capabilities())) {
			printk(ACER_ERR "Unable to detect available devices\n");
			return -ENODEV;
		}
	}

	if (wmi_has_guid(AMW0_GUID1)) {
		if (ACPI_FAILURE(AMW0_find_mailled()))
			printk(ACER_ERR "Unable to detect mail LED\n");
	}

	find_quirks();

	if (!interface) {
		printk(ACER_ERR "No or unsupported WMI interface, unable to ");
		printk(KERN_CONT "load.\n");
		return -ENODEV;
	}

	if (platform_driver_register(&acer_platform_driver)) {
		printk(ACER_ERR "Unable to register platform driver.\n");
		goto error_platform_register;
	}
	acer_platform_device = platform_device_alloc("acer-wmi", -1);
	platform_device_add(acer_platform_device);

	err = create_sysfs();
	if (err)
		return err;

	/* Override any initial settings with values from the commandline */
	acer_commandline_init();

	return 0;

error_platform_register:
	return -ENODEV;
}

static void __exit acer_wmi_exit(void)
{
	remove_sysfs(acer_platform_device);
	platform_device_del(acer_platform_device);
	platform_driver_unregister(&acer_platform_driver);

	printk(ACER_INFO "Acer Laptop WMI Extras unloaded\n");
	return;
}

module_init(acer_wmi_init);
module_exit(acer_wmi_exit);
