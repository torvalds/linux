/*
 * include/asm-xtensa/variant-s6000/dmac.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Tensilica Inc.
 * Copyright (C) 2008 Emlix GmbH <info@emlix.com>
 * Authors:	Fabian Godehardt <fg@emlix.com>
 *		Oskar Schirmer <os@emlix.com>
 *		Daniel Gloeckner <dg@emlix.com>
 */

#ifndef __ASM_XTENSA_S6000_DMAC_H
#define __ASM_XTENSA_S6000_DMAC_H
#include <linux/io.h>
#include <variant/hardware.h>

/* DMA global */

#define S6_DMA_INTSTAT0		0x000
#define S6_DMA_INTSTAT1		0x004
#define S6_DMA_INTENABLE0	0x008
#define S6_DMA_INTENABLE1	0x00C
#define S6_DMA_INTRAW0		0x010
#define S6_DMA_INTRAW1		0x014
#define S6_DMA_INTCLEAR0	0x018
#define S6_DMA_INTCLEAR1	0x01C
#define S6_DMA_INTSET0		0x020
#define S6_DMA_INTSET1		0x024
#define S6_DMA_INT0_UNDER		0
#define S6_DMA_INT0_OVER		16
#define S6_DMA_INT1_CHANNEL		0
#define S6_DMA_INT1_MASTER		16
#define S6_DMA_INT1_MASTER_MASK			7
#define S6_DMA_TERMCNTIRQSTAT	0x028
#define S6_DMA_TERMCNTIRQCLR	0x02C
#define S6_DMA_TERMCNTIRQSET	0x030
#define S6_DMA_PENDCNTIRQSTAT	0x034
#define S6_DMA_PENDCNTIRQCLR	0x038
#define S6_DMA_PENDCNTIRQSET	0x03C
#define S6_DMA_LOWWMRKIRQSTAT	0x040
#define S6_DMA_LOWWMRKIRQCLR	0x044
#define S6_DMA_LOWWMRKIRQSET	0x048
#define S6_DMA_MASTERERRINFO	0x04C
#define S6_DMA_MASTERERR_CHAN(n)	(4*(n))
#define S6_DMA_MASTERERR_CHAN_MASK		0xF
#define S6_DMA_DESCRFIFO0	0x050
#define S6_DMA_DESCRFIFO1	0x054
#define S6_DMA_DESCRFIFO2	0x058
#define S6_DMA_DESCRFIFO2_AUTODISABLE	24
#define S6_DMA_DESCRFIFO3	0x05C
#define S6_DMA_MASTER0START	0x060
#define S6_DMA_MASTER0END	0x064
#define S6_DMA_MASTER1START	0x068
#define S6_DMA_MASTER1END	0x06C
#define S6_DMA_NEXTFREE		0x070
#define S6_DMA_NEXTFREE_CHAN		0
#define S6_DMA_NEXTFREE_CHAN_MASK	0x1F
#define S6_DMA_NEXTFREE_ENA		16
#define S6_DMA_NEXTFREE_ENA_MASK	((1 << 16) - 1)
#define S6_DMA_DPORTCTRLGRP(p)	((p) * 4 + 0x074)
#define S6_DMA_DPORTCTRLGRP_FRAMEREP	0
#define S6_DMA_DPORTCTRLGRP_NRCHANS	1
#define S6_DMA_DPORTCTRLGRP_NRCHANS_1		0
#define S6_DMA_DPORTCTRLGRP_NRCHANS_3		1
#define S6_DMA_DPORTCTRLGRP_NRCHANS_4		2
#define S6_DMA_DPORTCTRLGRP_NRCHANS_2		3
#define S6_DMA_DPORTCTRLGRP_ENA		31


/* DMA per channel */

