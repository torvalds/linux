/*
 *  asus-laptop.c - Asus Laptop Support
 *
 *
 *  Copyright (C) 2002-2005 Julien Lerouge, 2003-2006 Karol Kozimor
 *  Copyright (C) 2006-2007 Corentin Chary
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
 *  Thomas Tuttle  - His first patch for led support was very helpfull
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
#include <linux/rfkill.h>
#include <linux/slab.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>

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
 * 0x0 will do nothing
 * 0x1 will allow to control the device with Fn+Fx key.
 * 0x4 will send an ACPI event (0x88) while pressing the Fn+Fx key
 * 0x5 like 0x1 or 0x4
 * So, if something doesn't work as you want, just try other values =)
 */
static uint wapf = 1;
module_param(wapf, uint, 0444);
MODULE_PARM_DESC(wapf, "WAPF value");

static int wlan_status = 1;
static int bluetooth_status = 1;

module_param(wlan_status, int, 0444);
MODULE_PARM_DESC(wlan_status, "Set the wireless status on boot "
		 "(0 = disabled, 1 = enabled, -1 = don't do anything). "
		 "default is 1");

module_param(bluetooth_status, int, 0444);
MODULE_PARM_DESC(bluetooth_status, "Set the wireless status on boot "
		 "(0 = disabled, 1 = enabled, -1 = don't do anything). "
		 "default is 1");

/*
 * Some events we use, same for all Asus
 */
#define ATKD_BR_UP	0x10	/* (event & ~ATKD_BR_UP) = brightness level */
#define ATKD_BR_DOWN	0x20	/* (event & ~ATKD_BR_DOWN) = britghness level */
#define ATKD_BR_MIN	ATKD_BR_UP
#define ATKD_BR_MAX	(ATKD_BR_DOWN | 0xF)	/* 0x2f */
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
#define METHOD_WL_STATUS	"RSTS"

/* Brightness */
#define METHOD_BRIGHTNESS_SET	"SPLV"
#define METHOD_BRIGHTNESS_GET	"GPLV"

/* Backlight */
static acpi_handle lcd_switch_handle;
static char *lcd_switch_paths[] = {
  "\\_SB.PCI0.SBRG.EC0._Q10",	/* All new models */
  "\\_SB.PCI0.ISA.EC0._Q10",	/* A1x */
  "\\_SB.PCI0.PX40.ECD0._Q10",	/* L3C */
  "\\_SB.PCI0.PX40.EC0.Q10",	/* M1A */
  "\\_SB.PCI0.LPCB.EC0._Q10",	/* P30 */
  "\\_SB.PCI0.LPCB.EC0._Q0E", /* P30/P35 */
  "\\_SB.PCI0.PX40.Q10",	/* S1x */
  "\\Q10"};		/* A2x, L2D, L3D, M2E */

/* Display */
#define METHOD_SWITCH_DISPLAY	"SDSP"

static acpi_handle display_get_handle;
static char *display_get_paths[] = {
  /* A6B, A6K A6R A7D F3JM L4R M6R A3G M6A M6V VX-1 V6J V6V W3Z */
  "\\_SB.PCI0.P0P1.VGA.GETD",
  /* A3E A4K, A4D A4L A6J A7J A8J Z71V M9V S5A M5A z33A W1Jc W2V G1 */
  "\\_SB.PCI0.P0P2.VGA.GETD",
  /* A6V A6Q */
  "\\_SB.PCI0.P0P3.VGA.GETD",
  /* A6T, A6M */
  "\\_SB.PCI0.P0PA.VGA.GETD",
  /* L3C */
  "\\_SB.PCI0.PCI1.VGAC.NMAP",
  /* Z96F */
  "\\_SB.PCI0.VGA.GETD",
  /* A2D */
  "\\ACTD",
  /* A4G Z71A W1N W5A W5F M2N M3N M5N M6N S1N S5N */
  "\\ADVG",
  /* P30 */
  "\\DNXT",
  /* A2H D1 L2D L3D L3H L2E L5D L5C M1A M2E L4L W3V */
  "\\INFB",
  /* A3F A6F A3N A3L M6N W3N W6A */
  "\\SSTE"};

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

	struct asus_led mled;
	struct asus_led tled;
	struct asus_led rled;
	struct asus_led pled;
	struct asus_led gled;
	struct asus_led kled;
	struct workqueue_struct *led_workqueue;

	int wireless_status;
	bool have_rsts;
	int lcd_state;

	struct rfkill *gps_rfkill;

	acpi_handle handle;	/* the handle of the hotk device */
	u32 ledd_status;	/* status of the LED display */
	u8 light_level;		/* light sensor level */
	u8 light_switch;	/* light sensor switch value */
	u16 event_count[128];	/* count for each event TODO make this better */
	u16 *keycode_map;
};

