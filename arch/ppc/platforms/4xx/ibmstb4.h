/*
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_IBMSTB4_H__
#define __ASM_IBMSTB4_H__

#include <linux/config.h>

/* serial port defines */
#define STB04xxx_IO_BASE	((uint)0xe0000000)
#define PPC4xx_PCI_IO_ADDR	STB04xxx_IO_BASE
#define PPC4xx_ONB_IO_PADDR	STB04xxx_IO_BASE
#define PPC4xx_ONB_IO_VADDR	((uint)0xe0000000)
#define PPC4xx_ONB_IO_SIZE	((uint)14*64*1024)

/*
 * map STB04xxx internal i/o address (0x400x00xx) to an address
 * which is below the 2GB limit...
 *
 * 4000 000x	uart1		-> 0xe000 000x
 * 4001 00xx	ppu
 * 4002 00xx	smart card
 * 4003 000x	iic
 * 4004 000x	uart0
 * 4005 0xxx	timer
 * 4006 00xx	gpio
 * 4007 00xx	smart card
 * 400b 000x	iic
 * 400c 000x	scp
 * 400d 000x	modem
 * 400e 000x	uart2
*/
#define STB04xxx_MAP_IO_ADDR(a)	(((uint)(a)) + (STB04xxx_IO_BASE - 0x40000000))

#define RS_TABLE_SIZE		3
#define UART0_INT		20

#ifdef __BOOTER__
#define UART0_IO_BASE		0x40040000
#else
#define UART0_IO_BASE		0xe0040000
#endif

#define UART1_INT		21

#ifdef __BOOTER__
#define UART1_IO_BASE		0x40000000
#else
#define UART1_IO_BASE		0xe0000000
#endif

#define UART2_INT		31
#ifdef __BOOTER__
#define UART2_IO_BASE		0x400e0000
#else
#define UART2_IO_BASE		0xe00e0000
#endif

#define IDE0_BASE	0x400F0000
#define IDE0_SIZE	0x200
#define IDE0_IRQ	25
#define IIC0_BASE	0x40030000
#define IIC1_BASE	0x400b0000
#define OPB0_BASE	0x40000000
#define GPIO0_BASE	0x40060000

#define USB0_BASE	0x40010000
#define USB0_SIZE	0xA0
#define USB0_IRQ	18

#define IIC_NUMS 2
#define UART_NUMS	3
#define IIC0_IRQ	9
#define IIC1_IRQ	10
#define IIC_OWN		0x55
#define IIC_CLOCK	50

#define BD_EMAC_ADDR(e,i) bi_enetaddr[i]

#define STD_UART_OP(num)					\
	{ 0, BASE_BAUD, 0, UART##num##_INT,			\
		(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST),	\
		iomem_base: (u8 *)UART##num##_IO_BASE,		\
		io_type: SERIAL_IO_MEM},

#if defined(CONFIG_UART0_TTYS0)
#define SERIAL_DEBUG_IO_BASE	UART0_IO_BASE
#define SERIAL_PORT_DFNS	\
	STD_UART_OP(0)		\
	STD_UART_OP(1)		\
	STD_UART_OP(2)
#endif

#if defined(CONFIG_UART0_TTYS1)
#define SERIAL_DEBUG_IO_BASE	UART2_IO_BASE
#define SERIAL_PORT_DFNS	\
	STD_UART_OP(1)		\
	STD_UART_OP(0)		\
	STD_UART_OP(2)
#endif

#if defined(CONFIG_UART0_TTYS2)
#define SERIAL_DEBUG_IO_BASE	UART2_IO_BASE
#define SERIAL_PORT_DFNS	\
	STD_UART_OP(2)		\
	STD_UART_OP(0)		\
	STD_UART_OP(1)
#endif

#define DCRN_BE_BASE		0x090
#define DCRN_DMA0_BASE		0x0C0
#define DCRN_DMA1_BASE		0x0C8
#define DCRN_DMA2_BASE		0x0D0
#define DCRN_DMA3_BASE		0x0D8
#define DCRNCAP_DMA_CC		1	/* have DMA chained count capability */
#define DCRN_DMASR_BASE		0x0E0
#define DCRN_PLB0_BASE		0x054
#define DCRN_PLB1_BASE		0x064
#define DCRN_POB0_BASE		0x0B0
#define DCRN_SCCR_BASE		0x120
#define DCRN_UIC0_BASE		0x040
#define DCRN_BE_BASE		0x090
#define DCRN_DMA0_BASE		0x0C0
#define DCRN_DMA1_BASE		0x0C8
#define DCRN_DMA2_BASE		0x0D0
#define DCRN_DMA3_BASE		0x0D8
#define DCRN_CIC_BASE 		0x030
#define DCRN_DMASR_BASE		0x0E0
#define DCRN_EBIMC_BASE		0x070
#define DCRN_DCRX_BASE		0x020
#define DCRN_CPMFR_BASE		0x102
#define DCRN_SCCR_BASE		0x120
#define UIC0 DCRN_UIC0_BASE

