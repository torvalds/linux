// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-2018 MediaTek Inc.

/*
 * Driver for MediaTek High-Speed DMA Controller
 *
 * Author: Sean Wang <sean.wang@mediatek.com>
 *
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/iopoll.h>
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

#define MTK_HSDMA_USEC_POLL		20
#define MTK_HSDMA_TIMEOUT_POLL		200000
#define MTK_HSDMA_DMA_BUSWIDTHS		BIT(DMA_SLAVE_BUSWIDTH_4_BYTES)

/* The default number of virtual channel */
#define MTK_HSDMA_NR_VCHANS		3

/* Only one physical channel supported */
#define MTK_HSDMA_NR_MAX_PCHANS		1

/* Macro for physical descriptor (PD) manipulation */
/* The number of PD which must be 2 of power */
#define MTK_DMA_SIZE			64
#define MTK_HSDMA_NEXT_DESP_IDX(x, y)	(((x) + 1) & ((y) - 1))
#define MTK_HSDMA_LAST_DESP_IDX(x, y)	(((x) - 1) & ((y) - 1))
#define MTK_HSDMA_MAX_LEN		0x3f80
#define MTK_HSDMA_ALIGN_SIZE		4
#define MTK_HSDMA_PLEN_MASK		0x3fff
#define MTK_HSDMA_DESC_PLEN(x)		(((x) & MTK_HSDMA_PLEN_MASK) << 16)
#define MTK_HSDMA_DESC_PLEN_GET(x)	(((x) >> 16) & MTK_HSDMA_PLEN_MASK)

/* Registers for underlying ring manipulation */
#define MTK_HSDMA_TX_BASE		0x0
#define MTK_HSDMA_TX_CNT		0x4
#define MTK_HSDMA_TX_CPU		0x8
#define MTK_HSDMA_TX_DMA		0xc
#define MTK_HSDMA_RX_BASE		0x100
#define MTK_HSDMA_RX_CNT		0x104
#define MTK_HSDMA_RX_CPU		0x108
#define MTK_HSDMA_RX_DMA		0x10c

/* Registers for global setup */
#define MTK_HSDMA_GLO			0x204
#define MTK_HSDMA_GLO_MULTI_DMA		BIT(10)
#define MTK_HSDMA_TX_WB_DDONE		BIT(6)
#define MTK_HSDMA_BURST_64BYTES		(0x2 << 4)
#define MTK_HSDMA_GLO_RX_BUSY		BIT(3)
#define MTK_HSDMA_GLO_RX_DMA		BIT(2)
#define MTK_HSDMA_GLO_TX_BUSY		BIT(1)
#define MTK_HSDMA_GLO_TX_DMA		BIT(0)
#define MTK_HSDMA_GLO_DMA		(MTK_HSDMA_GLO_TX_DMA |	\
					 MTK_HSDMA_GLO_RX_DMA)
#define MTK_HSDMA_GLO_BUSY		(MTK_HSDMA_GLO_RX_BUSY | \
					 MTK_HSDMA_GLO_TX_BUSY)
#define MTK_HSDMA_GLO_DEFAULT		(MTK_HSDMA_GLO_TX_DMA | \
					 MTK_HSDMA_GLO_RX_DMA | \
					 MTK_HSDMA_TX_WB_DDONE | \
					 MTK_HSDMA_BURST_64BYTES | \
					 MTK_HSDMA_GLO_MULTI_DMA)

/* Registers for reset */
#define MTK_HSDMA_RESET			0x208
#define MTK_HSDMA_RST_TX		BIT(0)
#define MTK_HSDMA_RST_RX		BIT(16)

/* Registers for interrupt control */
#define MTK_HSDMA_DLYINT		0x20c
#define MTK_HSDMA_RXDLY_INT_EN		BIT(15)

/* Interrupt fires when the pending number's more than the specified */
#define MTK_HSDMA_RXMAX_PINT(x)		(((x) & 0x7f) << 8)

/* Interrupt fires when the pending time's more than the specified in 20 us */
#define MTK_HSDMA_RXMAX_PTIME(x)	((x) & 0x7f)
#define MTK_HSDMA_DLYINT_DEFAULT	(MTK_HSDMA_RXDLY_INT_EN | \
					 MTK_HSDMA_RXMAX_PINT(20) | \
					 MTK_HSDMA_RXMAX_PTIME(20))
#define MTK_HSDMA_INT_STATUS		0x220
#define MTK_HSDMA_INT_ENABLE		0x228
#define MTK_HSDMA_INT_RXDONE		BIT(16)

enum mtk_hsdma_vdesc_flag {
	MTK_HSDMA_VDESC_FINISHED	= 0x01,
};

#define IS_MTK_HSDMA_VDESC_FINISHED(x) ((x) == MTK_HSDMA_VDESC_FINISHED)

