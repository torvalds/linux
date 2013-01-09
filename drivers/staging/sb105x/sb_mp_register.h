
/*
 * SB105X_UART.h
 *
 * Copyright (C) 2008 systembase
 *
 * UART registers.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef UART_SB105X_H
#define UART_SB105X_H

/* 
 * option register 
 */

/* Device Infomation Register */
#define MP_OPTR_DIR0		0x04 	/* port0 ~ port8 */
#define MP_OPTR_DIR1		0x05 	/* port8 ~ port15 */
#define MP_OPTR_DIR2		0x06 	/* port16 ~ port23 */
#define MP_OPTR_DIR3		0x07 	/* port24 ~ port31 */

#define DIR_UART_16C550 	0
#define DIR_UART_16C1050	1
#define DIR_UART_16C1050A	2

#define	DIR_CLK_1843200		0x0		/* input clock 1843200 Hz */
#define	DIR_CLK_3686400		0x1		/* input clock 3686400 Hz */
#define	DIR_CLK_7372800		0x2		/* input clock 7372800 Hz */
#define	DIR_CLK_14745600	0x3		/* input clock 14745600 Hz */
#define	DIR_CLK_29491200	0x4		/* input clock 29491200 Hz */
#define	DIR_CLK_58985400	0x5		/* input clock 58985400 Hz */

/* Interface Information Register */
#define MP_OPTR_IIR0		0x08 	/* port0 ~ port8 */
#define MP_OPTR_IIR1		0x09 	/* port8 ~ port15 */
#define MP_OPTR_IIR2		0x0A 	/* port16 ~ port23 */
#define MP_OPTR_IIR3		0x0B 	/* port24 ~ port31 */

#define IIR_RS232		0x00		/* RS232 type */
#define IIR_RS422		0x10		/* RS422 type */
#define IIR_RS485		0x20		/* RS485 type */
#define IIR_TYPE_MASK		0x30

/* Interrrupt Mask Register */
#define MP_OPTR_IMR0		0x0C 	/* port0 ~ port8 */
#define MP_OPTR_IMR1		0x0D 	/* port8 ~ port15 */
#define MP_OPTR_IMR2		0x0E 	/* port16 ~ port23 */
#define MP_OPTR_IMR3		0x0F 	/* port24 ~ port31 */

/* Interrupt Poll Register */
#define MP_OPTR_IPR0		0x10 	/* port0 ~ port8 */
#define MP_OPTR_IPR1		0x11 	/* port8 ~ port15 */
#define MP_OPTR_IPR2		0x12 	/* port16 ~ port23 */
#define MP_OPTR_IPR3		0x13 	/* port24 ~ port31 */

/* General Purpose Output Control Register */
#define MP_OPTR_GPOCR		0x20

/* General Purpose Output Data Register */
#define MP_OPTR_GPODR		0x21

/* Parallel Additional Function Register */
#define MP_OPTR_PAFR		0x23

/*
 * systembase 16c105x UART register
 */

#define PAGE_0 0
#define PAGE_1 1
#define PAGE_2 2
#define PAGE_3 3
#define PAGE_4 4

/*
 *  ******************************************************************
 *  * DLAB=0                  ===============       Page 0 Registers *
 *  ******************************************************************
 */

#define SB105X_RX		0	/* In:  Receive buffer */
#define SB105X_TX		0	/* Out: Transmit buffer */

#define SB105X_IER		1	/* Out: Interrupt Enable Register */

#define SB105X_IER_CTSI	  	0x80	/* CTS# Interrupt Enable (Requires EFR[4] = 1) */
#define SB105X_IER_RTSI	  	0x40	/* RTS# Interrupt Enable (Requires EFR[4] = 1) */
#define SB105X_IER_XOI	  	0x20	/* Xoff Interrupt Enable (Requires EFR[4] = 1) */
#define SB105X_IER_SME	  	0x10	/* Sleep Mode Enable (Requires EFR[4] = 1) */
#define SB105X_IER_MSI	  	0x08	/* Enable Modem status interrupt */
#define SB105X_IER_RLSI	  	0x04	/* Enable receiver line status interrupt */
#define SB105X_IER_THRI	  	0x02	/* Enable Transmitter holding register int. */
#define SB105X_IER_RDI	  	0x01	/* Enable receiver data interrupt */

