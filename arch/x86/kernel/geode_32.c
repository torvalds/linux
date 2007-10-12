/*
 * AMD Geode southbridge support code
 * Copyright (C) 2006, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <asm/msr.h>
#include <asm/geode.h>

static struct {
	char *name;
	u32 msr;
	int size;
	u32 base;
} lbars[] = {
	{ "geode-pms",   MSR_LBAR_PMS, LBAR_PMS_SIZE, 0 },
	{ "geode-acpi",  MSR_LBAR_ACPI, LBAR_ACPI_SIZE, 0 },
	{ "geode-gpio",  MSR_LBAR_GPIO, LBAR_GPIO_SIZE, 0 },
	{ "geode-mfgpt", MSR_LBAR_MFGPT, LBAR_MFGPT_SIZE, 0 }
};

static void __init init_lbars(void)
{
	u32 lo, hi;
	int i;

	for (i = 0; i < ARRAY_SIZE(lbars); i++) {
		rdmsr(lbars[i].msr, lo, hi);
		if (hi & 0x01)
			lbars[i].base = lo & 0x0000ffff;

		if (lbars[i].base == 0)
			printk(KERN_ERR "geode:  Couldn't initialize '%s'\n",
					lbars[i].name);
	}
}

int geode_get_dev_base(unsigned int dev)
{
	BUG_ON(dev >= ARRAY_SIZE(lbars));
	return lbars[dev].base;
}
EXPORT_SYMBOL_GPL(geode_get_dev_base);

/* === GPIO API === */

void geode_gpio_set(unsigned int gpio, unsigned int reg)
{
	u32 base = geode_get_dev_base(GEODE_DEV_GPIO);

	if (!base)
		return;

	if (gpio < 16)
		outl(1 << gpio, base + reg);
	else
		outl(1 << (gpio - 16), base + 0x80 + reg);
}
EXPORT_SYMBOL_GPL(geode_gpio_set);

void geode_gpio_clear(unsigned int gpio, unsigned int reg)
{
	u32 base = geode_get_dev_base(GEODE_DEV_GPIO);

	if (!base)
		return;

	if (gpio < 16)
		outl(1 << (gpio + 16), base + reg);
	else
		outl(1 << gpio, base + 0x80 + reg);
}
EXPORT_SYMBOL_GPL(geode_gpio_clear);

int geode_gpio_isset(unsigned int gpio, unsigned int reg)
{
	u32 base = geode_get_dev_base(GEODE_DEV_GPIO);

	if (!base)
		return 0;

	if (gpio < 16)
		return (inl(base + reg) & (1 << gpio)) ? 1 : 0;
	else
		return (inl(base + 0x80 + reg) & (1 << (gpio - 16))) ? 1 : 0;
}
EXPORT_SYMBOL_GPL(geode_gpio_isset);

void geode_gpio_set_irq(unsigned int group, unsigned int irq)
{
	u32 lo, hi;

	if (group > 7 || irq > 15)
		return;

	rdmsr(MSR_PIC_ZSEL_HIGH, lo, hi);

	lo &= ~(0xF << (group * 4));
	lo |= (irq & 0xF) << (group * 4);

	wrmsr(MSR_PIC_ZSEL_HIGH, lo, hi);
}
EXPORT_SYMBOL_GPL(geode_gpio_set_irq);

void geode_gpio_setup_event(unsigned int gpio, int pair, int pme)
{
	u32 base = geode_get_dev_base(GEODE_DEV_GPIO);
	u32 offset, shift, val;

	if (gpio >= 24)
		offset = GPIO_MAP_W;
	else if (gpio >= 16)
		offset = GPIO_MAP_Z;
	else if (gpio >= 8)
		offset = GPIO_MAP_Y;
	else
		offset = GPIO_MAP_X;

	shift = (gpio % 8) * 4;

	val = inl(base + offset);

	/* Clear whatever was there before */
	val &= ~(0xF << shift);

	/* And set the new value */

	val |= ((pair & 7) << shift);

	/* Set the PME bit if this is a PME event */

	if (pme)
		val |= (1 << (shift + 3));

	outl(val, base + offset);
}
EXPORT_SYMBOL_GPL(geode_gpio_setup_event);

static int __init geode_southbridge_init(void)
{
	int timers;

	if (!is_geode())
		return -ENODEV;

	init_lbars();
	timers = geode_mfgpt_detect();
	printk(KERN_INFO "geode:  %d MFGPT timers available.\n", timers);
	return 0;
}

postcore_initcall(geode_southbridge_init);
