/**
 * ipoctal.h
 *
 * driver for the IPOCTAL boards
 * Copyright (c) 2009 Nicolas Serafini, EIC2 SA
 * Copyright (c) 2010,2011 Samuel Iglesias Gonsalvez <siglesia@cern.ch>, CERN
 * Copyright (c) 2012 Samuel Iglesias Gonsalvez <siglesias@igalia.com>, Igalia
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#ifndef _IPOCTAL_H
#define _IPOCTAL_H_

#define NR_CHANNELS		8
#define IPOCTAL_MAX_BOARDS	16
#define MAX_DEVICES		(NR_CHANNELS * IPOCTAL_MAX_BOARDS)
#define RELEVANT_IFLAG(iflag) ((iflag) & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

/**
 * enum uart_parity_e - UART supported parity.
 */
enum uart_parity_e {
	UART_NONE  = 0,
	UART_ODD   = 1,
	UART_EVEN  = 2,
};

/**
 * enum uart_error - UART error type
 *
 */
enum uart_error	{
	UART_NOERROR = 0,      /* No error during transmission */
	UART_TIMEOUT = 1 << 0, /* Timeout error */
	UART_OVERRUN = 1 << 1, /* Overrun error */
	UART_PARITY  = 1 << 2, /* Parity error */
	UART_FRAMING = 1 << 3, /* Framing error */
	UART_BREAK   = 1 << 4, /* Received break */
};

/**
 * struct ipoctal_config - Serial configuration
 *
 * @baud: Baud rate
 * @stop_bits: Stop bits (1 or 2)
 * @bits_per_char: data size in bits
 * @parity
 * @flow_control: Flow control management (RTS/CTS) (0 disabled, 1 enabled)
 */
struct ipoctal_config {
	unsigned int baud;
	unsigned int stop_bits;
	unsigned int bits_per_char;
	unsigned short parity;
	unsigned int flow_control;
};

/**
 * struct ipoctal_stats -- Stats since last reset
 *
 * @tx: Number of transmitted bytes
 * @rx: Number of received bytes
 * @overrun: Number of overrun errors
 * @parity_err: Number of parity errors
 * @framing_err: Number of framing errors
 * @rcv_break: Number of break received
 */
struct ipoctal_stats {
	unsigned long tx;
	unsigned long rx;
	unsigned long overrun_err;
	unsigned long parity_err;
	unsigned long framing_err;
	unsigned long rcv_break;
};

#endif /* _IPOCTAL_H_ */
