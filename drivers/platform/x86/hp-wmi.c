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
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/input.h>
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

#define PREFIX "HP WMI: "
#define UNIMP "Unimplemented "

enum hp_wmi_radio {
	HPWMI_WIFI = 0,
	HPWMI_BLUETOOTH = 1,
	HPWMI_WWAN = 2,
};

enum hp_wmi_event_ids {
	HPWMI_DOCK_EVENT = 1,
	HPWMI_PARK_HDD = 2,
	HPWMI_SMART_ADAPTER = 3,
	HPWMI_BEZEL_BUTTON = 4,
	HPWMI_WIRELESS = 5,
	HPWMI_CPU_BATTERY_THROTTLE = 6,
	HPWMI_LOCK_SWITCH = 7,
};

static int __devinit hp_wmi_bios_setup(struct platform_device *device);
static int __exit hp_wmi_bios_remove(struct platform_device *device);
static int hp_wmi_resume_handler(struct device *device);

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
	{KE_KEY, 0x20e8, KEY_MEDIA},
	{KE_KEY, 0x2142, KEY_MEDIA},
	{KE_KEY, 0x213b, KEY_INFO},
	{KE_KEY, 0x2169, KEY_DIRECTION},
	{KE_KEY, 0x231b, KEY_HELP},
	{KE_END, 0}
};

static struct input_dev *hp_wmi_input_dev;
static struct platform_device *hp_wmi_platform_dev;

static struct rfkill *wifi_rfkill;
static struct rfkill *bluetooth_rfkill;
static struct rfkill *wwan_rfkill;

static const struct dev_pm_ops hp_wmi_pm_ops = {
	.resume  = hp_wmi_resume_handler,
	.restore  = hp_wmi_resume_handler,
};

static struct platform_driver hp_wmi_driver = {
	.driver = {
		.name = "hp-wmi",
		.owner = THIS_MODULE,
		.pm = &hp_wmi_pm_ops,
	},
	.probe = hp_wmi_bios_setup,
	.remove = hp_wmi_bios_remove,
};

/*
 * hp_wmi_perform_query
 *
 * query:	The commandtype -> What should be queried
 * write:	The command -> 0 read, 1 write, 3 ODM specific
 * buffer:	Buffer used as input and/or output
 * buffersize:	Size of buffer
 *
 * returns zero on success
 *         an HP WMI query specific error code (which is positive)
 *         -EINVAL if the query was not successful at all
 *         -EINVAL if the output buffer size exceeds buffersize
 *
 * Note: The buffersize must at least be the maximum of the input and output
 *       size. E.g. Battery info query (0x7) is defined to have 1 byte input
 *       and 128 byte output. The caller would do:
 *       buffer = kzalloc(128, GFP_KERNEL);
 *       ret = hp_wmi_perform_query(0x7, 0, buffer, 128)
 */
static int hp_wmi_perform_query(int query, int write, u32 *buffer,
				int buffersize)
{
	struct bios_return bios_return;
	acpi_status status;
	union acpi_object *obj;
	struct bios_args args = {
		.signature = 0x55434553,
		.command = write ? 0x2 : 0x1,
		.commandtype = query,
		.datasize = buffersize,
		.data = *buffer,
	};
	struct acpi_buffer input = { sizeof(struct bios_args), &args };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };

	status = wmi_evaluate_method(HPWMI_BIOS_GUID, 0, 0x3, &input, &output);

	obj = output.pointer;

	if (!obj)
		return -EINVAL;
	else if (obj->type != ACPI_TYPE_BUFFER) {
		kfree(obj);
		return -EINVAL;
	}

	bios_return = *((struct bios_return *)obj->buffer.pointer);

	memcpy(buffer, &bios_return.value, sizeof(bios_return.value));
	return 0;
}

static int hp_wmi_display_state(void)
{
	int state = 0;
	int ret = hp_wmi_perform_query(HPWMI_DISPLAY_QUERY, 0, &state,
				       sizeof(state));
	if (ret)
		return -EINVAL;
	return state;
}

static int hp_wmi_hddtemp_state(void)
{
	int state = 0;
	int ret = hp_wmi_perform_query(HPWMI_HDDTEMP_QUERY, 0, &state,
				       sizeof(state));
	if (ret)
		return -EINVAL;
	return state;
}

