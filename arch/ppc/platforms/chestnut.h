/*
 * arch/ppc/platforms/chestnut.h
 *
 * Definitions for IBM 750FXGX Eval (Chestnut)
 *
 * Author: <source@mvista.com>
 *
 * Based on Artesyn Katana code done by Tim Montgomery <timm@artesyncp.com>
 * Based on code done by Rabeeh Khoury - rabeeh@galileo.co.il
 * Based on code done by Mark A. Greer <mgreer@mvista.com>
 *
 * <2004> (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * This is the CPU physical memory map (windows must be at least 1MB and start
 * on a boundary that is a multiple of the window size):
 *
 * Seems on the IBM 750FXGX Eval board, the MV64460 Registers can be in
 * only 2 places per switch U17 0x14000000 or 0xf1000000 easily - chose to
 * implement at 0xf1000000 only at this time
 *
 *    0xfff00000-0xffffffff      - 8 Flash
 *    0xffe00000-0xffefffff      - BOOT SRAM
 *    0xffd00000-0xffd00004      - CPLD
 *    0xffc00000-0xffc0000f      - UART
 *    0xffb00000-0xffb07fff      - FRAM
 *    0xff840000-0xffafffff      - *** HOLE ***
 *    0xff800000-0xff83ffff      - MV64460 Integrated SRAM
 *    0xfe000000-0xff8fffff      - *** HOLE ***
 *    0xfc000000-0xfdffffff      - 32bit Flash
 *    0xf1010000-0xfbffffff      - *** HOLE ***
 *    0xf1000000-0xf100ffff      - MV64460 Registers
 */

#ifndef __PPC_PLATFORMS_CHESTNUT_H__
#define __PPC_PLATFORMS_CHESTNUT_H__

#define CHESTNUT_BOOT_8BIT_BASE			0xfff00000
#define CHESTNUT_BOOT_8BIT_SIZE_ACTUAL		(1024*1024)
#define CHESTNUT_BOOT_SRAM_BASE			0xffe00000
#define CHESTNUT_BOOT_SRAM_SIZE_ACTUAL		(1024*1024)
#define CHESTNUT_CPLD_BASE			0xffd00000
#define CHESTNUT_CPLD_SIZE_ACTUAL		5
#define CHESTNUT_CPLD_REG3			(CHESTNUT_CPLD_BASE+3)
#define CHESTNUT_UART_BASE			0xffc00000
#define CHESTNUT_UART_SIZE_ACTUAL		16
#define CHESTNUT_FRAM_BASE			0xffb00000
#define CHESTNUT_FRAM_SIZE_ACTUAL		(32*1024)
#define CHESTNUT_INTERNAL_SRAM_BASE		0xff800000
#define CHESTNUT_32BIT_BASE			0xfc000000
#define CHESTNUT_32BIT_SIZE			(32*1024*1024)

#define CHESTNUT_BOOT_8BIT_SIZE		max(MV64360_WINDOW_SIZE_MIN, \
					CHESTNUT_BOOT_8BIT_SIZE_ACTUAL)
#define CHESTNUT_BOOT_SRAM_SIZE		max(MV64360_WINDOW_SIZE_MIN, \
					CHESTNUT_BOOT_SRAM_SIZE_ACTUAL)
#define CHESTNUT_CPLD_SIZE		max(MV64360_WINDOW_SIZE_MIN, \
					CHESTNUT_CPLD_SIZE_ACTUAL)
#define CHESTNUT_UART_SIZE		max(MV64360_WINDOW_SIZE_MIN, \
					CHESTNUT_UART_SIZE_ACTUAL)
#define CHESTNUT_FRAM_SIZE		max(MV64360_WINDOW_SIZE_MIN, \
					CHESTNUT_FRAM_SIZE_ACTUAL)

#define CHESTNUT_BUS_SPEED		200000000
#define CHESTNUT_PIBS_DATABASE		0xf0000 /* from PIBS src code */

#define	KATANA_ETH0_PHY_ADDR			12
#define	KATANA_ETH1_PHY_ADDR			11
#define	KATANA_ETH2_PHY_ADDR			4

#define CHESTNUT_ETH_TX_QUEUE_SIZE		800
#define CHESTNUT_ETH_RX_QUEUE_SIZE		400

/*
 * PCI windows
 */

#define CHESTNUT_PCI0_MEM_PROC_ADDR	0x80000000
#define CHESTNUT_PCI0_MEM_PCI_HI_ADDR	0x00000000
#define CHESTNUT_PCI0_MEM_PCI_LO_ADDR	0x80000000
#define CHESTNUT_PCI0_MEM_SIZE		0x10000000
#define CHESTNUT_PCI0_IO_PROC_ADDR	0xa0000000
#define CHESTNUT_PCI0_IO_PCI_ADDR	0x00000000
#define CHESTNUT_PCI0_IO_SIZE		0x01000000

/*
 * Board-specific IRQ info
 */
#define CHESTNUT_PCI_SLOT0_IRQ	(64 + 31)
#define CHESTNUT_PCI_SLOT1_IRQ	(64 + 30)
#define CHESTNUT_PCI_SLOT2_IRQ	(64 + 29)
#define CHESTNUT_PCI_SLOT3_IRQ	(64 + 28)

/* serial port definitions */
#define CHESTNUT_UART0_IO_BASE  (CHESTNUT_UART_BASE + 8)
#define CHESTNUT_UART1_IO_BASE  CHESTNUT_UART_BASE

#define UART0_INT           	(64 + 25)
#define UART1_INT        	(64 + 26)

#ifdef CONFIG_SERIAL_MANY_PORTS
#define RS_TABLE_SIZE  64
#else
#define RS_TABLE_SIZE  2
#endif

/* Rate for the 3.6864 Mhz clock for the onboard serial chip */
#define BASE_BAUD 		(3686400 / 16)

#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST)
#endif

#define STD_UART_OP(num)						\
        { 0, BASE_BAUD, 0, UART##num##_INT, STD_COM_FLAGS,		\
                iomem_base: (u8 *)CHESTNUT_UART##num##_IO_BASE,	\
		io_type: SERIAL_IO_MEM},

#define SERIAL_PORT_DFNS        \
        STD_UART_OP(0)          \
        STD_UART_OP(1)

#endif /* __PPC_PLATFORMS_CHESTNUT_H__ */