/**
 * struct mtk_hsdma_pdesc - This is the struct holding info describing physical
 *			    descriptor (PD) and its placement must be kept at
 *			    4-bytes alignment in little endian order.
 * @desc[1-4]:		    The control pad used to indicate hardware how to
 *			    deal with the descriptor such as source and
 *			    destination address and data length. The maximum
 *			    data length each pdesc can handle is 0x3f80 bytes
 */
struct mtk_hsdma_pdesc {
	__le32 desc1;
	__le32 desc2;
	__le32 desc3;
	__le32 desc4;
} __packed __aligned(4);

/**
 * struct mtk_hsdma_vdesc - This is the struct holding info describing virtual
 *			    descriptor (VD)
 * @vd:			    An instance for struct virt_dma_desc
 * @len:		    The total data size device wants to move
 * @residue:		    The remaining data size device will move
 * @dest:		    The destination address device wants to move to
 * @src:		    The source address device wants to move from
 */
struct mtk_hsdma_vdesc {
	struct virt_dma_desc vd;
	size_t len;
	size_t residue;
	dma_addr_t dest;
	dma_addr_t src;
};

/**
 * struct mtk_hsdma_cb - This is the struct holding extra info required for RX
 *			 ring to know what relevant VD the the PD is being
 *			 mapped to.
 * @vd:			 Pointer to the relevant VD.
 * @flag:		 Flag indicating what action should be taken when VD
 *			 is completed.
 */
struct mtk_hsdma_cb {
	struct virt_dma_desc *vd;
	enum mtk_hsdma_vdesc_flag flag;
};

/**
 * struct mtk_hsdma_ring - This struct holds info describing underlying ring
 *			   space
 * @txd:		   The descriptor TX ring which describes DMA source
 *			   information
 * @rxd:		   The descriptor RX ring which describes DMA
 *			   destination information
 * @cb:			   The extra information pointed at by RX ring
 * @tphys:		   The physical addr of TX ring
 * @rphys:		   The physical addr of RX ring
 * @cur_tptr:		   Pointer to the next free descriptor used by the host
 * @cur_rptr:		   Pointer to the last done descriptor by the device
 */
struct mtk_hsdma_ring {
	struct mtk_hsdma_pdesc *txd;
	struct mtk_hsdma_pdesc *rxd;
	struct mtk_hsdma_cb *cb;
	dma_addr_t tphys;
	dma_addr_t rphys;
	u16 cur_tptr;
	u16 cur_rptr;
};

/**
 * struct mtk_hsdma_pchan - This is the struct holding info describing physical
 *			   channel (PC)
 * @ring:		   An instance for the underlying ring
 * @sz_ring:		   Total size allocated for the ring
 * @nr_free:		   Total number of free rooms in the ring. It would
 *			   be accessed and updated frequently between IRQ
 *			   context and user context to reflect whether ring
 *			   can accept requests from VD.
 */
struct mtk_hsdma_pchan {
	struct mtk_hsdma_ring ring;
	size_t sz_ring;
	atomic_t nr_free;
};

/**
 * struct mtk_hsdma_vchan - This is the struct holding info describing virtual
 *			   channel (VC)
 * @vc:			   An instance for struct virt_dma_chan
 * @issue_completion:	   The wait for all issued descriptors completited
 * @issue_synchronize:	   Bool indicating channel synchronization starts
 * @desc_hw_processing:	   List those descriptors the hardware is processing,
 *			   which is protected by vc.lock
 */
struct mtk_hsdma_vchan {
	struct virt_dma_chan vc;
	struct completion issue_completion;
	bool issue_synchronize;
	struct list_head desc_hw_processing;
};

/**
 * struct mtk_hsdma_soc - This is the struct holding differences among SoCs
 * @ddone:		  Bit mask for DDONE
 * @ls0:		  Bit mask for LS0
 */
struct mtk_hsdma_soc {
	__le32 ddone;
	__le32 ls0;
};

/**
 * struct mtk_hsdma_device - This is the struct holding info describing HSDMA
 *			     device
 * @ddev:		     An instance for struct dma_device
 * @base:		     The mapped register I/O base
 * @clk:		     The clock that device internal is using
 * @irq:		     The IRQ that device are using
 * @dma_requests:	     The number of VCs the device supports to
 * @vc:			     The pointer to all available VCs
 * @pc:			     The pointer to the underlying PC
 * @pc_refcnt:		     Track how many VCs are using the PC
 * @lock:		     Lock protect agaisting multiple VCs access PC
 * @soc:		     The pointer to area holding differences among
 *			     vaious platform
 */
struct mtk_hsdma_device {
	struct dma_device ddev;
	void __iomem *base;
	struct clk *clk;
	u32 irq;

	u32 dma_requests;
	struct mtk_hsdma_vchan *vc;
	struct mtk_hsdma_pchan *pc;
	refcount_t pc_refcnt;

	/* Lock used to protect against multiple VCs access PC */
	spinlock_t lock;

	const struct mtk_hsdma_soc *soc;
};

static struct mtk_hsdma_device *to_hsdma_dev(struct dma_chan *chan)
{
	return container_of(chan->device, struct mtk_hsdma_device, ddev);
}

