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


/*
 *	Define the 523x SIM register set addresses.
 */
#define	MCFICM_INTC0		0x0c00		/* Base for Interrupt Ctrl 0 */
#define	MCFICM_INTC1		0x0d00		/* Base for Interrupt Ctrl 0 */
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
#define	MCFSIM_DCR		0x44		/* SDRAM control */
#define	MCFSIM_DACR0		0x48		/* SDRAM base address 0 */
#define	MCFSIM_DMR0		0x4c		/* SDRAM address mask 0 */
#define	MCFSIM_DACR1		0x50		/* SDRAM base address 1 */
#define	MCFSIM_DMR1		0x54		/* SDRAM address mask 1 */

/****************************************************************************/
#endif	/* m523xsim_h */
