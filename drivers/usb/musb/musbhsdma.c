// SPDX-License-Identifier: GPL-2.0
/*
 * MUSB OTG driver - support for Mentor's DMA controller
 *
 * Copyright 2005 Mentor Graphics Corporation
 * Copyright (C) 2005-2007 by Texas Instruments
 */
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "musb_core.h"

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

static void dma_channel_release(struct dma_channel *channel);

static void dma_controller_stop(struct musb_dma_controller *controller)
{
	struct musb *musb = controller->private_data;
	struct dma_channel *channel;
	u8 bit;

	if (controller->used_channels != 0) {
		dev_err(musb->controller,
			"Stopping DMA controller while channel active\n");

		for (bit = 0; bit < MUSB_HSDMA_CHANNELS; bit++) {
			if (controller->used_channels & (1 << bit)) {
				channel = &controller->channel[bit].channel;
				dma_channel_release(channel);

				if (!controller->used_channels)
					break;
			}
		}
	}
}

static struct dma_channel *dma_channel_allocate(struct dma_controller *c,
				struct musb_hw_ep *hw_ep, u8 transmit)
{
	struct musb_dma_controller *controller = container_of(c,
			struct musb_dma_controller, controller);
	struct musb_dma_channel *musb_channel = NULL;
	struct dma_channel *channel = NULL;
	u8 bit;

	for (bit = 0; bit < MUSB_HSDMA_CHANNELS; bit++) {
		if (!(controller->used_channels & (1 << bit))) {
			controller->used_channels |= (1 << bit);
			musb_channel = &(controller->channel[bit]);
			musb_channel->controller = controller;
			musb_channel->idx = bit;
			musb_channel->epnum = hw_ep->epnum;
			musb_channel->transmit = transmit;
			channel = &(musb_channel->channel);
			channel->private_data = musb_channel;
			channel->status = MUSB_DMA_STATUS_FREE;
			channel->max_len = 0x100000;
			/* Tx => mode 1; Rx => mode 0 */
			channel->desired_mode = transmit;
			channel->actual_len = 0;
			break;
		}
	}

	return channel;
}

static void dma_channel_release(struct dma_channel *channel)
{
	struct musb_dma_channel *musb_channel = channel->private_data;

	channel->actual_len = 0;
	musb_channel->start_addr = 0;
	musb_channel->len = 0;

	musb_channel->controller->used_channels &=
		~(1 << musb_channel->idx);

	channel->status = MUSB_DMA_STATUS_UNKNOWN;
}

static void configure_channel(struct dma_channel *channel,
				u16 packet_sz, u8 mode,
				dma_addr_t dma_addr, u32 len)
{
	struct musb_dma_channel *musb_channel = channel->private_data;
	struct musb_dma_controller *controller = musb_channel->controller;
	struct musb *musb = controller->private_data;
	void __iomem *mbase = controller->base;
	u8 bchannel = musb_channel->idx;
	u16 csr = 0;

	musb_dbg(musb, "%p, pkt_sz %d, addr %pad, len %d, mode %d",
			channel, packet_sz, &dma_addr, len, mode);

	if (mode) {
		csr |= 1 << MUSB_HSDMA_MODE1_SHIFT;
		BUG_ON(len < packet_sz);
	}
	csr |= MUSB_HSDMA_BURSTMODE_INCR16
				<< MUSB_HSDMA_BURSTMODE_SHIFT;

	csr |= (musb_channel->epnum << MUSB_HSDMA_ENDPOINT_SHIFT)
		| (1 << MUSB_HSDMA_ENABLE_SHIFT)
		| (1 << MUSB_HSDMA_IRQENABLE_SHIFT)
		| (musb_channel->transmit
				? (1 << MUSB_HSDMA_TRANSMIT_SHIFT)
				: 0);

	/* address/count */
	musb_write_hsdma_addr(mbase, bchannel, dma_addr);
	musb_write_hsdma_count(mbase, bchannel, len);

	/* control (this should start things) */
	musb_writew(mbase,
		MUSB_HSDMA_CHANNEL_OFFSET(bchannel, MUSB_HSDMA_CONTROL),
		csr);
}

