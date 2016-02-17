/*
 * Dell WMI hotkeys
 *
 * Copyright (C) 2008 Red Hat <mjg@redhat.com>
 * Copyright (C) 2014-2015 Pali Rohár <pali.rohar@gmail.com>
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
#include "dell-smbios.h"

MODULE_AUTHOR("Matthew Garrett <mjg@redhat.com>");
MODULE_AUTHOR("Pali Rohár <pali.rohar@gmail.com>");
MODULE_DESCRIPTION("Dell laptop WMI hotkeys driver");
MODULE_LICENSE("GPL");

#define DELL_EVENT_GUID "9DBB5994-A997-11DA-B012-B622A1EF5492"
#define DELL_DESCRIPTOR_GUID "8D9DDCBC-A997-11DA-B012-B622A1EF5492"

static u32 dell_wmi_interface_version;
static bool wmi_requires_smbios_request;

MODULE_ALIAS("wmi:"DELL_EVENT_GUID);
MODULE_ALIAS("wmi:"DELL_DESCRIPTOR_GUID);

static int __init dmi_matched(const struct dmi_system_id *dmi)
{
	wmi_requires_smbios_request = 1;
	return 1;
}

static const struct dmi_system_id dell_wmi_smbios_list[] __initconst = {
	{
		.callback = dmi_matched,
		.ident = "Dell Inspiron M5110",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron M5110"),
		},
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Vostro V131",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Vostro V131"),
		},
	},
	{ }
};

/*
 * Keymap for WMI events of type 0x0000
 *
 * Certain keys are flagged as KE_IGNORE. All of these are either
 * notifications (rather than requests for change) or are also sent
 * via the keyboard controller so should not be sent again.
 */
static const struct key_entry dell_wmi_keymap_type_0000[] __initconst = {
	{ KE_IGNORE, 0x003a, { KEY_CAPSLOCK } },

	/* Key code is followed by brightness level */
	{ KE_KEY,    0xe005, { KEY_BRIGHTNESSDOWN } },
	{ KE_KEY,    0xe006, { KEY_BRIGHTNESSUP } },

	/* Battery health status button */
	{ KE_KEY,    0xe007, { KEY_BATTERY } },

	/* Radio devices state change, key code is followed by other values */
	{ KE_IGNORE, 0xe008, { KEY_RFKILL } },

	{ KE_KEY,    0xe009, { KEY_EJECTCD } },

	/* Key code is followed by: next, active and attached devices */
	{ KE_KEY,    0xe00b, { KEY_SWITCHVIDEOMODE } },

	/* Key code is followed by keyboard illumination level */
	{ KE_IGNORE, 0xe00c, { KEY_KBDILLUMTOGGLE } },

	/* BIOS error detected */
	{ KE_IGNORE, 0xe00d, { KEY_RESERVED } },

	/* Battery was removed or inserted */
	{ KE_IGNORE, 0xe00e, { KEY_RESERVED } },

	/* Wifi Catcher */
	{ KE_KEY,    0xe011, { KEY_WLAN } },

	/* Ambient light sensor toggle */
	{ KE_IGNORE, 0xe013, { KEY_RESERVED } },

	{ KE_IGNORE, 0xe020, { KEY_MUTE } },

	/* Unknown, defined in ACPI DSDT */
	/* { KE_IGNORE, 0xe023, { KEY_RESERVED } }, */

	/* Untested, Dell Instant Launch key on Inspiron 7520 */
	/* { KE_IGNORE, 0xe024, { KEY_RESERVED } }, */

	/* Dell Instant Launch key */
	{ KE_KEY,    0xe025, { KEY_PROG4 } },

	/* Audio panel key */
	{ KE_IGNORE, 0xe026, { KEY_RESERVED } },

	/* LCD Display On/Off Control key */
	{ KE_KEY,    0xe027, { KEY_DISPLAYTOGGLE } },

	/* Untested, Multimedia key on Dell Vostro 3560 */
	/* { KE_IGNORE, 0xe028, { KEY_RESERVED } }, */

	/* Dell Instant Launch key */
	{ KE_KEY,    0xe029, { KEY_PROG4 } },

	/* Untested, Windows Mobility Center button on Inspiron 7520 */
	/* { KE_IGNORE, 0xe02a, { KEY_RESERVED } }, */

	/* Unknown, defined in ACPI DSDT */
	/* { KE_IGNORE, 0xe02b, { KEY_RESERVED } }, */

	/* Untested, Dell Audio With Preset Switch button on Inspiron 7520 */
	/* { KE_IGNORE, 0xe02c, { KEY_RESERVED } }, */

	{ KE_IGNORE, 0xe02e, { KEY_VOLUMEDOWN } },
	{ KE_IGNORE, 0xe030, { KEY_VOLUMEUP } },
	{ KE_IGNORE, 0xe033, { KEY_KBDILLUMUP } },
	{ KE_IGNORE, 0xe034, { KEY_KBDILLUMDOWN } },
	{ KE_IGNORE, 0xe03a, { KEY_CAPSLOCK } },

	/* NIC Link is Up */
	{ KE_IGNORE, 0xe043, { KEY_RESERVED } },

	/* NIC Link is Down */
	{ KE_IGNORE, 0xe044, { KEY_RESERVED } },

	/*
	 * This entry is very suspicious!
	 * Originally Matthew Garrett created this dell-wmi driver specially for
	 * "button with a picture of a battery" which has event code 0xe045.
	 * Later Mario Limonciello from Dell told us that event code 0xe045 is
	 * reported by Num Lock and should be ignored because key is send also
	 * by keyboard controller.
	 * So for now we will ignore this event to prevent potential double
	 * Num Lock key press.
	 */
	{ KE_IGNORE, 0xe045, { KEY_NUMLOCK } },

	/* Scroll lock and also going to tablet mode on portable devices */
	{ KE_IGNORE, 0xe046, { KEY_SCROLLLOCK } },

	/* Untested, going from tablet mode on portable devices */
	/* { KE_IGNORE, 0xe047, { KEY_RESERVED } }, */

	/* Dell Support Center key */
	{ KE_IGNORE, 0xe06e, { KEY_RESERVED } },

	{ KE_IGNORE, 0xe0f7, { KEY_MUTE } },
	{ KE_IGNORE, 0xe0f8, { KEY_VOLUMEDOWN } },
	{ KE_IGNORE, 0xe0f9, { KEY_VOLUMEUP } },
};

