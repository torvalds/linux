// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  ideapad-laptop.c - Lenovo IdeaPad ACPI Extras
 *
 *  Copyright © 2010 Intel Corporation
 *  Copyright © 2010 David Woodhouse <dwmw2@infradead.org>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/backlight.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/fb.h>
#include <linux/i8042.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_profile.h>
#include <linux/rfkill.h>
#include <linux/seq_file.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include <acpi/video.h>

#include <dt-bindings/leds/common.h>

#define IDEAPAD_RFKILL_DEV_NUM	3

#if IS_ENABLED(CONFIG_ACPI_WMI)
static const char *const ideapad_wmi_fnesc_events[] = {
	"26CAB2E5-5CF1-46AE-AAC3-4A12B6BA50E6", /* Yoga 3 */
	"56322276-8493-4CE8-A783-98C991274F5E", /* Yoga 700 */
};
#endif

enum {
	CFG_CAP_BT_BIT       = 16,
	CFG_CAP_3G_BIT       = 17,
	CFG_CAP_WIFI_BIT     = 18,
	CFG_CAP_CAM_BIT      = 19,
	CFG_CAP_TOUCHPAD_BIT = 30,
};

enum {
	GBMD_CONSERVATION_STATE_BIT = 5,
};

enum {
	SBMC_CONSERVATION_ON  = 3,
	SBMC_CONSERVATION_OFF = 5,
};

enum {
	HALS_KBD_BL_SUPPORT_BIT       = 4,
	HALS_KBD_BL_STATE_BIT         = 5,
	HALS_USB_CHARGING_SUPPORT_BIT = 6,
	HALS_USB_CHARGING_STATE_BIT   = 7,
	HALS_FNLOCK_SUPPORT_BIT       = 9,
	HALS_FNLOCK_STATE_BIT         = 10,
	HALS_HOTKEYS_PRIMARY_BIT      = 11,
};

enum {
	SALS_KBD_BL_ON        = 0x8,
	SALS_KBD_BL_OFF       = 0x9,
	SALS_USB_CHARGING_ON  = 0xa,
	SALS_USB_CHARGING_OFF = 0xb,
	SALS_FNLOCK_ON        = 0xe,
	SALS_FNLOCK_OFF       = 0xf,
};

enum {
	VPCCMD_R_VPC1 = 0x10,
	VPCCMD_R_BL_MAX,
	VPCCMD_R_BL,
	VPCCMD_W_BL,
	VPCCMD_R_WIFI,
	VPCCMD_W_WIFI,
	VPCCMD_R_BT,
	VPCCMD_W_BT,
	VPCCMD_R_BL_POWER,
	VPCCMD_R_NOVO,
	VPCCMD_R_VPC2,
	VPCCMD_R_TOUCHPAD,
	VPCCMD_W_TOUCHPAD,
	VPCCMD_R_CAMERA,
	VPCCMD_W_CAMERA,
	VPCCMD_R_3G,
	VPCCMD_W_3G,
	VPCCMD_R_ODD, /* 0x21 */
	VPCCMD_W_FAN,
	VPCCMD_R_RF,
	VPCCMD_W_RF,
	VPCCMD_R_FAN = 0x2B,
	VPCCMD_R_SPECIAL_BUTTONS = 0x31,
	VPCCMD_W_BL_POWER = 0x33,
};

struct ideapad_dytc_priv {
	enum platform_profile_option current_profile;
	struct platform_profile_handler pprof;
	struct mutex mutex; /* protects the DYTC interface */
	struct ideapad_private *priv;
};

struct ideapad_rfk_priv {
	int dev;
	struct ideapad_private *priv;
};

struct ideapad_private {
	struct acpi_device *adev;
	struct rfkill *rfk[IDEAPAD_RFKILL_DEV_NUM];
	struct ideapad_rfk_priv rfk_priv[IDEAPAD_RFKILL_DEV_NUM];
	struct platform_device *platform_device;
	struct input_dev *inputdev;
	struct backlight_device *blightdev;
	struct ideapad_dytc_priv *dytc;
	struct dentry *debug;
	unsigned long cfg;
	const char *fnesc_guid;
	struct {
		bool conservation_mode    : 1;
		bool dytc                 : 1;
		bool fan_mode             : 1;
		bool fn_lock              : 1;
		bool hw_rfkill_switch     : 1;
		bool kbd_bl               : 1;
		bool touchpad_ctrl_via_ec : 1;
		bool usb_charging         : 1;
	} features;
	struct {
		bool initialized;
		struct led_classdev led;
		unsigned int last_brightness;
	} kbd_bl;
};

static bool no_bt_rfkill;
module_param(no_bt_rfkill, bool, 0444);
MODULE_PARM_DESC(no_bt_rfkill, "No rfkill for bluetooth.");

/*
 * ACPI Helpers
 */
#define IDEAPAD_EC_TIMEOUT 200 /* in ms */

static int eval_int(acpi_handle handle, const char *name, unsigned long *res)
{
	unsigned long long result;
	acpi_status status;

	status = acpi_evaluate_integer(handle, (char *)name, NULL, &result);
	if (ACPI_FAILURE(status))
		return -EIO;

	*res = result;

	return 0;
}

static int exec_simple_method(acpi_handle handle, const char *name, unsigned long arg)
{
	acpi_status status = acpi_execute_simple_method(handle, (char *)name, arg);

	return ACPI_FAILURE(status) ? -EIO : 0;
}

static int eval_gbmd(acpi_handle handle, unsigned long *res)
{
	return eval_int(handle, "GBMD", res);
}

static int exec_sbmc(acpi_handle handle, unsigned long arg)
{
	return exec_simple_method(handle, "SBMC", arg);
}

static int eval_hals(acpi_handle handle, unsigned long *res)
{
	return eval_int(handle, "HALS", res);
}

static int exec_sals(acpi_handle handle, unsigned long arg)
{
	return exec_simple_method(handle, "SALS", arg);
}

static int eval_int_with_arg(acpi_handle handle, const char *name, unsigned long arg, unsigned long *res)
{
	struct acpi_object_list params;
	unsigned long long result;
	union acpi_object in_obj;
	acpi_status status;

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = arg;

	status = acpi_evaluate_integer(handle, (char *)name, &params, &result);
	if (ACPI_FAILURE(status))
		return -EIO;

	if (res)
		*res = result;

	return 0;
}

static int eval_dytc(acpi_handle handle, unsigned long cmd, unsigned long *res)
{
	return eval_int_with_arg(handle, "DYTC", cmd, res);
}

