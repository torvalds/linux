/*
 * Eee PC WMI hotkey driver
 *
 * Copyright(C) 2010 Intel Corporation.
 * Copyright(C) 2010-2011 Corentin Chary <corentin.chary@gmail.com>
 *
 * Portions based on wistron_btns.c:
 * Copyright (C) 2005 Miloslav Trmac <mitr@volny.cz>
 * Copyright (C) 2005 Bernhard Rosenkraenzer <bero@arklinux.org>
 * Copyright (C) 2005 Dmitry Torokhov <dtor@mail.ru>
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
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/dmi.h>
#include <linux/fb.h>
#include <acpi/acpi_bus.h>

#include "asus-wmi.h"

#define	EEEPC_WMI_FILE	"eeepc-wmi"

MODULE_AUTHOR("Corentin Chary <corentincj@iksaif.net>");
MODULE_DESCRIPTION("Eee PC WMI Hotkey Driver");
MODULE_LICENSE("GPL");

#define EEEPC_ACPI_HID		"ASUS010" /* old _HID used in eeepc-laptop */

#define EEEPC_WMI_EVENT_GUID	"ABBC0F72-8EA1-11D1-00A0-C90629100000"

MODULE_ALIAS("wmi:"EEEPC_WMI_EVENT_GUID);

static bool hotplug_wireless;

module_param(hotplug_wireless, bool, 0444);
MODULE_PARM_DESC(hotplug_wireless,
		 "Enable hotplug for wireless device. "
		 "If your laptop needs that, please report to "
		 "acpi4asus-user@lists.sourceforge.net.");

/* Values for T101MT "Home" key */
#define HOME_PRESS	0xe4
#define HOME_HOLD	0xea
#define HOME_RELEASE	0xe5

static const struct key_entry eeepc_wmi_keymap[] = {
	/* Sleep already handled via generic ACPI code */
	{ KE_KEY, 0x30, { KEY_VOLUMEUP } },
	{ KE_KEY, 0x31, { KEY_VOLUMEDOWN } },
	{ KE_KEY, 0x32, { KEY_MUTE } },
	{ KE_KEY, 0x5c, { KEY_F15 } }, /* Power Gear key */
	{ KE_KEY, 0x5d, { KEY_WLAN } },
	{ KE_KEY, 0x6b, { KEY_TOUCHPAD_TOGGLE } }, /* Toggle Touchpad */
	{ KE_KEY, 0x82, { KEY_CAMERA } },
	{ KE_KEY, 0x83, { KEY_CAMERA_ZOOMIN } },
	{ KE_KEY, 0x88, { KEY_WLAN } },
	{ KE_KEY, 0xbd, { KEY_CAMERA } },
	{ KE_KEY, 0xcc, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0xe0, { KEY_PROG1 } }, /* Task Manager */
	{ KE_KEY, 0xe1, { KEY_F14 } }, /* Change Resolution */
	{ KE_KEY, HOME_PRESS, { KEY_CONFIG } }, /* Home/Express gate key */
	{ KE_KEY, 0xe8, { KEY_SCREENLOCK } },
	{ KE_KEY, 0xe9, { KEY_BRIGHTNESS_ZERO } },
	{ KE_KEY, 0xeb, { KEY_CAMERA_ZOOMOUT } },
	{ KE_KEY, 0xec, { KEY_CAMERA_UP } },
	{ KE_KEY, 0xed, { KEY_CAMERA_DOWN } },
	{ KE_KEY, 0xee, { KEY_CAMERA_LEFT } },
	{ KE_KEY, 0xef, { KEY_CAMERA_RIGHT } },
	{ KE_KEY, 0xf3, { KEY_MENU } },
	{ KE_KEY, 0xf5, { KEY_HOMEPAGE } },
	{ KE_KEY, 0xf6, { KEY_ESC } },
	{ KE_END, 0},
};

static struct quirk_entry quirk_asus_unknown = {
};

static struct quirk_entry quirk_asus_1000h = {
	.hotplug_wireless = true,
};

static struct quirk_entry quirk_asus_et2012_type1 = {
	.store_backlight_power = true,
};

static struct quirk_entry quirk_asus_et2012_type3 = {
	.scalar_panel_brightness = true,
	.store_backlight_power = true,
};

