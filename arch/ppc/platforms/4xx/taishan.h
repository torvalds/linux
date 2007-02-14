/*
 * arch/ppc/platforms/4xx/taishan.h
 *
 * AMCC Taishan board definitions
 *
 * Copyright 2007 DENX Software Engineering, Stefan Roese <sr@denx.de>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifdef __KERNEL__
#ifndef __ASM_TAISHAN_H__
#define __ASM_TAISHAN_H__

#include <platforms/4xx/ibm440gx.h>

/* External timer clock frequency */
#define TAISHAN_TMR_CLK	25000000

/* Flash */
#define TAISHAN_FPGA_ADDR		0x0000000141000000ULL
#define TAISHAN_LCM_ADDR		0x0000000142000000ULL
#define TAISHAN_FLASH_ADDR		0x00000001fc000000ULL
#define TAISHAN_FLASH_SIZE		0x4000000

/*
 * Serial port defines
 */
#define RS_TABLE_SIZE	2

/* head_44x.S created UART mapping, used before early_serial_setup.
 * We cannot use default OpenBIOS UART mappings because they
 * don't work for configurations with more than 512M RAM.    --ebs
 */
#define UART0_IO_BASE	0xF0000200
#define UART1_IO_BASE	0xF0000300

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
#define TAISHAN_PCI_LOWER_IO	0x00000000
#define TAISHAN_PCI_UPPER_IO	0x0000ffff
#define TAISHAN_PCI_LOWER_MEM	0x80000000
#define TAISHAN_PCI_UPPER_MEM	0xffffefff

#define TAISHAN_PCI_CFGA_PLB32	0x0ec00000
#define TAISHAN_PCI_CFGD_PLB32	0x0ec00004

#define TAISHAN_PCI_IO_BASE	0x0000000208000000ULL
#define TAISHAN_PCI_IO_SIZE	0x00010000
#define TAISHAN_PCI_MEM_OFFSET	0x00000000

#endif				/* __ASM_TAISHAN_H__ */
#endif				/* __KERNEL__ */
