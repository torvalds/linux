// SPDX-License-Identifier: GPL-2.0+
/*
 * Software Node helpers for the GPIO API
 *
 * Copyright 2022 Google LLC
 */
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/property.h>
#include <linux/string.h>

#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>

#include "gpiolib.h"
#include "gpiolib-swnode.h"

static void swnode_format_propname(const char *con_id, char *propname,
				   size_t max_size)
{
	/*
	 * Note we do not need to try both -gpios and -gpio suffixes,
	 * as, unlike OF and ACPI, we can fix software nodes to conform
	 * to the proper binding.
	 */
	if (con_id)
		snprintf(propname, max_size, "%s-gpios", con_id);
	else
		strscpy(propname, "gpios", max_size);
}

static int swnode_gpiochip_match_name(struct gpio_chip *chip, void *data)
{
	return !strcmp(chip->label, data);
}

static struct gpio_chip *swnode_get_chip(struct fwnode_handle *fwnode)
{
	const struct software_node *chip_node;
	struct gpio_chip *chip;

	chip_node = to_software_node(fwnode);
	if (!chip_node || !chip_node->name)
		return ERR_PTR(-EINVAL);

	chip = gpiochip_find((void *)chip_node->name, swnode_gpiochip_match_name);
	return chip ?: ERR_PTR(-EPROBE_DEFER);
}

struct gpio_desc *swnode_find_gpio(struct fwnode_handle *fwnode,
				   const char *con_id, unsigned int idx,
				   unsigned long *flags)
{
	const struct software_node *swnode;
	struct fwnode_reference_args args;
	struct gpio_chip *chip;
	struct gpio_desc *desc;
	char propname[32]; /* 32 is max size of property name */
	int error;

	swnode = to_software_node(fwnode);
	if (!swnode)
		return ERR_PTR(-EINVAL);

	swnode_format_propname(con_id, propname, sizeof(propname));

	/*
	 * We expect all swnode-described GPIOs have GPIO number and
	 * polarity arguments, hence nargs is set to 2.
	 */
	error = fwnode_property_get_reference_args(fwnode, propname, NULL, 2, idx, &args);
	if (error) {
		pr_debug("%s: can't parse '%s' property of node '%pfwP[%d]'\n",
			__func__, propname, fwnode, idx);
		return ERR_PTR(error);
	}

	chip = swnode_get_chip(args.fwnode);
	fwnode_handle_put(args.fwnode);
	if (IS_ERR(chip))
		return ERR_CAST(chip);

	desc = gpiochip_get_desc(chip, args.args[0]);
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
 * Return:
 * The number of GPIOs associated with a device / function or %-ENOENT,
 * if no GPIO has been assigned to the requested function.
 */
int swnode_gpio_count(const struct fwnode_handle *fwnode, const char *con_id)
{
	struct fwnode_reference_args args;
	char propname[32];
	int count;

	swnode_format_propname(con_id, propname, sizeof(propname));

	/*
	 * This is not very efficient, but GPIO lists usually have only
	 * 1 or 2 entries.
	 */
	count = 0;
	while (fwnode_property_get_reference_args(fwnode, propname, NULL, 0,
						  count, &args) == 0) {
		fwnode_handle_put(args.fwnode);
		count++;
	}

	return count ?: -ENOENT;
}
