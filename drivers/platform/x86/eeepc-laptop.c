/*
 *  eeepc-laptop.c - Asus Eee PC extras
 *
 *  Based on asus_acpi.c as patched for the Eee PC by Asus:
 *  ftp://ftp.asus.com/pub/ASUS/EeePC/701/ASUS_ACPI_071126.rar
 *  Based on eee.c from eeepc-linux
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/uaccess.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/rfkill.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/leds.h>
#include <linux/dmi.h>
#include <acpi/video.h>

#define EEEPC_LAPTOP_VERSION	"0.1"
#define EEEPC_LAPTOP_NAME	"Eee PC Hotkey Driver"
#define EEEPC_LAPTOP_FILE	"eeepc"

#define EEEPC_ACPI_CLASS	"hotkey"
#define EEEPC_ACPI_DEVICE_NAME	"Hotkey"
#define EEEPC_ACPI_HID		"ASUS010"

MODULE_AUTHOR("Corentin Chary, Eric Cooper");
MODULE_DESCRIPTION(EEEPC_LAPTOP_NAME);
MODULE_LICENSE("GPL");

static bool hotplug_disabled;

module_param(hotplug_disabled, bool, 0444);
MODULE_PARM_DESC(hotplug_disabled,
		 "Disable hotplug for wireless device. "
		 "If your laptop need that, please report to "
		 "acpi4asus-user@lists.sourceforge.net.");

/*
 * Definitions for Asus EeePC
 */
#define NOTIFY_BRN_MIN	0x20
#define NOTIFY_BRN_MAX	0x2f

enum {
	DISABLE_ASL_WLAN = 0x0001,
	DISABLE_ASL_BLUETOOTH = 0x0002,
	DISABLE_ASL_IRDA = 0x0004,
	DISABLE_ASL_CAMERA = 0x0008,
	DISABLE_ASL_TV = 0x0010,
	DISABLE_ASL_GPS = 0x0020,
	DISABLE_ASL_DISPLAYSWITCH = 0x0040,
	DISABLE_ASL_MODEM = 0x0080,
	DISABLE_ASL_CARDREADER = 0x0100,
	DISABLE_ASL_3G = 0x0200,
	DISABLE_ASL_WIMAX = 0x0400,
	DISABLE_ASL_HWCF = 0x0800
};

enum {
	CM_ASL_WLAN = 0,
	CM_ASL_BLUETOOTH,
	CM_ASL_IRDA,
	CM_ASL_1394,
	CM_ASL_CAMERA,
	CM_ASL_TV,
	CM_ASL_GPS,
	CM_ASL_DVDROM,
	CM_ASL_DISPLAYSWITCH,
	CM_ASL_PANELBRIGHT,
	CM_ASL_BIOSFLASH,
	CM_ASL_ACPIFLASH,
	CM_ASL_CPUFV,
	CM_ASL_CPUTEMPERATURE,
	CM_ASL_FANCPU,
	CM_ASL_FANCHASSIS,
	CM_ASL_USBPORT1,
	CM_ASL_USBPORT2,
	CM_ASL_USBPORT3,
	CM_ASL_MODEM,
	CM_ASL_CARDREADER,
	CM_ASL_3G,
	CM_ASL_WIMAX,
	CM_ASL_HWCF,
	CM_ASL_LID,
	CM_ASL_TYPE,
	CM_ASL_PANELPOWER,	/*P901*/
	CM_ASL_TPD
};

static const char *cm_getv[] = {
	"WLDG", "BTHG", NULL, NULL,
	"CAMG", NULL, NULL, NULL,
	NULL, "PBLG", NULL, NULL,
	"CFVG", NULL, NULL, NULL,
	"USBG", NULL, NULL, "MODG",
	"CRDG", "M3GG", "WIMG", "HWCF",
	"LIDG",	"TYPE", "PBPG",	"TPDG"
};

static const char *cm_setv[] = {
	"WLDS", "BTHS", NULL, NULL,
	"CAMS", NULL, NULL, NULL,
	"SDSP", "PBLS", "HDPS", NULL,
	"CFVS", NULL, NULL, NULL,
	"USBG", NULL, NULL, "MODS",
	"CRDS", "M3GS", "WIMS", NULL,
	NULL, NULL, "PBPS", "TPDS"
};

static const struct key_entry eeepc_keymap[] = {
	{ KE_KEY, 0x10, { KEY_WLAN } },
	{ KE_KEY, 0x11, { KEY_WLAN } },
	{ KE_KEY, 0x12, { KEY_PROG1 } },
	{ KE_KEY, 0x13, { KEY_MUTE } },
	{ KE_KEY, 0x14, { KEY_VOLUMEDOWN } },
	{ KE_KEY, 0x15, { KEY_VOLUMEUP } },
	{ KE_KEY, 0x16, { KEY_DISPLAY_OFF } },
	{ KE_KEY, 0x1a, { KEY_COFFEE } },
	{ KE_KEY, 0x1b, { KEY_ZOOM } },
	{ KE_KEY, 0x1c, { KEY_PROG2 } },
	{ KE_KEY, 0x1d, { KEY_PROG3 } },
	{ KE_KEY, NOTIFY_BRN_MIN, { KEY_BRIGHTNESSDOWN } },
	{ KE_KEY, NOTIFY_BRN_MAX, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 0x30, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0x31, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0x32, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0x37, { KEY_F13 } }, /* Disable Touchpad */
	{ KE_KEY, 0x38, { KEY_F14 } },
	{ KE_IGNORE, 0x50, { KEY_RESERVED } }, /* AC plugged */
	{ KE_IGNORE, 0x51, { KEY_RESERVED } }, /* AC unplugged */
	{ KE_END, 0 },
};

/*
 * This is the main structure, we can use it to store useful information
 */
