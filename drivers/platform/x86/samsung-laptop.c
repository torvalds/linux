// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung Laptop driver
 *
 * Copyright (C) 2009,2011 Greg Kroah-Hartman (gregkh@suse.de)
 * Copyright (C) 2009,2011 Novell Inc.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/backlight.h>
#include <linux/leds.h>
#include <linux/fb.h>
#include <linux/dmi.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>
#include <linux/acpi.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/ctype.h>
#include <linux/efi.h>
#include <linux/suspend.h>
#include <acpi/video.h>

/*
 * This driver is needed because a number of Samsung laptops do not hook
 * their control settings through ACPI.  So we have to poke around in the
 * BIOS to do things like brightness values, and "special" key controls.
 */

/*
 * We have 0 - 8 as valid brightness levels.  The specs say that level 0 should
 * be reserved by the BIOS (which really doesn't make much sense), we tell
 * userspace that the value is 0 - 7 and then just tell the hardware 1 - 8
 */
#define MAX_BRIGHT	0x07


#define SABI_IFACE_MAIN			0x00
#define SABI_IFACE_SUB			0x02
#define SABI_IFACE_COMPLETE		0x04
#define SABI_IFACE_DATA			0x05

#define WL_STATUS_WLAN			0x0
#define WL_STATUS_BT			0x2

/* Structure get/set data using sabi */
struct sabi_data {
	union {
		struct {
			u32 d0;
			u32 d1;
			u16 d2;
			u8  d3;
		};
		u8 data[11];
	};
};

struct sabi_header_offsets {
	u8 port;
	u8 re_mem;
	u8 iface_func;
	u8 en_mem;
	u8 data_offset;
	u8 data_segment;
};

struct sabi_commands {
	/*
	 * Brightness is 0 - 8, as described above.
	 * Value 0 is for the BIOS to use
	 */
	u16 get_brightness;
	u16 set_brightness;

	/*
	 * first byte:
	 * 0x00 - wireless is off
	 * 0x01 - wireless is on
	 * second byte:
	 * 0x02 - 3G is off
	 * 0x03 - 3G is on
	 * TODO, verify 3G is correct, that doesn't seem right...
	 */
	u16 get_wireless_button;
	u16 set_wireless_button;

	/* 0 is off, 1 is on */
	u16 get_backlight;
	u16 set_backlight;

	/*
	 * 0x80 or 0x00 - no action
	 * 0x81 - recovery key pressed
	 */
	u16 get_recovery_mode;
	u16 set_recovery_mode;

	/*
	 * on seclinux: 0 is low, 1 is high,
	 * on swsmi: 0 is normal, 1 is silent, 2 is turbo
	 */
	u16 get_performance_level;
	u16 set_performance_level;

	/* 0x80 is off, 0x81 is on */
	u16 get_battery_life_extender;
	u16 set_battery_life_extender;

	/* 0x80 is off, 0x81 is on */
	u16 get_usb_charge;
	u16 set_usb_charge;

	/* the first byte is for bluetooth and the third one is for wlan */
	u16 get_wireless_status;
	u16 set_wireless_status;

	/* 0x80 is off, 0x81 is on */
	u16 get_lid_handling;
	u16 set_lid_handling;

	/* 0x81 to read, (0x82 | level << 8) to set, 0xaabb to enable */
	u16 kbd_backlight;

	/*
	 * Tell the BIOS that Linux is running on this machine.
	 * 81 is on, 80 is off
	 */
	u16 set_linux;
};

struct sabi_performance_level {
	const char *name;
	u16 value;
};

struct sabi_config {
	int sabi_version;
	const char *test_string;
	u16 main_function;
	const struct sabi_header_offsets header_offsets;
	const struct sabi_commands commands;
	const struct sabi_performance_level performance_levels[4];
	u8 min_brightness;
	u8 max_brightness;
};

static const struct sabi_config sabi_configs[] = {
	{
		/* I don't know if it is really 2, but it is
		 * less than 3 anyway */
		.sabi_version = 2,

		.test_string = "SECLINUX",

		.main_function = 0x4c49,

		.header_offsets = {
			.port = 0x00,
			.re_mem = 0x02,
			.iface_func = 0x03,
			.en_mem = 0x04,
			.data_offset = 0x05,
			.data_segment = 0x07,
		},

		.commands = {
			.get_brightness = 0x00,
			.set_brightness = 0x01,

			.get_wireless_button = 0x02,
			.set_wireless_button = 0x03,

			.get_backlight = 0x04,
			.set_backlight = 0x05,

			.get_recovery_mode = 0x06,
			.set_recovery_mode = 0x07,

			.get_performance_level = 0x08,
			.set_performance_level = 0x09,

			.get_battery_life_extender = 0xFFFF,
			.set_battery_life_extender = 0xFFFF,

			.get_usb_charge = 0xFFFF,
			.set_usb_charge = 0xFFFF,

			.get_wireless_status = 0xFFFF,
			.set_wireless_status = 0xFFFF,

			.get_lid_handling = 0xFFFF,
			.set_lid_handling = 0xFFFF,

			.kbd_backlight = 0xFFFF,

			.set_linux = 0x0a,
		},

		.performance_levels = {
			{
				.name = "silent",
				.value = 0,
			},
			{
				.name = "normal",
				.value = 1,
			},
			{ },
		},
		.min_brightness = 1,
		.max_brightness = 8,
	},
	{
		.sabi_version = 3,

		.test_string = "SwSmi@",

		.main_function = 0x5843,

		.header_offsets = {
			.port = 0x00,
			.re_mem = 0x04,
			.iface_func = 0x02,
			.en_mem = 0x03,
			.data_offset = 0x05,
			.data_segment = 0x07,
		},

		.commands = {
			.get_brightness = 0x10,
			.set_brightness = 0x11,

			.get_wireless_button = 0x12,
			.set_wireless_button = 0x13,

			.get_backlight = 0x2d,
			.set_backlight = 0x2e,

			.get_recovery_mode = 0xff,
			.set_recovery_mode = 0xff,

			.get_performance_level = 0x31,
			.set_performance_level = 0x32,

			.get_battery_life_extender = 0x65,
			.set_battery_life_extender = 0x66,

			.get_usb_charge = 0x67,
			.set_usb_charge = 0x68,

			.get_wireless_status = 0x69,
			.set_wireless_status = 0x6a,

			.get_lid_handling = 0x6d,
			.set_lid_handling = 0x6e,

			.kbd_backlight = 0x78,

			.set_linux = 0xff,
		},

		.performance_levels = {
			{
				.name = "normal",
				.value = 0,
			},
			{
				.name = "silent",
				.value = 1,
			},
			{
				.name = "overclock",
				.value = 2,
			},
			{ },
		},
		.min_brightness = 0,
		.max_brightness = 8,
	},
	{ },
};

