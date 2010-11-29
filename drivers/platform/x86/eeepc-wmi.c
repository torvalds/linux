/*
 * Eee PC WMI hotkey driver
 *
 * Copyright(C) 2010 Intel Corporation.
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
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define	EEEPC_WMI_FILE	"eeepc-wmi"

MODULE_AUTHOR("Yong Wang <yong.y.wang@intel.com>");
MODULE_DESCRIPTION("Eee PC WMI Hotkey Driver");
MODULE_LICENSE("GPL");

#define EEEPC_WMI_EVENT_GUID	"ABBC0F72-8EA1-11D1-00A0-C90629100000"
#define EEEPC_WMI_MGMT_GUID	"97845ED0-4E6D-11DE-8A39-0800200C9A66"

MODULE_ALIAS("wmi:"EEEPC_WMI_EVENT_GUID);
MODULE_ALIAS("wmi:"EEEPC_WMI_MGMT_GUID);

#define NOTIFY_BRNUP_MIN	0x11
#define NOTIFY_BRNUP_MAX	0x1f
#define NOTIFY_BRNDOWN_MIN	0x20
#define NOTIFY_BRNDOWN_MAX	0x2e

#define EEEPC_WMI_METHODID_DEVS	0x53564544
#define EEEPC_WMI_METHODID_DSTS	0x53544344
#define EEEPC_WMI_METHODID_CFVS	0x53564643

#define EEEPC_WMI_DEVID_BACKLIGHT	0x00050012
#define EEEPC_WMI_DEVID_TPDLED		0x00100011
#define EEEPC_WMI_DEVID_WLAN		0x00010011
#define EEEPC_WMI_DEVID_BLUETOOTH	0x00010013
#define EEEPC_WMI_DEVID_WWAN3G		0x00010019

static const struct key_entry eeepc_wmi_keymap[] = {
	/* Sleep already handled via generic ACPI code */
	{ KE_KEY, 0x5d, { KEY_WLAN } },
	{ KE_KEY, 0x32, { KEY_MUTE } },
	{ KE_KEY, 0x31, { KEY_VOLUMEDOWN } },
	{ KE_KEY, 0x30, { KEY_VOLUMEUP } },
	{ KE_IGNORE, NOTIFY_BRNDOWN_MIN, { KEY_BRIGHTNESSDOWN } },
	{ KE_IGNORE, NOTIFY_BRNUP_MIN, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 0xcc, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0x6b, { KEY_F13 } }, /* Disable Touchpad */
	{ KE_KEY, 0xe1, { KEY_F14 } },
	{ KE_KEY, 0xe9, { KEY_DISPLAY_OFF } },
	{ KE_KEY, 0xe0, { KEY_PROG1 } },
	{ KE_KEY, 0x5c, { KEY_F15 } },
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
	struct input_dev *inputdev;
	struct backlight_device *backlight_device;
	struct platform_device *platform_device;

	struct led_classdev tpd_led;
	int tpd_led_wk;
	struct workqueue_struct *led_workqueue;
	struct work_struct tpd_led_work;

	struct rfkill *wlan_rfkill;
	struct rfkill *bluetooth_rfkill;
	struct rfkill *wwan3g_rfkill;

	struct eeepc_wmi_debug debug;
};

/* Only used in eeepc_wmi_init() and eeepc_wmi_exit() */
static struct platform_device *platform_device;

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

static acpi_status eeepc_wmi_get_devstate(u32 dev_id, u32 *ctrl_param)
{
	struct acpi_buffer input = { (acpi_size)sizeof(u32), &dev_id };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	u32 tmp;

	status = wmi_evaluate_method(EEEPC_WMI_MGMT_GUID,
			1, EEEPC_WMI_METHODID_DSTS, &input, &output);

	if (ACPI_FAILURE(status))
		return status;

	obj = (union acpi_object *)output.pointer;
	if (obj && obj->type == ACPI_TYPE_INTEGER)
		tmp = (u32)obj->integer.value;
	else
		tmp = 0;

	if (ctrl_param)
		*ctrl_param = tmp;

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
	eeepc_wmi_set_devstate(EEEPC_WMI_DEVID_TPDLED, ctrl_param, NULL);
}

