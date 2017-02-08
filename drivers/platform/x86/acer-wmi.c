/*
 *  Acer WMI Laptop Extras
 *
 *  Copyright (C) 2007-2009	Carlos Corbacho <carlos@strangeworlds.co.uk>
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/dmi.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/i8042.h>
#include <linux/rfkill.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <acpi/video.h>

MODULE_AUTHOR("Carlos Corbacho");
MODULE_DESCRIPTION("Acer Laptop WMI Extras Driver");
MODULE_LICENSE("GPL");

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
#define AMW0_GUID2		"431F16ED-0C2B-444C-B267-27DEB140CF9C"
#define WMID_GUID1		"6AF4F258-B401-42FD-BE91-3D4AC2D7C0D3"
#define WMID_GUID2		"95764E09-FB56-4E83-B31A-37761F60994A"
#define WMID_GUID3		"61EF69EA-865C-4BC3-A502-A0DEBA0CB531"

/*
 * Acer ACPI event GUIDs
 */
#define ACERWMID_EVENT_GUID "676AA15E-6A47-4D9F-A2CC-1E6D18D14026"

MODULE_ALIAS("wmi:67C3371D-95A3-4C37-BB61-DD47B491DAAB");
MODULE_ALIAS("wmi:6AF4F258-B401-42FD-BE91-3D4AC2D7C0D3");
MODULE_ALIAS("wmi:676AA15E-6A47-4D9F-A2CC-1E6D18D14026");

enum acer_wmi_event_ids {
	WMID_HOTKEY_EVENT = 0x1,
	WMID_ACCEL_EVENT = 0x5,
};

static const struct key_entry acer_wmi_keymap[] __initconst = {
	{KE_KEY, 0x01, {KEY_WLAN} },     /* WiFi */
	{KE_KEY, 0x03, {KEY_WLAN} },     /* WiFi */
	{KE_KEY, 0x04, {KEY_WLAN} },     /* WiFi */
	{KE_KEY, 0x12, {KEY_BLUETOOTH} },	/* BT */
	{KE_KEY, 0x21, {KEY_PROG1} },    /* Backup */
	{KE_KEY, 0x22, {KEY_PROG2} },    /* Arcade */
	{KE_KEY, 0x23, {KEY_PROG3} },    /* P_Key */
	{KE_KEY, 0x24, {KEY_PROG4} },    /* Social networking_Key */
	{KE_KEY, 0x29, {KEY_PROG3} },    /* P_Key for TM8372 */
	{KE_IGNORE, 0x41, {KEY_MUTE} },
	{KE_IGNORE, 0x42, {KEY_PREVIOUSSONG} },
	{KE_IGNORE, 0x4d, {KEY_PREVIOUSSONG} },
	{KE_IGNORE, 0x43, {KEY_NEXTSONG} },
	{KE_IGNORE, 0x4e, {KEY_NEXTSONG} },
	{KE_IGNORE, 0x44, {KEY_PLAYPAUSE} },
	{KE_IGNORE, 0x4f, {KEY_PLAYPAUSE} },
	{KE_IGNORE, 0x45, {KEY_STOP} },
	{KE_IGNORE, 0x50, {KEY_STOP} },
	{KE_IGNORE, 0x48, {KEY_VOLUMEUP} },
	{KE_IGNORE, 0x49, {KEY_VOLUMEDOWN} },
	{KE_IGNORE, 0x4a, {KEY_VOLUMEDOWN} },
	{KE_IGNORE, 0x61, {KEY_SWITCHVIDEOMODE} },
	{KE_IGNORE, 0x62, {KEY_BRIGHTNESSUP} },
	{KE_IGNORE, 0x63, {KEY_BRIGHTNESSDOWN} },
	{KE_KEY, 0x64, {KEY_SWITCHVIDEOMODE} },	/* Display Switch */
	{KE_IGNORE, 0x81, {KEY_SLEEP} },
	{KE_KEY, 0x82, {KEY_TOUCHPAD_TOGGLE} },	/* Touch Pad Toggle */
	{KE_KEY, KEY_TOUCHPAD_ON, {KEY_TOUCHPAD_ON} },
	{KE_KEY, KEY_TOUCHPAD_OFF, {KEY_TOUCHPAD_OFF} },
	{KE_IGNORE, 0x83, {KEY_TOUCHPAD_TOGGLE} },
	{KE_KEY, 0x85, {KEY_TOUCHPAD_TOGGLE} },
	{KE_END, 0}
};

static struct input_dev *acer_wmi_input_dev;
static struct input_dev *acer_wmi_accel_dev;

struct event_return_value {
	u8 function;
	u8 key_num;
	u16 device_state;
	u32 reserved;
} __attribute__((packed));

/*
 * GUID3 Get Device Status device flags
 */
#define ACER_WMID3_GDS_WIRELESS		(1<<0)	/* WiFi */
#define ACER_WMID3_GDS_THREEG		(1<<6)	/* 3G */
#define ACER_WMID3_GDS_WIMAX		(1<<7)	/* WiMAX */
#define ACER_WMID3_GDS_BLUETOOTH	(1<<11)	/* BT */
#define ACER_WMID3_GDS_TOUCHPAD		(1<<1)	/* Touchpad */

/* Hotkey Customized Setting and Acer Application Status.
 * Set Device Default Value and Report Acer Application Status.
 * When Acer Application starts, it will run this method to inform
 * BIOS/EC that Acer Application is on.
 * App Status
 *	Bit[0]: Launch Manager Status
 *	Bit[1]: ePM Status
 *	Bit[2]: Device Control Status
 *	Bit[3]: Acer Power Button Utility Status
 *	Bit[4]: RF Button Status
 *	Bit[5]: ODD PM Status
 *	Bit[6]: Device Default Value Control
 *	Bit[7]: Hall Sensor Application Status
 */
struct func_input_params {
	u8 function_num;        /* Function Number */
	u16 commun_devices;     /* Communication type devices default status */
	u16 devices;            /* Other type devices default status */
	u8 app_status;          /* Acer Device Status. LM, ePM, RF Button... */
	u8 app_mask;		/* Bit mask to app_status */
	u8 reserved;
} __attribute__((packed));

struct func_return_value {
	u8 error_code;          /* Error Code */
	u8 ec_return_value;     /* EC Return Value */
	u16 reserved;
} __attribute__((packed));

struct wmid3_gds_set_input_param {     /* Set Device Status input parameter */
	u8 function_num;        /* Function Number */
	u8 hotkey_number;       /* Hotkey Number */
	u16 devices;            /* Set Device */
	u8 volume_value;        /* Volume Value */
} __attribute__((packed));

struct wmid3_gds_get_input_param {     /* Get Device Status input parameter */
	u8 function_num;	/* Function Number */
	u8 hotkey_number;	/* Hotkey Number */
	u16 devices;		/* Get Device */
} __attribute__((packed));

struct wmid3_gds_return_value {	/* Get Device Status return value*/
	u8 error_code;		/* Error Code */
	u8 ec_return_value;	/* EC Return Value */
	u16 devices;		/* Current Device Status */
	u32 reserved;
} __attribute__((packed));

