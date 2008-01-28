/*
 *  linux/arch/arm/mach-pxa/generic.c
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * Code common to all PXA machines.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Since this file should be linked before any other machine specific file,
 * the __initcall() here will be executed first.  This serves as default
 * initialization stuff for PXA machines which can be overridden later if
 * need be.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/pm.h>
#include <linux/string.h>
#include <linux/sysdev.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/gpio.h>

#include "generic.h"

/*
 * Get the clock frequency as reflected by CCCR and the turbo flag.
 * We assume these values have been applied via a fcs.
 * If info is not 0 we also display the current settings.
 */
unsigned int get_clk_frequency_khz(int info)
{
	if (cpu_is_pxa21x() || cpu_is_pxa25x())
		return pxa25x_get_clk_frequency_khz(info);
	else if (cpu_is_pxa27x())
		return pxa27x_get_clk_frequency_khz(info);
	else
		return pxa3xx_get_clk_frequency_khz(info);
}
EXPORT_SYMBOL(get_clk_frequency_khz);

/*
 * Return the current memory clock frequency in units of 10kHz
 */
unsigned int get_memclk_frequency_10khz(void)
{
	if (cpu_is_pxa21x() || cpu_is_pxa25x())
		return pxa25x_get_memclk_frequency_10khz();
	else if (cpu_is_pxa27x())
		return pxa27x_get_memclk_frequency_10khz();
	else
		return pxa3xx_get_memclk_frequency_10khz();
}
EXPORT_SYMBOL(get_memclk_frequency_10khz);

/*
 * Handy function to set GPIO alternate functions
 */
int pxa_last_gpio;

int pxa_gpio_mode(int gpio_mode)
{
	unsigned long flags;
	int gpio = gpio_mode & GPIO_MD_MASK_NR;
	int fn = (gpio_mode & GPIO_MD_MASK_FN) >> 8;
	int gafr;

	if (gpio > pxa_last_gpio)
		return -EINVAL;

	local_irq_save(flags);
	if (gpio_mode & GPIO_DFLT_LOW)
		GPCR(gpio) = GPIO_bit(gpio);
	else if (gpio_mode & GPIO_DFLT_HIGH)
		GPSR(gpio) = GPIO_bit(gpio);
	if (gpio_mode & GPIO_MD_MASK_DIR)
		GPDR(gpio) |= GPIO_bit(gpio);
	else
		GPDR(gpio) &= ~GPIO_bit(gpio);
	gafr = GAFR(gpio) & ~(0x3 << (((gpio) & 0xf)*2));
	GAFR(gpio) = gafr |  (fn  << (((gpio) & 0xf)*2));
	local_irq_restore(flags);

	return 0;
}

EXPORT_SYMBOL(pxa_gpio_mode);

int gpio_direction_input(unsigned gpio)
{
	unsigned long flags;
	u32 mask;

	if (gpio > pxa_last_gpio)
		return -EINVAL;

	mask = GPIO_bit(gpio);
	local_irq_save(flags);
	GPDR(gpio) &= ~mask;
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(gpio_direction_input);

int gpio_direction_output(unsigned gpio, int value)
{
	unsigned long flags;
	u32 mask;

	if (gpio > pxa_last_gpio)
		return -EINVAL;

	mask = GPIO_bit(gpio);
	local_irq_save(flags);
	if (value)
		GPSR(gpio) = mask;
	else
		GPCR(gpio) = mask;
	GPDR(gpio) |= mask;
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(gpio_direction_output);

/*
 * Return GPIO level
 */
int pxa_gpio_get_value(unsigned gpio)
{
	return __gpio_get_value(gpio);
}

EXPORT_SYMBOL(pxa_gpio_get_value);

/*
 * Set output GPIO level
 */
void pxa_gpio_set_value(unsigned gpio, int value)
{
	__gpio_set_value(gpio, value);
}

EXPORT_SYMBOL(pxa_gpio_set_value);

/*
 * Routine to safely enable or disable a clock in the CKEN
 */
void __pxa_set_cken(int clock, int enable)
{
	unsigned long flags;
	local_irq_save(flags);

	if (enable)
		CKEN |= (1 << clock);
	else
		CKEN &= ~(1 << clock);

	local_irq_restore(flags);
}

EXPORT_SYMBOL(__pxa_set_cken);

/*
 * Intel PXA2xx internal register mapping.
 *
 * Note 1: not all PXA2xx variants implement all those addresses.
 *
 * Note 2: virtual 0xfffe0000-0xffffffff is reserved for the vector table
 *         and cache flush area.
 */
static struct map_desc standard_io_desc[] __initdata = {
  	{	/* Devs */
		.virtual	=  0xf2000000,
		.pfn		= __phys_to_pfn(0x40000000),
		.length		= 0x02000000,
		.type		= MT_DEVICE
	}, {	/* LCD */
		.virtual	=  0xf4000000,
		.pfn		= __phys_to_pfn(0x44000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* Mem Ctl */
		.virtual	=  0xf6000000,
		.pfn		= __phys_to_pfn(0x48000000),
		.length		= 0x00200000,
		.type		= MT_DEVICE
	}, {	/* USB host */
		.virtual	=  0xf8000000,
		.pfn		= __phys_to_pfn(0x4c000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* Camera */
		.virtual	=  0xfa000000,
		.pfn		= __phys_to_pfn(0x50000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* IMem ctl */
		.virtual	=  0xfe000000,
		.pfn		= __phys_to_pfn(0x58000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* UNCACHED_PHYS_0 */
		.virtual	= 0xff000000,
		.pfn		= __phys_to_pfn(0x00000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}
};

void __init pxa_map_io(void)
{
	iotable_init(standard_io_desc, ARRAY_SIZE(standard_io_desc));
	get_clk_frequency_khz(1);
}

#ifdef CONFIG_PM

static unsigned long saved_gplr[4];
static unsigned long saved_gpdr[4];
static unsigned long saved_grer[4];
static unsigned long saved_gfer[4];

static int pxa_gpio_suspend(struct sys_device *dev, pm_message_t state)
{
	int i, gpio;

	for (gpio = 0, i = 0; gpio < pxa_last_gpio; gpio += 32, i++) {
		saved_gplr[i] = GPLR(gpio);
		saved_gpdr[i] = GPDR(gpio);
		saved_grer[i] = GRER(gpio);
		saved_gfer[i] = GFER(gpio);

		/* Clear GPIO transition detect bits */
		GEDR(gpio) = GEDR(gpio);
	}
	return 0;
}

static int pxa_gpio_resume(struct sys_device *dev)
{
	int i, gpio;

	for (gpio = 0, i = 0; gpio < pxa_last_gpio; gpio += 32, i++) {
		/* restore level with set/clear */
		GPSR(gpio) = saved_gplr[i];
		GPCR(gpio) = ~saved_gplr[i];

		GRER(gpio) = saved_grer[i];
		GFER(gpio) = saved_gfer[i];
		GPDR(gpio) = saved_gpdr[i];
	}
	return 0;
}
#else
#define pxa_gpio_suspend	NULL
#define pxa_gpio_resume		NULL
#endif

struct sysdev_class pxa_gpio_sysclass = {
	.name		= "gpio",
	.suspend	= pxa_gpio_suspend,
	.resume		= pxa_gpio_resume,
};

static int __init pxa_gpio_init(void)
{
	return sysdev_class_register(&pxa_gpio_sysclass);
}

core_initcall(pxa_gpio_init);