static void tpd_led_set(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct eeepc_wmi *eeepc;

	eeepc = container_of(led_cdev, struct eeepc_wmi, tpd_led);

	eeepc->tpd_led_wk = !!value;
	queue_work(eeepc->led_workqueue, &eeepc->tpd_led_work);
}

static int read_tpd_state(struct eeepc_wmi *eeepc)
{
	static u32 ctrl_param;
	acpi_status status;

	status = eeepc_wmi_get_devstate(EEEPC_WMI_DEVID_TPDLED, &ctrl_param);

	if (ACPI_FAILURE(status))
		return -1;
	else if (!ctrl_param || ctrl_param == 0x00060000)
		/*
		 * if touchpad led is present, DSTS will set some bits,
		 * usually 0x00020000.
		 * 0x00060000 means that the device is not supported
		 */
		return -ENODEV;
	else
		/* Status is stored in the first bit */
		return ctrl_param & 0x1;
}

static enum led_brightness tpd_led_get(struct led_classdev *led_cdev)
{
	struct eeepc_wmi *eeepc;

	eeepc = container_of(led_cdev, struct eeepc_wmi, tpd_led);

	return read_tpd_state(eeepc);
}

static int eeepc_wmi_led_init(struct eeepc_wmi *eeepc)
{
	int rv;

	if (read_tpd_state(eeepc) < 0)
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
 * Rfkill devices
 */
static int eeepc_rfkill_set(void *data, bool blocked)
{
	int dev_id = (unsigned long)data;
	u32 ctrl_param = !blocked;

	return eeepc_wmi_set_devstate(dev_id, ctrl_param, NULL);
}

static void eeepc_rfkill_query(struct rfkill *rfkill, void *data)
{
	int dev_id = (unsigned long)data;
	u32 ctrl_param;
	acpi_status status;

	status = eeepc_wmi_get_devstate(dev_id, &ctrl_param);

	if (ACPI_FAILURE(status))
		return ;

	rfkill_set_sw_state(rfkill, !(ctrl_param & 0x1));
}

static const struct rfkill_ops eeepc_rfkill_ops = {
	.set_block = eeepc_rfkill_set,
	.query = eeepc_rfkill_query,
};

static int eeepc_new_rfkill(struct eeepc_wmi *eeepc,
			    struct rfkill **rfkill,
			    const char *name,
			    enum rfkill_type type, int dev_id)
{
	int result;
	u32 ctrl_param;
	acpi_status status;

	status = eeepc_wmi_get_devstate(dev_id, &ctrl_param);

	if (ACPI_FAILURE(status))
		return -1;

	/* If the device is present, DSTS will always set some bits
	 * 0x00070000 - 1110000000000000000 - device supported
	 * 0x00060000 - 1100000000000000000 - not supported
	 * 0x00020000 - 0100000000000000000 - device supported
	 * 0x00010000 - 0010000000000000000 - not supported / special mode ?
	 */
	if (!ctrl_param || ctrl_param == 0x00060000)
		return -ENODEV;

	*rfkill = rfkill_alloc(name, &eeepc->platform_device->dev, type,
			       &eeepc_rfkill_ops, (void *)(long)dev_id);

	if (!*rfkill)
		return -EINVAL;

	rfkill_init_sw_state(*rfkill, !(ctrl_param & 0x1));
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
	if (eeepc->wlan_rfkill) {
		rfkill_unregister(eeepc->wlan_rfkill);
		rfkill_destroy(eeepc->wlan_rfkill);
		eeepc->wlan_rfkill = NULL;
	}
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
}

static int eeepc_wmi_rfkill_init(struct eeepc_wmi *eeepc)
{
	int result = 0;

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

	result = eeepc_new_rfkill(eeepc, &eeepc->wwan3g_rfkill,
				  "eeepc-wwan3g", RFKILL_TYPE_WWAN,
				  EEEPC_WMI_DEVID_WWAN3G);

	if (result && result != -ENODEV)
		goto exit;

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
static int read_brightness(struct backlight_device *bd)
{
	static u32 ctrl_param;
	acpi_status status;

	status = eeepc_wmi_get_devstate(EEEPC_WMI_DEVID_BACKLIGHT, &ctrl_param);

	if (ACPI_FAILURE(status))
		return -1;
	else
		return ctrl_param & 0xFF;
}

static int update_bl_status(struct backlight_device *bd)
{

	static u32 ctrl_param;
	acpi_status status;

	ctrl_param = bd->props.brightness;

	status = eeepc_wmi_set_devstate(EEEPC_WMI_DEVID_BACKLIGHT,
					ctrl_param, NULL);

	if (ACPI_FAILURE(status))
		return -1;
	else
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

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = 15;
	bd = backlight_device_register(EEEPC_WMI_FILE,
				       &eeepc->platform_device->dev, eeepc,
				       &eeepc_wmi_bl_ops, &props);
	if (IS_ERR(bd)) {
		pr_err("Could not register backlight device\n");
		return PTR_ERR(bd);
	}

	eeepc->backlight_device = bd;

	bd->props.brightness = read_brightness(bd);
	bd->props.power = FB_BLANK_UNBLANK;
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
	NULL
};

static struct attribute_group platform_attribute_group = {
	.attrs = platform_attributes
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
	int err;

	eeepc->platform_device = platform_device_alloc(EEEPC_WMI_FILE, -1);
	if (!eeepc->platform_device)
		return -ENOMEM;
	platform_set_drvdata(eeepc->platform_device, eeepc);

	err = platform_device_add(eeepc->platform_device);
	if (err)
		goto fail_platform_device;

	err = eeepc_wmi_sysfs_init(eeepc->platform_device);
	if (err)
		goto fail_sysfs;
	return 0;

fail_sysfs:
	platform_device_del(eeepc->platform_device);
fail_platform_device:
	platform_device_put(eeepc->platform_device);
	return err;
}

static void eeepc_wmi_platform_exit(struct eeepc_wmi *eeepc)
{
	eeepc_wmi_sysfs_exit(eeepc->platform_device);
	platform_device_unregister(eeepc->platform_device);
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
static struct platform_device * __init eeepc_wmi_add(void)
{
	struct eeepc_wmi *eeepc;
	acpi_status status;
	int err;

	eeepc = kzalloc(sizeof(struct eeepc_wmi), GFP_KERNEL);
	if (!eeepc)
		return ERR_PTR(-ENOMEM);

	/*
	 * Register the platform device first.  It is used as a parent for the
	 * sub-devices below.
	 */
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
		if (err)
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

	return eeepc->platform_device;

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
	return ERR_PTR(err);
}

static int eeepc_wmi_remove(struct platform_device *device)
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

static struct platform_driver platform_driver = {
	.driver = {
		.name = EEEPC_WMI_FILE,
		.owner = THIS_MODULE,
	},
};

static int __init eeepc_wmi_init(void)
{
	int err;

	if (!wmi_has_guid(EEEPC_WMI_EVENT_GUID) ||
	    !wmi_has_guid(EEEPC_WMI_MGMT_GUID)) {
		pr_warning("No known WMI GUID found\n");
		return -ENODEV;
	}

	platform_device = eeepc_wmi_add();
	if (IS_ERR(platform_device)) {
		err = PTR_ERR(platform_device);
		goto fail_eeepc_wmi;
	}

	err = platform_driver_register(&platform_driver);
	if (err) {
		pr_warning("Unable to register platform driver\n");
		goto fail_platform_driver;
	}

	return 0;

fail_platform_driver:
	eeepc_wmi_remove(platform_device);
fail_eeepc_wmi:
	return err;
}

static void __exit eeepc_wmi_exit(void)
{
	eeepc_wmi_remove(platform_device);
	platform_driver_unregister(&platform_driver);
}

module_init(eeepc_wmi_init);
module_exit(eeepc_wmi_exit);
