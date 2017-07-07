/*
 *  toshiba_acpi.c - Toshiba Laptop ACPI Extras
 *
 *  Copyright (C) 2002-2004 John Belmonte
 *  Copyright (C) 2008 Philip Langdale
 *  Copyright (C) 2010 Pierre Ducroquet
 *  Copyright (C) 2014-2016 Azael Avalos
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

#define TOSHIBA_ACPI_VERSION	"0.24"
#define PROC_INTERFACE_VERSION	1

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/backlight.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/i8042.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/rfkill.h>
#include <linux/iio/iio.h>
#include <linux/toshiba.h>
#include <acpi/video.h>

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

/* Operations */
#define HCI_SET				0xff00
#define HCI_GET				0xfe00
#define SCI_OPEN			0xf100
#define SCI_CLOSE			0xf200
#define SCI_GET				0xf300
#define SCI_SET				0xf400

/* Return codes */
#define TOS_SUCCESS			0x0000
#define TOS_SUCCESS2			0x0001
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

/* Registers */
#define HCI_FAN				0x0004
#define HCI_TR_BACKLIGHT		0x0005
#define HCI_SYSTEM_EVENT		0x0016
#define HCI_VIDEO_OUT			0x001c
#define HCI_HOTKEY_EVENT		0x001e
#define HCI_LCD_BRIGHTNESS		0x002a
#define HCI_WIRELESS			0x0056
#define HCI_ACCELEROMETER		0x006d
#define HCI_COOLING_METHOD		0x007f
#define HCI_KBD_ILLUMINATION		0x0095
#define HCI_ECO_MODE			0x0097
#define HCI_ACCELEROMETER2		0x00a6
#define HCI_SYSTEM_INFO			0xc000
#define SCI_PANEL_POWER_ON		0x010d
#define SCI_ILLUMINATION		0x014e
#define SCI_USB_SLEEP_CHARGE		0x0150
#define SCI_KBD_ILLUM_STATUS		0x015c
#define SCI_USB_SLEEP_MUSIC		0x015e
#define SCI_USB_THREE			0x0169
#define SCI_TOUCHPAD			0x050e
#define SCI_KBD_FUNCTION_KEYS		0x0522

/* Field definitions */
#define HCI_ACCEL_MASK			0x7fff
#define HCI_ACCEL_DIRECTION_MASK	0x8000
#define HCI_HOTKEY_DISABLE		0x0b
#define HCI_HOTKEY_ENABLE		0x09
#define HCI_HOTKEY_SPECIAL_FUNCTIONS	0x10
#define HCI_LCD_BRIGHTNESS_BITS		3
#define HCI_LCD_BRIGHTNESS_SHIFT	(16-HCI_LCD_BRIGHTNESS_BITS)
#define HCI_LCD_BRIGHTNESS_LEVELS	(1 << HCI_LCD_BRIGHTNESS_BITS)
#define HCI_MISC_SHIFT			0x10
#define HCI_SYSTEM_TYPE1		0x10
#define HCI_SYSTEM_TYPE2		0x11
#define HCI_VIDEO_OUT_LCD		0x1
#define HCI_VIDEO_OUT_CRT		0x2
#define HCI_VIDEO_OUT_TV		0x4
#define SCI_KBD_MODE_MASK		0x1f
#define SCI_KBD_MODE_FNZ		0x1
#define SCI_KBD_MODE_AUTO		0x2
#define SCI_KBD_MODE_ON			0x8
#define SCI_KBD_MODE_OFF		0x10
#define SCI_KBD_TIME_MAX		0x3c001a
#define HCI_WIRELESS_STATUS		0x1
#define HCI_WIRELESS_WWAN		0x3
#define HCI_WIRELESS_WWAN_STATUS	0x2000
#define HCI_WIRELESS_WWAN_POWER		0x4000
#define SCI_USB_CHARGE_MODE_MASK	0xff
#define SCI_USB_CHARGE_DISABLED		0x00
#define SCI_USB_CHARGE_ALTERNATE	0x09
#define SCI_USB_CHARGE_TYPICAL		0x11
#define SCI_USB_CHARGE_AUTO		0x21
#define SCI_USB_CHARGE_BAT_MASK		0x7
#define SCI_USB_CHARGE_BAT_LVL_OFF	0x1
#define SCI_USB_CHARGE_BAT_LVL_ON	0x4
#define SCI_USB_CHARGE_BAT_LVL		0x0200
#define SCI_USB_CHARGE_RAPID_DSP	0x0300

struct toshiba_acpi_dev {
	struct acpi_device *acpi_dev;
	const char *method_hci;
	struct input_dev *hotkey_dev;
	struct work_struct hotkey_work;
	struct backlight_device *backlight_dev;
	struct led_classdev led_dev;
	struct led_classdev kbd_led;
	struct led_classdev eco_led;
	struct miscdevice miscdev;
	struct rfkill *wwan_rfk;
	struct iio_dev *indio_dev;

	int force_fan;
	int last_key_event;
	int key_event_valid;
	int kbd_type;
	int kbd_mode;
	int kbd_time;
	int usbsc_bat_level;
	int usbsc_mode_base;
	int hotkey_event_type;
	int max_cooling_method;

	unsigned int illumination_supported:1;
	unsigned int video_supported:1;
	unsigned int fan_supported:1;
	unsigned int system_event_supported:1;
	unsigned int ntfy_supported:1;
	unsigned int info_supported:1;
	unsigned int tr_backlight_supported:1;
	unsigned int kbd_illum_supported:1;
	unsigned int touchpad_supported:1;
	unsigned int eco_supported:1;
	unsigned int accelerometer_supported:1;
	unsigned int usb_sleep_charge_supported:1;
	unsigned int usb_rapid_charge_supported:1;
	unsigned int usb_sleep_music_supported:1;
	unsigned int kbd_function_keys_supported:1;
	unsigned int panel_power_on_supported:1;
	unsigned int usb_three_supported:1;
	unsigned int wwan_supported:1;
	unsigned int cooling_method_supported:1;
	unsigned int sysfs_created:1;
	unsigned int special_functions;

	bool kbd_event_generated;
	bool kbd_led_registered;
	bool illumination_led_registered;
	bool eco_led_registered;
	bool killswitch;
};

static struct toshiba_acpi_dev *toshiba_acpi;

static bool disable_hotkeys;
module_param(disable_hotkeys, bool, 0444);
MODULE_PARM_DESC(disable_hotkeys, "Disables the hotkeys activation");

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

static const struct key_entry toshiba_acpi_alt_keymap[] = {
	{ KE_KEY, 0x102, { KEY_ZOOMOUT } },
	{ KE_KEY, 0x103, { KEY_ZOOMIN } },
	{ KE_KEY, 0x12c, { KEY_KBDILLUMTOGGLE } },
	{ KE_KEY, 0x139, { KEY_ZOOMRESET } },
	{ KE_KEY, 0x13c, { KEY_BRIGHTNESSDOWN } },
	{ KE_KEY, 0x13d, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 0x13e, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0x13f, { KEY_TOUCHPAD_TOGGLE } },
	{ KE_KEY, 0x157, { KEY_MUTE } },
	{ KE_KEY, 0x158, { KEY_WLAN } },
	{ KE_END, 0 },
};