#define DMA_CHNL(dmac, n)	((dmac) + 0x1000 + (n) * 0x100)
#define DMA_INDEX_CHNL(addr)	(((addr) >> 8) & 0xF)
#define DMA_MASK_DMAC(addr)	((addr) & 0xFFFF0000)
#define S6_DMA_CHNCTRL		0x000
#define S6_DMA_CHNCTRL_ENABLE		0
#define S6_DMA_CHNCTRL_PAUSE		1
#define S6_DMA_CHNCTRL_PRIO		2
#define S6_DMA_CHNCTRL_PRIO_MASK		3
#define S6_DMA_CHNCTRL_PERIPHXFER	4
#define S6_DMA_CHNCTRL_PERIPHENA	5
#define S6_DMA_CHNCTRL_SRCINC		6
#define S6_DMA_CHNCTRL_DSTINC		7
#define S6_DMA_CHNCTRL_BURSTLOG		8
#define S6_DMA_CHNCTRL_BURSTLOG_MASK		7
#define S6_DMA_CHNCTRL_DESCFIFODEPTH	12
#define S6_DMA_CHNCTRL_DESCFIFODEPTH_MASK	0x1F
#define S6_DMA_CHNCTRL_DESCFIFOFULL	17
#define S6_DMA_CHNCTRL_BWCONSEL		18
#define S6_DMA_CHNCTRL_BWCONENA		19
#define S6_DMA_CHNCTRL_PENDGCNTSTAT	20
#define S6_DMA_CHNCTRL_PENDGCNTSTAT_MASK	0x3F
#define S6_DMA_CHNCTRL_LOWWMARK		26
#define S6_DMA_CHNCTRL_LOWWMARK_MASK		0xF
#define S6_DMA_CHNCTRL_TSTAMP		30
#define S6_DMA_TERMCNTNB	0x004
#define S6_DMA_TERMCNTNB_MASK			0xFFFF
#define S6_DMA_TERMCNTTMO	0x008
#define S6_DMA_TERMCNTSTAT	0x00C
#define S6_DMA_TERMCNTSTAT_MASK		0xFF
#define S6_DMA_CMONCHUNK	0x010
#define S6_DMA_SRCSKIP		0x014
#define S6_DMA_DSTSKIP		0x018
#define S6_DMA_CUR_SRC		0x024
#define S6_DMA_CUR_DST		0x028
#define S6_DMA_TIMESTAMP	0x030

/* DMA channel lists */

#define S6_DPDMA_CHAN(stream, channel)	(4 * (stream) + (channel))
#define S6_DPDMA_NB	16

#define S6_HIFDMA_GMACTX	0
#define S6_HIFDMA_GMACRX	1
#define S6_HIFDMA_I2S0		2
#define S6_HIFDMA_I2S1		3
#define S6_HIFDMA_EGIB		4
#define S6_HIFDMA_PCITX		5
#define S6_HIFDMA_PCIRX		6
#define S6_HIFDMA_NB	7

#define S6_NIDMA_NB	4

#define S6_LMSDMA_NB	12

/* controller access */

#define S6_DMAC_NB	4
#define S6_DMAC_INDEX(dmac)	(((unsigned)(dmac) >> 18) % S6_DMAC_NB)

struct s6dmac_ctrl {
	u32 dmac;
	spinlock_t lock;
	u8 chan_nb;
};

extern struct s6dmac_ctrl s6dmac_ctrl[S6_DMAC_NB];


/* DMA control, per channel */

static inline int s6dmac_fifo_full(u32 dmac, int chan)
{
	return (readl(DMA_CHNL(dmac, chan) + S6_DMA_CHNCTRL)
		& (1 << S6_DMA_CHNCTRL_DESCFIFOFULL)) && 1;
}

static inline int s6dmac_termcnt_irq(u32 dmac, int chan)
{
	u32 m = 1 << chan;
	int r = (readl(dmac + S6_DMA_TERMCNTIRQSTAT) & m) && 1;
	if (r)
		writel(m, dmac + S6_DMA_TERMCNTIRQCLR);
	return r;
}

static inline int s6dmac_pendcnt_irq(u32 dmac, int chan)
{
	u32 m = 1 << chan;
	int r = (readl(dmac + S6_DMA_PENDCNTIRQSTAT) & m) && 1;
	if (r)
		writel(m, dmac + S6_DMA_PENDCNTIRQCLR);
	return r;
}

static inline int s6dmac_lowwmark_irq(u32 dmac, int chan)
{
	int r = (readl(dmac + S6_DMA_LOWWMRKIRQSTAT) & (1 << chan)) ? 1 : 0;
	if (r)
		writel(1 << chan, dmac + S6_DMA_LOWWMRKIRQCLR);
	return r;
}

static inline u32 s6dmac_pending_count(u32 dmac, int chan)
{
	return (readl(DMA_CHNL(dmac, chan) + S6_DMA_CHNCTRL)
			>> S6_DMA_CHNCTRL_PENDGCNTSTAT)
		& S6_DMA_CHNCTRL_PENDGCNTSTAT_MASK;
}

static inline void s6dmac_set_terminal_count(u32 dmac, int chan, u32 n)
{
	n &= S6_DMA_TERMCNTNB_MASK;
	n |= readl(DMA_CHNL(dmac, chan) + S6_DMA_TERMCNTNB)
	      & ~S6_DMA_TERMCNTNB_MASK;
	writel(n, DMA_CHNL(dmac, chan) + S6_DMA_TERMCNTNB);
}

static inline u32 s6dmac_get_terminal_count(u32 dmac, int chan)
{
	return (readl(DMA_CHNL(dmac, chan) + S6_DMA_TERMCNTNB))
		& S6_DMA_TERMCNTNB_MASK;
}