/*
 * samsung-laptop/    - debugfs root directory
 *   f0000_segment    - dump f0000 segment
 *   command          - current command
 *   data             - current data
 *   d0, d1, d2, d3   - data fields
 *   call             - call SABI using command and data
 *
 * This allow to call arbitrary sabi commands wihout
 * modifying the driver at all.
 * For example, setting the keyboard backlight brightness to 5
 *
 *  echo 0x78 > command
 *  echo 0x0582 > d0
 *  echo 0 > d1
 *  echo 0 > d2
 *  echo 0 > d3
 *  cat call
 */

struct samsung_laptop_debug {
	struct dentry *root;
	struct sabi_data data;
	u16 command;

	struct debugfs_blob_wrapper f0000_wrapper;
	struct debugfs_blob_wrapper data_wrapper;
	struct debugfs_blob_wrapper sdiag_wrapper;
};

struct samsung_laptop;

struct samsung_rfkill {
	struct samsung_laptop *samsung;
	struct rfkill *rfkill;
	enum rfkill_type type;
};

struct samsung_laptop {
	const struct sabi_config *config;

	void __iomem *sabi;
	void __iomem *sabi_iface;
	void __iomem *f0000_segment;

	struct mutex sabi_mutex;

	struct platform_device *platform_device;
	struct backlight_device *backlight_device;

	struct samsung_rfkill wlan;
	struct samsung_rfkill bluetooth;

	struct led_classdev kbd_led;
	int kbd_led_wk;
	struct workqueue_struct *led_workqueue;
	struct work_struct kbd_led_work;

	struct samsung_laptop_debug debug;
	struct samsung_quirks *quirks;

	struct notifier_block pm_nb;

	bool handle_backlight;
	bool has_stepping_quirk;

	char sdiag[64];
};

struct samsung_quirks {
	bool broken_acpi_video;
	bool four_kbd_backlight_levels;
	bool enable_kbd_backlight;
	bool use_native_backlight;
	bool lid_handling;
};

static struct samsung_quirks samsung_unknown = {};

static struct samsung_quirks samsung_broken_acpi_video = {
	.broken_acpi_video = true,
};

static struct samsung_quirks samsung_use_native_backlight = {
	.use_native_backlight = true,
};

static struct samsung_quirks samsung_np740u3e = {
	.four_kbd_backlight_levels = true,
	.enable_kbd_backlight = true,
};

static struct samsung_quirks samsung_lid_handling = {
	.lid_handling = true,
};

static bool force;
module_param(force, bool, 0);
MODULE_PARM_DESC(force,
		"Disable the DMI check and forces the driver to be loaded");

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug enabled or not");

static int sabi_command(struct samsung_laptop *samsung, u16 command,
			struct sabi_data *in,
			struct sabi_data *out)
{
	const struct sabi_config *config = samsung->config;
	int ret = 0;
	u16 port = readw(samsung->sabi + config->header_offsets.port);
	u8 complete, iface_data;

	mutex_lock(&samsung->sabi_mutex);

	if (debug) {
		if (in)
			pr_info("SABI command:0x%04x "
				"data:{0x%08x, 0x%08x, 0x%04x, 0x%02x}",
				command, in->d0, in->d1, in->d2, in->d3);
		else
			pr_info("SABI command:0x%04x", command);
	}

	/* enable memory to be able to write to it */
	outb(readb(samsung->sabi + config->header_offsets.en_mem), port);

	/* write out the command */
	writew(config->main_function, samsung->sabi_iface + SABI_IFACE_MAIN);
	writew(command, samsung->sabi_iface + SABI_IFACE_SUB);
	writeb(0, samsung->sabi_iface + SABI_IFACE_COMPLETE);
	if (in) {
		writel(in->d0, samsung->sabi_iface + SABI_IFACE_DATA);
		writel(in->d1, samsung->sabi_iface + SABI_IFACE_DATA + 4);
		writew(in->d2, samsung->sabi_iface + SABI_IFACE_DATA + 8);
		writeb(in->d3, samsung->sabi_iface + SABI_IFACE_DATA + 10);
	}
	outb(readb(samsung->sabi + config->header_offsets.iface_func), port);

	/* write protect memory to make it safe */
	outb(readb(samsung->sabi + config->header_offsets.re_mem), port);

	/* see if the command actually succeeded */
	complete = readb(samsung->sabi_iface + SABI_IFACE_COMPLETE);
	iface_data = readb(samsung->sabi_iface + SABI_IFACE_DATA);

	/* iface_data = 0xFF happens when a command is not known
	 * so we only add a warning in debug mode since we will
	 * probably issue some unknown command at startup to find
	 * out which features are supported */
	if (complete != 0xaa || (iface_data == 0xff && debug))
		pr_warn("SABI command 0x%04x failed with"
			" completion flag 0x%02x and interface data 0x%02x",
			command, complete, iface_data);

	if (complete != 0xaa || iface_data == 0xff) {
		ret = -EINVAL;
		goto exit;
	}

	if (out) {
		out->d0 = readl(samsung->sabi_iface + SABI_IFACE_DATA);
		out->d1 = readl(samsung->sabi_iface + SABI_IFACE_DATA + 4);
		out->d2 = readw(samsung->sabi_iface + SABI_IFACE_DATA + 2);
		out->d3 = readb(samsung->sabi_iface + SABI_IFACE_DATA + 1);
	}

