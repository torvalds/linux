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
	{ KE_KEY, 0xe8, { KEY_SCREENLOCK } },
	{ KE_KEY, 0xe9, { KEY_BRIGHTNESS_ZERO } },
	{ KE_KEY, 0xeb, { KEY_CAMERA_ZOOMOUT } },
	{ KE_KEY, 0xec, { KEY_CAMERA_UP } },
	{ KE_KEY, 0xed, { KEY_CAMERA_DOWN } },
	{ KE_KEY, 0xee, { KEY_CAMERA_LEFT } },
	{ KE_KEY, 0xef, { KEY_CAMERA_RIGHT } },
	{ KE_END, 0},
};

static acpi_status eeepc_wmi_parse_device(acpi_handle handle, u32 level,
						 void *context, void **retval)
{
	pr_warning("Found legacy ATKD device (%s)", EEEPC_ACPI_HID);
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
		pr_warning("WMI device present, but legacy ATKD device is also "
			   "present and enabled.");
		pr_warning("You probably booted with acpi_osi=\"Linux\" or "
			   "acpi_osi=\"!Windows 2009\"");
		pr_warning("Can't load eeepc-wmi, use default acpi_osi "
			   "(preferred) or eeepc-laptop");
		return -EBUSY;
	}
	return 0;
}

static void eeepc_dmi_check(struct asus_wmi_driver *driver)
{
	const char *model;

	model = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (!model)
		return;

	/*
	 * Whitelist for wlan hotplug
	 *
	 * Asus 1000H needs the current hotplug code to handle
	 * Fn+F2 correctly. We may add other Asus here later, but
	 * it seems that most of the laptops supported by asus-wmi
	 * don't need to be on this list
	 */
	if (strcmp(model, "1000H") == 0) {
		driver->hotplug_wireless = true;
		pr_info("wlan hotplug enabled\n");
	}
}

static void eeepc_wmi_quirks(struct asus_wmi_driver *driver)
{
	driver->hotplug_wireless = hotplug_wireless;
	eeepc_dmi_check(driver);
}

static struct asus_wmi_driver asus_wmi_driver = {
	.name = EEEPC_WMI_FILE,
	.owner = THIS_MODULE,
	.event_guid = EEEPC_WMI_EVENT_GUID,
	.keymap = eeepc_wmi_keymap,
	.input_name = "Eee PC WMI hotkeys",
	.input_phys = EEEPC_WMI_FILE "/input0",
	.probe = eeepc_wmi_probe,
	.quirks = eeepc_wmi_quirks,
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