static inline u32 s6dmac_timestamp(u32 dmac, int chan)
{
	return readl(DMA_CHNL(dmac, chan) + S6_DMA_TIMESTAMP);
}

static inline u32 s6dmac_cur_src(u32 dmac, int chan)
{
	return readl(DMA_CHNL(dmac, chan) + S6_DMA_CUR_SRC);
}

static inline u32 s6dmac_cur_dst(u32 dmac, int chan)
{
	return readl(DMA_CHNL(dmac, chan) + S6_DMA_CUR_DST);
}

static inline void s6dmac_disable_chan(u32 dmac, int chan)
{
	u32 ctrl;
	writel(readl(DMA_CHNL(dmac, chan) + S6_DMA_CHNCTRL)
		& ~(1 << S6_DMA_CHNCTRL_ENABLE),
		DMA_CHNL(dmac, chan) + S6_DMA_CHNCTRL);
	do
		ctrl = readl(DMA_CHNL(dmac, chan) + S6_DMA_CHNCTRL);
	while (ctrl & (1 << S6_DMA_CHNCTRL_ENABLE));
}

static inline void s6dmac_set_stride_skip(u32 dmac, int chan,
	int comchunk,		/* 0: disable scatter/gather */
	int srcskip, int dstskip)
{
	writel(comchunk, DMA_CHNL(dmac, chan) + S6_DMA_CMONCHUNK);
	writel(srcskip, DMA_CHNL(dmac, chan) + S6_DMA_SRCSKIP);
	writel(dstskip, DMA_CHNL(dmac, chan) + S6_DMA_DSTSKIP);
}

static inline void s6dmac_enable_chan(u32 dmac, int chan,
	int prio,               /* 0 (highest) .. 3 (lowest) */
	int periphxfer,         /* <0: disable p.req.line, 0..1: mode */
	int srcinc, int dstinc, /* 0: dont increment src/dst address */
	int comchunk,		/* 0: disable scatter/gather */
	int srcskip, int dstskip,
	int burstsize,		/* 4 for I2S, 7 for everything else */
	int bandwidthconserve,  /* <0: disable, 0..1: select */
	int lowwmark,           /* 0..15 */
	int timestamp,		/* 0: disable timestamp */
	int enable)		/* 0: disable for now */
{
	writel(1, DMA_CHNL(dmac, chan) + S6_DMA_TERMCNTNB);
	writel(0, DMA_CHNL(dmac, chan) + S6_DMA_TERMCNTTMO);
	writel(lowwmark << S6_DMA_CHNCTRL_LOWWMARK,
		DMA_CHNL(dmac, chan) + S6_DMA_CHNCTRL);
	s6dmac_set_stride_skip(dmac, chan, comchunk, srcskip, dstskip);
	writel(((enable ? 1 : 0) << S6_DMA_CHNCTRL_ENABLE) |
		(prio << S6_DMA_CHNCTRL_PRIO) |
		(((periphxfer > 0) ? 1 : 0) << S6_DMA_CHNCTRL_PERIPHXFER) |
		(((periphxfer < 0) ? 0 : 1) << S6_DMA_CHNCTRL_PERIPHENA) |
		((srcinc ? 1 : 0) << S6_DMA_CHNCTRL_SRCINC) |
		((dstinc ? 1 : 0) << S6_DMA_CHNCTRL_DSTINC) |
		(burstsize << S6_DMA_CHNCTRL_BURSTLOG) |
		(((bandwidthconserve > 0) ? 1 : 0) << S6_DMA_CHNCTRL_BWCONSEL) |
		(((bandwidthconserve < 0) ? 0 : 1) << S6_DMA_CHNCTRL_BWCONENA) |
		(lowwmark << S6_DMA_CHNCTRL_LOWWMARK) |
		((timestamp ? 1 : 0) << S6_DMA_CHNCTRL_TSTAMP),
		DMA_CHNL(dmac, chan) + S6_DMA_CHNCTRL);
}


/* DMA control, per engine */

static inline unsigned _dmac_addr_index(u32 dmac)
{
	unsigned i = S6_DMAC_INDEX(dmac);
	if (s6dmac_ctrl[i].dmac != dmac)
		BUG();
	return i;
}

static inline void _s6dmac_disable_error_irqs(u32 dmac, u32 mask)
{
	writel(mask, dmac + S6_DMA_TERMCNTIRQCLR);
	writel(mask, dmac + S6_DMA_PENDCNTIRQCLR);
	writel(mask, dmac + S6_DMA_LOWWMRKIRQCLR);
	writel(readl(dmac + S6_DMA_INTENABLE0)
		& ~((mask << S6_DMA_INT0_UNDER) | (mask << S6_DMA_INT0_OVER)),
		dmac + S6_DMA_INTENABLE0);
	writel(readl(dmac + S6_DMA_INTENABLE1) & ~(mask << S6_DMA_INT1_CHANNEL),
		dmac + S6_DMA_INTENABLE1);
	writel((mask << S6_DMA_INT0_UNDER) | (mask << S6_DMA_INT0_OVER),
		dmac + S6_DMA_INTCLEAR0);
	writel(mask << S6_DMA_INT1_CHANNEL, dmac + S6_DMA_INTCLEAR1);
}

