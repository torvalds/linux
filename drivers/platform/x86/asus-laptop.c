/*
 *  asus-laptop.c - Asus Laptop Support
 *
 *
 *  Copyright (C) 2002-2005 Julien Lerouge, 2003-2006 Karol Kozimor
 *  Copyright (C) 2006-2007 Corentin Chary
 *  Copyright (C) 2011 Wind River Systems
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
 *
 *
 *  The development page for this driver is located at
 *  http://sourceforge.net/projects/acpi4asus/
 *
 *  Credits:
 *  Pontus Fuchs   - Helper functions, cleanup
 *  Johann Wiesner - Small compile fixes
 *  John Belmonte  - ACPI code for Toshiba laptop was a good starting point.
 *  Eric Burghard  - LED display support for W1N
 *  Josh Green     - Light Sens support
 *  Thomas Tuttle  - His first patch for led support was very helpful
 *  Sam Lin        - GPS support
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/proc_fs.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/input-polldev.h>
#include <linux/rfkill.h>
#include <linux/slab.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <acpi/video.h>

#define ASUS_LAPTOP_VERSION	"0.42"

#define ASUS_LAPTOP_NAME	"Asus Laptop Support"
#define ASUS_LAPTOP_CLASS	"hotkey"
#define ASUS_LAPTOP_DEVICE_NAME	"Hotkey"
#define ASUS_LAPTOP_FILE	KBUILD_MODNAME
#define ASUS_LAPTOP_PREFIX	"\\_SB.ATKD."

MODULE_AUTHOR("Julien Lerouge, Karol Kozimor, Corentin Chary");
MODULE_DESCRIPTION(ASUS_LAPTOP_NAME);
MODULE_LICENSE("GPL");

/*
 * WAPF defines the behavior of the Fn+Fx wlan key
 * The significance of values is yet to be found, but
 * most of the time:
 * Bit | Bluetooth | WLAN
 *  0  | Hardware  | Hardware
 *  1  | Hardware  | Software
 *  4  | Software  | Software
 */
static uint wapf = 1;
module_param(wapf, uint, 0444);
MODULE_PARM_DESC(wapf, "WAPF value");

static char *wled_type = "unknown";
static char *bled_type = "unknown";

module_param(wled_type, charp, 0444);
MODULE_PARM_DESC(wled_type, "Set the wled type on boot "
		 "(unknown, led or rfkill). "
		 "default is unknown");

module_param(bled_type, charp, 0444);
MODULE_PARM_DESC(bled_type, "Set the bled type on boot "
		 "(unknown, led or rfkill). "
		 "default is unknown");

static int wlan_status = 1;
static int bluetooth_status = 1;
static int wimax_status = -1;
static int wwan_status = -1;
static int als_status;

module_param(wlan_status, int, 0444);
MODULE_PARM_DESC(wlan_status, "Set the wireless status on boot "
		 "(0 = disabled, 1 = enabled, -1 = don't do anything). "
		 "default is -1");

module_param(bluetooth_status, int, 0444);
MODULE_PARM_DESC(bluetooth_status, "Set the wireless status on boot "
		 "(0 = disabled, 1 = enabled, -1 = don't do anything). "
		 "default is -1");

module_param(wimax_status, int, 0444);
MODULE_PARM_DESC(wimax_status, "Set the wireless status on boot "
		 "(0 = disabled, 1 = enabled, -1 = don't do anything). "
		 "default is -1");

module_param(wwan_status, int, 0444);
MODULE_PARM_DESC(wwan_status, "Set the wireless status on boot "
		 "(0 = disabled, 1 = enabled, -1 = don't do anything). "
		 "default is -1");

module_param(als_status, int, 0444);
MODULE_PARM_DESC(als_status, "Set the ALS status on boot "
		 "(0 = disabled, 1 = enabled). "
		 "default is 0");

/*
 * Some events we use, same for all Asus
 */
#define ATKD_BRNUP_MIN		0x10
#define ATKD_BRNUP_MAX		0x1f
#define ATKD_BRNDOWN_MIN	0x20
#define ATKD_BRNDOWN_MAX	0x2f
#define ATKD_BRNDOWN		0x20
#define ATKD_BRNUP		0x2f
#define ATKD_LCD_ON	0x33
#define ATKD_LCD_OFF	0x34

/*
 * Known bits returned by \_SB.ATKD.HWRS
 */
#define WL_HWRS		0x80
#define BT_HWRS		0x100

/*
 * Flags for hotk status
 * WL_ON and BT_ON are also used for wireless_status()
 */
#define WL_RSTS		0x01	/* internal Wifi */
#define BT_RSTS		0x02	/* internal Bluetooth */
#define WM_RSTS		0x08    /* internal wimax */
#define WW_RSTS		0x20    /* internal wwan */

/* WLED and BLED type */
#define TYPE_UNKNOWN	0
#define TYPE_LED	1
#define TYPE_RFKILL	2

/* LED */
#define METHOD_MLED		"MLED"
#define METHOD_TLED		"TLED"
#define METHOD_RLED		"RLED"	/* W1JC */
#define METHOD_PLED		"PLED"	/* A7J */
#define METHOD_GLED		"GLED"	/* G1, G2 (probably) */

/* LEDD */
#define METHOD_LEDD		"SLCM"

/*
 * Bluetooth and WLAN
 * WLED and BLED are not handled like other XLED, because in some dsdt
 * they also control the WLAN/Bluetooth device.
 */
#define METHOD_WLAN		"WLED"
#define METHOD_BLUETOOTH	"BLED"

/* WWAN and WIMAX */
#define METHOD_WWAN		"GSMC"
#define METHOD_WIMAX		"WMXC"

#define METHOD_WL_STATUS	"RSTS"

/* Brightness */
#define METHOD_BRIGHTNESS_SET	"SPLV"
#define METHOD_BRIGHTNESS_GET	"GPLV"

/* Display */
#define METHOD_SWITCH_DISPLAY	"SDSP"

#define METHOD_ALS_CONTROL	"ALSC" /* Z71A Z71V */
#define METHOD_ALS_LEVEL	"ALSL" /* Z71A Z71V */

/* GPS */
/* R2H use different handle for GPS on/off */
#define METHOD_GPS_ON		"SDON"
#define METHOD_GPS_OFF		"SDOF"
#define METHOD_GPS_STATUS	"GPST"

/* Keyboard light */
#define METHOD_KBD_LIGHT_SET	"SLKB"
#define METHOD_KBD_LIGHT_GET	"GLKB"

/* For Pegatron Lucid tablet */
#define DEVICE_NAME_PEGA	"Lucid"

#define METHOD_PEGA_ENABLE	"ENPR"
#define METHOD_PEGA_DISABLE	"DAPR"
#define PEGA_WLAN	0x00
#define PEGA_BLUETOOTH	0x01
#define PEGA_WWAN	0x02
#define PEGA_ALS	0x04
#define PEGA_ALS_POWER	0x05

#define METHOD_PEGA_READ	"RDLN"
#define PEGA_READ_ALS_H	0x02
#define PEGA_READ_ALS_L	0x03

#define PEGA_ACCEL_NAME "pega_accel"
#define PEGA_ACCEL_DESC "Pegatron Lucid Tablet Accelerometer"
#define METHOD_XLRX "XLRX"
#define METHOD_XLRY "XLRY"
#define METHOD_XLRZ "XLRZ"
#define PEGA_ACC_CLAMP 512 /* 1G accel is reported as ~256, so clamp to 2G */
#define PEGA_ACC_RETRIES 3

/*
 * Define a specific led structure to keep the main structure clean
 */
struct asus_led {
	int wk;
	struct work_struct work;
	struct led_classdev led;
	struct asus_laptop *asus;
	const char *method;
};

/*
 * Same thing for rfkill
 */
struct asus_rfkill {
	/* type of control. Maps to PEGA_* values or *_RSTS  */
	int control_id;
	struct rfkill *rfkill;
	struct asus_laptop *asus;
};

/*
 * This is the main structure, we can use it to store anything interesting
 * about the hotk device
 */
struct asus_laptop {
	char *name;		/* laptop name */

