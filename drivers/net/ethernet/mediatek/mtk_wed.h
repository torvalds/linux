// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2021 Felix Fietkau <nbd@nbd.name> */

#ifndef __MTK_WED_PRIV_H
#define __MTK_WED_PRIV_H

#include <linux/soc/mediatek/mtk_wed.h>
#include <linux/debugfs.h>
#include <linux/regmap.h>
#include <linux/netdevice.h>

struct mtk_eth;

struct mtk_wed_hw {
	struct device_node *node;
	struct mtk_eth *eth;
	struct regmap *regs;
	struct regmap *hifsys;
	struct device *dev;
	void __iomem *wdma;
	struct regmap *mirror;
	struct dentry *debugfs_dir;
	struct mtk_wed_device *wed_dev;
	u32 debugfs_reg;
	u32 num_flows;
	char dirname[5];
	int irq;
	int index;
};

struct mtk_wdma_info {
	u8 wdma_idx;
	u8 queue;
	u16 wcid;
	u8 bss;
};

#ifdef CONFIG_NET_MEDIATEK_SOC_WED
static inline void
wed_w32(struct mtk_wed_device *dev, u32 reg, u32 val)
{
	regmap_write(dev->hw->regs, reg, val);
}

static inline u32
wed_r32(struct mtk_wed_device *dev, u32 reg)
{
	unsigned int val;

	regmap_read(dev->hw->regs, reg, &val);

	return val;
}

static inline void
wdma_w32(struct mtk_wed_device *dev, u32 reg, u32 val)
{
	writel(val, dev->hw->wdma + reg);
}

static inline u32
wdma_r32(struct mtk_wed_device *dev, u32 reg)
{
	return readl(dev->hw->wdma + reg);
}

static inline u32
wpdma_tx_r32(struct mtk_wed_device *dev, int ring, u32 reg)
{
	if (!dev->tx_ring[ring].wpdma)
		return 0;

	return readl(dev->tx_ring[ring].wpdma + reg);
}

static inline void
wpdma_tx_w32(struct mtk_wed_device *dev, int ring, u32 reg, u32 val)
{
	if (!dev->tx_ring[ring].wpdma)
		return;

	writel(val, dev->tx_ring[ring].wpdma + reg);
}

static inline u32
wpdma_txfree_r32(struct mtk_wed_device *dev, u32 reg)
{
	if (!dev->txfree_ring.wpdma)
		return 0;

	return readl(dev->txfree_ring.wpdma + reg);
}

static inline void
wpdma_txfree_w32(struct mtk_wed_device *dev, u32 reg, u32 val)
{
	if (!dev->txfree_ring.wpdma)
		return;

	writel(val, dev->txfree_ring.wpdma + reg);
}

void mtk_wed_add_hw(struct device_node *np, struct mtk_eth *eth,
		    void __iomem *wdma, int index);
void mtk_wed_exit(void);
int mtk_wed_flow_add(int index);
void mtk_wed_flow_remove(int index);
#else
static inline void
mtk_wed_add_hw(struct device_node *np, struct mtk_eth *eth,
	       void __iomem *wdma, int index)
{
}
static inline void
mtk_wed_exit(void)
{
}
static inline int mtk_wed_flow_add(int index)
{
	return -EINVAL;
}
static inline void mtk_wed_flow_remove(int index)
{
}
#endif

#ifdef CONFIG_DEBUG_FS
void mtk_wed_hw_add_debugfs(struct mtk_wed_hw *hw);
#else
static inline void mtk_wed_hw_add_debugfs(struct mtk_wed_hw *hw)
{
}
#endif

#endif