/*
 * List of models which have a broken acpi-video backlight interface and thus
 * need to use the toshiba (vendor) interface instead.
 */
static const struct dmi_system_id toshiba_vendor_backlight_dmi[] = {
	{}
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
	union acpi_object in_objs[TCI_WORDS], out_objs[TCI_WORDS + 1];
	struct acpi_object_list params;
	struct acpi_buffer results;
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
 * Common hci tasks
 *
 * In addition to the ACPI status, the HCI system returns a result which
 * may be useful (such as "not supported").
 */

static u32 hci_write(struct toshiba_acpi_dev *dev, u32 reg, u32 in1)
{
	u32 in[TCI_WORDS] = { HCI_SET, reg, in1, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status = tci_raw(dev, in, out);

	return ACPI_SUCCESS(status) ? out[0] : TOS_FAILURE;
}

static u32 hci_read(struct toshiba_acpi_dev *dev, u32 reg, u32 *out1)
{
	u32 in[TCI_WORDS] = { HCI_GET, reg, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status = tci_raw(dev, in, out);

	if (ACPI_FAILURE(status))
		return TOS_FAILURE;

	*out1 = out[2];

	return out[0];
}

/*
 * Common sci tasks
 */

static int sci_open(struct toshiba_acpi_dev *dev)
{
	u32 in[TCI_WORDS] = { SCI_OPEN, 0, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status = tci_raw(dev, in, out);

	if  (ACPI_FAILURE(status)) {
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
	acpi_status status = tci_raw(dev, in, out);

	if (ACPI_FAILURE(status)) {
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
static void toshiba_illumination_available(struct toshiba_acpi_dev *dev)
{
	u32 in[TCI_WORDS] = { SCI_GET, SCI_ILLUMINATION, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	dev->illumination_supported = 0;
	dev->illumination_led_registered = false;

	if (!sci_open(dev))
		return;

	status = tci_raw(dev, in, out);
	sci_close(dev);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to query Illumination support failed\n");
		return;
	}

	if (out[0] != TOS_SUCCESS)
		return;

	dev->illumination_supported = 1;
}

static void toshiba_illumination_set(struct led_classdev *cdev,
				     enum led_brightness brightness)
{
	struct toshiba_acpi_dev *dev = container_of(cdev,
			struct toshiba_acpi_dev, led_dev);
	u32 result;
	u32 state;

	/* First request : initialize communication. */
	if (!sci_open(dev))
		return;

	/* Switch the illumination on/off */
	state = brightness ? 1 : 0;
	result = sci_write(dev, SCI_ILLUMINATION, state);
	sci_close(dev);
	if (result == TOS_FAILURE)
		pr_err("ACPI call for illumination failed\n");
}

static enum led_brightness toshiba_illumination_get(struct led_classdev *cdev)
{
	struct toshiba_acpi_dev *dev = container_of(cdev,
			struct toshiba_acpi_dev, led_dev);
	u32 result;
	u32 state;

	/* First request : initialize communication. */
	if (!sci_open(dev))
		return LED_OFF;

	/* Check the illumination */
	result = sci_read(dev, SCI_ILLUMINATION, &state);
	sci_close(dev);
	if (result == TOS_FAILURE) {
		pr_err("ACPI call for illumination failed\n");
		return LED_OFF;
	} else if (result != TOS_SUCCESS) {
		return LED_OFF;
	}

	return state ? LED_FULL : LED_OFF;
}

/* KBD Illumination */
static void toshiba_kbd_illum_available(struct toshiba_acpi_dev *dev)
{
	u32 in[TCI_WORDS] = { SCI_GET, SCI_KBD_ILLUM_STATUS, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	dev->kbd_illum_supported = 0;
	dev->kbd_led_registered = false;
	dev->kbd_event_generated = false;

	if (!sci_open(dev))
		return;

	status = tci_raw(dev, in, out);
	sci_close(dev);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to query kbd illumination support failed\n");
		return;
	}

	if (out[0] != TOS_SUCCESS)
		return;

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
	/* Flag as supported */
	dev->kbd_illum_supported = 1;
}

static int toshiba_kbd_illum_status_set(struct toshiba_acpi_dev *dev, u32 time)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_write(dev, SCI_KBD_ILLUM_STATUS, time);
	sci_close(dev);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to set KBD backlight status failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
}

static int toshiba_kbd_illum_status_get(struct toshiba_acpi_dev *dev, u32 *time)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_read(dev, SCI_KBD_ILLUM_STATUS, time);
	sci_close(dev);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to get KBD backlight status failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
}

static enum led_brightness toshiba_kbd_backlight_get(struct led_classdev *cdev)
{
	struct toshiba_acpi_dev *dev = container_of(cdev,
			struct toshiba_acpi_dev, kbd_led);
	u32 result;
	u32 state;

	/* Check the keyboard backlight state */
	result = hci_read(dev, HCI_KBD_ILLUMINATION, &state);
	if (result == TOS_FAILURE) {
		pr_err("ACPI call to get the keyboard backlight failed\n");
		return LED_OFF;
	} else if (result != TOS_SUCCESS) {
		return LED_OFF;
	}

	return state ? LED_FULL : LED_OFF;
}

static void toshiba_kbd_backlight_set(struct led_classdev *cdev,
				     enum led_brightness brightness)
{
	struct toshiba_acpi_dev *dev = container_of(cdev,
			struct toshiba_acpi_dev, kbd_led);
	u32 result;
	u32 state;

	/* Set the keyboard backlight state */
	state = brightness ? 1 : 0;
	result = hci_write(dev, HCI_KBD_ILLUMINATION, state);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to set KBD Illumination mode failed\n");
}

/* TouchPad support */
static int toshiba_touchpad_set(struct toshiba_acpi_dev *dev, u32 state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_write(dev, SCI_TOUCHPAD, state);
	sci_close(dev);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to set the touchpad failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
}

static int toshiba_touchpad_get(struct toshiba_acpi_dev *dev, u32 *state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_read(dev, SCI_TOUCHPAD, state);
	sci_close(dev);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to query the touchpad failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
}

/* Eco Mode support */
static void toshiba_eco_mode_available(struct toshiba_acpi_dev *dev)
{
	u32 in[TCI_WORDS] = { HCI_GET, HCI_ECO_MODE, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	dev->eco_supported = 0;
	dev->eco_led_registered = false;

	status = tci_raw(dev, in, out);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to get ECO led failed\n");
		return;
	}

	if (out[0] == TOS_INPUT_DATA_ERROR) {
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
		if (ACPI_FAILURE(status)) {
			pr_err("ACPI call to get ECO led failed\n");
			return;
		}

		if (out[0] != TOS_SUCCESS)
			return;

		dev->eco_supported = 1;
	}
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
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to get ECO led failed\n");
		return LED_OFF;
	}

	if (out[0] != TOS_SUCCESS)
		return LED_OFF;

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
	if (ACPI_FAILURE(status))
		pr_err("ACPI call to set ECO led failed\n");
}

/* Accelerometer support */
static void toshiba_accelerometer_available(struct toshiba_acpi_dev *dev)
{
	u32 in[TCI_WORDS] = { HCI_GET, HCI_ACCELEROMETER2, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	dev->accelerometer_supported = 0;

	/*
	 * Check if the accelerometer call exists,
	 * this call also serves as initialization
	 */
	status = tci_raw(dev, in, out);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to query the accelerometer failed\n");
		return;
	}

	if (out[0] != TOS_SUCCESS)
		return;

	dev->accelerometer_supported = 1;
}

static int toshiba_accelerometer_get(struct toshiba_acpi_dev *dev,
				     u32 *xy, u32 *z)
{
	u32 in[TCI_WORDS] = { HCI_GET, HCI_ACCELEROMETER, 0, 1, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	/* Check the Accelerometer status */
	status = tci_raw(dev, in, out);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to query the accelerometer failed\n");
		return -EIO;
	}

	if (out[0] == TOS_NOT_SUPPORTED)
		return -ENODEV;

	if (out[0] != TOS_SUCCESS)
		return -EIO;

	*xy = out[2];
	*z = out[4];

	return 0;
}

/* Sleep (Charge and Music) utilities support */
static void toshiba_usb_sleep_charge_available(struct toshiba_acpi_dev *dev)
{
	u32 in[TCI_WORDS] = { SCI_GET, SCI_USB_SLEEP_CHARGE, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	dev->usb_sleep_charge_supported = 0;

	if (!sci_open(dev))
		return;

	status = tci_raw(dev, in, out);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to get USB Sleep and Charge mode failed\n");
		sci_close(dev);
		return;
	}

	if (out[0] != TOS_SUCCESS) {
		sci_close(dev);
		return;
	}

	dev->usbsc_mode_base = out[4];

	in[5] = SCI_USB_CHARGE_BAT_LVL;
	status = tci_raw(dev, in, out);
	sci_close(dev);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to get USB Sleep and Charge mode failed\n");
		return;
	}

	if (out[0] != TOS_SUCCESS)
		return;

	dev->usbsc_bat_level = out[2];
	/* Flag as supported */
	dev->usb_sleep_charge_supported = 1;
}

static int toshiba_usb_sleep_charge_get(struct toshiba_acpi_dev *dev,
					u32 *mode)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_read(dev, SCI_USB_SLEEP_CHARGE, mode);
	sci_close(dev);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to set USB S&C mode failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
}

static int toshiba_usb_sleep_charge_set(struct toshiba_acpi_dev *dev,
					u32 mode)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_write(dev, SCI_USB_SLEEP_CHARGE, mode);
	sci_close(dev);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to set USB S&C mode failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
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
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to get USB S&C battery level failed\n");
		return -EIO;
	}

	if (out[0] == TOS_NOT_SUPPORTED)
		return -ENODEV;

	if (out[0] != TOS_SUCCESS)
		return -EIO;

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
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to set USB S&C battery level failed\n");
		return -EIO;
	}

	if (out[0] == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return out[0] == TOS_SUCCESS ? 0 : -EIO;
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
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to get USB Rapid Charge failed\n");
		return -EIO;
	}

	if (out[0] == TOS_NOT_SUPPORTED)
		return -ENODEV;

	if (out[0] != TOS_SUCCESS && out[0] != TOS_SUCCESS2)
		return -EIO;

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
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to set USB Rapid Charge failed\n");
		return -EIO;
	}

	if (out[0] == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return (out[0] == TOS_SUCCESS || out[0] == TOS_SUCCESS2) ? 0 : -EIO;
}