struct eeepc_laptop {
	acpi_handle handle;		/* the handle of the acpi device */
	u32 cm_supported;		/* the control methods supported
					   by this BIOS */
	bool cpufv_disabled;
	bool hotplug_disabled;
	u16 event_count[128];		/* count for each event */

	struct platform_device *platform_device;
	struct acpi_device *device;		/* the device we are in */
	struct backlight_device *backlight_device;

	struct input_dev *inputdev;

	struct rfkill *wlan_rfkill;
	struct rfkill *bluetooth_rfkill;
	struct rfkill *wwan3g_rfkill;
	struct rfkill *wimax_rfkill;

	struct hotplug_slot *hotplug_slot;
	struct mutex hotplug_lock;

	struct led_classdev tpd_led;
	int tpd_led_wk;
	struct workqueue_struct *led_workqueue;
	struct work_struct tpd_led_work;
};

/*
 * ACPI Helpers
 */
static int write_acpi_int(acpi_handle handle, const char *method, int val)
{
	acpi_status status;

	status = acpi_execute_simple_method(handle, (char *)method, val);

	return (status == AE_OK ? 0 : -1);
}

static int read_acpi_int(acpi_handle handle, const char *method, int *val)
{
	acpi_status status;
	unsigned long long result;

	status = acpi_evaluate_integer(handle, (char *)method, NULL, &result);
	if (ACPI_FAILURE(status)) {
		*val = -1;
		return -1;
	} else {
		*val = result;
		return 0;
	}
}

static int set_acpi(struct eeepc_laptop *eeepc, int cm, int value)
{
	const char *method = cm_setv[cm];

	if (method == NULL)
		return -ENODEV;
	if ((eeepc->cm_supported & (0x1 << cm)) == 0)
		return -ENODEV;

	if (write_acpi_int(eeepc->handle, method, value))
		pr_warn("Error writing %s\n", method);
	return 0;
}

static int get_acpi(struct eeepc_laptop *eeepc, int cm)
{
	const char *method = cm_getv[cm];
	int value;

	if (method == NULL)
		return -ENODEV;
	if ((eeepc->cm_supported & (0x1 << cm)) == 0)
		return -ENODEV;

	if (read_acpi_int(eeepc->handle, method, &value))
		pr_warn("Error reading %s\n", method);
	return value;
}

static int acpi_setter_handle(struct eeepc_laptop *eeepc, int cm,
			      acpi_handle *handle)
{
	const char *method = cm_setv[cm];
	acpi_status status;

	if (method == NULL)
		return -ENODEV;
	if ((eeepc->cm_supported & (0x1 << cm)) == 0)
		return -ENODEV;

	status = acpi_get_handle(eeepc->handle, (char *)method,
				 handle);
	if (status != AE_OK) {
		pr_warn("Error finding %s\n", method);
		return -ENODEV;
	}
	return 0;
}


/*
 * Sys helpers
 */
static int parse_arg(const char *buf, int *val)
{
	if (sscanf(buf, "%i", val) != 1)
		return -EINVAL;
	return 0;
}

static ssize_t store_sys_acpi(struct device *dev, int cm,
			      const char *buf, size_t count)
{
	struct eeepc_laptop *eeepc = dev_get_drvdata(dev);
	int rv, value;

	rv = parse_arg(buf, &value);
	if (rv < 0)
		return rv;
	rv = set_acpi(eeepc, cm, value);
	if (rv < 0)
		return -EIO;
	return count;
}

static ssize_t show_sys_acpi(struct device *dev, int cm, char *buf)
{
	struct eeepc_laptop *eeepc = dev_get_drvdata(dev);
	int value = get_acpi(eeepc, cm);

	if (value < 0)
		return -EIO;
	return sprintf(buf, "%d\n", value);
}

#define EEEPC_ACPI_SHOW_FUNC(_name, _cm)				\
	static ssize_t _name##_show(struct device *dev,			\
				    struct device_attribute *attr,	\
				    char *buf)				\
	{								\
		return show_sys_acpi(dev, _cm, buf);			\
	}

#define EEEPC_ACPI_STORE_FUNC(_name, _cm)				\
	static ssize_t _name##_store(struct device *dev,		\
				     struct device_attribute *attr,	\
				     const char *buf, size_t count)	\
	{								\
		return store_sys_acpi(dev, _cm, buf, count);		\
	}

#define EEEPC_CREATE_DEVICE_ATTR_RW(_name, _cm)				\
	EEEPC_ACPI_SHOW_FUNC(_name, _cm)				\
	EEEPC_ACPI_STORE_FUNC(_name, _cm)				\
	static DEVICE_ATTR_RW(_name)

#define EEEPC_CREATE_DEVICE_ATTR_WO(_name, _cm)				\
	EEEPC_ACPI_STORE_FUNC(_name, _cm)				\
	static DEVICE_ATTR_WO(_name)

EEEPC_CREATE_DEVICE_ATTR_RW(camera, CM_ASL_CAMERA);
EEEPC_CREATE_DEVICE_ATTR_RW(cardr, CM_ASL_CARDREADER);
EEEPC_CREATE_DEVICE_ATTR_WO(disp, CM_ASL_DISPLAYSWITCH);

struct eeepc_cpufv {
	int num;
	int cur;
};

static int get_cpufv(struct eeepc_laptop *eeepc, struct eeepc_cpufv *c)
{
	c->cur = get_acpi(eeepc, CM_ASL_CPUFV);
	if (c->cur < 0)
		return -ENODEV;

	c->num = (c->cur >> 8) & 0xff;
	c->cur &= 0xff;
	if (c->num == 0 || c->num > 12)
		return -ENODEV;
	return 0;
}

static ssize_t available_cpufv_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct eeepc_laptop *eeepc = dev_get_drvdata(dev);
	struct eeepc_cpufv c;
	int i;
	ssize_t len = 0;

	if (get_cpufv(eeepc, &c))
		return -ENODEV;
	for (i = 0; i < c.num; i++)
		len += sprintf(buf + len, "%d ", i);
	len += sprintf(buf + len, "\n");
	return len;
}