/*
 * request channel from specified engine
 * with chan<0, accept any channel
 * further parameters see s6dmac_enable_chan
 * returns < 0 upon error, channel nb otherwise
 */
static inline int s6dmac_request_chan(u32 dmac, int chan,
	int prio,
	int periphxfer,
	int srcinc, int dstinc,
	int comchunk,
	int srcskip, int dstskip,
	int burstsize,
	int bandwidthconserve,
	int lowwmark,
	int timestamp,
	int enable)
{
	int r = chan;
	unsigned long flags;
	spinlock_t *spinl = &s6dmac_ctrl[_dmac_addr_index(dmac)].lock;
	spin_lock_irqsave(spinl, flags);
	if (r < 0) {
		r = (readl(dmac + S6_DMA_NEXTFREE) >> S6_DMA_NEXTFREE_CHAN)
			& S6_DMA_NEXTFREE_CHAN_MASK;
	}
	if (r >= s6dmac_ctrl[_dmac_addr_index(dmac)].chan_nb) {
		if (chan < 0)
			r = -EBUSY;
		else
			r = -ENXIO;
	} else if (((readl(dmac + S6_DMA_NEXTFREE) >> S6_DMA_NEXTFREE_ENA)
			>> r) & 1) {
		r = -EBUSY;
	} else {
		s6dmac_enable_chan(dmac, r, prio, periphxfer,
			srcinc, dstinc, comchunk, srcskip, dstskip, burstsize,
			bandwidthconserve, lowwmark, timestamp, enable);
	}
	spin_unlock_irqrestore(spinl, flags);
	return r;
}

static inline void s6dmac_put_fifo(u32 dmac, int chan,
	u32 src, u32 dst, u32 size)
{
	unsigned long flags;
	spinlock_t *spinl = &s6dmac_ctrl[_dmac_addr_index(dmac)].lock;
	spin_lock_irqsave(spinl, flags);
	writel(src, dmac + S6_DMA_DESCRFIFO0);
	writel(dst, dmac + S6_DMA_DESCRFIFO1);
	writel(size, dmac + S6_DMA_DESCRFIFO2);
	writel(chan, dmac + S6_DMA_DESCRFIFO3);
	spin_unlock_irqrestore(spinl, flags);
}

static inline u32 s6dmac_channel_enabled(u32 dmac, int chan)
{
	return readl(DMA_CHNL(dmac, chan) + S6_DMA_CHNCTRL) &
			(1 << S6_DMA_CHNCTRL_ENABLE);
}

/*
 * group 1-4 data port channels
 * with port=0..3, nrch=1-4 channels,
 * frrep=0/1 (dis- or enable frame repeat)
 */
static inline void s6dmac_dp_setup_group(u32 dmac, int port,
			int nrch, int frrep)
{
	const static u8 mask[4] = {0, 3, 1, 2};
	BUG_ON(dmac != S6_REG_DPDMA);
	if ((port < 0) || (port > 3) || (nrch < 1) || (nrch > 4))
		return;
	writel((mask[nrch - 1] << S6_DMA_DPORTCTRLGRP_NRCHANS)
		| ((frrep ? 1 : 0) << S6_DMA_DPORTCTRLGRP_FRAMEREP),
		dmac + S6_DMA_DPORTCTRLGRP(port));
}

static inline void s6dmac_dp_switch_group(u32 dmac, int port, int enable)
{
	u32 tmp;
	BUG_ON(dmac != S6_REG_DPDMA);
	tmp = readl(dmac + S6_DMA_DPORTCTRLGRP(port));
	if (enable)
		tmp |= (1 << S6_DMA_DPORTCTRLGRP_ENA);
	else
		tmp &= ~(1 << S6_DMA_DPORTCTRLGRP_ENA);
	writel(tmp, dmac + S6_DMA_DPORTCTRLGRP(port));
}

extern void s6dmac_put_fifo_cache(u32 dmac, int chan,
	u32 src, u32 dst, u32 size);
extern void s6dmac_disable_error_irqs(u32 dmac, u32 mask);
extern u32 s6dmac_int_sources(u32 dmac, u32 channel);
extern void s6dmac_release_chan(u32 dmac, int chan);

#endif /* __ASM_XTENSA_S6000_DMAC_H */
