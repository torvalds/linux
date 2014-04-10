/*
 * ACPI Sony Notebook Control Driver (SNC and SPIC)
 *
 * Copyright (C) 2004-2005 Stelian Pop <stelian@popies.net>
 * Copyright (C) 2007-2009 Mattia Dongili <malattia@linux.it>
 *
 * Parts of this driver inspired from asus_acpi.c and ibm_acpi.c
 * which are copyrighted by their respective authors.
 *
 * The SNY6001 driver part is based on the sonypi driver which includes
 * material from:
 *
 * Copyright (C) 2001-2005 Stelian Pop <stelian@popies.net>
 *
 * Copyright (C) 2005 Narayanan R S <nars@kadamba.org>
 *
 * Copyright (C) 2001-2002 Alcôve <www.alcove.com>
 *
 * Copyright (C) 2001 Michael Ashley <m.ashley@unsw.edu.au>
 *
 * Copyright (C) 2001 Junichi Morita <jun1m@mars.dti.ne.jp>
 *
 * Copyright (C) 2000 Takaya Kinjo <t-kinjo@tc4.so-net.ne.jp>
 *
 * Copyright (C) 2000 Andrew Tridgell <tridge@valinux.com>
 *
 * Earlier work by Werner Almesberger, Paul `Rusty' Russell and Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/dmi.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/kfifo.h>
#include <linux/workqueue.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <linux/sonypi.h>
#include <linux/sony-laptop.h>
#include <linux/rfkill.h>
#ifdef CONFIG_SONYPI_COMPAT
#include <linux/poll.h>
#include <linux/miscdevice.h>
#endif
#include <asm/uaccess.h>

#define dprintk(fmt, ...)			\
do {						\
	if (debug)				\
		pr_warn(fmt, ##__VA_ARGS__);	\
} while (0)

#define SONY_NC_CLASS		"sony-nc"
#define SONY_NC_HID		"SNY5001"
#define SONY_NC_DRIVER_NAME	"Sony Notebook Control Driver"

#define SONY_PIC_CLASS		"sony-pic"
#define SONY_PIC_HID		"SNY6001"
#define SONY_PIC_DRIVER_NAME	"Sony Programmable IO Control Driver"

MODULE_AUTHOR("Stelian Pop, Mattia Dongili");
MODULE_DESCRIPTION("Sony laptop extras driver (SPIC and SNC ACPI device)");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "set this to 1 (and RTFM) if you want to help "
		 "the development of this driver");

static int no_spic;		/* = 0 */
module_param(no_spic, int, 0444);
MODULE_PARM_DESC(no_spic,
		 "set this if you don't want to enable the SPIC device");

static int compat;		/* = 0 */
module_param(compat, int, 0444);
MODULE_PARM_DESC(compat,
		 "set this if you want to enable backward compatibility mode");

static unsigned long mask = 0xffffffff;
module_param(mask, ulong, 0644);
MODULE_PARM_DESC(mask,
		 "set this to the mask of event you want to enable (see doc)");

static int camera;		/* = 0 */
module_param(camera, int, 0444);
MODULE_PARM_DESC(camera,
		 "set this to 1 to enable Motion Eye camera controls "
		 "(only use it if you have a C1VE or C1VN model)");

#ifdef CONFIG_SONYPI_COMPAT
static int minor = -1;
module_param(minor, int, 0);
MODULE_PARM_DESC(minor,
		 "minor number of the misc device for the SPIC compatibility code, "
		 "default is -1 (automatic)");
#endif

static int kbd_backlight = -1;
module_param(kbd_backlight, int, 0444);
MODULE_PARM_DESC(kbd_backlight,
		 "set this to 0 to disable keyboard backlight, "
		 "1 to enable it with automatic control and 2 to have it always "
		 "on (default: no change from current value)");

static int kbd_backlight_timeout = -1;
module_param(kbd_backlight_timeout, int, 0444);
MODULE_PARM_DESC(kbd_backlight_timeout,
		 "meaningful values vary from 0 to 3 and their meaning depends "
		 "on the model (default: no change from current value)");

#ifdef CONFIG_PM_SLEEP
static void sony_nc_thermal_resume(void);
#endif
static int sony_nc_kbd_backlight_setup(struct platform_device *pd,
		unsigned int handle);
static void sony_nc_kbd_backlight_cleanup(struct platform_device *pd,
		unsigned int handle);

static int sony_nc_battery_care_setup(struct platform_device *pd,
		unsigned int handle);
static void sony_nc_battery_care_cleanup(struct platform_device *pd);

static int sony_nc_thermal_setup(struct platform_device *pd);
static void sony_nc_thermal_cleanup(struct platform_device *pd);

static int sony_nc_lid_resume_setup(struct platform_device *pd,
				    unsigned int handle);
static void sony_nc_lid_resume_cleanup(struct platform_device *pd);

static int sony_nc_gfx_switch_setup(struct platform_device *pd,
		unsigned int handle);
static void sony_nc_gfx_switch_cleanup(struct platform_device *pd);
static int __sony_nc_gfx_switch_status_get(void);

static int sony_nc_highspeed_charging_setup(struct platform_device *pd);
static void sony_nc_highspeed_charging_cleanup(struct platform_device *pd);

static int sony_nc_lowbatt_setup(struct platform_device *pd);
static void sony_nc_lowbatt_cleanup(struct platform_device *pd);

static int sony_nc_fanspeed_setup(struct platform_device *pd);
static void sony_nc_fanspeed_cleanup(struct platform_device *pd);

static int sony_nc_usb_charge_setup(struct platform_device *pd);
static void sony_nc_usb_charge_cleanup(struct platform_device *pd);

static int sony_nc_panelid_setup(struct platform_device *pd);
static void sony_nc_panelid_cleanup(struct platform_device *pd);

static int sony_nc_smart_conn_setup(struct platform_device *pd);
static void sony_nc_smart_conn_cleanup(struct platform_device *pd);

static int sony_nc_touchpad_setup(struct platform_device *pd,
				  unsigned int handle);
static void sony_nc_touchpad_cleanup(struct platform_device *pd);

enum sony_nc_rfkill {
	SONY_WIFI,
	SONY_BLUETOOTH,
	SONY_WWAN,
	SONY_WIMAX,
	N_SONY_RFKILL,
};

static int sony_rfkill_handle;
static struct rfkill *sony_rfkill_devices[N_SONY_RFKILL];
static int sony_rfkill_address[N_SONY_RFKILL] = {0x300, 0x500, 0x700, 0x900};
static int sony_nc_rfkill_setup(struct acpi_device *device,
		unsigned int handle);
static void sony_nc_rfkill_cleanup(void);
static void sony_nc_rfkill_update(void);

/*********** Input Devices ***********/

#define SONY_LAPTOP_BUF_SIZE	128
struct sony_laptop_input_s {
	atomic_t		users;
	struct input_dev	*jog_dev;
	struct input_dev	*key_dev;
	struct kfifo		fifo;
	spinlock_t		fifo_lock;
	struct timer_list	release_key_timer;
};

static struct sony_laptop_input_s sony_laptop_input = {
	.users = ATOMIC_INIT(0),
};

struct sony_laptop_keypress {
	struct input_dev *dev;
	int key;
};

/* Correspondance table between sonypi events
 * and input layer indexes in the keymap
 */
static int sony_laptop_input_index[] = {
	-1,	/*  0 no event */
	-1,	/*  1 SONYPI_EVENT_JOGDIAL_DOWN */
	-1,	/*  2 SONYPI_EVENT_JOGDIAL_UP */
	-1,	/*  3 SONYPI_EVENT_JOGDIAL_DOWN_PRESSED */
	-1,	/*  4 SONYPI_EVENT_JOGDIAL_UP_PRESSED */
	-1,	/*  5 SONYPI_EVENT_JOGDIAL_PRESSED */
	-1,	/*  6 SONYPI_EVENT_JOGDIAL_RELEASED */
	 0,	/*  7 SONYPI_EVENT_CAPTURE_PRESSED */
	 1,	/*  8 SONYPI_EVENT_CAPTURE_RELEASED */
	 2,	/*  9 SONYPI_EVENT_CAPTURE_PARTIALPRESSED */
	 3,	/* 10 SONYPI_EVENT_CAPTURE_PARTIALRELEASED */
	 4,	/* 11 SONYPI_EVENT_FNKEY_ESC */
	 5,	/* 12 SONYPI_EVENT_FNKEY_F1 */
	 6,	/* 13 SONYPI_EVENT_FNKEY_F2 */
	 7,	/* 14 SONYPI_EVENT_FNKEY_F3 */
	 8,	/* 15 SONYPI_EVENT_FNKEY_F4 */
	 9,	/* 16 SONYPI_EVENT_FNKEY_F5 */
	10,	/* 17 SONYPI_EVENT_FNKEY_F6 */
	11,	/* 18 SONYPI_EVENT_FNKEY_F7 */
	12,	/* 19 SONYPI_EVENT_FNKEY_F8 */
	13,	/* 20 SONYPI_EVENT_FNKEY_F9 */
	14,	/* 21 SONYPI_EVENT_FNKEY_F10 */
	15,	/* 22 SONYPI_EVENT_FNKEY_F11 */
	16,	/* 23 SONYPI_EVENT_FNKEY_F12 */
	17,	/* 24 SONYPI_EVENT_FNKEY_1 */
	18,	/* 25 SONYPI_EVENT_FNKEY_2 */
	19,	/* 26 SONYPI_EVENT_FNKEY_D */
	20,	/* 27 SONYPI_EVENT_FNKEY_E */
	21,	/* 28 SONYPI_EVENT_FNKEY_F */
	22,	/* 29 SONYPI_EVENT_FNKEY_S */
	23,	/* 30 SONYPI_EVENT_FNKEY_B */
	24,	/* 31 SONYPI_EVENT_BLUETOOTH_PRESSED */
	25,	/* 32 SONYPI_EVENT_PKEY_P1 */
	26,	/* 33 SONYPI_EVENT_PKEY_P2 */
	27,	/* 34 SONYPI_EVENT_PKEY_P3 */
	28,	/* 35 SONYPI_EVENT_BACK_PRESSED */
	-1,	/* 36 SONYPI_EVENT_LID_CLOSED */
	-1,	/* 37 SONYPI_EVENT_LID_OPENED */
	29,	/* 38 SONYPI_EVENT_BLUETOOTH_ON */
	30,	/* 39 SONYPI_EVENT_BLUETOOTH_OFF */
	31,	/* 40 SONYPI_EVENT_HELP_PRESSED */
	32,	/* 41 SONYPI_EVENT_FNKEY_ONLY */
	33,	/* 42 SONYPI_EVENT_JOGDIAL_FAST_DOWN */
	34,	/* 43 SONYPI_EVENT_JOGDIAL_FAST_UP */
	35,	/* 44 SONYPI_EVENT_JOGDIAL_FAST_DOWN_PRESSED */
	36,	/* 45 SONYPI_EVENT_JOGDIAL_FAST_UP_PRESSED */
	37,	/* 46 SONYPI_EVENT_JOGDIAL_VFAST_DOWN */
	38,	/* 47 SONYPI_EVENT_JOGDIAL_VFAST_UP */
	39,	/* 48 SONYPI_EVENT_JOGDIAL_VFAST_DOWN_PRESSED */
	40,	/* 49 SONYPI_EVENT_JOGDIAL_VFAST_UP_PRESSED */
	41,	/* 50 SONYPI_EVENT_ZOOM_PRESSED */
	42,	/* 51 SONYPI_EVENT_THUMBPHRASE_PRESSED */
	43,	/* 52 SONYPI_EVENT_MEYE_FACE */
	44,	/* 53 SONYPI_EVENT_MEYE_OPPOSITE */
	45,	/* 54 SONYPI_EVENT_MEMORYSTICK_INSERT */
	46,	/* 55 SONYPI_EVENT_MEMORYSTICK_EJECT */
	-1,	/* 56 SONYPI_EVENT_ANYBUTTON_RELEASED */
	-1,	/* 57 SONYPI_EVENT_BATTERY_INSERT */
	-1,	/* 58 SONYPI_EVENT_BATTERY_REMOVE */
	-1,	/* 59 SONYPI_EVENT_FNKEY_RELEASED */
	47,	/* 60 SONYPI_EVENT_WIRELESS_ON */
	48,	/* 61 SONYPI_EVENT_WIRELESS_OFF */
	49,	/* 62 SONYPI_EVENT_ZOOM_IN_PRESSED */
	50,	/* 63 SONYPI_EVENT_ZOOM_OUT_PRESSED */
	51,	/* 64 SONYPI_EVENT_CD_EJECT_PRESSED */
	52,	/* 65 SONYPI_EVENT_MODEKEY_PRESSED */
	53,	/* 66 SONYPI_EVENT_PKEY_P4 */
	54,	/* 67 SONYPI_EVENT_PKEY_P5 */
	55,	/* 68 SONYPI_EVENT_SETTINGKEY_PRESSED */
	56,	/* 69 SONYPI_EVENT_VOLUME_INC_PRESSED */
	57,	/* 70 SONYPI_EVENT_VOLUME_DEC_PRESSED */
	-1,	/* 71 SONYPI_EVENT_BRIGHTNESS_PRESSED */
	58,	/* 72 SONYPI_EVENT_MEDIA_PRESSED */
	59,	/* 72 SONYPI_EVENT_VENDOR_PRESSED */
};

static int sony_laptop_input_keycode_map[] = {
	KEY_CAMERA,	/*  0 SONYPI_EVENT_CAPTURE_PRESSED */
	KEY_RESERVED,	/*  1 SONYPI_EVENT_CAPTURE_RELEASED */
	KEY_RESERVED,	/*  2 SONYPI_EVENT_CAPTURE_PARTIALPRESSED */
	KEY_RESERVED,	/*  3 SONYPI_EVENT_CAPTURE_PARTIALRELEASED */
	KEY_FN_ESC,	/*  4 SONYPI_EVENT_FNKEY_ESC */
	KEY_FN_F1,	/*  5 SONYPI_EVENT_FNKEY_F1 */
	KEY_FN_F2,	/*  6 SONYPI_EVENT_FNKEY_F2 */
	KEY_FN_F3,	/*  7 SONYPI_EVENT_FNKEY_F3 */
	KEY_FN_F4,	/*  8 SONYPI_EVENT_FNKEY_F4 */
	KEY_FN_F5,	/*  9 SONYPI_EVENT_FNKEY_F5 */
	KEY_FN_F6,	/* 10 SONYPI_EVENT_FNKEY_F6 */
	KEY_FN_F7,	/* 11 SONYPI_EVENT_FNKEY_F7 */
	KEY_FN_F8,	/* 12 SONYPI_EVENT_FNKEY_F8 */
	KEY_FN_F9,	/* 13 SONYPI_EVENT_FNKEY_F9 */
	KEY_FN_F10,	/* 14 SONYPI_EVENT_FNKEY_F10 */
	KEY_FN_F11,	/* 15 SONYPI_EVENT_FNKEY_F11 */
	KEY_FN_F12,	/* 16 SONYPI_EVENT_FNKEY_F12 */
	KEY_FN_1,	/* 17 SONYPI_EVENT_FNKEY_1 */
	KEY_FN_2,	/* 18 SONYPI_EVENT_FNKEY_2 */
	KEY_FN_D,	/* 19 SONYPI_EVENT_FNKEY_D */
	KEY_FN_E,	/* 20 SONYPI_EVENT_FNKEY_E */
	KEY_FN_F,	/* 21 SONYPI_EVENT_FNKEY_F */
	KEY_FN_S,	/* 22 SONYPI_EVENT_FNKEY_S */
	KEY_FN_B,	/* 23 SONYPI_EVENT_FNKEY_B */
	KEY_BLUETOOTH,	/* 24 SONYPI_EVENT_BLUETOOTH_PRESSED */
	KEY_PROG1,	/* 25 SONYPI_EVENT_PKEY_P1 */
	KEY_PROG2,	/* 26 SONYPI_EVENT_PKEY_P2 */
	KEY_PROG3,	/* 27 SONYPI_EVENT_PKEY_P3 */
	KEY_BACK,	/* 28 SONYPI_EVENT_BACK_PRESSED */
	KEY_BLUETOOTH,	/* 29 SONYPI_EVENT_BLUETOOTH_ON */
	KEY_BLUETOOTH,	/* 30 SONYPI_EVENT_BLUETOOTH_OFF */
	KEY_HELP,	/* 31 SONYPI_EVENT_HELP_PRESSED */
	KEY_FN,		/* 32 SONYPI_EVENT_FNKEY_ONLY */
	KEY_RESERVED,	/* 33 SONYPI_EVENT_JOGDIAL_FAST_DOWN */
	KEY_RESERVED,	/* 34 SONYPI_EVENT_JOGDIAL_FAST_UP */
	KEY_RESERVED,	/* 35 SONYPI_EVENT_JOGDIAL_FAST_DOWN_PRESSED */
	KEY_RESERVED,	/* 36 SONYPI_EVENT_JOGDIAL_FAST_UP_PRESSED */
	KEY_RESERVED,	/* 37 SONYPI_EVENT_JOGDIAL_VFAST_DOWN */
	KEY_RESERVED,	/* 38 SONYPI_EVENT_JOGDIAL_VFAST_UP */
	KEY_RESERVED,	/* 39 SONYPI_EVENT_JOGDIAL_VFAST_DOWN_PRESSED */
	KEY_RESERVED,	/* 40 SONYPI_EVENT_JOGDIAL_VFAST_UP_PRESSED */
	KEY_ZOOM,	/* 41 SONYPI_EVENT_ZOOM_PRESSED */
	BTN_THUMB,	/* 42 SONYPI_EVENT_THUMBPHRASE_PRESSED */
	KEY_RESERVED,	/* 43 SONYPI_EVENT_MEYE_FACE */
	KEY_RESERVED,	/* 44 SONYPI_EVENT_MEYE_OPPOSITE */
	KEY_RESERVED,	/* 45 SONYPI_EVENT_MEMORYSTICK_INSERT */
	KEY_RESERVED,	/* 46 SONYPI_EVENT_MEMORYSTICK_EJECT */
	KEY_WLAN,	/* 47 SONYPI_EVENT_WIRELESS_ON */
	KEY_WLAN,	/* 48 SONYPI_EVENT_WIRELESS_OFF */
	KEY_ZOOMIN,	/* 49 SONYPI_EVENT_ZOOM_IN_PRESSED */
	KEY_ZOOMOUT,	/* 50 SONYPI_EVENT_ZOOM_OUT_PRESSED */
	KEY_EJECTCD,	/* 51 SONYPI_EVENT_CD_EJECT_PRESSED */
	KEY_F13,	/* 52 SONYPI_EVENT_MODEKEY_PRESSED */
	KEY_PROG4,	/* 53 SONYPI_EVENT_PKEY_P4 */
	KEY_F14,	/* 54 SONYPI_EVENT_PKEY_P5 */
	KEY_F15,	/* 55 SONYPI_EVENT_SETTINGKEY_PRESSED */
	KEY_VOLUMEUP,	/* 56 SONYPI_EVENT_VOLUME_INC_PRESSED */
	KEY_VOLUMEDOWN,	/* 57 SONYPI_EVENT_VOLUME_DEC_PRESSED */
	KEY_MEDIA,	/* 58 SONYPI_EVENT_MEDIA_PRESSED */
	KEY_VENDOR,	/* 59 SONYPI_EVENT_VENDOR_PRESSED */
};

/* release buttons after a short delay if pressed */
static void do_sony_laptop_release_key(unsigned long unused)
{
	struct sony_laptop_keypress kp;
	unsigned long flags;

	spin_lock_irqsave(&sony_laptop_input.fifo_lock, flags);

	if (kfifo_out(&sony_laptop_input.fifo,
		      (unsigned char *)&kp, sizeof(kp)) == sizeof(kp)) {
		input_report_key(kp.dev, kp.key, 0);
		input_sync(kp.dev);
	}

	/* If there is something in the fifo schedule next release. */
	if (kfifo_len(&sony_laptop_input.fifo) != 0)
		mod_timer(&sony_laptop_input.release_key_timer,
			  jiffies + msecs_to_jiffies(10));

	spin_unlock_irqrestore(&sony_laptop_input.fifo_lock, flags);
}