struct dell_bios_keymap_entry {
	u16 scancode;
	u16 keycode;
};

struct dell_bios_hotkey_table {
	struct dmi_header header;
	struct dell_bios_keymap_entry keymap[];

};

struct dell_dmi_results {
	int err;
	int keymap_size;
	struct key_entry *keymap;
};

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

/*
 * Keymap for WMI events of type 0x0010
 *
 * These are applied if the 0xB2 DMI hotkey table is present and doesn't
 * override them.
 */
static const struct key_entry dell_wmi_keymap_type_0010[] __initconst = {
	/* Fn-lock */
	{ KE_IGNORE, 0x151, { KEY_RESERVED } },

	/* Change keyboard illumination */
	{ KE_IGNORE, 0x152, { KEY_KBDILLUMTOGGLE } },

	/*
	 * Radio disable (notify only -- there is no model for which the
	 * WMI event is supposed to trigger an action).
	 */
	{ KE_IGNORE, 0x153, { KEY_RFKILL } },

	/* RGB keyboard backlight control */
	{ KE_IGNORE, 0x154, { KEY_RESERVED } },

	/*
	 * Stealth mode toggle. This will "disable all lights and sounds".
	 * The action is performed by the BIOS and EC; the WMI event is just
	 * a notification. On the XPS 13 9350, this is Fn+F7, and there's
	 * a BIOS setting to enable and disable the hotkey.
	 */
	{ KE_IGNORE, 0x155, { KEY_RESERVED } },

	/* Rugged magnetic dock attach/detach events */
	{ KE_IGNORE, 0x156, { KEY_RESERVED } },
	{ KE_IGNORE, 0x157, { KEY_RESERVED } },

	/* Rugged programmable (P1/P2/P3 keys) */
	{ KE_KEY,    0x850, { KEY_PROG1 } },
	{ KE_KEY,    0x851, { KEY_PROG2 } },
	{ KE_KEY,    0x852, { KEY_PROG3 } },

};

/*
 * Keymap for WMI events of type 0x0011
 */
