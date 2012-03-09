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

void of_platform_reset_gpio_probe(void)
{
	int ret;
	handle = of_get_named_gpio(of_find_node_by_path("/"),
				   "hard-reset-gpios", 0);

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
