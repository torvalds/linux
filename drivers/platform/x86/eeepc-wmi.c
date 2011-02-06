/*
 * Eee PC WMI hotkey driver
 *
 * Copyright(C) 2010 Intel Corporation.
 * Copyright(C) 2010 Corentin Chary <corentin.chary@gmail.com>
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
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/leds.h>
#include <linux/rfkill.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>
#include <linux/dmi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define	EEEPC_WMI_FILE	"eeepc-wmi"

MODULE_AUTHOR("Yong Wang <yong.y.wang@intel.com>");
MODULE_DESCRIPTION("Eee PC WMI Hotkey Driver");
MODULE_LICENSE("GPL");

#define EEEPC_ACPI_HID		"ASUS010" /* old _HID used in eeepc-laptop */

#define EEEPC_WMI_EVENT_GUID	"ABBC0F72-8EA1-11D1-00A0-C90629100000"
#define EEEPC_WMI_MGMT_GUID	"97845ED0-4E6D-11DE-8A39-0800200C9A66"

MODULE_ALIAS("wmi:"EEEPC_WMI_EVENT_GUID);
MODULE_ALIAS("wmi:"EEEPC_WMI_MGMT_GUID);

#define NOTIFY_BRNUP_MIN		0x11
#define NOTIFY_BRNUP_MAX		0x1f
#define NOTIFY_BRNDOWN_MIN		0x20
#define NOTIFY_BRNDOWN_MAX		0x2e

/* WMI Methods */
#define EEEPC_WMI_METHODID_DSTS		0x53544344
#define EEEPC_WMI_METHODID_DEVS		0x53564544
#define EEEPC_WMI_METHODID_CFVS		0x53564643

/* Wireless */
#define EEEPC_WMI_DEVID_WLAN		0x00010011
#define EEEPC_WMI_DEVID_BLUETOOTH	0x00010013
#define EEEPC_WMI_DEVID_WIMAX		0x00010017
#define EEEPC_WMI_DEVID_WWAN3G		0x00010019

/* Backlight and Brightness */
#define EEEPC_WMI_DEVID_BACKLIGHT	0x00050011
#define EEEPC_WMI_DEVID_BRIGHTNESS	0x00050012

/* Misc */
#define EEEPC_WMI_DEVID_CAMERA		0x00060013

/* Storage */
#define EEEPC_WMI_DEVID_CARDREADER	0x00080013

/* Input */
#define EEEPC_WMI_DEVID_TOUCHPAD	0x00100011
#define EEEPC_WMI_DEVID_TOUCHPAD_LED	0x00100012

/* DSTS masks */
#define EEEPC_WMI_DSTS_STATUS_BIT	0x00000001
#define EEEPC_WMI_DSTS_PRESENCE_BIT	0x00010000
#define EEEPC_WMI_DSTS_BRIGHTNESS_MASK	0x000000FF
#define EEEPC_WMI_DSTS_MAX_BRIGTH_MASK	0x0000FF00

static bool hotplug_wireless;

module_param(hotplug_wireless, bool, 0444);
MODULE_PARM_DESC(hotplug_wireless,
		 "Enable hotplug for wireless device. "
		 "If your laptop needs that, please report to "
		 "acpi4asus-user@lists.sourceforge.net.");

static const struct key_entry eeepc_wmi_keymap[] = {
	/* Sleep already handled via generic ACPI code */
	{ KE_IGNORE, NOTIFY_BRNDOWN_MIN, { KEY_BRIGHTNESSDOWN } },
	{ KE_IGNORE, NOTIFY_BRNUP_MIN, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 0x30, { KEY_VOLUMEUP } },
	{ KE_KEY, 0x31, { KEY_VOLUMEDOWN } },
	{ KE_KEY, 0x32, { KEY_MUTE } },
	{ KE_KEY, 0x5c, { KEY_F15 } }, /* Power Gear key */
	{ KE_KEY, 0x5d, { KEY_WLAN } },
	{ KE_KEY, 0x6b, { KEY_F13 } }, /* Disable Touchpad */
	{ KE_KEY, 0x88, { KEY_WLAN } },
	{ KE_KEY, 0xcc, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0xe0, { KEY_PROG1 } }, /* Task Manager */
	{ KE_KEY, 0xe1, { KEY_F14 } }, /* Change Resolution */
	{ KE_KEY, 0xe9, { KEY_BRIGHTNESS_ZERO } },
	{ KE_END, 0},
};

struct bios_args {
	u32	dev_id;
	u32	ctrl_param;
};

/*
 * eeepc-wmi/    - debugfs root directory
 *   dev_id      - current dev_id
 *   ctrl_param  - current ctrl_param
 *   devs        - call DEVS(dev_id, ctrl_param) and print result
 *   dsts        - call DSTS(dev_id)  and print result
 */
struct eeepc_wmi_debug {
	struct dentry *root;
	u32 dev_id;
	u32 ctrl_param;
};

struct eeepc_wmi {
	bool hotplug_wireless;

	struct input_dev *inputdev;
	struct backlight_device *backlight_device;
	struct platform_device *platform_device;

	struct led_classdev tpd_led;
	int tpd_led_wk;
	struct workqueue_struct *led_workqueue;
	struct work_struct tpd_led_work;

	struct rfkill *wlan_rfkill;
	struct rfkill *bluetooth_rfkill;
	struct rfkill *wimax_rfkill;
	struct rfkill *wwan3g_rfkill;

	struct hotplug_slot *hotplug_slot;
	struct mutex hotplug_lock;
	struct mutex wmi_lock;
	struct workqueue_struct *hotplug_workqueue;
	struct work_struct hotplug_work;

	struct eeepc_wmi_debug debug;
};

