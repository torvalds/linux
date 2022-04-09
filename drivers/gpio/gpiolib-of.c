// SPDX-License-Identifier: GPL-2.0+
/*
 * OF helpers for the GPIO API
 *
 * Copyright (c) 2007-2008  MontaVista Software, Inc.
 *
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
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
#include "gpiolib-of.h"

/**
 * of_gpio_spi_cs_get_count() - special GPIO counting for SPI
 * @dev:    Consuming device
 * @con_id: Function within the GPIO consumer
 *
 * Some elder GPIO controllers need special quirks. Currently we handle
 * the Freescale and PPC GPIO controller with bindings that doesn't use the
 * established "cs-gpios" for chip selects but instead rely on
 * "gpios" for the chip select lines. If we detect this, we redirect
 * the counting of "cs-gpios" to count "gpios" transparent to the
 * driver.
 */
static int of_gpio_spi_cs_get_count(struct device *dev, const char *con_id)
{
	struct device_node *np = dev->of_node;

	if (!IS_ENABLED(CONFIG_SPI_MASTER))
		return 0;
	if (!con_id || strcmp(con_id, "cs"))
		return 0;
	if (!of_device_is_compatible(np, "fsl,spi") &&
	    !of_device_is_compatible(np, "aeroflexgaisler,spictrl") &&
	    !of_device_is_compatible(np, "ibm,ppc4xx-spi"))
		return 0;
	return of_gpio_named_count(np, "gpios");
}

/*
 * This is used by external users of of_gpio_count() from <linux/of_gpio.h>
 *
 * FIXME: get rid of those external users by converting them to GPIO
 * descriptors and let them all use gpiod_count()
 */
int of_gpio_get_count(struct device *dev, const char *con_id)
{
	int ret;
	char propname[32];
	unsigned int i;

	ret = of_gpio_spi_cs_get_count(dev, con_id);
	if (ret > 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(gpio_suffixes); i++) {
		if (con_id)
			snprintf(propname, sizeof(propname), "%s-%s",
				 con_id, gpio_suffixes[i]);
		else
			snprintf(propname, sizeof(propname), "%s",
				 gpio_suffixes[i]);

		ret = of_gpio_named_count(dev->of_node, propname);
		if (ret > 0)
			break;
	}
	return ret ? ret : -ENOENT;
}

