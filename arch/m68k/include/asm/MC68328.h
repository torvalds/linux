
/* include/asm-m68knommu/MC68328.h: '328 control registers
 *
 * Copyright (C) 1999  Vladimir Gurevich <vgurevic@cisco.com>
 *                     Bear & Hare Software, Inc.
 *
 * Based on include/asm-m68knommu/MC68332.h
 * Copyright (C) 1998  Kenneth Albanowski <kjahds@kjahds.com>,
 *
 */

#ifndef _MC68328_H_
#define _MC68328_H_

#define BYTE_REF(addr) (*((volatile unsigned char*)addr))
#define WORD_REF(addr) (*((volatile unsigned short*)addr))
#define LONG_REF(addr) (*((volatile unsigned long*)addr))

#define PUT_FIELD(field, val) (((val) << field##_SHIFT) & field##_MASK)
#define GET_FIELD(reg, field) (((reg) & field##_MASK) >> field##_SHIFT)

/********** 
 *
 * 0xFFFFF0xx -- System Control
 *
 **********/
 
/*
 * System Control Register (SCR)
 */
#define SCR_ADDR	0xfffff000
#define SCR		BYTE_REF(SCR_ADDR)

#define SCR_WDTH8	0x01	/* 8-Bit Width Select */
#define SCR_DMAP	0x04	/* Double Map */
#define SCR_SO		0x08	/* Supervisor Only */
#define SCR_BETEN	0x10	/* Bus-Error Time-Out Enable */
#define SCR_PRV		0x20	/* Privilege Violation */
#define SCR_WPV		0x40	/* Write Protect Violation */
#define SCR_BETO	0x80	/* Bus-Error TimeOut */

/*
 * Mask Revision Register
 */
#define MRR_ADDR 0xfffff004
#define MRR      LONG_REF(MRR_ADDR)
 
/********** 
 *
 * 0xFFFFF1xx -- Chip-Select logic
 *
 **********/

/********** 
 *
 * 0xFFFFF2xx -- Phase Locked Loop (PLL) & Power Control
 *
 **********/

/*
 * Group Base Address Registers
 */
#define GRPBASEA_ADDR	0xfffff100
#define GRPBASEB_ADDR	0xfffff102
#define GRPBASEC_ADDR	0xfffff104
#define GRPBASED_ADDR	0xfffff106

#define GRPBASEA	WORD_REF(GRPBASEA_ADDR)
#define GRPBASEB	WORD_REF(GRPBASEB_ADDR)
#define GRPBASEC	WORD_REF(GRPBASEC_ADDR)
#define GRPBASED	WORD_REF(GRPBASED_ADDR)

#define GRPBASE_V	  0x0001	/* Valid */
#define GRPBASE_GBA_MASK  0xfff0	/* Group Base Address (bits 31-20) */

/*
 * Group Base Address Mask Registers 
 */
#define GRPMASKA_ADDR	0xfffff108
#define GRPMASKB_ADDR	0xfffff10a
#define GRPMASKC_ADDR	0xfffff10c
#define GRPMASKD_ADDR	0xfffff10e

#define GRPMASKA	WORD_REF(GRPMASKA_ADDR)
#define GRPMASKB	WORD_REF(GRPMASKB_ADDR)
#define GRPMASKC	WORD_REF(GRPMASKC_ADDR)
#define GRPMASKD	WORD_REF(GRPMASKD_ADDR)

#define GRMMASK_GMA_MASK 0xfffff0	/* Group Base Mask (bits 31-20) */

/*
 * Chip-Select Option Registers (group A)
 */
#define CSA0_ADDR	0xfffff110
#define CSA1_ADDR	0xfffff114
#define CSA2_ADDR	0xfffff118
#define CSA3_ADDR	0xfffff11c

#define CSA0		LONG_REF(CSA0_ADDR)
#define CSA1		LONG_REF(CSA1_ADDR)
#define CSA2		LONG_REF(CSA2_ADDR)
#define CSA3		LONG_REF(CSA3_ADDR)

#define CSA_WAIT_MASK	0x00000007	/* Wait State Selection */
#define CSA_WAIT_SHIFT	0
#define CSA_RO		0x00000008	/* Read-Only */
#define CSA_AM_MASK	0x0000ff00	/* Address Mask (bits 23-16) */
#define CSA_AM_SHIFT	8
#define CSA_BUSW	0x00010000	/* Bus Width Select */
#define CSA_AC_MASK	0xff000000	/* Address Compare (bits 23-16) */
#define CSA_AC_SHIFT	24

/*
 * Chip-Select Option Registers (group B)
 */
#define CSB0_ADDR	0xfffff120
#define CSB1_ADDR	0xfffff124
#define CSB2_ADDR	0xfffff128
#define CSB3_ADDR	0xfffff12c

#define CSB0		LONG_REF(CSB0_ADDR)
#define CSB1		LONG_REF(CSB1_ADDR)
#define CSB2		LONG_REF(CSB2_ADDR)
#define CSB3		LONG_REF(CSB3_ADDR)

#define CSB_WAIT_MASK	0x00000007	/* Wait State Selection */
#define CSB_WAIT_SHIFT	0
#define CSB_RO		0x00000008	/* Read-Only */
#define CSB_AM_MASK	0x0000ff00	/* Address Mask (bits 23-16) */
#define CSB_AM_SHIFT	8
#define CSB_BUSW	0x00010000	/* Bus Width Select */
#define CSB_AC_MASK	0xff000000	/* Address Compare (bits 23-16) */
#define CSB_AC_SHIFT	24

/*
 * Chip-Select Option Registers (group C)
 */
#define CSC0_ADDR	0xfffff130
#define CSC1_ADDR	0xfffff134
#define CSC2_ADDR	0xfffff138
#define CSC3_ADDR	0xfffff13c

#define CSC0		LONG_REF(CSC0_ADDR)
#define CSC1		LONG_REF(CSC1_ADDR)
#define CSC2		LONG_REF(CSC2_ADDR)
#define CSC3		LONG_REF(CSC3_ADDR)

#define CSC_WAIT_MASK	0x00000007	/* Wait State Selection */
#define CSC_WAIT_SHIFT	0
#define CSC_RO		0x00000008	/* Read-Only */
#define CSC_AM_MASK	0x0000fff0	/* Address Mask (bits 23-12) */
#define CSC_AM_SHIFT	4
#define CSC_BUSW	0x00010000	/* Bus Width Select */
#define CSC_AC_MASK	0xfff00000	/* Address Compare (bits 23-12) */
#define CSC_AC_SHIFT	20

/*
 * Chip-Select Option Registers (group D)
 */
#define CSD0_ADDR	0xfffff140
#define CSD1_ADDR	0xfffff144
#define CSD2_ADDR	0xfffff148
#define CSD3_ADDR	0xfffff14c

#define CSD0		LONG_REF(CSD0_ADDR)
#define CSD1		LONG_REF(CSD1_ADDR)
#define CSD2		LONG_REF(CSD2_ADDR)
#define CSD3		LONG_REF(CSD3_ADDR)

#define CSD_WAIT_MASK	0x00000007	/* Wait State Selection */
#define CSD_WAIT_SHIFT	0
#define CSD_RO		0x00000008	/* Read-Only */
#define CSD_AM_MASK	0x0000fff0	/* Address Mask (bits 23-12) */
#define CSD_AM_SHIFT	4
#define CSD_BUSW	0x00010000	/* Bus Width Select */
#define CSD_AC_MASK	0xfff00000	/* Address Compare (bits 23-12) */
#define CSD_AC_SHIFT	20

