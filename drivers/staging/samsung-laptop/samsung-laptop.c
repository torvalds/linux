/*
 * Samsung N130 Laptop driver
 *
 * Copyright (C) 2009 Greg Kroah-Hartman (gregkh@suse.de)
 * Copyright (C) 2009 Novell Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */
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

/* Brightness is 0 - 8, as described above.  Value 0 is for the BIOS to use */
#define GET_BRIGHTNESS			0x00
#define SET_BRIGHTNESS			0x01

/* first byte:
 * 0x00 - wireless is off
 * 0x01 - wireless is on
 * second byte:
 * 0x02 - 3G is off
 * 0x03 - 3G is on
 * TODO, verify 3G is correct, that doesn't seem right...
 */
#define GET_WIRELESS_BUTTON		0x02
#define SET_WIRELESS_BUTTON		0x03

/* 0 is off, 1 is on */
#define GET_BACKLIGHT			0x04
#define SET_BACKLIGHT			0x05

/*
 * 0x80 or 0x00 - no action
 * 0x81 - recovery key pressed
 */
#define GET_RECOVERY_METHOD		0x06
#define SET_RECOVERY_METHOD		0x07

/* 0 is low, 1 is high */
#define GET_PERFORMANCE_LEVEL		0x08
#define SET_PERFORMANCE_LEVEL		0x09

/*
 * Tell the BIOS that Linux is running on this machine.
 * 81 is on, 80 is off
 */
#define SET_LINUX			0x0a


#define MAIN_FUNCTION			0x4c49

#define SABI_HEADER_PORT		0x00
#define SABI_HEADER_RE_MEM		0x02
#define SABI_HEADER_IFACEFUNC		0x03
#define SABI_HEADER_EN_MEM		0x04
#define SABI_HEADER_DATA_OFFSET		0x05
#define SABI_HEADER_DATA_SEGMENT	0x07

#define SABI_IFACE_MAIN			0x00
#define SABI_IFACE_SUB			0x02
#define SABI_IFACE_COMPLETE		0x04
#define SABI_IFACE_DATA			0x05

/* Structure to get data back to the calling function */
struct sabi_retval {
	u8 retval[20];
};

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
	u16 port = readw(sabi + SABI_HEADER_PORT);

	mutex_lock(&sabi_mutex);

	/* enable memory to be able to write to it */
	outb(readb(sabi + SABI_HEADER_EN_MEM), port);

	/* write out the command */
	writew(MAIN_FUNCTION, sabi_iface + SABI_IFACE_MAIN);
	writew(command, sabi_iface + SABI_IFACE_SUB);
	writeb(0, sabi_iface + SABI_IFACE_COMPLETE);
	outb(readb(sabi + SABI_HEADER_IFACEFUNC), port);

	/* write protect memory to make it safe */
	outb(readb(sabi + SABI_HEADER_RE_MEM), port);

	/* see if the command actually succeeded */
	if (readb(sabi_iface + SABI_IFACE_COMPLETE) == 0xaa &&
	    readb(sabi_iface + SABI_IFACE_DATA) != 0xff) {
		/*
		 * It did!
		 * Save off the data into a structure so the caller use it.
		 * Right now we only care about the first 4 bytes,
		 * I suppose there are commands that need more, but I don't
		 * know about them.
		 */
		sretval->retval[0] = readb(sabi_iface + SABI_IFACE_DATA);
		sretval->retval[1] = readb(sabi_iface + SABI_IFACE_DATA + 1);
		sretval->retval[2] = readb(sabi_iface + SABI_IFACE_DATA + 2);
		sretval->retval[3] = readb(sabi_iface + SABI_IFACE_DATA + 3);
		goto exit;
	}

	/* Something bad happened, so report it and error out */
	printk(KERN_WARNING "SABI command 0x%02x failed with completion flag 0x%02x and output 0x%02x\n",
		command, readb(sabi_iface + SABI_IFACE_COMPLETE),
		readb(sabi_iface + SABI_IFACE_DATA));
	retval = -EINVAL;
exit:
	mutex_unlock(&sabi_mutex);
	return retval;

}

