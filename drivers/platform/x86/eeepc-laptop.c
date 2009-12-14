/*
 *  eepc-laptop.c - Asus Eee PC extras
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
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>
#include <linux/uaccess.h>
#include <linux/input.h>
#include <linux/rfkill.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>

#define EEEPC_LAPTOP_VERSION	"0.1"

#define EEEPC_HOTK_NAME		"Eee PC Hotkey Driver"
#define EEEPC_HOTK_FILE		"eeepc"
#define EEEPC_HOTK_CLASS	"hotkey"
#define EEEPC_HOTK_DEVICE_NAME	"Hotkey"
#define EEEPC_HOTK_HID		"ASUS010"


/*
 * Definitions for Asus EeePC
 */
#define	NOTIFY_WLAN_ON	0x10
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

#define EEEPC_EC	"\\_SB.PCI0.SBRG.EC0."

#define EEEPC_EC_FAN_PWM	EEEPC_EC "SC02" /* Fan PWM duty cycle (%) */
#define EEEPC_EC_SC02		0x63
#define EEEPC_EC_FAN_HRPM	EEEPC_EC "SC05" /* High byte, fan speed (RPM) */
#define EEEPC_EC_FAN_LRPM	EEEPC_EC "SC06" /* Low byte, fan speed (RPM) */
#define EEEPC_EC_FAN_CTRL	EEEPC_EC "SFB3" /* Byte containing SF25  */
#define EEEPC_EC_SFB3		0xD3

/*
 * This is the main structure, we can use it to store useful information
 * about the hotk device
 */
struct eeepc_hotk {
	struct acpi_device *device;	/* the device we are in */
	acpi_handle handle;		/* the handle of the hotk device */
	u32 cm_supported;		/* the control methods supported
					   by this BIOS */
	uint init_flag;			/* Init flags */
	u16 event_count[128];		/* count for each event */
	struct input_dev *inputdev;
	u16 *keycode_map;
	struct rfkill *wlan_rfkill;
	struct rfkill *bluetooth_rfkill;
	struct rfkill *wwan3g_rfkill;
	struct rfkill *wimax_rfkill;
	struct hotplug_slot *hotplug_slot;
	struct mutex hotplug_lock;
};

/* The actual device the driver binds to */
static struct eeepc_hotk *ehotk;

/* Platform device/driver */
static int eeepc_hotk_thaw(struct device *device);
static int eeepc_hotk_restore(struct device *device);

static struct dev_pm_ops eeepc_pm_ops = {
	.thaw = eeepc_hotk_thaw,
	.restore = eeepc_hotk_restore,
};

static struct platform_driver platform_driver = {
	.driver = {
		.name = EEEPC_HOTK_FILE,
		.owner = THIS_MODULE,
		.pm = &eeepc_pm_ops,
	}
};

static struct platform_device *platform_device;

struct key_entry {
	char type;
	u8 code;
	u16 keycode;
};

enum { KE_KEY, KE_END };

static struct key_entry eeepc_keymap[] = {
	/* Sleep already handled via generic ACPI code */
	{KE_KEY, 0x10, KEY_WLAN },
	{KE_KEY, 0x11, KEY_WLAN },
	{KE_KEY, 0x12, KEY_PROG1 },
	{KE_KEY, 0x13, KEY_MUTE },
	{KE_KEY, 0x14, KEY_VOLUMEDOWN },
	{KE_KEY, 0x15, KEY_VOLUMEUP },
	{KE_KEY, 0x1a, KEY_COFFEE },
	{KE_KEY, 0x1b, KEY_ZOOM },
	{KE_KEY, 0x1c, KEY_PROG2 },
	{KE_KEY, 0x1d, KEY_PROG3 },
	{KE_KEY, NOTIFY_BRN_MIN,     KEY_BRIGHTNESSDOWN },
	{KE_KEY, NOTIFY_BRN_MIN + 2, KEY_BRIGHTNESSUP },
	{KE_KEY, 0x30, KEY_SWITCHVIDEOMODE },
	{KE_KEY, 0x31, KEY_SWITCHVIDEOMODE },
	{KE_KEY, 0x32, KEY_SWITCHVIDEOMODE },
	{KE_END, 0},
};

/*
 * The hotkey driver declaration
 */
static int eeepc_hotk_add(struct acpi_device *device);
static int eeepc_hotk_remove(struct acpi_device *device, int type);
static void eeepc_hotk_notify(struct acpi_device *device, u32 event);

