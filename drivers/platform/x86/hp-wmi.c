/*
 * HP WMI hotkeys
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
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/rfkill.h>
#include <linux/string.h>

MODULE_AUTHOR("Matthew Garrett <mjg59@srcf.ucam.org>");
MODULE_DESCRIPTION("HP laptop WMI hotkeys driver");
MODULE_LICENSE("GPL");

MODULE_ALIAS("wmi:95F24279-4D7B-4334-9387-ACCDC67EF61C");
MODULE_ALIAS("wmi:5FB7F034-2C63-45e9-BE91-3D44E2C707E4");

#define HPWMI_EVENT_GUID "95F24279-4D7B-4334-9387-ACCDC67EF61C"
#define HPWMI_BIOS_GUID "5FB7F034-2C63-45e9-BE91-3D44E2C707E4"

#define HPWMI_DISPLAY_QUERY 0x1
#define HPWMI_HDDTEMP_QUERY 0x2
#define HPWMI_ALS_QUERY 0x3
#define HPWMI_HARDWARE_QUERY 0x4
#define HPWMI_WIRELESS_QUERY 0x5
#define HPWMI_HOTKEY_QUERY 0xc

static int __init hp_wmi_bios_setup(struct platform_device *device);
static int __exit hp_wmi_bios_remove(struct platform_device *device);
static int hp_wmi_resume_handler(struct platform_device *device);

struct bios_args {
	u32 signature;
	u32 command;
	u32 commandtype;
	u32 datasize;
	u32 data;
};

struct bios_return {
	u32 sigpass;
	u32 return_code;
	u32 value;
};

struct key_entry {
	char type;		/* See KE_* below */
	u16 code;
	u16 keycode;
};

enum { KE_KEY, KE_END };

static struct key_entry hp_wmi_keymap[] = {
	{KE_KEY, 0x02, KEY_BRIGHTNESSUP},
	{KE_KEY, 0x03, KEY_BRIGHTNESSDOWN},
	{KE_KEY, 0x20e6, KEY_PROG1},
	{KE_KEY, 0x2142, KEY_MEDIA},
	{KE_KEY, 0x213b, KEY_INFO},
	{KE_KEY, 0x231b, KEY_HELP},
	{KE_END, 0}
};

static struct input_dev *hp_wmi_input_dev;
static struct platform_device *hp_wmi_platform_dev;

static struct rfkill *wifi_rfkill;
static struct rfkill *bluetooth_rfkill;
static struct rfkill *wwan_rfkill;

static struct platform_driver hp_wmi_driver = {
	.driver = {
		   .name = "hp-wmi",
		   .owner = THIS_MODULE,
	},
	.probe = hp_wmi_bios_setup,
	.remove = hp_wmi_bios_remove,
	.resume = hp_wmi_resume_handler,
};

static int hp_wmi_perform_query(int query, int write, int value)
{
	struct bios_return bios_return;
	acpi_status status;
	union acpi_object *obj;
	struct bios_args args = {
		.signature = 0x55434553,
		.command = write ? 0x2 : 0x1,
		.commandtype = query,
		.datasize = write ? 0x4 : 0,
		.data = value,
	};
	struct acpi_buffer input = { sizeof(struct bios_args), &args };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };

	status = wmi_evaluate_method(HPWMI_BIOS_GUID, 0, 0x3, &input, &output);

	obj = output.pointer;

	if (!obj || obj->type != ACPI_TYPE_BUFFER)
		return -EINVAL;

	bios_return = *((struct bios_return *)obj->buffer.pointer);
	if (bios_return.return_code > 0)
		return bios_return.return_code * -1;
	else
		return bios_return.value;
}

static int hp_wmi_display_state(void)
{
	return hp_wmi_perform_query(HPWMI_DISPLAY_QUERY, 0, 0);
}

static int hp_wmi_hddtemp_state(void)
{
	return hp_wmi_perform_query(HPWMI_HDDTEMP_QUERY, 0, 0);
}

static int hp_wmi_als_state(void)
{
	return hp_wmi_perform_query(HPWMI_ALS_QUERY, 0, 0);
}

static int hp_wmi_dock_state(void)
{
	int ret = hp_wmi_perform_query(HPWMI_HARDWARE_QUERY, 0, 0);

	if (ret < 0)
		return ret;

	return ret & 0x1;
}

