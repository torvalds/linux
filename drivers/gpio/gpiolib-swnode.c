// SPDX-License-Identifier: GPL-2.0+
/*
 * Software Node helpers for the GPIO API
 *
 * Copyright 2022 Google LLC
 */

#define pr_fmt(fmt) "gpiolib: swnode: " fmt

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/property.h>
#include <linux/string.h>

#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>

#include "gpiolib.h"
#include "gpiolib-swnode.h"

#define GPIOLIB_SWNODE_UNDEFINED_NAME "swnode-gpio-undefined"

static struct gpio_device *swnode_get_gpio_device(struct fwnode_handle *fwnode)
{
	const struct software_node *gdev_node;
	struct gpio_device *gdev;

	gdev_node = to_software_node(fwnode);
	if (!gdev_node || !gdev_node->name)
		return ERR_PTR(-EINVAL);

	/*
	 * Check for a special node that identifies undefined GPIOs, this is
	 * primarily used as a key for internal chip selects in SPI bindings.
	 */
	if (IS_ENABLED(CONFIG_GPIO_SWNODE_UNDEFINED) &&
	    !strcmp(gdev_node->name, GPIOLIB_SWNODE_UNDEFINED_NAME))
		return ERR_PTR(-ENOENT);

	gdev = gpio_device_find_by_label(gdev_node->name);
	return gdev ?: ERR_PTR(-EPROBE_DEFER);
}

static int swnode_gpio_get_reference(const struct fwnode_handle *fwnode,
				     const char *propname, unsigned int idx,
				     struct fwnode_reference_args *args)
{
	/*
	 * We expect all swnode-described GPIOs have GPIO number and
	 * polarity arguments, hence nargs is set to 2.
	 */
	return fwnode_property_get_reference_args(fwnode, propname, NULL, 2, idx, args);
}

struct gpio_desc *swnode_find_gpio(struct fwnode_handle *fwnode,
				   const char *con_id, unsigned int idx,
				   unsigned long *flags)
{
	const struct software_node *swnode;
	struct fwnode_reference_args args;
	struct gpio_desc *desc;
	char propname[32]; /* 32 is max size of property name */
	int ret = 0;

	swnode = to_software_node(fwnode);
	if (!swnode)
		return ERR_PTR(-EINVAL);

	for_each_gpio_property_name(propname, con_id) {
		ret = swnode_gpio_get_reference(fwnode, propname, idx, &args);
		if (ret == 0)
			break;
	}
	if (ret) {
		pr_debug("%s: can't parse '%s' property of node '%pfwP[%d]'\n",
			__func__, propname, fwnode, idx);
		return ERR_PTR(ret);
	}

	struct gpio_device *gdev __free(gpio_device_put) =
					swnode_get_gpio_device(args.fwnode);
	fwnode_handle_put(args.fwnode);
	if (IS_ERR(gdev))
		return ERR_CAST(gdev);

	/*
	 * FIXME: The GPIO device reference is put at return but the descriptor
	 * is passed on. Find a proper solution.
	 */
	desc = gpio_device_get_desc(gdev, args.args[0]);
	*flags = args.args[1]; /* We expect native GPIO flags */

	pr_debug("%s: parsed '%s' property of node '%pfwP[%d]' - status (%d)\n",
		 __func__, propname, fwnode, idx, PTR_ERR_OR_ZERO(desc));

	return desc;
}

/**
 * swnode_gpio_count - count the GPIOs associated with a device / function
 * @fwnode:	firmware node of the GPIO consumer, can be %NULL for
 *		system-global GPIOs
 * @con_id:	function within the GPIO consumer
 *
 * Returns:
 * The number of GPIOs associated with a device / function or %-ENOENT,
 * if no GPIO has been assigned to the requested function.
 */
int swnode_gpio_count(const struct fwnode_handle *fwnode, const char *con_id)
{
	struct fwnode_reference_args args;
	char propname[32];
	int count;

	/*
	 * This is not very efficient, but GPIO lists usually have only
	 * 1 or 2 entries.
	 */
	for_each_gpio_property_name(propname, con_id) {
		count = 0;
		while (swnode_gpio_get_reference(fwnode, propname, count, &args) == 0) {
			fwnode_handle_put(args.fwnode);
			count++;
		}
		if (count)
			return count;
	}

	return -ENOENT;
}

#if IS_ENABLED(CONFIG_GPIO_SWNODE_UNDEFINED)
/*
 * A special node that identifies undefined GPIOs, this is primarily used as
 * a key for internal chip selects in SPI bindings.
 */
const struct software_node swnode_gpio_undefined = {
	.name = GPIOLIB_SWNODE_UNDEFINED_NAME,
};
EXPORT_SYMBOL_NS_GPL(swnode_gpio_undefined, GPIO_SWNODE);

static int __init swnode_gpio_init(void)
{
	int ret;

	ret = software_node_register(&swnode_gpio_undefined);
	if (ret < 0)
		pr_err("failed to register swnode: %d\n", ret);

	return ret;
}
subsys_initcall(swnode_gpio_init);

static void __exit swnode_gpio_cleanup(void)
{
	software_node_unregister(&swnode_gpio_undefined);
}
__exitcall(swnode_gpio_cleanup);
#endif