#define IBM_CPM_IIC0	0x80000000	/* IIC 0 interface */
#define IBM_CPM_USB0	0x40000000	/* IEEE-1284 */
#define IBM_CPM_IIC1	0x20000000	/* IIC 1 interface */
#define IBM_CPM_CPU	0x10000000	/* PPC405B3 clock control */
#define IBM_CPM_AUD	0x08000000	/* Audio Decoder */
#define IBM_CPM_EBIU	0x04000000	/* External Bus Interface Unit */
#define IBM_CPM_SDRAM1	0x02000000	/* SDRAM 1 memory controller */
#define IBM_CPM_DMA	0x01000000	/* DMA controller */
#define IBM_CPM_DMA1	0x00800000	/* reserved */
#define IBM_CPM_XPT1	0x00400000	/* reserved */
#define IBM_CPM_XPT2	0x00200000	/* reserved */
#define IBM_CPM_UART1	0x00100000	/* Serial 1 / Infrared */
#define IBM_CPM_UART0	0x00080000	/* Serial 0 / 16550 */
#define IBM_CPM_EPI	0x00040000	/* DCR Extension */
#define IBM_CPM_SC0	0x00020000	/* Smart Card 0 */
#define IBM_CPM_VID	0x00010000	/* reserved */
#define IBM_CPM_SC1	0x00008000	/* Smart Card 1 */
#define IBM_CPM_USBSDRA	0x00004000	/* SDRAM 0 memory controller */
#define IBM_CPM_XPT0	0x00002000	/* Transport - 54 Mhz */
#define IBM_CPM_CBS	0x00001000	/* Cross Bar Switch */
#define IBM_CPM_GPT	0x00000800	/* GPTPWM */
#define IBM_CPM_GPIO0	0x00000400	/* General Purpose IO 0 */
#define IBM_CPM_DENC	0x00000200	/* Digital video Encoder */
#define IBM_CPM_TMRCLK	0x00000100	/* CPU timers */
#define IBM_CPM_XPT27	0x00000080	/* Transport - 27 Mhz */
#define IBM_CPM_UIC	0x00000040	/* Universal Interrupt Controller */
#define IBM_CPM_SSP	0x00000010	/* Modem Serial Interface (SSP) */
#define IBM_CPM_UART2	0x00000008	/* Serial Control Port */
#define IBM_CPM_DDIO	0x00000004	/* Descrambler */
#define IBM_CPM_VID2	0x00000002	/* Video Decoder clock domain 2 */

#define DFLT_IBM4xx_PM	~(IBM_CPM_CPU | IBM_CPM_EBIU | IBM_CPM_SDRAM1 \
			| IBM_CPM_DMA | IBM_CPM_DMA1 | IBM_CPM_CBS \
			| IBM_CPM_USBSDRA | IBM_CPM_XPT0 | IBM_CPM_TMRCLK \
			| IBM_CPM_XPT27 | IBM_CPM_UIC )

#define DCRN_BEAR	(DCRN_BE_BASE + 0x0)	/* Bus Error Address Register */
#define DCRN_BESR	(DCRN_BE_BASE + 0x1)	/* Bus Error Syndrome Register */
/* DCRN_BESR */
#define BESR_DSES	0x80000000	/* Data-Side Error Status */
#define BESR_DMES	0x40000000	/* DMA Error Status */
#define BESR_RWS	0x20000000	/* Read/Write Status */
#define BESR_ETMASK	0x1C000000	/* Error Type */
#define ET_PROT		0
#define ET_PARITY	1
#define ET_NCFG		2
#define ET_BUSERR	4
#define ET_BUSTO	6

#define CHR1_CETE	0x00800000	/* CPU external timer enable */
#define CHR1_PCIPW	0x00008000	/* PCI Int enable/Peripheral Write enable */

