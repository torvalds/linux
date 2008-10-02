/*
 * TUSB6010 USB 2.0 OTG Dual Role controller OMAP DMA interface
 *
 * Copyright (C) 2006 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <asm/arch/dma.h>
#include <asm/arch/mux.h>

#include "musb_core.h"

#define to_chdat(c)		((struct tusb_omap_dma_ch *)(c)->private_data)

#define MAX_DMAREQ		5	/* REVISIT: Really 6, but req5 not OK */

struct tusb_omap_dma_ch {
	struct musb		*musb;
	void __iomem		*tbase;
	unsigned long		phys_offset;
	int			epnum;
	u8			tx;
	struct musb_hw_ep	*hw_ep;

	int			ch;
	s8			dmareq;
	s8			sync_dev;

	struct tusb_omap_dma	*tusb_dma;

	void __iomem		*dma_addr;

	u32			len;
	u16			packet_sz;
	u16			transfer_packet_sz;
	u32			transfer_len;
	u32			completed_len;
};

struct tusb_omap_dma {
	struct dma_controller		controller;
	struct musb			*musb;
	void __iomem			*tbase;

	int				ch;
	s8				dmareq;
	s8				sync_dev;
	unsigned			multichannel:1;
};

static int tusb_omap_dma_start(struct dma_controller *c)
{
	struct tusb_omap_dma	*tusb_dma;

	tusb_dma = container_of(c, struct tusb_omap_dma, controller);

	/* DBG(3, "ep%i ch: %i\n", chdat->epnum, chdat->ch); */

	return 0;
}

static int tusb_omap_dma_stop(struct dma_controller *c)
{
	struct tusb_omap_dma	*tusb_dma;

	tusb_dma = container_of(c, struct tusb_omap_dma, controller);

	/* DBG(3, "ep%i ch: %i\n", chdat->epnum, chdat->ch); */

	return 0;
}

/*
 * Allocate dmareq0 to the current channel unless it's already taken
 */
static inline int tusb_omap_use_shared_dmareq(struct tusb_omap_dma_ch *chdat)
{
	u32		reg = musb_readl(chdat->tbase, TUSB_DMA_EP_MAP);

	if (reg != 0) {
		DBG(3, "ep%i dmareq0 is busy for ep%i\n",
			chdat->epnum, reg & 0xf);
		return -EAGAIN;
	}

	if (chdat->tx)
		reg = (1 << 4) | chdat->epnum;
	else
		reg = chdat->epnum;

	musb_writel(chdat->tbase, TUSB_DMA_EP_MAP, reg);

	return 0;
}

static inline void tusb_omap_free_shared_dmareq(struct tusb_omap_dma_ch *chdat)
{
	u32		reg = musb_readl(chdat->tbase, TUSB_DMA_EP_MAP);

	if ((reg & 0xf) != chdat->epnum) {
		printk(KERN_ERR "ep%i trying to release dmareq0 for ep%i\n",
			chdat->epnum, reg & 0xf);
		return;
	}
	musb_writel(chdat->tbase, TUSB_DMA_EP_MAP, 0);
}

/*
 * See also musb_dma_completion in plat_uds.c and musb_g_[tx|rx]() in
 * musb_gadget.c.
 */