	if (debug && out) {
		pr_info("SABI return data:{0x%08x, 0x%08x, 0x%04x, 0x%02x}",
			out->d0, out->d1, out->d2, out->d3);
	}

exit:
	mutex_unlock(&samsung->sabi_mutex);
	return ret;
}

/* simple wrappers usable with most commands */
static int sabi_set_commandb(struct samsung_laptop *samsung,
			     u16 command, u8 data)
{
	struct sabi_data in = { { { .d0 = 0, .d1 = 0, .d2 = 0, .d3 = 0 } } };

	in.data[0] = data;
	return sabi_command(samsung, command, &in, NULL);
}

static int read_brightness(struct samsung_laptop *samsung)
{
	const struct sabi_config *config = samsung->config;
	const struct sabi_commands *commands = &samsung->config->commands;
	struct sabi_data sretval;
	int user_brightness = 0;
	int retval;

	retval = sabi_command(samsung, commands->get_brightness,
			      NULL, &sretval);
	if (retval)
		return retval;

	user_brightness = sretval.data[0];
	if (user_brightness > config->min_brightness)
		user_brightness -= config->min_brightness;
	else
		user_brightness = 0;

	return user_brightness;
}

static void set_brightness(struct samsung_laptop *samsung, u8 user_brightness)
{
	const struct sabi_config *config = samsung->config;
	const struct sabi_commands *commands = &samsung->config->commands;
	u8 user_level = user_brightness + config->min_brightness;

	if (samsung->has_stepping_quirk && user_level != 0) {
		/*
		 * short circuit if the specified level is what's already set
		 * to prevent the screen from flickering needlessly
		 */
		if (user_brightness == read_brightness(samsung))
			return;

		sabi_set_commandb(samsung, commands->set_brightness, 0);
	}

	sabi_set_commandb(samsung, commands->set_brightness, user_level);
}

static int get_brightness(struct backlight_device *bd)
{
	struct samsung_laptop *samsung = bl_get_data(bd);

	return read_brightness(samsung);
}

static void check_for_stepping_quirk(struct samsung_laptop *samsung)
{
	int initial_level;
	int check_level;
	int orig_level = read_brightness(samsung);

	/*
	 * Some laptops exhibit the strange behaviour of stepping toward
	 * (rather than setting) the brightness except when changing to/from
	 * brightness level 0. This behaviour is checked for here and worked
	 * around in set_brightness.
	 */

	if (orig_level == 0)
		set_brightness(samsung, 1);

	initial_level = read_brightness(samsung);

	if (initial_level <= 2)
		check_level = initial_level + 2;
	else
		check_level = initial_level - 2;

	samsung->has_stepping_quirk = false;
	set_brightness(samsung, check_level);

	if (read_brightness(samsung) != check_level) {
		samsung->has_stepping_quirk = true;
		pr_info("enabled workaround for brightness stepping quirk\n");
	}

	set_brightness(samsung, orig_level);
}

static int update_status(struct backlight_device *bd)
{
	struct samsung_laptop *samsung = bl_get_data(bd);
	const struct sabi_commands *commands = &samsung->config->commands;

	set_brightness(samsung, bd->props.brightness);

	if (bd->props.power == FB_BLANK_UNBLANK)
		sabi_set_commandb(samsung, commands->set_backlight, 1);
	else
		sabi_set_commandb(samsung, commands->set_backlight, 0);

	return 0;
}

static const struct backlight_ops backlight_ops = {
	.get_brightness	= get_brightness,
	.update_status	= update_status,
};

static int seclinux_rfkill_set(void *data, bool blocked)
{
	struct samsung_rfkill *srfkill = data;
	struct samsung_laptop *samsung = srfkill->samsung;
	const struct sabi_commands *commands = &samsung->config->commands;

	return sabi_set_commandb(samsung, commands->set_wireless_button,
				 !blocked);
}

static const struct rfkill_ops seclinux_rfkill_ops = {
	.set_block = seclinux_rfkill_set,
};

static int swsmi_wireless_status(struct samsung_laptop *samsung,
				 struct sabi_data *data)
{
	const struct sabi_commands *commands = &samsung->config->commands;

	return sabi_command(samsung, commands->get_wireless_status,
			    NULL, data);
}

static int swsmi_rfkill_set(void *priv, bool blocked)
{
	struct samsung_rfkill *srfkill = priv;
	struct samsung_laptop *samsung = srfkill->samsung;
	const struct sabi_commands *commands = &samsung->config->commands;
	struct sabi_data data;
	int ret, i;

	ret = swsmi_wireless_status(samsung, &data);
	if (ret)
		return ret;

	/* Don't set the state for non-present devices */
	for (i = 0; i < 4; i++)
		if (data.data[i] == 0x02)
			data.data[1] = 0;

	if (srfkill->type == RFKILL_TYPE_WLAN)
		data.data[WL_STATUS_WLAN] = !blocked;
	else if (srfkill->type == RFKILL_TYPE_BLUETOOTH)
		data.data[WL_STATUS_BT] = !blocked;

	return sabi_command(samsung, commands->set_wireless_status,
			    &data, &data);
}

static void swsmi_rfkill_query(struct rfkill *rfkill, void *priv)
{
	struct samsung_rfkill *srfkill = priv;
	struct samsung_laptop *samsung = srfkill->samsung;
	struct sabi_data data;
	int ret;

	ret = swsmi_wireless_status(samsung, &data);
	if (ret)
		return ;

	if (srfkill->type == RFKILL_TYPE_WLAN)
		ret = data.data[WL_STATUS_WLAN];
	else if (srfkill->type == RFKILL_TYPE_BLUETOOTH)
		ret = data.data[WL_STATUS_BT];
	else
		return ;

	rfkill_set_sw_state(rfkill, !ret);
}

static const struct rfkill_ops swsmi_rfkill_ops = {
	.set_block = swsmi_rfkill_set,
	.query = swsmi_rfkill_query,
};

