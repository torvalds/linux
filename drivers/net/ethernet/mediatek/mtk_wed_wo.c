// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022 MediaTek Inc.
 *
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 *	   Sujuan Chen <sujuan.chen@mediatek.com>
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/mfd/syscon.h>
#include <linux/of_irq.h>
#include <linux/bitfield.h>

#include "mtk_wed.h"
#include "mtk_wed_regs.h"
#include "mtk_wed_wo.h"

static u32
mtk_wed_mmio_r32(struct mtk_wed_wo *wo, u32 reg)
{
	u32 val;

	if (regmap_read(wo->mmio.regs, reg, &val))
		val = ~0;

	return val;
}

static void
mtk_wed_mmio_w32(struct mtk_wed_wo *wo, u32 reg, u32 val)
{
	regmap_write(wo->mmio.regs, reg, val);
}

static u32
mtk_wed_wo_get_isr(struct mtk_wed_wo *wo)
{
	u32 val = mtk_wed_mmio_r32(wo, MTK_WED_WO_CCIF_RCHNUM);

	return val & MTK_WED_WO_CCIF_RCHNUM_MASK;
}

static void
mtk_wed_wo_set_isr(struct mtk_wed_wo *wo, u32 mask)
{
	mtk_wed_mmio_w32(wo, MTK_WED_WO_CCIF_IRQ0_MASK, mask);
}

static void
mtk_wed_wo_set_ack(struct mtk_wed_wo *wo, u32 mask)
{
	mtk_wed_mmio_w32(wo, MTK_WED_WO_CCIF_ACK, mask);
}

static void
mtk_wed_wo_set_isr_mask(struct mtk_wed_wo *wo, u32 mask, u32 val, bool set)
{
	unsigned long flags;

	spin_lock_irqsave(&wo->mmio.lock, flags);
	wo->mmio.irq_mask &= ~mask;
	wo->mmio.irq_mask |= val;
	if (set)
		mtk_wed_wo_set_isr(wo, wo->mmio.irq_mask);
	spin_unlock_irqrestore(&wo->mmio.lock, flags);
}

static void
mtk_wed_wo_irq_enable(struct mtk_wed_wo *wo, u32 mask)
{
	mtk_wed_wo_set_isr_mask(wo, 0, mask, false);
	tasklet_schedule(&wo->mmio.irq_tasklet);
}

static void
mtk_wed_wo_irq_disable(struct mtk_wed_wo *wo, u32 mask)
{
	mtk_wed_wo_set_isr_mask(wo, mask, 0, true);
}

static void
mtk_wed_wo_kickout(struct mtk_wed_wo *wo)
{
	mtk_wed_mmio_w32(wo, MTK_WED_WO_CCIF_BUSY, 1 << MTK_WED_WO_TXCH_NUM);
	mtk_wed_mmio_w32(wo, MTK_WED_WO_CCIF_TCHNUM, MTK_WED_WO_TXCH_NUM);
}

static void
mtk_wed_wo_queue_kick(struct mtk_wed_wo *wo, struct mtk_wed_wo_queue *q,
		      u32 val)
{
	wmb();
	mtk_wed_mmio_w32(wo, q->regs.cpu_idx, val);
}

static void *
mtk_wed_wo_dequeue(struct mtk_wed_wo *wo, struct mtk_wed_wo_queue *q, u32 *len,
		   bool flush)
{
	int buf_len = SKB_WITH_OVERHEAD(q->buf_size);
	int index = (q->tail + 1) % q->n_desc;
	struct mtk_wed_wo_queue_entry *entry;
	struct mtk_wed_wo_queue_desc *desc;
	void *buf;

	if (!q->queued)
		return NULL;

	if (flush)
		q->desc[index].ctrl |= cpu_to_le32(MTK_WED_WO_CTL_DMA_DONE);
	else if (!(q->desc[index].ctrl & cpu_to_le32(MTK_WED_WO_CTL_DMA_DONE)))
		return NULL;

	q->tail = index;
	q->queued--;

	desc = &q->desc[index];
	entry = &q->entry[index];
	buf = entry->buf;
	if (len)
		*len = FIELD_GET(MTK_WED_WO_CTL_SD_LEN0,
				 le32_to_cpu(READ_ONCE(desc->ctrl)));
	if (buf)
		dma_unmap_single(wo->hw->dev, entry->addr, buf_len,
				 DMA_FROM_DEVICE);
	entry->buf = NULL;

	return buf;
}