static void tusb_omap_dma_cb(int lch, u16 ch_status, void *data)
{
	struct dma_channel	*channel = (struct dma_channel *)data;
	struct tusb_omap_dma_ch	*chdat = to_chdat(channel);
	struct tusb_omap_dma	*tusb_dma = chdat->tusb_dma;
	struct musb		*musb = chdat->musb;
	struct musb_hw_ep	*hw_ep = chdat->hw_ep;
	void __iomem		*ep_conf = hw_ep->conf;
	void __iomem		*mbase = musb->mregs;
	unsigned long		remaining, flags, pio;
	int			ch;

	spin_lock_irqsave(&musb->lock, flags);

	if (tusb_dma->multichannel)
		ch = chdat->ch;
	else
		ch = tusb_dma->ch;

	if (ch_status != OMAP_DMA_BLOCK_IRQ)
		printk(KERN_ERR "TUSB DMA error status: %i\n", ch_status);

	DBG(3, "ep%i %s dma callback ch: %i status: %x\n",
		chdat->epnum, chdat->tx ? "tx" : "rx",
		ch, ch_status);

	if (chdat->tx)
		remaining = musb_readl(ep_conf, TUSB_EP_TX_OFFSET);
	else
		remaining = musb_readl(ep_conf, TUSB_EP_RX_OFFSET);

	remaining = TUSB_EP_CONFIG_XFR_SIZE(remaining);

	/* HW issue #10: XFR_SIZE may get corrupt on DMA (both async & sync) */
	if (unlikely(remaining > chdat->transfer_len)) {
		DBG(2, "Corrupt %s dma ch%i XFR_SIZE: 0x%08lx\n",
			chdat->tx ? "tx" : "rx", chdat->ch,
			remaining);
		remaining = 0;
	}

	channel->actual_len = chdat->transfer_len - remaining;
	pio = chdat->len - channel->actual_len;

	DBG(3, "DMA remaining %lu/%u\n", remaining, chdat->transfer_len);

	/* Transfer remaining 1 - 31 bytes */
	if (pio > 0 && pio < 32) {
		u8	*buf;

		DBG(3, "Using PIO for remaining %lu bytes\n", pio);
		buf = phys_to_virt((u32)chdat->dma_addr) + chdat->transfer_len;
		if (chdat->tx) {
			dma_cache_maint(phys_to_virt((u32)chdat->dma_addr),
					chdat->transfer_len, DMA_TO_DEVICE);
			musb_write_fifo(hw_ep, pio, buf);
		} else {
			musb_read_fifo(hw_ep, pio, buf);
			dma_cache_maint(phys_to_virt((u32)chdat->dma_addr),
					chdat->transfer_len, DMA_FROM_DEVICE);
		}
		channel->actual_len += pio;
	}

	if (!tusb_dma->multichannel)
		tusb_omap_free_shared_dmareq(chdat);

	channel->status = MUSB_DMA_STATUS_FREE;

	/* Handle only RX callbacks here. TX callbacks must be handled based
	 * on the TUSB DMA status interrupt.
	 * REVISIT: Use both TUSB DMA status interrupt and OMAP DMA callback
	 * interrupt for RX and TX.
	 */
	if (!chdat->tx)
		musb_dma_completion(musb, chdat->epnum, chdat->tx);

	/* We must terminate short tx transfers manually by setting TXPKTRDY.
	 * REVISIT: This same problem may occur with other MUSB dma as well.
	 * Easy to test with g_ether by pinging the MUSB board with ping -s54.
	 */
	if ((chdat->transfer_len < chdat->packet_sz)
			|| (chdat->transfer_len % chdat->packet_sz != 0)) {
		u16	csr;

		if (chdat->tx) {
			DBG(3, "terminating short tx packet\n");
			musb_ep_select(mbase, chdat->epnum);
			csr = musb_readw(hw_ep->regs, MUSB_TXCSR);
			csr |= MUSB_TXCSR_MODE | MUSB_TXCSR_TXPKTRDY
				| MUSB_TXCSR_P_WZC_BITS;
			musb_writew(hw_ep->regs, MUSB_TXCSR, csr);
		}
	}

	spin_unlock_irqrestore(&musb->lock, flags);
}