static const struct key_entry asus_keymap[] = {
	/* Lenovo SL Specific keycodes */
	{KE_KEY, 0x02, { KEY_SCREENLOCK } },
	{KE_KEY, 0x05, { KEY_WLAN } },
	{KE_KEY, 0x08, { KEY_F13 } },
	{KE_KEY, 0x17, { KEY_ZOOM } },
	{KE_KEY, 0x1f, { KEY_BATTERY } },
	/* End of Lenovo SL Specific keycodes */
	{KE_KEY, 0x30, { KEY_VOLUMEUP } },
	{KE_KEY, 0x31, { KEY_VOLUMEDOWN } },
	{KE_KEY, 0x32, { KEY_MUTE } },
	{KE_KEY, 0x33, { KEY_SWITCHVIDEOMODE } },
	{KE_KEY, 0x34, { KEY_SWITCHVIDEOMODE } },
	{KE_KEY, 0x40, { KEY_PREVIOUSSONG } },
	{KE_KEY, 0x41, { KEY_NEXTSONG } },
	{KE_KEY, 0x43, { KEY_STOPCD } },
	{KE_KEY, 0x45, { KEY_PLAYPAUSE } },
	{KE_KEY, 0x4c, { KEY_MEDIA } },
	{KE_KEY, 0x50, { KEY_EMAIL } },
	{KE_KEY, 0x51, { KEY_WWW } },
	{KE_KEY, 0x55, { KEY_CALC } },
	{KE_KEY, 0x5C, { KEY_SCREENLOCK } },  /* Screenlock */
	{KE_KEY, 0x5D, { KEY_WLAN } },
	{KE_KEY, 0x5E, { KEY_WLAN } },
	{KE_KEY, 0x5F, { KEY_WLAN } },
	{KE_KEY, 0x60, { KEY_SWITCHVIDEOMODE } },
	{KE_KEY, 0x61, { KEY_SWITCHVIDEOMODE } },
	{KE_KEY, 0x62, { KEY_SWITCHVIDEOMODE } },
	{KE_KEY, 0x63, { KEY_SWITCHVIDEOMODE } },
	{KE_KEY, 0x6B, { KEY_F13 } }, /* Lock Touchpad */
	{KE_KEY, 0x7E, { KEY_BLUETOOTH } },
	{KE_KEY, 0x7D, { KEY_BLUETOOTH } },
	{KE_KEY, 0x82, { KEY_CAMERA } },
	{KE_KEY, 0x88, { KEY_WLAN  } },
	{KE_KEY, 0x8A, { KEY_PROG1 } },
	{KE_KEY, 0x95, { KEY_MEDIA } },
	{KE_KEY, 0x99, { KEY_PHONE } },
	{KE_KEY, 0xc4, { KEY_KBDILLUMUP } },
	{KE_KEY, 0xc5, { KEY_KBDILLUMDOWN } },
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
			pr_warning("Error finding %s\n", method);
		return -ENODEV;
	}
	return 0;
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
		pr_warning("Error reading kled level\n");
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
		pr_warning("Keyboard LED display write failed\n");
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
	if (asus->mled.led.dev)
		led_classdev_unregister(&asus->mled.led);
	if (asus->tled.led.dev)
		led_classdev_unregister(&asus->tled.led);
	if (asus->pled.led.dev)
		led_classdev_unregister(&asus->pled.led);
	if (asus->rled.led.dev)
		led_classdev_unregister(&asus->rled.led);
	if (asus->gled.led.dev)
		led_classdev_unregister(&asus->gled.led);
	if (asus->kled.led.dev)
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
	int r;

	/*
	 * Functions that actually update the LED's are called from a
	 * workqueue. By doing this as separate work rather than when the LED
	 * subsystem asks, we avoid messing with the Asus ACPI stuff during a
	 * potentially bad time, such as a timer interrupt.
	 */
	asus->led_workqueue = create_singlethread_workqueue("led_workqueue");
	if (!asus->led_workqueue)
		return -ENOMEM;

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
static int asus_lcd_status(struct asus_laptop *asus)
{
	return asus->lcd_state;
}

