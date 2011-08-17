/****************************************************************************/

/*
 *	m523xsim.h -- ColdFire 523x System Integration Module support.
 *
 *	(C) Copyright 2003-2005, Greg Ungerer <gerg@snapgear.com>
 */

/****************************************************************************/
#ifndef	m523xsim_h
#define	m523xsim_h
/****************************************************************************/

#define	CPU_NAME		"COLDFIRE(m523x)"
#define	CPU_INSTR_PER_JIFFY	3
#define	MCF_BUSCLK		(MCF_CLK / 2)

#include <asm/m52xxacr.h>

/*
 *	Define the 523x SIM register set addresses.
 */
#define	MCFICM_INTC0		(MCF_IPSBAR + 0x0c00)	/* Base for Interrupt Ctrl 0 */
#define	MCFICM_INTC1		(MCF_IPSBAR + 0x0d00)	/* Base for Interrupt Ctrl 0 */

#define	MCFINTC_IPRH		0x00		/* Interrupt pending 32-63 */
#define	MCFINTC_IPRL		0x04		/* Interrupt pending 1-31 */
#define	MCFINTC_IMRH		0x08		/* Interrupt mask 32-63 */
#define	MCFINTC_IMRL		0x0c		/* Interrupt mask 1-31 */
#define	MCFINTC_INTFRCH		0x10		/* Interrupt force 32-63 */
#define	MCFINTC_INTFRCL		0x14		/* Interrupt force 1-31 */
#define	MCFINTC_IRLR		0x18		/* */
#define	MCFINTC_IACKL		0x19		/* */
#define	MCFINTC_ICR0		0x40		/* Base ICR register */

#define	MCFINT_VECBASE		64		/* Vector base number */
#define	MCFINT_UART0		13		/* Interrupt number for UART0 */
#define	MCFINT_PIT1		36		/* Interrupt number for PIT1 */
#define MCFINT_QSPI		18		/* Interrupt number for QSPI */

/*
 *	SDRAM configuration registers.
 */
#define	MCFSIM_DCR		(MCF_IPSBAR + 0x44)	/* Control */
#define	MCFSIM_DACR0		(MCF_IPSBAR + 0x48)	/* Base address 0 */
#define	MCFSIM_DMR0		(MCF_IPSBAR + 0x4c)	/* Address mask 0 */
#define	MCFSIM_DACR1		(MCF_IPSBAR + 0x50)	/* Base address 1 */
#define	MCFSIM_DMR1		(MCF_IPSBAR + 0x54)	/* Address mask 1 */

/*
 *  Reset Control Unit (relative to IPSBAR).
 */
#define	MCF_RCR			0x110000
#define	MCF_RSR			0x110001

#define	MCF_RCR_SWRESET		0x80		/* Software reset bit */
#define	MCF_RCR_FRCSTOUT	0x40		/* Force external reset */

/*
 *  UART module.
 */
#define MCFUART_BASE1		(MCF_IPSBAR + 0x200)
#define MCFUART_BASE2		(MCF_IPSBAR + 0x240)
#define MCFUART_BASE3		(MCF_IPSBAR + 0x280)

/*
 *  FEC ethernet module.
 */
#define	MCFFEC_BASE		(MCF_IPSBAR + 0x1000)
#define	MCFFEC_SIZE		0x800

/*
 *  GPIO module.
 */
#define MCFGPIO_PODR_ADDR	(MCF_IPSBAR + 0x100000)
#define MCFGPIO_PODR_DATAH	(MCF_IPSBAR + 0x100001)
#define MCFGPIO_PODR_DATAL	(MCF_IPSBAR + 0x100002)
#define MCFGPIO_PODR_BUSCTL	(MCF_IPSBAR + 0x100003)
#define MCFGPIO_PODR_BS		(MCF_IPSBAR + 0x100004)
#define MCFGPIO_PODR_CS		(MCF_IPSBAR + 0x100005)
#define MCFGPIO_PODR_SDRAM	(MCF_IPSBAR + 0x100006)
#define MCFGPIO_PODR_FECI2C	(MCF_IPSBAR + 0x100007)
#define MCFGPIO_PODR_UARTH	(MCF_IPSBAR + 0x100008)
#define MCFGPIO_PODR_UARTL	(MCF_IPSBAR + 0x100009)
#define MCFGPIO_PODR_QSPI	(MCF_IPSBAR + 0x10000A)
#define MCFGPIO_PODR_TIMER	(MCF_IPSBAR + 0x10000B)
#define MCFGPIO_PODR_ETPU	(MCF_IPSBAR + 0x10000C)

#define MCFGPIO_PDDR_ADDR	(MCF_IPSBAR + 0x100010)
#define MCFGPIO_PDDR_DATAH	(MCF_IPSBAR + 0x100011)
#define MCFGPIO_PDDR_DATAL	(MCF_IPSBAR + 0x100012)
#define MCFGPIO_PDDR_BUSCTL	(MCF_IPSBAR + 0x100013)
#define MCFGPIO_PDDR_BS		(MCF_IPSBAR + 0x100014)
#define MCFGPIO_PDDR_CS		(MCF_IPSBAR + 0x100015)
#define MCFGPIO_PDDR_SDRAM	(MCF_IPSBAR + 0x100016)
#define MCFGPIO_PDDR_FECI2C	(MCF_IPSBAR + 0x100017)
#define MCFGPIO_PDDR_UARTH	(MCF_IPSBAR + 0x100018)
#define MCFGPIO_PDDR_UARTL	(MCF_IPSBAR + 0x100019)
#define MCFGPIO_PDDR_QSPI	(MCF_IPSBAR + 0x10001A)
#define MCFGPIO_PDDR_TIMER	(MCF_IPSBAR + 0x10001B)
#define MCFGPIO_PDDR_ETPU	(MCF_IPSBAR + 0x10001C)