/* forward event to the input subsystem */
static void sony_laptop_report_input_event(u8 event)
{
	struct input_dev *jog_dev = sony_laptop_input.jog_dev;
	struct input_dev *key_dev = sony_laptop_input.key_dev;
	struct sony_laptop_keypress kp = { NULL };
	int scancode = -1;

	if (event == SONYPI_EVENT_FNKEY_RELEASED ||
			event == SONYPI_EVENT_ANYBUTTON_RELEASED) {
		/* Nothing, not all VAIOs generate this event */
		return;
	}

	/* report events */
	switch (event) {
	/* jog_dev events */
	case SONYPI_EVENT_JOGDIAL_UP:
	case SONYPI_EVENT_JOGDIAL_UP_PRESSED:
		input_report_rel(jog_dev, REL_WHEEL, 1);
		input_sync(jog_dev);
		return;

	case SONYPI_EVENT_JOGDIAL_DOWN:
	case SONYPI_EVENT_JOGDIAL_DOWN_PRESSED:
		input_report_rel(jog_dev, REL_WHEEL, -1);
		input_sync(jog_dev);
		return;

	/* key_dev events */
	case SONYPI_EVENT_JOGDIAL_PRESSED:
		kp.key = BTN_MIDDLE;
		kp.dev = jog_dev;
		break;

	default:
		if (event >= ARRAY_SIZE(sony_laptop_input_index)) {
			dprintk("sony_laptop_report_input_event, event not known: %d\n", event);
			break;
		}
		if ((scancode = sony_laptop_input_index[event]) != -1) {
			kp.key = sony_laptop_input_keycode_map[scancode];
			if (kp.key != KEY_UNKNOWN)
				kp.dev = key_dev;
		}
		break;
	}

	if (kp.dev) {
		/* if we have a scancode we emit it so we can always
		    remap the key */
		if (scancode != -1)
			input_event(kp.dev, EV_MSC, MSC_SCAN, scancode);
		input_report_key(kp.dev, kp.key, 1);
		input_sync(kp.dev);

		/* schedule key release */
		kfifo_in_locked(&sony_laptop_input.fifo,
				(unsigned char *)&kp, sizeof(kp),
				&sony_laptop_input.fifo_lock);
		mod_timer(&sony_laptop_input.release_key_timer,
			  jiffies + msecs_to_jiffies(10));
	} else
		dprintk("unknown input event %.2x\n", event);
}

static int sony_laptop_setup_input(struct acpi_device *acpi_device)
{
	struct input_dev *jog_dev;
	struct input_dev *key_dev;
	int i;
	int error;

	/* don't run again if already initialized */
	if (atomic_add_return(1, &sony_laptop_input.users) > 1)
		return 0;

	/* kfifo */
	spin_lock_init(&sony_laptop_input.fifo_lock);
	error = kfifo_alloc(&sony_laptop_input.fifo,
			    SONY_LAPTOP_BUF_SIZE, GFP_KERNEL);
	if (error) {
		pr_err("kfifo_alloc failed\n");
		goto err_dec_users;
	}

	setup_timer(&sony_laptop_input.release_key_timer,
		    do_sony_laptop_release_key, 0);

	/* input keys */
	key_dev = input_allocate_device();
	if (!key_dev) {
		error = -ENOMEM;
		goto err_free_kfifo;
	}

	key_dev->name = "Sony Vaio Keys";
	key_dev->id.bustype = BUS_ISA;
	key_dev->id.vendor = PCI_VENDOR_ID_SONY;
	key_dev->dev.parent = &acpi_device->dev;

	/* Initialize the Input Drivers: special keys */
	input_set_capability(key_dev, EV_MSC, MSC_SCAN);

	__set_bit(EV_KEY, key_dev->evbit);
	key_dev->keycodesize = sizeof(sony_laptop_input_keycode_map[0]);
	key_dev->keycodemax = ARRAY_SIZE(sony_laptop_input_keycode_map);
	key_dev->keycode = &sony_laptop_input_keycode_map;
	for (i = 0; i < ARRAY_SIZE(sony_laptop_input_keycode_map); i++)
		__set_bit(sony_laptop_input_keycode_map[i], key_dev->keybit);
	__clear_bit(KEY_RESERVED, key_dev->keybit);

	error = input_register_device(key_dev);
	if (error)
		goto err_free_keydev;

	sony_laptop_input.key_dev = key_dev;

	/* jogdial */
	jog_dev = input_allocate_device();
	if (!jog_dev) {
		error = -ENOMEM;
		goto err_unregister_keydev;
	}

	jog_dev->name = "Sony Vaio Jogdial";
	jog_dev->id.bustype = BUS_ISA;
	jog_dev->id.vendor = PCI_VENDOR_ID_SONY;
	jog_dev->dev.parent = &acpi_device->dev;

	input_set_capability(jog_dev, EV_KEY, BTN_MIDDLE);
	input_set_capability(jog_dev, EV_REL, REL_WHEEL);

	error = input_register_device(jog_dev);
	if (error)
		goto err_free_jogdev;

	sony_laptop_input.jog_dev = jog_dev;

	return 0;

err_free_jogdev:
	input_free_device(jog_dev);

err_unregister_keydev:
	input_unregister_device(key_dev);
	/* to avoid kref underflow below at input_free_device */
	key_dev = NULL;

err_free_keydev:
	input_free_device(key_dev);

err_free_kfifo:
	kfifo_free(&sony_laptop_input.fifo);

err_dec_users:
	atomic_dec(&sony_laptop_input.users);
	return error;
}

static void sony_laptop_remove_input(void)
{
	struct sony_laptop_keypress kp = { NULL };

	/* Cleanup only after the last user has gone */
	if (!atomic_dec_and_test(&sony_laptop_input.users))
		return;

	del_timer_sync(&sony_laptop_input.release_key_timer);

	/*
	 * Generate key-up events for remaining keys. Note that we don't
	 * need locking since nobody is adding new events to the kfifo.
	 */
	while (kfifo_out(&sony_laptop_input.fifo,
			 (unsigned char *)&kp, sizeof(kp)) == sizeof(kp)) {
		input_report_key(kp.dev, kp.key, 0);
		input_sync(kp.dev);
	}

	/* destroy input devs */
	input_unregister_device(sony_laptop_input.key_dev);
	sony_laptop_input.key_dev = NULL;

	if (sony_laptop_input.jog_dev) {
		input_unregister_device(sony_laptop_input.jog_dev);
		sony_laptop_input.jog_dev = NULL;
	}

	kfifo_free(&sony_laptop_input.fifo);
}

/*********** Platform Device ***********/

static atomic_t sony_pf_users = ATOMIC_INIT(0);
static struct platform_driver sony_pf_driver = {
	.driver = {
		   .name = "sony-laptop",
		   .owner = THIS_MODULE,
		   }
};
static struct platform_device *sony_pf_device;

static int sony_pf_add(void)
{
	int ret = 0;

	/* don't run again if already initialized */
	if (atomic_add_return(1, &sony_pf_users) > 1)
		return 0;

	ret = platform_driver_register(&sony_pf_driver);
	if (ret)
		goto out;

	sony_pf_device = platform_device_alloc("sony-laptop", -1);
	if (!sony_pf_device) {
		ret = -ENOMEM;
		goto out_platform_registered;
	}

	ret = platform_device_add(sony_pf_device);
	if (ret)
		goto out_platform_alloced;

	return 0;

      out_platform_alloced:
	platform_device_put(sony_pf_device);
	sony_pf_device = NULL;
      out_platform_registered:
	platform_driver_unregister(&sony_pf_driver);
      out:
	atomic_dec(&sony_pf_users);
	return ret;
}

static void sony_pf_remove(void)
{
	/* deregister only after the last user has gone */
	if (!atomic_dec_and_test(&sony_pf_users))
		return;

	platform_device_unregister(sony_pf_device);
	platform_driver_unregister(&sony_pf_driver);
}

/*********** SNC (SNY5001) Device ***********/

/* the device uses 1-based values, while the backlight subsystem uses
   0-based values */
#define SONY_MAX_BRIGHTNESS	8

#define SNC_VALIDATE_IN		0
#define SNC_VALIDATE_OUT	1

static ssize_t sony_nc_sysfs_show(struct device *, struct device_attribute *,
			      char *);
static ssize_t sony_nc_sysfs_store(struct device *, struct device_attribute *,
			       const char *, size_t);
static int boolean_validate(const int, const int);
static int brightness_default_validate(const int, const int);

struct sony_nc_value {
	char *name;		/* name of the entry */
	char **acpiget;		/* names of the ACPI get function */
	char **acpiset;		/* names of the ACPI set function */
	int (*validate)(const int, const int);	/* input/output validation */
	int value;		/* current setting */
	int valid;		/* Has ever been set */
	int debug;		/* active only in debug mode ? */
	struct device_attribute devattr;	/* sysfs attribute */
};

#define SNC_HANDLE_NAMES(_name, _values...) \
	static char *snc_##_name[] = { _values, NULL }

#define SNC_HANDLE(_name, _getters, _setters, _validate, _debug) \
	{ \
		.name		= __stringify(_name), \
		.acpiget	= _getters, \
		.acpiset	= _setters, \
		.validate	= _validate, \
		.debug		= _debug, \
		.devattr	= __ATTR(_name, 0, sony_nc_sysfs_show, sony_nc_sysfs_store), \
	}

#define SNC_HANDLE_NULL	{ .name = NULL }

SNC_HANDLE_NAMES(fnkey_get, "GHKE");

SNC_HANDLE_NAMES(brightness_def_get, "GPBR");
SNC_HANDLE_NAMES(brightness_def_set, "SPBR");

SNC_HANDLE_NAMES(cdpower_get, "GCDP");
SNC_HANDLE_NAMES(cdpower_set, "SCDP", "CDPW");

SNC_HANDLE_NAMES(audiopower_get, "GAZP");
SNC_HANDLE_NAMES(audiopower_set, "AZPW");

SNC_HANDLE_NAMES(lanpower_get, "GLNP");
SNC_HANDLE_NAMES(lanpower_set, "LNPW");

SNC_HANDLE_NAMES(lidstate_get, "GLID");

SNC_HANDLE_NAMES(indicatorlamp_get, "GILS");
SNC_HANDLE_NAMES(indicatorlamp_set, "SILS");

SNC_HANDLE_NAMES(gainbass_get, "GMGB");
SNC_HANDLE_NAMES(gainbass_set, "CMGB");

SNC_HANDLE_NAMES(PID_get, "GPID");

SNC_HANDLE_NAMES(CTR_get, "GCTR");
SNC_HANDLE_NAMES(CTR_set, "SCTR");

SNC_HANDLE_NAMES(PCR_get, "GPCR");
SNC_HANDLE_NAMES(PCR_set, "SPCR");

SNC_HANDLE_NAMES(CMI_get, "GCMI");
SNC_HANDLE_NAMES(CMI_set, "SCMI");

static struct sony_nc_value sony_nc_values[] = {
	SNC_HANDLE(brightness_default, snc_brightness_def_get,
			snc_brightness_def_set, brightness_default_validate, 0),
	SNC_HANDLE(fnkey, snc_fnkey_get, NULL, NULL, 0),
	SNC_HANDLE(cdpower, snc_cdpower_get, snc_cdpower_set, boolean_validate, 0),
	SNC_HANDLE(audiopower, snc_audiopower_get, snc_audiopower_set,
			boolean_validate, 0),
	SNC_HANDLE(lanpower, snc_lanpower_get, snc_lanpower_set,
			boolean_validate, 1),
	SNC_HANDLE(lidstate, snc_lidstate_get, NULL,
			boolean_validate, 0),
	SNC_HANDLE(indicatorlamp, snc_indicatorlamp_get, snc_indicatorlamp_set,
			boolean_validate, 0),
	SNC_HANDLE(gainbass, snc_gainbass_get, snc_gainbass_set,
			boolean_validate, 0),
	/* unknown methods */
	SNC_HANDLE(PID, snc_PID_get, NULL, NULL, 1),
	SNC_HANDLE(CTR, snc_CTR_get, snc_CTR_set, NULL, 1),
	SNC_HANDLE(PCR, snc_PCR_get, snc_PCR_set, NULL, 1),
	SNC_HANDLE(CMI, snc_CMI_get, snc_CMI_set, NULL, 1),
	SNC_HANDLE_NULL
};

static acpi_handle sony_nc_acpi_handle;
static struct acpi_device *sony_nc_acpi_device = NULL;

/*
 * acpi_evaluate_object wrappers
 * all useful calls into SNC methods take one or zero parameters and return
 * integers or arrays.
 */
static union acpi_object *__call_snc_method(acpi_handle handle, char *method,
		u64 *value)
{
	union acpi_object *result = NULL;
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;

	if (value) {
		struct acpi_object_list params;
		union acpi_object in;
		in.type = ACPI_TYPE_INTEGER;
		in.integer.value = *value;
		params.count = 1;
		params.pointer = &in;
		status = acpi_evaluate_object(handle, method, &params, &output);
		dprintk("__call_snc_method: [%s:0x%.8x%.8x]\n", method,
				(unsigned int)(*value >> 32),
				(unsigned int)*value & 0xffffffff);
	} else {
		status = acpi_evaluate_object(handle, method, NULL, &output);
		dprintk("__call_snc_method: [%s]\n", method);
	}

	if (ACPI_FAILURE(status)) {
		pr_err("Failed to evaluate [%s]\n", method);
		return NULL;
	}

	result = (union acpi_object *) output.pointer;
	if (!result)
		dprintk("No return object [%s]\n", method);

	return result;
}

static int sony_nc_int_call(acpi_handle handle, char *name, int *value,
		int *result)
{
	union acpi_object *object = NULL;
	if (value) {
		u64 v = *value;
		object = __call_snc_method(handle, name, &v);
	} else
		object = __call_snc_method(handle, name, NULL);

	if (!object)
		return -EINVAL;

	if (object->type != ACPI_TYPE_INTEGER) {
		pr_warn("Invalid acpi_object: expected 0x%x got 0x%x\n",
				ACPI_TYPE_INTEGER, object->type);
		kfree(object);
		return -EINVAL;
	}

	if (result)
		*result = object->integer.value;

	kfree(object);
	return 0;
}

#define MIN(a, b)	(a > b ? b : a)
static int sony_nc_buffer_call(acpi_handle handle, char *name, u64 *value,
		void *buffer, size_t buflen)
{
	int ret = 0;
	size_t len;
	union acpi_object *object = __call_snc_method(handle, name, value);

	if (!object)
		return -EINVAL;

	if (object->type == ACPI_TYPE_BUFFER) {
		len = MIN(buflen, object->buffer.length);
		memcpy(buffer, object->buffer.pointer, len);

	} else if (object->type == ACPI_TYPE_INTEGER) {
		len = MIN(buflen, sizeof(object->integer.value));
		memcpy(buffer, &object->integer.value, len);

	} else {
		pr_warn("Invalid acpi_object: expected 0x%x got 0x%x\n",
				ACPI_TYPE_BUFFER, object->type);
		ret = -EINVAL;
	}

	kfree(object);
	return ret;
}

struct sony_nc_handles {
	u16 cap[0x10];
	struct device_attribute devattr;
};

static struct sony_nc_handles *handles;

static ssize_t sony_nc_handles_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	ssize_t len = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(handles->cap); i++) {
		len += snprintf(buffer + len, PAGE_SIZE - len, "0x%.4x ",
				handles->cap[i]);
	}
	len += snprintf(buffer + len, PAGE_SIZE - len, "\n");

	return len;
}

static int sony_nc_handles_setup(struct platform_device *pd)
{
	int i, r, result, arg;

	handles = kzalloc(sizeof(*handles), GFP_KERNEL);
	if (!handles)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(handles->cap); i++) {
		arg = i + 0x20;
		r = sony_nc_int_call(sony_nc_acpi_handle, "SN00", &arg,
					&result);
		if (!r) {
			dprintk("caching handle 0x%.4x (offset: 0x%.2x)\n",
					result, i);
			handles->cap[i] = result;
		}
	}

	if (debug) {
		sysfs_attr_init(&handles->devattr.attr);
		handles->devattr.attr.name = "handles";
		handles->devattr.attr.mode = S_IRUGO;
		handles->devattr.show = sony_nc_handles_show;

		/* allow reading capabilities via sysfs */
		if (device_create_file(&pd->dev, &handles->devattr)) {
			kfree(handles);
			handles = NULL;
			return -1;
		}
	}

	return 0;
}

static int sony_nc_handles_cleanup(struct platform_device *pd)
{
	if (handles) {
		if (debug)
			device_remove_file(&pd->dev, &handles->devattr);
		kfree(handles);
		handles = NULL;
	}
	return 0;
}

static int sony_find_snc_handle(int handle)
{
	int i;

	/* not initialized yet, return early */
	if (!handles || !handle)
		return -EINVAL;

	for (i = 0; i < 0x10; i++) {
		if (handles->cap[i] == handle) {
			dprintk("found handle 0x%.4x (offset: 0x%.2x)\n",
					handle, i);
			return i;
		}
	}
	dprintk("handle 0x%.4x not found\n", handle);
	return -EINVAL;
}

static int sony_call_snc_handle(int handle, int argument, int *result)
{
	int arg, ret = 0;
	int offset = sony_find_snc_handle(handle);

	if (offset < 0)
		return offset;

	arg = offset | argument;
	ret = sony_nc_int_call(sony_nc_acpi_handle, "SN07", &arg, result);
	dprintk("called SN07 with 0x%.4x (result: 0x%.4x)\n", arg, *result);
	return ret;
}

/*
 * sony_nc_values input/output validate functions
 */

/* brightness_default_validate:
 *
 * manipulate input output values to keep consistency with the
 * backlight framework for which brightness values are 0-based.
 */
static int brightness_default_validate(const int direction, const int value)
{
	switch (direction) {
		case SNC_VALIDATE_OUT:
			return value - 1;
		case SNC_VALIDATE_IN:
			if (value >= 0 && value < SONY_MAX_BRIGHTNESS)
				return value + 1;
	}
	return -EINVAL;
}

/* boolean_validate:
 *
 * on input validate boolean values 0/1, on output just pass the
 * received value.
 */
static int boolean_validate(const int direction, const int value)
{
	if (direction == SNC_VALIDATE_IN) {
		if (value != 0 && value != 1)
			return -EINVAL;
	}
	return value;
}

/*
 * Sysfs show/store common to all sony_nc_values
 */
static ssize_t sony_nc_sysfs_show(struct device *dev, struct device_attribute *attr,
			      char *buffer)
{
	int value, ret = 0;
	struct sony_nc_value *item =
	    container_of(attr, struct sony_nc_value, devattr);

	if (!*item->acpiget)
		return -EIO;

	ret = sony_nc_int_call(sony_nc_acpi_handle, *item->acpiget, NULL,
				&value);
	if (ret < 0)
		return -EIO;

	if (item->validate)
		value = item->validate(SNC_VALIDATE_OUT, value);

	return snprintf(buffer, PAGE_SIZE, "%d\n", value);
}

static ssize_t sony_nc_sysfs_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buffer, size_t count)
{
	int value;
	int ret = 0;
	struct sony_nc_value *item =
	    container_of(attr, struct sony_nc_value, devattr);

	if (!item->acpiset)
		return -EIO;

	if (count > 31)
		return -EINVAL;

	if (kstrtoint(buffer, 10, &value))
		return -EINVAL;

	if (item->validate)
		value = item->validate(SNC_VALIDATE_IN, value);

	if (value < 0)
		return value;

	ret = sony_nc_int_call(sony_nc_acpi_handle, *item->acpiset,
			       &value, NULL);
	if (ret < 0)
		return -EIO;

	item->value = value;
	item->valid = 1;
	return count;
}


