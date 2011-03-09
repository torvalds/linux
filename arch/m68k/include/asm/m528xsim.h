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

#define	CPU_NAME		"COLDFIRE(m528x)"
#define	CPU_INSTR_PER_JIFFY	3

#include <asm/m52xxacr.h>

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
#define	MCFINT_QSPI		18		/* Interrupt number for QSPI */
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
 *	UART module.
 */
#define MCFUART_BASE1		0x200           /* Base address of UART1 */
#define MCFUART_BASE2		0x240           /* Base address of UART2 */
#define MCFUART_BASE3		0x280           /* Base address of UART3 */

/*
 * 	GPIO registers
 */
#define MCFGPIO_PORTA		(MCF_IPSBAR + 0x00100000)
#define MCFGPIO_PORTB		(MCF_IPSBAR + 0x00100001)
#define MCFGPIO_PORTC		(MCF_IPSBAR + 0x00100002)
#define MCFGPIO_PORTD		(MCF_IPSBAR + 0x00100003)
#define MCFGPIO_PORTE		(MCF_IPSBAR + 0x00100004)
#define MCFGPIO_PORTF		(MCF_IPSBAR + 0x00100005)
#define MCFGPIO_PORTG		(MCF_IPSBAR + 0x00100006)
#define MCFGPIO_PORTH		(MCF_IPSBAR + 0x00100007)
#define MCFGPIO_PORTJ		(MCF_IPSBAR + 0x00100008)
#define MCFGPIO_PORTDD		(MCF_IPSBAR + 0x00100009)
#define MCFGPIO_PORTEH		(MCF_IPSBAR + 0x0010000A)
#define MCFGPIO_PORTEL		(MCF_IPSBAR + 0x0010000B)
#define MCFGPIO_PORTAS		(MCF_IPSBAR + 0x0010000C)
#define MCFGPIO_PORTQS		(MCF_IPSBAR + 0x0010000D)
#define MCFGPIO_PORTSD		(MCF_IPSBAR + 0x0010000E)
#define MCFGPIO_PORTTC		(MCF_IPSBAR + 0x0010000F)
#define MCFGPIO_PORTTD		(MCF_IPSBAR + 0x00100010)
#define MCFGPIO_PORTUA		(MCF_IPSBAR + 0x00100011)

#define MCFGPIO_DDRA		(MCF_IPSBAR + 0x00100014)
#define MCFGPIO_DDRB		(MCF_IPSBAR + 0x00100015)
#define MCFGPIO_DDRC		(MCF_IPSBAR + 0x00100016)
#define MCFGPIO_DDRD		(MCF_IPSBAR + 0x00100017)
#define MCFGPIO_DDRE		(MCF_IPSBAR + 0x00100018)
#define MCFGPIO_DDRF		(MCF_IPSBAR + 0x00100019)
#define MCFGPIO_DDRG		(MCF_IPSBAR + 0x0010001A)
#define MCFGPIO_DDRH		(MCF_IPSBAR + 0x0010001B)
#define MCFGPIO_DDRJ		(MCF_IPSBAR + 0x0010001C)
#define MCFGPIO_DDRDD		(MCF_IPSBAR + 0x0010001D)
#define MCFGPIO_DDREH		(MCF_IPSBAR + 0x0010001E)
#define MCFGPIO_DDREL		(MCF_IPSBAR + 0x0010001F)
#define MCFGPIO_DDRAS		(MCF_IPSBAR + 0x00100020)
#define MCFGPIO_DDRQS		(MCF_IPSBAR + 0x00100021)
#define MCFGPIO_DDRSD		(MCF_IPSBAR + 0x00100022)
#define MCFGPIO_DDRTC		(MCF_IPSBAR + 0x00100023)
#define MCFGPIO_DDRTD		(MCF_IPSBAR + 0x00100024)
#define MCFGPIO_DDRUA		(MCF_IPSBAR + 0x00100025)