static const struct key_entry dell_wmi_keymap_type_0011[] __initconst = {
	/* Battery unplugged */
	{ KE_IGNORE, 0xfff0, { KEY_RESERVED } },

	/* Battery inserted */
	{ KE_IGNORE, 0xfff1, { KEY_RESERVED } },

	/* Keyboard backlight level changed */
	{ KE_IGNORE, 0x01e1, { KEY_RESERVED } },
	{ KE_IGNORE, 0x02ea, { KEY_RESERVED } },
	{ KE_IGNORE, 0x02eb, { KEY_RESERVED } },
	{ KE_IGNORE, 0x02ec, { KEY_RESERVED } },
	{ KE_IGNORE, 0x02f6, { KEY_RESERVED } },
};

static struct input_dev *dell_wmi_input_dev;

static void dell_wmi_process_key(int type, int code)
{
	const struct key_entry *key;

	key = sparse_keymap_entry_from_scancode(dell_wmi_input_dev,
						(type << 16) | code);
	if (!key) {
		pr_info("Unknown key with type 0x%04x and code 0x%04x pressed\n",
			type, code);
		return;
	}

	pr_debug("Key with type 0x%04x and code 0x%04x pressed\n", type, code);

	/* Don't report brightness notifications that will also come via ACPI */
	if ((key->keycode == KEY_BRIGHTNESSUP ||
	     key->keycode == KEY_BRIGHTNESSDOWN) &&
	    acpi_video_handles_brightness_key_presses())
		return;

	if (type == 0x0000 && code == 0xe025 && !wmi_requires_smbios_request)
		return;

	if (key->keycode == KEY_KBDILLUMTOGGLE)
		dell_laptop_call_notifier(
			DELL_LAPTOP_KBD_BACKLIGHT_BRIGHTNESS_CHANGED, NULL);

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
	buffer_end = buffer_entry + buffer_size;

	/*
	 * BIOS/ACPI on devices with WMI interface version 0 does not clear
	 * buffer before filling it. So next time when BIOS/ACPI send WMI event
	 * which is smaller as previous then it contains garbage in buffer from
	 * previous event.
	 *
	 * BIOS/ACPI on devices with WMI interface version 1 clears buffer and
	 * sometimes send more events in buffer at one call.
	 *
	 * So to prevent reading garbage from buffer we will process only first
	 * one event on devices with WMI interface version 0.
	 */
	if (dell_wmi_interface_version == 0 && buffer_entry < buffer_end)
		if (buffer_end > buffer_entry + buffer_entry[0] + 1)
			buffer_end = buffer_entry + buffer_entry[0] + 1;

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
		case 0x0000: /* One key pressed or event occurred */
			if (len > 2)
				dell_wmi_process_key(0x0000, buffer_entry[2]);
			/* Other entries could contain additional information */
			break;
		case 0x0010: /* Sequence of keys pressed */
		case 0x0011: /* Sequence of events occurred */
			for (i = 2; i < len; ++i)
				dell_wmi_process_key(buffer_entry[1],
						     buffer_entry[i]);
			break;
		default: /* Unknown event */
			pr_info("Unknown WMI event type 0x%x\n",
				(int)buffer_entry[1]);
			break;
		}

		buffer_entry += len;

	}

	kfree(obj);
}

static bool have_scancode(u32 scancode, const struct key_entry *keymap, int len)
{
	int i;

	for (i = 0; i < len; i++)
		if (keymap[i].code == scancode)
			return true;

	return false;
}

static void __init handle_dmi_entry(const struct dmi_header *dm,
				    void *opaque)

{
	struct dell_dmi_results *results = opaque;
	struct dell_bios_hotkey_table *table;
	int hotkey_num, i, pos = 0;
	struct key_entry *keymap;

	if (results->err || results->keymap)
		return;		/* We already found the hotkey table. */

	/* The Dell hotkey table is type 0xB2.  Scan until we find it. */
	if (dm->type != 0xb2)
		return;

	table = container_of(dm, struct dell_bios_hotkey_table, header);

	hotkey_num = (table->header.length -
		      sizeof(struct dell_bios_hotkey_table)) /
				sizeof(struct dell_bios_keymap_entry);
	if (hotkey_num < 1) {
		/*
		 * Historically, dell-wmi would ignore a DMI entry of
		 * fewer than 7 bytes.  Sizes between 4 and 8 bytes are
		 * nonsensical (both the header and all entries are 4
		 * bytes), so we approximate the old behavior by
		 * ignoring tables with fewer than one entry.
		 */
		return;
	}

	keymap = kcalloc(hotkey_num, sizeof(struct key_entry), GFP_KERNEL);
	if (!keymap) {
		results->err = -ENOMEM;
		return;
	}

	for (i = 0; i < hotkey_num; i++) {
		const struct dell_bios_keymap_entry *bios_entry =
					&table->keymap[i];

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
			keymap[pos].type = KE_IGNORE;
		else
			keymap[pos].type = KE_KEY;
		keymap[pos].code = bios_entry->scancode;
		keymap[pos].keycode = keycode;

		pos++;
	}

	results->keymap = keymap;
	results->keymap_size = pos;
}