static int hp_wmi_tablet_state(void)
{
	int ret = hp_wmi_perform_query(HPWMI_HARDWARE_QUERY, 0, 0);

	if (ret < 0)
		return ret;

	return (ret & 0x4) ? 1 : 0;
}

static int hp_wmi_set_block(void *data, bool blocked)
{
	unsigned long b = (unsigned long) data;
	int query = BIT(b + 8) | ((!blocked) << b);

	return hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, 1, query);
}

static const struct rfkill_ops hp_wmi_rfkill_ops = {
	.set_block = hp_wmi_set_block,
};

static bool hp_wmi_wifi_state(void)
{
	int wireless = hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, 0, 0);

	if (wireless & 0x100)
		return false;
	else
		return true;
}

static bool hp_wmi_bluetooth_state(void)
{
	int wireless = hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, 0, 0);

	if (wireless & 0x10000)
		return false;
	else
		return true;
}

static bool hp_wmi_wwan_state(void)
{
	int wireless = hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, 0, 0);

	if (wireless & 0x1000000)
		return false;
	else
		return true;
}

static ssize_t show_display(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int value = hp_wmi_display_state();
	if (value < 0)
		return -EINVAL;
	return sprintf(buf, "%d\n", value);
}

static ssize_t show_hddtemp(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int value = hp_wmi_hddtemp_state();
	if (value < 0)
		return -EINVAL;
	return sprintf(buf, "%d\n", value);
}

static ssize_t show_als(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int value = hp_wmi_als_state();
	if (value < 0)
		return -EINVAL;
	return sprintf(buf, "%d\n", value);
}

static ssize_t show_dock(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	int value = hp_wmi_dock_state();
	if (value < 0)
		return -EINVAL;
	return sprintf(buf, "%d\n", value);
}

static ssize_t show_tablet(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	int value = hp_wmi_tablet_state();
	if (value < 0)
		return -EINVAL;
	return sprintf(buf, "%d\n", value);
}

static ssize_t set_als(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	u32 tmp = simple_strtoul(buf, NULL, 10);
	hp_wmi_perform_query(HPWMI_ALS_QUERY, 1, tmp);
	return count;
}

static DEVICE_ATTR(display, S_IRUGO, show_display, NULL);
static DEVICE_ATTR(hddtemp, S_IRUGO, show_hddtemp, NULL);
static DEVICE_ATTR(als, S_IRUGO | S_IWUSR, show_als, set_als);
static DEVICE_ATTR(dock, S_IRUGO, show_dock, NULL);
static DEVICE_ATTR(tablet, S_IRUGO, show_tablet, NULL);

static struct key_entry *hp_wmi_get_entry_by_scancode(int code)
{
	struct key_entry *key;

	for (key = hp_wmi_keymap; key->type != KE_END; key++)
		if (code == key->code)
			return key;

	return NULL;
}

static struct key_entry *hp_wmi_get_entry_by_keycode(int keycode)
{
	struct key_entry *key;

	for (key = hp_wmi_keymap; key->type != KE_END; key++)
		if (key->type == KE_KEY && keycode == key->keycode)
			return key;

	return NULL;
}

static int hp_wmi_getkeycode(struct input_dev *dev, int scancode, int *keycode)
{
	struct key_entry *key = hp_wmi_get_entry_by_scancode(scancode);

	if (key && key->type == KE_KEY) {
		*keycode = key->keycode;
		return 0;
	}

	return -EINVAL;
}

static int hp_wmi_setkeycode(struct input_dev *dev, int scancode, int keycode)
{
	struct key_entry *key;
	int old_keycode;

	if (keycode < 0 || keycode > KEY_MAX)
		return -EINVAL;

	key = hp_wmi_get_entry_by_scancode(scancode);
	if (key && key->type == KE_KEY) {
		old_keycode = key->keycode;
		key->keycode = keycode;
		set_bit(keycode, dev->keybit);
		if (!hp_wmi_get_entry_by_keycode(old_keycode))
			clear_bit(old_keycode, dev->keybit);
		return 0;
	}

	return -EINVAL;
}