static const struct acpi_device_id eeepc_device_ids[] = {
	{EEEPC_HOTK_HID, 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, eeepc_device_ids);

static struct acpi_driver eeepc_hotk_driver = {
	.name = EEEPC_HOTK_NAME,
	.class = EEEPC_HOTK_CLASS,
	.ids = eeepc_device_ids,
	.flags = ACPI_DRIVER_ALL_NOTIFY_EVENTS,
	.ops = {
		.add = eeepc_hotk_add,
		.remove = eeepc_hotk_remove,
		.notify = eeepc_hotk_notify,
	},
};

/* PCI hotplug ops */
static int eeepc_get_adapter_status(struct hotplug_slot *slot, u8 *value);

static struct hotplug_slot_ops eeepc_hotplug_slot_ops = {
	.owner = THIS_MODULE,
	.get_adapter_status = eeepc_get_adapter_status,
	.get_power_status = eeepc_get_adapter_status,
};

/* The backlight device /sys/class/backlight */
static struct backlight_device *eeepc_backlight_device;

/* The hwmon device */
static struct device *eeepc_hwmon_device;

/*
 * The backlight class declaration
 */
static int read_brightness(struct backlight_device *bd);
static int update_bl_status(struct backlight_device *bd);
static struct backlight_ops eeepcbl_ops = {
	.get_brightness = read_brightness,
	.update_status = update_bl_status,
};

MODULE_AUTHOR("Corentin Chary, Eric Cooper");
MODULE_DESCRIPTION(EEEPC_HOTK_NAME);
MODULE_LICENSE("GPL");

/*
 * ACPI Helpers
 */
static int write_acpi_int(acpi_handle handle, const char *method, int val,
			  struct acpi_buffer *output)
{
	struct acpi_object_list params;
	union acpi_object in_obj;
	acpi_status status;

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = val;

	status = acpi_evaluate_object(handle, (char *)method, &params, output);
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

static int set_acpi(int cm, int value)
{
	if (ehotk->cm_supported & (0x1 << cm)) {
		const char *method = cm_setv[cm];
		if (method == NULL)
			return -ENODEV;
		if (write_acpi_int(ehotk->handle, method, value, NULL))
			pr_warning("Error writing %s\n", method);
	}
	return 0;
}

static int get_acpi(int cm)
{
	int value = -ENODEV;
	if ((ehotk->cm_supported & (0x1 << cm))) {
		const char *method = cm_getv[cm];
		if (method == NULL)
			return -ENODEV;
		if (read_acpi_int(ehotk->handle, method, &value))
			pr_warning("Error reading %s\n", method);
	}
	return value;
}

/*
 * Backlight
 */
static int read_brightness(struct backlight_device *bd)
{
	return get_acpi(CM_ASL_PANELBRIGHT);
}

static int set_brightness(struct backlight_device *bd, int value)
{
	value = max(0, min(15, value));
	return set_acpi(CM_ASL_PANELBRIGHT, value);
}

static int update_bl_status(struct backlight_device *bd)
{
	return set_brightness(bd, bd->props.brightness);
}

/*
 * Rfkill helpers
 */

static bool eeepc_wlan_rfkill_blocked(void)
{
	if (get_acpi(CM_ASL_WLAN) == 1)
		return false;
	return true;
}

static int eeepc_rfkill_set(void *data, bool blocked)
{
	unsigned long asl = (unsigned long)data;
	return set_acpi(asl, !blocked);
}

static const struct rfkill_ops eeepc_rfkill_ops = {
	.set_block = eeepc_rfkill_set,
};

static void __devinit eeepc_enable_camera(void)
{
	/*
	 * If the following call to set_acpi() fails, it's because there's no
	 * camera so we can ignore the error.
	 */
	if (get_acpi(CM_ASL_CAMERA) == 0)
		set_acpi(CM_ASL_CAMERA, 1);
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

static ssize_t store_sys_acpi(int cm, const char *buf, size_t count)
{
	int rv, value;

	rv = parse_arg(buf, count, &value);
	if (rv > 0)
		value = set_acpi(cm, value);
	if (value < 0)
		return value;
	return rv;
}

static ssize_t show_sys_acpi(int cm, char *buf)
{
	int value = get_acpi(cm);

	if (value < 0)
		return value;
	return sprintf(buf, "%d\n", value);
}

#define EEEPC_CREATE_DEVICE_ATTR(_name, _cm)				\
	static ssize_t show_##_name(struct device *dev,			\
				    struct device_attribute *attr,	\
				    char *buf)				\
	{								\
		return show_sys_acpi(_cm, buf);				\
	}								\
	static ssize_t store_##_name(struct device *dev,		\
				     struct device_attribute *attr,	\
				     const char *buf, size_t count)	\
	{								\
		return store_sys_acpi(_cm, buf, count);			\
	}								\
	static struct device_attribute dev_attr_##_name = {		\
		.attr = {						\
			.name = __stringify(_name),			\
			.mode = 0644 },					\
		.show   = show_##_name,					\
		.store  = store_##_name,				\
	}

