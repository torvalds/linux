/*
 * arch/sh/drivers/dma/dma-sh.c
 *
 * SuperH On-chip DMAC Support
 *
 * Copyright (C) 2000 Takashi YOSHII
 * Copyright (C) 2003, 2004 Paul Mundt
 * Copyright (C) 2005 Andriy Skulysh
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <mach-dreamcast/mach/dma.h>
#include <asm/dma.h>
#include <asm/io.h>
#include "dma-sh.h"

static int dmte_irq_map[] = {
	DMTE0_IRQ,
	DMTE1_IRQ,
	DMTE2_IRQ,
	DMTE3_IRQ,
#if defined(CONFIG_CPU_SUBTYPE_SH7720)  ||	\
    defined(CONFIG_CPU_SUBTYPE_SH7721)  ||	\
    defined(CONFIG_CPU_SUBTYPE_SH7751R) ||	\
    defined(CONFIG_CPU_SUBTYPE_SH7760)  ||	\
    defined(CONFIG_CPU_SUBTYPE_SH7709)  ||	\
    defined(CONFIG_CPU_SUBTYPE_SH7780)
	DMTE4_IRQ,
	DMTE5_IRQ,
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7751R) ||	\
    defined(CONFIG_CPU_SUBTYPE_SH7760)  ||	\
    defined(CONFIG_CPU_SUBTYPE_SH7780)
	DMTE6_IRQ,
	DMTE7_IRQ,
#endif
};

static inline unsigned int get_dmte_irq(unsigned int chan)
{
	unsigned int irq = 0;
	if (chan < ARRAY_SIZE(dmte_irq_map))
		irq = dmte_irq_map[chan];
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

	return ts_shift[(chcr & CHCR_TS_MASK)>>CHCR_TS_SHIFT];
}

/*
 * The transfer end interrupt must read the chcr register to end the
 * hardware interrupt active condition.
 * Besides that it needs to waken any waiting process, which should handle
 * setting up the next transfer.
 */
static irqreturn_t dma_tei(int irq, void *dev_id)
{
	struct dma_channel *chan = dev_id;
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
	if (unlikely(!(chan->flags & DMA_TEI_CAPABLE)))
		return 0;

	return request_irq(get_dmte_irq(chan->chan), dma_tei,
			   IRQF_DISABLED, chan->dev_id, chan);
}

static void sh_dmac_free_dma(struct dma_channel *chan)
{
	free_irq(get_dmte_irq(chan->chan), chan);
}

static int
sh_dmac_configure_channel(struct dma_channel *chan, unsigned long chcr)
{
	if (!chcr)
		chcr = RS_DUAL | CHCR_IE;

	if (chcr & CHCR_IE) {
		chcr &= ~CHCR_IE;
		chan->flags |= DMA_TEI_CAPABLE;
	} else {
		chan->flags &= ~DMA_TEI_CAPABLE;
	}

	ctrl_outl(chcr, CHCR[chan->chan]);

	chan->flags |= DMA_CONFIGURED;
	return 0;
}

static void sh_dmac_enable_dma(struct dma_channel *chan)
{
	int irq;
	u32 chcr;

	chcr = ctrl_inl(CHCR[chan->chan]);
	chcr |= CHCR_DE;

	if (chan->flags & DMA_TEI_CAPABLE)
		chcr |= CHCR_IE;

	ctrl_outl(chcr, CHCR[chan->chan]);

	if (chan->flags & DMA_TEI_CAPABLE) {
		irq = get_dmte_irq(chan->chan);
		enable_irq(irq);
	}
}

static void sh_dmac_disable_dma(struct dma_channel *chan)
{
	int irq;
	u32 chcr;

	if (chan->flags & DMA_TEI_CAPABLE) {
		irq = get_dmte_irq(chan->chan);
		disable_irq(irq);
	}

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
	if (unlikely(!(chan->flags & DMA_CONFIGURED)))
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
	if (chan->sar || (mach_is_dreamcast() &&
			  chan->chan == PVR2_CASCADE_CHAN))
		ctrl_outl(chan->sar, SAR[chan->chan]);
	if (chan->dar || (mach_is_dreamcast() &&
			  chan->chan == PVR2_CASCADE_CHAN))
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

#if defined(CONFIG_CPU_SUBTYPE_SH7720) || \
    defined(CONFIG_CPU_SUBTYPE_SH7721) || \
    defined(CONFIG_CPU_SUBTYPE_SH7780) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
#define dmaor_read_reg()	ctrl_inw(DMAOR)
#define dmaor_write_reg(data)	ctrl_outw(data, DMAOR)
#else
#define dmaor_read_reg()	ctrl_inl(DMAOR)
#define dmaor_write_reg(data)	ctrl_outl(data, DMAOR)
#endif

static inline int dmaor_reset(void)
{
	unsigned long dmaor = dmaor_read_reg();

	/* Try to clear the error flags first, incase they are set */
	dmaor &= ~(DMAOR_NMIF | DMAOR_AE);
	dmaor_write_reg(dmaor);

	dmaor |= DMAOR_INIT;
	dmaor_write_reg(dmaor);

	/* See if we got an error again */
	if ((dmaor_read_reg() & (DMAOR_AE | DMAOR_NMIF))) {
		printk(KERN_ERR "dma-sh: Can't initialize DMAOR.\n");
		return -EINVAL;
	}

	return 0;
}

#if defined(CONFIG_CPU_SH4)
static irqreturn_t dma_err(int irq, void *dummy)
{
	dmaor_reset();
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
	.name		= "sh_dmac",
	.nr_channels	= CONFIG_NR_ONCHIP_DMA_CHANNELS,
	.ops		= &sh_dmac_ops,
	.flags		= DMAC_CHANNELS_TEI_CAPABLE,
};

static int __init sh_dmac_init(void)
{
	struct dma_info *info = &sh_dmac_info;
	int i;

#ifdef CONFIG_CPU_SH4
	i = request_irq(DMAE_IRQ, dma_err, IRQF_DISABLED, "DMAC Address Error", 0);
	if (unlikely(i < 0))
		return i;
#endif

	/*
	 * Initialize DMAOR, and clean up any error flags that may have
	 * been set.
	 */
	i = dmaor_reset();
	if (unlikely(i != 0))
		return i;

	return register_dmac(info);
}

static void __exit sh_dmac_exit(void)
{
#ifdef CONFIG_CPU_SH4
	free_irq(DMAE_IRQ, 0);
#endif
	unregister_dmac(&sh_dmac_info);
}

subsys_initcall(sh_dmac_init);
module_exit(sh_dmac_exit);

MODULE_AUTHOR("Takashi YOSHII, Paul Mundt, Andriy Skulysh");
MODULE_DESCRIPTION("SuperH On-Chip DMAC Support");
MODULE_LICENSE("GPL");