static int eval_vpcr(acpi_handle handle, unsigned long cmd, unsigned long *res)
{
	return eval_int_with_arg(handle, "VPCR", cmd, res);
}

static int eval_vpcw(acpi_handle handle, unsigned long cmd, unsigned long data)
{
	struct acpi_object_list params;
	union acpi_object in_obj[2];
	acpi_status status;

	params.count = 2;
	params.pointer = in_obj;
	in_obj[0].type = ACPI_TYPE_INTEGER;
	in_obj[0].integer.value = cmd;
	in_obj[1].type = ACPI_TYPE_INTEGER;
	in_obj[1].integer.value = data;

	status = acpi_evaluate_object(handle, "VPCW", &params, NULL);
	if (ACPI_FAILURE(status))
		return -EIO;

	return 0;
}

static int read_ec_data(acpi_handle handle, unsigned long cmd, unsigned long *data)
{
	unsigned long end_jiffies, val;
	int err;

	err = eval_vpcw(handle, 1, cmd);
	if (err)
		return err;

	end_jiffies = jiffies + msecs_to_jiffies(IDEAPAD_EC_TIMEOUT) + 1;

	while (time_before(jiffies, end_jiffies)) {
		schedule();

		err = eval_vpcr(handle, 1, &val);
		if (err)
			return err;

		if (val == 0)
			return eval_vpcr(handle, 0, data);
	}

	acpi_handle_err(handle, "timeout in %s\n", __func__);

	return -ETIMEDOUT;
}

static int write_ec_cmd(acpi_handle handle, unsigned long cmd, unsigned long data)
{
	unsigned long end_jiffies, val;
	int err;

	err = eval_vpcw(handle, 0, data);
	if (err)
		return err;

	err = eval_vpcw(handle, 1, cmd);
	if (err)
		return err;

	end_jiffies = jiffies + msecs_to_jiffies(IDEAPAD_EC_TIMEOUT) + 1;

	while (time_before(jiffies, end_jiffies)) {
		schedule();

		err = eval_vpcr(handle, 1, &val);
		if (err)
			return err;

		if (val == 0)
			return 0;
	}

	acpi_handle_err(handle, "timeout in %s\n", __func__);

	return -ETIMEDOUT;
}

/*
 * debugfs
 */
static int debugfs_status_show(struct seq_file *s, void *data)
{
	struct ideapad_private *priv = s->private;
	unsigned long value;

	if (!read_ec_data(priv->adev->handle, VPCCMD_R_BL_MAX, &value))
		seq_printf(s, "Backlight max:  %lu\n", value);
	if (!read_ec_data(priv->adev->handle, VPCCMD_R_BL, &value))
		seq_printf(s, "Backlight now:  %lu\n", value);
	if (!read_ec_data(priv->adev->handle, VPCCMD_R_BL_POWER, &value))
		seq_printf(s, "BL power value: %s (%lu)\n", value ? "on" : "off", value);

	seq_puts(s, "=====================\n");

	if (!read_ec_data(priv->adev->handle, VPCCMD_R_RF, &value))
		seq_printf(s, "Radio status: %s (%lu)\n", value ? "on" : "off", value);
	if (!read_ec_data(priv->adev->handle, VPCCMD_R_WIFI, &value))
		seq_printf(s, "Wifi status:  %s (%lu)\n", value ? "on" : "off", value);
	if (!read_ec_data(priv->adev->handle, VPCCMD_R_BT, &value))
		seq_printf(s, "BT status:    %s (%lu)\n", value ? "on" : "off", value);
	if (!read_ec_data(priv->adev->handle, VPCCMD_R_3G, &value))
		seq_printf(s, "3G status:    %s (%lu)\n", value ? "on" : "off", value);

	seq_puts(s, "=====================\n");

	if (!read_ec_data(priv->adev->handle, VPCCMD_R_TOUCHPAD, &value))
		seq_printf(s, "Touchpad status: %s (%lu)\n", value ? "on" : "off", value);
	if (!read_ec_data(priv->adev->handle, VPCCMD_R_CAMERA, &value))
		seq_printf(s, "Camera status:   %s (%lu)\n", value ? "on" : "off", value);

	seq_puts(s, "=====================\n");

	if (!eval_gbmd(priv->adev->handle, &value))
		seq_printf(s, "GBMD: %#010lx\n", value);
	if (!eval_hals(priv->adev->handle, &value))
		seq_printf(s, "HALS: %#010lx\n", value);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(debugfs_status);

static int debugfs_cfg_show(struct seq_file *s, void *data)
{
	struct ideapad_private *priv = s->private;

	seq_printf(s, "_CFG: %#010lx\n\n", priv->cfg);

	seq_puts(s, "Capabilities:");
	if (test_bit(CFG_CAP_BT_BIT, &priv->cfg))
		seq_puts(s, " bluetooth");
	if (test_bit(CFG_CAP_3G_BIT, &priv->cfg))
		seq_puts(s, " 3G");
	if (test_bit(CFG_CAP_WIFI_BIT, &priv->cfg))
		seq_puts(s, " wifi");
	if (test_bit(CFG_CAP_CAM_BIT, &priv->cfg))
		seq_puts(s, " camera");
	if (test_bit(CFG_CAP_TOUCHPAD_BIT, &priv->cfg))
		seq_puts(s, " touchpad");
	seq_puts(s, "\n");

	seq_puts(s, "Graphics: ");
	switch (priv->cfg & 0x700) {
	case 0x100:
		seq_puts(s, "Intel");
		break;
	case 0x200:
		seq_puts(s, "ATI");
		break;
	case 0x300:
		seq_puts(s, "Nvidia");
		break;
	case 0x400:
		seq_puts(s, "Intel and ATI");
		break;
	case 0x500:
		seq_puts(s, "Intel and Nvidia");
		break;
	}
	seq_puts(s, "\n");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(debugfs_cfg);

static void ideapad_debugfs_init(struct ideapad_private *priv)
{
	struct dentry *dir;

	dir = debugfs_create_dir("ideapad", NULL);
	priv->debug = dir;

	debugfs_create_file("cfg", 0444, dir, priv, &debugfs_cfg_fops);
	debugfs_create_file("status", 0444, dir, priv, &debugfs_status_fops);
}

static void ideapad_debugfs_exit(struct ideapad_private *priv)
{
	debugfs_remove_recursive(priv->debug);
	priv->debug = NULL;
}

/*
 * sysfs
 */
static ssize_t camera_power_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct ideapad_private *priv = dev_get_drvdata(dev);
	unsigned long result;
	int err;

	err = read_ec_data(priv->adev->handle, VPCCMD_R_CAMERA, &result);
	if (err)
		return err;

	return sysfs_emit(buf, "%d\n", !!result);
}

static ssize_t camera_power_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct ideapad_private *priv = dev_get_drvdata(dev);
	bool state;
	int err;

	err = kstrtobool(buf, &state);
	if (err)
		return err;

	err = write_ec_cmd(priv->adev->handle, VPCCMD_W_CAMERA, state);
	if (err)
		return err;

	return count;
}

