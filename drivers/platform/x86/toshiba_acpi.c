/*
 *  toshiba_acpi.c - Toshiba Laptop ACPI Extras
 *
 *  Copyright (C) 2002-2004 John Belmonte
 *  Copyright (C) 2008 Philip Langdale
 *  Copyright (C) 2010 Pierre Ducroquet
 *  Copyright (C) 2014-2015 Azael Avalos
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
 *  The full GNU General Public License is included in this distribution in
 *  the file called "COPYING".
 *
 *  The devolpment page for this driver is located at
 *  http://memebeam.org/toys/ToshibaAcpiDriver.
 *
 *  Credits:
 *	Jonathan A. Buzzard - Toshiba HCI info, and critical tips on reverse
 *		engineering the Windows drivers
 *	Yasushi Nagato - changes for linux kernel 2.4 -> 2.5
 *	Rob Miller - TV out and hotkeys help
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define TOSHIBA_ACPI_VERSION	"0.21"
#define PROC_INTERFACE_VERSION	1

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/backlight.h>
#include <linux/rfkill.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/i8042.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/uaccess.h>

MODULE_AUTHOR("John Belmonte");
MODULE_DESCRIPTION("Toshiba Laptop ACPI Extras Driver");
MODULE_LICENSE("GPL");

#define TOSHIBA_WMI_EVENT_GUID "59142400-C6A3-40FA-BADB-8A2652834100"

/* Scan code for Fn key on TOS1900 models */
#define TOS1900_FN_SCAN		0x6e

/* Toshiba ACPI method paths */
#define METHOD_VIDEO_OUT	"\\_SB_.VALX.DSSX"

/*
 * The Toshiba configuration interface is composed of the HCI and the SCI,
 * which are defined as follows:
 *
 * HCI is Toshiba's "Hardware Control Interface" which is supposed to
 * be uniform across all their models.  Ideally we would just call
 * dedicated ACPI methods instead of using this primitive interface.
 * However the ACPI methods seem to be incomplete in some areas (for
 * example they allow setting, but not reading, the LCD brightness value),
 * so this is still useful.
 *
 * SCI stands for "System Configuration Interface" which aim is to
 * conceal differences in hardware between different models.
 */

#define TCI_WORDS			6

/* operations */
#define HCI_SET				0xff00
#define HCI_GET				0xfe00
#define SCI_OPEN			0xf100
#define SCI_CLOSE			0xf200
#define SCI_GET				0xf300
#define SCI_SET				0xf400

/* return codes */
#define TOS_SUCCESS			0x0000
#define TOS_OPEN_CLOSE_OK		0x0044
#define TOS_FAILURE			0x1000
#define TOS_NOT_SUPPORTED		0x8000
#define TOS_ALREADY_OPEN		0x8100
#define TOS_NOT_OPENED			0x8200
#define TOS_INPUT_DATA_ERROR		0x8300
#define TOS_WRITE_PROTECTED		0x8400
#define TOS_NOT_PRESENT			0x8600
#define TOS_FIFO_EMPTY			0x8c00
#define TOS_DATA_NOT_AVAILABLE		0x8d20
#define TOS_NOT_INITIALIZED		0x8d50
#define TOS_NOT_INSTALLED		0x8e00

/* registers */
#define HCI_FAN				0x0004
#define HCI_TR_BACKLIGHT		0x0005
#define HCI_SYSTEM_EVENT		0x0016
#define HCI_VIDEO_OUT			0x001c
#define HCI_HOTKEY_EVENT		0x001e
#define HCI_LCD_BRIGHTNESS		0x002a
#define HCI_WIRELESS			0x0056
#define HCI_ACCELEROMETER		0x006d
#define HCI_KBD_ILLUMINATION		0x0095
#define HCI_ECO_MODE			0x0097
#define HCI_ACCELEROMETER2		0x00a6
#define SCI_PANEL_POWER_ON		0x010d
#define SCI_ILLUMINATION		0x014e
#define SCI_USB_SLEEP_CHARGE		0x0150
#define SCI_KBD_ILLUM_STATUS		0x015c
#define SCI_USB_SLEEP_MUSIC		0x015e
#define SCI_USB_THREE			0x0169
#define SCI_TOUCHPAD			0x050e
#define SCI_KBD_FUNCTION_KEYS		0x0522

/* field definitions */
#define HCI_ACCEL_MASK			0x7fff
#define HCI_HOTKEY_DISABLE		0x0b
#define HCI_HOTKEY_ENABLE		0x09
#define HCI_LCD_BRIGHTNESS_BITS		3
#define HCI_LCD_BRIGHTNESS_SHIFT	(16-HCI_LCD_BRIGHTNESS_BITS)
#define HCI_LCD_BRIGHTNESS_LEVELS	(1 << HCI_LCD_BRIGHTNESS_BITS)
#define HCI_MISC_SHIFT			0x10
#define HCI_VIDEO_OUT_LCD		0x1
#define HCI_VIDEO_OUT_CRT		0x2
#define HCI_VIDEO_OUT_TV		0x4
#define HCI_WIRELESS_KILL_SWITCH	0x01
#define HCI_WIRELESS_BT_PRESENT		0x0f
#define HCI_WIRELESS_BT_ATTACH		0x40
#define HCI_WIRELESS_BT_POWER		0x80
#define SCI_KBD_MODE_MASK		0x1f
#define SCI_KBD_MODE_FNZ		0x1
#define SCI_KBD_MODE_AUTO		0x2
#define SCI_KBD_MODE_ON			0x8
#define SCI_KBD_MODE_OFF		0x10
#define SCI_KBD_TIME_MAX		0x3c001a
#define SCI_USB_CHARGE_MODE_MASK	0xff
#define SCI_USB_CHARGE_DISABLED		0x30000
#define SCI_USB_CHARGE_ALTERNATE	0x30009
#define SCI_USB_CHARGE_AUTO		0x30021
#define SCI_USB_CHARGE_BAT_MASK		0x7
#define SCI_USB_CHARGE_BAT_LVL_OFF	0x1
#define SCI_USB_CHARGE_BAT_LVL_ON	0x4
#define SCI_USB_CHARGE_BAT_LVL		0x0200
#define SCI_USB_CHARGE_RAPID_DSP	0x0300

struct toshiba_acpi_dev {
	struct acpi_device *acpi_dev;
	const char *method_hci;
	struct rfkill *bt_rfk;
	struct input_dev *hotkey_dev;
	struct work_struct hotkey_work;
	struct backlight_device *backlight_dev;
	struct led_classdev led_dev;
	struct led_classdev kbd_led;
	struct led_classdev eco_led;

	int force_fan;
	int last_key_event;
	int key_event_valid;
	int kbd_type;
	int kbd_mode;
	int kbd_time;
	int usbsc_bat_level;

	unsigned int illumination_supported:1;
	unsigned int video_supported:1;
	unsigned int fan_supported:1;
	unsigned int system_event_supported:1;
	unsigned int ntfy_supported:1;
	unsigned int info_supported:1;
	unsigned int tr_backlight_supported:1;
	unsigned int kbd_illum_supported:1;
	unsigned int kbd_led_registered:1;
	unsigned int touchpad_supported:1;
	unsigned int eco_supported:1;
	unsigned int accelerometer_supported:1;
	unsigned int usb_sleep_charge_supported:1;
	unsigned int usb_rapid_charge_supported:1;
	unsigned int usb_sleep_music_supported:1;
	unsigned int kbd_function_keys_supported:1;
	unsigned int panel_power_on_supported:1;
	unsigned int usb_three_supported:1;
	unsigned int sysfs_created:1;

	struct mutex mutex;
};

static struct toshiba_acpi_dev *toshiba_acpi;

static const struct acpi_device_id toshiba_device_ids[] = {
	{"TOS6200", 0},
	{"TOS6207", 0},
	{"TOS6208", 0},
	{"TOS1900", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, toshiba_device_ids);

static const struct key_entry toshiba_acpi_keymap[] = {
	{ KE_KEY, 0x9e, { KEY_RFKILL } },
	{ KE_KEY, 0x101, { KEY_MUTE } },
	{ KE_KEY, 0x102, { KEY_ZOOMOUT } },
	{ KE_KEY, 0x103, { KEY_ZOOMIN } },
	{ KE_KEY, 0x10f, { KEY_TAB } },
	{ KE_KEY, 0x12c, { KEY_KBDILLUMTOGGLE } },
	{ KE_KEY, 0x139, { KEY_ZOOMRESET } },
	{ KE_KEY, 0x13b, { KEY_COFFEE } },
	{ KE_KEY, 0x13c, { KEY_BATTERY } },
	{ KE_KEY, 0x13d, { KEY_SLEEP } },
	{ KE_KEY, 0x13e, { KEY_SUSPEND } },
	{ KE_KEY, 0x13f, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0x140, { KEY_BRIGHTNESSDOWN } },
	{ KE_KEY, 0x141, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 0x142, { KEY_WLAN } },
	{ KE_KEY, 0x143, { KEY_TOUCHPAD_TOGGLE } },
	{ KE_KEY, 0x17f, { KEY_FN } },
	{ KE_KEY, 0xb05, { KEY_PROG2 } },
	{ KE_KEY, 0xb06, { KEY_WWW } },
	{ KE_KEY, 0xb07, { KEY_MAIL } },
	{ KE_KEY, 0xb30, { KEY_STOP } },
	{ KE_KEY, 0xb31, { KEY_PREVIOUSSONG } },
	{ KE_KEY, 0xb32, { KEY_NEXTSONG } },
	{ KE_KEY, 0xb33, { KEY_PLAYPAUSE } },
	{ KE_KEY, 0xb5a, { KEY_MEDIA } },
	{ KE_IGNORE, 0x1430, { KEY_RESERVED } }, /* Wake from sleep */
	{ KE_IGNORE, 0x1501, { KEY_RESERVED } }, /* Output changed */
	{ KE_IGNORE, 0x1502, { KEY_RESERVED } }, /* HDMI plugged/unplugged */
	{ KE_IGNORE, 0x1ABE, { KEY_RESERVED } }, /* Protection level set */
	{ KE_IGNORE, 0x1ABF, { KEY_RESERVED } }, /* Protection level off */
	{ KE_END, 0 },
};

/* alternative keymap */
static const struct dmi_system_id toshiba_alt_keymap_dmi[] = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite M840"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Qosmio X75-A"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TECRA A50-A"),
		},
	},
	{}
};