/*
 * Backlight device
 */
struct sony_backlight_props {
	struct backlight_device *dev;
	int			handle;
	int			cmd_base;
	u8			offset;
	u8			maxlvl;
};
struct sony_backlight_props sony_bl_props;

static int sony_backlight_update_status(struct backlight_device *bd)
{
	int arg = bd->props.brightness + 1;
	return sony_nc_int_call(sony_nc_acpi_handle, "SBRT", &arg, NULL);
}

static int sony_backlight_get_brightness(struct backlight_device *bd)
{
	int value;

	if (sony_nc_int_call(sony_nc_acpi_handle, "GBRT", NULL, &value))
		return 0;
	/* brightness levels are 1-based, while backlight ones are 0-based */
	return value - 1;
}

static int sony_nc_get_brightness_ng(struct backlight_device *bd)
{
	int result;
	struct sony_backlight_props *sdev =
		(struct sony_backlight_props *)bl_get_data(bd);

	sony_call_snc_handle(sdev->handle, sdev->cmd_base + 0x100, &result);

	return (result & 0xff) - sdev->offset;
}

static int sony_nc_update_status_ng(struct backlight_device *bd)
{
	int value, result;
	struct sony_backlight_props *sdev =
		(struct sony_backlight_props *)bl_get_data(bd);

	value = bd->props.brightness + sdev->offset;
	if (sony_call_snc_handle(sdev->handle, sdev->cmd_base | (value << 0x10),
				&result))
		return -EIO;

	return value;
}

static const struct backlight_ops sony_backlight_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = sony_backlight_update_status,
	.get_brightness = sony_backlight_get_brightness,
};
static const struct backlight_ops sony_backlight_ng_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = sony_nc_update_status_ng,
	.get_brightness = sony_nc_get_brightness_ng,
};

/*
 * New SNC-only Vaios event mapping to driver known keys
 */
struct sony_nc_event {
	u8	data;
	u8	event;
};

static struct sony_nc_event sony_100_events[] = {
	{ 0x90, SONYPI_EVENT_PKEY_P1 },
	{ 0x10, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0x91, SONYPI_EVENT_PKEY_P2 },
	{ 0x11, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0x81, SONYPI_EVENT_FNKEY_F1 },
	{ 0x01, SONYPI_EVENT_FNKEY_RELEASED },
	{ 0x82, SONYPI_EVENT_FNKEY_F2 },
	{ 0x02, SONYPI_EVENT_FNKEY_RELEASED },
	{ 0x83, SONYPI_EVENT_FNKEY_F3 },
	{ 0x03, SONYPI_EVENT_FNKEY_RELEASED },
	{ 0x84, SONYPI_EVENT_FNKEY_F4 },
	{ 0x04, SONYPI_EVENT_FNKEY_RELEASED },
	{ 0x85, SONYPI_EVENT_FNKEY_F5 },
	{ 0x05, SONYPI_EVENT_FNKEY_RELEASED },
	{ 0x86, SONYPI_EVENT_FNKEY_F6 },
	{ 0x06, SONYPI_EVENT_FNKEY_RELEASED },
	{ 0x87, SONYPI_EVENT_FNKEY_F7 },
	{ 0x07, SONYPI_EVENT_FNKEY_RELEASED },
	{ 0x88, SONYPI_EVENT_FNKEY_F8 },
	{ 0x08, SONYPI_EVENT_FNKEY_RELEASED },
	{ 0x89, SONYPI_EVENT_FNKEY_F9 },
	{ 0x09, SONYPI_EVENT_FNKEY_RELEASED },
	{ 0x8A, SONYPI_EVENT_FNKEY_F10 },
	{ 0x0A, SONYPI_EVENT_FNKEY_RELEASED },
	{ 0x8B, SONYPI_EVENT_FNKEY_F11 },
	{ 0x0B, SONYPI_EVENT_FNKEY_RELEASED },
	{ 0x8C, SONYPI_EVENT_FNKEY_F12 },
	{ 0x0C, SONYPI_EVENT_FNKEY_RELEASED },
	{ 0x9d, SONYPI_EVENT_ZOOM_PRESSED },
	{ 0x1d, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0x9f, SONYPI_EVENT_CD_EJECT_PRESSED },
	{ 0x1f, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0xa1, SONYPI_EVENT_MEDIA_PRESSED },
	{ 0x21, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0xa4, SONYPI_EVENT_CD_EJECT_PRESSED },
	{ 0x24, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0xa5, SONYPI_EVENT_VENDOR_PRESSED },
	{ 0x25, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0xa6, SONYPI_EVENT_HELP_PRESSED },
	{ 0x26, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0xa8, SONYPI_EVENT_FNKEY_1 },
	{ 0x28, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0, 0 },
};

static struct sony_nc_event sony_127_events[] = {
	{ 0x81, SONYPI_EVENT_MODEKEY_PRESSED },
	{ 0x01, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0x82, SONYPI_EVENT_PKEY_P1 },
	{ 0x02, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0x83, SONYPI_EVENT_PKEY_P2 },
	{ 0x03, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0x84, SONYPI_EVENT_PKEY_P3 },
	{ 0x04, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0x85, SONYPI_EVENT_PKEY_P4 },
	{ 0x05, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0x86, SONYPI_EVENT_PKEY_P5 },
	{ 0x06, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0x87, SONYPI_EVENT_SETTINGKEY_PRESSED },
	{ 0x07, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0, 0 },
};

static int sony_nc_hotkeys_decode(u32 event, unsigned int handle)
{
	int ret = -EINVAL;
	unsigned int result = 0;
	struct sony_nc_event *key_event;

	if (sony_call_snc_handle(handle, 0x200, &result)) {
		dprintk("Unable to decode event 0x%.2x 0x%.2x\n", handle,
				event);
		return -EINVAL;
	}

	result &= 0xFF;

	if (handle == 0x0100)
		key_event = sony_100_events;
	else
		key_event = sony_127_events;

	for (; key_event->data; key_event++) {
		if (key_event->data == result) {
			ret = key_event->event;
			break;
		}
	}

	if (!key_event->data)
		pr_info("Unknown hotkey 0x%.2x/0x%.2x (handle 0x%.2x)\n",
				event, result, handle);

	return ret;
}

/*
 * ACPI callbacks
 */
enum event_types {
	HOTKEY = 1,
	KILLSWITCH,
	GFX_SWITCH
};
static void sony_nc_notify(struct acpi_device *device, u32 event)
{
	u32 real_ev = event;
	u8 ev_type = 0;
	dprintk("sony_nc_notify, event: 0x%.2x\n", event);

	if (event >= 0x90) {
		unsigned int result = 0;
		unsigned int arg = 0;
		unsigned int handle = 0;
		unsigned int offset = event - 0x90;

		if (offset >= ARRAY_SIZE(handles->cap)) {
			pr_err("Event 0x%x outside of capabilities list\n",
					event);
			return;
		}
		handle = handles->cap[offset];

		/* list of handles known for generating events */
		switch (handle) {
		/* hotkey event */
		case 0x0100:
		case 0x0127:
			ev_type = HOTKEY;
			real_ev = sony_nc_hotkeys_decode(event, handle);

			if (real_ev > 0)
				sony_laptop_report_input_event(real_ev);
			else
				/* restore the original event for reporting */
				real_ev = event;

			break;

		/* wlan switch */
		case 0x0124:
		case 0x0135:
			/* events on this handle are reported when the
			 * switch changes position or for battery
			 * events. We'll notify both of them but only
			 * update the rfkill device status when the
			 * switch is moved.
			 */
			ev_type = KILLSWITCH;
			sony_call_snc_handle(handle, 0x0100, &result);
			real_ev = result & 0x03;

			/* hw switch event */
			if (real_ev == 1)
				sony_nc_rfkill_update();

			break;

		case 0x0128:
		case 0x0146:
			/* Hybrid GFX switching */
			sony_call_snc_handle(handle, 0x0000, &result);
			dprintk("GFX switch event received (reason: %s)\n",
					(result == 0x1) ? "switch change" :
					(result == 0x2) ? "output switch" :
					(result == 0x3) ? "output switch" :
					"");

			ev_type = GFX_SWITCH;
			real_ev = __sony_nc_gfx_switch_status_get();
			break;

		case 0x015B:
			/* Hybrid GFX switching SVS151290S */
			ev_type = GFX_SWITCH;
			real_ev = __sony_nc_gfx_switch_status_get();
			break;
		default:
			dprintk("Unknown event 0x%x for handle 0x%x\n",
					event, handle);
			break;
		}

		/* clear the event (and the event reason when present) */
		arg = 1 << offset;
		sony_nc_int_call(sony_nc_acpi_handle, "SN05", &arg, &result);

	} else {
		/* old style event */
		ev_type = HOTKEY;
		sony_laptop_report_input_event(real_ev);
	}
	acpi_bus_generate_netlink_event(sony_nc_acpi_device->pnp.device_class,
			dev_name(&sony_nc_acpi_device->dev), ev_type, real_ev);
}

static acpi_status sony_walk_callback(acpi_handle handle, u32 level,
				      void *context, void **return_value)
{
	struct acpi_device_info *info;

	if (ACPI_SUCCESS(acpi_get_object_info(handle, &info))) {
		pr_warn("method: name: %4.4s, args %X\n",
			(char *)&info->name, info->param_count);

		kfree(info);
	}

	return AE_OK;
}

/*
 * ACPI device
 */
static void sony_nc_function_setup(struct acpi_device *device,
		struct platform_device *pf_device)
{
	unsigned int i, result, bitmask, arg;

	if (!handles)
		return;

	/* setup found handles here */
	for (i = 0; i < ARRAY_SIZE(handles->cap); i++) {
		unsigned int handle = handles->cap[i];

		if (!handle)
			continue;

		dprintk("setting up handle 0x%.4x\n", handle);

		switch (handle) {
		case 0x0100:
		case 0x0101:
		case 0x0127:
			/* setup hotkeys */
			sony_call_snc_handle(handle, 0, &result);
			break;
		case 0x0102:
			/* setup hotkeys */
			sony_call_snc_handle(handle, 0x100, &result);
			break;
		case 0x0105:
		case 0x0148:
			/* touchpad enable/disable */
			result = sony_nc_touchpad_setup(pf_device, handle);
			if (result)
				pr_err("couldn't set up touchpad control function (%d)\n",
						result);
			break;
		case 0x0115:
		case 0x0136:
		case 0x013f:
			result = sony_nc_battery_care_setup(pf_device, handle);
			if (result)
				pr_err("couldn't set up battery care function (%d)\n",
						result);
			break;
		case 0x0119:
		case 0x015D:
			result = sony_nc_lid_resume_setup(pf_device, handle);
			if (result)
				pr_err("couldn't set up lid resume function (%d)\n",
						result);
			break;
		case 0x0122:
			result = sony_nc_thermal_setup(pf_device);
			if (result)
				pr_err("couldn't set up thermal profile function (%d)\n",
						result);
			break;
		case 0x0128:
		case 0x0146:
		case 0x015B:
			result = sony_nc_gfx_switch_setup(pf_device, handle);
			if (result)
				pr_err("couldn't set up GFX Switch status (%d)\n",
						result);
			break;
		case 0x0131:
			result = sony_nc_highspeed_charging_setup(pf_device);
			if (result)
				pr_err("couldn't set up high speed charging function (%d)\n",
				       result);
			break;
		case 0x0124:
		case 0x0135:
			result = sony_nc_rfkill_setup(device, handle);
			if (result)
				pr_err("couldn't set up rfkill support (%d)\n",
						result);
			break;
		case 0x0137:
		case 0x0143:
		case 0x014b:
		case 0x014c:
		case 0x0163:
			result = sony_nc_kbd_backlight_setup(pf_device, handle);
			if (result)
				pr_err("couldn't set up keyboard backlight function (%d)\n",
						result);
			break;
		case 0x0121:
			result = sony_nc_lowbatt_setup(pf_device);
			if (result)
				pr_err("couldn't set up low battery function (%d)\n",
				       result);
			break;
		case 0x0149:
			result = sony_nc_fanspeed_setup(pf_device);
			if (result)
				pr_err("couldn't set up fan speed function (%d)\n",
				       result);
			break;
		case 0x0155:
			result = sony_nc_usb_charge_setup(pf_device);
			if (result)
				pr_err("couldn't set up USB charge support (%d)\n",
						result);
			break;
		case 0x011D:
			result = sony_nc_panelid_setup(pf_device);
			if (result)
				pr_err("couldn't set up panel ID function (%d)\n",
				       result);
			break;
		case 0x0168:
			result = sony_nc_smart_conn_setup(pf_device);
			if (result)
				pr_err("couldn't set up smart connect support (%d)\n",
						result);
			break;
		default:
			continue;
		}
	}

	/* Enable all events */
	arg = 0x10;
	if (!sony_nc_int_call(sony_nc_acpi_handle, "SN00", &arg, &bitmask))
		sony_nc_int_call(sony_nc_acpi_handle, "SN02", &bitmask,
				&result);
}

static void sony_nc_function_cleanup(struct platform_device *pd)
{
	unsigned int i, result, bitmask, handle;

	/* get enabled events and disable them */
	sony_nc_int_call(sony_nc_acpi_handle, "SN01", NULL, &bitmask);
	sony_nc_int_call(sony_nc_acpi_handle, "SN03", &bitmask, &result);

	/* cleanup handles here */
	for (i = 0; i < ARRAY_SIZE(handles->cap); i++) {

		handle = handles->cap[i];

		if (!handle)
			continue;

		switch (handle) {
		case 0x0105:
		case 0x0148:
			sony_nc_touchpad_cleanup(pd);
			break;
		case 0x0115:
		case 0x0136:
		case 0x013f:
			sony_nc_battery_care_cleanup(pd);
			break;
		case 0x0119:
		case 0x015D:
			sony_nc_lid_resume_cleanup(pd);
			break;
		case 0x0122:
			sony_nc_thermal_cleanup(pd);
			break;
		case 0x0128:
		case 0x0146:
		case 0x015B:
			sony_nc_gfx_switch_cleanup(pd);
			break;
		case 0x0131:
			sony_nc_highspeed_charging_cleanup(pd);
			break;
		case 0x0124:
		case 0x0135:
			sony_nc_rfkill_cleanup();
			break;
		case 0x0137:
		case 0x0143:
		case 0x014b:
		case 0x014c:
		case 0x0163:
			sony_nc_kbd_backlight_cleanup(pd, handle);
			break;
		case 0x0121:
			sony_nc_lowbatt_cleanup(pd);
			break;
		case 0x0149:
			sony_nc_fanspeed_cleanup(pd);
			break;
		case 0x0155:
			sony_nc_usb_charge_cleanup(pd);
			break;
		case 0x011D:
			sony_nc_panelid_cleanup(pd);
			break;
		case 0x0168:
			sony_nc_smart_conn_cleanup(pd);
			break;
		default:
			continue;
		}
	}

	/* finally cleanup the handles list */
	sony_nc_handles_cleanup(pd);
}

#ifdef CONFIG_PM_SLEEP
static void sony_nc_function_resume(void)
{
	unsigned int i, result, bitmask, arg;

	dprintk("Resuming SNC device\n");

	for (i = 0; i < ARRAY_SIZE(handles->cap); i++) {
		unsigned int handle = handles->cap[i];

		if (!handle)
			continue;

		switch (handle) {
		case 0x0100:
		case 0x0101:
		case 0x0127:
			/* re-enable hotkeys */
			sony_call_snc_handle(handle, 0, &result);
			break;
		case 0x0102:
			/* re-enable hotkeys */
			sony_call_snc_handle(handle, 0x100, &result);
			break;
		case 0x0122:
			sony_nc_thermal_resume();
			break;
		case 0x0124:
		case 0x0135:
			sony_nc_rfkill_update();
			break;
		default:
			continue;
		}
	}

	/* Enable all events */
	arg = 0x10;
	if (!sony_nc_int_call(sony_nc_acpi_handle, "SN00", &arg, &bitmask))
		sony_nc_int_call(sony_nc_acpi_handle, "SN02", &bitmask,
				&result);
}