static ssize_t get_performance_level(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct samsung_laptop *samsung = dev_get_drvdata(dev);
	const struct sabi_config *config = samsung->config;
	const struct sabi_commands *commands = &config->commands;
	struct sabi_data sretval;
	int retval;
	int i;

	/* Read the state */
	retval = sabi_command(samsung, commands->get_performance_level,
			      NULL, &sretval);
	if (retval)
		return retval;

	/* The logic is backwards, yeah, lots of fun... */
	for (i = 0; config->performance_levels[i].name; ++i) {
		if (sretval.data[0] == config->performance_levels[i].value)
			return sprintf(buf, "%s\n", config->performance_levels[i].name);
	}
	return sprintf(buf, "%s\n", "unknown");
}

static ssize_t set_performance_level(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct samsung_laptop *samsung = dev_get_drvdata(dev);
	const struct sabi_config *config = samsung->config;
	const struct sabi_commands *commands = &config->commands;
	int i;

	if (count < 1)
		return count;

	for (i = 0; config->performance_levels[i].name; ++i) {
		const struct sabi_performance_level *level =
			&config->performance_levels[i];
		if (!strncasecmp(level->name, buf, strlen(level->name))) {
			sabi_set_commandb(samsung,
					  commands->set_performance_level,
					  level->value);
			break;
		}
	}

	if (!config->performance_levels[i].name)
		return -EINVAL;

	return count;
}

static DEVICE_ATTR(performance_level, 0644,
		   get_performance_level, set_performance_level);

static int read_battery_life_extender(struct samsung_laptop *samsung)
{
	const struct sabi_commands *commands = &samsung->config->commands;
	struct sabi_data data;
	int retval;

	if (commands->get_battery_life_extender == 0xFFFF)
		return -ENODEV;

	memset(&data, 0, sizeof(data));
	data.data[0] = 0x80;
	retval = sabi_command(samsung, commands->get_battery_life_extender,
			      &data, &data);

	if (retval)
		return retval;

	if (data.data[0] != 0 && data.data[0] != 1)
		return -ENODEV;

	return data.data[0];
}

static int write_battery_life_extender(struct samsung_laptop *samsung,
				       int enabled)
{
	const struct sabi_commands *commands = &samsung->config->commands;
	struct sabi_data data;

	memset(&data, 0, sizeof(data));
	data.data[0] = 0x80 | enabled;
	return sabi_command(samsung, commands->set_battery_life_extender,
			    &data, NULL);
}

static ssize_t get_battery_life_extender(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct samsung_laptop *samsung = dev_get_drvdata(dev);
	int ret;

	ret = read_battery_life_extender(samsung);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", ret);
}