/**********
 *
 * 0xFFFFF2xx -- Phase Locked Loop (PLL) & Power Control
 *
 **********/
 
/*
 * PLL Control Register 
 */
#define PLLCR_ADDR	0xfffff200
#define PLLCR		WORD_REF(PLLCR_ADDR)

#define PLLCR_DISPLL	       0x0008	/* Disable PLL */
#define PLLCR_CLKEN	       0x0010	/* Clock (CLKO pin) enable */
#define PLLCR_SYSCLK_SEL_MASK  0x0700	/* System Clock Selection */
#define PLLCR_SYSCLK_SEL_SHIFT 8
#define PLLCR_PIXCLK_SEL_MASK  0x3800	/* LCD Clock Selection */
#define PLLCR_PIXCLK_SEL_SHIFT 11

/* 'EZ328-compatible definitions */
#define PLLCR_LCDCLK_SEL_MASK	PLLCR_PIXCLK_SEL_MASK
#define PLLCR_LCDCLK_SEL_SHIFT	PLLCR_PIXCLK_SEL_SHIFT

/*
 * PLL Frequency Select Register
 */
#define PLLFSR_ADDR	0xfffff202
#define PLLFSR		WORD_REF(PLLFSR_ADDR)

#define PLLFSR_PC_MASK	0x00ff		/* P Count */
#define PLLFSR_PC_SHIFT 0
#define PLLFSR_QC_MASK	0x0f00		/* Q Count */
#define PLLFSR_QC_SHIFT 8
#define PLLFSR_PROT	0x4000		/* Protect P & Q */
#define PLLFSR_CLK32	0x8000		/* Clock 32 (kHz) */

/*
 * Power Control Register
 */
#define PCTRL_ADDR	0xfffff207
#define PCTRL		BYTE_REF(PCTRL_ADDR)

#define PCTRL_WIDTH_MASK	0x1f	/* CPU Clock bursts width */
#define PCTRL_WIDTH_SHIFT	0
#define PCTRL_STOP		0x40	/* Enter power-save mode immediately */ 
#define PCTRL_PCEN		0x80	/* Power Control Enable */

/**********
 *
 * 0xFFFFF3xx -- Interrupt Controller
 *
 **********/

/* 
 * Interrupt Vector Register
 */
#define IVR_ADDR	0xfffff300
#define IVR		BYTE_REF(IVR_ADDR)

#define IVR_VECTOR_MASK 0xF8

/*
 * Interrupt control Register
 */
#define ICR_ADRR	0xfffff302
#define ICR		WORD_REF(ICR_ADDR)

#define ICR_ET6		0x0100	/* Edge Trigger Select for IRQ6 */
#define ICR_ET3		0x0200	/* Edge Trigger Select for IRQ3 */
#define ICR_ET2		0x0400	/* Edge Trigger Select for IRQ2 */
#define ICR_ET1		0x0800	/* Edge Trigger Select for IRQ1 */
#define ICR_POL6	0x1000	/* Polarity Control for IRQ6 */
#define ICR_POL3	0x2000	/* Polarity Control for IRQ3 */
#define ICR_POL2	0x4000	/* Polarity Control for IRQ2 */
#define ICR_POL1	0x8000	/* Polarity Control for IRQ1 */

/*
 * Interrupt Mask Register
 */
#define IMR_ADDR	0xfffff304
#define IMR		LONG_REF(IMR_ADDR)
 
/*
 * Define the names for bit positions first. This is useful for
 * request_irq
 */
#define SPIM_IRQ_NUM	0	/* SPI Master interrupt */
#define	TMR2_IRQ_NUM	1	/* Timer 2 interrupt */
#define UART_IRQ_NUM	2	/* UART interrupt */	
#define	WDT_IRQ_NUM	3	/* Watchdog Timer interrupt */
#define RTC_IRQ_NUM	4	/* RTC interrupt */
#define	KB_IRQ_NUM	6	/* Keyboard Interrupt */
#define PWM_IRQ_NUM	7	/* Pulse-Width Modulator int. */
#define	INT0_IRQ_NUM	8	/* External INT0 */
#define	INT1_IRQ_NUM	9	/* External INT1 */
#define	INT2_IRQ_NUM	10	/* External INT2 */
#define	INT3_IRQ_NUM	11	/* External INT3 */
#define	INT4_IRQ_NUM	12	/* External INT4 */
#define	INT5_IRQ_NUM	13	/* External INT5 */
#define	INT6_IRQ_NUM	14	/* External INT6 */
#define	INT7_IRQ_NUM	15	/* External INT7 */
#define IRQ1_IRQ_NUM	16	/* IRQ1 */
#define IRQ2_IRQ_NUM	17	/* IRQ2 */
#define IRQ3_IRQ_NUM	18	/* IRQ3 */
#define IRQ6_IRQ_NUM	19	/* IRQ6 */
#define PEN_IRQ_NUM	20	/* Pen Interrupt */
#define SPIS_IRQ_NUM	21	/* SPI Slave Interrupt */
#define TMR1_IRQ_NUM	22	/* Timer 1 interrupt */
#define IRQ7_IRQ_NUM	23	/* IRQ7 */

/* '328-compatible definitions */
#define SPI_IRQ_NUM	SPIM_IRQ_NUM
#define TMR_IRQ_NUM	TMR1_IRQ_NUM
 
/*
 * Here go the bitmasks themselves
 */
#define IMR_MSPIM 	(1 << SPIM_IRQ_NUM)	/* Mask SPI Master interrupt */
#define	IMR_MTMR2	(1 << TMR2_IRQ_NUM)	/* Mask Timer 2 interrupt */
#define IMR_MUART	(1 << UART_IRQ_NUM)	/* Mask UART interrupt */	
#define	IMR_MWDT	(1 << WDT_IRQ_NUM)	/* Mask Watchdog Timer interrupt */
#define IMR_MRTC	(1 << RTC_IRQ_NUM)	/* Mask RTC interrupt */
#define	IMR_MKB		(1 << KB_IRQ_NUM)	/* Mask Keyboard Interrupt */
#define IMR_MPWM	(1 << PWM_IRQ_NUM)	/* Mask Pulse-Width Modulator int. */
#define	IMR_MINT0	(1 << INT0_IRQ_NUM)	/* Mask External INT0 */
#define	IMR_MINT1	(1 << INT1_IRQ_NUM)	/* Mask External INT1 */
#define	IMR_MINT2	(1 << INT2_IRQ_NUM)	/* Mask External INT2 */
#define	IMR_MINT3	(1 << INT3_IRQ_NUM)	/* Mask External INT3 */
#define	IMR_MINT4	(1 << INT4_IRQ_NUM)	/* Mask External INT4 */
#define	IMR_MINT5	(1 << INT5_IRQ_NUM)	/* Mask External INT5 */
#define	IMR_MINT6	(1 << INT6_IRQ_NUM)	/* Mask External INT6 */
#define	IMR_MINT7	(1 << INT7_IRQ_NUM)	/* Mask External INT7 */
#define IMR_MIRQ1	(1 << IRQ1_IRQ_NUM)	/* Mask IRQ1 */
#define IMR_MIRQ2	(1 << IRQ2_IRQ_NUM)	/* Mask IRQ2 */
#define IMR_MIRQ3	(1 << IRQ3_IRQ_NUM)	/* Mask IRQ3 */
#define IMR_MIRQ6	(1 << IRQ6_IRQ_NUM)	/* Mask IRQ6 */
#define IMR_MPEN	(1 << PEN_IRQ_NUM)	/* Mask Pen Interrupt */
#define IMR_MSPIS	(1 << SPIS_IRQ_NUM)	/* Mask SPI Slave Interrupt */
#define IMR_MTMR1	(1 << TMR1_IRQ_NUM)	/* Mask Timer 1 interrupt */
#define IMR_MIRQ7	(1 << IRQ7_IRQ_NUM)	/* Mask IRQ7 */

