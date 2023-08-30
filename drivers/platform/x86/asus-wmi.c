// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/backlight.h>
#include <linux/debugfs.h>
#include <linux/dmi.h>
#include <linux/fb.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/platform_data/x86/asus-wmi.h>
#include <linux/platform_device.h>
#include <linux/platform_profile.h>
#include <linux/power_supply.h>
#include <linux/rfkill.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/units.h>

#include <acpi/battery.h>
#include <acpi/video.h>

#include "asus-wmi.h"

MODULE_AUTHOR("Corentin Chary <corentin.chary@gmail.com>");
MODULE_AUTHOR("Yong Wang <yong.y.wang@intel.com>");
MODULE_DESCRIPTION("Asus Generic WMI Driver");
MODULE_LICENSE("GPL");

static bool fnlock_default = true;
module_param(fnlock_default, bool, 0444);

#define to_asus_wmi_driver(pdrv)					\
	(container_of((pdrv), struct asus_wmi_driver, platform_driver))

#define ASUS_WMI_MGMT_GUID	"97845ED0-4E6D-11DE-8A39-0800200C9A66"

#define NOTIFY_BRNUP_MIN		0x11
#define NOTIFY_BRNUP_MAX		0x1f
#define NOTIFY_BRNDOWN_MIN		0x20
#define NOTIFY_BRNDOWN_MAX		0x2e
#define NOTIFY_FNLOCK_TOGGLE		0x4e
#define NOTIFY_KBD_DOCK_CHANGE		0x75
#define NOTIFY_KBD_BRTUP		0xc4
#define NOTIFY_KBD_BRTDWN		0xc5
#define NOTIFY_KBD_BRTTOGGLE		0xc7
#define NOTIFY_KBD_FBM			0x99
#define NOTIFY_KBD_TTP			0xae
#define NOTIFY_LID_FLIP			0xfa
#define NOTIFY_LID_FLIP_ROG		0xbd

#define ASUS_WMI_FNLOCK_BIOS_DISABLED	BIT(0)

#define ASUS_MID_FAN_DESC		"mid_fan"
#define ASUS_GPU_FAN_DESC		"gpu_fan"
#define ASUS_FAN_DESC			"cpu_fan"
#define ASUS_FAN_MFUN			0x13
#define ASUS_FAN_SFUN_READ		0x06
#define ASUS_FAN_SFUN_WRITE		0x07

/* Based on standard hwmon pwmX_enable values */
#define ASUS_FAN_CTRL_FULLSPEED		0
#define ASUS_FAN_CTRL_MANUAL		1
#define ASUS_FAN_CTRL_AUTO		2

#define ASUS_FAN_BOOST_MODE_NORMAL		0
#define ASUS_FAN_BOOST_MODE_OVERBOOST		1
#define ASUS_FAN_BOOST_MODE_OVERBOOST_MASK	0x01
#define ASUS_FAN_BOOST_MODE_SILENT		2
#define ASUS_FAN_BOOST_MODE_SILENT_MASK		0x02
#define ASUS_FAN_BOOST_MODES_MASK		0x03

#define ASUS_THROTTLE_THERMAL_POLICY_DEFAULT	0
#define ASUS_THROTTLE_THERMAL_POLICY_OVERBOOST	1
#define ASUS_THROTTLE_THERMAL_POLICY_SILENT	2

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

#define FAN_CURVE_POINTS		8
#define FAN_CURVE_BUF_LEN		32
#define FAN_CURVE_DEV_CPU		0x00
#define FAN_CURVE_DEV_GPU		0x01
#define FAN_CURVE_DEV_MID		0x02
/* Mask to determine if setting temperature or percentage */
#define FAN_CURVE_PWM_MASK		0x04

/* Limits for tunables available on ASUS ROG laptops */
#define PPT_TOTAL_MIN		5
#define PPT_TOTAL_MAX		250
#define PPT_CPU_MIN			5
#define PPT_CPU_MAX			130
#define NVIDIA_BOOST_MIN	5
#define NVIDIA_BOOST_MAX	25
#define NVIDIA_TEMP_MIN		75
#define NVIDIA_TEMP_MAX		87

static const char * const ashs_ids[] = { "ATK4001", "ATK4002", NULL };

static int throttle_thermal_policy_write(struct asus_wmi *);

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
	u32 arg3;
	u32 arg4; /* Some ROG laptops require a full 5 input args */
	u32 arg5;
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
struct agfn_fan_args {
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

enum fan_type {
	FAN_TYPE_NONE = 0,
	FAN_TYPE_AGFN,		/* deprecated on newer platforms */
	FAN_TYPE_SPEC83,	/* starting in Spec 8.3, use CPU_FAN_CTRL */
};

struct fan_curve_data {
	bool enabled;
	u32 device_id;
	u8 temps[FAN_CURVE_POINTS];
	u8 percents[FAN_CURVE_POINTS];
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
	struct led_classdev micmute_led;
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

	int tablet_switch_event_code;
	u32 tablet_switch_dev_id;
	bool tablet_switch_inverted;

	enum fan_type fan_type;
	enum fan_type gpu_fan_type;
	enum fan_type mid_fan_type;
	int fan_pwm_mode;
	int gpu_fan_pwm_mode;
	int mid_fan_pwm_mode;
	int agfn_pwm;

	bool fan_boost_mode_available;
	u8 fan_boost_mode_mask;
	u8 fan_boost_mode;

	bool charge_mode_available;
	bool egpu_enable_available;
	bool egpu_connect_available;
	bool dgpu_disable_available;
	bool gpu_mux_mode_available;

	/* Tunables provided by ASUS for gaming laptops */
	bool ppt_pl2_sppt_available;
	bool ppt_pl1_spl_available;
	bool ppt_apu_sppt_available;
	bool ppt_plat_sppt_available;
	bool ppt_fppt_available;
	bool nv_dyn_boost_available;
	bool nv_temp_tgt_available;

	bool kbd_rgb_mode_available;
	bool kbd_rgb_state_available;

	bool throttle_thermal_policy_available;
	u8 throttle_thermal_policy_mode;

	bool cpu_fan_curve_available;
	bool gpu_fan_curve_available;
	bool mid_fan_curve_available;
	struct fan_curve_data custom_fan_curves[3];

	struct platform_profile_handler platform_profile_handler;
	bool platform_profile_support;

	// The RSOC controls the maximum charging percentage.
	bool battery_rsoc_available;

	bool panel_overdrive_available;
	bool mini_led_mode_available;

	struct hotplug_slot hotplug_slot;
	struct mutex hotplug_lock;
	struct mutex wmi_lock;
	struct workqueue_struct *hotplug_workqueue;
	struct work_struct hotplug_work;

	bool fnlock_locked;

	struct asus_wmi_debug debug;

	struct asus_wmi_driver *driver;
};

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

static int asus_wmi_evaluate_method5(u32 method_id,
		u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4, u32 *retval)
{
	struct bios_args args = {
		.arg0 = arg0,
		.arg1 = arg1,
		.arg2 = arg2,
		.arg3 = arg3,
		.arg4 = arg4,
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

/*
 * Returns as an error if the method output is not a buffer. Typically this
 * means that the method called is unsupported.
 */
static int asus_wmi_evaluate_method_buf(u32 method_id,
		u32 arg0, u32 arg1, u8 *ret_buffer, size_t size)
{
	struct bios_args args = {
		.arg0 = arg0,
		.arg1 = arg1,
		.arg2 = 0,
	};
	struct acpi_buffer input = { (acpi_size) sizeof(args), &args };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;
	union acpi_object *obj;
	int err = 0;

	status = wmi_evaluate_method(ASUS_WMI_MGMT_GUID, 0, method_id,
				     &input, &output);

	if (ACPI_FAILURE(status))
		return -EIO;

	obj = (union acpi_object *)output.pointer;

	switch (obj->type) {
	case ACPI_TYPE_BUFFER:
		if (obj->buffer.length > size) {
			err = -ENOSPC;
			break;
		}
		if (obj->buffer.length == 0) {
			err = -ENODATA;
			break;
		}

		memcpy(ret_buffer, obj->buffer.pointer, obj->buffer.length);
		break;
	case ACPI_TYPE_INTEGER:
		err = (u32)obj->integer.value;

		if (err == ASUS_WMI_UNSUPPORTED_METHOD)
			err = -ENODEV;
		/*
		 * At least one method returns a 0 with no buffer if no arg
		 * is provided, such as ASUS_WMI_DEVID_CPU_FAN_CURVE
		 */
		if (err == 0)
			err = -ENODATA;
		break;
	default:
		err = -ENODATA;
		break;
	}

	kfree(obj);

	if (err)
		return err;

	return 0;
}

static int asus_wmi_evaluate_method_agfn(const struct acpi_buffer args)
{
	struct acpi_buffer input;
	u64 phys_addr;
	u32 retval;
	u32 status;

	/*
	 * Copy to dma capable address otherwise memory corruption occurs as
	 * bios has to be able to access it.
	 */
	input.pointer = kmemdup(args.pointer, args.length, GFP_DMA | GFP_KERNEL);
	input.length = args.length;
	if (!input.pointer)
		return -ENOMEM;
	phys_addr = virt_to_phys(input.pointer);

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

static bool asus_wmi_dev_is_present(struct asus_wmi *asus, u32 dev_id)
{
	u32 retval;
	int status = asus_wmi_get_devstate(asus, dev_id, &retval);

	return status == 0 && (retval & ASUS_WMI_DSTS_PRESENCE_BIT);
}

/* Input **********************************************************************/
static void asus_wmi_tablet_sw_report(struct asus_wmi *asus, bool value)
{
	input_report_switch(asus->inputdev, SW_TABLET_MODE,
			    asus->tablet_switch_inverted ? !value : value);
	input_sync(asus->inputdev);
}

static void asus_wmi_tablet_sw_init(struct asus_wmi *asus, u32 dev_id, int event_code)
{
	struct device *dev = &asus->platform_device->dev;
	int result;

	result = asus_wmi_get_devstate_simple(asus, dev_id);
	if (result >= 0) {
		input_set_capability(asus->inputdev, EV_SW, SW_TABLET_MODE);
		asus_wmi_tablet_sw_report(asus, result);
		asus->tablet_switch_dev_id = dev_id;
		asus->tablet_switch_event_code = event_code;
	} else if (result == -ENODEV) {
		dev_err(dev, "This device has tablet-mode-switch quirk but got ENODEV checking it. This is a bug.");
	} else {
		dev_err(dev, "Error checking for tablet-mode-switch: %d\n", result);
	}
}

static int asus_wmi_input_init(struct asus_wmi *asus)
{
	struct device *dev = &asus->platform_device->dev;
	int err;

	asus->inputdev = input_allocate_device();
	if (!asus->inputdev)
		return -ENOMEM;

	asus->inputdev->name = asus->driver->input_name;
	asus->inputdev->phys = asus->driver->input_phys;
	asus->inputdev->id.bustype = BUS_HOST;
	asus->inputdev->dev.parent = dev;
	set_bit(EV_REP, asus->inputdev->evbit);

	err = sparse_keymap_setup(asus->inputdev, asus->driver->keymap, NULL);
	if (err)
		goto err_free_dev;

	switch (asus->driver->quirks->tablet_switch_mode) {
	case asus_wmi_no_tablet_switch:
		break;
	case asus_wmi_kbd_dock_devid:
		asus->tablet_switch_inverted = true;
		asus_wmi_tablet_sw_init(asus, ASUS_WMI_DEVID_KBD_DOCK, NOTIFY_KBD_DOCK_CHANGE);
		break;
	case asus_wmi_lid_flip_devid:
		asus_wmi_tablet_sw_init(asus, ASUS_WMI_DEVID_LID_FLIP, NOTIFY_LID_FLIP);
		break;
	case asus_wmi_lid_flip_rog_devid:
		asus_wmi_tablet_sw_init(asus, ASUS_WMI_DEVID_LID_FLIP_ROG, NOTIFY_LID_FLIP_ROG);
		break;
	}

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

/* Tablet mode ****************************************************************/

static void asus_wmi_tablet_mode_get_state(struct asus_wmi *asus)
{
	int result;

	if (!asus->tablet_switch_dev_id)
		return;

	result = asus_wmi_get_devstate_simple(asus, asus->tablet_switch_dev_id);
	if (result >= 0)
		asus_wmi_tablet_sw_report(asus, result);
}

/* Charging mode, 1=Barrel, 2=USB ******************************************/
static ssize_t charge_mode_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int result, value;

	result = asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_CHARGE_MODE, &value);
	if (result < 0)
		return result;

	return sysfs_emit(buf, "%d\n", value & 0xff);
}

static DEVICE_ATTR_RO(charge_mode);

/* dGPU ********************************************************************/
static ssize_t dgpu_disable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int result;

	result = asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_DGPU);
	if (result < 0)
		return result;

	return sysfs_emit(buf, "%d\n", result);
}

/*
 * A user may be required to store the value twice, typcial store first, then
 * rescan PCI bus to activate power, then store a second time to save correctly.
 * The reason for this is that an extra code path in the ACPI is enabled when
 * the device and bus are powered.
 */
static ssize_t dgpu_disable_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int result, err;
	u32 disable;