#define SB105X_ISR		2	/* In:  Interrupt ID Register */

#define SB105X_ISR_NOINT	0x01	/* No interrupts pending */
#define SB105X_ISR_RLSI	  	0x06	/* Receiver line status interrupt (Priority = 1)*/
#define SB105X_ISR_RDAI	  	0x0c	/* Receive Data Available interrupt */
#define SB105X_ISR_CTII	  	0x04	/* Character Timeout Indication interrupt */
#define SB105X_ISR_THRI	  	0x02	/* Transmitter holding register empty */
#define SB105X_ISR_MSI	  	0x00	/* Modem status interrupt */
#define SB105X_ISR_RXCI	  	0x10	/* Receive Xoff or Special Character interrupt */
#define SB105X_ISR_RCSI	  	0x20	/* RTS#, CTS# status interrupt during Auto RTS/CTS flow control */

#define SB105X_FCR		2	/* Out: FIFO Control Register */

#define SB105X_FCR_FEN    	0x01	/* FIFO Enable */
#define SB105X_FCR_RXFR	  	0x02	/* RX FIFO Reset */
#define SB105X_FCR_TXFR	  	0x04	/* TX FIFO Reset */
#define SB105X_FCR_DMS	  	0x08	/* DMA Mode Select */

#define SB105X_FCR_RTR08  	0x00	/* Receice Trigger Level set at 8 */
#define SB105X_FCR_RTR16  	0x40  /* Receice Trigger Level set at 16 */
#define SB105X_FCR_RTR56  	0x80  /* Receice Trigger Level set at 56 */
#define SB105X_FCR_RTR60  	0xc0  /* Receice Trigger Level set at 60 */
#define SB105X_FCR_TTR08  	0x00  /* Transmit Trigger Level set at 8 */
#define SB105X_FCR_TTR16	0x10  /* Transmit Trigger Level set at 16 */
#define SB105X_FCR_TTR32	0x20  /* Transmit Trigger Level set at 32 */
#define SB105X_FCR_TTR56	0x30  /* Transmit Trigger Level set at 56 */

#define SB105X_LCR		3	/* Out: Line Control Register */
/*
 *  * Note: if the word length is 5 bits (SB105X_LCR_WLEN5), then setting 
 *  * SB105X_LCR_STOP will select 1.5 stop bits, not 2 stop bits.
 */
#define SB105X_LCR_DLAB   	0x80  /* Divisor Latch Enable */
#define SB105X_LCR_SBC    	0x40  /* Break Enable*/
#define SB105X_LCR_SPAR   	0x20  /* Set Stick parity */
#define SB105X_LCR_EPAR   	0x10  /* Even parity select */
#define SB105X_LCR_PAREN  	0x08  /* Parity Enable */
#define SB105X_LCR_STOP   	0x04  /* Stop bits: 0->1 bit, 1->2 bits, 1 and SB105X_LCR_WLEN5 -> 1.5 bit */
#define SB105X_LCR_WLEN5  	0x00  /* Wordlength: 5 bits */
#define SB105X_LCR_WLEN6  	0x01  /* Wordlength: 6 bits */
#define SB105X_LCR_WLEN7  	0x02  /* Wordlength: 7 bits */
#define SB105X_LCR_WLEN8  	0x03  /* Wordlength: 8 bits */

#define SB105X_LCR_BF		0xBF

#define SB105X_MCR		4	/* Out: Modem Control Register */
#define SB105X_MCR_CPS    	0x80  /* Clock Prescaler Select */
#define SB105X_MCR_P2S    	0x40  /* Page 2 Select /Xoff Re-Transmit Access Enable */
#define SB105X_MCR_XOA    	0x20  /* Xon Any Enable */
#define SB105X_MCR_ILB		0x10  /* Internal Loopback Enable */
#define SB105X_MCR_OUT2		0x08  /* Out2/Interrupt Output Enable*/
#define SB105X_MCR_OUT1		0x04  /* Out1/Interrupt Output Enable */
#define SB105X_MCR_RTS    	0x02  /* RTS# Output */
#define SB105X_MCR_DTR    	0x01  /* DTR# Output */

