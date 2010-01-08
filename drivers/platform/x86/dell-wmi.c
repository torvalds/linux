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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/input.h>
#include <acpi/acpi_drivers.h>
#include <linux/acpi.h>
#include <linux/string.h>
#include <linux/dmi.h>

MODULE_AUTHOR("Matthew Garrett <mjg@redhat.com>");
MODULE_DESCRIPTION("Dell laptop WMI hotkeys driver");
MODULE_LICENSE("GPL");

#define DELL_EVENT_GUID "9DBB5994-A997-11DA-B012-B622A1EF5492"

static int acpi_video;

MODULE_ALIAS("wmi:"DELL_EVENT_GUID);

struct key_entry {
	char type;		/* See KE_* below */
	u16 code;
	u16 keycode;
};

enum { KE_KEY, KE_SW, KE_IGNORE, KE_END };

/*
 * Certain keys are flagged as KE_IGNORE. All of these are either
 * notifications (rather than requests for change) or are also sent
 * via the keyboard controller so should not be sent again.
 */

static struct key_entry dell_legacy_wmi_keymap[] = {
	{KE_KEY, 0xe045, KEY_PROG1},
	{KE_KEY, 0xe009, KEY_EJECTCD},

	/* These also contain the brightness level at offset 6 */
	{KE_KEY, 0xe006, KEY_BRIGHTNESSUP},
	{KE_KEY, 0xe005, KEY_BRIGHTNESSDOWN},

	/* Battery health status button */
	{KE_KEY, 0xe007, KEY_BATTERY},

	/* This is actually for all radios. Although physically a
	 * switch, the notification does not provide an indication of
	 * state and so it should be reported as a key */
	{KE_KEY, 0xe008, KEY_WLAN},

	/* The next device is at offset 6, the active devices are at
	   offset 8 and the attached devices at offset 10 */
	{KE_KEY, 0xe00b, KEY_SWITCHVIDEOMODE},

	{KE_IGNORE, 0xe00c, KEY_KBDILLUMTOGGLE},

	/* BIOS error detected */
	{KE_IGNORE, 0xe00d, KEY_RESERVED},

	/* Wifi Catcher */
	{KE_KEY, 0xe011, KEY_PROG2},

	/* Ambient light sensor toggle */
	{KE_IGNORE, 0xe013, KEY_RESERVED},

	{KE_IGNORE, 0xe020, KEY_MUTE},
	{KE_IGNORE, 0xe02e, KEY_VOLUMEDOWN},
	{KE_IGNORE, 0xe030, KEY_VOLUMEUP},
	{KE_IGNORE, 0xe033, KEY_KBDILLUMUP},
	{KE_IGNORE, 0xe034, KEY_KBDILLUMDOWN},
	{KE_IGNORE, 0xe03a, KEY_CAPSLOCK},
	{KE_IGNORE, 0xe045, KEY_NUMLOCK},
	{KE_IGNORE, 0xe046, KEY_SCROLLLOCK},
	{KE_END, 0}
};

static bool dell_new_hk_type;

struct dell_new_keymap_entry {
	u16 scancode;
	u16 keycode;
};

struct dell_hotkey_table {
	struct dmi_header header;
	struct dell_new_keymap_entry keymap[];

};

static struct key_entry *dell_new_wmi_keymap;

static u16 bios_to_linux_keycode[256] = {

	KEY_MEDIA,	KEY_NEXTSONG,	KEY_PLAYPAUSE, KEY_PREVIOUSSONG,
	KEY_STOPCD,	KEY_UNKNOWN,	KEY_UNKNOWN,	KEY_UNKNOWN,
	KEY_WWW,	KEY_UNKNOWN,	KEY_VOLUMEDOWN, KEY_MUTE,
	KEY_VOLUMEUP,	KEY_UNKNOWN,	KEY_BATTERY,	KEY_EJECTCD,
	KEY_UNKNOWN,	KEY_SLEEP,	KEY_PROG1, KEY_BRIGHTNESSDOWN,
	KEY_BRIGHTNESSUP,	KEY_UNKNOWN,	KEY_KBDILLUMTOGGLE,
	KEY_UNKNOWN,	KEY_SWITCHVIDEOMODE,	KEY_UNKNOWN, KEY_UNKNOWN,
	KEY_SWITCHVIDEOMODE,	KEY_UNKNOWN,	KEY_UNKNOWN, KEY_PROG2,
	KEY_UNKNOWN, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	KEY_PROG3
};


static struct key_entry *dell_wmi_keymap = dell_legacy_wmi_keymap;

static struct input_dev *dell_wmi_input_dev;

static struct key_entry *dell_wmi_get_entry_by_scancode(int code)
{
	struct key_entry *key;

	for (key = dell_wmi_keymap; key->type != KE_END; key++)
		if (code == key->code)
			return key;

	return NULL;
}

static struct key_entry *dell_wmi_get_entry_by_keycode(int keycode)
{
	struct key_entry *key;

	for (key = dell_wmi_keymap; key->type != KE_END; key++)
		if (key->type == KE_KEY && keycode == key->keycode)
			return key;

	return NULL;
}

static int dell_wmi_getkeycode(struct input_dev *dev, int scancode,
			       int *keycode)
{
	struct key_entry *key = dell_wmi_get_entry_by_scancode(scancode);

	if (key && key->type == KE_KEY) {
		*keycode = key->keycode;
		return 0;
	}

	return -EINVAL;
}