	struct asus_wmi *asus = dev_get_drvdata(dev);

	result = kstrtou32(buf, 10, &disable);
	if (result)
		return result;

	if (disable > 1)
		return -EINVAL;

	if (asus->gpu_mux_mode_available) {
		result = asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_GPU_MUX);
		if (result < 0)
			/* An error here may signal greater failure of GPU handling */
			return result;
		if (!result && disable) {
			err = -ENODEV;
			pr_warn("Can not disable dGPU when the MUX is in dGPU mode: %d\n", err);
			return err;
		}
	}

	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_DGPU, disable, &result);
	if (err) {
		pr_warn("Failed to set dgpu disable: %d\n", err);
		return err;
	}

	if (result > 1) {
		pr_warn("Failed to set dgpu disable (result): 0x%x\n", result);
		return -EIO;
	}

	sysfs_notify(&asus->platform_device->dev.kobj, NULL, "dgpu_disable");

	return count;
}
static DEVICE_ATTR_RW(dgpu_disable);

/* eGPU ********************************************************************/
static ssize_t egpu_enable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int result;

	result = asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_EGPU);
	if (result < 0)
		return result;

	return sysfs_emit(buf, "%d\n", result);
}

/* The ACPI call to enable the eGPU also disables the internal dGPU */
static ssize_t egpu_enable_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int result, err;
	u32 enable;

	struct asus_wmi *asus = dev_get_drvdata(dev);

	err = kstrtou32(buf, 10, &enable);
	if (err)
		return err;

	if (enable > 1)
		return -EINVAL;

	err = asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_EGPU_CONNECTED);
	if (err < 0) {
		pr_warn("Failed to get egpu connection status: %d\n", err);
		return err;
	}

	if (asus->gpu_mux_mode_available) {
		result = asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_GPU_MUX);
		if (result < 0) {
			/* An error here may signal greater failure of GPU handling */
			pr_warn("Failed to get gpu mux status: %d\n", result);
			return result;
		}
		if (!result && enable) {
			err = -ENODEV;
			pr_warn("Can not enable eGPU when the MUX is in dGPU mode: %d\n", err);
			return err;
		}
	}

	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_EGPU, enable, &result);
	if (err) {
		pr_warn("Failed to set egpu state: %d\n", err);
		return err;
	}

	if (result > 1) {
		pr_warn("Failed to set egpu state (retval): 0x%x\n", result);
		return -EIO;
	}

	sysfs_notify(&asus->platform_device->dev.kobj, NULL, "egpu_enable");

	return count;
}
static DEVICE_ATTR_RW(egpu_enable);

/* Is eGPU connected? *********************************************************/
static ssize_t egpu_connected_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int result;

	result = asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_EGPU_CONNECTED);
	if (result < 0)
		return result;

	return sysfs_emit(buf, "%d\n", result);
}

static DEVICE_ATTR_RO(egpu_connected);

/* gpu mux switch *************************************************************/
static ssize_t gpu_mux_mode_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int result;

	result = asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_GPU_MUX);
	if (result < 0)
		return result;

	return sysfs_emit(buf, "%d\n", result);
}

static ssize_t gpu_mux_mode_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int result, err;
	u32 optimus;

	err = kstrtou32(buf, 10, &optimus);
	if (err)
		return err;

	if (optimus > 1)
		return -EINVAL;

	if (asus->dgpu_disable_available) {
		result = asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_DGPU);
		if (result < 0)
			/* An error here may signal greater failure of GPU handling */
			return result;
		if (result && !optimus) {
			err = -ENODEV;
			pr_warn("Can not switch MUX to dGPU mode when dGPU is disabled: %d\n", err);
			return err;
		}
	}

	if (asus->egpu_enable_available) {
		result = asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_EGPU);
		if (result < 0)
			/* An error here may signal greater failure of GPU handling */
			return result;
		if (result && !optimus) {
			err = -ENODEV;
			pr_warn("Can not switch MUX to dGPU mode when eGPU is enabled: %d\n", err);
			return err;
		}
	}

	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_GPU_MUX, optimus, &result);
	if (err) {
		dev_err(dev, "Failed to set GPU MUX mode: %d\n", err);
		return err;
	}
	/* !1 is considered a fail by ASUS */
	if (result != 1) {
		dev_warn(dev, "Failed to set GPU MUX mode (result): 0x%x\n", result);
		return -EIO;
	}

	sysfs_notify(&asus->platform_device->dev.kobj, NULL, "gpu_mux_mode");

	return count;
}
static DEVICE_ATTR_RW(gpu_mux_mode);

/* TUF Laptop Keyboard RGB Modes **********************************************/
static ssize_t kbd_rgb_mode_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	u32 cmd, mode, r, g, b, speed;
	int err;

	if (sscanf(buf, "%d %d %d %d %d %d", &cmd, &mode, &r, &g, &b, &speed) != 6)
		return -EINVAL;

	/* B3 is set and B4 is save to BIOS */
	switch (cmd) {
	case 0:
		cmd = 0xb3;
		break;
	case 1:
		cmd = 0xb4;
		break;
	default:
		return -EINVAL;
	}

	/* These are the known usable modes across all TUF/ROG */
	if (mode >= 12 || mode == 9)
		mode = 10;

	switch (speed) {
	case 0:
		speed = 0xe1;
		break;
	case 1:
		speed = 0xeb;
		break;
	case 2:
		speed = 0xf5;
		break;
	default:
		speed = 0xeb;
	}

	err = asus_wmi_evaluate_method3(ASUS_WMI_METHODID_DEVS, ASUS_WMI_DEVID_TUF_RGB_MODE,
			cmd | (mode << 8) | (r << 16) | (g << 24), b | (speed << 8), NULL);
	if (err)
		return err;

	return count;
}
static DEVICE_ATTR_WO(kbd_rgb_mode);

static ssize_t kbd_rgb_mode_index_show(struct device *device,
						 struct device_attribute *attr,
						 char *buf)
{
	return sysfs_emit(buf, "%s\n", "cmd mode red green blue speed");
}
static DEVICE_ATTR_RO(kbd_rgb_mode_index);

static struct attribute *kbd_rgb_mode_attrs[] = {
	&dev_attr_kbd_rgb_mode.attr,
	&dev_attr_kbd_rgb_mode_index.attr,
	NULL,
};

static const struct attribute_group kbd_rgb_mode_group = {
	.attrs = kbd_rgb_mode_attrs,
};

/* TUF Laptop Keyboard RGB State **********************************************/
static ssize_t kbd_rgb_state_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	u32 flags, cmd, boot, awake, sleep, keyboard;
	int err;

	if (sscanf(buf, "%d %d %d %d %d", &cmd, &boot, &awake, &sleep, &keyboard) != 5)
		return -EINVAL;

	if (cmd)
		cmd = BIT(2);

	flags = 0;
	if (boot)
		flags |= BIT(1);
	if (awake)
		flags |= BIT(3);
	if (sleep)
		flags |= BIT(5);
	if (keyboard)
		flags |= BIT(7);

	/* 0xbd is the required default arg0 for the method. Nothing happens otherwise */
	err = asus_wmi_evaluate_method3(ASUS_WMI_METHODID_DEVS,
			ASUS_WMI_DEVID_TUF_RGB_STATE, 0xbd | cmd << 8 | (flags << 16), 0, NULL);
	if (err)
		return err;

	return count;
}
static DEVICE_ATTR_WO(kbd_rgb_state);