static int toshiba_usb_sleep_music_get(struct toshiba_acpi_dev *dev, u32 *state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_read(dev, SCI_USB_SLEEP_MUSIC, state);
	sci_close(dev);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to get Sleep and Music failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
}

static int toshiba_usb_sleep_music_set(struct toshiba_acpi_dev *dev, u32 state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_write(dev, SCI_USB_SLEEP_MUSIC, state);
	sci_close(dev);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to set Sleep and Music failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
}

/* Keyboard function keys */
static int toshiba_function_keys_get(struct toshiba_acpi_dev *dev, u32 *mode)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_read(dev, SCI_KBD_FUNCTION_KEYS, mode);
	sci_close(dev);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to get KBD function keys failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return (result == TOS_SUCCESS || result == TOS_SUCCESS2) ? 0 : -EIO;
}

static int toshiba_function_keys_set(struct toshiba_acpi_dev *dev, u32 mode)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_write(dev, SCI_KBD_FUNCTION_KEYS, mode);
	sci_close(dev);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to set KBD function keys failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return (result == TOS_SUCCESS || result == TOS_SUCCESS2) ? 0 : -EIO;
}

/* Panel Power ON */
static int toshiba_panel_power_on_get(struct toshiba_acpi_dev *dev, u32 *state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_read(dev, SCI_PANEL_POWER_ON, state);
	sci_close(dev);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to get Panel Power ON failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
}

static int toshiba_panel_power_on_set(struct toshiba_acpi_dev *dev, u32 state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_write(dev, SCI_PANEL_POWER_ON, state);
	sci_close(dev);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to set Panel Power ON failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
}

/* USB Three */
static int toshiba_usb_three_get(struct toshiba_acpi_dev *dev, u32 *state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_read(dev, SCI_USB_THREE, state);
	sci_close(dev);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to get USB 3 failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return (result == TOS_SUCCESS || result == TOS_SUCCESS2) ? 0 : -EIO;
}

static int toshiba_usb_three_set(struct toshiba_acpi_dev *dev, u32 state)
{
	u32 result;

	if (!sci_open(dev))
		return -EIO;

	result = sci_write(dev, SCI_USB_THREE, state);
	sci_close(dev);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to set USB 3 failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return (result == TOS_SUCCESS || result == TOS_SUCCESS2) ? 0 : -EIO;
}

/* Hotkey Event type */
static int toshiba_hotkey_event_type_get(struct toshiba_acpi_dev *dev,
					 u32 *type)
{
	u32 in[TCI_WORDS] = { HCI_GET, HCI_SYSTEM_INFO, 0x03, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	status = tci_raw(dev, in, out);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to get System type failed\n");
		return -EIO;
	}

	if (out[0] == TOS_NOT_SUPPORTED)
		return -ENODEV;

	if (out[0] != TOS_SUCCESS)
		return -EIO;

	*type = out[3];

	return 0;
}

/* Wireless status (RFKill, WLAN, BT, WWAN) */
static int toshiba_wireless_status(struct toshiba_acpi_dev *dev)
{
	u32 in[TCI_WORDS] = { HCI_GET, HCI_WIRELESS, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	in[3] = HCI_WIRELESS_STATUS;
	status = tci_raw(dev, in, out);

	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to get Wireless status failed\n");
		return -EIO;
	}

	if (out[0] == TOS_NOT_SUPPORTED)
		return -ENODEV;

	if (out[0] != TOS_SUCCESS)
		return -EIO;

	dev->killswitch = !!(out[2] & HCI_WIRELESS_STATUS);

	return 0;
}

