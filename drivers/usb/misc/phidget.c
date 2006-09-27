/*
 * USB Phidgets class
 *
 * Copyright (C) 2006  Sean Young <sean@mess.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/device.h>

struct class *phidget_class;

static int __init init_phidget(void)
{
	phidget_class = class_create(THIS_MODULE, "phidget");

	if (IS_ERR(phidget_class))
		return PTR_ERR(phidget_class);

	return 0;
}

static void __exit cleanup_phidget(void)
{
	class_destroy(phidget_class);
}

EXPORT_SYMBOL_GPL(phidget_class);

module_init(init_phidget);
module_exit(cleanup_phidget);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sean Young <sean@mess.org>");
MODULE_DESCRIPTION("Container module for phidget class");

