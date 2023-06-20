// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2021 Felix Fietkau <nbd@nbd.name> */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/bitfield.h>
#include <linux/dma-mapping.h>
#include <linux/skbuff.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/mfd/syscon.h>
#include <linux/debugfs.h>
#include <linux/soc/mediatek/mtk_wed.h>
#include "mtk_eth_soc.h"
#include "mtk_wed_regs.h"
#include "mtk_wed.h"
#include "mtk_ppe.h"

#define MTK_PCIE_BASE(n)		(0x1a143000 + (n) * 0x2000)

#define MTK_WED_PKT_SIZE		1900
#define MTK_WED_BUF_SIZE		2048
#define MTK_WED_BUF_PER_PAGE		(PAGE_SIZE / 2048)

#define MTK_WED_TX_RING_SIZE		2048
#define MTK_WED_WDMA_RING_SIZE		1024

static struct mtk_wed_hw *hw_list[2];
static DEFINE_MUTEX(hw_lock);

static void
wed_m32(struct mtk_wed_device *dev, u32 reg, u32 mask, u32 val)
{
	regmap_update_bits(dev->hw->regs, reg, mask | val, val);
}

static void
wed_set(struct mtk_wed_device *dev, u32 reg, u32 mask)
{
	return wed_m32(dev, reg, 0, mask);
}

static void
wed_clr(struct mtk_wed_device *dev, u32 reg, u32 mask)
{
	return wed_m32(dev, reg, mask, 0);
}

static void
wdma_m32(struct mtk_wed_device *dev, u32 reg, u32 mask, u32 val)
{
	wdma_w32(dev, reg, (wdma_r32(dev, reg) & ~mask) | val);
}

static void
wdma_set(struct mtk_wed_device *dev, u32 reg, u32 mask)
{
	wdma_m32(dev, reg, 0, mask);
}

static u32
mtk_wed_read_reset(struct mtk_wed_device *dev)
{
	return wed_r32(dev, MTK_WED_RESET);
}

static void
mtk_wed_reset(struct mtk_wed_device *dev, u32 mask)
{
	u32 status;

	wed_w32(dev, MTK_WED_RESET, mask);
	if (readx_poll_timeout(mtk_wed_read_reset, dev, status,
			       !(status & mask), 0, 1000))
		WARN_ON_ONCE(1);
}

static struct mtk_wed_hw *
mtk_wed_assign(struct mtk_wed_device *dev)
{
	struct mtk_wed_hw *hw;

	hw = hw_list[pci_domain_nr(dev->wlan.pci_dev->bus)];
	if (!hw || hw->wed_dev)
		return NULL;

	hw->wed_dev = dev;
	return hw;
}

static int
mtk_wed_buffer_alloc(struct mtk_wed_device *dev)
{
	struct mtk_wdma_desc *desc;
	dma_addr_t desc_phys;
	void **page_list;
	int token = dev->wlan.token_start;
	int ring_size;
	int n_pages;
	int i, page_idx;

	ring_size = dev->wlan.nbuf & ~(MTK_WED_BUF_PER_PAGE - 1);
	n_pages = ring_size / MTK_WED_BUF_PER_PAGE;

	page_list = kcalloc(n_pages, sizeof(*page_list), GFP_KERNEL);
	if (!page_list)
		return -ENOMEM;

	dev->buf_ring.size = ring_size;
	dev->buf_ring.pages = page_list;

	desc = dma_alloc_coherent(dev->hw->dev, ring_size * sizeof(*desc),
				  &desc_phys, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	dev->buf_ring.desc = desc;
	dev->buf_ring.desc_phys = desc_phys;

	for (i = 0, page_idx = 0; i < ring_size; i += MTK_WED_BUF_PER_PAGE) {
		dma_addr_t page_phys, buf_phys;
		struct page *page;
		void *buf;
		int s;

		page = __dev_alloc_pages(GFP_KERNEL, 0);
		if (!page)
			return -ENOMEM;

		page_phys = dma_map_page(dev->hw->dev, page, 0, PAGE_SIZE,
					 DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev->hw->dev, page_phys)) {
			__free_page(page);
			return -ENOMEM;
		}

		page_list[page_idx++] = page;
		dma_sync_single_for_cpu(dev->hw->dev, page_phys, PAGE_SIZE,
					DMA_BIDIRECTIONAL);

		buf = page_to_virt(page);
		buf_phys = page_phys;

		for (s = 0; s < MTK_WED_BUF_PER_PAGE; s++) {
			u32 txd_size;
			u32 ctrl;

			txd_size = dev->wlan.init_buf(buf, buf_phys, token++);

			desc->buf0 = cpu_to_le32(buf_phys);
			desc->buf1 = cpu_to_le32(buf_phys + txd_size);
			ctrl = FIELD_PREP(MTK_WDMA_DESC_CTRL_LEN0, txd_size) |
			       FIELD_PREP(MTK_WDMA_DESC_CTRL_LEN1,
					  MTK_WED_BUF_SIZE - txd_size) |
			       MTK_WDMA_DESC_CTRL_LAST_SEG1;
			desc->ctrl = cpu_to_le32(ctrl);
			desc->info = 0;
			desc++;

			buf += MTK_WED_BUF_SIZE;
			buf_phys += MTK_WED_BUF_SIZE;
		}

		dma_sync_single_for_device(dev->hw->dev, page_phys, PAGE_SIZE,
					   DMA_BIDIRECTIONAL);
	}

	return 0;
}

