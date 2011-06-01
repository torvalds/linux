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

#ifndef	_sbdma_h_
#define	_sbdma_h_

/* DMA structure:
 *  support two DMA engines: 32 bits address or 64 bit addressing
 *  basic DMA register set is per channel(transmit or receive)
 *  a pair of channels is defined for convenience
 */

/* 32 bits addressing */

typedef volatile struct {	/* diag access */
	u32 fifoaddr;	/* diag address */
	u32 fifodatalow;	/* low 32bits of data */
	u32 fifodatahigh;	/* high 32bits of data */
	u32 pad;		/* reserved */
} dma32diag_t;

/* 64 bits addressing */

/* dma registers per channel(xmt or rcv) */
typedef volatile struct {
	u32 control;		/* enable, et al */
	u32 ptr;		/* last descriptor posted to chip */
	u32 addrlow;		/* descriptor ring base address low 32-bits (8K aligned) */
	u32 addrhigh;	/* descriptor ring base address bits 63:32 (8K aligned) */
	u32 status0;		/* current descriptor, xmt state */
	u32 status1;		/* active descriptor, xmt error */
} dma64regs_t;

#endif				/* _sbdma_h_ */
