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
#include <asm/dma-sh.h>

#if defined(DMAE1_IRQ)
#define NR_DMAE		2
#else
#define NR_DMAE		1
#endif

static const char *dmae_name[] = {
	"DMAC Address Error0", "DMAC Address Error1"
};

static inline unsigned int get_dmte_irq(unsigned int chan)
{
	unsigned int irq = 0;
	if (chan < ARRAY_SIZE(dmte_irq_map))
		irq = dmte_irq_map[chan];

#if defined(CONFIG_SH_DMA_IRQ_MULTI)
	if (irq > DMTE6_IRQ)
		return DMTE6_IRQ;
	return DMTE0_IRQ;
#else
	return irq;
#endif
}

/*
 * We determine the correct shift size based off of the CHCR transmit size
 * for the given channel. Since we know that it will take:
 *
 *	info->count >> ts_shift[transmit_size]
 *
 * iterations to complete the transfer.
 */
static unsigned int ts_shift[] = TS_SHIFT;
static inline unsigned int calc_xmit_shift(struct dma_channel *chan)
{
	u32 chcr = __raw_readl(dma_base_addr[chan->chan] + CHCR);
	int cnt = ((chcr & CHCR_TS_LOW_MASK) >> CHCR_TS_LOW_SHIFT) |
		((chcr & CHCR_TS_HIGH_MASK) >> CHCR_TS_HIGH_SHIFT);

	return ts_shift[cnt];
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

	chcr = __raw_readl(dma_base_addr[chan->chan] + CHCR);

	if (!(chcr & CHCR_TE))
		return IRQ_NONE;

	chcr &= ~(CHCR_IE | CHCR_DE);
	__raw_writel(chcr, (dma_base_addr[chan->chan] + CHCR));

	wake_up(&chan->wait_queue);

	return IRQ_HANDLED;
}

static int sh_dmac_request_dma(struct dma_channel *chan)
{
	if (unlikely(!(chan->flags & DMA_TEI_CAPABLE)))
		return 0;

	return request_irq(get_dmte_irq(chan->chan), dma_tei,
#if defined(CONFIG_SH_DMA_IRQ_MULTI)
				IRQF_SHARED,
#else
				IRQF_DISABLED,
#endif
				chan->dev_id, chan);
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

	__raw_writel(chcr, (dma_base_addr[chan->chan] + CHCR));

	chan->flags |= DMA_CONFIGURED;
	return 0;
}

static void sh_dmac_enable_dma(struct dma_channel *chan)
{
	int irq;
	u32 chcr;

	chcr = __raw_readl(dma_base_addr[chan->chan] + CHCR);
	chcr |= CHCR_DE;

	if (chan->flags & DMA_TEI_CAPABLE)
		chcr |= CHCR_IE;

	__raw_writel(chcr, (dma_base_addr[chan->chan] + CHCR));

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

	chcr = __raw_readl(dma_base_addr[chan->chan] + CHCR);
	chcr &= ~(CHCR_DE | CHCR_TE | CHCR_IE);
	__raw_writel(chcr, (dma_base_addr[chan->chan] + CHCR));
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
		__raw_writel(chan->sar, (dma_base_addr[chan->chan]+SAR));
	if (chan->dar || (mach_is_dreamcast() &&
			  chan->chan == PVR2_CASCADE_CHAN))
		__raw_writel(chan->dar, (dma_base_addr[chan->chan] + DAR));

	__raw_writel(chan->count >> calc_xmit_shift(chan),
		(dma_base_addr[chan->chan] + TCR));

	sh_dmac_enable_dma(chan);

	return 0;
}

static int sh_dmac_get_dma_residue(struct dma_channel *chan)
{
	if (!(__raw_readl(dma_base_addr[chan->chan] + CHCR) & CHCR_DE))
		return 0;

	return __raw_readl(dma_base_addr[chan->chan] + TCR)
		 << calc_xmit_shift(chan);
}

