/****************************************************************************/

/*
 *	m528xsim.h -- ColdFire 5280/5282 System Integration Module support.
 *
 *	(C) Copyright 2003, Greg Ungerer (gerg@snapgear.com)
 */

/****************************************************************************/
#ifndef	m528xsim_h
#define	m528xsim_h
/****************************************************************************/


/*
 *	Define the 5280/5282 SIM register set addresses.
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
#define	MCFINT_PIT1		55		/* Interrupt number for PIT1 */

/*
 *	SDRAM configuration registers.
 */
#define	MCFSIM_DCR		0x44		/* SDRAM control */
#define	MCFSIM_DACR0		0x48		/* SDRAM base address 0 */
#define	MCFSIM_DMR0		0x4c		/* SDRAM address mask 0 */
#define	MCFSIM_DACR1		0x50		/* SDRAM base address 1 */
#define	MCFSIM_DMR1		0x54		/* SDRAM address mask 1 */

/*
 *	Derek Cheung - 6 Feb 2005
 *		add I2C and QSPI register definition using Freescale's MCF5282
 */
/* set Port AS pin for I2C or UART */
#define MCF5282_GPIO_PASPAR     (volatile u16 *) (MCF_IPSBAR + 0x00100056)

/* Port UA Pin Assignment Register (8 Bit) */
#define MCF5282_GPIO_PUAPAR	0x10005C

/* Interrupt Mask Register Register Low */ 
#define MCF5282_INTC0_IMRL      (volatile u32 *) (MCF_IPSBAR + 0x0C0C)
/* Interrupt Control Register 7 */
#define MCF5282_INTC0_ICR17     (volatile u8 *) (MCF_IPSBAR + 0x0C51)


/*
 *  Reset Control Unit (relative to IPSBAR).
 */
#define	MCF_RCR			0x110000
#define	MCF_RSR			0x110001

#define	MCF_RCR_SWRESET		0x80		/* Software reset bit */
#define	MCF_RCR_FRCSTOUT	0x40		/* Force external reset */

/*********************************************************************
*
* Inter-IC (I2C) Module
*
*********************************************************************/
/* Read/Write access macros for general use */
#define MCF5282_I2C_I2ADR       (volatile u8 *) (MCF_IPSBAR + 0x0300) // Address 
#define MCF5282_I2C_I2FDR       (volatile u8 *) (MCF_IPSBAR + 0x0304) // Freq Divider
#define MCF5282_I2C_I2CR        (volatile u8 *) (MCF_IPSBAR + 0x0308) // Control
#define MCF5282_I2C_I2SR        (volatile u8 *) (MCF_IPSBAR + 0x030C) // Status
#define MCF5282_I2C_I2DR        (volatile u8 *) (MCF_IPSBAR + 0x0310) // Data I/O

/* Bit level definitions and macros */
#define MCF5282_I2C_I2ADR_ADDR(x)                       (((x)&0x7F)<<0x01)

#define MCF5282_I2C_I2FDR_IC(x)                         (((x)&0x3F))

#define MCF5282_I2C_I2CR_IEN    (0x80)	// I2C enable
#define MCF5282_I2C_I2CR_IIEN   (0x40)  // interrupt enable
#define MCF5282_I2C_I2CR_MSTA   (0x20)  // master/slave mode
#define MCF5282_I2C_I2CR_MTX    (0x10)  // transmit/receive mode
#define MCF5282_I2C_I2CR_TXAK   (0x08)  // transmit acknowledge enable
#define MCF5282_I2C_I2CR_RSTA   (0x04)  // repeat start

#define MCF5282_I2C_I2SR_ICF    (0x80)  // data transfer bit
#define MCF5282_I2C_I2SR_IAAS   (0x40)  // I2C addressed as a slave
#define MCF5282_I2C_I2SR_IBB    (0x20)  // I2C bus busy
#define MCF5282_I2C_I2SR_IAL    (0x10)  // aribitration lost
#define MCF5282_I2C_I2SR_SRW    (0x04)  // slave read/write
#define MCF5282_I2C_I2SR_IIF    (0x02)  // I2C interrupt
#define MCF5282_I2C_I2SR_RXAK   (0x01)  // received acknowledge