static int hp_wmi_als_state(void)
{
	int state = 0;
	int ret = hp_wmi_perform_query(HPWMI_ALS_QUERY, 0, &state,
				       sizeof(state));
	if (ret)
		return -EINVAL;
	return state;
}

static int hp_wmi_dock_state(void)
{
	int state = 0;
	int ret = hp_wmi_perform_query(HPWMI_HARDWARE_QUERY, 0, &state,
				       sizeof(state));

	if (ret)
		return -EINVAL;

	return state & 0x1;
}

static int hp_wmi_tablet_state(void)
{
	int state = 0;
	int ret = hp_wmi_perform_query(HPWMI_HARDWARE_QUERY, 0, &state,
				       sizeof(state));
	if (ret)
		return ret;

	return (state & 0x4) ? 1 : 0;
}

static int hp_wmi_set_block(void *data, bool blocked)
{
	enum hp_wmi_radio r = (enum hp_wmi_radio) data;
	int query = BIT(r + 8) | ((!blocked) << r);
	int ret;

	ret = hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, 1,
				   &query, sizeof(query));
	if (ret)
		return -EINVAL;
	return 0;
}

static const struct rfkill_ops hp_wmi_rfkill_ops = {
	.set_block = hp_wmi_set_block,
};

static bool hp_wmi_get_sw_state(enum hp_wmi_radio r)
{
	int wireless = 0;
	int mask;
	hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, 0,
			     &wireless, sizeof(wireless));
	/* TBD: Pass error */

	mask = 0x200 << (r * 8);

	if (wireless & mask)
		return false;
	else
		return true;
}

static bool hp_wmi_get_hw_state(enum hp_wmi_radio r)
{
	int wireless = 0;
	int mask;
	hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, 0,
			     &wireless, sizeof(wireless));
	/* TBD: Pass error */

	mask = 0x800 << (r * 8);

	if (wireless & mask)
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
	int ret = hp_wmi_perform_query(HPWMI_ALS_QUERY, 1, &tmp,
				       sizeof(tmp));
	if (ret)
		return -EINVAL;

	return count;
}

static DEVICE_ATTR(display, S_IRUGO, show_display, NULL);
static DEVICE_ATTR(hddtemp, S_IRUGO, show_hddtemp, NULL);
static DEVICE_ATTR(als, S_IRUGO | S_IWUSR, show_als, set_als);
static DEVICE_ATTR(dock, S_IRUGO, show_dock, NULL);
static DEVICE_ATTR(tablet, S_IRUGO, show_tablet, NULL);

static struct key_entry *hp_wmi_get_entry_by_scancode(unsigned int code)
{
	struct key_entry *key;

	for (key = hp_wmi_keymap; key->type != KE_END; key++)
		if (code == key->code)
			return key;

	return NULL;
}

static struct key_entry *hp_wmi_get_entry_by_keycode(unsigned int keycode)
{
	struct key_entry *key;

	for (key = hp_wmi_keymap; key->type != KE_END; key++)
		if (key->type == KE_KEY && keycode == key->keycode)
			return key;

	return NULL;
}

static int hp_wmi_getkeycode(struct input_dev *dev,
			     unsigned int scancode, unsigned int *keycode)
{
	struct key_entry *key = hp_wmi_get_entry_by_scancode(scancode);

	if (key && key->type == KE_KEY) {
		*keycode = key->keycode;
		return 0;
	}

	return -EINVAL;
}

