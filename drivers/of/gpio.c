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
 * of_get_gpio - Get a GPIO number from the device tree to use with GPIO API
 * @np:		device node to get GPIO from
 * @index:	index of the GPIO
 *
 * Returns GPIO number to use with Linux generic GPIO API, or one of the errno
 * value on the error condition.
 */
int of_get_gpio(struct device_node *np, int index)
{
	int ret = -EINVAL;
	struct device_node *gc;
	struct of_gpio_chip *of_gc = NULL;
	int size;
	const u32 *gpios;
	u32 nr_cells;
	int i;
	const void *gpio_spec;
	const u32 *gpio_cells;
	int gpio_index = 0;

	gpios = of_get_property(np, "gpios", &size);
	if (!gpios) {
		ret = -ENOENT;
		goto err0;
	}
	nr_cells = size / sizeof(u32);

	for (i = 0; i < nr_cells; gpio_index++) {
		const phandle *gpio_phandle;

		gpio_phandle = gpios + i;
		gpio_spec = gpio_phandle + 1;

		/* one cell hole in the gpios = <>; */
		if (!*gpio_phandle) {
			if (gpio_index == index)
				return -ENOENT;
			i++;
			continue;
		}

		gc = of_find_node_by_phandle(*gpio_phandle);
		if (!gc) {
			pr_debug("%s: could not find phandle for gpios\n",
				 np->full_name);
			goto err0;
		}

		of_gc = gc->data;
		if (!of_gc) {
			pr_debug("%s: gpio controller %s isn't registered\n",
				 np->full_name, gc->full_name);
			goto err1;
		}

		gpio_cells = of_get_property(gc, "#gpio-cells", &size);
		if (!gpio_cells || size != sizeof(*gpio_cells) ||
				*gpio_cells != of_gc->gpio_cells) {
			pr_debug("%s: wrong #gpio-cells for %s\n",
				 np->full_name, gc->full_name);
			goto err1;
		}

		/* Next phandle is at phandle cells + #gpio-cells */
		i += sizeof(*gpio_phandle) / sizeof(u32) + *gpio_cells;
		if (i >= nr_cells + 1) {
			pr_debug("%s: insufficient gpio-spec length\n",
				 np->full_name);
			goto err1;
		}

		if (gpio_index == index)
			break;

		of_gc = NULL;
		of_node_put(gc);
	}

	if (!of_gc) {
		ret = -ENOENT;
		goto err0;
	}

	ret = of_gc->xlate(of_gc, np, gpio_spec);
	if (ret < 0)
		goto err1;

	ret += of_gc->gc.base;
err1:
	of_node_put(gc);
err0:
	pr_debug("%s exited with status %d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL(of_get_gpio);

/**
 * of_gpio_simple_xlate - translate gpio_spec to the GPIO number
 * @of_gc:	pointer to the of_gpio_chip structure
 * @np:		device node of the GPIO chip
 * @gpio_spec:	gpio specifier as found in the device tree
 *
 * This is simple translation function, suitable for the most 1:1 mapped
 * gpio chips. This function performs only one sanity check: whether gpio
 * is less than ngpios (that is specified in the gpio_chip).
 */
int of_gpio_simple_xlate(struct of_gpio_chip *of_gc, struct device_node *np,
			 const void *gpio_spec)
{
	const u32 *gpio = gpio_spec;

	if (*gpio > of_gc->gc.ngpio)
		return -EINVAL;

	return *gpio;
}
EXPORT_SYMBOL(of_gpio_simple_xlate);

/* Should be sufficient for now, later we'll use dynamic bases. */
#if defined(CONFIG_PPC32) || defined(CONFIG_SPARC32)
#define GPIOS_PER_CHIP 32
#else
#define GPIOS_PER_CHIP 64
#endif

static int of_get_gpiochip_base(struct device_node *np)
{
	struct device_node *gc = NULL;
	int gpiochip_base = 0;

	while ((gc = of_find_all_nodes(gc))) {
		if (!of_get_property(gc, "gpio-controller", NULL))
			continue;

		if (gc != np) {
			gpiochip_base += GPIOS_PER_CHIP;
			continue;
		}

		of_node_put(gc);

		if (gpiochip_base >= ARCH_NR_GPIOS)
			return -ENOSPC;

		return gpiochip_base;
	}

	return -ENOENT;
}

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

	gc->base = of_get_gpiochip_base(np);
	if (gc->base < 0) {
		ret = gc->base;
		goto err1;
	}

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