static int sony_nc_resume(struct device *dev)
{
	struct sony_nc_value *item;

	for (item = sony_nc_values; item->name; item++) {
		int ret;

		if (!item->valid)
			continue;
		ret = sony_nc_int_call(sony_nc_acpi_handle, *item->acpiset,
				       &item->value, NULL);
		if (ret < 0) {
			pr_err("%s: %d\n", __func__, ret);
			break;
		}
	}

	if (acpi_has_method(sony_nc_acpi_handle, "ECON")) {
		int arg = 1;
		if (sony_nc_int_call(sony_nc_acpi_handle, "ECON", &arg, NULL))
			dprintk("ECON Method failed\n");
	}

	if (acpi_has_method(sony_nc_acpi_handle, "SN00"))
		sony_nc_function_resume();

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(sony_nc_pm, NULL, sony_nc_resume);

static void sony_nc_rfkill_cleanup(void)
{
	int i;

	for (i = 0; i < N_SONY_RFKILL; i++) {
		if (sony_rfkill_devices[i]) {
			rfkill_unregister(sony_rfkill_devices[i]);
			rfkill_destroy(sony_rfkill_devices[i]);
		}
	}
}

static int sony_nc_rfkill_set(void *data, bool blocked)
{
	int result;
	int argument = sony_rfkill_address[(long) data] + 0x100;

	if (!blocked)
		argument |= 0x070000;

	return sony_call_snc_handle(sony_rfkill_handle, argument, &result);
}

static const struct rfkill_ops sony_rfkill_ops = {
	.set_block = sony_nc_rfkill_set,
};

static int sony_nc_setup_rfkill(struct acpi_device *device,
				enum sony_nc_rfkill nc_type)
{
	int err = 0;
	struct rfkill *rfk;
	enum rfkill_type type;
	const char *name;
	int result;
	bool hwblock, swblock;

	switch (nc_type) {
	case SONY_WIFI:
		type = RFKILL_TYPE_WLAN;
		name = "sony-wifi";
		break;
	case SONY_BLUETOOTH:
		type = RFKILL_TYPE_BLUETOOTH;
		name = "sony-bluetooth";
		break;
	case SONY_WWAN:
		type = RFKILL_TYPE_WWAN;
		name = "sony-wwan";
		break;
	case SONY_WIMAX:
		type = RFKILL_TYPE_WIMAX;
		name = "sony-wimax";
		break;
	default:
		return -EINVAL;
	}

	rfk = rfkill_alloc(name, &device->dev, type,
			   &sony_rfkill_ops, (void *)nc_type);
	if (!rfk)
		return -ENOMEM;

	if (sony_call_snc_handle(sony_rfkill_handle, 0x200, &result) < 0) {
		rfkill_destroy(rfk);
		return -1;
	}
	hwblock = !(result & 0x1);

	if (sony_call_snc_handle(sony_rfkill_handle,
				sony_rfkill_address[nc_type],
				&result) < 0) {
		rfkill_destroy(rfk);
		return -1;
	}
	swblock = !(result & 0x2);

	rfkill_init_sw_state(rfk, swblock);
	rfkill_set_hw_state(rfk, hwblock);

	err = rfkill_register(rfk);
	if (err) {
		rfkill_destroy(rfk);
		return err;
	}
	sony_rfkill_devices[nc_type] = rfk;
	return err;
}

static void sony_nc_rfkill_update(void)
{
	enum sony_nc_rfkill i;
	int result;
	bool hwblock;

	sony_call_snc_handle(sony_rfkill_handle, 0x200, &result);
	hwblock = !(result & 0x1);

	for (i = 0; i < N_SONY_RFKILL; i++) {
		int argument = sony_rfkill_address[i];

		if (!sony_rfkill_devices[i])
			continue;

		if (hwblock) {
			if (rfkill_set_hw_state(sony_rfkill_devices[i], true)) {
				/* we already know we're blocked */
			}
			continue;
		}

		sony_call_snc_handle(sony_rfkill_handle, argument, &result);
		rfkill_set_states(sony_rfkill_devices[i],
				  !(result & 0x2), false);
	}
}

static int sony_nc_rfkill_setup(struct acpi_device *device,
		unsigned int handle)
{
	u64 offset;
	int i;
	unsigned char buffer[32] = { 0 };

	offset = sony_find_snc_handle(handle);
	sony_rfkill_handle = handle;

	i = sony_nc_buffer_call(sony_nc_acpi_handle, "SN06", &offset, buffer,
			32);
	if (i < 0)
		return i;

	/* The buffer is filled with magic numbers describing the devices
	 * available, 0xff terminates the enumeration.
	 * Known codes:
	 *	0x00 WLAN
	 *	0x10 BLUETOOTH
	 *	0x20 WWAN GPRS-EDGE
	 *	0x21 WWAN HSDPA
	 *	0x22 WWAN EV-DO
	 *	0x23 WWAN GPS
	 *	0x25 Gobi WWAN no GPS
	 *	0x26 Gobi WWAN + GPS
	 *	0x28 Gobi WWAN no GPS
	 *	0x29 Gobi WWAN + GPS
	 *	0x30 WIMAX
	 *	0x50 Gobi WWAN no GPS
	 *	0x51 Gobi WWAN + GPS
	 *	0x70 no SIM card slot
	 *	0x71 SIM card slot
	 */
	for (i = 0; i < ARRAY_SIZE(buffer); i++) {

		if (buffer[i] == 0xff)
			break;

		dprintk("Radio devices, found 0x%.2x\n", buffer[i]);

		if (buffer[i] == 0 && !sony_rfkill_devices[SONY_WIFI])
			sony_nc_setup_rfkill(device, SONY_WIFI);

		if (buffer[i] == 0x10 && !sony_rfkill_devices[SONY_BLUETOOTH])
			sony_nc_setup_rfkill(device, SONY_BLUETOOTH);

		if (((0xf0 & buffer[i]) == 0x20 ||
					(0xf0 & buffer[i]) == 0x50) &&
				!sony_rfkill_devices[SONY_WWAN])
			sony_nc_setup_rfkill(device, SONY_WWAN);

		if (buffer[i] == 0x30 && !sony_rfkill_devices[SONY_WIMAX])
			sony_nc_setup_rfkill(device, SONY_WIMAX);
	}
	return 0;
}

/* Keyboard backlight feature */
struct kbd_backlight {
	unsigned int handle;
	unsigned int base;
	unsigned int mode;
	unsigned int timeout;
	struct device_attribute mode_attr;
	struct device_attribute timeout_attr;
};

static struct kbd_backlight *kbdbl_ctl;

static ssize_t __sony_nc_kbd_backlight_mode_set(u8 value)
{
	int result;

	if (value > 2)
		return -EINVAL;

	if (sony_call_snc_handle(kbdbl_ctl->handle,
				(value << 0x10) | (kbdbl_ctl->base), &result))
		return -EIO;

	/* Try to turn the light on/off immediately */
	if (value != 1)
		sony_call_snc_handle(kbdbl_ctl->handle,
				(value << 0x0f) | (kbdbl_ctl->base + 0x100),
				&result);

	kbdbl_ctl->mode = value;

	return 0;
}

static ssize_t sony_nc_kbd_backlight_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t count)
{
	int ret = 0;
	unsigned long value;

	if (count > 31)
		return -EINVAL;

	if (kstrtoul(buffer, 10, &value))
		return -EINVAL;

	ret = __sony_nc_kbd_backlight_mode_set(value);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t sony_nc_kbd_backlight_mode_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	ssize_t count = 0;
	count = snprintf(buffer, PAGE_SIZE, "%d\n", kbdbl_ctl->mode);
	return count;
}

static int __sony_nc_kbd_backlight_timeout_set(u8 value)
{
	int result;

	if (value > 3)
		return -EINVAL;

	if (sony_call_snc_handle(kbdbl_ctl->handle, (value << 0x10) |
				(kbdbl_ctl->base + 0x200), &result))
		return -EIO;

	kbdbl_ctl->timeout = value;

	return 0;
}

static ssize_t sony_nc_kbd_backlight_timeout_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t count)
{
	int ret = 0;
	unsigned long value;

	if (count > 31)
		return -EINVAL;

	if (kstrtoul(buffer, 10, &value))
		return -EINVAL;

	ret = __sony_nc_kbd_backlight_timeout_set(value);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t sony_nc_kbd_backlight_timeout_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	ssize_t count = 0;
	count = snprintf(buffer, PAGE_SIZE, "%d\n", kbdbl_ctl->timeout);
	return count;
}

static int sony_nc_kbd_backlight_setup(struct platform_device *pd,
		unsigned int handle)
{
	int result;
	int ret = 0;

	if (kbdbl_ctl) {
		pr_warn("handle 0x%.4x: keyboard backlight setup already done for 0x%.4x\n",
				handle, kbdbl_ctl->handle);
		return -EBUSY;
	}

	/* verify the kbd backlight presence, these handles are not used for
	 * keyboard backlight only
	 */
	ret = sony_call_snc_handle(handle, handle == 0x0137 ? 0x0B00 : 0x0100,
			&result);
	if (ret)
		return ret;

	if ((handle == 0x0137 && !(result & 0x02)) ||
			!(result & 0x01)) {
		dprintk("no backlight keyboard found\n");
		return 0;
	}

	kbdbl_ctl = kzalloc(sizeof(*kbdbl_ctl), GFP_KERNEL);
	if (!kbdbl_ctl)
		return -ENOMEM;

	kbdbl_ctl->mode = kbd_backlight;
	kbdbl_ctl->timeout = kbd_backlight_timeout;
	kbdbl_ctl->handle = handle;
	if (handle == 0x0137)
		kbdbl_ctl->base = 0x0C00;
	else
		kbdbl_ctl->base = 0x4000;

	sysfs_attr_init(&kbdbl_ctl->mode_attr.attr);
	kbdbl_ctl->mode_attr.attr.name = "kbd_backlight";
	kbdbl_ctl->mode_attr.attr.mode = S_IRUGO | S_IWUSR;
	kbdbl_ctl->mode_attr.show = sony_nc_kbd_backlight_mode_show;
	kbdbl_ctl->mode_attr.store = sony_nc_kbd_backlight_mode_store;

	sysfs_attr_init(&kbdbl_ctl->timeout_attr.attr);
	kbdbl_ctl->timeout_attr.attr.name = "kbd_backlight_timeout";
	kbdbl_ctl->timeout_attr.attr.mode = S_IRUGO | S_IWUSR;
	kbdbl_ctl->timeout_attr.show = sony_nc_kbd_backlight_timeout_show;
	kbdbl_ctl->timeout_attr.store = sony_nc_kbd_backlight_timeout_store;

	ret = device_create_file(&pd->dev, &kbdbl_ctl->mode_attr);
	if (ret)
		goto outkzalloc;

	ret = device_create_file(&pd->dev, &kbdbl_ctl->timeout_attr);
	if (ret)
		goto outmode;

	__sony_nc_kbd_backlight_mode_set(kbdbl_ctl->mode);
	__sony_nc_kbd_backlight_timeout_set(kbdbl_ctl->timeout);

	return 0;

outmode:
	device_remove_file(&pd->dev, &kbdbl_ctl->mode_attr);
outkzalloc:
	kfree(kbdbl_ctl);
	kbdbl_ctl = NULL;
	return ret;
}

static void sony_nc_kbd_backlight_cleanup(struct platform_device *pd,
		unsigned int handle)
{
	if (kbdbl_ctl && handle == kbdbl_ctl->handle) {
		device_remove_file(&pd->dev, &kbdbl_ctl->mode_attr);
		device_remove_file(&pd->dev, &kbdbl_ctl->timeout_attr);
		kfree(kbdbl_ctl);
		kbdbl_ctl = NULL;
	}
}

struct battery_care_control {
	struct device_attribute attrs[2];
	unsigned int handle;
};
static struct battery_care_control *bcare_ctl;

static ssize_t sony_nc_battery_care_limit_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t count)
{
	unsigned int result, cmd;
	unsigned long value;

	if (count > 31)
		return -EINVAL;

	if (kstrtoul(buffer, 10, &value))
		return -EINVAL;

	/*  limit values (2 bits):
	 *  00 - none
	 *  01 - 80%
	 *  10 - 50%
	 *  11 - 100%
	 *
	 *  bit 0: 0 disable BCL, 1 enable BCL
	 *  bit 1: 1 tell to store the battery limit (see bits 6,7) too
	 *  bits 2,3: reserved
	 *  bits 4,5: store the limit into the EC
	 *  bits 6,7: store the limit into the battery
	 */
	cmd = 0;

	if (value > 0) {
		if (value <= 50)
			cmd = 0x20;

		else if (value <= 80)
			cmd = 0x10;

		else if (value <= 100)
			cmd = 0x30;

		else
			return -EINVAL;

		/*
		 * handle 0x0115 should allow storing on battery too;
		 * handle 0x0136 same as 0x0115 + health status;
		 * handle 0x013f, same as 0x0136 but no storing on the battery
		 */
		if (bcare_ctl->handle != 0x013f)
			cmd = cmd | (cmd << 2);

		cmd = (cmd | 0x1) << 0x10;
	}

	if (sony_call_snc_handle(bcare_ctl->handle, cmd | 0x0100, &result))
		return -EIO;

	return count;
}

static ssize_t sony_nc_battery_care_limit_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	unsigned int result, status;

	if (sony_call_snc_handle(bcare_ctl->handle, 0x0000, &result))
		return -EIO;

	status = (result & 0x01) ? ((result & 0x30) >> 0x04) : 0;
	switch (status) {
	case 1:
		status = 80;
		break;
	case 2:
		status = 50;
		break;
	case 3:
		status = 100;
		break;
	default:
		status = 0;
		break;
	}

	return snprintf(buffer, PAGE_SIZE, "%d\n", status);
}

static ssize_t sony_nc_battery_care_health_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	ssize_t count = 0;
	unsigned int health;

	if (sony_call_snc_handle(bcare_ctl->handle, 0x0200, &health))
		return -EIO;

	count = snprintf(buffer, PAGE_SIZE, "%d\n", health & 0xff);

	return count;
}

static int sony_nc_battery_care_setup(struct platform_device *pd,
		unsigned int handle)
{
	int ret = 0;

	bcare_ctl = kzalloc(sizeof(struct battery_care_control), GFP_KERNEL);
	if (!bcare_ctl)
		return -ENOMEM;

	bcare_ctl->handle = handle;

	sysfs_attr_init(&bcare_ctl->attrs[0].attr);
	bcare_ctl->attrs[0].attr.name = "battery_care_limiter";
	bcare_ctl->attrs[0].attr.mode = S_IRUGO | S_IWUSR;
	bcare_ctl->attrs[0].show = sony_nc_battery_care_limit_show;
	bcare_ctl->attrs[0].store = sony_nc_battery_care_limit_store;

	ret = device_create_file(&pd->dev, &bcare_ctl->attrs[0]);
	if (ret)
		goto outkzalloc;

	/* 0x0115 is for models with no health reporting capability */
	if (handle == 0x0115)
		return 0;

	sysfs_attr_init(&bcare_ctl->attrs[1].attr);
	bcare_ctl->attrs[1].attr.name = "battery_care_health";
	bcare_ctl->attrs[1].attr.mode = S_IRUGO;
	bcare_ctl->attrs[1].show = sony_nc_battery_care_health_show;

	ret = device_create_file(&pd->dev, &bcare_ctl->attrs[1]);
	if (ret)
		goto outlimiter;

	return 0;

outlimiter:
	device_remove_file(&pd->dev, &bcare_ctl->attrs[0]);

outkzalloc:
	kfree(bcare_ctl);
	bcare_ctl = NULL;

	return ret;
}

static void sony_nc_battery_care_cleanup(struct platform_device *pd)
{
	if (bcare_ctl) {
		device_remove_file(&pd->dev, &bcare_ctl->attrs[0]);
		if (bcare_ctl->handle != 0x0115)
			device_remove_file(&pd->dev, &bcare_ctl->attrs[1]);

		kfree(bcare_ctl);
		bcare_ctl = NULL;
	}
}

struct snc_thermal_ctrl {
	unsigned int mode;
	unsigned int profiles;
	struct device_attribute mode_attr;
	struct device_attribute profiles_attr;
};
static struct snc_thermal_ctrl *th_handle;

#define THM_PROFILE_MAX 3
static const char * const snc_thermal_profiles[] = {
	"balanced",
	"silent",
	"performance"
};

static int sony_nc_thermal_mode_set(unsigned short mode)
{
	unsigned int result;

	/* the thermal profile seems to be a two bit bitmask:
	 * lsb -> silent
	 * msb -> performance
	 * no bit set is the normal operation and is always valid
	 * Some vaio models only have "balanced" and "performance"
	 */
	if ((mode && !(th_handle->profiles & mode)) || mode >= THM_PROFILE_MAX)
		return -EINVAL;

	if (sony_call_snc_handle(0x0122, mode << 0x10 | 0x0200, &result))
		return -EIO;

	th_handle->mode = mode;

	return 0;
}

static int sony_nc_thermal_mode_get(void)
{
	unsigned int result;

	if (sony_call_snc_handle(0x0122, 0x0100, &result))
		return -EIO;

	return result & 0xff;
}

static ssize_t sony_nc_thermal_profiles_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	short cnt;
	size_t idx = 0;

	for (cnt = 0; cnt < THM_PROFILE_MAX; cnt++) {
		if (!cnt || (th_handle->profiles & cnt))
			idx += snprintf(buffer + idx, PAGE_SIZE - idx, "%s ",
					snc_thermal_profiles[cnt]);
	}
	idx += snprintf(buffer + idx, PAGE_SIZE - idx, "\n");

	return idx;
}

static ssize_t sony_nc_thermal_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t count)
{
	unsigned short cmd;
	size_t len = count;

	if (count == 0)
		return -EINVAL;

	/* skip the newline if present */
	if (buffer[len - 1] == '\n')
		len--;

	for (cmd = 0; cmd < THM_PROFILE_MAX; cmd++)
		if (strncmp(buffer, snc_thermal_profiles[cmd], len) == 0)
			break;

	if (sony_nc_thermal_mode_set(cmd))
		return -EIO;

	return count;
}

static ssize_t sony_nc_thermal_mode_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	ssize_t count = 0;
	int mode = sony_nc_thermal_mode_get();

	if (mode < 0)
		return mode;

	count = snprintf(buffer, PAGE_SIZE, "%s\n", snc_thermal_profiles[mode]);

	return count;
}

static int sony_nc_thermal_setup(struct platform_device *pd)
{
	int ret = 0;
	th_handle = kzalloc(sizeof(struct snc_thermal_ctrl), GFP_KERNEL);
	if (!th_handle)
		return -ENOMEM;

	ret = sony_call_snc_handle(0x0122, 0x0000, &th_handle->profiles);
	if (ret) {
		pr_warn("couldn't to read the thermal profiles\n");
		goto outkzalloc;
	}

	ret = sony_nc_thermal_mode_get();
	if (ret < 0) {
		pr_warn("couldn't to read the current thermal profile");
		goto outkzalloc;
	}
	th_handle->mode = ret;

	sysfs_attr_init(&th_handle->profiles_attr.attr);
	th_handle->profiles_attr.attr.name = "thermal_profiles";
	th_handle->profiles_attr.attr.mode = S_IRUGO;
	th_handle->profiles_attr.show = sony_nc_thermal_profiles_show;

	sysfs_attr_init(&th_handle->mode_attr.attr);
	th_handle->mode_attr.attr.name = "thermal_control";
	th_handle->mode_attr.attr.mode = S_IRUGO | S_IWUSR;
	th_handle->mode_attr.show = sony_nc_thermal_mode_show;
	th_handle->mode_attr.store = sony_nc_thermal_mode_store;

	ret = device_create_file(&pd->dev, &th_handle->profiles_attr);
	if (ret)
		goto outkzalloc;

	ret = device_create_file(&pd->dev, &th_handle->mode_attr);
	if (ret)
		goto outprofiles;

	return 0;

outprofiles:
	device_remove_file(&pd->dev, &th_handle->profiles_attr);
outkzalloc:
	kfree(th_handle);
	th_handle = NULL;
	return ret;
}

static void sony_nc_thermal_cleanup(struct platform_device *pd)
{
	if (th_handle) {
		device_remove_file(&pd->dev, &th_handle->profiles_attr);
		device_remove_file(&pd->dev, &th_handle->mode_attr);
		kfree(th_handle);
		th_handle = NULL;
	}
}

#ifdef CONFIG_PM_SLEEP
static void sony_nc_thermal_resume(void)
{
	unsigned int status = sony_nc_thermal_mode_get();

	if (status != th_handle->mode)
		sony_nc_thermal_mode_set(th_handle->mode);
}
#endif

/* resume on LID open */
#define LID_RESUME_S5	0
#define LID_RESUME_S4	1
#define LID_RESUME_S3	2
#define LID_RESUME_MAX	3
struct snc_lid_resume_control {
	struct device_attribute attrs[LID_RESUME_MAX];
	unsigned int status;
	int handle;
};
static struct snc_lid_resume_control *lid_ctl;

static ssize_t sony_nc_lid_resume_store(struct device *dev,
					struct device_attribute *attr,
					const char *buffer, size_t count)
{
	unsigned int result;
	unsigned long value;
	unsigned int pos = LID_RESUME_S5;
	if (count > 31)
		return -EINVAL;

	if (kstrtoul(buffer, 10, &value) || value > 1)
		return -EINVAL;

	/* the value we have to write to SNC is a bitmask:
	 * +--------------+
	 * | S3 | S4 | S5 |
	 * +--------------+
	 *   2    1    0
	 */
	while (pos < LID_RESUME_MAX) {
		if (&lid_ctl->attrs[pos].attr == &attr->attr)
			break;
		pos++;
	}
	if (pos == LID_RESUME_MAX)
		return -EINVAL;

	if (value)
		value = lid_ctl->status | (1 << pos);
	else
		value = lid_ctl->status & ~(1 << pos);

	if (sony_call_snc_handle(lid_ctl->handle, value << 0x10 | 0x0100,
				&result))
		return -EIO;

	lid_ctl->status = value;

	return count;
}

static ssize_t sony_nc_lid_resume_show(struct device *dev,
					struct device_attribute *attr,
					char *buffer)
{
	unsigned int pos = LID_RESUME_S5;

	while (pos < LID_RESUME_MAX) {
		if (&lid_ctl->attrs[pos].attr == &attr->attr)
			return snprintf(buffer, PAGE_SIZE, "%d\n",
					(lid_ctl->status >> pos) & 0x01);
		pos++;
	}
	return -EINVAL;
}