struct hotkey_function_type_aa {
	u8 type;
	u8 length;
	u16 handle;
	u16 commun_func_bitmap;
	u16 application_func_bitmap;
	u16 media_func_bitmap;
	u16 display_func_bitmap;
	u16 others_func_bitmap;
	u8 commun_fn_key_number;
} __attribute__((packed));

/*
 * Interface capability flags
 */
#define ACER_CAP_MAILLED		(1<<0)
#define ACER_CAP_WIRELESS		(1<<1)
#define ACER_CAP_BLUETOOTH		(1<<2)
#define ACER_CAP_BRIGHTNESS		(1<<3)
#define ACER_CAP_THREEG			(1<<4)
#define ACER_CAP_ACCEL			(1<<5)
#define ACER_CAP_ANY			(0xFFFFFFFF)

/*
 * Interface type flags
 */
enum interface_flags {
	ACER_AMW0,
	ACER_AMW0_V2,
	ACER_WMID,
	ACER_WMID_v2,
};

#define ACER_DEFAULT_WIRELESS  0
#define ACER_DEFAULT_BLUETOOTH 0
#define ACER_DEFAULT_MAILLED   0
#define ACER_DEFAULT_THREEG    0

static int max_brightness = 0xF;

static int mailled = -1;
static int brightness = -1;
static int threeg = -1;
static int force_series;
static bool ec_raw_mode;
static bool has_type_aa;
static u16 commun_func_bitmap;
static u8 commun_fn_key_number;

module_param(mailled, int, 0444);
module_param(brightness, int, 0444);
module_param(threeg, int, 0444);
module_param(force_series, int, 0444);
module_param(ec_raw_mode, bool, 0444);
MODULE_PARM_DESC(mailled, "Set initial state of Mail LED");
MODULE_PARM_DESC(brightness, "Set initial LCD backlight brightness");
MODULE_PARM_DESC(threeg, "Set initial state of 3G hardware");
MODULE_PARM_DESC(force_series, "Force a different laptop series");
MODULE_PARM_DESC(ec_raw_mode, "Enable EC raw mode");

struct acer_data {
	int mailled;
	int threeg;
	int brightness;
};

struct acer_debug {
	struct dentry *root;
	struct dentry *devices;
	u32 wmid_devices;
};

static struct rfkill *wireless_rfkill;
static struct rfkill *bluetooth_rfkill;
static struct rfkill *threeg_rfkill;
static bool rfkill_inited;

/* Each low-level interface must define at least some of the following */
struct wmi_interface {
	/* The WMI device type */
	u32 type;

	/* The capabilities this interface provides */
	u32 capability;

	/* Private data for the current interface */
	struct acer_data data;

	/* debugfs entries associated with this interface */
	struct acer_debug debug;
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
	s8 brightness;
	u8 bluetooth;
};

static struct quirk_entry *quirks;

static void __init set_quirks(void)
{
	if (!interface)
		return;

	if (quirks->mailled)
		interface->capability |= ACER_CAP_MAILLED;

	if (quirks->brightness)
		interface->capability |= ACER_CAP_BRIGHTNESS;
}

static int __init dmi_matched(const struct dmi_system_id *dmi)
{
	quirks = dmi->driver_data;
	return 1;
}

static struct quirk_entry quirk_unknown = {
};

static struct quirk_entry quirk_acer_aspire_1520 = {
	.brightness = -1,
};

static struct quirk_entry quirk_acer_travelmate_2490 = {
	.mailled = 1,
};

/* This AMW0 laptop has no bluetooth */
static struct quirk_entry quirk_medion_md_98300 = {
	.wireless = 1,
};

static struct quirk_entry quirk_fujitsu_amilo_li_1718 = {
	.wireless = 2,
};

static struct quirk_entry quirk_lenovo_ideapad_s205 = {
	.wireless = 3,
};

/* The Aspire One has a dummy ACPI-WMI interface - disable it */
static const struct dmi_system_id acer_blacklist[] __initconst = {
	{
		.ident = "Acer Aspire One (SSD)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AOA110"),
		},
	},
	{
		.ident = "Acer Aspire One (HDD)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AOA150"),
		},
	},
	{}
};

static const struct dmi_system_id amw0_whitelist[] __initconst = {
	{
		.ident = "Acer",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
		},
	},
	{
		.ident = "Gateway",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Gateway"),
		},
	},
	{
		.ident = "Packard Bell",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Packard Bell"),
		},
	},
	{}
};

/*
 * This quirk table is only for Acer/Gateway/Packard Bell family
 * that those machines are supported by acer-wmi driver.
 */
static const struct dmi_system_id acer_quirks[] __initconst = {
	{
		.callback = dmi_matched,
		.ident = "Acer Aspire 1360",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 1360"),
		},
		.driver_data = &quirk_acer_aspire_1520,
	},
	{
		.callback = dmi_matched,
		.ident = "Acer Aspire 1520",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 1520"),
		},
		.driver_data = &quirk_acer_aspire_1520,
	},
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
		.ident = "Acer Aspire 3610",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 3610"),
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
		.ident = "Acer Aspire 5610",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5610"),
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
	{}
};

/*
 * This quirk list is for those non-acer machines that have AMW0_GUID1
 * but supported by acer-wmi in past days. Keeping this quirk list here
 * is only for backward compatible. Please do not add new machine to
 * here anymore. Those non-acer machines should be supported by
 * appropriate wmi drivers.
 */
static const struct dmi_system_id non_acer_quirks[] __initconst = {
	{
		.callback = dmi_matched,
		.ident = "Fujitsu Siemens Amilo Li 1718",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Li 1718"),
		},
		.driver_data = &quirk_fujitsu_amilo_li_1718,
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
	{
		.callback = dmi_matched,
		.ident = "Lenovo Ideapad S205",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "10382LG"),
		},
		.driver_data = &quirk_lenovo_ideapad_s205,
	},
	{
		.callback = dmi_matched,
		.ident = "Lenovo Ideapad S205 (Brazos)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Brazos"),
		},
		.driver_data = &quirk_lenovo_ideapad_s205,
	},
	{
		.callback = dmi_matched,
		.ident = "Lenovo 3000 N200",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "0687A31"),
		},
		.driver_data = &quirk_fujitsu_amilo_li_1718,
	},
	{
		.callback = dmi_matched,
		.ident = "Lenovo Ideapad S205-10382JG",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "10382JG"),
		},
		.driver_data = &quirk_lenovo_ideapad_s205,
	},
	{
		.callback = dmi_matched,
		.ident = "Lenovo Ideapad S205-1038DPG",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "1038DPG"),
		},
		.driver_data = &quirk_lenovo_ideapad_s205,
	},
	{}
};

static int __init
video_set_backlight_video_vendor(const struct dmi_system_id *d)
{
	interface->capability &= ~ACER_CAP_BRIGHTNESS;
	pr_info("Brightness must be controlled by generic video driver\n");
	return 0;
}

