/*-*-linux-c-*-*/

/*
  Copyright (C) 2006 Lennart Poettering <mzxreary (at) 0pointer (dot) de>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.
 */

/*
 * msi-laptop.c - MSI S270 laptop support. This laptop is sold under
 * various brands, including "Cytron/TCM/Medion/Tchibo MD96100".
 *
 * Driver also supports S271, S420 models.
 *
 * This driver exports a few files in /sys/devices/platform/msi-laptop-pf/:
 *
 *   lcd_level - Screen brightness: contains a single integer in the
 *   range 0..8. (rw)
 *
 *   auto_brightness - Enable automatic brightness control: contains
 *   either 0 or 1. If set to 1 the hardware adjusts the screen
 *   brightness automatically when the power cord is
 *   plugged/unplugged. (rw)
 *
 *   wlan - WLAN subsystem enabled: contains either 0 or 1. (ro)
 *
 *   bluetooth - Bluetooth subsystem enabled: contains either 0 or 1
 *   Please note that this file is constantly 0 if no Bluetooth
 *   hardware is available. (ro)
 *
 * In addition to these platform device attributes the driver
 * registers itself in the Linux backlight control subsystem and is
 * available to userspace under /sys/class/backlight/msi-laptop-bl/.
 *
 * This driver might work on other laptops produced by MSI. If you
 * want to try it you can pass force=1 as argument to the module which
 * will force it to load even when the DMI data doesn't identify the
 * laptop as MSI S270. YMMV.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>
#include <linux/i8042.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>

#define MSI_DRIVER_VERSION "0.5"

#define MSI_LCD_LEVEL_MAX 9

#define MSI_EC_COMMAND_WIRELESS 0x10
#define MSI_EC_COMMAND_LCD_LEVEL 0x11

#define MSI_STANDARD_EC_COMMAND_ADDRESS	0x2e
#define MSI_STANDARD_EC_BLUETOOTH_MASK	(1 << 0)
#define MSI_STANDARD_EC_WEBCAM_MASK	(1 << 1)
#define MSI_STANDARD_EC_WLAN_MASK	(1 << 3)
#define MSI_STANDARD_EC_3G_MASK		(1 << 4)

/* For set SCM load flag to disable BIOS fn key */
#define MSI_STANDARD_EC_SCM_LOAD_ADDRESS	0x2d
#define MSI_STANDARD_EC_SCM_LOAD_MASK		(1 << 0)

#define MSI_STANDARD_EC_FUNCTIONS_ADDRESS	0xe4
/* Power LED is orange - Turbo mode */
#define MSI_STANDARD_EC_TURBO_MASK		(1 << 1)
/* Power LED is green - ECO mode */
#define MSI_STANDARD_EC_ECO_MASK		(1 << 3)
/* Touchpad is turned on */
#define MSI_STANDARD_EC_TOUCHPAD_MASK		(1 << 4)
/* If this bit != bit 1, turbo mode can't be toggled */
#define MSI_STANDARD_EC_TURBO_COOLDOWN_MASK	(1 << 7)

#define MSI_STANDARD_EC_FAN_ADDRESS		0x33
/* If zero, fan rotates at maximal speed */
#define MSI_STANDARD_EC_AUTOFAN_MASK		(1 << 0)

#ifdef CONFIG_PM_SLEEP
static int msi_laptop_resume(struct device *device);
#endif
static SIMPLE_DEV_PM_OPS(msi_laptop_pm, NULL, msi_laptop_resume);

#define MSI_STANDARD_EC_DEVICES_EXISTS_ADDRESS	0x2f

static bool force;
module_param(force, bool, 0);
MODULE_PARM_DESC(force, "Force driver load, ignore DMI data");

static int auto_brightness;
module_param(auto_brightness, int, 0);
MODULE_PARM_DESC(auto_brightness, "Enable automatic brightness control (0: disabled; 1: enabled; 2: don't touch)");

static const struct key_entry msi_laptop_keymap[] = {
	{KE_KEY, KEY_TOUCHPAD_ON, {KEY_TOUCHPAD_ON} },	/* Touch Pad On */
	{KE_KEY, KEY_TOUCHPAD_OFF, {KEY_TOUCHPAD_OFF} },/* Touch Pad On */
	{KE_END, 0}
};

static struct input_dev *msi_laptop_input_dev;

static int wlan_s, bluetooth_s, threeg_s;
static int threeg_exists;
static struct rfkill *rfk_wlan, *rfk_bluetooth, *rfk_threeg;

