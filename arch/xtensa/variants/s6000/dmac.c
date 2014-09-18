/*
 * Authors:	Oskar Schirmer <oskar@scara.com>
 *		Daniel Gloeckner <dg@emlix.com>
 * (c) 2008 emlix GmbH http://www.emlix.com
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <asm/cacheflush.h>
#include <variant/dmac.h>

/* DMA engine lookup */

struct s6dmac_ctrl s6dmac_ctrl[S6_DMAC_NB];


/* DMA control, per engine */

void s6dmac_put_fifo_cache(u32 dmac, int chan, u32 src, u32 dst, u32 size)
{
	if (xtensa_need_flush_dma_source(src)) {
		u32 base = src;
		u32 span = size;
		u32 chunk = readl(DMA_CHNL(dmac, chan) + S6_DMA_CMONCHUNK);
		if (chunk && (size > chunk)) {
			s32 skip =
				readl(DMA_CHNL(dmac, chan) + S6_DMA_SRCSKIP);
			u32 gaps = (size+chunk-1)/chunk - 1;
			if (skip >= 0) {
				span += gaps * skip;
			} else if (-skip > chunk) {
				s32 decr = gaps * (chunk + skip);
				base += decr;
				span = chunk - decr;
			} else {
				span = max(span + gaps * skip,
					(chunk + skip) * gaps - skip);
			}
		}
		flush_dcache_unaligned(base, span);
	}
	if (xtensa_need_invalidate_dma_destination(dst)) {
		u32 base = dst;
		u32 span = size;
		u32 chunk = readl(DMA_CHNL(dmac, chan) + S6_DMA_CMONCHUNK);
		if (chunk && (size > chunk)) {
			s32 skip =
				readl(DMA_CHNL(dmac, chan) + S6_DMA_DSTSKIP);
			u32 gaps = (size+chunk-1)/chunk - 1;
			if (skip >= 0) {
				span += gaps * skip;
			} else if (-skip > chunk) {
				s32 decr = gaps * (chunk + skip);
				base += decr;
				span = chunk - decr;
			} else {
				span = max(span + gaps * skip,
					(chunk + skip) * gaps - skip);
			}
		}
		invalidate_dcache_unaligned(base, span);
	}
	s6dmac_put_fifo(dmac, chan, src, dst, size);
}

void s6dmac_disable_error_irqs(u32 dmac, u32 mask)
{
	unsigned long flags;
	spinlock_t *spinl = &s6dmac_ctrl[_dmac_addr_index(dmac)].lock;
	spin_lock_irqsave(spinl, flags);
	_s6dmac_disable_error_irqs(dmac, mask);
	spin_unlock_irqrestore(spinl, flags);
}

u32 s6dmac_int_sources(u32 dmac, u32 channel)
{
	u32 mask, ret, tmp;
	mask = 1 << channel;

	tmp = readl(dmac + S6_DMA_TERMCNTIRQSTAT);
	tmp &= mask;
	writel(tmp, dmac + S6_DMA_TERMCNTIRQCLR);
	ret = tmp >> channel;

	tmp = readl(dmac + S6_DMA_PENDCNTIRQSTAT);
	tmp &= mask;
	writel(tmp, dmac + S6_DMA_PENDCNTIRQCLR);
	ret |= (tmp >> channel) << 1;

	tmp = readl(dmac + S6_DMA_LOWWMRKIRQSTAT);
	tmp &= mask;
	writel(tmp, dmac + S6_DMA_LOWWMRKIRQCLR);
	ret |= (tmp >> channel) << 2;

	tmp = readl(dmac + S6_DMA_INTRAW0);
	tmp &= (mask << S6_DMA_INT0_OVER) | (mask << S6_DMA_INT0_UNDER);
	writel(tmp, dmac + S6_DMA_INTCLEAR0);

	if (tmp & (mask << S6_DMA_INT0_UNDER))
		ret |= 1 << 3;
	if (tmp & (mask << S6_DMA_INT0_OVER))
		ret |= 1 << 4;

	tmp = readl(dmac + S6_DMA_MASTERERRINFO);
	mask <<= S6_DMA_INT1_CHANNEL;
	if (((tmp >> S6_DMA_MASTERERR_CHAN(0)) & S6_DMA_MASTERERR_CHAN_MASK)
			== channel)
		mask |= 1 << S6_DMA_INT1_MASTER;
	if (((tmp >> S6_DMA_MASTERERR_CHAN(1)) & S6_DMA_MASTERERR_CHAN_MASK)
			== channel)
		mask |= 1 << (S6_DMA_INT1_MASTER + 1);
	if (((tmp >> S6_DMA_MASTERERR_CHAN(2)) & S6_DMA_MASTERERR_CHAN_MASK)
			== channel)
		mask |= 1 << (S6_DMA_INT1_MASTER + 2);

	tmp = readl(dmac + S6_DMA_INTRAW1) & mask;
	writel(tmp, dmac + S6_DMA_INTCLEAR1);
	ret |= ((tmp >> channel) & 1) << 5;
	ret |= ((tmp >> S6_DMA_INT1_MASTER) & S6_DMA_INT1_MASTER_MASK) << 6;

	return ret;
}

void s6dmac_release_chan(u32 dmac, int chan)
{
	if (chan >= 0)
		s6dmac_disable_chan(dmac, chan);
}


/* global init */

static inline void __init dmac_init(u32 dmac, u8 chan_nb)
{
	s6dmac_ctrl[S6_DMAC_INDEX(dmac)].dmac = dmac;
	spin_lock_init(&s6dmac_ctrl[S6_DMAC_INDEX(dmac)].lock);
	s6dmac_ctrl[S6_DMAC_INDEX(dmac)].chan_nb = chan_nb;
	writel(S6_DMA_INT1_MASTER_MASK << S6_DMA_INT1_MASTER,
		dmac + S6_DMA_INTCLEAR1);
}

static inline void __init dmac_master(u32 dmac,
	u32 m0start, u32 m0end, u32 m1start, u32 m1end)
{
	writel(m0start, dmac + S6_DMA_MASTER0START);
	writel(m0end - 1, dmac + S6_DMA_MASTER0END);
	writel(m1start, dmac + S6_DMA_MASTER1START);
	writel(m1end - 1, dmac + S6_DMA_MASTER1END);
}

static void __init s6_dmac_init(void)
{
	dmac_init(S6_REG_LMSDMA, S6_LMSDMA_NB);
	dmac_master(S6_REG_LMSDMA,
		S6_MEM_DDR, S6_MEM_PCIE_APER, S6_MEM_EFI, S6_MEM_GMAC);
	dmac_init(S6_REG_NIDMA, S6_NIDMA_NB);
	dmac_init(S6_REG_DPDMA, S6_DPDMA_NB);
	dmac_master(S6_REG_DPDMA,
		S6_MEM_DDR, S6_MEM_PCIE_APER, S6_REG_DP, S6_REG_DPDMA);
	dmac_init(S6_REG_HIFDMA, S6_HIFDMA_NB);
	dmac_master(S6_REG_HIFDMA,
		S6_MEM_GMAC, S6_MEM_PCIE_CFG, S6_MEM_PCIE_APER, S6_MEM_AUX);
}

arch_initcall(s6_dmac_init);