static const struct dmi_system_id video_vendor_dmi_table[] __initconst = {
	{
		.callback = video_set_backlight_video_vendor,
		.ident = "Acer TravelMate 4750",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 4750"),
		},
	},
	{
		.callback = video_set_backlight_video_vendor,
		.ident = "Acer Extensa 5235",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Extensa 5235"),
		},
	},
	{
		.callback = video_set_backlight_video_vendor,
		.ident = "Acer TravelMate 5760",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 5760"),
		},
	},
	{
		.callback = video_set_backlight_video_vendor,
		.ident = "Acer Aspire 5750",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5750"),
		},
	},
	{
		.callback = video_set_backlight_video_vendor,
		.ident = "Acer Aspire 5741",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5741"),
		},
	},
	{
		/*
		 * Note no video_set_backlight_video_vendor, we must use the
		 * acer interface, as there is no native backlight interface.
		 */
		.ident = "Acer KAV80",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "KAV80"),
		},
	},
	{}
};

/* Find which quirks are needed for a particular vendor/ model pair */
static void __init find_quirks(void)
{
	if (!force_series) {
		dmi_check_system(acer_quirks);
		dmi_check_system(non_acer_quirks);
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

static acpi_status AMW0_get_u32(u32 *value, u32 cap)
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
		case 2:
			err = ec_read(0x71, &result);
			if (err)
				return AE_ERROR;
			*value = result & 0x1;
			return AE_OK;
		case 3:
			err = ec_read(0x78, &result);
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
		return AE_ERROR;
	}
	return AE_OK;
}

static acpi_status AMW0_set_u32(u32 value, u32 cap)
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
		return AE_ERROR;
	}

	/* Actually do the set */
	return wmab_execute(&args, NULL);
}

static acpi_status __init AMW0_find_mailled(void)
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
		kfree(out.pointer);
		return AE_ERROR;
	}

	if (ret.eex & 0x1)
		interface->capability |= ACER_CAP_MAILLED;

	kfree(out.pointer);

	return AE_OK;
}

static const struct acpi_device_id norfkill_ids[] __initconst = {
	{ "VPC2004", 0},
	{ "IBM0068", 0},
	{ "LEN0068", 0},
	{ "SNY5001", 0},	/* sony-laptop in charge */
	{ "HPQ6601", 0},
	{ "", 0},
};

static int __init AMW0_set_cap_acpi_check_device(void)
{
	const struct acpi_device_id *id;

	for (id = norfkill_ids; id->id[0]; id++)
		if (acpi_dev_found(id->id))
			return true;

	return false;
}

static acpi_status __init AMW0_set_capabilities(void)
{
	struct wmab_args args;
	struct wmab_ret ret;
	acpi_status status;
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;

	/*
	 * On laptops with this strange GUID (non Acer), normal probing doesn't
	 * work.
	 */
	if (wmi_has_guid(AMW0_GUID2)) {
		if ((quirks != &quirk_unknown) ||
		    !AMW0_set_cap_acpi_check_device())
			interface->capability |= ACER_CAP_WIRELESS;
		return AE_OK;
	}

	args.eax = ACER_AMW0_WRITE;
	args.ecx = args.edx = 0;

	args.ebx = 0xa2 << 8;
	args.ebx |= ACER_AMW0_WIRELESS_MASK;

	status = wmab_execute(&args, &out);
	if (ACPI_FAILURE(status))
		return status;

	obj = out.pointer;
	if (obj && obj->type == ACPI_TYPE_BUFFER &&
	obj->buffer.length == sizeof(struct wmab_ret)) {
		ret = *((struct wmab_ret *) obj->buffer.pointer);
	} else {
		status = AE_ERROR;
		goto out;
	}

	if (ret.eax & 0x1)
		interface->capability |= ACER_CAP_WIRELESS;

	args.ebx = 2 << 8;
	args.ebx |= ACER_AMW0_BLUETOOTH_MASK;

	/*
	 * It's ok to use existing buffer for next wmab_execute call.
	 * But we need to kfree(out.pointer) if next wmab_execute fail.
	 */
	status = wmab_execute(&args, &out);
	if (ACPI_FAILURE(status))
		goto out;

	obj = (union acpi_object *) out.pointer;
	if (obj && obj->type == ACPI_TYPE_BUFFER
	&& obj->buffer.length == sizeof(struct wmab_ret)) {
		ret = *((struct wmab_ret *) obj->buffer.pointer);
	} else {
		status = AE_ERROR;
		goto out;
	}

	if (ret.eax & 0x1)
		interface->capability |= ACER_CAP_BLUETOOTH;

	/*
	 * This appears to be safe to enable, since all Wistron based laptops
	 * appear to use the same EC register for brightness, even if they
	 * differ for wireless, etc
	 */
	if (quirks->brightness >= 0)
		interface->capability |= ACER_CAP_BRIGHTNESS;

	status = AE_OK;
out:
	kfree(out.pointer);
	return status;
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
	u32 tmp = 0;
	acpi_status status;

	status = wmi_evaluate_method(WMID_GUID1, 1, method_id, &input, &result);

	if (ACPI_FAILURE(status))
		return status;

	obj = (union acpi_object *) result.pointer;
	if (obj) {
		if (obj->type == ACPI_TYPE_BUFFER &&
			(obj->buffer.length == sizeof(u32) ||
			obj->buffer.length == sizeof(u64))) {
			tmp = *((u32 *) obj->buffer.pointer);
		} else if (obj->type == ACPI_TYPE_INTEGER) {
			tmp = (u32) obj->integer.value;
		}
	}

	if (out)
		*out = tmp;

	kfree(result.pointer);

	return status;
}

static acpi_status WMID_get_u32(u32 *value, u32 cap)
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
		return AE_ERROR;
	}
	status = WMI_execute_u32(method_id, 0, &result);

	if (ACPI_SUCCESS(status))
		*value = (u8)result;

	return status;
}

static acpi_status WMID_set_u32(u32 value, u32 cap)
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
			i8042_lock_chip();
			i8042_command(&param, 0x1059);
			i8042_unlock_chip();
			return 0;
		}
		break;
	default:
		return AE_ERROR;
	}
	return WMI_execute_u32(method_id, (u32)value, NULL);
}

static acpi_status wmid3_get_device_status(u32 *value, u16 device)
{
	struct wmid3_gds_return_value return_value;
	acpi_status status;
	union acpi_object *obj;
	struct wmid3_gds_get_input_param params = {
		.function_num = 0x1,
		.hotkey_number = commun_fn_key_number,
		.devices = device,
	};
	struct acpi_buffer input = {
		sizeof(struct wmid3_gds_get_input_param),
		&params
	};
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };

	status = wmi_evaluate_method(WMID_GUID3, 0, 0x2, &input, &output);
	if (ACPI_FAILURE(status))
		return status;

	obj = output.pointer;

	if (!obj)
		return AE_ERROR;
	else if (obj->type != ACPI_TYPE_BUFFER) {
		kfree(obj);
		return AE_ERROR;
	}
	if (obj->buffer.length != 8) {
		pr_warn("Unknown buffer length %d\n", obj->buffer.length);
		kfree(obj);
		return AE_ERROR;
	}

	return_value = *((struct wmid3_gds_return_value *)obj->buffer.pointer);
	kfree(obj);

	if (return_value.error_code || return_value.ec_return_value)
		pr_warn("Get 0x%x Device Status failed: 0x%x - 0x%x\n",
			device,
			return_value.error_code,
			return_value.ec_return_value);
	else
		*value = !!(return_value.devices & device);

	return status;
}