static inline struct mtk_hsdma_vchan *to_hsdma_vchan(struct dma_chan *chan)
{
	return container_of(chan, struct mtk_hsdma_vchan, vc.chan);
}

static struct mtk_hsdma_vdesc *to_hsdma_vdesc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct mtk_hsdma_vdesc, vd);
}

static struct device *hsdma2dev(struct mtk_hsdma_device *hsdma)
{
	return hsdma->ddev.dev;
}

static u32 mtk_dma_read(struct mtk_hsdma_device *hsdma, u32 reg)
{
	return readl(hsdma->base + reg);
}

static void mtk_dma_write(struct mtk_hsdma_device *hsdma, u32 reg, u32 val)
{
	writel(val, hsdma->base + reg);
}

static void mtk_dma_rmw(struct mtk_hsdma_device *hsdma, u32 reg,
			u32 mask, u32 set)
{
	u32 val;

	val = mtk_dma_read(hsdma, reg);
	val &= ~mask;
	val |= set;
	mtk_dma_write(hsdma, reg, val);
}

static void mtk_dma_set(struct mtk_hsdma_device *hsdma, u32 reg, u32 val)
{
	mtk_dma_rmw(hsdma, reg, 0, val);
}

static void mtk_dma_clr(struct mtk_hsdma_device *hsdma, u32 reg, u32 val)
{
	mtk_dma_rmw(hsdma, reg, val, 0);
}

static void mtk_hsdma_vdesc_free(struct virt_dma_desc *vd)
{
	kfree(container_of(vd, struct mtk_hsdma_vdesc, vd));
}

static int mtk_hsdma_busy_wait(struct mtk_hsdma_device *hsdma)
{
	u32 status = 0;

	return readl_poll_timeout(hsdma->base + MTK_HSDMA_GLO, status,
				  !(status & MTK_HSDMA_GLO_BUSY),
				  MTK_HSDMA_USEC_POLL,
				  MTK_HSDMA_TIMEOUT_POLL);
}

static int mtk_hsdma_alloc_pchan(struct mtk_hsdma_device *hsdma,
				 struct mtk_hsdma_pchan *pc)
{
	struct mtk_hsdma_ring *ring = &pc->ring;
	int err;

	memset(pc, 0, sizeof(*pc));

	/*
	 * Allocate ring space where [0 ... MTK_DMA_SIZE - 1] is for TX ring
	 * and [MTK_DMA_SIZE ... 2 * MTK_DMA_SIZE - 1] is for RX ring.
	 */
	pc->sz_ring = 2 * MTK_DMA_SIZE * sizeof(*ring->txd);
	ring->txd = dma_zalloc_coherent(hsdma2dev(hsdma), pc->sz_ring,
					&ring->tphys, GFP_NOWAIT);
	if (!ring->txd)
		return -ENOMEM;

	ring->rxd = &ring->txd[MTK_DMA_SIZE];
	ring->rphys = ring->tphys + MTK_DMA_SIZE * sizeof(*ring->txd);
	ring->cur_tptr = 0;
	ring->cur_rptr = MTK_DMA_SIZE - 1;

	ring->cb = kcalloc(MTK_DMA_SIZE, sizeof(*ring->cb), GFP_NOWAIT);
	if (!ring->cb) {
		err = -ENOMEM;
		goto err_free_dma;
	}

	atomic_set(&pc->nr_free, MTK_DMA_SIZE - 1);

	/* Disable HSDMA and wait for the completion */
	mtk_dma_clr(hsdma, MTK_HSDMA_GLO, MTK_HSDMA_GLO_DMA);
	err = mtk_hsdma_busy_wait(hsdma);
	if (err)
		goto err_free_cb;

	/* Reset */
	mtk_dma_set(hsdma, MTK_HSDMA_RESET,
		    MTK_HSDMA_RST_TX | MTK_HSDMA_RST_RX);
	mtk_dma_clr(hsdma, MTK_HSDMA_RESET,
		    MTK_HSDMA_RST_TX | MTK_HSDMA_RST_RX);

	/* Setup HSDMA initial pointer in the ring */
	mtk_dma_write(hsdma, MTK_HSDMA_TX_BASE, ring->tphys);
	mtk_dma_write(hsdma, MTK_HSDMA_TX_CNT, MTK_DMA_SIZE);
	mtk_dma_write(hsdma, MTK_HSDMA_TX_CPU, ring->cur_tptr);
	mtk_dma_write(hsdma, MTK_HSDMA_TX_DMA, 0);
	mtk_dma_write(hsdma, MTK_HSDMA_RX_BASE, ring->rphys);
	mtk_dma_write(hsdma, MTK_HSDMA_RX_CNT, MTK_DMA_SIZE);
	mtk_dma_write(hsdma, MTK_HSDMA_RX_CPU, ring->cur_rptr);
	mtk_dma_write(hsdma, MTK_HSDMA_RX_DMA, 0);