static ssize_t cpufv_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct eeepc_laptop *eeepc = dev_get_drvdata(dev);
	struct eeepc_cpufv c;

	if (get_cpufv(eeepc, &c))
		return -ENODEV;
	return sprintf(buf, "%#x\n", (c.num << 8) | c.cur);
}

static ssize_t cpufv_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct eeepc_laptop *eeepc = dev_get_drvdata(dev);
	struct eeepc_cpufv c;
	int rv, value;

	if (eeepc->cpufv_disabled)
		return -EPERM;
	if (get_cpufv(eeepc, &c))
		return -ENODEV;
	rv = parse_arg(buf, &value);
	if (rv < 0)
		return rv;
	if (value < 0 || value >= c.num)
		return -EINVAL;
	rv = set_acpi(eeepc, CM_ASL_CPUFV, value);
	if (rv)
		return rv;
	return count;
}

static ssize_t cpufv_disabled_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct eeepc_laptop *eeepc = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", eeepc->cpufv_disabled);
}

static ssize_t cpufv_disabled_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct eeepc_laptop *eeepc = dev_get_drvdata(dev);
	int rv, value;

	rv = parse_arg(buf, &value);
	if (rv < 0)
		return rv;

	switch (value) {
	case 0:
		if (eeepc->cpufv_disabled)
			pr_warn("cpufv enabled (not officially supported on this model)\n");
		eeepc->cpufv_disabled = false;
		return count;
	case 1:
		return -EPERM;
	default:
		return -EINVAL;
	}
}


static DEVICE_ATTR_RW(cpufv);
static DEVICE_ATTR_RO(available_cpufv);
static DEVICE_ATTR_RW(cpufv_disabled);

static struct attribute *platform_attributes[] = {
	&dev_attr_camera.attr,
	&dev_attr_cardr.attr,
	&dev_attr_disp.attr,
	&dev_attr_cpufv.attr,
	&dev_attr_available_cpufv.attr,
	&dev_attr_cpufv_disabled.attr,
	NULL
};

static struct attribute_group platform_attribute_group = {
	.attrs = platform_attributes
};

static int eeepc_platform_init(struct eeepc_laptop *eeepc)
{
	int result;

	eeepc->platform_device = platform_device_alloc(EEEPC_LAPTOP_FILE, -1);
	if (!eeepc->platform_device)
		return -ENOMEM;
	platform_set_drvdata(eeepc->platform_device, eeepc);

	result = platform_device_add(eeepc->platform_device);
	if (result)
		goto fail_platform_device;

	result = sysfs_create_group(&eeepc->platform_device->dev.kobj,
				    &platform_attribute_group);
	if (result)
		goto fail_sysfs;
	return 0;

fail_sysfs:
	platform_device_del(eeepc->platform_device);
fail_platform_device:
	platform_device_put(eeepc->platform_device);
	return result;
}

static void eeepc_platform_exit(struct eeepc_laptop *eeepc)
{
	sysfs_remove_group(&eeepc->platform_device->dev.kobj,
			   &platform_attribute_group);
	platform_device_unregister(eeepc->platform_device);
}

/*
 * LEDs
 */
/*
 * These functions actually update the LED's, and are called from a
 * workqueue. By doing this as separate work rather than when the LED
 * subsystem asks, we avoid messing with the Asus ACPI stuff during a
 * potentially bad time, such as a timer interrupt.
 */
static void tpd_led_update(struct work_struct *work)
 {
	struct eeepc_laptop *eeepc;

	eeepc = container_of(work, struct eeepc_laptop, tpd_led_work);

	set_acpi(eeepc, CM_ASL_TPD, eeepc->tpd_led_wk);
}

static void tpd_led_set(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct eeepc_laptop *eeepc;

	eeepc = container_of(led_cdev, struct eeepc_laptop, tpd_led);

	eeepc->tpd_led_wk = (value > 0) ? 1 : 0;
	queue_work(eeepc->led_workqueue, &eeepc->tpd_led_work);
}

static enum led_brightness tpd_led_get(struct led_classdev *led_cdev)
{
	struct eeepc_laptop *eeepc;

	eeepc = container_of(led_cdev, struct eeepc_laptop, tpd_led);

	return get_acpi(eeepc, CM_ASL_TPD);
}

