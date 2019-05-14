/*
 * Asus PC WMI hotkey driver
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
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/platform_data/x86/asus-wmi.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <acpi/video.h>

#include "asus-wmi.h"

MODULE_AUTHOR("Corentin Chary <corentin.chary@gmail.com>, "
	      "Yong Wang <yong.y.wang@intel.com>");
MODULE_DESCRIPTION("Asus Generic WMI Driver");
MODULE_LICENSE("GPL");

#define to_asus_wmi_driver(pdrv)					\
	(container_of((pdrv), struct asus_wmi_driver, platform_driver))

#define ASUS_WMI_MGMT_GUID	"97845ED0-4E6D-11DE-8A39-0800200C9A66"

#define NOTIFY_BRNUP_MIN		0x11
#define NOTIFY_BRNUP_MAX		0x1f
#define NOTIFY_BRNDOWN_MIN		0x20
#define NOTIFY_BRNDOWN_MAX		0x2e
#define NOTIFY_FNLOCK_TOGGLE		0x4e
#define NOTIFY_KBD_BRTUP		0xc4
#define NOTIFY_KBD_BRTDWN		0xc5
#define NOTIFY_KBD_BRTTOGGLE		0xc7
#define NOTIFY_KBD_FBM			0x99

#define ASUS_WMI_FNLOCK_BIOS_DISABLED	BIT(0)

#define ASUS_FAN_DESC			"cpu_fan"
#define ASUS_FAN_MFUN			0x13
#define ASUS_FAN_SFUN_READ		0x06
#define ASUS_FAN_SFUN_WRITE		0x07
#define ASUS_FAN_CTRL_MANUAL		1
#define ASUS_FAN_CTRL_AUTO		2

#define ASUS_FAN_MODE_NORMAL		0
#define ASUS_FAN_MODE_OVERBOOST		1
#define ASUS_FAN_MODE_OVERBOOST_MASK	0x01
#define ASUS_FAN_MODE_SILENT		2
#define ASUS_FAN_MODE_SILENT_MASK	0x02
#define ASUS_FAN_MODES_MASK		0x03

#define USB_INTEL_XUSB2PR		0xD0
#define PCI_DEVICE_ID_INTEL_LYNXPOINT_LP_XHCI	0x9c31

#define ASUS_ACPI_UID_ASUSWMI		"ASUSWMI"
#define ASUS_ACPI_UID_ATK		"ATK"

#define WMI_EVENT_QUEUE_SIZE		0x10
#define WMI_EVENT_QUEUE_END		0x1
#define WMI_EVENT_MASK			0xFFFF
/* The WMI hotkey event value is always the same. */
#define WMI_EVENT_VALUE_ATK		0xFF

#define WMI_EVENT_MASK			0xFFFF

static const char * const ashs_ids[] = { "ATK4001", "ATK4002", NULL };

static bool ashs_present(void)
{
	int i = 0;
	while (ashs_ids[i]) {
		if (acpi_dev_found(ashs_ids[i++]))
			return true;
	}
	return false;
}

struct bios_args {
	u32 arg0;
	u32 arg1;
	u32 arg2; /* At least TUF Gaming series uses 3 dword input buffer. */
} __packed;

/*
 * Struct that's used for all methods called via AGFN. Naming is
 * identically to the AML code.
 */
struct agfn_args {
	u16 mfun; /* probably "Multi-function" to be called */
	u16 sfun; /* probably "Sub-function" to be called */
	u16 len;  /* size of the hole struct, including subfunction fields */
	u8 stas;  /* not used by now */
	u8 err;   /* zero on success */
} __packed;

/* struct used for calling fan read and write methods */
struct fan_args {
	struct agfn_args agfn;	/* common fields */
	u8 fan;			/* fan number: 0: set auto mode 1: 1st fan */
	u32 speed;		/* read: RPM/100 - write: 0-255 */
} __packed;

/*
 * <platform>/    - debugfs root directory
 *   dev_id      - current dev_id
 *   ctrl_param  - current ctrl_param
 *   method_id   - current method_id
 *   devs        - call DEVS(dev_id, ctrl_param) and print result
 *   dsts        - call DSTS(dev_id)  and print result
 *   call        - call method_id(dev_id, ctrl_param) and print result
 */
struct asus_wmi_debug {
	struct dentry *root;
	u32 method_id;
	u32 dev_id;
	u32 ctrl_param;
};

struct asus_rfkill {
	struct asus_wmi *asus;
	struct rfkill *rfkill;
	u32 dev_id;
};

struct asus_wmi {
	int dsts_id;
	int spec;
	int sfun;
	bool wmi_event_queue;

	struct input_dev *inputdev;
	struct backlight_device *backlight_device;
	struct platform_device *platform_device;

	struct led_classdev wlan_led;
	int wlan_led_wk;
	struct led_classdev tpd_led;
	int tpd_led_wk;
	struct led_classdev kbd_led;
	int kbd_led_wk;
	struct led_classdev lightbar_led;
	int lightbar_led_wk;
	struct workqueue_struct *led_workqueue;
	struct work_struct tpd_led_work;
	struct work_struct wlan_led_work;
	struct work_struct lightbar_led_work;

	struct asus_rfkill wlan;
	struct asus_rfkill bluetooth;
	struct asus_rfkill wimax;
	struct asus_rfkill wwan3g;
	struct asus_rfkill gps;
	struct asus_rfkill uwb;

	bool asus_hwmon_fan_manual_mode;
	int asus_hwmon_num_fans;
	int asus_hwmon_pwm;

	bool fan_mode_available;
	u8 fan_mode_mask;
	u8 fan_mode;

	struct hotplug_slot hotplug_slot;
	struct mutex hotplug_lock;
	struct mutex wmi_lock;
	struct workqueue_struct *hotplug_workqueue;
	struct work_struct hotplug_work;

	bool fnlock_locked;

	struct asus_wmi_debug debug;

	struct asus_wmi_driver *driver;
};

/* Input **********************************************************************/

static int asus_wmi_input_init(struct asus_wmi *asus)
{
	int err;

	asus->inputdev = input_allocate_device();
	if (!asus->inputdev)
		return -ENOMEM;

	asus->inputdev->name = asus->driver->input_name;
	asus->inputdev->phys = asus->driver->input_phys;
	asus->inputdev->id.bustype = BUS_HOST;
	asus->inputdev->dev.parent = &asus->platform_device->dev;
	set_bit(EV_REP, asus->inputdev->evbit);

	err = sparse_keymap_setup(asus->inputdev, asus->driver->keymap, NULL);
	if (err)
		goto err_free_dev;

	err = input_register_device(asus->inputdev);
	if (err)
		goto err_free_dev;

	return 0;

err_free_dev:
	input_free_device(asus->inputdev);
	return err;
}

static void asus_wmi_input_exit(struct asus_wmi *asus)
{
	if (asus->inputdev)
		input_unregister_device(asus->inputdev);

	asus->inputdev = NULL;
}

/* WMI ************************************************************************/

static int asus_wmi_evaluate_method3(u32 method_id,
		u32 arg0, u32 arg1, u32 arg2, u32 *retval)
{
	struct bios_args args = {
		.arg0 = arg0,
		.arg1 = arg1,
		.arg2 = arg2,
	};
	struct acpi_buffer input = { (acpi_size) sizeof(args), &args };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;
	union acpi_object *obj;
	u32 tmp = 0;

	status = wmi_evaluate_method(ASUS_WMI_MGMT_GUID, 0, method_id,
				     &input, &output);

	if (ACPI_FAILURE(status))
		return -EIO;

	obj = (union acpi_object *)output.pointer;
	if (obj && obj->type == ACPI_TYPE_INTEGER)
		tmp = (u32) obj->integer.value;

	if (retval)
		*retval = tmp;

	kfree(obj);

	if (tmp == ASUS_WMI_UNSUPPORTED_METHOD)
		return -ENODEV;

	return 0;
}

int asus_wmi_evaluate_method(u32 method_id, u32 arg0, u32 arg1, u32 *retval)
{
	return asus_wmi_evaluate_method3(method_id, arg0, arg1, 0, retval);
}
EXPORT_SYMBOL_GPL(asus_wmi_evaluate_method);

static int asus_wmi_evaluate_method_agfn(const struct acpi_buffer args)
{
	struct acpi_buffer input;
	u64 phys_addr;
	u32 retval;
	u32 status = -1;

	/*
	 * Copy to dma capable address otherwise memory corruption occurs as
	 * bios has to be able to access it.
	 */
	input.pointer = kzalloc(args.length, GFP_DMA | GFP_KERNEL);
	input.length = args.length;
	if (!input.pointer)
		return -ENOMEM;
	phys_addr = virt_to_phys(input.pointer);
	memcpy(input.pointer, args.pointer, args.length);

	status = asus_wmi_evaluate_method(ASUS_WMI_METHODID_AGFN,
					phys_addr, 0, &retval);
	if (!status)
		memcpy(args.pointer, input.pointer, args.length);

	kfree(input.pointer);
	if (status)
		return -ENXIO;

	return retval;
}

static int asus_wmi_get_devstate(struct asus_wmi *asus, u32 dev_id, u32 *retval)
{
	return asus_wmi_evaluate_method(asus->dsts_id, dev_id, 0, retval);
}

static int asus_wmi_set_devstate(u32 dev_id, u32 ctrl_param,
				 u32 *retval)
{
	return asus_wmi_evaluate_method(ASUS_WMI_METHODID_DEVS, dev_id,
					ctrl_param, retval);
}

