// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2015 Robert Jarzmik <robert.jarzmik@free.fr>
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/platform_data/mmp_dma.h>
#include <linux/dmapool.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/of.h>
#include <linux/wait.h>
#include <linux/dma/pxa-dma.h>

#include "dmaengine.h"
#include "virt-dma.h"

#define DCSR(n)		(0x0000 + ((n) << 2))
#define DALGN(n)	0x00a0
#define DINT		0x00f0
#define DDADR(n)	(0x0200 + ((n) << 4))
#define DSADR(n)	(0x0204 + ((n) << 4))
#define DTADR(n)	(0x0208 + ((n) << 4))
#define DCMD(n)		(0x020c + ((n) << 4))

#define PXA_DCSR_RUN		BIT(31)	/* Run Bit (read / write) */
#define PXA_DCSR_NODESC		BIT(30)	/* No-Descriptor Fetch (read / write) */
#define PXA_DCSR_STOPIRQEN	BIT(29)	/* Stop Interrupt Enable (R/W) */
#define PXA_DCSR_REQPEND	BIT(8)	/* Request Pending (read-only) */
#define PXA_DCSR_STOPSTATE	BIT(3)	/* Stop State (read-only) */
#define PXA_DCSR_ENDINTR	BIT(2)	/* End Interrupt (read / write) */
#define PXA_DCSR_STARTINTR	BIT(1)	/* Start Interrupt (read / write) */
#define PXA_DCSR_BUSERR		BIT(0)	/* Bus Error Interrupt (read / write) */

#define PXA_DCSR_EORIRQEN	BIT(28)	/* End of Receive IRQ Enable (R/W) */
#define PXA_DCSR_EORJMPEN	BIT(27)	/* Jump to next descriptor on EOR */
#define PXA_DCSR_EORSTOPEN	BIT(26)	/* STOP on an EOR */
#define PXA_DCSR_SETCMPST	BIT(25)	/* Set Descriptor Compare Status */
#define PXA_DCSR_CLRCMPST	BIT(24)	/* Clear Descriptor Compare Status */
#define PXA_DCSR_CMPST		BIT(10)	/* The Descriptor Compare Status */
#define PXA_DCSR_EORINTR	BIT(9)	/* The end of Receive */

#define DRCMR_MAPVLD	BIT(7)	/* Map Valid (read / write) */
#define DRCMR_CHLNUM	0x1f	/* mask for Channel Number (read / write) */

#define DDADR_DESCADDR	0xfffffff0	/* Address of next descriptor (mask) */
#define DDADR_STOP	BIT(0)	/* Stop (read / write) */

#define PXA_DCMD_INCSRCADDR	BIT(31)	/* Source Address Increment Setting. */
#define PXA_DCMD_INCTRGADDR	BIT(30)	/* Target Address Increment Setting. */
#define PXA_DCMD_FLOWSRC	BIT(29)	/* Flow Control by the source. */
#define PXA_DCMD_FLOWTRG	BIT(28)	/* Flow Control by the target. */
#define PXA_DCMD_STARTIRQEN	BIT(22)	/* Start Interrupt Enable */
#define PXA_DCMD_ENDIRQEN	BIT(21)	/* End Interrupt Enable */
#define PXA_DCMD_ENDIAN		BIT(18)	/* Device Endian-ness. */
#define PXA_DCMD_BURST8		(1 << 16)	/* 8 byte burst */
#define PXA_DCMD_BURST16	(2 << 16)	/* 16 byte burst */
#define PXA_DCMD_BURST32	(3 << 16)	/* 32 byte burst */
#define PXA_DCMD_WIDTH1		(1 << 14)	/* 1 byte width */
#define PXA_DCMD_WIDTH2		(2 << 14)	/* 2 byte width (HalfWord) */
#define PXA_DCMD_WIDTH4		(3 << 14)	/* 4 byte width (Word) */
#define PXA_DCMD_LENGTH		0x01fff		/* length mask (max = 8K - 1) */

#define PDMA_ALIGNMENT		3
#define PDMA_MAX_DESC_BYTES	(PXA_DCMD_LENGTH & ~((1 << PDMA_ALIGNMENT) - 1))

struct pxad_desc_hw {
	u32 ddadr;	/* Points to the next descriptor + flags */
	u32 dsadr;	/* DSADR value for the current transfer */
	u32 dtadr;	/* DTADR value for the current transfer */
	u32 dcmd;	/* DCMD value for the current transfer */
} __aligned(16);

struct pxad_desc_sw {
	struct virt_dma_desc	vd;		/* Virtual descriptor */
	int			nb_desc;	/* Number of hw. descriptors */
	size_t			len;		/* Number of bytes xfered */
	dma_addr_t		first;		/* First descriptor's addr */

	/* At least one descriptor has an src/dst address not multiple of 8 */
	bool			misaligned;
	bool			cyclic;
	struct dma_pool		*desc_pool;	/* Channel's used allocator */

	struct pxad_desc_hw	*hw_desc[];	/* DMA coherent descriptors */
};

struct pxad_phy {
	int			idx;
	void __iomem		*base;
	struct pxad_chan	*vchan;
};

struct pxad_chan {
	struct virt_dma_chan	vc;		/* Virtual channel */
	u32			drcmr;		/* Requestor of the channel */
	enum pxad_chan_prio	prio;		/* Required priority of phy */
	/*
	 * At least one desc_sw in submitted or issued transfers on this channel
	 * has one address such as: addr % 8 != 0. This implies the DALGN
	 * setting on the phy.
	 */
	bool			misaligned;
	struct dma_slave_config	cfg;		/* Runtime config */

	/* protected by vc->lock */
	struct pxad_phy		*phy;
	struct dma_pool		*desc_pool;	/* Descriptors pool */
	dma_cookie_t		bus_error;

	wait_queue_head_t	wq_state;
};

struct pxad_device {
	struct dma_device		slave;
	int				nr_chans;
	int				nr_requestors;
	void __iomem			*base;
	struct pxad_phy			*phys;
	spinlock_t			phy_lock;	/* Phy association */
#ifdef CONFIG_DEBUG_FS
	struct dentry			*dbgfs_root;
	struct dentry			**dbgfs_chan;
#endif
};

#define tx_to_pxad_desc(tx)					\
	container_of(tx, struct pxad_desc_sw, async_tx)
#define to_pxad_chan(dchan)					\
	container_of(dchan, struct pxad_chan, vc.chan)
#define to_pxad_dev(dmadev)					\
	container_of(dmadev, struct pxad_device, slave)
#define to_pxad_sw_desc(_vd)				\
	container_of((_vd), struct pxad_desc_sw, vd)

#define _phy_readl_relaxed(phy, _reg)					\
	readl_relaxed((phy)->base + _reg((phy)->idx))
#define phy_readl_relaxed(phy, _reg)					\
	({								\
		u32 _v;							\
		_v = readl_relaxed((phy)->base + _reg((phy)->idx));	\
		dev_vdbg(&phy->vchan->vc.chan.dev->device,		\
			 "%s(): readl(%s): 0x%08x\n", __func__, #_reg,	\
			  _v);						\
		_v;							\
	})