	/* Enable HSDMA */
	mtk_dma_set(hsdma, MTK_HSDMA_GLO, MTK_HSDMA_GLO_DMA);

	/* Setup delayed interrupt */
	mtk_dma_write(hsdma, MTK_HSDMA_DLYINT, MTK_HSDMA_DLYINT_DEFAULT);

	/* Enable interrupt */
	mtk_dma_set(hsdma, MTK_HSDMA_INT_ENABLE, MTK_HSDMA_INT_RXDONE);

	return 0;

err_free_cb:
	kfree(ring->cb);

err_free_dma:
	dma_free_coherent(hsdma2dev(hsdma),
			  pc->sz_ring, ring->txd, ring->tphys);
	return err;
}

static void mtk_hsdma_free_pchan(struct mtk_hsdma_device *hsdma,
				 struct mtk_hsdma_pchan *pc)
{
	struct mtk_hsdma_ring *ring = &pc->ring;

	/* Disable HSDMA and then wait for the completion */
	mtk_dma_clr(hsdma, MTK_HSDMA_GLO, MTK_HSDMA_GLO_DMA);
	mtk_hsdma_busy_wait(hsdma);

	/* Reset pointer in the ring */
	mtk_dma_clr(hsdma, MTK_HSDMA_INT_ENABLE, MTK_HSDMA_INT_RXDONE);
	mtk_dma_write(hsdma, MTK_HSDMA_TX_BASE, 0);
	mtk_dma_write(hsdma, MTK_HSDMA_TX_CNT, 0);
	mtk_dma_write(hsdma, MTK_HSDMA_TX_CPU, 0);
	mtk_dma_write(hsdma, MTK_HSDMA_RX_BASE, 0);
	mtk_dma_write(hsdma, MTK_HSDMA_RX_CNT, 0);
	mtk_dma_write(hsdma, MTK_HSDMA_RX_CPU, MTK_DMA_SIZE - 1);

	kfree(ring->cb);

	dma_free_coherent(hsdma2dev(hsdma),
			  pc->sz_ring, ring->txd, ring->tphys);
}

static int mtk_hsdma_issue_pending_vdesc(struct mtk_hsdma_device *hsdma,
					 struct mtk_hsdma_pchan *pc,
					 struct mtk_hsdma_vdesc *hvd)
{
	struct mtk_hsdma_ring *ring = &pc->ring;
	struct mtk_hsdma_pdesc *txd, *rxd;
	u16 reserved, prev, tlen, num_sgs;
	unsigned long flags;

	/* Protect against PC is accessed by multiple VCs simultaneously */
	spin_lock_irqsave(&hsdma->lock, flags);

	/*
	 * Reserve rooms, where pc->nr_free is used to track how many free
	 * rooms in the ring being updated in user and IRQ context.
	 */
	num_sgs = DIV_ROUND_UP(hvd->len, MTK_HSDMA_MAX_LEN);
	reserved = min_t(u16, num_sgs, atomic_read(&pc->nr_free));

	if (!reserved) {
		spin_unlock_irqrestore(&hsdma->lock, flags);
		return -ENOSPC;
	}

	atomic_sub(reserved, &pc->nr_free);

	while (reserved--) {
		/* Limit size by PD capability for valid data moving */
		tlen = (hvd->len > MTK_HSDMA_MAX_LEN) ?
		       MTK_HSDMA_MAX_LEN : hvd->len;

		/*
		 * Setup PDs using the remaining VD info mapped on those
		 * reserved rooms. And since RXD is shared memory between the
		 * host and the device allocated by dma_alloc_coherent call,
		 * the helper macro WRITE_ONCE can ensure the data written to
		 * RAM would really happens.
		 */
		txd = &ring->txd[ring->cur_tptr];
		WRITE_ONCE(txd->desc1, hvd->src);
		WRITE_ONCE(txd->desc2,
			   hsdma->soc->ls0 | MTK_HSDMA_DESC_PLEN(tlen));

		rxd = &ring->rxd[ring->cur_tptr];
		WRITE_ONCE(rxd->desc1, hvd->dest);
		WRITE_ONCE(rxd->desc2, MTK_HSDMA_DESC_PLEN(tlen));

		/* Associate VD, the PD belonged to */
		ring->cb[ring->cur_tptr].vd = &hvd->vd;

		/* Move forward the pointer of TX ring */
		ring->cur_tptr = MTK_HSDMA_NEXT_DESP_IDX(ring->cur_tptr,
							 MTK_DMA_SIZE);

		/* Update VD with remaining data */
		hvd->src  += tlen;
		hvd->dest += tlen;
		hvd->len  -= tlen;
	}

	/*
	 * Tagging flag for the last PD for VD will be responsible for
	 * completing VD.
	 */
	if (!hvd->len) {
		prev = MTK_HSDMA_LAST_DESP_IDX(ring->cur_tptr, MTK_DMA_SIZE);
		ring->cb[prev].flag = MTK_HSDMA_VDESC_FINISHED;
	}

	/* Ensure all changes indeed done before we're going on */
	wmb();