/* Helper for special devices with magic return codes */
static int asus_wmi_get_devstate_bits(struct asus_wmi *asus,
				      u32 dev_id, u32 mask)
{
	u32 retval = 0;
	int err;

	err = asus_wmi_get_devstate(asus, dev_id, &retval);

	if (err < 0)
		return err;

	if (!(retval & ASUS_WMI_DSTS_PRESENCE_BIT))
		return -ENODEV;

	if (mask == ASUS_WMI_DSTS_STATUS_BIT) {
		if (retval & ASUS_WMI_DSTS_UNKNOWN_BIT)
			return -ENODEV;
	}

	return retval & mask;
}

static int asus_wmi_get_devstate_simple(struct asus_wmi *asus, u32 dev_id)
{
	return asus_wmi_get_devstate_bits(asus, dev_id,
					  ASUS_WMI_DSTS_STATUS_BIT);
}

/* LEDs ***********************************************************************/

/*
 * These functions actually update the LED's, and are called from a
 * workqueue. By doing this as separate work rather than when the LED
 * subsystem asks, we avoid messing with the Asus ACPI stuff during a
 * potentially bad time, such as a timer interrupt.
 */
static void tpd_led_update(struct work_struct *work)
{
	int ctrl_param;
	struct asus_wmi *asus;

	asus = container_of(work, struct asus_wmi, tpd_led_work);

	ctrl_param = asus->tpd_led_wk;
	asus_wmi_set_devstate(ASUS_WMI_DEVID_TOUCHPAD_LED, ctrl_param, NULL);
}

static void tpd_led_set(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct asus_wmi *asus;

	asus = container_of(led_cdev, struct asus_wmi, tpd_led);

	asus->tpd_led_wk = !!value;
	queue_work(asus->led_workqueue, &asus->tpd_led_work);
}

static int read_tpd_led_state(struct asus_wmi *asus)
{
	return asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_TOUCHPAD_LED);
}

static enum led_brightness tpd_led_get(struct led_classdev *led_cdev)
{
	struct asus_wmi *asus;

	asus = container_of(led_cdev, struct asus_wmi, tpd_led);

	return read_tpd_led_state(asus);
}

static void kbd_led_update(struct asus_wmi *asus)
{
	int ctrl_param = 0;

	/*
	 * bits 0-2: level
	 * bit 7: light on/off
	 */
	if (asus->kbd_led_wk > 0)
		ctrl_param = 0x80 | (asus->kbd_led_wk & 0x7F);

	asus_wmi_set_devstate(ASUS_WMI_DEVID_KBD_BACKLIGHT, ctrl_param, NULL);
}

static int kbd_led_read(struct asus_wmi *asus, int *level, int *env)
{
	int retval;

	/*
	 * bits 0-2: level
	 * bit 7: light on/off
	 * bit 8-10: environment (0: dark, 1: normal, 2: light)
	 * bit 17: status unknown
	 */
	retval = asus_wmi_get_devstate_bits(asus, ASUS_WMI_DEVID_KBD_BACKLIGHT,
					    0xFFFF);

	/* Unknown status is considered as off */
	if (retval == 0x8000)
		retval = 0;

	if (retval >= 0) {
		if (level)
			*level = retval & 0x7F;
		if (env)
			*env = (retval >> 8) & 0x7F;
		retval = 0;
	}

	return retval;
}

static void do_kbd_led_set(struct led_classdev *led_cdev, int value)
{
	struct asus_wmi *asus;
	int max_level;

	asus = container_of(led_cdev, struct asus_wmi, kbd_led);
	max_level = asus->kbd_led.max_brightness;

	if (value > max_level)
		value = max_level;
	else if (value < 0)
		value = 0;

	asus->kbd_led_wk = value;
	kbd_led_update(asus);
}

static void kbd_led_set(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	do_kbd_led_set(led_cdev, value);
}

static void kbd_led_set_by_kbd(struct asus_wmi *asus, enum led_brightness value)
{
	struct led_classdev *led_cdev = &asus->kbd_led;

	do_kbd_led_set(led_cdev, value);
	led_classdev_notify_brightness_hw_changed(led_cdev, asus->kbd_led_wk);
}

static enum led_brightness kbd_led_get(struct led_classdev *led_cdev)
{
	struct asus_wmi *asus;
	int retval, value;

	asus = container_of(led_cdev, struct asus_wmi, kbd_led);

	retval = kbd_led_read(asus, &value, NULL);

	if (retval < 0)
		return retval;

	return value;
}

static int wlan_led_unknown_state(struct asus_wmi *asus)
{
	u32 result;

	asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_WIRELESS_LED, &result);

	return result & ASUS_WMI_DSTS_UNKNOWN_BIT;
}

static int wlan_led_presence(struct asus_wmi *asus)
{
	u32 result;

	asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_WIRELESS_LED, &result);

	return result & ASUS_WMI_DSTS_PRESENCE_BIT;
}

static void wlan_led_update(struct work_struct *work)
{
	int ctrl_param;
	struct asus_wmi *asus;

	asus = container_of(work, struct asus_wmi, wlan_led_work);

	ctrl_param = asus->wlan_led_wk;
	asus_wmi_set_devstate(ASUS_WMI_DEVID_WIRELESS_LED, ctrl_param, NULL);
}

static void wlan_led_set(struct led_classdev *led_cdev,
			 enum led_brightness value)
{
	struct asus_wmi *asus;

	asus = container_of(led_cdev, struct asus_wmi, wlan_led);

	asus->wlan_led_wk = !!value;
	queue_work(asus->led_workqueue, &asus->wlan_led_work);
}

static enum led_brightness wlan_led_get(struct led_classdev *led_cdev)
{
	struct asus_wmi *asus;
	u32 result;

	asus = container_of(led_cdev, struct asus_wmi, wlan_led);
	asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_WIRELESS_LED, &result);

	return result & ASUS_WMI_DSTS_BRIGHTNESS_MASK;
}

static void lightbar_led_update(struct work_struct *work)
{
	struct asus_wmi *asus;
	int ctrl_param;

	asus = container_of(work, struct asus_wmi, lightbar_led_work);

	ctrl_param = asus->lightbar_led_wk;
	asus_wmi_set_devstate(ASUS_WMI_DEVID_LIGHTBAR, ctrl_param, NULL);
}

static void lightbar_led_set(struct led_classdev *led_cdev,
			     enum led_brightness value)
{
	struct asus_wmi *asus;

	asus = container_of(led_cdev, struct asus_wmi, lightbar_led);

	asus->lightbar_led_wk = !!value;
	queue_work(asus->led_workqueue, &asus->lightbar_led_work);
}

static enum led_brightness lightbar_led_get(struct led_classdev *led_cdev)
{
	struct asus_wmi *asus;
	u32 result;

	asus = container_of(led_cdev, struct asus_wmi, lightbar_led);
	asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_LIGHTBAR, &result);

	return result & ASUS_WMI_DSTS_LIGHTBAR_MASK;
}

static int lightbar_led_presence(struct asus_wmi *asus)
{
	u32 result;

	asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_LIGHTBAR, &result);

	return result & ASUS_WMI_DSTS_PRESENCE_BIT;
}

static void asus_wmi_led_exit(struct asus_wmi *asus)
{
	if (!IS_ERR_OR_NULL(asus->kbd_led.dev))
		led_classdev_unregister(&asus->kbd_led);
	if (!IS_ERR_OR_NULL(asus->tpd_led.dev))
		led_classdev_unregister(&asus->tpd_led);
	if (!IS_ERR_OR_NULL(asus->wlan_led.dev))
		led_classdev_unregister(&asus->wlan_led);
	if (!IS_ERR_OR_NULL(asus->lightbar_led.dev))
		led_classdev_unregister(&asus->lightbar_led);
	if (asus->led_workqueue)
		destroy_workqueue(asus->led_workqueue);
}

static int asus_wmi_led_init(struct asus_wmi *asus)
{
	int rv = 0, led_val;

	asus->led_workqueue = create_singlethread_workqueue("led_workqueue");
	if (!asus->led_workqueue)
		return -ENOMEM;

	if (read_tpd_led_state(asus) >= 0) {
		INIT_WORK(&asus->tpd_led_work, tpd_led_update);

		asus->tpd_led.name = "asus::touchpad";
		asus->tpd_led.brightness_set = tpd_led_set;
		asus->tpd_led.brightness_get = tpd_led_get;
		asus->tpd_led.max_brightness = 1;

		rv = led_classdev_register(&asus->platform_device->dev,
					   &asus->tpd_led);
		if (rv)
			goto error;
	}

	if (!kbd_led_read(asus, &led_val, NULL)) {
		asus->kbd_led_wk = led_val;
		asus->kbd_led.name = "asus::kbd_backlight";
		asus->kbd_led.flags = LED_BRIGHT_HW_CHANGED;
		asus->kbd_led.brightness_set = kbd_led_set;
		asus->kbd_led.brightness_get = kbd_led_get;
		asus->kbd_led.max_brightness = 3;

		rv = led_classdev_register(&asus->platform_device->dev,
					   &asus->kbd_led);
		if (rv)
			goto error;
	}

	if (wlan_led_presence(asus) && (asus->driver->quirks->wapf > 0)) {
		INIT_WORK(&asus->wlan_led_work, wlan_led_update);

		asus->wlan_led.name = "asus::wlan";
		asus->wlan_led.brightness_set = wlan_led_set;
		if (!wlan_led_unknown_state(asus))
			asus->wlan_led.brightness_get = wlan_led_get;
		asus->wlan_led.flags = LED_CORE_SUSPENDRESUME;
		asus->wlan_led.max_brightness = 1;
		asus->wlan_led.default_trigger = "asus-wlan";

		rv = led_classdev_register(&asus->platform_device->dev,
					   &asus->wlan_led);
		if (rv)
			goto error;
	}

	if (lightbar_led_presence(asus)) {
		INIT_WORK(&asus->lightbar_led_work, lightbar_led_update);

		asus->lightbar_led.name = "asus::lightbar";
		asus->lightbar_led.brightness_set = lightbar_led_set;
		asus->lightbar_led.brightness_get = lightbar_led_get;
		asus->lightbar_led.max_brightness = 1;

		rv = led_classdev_register(&asus->platform_device->dev,
					   &asus->lightbar_led);
	}

error:
	if (rv)
		asus_wmi_led_exit(asus);

	return rv;
}