static const struct key_entry toshiba_acpi_alt_keymap[] = {
	{ KE_KEY, 0x157, { KEY_MUTE } },
	{ KE_KEY, 0x102, { KEY_ZOOMOUT } },
	{ KE_KEY, 0x103, { KEY_ZOOMIN } },
	{ KE_KEY, 0x12c, { KEY_KBDILLUMTOGGLE } },
	{ KE_KEY, 0x139, { KEY_ZOOMRESET } },
	{ KE_KEY, 0x13e, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0x13c, { KEY_BRIGHTNESSDOWN } },
	{ KE_KEY, 0x13d, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 0x158, { KEY_WLAN } },
	{ KE_KEY, 0x13f, { KEY_TOUCHPAD_TOGGLE } },
	{ KE_END, 0 },
};

/*
 * Utility
 */

static inline void _set_bit(u32 *word, u32 mask, int value)
{
	*word = (*word & ~mask) | (mask * value);
}

/*
 * ACPI interface wrappers
 */

static int write_acpi_int(const char *methodName, int val)
{
	acpi_status status;

	status = acpi_execute_simple_method(NULL, (char *)methodName, val);
	return (status == AE_OK) ? 0 : -EIO;
}

/*
 * Perform a raw configuration call.  Here we don't care about input or output
 * buffer format.
 */
static acpi_status tci_raw(struct toshiba_acpi_dev *dev,
			   const u32 in[TCI_WORDS], u32 out[TCI_WORDS])
{
	struct acpi_object_list params;
	union acpi_object in_objs[TCI_WORDS];
	struct acpi_buffer results;
	union acpi_object out_objs[TCI_WORDS + 1];
	acpi_status status;
	int i;

	params.count = TCI_WORDS;
	params.pointer = in_objs;
	for (i = 0; i < TCI_WORDS; ++i) {
		in_objs[i].type = ACPI_TYPE_INTEGER;
		in_objs[i].integer.value = in[i];
	}

	results.length = sizeof(out_objs);
	results.pointer = out_objs;

	status = acpi_evaluate_object(dev->acpi_dev->handle,
				      (char *)dev->method_hci, &params,
				      &results);
	if ((status == AE_OK) && (out_objs->package.count <= TCI_WORDS)) {
		for (i = 0; i < out_objs->package.count; ++i)
			out[i] = out_objs->package.elements[i].integer.value;
	}

	return status;
}

/*
 * Common hci tasks (get or set one or two value)
 *
 * In addition to the ACPI status, the HCI system returns a result which
 * may be useful (such as "not supported").
 */

