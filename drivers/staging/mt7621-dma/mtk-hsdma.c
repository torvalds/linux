// SPDX-License-Identifier: GPL-2.0+
/*
 *  Copyright (C) 2015, Michael Lee <igvtee@gmail.com>
 *  MTK HSDMA support
 */

#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/irq.h>
#include <linux/of_dma.h>
#include <linux/reset.h>
#include <linux/of_device.h>

#include "virt-dma.h"

#define HSDMA_BASE_OFFSET		0x800

#define HSDMA_REG_TX_BASE		0x00
#define HSDMA_REG_TX_CNT		0x04
#define HSDMA_REG_TX_CTX		0x08
#define HSDMA_REG_TX_DTX		0x0c
#define HSDMA_REG_RX_BASE		0x100
#define HSDMA_REG_RX_CNT		0x104
#define HSDMA_REG_RX_CRX		0x108
#define HSDMA_REG_RX_DRX		0x10c
#define HSDMA_REG_INFO			0x200
#define HSDMA_REG_GLO_CFG		0x204
#define HSDMA_REG_RST_CFG		0x208
#define HSDMA_REG_DELAY_INT		0x20c
#define HSDMA_REG_FREEQ_THRES		0x210
#define HSDMA_REG_INT_STATUS		0x220
#define HSDMA_REG_INT_MASK		0x228
#define HSDMA_REG_SCH_Q01		0x280
#define HSDMA_REG_SCH_Q23		0x284

#define HSDMA_DESCS_MAX			0xfff
#define HSDMA_DESCS_NUM			8
#define HSDMA_DESCS_MASK		(HSDMA_DESCS_NUM - 1)
#define HSDMA_NEXT_DESC(x)		(((x) + 1) & HSDMA_DESCS_MASK)

/* HSDMA_REG_INFO */
#define HSDMA_INFO_INDEX_MASK		0xf
#define HSDMA_INFO_INDEX_SHIFT		24
#define HSDMA_INFO_BASE_MASK		0xff
#define HSDMA_INFO_BASE_SHIFT		16
#define HSDMA_INFO_RX_MASK		0xff
#define HSDMA_INFO_RX_SHIFT		8
#define HSDMA_INFO_TX_MASK		0xff
#define HSDMA_INFO_TX_SHIFT		0

/* HSDMA_REG_GLO_CFG */
#define HSDMA_GLO_TX_2B_OFFSET		BIT(31)
#define HSDMA_GLO_CLK_GATE		BIT(30)
#define HSDMA_GLO_BYTE_SWAP		BIT(29)
#define HSDMA_GLO_MULTI_DMA		BIT(10)
#define HSDMA_GLO_TWO_BUF		BIT(9)
#define HSDMA_GLO_32B_DESC		BIT(8)
#define HSDMA_GLO_BIG_ENDIAN		BIT(7)
#define HSDMA_GLO_TX_DONE		BIT(6)
#define HSDMA_GLO_BT_MASK		0x3
#define HSDMA_GLO_BT_SHIFT		4
#define HSDMA_GLO_RX_BUSY		BIT(3)
#define HSDMA_GLO_RX_DMA		BIT(2)
#define HSDMA_GLO_TX_BUSY		BIT(1)
#define HSDMA_GLO_TX_DMA		BIT(0)

#define HSDMA_BT_SIZE_16BYTES		(0 << HSDMA_GLO_BT_SHIFT)
#define HSDMA_BT_SIZE_32BYTES		(1 << HSDMA_GLO_BT_SHIFT)
#define HSDMA_BT_SIZE_64BYTES		(2 << HSDMA_GLO_BT_SHIFT)
#define HSDMA_BT_SIZE_128BYTES		(3 << HSDMA_GLO_BT_SHIFT)

#define HSDMA_GLO_DEFAULT		(HSDMA_GLO_MULTI_DMA | \
		HSDMA_GLO_RX_DMA | HSDMA_GLO_TX_DMA | HSDMA_BT_SIZE_32BYTES)

/* HSDMA_REG_RST_CFG */
#define HSDMA_RST_RX_SHIFT		16
#define HSDMA_RST_TX_SHIFT		0