#define DCRN_CICCR	(DCRN_CIC_BASE + 0x0)	/* CIC Control Register */
#define DCRN_DMAS1	(DCRN_CIC_BASE + 0x1)	/* DMA Select1 Register */
#define DCRN_DMAS2	(DCRN_CIC_BASE + 0x2)	/* DMA Select2 Register */
#define DCRN_CICVCR	(DCRN_CIC_BASE + 0x3)	/* CIC Video COntro Register */
#define DCRN_CICSEL3	(DCRN_CIC_BASE + 0x5)	/* CIC Select 3 Register */
#define DCRN_SGPO	(DCRN_CIC_BASE + 0x6)	/* CIC GPIO Output Register */
#define DCRN_SGPOD	(DCRN_CIC_BASE + 0x7)	/* CIC GPIO OD Register */
#define DCRN_SGPTC	(DCRN_CIC_BASE + 0x8)	/* CIC GPIO Tristate Ctrl Reg */
#define DCRN_SGPI	(DCRN_CIC_BASE + 0x9)	/* CIC GPIO Input Reg */

#define DCRN_DCRXICR	(DCRN_DCRX_BASE + 0x0)	/* Internal Control Register */
#define DCRN_DCRXISR	(DCRN_DCRX_BASE + 0x1)	/* Internal Status Register */
#define DCRN_DCRXECR	(DCRN_DCRX_BASE + 0x2)	/* External Control Register */
#define DCRN_DCRXESR	(DCRN_DCRX_BASE + 0x3)	/* External Status Register */
#define DCRN_DCRXTAR	(DCRN_DCRX_BASE + 0x4)	/* Target Address Register */
#define DCRN_DCRXTDR	(DCRN_DCRX_BASE + 0x5)	/* Target Data Register */
#define DCRN_DCRXIGR	(DCRN_DCRX_BASE + 0x6)	/* Interrupt Generation Register */
#define DCRN_DCRXBCR	(DCRN_DCRX_BASE + 0x7)	/* Line Buffer Control Register */

#define DCRN_BRCRH0	(DCRN_EBIMC_BASE + 0x0)	/* Bus Region Config High 0 */
#define DCRN_BRCRH1	(DCRN_EBIMC_BASE + 0x1)	/* Bus Region Config High 1 */
#define DCRN_BRCRH2	(DCRN_EBIMC_BASE + 0x2)	/* Bus Region Config High 2 */
#define DCRN_BRCRH3	(DCRN_EBIMC_BASE + 0x3)	/* Bus Region Config High 3 */
#define DCRN_BRCRH4	(DCRN_EBIMC_BASE + 0x4)	/* Bus Region Config High 4 */
#define DCRN_BRCRH5	(DCRN_EBIMC_BASE + 0x5)	/* Bus Region Config High 5 */
#define DCRN_BRCRH6	(DCRN_EBIMC_BASE + 0x6)	/* Bus Region Config High 6 */
#define DCRN_BRCRH7	(DCRN_EBIMC_BASE + 0x7)	/* Bus Region Config High 7 */
#define DCRN_BRCR0	(DCRN_EBIMC_BASE + 0x10)	/* BRC 0 */
#define DCRN_BRCR1	(DCRN_EBIMC_BASE + 0x11)	/* BRC 1 */
#define DCRN_BRCR2	(DCRN_EBIMC_BASE + 0x12)	/* BRC 2 */
#define DCRN_BRCR3	(DCRN_EBIMC_BASE + 0x13)	/* BRC 3 */
#define DCRN_BRCR4	(DCRN_EBIMC_BASE + 0x14)	/* BRC 4 */
#define DCRN_BRCR5	(DCRN_EBIMC_BASE + 0x15)	/* BRC 5 */
#define DCRN_BRCR6	(DCRN_EBIMC_BASE + 0x16)	/* BRC 6 */
#define DCRN_BRCR7	(DCRN_EBIMC_BASE + 0x17)	/* BRC 7 */
#define DCRN_BEAR0	(DCRN_EBIMC_BASE + 0x20)	/* Bus Error Address Register */
#define DCRN_BESR0	(DCRN_EBIMC_BASE + 0x21)	/* Bus Error Status Register */
#define DCRN_BIUCR	(DCRN_EBIMC_BASE + 0x2A)	/* Bus Interfac Unit Ctrl Reg */

#include <asm/ibm405.h>

#endif				/* __ASM_IBMSTB4_H__ */
#endif				/* __KERNEL__ */
