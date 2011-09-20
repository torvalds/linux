/*
 * Samsung Laptop driver
 *
 * Copyright (C) 2009,2011 Greg Kroah-Hartman (gregkh@suse.de)
 * Copyright (C) 2009,2011 Novell Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/dmi.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>

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

/* Structure to get data back to the calling function */
struct sabi_retval {
	u8 retval[20];
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
	u8 get_brightness;
	u8 set_brightness;

	/*
	 * first byte:
	 * 0x00 - wireless is off
	 * 0x01 - wireless is on
	 * second byte:
	 * 0x02 - 3G is off
	 * 0x03 - 3G is on
	 * TODO, verify 3G is correct, that doesn't seem right...
	 */
	u8 get_wireless_button;
	u8 set_wireless_button;

	/* 0 is off, 1 is on */
	u8 get_backlight;
	u8 set_backlight;

	/*
	 * 0x80 or 0x00 - no action
	 * 0x81 - recovery key pressed
	 */
	u8 get_recovery_mode;
	u8 set_recovery_mode;

	/*
	 * on seclinux: 0 is low, 1 is high,
	 * on swsmi: 0 is normal, 1 is silent, 2 is turbo
	 */
	u8 get_performance_level;
	u8 set_performance_level;

	/*
	 * Tell the BIOS that Linux is running on this machine.
	 * 81 is on, 80 is off
	 */
	u8 set_linux;
};

struct sabi_performance_level {
	const char *name;
	u8 value;
};