/* HSDMA_REG_DELAY_INT */
#define HSDMA_DELAY_INT_EN		BIT(15)
#define HSDMA_DELAY_PEND_OFFSET		8
#define HSDMA_DELAY_TIME_OFFSET		0
#define HSDMA_DELAY_TX_OFFSET		16
#define HSDMA_DELAY_RX_OFFSET		0

#define HSDMA_DELAY_INIT(x)		(HSDMA_DELAY_INT_EN | \
		((x) << HSDMA_DELAY_PEND_OFFSET))
#define HSDMA_DELAY(x)			((HSDMA_DELAY_INIT(x) << \
		HSDMA_DELAY_TX_OFFSET) | HSDMA_DELAY_INIT(x))

/* HSDMA_REG_INT_STATUS */
#define HSDMA_INT_DELAY_RX_COH		BIT(31)
#define HSDMA_INT_DELAY_RX_INT		BIT(30)
#define HSDMA_INT_DELAY_TX_COH		BIT(29)
#define HSDMA_INT_DELAY_TX_INT		BIT(28)
#define HSDMA_INT_RX_MASK		0x3
#define HSDMA_INT_RX_SHIFT		16
#define HSDMA_INT_RX_Q0			BIT(16)
#define HSDMA_INT_TX_MASK		0xf
#define HSDMA_INT_TX_SHIFT		0
#define HSDMA_INT_TX_Q0			BIT(0)

/* tx/rx dma desc flags */
#define HSDMA_PLEN_MASK			0x3fff
#define HSDMA_DESC_DONE			BIT(31)
#define HSDMA_DESC_LS0			BIT(30)
#define HSDMA_DESC_PLEN0(_x)		(((_x) & HSDMA_PLEN_MASK) << 16)
#define HSDMA_DESC_TAG			BIT(15)
#define HSDMA_DESC_LS1			BIT(14)
#define HSDMA_DESC_PLEN1(_x)		((_x) & HSDMA_PLEN_MASK)

/* align 4 bytes */
#define HSDMA_ALIGN_SIZE		3
/* align size 128bytes */
#define HSDMA_MAX_PLEN			0x3f80

struct hsdma_desc {
	u32 addr0;
	u32 flags;
	u32 addr1;
	u32 unused;
};

struct mtk_hsdma_sg {
	dma_addr_t src_addr;
	dma_addr_t dst_addr;
	u32 len;
};

struct mtk_hsdma_desc {
	struct virt_dma_desc vdesc;
	unsigned int num_sgs;
	struct mtk_hsdma_sg sg[1];
};

struct mtk_hsdma_chan {
	struct virt_dma_chan vchan;
	unsigned int id;
	dma_addr_t desc_addr;
	int tx_idx;
	int rx_idx;
	struct hsdma_desc *tx_ring;
	struct hsdma_desc *rx_ring;
	struct mtk_hsdma_desc *desc;
	unsigned int next_sg;
};

struct mtk_hsdam_engine {
	struct dma_device ddev;
	struct device_dma_parameters dma_parms;
	void __iomem *base;
	struct tasklet_struct task;
	volatile unsigned long chan_issued;

	struct mtk_hsdma_chan chan[1];
};

static inline struct mtk_hsdam_engine *mtk_hsdma_chan_get_dev(
		struct mtk_hsdma_chan *chan)
{
	return container_of(chan->vchan.chan.device, struct mtk_hsdam_engine,
			ddev);
}

static inline struct mtk_hsdma_chan *to_mtk_hsdma_chan(struct dma_chan *c)
{
	return container_of(c, struct mtk_hsdma_chan, vchan.chan);
}

static inline struct mtk_hsdma_desc *to_mtk_hsdma_desc(
		struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct mtk_hsdma_desc, vdesc);
}

static inline u32 mtk_hsdma_read(struct mtk_hsdam_engine *hsdma, u32 reg)
{
	return readl(hsdma->base + reg);
}

static inline void mtk_hsdma_write(struct mtk_hsdam_engine *hsdma,
				   unsigned int reg, u32 val)
{
	writel(val, hsdma->base + reg);
}

static void mtk_hsdma_reset_chan(struct mtk_hsdam_engine *hsdma,
				 struct mtk_hsdma_chan *chan)
{
	chan->tx_idx = 0;
	chan->rx_idx = HSDMA_DESCS_NUM - 1;

