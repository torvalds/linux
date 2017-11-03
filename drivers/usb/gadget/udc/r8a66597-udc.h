// SPDX-License-Identifier: GPL-2.0
/*
 * R8A66597 UDC
 *
 * Copyright (C) 2007-2009 Renesas Solutions Corp.
 *
 * Author : Yoshihiro Shimoda <yoshihiro.shimoda.uh@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#ifndef __R8A66597_H__
#define __R8A66597_H__

#include <linux/clk.h>
#include <linux/usb/r8a66597.h>

#define R8A66597_MAX_SAMPLING	10

#define R8A66597_MAX_NUM_PIPE	8
#define R8A66597_MAX_NUM_BULK	3
#define R8A66597_MAX_NUM_ISOC	2
#define R8A66597_MAX_NUM_INT	2

#define R8A66597_BASE_PIPENUM_BULK	3
#define R8A66597_BASE_PIPENUM_ISOC	1
#define R8A66597_BASE_PIPENUM_INT	6

#define R8A66597_BASE_BUFNUM	6
#define R8A66597_MAX_BUFNUM	0x4F

#define is_bulk_pipe(pipenum)	\
	((pipenum >= R8A66597_BASE_PIPENUM_BULK) && \
	 (pipenum < (R8A66597_BASE_PIPENUM_BULK + R8A66597_MAX_NUM_BULK)))
#define is_interrupt_pipe(pipenum)	\
	((pipenum >= R8A66597_BASE_PIPENUM_INT) && \
	 (pipenum < (R8A66597_BASE_PIPENUM_INT + R8A66597_MAX_NUM_INT)))
#define is_isoc_pipe(pipenum)	\
	((pipenum >= R8A66597_BASE_PIPENUM_ISOC) && \
	 (pipenum < (R8A66597_BASE_PIPENUM_ISOC + R8A66597_MAX_NUM_ISOC)))

#define r8a66597_is_sudmac(r8a66597)	(r8a66597->pdata->sudmac)
struct r8a66597_pipe_info {
	u16	pipe;
	u16	epnum;
	u16	maxpacket;
	u16	type;
	u16	interval;
	u16	dir_in;
};

struct r8a66597_request {
	struct usb_request	req;
	struct list_head	queue;
};

struct r8a66597_ep {
	struct usb_ep		ep;
	struct r8a66597		*r8a66597;
	struct r8a66597_dma	*dma;

	struct list_head	queue;
	unsigned		busy:1;
	unsigned		wedge:1;
	unsigned		internal_ccpl:1;	/* use only control */

	/* this member can able to after r8a66597_enable */
	unsigned		use_dma:1;
	u16			pipenum;
	u16			type;

	/* register address */
	unsigned char		fifoaddr;
	unsigned char		fifosel;
	unsigned char		fifoctr;
	unsigned char		pipectr;
	unsigned char		pipetre;
	unsigned char		pipetrn;
};

struct r8a66597_dma {
	unsigned		used:1;
	unsigned		dir:1;	/* 1 = IN(write), 0 = OUT(read) */
};

struct r8a66597 {
	spinlock_t		lock;
	void __iomem		*reg;
	void __iomem		*sudmac_reg;

	struct clk *clk;
	struct r8a66597_platdata	*pdata;

	struct usb_gadget		gadget;
	struct usb_gadget_driver	*driver;

	struct r8a66597_ep	ep[R8A66597_MAX_NUM_PIPE];
	struct r8a66597_ep	*pipenum2ep[R8A66597_MAX_NUM_PIPE];
	struct r8a66597_ep	*epaddr2ep[16];
	struct r8a66597_dma	dma;

	struct timer_list	timer;
	struct usb_request	*ep0_req;	/* for internal request */
	u16			ep0_data;	/* for internal request */
	u16			old_vbus;
	u16			scount;
	u16			old_dvsq;
	u16			device_status;	/* for GET_STATUS */

	/* pipe config */
	unsigned char bulk;
	unsigned char interrupt;
	unsigned char isochronous;
	unsigned char num_dma;

	unsigned irq_sense_low:1;
};

#define gadget_to_r8a66597(_gadget)	\
		container_of(_gadget, struct r8a66597, gadget)
#define r8a66597_to_gadget(r8a66597) (&r8a66597->gadget)
#define r8a66597_to_dev(r8a66597)	(r8a66597->gadget.dev.parent)

static inline u16 r8a66597_read(struct r8a66597 *r8a66597, unsigned long offset)
{
	return ioread16(r8a66597->reg + offset);
}

static inline void r8a66597_read_fifo(struct r8a66597 *r8a66597,
				      unsigned long offset,
				      unsigned char *buf,
				      int len)
{
	void __iomem *fifoaddr = r8a66597->reg + offset;
	unsigned int data = 0;
	int i;

	if (r8a66597->pdata->on_chip) {
		/* 32-bit accesses for on_chip controllers */

		/* aligned buf case */
		if (len >= 4 && !((unsigned long)buf & 0x03)) {
			ioread32_rep(fifoaddr, buf, len / 4);
			buf += len & ~0x03;
			len &= 0x03;
		}

		/* unaligned buf case */
		for (i = 0; i < len; i++) {
			if (!(i & 0x03))
				data = ioread32(fifoaddr);

			buf[i] = (data >> ((i & 0x03) * 8)) & 0xff;
		}
	} else {
		/* 16-bit accesses for external controllers */

		/* aligned buf case */
		if (len >= 2 && !((unsigned long)buf & 0x01)) {
			ioread16_rep(fifoaddr, buf, len / 2);
			buf += len & ~0x01;
			len &= 0x01;
		}

		/* unaligned buf case */
		for (i = 0; i < len; i++) {
			if (!(i & 0x01))
				data = ioread16(fifoaddr);

			buf[i] = (data >> ((i & 0x01) * 8)) & 0xff;
		}
	}
}