static int eeepc_wmi_input_init(struct eeepc_wmi *eeepc)
{
	int err;

	eeepc->inputdev = input_allocate_device();
	if (!eeepc->inputdev)
		return -ENOMEM;

	eeepc->inputdev->name = "Eee PC WMI hotkeys";
	eeepc->inputdev->phys = EEEPC_WMI_FILE "/input0";
	eeepc->inputdev->id.bustype = BUS_HOST;
	eeepc->inputdev->dev.parent = &eeepc->platform_device->dev;

	err = sparse_keymap_setup(eeepc->inputdev, eeepc_wmi_keymap, NULL);
	if (err)
		goto err_free_dev;

	err = input_register_device(eeepc->inputdev);
	if (err)
		goto err_free_keymap;

	return 0;

err_free_keymap:
	sparse_keymap_free(eeepc->inputdev);
err_free_dev:
	input_free_device(eeepc->inputdev);
	return err;
}

static void eeepc_wmi_input_exit(struct eeepc_wmi *eeepc)
{
	if (eeepc->inputdev) {
		sparse_keymap_free(eeepc->inputdev);
		input_unregister_device(eeepc->inputdev);
	}

	eeepc->inputdev = NULL;
}

static acpi_status eeepc_wmi_get_devstate(u32 dev_id, u32 *retval)
{
	struct acpi_buffer input = { (acpi_size)sizeof(u32), &dev_id };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	u32 tmp;

	status = wmi_evaluate_method(EEEPC_WMI_MGMT_GUID,
				     1, EEEPC_WMI_METHODID_DSTS,
				     &input, &output);

	if (ACPI_FAILURE(status))
		return status;

	obj = (union acpi_object *)output.pointer;
	if (obj && obj->type == ACPI_TYPE_INTEGER)
		tmp = (u32)obj->integer.value;
	else
		tmp = 0;

	if (retval)
		*retval = tmp;

	kfree(obj);

	return status;

}

static acpi_status eeepc_wmi_set_devstate(u32 dev_id, u32 ctrl_param,
					  u32 *retval)
{
	struct bios_args args = {
		.dev_id = dev_id,
		.ctrl_param = ctrl_param,
	};
	struct acpi_buffer input = { (acpi_size)sizeof(args), &args };
	acpi_status status;

	if (!retval) {
		status = wmi_evaluate_method(EEEPC_WMI_MGMT_GUID, 1,
					     EEEPC_WMI_METHODID_DEVS,
					     &input, NULL);
	} else {
		struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
		union acpi_object *obj;
		u32 tmp;

		status = wmi_evaluate_method(EEEPC_WMI_MGMT_GUID, 1,
					     EEEPC_WMI_METHODID_DEVS,
					     &input, &output);

		if (ACPI_FAILURE(status))
			return status;

		obj = (union acpi_object *)output.pointer;
		if (obj && obj->type == ACPI_TYPE_INTEGER)
			tmp = (u32)obj->integer.value;
		else
			tmp = 0;

		*retval = tmp;

		kfree(obj);
	}

	return status;
}

/* Helper for special devices with magic return codes */
static int eeepc_wmi_get_devstate_bits(u32 dev_id, u32 mask)
{
	u32 retval = 0;
	acpi_status status;

	status = eeepc_wmi_get_devstate(dev_id, &retval);

	if (ACPI_FAILURE(status))
		return -EINVAL;

	if (!(retval & EEEPC_WMI_DSTS_PRESENCE_BIT))
		return -ENODEV;

	return retval & mask;
}

static int eeepc_wmi_get_devstate_simple(u32 dev_id)
{
	return eeepc_wmi_get_devstate_bits(dev_id, EEEPC_WMI_DSTS_STATUS_BIT);
}

/*
 * LEDs
 */
/*
 * These functions actually update the LED's, and are called from a
 * workqueue. By doing this as separate work rather than when the LED
 * subsystem asks, we avoid messing with the Eeepc ACPI stuff during a
 * potentially bad time, such as a timer interrupt.
 */
static void tpd_led_update(struct work_struct *work)
{
	int ctrl_param;
	struct eeepc_wmi *eeepc;

	eeepc = container_of(work, struct eeepc_wmi, tpd_led_work);

	ctrl_param = eeepc->tpd_led_wk;
	eeepc_wmi_set_devstate(EEEPC_WMI_DEVID_TOUCHPAD_LED, ctrl_param, NULL);
}

static void tpd_led_set(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct eeepc_wmi *eeepc;

	eeepc = container_of(led_cdev, struct eeepc_wmi, tpd_led);

	eeepc->tpd_led_wk = !!value;
	queue_work(eeepc->led_workqueue, &eeepc->tpd_led_work);
}

static int read_tpd_led_state(struct eeepc_wmi *eeepc)
{
	return eeepc_wmi_get_devstate_simple(EEEPC_WMI_DEVID_TOUCHPAD_LED);
}

static enum led_brightness tpd_led_get(struct led_classdev *led_cdev)
{
	struct eeepc_wmi *eeepc;

	eeepc = container_of(led_cdev, struct eeepc_wmi, tpd_led);

	return read_tpd_led_state(eeepc);
}

static int eeepc_wmi_led_init(struct eeepc_wmi *eeepc)
{
	int rv;

	if (read_tpd_led_state(eeepc) < 0)
		return 0;

	eeepc->led_workqueue = create_singlethread_workqueue("led_workqueue");
	if (!eeepc->led_workqueue)
		return -ENOMEM;
	INIT_WORK(&eeepc->tpd_led_work, tpd_led_update);

	eeepc->tpd_led.name = "eeepc::touchpad";
	eeepc->tpd_led.brightness_set = tpd_led_set;
	eeepc->tpd_led.brightness_get = tpd_led_get;
	eeepc->tpd_led.max_brightness = 1;

	rv = led_classdev_register(&eeepc->platform_device->dev,
				   &eeepc->tpd_led);
	if (rv) {
		destroy_workqueue(eeepc->led_workqueue);
		return rv;
	}

	return 0;
}

