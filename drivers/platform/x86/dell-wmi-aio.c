/*
 *  WMI hotkeys support for Dell All-In-One series
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
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/acpi.h>
#include <linux/string.h>

MODULE_DESCRIPTION("WMI hotkeys driver for Dell All-In-One series");
MODULE_LICENSE("GPL");

#define EVENT_GUID1 "284A0E6B-380E-472A-921F-E52786257FB4"
#define EVENT_GUID2 "02314822-307C-4F66-BF0E-48AEAEB26CC8"

struct dell_wmi_event {
	u16	length;
	/* 0x000: A hot key pressed or an event occurred
	 * 0x00F: A sequence of hot keys are pressed */
	u16	type;
	u16	event[];
};

static const char *dell_wmi_aio_guids[] = {
	EVENT_GUID1,
	EVENT_GUID2,
	NULL
};

MODULE_ALIAS("wmi:"EVENT_GUID1);
MODULE_ALIAS("wmi:"EVENT_GUID2);

static const struct key_entry dell_wmi_aio_keymap[] = {
	{ KE_KEY, 0xc0, { KEY_VOLUMEUP } },
	{ KE_KEY, 0xc1, { KEY_VOLUMEDOWN } },
	{ KE_KEY, 0xe030, { KEY_VOLUMEUP } },
	{ KE_KEY, 0xe02e, { KEY_VOLUMEDOWN } },
	{ KE_KEY, 0xe020, { KEY_MUTE } },
	{ KE_KEY, 0xe027, { KEY_DISPLAYTOGGLE } },
	{ KE_KEY, 0xe006, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 0xe005, { KEY_BRIGHTNESSDOWN } },
	{ KE_KEY, 0xe00b, { KEY_SWITCHVIDEOMODE } },
	{ KE_END, 0 }
};

static struct input_dev *dell_wmi_aio_input_dev;

/*
 * The new WMI event data format will follow the dell_wmi_event structure
 * So, we will check if the buffer matches the format
 */
static bool dell_wmi_aio_event_check(u8 *buffer, int length)
{
	struct dell_wmi_event *event = (struct dell_wmi_event *)buffer;

	if (event == NULL || length < 6)
		return false;

	if ((event->type == 0 || event->type == 0xf) &&
			event->length >= 2)
		return true;

	return false;
}

static void dell_wmi_aio_notify(u32 value, void *context)
{
	struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	struct dell_wmi_event *event;
	acpi_status status;

	status = wmi_get_event_data(value, &response);
	if (status != AE_OK) {
		pr_info("bad event status 0x%x\n", status);
		return;
	}

	obj = (union acpi_object *)response.pointer;
	if (obj) {
		unsigned int scancode = 0;

		switch (obj->type) {
		case ACPI_TYPE_INTEGER:
			/* Most All-In-One correctly return integer scancode */
			scancode = obj->integer.value;
			sparse_keymap_report_event(dell_wmi_aio_input_dev,
				scancode, 1, true);
			break;
		case ACPI_TYPE_BUFFER:
			if (dell_wmi_aio_event_check(obj->buffer.pointer,
						obj->buffer.length)) {
				event = (struct dell_wmi_event *)
					obj->buffer.pointer;
				scancode = event->event[0];
			} else {
				/* Broken machines return the scancode in a
				   buffer */
				if (obj->buffer.pointer &&
						obj->buffer.length > 0)
					scancode = obj->buffer.pointer[0];
			}
			if (scancode)
				sparse_keymap_report_event(
					dell_wmi_aio_input_dev,
					scancode, 1, true);
			break;
		}
	}
	kfree(obj);
}

static int __init dell_wmi_aio_input_setup(void)
{
	int err;

	dell_wmi_aio_input_dev = input_allocate_device();

	if (!dell_wmi_aio_input_dev)
		return -ENOMEM;

	dell_wmi_aio_input_dev->name = "Dell AIO WMI hotkeys";
	dell_wmi_aio_input_dev->phys = "wmi/input0";
	dell_wmi_aio_input_dev->id.bustype = BUS_HOST;

	err = sparse_keymap_setup(dell_wmi_aio_input_dev,
			dell_wmi_aio_keymap, NULL);
	if (err) {
		pr_err("Unable to setup input device keymap\n");
		goto err_free_dev;
	}
	err = input_register_device(dell_wmi_aio_input_dev);
	if (err) {
		pr_info("Unable to register input device\n");
		goto err_free_keymap;
	}
	return 0;

err_free_keymap:
	sparse_keymap_free(dell_wmi_aio_input_dev);
err_free_dev:
	input_free_device(dell_wmi_aio_input_dev);
	return err;
}

static const char *dell_wmi_aio_find(void)
{
	int i;

	for (i = 0; dell_wmi_aio_guids[i] != NULL; i++)
		if (wmi_has_guid(dell_wmi_aio_guids[i]))
			return dell_wmi_aio_guids[i];

	return NULL;
}

static int __init dell_wmi_aio_init(void)
{
	int err;
	const char *guid;

	guid = dell_wmi_aio_find();
	if (!guid) {
		pr_warn("No known WMI GUID found\n");
		return -ENXIO;
	}

	err = dell_wmi_aio_input_setup();
	if (err)
		return err;

	err = wmi_install_notify_handler(guid, dell_wmi_aio_notify, NULL);
	if (err) {
		pr_err("Unable to register notify handler - %d\n", err);
		sparse_keymap_free(dell_wmi_aio_input_dev);
		input_unregister_device(dell_wmi_aio_input_dev);
		return err;
	}

	return 0;
}

static void __exit dell_wmi_aio_exit(void)
{
	const char *guid;

	guid = dell_wmi_aio_find();
	wmi_remove_notify_handler(guid);
	sparse_keymap_free(dell_wmi_aio_input_dev);
	input_unregister_device(dell_wmi_aio_input_dev);
}

module_init(dell_wmi_aio_init);
module_exit(dell_wmi_aio_exit);