static inline int dmaor_reset(int no)
{
	unsigned long dmaor = dmaor_read_reg(no);

	/* Try to clear the error flags first, incase they are set */
	dmaor &= ~(DMAOR_NMIF | DMAOR_AE);
	dmaor_write_reg(no, dmaor);

	dmaor |= DMAOR_INIT;
	dmaor_write_reg(no, dmaor);

	/* See if we got an error again */
	if ((dmaor_read_reg(no) & (DMAOR_AE | DMAOR_NMIF))) {
		printk(KERN_ERR "dma-sh: Can't initialize DMAOR.\n");
		return -EINVAL;
	}

	return 0;
}

#if defined(CONFIG_CPU_SH4)
static irqreturn_t dma_err(int irq, void *dummy)
{
#if defined(CONFIG_SH_DMA_IRQ_MULTI)
	int cnt = 0;
	switch (irq) {
#if defined(DMTE6_IRQ) && defined(DMAE1_IRQ)
	case DMTE6_IRQ:
		cnt++;
#endif
	case DMTE0_IRQ:
		if (dmaor_read_reg(cnt) & (DMAOR_NMIF | DMAOR_AE)) {
			disable_irq(irq);
			/* DMA multi and error IRQ */
			return IRQ_HANDLED;
		}
	default:
		return IRQ_NONE;
	}
#else
	dmaor_reset(0);
#if defined(CONFIG_CPU_SUBTYPE_SH7723)	|| \
		defined(CONFIG_CPU_SUBTYPE_SH7780)	|| \
		defined(CONFIG_CPU_SUBTYPE_SH7785)
	dmaor_reset(1);
#endif
	disable_irq(irq);

	return IRQ_HANDLED;
#endif
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

#ifdef CONFIG_CPU_SH4
static unsigned int get_dma_error_irq(int n)
{
#if defined(CONFIG_SH_DMA_IRQ_MULTI)
	return (n == 0) ? get_dmte_irq(0) : get_dmte_irq(6);
#else
	return (n == 0) ? DMAE0_IRQ :
#if defined(DMAE1_IRQ)
				DMAE1_IRQ;
#else
				-1;
#endif
#endif
}
#endif

static int __init sh_dmac_init(void)
{
	struct dma_info *info = &sh_dmac_info;
	int i;

#ifdef CONFIG_CPU_SH4
	int n;

	for (n = 0; n < NR_DMAE; n++) {
		i = request_irq(get_dma_error_irq(n), dma_err,
#if defined(CONFIG_SH_DMA_IRQ_MULTI)
				IRQF_SHARED,
#else
				IRQF_DISABLED,
#endif
				dmae_name[n], (void *)dmae_name[n]);
		if (unlikely(i < 0)) {
			printk(KERN_ERR "%s request_irq fail\n", dmae_name[n]);
			return i;
		}
	}
#endif /* CONFIG_CPU_SH4 */

	/*
	 * Initialize DMAOR, and clean up any error flags that may have
	 * been set.
	 */
	i = dmaor_reset(0);
	if (unlikely(i != 0))
		return i;
#if defined(CONFIG_CPU_SUBTYPE_SH7723)	|| \
		defined(CONFIG_CPU_SUBTYPE_SH7780)	|| \
		defined(CONFIG_CPU_SUBTYPE_SH7785)
	i = dmaor_reset(1);
	if (unlikely(i != 0))
		return i;
#endif

	return register_dmac(info);
}

static void __exit sh_dmac_exit(void)
{
#ifdef CONFIG_CPU_SH4
	int n;

	for (n = 0; n < NR_DMAE; n++) {
		free_irq(get_dma_error_irq(n), (void *)dmae_name[n]);
	}
#endif /* CONFIG_CPU_SH4 */
	unregister_dmac(&sh_dmac_info);
}

subsys_initcall(sh_dmac_init);
module_exit(sh_dmac_exit);

MODULE_AUTHOR("Takashi YOSHII, Paul Mundt, Andriy Skulysh");
MODULE_DESCRIPTION("SuperH On-Chip DMAC Support");
MODULE_LICENSE("GPL");