	mtk_hsdma_write(hsdma, HSDMA_REG_TX_CTX, chan->tx_idx);
	mtk_hsdma_write(hsdma, HSDMA_REG_RX_CRX, chan->rx_idx);

	mtk_hsdma_write(hsdma, HSDMA_REG_RST_CFG,
			0x1 << (chan->id + HSDMA_RST_TX_SHIFT));
	mtk_hsdma_write(hsdma, HSDMA_REG_RST_CFG,
			0x1 << (chan->id + HSDMA_RST_RX_SHIFT));
}

static void hsdma_dump_reg(struct mtk_hsdam_engine *hsdma)
{
	dev_dbg(hsdma->ddev.dev,
		"tbase %08x, tcnt %08x, tctx %08x, tdtx: %08x, rbase %08x, rcnt %08x, rctx %08x, rdtx %08x\n",
		mtk_hsdma_read(hsdma, HSDMA_REG_TX_BASE),
		mtk_hsdma_read(hsdma, HSDMA_REG_TX_CNT),
		mtk_hsdma_read(hsdma, HSDMA_REG_TX_CTX),
		mtk_hsdma_read(hsdma, HSDMA_REG_TX_DTX),
		mtk_hsdma_read(hsdma, HSDMA_REG_RX_BASE),
		mtk_hsdma_read(hsdma, HSDMA_REG_RX_CNT),
		mtk_hsdma_read(hsdma, HSDMA_REG_RX_CRX),
		mtk_hsdma_read(hsdma, HSDMA_REG_RX_DRX));

	dev_dbg(hsdma->ddev.dev,
		"info %08x, glo %08x, delay %08x, intr_stat %08x, intr_mask %08x\n",
		mtk_hsdma_read(hsdma, HSDMA_REG_INFO),
		mtk_hsdma_read(hsdma, HSDMA_REG_GLO_CFG),
		mtk_hsdma_read(hsdma, HSDMA_REG_DELAY_INT),
		mtk_hsdma_read(hsdma, HSDMA_REG_INT_STATUS),
		mtk_hsdma_read(hsdma, HSDMA_REG_INT_MASK));
}

static void hsdma_dump_desc(struct mtk_hsdam_engine *hsdma,
			    struct mtk_hsdma_chan *chan)
{
	struct hsdma_desc *tx_desc;
	struct hsdma_desc *rx_desc;
	int i;

	dev_dbg(hsdma->ddev.dev, "tx idx: %d, rx idx: %d\n",
		chan->tx_idx, chan->rx_idx);

	for (i = 0; i < HSDMA_DESCS_NUM; i++) {
		tx_desc = &chan->tx_ring[i];
		rx_desc = &chan->rx_ring[i];

		dev_dbg(hsdma->ddev.dev,
			"%d tx addr0: %08x, flags %08x, tx addr1: %08x, rx addr0 %08x, flags %08x\n",
			i, tx_desc->addr0, tx_desc->flags,
			tx_desc->addr1, rx_desc->addr0, rx_desc->flags);
	}
}

static void mtk_hsdma_reset(struct mtk_hsdam_engine *hsdma,
			    struct mtk_hsdma_chan *chan)
{
	int i;

	/* disable dma */
	mtk_hsdma_write(hsdma, HSDMA_REG_GLO_CFG, 0);

	/* disable intr */
	mtk_hsdma_write(hsdma, HSDMA_REG_INT_MASK, 0);

	/* init desc value */
	for (i = 0; i < HSDMA_DESCS_NUM; i++) {
		chan->tx_ring[i].addr0 = 0;
		chan->tx_ring[i].flags = HSDMA_DESC_LS0 | HSDMA_DESC_DONE;
	}
	for (i = 0; i < HSDMA_DESCS_NUM; i++) {
		chan->rx_ring[i].addr0 = 0;
		chan->rx_ring[i].flags = 0;
	}

	/* reset */
	mtk_hsdma_reset_chan(hsdma, chan);

	/* enable intr */
	mtk_hsdma_write(hsdma, HSDMA_REG_INT_MASK, HSDMA_INT_RX_Q0);