/* RF *************************************************************************/

/*
 * PCI hotplug (for wlan rfkill)
 */
static bool asus_wlan_rfkill_blocked(struct asus_wmi *asus)
{
	int result = asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_WLAN);

	if (result < 0)
		return false;
	return !result;
}

static void asus_rfkill_hotplug(struct asus_wmi *asus)
{
	struct pci_dev *dev;
	struct pci_bus *bus;
	bool blocked;
	bool absent;
	u32 l;

	mutex_lock(&asus->wmi_lock);
	blocked = asus_wlan_rfkill_blocked(asus);
	mutex_unlock(&asus->wmi_lock);

	mutex_lock(&asus->hotplug_lock);
	pci_lock_rescan_remove();

	if (asus->wlan.rfkill)
		rfkill_set_sw_state(asus->wlan.rfkill, blocked);

	if (asus->hotplug_slot.ops) {
		bus = pci_find_bus(0, 1);
		if (!bus) {
			pr_warn("Unable to find PCI bus 1?\n");
			goto out_unlock;
		}

		if (pci_bus_read_config_dword(bus, 0, PCI_VENDOR_ID, &l)) {
			pr_err("Unable to read PCI config space?\n");
			goto out_unlock;
		}
		absent = (l == 0xffffffff);

		if (blocked != absent) {
			pr_warn("BIOS says wireless lan is %s, "
				"but the pci device is %s\n",
				blocked ? "blocked" : "unblocked",
				absent ? "absent" : "present");
			pr_warn("skipped wireless hotplug as probably "
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
				pci_bus_add_device(dev);
			}
		} else {
			dev = pci_get_slot(bus, 0);
			if (dev) {
				pci_stop_and_remove_bus_device(dev);
				pci_dev_put(dev);
			}
		}
	}

out_unlock:
	pci_unlock_rescan_remove();
	mutex_unlock(&asus->hotplug_lock);
}

static void asus_rfkill_notify(acpi_handle handle, u32 event, void *data)
{
	struct asus_wmi *asus = data;

	if (event != ACPI_NOTIFY_BUS_CHECK)
		return;

	/*
	 * We can't call directly asus_rfkill_hotplug because most
	 * of the time WMBC is still being executed and not reetrant.
	 * There is currently no way to tell ACPICA that  we want this
	 * method to be serialized, we schedule a asus_rfkill_hotplug
	 * call later, in a safer context.
	 */
	queue_work(asus->hotplug_workqueue, &asus->hotplug_work);
}

static int asus_register_rfkill_notifier(struct asus_wmi *asus, char *node)
{
	acpi_status status;
	acpi_handle handle;

	status = acpi_get_handle(NULL, node, &handle);

	if (ACPI_SUCCESS(status)) {
		status = acpi_install_notify_handler(handle,
						     ACPI_SYSTEM_NOTIFY,
						     asus_rfkill_notify, asus);
		if (ACPI_FAILURE(status))
			pr_warn("Failed to register notify on %s\n", node);
	} else
		return -ENODEV;

	return 0;
}

static void asus_unregister_rfkill_notifier(struct asus_wmi *asus, char *node)
{
	acpi_status status = AE_OK;
	acpi_handle handle;

	status = acpi_get_handle(NULL, node, &handle);

	if (ACPI_SUCCESS(status)) {
		status = acpi_remove_notify_handler(handle,
						    ACPI_SYSTEM_NOTIFY,
						    asus_rfkill_notify);
		if (ACPI_FAILURE(status))
			pr_err("Error removing rfkill notify handler %s\n",
			       node);
	}
}

static int asus_get_adapter_status(struct hotplug_slot *hotplug_slot,
				   u8 *value)
{
	struct asus_wmi *asus = container_of(hotplug_slot,
					     struct asus_wmi, hotplug_slot);
	int result = asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_WLAN);

	if (result < 0)
		return result;

	*value = !!result;
	return 0;
}

static const struct hotplug_slot_ops asus_hotplug_slot_ops = {
	.get_adapter_status = asus_get_adapter_status,
	.get_power_status = asus_get_adapter_status,
};

static void asus_hotplug_work(struct work_struct *work)
{
	struct asus_wmi *asus;

	asus = container_of(work, struct asus_wmi, hotplug_work);
	asus_rfkill_hotplug(asus);
}

static int asus_setup_pci_hotplug(struct asus_wmi *asus)
{
	int ret = -ENOMEM;
	struct pci_bus *bus = pci_find_bus(0, 1);

	if (!bus) {
		pr_err("Unable to find wifi PCI bus\n");
		return -ENODEV;
	}

	asus->hotplug_workqueue =
	    create_singlethread_workqueue("hotplug_workqueue");
	if (!asus->hotplug_workqueue)
		goto error_workqueue;

	INIT_WORK(&asus->hotplug_work, asus_hotplug_work);

	asus->hotplug_slot.ops = &asus_hotplug_slot_ops;

	ret = pci_hp_register(&asus->hotplug_slot, bus, 0, "asus-wifi");
	if (ret) {
		pr_err("Unable to register hotplug slot - %d\n", ret);
		goto error_register;
	}

	return 0;

error_register:
	asus->hotplug_slot.ops = NULL;
	destroy_workqueue(asus->hotplug_workqueue);
error_workqueue:
	return ret;
}

/*
 * Rfkill devices
 */
static int asus_rfkill_set(void *data, bool blocked)
{
	struct asus_rfkill *priv = data;
	u32 ctrl_param = !blocked;
	u32 dev_id = priv->dev_id;

	/*
	 * If the user bit is set, BIOS can't set and record the wlan status,
	 * it will report the value read from id ASUS_WMI_DEVID_WLAN_LED
	 * while we query the wlan status through WMI(ASUS_WMI_DEVID_WLAN).
	 * So, we have to record wlan status in id ASUS_WMI_DEVID_WLAN_LED
	 * while setting the wlan status through WMI.
	 * This is also the behavior that windows app will do.
	 */
	if ((dev_id == ASUS_WMI_DEVID_WLAN) &&
	     priv->asus->driver->wlan_ctrl_by_user)
		dev_id = ASUS_WMI_DEVID_WLAN_LED;

	return asus_wmi_set_devstate(dev_id, ctrl_param, NULL);
}

static void asus_rfkill_query(struct rfkill *rfkill, void *data)
{
	struct asus_rfkill *priv = data;
	int result;

	result = asus_wmi_get_devstate_simple(priv->asus, priv->dev_id);

	if (result < 0)
		return;

	rfkill_set_sw_state(priv->rfkill, !result);
}

static int asus_rfkill_wlan_set(void *data, bool blocked)
{
	struct asus_rfkill *priv = data;
	struct asus_wmi *asus = priv->asus;
	int ret;

	/*
	 * This handler is enabled only if hotplug is enabled.
	 * In this case, the asus_wmi_set_devstate() will
	 * trigger a wmi notification and we need to wait
	 * this call to finish before being able to call
	 * any wmi method
	 */
	mutex_lock(&asus->wmi_lock);
	ret = asus_rfkill_set(data, blocked);
	mutex_unlock(&asus->wmi_lock);
	return ret;
}

static const struct rfkill_ops asus_rfkill_wlan_ops = {
	.set_block = asus_rfkill_wlan_set,
	.query = asus_rfkill_query,
};

static const struct rfkill_ops asus_rfkill_ops = {
	.set_block = asus_rfkill_set,
	.query = asus_rfkill_query,
};

static int asus_new_rfkill(struct asus_wmi *asus,
			   struct asus_rfkill *arfkill,
			   const char *name, enum rfkill_type type, int dev_id)
{
	int result = asus_wmi_get_devstate_simple(asus, dev_id);
	struct rfkill **rfkill = &arfkill->rfkill;

	if (result < 0)
		return result;

	arfkill->dev_id = dev_id;
	arfkill->asus = asus;

	if (dev_id == ASUS_WMI_DEVID_WLAN &&
	    asus->driver->quirks->hotplug_wireless)
		*rfkill = rfkill_alloc(name, &asus->platform_device->dev, type,
				       &asus_rfkill_wlan_ops, arfkill);
	else
		*rfkill = rfkill_alloc(name, &asus->platform_device->dev, type,
				       &asus_rfkill_ops, arfkill);

	if (!*rfkill)
		return -EINVAL;

	if ((dev_id == ASUS_WMI_DEVID_WLAN) &&
			(asus->driver->quirks->wapf > 0))
		rfkill_set_led_trigger_name(*rfkill, "asus-wlan");

	rfkill_init_sw_state(*rfkill, !result);
	result = rfkill_register(*rfkill);
	if (result) {
		rfkill_destroy(*rfkill);
		*rfkill = NULL;
		return result;
	}
	return 0;
}