static int sony_nc_lid_resume_setup(struct platform_device *pd,
					unsigned int handle)
{
	unsigned int result;
	int i;

	if (sony_call_snc_handle(handle, 0x0000, &result))
		return -EIO;

	lid_ctl = kzalloc(sizeof(struct snc_lid_resume_control), GFP_KERNEL);
	if (!lid_ctl)
		return -ENOMEM;

	lid_ctl->status = result & 0x7;
	lid_ctl->handle = handle;

	sysfs_attr_init(&lid_ctl->attrs[0].attr);
	lid_ctl->attrs[LID_RESUME_S5].attr.name = "lid_resume_S5";
	lid_ctl->attrs[LID_RESUME_S5].attr.mode = S_IRUGO | S_IWUSR;
	lid_ctl->attrs[LID_RESUME_S5].show = sony_nc_lid_resume_show;
	lid_ctl->attrs[LID_RESUME_S5].store = sony_nc_lid_resume_store;

	if (handle == 0x0119) {
		sysfs_attr_init(&lid_ctl->attrs[1].attr);
		lid_ctl->attrs[LID_RESUME_S4].attr.name = "lid_resume_S4";
		lid_ctl->attrs[LID_RESUME_S4].attr.mode = S_IRUGO | S_IWUSR;
		lid_ctl->attrs[LID_RESUME_S4].show = sony_nc_lid_resume_show;
		lid_ctl->attrs[LID_RESUME_S4].store = sony_nc_lid_resume_store;

		sysfs_attr_init(&lid_ctl->attrs[2].attr);
		lid_ctl->attrs[LID_RESUME_S3].attr.name = "lid_resume_S3";
		lid_ctl->attrs[LID_RESUME_S3].attr.mode = S_IRUGO | S_IWUSR;
		lid_ctl->attrs[LID_RESUME_S3].show = sony_nc_lid_resume_show;
		lid_ctl->attrs[LID_RESUME_S3].store = sony_nc_lid_resume_store;
	}
	for (i = 0; i < LID_RESUME_MAX &&
			lid_ctl->attrs[LID_RESUME_S3].attr.name; i++) {
		result = device_create_file(&pd->dev, &lid_ctl->attrs[i]);
		if (result)
			goto liderror;
	}

	return 0;

liderror:
	for (i--; i >= 0; i--)
		device_remove_file(&pd->dev, &lid_ctl->attrs[i]);

	kfree(lid_ctl);
	lid_ctl = NULL;

	return result;
}

static void sony_nc_lid_resume_cleanup(struct platform_device *pd)
{
	int i;

	if (lid_ctl) {
		for (i = 0; i < LID_RESUME_MAX; i++) {
			if (!lid_ctl->attrs[i].attr.name)
				break;

			device_remove_file(&pd->dev, &lid_ctl->attrs[i]);
		}

		kfree(lid_ctl);
		lid_ctl = NULL;
	}
}

/* GFX Switch position */
enum gfx_switch {
	SPEED,
	STAMINA,
	AUTO
};
struct snc_gfx_switch_control {
	struct device_attribute attr;
	unsigned int handle;
};
static struct snc_gfx_switch_control *gfxs_ctl;

/* returns 0 for speed, 1 for stamina */
static int __sony_nc_gfx_switch_status_get(void)
{
	unsigned int result;

	if (sony_call_snc_handle(gfxs_ctl->handle,
				gfxs_ctl->handle == 0x015B ? 0x0000 : 0x0100,
				&result))
		return -EIO;

	switch (gfxs_ctl->handle) {
	case 0x0146:
		/* 1: discrete GFX (speed)
		 * 0: integrated GFX (stamina)
		 */
		return result & 0x1 ? SPEED : STAMINA;
		break;
	case 0x015B:
		/* 0: discrete GFX (speed)
		 * 1: integrated GFX (stamina)
		 */
		return result & 0x1 ? STAMINA : SPEED;
		break;
	case 0x0128:
		/* it's a more elaborated bitmask, for now:
		 * 2: integrated GFX (stamina)
		 * 0: discrete GFX (speed)
		 */
		dprintk("GFX Status: 0x%x\n", result);
		return result & 0x80 ? AUTO :
			result & 0x02 ? STAMINA : SPEED;
		break;
	}
	return -EINVAL;
}

static ssize_t sony_nc_gfx_switch_status_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buffer)
{
	int pos = __sony_nc_gfx_switch_status_get();

	if (pos < 0)
		return pos;

	return snprintf(buffer, PAGE_SIZE, "%s\n",
					pos == SPEED ? "speed" :
					pos == STAMINA ? "stamina" :
					pos == AUTO ? "auto" : "unknown");
}

static int sony_nc_gfx_switch_setup(struct platform_device *pd,
		unsigned int handle)
{
	unsigned int result;

	gfxs_ctl = kzalloc(sizeof(struct snc_gfx_switch_control), GFP_KERNEL);
	if (!gfxs_ctl)
		return -ENOMEM;

	gfxs_ctl->handle = handle;

	sysfs_attr_init(&gfxs_ctl->attr.attr);
	gfxs_ctl->attr.attr.name = "gfx_switch_status";
	gfxs_ctl->attr.attr.mode = S_IRUGO;
	gfxs_ctl->attr.show = sony_nc_gfx_switch_status_show;

	result = device_create_file(&pd->dev, &gfxs_ctl->attr);
	if (result)
		goto gfxerror;

	return 0;

gfxerror:
	kfree(gfxs_ctl);
	gfxs_ctl = NULL;

	return result;
}

static void sony_nc_gfx_switch_cleanup(struct platform_device *pd)
{
	if (gfxs_ctl) {
		device_remove_file(&pd->dev, &gfxs_ctl->attr);

		kfree(gfxs_ctl);
		gfxs_ctl = NULL;
	}
}

/* High speed charging function */
static struct device_attribute *hsc_handle;

static ssize_t sony_nc_highspeed_charging_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t count)
{
	unsigned int result;
	unsigned long value;

	if (count > 31)
		return -EINVAL;

	if (kstrtoul(buffer, 10, &value) || value > 1)
		return -EINVAL;

	if (sony_call_snc_handle(0x0131, value << 0x10 | 0x0200, &result))
		return -EIO;

	return count;
}

static ssize_t sony_nc_highspeed_charging_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	unsigned int result;

	if (sony_call_snc_handle(0x0131, 0x0100, &result))
		return -EIO;

	return snprintf(buffer, PAGE_SIZE, "%d\n", result & 0x01);
}

static int sony_nc_highspeed_charging_setup(struct platform_device *pd)
{
	unsigned int result;

	if (sony_call_snc_handle(0x0131, 0x0000, &result) || !(result & 0x01)) {
		/* some models advertise the handle but have no implementation
		 * for it
		 */
		pr_info("No High Speed Charging capability found\n");
		return 0;
	}

	hsc_handle = kzalloc(sizeof(struct device_attribute), GFP_KERNEL);
	if (!hsc_handle)
		return -ENOMEM;

	sysfs_attr_init(&hsc_handle->attr);
	hsc_handle->attr.name = "battery_highspeed_charging";
	hsc_handle->attr.mode = S_IRUGO | S_IWUSR;
	hsc_handle->show = sony_nc_highspeed_charging_show;
	hsc_handle->store = sony_nc_highspeed_charging_store;

	result = device_create_file(&pd->dev, hsc_handle);
	if (result) {
		kfree(hsc_handle);
		hsc_handle = NULL;
		return result;
	}

	return 0;
}

static void sony_nc_highspeed_charging_cleanup(struct platform_device *pd)
{
	if (hsc_handle) {
		device_remove_file(&pd->dev, hsc_handle);
		kfree(hsc_handle);
		hsc_handle = NULL;
	}
}

/* low battery function */
static struct device_attribute *lowbatt_handle;

static ssize_t sony_nc_lowbatt_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t count)
{
	unsigned int result;
	unsigned long value;

	if (count > 31)
		return -EINVAL;

	if (kstrtoul(buffer, 10, &value) || value > 1)
		return -EINVAL;

	if (sony_call_snc_handle(0x0121, value << 8, &result))
		return -EIO;

	return count;
}

static ssize_t sony_nc_lowbatt_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	unsigned int result;

	if (sony_call_snc_handle(0x0121, 0x0200, &result))
		return -EIO;

	return snprintf(buffer, PAGE_SIZE, "%d\n", result & 1);
}

static int sony_nc_lowbatt_setup(struct platform_device *pd)
{
	unsigned int result;

	lowbatt_handle = kzalloc(sizeof(struct device_attribute), GFP_KERNEL);
	if (!lowbatt_handle)
		return -ENOMEM;

	sysfs_attr_init(&lowbatt_handle->attr);
	lowbatt_handle->attr.name = "lowbatt_hibernate";
	lowbatt_handle->attr.mode = S_IRUGO | S_IWUSR;
	lowbatt_handle->show = sony_nc_lowbatt_show;
	lowbatt_handle->store = sony_nc_lowbatt_store;

	result = device_create_file(&pd->dev, lowbatt_handle);
	if (result) {
		kfree(lowbatt_handle);
		lowbatt_handle = NULL;
		return result;
	}

	return 0;
}

static void sony_nc_lowbatt_cleanup(struct platform_device *pd)
{
	if (lowbatt_handle) {
		device_remove_file(&pd->dev, lowbatt_handle);
		kfree(lowbatt_handle);
		lowbatt_handle = NULL;
	}
}

/* fan speed function */
static struct device_attribute *fan_handle, *hsf_handle;

static ssize_t sony_nc_hsfan_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t count)
{
	unsigned int result;
	unsigned long value;

	if (count > 31)
		return -EINVAL;

	if (kstrtoul(buffer, 10, &value) || value > 1)
		return -EINVAL;

	if (sony_call_snc_handle(0x0149, value << 0x10 | 0x0200, &result))
		return -EIO;

	return count;
}

static ssize_t sony_nc_hsfan_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	unsigned int result;

	if (sony_call_snc_handle(0x0149, 0x0100, &result))
		return -EIO;

	return snprintf(buffer, PAGE_SIZE, "%d\n", result & 0x01);
}

static ssize_t sony_nc_fanspeed_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	unsigned int result;

	if (sony_call_snc_handle(0x0149, 0x0300, &result))
		return -EIO;

	return snprintf(buffer, PAGE_SIZE, "%d\n", result & 0xff);
}

static int sony_nc_fanspeed_setup(struct platform_device *pd)
{
	unsigned int result;

	fan_handle = kzalloc(sizeof(struct device_attribute), GFP_KERNEL);
	if (!fan_handle)
		return -ENOMEM;

	hsf_handle = kzalloc(sizeof(struct device_attribute), GFP_KERNEL);
	if (!hsf_handle) {
		result = -ENOMEM;
		goto out_hsf_handle_alloc;
	}

	sysfs_attr_init(&fan_handle->attr);
	fan_handle->attr.name = "fanspeed";
	fan_handle->attr.mode = S_IRUGO;
	fan_handle->show = sony_nc_fanspeed_show;
	fan_handle->store = NULL;

	sysfs_attr_init(&hsf_handle->attr);
	hsf_handle->attr.name = "fan_forced";
	hsf_handle->attr.mode = S_IRUGO | S_IWUSR;
	hsf_handle->show = sony_nc_hsfan_show;
	hsf_handle->store = sony_nc_hsfan_store;

	result = device_create_file(&pd->dev, fan_handle);
	if (result)
		goto out_fan_handle;

	result = device_create_file(&pd->dev, hsf_handle);
	if (result)
		goto out_hsf_handle;

	return 0;

out_hsf_handle:
	device_remove_file(&pd->dev, fan_handle);

out_fan_handle:
	kfree(hsf_handle);
	hsf_handle = NULL;

out_hsf_handle_alloc:
	kfree(fan_handle);
	fan_handle = NULL;
	return result;
}

static void sony_nc_fanspeed_cleanup(struct platform_device *pd)
{
	if (fan_handle) {
		device_remove_file(&pd->dev, fan_handle);
		kfree(fan_handle);
		fan_handle = NULL;
	}
	if (hsf_handle) {
		device_remove_file(&pd->dev, hsf_handle);
		kfree(hsf_handle);
		hsf_handle = NULL;
	}
}

/* USB charge function */
static struct device_attribute *uc_handle;

static ssize_t sony_nc_usb_charge_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t count)
{
	unsigned int result;
	unsigned long value;

	if (count > 31)
		return -EINVAL;

	if (kstrtoul(buffer, 10, &value) || value > 1)
		return -EINVAL;

	if (sony_call_snc_handle(0x0155, value << 0x10 | 0x0100, &result))
		return -EIO;

	return count;
}

static ssize_t sony_nc_usb_charge_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	unsigned int result;

	if (sony_call_snc_handle(0x0155, 0x0000, &result))
		return -EIO;

	return snprintf(buffer, PAGE_SIZE, "%d\n", result & 0x01);
}

static int sony_nc_usb_charge_setup(struct platform_device *pd)
{
	unsigned int result;

	if (sony_call_snc_handle(0x0155, 0x0000, &result) || !(result & 0x01)) {
		/* some models advertise the handle but have no implementation
		 * for it
		 */
		pr_info("No USB Charge capability found\n");
		return 0;
	}

	uc_handle = kzalloc(sizeof(struct device_attribute), GFP_KERNEL);
	if (!uc_handle)
		return -ENOMEM;

	sysfs_attr_init(&uc_handle->attr);
	uc_handle->attr.name = "usb_charge";
	uc_handle->attr.mode = S_IRUGO | S_IWUSR;
	uc_handle->show = sony_nc_usb_charge_show;
	uc_handle->store = sony_nc_usb_charge_store;

	result = device_create_file(&pd->dev, uc_handle);
	if (result) {
		kfree(uc_handle);
		uc_handle = NULL;
		return result;
	}

	return 0;
}

static void sony_nc_usb_charge_cleanup(struct platform_device *pd)
{
	if (uc_handle) {
		device_remove_file(&pd->dev, uc_handle);
		kfree(uc_handle);
		uc_handle = NULL;
	}
}

/* Panel ID function */
static struct device_attribute *panel_handle;

static ssize_t sony_nc_panelid_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	unsigned int result;

	if (sony_call_snc_handle(0x011D, 0x0000, &result))
		return -EIO;

	return snprintf(buffer, PAGE_SIZE, "%d\n", result);
}

static int sony_nc_panelid_setup(struct platform_device *pd)
{
	unsigned int result;

	panel_handle = kzalloc(sizeof(struct device_attribute), GFP_KERNEL);
	if (!panel_handle)
		return -ENOMEM;

	sysfs_attr_init(&panel_handle->attr);
	panel_handle->attr.name = "panel_id";
	panel_handle->attr.mode = S_IRUGO;
	panel_handle->show = sony_nc_panelid_show;
	panel_handle->store = NULL;

	result = device_create_file(&pd->dev, panel_handle);
	if (result) {
		kfree(panel_handle);
		panel_handle = NULL;
		return result;
	}

	return 0;
}

static void sony_nc_panelid_cleanup(struct platform_device *pd)
{
	if (panel_handle) {
		device_remove_file(&pd->dev, panel_handle);
		kfree(panel_handle);
		panel_handle = NULL;
	}
}

/* smart connect function */
static struct device_attribute *sc_handle;

static ssize_t sony_nc_smart_conn_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t count)
{
	unsigned int result;
	unsigned long value;

	if (count > 31)
		return -EINVAL;

	if (kstrtoul(buffer, 10, &value) || value > 1)
		return -EINVAL;

	if (sony_call_snc_handle(0x0168, value << 0x10, &result))
		return -EIO;

	return count;
}

static int sony_nc_smart_conn_setup(struct platform_device *pd)
{
	unsigned int result;

	sc_handle = kzalloc(sizeof(struct device_attribute), GFP_KERNEL);
	if (!sc_handle)
		return -ENOMEM;

	sysfs_attr_init(&sc_handle->attr);
	sc_handle->attr.name = "smart_connect";
	sc_handle->attr.mode = S_IWUSR;
	sc_handle->show = NULL;
	sc_handle->store = sony_nc_smart_conn_store;

	result = device_create_file(&pd->dev, sc_handle);
	if (result) {
		kfree(sc_handle);
		sc_handle = NULL;
		return result;
	}

	return 0;
}

static void sony_nc_smart_conn_cleanup(struct platform_device *pd)
{
	if (sc_handle) {
		device_remove_file(&pd->dev, sc_handle);
		kfree(sc_handle);
		sc_handle = NULL;
	}
}

/* Touchpad enable/disable */
struct touchpad_control {
	struct device_attribute attr;
	int handle;
};
static struct touchpad_control *tp_ctl;

static ssize_t sony_nc_touchpad_store(struct device *dev,
		struct device_attribute *attr, const char *buffer, size_t count)
{
	unsigned int result;
	unsigned long value;

	if (count > 31)
		return -EINVAL;

	if (kstrtoul(buffer, 10, &value) || value > 1)
		return -EINVAL;

	/* sysfs: 0 disabled, 1 enabled
	 * EC: 0 enabled, 1 disabled
	 */
	if (sony_call_snc_handle(tp_ctl->handle,
				(!value << 0x10) | 0x100, &result))
		return -EIO;

	return count;
}

static ssize_t sony_nc_touchpad_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	unsigned int result;

	if (sony_call_snc_handle(tp_ctl->handle, 0x000, &result))
		return -EINVAL;

	return snprintf(buffer, PAGE_SIZE, "%d\n", !(result & 0x01));
}

static int sony_nc_touchpad_setup(struct platform_device *pd,
		unsigned int handle)
{
	int ret = 0;

	tp_ctl = kzalloc(sizeof(struct touchpad_control), GFP_KERNEL);
	if (!tp_ctl)
		return -ENOMEM;

	tp_ctl->handle = handle;

	sysfs_attr_init(&tp_ctl->attr.attr);
	tp_ctl->attr.attr.name = "touchpad";
	tp_ctl->attr.attr.mode = S_IRUGO | S_IWUSR;
	tp_ctl->attr.show = sony_nc_touchpad_show;
	tp_ctl->attr.store = sony_nc_touchpad_store;

	ret = device_create_file(&pd->dev, &tp_ctl->attr);
	if (ret) {
		kfree(tp_ctl);
		tp_ctl = NULL;
	}

	return ret;
}

static void sony_nc_touchpad_cleanup(struct platform_device *pd)
{
	if (tp_ctl) {
		device_remove_file(&pd->dev, &tp_ctl->attr);
		kfree(tp_ctl);
		tp_ctl = NULL;
	}
}

static void sony_nc_backlight_ng_read_limits(int handle,
		struct sony_backlight_props *props)
{
	u64 offset;
	int i;
	int lvl_table_len = 0;
	u8 min = 0xff, max = 0x00;
	unsigned char buffer[32] = { 0 };

	props->handle = handle;
	props->offset = 0;
	props->maxlvl = 0xff;

	offset = sony_find_snc_handle(handle);

	/* try to read the boundaries from ACPI tables, if we fail the above
	 * defaults should be reasonable
	 */
	i = sony_nc_buffer_call(sony_nc_acpi_handle, "SN06", &offset, buffer,
			32);
	if (i < 0)
		return;

	switch (handle) {
	case 0x012f:
	case 0x0137:
		lvl_table_len = 9;
		break;
	case 0x143:
	case 0x14b:
	case 0x14c:
		lvl_table_len = 16;
		break;
	}

	/* the buffer lists brightness levels available, brightness levels are
	 * from position 0 to 8 in the array, other values are used by ALS
	 * control.
	 */
	for (i = 0; i < lvl_table_len && i < ARRAY_SIZE(buffer); i++) {

		dprintk("Brightness level: %d\n", buffer[i]);

		if (!buffer[i])
			break;

		if (buffer[i] > max)
			max = buffer[i];
		if (buffer[i] < min)
			min = buffer[i];
	}
	props->offset = min;
	props->maxlvl = max;
	dprintk("Brightness levels: min=%d max=%d\n", props->offset,
			props->maxlvl);
}