static void
mtk_wed_free_buffer(struct mtk_wed_device *dev)
{
	struct mtk_wdma_desc *desc = dev->buf_ring.desc;
	void **page_list = dev->buf_ring.pages;
	int page_idx;
	int i;

	if (!page_list)
		return;

	if (!desc)
		goto free_pagelist;

	for (i = 0, page_idx = 0; i < dev->buf_ring.size; i += MTK_WED_BUF_PER_PAGE) {
		void *page = page_list[page_idx++];
		dma_addr_t buf_addr;

		if (!page)
			break;

		buf_addr = le32_to_cpu(desc[i].buf0);
		dma_unmap_page(dev->hw->dev, buf_addr, PAGE_SIZE,
			       DMA_BIDIRECTIONAL);
		__free_page(page);
	}

	dma_free_coherent(dev->hw->dev, dev->buf_ring.size * sizeof(*desc),
			  desc, dev->buf_ring.desc_phys);

free_pagelist:
	kfree(page_list);
}

static void
mtk_wed_free_ring(struct mtk_wed_device *dev, struct mtk_wed_ring *ring)
{
	if (!ring->desc)
		return;

	dma_free_coherent(dev->hw->dev, ring->size * sizeof(*ring->desc),
			  ring->desc, ring->desc_phys);
}

static void
mtk_wed_free_tx_rings(struct mtk_wed_device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dev->tx_ring); i++)
		mtk_wed_free_ring(dev, &dev->tx_ring[i]);
	for (i = 0; i < ARRAY_SIZE(dev->tx_wdma); i++)
		mtk_wed_free_ring(dev, &dev->tx_wdma[i]);
}

static void
mtk_wed_set_ext_int(struct mtk_wed_device *dev, bool en)
{
	u32 mask = MTK_WED_EXT_INT_STATUS_ERROR_MASK;

	if (!dev->hw->num_flows)
		mask &= ~MTK_WED_EXT_INT_STATUS_TKID_WO_PYLD;

	wed_w32(dev, MTK_WED_EXT_INT_MASK, en ? mask : 0);
	wed_r32(dev, MTK_WED_EXT_INT_MASK);
}