static DEVICE_ATTR_RW(camera_power);

static ssize_t conservation_mode_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct ideapad_private *priv = dev_get_drvdata(dev);
	unsigned long result;
	int err;

	err = eval_gbmd(priv->adev->handle, &result);
	if (err)
		return err;

	return sysfs_emit(buf, "%d\n", !!test_bit(GBMD_CONSERVATION_STATE_BIT, &result));
}

static ssize_t conservation_mode_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct ideapad_private *priv = dev_get_drvdata(dev);
	bool state;
	int err;

	err = kstrtobool(buf, &state);
	if (err)
		return err;

	err = exec_sbmc(priv->adev->handle, state ? SBMC_CONSERVATION_ON : SBMC_CONSERVATION_OFF);
	if (err)
		return err;

	return count;
}

static DEVICE_ATTR_RW(conservation_mode);

static ssize_t fan_mode_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct ideapad_private *priv = dev_get_drvdata(dev);
	unsigned long result;
	int err;

	err = read_ec_data(priv->adev->handle, VPCCMD_R_FAN, &result);
	if (err)
		return err;

	return sysfs_emit(buf, "%lu\n", result);
}

static ssize_t fan_mode_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct ideapad_private *priv = dev_get_drvdata(dev);
	unsigned int state;
	int err;

	err = kstrtouint(buf, 0, &state);
	if (err)
		return err;

	if (state > 4 || state == 3)
		return -EINVAL;

	err = write_ec_cmd(priv->adev->handle, VPCCMD_W_FAN, state);
	if (err)
		return err;

	return count;
}

static DEVICE_ATTR_RW(fan_mode);

static ssize_t fn_lock_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct ideapad_private *priv = dev_get_drvdata(dev);
	unsigned long hals;
	int err;

	err = eval_hals(priv->adev->handle, &hals);
	if (err)
		return err;

	return sysfs_emit(buf, "%d\n", !!test_bit(HALS_FNLOCK_STATE_BIT, &hals));
}

static ssize_t fn_lock_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct ideapad_private *priv = dev_get_drvdata(dev);
	bool state;
	int err;

	err = kstrtobool(buf, &state);
	if (err)
		return err;

	err = exec_sals(priv->adev->handle, state ? SALS_FNLOCK_ON : SALS_FNLOCK_OFF);
	if (err)
		return err;

	return count;
}

static DEVICE_ATTR_RW(fn_lock);

static ssize_t touchpad_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct ideapad_private *priv = dev_get_drvdata(dev);
	unsigned long result;
	int err;

	err = read_ec_data(priv->adev->handle, VPCCMD_R_TOUCHPAD, &result);
	if (err)
		return err;

	return sysfs_emit(buf, "%d\n", !!result);
}

static ssize_t touchpad_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct ideapad_private *priv = dev_get_drvdata(dev);
	bool state;
	int err;

	err = kstrtobool(buf, &state);
	if (err)
		return err;

	err = write_ec_cmd(priv->adev->handle, VPCCMD_W_TOUCHPAD, state);
	if (err)
		return err;

	return count;
}

static DEVICE_ATTR_RW(touchpad);

static ssize_t usb_charging_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct ideapad_private *priv = dev_get_drvdata(dev);
	unsigned long hals;
	int err;

	err = eval_hals(priv->adev->handle, &hals);
	if (err)
		return err;

	return sysfs_emit(buf, "%d\n", !!test_bit(HALS_USB_CHARGING_STATE_BIT, &hals));
}

static ssize_t usb_charging_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct ideapad_private *priv = dev_get_drvdata(dev);
	bool state;
	int err;

	err = kstrtobool(buf, &state);
	if (err)
		return err;

	err = exec_sals(priv->adev->handle, state ? SALS_USB_CHARGING_ON : SALS_USB_CHARGING_OFF);
	if (err)
		return err;

	return count;
}

static DEVICE_ATTR_RW(usb_charging);

static struct attribute *ideapad_attributes[] = {
	&dev_attr_camera_power.attr,
	&dev_attr_conservation_mode.attr,
	&dev_attr_fan_mode.attr,
	&dev_attr_fn_lock.attr,
	&dev_attr_touchpad.attr,
	&dev_attr_usb_charging.attr,
	NULL
};

static umode_t ideapad_is_visible(struct kobject *kobj,
				  struct attribute *attr,
				  int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct ideapad_private *priv = dev_get_drvdata(dev);
	bool supported = true;

	if (attr == &dev_attr_camera_power.attr)
		supported = test_bit(CFG_CAP_CAM_BIT, &priv->cfg);
	else if (attr == &dev_attr_conservation_mode.attr)
		supported = priv->features.conservation_mode;
	else if (attr == &dev_attr_fan_mode.attr)
		supported = priv->features.fan_mode;
	else if (attr == &dev_attr_fn_lock.attr)
		supported = priv->features.fn_lock;
	else if (attr == &dev_attr_touchpad.attr)
		supported = priv->features.touchpad_ctrl_via_ec &&
			    test_bit(CFG_CAP_TOUCHPAD_BIT, &priv->cfg);
	else if (attr == &dev_attr_usb_charging.attr)
		supported = priv->features.usb_charging;

	return supported ? attr->mode : 0;
}

static const struct attribute_group ideapad_attribute_group = {
	.is_visible = ideapad_is_visible,
	.attrs = ideapad_attributes
};

/*
 * DYTC Platform profile
 */
#define DYTC_CMD_QUERY        0 /* To get DYTC status - enable/revision */
#define DYTC_CMD_SET          1 /* To enable/disable IC function mode */
#define DYTC_CMD_GET          2 /* To get current IC function and mode */
#define DYTC_CMD_RESET    0x1ff /* To reset back to default */

#define DYTC_QUERY_ENABLE_BIT 8  /* Bit        8 - 0 = disabled, 1 = enabled */
#define DYTC_QUERY_SUBREV_BIT 16 /* Bits 16 - 27 - sub revision */
#define DYTC_QUERY_REV_BIT    28 /* Bits 28 - 31 - revision */