EEEPC_CREATE_DEVICE_ATTR(camera, CM_ASL_CAMERA);
EEEPC_CREATE_DEVICE_ATTR(cardr, CM_ASL_CARDREADER);
EEEPC_CREATE_DEVICE_ATTR(disp, CM_ASL_DISPLAYSWITCH);

struct eeepc_cpufv {
	int num;
	int cur;
};

static int get_cpufv(struct eeepc_cpufv *c)
{
	c->cur = get_acpi(CM_ASL_CPUFV);
	c->num = (c->cur >> 8) & 0xff;
	c->cur &= 0xff;
	if (c->cur < 0 || c->num <= 0 || c->num > 12)
		return -ENODEV;
	return 0;
}

static ssize_t show_available_cpufv(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct eeepc_cpufv c;
	int i;
	ssize_t len = 0;

	if (get_cpufv(&c))
		return -ENODEV;
	for (i = 0; i < c.num; i++)
		len += sprintf(buf + len, "%d ", i);
	len += sprintf(buf + len, "\n");
	return len;
}

static ssize_t show_cpufv(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct eeepc_cpufv c;

	if (get_cpufv(&c))
		return -ENODEV;
	return sprintf(buf, "%#x\n", (c.num << 8) | c.cur);
}

static ssize_t store_cpufv(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct eeepc_cpufv c;
	int rv, value;

	if (get_cpufv(&c))
		return -ENODEV;
	rv = parse_arg(buf, count, &value);
	if (rv < 0)
		return rv;
	if (!rv || value < 0 || value >= c.num)
		return -EINVAL;
	set_acpi(CM_ASL_CPUFV, value);
	return rv;
}

static struct device_attribute dev_attr_cpufv = {
	.attr = {
		.name = "cpufv",
		.mode = 0644 },
	.show   = show_cpufv,
	.store  = store_cpufv
};

static struct device_attribute dev_attr_available_cpufv = {
	.attr = {
		.name = "available_cpufv",
		.mode = 0444 },
	.show   = show_available_cpufv
};

static struct attribute *platform_attributes[] = {
	&dev_attr_camera.attr,
	&dev_attr_cardr.attr,
	&dev_attr_disp.attr,
	&dev_attr_cpufv.attr,
	&dev_attr_available_cpufv.attr,
	NULL
};

static struct attribute_group platform_attribute_group = {
	.attrs = platform_attributes
};

/*
 * Hotkey functions
 */
static struct key_entry *eepc_get_entry_by_scancode(int code)
{
	struct key_entry *key;

	for (key = eeepc_keymap; key->type != KE_END; key++)
		if (code == key->code)
			return key;

	return NULL;
}

static struct key_entry *eepc_get_entry_by_keycode(int code)
{
	struct key_entry *key;

	for (key = eeepc_keymap; key->type != KE_END; key++)
		if (code == key->keycode && key->type == KE_KEY)
			return key;

	return NULL;
}

static int eeepc_getkeycode(struct input_dev *dev, int scancode, int *keycode)
{
	struct key_entry *key = eepc_get_entry_by_scancode(scancode);

	if (key && key->type == KE_KEY) {
		*keycode = key->keycode;
		return 0;
	}

	return -EINVAL;
}

static int eeepc_setkeycode(struct input_dev *dev, int scancode, int keycode)
{
	struct key_entry *key;
	int old_keycode;

	if (keycode < 0 || keycode > KEY_MAX)
		return -EINVAL;

	key = eepc_get_entry_by_scancode(scancode);
	if (key && key->type == KE_KEY) {
		old_keycode = key->keycode;
		key->keycode = keycode;
		set_bit(keycode, dev->keybit);
		if (!eepc_get_entry_by_keycode(old_keycode))
			clear_bit(old_keycode, dev->keybit);
		return 0;
	}

	return -EINVAL;
}

