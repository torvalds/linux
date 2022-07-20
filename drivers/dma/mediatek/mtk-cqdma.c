// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-2019 MediaTek Inc.

/*
 * Driver for MediaTek Command-Queue DMA Controller
 *
 * Author: Shun-Chih Yu <shun-chih.yu@mediatek.com>
 *
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/refcount.h>
#include <linux/slab.h>

#include "../virt-dma.h"

#define MTK_CQDMA_USEC_POLL		10
#define MTK_CQDMA_TIMEOUT_POLL		1000
#define MTK_CQDMA_DMA_BUSWIDTHS		BIT(DMA_SLAVE_BUSWIDTH_4_BYTES)
#define MTK_CQDMA_ALIGN_SIZE		1

/* The default number of virtual channel */
#define MTK_CQDMA_NR_VCHANS		32

/* The default number of physical channel */
#define MTK_CQDMA_NR_PCHANS		3

/* Registers for underlying dma manipulation */
#define MTK_CQDMA_INT_FLAG		0x0
#define MTK_CQDMA_INT_EN		0x4
#define MTK_CQDMA_EN			0x8
#define MTK_CQDMA_RESET			0xc
#define MTK_CQDMA_FLUSH			0x14
#define MTK_CQDMA_SRC			0x1c
#define MTK_CQDMA_DST			0x20
#define MTK_CQDMA_LEN1			0x24
#define MTK_CQDMA_LEN2			0x28
#define MTK_CQDMA_SRC2			0x60
#define MTK_CQDMA_DST2			0x64

/* Registers setting */
#define MTK_CQDMA_EN_BIT		BIT(0)
#define MTK_CQDMA_INT_FLAG_BIT		BIT(0)
#define MTK_CQDMA_INT_EN_BIT		BIT(0)
#define MTK_CQDMA_FLUSH_BIT		BIT(0)

#define MTK_CQDMA_WARM_RST_BIT		BIT(0)
#define MTK_CQDMA_HARD_RST_BIT		BIT(1)

#define MTK_CQDMA_MAX_LEN		GENMASK(27, 0)
#define MTK_CQDMA_ADDR_LIMIT		GENMASK(31, 0)
#define MTK_CQDMA_ADDR2_SHFIT		(32)

/**
 * struct mtk_cqdma_vdesc - The struct holding info describing virtual
 *                         descriptor (CVD)
 * @vd:                    An instance for struct virt_dma_desc
 * @len:                   The total data size device wants to move
 * @residue:               The remaining data size device will move
 * @dest:                  The destination address device wants to move to
 * @src:                   The source address device wants to move from
 * @ch:                    The pointer to the corresponding dma channel
 * @node:                  The lise_head struct to build link-list for VDs
 * @parent:                The pointer to the parent CVD
 */
struct mtk_cqdma_vdesc {
	struct virt_dma_desc vd;
	size_t len;
	size_t residue;
	dma_addr_t dest;
	dma_addr_t src;
	struct dma_chan *ch;

	struct list_head node;
	struct mtk_cqdma_vdesc *parent;
};

/**
 * struct mtk_cqdma_pchan - The struct holding info describing physical
 *                         channel (PC)
 * @queue:                 Queue for the PDs issued to this PC
 * @base:                  The mapped register I/O base of this PC
 * @irq:                   The IRQ that this PC are using
 * @refcnt:                Track how many VCs are using this PC
 * @tasklet:               Tasklet for this PC
 * @lock:                  Lock protect agaisting multiple VCs access PC
 */
struct mtk_cqdma_pchan {
	struct list_head queue;
	void __iomem *base;
	u32 irq;

	refcount_t refcnt;

	struct tasklet_struct tasklet;

	/* lock to protect PC */
	spinlock_t lock;
};

/**
 * struct mtk_cqdma_vchan - The struct holding info describing virtual
 *                         channel (VC)
 * @vc:                    An instance for struct virt_dma_chan
 * @pc:                    The pointer to the underlying PC
 * @issue_completion:	   The wait for all issued descriptors completited
 * @issue_synchronize:	   Bool indicating channel synchronization starts
 */
struct mtk_cqdma_vchan {
	struct virt_dma_chan vc;
	struct mtk_cqdma_pchan *pc;
	struct completion issue_completion;
	bool issue_synchronize;
};