	struct acpi_table_header *dsdt_info;
	struct platform_device *platform_device;
	struct acpi_device *device;		/* the device we are in */
	struct backlight_device *backlight_device;

	struct input_dev *inputdev;
	struct key_entry *keymap;
	struct input_polled_dev *pega_accel_poll;

	struct asus_led wled;
	struct asus_led bled;
	struct asus_led mled;
	struct asus_led tled;
	struct asus_led rled;
	struct asus_led pled;
	struct asus_led gled;
	struct asus_led kled;
	struct workqueue_struct *led_workqueue;

	int wled_type;
	int bled_type;
	int wireless_status;
	bool have_rsts;
	bool is_pega_lucid;
	bool pega_acc_live;
	int pega_acc_x;
	int pega_acc_y;
	int pega_acc_z;

	struct asus_rfkill wlan;
	struct asus_rfkill bluetooth;
	struct asus_rfkill wwan;
	struct asus_rfkill wimax;
	struct asus_rfkill gps;

	acpi_handle handle;	/* the handle of the hotk device */
	u32 ledd_status;	/* status of the LED display */
	u8 light_level;		/* light sensor level */
	u8 light_switch;	/* light sensor switch value */
	u16 event_count[128];	/* count for each event TODO make this better */
};

static const struct key_entry asus_keymap[] = {
	/* Lenovo SL Specific keycodes */
	{KE_KEY, 0x02, { KEY_SCREENLOCK } },
	{KE_KEY, 0x05, { KEY_WLAN } },
	{KE_KEY, 0x08, { KEY_F13 } },
	{KE_KEY, 0x09, { KEY_PROG2 } }, /* Dock */
	{KE_KEY, 0x17, { KEY_ZOOM } },
	{KE_KEY, 0x1f, { KEY_BATTERY } },
	/* End of Lenovo SL Specific keycodes */
	{KE_KEY, ATKD_BRNDOWN, { KEY_BRIGHTNESSDOWN } },
	{KE_KEY, ATKD_BRNUP, { KEY_BRIGHTNESSUP } },
	{KE_KEY, 0x30, { KEY_VOLUMEUP } },
	{KE_KEY, 0x31, { KEY_VOLUMEDOWN } },
	{KE_KEY, 0x32, { KEY_MUTE } },
	{KE_KEY, 0x33, { KEY_DISPLAYTOGGLE } }, /* LCD on */
	{KE_KEY, 0x34, { KEY_DISPLAY_OFF } }, /* LCD off */
	{KE_KEY, 0x40, { KEY_PREVIOUSSONG } },
	{KE_KEY, 0x41, { KEY_NEXTSONG } },
	{KE_KEY, 0x43, { KEY_STOPCD } }, /* Stop/Eject */
	{KE_KEY, 0x45, { KEY_PLAYPAUSE } },
	{KE_KEY, 0x4c, { KEY_MEDIA } }, /* WMP Key */
	{KE_KEY, 0x50, { KEY_EMAIL } },
	{KE_KEY, 0x51, { KEY_WWW } },
	{KE_KEY, 0x55, { KEY_CALC } },
	{KE_IGNORE, 0x57, },  /* Battery mode */
	{KE_IGNORE, 0x58, },  /* AC mode */
	{KE_KEY, 0x5C, { KEY_SCREENLOCK } },  /* Screenlock */
	{KE_KEY, 0x5D, { KEY_WLAN } }, /* WLAN Toggle */
	{KE_KEY, 0x5E, { KEY_WLAN } }, /* WLAN Enable */
	{KE_KEY, 0x5F, { KEY_WLAN } }, /* WLAN Disable */
	{KE_KEY, 0x60, { KEY_TOUCHPAD_ON } },
	{KE_KEY, 0x61, { KEY_SWITCHVIDEOMODE } }, /* SDSP LCD only */
	{KE_KEY, 0x62, { KEY_SWITCHVIDEOMODE } }, /* SDSP CRT only */
	{KE_KEY, 0x63, { KEY_SWITCHVIDEOMODE } }, /* SDSP LCD + CRT */
	{KE_KEY, 0x64, { KEY_SWITCHVIDEOMODE } }, /* SDSP TV */
	{KE_KEY, 0x65, { KEY_SWITCHVIDEOMODE } }, /* SDSP LCD + TV */
	{KE_KEY, 0x66, { KEY_SWITCHVIDEOMODE } }, /* SDSP CRT + TV */
	{KE_KEY, 0x67, { KEY_SWITCHVIDEOMODE } }, /* SDSP LCD + CRT + TV */
	{KE_KEY, 0x6A, { KEY_TOUCHPAD_TOGGLE } }, /* Lock Touchpad Fn + F9 */
	{KE_KEY, 0x6B, { KEY_TOUCHPAD_TOGGLE } }, /* Lock Touchpad */
	{KE_KEY, 0x6C, { KEY_SLEEP } }, /* Suspend */
	{KE_KEY, 0x6D, { KEY_SLEEP } }, /* Hibernate */
	{KE_IGNORE, 0x6E, },  /* Low Battery notification */
	{KE_KEY, 0x7D, { KEY_BLUETOOTH } }, /* Bluetooth Enable */
	{KE_KEY, 0x7E, { KEY_BLUETOOTH } }, /* Bluetooth Disable */
	{KE_KEY, 0x82, { KEY_CAMERA } },
	{KE_KEY, 0x88, { KEY_RFKILL  } }, /* Radio Toggle Key */
	{KE_KEY, 0x8A, { KEY_PROG1 } }, /* Color enhancement mode */
	{KE_KEY, 0x8C, { KEY_SWITCHVIDEOMODE } }, /* SDSP DVI only */
	{KE_KEY, 0x8D, { KEY_SWITCHVIDEOMODE } }, /* SDSP LCD + DVI */
	{KE_KEY, 0x8E, { KEY_SWITCHVIDEOMODE } }, /* SDSP CRT + DVI */
	{KE_KEY, 0x8F, { KEY_SWITCHVIDEOMODE } }, /* SDSP TV + DVI */
	{KE_KEY, 0x90, { KEY_SWITCHVIDEOMODE } }, /* SDSP LCD + CRT + DVI */
	{KE_KEY, 0x91, { KEY_SWITCHVIDEOMODE } }, /* SDSP LCD + TV + DVI */
	{KE_KEY, 0x92, { KEY_SWITCHVIDEOMODE } }, /* SDSP CRT + TV + DVI */
	{KE_KEY, 0x93, { KEY_SWITCHVIDEOMODE } }, /* SDSP LCD + CRT + TV + DVI */
	{KE_KEY, 0x95, { KEY_MEDIA } },
	{KE_KEY, 0x99, { KEY_PHONE } },
	{KE_KEY, 0xA0, { KEY_SWITCHVIDEOMODE } }, /* SDSP HDMI only */
	{KE_KEY, 0xA1, { KEY_SWITCHVIDEOMODE } }, /* SDSP LCD + HDMI */
	{KE_KEY, 0xA2, { KEY_SWITCHVIDEOMODE } }, /* SDSP CRT + HDMI */
	{KE_KEY, 0xA3, { KEY_SWITCHVIDEOMODE } }, /* SDSP TV + HDMI */
	{KE_KEY, 0xA4, { KEY_SWITCHVIDEOMODE } }, /* SDSP LCD + CRT + HDMI */
	{KE_KEY, 0xA5, { KEY_SWITCHVIDEOMODE } }, /* SDSP LCD + TV + HDMI */
	{KE_KEY, 0xA6, { KEY_SWITCHVIDEOMODE } }, /* SDSP CRT + TV + HDMI */
	{KE_KEY, 0xA7, { KEY_SWITCHVIDEOMODE } }, /* SDSP LCD + CRT + TV + HDMI */
	{KE_KEY, 0xB5, { KEY_CALC } },
	{KE_KEY, 0xC4, { KEY_KBDILLUMUP } },
	{KE_KEY, 0xC5, { KEY_KBDILLUMDOWN } },
	{KE_END, 0},
};


