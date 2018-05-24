/*
 * OF helpers for the GPIO API
 *
 * Copyright (c) 2007-2008  MontaVista Software, Inc.
 *
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/slab.h>
#include <linux/gpio/machine.h>

#include "gpiolib.h"

static int of_gpiochip_match_node_and_xlate(struct gpio_chip *chip, void *data)
{
	struct of_phandle_args *gpiospec = data;

	return chip->gpiodev->dev.of_node == gpiospec->np &&
				chip->of_xlate(chip, gpiospec, NULL) >= 0;
}

static struct gpio_chip *of_find_gpiochip_by_xlate(
					struct of_phandle_args *gpiospec)
{
	return gpiochip_find(gpiospec, of_gpiochip_match_node_and_xlate);
}

static struct gpio_desc *of_xlate_and_get_gpiod_flags(struct gpio_chip *chip,
					struct of_phandle_args *gpiospec,
					enum of_gpio_flags *flags)
{
	int ret;

	if (chip->of_gpio_n_cells != gpiospec->args_count)
		return ERR_PTR(-EINVAL);

	ret = chip->of_xlate(chip, gpiospec, flags);
	if (ret < 0)
		return ERR_PTR(ret);

	return gpiochip_get_desc(chip, ret);
}

static void of_gpio_flags_quirks(struct device_node *np,
				 enum of_gpio_flags *flags)
{
	/*
	 * Some GPIO fixed regulator quirks.
	 * Note that active low is the default.
	 */
	if (IS_ENABLED(CONFIG_REGULATOR) &&
	    (of_device_is_compatible(np, "reg-fixed-voltage") ||
	     of_device_is_compatible(np, "regulator-gpio"))) {
		/*
		 * The regulator GPIO handles are specified such that the
		 * presence or absence of "enable-active-high" solely controls
		 * the polarity of the GPIO line. Any phandle flags must
		 * be actively ignored.
		 */
		if (*flags & OF_GPIO_ACTIVE_LOW) {
			pr_warn("%s GPIO handle specifies active low - ignored\n",
				of_node_full_name(np));
			*flags &= ~OF_GPIO_ACTIVE_LOW;
		}
		if (!of_property_read_bool(np, "enable-active-high"))
			*flags |= OF_GPIO_ACTIVE_LOW;
	}
	/*
	 * Legacy open drain handling for fixed voltage regulators.
	 */
	if (IS_ENABLED(CONFIG_REGULATOR) &&
	    of_device_is_compatible(np, "reg-fixed-voltage") &&
	    of_property_read_bool(np, "gpio-open-drain")) {
		*flags |= (OF_GPIO_SINGLE_ENDED | OF_GPIO_OPEN_DRAIN);
		pr_info("%s uses legacy open drain flag - update the DTS if you can\n",
			of_node_full_name(np));
	}
}

/**
 * of_get_named_gpiod_flags() - Get a GPIO descriptor and flags for GPIO API
 * @np:		device node to get GPIO from
 * @propname:	property name containing gpio specifier(s)
 * @index:	index of the GPIO
 * @flags:	a flags pointer to fill in
 *
 * Returns GPIO descriptor to use with Linux GPIO API, or one of the errno
 * value on the error condition. If @flags is not NULL the function also fills
 * in flags for the GPIO.
 */
struct gpio_desc *of_get_named_gpiod_flags(struct device_node *np,
		     const char *propname, int index, enum of_gpio_flags *flags)
{
	struct of_phandle_args gpiospec;
	struct gpio_chip *chip;
	struct gpio_desc *desc;
	int ret;

	ret = of_parse_phandle_with_args_map(np, propname, "gpio", index,
					     &gpiospec);
	if (ret) {
		pr_debug("%s: can't parse '%s' property of node '%pOF[%d]'\n",
			__func__, propname, np, index);
		return ERR_PTR(ret);
	}

	chip = of_find_gpiochip_by_xlate(&gpiospec);
	if (!chip) {
		desc = ERR_PTR(-EPROBE_DEFER);
		goto out;
	}

	desc = of_xlate_and_get_gpiod_flags(chip, &gpiospec, flags);
	if (IS_ERR(desc))
		goto out;

	if (flags)
		of_gpio_flags_quirks(np, flags);

	pr_debug("%s: parsed '%s' property of node '%pOF[%d]' - status (%d)\n",
		 __func__, propname, np, index,
		 PTR_ERR_OR_ZERO(desc));

out:
	of_node_put(gpiospec.np);

	return desc;
}