#define DYTC_GET_FUNCTION_BIT 8  /* Bits  8-11 - function setting */
#define DYTC_GET_MODE_BIT     12 /* Bits 12-15 - mode setting */

#define DYTC_SET_FUNCTION_BIT 12 /* Bits 12-15 - function setting */
#define DYTC_SET_MODE_BIT     16 /* Bits 16-19 - mode setting */
#define DYTC_SET_VALID_BIT    20 /* Bit     20 - 1 = on, 0 = off */

#define DYTC_FUNCTION_STD     0  /* Function = 0, standard mode */
#define DYTC_FUNCTION_CQL     1  /* Function = 1, lap mode */
#define DYTC_FUNCTION_MMC     11 /* Function = 11, desk mode */

#define DYTC_MODE_PERFORM     2  /* High power mode aka performance */
#define DYTC_MODE_LOW_POWER       3  /* Low power mode aka quiet */
#define DYTC_MODE_BALANCE   0xF  /* Default mode aka balanced */

#define DYTC_SET_COMMAND(function, mode, on) \
	(DYTC_CMD_SET | (function) << DYTC_SET_FUNCTION_BIT | \
	 (mode) << DYTC_SET_MODE_BIT | \
	 (on) << DYTC_SET_VALID_BIT)

#define DYTC_DISABLE_CQL DYTC_SET_COMMAND(DYTC_FUNCTION_CQL, DYTC_MODE_BALANCE, 0)

#define DYTC_ENABLE_CQL DYTC_SET_COMMAND(DYTC_FUNCTION_CQL, DYTC_MODE_BALANCE, 1)

static int convert_dytc_to_profile(int dytcmode, enum platform_profile_option *profile)
{
	switch (dytcmode) {
	case DYTC_MODE_LOW_POWER:
		*profile = PLATFORM_PROFILE_LOW_POWER;
		break;
	case DYTC_MODE_BALANCE:
		*profile =  PLATFORM_PROFILE_BALANCED;
		break;
	case DYTC_MODE_PERFORM:
		*profile =  PLATFORM_PROFILE_PERFORMANCE;
		break;
	default: /* Unknown mode */
		return -EINVAL;
	}

	return 0;
}

static int convert_profile_to_dytc(enum platform_profile_option profile, int *perfmode)
{
	switch (profile) {
	case PLATFORM_PROFILE_LOW_POWER:
		*perfmode = DYTC_MODE_LOW_POWER;
		break;
	case PLATFORM_PROFILE_BALANCED:
		*perfmode = DYTC_MODE_BALANCE;
		break;
	case PLATFORM_PROFILE_PERFORMANCE:
		*perfmode = DYTC_MODE_PERFORM;
		break;
	default: /* Unknown profile */
		return -EOPNOTSUPP;
	}

	return 0;
}

/*
 * dytc_profile_get: Function to register with platform_profile
 * handler. Returns current platform profile.
 */
static int dytc_profile_get(struct platform_profile_handler *pprof,
			    enum platform_profile_option *profile)
{
	struct ideapad_dytc_priv *dytc = container_of(pprof, struct ideapad_dytc_priv, pprof);

	*profile = dytc->current_profile;
	return 0;
}

/*
 * Helper function - check if we are in CQL mode and if we are
 *  - disable CQL,
 *  - run the command
 *  - enable CQL
 *  If not in CQL mode, just run the command
 */
static int dytc_cql_command(struct ideapad_private *priv, unsigned long cmd,
			    unsigned long *output)
{
	int err, cmd_err, cur_funcmode;

	/* Determine if we are in CQL mode. This alters the commands we do */
	err = eval_dytc(priv->adev->handle, DYTC_CMD_GET, output);
	if (err)
		return err;

	cur_funcmode = (*output >> DYTC_GET_FUNCTION_BIT) & 0xF;
	/* Check if we're OK to return immediately */
	if (cmd == DYTC_CMD_GET && cur_funcmode != DYTC_FUNCTION_CQL)
		return 0;

	if (cur_funcmode == DYTC_FUNCTION_CQL) {
		err = eval_dytc(priv->adev->handle, DYTC_DISABLE_CQL, NULL);
		if (err)
			return err;
	}

	cmd_err = eval_dytc(priv->adev->handle, cmd, output);
	/* Check return condition after we've restored CQL state */

	if (cur_funcmode == DYTC_FUNCTION_CQL) {
		err = eval_dytc(priv->adev->handle, DYTC_ENABLE_CQL, NULL);
		if (err)
			return err;
	}

	return cmd_err;
}

/*
 * dytc_profile_set: Function to register with platform_profile
 * handler. Sets current platform profile.
 */
static int dytc_profile_set(struct platform_profile_handler *pprof,
			    enum platform_profile_option profile)
{
	struct ideapad_dytc_priv *dytc = container_of(pprof, struct ideapad_dytc_priv, pprof);
	struct ideapad_private *priv = dytc->priv;
	unsigned long output;
	int err;

	err = mutex_lock_interruptible(&dytc->mutex);
	if (err)
		return err;

	if (profile == PLATFORM_PROFILE_BALANCED) {
		/* To get back to balanced mode we just issue a reset command */
		err = eval_dytc(priv->adev->handle, DYTC_CMD_RESET, NULL);
		if (err)
			goto unlock;
	} else {
		int perfmode;

		err = convert_profile_to_dytc(profile, &perfmode);
		if (err)
			goto unlock;

		/* Determine if we are in CQL mode. This alters the commands we do */
		err = dytc_cql_command(priv, DYTC_SET_COMMAND(DYTC_FUNCTION_MMC, perfmode, 1),
				       &output);
		if (err)
			goto unlock;
	}

	/* Success - update current profile */
	dytc->current_profile = profile;

unlock:
	mutex_unlock(&dytc->mutex);

	return err;
}

static void dytc_profile_refresh(struct ideapad_private *priv)
{
	enum platform_profile_option profile;
	unsigned long output;
	int err, perfmode;

	mutex_lock(&priv->dytc->mutex);
	err = dytc_cql_command(priv, DYTC_CMD_GET, &output);
	mutex_unlock(&priv->dytc->mutex);
	if (err)
		return;

	perfmode = (output >> DYTC_GET_MODE_BIT) & 0xF;

	if (convert_dytc_to_profile(perfmode, &profile))
		return;

	if (profile != priv->dytc->current_profile) {
		priv->dytc->current_profile = profile;
		platform_profile_notify();
	}
}