static void
mtk_wed_stop(struct mtk_wed_device *dev)
{
	regmap_write(dev->hw->mirror, dev->hw->index * 4, 0);
	mtk_wed_set_ext_int(dev, false);

	wed_clr(dev, MTK_WED_CTRL,
		MTK_WED_CTRL_WDMA_INT_AGENT_EN |
		MTK_WED_CTRL_WPDMA_INT_AGENT_EN |
		MTK_WED_CTRL_WED_TX_BM_EN |
		MTK_WED_CTRL_WED_TX_FREE_AGENT_EN);
	wed_w32(dev, MTK_WED_WPDMA_INT_TRIGGER, 0);
	wed_w32(dev, MTK_WED_WDMA_INT_TRIGGER, 0);
	wdma_w32(dev, MTK_WDMA_INT_MASK, 0);
	wdma_w32(dev, MTK_WDMA_INT_GRP2, 0);
	wed_w32(dev, MTK_WED_WPDMA_INT_MASK, 0);

	wed_clr(dev, MTK_WED_GLO_CFG,
		MTK_WED_GLO_CFG_TX_DMA_EN |
		MTK_WED_GLO_CFG_RX_DMA_EN);
	wed_clr(dev, MTK_WED_WPDMA_GLO_CFG,
		MTK_WED_WPDMA_GLO_CFG_TX_DRV_EN |
		MTK_WED_WPDMA_GLO_CFG_RX_DRV_EN);
	wed_clr(dev, MTK_WED_WDMA_GLO_CFG,
		MTK_WED_WDMA_GLO_CFG_RX_DRV_EN);
}

static void
mtk_wed_detach(struct mtk_wed_device *dev)
{
	struct device_node *wlan_node = dev->wlan.pci_dev->dev.of_node;
	struct mtk_wed_hw *hw = dev->hw;

	mutex_lock(&hw_lock);

	mtk_wed_stop(dev);

	wdma_w32(dev, MTK_WDMA_RESET_IDX, MTK_WDMA_RESET_IDX_RX);
	wdma_w32(dev, MTK_WDMA_RESET_IDX, 0);

	mtk_wed_reset(dev, MTK_WED_RESET_WED);

	mtk_wed_free_buffer(dev);
	mtk_wed_free_tx_rings(dev);

	if (of_dma_is_coherent(wlan_node))
		regmap_update_bits(hw->hifsys, HIFSYS_DMA_AG_MAP,
				   BIT(hw->index), BIT(hw->index));

	if (!hw_list[!hw->index]->wed_dev &&
	    hw->eth->dma_dev != hw->eth->dev)
		mtk_eth_set_dma_device(hw->eth, hw->eth->dev);

	memset(dev, 0, sizeof(*dev));
	module_put(THIS_MODULE);

	hw->wed_dev = NULL;
	mutex_unlock(&hw_lock);
}

static void
mtk_wed_hw_init_early(struct mtk_wed_device *dev)
{
	u32 mask, set;
	u32 offset;

	mtk_wed_stop(dev);
	mtk_wed_reset(dev, MTK_WED_RESET_WED);

	mask = MTK_WED_WDMA_GLO_CFG_BT_SIZE |
	       MTK_WED_WDMA_GLO_CFG_DYNAMIC_DMAD_RECYCLE |
	       MTK_WED_WDMA_GLO_CFG_RX_DIS_FSM_AUTO_IDLE;
	set = FIELD_PREP(MTK_WED_WDMA_GLO_CFG_BT_SIZE, 2) |
	      MTK_WED_WDMA_GLO_CFG_DYNAMIC_SKIP_DMAD_PREP |
	      MTK_WED_WDMA_GLO_CFG_IDLE_DMAD_SUPPLY;
	wed_m32(dev, MTK_WED_WDMA_GLO_CFG, mask, set);

	wdma_set(dev, MTK_WDMA_GLO_CFG, MTK_WDMA_GLO_CFG_RX_INFO_PRERES);

	offset = dev->hw->index ? 0x04000400 : 0;
	wed_w32(dev, MTK_WED_WDMA_OFFSET0, 0x2a042a20 + offset);
	wed_w32(dev, MTK_WED_WDMA_OFFSET1, 0x29002800 + offset);

	wed_w32(dev, MTK_WED_PCIE_CFG_BASE, MTK_PCIE_BASE(dev->hw->index));
	wed_w32(dev, MTK_WED_WPDMA_CFG_BASE, dev->wlan.wpdma_phys);
}