static u32 hci_write1(struct toshiba_acpi_dev *dev, u32 reg, u32 in1)
{
	u32 in[TCI_WORDS] = { HCI_SET, reg, in1, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status = tci_raw(dev, in, out);

	return ACPI_SUCCESS(status) ? out[0] : TOS_FAILURE;
}

static u32 hci_read1(struct toshiba_acpi_dev *dev, u32 reg, u32 *out1)
{
	u32 in[TCI_WORDS] = { HCI_GET, reg, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status = tci_raw(dev, in, out);

	if (ACPI_FAILURE(status))
		return TOS_FAILURE;

	*out1 = out[2];

	return out[0];
}

static u32 hci_write2(struct toshiba_acpi_dev *dev, u32 reg, u32 in1, u32 in2)
{
	u32 in[TCI_WORDS] = { HCI_SET, reg, in1, in2, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status = tci_raw(dev, in, out);

	return ACPI_SUCCESS(status) ? out[0] : TOS_FAILURE;
}

static u32 hci_read2(struct toshiba_acpi_dev *dev,
		     u32 reg, u32 *out1, u32 *out2)
{
	u32 in[TCI_WORDS] = { HCI_GET, reg, *out1, *out2, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status = tci_raw(dev, in, out);

	if (ACPI_FAILURE(status))
		return TOS_FAILURE;

	*out1 = out[2];
	*out2 = out[3];

	return out[0];
}

/*
 * Common sci tasks
 */

static int sci_open(struct toshiba_acpi_dev *dev)
{
	u32 in[TCI_WORDS] = { SCI_OPEN, 0, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	status = tci_raw(dev, in, out);
	if  (ACPI_FAILURE(status) || out[0] == TOS_FAILURE) {
		pr_err("ACPI call to open SCI failed\n");
		return 0;
	}

	if (out[0] == TOS_OPEN_CLOSE_OK) {
		return 1;
	} else if (out[0] == TOS_ALREADY_OPEN) {
		pr_info("Toshiba SCI already opened\n");
		return 1;
	} else if (out[0] == TOS_NOT_SUPPORTED) {
		/*
		 * Some BIOSes do not have the SCI open/close functions
		 * implemented and return 0x8000 (Not Supported), failing to
		 * register some supported features.
		 *
		 * Simply return 1 if we hit those affected laptops to make the
		 * supported features work.
		 *
		 * In the case that some laptops really do not support the SCI,
		 * all the SCI dependent functions check for TOS_NOT_SUPPORTED,
		 * and thus, not registering support for the queried feature.
		 */
		return 1;
	} else if (out[0] == TOS_NOT_PRESENT) {
		pr_info("Toshiba SCI is not present\n");
	}

	return 0;
}

static void sci_close(struct toshiba_acpi_dev *dev)
{
	u32 in[TCI_WORDS] = { SCI_CLOSE, 0, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	status = tci_raw(dev, in, out);
	if (ACPI_FAILURE(status) || out[0] == TOS_FAILURE) {
		pr_err("ACPI call to close SCI failed\n");
		return;
	}

	if (out[0] == TOS_OPEN_CLOSE_OK)
		return;
	else if (out[0] == TOS_NOT_OPENED)
		pr_info("Toshiba SCI not opened\n");
	else if (out[0] == TOS_NOT_PRESENT)
		pr_info("Toshiba SCI is not present\n");
}

static u32 sci_read(struct toshiba_acpi_dev *dev, u32 reg, u32 *out1)
{
	u32 in[TCI_WORDS] = { SCI_GET, reg, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status = tci_raw(dev, in, out);

	if (ACPI_FAILURE(status))
		return TOS_FAILURE;

	*out1 = out[2];

	return out[0];
}

static u32 sci_write(struct toshiba_acpi_dev *dev, u32 reg, u32 in1)
{
	u32 in[TCI_WORDS] = { SCI_SET, reg, in1, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status = tci_raw(dev, in, out);

	return ACPI_SUCCESS(status) ? out[0] : TOS_FAILURE;
}

/* Illumination support */
static int toshiba_illumination_available(struct toshiba_acpi_dev *dev)
{
	u32 in[TCI_WORDS] = { SCI_GET, SCI_ILLUMINATION, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	if (!sci_open(dev))
		return 0;

	status = tci_raw(dev, in, out);
	sci_close(dev);
	if (ACPI_FAILURE(status) || out[0] == TOS_FAILURE) {
		pr_err("ACPI call to query Illumination support failed\n");
		return 0;
	} else if (out[0] == TOS_NOT_SUPPORTED) {
		pr_info("Illumination device not available\n");
		return 0;
	}

	return 1;
}

static void toshiba_illumination_set(struct led_classdev *cdev,
				     enum led_brightness brightness)
{
	struct toshiba_acpi_dev *dev = container_of(cdev,
			struct toshiba_acpi_dev, led_dev);
	u32 state, result;

	/* First request : initialize communication. */
	if (!sci_open(dev))
		return;

	/* Switch the illumination on/off */
	state = brightness ? 1 : 0;
	result = sci_write(dev, SCI_ILLUMINATION, state);
	sci_close(dev);
	if (result == TOS_FAILURE) {
		pr_err("ACPI call for illumination failed\n");
		return;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("Illumination not supported\n");
		return;
	}
}

static enum led_brightness toshiba_illumination_get(struct led_classdev *cdev)
{
	struct toshiba_acpi_dev *dev = container_of(cdev,
			struct toshiba_acpi_dev, led_dev);
	u32 state, result;

	/* First request : initialize communication. */
	if (!sci_open(dev))
		return LED_OFF;

	/* Check the illumination */
	result = sci_read(dev, SCI_ILLUMINATION, &state);
	sci_close(dev);
	if (result == TOS_FAILURE || result == TOS_INPUT_DATA_ERROR) {
		pr_err("ACPI call for illumination failed\n");
		return LED_OFF;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("Illumination not supported\n");
		return LED_OFF;
	}

	return state ? LED_FULL : LED_OFF;
}

/* KBD Illumination */
static int toshiba_kbd_illum_available(struct toshiba_acpi_dev *dev)
{
	u32 in[TCI_WORDS] = { SCI_GET, SCI_KBD_ILLUM_STATUS, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	if (!sci_open(dev))
		return 0;

	status = tci_raw(dev, in, out);
	sci_close(dev);
	if (ACPI_FAILURE(status) || out[0] == TOS_INPUT_DATA_ERROR) {
		pr_err("ACPI call to query kbd illumination support failed\n");
		return 0;
	} else if (out[0] == TOS_NOT_SUPPORTED) {
		pr_info("Keyboard illumination not available\n");
		return 0;
	}

	/*
	 * Check for keyboard backlight timeout max value,
	 * previous kbd backlight implementation set this to
	 * 0x3c0003, and now the new implementation set this
	 * to 0x3c001a, use this to distinguish between them.
	 */
	if (out[3] == SCI_KBD_TIME_MAX)
		dev->kbd_type = 2;
	else
		dev->kbd_type = 1;
	/* Get the current keyboard backlight mode */
	dev->kbd_mode = out[2] & SCI_KBD_MODE_MASK;
	/* Get the current time (1-60 seconds) */
	dev->kbd_time = out[2] >> HCI_MISC_SHIFT;

	return 1;
}

static int toshiba_kbd_illum_status_set(struct toshiba_acpi_dev *dev, u32 time)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_write(dev, SCI_KBD_ILLUM_STATUS, time);
	sci_close(dev);
	if (result == TOS_FAILURE || result == TOS_INPUT_DATA_ERROR) {
		pr_err("ACPI call to set KBD backlight status failed\n");
		return -EIO;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("Keyboard backlight status not supported\n");
		return -ENODEV;
	}

	return 0;
}

static int toshiba_kbd_illum_status_get(struct toshiba_acpi_dev *dev, u32 *time)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_read(dev, SCI_KBD_ILLUM_STATUS, time);
	sci_close(dev);
	if (result == TOS_FAILURE || result == TOS_INPUT_DATA_ERROR) {
		pr_err("ACPI call to get KBD backlight status failed\n");
		return -EIO;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("Keyboard backlight status not supported\n");
		return -ENODEV;
	}

	return 0;
}

static enum led_brightness toshiba_kbd_backlight_get(struct led_classdev *cdev)
{
	struct toshiba_acpi_dev *dev = container_of(cdev,
			struct toshiba_acpi_dev, kbd_led);
	u32 state, result;

	/* Check the keyboard backlight state */
	result = hci_read1(dev, HCI_KBD_ILLUMINATION, &state);
	if (result == TOS_FAILURE || result == TOS_INPUT_DATA_ERROR) {
		pr_err("ACPI call to get the keyboard backlight failed\n");
		return LED_OFF;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("Keyboard backlight not supported\n");
		return LED_OFF;
	}

	return state ? LED_FULL : LED_OFF;
}

static void toshiba_kbd_backlight_set(struct led_classdev *cdev,
				     enum led_brightness brightness)
{
	struct toshiba_acpi_dev *dev = container_of(cdev,
			struct toshiba_acpi_dev, kbd_led);
	u32 state, result;

	/* Set the keyboard backlight state */
	state = brightness ? 1 : 0;
	result = hci_write1(dev, HCI_KBD_ILLUMINATION, state);
	if (result == TOS_FAILURE || result == TOS_INPUT_DATA_ERROR) {
		pr_err("ACPI call to set KBD Illumination mode failed\n");
		return;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("Keyboard backlight not supported\n");
		return;
	}
}

/* TouchPad support */
static int toshiba_touchpad_set(struct toshiba_acpi_dev *dev, u32 state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_write(dev, SCI_TOUCHPAD, state);
	sci_close(dev);
	if (result == TOS_FAILURE) {
		pr_err("ACPI call to set the touchpad failed\n");
		return -EIO;
	} else if (result == TOS_NOT_SUPPORTED) {
		return -ENODEV;
	}

	return 0;
}

static int toshiba_touchpad_get(struct toshiba_acpi_dev *dev, u32 *state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_read(dev, SCI_TOUCHPAD, state);
	sci_close(dev);
	if (result == TOS_FAILURE) {
		pr_err("ACPI call to query the touchpad failed\n");
		return -EIO;
	} else if (result == TOS_NOT_SUPPORTED) {
		return -ENODEV;
	}

	return 0;
}

/* Eco Mode support */
static int toshiba_eco_mode_available(struct toshiba_acpi_dev *dev)
{
	acpi_status status;
	u32 in[TCI_WORDS] = { HCI_GET, HCI_ECO_MODE, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];

	status = tci_raw(dev, in, out);
	if (ACPI_FAILURE(status) || out[0] == TOS_FAILURE) {
		pr_err("ACPI call to get ECO led failed\n");
	} else if (out[0] == TOS_NOT_INSTALLED) {
		pr_info("ECO led not installed");
	} else if (out[0] == TOS_INPUT_DATA_ERROR) {
		/*
		 * If we receive 0x8300 (Input Data Error), it means that the
		 * LED device is present, but that we just screwed the input
		 * parameters.
		 *
		 * Let's query the status of the LED to see if we really have a
		 * success response, indicating the actual presense of the LED,
		 * bail out otherwise.
		 */
		in[3] = 1;
		status = tci_raw(dev, in, out);
		if (ACPI_FAILURE(status) || out[0] == TOS_FAILURE)
			pr_err("ACPI call to get ECO led failed\n");
		else if (out[0] == TOS_SUCCESS)
			return 1;
	}

	return 0;
}

static enum led_brightness
toshiba_eco_mode_get_status(struct led_classdev *cdev)
{
	struct toshiba_acpi_dev *dev = container_of(cdev,
			struct toshiba_acpi_dev, eco_led);
	u32 in[TCI_WORDS] = { HCI_GET, HCI_ECO_MODE, 0, 1, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	status = tci_raw(dev, in, out);
	if (ACPI_FAILURE(status) || out[0] == TOS_INPUT_DATA_ERROR) {
		pr_err("ACPI call to get ECO led failed\n");
		return LED_OFF;
	}

	return out[2] ? LED_FULL : LED_OFF;
}

static void toshiba_eco_mode_set_status(struct led_classdev *cdev,
				     enum led_brightness brightness)
{
	struct toshiba_acpi_dev *dev = container_of(cdev,
			struct toshiba_acpi_dev, eco_led);
	u32 in[TCI_WORDS] = { HCI_SET, HCI_ECO_MODE, 0, 1, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	/* Switch the Eco Mode led on/off */
	in[2] = (brightness) ? 1 : 0;
	status = tci_raw(dev, in, out);
	if (ACPI_FAILURE(status) || out[0] == TOS_INPUT_DATA_ERROR) {
		pr_err("ACPI call to set ECO led failed\n");
		return;
	}
}

/* Accelerometer support */
static int toshiba_accelerometer_supported(struct toshiba_acpi_dev *dev)
{
	u32 in[TCI_WORDS] = { HCI_GET, HCI_ACCELEROMETER2, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	/*
	 * Check if the accelerometer call exists,
	 * this call also serves as initialization
	 */
	status = tci_raw(dev, in, out);
	if (ACPI_FAILURE(status) || out[0] == TOS_INPUT_DATA_ERROR) {
		pr_err("ACPI call to query the accelerometer failed\n");
		return -EIO;
	} else if (out[0] == TOS_DATA_NOT_AVAILABLE ||
		   out[0] == TOS_NOT_INITIALIZED) {
		pr_err("Accelerometer not initialized\n");
		return -EIO;
	} else if (out[0] == TOS_NOT_SUPPORTED) {
		pr_info("Accelerometer not supported\n");
		return -ENODEV;
	}

	return 0;
}

static int toshiba_accelerometer_get(struct toshiba_acpi_dev *dev,
				      u32 *xy, u32 *z)
{
	u32 in[TCI_WORDS] = { HCI_GET, HCI_ACCELEROMETER, 0, 1, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	/* Check the Accelerometer status */
	status = tci_raw(dev, in, out);
	if (ACPI_FAILURE(status) || out[0] == TOS_INPUT_DATA_ERROR) {
		pr_err("ACPI call to query the accelerometer failed\n");
		return -EIO;
	}

	*xy = out[2];
	*z = out[4];

	return 0;
}

/* Sleep (Charge and Music) utilities support */
static int toshiba_usb_sleep_charge_get(struct toshiba_acpi_dev *dev,
					u32 *mode)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_read(dev, SCI_USB_SLEEP_CHARGE, mode);
	sci_close(dev);
	if (result == TOS_FAILURE) {
		pr_err("ACPI call to set USB S&C mode failed\n");
		return -EIO;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("USB Sleep and Charge not supported\n");
		return -ENODEV;
	} else if (result == TOS_INPUT_DATA_ERROR) {
		return -EIO;
	}

	return 0;
}

static int toshiba_usb_sleep_charge_set(struct toshiba_acpi_dev *dev,
					u32 mode)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_write(dev, SCI_USB_SLEEP_CHARGE, mode);
	sci_close(dev);
	if (result == TOS_FAILURE) {
		pr_err("ACPI call to set USB S&C mode failed\n");
		return -EIO;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("USB Sleep and Charge not supported\n");
		return -ENODEV;
	} else if (result == TOS_INPUT_DATA_ERROR) {
		return -EIO;
	}

	return 0;
}

static int toshiba_sleep_functions_status_get(struct toshiba_acpi_dev *dev,
					      u32 *mode)
{
	u32 in[TCI_WORDS] = { SCI_GET, SCI_USB_SLEEP_CHARGE, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	if (!sci_open(dev))
		return -EIO;

	in[5] = SCI_USB_CHARGE_BAT_LVL;
	status = tci_raw(dev, in, out);
	sci_close(dev);
	if (ACPI_FAILURE(status) || out[0] == TOS_FAILURE) {
		pr_err("ACPI call to get USB S&C battery level failed\n");
		return -EIO;
	} else if (out[0] == TOS_NOT_SUPPORTED) {
		pr_info("USB Sleep and Charge not supported\n");
		return -ENODEV;
	} else if (out[0] == TOS_INPUT_DATA_ERROR) {
		return -EIO;
	}

	*mode = out[2];

	return 0;
}

static int toshiba_sleep_functions_status_set(struct toshiba_acpi_dev *dev,
					      u32 mode)
{
	u32 in[TCI_WORDS] = { SCI_SET, SCI_USB_SLEEP_CHARGE, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	if (!sci_open(dev))
		return -EIO;

	in[2] = mode;
	in[5] = SCI_USB_CHARGE_BAT_LVL;
	status = tci_raw(dev, in, out);
	sci_close(dev);
	if (ACPI_FAILURE(status) || out[0] == TOS_FAILURE) {
		pr_err("ACPI call to set USB S&C battery level failed\n");
		return -EIO;
	} else if (out[0] == TOS_NOT_SUPPORTED) {
		pr_info("USB Sleep and Charge not supported\n");
		return -ENODEV;
	} else if (out[0] == TOS_INPUT_DATA_ERROR) {
		return -EIO;
	}

	return 0;
}

static int toshiba_usb_rapid_charge_get(struct toshiba_acpi_dev *dev,
					u32 *state)
{
	u32 in[TCI_WORDS] = { SCI_GET, SCI_USB_SLEEP_CHARGE, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	if (!sci_open(dev))
		return -EIO;

	in[5] = SCI_USB_CHARGE_RAPID_DSP;
	status = tci_raw(dev, in, out);
	sci_close(dev);
	if (ACPI_FAILURE(status) || out[0] == TOS_FAILURE) {
		pr_err("ACPI call to get USB S&C battery level failed\n");
		return -EIO;
	} else if (out[0] == TOS_NOT_SUPPORTED ||
		   out[0] == TOS_INPUT_DATA_ERROR) {
		pr_info("USB Sleep and Charge not supported\n");
		return -ENODEV;
	}

	*state = out[2];

	return 0;
}

static int toshiba_usb_rapid_charge_set(struct toshiba_acpi_dev *dev,
					u32 state)
{
	u32 in[TCI_WORDS] = { SCI_SET, SCI_USB_SLEEP_CHARGE, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	if (!sci_open(dev))
		return -EIO;

	in[2] = state;
	in[5] = SCI_USB_CHARGE_RAPID_DSP;
	status = tci_raw(dev, in, out);
	sci_close(dev);
	if (ACPI_FAILURE(status) || out[0] == TOS_FAILURE) {
		pr_err("ACPI call to set USB S&C battery level failed\n");
		return -EIO;
	} else if (out[0] == TOS_NOT_SUPPORTED) {
		pr_info("USB Sleep and Charge not supported\n");
		return -ENODEV;
	} else if (out[0] == TOS_INPUT_DATA_ERROR) {
		return -EIO;
	}

	return 0;
}

static int toshiba_usb_sleep_music_get(struct toshiba_acpi_dev *dev, u32 *state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_read(dev, SCI_USB_SLEEP_MUSIC, state);
	sci_close(dev);
	if (result == TOS_FAILURE) {
		pr_err("ACPI call to set USB S&C mode failed\n");
		return -EIO;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("USB Sleep and Charge not supported\n");
		return -ENODEV;
	} else if (result == TOS_INPUT_DATA_ERROR) {
		return -EIO;
	}

	return 0;
}

static int toshiba_usb_sleep_music_set(struct toshiba_acpi_dev *dev, u32 state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_write(dev, SCI_USB_SLEEP_MUSIC, state);
	sci_close(dev);
	if (result == TOS_FAILURE) {
		pr_err("ACPI call to set USB S&C mode failed\n");
		return -EIO;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("USB Sleep and Charge not supported\n");
		return -ENODEV;
	} else if (result == TOS_INPUT_DATA_ERROR) {
		return -EIO;
	}

	return 0;
}

/* Keyboard function keys */
static int toshiba_function_keys_get(struct toshiba_acpi_dev *dev, u32 *mode)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_read(dev, SCI_KBD_FUNCTION_KEYS, mode);
	sci_close(dev);
	if (result == TOS_FAILURE || result == TOS_INPUT_DATA_ERROR) {
		pr_err("ACPI call to get KBD function keys failed\n");
		return -EIO;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("KBD function keys not supported\n");
		return -ENODEV;
	}

	return 0;
}

static int toshiba_function_keys_set(struct toshiba_acpi_dev *dev, u32 mode)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_write(dev, SCI_KBD_FUNCTION_KEYS, mode);
	sci_close(dev);
	if (result == TOS_FAILURE || result == TOS_INPUT_DATA_ERROR) {
		pr_err("ACPI call to set KBD function keys failed\n");
		return -EIO;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("KBD function keys not supported\n");
		return -ENODEV;
	}

	return 0;
}

/* Panel Power ON */
static int toshiba_panel_power_on_get(struct toshiba_acpi_dev *dev, u32 *state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_read(dev, SCI_PANEL_POWER_ON, state);
	sci_close(dev);
	if (result == TOS_FAILURE) {
		pr_err("ACPI call to get Panel Power ON failed\n");
		return -EIO;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("Panel Power on not supported\n");
		return -ENODEV;
	} else if (result == TOS_INPUT_DATA_ERROR) {
		return -EIO;
	}

	return 0;
}

static int toshiba_panel_power_on_set(struct toshiba_acpi_dev *dev, u32 state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_write(dev, SCI_PANEL_POWER_ON, state);
	sci_close(dev);
	if (result == TOS_FAILURE) {
		pr_err("ACPI call to set Panel Power ON failed\n");
		return -EIO;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("Panel Power ON not supported\n");
		return -ENODEV;
	} else if (result == TOS_INPUT_DATA_ERROR) {
		return -EIO;
	}

	return 0;
}

/* USB Three */
static int toshiba_usb_three_get(struct toshiba_acpi_dev *dev, u32 *state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_read(dev, SCI_USB_THREE, state);
	sci_close(dev);
	if (result == TOS_FAILURE) {
		pr_err("ACPI call to get USB 3 failed\n");
		return -EIO;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("USB 3 not supported\n");
		return -ENODEV;
	} else if (result == TOS_INPUT_DATA_ERROR) {
		return -EIO;
	}

	return 0;
}

static int toshiba_usb_three_set(struct toshiba_acpi_dev *dev, u32 state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_write(dev, SCI_USB_THREE, state);
	sci_close(dev);
	if (result == TOS_FAILURE) {
		pr_err("ACPI call to set USB 3 failed\n");
		return -EIO;
	} else if (result == TOS_NOT_SUPPORTED) {
		pr_info("USB 3 not supported\n");
		return -ENODEV;
	} else if (result == TOS_INPUT_DATA_ERROR) {
		return -EIO;
	}

	return 0;
}

/* Bluetooth rfkill handlers */

static u32 hci_get_bt_present(struct toshiba_acpi_dev *dev, bool *present)
{
	u32 hci_result;
	u32 value, value2;

	value = 0;
	value2 = 0;
	hci_result = hci_read2(dev, HCI_WIRELESS, &value, &value2);
	if (hci_result == TOS_SUCCESS)
		*present = (value & HCI_WIRELESS_BT_PRESENT) ? true : false;

	return hci_result;
}

static u32 hci_get_radio_state(struct toshiba_acpi_dev *dev, bool *radio_state)
{
	u32 hci_result;
	u32 value, value2;

	value = 0;
	value2 = 0x0001;
	hci_result = hci_read2(dev, HCI_WIRELESS, &value, &value2);

	*radio_state = value & HCI_WIRELESS_KILL_SWITCH;
	return hci_result;
}

static int bt_rfkill_set_block(void *data, bool blocked)
{
	struct toshiba_acpi_dev *dev = data;
	u32 result1, result2;
	u32 value;
	int err;
	bool radio_state;

	value = (blocked == false);

	mutex_lock(&dev->mutex);
	if (hci_get_radio_state(dev, &radio_state) != TOS_SUCCESS) {
		err = -EIO;
		goto out;
	}

	if (!radio_state) {
		err = 0;
		goto out;
	}

	result1 = hci_write2(dev, HCI_WIRELESS, value, HCI_WIRELESS_BT_POWER);
	result2 = hci_write2(dev, HCI_WIRELESS, value, HCI_WIRELESS_BT_ATTACH);

	if (result1 != TOS_SUCCESS || result2 != TOS_SUCCESS)
		err = -EIO;
	else
		err = 0;
 out:
	mutex_unlock(&dev->mutex);
	return err;
}

static void bt_rfkill_poll(struct rfkill *rfkill, void *data)
{
	bool new_rfk_state;
	bool value;
	u32 hci_result;
	struct toshiba_acpi_dev *dev = data;

	mutex_lock(&dev->mutex);

	hci_result = hci_get_radio_state(dev, &value);
	if (hci_result != TOS_SUCCESS) {
		/* Can't do anything useful */
		mutex_unlock(&dev->mutex);
		return;
	}

	new_rfk_state = value;

	mutex_unlock(&dev->mutex);

	if (rfkill_set_hw_state(rfkill, !new_rfk_state))
		bt_rfkill_set_block(data, true);
}

static const struct rfkill_ops toshiba_rfk_ops = {
	.set_block = bt_rfkill_set_block,
	.poll = bt_rfkill_poll,
};

static int get_tr_backlight_status(struct toshiba_acpi_dev *dev, bool *enabled)
{
	u32 hci_result;
	u32 status;

	hci_result = hci_read1(dev, HCI_TR_BACKLIGHT, &status);
	*enabled = !status;
	return hci_result == TOS_SUCCESS ? 0 : -EIO;
}

static int set_tr_backlight_status(struct toshiba_acpi_dev *dev, bool enable)
{
	u32 hci_result;
	u32 value = !enable;

	hci_result = hci_write1(dev, HCI_TR_BACKLIGHT, value);
	return hci_result == TOS_SUCCESS ? 0 : -EIO;
}

static struct proc_dir_entry *toshiba_proc_dir /*= 0*/;

static int __get_lcd_brightness(struct toshiba_acpi_dev *dev)
{
	u32 hci_result;
	u32 value;
	int brightness = 0;

	if (dev->tr_backlight_supported) {
		bool enabled;
		int ret = get_tr_backlight_status(dev, &enabled);

		if (ret)
			return ret;
		if (enabled)
			return 0;
		brightness++;
	}

	hci_result = hci_read1(dev, HCI_LCD_BRIGHTNESS, &value);
	if (hci_result == TOS_SUCCESS)
		return brightness + (value >> HCI_LCD_BRIGHTNESS_SHIFT);

	return -EIO;
}

static int get_lcd_brightness(struct backlight_device *bd)
{
	struct toshiba_acpi_dev *dev = bl_get_data(bd);

	return __get_lcd_brightness(dev);
}

static int lcd_proc_show(struct seq_file *m, void *v)
{
	struct toshiba_acpi_dev *dev = m->private;
	int value;
	int levels;

	if (!dev->backlight_dev)
		return -ENODEV;

	levels = dev->backlight_dev->props.max_brightness + 1;
	value = get_lcd_brightness(dev->backlight_dev);
	if (value >= 0) {
		seq_printf(m, "brightness:              %d\n", value);
		seq_printf(m, "brightness_levels:       %d\n", levels);
		return 0;
	}

	pr_err("Error reading LCD brightness\n");
	return -EIO;
}

static int lcd_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, lcd_proc_show, PDE_DATA(inode));
}

static int set_lcd_brightness(struct toshiba_acpi_dev *dev, int value)
{
	u32 hci_result;

	if (dev->tr_backlight_supported) {
		bool enable = !value;
		int ret = set_tr_backlight_status(dev, enable);

		if (ret)
			return ret;
		if (value)
			value--;
	}

	value = value << HCI_LCD_BRIGHTNESS_SHIFT;
	hci_result = hci_write1(dev, HCI_LCD_BRIGHTNESS, value);
	return hci_result == TOS_SUCCESS ? 0 : -EIO;
}

static int set_lcd_status(struct backlight_device *bd)
{
	struct toshiba_acpi_dev *dev = bl_get_data(bd);

	return set_lcd_brightness(dev, bd->props.brightness);
}

static ssize_t lcd_proc_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *pos)
{
	struct toshiba_acpi_dev *dev = PDE_DATA(file_inode(file));
	char cmd[42];
	size_t len;
	int value;
	int ret;
	int levels = dev->backlight_dev->props.max_brightness + 1;

	len = min(count, sizeof(cmd) - 1);
	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = '\0';

	if (sscanf(cmd, " brightness : %i", &value) == 1 &&
	    value >= 0 && value < levels) {
		ret = set_lcd_brightness(dev, value);
		if (ret == 0)
			ret = count;
	} else {
		ret = -EINVAL;
	}
	return ret;
}

static const struct file_operations lcd_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= lcd_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= lcd_proc_write,
};

static int get_video_status(struct toshiba_acpi_dev *dev, u32 *status)
{
	u32 hci_result;

	hci_result = hci_read1(dev, HCI_VIDEO_OUT, status);
	return hci_result == TOS_SUCCESS ? 0 : -EIO;
}

static int video_proc_show(struct seq_file *m, void *v)
{
	struct toshiba_acpi_dev *dev = m->private;
	u32 value;
	int ret;

	ret = get_video_status(dev, &value);
	if (!ret) {
		int is_lcd = (value & HCI_VIDEO_OUT_LCD) ? 1 : 0;
		int is_crt = (value & HCI_VIDEO_OUT_CRT) ? 1 : 0;
		int is_tv = (value & HCI_VIDEO_OUT_TV) ? 1 : 0;

		seq_printf(m, "lcd_out:                 %d\n", is_lcd);
		seq_printf(m, "crt_out:                 %d\n", is_crt);
		seq_printf(m, "tv_out:                  %d\n", is_tv);
	}

	return ret;
}

static int video_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, video_proc_show, PDE_DATA(inode));
}

static ssize_t video_proc_write(struct file *file, const char __user *buf,
				size_t count, loff_t *pos)
{
	struct toshiba_acpi_dev *dev = PDE_DATA(file_inode(file));
	char *cmd, *buffer;
	int ret;
	int value;
	int remain = count;
	int lcd_out = -1;
	int crt_out = -1;
	int tv_out = -1;
	u32 video_out;

	cmd = kmalloc(count + 1, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;
	if (copy_from_user(cmd, buf, count)) {
		kfree(cmd);
		return -EFAULT;
	}
	cmd[count] = '\0';

	buffer = cmd;

	/*
	 * Scan expression.  Multiple expressions may be delimited with ;
	 * NOTE: To keep scanning simple, invalid fields are ignored.
	 */
	while (remain) {
		if (sscanf(buffer, " lcd_out : %i", &value) == 1)
			lcd_out = value & 1;
		else if (sscanf(buffer, " crt_out : %i", &value) == 1)
			crt_out = value & 1;
		else if (sscanf(buffer, " tv_out : %i", &value) == 1)
			tv_out = value & 1;
		/* Advance to one character past the next ; */
		do {
			++buffer;
			--remain;
		} while (remain && *(buffer - 1) != ';');
	}

	kfree(cmd);

	ret = get_video_status(dev, &video_out);
	if (!ret) {
		unsigned int new_video_out = video_out;

		if (lcd_out != -1)
			_set_bit(&new_video_out, HCI_VIDEO_OUT_LCD, lcd_out);
		if (crt_out != -1)
			_set_bit(&new_video_out, HCI_VIDEO_OUT_CRT, crt_out);
		if (tv_out != -1)
			_set_bit(&new_video_out, HCI_VIDEO_OUT_TV, tv_out);
		/*
		 * To avoid unnecessary video disruption, only write the new
		 * video setting if something changed. */
		if (new_video_out != video_out)
			ret = write_acpi_int(METHOD_VIDEO_OUT, new_video_out);
	}

	return ret ? ret : count;
}

static const struct file_operations video_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= video_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= video_proc_write,
};

static int get_fan_status(struct toshiba_acpi_dev *dev, u32 *status)
{
	u32 hci_result;

	hci_result = hci_read1(dev, HCI_FAN, status);
	return hci_result == TOS_SUCCESS ? 0 : -EIO;
}

static int fan_proc_show(struct seq_file *m, void *v)
{
	struct toshiba_acpi_dev *dev = m->private;
	int ret;
	u32 value;

	ret = get_fan_status(dev, &value);
	if (!ret) {
		seq_printf(m, "running:                 %d\n", (value > 0));
		seq_printf(m, "force_on:                %d\n", dev->force_fan);
	}

	return ret;
}

static int fan_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, fan_proc_show, PDE_DATA(inode));
}

static ssize_t fan_proc_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *pos)
{
	struct toshiba_acpi_dev *dev = PDE_DATA(file_inode(file));
	char cmd[42];
	size_t len;
	int value;
	u32 hci_result;

	len = min(count, sizeof(cmd) - 1);
	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = '\0';

	if (sscanf(cmd, " force_on : %i", &value) == 1 &&
	    value >= 0 && value <= 1) {
		hci_result = hci_write1(dev, HCI_FAN, value);
		if (hci_result == TOS_SUCCESS)
			dev->force_fan = value;
		else
			return -EIO;
	} else {
		return -EINVAL;
	}

	return count;
}

static const struct file_operations fan_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= fan_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= fan_proc_write,
};

static int keys_proc_show(struct seq_file *m, void *v)
{
	struct toshiba_acpi_dev *dev = m->private;
	u32 hci_result;
	u32 value;

	if (!dev->key_event_valid && dev->system_event_supported) {
		hci_result = hci_read1(dev, HCI_SYSTEM_EVENT, &value);
		if (hci_result == TOS_SUCCESS) {
			dev->key_event_valid = 1;
			dev->last_key_event = value;
		} else if (hci_result == TOS_FIFO_EMPTY) {
			/* Better luck next time */
		} else if (hci_result == TOS_NOT_SUPPORTED) {
			/*
			 * This is a workaround for an unresolved issue on
			 * some machines where system events sporadically
			 * become disabled.
			 */
			hci_result = hci_write1(dev, HCI_SYSTEM_EVENT, 1);
			pr_notice("Re-enabled hotkeys\n");
		} else {
			pr_err("Error reading hotkey status\n");
			return -EIO;
		}
	}

	seq_printf(m, "hotkey_ready:            %d\n", dev->key_event_valid);
	seq_printf(m, "hotkey:                  0x%04x\n", dev->last_key_event);
	return 0;
}

static int keys_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, keys_proc_show, PDE_DATA(inode));
}