static void cmsg_quirk(int cm, const char *name)
{
	int dummy;

	/* Some BIOSes do not report cm although it is avaliable.
	   Check if cm_getv[cm] works and, if yes, assume cm should be set. */
	if (!(ehotk->cm_supported & (1 << cm))
	    && !read_acpi_int(ehotk->handle, cm_getv[cm], &dummy)) {
		pr_info("%s (%x) not reported by BIOS,"
			" enabling anyway\n", name, 1 << cm);
		ehotk->cm_supported |= 1 << cm;
	}
}

static void cmsg_quirks(void)
{
	cmsg_quirk(CM_ASL_LID, "LID");
	cmsg_quirk(CM_ASL_TYPE, "TYPE");
	cmsg_quirk(CM_ASL_PANELPOWER, "PANELPOWER");
	cmsg_quirk(CM_ASL_TPD, "TPD");
}

static int eeepc_hotk_check(void)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	int result;

	result = acpi_bus_get_status(ehotk->device);
	if (result)
		return result;
	if (ehotk->device->status.present) {
		if (write_acpi_int(ehotk->handle, "INIT", ehotk->init_flag,
				    &buffer)) {
			pr_err("Hotkey initialization failed\n");
			return -ENODEV;
		} else {
			pr_notice("Hotkey init flags 0x%x\n", ehotk->init_flag);
		}
		/* get control methods supported */
		if (read_acpi_int(ehotk->handle, "CMSG"
				   , &ehotk->cm_supported)) {
			pr_err("Get control methods supported failed\n");
			return -ENODEV;
		} else {
			cmsg_quirks();
			pr_info("Get control methods supported: 0x%x\n",
				ehotk->cm_supported);
		}
	} else {
		pr_err("Hotkey device not present, aborting\n");
		return -EINVAL;
	}
	return 0;
}

static int notify_brn(void)
{
	/* returns the *previous* brightness, or -1 */
	struct backlight_device *bd = eeepc_backlight_device;
	if (bd) {
		int old = bd->props.brightness;
		backlight_force_update(bd, BACKLIGHT_UPDATE_HOTKEY);
		return old;
	}
	return -1;
}

static int eeepc_get_adapter_status(struct hotplug_slot *hotplug_slot,
				    u8 *value)
{
	int val = get_acpi(CM_ASL_WLAN);

	if (val == 1 || val == 0)
		*value = val;
	else
		return -EINVAL;

	return 0;
}