static void eeepc_wmi_led_exit(struct eeepc_wmi *eeepc)
{
	if (eeepc->tpd_led.dev)
		led_classdev_unregister(&eeepc->tpd_led);
	if (eeepc->led_workqueue)
		destroy_workqueue(eeepc->led_workqueue);
}

/*
 * PCI hotplug (for wlan rfkill)
 */
static bool eeepc_wlan_rfkill_blocked(struct eeepc_wmi *eeepc)
{
	int result = eeepc_wmi_get_devstate_simple(EEEPC_WMI_DEVID_WLAN);

	if (result < 0)
		return false;
	return !result;
}

static void eeepc_rfkill_hotplug(struct eeepc_wmi *eeepc)
{
	struct pci_dev *dev;
	struct pci_bus *bus;
	bool blocked;
	bool absent;
	u32 l;

	mutex_lock(&eeepc->wmi_lock);
	blocked = eeepc_wlan_rfkill_blocked(eeepc);
	mutex_unlock(&eeepc->wmi_lock);

	mutex_lock(&eeepc->hotplug_lock);

	if (eeepc->wlan_rfkill)
		rfkill_set_sw_state(eeepc->wlan_rfkill, blocked);

	if (eeepc->hotplug_slot) {
		bus = pci_find_bus(0, 1);
		if (!bus) {
			pr_warning("Unable to find PCI bus 1?\n");
			goto out_unlock;
		}

		if (pci_bus_read_config_dword(bus, 0, PCI_VENDOR_ID, &l)) {
			pr_err("Unable to read PCI config space?\n");
			goto out_unlock;
		}
		absent = (l == 0xffffffff);

		if (blocked != absent) {
			pr_warning("BIOS says wireless lan is %s, "
					"but the pci device is %s\n",
				blocked ? "blocked" : "unblocked",
				absent ? "absent" : "present");
			pr_warning("skipped wireless hotplug as probably "
					"inappropriate for this model\n");
			goto out_unlock;
		}

		if (!blocked) {
			dev = pci_get_slot(bus, 0);
			if (dev) {
				/* Device already present */
				pci_dev_put(dev);
				goto out_unlock;
			}
			dev = pci_scan_single_device(bus, 0);
			if (dev) {
				pci_bus_assign_resources(bus);
				if (pci_bus_add_device(dev))
					pr_err("Unable to hotplug wifi\n");
			}
		} else {
			dev = pci_get_slot(bus, 0);
			if (dev) {
				pci_remove_bus_device(dev);
				pci_dev_put(dev);
			}
		}
	}

out_unlock:
	mutex_unlock(&eeepc->hotplug_lock);
}

static void eeepc_rfkill_notify(acpi_handle handle, u32 event, void *data)
{
	struct eeepc_wmi *eeepc = data;

	if (event != ACPI_NOTIFY_BUS_CHECK)
		return;

	/*
	 * We can't call directly eeepc_rfkill_hotplug because most
	 * of the time WMBC is still being executed and not reetrant.
	 * There is currently no way to tell ACPICA that  we want this
	 * method to be serialized, we schedule a eeepc_rfkill_hotplug
	 * call later, in a safer context.
	 */
	queue_work(eeepc->hotplug_workqueue, &eeepc->hotplug_work);
}

static int eeepc_register_rfkill_notifier(struct eeepc_wmi *eeepc,
					  char *node)
{
	acpi_status status;
	acpi_handle handle;

	status = acpi_get_handle(NULL, node, &handle);

	if (ACPI_SUCCESS(status)) {
		status = acpi_install_notify_handler(handle,
						     ACPI_SYSTEM_NOTIFY,
						     eeepc_rfkill_notify,
						     eeepc);
		if (ACPI_FAILURE(status))
			pr_warning("Failed to register notify on %s\n", node);
	} else
		return -ENODEV;

	return 0;
}

static void eeepc_unregister_rfkill_notifier(struct eeepc_wmi *eeepc,
					     char *node)
{
	acpi_status status = AE_OK;
	acpi_handle handle;

	status = acpi_get_handle(NULL, node, &handle);

	if (ACPI_SUCCESS(status)) {
		status = acpi_remove_notify_handler(handle,
						     ACPI_SYSTEM_NOTIFY,
						     eeepc_rfkill_notify);
		if (ACPI_FAILURE(status))
			pr_err("Error removing rfkill notify handler %s\n",
				node);
	}
}

static int eeepc_get_adapter_status(struct hotplug_slot *hotplug_slot,
				    u8 *value)
{
	int result = eeepc_wmi_get_devstate_simple(EEEPC_WMI_DEVID_WLAN);

	if (result < 0)
		return result;

	*value = !!result;
	return 0;
}

static void eeepc_cleanup_pci_hotplug(struct hotplug_slot *hotplug_slot)
{
	kfree(hotplug_slot->info);
	kfree(hotplug_slot);
}

static struct hotplug_slot_ops eeepc_hotplug_slot_ops = {
	.owner = THIS_MODULE,
	.get_adapter_status = eeepc_get_adapter_status,
	.get_power_status = eeepc_get_adapter_status,
};

static void eeepc_hotplug_work(struct work_struct *work)
{
	struct eeepc_wmi *eeepc;

	eeepc = container_of(work, struct eeepc_wmi, hotplug_work);
	eeepc_rfkill_hotplug(eeepc);
}