static void
mtk_wed_hw_init(struct mtk_wed_device *dev)
{
	if (dev->init_done)
		return;

	dev->init_done = true;
	mtk_wed_set_ext_int(dev, false);
	wed_w32(dev, MTK_WED_TX_BM_CTRL,
		MTK_WED_TX_BM_CTRL_PAUSE |
		FIELD_PREP(MTK_WED_TX_BM_CTRL_VLD_GRP_NUM,
			   dev->buf_ring.size / 128) |
		FIELD_PREP(MTK_WED_TX_BM_CTRL_RSV_GRP_NUM,
			   MTK_WED_TX_RING_SIZE / 256));

	wed_w32(dev, MTK_WED_TX_BM_BASE, dev->buf_ring.desc_phys);

	wed_w32(dev, MTK_WED_TX_BM_TKID,
		FIELD_PREP(MTK_WED_TX_BM_TKID_START,
			   dev->wlan.token_start) |
		FIELD_PREP(MTK_WED_TX_BM_TKID_END,
			   dev->wlan.token_start + dev->wlan.nbuf - 1));

	wed_w32(dev, MTK_WED_TX_BM_BUF_LEN, MTK_WED_PKT_SIZE);

	wed_w32(dev, MTK_WED_TX_BM_DYN_THR,
		FIELD_PREP(MTK_WED_TX_BM_DYN_THR_LO, 1) |
		MTK_WED_TX_BM_DYN_THR_HI);

	mtk_wed_reset(dev, MTK_WED_RESET_TX_BM);

	wed_set(dev, MTK_WED_CTRL,
		MTK_WED_CTRL_WED_TX_BM_EN |
		MTK_WED_CTRL_WED_TX_FREE_AGENT_EN);

	wed_clr(dev, MTK_WED_TX_BM_CTRL, MTK_WED_TX_BM_CTRL_PAUSE);
}

static void
mtk_wed_ring_reset(struct mtk_wdma_desc *desc, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		desc[i].buf0 = 0;
		desc[i].ctrl = cpu_to_le32(MTK_WDMA_DESC_CTRL_DMA_DONE);
		desc[i].buf1 = 0;
		desc[i].info = 0;
	}
}

static u32
mtk_wed_check_busy(struct mtk_wed_device *dev)
{
	if (wed_r32(dev, MTK_WED_GLO_CFG) & MTK_WED_GLO_CFG_TX_DMA_BUSY)
		return true;

	if (wed_r32(dev, MTK_WED_WPDMA_GLO_CFG) &
	    MTK_WED_WPDMA_GLO_CFG_TX_DRV_BUSY)
		return true;

	if (wed_r32(dev, MTK_WED_CTRL) & MTK_WED_CTRL_WDMA_INT_AGENT_BUSY)
		return true;

	if (wed_r32(dev, MTK_WED_WDMA_GLO_CFG) &
	    MTK_WED_WDMA_GLO_CFG_RX_DRV_BUSY)
		return true;

	if (wdma_r32(dev, MTK_WDMA_GLO_CFG) &
	    MTK_WED_WDMA_GLO_CFG_RX_DRV_BUSY)
		return true;

	if (wed_r32(dev, MTK_WED_CTRL) &
	    (MTK_WED_CTRL_WED_TX_BM_BUSY | MTK_WED_CTRL_WED_TX_FREE_AGENT_BUSY))
		return true;

	return false;
}

static int
mtk_wed_poll_busy(struct mtk_wed_device *dev)
{
	int sleep = 15000;
	int timeout = 100 * sleep;
	u32 val;

	return read_poll_timeout(mtk_wed_check_busy, val, !val, sleep,
				 timeout, false, dev);
}

