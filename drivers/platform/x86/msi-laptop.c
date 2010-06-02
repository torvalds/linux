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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>
#include <linux/i8042.h>

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

static int msi_laptop_resume(struct platform_device *device);

#define MSI_STANDARD_EC_DEVICES_EXISTS_ADDRESS	0x2f

static int force;
module_param(force, bool, 0);
MODULE_PARM_DESC(force, "Force driver load, ignore DMI data");

static int auto_brightness;
module_param(auto_brightness, int, 0);
MODULE_PARM_DESC(auto_brightness, "Enable automatic brightness control (0: disabled; 1: enabled; 2: don't touch)");

static bool old_ec_model;
static int wlan_s, bluetooth_s, threeg_s;
static int threeg_exists;

/* Some MSI 3G netbook only have one fn key to control Wlan/Bluetooth/3G,
 * those netbook will load the SCM (windows app) to disable the original
 * Wlan/Bluetooth control by BIOS when user press fn key, then control
 * Wlan/Bluetooth/3G by SCM (software control by OS). Without SCM, user
 * cann't on/off 3G module on those 3G netbook.
 * On Linux, msi-laptop driver will do the same thing to disable the
 * original BIOS control, then might need use HAL or other userland
 * application to do the software control that simulate with SCM.
 * e.g. MSI N034 netbook
 */
static bool load_scm_model;
static struct rfkill *rfk_wlan, *rfk_bluetooth, *rfk_threeg;

/* Hardware access */

static int set_lcd_level(int level)
{
	u8 buf[2];

	if (level < 0 || level >= MSI_LCD_LEVEL_MAX)
		return -EINVAL;

	buf[0] = 0x80;
	buf[1] = (u8) (level*31);

	return ec_transaction(MSI_EC_COMMAND_LCD_LEVEL, buf, sizeof(buf),
			      NULL, 0, 1);
}

static int get_lcd_level(void)
{
	u8 wdata = 0, rdata;
	int result;

	result = ec_transaction(MSI_EC_COMMAND_LCD_LEVEL, &wdata, 1,
				&rdata, 1, 1);
	if (result < 0)
		return result;

	return (int) rdata / 31;
}

static int get_auto_brightness(void)
{
	u8 wdata = 4, rdata;
	int result;

	result = ec_transaction(MSI_EC_COMMAND_LCD_LEVEL, &wdata, 1,
				&rdata, 1, 1);
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
				&rdata, 1, 1);
	if (result < 0)
		return result;

	wdata[0] = 0x84;
	wdata[1] = (rdata & 0xF7) | (enable ? 8 : 0);

	return ec_transaction(MSI_EC_COMMAND_LCD_LEVEL, wdata, 2,
			      NULL, 0, 1);
}

static ssize_t set_device_state(const char *buf, size_t count, u8 mask)
{
	int status;
	u8 wdata = 0, rdata;
	int result;

	if (sscanf(buf, "%i", &status) != 1 || (status < 0 || status > 1))
		return -EINVAL;

	/* read current device state */
	result = ec_read(MSI_STANDARD_EC_COMMAND_ADDRESS, &rdata);
	if (result < 0)
		return -EINVAL;

	if (!!(rdata & mask) != status) {
		/* reverse device bit */
		if (rdata & mask)
			wdata = rdata & ~mask;
		else
			wdata = rdata | mask;

		result = ec_write(MSI_STANDARD_EC_COMMAND_ADDRESS, wdata);
		if (result < 0)
			return -EINVAL;
	}

	return count;
}