static void sony_nc_backlight_setup(void)
{
	int max_brightness = 0;
	const struct backlight_ops *ops = NULL;
	struct backlight_properties props;

	if (sony_find_snc_handle(0x12f) >= 0) {
		ops = &sony_backlight_ng_ops;
		sony_bl_props.cmd_base = 0x0100;
		sony_nc_backlight_ng_read_limits(0x12f, &sony_bl_props);
		max_brightness = sony_bl_props.maxlvl - sony_bl_props.offset;

	} else if (sony_find_snc_handle(0x137) >= 0) {
		ops = &sony_backlight_ng_ops;
		sony_bl_props.cmd_base = 0x0100;
		sony_nc_backlight_ng_read_limits(0x137, &sony_bl_props);
		max_brightness = sony_bl_props.maxlvl - sony_bl_props.offset;

	} else if (sony_find_snc_handle(0x143) >= 0) {
		ops = &sony_backlight_ng_ops;
		sony_bl_props.cmd_base = 0x3000;
		sony_nc_backlight_ng_read_limits(0x143, &sony_bl_props);
		max_brightness = sony_bl_props.maxlvl - sony_bl_props.offset;

	} else if (sony_find_snc_handle(0x14b) >= 0) {
		ops = &sony_backlight_ng_ops;
		sony_bl_props.cmd_base = 0x3000;
		sony_nc_backlight_ng_read_limits(0x14b, &sony_bl_props);
		max_brightness = sony_bl_props.maxlvl - sony_bl_props.offset;

	} else if (sony_find_snc_handle(0x14c) >= 0) {
		ops = &sony_backlight_ng_ops;
		sony_bl_props.cmd_base = 0x3000;
		sony_nc_backlight_ng_read_limits(0x14c, &sony_bl_props);
		max_brightness = sony_bl_props.maxlvl - sony_bl_props.offset;

	} else if (acpi_has_method(sony_nc_acpi_handle, "GBRT")) {
		ops = &sony_backlight_ops;
		max_brightness = SONY_MAX_BRIGHTNESS - 1;

	} else
		return;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = max_brightness;
	sony_bl_props.dev = backlight_device_register("sony", NULL,
						      &sony_bl_props,
						      ops, &props);

	if (IS_ERR(sony_bl_props.dev)) {
		pr_warn("unable to register backlight device\n");
		sony_bl_props.dev = NULL;
	} else
		sony_bl_props.dev->props.brightness =
			ops->get_brightness(sony_bl_props.dev);
}

static void sony_nc_backlight_cleanup(void)
{
	if (sony_bl_props.dev)
		backlight_device_unregister(sony_bl_props.dev);
}

static int sony_nc_add(struct acpi_device *device)
{
	acpi_status status;
	int result = 0;
	struct sony_nc_value *item;

	sony_nc_acpi_device = device;
	strcpy(acpi_device_class(device), "sony/hotkey");

	sony_nc_acpi_handle = device->handle;

	/* read device status */
	result = acpi_bus_get_status(device);
	/* bail IFF the above call was successful and the device is not present */
	if (!result && !device->status.present) {
		dprintk("Device not present\n");
		result = -ENODEV;
		goto outwalk;
	}

	result = sony_pf_add();
	if (result)
		goto outpresent;

	if (debug) {
		status = acpi_walk_namespace(ACPI_TYPE_METHOD,
				sony_nc_acpi_handle, 1, sony_walk_callback,
				NULL, NULL, NULL);
		if (ACPI_FAILURE(status)) {
			pr_warn("unable to walk acpi resources\n");
			result = -ENODEV;
			goto outpresent;
		}
	}

	result = sony_laptop_setup_input(device);
	if (result) {
		pr_err("Unable to create input devices\n");
		goto outplatform;
	}

	if (acpi_has_method(sony_nc_acpi_handle, "ECON")) {
		int arg = 1;
		if (sony_nc_int_call(sony_nc_acpi_handle, "ECON", &arg, NULL))
			dprintk("ECON Method failed\n");
	}

	if (acpi_has_method(sony_nc_acpi_handle, "SN00")) {
		dprintk("Doing SNC setup\n");
		/* retrieve the available handles */
		result = sony_nc_handles_setup(sony_pf_device);
		if (!result)
			sony_nc_function_setup(device, sony_pf_device);
	}

	/* setup input devices and helper fifo */
	if (acpi_video_backlight_support()) {
		pr_info("brightness ignored, must be controlled by ACPI video driver\n");
	} else {
		sony_nc_backlight_setup();
	}

	/* create sony_pf sysfs attributes related to the SNC device */
	for (item = sony_nc_values; item->name; ++item) {

		if (!debug && item->debug)
			continue;

		/* find the available acpiget as described in the DSDT */
		for (; item->acpiget && *item->acpiget; ++item->acpiget) {
			if (acpi_has_method(sony_nc_acpi_handle,
							*item->acpiget)) {
				dprintk("Found %s getter: %s\n",
						item->name, *item->acpiget);
				item->devattr.attr.mode |= S_IRUGO;
				break;
			}
		}

		/* find the available acpiset as described in the DSDT */
		for (; item->acpiset && *item->acpiset; ++item->acpiset) {
			if (acpi_has_method(sony_nc_acpi_handle,
							*item->acpiset)) {
				dprintk("Found %s setter: %s\n",
						item->name, *item->acpiset);
				item->devattr.attr.mode |= S_IWUSR;
				break;
			}
		}

		if (item->devattr.attr.mode != 0) {
			result =
			    device_create_file(&sony_pf_device->dev,
					       &item->devattr);
			if (result)
				goto out_sysfs;
		}
	}

	pr_info("SNC setup done.\n");
	return 0;

out_sysfs:
	for (item = sony_nc_values; item->name; ++item) {
		device_remove_file(&sony_pf_device->dev, &item->devattr);
	}
	sony_nc_backlight_cleanup();
	sony_nc_function_cleanup(sony_pf_device);
	sony_nc_handles_cleanup(sony_pf_device);

outplatform:
	sony_laptop_remove_input();

outpresent:
	sony_pf_remove();

outwalk:
	sony_nc_rfkill_cleanup();
	return result;
}

static int sony_nc_remove(struct acpi_device *device)
{
	struct sony_nc_value *item;

	sony_nc_backlight_cleanup();

	sony_nc_acpi_device = NULL;

	for (item = sony_nc_values; item->name; ++item) {
		device_remove_file(&sony_pf_device->dev, &item->devattr);
	}

	sony_nc_function_cleanup(sony_pf_device);
	sony_nc_handles_cleanup(sony_pf_device);
	sony_pf_remove();
	sony_laptop_remove_input();
	dprintk(SONY_NC_DRIVER_NAME " removed.\n");

	return 0;
}

static const struct acpi_device_id sony_device_ids[] = {
	{SONY_NC_HID, 0},
	{SONY_PIC_HID, 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, sony_device_ids);

static const struct acpi_device_id sony_nc_device_ids[] = {
	{SONY_NC_HID, 0},
	{"", 0},
};

static struct acpi_driver sony_nc_driver = {
	.name = SONY_NC_DRIVER_NAME,
	.class = SONY_NC_CLASS,
	.ids = sony_nc_device_ids,
	.owner = THIS_MODULE,
	.ops = {
		.add = sony_nc_add,
		.remove = sony_nc_remove,
		.notify = sony_nc_notify,
		},
	.drv.pm = &sony_nc_pm,
};

/*********** SPIC (SNY6001) Device ***********/

#define SONYPI_DEVICE_TYPE1	0x00000001
#define SONYPI_DEVICE_TYPE2	0x00000002
#define SONYPI_DEVICE_TYPE3	0x00000004

#define SONYPI_TYPE1_OFFSET	0x04
#define SONYPI_TYPE2_OFFSET	0x12
#define SONYPI_TYPE3_OFFSET	0x12

struct sony_pic_ioport {
	struct acpi_resource_io	io1;
	struct acpi_resource_io	io2;
	struct list_head	list;
};

struct sony_pic_irq {
	struct acpi_resource_irq	irq;
	struct list_head		list;
};

struct sonypi_eventtypes {
	u8			data;
	unsigned long		mask;
	struct sonypi_event	*events;
};

struct sony_pic_dev {
	struct acpi_device		*acpi_dev;
	struct sony_pic_irq		*cur_irq;
	struct sony_pic_ioport		*cur_ioport;
	struct list_head		interrupts;
	struct list_head		ioports;
	struct mutex			lock;
	struct sonypi_eventtypes	*event_types;
	int                             (*handle_irq)(const u8, const u8);
	int				model;
	u16				evport_offset;
	u8				camera_power;
	u8				bluetooth_power;
	u8				wwan_power;
};

static struct sony_pic_dev spic_dev = {
	.interrupts	= LIST_HEAD_INIT(spic_dev.interrupts),
	.ioports	= LIST_HEAD_INIT(spic_dev.ioports),
};

static int spic_drv_registered;

/* Event masks */
#define SONYPI_JOGGER_MASK			0x00000001
#define SONYPI_CAPTURE_MASK			0x00000002
#define SONYPI_FNKEY_MASK			0x00000004
#define SONYPI_BLUETOOTH_MASK			0x00000008
#define SONYPI_PKEY_MASK			0x00000010
#define SONYPI_BACK_MASK			0x00000020
#define SONYPI_HELP_MASK			0x00000040
#define SONYPI_LID_MASK				0x00000080
#define SONYPI_ZOOM_MASK			0x00000100
#define SONYPI_THUMBPHRASE_MASK			0x00000200
#define SONYPI_MEYE_MASK			0x00000400
#define SONYPI_MEMORYSTICK_MASK			0x00000800
#define SONYPI_BATTERY_MASK			0x00001000
#define SONYPI_WIRELESS_MASK			0x00002000

struct sonypi_event {
	u8	data;
	u8	event;
};

/* The set of possible button release events */
static struct sonypi_event sonypi_releaseev[] = {
	{ 0x00, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0, 0 }
};

/* The set of possible jogger events  */
static struct sonypi_event sonypi_joggerev[] = {
	{ 0x1f, SONYPI_EVENT_JOGDIAL_UP },
	{ 0x01, SONYPI_EVENT_JOGDIAL_DOWN },
	{ 0x5f, SONYPI_EVENT_JOGDIAL_UP_PRESSED },
	{ 0x41, SONYPI_EVENT_JOGDIAL_DOWN_PRESSED },
	{ 0x1e, SONYPI_EVENT_JOGDIAL_FAST_UP },
	{ 0x02, SONYPI_EVENT_JOGDIAL_FAST_DOWN },
	{ 0x5e, SONYPI_EVENT_JOGDIAL_FAST_UP_PRESSED },
	{ 0x42, SONYPI_EVENT_JOGDIAL_FAST_DOWN_PRESSED },
	{ 0x1d, SONYPI_EVENT_JOGDIAL_VFAST_UP },
	{ 0x03, SONYPI_EVENT_JOGDIAL_VFAST_DOWN },
	{ 0x5d, SONYPI_EVENT_JOGDIAL_VFAST_UP_PRESSED },
	{ 0x43, SONYPI_EVENT_JOGDIAL_VFAST_DOWN_PRESSED },
	{ 0x40, SONYPI_EVENT_JOGDIAL_PRESSED },
	{ 0, 0 }
};

/* The set of possible capture button events */
static struct sonypi_event sonypi_captureev[] = {
	{ 0x05, SONYPI_EVENT_CAPTURE_PARTIALPRESSED },
	{ 0x07, SONYPI_EVENT_CAPTURE_PRESSED },
	{ 0x40, SONYPI_EVENT_CAPTURE_PRESSED },
	{ 0x01, SONYPI_EVENT_CAPTURE_PARTIALRELEASED },
	{ 0, 0 }
};

/* The set of possible fnkeys events */
static struct sonypi_event sonypi_fnkeyev[] = {
	{ 0x10, SONYPI_EVENT_FNKEY_ESC },
	{ 0x11, SONYPI_EVENT_FNKEY_F1 },
	{ 0x12, SONYPI_EVENT_FNKEY_F2 },
	{ 0x13, SONYPI_EVENT_FNKEY_F3 },
	{ 0x14, SONYPI_EVENT_FNKEY_F4 },
	{ 0x15, SONYPI_EVENT_FNKEY_F5 },
	{ 0x16, SONYPI_EVENT_FNKEY_F6 },
	{ 0x17, SONYPI_EVENT_FNKEY_F7 },
	{ 0x18, SONYPI_EVENT_FNKEY_F8 },
	{ 0x19, SONYPI_EVENT_FNKEY_F9 },
	{ 0x1a, SONYPI_EVENT_FNKEY_F10 },
	{ 0x1b, SONYPI_EVENT_FNKEY_F11 },
	{ 0x1c, SONYPI_EVENT_FNKEY_F12 },
	{ 0x1f, SONYPI_EVENT_FNKEY_RELEASED },
	{ 0x21, SONYPI_EVENT_FNKEY_1 },
	{ 0x22, SONYPI_EVENT_FNKEY_2 },
	{ 0x31, SONYPI_EVENT_FNKEY_D },
	{ 0x32, SONYPI_EVENT_FNKEY_E },
	{ 0x33, SONYPI_EVENT_FNKEY_F },
	{ 0x34, SONYPI_EVENT_FNKEY_S },
	{ 0x35, SONYPI_EVENT_FNKEY_B },
	{ 0x36, SONYPI_EVENT_FNKEY_ONLY },
	{ 0, 0 }
};

/* The set of possible program key events */
static struct sonypi_event sonypi_pkeyev[] = {
	{ 0x01, SONYPI_EVENT_PKEY_P1 },
	{ 0x02, SONYPI_EVENT_PKEY_P2 },
	{ 0x04, SONYPI_EVENT_PKEY_P3 },
	{ 0x20, SONYPI_EVENT_PKEY_P1 },
	{ 0, 0 }
};

/* The set of possible bluetooth events */
static struct sonypi_event sonypi_blueev[] = {
	{ 0x55, SONYPI_EVENT_BLUETOOTH_PRESSED },
	{ 0x59, SONYPI_EVENT_BLUETOOTH_ON },
	{ 0x5a, SONYPI_EVENT_BLUETOOTH_OFF },
	{ 0, 0 }
};

/* The set of possible wireless events */
static struct sonypi_event sonypi_wlessev[] = {
	{ 0x59, SONYPI_EVENT_IGNORE },
	{ 0x5a, SONYPI_EVENT_IGNORE },
	{ 0, 0 }
};

/* The set of possible back button events */
static struct sonypi_event sonypi_backev[] = {
	{ 0x20, SONYPI_EVENT_BACK_PRESSED },
	{ 0, 0 }
};

/* The set of possible help button events */
static struct sonypi_event sonypi_helpev[] = {
	{ 0x3b, SONYPI_EVENT_HELP_PRESSED },
	{ 0, 0 }
};


/* The set of possible lid events */
static struct sonypi_event sonypi_lidev[] = {
	{ 0x51, SONYPI_EVENT_LID_CLOSED },
	{ 0x50, SONYPI_EVENT_LID_OPENED },
	{ 0, 0 }
};

/* The set of possible zoom events */
static struct sonypi_event sonypi_zoomev[] = {
	{ 0x39, SONYPI_EVENT_ZOOM_PRESSED },
	{ 0x10, SONYPI_EVENT_ZOOM_IN_PRESSED },
	{ 0x20, SONYPI_EVENT_ZOOM_OUT_PRESSED },
	{ 0x04, SONYPI_EVENT_ZOOM_PRESSED },
	{ 0, 0 }
};

/* The set of possible thumbphrase events */
static struct sonypi_event sonypi_thumbphraseev[] = {
	{ 0x3a, SONYPI_EVENT_THUMBPHRASE_PRESSED },
	{ 0, 0 }
};

/* The set of possible motioneye camera events */
static struct sonypi_event sonypi_meyeev[] = {
	{ 0x00, SONYPI_EVENT_MEYE_FACE },
	{ 0x01, SONYPI_EVENT_MEYE_OPPOSITE },
	{ 0, 0 }
};

/* The set of possible memorystick events */
static struct sonypi_event sonypi_memorystickev[] = {
	{ 0x53, SONYPI_EVENT_MEMORYSTICK_INSERT },
	{ 0x54, SONYPI_EVENT_MEMORYSTICK_EJECT },
	{ 0, 0 }
};

/* The set of possible battery events */
static struct sonypi_event sonypi_batteryev[] = {
	{ 0x20, SONYPI_EVENT_BATTERY_INSERT },
	{ 0x30, SONYPI_EVENT_BATTERY_REMOVE },
	{ 0, 0 }
};

/* The set of possible volume events */
static struct sonypi_event sonypi_volumeev[] = {
	{ 0x01, SONYPI_EVENT_VOLUME_INC_PRESSED },
	{ 0x02, SONYPI_EVENT_VOLUME_DEC_PRESSED },
	{ 0, 0 }
};

/* The set of possible brightness events */
static struct sonypi_event sonypi_brightnessev[] = {
	{ 0x80, SONYPI_EVENT_BRIGHTNESS_PRESSED },
	{ 0, 0 }
};

static struct sonypi_eventtypes type1_events[] = {
	{ 0, 0xffffffff, sonypi_releaseev },
	{ 0x70, SONYPI_MEYE_MASK, sonypi_meyeev },
	{ 0x30, SONYPI_LID_MASK, sonypi_lidev },
	{ 0x60, SONYPI_CAPTURE_MASK, sonypi_captureev },
	{ 0x10, SONYPI_JOGGER_MASK, sonypi_joggerev },
	{ 0x20, SONYPI_FNKEY_MASK, sonypi_fnkeyev },
	{ 0x30, SONYPI_BLUETOOTH_MASK, sonypi_blueev },
	{ 0x40, SONYPI_PKEY_MASK, sonypi_pkeyev },
	{ 0x30, SONYPI_MEMORYSTICK_MASK, sonypi_memorystickev },
	{ 0x40, SONYPI_BATTERY_MASK, sonypi_batteryev },
	{ 0 },
};
static struct sonypi_eventtypes type2_events[] = {
	{ 0, 0xffffffff, sonypi_releaseev },
	{ 0x38, SONYPI_LID_MASK, sonypi_lidev },
	{ 0x11, SONYPI_JOGGER_MASK, sonypi_joggerev },
	{ 0x61, SONYPI_CAPTURE_MASK, sonypi_captureev },
	{ 0x21, SONYPI_FNKEY_MASK, sonypi_fnkeyev },
	{ 0x31, SONYPI_BLUETOOTH_MASK, sonypi_blueev },
	{ 0x08, SONYPI_PKEY_MASK, sonypi_pkeyev },
	{ 0x11, SONYPI_BACK_MASK, sonypi_backev },
	{ 0x21, SONYPI_HELP_MASK, sonypi_helpev },
	{ 0x21, SONYPI_ZOOM_MASK, sonypi_zoomev },
	{ 0x20, SONYPI_THUMBPHRASE_MASK, sonypi_thumbphraseev },
	{ 0x31, SONYPI_MEMORYSTICK_MASK, sonypi_memorystickev },
	{ 0x41, SONYPI_BATTERY_MASK, sonypi_batteryev },
	{ 0x31, SONYPI_PKEY_MASK, sonypi_pkeyev },
	{ 0 },
};
static struct sonypi_eventtypes type3_events[] = {
	{ 0, 0xffffffff, sonypi_releaseev },
	{ 0x21, SONYPI_FNKEY_MASK, sonypi_fnkeyev },
	{ 0x31, SONYPI_WIRELESS_MASK, sonypi_wlessev },
	{ 0x31, SONYPI_MEMORYSTICK_MASK, sonypi_memorystickev },
	{ 0x41, SONYPI_BATTERY_MASK, sonypi_batteryev },
	{ 0x31, SONYPI_PKEY_MASK, sonypi_pkeyev },
	{ 0x05, SONYPI_PKEY_MASK, sonypi_pkeyev },
	{ 0x05, SONYPI_ZOOM_MASK, sonypi_zoomev },
	{ 0x05, SONYPI_CAPTURE_MASK, sonypi_captureev },
	{ 0x05, SONYPI_PKEY_MASK, sonypi_volumeev },
	{ 0x05, SONYPI_PKEY_MASK, sonypi_brightnessev },
	{ 0 },
};

/* low level spic calls */
#define ITERATIONS_LONG		10000
#define ITERATIONS_SHORT	10
#define wait_on_command(command, iterations) {				\
	unsigned int n = iterations;					\
	while (--n && (command))					\
		udelay(1);						\
	if (!n)								\
		dprintk("command failed at %s : %s (line %d)\n",	\
				__FILE__, __func__, __LINE__);	\
}