static acpi_status wmid_v2_get_u32(u32 *value, u32 cap)
{
	u16 device;

	switch (cap) {
	case ACER_CAP_WIRELESS:
		device = ACER_WMID3_GDS_WIRELESS;
		break;
	case ACER_CAP_BLUETOOTH:
		device = ACER_WMID3_GDS_BLUETOOTH;
		break;
	case ACER_CAP_THREEG:
		device = ACER_WMID3_GDS_THREEG;
		break;
	default:
		return AE_ERROR;
	}
	return wmid3_get_device_status(value, device);
}

static acpi_status wmid3_set_device_status(u32 value, u16 device)
{
	struct wmid3_gds_return_value return_value;
	acpi_status status;
	union acpi_object *obj;
	u16 devices;
	struct wmid3_gds_get_input_param get_params = {
		.function_num = 0x1,
		.hotkey_number = commun_fn_key_number,
		.devices = commun_func_bitmap,
	};
	struct acpi_buffer get_input = {
		sizeof(struct wmid3_gds_get_input_param),
		&get_params
	};
	struct wmid3_gds_set_input_param set_params = {
		.function_num = 0x2,
		.hotkey_number = commun_fn_key_number,
		.devices = commun_func_bitmap,
	};
	struct acpi_buffer set_input = {
		sizeof(struct wmid3_gds_set_input_param),
		&set_params
	};
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer output2 = { ACPI_ALLOCATE_BUFFER, NULL };

	status = wmi_evaluate_method(WMID_GUID3, 0, 0x2, &get_input, &output);
	if (ACPI_FAILURE(status))
		return status;

	obj = output.pointer;

	if (!obj)
		return AE_ERROR;
	else if (obj->type != ACPI_TYPE_BUFFER) {
		kfree(obj);
		return AE_ERROR;
	}
	if (obj->buffer.length != 8) {
		pr_warn("Unknown buffer length %d\n", obj->buffer.length);
		kfree(obj);
		return AE_ERROR;
	}

	return_value = *((struct wmid3_gds_return_value *)obj->buffer.pointer);
	kfree(obj);

	if (return_value.error_code || return_value.ec_return_value) {
		pr_warn("Get Current Device Status failed: 0x%x - 0x%x\n",
			return_value.error_code,
			return_value.ec_return_value);
		return status;
	}

	devices = return_value.devices;
	set_params.devices = (value) ? (devices | device) : (devices & ~device);

	status = wmi_evaluate_method(WMID_GUID3, 0, 0x1, &set_input, &output2);
	if (ACPI_FAILURE(status))
		return status;

	obj = output2.pointer;

	if (!obj)
		return AE_ERROR;
	else if (obj->type != ACPI_TYPE_BUFFER) {
		kfree(obj);
		return AE_ERROR;
	}
	if (obj->buffer.length != 4) {
		pr_warn("Unknown buffer length %d\n", obj->buffer.length);
		kfree(obj);
		return AE_ERROR;
	}

	return_value = *((struct wmid3_gds_return_value *)obj->buffer.pointer);
	kfree(obj);

	if (return_value.error_code || return_value.ec_return_value)
		pr_warn("Set Device Status failed: 0x%x - 0x%x\n",
			return_value.error_code,
			return_value.ec_return_value);

	return status;
}

static acpi_status wmid_v2_set_u32(u32 value, u32 cap)
{
	u16 device;

	switch (cap) {
	case ACER_CAP_WIRELESS:
		device = ACER_WMID3_GDS_WIRELESS;
		break;
	case ACER_CAP_BLUETOOTH:
		device = ACER_WMID3_GDS_BLUETOOTH;
		break;
	case ACER_CAP_THREEG:
		device = ACER_WMID3_GDS_THREEG;
		break;
	default:
		return AE_ERROR;
	}
	return wmid3_set_device_status(value, device);
}

static void __init type_aa_dmi_decode(const struct dmi_header *header, void *d)
{
	struct hotkey_function_type_aa *type_aa;

	/* We are looking for OEM-specific Type AAh */
	if (header->type != 0xAA)
		return;

	has_type_aa = true;
	type_aa = (struct hotkey_function_type_aa *) header;

	pr_info("Function bitmap for Communication Button: 0x%x\n",
		type_aa->commun_func_bitmap);
	commun_func_bitmap = type_aa->commun_func_bitmap;

	if (type_aa->commun_func_bitmap & ACER_WMID3_GDS_WIRELESS)
		interface->capability |= ACER_CAP_WIRELESS;
	if (type_aa->commun_func_bitmap & ACER_WMID3_GDS_THREEG)
		interface->capability |= ACER_CAP_THREEG;
	if (type_aa->commun_func_bitmap & ACER_WMID3_GDS_BLUETOOTH)
		interface->capability |= ACER_CAP_BLUETOOTH;

	commun_fn_key_number = type_aa->commun_fn_key_number;
}

static acpi_status __init WMID_set_capabilities(void)
{
	struct acpi_buffer out = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *obj;
	acpi_status status;
	u32 devices;

	status = wmi_query_block(WMID_GUID2, 1, &out);
	if (ACPI_FAILURE(status))
		return status;

	obj = (union acpi_object *) out.pointer;
	if (obj) {
		if (obj->type == ACPI_TYPE_BUFFER &&
			(obj->buffer.length == sizeof(u32) ||
			obj->buffer.length == sizeof(u64))) {
			devices = *((u32 *) obj->buffer.pointer);
		} else if (obj->type == ACPI_TYPE_INTEGER) {
			devices = (u32) obj->integer.value;
		} else {
			kfree(out.pointer);
			return AE_ERROR;
		}
	} else {
		kfree(out.pointer);
		return AE_ERROR;
	}

	pr_info("Function bitmap for Communication Device: 0x%x\n", devices);
	if (devices & 0x07)
		interface->capability |= ACER_CAP_WIRELESS;
	if (devices & 0x40)
		interface->capability |= ACER_CAP_THREEG;
	if (devices & 0x10)
		interface->capability |= ACER_CAP_BLUETOOTH;

	if (!(devices & 0x20))
		max_brightness = 0x9;

	kfree(out.pointer);
	return status;
}

static struct wmi_interface wmid_interface = {
	.type = ACER_WMID,
};

static struct wmi_interface wmid_v2_interface = {
	.type = ACER_WMID_v2,
};

/*
 * Generic Device (interface-independent)
 */

static acpi_status get_u32(u32 *value, u32 cap)
{
	acpi_status status = AE_ERROR;

	switch (interface->type) {
	case ACER_AMW0:
		status = AMW0_get_u32(value, cap);
		break;
	case ACER_AMW0_V2:
		if (cap == ACER_CAP_MAILLED) {
			status = AMW0_get_u32(value, cap);
			break;
		}
	case ACER_WMID:
		status = WMID_get_u32(value, cap);
		break;
	case ACER_WMID_v2:
		if (cap & (ACER_CAP_WIRELESS |
			   ACER_CAP_BLUETOOTH |
			   ACER_CAP_THREEG))
			status = wmid_v2_get_u32(value, cap);
		else if (wmi_has_guid(WMID_GUID2))
			status = WMID_get_u32(value, cap);
		break;
	}

	return status;
}