	/*
	 * Updating into hardware the pointer of TX ring lets HSDMA to take
	 * action for those pending PDs.
	 */
	mtk_dma_write(hsdma, MTK_HSDMA_TX_CPU, ring->cur_tptr);

	spin_unlock_irqrestore(&hsdma->lock, flags);

	return 0;
}

static void mtk_hsdma_issue_vchan_pending(struct mtk_hsdma_device *hsdma,
					  struct mtk_hsdma_vchan *hvc)
{
	struct virt_dma_desc *vd, *vd2;
	int err;

	lockdep_assert_held(&hvc->vc.lock);

	list_for_each_entry_safe(vd, vd2, &hvc->vc.desc_issued, node) {
		struct mtk_hsdma_vdesc *hvd;

		hvd = to_hsdma_vdesc(vd);

		/* Map VD into PC and all VCs shares a single PC */
		err = mtk_hsdma_issue_pending_vdesc(hsdma, hsdma->pc, hvd);

		/*
		 * Move VD from desc_issued to desc_hw_processing when entire
		 * VD is fit into available PDs. Otherwise, the uncompleted
		 * VDs would stay in list desc_issued and then restart the
		 * processing as soon as possible once underlying ring space
		 * got freed.
		 */
		if (err == -ENOSPC || hvd->len > 0)
			break;

		/*
		 * The extra list desc_hw_processing is used because
		 * hardware can't provide sufficient information allowing us
		 * to know what VDs are still working on the underlying ring.
		 * Through the additional list, it can help us to implement
		 * terminate_all, residue calculation and such thing needed
		 * to know detail descriptor status on the hardware.
		 */
		list_move_tail(&vd->node, &hvc->desc_hw_processing);
	}
}

static void mtk_hsdma_free_rooms_in_ring(struct mtk_hsdma_device *hsdma)
{
	struct mtk_hsdma_vchan *hvc;
	struct mtk_hsdma_pdesc *rxd;
	struct mtk_hsdma_vdesc *hvd;
	struct mtk_hsdma_pchan *pc;
	struct mtk_hsdma_cb *cb;
	int i = MTK_DMA_SIZE;
	__le32 desc2;
	u32 status;
	u16 next;

	/* Read IRQ status */
	status = mtk_dma_read(hsdma, MTK_HSDMA_INT_STATUS);
	if (unlikely(!(status & MTK_HSDMA_INT_RXDONE)))
		goto rx_done;

	pc = hsdma->pc;

	/*
	 * Using a fail-safe loop with iterations of up to MTK_DMA_SIZE to
	 * reclaim these finished descriptors: The most number of PDs the ISR
	 * can handle at one time shouldn't be more than MTK_DMA_SIZE so we
	 * take it as limited count instead of just using a dangerous infinite
	 * poll.
	 */
	while (i--) {
		next = MTK_HSDMA_NEXT_DESP_IDX(pc->ring.cur_rptr,
					       MTK_DMA_SIZE);
		rxd = &pc->ring.rxd[next];

		/*
		 * If MTK_HSDMA_DESC_DDONE is no specified, that means data
		 * moving for the PD is still under going.
		 */
		desc2 = READ_ONCE(rxd->desc2);
		if (!(desc2 & hsdma->soc->ddone))
			break;

		cb = &pc->ring.cb[next];
		if (unlikely(!cb->vd)) {
			dev_err(hsdma2dev(hsdma), "cb->vd cannot be null\n");
			break;
		}

		/* Update residue of VD the associated PD belonged to */
		hvd = to_hsdma_vdesc(cb->vd);
		hvd->residue -= MTK_HSDMA_DESC_PLEN_GET(rxd->desc2);

		/* Complete VD until the relevant last PD is finished */
		if (IS_MTK_HSDMA_VDESC_FINISHED(cb->flag)) {
			hvc = to_hsdma_vchan(cb->vd->tx.chan);

			spin_lock(&hvc->vc.lock);

			/* Remove VD from list desc_hw_processing */
			list_del(&cb->vd->node);

			/* Add VD into list desc_completed */
			vchan_cookie_complete(cb->vd);

			if (hvc->issue_synchronize &&
			    list_empty(&hvc->desc_hw_processing)) {
				complete(&hvc->issue_completion);
				hvc->issue_synchronize = false;
			}
			spin_unlock(&hvc->vc.lock);

			cb->flag = 0;
		}

		cb->vd = 0;

		/*
		 * Recycle the RXD with the helper WRITE_ONCE that can ensure
		 * data written into RAM would really happens.
		 */
		WRITE_ONCE(rxd->desc1, 0);
		WRITE_ONCE(rxd->desc2, 0);
		pc->ring.cur_rptr = next;

		/* Release rooms */
		atomic_inc(&pc->nr_free);
	}

	/* Ensure all changes indeed done before we're going on */
	wmb();

	/* Update CPU pointer for those completed PDs */
	mtk_dma_write(hsdma, MTK_HSDMA_RX_CPU, pc->ring.cur_rptr);

	/*
	 * Acking the pending IRQ allows hardware no longer to keep the used
	 * IRQ line in certain trigger state when software has completed all
	 * the finished physical descriptors.
	 */
	if (atomic_read(&pc->nr_free) >= MTK_DMA_SIZE - 1)
		mtk_dma_write(hsdma, MTK_HSDMA_INT_STATUS, status);

	/* ASAP handles pending VDs in all VCs after freeing some rooms */
	for (i = 0; i < hsdma->dma_requests; i++) {
		hvc = &hsdma->vc[i];
		spin_lock(&hvc->vc.lock);
		mtk_hsdma_issue_vchan_pending(hsdma, hvc);
		spin_unlock(&hvc->vc.lock);
	}

rx_done:
	/* All completed PDs are cleaned up, so enable interrupt again */
	mtk_dma_set(hsdma, MTK_HSDMA_INT_ENABLE, MTK_HSDMA_INT_RXDONE);
}