static int get_wireless_state(int *wlan, int *bluetooth)
{
	u8 wdata = 0, rdata;
	int result;

	result = ec_transaction(MSI_EC_COMMAND_WIRELESS, &wdata, 1, &rdata, 1, 1);
	if (result < 0)
		return -1;

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
		return -1;

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
		return -1;

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

	int ret, enabled;

	if (old_ec_model) {
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

	int ret, enabled;

	if (old_ec_model) {
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
	if (old_ec_model)
		return -1;

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

static DEVICE_ATTR(lcd_level, 0644, show_lcd_level, store_lcd_level);
static DEVICE_ATTR(auto_brightness, 0644, show_auto_brightness,
		   store_auto_brightness);
static DEVICE_ATTR(bluetooth, 0444, show_bluetooth, NULL);
static DEVICE_ATTR(wlan, 0444, show_wlan, NULL);
static DEVICE_ATTR(threeg, 0444, show_threeg, NULL);

static struct attribute *msipf_attributes[] = {
	&dev_attr_lcd_level.attr,
	&dev_attr_auto_brightness.attr,
	&dev_attr_bluetooth.attr,
	&dev_attr_wlan.attr,
	NULL
};

static struct attribute_group msipf_attribute_group = {
	.attrs = msipf_attributes
};

static struct platform_driver msipf_driver = {
	.driver = {
		.name = "msi-laptop-pf",
		.owner = THIS_MODULE,
	},
	.resume = msi_laptop_resume,
};

static struct platform_device *msipf_device;

/* Initialization */

static int dmi_check_cb(const struct dmi_system_id *id)
{
	printk(KERN_INFO "msi-laptop: Identified laptop model '%s'.\n",
	       id->ident);
	return 0;
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
		.callback = dmi_check_cb
	},
	{ }
};

static struct dmi_system_id __initdata msi_load_scm_models_dmi_table[] = {
	{
		.ident = "MSI N034",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
				"MICRO-STAR INTERNATIONAL CO., LTD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MS-N034"),
			DMI_MATCH(DMI_CHASSIS_VENDOR,
			"MICRO-STAR INTERNATIONAL CO., LTD")
		},
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
		.callback = dmi_check_cb
	},
	{
		.ident = "MSI N014",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
				"MICRO-STAR INTERNATIONAL CO., LTD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MS-N014"),
		},
		.callback = dmi_check_cb
	},
	{
		.ident = "MSI CR620",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
				"Micro-Star International"),
			DMI_MATCH(DMI_PRODUCT_NAME, "CR620"),
		},
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
	if (blocked)
		set_device_state("0", 0, MSI_STANDARD_EC_BLUETOOTH_MASK);
	else
		set_device_state("1", 0, MSI_STANDARD_EC_BLUETOOTH_MASK);

	return 0;
}

static int rfkill_wlan_set(void *data, bool blocked)
{
	if (blocked)
		set_device_state("0", 0, MSI_STANDARD_EC_WLAN_MASK);
	else
		set_device_state("1", 0, MSI_STANDARD_EC_WLAN_MASK);

	return 0;
}

static int rfkill_threeg_set(void *data, bool blocked)
{
	if (blocked)
		set_device_state("0", 0, MSI_STANDARD_EC_3G_MASK);
	else
		set_device_state("1", 0, MSI_STANDARD_EC_3G_MASK);

	return 0;
}

static struct rfkill_ops rfkill_bluetooth_ops = {
	.set_block = rfkill_bluetooth_set
};

static struct rfkill_ops rfkill_wlan_ops = {
	.set_block = rfkill_wlan_set
};

static struct rfkill_ops rfkill_threeg_ops = {
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

static void msi_update_rfkill(struct work_struct *ignored)
{
	get_wireless_state_ec_standard();

	if (rfk_wlan)
		rfkill_set_sw_state(rfk_wlan, !wlan_s);
	if (rfk_bluetooth)
		rfkill_set_sw_state(rfk_bluetooth, !bluetooth_s);
	if (rfk_threeg)
		rfkill_set_sw_state(rfk_threeg, !threeg_s);
}
static DECLARE_DELAYED_WORK(msi_rfkill_work, msi_update_rfkill);

static bool msi_laptop_i8042_filter(unsigned char data, unsigned char str,
				struct serio *port)
{
	static bool extended;

	if (str & 0x20)
		return false;