static int __init dell_wmi_input_setup(void)
{
	struct dell_dmi_results dmi_results = {};
	struct key_entry *keymap;
	int err, i, pos = 0;

	dell_wmi_input_dev = input_allocate_device();
	if (!dell_wmi_input_dev)
		return -ENOMEM;

	dell_wmi_input_dev->name = "Dell WMI hotkeys";
	dell_wmi_input_dev->phys = "wmi/input0";
	dell_wmi_input_dev->id.bustype = BUS_HOST;

	if (dmi_walk(handle_dmi_entry, &dmi_results)) {
		/*
		 * Historically, dell-wmi ignored dmi_walk errors.  A failure
		 * is certainly surprising, but it probably just indicates
		 * a very old laptop.
		 */
		pr_warn("no DMI; using the old-style hotkey interface\n");
	}

	if (dmi_results.err) {
		err = dmi_results.err;
		goto err_free_dev;
	}

	keymap = kcalloc(dmi_results.keymap_size +
			 ARRAY_SIZE(dell_wmi_keymap_type_0000) +
			 ARRAY_SIZE(dell_wmi_keymap_type_0010) +
			 ARRAY_SIZE(dell_wmi_keymap_type_0011) +
			 1,
			 sizeof(struct key_entry), GFP_KERNEL);
	if (!keymap) {
		kfree(dmi_results.keymap);
		err = -ENOMEM;
		goto err_free_dev;
	}

	/* Append table with events of type 0x0010 which comes from DMI */
	for (i = 0; i < dmi_results.keymap_size; i++) {
		keymap[pos] = dmi_results.keymap[i];
		keymap[pos].code |= (0x0010 << 16);
		pos++;
	}

	kfree(dmi_results.keymap);

	/* Append table with extra events of type 0x0010 which are not in DMI */
	for (i = 0; i < ARRAY_SIZE(dell_wmi_keymap_type_0010); i++) {
		const struct key_entry *entry = &dell_wmi_keymap_type_0010[i];

		/*
		 * Check if we've already found this scancode.  This takes
		 * quadratic time, but it doesn't matter unless the list
		 * of extra keys gets very long.
		 */
		if (dmi_results.keymap_size &&
		    have_scancode(entry->code | (0x0010 << 16),
				  keymap, dmi_results.keymap_size)
		   )
			continue;

		keymap[pos] = *entry;
		keymap[pos].code |= (0x0010 << 16);
		pos++;
	}

	/* Append table with events of type 0x0011 */
	for (i = 0; i < ARRAY_SIZE(dell_wmi_keymap_type_0011); i++) {
		keymap[pos] = dell_wmi_keymap_type_0011[i];
		keymap[pos].code |= (0x0011 << 16);
		pos++;
	}

	/*
	 * Now append also table with "legacy" events of type 0x0000. Some of
	 * them are reported also on laptops which have scancodes in DMI.
	 */
	for (i = 0; i < ARRAY_SIZE(dell_wmi_keymap_type_0000); i++) {
		keymap[pos] = dell_wmi_keymap_type_0000[i];
		pos++;
	}

	keymap[pos].type = KE_END;

	err = sparse_keymap_setup(dell_wmi_input_dev, keymap, NULL);
	/*
	 * Sparse keymap library makes a copy of keymap so we don't need the
	 * original one that was allocated.
	 */
	kfree(keymap);
	if (err)
		goto err_free_dev;

	err = input_register_device(dell_wmi_input_dev);
	if (err)
		goto err_free_dev;

	return 0;

 err_free_dev:
	input_free_device(dell_wmi_input_dev);
	return err;
}

/*
 * Descriptor buffer is 128 byte long and contains:
 *
 *       Name             Offset  Length  Value
 * Vendor Signature          0       4    "DELL"
 * Object Signature          4       4    " WMI"
 * WMI Interface Version     8       4    <version>
 * WMI buffer length        12       4    4096
 */