static int sabi_set_command(u8 command, u8 data)
{
	int retval = 0;
	u16 port = readw(sabi + SABI_HEADER_PORT);

	mutex_lock(&sabi_mutex);

	/* enable memory to be able to write to it */
	outb(readb(sabi + SABI_HEADER_EN_MEM), port);

	/* write out the command */
	writew(MAIN_FUNCTION, sabi_iface + SABI_IFACE_MAIN);
	writew(command, sabi_iface + SABI_IFACE_SUB);
	writeb(0, sabi_iface + SABI_IFACE_COMPLETE);
	writeb(data, sabi_iface + SABI_IFACE_DATA);
	outb(readb(sabi + SABI_HEADER_IFACEFUNC), port);

	/* write protect memory to make it safe */
	outb(readb(sabi + SABI_HEADER_RE_MEM), port);

	/* see if the command actually succeeded */
	if (readb(sabi_iface + SABI_IFACE_COMPLETE) == 0xaa &&
	    readb(sabi_iface + SABI_IFACE_DATA) != 0xff) {
		/* it did! */
		goto exit;
	}

	/* Something bad happened, so report it and error out */
	printk(KERN_WARNING "SABI command 0x%02x failed with completion flag 0x%02x and output 0x%02x\n",
		command, readb(sabi_iface + SABI_IFACE_COMPLETE),
		readb(sabi_iface + SABI_IFACE_DATA));
	retval = -EINVAL;
exit:
	mutex_unlock(&sabi_mutex);
	return retval;
}

static void test_backlight(void)
{
	struct sabi_retval sretval;

	sabi_get_command(GET_BACKLIGHT, &sretval);
	printk(KERN_DEBUG "backlight = 0x%02x\n", sretval.retval[0]);

	sabi_set_command(SET_BACKLIGHT, 0);
	printk(KERN_DEBUG "backlight should be off\n");

	sabi_get_command(GET_BACKLIGHT, &sretval);
	printk(KERN_DEBUG "backlight = 0x%02x\n", sretval.retval[0]);

	msleep(1000);

	sabi_set_command(SET_BACKLIGHT, 1);
	printk(KERN_DEBUG "backlight should be on\n");

	sabi_get_command(GET_BACKLIGHT, &sretval);
	printk(KERN_DEBUG "backlight = 0x%02x\n", sretval.retval[0]);
}

static void test_wireless(void)
{
	struct sabi_retval sretval;

	sabi_get_command(GET_WIRELESS_BUTTON, &sretval);
	printk(KERN_DEBUG "wireless led = 0x%02x\n", sretval.retval[0]);

	sabi_set_command(SET_WIRELESS_BUTTON, 0);
	printk(KERN_DEBUG "wireless led should be off\n");

	sabi_get_command(GET_WIRELESS_BUTTON, &sretval);
	printk(KERN_DEBUG "wireless led = 0x%02x\n", sretval.retval[0]);

	msleep(1000);

	sabi_set_command(SET_WIRELESS_BUTTON, 1);
	printk(KERN_DEBUG "wireless led should be on\n");

	sabi_get_command(GET_WIRELESS_BUTTON, &sretval);
	printk(KERN_DEBUG "wireless led = 0x%02x\n", sretval.retval[0]);
}

static u8 read_brightness(void)
{
	struct sabi_retval sretval;
	int user_brightness = 0;
	int retval;

	retval = sabi_get_command(GET_BRIGHTNESS, &sretval);
	if (!retval)
		user_brightness = sretval.retval[0];
		if (user_brightness != 0)
			--user_brightness;
	return user_brightness;
}

static void set_brightness(u8 user_brightness)
{
	sabi_set_command(SET_BRIGHTNESS, user_brightness + 1);
}

static int get_brightness(struct backlight_device *bd)
{
	return (int)read_brightness();
}

static int update_status(struct backlight_device *bd)
{
	set_brightness(bd->props.brightness);

	if (bd->props.power == FB_BLANK_UNBLANK)
		sabi_set_command(SET_BACKLIGHT, 1);
	else
		sabi_set_command(SET_BACKLIGHT, 0);
	return 0;
}

