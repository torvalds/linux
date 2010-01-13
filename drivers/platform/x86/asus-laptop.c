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
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>
#include <asm/uaccess.h>
#include <linux/input.h>

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
module_param(wapf, uint, 0644);
MODULE_PARM_DESC(wapf, "WAPF value");

static uint wireless_status = 1;
static uint bluetooth_status = 1;

module_param(wireless_status, uint, 0644);
MODULE_PARM_DESC(wireless_status, "Set the wireless status on boot "
		 "(0 = disabled, 1 = enabled, -1 = don't do anything). "
		 "default is 1");

module_param(bluetooth_status, uint, 0644);
MODULE_PARM_DESC(bluetooth_status, "Set the wireless status on boot "
		 "(0 = disabled, 1 = enabled, -1 = don't do anything). "
		 "default is 1");

/*
 * Some events we use, same for all Asus
 */
#define ATKD_BR_UP	0x10
#define ATKD_BR_DOWN	0x20
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

#define ASUS_HANDLE(object, paths...)					\
	static acpi_handle  object##_handle = NULL;			\
	static char *object##_paths[] = { paths }

/* LED */
ASUS_HANDLE(mled_set, ASUS_LAPTOP_PREFIX "MLED");
ASUS_HANDLE(tled_set, ASUS_LAPTOP_PREFIX "TLED");
ASUS_HANDLE(rled_set, ASUS_LAPTOP_PREFIX "RLED");	/* W1JC */
ASUS_HANDLE(pled_set, ASUS_LAPTOP_PREFIX "PLED");	/* A7J */
ASUS_HANDLE(gled_set, ASUS_LAPTOP_PREFIX "GLED");	/* G1, G2 (probably) */

/* LEDD */
ASUS_HANDLE(ledd_set, ASUS_LAPTOP_PREFIX "SLCM");

/*
 * Bluetooth and WLAN
 * WLED and BLED are not handled like other XLED, because in some dsdt
 * they also control the WLAN/Bluetooth device.
 */
ASUS_HANDLE(wl_switch, ASUS_LAPTOP_PREFIX "WLED");
ASUS_HANDLE(bt_switch, ASUS_LAPTOP_PREFIX "BLED");
ASUS_HANDLE(wireless_status, ASUS_LAPTOP_PREFIX "RSTS");	/* All new models */

/* Brightness */
ASUS_HANDLE(brightness_set, ASUS_LAPTOP_PREFIX "SPLV");
ASUS_HANDLE(brightness_get, ASUS_LAPTOP_PREFIX "GPLV");

/* Backlight */
ASUS_HANDLE(lcd_switch, "\\_SB.PCI0.SBRG.EC0._Q10",	/* All new models */
	    "\\_SB.PCI0.ISA.EC0._Q10",	/* A1x */
	    "\\_SB.PCI0.PX40.ECD0._Q10",	/* L3C */
	    "\\_SB.PCI0.PX40.EC0.Q10",	/* M1A */
	    "\\_SB.PCI0.LPCB.EC0._Q10",	/* P30 */
	    "\\_SB.PCI0.LPCB.EC0._Q0E", /* P30/P35 */
	    "\\_SB.PCI0.PX40.Q10",	/* S1x */
	    "\\Q10");		/* A2x, L2D, L3D, M2E */

/* Display */
ASUS_HANDLE(display_set, ASUS_LAPTOP_PREFIX "SDSP");
ASUS_HANDLE(display_get,
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
	    "\\SSTE");

ASUS_HANDLE(ls_switch, ASUS_LAPTOP_PREFIX "ALSC"); /* Z71A Z71V */
ASUS_HANDLE(ls_level, ASUS_LAPTOP_PREFIX "ALSL");	 /* Z71A Z71V */