/* 'EZ328-compatible definitions */
#define IMR_MSPI	IMR_MSPIM
#define IMR_MTMR	IMR_MTMR1

/* 
 * Interrupt Wake-Up Enable Register
 */
#define IWR_ADDR	0xfffff308
#define IWR		LONG_REF(IWR_ADDR)

#define IWR_SPIM 	(1 << SPIM_IRQ_NUM)	/* SPI Master interrupt */
#define	IWR_TMR2	(1 << TMR2_IRQ_NUM)	/* Timer 2 interrupt */
#define IWR_UART	(1 << UART_IRQ_NUM)	/* UART interrupt */	
#define	IWR_WDT		(1 << WDT_IRQ_NUM)	/* Watchdog Timer interrupt */
#define IWR_RTC		(1 << RTC_IRQ_NUM)	/* RTC interrupt */
#define	IWR_KB		(1 << KB_IRQ_NUM)	/* Keyboard Interrupt */
#define IWR_PWM		(1 << PWM_IRQ_NUM)	/* Pulse-Width Modulator int. */
#define	IWR_INT0	(1 << INT0_IRQ_NUM)	/* External INT0 */
#define	IWR_INT1	(1 << INT1_IRQ_NUM)	/* External INT1 */
#define	IWR_INT2	(1 << INT2_IRQ_NUM)	/* External INT2 */
#define	IWR_INT3	(1 << INT3_IRQ_NUM)	/* External INT3 */
#define	IWR_INT4	(1 << INT4_IRQ_NUM)	/* External INT4 */
#define	IWR_INT5	(1 << INT5_IRQ_NUM)	/* External INT5 */
#define	IWR_INT6	(1 << INT6_IRQ_NUM)	/* External INT6 */
#define	IWR_INT7	(1 << INT7_IRQ_NUM)	/* External INT7 */
#define IWR_IRQ1	(1 << IRQ1_IRQ_NUM)	/* IRQ1 */
#define IWR_IRQ2	(1 << IRQ2_IRQ_NUM)	/* IRQ2 */
#define IWR_IRQ3	(1 << IRQ3_IRQ_NUM)	/* IRQ3 */
#define IWR_IRQ6	(1 << IRQ6_IRQ_NUM)	/* IRQ6 */
#define IWR_PEN		(1 << PEN_IRQ_NUM)	/* Pen Interrupt */
#define IWR_SPIS	(1 << SPIS_IRQ_NUM)	/* SPI Slave Interrupt */
#define IWR_TMR1	(1 << TMR1_IRQ_NUM)	/* Timer 1 interrupt */
#define IWR_IRQ7	(1 << IRQ7_IRQ_NUM)	/* IRQ7 */

/* 
 * Interrupt Status Register 
 */
#define ISR_ADDR	0xfffff30c
#define ISR		LONG_REF(ISR_ADDR)

#define ISR_SPIM 	(1 << SPIM_IRQ_NUM)	/* SPI Master interrupt */
#define	ISR_TMR2	(1 << TMR2_IRQ_NUM)	/* Timer 2 interrupt */
#define ISR_UART	(1 << UART_IRQ_NUM)	/* UART interrupt */	
#define	ISR_WDT		(1 << WDT_IRQ_NUM)	/* Watchdog Timer interrupt */
#define ISR_RTC		(1 << RTC_IRQ_NUM)	/* RTC interrupt */
#define	ISR_KB		(1 << KB_IRQ_NUM)	/* Keyboard Interrupt */
#define ISR_PWM		(1 << PWM_IRQ_NUM)	/* Pulse-Width Modulator int. */
#define	ISR_INT0	(1 << INT0_IRQ_NUM)	/* External INT0 */
#define	ISR_INT1	(1 << INT1_IRQ_NUM)	/* External INT1 */
#define	ISR_INT2	(1 << INT2_IRQ_NUM)	/* External INT2 */
#define	ISR_INT3	(1 << INT3_IRQ_NUM)	/* External INT3 */
#define	ISR_INT4	(1 << INT4_IRQ_NUM)	/* External INT4 */
#define	ISR_INT5	(1 << INT5_IRQ_NUM)	/* External INT5 */
#define	ISR_INT6	(1 << INT6_IRQ_NUM)	/* External INT6 */
#define	ISR_INT7	(1 << INT7_IRQ_NUM)	/* External INT7 */
#define ISR_IRQ1	(1 << IRQ1_IRQ_NUM)	/* IRQ1 */
#define ISR_IRQ2	(1 << IRQ2_IRQ_NUM)	/* IRQ2 */
#define ISR_IRQ3	(1 << IRQ3_IRQ_NUM)	/* IRQ3 */
#define ISR_IRQ6	(1 << IRQ6_IRQ_NUM)	/* IRQ6 */
#define ISR_PEN		(1 << PEN_IRQ_NUM)	/* Pen Interrupt */
#define ISR_SPIS	(1 << SPIS_IRQ_NUM)	/* SPI Slave Interrupt */
#define ISR_TMR1	(1 << TMR1_IRQ_NUM)	/* Timer 1 interrupt */
#define ISR_IRQ7	(1 << IRQ7_IRQ_NUM)	/* IRQ7 */

/* 'EZ328-compatible definitions */
#define ISR_SPI	ISR_SPIM
#define ISR_TMR	ISR_TMR1

/* 
 * Interrupt Pending Register 
 */
#define IPR_ADDR	0xfffff310
#define IPR		LONG_REF(IPR_ADDR)

#define IPR_SPIM 	(1 << SPIM_IRQ_NUM)	/* SPI Master interrupt */
#define	IPR_TMR2	(1 << TMR2_IRQ_NUM)	/* Timer 2 interrupt */
#define IPR_UART	(1 << UART_IRQ_NUM)	/* UART interrupt */	
#define	IPR_WDT		(1 << WDT_IRQ_NUM)	/* Watchdog Timer interrupt */
#define IPR_RTC		(1 << RTC_IRQ_NUM)	/* RTC interrupt */
#define	IPR_KB		(1 << KB_IRQ_NUM)	/* Keyboard Interrupt */
#define IPR_PWM		(1 << PWM_IRQ_NUM)	/* Pulse-Width Modulator int. */
#define	IPR_INT0	(1 << INT0_IRQ_NUM)	/* External INT0 */
#define	IPR_INT1	(1 << INT1_IRQ_NUM)	/* External INT1 */
#define	IPR_INT2	(1 << INT2_IRQ_NUM)	/* External INT2 */
#define	IPR_INT3	(1 << INT3_IRQ_NUM)	/* External INT3 */
#define	IPR_INT4	(1 << INT4_IRQ_NUM)	/* External INT4 */
#define	IPR_INT5	(1 << INT5_IRQ_NUM)	/* External INT5 */
#define	IPR_INT6	(1 << INT6_IRQ_NUM)	/* External INT6 */
#define	IPR_INT7	(1 << INT7_IRQ_NUM)	/* External INT7 */
#define IPR_IRQ1	(1 << IRQ1_IRQ_NUM)	/* IRQ1 */
#define IPR_IRQ2	(1 << IRQ2_IRQ_NUM)	/* IRQ2 */
#define IPR_IRQ3	(1 << IRQ3_IRQ_NUM)	/* IRQ3 */
#define IPR_IRQ6	(1 << IRQ6_IRQ_NUM)	/* IRQ6 */
#define IPR_PEN		(1 << PEN_IRQ_NUM)	/* Pen Interrupt */
#define IPR_SPIS	(1 << SPIS_IRQ_NUM)	/* SPI Slave Interrupt */
#define IPR_TMR1	(1 << TMR1_IRQ_NUM)	/* Timer 1 interrupt */
#define IPR_IRQ7	(1 << IRQ7_IRQ_NUM)	/* IRQ7 */