static ssize_t keys_proc_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *pos)
{
	struct toshiba_acpi_dev *dev = PDE_DATA(file_inode(file));
	char cmd[42];
	size_t len;
	int value;

	len = min(count, sizeof(cmd) - 1);
	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = '\0';

	if (sscanf(cmd, " hotkey_ready : %i", &value) == 1 && value == 0)
		dev->key_event_valid = 0;
	else
		return -EINVAL;

	return count;
}

static const struct file_operations keys_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= keys_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= keys_proc_write,
};

static int version_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "driver:                  %s\n", TOSHIBA_ACPI_VERSION);
	seq_printf(m, "proc_interface:          %d\n", PROC_INTERFACE_VERSION);
	return 0;
}

static int version_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, version_proc_show, PDE_DATA(inode));
}

static const struct file_operations version_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= version_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * Proc and module init
 */

#define PROC_TOSHIBA		"toshiba"

static void create_toshiba_proc_entries(struct toshiba_acpi_dev *dev)
{
	if (dev->backlight_dev)
		proc_create_data("lcd", S_IRUGO | S_IWUSR, toshiba_proc_dir,
				 &lcd_proc_fops, dev);
	if (dev->video_supported)
		proc_create_data("video", S_IRUGO | S_IWUSR, toshiba_proc_dir,
				 &video_proc_fops, dev);
	if (dev->fan_supported)
		proc_create_data("fan", S_IRUGO | S_IWUSR, toshiba_proc_dir,
				 &fan_proc_fops, dev);
	if (dev->hotkey_dev)
		proc_create_data("keys", S_IRUGO | S_IWUSR, toshiba_proc_dir,
				 &keys_proc_fops, dev);
	proc_create_data("version", S_IRUGO, toshiba_proc_dir,
			 &version_proc_fops, dev);
}

