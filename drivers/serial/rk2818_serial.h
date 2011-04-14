/*
 * drivers/serial/rk2818_serial.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DRIVERS_SERIAL_RK2818_SERIAL_H
#define __DRIVERS_SERIAL_RK2818_SERIAL_H

#define UART_RBR 	0x0000			/* Receive Buffer Register */
#define UART_THR 	0x0000			/* Transmit Holding Register */
#define UART_DLL 	0x0000			/* Divisor Latch (Low) */
#define UART_DLH 	0x0004			/* Divisor Latch (High) */
#define UART_IER 	0x0004			/* Interrupt Enable Register */
#define UART_IIR 	0x0008			/* Interrupt Identification Register */
#define UART_FCR 	0x0008			/* FIFO Control Register */
#define UART_LCR 	0x000C			/* Line Control Register */
#define UART_MCR 	0x0010			/* Modem Control Register */
#define UART_LSR 	0x0014			/* [0x0000_0060] Line Status Register */
#define UART_MSR 	0x0018			/* Modem Status Register */
#define UART_SCR 	0x001c			/* Scratchpad Register */
#define UART_SRBR(n) 	(0x0030+((n) * 4))	/* Shadow Receive Buffer Register */
#define UART_STHR(n) 	(0x0030+((n) * 4))	/* Shadow Transmit Holding Register */
#define UART_FAR 	0x0070			/* FIFO Access Register */
#define UART_TFR 	0x0074			/* Transmit FIFO Read */
#define UART_RFW 	0x0078			/* Receive FIFO Write */
#define UART_USR 	0x007C			/* UART Status Register */
#define UART_TFL 	0x0080			/* Transmit FIFO Level */
#define UART_RFL 	0x0084			/* Receive FIFO Level */
#define UART_SRR 	0x0088			/* Software Reset Register */
#define UART_SRTS 	0x008C			/* Shadow Request to Send */
#define UART_SBCR 	0x0090			/* Shadow Break Control Register */
#define UART_SDMAM 	0x0094			/* Shadow DMA Mode */
#define UART_SFE 	0x0098			/* Shadow FIFO Enable */
#define UART_SRT 	0x009C			/* Shadow RCVR Trigger */
#define UART_STET 	0x00A0			/* Shadow TX Empty Trigger */
#define UART_HTX 	0x00A4			/* Halt TX */
#define UART_DMASA 	0x00A8			/* DMA Software Acknowledge */
#define UART_CPR 	0x00F4			/* Component Parameter Register */
#define UART_UCV 	0x00F8			/* [0x3330_372a] UART Component Version */
#define UART_CTR 	0x00FC			/* [0x4457_0110] Component Type Register */

//#define UART_FCR            0x08
#define  UART_FCR_FIFO_ENABLE	(1<<0) 
#define  UART_FCR_CLEAR_RCVR	(1<<1) 	/* Clear the RCVR FIFO */
#define  UART_FCR_CLEAR_XMIT		(1<<2)	/* Clear the XMIT FIFO */
#define  UART_FCR_DMA_SELECT	(1<<3)	 /* For DMA applications */
#define  UART_FCR_R_TRIG_00	0x00
#define  UART_FCR_R_TRIG_01	0x40
#define  UART_FCR_R_TRIG_10	0x80
#define  UART_FCR_R_TRIG_11	0xc0
#define  UART_FCR_T_TRIG_00	0x00
#define  UART_FCR_T_TRIG_01	0x10
#define  UART_FCR_T_TRIG_10	0x20
#define  UART_FCR_T_TRIG_11	0x30