#define MCFGPIO_PORTAP		(MCF_IPSBAR + 0x00100028)
#define MCFGPIO_PORTBP		(MCF_IPSBAR + 0x00100029)
#define MCFGPIO_PORTCP		(MCF_IPSBAR + 0x0010002A)
#define MCFGPIO_PORTDP		(MCF_IPSBAR + 0x0010002B)
#define MCFGPIO_PORTEP		(MCF_IPSBAR + 0x0010002C)
#define MCFGPIO_PORTFP		(MCF_IPSBAR + 0x0010002D)
#define MCFGPIO_PORTGP		(MCF_IPSBAR + 0x0010002E)
#define MCFGPIO_PORTHP		(MCF_IPSBAR + 0x0010002F)
#define MCFGPIO_PORTJP		(MCF_IPSBAR + 0x00100030)
#define MCFGPIO_PORTDDP		(MCF_IPSBAR + 0x00100031)
#define MCFGPIO_PORTEHP		(MCF_IPSBAR + 0x00100032)
#define MCFGPIO_PORTELP		(MCF_IPSBAR + 0x00100033)
#define MCFGPIO_PORTASP		(MCF_IPSBAR + 0x00100034)
#define MCFGPIO_PORTQSP		(MCF_IPSBAR + 0x00100035)
#define MCFGPIO_PORTSDP		(MCF_IPSBAR + 0x00100036)
#define MCFGPIO_PORTTCP		(MCF_IPSBAR + 0x00100037)
#define MCFGPIO_PORTTDP		(MCF_IPSBAR + 0x00100038)
#define MCFGPIO_PORTUAP		(MCF_IPSBAR + 0x00100039)

#define MCFGPIO_SETA		(MCF_IPSBAR + 0x00100028)
#define MCFGPIO_SETB		(MCF_IPSBAR + 0x00100029)
#define MCFGPIO_SETC		(MCF_IPSBAR + 0x0010002A)
#define MCFGPIO_SETD		(MCF_IPSBAR + 0x0010002B)
#define MCFGPIO_SETE		(MCF_IPSBAR + 0x0010002C)
#define MCFGPIO_SETF		(MCF_IPSBAR + 0x0010002D)
#define MCFGPIO_SETG		(MCF_IPSBAR + 0x0010002E)
#define MCFGPIO_SETH		(MCF_IPSBAR + 0x0010002F)
#define MCFGPIO_SETJ		(MCF_IPSBAR + 0x00100030)
#define MCFGPIO_SETDD		(MCF_IPSBAR + 0x00100031)
#define MCFGPIO_SETEH		(MCF_IPSBAR + 0x00100032)
#define MCFGPIO_SETEL		(MCF_IPSBAR + 0x00100033)
#define MCFGPIO_SETAS		(MCF_IPSBAR + 0x00100034)
#define MCFGPIO_SETQS		(MCF_IPSBAR + 0x00100035)
#define MCFGPIO_SETSD		(MCF_IPSBAR + 0x00100036)
#define MCFGPIO_SETTC		(MCF_IPSBAR + 0x00100037)
#define MCFGPIO_SETTD		(MCF_IPSBAR + 0x00100038)
#define MCFGPIO_SETUA		(MCF_IPSBAR + 0x00100039)

#define MCFGPIO_CLRA		(MCF_IPSBAR + 0x0010003C)
#define MCFGPIO_CLRB		(MCF_IPSBAR + 0x0010003D)
#define MCFGPIO_CLRC		(MCF_IPSBAR + 0x0010003E)
#define MCFGPIO_CLRD		(MCF_IPSBAR + 0x0010003F)
#define MCFGPIO_CLRE		(MCF_IPSBAR + 0x00100040)
#define MCFGPIO_CLRF		(MCF_IPSBAR + 0x00100041)
#define MCFGPIO_CLRG		(MCF_IPSBAR + 0x00100042)
#define MCFGPIO_CLRH		(MCF_IPSBAR + 0x00100043)
#define MCFGPIO_CLRJ		(MCF_IPSBAR + 0x00100044)
#define MCFGPIO_CLRDD		(MCF_IPSBAR + 0x00100045)
#define MCFGPIO_CLREH		(MCF_IPSBAR + 0x00100046)
#define MCFGPIO_CLREL		(MCF_IPSBAR + 0x00100047)
#define MCFGPIO_CLRAS		(MCF_IPSBAR + 0x00100048)
#define MCFGPIO_CLRQS		(MCF_IPSBAR + 0x00100049)
#define MCFGPIO_CLRSD		(MCF_IPSBAR + 0x0010004A)
#define MCFGPIO_CLRTC		(MCF_IPSBAR + 0x0010004B)
#define MCFGPIO_CLRTD		(MCF_IPSBAR + 0x0010004C)
#define MCFGPIO_CLRUA		(MCF_IPSBAR + 0x0010004D)