static void
mtk_wed_reset_dma(struct mtk_wed_device *dev)
{
	bool busy = false;
	u32 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(dev->tx_ring); i++) {
		struct mtk_wdma_desc *desc = dev->tx_ring[i].desc;

		if (!desc)
			continue;

		mtk_wed_ring_reset(desc, MTK_WED_TX_RING_SIZE);
	}

	if (mtk_wed_poll_busy(dev))
		busy = mtk_wed_check_busy(dev);

	if (busy) {
		mtk_wed_reset(dev, MTK_WED_RESET_WED_TX_DMA);
	} else {
		wed_w32(dev, MTK_WED_RESET_IDX,
			MTK_WED_RESET_IDX_TX |
			MTK_WED_RESET_IDX_RX);
		wed_w32(dev, MTK_WED_RESET_IDX, 0);
	}

	wdma_w32(dev, MTK_WDMA_RESET_IDX, MTK_WDMA_RESET_IDX_RX);
	wdma_w32(dev, MTK_WDMA_RESET_IDX, 0);

	if (busy) {
		mtk_wed_reset(dev, MTK_WED_RESET_WDMA_INT_AGENT);
		mtk_wed_reset(dev, MTK_WED_RESET_WDMA_RX_DRV);
	} else {
		wed_w32(dev, MTK_WED_WDMA_RESET_IDX,
			MTK_WED_WDMA_RESET_IDX_RX | MTK_WED_WDMA_RESET_IDX_DRV);
		wed_w32(dev, MTK_WED_WDMA_RESET_IDX, 0);

		wed_set(dev, MTK_WED_WDMA_GLO_CFG,
			MTK_WED_WDMA_GLO_CFG_RST_INIT_COMPLETE);

		wed_clr(dev, MTK_WED_WDMA_GLO_CFG,
			MTK_WED_WDMA_GLO_CFG_RST_INIT_COMPLETE);
	}

	for (i = 0; i < 100; i++) {
		val = wed_r32(dev, MTK_WED_TX_BM_INTF);
		if (FIELD_GET(MTK_WED_TX_BM_INTF_TKFIFO_FDEP, val) == 0x40)
			break;
	}

	mtk_wed_reset(dev, MTK_WED_RESET_TX_FREE_AGENT);
	mtk_wed_reset(dev, MTK_WED_RESET_TX_BM);

	if (busy) {
		mtk_wed_reset(dev, MTK_WED_RESET_WPDMA_INT_AGENT);
		mtk_wed_reset(dev, MTK_WED_RESET_WPDMA_TX_DRV);
		mtk_wed_reset(dev, MTK_WED_RESET_WPDMA_RX_DRV);
	} else {
		wed_w32(dev, MTK_WED_WPDMA_RESET_IDX,
			MTK_WED_WPDMA_RESET_IDX_TX |
			MTK_WED_WPDMA_RESET_IDX_RX);
		wed_w32(dev, MTK_WED_WPDMA_RESET_IDX, 0);
	}

}

static int
mtk_wed_ring_alloc(struct mtk_wed_device *dev, struct mtk_wed_ring *ring,
		   int size)
{
	ring->desc = dma_alloc_coherent(dev->hw->dev,
					size * sizeof(*ring->desc),
					&ring->desc_phys, GFP_KERNEL);
	if (!ring->desc)
		return -ENOMEM;

	ring->size = size;
	mtk_wed_ring_reset(ring->desc, size);

	return 0;
}

static int
mtk_wed_wdma_ring_setup(struct mtk_wed_device *dev, int idx, int size)
{
	struct mtk_wed_ring *wdma = &dev->tx_wdma[idx];

	if (mtk_wed_ring_alloc(dev, wdma, MTK_WED_WDMA_RING_SIZE))
		return -ENOMEM;

	wdma_w32(dev, MTK_WDMA_RING_RX(idx) + MTK_WED_RING_OFS_BASE,
		 wdma->desc_phys);
	wdma_w32(dev, MTK_WDMA_RING_RX(idx) + MTK_WED_RING_OFS_COUNT,
		 size);
	wdma_w32(dev, MTK_WDMA_RING_RX(idx) + MTK_WED_RING_OFS_CPU_IDX, 0);

	wed_w32(dev, MTK_WED_WDMA_RING_RX(idx) + MTK_WED_RING_OFS_BASE,
		wdma->desc_phys);
	wed_w32(dev, MTK_WED_WDMA_RING_RX(idx) + MTK_WED_RING_OFS_COUNT,
		size);

	return 0;
}