static ssize_t kbd_rgb_state_index_show(struct device *device,
						 struct device_attribute *attr,
						 char *buf)
{
	return sysfs_emit(buf, "%s\n", "cmd boot awake sleep keyboard");
}
static DEVICE_ATTR_RO(kbd_rgb_state_index);

static struct attribute *kbd_rgb_state_attrs[] = {
	&dev_attr_kbd_rgb_state.attr,
	&dev_attr_kbd_rgb_state_index.attr,
	NULL,
};

static const struct attribute_group kbd_rgb_state_group = {
	.attrs = kbd_rgb_state_attrs,
};

static const struct attribute_group *kbd_rgb_mode_groups[] = {
	NULL,
	NULL,
	NULL,
};

/* Tunable: PPT: Intel=PL1, AMD=SPPT *****************************************/
static ssize_t ppt_pl2_sppt_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int result, err;
	u32 value;

	struct asus_wmi *asus = dev_get_drvdata(dev);

	result = kstrtou32(buf, 10, &value);
	if (result)
		return result;

	if (value < PPT_TOTAL_MIN || value > PPT_TOTAL_MAX)
		return -EINVAL;

	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_PPT_PL2_SPPT, value, &result);
	if (err) {
		pr_warn("Failed to set ppt_pl2_sppt: %d\n", err);
		return err;
	}

	if (result > 1) {
		pr_warn("Failed to set ppt_pl2_sppt (result): 0x%x\n", result);
		return -EIO;
	}

	sysfs_notify(&asus->platform_device->dev.kobj, NULL, "ppt_pl2_sppt");

	return count;
}
static DEVICE_ATTR_WO(ppt_pl2_sppt);

/* Tunable: PPT, Intel=PL1, AMD=SPL ******************************************/
static ssize_t ppt_pl1_spl_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int result, err;
	u32 value;

	struct asus_wmi *asus = dev_get_drvdata(dev);

	result = kstrtou32(buf, 10, &value);
	if (result)
		return result;

	if (value < PPT_TOTAL_MIN || value > PPT_TOTAL_MAX)
		return -EINVAL;

	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_PPT_PL1_SPL, value, &result);
	if (err) {
		pr_warn("Failed to set ppt_pl1_spl: %d\n", err);
		return err;
	}

	if (result > 1) {
		pr_warn("Failed to set ppt_pl1_spl (result): 0x%x\n", result);
		return -EIO;
	}

	sysfs_notify(&asus->platform_device->dev.kobj, NULL, "ppt_pl1_spl");

	return count;
}
static DEVICE_ATTR_WO(ppt_pl1_spl);

/* Tunable: PPT APU FPPT ******************************************************/
static ssize_t ppt_fppt_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int result, err;
	u32 value;

	struct asus_wmi *asus = dev_get_drvdata(dev);

	result = kstrtou32(buf, 10, &value);
	if (result)
		return result;

	if (value < PPT_TOTAL_MIN || value > PPT_TOTAL_MAX)
		return -EINVAL;

	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_PPT_FPPT, value, &result);
	if (err) {
		pr_warn("Failed to set ppt_fppt: %d\n", err);
		return err;
	}

	if (result > 1) {
		pr_warn("Failed to set ppt_fppt (result): 0x%x\n", result);
		return -EIO;
	}

	sysfs_notify(&asus->platform_device->dev.kobj, NULL, "ppt_fpu_sppt");

	return count;
}
static DEVICE_ATTR_WO(ppt_fppt);

/* Tunable: PPT APU SPPT *****************************************************/
static ssize_t ppt_apu_sppt_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int result, err;
	u32 value;

	struct asus_wmi *asus = dev_get_drvdata(dev);

	result = kstrtou32(buf, 10, &value);
	if (result)
		return result;

	if (value < PPT_CPU_MIN || value > PPT_CPU_MAX)
		return -EINVAL;

	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_PPT_APU_SPPT, value, &result);
	if (err) {
		pr_warn("Failed to set ppt_apu_sppt: %d\n", err);
		return err;
	}

	if (result > 1) {
		pr_warn("Failed to set ppt_apu_sppt (result): 0x%x\n", result);
		return -EIO;
	}

	sysfs_notify(&asus->platform_device->dev.kobj, NULL, "ppt_apu_sppt");

	return count;
}
static DEVICE_ATTR_WO(ppt_apu_sppt);

/* Tunable: PPT platform SPPT ************************************************/
static ssize_t ppt_platform_sppt_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int result, err;
	u32 value;

	struct asus_wmi *asus = dev_get_drvdata(dev);

	result = kstrtou32(buf, 10, &value);
	if (result)
		return result;

	if (value < PPT_CPU_MIN || value > PPT_CPU_MAX)
		return -EINVAL;

	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_PPT_PLAT_SPPT, value, &result);
	if (err) {
		pr_warn("Failed to set ppt_platform_sppt: %d\n", err);
		return err;
	}

	if (result > 1) {
		pr_warn("Failed to set ppt_platform_sppt (result): 0x%x\n", result);
		return -EIO;
	}

	sysfs_notify(&asus->platform_device->dev.kobj, NULL, "ppt_platform_sppt");

	return count;
}
static DEVICE_ATTR_WO(ppt_platform_sppt);

/* Tunable: NVIDIA dynamic boost *********************************************/
static ssize_t nv_dynamic_boost_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int result, err;
	u32 value;

	struct asus_wmi *asus = dev_get_drvdata(dev);

	result = kstrtou32(buf, 10, &value);
	if (result)
		return result;

	if (value < NVIDIA_BOOST_MIN || value > NVIDIA_BOOST_MAX)
		return -EINVAL;

	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_NV_DYN_BOOST, value, &result);
	if (err) {
		pr_warn("Failed to set nv_dynamic_boost: %d\n", err);
		return err;
	}

	if (result > 1) {
		pr_warn("Failed to set nv_dynamic_boost (result): 0x%x\n", result);
		return -EIO;
	}

	sysfs_notify(&asus->platform_device->dev.kobj, NULL, "nv_dynamic_boost");

	return count;
}
static DEVICE_ATTR_WO(nv_dynamic_boost);

/* Tunable: NVIDIA temperature target ****************************************/
static ssize_t nv_temp_target_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int result, err;
	u32 value;

	struct asus_wmi *asus = dev_get_drvdata(dev);

	result = kstrtou32(buf, 10, &value);
	if (result)
		return result;

	if (value < NVIDIA_TEMP_MIN || value > NVIDIA_TEMP_MAX)
		return -EINVAL;

	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_NV_THERM_TARGET, value, &result);
	if (err) {
		pr_warn("Failed to set nv_temp_target: %d\n", err);
		return err;
	}

	if (result > 1) {
		pr_warn("Failed to set nv_temp_target (result): 0x%x\n", result);
		return -EIO;
	}

	sysfs_notify(&asus->platform_device->dev.kobj, NULL, "nv_temp_target");

	return count;
}
static DEVICE_ATTR_WO(nv_temp_target);

/* Battery ********************************************************************/

/* The battery maximum charging percentage */
static int charge_end_threshold;

static ssize_t charge_control_end_threshold_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t count)
{
	int value, ret, rv;

	ret = kstrtouint(buf, 10, &value);
	if (ret)
		return ret;

	if (value < 0 || value > 100)
		return -EINVAL;

	ret = asus_wmi_set_devstate(ASUS_WMI_DEVID_RSOC, value, &rv);
	if (ret)
		return ret;

	if (rv != 1)
		return -EIO;

	/* There isn't any method in the DSDT to read the threshold, so we
	 * save the threshold.
	 */
	charge_end_threshold = value;
	return count;
}

static ssize_t charge_control_end_threshold_show(struct device *device,
						 struct device_attribute *attr,
						 char *buf)
{
	return sysfs_emit(buf, "%d\n", charge_end_threshold);
}

static DEVICE_ATTR_RW(charge_control_end_threshold);

static int asus_wmi_battery_add(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	/* The WMI method does not provide a way to specific a battery, so we
	 * just assume it is the first battery.
	 * Note: On some newer ASUS laptops (Zenbook UM431DA), the primary/first
	 * battery is named BATT.
	 */
	if (strcmp(battery->desc->name, "BAT0") != 0 &&
	    strcmp(battery->desc->name, "BAT1") != 0 &&
	    strcmp(battery->desc->name, "BATC") != 0 &&
	    strcmp(battery->desc->name, "BATT") != 0)
		return -ENODEV;

	if (device_create_file(&battery->dev,
	    &dev_attr_charge_control_end_threshold))
		return -ENODEV;

	/* The charge threshold is only reset when the system is power cycled,
	 * and we can't get the current threshold so let set it to 100% when
	 * a battery is added.
	 */
	asus_wmi_set_devstate(ASUS_WMI_DEVID_RSOC, 100, NULL);
	charge_end_threshold = 100;

	return 0;
}

static int asus_wmi_battery_remove(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	device_remove_file(&battery->dev,
			   &dev_attr_charge_control_end_threshold);
	return 0;
}

static struct acpi_battery_hook battery_hook = {
	.add_battery = asus_wmi_battery_add,
	.remove_battery = asus_wmi_battery_remove,
	.name = "ASUS Battery Extension",
};

static void asus_wmi_battery_init(struct asus_wmi *asus)
{
	asus->battery_rsoc_available = false;
	if (asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_RSOC)) {
		asus->battery_rsoc_available = true;
		battery_hook_register(&battery_hook);
	}
}

static void asus_wmi_battery_exit(struct asus_wmi *asus)
{
	if (asus->battery_rsoc_available)
		battery_hook_unregister(&battery_hook);
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

	if (retval < 0)
		return retval;

	if (level)
		*level = retval & 0x7F;
	if (env)
		*env = (retval >> 8) & 0x7F;
	return 0;
}

static void do_kbd_led_set(struct led_classdev *led_cdev, int value)
{
	struct asus_wmi *asus;
	int max_level;

	asus = container_of(led_cdev, struct asus_wmi, kbd_led);
	max_level = asus->kbd_led.max_brightness;

	asus->kbd_led_wk = clamp_val(value, 0, max_level);
	kbd_led_update(asus);
}

static void kbd_led_set(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	/* Prevent disabling keyboard backlight on module unregister */
	if (led_cdev->flags & LED_UNREGISTERING)
		return;

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

static int micmute_led_set(struct led_classdev *led_cdev,
			   enum led_brightness brightness)
{
	int state = brightness != LED_OFF;
	int err;

	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_MICMUTE_LED, state, NULL);
	return err < 0 ? err : 0;
}