static int eeepc_led_init(struct eeepc_laptop *eeepc)
{
	int rv;

	if (get_acpi(eeepc, CM_ASL_TPD) == -ENODEV)
		return 0;

	eeepc->led_workqueue = create_singlethread_workqueue("led_workqueue");
	if (!eeepc->led_workqueue)
		return -ENOMEM;
	INIT_WORK(&eeepc->tpd_led_work, tpd_led_update);

	eeepc->tpd_led.name = "eeepc::touchpad";
	eeepc->tpd_led.brightness_set = tpd_led_set;
	if (get_acpi(eeepc, CM_ASL_TPD) >= 0) /* if method is available */
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

static void eeepc_led_exit(struct eeepc_laptop *eeepc)
{
	if (!IS_ERR_OR_NULL(eeepc->tpd_led.dev))
		led_classdev_unregister(&eeepc->tpd_led);
	if (eeepc->led_workqueue)
		destroy_workqueue(eeepc->led_workqueue);
}


/*
 * PCI hotplug (for wlan rfkill)
 */
static bool eeepc_wlan_rfkill_blocked(struct eeepc_laptop *eeepc)
{
	if (get_acpi(eeepc, CM_ASL_WLAN) == 1)
		return false;
	return true;
}

static void eeepc_rfkill_hotplug(struct eeepc_laptop *eeepc, acpi_handle handle)
{
	struct pci_dev *port;
	struct pci_dev *dev;
	struct pci_bus *bus;
	bool blocked = eeepc_wlan_rfkill_blocked(eeepc);
	bool absent;
	u32 l;

	if (eeepc->wlan_rfkill)
		rfkill_set_sw_state(eeepc->wlan_rfkill, blocked);

	mutex_lock(&eeepc->hotplug_lock);
	pci_lock_rescan_remove();

	if (!eeepc->hotplug_slot)
		goto out_unlock;

	port = acpi_get_pci_dev(handle);
	if (!port) {
		pr_warning("Unable to find port\n");
		goto out_unlock;
	}

	bus = port->subordinate;

	if (!bus) {
		pr_warn("Unable to find PCI bus 1?\n");
		goto out_put_dev;
	}

	if (pci_bus_read_config_dword(bus, 0, PCI_VENDOR_ID, &l)) {
		pr_err("Unable to read PCI config space?\n");
		goto out_put_dev;
	}

	absent = (l == 0xffffffff);

	if (blocked != absent) {
		pr_warn("BIOS says wireless lan is %s, but the pci device is %s\n",
			blocked ? "blocked" : "unblocked",
			absent ? "absent" : "present");
		pr_warn("skipped wireless hotplug as probably inappropriate for this model\n");
		goto out_put_dev;
	}

	if (!blocked) {
		dev = pci_get_slot(bus, 0);
		if (dev) {
			/* Device already present */
			pci_dev_put(dev);
			goto out_put_dev;
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
out_put_dev:
	pci_dev_put(port);

out_unlock:
	pci_unlock_rescan_remove();
	mutex_unlock(&eeepc->hotplug_lock);
}

static void eeepc_rfkill_hotplug_update(struct eeepc_laptop *eeepc, char *node)
{
	acpi_status status = AE_OK;
	acpi_handle handle;

	status = acpi_get_handle(NULL, node, &handle);

	if (ACPI_SUCCESS(status))
		eeepc_rfkill_hotplug(eeepc, handle);
}

static void eeepc_rfkill_notify(acpi_handle handle, u32 event, void *data)
{
	struct eeepc_laptop *eeepc = data;

	if (event != ACPI_NOTIFY_BUS_CHECK)
		return;

	eeepc_rfkill_hotplug(eeepc, handle);
}

static int eeepc_register_rfkill_notifier(struct eeepc_laptop *eeepc,
					  char *node)
{
	acpi_status status;
	acpi_handle handle;

	status = acpi_get_handle(NULL, node, &handle);

	if (ACPI_FAILURE(status))
		return -ENODEV;

	status = acpi_install_notify_handler(handle,
					     ACPI_SYSTEM_NOTIFY,
					     eeepc_rfkill_notify,
					     eeepc);
	if (ACPI_FAILURE(status))
		pr_warn("Failed to register notify on %s\n", node);

	/*
	 * Refresh pci hotplug in case the rfkill state was
	 * changed during setup.
	 */
	eeepc_rfkill_hotplug(eeepc, handle);
	return 0;
}

static void eeepc_unregister_rfkill_notifier(struct eeepc_laptop *eeepc,
					     char *node)
{
	acpi_status status = AE_OK;
	acpi_handle handle;

	status = acpi_get_handle(NULL, node, &handle);

	if (ACPI_FAILURE(status))
		return;

	status = acpi_remove_notify_handler(handle,
					     ACPI_SYSTEM_NOTIFY,
					     eeepc_rfkill_notify);
	if (ACPI_FAILURE(status))
		pr_err("Error removing rfkill notify handler %s\n",
			node);
		/*
		 * Refresh pci hotplug in case the rfkill
		 * state was changed after
		 * eeepc_unregister_rfkill_notifier()
		 */
	eeepc_rfkill_hotplug(eeepc, handle);
}

static int eeepc_get_adapter_status(struct hotplug_slot *hotplug_slot,
				    u8 *value)
{
	struct eeepc_laptop *eeepc = hotplug_slot->private;
	int val = get_acpi(eeepc, CM_ASL_WLAN);

	if (val == 1 || val == 0)
		*value = val;
	else
		return -EINVAL;

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

static int eeepc_setup_pci_hotplug(struct eeepc_laptop *eeepc)
{
	int ret = -ENOMEM;
	struct pci_bus *bus = pci_find_bus(0, 1);

	if (!bus) {
		pr_err("Unable to find wifi PCI bus\n");
		return -ENODEV;
	}

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
	return ret;
}

/*
 * Rfkill devices
 */
static int eeepc_rfkill_set(void *data, bool blocked)
{
	acpi_handle handle = data;

	return write_acpi_int(handle, NULL, !blocked);
}

static const struct rfkill_ops eeepc_rfkill_ops = {
	.set_block = eeepc_rfkill_set,
};

static int eeepc_new_rfkill(struct eeepc_laptop *eeepc,
			    struct rfkill **rfkill,
			    const char *name,
			    enum rfkill_type type, int cm)
{
	acpi_handle handle;
	int result;

	result = acpi_setter_handle(eeepc, cm, &handle);
	if (result < 0)
		return result;

	*rfkill = rfkill_alloc(name, &eeepc->platform_device->dev, type,
			       &eeepc_rfkill_ops, handle);

	if (!*rfkill)
		return -EINVAL;

	rfkill_init_sw_state(*rfkill, get_acpi(eeepc, cm) != 1);
	result = rfkill_register(*rfkill);
	if (result) {
		rfkill_destroy(*rfkill);
		*rfkill = NULL;
		return result;
	}
	return 0;
}

static char EEEPC_RFKILL_NODE_1[] = "\\_SB.PCI0.P0P5";
static char EEEPC_RFKILL_NODE_2[] = "\\_SB.PCI0.P0P6";
static char EEEPC_RFKILL_NODE_3[] = "\\_SB.PCI0.P0P7";

static void eeepc_rfkill_exit(struct eeepc_laptop *eeepc)
{
	eeepc_unregister_rfkill_notifier(eeepc, EEEPC_RFKILL_NODE_1);
	eeepc_unregister_rfkill_notifier(eeepc, EEEPC_RFKILL_NODE_2);
	eeepc_unregister_rfkill_notifier(eeepc, EEEPC_RFKILL_NODE_3);
	if (eeepc->wlan_rfkill) {
		rfkill_unregister(eeepc->wlan_rfkill);
		rfkill_destroy(eeepc->wlan_rfkill);
		eeepc->wlan_rfkill = NULL;
	}

	if (eeepc->hotplug_slot)
		pci_hp_deregister(eeepc->hotplug_slot);

	if (eeepc->bluetooth_rfkill) {
		rfkill_unregister(eeepc->bluetooth_rfkill);
		rfkill_destroy(eeepc->bluetooth_rfkill);
		eeepc->bluetooth_rfkill = NULL;
	}
	if (eeepc->wwan3g_rfkill) {
		rfkill_unregister(eeepc->wwan3g_rfkill);
		rfkill_destroy(eeepc->wwan3g_rfkill);
		eeepc->wwan3g_rfkill = NULL;
	}
	if (eeepc->wimax_rfkill) {
		rfkill_unregister(eeepc->wimax_rfkill);
		rfkill_destroy(eeepc->wimax_rfkill);
		eeepc->wimax_rfkill = NULL;
	}
}

static int eeepc_rfkill_init(struct eeepc_laptop *eeepc)
{
	int result = 0;

	mutex_init(&eeepc->hotplug_lock);

	result = eeepc_new_rfkill(eeepc, &eeepc->wlan_rfkill,
				  "eeepc-wlan", RFKILL_TYPE_WLAN,
				  CM_ASL_WLAN);

	if (result && result != -ENODEV)
		goto exit;

	result = eeepc_new_rfkill(eeepc, &eeepc->bluetooth_rfkill,
				  "eeepc-bluetooth", RFKILL_TYPE_BLUETOOTH,
				  CM_ASL_BLUETOOTH);

	if (result && result != -ENODEV)
		goto exit;

	result = eeepc_new_rfkill(eeepc, &eeepc->wwan3g_rfkill,
				  "eeepc-wwan3g", RFKILL_TYPE_WWAN,
				  CM_ASL_3G);

	if (result && result != -ENODEV)
		goto exit;

	result = eeepc_new_rfkill(eeepc, &eeepc->wimax_rfkill,
				  "eeepc-wimax", RFKILL_TYPE_WIMAX,
				  CM_ASL_WIMAX);

	if (result && result != -ENODEV)
		goto exit;

	if (eeepc->hotplug_disabled)
		return 0;

	result = eeepc_setup_pci_hotplug(eeepc);
	/*
	 * If we get -EBUSY then something else is handling the PCI hotplug -
	 * don't fail in this case
	 */
	if (result == -EBUSY)
		result = 0;

	eeepc_register_rfkill_notifier(eeepc, EEEPC_RFKILL_NODE_1);
	eeepc_register_rfkill_notifier(eeepc, EEEPC_RFKILL_NODE_2);
	eeepc_register_rfkill_notifier(eeepc, EEEPC_RFKILL_NODE_3);

exit:
	if (result && result != -ENODEV)
		eeepc_rfkill_exit(eeepc);
	return result;
}

/*
 * Platform driver - hibernate/resume callbacks
 */
static int eeepc_hotk_thaw(struct device *device)
{
	struct eeepc_laptop *eeepc = dev_get_drvdata(device);

	if (eeepc->wlan_rfkill) {
		int wlan;

		/*
		 * Work around bios bug - acpi _PTS turns off the wireless led
		 * during suspend.  Normally it restores it on resume, but
		 * we should kick it ourselves in case hibernation is aborted.
		 */
		wlan = get_acpi(eeepc, CM_ASL_WLAN);
		if (wlan >= 0)
			set_acpi(eeepc, CM_ASL_WLAN, wlan);
	}

	return 0;
}

static int eeepc_hotk_restore(struct device *device)
{
	struct eeepc_laptop *eeepc = dev_get_drvdata(device);

	/* Refresh both wlan rfkill state and pci hotplug */
	if (eeepc->wlan_rfkill) {
		eeepc_rfkill_hotplug_update(eeepc, EEEPC_RFKILL_NODE_1);
		eeepc_rfkill_hotplug_update(eeepc, EEEPC_RFKILL_NODE_2);
		eeepc_rfkill_hotplug_update(eeepc, EEEPC_RFKILL_NODE_3);
	}

	if (eeepc->bluetooth_rfkill)
		rfkill_set_sw_state(eeepc->bluetooth_rfkill,
				    get_acpi(eeepc, CM_ASL_BLUETOOTH) != 1);
	if (eeepc->wwan3g_rfkill)
		rfkill_set_sw_state(eeepc->wwan3g_rfkill,
				    get_acpi(eeepc, CM_ASL_3G) != 1);
	if (eeepc->wimax_rfkill)
		rfkill_set_sw_state(eeepc->wimax_rfkill,
				    get_acpi(eeepc, CM_ASL_WIMAX) != 1);

	return 0;
}

static const struct dev_pm_ops eeepc_pm_ops = {
	.thaw = eeepc_hotk_thaw,
	.restore = eeepc_hotk_restore,
};

static struct platform_driver platform_driver = {
	.driver = {
		.name = EEEPC_LAPTOP_FILE,
		.pm = &eeepc_pm_ops,
	}
};

/*
 * Hwmon device
 */

#define EEEPC_EC_SC00      0x61
#define EEEPC_EC_FAN_PWM   (EEEPC_EC_SC00 + 2) /* Fan PWM duty cycle (%) */
#define EEEPC_EC_FAN_HRPM  (EEEPC_EC_SC00 + 5) /* High byte, fan speed (RPM) */
#define EEEPC_EC_FAN_LRPM  (EEEPC_EC_SC00 + 6) /* Low byte, fan speed (RPM) */

#define EEEPC_EC_SFB0      0xD0
#define EEEPC_EC_FAN_CTRL  (EEEPC_EC_SFB0 + 3) /* Byte containing SF25  */

static inline int eeepc_pwm_to_lmsensors(int value)
{
	return value * 255 / 100;
}

static inline int eeepc_lmsensors_to_pwm(int value)
{
	value = clamp_val(value, 0, 255);
	return value * 100 / 255;
}

static int eeepc_get_fan_pwm(void)
{
	u8 value = 0;

	ec_read(EEEPC_EC_FAN_PWM, &value);
	return eeepc_pwm_to_lmsensors(value);
}

static void eeepc_set_fan_pwm(int value)
{
	value = eeepc_lmsensors_to_pwm(value);
	ec_write(EEEPC_EC_FAN_PWM, value);
}

static int eeepc_get_fan_rpm(void)
{
	u8 high = 0;
	u8 low = 0;

	ec_read(EEEPC_EC_FAN_HRPM, &high);
	ec_read(EEEPC_EC_FAN_LRPM, &low);
	return high << 8 | low;
}

#define EEEPC_EC_FAN_CTRL_BIT	0x02
#define EEEPC_FAN_CTRL_MANUAL	1
#define EEEPC_FAN_CTRL_AUTO	2

static int eeepc_get_fan_ctrl(void)
{
	u8 value = 0;

	ec_read(EEEPC_EC_FAN_CTRL, &value);
	if (value & EEEPC_EC_FAN_CTRL_BIT)
		return EEEPC_FAN_CTRL_MANUAL;
	else
		return EEEPC_FAN_CTRL_AUTO;
}

static void eeepc_set_fan_ctrl(int manual)
{
	u8 value = 0;

	ec_read(EEEPC_EC_FAN_CTRL, &value);
	if (manual == EEEPC_FAN_CTRL_MANUAL)
		value |= EEEPC_EC_FAN_CTRL_BIT;
	else
		value &= ~EEEPC_EC_FAN_CTRL_BIT;
	ec_write(EEEPC_EC_FAN_CTRL, value);
}

static ssize_t store_sys_hwmon(void (*set)(int), const char *buf, size_t count)
{
	int rv, value;

	rv = parse_arg(buf, &value);
	if (rv < 0)
		return rv;
	set(value);
	return count;
}

static ssize_t show_sys_hwmon(int (*get)(void), char *buf)
{
	return sprintf(buf, "%d\n", get());
}

#define EEEPC_SENSOR_SHOW_FUNC(_name, _get)				\
	static ssize_t _name##_show(struct device *dev,			\
				    struct device_attribute *attr,	\
				    char *buf)				\
	{								\
		return show_sys_hwmon(_get, buf);			\
	}

#define EEEPC_SENSOR_STORE_FUNC(_name, _set)				\
	static ssize_t _name##_store(struct device *dev,		\
				     struct device_attribute *attr,	\
				     const char *buf, size_t count)	\
	{								\
		return store_sys_hwmon(_set, buf, count);		\
	}

#define EEEPC_CREATE_SENSOR_ATTR_RW(_name, _get, _set)			\
	EEEPC_SENSOR_SHOW_FUNC(_name, _get)				\
	EEEPC_SENSOR_STORE_FUNC(_name, _set)				\
	static DEVICE_ATTR_RW(_name)

#define EEEPC_CREATE_SENSOR_ATTR_RO(_name, _get)			\
	EEEPC_SENSOR_SHOW_FUNC(_name, _get)				\
	static DEVICE_ATTR_RO(_name)

EEEPC_CREATE_SENSOR_ATTR_RO(fan1_input, eeepc_get_fan_rpm);
EEEPC_CREATE_SENSOR_ATTR_RW(pwm1, eeepc_get_fan_pwm,
			    eeepc_set_fan_pwm);
EEEPC_CREATE_SENSOR_ATTR_RW(pwm1_enable, eeepc_get_fan_ctrl,
			    eeepc_set_fan_ctrl);

static struct attribute *hwmon_attrs[] = {
	&dev_attr_pwm1.attr,
	&dev_attr_fan1_input.attr,
	&dev_attr_pwm1_enable.attr,
	NULL
};
ATTRIBUTE_GROUPS(hwmon);

static int eeepc_hwmon_init(struct eeepc_laptop *eeepc)
{
	struct device *dev = &eeepc->platform_device->dev;
	struct device *hwmon;

	hwmon = devm_hwmon_device_register_with_groups(dev, "eeepc", NULL,
						       hwmon_groups);
	if (IS_ERR(hwmon)) {
		pr_err("Could not register eeepc hwmon device\n");
		return PTR_ERR(hwmon);
	}
	return 0;
}

/*
 * Backlight device
 */
static int read_brightness(struct backlight_device *bd)
{
	struct eeepc_laptop *eeepc = bl_get_data(bd);

	return get_acpi(eeepc, CM_ASL_PANELBRIGHT);
}

static int set_brightness(struct backlight_device *bd, int value)
{
	struct eeepc_laptop *eeepc = bl_get_data(bd);

	return set_acpi(eeepc, CM_ASL_PANELBRIGHT, value);
}

static int update_bl_status(struct backlight_device *bd)
{
	return set_brightness(bd, bd->props.brightness);
}

static const struct backlight_ops eeepcbl_ops = {
	.get_brightness = read_brightness,
	.update_status = update_bl_status,
};

static int eeepc_backlight_notify(struct eeepc_laptop *eeepc)
{
	struct backlight_device *bd = eeepc->backlight_device;
	int old = bd->props.brightness;

	backlight_force_update(bd, BACKLIGHT_UPDATE_HOTKEY);

	return old;
}

static int eeepc_backlight_init(struct eeepc_laptop *eeepc)
{
	struct backlight_properties props;
	struct backlight_device *bd;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = 15;
	bd = backlight_device_register(EEEPC_LAPTOP_FILE,
				       &eeepc->platform_device->dev, eeepc,
				       &eeepcbl_ops, &props);
	if (IS_ERR(bd)) {
		pr_err("Could not register eeepc backlight device\n");
		eeepc->backlight_device = NULL;
		return PTR_ERR(bd);
	}
	eeepc->backlight_device = bd;
	bd->props.brightness = read_brightness(bd);
	bd->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(bd);
	return 0;
}

static void eeepc_backlight_exit(struct eeepc_laptop *eeepc)
{
	backlight_device_unregister(eeepc->backlight_device);
	eeepc->backlight_device = NULL;
}


/*
 * Input device (i.e. hotkeys)
 */
static int eeepc_input_init(struct eeepc_laptop *eeepc)
{
	struct input_dev *input;
	int error;

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	input->name = "Asus EeePC extra buttons";
	input->phys = EEEPC_LAPTOP_FILE "/input0";
	input->id.bustype = BUS_HOST;
	input->dev.parent = &eeepc->platform_device->dev;

	error = sparse_keymap_setup(input, eeepc_keymap, NULL);
	if (error) {
		pr_err("Unable to setup input device keymap\n");
		goto err_free_dev;
	}

	error = input_register_device(input);
	if (error) {
		pr_err("Unable to register input device\n");
		goto err_free_dev;
	}

	eeepc->inputdev = input;
	return 0;

err_free_dev:
	input_free_device(input);
	return error;
}

static void eeepc_input_exit(struct eeepc_laptop *eeepc)
{
	if (eeepc->inputdev)
		input_unregister_device(eeepc->inputdev);
	eeepc->inputdev = NULL;
}

/*
 * ACPI driver
 */
static void eeepc_input_notify(struct eeepc_laptop *eeepc, int event)
{
	if (!eeepc->inputdev)
		return;
	if (!sparse_keymap_report_event(eeepc->inputdev, event, 1, true))
		pr_info("Unknown key %x pressed\n", event);
}

static void eeepc_acpi_notify(struct acpi_device *device, u32 event)
{
	struct eeepc_laptop *eeepc = acpi_driver_data(device);
	int old_brightness, new_brightness;
	u16 count;

	if (event > ACPI_MAX_SYS_NOTIFY)
		return;
	count = eeepc->event_count[event % 128]++;
	acpi_bus_generate_netlink_event(device->pnp.device_class,
					dev_name(&device->dev), event,
					count);

	/* Brightness events are special */
	if (event < NOTIFY_BRN_MIN || event > NOTIFY_BRN_MAX) {
		eeepc_input_notify(eeepc, event);
		return;
	}

	/* Ignore them completely if the acpi video driver is used */
	if (!eeepc->backlight_device)
		return;

	/* Update the backlight device. */
	old_brightness = eeepc_backlight_notify(eeepc);

	/* Convert event to keypress (obsolescent hack) */
	new_brightness = event - NOTIFY_BRN_MIN;

	if (new_brightness < old_brightness) {
		event = NOTIFY_BRN_MIN; /* brightness down */
	} else if (new_brightness > old_brightness) {
		event = NOTIFY_BRN_MAX; /* brightness up */
	} else {
		/*
		 * no change in brightness - already at min/max,
		 * event will be desired value (or else ignored)
		 */
	}
	eeepc_input_notify(eeepc, event);
}

static void eeepc_dmi_check(struct eeepc_laptop *eeepc)
{
	const char *model;

	model = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (!model)
		return;

	/*
	 * Blacklist for setting cpufv (cpu speed).
	 *
	 * EeePC 4G ("701") implements CFVS, but it is not supported
	 * by the pre-installed OS, and the original option to change it
	 * in the BIOS setup screen was removed in later versions.
	 *
	 * Judging by the lack of "Super Hybrid Engine" on Asus product pages,
	 * this applies to all "701" models (4G/4G Surf/2G Surf).
	 *
	 * So Asus made a deliberate decision not to support it on this model.
	 * We have several reports that using it can cause the system to hang
	 *
	 * The hang has also been reported on a "702" (Model name "8G"?).
	 *
	 * We avoid dmi_check_system() / dmi_match(), because they use
	 * substring matching.  We don't want to affect the "701SD"
	 * and "701SDX" models, because they do support S.H.E.
	 */
	if (strcmp(model, "701") == 0 || strcmp(model, "702") == 0) {
		eeepc->cpufv_disabled = true;
		pr_info("model %s does not officially support setting cpu speed\n",
			model);
		pr_info("cpufv disabled to avoid instability\n");
	}

	/*
	 * Blacklist for wlan hotplug
	 *
	 * Eeepc 1005HA doesn't work like others models and don't need the
	 * hotplug code. In fact, current hotplug code seems to unplug another
	 * device...
	 */
	if (strcmp(model, "1005HA") == 0 || strcmp(model, "1201N") == 0 ||
	    strcmp(model, "1005PE") == 0) {
		eeepc->hotplug_disabled = true;
		pr_info("wlan hotplug disabled\n");
	}
}

static void cmsg_quirk(struct eeepc_laptop *eeepc, int cm, const char *name)
{
	int dummy;

	/* Some BIOSes do not report cm although it is available.
	   Check if cm_getv[cm] works and, if yes, assume cm should be set. */
	if (!(eeepc->cm_supported & (1 << cm))
	    && !read_acpi_int(eeepc->handle, cm_getv[cm], &dummy)) {
		pr_info("%s (%x) not reported by BIOS, enabling anyway\n",
			name, 1 << cm);
		eeepc->cm_supported |= 1 << cm;
	}
}

static void cmsg_quirks(struct eeepc_laptop *eeepc)
{
	cmsg_quirk(eeepc, CM_ASL_LID, "LID");
	cmsg_quirk(eeepc, CM_ASL_TYPE, "TYPE");
	cmsg_quirk(eeepc, CM_ASL_PANELPOWER, "PANELPOWER");
	cmsg_quirk(eeepc, CM_ASL_TPD, "TPD");
}

static int eeepc_acpi_init(struct eeepc_laptop *eeepc)
{
	unsigned int init_flags;
	int result;

	result = acpi_bus_get_status(eeepc->device);
	if (result)
		return result;
	if (!eeepc->device->status.present) {
		pr_err("Hotkey device not present, aborting\n");
		return -ENODEV;
	}

	init_flags = DISABLE_ASL_WLAN | DISABLE_ASL_DISPLAYSWITCH;
	pr_notice("Hotkey init flags 0x%x\n", init_flags);

	if (write_acpi_int(eeepc->handle, "INIT", init_flags)) {
		pr_err("Hotkey initialization failed\n");
		return -ENODEV;
	}

	/* get control methods supported */
	if (read_acpi_int(eeepc->handle, "CMSG", &eeepc->cm_supported)) {
		pr_err("Get control methods supported failed\n");
		return -ENODEV;
	}
	cmsg_quirks(eeepc);
	pr_info("Get control methods supported: 0x%x\n", eeepc->cm_supported);

	return 0;
}

static void eeepc_enable_camera(struct eeepc_laptop *eeepc)
{
	/*
	 * If the following call to set_acpi() fails, it's because there's no
	 * camera so we can ignore the error.
	 */
	if (get_acpi(eeepc, CM_ASL_CAMERA) == 0)
		set_acpi(eeepc, CM_ASL_CAMERA, 1);
}

static bool eeepc_device_present;

static int eeepc_acpi_add(struct acpi_device *device)
{
	struct eeepc_laptop *eeepc;
	int result;

	pr_notice(EEEPC_LAPTOP_NAME "\n");
	eeepc = kzalloc(sizeof(struct eeepc_laptop), GFP_KERNEL);
	if (!eeepc)
		return -ENOMEM;
	eeepc->handle = device->handle;
	strcpy(acpi_device_name(device), EEEPC_ACPI_DEVICE_NAME);
	strcpy(acpi_device_class(device), EEEPC_ACPI_CLASS);
	device->driver_data = eeepc;
	eeepc->device = device;

	eeepc->hotplug_disabled = hotplug_disabled;

	eeepc_dmi_check(eeepc);

	result = eeepc_acpi_init(eeepc);
	if (result)
		goto fail_platform;
	eeepc_enable_camera(eeepc);

	/*
	 * Register the platform device first.  It is used as a parent for the
	 * sub-devices below.
	 *
	 * Note that if there are multiple instances of this ACPI device it
	 * will bail out, because the platform device is registered with a
	 * fixed name.  Of course it doesn't make sense to have more than one,
	 * and machine-specific scripts find the fixed name convenient.  But
	 * It's also good for us to exclude multiple instances because both
	 * our hwmon and our wlan rfkill subdevice use global ACPI objects
	 * (the EC and the wlan PCI slot respectively).
	 */
	result = eeepc_platform_init(eeepc);
	if (result)
		goto fail_platform;

	if (acpi_video_get_backlight_type() == acpi_backlight_vendor) {
		result = eeepc_backlight_init(eeepc);
		if (result)
			goto fail_backlight;
	}

	result = eeepc_input_init(eeepc);
	if (result)
		goto fail_input;

	result = eeepc_hwmon_init(eeepc);
	if (result)
		goto fail_hwmon;

	result = eeepc_led_init(eeepc);
	if (result)
		goto fail_led;

	result = eeepc_rfkill_init(eeepc);
	if (result)
		goto fail_rfkill;

	eeepc_device_present = true;
	return 0;

fail_rfkill:
	eeepc_led_exit(eeepc);
fail_led:
fail_hwmon:
	eeepc_input_exit(eeepc);
fail_input:
	eeepc_backlight_exit(eeepc);
fail_backlight:
	eeepc_platform_exit(eeepc);
fail_platform:
	kfree(eeepc);

	return result;
}

static int eeepc_acpi_remove(struct acpi_device *device)
{
	struct eeepc_laptop *eeepc = acpi_driver_data(device);

	eeepc_backlight_exit(eeepc);
	eeepc_rfkill_exit(eeepc);
	eeepc_input_exit(eeepc);
	eeepc_led_exit(eeepc);
	eeepc_platform_exit(eeepc);

	kfree(eeepc);
	return 0;
}


static const struct acpi_device_id eeepc_device_ids[] = {
	{EEEPC_ACPI_HID, 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, eeepc_device_ids);

static struct acpi_driver eeepc_acpi_driver = {
	.name = EEEPC_LAPTOP_NAME,
	.class = EEEPC_ACPI_CLASS,
	.owner = THIS_MODULE,
	.ids = eeepc_device_ids,
	.flags = ACPI_DRIVER_ALL_NOTIFY_EVENTS,
	.ops = {
		.add = eeepc_acpi_add,
		.remove = eeepc_acpi_remove,
		.notify = eeepc_acpi_notify,
	},
};


static int __init eeepc_laptop_init(void)
{
	int result;

	result = platform_driver_register(&platform_driver);
	if (result < 0)
		return result;

	result = acpi_bus_register_driver(&eeepc_acpi_driver);
	if (result < 0)
		goto fail_acpi_driver;

	if (!eeepc_device_present) {
		result = -ENODEV;
		goto fail_no_device;
	}

	return 0;

fail_no_device:
	acpi_bus_unregister_driver(&eeepc_acpi_driver);
fail_acpi_driver:
	platform_driver_unregister(&platform_driver);
	return result;
}

static void __exit eeepc_laptop_exit(void)
{
	acpi_bus_unregister_driver(&eeepc_acpi_driver);
	platform_driver_unregister(&platform_driver);
}

module_init(eeepc_laptop_init);
module_exit(eeepc_laptop_exit);