static void
mtk_wed_start(struct mtk_wed_device *dev, u32 irq_mask)
{
	u32 wdma_mask;
	u32 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(dev->tx_wdma); i++)
		if (!dev->tx_wdma[i].desc)
			mtk_wed_wdma_ring_setup(dev, i, 16);

	wdma_mask = FIELD_PREP(MTK_WDMA_INT_MASK_RX_DONE, GENMASK(1, 0));

	mtk_wed_hw_init(dev);

	wed_set(dev, MTK_WED_CTRL,
		MTK_WED_CTRL_WDMA_INT_AGENT_EN |
		MTK_WED_CTRL_WPDMA_INT_AGENT_EN |
		MTK_WED_CTRL_WED_TX_BM_EN |
		MTK_WED_CTRL_WED_TX_FREE_AGENT_EN);

	wed_w32(dev, MTK_WED_PCIE_INT_TRIGGER, MTK_WED_PCIE_INT_TRIGGER_STATUS);

	wed_w32(dev, MTK_WED_WPDMA_INT_TRIGGER,
		MTK_WED_WPDMA_INT_TRIGGER_RX_DONE |
		MTK_WED_WPDMA_INT_TRIGGER_TX_DONE);

	wed_set(dev, MTK_WED_WPDMA_INT_CTRL,
		MTK_WED_WPDMA_INT_CTRL_SUBRT_ADV);

	wed_w32(dev, MTK_WED_WDMA_INT_TRIGGER, wdma_mask);
	wed_clr(dev, MTK_WED_WDMA_INT_CTRL, wdma_mask);

	wdma_w32(dev, MTK_WDMA_INT_MASK, wdma_mask);
	wdma_w32(dev, MTK_WDMA_INT_GRP2, wdma_mask);

	wed_w32(dev, MTK_WED_WPDMA_INT_MASK, irq_mask);
	wed_w32(dev, MTK_WED_INT_MASK, irq_mask);

	wed_set(dev, MTK_WED_GLO_CFG,
		MTK_WED_GLO_CFG_TX_DMA_EN |
		MTK_WED_GLO_CFG_RX_DMA_EN);
	wed_set(dev, MTK_WED_WPDMA_GLO_CFG,
		MTK_WED_WPDMA_GLO_CFG_TX_DRV_EN |
		MTK_WED_WPDMA_GLO_CFG_RX_DRV_EN);
	wed_set(dev, MTK_WED_WDMA_GLO_CFG,
		MTK_WED_WDMA_GLO_CFG_RX_DRV_EN);

	mtk_wed_set_ext_int(dev, true);
	val = dev->wlan.wpdma_phys |
	      MTK_PCIE_MIRROR_MAP_EN |
	      FIELD_PREP(MTK_PCIE_MIRROR_MAP_WED_ID, dev->hw->index);

	if (dev->hw->index)
		val |= BIT(1);
	val |= BIT(0);
	regmap_write(dev->hw->mirror, dev->hw->index * 4, val);

	dev->running = true;
}

static int
mtk_wed_attach(struct mtk_wed_device *dev)
	__releases(RCU)
{
	struct mtk_wed_hw *hw;
	int ret = 0;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
			 "mtk_wed_attach without holding the RCU read lock");

	if (pci_domain_nr(dev->wlan.pci_dev->bus) > 1 ||
	    !try_module_get(THIS_MODULE))
		ret = -ENODEV;

	rcu_read_unlock();

	if (ret)
		return ret;

	mutex_lock(&hw_lock);

	hw = mtk_wed_assign(dev);
	if (!hw) {
		module_put(THIS_MODULE);
		ret = -ENODEV;
		goto out;
	}

	dev_info(&dev->wlan.pci_dev->dev, "attaching wed device %d\n", hw->index);

	dev->hw = hw;
	dev->dev = hw->dev;
	dev->irq = hw->irq;
	dev->wdma_idx = hw->index;

	if (hw->eth->dma_dev == hw->eth->dev &&
	    of_dma_is_coherent(hw->eth->dev->of_node))
		mtk_eth_set_dma_device(hw->eth, hw->dev);

	ret = mtk_wed_buffer_alloc(dev);
	if (ret) {
		mtk_wed_detach(dev);
		goto out;
	}

	mtk_wed_hw_init_early(dev);
	regmap_update_bits(hw->hifsys, HIFSYS_DMA_AG_MAP, BIT(hw->index), 0);