/**
 * struct mtk_cqdma_device - The struct holding info describing CQDMA
 *                          device
 * @ddev:                   An instance for struct dma_device
 * @clk:                    The clock that device internal is using
 * @dma_requests:           The number of VCs the device supports to
 * @dma_channels:           The number of PCs the device supports to
 * @vc:                     The pointer to all available VCs
 * @pc:                     The pointer to all the underlying PCs
 */
struct mtk_cqdma_device {
	struct dma_device ddev;
	struct clk *clk;

	u32 dma_requests;
	u32 dma_channels;
	struct mtk_cqdma_vchan *vc;
	struct mtk_cqdma_pchan **pc;
};

static struct mtk_cqdma_device *to_cqdma_dev(struct dma_chan *chan)
{
	return container_of(chan->device, struct mtk_cqdma_device, ddev);
}

static struct mtk_cqdma_vchan *to_cqdma_vchan(struct dma_chan *chan)
{
	return container_of(chan, struct mtk_cqdma_vchan, vc.chan);
}

static struct mtk_cqdma_vdesc *to_cqdma_vdesc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct mtk_cqdma_vdesc, vd);
}

static struct device *cqdma2dev(struct mtk_cqdma_device *cqdma)
{
	return cqdma->ddev.dev;
}

static u32 mtk_dma_read(struct mtk_cqdma_pchan *pc, u32 reg)
{
	return readl(pc->base + reg);
}

static void mtk_dma_write(struct mtk_cqdma_pchan *pc, u32 reg, u32 val)
{
	writel_relaxed(val, pc->base + reg);
}

static void mtk_dma_rmw(struct mtk_cqdma_pchan *pc, u32 reg,
			u32 mask, u32 set)
{
	u32 val;

	val = mtk_dma_read(pc, reg);
	val &= ~mask;
	val |= set;
	mtk_dma_write(pc, reg, val);
}

static void mtk_dma_set(struct mtk_cqdma_pchan *pc, u32 reg, u32 val)
{
	mtk_dma_rmw(pc, reg, 0, val);
}

static void mtk_dma_clr(struct mtk_cqdma_pchan *pc, u32 reg, u32 val)
{
	mtk_dma_rmw(pc, reg, val, 0);
}

static void mtk_cqdma_vdesc_free(struct virt_dma_desc *vd)
{
	kfree(to_cqdma_vdesc(vd));
}

static int mtk_cqdma_poll_engine_done(struct mtk_cqdma_pchan *pc, bool atomic)
{
	u32 status = 0;

	if (!atomic)
		return readl_poll_timeout(pc->base + MTK_CQDMA_EN,
					  status,
					  !(status & MTK_CQDMA_EN_BIT),
					  MTK_CQDMA_USEC_POLL,
					  MTK_CQDMA_TIMEOUT_POLL);

	return readl_poll_timeout_atomic(pc->base + MTK_CQDMA_EN,
					 status,
					 !(status & MTK_CQDMA_EN_BIT),
					 MTK_CQDMA_USEC_POLL,
					 MTK_CQDMA_TIMEOUT_POLL);
}

static int mtk_cqdma_hard_reset(struct mtk_cqdma_pchan *pc)
{
	mtk_dma_set(pc, MTK_CQDMA_RESET, MTK_CQDMA_HARD_RST_BIT);
	mtk_dma_clr(pc, MTK_CQDMA_RESET, MTK_CQDMA_HARD_RST_BIT);

	return mtk_cqdma_poll_engine_done(pc, true);
}

static void mtk_cqdma_start(struct mtk_cqdma_pchan *pc,
			    struct mtk_cqdma_vdesc *cvd)
{
	/* wait for the previous transaction done */
	if (mtk_cqdma_poll_engine_done(pc, true) < 0)
		dev_err(cqdma2dev(to_cqdma_dev(cvd->ch)), "cqdma wait transaction timeout\n");

	/* warm reset the dma engine for the new transaction */
	mtk_dma_set(pc, MTK_CQDMA_RESET, MTK_CQDMA_WARM_RST_BIT);
	if (mtk_cqdma_poll_engine_done(pc, true) < 0)
		dev_err(cqdma2dev(to_cqdma_dev(cvd->ch)), "cqdma warm reset timeout\n");