/* MSI laptop quirks */
struct quirk_entry {
	bool old_ec_model;

	/* Some MSI 3G netbook only have one fn key to control
	 * Wlan/Bluetooth/3G, those netbook will load the SCM (windows app) to
	 * disable the original Wlan/Bluetooth control by BIOS when user press
	 * fn key, then control Wlan/Bluetooth/3G by SCM (software control by
	 * OS). Without SCM, user cann't on/off 3G module on those 3G netbook.
	 * On Linux, msi-laptop driver will do the same thing to disable the
	 * original BIOS control, then might need use HAL or other userland
	 * application to do the software control that simulate with SCM.
	 * e.g. MSI N034 netbook
	 */
	bool load_scm_model;

	/* Some MSI laptops need delay before reading from EC */
	bool ec_delay;

	/* Some MSI Wind netbooks (e.g. MSI Wind U100) need loading SCM to get
	 * some features working (e.g. ECO mode), but we cannot change
	 * Wlan/Bluetooth state in software and we can only read its state.
	 */
	bool ec_read_only;
};

static struct quirk_entry *quirks;

/* Hardware access */

static int set_lcd_level(int level)
{
	u8 buf[2];

	if (level < 0 || level >= MSI_LCD_LEVEL_MAX)
		return -EINVAL;

	buf[0] = 0x80;
	buf[1] = (u8) (level*31);

	return ec_transaction(MSI_EC_COMMAND_LCD_LEVEL, buf, sizeof(buf),
			      NULL, 0);
}

static int get_lcd_level(void)
{
	u8 wdata = 0, rdata;
	int result;

	result = ec_transaction(MSI_EC_COMMAND_LCD_LEVEL, &wdata, 1,
				&rdata, 1);
	if (result < 0)
		return result;

	return (int) rdata / 31;
}

static int get_auto_brightness(void)
{
	u8 wdata = 4, rdata;
	int result;

	result = ec_transaction(MSI_EC_COMMAND_LCD_LEVEL, &wdata, 1,
				&rdata, 1);
	if (result < 0)
		return result;

	return !!(rdata & 8);
}

static int set_auto_brightness(int enable)
{
	u8 wdata[2], rdata;
	int result;

	wdata[0] = 4;

	result = ec_transaction(MSI_EC_COMMAND_LCD_LEVEL, wdata, 1,
				&rdata, 1);
	if (result < 0)
		return result;

	wdata[0] = 0x84;
	wdata[1] = (rdata & 0xF7) | (enable ? 8 : 0);

	return ec_transaction(MSI_EC_COMMAND_LCD_LEVEL, wdata, 2,
			      NULL, 0);
}

static ssize_t set_device_state(const char *buf, size_t count, u8 mask)
{
	int status;
	u8 wdata = 0, rdata;
	int result;

	if (sscanf(buf, "%i", &status) != 1 || (status < 0 || status > 1))
		return -EINVAL;

	if (quirks->ec_read_only)
		return -EOPNOTSUPP;

	/* read current device state */
	result = ec_read(MSI_STANDARD_EC_COMMAND_ADDRESS, &rdata);
	if (result < 0)
		return result;

	if (!!(rdata & mask) != status) {
		/* reverse device bit */
		if (rdata & mask)
			wdata = rdata & ~mask;
		else
			wdata = rdata | mask;

		result = ec_write(MSI_STANDARD_EC_COMMAND_ADDRESS, wdata);
		if (result < 0)
			return result;
	}

	return count;
}

static int get_wireless_state(int *wlan, int *bluetooth)
{
	u8 wdata = 0, rdata;
	int result;

	result = ec_transaction(MSI_EC_COMMAND_WIRELESS, &wdata, 1, &rdata, 1);
	if (result < 0)
		return result;

	if (wlan)
		*wlan = !!(rdata & 8);

	if (bluetooth)
		*bluetooth = !!(rdata & 128);

	return 0;
}

static int get_wireless_state_ec_standard(void)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_STANDARD_EC_COMMAND_ADDRESS, &rdata);
	if (result < 0)
		return result;

	wlan_s = !!(rdata & MSI_STANDARD_EC_WLAN_MASK);

	bluetooth_s = !!(rdata & MSI_STANDARD_EC_BLUETOOTH_MASK);

	threeg_s = !!(rdata & MSI_STANDARD_EC_3G_MASK);

	return 0;
}

