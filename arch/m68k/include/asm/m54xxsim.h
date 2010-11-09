/*
 *	m54xxsim.h -- ColdFire 547x/548x System Integration Unit support.
 */

#ifndef	m54xxsim_h
#define m54xxsim_h

#define	CPU_NAME		"COLDFIRE(m54xx)"
#define	CPU_INSTR_PER_JIFFY	2

#include <asm/m54xxacr.h>

#define MCFINT_VECBASE		64

/*
 *      Interrupt Controller Registers
 */
#define MCFICM_INTC0		0x0700		/* Base for Interrupt Ctrl 0 */
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
#define MCFUART_BASE1		0x8600		/* Base address of UART1 */
#define MCFUART_BASE2		0x8700		/* Base address of UART2 */
#define MCFUART_BASE3		0x8800		/* Base address of UART3 */
#define MCFUART_BASE4		0x8900		/* Base address of UART4 */

/*
 *	Define system peripheral IRQ usage.
 */
#define MCF_IRQ_TIMER		(64 + 54)	/* Slice Timer 0 */
#define MCF_IRQ_PROFILER	(64 + 53)	/* Slice Timer 1 */

/*
 *	Generic GPIO support
 */
#define MCFGPIO_PIN_MAX		0	/* I am too lazy to count */
#define MCFGPIO_IRQ_MAX		-1
#define MCFGPIO_IRQ_VECBASE	-1

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

#endif	/* m54xxsim_h */
