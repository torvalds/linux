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

#include <linux/autoconf.h>
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

#define ASUS_LAPTOP_VERSION "0.42"

#define ASUS_HOTK_NAME          "Asus Laptop Support"
#define ASUS_HOTK_CLASS         "hotkey"
#define ASUS_HOTK_DEVICE_NAME   "Hotkey"
#define ASUS_HOTK_FILE          "asus-laptop"
#define ASUS_HOTK_PREFIX        "\\_SB.ATKD."

/*
 * Some events we use, same for all Asus
 */
#define ATKD_BR_UP       0x10
#define ATKD_BR_DOWN     0x20
#define ATKD_LCD_ON      0x33
#define ATKD_LCD_OFF     0x34

/*
 * Known bits returned by \_SB.ATKD.HWRS
 */
#define WL_HWRS     0x80
#define BT_HWRS     0x100

/*
 * Flags for hotk status
 * WL_ON and BT_ON are also used for wireless_status()
 */
#define WL_ON       0x01	//internal Wifi
#define BT_ON       0x02	//internal Bluetooth
#define MLED_ON     0x04	//mail LED
#define TLED_ON     0x08	//touchpad LED
#define RLED_ON     0x10	//Record LED
#define PLED_ON     0x20	//Phone LED
#define GLED_ON     0x40	//Gaming LED
#define LCD_ON      0x80	//LCD backlight
#define GPS_ON      0x100	//GPS

#define ASUS_LOG    ASUS_HOTK_FILE ": "
#define ASUS_ERR    KERN_ERR    ASUS_LOG
#define ASUS_WARNING    KERN_WARNING    ASUS_LOG
#define ASUS_NOTICE KERN_NOTICE ASUS_LOG
#define ASUS_INFO   KERN_INFO   ASUS_LOG
#define ASUS_DEBUG  KERN_DEBUG  ASUS_LOG

MODULE_AUTHOR("Julien Lerouge, Karol Kozimor, Corentin Chary");
MODULE_DESCRIPTION(ASUS_HOTK_NAME);
MODULE_LICENSE("GPL");

/* WAPF defines the behavior of the Fn+Fx wlan key
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

#define ASUS_HANDLE(object, paths...)					\
	static acpi_handle  object##_handle = NULL;			\
	static char *object##_paths[] = { paths }

/* LED */
ASUS_HANDLE(mled_set, ASUS_HOTK_PREFIX "MLED");
ASUS_HANDLE(tled_set, ASUS_HOTK_PREFIX "TLED");
ASUS_HANDLE(rled_set, ASUS_HOTK_PREFIX "RLED");	/* W1JC */
ASUS_HANDLE(pled_set, ASUS_HOTK_PREFIX "PLED");	/* A7J */
ASUS_HANDLE(gled_set, ASUS_HOTK_PREFIX "GLED");	/* G1, G2 (probably) */

/* LEDD */
ASUS_HANDLE(ledd_set, ASUS_HOTK_PREFIX "SLCM");

/* Bluetooth and WLAN
 * WLED and BLED are not handled like other XLED, because in some dsdt
 * they also control the WLAN/Bluetooth device.
 */
ASUS_HANDLE(wl_switch, ASUS_HOTK_PREFIX "WLED");
ASUS_HANDLE(bt_switch, ASUS_HOTK_PREFIX "BLED");
ASUS_HANDLE(wireless_status, ASUS_HOTK_PREFIX "RSTS");	/* All new models */

/* Brightness */
ASUS_HANDLE(brightness_set, ASUS_HOTK_PREFIX "SPLV");
ASUS_HANDLE(brightness_get, ASUS_HOTK_PREFIX "GPLV");

/* Backlight */
ASUS_HANDLE(lcd_switch, "\\_SB.PCI0.SBRG.EC0._Q10",	/* All new models */
	    "\\_SB.PCI0.ISA.EC0._Q10",	/* A1x */
	    "\\_SB.PCI0.PX40.ECD0._Q10",	/* L3C */
	    "\\_SB.PCI0.PX40.EC0.Q10",	/* M1A */
	    "\\_SB.PCI0.LPCB.EC0._Q10",	/* P30 */
	    "\\_SB.PCI0.PX40.Q10",	/* S1x */
	    "\\Q10");		/* A2x, L2D, L3D, M2E */