static void remove_toshiba_proc_entries(struct toshiba_acpi_dev *dev)
{
	if (dev->backlight_dev)
		remove_proc_entry("lcd", toshiba_proc_dir);
	if (dev->video_supported)
		remove_proc_entry("video", toshiba_proc_dir);
	if (dev->fan_supported)
		remove_proc_entry("fan", toshiba_proc_dir);
	if (dev->hotkey_dev)
		remove_proc_entry("keys", toshiba_proc_dir);
	remove_proc_entry("version", toshiba_proc_dir);
}

static const struct backlight_ops toshiba_backlight_data = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = get_lcd_brightness,
	.update_status  = set_lcd_status,
};

/*
 * Sysfs files
 */
static ssize_t version_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", TOSHIBA_ACPI_VERSION);
}
static DEVICE_ATTR_RO(version);

static ssize_t fan_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	u32 result;
	int state;
	int ret;

	ret = kstrtoint(buf, 0, &state);
	if (ret)
		return ret;

	if (state != 0 && state != 1)
		return -EINVAL;

	result = hci_write1(toshiba, HCI_FAN, state);
	if (result == TOS_FAILURE)
		return -EIO;
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return count;
}

static ssize_t fan_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	u32 value;
	int ret;

	ret = get_fan_status(toshiba, &value);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", value);
}
static DEVICE_ATTR_RW(fan);