/*********************************************************************
*
* Queued Serial Peripheral Interface (QSPI) Module
*
*********************************************************************/
/* Derek - 21 Feb 2005 */
/* change to the format used in I2C */
/* Read/Write access macros for general use */
#define MCF5282_QSPI_QMR        MCF_IPSBAR + 0x0340
#define MCF5282_QSPI_QDLYR      MCF_IPSBAR + 0x0344
#define MCF5282_QSPI_QWR        MCF_IPSBAR + 0x0348
#define MCF5282_QSPI_QIR        MCF_IPSBAR + 0x034C
#define MCF5282_QSPI_QAR        MCF_IPSBAR + 0x0350
#define MCF5282_QSPI_QDR        MCF_IPSBAR + 0x0354
#define MCF5282_QSPI_QCR        MCF_IPSBAR + 0x0354

/* Bit level definitions and macros */
#define MCF5282_QSPI_QMR_MSTR                           (0x8000)
#define MCF5282_QSPI_QMR_DOHIE                          (0x4000)
#define MCF5282_QSPI_QMR_BITS_16                        (0x0000)
#define MCF5282_QSPI_QMR_BITS_8                         (0x2000)
#define MCF5282_QSPI_QMR_BITS_9                         (0x2400)
#define MCF5282_QSPI_QMR_BITS_10                        (0x2800)
#define MCF5282_QSPI_QMR_BITS_11                        (0x2C00)
#define MCF5282_QSPI_QMR_BITS_12                        (0x3000)
#define MCF5282_QSPI_QMR_BITS_13                        (0x3400)
#define MCF5282_QSPI_QMR_BITS_14                        (0x3800)
#define MCF5282_QSPI_QMR_BITS_15                        (0x3C00)
#define MCF5282_QSPI_QMR_CPOL                           (0x0200)
#define MCF5282_QSPI_QMR_CPHA                           (0x0100)
#define MCF5282_QSPI_QMR_BAUD(x)                        (((x)&0x00FF))

#define MCF5282_QSPI_QDLYR_SPE                          (0x80)
#define MCF5282_QSPI_QDLYR_QCD(x)                       (((x)&0x007F)<<8)
#define MCF5282_QSPI_QDLYR_DTL(x)                       (((x)&0x00FF))

#define MCF5282_QSPI_QWR_HALT                           (0x8000)
#define MCF5282_QSPI_QWR_WREN                           (0x4000)
#define MCF5282_QSPI_QWR_WRTO                           (0x2000)
#define MCF5282_QSPI_QWR_CSIV                           (0x1000)
#define MCF5282_QSPI_QWR_ENDQP(x)                       (((x)&0x000F)<<8)
#define MCF5282_QSPI_QWR_CPTQP(x)                       (((x)&0x000F)<<4)
#define MCF5282_QSPI_QWR_NEWQP(x)                       (((x)&0x000F))

#define MCF5282_QSPI_QIR_WCEFB                          (0x8000)
#define MCF5282_QSPI_QIR_ABRTB                          (0x4000)
#define MCF5282_QSPI_QIR_ABRTL                          (0x1000)
#define MCF5282_QSPI_QIR_WCEFE                          (0x0800)
#define MCF5282_QSPI_QIR_ABRTE                          (0x0400)
#define MCF5282_QSPI_QIR_SPIFE                          (0x0100)
#define MCF5282_QSPI_QIR_WCEF                           (0x0008)
#define MCF5282_QSPI_QIR_ABRT                           (0x0004)
#define MCF5282_QSPI_QIR_SPIF                           (0x0001)

#define MCF5282_QSPI_QAR_ADDR(x)                        (((x)&0x003F))

#define MCF5282_QSPI_QDR_COMMAND(x)                     (((x)&0xFF00))
#define MCF5282_QSPI_QCR_DATA(x)                        (((x)&0x00FF)<<8)
#define MCF5282_QSPI_QCR_CONT                           (0x8000)
#define MCF5282_QSPI_QCR_BITSE                          (0x4000)
#define MCF5282_QSPI_QCR_DT                             (0x2000)
#define MCF5282_QSPI_QCR_DSCK                           (0x1000)
#define MCF5282_QSPI_QCR_CS                             (((x)&0x000F)<<8)

/****************************************************************************/
#endif	/* m528xsim_h */