static void hp_wmi_notify(u32 value, void *context)
{
	struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
	static struct key_entry *key;
	union acpi_object *obj;

	wmi_get_event_data(value, &response);

	obj = (union acpi_object *)response.pointer;

	if (obj && obj->type == ACPI_TYPE_BUFFER && obj->buffer.length == 8) {
		int eventcode = *((u8 *) obj->buffer.pointer);
		if (eventcode == 0x4)
			eventcode = hp_wmi_perform_query(HPWMI_HOTKEY_QUERY, 0,
							 0);
		key = hp_wmi_get_entry_by_scancode(eventcode);
		if (key) {
			switch (key->type) {
			case KE_KEY:
				input_report_key(hp_wmi_input_dev,
						 key->keycode, 1);
				input_sync(hp_wmi_input_dev);
				input_report_key(hp_wmi_input_dev,
						 key->keycode, 0);
				input_sync(hp_wmi_input_dev);
				break;
			}
		} else if (eventcode == 0x1) {
			input_report_switch(hp_wmi_input_dev, SW_DOCK,
					    hp_wmi_dock_state());
			input_report_switch(hp_wmi_input_dev, SW_TABLET_MODE,
					    hp_wmi_tablet_state());
			input_sync(hp_wmi_input_dev);
		} else if (eventcode == 0x5) {
			if (wifi_rfkill)
				rfkill_set_sw_state(wifi_rfkill,
						    hp_wmi_wifi_state());
			if (bluetooth_rfkill)
				rfkill_set_sw_state(bluetooth_rfkill,
						    hp_wmi_bluetooth_state());
			if (wwan_rfkill)
				rfkill_set_sw_state(wwan_rfkill,
						    hp_wmi_wwan_state());
		} else
			printk(KERN_INFO "HP WMI: Unknown key pressed - %x\n",
			       eventcode);
	} else
		printk(KERN_INFO "HP WMI: Unknown response received\n");
}

static int __init hp_wmi_input_setup(void)
{
	struct key_entry *key;
	int err;

	hp_wmi_input_dev = input_allocate_device();

	hp_wmi_input_dev->name = "HP WMI hotkeys";
	hp_wmi_input_dev->phys = "wmi/input0";
	hp_wmi_input_dev->id.bustype = BUS_HOST;
	hp_wmi_input_dev->getkeycode = hp_wmi_getkeycode;
	hp_wmi_input_dev->setkeycode = hp_wmi_setkeycode;

	for (key = hp_wmi_keymap; key->type != KE_END; key++) {
		switch (key->type) {
		case KE_KEY:
			set_bit(EV_KEY, hp_wmi_input_dev->evbit);
			set_bit(key->keycode, hp_wmi_input_dev->keybit);
			break;
		}
	}

	set_bit(EV_SW, hp_wmi_input_dev->evbit);
	set_bit(SW_DOCK, hp_wmi_input_dev->swbit);
	set_bit(SW_TABLET_MODE, hp_wmi_input_dev->swbit);

	/* Set initial hardware state */
	input_report_switch(hp_wmi_input_dev, SW_DOCK, hp_wmi_dock_state());
	input_report_switch(hp_wmi_input_dev, SW_TABLET_MODE,
			    hp_wmi_tablet_state());
	input_sync(hp_wmi_input_dev);

	err = input_register_device(hp_wmi_input_dev);

	if (err) {
		input_free_device(hp_wmi_input_dev);
		return err;
	}

	return 0;
}

static void cleanup_sysfs(struct platform_device *device)
{
	device_remove_file(&device->dev, &dev_attr_display);
	device_remove_file(&device->dev, &dev_attr_hddtemp);
	device_remove_file(&device->dev, &dev_attr_als);
	device_remove_file(&device->dev, &dev_attr_dock);
	device_remove_file(&device->dev, &dev_attr_tablet);
}

