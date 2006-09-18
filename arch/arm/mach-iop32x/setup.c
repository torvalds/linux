/*
 * linux/arch/arm/mach-iop32x/setup.c
 *
 * Author: Nicolas Pitre <nico@cam.org>
 * Copyright (C) 2001 MontaVista Software, Inc.
 * Copyright (C) 2004 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_core.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/mach/map.h>
#include <asm/setup.h>
#include <asm/system.h>
#include <asm/memory.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/hardware/iop3xx.h>

#define IOP321_UART_XTAL 1843200

#ifdef CONFIG_ARCH_IQ80321
#define UARTBASE IQ80321_UART
#define IRQ_UART IRQ_IQ80321_UART
#endif

#ifdef CONFIG_ARCH_IQ31244
#define UARTBASE IQ31244_UART
#define IRQ_UART IRQ_IQ31244_UART
#endif

static struct uart_port iop321_serial_ports[] = {
	{
		.membase	= (char*)(UARTBASE),
		.mapbase	= (UARTBASE),
		.irq		= IRQ_UART,
		.flags		= UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= IOP321_UART_XTAL,
		.line		= 0,
		.type		= PORT_16550A,
		.fifosize	= 16
	}
};

void __init iop32x_init(void)
{
	platform_device_register(&iop3xx_i2c0_device);
	platform_device_register(&iop3xx_i2c1_device);
	early_serial_setup(&iop321_serial_ports[0]);
}

#ifdef CONFIG_ARCH_IQ80321
extern void iq80321_map_io(void);
extern struct sys_timer iop321_timer;
extern void iop321_init_time(void);
#endif

#ifdef CONFIG_ARCH_IQ31244
extern void iq31244_map_io(void);
extern struct sys_timer iop321_timer;
extern void iop321_init_time(void);
#endif

#if defined(CONFIG_ARCH_IQ80321)
MACHINE_START(IQ80321, "Intel IQ80321")
	/* Maintainer: Intel Corporation */
	.phys_io	= IQ80321_UART,
	.io_pg_offst	= ((IQ80321_UART) >> 18) & 0xfffc,
	.map_io		= iq80321_map_io,
	.init_irq	= iop321_init_irq,
	.timer		= &iop321_timer,
	.boot_params	= 0xa0000100,
	.init_machine	= iop32x_init,
MACHINE_END
#elif defined(CONFIG_ARCH_IQ31244)
MACHINE_START(IQ31244, "Intel IQ31244")
	/* Maintainer: Intel Corp. */
	.phys_io	= IQ31244_UART,
	.io_pg_offst	= ((IQ31244_UART) >> 18) & 0xfffc,
	.map_io		= iq31244_map_io,
	.init_irq	= iop321_init_irq,
	.timer		= &iop321_timer,
	.boot_params	= 0xa0000100,
	.init_machine	= iop32x_init,
MACHINE_END
#else
#error No machine descriptor defined for this IOP3XX implementation
#endif