static void asus_wmi_rfkill_exit(struct asus_wmi *asus)
{
	if (asus->driver->wlan_ctrl_by_user && ashs_present())
		return;

	asus_unregister_rfkill_notifier(asus, "\\_SB.PCI0.P0P5");
	asus_unregister_rfkill_notifier(asus, "\\_SB.PCI0.P0P6");
	asus_unregister_rfkill_notifier(asus, "\\_SB.PCI0.P0P7");
	if (asus->wlan.rfkill) {
		rfkill_unregister(asus->wlan.rfkill);
		rfkill_destroy(asus->wlan.rfkill);
		asus->wlan.rfkill = NULL;
	}
	/*
	 * Refresh pci hotplug in case the rfkill state was changed after
	 * asus_unregister_rfkill_notifier()
	 */
	asus_rfkill_hotplug(asus);
	if (asus->hotplug_slot.ops)
		pci_hp_deregister(&asus->hotplug_slot);
	if (asus->hotplug_workqueue)
		destroy_workqueue(asus->hotplug_workqueue);

	if (asus->bluetooth.rfkill) {
		rfkill_unregister(asus->bluetooth.rfkill);
		rfkill_destroy(asus->bluetooth.rfkill);
		asus->bluetooth.rfkill = NULL;
	}
	if (asus->wimax.rfkill) {
		rfkill_unregister(asus->wimax.rfkill);
		rfkill_destroy(asus->wimax.rfkill);
		asus->wimax.rfkill = NULL;
	}
	if (asus->wwan3g.rfkill) {
		rfkill_unregister(asus->wwan3g.rfkill);
		rfkill_destroy(asus->wwan3g.rfkill);
		asus->wwan3g.rfkill = NULL;
	}
	if (asus->gps.rfkill) {
		rfkill_unregister(asus->gps.rfkill);
		rfkill_destroy(asus->gps.rfkill);
		asus->gps.rfkill = NULL;
	}
	if (asus->uwb.rfkill) {
		rfkill_unregister(asus->uwb.rfkill);
		rfkill_destroy(asus->uwb.rfkill);
		asus->uwb.rfkill = NULL;
	}
}

static int asus_wmi_rfkill_init(struct asus_wmi *asus)
{
	int result = 0;

	mutex_init(&asus->hotplug_lock);
	mutex_init(&asus->wmi_lock);

	result = asus_new_rfkill(asus, &asus->wlan, "asus-wlan",
				 RFKILL_TYPE_WLAN, ASUS_WMI_DEVID_WLAN);

	if (result && result != -ENODEV)
		goto exit;

	result = asus_new_rfkill(asus, &asus->bluetooth,
				 "asus-bluetooth", RFKILL_TYPE_BLUETOOTH,
				 ASUS_WMI_DEVID_BLUETOOTH);

	if (result && result != -ENODEV)
		goto exit;

	result = asus_new_rfkill(asus, &asus->wimax, "asus-wimax",
				 RFKILL_TYPE_WIMAX, ASUS_WMI_DEVID_WIMAX);

	if (result && result != -ENODEV)
		goto exit;

	result = asus_new_rfkill(asus, &asus->wwan3g, "asus-wwan3g",
				 RFKILL_TYPE_WWAN, ASUS_WMI_DEVID_WWAN3G);

	if (result && result != -ENODEV)
		goto exit;

	result = asus_new_rfkill(asus, &asus->gps, "asus-gps",
				 RFKILL_TYPE_GPS, ASUS_WMI_DEVID_GPS);

	if (result && result != -ENODEV)
		goto exit;

	result = asus_new_rfkill(asus, &asus->uwb, "asus-uwb",
				 RFKILL_TYPE_UWB, ASUS_WMI_DEVID_UWB);

	if (result && result != -ENODEV)
		goto exit;

	if (!asus->driver->quirks->hotplug_wireless)
		goto exit;

	result = asus_setup_pci_hotplug(asus);
	/*
	 * If we get -EBUSY then something else is handling the PCI hotplug -
	 * don't fail in this case
	 */
	if (result == -EBUSY)
		result = 0;

	asus_register_rfkill_notifier(asus, "\\_SB.PCI0.P0P5");
	asus_register_rfkill_notifier(asus, "\\_SB.PCI0.P0P6");
	asus_register_rfkill_notifier(asus, "\\_SB.PCI0.P0P7");
	/*
	 * Refresh pci hotplug in case the rfkill state was changed during
	 * setup.
	 */
	asus_rfkill_hotplug(asus);

exit:
	if (result && result != -ENODEV)
		asus_wmi_rfkill_exit(asus);

	if (result == -ENODEV)
		result = 0;

	return result;
}

/* Quirks *********************************************************************/

static void asus_wmi_set_xusb2pr(struct asus_wmi *asus)
{
	struct pci_dev *xhci_pdev;
	u32 orig_ports_available;
	u32 ports_available = asus->driver->quirks->xusb2pr;

	xhci_pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
			PCI_DEVICE_ID_INTEL_LYNXPOINT_LP_XHCI,
			NULL);

	if (!xhci_pdev)
		return;

	pci_read_config_dword(xhci_pdev, USB_INTEL_XUSB2PR,
				&orig_ports_available);

	pci_write_config_dword(xhci_pdev, USB_INTEL_XUSB2PR,
				cpu_to_le32(ports_available));

	pr_info("set USB_INTEL_XUSB2PR old: 0x%04x, new: 0x%04x\n",
			orig_ports_available, ports_available);
}

/*
 * Some devices dont support or have borcken get_als method
 * but still support set method.
 */
static void asus_wmi_set_als(void)
{
	asus_wmi_set_devstate(ASUS_WMI_DEVID_ALS_ENABLE, 1, NULL);
}

/* Hwmon device ***************************************************************/

static int asus_hwmon_agfn_fan_speed_read(struct asus_wmi *asus, int fan,
					  int *speed)
{
	struct fan_args args = {
		.agfn.len = sizeof(args),
		.agfn.mfun = ASUS_FAN_MFUN,
		.agfn.sfun = ASUS_FAN_SFUN_READ,
		.fan = fan,
		.speed = 0,
	};
	struct acpi_buffer input = { (acpi_size) sizeof(args), &args };
	int status;

	if (fan != 1)
		return -EINVAL;

	status = asus_wmi_evaluate_method_agfn(input);

	if (status || args.agfn.err)
		return -ENXIO;

	if (speed)
		*speed = args.speed;

	return 0;
}

static int asus_hwmon_agfn_fan_speed_write(struct asus_wmi *asus, int fan,
				     int *speed)
{
	struct fan_args args = {
		.agfn.len = sizeof(args),
		.agfn.mfun = ASUS_FAN_MFUN,
		.agfn.sfun = ASUS_FAN_SFUN_WRITE,
		.fan = fan,
		.speed = speed ?  *speed : 0,
	};
	struct acpi_buffer input = { (acpi_size) sizeof(args), &args };
	int status;

	/* 1: for setting 1st fan's speed 0: setting auto mode */
	if (fan != 1 && fan != 0)
		return -EINVAL;

	status = asus_wmi_evaluate_method_agfn(input);

	if (status || args.agfn.err)
		return -ENXIO;

	if (speed && fan == 1)
		asus->asus_hwmon_pwm = *speed;

	return 0;
}

/*
 * Check if we can read the speed of one fan. If true we assume we can also
 * control it.
 */
static int asus_hwmon_get_fan_number(struct asus_wmi *asus, int *num_fans)
{
	int status;
	int speed = 0;

	*num_fans = 0;

	status = asus_hwmon_agfn_fan_speed_read(asus, 1, &speed);
	if (!status)
		*num_fans = 1;

	return 0;
}

static int asus_hwmon_fan_set_auto(struct asus_wmi *asus)
{
	int status;

	status = asus_hwmon_agfn_fan_speed_write(asus, 0, NULL);
	if (status)
		return -ENXIO;

	asus->asus_hwmon_fan_manual_mode = false;

	return 0;
}

static int asus_hwmon_fan_rpm_show(struct device *dev, int fan)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int value;
	int ret;

	/* no speed readable on manual mode */
	if (asus->asus_hwmon_fan_manual_mode)
		return -ENXIO;

	ret = asus_hwmon_agfn_fan_speed_read(asus, fan+1, &value);
	if (ret) {
		pr_warn("reading fan speed failed: %d\n", ret);
		return -ENXIO;
	}

	return value;
}

static void asus_hwmon_pwm_show(struct asus_wmi *asus, int fan, int *value)
{
	int err;

	if (asus->asus_hwmon_pwm >= 0) {
		*value = asus->asus_hwmon_pwm;
		return;
	}

	err = asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_FAN_CTRL, value);
	if (err < 0)
		return;

	*value &= 0xFF;

	if (*value == 1) /* Low Speed */
		*value = 85;
	else if (*value == 2)
		*value = 170;
	else if (*value == 3)
		*value = 255;
	else if (*value) {
		pr_err("Unknown fan speed %#x\n", *value);
		*value = -1;
	}
}

static ssize_t pwm1_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int value;

	asus_hwmon_pwm_show(asus, 0, &value);

	return sprintf(buf, "%d\n", value);
}

static ssize_t pwm1_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count) {
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int value;
	int state;
	int ret;

	ret = kstrtouint(buf, 10, &value);

	if (ret)
		return ret;

	value = clamp(value, 0, 255);

	state = asus_hwmon_agfn_fan_speed_write(asus, 1, &value);
	if (state)
		pr_warn("Setting fan speed failed: %d\n", state);
	else
		asus->asus_hwmon_fan_manual_mode = true;

	return count;
}

static ssize_t fan1_input_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int value = asus_hwmon_fan_rpm_show(dev, 0);

	return sprintf(buf, "%d\n", value < 0 ? -1 : value*100);

}

static ssize_t pwm1_enable_show(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);

	if (asus->asus_hwmon_fan_manual_mode)
		return sprintf(buf, "%d\n", ASUS_FAN_CTRL_MANUAL);

	return sprintf(buf, "%d\n", ASUS_FAN_CTRL_AUTO);
}

