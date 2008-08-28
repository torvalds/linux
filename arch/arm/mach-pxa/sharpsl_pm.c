/*
 * Battery and Power Management code for the Sharp SL-C7xx and SL-Cxx00
 * series of PDAs
 *
 * Copyright (c) 2004-2005 Richard Purdie
 *
 * Based on code written by Sharp for 2.4 kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#undef DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/apm-emulation.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <mach/pm.h>
#include <mach/pxa-regs.h>
#include <mach/pxa2xx-gpio.h>
#include <mach/sharpsl.h>
#include "sharpsl.h"

struct battery_thresh spitz_battery_levels_acin[] = {
	{ 213, 100},
	{ 212,  98},
	{ 211,  95},
	{ 210,  93},
	{ 209,  90},
	{ 208,  88},
	{ 207,  85},
	{ 206,  83},
	{ 205,  80},
	{ 204,  78},
	{ 203,  75},
	{ 202,  73},
	{ 201,  70},
	{ 200,  68},
	{ 199,  65},
	{ 198,  63},
	{ 197,  60},
	{ 196,  58},
	{ 195,  55},
	{ 194,  53},
	{ 193,  50},
	{ 192,  48},
	{ 192,  45},
	{ 191,  43},
	{ 191,  40},
	{ 190,  38},
	{ 190,  35},
	{ 189,  33},
	{ 188,  30},
	{ 187,  28},
	{ 186,  25},
	{ 185,  23},
	{ 184,  20},
	{ 183,  18},
	{ 182,  15},
	{ 181,  13},
	{ 180,  10},
	{ 179,   8},
	{ 178,   5},
	{   0,   0},
};

struct battery_thresh  spitz_battery_levels_noac[] = {
	{ 213, 100},
	{ 212,  98},
	{ 211,  95},
	{ 210,  93},
	{ 209,  90},
	{ 208,  88},
	{ 207,  85},
	{ 206,  83},
	{ 205,  80},
	{ 204,  78},
	{ 203,  75},
	{ 202,  73},
	{ 201,  70},
	{ 200,  68},
	{ 199,  65},
	{ 198,  63},
	{ 197,  60},
	{ 196,  58},
	{ 195,  55},
	{ 194,  53},
	{ 193,  50},
	{ 192,  48},
	{ 191,  45},
	{ 190,  43},
	{ 189,  40},
	{ 188,  38},
	{ 187,  35},
	{ 186,  33},
	{ 185,  30},
	{ 184,  28},
	{ 183,  25},
	{ 182,  23},
	{ 181,  20},
	{ 180,  18},
	{ 179,  15},
	{ 178,  13},
	{ 177,  10},
	{ 176,   8},
	{ 175,   5},
	{   0,   0},
};

/* MAX1111 Commands */
#define MAXCTRL_PD0      1u << 0
#define MAXCTRL_PD1      1u << 1
#define MAXCTRL_SGL      1u << 2
#define MAXCTRL_UNI      1u << 3
#define MAXCTRL_SEL_SH   4
#define MAXCTRL_STR      1u << 7

/*
 * Read MAX1111 ADC
 */
int sharpsl_pm_pxa_read_max1111(int channel)
{
	if (machine_is_tosa()) // Ugly, better move this function into another module
	    return 0;

#ifdef CONFIG_SENSORS_MAX1111
	extern int max1111_read_channel(int);

	/* max1111 accepts channels from 0-3, however,
	 * it is encoded from 0-7 here in the code.
	 */
	return max1111_read_channel(channel >> 1);
#else
	return corgi_ssp_max1111_get((channel << MAXCTRL_SEL_SH) | MAXCTRL_PD0 | MAXCTRL_PD1
			| MAXCTRL_SGL | MAXCTRL_UNI | MAXCTRL_STR);
#endif
}

void sharpsl_pm_pxa_init(void)
{
	pxa_gpio_mode(sharpsl_pm.machinfo->gpio_acin | GPIO_IN);
	pxa_gpio_mode(sharpsl_pm.machinfo->gpio_batfull | GPIO_IN);
	pxa_gpio_mode(sharpsl_pm.machinfo->gpio_batlock | GPIO_IN);

	/* Register interrupt handlers */
	if (request_irq(IRQ_GPIO(sharpsl_pm.machinfo->gpio_acin), sharpsl_ac_isr, IRQF_DISABLED, "AC Input Detect", sharpsl_ac_isr)) {
		dev_err(sharpsl_pm.dev, "Could not get irq %d.\n", IRQ_GPIO(sharpsl_pm.machinfo->gpio_acin));
	}
	else set_irq_type(IRQ_GPIO(sharpsl_pm.machinfo->gpio_acin),IRQ_TYPE_EDGE_BOTH);

	if (request_irq(IRQ_GPIO(sharpsl_pm.machinfo->gpio_batlock), sharpsl_fatal_isr, IRQF_DISABLED, "Battery Cover", sharpsl_fatal_isr)) {
		dev_err(sharpsl_pm.dev, "Could not get irq %d.\n", IRQ_GPIO(sharpsl_pm.machinfo->gpio_batlock));
	}
	else set_irq_type(IRQ_GPIO(sharpsl_pm.machinfo->gpio_batlock),IRQ_TYPE_EDGE_FALLING);

	if (sharpsl_pm.machinfo->gpio_fatal) {
		if (request_irq(IRQ_GPIO(sharpsl_pm.machinfo->gpio_fatal), sharpsl_fatal_isr, IRQF_DISABLED, "Fatal Battery", sharpsl_fatal_isr)) {
			dev_err(sharpsl_pm.dev, "Could not get irq %d.\n", IRQ_GPIO(sharpsl_pm.machinfo->gpio_fatal));
		}
		else set_irq_type(IRQ_GPIO(sharpsl_pm.machinfo->gpio_fatal),IRQ_TYPE_EDGE_FALLING);
	}

	if (sharpsl_pm.machinfo->batfull_irq)
	{
		/* Register interrupt handler. */
		if (request_irq(IRQ_GPIO(sharpsl_pm.machinfo->gpio_batfull), sharpsl_chrg_full_isr, IRQF_DISABLED, "CO", sharpsl_chrg_full_isr)) {
			dev_err(sharpsl_pm.dev, "Could not get irq %d.\n", IRQ_GPIO(sharpsl_pm.machinfo->gpio_batfull));
		}
		else set_irq_type(IRQ_GPIO(sharpsl_pm.machinfo->gpio_batfull),IRQ_TYPE_EDGE_RISING);
	}
}

void sharpsl_pm_pxa_remove(void)
{
	free_irq(IRQ_GPIO(sharpsl_pm.machinfo->gpio_acin), sharpsl_ac_isr);
	free_irq(IRQ_GPIO(sharpsl_pm.machinfo->gpio_batlock), sharpsl_fatal_isr);

	if (sharpsl_pm.machinfo->gpio_fatal)
		free_irq(IRQ_GPIO(sharpsl_pm.machinfo->gpio_fatal), sharpsl_fatal_isr);

	if (sharpsl_pm.machinfo->batfull_irq)
		free_irq(IRQ_GPIO(sharpsl_pm.machinfo->gpio_batfull), sharpsl_chrg_full_isr);
}