static int tusb_omap_dma_program(struct dma_channel *channel, u16 packet_sz,
				u8 rndis_mode, dma_addr_t dma_addr, u32 len)
{
	struct tusb_omap_dma_ch		*chdat = to_chdat(channel);
	struct tusb_omap_dma		*tusb_dma = chdat->tusb_dma;
	struct musb			*musb = chdat->musb;
	struct musb_hw_ep		*hw_ep = chdat->hw_ep;
	void __iomem			*mbase = musb->mregs;
	void __iomem			*ep_conf = hw_ep->conf;
	dma_addr_t			fifo = hw_ep->fifo_sync;
	struct omap_dma_channel_params	dma_params;
	u32				dma_remaining;
	int				src_burst, dst_burst;
	u16				csr;
	int				ch;
	s8				dmareq;
	s8				sync_dev;

	if (unlikely(dma_addr & 0x1) || (len < 32) || (len > packet_sz))
		return false;

	/*
	 * HW issue #10: Async dma will eventually corrupt the XFR_SIZE
	 * register which will cause missed DMA interrupt. We could try to
	 * use a timer for the callback, but it is unsafe as the XFR_SIZE
	 * register is corrupt, and we won't know if the DMA worked.
	 */
	if (dma_addr & 0x2)
		return false;

	/*
	 * Because of HW issue #10, it seems like mixing sync DMA and async
	 * PIO access can confuse the DMA. Make sure XFR_SIZE is reset before
	 * using the channel for DMA.
	 */
	if (chdat->tx)
		dma_remaining = musb_readl(ep_conf, TUSB_EP_TX_OFFSET);
	else
		dma_remaining = musb_readl(ep_conf, TUSB_EP_RX_OFFSET);

	dma_remaining = TUSB_EP_CONFIG_XFR_SIZE(dma_remaining);
	if (dma_remaining) {
		DBG(2, "Busy %s dma ch%i, not using: %08x\n",
			chdat->tx ? "tx" : "rx", chdat->ch,
			dma_remaining);
		return false;
	}

	chdat->transfer_len = len & ~0x1f;

	if (len < packet_sz)
		chdat->transfer_packet_sz = chdat->transfer_len;
	else
		chdat->transfer_packet_sz = packet_sz;

	if (tusb_dma->multichannel) {
		ch = chdat->ch;
		dmareq = chdat->dmareq;
		sync_dev = chdat->sync_dev;
	} else {
		if (tusb_omap_use_shared_dmareq(chdat) != 0) {
			DBG(3, "could not get dma for ep%i\n", chdat->epnum);
			return false;
		}
		if (tusb_dma->ch < 0) {
			/* REVISIT: This should get blocked earlier, happens
			 * with MSC ErrorRecoveryTest
			 */
			WARN_ON(1);
			return false;
		}

		ch = tusb_dma->ch;
		dmareq = tusb_dma->dmareq;
		sync_dev = tusb_dma->sync_dev;
		omap_set_dma_callback(ch, tusb_omap_dma_cb, channel);
	}

	chdat->packet_sz = packet_sz;
	chdat->len = len;
	channel->actual_len = 0;
	chdat->dma_addr = (void __iomem *)dma_addr;
	channel->status = MUSB_DMA_STATUS_BUSY;

	/* Since we're recycling dma areas, we need to clean or invalidate */
	if (chdat->tx)
		dma_cache_maint(phys_to_virt(dma_addr), len, DMA_TO_DEVICE);
	else
		dma_cache_maint(phys_to_virt(dma_addr), len, DMA_FROM_DEVICE);

	/* Use 16-bit transfer if dma_addr is not 32-bit aligned */
	if ((dma_addr & 0x3) == 0) {
		dma_params.data_type = OMAP_DMA_DATA_TYPE_S32;
		dma_params.elem_count = 8;		/* Elements in frame */
	} else {
		dma_params.data_type = OMAP_DMA_DATA_TYPE_S16;
		dma_params.elem_count = 16;		/* Elements in frame */
		fifo = hw_ep->fifo_async;
	}

	dma_params.frame_count	= chdat->transfer_len / 32; /* Burst sz frame */

	DBG(3, "ep%i %s dma ch%i dma: %08x len: %u(%u) packet_sz: %i(%i)\n",
		chdat->epnum, chdat->tx ? "tx" : "rx",
		ch, dma_addr, chdat->transfer_len, len,
		chdat->transfer_packet_sz, packet_sz);

	/*
	 * Prepare omap DMA for transfer
	 */
	if (chdat->tx) {
		dma_params.src_amode	= OMAP_DMA_AMODE_POST_INC;
		dma_params.src_start	= (unsigned long)dma_addr;
		dma_params.src_ei	= 0;
		dma_params.src_fi	= 0;

		dma_params.dst_amode	= OMAP_DMA_AMODE_DOUBLE_IDX;
		dma_params.dst_start	= (unsigned long)fifo;
		dma_params.dst_ei	= 1;
		dma_params.dst_fi	= -31;	/* Loop 32 byte window */

		dma_params.trigger	= sync_dev;
		dma_params.sync_mode	= OMAP_DMA_SYNC_FRAME;
		dma_params.src_or_dst_synch	= 0;	/* Dest sync */

		src_burst = OMAP_DMA_DATA_BURST_16;	/* 16x32 read */
		dst_burst = OMAP_DMA_DATA_BURST_8;	/* 8x32 write */
	} else {
		dma_params.src_amode	= OMAP_DMA_AMODE_DOUBLE_IDX;
		dma_params.src_start	= (unsigned long)fifo;
		dma_params.src_ei	= 1;
		dma_params.src_fi	= -31;	/* Loop 32 byte window */

		dma_params.dst_amode	= OMAP_DMA_AMODE_POST_INC;
		dma_params.dst_start	= (unsigned long)dma_addr;
		dma_params.dst_ei	= 0;
		dma_params.dst_fi	= 0;

		dma_params.trigger	= sync_dev;
		dma_params.sync_mode	= OMAP_DMA_SYNC_FRAME;
		dma_params.src_or_dst_synch	= 1;	/* Source sync */

		src_burst = OMAP_DMA_DATA_BURST_8;	/* 8x32 read */
		dst_burst = OMAP_DMA_DATA_BURST_16;	/* 16x32 write */
	}

	DBG(3, "ep%i %s using %i-bit %s dma from 0x%08lx to 0x%08lx\n",
		chdat->epnum, chdat->tx ? "tx" : "rx",
		(dma_params.data_type == OMAP_DMA_DATA_TYPE_S32) ? 32 : 16,
		((dma_addr & 0x3) == 0) ? "sync" : "async",
		dma_params.src_start, dma_params.dst_start);

	omap_set_dma_params(ch, &dma_params);
	omap_set_dma_src_burst_mode(ch, src_burst);
	omap_set_dma_dest_burst_mode(ch, dst_burst);
	omap_set_dma_write_mode(ch, OMAP_DMA_WRITE_LAST_NON_POSTED);

	/*
	 * Prepare MUSB for DMA transfer
	 */
	if (chdat->tx) {
		musb_ep_select(mbase, chdat->epnum);
		csr = musb_readw(hw_ep->regs, MUSB_TXCSR);
		csr |= (MUSB_TXCSR_AUTOSET | MUSB_TXCSR_DMAENAB
			| MUSB_TXCSR_DMAMODE | MUSB_TXCSR_MODE);
		csr &= ~MUSB_TXCSR_P_UNDERRUN;
		musb_writew(hw_ep->regs, MUSB_TXCSR, csr);
	} else {
		musb_ep_select(mbase, chdat->epnum);
		csr = musb_readw(hw_ep->regs, MUSB_RXCSR);
		csr |= MUSB_RXCSR_DMAENAB;
		csr &= ~(MUSB_RXCSR_AUTOCLEAR | MUSB_RXCSR_DMAMODE);
		musb_writew(hw_ep->regs, MUSB_RXCSR,
			csr | MUSB_RXCSR_P_WZC_BITS);
	}

	/*
	 * Start DMA transfer
	 */
	omap_start_dma(ch);

	if (chdat->tx) {
		/* Send transfer_packet_sz packets at a time */
		musb_writel(ep_conf, TUSB_EP_MAX_PACKET_SIZE_OFFSET,
			chdat->transfer_packet_sz);

		musb_writel(ep_conf, TUSB_EP_TX_OFFSET,
			TUSB_EP_CONFIG_XFR_SIZE(chdat->transfer_len));
	} else {
		/* Receive transfer_packet_sz packets at a time */
		musb_writel(ep_conf, TUSB_EP_MAX_PACKET_SIZE_OFFSET,
			chdat->transfer_packet_sz << 16);

		musb_writel(ep_conf, TUSB_EP_RX_OFFSET,
			TUSB_EP_CONFIG_XFR_SIZE(chdat->transfer_len));
	}

	return true;
}