static void eeepc_rfkill_hotplug(void)
{
	struct pci_dev *dev;
	struct pci_bus *bus;
	bool blocked = eeepc_wlan_rfkill_blocked();

	if (ehotk->wlan_rfkill)
		rfkill_set_sw_state(ehotk->wlan_rfkill, blocked);

	mutex_lock(&ehotk->hotplug_lock);

	if (ehotk->hotplug_slot) {
		bus = pci_find_bus(0, 1);
		if (!bus) {
			pr_warning("Unable to find PCI bus 1?\n");
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
	mutex_unlock(&ehotk->hotplug_lock);
}

static void eeepc_rfkill_notify(acpi_handle handle, u32 event, void *data)
{
	if (event != ACPI_NOTIFY_BUS_CHECK)
		return;

	eeepc_rfkill_hotplug();
}

static void eeepc_hotk_notify(struct acpi_device *device, u32 event)
{
	static struct key_entry *key;
	u16 count;
	int brn = -ENODEV;

	if (!ehotk)
		return;
	if (event > ACPI_MAX_SYS_NOTIFY)
		return;
	if (event >= NOTIFY_BRN_MIN && event <= NOTIFY_BRN_MAX)
		brn = notify_brn();
	count = ehotk->event_count[event % 128]++;
	acpi_bus_generate_proc_event(ehotk->device, event, count);
	acpi_bus_generate_netlink_event(ehotk->device->pnp.device_class,
					dev_name(&ehotk->device->dev), event,
					count);
	if (ehotk->inputdev) {
		if (brn != -ENODEV) {
			/* brightness-change events need special
			 * handling for conversion to key events
			 */
			if (brn < 0)
				brn = event;
			else
				brn += NOTIFY_BRN_MIN;
			if (event < brn)
				event = NOTIFY_BRN_MIN; /* brightness down */
			else if (event > brn)
				event = NOTIFY_BRN_MIN + 2; /* ... up */
			else
				event = NOTIFY_BRN_MIN + 1; /* ... unchanged */
		}
		key = eepc_get_entry_by_scancode(event);
		if (key) {
			switch (key->type) {
			case KE_KEY:
				input_report_key(ehotk->inputdev, key->keycode,
						 1);
				input_sync(ehotk->inputdev);
				input_report_key(ehotk->inputdev, key->keycode,
						 0);
				input_sync(ehotk->inputdev);
				break;
			}
		}
	}
}

static int eeepc_register_rfkill_notifier(char *node)
{
	acpi_status status = AE_OK;
	acpi_handle handle;

	status = acpi_get_handle(NULL, node, &handle);

	if (ACPI_SUCCESS(status)) {
		status = acpi_install_notify_handler(handle,
						     ACPI_SYSTEM_NOTIFY,
						     eeepc_rfkill_notify,
						     NULL);
		if (ACPI_FAILURE(status))
			pr_warning("Failed to register notify on %s\n", node);
	} else
		return -ENODEV;

	return 0;
}

static void eeepc_unregister_rfkill_notifier(char *node)
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

static void eeepc_cleanup_pci_hotplug(struct hotplug_slot *hotplug_slot)
{
	kfree(hotplug_slot->info);
	kfree(hotplug_slot);
}

static int eeepc_setup_pci_hotplug(void)
{
	int ret = -ENOMEM;
	struct pci_bus *bus = pci_find_bus(0, 1);

	if (!bus) {
		pr_err("Unable to find wifi PCI bus\n");
		return -ENODEV;
	}

	ehotk->hotplug_slot = kzalloc(sizeof(struct hotplug_slot), GFP_KERNEL);
	if (!ehotk->hotplug_slot)
		goto error_slot;

	ehotk->hotplug_slot->info = kzalloc(sizeof(struct hotplug_slot_info),
					    GFP_KERNEL);
	if (!ehotk->hotplug_slot->info)
		goto error_info;

	ehotk->hotplug_slot->private = ehotk;
	ehotk->hotplug_slot->release = &eeepc_cleanup_pci_hotplug;
	ehotk->hotplug_slot->ops = &eeepc_hotplug_slot_ops;
	eeepc_get_adapter_status(ehotk->hotplug_slot,
				 &ehotk->hotplug_slot->info->adapter_status);

	ret = pci_hp_register(ehotk->hotplug_slot, bus, 0, "eeepc-wifi");
	if (ret) {
		pr_err("Unable to register hotplug slot - %d\n", ret);
		goto error_register;
	}

	return 0;

error_register:
	kfree(ehotk->hotplug_slot->info);
error_info:
	kfree(ehotk->hotplug_slot);
	ehotk->hotplug_slot = NULL;
error_slot:
	return ret;
}

static int eeepc_hotk_thaw(struct device *device)
{
	if (ehotk->wlan_rfkill) {
		bool wlan;

		/*
		 * Work around bios bug - acpi _PTS turns off the wireless led
		 * during suspend.  Normally it restores it on resume, but
		 * we should kick it ourselves in case hibernation is aborted.
		 */
		wlan = get_acpi(CM_ASL_WLAN);
		set_acpi(CM_ASL_WLAN, wlan);
	}

	return 0;
}

static int eeepc_hotk_restore(struct device *device)
{
	/* Refresh both wlan rfkill state and pci hotplug */
	if (ehotk->wlan_rfkill)
		eeepc_rfkill_hotplug();

	if (ehotk->bluetooth_rfkill)
		rfkill_set_sw_state(ehotk->bluetooth_rfkill,
				    get_acpi(CM_ASL_BLUETOOTH) != 1);
	if (ehotk->wwan3g_rfkill)
		rfkill_set_sw_state(ehotk->wwan3g_rfkill,
				    get_acpi(CM_ASL_3G) != 1);
	if (ehotk->wimax_rfkill)
		rfkill_set_sw_state(ehotk->wimax_rfkill,
				    get_acpi(CM_ASL_WIMAX) != 1);

	return 0;
}

/*
 * Hwmon
 */
static int eeepc_get_fan_pwm(void)
{
	int value = 0;

	read_acpi_int(NULL, EEEPC_EC_FAN_PWM, &value);
	value = value * 255 / 100;
	return (value);
}

static void eeepc_set_fan_pwm(int value)
{
	value = SENSORS_LIMIT(value, 0, 255);
	value = value * 100 / 255;
	ec_write(EEEPC_EC_SC02, value);
}

static int eeepc_get_fan_rpm(void)
{
	int high = 0;
	int low = 0;

	read_acpi_int(NULL, EEEPC_EC_FAN_HRPM, &high);
	read_acpi_int(NULL, EEEPC_EC_FAN_LRPM, &low);
	return (high << 8 | low);
}

static int eeepc_get_fan_ctrl(void)
{
	int value = 0;

	read_acpi_int(NULL, EEEPC_EC_FAN_CTRL, &value);
	return ((value & 0x02 ? 1 : 0));
}

static void eeepc_set_fan_ctrl(int manual)
{
	int value = 0;

	read_acpi_int(NULL, EEEPC_EC_FAN_CTRL, &value);
	if (manual)
		value |= 0x02;
	else
		value &= ~0x02;
	ec_write(EEEPC_EC_SFB3, value);
}

static ssize_t store_sys_hwmon(void (*set)(int), const char *buf, size_t count)
{
	int rv, value;

	rv = parse_arg(buf, count, &value);
	if (rv > 0)
		set(value);
	return rv;
}

static ssize_t show_sys_hwmon(int (*get)(void), char *buf)
{
	return sprintf(buf, "%d\n", get());
}

#define EEEPC_CREATE_SENSOR_ATTR(_name, _mode, _set, _get)		\
	static ssize_t show_##_name(struct device *dev,			\
				    struct device_attribute *attr,	\
				    char *buf)				\
	{								\
		return show_sys_hwmon(_set, buf);			\
	}								\
	static ssize_t store_##_name(struct device *dev,		\
				     struct device_attribute *attr,	\
				     const char *buf, size_t count)	\
	{								\
		return store_sys_hwmon(_get, buf, count);		\
	}								\
	static SENSOR_DEVICE_ATTR(_name, _mode, show_##_name, store_##_name, 0);

EEEPC_CREATE_SENSOR_ATTR(fan1_input, S_IRUGO, eeepc_get_fan_rpm, NULL);
EEEPC_CREATE_SENSOR_ATTR(pwm1, S_IRUGO | S_IWUSR,
			 eeepc_get_fan_pwm, eeepc_set_fan_pwm);
EEEPC_CREATE_SENSOR_ATTR(pwm1_enable, S_IRUGO | S_IWUSR,
			 eeepc_get_fan_ctrl, eeepc_set_fan_ctrl);

static ssize_t
show_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "eeepc\n");
}
static SENSOR_DEVICE_ATTR(name, S_IRUGO, show_name, NULL, 0);

