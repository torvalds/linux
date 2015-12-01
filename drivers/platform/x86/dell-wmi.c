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
#include <acpi/video.h>

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

	/* Radio devices state change */
	{ KE_IGNORE, 0xe008, { KEY_RFKILL } },

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

/* Uninitialized entries here are KEY_RESERVED == 0. */
static const u16 bios_to_linux_keycode[256] __initconst = {
	[0]	= KEY_MEDIA,
	[1]	= KEY_NEXTSONG,
	[2]	= KEY_PLAYPAUSE,
	[3]	= KEY_PREVIOUSSONG,
	[4]	= KEY_STOPCD,
	[5]	= KEY_UNKNOWN,
	[6]	= KEY_UNKNOWN,
	[7]	= KEY_UNKNOWN,
	[8]	= KEY_WWW,
	[9]	= KEY_UNKNOWN,
	[10]	= KEY_VOLUMEDOWN,
	[11]	= KEY_MUTE,
	[12]	= KEY_VOLUMEUP,
	[13]	= KEY_UNKNOWN,
	[14]	= KEY_BATTERY,
	[15]	= KEY_EJECTCD,
	[16]	= KEY_UNKNOWN,
	[17]	= KEY_SLEEP,
	[18]	= KEY_PROG1,
	[19]	= KEY_BRIGHTNESSDOWN,
	[20]	= KEY_BRIGHTNESSUP,
	[21]	= KEY_UNKNOWN,
	[22]	= KEY_KBDILLUMTOGGLE,
	[23]	= KEY_UNKNOWN,
	[24]	= KEY_SWITCHVIDEOMODE,
	[25]	= KEY_UNKNOWN,
	[26]	= KEY_UNKNOWN,
	[27]	= KEY_SWITCHVIDEOMODE,
	[28]	= KEY_UNKNOWN,
	[29]	= KEY_UNKNOWN,
	[30]	= KEY_PROG2,
	[31]	= KEY_UNKNOWN,
	[32]	= KEY_UNKNOWN,
	[33]	= KEY_UNKNOWN,
	[34]	= KEY_UNKNOWN,
	[35]	= KEY_UNKNOWN,
	[36]	= KEY_UNKNOWN,
	[37]	= KEY_UNKNOWN,
	[38]	= KEY_MICMUTE,
	[255]	= KEY_PROG3,
};

static struct input_dev *dell_wmi_input_dev;

static void dell_wmi_process_key(int reported_key)
{
	const struct key_entry *key;

	key = sparse_keymap_entry_from_scancode(dell_wmi_input_dev,
						reported_key);
	if (!key) {
		pr_info("Unknown key with scancode 0x%x pressed\n",
			reported_key);
		return;
	}

	pr_debug("Key %x pressed\n", reported_key);

	/* Don't report brightness notifications that will also come via ACPI */
	if ((key->keycode == KEY_BRIGHTNESSUP ||
	     key->keycode == KEY_BRIGHTNESSDOWN) && acpi_video)
		return;

	sparse_keymap_report_entry(dell_wmi_input_dev, key, 1, true);
}

static void dell_wmi_notify(u32 value, void *context)
{
	struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	acpi_size buffer_size;
	u16 *buffer_entry, *buffer_end;
	int len, i;

	status = wmi_get_event_data(value, &response);
	if (status != AE_OK) {
		pr_warn("bad event status 0x%x\n", status);
		return;
	}

	obj = (union acpi_object *)response.pointer;
	if (!obj) {
		pr_warn("no response\n");
		return;
	}

	if (obj->type != ACPI_TYPE_BUFFER) {
		pr_warn("bad response type %x\n", obj->type);
		kfree(obj);
		return;
	}

	pr_debug("Received WMI event (%*ph)\n",
		obj->buffer.length, obj->buffer.pointer);

	buffer_entry = (u16 *)obj->buffer.pointer;
	buffer_size = obj->buffer.length/2;

	if (!dell_new_hk_type) {
		if (buffer_size >= 3 && buffer_entry[1] == 0x0)
			dell_wmi_process_key(buffer_entry[2]);
		else if (buffer_size >= 2)
			dell_wmi_process_key(buffer_entry[1]);
		else
			pr_info("Received unknown WMI event\n");
		kfree(obj);
		return;
	}

	buffer_end = buffer_entry + buffer_size;

	while (buffer_entry < buffer_end) {

		len = buffer_entry[0];
		if (len == 0)
			break;

		len++;

		if (buffer_entry + len > buffer_end) {
			pr_warn("Invalid length of WMI event\n");
			break;
		}

		pr_debug("Process buffer (%*ph)\n", len*2, buffer_entry);

		switch (buffer_entry[1]) {
		case 0x00:
			for (i = 2; i < len; ++i) {
				switch (buffer_entry[i]) {
				case 0xe043:
					/* NIC Link is Up */
					pr_debug("NIC Link is Up\n");
					break;
				case 0xe044:
					/* NIC Link is Down */
					pr_debug("NIC Link is Down\n");
					break;
				case 0xe045:
					/* Unknown event but defined in DSDT */
				default:
					/* Unknown event */
					pr_info("Unknown WMI event type 0x00: "
						"0x%x\n", (int)buffer_entry[i]);
					break;
				}
			}
			break;
		case 0x10:
			/* Keys pressed */
			for (i = 2; i < len; ++i)
				dell_wmi_process_key(buffer_entry[i]);
			break;
		case 0x11:
			for (i = 2; i < len; ++i) {
				switch (buffer_entry[i]) {
				case 0xfff0:
					/* Battery unplugged */
					pr_debug("Battery unplugged\n");
					break;
				case 0xfff1:
					/* Battery inserted */
					pr_debug("Battery inserted\n");
					break;
				case 0x01e1:
				case 0x02ea:
				case 0x02eb:
				case 0x02ec:
				case 0x02f6:
					/* Keyboard backlight level changed */
					pr_debug("Keyboard backlight level "
						 "changed\n");
					break;
				default:
					/* Unknown event */
					pr_info("Unknown WMI event type 0x11: "
						"0x%x\n", (int)buffer_entry[i]);
					break;
				}
			}
			break;
		default:
			/* Unknown event */
			pr_info("Unknown WMI event type 0x%x\n",
				(int)buffer_entry[1]);
			break;
		}

		buffer_entry += len;

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

		/* Uninitialized entries are 0 aka KEY_RESERVED. */
		u16 keycode = (bios_entry->keycode <
			       ARRAY_SIZE(bios_to_linux_keycode)) ?
			bios_to_linux_keycode[bios_entry->keycode] :
			KEY_RESERVED;

		/*
		 * Log if we find an entry in the DMI table that we don't
		 * understand.  If this happens, we should figure out what
		 * the entry means and add it to bios_to_linux_keycode.
		 */
		if (keycode == KEY_RESERVED) {
			pr_info("firmware scancode 0x%x maps to unrecognized keycode 0x%x\n",
				bios_entry->scancode, bios_entry->keycode);
			continue;
		}

		if (keycode == KEY_KBDILLUMTOGGLE)
			keymap[i].type = KE_IGNORE;
		else
			keymap[i].type = KE_KEY;
		keymap[i].code = bios_entry->scancode;
		keymap[i].keycode = keycode;
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
	acpi_video = acpi_video_get_backlight_type() != acpi_backlight_vendor;

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