static int
mtk_wed_wo_queue_refill(struct mtk_wed_wo *wo, struct mtk_wed_wo_queue *q,
			bool rx)
{
	enum dma_data_direction dir = rx ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	int n_buf = 0;

	spin_lock_bh(&q->lock);
	while (q->queued < q->n_desc) {
		struct mtk_wed_wo_queue_entry *entry;
		dma_addr_t addr;
		void *buf;

		buf = page_frag_alloc(&q->cache, q->buf_size, GFP_ATOMIC);
		if (!buf)
			break;

		addr = dma_map_single(wo->hw->dev, buf, q->buf_size, dir);
		if (unlikely(dma_mapping_error(wo->hw->dev, addr))) {
			skb_free_frag(buf);
			break;
		}

		q->head = (q->head + 1) % q->n_desc;
		entry = &q->entry[q->head];
		entry->addr = addr;
		entry->len = q->buf_size;
		q->entry[q->head].buf = buf;

		if (rx) {
			struct mtk_wed_wo_queue_desc *desc = &q->desc[q->head];
			u32 ctrl = MTK_WED_WO_CTL_LAST_SEC0 |
				   FIELD_PREP(MTK_WED_WO_CTL_SD_LEN0,
					      entry->len);

			WRITE_ONCE(desc->buf0, cpu_to_le32(addr));
			WRITE_ONCE(desc->ctrl, cpu_to_le32(ctrl));
		}
		q->queued++;
		n_buf++;
	}
	spin_unlock_bh(&q->lock);

	return n_buf;
}

static void
mtk_wed_wo_rx_complete(struct mtk_wed_wo *wo)
{
	mtk_wed_wo_set_ack(wo, MTK_WED_WO_RXCH_INT_MASK);
	mtk_wed_wo_irq_enable(wo, MTK_WED_WO_RXCH_INT_MASK);
}

static void
mtk_wed_wo_rx_run_queue(struct mtk_wed_wo *wo, struct mtk_wed_wo_queue *q)
{
	for (;;) {
		struct mtk_wed_mcu_hdr *hdr;
		struct sk_buff *skb;
		void *data;
		u32 len;

		data = mtk_wed_wo_dequeue(wo, q, &len, false);
		if (!data)
			break;

		skb = build_skb(data, q->buf_size);
		if (!skb) {
			skb_free_frag(data);
			continue;
		}

		__skb_put(skb, len);
		if (mtk_wed_mcu_check_msg(wo, skb)) {
			dev_kfree_skb(skb);
			continue;
		}

		hdr = (struct mtk_wed_mcu_hdr *)skb->data;
		if (hdr->flag & cpu_to_le16(MTK_WED_WARP_CMD_FLAG_RSP))
			mtk_wed_mcu_rx_event(wo, skb);
		else
			mtk_wed_mcu_rx_unsolicited_event(wo, skb);
	}

	if (mtk_wed_wo_queue_refill(wo, q, true)) {
		u32 index = (q->head - 1) % q->n_desc;

		mtk_wed_wo_queue_kick(wo, q, index);
	}
}

static irqreturn_t
mtk_wed_wo_irq_handler(int irq, void *data)
{
	struct mtk_wed_wo *wo = data;

	mtk_wed_wo_set_isr(wo, 0);
	tasklet_schedule(&wo->mmio.irq_tasklet);

	return IRQ_HANDLED;
}

static void mtk_wed_wo_irq_tasklet(struct tasklet_struct *t)
{
	struct mtk_wed_wo *wo = from_tasklet(wo, t, mmio.irq_tasklet);
	u32 intr, mask;

	/* disable interrupts */
	mtk_wed_wo_set_isr(wo, 0);

	intr = mtk_wed_wo_get_isr(wo);
	intr &= wo->mmio.irq_mask;
	mask = intr & (MTK_WED_WO_RXCH_INT_MASK | MTK_WED_WO_EXCEPTION_INT_MASK);
	mtk_wed_wo_irq_disable(wo, mask);

	if (intr & MTK_WED_WO_RXCH_INT_MASK) {
		mtk_wed_wo_rx_run_queue(wo, &wo->q_rx);
		mtk_wed_wo_rx_complete(wo);
	}
}

/* mtk wed wo hw queues */

static int
mtk_wed_wo_queue_alloc(struct mtk_wed_wo *wo, struct mtk_wed_wo_queue *q,
		       int n_desc, int buf_size, int index,
		       struct mtk_wed_wo_queue_regs *regs)
{
	spin_lock_init(&q->lock);
	q->regs = *regs;
	q->n_desc = n_desc;
	q->buf_size = buf_size;