static struct backlight_ops backlight_ops = {
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
		sabi_set_command(SET_WIRELESS_BUTTON, 0);
	else
		sabi_set_command(SET_WIRELESS_BUTTON, 1);

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

static ssize_t get_silent_state(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sabi_retval sretval;
	int retval;

	/* Read the state */
	retval = sabi_get_command(GET_PERFORMANCE_LEVEL, &sretval);
	if (retval)
		return retval;

	/* The logic is backwards, yeah, lots of fun... */
	if (sretval.retval[0] == 0)
		retval = 1;
	else
		retval = 0;
	return sprintf(buf, "%d\n", retval);
}

static ssize_t set_silent_state(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	char value;

	if (count >= 1) {
		value = buf[0];
		if ((value == '0') || (value == 'n') || (value == 'N')) {
			/* Turn speed up */
			sabi_set_command(SET_PERFORMANCE_LEVEL, 0x01);
		} else if ((value == '1') || (value == 'y') || (value == 'Y')) {
			/* Turn speed down */
			sabi_set_command(SET_PERFORMANCE_LEVEL, 0x00);
		} else {
			return -EINVAL;
		}
	}
	return count;
}
static DEVICE_ATTR(silent, S_IWUSR | S_IRUGO,
		   get_silent_state, set_silent_state);


static int __init dmi_check_cb(const struct dmi_system_id *id)
{
	printk(KERN_INFO KBUILD_MODNAME ": found laptop model '%s'\n",
		id->ident);
	return 0;
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
	{ },
};
MODULE_DEVICE_TABLE(dmi, samsung_dmi_table);

static int __init samsung_init(void)
{
	struct backlight_properties props;
	struct sabi_retval sretval;
	const char *testStr = "SECLINUX";
	void __iomem *memcheck;
	unsigned int ifaceP;
	int pStr;
	int loca;
	int retval;

	mutex_init(&sabi_mutex);

	if (!force && !dmi_check_system(samsung_dmi_table))
		return -ENODEV;

	f0000_segment = ioremap(0xf0000, 0xffff);
	if (!f0000_segment) {
		printk(KERN_ERR "Can't map the segment at 0xf0000\n");
		return -EINVAL;
	}

	/* Try to find the signature "SECLINUX" in memory to find the header */
	pStr = 0;
	memcheck = f0000_segment;
	for (loca = 0; loca < 0xffff; loca++) {
		char temp = readb(memcheck + loca);

		if (temp == testStr[pStr]) {
			if (pStr == strlen(testStr)-1)
				break;
			++pStr;
		} else {
			pStr = 0;
		}
	}
	if (loca == 0xffff) {
		printk(KERN_ERR "This computer does not support SABI\n");
		goto error_no_signature;
		}

	/* point to the SMI port Number */
	loca += 1;
	sabi = (memcheck + loca);

	if (debug) {
		printk(KERN_DEBUG "This computer supports SABI==%x\n",
			loca + 0xf0000 - 6);
		printk(KERN_DEBUG "SABI header:\n");
		printk(KERN_DEBUG " SMI Port Number = 0x%04x\n",
			readw(sabi + SABI_HEADER_PORT));
		printk(KERN_DEBUG " SMI Interface Function = 0x%02x\n",
			readb(sabi + SABI_HEADER_IFACEFUNC));
		printk(KERN_DEBUG " SMI enable memory buffer = 0x%02x\n",
			readb(sabi + SABI_HEADER_EN_MEM));
		printk(KERN_DEBUG " SMI restore memory buffer = 0x%02x\n",
			readb(sabi + SABI_HEADER_RE_MEM));
		printk(KERN_DEBUG " SABI data offset = 0x%04x\n",
			readw(sabi + SABI_HEADER_DATA_OFFSET));
		printk(KERN_DEBUG " SABI data segment = 0x%04x\n",
			readw(sabi + SABI_HEADER_DATA_SEGMENT));
	}

	/* Get a pointer to the SABI Interface */
	ifaceP = (readw(sabi + SABI_HEADER_DATA_SEGMENT) & 0x0ffff) << 4;
	ifaceP += readw(sabi + SABI_HEADER_DATA_OFFSET) & 0x0ffff;
	sabi_iface = ioremap(ifaceP, 16);
	if (!sabi_iface) {
		printk(KERN_ERR "Can't remap %x\n", ifaceP);
		goto exit;
	}
	if (debug) {
		printk(KERN_DEBUG "ifaceP = 0x%08x\n", ifaceP);
		printk(KERN_DEBUG "sabi_iface = %p\n", sabi_iface);

		test_backlight();
		test_wireless();

		retval = sabi_get_command(GET_BRIGHTNESS, &sretval);
		printk(KERN_DEBUG "brightness = 0x%02x\n", sretval.retval[0]);
	}

	/* Turn on "Linux" mode in the BIOS */
	retval = sabi_set_command(SET_LINUX, 0x81);
	if (retval) {
		printk(KERN_ERR KBUILD_MODNAME ": Linux mode was not set!\n");
		goto error_no_platform;
	}

	/* knock up a platform device to hang stuff off of */
	sdev = platform_device_register_simple("samsung", -1, NULL, 0);
	if (IS_ERR(sdev))
		goto error_no_platform;

	/* create a backlight device to talk to this one */
	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = MAX_BRIGHT;
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

	retval = device_create_file(&sdev->dev, &dev_attr_silent);
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
	sabi_set_command(SET_LINUX, 0x80);

	device_remove_file(&sdev->dev, &dev_attr_silent);
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