static irqreturn_t mtk_hsdma_irq(int irq, void *devid)
{
	struct mtk_hsdma_device *hsdma = devid;

	/*
	 * Disable interrupt until all completed PDs are cleaned up in
	 * mtk_hsdma_free_rooms call.
	 */
	mtk_dma_clr(hsdma, MTK_HSDMA_INT_ENABLE, MTK_HSDMA_INT_RXDONE);

	mtk_hsdma_free_rooms_in_ring(hsdma);

	return IRQ_HANDLED;
}

static struct virt_dma_desc *mtk_hsdma_find_active_desc(struct dma_chan *c,
							dma_cookie_t cookie)
{
	struct mtk_hsdma_vchan *hvc = to_hsdma_vchan(c);
	struct virt_dma_desc *vd;

	list_for_each_entry(vd, &hvc->desc_hw_processing, node)
		if (vd->tx.cookie == cookie)
			return vd;

	list_for_each_entry(vd, &hvc->vc.desc_issued, node)
		if (vd->tx.cookie == cookie)
			return vd;

	return NULL;
}

static enum dma_status mtk_hsdma_tx_status(struct dma_chan *c,
					   dma_cookie_t cookie,
					   struct dma_tx_state *txstate)
{
	struct mtk_hsdma_vchan *hvc = to_hsdma_vchan(c);
	struct mtk_hsdma_vdesc *hvd;
	struct virt_dma_desc *vd;
	enum dma_status ret;
	unsigned long flags;
	size_t bytes = 0;

	ret = dma_cookie_status(c, cookie, txstate);
	if (ret == DMA_COMPLETE || !txstate)
		return ret;

	spin_lock_irqsave(&hvc->vc.lock, flags);
	vd = mtk_hsdma_find_active_desc(c, cookie);
	spin_unlock_irqrestore(&hvc->vc.lock, flags);

	if (vd) {
		hvd = to_hsdma_vdesc(vd);
		bytes = hvd->residue;
	}

	dma_set_residue(txstate, bytes);

	return ret;
}

static void mtk_hsdma_issue_pending(struct dma_chan *c)
{
	struct mtk_hsdma_device *hsdma = to_hsdma_dev(c);
	struct mtk_hsdma_vchan *hvc = to_hsdma_vchan(c);
	unsigned long flags;

	spin_lock_irqsave(&hvc->vc.lock, flags);

	if (vchan_issue_pending(&hvc->vc))
		mtk_hsdma_issue_vchan_pending(hsdma, hvc);

	spin_unlock_irqrestore(&hvc->vc.lock, flags);
}

static struct dma_async_tx_descriptor *
mtk_hsdma_prep_dma_memcpy(struct dma_chan *c, dma_addr_t dest,
			  dma_addr_t src, size_t len, unsigned long flags)
{
	struct mtk_hsdma_vdesc *hvd;

	hvd = kzalloc(sizeof(*hvd), GFP_NOWAIT);
	if (!hvd)
		return NULL;

	hvd->len = len;
	hvd->residue = len;
	hvd->src = src;
	hvd->dest = dest;

	return vchan_tx_prep(to_virt_chan(c), &hvd->vd, flags);
}

static int mtk_hsdma_free_inactive_desc(struct dma_chan *c)
{
	struct virt_dma_chan *vc = to_virt_chan(c);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&vc->lock, flags);
	list_splice_tail_init(&vc->desc_allocated, &head);
	list_splice_tail_init(&vc->desc_submitted, &head);
	list_splice_tail_init(&vc->desc_issued, &head);
	spin_unlock_irqrestore(&vc->lock, flags);

	/* At the point, we don't expect users put descriptor into VC again */
	vchan_dma_desc_free_list(vc, &head);

	return 0;
}

