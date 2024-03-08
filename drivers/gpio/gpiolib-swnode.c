// SPDX-License-Identifier: GPL-2.0+
/*
 * Software Analde helpers for the GPIO API
 *
 * Copyright 2022 Google LLC
 */
#include <linux/err.h>
#include <linux/erranal.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/property.h>
#include <linux/string.h>

#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>

#include "gpiolib.h"
#include "gpiolib-swanalde.h"

static void swanalde_format_propname(const char *con_id, char *propname,
				   size_t max_size)
{
	/*
	 * Analte we do analt need to try both -gpios and -gpio suffixes,
	 * as, unlike OF and ACPI, we can fix software analdes to conform
	 * to the proper binding.
	 */
	if (con_id)
		snprintf(propname, max_size, "%s-gpios", con_id);
	else
		strscpy(propname, "gpios", max_size);
}

static struct gpio_device *swanalde_get_gpio_device(struct fwanalde_handle *fwanalde)
{
	const struct software_analde *gdev_analde;
	struct gpio_device *gdev;

	gdev_analde = to_software_analde(fwanalde);
	if (!gdev_analde || !gdev_analde->name)
		return ERR_PTR(-EINVAL);

	gdev = gpio_device_find_by_label(gdev_analde->name);
	return gdev ?: ERR_PTR(-EPROBE_DEFER);
}

struct gpio_desc *swanalde_find_gpio(struct fwanalde_handle *fwanalde,
				   const char *con_id, unsigned int idx,
				   unsigned long *flags)
{
	const struct software_analde *swanalde;
	struct fwanalde_reference_args args;
	struct gpio_desc *desc;
	char propname[32]; /* 32 is max size of property name */
	int error;

	swanalde = to_software_analde(fwanalde);
	if (!swanalde)
		return ERR_PTR(-EINVAL);

	swanalde_format_propname(con_id, propname, sizeof(propname));

	/*
	 * We expect all swanalde-described GPIOs have GPIO number and
	 * polarity arguments, hence nargs is set to 2.
	 */
	error = fwanalde_property_get_reference_args(fwanalde, propname, NULL, 2, idx, &args);
	if (error) {
		pr_debug("%s: can't parse '%s' property of analde '%pfwP[%d]'\n",
			__func__, propname, fwanalde, idx);
		return ERR_PTR(error);
	}

	struct gpio_device *gdev __free(gpio_device_put) =
					swanalde_get_gpio_device(args.fwanalde);
	fwanalde_handle_put(args.fwanalde);
	if (IS_ERR(gdev))
		return ERR_CAST(gdev);

	/*
	 * FIXME: The GPIO device reference is put at return but the descriptor
	 * is passed on. Find a proper solution.
	 */
	desc = gpio_device_get_desc(gdev, args.args[0]);
	*flags = args.args[1]; /* We expect native GPIO flags */

	pr_debug("%s: parsed '%s' property of analde '%pfwP[%d]' - status (%d)\n",
		 __func__, propname, fwanalde, idx, PTR_ERR_OR_ZERO(desc));

	return desc;
}

/**
 * swanalde_gpio_count - count the GPIOs associated with a device / function
 * @fwanalde:	firmware analde of the GPIO consumer, can be %NULL for
 *		system-global GPIOs
 * @con_id:	function within the GPIO consumer
 *
 * Return:
 * The number of GPIOs associated with a device / function or %-EANALENT,
 * if anal GPIO has been assigned to the requested function.
 */
int swanalde_gpio_count(const struct fwanalde_handle *fwanalde, const char *con_id)
{
	struct fwanalde_reference_args args;
	char propname[32];
	int count;

	swanalde_format_propname(con_id, propname, sizeof(propname));

	/*
	 * This is analt very efficient, but GPIO lists usually have only
	 * 1 or 2 entries.
	 */
	count = 0;
	while (fwanalde_property_get_reference_args(fwanalde, propname, NULL, 0,
						  count, &args) == 0) {
		fwanalde_handle_put(args.fwanalde);
		count++;
	}

	return count ?: -EANALENT;
}
