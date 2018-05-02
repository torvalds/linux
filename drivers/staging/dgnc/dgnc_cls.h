/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 */

#ifndef _DGNC_CLS_H
#define _DGNC_CLS_H

/**
 * struct cls_uart_struct - Per channel/port Classic UART.
 *
 * key - W = read write
 *     - R = read only
 *     - U = unused
 *
 * @txrx: (WR) Holding Register.
 * @ier: (WR) Interrupt Enable Register.
 * @isr_fcr: (WR) Interrupt Status Register/Fifo Control Register.
 * @lcr: (WR) Line Control Register.
 * @mcr: (WR) Modem Control Register.
 * @lsr: (WR) Line Status Register.
 * @msr: (WR) Modem Status Register.
 * @spr: (WR) Scratch Pad Register.
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
#define UART_EXAR654_EFR_CTSDSR   0x80    /* Auto CTS/DSR Flow Control Enable */
#define UART_EXAR654_IER_XOFF     0x20    /* Xoff Interrupt Enable */
#define UART_EXAR654_IER_RTSDTR   0x40    /* Output Interrupt Enable */
#define UART_EXAR654_IER_CTSDSR   0x80    /* Input Interrupt Enable */

extern struct board_ops dgnc_cls_ops;

#endif	/* _DGNC_CLS_H */
