/****************************************************************************/

/*
 *  m520xsim.h -- ColdFire 5207/5208 System Integration Module support.
 *
 *  (C) Copyright 2005, Intec Automation (mike@steroidmicros.com)
 */

/****************************************************************************/
#ifndef m520xsim_h
#define m520xsim_h
/****************************************************************************/

#define	CPU_NAME		"COLDFIRE(m520x)"
#define	CPU_INSTR_PER_JIFFY	3
#define	MCF_BUSCLK		(MCF_CLK / 2)

#include <asm/m52xxacr.h>

/*
 *  Define the 520x SIM register set addresses.
 */
#define MCFICM_INTC0        0xFC048000  /* Base for Interrupt Ctrl 0 */
#define MCFINTC_IPRH        0x00        /* Interrupt pending 32-63 */
#define MCFINTC_IPRL        0x04        /* Interrupt pending 1-31 */
#define MCFINTC_IMRH        0x08        /* Interrupt mask 32-63 */
#define MCFINTC_IMRL        0x0c        /* Interrupt mask 1-31 */
#define MCFINTC_INTFRCH     0x10        /* Interrupt force 32-63 */
#define MCFINTC_INTFRCL     0x14        /* Interrupt force 1-31 */
#define MCFINTC_SIMR        0x1c        /* Set interrupt mask 0-63 */
#define MCFINTC_CIMR        0x1d        /* Clear interrupt mask 0-63 */
#define MCFINTC_ICR0        0x40        /* Base ICR register */

/*
 *  The common interrupt controller code just wants to know the absolute
 *  address to the SIMR and CIMR registers (not offsets into IPSBAR).
 *  The 520x family only has a single INTC unit.
 */
#define MCFINTC0_SIMR       (MCFICM_INTC0 + MCFINTC_SIMR)
#define MCFINTC0_CIMR       (MCFICM_INTC0 + MCFINTC_CIMR)
#define	MCFINTC0_ICR0       (MCFICM_INTC0 + MCFINTC_ICR0)
#define MCFINTC1_SIMR       (0)
#define MCFINTC1_CIMR       (0)
#define	MCFINTC1_ICR0       (0)

#define MCFINT_VECBASE      64
#define MCFINT_UART0        26          /* Interrupt number for UART0 */
#define MCFINT_UART1        27          /* Interrupt number for UART1 */
#define MCFINT_UART2        28          /* Interrupt number for UART2 */
#define MCFINT_QSPI         31          /* Interrupt number for QSPI */
#define MCFINT_PIT1         4           /* Interrupt number for PIT1 (PIT0 in processor) */

/*
 *  SDRAM configuration registers.
 */
#define MCFSIM_SDMR         0xFC0a8000	/* SDRAM Mode/Extended Mode Register */
#define MCFSIM_SDCR         0xFC0a8004	/* SDRAM Control Register */
#define MCFSIM_SDCFG1       0xFC0a8008	/* SDRAM Configuration Register 1 */
#define MCFSIM_SDCFG2       0xFC0a800c	/* SDRAM Configuration Register 2 */
#define MCFSIM_SDCS0        0xFC0a8110	/* SDRAM Chip Select 0 Configuration */
#define MCFSIM_SDCS1        0xFC0a8114	/* SDRAM Chip Select 1 Configuration */

/*
 * EPORT and GPIO registers.
 */
#define MCFEPORT_EPPAR			0xFC088000
#define MCFEPORT_EPDDR			0xFC088002
#define MCFEPORT_EPIER			0xFC088003
#define MCFEPORT_EPDR			0xFC088004
#define MCFEPORT_EPPDR			0xFC088005
#define MCFEPORT_EPFR			0xFC088006

#define MCFGPIO_PODR_BUSCTL		0xFC0A4000
#define MCFGPIO_PODR_BE			0xFC0A4001
#define MCFGPIO_PODR_CS			0xFC0A4002
#define MCFGPIO_PODR_FECI2C		0xFC0A4003
#define MCFGPIO_PODR_QSPI		0xFC0A4004
#define MCFGPIO_PODR_TIMER		0xFC0A4005
#define MCFGPIO_PODR_UART		0xFC0A4006
#define MCFGPIO_PODR_FECH		0xFC0A4007
#define MCFGPIO_PODR_FECL		0xFC0A4008