/* WWAN */
static void toshiba_wwan_available(struct toshiba_acpi_dev *dev)
{
	u32 in[TCI_WORDS] = { HCI_GET, HCI_WIRELESS, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	dev->wwan_supported = 0;

	/*
	 * WWAN support can be queried by setting the in[3] value to
	 * HCI_WIRELESS_WWAN (0x03).
	 *
	 * If supported, out[0] contains TOS_SUCCESS and out[2] contains
	 * HCI_WIRELESS_WWAN_STATUS (0x2000).
	 *
	 * If not supported, out[0] contains TOS_INPUT_DATA_ERROR (0x8300)
	 * or TOS_NOT_SUPPORTED (0x8000).
	 */
	in[3] = HCI_WIRELESS_WWAN;
	status = tci_raw(dev, in, out);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to get WWAN status failed\n");
		return;
	}

	if (out[0] != TOS_SUCCESS)
		return;

	dev->wwan_supported = (out[2] == HCI_WIRELESS_WWAN_STATUS);
}

static int toshiba_wwan_set(struct toshiba_acpi_dev *dev, u32 state)
{
	u32 in[TCI_WORDS] = { HCI_SET, HCI_WIRELESS, state, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	in[3] = HCI_WIRELESS_WWAN_STATUS;
	status = tci_raw(dev, in, out);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to set WWAN status failed\n");
		return -EIO;
	}

	if (out[0] == TOS_NOT_SUPPORTED)
		return -ENODEV;

	if (out[0] != TOS_SUCCESS)
		return -EIO;

	/*
	 * Some devices only need to call HCI_WIRELESS_WWAN_STATUS to
	 * (de)activate the device, but some others need the
	 * HCI_WIRELESS_WWAN_POWER call as well.
	 */
	in[3] = HCI_WIRELESS_WWAN_POWER;
	status = tci_raw(dev, in, out);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to set WWAN power failed\n");
		return -EIO;
	}

	if (out[0] == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return out[0] == TOS_SUCCESS ? 0 : -EIO;
}

/* Cooling Method */
static void toshiba_cooling_method_available(struct toshiba_acpi_dev *dev)
{
	u32 in[TCI_WORDS] = { HCI_GET, HCI_COOLING_METHOD, 0, 0, 0, 0 };
	u32 out[TCI_WORDS];
	acpi_status status;

	dev->cooling_method_supported = 0;
	dev->max_cooling_method = 0;

	status = tci_raw(dev, in, out);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to get Cooling Method failed\n");
		return;
	}

	if (out[0] != TOS_SUCCESS && out[0] != TOS_SUCCESS2)
		return;

	dev->cooling_method_supported = 1;
	dev->max_cooling_method = out[3];
}

static int toshiba_cooling_method_get(struct toshiba_acpi_dev *dev, u32 *state)
{
	u32 result = hci_read(dev, HCI_COOLING_METHOD, state);

	if (result == TOS_FAILURE)
		pr_err("ACPI call to get Cooling Method failed\n");

	if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return (result == TOS_SUCCESS || result == TOS_SUCCESS2) ? 0 : -EIO;
}

static int toshiba_cooling_method_set(struct toshiba_acpi_dev *dev, u32 state)
{
	u32 result = hci_write(dev, HCI_COOLING_METHOD, state);

	if (result == TOS_FAILURE)
		pr_err("ACPI call to set Cooling Method failed\n");

	if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return (result == TOS_SUCCESS || result == TOS_SUCCESS2) ? 0 : -EIO;
}

/* Transflective Backlight */
static int get_tr_backlight_status(struct toshiba_acpi_dev *dev, u32 *status)
{
	u32 result = hci_read(dev, HCI_TR_BACKLIGHT, status);

	if (result == TOS_FAILURE)
		pr_err("ACPI call to get Transflective Backlight failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
}

static int set_tr_backlight_status(struct toshiba_acpi_dev *dev, u32 status)
{
	u32 result = hci_write(dev, HCI_TR_BACKLIGHT, !status);

	if (result == TOS_FAILURE)
		pr_err("ACPI call to set Transflective Backlight failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
}

static struct proc_dir_entry *toshiba_proc_dir;

/* LCD Brightness */
static int __get_lcd_brightness(struct toshiba_acpi_dev *dev)
{
	int brightness = 0;
	u32 result;
	u32 value;

	if (dev->tr_backlight_supported) {
		int ret = get_tr_backlight_status(dev, &value);

		if (ret)
			return ret;
		if (value)
			return 0;
		brightness++;
	}

	result = hci_read(dev, HCI_LCD_BRIGHTNESS, &value);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to get LCD Brightness failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ?
			brightness + (value >> HCI_LCD_BRIGHTNESS_SHIFT) :
			-EIO;
}

static int get_lcd_brightness(struct backlight_device *bd)
{
	struct toshiba_acpi_dev *dev = bl_get_data(bd);

	return __get_lcd_brightness(dev);
}

static int lcd_proc_show(struct seq_file *m, void *v)
{
	struct toshiba_acpi_dev *dev = m->private;
	int levels;
	int value;

	if (!dev->backlight_dev)
		return -ENODEV;

	levels = dev->backlight_dev->props.max_brightness + 1;
	value = get_lcd_brightness(dev->backlight_dev);
	if (value < 0) {
		pr_err("Error reading LCD brightness\n");
		return value;
	}

	seq_printf(m, "brightness:              %d\n", value);
	seq_printf(m, "brightness_levels:       %d\n", levels);

	return 0;
}

static int lcd_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, lcd_proc_show, PDE_DATA(inode));
}

static int set_lcd_brightness(struct toshiba_acpi_dev *dev, int value)
{
	u32 result;

	if (dev->tr_backlight_supported) {
		int ret = set_tr_backlight_status(dev, !value);

		if (ret)
			return ret;
		if (value)
			value--;
	}

	value = value << HCI_LCD_BRIGHTNESS_SHIFT;
	result = hci_write(dev, HCI_LCD_BRIGHTNESS, value);
	if (result == TOS_FAILURE)
		pr_err("ACPI call to set LCD Brightness failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
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
	int levels;
	int value;

	len = min(count, sizeof(cmd) - 1);
	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = '\0';

	levels = dev->backlight_dev->props.max_brightness + 1;
	if (sscanf(cmd, " brightness : %i", &value) != 1 &&
	    value < 0 && value > levels)
		return -EINVAL;

	if (set_lcd_brightness(dev, value))
		return -EIO;

	return count;
}

static const struct file_operations lcd_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= lcd_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= lcd_proc_write,
};