/*
 * This function evaluates an ACPI method, given an int as parameter, the
 * method is searched within the scope of the handle, can be NULL. The output
 * of the method is written is output, which can also be NULL
 *
 * returns 0 if write is successful, -1 else.
 */
static int write_acpi_int_ret(acpi_handle handle, const char *method, int val,
			      struct acpi_buffer *output)
{
	struct acpi_object_list params;	/* list of input parameters (an int) */
	union acpi_object in_obj;	/* the only param we use */
	acpi_status status;

	if (!handle)
		return -1;

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = val;

	status = acpi_evaluate_object(handle, (char *)method, &params, output);
	if (status == AE_OK)
		return 0;
	else
		return -1;
}

static int write_acpi_int(acpi_handle handle, const char *method, int val)
{
	return write_acpi_int_ret(handle, method, val, NULL);
}

static int acpi_check_handle(acpi_handle handle, const char *method,
			     acpi_handle *ret)
{
	acpi_status status;

	if (method == NULL)
		return -ENODEV;

	if (ret)
		status = acpi_get_handle(handle, (char *)method,
					 ret);
	else {
		acpi_handle dummy;

		status = acpi_get_handle(handle, (char *)method,
					 &dummy);
	}

	if (status != AE_OK) {
		if (ret)
			pr_warn("Error finding %s\n", method);
		return -ENODEV;
	}
	return 0;
}

static bool asus_check_pega_lucid(struct asus_laptop *asus)
{
	return !strcmp(asus->name, DEVICE_NAME_PEGA) &&
	   !acpi_check_handle(asus->handle, METHOD_PEGA_ENABLE, NULL) &&
	   !acpi_check_handle(asus->handle, METHOD_PEGA_DISABLE, NULL) &&
	   !acpi_check_handle(asus->handle, METHOD_PEGA_READ, NULL);
}

static int asus_pega_lucid_set(struct asus_laptop *asus, int unit, bool enable)
{
	char *method = enable ? METHOD_PEGA_ENABLE : METHOD_PEGA_DISABLE;
	return write_acpi_int(asus->handle, method, unit);
}

static int pega_acc_axis(struct asus_laptop *asus, int curr, char *method)
{
	int i, delta;
	unsigned long long val;
	for (i = 0; i < PEGA_ACC_RETRIES; i++) {
		acpi_evaluate_integer(asus->handle, method, NULL, &val);

		/* The output is noisy.  From reading the ASL
		 * dissassembly, timeout errors are returned with 1's
		 * in the high word, and the lack of locking around
		 * thei hi/lo byte reads means that a transition
		 * between (for example) -1 and 0 could be read as
		 * 0xff00 or 0x00ff. */
		delta = abs(curr - (short)val);
		if (delta < 128 && !(val & ~0xffff))
			break;
	}
	return clamp_val((short)val, -PEGA_ACC_CLAMP, PEGA_ACC_CLAMP);
}

static void pega_accel_poll(struct input_polled_dev *ipd)
{
	struct device *parent = ipd->input->dev.parent;
	struct asus_laptop *asus = dev_get_drvdata(parent);

	/* In some cases, the very first call to poll causes a
	 * recursive fault under the polldev worker.  This is
	 * apparently related to very early userspace access to the
	 * device, and perhaps a firmware bug. Fake the first report. */
	if (!asus->pega_acc_live) {
		asus->pega_acc_live = true;
		input_report_abs(ipd->input, ABS_X, 0);
		input_report_abs(ipd->input, ABS_Y, 0);
		input_report_abs(ipd->input, ABS_Z, 0);
		input_sync(ipd->input);
		return;
	}

	asus->pega_acc_x = pega_acc_axis(asus, asus->pega_acc_x, METHOD_XLRX);
	asus->pega_acc_y = pega_acc_axis(asus, asus->pega_acc_y, METHOD_XLRY);
	asus->pega_acc_z = pega_acc_axis(asus, asus->pega_acc_z, METHOD_XLRZ);

	/* Note transform, convert to "right/up/out" in the native
	 * landscape orientation (i.e. the vector is the direction of
	 * "real up" in the device's cartiesian coordinates). */
	input_report_abs(ipd->input, ABS_X, -asus->pega_acc_x);
	input_report_abs(ipd->input, ABS_Y, -asus->pega_acc_y);
	input_report_abs(ipd->input, ABS_Z,  asus->pega_acc_z);
	input_sync(ipd->input);
}

static void pega_accel_exit(struct asus_laptop *asus)
{
	if (asus->pega_accel_poll) {
		input_unregister_polled_device(asus->pega_accel_poll);
		input_free_polled_device(asus->pega_accel_poll);
	}
	asus->pega_accel_poll = NULL;
}

static int pega_accel_init(struct asus_laptop *asus)
{
	int err;
	struct input_polled_dev *ipd;

	if (!asus->is_pega_lucid)
		return -ENODEV;

	if (acpi_check_handle(asus->handle, METHOD_XLRX, NULL) ||
	    acpi_check_handle(asus->handle, METHOD_XLRY, NULL) ||
	    acpi_check_handle(asus->handle, METHOD_XLRZ, NULL))
		return -ENODEV;

	ipd = input_allocate_polled_device();
	if (!ipd)
		return -ENOMEM;

	ipd->poll = pega_accel_poll;
	ipd->poll_interval = 125;
	ipd->poll_interval_min = 50;
	ipd->poll_interval_max = 2000;

	ipd->input->name = PEGA_ACCEL_DESC;
	ipd->input->phys = PEGA_ACCEL_NAME "/input0";
	ipd->input->dev.parent = &asus->platform_device->dev;
	ipd->input->id.bustype = BUS_HOST;

	set_bit(EV_ABS, ipd->input->evbit);
	input_set_abs_params(ipd->input, ABS_X,
			     -PEGA_ACC_CLAMP, PEGA_ACC_CLAMP, 0, 0);
	input_set_abs_params(ipd->input, ABS_Y,
			     -PEGA_ACC_CLAMP, PEGA_ACC_CLAMP, 0, 0);
	input_set_abs_params(ipd->input, ABS_Z,
			     -PEGA_ACC_CLAMP, PEGA_ACC_CLAMP, 0, 0);

	err = input_register_polled_device(ipd);
	if (err)
		goto exit;

	asus->pega_accel_poll = ipd;
	return 0;

exit:
	input_free_polled_device(ipd);
	return err;
}

/* Generic LED function */
static int asus_led_set(struct asus_laptop *asus, const char *method,
			 int value)
{
	if (!strcmp(method, METHOD_MLED))
		value = !value;
	else if (!strcmp(method, METHOD_GLED))
		value = !value + 1;
	else
		value = !!value;

	return write_acpi_int(asus->handle, method, value);
}

/*
 * LEDs
 */
/* /sys/class/led handlers */
static void asus_led_cdev_set(struct led_classdev *led_cdev,
			 enum led_brightness value)
{
	struct asus_led *led = container_of(led_cdev, struct asus_led, led);
	struct asus_laptop *asus = led->asus;

	led->wk = !!value;
	queue_work(asus->led_workqueue, &led->work);
}

static void asus_led_cdev_update(struct work_struct *work)
{
	struct asus_led *led = container_of(work, struct asus_led, work);
	struct asus_laptop *asus = led->asus;

	asus_led_set(asus, led->method, led->wk);
}

static enum led_brightness asus_led_cdev_get(struct led_classdev *led_cdev)
{
	return led_cdev->brightness;
}

/*
 * Keyboard backlight (also a LED)
 */
static int asus_kled_lvl(struct asus_laptop *asus)
{
	unsigned long long kblv;
	struct acpi_object_list params;
	union acpi_object in_obj;
	acpi_status rv;

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = 2;

	rv = acpi_evaluate_integer(asus->handle, METHOD_KBD_LIGHT_GET,
				   &params, &kblv);
	if (ACPI_FAILURE(rv)) {
		pr_warn("Error reading kled level\n");
		return -ENODEV;
	}
	return kblv;
}

static int asus_kled_set(struct asus_laptop *asus, int kblv)
{
	if (kblv > 0)
		kblv = (1 << 7) | (kblv & 0x7F);
	else
		kblv = 0;

	if (write_acpi_int(asus->handle, METHOD_KBD_LIGHT_SET, kblv)) {
		pr_warn("Keyboard LED display write failed\n");
		return -EINVAL;
	}
	return 0;
}