static void asus_wmi_led_exit(struct asus_wmi *asus)
{
	led_classdev_unregister(&asus->kbd_led);
	led_classdev_unregister(&asus->tpd_led);
	led_classdev_unregister(&asus->wlan_led);
	led_classdev_unregister(&asus->lightbar_led);
	led_classdev_unregister(&asus->micmute_led);

	if (asus->led_workqueue)
		destroy_workqueue(asus->led_workqueue);
}

static int asus_wmi_led_init(struct asus_wmi *asus)
{
	int rv = 0, num_rgb_groups = 0, led_val;

	if (asus->kbd_rgb_mode_available)
		kbd_rgb_mode_groups[num_rgb_groups++] = &kbd_rgb_mode_group;
	if (asus->kbd_rgb_state_available)
		kbd_rgb_mode_groups[num_rgb_groups++] = &kbd_rgb_state_group;

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

		if (num_rgb_groups != 0)
			asus->kbd_led.groups = kbd_rgb_mode_groups;

		rv = led_classdev_register(&asus->platform_device->dev,
					   &asus->kbd_led);
		if (rv)
			goto error;
	}

	if (asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_WIRELESS_LED)
			&& (asus->driver->quirks->wapf > 0)) {
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

	if (asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_LIGHTBAR)) {
		INIT_WORK(&asus->lightbar_led_work, lightbar_led_update);

		asus->lightbar_led.name = "asus::lightbar";
		asus->lightbar_led.brightness_set = lightbar_led_set;
		asus->lightbar_led.brightness_get = lightbar_led_get;
		asus->lightbar_led.max_brightness = 1;

		rv = led_classdev_register(&asus->platform_device->dev,
					   &asus->lightbar_led);
	}

	if (asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_MICMUTE_LED)) {
		asus->micmute_led.name = "platform::micmute";
		asus->micmute_led.max_brightness = 1;
		asus->micmute_led.brightness = ledtrig_audio_get(LED_AUDIO_MICMUTE);
		asus->micmute_led.brightness_set_blocking = micmute_led_set;
		asus->micmute_led.default_trigger = "audio-micmute";

		rv = led_classdev_register(&asus->platform_device->dev,
						&asus->micmute_led);
		if (rv)
			goto error;
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
			pr_warn("BIOS says wireless lan is %s, but the pci device is %s\n",
				blocked ? "blocked" : "unblocked",
				absent ? "absent" : "present");
			pr_warn("skipped wireless hotplug as probably inappropriate for this model\n");
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
	if (ACPI_FAILURE(status))
		return -ENODEV;

	status = acpi_install_notify_handler(handle, ACPI_SYSTEM_NOTIFY,
					     asus_rfkill_notify, asus);
	if (ACPI_FAILURE(status))
		pr_warn("Failed to register notify on %s\n", node);

	return 0;
}

static void asus_unregister_rfkill_notifier(struct asus_wmi *asus, char *node)
{
	acpi_status status = AE_OK;
	acpi_handle handle;

	status = acpi_get_handle(NULL, node, &handle);
	if (ACPI_FAILURE(status))
		return;

	status = acpi_remove_notify_handler(handle, ACPI_SYSTEM_NOTIFY,
					    asus_rfkill_notify);
	if (ACPI_FAILURE(status))
		pr_err("Error removing rfkill notify handler %s\n", node);
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

/* Panel Overdrive ************************************************************/
static ssize_t panel_od_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int result;

	result = asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_PANEL_OD);
	if (result < 0)
		return result;

	return sysfs_emit(buf, "%d\n", result);
}

static ssize_t panel_od_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int result, err;
	u32 overdrive;

	struct asus_wmi *asus = dev_get_drvdata(dev);

	result = kstrtou32(buf, 10, &overdrive);
	if (result)
		return result;

	if (overdrive > 1)
		return -EINVAL;

	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_PANEL_OD, overdrive, &result);

	if (err) {
		pr_warn("Failed to set panel overdrive: %d\n", err);
		return err;
	}

	if (result > 1) {
		pr_warn("Failed to set panel overdrive (result): 0x%x\n", result);
		return -EIO;
	}

	sysfs_notify(&asus->platform_device->dev.kobj, NULL, "panel_od");

	return count;
}
static DEVICE_ATTR_RW(panel_od);

/* Mini-LED mode **************************************************************/
static ssize_t mini_led_mode_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int result;

	result = asus_wmi_get_devstate_simple(asus, ASUS_WMI_DEVID_MINI_LED_MODE);
	if (result < 0)
		return result;

	return sysfs_emit(buf, "%d\n", result);
}

static ssize_t mini_led_mode_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int result, err;
	u32 mode;

	struct asus_wmi *asus = dev_get_drvdata(dev);

	result = kstrtou32(buf, 10, &mode);
	if (result)
		return result;

	if (mode > 1)
		return -EINVAL;

	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_MINI_LED_MODE, mode, &result);

	if (err) {
		pr_warn("Failed to set mini-LED: %d\n", err);
		return err;
	}

	if (result > 1) {
		pr_warn("Failed to set mini-LED mode (result): 0x%x\n", result);
		return -EIO;
	}

	sysfs_notify(&asus->platform_device->dev.kobj, NULL, "mini_led_mode");

	return count;
}
static DEVICE_ATTR_RW(mini_led_mode);

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

	pci_dev_put(xhci_pdev);

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

static int asus_agfn_fan_speed_read(struct asus_wmi *asus, int fan,
					  int *speed)
{
	struct agfn_fan_args args = {
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

static int asus_agfn_fan_speed_write(struct asus_wmi *asus, int fan,
				     int *speed)
{
	struct agfn_fan_args args = {
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
		asus->agfn_pwm = *speed;

	return 0;
}

/*
 * Check if we can read the speed of one fan. If true we assume we can also
 * control it.
 */
static bool asus_wmi_has_agfn_fan(struct asus_wmi *asus)
{
	int status;
	int speed;
	u32 value;

	status = asus_agfn_fan_speed_read(asus, 1, &speed);
	if (status != 0)
		return false;

	status = asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_FAN_CTRL, &value);
	if (status != 0)
		return false;

	/*
	 * We need to find a better way, probably using sfun,
	 * bits or spec ...
	 * Currently we disable it if:
	 * - ASUS_WMI_UNSUPPORTED_METHOD is returned
	 * - reverved bits are non-zero
	 * - sfun and presence bit are not set
	 */
	return !(value == ASUS_WMI_UNSUPPORTED_METHOD || value & 0xFFF80000
		 || (!asus->sfun && !(value & ASUS_WMI_DSTS_PRESENCE_BIT)));
}

static int asus_fan_set_auto(struct asus_wmi *asus)
{
	int status;
	u32 retval;

	switch (asus->fan_type) {
	case FAN_TYPE_SPEC83:
		status = asus_wmi_set_devstate(ASUS_WMI_DEVID_CPU_FAN_CTRL,
					       0, &retval);
		if (status)
			return status;

		if (retval != 1)
			return -EIO;
		break;

	case FAN_TYPE_AGFN:
		status = asus_agfn_fan_speed_write(asus, 0, NULL);
		if (status)
			return -ENXIO;
		break;

	default:
		return -ENXIO;
	}

	/*
	 * Modern models like the G713 also have GPU fan control (this is not AGFN)
	 */
	if (asus->gpu_fan_type == FAN_TYPE_SPEC83) {
		status = asus_wmi_set_devstate(ASUS_WMI_DEVID_GPU_FAN_CTRL,
					       0, &retval);
		if (status)
			return status;

		if (retval != 1)
			return -EIO;
	}

	return 0;
}

static ssize_t pwm1_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int err;
	int value;

	/* If we already set a value then just return it */
	if (asus->agfn_pwm >= 0)
		return sprintf(buf, "%d\n", asus->agfn_pwm);

	/*
	 * If we haven't set already set a value through the AGFN interface,
	 * we read a current value through the (now-deprecated) FAN_CTRL device.
	 */
	err = asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_FAN_CTRL, &value);
	if (err < 0)
		return err;

	value &= 0xFF;

	if (value == 1) /* Low Speed */
		value = 85;
	else if (value == 2)
		value = 170;
	else if (value == 3)
		value = 255;
	else if (value) {
		pr_err("Unknown fan speed %#x\n", value);
		value = -1;
	}

	return sysfs_emit(buf, "%d\n", value);
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

	state = asus_agfn_fan_speed_write(asus, 1, &value);
	if (state)
		pr_warn("Setting fan speed failed: %d\n", state);
	else
		asus->fan_pwm_mode = ASUS_FAN_CTRL_MANUAL;

	return count;
}

static ssize_t fan1_input_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int value;
	int ret;

	switch (asus->fan_type) {
	case FAN_TYPE_SPEC83:
		ret = asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_CPU_FAN_CTRL,
					    &value);
		if (ret < 0)
			return ret;

		value &= 0xffff;
		break;

	case FAN_TYPE_AGFN:
		/* no speed readable on manual mode */
		if (asus->fan_pwm_mode == ASUS_FAN_CTRL_MANUAL)
			return -ENXIO;

		ret = asus_agfn_fan_speed_read(asus, 1, &value);
		if (ret) {
			pr_warn("reading fan speed failed: %d\n", ret);
			return -ENXIO;
		}
		break;

	default:
		return -ENXIO;
	}

	return sysfs_emit(buf, "%d\n", value < 0 ? -1 : value * 100);
}

static ssize_t pwm1_enable_show(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);

	/*
	 * Just read back the cached pwm mode.
	 *
	 * For the CPU_FAN device, the spec indicates that we should be
	 * able to read the device status and consult bit 19 to see if we
	 * are in Full On or Automatic mode. However, this does not work
	 * in practice on X532FL at least (the bit is always 0) and there's
	 * also nothing in the DSDT to indicate that this behaviour exists.
	 */
	return sysfs_emit(buf, "%d\n", asus->fan_pwm_mode);
}