/* Video-Out */
static int get_video_status(struct toshiba_acpi_dev *dev, u32 *status)
{
	u32 result = hci_read(dev, HCI_VIDEO_OUT, status);

	if (result == TOS_FAILURE)
		pr_err("ACPI call to get Video-Out failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
}

static int video_proc_show(struct seq_file *m, void *v)
{
	struct toshiba_acpi_dev *dev = m->private;
	int is_lcd, is_crt, is_tv;
	u32 value;

	if (get_video_status(dev, &value))
		return -EIO;

	is_lcd = (value & HCI_VIDEO_OUT_LCD) ? 1 : 0;
	is_crt = (value & HCI_VIDEO_OUT_CRT) ? 1 : 0;
	is_tv = (value & HCI_VIDEO_OUT_TV) ? 1 : 0;

	seq_printf(m, "lcd_out:                 %d\n", is_lcd);
	seq_printf(m, "crt_out:                 %d\n", is_crt);
	seq_printf(m, "tv_out:                  %d\n", is_tv);

	return 0;
}

static int video_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, video_proc_show, PDE_DATA(inode));
}

static ssize_t video_proc_write(struct file *file, const char __user *buf,
				size_t count, loff_t *pos)
{
	struct toshiba_acpi_dev *dev = PDE_DATA(file_inode(file));
	char *buffer;
	char *cmd;
	int lcd_out, crt_out, tv_out;
	int remain = count;
	int value;
	int ret;
	u32 video_out;

	cmd = memdup_user_nul(buf, count);
	if (IS_ERR(cmd))
		return PTR_ERR(cmd);

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

	lcd_out = crt_out = tv_out = -1;
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
		 * video setting if something changed.
		 */
		if (new_video_out != video_out)
			ret = write_acpi_int(METHOD_VIDEO_OUT, new_video_out);
	}

	return ret ? -EIO : count;
}

static const struct file_operations video_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= video_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= video_proc_write,
};

/* Fan status */
static int get_fan_status(struct toshiba_acpi_dev *dev, u32 *status)
{
	u32 result = hci_read(dev, HCI_FAN, status);

	if (result == TOS_FAILURE)
		pr_err("ACPI call to get Fan status failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
}

static int set_fan_status(struct toshiba_acpi_dev *dev, u32 status)
{
	u32 result = hci_write(dev, HCI_FAN, status);

	if (result == TOS_FAILURE)
		pr_err("ACPI call to set Fan status failed\n");
	else if (result == TOS_NOT_SUPPORTED)
		return -ENODEV;

	return result == TOS_SUCCESS ? 0 : -EIO;
}

static int fan_proc_show(struct seq_file *m, void *v)
{
	struct toshiba_acpi_dev *dev = m->private;
	u32 value;

	if (get_fan_status(dev, &value))
		return -EIO;

	seq_printf(m, "running:                 %d\n", (value > 0));
	seq_printf(m, "force_on:                %d\n", dev->force_fan);

	return 0;
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

	len = min(count, sizeof(cmd) - 1);
	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = '\0';

	if (sscanf(cmd, " force_on : %i", &value) != 1 &&
	    value != 0 && value != 1)
		return -EINVAL;

	if (set_fan_status(dev, value))
		return -EIO;

	dev->force_fan = value;

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

/* Keyboard backlight work */
static void toshiba_acpi_kbd_bl_work(struct work_struct *work);

static DECLARE_WORK(kbd_bl_work, toshiba_acpi_kbd_bl_work);

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
	int state;
	int ret;

	ret = kstrtoint(buf, 0, &state);
	if (ret)
		return ret;

	if (state != 0 && state != 1)
		return -EINVAL;

	ret = set_fan_status(toshiba, state);
	if (ret)
		return ret;

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
		int time = toshiba->kbd_time << HCI_MISC_SHIFT;

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

		/*
		 * Some laptop models with the second generation backlit
		 * keyboard (type 2) do not generate the keyboard backlight
		 * changed event (0x92), and thus, the driver will never update
		 * the sysfs entries.
		 *
		 * The event is generated right when changing the keyboard
		 * backlight mode and the *notify function will set the
		 * kbd_event_generated to true.
		 *
		 * In case the event is not generated, schedule the keyboard
		 * backlight work to update the sysfs entries and emulate the
		 * event via genetlink.
		 */
		if (toshiba->kbd_type == 2 &&
		    !toshiba_acpi->kbd_event_generated)
			schedule_work(&kbd_bl_work);
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
		return sprintf(buf, "0x%x 0x%x\n",
			       SCI_KBD_MODE_FNZ, SCI_KBD_MODE_AUTO);

	return sprintf(buf, "0x%x 0x%x 0x%x\n",
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
	int state;
	u32 mode;
	int ret;

	ret = kstrtoint(buf, 0, &state);
	if (ret)
		return ret;
	/*
	 * Check for supported values, where:
	 * 0 - Disabled
	 * 1 - Alternate (Non USB conformant devices that require more power)
	 * 2 - Auto (USB conformant devices)
	 * 3 - Typical
	 */
	if (state != 0 && state != 1 && state != 2 && state != 3)
		return -EINVAL;

	/* Set the USB charging mode to internal value */
	mode = toshiba->usbsc_mode_base;
	if (state == 0)
		mode |= SCI_USB_CHARGE_DISABLED;
	else if (state == 1)
		mode |= SCI_USB_CHARGE_ALTERNATE;
	else if (state == 2)
		mode |= SCI_USB_CHARGE_AUTO;
	else if (state == 3)
		mode |= SCI_USB_CHARGE_TYPICAL;

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
	int bat_lvl, status;
	u32 state;
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

static ssize_t cooling_method_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct toshiba_acpi_dev *toshiba = dev_get_drvdata(dev);
	int state;
	int ret;

	ret = toshiba_cooling_method_get(toshiba, &state);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d %d\n", state, toshiba->max_cooling_method);
}

static ssize_t cooling_method_store(struct device *dev,
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
	 * Check for supported values
	 * Depending on the laptop model, some only support these two:
	 * 0 - Maximum Performance
	 * 1 - Battery Optimized
	 *
	 * While some others support all three methods:
	 * 0 - Maximum Performance
	 * 1 - Performance
	 * 2 - Battery Optimized
	 */
	if (state < 0 || state > toshiba->max_cooling_method)
		return -EINVAL;

	ret = toshiba_cooling_method_set(toshiba, state);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(cooling_method);

static struct attribute *toshiba_attributes[] = {
	&dev_attr_version.attr,
	&dev_attr_fan.attr,
	&dev_attr_kbd_backlight_mode.attr,
	&dev_attr_kbd_type.attr,
	&dev_attr_available_kbd_modes.attr,
	&dev_attr_kbd_backlight_timeout.attr,
	&dev_attr_touchpad.attr,
	&dev_attr_usb_sleep_charge.attr,
	&dev_attr_sleep_functions_on_battery.attr,
	&dev_attr_usb_rapid_charge.attr,
	&dev_attr_usb_sleep_music.attr,
	&dev_attr_kbd_function_keys.attr,
	&dev_attr_panel_power_on.attr,
	&dev_attr_usb_three.attr,
	&dev_attr_cooling_method.attr,
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
	else if (attr == &dev_attr_cooling_method.attr)
		exists = (drv->cooling_method_supported) ? true : false;

	return exists ? attr->mode : 0;
}

static struct attribute_group toshiba_attr_group = {
	.is_visible = toshiba_sysfs_is_visible,
	.attrs = toshiba_attributes,
};

static void toshiba_acpi_kbd_bl_work(struct work_struct *work)
{
	struct acpi_device *acpi_dev = toshiba_acpi->acpi_dev;

	/* Update the sysfs entries */
	if (sysfs_update_group(&acpi_dev->dev.kobj,
			       &toshiba_attr_group))
		pr_err("Unable to update sysfs entries\n");

	/* Emulate the keyboard backlight event */
	acpi_bus_generate_netlink_event(acpi_dev->pnp.device_class,
					dev_name(&acpi_dev->dev),
					0x92, 0);
}

/*
 * IIO device
 */

enum toshiba_iio_accel_chan {
	AXIS_X,
	AXIS_Y,
	AXIS_Z
};

static int toshiba_iio_accel_get_axis(enum toshiba_iio_accel_chan chan)
{
	u32 xyval, zval;
	int ret;

	ret = toshiba_accelerometer_get(toshiba_acpi, &xyval, &zval);
	if (ret < 0)
		return ret;

	switch (chan) {
	case AXIS_X:
		return xyval & HCI_ACCEL_DIRECTION_MASK ?
			-(xyval & HCI_ACCEL_MASK) : xyval & HCI_ACCEL_MASK;
	case AXIS_Y:
		return (xyval >> HCI_MISC_SHIFT) & HCI_ACCEL_DIRECTION_MASK ?
			-((xyval >> HCI_MISC_SHIFT) & HCI_ACCEL_MASK) :
			(xyval >> HCI_MISC_SHIFT) & HCI_ACCEL_MASK;
	case AXIS_Z:
		return zval & HCI_ACCEL_DIRECTION_MASK ?
			-(zval & HCI_ACCEL_MASK) : zval & HCI_ACCEL_MASK;
	}

	return ret;
}

static int toshiba_iio_accel_read_raw(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      int *val, int *val2, long mask)
{
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = toshiba_iio_accel_get_axis(chan->channel);
		if (ret == -EIO || ret == -ENODEV)
			return ret;

		*val = ret;

		return IIO_VAL_INT;
	}

	return -EINVAL;
}

#define TOSHIBA_IIO_ACCEL_CHANNEL(axis, chan) { \
	.type = IIO_ACCEL, \
	.modified = 1, \
	.channel = chan, \
	.channel2 = IIO_MOD_##axis, \
	.output = 1, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
}

static const struct iio_chan_spec toshiba_iio_accel_channels[] = {
	TOSHIBA_IIO_ACCEL_CHANNEL(X, AXIS_X),
	TOSHIBA_IIO_ACCEL_CHANNEL(Y, AXIS_Y),
	TOSHIBA_IIO_ACCEL_CHANNEL(Z, AXIS_Z),
};

static const struct iio_info toshiba_iio_accel_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &toshiba_iio_accel_read_raw,
};

