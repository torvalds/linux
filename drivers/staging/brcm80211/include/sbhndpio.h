/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_sbhndpio_h_
#define	_sbhndpio_h_

/* PIO structure,
 *  support two PIO format: 2 bytes access and 4 bytes access
 *  basic FIFO register set is per channel(transmit or receive)
 *  a pair of channels is defined for convenience
 */

/* 2byte-wide pio register set per channel(xmt or rcv) */
typedef volatile struct {
	u16 fifocontrol;
	u16 fifodata;
	u16 fifofree;	/* only valid in xmt channel, not in rcv channel */
	u16 PAD;
} pio2regs_t;

/* a pair of pio channels(tx and rx) */
typedef volatile struct {
	pio2regs_t tx;
	pio2regs_t rx;
} pio2regp_t;

/* 4byte-wide pio register set per channel(xmt or rcv) */
typedef volatile struct {
	u32 fifocontrol;
	u32 fifodata;
} pio4regs_t;

/* a pair of pio channels(tx and rx) */
typedef volatile struct {
	pio4regs_t tx;
	pio4regs_t rx;
} pio4regp_t;

#endif				/* _sbhndpio_h_ */