int of_get_named_gpio_flags(struct device_node *np, const char *list_name,
			    int index, enum of_gpio_flags *flags)
{
	struct gpio_desc *desc;

	desc = of_get_named_gpiod_flags(np, list_name, index, flags);

	if (IS_ERR(desc))
		return PTR_ERR(desc);
	else
		return desc_to_gpio(desc);
}
EXPORT_SYMBOL(of_get_named_gpio_flags);

/*
 * The SPI GPIO bindings happened before we managed to establish that GPIO
 * properties should be named "foo-gpios" so we have this special kludge for
 * them.
 */
static struct gpio_desc *of_find_spi_gpio(struct device *dev, const char *con_id,
					  enum of_gpio_flags *of_flags)
{
	char prop_name[32]; /* 32 is max size of property name */
	struct device_node *np = dev->of_node;
	struct gpio_desc *desc;

	/*
	 * Hopefully the compiler stubs the rest of the function if this
	 * is false.
	 */
	if (!IS_ENABLED(CONFIG_SPI_MASTER))
		return ERR_PTR(-ENOENT);

	/* Allow this specifically for "spi-gpio" devices */
	if (!of_device_is_compatible(np, "spi-gpio") || !con_id)
		return ERR_PTR(-ENOENT);

	/* Will be "gpio-sck", "gpio-mosi" or "gpio-miso" */
	snprintf(prop_name, sizeof(prop_name), "%s-%s", "gpio", con_id);

	desc = of_get_named_gpiod_flags(np, prop_name, 0, of_flags);
	return desc;
}

/*
 * Some regulator bindings happened before we managed to establish that GPIO
 * properties should be named "foo-gpios" so we have this special kludge for
 * them.
 */
static struct gpio_desc *of_find_regulator_gpio(struct device *dev, const char *con_id,
						enum of_gpio_flags *of_flags)
{
	/* These are the connection IDs we accept as legacy GPIO phandles */
	const char *whitelist[] = {
		"wlf,ldoena", /* Arizona */
		"wlf,ldo1ena", /* WM8994 */
		"wlf,ldo2ena", /* WM8994 */
	};
	struct device_node *np = dev->of_node;
	struct gpio_desc *desc;
	int i;

	if (!IS_ENABLED(CONFIG_REGULATOR))
		return ERR_PTR(-ENOENT);

	if (!con_id)
		return ERR_PTR(-ENOENT);

	for (i = 0; i < ARRAY_SIZE(whitelist); i++)
		if (!strcmp(con_id, whitelist[i]))
			break;

	if (i == ARRAY_SIZE(whitelist))
		return ERR_PTR(-ENOENT);

	desc = of_get_named_gpiod_flags(np, con_id, 0, of_flags);
	return desc;
}

struct gpio_desc *of_find_gpio(struct device *dev, const char *con_id,
			       unsigned int idx,
			       enum gpio_lookup_flags *flags)
{
	char prop_name[32]; /* 32 is max size of property name */
	enum of_gpio_flags of_flags;
	struct gpio_desc *desc;
	unsigned int i;

	/* Try GPIO property "foo-gpios" and "foo-gpio" */
	for (i = 0; i < ARRAY_SIZE(gpio_suffixes); i++) {
		if (con_id)
			snprintf(prop_name, sizeof(prop_name), "%s-%s", con_id,
				 gpio_suffixes[i]);
		else
			snprintf(prop_name, sizeof(prop_name), "%s",
				 gpio_suffixes[i]);

		desc = of_get_named_gpiod_flags(dev->of_node, prop_name, idx,
						&of_flags);
		/*
		 * -EPROBE_DEFER in our case means that we found a
		 * valid GPIO property, but no controller has been
		 * registered so far.
		 *
		 * This means we don't need to look any further for
		 * alternate name conventions, and we should really
		 * preserve the return code for our user to be able to
		 * retry probing later.
		 */
		if (IS_ERR(desc) && PTR_ERR(desc) == -EPROBE_DEFER)
			return desc;

		if (!IS_ERR(desc) || (PTR_ERR(desc) != -ENOENT))
			break;
	}