	q->desc = dmam_alloc_coherent(wo->hw->dev, n_desc * sizeof(*q->desc),
				      &q->desc_dma, GFP_KERNEL);
	if (!q->desc)
		return -ENOMEM;

	q->entry = devm_kzalloc(wo->hw->dev, n_desc * sizeof(*q->entry),
				GFP_KERNEL);
	if (!q->entry)
		return -ENOMEM;

	return 0;
}

static void
mtk_wed_wo_queue_free(struct mtk_wed_wo *wo, struct mtk_wed_wo_queue *q)
{
	mtk_wed_mmio_w32(wo, q->regs.cpu_idx, 0);
	dma_free_coherent(wo->hw->dev, q->n_desc * sizeof(*q->desc), q->desc,
			  q->desc_dma);
}

static void
mtk_wed_wo_queue_tx_clean(struct mtk_wed_wo *wo, struct mtk_wed_wo_queue *q)
{
	struct page *page;
	int i;

	spin_lock_bh(&q->lock);
	for (i = 0; i < q->n_desc; i++) {
		struct mtk_wed_wo_queue_entry *entry = &q->entry[i];

		dma_unmap_single(wo->hw->dev, entry->addr, entry->len,
				 DMA_TO_DEVICE);
		skb_free_frag(entry->buf);
		entry->buf = NULL;
	}
	spin_unlock_bh(&q->lock);

	if (!q->cache.va)
		return;

	page = virt_to_page(q->cache.va);
	__page_frag_cache_drain(page, q->cache.pagecnt_bias);
	memset(&q->cache, 0, sizeof(q->cache));
}

static void
mtk_wed_wo_queue_rx_clean(struct mtk_wed_wo *wo, struct mtk_wed_wo_queue *q)
{
	struct page *page;

	spin_lock_bh(&q->lock);
	for (;;) {
		void *buf = mtk_wed_wo_dequeue(wo, q, NULL, true);

		if (!buf)
			break;

		skb_free_frag(buf);
	}
	spin_unlock_bh(&q->lock);

	if (!q->cache.va)
		return;

	page = virt_to_page(q->cache.va);
	__page_frag_cache_drain(page, q->cache.pagecnt_bias);
	memset(&q->cache, 0, sizeof(q->cache));
}

static void
mtk_wed_wo_queue_reset(struct mtk_wed_wo *wo, struct mtk_wed_wo_queue *q)
{
	mtk_wed_mmio_w32(wo, q->regs.cpu_idx, 0);
	mtk_wed_mmio_w32(wo, q->regs.desc_base, q->desc_dma);
	mtk_wed_mmio_w32(wo, q->regs.ring_size, q->n_desc);
}

int mtk_wed_wo_queue_tx_skb(struct mtk_wed_wo *wo, struct mtk_wed_wo_queue *q,
			    struct sk_buff *skb)
{
	struct mtk_wed_wo_queue_entry *entry;
	struct mtk_wed_wo_queue_desc *desc;
	int ret = 0, index;
	u32 ctrl;

	spin_lock_bh(&q->lock);

	q->tail = mtk_wed_mmio_r32(wo, q->regs.dma_idx);
	index = (q->head + 1) % q->n_desc;
	if (q->tail == index) {
		ret = -ENOMEM;
		goto out;
	}

	entry = &q->entry[index];
	if (skb->len > entry->len) {
		ret = -ENOMEM;
		goto out;
	}

	desc = &q->desc[index];
	q->head = index;

	dma_sync_single_for_cpu(wo->hw->dev, entry->addr, skb->len,
				DMA_TO_DEVICE);
	memcpy(entry->buf, skb->data, skb->len);
	dma_sync_single_for_device(wo->hw->dev, entry->addr, skb->len,
				   DMA_TO_DEVICE);

	ctrl = FIELD_PREP(MTK_WED_WO_CTL_SD_LEN0, skb->len) |
	       MTK_WED_WO_CTL_LAST_SEC0 | MTK_WED_WO_CTL_DMA_DONE;
	WRITE_ONCE(desc->buf0, cpu_to_le32(entry->addr));
	WRITE_ONCE(desc->ctrl, cpu_to_le32(ctrl));

	mtk_wed_wo_queue_kick(wo, q, q->head);
	mtk_wed_wo_kickout(wo);
out:
	spin_unlock_bh(&q->lock);

	dev_kfree_skb(skb);

	return ret;
}