static int get_threeg_exists(void)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_STANDARD_EC_DEVICES_EXISTS_ADDRESS, &rdata);
	if (result < 0)
		return result;

	threeg_exists = !!(rdata & MSI_STANDARD_EC_3G_MASK);

	return 0;
}

/* Backlight device stuff */

static int bl_get_brightness(struct backlight_device *b)
{
	return get_lcd_level();
}


static int bl_update_status(struct backlight_device *b)
{
	return set_lcd_level(b->props.brightness);
}

static const struct backlight_ops msibl_ops = {
	.get_brightness = bl_get_brightness,
	.update_status  = bl_update_status,
};

static struct backlight_device *msibl_device;

/* Platform device */

static ssize_t show_wlan(struct device *dev,
	struct device_attribute *attr, char *buf)
{

	int ret, enabled = 0;

	if (quirks->old_ec_model) {
		ret = get_wireless_state(&enabled, NULL);
	} else {
		ret = get_wireless_state_ec_standard();
		enabled = wlan_s;
	}
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", enabled);
}

static ssize_t store_wlan(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return set_device_state(buf, count, MSI_STANDARD_EC_WLAN_MASK);
}

static ssize_t show_bluetooth(struct device *dev,
	struct device_attribute *attr, char *buf)
{

	int ret, enabled = 0;

	if (quirks->old_ec_model) {
		ret = get_wireless_state(NULL, &enabled);
	} else {
		ret = get_wireless_state_ec_standard();
		enabled = bluetooth_s;
	}
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", enabled);
}

static ssize_t store_bluetooth(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return set_device_state(buf, count, MSI_STANDARD_EC_BLUETOOTH_MASK);
}

static ssize_t show_threeg(struct device *dev,
	struct device_attribute *attr, char *buf)
{

	int ret;

	/* old msi ec not support 3G */
	if (quirks->old_ec_model)
		return -ENODEV;

	ret = get_wireless_state_ec_standard();
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", threeg_s);
}

static ssize_t store_threeg(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return set_device_state(buf, count, MSI_STANDARD_EC_3G_MASK);
}

static ssize_t show_lcd_level(struct device *dev,
	struct device_attribute *attr, char *buf)
{

	int ret;