	/* Special handling for SPI GPIOs if used */
	if (IS_ERR(desc))
		desc = of_find_spi_gpio(dev, con_id, &of_flags);

	/* Special handling for regulator GPIOs if used */
	if (IS_ERR(desc) && PTR_ERR(desc) != -EPROBE_DEFER)
		desc = of_find_regulator_gpio(dev, con_id, &of_flags);

	if (IS_ERR(desc))
		return desc;

	if (of_flags & OF_GPIO_ACTIVE_LOW)
		*flags |= GPIO_ACTIVE_LOW;

	if (of_flags & OF_GPIO_SINGLE_ENDED) {
		if (of_flags & OF_GPIO_OPEN_DRAIN)
			*flags |= GPIO_OPEN_DRAIN;
		else
			*flags |= GPIO_OPEN_SOURCE;
	}

	if (of_flags & OF_GPIO_TRANSITORY)
		*flags |= GPIO_TRANSITORY;

	return desc;
}

/**
 * of_parse_own_gpio() - Get a GPIO hog descriptor, names and flags for GPIO API
 * @np:		device node to get GPIO from
 * @chip:	GPIO chip whose hog is parsed
 * @idx:	Index of the GPIO to parse
 * @name:	GPIO line name
 * @lflags:	gpio_lookup_flags - returned from of_find_gpio() or
 *		of_parse_own_gpio()
 * @dflags:	gpiod_flags - optional GPIO initialization flags
 *
 * Returns GPIO descriptor to use with Linux GPIO API, or one of the errno
 * value on the error condition.
 */
static struct gpio_desc *of_parse_own_gpio(struct device_node *np,
					   struct gpio_chip *chip,
					   unsigned int idx, const char **name,
					   enum gpio_lookup_flags *lflags,
					   enum gpiod_flags *dflags)
{
	struct device_node *chip_np;
	enum of_gpio_flags xlate_flags;
	struct of_phandle_args gpiospec;
	struct gpio_desc *desc;
	unsigned int i;
	u32 tmp;
	int ret;

	chip_np = chip->of_node;
	if (!chip_np)
		return ERR_PTR(-EINVAL);

	xlate_flags = 0;
	*lflags = 0;
	*dflags = 0;

	ret = of_property_read_u32(chip_np, "#gpio-cells", &tmp);
	if (ret)
		return ERR_PTR(ret);

	gpiospec.np = chip_np;
	gpiospec.args_count = tmp;

	for (i = 0; i < tmp; i++) {
		ret = of_property_read_u32_index(np, "gpios", idx * tmp + i,
						 &gpiospec.args[i]);
		if (ret)
			return ERR_PTR(ret);
	}

	desc = of_xlate_and_get_gpiod_flags(chip, &gpiospec, &xlate_flags);
	if (IS_ERR(desc))
		return desc;

	if (xlate_flags & OF_GPIO_ACTIVE_LOW)
		*lflags |= GPIO_ACTIVE_LOW;
	if (xlate_flags & OF_GPIO_TRANSITORY)
		*lflags |= GPIO_TRANSITORY;

	if (of_property_read_bool(np, "input"))
		*dflags |= GPIOD_IN;
	else if (of_property_read_bool(np, "output-low"))
		*dflags |= GPIOD_OUT_LOW;
	else if (of_property_read_bool(np, "output-high"))
		*dflags |= GPIOD_OUT_HIGH;
	else {
		pr_warn("GPIO line %d (%s): no hogging state specified, bailing out\n",
			desc_to_gpio(desc), np->name);
		return ERR_PTR(-EINVAL);
	}

	if (name && of_property_read_string(np, "line-name", name))
		*name = np->name;

	return desc;
}

/**
 * of_gpiochip_scan_gpios - Scan gpio-controller for gpio definitions
 * @chip:	gpio chip to act on
 *
 * This is only used by of_gpiochip_add to request/set GPIO initial
 * configuration.
 * It returns error if it fails otherwise 0 on success.
 */