static int tusb_omap_dma_abort(struct dma_channel *channel)
{
	struct tusb_omap_dma_ch	*chdat = to_chdat(channel);
	struct tusb_omap_dma	*tusb_dma = chdat->tusb_dma;

	if (!tusb_dma->multichannel) {
		if (tusb_dma->ch >= 0) {
			omap_stop_dma(tusb_dma->ch);
			omap_free_dma(tusb_dma->ch);
			tusb_dma->ch = -1;
		}

		tusb_dma->dmareq = -1;
		tusb_dma->sync_dev = -1;
	}

	channel->status = MUSB_DMA_STATUS_FREE;

	return 0;
}

static inline int tusb_omap_dma_allocate_dmareq(struct tusb_omap_dma_ch *chdat)
{
	u32		reg = musb_readl(chdat->tbase, TUSB_DMA_EP_MAP);
	int		i, dmareq_nr = -1;

	const int sync_dev[6] = {
		OMAP24XX_DMA_EXT_DMAREQ0,
		OMAP24XX_DMA_EXT_DMAREQ1,
		OMAP242X_DMA_EXT_DMAREQ2,
		OMAP242X_DMA_EXT_DMAREQ3,
		OMAP242X_DMA_EXT_DMAREQ4,
		OMAP242X_DMA_EXT_DMAREQ5,
	};

	for (i = 0; i < MAX_DMAREQ; i++) {
		int cur = (reg & (0xf << (i * 5))) >> (i * 5);
		if (cur == 0) {
			dmareq_nr = i;
			break;
		}
	}

	if (dmareq_nr == -1)
		return -EAGAIN;

	reg |= (chdat->epnum << (dmareq_nr * 5));
	if (chdat->tx)
		reg |= ((1 << 4) << (dmareq_nr * 5));
	musb_writel(chdat->tbase, TUSB_DMA_EP_MAP, reg);

	chdat->dmareq = dmareq_nr;
	chdat->sync_dev = sync_dev[chdat->dmareq];

	return 0;
}