static int ideapad_dytc_profile_init(struct ideapad_private *priv)
{
	int err, dytc_version;
	unsigned long output;

	if (!priv->features.dytc)
		return -ENODEV;

	err = eval_dytc(priv->adev->handle, DYTC_CMD_QUERY, &output);
	/* For all other errors we can flag the failure */
	if (err)
		return err;

	/* Check DYTC is enabled and supports mode setting */
	if (!test_bit(DYTC_QUERY_ENABLE_BIT, &output))
		return -ENODEV;

	dytc_version = (output >> DYTC_QUERY_REV_BIT) & 0xF;
	if (dytc_version < 5)
		return -ENODEV;

	priv->dytc = kzalloc(sizeof(*priv->dytc), GFP_KERNEL);
	if (!priv->dytc)
		return -ENOMEM;

	mutex_init(&priv->dytc->mutex);

	priv->dytc->priv = priv;
	priv->dytc->pprof.profile_get = dytc_profile_get;
	priv->dytc->pprof.profile_set = dytc_profile_set;

	/* Setup supported modes */
	set_bit(PLATFORM_PROFILE_LOW_POWER, priv->dytc->pprof.choices);
	set_bit(PLATFORM_PROFILE_BALANCED, priv->dytc->pprof.choices);
	set_bit(PLATFORM_PROFILE_PERFORMANCE, priv->dytc->pprof.choices);

	/* Create platform_profile structure and register */
	err = platform_profile_register(&priv->dytc->pprof);
	if (err)
		goto pp_reg_failed;

	/* Ensure initial values are correct */
	dytc_profile_refresh(priv);

	return 0;

pp_reg_failed:
	mutex_destroy(&priv->dytc->mutex);
	kfree(priv->dytc);
	priv->dytc = NULL;

	return err;
}

static void ideapad_dytc_profile_exit(struct ideapad_private *priv)
{
	if (!priv->dytc)
		return;

	platform_profile_remove();
	mutex_destroy(&priv->dytc->mutex);
	kfree(priv->dytc);

	priv->dytc = NULL;
}

/*
 * Rfkill
 */
struct ideapad_rfk_data {
	char *name;
	int cfgbit;
	int opcode;
	int type;
};

static const struct ideapad_rfk_data ideapad_rfk_data[] = {
	{ "ideapad_wlan",      CFG_CAP_WIFI_BIT, VPCCMD_W_WIFI, RFKILL_TYPE_WLAN },
	{ "ideapad_bluetooth", CFG_CAP_BT_BIT,   VPCCMD_W_BT,   RFKILL_TYPE_BLUETOOTH },
	{ "ideapad_3g",        CFG_CAP_3G_BIT,   VPCCMD_W_3G,   RFKILL_TYPE_WWAN },
};

static int ideapad_rfk_set(void *data, bool blocked)
{
	struct ideapad_rfk_priv *priv = data;
	int opcode = ideapad_rfk_data[priv->dev].opcode;

	return write_ec_cmd(priv->priv->adev->handle, opcode, !blocked);
}

static const struct rfkill_ops ideapad_rfk_ops = {
	.set_block = ideapad_rfk_set,
};

static void ideapad_sync_rfk_state(struct ideapad_private *priv)
{
	unsigned long hw_blocked = 0;
	int i;

	if (priv->features.hw_rfkill_switch) {
		if (read_ec_data(priv->adev->handle, VPCCMD_R_RF, &hw_blocked))
			return;
		hw_blocked = !hw_blocked;
	}

	for (i = 0; i < IDEAPAD_RFKILL_DEV_NUM; i++)
		if (priv->rfk[i])
			rfkill_set_hw_state(priv->rfk[i], hw_blocked);
}

static int ideapad_register_rfkill(struct ideapad_private *priv, int dev)
{
	unsigned long rf_enabled;
	int err;

	if (no_bt_rfkill && ideapad_rfk_data[dev].type == RFKILL_TYPE_BLUETOOTH) {
		/* Force to enable bluetooth when no_bt_rfkill=1 */
		write_ec_cmd(priv->adev->handle, ideapad_rfk_data[dev].opcode, 1);
		return 0;
	}

	priv->rfk_priv[dev].dev = dev;
	priv->rfk_priv[dev].priv = priv;

	priv->rfk[dev] = rfkill_alloc(ideapad_rfk_data[dev].name,
				      &priv->platform_device->dev,
				      ideapad_rfk_data[dev].type,
				      &ideapad_rfk_ops,
				      &priv->rfk_priv[dev]);
	if (!priv->rfk[dev])
		return -ENOMEM;

	err = read_ec_data(priv->adev->handle, ideapad_rfk_data[dev].opcode - 1, &rf_enabled);
	if (err)
		rf_enabled = 1;

	rfkill_init_sw_state(priv->rfk[dev], !rf_enabled);

	err = rfkill_register(priv->rfk[dev]);
	if (err)
		rfkill_destroy(priv->rfk[dev]);

	return err;
}

static void ideapad_unregister_rfkill(struct ideapad_private *priv, int dev)
{
	if (!priv->rfk[dev])
		return;

	rfkill_unregister(priv->rfk[dev]);
	rfkill_destroy(priv->rfk[dev]);
}

/*
 * Platform device
 */
static int ideapad_sysfs_init(struct ideapad_private *priv)
{
	return device_add_group(&priv->platform_device->dev,
				&ideapad_attribute_group);
}

static void ideapad_sysfs_exit(struct ideapad_private *priv)
{
	device_remove_group(&priv->platform_device->dev,
			    &ideapad_attribute_group);
}

/*
 * input device
 */
static const struct key_entry ideapad_keymap[] = {
	{ KE_KEY,   6, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY,   7, { KEY_CAMERA } },
	{ KE_KEY,   8, { KEY_MICMUTE } },
	{ KE_KEY,  11, { KEY_F16 } },
	{ KE_KEY,  13, { KEY_WLAN } },
	{ KE_KEY,  16, { KEY_PROG1 } },
	{ KE_KEY,  17, { KEY_PROG2 } },
	{ KE_KEY,  64, { KEY_PROG3 } },
	{ KE_KEY,  65, { KEY_PROG4 } },
	{ KE_KEY,  66, { KEY_TOUCHPAD_OFF } },
	{ KE_KEY,  67, { KEY_TOUCHPAD_ON } },
	{ KE_KEY, 128, { KEY_ESC } },
	{ KE_END },
};

