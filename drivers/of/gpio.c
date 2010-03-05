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

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <asm/prom.h>

/**
 * of_get_gpio_flags - Get a GPIO number and flags to use with GPIO API
 * @np:		device node to get GPIO from
 * @index:	index of the GPIO
 * @flags:	a flags pointer to fill in
 *
 * Returns GPIO number to use with Linux generic GPIO API, or one of the errno
 * value on the error condition. If @flags is not NULL the function also fills
 * in flags for the GPIO.
 */
int of_get_gpio_flags(struct device_node *np, int index,
		      enum of_gpio_flags *flags)
{
	int ret;
	struct device_node *gc;
	struct of_gpio_chip *of_gc = NULL;
	int size;
	const void *gpio_spec;
	const __be32 *gpio_cells;

	ret = of_parse_phandles_with_args(np, "gpios", "#gpio-cells", index,
					  &gc, &gpio_spec);
	if (ret) {
		pr_debug("%s: can't parse gpios property\n", __func__);
		goto err0;
	}

	of_gc = gc->data;
	if (!of_gc) {
		pr_debug("%s: gpio controller %s isn't registered\n",
			 np->full_name, gc->full_name);
		ret = -ENODEV;
		goto err1;
	}

	gpio_cells = of_get_property(gc, "#gpio-cells", &size);
	if (!gpio_cells || size != sizeof(*gpio_cells) ||
			be32_to_cpup(gpio_cells) != of_gc->gpio_cells) {
		pr_debug("%s: wrong #gpio-cells for %s\n",
			 np->full_name, gc->full_name);
		ret = -EINVAL;
		goto err1;
	}

	/* .xlate might decide to not fill in the flags, so clear it. */
	if (flags)
		*flags = 0;

	ret = of_gc->xlate(of_gc, np, gpio_spec, flags);
	if (ret < 0)
		goto err1;

	ret += of_gc->gc.base;
err1:
	of_node_put(gc);
err0:
	pr_debug("%s exited with status %d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL(of_get_gpio_flags);

/**
 * of_gpio_count - Count GPIOs for a device
 * @np:		device node to count GPIOs for
 *
 * The function returns the count of GPIOs specified for a node.
 *
 * Note that the empty GPIO specifiers counts too. For example,
 *
 * gpios = <0
 *          &pio1 1 2
 *          0
 *          &pio2 3 4>;
 *
 * defines four GPIOs (so this function will return 4), two of which
 * are not specified.
 */
unsigned int of_gpio_count(struct device_node *np)
{
	unsigned int cnt = 0;

	do {
		int ret;

		ret = of_parse_phandles_with_args(np, "gpios", "#gpio-cells",
						  cnt, NULL, NULL);
		/* A hole in the gpios = <> counts anyway. */
		if (ret < 0 && ret != -EEXIST)
			break;
	} while (++cnt);

	return cnt;
}
EXPORT_SYMBOL(of_gpio_count);

/**
 * of_gpio_simple_xlate - translate gpio_spec to the GPIO number and flags
 * @of_gc:	pointer to the of_gpio_chip structure
 * @np:		device node of the GPIO chip
 * @gpio_spec:	gpio specifier as found in the device tree
 * @flags:	a flags pointer to fill in
 *
 * This is simple translation function, suitable for the most 1:1 mapped
 * gpio chips. This function performs only one sanity check: whether gpio
 * is less than ngpios (that is specified in the gpio_chip).
 */
int of_gpio_simple_xlate(struct of_gpio_chip *of_gc, struct device_node *np,
			 const void *gpio_spec, enum of_gpio_flags *flags)
{
	const __be32 *gpio = gpio_spec;
	const u32 n = be32_to_cpup(gpio);

	/*
	 * We're discouraging gpio_cells < 2, since that way you'll have to
	 * write your own xlate function (that will have to retrive the GPIO
	 * number and the flags from a single gpio cell -- this is possible,
	 * but not recommended).
	 */
	if (of_gc->gpio_cells < 2) {
		WARN_ON(1);
		return -EINVAL;
	}

	if (n > of_gc->gc.ngpio)
		return -EINVAL;

	if (flags)
		*flags = be32_to_cpu(gpio[1]);

	return n;
}
EXPORT_SYMBOL(of_gpio_simple_xlate);

/**
 * of_mm_gpiochip_add - Add memory mapped GPIO chip (bank)
 * @np:		device node of the GPIO chip
 * @mm_gc:	pointer to the of_mm_gpio_chip allocated structure
 *
 * To use this function you should allocate and fill mm_gc with:
 *
 * 1) In the gpio_chip structure:
 *    - all the callbacks
 *
 * 2) In the of_gpio_chip structure:
 *    - gpio_cells
 *    - xlate callback (optional)
 *
 * 3) In the of_mm_gpio_chip structure:
 *    - save_regs callback (optional)
 *
 * If succeeded, this function will map bank's memory and will
 * do all necessary work for you. Then you'll able to use .regs
 * to manage GPIOs from the callbacks.
 */
int of_mm_gpiochip_add(struct device_node *np,
		       struct of_mm_gpio_chip *mm_gc)
{
	int ret = -ENOMEM;
	struct of_gpio_chip *of_gc = &mm_gc->of_gc;
	struct gpio_chip *gc = &of_gc->gc;

	gc->label = kstrdup(np->full_name, GFP_KERNEL);
	if (!gc->label)
		goto err0;

	mm_gc->regs = of_iomap(np, 0);
	if (!mm_gc->regs)
		goto err1;

	gc->base = -1;

	if (!of_gc->xlate)
		of_gc->xlate = of_gpio_simple_xlate;

	if (mm_gc->save_regs)
		mm_gc->save_regs(mm_gc);

	np->data = of_gc;

	ret = gpiochip_add(gc);
	if (ret)
		goto err2;

	/* We don't want to lose the node and its ->data */
	of_node_get(np);

	pr_debug("%s: registered as generic GPIO chip, base is %d\n",
		 np->full_name, gc->base);
	return 0;
err2:
	np->data = NULL;
	iounmap(mm_gc->regs);
err1:
	kfree(gc->label);
err0:
	pr_err("%s: GPIO chip registration failed with status %d\n",
	       np->full_name, ret);
	return ret;
}
EXPORT_SYMBOL(of_mm_gpiochip_add);