static void asus_kled_cdev_set(struct led_classdev *led_cdev,
			      enum led_brightness value)
{
	struct asus_led *led = container_of(led_cdev, struct asus_led, led);
	struct asus_laptop *asus = led->asus;

	led->wk = value;
	queue_work(asus->led_workqueue, &led->work);
}

static void asus_kled_cdev_update(struct work_struct *work)
{
	struct asus_led *led = container_of(work, struct asus_led, work);
	struct asus_laptop *asus = led->asus;

	asus_kled_set(asus, led->wk);
}

static enum led_brightness asus_kled_cdev_get(struct led_classdev *led_cdev)
{
	struct asus_led *led = container_of(led_cdev, struct asus_led, led);
	struct asus_laptop *asus = led->asus;

	return asus_kled_lvl(asus);
}

static void asus_led_exit(struct asus_laptop *asus)
{
	if (!IS_ERR_OR_NULL(asus->wled.led.dev))
		led_classdev_unregister(&asus->wled.led);
	if (!IS_ERR_OR_NULL(asus->bled.led.dev))
		led_classdev_unregister(&asus->bled.led);
	if (!IS_ERR_OR_NULL(asus->mled.led.dev))
		led_classdev_unregister(&asus->mled.led);
	if (!IS_ERR_OR_NULL(asus->tled.led.dev))
		led_classdev_unregister(&asus->tled.led);
	if (!IS_ERR_OR_NULL(asus->pled.led.dev))
		led_classdev_unregister(&asus->pled.led);
	if (!IS_ERR_OR_NULL(asus->rled.led.dev))
		led_classdev_unregister(&asus->rled.led);
	if (!IS_ERR_OR_NULL(asus->gled.led.dev))
		led_classdev_unregister(&asus->gled.led);
	if (!IS_ERR_OR_NULL(asus->kled.led.dev))
		led_classdev_unregister(&asus->kled.led);
	if (asus->led_workqueue) {
		destroy_workqueue(asus->led_workqueue);
		asus->led_workqueue = NULL;
	}
}

/*  Ugly macro, need to fix that later */
static int asus_led_register(struct asus_laptop *asus,
			     struct asus_led *led,
			     const char *name, const char *method)
{
	struct led_classdev *led_cdev = &led->led;

	if (!method || acpi_check_handle(asus->handle, method, NULL))
		return 0; /* Led not present */

	led->asus = asus;
	led->method = method;

	INIT_WORK(&led->work, asus_led_cdev_update);
	led_cdev->name = name;
	led_cdev->brightness_set = asus_led_cdev_set;
	led_cdev->brightness_get = asus_led_cdev_get;
	led_cdev->max_brightness = 1;
	return led_classdev_register(&asus->platform_device->dev, led_cdev);
}

static int asus_led_init(struct asus_laptop *asus)
{
	int r = 0;

	/*
	 * The Pegatron Lucid has no physical leds, but all methods are
	 * available in the DSDT...
	 */
	if (asus->is_pega_lucid)
		return 0;

	/*
	 * Functions that actually update the LED's are called from a
	 * workqueue. By doing this as separate work rather than when the LED
	 * subsystem asks, we avoid messing with the Asus ACPI stuff during a
	 * potentially bad time, such as a timer interrupt.
	 */
	asus->led_workqueue = create_singlethread_workqueue("led_workqueue");
	if (!asus->led_workqueue)
		return -ENOMEM;

	if (asus->wled_type == TYPE_LED)
		r = asus_led_register(asus, &asus->wled, "asus::wlan",
				      METHOD_WLAN);
	if (r)
		goto error;
	if (asus->bled_type == TYPE_LED)
		r = asus_led_register(asus, &asus->bled, "asus::bluetooth",
				      METHOD_BLUETOOTH);
	if (r)
		goto error;
	r = asus_led_register(asus, &asus->mled, "asus::mail", METHOD_MLED);
	if (r)
		goto error;
	r = asus_led_register(asus, &asus->tled, "asus::touchpad", METHOD_TLED);
	if (r)
		goto error;
	r = asus_led_register(asus, &asus->rled, "asus::record", METHOD_RLED);
	if (r)
		goto error;
	r = asus_led_register(asus, &asus->pled, "asus::phone", METHOD_PLED);
	if (r)
		goto error;
	r = asus_led_register(asus, &asus->gled, "asus::gaming", METHOD_GLED);
	if (r)
		goto error;
	if (!acpi_check_handle(asus->handle, METHOD_KBD_LIGHT_SET, NULL) &&
	    !acpi_check_handle(asus->handle, METHOD_KBD_LIGHT_GET, NULL)) {
		struct asus_led *led = &asus->kled;
		struct led_classdev *cdev = &led->led;

		led->asus = asus;

		INIT_WORK(&led->work, asus_kled_cdev_update);
		cdev->name = "asus::kbd_backlight";
		cdev->brightness_set = asus_kled_cdev_set;
		cdev->brightness_get = asus_kled_cdev_get;
		cdev->max_brightness = 3;
		r = led_classdev_register(&asus->platform_device->dev, cdev);
	}
error:
	if (r)
		asus_led_exit(asus);
	return r;
}

/*
 * Backlight device
 */
static int asus_read_brightness(struct backlight_device *bd)
{
	struct asus_laptop *asus = bl_get_data(bd);
	unsigned long long value;
	acpi_status rv = AE_OK;

	rv = acpi_evaluate_integer(asus->handle, METHOD_BRIGHTNESS_GET,
				   NULL, &value);
	if (ACPI_FAILURE(rv))
		pr_warn("Error reading brightness\n");

	return value;
}

static int asus_set_brightness(struct backlight_device *bd, int value)
{
	struct asus_laptop *asus = bl_get_data(bd);

	if (write_acpi_int(asus->handle, METHOD_BRIGHTNESS_SET, value)) {
		pr_warn("Error changing brightness\n");
		return -EIO;
	}
	return 0;
}

static int update_bl_status(struct backlight_device *bd)
{
	int value = bd->props.brightness;

	return asus_set_brightness(bd, value);
}

static const struct backlight_ops asusbl_ops = {
	.get_brightness = asus_read_brightness,
	.update_status = update_bl_status,
};

static int asus_backlight_notify(struct asus_laptop *asus)
{
	struct backlight_device *bd = asus->backlight_device;
	int old = bd->props.brightness;

	backlight_force_update(bd, BACKLIGHT_UPDATE_HOTKEY);

	return old;
}

static int asus_backlight_init(struct asus_laptop *asus)
{
	struct backlight_device *bd;
	struct backlight_properties props;

	if (acpi_check_handle(asus->handle, METHOD_BRIGHTNESS_GET, NULL) ||
	    acpi_check_handle(asus->handle, METHOD_BRIGHTNESS_SET, NULL))
		return 0;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = 15;
	props.type = BACKLIGHT_PLATFORM;

	bd = backlight_device_register(ASUS_LAPTOP_FILE,
				       &asus->platform_device->dev, asus,
				       &asusbl_ops, &props);
	if (IS_ERR(bd)) {
		pr_err("Could not register asus backlight device\n");
		asus->backlight_device = NULL;
		return PTR_ERR(bd);
	}

	asus->backlight_device = bd;
	bd->props.brightness = asus_read_brightness(bd);
	bd->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(bd);
	return 0;
}

static void asus_backlight_exit(struct asus_laptop *asus)
{
	backlight_device_unregister(asus->backlight_device);
	asus->backlight_device = NULL;
}

/*
 * Platform device handlers
 */

/*
 * We write our info in page, we begin at offset off and cannot write more
 * than count bytes. We set eof to 1 if we handle those 2 values. We return the
 * number of bytes written in page
 */