/*
 * Misc device
 */
static int toshiba_acpi_smm_bridge(SMMRegisters *regs)
{
	u32 in[TCI_WORDS] = { regs->eax, regs->ebx, regs->ecx,
			      regs->edx, regs->esi, regs->edi };
	u32 out[TCI_WORDS];
	acpi_status status;

	status = tci_raw(toshiba_acpi, in, out);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to query SMM registers failed\n");
		return -EIO;
	}

	/* Fillout the SMM struct with the TCI call results */
	regs->eax = out[0];
	regs->ebx = out[1];
	regs->ecx = out[2];
	regs->edx = out[3];
	regs->esi = out[4];
	regs->edi = out[5];

	return 0;
}

static long toshiba_acpi_ioctl(struct file *fp, unsigned int cmd,
			       unsigned long arg)
{
	SMMRegisters __user *argp = (SMMRegisters __user *)arg;
	SMMRegisters regs;
	int ret;

	if (!argp)
		return -EINVAL;

	switch (cmd) {
	case TOSH_SMM:
		if (copy_from_user(&regs, argp, sizeof(SMMRegisters)))
			return -EFAULT;
		ret = toshiba_acpi_smm_bridge(&regs);
		if (ret)
			return ret;
		if (copy_to_user(argp, &regs, sizeof(SMMRegisters)))
			return -EFAULT;
		break;
	case TOSHIBA_ACPI_SCI:
		if (copy_from_user(&regs, argp, sizeof(SMMRegisters)))
			return -EFAULT;
		/* Ensure we are being called with a SCI_{GET, SET} register */
		if (regs.eax != SCI_GET && regs.eax != SCI_SET)
			return -EINVAL;
		if (!sci_open(toshiba_acpi))
			return -EIO;
		ret = toshiba_acpi_smm_bridge(&regs);
		sci_close(toshiba_acpi);
		if (ret)
			return ret;
		if (copy_to_user(argp, &regs, sizeof(SMMRegisters)))
			return -EFAULT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations toshiba_acpi_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl = toshiba_acpi_ioctl,
	.llseek		= noop_llseek,
};

/*
 * WWAN RFKill handlers
 */
static int toshiba_acpi_wwan_set_block(void *data, bool blocked)
{
	struct toshiba_acpi_dev *dev = data;
	int ret;

	ret = toshiba_wireless_status(dev);
	if (ret)
		return ret;

	if (!dev->killswitch)
		return 0;

	return toshiba_wwan_set(dev, !blocked);
}

static void toshiba_acpi_wwan_poll(struct rfkill *rfkill, void *data)
{
	struct toshiba_acpi_dev *dev = data;

	if (toshiba_wireless_status(dev))
		return;

	rfkill_set_hw_state(dev->wwan_rfk, !dev->killswitch);
}

static const struct rfkill_ops wwan_rfk_ops = {
	.set_block = toshiba_acpi_wwan_set_block,
	.poll = toshiba_acpi_wwan_poll,
};

static int toshiba_acpi_setup_wwan_rfkill(struct toshiba_acpi_dev *dev)
{
	int ret = toshiba_wireless_status(dev);

	if (ret)
		return ret;

	dev->wwan_rfk = rfkill_alloc("Toshiba WWAN",
				     &dev->acpi_dev->dev,
				     RFKILL_TYPE_WWAN,
				     &wwan_rfk_ops,
				     dev);
	if (!dev->wwan_rfk) {
		pr_err("Unable to allocate WWAN rfkill device\n");
		return -ENOMEM;
	}

	rfkill_set_hw_state(dev->wwan_rfk, !dev->killswitch);

	ret = rfkill_register(dev->wwan_rfk);
	if (ret) {
		pr_err("Unable to register WWAN rfkill device\n");
		rfkill_destroy(dev->wwan_rfk);
	}

	return ret;
}

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

	/*
	 * Enable the "Special Functions" mode only if they are
	 * supported and if they are activated.
	 */
	if (dev->kbd_function_keys_supported && dev->special_functions)
		result = hci_write(dev, HCI_HOTKEY_EVENT,
				   HCI_HOTKEY_SPECIAL_FUNCTIONS);
	else
		result = hci_write(dev, HCI_HOTKEY_EVENT, HCI_HOTKEY_ENABLE);

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
	if (dev->info_supported) {
		int scancode = toshiba_acpi_query_hotkey(dev);

		if (scancode < 0) {
			pr_err("Failed to query hotkey event\n");
		} else if (scancode != 0) {
			toshiba_acpi_report_hotkey(dev, scancode);
			dev->key_event_valid = 1;
			dev->last_key_event = scancode;
		}
	} else if (dev->system_event_supported) {
		u32 result;
		u32 value;
		int retries = 3;

		do {
			result = hci_read(dev, HCI_SYSTEM_EVENT, &value);
			switch (result) {
			case TOS_SUCCESS:
				toshiba_acpi_report_hotkey(dev, (int)value);
				dev->key_event_valid = 1;
				dev->last_key_event = value;
				break;
			case TOS_NOT_SUPPORTED:
				/*
				 * This is a workaround for an unresolved
				 * issue on some machines where system events
				 * sporadically become disabled.
				 */
				result = hci_write(dev, HCI_SYSTEM_EVENT, 1);
				if (result == TOS_SUCCESS)
					pr_notice("Re-enabled hotkeys\n");
				/* Fall through */
			default:
				retries--;
				break;
			}
		} while (retries && result != TOS_FIFO_EMPTY);
	}
}

