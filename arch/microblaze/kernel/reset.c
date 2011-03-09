/*
 * Copyright (C) 2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2009 PetaLogix
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/of_platform.h>
#include <asm/prom.h>

/* Trigger specific functions */
#ifdef CONFIG_GPIOLIB

#include <linux/of_gpio.h>

static int handle; /* reset pin handle */
static unsigned int reset_val;

static int of_reset_gpio_handle(void)
{
	int ret; /* variable which stored handle reset gpio pin */
	struct device_node *root; /* root node */
	struct device_node *gpio; /* gpio node */
	struct gpio_chip *gc;
	u32 flags;
	const void *gpio_spec;

	/* find out root node */
	root = of_find_node_by_path("/");

	/* give me handle for gpio node to be possible allocate pin */
	ret = of_parse_phandles_with_args(root, "hard-reset-gpios",
				"#gpio-cells", 0, &gpio, &gpio_spec);
	if (ret) {
		pr_debug("%s: can't parse gpios property\n", __func__);
		goto err0;
	}

	gc = of_node_to_gpiochip(gpio);
	if (!gc) {
		pr_debug("%s: gpio controller %s isn't registered\n",
			 root->full_name, gpio->full_name);
		ret = -ENODEV;
		goto err1;
	}

	ret = gc->of_xlate(gc, root, gpio_spec, &flags);
	if (ret < 0)
		goto err1;

	ret += gc->base;
err1:
	of_node_put(gpio);
err0:
	pr_debug("%s exited with status %d\n", __func__, ret);
	return ret;
}

void of_platform_reset_gpio_probe(void)
{
	int ret;
	handle = of_reset_gpio_handle();

	if (!gpio_is_valid(handle)) {
		printk(KERN_INFO "Skipping unavailable RESET gpio %d (%s)\n",
				handle, "reset");
	}

	ret = gpio_request(handle, "reset");
	if (ret < 0) {
		printk(KERN_INFO "GPIO pin is already allocated\n");
		return;
	}

	/* get current setup value */
	reset_val = gpio_get_value(handle);
	/* FIXME maybe worth to perform any action */
	pr_debug("Reset: Gpio output state: 0x%x\n", reset_val);

	/* Setup GPIO as output */
	ret = gpio_direction_output(handle, 0);
	if (ret < 0)
		goto err;

	/* Setup output direction */
	gpio_set_value(handle, 0);

	printk(KERN_INFO "RESET: Registered gpio device: %d, current val: %d\n",
							handle, reset_val);
	return;
err:
	gpio_free(handle);
	return;
}


static void gpio_system_reset(void)
{
	gpio_set_value(handle, 1 - reset_val);
}
#else
#define gpio_system_reset() do {} while (0)
void of_platform_reset_gpio_probe(void)
{
	return;
}
#endif

void machine_restart(char *cmd)
{
	printk(KERN_NOTICE "Machine restart...\n");
	gpio_system_reset();
	dump_stack();
	while (1)
		;
}

void machine_shutdown(void)
{
	printk(KERN_NOTICE "Machine shutdown...\n");
	while (1)
		;
}

void machine_halt(void)
{
	printk(KERN_NOTICE "Machine halt...\n");
	while (1)
		;
}

void machine_power_off(void)
{
	printk(KERN_NOTICE "Machine power off...\n");
	while (1)
		;
}
