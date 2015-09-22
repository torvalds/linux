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

#ifndef __DGNC_CLS_H
#define __DGNC_CLS_H

/************************************************************************
 * Per channel/port Classic UART structure				*
 ************************************************************************
 *		Base Structure Entries Usage Meanings to Host		*
 *									*
 *	W = read write		R = read only				*
 *			U = Unused.					*
 ************************************************************************/

/*
 * txrx    : WR RHR/THR - Holding reg
 * ier     : WR IER - Interrupt Enable Reg
 * isr_fcr : WR ISR/FCR - Interrupt Status Reg/Fifo Control Reg
 * lcr     : WR LCR - Line Control Reg
 * mcr     : WR MCR - Modem Control Reg
 * lsr     : WR LSR - Line Status Reg
 * msr     : WR MSG - Modem Status Reg
 * spr     : WR SPR - Scratch pad Reg
 */
struct cls_uart_struct {
	u8 txrx;
	u8 ier;
	u8 isr_fcr;
	u8 lcr;
	u8 mcr;
	u8 lsr;
	u8 msr;
	u8 spr;
};

/* Where to read the interrupt register (8bits) */
#define	UART_CLASSIC_POLL_ADDR_OFFSET	0x40

#define UART_EXAR654_ENHANCED_REGISTER_SET 0xBF

#define UART_16654_FCR_TXTRIGGER_16	0x10
#define UART_16654_FCR_RXTRIGGER_16	0x40
#define UART_16654_FCR_RXTRIGGER_56	0x80

/* Received CTS/RTS change of state */
#define UART_IIR_CTSRTS			0x20

/* Receiver data TIMEOUT */
#define UART_IIR_RDI_TIMEOUT		0x0C

/*
 * These are the EXTENDED definitions for the Exar 654's Interrupt
 * Enable Register.
 */
#define UART_EXAR654_EFR_ECB      0x10    /* Enhanced control bit */
#define UART_EXAR654_EFR_IXON     0x2     /* Receiver compares Xon1/Xoff1 */
#define UART_EXAR654_EFR_IXOFF    0x8     /* Transmit Xon1/Xoff1 */
#define UART_EXAR654_EFR_RTSDTR   0x40    /* Auto RTS/DTR Flow Control Enable */
#define UART_EXAR654_EFR_CTSDSR   0x80    /* Auto CTS/DSR Flow COntrol Enable */
#define UART_EXAR654_IER_XOFF     0x20    /* Xoff Interrupt Enable */
#define UART_EXAR654_IER_RTSDTR   0x40    /* Output Interrupt Enable */
#define UART_EXAR654_IER_CTSDSR   0x80    /* Input Interrupt Enable */

/*
 * Our Global Variables
 */
extern struct board_ops dgnc_cls_ops;

#endif