	ret = get_lcd_level();
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t store_lcd_level(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{

	int level, ret;

	if (sscanf(buf, "%i", &level) != 1 ||
	    (level < 0 || level >= MSI_LCD_LEVEL_MAX))
		return -EINVAL;

	ret = set_lcd_level(level);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t show_auto_brightness(struct device *dev,
	struct device_attribute *attr, char *buf)
{

	int ret;

	ret = get_auto_brightness();
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t store_auto_brightness(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{

	int enable, ret;

	if (sscanf(buf, "%i", &enable) != 1 || (enable != (enable & 1)))
		return -EINVAL;

	ret = set_auto_brightness(enable);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t show_touchpad(struct device *dev,
	struct device_attribute *attr, char *buf)
{

	u8 rdata;
	int result;

	result = ec_read(MSI_STANDARD_EC_FUNCTIONS_ADDRESS, &rdata);
	if (result < 0)
		return result;

	return sprintf(buf, "%i\n", !!(rdata & MSI_STANDARD_EC_TOUCHPAD_MASK));
}

static ssize_t show_turbo(struct device *dev,
	struct device_attribute *attr, char *buf)
{

	u8 rdata;
	int result;

	result = ec_read(MSI_STANDARD_EC_FUNCTIONS_ADDRESS, &rdata);
	if (result < 0)
		return result;

	return sprintf(buf, "%i\n", !!(rdata & MSI_STANDARD_EC_TURBO_MASK));
}

static ssize_t show_eco(struct device *dev,
	struct device_attribute *attr, char *buf)
{

	u8 rdata;
	int result;

	result = ec_read(MSI_STANDARD_EC_FUNCTIONS_ADDRESS, &rdata);
	if (result < 0)
		return result;

	return sprintf(buf, "%i\n", !!(rdata & MSI_STANDARD_EC_ECO_MASK));
}

static ssize_t show_turbo_cooldown(struct device *dev,
	struct device_attribute *attr, char *buf)
{

	u8 rdata;
	int result;

	result = ec_read(MSI_STANDARD_EC_FUNCTIONS_ADDRESS, &rdata);
	if (result < 0)
		return result;

	return sprintf(buf, "%i\n", (!!(rdata & MSI_STANDARD_EC_TURBO_MASK)) |
		(!!(rdata & MSI_STANDARD_EC_TURBO_COOLDOWN_MASK) << 1));
}

static ssize_t show_auto_fan(struct device *dev,
	struct device_attribute *attr, char *buf)
{

	u8 rdata;
	int result;

	result = ec_read(MSI_STANDARD_EC_FAN_ADDRESS, &rdata);
	if (result < 0)
		return result;

	return sprintf(buf, "%i\n", !!(rdata & MSI_STANDARD_EC_AUTOFAN_MASK));
}

static ssize_t store_auto_fan(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{

	int enable, result;

	if (sscanf(buf, "%i", &enable) != 1 || (enable != (enable & 1)))
		return -EINVAL;

	result = ec_write(MSI_STANDARD_EC_FAN_ADDRESS, enable);
	if (result < 0)
		return result;

	return count;
}

static DEVICE_ATTR(lcd_level, 0644, show_lcd_level, store_lcd_level);
static DEVICE_ATTR(auto_brightness, 0644, show_auto_brightness,
		   store_auto_brightness);
static DEVICE_ATTR(bluetooth, 0444, show_bluetooth, NULL);
static DEVICE_ATTR(wlan, 0444, show_wlan, NULL);
static DEVICE_ATTR(threeg, 0444, show_threeg, NULL);
static DEVICE_ATTR(touchpad, 0444, show_touchpad, NULL);
static DEVICE_ATTR(turbo_mode, 0444, show_turbo, NULL);
static DEVICE_ATTR(eco_mode, 0444, show_eco, NULL);
static DEVICE_ATTR(turbo_cooldown, 0444, show_turbo_cooldown, NULL);
static DEVICE_ATTR(auto_fan, 0644, show_auto_fan, store_auto_fan);

static struct attribute *msipf_attributes[] = {
	&dev_attr_bluetooth.attr,
	&dev_attr_wlan.attr,
	&dev_attr_touchpad.attr,
	&dev_attr_turbo_mode.attr,
	&dev_attr_eco_mode.attr,
	&dev_attr_turbo_cooldown.attr,
	&dev_attr_auto_fan.attr,
	NULL
};

static struct attribute *msipf_old_attributes[] = {
	&dev_attr_lcd_level.attr,
	&dev_attr_auto_brightness.attr,
	NULL
};

static struct attribute_group msipf_attribute_group = {
	.attrs = msipf_attributes
};

static struct attribute_group msipf_old_attribute_group = {
	.attrs = msipf_old_attributes
};

static struct platform_driver msipf_driver = {
	.driver = {
		.name = "msi-laptop-pf",
		.owner = THIS_MODULE,
		.pm = &msi_laptop_pm,
	},
};

static struct platform_device *msipf_device;

/* Initialization */

static struct quirk_entry quirk_old_ec_model = {
	.old_ec_model = true,
};

static struct quirk_entry quirk_load_scm_model = {
	.load_scm_model = true,
	.ec_delay = true,
};

static struct quirk_entry quirk_load_scm_ro_model = {
	.load_scm_model = true,
	.ec_read_only = true,
};

static int dmi_check_cb(const struct dmi_system_id *dmi)
{
	pr_info("Identified laptop model '%s'\n", dmi->ident);

	quirks = dmi->driver_data;

	return 1;
}

static struct dmi_system_id __initdata msi_dmi_table[] = {
	{
		.ident = "MSI S270",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MICRO-STAR INT'L CO.,LTD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MS-1013"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "0131"),
			DMI_MATCH(DMI_CHASSIS_VENDOR,
				  "MICRO-STAR INT'L CO.,LTD")
		},
		.driver_data = &quirk_old_ec_model,
		.callback = dmi_check_cb
	},
	{
		.ident = "MSI S271",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star International"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MS-1058"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "0581"),
			DMI_MATCH(DMI_BOARD_NAME, "MS-1058")
		},
		.driver_data = &quirk_old_ec_model,
		.callback = dmi_check_cb
	},
	{
		.ident = "MSI S420",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star International"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MS-1412"),
			DMI_MATCH(DMI_BOARD_VENDOR, "MSI"),
			DMI_MATCH(DMI_BOARD_NAME, "MS-1412")
		},
		.driver_data = &quirk_old_ec_model,
		.callback = dmi_check_cb
	},
	{
		.ident = "Medion MD96100",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "NOTEBOOK"),
			DMI_MATCH(DMI_PRODUCT_NAME, "SAM2000"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "0131"),
			DMI_MATCH(DMI_CHASSIS_VENDOR,
				  "MICRO-STAR INT'L CO.,LTD")
		},
		.driver_data = &quirk_old_ec_model,
		.callback = dmi_check_cb
	},
	{
		.ident = "MSI N034",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
				"MICRO-STAR INTERNATIONAL CO., LTD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MS-N034"),
			DMI_MATCH(DMI_CHASSIS_VENDOR,
			"MICRO-STAR INTERNATIONAL CO., LTD")
		},
		.driver_data = &quirk_load_scm_model,
		.callback = dmi_check_cb
	},
	{
		.ident = "MSI N051",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
				"MICRO-STAR INTERNATIONAL CO., LTD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MS-N051"),
			DMI_MATCH(DMI_CHASSIS_VENDOR,
			"MICRO-STAR INTERNATIONAL CO., LTD")
		},
		.driver_data = &quirk_load_scm_model,
		.callback = dmi_check_cb
	},
	{
		.ident = "MSI N014",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
				"MICRO-STAR INTERNATIONAL CO., LTD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MS-N014"),
		},
		.driver_data = &quirk_load_scm_model,
		.callback = dmi_check_cb
	},
	{
		.ident = "MSI CR620",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
				"Micro-Star International"),
			DMI_MATCH(DMI_PRODUCT_NAME, "CR620"),
		},
		.driver_data = &quirk_load_scm_model,
		.callback = dmi_check_cb
	},
	{
		.ident = "MSI U270",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
				"Micro-Star International Co., Ltd."),
			DMI_MATCH(DMI_PRODUCT_NAME, "U270 series"),
		},
		.driver_data = &quirk_load_scm_model,
		.callback = dmi_check_cb
	},
	{
		.ident = "MSI U90/U100",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
				"MICRO-STAR INTERNATIONAL CO., LTD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "U90/U100"),
		},
		.driver_data = &quirk_load_scm_ro_model,
		.callback = dmi_check_cb
	},
	{ }
};

