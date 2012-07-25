/*
 *	m54xxsim.h -- ColdFire 547x/548x System Integration Unit support.
 */

#ifndef	m54xxsim_h
#define m54xxsim_h

#define	CPU_NAME		"COLDFIRE(m54xx)"
#define	CPU_INSTR_PER_JIFFY	2
#define	MCF_BUSCLK		(MCF_CLK / 2)

#include <asm/m54xxacr.h>

#define MCFINT_VECBASE		64

/*
 *      Interrupt Controller Registers
 */
#define MCFICM_INTC0		(MCF_MBAR + 0x700) 	/* Base for Interrupt Ctrl 0 */

#define MCFINTC_IPRH		0x00		/* Interrupt pending 32-63 */
#define MCFINTC_IPRL		0x04		/* Interrupt pending 1-31 */
#define MCFINTC_IMRH		0x08		/* Interrupt mask 32-63 */
#define MCFINTC_IMRL		0x0c		/* Interrupt mask 1-31 */
#define MCFINTC_INTFRCH		0x10		/* Interrupt force 32-63 */
#define MCFINTC_INTFRCL		0x14		/* Interrupt force 1-31 */
#define MCFINTC_IRLR		0x18		/* */
#define MCFINTC_IACKL		0x19		/* */
#define MCFINTC_ICR0		0x40		/* Base ICR register */

/*
 *	UART module.
 */
#define MCFUART_BASE0		(MCF_MBAR + 0x8600)	/* Base address UART0 */
#define MCFUART_BASE1		(MCF_MBAR + 0x8700)	/* Base address UART1 */
#define MCFUART_BASE2		(MCF_MBAR + 0x8800)	/* Base address UART2 */
#define MCFUART_BASE3		(MCF_MBAR + 0x8900)	/* Base address UART3 */

/*
 *	Define system peripheral IRQ usage.
 */
#define MCF_IRQ_TIMER		(MCFINT_VECBASE + 54)	/* Slice Timer 0 */
#define MCF_IRQ_PROFILER	(MCFINT_VECBASE + 53)	/* Slice Timer 1 */
#define MCF_IRQ_UART0		(MCFINT_VECBASE + 35)
#define MCF_IRQ_UART1		(MCFINT_VECBASE + 34)
#define MCF_IRQ_UART2		(MCFINT_VECBASE + 33)
#define MCF_IRQ_UART3		(MCFINT_VECBASE + 32)

/*
 *	Generic GPIO support
 */
#define MCFGPIO_PIN_MAX		0	/* I am too lazy to count */
#define MCFGPIO_IRQ_MAX		-1
#define MCFGPIO_IRQ_VECBASE	-1

/*
 *	EDGE Port support.
 */
#define	MCFEPORT_EPPAR		(MCF_MBAR + 0xf00)	/* Pin assignment */
#define	MCFEPORT_EPDDR		(MCF_MBAR + 0xf04)	/* Data direction */
#define	MCFEPORT_EPIER		(MCF_MBAR + 0xf05)	/* Interrupt enable */
#define	MCFEPORT_EPDR		(MCF_MBAR + 0xf08)	/* Port data (w) */
#define	MCFEPORT_EPPDR		(MCF_MBAR + 0xf09)	/* Port data (r) */
#define	MCFEPORT_EPFR		(MCF_MBAR + 0xf0c)	/* Flags */

/*
 *	Some PSC related definitions
 */
#define MCF_PAR_PSC(x)		(0x000A4F-((x)&0x3))
#define MCF_PAR_SDA		(0x0008)
#define MCF_PAR_SCL		(0x0004)
#define MCF_PAR_PSC_TXD		(0x04)
#define MCF_PAR_PSC_RXD		(0x08)
#define MCF_PAR_PSC_RTS(x)	(((x)&0x03)<<4)
#define MCF_PAR_PSC_CTS(x)	(((x)&0x03)<<6)
#define MCF_PAR_PSC_CTS_GPIO	(0x00)
#define MCF_PAR_PSC_CTS_BCLK	(0x80)
#define MCF_PAR_PSC_CTS_CTS	(0xC0)
#define MCF_PAR_PSC_RTS_GPIO    (0x00)
#define MCF_PAR_PSC_RTS_FSYNC	(0x20)
#define MCF_PAR_PSC_RTS_RTS	(0x30)
#define MCF_PAR_PSC_CANRX	(0x40)

#define MCF_PAR_PCIBG		(CONFIG_MBAR + 0xa48)	/* PCI bus grant */
#define MCF_PAR_PCIBR		(CONFIG_MBAR + 0xa4a)	/* PCI */

#endif	/* m54xxsim_h */