static int eeepc_setup_pci_hotplug(struct eeepc_wmi *eeepc)
{
	int ret = -ENOMEM;
	struct pci_bus *bus = pci_find_bus(0, 1);

	if (!bus) {
		pr_err("Unable to find wifi PCI bus\n");
		return -ENODEV;
	}

	eeepc->hotplug_workqueue =
		create_singlethread_workqueue("hotplug_workqueue");
	if (!eeepc->hotplug_workqueue)
		goto error_workqueue;

	INIT_WORK(&eeepc->hotplug_work, eeepc_hotplug_work);

	eeepc->hotplug_slot = kzalloc(sizeof(struct hotplug_slot), GFP_KERNEL);
	if (!eeepc->hotplug_slot)
		goto error_slot;

	eeepc->hotplug_slot->info = kzalloc(sizeof(struct hotplug_slot_info),
					    GFP_KERNEL);
	if (!eeepc->hotplug_slot->info)
		goto error_info;

	eeepc->hotplug_slot->private = eeepc;
	eeepc->hotplug_slot->release = &eeepc_cleanup_pci_hotplug;
	eeepc->hotplug_slot->ops = &eeepc_hotplug_slot_ops;
	eeepc_get_adapter_status(eeepc->hotplug_slot,
				 &eeepc->hotplug_slot->info->adapter_status);

	ret = pci_hp_register(eeepc->hotplug_slot, bus, 0, "eeepc-wifi");
	if (ret) {
		pr_err("Unable to register hotplug slot - %d\n", ret);
		goto error_register;
	}

	return 0;

error_register:
	kfree(eeepc->hotplug_slot->info);
error_info:
	kfree(eeepc->hotplug_slot);
	eeepc->hotplug_slot = NULL;
error_slot:
	destroy_workqueue(eeepc->hotplug_workqueue);
error_workqueue:
	return ret;
}

/*
 * Rfkill devices
 */
static int eeepc_rfkill_set(void *data, bool blocked)
{
	int dev_id = (unsigned long)data;
	u32 ctrl_param = !blocked;
	acpi_status status;

	status = eeepc_wmi_set_devstate(dev_id, ctrl_param, NULL);

	if (ACPI_FAILURE(status))
		return -EIO;

	return 0;
}

static void eeepc_rfkill_query(struct rfkill *rfkill, void *data)
{
	int dev_id = (unsigned long)data;
	int result;

	result = eeepc_wmi_get_devstate_simple(dev_id);

	if (result < 0)
		return ;

	rfkill_set_sw_state(rfkill, !result);
}

static int eeepc_rfkill_wlan_set(void *data, bool blocked)
{
	struct eeepc_wmi *eeepc = data;
	int ret;

	/*
	 * This handler is enabled only if hotplug is enabled.
	 * In this case, the eeepc_wmi_set_devstate() will
	 * trigger a wmi notification and we need to wait
	 * this call to finish before being able to call
	 * any wmi method
	 */
	mutex_lock(&eeepc->wmi_lock);
	ret = eeepc_rfkill_set((void *)(long)EEEPC_WMI_DEVID_WLAN, blocked);
	mutex_unlock(&eeepc->wmi_lock);
	return ret;
}

static void eeepc_rfkill_wlan_query(struct rfkill *rfkill, void *data)
{
	eeepc_rfkill_query(rfkill, (void *)(long)EEEPC_WMI_DEVID_WLAN);
}

static const struct rfkill_ops eeepc_rfkill_wlan_ops = {
	.set_block = eeepc_rfkill_wlan_set,
	.query = eeepc_rfkill_wlan_query,
};

static const struct rfkill_ops eeepc_rfkill_ops = {
	.set_block = eeepc_rfkill_set,
	.query = eeepc_rfkill_query,
};

static int eeepc_new_rfkill(struct eeepc_wmi *eeepc,
			    struct rfkill **rfkill,
			    const char *name,
			    enum rfkill_type type, int dev_id)
{
	int result = eeepc_wmi_get_devstate_simple(dev_id);

	if (result < 0)
		return result;

	if (dev_id == EEEPC_WMI_DEVID_WLAN && eeepc->hotplug_wireless)
		*rfkill = rfkill_alloc(name, &eeepc->platform_device->dev, type,
				       &eeepc_rfkill_wlan_ops, eeepc);
	else
		*rfkill = rfkill_alloc(name, &eeepc->platform_device->dev, type,
				       &eeepc_rfkill_ops, (void *)(long)dev_id);

	if (!*rfkill)
		return -EINVAL;

	rfkill_init_sw_state(*rfkill, !result);
	result = rfkill_register(*rfkill);
	if (result) {
		rfkill_destroy(*rfkill);
		*rfkill = NULL;
		return result;
	}
	return 0;
}

static void eeepc_wmi_rfkill_exit(struct eeepc_wmi *eeepc)
{
	eeepc_unregister_rfkill_notifier(eeepc, "\\_SB.PCI0.P0P5");
	eeepc_unregister_rfkill_notifier(eeepc, "\\_SB.PCI0.P0P6");
	eeepc_unregister_rfkill_notifier(eeepc, "\\_SB.PCI0.P0P7");
	if (eeepc->wlan_rfkill) {
		rfkill_unregister(eeepc->wlan_rfkill);
		rfkill_destroy(eeepc->wlan_rfkill);
		eeepc->wlan_rfkill = NULL;
	}
	/*
	 * Refresh pci hotplug in case the rfkill state was changed after
	 * eeepc_unregister_rfkill_notifier()
	 */
	eeepc_rfkill_hotplug(eeepc);
	if (eeepc->hotplug_slot)
		pci_hp_deregister(eeepc->hotplug_slot);
	if (eeepc->hotplug_workqueue)
		destroy_workqueue(eeepc->hotplug_workqueue);

	if (eeepc->bluetooth_rfkill) {
		rfkill_unregister(eeepc->bluetooth_rfkill);
		rfkill_destroy(eeepc->bluetooth_rfkill);
		eeepc->bluetooth_rfkill = NULL;
	}
	if (eeepc->wimax_rfkill) {
		rfkill_unregister(eeepc->wimax_rfkill);
		rfkill_destroy(eeepc->wimax_rfkill);
		eeepc->wimax_rfkill = NULL;
	}
	if (eeepc->wwan3g_rfkill) {
		rfkill_unregister(eeepc->wwan3g_rfkill);
		rfkill_destroy(eeepc->wwan3g_rfkill);
		eeepc->wwan3g_rfkill = NULL;
	}
}

