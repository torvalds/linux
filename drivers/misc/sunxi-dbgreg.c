/* driver/misc/sunxi-dbgreg.c
 *
 *  Copyright (C) 2011 Allwinner Technology Co.Ltd
 *  Tom Cubie <tangliang@allwinnertech.com>
 *
 *  www.allwinnertech.com
 *
 *  Read and write system registers in userspace.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/device.h>

#undef DEBUG_SUNXI

#ifdef DEBUG_SUNXI
#define sunxi_reg_dbg(x...) printk(x)
#else
#define sunxi_reg_dbg(x...)
#endif


static char readme[] = "This is a userspace interface to access the sunxi soc registers.\n"
                       "Usage:\n"
                       "\techo address > read           # Read the value at address\n"
					   "\teg: echo f1c20c14 > read\n"
                       "\techo address:value > write    # Write value to address\n"
					   "\teg: echo f1c20c14:ffff > write\n"
                       "\tcat read or cat write         # See this readme\n"
                       "Note: Always use hex and always use virtual address\n"
					   "Warnning: use at your own risk\n";

static ssize_t sunxi_debugreg_read_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long addr;
	int err, len;

	len = strlen(buf);

	/* echo will append '\n', user may use 0x */
	if( len != 9 && len != 11) {
		printk("Invalid address length, please cat read to see readme\n");
		return count;
	}

	err = strict_strtoul(buf, 16, &addr);

	if (err) {
		printk("Invalid value, please cat read to see readme\n");
		return count;
	}

	if(addr < 0xf0000000) {
		printk("Please use virtual address!!!\n");
		return count;
	}

	printk("0x%x\n", readl(addr));

	return count;
}

static ssize_t sunxi_debugreg_write_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long addr, value;
	int err, len;
	const char *s = NULL;
	char addrstr[16];

	s = strchr(buf, ':');
	if( s == NULL) {
		printk("Wrong format, no :, please cat write to see readme\n");
		return count;
	}

	len = s - buf;
	sunxi_reg_dbg("len: %d\n", len);

	if( len != 8 && len != 10) {
		printk("Invalid address length, please cat write to see readme\n");
		return count;
	}

	strncpy(addrstr, buf, len);
	addrstr[len] = '\0';

	sunxi_reg_dbg("addrstr: %s\n", addrstr);

	err = strict_strtoul(addrstr, 16, &addr);
	sunxi_reg_dbg("addr: 0x%lx\n", addr);
	if(err) {
		printk("Invalid address, please cat write to see readme\n");
		return count;
	}

	/* value starts after the : */
	len = strlen(s+1);
	sunxi_reg_dbg("s+1 length: %d\n", len);
	if( len > 11) {
		printk("Invalid value length, please cat read to see readme\n");
		return count;
	}

	err = strict_strtoul(s+1, 16, &value);
	sunxi_reg_dbg("value: 0x%lx\n", value);

	if(err) {
		printk("Invalid value, please cat write to see readme\n");
		return count;
	}

	if(addr < 0xf0000000) {
		printk("Please use virtual address!!!\n");
		return count;
	}

	writel(value, addr);

	return count;
}

static ssize_t sunxi_debugreg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, readme);
}

static DEVICE_ATTR(read, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		sunxi_debugreg_show, sunxi_debugreg_read_store);
static DEVICE_ATTR(write, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		sunxi_debugreg_show, sunxi_debugreg_write_store);

static struct attribute *sunxi_debugreg_attributes[] = {
	&dev_attr_read.attr,
	&dev_attr_write.attr,
	NULL
};

static struct attribute_group sunxi_debugreg_attribute_group = {
	.name = "rw",
	.attrs = sunxi_debugreg_attributes
};

static struct miscdevice sunxi_debugreg_dev = {
	.minor =	MISC_DYNAMIC_MINOR,
	.name =		"sunxi-dbgreg",
};

static int __init sunxi_debugreg_init(void) {
	int err;

	pr_info("sunxi debug register driver init\n");

	err = misc_register(&sunxi_debugreg_dev);
	if(err) {
		pr_err("%s register sunxi debug register driver as misc device error\n", __FUNCTION__);
		goto exit;
	}

	err = sysfs_create_group(&sunxi_debugreg_dev.this_device->kobj,
						 &sunxi_debugreg_attribute_group);

	if(err) {
		pr_err("%s create sysfs failed\n", __FUNCTION__);
		goto exit;
	}

exit:
	return err;
}

static void __exit sunxi_debugreg_exit(void) {

	sunxi_reg_dbg("bye, sun4i_debugreg exit\n");
	misc_deregister(&sunxi_debugreg_dev);
	sysfs_remove_group(&sunxi_debugreg_dev.this_device->kobj,
						 &sunxi_debugreg_attribute_group);
}

module_init(sunxi_debugreg_init);
module_exit(sunxi_debugreg_exit);

MODULE_DESCRIPTION("a simple sunxi debug driver");
MODULE_AUTHOR("Tom Cubie");
MODULE_LICENSE("GPL");