static struct attribute *hwmon_attributes[] = {
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_name.dev_attr.attr,
	NULL
};

static struct attribute_group hwmon_attribute_group = {
	.attrs = hwmon_attributes
};

/*
 * exit/init
 */
static void eeepc_backlight_exit(void)
{
	if (eeepc_backlight_device)
		backlight_device_unregister(eeepc_backlight_device);
	eeepc_backlight_device = NULL;
}

static void eeepc_rfkill_exit(void)
{
	eeepc_unregister_rfkill_notifier("\\_SB.PCI0.P0P5");
	eeepc_unregister_rfkill_notifier("\\_SB.PCI0.P0P6");
	eeepc_unregister_rfkill_notifier("\\_SB.PCI0.P0P7");
	if (ehotk->wlan_rfkill) {
		rfkill_unregister(ehotk->wlan_rfkill);
		rfkill_destroy(ehotk->wlan_rfkill);
		ehotk->wlan_rfkill = NULL;
	}
	/*
	 * Refresh pci hotplug in case the rfkill state was changed after
	 * eeepc_unregister_rfkill_notifier()
	 */
	eeepc_rfkill_hotplug();
	if (ehotk->hotplug_slot)
		pci_hp_deregister(ehotk->hotplug_slot);

	if (ehotk->bluetooth_rfkill) {
		rfkill_unregister(ehotk->bluetooth_rfkill);
		rfkill_destroy(ehotk->bluetooth_rfkill);
		ehotk->bluetooth_rfkill = NULL;
	}
	if (ehotk->wwan3g_rfkill) {
		rfkill_unregister(ehotk->wwan3g_rfkill);
		rfkill_destroy(ehotk->wwan3g_rfkill);
		ehotk->wwan3g_rfkill = NULL;
	}
	if (ehotk->wimax_rfkill) {
		rfkill_unregister(ehotk->wimax_rfkill);
		rfkill_destroy(ehotk->wimax_rfkill);
		ehotk->wimax_rfkill = NULL;
	}
}