static int ideapad_input_init(struct ideapad_private *priv)
{
	struct input_dev *inputdev;
	int err;

	inputdev = input_allocate_device();
	if (!inputdev)
		return -ENOMEM;

	inputdev->name = "Ideapad extra buttons";
	inputdev->phys = "ideapad/input0";
	inputdev->id.bustype = BUS_HOST;
	inputdev->dev.parent = &priv->platform_device->dev;

	err = sparse_keymap_setup(inputdev, ideapad_keymap, NULL);
	if (err) {
		dev_err(&priv->platform_device->dev,
			"Could not set up input device keymap: %d\n", err);
		goto err_free_dev;
	}

	err = input_register_device(inputdev);
	if (err) {
		dev_err(&priv->platform_device->dev,
			"Could not register input device: %d\n", err);
		goto err_free_dev;
	}

	priv->inputdev = inputdev;

	return 0;

err_free_dev:
	input_free_device(inputdev);

	return err;
}

static void ideapad_input_exit(struct ideapad_private *priv)
{
	input_unregister_device(priv->inputdev);
	priv->inputdev = NULL;
}

static void ideapad_input_report(struct ideapad_private *priv,
				 unsigned long scancode)
{
	sparse_keymap_report_event(priv->inputdev, scancode, 1, true);
}

static void ideapad_input_novokey(struct ideapad_private *priv)
{
	unsigned long long_pressed;

	if (read_ec_data(priv->adev->handle, VPCCMD_R_NOVO, &long_pressed))
		return;

	if (long_pressed)
		ideapad_input_report(priv, 17);
	else
		ideapad_input_report(priv, 16);
}

static void ideapad_check_special_buttons(struct ideapad_private *priv)
{
	unsigned long bit, value;

	if (read_ec_data(priv->adev->handle, VPCCMD_R_SPECIAL_BUTTONS, &value))
		return;

	for_each_set_bit (bit, &value, 16) {
		switch (bit) {
		case 6:	/* Z570 */
		case 0:	/* Z580 */
			/* Thermal Management button */
			ideapad_input_report(priv, 65);
			break;
		case 1:
			/* OneKey Theater button */
			ideapad_input_report(priv, 64);
			break;
		default:
			dev_info(&priv->platform_device->dev,
				 "Unknown special button: %lu\n", bit);
			break;
		}
	}
}

/*
 * backlight
 */
static int ideapad_backlight_get_brightness(struct backlight_device *blightdev)
{
	struct ideapad_private *priv = bl_get_data(blightdev);
	unsigned long now;
	int err;

	err = read_ec_data(priv->adev->handle, VPCCMD_R_BL, &now);
	if (err)
		return err;

	return now;
}

static int ideapad_backlight_update_status(struct backlight_device *blightdev)
{
	struct ideapad_private *priv = bl_get_data(blightdev);
	int err;

	err = write_ec_cmd(priv->adev->handle, VPCCMD_W_BL,
			   blightdev->props.brightness);
	if (err)
		return err;

	err = write_ec_cmd(priv->adev->handle, VPCCMD_W_BL_POWER,
			   blightdev->props.power != FB_BLANK_POWERDOWN);
	if (err)
		return err;

	return 0;
}

static const struct backlight_ops ideapad_backlight_ops = {
	.get_brightness = ideapad_backlight_get_brightness,
	.update_status = ideapad_backlight_update_status,
};

static int ideapad_backlight_init(struct ideapad_private *priv)
{
	struct backlight_device *blightdev;
	struct backlight_properties props;
	unsigned long max, now, power;
	int err;

	err = read_ec_data(priv->adev->handle, VPCCMD_R_BL_MAX, &max);
	if (err)
		return err;

	err = read_ec_data(priv->adev->handle, VPCCMD_R_BL, &now);
	if (err)
		return err;

	err = read_ec_data(priv->adev->handle, VPCCMD_R_BL_POWER, &power);
	if (err)
		return err;

	memset(&props, 0, sizeof(props));

	props.max_brightness = max;
	props.type = BACKLIGHT_PLATFORM;

	blightdev = backlight_device_register("ideapad",
					      &priv->platform_device->dev,
					      priv,
					      &ideapad_backlight_ops,
					      &props);
	if (IS_ERR(blightdev)) {
		err = PTR_ERR(blightdev);
		dev_err(&priv->platform_device->dev,
			"Could not register backlight device: %d\n", err);
		return err;
	}

	priv->blightdev = blightdev;
	blightdev->props.brightness = now;
	blightdev->props.power = power ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN;

	backlight_update_status(blightdev);

	return 0;
}

static void ideapad_backlight_exit(struct ideapad_private *priv)
{
	backlight_device_unregister(priv->blightdev);
	priv->blightdev = NULL;
}

static void ideapad_backlight_notify_power(struct ideapad_private *priv)
{
	struct backlight_device *blightdev = priv->blightdev;
	unsigned long power;

	if (!blightdev)
		return;

	if (read_ec_data(priv->adev->handle, VPCCMD_R_BL_POWER, &power))
		return;

	blightdev->props.power = power ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN;
}

static void ideapad_backlight_notify_brightness(struct ideapad_private *priv)
{
	unsigned long now;

	/* if we control brightness via acpi video driver */
	if (!priv->blightdev)
		read_ec_data(priv->adev->handle, VPCCMD_R_BL, &now);
	else
		backlight_force_update(priv->blightdev, BACKLIGHT_UPDATE_HOTKEY);
}

/*
 * keyboard backlight
 */
static int ideapad_kbd_bl_brightness_get(struct ideapad_private *priv)
{
	unsigned long hals;
	int err;

	err = eval_hals(priv->adev->handle, &hals);
	if (err)
		return err;

	return !!test_bit(HALS_KBD_BL_STATE_BIT, &hals);
}

static enum led_brightness ideapad_kbd_bl_led_cdev_brightness_get(struct led_classdev *led_cdev)
{
	struct ideapad_private *priv = container_of(led_cdev, struct ideapad_private, kbd_bl.led);

	return ideapad_kbd_bl_brightness_get(priv);
}

static int ideapad_kbd_bl_brightness_set(struct ideapad_private *priv, unsigned int brightness)
{
	int err = exec_sals(priv->adev->handle, brightness ? SALS_KBD_BL_ON : SALS_KBD_BL_OFF);

	if (err)
		return err;

	priv->kbd_bl.last_brightness = brightness;

	return 0;
}

static int ideapad_kbd_bl_led_cdev_brightness_set(struct led_classdev *led_cdev,
						  enum led_brightness brightness)
{
	struct ideapad_private *priv = container_of(led_cdev, struct ideapad_private, kbd_bl.led);

	return ideapad_kbd_bl_brightness_set(priv, brightness);
}

