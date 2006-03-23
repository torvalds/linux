/*
 * IBM PPC 405EP processor defines.
 *
 * Author: SAW (IBM), derived from ibm405gp.h.
 *         Maintained by MontaVista Software <source@mvista.com>
 *
 * 2003 (c) MontaVista Softare Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_IBM405EP_H__
#define __ASM_IBM405EP_H__

#include <linux/config.h>

/* ibm405.h at bottom of this file */

/* PCI
 * PCI Bridge config reg definitions
 * see 17-19 of manual
 */

#define PPC405_PCI_CONFIG_ADDR	0xeec00000
#define PPC405_PCI_CONFIG_DATA	0xeec00004

#define PPC405_PCI_PHY_MEM_BASE	0x80000000	/* hose_a->pci_mem_offset */
						/* setbat */
#define PPC405_PCI_MEM_BASE	PPC405_PCI_PHY_MEM_BASE	/* setbat */
#define PPC405_PCI_PHY_IO_BASE	0xe8000000	/* setbat */
#define PPC405_PCI_IO_BASE	PPC405_PCI_PHY_IO_BASE	/* setbat */

#define PPC405_PCI_LOWER_MEM	0x80000000	/* hose_a->mem_space.start */
#define PPC405_PCI_UPPER_MEM	0xBfffffff	/* hose_a->mem_space.end */
#define PPC405_PCI_LOWER_IO	0x00000000	/* hose_a->io_space.start */
#define PPC405_PCI_UPPER_IO	0x0000ffff	/* hose_a->io_space.end */

#define PPC405_ISA_IO_BASE	PPC405_PCI_IO_BASE

#define PPC4xx_PCI_IO_PADDR	((uint)PPC405_PCI_PHY_IO_BASE)
#define PPC4xx_PCI_IO_VADDR	PPC4xx_PCI_IO_PADDR
#define PPC4xx_PCI_IO_SIZE	((uint)64*1024)
#define PPC4xx_PCI_CFG_PADDR	((uint)PPC405_PCI_CONFIG_ADDR)
#define PPC4xx_PCI_CFG_VADDR	PPC4xx_PCI_CFG_PADDR
#define PPC4xx_PCI_CFG_SIZE	((uint)4*1024)
#define PPC4xx_PCI_LCFG_PADDR	((uint)0xef400000)
#define PPC4xx_PCI_LCFG_VADDR	PPC4xx_PCI_LCFG_PADDR
#define PPC4xx_PCI_LCFG_SIZE	((uint)4*1024)
#define PPC4xx_ONB_IO_PADDR	((uint)0xef600000)
#define PPC4xx_ONB_IO_VADDR	PPC4xx_ONB_IO_PADDR
#define PPC4xx_ONB_IO_SIZE	((uint)4*1024)

/* serial port defines */
#define RS_TABLE_SIZE	2

#define UART0_INT	0
#define UART1_INT	1

#define PCIL0_BASE	0xEF400000
#define UART0_IO_BASE	0xEF600300
#define UART1_IO_BASE	0xEF600400
#define EMAC0_BASE	0xEF600800

#define BD_EMAC_ADDR(e,i) bi_enetaddr[e][i]

#if defined(CONFIG_UART0_TTYS0)
#define ACTING_UART0_IO_BASE	UART0_IO_BASE
#define ACTING_UART1_IO_BASE	UART1_IO_BASE
#define ACTING_UART0_INT	UART0_INT
#define ACTING_UART1_INT	UART1_INT
#else
#define ACTING_UART0_IO_BASE	UART1_IO_BASE
#define ACTING_UART1_IO_BASE	UART0_IO_BASE
#define ACTING_UART0_INT	UART1_INT
#define ACTING_UART1_INT	UART0_INT
#endif

#define STD_UART_OP(num)					\
	{ 0, BASE_BAUD, 0, ACTING_UART##num##_INT,			\
		(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST),	\
		iomem_base: (u8 *)ACTING_UART##num##_IO_BASE,		\
		io_type: SERIAL_IO_MEM},

#define SERIAL_DEBUG_IO_BASE	ACTING_UART0_IO_BASE
#define SERIAL_PORT_DFNS	\
	STD_UART_OP(0)		\
	STD_UART_OP(1)

/* DCR defines */
#define DCRN_CPMSR_BASE         0x0BA
#define DCRN_CPMFR_BASE         0x0B9

#define DCRN_CPC0_PLLMR0_BASE   0x0F0
#define DCRN_CPC0_BOOT_BASE     0x0F1
#define DCRN_CPC0_CR1_BASE      0x0F2
#define DCRN_CPC0_EPRCSR_BASE   0x0F3
#define DCRN_CPC0_PLLMR1_BASE   0x0F4
#define DCRN_CPC0_UCR_BASE      0x0F5
#define DCRN_CPC0_UCR_U0DIV     0x07F
#define DCRN_CPC0_SRR_BASE      0x0F6
#define DCRN_CPC0_JTAGID_BASE   0x0F7
#define DCRN_CPC0_SPARE_BASE    0x0F8
#define DCRN_CPC0_PCI_BASE      0x0F9


#define IBM_CPM_GPT             0x80000000      /* GPT interface */
#define IBM_CPM_PCI             0x40000000      /* PCI bridge */
#define IBM_CPM_UIC             0x00010000      /* Universal Int Controller */
#define IBM_CPM_CPU             0x00008000      /* processor core */
#define IBM_CPM_EBC             0x00002000      /* EBC controller */
#define IBM_CPM_SDRAM0          0x00004000      /* SDRAM memory controller */
#define IBM_CPM_GPIO0           0x00001000      /* General Purpose IO */
#define IBM_CPM_TMRCLK          0x00000400      /* CPU timers */
#define IBM_CPM_PLB             0x00000100      /* PLB bus arbiter */
#define IBM_CPM_OPB             0x00000080      /* PLB to OPB bridge */
#define IBM_CPM_DMA             0x00000040      /* DMA controller */
#define IBM_CPM_IIC0            0x00000010      /* IIC interface */
#define IBM_CPM_UART1           0x00000002      /* serial port 0 */
#define IBM_CPM_UART0           0x00000001      /* serial port 1 */
#define DFLT_IBM4xx_PM          ~(IBM_CPM_PCI | IBM_CPM_CPU | IBM_CPM_DMA \
                                        | IBM_CPM_OPB | IBM_CPM_EBC \
                                        | IBM_CPM_SDRAM0 | IBM_CPM_PLB \
                                        | IBM_CPM_UIC | IBM_CPM_TMRCLK)
#define DCRN_DMA0_BASE          0x100
#define DCRN_DMA1_BASE          0x108
#define DCRN_DMA2_BASE          0x110
#define DCRN_DMA3_BASE          0x118
#define DCRNCAP_DMA_SG          1       /* have DMA scatter/gather capability */
#define DCRN_DMASR_BASE         0x120
#define DCRN_EBC_BASE           0x012
#define DCRN_DCP0_BASE          0x014
#define DCRN_MAL_BASE           0x180
#define DCRN_OCM0_BASE          0x018
#define DCRN_PLB0_BASE          0x084
#define DCRN_PLLMR_BASE         0x0B0
#define DCRN_POB0_BASE          0x0A0
#define DCRN_SDRAM0_BASE        0x010
#define DCRN_UIC0_BASE          0x0C0
#define UIC0 DCRN_UIC0_BASE

#include <asm/ibm405.h>

#endif				/* __ASM_IBM405EP_H__ */
#endif				/* __KERNEL__ */