	/* enable dma */
	mtk_hsdma_write(hsdma, HSDMA_REG_GLO_CFG, HSDMA_GLO_DEFAULT);
}

static int mtk_hsdma_terminate_all(struct dma_chan *c)
{
	struct mtk_hsdma_chan *chan = to_mtk_hsdma_chan(c);
	struct mtk_hsdam_engine *hsdma = mtk_hsdma_chan_get_dev(chan);
	unsigned long timeout;
	LIST_HEAD(head);

	spin_lock_bh(&chan->vchan.lock);
	chan->desc = NULL;
	clear_bit(chan->id, &hsdma->chan_issued);
	vchan_get_all_descriptors(&chan->vchan, &head);
	spin_unlock_bh(&chan->vchan.lock);

	vchan_dma_desc_free_list(&chan->vchan, &head);

	/* wait dma transfer complete */
	timeout = jiffies + msecs_to_jiffies(2000);
	while (mtk_hsdma_read(hsdma, HSDMA_REG_GLO_CFG) &
			(HSDMA_GLO_RX_BUSY | HSDMA_GLO_TX_BUSY)) {
		if (time_after_eq(jiffies, timeout)) {
			hsdma_dump_desc(hsdma, chan);
			mtk_hsdma_reset(hsdma, chan);
			dev_err(hsdma->ddev.dev, "timeout, reset it\n");
			break;
		}
		cpu_relax();
	}

	return 0;
}

static int mtk_hsdma_start_transfer(struct mtk_hsdam_engine *hsdma,
				    struct mtk_hsdma_chan *chan)
{
	dma_addr_t src, dst;
	size_t len, tlen;
	struct hsdma_desc *tx_desc, *rx_desc;
	struct mtk_hsdma_sg *sg;
	unsigned int i;
	int rx_idx;

	sg = &chan->desc->sg[0];
	len = sg->len;
	chan->desc->num_sgs = DIV_ROUND_UP(len, HSDMA_MAX_PLEN);

	/* tx desc */
	src = sg->src_addr;
	for (i = 0; i < chan->desc->num_sgs; i++) {
		tx_desc = &chan->tx_ring[chan->tx_idx];

		if (len > HSDMA_MAX_PLEN)
			tlen = HSDMA_MAX_PLEN;
		else
			tlen = len;

		if (i & 0x1) {
			tx_desc->addr1 = src;
			tx_desc->flags |= HSDMA_DESC_PLEN1(tlen);
		} else {
			tx_desc->addr0 = src;
			tx_desc->flags = HSDMA_DESC_PLEN0(tlen);

			/* update index */
			chan->tx_idx = HSDMA_NEXT_DESC(chan->tx_idx);
		}

		src += tlen;
		len -= tlen;
	}
	if (i & 0x1)
		tx_desc->flags |= HSDMA_DESC_LS0;
	else
		tx_desc->flags |= HSDMA_DESC_LS1;

	/* rx desc */
	rx_idx = HSDMA_NEXT_DESC(chan->rx_idx);
	len = sg->len;
	dst = sg->dst_addr;
	for (i = 0; i < chan->desc->num_sgs; i++) {
		rx_desc = &chan->rx_ring[rx_idx];
		if (len > HSDMA_MAX_PLEN)
			tlen = HSDMA_MAX_PLEN;
		else
			tlen = len;

		rx_desc->addr0 = dst;
		rx_desc->flags = HSDMA_DESC_PLEN0(tlen);

		dst += tlen;
		len -= tlen;

		/* update index */
		rx_idx = HSDMA_NEXT_DESC(rx_idx);
	}

	/* make sure desc and index all up to date */
	wmb();
	mtk_hsdma_write(hsdma, HSDMA_REG_TX_CTX, chan->tx_idx);

	return 0;
}

static int gdma_next_desc(struct mtk_hsdma_chan *chan)
{
	struct virt_dma_desc *vdesc;

	vdesc = vchan_next_desc(&chan->vchan);
	if (!vdesc) {
		chan->desc = NULL;
		return 0;
	}
	chan->desc = to_mtk_hsdma_desc(vdesc);
	chan->next_sg = 0;

	return 1;
}