static inline void tusb_omap_dma_free_dmareq(struct tusb_omap_dma_ch *chdat)
{
	u32 reg;

	if (!chdat || chdat->dmareq < 0)
		return;

	reg = musb_readl(chdat->tbase, TUSB_DMA_EP_MAP);
	reg &= ~(0x1f << (chdat->dmareq * 5));
	musb_writel(chdat->tbase, TUSB_DMA_EP_MAP, reg);

	chdat->dmareq = -1;
	chdat->sync_dev = -1;
}

static struct dma_channel *dma_channel_pool[MAX_DMAREQ];

static struct dma_channel *
tusb_omap_dma_allocate(struct dma_controller *c,
		struct musb_hw_ep *hw_ep,
		u8 tx)
{
	int ret, i;
	const char		*dev_name;
	struct tusb_omap_dma	*tusb_dma;
	struct musb		*musb;
	void __iomem		*tbase;
	struct dma_channel	*channel = NULL;
	struct tusb_omap_dma_ch	*chdat = NULL;
	u32			reg;

	tusb_dma = container_of(c, struct tusb_omap_dma, controller);
	musb = tusb_dma->musb;
	tbase = musb->ctrl_base;

	reg = musb_readl(tbase, TUSB_DMA_INT_MASK);
	if (tx)
		reg &= ~(1 << hw_ep->epnum);
	else
		reg &= ~(1 << (hw_ep->epnum + 15));
	musb_writel(tbase, TUSB_DMA_INT_MASK, reg);

	/* REVISIT: Why does dmareq5 not work? */
	if (hw_ep->epnum == 0) {
		DBG(3, "Not allowing DMA for ep0 %s\n", tx ? "tx" : "rx");
		return NULL;
	}

	for (i = 0; i < MAX_DMAREQ; i++) {
		struct dma_channel *ch = dma_channel_pool[i];
		if (ch->status == MUSB_DMA_STATUS_UNKNOWN) {
			ch->status = MUSB_DMA_STATUS_FREE;
			channel = ch;
			chdat = ch->private_data;
			break;
		}
	}

	if (!channel)
		return NULL;

	if (tx) {
		chdat->tx = 1;
		dev_name = "TUSB transmit";
	} else {
		chdat->tx = 0;
		dev_name = "TUSB receive";
	}

	chdat->musb = tusb_dma->musb;
	chdat->tbase = tusb_dma->tbase;
	chdat->hw_ep = hw_ep;
	chdat->epnum = hw_ep->epnum;
	chdat->dmareq = -1;
	chdat->completed_len = 0;
	chdat->tusb_dma = tusb_dma;

	channel->max_len = 0x7fffffff;
	channel->desired_mode = 0;
	channel->actual_len = 0;

	if (tusb_dma->multichannel) {
		ret = tusb_omap_dma_allocate_dmareq(chdat);
		if (ret != 0)
			goto free_dmareq;

		ret = omap_request_dma(chdat->sync_dev, dev_name,
				tusb_omap_dma_cb, channel, &chdat->ch);
		if (ret != 0)
			goto free_dmareq;
	} else if (tusb_dma->ch == -1) {
		tusb_dma->dmareq = 0;
		tusb_dma->sync_dev = OMAP24XX_DMA_EXT_DMAREQ0;

		/* Callback data gets set later in the shared dmareq case */
		ret = omap_request_dma(tusb_dma->sync_dev, "TUSB shared",
				tusb_omap_dma_cb, NULL, &tusb_dma->ch);
		if (ret != 0)
			goto free_dmareq;

		chdat->dmareq = -1;
		chdat->ch = -1;
	}

	DBG(3, "ep%i %s dma: %s dma%i dmareq%i sync%i\n",
		chdat->epnum,
		chdat->tx ? "tx" : "rx",
		chdat->ch >= 0 ? "dedicated" : "shared",
		chdat->ch >= 0 ? chdat->ch : tusb_dma->ch,
		chdat->dmareq >= 0 ? chdat->dmareq : tusb_dma->dmareq,
		chdat->sync_dev >= 0 ? chdat->sync_dev : tusb_dma->sync_dev);

	return channel;

free_dmareq:
	tusb_omap_dma_free_dmareq(chdat);

	DBG(3, "ep%i: Could not get a DMA channel\n", chdat->epnum);
	channel->status = MUSB_DMA_STATUS_UNKNOWN;

	return NULL;
}