struct sabi_config {
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

static const struct sabi_config *sabi_config;

static void __iomem *sabi;
static void __iomem *sabi_iface;
static void __iomem *f0000_segment;
static struct backlight_device *backlight_device;
static struct mutex sabi_mutex;
static struct platform_device *sdev;
static struct rfkill *rfk;

static int force;
module_param(force, bool, 0);
MODULE_PARM_DESC(force,
		"Disable the DMI check and forces the driver to be loaded");

static int debug;
module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");

static int sabi_get_command(u8 command, struct sabi_retval *sretval)
{
	int retval = 0;
	u16 port = readw(sabi + sabi_config->header_offsets.port);
	u8 complete, iface_data;

	mutex_lock(&sabi_mutex);

	/* enable memory to be able to write to it */
	outb(readb(sabi + sabi_config->header_offsets.en_mem), port);

	/* write out the command */
	writew(sabi_config->main_function, sabi_iface + SABI_IFACE_MAIN);
	writew(command, sabi_iface + SABI_IFACE_SUB);
	writeb(0, sabi_iface + SABI_IFACE_COMPLETE);
	outb(readb(sabi + sabi_config->header_offsets.iface_func), port);

	/* write protect memory to make it safe */
	outb(readb(sabi + sabi_config->header_offsets.re_mem), port);

	/* see if the command actually succeeded */
	complete = readb(sabi_iface + SABI_IFACE_COMPLETE);
	iface_data = readb(sabi_iface + SABI_IFACE_DATA);
	if (complete != 0xaa || iface_data == 0xff) {
		pr_warn("SABI get command 0x%02x failed with completion flag 0x%02x and data 0x%02x\n",
		        command, complete, iface_data);
		retval = -EINVAL;
		goto exit;
	}
	/*
	 * Save off the data into a structure so the caller use it.
	 * Right now we only want the first 4 bytes,
	 * There are commands that need more, but not for the ones we
	 * currently care about.
	 */
	sretval->retval[0] = readb(sabi_iface + SABI_IFACE_DATA);
	sretval->retval[1] = readb(sabi_iface + SABI_IFACE_DATA + 1);
	sretval->retval[2] = readb(sabi_iface + SABI_IFACE_DATA + 2);
	sretval->retval[3] = readb(sabi_iface + SABI_IFACE_DATA + 3);

exit:
	mutex_unlock(&sabi_mutex);
	return retval;

}

static int sabi_set_command(u8 command, u8 data)
{
	int retval = 0;
	u16 port = readw(sabi + sabi_config->header_offsets.port);
	u8 complete, iface_data;

	mutex_lock(&sabi_mutex);

	/* enable memory to be able to write to it */
	outb(readb(sabi + sabi_config->header_offsets.en_mem), port);

	/* write out the command */
	writew(sabi_config->main_function, sabi_iface + SABI_IFACE_MAIN);
	writew(command, sabi_iface + SABI_IFACE_SUB);
	writeb(0, sabi_iface + SABI_IFACE_COMPLETE);
	writeb(data, sabi_iface + SABI_IFACE_DATA);
	outb(readb(sabi + sabi_config->header_offsets.iface_func), port);

	/* write protect memory to make it safe */
	outb(readb(sabi + sabi_config->header_offsets.re_mem), port);

	/* see if the command actually succeeded */
	complete = readb(sabi_iface + SABI_IFACE_COMPLETE);
	iface_data = readb(sabi_iface + SABI_IFACE_DATA);
	if (complete != 0xaa || iface_data == 0xff) {
		pr_warn("SABI set command 0x%02x failed with completion flag 0x%02x and data 0x%02x\n",
		       command, complete, iface_data);
		retval = -EINVAL;
	}

	mutex_unlock(&sabi_mutex);
	return retval;
}

static void test_backlight(void)
{
	struct sabi_retval sretval;

	sabi_get_command(sabi_config->commands.get_backlight, &sretval);
	printk(KERN_DEBUG "backlight = 0x%02x\n", sretval.retval[0]);

	sabi_set_command(sabi_config->commands.set_backlight, 0);
	printk(KERN_DEBUG "backlight should be off\n");

	sabi_get_command(sabi_config->commands.get_backlight, &sretval);
	printk(KERN_DEBUG "backlight = 0x%02x\n", sretval.retval[0]);

	msleep(1000);

	sabi_set_command(sabi_config->commands.set_backlight, 1);
	printk(KERN_DEBUG "backlight should be on\n");

	sabi_get_command(sabi_config->commands.get_backlight, &sretval);
	printk(KERN_DEBUG "backlight = 0x%02x\n", sretval.retval[0]);
}

static void test_wireless(void)
{
	struct sabi_retval sretval;

	sabi_get_command(sabi_config->commands.get_wireless_button, &sretval);
	printk(KERN_DEBUG "wireless led = 0x%02x\n", sretval.retval[0]);

	sabi_set_command(sabi_config->commands.set_wireless_button, 0);
	printk(KERN_DEBUG "wireless led should be off\n");

	sabi_get_command(sabi_config->commands.get_wireless_button, &sretval);
	printk(KERN_DEBUG "wireless led = 0x%02x\n", sretval.retval[0]);

	msleep(1000);

	sabi_set_command(sabi_config->commands.set_wireless_button, 1);
	printk(KERN_DEBUG "wireless led should be on\n");

	sabi_get_command(sabi_config->commands.get_wireless_button, &sretval);
	printk(KERN_DEBUG "wireless led = 0x%02x\n", sretval.retval[0]);
}

static u8 read_brightness(void)
{
	struct sabi_retval sretval;
	int user_brightness = 0;
	int retval;

	retval = sabi_get_command(sabi_config->commands.get_brightness,
				  &sretval);
	if (!retval) {
		user_brightness = sretval.retval[0];
		if (user_brightness != 0)
			user_brightness -= sabi_config->min_brightness;
	}
	return user_brightness;
}

static void set_brightness(u8 user_brightness)
{
	u8 user_level = user_brightness - sabi_config->min_brightness;

	sabi_set_command(sabi_config->commands.set_brightness, user_level);
}

static int get_brightness(struct backlight_device *bd)
{
	return (int)read_brightness();
}

static int update_status(struct backlight_device *bd)
{
	set_brightness(bd->props.brightness);

	if (bd->props.power == FB_BLANK_UNBLANK)
		sabi_set_command(sabi_config->commands.set_backlight, 1);
	else
		sabi_set_command(sabi_config->commands.set_backlight, 0);
	return 0;
}

static const struct backlight_ops backlight_ops = {
	.get_brightness	= get_brightness,
	.update_status	= update_status,
};

static int rfkill_set(void *data, bool blocked)
{
	/* Do something with blocked...*/
	/*
	 * blocked == false is on
	 * blocked == true is off
	 */
	if (blocked)
		sabi_set_command(sabi_config->commands.set_wireless_button, 0);
	else
		sabi_set_command(sabi_config->commands.set_wireless_button, 1);

	return 0;
}

static struct rfkill_ops rfkill_ops = {
	.set_block = rfkill_set,
};

static int init_wireless(struct platform_device *sdev)
{
	int retval;

	rfk = rfkill_alloc("samsung-wifi", &sdev->dev, RFKILL_TYPE_WLAN,
			   &rfkill_ops, NULL);
	if (!rfk)
		return -ENOMEM;

	retval = rfkill_register(rfk);
	if (retval) {
		rfkill_destroy(rfk);
		return -ENODEV;
	}

	return 0;
}

static void destroy_wireless(void)
{
	rfkill_unregister(rfk);
	rfkill_destroy(rfk);
}

static ssize_t get_performance_level(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct sabi_retval sretval;
	int retval;
	int i;

	/* Read the state */
	retval = sabi_get_command(sabi_config->commands.get_performance_level,
				  &sretval);
	if (retval)
		return retval;

	/* The logic is backwards, yeah, lots of fun... */
	for (i = 0; sabi_config->performance_levels[i].name; ++i) {
		if (sretval.retval[0] == sabi_config->performance_levels[i].value)
			return sprintf(buf, "%s\n", sabi_config->performance_levels[i].name);
	}
	return sprintf(buf, "%s\n", "unknown");
}

static ssize_t set_performance_level(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	if (count >= 1) {
		int i;
		for (i = 0; sabi_config->performance_levels[i].name; ++i) {
			const struct sabi_performance_level *level =
				&sabi_config->performance_levels[i];
			if (!strncasecmp(level->name, buf, strlen(level->name))) {
				sabi_set_command(sabi_config->commands.set_performance_level,
						 level->value);
				break;
			}
		}
		if (!sabi_config->performance_levels[i].name)
			return -EINVAL;
	}
	return count;
}
static DEVICE_ATTR(performance_level, S_IWUSR | S_IRUGO,
		   get_performance_level, set_performance_level);


static int __init dmi_check_cb(const struct dmi_system_id *id)
{
	pr_info("found laptop model '%s'\n",
		id->ident);
	return 1;
}

static struct dmi_system_id __initdata samsung_dmi_table[] = {
	{
		.ident = "N128",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "N128"),
			DMI_MATCH(DMI_BOARD_NAME, "N128"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "N130",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "N130"),
			DMI_MATCH(DMI_BOARD_NAME, "N130"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "X125",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "X125"),
			DMI_MATCH(DMI_BOARD_NAME, "X125"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "X120/X170",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "X120/X170"),
			DMI_MATCH(DMI_BOARD_NAME, "X120/X170"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "NC10",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "NC10"),
			DMI_MATCH(DMI_BOARD_NAME, "NC10"),
		},
		.callback = dmi_check_cb,
	},
		{
		.ident = "NP-Q45",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "SQ45S70S"),
			DMI_MATCH(DMI_BOARD_NAME, "SQ45S70S"),
		},
		.callback = dmi_check_cb,
		},
	{
		.ident = "X360",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "X360"),
			DMI_MATCH(DMI_BOARD_NAME, "X360"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "R410 Plus",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "R410P"),
			DMI_MATCH(DMI_BOARD_NAME, "R460"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "R518",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "R518"),
			DMI_MATCH(DMI_BOARD_NAME, "R518"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "R519/R719",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "R519/R719"),
			DMI_MATCH(DMI_BOARD_NAME, "R519/R719"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "N150/N210/N220",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "N150/N210/N220"),
			DMI_MATCH(DMI_BOARD_NAME, "N150/N210/N220"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "N150/N210/N220/N230",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "N150/N210/N220/N230"),
			DMI_MATCH(DMI_BOARD_NAME, "N150/N210/N220/N230"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "N150P/N210P/N220P",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "N150P/N210P/N220P"),
			DMI_MATCH(DMI_BOARD_NAME, "N150P/N210P/N220P"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "R700",
		.matches = {
		      DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		      DMI_MATCH(DMI_PRODUCT_NAME, "SR700"),
		      DMI_MATCH(DMI_BOARD_NAME, "SR700"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "R530/R730",
		.matches = {
		      DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		      DMI_MATCH(DMI_PRODUCT_NAME, "R530/R730"),
		      DMI_MATCH(DMI_BOARD_NAME, "R530/R730"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "NF110/NF210/NF310",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "NF110/NF210/NF310"),
			DMI_MATCH(DMI_BOARD_NAME, "NF110/NF210/NF310"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "N145P/N250P/N260P",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "N145P/N250P/N260P"),
			DMI_MATCH(DMI_BOARD_NAME, "N145P/N250P/N260P"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "R70/R71",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR,
					"SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "R70/R71"),
			DMI_MATCH(DMI_BOARD_NAME, "R70/R71"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "P460",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "P460"),
			DMI_MATCH(DMI_BOARD_NAME, "P460"),
		},
		.callback = dmi_check_cb,
	},
		{
		.ident = "X520",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "X520"),
			DMI_MATCH(DMI_BOARD_NAME, "X520"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "R528/R728",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "R528/R728"),
			DMI_MATCH(DMI_BOARD_NAME, "R528/R728"),
		},
		.callback = dmi_check_cb,
	},
	{ },
};
MODULE_DEVICE_TABLE(dmi, samsung_dmi_table);

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