#define phy_writel(phy, val, _reg)					\
	do {								\
		writel((val), (phy)->base + _reg((phy)->idx));		\
		dev_vdbg(&phy->vchan->vc.chan.dev->device,		\
			 "%s(): writel(0x%08x, %s)\n",			\
			 __func__, (u32)(val), #_reg);			\
	} while (0)
#define phy_writel_relaxed(phy, val, _reg)				\
	do {								\
		writel_relaxed((val), (phy)->base + _reg((phy)->idx));	\
		dev_vdbg(&phy->vchan->vc.chan.dev->device,		\
			 "%s(): writel_relaxed(0x%08x, %s)\n",		\
			 __func__, (u32)(val), #_reg);			\
	} while (0)

static unsigned int pxad_drcmr(unsigned int line)
{
	if (line < 64)
		return 0x100 + line * 4;
	return 0x1000 + line * 4;
}

static bool pxad_filter_fn(struct dma_chan *chan, void *param);

/*
 * Debug fs
 */
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>

static int requester_chan_show(struct seq_file *s, void *p)
{
	struct pxad_phy *phy = s->private;
	int i;
	u32 drcmr;

	seq_printf(s, "DMA channel %d requester :\n", phy->idx);
	for (i = 0; i < 70; i++) {
		drcmr = readl_relaxed(phy->base + pxad_drcmr(i));
		if ((drcmr & DRCMR_CHLNUM) == phy->idx)
			seq_printf(s, "\tRequester %d (MAPVLD=%d)\n", i,
				   !!(drcmr & DRCMR_MAPVLD));
	}
	return 0;
}

static inline int dbg_burst_from_dcmd(u32 dcmd)
{
	int burst = (dcmd >> 16) & 0x3;

	return burst ? 4 << burst : 0;
}

static int is_phys_valid(unsigned long addr)
{
	return pfn_valid(__phys_to_pfn(addr));
}

#define PXA_DCSR_STR(flag) (dcsr & PXA_DCSR_##flag ? #flag" " : "")
#define PXA_DCMD_STR(flag) (dcmd & PXA_DCMD_##flag ? #flag" " : "")

static int descriptors_show(struct seq_file *s, void *p)
{
	struct pxad_phy *phy = s->private;
	int i, max_show = 20, burst, width;
	u32 dcmd;
	unsigned long phys_desc, ddadr;
	struct pxad_desc_hw *desc;

	phys_desc = ddadr = _phy_readl_relaxed(phy, DDADR);

	seq_printf(s, "DMA channel %d descriptors :\n", phy->idx);
	seq_printf(s, "[%03d] First descriptor unknown\n", 0);
	for (i = 1; i < max_show && is_phys_valid(phys_desc); i++) {
		desc = phys_to_virt(phys_desc);
		dcmd = desc->dcmd;
		burst = dbg_burst_from_dcmd(dcmd);
		width = (1 << ((dcmd >> 14) & 0x3)) >> 1;

		seq_printf(s, "[%03d] Desc at %08lx(virt %p)\n",
			   i, phys_desc, desc);
		seq_printf(s, "\tDDADR = %08x\n", desc->ddadr);
		seq_printf(s, "\tDSADR = %08x\n", desc->dsadr);
		seq_printf(s, "\tDTADR = %08x\n", desc->dtadr);
		seq_printf(s, "\tDCMD  = %08x (%s%s%s%s%s%s%sburst=%d width=%d len=%d)\n",
			   dcmd,
			   PXA_DCMD_STR(INCSRCADDR), PXA_DCMD_STR(INCTRGADDR),
			   PXA_DCMD_STR(FLOWSRC), PXA_DCMD_STR(FLOWTRG),
			   PXA_DCMD_STR(STARTIRQEN), PXA_DCMD_STR(ENDIRQEN),
			   PXA_DCMD_STR(ENDIAN), burst, width,
			   dcmd & PXA_DCMD_LENGTH);
		phys_desc = desc->ddadr;
	}
	if (i == max_show)
		seq_printf(s, "[%03d] Desc at %08lx ... max display reached\n",
			   i, phys_desc);
	else
		seq_printf(s, "[%03d] Desc at %08lx is %s\n",
			   i, phys_desc, phys_desc == DDADR_STOP ?
			   "DDADR_STOP" : "invalid");

	return 0;
}

static int chan_state_show(struct seq_file *s, void *p)
{
	struct pxad_phy *phy = s->private;
	u32 dcsr, dcmd;
	int burst, width;
	static const char * const str_prio[] = {
		"high", "normal", "low", "invalid"
	};

	dcsr = _phy_readl_relaxed(phy, DCSR);
	dcmd = _phy_readl_relaxed(phy, DCMD);
	burst = dbg_burst_from_dcmd(dcmd);
	width = (1 << ((dcmd >> 14) & 0x3)) >> 1;

	seq_printf(s, "DMA channel %d\n", phy->idx);
	seq_printf(s, "\tPriority : %s\n",
			  str_prio[(phy->idx & 0xf) / 4]);
	seq_printf(s, "\tUnaligned transfer bit: %s\n",
			  _phy_readl_relaxed(phy, DALGN) & BIT(phy->idx) ?
			  "yes" : "no");
	seq_printf(s, "\tDCSR  = %08x (%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s)\n",
		   dcsr, PXA_DCSR_STR(RUN), PXA_DCSR_STR(NODESC),
		   PXA_DCSR_STR(STOPIRQEN), PXA_DCSR_STR(EORIRQEN),
		   PXA_DCSR_STR(EORJMPEN), PXA_DCSR_STR(EORSTOPEN),
		   PXA_DCSR_STR(SETCMPST), PXA_DCSR_STR(CLRCMPST),
		   PXA_DCSR_STR(CMPST), PXA_DCSR_STR(EORINTR),
		   PXA_DCSR_STR(REQPEND), PXA_DCSR_STR(STOPSTATE),
		   PXA_DCSR_STR(ENDINTR), PXA_DCSR_STR(STARTINTR),
		   PXA_DCSR_STR(BUSERR));

	seq_printf(s, "\tDCMD  = %08x (%s%s%s%s%s%s%sburst=%d width=%d len=%d)\n",
		   dcmd,
		   PXA_DCMD_STR(INCSRCADDR), PXA_DCMD_STR(INCTRGADDR),
		   PXA_DCMD_STR(FLOWSRC), PXA_DCMD_STR(FLOWTRG),
		   PXA_DCMD_STR(STARTIRQEN), PXA_DCMD_STR(ENDIRQEN),
		   PXA_DCMD_STR(ENDIAN), burst, width, dcmd & PXA_DCMD_LENGTH);
	seq_printf(s, "\tDSADR = %08x\n", _phy_readl_relaxed(phy, DSADR));
	seq_printf(s, "\tDTADR = %08x\n", _phy_readl_relaxed(phy, DTADR));
	seq_printf(s, "\tDDADR = %08x\n", _phy_readl_relaxed(phy, DDADR));

	return 0;
}

static int state_show(struct seq_file *s, void *p)
{
	struct pxad_device *pdev = s->private;

	/* basic device status */
	seq_puts(s, "DMA engine status\n");
	seq_printf(s, "\tChannel number: %d\n", pdev->nr_chans);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(state);
DEFINE_SHOW_ATTRIBUTE(chan_state);
DEFINE_SHOW_ATTRIBUTE(descriptors);
DEFINE_SHOW_ATTRIBUTE(requester_chan);

static struct dentry *pxad_dbg_alloc_chan(struct pxad_device *pdev,
					     int ch, struct dentry *chandir)
{
	char chan_name[11];
	struct dentry *chan;
	void *dt;

	scnprintf(chan_name, sizeof(chan_name), "%d", ch);
	chan = debugfs_create_dir(chan_name, chandir);
	dt = (void *)&pdev->phys[ch];

	debugfs_create_file("state", 0400, chan, dt, &chan_state_fops);
	debugfs_create_file("descriptors", 0400, chan, dt, &descriptors_fops);
	debugfs_create_file("requesters", 0400, chan, dt, &requester_chan_fops);

	return chan;
}

static void pxad_init_debugfs(struct pxad_device *pdev)
{
	int i;
	struct dentry *chandir;

	pdev->dbgfs_chan =
		kmalloc_array(pdev->nr_chans, sizeof(struct dentry *),
			      GFP_KERNEL);
	if (!pdev->dbgfs_chan)
		return;

	pdev->dbgfs_root = debugfs_create_dir(dev_name(pdev->slave.dev), NULL);

	debugfs_create_file("state", 0400, pdev->dbgfs_root, pdev, &state_fops);

	chandir = debugfs_create_dir("channels", pdev->dbgfs_root);

	for (i = 0; i < pdev->nr_chans; i++)
		pdev->dbgfs_chan[i] = pxad_dbg_alloc_chan(pdev, i, chandir);
}

static void pxad_cleanup_debugfs(struct pxad_device *pdev)
{
	debugfs_remove_recursive(pdev->dbgfs_root);
}
#else
static inline void pxad_init_debugfs(struct pxad_device *pdev) {}
static inline void pxad_cleanup_debugfs(struct pxad_device *pdev) {}
#endif

static struct pxad_phy *lookup_phy(struct pxad_chan *pchan)
{
	int prio, i;
	struct pxad_device *pdev = to_pxad_dev(pchan->vc.chan.device);
	struct pxad_phy *phy, *found = NULL;
	unsigned long flags;

	/*
	 * dma channel priorities
	 * ch 0 - 3,  16 - 19  <--> (0)
	 * ch 4 - 7,  20 - 23  <--> (1)
	 * ch 8 - 11, 24 - 27  <--> (2)
	 * ch 12 - 15, 28 - 31  <--> (3)
	 */

	spin_lock_irqsave(&pdev->phy_lock, flags);
	for (prio = pchan->prio; prio >= PXAD_PRIO_HIGHEST; prio--) {
		for (i = 0; i < pdev->nr_chans; i++) {
			if (prio != (i & 0xf) >> 2)
				continue;
			phy = &pdev->phys[i];
			if (!phy->vchan) {
				phy->vchan = pchan;
				found = phy;
				goto out_unlock;
			}
		}
	}

out_unlock:
	spin_unlock_irqrestore(&pdev->phy_lock, flags);
	dev_dbg(&pchan->vc.chan.dev->device,
		"%s(): phy=%p(%d)\n", __func__, found,
		found ? found->idx : -1);

	return found;
}

static void pxad_free_phy(struct pxad_chan *chan)
{
	struct pxad_device *pdev = to_pxad_dev(chan->vc.chan.device);
	unsigned long flags;
	u32 reg;

	dev_dbg(&chan->vc.chan.dev->device,
		"%s(): freeing\n", __func__);
	if (!chan->phy)
		return;

	/* clear the channel mapping in DRCMR */
	if (chan->drcmr <= pdev->nr_requestors) {
		reg = pxad_drcmr(chan->drcmr);
		writel_relaxed(0, chan->phy->base + reg);
	}

	spin_lock_irqsave(&pdev->phy_lock, flags);
	chan->phy->vchan = NULL;
	chan->phy = NULL;
	spin_unlock_irqrestore(&pdev->phy_lock, flags);
}

static bool is_chan_running(struct pxad_chan *chan)
{
	u32 dcsr;
	struct pxad_phy *phy = chan->phy;

	if (!phy)
		return false;
	dcsr = phy_readl_relaxed(phy, DCSR);
	return dcsr & PXA_DCSR_RUN;
}

static bool is_running_chan_misaligned(struct pxad_chan *chan)
{
	u32 dalgn;

	BUG_ON(!chan->phy);
	dalgn = phy_readl_relaxed(chan->phy, DALGN);
	return dalgn & (BIT(chan->phy->idx));
}

static void phy_enable(struct pxad_phy *phy, bool misaligned)
{
	struct pxad_device *pdev;
	u32 reg, dalgn;

	if (!phy->vchan)
		return;

	dev_dbg(&phy->vchan->vc.chan.dev->device,
		"%s(); phy=%p(%d) misaligned=%d\n", __func__,
		phy, phy->idx, misaligned);

	pdev = to_pxad_dev(phy->vchan->vc.chan.device);
	if (phy->vchan->drcmr <= pdev->nr_requestors) {
		reg = pxad_drcmr(phy->vchan->drcmr);
		writel_relaxed(DRCMR_MAPVLD | phy->idx, phy->base + reg);
	}

	dalgn = phy_readl_relaxed(phy, DALGN);
	if (misaligned)
		dalgn |= BIT(phy->idx);
	else
		dalgn &= ~BIT(phy->idx);
	phy_writel_relaxed(phy, dalgn, DALGN);

	phy_writel(phy, PXA_DCSR_STOPIRQEN | PXA_DCSR_ENDINTR |
		   PXA_DCSR_BUSERR | PXA_DCSR_RUN, DCSR);
}

static void phy_disable(struct pxad_phy *phy)
{
	u32 dcsr;

	if (!phy)
		return;

	dcsr = phy_readl_relaxed(phy, DCSR);
	dev_dbg(&phy->vchan->vc.chan.dev->device,
		"%s(): phy=%p(%d)\n", __func__, phy, phy->idx);
	phy_writel(phy, dcsr & ~PXA_DCSR_RUN & ~PXA_DCSR_STOPIRQEN, DCSR);
}

static void pxad_launch_chan(struct pxad_chan *chan,
				 struct pxad_desc_sw *desc)
{
	dev_dbg(&chan->vc.chan.dev->device,
		"%s(): desc=%p\n", __func__, desc);
	if (!chan->phy) {
		chan->phy = lookup_phy(chan);
		if (!chan->phy) {
			dev_dbg(&chan->vc.chan.dev->device,
				"%s(): no free dma channel\n", __func__);
			return;
		}
	}
	chan->bus_error = 0;

	/*
	 * Program the descriptor's address into the DMA controller,
	 * then start the DMA transaction
	 */
	phy_writel(chan->phy, desc->first, DDADR);
	phy_enable(chan->phy, chan->misaligned);
	wake_up(&chan->wq_state);
}

static void set_updater_desc(struct pxad_desc_sw *sw_desc,
			     unsigned long flags)
{
	struct pxad_desc_hw *updater =
		sw_desc->hw_desc[sw_desc->nb_desc - 1];
	dma_addr_t dma = sw_desc->hw_desc[sw_desc->nb_desc - 2]->ddadr;

	updater->ddadr = DDADR_STOP;
	updater->dsadr = dma;
	updater->dtadr = dma + 8;
	updater->dcmd = PXA_DCMD_WIDTH4 | PXA_DCMD_BURST32 |
		(PXA_DCMD_LENGTH & sizeof(u32));
	if (flags & DMA_PREP_INTERRUPT)
		updater->dcmd |= PXA_DCMD_ENDIRQEN;
	if (sw_desc->cyclic)
		sw_desc->hw_desc[sw_desc->nb_desc - 2]->ddadr = sw_desc->first;
}

static bool is_desc_completed(struct virt_dma_desc *vd)
{
	struct pxad_desc_sw *sw_desc = to_pxad_sw_desc(vd);
	struct pxad_desc_hw *updater =
		sw_desc->hw_desc[sw_desc->nb_desc - 1];

	return updater->dtadr != (updater->dsadr + 8);
}

static void pxad_desc_chain(struct virt_dma_desc *vd1,
				struct virt_dma_desc *vd2)
{
	struct pxad_desc_sw *desc1 = to_pxad_sw_desc(vd1);
	struct pxad_desc_sw *desc2 = to_pxad_sw_desc(vd2);
	dma_addr_t dma_to_chain;

	dma_to_chain = desc2->first;
	desc1->hw_desc[desc1->nb_desc - 1]->ddadr = dma_to_chain;
}

static bool pxad_try_hotchain(struct virt_dma_chan *vc,
				  struct virt_dma_desc *vd)
{
	struct virt_dma_desc *vd_last_issued = NULL;
	struct pxad_chan *chan = to_pxad_chan(&vc->chan);

	/*
	 * Attempt to hot chain the tx if the phy is still running. This is
	 * considered successful only if either the channel is still running
	 * after the chaining, or if the chained transfer is completed after
	 * having been hot chained.
	 * A change of alignment is not allowed, and forbids hotchaining.
	 */
	if (is_chan_running(chan)) {
		BUG_ON(list_empty(&vc->desc_issued));

		if (!is_running_chan_misaligned(chan) &&
		    to_pxad_sw_desc(vd)->misaligned)
			return false;

		vd_last_issued = list_entry(vc->desc_issued.prev,
					    struct virt_dma_desc, node);
		pxad_desc_chain(vd_last_issued, vd);
		if (is_chan_running(chan) || is_desc_completed(vd))
			return true;
	}

	return false;
}

static unsigned int clear_chan_irq(struct pxad_phy *phy)
{
	u32 dcsr;
	u32 dint = readl(phy->base + DINT);

	if (!(dint & BIT(phy->idx)))
		return PXA_DCSR_RUN;

	/* clear irq */
	dcsr = phy_readl_relaxed(phy, DCSR);
	phy_writel(phy, dcsr, DCSR);
	if ((dcsr & PXA_DCSR_BUSERR) && (phy->vchan))
		dev_warn(&phy->vchan->vc.chan.dev->device,
			 "%s(chan=%p): PXA_DCSR_BUSERR\n",
			 __func__, &phy->vchan);

	return dcsr & ~PXA_DCSR_RUN;
}

static irqreturn_t pxad_chan_handler(int irq, void *dev_id)
{
	struct pxad_phy *phy = dev_id;
	struct pxad_chan *chan = phy->vchan;
	struct virt_dma_desc *vd, *tmp;
	unsigned int dcsr;
	bool vd_completed;
	dma_cookie_t last_started = 0;

	BUG_ON(!chan);

	dcsr = clear_chan_irq(phy);
	if (dcsr & PXA_DCSR_RUN)
		return IRQ_NONE;

	spin_lock(&chan->vc.lock);
	list_for_each_entry_safe(vd, tmp, &chan->vc.desc_issued, node) {
		vd_completed = is_desc_completed(vd);
		dev_dbg(&chan->vc.chan.dev->device,
			"%s(): checking txd %p[%x]: completed=%d dcsr=0x%x\n",
			__func__, vd, vd->tx.cookie, vd_completed,
			dcsr);
		last_started = vd->tx.cookie;
		if (to_pxad_sw_desc(vd)->cyclic) {
			vchan_cyclic_callback(vd);
			break;
		}
		if (vd_completed) {
			list_del(&vd->node);
			vchan_cookie_complete(vd);
		} else {
			break;
		}
	}

	if (dcsr & PXA_DCSR_BUSERR) {
		chan->bus_error = last_started;
		phy_disable(phy);
	}

	if (!chan->bus_error && dcsr & PXA_DCSR_STOPSTATE) {
		dev_dbg(&chan->vc.chan.dev->device,
		"%s(): channel stopped, submitted_empty=%d issued_empty=%d",
			__func__,
			list_empty(&chan->vc.desc_submitted),
			list_empty(&chan->vc.desc_issued));
		phy_writel_relaxed(phy, dcsr & ~PXA_DCSR_STOPIRQEN, DCSR);

		if (list_empty(&chan->vc.desc_issued)) {
			chan->misaligned =
				!list_empty(&chan->vc.desc_submitted);
		} else {
			vd = list_first_entry(&chan->vc.desc_issued,
					      struct virt_dma_desc, node);
			pxad_launch_chan(chan, to_pxad_sw_desc(vd));
		}
	}
	spin_unlock(&chan->vc.lock);
	wake_up(&chan->wq_state);

	return IRQ_HANDLED;
}

static irqreturn_t pxad_int_handler(int irq, void *dev_id)
{
	struct pxad_device *pdev = dev_id;
	struct pxad_phy *phy;
	u32 dint = readl(pdev->base + DINT);
	int i, ret = IRQ_NONE;

	while (dint) {
		i = __ffs(dint);
		dint &= (dint - 1);
		phy = &pdev->phys[i];
		if (pxad_chan_handler(irq, phy) == IRQ_HANDLED)
			ret = IRQ_HANDLED;
	}

	return ret;
}

static int pxad_alloc_chan_resources(struct dma_chan *dchan)
{
	struct pxad_chan *chan = to_pxad_chan(dchan);
	struct pxad_device *pdev = to_pxad_dev(chan->vc.chan.device);

	if (chan->desc_pool)
		return 1;

	chan->desc_pool = dma_pool_create(dma_chan_name(dchan),
					  pdev->slave.dev,
					  sizeof(struct pxad_desc_hw),
					  __alignof__(struct pxad_desc_hw),
					  0);
	if (!chan->desc_pool) {
		dev_err(&chan->vc.chan.dev->device,
			"%s(): unable to allocate descriptor pool\n",
			__func__);
		return -ENOMEM;
	}

	return 1;
}

static void pxad_free_chan_resources(struct dma_chan *dchan)
{
	struct pxad_chan *chan = to_pxad_chan(dchan);

	vchan_free_chan_resources(&chan->vc);
	dma_pool_destroy(chan->desc_pool);
	chan->desc_pool = NULL;

	chan->drcmr = U32_MAX;
	chan->prio = PXAD_PRIO_LOWEST;
}

static void pxad_free_desc(struct virt_dma_desc *vd)
{
	int i;
	dma_addr_t dma;
	struct pxad_desc_sw *sw_desc = to_pxad_sw_desc(vd);

	BUG_ON(sw_desc->nb_desc == 0);
	for (i = sw_desc->nb_desc - 1; i >= 0; i--) {
		if (i > 0)
			dma = sw_desc->hw_desc[i - 1]->ddadr;
		else
			dma = sw_desc->first;
		dma_pool_free(sw_desc->desc_pool,
			      sw_desc->hw_desc[i], dma);
	}
	sw_desc->nb_desc = 0;
	kfree(sw_desc);
}

static struct pxad_desc_sw *
pxad_alloc_desc(struct pxad_chan *chan, unsigned int nb_hw_desc)
{
	struct pxad_desc_sw *sw_desc;
	dma_addr_t dma;
	int i;

	sw_desc = kzalloc(struct_size(sw_desc, hw_desc, nb_hw_desc),
			  GFP_NOWAIT);
	if (!sw_desc)
		return NULL;
	sw_desc->desc_pool = chan->desc_pool;

	for (i = 0; i < nb_hw_desc; i++) {
		sw_desc->hw_desc[i] = dma_pool_alloc(sw_desc->desc_pool,
						     GFP_NOWAIT, &dma);
		if (!sw_desc->hw_desc[i]) {
			dev_err(&chan->vc.chan.dev->device,
				"%s(): Couldn't allocate the %dth hw_desc from dma_pool %p\n",
				__func__, i, sw_desc->desc_pool);
			goto err;
		}

		if (i == 0)
			sw_desc->first = dma;
		else
			sw_desc->hw_desc[i - 1]->ddadr = dma;
		sw_desc->nb_desc++;
	}

	return sw_desc;
err:
	pxad_free_desc(&sw_desc->vd);
	return NULL;
}

static dma_cookie_t pxad_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct virt_dma_chan *vc = to_virt_chan(tx->chan);
	struct pxad_chan *chan = to_pxad_chan(&vc->chan);
	struct virt_dma_desc *vd_chained = NULL,
		*vd = container_of(tx, struct virt_dma_desc, tx);
	dma_cookie_t cookie;
	unsigned long flags;

	set_updater_desc(to_pxad_sw_desc(vd), tx->flags);

	spin_lock_irqsave(&vc->lock, flags);
	cookie = dma_cookie_assign(tx);

	if (list_empty(&vc->desc_submitted) && pxad_try_hotchain(vc, vd)) {
		list_move_tail(&vd->node, &vc->desc_issued);
		dev_dbg(&chan->vc.chan.dev->device,
			"%s(): txd %p[%x]: submitted (hot linked)\n",
			__func__, vd, cookie);
		goto out;
	}

	/*
	 * Fallback to placing the tx in the submitted queue
	 */
	if (!list_empty(&vc->desc_submitted)) {
		vd_chained = list_entry(vc->desc_submitted.prev,
					struct virt_dma_desc, node);
		/*
		 * Only chain the descriptors if no new misalignment is
		 * introduced. If a new misalignment is chained, let the channel
		 * stop, and be relaunched in misalign mode from the irq
		 * handler.
		 */
		if (chan->misaligned || !to_pxad_sw_desc(vd)->misaligned)
			pxad_desc_chain(vd_chained, vd);
		else
			vd_chained = NULL;
	}
	dev_dbg(&chan->vc.chan.dev->device,
		"%s(): txd %p[%x]: submitted (%s linked)\n",
		__func__, vd, cookie, vd_chained ? "cold" : "not");
	list_move_tail(&vd->node, &vc->desc_submitted);
	chan->misaligned |= to_pxad_sw_desc(vd)->misaligned;

out:
	spin_unlock_irqrestore(&vc->lock, flags);
	return cookie;
}

static void pxad_issue_pending(struct dma_chan *dchan)
{
	struct pxad_chan *chan = to_pxad_chan(dchan);
	struct virt_dma_desc *vd_first;
	unsigned long flags;

	spin_lock_irqsave(&chan->vc.lock, flags);
	if (list_empty(&chan->vc.desc_submitted))
		goto out;

	vd_first = list_first_entry(&chan->vc.desc_submitted,
				    struct virt_dma_desc, node);
	dev_dbg(&chan->vc.chan.dev->device,
		"%s(): txd %p[%x]", __func__, vd_first, vd_first->tx.cookie);

	vchan_issue_pending(&chan->vc);
	if (!pxad_try_hotchain(&chan->vc, vd_first))
		pxad_launch_chan(chan, to_pxad_sw_desc(vd_first));
out:
	spin_unlock_irqrestore(&chan->vc.lock, flags);
}

static inline struct dma_async_tx_descriptor *
pxad_tx_prep(struct virt_dma_chan *vc, struct virt_dma_desc *vd,
		 unsigned long tx_flags)
{
	struct dma_async_tx_descriptor *tx;
	struct pxad_chan *chan = container_of(vc, struct pxad_chan, vc);

	INIT_LIST_HEAD(&vd->node);
	tx = vchan_tx_prep(vc, vd, tx_flags);
	tx->tx_submit = pxad_tx_submit;
	dev_dbg(&chan->vc.chan.dev->device,
		"%s(): vc=%p txd=%p[%x] flags=0x%lx\n", __func__,
		vc, vd, vd->tx.cookie,
		tx_flags);

	return tx;
}

static void pxad_get_config(struct pxad_chan *chan,
			    enum dma_transfer_direction dir,
			    u32 *dcmd, u32 *dev_src, u32 *dev_dst)
{
	u32 maxburst = 0, dev_addr = 0;
	enum dma_slave_buswidth width = DMA_SLAVE_BUSWIDTH_UNDEFINED;
	struct pxad_device *pdev = to_pxad_dev(chan->vc.chan.device);

	*dcmd = 0;
	if (dir == DMA_DEV_TO_MEM) {
		maxburst = chan->cfg.src_maxburst;
		width = chan->cfg.src_addr_width;
		dev_addr = chan->cfg.src_addr;
		*dev_src = dev_addr;
		*dcmd |= PXA_DCMD_INCTRGADDR;
		if (chan->drcmr <= pdev->nr_requestors)
			*dcmd |= PXA_DCMD_FLOWSRC;
	}
	if (dir == DMA_MEM_TO_DEV) {
		maxburst = chan->cfg.dst_maxburst;
		width = chan->cfg.dst_addr_width;
		dev_addr = chan->cfg.dst_addr;
		*dev_dst = dev_addr;
		*dcmd |= PXA_DCMD_INCSRCADDR;
		if (chan->drcmr <= pdev->nr_requestors)
			*dcmd |= PXA_DCMD_FLOWTRG;
	}
	if (dir == DMA_MEM_TO_MEM)
		*dcmd |= PXA_DCMD_BURST32 | PXA_DCMD_INCTRGADDR |
			PXA_DCMD_INCSRCADDR;

	dev_dbg(&chan->vc.chan.dev->device,
		"%s(): dev_addr=0x%x maxburst=%d width=%d  dir=%d\n",
		__func__, dev_addr, maxburst, width, dir);

	if (width == DMA_SLAVE_BUSWIDTH_1_BYTE)
		*dcmd |= PXA_DCMD_WIDTH1;
	else if (width == DMA_SLAVE_BUSWIDTH_2_BYTES)
		*dcmd |= PXA_DCMD_WIDTH2;
	else if (width == DMA_SLAVE_BUSWIDTH_4_BYTES)
		*dcmd |= PXA_DCMD_WIDTH4;

	if (maxburst == 8)
		*dcmd |= PXA_DCMD_BURST8;
	else if (maxburst == 16)
		*dcmd |= PXA_DCMD_BURST16;
	else if (maxburst == 32)
		*dcmd |= PXA_DCMD_BURST32;

	/* FIXME: drivers should be ported over to use the filter
	 * function. Once that's done, the following two lines can
	 * be removed.
	 */
	if (chan->cfg.slave_id)
		chan->drcmr = chan->cfg.slave_id;
}

static struct dma_async_tx_descriptor *
pxad_prep_memcpy(struct dma_chan *dchan,
		 dma_addr_t dma_dst, dma_addr_t dma_src,
		 size_t len, unsigned long flags)
{
	struct pxad_chan *chan = to_pxad_chan(dchan);
	struct pxad_desc_sw *sw_desc;
	struct pxad_desc_hw *hw_desc;
	u32 dcmd;
	unsigned int i, nb_desc = 0;
	size_t copy;

	if (!dchan || !len)
		return NULL;

	dev_dbg(&chan->vc.chan.dev->device,
		"%s(): dma_dst=0x%lx dma_src=0x%lx len=%zu flags=%lx\n",
		__func__, (unsigned long)dma_dst, (unsigned long)dma_src,
		len, flags);
	pxad_get_config(chan, DMA_MEM_TO_MEM, &dcmd, NULL, NULL);

	nb_desc = DIV_ROUND_UP(len, PDMA_MAX_DESC_BYTES);
	sw_desc = pxad_alloc_desc(chan, nb_desc + 1);
	if (!sw_desc)
		return NULL;
	sw_desc->len = len;

	if (!IS_ALIGNED(dma_src, 1 << PDMA_ALIGNMENT) ||
	    !IS_ALIGNED(dma_dst, 1 << PDMA_ALIGNMENT))
		sw_desc->misaligned = true;

	i = 0;
	do {
		hw_desc = sw_desc->hw_desc[i++];
		copy = min_t(size_t, len, PDMA_MAX_DESC_BYTES);
		hw_desc->dcmd = dcmd | (PXA_DCMD_LENGTH & copy);
		hw_desc->dsadr = dma_src;
		hw_desc->dtadr = dma_dst;
		len -= copy;
		dma_src += copy;
		dma_dst += copy;
	} while (len);
	set_updater_desc(sw_desc, flags);

	return pxad_tx_prep(&chan->vc, &sw_desc->vd, flags);
}

static struct dma_async_tx_descriptor *
pxad_prep_slave_sg(struct dma_chan *dchan, struct scatterlist *sgl,
		   unsigned int sg_len, enum dma_transfer_direction dir,
		   unsigned long flags, void *context)
{
	struct pxad_chan *chan = to_pxad_chan(dchan);
	struct pxad_desc_sw *sw_desc;
	size_t len, avail;
	struct scatterlist *sg;
	dma_addr_t dma;
	u32 dcmd, dsadr = 0, dtadr = 0;
	unsigned int nb_desc = 0, i, j = 0;

	if ((sgl == NULL) || (sg_len == 0))
		return NULL;

	pxad_get_config(chan, dir, &dcmd, &dsadr, &dtadr);
	dev_dbg(&chan->vc.chan.dev->device,
		"%s(): dir=%d flags=%lx\n", __func__, dir, flags);

	for_each_sg(sgl, sg, sg_len, i)
		nb_desc += DIV_ROUND_UP(sg_dma_len(sg), PDMA_MAX_DESC_BYTES);
	sw_desc = pxad_alloc_desc(chan, nb_desc + 1);
	if (!sw_desc)
		return NULL;

	for_each_sg(sgl, sg, sg_len, i) {
		dma = sg_dma_address(sg);
		avail = sg_dma_len(sg);
		sw_desc->len += avail;

		do {
			len = min_t(size_t, avail, PDMA_MAX_DESC_BYTES);
			if (dma & 0x7)
				sw_desc->misaligned = true;

			sw_desc->hw_desc[j]->dcmd =
				dcmd | (PXA_DCMD_LENGTH & len);
			sw_desc->hw_desc[j]->dsadr = dsadr ? dsadr : dma;
			sw_desc->hw_desc[j++]->dtadr = dtadr ? dtadr : dma;

			dma += len;
			avail -= len;
		} while (avail);
	}
	set_updater_desc(sw_desc, flags);

	return pxad_tx_prep(&chan->vc, &sw_desc->vd, flags);
}

static struct dma_async_tx_descriptor *
pxad_prep_dma_cyclic(struct dma_chan *dchan,
		     dma_addr_t buf_addr, size_t len, size_t period_len,
		     enum dma_transfer_direction dir, unsigned long flags)
{
	struct pxad_chan *chan = to_pxad_chan(dchan);
	struct pxad_desc_sw *sw_desc;
	struct pxad_desc_hw **phw_desc;
	dma_addr_t dma;
	u32 dcmd, dsadr = 0, dtadr = 0;
	unsigned int nb_desc = 0;

	if (!dchan || !len || !period_len)
		return NULL;
	if ((dir != DMA_DEV_TO_MEM) && (dir != DMA_MEM_TO_DEV)) {
		dev_err(&chan->vc.chan.dev->device,
			"Unsupported direction for cyclic DMA\n");
		return NULL;
	}
	/* the buffer length must be a multiple of period_len */
	if (len % period_len != 0 || period_len > PDMA_MAX_DESC_BYTES ||
	    !IS_ALIGNED(period_len, 1 << PDMA_ALIGNMENT))
		return NULL;

	pxad_get_config(chan, dir, &dcmd, &dsadr, &dtadr);
	dcmd |= PXA_DCMD_ENDIRQEN | (PXA_DCMD_LENGTH & period_len);
	dev_dbg(&chan->vc.chan.dev->device,
		"%s(): buf_addr=0x%lx len=%zu period=%zu dir=%d flags=%lx\n",
		__func__, (unsigned long)buf_addr, len, period_len, dir, flags);

	nb_desc = DIV_ROUND_UP(period_len, PDMA_MAX_DESC_BYTES);
	nb_desc *= DIV_ROUND_UP(len, period_len);
	sw_desc = pxad_alloc_desc(chan, nb_desc + 1);
	if (!sw_desc)
		return NULL;
	sw_desc->cyclic = true;
	sw_desc->len = len;

	phw_desc = sw_desc->hw_desc;
	dma = buf_addr;
	do {
		phw_desc[0]->dsadr = dsadr ? dsadr : dma;
		phw_desc[0]->dtadr = dtadr ? dtadr : dma;
		phw_desc[0]->dcmd = dcmd;
		phw_desc++;
		dma += period_len;
		len -= period_len;
	} while (len);
	set_updater_desc(sw_desc, flags);

	return pxad_tx_prep(&chan->vc, &sw_desc->vd, flags);
}

static int pxad_config(struct dma_chan *dchan,
		       struct dma_slave_config *cfg)
{
	struct pxad_chan *chan = to_pxad_chan(dchan);

	if (!dchan)
		return -EINVAL;

	chan->cfg = *cfg;
	return 0;
}

static int pxad_terminate_all(struct dma_chan *dchan)
{
	struct pxad_chan *chan = to_pxad_chan(dchan);
	struct pxad_device *pdev = to_pxad_dev(chan->vc.chan.device);
	struct virt_dma_desc *vd = NULL;
	unsigned long flags;
	struct pxad_phy *phy;
	LIST_HEAD(head);

	dev_dbg(&chan->vc.chan.dev->device,
		"%s(): vchan %p: terminate all\n", __func__, &chan->vc);

	spin_lock_irqsave(&chan->vc.lock, flags);
	vchan_get_all_descriptors(&chan->vc, &head);

	list_for_each_entry(vd, &head, node) {
		dev_dbg(&chan->vc.chan.dev->device,
			"%s(): cancelling txd %p[%x] (completed=%d)", __func__,
			vd, vd->tx.cookie, is_desc_completed(vd));
	}

	phy = chan->phy;
	if (phy) {
		phy_disable(chan->phy);
		pxad_free_phy(chan);
		chan->phy = NULL;
		spin_lock(&pdev->phy_lock);
		phy->vchan = NULL;
		spin_unlock(&pdev->phy_lock);
	}
	spin_unlock_irqrestore(&chan->vc.lock, flags);
	vchan_dma_desc_free_list(&chan->vc, &head);

	return 0;
}

static unsigned int pxad_residue(struct pxad_chan *chan,
				 dma_cookie_t cookie)
{
	struct virt_dma_desc *vd = NULL;
	struct pxad_desc_sw *sw_desc = NULL;
	struct pxad_desc_hw *hw_desc = NULL;
	u32 curr, start, len, end, residue = 0;
	unsigned long flags;
	bool passed = false;
	int i;

	/*
	 * If the channel does not have a phy pointer anymore, it has already
	 * been completed. Therefore, its residue is 0.
	 */
	if (!chan->phy)
		return 0;

	spin_lock_irqsave(&chan->vc.lock, flags);

	vd = vchan_find_desc(&chan->vc, cookie);
	if (!vd)
		goto out;

	sw_desc = to_pxad_sw_desc(vd);
	if (sw_desc->hw_desc[0]->dcmd & PXA_DCMD_INCSRCADDR)
		curr = phy_readl_relaxed(chan->phy, DSADR);
	else
		curr = phy_readl_relaxed(chan->phy, DTADR);

	/*
	 * curr has to be actually read before checking descriptor
	 * completion, so that a curr inside a status updater
	 * descriptor implies the following test returns true, and
	 * preventing reordering of curr load and the test.
	 */
	rmb();
	if (is_desc_completed(vd))
		goto out;

	for (i = 0; i < sw_desc->nb_desc - 1; i++) {
		hw_desc = sw_desc->hw_desc[i];
		if (sw_desc->hw_desc[0]->dcmd & PXA_DCMD_INCSRCADDR)
			start = hw_desc->dsadr;
		else
			start = hw_desc->dtadr;
		len = hw_desc->dcmd & PXA_DCMD_LENGTH;
		end = start + len;

		/*
		 * 'passed' will be latched once we found the descriptor
		 * which lies inside the boundaries of the curr
		 * pointer. All descriptors that occur in the list
		 * _after_ we found that partially handled descriptor
		 * are still to be processed and are hence added to the
		 * residual bytes counter.
		 */

		if (passed) {
			residue += len;
		} else if (curr >= start && curr <= end) {
			residue += end - curr;
			passed = true;
		}
	}
	if (!passed)
		residue = sw_desc->len;

out:
	spin_unlock_irqrestore(&chan->vc.lock, flags);
	dev_dbg(&chan->vc.chan.dev->device,
		"%s(): txd %p[%x] sw_desc=%p: %d\n",
		__func__, vd, cookie, sw_desc, residue);
	return residue;
}

static enum dma_status pxad_tx_status(struct dma_chan *dchan,
				      dma_cookie_t cookie,
				      struct dma_tx_state *txstate)
{
	struct pxad_chan *chan = to_pxad_chan(dchan);
	enum dma_status ret;

	if (cookie == chan->bus_error)
		return DMA_ERROR;

	ret = dma_cookie_status(dchan, cookie, txstate);
	if (likely(txstate && (ret != DMA_ERROR)))
		dma_set_residue(txstate, pxad_residue(chan, cookie));

	return ret;
}

static void pxad_synchronize(struct dma_chan *dchan)
{
	struct pxad_chan *chan = to_pxad_chan(dchan);

	wait_event(chan->wq_state, !is_chan_running(chan));
	vchan_synchronize(&chan->vc);
}

static void pxad_free_channels(struct dma_device *dmadev)
{
	struct pxad_chan *c, *cn;

	list_for_each_entry_safe(c, cn, &dmadev->channels,
				 vc.chan.device_node) {
		list_del(&c->vc.chan.device_node);
		tasklet_kill(&c->vc.task);
	}
}

static int pxad_remove(struct platform_device *op)
{
	struct pxad_device *pdev = platform_get_drvdata(op);

	pxad_cleanup_debugfs(pdev);
	pxad_free_channels(&pdev->slave);
	return 0;
}

static int pxad_init_phys(struct platform_device *op,
			  struct pxad_device *pdev,
			  unsigned int nb_phy_chans)
{
	int irq0, irq, nr_irq = 0, i, ret;
	struct pxad_phy *phy;

	irq0 = platform_get_irq(op, 0);
	if (irq0 < 0)
		return irq0;

	pdev->phys = devm_kcalloc(&op->dev, nb_phy_chans,
				  sizeof(pdev->phys[0]), GFP_KERNEL);
	if (!pdev->phys)
		return -ENOMEM;

	for (i = 0; i < nb_phy_chans; i++)
		if (platform_get_irq(op, i) > 0)
			nr_irq++;

	for (i = 0; i < nb_phy_chans; i++) {
		phy = &pdev->phys[i];
		phy->base = pdev->base;
		phy->idx = i;
		irq = platform_get_irq(op, i);
		if ((nr_irq > 1) && (irq > 0))
			ret = devm_request_irq(&op->dev, irq,
					       pxad_chan_handler,
					       IRQF_SHARED, "pxa-dma", phy);
		if ((nr_irq == 1) && (i == 0))
			ret = devm_request_irq(&op->dev, irq0,
					       pxad_int_handler,
					       IRQF_SHARED, "pxa-dma", pdev);
		if (ret) {
			dev_err(pdev->slave.dev,
				"%s(): can't request irq %d:%d\n", __func__,
				irq, ret);
			return ret;
		}
	}

	return 0;
}

static const struct of_device_id pxad_dt_ids[] = {
	{ .compatible = "marvell,pdma-1.0", },
	{}
};
MODULE_DEVICE_TABLE(of, pxad_dt_ids);

static struct dma_chan *pxad_dma_xlate(struct of_phandle_args *dma_spec,
					   struct of_dma *ofdma)
{
	struct pxad_device *d = ofdma->of_dma_data;
	struct dma_chan *chan;

	chan = dma_get_any_slave_channel(&d->slave);
	if (!chan)
		return NULL;

	to_pxad_chan(chan)->drcmr = dma_spec->args[0];
	to_pxad_chan(chan)->prio = dma_spec->args[1];

	return chan;
}

static int pxad_init_dmadev(struct platform_device *op,
			    struct pxad_device *pdev,
			    unsigned int nr_phy_chans,
			    unsigned int nr_requestors)
{
	int ret;
	unsigned int i;
	struct pxad_chan *c;

	pdev->nr_chans = nr_phy_chans;
	pdev->nr_requestors = nr_requestors;
	INIT_LIST_HEAD(&pdev->slave.channels);
	pdev->slave.device_alloc_chan_resources = pxad_alloc_chan_resources;
	pdev->slave.device_free_chan_resources = pxad_free_chan_resources;
	pdev->slave.device_tx_status = pxad_tx_status;
	pdev->slave.device_issue_pending = pxad_issue_pending;
	pdev->slave.device_config = pxad_config;
	pdev->slave.device_synchronize = pxad_synchronize;
	pdev->slave.device_terminate_all = pxad_terminate_all;

	if (op->dev.coherent_dma_mask)
		dma_set_mask(&op->dev, op->dev.coherent_dma_mask);
	else
		dma_set_mask(&op->dev, DMA_BIT_MASK(32));

	ret = pxad_init_phys(op, pdev, nr_phy_chans);
	if (ret)
		return ret;

	for (i = 0; i < nr_phy_chans; i++) {
		c = devm_kzalloc(&op->dev, sizeof(*c), GFP_KERNEL);
		if (!c)
			return -ENOMEM;

		c->drcmr = U32_MAX;
		c->prio = PXAD_PRIO_LOWEST;
		c->vc.desc_free = pxad_free_desc;
		vchan_init(&c->vc, &pdev->slave);
		init_waitqueue_head(&c->wq_state);
	}

	return dmaenginem_async_device_register(&pdev->slave);
}

static int pxad_probe(struct platform_device *op)
{
	struct pxad_device *pdev;
	const struct of_device_id *of_id;
	const struct dma_slave_map *slave_map = NULL;
	struct mmp_dma_platdata *pdata = dev_get_platdata(&op->dev);
	struct resource *iores;
	int ret, dma_channels = 0, nb_requestors = 0, slave_map_cnt = 0;
	const enum dma_slave_buswidth widths =
		DMA_SLAVE_BUSWIDTH_1_BYTE   | DMA_SLAVE_BUSWIDTH_2_BYTES |
		DMA_SLAVE_BUSWIDTH_4_BYTES;

	pdev = devm_kzalloc(&op->dev, sizeof(*pdev), GFP_KERNEL);
	if (!pdev)
		return -ENOMEM;

	spin_lock_init(&pdev->phy_lock);

	iores = platform_get_resource(op, IORESOURCE_MEM, 0);
	pdev->base = devm_ioremap_resource(&op->dev, iores);
	if (IS_ERR(pdev->base))
		return PTR_ERR(pdev->base);

	of_id = of_match_device(pxad_dt_ids, &op->dev);
	if (of_id) {
		of_property_read_u32(op->dev.of_node, "#dma-channels",
				     &dma_channels);
		ret = of_property_read_u32(op->dev.of_node, "#dma-requests",
					   &nb_requestors);
		if (ret) {
			dev_warn(pdev->slave.dev,
				 "#dma-requests set to default 32 as missing in OF: %d",
				 ret);
			nb_requestors = 32;
		}
	} else if (pdata && pdata->dma_channels) {
		dma_channels = pdata->dma_channels;
		nb_requestors = pdata->nb_requestors;
		slave_map = pdata->slave_map;
		slave_map_cnt = pdata->slave_map_cnt;
	} else {
		dma_channels = 32;	/* default 32 channel */
	}

	dma_cap_set(DMA_SLAVE, pdev->slave.cap_mask);
	dma_cap_set(DMA_MEMCPY, pdev->slave.cap_mask);
	dma_cap_set(DMA_CYCLIC, pdev->slave.cap_mask);
	dma_cap_set(DMA_PRIVATE, pdev->slave.cap_mask);
	pdev->slave.device_prep_dma_memcpy = pxad_prep_memcpy;
	pdev->slave.device_prep_slave_sg = pxad_prep_slave_sg;
	pdev->slave.device_prep_dma_cyclic = pxad_prep_dma_cyclic;
	pdev->slave.filter.map = slave_map;
	pdev->slave.filter.mapcnt = slave_map_cnt;
	pdev->slave.filter.fn = pxad_filter_fn;

	pdev->slave.copy_align = PDMA_ALIGNMENT;
	pdev->slave.src_addr_widths = widths;
	pdev->slave.dst_addr_widths = widths;
	pdev->slave.directions = BIT(DMA_MEM_TO_DEV) | BIT(DMA_DEV_TO_MEM);
	pdev->slave.residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;
	pdev->slave.descriptor_reuse = true;

	pdev->slave.dev = &op->dev;
	ret = pxad_init_dmadev(op, pdev, dma_channels, nb_requestors);
	if (ret) {
		dev_err(pdev->slave.dev, "unable to register\n");
		return ret;
	}

	if (op->dev.of_node) {
		/* Device-tree DMA controller registration */
		ret = of_dma_controller_register(op->dev.of_node,
						 pxad_dma_xlate, pdev);
		if (ret < 0) {
			dev_err(pdev->slave.dev,
				"of_dma_controller_register failed\n");
			return ret;
		}
	}

	platform_set_drvdata(op, pdev);
	pxad_init_debugfs(pdev);
	dev_info(pdev->slave.dev, "initialized %d channels on %d requestors\n",
		 dma_channels, nb_requestors);
	return 0;
}

static const struct platform_device_id pxad_id_table[] = {
	{ "pxa-dma", },
	{ },
};

static struct platform_driver pxad_driver = {
	.driver		= {
		.name	= "pxa-dma",
		.of_match_table = pxad_dt_ids,
	},
	.id_table	= pxad_id_table,
	.probe		= pxad_probe,
	.remove		= pxad_remove,
};

static bool pxad_filter_fn(struct dma_chan *chan, void *param)
{
	struct pxad_chan *c = to_pxad_chan(chan);
	struct pxad_param *p = param;

	if (chan->device->dev->driver != &pxad_driver.driver)
		return false;

	c->drcmr = p->drcmr;
	c->prio = p->prio;

	return true;
}

module_platform_driver(pxad_driver);

MODULE_DESCRIPTION("Marvell PXA Peripheral DMA Driver");
MODULE_AUTHOR("Robert Jarzmik <robert.jarzmik@free.fr>");
MODULE_LICENSE("GPL v2");
