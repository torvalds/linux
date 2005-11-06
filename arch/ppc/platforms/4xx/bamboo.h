/*
 * arch/ppc/platforms/bamboo.h
 *
 * Bamboo board definitions
 *
 * Wade Farnsworth <wfarnsworth@mvista.com>
 *
 * Copyright 2004 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifdef __KERNEL__
#ifndef __ASM_BAMBOO_H__
#define __ASM_BAMBOO_H__

#include <linux/config.h>
#include <platforms/4xx/ibm440ep.h>

/* F/W TLB mapping used in bootloader glue to reset EMAC */
#define PPC44x_EMAC0_MR0		0x0EF600E00

/* Location of MAC addresses in PIBS image */
#define PIBS_FLASH_BASE			0xfff00000
#define PIBS_MAC_BASE			(PIBS_FLASH_BASE+0xc0400)
#define PIBS_MAC_SIZE			0x200
#define PIBS_MAC_OFFSET			0x100

/* Default clock rate */
#define BAMBOO_TMRCLK			25000000

/* RTC/NVRAM location */
#define BAMBOO_RTC_ADDR			0x080000000ULL
#define BAMBOO_RTC_SIZE			0x2000

/* FPGA Registers */
#define BAMBOO_FPGA_ADDR		0x080002000ULL

#define BAMBOO_FPGA_CONFIG2_REG_ADDR	(BAMBOO_FPGA_ADDR + 0x1)
#define BAMBOO_FULL_DUPLEX_EN(x)	(x & 0x08)
#define BAMBOO_FORCE_100Mbps(x)		(x & 0x04)
#define BAMBOO_AUTONEGOTIATE(x)		(x & 0x02)

#define BAMBOO_FPGA_SETTING_REG_ADDR	(BAMBOO_FPGA_ADDR + 0x3)
#define BAMBOO_BOOT_SMALL_FLASH(x)	(!(x & 0x80))
#define BAMBOO_LARGE_FLASH_EN(x)	(!(x & 0x40))
#define BAMBOO_BOOT_NAND_FLASH(x)	(!(x & 0x20))

#define BAMBOO_FPGA_SELECTION1_REG_ADDR (BAMBOO_FPGA_ADDR + 0x4)
#define BAMBOO_SEL_MII(x)		(x & 0x80)
#define BAMBOO_SEL_RMII(x)		(x & 0x40)
#define BAMBOO_SEL_SMII(x)		(x & 0x20)

/* Flash */
#define BAMBOO_SMALL_FLASH_LOW		0x087f00000ULL
#define BAMBOO_SMALL_FLASH_HIGH		0x0fff00000ULL
#define BAMBOO_SMALL_FLASH_SIZE		0x100000
#define BAMBOO_LARGE_FLASH_LOW		0x087800000ULL
#define BAMBOO_LARGE_FLASH_HIGH1	0x0ff800000ULL
#define BAMBOO_LARGE_FLASH_HIGH2	0x0ffc00000ULL
#define BAMBOO_LARGE_FLASH_SIZE		0x400000
#define BAMBOO_SRAM_LOW			0x087f00000ULL
#define BAMBOO_SRAM_HIGH1		0x0fff00000ULL
#define BAMBOO_SRAM_HIGH2		0x0ff800000ULL
#define BAMBOO_SRAM_SIZE		0x100000
#define BAMBOO_NAND_FLASH_REG_ADDR	0x090000000ULL
#define BAMBOO_NAND_FLASH_REG_SIZE	0x2000

/*
 * Serial port defines
 */
#define RS_TABLE_SIZE			4

#define UART0_IO_BASE			0xEF600300
#define UART1_IO_BASE			0xEF600400
#define UART2_IO_BASE			0xEF600500
#define UART3_IO_BASE			0xEF600600

#define BASE_BAUD			33177600/3/16
#define UART0_INT			0
#define UART1_INT			1
#define UART2_INT			3
#define UART3_INT			4

#define STD_UART_OP(num)					\
	{ 0, BASE_BAUD, 0, UART##num##_INT,			\
		(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST),	\
		iomem_base: (void*)UART##num##_IO_BASE,		\
		io_type: SERIAL_IO_MEM},

#define SERIAL_PORT_DFNS	\
	STD_UART_OP(0)		\
	STD_UART_OP(1)		\
	STD_UART_OP(2)		\
	STD_UART_OP(3)

/* PCI support */
#define BAMBOO_PCI_CFGA_PLB32		0xeec00000
#define BAMBOO_PCI_CFGD_PLB32		0xeec00004

#define BAMBOO_PCI_IO_BASE		0x00000000e8000000ULL
#define BAMBOO_PCI_IO_SIZE		0x00010000
#define BAMBOO_PCI_MEM_OFFSET		0x00000000
#define BAMBOO_PCI_PHY_MEM_BASE		0x00000000a0000000ULL

#define BAMBOO_PCI_LOWER_IO		0x00000000
#define BAMBOO_PCI_UPPER_IO		0x0000ffff
#define BAMBOO_PCI_LOWER_MEM		0xa0000000
#define BAMBOO_PCI_UPPER_MEM		0xafffffff
#define BAMBOO_PCI_MEM_BASE		0xa0000000

#define BAMBOO_PCIL0_BASE		0x00000000ef400000ULL
#define BAMBOO_PCIL0_SIZE		0x40

#define BAMBOO_PCIL0_PMM0LA		0x000
#define BAMBOO_PCIL0_PMM0MA		0x004
#define BAMBOO_PCIL0_PMM0PCILA		0x008
#define BAMBOO_PCIL0_PMM0PCIHA		0x00C
#define BAMBOO_PCIL0_PMM1LA		0x010
#define BAMBOO_PCIL0_PMM1MA		0x014
#define BAMBOO_PCIL0_PMM1PCILA		0x018
#define BAMBOO_PCIL0_PMM1PCIHA		0x01C
#define BAMBOO_PCIL0_PMM2LA		0x020
#define BAMBOO_PCIL0_PMM2MA		0x024
#define BAMBOO_PCIL0_PMM2PCILA		0x028
#define BAMBOO_PCIL0_PMM2PCIHA		0x02C
#define BAMBOO_PCIL0_PTM1MS		0x030
#define BAMBOO_PCIL0_PTM1LA		0x034
#define BAMBOO_PCIL0_PTM2MS		0x038
#define BAMBOO_PCIL0_PTM2LA		0x03C

#endif                          /* __ASM_BAMBOO_H__ */
#endif                          /* __KERNEL__ */