static struct quirk_entry *quirks;

static void et2012_quirks(void)
{
	const struct dmi_device *dev = NULL;
	char oemstring[30];

	while ((dev = dmi_find_device(DMI_DEV_TYPE_OEM_STRING, NULL, dev))) {
		if (sscanf(dev->name, "AEMS%24c", oemstring) == 1) {
			if (oemstring[18] == '1')
				quirks = &quirk_asus_et2012_type1;
			else if (oemstring[18] == '3')
				quirks = &quirk_asus_et2012_type3;
			break;
		}
	}
}

static int dmi_matched(const struct dmi_system_id *dmi)
{
	char *model;

	quirks = dmi->driver_data;

	model = (char *)dmi->matches[1].substr;
	if (unlikely(strncmp(model, "ET2012", 6) == 0))
		et2012_quirks();

	return 1;
}

static struct dmi_system_id asus_quirks[] = {
	{
		.callback = dmi_matched,
		.ident = "ASUSTeK Computer INC. 1000H",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "1000H"),
		},
		.driver_data = &quirk_asus_1000h,
	},
	{
		.callback = dmi_matched,
		.ident = "ASUSTeK Computer INC. ET2012E/I",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "ET2012"),
		},
		.driver_data = &quirk_asus_unknown,
	},
	{},
};

static void eeepc_wmi_key_filter(struct asus_wmi_driver *asus_wmi, int *code,
				 unsigned int *value, bool *autorelease)
{
	switch (*code) {
	case HOME_PRESS:
		*value = 1;
		*autorelease = 0;
		break;
	case HOME_HOLD:
		*code = ASUS_WMI_KEY_IGNORE;
		break;
	case HOME_RELEASE:
		*code = HOME_PRESS;
		*value = 0;
		*autorelease = 0;
		break;
	}
}

static acpi_status eeepc_wmi_parse_device(acpi_handle handle, u32 level,
						 void *context, void **retval)
{
	pr_warn("Found legacy ATKD device (%s)\n", EEEPC_ACPI_HID);
	*(bool *)context = true;
	return AE_CTRL_TERMINATE;
}

static int eeepc_wmi_check_atkd(void)
{
	acpi_status status;
	bool found = false;

	status = acpi_get_devices(EEEPC_ACPI_HID, eeepc_wmi_parse_device,
				  &found, NULL);

	if (ACPI_FAILURE(status) || !found)
		return 0;
	return -1;
}

static int eeepc_wmi_probe(struct platform_device *pdev)
{
	if (eeepc_wmi_check_atkd()) {
		pr_warn("WMI device present, but legacy ATKD device is also "
			"present and enabled\n");
		pr_warn("You probably booted with acpi_osi=\"Linux\" or "
			"acpi_osi=\"!Windows 2009\"\n");
		pr_warn("Can't load eeepc-wmi, use default acpi_osi "
			"(preferred) or eeepc-laptop\n");
		return -EBUSY;
	}
	return 0;
}

static void eeepc_wmi_quirks(struct asus_wmi_driver *driver)
{
	quirks = &quirk_asus_unknown;
	quirks->hotplug_wireless = hotplug_wireless;

	dmi_check_system(asus_quirks);

	driver->quirks = quirks;
	driver->quirks->wapf = -1;
	driver->panel_power = FB_BLANK_UNBLANK;
}

static struct asus_wmi_driver asus_wmi_driver = {
	.name = EEEPC_WMI_FILE,
	.owner = THIS_MODULE,
	.event_guid = EEEPC_WMI_EVENT_GUID,
	.keymap = eeepc_wmi_keymap,
	.input_name = "Eee PC WMI hotkeys",
	.input_phys = EEEPC_WMI_FILE "/input0",
	.key_filter = eeepc_wmi_key_filter,
	.probe = eeepc_wmi_probe,
	.detect_quirks = eeepc_wmi_quirks,
};


static int __init eeepc_wmi_init(void)
{
	return asus_wmi_register_driver(&asus_wmi_driver);
}

static void __exit eeepc_wmi_exit(void)
{
	asus_wmi_unregister_driver(&asus_wmi_driver);
}

module_init(eeepc_wmi_init);
module_exit(eeepc_wmi_exit);