	/* setup the source */
	mtk_dma_set(pc, MTK_CQDMA_SRC, cvd->src & MTK_CQDMA_ADDR_LIMIT);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	mtk_dma_set(pc, MTK_CQDMA_SRC2, cvd->src >> MTK_CQDMA_ADDR2_SHFIT);
#else
	mtk_dma_set(pc, MTK_CQDMA_SRC2, 0);
#endif

	/* setup the destination */
	mtk_dma_set(pc, MTK_CQDMA_DST, cvd->dest & MTK_CQDMA_ADDR_LIMIT);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	mtk_dma_set(pc, MTK_CQDMA_DST2, cvd->dest >> MTK_CQDMA_ADDR2_SHFIT);
#else
	mtk_dma_set(pc, MTK_CQDMA_DST2, 0);
#endif

	/* setup the length */
	mtk_dma_set(pc, MTK_CQDMA_LEN1, cvd->len);

	/* start dma engine */
	mtk_dma_set(pc, MTK_CQDMA_EN, MTK_CQDMA_EN_BIT);
}

static void mtk_cqdma_issue_vchan_pending(struct mtk_cqdma_vchan *cvc)
{
	struct virt_dma_desc *vd, *vd2;
	struct mtk_cqdma_pchan *pc = cvc->pc;
	struct mtk_cqdma_vdesc *cvd;
	bool trigger_engine = false;

	lockdep_assert_held(&cvc->vc.lock);
	lockdep_assert_held(&pc->lock);

	list_for_each_entry_safe(vd, vd2, &cvc->vc.desc_issued, node) {
		/* need to trigger dma engine if PC's queue is empty */
		if (list_empty(&pc->queue))
			trigger_engine = true;

		cvd = to_cqdma_vdesc(vd);

		/* add VD into PC's queue */
		list_add_tail(&cvd->node, &pc->queue);

		/* start the dma engine */
		if (trigger_engine)
			mtk_cqdma_start(pc, cvd);

		/* remove VD from list desc_issued */
		list_del(&vd->node);
	}
}

/*
 * return true if this VC is active,
 * meaning that there are VDs under processing by the PC
 */
static bool mtk_cqdma_is_vchan_active(struct mtk_cqdma_vchan *cvc)
{
	struct mtk_cqdma_vdesc *cvd;

	list_for_each_entry(cvd, &cvc->pc->queue, node)
		if (cvc == to_cqdma_vchan(cvd->ch))
			return true;

	return false;
}

/*
 * return the pointer of the CVD that is just consumed by the PC
 */
static struct mtk_cqdma_vdesc
*mtk_cqdma_consume_work_queue(struct mtk_cqdma_pchan *pc)
{
	struct mtk_cqdma_vchan *cvc;
	struct mtk_cqdma_vdesc *cvd, *ret = NULL;

	/* consume a CVD from PC's queue */
	cvd = list_first_entry_or_null(&pc->queue,
				       struct mtk_cqdma_vdesc, node);
	if (unlikely(!cvd || !cvd->parent))
		return NULL;

	cvc = to_cqdma_vchan(cvd->ch);
	ret = cvd;

	/* update residue of the parent CVD */
	cvd->parent->residue -= cvd->len;

	/* delete CVD from PC's queue */
	list_del(&cvd->node);

	spin_lock(&cvc->vc.lock);

	/* check whether all the child CVDs completed */
	if (!cvd->parent->residue) {
		/* add the parent VD into list desc_completed */
		vchan_cookie_complete(&cvd->parent->vd);

		/* setup completion if this VC is under synchronization */
		if (cvc->issue_synchronize && !mtk_cqdma_is_vchan_active(cvc)) {
			complete(&cvc->issue_completion);
			cvc->issue_synchronize = false;
		}
	}

	spin_unlock(&cvc->vc.lock);

	/* start transaction for next CVD in the queue */
	cvd = list_first_entry_or_null(&pc->queue,
				       struct mtk_cqdma_vdesc, node);
	if (cvd)
		mtk_cqdma_start(pc, cvd);

	return ret;
}