static ssize_t pwm1_enable_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t count)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int status = 0;
	int state;
	int ret;

	ret = kstrtouint(buf, 10, &state);

	if (ret)
		return ret;

	if (state == ASUS_FAN_CTRL_MANUAL)
		asus->asus_hwmon_fan_manual_mode = true;
	else
		status = asus_hwmon_fan_set_auto(asus);

	if (status)
		return status;

	return count;
}

static ssize_t fan1_label_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	return sprintf(buf, "%s\n", ASUS_FAN_DESC);
}

static ssize_t asus_hwmon_temp1(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	u32 value;
	int err;

	err = asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_THERMAL_CTRL, &value);

	if (err < 0)
		return err;

	value = DECI_KELVIN_TO_CELSIUS((value & 0xFFFF)) * 1000;

	return sprintf(buf, "%d\n", value);
}

/* Fan1 */
static DEVICE_ATTR_RW(pwm1);
static DEVICE_ATTR_RW(pwm1_enable);
static DEVICE_ATTR_RO(fan1_input);
static DEVICE_ATTR_RO(fan1_label);

/* Temperature */
static DEVICE_ATTR(temp1_input, S_IRUGO, asus_hwmon_temp1, NULL);

static struct attribute *hwmon_attributes[] = {
	&dev_attr_pwm1.attr,
	&dev_attr_pwm1_enable.attr,
	&dev_attr_fan1_input.attr,
	&dev_attr_fan1_label.attr,

	&dev_attr_temp1_input.attr,
	NULL
};

static umode_t asus_hwmon_sysfs_is_visible(struct kobject *kobj,
					  struct attribute *attr, int idx)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct platform_device *pdev = to_platform_device(dev->parent);
	struct asus_wmi *asus = platform_get_drvdata(pdev);
	int dev_id = -1;
	int fan_attr = -1;
	u32 value = ASUS_WMI_UNSUPPORTED_METHOD;
	bool ok = true;

	if (attr == &dev_attr_pwm1.attr)
		dev_id = ASUS_WMI_DEVID_FAN_CTRL;
	else if (attr == &dev_attr_temp1_input.attr)
		dev_id = ASUS_WMI_DEVID_THERMAL_CTRL;


	if (attr == &dev_attr_fan1_input.attr
	    || attr == &dev_attr_fan1_label.attr
	    || attr == &dev_attr_pwm1.attr
	    || attr == &dev_attr_pwm1_enable.attr) {
		fan_attr = 1;
	}

	if (dev_id != -1) {
		int err = asus_wmi_get_devstate(asus, dev_id, &value);

		if (err < 0 && fan_attr == -1)
			return 0; /* can't return negative here */
	}

	if (dev_id == ASUS_WMI_DEVID_FAN_CTRL) {
		/*
		 * We need to find a better way, probably using sfun,
		 * bits or spec ...
		 * Currently we disable it if:
		 * - ASUS_WMI_UNSUPPORTED_METHOD is returned
		 * - reverved bits are non-zero
		 * - sfun and presence bit are not set
		 */
		if (value == ASUS_WMI_UNSUPPORTED_METHOD || value & 0xFFF80000
		    || (!asus->sfun && !(value & ASUS_WMI_DSTS_PRESENCE_BIT)))
			ok = false;
		else
			ok = fan_attr <= asus->asus_hwmon_num_fans;
	} else if (dev_id == ASUS_WMI_DEVID_THERMAL_CTRL) {
		/*
		 * If the temperature value in deci-Kelvin is near the absolute
		 * zero temperature, something is clearly wrong
		 */
		if (value == 0 || value == 1)
			ok = false;
	} else if (fan_attr <= asus->asus_hwmon_num_fans && fan_attr != -1) {
		ok = true;
	} else {
		ok = false;
	}

	return ok ? attr->mode : 0;
}

static const struct attribute_group hwmon_attribute_group = {
	.is_visible = asus_hwmon_sysfs_is_visible,
	.attrs = hwmon_attributes
};
__ATTRIBUTE_GROUPS(hwmon_attribute);

static int asus_wmi_hwmon_init(struct asus_wmi *asus)
{
	struct device *dev = &asus->platform_device->dev;
	struct device *hwmon;

	hwmon = devm_hwmon_device_register_with_groups(dev, "asus", asus,
			hwmon_attribute_groups);

	if (IS_ERR(hwmon)) {
		pr_err("Could not register asus hwmon device\n");
		return PTR_ERR(hwmon);
	}
	return 0;
}

static int asus_wmi_fan_init(struct asus_wmi *asus)
{
	int status;

	asus->asus_hwmon_pwm = -1;
	asus->asus_hwmon_num_fans = -1;
	asus->asus_hwmon_fan_manual_mode = false;

	status = asus_hwmon_get_fan_number(asus, &asus->asus_hwmon_num_fans);
	if (status) {
		asus->asus_hwmon_num_fans = 0;
		pr_warn("Could not determine number of fans: %d\n", status);
		return -ENXIO;
	}

	pr_info("Number of fans: %d\n", asus->asus_hwmon_num_fans);
	return 0;
}

/* Fan mode *******************************************************************/

static int fan_mode_check_present(struct asus_wmi *asus)
{
	u32 result;
	int err;

	asus->fan_mode_available = false;

	err = asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_FAN_MODE, &result);
	if (err) {
		if (err == -ENODEV)
			return 0;
		else
			return err;
	}

	if ((result & ASUS_WMI_DSTS_PRESENCE_BIT) &&
			(result & ASUS_FAN_MODES_MASK)) {
		asus->fan_mode_available = true;
		asus->fan_mode_mask = result & ASUS_FAN_MODES_MASK;
	}

	return 0;
}

static int fan_mode_write(struct asus_wmi *asus)
{
	int err;
	u8 value;
	u32 retval;

	value = asus->fan_mode;

	pr_info("Set fan mode: %u\n", value);
	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_FAN_MODE, value, &retval);

	if (err) {
		pr_warn("Failed to set fan mode: %d\n", err);
		return err;
	}

	if (retval != 1) {
		pr_warn("Failed to set fan mode (retval): 0x%x\n", retval);
		return -EIO;
	}

	return 0;
}

static int fan_mode_switch_next(struct asus_wmi *asus)
{
	if (asus->fan_mode == ASUS_FAN_MODE_NORMAL) {
		if (asus->fan_mode_mask & ASUS_FAN_MODE_OVERBOOST_MASK)
			asus->fan_mode = ASUS_FAN_MODE_OVERBOOST;
		else if (asus->fan_mode_mask & ASUS_FAN_MODE_SILENT_MASK)
			asus->fan_mode = ASUS_FAN_MODE_SILENT;
	} else if (asus->fan_mode == ASUS_FAN_MODE_OVERBOOST) {
		if (asus->fan_mode_mask & ASUS_FAN_MODE_SILENT_MASK)
			asus->fan_mode = ASUS_FAN_MODE_SILENT;
		else
			asus->fan_mode = ASUS_FAN_MODE_NORMAL;
	} else {
		asus->fan_mode = ASUS_FAN_MODE_NORMAL;
	}

	return fan_mode_write(asus);
}

static ssize_t fan_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", asus->fan_mode);
}

static ssize_t fan_mode_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int result;
	u8 new_mode;

	struct asus_wmi *asus = dev_get_drvdata(dev);

	result = kstrtou8(buf, 10, &new_mode);
	if (result < 0) {
		pr_warn("Trying to store invalid value\n");
		return result;
	}

	if (new_mode == ASUS_FAN_MODE_OVERBOOST) {
		if (!(asus->fan_mode_mask & ASUS_FAN_MODE_OVERBOOST_MASK))
			return -EINVAL;
	} else if (new_mode == ASUS_FAN_MODE_SILENT) {
		if (!(asus->fan_mode_mask & ASUS_FAN_MODE_SILENT_MASK))
			return -EINVAL;
	} else if (new_mode != ASUS_FAN_MODE_NORMAL) {
		return -EINVAL;
	}

	asus->fan_mode = new_mode;
	fan_mode_write(asus);

	return result;
}

// Fan mode: 0 - normal, 1 - overboost, 2 - silent
static DEVICE_ATTR_RW(fan_mode);

/* Backlight ******************************************************************/

static int read_backlight_power(struct asus_wmi *asus)
{
	int ret;
	if (asus->driver->quirks->store_backlight_power)
		ret = !asus->driver->panel_power;
	else
		ret = asus_wmi_get_devstate_simple(asus,
						   ASUS_WMI_DEVID_BACKLIGHT);

	if (ret < 0)
		return ret;

	return ret ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN;
}

static int read_brightness_max(struct asus_wmi *asus)
{
	u32 retval;
	int err;

	err = asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_BRIGHTNESS, &retval);

	if (err < 0)
		return err;

	retval = retval & ASUS_WMI_DSTS_MAX_BRIGTH_MASK;
	retval >>= 8;

	if (!retval)
		return -ENODEV;

	return retval;
}

static int read_brightness(struct backlight_device *bd)
{
	struct asus_wmi *asus = bl_get_data(bd);
	u32 retval;
	int err;

	err = asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_BRIGHTNESS, &retval);

	if (err < 0)
		return err;

	return retval & ASUS_WMI_DSTS_BRIGHTNESS_MASK;
}

static u32 get_scalar_command(struct backlight_device *bd)
{
	struct asus_wmi *asus = bl_get_data(bd);
	u32 ctrl_param = 0;

	if ((asus->driver->brightness < bd->props.brightness) ||
	    bd->props.brightness == bd->props.max_brightness)
		ctrl_param = 0x00008001;
	else if ((asus->driver->brightness > bd->props.brightness) ||
		 bd->props.brightness == 0)
		ctrl_param = 0x00008000;

	asus->driver->brightness = bd->props.brightness;

	return ctrl_param;
}

