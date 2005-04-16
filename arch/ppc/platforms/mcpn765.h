/*
 * arch/ppc/platforms/mcpn765.h
 *
 * Definitions for Motorola MCG MCPN765 cPCI Board.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2001-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * From Processor to PCI:
 *   PCI Mem Space: 0x80000000 - 0xc0000000 -> 0x80000000 - 0xc0000000 (1 GB)
 *   PCI I/O Space: 0xfd800000 - 0xfe000000 -> 0x00000000 - 0x00800000 (8 MB)
 *	Note: Must skip 0xfe000000-0xfe400000 for CONFIG_HIGHMEM/PKMAP area
 *   MPIC in PCI Mem Space: 0xfe800000 - 0xfe830000 (not all used by MPIC)
 *
 * From PCI to Processor:
 *   System Memory: 0x00000000 -> 0x00000000
 */

#ifndef __PPC_PLATFORMS_MCPN765_H
#define __PPC_PLATFORMS_MCPN765_H
#include <linux/config.h>

/* PCI Memory space mapping info */
#define	MCPN765_PCI_MEM_SIZE		0x40000000U
#define MCPN765_PROC_PCI_MEM_START	0x80000000U
#define MCPN765_PROC_PCI_MEM_END	(MCPN765_PROC_PCI_MEM_START +	\
					 MCPN765_PCI_MEM_SIZE - 1)
#define MCPN765_PCI_MEM_START		0x80000000U
#define MCPN765_PCI_MEM_END		(MCPN765_PCI_MEM_START +	\
					 MCPN765_PCI_MEM_SIZE - 1)

/* PCI I/O space mapping info */
#define	MCPN765_PCI_IO_SIZE		0x00800000U
#define MCPN765_PROC_PCI_IO_START	0xfd800000U
#define MCPN765_PROC_PCI_IO_END		(MCPN765_PROC_PCI_IO_START +	\
					 MCPN765_PCI_IO_SIZE - 1)
#define MCPN765_PCI_IO_START		0x00000000U
#define MCPN765_PCI_IO_END		(MCPN765_PCI_IO_START + 	\
					 MCPN765_PCI_IO_SIZE - 1)

/* System memory mapping info */
#define MCPN765_PCI_DRAM_OFFSET		0x00000000U
#define MCPN765_PCI_PHY_MEM_OFFSET	0x00000000U

#define MCPN765_ISA_MEM_BASE		0x00000000U
#define MCPN765_ISA_IO_BASE		MCPN765_PROC_PCI_IO_START

/* Define base addresses for important sets of registers */
#define MCPN765_HAWK_MPIC_BASE		0xfe800000U
#define MCPN765_HAWK_SMC_BASE		0xfef80000U
#define	MCPN765_HAWK_PPC_REG_BASE	0xfeff0000U

/* Define MCPN765 board register addresses. */
#define	MCPN765_BOARD_STATUS_REG	0xfef88080U
#define	MCPN765_BOARD_MODFAIL_REG	0xfef88090U
#define	MCPN765_BOARD_MODRST_REG	0xfef880a0U
#define	MCPN765_BOARD_TBEN_REG		0xfef880c0U
#define	MCPN765_BOARD_GEOGRAPHICAL_REG	0xfef880e8U
#define	MCPN765_BOARD_EXT_FEATURE_REG	0xfef880f0U
#define	MCPN765_BOARD_LAST_RESET_REG	0xfef880f8U

/* Defines for UART */

/* Define the UART base addresses */
#define	MCPN765_SERIAL_1		0xfef88000
#define	MCPN765_SERIAL_2		0xfef88200
#define	MCPN765_SERIAL_3		0xfef88400
#define	MCPN765_SERIAL_4		0xfef88600

#ifdef CONFIG_SERIAL_MANY_PORTS
#define RS_TABLE_SIZE  64
#else
#define RS_TABLE_SIZE  4
#endif

/* Rate for the 1.8432 Mhz clock for the onboard serial chip */
#define BASE_BAUD	( 1843200 / 16 )
#define UART_CLK	1843200

#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST)
#endif

/* All UART IRQ's are wire-OR'd to IRQ 17 */
#define STD_SERIAL_PORT_DFNS \
        { 0, BASE_BAUD, MCPN765_SERIAL_1, 17, STD_COM_FLAGS, /* ttyS0 */\
		iomem_base: (u8 *)MCPN765_SERIAL_1,			\
		iomem_reg_shift: 4,					\
		io_type: SERIAL_IO_MEM },				\
        { 0, BASE_BAUD, MCPN765_SERIAL_2, 17, STD_COM_FLAGS, /* ttyS1 */\
		iomem_base: (u8 *)MCPN765_SERIAL_2,			\
		iomem_reg_shift: 4,					\
		io_type: SERIAL_IO_MEM },				\
        { 0, BASE_BAUD, MCPN765_SERIAL_3, 17, STD_COM_FLAGS, /* ttyS2 */\
		iomem_base: (u8 *)MCPN765_SERIAL_3,			\
		iomem_reg_shift: 4,					\
		io_type: SERIAL_IO_MEM },				\
        { 0, BASE_BAUD, MCPN765_SERIAL_4, 17, STD_COM_FLAGS, /* ttyS3 */\
		iomem_base: (u8 *)MCPN765_SERIAL_4,			\
		iomem_reg_shift: 4,					\
		io_type: SERIAL_IO_MEM },

#define SERIAL_PORT_DFNS \
        STD_SERIAL_PORT_DFNS

/* Define the NVRAM/RTC address strobe & data registers */
#define MCPN765_PHYS_NVRAM_AS0          0xfef880c8U
#define MCPN765_PHYS_NVRAM_AS1          0xfef880d0U
#define MCPN765_PHYS_NVRAM_DATA         0xfef880d8U

extern void mcpn765_find_bridges(void);

#endif /* __PPC_PLATFORMS_MCPN765_H */