static void mtk_hsdma_free_active_desc(struct dma_chan *c)
{
	struct mtk_hsdma_vchan *hvc = to_hsdma_vchan(c);
	bool sync_needed = false;

	/*
	 * Once issue_synchronize is being set, which means once the hardware
	 * consumes all descriptors for the channel in the ring, the
	 * synchronization must be be notified immediately it is completed.
	 */
	spin_lock(&hvc->vc.lock);
	if (!list_empty(&hvc->desc_hw_processing)) {
		hvc->issue_synchronize = true;
		sync_needed = true;
	}
	spin_unlock(&hvc->vc.lock);

	if (sync_needed)
		wait_for_completion(&hvc->issue_completion);
	/*
	 * At the point, we expect that all remaining descriptors in the ring
	 * for the channel should be all processing done.
	 */
	WARN_ONCE(!list_empty(&hvc->desc_hw_processing),
		  "Desc pending still in list desc_hw_processing\n");

	/* Free all descriptors in list desc_completed */
	vchan_synchronize(&hvc->vc);

	WARN_ONCE(!list_empty(&hvc->vc.desc_completed),
		  "Desc pending still in list desc_completed\n");
}

static int mtk_hsdma_terminate_all(struct dma_chan *c)
{
	/*
	 * Free pending descriptors not processed yet by hardware that have
	 * previously been submitted to the channel.
	 */
	mtk_hsdma_free_inactive_desc(c);

	/*
	 * However, the DMA engine doesn't provide any way to stop these
	 * descriptors being processed currently by hardware. The only way is
	 * to just waiting until these descriptors are all processed completely
	 * through mtk_hsdma_free_active_desc call.
	 */
	mtk_hsdma_free_active_desc(c);

	return 0;
}

static int mtk_hsdma_alloc_chan_resources(struct dma_chan *c)
{
	struct mtk_hsdma_device *hsdma = to_hsdma_dev(c);
	int err;

	/*
	 * Since HSDMA has only one PC, the resource for PC is being allocated
	 * when the first VC is being created and the other VCs would run on
	 * the same PC.
	 */
	if (!refcount_read(&hsdma->pc_refcnt)) {
		err = mtk_hsdma_alloc_pchan(hsdma, hsdma->pc);
		if (err)
			return err;
		/*
		 * refcount_inc would complain increment on 0; use-after-free.
		 * Thus, we need to explicitly set it as 1 initially.
		 */
		refcount_set(&hsdma->pc_refcnt, 1);
	} else {
		refcount_inc(&hsdma->pc_refcnt);
	}

	return 0;
}

static void mtk_hsdma_free_chan_resources(struct dma_chan *c)
{
	struct mtk_hsdma_device *hsdma = to_hsdma_dev(c);

	/* Free all descriptors in all lists on the VC */
	mtk_hsdma_terminate_all(c);

	/* The resource for PC is not freed until all the VCs are destroyed */
	if (!refcount_dec_and_test(&hsdma->pc_refcnt))
		return;

	mtk_hsdma_free_pchan(hsdma, hsdma->pc);
}

static int mtk_hsdma_hw_init(struct mtk_hsdma_device *hsdma)
{
	int err;

	pm_runtime_enable(hsdma2dev(hsdma));
	pm_runtime_get_sync(hsdma2dev(hsdma));

	err = clk_prepare_enable(hsdma->clk);
	if (err)
		return err;

	mtk_dma_write(hsdma, MTK_HSDMA_INT_ENABLE, 0);
	mtk_dma_write(hsdma, MTK_HSDMA_GLO, MTK_HSDMA_GLO_DEFAULT);

	return 0;
}

static int mtk_hsdma_hw_deinit(struct mtk_hsdma_device *hsdma)
{
	mtk_dma_write(hsdma, MTK_HSDMA_GLO, 0);

	clk_disable_unprepare(hsdma->clk);

	pm_runtime_put_sync(hsdma2dev(hsdma));
	pm_runtime_disable(hsdma2dev(hsdma));

	return 0;
}

static const struct mtk_hsdma_soc mt7623_soc = {
	.ddone = BIT(31),
	.ls0 = BIT(30),
};

static const struct mtk_hsdma_soc mt7622_soc = {
	.ddone = BIT(15),
	.ls0 = BIT(14),
};

static const struct of_device_id mtk_hsdma_match[] = {
	{ .compatible = "mediatek,mt7623-hsdma", .data = &mt7623_soc},
	{ .compatible = "mediatek,mt7622-hsdma", .data = &mt7622_soc},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_hsdma_match);