static int __init dell_wmi_check_descriptor_buffer(void)
{
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	u32 *buffer;

	status = wmi_query_block(DELL_DESCRIPTOR_GUID, 0, &out);
	if (ACPI_FAILURE(status)) {
		pr_err("Cannot read Dell descriptor buffer - %d\n", status);
		return status;
	}

	obj = (union acpi_object *)out.pointer;
	if (!obj) {
		pr_err("Dell descriptor buffer is empty\n");
		return -EINVAL;
	}

	if (obj->type != ACPI_TYPE_BUFFER) {
		pr_err("Cannot read Dell descriptor buffer\n");
		kfree(obj);
		return -EINVAL;
	}

	if (obj->buffer.length != 128) {
		pr_err("Dell descriptor buffer has invalid length (%d)\n",
			obj->buffer.length);
		if (obj->buffer.length < 16) {
			kfree(obj);
			return -EINVAL;
		}
	}

	buffer = (u32 *)obj->buffer.pointer;

	if (buffer[0] != 0x4C4C4544 && buffer[1] != 0x494D5720)
		pr_warn("Dell descriptor buffer has invalid signature (%*ph)\n",
			8, buffer);

	if (buffer[2] != 0 && buffer[2] != 1)
		pr_warn("Dell descriptor buffer has unknown version (%d)\n",
			buffer[2]);

	if (buffer[3] != 4096)
		pr_warn("Dell descriptor buffer has invalid buffer length (%d)\n",
			buffer[3]);

	dell_wmi_interface_version = buffer[2];

	pr_info("Detected Dell WMI interface version %u\n",
		dell_wmi_interface_version);

	kfree(obj);
	return 0;
}

/*
 * According to Dell SMBIOS documentation:
 *
 * 17  3  Application Program Registration
 *
 *     cbArg1 Application ID 1 = 0x00010000
 *     cbArg2 Application ID 2
 *            QUICKSET/DCP = 0x51534554 "QSET"
 *            ALS Driver   = 0x416c7353 "AlsS"
 *            Latitude ON  = 0x4c6f6e52 "LonR"
 *     cbArg3 Application version or revision number
 *     cbArg4 0 = Unregister application
 *            1 = Register application
 *     cbRes1 Standard return codes (0, -1, -2)
 */

static int dell_wmi_events_set_enabled(bool enable)
{
	struct calling_interface_buffer *buffer;
	int ret;

	buffer = dell_smbios_get_buffer();
	buffer->input[0] = 0x10000;
	buffer->input[1] = 0x51534554;
	buffer->input[3] = enable;
	dell_smbios_send_request(17, 3);
	ret = buffer->output[0];
	dell_smbios_release_buffer();

	return dell_smbios_error(ret);
}

static int __init dell_wmi_init(void)
{
	int err;
	acpi_status status;

	if (!wmi_has_guid(DELL_EVENT_GUID) ||
	    !wmi_has_guid(DELL_DESCRIPTOR_GUID)) {
		pr_warn("Dell WMI GUID were not found\n");
		return -ENODEV;
	}

	err = dell_wmi_check_descriptor_buffer();
	if (err)
		return err;

	err = dell_wmi_input_setup();
	if (err)
		return err;

	status = wmi_install_notify_handler(DELL_EVENT_GUID,
					 dell_wmi_notify, NULL);
	if (ACPI_FAILURE(status)) {
		input_unregister_device(dell_wmi_input_dev);
		pr_err("Unable to register notify handler - %d\n", status);
		return -ENODEV;
	}

	dmi_check_system(dell_wmi_smbios_list);

	if (wmi_requires_smbios_request) {
		err = dell_wmi_events_set_enabled(true);
		if (err) {
			pr_err("Failed to enable WMI events\n");
			wmi_remove_notify_handler(DELL_EVENT_GUID);
			input_unregister_device(dell_wmi_input_dev);
			return err;
		}
	}

	return 0;
}
module_init(dell_wmi_init);

static void __exit dell_wmi_exit(void)
{
	if (wmi_requires_smbios_request)
		dell_wmi_events_set_enabled(false);
	wmi_remove_notify_handler(DELL_EVENT_GUID);
	input_unregister_device(dell_wmi_input_dev);
}
module_exit(dell_wmi_exit);