static void tusb_omap_dma_release(struct dma_channel *channel)
{
	struct tusb_omap_dma_ch	*chdat = to_chdat(channel);
	struct musb		*musb = chdat->musb;
	void __iomem		*tbase = musb->ctrl_base;
	u32			reg;

	DBG(3, "ep%i ch%i\n", chdat->epnum, chdat->ch);

	reg = musb_readl(tbase, TUSB_DMA_INT_MASK);
	if (chdat->tx)
		reg |= (1 << chdat->epnum);
	else
		reg |= (1 << (chdat->epnum + 15));
	musb_writel(tbase, TUSB_DMA_INT_MASK, reg);

	reg = musb_readl(tbase, TUSB_DMA_INT_CLEAR);
	if (chdat->tx)
		reg |= (1 << chdat->epnum);
	else
		reg |= (1 << (chdat->epnum + 15));
	musb_writel(tbase, TUSB_DMA_INT_CLEAR, reg);

	channel->status = MUSB_DMA_STATUS_UNKNOWN;

	if (chdat->ch >= 0) {
		omap_stop_dma(chdat->ch);
		omap_free_dma(chdat->ch);
		chdat->ch = -1;
	}

	if (chdat->dmareq >= 0)
		tusb_omap_dma_free_dmareq(chdat);

	channel = NULL;
}

