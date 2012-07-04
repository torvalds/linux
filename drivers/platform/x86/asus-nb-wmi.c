/*
 * Asus Notebooks WMI hotkey driver
 *
 * Copyright(C) 2010 Corentin Chary <corentin.chary@gmail.com>
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
#include <linux/fb.h>

#include "asus-wmi.h"

#define	ASUS_NB_WMI_FILE	"asus-nb-wmi"

MODULE_AUTHOR("Corentin Chary <corentincj@iksaif.net>");
MODULE_DESCRIPTION("Asus Notebooks WMI Hotkey Driver");
MODULE_LICENSE("GPL");

#define ASUS_NB_WMI_EVENT_GUID	"0B3CBB35-E3C2-45ED-91C2-4C5A6D195D1C"

MODULE_ALIAS("wmi:"ASUS_NB_WMI_EVENT_GUID);

/*
 * WAPF defines the behavior of the Fn+Fx wlan key
 * The significance of values is yet to be found, but
 * most of the time:
 * Bit | Bluetooth | WLAN
 *  0  | Hardware  | Hardware
 *  1  | Hardware  | Software
 *  4  | Software  | Software
 */
static uint wapf;
module_param(wapf, uint, 0444);
MODULE_PARM_DESC(wapf, "WAPF value");

static struct quirk_entry quirk_asus_unknown = {
};

static void asus_nb_wmi_quirks(struct asus_wmi_driver *driver)
{
	driver->quirks = &quirk_asus_unknown;
	driver->quirks->wapf = wapf;
	driver->panel_power = FB_BLANK_UNBLANK;
}

static const struct key_entry asus_nb_wmi_keymap[] = {
	{ KE_KEY, 0x30, { KEY_VOLUMEUP } },
	{ KE_KEY, 0x31, { KEY_VOLUMEDOWN } },
	{ KE_KEY, 0x32, { KEY_MUTE } },
	{ KE_KEY, 0x33, { KEY_DISPLAYTOGGLE } }, /* LCD on */
	{ KE_KEY, 0x34, { KEY_DISPLAY_OFF } }, /* LCD off */
	{ KE_KEY, 0x40, { KEY_PREVIOUSSONG } },
	{ KE_KEY, 0x41, { KEY_NEXTSONG } },
	{ KE_KEY, 0x43, { KEY_STOPCD } },
	{ KE_KEY, 0x45, { KEY_PLAYPAUSE } },
	{ KE_KEY, 0x4c, { KEY_MEDIA } },
	{ KE_KEY, 0x50, { KEY_EMAIL } },
	{ KE_KEY, 0x51, { KEY_WWW } },
	{ KE_KEY, 0x55, { KEY_CALC } },
	{ KE_IGNORE, 0x57, },  /* Battery mode */
	{ KE_IGNORE, 0x58, },  /* AC mode */
	{ KE_KEY, 0x5C, { KEY_F15 } },  /* Power Gear key */
	{ KE_KEY, 0x5D, { KEY_WLAN } }, /* Wireless console Toggle */
	{ KE_KEY, 0x5E, { KEY_WLAN } }, /* Wireless console Enable */
	{ KE_KEY, 0x5F, { KEY_WLAN } }, /* Wireless console Disable */
	{ KE_KEY, 0x60, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0x61, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0x62, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0x63, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0x6B, { KEY_TOUCHPAD_TOGGLE } },
	{ KE_KEY, 0x7D, { KEY_BLUETOOTH } },
	{ KE_KEY, 0x7E, { KEY_BLUETOOTH } },
	{ KE_KEY, 0x82, { KEY_CAMERA } },
	{ KE_KEY, 0x88, { KEY_RFKILL  } },
	{ KE_KEY, 0x8A, { KEY_PROG1 } },
	{ KE_KEY, 0x95, { KEY_MEDIA } },
	{ KE_KEY, 0x99, { KEY_PHONE } },
	{ KE_KEY, 0xA0, { KEY_SWITCHVIDEOMODE } }, /* SDSP HDMI only */
	{ KE_KEY, 0xA1, { KEY_SWITCHVIDEOMODE } }, /* SDSP LCD + HDMI */
	{ KE_KEY, 0xA2, { KEY_SWITCHVIDEOMODE } }, /* SDSP CRT + HDMI */
	{ KE_KEY, 0xA3, { KEY_SWITCHVIDEOMODE } }, /* SDSP TV + HDMI */
	{ KE_KEY, 0xb5, { KEY_CALC } },
	{ KE_KEY, 0xc4, { KEY_KBDILLUMUP } },
	{ KE_KEY, 0xc5, { KEY_KBDILLUMDOWN } },
	{ KE_END, 0},
};

static struct asus_wmi_driver asus_nb_wmi_driver = {
	.name = ASUS_NB_WMI_FILE,
	.owner = THIS_MODULE,
	.event_guid = ASUS_NB_WMI_EVENT_GUID,
	.keymap = asus_nb_wmi_keymap,
	.input_name = "Asus WMI hotkeys",
	.input_phys = ASUS_NB_WMI_FILE "/input0",
	.detect_quirks = asus_nb_wmi_quirks,
};


static int __init asus_nb_wmi_init(void)
{
	return asus_wmi_register_driver(&asus_nb_wmi_driver);
}

static void __exit asus_nb_wmi_exit(void)
{
	asus_wmi_unregister_driver(&asus_nb_wmi_driver);
}

module_init(asus_nb_wmi_init);
module_exit(asus_nb_wmi_exit);
