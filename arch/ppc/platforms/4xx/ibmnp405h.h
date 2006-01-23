/*
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_IBMNP405H_H__
#define __ASM_IBMNP405H_H__

#include <linux/config.h>

/* ibm405.h at bottom of this file */

#define PPC405_PCI_CONFIG_ADDR	0xeec00000
#define PPC405_PCI_CONFIG_DATA	0xeec00004
#define PPC405_PCI_PHY_MEM_BASE	0x80000000	/* hose_a->pci_mem_offset */
						/* setbat */
#define PPC405_PCI_MEM_BASE	PPC405_PCI_PHY_MEM_BASE	/* setbat */
#define PPC405_PCI_PHY_IO_BASE	0xe8000000	/* setbat */
#define PPC405_PCI_IO_BASE	PPC405_PCI_PHY_IO_BASE	/* setbat */

#define PPC405_PCI_LOWER_MEM	0x00000000	/* hose_a->mem_space.start */
#define PPC405_PCI_UPPER_MEM	0xBfffffff	/* hose_a->mem_space.end */
#define PPC405_PCI_LOWER_IO	0x00000000	/* hose_a->io_space.start */
#define PPC405_PCI_UPPER_IO	0x0000ffff	/* hose_a->io_space.end */

#define PPC405_ISA_IO_BASE	PPC405_PCI_IO_BASE

#define PPC4xx_PCI_IO_ADDR	((uint)PPC405_PCI_PHY_IO_BASE)
#define PPC4xx_PCI_IO_SIZE	((uint)64*1024)
#define PPC4xx_PCI_CFG_ADDR	((uint)PPC405_PCI_CONFIG_ADDR)
#define PPC4xx_PCI_CFG_SIZE	((uint)4*1024)
#define PPC4xx_PCI_LCFG_ADDR	((uint)0xef400000)
#define PPC4xx_PCI_LCFG_SIZE	((uint)4*1024)
#define PPC4xx_ONB_IO_ADDR	((uint)0xef600000)
#define PPC4xx_ONB_IO_SIZE	((uint)4*1024)

/* serial port defines */
#define RS_TABLE_SIZE	4

#define UART0_INT	0
#define UART1_INT	1
#define PCIL0_BASE	0xEF400000
#define UART0_IO_BASE	0xEF600300
#define UART1_IO_BASE	0xEF600400
#define OPB0_BASE	0xEF600600
#define EMAC0_BASE	0xEF600800

#define BD_EMAC_ADDR(e,i) bi_enetaddr[e][i]

#define STD_UART_OP(num)					\
	{ 0, BASE_BAUD, 0, UART##num##_INT,			\
		(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST),	\
		iomem_base:(u8 *) UART##num##_IO_BASE,		\
		io_type: SERIAL_IO_MEM},

#if defined(CONFIG_UART0_TTYS0)
#define SERIAL_DEBUG_IO_BASE	UART0_IO_BASE
#define SERIAL_PORT_DFNS        \
        STD_UART_OP(0)          \
        STD_UART_OP(1)
#endif

#if defined(CONFIG_UART0_TTYS1)
#define SERIAL_DEBUG_IO_BASE	UART0_IO_BASE
#define SERIAL_PORT_DFNS        \
        STD_UART_OP(1)          \
        STD_UART_OP(0)
#endif

/* DCR defines */
/* ------------------------------------------------------------------------- */

#define DCRN_CHCR_BASE	0x0F1
#define DCRN_CHPSR_BASE	0x0B4
#define DCRN_CPMSR_BASE	0x0BA
#define DCRN_CPMFR_BASE	0x0B9
#define DCRN_CPMER_BASE	0x0B8

