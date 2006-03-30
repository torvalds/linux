/*
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_IBMSTBX25_H__
#define __ASM_IBMSTBX25_H__

#include <linux/config.h>

/* serial port defines */
#define STBx25xx_IO_BASE	((uint)0xe0000000)
#define PPC4xx_ONB_IO_PADDR	STBx25xx_IO_BASE
#define PPC4xx_ONB_IO_VADDR	((uint)0xe0000000)
#define PPC4xx_ONB_IO_SIZE	((uint)14*64*1024)

/*
 * map STBxxxx internal i/o address (0x400x00xx) to an address
 * which is below the 2GB limit...
 *
 * 4000 000x	uart1		-> 0xe000 000x
 * 4001 00xx	uart2
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
#define STBx25xx_MAP_IO_ADDR(a)	(((uint)(a)) + (STBx25xx_IO_BASE - 0x40000000))

#define RS_TABLE_SIZE	3

#define OPB_BASE_START	0x40000000
#define EBIU_BASE_START	0xF0100000
#define DCR_BASE_START  0x0000

#ifdef __BOOTER__
#define UART1_IO_BASE	0x40000000
#define UART2_IO_BASE	0x40010000
#else
#define UART1_IO_BASE	0xe0000000
#define UART2_IO_BASE	0xe0010000
#endif
#define SC0_BASE	0x40020000	/* smart card #0 */
#define IIC0_BASE	0x40030000
#ifdef __BOOTER__
#define UART0_IO_BASE	0x40040000
#else
#define UART0_IO_BASE	0xe0040000
#endif
#define SCC0_BASE	0x40040000	/* Serial 0 controller IrdA */
#define GPT0_BASE	0x40050000	/* General purpose timers */
#define GPIO0_BASE	0x40060000
#define SC1_BASE	0x40070000	/* smart card #1 */
#define SCP0_BASE	0x400C0000	/* Serial Controller Port */
#define SSP0_BASE	0x400D0000	/* Sync serial port */

#define IDE0_BASE		0xf0100000
#define REDWOOD_IDE_CTRL	0xf1100000

#define RTCFPC_IRQ	0
#define XPORT_IRQ	1
#define AUD_IRQ		2
#define AID_IRQ		3
#define DMA0		4
#define DMA1_IRQ	5
#define DMA2_IRQ	6
#define DMA3_IRQ	7
#define SC0_IRQ		8
#define IIC0_IRQ	9
#define IIR0_IRQ	10
#define GPT0_IRQ	11
#define GPT1_IRQ	12
#define SCP0_IRQ	13
#define SSP0_IRQ	14
#define GPT2_IRQ	15	/* count down timer */
#define SC1_IRQ		16
/* IRQ 17 - 19  external */
#define UART0_INT	20
#define UART1_INT	21
#define UART2_INT	22
#define XPTDMA_IRQ	23
#define DCRIDE_IRQ	24
/* IRQ 25 - 30 external */
#define IDE0_IRQ	26

#define IIC_NUMS	1
#define UART_NUMS	3
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
#define DCRN_RTCFP_BASE		0x310

#define UIC0 DCRN_UIC0_BASE

#define IBM_CPM_IIC0	0x80000000	/* IIC 0 interface */
#define IBM_CPM_CPU	0x10000000	/* PPC405B3 clock control */
#define IBM_CPM_AUD	0x08000000	/* Audio Decoder */
#define IBM_CPM_EBIU	0x04000000	/* External Bus Interface Unit */
#define IBM_CPM_IRR	0x02000000	/* Infrared receiver */
#define IBM_CPM_DMA	0x01000000	/* DMA controller */
#define IBM_CPM_UART2	0x00200000	/* Serial Control Port */
#define IBM_CPM_UART1	0x00100000	/* Serial 1 / Infrared */
#define IBM_CPM_UART0	0x00080000	/* Serial 0 / 16550 */
#define IBM_PM_DCRIDE	0x00040000	/* DCR timeout & IDE line Mode clock */
#define IBM_CPM_SC0	0x00020000	/* Smart Card 0 */
#define IBM_CPM_VID	0x00010000	/* reserved */
#define IBM_CPM_SC1	0x00008000	/* Smart Card 0 */
#define IBM_CPM_XPT0	0x00002000	/* Transport - 54 Mhz */
#define IBM_CPM_CBS	0x00001000	/* Cross Bar Switch */
#define IBM_CPM_GPT	0x00000800	/* GPTPWM */
#define IBM_CPM_GPIO0	0x00000400	/* General Purpose IO 0 */
#define IBM_CPM_DENC	0x00000200	/* Digital video Encoder */
#define IBM_CPM_C405T	0x00000100	/* CPU timers */
#define IBM_CPM_XPT27	0x00000080	/* Transport - 27 Mhz */
#define IBM_CPM_UIC	0x00000040	/* Universal Interrupt Controller */
#define IBM_CPM_RTCFPC	0x00000020	/* Realtime clock and front panel */
#define IBM_CPM_SSP	0x00000010	/* Modem Serial Interface (SSP) */
#define IBM_CPM_VID2	0x00000002	/* Video Decoder clock domain 2 */
#define DFLT_IBM4xx_PM	~(IBM_CPM_CPU | IBM_CPM_EBIU | IBM_CPM_DMA	\
			| IBM_CPM_CBS | IBM_CPM_XPT0 | IBM_CPM_C405T 	\
			| IBM_CPM_XPT27 | IBM_CPM_UIC)

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

#define DCRN_RTC_FPC0_CNTL 	(DCRN_RTCFP_BASE + 0x00)	/* RTC cntl */
#define DCRN_RTC_FPC0_INT 	(DCRN_RTCFP_BASE + 0x01)	/* RTC Interrupt */
#define DCRN_RTC_FPC0_TIME 	(DCRN_RTCFP_BASE + 0x02)	/* RTC time reg */
#define DCRN_RTC_FPC0_ALRM 	(DCRN_RTCFP_BASE + 0x03)	/* RTC Alarm reg */
#define DCRN_RTC_FPC0_D1 	(DCRN_RTCFP_BASE + 0x04)	/* LED Data 1 */
#define DCRN_RTC_FPC0_D2 	(DCRN_RTCFP_BASE + 0x05)	/* LED Data 2 */
#define DCRN_RTC_FPC0_D3 	(DCRN_RTCFP_BASE + 0x06)	/* LED Data 3 */
#define DCRN_RTC_FPC0_D4 	(DCRN_RTCFP_BASE + 0x07)	/* LED Data 4 */
#define DCRN_RTC_FPC0_D5 	(DCRN_RTCFP_BASE + 0x08)	/* LED Data 5 */
#define DCRN_RTC_FPC0_FCNTL 	(DCRN_RTCFP_BASE + 0x09)	/* LED control */
#define DCRN_RTC_FPC0_BRT 	(DCRN_RTCFP_BASE + 0x0A)	/* Brightness cntl */

#include <asm/ibm405.h>

#endif				/* __ASM_IBMSTBX25_H__ */
#endif				/* __KERNEL__ */