static void mtk_hsdma_chan_done(struct mtk_hsdam_engine *hsdma,
				struct mtk_hsdma_chan *chan)
{
	struct mtk_hsdma_desc *desc;
	int chan_issued;

	chan_issued = 0;
	spin_lock_bh(&chan->vchan.lock);
	desc = chan->desc;
	if (likely(desc)) {
		if (chan->next_sg == desc->num_sgs) {
			list_del(&desc->vdesc.node);
			vchan_cookie_complete(&desc->vdesc);
			chan_issued = gdma_next_desc(chan);
		}
	} else {
		dev_dbg(hsdma->ddev.dev, "no desc to complete\n");
	}

	if (chan_issued)
		set_bit(chan->id, &hsdma->chan_issued);
	spin_unlock_bh(&chan->vchan.lock);
}

static irqreturn_t mtk_hsdma_irq(int irq, void *devid)
{
	struct mtk_hsdam_engine *hsdma = devid;
	u32 status;

	status = mtk_hsdma_read(hsdma, HSDMA_REG_INT_STATUS);
	if (unlikely(!status))
		return IRQ_NONE;

	if (likely(status & HSDMA_INT_RX_Q0))
		tasklet_schedule(&hsdma->task);
	else
		dev_dbg(hsdma->ddev.dev, "unhandle irq status %08x\n", status);
	/* clean intr bits */
	mtk_hsdma_write(hsdma, HSDMA_REG_INT_STATUS, status);

	return IRQ_HANDLED;
}

static void mtk_hsdma_issue_pending(struct dma_chan *c)
{
	struct mtk_hsdma_chan *chan = to_mtk_hsdma_chan(c);
	struct mtk_hsdam_engine *hsdma = mtk_hsdma_chan_get_dev(chan);

	spin_lock_bh(&chan->vchan.lock);
	if (vchan_issue_pending(&chan->vchan) && !chan->desc) {
		if (gdma_next_desc(chan)) {
			set_bit(chan->id, &hsdma->chan_issued);
			tasklet_schedule(&hsdma->task);
		} else {
			dev_dbg(hsdma->ddev.dev, "no desc to issue\n");
		}
	}
	spin_unlock_bh(&chan->vchan.lock);
}

static struct dma_async_tx_descriptor *mtk_hsdma_prep_dma_memcpy(
		struct dma_chan *c, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct mtk_hsdma_chan *chan = to_mtk_hsdma_chan(c);
	struct mtk_hsdma_desc *desc;

	if (len <= 0)
		return NULL;

	desc = kzalloc(sizeof(*desc), GFP_ATOMIC);
	if (!desc) {
		dev_err(c->device->dev, "alloc memcpy decs error\n");
		return NULL;
	}

	desc->sg[0].src_addr = src;
	desc->sg[0].dst_addr = dest;
	desc->sg[0].len = len;

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);
}

static enum dma_status mtk_hsdma_tx_status(struct dma_chan *c,
					   dma_cookie_t cookie,
					   struct dma_tx_state *state)
{
	return dma_cookie_status(c, cookie, state);
}

static void mtk_hsdma_free_chan_resources(struct dma_chan *c)
{
	vchan_free_chan_resources(to_virt_chan(c));
}

static void mtk_hsdma_desc_free(struct virt_dma_desc *vdesc)
{
	kfree(container_of(vdesc, struct mtk_hsdma_desc, vdesc));
}

static void mtk_hsdma_tx(struct mtk_hsdam_engine *hsdma)
{
	struct mtk_hsdma_chan *chan;

	if (test_and_clear_bit(0, &hsdma->chan_issued)) {
		chan = &hsdma->chan[0];
		if (chan->desc)
			mtk_hsdma_start_transfer(hsdma, chan);
		else
			dev_dbg(hsdma->ddev.dev, "chan 0 no desc to issue\n");
	}
}