static int __init samsung_init(void)
{
	struct backlight_properties props;
	struct sabi_retval sretval;
	unsigned int ifaceP;
	int i;
	int loca;
	int retval;

	mutex_init(&sabi_mutex);

	if (!force && !dmi_check_system(samsung_dmi_table))
		return -ENODEV;

	f0000_segment = ioremap_nocache(0xf0000, 0xffff);
	if (!f0000_segment) {
		pr_err("Can't map the segment at 0xf0000\n");
		return -EINVAL;
	}

	/* Try to find one of the signatures in memory to find the header */
	for (i = 0; sabi_configs[i].test_string != 0; ++i) {
		sabi_config = &sabi_configs[i];
		loca = find_signature(f0000_segment, sabi_config->test_string);
		if (loca != 0xffff)
			break;
	}

	if (loca == 0xffff) {
		pr_err("This computer does not support SABI\n");
		goto error_no_signature;
	}

	/* point to the SMI port Number */
	loca += 1;
	sabi = (f0000_segment + loca);

	if (debug) {
		printk(KERN_DEBUG "This computer supports SABI==%x\n",
			loca + 0xf0000 - 6);
		printk(KERN_DEBUG "SABI header:\n");
		printk(KERN_DEBUG " SMI Port Number = 0x%04x\n",
			readw(sabi + sabi_config->header_offsets.port));
		printk(KERN_DEBUG " SMI Interface Function = 0x%02x\n",
			readb(sabi + sabi_config->header_offsets.iface_func));
		printk(KERN_DEBUG " SMI enable memory buffer = 0x%02x\n",
			readb(sabi + sabi_config->header_offsets.en_mem));
		printk(KERN_DEBUG " SMI restore memory buffer = 0x%02x\n",
			readb(sabi + sabi_config->header_offsets.re_mem));
		printk(KERN_DEBUG " SABI data offset = 0x%04x\n",
			readw(sabi + sabi_config->header_offsets.data_offset));
		printk(KERN_DEBUG " SABI data segment = 0x%04x\n",
			readw(sabi + sabi_config->header_offsets.data_segment));
	}

	/* Get a pointer to the SABI Interface */
	ifaceP = (readw(sabi + sabi_config->header_offsets.data_segment) & 0x0ffff) << 4;
	ifaceP += readw(sabi + sabi_config->header_offsets.data_offset) & 0x0ffff;
	sabi_iface = ioremap_nocache(ifaceP, 16);
	if (!sabi_iface) {
		pr_err("Can't remap %x\n", ifaceP);
		goto exit;
	}
	if (debug) {
		printk(KERN_DEBUG "ifaceP = 0x%08x\n", ifaceP);
		printk(KERN_DEBUG "sabi_iface = %p\n", sabi_iface);

		test_backlight();
		test_wireless();

		retval = sabi_get_command(sabi_config->commands.get_brightness,
					  &sretval);
		printk(KERN_DEBUG "brightness = 0x%02x\n", sretval.retval[0]);
	}

	/* Turn on "Linux" mode in the BIOS */
	if (sabi_config->commands.set_linux != 0xff) {
		retval = sabi_set_command(sabi_config->commands.set_linux,
					  0x81);
		if (retval) {
			pr_warn("Linux mode was not set!\n");
			goto error_no_platform;
		}
	}

	/* knock up a platform device to hang stuff off of */
	sdev = platform_device_register_simple("samsung", -1, NULL, 0);
	if (IS_ERR(sdev))
		goto error_no_platform;

	/* create a backlight device to talk to this one */
	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = sabi_config->max_brightness;
	backlight_device = backlight_device_register("samsung", &sdev->dev,
						     NULL, &backlight_ops,
						     &props);
	if (IS_ERR(backlight_device))
		goto error_no_backlight;

	backlight_device->props.brightness = read_brightness();
	backlight_device->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(backlight_device);

	retval = init_wireless(sdev);
	if (retval)
		goto error_no_rfk;

	retval = device_create_file(&sdev->dev, &dev_attr_performance_level);
	if (retval)
		goto error_file_create;

exit:
	return 0;

error_file_create:
	destroy_wireless();

error_no_rfk:
	backlight_device_unregister(backlight_device);

error_no_backlight:
	platform_device_unregister(sdev);

error_no_platform:
	iounmap(sabi_iface);

error_no_signature:
	iounmap(f0000_segment);
	return -EINVAL;
}

static void __exit samsung_exit(void)
{
	/* Turn off "Linux" mode in the BIOS */
	if (sabi_config->commands.set_linux != 0xff)
		sabi_set_command(sabi_config->commands.set_linux, 0x80);

	device_remove_file(&sdev->dev, &dev_attr_performance_level);
	backlight_device_unregister(backlight_device);
	destroy_wireless();
	iounmap(sabi_iface);
	iounmap(f0000_segment);
	platform_device_unregister(sdev);
}

module_init(samsung_init);
module_exit(samsung_exit);

MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@suse.de>");
MODULE_DESCRIPTION("Samsung Backlight driver");
MODULE_LICENSE("GPL");