static int update_bl_status(struct backlight_device *bd)
{
	struct asus_wmi *asus = bl_get_data(bd);
	u32 ctrl_param;
	int power, err = 0;

	power = read_backlight_power(asus);
	if (power != -ENODEV && bd->props.power != power) {
		ctrl_param = !!(bd->props.power == FB_BLANK_UNBLANK);
		err = asus_wmi_set_devstate(ASUS_WMI_DEVID_BACKLIGHT,
					    ctrl_param, NULL);
		if (asus->driver->quirks->store_backlight_power)
			asus->driver->panel_power = bd->props.power;

		/* When using scalar brightness, updating the brightness
		 * will mess with the backlight power */
		if (asus->driver->quirks->scalar_panel_brightness)
			return err;
	}

	if (asus->driver->quirks->scalar_panel_brightness)
		ctrl_param = get_scalar_command(bd);
	else
		ctrl_param = bd->props.brightness;

	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_BRIGHTNESS,
				    ctrl_param, NULL);

	return err;
}

static const struct backlight_ops asus_wmi_bl_ops = {
	.get_brightness = read_brightness,
	.update_status = update_bl_status,
};

static int asus_wmi_backlight_notify(struct asus_wmi *asus, int code)
{
	struct backlight_device *bd = asus->backlight_device;
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

static int asus_wmi_backlight_init(struct asus_wmi *asus)
{
	struct backlight_device *bd;
	struct backlight_properties props;
	int max;
	int power;

	max = read_brightness_max(asus);
	if (max < 0)
		return max;

	power = read_backlight_power(asus);

	if (power == -ENODEV)
		power = FB_BLANK_UNBLANK;
	else if (power < 0)
		return power;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = max;
	bd = backlight_device_register(asus->driver->name,
				       &asus->platform_device->dev, asus,
				       &asus_wmi_bl_ops, &props);
	if (IS_ERR(bd)) {
		pr_err("Could not register backlight device\n");
		return PTR_ERR(bd);
	}

	asus->backlight_device = bd;

	if (asus->driver->quirks->store_backlight_power)
		asus->driver->panel_power = power;

	bd->props.brightness = read_brightness(bd);
	bd->props.power = power;
	backlight_update_status(bd);

	asus->driver->brightness = bd->props.brightness;

	return 0;
}

static void asus_wmi_backlight_exit(struct asus_wmi *asus)
{
	backlight_device_unregister(asus->backlight_device);

	asus->backlight_device = NULL;
}

static int is_display_toggle(int code)
{
	/* display toggle keys */
	if ((code >= 0x61 && code <= 0x67) ||
	    (code >= 0x8c && code <= 0x93) ||
	    (code >= 0xa0 && code <= 0xa7) ||
	    (code >= 0xd0 && code <= 0xd5))
		return 1;

	return 0;
}

/* Fn-lock ********************************************************************/

static bool asus_wmi_has_fnlock_key(struct asus_wmi *asus)
{
	u32 result;

	asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_FNLOCK, &result);

	return (result & ASUS_WMI_DSTS_PRESENCE_BIT) &&
		!(result & ASUS_WMI_FNLOCK_BIOS_DISABLED);
}

static void asus_wmi_fnlock_update(struct asus_wmi *asus)
{
	int mode = asus->fnlock_locked;

	asus_wmi_set_devstate(ASUS_WMI_DEVID_FNLOCK, mode, NULL);
}

/* WMI events *****************************************************************/

static int asus_wmi_get_event_code(u32 value)
{
	struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	int code;

	status = wmi_get_event_data(value, &response);
	if (ACPI_FAILURE(status)) {
		pr_warn("Failed to get WMI notify code: %s\n",
				acpi_format_exception(status));
		return -EIO;
	}

	obj = (union acpi_object *)response.pointer;

	if (obj && obj->type == ACPI_TYPE_INTEGER)
		code = (int)(obj->integer.value & WMI_EVENT_MASK);
	else
		code = -EIO;

	kfree(obj);
	return code;
}

static void asus_wmi_handle_event_code(int code, struct asus_wmi *asus)
{
	int orig_code;
	unsigned int key_value = 1;
	bool autorelease = 1;

	orig_code = code;

	if (asus->driver->key_filter) {
		asus->driver->key_filter(asus->driver, &code, &key_value,
					 &autorelease);
		if (code == ASUS_WMI_KEY_IGNORE)
			return;
	}

	if (code >= NOTIFY_BRNUP_MIN && code <= NOTIFY_BRNUP_MAX)
		code = ASUS_WMI_BRN_UP;
	else if (code >= NOTIFY_BRNDOWN_MIN && code <= NOTIFY_BRNDOWN_MAX)
		code = ASUS_WMI_BRN_DOWN;

	if (code == ASUS_WMI_BRN_DOWN || code == ASUS_WMI_BRN_UP) {
		if (acpi_video_get_backlight_type() == acpi_backlight_vendor) {
			asus_wmi_backlight_notify(asus, orig_code);
			return;
		}
	}

	if (code == NOTIFY_KBD_BRTUP) {
		kbd_led_set_by_kbd(asus, asus->kbd_led_wk + 1);
		return;
	}
	if (code == NOTIFY_KBD_BRTDWN) {
		kbd_led_set_by_kbd(asus, asus->kbd_led_wk - 1);
		return;
	}
	if (code == NOTIFY_KBD_BRTTOGGLE) {
		if (asus->kbd_led_wk == asus->kbd_led.max_brightness)
			kbd_led_set_by_kbd(asus, 0);
		else
			kbd_led_set_by_kbd(asus, asus->kbd_led_wk + 1);
		return;
	}

	if (code == NOTIFY_FNLOCK_TOGGLE) {
		asus->fnlock_locked = !asus->fnlock_locked;
		asus_wmi_fnlock_update(asus);
		return;
	}

	if (asus->fan_mode_available && code == NOTIFY_KBD_FBM) {
		fan_mode_switch_next(asus);
		return;
	}

	if (is_display_toggle(code) && asus->driver->quirks->no_display_toggle)
		return;

	if (!sparse_keymap_report_event(asus->inputdev, code,
					key_value, autorelease))
		pr_info("Unknown key %x pressed\n", code);
}

static void asus_wmi_notify(u32 value, void *context)
{
	struct asus_wmi *asus = context;
	int code;
	int i;

	for (i = 0; i < WMI_EVENT_QUEUE_SIZE + 1; i++) {
		code = asus_wmi_get_event_code(value);

		if (code < 0) {
			pr_warn("Failed to get notify code: %d\n", code);
			return;
		}

		if (code == WMI_EVENT_QUEUE_END || code == WMI_EVENT_MASK)
			return;

		asus_wmi_handle_event_code(code, asus);

		/*
		 * Double check that queue is present:
		 * ATK (with queue) uses 0xff, ASUSWMI (without) 0xd2.
		 */
		if (!asus->wmi_event_queue || value != WMI_EVENT_VALUE_ATK)
			return;
	}

	pr_warn("Failed to process event queue, last code: 0x%x\n", code);
}

static int asus_wmi_notify_queue_flush(struct asus_wmi *asus)
{
	int code;
	int i;

	for (i = 0; i < WMI_EVENT_QUEUE_SIZE + 1; i++) {
		code = asus_wmi_get_event_code(WMI_EVENT_VALUE_ATK);

		if (code < 0) {
			pr_warn("Failed to get event during flush: %d\n", code);
			return code;
		}

		if (code == WMI_EVENT_QUEUE_END || code == WMI_EVENT_MASK)
			return 0;
	}

	pr_warn("Failed to flush event queue\n");
	return -EIO;
}

/* Sysfs **********************************************************************/

static int parse_arg(const char *buf, unsigned long count, int *val)
{
	if (!count)
		return 0;
	if (sscanf(buf, "%i", val) != 1)
		return -EINVAL;
	return count;
}

static ssize_t store_sys_wmi(struct asus_wmi *asus, int devid,
			     const char *buf, size_t count)
{
	u32 retval;
	int rv, err, value;

	value = asus_wmi_get_devstate_simple(asus, devid);
	if (value < 0)
		return value;

	rv = parse_arg(buf, count, &value);
	err = asus_wmi_set_devstate(devid, value, &retval);

	if (err < 0)
		return err;

	return rv;
}

static ssize_t show_sys_wmi(struct asus_wmi *asus, int devid, char *buf)
{
	int value = asus_wmi_get_devstate_simple(asus, devid);

	if (value < 0)
		return value;

	return sprintf(buf, "%d\n", value);
}

#define ASUS_WMI_CREATE_DEVICE_ATTR(_name, _mode, _cm)			\
	static ssize_t show_##_name(struct device *dev,			\
				    struct device_attribute *attr,	\
				    char *buf)				\
	{								\
		struct asus_wmi *asus = dev_get_drvdata(dev);		\
									\
		return show_sys_wmi(asus, _cm, buf);			\
	}								\
	static ssize_t store_##_name(struct device *dev,		\
				     struct device_attribute *attr,	\
				     const char *buf, size_t count)	\
	{								\
		struct asus_wmi *asus = dev_get_drvdata(dev);		\
									\
		return store_sys_wmi(asus, _cm, buf, count);		\
	}								\
	static struct device_attribute dev_attr_##_name = {		\
		.attr = {						\
			.name = __stringify(_name),			\
			.mode = _mode },				\
		.show   = show_##_name,					\
		.store  = store_##_name,				\
	}

