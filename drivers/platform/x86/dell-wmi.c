/*
 * Dell WMI hotkeys
 *
 * Copyright (C) 2008 Red Hat <mjg@redhat.com>
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
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/acpi.h>
#include <linux/string.h>
#include <linux/dmi.h>

MODULE_AUTHOR("Matthew Garrett <mjg@redhat.com>");
MODULE_DESCRIPTION("Dell laptop WMI hotkeys driver");
MODULE_LICENSE("GPL");

#define DELL_EVENT_GUID "9DBB5994-A997-11DA-B012-B622A1EF5492"

static int acpi_video;

MODULE_ALIAS("wmi:"DELL_EVENT_GUID);

/*
 * Certain keys are flagged as KE_IGNORE. All of these are either
 * notifications (rather than requests for change) or are also sent
 * via the keyboard controller so should not be sent again.
 */

static const struct key_entry dell_wmi_legacy_keymap[] __initconst = {
	{ KE_IGNORE, 0x003a, { KEY_CAPSLOCK } },

	{ KE_KEY, 0xe045, { KEY_PROG1 } },
	{ KE_KEY, 0xe009, { KEY_EJECTCD } },

	/* These also contain the brightness level at offset 6 */
	{ KE_KEY, 0xe006, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 0xe005, { KEY_BRIGHTNESSDOWN } },

	/* Battery health status button */
	{ KE_KEY, 0xe007, { KEY_BATTERY } },

	/* This is actually for all radios. Although physically a
	 * switch, the notification does not provide an indication of
	 * state and so it should be reported as a key */
	{ KE_KEY, 0xe008, { KEY_WLAN } },

	/* The next device is at offset 6, the active devices are at
	   offset 8 and the attached devices at offset 10 */
	{ KE_KEY, 0xe00b, { KEY_SWITCHVIDEOMODE } },

	{ KE_IGNORE, 0xe00c, { KEY_KBDILLUMTOGGLE } },

	/* BIOS error detected */
	{ KE_IGNORE, 0xe00d, { KEY_RESERVED } },

	/* Wifi Catcher */
	{ KE_KEY, 0xe011, {KEY_PROG2 } },

	/* Ambient light sensor toggle */
	{ KE_IGNORE, 0xe013, { KEY_RESERVED } },

	{ KE_IGNORE, 0xe020, { KEY_MUTE } },

	/* Shortcut and audio panel keys */
	{ KE_IGNORE, 0xe025, { KEY_RESERVED } },
	{ KE_IGNORE, 0xe026, { KEY_RESERVED } },

	{ KE_IGNORE, 0xe02e, { KEY_VOLUMEDOWN } },
	{ KE_IGNORE, 0xe030, { KEY_VOLUMEUP } },
	{ KE_IGNORE, 0xe033, { KEY_KBDILLUMUP } },
	{ KE_IGNORE, 0xe034, { KEY_KBDILLUMDOWN } },
	{ KE_IGNORE, 0xe03a, { KEY_CAPSLOCK } },
	{ KE_IGNORE, 0xe045, { KEY_NUMLOCK } },
	{ KE_IGNORE, 0xe046, { KEY_SCROLLLOCK } },
	{ KE_IGNORE, 0xe0f7, { KEY_MUTE } },
	{ KE_IGNORE, 0xe0f8, { KEY_VOLUMEDOWN } },
	{ KE_IGNORE, 0xe0f9, { KEY_VOLUMEUP } },
	{ KE_END, 0 }
};

static bool dell_new_hk_type;

struct dell_bios_keymap_entry {
	u16 scancode;
	u16 keycode;
};

struct dell_bios_hotkey_table {
	struct dmi_header header;
	struct dell_bios_keymap_entry keymap[];

};

static const struct dell_bios_hotkey_table *dell_bios_hotkey_table;

static const u16 bios_to_linux_keycode[256] __initconst = {

	KEY_MEDIA,	KEY_NEXTSONG,	KEY_PLAYPAUSE, KEY_PREVIOUSSONG,
	KEY_STOPCD,	KEY_UNKNOWN,	KEY_UNKNOWN,	KEY_UNKNOWN,
	KEY_WWW,	KEY_UNKNOWN,	KEY_VOLUMEDOWN, KEY_MUTE,
	KEY_VOLUMEUP,	KEY_UNKNOWN,	KEY_BATTERY,	KEY_EJECTCD,
	KEY_UNKNOWN,	KEY_SLEEP,	KEY_PROG1, KEY_BRIGHTNESSDOWN,
	KEY_BRIGHTNESSUP,	KEY_UNKNOWN,	KEY_KBDILLUMTOGGLE,
	KEY_UNKNOWN,	KEY_SWITCHVIDEOMODE,	KEY_UNKNOWN, KEY_UNKNOWN,
	KEY_SWITCHVIDEOMODE,	KEY_UNKNOWN,	KEY_UNKNOWN, KEY_PROG2,
	KEY_UNKNOWN,	KEY_UNKNOWN,	KEY_UNKNOWN,	KEY_UNKNOWN,
	KEY_UNKNOWN,	KEY_UNKNOWN,	KEY_UNKNOWN,	KEY_MICMUTE,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, KEY_PROG3
};

static struct input_dev *dell_wmi_input_dev;