	/* 0x54 wwan, 0x62 bluetooth, 0x76 wlan*/
	if (unlikely(data == 0xe0)) {
		extended = true;
		return false;
	} else if (unlikely(extended)) {
		switch (data) {
		case 0x54:
		case 0x62:
		case 0x76:
			schedule_delayed_work(&msi_rfkill_work,
				round_jiffies_relative(0.5 * HZ));
			break;
		}
		extended = false;
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
	schedule_delayed_work(&msi_rfkill_init,
				round_jiffies_relative(1 * HZ));

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

static int msi_laptop_resume(struct platform_device *device)
{
	u8 data;
	int result;

	if (!load_scm_model)
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

static int load_scm_model_init(struct platform_device *sdev)
{
	u8 data;
	int result;

	/* allow userland write sysfs file  */
	dev_attr_bluetooth.store = store_bluetooth;
	dev_attr_wlan.store = store_wlan;
	dev_attr_threeg.store = store_threeg;
	dev_attr_bluetooth.attr.mode |= S_IWUSR;
	dev_attr_wlan.attr.mode |= S_IWUSR;
	dev_attr_threeg.attr.mode |= S_IWUSR;

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

	result = i8042_install_filter(msi_laptop_i8042_filter);
	if (result) {
		printk(KERN_ERR
			"msi-laptop: Unable to install key filter\n");
		goto fail_filter;
	}

	return 0;

fail_filter:
	rfkill_cleanup();

fail_rfkill:

	return result;

}

static int __init msi_init(void)
{
	int ret;

	if (acpi_disabled)
		return -ENODEV;

	if (force || dmi_check_system(msi_dmi_table))
		old_ec_model = 1;

	if (!old_ec_model)
		get_threeg_exists();

	if (!old_ec_model && dmi_check_system(msi_load_scm_models_dmi_table))
		load_scm_model = 1;

	if (auto_brightness < 0 || auto_brightness > 2)
		return -EINVAL;

	/* Register backlight stuff */

	if (acpi_video_backlight_support()) {
		printk(KERN_INFO "MSI: Brightness ignored, must be controlled "
		       "by ACPI video driver\n");
	} else {
		struct backlight_properties props;
		memset(&props, 0, sizeof(struct backlight_properties));
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
		goto fail_platform_device1;

	if (load_scm_model && (load_scm_model_init(msipf_device) < 0)) {
		ret = -EINVAL;
		goto fail_platform_device1;
	}

	ret = sysfs_create_group(&msipf_device->dev.kobj,
				 &msipf_attribute_group);
	if (ret)
		goto fail_platform_device2;

	if (!old_ec_model) {
		if (threeg_exists)
			ret = device_create_file(&msipf_device->dev,
						&dev_attr_threeg);
		if (ret)
			goto fail_platform_device2;
	}

	/* Disable automatic brightness control by default because
	 * this module was probably loaded to do brightness control in
	 * software. */

	if (auto_brightness != 2)
		set_auto_brightness(auto_brightness);

	printk(KERN_INFO "msi-laptop: driver "MSI_DRIVER_VERSION" successfully loaded.\n");

	return 0;

fail_platform_device2:

	if (load_scm_model) {
		i8042_remove_filter(msi_laptop_i8042_filter);
		cancel_delayed_work_sync(&msi_rfkill_work);
		rfkill_cleanup();
	}
	platform_device_del(msipf_device);

fail_platform_device1:

	platform_device_put(msipf_device);

fail_platform_driver:

	platform_driver_unregister(&msipf_driver);

fail_backlight:

	backlight_device_unregister(msibl_device);

	return ret;
}

static void __exit msi_cleanup(void)
{
	if (load_scm_model) {
		i8042_remove_filter(msi_laptop_i8042_filter);
		cancel_delayed_work_sync(&msi_rfkill_work);
		rfkill_cleanup();
	}

	sysfs_remove_group(&msipf_device->dev.kobj, &msipf_attribute_group);
	if (!old_ec_model && threeg_exists)
		device_remove_file(&msipf_device->dev, &dev_attr_threeg);
	platform_device_unregister(msipf_device);
	platform_driver_unregister(&msipf_driver);
	backlight_device_unregister(msibl_device);

	/* Enable automatic brightness control again */
	if (auto_brightness != 2)
		set_auto_brightness(1);

	printk(KERN_INFO "msi-laptop: driver unloaded.\n");
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