static int toshiba_acpi_setup_keyboard(struct toshiba_acpi_dev *dev)
{
	const struct key_entry *keymap = toshiba_acpi_keymap;
	acpi_handle ec_handle;
	int error;

	if (disable_hotkeys) {
		pr_info("Hotkeys disabled by module parameter\n");
		return 0;
	}

	if (wmi_has_guid(TOSHIBA_WMI_EVENT_GUID)) {
		pr_info("WMI event detected, hotkeys will not be monitored\n");
		return 0;
	}

	error = toshiba_acpi_enable_hotkeys(dev);
	if (error)
		return error;

	if (toshiba_hotkey_event_type_get(dev, &dev->hotkey_event_type))
		pr_notice("Unable to query Hotkey Event Type\n");

	dev->hotkey_dev = input_allocate_device();
	if (!dev->hotkey_dev)
		return -ENOMEM;

	dev->hotkey_dev->name = "Toshiba input device";
	dev->hotkey_dev->phys = "toshiba_acpi/input0";
	dev->hotkey_dev->id.bustype = BUS_HOST;

	if (dev->hotkey_event_type == HCI_SYSTEM_TYPE1 ||
	    !dev->kbd_function_keys_supported)
		keymap = toshiba_acpi_keymap;
	else if (dev->hotkey_event_type == HCI_SYSTEM_TYPE2 ||
		 dev->kbd_function_keys_supported)
		keymap = toshiba_acpi_alt_keymap;
	else
		pr_info("Unknown event type received %x\n",
			dev->hotkey_event_type);
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
			goto err_free_dev;
		}

		dev->ntfy_supported = 1;
	}

	/*
	 * Determine hotkey query interface. Prefer using the INFO
	 * method when it is available.
	 */
	if (acpi_has_method(dev->acpi_dev->handle, "INFO"))
		dev->info_supported = 1;
	else if (hci_write(dev, HCI_SYSTEM_EVENT, 1) == TOS_SUCCESS)
		dev->system_event_supported = 1;

	if (!dev->info_supported && !dev->system_event_supported) {
		pr_warn("No hotkey query interface found\n");
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

	/*
	 * Some machines don't support the backlight methods at all, and
	 * others support it read-only. Either of these is pretty useless,
	 * so only register the backlight device if the backlight method
	 * supports both reads and writes.
	 */
	brightness = __get_lcd_brightness(dev);
	if (brightness < 0)
		return 0;
	/*
	 * If transflective backlight is supported and the brightness is zero
	 * (lowest brightness level), the set_lcd_brightness function will
	 * activate the transflective backlight, making the LCD appear to be
	 * turned off, simply increment the brightness level to avoid that.
	 */
	if (dev->tr_backlight_supported && brightness == 0)
		brightness++;
	ret = set_lcd_brightness(dev, brightness);
	if (ret) {
		pr_debug("Backlight method is read-only, disabling backlight support\n");
		return 0;
	}

	/*
	 * Tell acpi-video-detect code to prefer vendor backlight on all
	 * systems with transflective backlight and on dmi matched systems.
	 */
	if (dev->tr_backlight_supported ||
	    dmi_check_system(toshiba_vendor_backlight_dmi))
		acpi_video_set_dmi_backlight_type(acpi_backlight_vendor);

	if (acpi_video_get_backlight_type() != acpi_backlight_vendor)
		return 0;

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

static void print_supported_features(struct toshiba_acpi_dev *dev)
{
	pr_info("Supported laptop features:");

	if (dev->hotkey_dev)
		pr_cont(" hotkeys");
	if (dev->backlight_dev)
		pr_cont(" backlight");
	if (dev->video_supported)
		pr_cont(" video-out");
	if (dev->fan_supported)
		pr_cont(" fan");
	if (dev->tr_backlight_supported)
		pr_cont(" transflective-backlight");
	if (dev->illumination_supported)
		pr_cont(" illumination");
	if (dev->kbd_illum_supported)
		pr_cont(" keyboard-backlight");
	if (dev->touchpad_supported)
		pr_cont(" touchpad");
	if (dev->eco_supported)
		pr_cont(" eco-led");
	if (dev->accelerometer_supported)
		pr_cont(" accelerometer-axes");
	if (dev->usb_sleep_charge_supported)
		pr_cont(" usb-sleep-charge");
	if (dev->usb_rapid_charge_supported)
		pr_cont(" usb-rapid-charge");
	if (dev->usb_sleep_music_supported)
		pr_cont(" usb-sleep-music");
	if (dev->kbd_function_keys_supported)
		pr_cont(" special-function-keys");
	if (dev->panel_power_on_supported)
		pr_cont(" panel-power-on");
	if (dev->usb_three_supported)
		pr_cont(" usb3");
	if (dev->wwan_supported)
		pr_cont(" wwan");
	if (dev->cooling_method_supported)
		pr_cont(" cooling-method");

	pr_cont("\n");
}

static int toshiba_acpi_remove(struct acpi_device *acpi_dev)
{
	struct toshiba_acpi_dev *dev = acpi_driver_data(acpi_dev);

	misc_deregister(&dev->miscdev);

	remove_toshiba_proc_entries(dev);

	if (dev->accelerometer_supported && dev->indio_dev) {
		iio_device_unregister(dev->indio_dev);
		iio_device_free(dev->indio_dev);
	}

	if (dev->sysfs_created)
		sysfs_remove_group(&dev->acpi_dev->dev.kobj,
				   &toshiba_attr_group);

	if (dev->ntfy_supported) {
		i8042_remove_filter(toshiba_acpi_i8042_filter);
		cancel_work_sync(&dev->hotkey_work);
	}

	if (dev->hotkey_dev)
		input_unregister_device(dev->hotkey_dev);

	backlight_device_unregister(dev->backlight_dev);

	if (dev->illumination_led_registered)
		led_classdev_unregister(&dev->led_dev);

	if (dev->kbd_led_registered)
		led_classdev_unregister(&dev->kbd_led);

	if (dev->eco_led_registered)
		led_classdev_unregister(&dev->eco_led);

	if (dev->wwan_rfk) {
		rfkill_unregister(dev->wwan_rfk);
		rfkill_destroy(dev->wwan_rfk);
	}

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
	dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	dev->miscdev.name = "toshiba_acpi";
	dev->miscdev.fops = &toshiba_acpi_fops;

	ret = misc_register(&dev->miscdev);
	if (ret) {
		pr_err("Failed to register miscdevice\n");
		kfree(dev);
		return ret;
	}

	acpi_dev->driver_data = dev;
	dev_set_drvdata(&acpi_dev->dev, dev);

	/* Query the BIOS for supported features */

	/*
	 * The "Special Functions" are always supported by the laptops
	 * with the new keyboard layout, query for its presence to help
	 * determine the keymap layout to use.
	 */
	ret = toshiba_function_keys_get(dev, &dev->special_functions);
	dev->kbd_function_keys_supported = !ret;

	dev->hotkey_event_type = 0;
	if (toshiba_acpi_setup_keyboard(dev))
		pr_info("Unable to activate hotkeys\n");

	/* Determine whether or not BIOS supports transflective backlight */
	ret = get_tr_backlight_status(dev, &dummy);
	dev->tr_backlight_supported = !ret;

	ret = toshiba_acpi_setup_backlight(dev);
	if (ret)
		goto error;

	toshiba_illumination_available(dev);
	if (dev->illumination_supported) {
		dev->led_dev.name = "toshiba::illumination";
		dev->led_dev.max_brightness = 1;
		dev->led_dev.brightness_set = toshiba_illumination_set;
		dev->led_dev.brightness_get = toshiba_illumination_get;
		if (!led_classdev_register(&acpi_dev->dev, &dev->led_dev))
			dev->illumination_led_registered = true;
	}

	toshiba_eco_mode_available(dev);
	if (dev->eco_supported) {
		dev->eco_led.name = "toshiba::eco_mode";
		dev->eco_led.max_brightness = 1;
		dev->eco_led.brightness_set = toshiba_eco_mode_set_status;
		dev->eco_led.brightness_get = toshiba_eco_mode_get_status;
		if (!led_classdev_register(&dev->acpi_dev->dev, &dev->eco_led))
			dev->eco_led_registered = true;
	}

	toshiba_kbd_illum_available(dev);
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
			dev->kbd_led_registered = true;
	}

	ret = toshiba_touchpad_get(dev, &dummy);
	dev->touchpad_supported = !ret;

	toshiba_accelerometer_available(dev);
	if (dev->accelerometer_supported) {
		dev->indio_dev = iio_device_alloc(sizeof(*dev));
		if (!dev->indio_dev) {
			pr_err("Unable to allocate iio device\n");
			goto iio_error;
		}

		pr_info("Registering Toshiba accelerometer iio device\n");

		dev->indio_dev->info = &toshiba_iio_accel_info;
		dev->indio_dev->name = "Toshiba accelerometer";
		dev->indio_dev->dev.parent = &acpi_dev->dev;
		dev->indio_dev->modes = INDIO_DIRECT_MODE;
		dev->indio_dev->channels = toshiba_iio_accel_channels;
		dev->indio_dev->num_channels =
					ARRAY_SIZE(toshiba_iio_accel_channels);

		ret = iio_device_register(dev->indio_dev);
		if (ret < 0) {
			pr_err("Unable to register iio device\n");
			iio_device_free(dev->indio_dev);
		}
	}
iio_error:

	toshiba_usb_sleep_charge_available(dev);

	ret = toshiba_usb_rapid_charge_get(dev, &dummy);
	dev->usb_rapid_charge_supported = !ret;

	ret = toshiba_usb_sleep_music_get(dev, &dummy);
	dev->usb_sleep_music_supported = !ret;

	ret = toshiba_panel_power_on_get(dev, &dummy);
	dev->panel_power_on_supported = !ret;

	ret = toshiba_usb_three_get(dev, &dummy);
	dev->usb_three_supported = !ret;

	ret = get_video_status(dev, &dummy);
	dev->video_supported = !ret;

	ret = get_fan_status(dev, &dummy);
	dev->fan_supported = !ret;

	toshiba_wwan_available(dev);
	if (dev->wwan_supported)
		toshiba_acpi_setup_wwan_rfkill(dev);

	toshiba_cooling_method_available(dev);

	print_supported_features(dev);

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

	switch (event) {
	case 0x80: /* Hotkeys and some system events */
		/*
		 * Machines with this WMI GUID aren't supported due to bugs in
		 * their AML.
		 *
		 * Return silently to avoid triggering a netlink event.
		 */
		if (wmi_has_guid(TOSHIBA_WMI_EVENT_GUID))
			return;
		toshiba_acpi_process_hotkeys(dev);
		break;
	case 0x81: /* Dock events */
	case 0x82:
	case 0x83:
		pr_info("Dock event received %x\n", event);
		break;
	case 0x88: /* Thermal events */
		pr_info("Thermal event received\n");
		break;
	case 0x8f: /* LID closed */
	case 0x90: /* LID is closed and Dock has been ejected */
		break;
	case 0x8c: /* SATA power events */
	case 0x8b:
		pr_info("SATA power event received %x\n", event);
		break;
	case 0x92: /* Keyboard backlight mode changed */
		toshiba_acpi->kbd_event_generated = true;
		/* Update sysfs entries */
		if (sysfs_update_group(&acpi_dev->dev.kobj,
				       &toshiba_attr_group))
			pr_err("Unable to update sysfs entries\n");
		break;
	case 0x85: /* Unknown */
	case 0x8d: /* Unknown */
	case 0x8e: /* Unknown */
	case 0x94: /* Unknown */
	case 0x95: /* Unknown */
	default:
		pr_info("Unknown event received %x\n", event);
		break;
	}

	acpi_bus_generate_netlink_event(acpi_dev->pnp.device_class,
					dev_name(&acpi_dev->dev),
					event, (event == 0x80) ?
					dev->last_key_event : 0);
}

#ifdef CONFIG_PM_SLEEP
static int toshiba_acpi_suspend(struct device *device)
{
	struct toshiba_acpi_dev *dev = acpi_driver_data(to_acpi_device(device));

	if (dev->hotkey_dev) {
		u32 result;

		result = hci_write(dev, HCI_HOTKEY_EVENT, HCI_HOTKEY_DISABLE);
		if (result != TOS_SUCCESS)
			pr_info("Unable to disable hotkeys\n");
	}

	return 0;
}

static int toshiba_acpi_resume(struct device *device)
{
	struct toshiba_acpi_dev *dev = acpi_driver_data(to_acpi_device(device));

	if (dev->hotkey_dev) {
		if (toshiba_acpi_enable_hotkeys(dev))
			pr_info("Unable to re-enable hotkeys\n");
	}

	if (dev->wwan_rfk) {
		if (!toshiba_wireless_status(dev))
			rfkill_set_hw_state(dev->wwan_rfk, !dev->killswitch);
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