static ssize_t infos_show(struct device *dev, struct device_attribute *attr,
			  char *page)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);
	int len = 0;
	unsigned long long temp;
	char buf[16];		/* enough for all info */
	acpi_status rv = AE_OK;

	/*
	 * We use the easy way, we don't care of off and count,
	 * so we don't set eof to 1
	 */

	len += sprintf(page, ASUS_LAPTOP_NAME " " ASUS_LAPTOP_VERSION "\n");
	len += sprintf(page + len, "Model reference    : %s\n", asus->name);
	/*
	 * The SFUN method probably allows the original driver to get the list
	 * of features supported by a given model. For now, 0x0100 or 0x0800
	 * bit signifies that the laptop is equipped with a Wi-Fi MiniPCI card.
	 * The significance of others is yet to be found.
	 */
	rv = acpi_evaluate_integer(asus->handle, "SFUN", NULL, &temp);
	if (!ACPI_FAILURE(rv))
		len += sprintf(page + len, "SFUN value         : %#x\n",
			       (uint) temp);
	/*
	 * The HWRS method return informations about the hardware.
	 * 0x80 bit is for WLAN, 0x100 for Bluetooth.
	 * 0x40 for WWAN, 0x10 for WIMAX.
	 * The significance of others is yet to be found.
	 * We don't currently use this for device detection, and it
	 * takes several seconds to run on some systems.
	 */
	rv = acpi_evaluate_integer(asus->handle, "HWRS", NULL, &temp);
	if (!ACPI_FAILURE(rv))
		len += sprintf(page + len, "HWRS value         : %#x\n",
			       (uint) temp);
	/*
	 * Another value for userspace: the ASYM method returns 0x02 for
	 * battery low and 0x04 for battery critical, its readings tend to be
	 * more accurate than those provided by _BST.
	 * Note: since not all the laptops provide this method, errors are
	 * silently ignored.
	 */
	rv = acpi_evaluate_integer(asus->handle, "ASYM", NULL, &temp);
	if (!ACPI_FAILURE(rv))
		len += sprintf(page + len, "ASYM value         : %#x\n",
			       (uint) temp);
	if (asus->dsdt_info) {
		snprintf(buf, 16, "%d", asus->dsdt_info->length);
		len += sprintf(page + len, "DSDT length        : %s\n", buf);
		snprintf(buf, 16, "%d", asus->dsdt_info->checksum);
		len += sprintf(page + len, "DSDT checksum      : %s\n", buf);
		snprintf(buf, 16, "%d", asus->dsdt_info->revision);
		len += sprintf(page + len, "DSDT revision      : %s\n", buf);
		snprintf(buf, 7, "%s", asus->dsdt_info->oem_id);
		len += sprintf(page + len, "OEM id             : %s\n", buf);
		snprintf(buf, 9, "%s", asus->dsdt_info->oem_table_id);
		len += sprintf(page + len, "OEM table id       : %s\n", buf);
		snprintf(buf, 16, "%x", asus->dsdt_info->oem_revision);
		len += sprintf(page + len, "OEM revision       : 0x%s\n", buf);
		snprintf(buf, 5, "%s", asus->dsdt_info->asl_compiler_id);
		len += sprintf(page + len, "ASL comp vendor id : %s\n", buf);
		snprintf(buf, 16, "%x", asus->dsdt_info->asl_compiler_revision);
		len += sprintf(page + len, "ASL comp revision  : 0x%s\n", buf);
	}

	return len;
}
static DEVICE_ATTR_RO(infos);

static int parse_arg(const char *buf, unsigned long count, int *val)
{
	if (!count)
		return 0;
	if (count > 31)
		return -EINVAL;
	if (sscanf(buf, "%i", val) != 1)
		return -EINVAL;
	return count;
}

static ssize_t sysfs_acpi_set(struct asus_laptop *asus,
			      const char *buf, size_t count,
			      const char *method)
{
	int rv, value;
	int out = 0;

	rv = parse_arg(buf, count, &value);
	if (rv > 0)
		out = value ? 1 : 0;

	if (write_acpi_int(asus->handle, method, value))
		return -ENODEV;
	return rv;
}

/*
 * LEDD display
 */
static ssize_t ledd_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sprintf(buf, "0x%08x\n", asus->ledd_status);
}

static ssize_t ledd_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);
	int rv, value;

	rv = parse_arg(buf, count, &value);
	if (rv > 0) {
		if (write_acpi_int(asus->handle, METHOD_LEDD, value)) {
			pr_warn("LED display write failed\n");
			return -ENODEV;
		}
		asus->ledd_status = (u32) value;
	}
	return rv;
}
static DEVICE_ATTR_RW(ledd);

/*
 * Wireless
 */
static int asus_wireless_status(struct asus_laptop *asus, int mask)
{
	unsigned long long status;
	acpi_status rv = AE_OK;

	if (!asus->have_rsts)
		return (asus->wireless_status & mask) ? 1 : 0;

	rv = acpi_evaluate_integer(asus->handle, METHOD_WL_STATUS,
				   NULL, &status);
	if (ACPI_FAILURE(rv)) {
		pr_warn("Error reading Wireless status\n");
		return -EINVAL;
	}
	return !!(status & mask);
}

/*
 * WLAN
 */
static int asus_wlan_set(struct asus_laptop *asus, int status)
{
	if (write_acpi_int(asus->handle, METHOD_WLAN, !!status)) {
		pr_warn("Error setting wlan status to %d\n", status);
		return -EIO;
	}
	return 0;
}

static ssize_t wlan_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", asus_wireless_status(asus, WL_RSTS));
}

static ssize_t wlan_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sysfs_acpi_set(asus, buf, count, METHOD_WLAN);
}
static DEVICE_ATTR_RW(wlan);

/*e
 * Bluetooth
 */
static int asus_bluetooth_set(struct asus_laptop *asus, int status)
{
	if (write_acpi_int(asus->handle, METHOD_BLUETOOTH, !!status)) {
		pr_warn("Error setting bluetooth status to %d\n", status);
		return -EIO;
	}
	return 0;
}

static ssize_t bluetooth_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", asus_wireless_status(asus, BT_RSTS));
}

static ssize_t bluetooth_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sysfs_acpi_set(asus, buf, count, METHOD_BLUETOOTH);
}
static DEVICE_ATTR_RW(bluetooth);

/*
 * Wimax
 */
static int asus_wimax_set(struct asus_laptop *asus, int status)
{
	if (write_acpi_int(asus->handle, METHOD_WIMAX, !!status)) {
		pr_warn("Error setting wimax status to %d\n", status);
		return -EIO;
	}
	return 0;
}

static ssize_t wimax_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", asus_wireless_status(asus, WM_RSTS));
}

static ssize_t wimax_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sysfs_acpi_set(asus, buf, count, METHOD_WIMAX);
}
static DEVICE_ATTR_RW(wimax);

/*
 * Wwan
 */
static int asus_wwan_set(struct asus_laptop *asus, int status)
{
	if (write_acpi_int(asus->handle, METHOD_WWAN, !!status)) {
		pr_warn("Error setting wwan status to %d\n", status);
		return -EIO;
	}
	return 0;
}

static ssize_t wwan_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", asus_wireless_status(asus, WW_RSTS));
}

static ssize_t wwan_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sysfs_acpi_set(asus, buf, count, METHOD_WWAN);
}
static DEVICE_ATTR_RW(wwan);

/*
 * Display
 */
static void asus_set_display(struct asus_laptop *asus, int value)
{
	/* no sanity check needed for now */
	if (write_acpi_int(asus->handle, METHOD_SWITCH_DISPLAY, value))
		pr_warn("Error setting display\n");
	return;
}

/*
 * Experimental support for display switching. As of now: 1 should activate
 * the LCD output, 2 should do for CRT, 4 for TV-Out and 8 for DVI.
 * Any combination (bitwise) of these will suffice. I never actually tested 4
 * displays hooked up simultaneously, so be warned. See the acpi4asus README
 * for more info.
 */
static ssize_t display_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);
	int rv, value;

	rv = parse_arg(buf, count, &value);
	if (rv > 0)
		asus_set_display(asus, value);
	return rv;
}
static DEVICE_ATTR_WO(display);

/*
 * Light Sens
 */