#define MCFGPIO_PDDR_BUSCTL		0xFC0A400C
#define MCFGPIO_PDDR_BE			0xFC0A400D
#define MCFGPIO_PDDR_CS			0xFC0A400E
#define MCFGPIO_PDDR_FECI2C		0xFC0A400F
#define MCFGPIO_PDDR_QSPI		0xFC0A4010
#define MCFGPIO_PDDR_TIMER		0xFC0A4011
#define MCFGPIO_PDDR_UART		0xFC0A4012
#define MCFGPIO_PDDR_FECH		0xFC0A4013
#define MCFGPIO_PDDR_FECL		0xFC0A4014

#define MCFGPIO_PPDSDR_CS		0xFC0A401A
#define MCFGPIO_PPDSDR_FECI2C		0xFC0A401B
#define MCFGPIO_PPDSDR_QSPI		0xFC0A401C
#define MCFGPIO_PPDSDR_TIMER		0xFC0A401D
#define MCFGPIO_PPDSDR_UART		0xFC0A401E
#define MCFGPIO_PPDSDR_FECH		0xFC0A401F
#define MCFGPIO_PPDSDR_FECL		0xFC0A4020

#define MCFGPIO_PCLRR_BUSCTL		0xFC0A4024
#define MCFGPIO_PCLRR_BE		0xFC0A4025
#define MCFGPIO_PCLRR_CS		0xFC0A4026
#define MCFGPIO_PCLRR_FECI2C		0xFC0A4027
#define MCFGPIO_PCLRR_QSPI		0xFC0A4028
#define MCFGPIO_PCLRR_TIMER		0xFC0A4029
#define MCFGPIO_PCLRR_UART		0xFC0A402A
#define MCFGPIO_PCLRR_FECH		0xFC0A402B
#define MCFGPIO_PCLRR_FECL		0xFC0A402C

/*
 * Generic GPIO support
 */
#define MCFGPIO_PODR			MCFGPIO_PODR_CS
#define MCFGPIO_PDDR			MCFGPIO_PDDR_CS
#define MCFGPIO_PPDR			MCFGPIO_PPDSDR_CS
#define MCFGPIO_SETR			MCFGPIO_PPDSDR_CS
#define MCFGPIO_CLRR			MCFGPIO_PCLRR_CS

#define MCFGPIO_PIN_MAX			80
#define MCFGPIO_IRQ_MAX			8
#define MCFGPIO_IRQ_VECBASE		MCFINT_VECBASE

#define MCF_GPIO_PAR_UART		0xFC0A4036
#define MCF_GPIO_PAR_FECI2C		0xFC0A4033
#define MCF_GPIO_PAR_QSPI		0xFC0A4034
#define MCF_GPIO_PAR_FEC		0xFC0A4038

#define MCF_GPIO_PAR_UART_PAR_URXD0         (0x0001)
#define MCF_GPIO_PAR_UART_PAR_UTXD0         (0x0002)

#define MCF_GPIO_PAR_UART_PAR_URXD1         (0x0040)
#define MCF_GPIO_PAR_UART_PAR_UTXD1         (0x0080)

#define MCF_GPIO_PAR_FECI2C_PAR_SDA_URXD2   (0x02)
#define MCF_GPIO_PAR_FECI2C_PAR_SCL_UTXD2   (0x04)

/*
 *  PIT timer module.
 */
#define	MCFPIT_BASE1		0xFC080000	/* Base address of TIMER1 */
#define	MCFPIT_BASE2		0xFC084000	/* Base address of TIMER2 */

/*
 *  UART module.
 */
#define MCFUART_BASE1		0xFC060000	/* Base address of UART1 */
#define MCFUART_BASE2		0xFC064000	/* Base address of UART2 */
#define MCFUART_BASE3		0xFC068000	/* Base address of UART2 */

/*
 *  FEC module.
 */
#define	MCFFEC_BASE		0xFC030000	/* Base of FEC ethernet */
#define	MCFFEC_SIZE		0x800		/* Register set size */

/*
 *  Reset Control Unit.
 */
#define	MCF_RCR			0xFC0A0000
#define	MCF_RSR			0xFC0A0001

#define	MCF_RCR_SWRESET		0x80		/* Software reset bit */
#define	MCF_RCR_FRCSTOUT	0x40		/* Force external reset */

/****************************************************************************/
#endif  /* m520xsim_h */