static void mtk_cqdma_tasklet_cb(struct tasklet_struct *t)
{
	struct mtk_cqdma_pchan *pc = from_tasklet(pc, t, tasklet);
	struct mtk_cqdma_vdesc *cvd = NULL;
	unsigned long flags;

	spin_lock_irqsave(&pc->lock, flags);
	/* consume the queue */
	cvd = mtk_cqdma_consume_work_queue(pc);
	spin_unlock_irqrestore(&pc->lock, flags);

	/* submit the next CVD */
	if (cvd) {
		dma_run_dependencies(&cvd->vd.tx);

		/*
		 * free child CVD after completion.
		 * the parent CVD would be freeed with desc_free by user.
		 */
		if (cvd->parent != cvd)
			kfree(cvd);
	}

	/* re-enable interrupt before leaving tasklet */
	enable_irq(pc->irq);
}

static irqreturn_t mtk_cqdma_irq(int irq, void *devid)
{
	struct mtk_cqdma_device *cqdma = devid;
	irqreturn_t ret = IRQ_NONE;
	bool schedule_tasklet = false;
	u32 i;

	/* clear interrupt flags for each PC */
	for (i = 0; i < cqdma->dma_channels; ++i, schedule_tasklet = false) {
		spin_lock(&cqdma->pc[i]->lock);
		if (mtk_dma_read(cqdma->pc[i],
				 MTK_CQDMA_INT_FLAG) & MTK_CQDMA_INT_FLAG_BIT) {
			/* clear interrupt */
			mtk_dma_clr(cqdma->pc[i], MTK_CQDMA_INT_FLAG,
				    MTK_CQDMA_INT_FLAG_BIT);

			schedule_tasklet = true;
			ret = IRQ_HANDLED;
		}
		spin_unlock(&cqdma->pc[i]->lock);

		if (schedule_tasklet) {
			/* disable interrupt */
			disable_irq_nosync(cqdma->pc[i]->irq);

			/* schedule the tasklet to handle the transactions */
			tasklet_schedule(&cqdma->pc[i]->tasklet);
		}
	}

	return ret;
}

static struct virt_dma_desc *mtk_cqdma_find_active_desc(struct dma_chan *c,
							dma_cookie_t cookie)
{
	struct mtk_cqdma_vchan *cvc = to_cqdma_vchan(c);
	struct virt_dma_desc *vd;
	unsigned long flags;

	spin_lock_irqsave(&cvc->pc->lock, flags);
	list_for_each_entry(vd, &cvc->pc->queue, node)
		if (vd->tx.cookie == cookie) {
			spin_unlock_irqrestore(&cvc->pc->lock, flags);
			return vd;
		}
	spin_unlock_irqrestore(&cvc->pc->lock, flags);

	list_for_each_entry(vd, &cvc->vc.desc_issued, node)
		if (vd->tx.cookie == cookie)
			return vd;

	return NULL;
}

static enum dma_status mtk_cqdma_tx_status(struct dma_chan *c,
					   dma_cookie_t cookie,
					   struct dma_tx_state *txstate)
{
	struct mtk_cqdma_vchan *cvc = to_cqdma_vchan(c);
	struct mtk_cqdma_vdesc *cvd;
	struct virt_dma_desc *vd;
	enum dma_status ret;
	unsigned long flags;
	size_t bytes = 0;

	ret = dma_cookie_status(c, cookie, txstate);
	if (ret == DMA_COMPLETE || !txstate)
		return ret;

	spin_lock_irqsave(&cvc->vc.lock, flags);
	vd = mtk_cqdma_find_active_desc(c, cookie);
	spin_unlock_irqrestore(&cvc->vc.lock, flags);

	if (vd) {
		cvd = to_cqdma_vdesc(vd);
		bytes = cvd->residue;
	}

	dma_set_residue(txstate, bytes);

	return ret;
}

static void mtk_cqdma_issue_pending(struct dma_chan *c)
{
	struct mtk_cqdma_vchan *cvc = to_cqdma_vchan(c);
	unsigned long pc_flags;
	unsigned long vc_flags;

	/* acquire PC's lock before VS's lock for lock dependency in tasklet */
	spin_lock_irqsave(&cvc->pc->lock, pc_flags);
	spin_lock_irqsave(&cvc->vc.lock, vc_flags);

	if (vchan_issue_pending(&cvc->vc))
		mtk_cqdma_issue_vchan_pending(cvc);

	spin_unlock_irqrestore(&cvc->vc.lock, vc_flags);
	spin_unlock_irqrestore(&cvc->pc->lock, pc_flags);
}