static void asus_als_switch(struct asus_laptop *asus, int value)
{
	int ret;

	if (asus->is_pega_lucid) {
		ret = asus_pega_lucid_set(asus, PEGA_ALS, value);
		if (!ret)
			ret = asus_pega_lucid_set(asus, PEGA_ALS_POWER, value);
	} else {
		ret = write_acpi_int(asus->handle, METHOD_ALS_CONTROL, value);
	}
	if (ret)
		pr_warning("Error setting light sensor switch\n");

	asus->light_switch = value;
}

static ssize_t ls_switch_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", asus->light_switch);
}

static ssize_t ls_switch_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);
	int rv, value;

	rv = parse_arg(buf, count, &value);
	if (rv > 0)
		asus_als_switch(asus, value ? 1 : 0);

	return rv;
}
static DEVICE_ATTR_RW(ls_switch);

static void asus_als_level(struct asus_laptop *asus, int value)
{
	if (write_acpi_int(asus->handle, METHOD_ALS_LEVEL, value))
		pr_warn("Error setting light sensor level\n");
	asus->light_level = value;
}

static ssize_t ls_level_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", asus->light_level);
}

static ssize_t ls_level_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);
	int rv, value;

	rv = parse_arg(buf, count, &value);
	if (rv > 0) {
		value = (0 < value) ? ((15 < value) ? 15 : value) : 0;
		/* 0 <= value <= 15 */
		asus_als_level(asus, value);
	}

	return rv;
}
static DEVICE_ATTR_RW(ls_level);

static int pega_int_read(struct asus_laptop *asus, int arg, int *result)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	int err = write_acpi_int_ret(asus->handle, METHOD_PEGA_READ, arg,
				     &buffer);
	if (!err) {
		union acpi_object *obj = buffer.pointer;
		if (obj && obj->type == ACPI_TYPE_INTEGER)
			*result = obj->integer.value;
		else
			err = -EIO;
	}
	return err;
}

static ssize_t ls_value_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);
	int err, hi, lo;

	err = pega_int_read(asus, PEGA_READ_ALS_H, &hi);
	if (!err)
		err = pega_int_read(asus, PEGA_READ_ALS_L, &lo);
	if (!err)
		return sprintf(buf, "%d\n", 10 * hi + lo);
	return err;
}
static DEVICE_ATTR_RO(ls_value);

/*
 * GPS
 */
static int asus_gps_status(struct asus_laptop *asus)
{
	unsigned long long status;
	acpi_status rv = AE_OK;

	rv = acpi_evaluate_integer(asus->handle, METHOD_GPS_STATUS,
				   NULL, &status);
	if (ACPI_FAILURE(rv)) {
		pr_warn("Error reading GPS status\n");
		return -ENODEV;
	}
	return !!status;
}

static int asus_gps_switch(struct asus_laptop *asus, int status)
{
	const char *meth = status ? METHOD_GPS_ON : METHOD_GPS_OFF;

	if (write_acpi_int(asus->handle, meth, 0x02))
		return -ENODEV;
	return 0;
}

static ssize_t gps_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", asus_gps_status(asus));
}

static ssize_t gps_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);
	int rv, value;
	int ret;

	rv = parse_arg(buf, count, &value);
	if (rv <= 0)
		return -EINVAL;
	ret = asus_gps_switch(asus, !!value);
	if (ret)
		return ret;
	rfkill_set_sw_state(asus->gps.rfkill, !value);
	return rv;
}
static DEVICE_ATTR_RW(gps);

/*
 * rfkill
 */
static int asus_gps_rfkill_set(void *data, bool blocked)
{
	struct asus_laptop *asus = data;

	return asus_gps_switch(asus, !blocked);
}

static const struct rfkill_ops asus_gps_rfkill_ops = {
	.set_block = asus_gps_rfkill_set,
};

static int asus_rfkill_set(void *data, bool blocked)
{
	struct asus_rfkill *rfk = data;
	struct asus_laptop *asus = rfk->asus;

	if (rfk->control_id == WL_RSTS)
		return asus_wlan_set(asus, !blocked);
	else if (rfk->control_id == BT_RSTS)
		return asus_bluetooth_set(asus, !blocked);
	else if (rfk->control_id == WM_RSTS)
		return asus_wimax_set(asus, !blocked);
	else if (rfk->control_id == WW_RSTS)
		return asus_wwan_set(asus, !blocked);

	return -EINVAL;
}

static const struct rfkill_ops asus_rfkill_ops = {
	.set_block = asus_rfkill_set,
};

static void asus_rfkill_terminate(struct asus_rfkill *rfk)
{
	if (!rfk->rfkill)
		return ;

	rfkill_unregister(rfk->rfkill);
	rfkill_destroy(rfk->rfkill);
	rfk->rfkill = NULL;
}

static void asus_rfkill_exit(struct asus_laptop *asus)
{
	asus_rfkill_terminate(&asus->wwan);
	asus_rfkill_terminate(&asus->bluetooth);
	asus_rfkill_terminate(&asus->wlan);
	asus_rfkill_terminate(&asus->gps);
}

static int asus_rfkill_setup(struct asus_laptop *asus, struct asus_rfkill *rfk,
			     const char *name, int control_id, int type,
			     const struct rfkill_ops *ops)
{
	int result;

	rfk->control_id = control_id;
	rfk->asus = asus;
	rfk->rfkill = rfkill_alloc(name, &asus->platform_device->dev,
				   type, ops, rfk);
	if (!rfk->rfkill)
		return -EINVAL;

	result = rfkill_register(rfk->rfkill);
	if (result) {
		rfkill_destroy(rfk->rfkill);
		rfk->rfkill = NULL;
	}

	return result;
}

static int asus_rfkill_init(struct asus_laptop *asus)
{
	int result = 0;

	if (asus->is_pega_lucid)
		return -ENODEV;

	if (!acpi_check_handle(asus->handle, METHOD_GPS_ON, NULL) &&
	    !acpi_check_handle(asus->handle, METHOD_GPS_OFF, NULL) &&
	    !acpi_check_handle(asus->handle, METHOD_GPS_STATUS, NULL))
		result = asus_rfkill_setup(asus, &asus->gps, "asus-gps",
					   -1, RFKILL_TYPE_GPS,
					   &asus_gps_rfkill_ops);
	if (result)
		goto exit;


	if (!acpi_check_handle(asus->handle, METHOD_WLAN, NULL) &&
	    asus->wled_type == TYPE_RFKILL)
		result = asus_rfkill_setup(asus, &asus->wlan, "asus-wlan",
					   WL_RSTS, RFKILL_TYPE_WLAN,
					   &asus_rfkill_ops);
	if (result)
		goto exit;

	if (!acpi_check_handle(asus->handle, METHOD_BLUETOOTH, NULL) &&
	    asus->bled_type == TYPE_RFKILL)
		result = asus_rfkill_setup(asus, &asus->bluetooth,
					   "asus-bluetooth", BT_RSTS,
					   RFKILL_TYPE_BLUETOOTH,
					   &asus_rfkill_ops);
	if (result)
		goto exit;

	if (!acpi_check_handle(asus->handle, METHOD_WWAN, NULL))
		result = asus_rfkill_setup(asus, &asus->wwan, "asus-wwan",
					   WW_RSTS, RFKILL_TYPE_WWAN,
					   &asus_rfkill_ops);
	if (result)
		goto exit;

	if (!acpi_check_handle(asus->handle, METHOD_WIMAX, NULL))
		result = asus_rfkill_setup(asus, &asus->wimax, "asus-wimax",
					   WM_RSTS, RFKILL_TYPE_WIMAX,
					   &asus_rfkill_ops);
	if (result)
		goto exit;

exit:
	if (result)
		asus_rfkill_exit(asus);

	return result;
}

static int pega_rfkill_set(void *data, bool blocked)
{
	struct asus_rfkill *rfk = data;

	int ret = asus_pega_lucid_set(rfk->asus, rfk->control_id, !blocked);
	return ret;
}

static const struct rfkill_ops pega_rfkill_ops = {
	.set_block = pega_rfkill_set,
};