static int asus_lcd_set(struct asus_laptop *asus, int value)
{
	int lcd = 0;
	acpi_status status = 0;

	lcd = !!value;

	if (lcd == asus_lcd_status(asus))
		return 0;

	if (!lcd_switch_handle)
		return -ENODEV;

	status = acpi_evaluate_object(lcd_switch_handle,
				      NULL, NULL, NULL);

	if (ACPI_FAILURE(status)) {
		pr_warning("Error switching LCD\n");
		return -ENODEV;
	}

	asus->lcd_state = lcd;
	return 0;
}

static void lcd_blank(struct asus_laptop *asus, int blank)
{
	struct backlight_device *bd = asus->backlight_device;

	asus->lcd_state = (blank == FB_BLANK_UNBLANK);

	if (bd) {
		bd->props.power = blank;
		backlight_update_status(bd);
	}
}

static int asus_read_brightness(struct backlight_device *bd)
{
	struct asus_laptop *asus = bl_get_data(bd);
	unsigned long long value;
	acpi_status rv = AE_OK;

	rv = acpi_evaluate_integer(asus->handle, METHOD_BRIGHTNESS_GET,
				   NULL, &value);
	if (ACPI_FAILURE(rv))
		pr_warning("Error reading brightness\n");

	return value;
}

static int asus_set_brightness(struct backlight_device *bd, int value)
{
	struct asus_laptop *asus = bl_get_data(bd);

	if (write_acpi_int(asus->handle, METHOD_BRIGHTNESS_SET, value)) {
		pr_warning("Error changing brightness\n");
		return -EIO;
	}
	return 0;
}

static int update_bl_status(struct backlight_device *bd)
{
	struct asus_laptop *asus = bl_get_data(bd);
	int rv;
	int value = bd->props.brightness;

	rv = asus_set_brightness(bd, value);
	if (rv)
		return rv;

	value = (bd->props.power == FB_BLANK_UNBLANK) ? 1 : 0;
	return asus_lcd_set(asus, value);
}

static struct backlight_ops asusbl_ops = {
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
	struct device *dev = &asus->platform_device->dev;
	struct backlight_properties props;

	if (!acpi_check_handle(asus->handle, METHOD_BRIGHTNESS_GET, NULL) &&
	    !acpi_check_handle(asus->handle, METHOD_BRIGHTNESS_SET, NULL) &&
	    lcd_switch_handle) {
		memset(&props, 0, sizeof(struct backlight_properties));
		props.max_brightness = 15;

		bd = backlight_device_register(ASUS_LAPTOP_FILE, dev,
					       asus, &asusbl_ops, &props);
		if (IS_ERR(bd)) {
			pr_err("Could not register asus backlight device\n");
			asus->backlight_device = NULL;
			return PTR_ERR(bd);
		}

		asus->backlight_device = bd;

		bd->props.power = FB_BLANK_UNBLANK;
		bd->props.brightness = asus_read_brightness(bd);
		backlight_update_status(bd);
	}
	return 0;
}