static int mtk_hsdma_probe(struct platform_device *pdev)
{
	struct mtk_hsdma_device *hsdma;
	struct mtk_hsdma_vchan *vc;
	struct dma_device *dd;
	struct resource *res;
	int i, err;

	hsdma = devm_kzalloc(&pdev->dev, sizeof(*hsdma), GFP_KERNEL);
	if (!hsdma)
		return -ENOMEM;

	dd = &hsdma->ddev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hsdma->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hsdma->base))
		return PTR_ERR(hsdma->base);

	hsdma->soc = of_device_get_match_data(&pdev->dev);
	if (!hsdma->soc) {
		dev_err(&pdev->dev, "No device match found\n");
		return -ENODEV;
	}

	hsdma->clk = devm_clk_get(&pdev->dev, "hsdma");
	if (IS_ERR(hsdma->clk)) {
		dev_err(&pdev->dev, "No clock for %s\n",
			dev_name(&pdev->dev));
		return PTR_ERR(hsdma->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "No irq resource for %s\n",
			dev_name(&pdev->dev));
		return -EINVAL;
	}
	hsdma->irq = res->start;

	refcount_set(&hsdma->pc_refcnt, 0);
	spin_lock_init(&hsdma->lock);

	dma_cap_set(DMA_MEMCPY, dd->cap_mask);

	dd->copy_align = MTK_HSDMA_ALIGN_SIZE;
	dd->device_alloc_chan_resources = mtk_hsdma_alloc_chan_resources;
	dd->device_free_chan_resources = mtk_hsdma_free_chan_resources;
	dd->device_tx_status = mtk_hsdma_tx_status;
	dd->device_issue_pending = mtk_hsdma_issue_pending;
	dd->device_prep_dma_memcpy = mtk_hsdma_prep_dma_memcpy;
	dd->device_terminate_all = mtk_hsdma_terminate_all;
	dd->src_addr_widths = MTK_HSDMA_DMA_BUSWIDTHS;
	dd->dst_addr_widths = MTK_HSDMA_DMA_BUSWIDTHS;
	dd->directions = BIT(DMA_MEM_TO_MEM);
	dd->residue_granularity = DMA_RESIDUE_GRANULARITY_SEGMENT;
	dd->dev = &pdev->dev;
	INIT_LIST_HEAD(&dd->channels);

	hsdma->dma_requests = MTK_HSDMA_NR_VCHANS;
	if (pdev->dev.of_node && of_property_read_u32(pdev->dev.of_node,
						      "dma-requests",
						      &hsdma->dma_requests)) {
		dev_info(&pdev->dev,
			 "Using %u as missing dma-requests property\n",
			 MTK_HSDMA_NR_VCHANS);
	}

	hsdma->pc = devm_kcalloc(&pdev->dev, MTK_HSDMA_NR_MAX_PCHANS,
				 sizeof(*hsdma->pc), GFP_KERNEL);
	if (!hsdma->pc)
		return -ENOMEM;

	hsdma->vc = devm_kcalloc(&pdev->dev, hsdma->dma_requests,
				 sizeof(*hsdma->vc), GFP_KERNEL);
	if (!hsdma->vc)
		return -ENOMEM;

	for (i = 0; i < hsdma->dma_requests; i++) {
		vc = &hsdma->vc[i];
		vc->vc.desc_free = mtk_hsdma_vdesc_free;
		vchan_init(&vc->vc, dd);
		init_completion(&vc->issue_completion);
		INIT_LIST_HEAD(&vc->desc_hw_processing);
	}

	err = dma_async_device_register(dd);
	if (err)
		return err;

	err = of_dma_controller_register(pdev->dev.of_node,
					 of_dma_xlate_by_chan_id, hsdma);
	if (err) {
		dev_err(&pdev->dev,
			"MediaTek HSDMA OF registration failed %d\n", err);
		goto err_unregister;
	}

	mtk_hsdma_hw_init(hsdma);

	err = devm_request_irq(&pdev->dev, hsdma->irq,
			       mtk_hsdma_irq, 0,
			       dev_name(&pdev->dev), hsdma);
	if (err) {
		dev_err(&pdev->dev,
			"request_irq failed with err %d\n", err);
		goto err_unregister;
	}

	platform_set_drvdata(pdev, hsdma);

	dev_info(&pdev->dev, "MediaTek HSDMA driver registered\n");

	return 0;

err_unregister:
	dma_async_device_unregister(dd);

	return err;
}

static int mtk_hsdma_remove(struct platform_device *pdev)
{
	struct mtk_hsdma_device *hsdma = platform_get_drvdata(pdev);
	struct mtk_hsdma_vchan *vc;
	int i;

	/* Kill VC task */
	for (i = 0; i < hsdma->dma_requests; i++) {
		vc = &hsdma->vc[i];

		list_del(&vc->vc.chan.device_node);
		tasklet_kill(&vc->vc.task);
	}

	/* Disable DMA interrupt */
	mtk_dma_write(hsdma, MTK_HSDMA_INT_ENABLE, 0);

	/* Waits for any pending IRQ handlers to complete */
	synchronize_irq(hsdma->irq);

	/* Disable hardware */
	mtk_hsdma_hw_deinit(hsdma);

	dma_async_device_unregister(&hsdma->ddev);
	of_dma_controller_free(pdev->dev.of_node);

	return 0;
}

static struct platform_driver mtk_hsdma_driver = {
	.probe		= mtk_hsdma_probe,
	.remove		= mtk_hsdma_remove,
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= mtk_hsdma_match,
	},
};
module_platform_driver(mtk_hsdma_driver);

MODULE_DESCRIPTION("MediaTek High-Speed DMA Controller Driver");
MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_LICENSE("GPL v2");