static int rfkill_bluetooth_set(void *data, bool blocked)
{
	/* Do something with blocked...*/
	/*
	 * blocked == false is on
	 * blocked == true is off
	 */
	int result = set_device_state(blocked ? "0" : "1", 0,
			MSI_STANDARD_EC_BLUETOOTH_MASK);

	return min(result, 0);
}

static int rfkill_wlan_set(void *data, bool blocked)
{
	int result = set_device_state(blocked ? "0" : "1", 0,
			MSI_STANDARD_EC_WLAN_MASK);

	return min(result, 0);
}

static int rfkill_threeg_set(void *data, bool blocked)
{
	int result = set_device_state(blocked ? "0" : "1", 0,
			MSI_STANDARD_EC_3G_MASK);

	return min(result, 0);
}

static const struct rfkill_ops rfkill_bluetooth_ops = {
	.set_block = rfkill_bluetooth_set
};

static const struct rfkill_ops rfkill_wlan_ops = {
	.set_block = rfkill_wlan_set
};

static const struct rfkill_ops rfkill_threeg_ops = {
	.set_block = rfkill_threeg_set
};

static void rfkill_cleanup(void)
{
	if (rfk_bluetooth) {
		rfkill_unregister(rfk_bluetooth);
		rfkill_destroy(rfk_bluetooth);
	}

	if (rfk_threeg) {
		rfkill_unregister(rfk_threeg);
		rfkill_destroy(rfk_threeg);
	}

	if (rfk_wlan) {
		rfkill_unregister(rfk_wlan);
		rfkill_destroy(rfk_wlan);
	}
}

static bool msi_rfkill_set_state(struct rfkill *rfkill, bool blocked)
{
	if (quirks->ec_read_only)
		return rfkill_set_hw_state(rfkill, blocked);
	else
		return rfkill_set_sw_state(rfkill, blocked);
}

static void msi_update_rfkill(struct work_struct *ignored)
{
	get_wireless_state_ec_standard();

	if (rfk_wlan)
		msi_rfkill_set_state(rfk_wlan, !wlan_s);
	if (rfk_bluetooth)
		msi_rfkill_set_state(rfk_bluetooth, !bluetooth_s);
	if (rfk_threeg)
		msi_rfkill_set_state(rfk_threeg, !threeg_s);
}
static DECLARE_DELAYED_WORK(msi_rfkill_dwork, msi_update_rfkill);
static DECLARE_WORK(msi_rfkill_work, msi_update_rfkill);