static void eeepc_input_exit(void)
{
	if (ehotk->inputdev)
		input_unregister_device(ehotk->inputdev);
}

static void eeepc_hwmon_exit(void)
{
	struct device *hwmon;

	hwmon = eeepc_hwmon_device;
	if (!hwmon)
		return ;
	sysfs_remove_group(&hwmon->kobj,
			   &hwmon_attribute_group);
	hwmon_device_unregister(hwmon);
	eeepc_hwmon_device = NULL;
}

static int eeepc_new_rfkill(struct rfkill **rfkill,
			    const char *name, struct device *dev,
			    enum rfkill_type type, int cm)
{
	int result;

	result = get_acpi(cm);
	if (result < 0)
		return result;

	*rfkill = rfkill_alloc(name, dev, type,
			       &eeepc_rfkill_ops, (void *)(unsigned long)cm);

	if (!*rfkill)
		return -EINVAL;

	rfkill_init_sw_state(*rfkill, get_acpi(cm) != 1);
	result = rfkill_register(*rfkill);
	if (result) {
		rfkill_destroy(*rfkill);
		*rfkill = NULL;
		return result;
	}
	return 0;
}


static int eeepc_rfkill_init(struct device *dev)
{
	int result = 0;

	mutex_init(&ehotk->hotplug_lock);

	result = eeepc_new_rfkill(&ehotk->wlan_rfkill,
				  "eeepc-wlan", dev,
				  RFKILL_TYPE_WLAN, CM_ASL_WLAN);

	if (result && result != -ENODEV)
		goto exit;

	result = eeepc_new_rfkill(&ehotk->bluetooth_rfkill,
				  "eeepc-bluetooth", dev,
				  RFKILL_TYPE_BLUETOOTH, CM_ASL_BLUETOOTH);

	if (result && result != -ENODEV)
		goto exit;

	result = eeepc_new_rfkill(&ehotk->wwan3g_rfkill,
				  "eeepc-wwan3g", dev,
				  RFKILL_TYPE_WWAN, CM_ASL_3G);

	if (result && result != -ENODEV)
		goto exit;

	result = eeepc_new_rfkill(&ehotk->wimax_rfkill,
				  "eeepc-wimax", dev,
				  RFKILL_TYPE_WIMAX, CM_ASL_WIMAX);

	if (result && result != -ENODEV)
		goto exit;

	result = eeepc_setup_pci_hotplug();
	/*
	 * If we get -EBUSY then something else is handling the PCI hotplug -
	 * don't fail in this case
	 */
	if (result == -EBUSY)
		result = 0;

	eeepc_register_rfkill_notifier("\\_SB.PCI0.P0P5");
	eeepc_register_rfkill_notifier("\\_SB.PCI0.P0P6");
	eeepc_register_rfkill_notifier("\\_SB.PCI0.P0P7");
	/*
	 * Refresh pci hotplug in case the rfkill state was changed during
	 * setup.
	 */
	eeepc_rfkill_hotplug();

exit:
	if (result && result != -ENODEV)
		eeepc_rfkill_exit();
	return result;
}

static int eeepc_backlight_init(struct device *dev)
{
	struct backlight_device *bd;

	bd = backlight_device_register(EEEPC_HOTK_FILE, dev,
				       NULL, &eeepcbl_ops);
	if (IS_ERR(bd)) {
		pr_err("Could not register eeepc backlight device\n");
		eeepc_backlight_device = NULL;
		return PTR_ERR(bd);
	}
	eeepc_backlight_device = bd;
	bd->props.max_brightness = 15;
	bd->props.brightness = read_brightness(NULL);
	bd->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(bd);
	return 0;
}

static int eeepc_hwmon_init(struct device *dev)
{
	struct device *hwmon;
	int result;

	hwmon = hwmon_device_register(dev);
	if (IS_ERR(hwmon)) {
		pr_err("Could not register eeepc hwmon device\n");
		eeepc_hwmon_device = NULL;
		return PTR_ERR(hwmon);
	}
	eeepc_hwmon_device = hwmon;
	result = sysfs_create_group(&hwmon->kobj,
				    &hwmon_attribute_group);
	if (result)
		eeepc_hwmon_exit();
	return result;
}

