/*
 * Driver frontend for IBM Power 842 compression accelerator
 *
 * Copyright (C) 2015 Dan Streetman, IBM Corp
 *
 * Designer of the Power data compression engine:
 *   Bulent Abali <abali@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "nx-842.h"

#define MODULE_NAME "nx-compress"
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dan Streetman <ddstreet@ieee.org>");
MODULE_DESCRIPTION("842 H/W Compression driver for IBM Power processors");

/* Only one driver is expected, based on the HW platform */
static struct nx842_driver *nx842_driver;
static DEFINE_SPINLOCK(nx842_driver_lock); /* protects driver pointers */

void nx842_register_driver(struct nx842_driver *driver)
{
	spin_lock(&nx842_driver_lock);

	if (nx842_driver) {
		pr_err("can't register driver %s, already using driver %s\n",
		       driver->owner->name, nx842_driver->owner->name);
	} else {
		pr_info("registering driver %s\n", driver->owner->name);
		nx842_driver = driver;
	}

	spin_unlock(&nx842_driver_lock);
}
EXPORT_SYMBOL_GPL(nx842_register_driver);

void nx842_unregister_driver(struct nx842_driver *driver)
{
	spin_lock(&nx842_driver_lock);

	if (nx842_driver == driver) {
		pr_info("unregistering driver %s\n", driver->owner->name);
		nx842_driver = NULL;
	} else if (nx842_driver) {
		pr_err("can't unregister driver %s, using driver %s\n",
		       driver->owner->name, nx842_driver->owner->name);
	} else {
		pr_err("can't unregister driver %s, no driver in use\n",
		       driver->owner->name);
	}

	spin_unlock(&nx842_driver_lock);
}
EXPORT_SYMBOL_GPL(nx842_unregister_driver);

static struct nx842_driver *get_driver(void)
{
	struct nx842_driver *driver = NULL;

	spin_lock(&nx842_driver_lock);

	driver = nx842_driver;

	if (driver && !try_module_get(driver->owner))
		driver = NULL;

	spin_unlock(&nx842_driver_lock);

	return driver;
}

static void put_driver(struct nx842_driver *driver)
{
	module_put(driver->owner);
}

/**
 * nx842_constraints
 *
 * This provides the driver's constraints.  Different nx842 implementations
 * may have varying requirements.  The constraints are:
 *   @alignment:	All buffers should be aligned to this
 *   @multiple:		All buffer lengths should be a multiple of this
 *   @minimum:		Buffer lengths must not be less than this amount
 *   @maximum:		Buffer lengths must not be more than this amount
 *
 * The constraints apply to all buffers and lengths, both input and output,
 * for both compression and decompression, except for the minimum which
 * only applies to compression input and decompression output; the
 * compressed data can be less than the minimum constraint.  It can be
 * assumed that compressed data will always adhere to the multiple
 * constraint.
 *
 * The driver may succeed even if these constraints are violated;
 * however the driver can return failure or suffer reduced performance
 * if any constraint is not met.
 */
int nx842_constraints(struct nx842_constraints *c)
{
	struct nx842_driver *driver = get_driver();
	int ret = 0;

	if (!driver)
		return -ENODEV;

	BUG_ON(!c);
	memcpy(c, driver->constraints, sizeof(*c));

	put_driver(driver);

	return ret;
}
EXPORT_SYMBOL_GPL(nx842_constraints);

int nx842_compress(const unsigned char *in, unsigned int in_len,
		   unsigned char *out, unsigned int *out_len,
		   void *wrkmem)
{
	struct nx842_driver *driver = get_driver();
	int ret;

	if (!driver)
		return -ENODEV;

	ret = driver->compress(in, in_len, out, out_len, wrkmem);

	put_driver(driver);

	return ret;
}
EXPORT_SYMBOL_GPL(nx842_compress);

int nx842_decompress(const unsigned char *in, unsigned int in_len,
		     unsigned char *out, unsigned int *out_len,
		     void *wrkmem)
{
	struct nx842_driver *driver = get_driver();
	int ret;

	if (!driver)
		return -ENODEV;

	ret = driver->decompress(in, in_len, out, out_len, wrkmem);

	put_driver(driver);

	return ret;
}
EXPORT_SYMBOL_GPL(nx842_decompress);

static __init int nx842_init(void)
{
	pr_info("loading\n");

	if (of_find_compatible_node(NULL, NULL, NX842_POWERNV_COMPAT_NAME))
		request_module_nowait(NX842_POWERNV_MODULE_NAME);
	else if (of_find_compatible_node(NULL, NULL, NX842_PSERIES_COMPAT_NAME))
		request_module_nowait(NX842_PSERIES_MODULE_NAME);
	else
		pr_err("no nx842 driver found.\n");

	pr_info("loaded\n");

	return 0;
}
module_init(nx842_init);

static void __exit nx842_exit(void)
{
	pr_info("NX842 unloaded\n");
}
module_exit(nx842_exit);