static int eeepc_wmi_rfkill_init(struct eeepc_wmi *eeepc)
{
	int result = 0;

	mutex_init(&eeepc->hotplug_lock);
	mutex_init(&eeepc->wmi_lock);

	result = eeepc_new_rfkill(eeepc, &eeepc->wlan_rfkill,
				  "eeepc-wlan", RFKILL_TYPE_WLAN,
				  EEEPC_WMI_DEVID_WLAN);

	if (result && result != -ENODEV)
		goto exit;

	result = eeepc_new_rfkill(eeepc, &eeepc->bluetooth_rfkill,
				  "eeepc-bluetooth", RFKILL_TYPE_BLUETOOTH,
				  EEEPC_WMI_DEVID_BLUETOOTH);

	if (result && result != -ENODEV)
		goto exit;

	result = eeepc_new_rfkill(eeepc, &eeepc->wimax_rfkill,
				  "eeepc-wimax", RFKILL_TYPE_WIMAX,
				  EEEPC_WMI_DEVID_WIMAX);

	if (result && result != -ENODEV)
		goto exit;

	result = eeepc_new_rfkill(eeepc, &eeepc->wwan3g_rfkill,
				  "eeepc-wwan3g", RFKILL_TYPE_WWAN,
				  EEEPC_WMI_DEVID_WWAN3G);

	if (result && result != -ENODEV)
		goto exit;

	if (!eeepc->hotplug_wireless)
		goto exit;

	result = eeepc_setup_pci_hotplug(eeepc);
	/*
	 * If we get -EBUSY then something else is handling the PCI hotplug -
	 * don't fail in this case
	 */
	if (result == -EBUSY)
		result = 0;

	eeepc_register_rfkill_notifier(eeepc, "\\_SB.PCI0.P0P5");
	eeepc_register_rfkill_notifier(eeepc, "\\_SB.PCI0.P0P6");
	eeepc_register_rfkill_notifier(eeepc, "\\_SB.PCI0.P0P7");
	/*
	 * Refresh pci hotplug in case the rfkill state was changed during
	 * setup.
	 */
	eeepc_rfkill_hotplug(eeepc);

exit:
	if (result && result != -ENODEV)
		eeepc_wmi_rfkill_exit(eeepc);

	if (result == -ENODEV)
		result = 0;

	return result;
}

/*
 * Backlight
 */
static int read_backlight_power(void)
{
	int ret = eeepc_wmi_get_devstate_simple(EEEPC_WMI_DEVID_BACKLIGHT);

	if (ret < 0)
		return ret;

	return ret ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN;
}

static int read_brightness(struct backlight_device *bd)
{
	u32 retval;
	acpi_status status;

	status = eeepc_wmi_get_devstate(EEEPC_WMI_DEVID_BRIGHTNESS, &retval);

	if (ACPI_FAILURE(status))
		return -EIO;
	else
		return retval & EEEPC_WMI_DSTS_BRIGHTNESS_MASK;
}

static int update_bl_status(struct backlight_device *bd)
{
	u32 ctrl_param;
	acpi_status status;
	int power;

	ctrl_param = bd->props.brightness;

	status = eeepc_wmi_set_devstate(EEEPC_WMI_DEVID_BRIGHTNESS,
					ctrl_param, NULL);

	if (ACPI_FAILURE(status))
		return -EIO;

	power = read_backlight_power();
	if (power != -ENODEV && bd->props.power != power) {
		ctrl_param = !!(bd->props.power == FB_BLANK_UNBLANK);
		status = eeepc_wmi_set_devstate(EEEPC_WMI_DEVID_BACKLIGHT,
						ctrl_param, NULL);

		if (ACPI_FAILURE(status))
			return -EIO;
	}
	return 0;
}

static const struct backlight_ops eeepc_wmi_bl_ops = {
	.get_brightness = read_brightness,
	.update_status = update_bl_status,
};

static int eeepc_wmi_backlight_notify(struct eeepc_wmi *eeepc, int code)
{
	struct backlight_device *bd = eeepc->backlight_device;
	int old = bd->props.brightness;
	int new = old;

	if (code >= NOTIFY_BRNUP_MIN && code <= NOTIFY_BRNUP_MAX)
		new = code - NOTIFY_BRNUP_MIN + 1;
	else if (code >= NOTIFY_BRNDOWN_MIN && code <= NOTIFY_BRNDOWN_MAX)
		new = code - NOTIFY_BRNDOWN_MIN;

	bd->props.brightness = new;
	backlight_update_status(bd);
	backlight_force_update(bd, BACKLIGHT_UPDATE_HOTKEY);

	return old;
}

static int eeepc_wmi_backlight_init(struct eeepc_wmi *eeepc)
{
	struct backlight_device *bd;
	struct backlight_properties props;
	int max;
	int power;

	max = eeepc_wmi_get_devstate_bits(EEEPC_WMI_DEVID_BRIGHTNESS,
					  EEEPC_WMI_DSTS_MAX_BRIGTH_MASK);
	power = read_backlight_power();

	if (max < 0 && power < 0) {
		/* Try to keep the original error */
		if (max == -ENODEV && power == -ENODEV)
			return -ENODEV;
		if (max != -ENODEV)
			return max;
		else
			return power;
	}
	if (max == -ENODEV)
		max = 0;
	if (power == -ENODEV)
		power = FB_BLANK_UNBLANK;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = max;
	bd = backlight_device_register(EEEPC_WMI_FILE,
				       &eeepc->platform_device->dev, eeepc,
				       &eeepc_wmi_bl_ops, &props);
	if (IS_ERR(bd)) {
		pr_err("Could not register backlight device\n");
		return PTR_ERR(bd);
	}

	eeepc->backlight_device = bd;

	bd->props.brightness = read_brightness(bd);
	bd->props.power = power;
	backlight_update_status(bd);

	return 0;
}