static int of_gpiochip_scan_gpios(struct gpio_chip *chip)
{
	struct gpio_desc *desc = NULL;
	struct device_node *np;
	const char *name;
	enum gpio_lookup_flags lflags;
	enum gpiod_flags dflags;
	unsigned int i;
	int ret;

	for_each_available_child_of_node(chip->of_node, np) {
		if (!of_property_read_bool(np, "gpio-hog"))
			continue;

		for (i = 0;; i++) {
			desc = of_parse_own_gpio(np, chip, i, &name, &lflags,
						 &dflags);
			if (IS_ERR(desc))
				break;

			ret = gpiod_hog(desc, name, lflags, dflags);
			if (ret < 0) {
				of_node_put(np);
				return ret;
			}
		}
	}

	return 0;
}

/**
 * of_gpio_simple_xlate - translate gpiospec to the GPIO number and flags
 * @gc:		pointer to the gpio_chip structure
 * @gpiospec:	GPIO specifier as found in the device tree
 * @flags:	a flags pointer to fill in
 *
 * This is simple translation function, suitable for the most 1:1 mapped
 * GPIO chips. This function performs only one sanity check: whether GPIO
 * is less than ngpios (that is specified in the gpio_chip).
 */
int of_gpio_simple_xlate(struct gpio_chip *gc,
			 const struct of_phandle_args *gpiospec, u32 *flags)
{
	/*
	 * We're discouraging gpio_cells < 2, since that way you'll have to
	 * write your own xlate function (that will have to retrieve the GPIO
	 * number and the flags from a single gpio cell -- this is possible,
	 * but not recommended).
	 */
	if (gc->of_gpio_n_cells < 2) {
		WARN_ON(1);
		return -EINVAL;
	}

	if (WARN_ON(gpiospec->args_count < gc->of_gpio_n_cells))
		return -EINVAL;

	if (gpiospec->args[0] >= gc->ngpio)
		return -EINVAL;

	if (flags)
		*flags = gpiospec->args[1];

	return gpiospec->args[0];
}
EXPORT_SYMBOL(of_gpio_simple_xlate);

/**
 * of_mm_gpiochip_add_data - Add memory mapped GPIO chip (bank)
 * @np:		device node of the GPIO chip
 * @mm_gc:	pointer to the of_mm_gpio_chip allocated structure
 * @data:	driver data to store in the struct gpio_chip
 *
 * To use this function you should allocate and fill mm_gc with:
 *
 * 1) In the gpio_chip structure:
 *    - all the callbacks
 *    - of_gpio_n_cells
 *    - of_xlate callback (optional)
 *
 * 3) In the of_mm_gpio_chip structure:
 *    - save_regs callback (optional)
 *
 * If succeeded, this function will map bank's memory and will
 * do all necessary work for you. Then you'll able to use .regs
 * to manage GPIOs from the callbacks.
 */
int of_mm_gpiochip_add_data(struct device_node *np,
			    struct of_mm_gpio_chip *mm_gc,
			    void *data)
{
	int ret = -ENOMEM;
	struct gpio_chip *gc = &mm_gc->gc;

	gc->label = kasprintf(GFP_KERNEL, "%pOF", np);
	if (!gc->label)
		goto err0;

	mm_gc->regs = of_iomap(np, 0);
	if (!mm_gc->regs)
		goto err1;

	gc->base = -1;

	if (mm_gc->save_regs)
		mm_gc->save_regs(mm_gc);

	mm_gc->gc.of_node = np;

	ret = gpiochip_add_data(gc, data);
	if (ret)
		goto err2;

	return 0;
err2:
	iounmap(mm_gc->regs);
err1:
	kfree(gc->label);
err0:
	pr_err("%pOF: GPIO chip registration failed with status %d\n", np, ret);
	return ret;
}
EXPORT_SYMBOL(of_mm_gpiochip_add_data);

/**
 * of_mm_gpiochip_remove - Remove memory mapped GPIO chip (bank)
 * @mm_gc:	pointer to the of_mm_gpio_chip allocated structure
 */
