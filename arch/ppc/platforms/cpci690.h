/*
 * arch/ppc/platforms/cpci690.h
 *
 * Definitions for Force CPCI690
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2003 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * The GT64260 has 2 PCI buses each with 1 window from the CPU bus to
 * PCI I/O space and 4 windows from the CPU bus to PCI MEM space.
 */

#ifndef __PPC_PLATFORMS_CPCI690_H
#define __PPC_PLATFORMS_CPCI690_H

/*
 * Define bd_t to pass in the MAC addresses used by the GT64260's enet ctlrs.
 */
#define	CPCI690_BI_MAGIC		0xFE8765DC

typedef struct board_info {
	u32	bi_magic;
	u8	bi_enetaddr[3][6];
} bd_t;

/* PCI bus Resource setup */
#define CPCI690_PCI0_MEM_START_PROC_ADDR	0x80000000
#define CPCI690_PCI0_MEM_START_PCI_HI_ADDR	0x00000000
#define CPCI690_PCI0_MEM_START_PCI_LO_ADDR	0x80000000
#define CPCI690_PCI0_MEM_SIZE			0x10000000
#define CPCI690_PCI0_IO_START_PROC_ADDR		0xa0000000
#define CPCI690_PCI0_IO_START_PCI_ADDR		0x00000000
#define CPCI690_PCI0_IO_SIZE			0x01000000

#define CPCI690_PCI1_MEM_START_PROC_ADDR	0x90000000
#define CPCI690_PCI1_MEM_START_PCI_HI_ADDR	0x00000000
#define CPCI690_PCI1_MEM_START_PCI_LO_ADDR	0x90000000
#define CPCI690_PCI1_MEM_SIZE			0x10000000
#define CPCI690_PCI1_IO_START_PROC_ADDR		0xa1000000
#define CPCI690_PCI1_IO_START_PCI_ADDR		0x01000000
#define CPCI690_PCI1_IO_SIZE			0x01000000

/* Board Registers */
#define	CPCI690_BR_BASE				0xf0000000
#define	CPCI690_BR_SIZE_ACTUAL			0x8
#define	CPCI690_BR_SIZE			max(GT64260_WINDOW_SIZE_MIN,	\
						CPCI690_BR_SIZE_ACTUAL)
#define	CPCI690_BR_LED_CNTL			0x00
#define	CPCI690_BR_SW_RESET			0x01
#define	CPCI690_BR_MISC_STATUS			0x02
#define	CPCI690_BR_SWITCH_STATUS		0x03
#define	CPCI690_BR_MEM_CTLR			0x04
#define	CPCI690_BR_LAST_RESET_1			0x05
#define	CPCI690_BR_LAST_RESET_2			0x06

#define	CPCI690_TODC_BASE			0xf0100000
#define	CPCI690_TODC_SIZE_ACTUAL		0x8000 /* Size or NVRAM + RTC */
#define	CPCI690_TODC_SIZE		max(GT64260_WINDOW_SIZE_MIN,	\
						CPCI690_TODC_SIZE_ACTUAL)
#define	CPCI690_MAC_OFFSET			0x7c10 /* MAC in RTC NVRAM */

#define	CPCI690_IPMI_BASE			0xf0200000
#define	CPCI690_IPMI_SIZE_ACTUAL		0x10 /* 16 bytes of IPMI */
#define	CPCI690_IPMI_SIZE		max(GT64260_WINDOW_SIZE_MIN,	\
						CPCI690_IPMI_SIZE_ACTUAL)

#define	CPCI690_MPSC_BAUD			9600
#define	CPCI690_MPSC_CLK_SRC			8 /* TCLK */

#endif /* __PPC_PLATFORMS_CPCI690_H */