//#define UART_LCR            0x0c
#define  LCR_DLA_EN                         (1<<7)
#define  BREAK_CONTROL_BIT                  (1<<6)
#define  EVEN_PARITY_SELECT                 (1<<4)
#define  EVEN_PARITY                        (1<<4)
#define  ODD_PARITY                          (0)
#define  PARITY_DISABLED                     (0)
#define  PARITY_ENABLED                     (1<<3)
#define  ONE_STOP_BIT                        (0)
#define  ONE_HALF_OR_TWO_BIT                (1<<2)
#define  LCR_WLS_5                           (0x00)
#define  LCR_WLS_6                           (0x01)
#define  LCR_WLS_7                           (0x02)
#define  LCR_WLS_8                           (0x03)
#define  UART_DATABIT_MASK                   (0x03)

/* Detail UART_IER  Register Description */
#define UART_IER_THRE_MODE_INT_ENABLE		1<<7
#define UART_IER_MODEM_STATUS_INT_ENABLE	1<<3
#define UART_IER_RECV_LINE_STATUS_INT_ENABLE	1<<2
#define UART_IER_SEND_EMPTY_INT_ENABLE		1<<1
#define UART_IER_RECV_DATA_AVAIL_INT_ENABLE	1<<0


/* Detail UART_IIR  Register Description */
#define UART_IIR_FIFO_DISABLE			0x00
#define UART_IIR_FIFO_ENABLE			0x03
#define UART_IIR_INT_ID_MASK			0x0F
#define UART_IIR_MODEM_STATUS			0x00
#define UART_IIR_NO_INTERRUPT_PENDING	0x01
#define UART_IIR_THR_EMPTY			    0x02
#define UART_IIR_RECV_AVAILABLE			0x04
#define UART_IIR_RECV_LINE_STATUS		0x06
#define UART_IIR_BUSY_DETECT			0x07
#define UART_IIR_CHAR_TIMEOUT			0x0C

//#define UART_MCR            0x10
/* Modem Control Register */
#define	UART_SIR_ENABLE (1 << 6)
#define UART_MCR_AFCEN	(1 << 5)	/* Auto Flow Control Mode enabled */
#define UART_MCR_URLB	(1 << 4)	/* Loop-back mode */
#define UART_MCR_UROUT2	(1 << 3)	/* OUT2 signal */
#define UART_MCR_UROUT1	(1 << 2)	/* OUT1 signal */
#define UART_MCR_URRTS	(1 << 1)	/* Request to Send */
#define UART_MCR_URDTR	(1 << 0)	/* Data Terminal Ready */

//#define UART_MSR            0x18
/* Modem Status Register */
#define UART_MSR_URDCD	(1 << 7)	/* Data Carrier Detect */
#define UART_MSR_URRI	(1 << 6)	/* Ring Indicator */
#define UART_MSR_URDSR	(1 << 5)	/* Data Set Ready */
#define UART_MSR_URCTS	(1 << 4)	/* Clear to Send */
#define UART_MSR_URDDCD	(1 << 3)	/* Delta Data Carrier Detect */
#define UART_MSR_URTERI	(1 << 2)	/* Trailing Edge Ring Indicator */
#define UART_MSR_URDDST	(1 << 1)	/* Delta Data Set Ready */
#define UART_MSR_URDCTS	(1 << 0)	/* Delta Clear to Send */

/* Detail UART_USR  Register Description */
#define  UART_RECEIVE_FIFO_FULL              (1<<4)
#define  UART_RECEIVE_FIFO_NOT_FULL          (0)
#define  UART_RECEIVE_FIFO_EMPTY             (0)
#define  UART_RECEIVE_FIFO_NOT_EMPTY         (1<<3)
#define  UART_TRANSMIT_FIFO_NOT_EMPTY        (0)
#define  UART_TRANSMIT_FIFO_EMPTY            (1<<2)
#define  UART_TRANSMIT_FIFO_FULL             (0)
#define  UART_TRANSMIT_FIFO_NOT_FULL         (1<<1)
#define  UART_USR_BUSY                       (1)

/*UART_LSR Line Status Register*/
#define UART_BREAK_INT_BIT					(1<<4)/*break Interrupt bit*/

#endif	/* __DRIVERS_SERIAL_RK2818_SERIAL_H */
