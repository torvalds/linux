/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __XLP_HAL_UART_H__
#define __XLP_HAL_UART_H__

/* UART Specific registers */
#define UART_RX_DATA		0x00
#define UART_TX_DATA		0x00

#define UART_INT_EN		0x01
#define UART_INT_ID		0x02
#define UART_FIFO_CTL		0x02
#define UART_LINE_CTL		0x03
#define UART_MODEM_CTL		0x04
#define UART_LINE_STS		0x05
#define UART_MODEM_STS		0x06

#define UART_DIVISOR0		0x00
#define UART_DIVISOR1		0x01

#define BASE_BAUD		(XLP_IO_CLK/16)
#define BAUD_DIVISOR(baud)	(BASE_BAUD / baud)

/* LCR mask values */
#define LCR_5BITS		0x00
#define LCR_6BITS		0x01
#define LCR_7BITS		0x02
#define LCR_8BITS		0x03
#define LCR_STOPB		0x04
#define LCR_PENAB		0x08
#define LCR_PODD		0x00
#define LCR_PEVEN		0x10
#define LCR_PONE		0x20
#define LCR_PZERO		0x30
#define LCR_SBREAK		0x40
#define LCR_EFR_ENABLE		0xbf
#define LCR_DLAB		0x80

/* MCR mask values */
#define MCR_DTR			0x01
#define MCR_RTS			0x02
#define MCR_DRS			0x04
#define MCR_IE			0x08
#define MCR_LOOPBACK		0x10

/* FCR mask values */
#define FCR_RCV_RST		0x02
#define FCR_XMT_RST		0x04
#define FCR_RX_LOW		0x00
#define FCR_RX_MEDL		0x40
#define FCR_RX_MEDH		0x80
#define FCR_RX_HIGH		0xc0

/* IER mask values */
#define IER_ERXRDY		0x1
#define IER_ETXRDY		0x2
#define IER_ERLS		0x4
#define IER_EMSC		0x8

#if !defined(LOCORE) && !defined(__ASSEMBLY__)

#define	nlm_read_uart_reg(b, r)		nlm_read_reg(b, r)
#define	nlm_write_uart_reg(b, r, v)	nlm_write_reg(b, r, v)
#define nlm_get_uart_pcibase(node, inst)	\
		nlm_pcicfg_base(XLP_IO_UART_OFFSET(node, inst))
#define nlm_get_uart_regbase(node, inst)	\
			(nlm_get_uart_pcibase(node, inst) + XLP_IO_PCI_HDRSZ)

static inline void
nlm_uart_set_baudrate(uint64_t base, int baud)
{
	uint32_t lcr;

	lcr = nlm_read_uart_reg(base, UART_LINE_CTL);

	/* enable divisor register, and write baud values */
	nlm_write_uart_reg(base, UART_LINE_CTL, lcr | (1 << 7));
	nlm_write_uart_reg(base, UART_DIVISOR0,
			(BAUD_DIVISOR(baud) & 0xff));
	nlm_write_uart_reg(base, UART_DIVISOR1,
			((BAUD_DIVISOR(baud) >> 8) & 0xff));

	/* restore default lcr */
	nlm_write_uart_reg(base, UART_LINE_CTL, lcr);
}

static inline void
nlm_uart_outbyte(uint64_t base, char c)
{
	uint32_t lsr;

	for (;;) {
		lsr = nlm_read_uart_reg(base, UART_LINE_STS);
		if (lsr & 0x20)
			break;
	}

	nlm_write_uart_reg(base, UART_TX_DATA, (int)c);
}

static inline char
nlm_uart_inbyte(uint64_t base)
{
	int data, lsr;

	for (;;) {
		lsr = nlm_read_uart_reg(base, UART_LINE_STS);
		if (lsr & 0x80) { /* parity/frame/break-error - push a zero */
			data = 0;
			break;
		}
		if (lsr & 0x01) {	/* Rx data */
			data = nlm_read_uart_reg(base, UART_RX_DATA);
			break;
		}
	}

	return (char)data;
}

static inline int
nlm_uart_init(uint64_t base, int baud, int databits, int stopbits,
	int parity, int int_en, int loopback)
{
	uint32_t lcr;

	lcr = 0;
	if (databits >= 8)
		lcr |= LCR_8BITS;
	else if (databits == 7)
		lcr |= LCR_7BITS;
	else if (databits == 6)
		lcr |= LCR_6BITS;
	else
		lcr |= LCR_5BITS;

	if (stopbits > 1)
		lcr |= LCR_STOPB;

	lcr |= parity << 3;

	/* setup default lcr */
	nlm_write_uart_reg(base, UART_LINE_CTL, lcr);

	/* Reset the FIFOs */
	nlm_write_uart_reg(base, UART_LINE_CTL, FCR_RCV_RST | FCR_XMT_RST);

	nlm_uart_set_baudrate(base, baud);

	if (loopback)
		nlm_write_uart_reg(base, UART_MODEM_CTL, 0x1f);

	if (int_en)
		nlm_write_uart_reg(base, UART_INT_EN, IER_ERXRDY | IER_ETXRDY);

	return 0;
}
#endif /* !LOCORE && !__ASSEMBLY__ */
#endif /* __XLP_HAL_UART_H__ */