ASUS_WMI_CREATE_DEVICE_ATTR(touchpad, 0644, ASUS_WMI_DEVID_TOUCHPAD);
ASUS_WMI_CREATE_DEVICE_ATTR(camera, 0644, ASUS_WMI_DEVID_CAMERA);
ASUS_WMI_CREATE_DEVICE_ATTR(cardr, 0644, ASUS_WMI_DEVID_CARDREADER);
ASUS_WMI_CREATE_DEVICE_ATTR(lid_resume, 0644, ASUS_WMI_DEVID_LID_RESUME);
ASUS_WMI_CREATE_DEVICE_ATTR(als_enable, 0644, ASUS_WMI_DEVID_ALS_ENABLE);

static ssize_t cpufv_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	int value, rv;

	if (!count || sscanf(buf, "%i", &value) != 1)
		return -EINVAL;
	if (value < 0 || value > 2)
		return -EINVAL;

	rv = asus_wmi_evaluate_method(ASUS_WMI_METHODID_CFVS, value, 0, NULL);
	if (rv < 0)
		return rv;

	return count;
}

static DEVICE_ATTR_WO(cpufv);

static struct attribute *platform_attributes[] = {
	&dev_attr_cpufv.attr,
	&dev_attr_camera.attr,
	&dev_attr_cardr.attr,
	&dev_attr_touchpad.attr,
	&dev_attr_lid_resume.attr,
	&dev_attr_als_enable.attr,
	&dev_attr_fan_mode.attr,
	NULL
};

static umode_t asus_sysfs_is_visible(struct kobject *kobj,
				    struct attribute *attr, int idx)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct asus_wmi *asus = dev_get_drvdata(dev);
	bool ok = true;
	int devid = -1;

	if (attr == &dev_attr_camera.attr)
		devid = ASUS_WMI_DEVID_CAMERA;
	else if (attr == &dev_attr_cardr.attr)
		devid = ASUS_WMI_DEVID_CARDREADER;
	else if (attr == &dev_attr_touchpad.attr)
		devid = ASUS_WMI_DEVID_TOUCHPAD;
	else if (attr == &dev_attr_lid_resume.attr)
		devid = ASUS_WMI_DEVID_LID_RESUME;
	else if (attr == &dev_attr_als_enable.attr)
		devid = ASUS_WMI_DEVID_ALS_ENABLE;
	else if (attr == &dev_attr_fan_mode.attr)
		ok = asus->fan_mode_available;

	if (devid != -1)
		ok = !(asus_wmi_get_devstate_simple(asus, devid) < 0);

	return ok ? attr->mode : 0;
}

static const struct attribute_group platform_attribute_group = {
	.is_visible = asus_sysfs_is_visible,
	.attrs = platform_attributes
};

static void asus_wmi_sysfs_exit(struct platform_device *device)
{
	sysfs_remove_group(&device->dev.kobj, &platform_attribute_group);
}

static int asus_wmi_sysfs_init(struct platform_device *device)
{
	return sysfs_create_group(&device->dev.kobj, &platform_attribute_group);
}

/* Platform device ************************************************************/

static int asus_wmi_platform_init(struct asus_wmi *asus)
{
	struct device *dev = &asus->platform_device->dev;
	char *wmi_uid;
	int rv;

	/* INIT enable hotkeys on some models */
	if (!asus_wmi_evaluate_method(ASUS_WMI_METHODID_INIT, 0, 0, &rv))
		pr_info("Initialization: %#x\n", rv);

	/* We don't know yet what to do with this version... */
	if (!asus_wmi_evaluate_method(ASUS_WMI_METHODID_SPEC, 0, 0x9, &rv)) {
		pr_info("BIOS WMI version: %d.%d\n", rv >> 16, rv & 0xFF);
		asus->spec = rv;
	}

	/*
	 * The SFUN method probably allows the original driver to get the list
	 * of features supported by a given model. For now, 0x0100 or 0x0800
	 * bit signifies that the laptop is equipped with a Wi-Fi MiniPCI card.
	 * The significance of others is yet to be found.
	 */
	if (!asus_wmi_evaluate_method(ASUS_WMI_METHODID_SFUN, 0, 0, &rv)) {
		pr_info("SFUN value: %#x\n", rv);
		asus->sfun = rv;
	}

	/*
	 * Eee PC and Notebooks seems to have different method_id for DSTS,
	 * but it may also be related to the BIOS's SPEC.
	 * Note, on most Eeepc, there is no way to check if a method exist
	 * or note, while on notebooks, they returns 0xFFFFFFFE on failure,
	 * but once again, SPEC may probably be used for that kind of things.
	 *
	 * Additionally at least TUF Gaming series laptops return nothing for
	 * unknown methods, so the detection in this way is not possible.
	 *
	 * There is strong indication that only ACPI WMI devices that have _UID
	 * equal to "ASUSWMI" use DCTS whereas those with "ATK" use DSTS.
	 */
	wmi_uid = wmi_get_acpi_device_uid(ASUS_WMI_MGMT_GUID);
	if (!wmi_uid)
		return -ENODEV;

	if (!strcmp(wmi_uid, ASUS_ACPI_UID_ASUSWMI)) {
		dev_info(dev, "Detected ASUSWMI, use DCTS\n");
		asus->dsts_id = ASUS_WMI_METHODID_DCTS;
	} else {
		dev_info(dev, "Detected %s, not ASUSWMI, use DSTS\n", wmi_uid);
		asus->dsts_id = ASUS_WMI_METHODID_DSTS;
	}

	/*
	 * Some devices can have multiple event codes stored in a queue before
	 * the module load if it was unloaded intermittently after calling
	 * the INIT method (enables event handling). The WMI notify handler is
	 * expected to retrieve all event codes until a retrieved code equals
	 * queue end marker (One or Ones). Old codes are flushed from the queue
	 * upon module load. Not enabling this when it should be has minimal
	 * visible impact so fall back if anything goes wrong.
	 */
	wmi_uid = wmi_get_acpi_device_uid(asus->driver->event_guid);
	if (wmi_uid && !strcmp(wmi_uid, ASUS_ACPI_UID_ATK)) {
		dev_info(dev, "Detected ATK, enable event queue\n");

		if (!asus_wmi_notify_queue_flush(asus))
			asus->wmi_event_queue = true;
	}

	/* CWAP allow to define the behavior of the Fn+F2 key,
	 * this method doesn't seems to be present on Eee PCs */
	if (asus->driver->quirks->wapf >= 0)
		asus_wmi_set_devstate(ASUS_WMI_DEVID_CWAP,
				      asus->driver->quirks->wapf, NULL);

	return 0;
}

/* debugfs ********************************************************************/

struct asus_wmi_debugfs_node {
	struct asus_wmi *asus;
	char *name;
	int (*show) (struct seq_file *m, void *data);
};

static int show_dsts(struct seq_file *m, void *data)
{
	struct asus_wmi *asus = m->private;
	int err;
	u32 retval = -1;

	err = asus_wmi_get_devstate(asus, asus->debug.dev_id, &retval);

	if (err < 0)
		return err;

	seq_printf(m, "DSTS(%#x) = %#x\n", asus->debug.dev_id, retval);

	return 0;
}

static int show_devs(struct seq_file *m, void *data)
{
	struct asus_wmi *asus = m->private;
	int err;
	u32 retval = -1;

	err = asus_wmi_set_devstate(asus->debug.dev_id, asus->debug.ctrl_param,
				    &retval);

	if (err < 0)
		return err;

	seq_printf(m, "DEVS(%#x, %#x) = %#x\n", asus->debug.dev_id,
		   asus->debug.ctrl_param, retval);

	return 0;
}

static int show_call(struct seq_file *m, void *data)
{
	struct asus_wmi *asus = m->private;
	struct bios_args args = {
		.arg0 = asus->debug.dev_id,
		.arg1 = asus->debug.ctrl_param,
	};
	struct acpi_buffer input = { (acpi_size) sizeof(args), &args };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;

	status = wmi_evaluate_method(ASUS_WMI_MGMT_GUID,
				     0, asus->debug.method_id,
				     &input, &output);

	if (ACPI_FAILURE(status))
		return -EIO;

	obj = (union acpi_object *)output.pointer;
	if (obj && obj->type == ACPI_TYPE_INTEGER)
		seq_printf(m, "%#x(%#x, %#x) = %#x\n", asus->debug.method_id,
			   asus->debug.dev_id, asus->debug.ctrl_param,
			   (u32) obj->integer.value);
	else
		seq_printf(m, "%#x(%#x, %#x) = t:%d\n", asus->debug.method_id,
			   asus->debug.dev_id, asus->debug.ctrl_param,
			   obj ? obj->type : -1);

	kfree(obj);

	return 0;
}

static struct asus_wmi_debugfs_node asus_wmi_debug_files[] = {
	{NULL, "devs", show_devs},
	{NULL, "dsts", show_dsts},
	{NULL, "call", show_call},
};

static int asus_wmi_debugfs_open(struct inode *inode, struct file *file)
{
	struct asus_wmi_debugfs_node *node = inode->i_private;

	return single_open(file, node->show, node->asus);
}

