/* driver/misc/sun4i-gpio.c
 *
 *  Copyright (C) 2011 Allwinner Technology Co.Ltd
 *   Tom Cubie <tangliang@allwinnertech.com>
 *
 *  www.allwinnertech.com
 *
 *  An ugly sun4i gpio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/sysdev.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/earlysuspend.h>
#include <linux/miscdevice.h>
#include <linux/device.h>

#include <mach/sys_config.h>

#undef DEBUG_SUN4I

#ifdef DEBUG_SUN4I
#define sun4i_gpio_dbg(x...)	printk(x)
#else
#define sun4i_gpio_dbg(x...)
#endif

struct sun4i_gpio_data {
	int status;
	unsigned gpio_handler;
	script_gpio_set_t info;
	char name[8];
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
};

static int sun4i_gpio_num;
static struct sun4i_gpio_data *psun4i_gpio;
static struct device_attribute *pattr;

static void set_sun4i_gpio_status(struct sun4i_gpio_data *sun4i_gpio,  int on)
{
	if(on) {
		sun4i_gpio_dbg("\n-on-");
		gpio_write_one_pin_value(sun4i_gpio->gpio_handler, 1, NULL);
	}
	else{
		sun4i_gpio_dbg("\n-off-");
		gpio_write_one_pin_value(sun4i_gpio->gpio_handler, 0, NULL);
	}
}

static ssize_t sun4i_gpio_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long data;
	int i,error;
	struct sun4i_gpio_data *gpio_i = psun4i_gpio;

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	for(i = 0; i < sun4i_gpio_num; i++) {
		sun4i_gpio_dbg("%s\n", attr->attr.name);
		sun4i_gpio_dbg("%s\n", gpio_i->name);

		if(!strcmp(attr->attr.name, gpio_i->name)) {
			set_sun4i_gpio_status(gpio_i, data);
			break;
		}
		gpio_i++;
	}

	return count;
}

static ssize_t sun4i_gpio_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i;
	int data = EGPIO_FAIL;
	struct sun4i_gpio_data *gpio_i = psun4i_gpio;

	for(i = 0; i < sun4i_gpio_num; i++) {
		sun4i_gpio_dbg("%s\n", attr->attr.name);
		sun4i_gpio_dbg("%s\n", gpio_i->name);

		if(!strcmp(attr->attr.name, gpio_i->name)) {
			data = gpio_read_one_pin_value(gpio_i->gpio_handler, NULL);
			sun4i_gpio_dbg("handler:%d\n", gpio_i->gpio_handler);
			sun4i_gpio_dbg("data:%d\n", data);
			break;
		}
		gpio_i++;
	}

	if(data != EGPIO_FAIL) {
		return sprintf(buf, "%d\n", data);
	} else {
		return sprintf(buf, "error\n");
	}
}

static struct attribute *sun4i_gpio_attributes[256] = {
	NULL
};

static struct attribute_group sun4i_gpio_attribute_group = {
	.name = "pin",
	.attrs = sun4i_gpio_attributes
};

static int sun4i_gpio_open(struct inode *inode, struct file *file) {
	pr_info("sun4i_gpio open\n");
	return 0;
}

ssize_t sun4i_gpio_write(struct file *file, const char __user *buf, size_t size, loff_t *offset) {
	pr_info("sun4i_gpio write\n");
	return 0;
}

static int sun4i_gpio_release(struct inode *inode, struct file *file) {
	gpio_release(psun4i_gpio->gpio_handler, 0);
	kfree(psun4i_gpio);
	return 0;
}

static const struct file_operations sun4i_gpio_fops = {
	.open		= sun4i_gpio_open,
	.write		= sun4i_gpio_write,
	.release	= sun4i_gpio_release
};

static struct miscdevice sun4i_gpio_dev = {
	.minor =	MISC_DYNAMIC_MINOR,
	.name =		"sun4i-gpio",
	.fops =		&sun4i_gpio_fops
};

static int __init sun4i_gpio_init(void) {
	int err;
	int i;
	int sun4i_gpio_used = 0;
	struct sun4i_gpio_data *gpio_i;
	struct device_attribute *attr_i;
	char pin[16];

	pr_info("sun4i gpio driver init\n");

	err = script_parser_fetch("gpio_para", "gpio_used", &sun4i_gpio_used, sizeof(sun4i_gpio_used)/sizeof(int));
	if(err) {
		pr_err("%s script_parser_fetch \"gpio_para\" \"gpio_used\" error\n", __FUNCTION__);
		goto exit;
	}

	if(!sun4i_gpio_used) {
		pr_err("%s sun4i_gpio is not used in config\n", __FUNCTION__);
		err = -1;
		goto exit;
	}

	err = script_parser_fetch("gpio_para", "gpio_num", &sun4i_gpio_num, sizeof(sun4i_gpio_num)/sizeof(int));
	if(err) {
		pr_err("%s script_parser_fetch \"gpio_para\" \"gpio_num\" error\n", __FUNCTION__);
		goto exit;
	}

	sun4i_gpio_dbg("sun4i_gpio_num:%d\n", sun4i_gpio_num);
	if(!sun4i_gpio_num) {
		pr_err("%s sun4i_gpio_num is none\n", __FUNCTION__);
		err = -1;
		goto exit;
	}

	err = misc_register(&sun4i_gpio_dev);
	if(err) {
		pr_err("%s register sun4i_gpio as misc device error\n", __FUNCTION__);
		goto exit;
	}

	psun4i_gpio = kzalloc(sizeof(struct sun4i_gpio_data) * sun4i_gpio_num, GFP_KERNEL);
	pattr = kzalloc(sizeof(struct device_attribute) * sun4i_gpio_num, GFP_KERNEL);

	if(!psun4i_gpio || !pattr) {
		pr_err("%s kzalloc failed\n", __FUNCTION__);
		err = -ENOMEM;
		goto exit;
	}

	gpio_i = psun4i_gpio;
	attr_i = pattr;

	for(i = 0; i < sun4i_gpio_num; i++) {

		sprintf(pin, "gpio_pin_%d", i+1);
		sun4i_gpio_dbg("pin:%s\n", pin);

		err = script_parser_fetch("gpio_para", pin,
					(int *)&gpio_i->info, sizeof(script_gpio_set_t));

		if(err) {
			pr_err("%s script_parser_fetch \"gpio_para\" \"%s\" error\n", __FUNCTION__, pin);
			break;
		}

		gpio_i->gpio_handler = gpio_request_ex("gpio_para", pin);
		sun4i_gpio_dbg("gpio handler: %d", gpio_i->gpio_handler);

		if(!gpio_i->gpio_handler) {
			pr_err("%s can not get \"gpio_para\" \"%s\" gpio handler,\
					already used by others?", __FUNCTION__, pin);
			break;
		}

		sun4i_gpio_dbg("%s: port:%d, portnum:%d\n", pin, gpio_i->info.port,
				gpio_i->info.port_num);

		/* Turn the name to pa1, pb2 etc... */
		sprintf(gpio_i->name, "p%c%d", 'a'+gpio_i->info.port-1, gpio_i->info.port_num);

		sun4i_gpio_dbg("psun4i_gpio->name%s\n", gpio_i->name);

		/* Add attributes to the group */
		sysfs_attr_init(&attr_i->attr);
		attr_i->attr.name = gpio_i->name;
		attr_i->attr.mode = S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH;
		attr_i->show = sun4i_gpio_enable_show;
		attr_i->store = sun4i_gpio_enable_store;
		sun4i_gpio_attributes[i] = &attr_i->attr;

		gpio_i++;
		attr_i++;
	}

	sysfs_create_group(&sun4i_gpio_dev.this_device->kobj,
						 &sun4i_gpio_attribute_group);
exit:
	return err;
}

static void __exit sun4i_gpio_exit(void) {

	sun4i_gpio_dbg("bye, sun4i_gpio exit\n");
	misc_deregister(&sun4i_gpio_dev);
	sysfs_remove_group(&sun4i_gpio_dev.this_device->kobj,
						 &sun4i_gpio_attribute_group);
	kfree(psun4i_gpio);
	kfree(pattr);
}

module_init(sun4i_gpio_init);
module_exit(sun4i_gpio_exit);

MODULE_DESCRIPTION("a simple sun4i_gpio driver");
MODULE_AUTHOR("Tom Cubie");
MODULE_LICENSE("GPL");
