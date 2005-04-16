/*
 * arch/sh/drivers/dma/dma-sh.c
 *
 * SuperH On-chip DMAC Support
 *
 * Copyright (C) 2000 Takashi YOSHII
 * Copyright (C) 2003, 2004 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/signal.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/io.h>
#include "dma-sh.h"

/*
 * The SuperH DMAC supports a number of transmit sizes, we list them here,
 * with their respective values as they appear in the CHCR registers.
 *
 * Defaults to a 64-bit transfer size.
 */
enum {
	XMIT_SZ_64BIT,
	XMIT_SZ_8BIT,
	XMIT_SZ_16BIT,
	XMIT_SZ_32BIT,
	XMIT_SZ_256BIT,
};

/*
 * The DMA count is defined as the number of bytes to transfer.
 */
static unsigned int ts_shift[] = {
	[XMIT_SZ_64BIT]		= 3,
	[XMIT_SZ_8BIT]		= 0,
	[XMIT_SZ_16BIT]		= 1,
	[XMIT_SZ_32BIT]		= 2,
	[XMIT_SZ_256BIT]	= 5,
};

static inline unsigned int get_dmte_irq(unsigned int chan)
{
	unsigned int irq;

	/*
	 * Normally we could just do DMTE0_IRQ + chan outright, though in the
	 * case of the 7751R, the DMTE IRQs for channels > 4 start right above
	 * the SCIF
	 */

	if (chan < 4) {
		irq = DMTE0_IRQ + chan;
	} else {
		irq = DMTE4_IRQ + chan - 4;
	}

	return irq;
}

/*
 * We determine the correct shift size based off of the CHCR transmit size
 * for the given channel. Since we know that it will take:
 *
 *	info->count >> ts_shift[transmit_size]
 *
 * iterations to complete the transfer.
 */
static inline unsigned int calc_xmit_shift(struct dma_channel *chan)
{
	u32 chcr = ctrl_inl(CHCR[chan->chan]);

	chcr >>= 4;

	return ts_shift[chcr & 0x0007];
}

/*
 * The transfer end interrupt must read the chcr register to end the
 * hardware interrupt active condition.
 * Besides that it needs to waken any waiting process, which should handle
 * setting up the next transfer.
 */
static irqreturn_t dma_tei(int irq, void *dev_id, struct pt_regs *regs)
{
	struct dma_channel *chan = (struct dma_channel *)dev_id;
	u32 chcr;

	chcr = ctrl_inl(CHCR[chan->chan]);

	if (!(chcr & CHCR_TE))
		return IRQ_NONE;

	chcr &= ~(CHCR_IE | CHCR_DE);
	ctrl_outl(chcr, CHCR[chan->chan]);

	wake_up(&chan->wait_queue);

	return IRQ_HANDLED;
}

static int sh_dmac_request_dma(struct dma_channel *chan)
{
	return request_irq(get_dmte_irq(chan->chan), dma_tei,
			   SA_INTERRUPT, "DMAC Transfer End", chan);
}

static void sh_dmac_free_dma(struct dma_channel *chan)
{
	free_irq(get_dmte_irq(chan->chan), chan);
}

static void sh_dmac_configure_channel(struct dma_channel *chan, unsigned long chcr)
{
	if (!chcr)
		chcr = RS_DUAL;

	ctrl_outl(chcr, CHCR[chan->chan]);

	chan->flags |= DMA_CONFIGURED;
}

static void sh_dmac_enable_dma(struct dma_channel *chan)
{
	int irq = get_dmte_irq(chan->chan);
	u32 chcr;

	chcr = ctrl_inl(CHCR[chan->chan]);
	chcr |= CHCR_DE | CHCR_IE;
	ctrl_outl(chcr, CHCR[chan->chan]);

	enable_irq(irq);
}

static void sh_dmac_disable_dma(struct dma_channel *chan)
{
	int irq = get_dmte_irq(chan->chan);
	u32 chcr;

	disable_irq(irq);

	chcr = ctrl_inl(CHCR[chan->chan]);
	chcr &= ~(CHCR_DE | CHCR_TE | CHCR_IE);
	ctrl_outl(chcr, CHCR[chan->chan]);
}