static int hp_wmi_setkeycode(struct input_dev *dev,
			     unsigned int scancode, unsigned int keycode)
{
	struct key_entry *key;
	unsigned int old_keycode;

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
	u32 event_id, event_data;
	int key_code = 0, ret;
	u32 *location;
	acpi_status status;

	status = wmi_get_event_data(value, &response);
	if (status != AE_OK) {
		printk(KERN_INFO PREFIX "bad event status 0x%x\n", status);
		return;
	}

	obj = (union acpi_object *)response.pointer;

	if (!obj)
		return;
	if (obj->type != ACPI_TYPE_BUFFER) {
		printk(KERN_INFO "hp-wmi: Unknown response received %d\n",
		       obj->type);
		kfree(obj);
		return;
	}

	/*
	 * Depending on ACPI version the concatenation of id and event data
	 * inside _WED function will result in a 8 or 16 byte buffer.
	 */
	location = (u32 *)obj->buffer.pointer;
	if (obj->buffer.length == 8) {
		event_id = *location;
		event_data = *(location + 1);
	} else if (obj->buffer.length == 16) {
		event_id = *location;
		event_data = *(location + 2);
	} else {
		printk(KERN_INFO "hp-wmi: Unknown buffer length %d\n",
		       obj->buffer.length);
		kfree(obj);
		return;
	}
	kfree(obj);

	switch (event_id) {
	case HPWMI_DOCK_EVENT:
		input_report_switch(hp_wmi_input_dev, SW_DOCK,
				    hp_wmi_dock_state());
		input_report_switch(hp_wmi_input_dev, SW_TABLET_MODE,
				    hp_wmi_tablet_state());
		input_sync(hp_wmi_input_dev);
		break;
	case HPWMI_PARK_HDD:
		break;
	case HPWMI_SMART_ADAPTER:
		break;
	case HPWMI_BEZEL_BUTTON:
		ret = hp_wmi_perform_query(HPWMI_HOTKEY_QUERY, 0,
					   &key_code,
					   sizeof(key_code));
		if (ret)
			break;
		key = hp_wmi_get_entry_by_scancode(key_code);
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
		} else
			printk(KERN_INFO PREFIX "Unknown key code - 0x%x\n",
			       key_code);
		break;
	case HPWMI_WIRELESS:
		if (wifi_rfkill)
			rfkill_set_states(wifi_rfkill,
					  hp_wmi_get_sw_state(HPWMI_WIFI),
					  hp_wmi_get_hw_state(HPWMI_WIFI));
		if (bluetooth_rfkill)
			rfkill_set_states(bluetooth_rfkill,
					  hp_wmi_get_sw_state(HPWMI_BLUETOOTH),
					  hp_wmi_get_hw_state(HPWMI_BLUETOOTH));
		if (wwan_rfkill)
			rfkill_set_states(wwan_rfkill,
					  hp_wmi_get_sw_state(HPWMI_WWAN),
					  hp_wmi_get_hw_state(HPWMI_WWAN));
		break;
	case HPWMI_CPU_BATTERY_THROTTLE:
		printk(KERN_INFO PREFIX UNIMP "CPU throttle because of 3 Cell"
		       " battery event detected\n");
		break;
	case HPWMI_LOCK_SWITCH:
		break;
	default:
		printk(KERN_INFO PREFIX "Unknown event_id - %d - 0x%x\n",
		       event_id, event_data);
		break;
	}
}

static int __init hp_wmi_input_setup(void)
{
	struct key_entry *key;
	int err;

	hp_wmi_input_dev = input_allocate_device();
	if (!hp_wmi_input_dev)
		return -ENOMEM;

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

static int __devinit hp_wmi_bios_setup(struct platform_device *device)
{
	int err;
	int wireless = 0;

	err = hp_wmi_perform_query(HPWMI_WIRELESS_QUERY, 0, &wireless,
				   sizeof(wireless));
	if (err)
		return err;

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
					   (void *) HPWMI_WIFI);
		rfkill_init_sw_state(wifi_rfkill,
				     hp_wmi_get_sw_state(HPWMI_WIFI));
		rfkill_set_hw_state(wifi_rfkill,
				    hp_wmi_get_hw_state(HPWMI_WIFI));
		err = rfkill_register(wifi_rfkill);
		if (err)
			goto register_wifi_error;
	}

	if (wireless & 0x2) {
		bluetooth_rfkill = rfkill_alloc("hp-bluetooth", &device->dev,
						RFKILL_TYPE_BLUETOOTH,
						&hp_wmi_rfkill_ops,
						(void *) HPWMI_BLUETOOTH);
		rfkill_init_sw_state(bluetooth_rfkill,
				     hp_wmi_get_sw_state(HPWMI_BLUETOOTH));
		rfkill_set_hw_state(bluetooth_rfkill,
				    hp_wmi_get_hw_state(HPWMI_BLUETOOTH));
		err = rfkill_register(bluetooth_rfkill);
		if (err)
			goto register_bluetooth_error;
	}

	if (wireless & 0x4) {
		wwan_rfkill = rfkill_alloc("hp-wwan", &device->dev,
					   RFKILL_TYPE_WWAN,
					   &hp_wmi_rfkill_ops,
					   (void *) HPWMI_WWAN);
		rfkill_init_sw_state(wwan_rfkill,
				     hp_wmi_get_sw_state(HPWMI_WWAN));
		rfkill_set_hw_state(wwan_rfkill,
				    hp_wmi_get_hw_state(HPWMI_WWAN));
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
		rfkill_destroy(bluetooth_rfkill);
	}
	if (wwan_rfkill) {
		rfkill_unregister(wwan_rfkill);
		rfkill_destroy(wwan_rfkill);
	}

	return 0;
}