static u8 sony_pic_call1(u8 dev)
{
	u8 v1, v2;

	wait_on_command(inb_p(spic_dev.cur_ioport->io1.minimum + 4) & 2,
			ITERATIONS_LONG);
	outb(dev, spic_dev.cur_ioport->io1.minimum + 4);
	v1 = inb_p(spic_dev.cur_ioport->io1.minimum + 4);
	v2 = inb_p(spic_dev.cur_ioport->io1.minimum);
	dprintk("sony_pic_call1(0x%.2x): 0x%.4x\n", dev, (v2 << 8) | v1);
	return v2;
}

static u8 sony_pic_call2(u8 dev, u8 fn)
{
	u8 v1;

	wait_on_command(inb_p(spic_dev.cur_ioport->io1.minimum + 4) & 2,
			ITERATIONS_LONG);
	outb(dev, spic_dev.cur_ioport->io1.minimum + 4);
	wait_on_command(inb_p(spic_dev.cur_ioport->io1.minimum + 4) & 2,
			ITERATIONS_LONG);
	outb(fn, spic_dev.cur_ioport->io1.minimum);
	v1 = inb_p(spic_dev.cur_ioport->io1.minimum);
	dprintk("sony_pic_call2(0x%.2x - 0x%.2x): 0x%.4x\n", dev, fn, v1);
	return v1;
}

static u8 sony_pic_call3(u8 dev, u8 fn, u8 v)
{
	u8 v1;

	wait_on_command(inb_p(spic_dev.cur_ioport->io1.minimum + 4) & 2, ITERATIONS_LONG);
	outb(dev, spic_dev.cur_ioport->io1.minimum + 4);
	wait_on_command(inb_p(spic_dev.cur_ioport->io1.minimum + 4) & 2, ITERATIONS_LONG);
	outb(fn, spic_dev.cur_ioport->io1.minimum);
	wait_on_command(inb_p(spic_dev.cur_ioport->io1.minimum + 4) & 2, ITERATIONS_LONG);
	outb(v, spic_dev.cur_ioport->io1.minimum);
	v1 = inb_p(spic_dev.cur_ioport->io1.minimum);
	dprintk("sony_pic_call3(0x%.2x - 0x%.2x - 0x%.2x): 0x%.4x\n",
			dev, fn, v, v1);
	return v1;
}

/*
 * minidrivers for SPIC models
 */
static int type3_handle_irq(const u8 data_mask, const u8 ev)
{
	/*
	 * 0x31 could mean we have to take some extra action and wait for
	 * the next irq for some Type3 models, it will generate a new
	 * irq and we can read new data from the device:
	 *  - 0x5c and 0x5f requires 0xA0
	 *  - 0x61 requires 0xB3
	 */
	if (data_mask == 0x31) {
		if (ev == 0x5c || ev == 0x5f)
			sony_pic_call1(0xA0);
		else if (ev == 0x61)
			sony_pic_call1(0xB3);
		return 0;
	}
	return 1;
}

static void sony_pic_detect_device_type(struct sony_pic_dev *dev)
{
	struct pci_dev *pcidev;

	pcidev = pci_get_device(PCI_VENDOR_ID_INTEL,
			PCI_DEVICE_ID_INTEL_82371AB_3, NULL);
	if (pcidev) {
		dev->model = SONYPI_DEVICE_TYPE1;
		dev->evport_offset = SONYPI_TYPE1_OFFSET;
		dev->event_types = type1_events;
		goto out;
	}

	pcidev = pci_get_device(PCI_VENDOR_ID_INTEL,
			PCI_DEVICE_ID_INTEL_ICH6_1, NULL);
	if (pcidev) {
		dev->model = SONYPI_DEVICE_TYPE2;
		dev->evport_offset = SONYPI_TYPE2_OFFSET;
		dev->event_types = type2_events;
		goto out;
	}

	pcidev = pci_get_device(PCI_VENDOR_ID_INTEL,
			PCI_DEVICE_ID_INTEL_ICH7_1, NULL);
	if (pcidev) {
		dev->model = SONYPI_DEVICE_TYPE3;
		dev->handle_irq = type3_handle_irq;
		dev->evport_offset = SONYPI_TYPE3_OFFSET;
		dev->event_types = type3_events;
		goto out;
	}

	pcidev = pci_get_device(PCI_VENDOR_ID_INTEL,
			PCI_DEVICE_ID_INTEL_ICH8_4, NULL);
	if (pcidev) {
		dev->model = SONYPI_DEVICE_TYPE3;
		dev->handle_irq = type3_handle_irq;
		dev->evport_offset = SONYPI_TYPE3_OFFSET;
		dev->event_types = type3_events;
		goto out;
	}

	pcidev = pci_get_device(PCI_VENDOR_ID_INTEL,
			PCI_DEVICE_ID_INTEL_ICH9_1, NULL);
	if (pcidev) {
		dev->model = SONYPI_DEVICE_TYPE3;
		dev->handle_irq = type3_handle_irq;
		dev->evport_offset = SONYPI_TYPE3_OFFSET;
		dev->event_types = type3_events;
		goto out;
	}

	/* default */
	dev->model = SONYPI_DEVICE_TYPE2;
	dev->evport_offset = SONYPI_TYPE2_OFFSET;
	dev->event_types = type2_events;

out:
	if (pcidev)
		pci_dev_put(pcidev);

	pr_info("detected Type%d model\n",
		dev->model == SONYPI_DEVICE_TYPE1 ? 1 :
		dev->model == SONYPI_DEVICE_TYPE2 ? 2 : 3);
}

/* camera tests and poweron/poweroff */
#define SONYPI_CAMERA_PICTURE		5
#define SONYPI_CAMERA_CONTROL		0x10

#define SONYPI_CAMERA_BRIGHTNESS		0
#define SONYPI_CAMERA_CONTRAST			1
#define SONYPI_CAMERA_HUE			2
#define SONYPI_CAMERA_COLOR			3
#define SONYPI_CAMERA_SHARPNESS			4

#define SONYPI_CAMERA_EXPOSURE_MASK		0xC
#define SONYPI_CAMERA_WHITE_BALANCE_MASK	0x3
#define SONYPI_CAMERA_PICTURE_MODE_MASK		0x30
#define SONYPI_CAMERA_MUTE_MASK			0x40

/* the rest don't need a loop until not 0xff */
#define SONYPI_CAMERA_AGC			6
#define SONYPI_CAMERA_AGC_MASK			0x30
#define SONYPI_CAMERA_SHUTTER_MASK 		0x7

#define SONYPI_CAMERA_SHUTDOWN_REQUEST		7
#define SONYPI_CAMERA_CONTROL			0x10

#define SONYPI_CAMERA_STATUS 			7
#define SONYPI_CAMERA_STATUS_READY 		0x2
#define SONYPI_CAMERA_STATUS_POSITION		0x4

#define SONYPI_DIRECTION_BACKWARDS 		0x4

#define SONYPI_CAMERA_REVISION 			8
#define SONYPI_CAMERA_ROMVERSION 		9

static int __sony_pic_camera_ready(void)
{
	u8 v;

	v = sony_pic_call2(0x8f, SONYPI_CAMERA_STATUS);
	return (v != 0xff && (v & SONYPI_CAMERA_STATUS_READY));
}

static int __sony_pic_camera_off(void)
{
	if (!camera) {
		pr_warn("camera control not enabled\n");
		return -ENODEV;
	}

	wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_PICTURE,
				SONYPI_CAMERA_MUTE_MASK),
			ITERATIONS_SHORT);

	if (spic_dev.camera_power) {
		sony_pic_call2(0x91, 0);
		spic_dev.camera_power = 0;
	}
	return 0;
}

static int __sony_pic_camera_on(void)
{
	int i, j, x;

	if (!camera) {
		pr_warn("camera control not enabled\n");
		return -ENODEV;
	}

	if (spic_dev.camera_power)
		return 0;

	for (j = 5; j > 0; j--) {

		for (x = 0; x < 100 && sony_pic_call2(0x91, 0x1); x++)
			msleep(10);
		sony_pic_call1(0x93);

		for (i = 400; i > 0; i--) {
			if (__sony_pic_camera_ready())
				break;
			msleep(10);
		}
		if (i)
			break;
	}

	if (j == 0) {
		pr_warn("failed to power on camera\n");
		return -ENODEV;
	}

	wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_CONTROL,
				0x5a),
			ITERATIONS_SHORT);

	spic_dev.camera_power = 1;
	return 0;
}

/* External camera command (exported to the motion eye v4l driver) */
int sony_pic_camera_command(int command, u8 value)
{
	if (!camera)
		return -EIO;

	mutex_lock(&spic_dev.lock);

	switch (command) {
	case SONY_PIC_COMMAND_SETCAMERA:
		if (value)
			__sony_pic_camera_on();
		else
			__sony_pic_camera_off();
		break;
	case SONY_PIC_COMMAND_SETCAMERABRIGHTNESS:
		wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_BRIGHTNESS, value),
				ITERATIONS_SHORT);
		break;
	case SONY_PIC_COMMAND_SETCAMERACONTRAST:
		wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_CONTRAST, value),
				ITERATIONS_SHORT);
		break;
	case SONY_PIC_COMMAND_SETCAMERAHUE:
		wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_HUE, value),
				ITERATIONS_SHORT);
		break;
	case SONY_PIC_COMMAND_SETCAMERACOLOR:
		wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_COLOR, value),
				ITERATIONS_SHORT);
		break;
	case SONY_PIC_COMMAND_SETCAMERASHARPNESS:
		wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_SHARPNESS, value),
				ITERATIONS_SHORT);
		break;
	case SONY_PIC_COMMAND_SETCAMERAPICTURE:
		wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_PICTURE, value),
				ITERATIONS_SHORT);
		break;
	case SONY_PIC_COMMAND_SETCAMERAAGC:
		wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_AGC, value),
				ITERATIONS_SHORT);
		break;
	default:
		pr_err("sony_pic_camera_command invalid: %d\n", command);
		break;
	}
	mutex_unlock(&spic_dev.lock);
	return 0;
}
EXPORT_SYMBOL(sony_pic_camera_command);

/* gprs/edge modem (SZ460N and SZ210P), thanks to Joshua Wise */
static void __sony_pic_set_wwanpower(u8 state)
{
	state = !!state;
	if (spic_dev.wwan_power == state)
		return;
	sony_pic_call2(0xB0, state);
	sony_pic_call1(0x82);
	spic_dev.wwan_power = state;
}

static ssize_t sony_pic_wwanpower_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t count)
{
	unsigned long value;
	if (count > 31)
		return -EINVAL;

	if (kstrtoul(buffer, 10, &value))
		return -EINVAL;

	mutex_lock(&spic_dev.lock);
	__sony_pic_set_wwanpower(value);
	mutex_unlock(&spic_dev.lock);

	return count;
}

static ssize_t sony_pic_wwanpower_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	ssize_t count;
	mutex_lock(&spic_dev.lock);
	count = snprintf(buffer, PAGE_SIZE, "%d\n", spic_dev.wwan_power);
	mutex_unlock(&spic_dev.lock);
	return count;
}

/* bluetooth subsystem power state */
static void __sony_pic_set_bluetoothpower(u8 state)
{
	state = !!state;
	if (spic_dev.bluetooth_power == state)
		return;
	sony_pic_call2(0x96, state);
	sony_pic_call1(0x82);
	spic_dev.bluetooth_power = state;
}

static ssize_t sony_pic_bluetoothpower_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t count)
{
	unsigned long value;
	if (count > 31)
		return -EINVAL;

	if (kstrtoul(buffer, 10, &value))
		return -EINVAL;

	mutex_lock(&spic_dev.lock);
	__sony_pic_set_bluetoothpower(value);
	mutex_unlock(&spic_dev.lock);

	return count;
}

static ssize_t sony_pic_bluetoothpower_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	ssize_t count = 0;
	mutex_lock(&spic_dev.lock);
	count = snprintf(buffer, PAGE_SIZE, "%d\n", spic_dev.bluetooth_power);
	mutex_unlock(&spic_dev.lock);
	return count;
}

/* fan speed */
/* FAN0 information (reverse engineered from ACPI tables) */
#define SONY_PIC_FAN0_STATUS	0x93
static int sony_pic_set_fanspeed(unsigned long value)
{
	return ec_write(SONY_PIC_FAN0_STATUS, value);
}

static int sony_pic_get_fanspeed(u8 *value)
{
	return ec_read(SONY_PIC_FAN0_STATUS, value);
}

static ssize_t sony_pic_fanspeed_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t count)
{
	unsigned long value;
	if (count > 31)
		return -EINVAL;

	if (kstrtoul(buffer, 10, &value))
		return -EINVAL;

	if (sony_pic_set_fanspeed(value))
		return -EIO;

	return count;
}

static ssize_t sony_pic_fanspeed_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	u8 value = 0;
	if (sony_pic_get_fanspeed(&value))
		return -EIO;

	return snprintf(buffer, PAGE_SIZE, "%d\n", value);
}

#define SPIC_ATTR(_name, _mode)					\
struct device_attribute spic_attr_##_name = __ATTR(_name,	\
		_mode, sony_pic_## _name ##_show,		\
		sony_pic_## _name ##_store)

static SPIC_ATTR(bluetoothpower, 0644);
static SPIC_ATTR(wwanpower, 0644);
static SPIC_ATTR(fanspeed, 0644);

static struct attribute *spic_attributes[] = {
	&spic_attr_bluetoothpower.attr,
	&spic_attr_wwanpower.attr,
	&spic_attr_fanspeed.attr,
	NULL
};

static struct attribute_group spic_attribute_group = {
	.attrs = spic_attributes
};

/******** SONYPI compatibility **********/
#ifdef CONFIG_SONYPI_COMPAT

/* battery / brightness / temperature  addresses */
#define SONYPI_BAT_FLAGS	0x81
#define SONYPI_LCD_LIGHT	0x96
#define SONYPI_BAT1_PCTRM	0xa0
#define SONYPI_BAT1_LEFT	0xa2
#define SONYPI_BAT1_MAXRT	0xa4
#define SONYPI_BAT2_PCTRM	0xa8
#define SONYPI_BAT2_LEFT	0xaa
#define SONYPI_BAT2_MAXRT	0xac
#define SONYPI_BAT1_MAXTK	0xb0
#define SONYPI_BAT1_FULL	0xb2
#define SONYPI_BAT2_MAXTK	0xb8
#define SONYPI_BAT2_FULL	0xba
#define SONYPI_TEMP_STATUS	0xC1

struct sonypi_compat_s {
	struct fasync_struct	*fifo_async;
	struct kfifo		fifo;
	spinlock_t		fifo_lock;
	wait_queue_head_t	fifo_proc_list;
	atomic_t		open_count;
};
static struct sonypi_compat_s sonypi_compat = {
	.open_count = ATOMIC_INIT(0),
};

static int sonypi_misc_fasync(int fd, struct file *filp, int on)
{
	return fasync_helper(fd, filp, on, &sonypi_compat.fifo_async);
}

static int sonypi_misc_release(struct inode *inode, struct file *file)
{
	atomic_dec(&sonypi_compat.open_count);
	return 0;
}

static int sonypi_misc_open(struct inode *inode, struct file *file)
{
	/* Flush input queue on first open */
	unsigned long flags;

	spin_lock_irqsave(&sonypi_compat.fifo_lock, flags);

	if (atomic_inc_return(&sonypi_compat.open_count) == 1)
		kfifo_reset(&sonypi_compat.fifo);

	spin_unlock_irqrestore(&sonypi_compat.fifo_lock, flags);

	return 0;
}

static ssize_t sonypi_misc_read(struct file *file, char __user *buf,
				size_t count, loff_t *pos)
{
	ssize_t ret;
	unsigned char c;

	if ((kfifo_len(&sonypi_compat.fifo) == 0) &&
	    (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	ret = wait_event_interruptible(sonypi_compat.fifo_proc_list,
				       kfifo_len(&sonypi_compat.fifo) != 0);
	if (ret)
		return ret;

	while (ret < count &&
	       (kfifo_out_locked(&sonypi_compat.fifo, &c, sizeof(c),
			  &sonypi_compat.fifo_lock) == sizeof(c))) {
		if (put_user(c, buf++))
			return -EFAULT;
		ret++;
	}

	if (ret > 0) {
		struct inode *inode = file_inode(file);
		inode->i_atime = current_fs_time(inode->i_sb);
	}

	return ret;
}

static unsigned int sonypi_misc_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &sonypi_compat.fifo_proc_list, wait);
	if (kfifo_len(&sonypi_compat.fifo))
		return POLLIN | POLLRDNORM;
	return 0;
}

static int ec_read16(u8 addr, u16 *value)
{
	u8 val_lb, val_hb;
	if (ec_read(addr, &val_lb))
		return -1;
	if (ec_read(addr + 1, &val_hb))
		return -1;
	*value = val_lb | (val_hb << 8);
	return 0;
}

