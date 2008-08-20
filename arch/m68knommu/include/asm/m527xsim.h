/****************************************************************************/

/*
 *	m527xsim.h -- ColdFire 5270/5271 System Integration Module support.
 *
 *	(C) Copyright 2004, Greg Ungerer (gerg@snapgear.com)
 */

/****************************************************************************/
#ifndef	m527xsim_h
#define	m527xsim_h
/****************************************************************************/


/*
 *	Define the 5270/5271 SIM register set addresses.
 */
#define	MCFICM_INTC0		0x0c00		/* Base for Interrupt Ctrl 0 */
#define	MCFICM_INTC1		0x0d00		/* Base for Interrupt Ctrl 1 */
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
#define	MCFINT_UART1		14		/* Interrupt number for UART1 */
#define	MCFINT_UART2		15		/* Interrupt number for UART2 */
#define	MCFINT_PIT1		36		/* Interrupt number for PIT1 */

/*
 *	SDRAM configuration registers.
 */
#ifdef CONFIG_M5271
#define	MCFSIM_DCR		0x40		/* SDRAM control */
#define	MCFSIM_DACR0		0x48		/* SDRAM base address 0 */
#define	MCFSIM_DMR0		0x4c		/* SDRAM address mask 0 */
#define	MCFSIM_DACR1		0x50		/* SDRAM base address 1 */
#define	MCFSIM_DMR1		0x54		/* SDRAM address mask 1 */
#endif
#ifdef CONFIG_M5275
#define	MCFSIM_DMR		0x40		/* SDRAM mode */
#define	MCFSIM_DCR		0x44		/* SDRAM control */
#define	MCFSIM_DCFG1		0x48		/* SDRAM configuration 1 */
#define	MCFSIM_DCFG2		0x4c		/* SDRAM configuration 2 */
#define	MCFSIM_DBAR0		0x50		/* SDRAM base address 0 */
#define	MCFSIM_DMR0		0x54		/* SDRAM address mask 0 */
#define	MCFSIM_DBAR1		0x58		/* SDRAM base address 1 */
#define	MCFSIM_DMR1		0x5c		/* SDRAM address mask 1 */
#endif

/*
 *	GPIO pins setups to enable the UARTs.
 */
#ifdef CONFIG_M5271
#define MCF_GPIO_PAR_UART	0x100048	/* PAR UART address */
#define UART0_ENABLE_MASK	0x000f
#define UART1_ENABLE_MASK	0x0ff0
#define UART2_ENABLE_MASK	0x3000
#endif
#ifdef CONFIG_M5275
#define MCF_GPIO_PAR_UART	0x10007c	/* PAR UART address */
#define UART0_ENABLE_MASK	0x000f
#define UART1_ENABLE_MASK	0x00f0
#define UART2_ENABLE_MASK	0x3f00 
#endif

/****************************************************************************/
#endif	/* m527xsim_h */