static void eeepc_wmi_backlight_exit(struct eeepc_wmi *eeepc)
{
	if (eeepc->backlight_device)
		backlight_device_unregister(eeepc->backlight_device);

	eeepc->backlight_device = NULL;
}

static void eeepc_wmi_notify(u32 value, void *context)
{
	struct eeepc_wmi *eeepc = context;
	struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	int code;
	int orig_code;

	status = wmi_get_event_data(value, &response);
	if (status != AE_OK) {
		pr_err("bad event status 0x%x\n", status);
		return;
	}

	obj = (union acpi_object *)response.pointer;

	if (obj && obj->type == ACPI_TYPE_INTEGER) {
		code = obj->integer.value;
		orig_code = code;

		if (code >= NOTIFY_BRNUP_MIN && code <= NOTIFY_BRNUP_MAX)
			code = NOTIFY_BRNUP_MIN;
		else if (code >= NOTIFY_BRNDOWN_MIN &&
			 code <= NOTIFY_BRNDOWN_MAX)
			code = NOTIFY_BRNDOWN_MIN;

		if (code == NOTIFY_BRNUP_MIN || code == NOTIFY_BRNDOWN_MIN) {
			if (!acpi_video_backlight_support())
				eeepc_wmi_backlight_notify(eeepc, orig_code);
		}

		if (!sparse_keymap_report_event(eeepc->inputdev,
						code, 1, true))
			pr_info("Unknown key %x pressed\n", code);
	}

	kfree(obj);
}

/*
 * Sys helpers
 */
static int parse_arg(const char *buf, unsigned long count, int *val)
{
	if (!count)
		return 0;
	if (sscanf(buf, "%i", val) != 1)
		return -EINVAL;
	return count;
}

static ssize_t store_sys_wmi(int devid, const char *buf, size_t count)
{
	acpi_status status;
	u32 retval;
	int rv, value;

	value = eeepc_wmi_get_devstate_simple(devid);
	if (value == -ENODEV) /* Check device presence */
		return value;

	rv = parse_arg(buf, count, &value);
	status = eeepc_wmi_set_devstate(devid, value, &retval);

	if (ACPI_FAILURE(status))
		return -EIO;
	return rv;
}

static ssize_t show_sys_wmi(int devid, char *buf)
{
	int value = eeepc_wmi_get_devstate_simple(devid);

	if (value < 0)
		return value;

	return sprintf(buf, "%d\n", value);
}

#define EEEPC_WMI_CREATE_DEVICE_ATTR(_name, _mode, _cm)			\
	static ssize_t show_##_name(struct device *dev,			\
				    struct device_attribute *attr,	\
				    char *buf)				\
	{								\
		return show_sys_wmi(_cm, buf);				\
	}								\
	static ssize_t store_##_name(struct device *dev,		\
				     struct device_attribute *attr,	\
				     const char *buf, size_t count)	\
	{								\
		return store_sys_wmi(_cm, buf, count);			\
	}								\
	static struct device_attribute dev_attr_##_name = {		\
		.attr = {						\
			.name = __stringify(_name),			\
			.mode = _mode },				\
		.show   = show_##_name,					\
		.store  = store_##_name,				\
	}

EEEPC_WMI_CREATE_DEVICE_ATTR(touchpad, 0644, EEEPC_WMI_DEVID_TOUCHPAD);
EEEPC_WMI_CREATE_DEVICE_ATTR(camera, 0644, EEEPC_WMI_DEVID_CAMERA);
EEEPC_WMI_CREATE_DEVICE_ATTR(cardr, 0644, EEEPC_WMI_DEVID_CARDREADER);

static ssize_t store_cpufv(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	int value;
	struct acpi_buffer input = { (acpi_size)sizeof(value), &value };
	acpi_status status;

	if (!count || sscanf(buf, "%i", &value) != 1)
		return -EINVAL;
	if (value < 0 || value > 2)
		return -EINVAL;

	status = wmi_evaluate_method(EEEPC_WMI_MGMT_GUID,
				     1, EEEPC_WMI_METHODID_CFVS, &input, NULL);

	if (ACPI_FAILURE(status))
		return -EIO;
	else
		return count;
}

static DEVICE_ATTR(cpufv, S_IRUGO | S_IWUSR, NULL, store_cpufv);

static struct attribute *platform_attributes[] = {
	&dev_attr_cpufv.attr,
	&dev_attr_camera.attr,
	&dev_attr_cardr.attr,
	&dev_attr_touchpad.attr,
	NULL
};

static mode_t eeepc_sysfs_is_visible(struct kobject *kobj,
				     struct attribute *attr,
				     int idx)
{
	bool supported = true;
	int devid = -1;

	if (attr == &dev_attr_camera.attr)
		devid = EEEPC_WMI_DEVID_CAMERA;
	else if (attr == &dev_attr_cardr.attr)
		devid = EEEPC_WMI_DEVID_CARDREADER;
	else if (attr == &dev_attr_touchpad.attr)
		devid = EEEPC_WMI_DEVID_TOUCHPAD;

	if (devid != -1)
		supported = eeepc_wmi_get_devstate_simple(devid) != -ENODEV;

	return supported ? attr->mode : 0;
}

static struct attribute_group platform_attribute_group = {
	.is_visible	= eeepc_sysfs_is_visible,
	.attrs		= platform_attributes
};