/* GPS */
/* R2H use different handle for GPS on/off */
ASUS_HANDLE(gps_on, ASUS_LAPTOP_PREFIX "SDON");	/* R2H */
ASUS_HANDLE(gps_off, ASUS_LAPTOP_PREFIX "SDOF");	/* R2H */
ASUS_HANDLE(gps_status, ASUS_LAPTOP_PREFIX "GPST");

/* Keyboard light */
ASUS_HANDLE(kled_set, ASUS_LAPTOP_PREFIX "SLKB");
ASUS_HANDLE(kled_get, ASUS_LAPTOP_PREFIX "GLKB");

/*
 * Define a specific led structure to keep the main structure clean
 */
#define ASUS_DEFINE_LED(object)					\
	int object##_wk;					\
	struct work_struct object##_work;			\
	struct led_classdev object;


#define led_to_asus(led_cdev, led)					\
	container_of(container_of(led_cdev, struct asus_laptop_leds,	\
				  led),					\
		     struct asus_laptop, leds)
#define work_to_asus(work, led)						\
	container_of(container_of(work, struct asus_laptop_leds,	\
				  led##_work),				\
		     struct asus_laptop, leds)

struct asus_laptop_leds {
	ASUS_DEFINE_LED(mled)
	ASUS_DEFINE_LED(tled)
	ASUS_DEFINE_LED(rled)
	ASUS_DEFINE_LED(pled)
	ASUS_DEFINE_LED(gled)
	ASUS_DEFINE_LED(kled)
	struct workqueue_struct *workqueue;
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

	struct asus_laptop_leds leds;

	int wireless_status;
	bool have_rsts;
	int lcd_state;

	acpi_handle handle;	/* the handle of the hotk device */
	u32 ledd_status;	/* status of the LED display */
	u8 light_level;		/* light sensor level */
	u8 light_switch;	/* light sensor switch value */
	u16 event_count[128];	/* count for each event TODO make this better */
	u16 *keycode_map;
};

struct key_entry {
	char type;
	u8 code;
	u16 keycode;
};

enum { KE_KEY, KE_END };

static const struct key_entry asus_keymap[] = {
	{KE_KEY, 0x02, KEY_SCREENLOCK},
	{KE_KEY, 0x05, KEY_WLAN},
	{KE_KEY, 0x08, KEY_F13},
	{KE_KEY, 0x17, KEY_ZOOM},
	{KE_KEY, 0x1f, KEY_BATTERY},
	{KE_KEY, 0x30, KEY_VOLUMEUP},
	{KE_KEY, 0x31, KEY_VOLUMEDOWN},
	{KE_KEY, 0x32, KEY_MUTE},
	{KE_KEY, 0x33, KEY_SWITCHVIDEOMODE},
	{KE_KEY, 0x34, KEY_SWITCHVIDEOMODE},
	{KE_KEY, 0x40, KEY_PREVIOUSSONG},
	{KE_KEY, 0x41, KEY_NEXTSONG},
	{KE_KEY, 0x43, KEY_STOPCD},
	{KE_KEY, 0x45, KEY_PLAYPAUSE},
	{KE_KEY, 0x4c, KEY_MEDIA},
	{KE_KEY, 0x50, KEY_EMAIL},
	{KE_KEY, 0x51, KEY_WWW},
	{KE_KEY, 0x55, KEY_CALC},
	{KE_KEY, 0x5C, KEY_SCREENLOCK},  /* Screenlock */
	{KE_KEY, 0x5D, KEY_WLAN},
	{KE_KEY, 0x5E, KEY_WLAN},
	{KE_KEY, 0x5F, KEY_WLAN},
	{KE_KEY, 0x60, KEY_SWITCHVIDEOMODE},
	{KE_KEY, 0x61, KEY_SWITCHVIDEOMODE},
	{KE_KEY, 0x62, KEY_SWITCHVIDEOMODE},
	{KE_KEY, 0x63, KEY_SWITCHVIDEOMODE},
	{KE_KEY, 0x6B, KEY_F13}, /* Lock Touchpad */
	{KE_KEY, 0x82, KEY_CAMERA},
	{KE_KEY, 0x88, KEY_WLAN },
	{KE_KEY, 0x8A, KEY_PROG1},
	{KE_KEY, 0x95, KEY_MEDIA},
	{KE_KEY, 0x99, KEY_PHONE},
	{KE_KEY, 0xc4, KEY_KBDILLUMUP},
	{KE_KEY, 0xc5, KEY_KBDILLUMDOWN},
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
		return 0;

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

/* Generic LED function */
static void asus_led_set(struct asus_laptop *asus, acpi_handle handle,
			 int value)
{
	if (handle == mled_set_handle)
		value = !value;
	else if (handle == gled_set_handle)
		value = !value + 1;
	else
		value = !!value;

	write_acpi_int(handle, NULL, value);
}

/*
 * LEDs
 */
#define ASUS_LED(object, ledname, max)					\
	static void object##_led_set(struct led_classdev *led_cdev,	\
				     enum led_brightness value);	\
	static enum led_brightness object##_led_get(			\
		struct led_classdev *led_cdev);				\
	static void object##_led_update(struct work_struct *ignored);	\
	static struct led_classdev object##_led = {			\
		.name           = "asus::" ledname,			\
		.brightness_set = object##_led_set,			\
		.brightness_get = object##_led_get,			\
		.max_brightness = max					\
	}