out:
	mutex_unlock(&hw_lock);

	return ret;
}

static int
mtk_wed_tx_ring_setup(struct mtk_wed_device *dev, int idx, void __iomem *regs)
{
	struct mtk_wed_ring *ring = &dev->tx_ring[idx];

	/*
	 * Tx ring redirection:
	 * Instead of configuring the WLAN PDMA TX ring directly, the WLAN
	 * driver allocated DMA ring gets configured into WED MTK_WED_RING_TX(n)
	 * registers.
	 *
	 * WED driver posts its own DMA ring as WLAN PDMA TX and configures it
	 * into MTK_WED_WPDMA_RING_TX(n) registers.
	 * It gets filled with packets picked up from WED TX ring and from
	 * WDMA RX.
	 */

	BUG_ON(idx >= ARRAY_SIZE(dev->tx_ring));

	if (mtk_wed_ring_alloc(dev, ring, MTK_WED_TX_RING_SIZE))
		return -ENOMEM;

	if (mtk_wed_wdma_ring_setup(dev, idx, MTK_WED_WDMA_RING_SIZE))
		return -ENOMEM;

	ring->reg_base = MTK_WED_RING_TX(idx);
	ring->wpdma = regs;

	/* WED -> WPDMA */
	wpdma_tx_w32(dev, idx, MTK_WED_RING_OFS_BASE, ring->desc_phys);
	wpdma_tx_w32(dev, idx, MTK_WED_RING_OFS_COUNT, MTK_WED_TX_RING_SIZE);
	wpdma_tx_w32(dev, idx, MTK_WED_RING_OFS_CPU_IDX, 0);

	wed_w32(dev, MTK_WED_WPDMA_RING_TX(idx) + MTK_WED_RING_OFS_BASE,
		ring->desc_phys);
	wed_w32(dev, MTK_WED_WPDMA_RING_TX(idx) + MTK_WED_RING_OFS_COUNT,
		MTK_WED_TX_RING_SIZE);
	wed_w32(dev, MTK_WED_WPDMA_RING_TX(idx) + MTK_WED_RING_OFS_CPU_IDX, 0);

	return 0;
}

static int
mtk_wed_txfree_ring_setup(struct mtk_wed_device *dev, void __iomem *regs)
{
	struct mtk_wed_ring *ring = &dev->txfree_ring;
	int i;

	/*
	 * For txfree event handling, the same DMA ring is shared between WED
	 * and WLAN. The WLAN driver accesses the ring index registers through
	 * WED
	 */
	ring->reg_base = MTK_WED_RING_RX(1);
	ring->wpdma = regs;

	for (i = 0; i < 12; i += 4) {
		u32 val = readl(regs + i);

		wed_w32(dev, MTK_WED_RING_RX(1) + i, val);
		wed_w32(dev, MTK_WED_WPDMA_RING_RX(1) + i, val);
	}

	return 0;
}

static u32
mtk_wed_irq_get(struct mtk_wed_device *dev, u32 mask)
{
	u32 val;

	val = wed_r32(dev, MTK_WED_EXT_INT_STATUS);
	wed_w32(dev, MTK_WED_EXT_INT_STATUS, val);
	val &= MTK_WED_EXT_INT_STATUS_ERROR_MASK;
	if (!dev->hw->num_flows)
		val &= ~MTK_WED_EXT_INT_STATUS_TKID_WO_PYLD;
	if (val && net_ratelimit())
		pr_err("mtk_wed%d: error status=%08x\n", dev->hw->index, val);

	val = wed_r32(dev, MTK_WED_INT_STATUS);
	val &= mask;
	wed_w32(dev, MTK_WED_INT_STATUS, val); /* ACK */

	return val;
}

static void
mtk_wed_irq_set_mask(struct mtk_wed_device *dev, u32 mask)
{
	if (!dev->running)
		return;

	mtk_wed_set_ext_int(dev, !!mask);
	wed_w32(dev, MTK_WED_INT_MASK, mask);
}

