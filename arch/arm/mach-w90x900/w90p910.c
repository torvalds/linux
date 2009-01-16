/*
 * linux/arch/arm/mach-w90x900/w90p910.c
 *
 * Based on linux/arch/arm/plat-s3c24xx/s3c244x.c by Ben Dooks
 *
 * Copyright (c) 2008 Nuvoton technology corporation
 * All rights reserved.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * W90P910 cpu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/irq.h>

#include <mach/hardware.h>
#include <mach/regs-serial.h>

#include "cpu.h"

/*W90P910 has five uarts*/

#define MAX_UART_COUNT 5
static int uart_count;
static struct platform_device *uart_devs[MAX_UART_COUNT-1];

/* Initial IO mappings */

static struct map_desc w90p910_iodesc[] __initdata = {
	IODESC_ENT(IRQ),
	IODESC_ENT(GCR),
	IODESC_ENT(UART),
	IODESC_ENT(TIMER),
	IODESC_ENT(EBI),
	/*IODESC_ENT(LCD),*/
};

/*Init the dev resource*/

static W90X900_RES(UART0);
static W90X900_RES(UART1);
static W90X900_RES(UART2);
static W90X900_RES(UART3);
static W90X900_RES(UART4);
static W90X900_DEVICE(uart0, UART0, 0, "w90x900-uart");
static W90X900_DEVICE(uart1, UART1, 1, "w90x900-uart");
static W90X900_DEVICE(uart2, UART2, 2, "w90x900-uart");
static W90X900_DEVICE(uart3, UART3, 3, "w90x900-uart");
static W90X900_DEVICE(uart4, UART4, 4, "w90x900-uart");

static struct platform_device *uart_devices[] __initdata = {
	&w90x900_uart0,
	&w90x900_uart1,
	&w90x900_uart2,
	&w90x900_uart3,
	&w90x900_uart4
};

/*Init W90P910 uart device*/

void __init w90p910_init_uarts(struct w90x900_uartcfg *cfg, int no)
{
	struct platform_device *platdev;
	int uart, uartdev;

	/*By min() to judge count of uart be used indeed*/

	uartdev = ARRAY_SIZE(uart_devices);
	no = min(uartdev, no);

	for (uart = 0; uart < no; uart++, cfg++) {
		if (cfg->hwport != uart)
			printk(KERN_ERR "w90x900_uartcfg[%d] error\n", uart);
		platdev = uart_devices[cfg->hwport];
		uart_devs[uart] = platdev;
		platdev->dev.platform_data = cfg;
	}
	uart_count = uart;
}

/*Init W90P910 evb io*/

void __init w90p910_map_io(struct map_desc *mach_desc, int mach_size)
{
	unsigned long idcode = 0x0;

	iotable_init(w90p910_iodesc, ARRAY_SIZE(w90p910_iodesc));

	idcode = __raw_readl(W90X900PDID);
	if (idcode != W90P910_CPUID)
		printk(KERN_ERR "CPU type 0x%08lx is not W90P910\n", idcode);
}

/*Init W90P910 clock*/

void __init w90p910_init_clocks(int xtal)
{
}

static int __init w90p910_init_cpu(void)
{
	return 0;
}

static int __init w90x900_arch_init(void)
{
	int ret;

	ret = w90p910_init_cpu();
	if (ret != 0)
		return ret;

	return platform_add_devices(uart_devs, uart_count);

}
arch_initcall(w90x900_arch_init);