static void dell_wmi_notify(u32 value, void *context)
{
	struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;

	status = wmi_get_event_data(value, &response);
	if (status != AE_OK) {
		pr_info("bad event status 0x%x\n", status);
		return;
	}

	obj = (union acpi_object *)response.pointer;

	if (obj && obj->type == ACPI_TYPE_BUFFER) {
		const struct key_entry *key;
		int reported_key;
		u16 *buffer_entry = (u16 *)obj->buffer.pointer;
		int buffer_size = obj->buffer.length/2;

		if (buffer_size >= 2 && dell_new_hk_type && buffer_entry[1] != 0x10) {
			pr_info("Received unknown WMI event (0x%x)\n",
				buffer_entry[1]);
			kfree(obj);
			return;
		}

		if (buffer_size >= 3 && (dell_new_hk_type || buffer_entry[1] == 0x0))
			reported_key = (int)buffer_entry[2];
		else if (buffer_size >= 2)
			reported_key = (int)buffer_entry[1] & 0xffff;
		else {
			pr_info("Received unknown WMI event\n");
			kfree(obj);
			return;
		}

		key = sparse_keymap_entry_from_scancode(dell_wmi_input_dev,
							reported_key);
		if (!key) {
			pr_info("Unknown key %x pressed\n", reported_key);
		} else if ((key->keycode == KEY_BRIGHTNESSUP ||
			    key->keycode == KEY_BRIGHTNESSDOWN) && acpi_video) {
			/* Don't report brightness notifications that will also
			 * come via ACPI */
			;
		} else {
			sparse_keymap_report_entry(dell_wmi_input_dev, key,
						   1, true);
		}
	}
	kfree(obj);
}

static const struct key_entry * __init dell_wmi_prepare_new_keymap(void)
{
	int hotkey_num = (dell_bios_hotkey_table->header.length - 4) /
				sizeof(struct dell_bios_keymap_entry);
	struct key_entry *keymap;
	int i;

	keymap = kcalloc(hotkey_num + 1, sizeof(struct key_entry), GFP_KERNEL);
	if (!keymap)
		return NULL;

	for (i = 0; i < hotkey_num; i++) {
		const struct dell_bios_keymap_entry *bios_entry =
					&dell_bios_hotkey_table->keymap[i];
		keymap[i].type = KE_KEY;
		keymap[i].code = bios_entry->scancode;
		keymap[i].keycode = bios_entry->keycode < 256 ?
				    bios_to_linux_keycode[bios_entry->keycode] :
				    KEY_RESERVED;
	}

	keymap[hotkey_num].type = KE_END;

	return keymap;
}

static int __init dell_wmi_input_setup(void)
{
	int err;

	dell_wmi_input_dev = input_allocate_device();
	if (!dell_wmi_input_dev)
		return -ENOMEM;

	dell_wmi_input_dev->name = "Dell WMI hotkeys";
	dell_wmi_input_dev->phys = "wmi/input0";
	dell_wmi_input_dev->id.bustype = BUS_HOST;

	if (dell_new_hk_type) {
		const struct key_entry *keymap = dell_wmi_prepare_new_keymap();
		if (!keymap) {
			err = -ENOMEM;
			goto err_free_dev;
		}

		err = sparse_keymap_setup(dell_wmi_input_dev, keymap, NULL);

		/*
		 * Sparse keymap library makes a copy of keymap so we
		 * don't need the original one that was allocated.
		 */
		kfree(keymap);
	} else {
		err = sparse_keymap_setup(dell_wmi_input_dev,
					  dell_wmi_legacy_keymap, NULL);
	}
	if (err)
		goto err_free_dev;

	err = input_register_device(dell_wmi_input_dev);
	if (err)
		goto err_free_keymap;

	return 0;

 err_free_keymap:
	sparse_keymap_free(dell_wmi_input_dev);
 err_free_dev:
	input_free_device(dell_wmi_input_dev);
	return err;
}

static void dell_wmi_input_destroy(void)
{
	sparse_keymap_free(dell_wmi_input_dev);
	input_unregister_device(dell_wmi_input_dev);
}

static void __init find_hk_type(const struct dmi_header *dm, void *dummy)
{
	if (dm->type == 0xb2 && dm->length > 6) {
		dell_new_hk_type = true;
		dell_bios_hotkey_table =
			container_of(dm, struct dell_bios_hotkey_table, header);
	}
}

static int __init dell_wmi_init(void)
{
	int err;
	acpi_status status;

	if (!wmi_has_guid(DELL_EVENT_GUID)) {
		pr_warn("No known WMI GUID found\n");
		return -ENODEV;
	}

	dmi_walk(find_hk_type, NULL);
	acpi_video = acpi_video_backlight_support();

	err = dell_wmi_input_setup();
	if (err)
		return err;

	status = wmi_install_notify_handler(DELL_EVENT_GUID,
					 dell_wmi_notify, NULL);
	if (ACPI_FAILURE(status)) {
		dell_wmi_input_destroy();
		pr_err("Unable to register notify handler - %d\n", status);
		return -ENODEV;
	}

	return 0;
}
module_init(dell_wmi_init);

static void __exit dell_wmi_exit(void)
{
	wmi_remove_notify_handler(DELL_EVENT_GUID);
	dell_wmi_input_destroy();
}
module_exit(dell_wmi_exit);