void of_mm_gpiochip_remove(struct of_mm_gpio_chip *mm_gc)
{
	struct gpio_chip *gc = &mm_gc->gc;

	if (!mm_gc)
		return;

	gpiochip_remove(gc);
	iounmap(mm_gc->regs);
	kfree(gc->label);
}
EXPORT_SYMBOL(of_mm_gpiochip_remove);

static void of_gpiochip_init_valid_mask(struct gpio_chip *chip)
{
	int len, i;
	u32 start, count;
	struct device_node *np = chip->of_node;

	len = of_property_count_u32_elems(np,  "gpio-reserved-ranges");
	if (len < 0 || len % 2 != 0)
		return;

	for (i = 0; i < len; i += 2) {
		of_property_read_u32_index(np, "gpio-reserved-ranges",
					   i, &start);
		of_property_read_u32_index(np, "gpio-reserved-ranges",
					   i + 1, &count);
		if (start >= chip->ngpio || start + count >= chip->ngpio)
			continue;

		bitmap_clear(chip->valid_mask, start, count);
	}
};

#ifdef CONFIG_PINCTRL
static int of_gpiochip_add_pin_range(struct gpio_chip *chip)
{
	struct device_node *np = chip->of_node;
	struct of_phandle_args pinspec;
	struct pinctrl_dev *pctldev;
	int index = 0, ret;
	const char *name;
	static const char group_names_propname[] = "gpio-ranges-group-names";
	struct property *group_names;

	if (!np)
		return 0;

	group_names = of_find_property(np, group_names_propname, NULL);

	for (;; index++) {
		ret = of_parse_phandle_with_fixed_args(np, "gpio-ranges", 3,
				index, &pinspec);
		if (ret)
			break;

		pctldev = of_pinctrl_get(pinspec.np);
		of_node_put(pinspec.np);
		if (!pctldev)
			return -EPROBE_DEFER;

		if (pinspec.args[2]) {
			if (group_names) {
				of_property_read_string_index(np,
						group_names_propname,
						index, &name);
				if (strlen(name)) {
					pr_err("%pOF: Group name of numeric GPIO ranges must be the empty string.\n",
						np);
					break;
				}
			}
			/* npins != 0: linear range */
			ret = gpiochip_add_pin_range(chip,
					pinctrl_dev_get_devname(pctldev),
					pinspec.args[0],
					pinspec.args[1],
					pinspec.args[2]);
			if (ret)
				return ret;
		} else {
			/* npins == 0: special range */
			if (pinspec.args[1]) {
				pr_err("%pOF: Illegal gpio-range format.\n",
					np);
				break;
			}

			if (!group_names) {
				pr_err("%pOF: GPIO group range requested but no %s property.\n",
					np, group_names_propname);
				break;
			}

			ret = of_property_read_string_index(np,
						group_names_propname,
						index, &name);
			if (ret)
				break;

			if (!strlen(name)) {
				pr_err("%pOF: Group name of GPIO group range cannot be the empty string.\n",
				np);
				break;
			}

			ret = gpiochip_add_pingroup_range(chip, pctldev,
						pinspec.args[0], name);
			if (ret)
				return ret;
		}
	}

	return 0;
}

#else
static int of_gpiochip_add_pin_range(struct gpio_chip *chip) { return 0; }
#endif

int of_gpiochip_add(struct gpio_chip *chip)
{
	int status;

	if ((!chip->of_node) && (chip->parent))
		chip->of_node = chip->parent->of_node;

	if (!chip->of_node)
		return 0;

	if (!chip->of_xlate) {
		chip->of_gpio_n_cells = 2;
		chip->of_xlate = of_gpio_simple_xlate;
	}

	if (chip->of_gpio_n_cells > MAX_PHANDLE_ARGS)
		return -EINVAL;

	of_gpiochip_init_valid_mask(chip);

	status = of_gpiochip_add_pin_range(chip);
	if (status)
		return status;

	/* If the chip defines names itself, these take precedence */
	if (!chip->names)
		devprop_gpiochip_set_names(chip,
					   of_fwnode_handle(chip->of_node));

	of_node_get(chip->of_node);

	return of_gpiochip_scan_gpios(chip);
}

void of_gpiochip_remove(struct gpio_chip *chip)
{
	gpiochip_remove_pin_ranges(chip);
	of_node_put(chip->of_node);
}