static int pega_rfkill_setup(struct asus_laptop *asus, struct asus_rfkill *rfk,
			     const char *name, int controlid, int rfkill_type)
{
	return asus_rfkill_setup(asus, rfk, name, controlid, rfkill_type,
				 &pega_rfkill_ops);
}

static int pega_rfkill_init(struct asus_laptop *asus)
{
	int ret = 0;

	if(!asus->is_pega_lucid)
		return -ENODEV;

	ret = pega_rfkill_setup(asus, &asus->wlan, "pega-wlan",
				PEGA_WLAN, RFKILL_TYPE_WLAN);
	if(ret)
		goto exit;

	ret = pega_rfkill_setup(asus, &asus->bluetooth, "pega-bt",
				PEGA_BLUETOOTH, RFKILL_TYPE_BLUETOOTH);
	if(ret)
		goto exit;

	ret = pega_rfkill_setup(asus, &asus->wwan, "pega-wwan",
				PEGA_WWAN, RFKILL_TYPE_WWAN);

exit:
	if (ret)
		asus_rfkill_exit(asus);

	return ret;
}

/*
 * Input device (i.e. hotkeys)
 */
static void asus_input_notify(struct asus_laptop *asus, int event)
{
	if (!asus->inputdev)
		return ;
	if (!sparse_keymap_report_event(asus->inputdev, event, 1, true))
		pr_info("Unknown key %x pressed\n", event);
}

static int asus_input_init(struct asus_laptop *asus)
{
	struct input_dev *input;
	int error;

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	input->name = "Asus Laptop extra buttons";
	input->phys = ASUS_LAPTOP_FILE "/input0";
	input->id.bustype = BUS_HOST;
	input->dev.parent = &asus->platform_device->dev;

	error = sparse_keymap_setup(input, asus_keymap, NULL);
	if (error) {
		pr_err("Unable to setup input device keymap\n");
		goto err_free_dev;
	}
	error = input_register_device(input);
	if (error) {
		pr_warn("Unable to register input device\n");
		goto err_free_keymap;
	}

	asus->inputdev = input;
	return 0;

err_free_keymap:
	sparse_keymap_free(input);
err_free_dev:
	input_free_device(input);
	return error;
}

static void asus_input_exit(struct asus_laptop *asus)
{
	if (asus->inputdev) {
		sparse_keymap_free(asus->inputdev);
		input_unregister_device(asus->inputdev);
	}
	asus->inputdev = NULL;
}

/*
 * ACPI driver
 */
static void asus_acpi_notify(struct acpi_device *device, u32 event)
{
	struct asus_laptop *asus = acpi_driver_data(device);
	u16 count;

	/* TODO Find a better way to handle events count. */
	count = asus->event_count[event % 128]++;
	acpi_bus_generate_netlink_event(asus->device->pnp.device_class,
					dev_name(&asus->device->dev), event,
					count);

	if (event >= ATKD_BRNUP_MIN && event <= ATKD_BRNUP_MAX)
		event = ATKD_BRNUP;
	else if (event >= ATKD_BRNDOWN_MIN &&
		 event <= ATKD_BRNDOWN_MAX)
		event = ATKD_BRNDOWN;

	/* Brightness events are special */
	if (event == ATKD_BRNDOWN || event == ATKD_BRNUP) {
		if (asus->backlight_device != NULL) {
			/* Update the backlight device. */
			asus_backlight_notify(asus);
			return ;
		}
	}

	/* Accelerometer "coarse orientation change" event */
	if (asus->pega_accel_poll && event == 0xEA) {
		kobject_uevent(&asus->pega_accel_poll->input->dev.kobj,
			       KOBJ_CHANGE);
		return ;
	}

	asus_input_notify(asus, event);
}

static struct attribute *asus_attributes[] = {
	&dev_attr_infos.attr,
	&dev_attr_wlan.attr,
	&dev_attr_bluetooth.attr,
	&dev_attr_wimax.attr,
	&dev_attr_wwan.attr,
	&dev_attr_display.attr,
	&dev_attr_ledd.attr,
	&dev_attr_ls_value.attr,
	&dev_attr_ls_level.attr,
	&dev_attr_ls_switch.attr,
	&dev_attr_gps.attr,
	NULL
};

static umode_t asus_sysfs_is_visible(struct kobject *kobj,
				    struct attribute *attr,
				    int idx)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct platform_device *pdev = to_platform_device(dev);
	struct asus_laptop *asus = platform_get_drvdata(pdev);
	acpi_handle handle = asus->handle;
	bool supported;

	if (asus->is_pega_lucid) {
		/* no ls_level interface on the Lucid */
		if (attr == &dev_attr_ls_switch.attr)
			supported = true;
		else if (attr == &dev_attr_ls_level.attr)
			supported = false;
		else
			goto normal;

		return supported ? attr->mode : 0;
	}

normal:
	if (attr == &dev_attr_wlan.attr) {
		supported = !acpi_check_handle(handle, METHOD_WLAN, NULL);

	} else if (attr == &dev_attr_bluetooth.attr) {
		supported = !acpi_check_handle(handle, METHOD_BLUETOOTH, NULL);

	} else if (attr == &dev_attr_display.attr) {
		supported = !acpi_check_handle(handle, METHOD_SWITCH_DISPLAY, NULL);

	} else if (attr == &dev_attr_wimax.attr) {
		supported =
			!acpi_check_handle(asus->handle, METHOD_WIMAX, NULL);

	} else if (attr == &dev_attr_wwan.attr) {
		supported = !acpi_check_handle(asus->handle, METHOD_WWAN, NULL);

	} else if (attr == &dev_attr_ledd.attr) {
		supported = !acpi_check_handle(handle, METHOD_LEDD, NULL);

	} else if (attr == &dev_attr_ls_switch.attr ||
		   attr == &dev_attr_ls_level.attr) {
		supported = !acpi_check_handle(handle, METHOD_ALS_CONTROL, NULL) &&
			!acpi_check_handle(handle, METHOD_ALS_LEVEL, NULL);
	} else if (attr == &dev_attr_ls_value.attr) {
		supported = asus->is_pega_lucid;
	} else if (attr == &dev_attr_gps.attr) {
		supported = !acpi_check_handle(handle, METHOD_GPS_ON, NULL) &&
			    !acpi_check_handle(handle, METHOD_GPS_OFF, NULL) &&
			    !acpi_check_handle(handle, METHOD_GPS_STATUS, NULL);
	} else {
		supported = true;
	}

	return supported ? attr->mode : 0;
}


static const struct attribute_group asus_attr_group = {
	.is_visible	= asus_sysfs_is_visible,
	.attrs		= asus_attributes,
};

static int asus_platform_init(struct asus_laptop *asus)
{
	int result;

	asus->platform_device = platform_device_alloc(ASUS_LAPTOP_FILE, -1);
	if (!asus->platform_device)
		return -ENOMEM;
	platform_set_drvdata(asus->platform_device, asus);

	result = platform_device_add(asus->platform_device);
	if (result)
		goto fail_platform_device;

	result = sysfs_create_group(&asus->platform_device->dev.kobj,
				    &asus_attr_group);
	if (result)
		goto fail_sysfs;

	return 0;

fail_sysfs:
	platform_device_del(asus->platform_device);
fail_platform_device:
	platform_device_put(asus->platform_device);
	return result;
}

static void asus_platform_exit(struct asus_laptop *asus)
{
	sysfs_remove_group(&asus->platform_device->dev.kobj, &asus_attr_group);
	platform_device_unregister(asus->platform_device);
}

static struct platform_driver platform_driver = {
	.driver = {
		.name = ASUS_LAPTOP_FILE,
	},
};

/*
 * This function is used to initialize the context with right values. In this
 * method, we can make all the detection we want, and modify the asus_laptop
 * struct
 */
