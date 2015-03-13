/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 */

#ifndef __DGNC_NEO_H
#define __DGNC_NEO_H

#include "dgnc_driver.h"

/************************************************************************
 * Per channel/port NEO UART structure					*
 ************************************************************************
 *		Base Structure Entries Usage Meanings to Host		*
 *									*
 *	W = read write		R = read only				*
 *			U = Unused.					*
 ************************************************************************/

struct neo_uart_struct {
	u8 txrx;		/* WR  RHR/THR - Holding Reg */
	u8 ier;		/* WR  IER - Interrupt Enable Reg */
	u8 isr_fcr;		/* WR  ISR/FCR - Interrupt Status Reg/Fifo Control Reg */
	u8 lcr;		/* WR  LCR - Line Control Reg */
	u8 mcr;		/* WR  MCR - Modem Control Reg */
	u8 lsr;		/* WR  LSR - Line Status Reg */
	u8 msr;		/* WR  MSR - Modem Status Reg */
	u8 spr;		/* WR  SPR - Scratch Pad Reg */
	u8 fctr;		/* WR  FCTR - Feature Control Reg */
	u8 efr;		/* WR  EFR - Enhanced Function Reg */
	u8 tfifo;		/* WR  TXCNT/TXTRG - Transmit FIFO Reg */
	u8 rfifo;		/* WR  RXCNT/RXTRG - Receive  FIFO Reg */
	u8 xoffchar1;	/* WR  XOFF 1 - XOff Character 1 Reg */
	u8 xoffchar2;	/* WR  XOFF 2 - XOff Character 2 Reg */
	u8 xonchar1;	/* WR  XON 1 - Xon Character 1 Reg */
	u8 xonchar2;	/* WR  XON 2 - XOn Character 2 Reg */

	u8 reserved1[0x2ff - 0x200]; /* U   Reserved by Exar */
	u8 txrxburst[64];	/* RW  64 bytes of RX/TX FIFO Data */
	u8 reserved2[0x37f - 0x340]; /* U   Reserved by Exar */
	u8 rxburst_with_errors[64];	/* R  64 bytes of RX FIFO Data + LSR */
};

/* Where to read the extended interrupt register (32bits instead of 8bits) */
#define	UART_17158_POLL_ADDR_OFFSET	0x80

/* These are the current dvid's of the Neo boards */
#define UART_XR17C158_DVID 0x20
#define UART_XR17D158_DVID 0x20
#define UART_XR17E158_DVID 0x40

#define NEO_EECK  0x10		/* Clock */
#define NEO_EECS  0x20		/* Chip Select */
#define NEO_EEDI  0x40		/* Data In  is an Output Pin */
#define NEO_EEDO  0x80		/* Data Out is an Input Pin */
#define NEO_EEREG 0x8E		/* offset to EEPROM control reg */


#define NEO_VPD_IMAGESIZE 0x40	/* size of image to read from EEPROM in words */
#define NEO_VPD_IMAGEBYTES (NEO_VPD_IMAGESIZE * 2)

/*
 * These are the redefinitions for the FCTR on the XR17C158, since
 * Exar made them different than their earlier design. (XR16C854)
 */

/* These are only applicable when table D is selected */
#define UART_17158_FCTR_RTS_NODELAY	0x00
#define UART_17158_FCTR_RTS_4DELAY	0x01
#define UART_17158_FCTR_RTS_6DELAY	0x02
#define UART_17158_FCTR_RTS_8DELAY	0x03
#define UART_17158_FCTR_RTS_12DELAY	0x12
#define UART_17158_FCTR_RTS_16DELAY	0x05
#define UART_17158_FCTR_RTS_20DELAY	0x13
#define UART_17158_FCTR_RTS_24DELAY	0x06
#define UART_17158_FCTR_RTS_28DELAY	0x14
#define UART_17158_FCTR_RTS_32DELAY	0x07
#define UART_17158_FCTR_RTS_36DELAY	0x16
#define UART_17158_FCTR_RTS_40DELAY	0x08
#define UART_17158_FCTR_RTS_44DELAY	0x09
#define UART_17158_FCTR_RTS_48DELAY	0x10
#define UART_17158_FCTR_RTS_52DELAY	0x11

#define UART_17158_FCTR_RTS_IRDA	0x10
#define UART_17158_FCTR_RS485		0x20
#define UART_17158_FCTR_TRGA		0x00
#define UART_17158_FCTR_TRGB		0x40
#define UART_17158_FCTR_TRGC		0x80
#define UART_17158_FCTR_TRGD		0xC0

/* 17158 trigger table selects.. */
#define UART_17158_FCTR_BIT6		0x40
#define UART_17158_FCTR_BIT7		0x80

/* 17158 TX/RX memmapped buffer offsets */
#define UART_17158_RX_FIFOSIZE		64
#define UART_17158_TX_FIFOSIZE		64

/* 17158 Extended IIR's */
#define UART_17158_IIR_RDI_TIMEOUT	0x0C	/* Receiver data TIMEOUT */
#define UART_17158_IIR_XONXOFF		0x10	/* Received an XON/XOFF char */
#define UART_17158_IIR_HWFLOW_STATE_CHANGE 0x20	/* CTS/DSR or RTS/DTR state change */
#define UART_17158_IIR_FIFO_ENABLED	0xC0	/* 16550 FIFOs are Enabled */

/*
 * These are the extended interrupts that get sent
 * back to us from the UART's 32bit interrupt register
 */
#define UART_17158_RX_LINE_STATUS	0x1	/* RX Ready */
#define UART_17158_RXRDY_TIMEOUT	0x2	/* RX Ready Timeout */
#define UART_17158_TXRDY		0x3	/* TX Ready */
#define UART_17158_MSR			0x4	/* Modem State Change */
#define UART_17158_TX_AND_FIFO_CLR	0x40	/* Transmitter Holding Reg Empty */
#define UART_17158_RX_FIFO_DATA_ERROR	0x80	/* UART detected an RX FIFO Data error */

/*
 * These are the EXTENDED definitions for the 17C158's Interrupt
 * Enable Register.
 */
#define UART_17158_EFR_ECB	0x10	/* Enhanced control bit */
#define UART_17158_EFR_IXON	0x2	/* Receiver compares Xon1/Xoff1 */
#define UART_17158_EFR_IXOFF	0x8	/* Transmit Xon1/Xoff1 */
#define UART_17158_EFR_RTSDTR	0x40	/* Auto RTS/DTR Flow Control Enable */
#define UART_17158_EFR_CTSDSR	0x80	/* Auto CTS/DSR Flow COntrol Enable */

#define UART_17158_XOFF_DETECT	0x1	/* Indicates whether chip saw an incoming XOFF char  */
#define UART_17158_XON_DETECT	0x2	/* Indicates whether chip saw an incoming XON char */

#define UART_17158_IER_RSVD1	0x10	/* Reserved by Exar */
#define UART_17158_IER_XOFF	0x20	/* Xoff Interrupt Enable */
#define UART_17158_IER_RTSDTR	0x40	/* Output Interrupt Enable */
#define UART_17158_IER_CTSDSR	0x80	/* Input Interrupt Enable */

/*
 * Our Global Variables
 */
extern struct board_ops dgnc_neo_ops;

#endif