/* Display */
ASUS_HANDLE(display_set, ASUS_HOTK_PREFIX "SDSP");
ASUS_HANDLE(display_get, "\\_SB.PCI0.P0P1.VGA.GETD",	/*  A6B, A6K A6R A7D F3JM L4R M6R A3G
							   M6A M6V VX-1 V6J V6V W3Z */
	    "\\_SB.PCI0.P0P2.VGA.GETD",	/* A3E A4K, A4D A4L A6J A7J A8J Z71V M9V
					   S5A M5A z33A W1Jc W2V G1 */
	    "\\_SB.PCI0.P0P3.VGA.GETD",	/* A6V A6Q */
	    "\\_SB.PCI0.P0PA.VGA.GETD",	/* A6T, A6M */
	    "\\_SB.PCI0.PCI1.VGAC.NMAP",	/* L3C */
	    "\\_SB.PCI0.VGA.GETD",	/* Z96F */
	    "\\ACTD",		/* A2D */
	    "\\ADVG",		/* A4G Z71A W1N W5A W5F M2N M3N M5N M6N S1N S5N */
	    "\\DNXT",		/* P30 */
	    "\\INFB",		/* A2H D1 L2D L3D L3H L2E L5D L5C M1A M2E L4L W3V */
	    "\\SSTE");		/* A3F A6F A3N A3L M6N W3N W6A */

ASUS_HANDLE(ls_switch, ASUS_HOTK_PREFIX "ALSC");	/* Z71A Z71V */
ASUS_HANDLE(ls_level, ASUS_HOTK_PREFIX "ALSL");	/* Z71A Z71V */

/* GPS */
/* R2H use different handle for GPS on/off */
ASUS_HANDLE(gps_on, ASUS_HOTK_PREFIX "SDON");	/* R2H */
ASUS_HANDLE(gps_off, ASUS_HOTK_PREFIX "SDOF");	/* R2H */
ASUS_HANDLE(gps_status, ASUS_HOTK_PREFIX "GPST");

/*
 * This is the main structure, we can use it to store anything interesting
 * about the hotk device
 */
struct asus_hotk {
	char *name;		//laptop name
	struct acpi_device *device;	//the device we are in
	acpi_handle handle;	//the handle of the hotk device
	char status;		//status of the hotk, for LEDs, ...
	u32 ledd_status;	//status of the LED display
	u8 light_level;		//light sensor level
	u8 light_switch;	//light sensor switch value
	u16 event_count[128];	//count for each event TODO make this better
};

/*
 * This header is made available to allow proper configuration given model,
 * revision number , ... this info cannot go in struct asus_hotk because it is
 * available before the hotk
 */
static struct acpi_table_header *asus_info;

/* The actual device the driver binds to */
static struct asus_hotk *hotk;

/*
 * The hotkey driver declaration
 */
static const struct acpi_device_id asus_device_ids[] = {
	{"ATK0100", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, asus_device_ids);

static int asus_hotk_add(struct acpi_device *device);
static int asus_hotk_remove(struct acpi_device *device, int type);
static struct acpi_driver asus_hotk_driver = {
	.name = ASUS_HOTK_NAME,
	.class = ASUS_HOTK_CLASS,
	.ids = asus_device_ids,
	.ops = {
		.add = asus_hotk_add,
		.remove = asus_hotk_remove,
		},
};

/* The backlight device /sys/class/backlight */
static struct backlight_device *asus_backlight_device;

/*
 * The backlight class declaration
 */
static int read_brightness(struct backlight_device *bd);
static int update_bl_status(struct backlight_device *bd);
static struct backlight_ops asusbl_ops = {
	.get_brightness = read_brightness,
	.update_status = update_bl_status,
};

/* These functions actually update the LED's, and are called from a
 * workqueue. By doing this as separate work rather than when the LED
 * subsystem asks, we avoid messing with the Asus ACPI stuff during a
 * potentially bad time, such as a timer interrupt. */
static struct workqueue_struct *led_workqueue;

#define ASUS_LED(object, ledname)					\
	static void object##_led_set(struct led_classdev *led_cdev,	\
				     enum led_brightness value);	\
	static void object##_led_update(struct work_struct *ignored);	\
	static int object##_led_wk;					\
	static DECLARE_WORK(object##_led_work, object##_led_update);	\
	static struct led_classdev object##_led = {			\
		.name           = "asus:" ledname,			\
		.brightness_set = object##_led_set,			\
	}