static ssize_t kbd_backlight_mode_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	int mode;
	int time;
	int ret;


	ret = kstrtoint(buf, 0, &mode);
	if (ret)
		return ret;

	/* Check for supported modes depending on keyboard backlight type */
	if (toshiba->kbd_type == 1) {
		/* Type 1 supports SCI_KBD_MODE_FNZ and SCI_KBD_MODE_AUTO */
		if (mode != SCI_KBD_MODE_FNZ && mode != SCI_KBD_MODE_AUTO)
			return -EINVAL;
	} else if (toshiba->kbd_type == 2) {
		/* Type 2 doesn't support SCI_KBD_MODE_FNZ */
		if (mode != SCI_KBD_MODE_AUTO && mode != SCI_KBD_MODE_ON &&
		    mode != SCI_KBD_MODE_OFF)
			return -EINVAL;
	}

	/*
	 * Set the Keyboard Backlight Mode where:
	 *	Auto - KBD backlight turns off automatically in given time
	 *	FN-Z - KBD backlight "toggles" when hotkey pressed
	 *	ON   - KBD backlight is always on
	 *	OFF  - KBD backlight is always off
	 */

	/* Only make a change if the actual mode has changed */
	if (toshiba->kbd_mode != mode) {
		/* Shift the time to "base time" (0x3c0000 == 60 seconds) */
		time = toshiba->kbd_time << HCI_MISC_SHIFT;

		/* OR the "base time" to the actual method format */
		if (toshiba->kbd_type == 1) {
			/* Type 1 requires the current mode */
			time |= toshiba->kbd_mode;
		} else if (toshiba->kbd_type == 2) {
			/* Type 2 requires the desired mode */
			time |= mode;
		}

		ret = toshiba_kbd_illum_status_set(toshiba, time);
		if (ret)
			return ret;

		toshiba->kbd_mode = mode;
	}

	return count;
}

static ssize_t kbd_backlight_mode_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	u32 time;

	if (toshiba_kbd_illum_status_get(toshiba, &time) < 0)
		return -EIO;

	return sprintf(buf, "%i\n", time & SCI_KBD_MODE_MASK);
}
static DEVICE_ATTR_RW(kbd_backlight_mode);

static ssize_t kbd_type_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", toshiba->kbd_type);
}
static DEVICE_ATTR_RO(kbd_type);

static ssize_t available_kbd_modes_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);

	if (toshiba->kbd_type == 1)
		return sprintf(buf, "%x %x\n",
			       SCI_KBD_MODE_FNZ, SCI_KBD_MODE_AUTO);

	return sprintf(buf, "%x %x %x\n",
		       SCI_KBD_MODE_AUTO, SCI_KBD_MODE_ON, SCI_KBD_MODE_OFF);
}
static DEVICE_ATTR_RO(available_kbd_modes);

static ssize_t kbd_backlight_timeout_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	int time;
	int ret;

	ret = kstrtoint(buf, 0, &time);
	if (ret)
		return ret;

	/* Check for supported values depending on kbd_type */
	if (toshiba->kbd_type == 1) {
		if (time < 0 || time > 60)
			return -EINVAL;
	} else if (toshiba->kbd_type == 2) {
		if (time < 1 || time > 60)
			return -EINVAL;
	}

	/* Set the Keyboard Backlight Timeout */

	/* Only make a change if the actual timeout has changed */
	if (toshiba->kbd_time != time) {
		/* Shift the time to "base time" (0x3c0000 == 60 seconds) */
		time = time << HCI_MISC_SHIFT;
		/* OR the "base time" to the actual method format */
		if (toshiba->kbd_type == 1)
			time |= SCI_KBD_MODE_FNZ;
		else if (toshiba->kbd_type == 2)
			time |= SCI_KBD_MODE_AUTO;

		ret = toshiba_kbd_illum_status_set(toshiba, time);
		if (ret)
			return ret;

		toshiba->kbd_time = time >> HCI_MISC_SHIFT;
	}

	return count;
}

static ssize_t kbd_backlight_timeout_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	u32 time;

	if (toshiba_kbd_illum_status_get(toshiba, &time) < 0)
		return -EIO;

	return sprintf(buf, "%i\n", time >> HCI_MISC_SHIFT);
}
static DEVICE_ATTR_RW(kbd_backlight_timeout);

static ssize_t touchpad_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	int state;
	int ret;

	/* Set the TouchPad on/off, 0 - Disable | 1 - Enable */
	ret = kstrtoint(buf, 0, &state);
	if (ret)
		return ret;
	if (state != 0 && state != 1)
		return -EINVAL;

	ret = toshiba_touchpad_set(toshiba, state);
	if (ret)
		return ret;

	return count;
}

static ssize_t touchpad_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	u32 state;
	int ret;

	ret = toshiba_touchpad_get(toshiba, &state);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", state);
}
static DEVICE_ATTR_RW(touchpad);

static ssize_t position_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	u32 xyval, zval, tmp;
	u16 x, y, z;
	int ret;

	xyval = zval = 0;
	ret = toshiba_accelerometer_get(toshiba, &xyval, &zval);
	if (ret < 0)
		return ret;

	x = xyval & HCI_ACCEL_MASK;
	tmp = xyval >> HCI_MISC_SHIFT;
	y = tmp & HCI_ACCEL_MASK;
	z = zval & HCI_ACCEL_MASK;

	return sprintf(buf, "%d %d %d\n", x, y, z);
}
static DEVICE_ATTR_RO(position);

static ssize_t usb_sleep_charge_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	u32 mode;
	int ret;

	ret = toshiba_usb_sleep_charge_get(toshiba, &mode);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%x\n", mode & SCI_USB_CHARGE_MODE_MASK);
}

static ssize_t usb_sleep_charge_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	u32 mode;
	int state;
	int ret;

	ret = kstrtoint(buf, 0, &state);
	if (ret)
		return ret;
	/*
	 * Check for supported values, where:
	 * 0 - Disabled
	 * 1 - Alternate (Non USB conformant devices that require more power)
	 * 2 - Auto (USB conformant devices)
	 */
	if (state != 0 && state != 1 && state != 2)
		return -EINVAL;

	/* Set the USB charging mode to internal value */
	if (state == 0)
		mode = SCI_USB_CHARGE_DISABLED;
	else if (state == 1)
		mode = SCI_USB_CHARGE_ALTERNATE;
	else if (state == 2)
		mode = SCI_USB_CHARGE_AUTO;

	ret = toshiba_usb_sleep_charge_set(toshiba, mode);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(usb_sleep_charge);

static ssize_t sleep_functions_on_battery_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	u32 state;
	int bat_lvl;
	int status;
	int ret;
	int tmp;

	ret = toshiba_sleep_functions_status_get(toshiba, &state);
	if (ret < 0)
		return ret;

	/* Determine the status: 0x4 - Enabled | 0x1 - Disabled */
	tmp = state & SCI_USB_CHARGE_BAT_MASK;
	status = (tmp == 0x4) ? 1 : 0;
	/* Determine the battery level set */
	bat_lvl = state >> HCI_MISC_SHIFT;

	return sprintf(buf, "%d %d\n", status, bat_lvl);
}

static ssize_t sleep_functions_on_battery_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	u32 status;
	int value;
	int ret;
	int tmp;

	ret = kstrtoint(buf, 0, &value);
	if (ret)
		return ret;

	/*
	 * Set the status of the function:
	 * 0 - Disabled
	 * 1-100 - Enabled
	 */
	if (value < 0 || value > 100)
		return -EINVAL;

	if (value == 0) {
		tmp = toshiba->usbsc_bat_level << HCI_MISC_SHIFT;
		status = tmp | SCI_USB_CHARGE_BAT_LVL_OFF;
	} else {
		tmp = value << HCI_MISC_SHIFT;
		status = tmp | SCI_USB_CHARGE_BAT_LVL_ON;
	}
	ret = toshiba_sleep_functions_status_set(toshiba, status);
	if (ret < 0)
		return ret;

	toshiba->usbsc_bat_level = status >> HCI_MISC_SHIFT;

	return count;
}
static DEVICE_ATTR_RW(sleep_functions_on_battery);

static ssize_t usb_rapid_charge_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	u32 state;
	int ret;

	ret = toshiba_usb_rapid_charge_get(toshiba, &state);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", state);
}

static ssize_t usb_rapid_charge_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	int state;
	int ret;

	ret = kstrtoint(buf, 0, &state);
	if (ret)
		return ret;
	if (state != 0 && state != 1)
		return -EINVAL;

	ret = toshiba_usb_rapid_charge_set(toshiba, state);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(usb_rapid_charge);

static ssize_t usb_sleep_music_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	u32 state;
	int ret;

	ret = toshiba_usb_sleep_music_get(toshiba, &state);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", state);
}

static ssize_t usb_sleep_music_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	int state;
	int ret;

	ret = kstrtoint(buf, 0, &state);
	if (ret)
		return ret;
	if (state != 0 && state != 1)
		return -EINVAL;

	ret = toshiba_usb_sleep_music_set(toshiba, state);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(usb_sleep_music);

static ssize_t kbd_function_keys_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	int mode;
	int ret;

	ret = toshiba_function_keys_get(toshiba, &mode);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", mode);
}

static ssize_t kbd_function_keys_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	int mode;
	int ret;

	ret = kstrtoint(buf, 0, &mode);
	if (ret)
		return ret;
	/*
	 * Check for the function keys mode where:
	 * 0 - Normal operation (F{1-12} as usual and hotkeys via FN-F{1-12})
	 * 1 - Special functions (Opposite of the above setting)
	 */
	if (mode != 0 && mode != 1)
		return -EINVAL;

	ret = toshiba_function_keys_set(toshiba, mode);
	if (ret)
		return ret;

	pr_info("Reboot for changes to KBD Function Keys to take effect");

	return count;
}
static DEVICE_ATTR_RW(kbd_function_keys);

static ssize_t panel_power_on_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	u32 state;
	int ret;

	ret = toshiba_panel_power_on_get(toshiba, &state);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", state);
}

static ssize_t panel_power_on_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	int state;
	int ret;

	ret = kstrtoint(buf, 0, &state);
	if (ret)
		return ret;
	if (state != 0 && state != 1)
		return -EINVAL;

	ret = toshiba_panel_power_on_set(toshiba, state);
	if (ret)
		return ret;

	pr_info("Reboot for changes to Panel Power ON to take effect");

	return count;
}
static DEVICE_ATTR_RW(panel_power_on);

static ssize_t usb_three_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	u32 state;
	int ret;

	ret = toshiba_usb_three_get(toshiba, &state);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", state);
}

