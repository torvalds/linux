/*
 * Virtex-II Pro & Virtex-4 FX common infrastructure
 *
 * Maintainer: Grant Likely <grant.likely@secretlab.ca>
 *
 * Copyright 2005 Secret Lab Technologies Ltd.
 * Copyright 2005 General Dynamics Canada Ltd.
 * Copyright 2005 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/serial_8250.h>
#include <asm/ppc_sys.h>
#include <platforms/4xx/virtex.h>
#include <platforms/4xx/xparameters/xparameters.h>

#define XPAR_UART(num) { \
		.mapbase  = XPAR_UARTNS550_##num##_BASEADDR + 3, \
		.irq	  = XPAR_INTC_0_UARTNS550_##num##_VEC_ID, \
		.iotype	  = UPIO_MEM, \
		.uartclk  = XPAR_UARTNS550_##num##_CLOCK_FREQ_HZ, \
		.flags	  = UPF_BOOT_AUTOCONF, \
		.regshift = 2, \
	}

struct plat_serial8250_port serial_platform_data[] = {
#ifdef XPAR_UARTNS550_0_BASEADDR
	XPAR_UART(0),
#endif
#ifdef XPAR_UARTNS550_1_BASEADDR
	XPAR_UART(1),
#endif
#ifdef XPAR_UARTNS550_2_BASEADDR
	XPAR_UART(2),
#endif
#ifdef XPAR_UARTNS550_3_BASEADDR
	XPAR_UART(3),
#endif
	{ }, /* terminated by empty record */
};

struct platform_device ppc_sys_platform_devices[] = {
	[VIRTEX_UART] = {
		.name		= "serial8250",
		.id		= 0,
		.dev.platform_data = serial_platform_data,
	},
};