static int sh_dmac_xfer_dma(struct dma_channel *chan)
{
	/*
	 * If we haven't pre-configured the channel with special flags, use
	 * the defaults.
	 */
	if (!(chan->flags & DMA_CONFIGURED))
		sh_dmac_configure_channel(chan, 0);

	sh_dmac_disable_dma(chan);

	/*
	 * Single-address mode usage note!
	 *
	 * It's important that we don't accidentally write any value to SAR/DAR
	 * (this includes 0) that hasn't been directly specified by the user if
	 * we're in single-address mode.
	 *
	 * In this case, only one address can be defined, anything else will
	 * result in a DMA address error interrupt (at least on the SH-4),
	 * which will subsequently halt the transfer.
	 *
	 * Channel 2 on the Dreamcast is a special case, as this is used for
	 * cascading to the PVR2 DMAC. In this case, we still need to write
	 * SAR and DAR, regardless of value, in order for cascading to work.
	 */
	if (chan->sar || (mach_is_dreamcast() && chan->chan == 2))
		ctrl_outl(chan->sar, SAR[chan->chan]);
	if (chan->dar || (mach_is_dreamcast() && chan->chan == 2))
		ctrl_outl(chan->dar, DAR[chan->chan]);

	ctrl_outl(chan->count >> calc_xmit_shift(chan), DMATCR[chan->chan]);

	sh_dmac_enable_dma(chan);

	return 0;
}

static int sh_dmac_get_dma_residue(struct dma_channel *chan)
{
	if (!(ctrl_inl(CHCR[chan->chan]) & CHCR_DE))
		return 0;

	return ctrl_inl(DMATCR[chan->chan]) << calc_xmit_shift(chan);
}

#if defined(CONFIG_CPU_SH4)
static irqreturn_t dma_err(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long dmaor = ctrl_inl(DMAOR);

	printk("DMAE: DMAOR=%lx\n", dmaor);

	ctrl_outl(ctrl_inl(DMAOR)&~DMAOR_NMIF, DMAOR);
	ctrl_outl(ctrl_inl(DMAOR)&~DMAOR_AE, DMAOR);
	ctrl_outl(ctrl_inl(DMAOR)|DMAOR_DME, DMAOR);

	disable_irq(irq);

	return IRQ_HANDLED;
}
#endif

static struct dma_ops sh_dmac_ops = {
	.request	= sh_dmac_request_dma,
	.free		= sh_dmac_free_dma,
	.get_residue	= sh_dmac_get_dma_residue,
	.xfer		= sh_dmac_xfer_dma,
	.configure	= sh_dmac_configure_channel,
};

static struct dma_info sh_dmac_info = {
	.name		= "SuperH DMAC",
	.nr_channels	= 4,
	.ops		= &sh_dmac_ops,
	.flags		= DMAC_CHANNELS_TEI_CAPABLE,
};

static int __init sh_dmac_init(void)
{
	struct dma_info *info = &sh_dmac_info;
	int i;

#ifdef CONFIG_CPU_SH4
	make_ipr_irq(DMAE_IRQ, DMA_IPR_ADDR, DMA_IPR_POS, DMA_PRIORITY);
	i = request_irq(DMAE_IRQ, dma_err, SA_INTERRUPT, "DMAC Address Error", 0);
	if (i < 0)
		return i;
#endif

	for (i = 0; i < info->nr_channels; i++) {
		int irq = get_dmte_irq(i);

		make_ipr_irq(irq, DMA_IPR_ADDR, DMA_IPR_POS, DMA_PRIORITY);
	}

	ctrl_outl(0x8000 | DMAOR_DME, DMAOR);

	return register_dmac(info);
}

static void __exit sh_dmac_exit(void)
{
#ifdef CONFIG_CPU_SH4
	free_irq(DMAE_IRQ, 0);
#endif
}

subsys_initcall(sh_dmac_init);
module_exit(sh_dmac_exit);

MODULE_LICENSE("GPL");