static void eeepc_wmi_sysfs_exit(struct platform_device *device)
{
	sysfs_remove_group(&device->dev.kobj, &platform_attribute_group);
}

static int eeepc_wmi_sysfs_init(struct platform_device *device)
{
	return sysfs_create_group(&device->dev.kobj, &platform_attribute_group);
}

/*
 * Platform device
 */
static int __init eeepc_wmi_platform_init(struct eeepc_wmi *eeepc)
{
	return eeepc_wmi_sysfs_init(eeepc->platform_device);
}

static void eeepc_wmi_platform_exit(struct eeepc_wmi *eeepc)
{
	eeepc_wmi_sysfs_exit(eeepc->platform_device);
}

/*
 * debugfs
 */
struct eeepc_wmi_debugfs_node {
	struct eeepc_wmi *eeepc;
	char *name;
	int (*show)(struct seq_file *m, void *data);
};

static int show_dsts(struct seq_file *m, void *data)
{
	struct eeepc_wmi *eeepc = m->private;
	acpi_status status;
	u32 retval = -1;

	status = eeepc_wmi_get_devstate(eeepc->debug.dev_id, &retval);

	if (ACPI_FAILURE(status))
		return -EIO;

	seq_printf(m, "DSTS(%x) = %x\n", eeepc->debug.dev_id, retval);

	return 0;
}

static int show_devs(struct seq_file *m, void *data)
{
	struct eeepc_wmi *eeepc = m->private;
	acpi_status status;
	u32 retval = -1;

	status = eeepc_wmi_set_devstate(eeepc->debug.dev_id,
					eeepc->debug.ctrl_param, &retval);
	if (ACPI_FAILURE(status))
		return -EIO;

	seq_printf(m, "DEVS(%x, %x) = %x\n", eeepc->debug.dev_id,
		   eeepc->debug.ctrl_param, retval);

	return 0;
}

static struct eeepc_wmi_debugfs_node eeepc_wmi_debug_files[] = {
	{ NULL, "devs", show_devs },
	{ NULL, "dsts", show_dsts },
};

static int eeepc_wmi_debugfs_open(struct inode *inode, struct file *file)
{
	struct eeepc_wmi_debugfs_node *node = inode->i_private;

	return single_open(file, node->show, node->eeepc);
}