static ssize_t pwm1_enable_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t count)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int status = 0;
	int state;
	int value;
	int ret;
	u32 retval;

	ret = kstrtouint(buf, 10, &state);
	if (ret)
		return ret;

	if (asus->fan_type == FAN_TYPE_SPEC83) {
		switch (state) { /* standard documented hwmon values */
		case ASUS_FAN_CTRL_FULLSPEED:
			value = 1;
			break;
		case ASUS_FAN_CTRL_AUTO:
			value = 0;
			break;
		default:
			return -EINVAL;
		}

		ret = asus_wmi_set_devstate(ASUS_WMI_DEVID_CPU_FAN_CTRL,
					    value, &retval);
		if (ret)
			return ret;

		if (retval != 1)
			return -EIO;
	} else if (asus->fan_type == FAN_TYPE_AGFN) {
		switch (state) {
		case ASUS_FAN_CTRL_MANUAL:
			break;

		case ASUS_FAN_CTRL_AUTO:
			status = asus_fan_set_auto(asus);
			if (status)
				return status;
			break;

		default:
			return -EINVAL;
		}
	}

	asus->fan_pwm_mode = state;

	/* Must set to disabled if mode is toggled */
	if (asus->cpu_fan_curve_available)
		asus->custom_fan_curves[FAN_CURVE_DEV_CPU].enabled = false;
	if (asus->gpu_fan_curve_available)
		asus->custom_fan_curves[FAN_CURVE_DEV_GPU].enabled = false;
	if (asus->mid_fan_curve_available)
		asus->custom_fan_curves[FAN_CURVE_DEV_MID].enabled = false;

	return count;
}

static ssize_t fan1_label_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	return sysfs_emit(buf, "%s\n", ASUS_FAN_DESC);
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

	return sprintf(buf, "%ld\n",
		       deci_kelvin_to_millicelsius(value & 0xFFFF));
}

/* GPU fan on modern ROG laptops */
static ssize_t fan2_input_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int value;
	int ret;

	ret = asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_GPU_FAN_CTRL, &value);
	if (ret < 0)
		return ret;

	value &= 0xffff;

	return sysfs_emit(buf, "%d\n", value * 100);
}

static ssize_t fan2_label_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	return sysfs_emit(buf, "%s\n", ASUS_GPU_FAN_DESC);
}

/* Middle/Center fan on modern ROG laptops */
static ssize_t fan3_input_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int value;
	int ret;

	ret = asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_MID_FAN_CTRL, &value);
	if (ret < 0)
		return ret;

	value &= 0xffff;

	return sysfs_emit(buf, "%d\n", value * 100);
}

static ssize_t fan3_label_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	return sysfs_emit(buf, "%s\n", ASUS_MID_FAN_DESC);
}

static ssize_t pwm2_enable_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", asus->gpu_fan_pwm_mode);
}

static ssize_t pwm2_enable_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int state;
	int value;
	int ret;
	u32 retval;

	ret = kstrtouint(buf, 10, &state);
	if (ret)
		return ret;

	switch (state) { /* standard documented hwmon values */
	case ASUS_FAN_CTRL_FULLSPEED:
		value = 1;
		break;
	case ASUS_FAN_CTRL_AUTO:
		value = 0;
		break;
	default:
		return -EINVAL;
	}

	ret = asus_wmi_set_devstate(ASUS_WMI_DEVID_GPU_FAN_CTRL,
				    value, &retval);
	if (ret)
		return ret;

	if (retval != 1)
		return -EIO;

	asus->gpu_fan_pwm_mode = state;
	return count;
}

static ssize_t pwm3_enable_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", asus->mid_fan_pwm_mode);
}

static ssize_t pwm3_enable_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	int state;
	int value;
	int ret;
	u32 retval;

	ret = kstrtouint(buf, 10, &state);
	if (ret)
		return ret;

	switch (state) { /* standard documented hwmon values */
	case ASUS_FAN_CTRL_FULLSPEED:
		value = 1;
		break;
	case ASUS_FAN_CTRL_AUTO:
		value = 0;
		break;
	default:
		return -EINVAL;
	}

	ret = asus_wmi_set_devstate(ASUS_WMI_DEVID_MID_FAN_CTRL,
				    value, &retval);
	if (ret)
		return ret;

	if (retval != 1)
		return -EIO;

	asus->mid_fan_pwm_mode = state;
	return count;
}

/* Fan1 */
static DEVICE_ATTR_RW(pwm1);
static DEVICE_ATTR_RW(pwm1_enable);
static DEVICE_ATTR_RO(fan1_input);
static DEVICE_ATTR_RO(fan1_label);
/* Fan2 - GPU fan */
static DEVICE_ATTR_RW(pwm2_enable);
static DEVICE_ATTR_RO(fan2_input);
static DEVICE_ATTR_RO(fan2_label);
/* Fan3 - Middle/center fan */
static DEVICE_ATTR_RW(pwm3_enable);
static DEVICE_ATTR_RO(fan3_input);
static DEVICE_ATTR_RO(fan3_label);

/* Temperature */
static DEVICE_ATTR(temp1_input, S_IRUGO, asus_hwmon_temp1, NULL);

static struct attribute *hwmon_attributes[] = {
	&dev_attr_pwm1.attr,
	&dev_attr_pwm1_enable.attr,
	&dev_attr_pwm2_enable.attr,
	&dev_attr_pwm3_enable.attr,
	&dev_attr_fan1_input.attr,
	&dev_attr_fan1_label.attr,
	&dev_attr_fan2_input.attr,
	&dev_attr_fan2_label.attr,
	&dev_attr_fan3_input.attr,
	&dev_attr_fan3_label.attr,

	&dev_attr_temp1_input.attr,
	NULL
};

static umode_t asus_hwmon_sysfs_is_visible(struct kobject *kobj,
					  struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct asus_wmi *asus = dev_get_drvdata(dev->parent);
	u32 value = ASUS_WMI_UNSUPPORTED_METHOD;

	if (attr == &dev_attr_pwm1.attr) {
		if (asus->fan_type != FAN_TYPE_AGFN)
			return 0;
	} else if (attr == &dev_attr_fan1_input.attr
	    || attr == &dev_attr_fan1_label.attr
	    || attr == &dev_attr_pwm1_enable.attr) {
		if (asus->fan_type == FAN_TYPE_NONE)
			return 0;
	} else if (attr == &dev_attr_fan2_input.attr
	    || attr == &dev_attr_fan2_label.attr
	    || attr == &dev_attr_pwm2_enable.attr) {
		if (asus->gpu_fan_type == FAN_TYPE_NONE)
			return 0;
	} else if (attr == &dev_attr_fan3_input.attr
	    || attr == &dev_attr_fan3_label.attr
	    || attr == &dev_attr_pwm3_enable.attr) {
		if (asus->mid_fan_type == FAN_TYPE_NONE)
			return 0;
	} else if (attr == &dev_attr_temp1_input.attr) {
		int err = asus_wmi_get_devstate(asus,
						ASUS_WMI_DEVID_THERMAL_CTRL,
						&value);

		if (err < 0)
			return 0; /* can't return negative here */

		/*
		 * If the temperature value in deci-Kelvin is near the absolute
		 * zero temperature, something is clearly wrong
		 */
		if (value == 0 || value == 1)
			return 0;
	}

	return attr->mode;
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
	asus->gpu_fan_type = FAN_TYPE_NONE;
	asus->mid_fan_type = FAN_TYPE_NONE;
	asus->fan_type = FAN_TYPE_NONE;
	asus->agfn_pwm = -1;

	if (asus->driver->quirks->wmi_ignore_fan)
		asus->fan_type = FAN_TYPE_NONE;
	else if (asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_CPU_FAN_CTRL))
		asus->fan_type = FAN_TYPE_SPEC83;
	else if (asus_wmi_has_agfn_fan(asus))
		asus->fan_type = FAN_TYPE_AGFN;

	/*  Modern models like G713 also have GPU fan control */
	if (asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_GPU_FAN_CTRL))
		asus->gpu_fan_type = FAN_TYPE_SPEC83;

	/* Some models also have a center/middle fan */
	if (asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_MID_FAN_CTRL))
		asus->mid_fan_type = FAN_TYPE_SPEC83;

	if (asus->fan_type == FAN_TYPE_NONE)
		return -ENODEV;

	asus_fan_set_auto(asus);
	asus->fan_pwm_mode = ASUS_FAN_CTRL_AUTO;
	return 0;
}

/* Fan mode *******************************************************************/

static int fan_boost_mode_check_present(struct asus_wmi *asus)
{
	u32 result;
	int err;

	asus->fan_boost_mode_available = false;

	err = asus_wmi_get_devstate(asus, ASUS_WMI_DEVID_FAN_BOOST_MODE,
				    &result);
	if (err) {
		if (err == -ENODEV)
			return 0;
		else
			return err;
	}

	if ((result & ASUS_WMI_DSTS_PRESENCE_BIT) &&
			(result & ASUS_FAN_BOOST_MODES_MASK)) {
		asus->fan_boost_mode_available = true;
		asus->fan_boost_mode_mask = result & ASUS_FAN_BOOST_MODES_MASK;
	}

	return 0;
}

static int fan_boost_mode_write(struct asus_wmi *asus)
{
	u32 retval;
	u8 value;
	int err;

	value = asus->fan_boost_mode;

	pr_info("Set fan boost mode: %u\n", value);
	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_FAN_BOOST_MODE, value,
				    &retval);

	sysfs_notify(&asus->platform_device->dev.kobj, NULL,
			"fan_boost_mode");

	if (err) {
		pr_warn("Failed to set fan boost mode: %d\n", err);
		return err;
	}

	if (retval != 1) {
		pr_warn("Failed to set fan boost mode (retval): 0x%x\n",
			retval);
		return -EIO;
	}

	return 0;
}

static int fan_boost_mode_switch_next(struct asus_wmi *asus)
{
	u8 mask = asus->fan_boost_mode_mask;

	if (asus->fan_boost_mode == ASUS_FAN_BOOST_MODE_NORMAL) {
		if (mask & ASUS_FAN_BOOST_MODE_OVERBOOST_MASK)
			asus->fan_boost_mode = ASUS_FAN_BOOST_MODE_OVERBOOST;
		else if (mask & ASUS_FAN_BOOST_MODE_SILENT_MASK)
			asus->fan_boost_mode = ASUS_FAN_BOOST_MODE_SILENT;
	} else if (asus->fan_boost_mode == ASUS_FAN_BOOST_MODE_OVERBOOST) {
		if (mask & ASUS_FAN_BOOST_MODE_SILENT_MASK)
			asus->fan_boost_mode = ASUS_FAN_BOOST_MODE_SILENT;
		else
			asus->fan_boost_mode = ASUS_FAN_BOOST_MODE_NORMAL;
	} else {
		asus->fan_boost_mode = ASUS_FAN_BOOST_MODE_NORMAL;
	}

	return fan_boost_mode_write(asus);
}