static int dma_channel_program(struct dma_channel *channel,
				u16 packet_sz, u8 mode,
				dma_addr_t dma_addr, u32 len)
{
	struct musb_dma_channel *musb_channel = channel->private_data;
	struct musb_dma_controller *controller = musb_channel->controller;
	struct musb *musb = controller->private_data;

	musb_dbg(musb, "ep%d-%s pkt_sz %d, dma_addr %pad length %d, mode %d",
		musb_channel->epnum,
		musb_channel->transmit ? "Tx" : "Rx",
		packet_sz, &dma_addr, len, mode);

	BUG_ON(channel->status == MUSB_DMA_STATUS_UNKNOWN ||
		channel->status == MUSB_DMA_STATUS_BUSY);

	/*
	 * The DMA engine in RTL1.8 and above cannot handle
	 * DMA addresses that are not aligned to a 4 byte boundary.
	 * It ends up masking the last two bits of the address
	 * programmed in DMA_ADDR.
	 *
	 * Fail such DMA transfers, so that the backup PIO mode
	 * can carry out the transfer
	 */
	if ((musb->hwvers >= MUSB_HWVERS_1800) && (dma_addr % 4))
		return false;

	channel->actual_len = 0;
	musb_channel->start_addr = dma_addr;
	musb_channel->len = len;
	musb_channel->max_packet_sz = packet_sz;
	channel->status = MUSB_DMA_STATUS_BUSY;

	configure_channel(channel, packet_sz, mode, dma_addr, len);

	return true;
}

static int dma_channel_abort(struct dma_channel *channel)
{
	struct musb_dma_channel *musb_channel = channel->private_data;
	void __iomem *mbase = musb_channel->controller->base;
	struct musb *musb = musb_channel->controller->private_data;

	u8 bchannel = musb_channel->idx;
	int offset;
	u16 csr;

	if (channel->status == MUSB_DMA_STATUS_BUSY) {
		if (musb_channel->transmit) {
			offset = musb->io.ep_offset(musb_channel->epnum,
						MUSB_TXCSR);

			/*
			 * The programming guide says that we must clear
			 * the DMAENAB bit before the DMAMODE bit...
			 */
			csr = musb_readw(mbase, offset);
			csr &= ~(MUSB_TXCSR_AUTOSET | MUSB_TXCSR_DMAENAB);
			musb_writew(mbase, offset, csr);
			csr &= ~MUSB_TXCSR_DMAMODE;
			musb_writew(mbase, offset, csr);
		} else {
			offset = musb->io.ep_offset(musb_channel->epnum,
						MUSB_RXCSR);

			csr = musb_readw(mbase, offset);
			csr &= ~(MUSB_RXCSR_AUTOCLEAR |
				 MUSB_RXCSR_DMAENAB |
				 MUSB_RXCSR_DMAMODE);
			musb_writew(mbase, offset, csr);
		}

		musb_writew(mbase,
			MUSB_HSDMA_CHANNEL_OFFSET(bchannel, MUSB_HSDMA_CONTROL),
			0);
		musb_write_hsdma_addr(mbase, bchannel, 0);
		musb_write_hsdma_count(mbase, bchannel, 0);
		channel->status = MUSB_DMA_STATUS_FREE;
	}

	return 0;
}

