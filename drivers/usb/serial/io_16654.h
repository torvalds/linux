// SPDX-License-Identifier: GPL-2.0+
/************************************************************************
 *
 *	16654.H		Definitions for 16C654 UART used on EdgePorts
 *
 *	Copyright (C) 1998 Inside Out Networks, Inc.
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 ************************************************************************/

#if !defined(_16654_H)
#define	_16654_H

/************************************************************************
 *
 *			D e f i n e s   /   T y p e d e f s
 *
 ************************************************************************/

	//
	// UART register numbers
	// Numbers 0-7 are passed to the Edgeport directly. Numbers 8 and
	// above are used internally to indicate that we must enable access
	// to them via LCR bit 0x80 or LCR = 0xBF.
	// The register number sent to the Edgeport is then (x & 0x7).
	//
	// Driver must not access registers that affect operation of the
	// the EdgePort firmware -- that includes THR, RHR, IER, FCR.


#define THR			0	// ! Transmit Holding Register (Write)
#define RDR			0	// ! Receive Holding Register (Read)
#define IER			1	// ! Interrupt Enable Register
#define FCR			2	// ! Fifo Control Register (Write)
#define ISR			2	// Interrupt Status Register (Read)
#define LCR			3	// Line Control Register
#define MCR			4	// Modem Control Register
#define LSR			5	// Line Status Register
#define MSR			6	// Modem Status Register
#define SPR			7	// ScratchPad Register
#define DLL			8	// Bank2[ 0 ] Divisor Latch LSB
#define DLM			9	// Bank2[ 1 ] Divisor Latch MSB
#define EFR			10	// Bank2[ 2 ] Extended Function Register
//efine unused			11	// Bank2[ 3 ]
#define XON1			12	// Bank2[ 4 ] Xon-1
#define XON2			13	// Bank2[ 5 ] Xon-2
#define XOFF1			14	// Bank2[ 6 ] Xoff-1
#define XOFF2			15	// Bank2[ 7 ] Xoff-2

#define	NUM_16654_REGS		16

#define IS_REG_2ND_BANK(x)	((x) >= 8)

	//
	// Bit definitions for each register
	//

#define IER_RX			0x01	// Enable receive interrupt
#define IER_TX			0x02	// Enable transmit interrupt
#define IER_RXS			0x04	// Enable receive status interrupt
#define IER_MDM			0x08	// Enable modem status interrupt
#define IER_SLEEP		0x10	// Enable sleep mode
#define IER_XOFF		0x20	// Enable s/w flow control (XOFF) interrupt
#define IER_RTS			0x40	// Enable RTS interrupt
#define IER_CTS			0x80	// Enable CTS interrupt
#define IER_ENABLE_ALL		0xFF	// Enable all ints


#define FCR_FIFO_EN		0x01	// Enable FIFOs
#define FCR_RXCLR		0x02	// Reset Rx FIFO
#define FCR_TXCLR		0x04	// Reset Tx FIFO
#define FCR_DMA_BLK		0x08	// Enable DMA block mode
#define FCR_TX_LEVEL_MASK	0x30	// Mask for Tx FIFO Level
#define FCR_TX_LEVEL_8		0x00	// Tx FIFO Level =  8 bytes
#define FCR_TX_LEVEL_16		0x10	// Tx FIFO Level = 16 bytes
#define FCR_TX_LEVEL_32		0x20	// Tx FIFO Level = 32 bytes
#define FCR_TX_LEVEL_56		0x30	// Tx FIFO Level = 56 bytes
#define FCR_RX_LEVEL_MASK	0xC0	// Mask for Rx FIFO Level
#define FCR_RX_LEVEL_8		0x00	// Rx FIFO Level =  8 bytes
#define FCR_RX_LEVEL_16		0x40	// Rx FIFO Level = 16 bytes
#define FCR_RX_LEVEL_56		0x80	// Rx FIFO Level = 56 bytes
#define FCR_RX_LEVEL_60		0xC0	// Rx FIFO Level = 60 bytes


#define ISR_INT_MDM_STATUS	0x00	// Modem status int pending
#define ISR_INT_NONE		0x01	// No interrupt pending
#define ISR_INT_TXRDY		0x02	// Tx ready int pending
#define ISR_INT_RXRDY		0x04	// Rx ready int pending
#define ISR_INT_LINE_STATUS	0x06	// Line status int pending
#define ISR_INT_RX_TIMEOUT	0x0C	// Rx timeout int pending
#define ISR_INT_RX_XOFF		0x10	// Rx Xoff int pending
#define ISR_INT_RTS_CTS		0x20	// RTS/CTS change int pending
#define ISR_FIFO_ENABLED	0xC0	// Bits set if FIFOs enabled
#define ISR_INT_BITS_MASK	0x3E	// Mask to isolate valid int causes


#define LCR_BITS_5		0x00	// 5 bits/char
#define LCR_BITS_6		0x01	// 6 bits/char
#define LCR_BITS_7		0x02	// 7 bits/char
#define LCR_BITS_8		0x03	// 8 bits/char
#define LCR_BITS_MASK		0x03	// Mask for bits/char field

#define LCR_STOP_1		0x00	// 1 stop bit
#define LCR_STOP_1_5		0x04	// 1.5 stop bits (if 5   bits/char)
#define LCR_STOP_2		0x04	// 2 stop bits   (if 6-8 bits/char)
#define LCR_STOP_MASK		0x04	// Mask for stop bits field