static void mtk_hsdma_rx(struct mtk_hsdam_engine *hsdma)
{
	struct mtk_hsdma_chan *chan;
	int next_idx, drx_idx, cnt;

	chan = &hsdma->chan[0];
	next_idx = HSDMA_NEXT_DESC(chan->rx_idx);
	drx_idx = mtk_hsdma_read(hsdma, HSDMA_REG_RX_DRX);

	cnt = (drx_idx - next_idx) & HSDMA_DESCS_MASK;
	if (!cnt)
		return;

	chan->next_sg += cnt;
	chan->rx_idx = (chan->rx_idx + cnt) & HSDMA_DESCS_MASK;

	/* update rx crx */
	wmb();
	mtk_hsdma_write(hsdma, HSDMA_REG_RX_CRX, chan->rx_idx);

	mtk_hsdma_chan_done(hsdma, chan);
}

static void mtk_hsdma_tasklet(struct tasklet_struct *t)
{
	struct mtk_hsdam_engine *hsdma = from_tasklet(hsdma, t, task);

	mtk_hsdma_rx(hsdma);
	mtk_hsdma_tx(hsdma);
}

static int mtk_hsdam_alloc_desc(struct mtk_hsdam_engine *hsdma,
				struct mtk_hsdma_chan *chan)
{
	int i;

	chan->tx_ring = dma_alloc_coherent(hsdma->ddev.dev,
					   2 * HSDMA_DESCS_NUM *
					   sizeof(*chan->tx_ring),
			&chan->desc_addr, GFP_ATOMIC | __GFP_ZERO);
	if (!chan->tx_ring)
		goto no_mem;

	chan->rx_ring = &chan->tx_ring[HSDMA_DESCS_NUM];

	/* init tx ring value */
	for (i = 0; i < HSDMA_DESCS_NUM; i++)
		chan->tx_ring[i].flags = HSDMA_DESC_LS0 | HSDMA_DESC_DONE;

	return 0;
no_mem:
	return -ENOMEM;
}

static void mtk_hsdam_free_desc(struct mtk_hsdam_engine *hsdma,
				struct mtk_hsdma_chan *chan)
{
	if (chan->tx_ring) {
		dma_free_coherent(hsdma->ddev.dev,
				  2 * HSDMA_DESCS_NUM * sizeof(*chan->tx_ring),
				  chan->tx_ring, chan->desc_addr);
		chan->tx_ring = NULL;
		chan->rx_ring = NULL;
	}
}

static int mtk_hsdma_init(struct mtk_hsdam_engine *hsdma)
{
	struct mtk_hsdma_chan *chan;
	int ret;
	u32 reg;

	/* init desc */
	chan = &hsdma->chan[0];
	ret = mtk_hsdam_alloc_desc(hsdma, chan);
	if (ret)
		return ret;

	/* tx */
	mtk_hsdma_write(hsdma, HSDMA_REG_TX_BASE, chan->desc_addr);
	mtk_hsdma_write(hsdma, HSDMA_REG_TX_CNT, HSDMA_DESCS_NUM);
	/* rx */
	mtk_hsdma_write(hsdma, HSDMA_REG_RX_BASE, chan->desc_addr +
			(sizeof(struct hsdma_desc) * HSDMA_DESCS_NUM));
	mtk_hsdma_write(hsdma, HSDMA_REG_RX_CNT, HSDMA_DESCS_NUM);
	/* reset */
	mtk_hsdma_reset_chan(hsdma, chan);

	/* enable rx intr */
	mtk_hsdma_write(hsdma, HSDMA_REG_INT_MASK, HSDMA_INT_RX_Q0);

	/* enable dma */
	mtk_hsdma_write(hsdma, HSDMA_REG_GLO_CFG, HSDMA_GLO_DEFAULT);

	/* hardware info */
	reg = mtk_hsdma_read(hsdma, HSDMA_REG_INFO);
	dev_info(hsdma->ddev.dev, "rx: %d, tx: %d\n",
		 (reg >> HSDMA_INFO_RX_SHIFT) & HSDMA_INFO_RX_MASK,
		 (reg >> HSDMA_INFO_TX_SHIFT) & HSDMA_INFO_TX_MASK);

	hsdma_dump_reg(hsdma);

	return ret;
}