static void ideapad_kbd_bl_notify(struct ideapad_private *priv)
{
	int brightness;

	if (!priv->kbd_bl.initialized)
		return;

	brightness = ideapad_kbd_bl_brightness_get(priv);
	if (brightness < 0)
		return;

	if (brightness == priv->kbd_bl.last_brightness)
		return;

	priv->kbd_bl.last_brightness = brightness;

	led_classdev_notify_brightness_hw_changed(&priv->kbd_bl.led, brightness);
}

static int ideapad_kbd_bl_init(struct ideapad_private *priv)
{
	int brightness, err;

	if (!priv->features.kbd_bl)
		return -ENODEV;

	if (WARN_ON(priv->kbd_bl.initialized))
		return -EEXIST;

	brightness = ideapad_kbd_bl_brightness_get(priv);
	if (brightness < 0)
		return brightness;

	priv->kbd_bl.last_brightness = brightness;

	priv->kbd_bl.led.name                    = "platform::" LED_FUNCTION_KBD_BACKLIGHT;
	priv->kbd_bl.led.max_brightness          = 1;
	priv->kbd_bl.led.brightness_get          = ideapad_kbd_bl_led_cdev_brightness_get;
	priv->kbd_bl.led.brightness_set_blocking = ideapad_kbd_bl_led_cdev_brightness_set;
	priv->kbd_bl.led.flags                   = LED_BRIGHT_HW_CHANGED;

	err = led_classdev_register(&priv->platform_device->dev, &priv->kbd_bl.led);
	if (err)
		return err;

	priv->kbd_bl.initialized = true;

	return 0;
}

static void ideapad_kbd_bl_exit(struct ideapad_private *priv)
{
	if (!priv->kbd_bl.initialized)
		return;

	priv->kbd_bl.initialized = false;

	led_classdev_unregister(&priv->kbd_bl.led);
}

/*
 * module init/exit
 */
static void ideapad_sync_touchpad_state(struct ideapad_private *priv)
{
	unsigned long value;

	if (!priv->features.touchpad_ctrl_via_ec)
		return;

	/* Without reading from EC touchpad LED doesn't switch state */
	if (!read_ec_data(priv->adev->handle, VPCCMD_R_TOUCHPAD, &value)) {
		unsigned char param;
		/*
		 * Some IdeaPads don't really turn off touchpad - they only
		 * switch the LED state. We (de)activate KBC AUX port to turn
		 * touchpad off and on. We send KEY_TOUCHPAD_OFF and
		 * KEY_TOUCHPAD_ON to not to get out of sync with LED
		 */
		i8042_command(&param, value ? I8042_CMD_AUX_ENABLE : I8042_CMD_AUX_DISABLE);
		ideapad_input_report(priv, value ? 67 : 66);
		sysfs_notify(&priv->platform_device->dev.kobj, NULL, "touchpad");
	}
}

static void ideapad_acpi_notify(acpi_handle handle, u32 event, void *data)
{
	struct ideapad_private *priv = data;
	unsigned long vpc1, vpc2, bit;

	if (read_ec_data(handle, VPCCMD_R_VPC1, &vpc1))
		return;

	if (read_ec_data(handle, VPCCMD_R_VPC2, &vpc2))
		return;

	vpc1 = (vpc2 << 8) | vpc1;

	for_each_set_bit (bit, &vpc1, 16) {
		switch (bit) {
		case 13:
		case 11:
		case 8:
		case 7:
		case 6:
			ideapad_input_report(priv, bit);
			break;
		case 10:
			/*
			 * This event gets send on a Yoga 300-11IBR when the EC
			 * believes that the device has changed between laptop/
			 * tent/stand/tablet mode. The EC relies on getting
			 * angle info from 2 accelerometers through a special
			 * windows service calling a DSM on the DUAL250E ACPI-
			 * device. Linux does not do this, making the laptop/
			 * tent/stand/tablet mode info unreliable, so we simply
			 * ignore these events.
			 */
			break;
		case 9:
			ideapad_sync_rfk_state(priv);
			break;
		case 5:
			ideapad_sync_touchpad_state(priv);
			break;
		case 4:
			ideapad_backlight_notify_brightness(priv);
			break;
		case 3:
			ideapad_input_novokey(priv);
			break;
		case 2:
			ideapad_backlight_notify_power(priv);
			break;
		case 1:
			/*
			 * Some IdeaPads report event 1 every ~20
			 * seconds while on battery power; some
			 * report this when changing to/from tablet
			 * mode; some report this when the keyboard
			 * backlight has changed.
			 */
			ideapad_kbd_bl_notify(priv);
			break;
		case 0:
			ideapad_check_special_buttons(priv);
			break;
		default:
			dev_info(&priv->platform_device->dev,
				 "Unknown event: %lu\n", bit);
		}
	}
}

#if IS_ENABLED(CONFIG_ACPI_WMI)
static void ideapad_wmi_notify(u32 value, void *context)
{
	struct ideapad_private *priv = context;

	switch (value) {
	case 128:
		ideapad_input_report(priv, value);
		break;
	default:
		dev_info(&priv->platform_device->dev,
			 "Unknown WMI event: %u\n", value);
	}
}
#endif

/*
 * Some ideapads have a hardware rfkill switch, but most do not have one.
 * Reading VPCCMD_R_RF always results in 0 on models without a hardware rfkill,
 * switch causing ideapad_laptop to wrongly report all radios as hw-blocked.
 * There used to be a long list of DMI ids for models without a hw rfkill
 * switch here, but that resulted in playing whack a mole.
 * More importantly wrongly reporting the wifi radio as hw-blocked, results in
 * non working wifi. Whereas not reporting it hw-blocked, when it actually is
 * hw-blocked results in an empty SSID list, which is a much more benign
 * failure mode.
 * So the default now is the much safer option of assuming there is no
 * hardware rfkill switch. This default also actually matches most hardware,
 * since having a hw rfkill switch is quite rare on modern hardware, so this
 * also leads to a much shorter list.
 */
static const struct dmi_system_id hw_rfkill_list[] = {
	{}
};

