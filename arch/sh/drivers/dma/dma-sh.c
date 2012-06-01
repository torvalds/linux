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
#include <linux/io.h>
#include <mach-dreamcast/mach/dma.h>
#include <asm/dma.h>
#include <asm/dma-register.h>
#include <cpu/dma-register.h>
#include <cpu/dma.h>

/*
 * Define the default configuration for dual address memory-memory transfer.
 * The 0x400 value represents auto-request, external->external.
 */
#define RS_DUAL	(DM_INC | SM_INC | 0x400 | TS_INDEX2VAL(XMIT_SZ_32BIT))

static unsigned long dma_find_base(unsigned int chan)
{
	unsigned long base = SH_DMAC_BASE0;

#ifdef SH_DMAC_BASE1
	if (chan >= 6)
		base = SH_DMAC_BASE1;
#endif

	return base;
}

static unsigned long dma_base_addr(unsigned int chan)
{
	unsigned long base = dma_find_base(chan);

	/* Normalize offset calculation */
	if (chan >= 9)
		chan -= 6;
	if (chan >= 4)
		base += 0x10;

	return base + (chan * 0x10);
}

#ifdef CONFIG_SH_DMA_IRQ_MULTI
static inline unsigned int get_dmte_irq(unsigned int chan)
{
	return chan >= 6 ? DMTE6_IRQ : DMTE0_IRQ;
}
#else

static unsigned int dmte_irq_map[] = {
	DMTE0_IRQ, DMTE0_IRQ + 1, DMTE0_IRQ + 2, DMTE0_IRQ + 3,

#ifdef DMTE4_IRQ
	DMTE4_IRQ, DMTE4_IRQ + 1,
#endif

#ifdef DMTE6_IRQ
	DMTE6_IRQ, DMTE6_IRQ + 1,
#endif

#ifdef DMTE8_IRQ
	DMTE8_IRQ, DMTE9_IRQ, DMTE10_IRQ, DMTE11_IRQ,
#endif
};

static inline unsigned int get_dmte_irq(unsigned int chan)
{
	return dmte_irq_map[chan];
}
#endif

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
	u32 chcr = __raw_readl(dma_base_addr(chan->chan) + CHCR);
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

	chcr = __raw_readl(dma_base_addr(chan->chan) + CHCR);

	if (!(chcr & CHCR_TE))
		return IRQ_NONE;

	chcr &= ~(CHCR_IE | CHCR_DE);
	__raw_writel(chcr, (dma_base_addr(chan->chan) + CHCR));

	wake_up(&chan->wait_queue);

	return IRQ_HANDLED;
}

