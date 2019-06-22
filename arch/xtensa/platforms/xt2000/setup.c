// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/xtensa/platforms/xt2000/setup.c
 *
 * Platform specific functions for the XT2000 board.
 *
 * Authors:	Chris Zankel <chris@zankel.net>
 *		Joe Taylor <joe@tensilica.com>
 *
 * Copyright 2001 - 2004 Tensilica Inc.
 */
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/stringify.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>

#include <asm/processor.h>
#include <asm/platform.h>
#include <asm/bootparam.h>
#include <platform/hardware.h>
#include <platform/serial.h>

/* Assumes s points to an 8-chr string.  No checking for NULL. */

static void led_print (int f, char *s)
{
	unsigned long* led_addr = (unsigned long*) (XT2000_LED_ADDR + 0xE0) + f;
	int i;
	for (i = f; i < 8; i++)
		if ((*led_addr++ = *s++) == 0)
		    break;
}

void platform_halt(void)
{
	led_print (0, "  HALT  ");
	local_irq_disable();
	while (1);
}

void platform_power_off(void)
{
	led_print (0, "POWEROFF");
	local_irq_disable();
	while (1);
}

void platform_restart(void)
{
	/* Flush and reset the mmu, simulate a processor reset, and
	 * jump to the reset vector. */
	cpu_reset();
	/* control never gets here */
}

void __init platform_setup(char** cmdline)
{
	led_print (0, "LINUX   ");
}

/* early initialization */

void __init platform_init(bp_tag_t *first)
{
}

/* Heartbeat. Let the LED blink. */

void platform_heartbeat(void)
{
	static int i=0, t = 0;

	if (--t < 0)
	{
		t = 59;
		led_print(7, i ? ".": " ");
		i ^= 1;
	}
}

//#define RS_TABLE_SIZE 2

#define _SERIAL_PORT(_base,_irq)					\
{									\
	.mapbase	= (_base),					\
	.membase	= (void*)(_base),				\
	.irq		= (_irq),					\
	.uartclk	= DUART16552_XTAL_FREQ,				\
	.iotype		= UPIO_MEM,					\
	.flags		= UPF_BOOT_AUTOCONF,				\
	.regshift	= 2,						\
}

static struct plat_serial8250_port xt2000_serial_data[] = {
#if XCHAL_HAVE_BE
	_SERIAL_PORT(DUART16552_1_ADDR + 3, DUART16552_1_INTNUM),
	_SERIAL_PORT(DUART16552_2_ADDR + 3, DUART16552_2_INTNUM),
#else
	_SERIAL_PORT(DUART16552_1_ADDR, DUART16552_1_INTNUM),
	_SERIAL_PORT(DUART16552_2_ADDR, DUART16552_2_INTNUM),
#endif
	{ }
};

static struct platform_device xt2000_serial8250_device = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev		= {
	    .platform_data = xt2000_serial_data,
	},
};

static struct resource xt2000_sonic_res[] = {
	{
		.start = SONIC83934_ADDR,
		.end   = SONIC83934_ADDR + 0xff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = SONIC83934_INTNUM,
		.end = SONIC83934_INTNUM,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device xt2000_sonic_device = {
	.name		= "xtsonic",
	.num_resources	= ARRAY_SIZE(xt2000_sonic_res),
	.resource		= xt2000_sonic_res,
};

static int __init xt2000_setup_devinit(void)
{
	platform_device_register(&xt2000_serial8250_device);
	platform_device_register(&xt2000_sonic_device);

	return 0;
}

device_initcall(xt2000_setup_devinit);