static const struct file_operations asus_wmi_debugfs_io_ops = {
	.owner = THIS_MODULE,
	.open = asus_wmi_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void asus_wmi_debugfs_exit(struct asus_wmi *asus)
{
	debugfs_remove_recursive(asus->debug.root);
}

static void asus_wmi_debugfs_init(struct asus_wmi *asus)
{
	int i;

	asus->debug.root = debugfs_create_dir(asus->driver->name, NULL);

	debugfs_create_x32("method_id", S_IRUGO | S_IWUSR, asus->debug.root,
			   &asus->debug.method_id);

	debugfs_create_x32("dev_id", S_IRUGO | S_IWUSR, asus->debug.root,
			   &asus->debug.dev_id);

	debugfs_create_x32("ctrl_param", S_IRUGO | S_IWUSR, asus->debug.root,
			   &asus->debug.ctrl_param);

	for (i = 0; i < ARRAY_SIZE(asus_wmi_debug_files); i++) {
		struct asus_wmi_debugfs_node *node = &asus_wmi_debug_files[i];

		node->asus = asus;
		debugfs_create_file(node->name, S_IFREG | S_IRUGO,
				    asus->debug.root, node,
				    &asus_wmi_debugfs_io_ops);
	}
}

/* Init / exit ****************************************************************/

static int asus_wmi_add(struct platform_device *pdev)
{
	struct platform_driver *pdrv = to_platform_driver(pdev->dev.driver);
	struct asus_wmi_driver *wdrv = to_asus_wmi_driver(pdrv);
	struct asus_wmi *asus;
	const char *chassis_type;
	acpi_status status;
	int err;
	u32 result;

	asus = kzalloc(sizeof(struct asus_wmi), GFP_KERNEL);
	if (!asus)
		return -ENOMEM;

	asus->driver = wdrv;
	asus->platform_device = pdev;
	wdrv->platform_device = pdev;
	platform_set_drvdata(asus->platform_device, asus);

	if (wdrv->detect_quirks)
		wdrv->detect_quirks(asus->driver);

	err = asus_wmi_platform_init(asus);
	if (err)
		goto fail_platform;

	err = fan_mode_check_present(asus);
	if (err)
		goto fail_fan_mode;

	err = asus_wmi_sysfs_init(asus->platform_device);
	if (err)
		goto fail_sysfs;

	err = asus_wmi_input_init(asus);
	if (err)
		goto fail_input;

	err = asus_wmi_fan_init(asus); /* probably no problems on error */
	asus_hwmon_fan_set_auto(asus);

	err = asus_wmi_hwmon_init(asus);
	if (err)
		goto fail_hwmon;

	err = asus_wmi_led_init(asus);
	if (err)
		goto fail_leds;

	asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_WLAN, &result);
	if (result & (ASUS_WMI_DSTS_PRESENCE_BIT | ASUS_WMI_DSTS_USER_BIT))
		asus->driver->wlan_ctrl_by_user = 1;

	if (!(asus->driver->wlan_ctrl_by_user && ashs_present())) {
		err = asus_wmi_rfkill_init(asus);
		if (err)
			goto fail_rfkill;
	}

	if (asus->driver->quirks->wmi_force_als_set)
		asus_wmi_set_als();

	/* Some Asus desktop boards export an acpi-video backlight interface,
	   stop this from showing up */
	chassis_type = dmi_get_system_info(DMI_CHASSIS_TYPE);
	if (chassis_type && !strcmp(chassis_type, "3"))
		acpi_video_set_dmi_backlight_type(acpi_backlight_vendor);

	if (asus->driver->quirks->wmi_backlight_power)
		acpi_video_set_dmi_backlight_type(acpi_backlight_vendor);

	if (asus->driver->quirks->wmi_backlight_native)
		acpi_video_set_dmi_backlight_type(acpi_backlight_native);

	if (asus->driver->quirks->xusb2pr)
		asus_wmi_set_xusb2pr(asus);

	if (acpi_video_get_backlight_type() == acpi_backlight_vendor) {
		err = asus_wmi_backlight_init(asus);
		if (err && err != -ENODEV)
			goto fail_backlight;
	} else if (asus->driver->quirks->wmi_backlight_set_devstate)
		err = asus_wmi_set_devstate(ASUS_WMI_DEVID_BACKLIGHT, 2, NULL);

	if (asus_wmi_has_fnlock_key(asus)) {
		asus->fnlock_locked = true;
		asus_wmi_fnlock_update(asus);
	}

	status = wmi_install_notify_handler(asus->driver->event_guid,
					    asus_wmi_notify, asus);
	if (ACPI_FAILURE(status)) {
		pr_err("Unable to register notify handler - %d\n", status);
		err = -ENODEV;
		goto fail_wmi_handler;
	}

	asus_wmi_debugfs_init(asus);

	return 0;

fail_wmi_handler:
	asus_wmi_backlight_exit(asus);
fail_backlight:
	asus_wmi_rfkill_exit(asus);
fail_rfkill:
	asus_wmi_led_exit(asus);
fail_leds:
fail_hwmon:
	asus_wmi_input_exit(asus);
fail_input:
	asus_wmi_sysfs_exit(asus->platform_device);
fail_sysfs:
fail_fan_mode:
fail_platform:
	kfree(asus);
	return err;
}

static int asus_wmi_remove(struct platform_device *device)
{
	struct asus_wmi *asus;

	asus = platform_get_drvdata(device);
	wmi_remove_notify_handler(asus->driver->event_guid);
	asus_wmi_backlight_exit(asus);
	asus_wmi_input_exit(asus);
	asus_wmi_led_exit(asus);
	asus_wmi_rfkill_exit(asus);
	asus_wmi_debugfs_exit(asus);
	asus_wmi_sysfs_exit(asus->platform_device);
	asus_hwmon_fan_set_auto(asus);

	kfree(asus);
	return 0;
}

/* Platform driver - hibernate/resume callbacks *******************************/

static int asus_hotk_thaw(struct device *device)
{
	struct asus_wmi *asus = dev_get_drvdata(device);

	if (asus->wlan.rfkill) {
		bool wlan;

		/*
		 * Work around bios bug - acpi _PTS turns off the wireless led
		 * during suspend.  Normally it restores it on resume, but
		 * we should kick it ourselves in case hibernation is aborted.
		 */
		wlan = asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_WLAN);
		asus_wmi_set_devstate(ASUS_WMI_DEVID_WLAN, wlan, NULL);
	}

	return 0;
}

static int asus_hotk_resume(struct device *device)
{
	struct asus_wmi *asus = dev_get_drvdata(device);

	if (!IS_ERR_OR_NULL(asus->kbd_led.dev))
		kbd_led_update(asus);

	if (asus_wmi_has_fnlock_key(asus))
		asus_wmi_fnlock_update(asus);
	return 0;
}

static int asus_hotk_restore(struct device *device)
{
	struct asus_wmi *asus = dev_get_drvdata(device);
	int bl;

	/* Refresh both wlan rfkill state and pci hotplug */
	if (asus->wlan.rfkill)
		asus_rfkill_hotplug(asus);

	if (asus->bluetooth.rfkill) {
		bl = !asus_wmi_get_devstate_simple(asus,
						   ASUS_WMI_DEVID_BLUETOOTH);
		rfkill_set_sw_state(asus->bluetooth.rfkill, bl);
	}
	if (asus->wimax.rfkill) {
		bl = !asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_WIMAX);
		rfkill_set_sw_state(asus->wimax.rfkill, bl);
	}
	if (asus->wwan3g.rfkill) {
		bl = !asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_WWAN3G);
		rfkill_set_sw_state(asus->wwan3g.rfkill, bl);
	}
	if (asus->gps.rfkill) {
		bl = !asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_GPS);
		rfkill_set_sw_state(asus->gps.rfkill, bl);
	}
	if (asus->uwb.rfkill) {
		bl = !asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_UWB);
		rfkill_set_sw_state(asus->uwb.rfkill, bl);
	}
	if (!IS_ERR_OR_NULL(asus->kbd_led.dev))
		kbd_led_update(asus);

	if (asus_wmi_has_fnlock_key(asus))
		asus_wmi_fnlock_update(asus);
	return 0;
}

static const struct dev_pm_ops asus_pm_ops = {
	.thaw = asus_hotk_thaw,
	.restore = asus_hotk_restore,
	.resume = asus_hotk_resume,
};

/* Registration ***************************************************************/

static int asus_wmi_probe(struct platform_device *pdev)
{
	struct platform_driver *pdrv = to_platform_driver(pdev->dev.driver);
	struct asus_wmi_driver *wdrv = to_asus_wmi_driver(pdrv);
	int ret;

	if (!wmi_has_guid(ASUS_WMI_MGMT_GUID)) {
		pr_warn("ASUS Management GUID not found\n");
		return -ENODEV;
	}

	if (wdrv->event_guid && !wmi_has_guid(wdrv->event_guid)) {
		pr_warn("ASUS Event GUID not found\n");
		return -ENODEV;
	}

	if (wdrv->probe) {
		ret = wdrv->probe(pdev);
		if (ret)
			return ret;
	}

	return asus_wmi_add(pdev);
}

static bool used;

int __init_or_module asus_wmi_register_driver(struct asus_wmi_driver *driver)
{
	struct platform_driver *platform_driver;
	struct platform_device *platform_device;

	if (used)
		return -EBUSY;

	platform_driver = &driver->platform_driver;
	platform_driver->remove = asus_wmi_remove;
	platform_driver->driver.owner = driver->owner;
	platform_driver->driver.name = driver->name;
	platform_driver->driver.pm = &asus_pm_ops;

	platform_device = platform_create_bundle(platform_driver,
						 asus_wmi_probe,
						 NULL, 0, NULL, 0);
	if (IS_ERR(platform_device))
		return PTR_ERR(platform_device);

	used = true;
	return 0;
}
EXPORT_SYMBOL_GPL(asus_wmi_register_driver);

void asus_wmi_unregister_driver(struct asus_wmi_driver *driver)
{
	platform_device_unregister(driver->platform_device);
	platform_driver_unregister(&driver->platform_driver);
	used = false;
}
EXPORT_SYMBOL_GPL(asus_wmi_unregister_driver);

static int __init asus_wmi_init(void)
{
	pr_info("ASUS WMI generic driver loaded\n");
	return 0;
}

static void __exit asus_wmi_exit(void)
{
	pr_info("ASUS WMI generic driver unloaded\n");
}

module_init(asus_wmi_init);
module_exit(asus_wmi_exit);