#define LCR_PAR_NONE		0x00	// No parity
#define LCR_PAR_ODD		0x08	// Odd parity
#define LCR_PAR_EVEN		0x18	// Even parity
#define LCR_PAR_MARK		0x28	// Force parity bit to 1
#define LCR_PAR_SPACE		0x38	// Force parity bit to 0
#define LCR_PAR_MASK		0x38	// Mask for parity field

#define LCR_SET_BREAK		0x40	// Set Break condition
#define LCR_DL_ENABLE		0x80	// Enable access to divisor latch

#define LCR_ACCESS_EFR		0xBF	// Load this value to access DLL,DLM,
					// and also the '654-only registers
					// EFR, XON1, XON2, XOFF1, XOFF2


#define MCR_DTR			0x01	// Assert DTR
#define MCR_RTS			0x02	// Assert RTS
#define MCR_OUT1		0x04	// Loopback only: Sets state of RI
#define MCR_MASTER_IE		0x08	// Enable interrupt outputs
#define MCR_LOOPBACK		0x10	// Set internal (digital) loopback mode
#define MCR_XON_ANY		0x20	// Enable any char to exit XOFF mode
#define MCR_IR_ENABLE		0x40	// Enable IrDA functions
#define MCR_BRG_DIV_4		0x80	// Divide baud rate clk by /4 instead of /1


#define LSR_RX_AVAIL		0x01	// Rx data available
#define LSR_OVER_ERR		0x02	// Rx overrun
#define LSR_PAR_ERR		0x04	// Rx parity error
#define LSR_FRM_ERR		0x08	// Rx framing error
#define LSR_BREAK		0x10	// Rx break condition detected
#define LSR_TX_EMPTY		0x20	// Tx Fifo empty
#define LSR_TX_ALL_EMPTY	0x40	// Tx Fifo and shift register empty
#define LSR_FIFO_ERR		0x80	// Rx Fifo contains at least 1 erred char


#define EDGEPORT_MSR_DELTA_CTS	0x01	// CTS changed from last read
#define EDGEPORT_MSR_DELTA_DSR	0x02	// DSR changed from last read
#define EDGEPORT_MSR_DELTA_RI	0x04	// RI  changed from 0 -> 1
#define EDGEPORT_MSR_DELTA_CD	0x08	// CD  changed from last read
#define EDGEPORT_MSR_CTS	0x10	// Current state of CTS
#define EDGEPORT_MSR_DSR	0x20	// Current state of DSR
#define EDGEPORT_MSR_RI		0x40	// Current state of RI
#define EDGEPORT_MSR_CD		0x80	// Current state of CD



					//	Tx		Rx
					//-------------------------------
#define EFR_SWFC_NONE		0x00	//	None		None
#define EFR_SWFC_RX1		0x02 	//	None		XOFF1
#define EFR_SWFC_RX2		0x01 	//	None		XOFF2
#define EFR_SWFC_RX12		0x03 	//	None		XOFF1 & XOFF2
#define EFR_SWFC_TX1		0x08 	//	XOFF1		None
#define EFR_SWFC_TX1_RX1	0x0a 	//	XOFF1		XOFF1
#define EFR_SWFC_TX1_RX2	0x09 	//	XOFF1		XOFF2
#define EFR_SWFC_TX1_RX12	0x0b 	//	XOFF1		XOFF1 & XOFF2
#define EFR_SWFC_TX2		0x04 	//	XOFF2		None
#define EFR_SWFC_TX2_RX1	0x06 	//	XOFF2		XOFF1
#define EFR_SWFC_TX2_RX2	0x05 	//	XOFF2		XOFF2
#define EFR_SWFC_TX2_RX12	0x07 	//	XOFF2		XOFF1 & XOFF2
#define EFR_SWFC_TX12		0x0c 	//	XOFF1 & XOFF2	None
#define EFR_SWFC_TX12_RX1	0x0e 	//	XOFF1 & XOFF2	XOFF1
#define EFR_SWFC_TX12_RX2	0x0d 	//	XOFF1 & XOFF2	XOFF2
#define EFR_SWFC_TX12_RX12	0x0f 	//	XOFF1 & XOFF2	XOFF1 & XOFF2

#define EFR_TX_FC_MASK		0x0c	// Mask to isolate Rx flow control
#define EFR_TX_FC_NONE		0x00	// No Tx Xon/Xoff flow control
#define EFR_TX_FC_X1		0x08	// Transmit Xon1/Xoff1
#define EFR_TX_FC_X2		0x04	// Transmit Xon2/Xoff2
#define EFR_TX_FC_X1_2		0x0c	// Transmit Xon1&2/Xoff1&2

#define EFR_RX_FC_MASK		0x03	// Mask to isolate Rx flow control
#define EFR_RX_FC_NONE		0x00	// No Rx Xon/Xoff flow control
#define EFR_RX_FC_X1		0x02	// Receiver compares Xon1/Xoff1
#define EFR_RX_FC_X2		0x01	// Receiver compares Xon2/Xoff2
#define EFR_RX_FC_X1_2		0x03	// Receiver compares Xon1&2/Xoff1&2


#define EFR_SWFC_MASK		0x0F	// Mask for software flow control field
#define EFR_ENABLE_16654	0x10	// Enable 16C654 features
#define EFR_SPEC_DETECT		0x20	// Enable special character detect interrupt
#define EFR_AUTO_RTS		0x40	// Use RTS for Rx flow control
#define EFR_AUTO_CTS		0x80	// Use CTS for Tx flow control

#endif	// if !defined(_16654_H)