/* 'EZ328-compatible definitions */
#define IPR_SPI	IPR_SPIM
#define IPR_TMR	IPR_TMR1

/**********
 *
 * 0xFFFFF4xx -- Parallel Ports
 *
 **********/

/*
 * Port A
 */
#define PADIR_ADDR	0xfffff400		/* Port A direction reg */
#define PADATA_ADDR	0xfffff401		/* Port A data register */
#define PASEL_ADDR	0xfffff403		/* Port A Select register */

#define PADIR		BYTE_REF(PADIR_ADDR)
#define PADATA		BYTE_REF(PADATA_ADDR)
#define PASEL		BYTE_REF(PASEL_ADDR)

#define PA(x)           (1 << (x))
#define PA_A(x)		PA((x) - 16)	/* This is specific to PA only! */

#define PA_A16		PA(0)		/* Use A16 as PA(0) */
#define PA_A17		PA(1)		/* Use A17 as PA(1) */
#define PA_A18		PA(2)		/* Use A18 as PA(2) */
#define PA_A19		PA(3)		/* Use A19 as PA(3) */
#define PA_A20		PA(4)		/* Use A20 as PA(4) */
#define PA_A21		PA(5)		/* Use A21 as PA(5) */
#define PA_A22		PA(6)		/* Use A22 as PA(6) */
#define PA_A23		PA(7)		/* Use A23 as PA(7) */

/* 
 * Port B
 */
#define PBDIR_ADDR	0xfffff408		/* Port B direction reg */
#define PBDATA_ADDR	0xfffff409		/* Port B data register */
#define PBSEL_ADDR	0xfffff40b		/* Port B Select Register */

#define PBDIR		BYTE_REF(PBDIR_ADDR)
#define PBDATA		BYTE_REF(PBDATA_ADDR)
#define PBSEL		BYTE_REF(PBSEL_ADDR)

#define PB(x)           (1 << (x))
#define PB_D(x)		PB(x)		/* This is specific to port B only */

#define PB_D0		PB(0)		/* Use D0 as PB(0) */
#define PB_D1		PB(1)		/* Use D1 as PB(1) */
#define PB_D2		PB(2)		/* Use D2 as PB(2) */
#define PB_D3		PB(3)		/* Use D3 as PB(3) */
#define PB_D4		PB(4)		/* Use D4 as PB(4) */
#define PB_D5		PB(5)		/* Use D5 as PB(5) */
#define PB_D6		PB(6)		/* Use D6 as PB(6) */
#define PB_D7		PB(7)		/* Use D7 as PB(7) */

/* 
 * Port C
 */
#define PCDIR_ADDR	0xfffff410		/* Port C direction reg */
#define PCDATA_ADDR	0xfffff411		/* Port C data register */
#define PCSEL_ADDR	0xfffff413		/* Port C Select Register */

#define PCDIR		BYTE_REF(PCDIR_ADDR)
#define PCDATA		BYTE_REF(PCDATA_ADDR)
#define PCSEL		BYTE_REF(PCSEL_ADDR)

#define PC(x)           (1 << (x))

#define PC_WE		PC(6)		/* Use WE    as PC(6) */
#define PC_DTACK	PC(5)		/* Use DTACK as PC(5) */
#define PC_IRQ7		PC(4)		/* Use IRQ7  as PC(4) */
#define PC_LDS		PC(2)		/* Use LDS   as PC(2) */
#define PC_UDS		PC(1)		/* Use UDS   as PC(1) */
#define PC_MOCLK	PC(0)		/* Use MOCLK as PC(0) */

/* 
 * Port D
 */
#define PDDIR_ADDR	0xfffff418		/* Port D direction reg */
#define PDDATA_ADDR	0xfffff419		/* Port D data register */
#define PDPUEN_ADDR	0xfffff41a		/* Port D Pull-Up enable reg */
#define PDPOL_ADDR	0xfffff41c		/* Port D Polarity Register */
#define PDIRQEN_ADDR	0xfffff41d		/* Port D IRQ enable register */
#define	PDIQEG_ADDR	0xfffff41f		/* Port D IRQ Edge Register */

#define PDDIR		BYTE_REF(PDDIR_ADDR)
#define PDDATA		BYTE_REF(PDDATA_ADDR)
#define PDPUEN		BYTE_REF(PDPUEN_ADDR)
#define	PDPOL		BYTE_REF(PDPOL_ADDR)
#define PDIRQEN		BYTE_REF(PDIRQEN_ADDR)
#define PDIQEG		BYTE_REF(PDIQEG_ADDR)

#define PD(x)           (1 << (x))
#define PD_KB(x)	PD(x)		/* This is specific for Port D only */

#define PD_KB0		PD(0)	/* Use KB0 as PD(0) */
#define PD_KB1		PD(1)	/* Use KB1 as PD(1) */
#define PD_KB2		PD(2)	/* Use KB2 as PD(2) */
#define PD_KB3		PD(3)	/* Use KB3 as PD(3) */
#define PD_KB4		PD(4)	/* Use KB4 as PD(4) */
#define PD_KB5		PD(5)	/* Use KB5 as PD(5) */
#define PD_KB6		PD(6)	/* Use KB6 as PD(6) */
#define PD_KB7		PD(7)	/* Use KB7 as PD(7) */

/* 
 * Port E
 */
#define PEDIR_ADDR	0xfffff420		/* Port E direction reg */
#define PEDATA_ADDR	0xfffff421		/* Port E data register */
#define PEPUEN_ADDR	0xfffff422		/* Port E Pull-Up enable reg */
#define PESEL_ADDR	0xfffff423		/* Port E Select Register */

#define PEDIR		BYTE_REF(PEDIR_ADDR)
#define PEDATA		BYTE_REF(PEDATA_ADDR)
#define PEPUEN		BYTE_REF(PEPUEN_ADDR)
#define PESEL		BYTE_REF(PESEL_ADDR)

#define PE(x)           (1 << (x))

#define PE_CSA1		PE(1)	/* Use CSA1 as PE(1) */
#define PE_CSA2		PE(2)	/* Use CSA2 as PE(2) */
#define PE_CSA3		PE(3)	/* Use CSA3 as PE(3) */
#define PE_CSB0		PE(4)	/* Use CSB0 as PE(4) */
#define PE_CSB1		PE(5)	/* Use CSB1 as PE(5) */
#define PE_CSB2		PE(6)	/* Use CSB2 as PE(6) */
#define PE_CSB3		PE(7)	/* Use CSB3 as PE(7) */

/* 
 * Port F
 */
#define PFDIR_ADDR	0xfffff428		/* Port F direction reg */
#define PFDATA_ADDR	0xfffff429		/* Port F data register */
#define PFPUEN_ADDR	0xfffff42a		/* Port F Pull-Up enable reg */
#define PFSEL_ADDR	0xfffff42b		/* Port F Select Register */

#define PFDIR		BYTE_REF(PFDIR_ADDR)
#define PFDATA		BYTE_REF(PFDATA_ADDR)
#define PFPUEN		BYTE_REF(PFPUEN_ADDR)
#define PFSEL		BYTE_REF(PFSEL_ADDR)

#define PF(x)           (1 << (x))
#define PF_A(x)		PF((x) - 24)	/* This is Port F specific only */

