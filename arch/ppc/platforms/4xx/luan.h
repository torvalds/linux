/*
 * Luan board definitions
 *
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright 2004-2005 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifdef __KERNEL__
#ifndef __ASM_LUAN_H__
#define __ASM_LUAN_H__

#include <platforms/4xx/ibm440sp.h>

/* F/W TLB mapping used in bootloader glue to reset EMAC */
#define PPC44x_EMAC0_MR0	0xa0000800

/* Location of MAC addresses in PIBS image */
#define PIBS_FLASH_BASE		0xffe00000
#define PIBS_MAC_BASE		(PIBS_FLASH_BASE+0x1b0400)

/* External timer clock frequency */
#define LUAN_TMR_CLK		25000000

/* Flash */
#define LUAN_FPGA_REG_0			0x0000000148300000ULL
#define LUAN_BOOT_LARGE_FLASH(x)	(x & 0x40)
#define LUAN_SMALL_FLASH_LOW		0x00000001ff900000ULL
#define LUAN_SMALL_FLASH_HIGH		0x00000001ffe00000ULL
#define LUAN_SMALL_FLASH_SIZE		0x100000
#define LUAN_LARGE_FLASH_LOW		0x00000001ff800000ULL
#define LUAN_LARGE_FLASH_HIGH		0x00000001ffc00000ULL
#define LUAN_LARGE_FLASH_SIZE		0x400000

/*
 * Serial port defines
 */
#define RS_TABLE_SIZE	3

/* PIBS defined UART mappings, used before early_serial_setup */
#define UART0_IO_BASE	0xa0000200
#define UART1_IO_BASE	0xa0000300
#define UART2_IO_BASE	0xa0000600

#define BASE_BAUD	11059200
#define STD_UART_OP(num)					\
	{ 0, BASE_BAUD, 0, UART##num##_INT,			\
		(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST),	\
		iomem_base: (void*)UART##num##_IO_BASE,		\
		io_type: SERIAL_IO_MEM},

#define SERIAL_PORT_DFNS	\
	STD_UART_OP(0)		\
	STD_UART_OP(1)		\
	STD_UART_OP(2)

/* PCI support */
#define LUAN_PCIX_LOWER_IO	0x00000000
#define LUAN_PCIX_UPPER_IO	0x0000ffff
#define LUAN_PCIX0_LOWER_MEM	0x80000000
#define LUAN_PCIX0_UPPER_MEM	0x9fffffff
#define LUAN_PCIX1_LOWER_MEM	0xa0000000
#define LUAN_PCIX1_UPPER_MEM	0xbfffffff
#define LUAN_PCIX2_LOWER_MEM	0xc0000000
#define LUAN_PCIX2_UPPER_MEM	0xdfffffff

#define LUAN_PCIX_MEM_SIZE	0x20000000
#define LUAN_PCIX_MEM_OFFSET	0x00000000

#endif				/* __ASM_LUAN_H__ */
#endif				/* __KERNEL__ */