static void asus_backlight_exit(struct asus_laptop *asus)
{
	if (asus->backlight_device)
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
static ssize_t show_infos(struct device *dev,
			  struct device_attribute *attr, char *page)
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
	 * The significance of others is yet to be found.
	 * If we don't find the method, we assume the device are present.
	 */
	rv = acpi_evaluate_integer(asus->handle, "HRWS", NULL, &temp);
	if (!ACPI_FAILURE(rv))
		len += sprintf(page + len, "HRWS value         : %#x\n",
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
static ssize_t show_ledd(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sprintf(buf, "0x%08x\n", asus->ledd_status);
}

static ssize_t store_ledd(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);
	int rv, value;

	rv = parse_arg(buf, count, &value);
	if (rv > 0) {
		if (write_acpi_int(asus->handle, METHOD_LEDD, value)) {
			pr_warning("LED display write failed\n");
			return -ENODEV;
		}
		asus->ledd_status = (u32) value;
	}
	return rv;
}

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
		pr_warning("Error reading Wireless status\n");
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
		pr_warning("Error setting wlan status to %d", status);
		return -EIO;
	}
	return 0;
}

static ssize_t show_wlan(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", asus_wireless_status(asus, WL_RSTS));
}

static ssize_t store_wlan(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sysfs_acpi_set(asus, buf, count, METHOD_WLAN);
}

/*
 * Bluetooth
 */
static int asus_bluetooth_set(struct asus_laptop *asus, int status)
{
	if (write_acpi_int(asus->handle, METHOD_BLUETOOTH, !!status)) {
		pr_warning("Error setting bluetooth status to %d", status);
		return -EIO;
	}
	return 0;
}

static ssize_t show_bluetooth(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", asus_wireless_status(asus, BT_RSTS));
}

static ssize_t store_bluetooth(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sysfs_acpi_set(asus, buf, count, METHOD_BLUETOOTH);
}

/*
 * Display
 */
static void asus_set_display(struct asus_laptop *asus, int value)
{
	/* no sanity check needed for now */
	if (write_acpi_int(asus->handle, METHOD_SWITCH_DISPLAY, value))
		pr_warning("Error setting display\n");
	return;
}

static int read_display(struct asus_laptop *asus)
{
	unsigned long long value = 0;
	acpi_status rv = AE_OK;

	/*
	 * In most of the case, we know how to set the display, but sometime
	 * we can't read it
	 */
	if (display_get_handle) {
		rv = acpi_evaluate_integer(display_get_handle, NULL,
					   NULL, &value);
		if (ACPI_FAILURE(rv))
			pr_warning("Error reading display status\n");
	}

	value &= 0x0F; /* needed for some models, shouldn't hurt others */

	return value;
}

/*
 * Now, *this* one could be more user-friendly, but so far, no-one has
 * complained. The significance of bits is the same as in store_disp()
 */
static ssize_t show_disp(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	if (!display_get_handle)
		return -ENODEV;
	return sprintf(buf, "%d\n", read_display(asus));
}

/*
 * Experimental support for display switching. As of now: 1 should activate
 * the LCD output, 2 should do for CRT, 4 for TV-Out and 8 for DVI.
 * Any combination (bitwise) of these will suffice. I never actually tested 4
 * displays hooked up simultaneously, so be warned. See the acpi4asus README
 * for more info.
 */
static ssize_t store_disp(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);
	int rv, value;

	rv = parse_arg(buf, count, &value);
	if (rv > 0)
		asus_set_display(asus, value);
	return rv;
}

/*
 * Light Sens
 */
static void asus_als_switch(struct asus_laptop *asus, int value)
{
	if (write_acpi_int(asus->handle, METHOD_ALS_CONTROL, value))
		pr_warning("Error setting light sensor switch\n");
	asus->light_switch = value;
}

static ssize_t show_lssw(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", asus->light_switch);
}

static ssize_t store_lssw(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);
	int rv, value;

	rv = parse_arg(buf, count, &value);
	if (rv > 0)
		asus_als_switch(asus, value ? 1 : 0);

	return rv;
}

static void asus_als_level(struct asus_laptop *asus, int value)
{
	if (write_acpi_int(asus->handle, METHOD_ALS_LEVEL, value))
		pr_warning("Error setting light sensor level\n");
	asus->light_level = value;
}

static ssize_t show_lslvl(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", asus->light_level);
}