#define PF_A24		PF(0)	/* Use A24 as PF(0) */
#define PF_A25		PF(1)	/* Use A25 as PF(1) */
#define PF_A26		PF(2)	/* Use A26 as PF(2) */
#define PF_A27		PF(3)	/* Use A27 as PF(3) */
#define PF_A28		PF(4)	/* Use A28 as PF(4) */
#define PF_A29		PF(5)	/* Use A29 as PF(5) */
#define PF_A30		PF(6)	/* Use A30 as PF(6) */
#define PF_A31		PF(7)	/* Use A31 as PF(7) */

/* 
 * Port G
 */
#define PGDIR_ADDR	0xfffff430		/* Port G direction reg */
#define PGDATA_ADDR	0xfffff431		/* Port G data register */
#define PGPUEN_ADDR	0xfffff432		/* Port G Pull-Up enable reg */
#define PGSEL_ADDR	0xfffff433		/* Port G Select Register */

#define PGDIR		BYTE_REF(PGDIR_ADDR)
#define PGDATA		BYTE_REF(PGDATA_ADDR)
#define PGPUEN		BYTE_REF(PGPUEN_ADDR)
#define PGSEL		BYTE_REF(PGSEL_ADDR)

#define PG(x)           (1 << (x))

#define PG_UART_TXD	PG(0)	/* Use UART_TXD as PG(0) */
#define PG_UART_RXD	PG(1)	/* Use UART_RXD as PG(1) */
#define PG_PWMOUT	PG(2)	/* Use PWMOUT   as PG(2) */
#define PG_TOUT2	PG(3)   /* Use TOUT2    as PG(3) */
#define PG_TIN2		PG(4)	/* Use TIN2     as PG(4) */
#define PG_TOUT1	PG(5)   /* Use TOUT1    as PG(5) */
#define PG_TIN1		PG(6)	/* Use TIN1     as PG(6) */
#define PG_RTCOUT	PG(7)	/* Use RTCOUT   as PG(7) */

/* 
 * Port J
 */
#define PJDIR_ADDR	0xfffff438		/* Port J direction reg */
#define PJDATA_ADDR	0xfffff439		/* Port J data register */
#define PJSEL_ADDR	0xfffff43b		/* Port J Select Register */

#define PJDIR		BYTE_REF(PJDIR_ADDR)
#define PJDATA		BYTE_REF(PJDATA_ADDR)
#define PJSEL		BYTE_REF(PJSEL_ADDR)

#define PJ(x)           (1 << (x)) 

#define PJ_CSD3		PJ(7)	/* Use CSD3 as PJ(7) */

/* 
 * Port K
 */
#define PKDIR_ADDR	0xfffff440		/* Port K direction reg */
#define PKDATA_ADDR	0xfffff441		/* Port K data register */
#define PKPUEN_ADDR	0xfffff442		/* Port K Pull-Up enable reg */
#define PKSEL_ADDR	0xfffff443		/* Port K Select Register */

#define PKDIR		BYTE_REF(PKDIR_ADDR)
#define PKDATA		BYTE_REF(PKDATA_ADDR)
#define PKPUEN		BYTE_REF(PKPUEN_ADDR)
#define PKSEL		BYTE_REF(PKSEL_ADDR)

#define PK(x)           (1 << (x))

/* 
 * Port M
 */
#define PMDIR_ADDR	0xfffff438		/* Port M direction reg */
#define PMDATA_ADDR	0xfffff439		/* Port M data register */
#define PMPUEN_ADDR	0xfffff43a		/* Port M Pull-Up enable reg */
#define PMSEL_ADDR	0xfffff43b		/* Port M Select Register */

#define PMDIR		BYTE_REF(PMDIR_ADDR)
#define PMDATA		BYTE_REF(PMDATA_ADDR)
#define PMPUEN		BYTE_REF(PMPUEN_ADDR)
#define PMSEL		BYTE_REF(PMSEL_ADDR)

#define PM(x)           (1 << (x))

/**********
 *
 * 0xFFFFF5xx -- Pulse-Width Modulator (PWM)
 *
 **********/

/*
 * PWM Control Register 
 */
#define PWMC_ADDR	0xfffff500
#define PWMC		WORD_REF(PWMC_ADDR)

#define PWMC_CLKSEL_MASK	0x0007	/* Clock Selection */
#define PWMC_CLKSEL_SHIFT	0
#define PWMC_PWMEN		0x0010	/* Enable PWM */
#define PMNC_POL		0x0020	/* PWM Output Bit Polarity */
#define PWMC_PIN		0x0080	/* Current PWM output pin status */
#define PWMC_LOAD		0x0100	/* Force a new period */
#define PWMC_IRQEN		0x4000	/* Interrupt Request Enable */
#define PWMC_CLKSRC		0x8000	/* Clock Source Select */

/* 'EZ328-compatible definitions */
#define PWMC_EN	PWMC_PWMEN

/*
 * PWM Period Register
 */
#define PWMP_ADDR	0xfffff502
#define PWMP		WORD_REF(PWMP_ADDR)

/* 
 * PWM Width Register 
 */
#define PWMW_ADDR	0xfffff504
#define PWMW		WORD_REF(PWMW_ADDR)

/*
 * PWM Counter Register
 */
#define PWMCNT_ADDR	0xfffff506
#define PWMCNT		WORD_REF(PWMCNT_ADDR)

/**********
 *
 * 0xFFFFF6xx -- General-Purpose Timers
 *
 **********/

/* 
 * Timer Unit 1 and 2 Control Registers
 */
#define TCTL1_ADDR	0xfffff600
#define TCTL1		WORD_REF(TCTL1_ADDR)
#define TCTL2_ADDR	0xfffff60c
#define TCTL2		WORD_REF(TCTL2_ADDR)

#define	TCTL_TEN		0x0001	/* Timer Enable  */
#define TCTL_CLKSOURCE_MASK 	0x000e	/* Clock Source: */
#define   TCTL_CLKSOURCE_STOP	   0x0000	/* Stop count (disabled)    */
#define   TCTL_CLKSOURCE_SYSCLK	   0x0002	/* SYSCLK to prescaler      */
#define   TCTL_CLKSOURCE_SYSCLK_16 0x0004	/* SYSCLK/16 to prescaler   */
#define   TCTL_CLKSOURCE_TIN	   0x0006	/* TIN to prescaler         */
#define   TCTL_CLKSOURCE_32KHZ	   0x0008	/* 32kHz clock to prescaler */
#define TCTL_IRQEN		0x0010	/* IRQ Enable    */
#define TCTL_OM			0x0020	/* Output Mode   */
#define TCTL_CAP_MASK		0x00c0	/* Capture Edge: */
#define	  TCTL_CAP_RE		0x0040		/* Capture on rizing edge   */
#define   TCTL_CAP_FE		0x0080		/* Capture on falling edge  */
#define TCTL_FRR		0x0010	/* Free-Run Mode */

/* 'EZ328-compatible definitions */
#define TCTL_ADDR	TCTL1_ADDR
#define TCTL		TCTL1

/*
 * Timer Unit 1 and 2 Prescaler Registers
 */
#define TPRER1_ADDR	0xfffff602
#define TPRER1		WORD_REF(TPRER1_ADDR)
#define TPRER2_ADDR	0xfffff60e
#define TPRER2		WORD_REF(TPRER2_ADDR)

/* 'EZ328-compatible definitions */
#define TPRER_ADDR	TPRER1_ADDR
#define TPRER		TPRER1

/*
 * Timer Unit 1 and 2 Compare Registers
 */