#define SB105X_LSR		5	/* In:  Line Status Register */
#define SB105X_LSR_RFEI   	0x80  /* Receive FIFO data error Indicator */
#define SB105X_LSR_TEMI   	0x40  /* THR and TSR Empty Indicator */
#define SB105X_LSR_THRE		0x20  /* THR Empty Indicator */
#define SB105X_LSR_BII		0x10  /* Break interrupt indicator */
#define SB105X_LSR_FEI		0x08  /* Frame error indicator */
#define SB105X_LSR_PEI		0x04  /* Parity error indicator */
#define SB105X_LSR_OEI		0x02  /* Overrun error indicator */
#define SB105X_LSR_RDRI		0x01  /* Receive data ready Indicator*/

#define SB105X_MSR		6	/* In:  Modem Status Register */
#define SB105X_MSR_DCD		0x80  /* Data Carrier Detect */
#define SB105X_MSR_RI		0x40  /* Ring Indicator */
#define SB105X_MSR_DSR		0x20  /* Data Set Ready */
#define SB105X_MSR_CTS		0x10  /* Clear to Send */
#define SB105X_MSR_DDCD		0x08  /* Delta DCD */
#define SB105X_MSR_DRI		0x04  /* Delta ring indicator */
#define SB105X_MSR_DDSR		0x02  /* Delta DSR */
#define SB105X_MSR_DCTS		0x01  /* Delta CTS */

#define SB105XA_MDR		6	/* Out: Multi Drop mode Register */
#define SB105XA_MDR_NPS		0x08  /* 9th Bit Polarity Select */
#define SB105XA_MDR_AME		0x02  /* Auto Multi-drop Enable */
#define SB105XA_MDR_MDE		0x01  /* Multi Drop Enable */

#define SB105X_SPR		7	/* I/O: Scratch Register */

/*
 * DLAB=1
 */
#define SB105X_DLL		0	/* Out: Divisor Latch Low */
#define SB105X_DLM		1	/* Out: Divisor Latch High */

/*
 *  ******************************************************************
 *  * DLAB(LCR[7]) = 0 , MCR[6] = 1  =============  Page 2 Registers *
 *  ******************************************************************
 */
#define SB105X_GICR		1	/* Global Interrupt Control Register */
#define SB105X_GICR_GIM   	0x01  /* Global Interrupt Mask */

#define SB105X_GISR		2	/* Global Interrupt Status Register */
#define SB105X_GISR_MGICR0  	0x80  /* Mirror the content of GICR[0] */
#define SB105X_GISR_CS3IS   	0x08  /* SB105X of CS3# Interrupt Status */
#define SB105X_GISR_CS2IS   	0x04  /* SB105X of CS2# Interrupt Status */
#define SB105X_GISR_CS1IS   	0x02  /* SB105X of CS1# Interrupt Status */
#define SB105X_GISR_CS0IS   	0x01  /* SB105X of CS0# Interrupt Status */

#define SB105X_TFCR		5	/* Transmit FIFO Count Register */

#define SB105X_RFCR		6	/* Receive FIFO Count Register */

#define	SB105X_FSR		7	/* Flow Control Status Register */
#define SB105X_FSR_THFS     	0x20  /* Transmit Hardware Flow Control Status */
#define SB105X_FSR_TSFS     	0x10  /* Transmit Software Flow Control Status */
#define SB105X_FSR_RHFS     	0x02  /* Receive Hardware Flow Control Status */
#define SB105X_FSR_RSFS     	0x01  /* Receive Software Flow Control Status */

/*
 *  ******************************************************************
 *  * LCR = 0xBF, PSR[0] = 0       =============    Page 3 Registers *
 *  ******************************************************************
 */

#define SB105X_PSR		0	/* Page Select Register */
#define SB105X_PSR_P3KEY    	0xA4 /* Page 3 Select Key */
#define SB105X_PSR_P4KEY    	0xA5 /* Page 5 Select Key */

#define SB105X_ATR		1	/* Auto Toggle Control Register */
#define SB105X_ATR_RPS      	0x80  /* RXEN Polarity Select */
#define SB105X_ATR_RCMS     	0x40  /* RXEN Control Mode Select */
#define SB105X_ATR_TPS      	0x20  /* TXEN Polarity Select */
#define SB105X_ATR_TCMS     	0x10  /* TXEN Control Mode Select */
#define SB105X_ATR_ATDIS    	0x00  /* Auto Toggle is disabled */
#define SB105X_ATR_ART      	0x01  /* RTS#/TXEN pin operates as TXEN */
#define SB105X_ATR_ADT      	0x02  /* DTR#/TXEN pin operates as TXEN */
#define SB105X_ATR_A80      	0x03  /* only in 80 pin use */