void dma_controller_destroy(struct dma_controller *c)
{
	struct tusb_omap_dma	*tusb_dma;
	int			i;

	tusb_dma = container_of(c, struct tusb_omap_dma, controller);
	for (i = 0; i < MAX_DMAREQ; i++) {
		struct dma_channel *ch = dma_channel_pool[i];
		if (ch) {
			kfree(ch->private_data);
			kfree(ch);
		}
	}

	if (!tusb_dma->multichannel && tusb_dma && tusb_dma->ch >= 0)
		omap_free_dma(tusb_dma->ch);

	kfree(tusb_dma);
}

struct dma_controller *__init
dma_controller_create(struct musb *musb, void __iomem *base)
{
	void __iomem		*tbase = musb->ctrl_base;
	struct tusb_omap_dma	*tusb_dma;
	int			i;

	/* REVISIT: Get dmareq lines used from board-*.c */

	musb_writel(musb->ctrl_base, TUSB_DMA_INT_MASK, 0x7fffffff);
	musb_writel(musb->ctrl_base, TUSB_DMA_EP_MAP, 0);

	musb_writel(tbase, TUSB_DMA_REQ_CONF,
		TUSB_DMA_REQ_CONF_BURST_SIZE(2)
		| TUSB_DMA_REQ_CONF_DMA_REQ_EN(0x3f)
		| TUSB_DMA_REQ_CONF_DMA_REQ_ASSER(2));

	tusb_dma = kzalloc(sizeof(struct tusb_omap_dma), GFP_KERNEL);
	if (!tusb_dma)
		goto cleanup;

	tusb_dma->musb = musb;
	tusb_dma->tbase = musb->ctrl_base;

	tusb_dma->ch = -1;
	tusb_dma->dmareq = -1;
	tusb_dma->sync_dev = -1;

	tusb_dma->controller.start = tusb_omap_dma_start;
	tusb_dma->controller.stop = tusb_omap_dma_stop;
	tusb_dma->controller.channel_alloc = tusb_omap_dma_allocate;
	tusb_dma->controller.channel_release = tusb_omap_dma_release;
	tusb_dma->controller.channel_program = tusb_omap_dma_program;
	tusb_dma->controller.channel_abort = tusb_omap_dma_abort;

	if (tusb_get_revision(musb) >= TUSB_REV_30)
		tusb_dma->multichannel = 1;

	for (i = 0; i < MAX_DMAREQ; i++) {
		struct dma_channel	*ch;
		struct tusb_omap_dma_ch	*chdat;

		ch = kzalloc(sizeof(struct dma_channel), GFP_KERNEL);
		if (!ch)
			goto cleanup;

		dma_channel_pool[i] = ch;

		chdat = kzalloc(sizeof(struct tusb_omap_dma_ch), GFP_KERNEL);
		if (!chdat)
			goto cleanup;

		ch->status = MUSB_DMA_STATUS_UNKNOWN;
		ch->private_data = chdat;
	}

	return &tusb_dma->controller;

cleanup:
	dma_controller_destroy(&tusb_dma->controller);

	return NULL;
}