static ssize_t usb_three_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	int state;
	int ret;

	ret = kstrtoint(buf, 0, &state);
	if (ret)
		return ret;
	/*
	 * Check for USB 3 mode where:
	 * 0 - Disabled (Acts like a USB 2 port, saving power)
	 * 1 - Enabled
	 */
	if (state != 0 && state != 1)
		return -EINVAL;

	ret = toshiba_usb_three_set(toshiba, state);
	if (ret)
		return ret;

	pr_info("Reboot for changes to USB 3 to take effect");

	return count;
}
static DEVICE_ATTR_RW(usb_three);

static struct attribute *toshiba_attributes[] = {
	&dev_attr_version.attr,
	&dev_attr_fan.attr,
	&dev_attr_kbd_backlight_mode.attr,
	&dev_attr_kbd_type.attr,
	&dev_attr_available_kbd_modes.attr,
	&dev_attr_kbd_backlight_timeout.attr,
	&dev_attr_touchpad.attr,
	&dev_attr_position.attr,
	&dev_attr_usb_sleep_charge.attr,
	&dev_attr_sleep_functions_on_battery.attr,
	&dev_attr_usb_rapid_charge.attr,
	&dev_attr_usb_sleep_music.attr,
	&dev_attr_kbd_function_keys.attr,
	&dev_attr_panel_power_on.attr,
	&dev_attr_usb_three.attr,
	NULL,
};

static umode_t toshiba_sysfs_is_visible(struct kobject *kobj,
					struct attribute *attr, int idx)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct toshiba_acpi_dev *drv = dev_get_drvdata(dev);
	bool exists = true;

	if (attr == &dev_attr_fan.attr)
		exists = (drv->fan_supported) ? true : false;
	else if (attr == &dev_attr_kbd_backlight_mode.attr)
		exists = (drv->kbd_illum_supported) ? true : false;
	else if (attr == &dev_attr_kbd_backlight_timeout.attr)
		exists = (drv->kbd_mode == SCI_KBD_MODE_AUTO) ? true : false;
	else if (attr == &dev_attr_touchpad.attr)
		exists = (drv->touchpad_supported) ? true : false;
	else if (attr == &dev_attr_position.attr)
		exists = (drv->accelerometer_supported) ? true : false;
	else if (attr == &dev_attr_usb_sleep_charge.attr)
		exists = (drv->usb_sleep_charge_supported) ? true : false;
	else if (attr == &dev_attr_sleep_functions_on_battery.attr)
		exists = (drv->usb_sleep_charge_supported) ? true : false;
	else if (attr == &dev_attr_usb_rapid_charge.attr)
		exists = (drv->usb_rapid_charge_supported) ? true : false;
	else if (attr == &dev_attr_usb_sleep_music.attr)
		exists = (drv->usb_sleep_music_supported) ? true : false;
	else if (attr == &dev_attr_kbd_function_keys.attr)
		exists = (drv->kbd_function_keys_supported) ? true : false;
	else if (attr == &dev_attr_panel_power_on.attr)
		exists = (drv->panel_power_on_supported) ? true : false;
	else if (attr == &dev_attr_usb_three.attr)
		exists = (drv->usb_three_supported) ? true : false;

	return exists ? attr->mode : 0;
}

static struct attribute_group toshiba_attr_group = {
	.is_visible = toshiba_sysfs_is_visible,
	.attrs = toshiba_attributes,
};

/*
 * Hotkeys
 */
static int toshiba_acpi_enable_hotkeys(struct toshiba_acpi_dev *dev)
{
	acpi_status status;
	u32 result;

	status = acpi_evaluate_object(dev->acpi_dev->handle,
				      "ENAB", NULL, NULL);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	result = hci_write1(dev, HCI_HOTKEY_EVENT, HCI_HOTKEY_ENABLE);
	if (result == TOS_FAILURE)
		return -EIO;
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return 0;
}

static bool toshiba_acpi_i8042_filter(unsigned char data, unsigned char str,
				      struct serio *port)
{
	if (str & I8042_STR_AUXDATA)
		return false;

	if (unlikely(data == 0xe0))
		return false;

	if ((data & 0x7f) == TOS1900_FN_SCAN) {
		schedule_work(&toshiba_acpi->hotkey_work);
		return true;
	}

	return false;
}

static void toshiba_acpi_hotkey_work(struct work_struct *work)
{
	acpi_handle ec_handle = ec_get_handle();
	acpi_status status;

	if (!ec_handle)
		return;

	status = acpi_evaluate_object(ec_handle, "NTFY", NULL, NULL);
	if (ACPI_FAILURE(status))
		pr_err("ACPI NTFY method execution failed\n");
}

/*
 * Returns hotkey scancode, or < 0 on failure.
 */
static int toshiba_acpi_query_hotkey(struct toshiba_acpi_dev *dev)
{
	unsigned long long value;
	acpi_status status;

	status = acpi_evaluate_integer(dev->acpi_dev->handle, "INFO",
				      NULL, &value);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI INFO method execution failed\n");
		return -EIO;
	}

	return value;
}

static void toshiba_acpi_report_hotkey(struct toshiba_acpi_dev *dev,
				       int scancode)
{
	if (scancode == 0x100)
		return;

	/* Act on key press; ignore key release */
	if (scancode & 0x80)
		return;

	if (!sparse_keymap_report_event(dev->hotkey_dev, scancode, 1, true))
		pr_info("Unknown key %x\n", scancode);
}

static void toshiba_acpi_process_hotkeys(struct toshiba_acpi_dev *dev)
{
	u32 hci_result, value;
	int retries = 3;
	int scancode;

	if (dev->info_supported) {
		scancode = toshiba_acpi_query_hotkey(dev);
		if (scancode < 0)
			pr_err("Failed to query hotkey event\n");
		else if (scancode != 0)
			toshiba_acpi_report_hotkey(dev, scancode);
	} else if (dev->system_event_supported) {
		do {
			hci_result = hci_read1(dev, HCI_SYSTEM_EVENT, &value);
			switch (hci_result) {
			case TOS_SUCCESS:
				toshiba_acpi_report_hotkey(dev, (int)value);
				break;
			case TOS_NOT_SUPPORTED:
				/*
				 * This is a workaround for an unresolved
				 * issue on some machines where system events
				 * sporadically become disabled.
				 */
				hci_result =
					hci_write1(dev, HCI_SYSTEM_EVENT, 1);
				pr_notice("Re-enabled hotkeys\n");
				/* Fall through */
			default:
				retries--;
				break;
			}
		} while (retries && hci_result != TOS_FIFO_EMPTY);
	}
}

static int toshiba_acpi_setup_keyboard(struct toshiba_acpi_dev *dev)
{
	acpi_handle ec_handle;
	int error;
	u32 hci_result;
	const struct key_entry *keymap = toshiba_acpi_keymap;

	dev->hotkey_dev = input_allocate_device();
	if (!dev->hotkey_dev)
		return -ENOMEM;

	dev->hotkey_dev->name = "Toshiba input device";
	dev->hotkey_dev->phys = "toshiba_acpi/input0";
	dev->hotkey_dev->id.bustype = BUS_HOST;

	if (dmi_check_system(toshiba_alt_keymap_dmi))
		keymap = toshiba_acpi_alt_keymap;
	error = sparse_keymap_setup(dev->hotkey_dev, keymap, NULL);
	if (error)
		goto err_free_dev;

	/*
	 * For some machines the SCI responsible for providing hotkey
	 * notification doesn't fire. We can trigger the notification
	 * whenever the Fn key is pressed using the NTFY method, if
	 * supported, so if it's present set up an i8042 key filter
	 * for this purpose.
	 */
	ec_handle = ec_get_handle();
	if (ec_handle && acpi_has_method(ec_handle, "NTFY")) {
		INIT_WORK(&dev->hotkey_work, toshiba_acpi_hotkey_work);

		error = i8042_install_filter(toshiba_acpi_i8042_filter);
		if (error) {
			pr_err("Error installing key filter\n");
			goto err_free_keymap;
		}

		dev->ntfy_supported = 1;
	}

	/*
	 * Determine hotkey query interface. Prefer using the INFO
	 * method when it is available.
	 */
	if (acpi_has_method(dev->acpi_dev->handle, "INFO"))
		dev->info_supported = 1;
	else {
		hci_result = hci_write1(dev, HCI_SYSTEM_EVENT, 1);
		if (hci_result == TOS_SUCCESS)
			dev->system_event_supported = 1;
	}

	if (!dev->info_supported && !dev->system_event_supported) {
		pr_warn("No hotkey query interface found\n");
		goto err_remove_filter;
	}

	error = toshiba_acpi_enable_hotkeys(dev);
	if (error) {
		pr_info("Unable to enable hotkeys\n");
		goto err_remove_filter;
	}

	error = input_register_device(dev->hotkey_dev);
	if (error) {
		pr_info("Unable to register input device\n");
		goto err_remove_filter;
	}

	return 0;

 err_remove_filter:
	if (dev->ntfy_supported)
		i8042_remove_filter(toshiba_acpi_i8042_filter);
 err_free_keymap:
	sparse_keymap_free(dev->hotkey_dev);
 err_free_dev:
	input_free_device(dev->hotkey_dev);
	dev->hotkey_dev = NULL;
	return error;
}

static int toshiba_acpi_setup_backlight(struct toshiba_acpi_dev *dev)
{
	struct backlight_properties props;
	int brightness;
	int ret;
	bool enabled;

	/*
	 * Some machines don't support the backlight methods at all, and
	 * others support it read-only. Either of these is pretty useless,
	 * so only register the backlight device if the backlight method
	 * supports both reads and writes.
	 */
	brightness = __get_lcd_brightness(dev);
	if (brightness < 0)
		return 0;
	ret = set_lcd_brightness(dev, brightness);
	if (ret) {
		pr_debug("Backlight method is read-only, disabling backlight support\n");
		return 0;
	}

	/* Determine whether or not BIOS supports transflective backlight */
	ret = get_tr_backlight_status(dev, &enabled);
	dev->tr_backlight_supported = !ret;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = HCI_LCD_BRIGHTNESS_LEVELS - 1;

	/* Adding an extra level and having 0 change to transflective mode */
	if (dev->tr_backlight_supported)
		props.max_brightness++;

	dev->backlight_dev = backlight_device_register("toshiba",
						       &dev->acpi_dev->dev,
						       dev,
						       &toshiba_backlight_data,
						       &props);
	if (IS_ERR(dev->backlight_dev)) {
		ret = PTR_ERR(dev->backlight_dev);
		pr_err("Could not register toshiba backlight device\n");
		dev->backlight_dev = NULL;
		return ret;
	}

	dev->backlight_dev->props.brightness = brightness;
	return 0;
}