#define MCFGPIO_PBCDPAR		(MCF_IPSBAR + 0x00100050)
#define MCFGPIO_PFPAR		(MCF_IPSBAR + 0x00100051)
#define MCFGPIO_PEPAR		(MCF_IPSBAR + 0x00100052)
#define MCFGPIO_PJPAR		(MCF_IPSBAR + 0x00100054)
#define MCFGPIO_PSDPAR		(MCF_IPSBAR + 0x00100055)
#define MCFGPIO_PASPAR		(MCF_IPSBAR + 0x00100056)
#define MCFGPIO_PEHLPAR		(MCF_IPSBAR + 0x00100058)
#define MCFGPIO_PQSPAR		(MCF_IPSBAR + 0x00100059)
#define MCFGPIO_PTCPAR		(MCF_IPSBAR + 0x0010005A)
#define MCFGPIO_PTDPAR		(MCF_IPSBAR + 0x0010005B)
#define MCFGPIO_PUAPAR		(MCF_IPSBAR + 0x0010005C)

/*
 * 	Edge Port registers
 */
#define MCFEPORT_EPPAR		(MCF_IPSBAR + 0x00130000)
#define MCFEPORT_EPDDR		(MCF_IPSBAR + 0x00130002)
#define MCFEPORT_EPIER		(MCF_IPSBAR + 0x00130003)
#define MCFEPORT_EPDR		(MCF_IPSBAR + 0x00130004)
#define MCFEPORT_EPPDR		(MCF_IPSBAR + 0x00130005)
#define MCFEPORT_EPFR		(MCF_IPSBAR + 0x00130006)

/*
 * 	Queued ADC registers
 */
#define MCFQADC_PORTQA		(MCF_IPSBAR + 0x00190006)
#define MCFQADC_PORTQB		(MCF_IPSBAR + 0x00190007)
#define MCFQADC_DDRQA		(MCF_IPSBAR + 0x00190008)
#define MCFQADC_DDRQB		(MCF_IPSBAR + 0x00190009)

/*
 * 	General Purpose Timers registers
 */
#define MCFGPTA_GPTPORT		(MCF_IPSBAR + 0x001A001D)
#define MCFGPTA_GPTDDR		(MCF_IPSBAR + 0x001A001E)
#define MCFGPTB_GPTPORT		(MCF_IPSBAR + 0x001B001D)
#define MCFGPTB_GPTDDR		(MCF_IPSBAR + 0x001B001E)
/*
 *
 * definitions for generic gpio support
 *
 */
#define MCFGPIO_PODR		MCFGPIO_PORTA	/* port output data */
#define MCFGPIO_PDDR		MCFGPIO_DDRA	/* port data direction */
#define MCFGPIO_PPDR		MCFGPIO_PORTAP	/* port pin data */
#define MCFGPIO_SETR		MCFGPIO_SETA	/* set output */
#define MCFGPIO_CLRR		MCFGPIO_CLRA	/* clr output */

#define MCFGPIO_IRQ_MAX		8
#define MCFGPIO_IRQ_VECBASE	MCFINT_VECBASE
#define MCFGPIO_PIN_MAX		180


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


#endif	/* m528xsim_h */