#define TCMP1_ADDR	0xfffff604
#define TCMP1		WORD_REF(TCMP1_ADDR)
#define TCMP2_ADDR	0xfffff610
#define TCMP2		WORD_REF(TCMP2_ADDR)

/* 'EZ328-compatible definitions */
#define TCMP_ADDR	TCMP1_ADDR
#define TCMP		TCMP1

/*
 * Timer Unit 1 and 2 Capture Registers
 */
#define TCR1_ADDR	0xfffff606
#define TCR1		WORD_REF(TCR1_ADDR)
#define TCR2_ADDR	0xfffff612
#define TCR2		WORD_REF(TCR2_ADDR)

/* 'EZ328-compatible definitions */
#define TCR_ADDR	TCR1_ADDR
#define TCR		TCR1

/*
 * Timer Unit 1 and 2 Counter Registers
 */
#define TCN1_ADDR	0xfffff608
#define TCN1		WORD_REF(TCN1_ADDR)
#define TCN2_ADDR	0xfffff614
#define TCN2		WORD_REF(TCN2_ADDR)

/* 'EZ328-compatible definitions */
#define TCN_ADDR	TCN1_ADDR
#define TCN		TCN1

/*
 * Timer Unit 1 and 2 Status Registers
 */
#define TSTAT1_ADDR	0xfffff60a
#define TSTAT1		WORD_REF(TSTAT1_ADDR)
#define TSTAT2_ADDR	0xfffff616
#define TSTAT2		WORD_REF(TSTAT2_ADDR)

#define TSTAT_COMP	0x0001		/* Compare Event occurred */
#define TSTAT_CAPT	0x0001		/* Capture Event occurred */

/* 'EZ328-compatible definitions */
#define TSTAT_ADDR	TSTAT1_ADDR
#define TSTAT		TSTAT1

/*
 * Watchdog Compare Register 
 */
#define WRR_ADDR	0xfffff61a
#define WRR		WORD_REF(WRR_ADDR)

/*
 * Watchdog Counter Register 
 */
#define WCN_ADDR	0xfffff61c
#define WCN		WORD_REF(WCN_ADDR)

/*
 * Watchdog Control and Status Register
 */
#define WCSR_ADDR	0xfffff618
#define WCSR		WORD_REF(WCSR_ADDR)

#define WCSR_WDEN	0x0001	/* Watchdog Enable */
#define WCSR_FI		0x0002	/* Forced Interrupt (instead of SW reset)*/
#define WCSR_WRST	0x0004	/* Watchdog Reset */

/**********
 *
 * 0xFFFFF7xx -- Serial Peripheral Interface Slave (SPIS)
 *
 **********/

/*
 * SPI Slave Register
 */
#define SPISR_ADDR	0xfffff700
#define SPISR		WORD_REF(SPISR_ADDR)

#define SPISR_DATA_ADDR	0xfffff701
#define SPISR_DATA	BYTE_REF(SPISR_DATA_ADDR)

#define SPISR_DATA_MASK	 0x00ff	/* Shifted data from the external device */
#define SPISR_DATA_SHIFT 0
#define SPISR_SPISEN	 0x0100	/* SPIS module enable */
#define SPISR_POL	 0x0200	/* SPSCLK polarity control */
#define SPISR_PHA	 0x0400	/* Phase relationship between SPSCLK & SPSRxD */
#define SPISR_OVWR	 0x0800	/* Data buffer has been overwritten */
#define SPISR_DATARDY	 0x1000	/* Data ready */
#define SPISR_ENPOL	 0x2000	/* Enable Polarity */
#define SPISR_IRQEN	 0x4000	/* SPIS IRQ Enable */
#define SPISR_SPISIRQ	 0x8000	/* SPIS IRQ posted */

/**********
 *
 * 0xFFFFF8xx -- Serial Peripheral Interface Master (SPIM)
 *
 **********/

/*
 * SPIM Data Register
 */
#define SPIMDATA_ADDR	0xfffff800
#define SPIMDATA	WORD_REF(SPIMDATA_ADDR)

/*
 * SPIM Control/Status Register
 */
#define SPIMCONT_ADDR	0xfffff802
#define SPIMCONT	WORD_REF(SPIMCONT_ADDR)

#define SPIMCONT_BIT_COUNT_MASK	 0x000f	/* Transfer Length in Bytes */
#define SPIMCONT_BIT_COUNT_SHIFT 0
#define SPIMCONT_POL		 0x0010	/* SPMCLK Signel Polarity */
#define	SPIMCONT_PHA		 0x0020	/* Clock/Data phase relationship */
#define SPIMCONT_IRQEN		 0x0040 /* IRQ Enable */
#define SPIMCONT_SPIMIRQ	 0x0080	/* Interrupt Request */
#define SPIMCONT_XCH		 0x0100	/* Exchange */
#define SPIMCONT_RSPIMEN	 0x0200	/* Enable SPIM */
#define SPIMCONT_DATA_RATE_MASK	 0xe000	/* SPIM Data Rate */
#define SPIMCONT_DATA_RATE_SHIFT 13

/* 'EZ328-compatible definitions */
#define SPIMCONT_IRQ	SPIMCONT_SPIMIRQ
#define SPIMCONT_ENABLE	SPIMCONT_SPIMEN
/**********
 *
 * 0xFFFFF9xx -- UART
 *
 **********/

/*
 * UART Status/Control Register
 */
#define USTCNT_ADDR	0xfffff900
#define USTCNT		WORD_REF(USTCNT_ADDR)

#define USTCNT_TXAVAILEN	0x0001	/* Transmitter Available Int Enable */
#define USTCNT_TXHALFEN		0x0002	/* Transmitter Half Empty Int Enable */
#define USTCNT_TXEMPTYEN	0x0004	/* Transmitter Empty Int Enable */
#define USTCNT_RXREADYEN	0x0008	/* Receiver Ready Interrupt Enable */
#define USTCNT_RXHALFEN		0x0010	/* Receiver Half-Full Int Enable */
#define USTCNT_RXFULLEN		0x0020	/* Receiver Full Interrupt Enable */
#define USTCNT_CTSDELTAEN	0x0040	/* CTS Delta Interrupt Enable */
#define USTCNT_GPIODELTAEN	0x0080	/* Old Data Interrupt Enable */
#define USTCNT_8_7		0x0100	/* Eight or seven-bit transmission */
#define USTCNT_STOP		0x0200	/* Stop bit transmission */
#define USTCNT_ODD_EVEN		0x0400	/* Odd Parity */
#define	USTCNT_PARITYEN		0x0800	/* Parity Enable */
#define USTCNT_CLKMODE		0x1000	/* Clock Mode Select */
#define	USTCNT_TXEN		0x2000	/* Transmitter Enable */
#define USTCNT_RXEN		0x4000	/* Receiver Enable */
#define USTCNT_UARTEN		0x8000	/* UART Enable */

/* 'EZ328-compatible definitions */
#define USTCNT_TXAE	USTCNT_TXAVAILEN 
#define USTCNT_TXHE	USTCNT_TXHALFEN
#define USTCNT_TXEE	USTCNT_TXEMPTYEN
#define USTCNT_RXRE	USTCNT_RXREADYEN
#define USTCNT_RXHE	USTCNT_RXHALFEN
#define USTCNT_RXFE	USTCNT_RXFULLEN
#define USTCNT_CTSD	USTCNT_CTSDELTAEN
#define USTCNT_ODD	USTCNT_ODD_EVEN
#define USTCNT_PEN	USTCNT_PARITYEN
#define USTCNT_CLKM	USTCNT_CLKMODE
#define USTCNT_UEN	USTCNT_UARTEN