static void mtk_hsdma_uninit(struct mtk_hsdam_engine *hsdma)
{
	struct mtk_hsdma_chan *chan;

	/* disable dma */
	mtk_hsdma_write(hsdma, HSDMA_REG_GLO_CFG, 0);

	/* disable intr */
	mtk_hsdma_write(hsdma, HSDMA_REG_INT_MASK, 0);

	/* free desc */
	chan = &hsdma->chan[0];
	mtk_hsdam_free_desc(hsdma, chan);

	/* tx */
	mtk_hsdma_write(hsdma, HSDMA_REG_TX_BASE, 0);
	mtk_hsdma_write(hsdma, HSDMA_REG_TX_CNT, 0);
	/* rx */
	mtk_hsdma_write(hsdma, HSDMA_REG_RX_BASE, 0);
	mtk_hsdma_write(hsdma, HSDMA_REG_RX_CNT, 0);
	/* reset */
	mtk_hsdma_reset_chan(hsdma, chan);
}

static const struct of_device_id mtk_hsdma_of_match[] = {
	{ .compatible = "mediatek,mt7621-hsdma" },
	{ },
};

static int mtk_hsdma_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct mtk_hsdma_chan *chan;
	struct mtk_hsdam_engine *hsdma;
	struct dma_device *dd;
	int ret;
	int irq;
	void __iomem *base;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	match = of_match_device(mtk_hsdma_of_match, &pdev->dev);
	if (!match)
		return -EINVAL;

	hsdma = devm_kzalloc(&pdev->dev, sizeof(*hsdma), GFP_KERNEL);
	if (!hsdma)
		return -EINVAL;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);
	hsdma->base = base + HSDMA_BASE_OFFSET;
	tasklet_setup(&hsdma->task, mtk_hsdma_tasklet);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EINVAL;
	ret = devm_request_irq(&pdev->dev, irq, mtk_hsdma_irq,
			       0, dev_name(&pdev->dev), hsdma);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq\n");
		return ret;
	}

	device_reset(&pdev->dev);

	dd = &hsdma->ddev;
	dma_cap_set(DMA_MEMCPY, dd->cap_mask);
	dd->copy_align = HSDMA_ALIGN_SIZE;
	dd->device_free_chan_resources = mtk_hsdma_free_chan_resources;
	dd->device_prep_dma_memcpy = mtk_hsdma_prep_dma_memcpy;
	dd->device_terminate_all = mtk_hsdma_terminate_all;
	dd->device_tx_status = mtk_hsdma_tx_status;
	dd->device_issue_pending = mtk_hsdma_issue_pending;
	dd->dev = &pdev->dev;
	dd->dev->dma_parms = &hsdma->dma_parms;
	dma_set_max_seg_size(dd->dev, HSDMA_MAX_PLEN);
	INIT_LIST_HEAD(&dd->channels);

	chan = &hsdma->chan[0];
	chan->id = 0;
	chan->vchan.desc_free = mtk_hsdma_desc_free;
	vchan_init(&chan->vchan, dd);

	/* init hardware */
	ret = mtk_hsdma_init(hsdma);
	if (ret) {
		dev_err(&pdev->dev, "failed to alloc ring descs\n");
		return ret;
	}

	ret = dma_async_device_register(dd);
	if (ret) {
		dev_err(&pdev->dev, "failed to register dma device\n");
		return ret;
	}

	ret = of_dma_controller_register(pdev->dev.of_node,
					 of_dma_xlate_by_chan_id, hsdma);
	if (ret) {
		dev_err(&pdev->dev, "failed to register of dma controller\n");
		goto err_unregister;
	}

	platform_set_drvdata(pdev, hsdma);

	return 0;

err_unregister:
	dma_async_device_unregister(dd);
	return ret;
}

static int mtk_hsdma_remove(struct platform_device *pdev)
{
	struct mtk_hsdam_engine *hsdma = platform_get_drvdata(pdev);

	mtk_hsdma_uninit(hsdma);

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&hsdma->ddev);

	return 0;
}

static struct platform_driver mtk_hsdma_driver = {
	.probe = mtk_hsdma_probe,
	.remove = mtk_hsdma_remove,
	.driver = {
		.name = "hsdma-mt7621",
		.of_match_table = mtk_hsdma_of_match,
	},
};
module_platform_driver(mtk_hsdma_driver);

MODULE_AUTHOR("Michael Lee <igvtee@gmail.com>");
MODULE_DESCRIPTION("MTK HSDMA driver");
MODULE_LICENSE("GPL v2");