static void msi_send_touchpad_key(struct work_struct *ignored)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_STANDARD_EC_FUNCTIONS_ADDRESS, &rdata);
	if (result < 0)
		return;

	sparse_keymap_report_event(msi_laptop_input_dev,
		(rdata & MSI_STANDARD_EC_TOUCHPAD_MASK) ?
		KEY_TOUCHPAD_ON : KEY_TOUCHPAD_OFF, 1, true);
}
static DECLARE_DELAYED_WORK(msi_touchpad_dwork, msi_send_touchpad_key);
static DECLARE_WORK(msi_touchpad_work, msi_send_touchpad_key);

static bool msi_laptop_i8042_filter(unsigned char data, unsigned char str,
				struct serio *port)
{
	static bool extended;

	if (str & I8042_STR_AUXDATA)
		return false;

	/* 0x54 wwan, 0x62 bluetooth, 0x76 wlan, 0xE4 touchpad toggle*/
	if (unlikely(data == 0xe0)) {
		extended = true;
		return false;
	} else if (unlikely(extended)) {
		extended = false;
		switch (data) {
		case 0xE4:
			if (quirks->ec_delay) {
				schedule_delayed_work(&msi_touchpad_dwork,
					round_jiffies_relative(0.5 * HZ));
			} else
				schedule_work(&msi_touchpad_work);
			break;
		case 0x54:
		case 0x62:
		case 0x76:
			if (quirks->ec_delay) {
				schedule_delayed_work(&msi_rfkill_dwork,
					round_jiffies_relative(0.5 * HZ));
			} else
				schedule_work(&msi_rfkill_work);
			break;
		}
	}

	return false;
}

static void msi_init_rfkill(struct work_struct *ignored)
{
	if (rfk_wlan) {
		rfkill_set_sw_state(rfk_wlan, !wlan_s);
		rfkill_wlan_set(NULL, !wlan_s);
	}
	if (rfk_bluetooth) {
		rfkill_set_sw_state(rfk_bluetooth, !bluetooth_s);
		rfkill_bluetooth_set(NULL, !bluetooth_s);
	}
	if (rfk_threeg) {
		rfkill_set_sw_state(rfk_threeg, !threeg_s);
		rfkill_threeg_set(NULL, !threeg_s);
	}
}
static DECLARE_DELAYED_WORK(msi_rfkill_init, msi_init_rfkill);

static int rfkill_init(struct platform_device *sdev)
{
	/* add rfkill */
	int retval;

	/* keep the hardware wireless state */
	get_wireless_state_ec_standard();

	rfk_bluetooth = rfkill_alloc("msi-bluetooth", &sdev->dev,
				RFKILL_TYPE_BLUETOOTH,
				&rfkill_bluetooth_ops, NULL);
	if (!rfk_bluetooth) {
		retval = -ENOMEM;
		goto err_bluetooth;
	}
	retval = rfkill_register(rfk_bluetooth);
	if (retval)
		goto err_bluetooth;

	rfk_wlan = rfkill_alloc("msi-wlan", &sdev->dev, RFKILL_TYPE_WLAN,
				&rfkill_wlan_ops, NULL);
	if (!rfk_wlan) {
		retval = -ENOMEM;
		goto err_wlan;
	}
	retval = rfkill_register(rfk_wlan);
	if (retval)
		goto err_wlan;

	if (threeg_exists) {
		rfk_threeg = rfkill_alloc("msi-threeg", &sdev->dev,
				RFKILL_TYPE_WWAN, &rfkill_threeg_ops, NULL);
		if (!rfk_threeg) {
			retval = -ENOMEM;
			goto err_threeg;
		}
		retval = rfkill_register(rfk_threeg);
		if (retval)
			goto err_threeg;
	}

	/* schedule to run rfkill state initial */
	if (quirks->ec_delay) {
		schedule_delayed_work(&msi_rfkill_init,
			round_jiffies_relative(1 * HZ));
	} else
		schedule_work(&msi_rfkill_work);

	return 0;

err_threeg:
	rfkill_destroy(rfk_threeg);
	if (rfk_wlan)
		rfkill_unregister(rfk_wlan);
err_wlan:
	rfkill_destroy(rfk_wlan);
	if (rfk_bluetooth)
		rfkill_unregister(rfk_bluetooth);
err_bluetooth:
	rfkill_destroy(rfk_bluetooth);

	return retval;
}