#define MCFGPIO_PPDSDR_ADDR	(MCF_IPSBAR + 0x100020)
#define MCFGPIO_PPDSDR_DATAH	(MCF_IPSBAR + 0x100021)
#define MCFGPIO_PPDSDR_DATAL	(MCF_IPSBAR + 0x100022)
#define MCFGPIO_PPDSDR_BUSCTL	(MCF_IPSBAR + 0x100023)
#define MCFGPIO_PPDSDR_BS	(MCF_IPSBAR + 0x100024)
#define MCFGPIO_PPDSDR_CS	(MCF_IPSBAR + 0x100025)
#define MCFGPIO_PPDSDR_SDRAM	(MCF_IPSBAR + 0x100026)
#define MCFGPIO_PPDSDR_FECI2C	(MCF_IPSBAR + 0x100027)
#define MCFGPIO_PPDSDR_UARTH	(MCF_IPSBAR + 0x100028)
#define MCFGPIO_PPDSDR_UARTL	(MCF_IPSBAR + 0x100029)
#define MCFGPIO_PPDSDR_QSPI	(MCF_IPSBAR + 0x10002A)
#define MCFGPIO_PPDSDR_TIMER	(MCF_IPSBAR + 0x10002B)
#define MCFGPIO_PPDSDR_ETPU	(MCF_IPSBAR + 0x10002C)

#define MCFGPIO_PCLRR_ADDR	(MCF_IPSBAR + 0x100030)
#define MCFGPIO_PCLRR_DATAH	(MCF_IPSBAR + 0x100031)
#define MCFGPIO_PCLRR_DATAL	(MCF_IPSBAR + 0x100032)
#define MCFGPIO_PCLRR_BUSCTL	(MCF_IPSBAR + 0x100033)
#define MCFGPIO_PCLRR_BS	(MCF_IPSBAR + 0x100034)
#define MCFGPIO_PCLRR_CS	(MCF_IPSBAR + 0x100035)
#define MCFGPIO_PCLRR_SDRAM	(MCF_IPSBAR + 0x100036)
#define MCFGPIO_PCLRR_FECI2C	(MCF_IPSBAR + 0x100037)
#define MCFGPIO_PCLRR_UARTH	(MCF_IPSBAR + 0x100038)
#define MCFGPIO_PCLRR_UARTL	(MCF_IPSBAR + 0x100039)
#define MCFGPIO_PCLRR_QSPI	(MCF_IPSBAR + 0x10003A)
#define MCFGPIO_PCLRR_TIMER	(MCF_IPSBAR + 0x10003B)
#define MCFGPIO_PCLRR_ETPU	(MCF_IPSBAR + 0x10003C)

/*
 * PIT timer base addresses.
 */
#define	MCFPIT_BASE1		(MCF_IPSBAR + 0x150000)
#define	MCFPIT_BASE2		(MCF_IPSBAR + 0x160000)
#define	MCFPIT_BASE3		(MCF_IPSBAR + 0x170000)
#define	MCFPIT_BASE4		(MCF_IPSBAR + 0x180000)

/*
 * EPort
 */
#define MCFEPORT_EPPAR		(MCF_IPSBAR + 0x130000)
#define MCFEPORT_EPDDR		(MCF_IPSBAR + 0x130002)
#define MCFEPORT_EPIER		(MCF_IPSBAR + 0x130003)
#define MCFEPORT_EPDR		(MCF_IPSBAR + 0x130004)
#define MCFEPORT_EPPDR		(MCF_IPSBAR + 0x130005)
#define MCFEPORT_EPFR		(MCF_IPSBAR + 0x130006)

/*
 * Generic GPIO support
 */
#define MCFGPIO_PODR			MCFGPIO_PODR_ADDR
#define MCFGPIO_PDDR			MCFGPIO_PDDR_ADDR
#define MCFGPIO_PPDR			MCFGPIO_PPDSDR_ADDR
#define MCFGPIO_SETR			MCFGPIO_PPDSDR_ADDR
#define MCFGPIO_CLRR			MCFGPIO_PCLRR_ADDR

#define MCFGPIO_PIN_MAX			107
#define MCFGPIO_IRQ_MAX			8
#define MCFGPIO_IRQ_VECBASE		MCFINT_VECBASE

/*
 * Pin Assignment
*/
#define	MCFGPIO_PAR_QSPI	(MCF_IPSBAR + 0x10004A)
#define	MCFGPIO_PAR_TIMER	(MCF_IPSBAR + 0x10004C)

/*
 * DMA unit base addresses.
 */
#define	MCFDMA_BASE0		(MCF_IPSBAR + 0x100)
#define	MCFDMA_BASE1		(MCF_IPSBAR + 0x140)
#define	MCFDMA_BASE2		(MCF_IPSBAR + 0x180)
#define	MCFDMA_BASE3		(MCF_IPSBAR + 0x1C0)

/****************************************************************************/
#endif	/* m523xsim_h */
