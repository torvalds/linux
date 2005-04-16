/*
 * arch/ppc/platforms/k2.h
 *
 * Definitions for SBS K2 board support
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __PPC_PLATFORMS_K2_H
#define __PPC_PLATFORMS_K2_H

/*
 * SBS K2 definitions
 */

#define	K2_PCI64_BAR		0xff400000
#define	K2_PCI32_BAR		0xff500000

#define K2_PCI64_CONFIG_ADDR	(K2_PCI64_BAR + 0x000f8000)
#define K2_PCI64_CONFIG_DATA	(K2_PCI64_BAR + 0x000f8010)

#define K2_PCI32_CONFIG_ADDR	(K2_PCI32_BAR + 0x000f8000)
#define K2_PCI32_CONFIG_DATA	(K2_PCI32_BAR + 0x000f8010)

#define K2_PCI64_MEM_BASE	0xd0000000
#define K2_PCI64_IO_BASE	0x80100000

#define K2_PCI32_MEM_BASE	0xc0000000
#define K2_PCI32_IO_BASE	0x80000000

#define K2_PCI32_SYS_MEM_BASE	0x80000000
#define K2_PCI64_SYS_MEM_BASE	K2_PCI32_SYS_MEM_BASE

#define K2_PCI32_LOWER_MEM	0x00000000
#define K2_PCI32_UPPER_MEM	0x0fffffff
#define K2_PCI32_LOWER_IO	0x00000000
#define K2_PCI32_UPPER_IO	0x000fffff

#define K2_PCI64_LOWER_MEM	0x10000000
#define K2_PCI64_UPPER_MEM	0x1fffffff
#define K2_PCI64_LOWER_IO	0x00100000
#define	K2_PCI64_UPPER_IO	0x001fffff

#define K2_ISA_IO_BASE		K2_PCI32_IO_BASE
#define K2_ISA_MEM_BASE		K2_PCI32_MEM_BASE

#define K2_BOARD_ID_REG		(K2_ISA_IO_BASE + 0x800)
#define K2_MISC_REG		(K2_ISA_IO_BASE + 0x804)
#define K2_MSIZ_GEO_REG		(K2_ISA_IO_BASE + 0x808)
#define K2_HOT_SWAP_REG		(K2_ISA_IO_BASE + 0x80c)
#define K2_PLD2_REG		(K2_ISA_IO_BASE + 0x80e)
#define K2_PLD3_REG		(K2_ISA_IO_BASE + 0x80f)

#define K2_BUS_SPD(board_id)	(board_id >> 2) & 3

#define K2_RTC_BASE_OFFSET	0x90000
#define K2_RTC_BASE_ADDRESS	(K2_PCI32_MEM_BASE + K2_RTC_BASE_OFFSET)
#define K2_RTC_SIZE		0x8000

#define K2_MEM_SIZE_MASK	0xe0
#define K2_MEM_SIZE(size_reg)	(size_reg & K2_MEM_SIZE_MASK) >> 5
#define	K2_MEM_SIZE_1GB		0x40000000
#define K2_MEM_SIZE_512MB	0x20000000
#define K2_MEM_SIZE_256MB	0x10000000
#define K2_MEM_SIZE_128MB	0x08000000

#define K2_L2CACHE_MASK		0x03	/* Mask for 2 L2 Cache bits */
#define K2_L2CACHE_512KB	0x00	/* 512KB */
#define K2_L2CACHE_256KB	0x01	/* 256KB */
#define K2_L2CACHE_1MB		0x02	/* 1MB */
#define K2_L2CACHE_NONE		0x03	/* None */

#define K2_GEO_ADR_MASK		0x1f

#define K2_SYS_SLOT_MASK	0x08

#endif /* __PPC_PLATFORMS_K2_H */
