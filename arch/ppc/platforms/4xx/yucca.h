/*
 * Yucca board definitions
 *
 * Roland Dreier <rolandd@cisco.com> (based on luan.h by Matt Porter)
 *
 * Copyright 2004-2005 MontaVista Software Inc.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifdef __KERNEL__
#ifndef __ASM_YUCCA_H__
#define __ASM_YUCCA_H__

#include <linux/config.h>
#include <platforms/4xx/ppc440spe.h>

/* F/W TLB mapping used in bootloader glue to reset EMAC */
#define PPC44x_EMAC0_MR0	0xa0000800

/* Location of MAC addresses in PIBS image */
#define PIBS_FLASH_BASE		0xffe00000
#define PIBS_MAC_BASE		(PIBS_FLASH_BASE+0x1b0400)

/* External timer clock frequency */
#define YUCCA_TMR_CLK		25000000

/*
 * FPGA registers
 */
#define YUCCA_FPGA_REG_BASE			0x00000004e2000000ULL
#define YUCCA_FPGA_REG_SIZE			0x24

#define FPGA_REG1A				0x1a

#define FPGA_REG1A_PE0_GLED			0x8000
#define FPGA_REG1A_PE1_GLED			0x4000
#define FPGA_REG1A_PE2_GLED			0x2000
#define FPGA_REG1A_PE0_YLED			0x1000
#define FPGA_REG1A_PE1_YLED			0x0800
#define FPGA_REG1A_PE2_YLED			0x0400
#define FPGA_REG1A_PE0_PWRON			0x0200
#define FPGA_REG1A_PE1_PWRON			0x0100
#define FPGA_REG1A_PE2_PWRON			0x0080
#define FPGA_REG1A_PE0_REFCLK_ENABLE		0x0040
#define FPGA_REG1A_PE1_REFCLK_ENABLE		0x0020
#define FPGA_REG1A_PE2_REFCLK_ENABLE		0x0010
#define FPGA_REG1A_PE_SPREAD0			0x0008
#define FPGA_REG1A_PE_SPREAD1			0x0004
#define FPGA_REG1A_PE_SELSOURCE_0		0x0002
#define FPGA_REG1A_PE_SELSOURCE_1		0x0001

#define FPGA_REG1C				0x1c

#define FPGA_REG1C_PE0_ROOTPOINT		0x8000
#define FPGA_REG1C_PE1_ENDPOINT			0x4000
#define FPGA_REG1C_PE2_ENDPOINT			0x2000
#define FPGA_REG1C_PE0_PRSNT			0x1000
#define FPGA_REG1C_PE1_PRSNT			0x0800
#define FPGA_REG1C_PE2_PRSNT			0x0400
#define FPGA_REG1C_PE0_WAKE			0x0080
#define FPGA_REG1C_PE1_WAKE			0x0040
#define FPGA_REG1C_PE2_WAKE			0x0020
#define FPGA_REG1C_PE0_PERST			0x0010
#define FPGA_REG1C_PE1_PERST			0x0008
#define FPGA_REG1C_PE2_PERST			0x0004

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
#define YUCCA_PCIX_LOWER_IO	0x00000000
#define YUCCA_PCIX_UPPER_IO	0x0000ffff
#define YUCCA_PCIX_LOWER_MEM	0x80000000
#define YUCCA_PCIX_UPPER_MEM	0x8fffffff
#define YUCCA_PCIE_LOWER_MEM	0x90000000
#define YUCCA_PCIE_MEM_SIZE	0x10000000

#define YUCCA_PCIX_MEM_SIZE	0x10000000
#define YUCCA_PCIX_MEM_OFFSET	0x00000000
#define YUCCA_PCIE_MEM_SIZE	0x10000000
#define YUCCA_PCIE_MEM_OFFSET	0x00000000

#endif				/* __ASM_YUCCA_H__ */
#endif				/* __KERNEL__ */