static int sh_dmac_request_dma(struct dma_channel *chan)
{
	if (unlikely(!(chan->flags & DMA_TEI_CAPABLE)))
		return 0;

	return request_irq(get_dmte_irq(chan->chan), dma_tei, IRQF_SHARED,
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

	__raw_writel(chcr, (dma_base_addr(chan->chan) + CHCR));

	chan->flags |= DMA_CONFIGURED;
	return 0;
}

static void sh_dmac_enable_dma(struct dma_channel *chan)
{
	int irq;
	u32 chcr;

	chcr = __raw_readl(dma_base_addr(chan->chan) + CHCR);
	chcr |= CHCR_DE;

	if (chan->flags & DMA_TEI_CAPABLE)
		chcr |= CHCR_IE;

	__raw_writel(chcr, (dma_base_addr(chan->chan) + CHCR));

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

	chcr = __raw_readl(dma_base_addr(chan->chan) + CHCR);
	chcr &= ~(CHCR_DE | CHCR_TE | CHCR_IE);
	__raw_writel(chcr, (dma_base_addr(chan->chan) + CHCR));
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
		__raw_writel(chan->sar, (dma_base_addr(chan->chan) + SAR));
	if (chan->dar || (mach_is_dreamcast() &&
			  chan->chan == PVR2_CASCADE_CHAN))
		__raw_writel(chan->dar, (dma_base_addr(chan->chan) + DAR));

	__raw_writel(chan->count >> calc_xmit_shift(chan),
		(dma_base_addr(chan->chan) + TCR));

	sh_dmac_enable_dma(chan);

	return 0;
}

static int sh_dmac_get_dma_residue(struct dma_channel *chan)
{
	if (!(__raw_readl(dma_base_addr(chan->chan) + CHCR) & CHCR_DE))
		return 0;

	return __raw_readl(dma_base_addr(chan->chan) + TCR)
		 << calc_xmit_shift(chan);
}

/*
 * DMAOR handling
 */
#if defined(CONFIG_CPU_SUBTYPE_SH7723)	|| \
    defined(CONFIG_CPU_SUBTYPE_SH7724)	|| \
    defined(CONFIG_CPU_SUBTYPE_SH7780)	|| \
    defined(CONFIG_CPU_SUBTYPE_SH7785)
#define NR_DMAOR	2
#else
#define NR_DMAOR	1
#endif

/*
 * DMAOR bases are broken out amongst channel groups. DMAOR0 manages
 * channels 0 - 5, DMAOR1 6 - 11 (optional).
 */
#define dmaor_read_reg(n)		__raw_readw(dma_find_base((n)*6))
#define dmaor_write_reg(n, data)	__raw_writew(data, dma_find_base(n)*6)

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

/*
 * DMAE handling
 */
#ifdef CONFIG_CPU_SH4

#if defined(DMAE1_IRQ)
#define NR_DMAE		2
#else
#define NR_DMAE		1
#endif

static const char *dmae_name[] = {
	"DMAC Address Error0",
	"DMAC Address Error1"
};

#ifdef CONFIG_SH_DMA_IRQ_MULTI
static inline unsigned int get_dma_error_irq(int n)
{
	return get_dmte_irq(n * 6);
}
#else

static unsigned int dmae_irq_map[] = {
	DMAE0_IRQ,

#ifdef DMAE1_IRQ
	DMAE1_IRQ,
#endif
};

static inline unsigned int get_dma_error_irq(int n)
{
	return dmae_irq_map[n];
}
#endif

static irqreturn_t dma_err(int irq, void *dummy)
{
	int i;

	for (i = 0; i < NR_DMAOR; i++)
		dmaor_reset(i);

	disable_irq(irq);

	return IRQ_HANDLED;
}

static int dmae_irq_init(void)
{
	int n;

	for (n = 0; n < NR_DMAE; n++) {
		int i = request_irq(get_dma_error_irq(n), dma_err,
				    IRQF_SHARED, dmae_name[n], NULL);
		if (unlikely(i < 0)) {
			printk(KERN_ERR "%s request_irq fail\n", dmae_name[n]);
			return i;
		}
	}

	return 0;
}

static void dmae_irq_free(void)
{
	int n;

	for (n = 0; n < NR_DMAE; n++)
		free_irq(get_dma_error_irq(n), NULL);
}
#else
static inline int dmae_irq_init(void)
{
	return 0;
}

static void dmae_irq_free(void)
{
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
	int i, rc;

	/*
	 * Initialize DMAE, for parts that support it.
	 */
	rc = dmae_irq_init();
	if (unlikely(rc != 0))
		return rc;

	/*
	 * Initialize DMAOR, and clean up any error flags that may have
	 * been set.
	 */
	for (i = 0; i < NR_DMAOR; i++) {
		rc = dmaor_reset(i);
		if (unlikely(rc != 0))
			return rc;
	}

	return register_dmac(info);
}

static void __exit sh_dmac_exit(void)
{
	dmae_irq_free();
	unregister_dmac(&sh_dmac_info);
}

subsys_initcall(sh_dmac_init);
module_exit(sh_dmac_exit);

MODULE_AUTHOR("Takashi YOSHII, Paul Mundt, Andriy Skulysh");
MODULE_DESCRIPTION("SuperH On-Chip DMAC Support");
MODULE_LICENSE("GPL");