#ifdef CONFIG_PM_SLEEP
static int msi_laptop_resume(struct device *device)
{
	u8 data;
	int result;

	if (!quirks->load_scm_model)
		return 0;

	/* set load SCM to disable hardware control by fn key */
	result = ec_read(MSI_STANDARD_EC_SCM_LOAD_ADDRESS, &data);
	if (result < 0)
		return result;

	result = ec_write(MSI_STANDARD_EC_SCM_LOAD_ADDRESS,
		data | MSI_STANDARD_EC_SCM_LOAD_MASK);
	if (result < 0)
		return result;

	return 0;
}
#endif

static int __init msi_laptop_input_setup(void)
{
	int err;

	msi_laptop_input_dev = input_allocate_device();
	if (!msi_laptop_input_dev)
		return -ENOMEM;

	msi_laptop_input_dev->name = "MSI Laptop hotkeys";
	msi_laptop_input_dev->phys = "msi-laptop/input0";
	msi_laptop_input_dev->id.bustype = BUS_HOST;

	err = sparse_keymap_setup(msi_laptop_input_dev,
		msi_laptop_keymap, NULL);
	if (err)
		goto err_free_dev;

	err = input_register_device(msi_laptop_input_dev);
	if (err)
		goto err_free_keymap;

	return 0;

err_free_keymap:
	sparse_keymap_free(msi_laptop_input_dev);
err_free_dev:
	input_free_device(msi_laptop_input_dev);
	return err;
}

static void msi_laptop_input_destroy(void)
{
	sparse_keymap_free(msi_laptop_input_dev);
	input_unregister_device(msi_laptop_input_dev);
}

static int __init load_scm_model_init(struct platform_device *sdev)
{
	u8 data;
	int result;

	if (!quirks->ec_read_only) {
		/* allow userland write sysfs file  */
		dev_attr_bluetooth.store = store_bluetooth;
		dev_attr_wlan.store = store_wlan;
		dev_attr_threeg.store = store_threeg;
		dev_attr_bluetooth.attr.mode |= S_IWUSR;
		dev_attr_wlan.attr.mode |= S_IWUSR;
		dev_attr_threeg.attr.mode |= S_IWUSR;
	}

	/* disable hardware control by fn key */
	result = ec_read(MSI_STANDARD_EC_SCM_LOAD_ADDRESS, &data);
	if (result < 0)
		return result;

	result = ec_write(MSI_STANDARD_EC_SCM_LOAD_ADDRESS,
		data | MSI_STANDARD_EC_SCM_LOAD_MASK);
	if (result < 0)
		return result;

	/* initial rfkill */
	result = rfkill_init(sdev);
	if (result < 0)
		goto fail_rfkill;

	/* setup input device */
	result = msi_laptop_input_setup();
	if (result)
		goto fail_input;

	result = i8042_install_filter(msi_laptop_i8042_filter);
	if (result) {
		pr_err("Unable to install key filter\n");
		goto fail_filter;
	}

	return 0;

fail_filter:
	msi_laptop_input_destroy();

fail_input:
	rfkill_cleanup();

fail_rfkill:

	return result;

}

