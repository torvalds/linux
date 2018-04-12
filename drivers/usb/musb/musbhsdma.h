// SPDX-License-Identifier: GPL-2.0
/*
 * MUSB OTG driver - support for Mentor's DMA controller
 *
 * Copyright 2005 Mentor Graphics Corporation
 * Copyright (C) 2005-2007 by Texas Instruments
 */

#define MUSB_HSDMA_BASE		0x200
#define MUSB_HSDMA_INTR		(MUSB_HSDMA_BASE + 0)
#define MUSB_HSDMA_CONTROL		0x4
#define MUSB_HSDMA_ADDRESS		0x8
#define MUSB_HSDMA_COUNT		0xc

#define MUSB_HSDMA_CHANNEL_OFFSET(_bchannel, _offset)		\
		(MUSB_HSDMA_BASE + (_bchannel << 4) + _offset)

#define musb_read_hsdma_addr(mbase, bchannel)	\
	musb_readl(mbase,	\
		   MUSB_HSDMA_CHANNEL_OFFSET(bchannel, MUSB_HSDMA_ADDRESS))

#define musb_write_hsdma_addr(mbase, bchannel, addr) \
	musb_writel(mbase, \
		    MUSB_HSDMA_CHANNEL_OFFSET(bchannel, MUSB_HSDMA_ADDRESS), \
		    addr)

#define musb_read_hsdma_count(mbase, bchannel)	\
	musb_readl(mbase,	\
		   MUSB_HSDMA_CHANNEL_OFFSET(bchannel, MUSB_HSDMA_COUNT))

#define musb_write_hsdma_count(mbase, bchannel, len) \
	musb_writel(mbase, \
		    MUSB_HSDMA_CHANNEL_OFFSET(bchannel, MUSB_HSDMA_COUNT), \
		    len)
/* control register (16-bit): */
#define MUSB_HSDMA_ENABLE_SHIFT		0
#define MUSB_HSDMA_TRANSMIT_SHIFT	1
#define MUSB_HSDMA_MODE1_SHIFT		2
#define MUSB_HSDMA_IRQENABLE_SHIFT	3
#define MUSB_HSDMA_ENDPOINT_SHIFT	4
#define MUSB_HSDMA_BUSERROR_SHIFT	8
#define MUSB_HSDMA_BURSTMODE_SHIFT	9
#define MUSB_HSDMA_BURSTMODE		(3 << MUSB_HSDMA_BURSTMODE_SHIFT)
#define MUSB_HSDMA_BURSTMODE_UNSPEC	0
#define MUSB_HSDMA_BURSTMODE_INCR4	1
#define MUSB_HSDMA_BURSTMODE_INCR8	2
#define MUSB_HSDMA_BURSTMODE_INCR16	3

#define MUSB_HSDMA_CHANNELS		8

struct musb_dma_controller;

struct musb_dma_channel {
	struct dma_channel		channel;
	struct musb_dma_controller	*controller;
	u32				start_addr;
	u32				len;
	u16				max_packet_sz;
	u8				idx;
	u8				epnum;
	u8				transmit;
};

struct musb_dma_controller {
	struct dma_controller		controller;
	struct musb_dma_channel		channel[MUSB_HSDMA_CHANNELS];
	void				*private_data;
	void __iomem			*base;
	u8				channel_count;
	u8				used_channels;
	int				irq;
};