static int __init hp_wmi_bios_setup(struct platform_device *device)
{
	int err;
	int wireless = hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, 0, 0);

	err = device_create_file(&device->dev, &dev_attr_display);
	if (err)
		goto add_sysfs_error;
	err = device_create_file(&device->dev, &dev_attr_hddtemp);
	if (err)
		goto add_sysfs_error;
	err = device_create_file(&device->dev, &dev_attr_als);
	if (err)
		goto add_sysfs_error;
	err = device_create_file(&device->dev, &dev_attr_dock);
	if (err)
		goto add_sysfs_error;
	err = device_create_file(&device->dev, &dev_attr_tablet);
	if (err)
		goto add_sysfs_error;

	if (wireless & 0x1) {
		wifi_rfkill = rfkill_alloc("hp-wifi", &device->dev,
					   RFKILL_TYPE_WLAN,
					   &hp_wmi_rfkill_ops,
					   (void *) 0);
		err = rfkill_register(wifi_rfkill);
		if (err)
			goto register_wifi_error;
	}

	if (wireless & 0x2) {
		bluetooth_rfkill = rfkill_alloc("hp-bluetooth", &device->dev,
						RFKILL_TYPE_BLUETOOTH,
						&hp_wmi_rfkill_ops,
						(void *) 1);
		err = rfkill_register(bluetooth_rfkill);
		if (err)
			goto register_bluetooth_error;
	}

	if (wireless & 0x4) {
		wwan_rfkill = rfkill_alloc("hp-wwan", &device->dev,
					   RFKILL_TYPE_WWAN,
					   &hp_wmi_rfkill_ops,
					   (void *) 2);
		err = rfkill_register(wwan_rfkill);
		if (err)
			goto register_wwan_err;
	}

	return 0;
register_wwan_err:
	rfkill_destroy(wwan_rfkill);
	if (bluetooth_rfkill)
		rfkill_unregister(bluetooth_rfkill);
register_bluetooth_error:
	rfkill_destroy(bluetooth_rfkill);
	if (wifi_rfkill)
		rfkill_unregister(wifi_rfkill);
register_wifi_error:
	rfkill_destroy(wifi_rfkill);
add_sysfs_error:
	cleanup_sysfs(device);
	return err;
}

static int __exit hp_wmi_bios_remove(struct platform_device *device)
{
	cleanup_sysfs(device);

	if (wifi_rfkill) {
		rfkill_unregister(wifi_rfkill);
		rfkill_destroy(wifi_rfkill);
	}
	if (bluetooth_rfkill) {
		rfkill_unregister(bluetooth_rfkill);
		rfkill_destroy(wifi_rfkill);
	}
	if (wwan_rfkill) {
		rfkill_unregister(wwan_rfkill);
		rfkill_destroy(wwan_rfkill);
	}

	return 0;
}

static int hp_wmi_resume_handler(struct platform_device *device)
{
	/*
	 * Hardware state may have changed while suspended, so trigger
	 * input events for the current state. As this is a switch,
	 * the input layer will only actually pass it on if the state
	 * changed.
	 */

	input_report_switch(hp_wmi_input_dev, SW_DOCK, hp_wmi_dock_state());
	input_report_switch(hp_wmi_input_dev, SW_TABLET_MODE,
			    hp_wmi_tablet_state());
	input_sync(hp_wmi_input_dev);

	return 0;
}

static int __init hp_wmi_init(void)
{
	int err;

	if (wmi_has_guid(HPWMI_EVENT_GUID)) {
		err = wmi_install_notify_handler(HPWMI_EVENT_GUID,
						 hp_wmi_notify, NULL);
		if (!err)
			hp_wmi_input_setup();
	}

	if (wmi_has_guid(HPWMI_BIOS_GUID)) {
		err = platform_driver_register(&hp_wmi_driver);
		if (err)
			return 0;
		hp_wmi_platform_dev = platform_device_alloc("hp-wmi", -1);
		if (!hp_wmi_platform_dev) {
			platform_driver_unregister(&hp_wmi_driver);
			return 0;
		}
		platform_device_add(hp_wmi_platform_dev);
	}

	return 0;
}

static void __exit hp_wmi_exit(void)
{
	if (wmi_has_guid(HPWMI_EVENT_GUID)) {
		wmi_remove_notify_handler(HPWMI_EVENT_GUID);
		input_unregister_device(hp_wmi_input_dev);
	}
	if (hp_wmi_platform_dev) {
		platform_device_del(hp_wmi_platform_dev);
		platform_driver_unregister(&hp_wmi_driver);
	}
}

module_init(hp_wmi_init);
module_exit(hp_wmi_exit);