static ssize_t store_lslvl(struct device *dev, struct device_attribute *attr,
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
		pr_warning("Error reading GPS status\n");
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

static ssize_t show_gps(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct asus_laptop *asus = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", asus_gps_status(asus));
}

static ssize_t store_gps(struct device *dev, struct device_attribute *attr,
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
	rfkill_set_sw_state(asus->gps_rfkill, !value);
	return rv;
}

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

static void asus_rfkill_exit(struct asus_laptop *asus)
{
	if (asus->gps_rfkill) {
		rfkill_unregister(asus->gps_rfkill);
		rfkill_destroy(asus->gps_rfkill);
		asus->gps_rfkill = NULL;
	}
}

static int asus_rfkill_init(struct asus_laptop *asus)
{
	int result;

	if (acpi_check_handle(asus->handle, METHOD_GPS_ON, NULL) ||
	    acpi_check_handle(asus->handle, METHOD_GPS_OFF, NULL) ||
	    acpi_check_handle(asus->handle, METHOD_GPS_STATUS, NULL))
		return 0;

	asus->gps_rfkill = rfkill_alloc("asus-gps", &asus->platform_device->dev,
					RFKILL_TYPE_GPS,
					&asus_gps_rfkill_ops, asus);
	if (!asus->gps_rfkill)
		return -EINVAL;

	result = rfkill_register(asus->gps_rfkill);
	if (result) {
		rfkill_destroy(asus->gps_rfkill);
		asus->gps_rfkill = NULL;
	}

	return result;
}

/*
 * Input device (i.e. hotkeys)
 */
static void asus_input_notify(struct asus_laptop *asus, int event)
{
	if (asus->inputdev)
		sparse_keymap_report_event(asus->inputdev, event, 1, true);
}

static int asus_input_init(struct asus_laptop *asus)
{
	struct input_dev *input;
	int error;

	input = input_allocate_device();
	if (!input) {
		pr_info("Unable to allocate input device\n");
		return -ENOMEM;
	}
	input->name = "Asus Laptop extra buttons";
	input->phys = ASUS_LAPTOP_FILE "/input0";
	input->id.bustype = BUS_HOST;
	input->dev.parent = &asus->platform_device->dev;
	input_set_drvdata(input, asus);

	error = sparse_keymap_setup(input, asus_keymap, NULL);
	if (error) {
		pr_err("Unable to setup input device keymap\n");
		goto err_free_dev;
	}
	error = input_register_device(input);
	if (error) {
		pr_info("Unable to register input device\n");
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
}

/*
 * ACPI driver
 */
static void asus_acpi_notify(struct acpi_device *device, u32 event)
{
	struct asus_laptop *asus = acpi_driver_data(device);
	u16 count;

	/*
	 * We need to tell the backlight device when the backlight power is
	 * switched
	 */
	if (event == ATKD_LCD_ON)
		lcd_blank(asus, FB_BLANK_UNBLANK);
	else if (event == ATKD_LCD_OFF)
		lcd_blank(asus, FB_BLANK_POWERDOWN);

	/* TODO Find a better way to handle events count. */
	count = asus->event_count[event % 128]++;
	acpi_bus_generate_proc_event(asus->device, event, count);
	acpi_bus_generate_netlink_event(asus->device->pnp.device_class,
					dev_name(&asus->device->dev), event,
					count);

	/* Brightness events are special */
	if (event >= ATKD_BR_MIN && event <= ATKD_BR_MAX) {

		/* Ignore them completely if the acpi video driver is used */
		if (asus->backlight_device != NULL) {
			/* Update the backlight device. */
			asus_backlight_notify(asus);
		}
		return ;
	}
	asus_input_notify(asus, event);
}

static DEVICE_ATTR(infos, S_IRUGO, show_infos, NULL);
static DEVICE_ATTR(wlan, S_IRUGO | S_IWUSR, show_wlan, store_wlan);
static DEVICE_ATTR(bluetooth, S_IRUGO | S_IWUSR, show_bluetooth,
		   store_bluetooth);
static DEVICE_ATTR(display, S_IRUGO | S_IWUSR, show_disp, store_disp);
static DEVICE_ATTR(ledd, S_IRUGO | S_IWUSR, show_ledd, store_ledd);
static DEVICE_ATTR(ls_level, S_IRUGO | S_IWUSR, show_lslvl, store_lslvl);
static DEVICE_ATTR(ls_switch, S_IRUGO | S_IWUSR, show_lssw, store_lssw);
static DEVICE_ATTR(gps, S_IRUGO | S_IWUSR, show_gps, store_gps);

static void asus_sysfs_exit(struct asus_laptop *asus)
{
	struct platform_device *device = asus->platform_device;

	device_remove_file(&device->dev, &dev_attr_infos);
	device_remove_file(&device->dev, &dev_attr_wlan);
	device_remove_file(&device->dev, &dev_attr_bluetooth);
	device_remove_file(&device->dev, &dev_attr_display);
	device_remove_file(&device->dev, &dev_attr_ledd);
	device_remove_file(&device->dev, &dev_attr_ls_switch);
	device_remove_file(&device->dev, &dev_attr_ls_level);
	device_remove_file(&device->dev, &dev_attr_gps);
}

static int asus_sysfs_init(struct asus_laptop *asus)
{
	struct platform_device *device = asus->platform_device;
	int err;

	err = device_create_file(&device->dev, &dev_attr_infos);
	if (err)
		return err;

	if (!acpi_check_handle(asus->handle, METHOD_WLAN, NULL)) {
		err = device_create_file(&device->dev, &dev_attr_wlan);
		if (err)
			return err;
	}

	if (!acpi_check_handle(asus->handle, METHOD_BLUETOOTH, NULL)) {
		err = device_create_file(&device->dev, &dev_attr_bluetooth);
		if (err)
			return err;
	}

	if (!acpi_check_handle(asus->handle, METHOD_SWITCH_DISPLAY, NULL)) {
		err = device_create_file(&device->dev, &dev_attr_display);
		if (err)
			return err;
	}

	if (!acpi_check_handle(asus->handle, METHOD_LEDD, NULL)) {
		err = device_create_file(&device->dev, &dev_attr_ledd);
		if (err)
			return err;
	}

	if (!acpi_check_handle(asus->handle, METHOD_ALS_CONTROL, NULL) &&
	    !acpi_check_handle(asus->handle, METHOD_ALS_LEVEL, NULL)) {
		err = device_create_file(&device->dev, &dev_attr_ls_switch);
		if (err)
			return err;
		err = device_create_file(&device->dev, &dev_attr_ls_level);
		if (err)
			return err;
	}

	if (!acpi_check_handle(asus->handle, METHOD_GPS_ON, NULL) &&
	    !acpi_check_handle(asus->handle, METHOD_GPS_OFF, NULL) &&
	    !acpi_check_handle(asus->handle, METHOD_GPS_STATUS, NULL)) {
		err = device_create_file(&device->dev, &dev_attr_gps);
		if (err)
			return err;
	}

	return err;
}

static int asus_platform_init(struct asus_laptop *asus)
{
	int err;

	asus->platform_device = platform_device_alloc(ASUS_LAPTOP_FILE, -1);
	if (!asus->platform_device)
		return -ENOMEM;
	platform_set_drvdata(asus->platform_device, asus);

	err = platform_device_add(asus->platform_device);
	if (err)
		goto fail_platform_device;

	err = asus_sysfs_init(asus);
	if (err)
		goto fail_sysfs;
	return 0;

fail_sysfs:
	asus_sysfs_exit(asus);
	platform_device_del(asus->platform_device);
fail_platform_device:
	platform_device_put(asus->platform_device);
	return err;
}

static void asus_platform_exit(struct asus_laptop *asus)
{
	asus_sysfs_exit(asus);
	platform_device_unregister(asus->platform_device);
}

static struct platform_driver platform_driver = {
	.driver = {
		.name = ASUS_LAPTOP_FILE,
		.owner = THIS_MODULE,
	}
};

static int asus_handle_init(char *name, acpi_handle * handle,
			    char **paths, int num_paths)
{
	int i;
	acpi_status status;

	for (i = 0; i < num_paths; i++) {
		status = acpi_get_handle(NULL, paths[i], handle);
		if (ACPI_SUCCESS(status))
			return 0;
	}

	*handle = NULL;
	return -ENODEV;
}

#define ASUS_HANDLE_INIT(object)					\
	asus_handle_init(#object, &object##_handle, object##_paths,	\
			 ARRAY_SIZE(object##_paths))

/*
 * This function is used to initialize the context with right values. In this
 * method, we can make all the detection we want, and modify the asus_laptop
 * struct
 */
static int asus_laptop_get_info(struct asus_laptop *asus)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *model = NULL;
	unsigned long long bsts_result, hwrs_result;
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
		pr_warning("Couldn't get the DSDT table header\n");

	/* We have to write 0 on init this far for all ASUS models */
	if (write_acpi_int_ret(asus->handle, "INIT", 0, &buffer)) {
		pr_err("Hotkey initialization failed\n");
		return -ENODEV;
	}

	/* This needs to be called for some laptops to init properly */
	status =
	    acpi_evaluate_integer(asus->handle, "BSTS", NULL, &bsts_result);
	if (ACPI_FAILURE(status))
		pr_warning("Error calling BSTS\n");
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

	if (*string)
		pr_notice("  %s model detected\n", string);

	/*
	 * The HWRS method return informations about the hardware.
	 * 0x80 bit is for WLAN, 0x100 for Bluetooth.
	 * The significance of others is yet to be found.
	 */
	status =
	    acpi_evaluate_integer(asus->handle, "HRWS", NULL, &hwrs_result);
	if (!ACPI_FAILURE(status))
		pr_notice("  HRWS returned %x", (int)hwrs_result);

	if (!acpi_check_handle(asus->handle, METHOD_WL_STATUS, NULL))
		asus->have_rsts = true;

	/* Scheduled for removal */
	ASUS_HANDLE_INIT(lcd_switch);
	ASUS_HANDLE_INIT(display_get);

	kfree(model);

	return AE_OK;
}

static bool asus_device_present;

static int __devinit asus_acpi_init(struct asus_laptop *asus)
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

	/* WLED and BLED are on by default */
	if (bluetooth_status >= 0)
		asus_bluetooth_set(asus, !!bluetooth_status);

	if (wlan_status >= 0)
		asus_wlan_set(asus, !!wlan_status);

	/* Keyboard Backlight is on by default */
	if (!acpi_check_handle(asus->handle, METHOD_KBD_LIGHT_SET, NULL))
		asus_kled_set(asus, 1);

	/* LED display is off by default */
	asus->ledd_status = 0xFFF;

	/* Set initial values of light sensor and level */
	asus->light_switch = 0;	/* Default to light sensor disabled */
	asus->light_level = 5;	/* level 5 for sensor sensitivity */

	if (!acpi_check_handle(asus->handle, METHOD_ALS_CONTROL, NULL) &&
	    !acpi_check_handle(asus->handle, METHOD_ALS_LEVEL, NULL)) {
		asus_als_switch(asus, asus->light_switch);
		asus_als_level(asus, asus->light_level);
	}

	asus->lcd_state = 1; /* LCD should be on when the module load */
	return result;
}

static int __devinit asus_acpi_add(struct acpi_device *device)
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

	result = asus_acpi_init(asus);
	if (result)
		goto fail_platform;

	/*
	 * Register the platform device first.  It is used as a parent for the
	 * sub-devices below.
	 */
	result = asus_platform_init(asus);
	if (result)
		goto fail_platform;

	if (!acpi_video_backlight_support()) {
		result = asus_backlight_init(asus);
		if (result)
			goto fail_backlight;
	} else
		pr_info("Backlight controlled by ACPI video driver\n");

	result = asus_input_init(asus);
	if (result)
		goto fail_input;

	result = asus_led_init(asus);
	if (result)
		goto fail_led;

	result = asus_rfkill_init(asus);
	if (result)
		goto fail_rfkill;

	asus_device_present = true;
	return 0;

fail_rfkill:
	asus_led_exit(asus);
fail_led:
	asus_input_exit(asus);
fail_input:
	asus_backlight_exit(asus);
fail_backlight:
	asus_platform_exit(asus);
fail_platform:
	kfree(asus->name);
	kfree(asus);

	return result;
}

static int asus_acpi_remove(struct acpi_device *device, int type)
{
	struct asus_laptop *asus = acpi_driver_data(device);

	asus_backlight_exit(asus);
	asus_rfkill_exit(asus);
	asus_led_exit(asus);
	asus_input_exit(asus);
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