static ssize_t fan_boost_mode_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", asus->fan_boost_mode);
}

static ssize_t fan_boost_mode_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	u8 mask = asus->fan_boost_mode_mask;
	u8 new_mode;
	int result;

	result = kstrtou8(buf, 10, &new_mode);
	if (result < 0) {
		pr_warn("Trying to store invalid value\n");
		return result;
	}

	if (new_mode == ASUS_FAN_BOOST_MODE_OVERBOOST) {
		if (!(mask & ASUS_FAN_BOOST_MODE_OVERBOOST_MASK))
			return -EINVAL;
	} else if (new_mode == ASUS_FAN_BOOST_MODE_SILENT) {
		if (!(mask & ASUS_FAN_BOOST_MODE_SILENT_MASK))
			return -EINVAL;
	} else if (new_mode != ASUS_FAN_BOOST_MODE_NORMAL) {
		return -EINVAL;
	}

	asus->fan_boost_mode = new_mode;
	fan_boost_mode_write(asus);

	return count;
}

// Fan boost mode: 0 - normal, 1 - overboost, 2 - silent
static DEVICE_ATTR_RW(fan_boost_mode);

/* Custom fan curves **********************************************************/

static void fan_curve_copy_from_buf(struct fan_curve_data *data, u8 *buf)
{
	int i;

	for (i = 0; i < FAN_CURVE_POINTS; i++) {
		data->temps[i] = buf[i];
	}

	for (i = 0; i < FAN_CURVE_POINTS; i++) {
		data->percents[i] =
			255 * buf[i + FAN_CURVE_POINTS] / 100;
	}
}

static int fan_curve_get_factory_default(struct asus_wmi *asus, u32 fan_dev)
{
	struct fan_curve_data *curves;
	u8 buf[FAN_CURVE_BUF_LEN];
	int err, fan_idx;
	u8 mode = 0;

	if (asus->throttle_thermal_policy_available)
		mode = asus->throttle_thermal_policy_mode;
	/* DEVID_<C/G>PU_FAN_CURVE is switched for OVERBOOST vs SILENT */
	if (mode == 2)
		mode = 1;
	else if (mode == 1)
		mode = 2;

	err = asus_wmi_evaluate_method_buf(asus->dsts_id, fan_dev, mode, buf,
					   FAN_CURVE_BUF_LEN);
	if (err) {
		pr_warn("%s (0x%08x) failed: %d\n", __func__, fan_dev, err);
		return err;
	}

	fan_idx = FAN_CURVE_DEV_CPU;
	if (fan_dev == ASUS_WMI_DEVID_GPU_FAN_CURVE)
		fan_idx = FAN_CURVE_DEV_GPU;

	if (fan_dev == ASUS_WMI_DEVID_MID_FAN_CURVE)
		fan_idx = FAN_CURVE_DEV_MID;

	curves = &asus->custom_fan_curves[fan_idx];
	curves->device_id = fan_dev;

	fan_curve_copy_from_buf(curves, buf);
	return 0;
}

/* Check if capability exists, and populate defaults */
static int fan_curve_check_present(struct asus_wmi *asus, bool *available,
				   u32 fan_dev)
{
	int err;

	*available = false;

	if (asus->fan_type == FAN_TYPE_NONE)
		return 0;

	err = fan_curve_get_factory_default(asus, fan_dev);
	if (err) {
		return 0;
	}

	*available = true;
	return 0;
}

/* Determine which fan the attribute is for if SENSOR_ATTR */
static struct fan_curve_data *fan_curve_attr_select(struct asus_wmi *asus,
					      struct device_attribute *attr)
{
	int index = to_sensor_dev_attr(attr)->index;

	return &asus->custom_fan_curves[index];
}

/* Determine which fan the attribute is for if SENSOR_ATTR_2 */
static struct fan_curve_data *fan_curve_attr_2_select(struct asus_wmi *asus,
					    struct device_attribute *attr)
{
	int nr = to_sensor_dev_attr_2(attr)->nr;

	return &asus->custom_fan_curves[nr & ~FAN_CURVE_PWM_MASK];
}

static ssize_t fan_curve_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *dev_attr = to_sensor_dev_attr_2(attr);
	struct asus_wmi *asus = dev_get_drvdata(dev);
	struct fan_curve_data *data;
	int value, pwm, index;

	data = fan_curve_attr_2_select(asus, attr);
	pwm = dev_attr->nr & FAN_CURVE_PWM_MASK;
	index = dev_attr->index;

	if (pwm)
		value = data->percents[index];
	else
		value = data->temps[index];

	return sysfs_emit(buf, "%d\n", value);
}

/*
 * "fan_dev" is the related WMI method such as ASUS_WMI_DEVID_CPU_FAN_CURVE.
 */
static int fan_curve_write(struct asus_wmi *asus,
			   struct fan_curve_data *data)
{
	u32 arg1 = 0, arg2 = 0, arg3 = 0, arg4 = 0;
	u8 *percents = data->percents;
	u8 *temps = data->temps;
	int ret, i, shift = 0;

	if (!data->enabled)
		return 0;

	for (i = 0; i < FAN_CURVE_POINTS / 2; i++) {
		arg1 += (temps[i]) << shift;
		arg2 += (temps[i + 4]) << shift;
		/* Scale to percentage for device */
		arg3 += (100 * percents[i] / 255) << shift;
		arg4 += (100 * percents[i + 4] / 255) << shift;
		shift += 8;
	}

	return asus_wmi_evaluate_method5(ASUS_WMI_METHODID_DEVS,
					 data->device_id,
					 arg1, arg2, arg3, arg4, &ret);
}

static ssize_t fan_curve_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct sensor_device_attribute_2 *dev_attr = to_sensor_dev_attr_2(attr);
	struct asus_wmi *asus = dev_get_drvdata(dev);
	struct fan_curve_data *data;
	int err, pwm, index;
	u8 value;

	data = fan_curve_attr_2_select(asus, attr);
	pwm = dev_attr->nr & FAN_CURVE_PWM_MASK;
	index = dev_attr->index;

	err = kstrtou8(buf, 10, &value);
	if (err < 0)
		return err;

	if (pwm)
		data->percents[index] = value;
	else
		data->temps[index] = value;

	/*
	 * Mark as disabled so the user has to explicitly enable to apply a
	 * changed fan curve. This prevents potential lockups from writing out
	 * many changes as one-write-per-change.
	 */
	data->enabled = false;

	return count;
}

static ssize_t fan_curve_enable_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	struct fan_curve_data *data;
	int out = 2;

	data = fan_curve_attr_select(asus, attr);

	if (data->enabled)
		out = 1;

	return sysfs_emit(buf, "%d\n", out);
}

static ssize_t fan_curve_enable_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	struct fan_curve_data *data;
	int value, err;

	data = fan_curve_attr_select(asus, attr);

	err = kstrtoint(buf, 10, &value);
	if (err < 0)
		return err;

	switch (value) {
	case 1:
		data->enabled = true;
		break;
	case 2:
		data->enabled = false;
		break;
	/*
	 * Auto + reset the fan curve data to defaults. Make it an explicit
	 * option so that users don't accidentally overwrite a set fan curve.
	 */
	case 3:
		err = fan_curve_get_factory_default(asus, data->device_id);
		if (err)
			return err;
		data->enabled = false;
		break;
	default:
		return -EINVAL;
	}

	if (data->enabled) {
		err = fan_curve_write(asus, data);
		if (err)
			return err;
	} else {
		/*
		 * For machines with throttle this is the only way to reset fans
		 * to default mode of operation (does not erase curve data).
		 */
		if (asus->throttle_thermal_policy_available) {
			err = throttle_thermal_policy_write(asus);
			if (err)
				return err;
		/* Similar is true for laptops with this fan */
		} else if (asus->fan_type == FAN_TYPE_SPEC83) {
			err = asus_fan_set_auto(asus);
			if (err)
				return err;
		} else {
			/* Safeguard against fautly ACPI tables */
			err = fan_curve_get_factory_default(asus, data->device_id);
			if (err)
				return err;
			err = fan_curve_write(asus, data);
			if (err)
				return err;
		}
	}
	return count;
}

/* CPU */
static SENSOR_DEVICE_ATTR_RW(pwm1_enable, fan_curve_enable, FAN_CURVE_DEV_CPU);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point1_temp, fan_curve,
			       FAN_CURVE_DEV_CPU, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point2_temp, fan_curve,
			       FAN_CURVE_DEV_CPU, 1);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point3_temp, fan_curve,
			       FAN_CURVE_DEV_CPU, 2);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point4_temp, fan_curve,
			       FAN_CURVE_DEV_CPU, 3);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point5_temp, fan_curve,
			       FAN_CURVE_DEV_CPU, 4);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point6_temp, fan_curve,
			       FAN_CURVE_DEV_CPU, 5);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point7_temp, fan_curve,
			       FAN_CURVE_DEV_CPU, 6);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point8_temp, fan_curve,
			       FAN_CURVE_DEV_CPU, 7);

static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point1_pwm, fan_curve,
				FAN_CURVE_DEV_CPU | FAN_CURVE_PWM_MASK, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point2_pwm, fan_curve,
			       FAN_CURVE_DEV_CPU | FAN_CURVE_PWM_MASK, 1);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point3_pwm, fan_curve,
			       FAN_CURVE_DEV_CPU | FAN_CURVE_PWM_MASK, 2);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point4_pwm, fan_curve,
			       FAN_CURVE_DEV_CPU | FAN_CURVE_PWM_MASK, 3);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point5_pwm, fan_curve,
			       FAN_CURVE_DEV_CPU | FAN_CURVE_PWM_MASK, 4);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point6_pwm, fan_curve,
			       FAN_CURVE_DEV_CPU | FAN_CURVE_PWM_MASK, 5);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point7_pwm, fan_curve,
			       FAN_CURVE_DEV_CPU | FAN_CURVE_PWM_MASK, 6);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point8_pwm, fan_curve,
			       FAN_CURVE_DEV_CPU | FAN_CURVE_PWM_MASK, 7);