static const struct file_operations eeepc_wmi_debugfs_io_ops = {
	.owner = THIS_MODULE,
	.open  = eeepc_wmi_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void eeepc_wmi_debugfs_exit(struct eeepc_wmi *eeepc)
{
	debugfs_remove_recursive(eeepc->debug.root);
}

static int eeepc_wmi_debugfs_init(struct eeepc_wmi *eeepc)
{
	struct dentry *dent;
	int i;

	eeepc->debug.root = debugfs_create_dir(EEEPC_WMI_FILE, NULL);
	if (!eeepc->debug.root) {
		pr_err("failed to create debugfs directory");
		goto error_debugfs;
	}

	dent = debugfs_create_x32("dev_id", S_IRUGO|S_IWUSR,
				  eeepc->debug.root, &eeepc->debug.dev_id);
	if (!dent)
		goto error_debugfs;

	dent = debugfs_create_x32("ctrl_param", S_IRUGO|S_IWUSR,
				  eeepc->debug.root, &eeepc->debug.ctrl_param);
	if (!dent)
		goto error_debugfs;

	for (i = 0; i < ARRAY_SIZE(eeepc_wmi_debug_files); i++) {
		struct eeepc_wmi_debugfs_node *node = &eeepc_wmi_debug_files[i];

		node->eeepc = eeepc;
		dent = debugfs_create_file(node->name, S_IFREG | S_IRUGO,
					   eeepc->debug.root, node,
					   &eeepc_wmi_debugfs_io_ops);
		if (!dent) {
			pr_err("failed to create debug file: %s\n", node->name);
			goto error_debugfs;
		}
	}

	return 0;

error_debugfs:
	eeepc_wmi_debugfs_exit(eeepc);
	return -ENOMEM;
}

/*
 * WMI Driver
 */
static void eeepc_dmi_check(struct eeepc_wmi *eeepc)
{
	const char *model;

	model = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (!model)
		return;

	/*
	 * Whitelist for wlan hotplug
	 *
	 * Eeepc 1000H needs the current hotplug code to handle
	 * Fn+F2 correctly. We may add other Eeepc here later, but
	 * it seems that most of the laptops supported by eeepc-wmi
	 * don't need to be on this list
	 */
	if (strcmp(model, "1000H") == 0) {
		eeepc->hotplug_wireless = true;
		pr_info("wlan hotplug enabled\n");
	}
}

static int __init eeepc_wmi_add(struct platform_device *pdev)
{
	struct eeepc_wmi *eeepc;
	acpi_status status;
	int err;

	eeepc = kzalloc(sizeof(struct eeepc_wmi), GFP_KERNEL);
	if (!eeepc)
		return -ENOMEM;

	eeepc->platform_device = pdev;
	platform_set_drvdata(eeepc->platform_device, eeepc);

	eeepc->hotplug_wireless = hotplug_wireless;
	eeepc_dmi_check(eeepc);

	err = eeepc_wmi_platform_init(eeepc);
	if (err)
		goto fail_platform;

	err = eeepc_wmi_input_init(eeepc);
	if (err)
		goto fail_input;

	err = eeepc_wmi_led_init(eeepc);
	if (err)
		goto fail_leds;

	err = eeepc_wmi_rfkill_init(eeepc);
	if (err)
		goto fail_rfkill;

	if (!acpi_video_backlight_support()) {
		err = eeepc_wmi_backlight_init(eeepc);
		if (err && err != -ENODEV)
			goto fail_backlight;
	} else
		pr_info("Backlight controlled by ACPI video driver\n");

	status = wmi_install_notify_handler(EEEPC_WMI_EVENT_GUID,
					    eeepc_wmi_notify, eeepc);
	if (ACPI_FAILURE(status)) {
		pr_err("Unable to register notify handler - %d\n",
			status);
		err = -ENODEV;
		goto fail_wmi_handler;
	}

	err = eeepc_wmi_debugfs_init(eeepc);
	if (err)
		goto fail_debugfs;

	return 0;

fail_debugfs:
	wmi_remove_notify_handler(EEEPC_WMI_EVENT_GUID);
fail_wmi_handler:
	eeepc_wmi_backlight_exit(eeepc);
fail_backlight:
	eeepc_wmi_rfkill_exit(eeepc);
fail_rfkill:
	eeepc_wmi_led_exit(eeepc);
fail_leds:
	eeepc_wmi_input_exit(eeepc);
fail_input:
	eeepc_wmi_platform_exit(eeepc);
fail_platform:
	kfree(eeepc);
	return err;
}

static int __exit eeepc_wmi_remove(struct platform_device *device)
{
	struct eeepc_wmi *eeepc;

	eeepc = platform_get_drvdata(device);
	wmi_remove_notify_handler(EEEPC_WMI_EVENT_GUID);
	eeepc_wmi_backlight_exit(eeepc);
	eeepc_wmi_input_exit(eeepc);
	eeepc_wmi_led_exit(eeepc);
	eeepc_wmi_rfkill_exit(eeepc);
	eeepc_wmi_debugfs_exit(eeepc);
	eeepc_wmi_platform_exit(eeepc);

	kfree(eeepc);
	return 0;
}

/*
 * Platform driver - hibernate/resume callbacks
 */
static int eeepc_hotk_thaw(struct device *device)
{
	struct eeepc_wmi *eeepc = dev_get_drvdata(device);

	if (eeepc->wlan_rfkill) {
		bool wlan;

		/*
		 * Work around bios bug - acpi _PTS turns off the wireless led
		 * during suspend.  Normally it restores it on resume, but
		 * we should kick it ourselves in case hibernation is aborted.
		 */
		wlan = eeepc_wmi_get_devstate_simple(EEEPC_WMI_DEVID_WLAN);
		eeepc_wmi_set_devstate(EEEPC_WMI_DEVID_WLAN, wlan, NULL);
	}

	return 0;
}

static int eeepc_hotk_restore(struct device *device)
{
	struct eeepc_wmi *eeepc = dev_get_drvdata(device);
	int bl;

	/* Refresh both wlan rfkill state and pci hotplug */
	if (eeepc->wlan_rfkill)
		eeepc_rfkill_hotplug(eeepc);

	if (eeepc->bluetooth_rfkill) {
		bl = !eeepc_wmi_get_devstate_simple(EEEPC_WMI_DEVID_BLUETOOTH);
		rfkill_set_sw_state(eeepc->bluetooth_rfkill, bl);
	}
	if (eeepc->wimax_rfkill) {
		bl = !eeepc_wmi_get_devstate_simple(EEEPC_WMI_DEVID_WIMAX);
		rfkill_set_sw_state(eeepc->wimax_rfkill, bl);
	}
	if (eeepc->wwan3g_rfkill) {
		bl = !eeepc_wmi_get_devstate_simple(EEEPC_WMI_DEVID_WWAN3G);
		rfkill_set_sw_state(eeepc->wwan3g_rfkill, bl);
	}

	return 0;
}

static const struct dev_pm_ops eeepc_pm_ops = {
	.thaw = eeepc_hotk_thaw,
	.restore = eeepc_hotk_restore,
};

static struct platform_driver platform_driver = {
	.remove = __exit_p(eeepc_wmi_remove),
	.driver = {
		.name = EEEPC_WMI_FILE,
		.owner = THIS_MODULE,
		.pm = &eeepc_pm_ops,
	},
};

static acpi_status __init eeepc_wmi_parse_device(acpi_handle handle, u32 level,
						 void *context, void **retval)
{
	pr_warning("Found legacy ATKD device (%s)", EEEPC_ACPI_HID);
	*(bool *)context = true;
	return AE_CTRL_TERMINATE;
}

static int __init eeepc_wmi_check_atkd(void)
{
	acpi_status status;
	bool found = false;

	status = acpi_get_devices(EEEPC_ACPI_HID, eeepc_wmi_parse_device,
				  &found, NULL);

	if (ACPI_FAILURE(status) || !found)
		return 0;
	return -1;
}

static int __init eeepc_wmi_probe(struct platform_device *pdev)
{
	if (!wmi_has_guid(EEEPC_WMI_EVENT_GUID) ||
	    !wmi_has_guid(EEEPC_WMI_MGMT_GUID)) {
		pr_warning("No known WMI GUID found\n");
		return -ENODEV;
	}

	if (eeepc_wmi_check_atkd()) {
		pr_warning("WMI device present, but legacy ATKD device is also "
			   "present and enabled.");
		pr_warning("You probably booted with acpi_osi=\"Linux\" or "
			   "acpi_osi=\"!Windows 2009\"");
		pr_warning("Can't load eeepc-wmi, use default acpi_osi "
			   "(preferred) or eeepc-laptop");
		return -ENODEV;
	}

	return eeepc_wmi_add(pdev);
}

static struct platform_device *platform_device;

static int __init eeepc_wmi_init(void)
{
	platform_device = platform_create_bundle(&platform_driver,
						 eeepc_wmi_probe,
						 NULL, 0, NULL, 0);
	if (IS_ERR(platform_device))
		return PTR_ERR(platform_device);
	return 0;
}

static void __exit eeepc_wmi_exit(void)
{
	platform_device_unregister(platform_device);
	platform_driver_unregister(&platform_driver);
}

module_init(eeepc_wmi_init);
module_exit(eeepc_wmi_exit);