static struct dma_async_tx_descriptor *
mtk_cqdma_prep_dma_memcpy(struct dma_chan *c, dma_addr_t dest,
			  dma_addr_t src, size_t len, unsigned long flags)
{
	struct mtk_cqdma_vdesc **cvd;
	struct dma_async_tx_descriptor *tx = NULL, *prev_tx = NULL;
	size_t i, tlen, nr_vd;

	/*
	 * In the case that trsanction length is larger than the
	 * DMA engine supports, a single memcpy transaction needs
	 * to be separated into several DMA transactions.
	 * Each DMA transaction would be described by a CVD,
	 * and the first one is referred as the parent CVD,
	 * while the others are child CVDs.
	 * The parent CVD's tx descriptor is the only tx descriptor
	 * returned to the DMA user, and it should not be completed
	 * until all the child CVDs completed.
	 */
	nr_vd = DIV_ROUND_UP(len, MTK_CQDMA_MAX_LEN);
	cvd = kcalloc(nr_vd, sizeof(*cvd), GFP_NOWAIT);
	if (!cvd)
		return NULL;

	for (i = 0; i < nr_vd; ++i) {
		cvd[i] = kzalloc(sizeof(*cvd[i]), GFP_NOWAIT);
		if (!cvd[i]) {
			for (; i > 0; --i)
				kfree(cvd[i - 1]);
			return NULL;
		}

		/* setup dma channel */
		cvd[i]->ch = c;

		/* setup sourece, destination, and length */
		tlen = (len > MTK_CQDMA_MAX_LEN) ? MTK_CQDMA_MAX_LEN : len;
		cvd[i]->len = tlen;
		cvd[i]->src = src;
		cvd[i]->dest = dest;

		/* setup tx descriptor */
		tx = vchan_tx_prep(to_virt_chan(c), &cvd[i]->vd, flags);
		tx->next = NULL;

		if (!i) {
			cvd[0]->residue = len;
		} else {
			prev_tx->next = tx;
			cvd[i]->residue = tlen;
		}

		cvd[i]->parent = cvd[0];

		/* update the src, dest, len, prev_tx for the next CVD */
		src += tlen;
		dest += tlen;
		len -= tlen;
		prev_tx = tx;
	}

	return &cvd[0]->vd.tx;
}

static void mtk_cqdma_free_inactive_desc(struct dma_chan *c)
{
	struct virt_dma_chan *vc = to_virt_chan(c);
	unsigned long flags;
	LIST_HEAD(head);

	/*
	 * set desc_allocated, desc_submitted,
	 * and desc_issued as the candicates to be freed
	 */
	spin_lock_irqsave(&vc->lock, flags);
	list_splice_tail_init(&vc->desc_allocated, &head);
	list_splice_tail_init(&vc->desc_submitted, &head);
	list_splice_tail_init(&vc->desc_issued, &head);
	spin_unlock_irqrestore(&vc->lock, flags);

	/* free descriptor lists */
	vchan_dma_desc_free_list(vc, &head);
}

static void mtk_cqdma_free_active_desc(struct dma_chan *c)
{
	struct mtk_cqdma_vchan *cvc = to_cqdma_vchan(c);
	bool sync_needed = false;
	unsigned long pc_flags;
	unsigned long vc_flags;

	/* acquire PC's lock first due to lock dependency in dma ISR */
	spin_lock_irqsave(&cvc->pc->lock, pc_flags);
	spin_lock_irqsave(&cvc->vc.lock, vc_flags);

	/* synchronization is required if this VC is active */
	if (mtk_cqdma_is_vchan_active(cvc)) {
		cvc->issue_synchronize = true;
		sync_needed = true;
	}

	spin_unlock_irqrestore(&cvc->vc.lock, vc_flags);
	spin_unlock_irqrestore(&cvc->pc->lock, pc_flags);

	/* waiting for the completion of this VC */
	if (sync_needed)
		wait_for_completion(&cvc->issue_completion);

	/* free all descriptors in list desc_completed */
	vchan_synchronize(&cvc->vc);

	WARN_ONCE(!list_empty(&cvc->vc.desc_completed),
		  "Desc pending still in list desc_completed\n");
}