static int dell_wmi_setkeycode(struct input_dev *dev, int scancode, int keycode)
{
	struct key_entry *key;
	int old_keycode;

	if (keycode < 0 || keycode > KEY_MAX)
		return -EINVAL;

	key = dell_wmi_get_entry_by_scancode(scancode);
	if (key && key->type == KE_KEY) {
		old_keycode = key->keycode;
		key->keycode = keycode;
		set_bit(keycode, dev->keybit);
		if (!dell_wmi_get_entry_by_keycode(old_keycode))
			clear_bit(old_keycode, dev->keybit);
		return 0;
	}
	return -EINVAL;
}

static void dell_wmi_notify(u32 value, void *context)
{
	struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
	static struct key_entry *key;
	union acpi_object *obj;
	acpi_status status;

	status = wmi_get_event_data(value, &response);
	if (status != AE_OK) {
		printk(KERN_INFO "dell-wmi: bad event status 0x%x\n", status);
		return;
	}

	obj = (union acpi_object *)response.pointer;

	if (obj && obj->type == ACPI_TYPE_BUFFER) {
		int reported_key;
		u16 *buffer_entry = (u16 *)obj->buffer.pointer;
		if (dell_new_hk_type && (buffer_entry[1] != 0x10)) {
			printk(KERN_INFO "dell-wmi: Received unknown WMI event"
					 " (0x%x)\n", buffer_entry[1]);
			return;
		}

		if (dell_new_hk_type)
			reported_key = (int)buffer_entry[2];
		else
			reported_key = (int)buffer_entry[1] & 0xffff;

		key = dell_wmi_get_entry_by_scancode(reported_key);

		if (!key) {
			printk(KERN_INFO "dell-wmi: Unknown key %x pressed\n",
				reported_key);
		} else if ((key->keycode == KEY_BRIGHTNESSUP ||
			    key->keycode == KEY_BRIGHTNESSDOWN) && acpi_video) {
			/* Don't report brightness notifications that will also
			 * come via ACPI */
			return;
		} else {
			input_report_key(dell_wmi_input_dev, key->keycode, 1);
			input_sync(dell_wmi_input_dev);
			input_report_key(dell_wmi_input_dev, key->keycode, 0);
			input_sync(dell_wmi_input_dev);
		}
	}
	kfree(obj);
}


static void setup_new_hk_map(const struct dmi_header *dm)
{

	int i;
	int hotkey_num = (dm->length-4)/sizeof(struct dell_new_keymap_entry);
	struct dell_hotkey_table *table =
		container_of(dm, struct dell_hotkey_table, header);

	dell_new_wmi_keymap = kzalloc((hotkey_num+1) *
				      sizeof(struct key_entry), GFP_KERNEL);

	for (i = 0; i < hotkey_num; i++) {
		dell_new_wmi_keymap[i].type = KE_KEY;
		dell_new_wmi_keymap[i].code = table->keymap[i].scancode;
		dell_new_wmi_keymap[i].keycode =
			(table->keymap[i].keycode > 255) ? 0 :
			bios_to_linux_keycode[table->keymap[i].keycode];
	}

	dell_new_wmi_keymap[i].type = KE_END;
	dell_new_wmi_keymap[i].code = 0;
	dell_new_wmi_keymap[i].keycode = 0;

	dell_wmi_keymap = dell_new_wmi_keymap;

}


static void find_hk_type(const struct dmi_header *dm, void *dummy)
{

	if ((dm->type == 0xb2) && (dm->length > 6)) {
		dell_new_hk_type = true;
		setup_new_hk_map(dm);
	}

}


static int __init dell_wmi_input_setup(void)
{
	struct key_entry *key;
	int err;

	dell_wmi_input_dev = input_allocate_device();

	if (!dell_wmi_input_dev)
		return -ENOMEM;

	dell_wmi_input_dev->name = "Dell WMI hotkeys";
	dell_wmi_input_dev->phys = "wmi/input0";
	dell_wmi_input_dev->id.bustype = BUS_HOST;
	dell_wmi_input_dev->getkeycode = dell_wmi_getkeycode;
	dell_wmi_input_dev->setkeycode = dell_wmi_setkeycode;

	for (key = dell_wmi_keymap; key->type != KE_END; key++) {
		switch (key->type) {
		case KE_KEY:
			set_bit(EV_KEY, dell_wmi_input_dev->evbit);
			set_bit(key->keycode, dell_wmi_input_dev->keybit);
			break;
		case KE_SW:
			set_bit(EV_SW, dell_wmi_input_dev->evbit);
			set_bit(key->keycode, dell_wmi_input_dev->swbit);
			break;
		}
	}

	err = input_register_device(dell_wmi_input_dev);

	if (err) {
		input_free_device(dell_wmi_input_dev);
		return err;
	}

	return 0;
}

static int __init dell_wmi_init(void)
{
	int err;
	acpi_status status;

	if (!wmi_has_guid(DELL_EVENT_GUID)) {
		printk(KERN_WARNING "dell-wmi: No known WMI GUID found\n");
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
		input_unregister_device(dell_wmi_input_dev);
		printk(KERN_ERR
			"dell-wmi: Unable to register notify handler - %d\n",
			status);
		return -ENODEV;
	}

	return 0;
}

static void __exit dell_wmi_exit(void)
{
	wmi_remove_notify_handler(DELL_EVENT_GUID);
	input_unregister_device(dell_wmi_input_dev);
}

module_init(dell_wmi_init);
module_exit(dell_wmi_exit);