static int
mtk_wed_wo_exception_init(struct mtk_wed_wo *wo)
{
	return 0;
}

static int
mtk_wed_wo_hardware_init(struct mtk_wed_wo *wo)
{
	struct mtk_wed_wo_queue_regs regs;
	struct device_node *np;
	int ret;

	np = of_parse_phandle(wo->hw->node, "mediatek,wo-ccif", 0);
	if (!np)
		return -ENODEV;

	wo->mmio.regs = syscon_regmap_lookup_by_phandle(np, NULL);
	if (IS_ERR(wo->mmio.regs)) {
		ret = PTR_ERR(wo->mmio.regs);
		goto error_put;
	}

	wo->mmio.irq = irq_of_parse_and_map(np, 0);
	wo->mmio.irq_mask = MTK_WED_WO_ALL_INT_MASK;
	spin_lock_init(&wo->mmio.lock);
	tasklet_setup(&wo->mmio.irq_tasklet, mtk_wed_wo_irq_tasklet);

	ret = devm_request_irq(wo->hw->dev, wo->mmio.irq,
			       mtk_wed_wo_irq_handler, IRQF_TRIGGER_HIGH,
			       KBUILD_MODNAME, wo);
	if (ret)
		goto error;

	regs.desc_base = MTK_WED_WO_CCIF_DUMMY1;
	regs.ring_size = MTK_WED_WO_CCIF_DUMMY2;
	regs.dma_idx = MTK_WED_WO_CCIF_SHADOW4;
	regs.cpu_idx = MTK_WED_WO_CCIF_DUMMY3;

	ret = mtk_wed_wo_queue_alloc(wo, &wo->q_tx, MTK_WED_WO_RING_SIZE,
				     MTK_WED_WO_CMD_LEN, MTK_WED_WO_TXCH_NUM,
				     &regs);
	if (ret)
		goto error;

	mtk_wed_wo_queue_refill(wo, &wo->q_tx, false);
	mtk_wed_wo_queue_reset(wo, &wo->q_tx);

	regs.desc_base = MTK_WED_WO_CCIF_DUMMY5;
	regs.ring_size = MTK_WED_WO_CCIF_DUMMY6;
	regs.dma_idx = MTK_WED_WO_CCIF_SHADOW8;
	regs.cpu_idx = MTK_WED_WO_CCIF_DUMMY7;

	ret = mtk_wed_wo_queue_alloc(wo, &wo->q_rx, MTK_WED_WO_RING_SIZE,
				     MTK_WED_WO_CMD_LEN, MTK_WED_WO_RXCH_NUM,
				     &regs);
	if (ret)
		goto error;

	mtk_wed_wo_queue_refill(wo, &wo->q_rx, true);
	mtk_wed_wo_queue_reset(wo, &wo->q_rx);

	/* rx queue irqmask */
	mtk_wed_wo_set_isr(wo, wo->mmio.irq_mask);

	return 0;

error:
	devm_free_irq(wo->hw->dev, wo->mmio.irq, wo);
error_put:
	of_node_put(np);
	return ret;
}

static void
mtk_wed_wo_hw_deinit(struct mtk_wed_wo *wo)
{
	/* disable interrupts */
	mtk_wed_wo_set_isr(wo, 0);

	tasklet_disable(&wo->mmio.irq_tasklet);

	disable_irq(wo->mmio.irq);
	devm_free_irq(wo->hw->dev, wo->mmio.irq, wo);

	mtk_wed_wo_queue_tx_clean(wo, &wo->q_tx);
	mtk_wed_wo_queue_rx_clean(wo, &wo->q_rx);
	mtk_wed_wo_queue_free(wo, &wo->q_tx);
	mtk_wed_wo_queue_free(wo, &wo->q_rx);
}

int mtk_wed_wo_init(struct mtk_wed_hw *hw)
{
	struct mtk_wed_wo *wo;
	int ret;

	wo = devm_kzalloc(hw->dev, sizeof(*wo), GFP_KERNEL);
	if (!wo)
		return -ENOMEM;

	hw->wed_wo = wo;
	wo->hw = hw;

	ret = mtk_wed_wo_hardware_init(wo);
	if (ret)
		return ret;

	ret = mtk_wed_mcu_init(wo);
	if (ret)
		return ret;

	return mtk_wed_wo_exception_init(wo);
}

void mtk_wed_wo_deinit(struct mtk_wed_hw *hw)
{
	struct mtk_wed_wo *wo = hw->wed_wo;

	mtk_wed_wo_hw_deinit(wo);
}