/*
 * UART Baud Control Register
 */
#define UBAUD_ADDR	0xfffff902
#define UBAUD		WORD_REF(UBAUD_ADDR)

#define UBAUD_PRESCALER_MASK	0x003f	/* Actual divisor is 65 - PRESCALER */
#define UBAUD_PRESCALER_SHIFT	0
#define UBAUD_DIVIDE_MASK	0x0700	/* Baud Rate freq. divisor */
#define UBAUD_DIVIDE_SHIFT	8
#define UBAUD_BAUD_SRC		0x0800	/* Baud Rate Source */
#define UBAUD_GPIOSRC		0x1000	/* GPIO source */
#define UBAUD_GPIODIR		0x2000	/* GPIO Direction */
#define UBAUD_GPIO		0x4000	/* Current GPIO pin status */
#define UBAUD_GPIODELTA		0x8000	/* GPIO pin value changed */

/*
 * UART Receiver Register 
 */
#define URX_ADDR	0xfffff904
#define URX		WORD_REF(URX_ADDR)

#define URX_RXDATA_ADDR	0xfffff905
#define URX_RXDATA	BYTE_REF(URX_RXDATA_ADDR)

#define URX_RXDATA_MASK	 0x00ff	/* Received data */
#define URX_RXDATA_SHIFT 0
#define URX_PARITY_ERROR 0x0100	/* Parity Error */
#define URX_BREAK	 0x0200	/* Break Detected */
#define URX_FRAME_ERROR	 0x0400	/* Framing Error */
#define URX_OVRUN	 0x0800	/* Serial Overrun */
#define URX_DATA_READY	 0x2000	/* Data Ready (FIFO not empty) */
#define URX_FIFO_HALF	 0x4000 /* FIFO is Half-Full */
#define URX_FIFO_FULL	 0x8000	/* FIFO is Full */

/*
 * UART Transmitter Register 
 */
#define UTX_ADDR	0xfffff906
#define UTX		WORD_REF(UTX_ADDR)

#define UTX_TXDATA_ADDR	0xfffff907
#define UTX_TXDATA	BYTE_REF(UTX_TXDATA_ADDR)

#define UTX_TXDATA_MASK	 0x00ff	/* Data to be transmitted */
#define UTX_TXDATA_SHIFT 0
#define UTX_CTS_DELTA	 0x0100	/* CTS changed */
#define UTX_CTS_STATUS	 0x0200	/* CTS State */
#define	UTX_IGNORE_CTS	 0x0800	/* Ignore CTS */
#define UTX_SEND_BREAK	 0x1000	/* Send a BREAK */
#define UTX_TX_AVAIL	 0x2000	/* Transmit FIFO has a slot available */
#define UTX_FIFO_HALF	 0x4000	/* Transmit FIFO is half empty */
#define UTX_FIFO_EMPTY	 0x8000	/* Transmit FIFO is empty */

/* 'EZ328-compatible definitions */
#define UTX_CTS_STAT	UTX_CTS_STATUS
#define UTX_NOCTS	UTX_IGNORE_CTS

/*
 * UART Miscellaneous Register 
 */
#define UMISC_ADDR	0xfffff908
#define UMISC		WORD_REF(UMISC_ADDR)

#define UMISC_TX_POL	 0x0004	/* Transmit Polarity */
#define UMISC_RX_POL	 0x0008	/* Receive Polarity */
#define UMISC_IRDA_LOOP	 0x0010	/* IrDA Loopback Enable */
#define UMISC_IRDA_EN	 0x0020	/* Infra-Red Enable */
#define UMISC_RTS	 0x0040	/* Set RTS status */
#define UMISC_RTSCONT	 0x0080	/* Choose RTS control */
#define UMISC_LOOP	 0x1000	/* Serial Loopback Enable */
#define UMISC_FORCE_PERR 0x2000	/* Force Parity Error */
#define UMISC_CLKSRC	 0x4000	/* Clock Source */


/* generalization of uart control registers to support multiple ports: */
typedef volatile struct {
  volatile unsigned short int ustcnt;
  volatile unsigned short int ubaud;
  union {
    volatile unsigned short int w;
    struct {
      volatile unsigned char status;
      volatile unsigned char rxdata;
    } b;
  } urx;
  union {
    volatile unsigned short int w;
    struct {
      volatile unsigned char status;
      volatile unsigned char txdata;
    } b;
  } utx;
  volatile unsigned short int umisc;
  volatile unsigned short int pad1;
  volatile unsigned short int pad2;
  volatile unsigned short int pad3;
} __attribute__((packed)) m68328_uart;


/**********
 *
 * 0xFFFFFAxx -- LCD Controller
 *
 **********/

/*
 * LCD Screen Starting Address Register 
 */
#define LSSA_ADDR	0xfffffa00
#define LSSA		LONG_REF(LSSA_ADDR)

#define LSSA_SSA_MASK	0xfffffffe	/* Bit 0 is reserved */

/*
 * LCD Virtual Page Width Register 
 */
#define LVPW_ADDR	0xfffffa05
#define LVPW		BYTE_REF(LVPW_ADDR)

/*
 * LCD Screen Width Register (not compatible with 'EZ328 !!!)
 */
#define LXMAX_ADDR	0xfffffa08
#define LXMAX		WORD_REF(LXMAX_ADDR)

#define LXMAX_XM_MASK	0x02ff		/* Bits 0-3 are reserved */

/*
 * LCD Screen Height Register
 */
#define LYMAX_ADDR	0xfffffa0a
#define LYMAX		WORD_REF(LYMAX_ADDR)

#define LYMAX_YM_MASK	0x02ff		/* Bits 10-15 are reserved */

/*
 * LCD Cursor X Position Register
 */
#define LCXP_ADDR	0xfffffa18
#define LCXP		WORD_REF(LCXP_ADDR)

#define LCXP_CC_MASK	0xc000		/* Cursor Control */
#define   LCXP_CC_TRAMSPARENT	0x0000
#define   LCXP_CC_BLACK		0x4000
#define   LCXP_CC_REVERSED	0x8000
#define   LCXP_CC_WHITE		0xc000
#define LCXP_CXP_MASK	0x02ff		/* Cursor X position */

/*
 * LCD Cursor Y Position Register
 */
#define LCYP_ADDR	0xfffffa1a
#define LCYP		WORD_REF(LCYP_ADDR)

#define LCYP_CYP_MASK	0x01ff		/* Cursor Y Position */

/*
 * LCD Cursor Width and Heigth Register
 */
#define LCWCH_ADDR	0xfffffa1c
#define LCWCH		WORD_REF(LCWCH_ADDR)

#define LCWCH_CH_MASK	0x001f		/* Cursor Height */
#define LCWCH_CH_SHIFT	0
#define LCWCH_CW_MASK	0x1f00		/* Cursor Width */
#define LCWCH_CW_SHIFT	8

/*
 * LCD Blink Control Register
 */
#define LBLKC_ADDR	0xfffffa1f
#define LBLKC		BYTE_REF(LBLKC_ADDR)

#define LBLKC_BD_MASK	0x7f	/* Blink Divisor */
#define LBLKC_BD_SHIFT	0
#define LBLKC_BKEN	0x80	/* Blink Enabled */

/*
 * LCD Panel Interface Configuration Register 
 */
#define LPICF_ADDR	0xfffffa20
#define LPICF		BYTE_REF(LPICF_ADDR)