/* GPU */
static SENSOR_DEVICE_ATTR_RW(pwm2_enable, fan_curve_enable, FAN_CURVE_DEV_GPU);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point1_temp, fan_curve,
			       FAN_CURVE_DEV_GPU, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point2_temp, fan_curve,
			       FAN_CURVE_DEV_GPU, 1);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point3_temp, fan_curve,
			       FAN_CURVE_DEV_GPU, 2);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point4_temp, fan_curve,
			       FAN_CURVE_DEV_GPU, 3);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point5_temp, fan_curve,
			       FAN_CURVE_DEV_GPU, 4);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point6_temp, fan_curve,
			       FAN_CURVE_DEV_GPU, 5);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point7_temp, fan_curve,
			       FAN_CURVE_DEV_GPU, 6);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point8_temp, fan_curve,
			       FAN_CURVE_DEV_GPU, 7);

static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point1_pwm, fan_curve,
			       FAN_CURVE_DEV_GPU | FAN_CURVE_PWM_MASK, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point2_pwm, fan_curve,
			       FAN_CURVE_DEV_GPU | FAN_CURVE_PWM_MASK, 1);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point3_pwm, fan_curve,
			       FAN_CURVE_DEV_GPU | FAN_CURVE_PWM_MASK, 2);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point4_pwm, fan_curve,
			       FAN_CURVE_DEV_GPU | FAN_CURVE_PWM_MASK, 3);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point5_pwm, fan_curve,
			       FAN_CURVE_DEV_GPU | FAN_CURVE_PWM_MASK, 4);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point6_pwm, fan_curve,
			       FAN_CURVE_DEV_GPU | FAN_CURVE_PWM_MASK, 5);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point7_pwm, fan_curve,
			       FAN_CURVE_DEV_GPU | FAN_CURVE_PWM_MASK, 6);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point8_pwm, fan_curve,
			       FAN_CURVE_DEV_GPU | FAN_CURVE_PWM_MASK, 7);

/* MID */
static SENSOR_DEVICE_ATTR_RW(pwm3_enable, fan_curve_enable, FAN_CURVE_DEV_MID);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point1_temp, fan_curve,
			       FAN_CURVE_DEV_MID, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point2_temp, fan_curve,
			       FAN_CURVE_DEV_MID, 1);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point3_temp, fan_curve,
			       FAN_CURVE_DEV_MID, 2);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point4_temp, fan_curve,
			       FAN_CURVE_DEV_MID, 3);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point5_temp, fan_curve,
			       FAN_CURVE_DEV_MID, 4);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point6_temp, fan_curve,
			       FAN_CURVE_DEV_MID, 5);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point7_temp, fan_curve,
			       FAN_CURVE_DEV_MID, 6);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point8_temp, fan_curve,
			       FAN_CURVE_DEV_MID, 7);

static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point1_pwm, fan_curve,
			       FAN_CURVE_DEV_MID | FAN_CURVE_PWM_MASK, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point2_pwm, fan_curve,
			       FAN_CURVE_DEV_MID | FAN_CURVE_PWM_MASK, 1);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point3_pwm, fan_curve,
			       FAN_CURVE_DEV_MID | FAN_CURVE_PWM_MASK, 2);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point4_pwm, fan_curve,
			       FAN_CURVE_DEV_MID | FAN_CURVE_PWM_MASK, 3);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point5_pwm, fan_curve,
			       FAN_CURVE_DEV_MID | FAN_CURVE_PWM_MASK, 4);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point6_pwm, fan_curve,
			       FAN_CURVE_DEV_MID | FAN_CURVE_PWM_MASK, 5);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point7_pwm, fan_curve,
			       FAN_CURVE_DEV_MID | FAN_CURVE_PWM_MASK, 6);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point8_pwm, fan_curve,
			       FAN_CURVE_DEV_MID | FAN_CURVE_PWM_MASK, 7);

static struct attribute *asus_fan_curve_attr[] = {
	/* CPU */
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point3_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point4_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point5_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point6_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point7_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point8_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point3_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point4_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point5_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point6_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point7_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point8_pwm.dev_attr.attr,
	/* GPU */
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point3_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point4_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point5_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point6_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point7_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point8_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point3_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point4_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point5_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point6_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point7_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point8_pwm.dev_attr.attr,
	/* MID */
	&sensor_dev_attr_pwm3_enable.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point3_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point4_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point5_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point6_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point7_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point8_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point3_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point4_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point5_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point6_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point7_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point8_pwm.dev_attr.attr,
	NULL
};

static umode_t asus_fan_curve_is_visible(struct kobject *kobj,
					 struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct asus_wmi *asus = dev_get_drvdata(dev->parent);

	/*
	 * Check the char instead of casting attr as there are two attr types
	 * involved here (attr1 and attr2)
	 */
	if (asus->cpu_fan_curve_available && attr->name[3] == '1')
		return 0644;

	if (asus->gpu_fan_curve_available && attr->name[3] == '2')
		return 0644;

	if (asus->mid_fan_curve_available && attr->name[3] == '3')
		return 0644;

	return 0;
}

static const struct attribute_group asus_fan_curve_attr_group = {
	.is_visible = asus_fan_curve_is_visible,
	.attrs = asus_fan_curve_attr,
};
__ATTRIBUTE_GROUPS(asus_fan_curve_attr);

/*
 * Must be initialised after throttle_thermal_policy_check_present() as
 * we check the status of throttle_thermal_policy_available during init.
 */
static int asus_wmi_custom_fan_curve_init(struct asus_wmi *asus)
{
	struct device *dev = &asus->platform_device->dev;
	struct device *hwmon;
	int err;

	err = fan_curve_check_present(asus, &asus->cpu_fan_curve_available,
				      ASUS_WMI_DEVID_CPU_FAN_CURVE);
	if (err)
		return err;

	err = fan_curve_check_present(asus, &asus->gpu_fan_curve_available,
				      ASUS_WMI_DEVID_GPU_FAN_CURVE);
	if (err)
		return err;

	err = fan_curve_check_present(asus, &asus->mid_fan_curve_available,
				      ASUS_WMI_DEVID_MID_FAN_CURVE);
	if (err)
		return err;

	if (!asus->cpu_fan_curve_available
		&& !asus->gpu_fan_curve_available
		&& !asus->mid_fan_curve_available)
		return 0;

	hwmon = devm_hwmon_device_register_with_groups(
		dev, "asus_custom_fan_curve", asus, asus_fan_curve_attr_groups);

	if (IS_ERR(hwmon)) {
		dev_err(dev,
			"Could not register asus_custom_fan_curve device\n");
		return PTR_ERR(hwmon);
	}

	return 0;
}

/* Throttle thermal policy ****************************************************/

static int throttle_thermal_policy_check_present(struct asus_wmi *asus)
{
	u32 result;
	int err;

	asus->throttle_thermal_policy_available = false;

	err = asus_wmi_get_devstate(asus,
				    ASUS_WMI_DEVID_THROTTLE_THERMAL_POLICY,
				    &result);
	if (err) {
		if (err == -ENODEV)
			return 0;
		return err;
	}

	if (result & ASUS_WMI_DSTS_PRESENCE_BIT)
		asus->throttle_thermal_policy_available = true;

	return 0;
}

static int throttle_thermal_policy_write(struct asus_wmi *asus)
{
	int err;
	u8 value;
	u32 retval;

	value = asus->throttle_thermal_policy_mode;

	err = asus_wmi_set_devstate(ASUS_WMI_DEVID_THROTTLE_THERMAL_POLICY,
				    value, &retval);

	sysfs_notify(&asus->platform_device->dev.kobj, NULL,
			"throttle_thermal_policy");

	if (err) {
		pr_warn("Failed to set throttle thermal policy: %d\n", err);
		return err;
	}

	if (retval != 1) {
		pr_warn("Failed to set throttle thermal policy (retval): 0x%x\n",
			retval);
		return -EIO;
	}

	/* Must set to disabled if mode is toggled */
	if (asus->cpu_fan_curve_available)
		asus->custom_fan_curves[FAN_CURVE_DEV_CPU].enabled = false;
	if (asus->gpu_fan_curve_available)
		asus->custom_fan_curves[FAN_CURVE_DEV_GPU].enabled = false;
	if (asus->mid_fan_curve_available)
		asus->custom_fan_curves[FAN_CURVE_DEV_MID].enabled = false;

	return 0;
}

static int throttle_thermal_policy_set_default(struct asus_wmi *asus)
{
	if (!asus->throttle_thermal_policy_available)
		return 0;

	asus->throttle_thermal_policy_mode = ASUS_THROTTLE_THERMAL_POLICY_DEFAULT;
	return throttle_thermal_policy_write(asus);
}

static int throttle_thermal_policy_switch_next(struct asus_wmi *asus)
{
	u8 new_mode = asus->throttle_thermal_policy_mode + 1;
	int err;

	if (new_mode > ASUS_THROTTLE_THERMAL_POLICY_SILENT)
		new_mode = ASUS_THROTTLE_THERMAL_POLICY_DEFAULT;

	asus->throttle_thermal_policy_mode = new_mode;
	err = throttle_thermal_policy_write(asus);
	if (err)
		return err;

	/*
	 * Ensure that platform_profile updates userspace with the change to ensure
	 * that platform_profile and throttle_thermal_policy_mode are in sync.
	 */
	platform_profile_notify();

	return 0;
}

static ssize_t throttle_thermal_policy_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	u8 mode = asus->throttle_thermal_policy_mode;

	return sysfs_emit(buf, "%d\n", mode);
}

static ssize_t throttle_thermal_policy_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct asus_wmi *asus = dev_get_drvdata(dev);
	u8 new_mode;
	int result;
	int err;

	result = kstrtou8(buf, 10, &new_mode);
	if (result < 0)
		return result;

	if (new_mode > ASUS_THROTTLE_THERMAL_POLICY_SILENT)
		return -EINVAL;

	asus->throttle_thermal_policy_mode = new_mode;
	err = throttle_thermal_policy_write(asus);
	if (err)
		return err;

	/*
	 * Ensure that platform_profile updates userspace with the change to ensure
	 * that platform_profile and throttle_thermal_policy_mode are in sync.
	 */
	platform_profile_notify();

	return count;
}

// Throttle thermal policy: 0 - default, 1 - overboost, 2 - silent
static DEVICE_ATTR_RW(throttle_thermal_policy);