static int of_gpiochip_match_node_and_xlate(struct gpio_chip *chip, void *data)
{
	struct of_phandle_args *gpiospec = data;

	return chip->gpiodev->dev.of_node == gpiospec->np &&
				chip->of_xlate &&
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

/**
 * of_gpio_need_valid_mask() - figure out if the OF GPIO driver needs
 * to set the .valid_mask
 * @gc: the target gpio_chip
 *
 * Return: true if the valid mask needs to be set
 */
bool of_gpio_need_valid_mask(const struct gpio_chip *gc)
{
	int size;
	struct device_node *np = gc->of_node;

	size = of_property_count_u32_elems(np,  "gpio-reserved-ranges");
	if (size > 0 && size % 2 == 0)
		return true;
	return false;
}

static void of_gpio_flags_quirks(struct device_node *np,
				 const char *propname,
				 enum of_gpio_flags *flags,
				 int index)
{
	/*
	 * Some GPIO fixed regulator quirks.
	 * Note that active low is the default.
	 */
	if (IS_ENABLED(CONFIG_REGULATOR) &&
	    (of_device_is_compatible(np, "regulator-fixed") ||
	     of_device_is_compatible(np, "reg-fixed-voltage") ||
	     (!(strcmp(propname, "enable-gpio") &&
		strcmp(propname, "enable-gpios")) &&
	      of_device_is_compatible(np, "regulator-gpio")))) {
		bool active_low = !of_property_read_bool(np,
							 "enable-active-high");
		/*
		 * The regulator GPIO handles are specified such that the
		 * presence or absence of "enable-active-high" solely controls
		 * the polarity of the GPIO line. Any phandle flags must
		 * be actively ignored.
		 */
		if ((*flags & OF_GPIO_ACTIVE_LOW) && !active_low) {
			pr_warn("%s GPIO handle specifies active low - ignored\n",
				of_node_full_name(np));
			*flags &= ~OF_GPIO_ACTIVE_LOW;
		}
		if (active_low)
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

	/*
	 * Legacy handling of SPI active high chip select. If we have a
	 * property named "cs-gpios" we need to inspect the child node
	 * to determine if the flags should have inverted semantics.
	 */
	if (IS_ENABLED(CONFIG_SPI_MASTER) && !strcmp(propname, "cs-gpios") &&
	    of_property_read_bool(np, "cs-gpios")) {
		struct device_node *child;
		u32 cs;
		int ret;

		for_each_child_of_node(np, child) {
			ret = of_property_read_u32(child, "reg", &cs);
			if (ret)
				continue;
			if (cs == index) {
				/*
				 * SPI children have active low chip selects
				 * by default. This can be specified negatively
				 * by just omitting "spi-cs-high" in the
				 * device node, or actively by tagging on
				 * GPIO_ACTIVE_LOW as flag in the device
				 * tree. If the line is simultaneously
				 * tagged as active low in the device tree
				 * and has the "spi-cs-high" set, we get a
				 * conflict and the "spi-cs-high" flag will
				 * take precedence.
				 */
				if (of_property_read_bool(child, "spi-cs-high")) {
					if (*flags & OF_GPIO_ACTIVE_LOW) {
						pr_warn("%s GPIO handle specifies active low - ignored\n",
							of_node_full_name(child));
						*flags &= ~OF_GPIO_ACTIVE_LOW;
					}
				} else {
					if (!(*flags & OF_GPIO_ACTIVE_LOW))
						pr_info("%s enforce active low on chipselect handle\n",
							of_node_full_name(child));
					*flags |= OF_GPIO_ACTIVE_LOW;
				}
				of_node_put(child);
				break;
			}
		}
	}

	/* Legacy handling of stmmac's active-low PHY reset line */
	if (IS_ENABLED(CONFIG_STMMAC_ETH) &&
	    !strcmp(propname, "snps,reset-gpio") &&
	    of_property_read_bool(np, "snps,reset-active-low"))
		*flags |= OF_GPIO_ACTIVE_LOW;
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
static struct gpio_desc *of_get_named_gpiod_flags(struct device_node *np,
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
		of_gpio_flags_quirks(np, propname, flags, index);

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
EXPORT_SYMBOL_GPL(of_get_named_gpio_flags);

/**
 * gpiod_get_from_of_node() - obtain a GPIO from an OF node
 * @node:	handle of the OF node
 * @propname:	name of the DT property representing the GPIO
 * @index:	index of the GPIO to obtain for the consumer
 * @dflags:	GPIO initialization flags
 * @label:	label to attach to the requested GPIO
 *
 * Returns:
 * On successful request the GPIO pin is configured in accordance with
 * provided @dflags.
 *
 * In case of error an ERR_PTR() is returned.
 */
struct gpio_desc *gpiod_get_from_of_node(struct device_node *node,
					 const char *propname, int index,
					 enum gpiod_flags dflags,
					 const char *label)
{
	unsigned long lflags = GPIO_LOOKUP_FLAGS_DEFAULT;
	struct gpio_desc *desc;
	enum of_gpio_flags flags;
	bool active_low = false;
	bool single_ended = false;
	bool open_drain = false;
	bool transitory = false;
	int ret;

	desc = of_get_named_gpiod_flags(node, propname,
					index, &flags);

	if (!desc || IS_ERR(desc)) {
		return desc;
	}

	active_low = flags & OF_GPIO_ACTIVE_LOW;
	single_ended = flags & OF_GPIO_SINGLE_ENDED;
	open_drain = flags & OF_GPIO_OPEN_DRAIN;
	transitory = flags & OF_GPIO_TRANSITORY;

	ret = gpiod_request(desc, label);
	if (ret == -EBUSY && (dflags & GPIOD_FLAGS_BIT_NONEXCLUSIVE))
		return desc;
	if (ret)
		return ERR_PTR(ret);

	if (active_low)
		lflags |= GPIO_ACTIVE_LOW;

	if (single_ended) {
		if (open_drain)
			lflags |= GPIO_OPEN_DRAIN;
		else
			lflags |= GPIO_OPEN_SOURCE;
	}

	if (transitory)
		lflags |= GPIO_TRANSITORY;

	if (flags & OF_GPIO_PULL_UP)
		lflags |= GPIO_PULL_UP;

	if (flags & OF_GPIO_PULL_DOWN)
		lflags |= GPIO_PULL_DOWN;

	ret = gpiod_configure_flags(desc, propname, lflags, dflags);
	if (ret < 0) {
		gpiod_put(desc);
		return ERR_PTR(ret);
	}

	return desc;
}
EXPORT_SYMBOL_GPL(gpiod_get_from_of_node);

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
 * The old Freescale bindings use simply "gpios" as name for the chip select
 * lines rather than "cs-gpios" like all other SPI hardware. Account for this
 * with a special quirk.
 */
static struct gpio_desc *of_find_spi_cs_gpio(struct device *dev,
					     const char *con_id,
					     unsigned int idx,
					     unsigned long *flags)
{
	struct device_node *np = dev->of_node;

	if (!IS_ENABLED(CONFIG_SPI_MASTER))
		return ERR_PTR(-ENOENT);

	/* Allow this specifically for Freescale and PPC devices */
	if (!of_device_is_compatible(np, "fsl,spi") &&
	    !of_device_is_compatible(np, "aeroflexgaisler,spictrl") &&
	    !of_device_is_compatible(np, "ibm,ppc4xx-spi"))
		return ERR_PTR(-ENOENT);
	/* Allow only if asking for "cs-gpios" */
	if (!con_id || strcmp(con_id, "cs"))
		return ERR_PTR(-ENOENT);

	/*
	 * While all other SPI controllers use "cs-gpios" the Freescale
	 * uses just "gpios" so translate to that when "cs-gpios" is
	 * requested.
	 */
	return of_find_gpio(dev, NULL, idx, flags);
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

	i = match_string(whitelist, ARRAY_SIZE(whitelist), con_id);
	if (i < 0)
		return ERR_PTR(-ENOENT);

	desc = of_get_named_gpiod_flags(np, con_id, 0, of_flags);
	return desc;
}

static struct gpio_desc *of_find_arizona_gpio(struct device *dev,
					      const char *con_id,
					      enum of_gpio_flags *of_flags)
{
	if (!IS_ENABLED(CONFIG_MFD_ARIZONA))
		return ERR_PTR(-ENOENT);

	if (!con_id || strcmp(con_id, "wlf,reset"))
		return ERR_PTR(-ENOENT);

	return of_get_named_gpiod_flags(dev->of_node, con_id, 0, of_flags);
}

static struct gpio_desc *of_find_usb_gpio(struct device *dev,
					  const char *con_id,
					  enum of_gpio_flags *of_flags)
{
	/*
	 * Currently this USB quirk is only for the Fairchild FUSB302 host which is using
	 * an undocumented DT GPIO line named "fcs,int_n" without the compulsory "-gpios"
	 * suffix.
	 */
	if (!IS_ENABLED(CONFIG_TYPEC_FUSB302))
		return ERR_PTR(-ENOENT);

	if (!con_id || strcmp(con_id, "fcs,int_n"))
		return ERR_PTR(-ENOENT);

	return of_get_named_gpiod_flags(dev->of_node, con_id, 0, of_flags);
}

struct gpio_desc *of_find_gpio(struct device *dev, const char *con_id,
			       unsigned int idx, unsigned long *flags)
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

		if (!IS_ERR(desc) || PTR_ERR(desc) != -ENOENT)
			break;
	}

	if (PTR_ERR(desc) == -ENOENT) {
		/* Special handling for SPI GPIOs if used */
		desc = of_find_spi_gpio(dev, con_id, &of_flags);
	}

	if (PTR_ERR(desc) == -ENOENT) {
		/* This quirk looks up flags and all */
		desc = of_find_spi_cs_gpio(dev, con_id, idx, flags);
		if (!IS_ERR(desc))
			return desc;
	}

	if (PTR_ERR(desc) == -ENOENT) {
		/* Special handling for regulator GPIOs if used */
		desc = of_find_regulator_gpio(dev, con_id, &of_flags);
	}

	if (PTR_ERR(desc) == -ENOENT)
		desc = of_find_arizona_gpio(dev, con_id, &of_flags);

	if (PTR_ERR(desc) == -ENOENT)
		desc = of_find_usb_gpio(dev, con_id, &of_flags);

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

	if (of_flags & OF_GPIO_PULL_UP)
		*flags |= GPIO_PULL_UP;
	if (of_flags & OF_GPIO_PULL_DOWN)
		*flags |= GPIO_PULL_DOWN;

	return desc;
}

/**
 * of_parse_own_gpio() - Get a GPIO hog descriptor, names and flags for GPIO API
 * @np:		device node to get GPIO from
 * @chip:	GPIO chip whose hog is parsed
 * @idx:	Index of the GPIO to parse
 * @name:	GPIO line name
 * @lflags:	bitmask of gpio_lookup_flags GPIO_* values - returned from
 *		of_find_gpio() or of_parse_own_gpio()
 * @dflags:	gpiod_flags - optional GPIO initialization flags
 *
 * Returns GPIO descriptor to use with Linux GPIO API, or one of the errno
 * value on the error condition.
 */
static struct gpio_desc *of_parse_own_gpio(struct device_node *np,
					   struct gpio_chip *chip,
					   unsigned int idx, const char **name,
					   unsigned long *lflags,
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
	*lflags = GPIO_LOOKUP_FLAGS_DEFAULT;
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
	if (xlate_flags & OF_GPIO_PULL_UP)
		*lflags |= GPIO_PULL_UP;
	if (xlate_flags & OF_GPIO_PULL_DOWN)
		*lflags |= GPIO_PULL_DOWN;

	if (of_property_read_bool(np, "input"))
		*dflags |= GPIOD_IN;
	else if (of_property_read_bool(np, "output-low"))
		*dflags |= GPIOD_OUT_LOW;
	else if (of_property_read_bool(np, "output-high"))
		*dflags |= GPIOD_OUT_HIGH;
	else {
		pr_warn("GPIO line %d (%pOFn): no hogging state specified, bailing out\n",
			desc_to_gpio(desc), np);
		return ERR_PTR(-EINVAL);
	}

	if (name && of_property_read_string(np, "line-name", name))
		*name = np->name;

	return desc;
}

/**
 * of_gpiochip_add_hog - Add all hogs in a hog device node
 * @chip:	gpio chip to act on
 * @hog:	device node describing the hogs
 *
 * Returns error if it fails otherwise 0 on success.
 */
static int of_gpiochip_add_hog(struct gpio_chip *chip, struct device_node *hog)
{
	enum gpiod_flags dflags;
	struct gpio_desc *desc;
	unsigned long lflags;
	const char *name;
	unsigned int i;
	int ret;

	for (i = 0;; i++) {
		desc = of_parse_own_gpio(hog, chip, i, &name, &lflags, &dflags);
		if (IS_ERR(desc))
			break;

		ret = gpiod_hog(desc, name, lflags, dflags);
		if (ret < 0)
			return ret;

#ifdef CONFIG_OF_DYNAMIC
		desc->hog = hog;
#endif
	}

	return 0;
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
	struct device_node *np;
	int ret;

	for_each_available_child_of_node(chip->of_node, np) {
		if (!of_property_read_bool(np, "gpio-hog"))
			continue;

		ret = of_gpiochip_add_hog(chip, np);
		if (ret < 0) {
			of_node_put(np);
			return ret;
		}

		of_node_set_flag(np, OF_POPULATED);
	}

	return 0;
}

#ifdef CONFIG_OF_DYNAMIC
/**
 * of_gpiochip_remove_hog - Remove all hogs in a hog device node
 * @chip:	gpio chip to act on
 * @hog:	device node describing the hogs
 */
static void of_gpiochip_remove_hog(struct gpio_chip *chip,
				   struct device_node *hog)
{
	struct gpio_desc *descs = chip->gpiodev->descs;
	unsigned int i;

	for (i = 0; i < chip->ngpio; i++) {
		if (test_bit(FLAG_IS_HOGGED, &descs[i].flags) &&
		    descs[i].hog == hog)
			gpiochip_free_own_desc(&descs[i]);
	}
}

static int of_gpiochip_match_node(struct gpio_chip *chip, void *data)
{
	return chip->gpiodev->dev.of_node == data;
}

static struct gpio_chip *of_find_gpiochip_by_node(struct device_node *np)
{
	return gpiochip_find(np, of_gpiochip_match_node);
}

static int of_gpio_notify(struct notifier_block *nb, unsigned long action,
			  void *arg)
{
	struct of_reconfig_data *rd = arg;
	struct gpio_chip *chip;
	int ret;

	/*
	 * This only supports adding and removing complete gpio-hog nodes.
	 * Modifying an existing gpio-hog node is not supported (except for
	 * changing its "status" property, which is treated the same as
	 * addition/removal).
	 */
	switch (of_reconfig_get_state_change(action, arg)) {
	case OF_RECONFIG_CHANGE_ADD:
		if (!of_property_read_bool(rd->dn, "gpio-hog"))
			return NOTIFY_OK;	/* not for us */

		if (of_node_test_and_set_flag(rd->dn, OF_POPULATED))
			return NOTIFY_OK;

		chip = of_find_gpiochip_by_node(rd->dn->parent);
		if (chip == NULL)
			return NOTIFY_OK;	/* not for us */

		ret = of_gpiochip_add_hog(chip, rd->dn);
		if (ret < 0) {
			pr_err("%s: failed to add hogs for %pOF\n", __func__,
			       rd->dn);
			of_node_clear_flag(rd->dn, OF_POPULATED);
			return notifier_from_errno(ret);
		}
		break;

	case OF_RECONFIG_CHANGE_REMOVE:
		if (!of_node_check_flag(rd->dn, OF_POPULATED))
			return NOTIFY_OK;	/* already depopulated */

		chip = of_find_gpiochip_by_node(rd->dn->parent);
		if (chip == NULL)
			return NOTIFY_OK;	/* not for us */

		of_gpiochip_remove_hog(chip, rd->dn);
		of_node_clear_flag(rd->dn, OF_POPULATED);
		break;
	}

	return NOTIFY_OK;
}

struct notifier_block gpio_of_notifier = {
	.notifier_call = of_gpio_notify,
};
#endif /* CONFIG_OF_DYNAMIC */

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
static int of_gpio_simple_xlate(struct gpio_chip *gc,
				const struct of_phandle_args *gpiospec,
				u32 *flags)
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
EXPORT_SYMBOL_GPL(of_mm_gpiochip_add_data);

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
EXPORT_SYMBOL_GPL(of_mm_gpiochip_remove);

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
		if (start >= chip->ngpio || start + count > chip->ngpio)
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

	if (!of_property_read_bool(np, "gpio-ranges") &&
	    chip->of_gpio_ranges_fallback) {
		return chip->of_gpio_ranges_fallback(chip, np);
	}

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
	int ret;

	if (!chip->of_node)
		return 0;

	if (!chip->of_xlate) {
		chip->of_gpio_n_cells = 2;
		chip->of_xlate = of_gpio_simple_xlate;
	}

	if (chip->of_gpio_n_cells > MAX_PHANDLE_ARGS)
		return -EINVAL;

	of_gpiochip_init_valid_mask(chip);

	ret = of_gpiochip_add_pin_range(chip);
	if (ret)
		return ret;

	of_node_get(chip->of_node);

	ret = of_gpiochip_scan_gpios(chip);
	if (ret)
		of_node_put(chip->of_node);

	return ret;
}

void of_gpiochip_remove(struct gpio_chip *chip)
{
	of_node_put(chip->of_node);
}