static ssize_t set_battery_life_extender(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct samsung_laptop *samsung = dev_get_drvdata(dev);
	int ret, value;

	if (!count || kstrtoint(buf, 0, &value) != 0)
		return -EINVAL;

	ret = write_battery_life_extender(samsung, !!value);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(battery_life_extender, 0644,
		   get_battery_life_extender, set_battery_life_extender);

static int read_usb_charge(struct samsung_laptop *samsung)
{
	const struct sabi_commands *commands = &samsung->config->commands;
	struct sabi_data data;
	int retval;

	if (commands->get_usb_charge == 0xFFFF)
		return -ENODEV;

	memset(&data, 0, sizeof(data));
	data.data[0] = 0x80;
	retval = sabi_command(samsung, commands->get_usb_charge,
			      &data, &data);

	if (retval)
		return retval;

	if (data.data[0] != 0 && data.data[0] != 1)
		return -ENODEV;

	return data.data[0];
}

static int write_usb_charge(struct samsung_laptop *samsung,
			    int enabled)
{
	const struct sabi_commands *commands = &samsung->config->commands;
	struct sabi_data data;

	memset(&data, 0, sizeof(data));
	data.data[0] = 0x80 | enabled;
	return sabi_command(samsung, commands->set_usb_charge,
			    &data, NULL);
}

static ssize_t get_usb_charge(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct samsung_laptop *samsung = dev_get_drvdata(dev);
	int ret;

	ret = read_usb_charge(samsung);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", ret);
}

static ssize_t set_usb_charge(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct samsung_laptop *samsung = dev_get_drvdata(dev);
	int ret, value;

	if (!count || kstrtoint(buf, 0, &value) != 0)
		return -EINVAL;

	ret = write_usb_charge(samsung, !!value);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(usb_charge, 0644,
		   get_usb_charge, set_usb_charge);

static int read_lid_handling(struct samsung_laptop *samsung)
{
	const struct sabi_commands *commands = &samsung->config->commands;
	struct sabi_data data;
	int retval;

	if (commands->get_lid_handling == 0xFFFF)
		return -ENODEV;

	memset(&data, 0, sizeof(data));
	retval = sabi_command(samsung, commands->get_lid_handling,
			      &data, &data);

	if (retval)
		return retval;

	return data.data[0] & 0x1;
}

static int write_lid_handling(struct samsung_laptop *samsung,
			      int enabled)
{
	const struct sabi_commands *commands = &samsung->config->commands;
	struct sabi_data data;

	memset(&data, 0, sizeof(data));
	data.data[0] = 0x80 | enabled;
	return sabi_command(samsung, commands->set_lid_handling,
			    &data, NULL);
}

static ssize_t get_lid_handling(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct samsung_laptop *samsung = dev_get_drvdata(dev);
	int ret;

	ret = read_lid_handling(samsung);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", ret);
}

static ssize_t set_lid_handling(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct samsung_laptop *samsung = dev_get_drvdata(dev);
	int ret, value;

	if (!count || kstrtoint(buf, 0, &value) != 0)
		return -EINVAL;

	ret = write_lid_handling(samsung, !!value);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(lid_handling, 0644,
		   get_lid_handling, set_lid_handling);

static struct attribute *platform_attributes[] = {
	&dev_attr_performance_level.attr,
	&dev_attr_battery_life_extender.attr,
	&dev_attr_usb_charge.attr,
	&dev_attr_lid_handling.attr,
	NULL
};

static int find_signature(void __iomem *memcheck, const char *testStr)
{
	int i = 0;
	int loca;

	for (loca = 0; loca < 0xffff; loca++) {
		char temp = readb(memcheck + loca);

		if (temp == testStr[i]) {
			if (i == strlen(testStr)-1)
				break;
			++i;
		} else {
			i = 0;
		}
	}
	return loca;
}

static void samsung_rfkill_exit(struct samsung_laptop *samsung)
{
	if (samsung->wlan.rfkill) {
		rfkill_unregister(samsung->wlan.rfkill);
		rfkill_destroy(samsung->wlan.rfkill);
		samsung->wlan.rfkill = NULL;
	}
	if (samsung->bluetooth.rfkill) {
		rfkill_unregister(samsung->bluetooth.rfkill);
		rfkill_destroy(samsung->bluetooth.rfkill);
		samsung->bluetooth.rfkill = NULL;
	}
}

static int samsung_new_rfkill(struct samsung_laptop *samsung,
			      struct samsung_rfkill *arfkill,
			      const char *name, enum rfkill_type type,
			      const struct rfkill_ops *ops,
			      int blocked)
{
	struct rfkill **rfkill = &arfkill->rfkill;
	int ret;

	arfkill->type = type;
	arfkill->samsung = samsung;

	*rfkill = rfkill_alloc(name, &samsung->platform_device->dev,
			       type, ops, arfkill);

	if (!*rfkill)
		return -EINVAL;

	if (blocked != -1)
		rfkill_init_sw_state(*rfkill, blocked);

	ret = rfkill_register(*rfkill);
	if (ret) {
		rfkill_destroy(*rfkill);
		*rfkill = NULL;
		return ret;
	}
	return 0;
}

static int __init samsung_rfkill_init_seclinux(struct samsung_laptop *samsung)
{
	return samsung_new_rfkill(samsung, &samsung->wlan, "samsung-wlan",
				  RFKILL_TYPE_WLAN, &seclinux_rfkill_ops, -1);
}

static int __init samsung_rfkill_init_swsmi(struct samsung_laptop *samsung)
{
	struct sabi_data data;
	int ret;

	ret = swsmi_wireless_status(samsung, &data);
	if (ret) {
		/* Some swsmi laptops use the old seclinux way to control
		 * wireless devices */
		if (ret == -EINVAL)
			ret = samsung_rfkill_init_seclinux(samsung);
		return ret;
	}

	/* 0x02 seems to mean that the device is no present/available */

	if (data.data[WL_STATUS_WLAN] != 0x02)
		ret = samsung_new_rfkill(samsung, &samsung->wlan,
					 "samsung-wlan",
					 RFKILL_TYPE_WLAN,
					 &swsmi_rfkill_ops,
					 !data.data[WL_STATUS_WLAN]);
	if (ret)
		goto exit;

	if (data.data[WL_STATUS_BT] != 0x02)
		ret = samsung_new_rfkill(samsung, &samsung->bluetooth,
					 "samsung-bluetooth",
					 RFKILL_TYPE_BLUETOOTH,
					 &swsmi_rfkill_ops,
					 !data.data[WL_STATUS_BT]);
	if (ret)
		goto exit;

exit:
	if (ret)
		samsung_rfkill_exit(samsung);

	return ret;
}

static int __init samsung_rfkill_init(struct samsung_laptop *samsung)
{
	if (samsung->config->sabi_version == 2)
		return samsung_rfkill_init_seclinux(samsung);
	if (samsung->config->sabi_version == 3)
		return samsung_rfkill_init_swsmi(samsung);
	return 0;
}

static void samsung_lid_handling_exit(struct samsung_laptop *samsung)
{
	if (samsung->quirks->lid_handling)
		write_lid_handling(samsung, 0);
}

static int __init samsung_lid_handling_init(struct samsung_laptop *samsung)
{
	int retval = 0;

	if (samsung->quirks->lid_handling)
		retval = write_lid_handling(samsung, 1);

	return retval;
}

static int kbd_backlight_enable(struct samsung_laptop *samsung)
{
	const struct sabi_commands *commands = &samsung->config->commands;
	struct sabi_data data;
	int retval;

	if (commands->kbd_backlight == 0xFFFF)
		return -ENODEV;

	memset(&data, 0, sizeof(data));
	data.d0 = 0xaabb;
	retval = sabi_command(samsung, commands->kbd_backlight,
			      &data, &data);

	if (retval)
		return retval;

	if (data.d0 != 0xccdd)
		return -ENODEV;
	return 0;
}

static int kbd_backlight_read(struct samsung_laptop *samsung)
{
	const struct sabi_commands *commands = &samsung->config->commands;
	struct sabi_data data;
	int retval;

	memset(&data, 0, sizeof(data));
	data.data[0] = 0x81;
	retval = sabi_command(samsung, commands->kbd_backlight,
			      &data, &data);

	if (retval)
		return retval;

	return data.data[0];
}

static int kbd_backlight_write(struct samsung_laptop *samsung, int brightness)
{
	const struct sabi_commands *commands = &samsung->config->commands;
	struct sabi_data data;

	memset(&data, 0, sizeof(data));
	data.d0 = 0x82 | ((brightness & 0xFF) << 8);
	return sabi_command(samsung, commands->kbd_backlight,
			    &data, NULL);
}

static void kbd_led_update(struct work_struct *work)
{
	struct samsung_laptop *samsung;

	samsung = container_of(work, struct samsung_laptop, kbd_led_work);
	kbd_backlight_write(samsung, samsung->kbd_led_wk);
}

static void kbd_led_set(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct samsung_laptop *samsung;

	samsung = container_of(led_cdev, struct samsung_laptop, kbd_led);

	if (value > samsung->kbd_led.max_brightness)
		value = samsung->kbd_led.max_brightness;
	else if (value < 0)
		value = 0;

	samsung->kbd_led_wk = value;
	queue_work(samsung->led_workqueue, &samsung->kbd_led_work);
}

static enum led_brightness kbd_led_get(struct led_classdev *led_cdev)
{
	struct samsung_laptop *samsung;

	samsung = container_of(led_cdev, struct samsung_laptop, kbd_led);
	return kbd_backlight_read(samsung);
}

static void samsung_leds_exit(struct samsung_laptop *samsung)
{
	led_classdev_unregister(&samsung->kbd_led);
	if (samsung->led_workqueue)
		destroy_workqueue(samsung->led_workqueue);
}

static int __init samsung_leds_init(struct samsung_laptop *samsung)
{
	int ret = 0;

	samsung->led_workqueue = create_singlethread_workqueue("led_workqueue");
	if (!samsung->led_workqueue)
		return -ENOMEM;

	if (kbd_backlight_enable(samsung) >= 0) {
		INIT_WORK(&samsung->kbd_led_work, kbd_led_update);

		samsung->kbd_led.name = "samsung::kbd_backlight";
		samsung->kbd_led.brightness_set = kbd_led_set;
		samsung->kbd_led.brightness_get = kbd_led_get;
		samsung->kbd_led.max_brightness = 8;
		if (samsung->quirks->four_kbd_backlight_levels)
			samsung->kbd_led.max_brightness = 4;

		ret = led_classdev_register(&samsung->platform_device->dev,
					   &samsung->kbd_led);
	}

	if (ret)
		samsung_leds_exit(samsung);

	return ret;
}

static void samsung_backlight_exit(struct samsung_laptop *samsung)
{
	if (samsung->backlight_device) {
		backlight_device_unregister(samsung->backlight_device);
		samsung->backlight_device = NULL;
	}
}

static int __init samsung_backlight_init(struct samsung_laptop *samsung)
{
	struct backlight_device *bd;
	struct backlight_properties props;

	if (!samsung->handle_backlight)
		return 0;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = samsung->config->max_brightness -
		samsung->config->min_brightness;

	bd = backlight_device_register("samsung",
				       &samsung->platform_device->dev,
				       samsung, &backlight_ops,
				       &props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	samsung->backlight_device = bd;
	samsung->backlight_device->props.brightness = read_brightness(samsung);
	samsung->backlight_device->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(samsung->backlight_device);

	return 0;
}

static umode_t samsung_sysfs_is_visible(struct kobject *kobj,
					struct attribute *attr, int idx)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct samsung_laptop *samsung = dev_get_drvdata(dev);
	bool ok = true;

	if (attr == &dev_attr_performance_level.attr)
		ok = !!samsung->config->performance_levels[0].name;
	if (attr == &dev_attr_battery_life_extender.attr)
		ok = !!(read_battery_life_extender(samsung) >= 0);
	if (attr == &dev_attr_usb_charge.attr)
		ok = !!(read_usb_charge(samsung) >= 0);
	if (attr == &dev_attr_lid_handling.attr)
		ok = !!(read_lid_handling(samsung) >= 0);

	return ok ? attr->mode : 0;
}

static const struct attribute_group platform_attribute_group = {
	.is_visible = samsung_sysfs_is_visible,
	.attrs = platform_attributes
};

static void samsung_sysfs_exit(struct samsung_laptop *samsung)
{
	struct platform_device *device = samsung->platform_device;

	sysfs_remove_group(&device->dev.kobj, &platform_attribute_group);
}

static int __init samsung_sysfs_init(struct samsung_laptop *samsung)
{
	struct platform_device *device = samsung->platform_device;

	return sysfs_create_group(&device->dev.kobj, &platform_attribute_group);

}

static int samsung_laptop_call_show(struct seq_file *m, void *data)
{
	struct samsung_laptop *samsung = m->private;
	struct sabi_data *sdata = &samsung->debug.data;
	int ret;

	seq_printf(m, "SABI 0x%04x {0x%08x, 0x%08x, 0x%04x, 0x%02x}\n",
		   samsung->debug.command,
		   sdata->d0, sdata->d1, sdata->d2, sdata->d3);

	ret = sabi_command(samsung, samsung->debug.command, sdata, sdata);

	if (ret) {
		seq_printf(m, "SABI command 0x%04x failed\n",
			   samsung->debug.command);
		return ret;
	}

	seq_printf(m, "SABI {0x%08x, 0x%08x, 0x%04x, 0x%02x}\n",
		   sdata->d0, sdata->d1, sdata->d2, sdata->d3);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(samsung_laptop_call);

static void samsung_debugfs_exit(struct samsung_laptop *samsung)
{
	debugfs_remove_recursive(samsung->debug.root);
}

static void samsung_debugfs_init(struct samsung_laptop *samsung)
{
	struct dentry *root;

	root = debugfs_create_dir("samsung-laptop", NULL);
	samsung->debug.root = root;

	samsung->debug.f0000_wrapper.data = samsung->f0000_segment;
	samsung->debug.f0000_wrapper.size = 0xffff;

	samsung->debug.data_wrapper.data = &samsung->debug.data;
	samsung->debug.data_wrapper.size = sizeof(samsung->debug.data);

	samsung->debug.sdiag_wrapper.data = samsung->sdiag;
	samsung->debug.sdiag_wrapper.size = strlen(samsung->sdiag);

	debugfs_create_u16("command", 0644, root, &samsung->debug.command);
	debugfs_create_u32("d0", 0644, root, &samsung->debug.data.d0);
	debugfs_create_u32("d1", 0644, root, &samsung->debug.data.d1);
	debugfs_create_u16("d2", 0644, root, &samsung->debug.data.d2);
	debugfs_create_u8("d3", 0644, root, &samsung->debug.data.d3);
	debugfs_create_blob("data", 0444, root, &samsung->debug.data_wrapper);
	debugfs_create_blob("f0000_segment", 0400, root,
			    &samsung->debug.f0000_wrapper);
	debugfs_create_file("call", 0444, root, samsung,
			    &samsung_laptop_call_fops);
	debugfs_create_blob("sdiag", 0444, root, &samsung->debug.sdiag_wrapper);
}

static void samsung_sabi_exit(struct samsung_laptop *samsung)
{
	const struct sabi_config *config = samsung->config;

	/* Turn off "Linux" mode in the BIOS */
	if (config && config->commands.set_linux != 0xff)
		sabi_set_commandb(samsung, config->commands.set_linux, 0x80);

	if (samsung->sabi_iface) {
		iounmap(samsung->sabi_iface);
		samsung->sabi_iface = NULL;
	}
	if (samsung->f0000_segment) {
		iounmap(samsung->f0000_segment);
		samsung->f0000_segment = NULL;
	}

	samsung->config = NULL;
}

static __init void samsung_sabi_infos(struct samsung_laptop *samsung, int loca,
				      unsigned int ifaceP)
{
	const struct sabi_config *config = samsung->config;

	printk(KERN_DEBUG "This computer supports SABI==%x\n",
	       loca + 0xf0000 - 6);

	printk(KERN_DEBUG "SABI header:\n");
	printk(KERN_DEBUG " SMI Port Number = 0x%04x\n",
	       readw(samsung->sabi + config->header_offsets.port));
	printk(KERN_DEBUG " SMI Interface Function = 0x%02x\n",
	       readb(samsung->sabi + config->header_offsets.iface_func));
	printk(KERN_DEBUG " SMI enable memory buffer = 0x%02x\n",
	       readb(samsung->sabi + config->header_offsets.en_mem));
	printk(KERN_DEBUG " SMI restore memory buffer = 0x%02x\n",
	       readb(samsung->sabi + config->header_offsets.re_mem));
	printk(KERN_DEBUG " SABI data offset = 0x%04x\n",
	       readw(samsung->sabi + config->header_offsets.data_offset));
	printk(KERN_DEBUG " SABI data segment = 0x%04x\n",
	       readw(samsung->sabi + config->header_offsets.data_segment));

	printk(KERN_DEBUG " SABI pointer = 0x%08x\n", ifaceP);
}

static void __init samsung_sabi_diag(struct samsung_laptop *samsung)
{
	int loca = find_signature(samsung->f0000_segment, "SDiaG@");
	int i;

	if (loca == 0xffff)
		return ;

	/* Example:
	 * Ident: @SDiaG@686XX-N90X3A/966-SEC-07HL-S90X3A
	 *
	 * Product name: 90X3A
	 * BIOS Version: 07HL
	 */
	loca += 1;
	for (i = 0; loca < 0xffff && i < sizeof(samsung->sdiag) - 1; loca++) {
		char temp = readb(samsung->f0000_segment + loca);

		if (isalnum(temp) || temp == '/' || temp == '-')
			samsung->sdiag[i++] = temp;
		else
			break ;
	}

	if (debug && samsung->sdiag[0])
		pr_info("sdiag: %s", samsung->sdiag);
}

static int __init samsung_sabi_init(struct samsung_laptop *samsung)
{
	const struct sabi_config *config = NULL;
	const struct sabi_commands *commands;
	unsigned int ifaceP;
	int loca = 0xffff;
	int ret = 0;
	int i;

	samsung->f0000_segment = ioremap(0xf0000, 0xffff);
	if (!samsung->f0000_segment) {
		if (debug || force)
			pr_err("Can't map the segment at 0xf0000\n");
		ret = -EINVAL;
		goto exit;
	}

	samsung_sabi_diag(samsung);

	/* Try to find one of the signatures in memory to find the header */
	for (i = 0; sabi_configs[i].test_string != NULL; ++i) {
		samsung->config = &sabi_configs[i];
		loca = find_signature(samsung->f0000_segment,
				      samsung->config->test_string);
		if (loca != 0xffff)
			break;
	}

	if (loca == 0xffff) {
		if (debug || force)
			pr_err("This computer does not support SABI\n");
		ret = -ENODEV;
		goto exit;
	}

	config = samsung->config;
	commands = &config->commands;

	/* point to the SMI port Number */
	loca += 1;
	samsung->sabi = (samsung->f0000_segment + loca);

	/* Get a pointer to the SABI Interface */
	ifaceP = (readw(samsung->sabi + config->header_offsets.data_segment) & 0x0ffff) << 4;
	ifaceP += readw(samsung->sabi + config->header_offsets.data_offset) & 0x0ffff;

	if (debug)
		samsung_sabi_infos(samsung, loca, ifaceP);

	samsung->sabi_iface = ioremap(ifaceP, 16);
	if (!samsung->sabi_iface) {
		pr_err("Can't remap %x\n", ifaceP);
		ret = -EINVAL;
		goto exit;
	}

	/* Turn on "Linux" mode in the BIOS */
	if (commands->set_linux != 0xff) {
		int retval = sabi_set_commandb(samsung,
					       commands->set_linux, 0x81);
		if (retval) {
			pr_warn("Linux mode was not set!\n");
			ret = -ENODEV;
			goto exit;
		}
	}

	/* Check for stepping quirk */
	if (samsung->handle_backlight)
		check_for_stepping_quirk(samsung);

	pr_info("detected SABI interface: %s\n",
		samsung->config->test_string);

exit:
	if (ret)
		samsung_sabi_exit(samsung);

	return ret;
}

static void samsung_platform_exit(struct samsung_laptop *samsung)
{
	if (samsung->platform_device) {
		platform_device_unregister(samsung->platform_device);
		samsung->platform_device = NULL;
	}
}

static int samsung_pm_notification(struct notifier_block *nb,
				   unsigned long val, void *ptr)
{
	struct samsung_laptop *samsung;

	samsung = container_of(nb, struct samsung_laptop, pm_nb);
	if (val == PM_POST_HIBERNATION &&
	    samsung->quirks->enable_kbd_backlight)
		kbd_backlight_enable(samsung);

	if (val == PM_POST_HIBERNATION && samsung->quirks->lid_handling)
		write_lid_handling(samsung, 1);

	return 0;
}

static int __init samsung_platform_init(struct samsung_laptop *samsung)
{
	struct platform_device *pdev;

	pdev = platform_device_register_simple("samsung", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	samsung->platform_device = pdev;
	platform_set_drvdata(samsung->platform_device, samsung);
	return 0;
}

static struct samsung_quirks *quirks;

static int __init samsung_dmi_matched(const struct dmi_system_id *d)
{
	quirks = d->driver_data;
	return 0;
}

static const struct dmi_system_id samsung_dmi_table[] __initconst = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_CHASSIS_TYPE, "8"), /* Portable */
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_CHASSIS_TYPE, "9"), /* Laptop */
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_CHASSIS_TYPE, "10"), /* Notebook */
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_CHASSIS_TYPE, "14"), /* Sub-Notebook */
		},
	},
	/* DMI ids for laptops with bad Chassis Type */
	{
	  .ident = "R40/R41",
	  .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "R40/R41"),
		DMI_MATCH(DMI_BOARD_NAME, "R40/R41"),
		},
	},
	/* Specific DMI ids for laptop with quirks */
	{
	 .callback = samsung_dmi_matched,
	 .ident = "N150P",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "N150P"),
		DMI_MATCH(DMI_BOARD_NAME, "N150P"),
		},
	 .driver_data = &samsung_use_native_backlight,
	},
	{
	 .callback = samsung_dmi_matched,
	 .ident = "N145P/N250P/N260P",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "N145P/N250P/N260P"),
		DMI_MATCH(DMI_BOARD_NAME, "N145P/N250P/N260P"),
		},
	 .driver_data = &samsung_use_native_backlight,
	},
	{
	 .callback = samsung_dmi_matched,
	 .ident = "N150/N210/N220",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "N150/N210/N220"),
		DMI_MATCH(DMI_BOARD_NAME, "N150/N210/N220"),
		},
	 .driver_data = &samsung_broken_acpi_video,
	},
	{
	 .callback = samsung_dmi_matched,
	 .ident = "NF110/NF210/NF310",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "NF110/NF210/NF310"),
		DMI_MATCH(DMI_BOARD_NAME, "NF110/NF210/NF310"),
		},
	 .driver_data = &samsung_broken_acpi_video,
	},
	{
	 .callback = samsung_dmi_matched,
	 .ident = "X360",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "X360"),
		DMI_MATCH(DMI_BOARD_NAME, "X360"),
		},
	 .driver_data = &samsung_broken_acpi_video,
	},
	{
	 .callback = samsung_dmi_matched,
	 .ident = "N250P",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "N250P"),
		DMI_MATCH(DMI_BOARD_NAME, "N250P"),
		},
	 .driver_data = &samsung_use_native_backlight,
	},
	{
	 .callback = samsung_dmi_matched,
	 .ident = "NC210",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "NC210/NC110"),
		DMI_MATCH(DMI_BOARD_NAME, "NC210/NC110"),
		},
	 .driver_data = &samsung_broken_acpi_video,
	},
	{
	 .callback = samsung_dmi_matched,
	 .ident = "730U3E/740U3E",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "730U3E/740U3E"),
		},
	 .driver_data = &samsung_np740u3e,
	},
	{
	 .callback = samsung_dmi_matched,
	 .ident = "300V3Z/300V4Z/300V5Z",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "300V3Z/300V4Z/300V5Z"),
		},
	 .driver_data = &samsung_lid_handling,
	},
	{ },
};
MODULE_DEVICE_TABLE(dmi, samsung_dmi_table);