/* Platform profile ***********************************************************/
static int asus_wmi_platform_profile_get(struct platform_profile_handler *pprof,
					enum platform_profile_option *profile)
{
	struct asus_wmi *asus;
	int tp;

	asus = container_of(pprof, struct asus_wmi, platform_profile_handler);

	tp = asus->throttle_thermal_policy_mode;

	switch (tp) {
	case ASUS_THROTTLE_THERMAL_POLICY_DEFAULT:
		*profile = PLATFORM_PROFILE_BALANCED;
		break;
	case ASUS_THROTTLE_THERMAL_POLICY_OVERBOOST:
		*profile = PLATFORM_PROFILE_PERFORMANCE;
		break;
	case ASUS_THROTTLE_THERMAL_POLICY_SILENT:
		*profile = PLATFORM_PROFILE_QUIET;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int asus_wmi_platform_profile_set(struct platform_profile_handler *pprof,
					enum platform_profile_option profile)
{
	struct asus_wmi *asus;
	int tp;

	asus = container_of(pprof, struct asus_wmi, platform_profile_handler);

	switch (profile) {
	case PLATFORM_PROFILE_PERFORMANCE:
		tp = ASUS_THROTTLE_THERMAL_POLICY_OVERBOOST;
		break;
	case PLATFORM_PROFILE_BALANCED:
		tp = ASUS_THROTTLE_THERMAL_POLICY_DEFAULT;
		break;
	case PLATFORM_PROFILE_QUIET:
		tp = ASUS_THROTTLE_THERMAL_POLICY_SILENT;
		break;
	default:
		return -EOPNOTSUPP;
	}

	asus->throttle_thermal_policy_mode = tp;
	return throttle_thermal_policy_write(asus);
}

static int platform_profile_setup(struct asus_wmi *asus)
{
	struct device *dev = &asus->platform_device->dev;
	int err;

	/*
	 * Not an error if a component platform_profile relies on is unavailable
	 * so early return, skipping the setup of platform_profile.
	 */
	if (!asus->throttle_thermal_policy_available)
		return 0;

	dev_info(dev, "Using throttle_thermal_policy for platform_profile support\n");

	asus->platform_profile_handler.profile_get = asus_wmi_platform_profile_get;
	asus->platform_profile_handler.profile_set = asus_wmi_platform_profile_set;

	set_bit(PLATFORM_PROFILE_QUIET, asus->platform_profile_handler.choices);
	set_bit(PLATFORM_PROFILE_BALANCED,
		asus->platform_profile_handler.choices);
	set_bit(PLATFORM_PROFILE_PERFORMANCE,
		asus->platform_profile_handler.choices);

	err = platform_profile_register(&asus->platform_profile_handler);
	if (err)
		return err;

	asus->platform_profile_support = true;
	return 0;
}

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
	unsigned int key_value = 1;
	bool autorelease = 1;
	int orig_code = code;

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

	if (code == asus->tablet_switch_event_code) {
		asus_wmi_tablet_mode_get_state(asus);
		return;
	}

	if (code == NOTIFY_KBD_FBM || code == NOTIFY_KBD_TTP) {
		if (asus->fan_boost_mode_available)
			fan_boost_mode_switch_next(asus);
		if (asus->throttle_thermal_policy_available)
			throttle_thermal_policy_switch_next(asus);
		return;

	}

	if (is_display_toggle(code) && asus->driver->quirks->no_display_toggle)
		return;

	if (!sparse_keymap_report_event(asus->inputdev, code,
					key_value, autorelease))
		pr_info("Unknown key code 0x%x\n", code);
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

static ssize_t store_sys_wmi(struct asus_wmi *asus, int devid,
			     const char *buf, size_t count)
{
	u32 retval;
	int err, value;

	value = asus_wmi_get_devstate_simple(asus, devid);
	if (value < 0)
		return value;

	err = kstrtoint(buf, 0, &value);
	if (err)
		return err;

	err = asus_wmi_set_devstate(devid, value, &retval);
	if (err < 0)
		return err;

	return count;
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

	rv = kstrtoint(buf, 0, &value);
	if (rv)
		return rv;

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
	&dev_attr_charge_mode.attr,
	&dev_attr_egpu_enable.attr,
	&dev_attr_egpu_connected.attr,
	&dev_attr_dgpu_disable.attr,
	&dev_attr_gpu_mux_mode.attr,
	&dev_attr_lid_resume.attr,
	&dev_attr_als_enable.attr,
	&dev_attr_fan_boost_mode.attr,
	&dev_attr_throttle_thermal_policy.attr,
	&dev_attr_ppt_pl2_sppt.attr,
	&dev_attr_ppt_pl1_spl.attr,
	&dev_attr_ppt_fppt.attr,
	&dev_attr_ppt_apu_sppt.attr,
	&dev_attr_ppt_platform_sppt.attr,
	&dev_attr_nv_dynamic_boost.attr,
	&dev_attr_nv_temp_target.attr,
	&dev_attr_panel_od.attr,
	&dev_attr_mini_led_mode.attr,
	NULL
};

static umode_t asus_sysfs_is_visible(struct kobject *kobj,
				    struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
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
	else if (attr == &dev_attr_charge_mode.attr)
		ok = asus->charge_mode_available;
	else if (attr == &dev_attr_egpu_enable.attr)
		ok = asus->egpu_enable_available;
	else if (attr == &dev_attr_egpu_connected.attr)
		ok = asus->egpu_connect_available;
	else if (attr == &dev_attr_dgpu_disable.attr)
		ok = asus->dgpu_disable_available;
	else if (attr == &dev_attr_gpu_mux_mode.attr)
		ok = asus->gpu_mux_mode_available;
	else if (attr == &dev_attr_fan_boost_mode.attr)
		ok = asus->fan_boost_mode_available;
	else if (attr == &dev_attr_throttle_thermal_policy.attr)
		ok = asus->throttle_thermal_policy_available;
	else if (attr == &dev_attr_ppt_pl2_sppt.attr)
		ok = asus->ppt_pl2_sppt_available;
	else if (attr == &dev_attr_ppt_pl1_spl.attr)
		ok = asus->ppt_pl1_spl_available;
	else if (attr == &dev_attr_ppt_fppt.attr)
		ok = asus->ppt_fppt_available;
	else if (attr == &dev_attr_ppt_apu_sppt.attr)
		ok = asus->ppt_apu_sppt_available;
	else if (attr == &dev_attr_ppt_platform_sppt.attr)
		ok = asus->ppt_plat_sppt_available;
	else if (attr == &dev_attr_nv_dynamic_boost.attr)
		ok = asus->nv_dyn_boost_available;
	else if (attr == &dev_attr_nv_temp_target.attr)
		ok = asus->nv_temp_tgt_available;
	else if (attr == &dev_attr_panel_od.attr)
		ok = asus->panel_overdrive_available;
	else if (attr == &dev_attr_mini_led_mode.attr)
		ok = asus->mini_led_mode_available;

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

	asus->charge_mode_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_CHARGE_MODE);
	asus->egpu_enable_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_EGPU);
	asus->egpu_connect_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_EGPU_CONNECTED);
	asus->dgpu_disable_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_DGPU);
	asus->gpu_mux_mode_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_GPU_MUX);
	asus->kbd_rgb_mode_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_TUF_RGB_MODE);
	asus->kbd_rgb_state_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_TUF_RGB_STATE);
	asus->ppt_pl2_sppt_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_PPT_PL2_SPPT);
	asus->ppt_pl1_spl_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_PPT_PL1_SPL);
	asus->ppt_fppt_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_PPT_FPPT);
	asus->ppt_apu_sppt_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_PPT_APU_SPPT);
	asus->ppt_plat_sppt_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_PPT_PLAT_SPPT);
	asus->nv_dyn_boost_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_NV_DYN_BOOST);
	asus->nv_temp_tgt_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_NV_THERM_TARGET);
	asus->panel_overdrive_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_PANEL_OD);
	asus->mini_led_mode_available = asus_wmi_dev_is_present(asus, ASUS_WMI_DEVID_MINI_LED_MODE);

	err = fan_boost_mode_check_present(asus);
	if (err)
		goto fail_fan_boost_mode;

	err = throttle_thermal_policy_check_present(asus);
	if (err)
		goto fail_throttle_thermal_policy;
	else
		throttle_thermal_policy_set_default(asus);

	err = platform_profile_setup(asus);
	if (err)
		goto fail_platform_profile_setup;

	err = asus_wmi_sysfs_init(asus->platform_device);
	if (err)
		goto fail_sysfs;

	err = asus_wmi_input_init(asus);
	if (err)
		goto fail_input;

	err = asus_wmi_fan_init(asus); /* probably no problems on error */

	err = asus_wmi_hwmon_init(asus);
	if (err)
		goto fail_hwmon;

	err = asus_wmi_custom_fan_curve_init(asus);
	if (err)
		goto fail_custom_fan_curve;

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

	if (asus->driver->quirks->xusb2pr)
		asus_wmi_set_xusb2pr(asus);

	if (acpi_video_get_backlight_type() == acpi_backlight_vendor) {
		err = asus_wmi_backlight_init(asus);
		if (err && err != -ENODEV)
			goto fail_backlight;
	} else if (asus->driver->quirks->wmi_backlight_set_devstate)
		err = asus_wmi_set_devstate(ASUS_WMI_DEVID_BACKLIGHT, 2, NULL);

	if (asus_wmi_has_fnlock_key(asus)) {
		asus->fnlock_locked = fnlock_default;
		asus_wmi_fnlock_update(asus);
	}

	status = wmi_install_notify_handler(asus->driver->event_guid,
					    asus_wmi_notify, asus);
	if (ACPI_FAILURE(status)) {
		pr_err("Unable to register notify handler - %d\n", status);
		err = -ENODEV;
		goto fail_wmi_handler;
	}

	asus_wmi_battery_init(asus);

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
fail_throttle_thermal_policy:
fail_custom_fan_curve:
fail_platform_profile_setup:
	if (asus->platform_profile_support)
		platform_profile_remove();
fail_fan_boost_mode:
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
	asus_fan_set_auto(asus);
	throttle_thermal_policy_set_default(asus);
	asus_wmi_battery_exit(asus);

	if (asus->platform_profile_support)
		platform_profile_remove();

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

	asus_wmi_tablet_mode_get_state(asus);
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

	asus_wmi_tablet_mode_get_state(asus);
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