static inline void r8a66597_write(struct r8a66597 *r8a66597, u16 val,
				  unsigned long offset)
{
	iowrite16(val, r8a66597->reg + offset);
}

static inline void r8a66597_mdfy(struct r8a66597 *r8a66597,
				 u16 val, u16 pat, unsigned long offset)
{
	u16 tmp;
	tmp = r8a66597_read(r8a66597, offset);
	tmp = tmp & (~pat);
	tmp = tmp | val;
	r8a66597_write(r8a66597, tmp, offset);
}

#define r8a66597_bclr(r8a66597, val, offset)	\
			r8a66597_mdfy(r8a66597, 0, val, offset)
#define r8a66597_bset(r8a66597, val, offset)	\
			r8a66597_mdfy(r8a66597, val, 0, offset)

static inline void r8a66597_write_fifo(struct r8a66597 *r8a66597,
				       struct r8a66597_ep *ep,
				       unsigned char *buf,
				       int len)
{
	void __iomem *fifoaddr = r8a66597->reg + ep->fifoaddr;
	int adj = 0;
	int i;

	if (r8a66597->pdata->on_chip) {
		/* 32-bit access only if buf is 32-bit aligned */
		if (len >= 4 && !((unsigned long)buf & 0x03)) {
			iowrite32_rep(fifoaddr, buf, len / 4);
			buf += len & ~0x03;
			len &= 0x03;
		}
	} else {
		/* 16-bit access only if buf is 16-bit aligned */
		if (len >= 2 && !((unsigned long)buf & 0x01)) {
			iowrite16_rep(fifoaddr, buf, len / 2);
			buf += len & ~0x01;
			len &= 0x01;
		}
	}

	/* adjust fifo address in the little endian case */
	if (!(r8a66597_read(r8a66597, CFIFOSEL) & BIGEND)) {
		if (r8a66597->pdata->on_chip)
			adj = 0x03; /* 32-bit wide */
		else
			adj = 0x01; /* 16-bit wide */
	}

	if (r8a66597->pdata->wr0_shorted_to_wr1)
		r8a66597_bclr(r8a66597, MBW_16, ep->fifosel);
	for (i = 0; i < len; i++)
		iowrite8(buf[i], fifoaddr + adj - (i & adj));
	if (r8a66597->pdata->wr0_shorted_to_wr1)
		r8a66597_bclr(r8a66597, MBW_16, ep->fifosel);
}

static inline u16 get_xtal_from_pdata(struct r8a66597_platdata *pdata)
{
	u16 clock = 0;

	switch (pdata->xtal) {
	case R8A66597_PLATDATA_XTAL_12MHZ:
		clock = XTAL12;
		break;
	case R8A66597_PLATDATA_XTAL_24MHZ:
		clock = XTAL24;
		break;
	case R8A66597_PLATDATA_XTAL_48MHZ:
		clock = XTAL48;
		break;
	default:
		printk(KERN_ERR "r8a66597: platdata clock is wrong.\n");
		break;
	}

	return clock;
}

static inline u32 r8a66597_sudmac_read(struct r8a66597 *r8a66597,
				       unsigned long offset)
{
	return ioread32(r8a66597->sudmac_reg + offset);
}

static inline void r8a66597_sudmac_write(struct r8a66597 *r8a66597, u32 val,
					 unsigned long offset)
{
	iowrite32(val, r8a66597->sudmac_reg + offset);
}

#define get_pipectr_addr(pipenum)	(PIPE1CTR + (pipenum - 1) * 2)
#define get_pipetre_addr(pipenum)	(PIPE1TRE + (pipenum - 1) * 4)
#define get_pipetrn_addr(pipenum)	(PIPE1TRN + (pipenum - 1) * 4)

#define enable_irq_ready(r8a66597, pipenum)	\
	enable_pipe_irq(r8a66597, pipenum, BRDYENB)
#define disable_irq_ready(r8a66597, pipenum)	\
	disable_pipe_irq(r8a66597, pipenum, BRDYENB)
#define enable_irq_empty(r8a66597, pipenum)	\
	enable_pipe_irq(r8a66597, pipenum, BEMPENB)
#define disable_irq_empty(r8a66597, pipenum)	\
	disable_pipe_irq(r8a66597, pipenum, BEMPENB)
#define enable_irq_nrdy(r8a66597, pipenum)	\
	enable_pipe_irq(r8a66597, pipenum, NRDYENB)
#define disable_irq_nrdy(r8a66597, pipenum)	\
	disable_pipe_irq(r8a66597, pipenum, NRDYENB)

#endif	/* __R8A66597_H__ */