static int toshiba_acpi_remove(struct acpi_device *acpi_dev)
{
	struct toshiba_acpi_dev *dev = acpi_driver_data(acpi_dev);

	remove_toshiba_proc_entries(dev);

	if (dev->sysfs_created)
		sysfs_remove_group(&dev->acpi_dev->dev.kobj,
				   &toshiba_attr_group);

	if (dev->ntfy_supported) {
		i8042_remove_filter(toshiba_acpi_i8042_filter);
		cancel_work_sync(&dev->hotkey_work);
	}

	if (dev->hotkey_dev) {
		input_unregister_device(dev->hotkey_dev);
		sparse_keymap_free(dev->hotkey_dev);
	}

	if (dev->bt_rfk) {
		rfkill_unregister(dev->bt_rfk);
		rfkill_destroy(dev->bt_rfk);
	}

	backlight_device_unregister(dev->backlight_dev);

	if (dev->illumination_supported)
		led_classdev_unregister(&dev->led_dev);

	if (dev->kbd_led_registered)
		led_classdev_unregister(&dev->kbd_led);

	if (dev->eco_supported)
		led_classdev_unregister(&dev->eco_led);

	if (toshiba_acpi)
		toshiba_acpi = NULL;

	kfree(dev);

	return 0;
}

static const char *find_hci_method(acpi_handle handle)
{
	if (acpi_has_method(handle, "GHCI"))
		return "GHCI";

	if (acpi_has_method(handle, "SPFC"))
		return "SPFC";

	return NULL;
}

static int toshiba_acpi_add(struct acpi_device *acpi_dev)
{
	struct toshiba_acpi_dev *dev;
	const char *hci_method;
	u32 dummy;
	bool bt_present;
	int ret = 0;

	if (toshiba_acpi)
		return -EBUSY;

	pr_info("Toshiba Laptop ACPI Extras version %s\n",
	       TOSHIBA_ACPI_VERSION);

	hci_method = find_hci_method(acpi_dev->handle);
	if (!hci_method) {
		pr_err("HCI interface not found\n");
		return -ENODEV;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->acpi_dev = acpi_dev;
	dev->method_hci = hci_method;
	acpi_dev->driver_data = dev;
	dev_set_drvdata(&acpi_dev->dev, dev);

	if (toshiba_acpi_setup_keyboard(dev))
		pr_info("Unable to activate hotkeys\n");

	mutex_init(&dev->mutex);

	ret = toshiba_acpi_setup_backlight(dev);
	if (ret)
		goto error;

	/* Register rfkill switch for Bluetooth */
	if (hci_get_bt_present(dev, &bt_present) == TOS_SUCCESS && bt_present) {
		dev->bt_rfk = rfkill_alloc("Toshiba Bluetooth",
					   &acpi_dev->dev,
					   RFKILL_TYPE_BLUETOOTH,
					   &toshiba_rfk_ops,
					   dev);
		if (!dev->bt_rfk) {
			pr_err("unable to allocate rfkill device\n");
			ret = -ENOMEM;
			goto error;
		}

		ret = rfkill_register(dev->bt_rfk);
		if (ret) {
			pr_err("unable to register rfkill device\n");
			rfkill_destroy(dev->bt_rfk);
			goto error;
		}
	}

	if (toshiba_illumination_available(dev)) {
		dev->led_dev.name = "toshiba::illumination";
		dev->led_dev.max_brightness = 1;
		dev->led_dev.brightness_set = toshiba_illumination_set;
		dev->led_dev.brightness_get = toshiba_illumination_get;
		if (!led_classdev_register(&acpi_dev->dev, &dev->led_dev))
			dev->illumination_supported = 1;
	}

	if (toshiba_eco_mode_available(dev)) {
		dev->eco_led.name = "toshiba::eco_mode";
		dev->eco_led.max_brightness = 1;
		dev->eco_led.brightness_set = toshiba_eco_mode_set_status;
		dev->eco_led.brightness_get = toshiba_eco_mode_get_status;
		if (!led_classdev_register(&dev->acpi_dev->dev, &dev->eco_led))
			dev->eco_supported = 1;
	}

	dev->kbd_illum_supported = toshiba_kbd_illum_available(dev);
	/*
	 * Only register the LED if KBD illumination is supported
	 * and the keyboard backlight operation mode is set to FN-Z
	 */
	if (dev->kbd_illum_supported && dev->kbd_mode == SCI_KBD_MODE_FNZ) {
		dev->kbd_led.name = "toshiba::kbd_backlight";
		dev->kbd_led.max_brightness = 1;
		dev->kbd_led.brightness_set = toshiba_kbd_backlight_set;
		dev->kbd_led.brightness_get = toshiba_kbd_backlight_get;
		if (!led_classdev_register(&dev->acpi_dev->dev, &dev->kbd_led))
			dev->kbd_led_registered = 1;
	}

	ret = toshiba_touchpad_get(dev, &dummy);
	dev->touchpad_supported = !ret;

	ret = toshiba_accelerometer_supported(dev);
	dev->accelerometer_supported = !ret;

	ret = toshiba_usb_sleep_charge_get(dev, &dummy);
	dev->usb_sleep_charge_supported = !ret;

	ret = toshiba_usb_rapid_charge_get(dev, &dummy);
	dev->usb_rapid_charge_supported = !ret;

	ret = toshiba_usb_sleep_music_get(dev, &dummy);
	dev->usb_sleep_music_supported = !ret;

	ret = toshiba_function_keys_get(dev, &dummy);
	dev->kbd_function_keys_supported = !ret;

	ret = toshiba_panel_power_on_get(dev, &dummy);
	dev->panel_power_on_supported = !ret;

	ret = toshiba_usb_three_get(dev, &dummy);
	dev->usb_three_supported = !ret;

	/* Determine whether or not BIOS supports fan and video interfaces */

	ret = get_video_status(dev, &dummy);
	dev->video_supported = !ret;

	ret = get_fan_status(dev, &dummy);
	dev->fan_supported = !ret;

	ret = sysfs_create_group(&dev->acpi_dev->dev.kobj,
				 &toshiba_attr_group);
	if (ret) {
		dev->sysfs_created = 0;
		goto error;
	}
	dev->sysfs_created = !ret;

	create_toshiba_proc_entries(dev);

	toshiba_acpi = dev;

	return 0;

error:
	toshiba_acpi_remove(acpi_dev);
	return ret;
}

static void toshiba_acpi_notify(struct acpi_device *acpi_dev, u32 event)
{
	struct toshiba_acpi_dev *dev = acpi_driver_data(acpi_dev);
	int ret;

	switch (event) {
	case 0x80: /* Hotkeys and some system events */
		toshiba_acpi_process_hotkeys(dev);
		break;
	case 0x92: /* Keyboard backlight mode changed */
		/* Update sysfs entries */
		ret = sysfs_update_group(&acpi_dev->dev.kobj,
					 &toshiba_attr_group);
		if (ret)
			pr_err("Unable to update sysfs entries\n");
		break;
	case 0x81: /* Unknown */
	case 0x82: /* Unknown */
	case 0x83: /* Unknown */
	case 0x8c: /* Unknown */
	case 0x8e: /* Unknown */
	case 0x8f: /* Unknown */
	case 0x90: /* Unknown */
	default:
		pr_info("Unknown event received %x\n", event);
		break;
	}
}

#ifdef CONFIG_PM_SLEEP
static int toshiba_acpi_suspend(struct device *device)
{
	struct toshiba_acpi_dev *dev = acpi_driver_data(to_acpi_device(device));
	u32 result;

	if (dev->hotkey_dev)
		result = hci_write1(dev, HCI_HOTKEY_EVENT, HCI_HOTKEY_DISABLE);

	return 0;
}

static int toshiba_acpi_resume(struct device *device)
{
	struct toshiba_acpi_dev *dev = acpi_driver_data(to_acpi_device(device));
	int error;

	if (dev->hotkey_dev) {
		error = toshiba_acpi_enable_hotkeys(dev);
		if (error)
			pr_info("Unable to re-enable hotkeys\n");
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(toshiba_acpi_pm,
			 toshiba_acpi_suspend, toshiba_acpi_resume);

static struct acpi_driver toshiba_acpi_driver = {
	.name	= "Toshiba ACPI driver",
	.owner	= THIS_MODULE,
	.ids	= toshiba_device_ids,
	.flags	= ACPI_DRIVER_ALL_NOTIFY_EVENTS,
	.ops	= {
		.add		= toshiba_acpi_add,
		.remove		= toshiba_acpi_remove,
		.notify		= toshiba_acpi_notify,
	},
	.drv.pm	= &toshiba_acpi_pm,
};

static int __init toshiba_acpi_init(void)
{
	int ret;

	/*
	 * Machines with this WMI guid aren't supported due to bugs in
	 * their AML. This check relies on wmi initializing before
	 * toshiba_acpi to guarantee guids have been identified.
	 */
	if (wmi_has_guid(TOSHIBA_WMI_EVENT_GUID))
		return -ENODEV;

	toshiba_proc_dir = proc_mkdir(PROC_TOSHIBA, acpi_root_dir);
	if (!toshiba_proc_dir) {
		pr_err("Unable to create proc dir " PROC_TOSHIBA "\n");
		return -ENODEV;
	}

	ret = acpi_bus_register_driver(&toshiba_acpi_driver);
	if (ret) {
		pr_err("Failed to register ACPI driver: %d\n", ret);
		remove_proc_entry(PROC_TOSHIBA, acpi_root_dir);
	}

	return ret;
}

static void __exit toshiba_acpi_exit(void)
{
	acpi_bus_unregister_driver(&toshiba_acpi_driver);
	if (toshiba_proc_dir)
		remove_proc_entry(PROC_TOSHIBA, acpi_root_dir);
}

module_init(toshiba_acpi_init);
module_exit(toshiba_acpi_exit);