int mtk_wed_flow_add(int index)
{
	struct mtk_wed_hw *hw = hw_list[index];
	int ret;

	if (!hw || !hw->wed_dev)
		return -ENODEV;

	if (hw->num_flows) {
		hw->num_flows++;
		return 0;
	}

	mutex_lock(&hw_lock);
	if (!hw->wed_dev) {
		ret = -ENODEV;
		goto out;
	}

	ret = hw->wed_dev->wlan.offload_enable(hw->wed_dev);
	if (!ret)
		hw->num_flows++;
	mtk_wed_set_ext_int(hw->wed_dev, true);

out:
	mutex_unlock(&hw_lock);

	return ret;
}

void mtk_wed_flow_remove(int index)
{
	struct mtk_wed_hw *hw = hw_list[index];

	if (!hw)
		return;

	if (--hw->num_flows)
		return;

	mutex_lock(&hw_lock);
	if (!hw->wed_dev)
		goto out;

	hw->wed_dev->wlan.offload_disable(hw->wed_dev);
	mtk_wed_set_ext_int(hw->wed_dev, true);

out:
	mutex_unlock(&hw_lock);
}

void mtk_wed_add_hw(struct device_node *np, struct mtk_eth *eth,
		    void __iomem *wdma, int index)
{
	static const struct mtk_wed_ops wed_ops = {
		.attach = mtk_wed_attach,
		.tx_ring_setup = mtk_wed_tx_ring_setup,
		.txfree_ring_setup = mtk_wed_txfree_ring_setup,
		.start = mtk_wed_start,
		.stop = mtk_wed_stop,
		.reset_dma = mtk_wed_reset_dma,
		.reg_read = wed_r32,
		.reg_write = wed_w32,
		.irq_get = mtk_wed_irq_get,
		.irq_set_mask = mtk_wed_irq_set_mask,
		.detach = mtk_wed_detach,
	};
	struct device_node *eth_np = eth->dev->of_node;
	struct platform_device *pdev;
	struct mtk_wed_hw *hw;
	struct regmap *regs;
	int irq;

	if (!np)
		return;

	pdev = of_find_device_by_node(np);
	if (!pdev)
		return;

	get_device(&pdev->dev);
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return;

	regs = syscon_regmap_lookup_by_phandle(np, NULL);
	if (IS_ERR(regs))
		return;

	rcu_assign_pointer(mtk_soc_wed_ops, &wed_ops);

	mutex_lock(&hw_lock);

	if (WARN_ON(hw_list[index]))
		goto unlock;

	hw = kzalloc(sizeof(*hw), GFP_KERNEL);
	if (!hw)
		goto unlock;
	hw->node = np;
	hw->regs = regs;
	hw->eth = eth;
	hw->dev = &pdev->dev;
	hw->wdma = wdma;
	hw->index = index;
	hw->irq = irq;
	hw->mirror = syscon_regmap_lookup_by_phandle(eth_np,
						     "mediatek,pcie-mirror");
	hw->hifsys = syscon_regmap_lookup_by_phandle(eth_np,
						     "mediatek,hifsys");
	if (IS_ERR(hw->mirror) || IS_ERR(hw->hifsys)) {
		kfree(hw);
		goto unlock;
	}

	if (!index) {
		regmap_write(hw->mirror, 0, 0);
		regmap_write(hw->mirror, 4, 0);
	}
	mtk_wed_hw_add_debugfs(hw);

	hw_list[index] = hw;

unlock:
	mutex_unlock(&hw_lock);
}

void mtk_wed_exit(void)
{
	int i;

	rcu_assign_pointer(mtk_soc_wed_ops, NULL);

	synchronize_rcu();

	for (i = 0; i < ARRAY_SIZE(hw_list); i++) {
		struct mtk_wed_hw *hw;

		hw = hw_list[i];
		if (!hw)
			continue;

		hw_list[i] = NULL;
		debugfs_remove(hw->debugfs_dir);
		put_device(hw->dev);
		kfree(hw);
	}
}