ASUS_LED(mled, "mail");
ASUS_LED(tled, "touchpad");
ASUS_LED(rled, "record");
ASUS_LED(pled, "phone");
ASUS_LED(gled, "gaming");

/*
 * This function evaluates an ACPI method, given an int as parameter, the
 * method is searched within the scope of the handle, can be NULL. The output
 * of the method is written is output, which can also be NULL
 *
 * returns 1 if write is successful, 0 else.
 */
static int write_acpi_int(acpi_handle handle, const char *method, int val,
			  struct acpi_buffer *output)
{
	struct acpi_object_list params;	//list of input parameters (an int here)
	union acpi_object in_obj;	//the only param we use
	acpi_status status;

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = val;

	status = acpi_evaluate_object(handle, (char *)method, &params, output);
	return (status == AE_OK);
}

static int read_wireless_status(int mask)
{
	ulong status;
	acpi_status rv = AE_OK;

	if (!wireless_status_handle)
		return (hotk->status & mask) ? 1 : 0;

	rv = acpi_evaluate_integer(wireless_status_handle, NULL, NULL, &status);
	if (ACPI_FAILURE(rv))
		printk(ASUS_WARNING "Error reading Wireless status\n");
	else
		return (status & mask) ? 1 : 0;

	return (hotk->status & mask) ? 1 : 0;
}

static int read_gps_status(void)
{
	ulong status;
	acpi_status rv = AE_OK;

	rv = acpi_evaluate_integer(gps_status_handle, NULL, NULL, &status);
	if (ACPI_FAILURE(rv))
		printk(ASUS_WARNING "Error reading GPS status\n");
	else
		return status ? 1 : 0;

	return (hotk->status & GPS_ON) ? 1 : 0;
}

/* Generic LED functions */
static int read_status(int mask)
{
	/* There is a special method for both wireless devices */
	if (mask == BT_ON || mask == WL_ON)
		return read_wireless_status(mask);
	else if (mask == GPS_ON)
		return read_gps_status();

	return (hotk->status & mask) ? 1 : 0;
}

static void write_status(acpi_handle handle, int out, int mask)
{
	hotk->status = (out) ? (hotk->status | mask) : (hotk->status & ~mask);

	switch (mask) {
	case MLED_ON:
		out = !out & 0x1;
		break;
	case GLED_ON:
		out = (out & 0x1) + 1;
		break;
	case GPS_ON:
		handle = (out) ? gps_on_handle : gps_off_handle;
		out = 0x02;
		break;
	default:
		out &= 0x1;
		break;
	}

	if (handle && !write_acpi_int(handle, NULL, out, NULL))
		printk(ASUS_WARNING " write failed %x\n", mask);
}