static irqreturn_t dma_controller_irq(int irq, void *private_data)
{
	struct musb_dma_controller *controller = private_data;
	struct musb *musb = controller->private_data;
	struct musb_dma_channel *musb_channel;
	struct dma_channel *channel;

	void __iomem *mbase = controller->base;

	irqreturn_t retval = IRQ_NONE;

	unsigned long flags;

	u8 bchannel;
	u8 int_hsdma;

	u32 addr, count;
	u16 csr;

	spin_lock_irqsave(&musb->lock, flags);

	int_hsdma = musb_readb(mbase, MUSB_HSDMA_INTR);

	if (!int_hsdma) {
		musb_dbg(musb, "spurious DMA irq");

		for (bchannel = 0; bchannel < MUSB_HSDMA_CHANNELS; bchannel++) {
			musb_channel = (struct musb_dma_channel *)
					&(controller->channel[bchannel]);
			channel = &musb_channel->channel;
			if (channel->status == MUSB_DMA_STATUS_BUSY) {
				count = musb_read_hsdma_count(mbase, bchannel);

				if (count == 0)
					int_hsdma |= (1 << bchannel);
			}
		}

		musb_dbg(musb, "int_hsdma = 0x%x", int_hsdma);

		if (!int_hsdma)
			goto done;
	}

	for (bchannel = 0; bchannel < MUSB_HSDMA_CHANNELS; bchannel++) {
		if (int_hsdma & (1 << bchannel)) {
			musb_channel = (struct musb_dma_channel *)
					&(controller->channel[bchannel]);
			channel = &musb_channel->channel;

			csr = musb_readw(mbase,
					MUSB_HSDMA_CHANNEL_OFFSET(bchannel,
							MUSB_HSDMA_CONTROL));

			if (csr & (1 << MUSB_HSDMA_BUSERROR_SHIFT)) {
				musb_channel->channel.status =
					MUSB_DMA_STATUS_BUS_ABORT;
			} else {
				u8 devctl;

				addr = musb_read_hsdma_addr(mbase,
						bchannel);
				channel->actual_len = addr
					- musb_channel->start_addr;

				musb_dbg(musb, "ch %p, 0x%x -> 0x%x (%zu / %d) %s",
					channel, musb_channel->start_addr,
					addr, channel->actual_len,
					musb_channel->len,
					(channel->actual_len
						< musb_channel->len) ?
					"=> reconfig 0" : "=> complete");

				devctl = musb_readb(mbase, MUSB_DEVCTL);

				channel->status = MUSB_DMA_STATUS_FREE;

				/* completed */
				if ((devctl & MUSB_DEVCTL_HM)
					&& (musb_channel->transmit)
					&& ((channel->desired_mode == 0)
					    || (channel->actual_len &
					    (musb_channel->max_packet_sz - 1)))
				    ) {
					u8  epnum  = musb_channel->epnum;
					int offset = musb->io.ep_offset(epnum,
								    MUSB_TXCSR);
					u16 txcsr;

					/*
					 * The programming guide says that we
					 * must clear DMAENAB before DMAMODE.
					 */
					musb_ep_select(mbase, epnum);
					txcsr = musb_readw(mbase, offset);
					txcsr &= ~(MUSB_TXCSR_DMAENAB
							| MUSB_TXCSR_AUTOSET);
					musb_writew(mbase, offset, txcsr);
					/* Send out the packet */
					txcsr &= ~MUSB_TXCSR_DMAMODE;
					txcsr |=  MUSB_TXCSR_TXPKTRDY;
					musb_writew(mbase, offset, txcsr);
				}
				musb_dma_completion(musb, musb_channel->epnum,
						    musb_channel->transmit);
			}
		}
	}

	retval = IRQ_HANDLED;
done:
	spin_unlock_irqrestore(&musb->lock, flags);
	return retval;
}

void musbhs_dma_controller_destroy(struct dma_controller *c)
{
	struct musb_dma_controller *controller = container_of(c,
			struct musb_dma_controller, controller);

	dma_controller_stop(controller);

	if (controller->irq)
		free_irq(controller->irq, c);

	kfree(controller);
}
EXPORT_SYMBOL_GPL(musbhs_dma_controller_destroy);

struct dma_controller *musbhs_dma_controller_create(struct musb *musb,
						    void __iomem *base)
{
	struct musb_dma_controller *controller;
	struct device *dev = musb->controller;
	struct platform_device *pdev = to_platform_device(dev);
	int irq = platform_get_irq_byname(pdev, "dma");

	if (irq <= 0) {
		dev_err(dev, "No DMA interrupt line!\n");
		return NULL;
	}

	controller = kzalloc(sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return NULL;

	controller->channel_count = MUSB_HSDMA_CHANNELS;
	controller->private_data = musb;
	controller->base = base;

	controller->controller.channel_alloc = dma_channel_allocate;
	controller->controller.channel_release = dma_channel_release;
	controller->controller.channel_program = dma_channel_program;
	controller->controller.channel_abort = dma_channel_abort;

	if (request_irq(irq, dma_controller_irq, 0,
			dev_name(musb->controller), &controller->controller)) {
		dev_err(dev, "request_irq %d failed!\n", irq);
		musb_dma_controller_destroy(&controller->controller);

		return NULL;
	}

	controller->irq = irq;

	return &controller->controller;
}
EXPORT_SYMBOL_GPL(musbhs_dma_controller_create);