/* CPM Clocking & Power Mangement defines */
#define IBM_CPM_PCI		0x40000000	/* PCI */
#define IBM_CPM_EMAC2	0x20000000	/* EMAC 2 MII */
#define IBM_CPM_EMAC3	0x04000000	/* EMAC 3 MII */
#define IBM_CPM_EMAC0	0x00800000	/* EMAC 0 MII */
#define IBM_CPM_EMAC1	0x00100000	/* EMAC 1 MII */
#define IBM_CPM_EMMII	0	/* Shift value for MII */
#define IBM_CPM_EMRX	1	/* Shift value for recv */
#define IBM_CPM_EMTX	2	/* Shift value for MAC */
#define IBM_CPM_UIC1	0x00020000	/* Universal Interrupt Controller */
#define IBM_CPM_UIC0	0x00010000	/* Universal Interrupt Controller */
#define IBM_CPM_CPU	0x00008000	/* processor core */
#define IBM_CPM_EBC	0x00004000	/* ROM/SRAM peripheral controller */
#define IBM_CPM_SDRAM0	0x00002000	/* SDRAM memory controller */
#define IBM_CPM_GPIO0	0x00001000	/* General Purpose IO (??) */
#define IBM_CPM_HDLC	0x00000800	/* HDCL */
#define IBM_CPM_TMRCLK	0x00000400	/* CPU timers */
#define IBM_CPM_PLB	0x00000100	/* PLB bus arbiter */
#define IBM_CPM_OPB	0x00000080	/* PLB to OPB bridge */
#define IBM_CPM_DMA	0x00000040	/* DMA controller */
#define IBM_CPM_IIC0	0x00000010	/* IIC interface */
#define IBM_CPM_UART0	0x00000002	/* serial port 0 */
#define IBM_CPM_UART1	0x00000001	/* serial port 1 */
/* this is the default setting for devices put to sleep when booting */

#define DFLT_IBM4xx_PM	~(IBM_CPM_UIC0 | IBM_CPM_UIC1 | IBM_CPM_CPU 	\
			| IBM_CPM_EBC | IBM_CPM_SDRAM0 | IBM_CPM_PLB 	\
			| IBM_CPM_OPB | IBM_CPM_TMRCLK | IBM_CPM_DMA	\
			| IBM_CPM_EMAC0 | IBM_CPM_EMAC1 | IBM_CPM_EMAC2	\
			| IBM_CPM_EMAC3 | IBM_CPM_PCI)

#define DCRN_DMA0_BASE	0x100
#define DCRN_DMA1_BASE	0x108
#define DCRN_DMA2_BASE	0x110
#define DCRN_DMA3_BASE	0x118
#define DCRNCAP_DMA_SG	1	/* have DMA scatter/gather capability */
#define DCRN_DMASR_BASE	0x120
#define DCRN_EBC_BASE	0x012
#define DCRN_DCP0_BASE	0x014
#define DCRN_MAL_BASE	0x180
#define DCRN_OCM0_BASE	0x018
#define DCRN_PLB0_BASE	0x084
#define DCRN_PLLMR_BASE	0x0B0
#define DCRN_POB0_BASE	0x0A0
#define DCRN_SDRAM0_BASE 0x010
#define DCRN_UIC0_BASE	0x0C0
#define DCRN_UIC1_BASE	0x0D0
#define DCRN_CPC0_EPRCSR 0x0F3

#define UIC0_UIC1NC	0x00000002

#define CHR1_CETE	0x00000004	/* CPU external timer enable */
#define UIC0	DCRN_UIC0_BASE
#define UIC1	DCRN_UIC1_BASE

#undef NR_UICS
#define NR_UICS	2

/* EMAC DCRN's FIXME: armin */
#define DCRN_MALRXCTP2R(base)	((base) + 0x42)	/* Channel Rx 2 Channel Table Pointer */
#define DCRN_MALRXCTP3R(base)	((base) + 0x43)	/* Channel Rx 3 Channel Table Pointer */
#define DCRN_MALTXCTP4R(base)	((base) + 0x24)	/* Channel Tx 4 Channel Table Pointer */
#define DCRN_MALTXCTP5R(base)	((base) + 0x25)	/* Channel Tx 5 Channel Table Pointer */
#define DCRN_MALTXCTP6R(base)	((base) + 0x26)	/* Channel Tx 6 Channel Table Pointer */
#define DCRN_MALTXCTP7R(base)	((base) + 0x27)	/* Channel Tx 7 Channel Table Pointer */
#define DCRN_MALRCBS2(base)	((base) + 0x62)	/* Channel Rx 2 Channel Buffer Size */
#define DCRN_MALRCBS3(base)	((base) + 0x63)	/* Channel Rx 3 Channel Buffer Size */

#include <asm/ibm405.h>

#endif				/* __ASM_IBMNP405H_H__ */
#endif				/* __KERNEL__ */