static acpi_status set_u32(u32 value, u32 cap)
{
	acpi_status status;

	if (interface->capability & cap) {
		switch (interface->type) {
		case ACER_AMW0:
			return AMW0_set_u32(value, cap);
		case ACER_AMW0_V2:
			if (cap == ACER_CAP_MAILLED)
				return AMW0_set_u32(value, cap);

			/*
			 * On some models, some WMID methods don't toggle
			 * properly. For those cases, we want to run the AMW0
			 * method afterwards to be certain we've really toggled
			 * the device state.
			 */
			if (cap == ACER_CAP_WIRELESS ||
				cap == ACER_CAP_BLUETOOTH) {
				status = WMID_set_u32(value, cap);
				if (ACPI_FAILURE(status))
					return status;

				return AMW0_set_u32(value, cap);
			}
		case ACER_WMID:
			return WMID_set_u32(value, cap);
		case ACER_WMID_v2:
			if (cap & (ACER_CAP_WIRELESS |
				   ACER_CAP_BLUETOOTH |
				   ACER_CAP_THREEG))
				return wmid_v2_set_u32(value, cap);
			else if (wmi_has_guid(WMID_GUID2))
				return WMID_set_u32(value, cap);
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
	if (mailled >= 0)
		set_u32(mailled, ACER_CAP_MAILLED);
	if (!has_type_aa && threeg >= 0)
		set_u32(threeg, ACER_CAP_THREEG);
	if (brightness >= 0)
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
	.name = "acer-wmi::mail",
	.brightness_set = mail_led_set,
};

static int acer_led_init(struct device *dev)
{
	return led_classdev_register(dev, &mail_led);
}

static void acer_led_exit(void)
{
	set_u32(LED_OFF, ACER_CAP_MAILLED);
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
	int intensity = bd->props.brightness;

	if (bd->props.power != FB_BLANK_UNBLANK)
		intensity = 0;
	if (bd->props.fb_blank != FB_BLANK_UNBLANK)
		intensity = 0;

	set_u32(intensity, ACER_CAP_BRIGHTNESS);

	return 0;
}

static const struct backlight_ops acer_bl_ops = {
	.get_brightness = read_brightness,
	.update_status = update_bl_status,
};

static int acer_backlight_init(struct device *dev)
{
	struct backlight_properties props;
	struct backlight_device *bd;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = max_brightness;
	bd = backlight_device_register("acer-wmi", dev, NULL, &acer_bl_ops,
				       &props);
	if (IS_ERR(bd)) {
		pr_err("Could not register Acer backlight device\n");
		acer_backlight_device = NULL;
		return PTR_ERR(bd);
	}

	acer_backlight_device = bd;

	bd->props.power = FB_BLANK_UNBLANK;
	bd->props.brightness = read_brightness(bd);
	backlight_update_status(bd);
	return 0;
}

static void acer_backlight_exit(void)
{
	backlight_device_unregister(acer_backlight_device);
}

/*
 * Accelerometer device
 */
static acpi_handle gsensor_handle;

static int acer_gsensor_init(void)
{
	acpi_status status;
	struct acpi_buffer output;
	union acpi_object out_obj;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;
	status = acpi_evaluate_object(gsensor_handle, "_INI", NULL, &output);
	if (ACPI_FAILURE(status))
		return -1;

	return 0;
}

static int acer_gsensor_open(struct input_dev *input)
{
	return acer_gsensor_init();
}

static int acer_gsensor_event(void)
{
	acpi_status status;
	struct acpi_buffer output;
	union acpi_object out_obj[5];

	if (!has_cap(ACER_CAP_ACCEL))
		return -1;

	output.length = sizeof(out_obj);
	output.pointer = out_obj;

	status = acpi_evaluate_object(gsensor_handle, "RDVL", NULL, &output);
	if (ACPI_FAILURE(status))
		return -1;

	if (out_obj->package.count != 4)
		return -1;

	input_report_abs(acer_wmi_accel_dev, ABS_X,
		(s16)out_obj->package.elements[0].integer.value);
	input_report_abs(acer_wmi_accel_dev, ABS_Y,
		(s16)out_obj->package.elements[1].integer.value);
	input_report_abs(acer_wmi_accel_dev, ABS_Z,
		(s16)out_obj->package.elements[2].integer.value);
	input_sync(acer_wmi_accel_dev);
	return 0;
}

/*
 * Rfkill devices
 */
static void acer_rfkill_update(struct work_struct *ignored);
static DECLARE_DELAYED_WORK(acer_rfkill_work, acer_rfkill_update);
static void acer_rfkill_update(struct work_struct *ignored)
{
	u32 state;
	acpi_status status;

	if (has_cap(ACER_CAP_WIRELESS)) {
		status = get_u32(&state, ACER_CAP_WIRELESS);
		if (ACPI_SUCCESS(status)) {
			if (quirks->wireless == 3)
				rfkill_set_hw_state(wireless_rfkill, !state);
			else
				rfkill_set_sw_state(wireless_rfkill, !state);
		}
	}

	if (has_cap(ACER_CAP_BLUETOOTH)) {
		status = get_u32(&state, ACER_CAP_BLUETOOTH);
		if (ACPI_SUCCESS(status))
			rfkill_set_sw_state(bluetooth_rfkill, !state);
	}

	if (has_cap(ACER_CAP_THREEG) && wmi_has_guid(WMID_GUID3)) {
		status = get_u32(&state, ACER_WMID3_GDS_THREEG);
		if (ACPI_SUCCESS(status))
			rfkill_set_sw_state(threeg_rfkill, !state);
	}

	schedule_delayed_work(&acer_rfkill_work, round_jiffies_relative(HZ));
}

static int acer_rfkill_set(void *data, bool blocked)
{
	acpi_status status;
	u32 cap = (unsigned long)data;

	if (rfkill_inited) {
		status = set_u32(!blocked, cap);
		if (ACPI_FAILURE(status))
			return -ENODEV;
	}

	return 0;
}

static const struct rfkill_ops acer_rfkill_ops = {
	.set_block = acer_rfkill_set,
};

static struct rfkill *acer_rfkill_register(struct device *dev,
					   enum rfkill_type type,
					   char *name, u32 cap)
{
	int err;
	struct rfkill *rfkill_dev;
	u32 state;
	acpi_status status;

	rfkill_dev = rfkill_alloc(name, dev, type,
				  &acer_rfkill_ops,
				  (void *)(unsigned long)cap);
	if (!rfkill_dev)
		return ERR_PTR(-ENOMEM);

	status = get_u32(&state, cap);

	err = rfkill_register(rfkill_dev);
	if (err) {
		rfkill_destroy(rfkill_dev);
		return ERR_PTR(err);
	}

	if (ACPI_SUCCESS(status))
		rfkill_set_sw_state(rfkill_dev, !state);

	return rfkill_dev;
}

static int acer_rfkill_init(struct device *dev)
{
	int err;

	if (has_cap(ACER_CAP_WIRELESS)) {
		wireless_rfkill = acer_rfkill_register(dev, RFKILL_TYPE_WLAN,
			"acer-wireless", ACER_CAP_WIRELESS);
		if (IS_ERR(wireless_rfkill)) {
			err = PTR_ERR(wireless_rfkill);
			goto error_wireless;
		}
	}

	if (has_cap(ACER_CAP_BLUETOOTH)) {
		bluetooth_rfkill = acer_rfkill_register(dev,
			RFKILL_TYPE_BLUETOOTH, "acer-bluetooth",
			ACER_CAP_BLUETOOTH);
		if (IS_ERR(bluetooth_rfkill)) {
			err = PTR_ERR(bluetooth_rfkill);
			goto error_bluetooth;
		}
	}

	if (has_cap(ACER_CAP_THREEG)) {
		threeg_rfkill = acer_rfkill_register(dev,
			RFKILL_TYPE_WWAN, "acer-threeg",
			ACER_CAP_THREEG);
		if (IS_ERR(threeg_rfkill)) {
			err = PTR_ERR(threeg_rfkill);
			goto error_threeg;
		}
	}

	rfkill_inited = true;

	if ((ec_raw_mode || !wmi_has_guid(ACERWMID_EVENT_GUID)) &&
	    has_cap(ACER_CAP_WIRELESS | ACER_CAP_BLUETOOTH | ACER_CAP_THREEG))
		schedule_delayed_work(&acer_rfkill_work,
			round_jiffies_relative(HZ));

	return 0;

error_threeg:
	if (has_cap(ACER_CAP_BLUETOOTH)) {
		rfkill_unregister(bluetooth_rfkill);
		rfkill_destroy(bluetooth_rfkill);
	}
error_bluetooth:
	if (has_cap(ACER_CAP_WIRELESS)) {
		rfkill_unregister(wireless_rfkill);
		rfkill_destroy(wireless_rfkill);
	}
error_wireless:
	return err;
}

static void acer_rfkill_exit(void)
{
	if ((ec_raw_mode || !wmi_has_guid(ACERWMID_EVENT_GUID)) &&
	    has_cap(ACER_CAP_WIRELESS | ACER_CAP_BLUETOOTH | ACER_CAP_THREEG))
		cancel_delayed_work_sync(&acer_rfkill_work);

	if (has_cap(ACER_CAP_WIRELESS)) {
		rfkill_unregister(wireless_rfkill);
		rfkill_destroy(wireless_rfkill);
	}

	if (has_cap(ACER_CAP_BLUETOOTH)) {
		rfkill_unregister(bluetooth_rfkill);
		rfkill_destroy(bluetooth_rfkill);
	}

	if (has_cap(ACER_CAP_THREEG)) {
		rfkill_unregister(threeg_rfkill);
		rfkill_destroy(threeg_rfkill);
	}
	return;
}

static void acer_wmi_notify(u32 value, void *context)
{
	struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	struct event_return_value return_value;
	acpi_status status;
	u16 device_state;
	const struct key_entry *key;
	u32 scancode;

	status = wmi_get_event_data(value, &response);
	if (status != AE_OK) {
		pr_warn("bad event status 0x%x\n", status);
		return;
	}

	obj = (union acpi_object *)response.pointer;

	if (!obj)
		return;
	if (obj->type != ACPI_TYPE_BUFFER) {
		pr_warn("Unknown response received %d\n", obj->type);
		kfree(obj);
		return;
	}
	if (obj->buffer.length != 8) {
		pr_warn("Unknown buffer length %d\n", obj->buffer.length);
		kfree(obj);
		return;
	}

	return_value = *((struct event_return_value *)obj->buffer.pointer);
	kfree(obj);

	switch (return_value.function) {
	case WMID_HOTKEY_EVENT:
		device_state = return_value.device_state;
		pr_debug("device state: 0x%x\n", device_state);

		key = sparse_keymap_entry_from_scancode(acer_wmi_input_dev,
							return_value.key_num);
		if (!key) {
			pr_warn("Unknown key number - 0x%x\n",
				return_value.key_num);
		} else {
			scancode = return_value.key_num;
			switch (key->keycode) {
			case KEY_WLAN:
			case KEY_BLUETOOTH:
				if (has_cap(ACER_CAP_WIRELESS))
					rfkill_set_sw_state(wireless_rfkill,
						!(device_state & ACER_WMID3_GDS_WIRELESS));
				if (has_cap(ACER_CAP_THREEG))
					rfkill_set_sw_state(threeg_rfkill,
						!(device_state & ACER_WMID3_GDS_THREEG));
				if (has_cap(ACER_CAP_BLUETOOTH))
					rfkill_set_sw_state(bluetooth_rfkill,
						!(device_state & ACER_WMID3_GDS_BLUETOOTH));
				break;
			case KEY_TOUCHPAD_TOGGLE:
				scancode = (device_state & ACER_WMID3_GDS_TOUCHPAD) ?
						KEY_TOUCHPAD_ON : KEY_TOUCHPAD_OFF;
			}
			sparse_keymap_report_event(acer_wmi_input_dev, scancode, 1, true);
		}
		break;
	case WMID_ACCEL_EVENT:
		acer_gsensor_event();
		break;
	default:
		pr_warn("Unknown function number - %d - %d\n",
			return_value.function, return_value.key_num);
		break;
	}
}

static acpi_status __init
wmid3_set_function_mode(struct func_input_params *params,
			struct func_return_value *return_value)
{
	acpi_status status;
	union acpi_object *obj;

	struct acpi_buffer input = { sizeof(struct func_input_params), params };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };

	status = wmi_evaluate_method(WMID_GUID3, 0, 0x1, &input, &output);
	if (ACPI_FAILURE(status))
		return status;

	obj = output.pointer;

	if (!obj)
		return AE_ERROR;
	else if (obj->type != ACPI_TYPE_BUFFER) {
		kfree(obj);
		return AE_ERROR;
	}
	if (obj->buffer.length != 4) {
		pr_warn("Unknown buffer length %d\n", obj->buffer.length);
		kfree(obj);
		return AE_ERROR;
	}

	*return_value = *((struct func_return_value *)obj->buffer.pointer);
	kfree(obj);

	return status;
}

static int __init acer_wmi_enable_ec_raw(void)
{
	struct func_return_value return_value;
	acpi_status status;
	struct func_input_params params = {
		.function_num = 0x1,
		.commun_devices = 0xFFFF,
		.devices = 0xFFFF,
		.app_status = 0x00,		/* Launch Manager Deactive */
		.app_mask = 0x01,
	};

	status = wmid3_set_function_mode(&params, &return_value);

	if (return_value.error_code || return_value.ec_return_value)
		pr_warn("Enabling EC raw mode failed: 0x%x - 0x%x\n",
			return_value.error_code,
			return_value.ec_return_value);
	else
		pr_info("Enabled EC raw mode\n");

	return status;
}

static int __init acer_wmi_enable_lm(void)
{
	struct func_return_value return_value;
	acpi_status status;
	struct func_input_params params = {
		.function_num = 0x1,
		.commun_devices = 0xFFFF,
		.devices = 0xFFFF,
		.app_status = 0x01,            /* Launch Manager Active */
		.app_mask = 0x01,
	};

	status = wmid3_set_function_mode(&params, &return_value);

	if (return_value.error_code || return_value.ec_return_value)
		pr_warn("Enabling Launch Manager failed: 0x%x - 0x%x\n",
			return_value.error_code,
			return_value.ec_return_value);

	return status;
}

static int __init acer_wmi_enable_rf_button(void)
{
	struct func_return_value return_value;
	acpi_status status;
	struct func_input_params params = {
		.function_num = 0x1,
		.commun_devices = 0xFFFF,
		.devices = 0xFFFF,
		.app_status = 0x10,            /* RF Button Active */
		.app_mask = 0x10,
	};

	status = wmid3_set_function_mode(&params, &return_value);

	if (return_value.error_code || return_value.ec_return_value)
		pr_warn("Enabling RF Button failed: 0x%x - 0x%x\n",
			return_value.error_code,
			return_value.ec_return_value);

	return status;
}

#define ACER_WMID_ACCEL_HID	"BST0001"

static acpi_status __init acer_wmi_get_handle_cb(acpi_handle ah, u32 level,
						void *ctx, void **retval)
{
	struct acpi_device *dev;

	if (!strcmp(ctx, "SENR")) {
		if (acpi_bus_get_device(ah, &dev))
			return AE_OK;
		if (!strcmp(ACER_WMID_ACCEL_HID, acpi_device_hid(dev)))
			return AE_OK;
	} else
		return AE_OK;

	*(acpi_handle *)retval = ah;

	return AE_CTRL_TERMINATE;
}

static int __init acer_wmi_get_handle(const char *name, const char *prop,
					acpi_handle *ah)
{
	acpi_status status;
	acpi_handle handle;

	BUG_ON(!name || !ah);

	handle = NULL;
	status = acpi_get_devices(prop, acer_wmi_get_handle_cb,
					(void *)name, &handle);

	if (ACPI_SUCCESS(status)) {
		*ah = handle;
		return 0;
	} else {
		return -ENODEV;
	}
}

static int __init acer_wmi_accel_setup(void)
{
	int err;

	err = acer_wmi_get_handle("SENR", ACER_WMID_ACCEL_HID, &gsensor_handle);
	if (err)
		return err;

	interface->capability |= ACER_CAP_ACCEL;

	acer_wmi_accel_dev = input_allocate_device();
	if (!acer_wmi_accel_dev)
		return -ENOMEM;

	acer_wmi_accel_dev->open = acer_gsensor_open;

	acer_wmi_accel_dev->name = "Acer BMA150 accelerometer";
	acer_wmi_accel_dev->phys = "wmi/input1";
	acer_wmi_accel_dev->id.bustype = BUS_HOST;
	acer_wmi_accel_dev->evbit[0] = BIT_MASK(EV_ABS);
	input_set_abs_params(acer_wmi_accel_dev, ABS_X, -16384, 16384, 0, 0);
	input_set_abs_params(acer_wmi_accel_dev, ABS_Y, -16384, 16384, 0, 0);
	input_set_abs_params(acer_wmi_accel_dev, ABS_Z, -16384, 16384, 0, 0);

	err = input_register_device(acer_wmi_accel_dev);
	if (err)
		goto err_free_dev;

	return 0;

err_free_dev:
	input_free_device(acer_wmi_accel_dev);
	return err;
}

static void acer_wmi_accel_destroy(void)
{
	input_unregister_device(acer_wmi_accel_dev);
}

static int __init acer_wmi_input_setup(void)
{
	acpi_status status;
	int err;

	acer_wmi_input_dev = input_allocate_device();
	if (!acer_wmi_input_dev)
		return -ENOMEM;

	acer_wmi_input_dev->name = "Acer WMI hotkeys";
	acer_wmi_input_dev->phys = "wmi/input0";
	acer_wmi_input_dev->id.bustype = BUS_HOST;

	err = sparse_keymap_setup(acer_wmi_input_dev, acer_wmi_keymap, NULL);
	if (err)
		goto err_free_dev;

	status = wmi_install_notify_handler(ACERWMID_EVENT_GUID,
						acer_wmi_notify, NULL);
	if (ACPI_FAILURE(status)) {
		err = -EIO;
		goto err_free_keymap;
	}

	err = input_register_device(acer_wmi_input_dev);
	if (err)
		goto err_uninstall_notifier;

	return 0;

err_uninstall_notifier:
	wmi_remove_notify_handler(ACERWMID_EVENT_GUID);
err_free_keymap:
	sparse_keymap_free(acer_wmi_input_dev);
err_free_dev:
	input_free_device(acer_wmi_input_dev);
	return err;
}

static void acer_wmi_input_destroy(void)
{
	wmi_remove_notify_handler(ACERWMID_EVENT_GUID);
	sparse_keymap_free(acer_wmi_input_dev);
	input_unregister_device(acer_wmi_input_dev);
}

/*
 * debugfs functions
 */
static u32 get_wmid_devices(void)
{
	struct acpi_buffer out = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *obj;
	acpi_status status;
	u32 devices = 0;

	status = wmi_query_block(WMID_GUID2, 1, &out);
	if (ACPI_FAILURE(status))
		return 0;

	obj = (union acpi_object *) out.pointer;
	if (obj) {
		if (obj->type == ACPI_TYPE_BUFFER &&
			(obj->buffer.length == sizeof(u32) ||
			obj->buffer.length == sizeof(u64))) {
			devices = *((u32 *) obj->buffer.pointer);
		} else if (obj->type == ACPI_TYPE_INTEGER) {
			devices = (u32) obj->integer.value;
		}
	}

	kfree(out.pointer);
	return devices;
}

/*
 * Platform device
 */
static int acer_platform_probe(struct platform_device *device)
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

	err = acer_rfkill_init(&device->dev);
	if (err)
		goto error_rfkill;

	return err;

error_rfkill:
	if (has_cap(ACER_CAP_BRIGHTNESS))
		acer_backlight_exit();
error_brightness:
	if (has_cap(ACER_CAP_MAILLED))
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

	acer_rfkill_exit();
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int acer_suspend(struct device *dev)
{
	u32 value;
	struct acer_data *data = &interface->data;

	if (!data)
		return -ENOMEM;

	if (has_cap(ACER_CAP_MAILLED)) {
		get_u32(&value, ACER_CAP_MAILLED);
		set_u32(LED_OFF, ACER_CAP_MAILLED);
		data->mailled = value;
	}

	if (has_cap(ACER_CAP_BRIGHTNESS)) {
		get_u32(&value, ACER_CAP_BRIGHTNESS);
		data->brightness = value;
	}

	return 0;
}

static int acer_resume(struct device *dev)
{
	struct acer_data *data = &interface->data;

	if (!data)
		return -ENOMEM;

	if (has_cap(ACER_CAP_MAILLED))
		set_u32(data->mailled, ACER_CAP_MAILLED);

	if (has_cap(ACER_CAP_BRIGHTNESS))
		set_u32(data->brightness, ACER_CAP_BRIGHTNESS);

	if (has_cap(ACER_CAP_ACCEL))
		acer_gsensor_init();

	return 0;
}
#else
#define acer_suspend	NULL
#define acer_resume	NULL
#endif

static SIMPLE_DEV_PM_OPS(acer_pm, acer_suspend, acer_resume);

static void acer_platform_shutdown(struct platform_device *device)
{
	struct acer_data *data = &interface->data;

	if (!data)
		return;

	if (has_cap(ACER_CAP_MAILLED))
		set_u32(LED_OFF, ACER_CAP_MAILLED);
}

static struct platform_driver acer_platform_driver = {
	.driver = {
		.name = "acer-wmi",
		.pm = &acer_pm,
	},
	.probe = acer_platform_probe,
	.remove = acer_platform_remove,
	.shutdown = acer_platform_shutdown,
};

static struct platform_device *acer_platform_device;

static void remove_debugfs(void)
{
	debugfs_remove(interface->debug.devices);
	debugfs_remove(interface->debug.root);
}

static int __init create_debugfs(void)
{
	interface->debug.root = debugfs_create_dir("acer-wmi", NULL);
	if (!interface->debug.root) {
		pr_err("Failed to create debugfs directory");
		return -ENOMEM;
	}

	interface->debug.devices = debugfs_create_u32("devices", S_IRUGO,
					interface->debug.root,
					&interface->debug.wmid_devices);
	if (!interface->debug.devices)
		goto error_debugfs;

	return 0;

error_debugfs:
	remove_debugfs();
	return -ENOMEM;
}

static int __init acer_wmi_init(void)
{
	int err;

	pr_info("Acer Laptop ACPI-WMI Extras\n");

	if (dmi_check_system(acer_blacklist)) {
		pr_info("Blacklisted hardware detected - not loading\n");
		return -ENODEV;
	}

	find_quirks();

	/*
	 * The AMW0_GUID1 wmi is not only found on Acer family but also other
	 * machines like Lenovo, Fujitsu and Medion. In the past days,
	 * acer-wmi driver handled those non-Acer machines by quirks list.
	 * But actually acer-wmi driver was loaded on any machines that have
	 * AMW0_GUID1. This behavior is strange because those machines should
	 * be supported by appropriate wmi drivers. e.g. fujitsu-laptop,
	 * ideapad-laptop. So, here checks the machine that has AMW0_GUID1
	 * should be in Acer/Gateway/Packard Bell white list, or it's already
	 * in the past quirk list.
	 */
	if (wmi_has_guid(AMW0_GUID1) &&
	    !dmi_check_system(amw0_whitelist) &&
	    quirks == &quirk_unknown) {
		pr_err("Unsupported machine has AMW0_GUID1, unable to load\n");
		return -ENODEV;
	}

	/*
	 * Detect which ACPI-WMI interface we're using.
	 */
	if (wmi_has_guid(AMW0_GUID1) && wmi_has_guid(WMID_GUID1))
		interface = &AMW0_V2_interface;

	if (!wmi_has_guid(AMW0_GUID1) && wmi_has_guid(WMID_GUID1))
		interface = &wmid_interface;

	if (wmi_has_guid(WMID_GUID3))
		interface = &wmid_v2_interface;

	if (interface)
		dmi_walk(type_aa_dmi_decode, NULL);

	if (wmi_has_guid(WMID_GUID2) && interface) {
		if (!has_type_aa && ACPI_FAILURE(WMID_set_capabilities())) {
			pr_err("Unable to detect available WMID devices\n");
			return -ENODEV;
		}
		/* WMID always provides brightness methods */
		interface->capability |= ACER_CAP_BRIGHTNESS;
	} else if (!wmi_has_guid(WMID_GUID2) && interface && !has_type_aa) {
		pr_err("No WMID device detection method found\n");
		return -ENODEV;
	}

	if (wmi_has_guid(AMW0_GUID1) && !wmi_has_guid(WMID_GUID1)) {
		interface = &AMW0_interface;

		if (ACPI_FAILURE(AMW0_set_capabilities())) {
			pr_err("Unable to detect available AMW0 devices\n");
			return -ENODEV;
		}
	}

	if (wmi_has_guid(AMW0_GUID1))
		AMW0_find_mailled();

	if (!interface) {
		pr_err("No or unsupported WMI interface, unable to load\n");
		return -ENODEV;
	}

	set_quirks();

	if (dmi_check_system(video_vendor_dmi_table))
		acpi_video_set_dmi_backlight_type(acpi_backlight_vendor);

	if (acpi_video_get_backlight_type() != acpi_backlight_vendor)
		interface->capability &= ~ACER_CAP_BRIGHTNESS;

	if (wmi_has_guid(WMID_GUID3)) {
		if (ACPI_FAILURE(acer_wmi_enable_rf_button()))
			pr_warn("Cannot enable RF Button Driver\n");

		if (ec_raw_mode) {
			if (ACPI_FAILURE(acer_wmi_enable_ec_raw())) {
				pr_err("Cannot enable EC raw mode\n");
				return -ENODEV;
			}
		} else if (ACPI_FAILURE(acer_wmi_enable_lm())) {
			pr_err("Cannot enable Launch Manager mode\n");
			return -ENODEV;
		}
	} else if (ec_raw_mode) {
		pr_info("No WMID EC raw mode enable method\n");
	}

	if (wmi_has_guid(ACERWMID_EVENT_GUID)) {
		err = acer_wmi_input_setup();
		if (err)
			return err;
		err = acer_wmi_accel_setup();
		if (err)
			return err;
	}

	err = platform_driver_register(&acer_platform_driver);
	if (err) {
		pr_err("Unable to register platform driver\n");
		goto error_platform_register;
	}

	acer_platform_device = platform_device_alloc("acer-wmi", -1);
	if (!acer_platform_device) {
		err = -ENOMEM;
		goto error_device_alloc;
	}

	err = platform_device_add(acer_platform_device);
	if (err)
		goto error_device_add;

	if (wmi_has_guid(WMID_GUID2)) {
		interface->debug.wmid_devices = get_wmid_devices();
		err = create_debugfs();
		if (err)
			goto error_create_debugfs;
	}

	/* Override any initial settings with values from the commandline */
	acer_commandline_init();

	return 0;

error_create_debugfs:
	platform_device_del(acer_platform_device);
error_device_add:
	platform_device_put(acer_platform_device);
error_device_alloc:
	platform_driver_unregister(&acer_platform_driver);
error_platform_register:
	if (wmi_has_guid(ACERWMID_EVENT_GUID))
		acer_wmi_input_destroy();
	if (has_cap(ACER_CAP_ACCEL))
		acer_wmi_accel_destroy();

	return err;
}

static void __exit acer_wmi_exit(void)
{
	if (wmi_has_guid(ACERWMID_EVENT_GUID))
		acer_wmi_input_destroy();

	if (has_cap(ACER_CAP_ACCEL))
		acer_wmi_accel_destroy();

	remove_debugfs();
	platform_device_unregister(acer_platform_device);
	platform_driver_unregister(&acer_platform_driver);

	pr_info("Acer Laptop WMI Extras unloaded\n");
	return;
}

module_init(acer_wmi_init);
module_exit(acer_wmi_exit);