/* /sys/class/led handlers */
#define ASUS_LED_HANDLER(object, mask)					\
	static void object##_led_set(struct led_classdev *led_cdev,	\
				     enum led_brightness value)		\
	{								\
		object##_led_wk = value;				\
		queue_work(led_workqueue, &object##_led_work);		\
	}								\
	static void object##_led_update(struct work_struct *ignored)	\
	{								\
		int value = object##_led_wk;				\
		write_status(object##_set_handle, value, (mask));	\
	}

ASUS_LED_HANDLER(mled, MLED_ON);
ASUS_LED_HANDLER(pled, PLED_ON);
ASUS_LED_HANDLER(rled, RLED_ON);
ASUS_LED_HANDLER(tled, TLED_ON);
ASUS_LED_HANDLER(gled, GLED_ON);

static int get_lcd_state(void)
{
	return read_status(LCD_ON);
}

static int set_lcd_state(int value)
{
	int lcd = 0;
	acpi_status status = 0;

	lcd = value ? 1 : 0;

	if (lcd == get_lcd_state())
		return 0;

	if (lcd_switch_handle) {
		status = acpi_evaluate_object(lcd_switch_handle,
					      NULL, NULL, NULL);

		if (ACPI_FAILURE(status))
			printk(ASUS_WARNING "Error switching LCD\n");
	}

	write_status(NULL, lcd, LCD_ON);
	return 0;
}

static void lcd_blank(int blank)
{
	struct backlight_device *bd = asus_backlight_device;

	if (bd) {
		bd->props.power = blank;
		backlight_update_status(bd);
	}
}

static int read_brightness(struct backlight_device *bd)
{
	ulong value;
	acpi_status rv = AE_OK;

	rv = acpi_evaluate_integer(brightness_get_handle, NULL, NULL, &value);
	if (ACPI_FAILURE(rv))
		printk(ASUS_WARNING "Error reading brightness\n");

	return value;
}

static int set_brightness(struct backlight_device *bd, int value)
{
	int ret = 0;

	value = (0 < value) ? ((15 < value) ? 15 : value) : 0;
	/* 0 <= value <= 15 */

	if (!write_acpi_int(brightness_set_handle, NULL, value, NULL)) {
		printk(ASUS_WARNING "Error changing brightness\n");
		ret = -EIO;
	}

	return ret;
}

static int update_bl_status(struct backlight_device *bd)
{
	int rv;
	int value = bd->props.brightness;

	rv = set_brightness(bd, value);
	if (rv)
		return rv;

	value = (bd->props.power == FB_BLANK_UNBLANK) ? 1 : 0;
	return set_lcd_state(value);
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
	int len = 0;
	ulong temp;
	char buf[16];		//enough for all info
	acpi_status rv = AE_OK;

	/*
	 * We use the easy way, we don't care of off and count, so we don't set eof
	 * to 1
	 */

	len += sprintf(page, ASUS_HOTK_NAME " " ASUS_LAPTOP_VERSION "\n");
	len += sprintf(page + len, "Model reference    : %s\n", hotk->name);
	/*
	 * The SFUN method probably allows the original driver to get the list
	 * of features supported by a given model. For now, 0x0100 or 0x0800
	 * bit signifies that the laptop is equipped with a Wi-Fi MiniPCI card.
	 * The significance of others is yet to be found.
	 */
	rv = acpi_evaluate_integer(hotk->handle, "SFUN", NULL, &temp);
	if (!ACPI_FAILURE(rv))
		len += sprintf(page + len, "SFUN value         : 0x%04x\n",
			       (uint) temp);
	/*
	 * Another value for userspace: the ASYM method returns 0x02 for
	 * battery low and 0x04 for battery critical, its readings tend to be
	 * more accurate than those provided by _BST.
	 * Note: since not all the laptops provide this method, errors are
	 * silently ignored.
	 */
	rv = acpi_evaluate_integer(hotk->handle, "ASYM", NULL, &temp);
	if (!ACPI_FAILURE(rv))
		len += sprintf(page + len, "ASYM value         : 0x%04x\n",
			       (uint) temp);
	if (asus_info) {
		snprintf(buf, 16, "%d", asus_info->length);
		len += sprintf(page + len, "DSDT length        : %s\n", buf);
		snprintf(buf, 16, "%d", asus_info->checksum);
		len += sprintf(page + len, "DSDT checksum      : %s\n", buf);
		snprintf(buf, 16, "%d", asus_info->revision);
		len += sprintf(page + len, "DSDT revision      : %s\n", buf);
		snprintf(buf, 7, "%s", asus_info->oem_id);
		len += sprintf(page + len, "OEM id             : %s\n", buf);
		snprintf(buf, 9, "%s", asus_info->oem_table_id);
		len += sprintf(page + len, "OEM table id       : %s\n", buf);
		snprintf(buf, 16, "%x", asus_info->oem_revision);
		len += sprintf(page + len, "OEM revision       : 0x%s\n", buf);
		snprintf(buf, 5, "%s", asus_info->asl_compiler_id);
		len += sprintf(page + len, "ASL comp vendor id : %s\n", buf);
		snprintf(buf, 16, "%x", asus_info->asl_compiler_revision);
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

static ssize_t store_status(const char *buf, size_t count,
			    acpi_handle handle, int mask)
{
	int rv, value;
	int out = 0;

	rv = parse_arg(buf, count, &value);
	if (rv > 0)
		out = value ? 1 : 0;

	write_status(handle, out, mask);

	return rv;
}

/*
 * LEDD display
 */
static ssize_t show_ledd(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "0x%08x\n", hotk->ledd_status);
}

static ssize_t store_ledd(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	int rv, value;

	rv = parse_arg(buf, count, &value);
	if (rv > 0) {
		if (!write_acpi_int(ledd_set_handle, NULL, value, NULL))
			printk(ASUS_WARNING "LED display write failed\n");
		else
			hotk->ledd_status = (u32) value;
	}
	return rv;
}

/*
 * WLAN
 */
static ssize_t show_wlan(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", read_status(WL_ON));
}

static ssize_t store_wlan(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	return store_status(buf, count, wl_switch_handle, WL_ON);
}

/*
 * Bluetooth
 */
static ssize_t show_bluetooth(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", read_status(BT_ON));
}

static ssize_t store_bluetooth(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	return store_status(buf, count, bt_switch_handle, BT_ON);
}

/*
 * Display
 */
static void set_display(int value)
{
	/* no sanity check needed for now */
	if (!write_acpi_int(display_set_handle, NULL, value, NULL))
		printk(ASUS_WARNING "Error setting display\n");
	return;
}

static int read_display(void)
{
	ulong value = 0;
	acpi_status rv = AE_OK;

	/* In most of the case, we know how to set the display, but sometime
	   we can't read it */
	if (display_get_handle) {
		rv = acpi_evaluate_integer(display_get_handle, NULL,
					   NULL, &value);
		if (ACPI_FAILURE(rv))
			printk(ASUS_WARNING "Error reading display status\n");
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
	return sprintf(buf, "%d\n", read_display());
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
	int rv, value;

	rv = parse_arg(buf, count, &value);
	if (rv > 0)
		set_display(value);
	return rv;
}

/*
 * Light Sens
 */
static void set_light_sens_switch(int value)
{
	if (!write_acpi_int(ls_switch_handle, NULL, value, NULL))
		printk(ASUS_WARNING "Error setting light sensor switch\n");
	hotk->light_switch = value;
}

static ssize_t show_lssw(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", hotk->light_switch);
}

static ssize_t store_lssw(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	int rv, value;

	rv = parse_arg(buf, count, &value);
	if (rv > 0)
		set_light_sens_switch(value ? 1 : 0);

	return rv;
}

static void set_light_sens_level(int value)
{
	if (!write_acpi_int(ls_level_handle, NULL, value, NULL))
		printk(ASUS_WARNING "Error setting light sensor level\n");
	hotk->light_level = value;
}

static ssize_t show_lslvl(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", hotk->light_level);
}

static ssize_t store_lslvl(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	int rv, value;

	rv = parse_arg(buf, count, &value);
	if (rv > 0) {
		value = (0 < value) ? ((15 < value) ? 15 : value) : 0;
		/* 0 <= value <= 15 */
		set_light_sens_level(value);
	}

	return rv;
}

/*
 * GPS
 */
static ssize_t show_gps(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", read_status(GPS_ON));
}

static ssize_t store_gps(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	return store_status(buf, count, NULL, GPS_ON);
}

static void asus_hotk_notify(acpi_handle handle, u32 event, void *data)
{
	/* TODO Find a better way to handle events count. */
	if (!hotk)
		return;

	/*
	 * We need to tell the backlight device when the backlight power is
	 * switched
	 */
	if (event == ATKD_LCD_ON) {
		write_status(NULL, 1, LCD_ON);
		lcd_blank(FB_BLANK_UNBLANK);
	} else if (event == ATKD_LCD_OFF) {
		write_status(NULL, 0, LCD_ON);
		lcd_blank(FB_BLANK_POWERDOWN);
	}

	acpi_bus_generate_proc_event(hotk->device, event,
				hotk->event_count[event % 128]++);

	return;
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

static struct attribute_group asuspf_attribute_group = {
	.attrs = asuspf_attributes
};

static struct platform_driver asuspf_driver = {
	.driver = {
		   .name = ASUS_HOTK_FILE,
		   .owner = THIS_MODULE,
		   }
};

static struct platform_device *asuspf_device;

static void asus_hotk_add_fs(void)
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
 * This function is used to initialize the hotk with right values. In this
 * method, we can make all the detection we want, and modify the hotk struct
 */
static int asus_hotk_get_info(void)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *model = NULL;
	ulong bsts_result, hwrs_result;
	char *string = NULL;
	acpi_status status;

	/*
	 * Get DSDT headers early enough to allow for differentiating between
	 * models, but late enough to allow acpi_bus_register_driver() to fail
	 * before doing anything ACPI-specific. Should we encounter a machine,
	 * which needs special handling (i.e. its hotkey device has a different
	 * HID), this bit will be moved. A global variable asus_info contains
	 * the DSDT header.
	 */
	status = acpi_get_table(ACPI_SIG_DSDT, 1, &asus_info);
	if (ACPI_FAILURE(status))
		printk(ASUS_WARNING "Couldn't get the DSDT table header\n");

	/* We have to write 0 on init this far for all ASUS models */
	if (!write_acpi_int(hotk->handle, "INIT", 0, &buffer)) {
		printk(ASUS_ERR "Hotkey initialization failed\n");
		return -ENODEV;
	}

	/* This needs to be called for some laptops to init properly */
	status =
	    acpi_evaluate_integer(hotk->handle, "BSTS", NULL, &bsts_result);
	if (ACPI_FAILURE(status))
		printk(ASUS_WARNING "Error calling BSTS\n");
	else if (bsts_result)
		printk(ASUS_NOTICE "BSTS called, 0x%02x returned\n",
		       (uint) bsts_result);

	/* This too ... */
	write_acpi_int(hotk->handle, "CWAP", wapf, NULL);

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
	hotk->name = kstrdup(string, GFP_KERNEL);
	if (!hotk->name)
		return -ENOMEM;

	if (*string)
		printk(ASUS_NOTICE "  %s model detected\n", string);

	ASUS_HANDLE_INIT(mled_set);
	ASUS_HANDLE_INIT(tled_set);
	ASUS_HANDLE_INIT(rled_set);
	ASUS_HANDLE_INIT(pled_set);
	ASUS_HANDLE_INIT(gled_set);

	ASUS_HANDLE_INIT(ledd_set);

	/*
	 * The HWRS method return informations about the hardware.
	 * 0x80 bit is for WLAN, 0x100 for Bluetooth.
	 * The significance of others is yet to be found.
	 * If we don't find the method, we assume the device are present.
	 */
	status =
	    acpi_evaluate_integer(hotk->handle, "HRWS", NULL, &hwrs_result);
	if (ACPI_FAILURE(status))
		hwrs_result = WL_HWRS | BT_HWRS;

	if (hwrs_result & WL_HWRS)
		ASUS_HANDLE_INIT(wl_switch);
	if (hwrs_result & BT_HWRS)
		ASUS_HANDLE_INIT(bt_switch);

	ASUS_HANDLE_INIT(wireless_status);

	ASUS_HANDLE_INIT(brightness_set);
	ASUS_HANDLE_INIT(brightness_get);

	ASUS_HANDLE_INIT(lcd_switch);

	ASUS_HANDLE_INIT(display_set);
	ASUS_HANDLE_INIT(display_get);

	/* There is a lot of models with "ALSL", but a few get
	   a real light sens, so we need to check it. */
	if (!ASUS_HANDLE_INIT(ls_switch))
		ASUS_HANDLE_INIT(ls_level);

	ASUS_HANDLE_INIT(gps_on);
	ASUS_HANDLE_INIT(gps_off);
	ASUS_HANDLE_INIT(gps_status);

	kfree(model);

	return AE_OK;
}

static int asus_hotk_check(void)
{
	int result = 0;

	result = acpi_bus_get_status(hotk->device);
	if (result)
		return result;

	if (hotk->device->status.present) {
		result = asus_hotk_get_info();
	} else {
		printk(ASUS_ERR "Hotkey device not present, aborting\n");
		return -EINVAL;
	}

	return result;
}

static int asus_hotk_found;

static int asus_hotk_add(struct acpi_device *device)
{
	acpi_status status = AE_OK;
	int result;

	if (!device)
		return -EINVAL;

	printk(ASUS_NOTICE "Asus Laptop Support version %s\n",
	       ASUS_LAPTOP_VERSION);

	hotk = kzalloc(sizeof(struct asus_hotk), GFP_KERNEL);
	if (!hotk)
		return -ENOMEM;

	hotk->handle = device->handle;
	strcpy(acpi_device_name(device), ASUS_HOTK_DEVICE_NAME);
	strcpy(acpi_device_class(device), ASUS_HOTK_CLASS);
	acpi_driver_data(device) = hotk;
	hotk->device = device;

	result = asus_hotk_check();
	if (result)
		goto end;

	asus_hotk_add_fs();

	/*
	 * We install the handler, it will receive the hotk in parameter, so, we
	 * could add other data to the hotk struct
	 */
	status = acpi_install_notify_handler(hotk->handle, ACPI_ALL_NOTIFY,
					     asus_hotk_notify, hotk);
	if (ACPI_FAILURE(status))
		printk(ASUS_ERR "Error installing notify handler\n");

	asus_hotk_found = 1;

	/* WLED and BLED are on by default */
	write_status(bt_switch_handle, 1, BT_ON);
	write_status(wl_switch_handle, 1, WL_ON);

	/* If the h/w switch is off, we need to check the real status */
	write_status(NULL, read_status(BT_ON), BT_ON);
	write_status(NULL, read_status(WL_ON), WL_ON);

	/* LCD Backlight is on by default */
	write_status(NULL, 1, LCD_ON);

	/* LED display is off by default */
	hotk->ledd_status = 0xFFF;

	/* Set initial values of light sensor and level */
	hotk->light_switch = 1;	/* Default to light sensor disabled */
	hotk->light_level = 0;	/* level 5 for sensor sensitivity */

	if (ls_switch_handle)
		set_light_sens_switch(hotk->light_switch);

	if (ls_level_handle)
		set_light_sens_level(hotk->light_level);

	/* GPS is on by default */
	write_status(NULL, 1, GPS_ON);

      end:
	if (result) {
		kfree(hotk->name);
		kfree(hotk);
	}

	return result;
}

static int asus_hotk_remove(struct acpi_device *device, int type)
{
	acpi_status status = 0;

	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	status = acpi_remove_notify_handler(hotk->handle, ACPI_ALL_NOTIFY,
					    asus_hotk_notify);
	if (ACPI_FAILURE(status))
		printk(ASUS_ERR "Error removing notify handler\n");

	kfree(hotk->name);
	kfree(hotk);

	return 0;
}

static void asus_backlight_exit(void)
{
	if (asus_backlight_device)
		backlight_device_unregister(asus_backlight_device);
}

#define  ASUS_LED_UNREGISTER(object)				\
	if (object##_led.dev)					\
		led_classdev_unregister(&object##_led)

static void asus_led_exit(void)
{
	destroy_workqueue(led_workqueue);
	ASUS_LED_UNREGISTER(mled);
	ASUS_LED_UNREGISTER(tled);
	ASUS_LED_UNREGISTER(pled);
	ASUS_LED_UNREGISTER(rled);
	ASUS_LED_UNREGISTER(gled);
}

static void __exit asus_laptop_exit(void)
{
	asus_backlight_exit();
	asus_led_exit();

	acpi_bus_unregister_driver(&asus_hotk_driver);
	sysfs_remove_group(&asuspf_device->dev.kobj, &asuspf_attribute_group);
	platform_device_unregister(asuspf_device);
	platform_driver_unregister(&asuspf_driver);
}

static int asus_backlight_init(struct device *dev)
{
	struct backlight_device *bd;

	if (brightness_set_handle && lcd_switch_handle) {
		bd = backlight_device_register(ASUS_HOTK_FILE, dev,
					       NULL, &asusbl_ops);
		if (IS_ERR(bd)) {
			printk(ASUS_ERR
			       "Could not register asus backlight device\n");
			asus_backlight_device = NULL;
			return PTR_ERR(bd);
		}

		asus_backlight_device = bd;

		bd->props.max_brightness = 15;
		bd->props.brightness = read_brightness(NULL);
		bd->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(bd);
	}
	return 0;
}

static int asus_led_register(acpi_handle handle,
			     struct led_classdev *ldev, struct device *dev)
{
	if (!handle)
		return 0;

	return led_classdev_register(dev, ldev);
}

#define ASUS_LED_REGISTER(object, device)				\
	asus_led_register(object##_set_handle, &object##_led, device)

static int asus_led_init(struct device *dev)
{
	int rv;

	rv = ASUS_LED_REGISTER(mled, dev);
	if (rv)
		goto out;

	rv = ASUS_LED_REGISTER(tled, dev);
	if (rv)
		goto out1;

	rv = ASUS_LED_REGISTER(rled, dev);
	if (rv)
		goto out2;

	rv = ASUS_LED_REGISTER(pled, dev);
	if (rv)
		goto out3;

	rv = ASUS_LED_REGISTER(gled, dev);
	if (rv)
		goto out4;

	led_workqueue = create_singlethread_workqueue("led_workqueue");
	if (!led_workqueue)
		goto out5;

	return 0;
out5:
	rv = -ENOMEM;
	ASUS_LED_UNREGISTER(gled);
out4:
	ASUS_LED_UNREGISTER(pled);
out3:
	ASUS_LED_UNREGISTER(rled);
out2:
	ASUS_LED_UNREGISTER(tled);
out1:
	ASUS_LED_UNREGISTER(mled);
out:
	return rv;
}

static int __init asus_laptop_init(void)
{
	struct device *dev;
	int result;

	if (acpi_disabled)
		return -ENODEV;

	result = acpi_bus_register_driver(&asus_hotk_driver);
	if (result < 0)
		return result;

	/*
	 * This is a bit of a kludge.  We only want this module loaded
	 * for ASUS systems, but there's currently no way to probe the
	 * ACPI namespace for ASUS HIDs.  So we just return failure if
	 * we didn't find one, which will cause the module to be
	 * unloaded.
	 */
	if (!asus_hotk_found) {
		acpi_bus_unregister_driver(&asus_hotk_driver);
		return -ENODEV;
	}

	dev = acpi_get_physical_device(hotk->device->handle);

	result = asus_backlight_init(dev);
	if (result)
		goto fail_backlight;

	result = asus_led_init(dev);
	if (result)
		goto fail_led;

	/* Register platform stuff */
	result = platform_driver_register(&asuspf_driver);
	if (result)
		goto fail_platform_driver;

	asuspf_device = platform_device_alloc(ASUS_HOTK_FILE, -1);
	if (!asuspf_device) {
		result = -ENOMEM;
		goto fail_platform_device1;
	}

	result = platform_device_add(asuspf_device);
	if (result)
		goto fail_platform_device2;

	result = sysfs_create_group(&asuspf_device->dev.kobj,
				    &asuspf_attribute_group);
	if (result)
		goto fail_sysfs;

	return 0;

      fail_sysfs:
	platform_device_del(asuspf_device);

      fail_platform_device2:
	platform_device_put(asuspf_device);

      fail_platform_device1:
	platform_driver_unregister(&asuspf_driver);

      fail_platform_driver:
	asus_led_exit();

      fail_led:
	asus_backlight_exit();

      fail_backlight:

	return result;
}

module_init(asus_laptop_init);
module_exit(asus_laptop_exit);