static int eeepc_input_init(struct device *dev)
{
	const struct key_entry *key;
	int result;

	ehotk->inputdev = input_allocate_device();
	if (!ehotk->inputdev) {
		pr_info("Unable to allocate input device\n");
		return -ENOMEM;
	}
	ehotk->inputdev->name = "Asus EeePC extra buttons";
	ehotk->inputdev->dev.parent = dev;
	ehotk->inputdev->phys = EEEPC_HOTK_FILE "/input0";
	ehotk->inputdev->id.bustype = BUS_HOST;
	ehotk->inputdev->getkeycode = eeepc_getkeycode;
	ehotk->inputdev->setkeycode = eeepc_setkeycode;

	for (key = eeepc_keymap; key->type != KE_END; key++) {
		switch (key->type) {
		case KE_KEY:
			set_bit(EV_KEY, ehotk->inputdev->evbit);
			set_bit(key->keycode, ehotk->inputdev->keybit);
			break;
		}
	}
	result = input_register_device(ehotk->inputdev);
	if (result) {
		pr_info("Unable to register input device\n");
		input_free_device(ehotk->inputdev);
		return result;
	}
	return 0;
}

static int __devinit eeepc_hotk_add(struct acpi_device *device)
{
	struct device *dev;
	int result;

	if (!device)
		return -EINVAL;
	pr_notice(EEEPC_HOTK_NAME "\n");
	ehotk = kzalloc(sizeof(struct eeepc_hotk), GFP_KERNEL);
	if (!ehotk)
		return -ENOMEM;
	ehotk->init_flag = DISABLE_ASL_WLAN | DISABLE_ASL_DISPLAYSWITCH;
	ehotk->handle = device->handle;
	strcpy(acpi_device_name(device), EEEPC_HOTK_DEVICE_NAME);
	strcpy(acpi_device_class(device), EEEPC_HOTK_CLASS);
	device->driver_data = ehotk;
	ehotk->device = device;

	result = eeepc_hotk_check();
	if (result)
		goto fail_platform_driver;
	eeepc_enable_camera();

	/* Register platform stuff */
	result = platform_driver_register(&platform_driver);
	if (result)
		goto fail_platform_driver;
	platform_device = platform_device_alloc(EEEPC_HOTK_FILE, -1);
	if (!platform_device) {
		result = -ENOMEM;
		goto fail_platform_device1;
	}
	result = platform_device_add(platform_device);
	if (result)
		goto fail_platform_device2;
	result = sysfs_create_group(&platform_device->dev.kobj,
				    &platform_attribute_group);
	if (result)
		goto fail_sysfs;

	dev = &platform_device->dev;

	if (!acpi_video_backlight_support()) {
		result = eeepc_backlight_init(dev);
		if (result)
			goto fail_backlight;
	} else
		pr_info("Backlight controlled by ACPI video "
			"driver\n");

	result = eeepc_input_init(dev);
	if (result)
		goto fail_input;

	result = eeepc_hwmon_init(dev);
	if (result)
		goto fail_hwmon;

	result = eeepc_rfkill_init(dev);
	if (result)
		goto fail_rfkill;

	return 0;

fail_rfkill:
	eeepc_hwmon_exit();
fail_hwmon:
	eeepc_input_exit();
fail_input:
	eeepc_backlight_exit();
fail_backlight:
	sysfs_remove_group(&platform_device->dev.kobj,
			   &platform_attribute_group);
fail_sysfs:
	platform_device_del(platform_device);
fail_platform_device2:
	platform_device_put(platform_device);
fail_platform_device1:
	platform_driver_unregister(&platform_driver);
fail_platform_driver:
	kfree(ehotk);

	return result;
}

static int eeepc_hotk_remove(struct acpi_device *device, int type)
{
	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	eeepc_backlight_exit();
	eeepc_rfkill_exit();
	eeepc_input_exit();
	eeepc_hwmon_exit();
	sysfs_remove_group(&platform_device->dev.kobj,
			   &platform_attribute_group);
	platform_device_unregister(platform_device);
	platform_driver_unregister(&platform_driver);

	kfree(ehotk);
	return 0;
}

static int __init eeepc_laptop_init(void)
{
	int result;

	if (acpi_disabled)
		return -ENODEV;
	result = acpi_bus_register_driver(&eeepc_hotk_driver);
	if (result < 0)
		return result;
	if (!ehotk) {
		acpi_bus_unregister_driver(&eeepc_hotk_driver);
		return -ENODEV;
	}
	return 0;
}

static void __exit eeepc_laptop_exit(void)
{
	acpi_bus_unregister_driver(&eeepc_hotk_driver);
}

module_init(eeepc_laptop_init);
module_exit(eeepc_laptop_exit);