static void ideapad_check_features(struct ideapad_private *priv)
{
	acpi_handle handle = priv->adev->handle;
	unsigned long val;

	priv->features.hw_rfkill_switch = dmi_check_system(hw_rfkill_list);

	/* Most ideapads with ELAN0634 touchpad don't use EC touchpad switch */
	priv->features.touchpad_ctrl_via_ec = !acpi_dev_present("ELAN0634", NULL, -1);

	if (!read_ec_data(handle, VPCCMD_R_FAN, &val))
		priv->features.fan_mode = true;

	if (acpi_has_method(handle, "GBMD") && acpi_has_method(handle, "SBMC"))
		priv->features.conservation_mode = true;

	if (acpi_has_method(handle, "DYTC"))
		priv->features.dytc = true;

	if (acpi_has_method(handle, "HALS") && acpi_has_method(handle, "SALS")) {
		if (!eval_hals(handle, &val)) {
			if (test_bit(HALS_FNLOCK_SUPPORT_BIT, &val))
				priv->features.fn_lock = true;

			if (test_bit(HALS_KBD_BL_SUPPORT_BIT, &val))
				priv->features.kbd_bl = true;

			if (test_bit(HALS_USB_CHARGING_SUPPORT_BIT, &val))
				priv->features.usb_charging = true;
		}
	}
}

static int ideapad_acpi_add(struct platform_device *pdev)
{
	struct ideapad_private *priv;
	struct acpi_device *adev;
	acpi_status status;
	unsigned long cfg;
	int err, i;

	err = acpi_bus_get_device(ACPI_HANDLE(&pdev->dev), &adev);
	if (err)
		return -ENODEV;

	if (eval_int(adev->handle, "_CFG", &cfg))
		return -ENODEV;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, priv);

	priv->cfg = cfg;
	priv->adev = adev;
	priv->platform_device = pdev;

	ideapad_check_features(priv);

	err = ideapad_sysfs_init(priv);
	if (err)
		return err;

	ideapad_debugfs_init(priv);

	err = ideapad_input_init(priv);
	if (err)
		goto input_failed;

	err = ideapad_kbd_bl_init(priv);
	if (err) {
		if (err != -ENODEV)
			dev_warn(&pdev->dev, "Could not set up keyboard backlight LED: %d\n", err);
		else
			dev_info(&pdev->dev, "Keyboard backlight control not available\n");
	}

	/*
	 * On some models without a hw-switch (the yoga 2 13 at least)
	 * VPCCMD_W_RF must be explicitly set to 1 for the wifi to work.
	 */
	if (!priv->features.hw_rfkill_switch)
		write_ec_cmd(priv->adev->handle, VPCCMD_W_RF, 1);

	/* The same for Touchpad */
	if (!priv->features.touchpad_ctrl_via_ec)
		write_ec_cmd(priv->adev->handle, VPCCMD_W_TOUCHPAD, 1);

	for (i = 0; i < IDEAPAD_RFKILL_DEV_NUM; i++)
		if (test_bit(ideapad_rfk_data[i].cfgbit, &priv->cfg))
			ideapad_register_rfkill(priv, i);

	ideapad_sync_rfk_state(priv);
	ideapad_sync_touchpad_state(priv);

	err = ideapad_dytc_profile_init(priv);
	if (err) {
		if (err != -ENODEV)
			dev_warn(&pdev->dev, "Could not set up DYTC interface: %d\n", err);
		else
			dev_info(&pdev->dev, "DYTC interface is not available\n");
	}

	if (acpi_video_get_backlight_type() == acpi_backlight_vendor) {
		err = ideapad_backlight_init(priv);
		if (err && err != -ENODEV)
			goto backlight_failed;
	}

	status = acpi_install_notify_handler(adev->handle,
					     ACPI_DEVICE_NOTIFY,
					     ideapad_acpi_notify, priv);
	if (ACPI_FAILURE(status)) {
		err = -EIO;
		goto notification_failed;
	}

#if IS_ENABLED(CONFIG_ACPI_WMI)
	for (i = 0; i < ARRAY_SIZE(ideapad_wmi_fnesc_events); i++) {
		status = wmi_install_notify_handler(ideapad_wmi_fnesc_events[i],
						    ideapad_wmi_notify, priv);
		if (ACPI_SUCCESS(status)) {
			priv->fnesc_guid = ideapad_wmi_fnesc_events[i];
			break;
		}
	}

	if (ACPI_FAILURE(status) && status != AE_NOT_EXIST) {
		err = -EIO;
		goto notification_failed_wmi;
	}
#endif

	return 0;

#if IS_ENABLED(CONFIG_ACPI_WMI)
notification_failed_wmi:
	acpi_remove_notify_handler(priv->adev->handle,
				   ACPI_DEVICE_NOTIFY,
				   ideapad_acpi_notify);
#endif

notification_failed:
	ideapad_backlight_exit(priv);

backlight_failed:
	ideapad_dytc_profile_exit(priv);

	for (i = 0; i < IDEAPAD_RFKILL_DEV_NUM; i++)
		ideapad_unregister_rfkill(priv, i);

	ideapad_kbd_bl_exit(priv);
	ideapad_input_exit(priv);

input_failed:
	ideapad_debugfs_exit(priv);
	ideapad_sysfs_exit(priv);

	return err;
}

static int ideapad_acpi_remove(struct platform_device *pdev)
{
	struct ideapad_private *priv = dev_get_drvdata(&pdev->dev);
	int i;

#if IS_ENABLED(CONFIG_ACPI_WMI)
	if (priv->fnesc_guid)
		wmi_remove_notify_handler(priv->fnesc_guid);
#endif

	acpi_remove_notify_handler(priv->adev->handle,
				   ACPI_DEVICE_NOTIFY,
				   ideapad_acpi_notify);

	ideapad_backlight_exit(priv);
	ideapad_dytc_profile_exit(priv);

	for (i = 0; i < IDEAPAD_RFKILL_DEV_NUM; i++)
		ideapad_unregister_rfkill(priv, i);

	ideapad_kbd_bl_exit(priv);
	ideapad_input_exit(priv);
	ideapad_debugfs_exit(priv);
	ideapad_sysfs_exit(priv);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ideapad_acpi_resume(struct device *dev)
{
	struct ideapad_private *priv = dev_get_drvdata(dev);

	ideapad_sync_rfk_state(priv);
	ideapad_sync_touchpad_state(priv);

	if (priv->dytc)
		dytc_profile_refresh(priv);

	return 0;
}
#endif
static SIMPLE_DEV_PM_OPS(ideapad_pm, NULL, ideapad_acpi_resume);

static const struct acpi_device_id ideapad_device_ids[] = {
	{"VPC2004", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, ideapad_device_ids);

static struct platform_driver ideapad_acpi_driver = {
	.probe = ideapad_acpi_add,
	.remove = ideapad_acpi_remove,
	.driver = {
		.name   = "ideapad_acpi",
		.pm     = &ideapad_pm,
		.acpi_match_table = ACPI_PTR(ideapad_device_ids),
	},
};

module_platform_driver(ideapad_acpi_driver);

MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("IdeaPad ACPI Extras");
MODULE_LICENSE("GPL");