static int hp_wmi_resume_handler(struct device *device)
{
	/*
	 * Hardware state may have changed while suspended, so trigger
	 * input events for the current state. As this is a switch,
	 * the input layer will only actually pass it on if the state
	 * changed.
	 */
	if (hp_wmi_input_dev) {
		input_report_switch(hp_wmi_input_dev, SW_DOCK,
				    hp_wmi_dock_state());
		input_report_switch(hp_wmi_input_dev, SW_TABLET_MODE,
				    hp_wmi_tablet_state());
		input_sync(hp_wmi_input_dev);
	}

	if (wifi_rfkill)
		rfkill_set_states(wifi_rfkill,
				  hp_wmi_get_sw_state(HPWMI_WIFI),
				  hp_wmi_get_hw_state(HPWMI_WIFI));
	if (bluetooth_rfkill)
		rfkill_set_states(bluetooth_rfkill,
				  hp_wmi_get_sw_state(HPWMI_BLUETOOTH),
				  hp_wmi_get_hw_state(HPWMI_BLUETOOTH));
	if (wwan_rfkill)
		rfkill_set_states(wwan_rfkill,
				  hp_wmi_get_sw_state(HPWMI_WWAN),
				  hp_wmi_get_hw_state(HPWMI_WWAN));

	return 0;
}

static int __init hp_wmi_init(void)
{
	int err;
	int event_capable = wmi_has_guid(HPWMI_EVENT_GUID);
	int bios_capable = wmi_has_guid(HPWMI_BIOS_GUID);

	if (event_capable) {
		err = wmi_install_notify_handler(HPWMI_EVENT_GUID,
						 hp_wmi_notify, NULL);
		if (ACPI_FAILURE(err))
			return -EINVAL;
		err = hp_wmi_input_setup();
		if (err) {
			wmi_remove_notify_handler(HPWMI_EVENT_GUID);
			return err;
		}
	}

	if (bios_capable) {
		err = platform_driver_register(&hp_wmi_driver);
		if (err)
			goto err_driver_reg;
		hp_wmi_platform_dev = platform_device_alloc("hp-wmi", -1);
		if (!hp_wmi_platform_dev) {
			err = -ENOMEM;
			goto err_device_alloc;
		}
		err = platform_device_add(hp_wmi_platform_dev);
		if (err)
			goto err_device_add;
	}

	if (!bios_capable && !event_capable)
		return -ENODEV;

	return 0;

err_device_add:
	platform_device_put(hp_wmi_platform_dev);
err_device_alloc:
	platform_driver_unregister(&hp_wmi_driver);
err_driver_reg:
	if (wmi_has_guid(HPWMI_EVENT_GUID)) {
		input_unregister_device(hp_wmi_input_dev);
		wmi_remove_notify_handler(HPWMI_EVENT_GUID);
	}

	return err;
}

static void __exit hp_wmi_exit(void)
{
	if (wmi_has_guid(HPWMI_EVENT_GUID)) {
		wmi_remove_notify_handler(HPWMI_EVENT_GUID);
		input_unregister_device(hp_wmi_input_dev);
	}
	if (hp_wmi_platform_dev) {
		platform_device_unregister(hp_wmi_platform_dev);
		platform_driver_unregister(&hp_wmi_driver);
	}
}

module_init(hp_wmi_init);
module_exit(hp_wmi_exit);