ASUS_LED(mled, "mail", 1);
ASUS_LED(tled, "touchpad", 1);
ASUS_LED(rled, "record", 1);
ASUS_LED(pled, "phone", 1);
ASUS_LED(gled, "gaming", 1);
ASUS_LED(kled, "kbd_backlight", 3);

/* /sys/class/led handlers */
#define ASUS_LED_HANDLER(object, mask)					\
	static void object##_led_set(struct led_classdev *led_cdev,	\
				     enum led_brightness value)		\
	{								\
		struct asus_laptop *asus =				\
			led_to_asus(led_cdev, object);			\
									\
		asus->leds.object##_wk = (value > 0) ? 1 : 0;		\
		queue_work(asus->leds.workqueue,			\
			   &asus->leds.object##_work);			\
	}								\
	static void object##_led_update(struct work_struct *work)	\
	{								\
		struct asus_laptop *asus = work_to_asus(work, object);	\
									\
		int value = asus->leds.object##_wk;			\
		asus_led_set(asus, object##_set_handle, value);		\
	}								\
	static enum led_brightness object##_led_get(			\
		struct led_classdev *led_cdev)				\
	{								\
		return led_cdev->brightness;				\
	}

ASUS_LED_HANDLER(mled, MLED_ON);
ASUS_LED_HANDLER(pled, PLED_ON);
ASUS_LED_HANDLER(rled, RLED_ON);
ASUS_LED_HANDLER(tled, TLED_ON);
ASUS_LED_HANDLER(gled, GLED_ON);

/*
 * Keyboard backlight (also a LED)
 */
static int asus_kled_lvl(void)
{
	unsigned long long kblv;
	struct acpi_object_list params;
	union acpi_object in_obj;
	acpi_status rv;

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = 2;

	rv = acpi_evaluate_integer(kled_get_handle, NULL, &params, &kblv);
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

	if (write_acpi_int(kled_set_handle, NULL, kblv)) {
		pr_warning("Keyboard LED display write failed\n");
		return -EINVAL;
	}
	return 0;
}

static void kled_led_set(struct led_classdev *led_cdev,
			 enum led_brightness value)
{
	struct asus_laptop *asus = led_to_asus(led_cdev, kled);

	asus->leds.kled_wk = value;
	queue_work(asus->leds.workqueue, &asus->leds.kled_work);
}

static void kled_led_update(struct work_struct *work)
{
	struct asus_laptop *asus = work_to_asus(work, kled);

	asus_kled_set(asus, asus->leds.kled_wk);
}

static enum led_brightness kled_led_get(struct led_classdev *led_cdev)
{
	return asus_kled_lvl();
}

#define ASUS_LED_UNREGISTER(object)				\
	if (object##_led.dev)					\
		led_classdev_unregister(&object##_led)

static void asus_led_exit(struct asus_laptop *asus)
{
	ASUS_LED_UNREGISTER(mled);
	ASUS_LED_UNREGISTER(tled);
	ASUS_LED_UNREGISTER(pled);
	ASUS_LED_UNREGISTER(rled);
	ASUS_LED_UNREGISTER(gled);
	ASUS_LED_UNREGISTER(kled);
	if (asus->leds.workqueue) {
		destroy_workqueue(asus->leds.workqueue);
		asus->leds.workqueue = NULL;
	}
}

/*  Ugly macro, need to fix that later */
#define ASUS_LED_REGISTER(asus, object, _name, max)			\
	do {								\
		struct led_classdev *ldev = &asus->leds.object;		\
		if (!object##_set_handle)				\
			break ;						\
									\
		INIT_WORK(&asus->leds.object##_work, object##_led_update); \
		ldev->name = "asus::" _name;				\
		ldev->brightness_set = object##_led_set;		\
		ldev->max_brightness = max;				\
		rv = led_classdev_register(&asus->platform_device->dev, ldev); \
		if (rv)							\
			goto error;					\
	} while (0)

static int asus_led_init(struct asus_laptop *asus)
{
	int rv;

	/*
	 * Functions that actually update the LED's are called from a
	 * workqueue. By doing this as separate work rather than when the LED
	 * subsystem asks, we avoid messing with the Asus ACPI stuff during a
	 * potentially bad time, such as a timer interrupt.
	 */
	asus->leds.workqueue = create_singlethread_workqueue("led_workqueue");
	if (!asus->leds.workqueue)
		return -ENOMEM;

	ASUS_LED_REGISTER(asus, mled, "mail", 1);
	ASUS_LED_REGISTER(asus, tled, "touchpad", 1);
	ASUS_LED_REGISTER(asus, rled, "record", 1);
	ASUS_LED_REGISTER(asus, pled, "phone", 1);
	ASUS_LED_REGISTER(asus, gled, "gaming", 1);
	if (kled_set_handle && kled_get_handle)
		ASUS_LED_REGISTER(asus, kled, "kbd_backlight", 3);
error:
	if (rv)
		asus_led_exit(asus);
	return rv;
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
	unsigned long long value;
	acpi_status rv = AE_OK;

	rv = acpi_evaluate_integer(brightness_get_handle, NULL, NULL, &value);
	if (ACPI_FAILURE(rv))
		pr_warning("Error reading brightness\n");

	return value;
}

static int asus_set_brightness(struct backlight_device *bd, int value)
{
	if (write_acpi_int(brightness_set_handle, NULL, value)) {
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

static int asus_backlight_init(struct asus_laptop *asus)
{
	struct backlight_device *bd;
	struct device *dev = &asus->platform_device->dev;

	if (brightness_set_handle && lcd_switch_handle) {
		bd = backlight_device_register(ASUS_LAPTOP_FILE, dev,
					       asus, &asusbl_ops);
		if (IS_ERR(bd)) {
			pr_err("Could not register asus backlight device\n");
			asus->backlight_device = NULL;
			return PTR_ERR(bd);
		}

		asus->backlight_device = bd;

		bd->props.max_brightness = 15;
		bd->props.brightness = asus_read_brightness(NULL);
		bd->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(bd);
	}
	return 0;
}

static void asus_backlight_exit(struct asus_laptop *asus)
{
	if (asus->backlight_device)
		backlight_device_unregister(asus->backlight_device);
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
	 * We use the easy way, we don't care of off and count, so we don't set eof
	 * to 1
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
			      acpi_handle handle)
{
	int rv, value;
	int out = 0;

	rv = parse_arg(buf, count, &value);
	if (rv > 0)
		out = value ? 1 : 0;

	if (write_acpi_int(handle, NULL, value))
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
		if (write_acpi_int(ledd_set_handle, NULL, value))
			pr_warning("LED display write failed\n");
		else
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

	rv = acpi_evaluate_integer(wireless_status_handle, NULL, NULL, &status);
	if (ACPI_FAILURE(rv)) {
		pr_warning("Error reading Wireless status\n");
		return -EINVAL;
	}
	return !!(status & mask);
}

/*
 * WLAN
 */
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

	return sysfs_acpi_set(asus, buf, count, wl_switch_handle);
}

/*
 * Bluetooth
 */
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

	return sysfs_acpi_set(asus, buf, count, bt_switch_handle);
}

/*
 * Display
 */
static void asus_set_display(struct asus_laptop *asus, int value)
{
	/* no sanity check needed for now */
	if (write_acpi_int(display_set_handle, NULL, value))
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

	value &= 0x0F;		/* needed for some models, shouldn't hurt others */

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
	if (write_acpi_int(ls_switch_handle, NULL, value))
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
	if (write_acpi_int(ls_level_handle, NULL, value))
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
 * TODO: use rfkill
 */
static int asus_gps_status(struct asus_laptop *asus)
{
	unsigned long long status;
	acpi_status rv = AE_OK;

	rv = acpi_evaluate_integer(gps_status_handle, NULL, NULL, &status);
	if (ACPI_FAILURE(rv)) {
		pr_warning("Error reading GPS status\n");
		return -ENODEV;
	}
	return !!status;
}

static int asus_gps_switch(struct asus_laptop *asus, int status)
{
	acpi_handle handle = status ? gps_on_handle : gps_off_handle;

	if (write_acpi_int(handle, NULL, 0x02))
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
	return rv;
}

/*
 * Input device (i.e. hotkeys)
 */
static struct key_entry *asus_get_entry_by_scancode(struct asus_laptop *asus,
						    int code)
{
	struct key_entry *key;

	for (key = asus->keymap; key->type != KE_END; key++)
		if (code == key->code)
			return key;

	return NULL;
}

static struct key_entry *asus_get_entry_by_keycode(struct asus_laptop *asus,
						   int code)
{
	struct key_entry *key;

	for (key = asus->keymap; key->type != KE_END; key++)
		if (code == key->keycode && key->type == KE_KEY)
			return key;

	return NULL;
}

static int asus_getkeycode(struct input_dev *dev, int scancode, int *keycode)
{
	struct asus_laptop *asus = input_get_drvdata(dev);
	struct key_entry *key = asus_get_entry_by_scancode(asus, scancode);

	if (key && key->type == KE_KEY) {
		*keycode = key->keycode;
		return 0;
	}

	return -EINVAL;
}

static int asus_setkeycode(struct input_dev *dev, int scancode, int keycode)
{
	struct asus_laptop *asus = input_get_drvdata(dev);
	struct key_entry *key;
	int old_keycode;

	if (keycode < 0 || keycode > KEY_MAX)
		return -EINVAL;

	key = asus_get_entry_by_scancode(asus, scancode);
	if (key && key->type == KE_KEY) {
		old_keycode = key->keycode;
		key->keycode = keycode;
		set_bit(keycode, dev->keybit);
		if (!asus_get_entry_by_keycode(asus, old_keycode))
			clear_bit(old_keycode, dev->keybit);
		return 0;
	}

	return -EINVAL;
}

static void asus_input_notify(struct asus_laptop *asus, int event)
{
	struct key_entry *key;

	key = asus_get_entry_by_scancode(asus, event);
	if (!key)
		return ;

	switch (key->type) {
	case KE_KEY:
		input_report_key(asus->inputdev, key->keycode, 1);
		input_sync(asus->inputdev);
		input_report_key(asus->inputdev, key->keycode, 0);
		input_sync(asus->inputdev);
		break;
	}
}

static int asus_input_init(struct asus_laptop *asus)
{
	const struct key_entry *key;
	int result;

	asus->inputdev = input_allocate_device();
	if (!asus->inputdev) {
		pr_info("Unable to allocate input device\n");
		return 0;
	}
	asus->inputdev->name = "Asus Laptop extra buttons";
	asus->inputdev->dev.parent = &asus->platform_device->dev;
	asus->inputdev->phys = ASUS_LAPTOP_FILE "/input0";
	asus->inputdev->id.bustype = BUS_HOST;
	asus->inputdev->getkeycode = asus_getkeycode;
	asus->inputdev->setkeycode = asus_setkeycode;
	input_set_drvdata(asus->inputdev, asus);

	asus->keymap = kmemdup(asus_keymap, sizeof(asus_keymap),
				GFP_KERNEL);
	for (key = asus->keymap; key->type != KE_END; key++) {
		switch (key->type) {
		case KE_KEY:
			set_bit(EV_KEY, asus->inputdev->evbit);
			set_bit(key->keycode, asus->inputdev->keybit);
			break;
		}
	}
	result = input_register_device(asus->inputdev);
	if (result) {
		pr_info("Unable to register input device\n");
		input_free_device(asus->inputdev);
	}
	return result;
}

static void asus_input_exit(struct asus_laptop *asus)
{
	if (asus->inputdev)
		input_unregister_device(asus->inputdev);
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

	asus_input_notify(asus, event);
}

#define ASUS_CREATE_DEVICE_ATTR(_name)					\
	struct device_attribute dev_attr_##_name = {			\
		.attr = {						\
			.name = __stringify(_name),			\
			.mode = 0 },					\
		.show   = NULL,						\
		.store  = NULL,						\
	}

#define ASUS_SET_DEVICE_ATTR(_name, _mode, _show, _store)		\
	do {								\
		dev_attr_##_name.attr.mode = _mode;			\
		dev_attr_##_name.show = _show;				\
		dev_attr_##_name.store = _store;			\
	} while(0)

static ASUS_CREATE_DEVICE_ATTR(infos);
static ASUS_CREATE_DEVICE_ATTR(wlan);
static ASUS_CREATE_DEVICE_ATTR(bluetooth);
static ASUS_CREATE_DEVICE_ATTR(display);
static ASUS_CREATE_DEVICE_ATTR(ledd);
static ASUS_CREATE_DEVICE_ATTR(ls_switch);
static ASUS_CREATE_DEVICE_ATTR(ls_level);
static ASUS_CREATE_DEVICE_ATTR(gps);

static struct attribute *asuspf_attributes[] = {
	&dev_attr_infos.attr,
	&dev_attr_wlan.attr,
	&dev_attr_bluetooth.attr,
	&dev_attr_display.attr,
	&dev_attr_ledd.attr,
	&dev_attr_ls_switch.attr,
	&dev_attr_ls_level.attr,
	&dev_attr_gps.attr,
	NULL
};

static struct attribute_group platform_attribute_group = {
	.attrs = asuspf_attributes
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
				    &platform_attribute_group);
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
	sysfs_remove_group(&asus->platform_device->dev.kobj,
			   &platform_attribute_group);
	platform_device_unregister(asus->platform_device);
}

static struct platform_driver platform_driver = {
	.driver = {
		.name = ASUS_LAPTOP_FILE,
		.owner = THIS_MODULE,
	}
};

static void asus_laptop_add_fs(struct asus_laptop *asus)
{
	ASUS_SET_DEVICE_ATTR(infos, 0444, show_infos, NULL);

	if (wl_switch_handle)
		ASUS_SET_DEVICE_ATTR(wlan, 0644, show_wlan, store_wlan);

	if (bt_switch_handle)
		ASUS_SET_DEVICE_ATTR(bluetooth, 0644,
				     show_bluetooth, store_bluetooth);

	if (display_set_handle && display_get_handle)
		ASUS_SET_DEVICE_ATTR(display, 0644, show_disp, store_disp);
	else if (display_set_handle)
		ASUS_SET_DEVICE_ATTR(display, 0200, NULL, store_disp);

	if (ledd_set_handle)
		ASUS_SET_DEVICE_ATTR(ledd, 0644, show_ledd, store_ledd);

	if (ls_switch_handle && ls_level_handle) {
		ASUS_SET_DEVICE_ATTR(ls_level, 0644, show_lslvl, store_lslvl);
		ASUS_SET_DEVICE_ATTR(ls_switch, 0644, show_lssw, store_lssw);
	}

	if (gps_status_handle && gps_on_handle && gps_off_handle)
		ASUS_SET_DEVICE_ATTR(gps, 0644, show_gps, store_gps);
}

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
	write_acpi_int(asus->handle, "CWAP", wapf);

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
	if (!asus->name)
		return -ENOMEM;

	if (*string)
		pr_notice("  %s model detected\n", string);

	ASUS_HANDLE_INIT(mled_set);
	ASUS_HANDLE_INIT(tled_set);
	ASUS_HANDLE_INIT(rled_set);
	ASUS_HANDLE_INIT(pled_set);
	ASUS_HANDLE_INIT(gled_set);

	ASUS_HANDLE_INIT(ledd_set);

	ASUS_HANDLE_INIT(kled_set);
	ASUS_HANDLE_INIT(kled_get);

	/*
	 * The HWRS method return informations about the hardware.
	 * 0x80 bit is for WLAN, 0x100 for Bluetooth.
	 * The significance of others is yet to be found.
	 * If we don't find the method, we assume the device are present.
	 */
	status =
	    acpi_evaluate_integer(asus->handle, "HRWS", NULL, &hwrs_result);
	if (ACPI_FAILURE(status))
		hwrs_result = WL_HWRS | BT_HWRS;

	if (hwrs_result & WL_HWRS)
		ASUS_HANDLE_INIT(wl_switch);
	if (hwrs_result & BT_HWRS)
		ASUS_HANDLE_INIT(bt_switch);

	if (!ASUS_HANDLE_INIT(wireless_status))
		asus->have_rsts = true;

	ASUS_HANDLE_INIT(brightness_set);
	ASUS_HANDLE_INIT(brightness_get);

	ASUS_HANDLE_INIT(lcd_switch);

	ASUS_HANDLE_INIT(display_set);
	ASUS_HANDLE_INIT(display_get);

	/*
	 * There is a lot of models with "ALSL", but a few get
	 * a real light sens, so we need to check it.
	 */
	if (!ASUS_HANDLE_INIT(ls_switch))
		ASUS_HANDLE_INIT(ls_level);

	ASUS_HANDLE_INIT(gps_on);
	ASUS_HANDLE_INIT(gps_off);
	ASUS_HANDLE_INIT(gps_status);

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

	asus_laptop_add_fs(asus);

	/* WLED and BLED are on by default */
	if (bluetooth_status >= 0)
		write_acpi_int(bt_switch_handle, NULL, !!bluetooth_status);
	if (wireless_status >= 0)
		write_acpi_int(wl_switch_handle, NULL, !!wireless_status);

	/* Keyboard Backlight is on by default */
	if (kled_set_handle)
		asus_kled_set(asus, 1);

	/* LED display is off by default */
	asus->ledd_status = 0xFFF;

	/* Set initial values of light sensor and level */
	asus->light_switch = 0;	/* Default to light sensor disabled */
	asus->light_level = 5;	/* level 5 for sensor sensitivity */

	if (ls_switch_handle)
		asus_als_switch(asus, asus->light_switch);

	if (ls_level_handle)
		asus_als_level(asus, asus->light_level);

	/* GPS is on by default */
	asus_gps_switch(asus, 1);
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

	asus_device_present = true;
	return 0;

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