static int mtk_cqdma_terminate_all(struct dma_chan *c)
{
	/* free descriptors not processed yet by hardware */
	mtk_cqdma_free_inactive_desc(c);

	/* free descriptors being processed by hardware */
	mtk_cqdma_free_active_desc(c);

	return 0;
}

static int mtk_cqdma_alloc_chan_resources(struct dma_chan *c)
{
	struct mtk_cqdma_device *cqdma = to_cqdma_dev(c);
	struct mtk_cqdma_vchan *vc = to_cqdma_vchan(c);
	struct mtk_cqdma_pchan *pc = NULL;
	u32 i, min_refcnt = U32_MAX, refcnt;
	unsigned long flags;

	/* allocate PC with the minimun refcount */
	for (i = 0; i < cqdma->dma_channels; ++i) {
		refcnt = refcount_read(&cqdma->pc[i]->refcnt);
		if (refcnt < min_refcnt) {
			pc = cqdma->pc[i];
			min_refcnt = refcnt;
		}
	}

	if (!pc)
		return -ENOSPC;

	spin_lock_irqsave(&pc->lock, flags);

	if (!refcount_read(&pc->refcnt)) {
		/* allocate PC when the refcount is zero */
		mtk_cqdma_hard_reset(pc);

		/* enable interrupt for this PC */
		mtk_dma_set(pc, MTK_CQDMA_INT_EN, MTK_CQDMA_INT_EN_BIT);

		/*
		 * refcount_inc would complain increment on 0; use-after-free.
		 * Thus, we need to explicitly set it as 1 initially.
		 */
		refcount_set(&pc->refcnt, 1);
	} else {
		refcount_inc(&pc->refcnt);
	}

	spin_unlock_irqrestore(&pc->lock, flags);

	vc->pc = pc;

	return 0;
}

static void mtk_cqdma_free_chan_resources(struct dma_chan *c)
{
	struct mtk_cqdma_vchan *cvc = to_cqdma_vchan(c);
	unsigned long flags;

	/* free all descriptors in all lists on the VC */
	mtk_cqdma_terminate_all(c);

	spin_lock_irqsave(&cvc->pc->lock, flags);

	/* PC is not freed until there is no VC mapped to it */
	if (refcount_dec_and_test(&cvc->pc->refcnt)) {
		/* start the flush operation and stop the engine */
		mtk_dma_set(cvc->pc, MTK_CQDMA_FLUSH, MTK_CQDMA_FLUSH_BIT);

		/* wait for the completion of flush operation */
		if (mtk_cqdma_poll_engine_done(cvc->pc, true) < 0)
			dev_err(cqdma2dev(to_cqdma_dev(c)), "cqdma flush timeout\n");

		/* clear the flush bit and interrupt flag */
		mtk_dma_clr(cvc->pc, MTK_CQDMA_FLUSH, MTK_CQDMA_FLUSH_BIT);
		mtk_dma_clr(cvc->pc, MTK_CQDMA_INT_FLAG,
			    MTK_CQDMA_INT_FLAG_BIT);

		/* disable interrupt for this PC */
		mtk_dma_clr(cvc->pc, MTK_CQDMA_INT_EN, MTK_CQDMA_INT_EN_BIT);
	}

	spin_unlock_irqrestore(&cvc->pc->lock, flags);
}

static int mtk_cqdma_hw_init(struct mtk_cqdma_device *cqdma)
{
	unsigned long flags;
	int err;
	u32 i;

	pm_runtime_enable(cqdma2dev(cqdma));
	pm_runtime_get_sync(cqdma2dev(cqdma));

	err = clk_prepare_enable(cqdma->clk);

	if (err) {
		pm_runtime_put_sync(cqdma2dev(cqdma));
		pm_runtime_disable(cqdma2dev(cqdma));
		return err;
	}

	/* reset all PCs */
	for (i = 0; i < cqdma->dma_channels; ++i) {
		spin_lock_irqsave(&cqdma->pc[i]->lock, flags);
		if (mtk_cqdma_hard_reset(cqdma->pc[i]) < 0) {
			dev_err(cqdma2dev(cqdma), "cqdma hard reset timeout\n");
			spin_unlock_irqrestore(&cqdma->pc[i]->lock, flags);

			clk_disable_unprepare(cqdma->clk);
			pm_runtime_put_sync(cqdma2dev(cqdma));
			pm_runtime_disable(cqdma2dev(cqdma));
			return -EINVAL;
		}
		spin_unlock_irqrestore(&cqdma->pc[i]->lock, flags);
	}

	return 0;
}