static int __init msi_init(void)
{
	int ret;

	if (acpi_disabled)
		return -ENODEV;

	dmi_check_system(msi_dmi_table);
	if (!quirks)
		/* quirks may be NULL if no match in DMI table */
		quirks = &quirk_load_scm_model;
	if (force)
		quirks = &quirk_old_ec_model;

	if (!quirks->old_ec_model)
		get_threeg_exists();

	if (auto_brightness < 0 || auto_brightness > 2)
		return -EINVAL;

	/* Register backlight stuff */

	if (!quirks->old_ec_model || acpi_video_backlight_support()) {
		pr_info("Brightness ignored, must be controlled by ACPI video driver\n");
	} else {
		struct backlight_properties props;
		memset(&props, 0, sizeof(struct backlight_properties));
		props.type = BACKLIGHT_PLATFORM;
		props.max_brightness = MSI_LCD_LEVEL_MAX - 1;
		msibl_device = backlight_device_register("msi-laptop-bl", NULL,
							 NULL, &msibl_ops,
							 &props);
		if (IS_ERR(msibl_device))
			return PTR_ERR(msibl_device);
	}

	ret = platform_driver_register(&msipf_driver);
	if (ret)
		goto fail_backlight;

	/* Register platform stuff */

	msipf_device = platform_device_alloc("msi-laptop-pf", -1);
	if (!msipf_device) {
		ret = -ENOMEM;
		goto fail_platform_driver;
	}

	ret = platform_device_add(msipf_device);
	if (ret)
		goto fail_device_add;

	if (quirks->load_scm_model && (load_scm_model_init(msipf_device) < 0)) {
		ret = -EINVAL;
		goto fail_scm_model_init;
	}

	ret = sysfs_create_group(&msipf_device->dev.kobj,
				 &msipf_attribute_group);
	if (ret)
		goto fail_create_group;

	if (!quirks->old_ec_model) {
		if (threeg_exists)
			ret = device_create_file(&msipf_device->dev,
						&dev_attr_threeg);
		if (ret)
			goto fail_create_attr;
	} else {
		ret = sysfs_create_group(&msipf_device->dev.kobj,
					 &msipf_old_attribute_group);
		if (ret)
			goto fail_create_attr;

		/* Disable automatic brightness control by default because
		 * this module was probably loaded to do brightness control in
		 * software. */

		if (auto_brightness != 2)
			set_auto_brightness(auto_brightness);
	}

	pr_info("driver " MSI_DRIVER_VERSION " successfully loaded\n");

	return 0;

fail_create_attr:
	sysfs_remove_group(&msipf_device->dev.kobj, &msipf_attribute_group);
fail_create_group:
	if (quirks->load_scm_model) {
		i8042_remove_filter(msi_laptop_i8042_filter);
		cancel_delayed_work_sync(&msi_rfkill_dwork);
		cancel_work_sync(&msi_rfkill_work);
		rfkill_cleanup();
	}
fail_scm_model_init:
	platform_device_del(msipf_device);
fail_device_add:
	platform_device_put(msipf_device);
fail_platform_driver:
	platform_driver_unregister(&msipf_driver);
fail_backlight:
	backlight_device_unregister(msibl_device);

	return ret;
}

static void __exit msi_cleanup(void)
{
	if (quirks->load_scm_model) {
		i8042_remove_filter(msi_laptop_i8042_filter);
		msi_laptop_input_destroy();
		cancel_delayed_work_sync(&msi_rfkill_dwork);
		cancel_work_sync(&msi_rfkill_work);
		rfkill_cleanup();
	}

	sysfs_remove_group(&msipf_device->dev.kobj, &msipf_attribute_group);
	if (!quirks->old_ec_model && threeg_exists)
		device_remove_file(&msipf_device->dev, &dev_attr_threeg);
	platform_device_unregister(msipf_device);
	platform_driver_unregister(&msipf_driver);
	backlight_device_unregister(msibl_device);

	if (quirks->old_ec_model) {
		/* Enable automatic brightness control again */
		if (auto_brightness != 2)
			set_auto_brightness(1);
	}

	pr_info("driver unloaded\n");
}

module_init(msi_init);
module_exit(msi_cleanup);

MODULE_AUTHOR("Lennart Poettering");
MODULE_DESCRIPTION("MSI Laptop Support");
MODULE_VERSION(MSI_DRIVER_VERSION);
MODULE_LICENSE("GPL");

MODULE_ALIAS("dmi:*:svnMICRO-STARINT'LCO.,LTD:pnMS-1013:pvr0131*:cvnMICRO-STARINT'LCO.,LTD:ct10:*");
MODULE_ALIAS("dmi:*:svnMicro-StarInternational:pnMS-1058:pvr0581:rvnMSI:rnMS-1058:*:ct10:*");
MODULE_ALIAS("dmi:*:svnMicro-StarInternational:pnMS-1412:*:rvnMSI:rnMS-1412:*:cvnMICRO-STARINT'LCO.,LTD:ct10:*");
MODULE_ALIAS("dmi:*:svnNOTEBOOK:pnSAM2000:pvr0131*:cvnMICRO-STARINT'LCO.,LTD:ct10:*");
MODULE_ALIAS("dmi:*:svnMICRO-STARINTERNATIONAL*:pnMS-N034:*");
MODULE_ALIAS("dmi:*:svnMICRO-STARINTERNATIONAL*:pnMS-N051:*");
MODULE_ALIAS("dmi:*:svnMICRO-STARINTERNATIONAL*:pnMS-N014:*");
MODULE_ALIAS("dmi:*:svnMicro-StarInternational*:pnCR620:*");
MODULE_ALIAS("dmi:*:svnMicro-StarInternational*:pnU270series:*");
MODULE_ALIAS("dmi:*:svnMICRO-STARINTERNATIONAL*:pnU90/U100:*");