static struct platform_device *samsung_platform_device;

static int __init samsung_init(void)
{
	struct samsung_laptop *samsung;
	int ret;

	if (efi_enabled(EFI_BOOT))
		return -ENODEV;

	quirks = &samsung_unknown;
	if (!force && !dmi_check_system(samsung_dmi_table))
		return -ENODEV;

	samsung = kzalloc(sizeof(*samsung), GFP_KERNEL);
	if (!samsung)
		return -ENOMEM;

	mutex_init(&samsung->sabi_mutex);
	samsung->handle_backlight = true;
	samsung->quirks = quirks;

#ifdef CONFIG_ACPI
	if (samsung->quirks->broken_acpi_video)
		acpi_video_set_dmi_backlight_type(acpi_backlight_vendor);
	if (samsung->quirks->use_native_backlight)
		acpi_video_set_dmi_backlight_type(acpi_backlight_native);

	if (acpi_video_get_backlight_type() != acpi_backlight_vendor)
		samsung->handle_backlight = false;
#endif

	ret = samsung_platform_init(samsung);
	if (ret)
		goto error_platform;

	ret = samsung_sabi_init(samsung);
	if (ret)
		goto error_sabi;

	ret = samsung_sysfs_init(samsung);
	if (ret)
		goto error_sysfs;

	ret = samsung_backlight_init(samsung);
	if (ret)
		goto error_backlight;

	ret = samsung_rfkill_init(samsung);
	if (ret)
		goto error_rfkill;

	ret = samsung_leds_init(samsung);
	if (ret)
		goto error_leds;

	ret = samsung_lid_handling_init(samsung);
	if (ret)
		goto error_lid_handling;

	samsung_debugfs_init(samsung);

	samsung->pm_nb.notifier_call = samsung_pm_notification;
	register_pm_notifier(&samsung->pm_nb);

	samsung_platform_device = samsung->platform_device;
	return ret;

error_lid_handling:
	samsung_leds_exit(samsung);
error_leds:
	samsung_rfkill_exit(samsung);
error_rfkill:
	samsung_backlight_exit(samsung);
error_backlight:
	samsung_sysfs_exit(samsung);
error_sysfs:
	samsung_sabi_exit(samsung);
error_sabi:
	samsung_platform_exit(samsung);
error_platform:
	kfree(samsung);
	return ret;
}

static void __exit samsung_exit(void)
{
	struct samsung_laptop *samsung;

	samsung = platform_get_drvdata(samsung_platform_device);
	unregister_pm_notifier(&samsung->pm_nb);

	samsung_debugfs_exit(samsung);
	samsung_lid_handling_exit(samsung);
	samsung_leds_exit(samsung);
	samsung_rfkill_exit(samsung);
	samsung_backlight_exit(samsung);
	samsung_sysfs_exit(samsung);
	samsung_sabi_exit(samsung);
	samsung_platform_exit(samsung);

	kfree(samsung);
	samsung_platform_device = NULL;
}

module_init(samsung_init);
module_exit(samsung_exit);

MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@suse.de>");
MODULE_DESCRIPTION("Samsung Backlight driver");
MODULE_LICENSE("GPL");