static void mtk_cqdma_hw_deinit(struct mtk_cqdma_device *cqdma)
{
	unsigned long flags;
	u32 i;

	/* reset all PCs */
	for (i = 0; i < cqdma->dma_channels; ++i) {
		spin_lock_irqsave(&cqdma->pc[i]->lock, flags);
		if (mtk_cqdma_hard_reset(cqdma->pc[i]) < 0)
			dev_err(cqdma2dev(cqdma), "cqdma hard reset timeout\n");
		spin_unlock_irqrestore(&cqdma->pc[i]->lock, flags);
	}

	clk_disable_unprepare(cqdma->clk);

	pm_runtime_put_sync(cqdma2dev(cqdma));
	pm_runtime_disable(cqdma2dev(cqdma));
}

static const struct of_device_id mtk_cqdma_match[] = {
	{ .compatible = "mediatek,mt6765-cqdma" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_cqdma_match);

static int mtk_cqdma_probe(struct platform_device *pdev)
{
	struct mtk_cqdma_device *cqdma;
	struct mtk_cqdma_vchan *vc;
	struct dma_device *dd;
	int err;
	u32 i;

	cqdma = devm_kzalloc(&pdev->dev, sizeof(*cqdma), GFP_KERNEL);
	if (!cqdma)
		return -ENOMEM;

	dd = &cqdma->ddev;

	cqdma->clk = devm_clk_get(&pdev->dev, "cqdma");
	if (IS_ERR(cqdma->clk)) {
		dev_err(&pdev->dev, "No clock for %s\n",
			dev_name(&pdev->dev));
		return PTR_ERR(cqdma->clk);
	}

	dma_cap_set(DMA_MEMCPY, dd->cap_mask);

	dd->copy_align = MTK_CQDMA_ALIGN_SIZE;
	dd->device_alloc_chan_resources = mtk_cqdma_alloc_chan_resources;
	dd->device_free_chan_resources = mtk_cqdma_free_chan_resources;
	dd->device_tx_status = mtk_cqdma_tx_status;
	dd->device_issue_pending = mtk_cqdma_issue_pending;
	dd->device_prep_dma_memcpy = mtk_cqdma_prep_dma_memcpy;
	dd->device_terminate_all = mtk_cqdma_terminate_all;
	dd->src_addr_widths = MTK_CQDMA_DMA_BUSWIDTHS;
	dd->dst_addr_widths = MTK_CQDMA_DMA_BUSWIDTHS;
	dd->directions = BIT(DMA_MEM_TO_MEM);
	dd->residue_granularity = DMA_RESIDUE_GRANULARITY_SEGMENT;
	dd->dev = &pdev->dev;
	INIT_LIST_HEAD(&dd->channels);

	if (pdev->dev.of_node && of_property_read_u32(pdev->dev.of_node,
						      "dma-requests",
						      &cqdma->dma_requests)) {
		dev_info(&pdev->dev,
			 "Using %u as missing dma-requests property\n",
			 MTK_CQDMA_NR_VCHANS);

		cqdma->dma_requests = MTK_CQDMA_NR_VCHANS;
	}

	if (pdev->dev.of_node && of_property_read_u32(pdev->dev.of_node,
						      "dma-channels",
						      &cqdma->dma_channels)) {
		dev_info(&pdev->dev,
			 "Using %u as missing dma-channels property\n",
			 MTK_CQDMA_NR_PCHANS);

		cqdma->dma_channels = MTK_CQDMA_NR_PCHANS;
	}

	cqdma->pc = devm_kcalloc(&pdev->dev, cqdma->dma_channels,
				 sizeof(*cqdma->pc), GFP_KERNEL);
	if (!cqdma->pc)
		return -ENOMEM;

	/* initialization for PCs */
	for (i = 0; i < cqdma->dma_channels; ++i) {
		cqdma->pc[i] = devm_kcalloc(&pdev->dev, 1,
					    sizeof(**cqdma->pc), GFP_KERNEL);
		if (!cqdma->pc[i])
			return -ENOMEM;

		INIT_LIST_HEAD(&cqdma->pc[i]->queue);
		spin_lock_init(&cqdma->pc[i]->lock);
		refcount_set(&cqdma->pc[i]->refcnt, 0);
		cqdma->pc[i]->base = devm_platform_ioremap_resource(pdev, i);
		if (IS_ERR(cqdma->pc[i]->base))
			return PTR_ERR(cqdma->pc[i]->base);

		/* allocate IRQ resource */
		err = platform_get_irq(pdev, i);
		if (err < 0)
			return err;
		cqdma->pc[i]->irq = err;

		err = devm_request_irq(&pdev->dev, cqdma->pc[i]->irq,
				       mtk_cqdma_irq, 0, dev_name(&pdev->dev),
				       cqdma);
		if (err) {
			dev_err(&pdev->dev,
				"request_irq failed with err %d\n", err);
			return -EINVAL;
		}
	}

	/* allocate resource for VCs */
	cqdma->vc = devm_kcalloc(&pdev->dev, cqdma->dma_requests,
				 sizeof(*cqdma->vc), GFP_KERNEL);
	if (!cqdma->vc)
		return -ENOMEM;

	for (i = 0; i < cqdma->dma_requests; i++) {
		vc = &cqdma->vc[i];
		vc->vc.desc_free = mtk_cqdma_vdesc_free;
		vchan_init(&vc->vc, dd);
		init_completion(&vc->issue_completion);
	}

	err = dma_async_device_register(dd);
	if (err)
		return err;

	err = of_dma_controller_register(pdev->dev.of_node,
					 of_dma_xlate_by_chan_id, cqdma);
	if (err) {
		dev_err(&pdev->dev,
			"MediaTek CQDMA OF registration failed %d\n", err);
		goto err_unregister;
	}

	err = mtk_cqdma_hw_init(cqdma);
	if (err) {
		dev_err(&pdev->dev,
			"MediaTek CQDMA HW initialization failed %d\n", err);
		goto err_unregister;
	}

	platform_set_drvdata(pdev, cqdma);

	/* initialize tasklet for each PC */
	for (i = 0; i < cqdma->dma_channels; ++i)
		tasklet_setup(&cqdma->pc[i]->tasklet, mtk_cqdma_tasklet_cb);

	dev_info(&pdev->dev, "MediaTek CQDMA driver registered\n");

	return 0;

err_unregister:
	dma_async_device_unregister(dd);

	return err;
}

static int mtk_cqdma_remove(struct platform_device *pdev)
{
	struct mtk_cqdma_device *cqdma = platform_get_drvdata(pdev);
	struct mtk_cqdma_vchan *vc;
	unsigned long flags;
	int i;

	/* kill VC task */
	for (i = 0; i < cqdma->dma_requests; i++) {
		vc = &cqdma->vc[i];

		list_del(&vc->vc.chan.device_node);
		tasklet_kill(&vc->vc.task);
	}

	/* disable interrupt */
	for (i = 0; i < cqdma->dma_channels; i++) {
		spin_lock_irqsave(&cqdma->pc[i]->lock, flags);
		mtk_dma_clr(cqdma->pc[i], MTK_CQDMA_INT_EN,
			    MTK_CQDMA_INT_EN_BIT);
		spin_unlock_irqrestore(&cqdma->pc[i]->lock, flags);

		/* Waits for any pending IRQ handlers to complete */
		synchronize_irq(cqdma->pc[i]->irq);

		tasklet_kill(&cqdma->pc[i]->tasklet);
	}

	/* disable hardware */
	mtk_cqdma_hw_deinit(cqdma);

	dma_async_device_unregister(&cqdma->ddev);
	of_dma_controller_free(pdev->dev.of_node);

	return 0;
}

static struct platform_driver mtk_cqdma_driver = {
	.probe = mtk_cqdma_probe,
	.remove = mtk_cqdma_remove,
	.driver = {
		.name           = KBUILD_MODNAME,
		.of_match_table = mtk_cqdma_match,
	},
};
module_platform_driver(mtk_cqdma_driver);

MODULE_DESCRIPTION("MediaTek CQDMA Controller Driver");
MODULE_AUTHOR("Shun-Chih Yu <shun-chih.yu@mediatek.com>");
MODULE_LICENSE("GPL v2");