#define SB105X_EFR		2	/* (Auto) Enhanced Feature Register */
#define SB105X_EFR_ACTS     	0x80  /* Auto-CTS Flow Control Enable */
#define SB105X_EFR_ARTS     	0x40  /* Auto-RTS Flow Control Enable */
#define SB105X_EFR_SCD      	0x20  /* Special Character Detect */
#define SB105X_EFR_EFBEN    	0x10  /* Enhanced Function Bits Enable */

#define SB105X_XON1		4	/* Xon1 Character Register */
#define SB105X_XON2		5	/* Xon2 Character Register */
#define SB105X_XOFF1		6	/* Xoff1 Character Register */
#define SB105X_XOFF2		7	/* Xoff2 Character Register */

/*
 *  ******************************************************************
 *  * LCR = 0xBF, PSR[0] = 1       ============     Page 4 Registers *
 *  ******************************************************************
 */

#define SB105X_AFR		1	/* Additional Feature Register */
#define SB105X_AFR_GIPS     	0x20  /* Global Interrupt Polarity Select */
#define SB105X_AFR_GIEN     	0x10  /* Global Interrupt Enable */
#define SB105X_AFR_AFEN     	0x01  /* 256-byte FIFO Enable */

#define SB105X_XRCR		2	/* Xoff Re-transmit Count Register */
#define SB105X_XRCR_NRC1    	0x00  /* Transmits Xoff Character whenever the number of received data is 1 during XOFF status */
#define SB105X_XRCR_NRC4    	0x01  /* Transmits Xoff Character whenever the number of received data is 4 during XOFF status */
#define SB105X_XRCR_NRC8    	0x02  /* Transmits Xoff Character whenever the number of received data is 8 during XOFF status */
#define SB105X_XRCR_NRC16   	0x03  /* Transmits Xoff Character whenever the number of received data is 16 during XOFF status */

#define SB105X_TTR		4	/* Transmit FIFO Trigger Level Register */
#define SB105X_RTR		5	/* Receive FIFO Trigger Level Register */
#define SB105X_FUR		6	/* Flow Control Upper Threshold Register */
#define SB105X_FLR		7	/* Flow Control Lower Threshold Register */


/* page 0 */

#define SB105X_GET_CHAR(port)	inb((port)->iobase + SB105X_RX)
#define SB105X_GET_IER(port)	inb((port)->iobase + SB105X_IER)
#define SB105X_GET_ISR(port)	inb((port)->iobase + SB105X_ISR)
#define SB105X_GET_LCR(port)	inb((port)->iobase + SB105X_LCR)
#define SB105X_GET_MCR(port)	inb((port)->iobase + SB105X_MCR)
#define SB105X_GET_LSR(port)	inb((port)->iobase + SB105X_LSR)
#define SB105X_GET_MSR(port)	inb((port)->iobase + SB105X_MSR)
#define SB105X_GET_SPR(port)	inb((port)->iobase + SB105X_SPR)

#define SB105X_PUT_CHAR(port,v)	outb((v),(port)->iobase + SB105X_TX )
#define SB105X_PUT_IER(port,v)	outb((v),(port)->iobase + SB105X_IER )
#define SB105X_PUT_FCR(port,v)	outb((v),(port)->iobase + SB105X_FCR )
#define SB105X_PUT_LCR(port,v)	outb((v),(port)->iobase + SB105X_LCR )
#define SB105X_PUT_MCR(port,v)	outb((v),(port)->iobase + SB105X_MCR )
#define SB105X_PUT_SPR(port,v)	outb((v),(port)->iobase + SB105X_SPR )


/* page 1 */
#define SB105X_GET_REG(port,reg)	inb((port)->iobase + (reg))
#define SB105X_PUT_REG(port,reg,v)	outb((v),(port)->iobase + (reg))

/* page 2 */

#define SB105X_PUT_PSR(port,v)	outb((v),(port)->iobase + SB105X_PSR )

#endif 
