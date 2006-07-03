/*
 * Ocotea board definitions
 *
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright 2003-2005 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifdef __KERNEL__
#ifndef __ASM_OCOTEA_H__
#define __ASM_OCOTEA_H__

#include <platforms/4xx/ibm440gx.h>

/* F/W TLB mapping used in bootloader glue to reset EMAC */
#define PPC44x_EMAC0_MR0	0xe0000800

/* Location of MAC addresses in PIBS image */
#define PIBS_FLASH_BASE		0xfff00000
#define PIBS_MAC_BASE		(PIBS_FLASH_BASE+0xb0500)
#define PIBS_MAC_SIZE		0x200
#define PIBS_MAC_OFFSET		0x100

/* External timer clock frequency */
#define OCOTEA_TMR_CLK	25000000

/* RTC/NVRAM location */
#define OCOTEA_RTC_ADDR		0x0000000148000000ULL
#define OCOTEA_RTC_SIZE		0x2000

/* Flash */
#define OCOTEA_FPGA_REG_0		0x0000000148300000ULL
#define OCOTEA_BOOT_LARGE_FLASH(x)	(x & 0x40)
#define OCOTEA_SMALL_FLASH_LOW		0x00000001ff900000ULL
#define OCOTEA_SMALL_FLASH_HIGH		0x00000001fff00000ULL
#define OCOTEA_SMALL_FLASH_SIZE		0x100000
#define OCOTEA_LARGE_FLASH_LOW		0x00000001ff800000ULL
#define OCOTEA_LARGE_FLASH_HIGH		0x00000001ffc00000ULL
#define OCOTEA_LARGE_FLASH_SIZE		0x400000

/* FPGA_REG_3 (Ethernet Groups) */
#define OCOTEA_FPGA_REG_3		0x0000000148300003ULL

/*
 * Serial port defines
 */
#define RS_TABLE_SIZE	2

#if defined(__BOOTER__)
/* OpenBIOS defined UART mappings, used by bootloader shim */
#define UART0_IO_BASE	0xE0000200
#define UART1_IO_BASE	0xE0000300
#else
/* head_44x.S created UART mapping, used before early_serial_setup.
 * We cannot use default OpenBIOS UART mappings because they
 * don't work for configurations with more than 512M RAM.    --ebs
 */
#define UART0_IO_BASE	0xF0000200
#define UART1_IO_BASE	0xF0000300
#endif

#define BASE_BAUD	11059200/16
#define STD_UART_OP(num)					\
	{ 0, BASE_BAUD, 0, UART##num##_INT,			\
		(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST),	\
		iomem_base: (void*)UART##num##_IO_BASE,		\
		io_type: SERIAL_IO_MEM},

#define SERIAL_PORT_DFNS	\
	STD_UART_OP(0)		\
	STD_UART_OP(1)

/* PCI support */
#define OCOTEA_PCI_LOWER_IO	0x00000000
#define OCOTEA_PCI_UPPER_IO	0x0000ffff
#define OCOTEA_PCI_LOWER_MEM	0x80000000
#define OCOTEA_PCI_UPPER_MEM	0xffffefff

#define OCOTEA_PCI_CFGREGS_BASE	0x000000020ec00000ULL
#define OCOTEA_PCI_CFGA_PLB32	0x0ec00000
#define OCOTEA_PCI_CFGD_PLB32	0x0ec00004

#define OCOTEA_PCI_IO_BASE	0x0000000208000000ULL
#define OCOTEA_PCI_IO_SIZE	0x00010000
#define OCOTEA_PCI_MEM_OFFSET	0x00000000

#endif				/* __ASM_OCOTEA_H__ */
#endif				/* __KERNEL__ */