#define LPICF_GS_MASK	 0x01	 /* Gray-Scale Mode */
#define	  LPICF_GS_BW	   0x00
#define   LPICF_GS_GRAY_4  0x01
#define LPICF_PBSIZ_MASK 0x06	/* Panel Bus Width */
#define   LPICF_PBSIZ_1	   0x00
#define   LPICF_PBSIZ_2    0x02
#define   LPICF_PBSIZ_4    0x04

/*
 * LCD Polarity Configuration Register 
 */
#define LPOLCF_ADDR	0xfffffa21
#define LPOLCF		BYTE_REF(LPOLCF_ADDR)

#define LPOLCF_PIXPOL	0x01	/* Pixel Polarity */
#define LPOLCF_LPPOL	0x02	/* Line Pulse Polarity */
#define LPOLCF_FLMPOL	0x04	/* Frame Marker Polarity */
#define LPOLCF_LCKPOL	0x08	/* LCD Shift Lock Polarity */

/*
 * LACD (LCD Alternate Crystal Direction) Rate Control Register
 */
#define LACDRC_ADDR	0xfffffa23
#define LACDRC		BYTE_REF(LACDRC_ADDR)

#define LACDRC_ACD_MASK	 0x0f	/* Alternate Crystal Direction Control */
#define LACDRC_ACD_SHIFT 0

/*
 * LCD Pixel Clock Divider Register
 */
#define LPXCD_ADDR	0xfffffa25
#define LPXCD		BYTE_REF(LPXCD_ADDR)

#define	LPXCD_PCD_MASK	0x3f 	/* Pixel Clock Divider */
#define LPXCD_PCD_SHIFT	0

/*
 * LCD Clocking Control Register
 */
#define LCKCON_ADDR	0xfffffa27
#define LCKCON		BYTE_REF(LCKCON_ADDR)

#define LCKCON_PCDS	 0x01	/* Pixel Clock Divider Source Select */
#define LCKCON_DWIDTH	 0x02	/* Display Memory Width  */
#define LCKCON_DWS_MASK	 0x3c	/* Display Wait-State */
#define LCKCON_DWS_SHIFT 2
#define LCKCON_DMA16	 0x40	/* DMA burst length */
#define LCKCON_LCDON	 0x80	/* Enable LCD Controller */

/* 'EZ328-compatible definitions */
#define LCKCON_DW_MASK	LCKCON_DWS_MASK
#define LCKCON_DW_SHIFT	LCKCON_DWS_SHIFT

/*
 * LCD Last Buffer Address Register
 */
#define LLBAR_ADDR	0xfffffa29
#define LLBAR		BYTE_REF(LLBAR_ADDR)

#define LLBAR_LBAR_MASK	 0x7f	/* Number of memory words to fill 1 line */
#define LLBAR_LBAR_SHIFT 0

/*
 * LCD Octet Terminal Count Register 
 */
#define LOTCR_ADDR	0xfffffa2b
#define LOTCR		BYTE_REF(LOTCR_ADDR)

/*
 * LCD Panning Offset Register
 */
#define LPOSR_ADDR	0xfffffa2d
#define LPOSR		BYTE_REF(LPOSR_ADDR)

#define LPOSR_BOS	0x08	/* Byte offset (for B/W mode only */
#define LPOSR_POS_MASK	0x07	/* Pixel Offset Code */
#define LPOSR_POS_SHIFT	0

/*
 * LCD Frame Rate Control Modulation Register
 */
#define LFRCM_ADDR	0xfffffa31
#define LFRCM		BYTE_REF(LFRCM_ADDR)

#define LFRCM_YMOD_MASK	 0x0f	/* Vertical Modulation */
#define LFRCM_YMOD_SHIFT 0
#define LFRCM_XMOD_MASK	 0xf0	/* Horizontal Modulation */
#define LFRCM_XMOD_SHIFT 4

/*
 * LCD Gray Palette Mapping Register
 */
#define LGPMR_ADDR	0xfffffa32
#define LGPMR		WORD_REF(LGPMR_ADDR)

#define LGPMR_GLEVEL3_MASK	0x000f
#define LGPMR_GLEVEL3_SHIFT	0 
#define LGPMR_GLEVEL2_MASK	0x00f0
#define LGPMR_GLEVEL2_SHIFT	4 
#define LGPMR_GLEVEL0_MASK	0x0f00
#define LGPMR_GLEVEL0_SHIFT	8 
#define LGPMR_GLEVEL1_MASK	0xf000
#define LGPMR_GLEVEL1_SHIFT	12

/**********
 *
 * 0xFFFFFBxx -- Real-Time Clock (RTC)
 *
 **********/

/*
 * RTC Hours Minutes and Seconds Register
 */
#define RTCTIME_ADDR	0xfffffb00
#define RTCTIME		LONG_REF(RTCTIME_ADDR)

#define RTCTIME_SECONDS_MASK	0x0000003f	/* Seconds */
#define RTCTIME_SECONDS_SHIFT	0
#define RTCTIME_MINUTES_MASK	0x003f0000	/* Minutes */
#define RTCTIME_MINUTES_SHIFT	16
#define RTCTIME_HOURS_MASK	0x1f000000	/* Hours */
#define RTCTIME_HOURS_SHIFT	24

/*
 *  RTC Alarm Register 
 */
#define RTCALRM_ADDR    0xfffffb04
#define RTCALRM         LONG_REF(RTCALRM_ADDR)

#define RTCALRM_SECONDS_MASK    0x0000003f      /* Seconds */
#define RTCALRM_SECONDS_SHIFT   0
#define RTCALRM_MINUTES_MASK    0x003f0000      /* Minutes */
#define RTCALRM_MINUTES_SHIFT   16
#define RTCALRM_HOURS_MASK      0x1f000000      /* Hours */
#define RTCALRM_HOURS_SHIFT     24

/*
 * RTC Control Register
 */
#define RTCCTL_ADDR	0xfffffb0c
#define RTCCTL		WORD_REF(RTCCTL_ADDR)

#define RTCCTL_384	0x0020	/* Crystal Selection */
#define RTCCTL_ENABLE	0x0080	/* RTC Enable */

/* 'EZ328-compatible definitions */
#define RTCCTL_XTL	RTCCTL_384
#define RTCCTL_EN	RTCCTL_ENABLE

/*
 * RTC Interrupt Status Register 
 */
#define RTCISR_ADDR	0xfffffb0e
#define RTCISR		WORD_REF(RTCISR_ADDR)

#define RTCISR_SW	0x0001	/* Stopwatch timed out */
#define RTCISR_MIN	0x0002	/* 1-minute interrupt has occurred */
#define RTCISR_ALM	0x0004	/* Alarm interrupt has occurred */
#define RTCISR_DAY	0x0008	/* 24-hour rollover interrupt has occurred */
#define RTCISR_1HZ	0x0010	/* 1Hz interrupt has occurred */

/*
 * RTC Interrupt Enable Register
 */
#define RTCIENR_ADDR	0xfffffb10
#define RTCIENR		WORD_REF(RTCIENR_ADDR)

#define RTCIENR_SW	0x0001	/* Stopwatch interrupt enable */
#define RTCIENR_MIN	0x0002	/* 1-minute interrupt enable */
#define RTCIENR_ALM	0x0004	/* Alarm interrupt enable */
#define RTCIENR_DAY	0x0008	/* 24-hour rollover interrupt enable */
#define RTCIENR_1HZ	0x0010	/* 1Hz interrupt enable */

/* 
 * Stopwatch Minutes Register
 */
#define STPWCH_ADDR	0xfffffb12
#define STPWCH		WORD_REF(STPWCH)

#define STPWCH_CNT_MASK	 0x00ff	/* Stopwatch countdown value */
#define SPTWCH_CNT_SHIFT 0

#endif /* _MC68328_H_ */