static int asus_laptop_get_info(struct asus_laptop *asus)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *model = NULL;
	unsigned long long bsts_result;
	char *string = NULL;
	acpi_status status;

	/*
	 * Get DSDT headers early enough to allow for differentiating between
	 * models, but late enough to allow acpi_bus_register_driver() to fail
	 * before doing anything ACPI-specific. Should we encounter a machine,
	 * which needs special handling (i.e. its hotkey device has a different
	 * HID), this bit will be moved.
	 */
	status = acpi_get_table(ACPI_SIG_DSDT, 1, &asus->dsdt_info);
	if (ACPI_FAILURE(status))
		pr_warn("Couldn't get the DSDT table header\n");

	/* We have to write 0 on init this far for all ASUS models */
	if (write_acpi_int_ret(asus->handle, "INIT", 0, &buffer)) {
		pr_err("Hotkey initialization failed\n");
		return -ENODEV;
	}

	/* This needs to be called for some laptops to init properly */
	status =
	    acpi_evaluate_integer(asus->handle, "BSTS", NULL, &bsts_result);
	if (ACPI_FAILURE(status))
		pr_warn("Error calling BSTS\n");
	else if (bsts_result)
		pr_notice("BSTS called, 0x%02x returned\n",
		       (uint) bsts_result);

	/* This too ... */
	if (write_acpi_int(asus->handle, "CWAP", wapf))
		pr_err("Error calling CWAP(%d)\n", wapf);
	/*
	 * Try to match the object returned by INIT to the specific model.
	 * Handle every possible object (or the lack of thereof) the DSDT
	 * writers might throw at us. When in trouble, we pass NULL to
	 * asus_model_match() and try something completely different.
	 */
	if (buffer.pointer) {
		model = buffer.pointer;
		switch (model->type) {
		case ACPI_TYPE_STRING:
			string = model->string.pointer;
			break;
		case ACPI_TYPE_BUFFER:
			string = model->buffer.pointer;
			break;
		default:
			string = "";
			break;
		}
	}
	asus->name = kstrdup(string, GFP_KERNEL);
	if (!asus->name) {
		kfree(buffer.pointer);
		return -ENOMEM;
	}

	if (string)
		pr_notice("  %s model detected\n", string);

	if (!acpi_check_handle(asus->handle, METHOD_WL_STATUS, NULL))
		asus->have_rsts = true;

	kfree(model);

	return AE_OK;
}

static int asus_acpi_init(struct asus_laptop *asus)
{
	int result = 0;

	result = acpi_bus_get_status(asus->device);
	if (result)
		return result;
	if (!asus->device->status.present) {
		pr_err("Hotkey device not present, aborting\n");
		return -ENODEV;
	}

	result = asus_laptop_get_info(asus);
	if (result)
		return result;

	if (!strcmp(bled_type, "led"))
		asus->bled_type = TYPE_LED;
	else if (!strcmp(bled_type, "rfkill"))
		asus->bled_type = TYPE_RFKILL;

	if (!strcmp(wled_type, "led"))
		asus->wled_type = TYPE_LED;
	else if (!strcmp(wled_type, "rfkill"))
		asus->wled_type = TYPE_RFKILL;

	if (bluetooth_status >= 0)
		asus_bluetooth_set(asus, !!bluetooth_status);

	if (wlan_status >= 0)
		asus_wlan_set(asus, !!wlan_status);

	if (wimax_status >= 0)
		asus_wimax_set(asus, !!wimax_status);

	if (wwan_status >= 0)
		asus_wwan_set(asus, !!wwan_status);

	/* Keyboard Backlight is on by default */
	if (!acpi_check_handle(asus->handle, METHOD_KBD_LIGHT_SET, NULL))
		asus_kled_set(asus, 1);

	/* LED display is off by default */
	asus->ledd_status = 0xFFF;

	/* Set initial values of light sensor and level */
	asus->light_switch = !!als_status;
	asus->light_level = 5;	/* level 5 for sensor sensitivity */

	if (asus->is_pega_lucid) {
		asus_als_switch(asus, asus->light_switch);
	} else if (!acpi_check_handle(asus->handle, METHOD_ALS_CONTROL, NULL) &&
		   !acpi_check_handle(asus->handle, METHOD_ALS_LEVEL, NULL)) {
		asus_als_switch(asus, asus->light_switch);
		asus_als_level(asus, asus->light_level);
	}

	return result;
}

static void asus_dmi_check(void)
{
	const char *model;

	model = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (!model)
		return;

	/* On L1400B WLED control the sound card, don't mess with it ... */
	if (strncmp(model, "L1400B", 6) == 0) {
		wlan_status = -1;
	}
}

static bool asus_device_present;

static int asus_acpi_add(struct acpi_device *device)
{
	struct asus_laptop *asus;
	int result;

	pr_notice("Asus Laptop Support version %s\n",
		  ASUS_LAPTOP_VERSION);
	asus = kzalloc(sizeof(struct asus_laptop), GFP_KERNEL);
	if (!asus)
		return -ENOMEM;
	asus->handle = device->handle;
	strcpy(acpi_device_name(device), ASUS_LAPTOP_DEVICE_NAME);
	strcpy(acpi_device_class(device), ASUS_LAPTOP_CLASS);
	device->driver_data = asus;
	asus->device = device;

	asus_dmi_check();

	result = asus_acpi_init(asus);
	if (result)
		goto fail_platform;

	/*
	 * Need platform type detection first, then the platform
	 * device.  It is used as a parent for the sub-devices below.
	 */
	asus->is_pega_lucid = asus_check_pega_lucid(asus);
	result = asus_platform_init(asus);
	if (result)
		goto fail_platform;

	if (acpi_video_get_backlight_type() == acpi_backlight_vendor) {
		result = asus_backlight_init(asus);
		if (result)
			goto fail_backlight;
	}

	result = asus_input_init(asus);
	if (result)
		goto fail_input;

	result = asus_led_init(asus);
	if (result)
		goto fail_led;

	result = asus_rfkill_init(asus);
	if (result && result != -ENODEV)
		goto fail_rfkill;

	result = pega_accel_init(asus);
	if (result && result != -ENODEV)
		goto fail_pega_accel;

	result = pega_rfkill_init(asus);
	if (result && result != -ENODEV)
		goto fail_pega_rfkill;

	asus_device_present = true;
	return 0;

fail_pega_rfkill:
	pega_accel_exit(asus);
fail_pega_accel:
	asus_rfkill_exit(asus);
fail_rfkill:
	asus_led_exit(asus);
fail_led:
	asus_input_exit(asus);
fail_input:
	asus_backlight_exit(asus);
fail_backlight:
	asus_platform_exit(asus);
fail_platform:
	kfree(asus);

	return result;
}

static int asus_acpi_remove(struct acpi_device *device)
{
	struct asus_laptop *asus = acpi_driver_data(device);

	asus_backlight_exit(asus);
	asus_rfkill_exit(asus);
	asus_led_exit(asus);
	asus_input_exit(asus);
	pega_accel_exit(asus);
	asus_platform_exit(asus);

	kfree(asus->name);
	kfree(asus);
	return 0;
}

static const struct acpi_device_id asus_device_ids[] = {
	{"ATK0100", 0},
	{"ATK0101", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, asus_device_ids);

static struct acpi_driver asus_acpi_driver = {
	.name = ASUS_LAPTOP_NAME,
	.class = ASUS_LAPTOP_CLASS,
	.owner = THIS_MODULE,
	.ids = asus_device_ids,
	.flags = ACPI_DRIVER_ALL_NOTIFY_EVENTS,
	.ops = {
		.add = asus_acpi_add,
		.remove = asus_acpi_remove,
		.notify = asus_acpi_notify,
		},
};

static int __init asus_laptop_init(void)
{
	int result;

	result = platform_driver_register(&platform_driver);
	if (result < 0)
		return result;

	result = acpi_bus_register_driver(&asus_acpi_driver);
	if (result < 0)
		goto fail_acpi_driver;
	if (!asus_device_present) {
		result = -ENODEV;
		goto fail_no_device;
	}
	return 0;

fail_no_device:
	acpi_bus_unregister_driver(&asus_acpi_driver);
fail_acpi_driver:
	platform_driver_unregister(&platform_driver);
	return result;
}

static void __exit asus_laptop_exit(void)
{
	acpi_bus_unregister_driver(&asus_acpi_driver);
	platform_driver_unregister(&platform_driver);
}

module_init(asus_laptop_init);
module_exit(asus_laptop_exit);