static long sonypi_misc_ioctl(struct file *fp, unsigned int cmd,
							unsigned long arg)
{
	int ret = 0;
	void __user *argp = (void __user *)arg;
	u8 val8;
	u16 val16;
	int value;

	mutex_lock(&spic_dev.lock);
	switch (cmd) {
	case SONYPI_IOCGBRT:
		if (sony_bl_props.dev == NULL) {
			ret = -EIO;
			break;
		}
		if (sony_nc_int_call(sony_nc_acpi_handle, "GBRT", NULL,
					&value)) {
			ret = -EIO;
			break;
		}
		val8 = ((value & 0xff) - 1) << 5;
		if (copy_to_user(argp, &val8, sizeof(val8)))
				ret = -EFAULT;
		break;
	case SONYPI_IOCSBRT:
		if (sony_bl_props.dev == NULL) {
			ret = -EIO;
			break;
		}
		if (copy_from_user(&val8, argp, sizeof(val8))) {
			ret = -EFAULT;
			break;
		}
		value = (val8 >> 5) + 1;
		if (sony_nc_int_call(sony_nc_acpi_handle, "SBRT", &value,
					NULL)) {
			ret = -EIO;
			break;
		}
		/* sync the backlight device status */
		sony_bl_props.dev->props.brightness =
		    sony_backlight_get_brightness(sony_bl_props.dev);
		break;
	case SONYPI_IOCGBAT1CAP:
		if (ec_read16(SONYPI_BAT1_FULL, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBAT1REM:
		if (ec_read16(SONYPI_BAT1_LEFT, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBAT2CAP:
		if (ec_read16(SONYPI_BAT2_FULL, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBAT2REM:
		if (ec_read16(SONYPI_BAT2_LEFT, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBATFLAGS:
		if (ec_read(SONYPI_BAT_FLAGS, &val8)) {
			ret = -EIO;
			break;
		}
		val8 &= 0x07;
		if (copy_to_user(argp, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBLUE:
		val8 = spic_dev.bluetooth_power;
		if (copy_to_user(argp, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCSBLUE:
		if (copy_from_user(&val8, argp, sizeof(val8))) {
			ret = -EFAULT;
			break;
		}
		__sony_pic_set_bluetoothpower(val8);
		break;
	/* FAN Controls */
	case SONYPI_IOCGFAN:
		if (sony_pic_get_fanspeed(&val8)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCSFAN:
		if (copy_from_user(&val8, argp, sizeof(val8))) {
			ret = -EFAULT;
			break;
		}
		if (sony_pic_set_fanspeed(val8))
			ret = -EIO;
		break;
	/* GET Temperature (useful under APM) */
	case SONYPI_IOCGTEMP:
		if (ec_read(SONYPI_TEMP_STATUS, &val8)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&spic_dev.lock);
	return ret;
}

static const struct file_operations sonypi_misc_fops = {
	.owner		= THIS_MODULE,
	.read		= sonypi_misc_read,
	.poll		= sonypi_misc_poll,
	.open		= sonypi_misc_open,
	.release	= sonypi_misc_release,
	.fasync		= sonypi_misc_fasync,
	.unlocked_ioctl	= sonypi_misc_ioctl,
	.llseek		= noop_llseek,
};

static struct miscdevice sonypi_misc_device = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "sonypi",
	.fops		= &sonypi_misc_fops,
};

static void sonypi_compat_report_event(u8 event)
{
	kfifo_in_locked(&sonypi_compat.fifo, (unsigned char *)&event,
			sizeof(event), &sonypi_compat.fifo_lock);
	kill_fasync(&sonypi_compat.fifo_async, SIGIO, POLL_IN);
	wake_up_interruptible(&sonypi_compat.fifo_proc_list);
}

static int sonypi_compat_init(void)
{
	int error;

	spin_lock_init(&sonypi_compat.fifo_lock);
	error =
	 kfifo_alloc(&sonypi_compat.fifo, SONY_LAPTOP_BUF_SIZE, GFP_KERNEL);
	if (error) {
		pr_err("kfifo_alloc failed\n");
		return error;
	}

	init_waitqueue_head(&sonypi_compat.fifo_proc_list);

	if (minor != -1)
		sonypi_misc_device.minor = minor;
	error = misc_register(&sonypi_misc_device);
	if (error) {
		pr_err("misc_register failed\n");
		goto err_free_kfifo;
	}
	if (minor == -1)
		pr_info("device allocated minor is %d\n",
			sonypi_misc_device.minor);

	return 0;

err_free_kfifo:
	kfifo_free(&sonypi_compat.fifo);
	return error;
}

static void sonypi_compat_exit(void)
{
	misc_deregister(&sonypi_misc_device);
	kfifo_free(&sonypi_compat.fifo);
}
#else
static int sonypi_compat_init(void) { return 0; }
static void sonypi_compat_exit(void) { }
static void sonypi_compat_report_event(u8 event) { }
#endif /* CONFIG_SONYPI_COMPAT */

/*
 * ACPI callbacks
 */
static acpi_status
sony_pic_read_possible_resource(struct acpi_resource *resource, void *context)
{
	u32 i;
	struct sony_pic_dev *dev = (struct sony_pic_dev *)context;

	switch (resource->type) {
	case ACPI_RESOURCE_TYPE_START_DEPENDENT:
		{
			/* start IO enumeration */
			struct sony_pic_ioport *ioport = kzalloc(sizeof(*ioport), GFP_KERNEL);
			if (!ioport)
				return AE_ERROR;

			list_add(&ioport->list, &dev->ioports);
			return AE_OK;
		}

	case ACPI_RESOURCE_TYPE_END_DEPENDENT:
		/* end IO enumeration */
		return AE_OK;

	case ACPI_RESOURCE_TYPE_IRQ:
		{
			struct acpi_resource_irq *p = &resource->data.irq;
			struct sony_pic_irq *interrupt = NULL;
			if (!p || !p->interrupt_count) {
				/*
				 * IRQ descriptors may have no IRQ# bits set,
				 * particularly those those w/ _STA disabled
				 */
				dprintk("Blank IRQ resource\n");
				return AE_OK;
			}
			for (i = 0; i < p->interrupt_count; i++) {
				if (!p->interrupts[i]) {
					pr_warn("Invalid IRQ %d\n",
						p->interrupts[i]);
					continue;
				}
				interrupt = kzalloc(sizeof(*interrupt),
						GFP_KERNEL);
				if (!interrupt)
					return AE_ERROR;

				list_add(&interrupt->list, &dev->interrupts);
				interrupt->irq.triggering = p->triggering;
				interrupt->irq.polarity = p->polarity;
				interrupt->irq.sharable = p->sharable;
				interrupt->irq.interrupt_count = 1;
				interrupt->irq.interrupts[0] = p->interrupts[i];
			}
			return AE_OK;
		}
	case ACPI_RESOURCE_TYPE_IO:
		{
			struct acpi_resource_io *io = &resource->data.io;
			struct sony_pic_ioport *ioport =
				list_first_entry(&dev->ioports, struct sony_pic_ioport, list);
			if (!io) {
				dprintk("Blank IO resource\n");
				return AE_OK;
			}

			if (!ioport->io1.minimum) {
				memcpy(&ioport->io1, io, sizeof(*io));
				dprintk("IO1 at 0x%.4x (0x%.2x)\n", ioport->io1.minimum,
						ioport->io1.address_length);
			}
			else if (!ioport->io2.minimum) {
				memcpy(&ioport->io2, io, sizeof(*io));
				dprintk("IO2 at 0x%.4x (0x%.2x)\n", ioport->io2.minimum,
						ioport->io2.address_length);
			}
			else {
				pr_err("Unknown SPIC Type, more than 2 IO Ports\n");
				return AE_ERROR;
			}
			return AE_OK;
		}
	default:
		dprintk("Resource %d isn't an IRQ nor an IO port\n",
			resource->type);

	case ACPI_RESOURCE_TYPE_END_TAG:
		return AE_OK;
	}
	return AE_CTRL_TERMINATE;
}

static int sony_pic_possible_resources(struct acpi_device *device)
{
	int result = 0;
	acpi_status status = AE_OK;

	if (!device)
		return -EINVAL;

	/* get device status */
	/* see acpi_pci_link_get_current acpi_pci_link_get_possible */
	dprintk("Evaluating _STA\n");
	result = acpi_bus_get_status(device);
	if (result) {
		pr_warn("Unable to read status\n");
		goto end;
	}

	if (!device->status.enabled)
		dprintk("Device disabled\n");
	else
		dprintk("Device enabled\n");

	/*
	 * Query and parse 'method'
	 */
	dprintk("Evaluating %s\n", METHOD_NAME__PRS);
	status = acpi_walk_resources(device->handle, METHOD_NAME__PRS,
			sony_pic_read_possible_resource, &spic_dev);
	if (ACPI_FAILURE(status)) {
		pr_warn("Failure evaluating %s\n", METHOD_NAME__PRS);
		result = -ENODEV;
	}
end:
	return result;
}

/*
 *  Disable the spic device by calling its _DIS method
 */
static int sony_pic_disable(struct acpi_device *device)
{
	acpi_status ret = acpi_evaluate_object(device->handle, "_DIS", NULL,
					       NULL);

	if (ACPI_FAILURE(ret) && ret != AE_NOT_FOUND)
		return -ENXIO;

	dprintk("Device disabled\n");
	return 0;
}


/*
 *  Based on drivers/acpi/pci_link.c:acpi_pci_link_set
 *
 *  Call _SRS to set current resources
 */
static int sony_pic_enable(struct acpi_device *device,
		struct sony_pic_ioport *ioport, struct sony_pic_irq *irq)
{
	acpi_status status;
	int result = 0;
	/* Type 1 resource layout is:
	 *    IO
	 *    IO
	 *    IRQNoFlags
	 *    End
	 *
	 * Type 2 and 3 resource layout is:
	 *    IO
	 *    IRQNoFlags
	 *    End
	 */
	struct {
		struct acpi_resource res1;
		struct acpi_resource res2;
		struct acpi_resource res3;
		struct acpi_resource res4;
	} *resource;
	struct acpi_buffer buffer = { 0, NULL };

	if (!ioport || !irq)
		return -EINVAL;

	/* init acpi_buffer */
	resource = kzalloc(sizeof(*resource) + 1, GFP_KERNEL);
	if (!resource)
		return -ENOMEM;

	buffer.length = sizeof(*resource) + 1;
	buffer.pointer = resource;

	/* setup Type 1 resources */
	if (spic_dev.model == SONYPI_DEVICE_TYPE1) {

		/* setup io resources */
		resource->res1.type = ACPI_RESOURCE_TYPE_IO;
		resource->res1.length = sizeof(struct acpi_resource);
		memcpy(&resource->res1.data.io, &ioport->io1,
				sizeof(struct acpi_resource_io));

		resource->res2.type = ACPI_RESOURCE_TYPE_IO;
		resource->res2.length = sizeof(struct acpi_resource);
		memcpy(&resource->res2.data.io, &ioport->io2,
				sizeof(struct acpi_resource_io));

		/* setup irq resource */
		resource->res3.type = ACPI_RESOURCE_TYPE_IRQ;
		resource->res3.length = sizeof(struct acpi_resource);
		memcpy(&resource->res3.data.irq, &irq->irq,
				sizeof(struct acpi_resource_irq));
		/* we requested a shared irq */
		resource->res3.data.irq.sharable = ACPI_SHARED;

		resource->res4.type = ACPI_RESOURCE_TYPE_END_TAG;
		resource->res4.length = sizeof(struct acpi_resource);
	}
	/* setup Type 2/3 resources */
	else {
		/* setup io resource */
		resource->res1.type = ACPI_RESOURCE_TYPE_IO;
		resource->res1.length = sizeof(struct acpi_resource);
		memcpy(&resource->res1.data.io, &ioport->io1,
				sizeof(struct acpi_resource_io));

		/* setup irq resource */
		resource->res2.type = ACPI_RESOURCE_TYPE_IRQ;
		resource->res2.length = sizeof(struct acpi_resource);
		memcpy(&resource->res2.data.irq, &irq->irq,
				sizeof(struct acpi_resource_irq));
		/* we requested a shared irq */
		resource->res2.data.irq.sharable = ACPI_SHARED;

		resource->res3.type = ACPI_RESOURCE_TYPE_END_TAG;
		resource->res3.length = sizeof(struct acpi_resource);
	}

	/* Attempt to set the resource */
	dprintk("Evaluating _SRS\n");
	status = acpi_set_current_resources(device->handle, &buffer);

	/* check for total failure */
	if (ACPI_FAILURE(status)) {
		pr_err("Error evaluating _SRS\n");
		result = -ENODEV;
		goto end;
	}

	/* Necessary device initializations calls (from sonypi) */
	sony_pic_call1(0x82);
	sony_pic_call2(0x81, 0xff);
	sony_pic_call1(compat ? 0x92 : 0x82);

end:
	kfree(resource);
	return result;
}

/*****************
 *
 * ISR: some event is available
 *
 *****************/
static irqreturn_t sony_pic_irq(int irq, void *dev_id)
{
	int i, j;
	u8 ev = 0;
	u8 data_mask = 0;
	u8 device_event = 0;

	struct sony_pic_dev *dev = (struct sony_pic_dev *) dev_id;

	ev = inb_p(dev->cur_ioport->io1.minimum);
	if (dev->cur_ioport->io2.minimum)
		data_mask = inb_p(dev->cur_ioport->io2.minimum);
	else
		data_mask = inb_p(dev->cur_ioport->io1.minimum +
				dev->evport_offset);

	dprintk("event ([%.2x] [%.2x]) at port 0x%.4x(+0x%.2x)\n",
			ev, data_mask, dev->cur_ioport->io1.minimum,
			dev->evport_offset);

	if (ev == 0x00 || ev == 0xff)
		return IRQ_HANDLED;

	for (i = 0; dev->event_types[i].mask; i++) {

		if ((data_mask & dev->event_types[i].data) !=
		    dev->event_types[i].data)
			continue;

		if (!(mask & dev->event_types[i].mask))
			continue;

		for (j = 0; dev->event_types[i].events[j].event; j++) {
			if (ev == dev->event_types[i].events[j].data) {
				device_event =
					dev->event_types[i].events[j].event;
				/* some events may require ignoring */
				if (!device_event)
					return IRQ_HANDLED;
				goto found;
			}
		}
	}
	/* Still not able to decode the event try to pass
	 * it over to the minidriver
	 */
	if (dev->handle_irq && dev->handle_irq(data_mask, ev) == 0)
		return IRQ_HANDLED;

	dprintk("unknown event ([%.2x] [%.2x]) at port 0x%.4x(+0x%.2x)\n",
			ev, data_mask, dev->cur_ioport->io1.minimum,
			dev->evport_offset);
	return IRQ_HANDLED;

found:
	sony_laptop_report_input_event(device_event);
	sonypi_compat_report_event(device_event);
	return IRQ_HANDLED;
}

/*****************
 *
 *  ACPI driver
 *
 *****************/
static int sony_pic_remove(struct acpi_device *device)
{
	struct sony_pic_ioport *io, *tmp_io;
	struct sony_pic_irq *irq, *tmp_irq;

	if (sony_pic_disable(device)) {
		pr_err("Couldn't disable device\n");
		return -ENXIO;
	}

	free_irq(spic_dev.cur_irq->irq.interrupts[0], &spic_dev);
	release_region(spic_dev.cur_ioport->io1.minimum,
			spic_dev.cur_ioport->io1.address_length);
	if (spic_dev.cur_ioport->io2.minimum)
		release_region(spic_dev.cur_ioport->io2.minimum,
				spic_dev.cur_ioport->io2.address_length);

	sonypi_compat_exit();

	sony_laptop_remove_input();

	/* pf attrs */
	sysfs_remove_group(&sony_pf_device->dev.kobj, &spic_attribute_group);
	sony_pf_remove();

	list_for_each_entry_safe(io, tmp_io, &spic_dev.ioports, list) {
		list_del(&io->list);
		kfree(io);
	}
	list_for_each_entry_safe(irq, tmp_irq, &spic_dev.interrupts, list) {
		list_del(&irq->list);
		kfree(irq);
	}
	spic_dev.cur_ioport = NULL;
	spic_dev.cur_irq = NULL;

	dprintk(SONY_PIC_DRIVER_NAME " removed.\n");
	return 0;
}

static int sony_pic_add(struct acpi_device *device)
{
	int result;
	struct sony_pic_ioport *io, *tmp_io;
	struct sony_pic_irq *irq, *tmp_irq;

	spic_dev.acpi_dev = device;
	strcpy(acpi_device_class(device), "sony/hotkey");
	sony_pic_detect_device_type(&spic_dev);
	mutex_init(&spic_dev.lock);

	/* read _PRS resources */
	result = sony_pic_possible_resources(device);
	if (result) {
		pr_err("Unable to read possible resources\n");
		goto err_free_resources;
	}

	/* setup input devices and helper fifo */
	result = sony_laptop_setup_input(device);
	if (result) {
		pr_err("Unable to create input devices\n");
		goto err_free_resources;
	}

	result = sonypi_compat_init();
	if (result)
		goto err_remove_input;

	/* request io port */
	list_for_each_entry_reverse(io, &spic_dev.ioports, list) {
		if (request_region(io->io1.minimum, io->io1.address_length,
					"Sony Programmable I/O Device")) {
			dprintk("I/O port1: 0x%.4x (0x%.4x) + 0x%.2x\n",
					io->io1.minimum, io->io1.maximum,
					io->io1.address_length);
			/* Type 1 have 2 ioports */
			if (io->io2.minimum) {
				if (request_region(io->io2.minimum,
						io->io2.address_length,
						"Sony Programmable I/O Device")) {
					dprintk("I/O port2: 0x%.4x (0x%.4x) + 0x%.2x\n",
							io->io2.minimum, io->io2.maximum,
							io->io2.address_length);
					spic_dev.cur_ioport = io;
					break;
				}
				else {
					dprintk("Unable to get I/O port2: "
							"0x%.4x (0x%.4x) + 0x%.2x\n",
							io->io2.minimum, io->io2.maximum,
							io->io2.address_length);
					release_region(io->io1.minimum,
							io->io1.address_length);
				}
			}
			else {
				spic_dev.cur_ioport = io;
				break;
			}
		}
	}
	if (!spic_dev.cur_ioport) {
		pr_err("Failed to request_region\n");
		result = -ENODEV;
		goto err_remove_compat;
	}

	/* request IRQ */
	list_for_each_entry_reverse(irq, &spic_dev.interrupts, list) {
		if (!request_irq(irq->irq.interrupts[0], sony_pic_irq,
					0, "sony-laptop", &spic_dev)) {
			dprintk("IRQ: %d - triggering: %d - "
					"polarity: %d - shr: %d\n",
					irq->irq.interrupts[0],
					irq->irq.triggering,
					irq->irq.polarity,
					irq->irq.sharable);
			spic_dev.cur_irq = irq;
			break;
		}
	}
	if (!spic_dev.cur_irq) {
		pr_err("Failed to request_irq\n");
		result = -ENODEV;
		goto err_release_region;
	}

	/* set resource status _SRS */
	result = sony_pic_enable(device, spic_dev.cur_ioport, spic_dev.cur_irq);
	if (result) {
		pr_err("Couldn't enable device\n");
		goto err_free_irq;
	}

	spic_dev.bluetooth_power = -1;
	/* create device attributes */
	result = sony_pf_add();
	if (result)
		goto err_disable_device;

	result = sysfs_create_group(&sony_pf_device->dev.kobj, &spic_attribute_group);
	if (result)
		goto err_remove_pf;

	pr_info("SPIC setup done.\n");
	return 0;

err_remove_pf:
	sony_pf_remove();

err_disable_device:
	sony_pic_disable(device);

err_free_irq:
	free_irq(spic_dev.cur_irq->irq.interrupts[0], &spic_dev);

err_release_region:
	release_region(spic_dev.cur_ioport->io1.minimum,
			spic_dev.cur_ioport->io1.address_length);
	if (spic_dev.cur_ioport->io2.minimum)
		release_region(spic_dev.cur_ioport->io2.minimum,
				spic_dev.cur_ioport->io2.address_length);

err_remove_compat:
	sonypi_compat_exit();

err_remove_input:
	sony_laptop_remove_input();

err_free_resources:
	list_for_each_entry_safe(io, tmp_io, &spic_dev.ioports, list) {
		list_del(&io->list);
		kfree(io);
	}
	list_for_each_entry_safe(irq, tmp_irq, &spic_dev.interrupts, list) {
		list_del(&irq->list);
		kfree(irq);
	}
	spic_dev.cur_ioport = NULL;
	spic_dev.cur_irq = NULL;

	return result;
}

#ifdef CONFIG_PM_SLEEP
static int sony_pic_suspend(struct device *dev)
{
	if (sony_pic_disable(to_acpi_device(dev)))
		return -ENXIO;
	return 0;
}

static int sony_pic_resume(struct device *dev)
{
	sony_pic_enable(to_acpi_device(dev),
			spic_dev.cur_ioport, spic_dev.cur_irq);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(sony_pic_pm, sony_pic_suspend, sony_pic_resume);

static const struct acpi_device_id sony_pic_device_ids[] = {
	{SONY_PIC_HID, 0},
	{"", 0},
};

static struct acpi_driver sony_pic_driver = {
	.name = SONY_PIC_DRIVER_NAME,
	.class = SONY_PIC_CLASS,
	.ids = sony_pic_device_ids,
	.owner = THIS_MODULE,
	.ops = {
		.add = sony_pic_add,
		.remove = sony_pic_remove,
		},
	.drv.pm = &sony_pic_pm,
};

static struct dmi_system_id __initdata sonypi_dmi_table[] = {
	{
		.ident = "Sony Vaio",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PCG-"),
		},
	},
	{
		.ident = "Sony Vaio",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "VGN-"),
		},
	},
	{ }
};

static int __init sony_laptop_init(void)
{
	int result;

	if (!no_spic && dmi_check_system(sonypi_dmi_table)) {
		result = acpi_bus_register_driver(&sony_pic_driver);
		if (result) {
			pr_err("Unable to register SPIC driver\n");
			goto out;
		}
		spic_drv_registered = 1;
	}

	result = acpi_bus_register_driver(&sony_nc_driver);
	if (result) {
		pr_err("Unable to register SNC driver\n");
		goto out_unregister_pic;
	}

	return 0;

out_unregister_pic:
	if (spic_drv_registered)
		acpi_bus_unregister_driver(&sony_pic_driver);
out:
	return result;
}

static void __exit sony_laptop_exit(void)
{
	acpi_bus_unregister_driver(&sony_nc_driver);
	if (spic_drv_registered)
		acpi_bus_unregister_driver(&sony_pic_driver);
}

module_init(sony_laptop_init);
module_exit(sony_laptop_exit);
