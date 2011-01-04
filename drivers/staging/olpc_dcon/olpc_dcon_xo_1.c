/*
 * Mainly by David Woodhouse, somewhat modified by Jordan Crouse
 *
 * Copyright © 2006-2007  Red Hat, Inc.
 * Copyright © 2006-2007  Advanced Micro Devices, Inc.
 * Copyright © 2009       VIA Technology, Inc.
 * Copyright (c) 2010  Andres Salomon <dilinger@queued.net>
 *
 * This program is free software.  You can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */

#include <asm/olpc.h>

#include "olpc_dcon.h"

/* Base address of the GPIO registers */
static unsigned long gpio_base;

/*
 * List of GPIOs that we care about:
 * (in)  GPIO12   -- DCONBLANK
 * (in)  GPIO[56] -- DCONSTAT[01]
 * (out) GPIO11   -- DCONLOAD
 */

#define IN_GPIOS ((1<<5) | (1<<6) | (1<<7) | (1<<12))
#define OUT_GPIOS (1<<11)

static int dcon_init_xo_1(void)
{
	unsigned long lo, hi;
	unsigned char lob;

	rdmsr(MSR_LBAR_GPIO, lo, hi);

	/* Check the mask and whether GPIO is enabled (sanity check) */
	if (hi != 0x0000f001) {
		printk(KERN_ERR "GPIO not enabled -- cannot use DCON\n");
		return -ENODEV;
	}

	/* Mask off the IO base address */
	gpio_base = lo & 0x0000ff00;

	/* Turn off the event enable for GPIO7 just to be safe */
	outl(1 << (16+7), gpio_base + GPIOx_EVNT_EN);

	/* Set the directions for the GPIO pins */
	outl(OUT_GPIOS | (IN_GPIOS << 16), gpio_base + GPIOx_OUT_EN);
	outl(IN_GPIOS | (OUT_GPIOS << 16), gpio_base + GPIOx_IN_EN);

	/* Set up the interrupt mappings */

	/* Set the IRQ to pair 2 */
	geode_gpio_event_irq(OLPC_GPIO_DCON_IRQ, 2);

	/* Enable group 2 to trigger the DCON interrupt */
	geode_gpio_set_irq(2, DCON_IRQ);

	/* Select edge level for interrupt (in PIC) */
	lob = inb(0x4d0);
	lob &= ~(1 << DCON_IRQ);
	outb(lob, 0x4d0);

	/* Register the interupt handler */
	if (request_irq(DCON_IRQ, &dcon_interrupt, 0, "DCON", &dcon_driver))
		return -EIO;

	/* Clear INV_EN for GPIO7 (DCONIRQ) */
	outl((1<<(16+7)), gpio_base + GPIOx_INV_EN);

	/* Enable filter for GPIO12 (DCONBLANK) */
	outl(1<<(12), gpio_base + GPIOx_IN_FLTR_EN);

	/* Disable filter for GPIO7 */
	outl(1<<(16+7), gpio_base + GPIOx_IN_FLTR_EN);

	/* Disable event counter for GPIO7 (DCONIRQ) and GPIO12 (DCONBLANK) */

	outl(1<<(16+7), gpio_base + GPIOx_EVNTCNT_EN);
	outl(1<<(16+12), gpio_base + GPIOx_EVNTCNT_EN);

	/* Add GPIO12 to the Filter Event Pair #7 */
	outb(12, gpio_base + GPIO_FE7_SEL);

	/* Turn off negative Edge Enable for GPIO12 */
	outl(1<<(16+12), gpio_base + GPIOx_NEGEDGE_EN);

	/* Enable negative Edge Enable for GPIO7 */
	outl(1<<7, gpio_base + GPIOx_NEGEDGE_EN);

	/* Zero the filter amount for Filter Event Pair #7 */
	outw(0, gpio_base + GPIO_FLT7_AMNT);

	/* Clear the negative edge status for GPIO7 and GPIO12 */
	outl((1<<7) | (1<<12), gpio_base+0x4c);

	/* FIXME:  Clear the posiitive status as well, just to be sure */
	outl((1<<7) | (1<<12), gpio_base+0x48);

	/* Enable events for GPIO7 (DCONIRQ) and GPIO12 (DCONBLANK) */
	outl((1<<(7))|(1<<12), gpio_base + GPIOx_EVNT_EN);

	/* Determine the current state by reading the GPIO bit */
	/* Earlier stages of the boot process have established the state */
	dcon_source = inl(gpio_base + GPIOx_OUT_VAL) & (1<<11)
		? DCON_SOURCE_CPU
		: DCON_SOURCE_DCON;
	dcon_pending = dcon_source;

	return 0;
}

static void dcon_wiggle_xo_1(void)
{
	int x;

	/*
	 * According to HiMax, when powering the DCON up we should hold
	 * SMB_DATA high for 8 SMB_CLK cycles.  This will force the DCON
	 * state machine to reset to a (sane) initial state.  Mitch Bradley
	 * did some testing and discovered that holding for 16 SMB_CLK cycles
	 * worked a lot more reliably, so that's what we do here.
	 *
	 * According to the cs5536 spec, to set GPIO14 to SMB_CLK we must
	 * simultaneously set AUX1 IN/OUT to GPIO14; ditto for SMB_DATA and
	 * GPIO15.
 	 */
	geode_gpio_set(OLPC_GPIO_SMB_CLK|OLPC_GPIO_SMB_DATA, GPIO_OUTPUT_VAL);
	geode_gpio_set(OLPC_GPIO_SMB_CLK|OLPC_GPIO_SMB_DATA, GPIO_OUTPUT_ENABLE);
	geode_gpio_clear(OLPC_GPIO_SMB_CLK|OLPC_GPIO_SMB_DATA, GPIO_OUTPUT_AUX1);
	geode_gpio_clear(OLPC_GPIO_SMB_CLK|OLPC_GPIO_SMB_DATA, GPIO_OUTPUT_AUX2);
	geode_gpio_clear(OLPC_GPIO_SMB_CLK|OLPC_GPIO_SMB_DATA, GPIO_INPUT_AUX1);

	for (x = 0; x < 16; x++) {
		udelay(5);
		geode_gpio_clear(OLPC_GPIO_SMB_CLK, GPIO_OUTPUT_VAL);
		udelay(5);
		geode_gpio_set(OLPC_GPIO_SMB_CLK, GPIO_OUTPUT_VAL);
	}
	udelay(5);
	geode_gpio_set(OLPC_GPIO_SMB_CLK|OLPC_GPIO_SMB_DATA, GPIO_OUTPUT_AUX1);
	geode_gpio_set(OLPC_GPIO_SMB_CLK|OLPC_GPIO_SMB_DATA, GPIO_INPUT_AUX1);
}

static void dcon_set_dconload_1(int val)
{
	if (val)	
		outl(1<<11, gpio_base + GPIOx_OUT_VAL);
	else
		outl(1<<(11 + 16), gpio_base + GPIOx_OUT_VAL);
}

static int dcon_read_status_xo_1(void)
{
	int status = inl(gpio_base + GPIOx_READ_BACK) >> 5;
	
	/* Clear the negative edge status for GPIO7 */
	outl(1 << 7, gpio_base + GPIOx_NEGEDGE_STS);

	return status;
}

static struct dcon_platform_data dcon_pdata_xo_1 = {
	.init = dcon_init_xo_1,
	.bus_stabilize_wiggle = dcon_wiggle_xo_1,
	.set_dconload = dcon_set_dconload_1,
	.read_status = dcon_read_status_xo_1,
};
