// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/rtnetlink.h>

#include "mt7996.h"
#include "mac.h"
#include "mcu.h"
#include "../trace.h"
#include "../dma.h"

static bool wed_enable;
module_param(wed_enable, bool, 0644);

static const struct __base mt7996_reg_base[] = {
	[WF_AGG_BASE]		= { { 0x820e2000, 0x820f2000, 0x830e2000 } },
	[WF_ARB_BASE]		= { { 0x820e3000, 0x820f3000, 0x830e3000 } },
	[WF_TMAC_BASE]		= { { 0x820e4000, 0x820f4000, 0x830e4000 } },
	[WF_RMAC_BASE]		= { { 0x820e5000, 0x820f5000, 0x830e5000 } },
	[WF_DMA_BASE]		= { { 0x820e7000, 0x820f7000, 0x830e7000 } },
	[WF_WTBLOFF_BASE]	= { { 0x820e9000, 0x820f9000, 0x830e9000 } },
	[WF_ETBF_BASE]		= { { 0x820ea000, 0x820fa000, 0x830ea000 } },
	[WF_LPON_BASE]		= { { 0x820eb000, 0x820fb000, 0x830eb000 } },
	[WF_MIB_BASE]		= { { 0x820ed000, 0x820fd000, 0x830ed000 } },
	[WF_RATE_BASE]		= { { 0x820ee000, 0x820fe000, 0x830ee000 } },
};

static const u32 mt7996_offs[] = {
	[MIB_RVSR0]		= 0x720,
	[MIB_RVSR1]		= 0x724,
	[MIB_BTSCR5]		= 0x788,
	[MIB_BTSCR6]		= 0x798,
	[MIB_RSCR1]		= 0x7ac,
	[MIB_RSCR27]		= 0x954,
	[MIB_RSCR28]		= 0x958,
	[MIB_RSCR29]		= 0x95c,
	[MIB_RSCR30]		= 0x960,
	[MIB_RSCR31]		= 0x964,
	[MIB_RSCR33]		= 0x96c,
	[MIB_RSCR35]		= 0x974,
	[MIB_RSCR36]		= 0x978,
	[MIB_BSCR0]		= 0x9cc,
	[MIB_BSCR1]		= 0x9d0,
	[MIB_BSCR2]		= 0x9d4,
	[MIB_BSCR3]		= 0x9d8,
	[MIB_BSCR4]		= 0x9dc,
	[MIB_BSCR5]		= 0x9e0,
	[MIB_BSCR6]		= 0x9e4,
	[MIB_BSCR7]		= 0x9e8,
	[MIB_BSCR17]		= 0xa10,
	[MIB_TRDR1]		= 0xa28,
};

static const u32 mt7992_offs[] = {
	[MIB_RVSR0]		= 0x760,
	[MIB_RVSR1]		= 0x764,
	[MIB_BTSCR5]		= 0x7c8,
	[MIB_BTSCR6]		= 0x7d8,
	[MIB_RSCR1]		= 0x7f0,
	[MIB_RSCR27]		= 0x998,
	[MIB_RSCR28]		= 0x99c,
	[MIB_RSCR29]		= 0x9a0,
	[MIB_RSCR30]		= 0x9a4,
	[MIB_RSCR31]		= 0x9a8,
	[MIB_RSCR33]		= 0x9b0,
	[MIB_RSCR35]		= 0x9b8,
	[MIB_RSCR36]		= 0x9bc,
	[MIB_BSCR0]		= 0xac8,
	[MIB_BSCR1]		= 0xacc,
	[MIB_BSCR2]		= 0xad0,
	[MIB_BSCR3]		= 0xad4,
	[MIB_BSCR4]		= 0xad8,
	[MIB_BSCR5]		= 0xadc,
	[MIB_BSCR6]		= 0xae0,
	[MIB_BSCR7]		= 0xae4,
	[MIB_BSCR17]		= 0xb0c,
	[MIB_TRDR1]		= 0xb24,
};

static const struct __map mt7996_reg_map[] = {
	{ 0x54000000, 0x02000, 0x1000 }, /* WFDMA_0 (PCIE0 MCU DMA0) */
	{ 0x55000000, 0x03000, 0x1000 }, /* WFDMA_1 (PCIE0 MCU DMA1) */
	{ 0x56000000, 0x04000, 0x1000 }, /* WFDMA reserved */
	{ 0x57000000, 0x05000, 0x1000 }, /* WFDMA MCU wrap CR */
	{ 0x58000000, 0x06000, 0x1000 }, /* WFDMA PCIE1 MCU DMA0 (MEM_DMA) */
	{ 0x59000000, 0x07000, 0x1000 }, /* WFDMA PCIE1 MCU DMA1 */
	{ 0x820c0000, 0x08000, 0x4000 }, /* WF_UMAC_TOP (PLE) */
	{ 0x820c8000, 0x0c000, 0x2000 }, /* WF_UMAC_TOP (PSE) */
	{ 0x820cc000, 0x0e000, 0x1000 }, /* WF_UMAC_TOP (PP) */
	{ 0x74030000, 0x10000, 0x1000 }, /* PCIe MAC */
	{ 0x820e0000, 0x20000, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_CFG) */
	{ 0x820e1000, 0x20400, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_TRB) */
	{ 0x820e2000, 0x20800, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_AGG) */
	{ 0x820e3000, 0x20c00, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_ARB) */
	{ 0x820e4000, 0x21000, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_TMAC) */
	{ 0x820e5000, 0x21400, 0x0800 }, /* WF_LMAC_TOP BN0 (WF_RMAC) */
	{ 0x820ce000, 0x21c00, 0x0200 }, /* WF_LMAC_TOP (WF_SEC) */
	{ 0x820e7000, 0x21e00, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_DMA) */
	{ 0x820cf000, 0x22000, 0x1000 }, /* WF_LMAC_TOP (WF_PF) */
	{ 0x820e9000, 0x23400, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_WTBLOFF) */
	{ 0x820ea000, 0x24000, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_ETBF) */
	{ 0x820eb000, 0x24200, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_LPON) */
	{ 0x820ec000, 0x24600, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_INT) */
	{ 0x820ed000, 0x24800, 0x0800 }, /* WF_LMAC_TOP BN0 (WF_MIB) */
	{ 0x820ca000, 0x26000, 0x2000 }, /* WF_LMAC_TOP BN0 (WF_MUCOP) */
	{ 0x820d0000, 0x30000, 0x10000 }, /* WF_LMAC_TOP (WF_WTBLON) */
	{ 0x40000000, 0x70000, 0x10000 }, /* WF_UMAC_SYSRAM */
	{ 0x00400000, 0x80000, 0x10000 }, /* WF_MCU_SYSRAM */
	{ 0x00410000, 0x90000, 0x10000 }, /* WF_MCU_SYSRAM (configure register) */
	{ 0x820f0000, 0xa0000, 0x0400 }, /* WF_LMAC_TOP BN1 (WF_CFG) */
	{ 0x820f1000, 0xa0600, 0x0200 }, /* WF_LMAC_TOP BN1 (WF_TRB) */
	{ 0x820f2000, 0xa0800, 0x0400 }, /* WF_LMAC_TOP BN1 (WF_AGG) */
	{ 0x820f3000, 0xa0c00, 0x0400 }, /* WF_LMAC_TOP BN1 (WF_ARB) */
	{ 0x820f4000, 0xa1000, 0x0400 }, /* WF_LMAC_TOP BN1 (WF_TMAC) */
	{ 0x820f5000, 0xa1400, 0x0800 }, /* WF_LMAC_TOP BN1 (WF_RMAC) */
	{ 0x820f7000, 0xa1e00, 0x0200 }, /* WF_LMAC_TOP BN1 (WF_DMA) */
	{ 0x820f9000, 0xa3400, 0x0200 }, /* WF_LMAC_TOP BN1 (WF_WTBLOFF) */
	{ 0x820fa000, 0xa4000, 0x0200 }, /* WF_LMAC_TOP BN1 (WF_ETBF) */
	{ 0x820fb000, 0xa4200, 0x0400 }, /* WF_LMAC_TOP BN1 (WF_LPON) */
	{ 0x820fc000, 0xa4600, 0x0200 }, /* WF_LMAC_TOP BN1 (WF_INT) */
	{ 0x820fd000, 0xa4800, 0x0800 }, /* WF_LMAC_TOP BN1 (WF_MIB) */
	{ 0x820cc000, 0xa5000, 0x2000 }, /* WF_LMAC_TOP BN1 (WF_MUCOP) */
	{ 0x820c4000, 0xa8000, 0x4000 }, /* WF_LMAC_TOP BN1 (WF_MUCOP) */
	{ 0x820b0000, 0xae000, 0x1000 }, /* [APB2] WFSYS_ON */
	{ 0x80020000, 0xb0000, 0x10000 }, /* WF_TOP_MISC_OFF */
	{ 0x81020000, 0xc0000, 0x10000 }, /* WF_TOP_MISC_ON */
	{ 0x7c020000, 0xd0000, 0x10000 }, /* CONN_INFRA, wfdma */
	{ 0x7c060000, 0xe0000, 0x10000 }, /* CONN_INFRA, conn_host_csr_top */
	{ 0x7c000000, 0xf0000, 0x10000 }, /* CONN_INFRA */
	{ 0x0, 0x0, 0x0 }, /* imply end of search */
};

static u32 mt7996_reg_map_l1(struct mt7996_dev *dev, u32 addr)
{
	u32 offset = FIELD_GET(MT_HIF_REMAP_L1_OFFSET, addr);
	u32 base = FIELD_GET(MT_HIF_REMAP_L1_BASE, addr);

	dev->reg_l1_backup = dev->bus_ops->rr(&dev->mt76, MT_HIF_REMAP_L1);
	dev->bus_ops->rmw(&dev->mt76, MT_HIF_REMAP_L1,
			  MT_HIF_REMAP_L1_MASK,
			  FIELD_PREP(MT_HIF_REMAP_L1_MASK, base));
	/* use read to push write */
	dev->bus_ops->rr(&dev->mt76, MT_HIF_REMAP_L1);

	return MT_HIF_REMAP_BASE_L1 + offset;
}

static u32 mt7996_reg_map_l2(struct mt7996_dev *dev, u32 addr)
{
	u32 offset = FIELD_GET(MT_HIF_REMAP_L2_OFFSET, addr);
	u32 base = FIELD_GET(MT_HIF_REMAP_L2_BASE, addr);

	dev->reg_l2_backup = dev->bus_ops->rr(&dev->mt76, MT_HIF_REMAP_L2);
	dev->bus_ops->rmw(&dev->mt76, MT_HIF_REMAP_L2,
			  MT_HIF_REMAP_L2_MASK,
			  FIELD_PREP(MT_HIF_REMAP_L2_MASK, base));
	/* use read to push write */
	dev->bus_ops->rr(&dev->mt76, MT_HIF_REMAP_L2);

	return MT_HIF_REMAP_BASE_L2 + offset;
}

static void mt7996_reg_remap_restore(struct mt7996_dev *dev)
{
	/* remap to ori status */
	if (unlikely(dev->reg_l1_backup)) {
		dev->bus_ops->wr(&dev->mt76, MT_HIF_REMAP_L1, dev->reg_l1_backup);
		dev->reg_l1_backup = 0;
	}

	if (dev->reg_l2_backup) {
		dev->bus_ops->wr(&dev->mt76, MT_HIF_REMAP_L2, dev->reg_l2_backup);
		dev->reg_l2_backup = 0;
	}
}

static u32 __mt7996_reg_addr(struct mt7996_dev *dev, u32 addr)
{
	int i;

	mt7996_reg_remap_restore(dev);

	if (addr < 0x100000)
		return addr;

	for (i = 0; i < dev->reg.map_size; i++) {
		u32 ofs;

		if (addr < dev->reg.map[i].phys)
			continue;

		ofs = addr - dev->reg.map[i].phys;
		if (ofs > dev->reg.map[i].size)
			continue;

		return dev->reg.map[i].mapped + ofs;
	}

	if ((addr >= MT_INFRA_BASE && addr < MT_WFSYS0_PHY_START) ||
	    (addr >= MT_WFSYS0_PHY_START && addr < MT_WFSYS1_PHY_START) ||
	    (addr >= MT_WFSYS1_PHY_START && addr <= MT_WFSYS1_PHY_END))
		return mt7996_reg_map_l1(dev, addr);

	if (dev_is_pci(dev->mt76.dev) &&
	    ((addr >= MT_CBTOP1_PHY_START && addr <= MT_CBTOP1_PHY_END) ||
	    addr >= MT_CBTOP2_PHY_START))
		return mt7996_reg_map_l1(dev, addr);

	/* CONN_INFRA: covert to phyiscal addr and use layer 1 remap */
	if (addr >= MT_INFRA_MCU_START && addr <= MT_INFRA_MCU_END) {
		addr = addr - MT_INFRA_MCU_START + MT_INFRA_BASE;
		return mt7996_reg_map_l1(dev, addr);
	}

	return mt7996_reg_map_l2(dev, addr);
}

void mt7996_memcpy_fromio(struct mt7996_dev *dev, void *buf, u32 offset,
			  size_t len)
{
	u32 addr = __mt7996_reg_addr(dev, offset);

	memcpy_fromio(buf, dev->mt76.mmio.regs + addr, len);
}

static u32 mt7996_rr(struct mt76_dev *mdev, u32 offset)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);

	return dev->bus_ops->rr(mdev, __mt7996_reg_addr(dev, offset));
}

static void mt7996_wr(struct mt76_dev *mdev, u32 offset, u32 val)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);

	dev->bus_ops->wr(mdev, __mt7996_reg_addr(dev, offset), val);
}

static u32 mt7996_rmw(struct mt76_dev *mdev, u32 offset, u32 mask, u32 val)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);

	return dev->bus_ops->rmw(mdev, __mt7996_reg_addr(dev, offset), mask, val);
}

#ifdef CONFIG_NET_MEDIATEK_SOC_WED
static int mt7996_mmio_wed_reset(struct mtk_wed_device *wed)
{
	struct mt76_dev *mdev = container_of(wed, struct mt76_dev, mmio.wed);
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);
	struct mt76_phy *mphy = &dev->mphy;
	int ret;

	ASSERT_RTNL();

	if (test_and_set_bit(MT76_STATE_WED_RESET, &mphy->state))
		return -EBUSY;

	ret = mt7996_mcu_set_ser(dev, UNI_CMD_SER_TRIGGER, UNI_CMD_SER_SET_RECOVER_L1,
				 mphy->band_idx);
	if (ret)
		goto out;

	rtnl_unlock();
	if (!wait_for_completion_timeout(&mdev->mmio.wed_reset, 20 * HZ)) {
		dev_err(mdev->dev, "wed reset timeout\n");
		ret = -ETIMEDOUT;
	}
	rtnl_lock();
out:
	clear_bit(MT76_STATE_WED_RESET, &mphy->state);

	return ret;
}
#endif

int mt7996_mmio_wed_init(struct mt7996_dev *dev, void *pdev_ptr,
			 bool hif2, int *irq)
{
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
	struct pci_dev *pci_dev = pdev_ptr;
	u32 hif1_ofs = 0;

	if (!wed_enable)
		return 0;

	dev->has_rro = true;

	hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

	if (hif2)
		wed = &dev->mt76.mmio.wed_hif2;

	wed->wlan.pci_dev = pci_dev;
	wed->wlan.bus_type = MTK_WED_BUS_PCIE;

	wed->wlan.base = devm_ioremap(dev->mt76.dev,
				      pci_resource_start(pci_dev, 0),
				      pci_resource_len(pci_dev, 0));
	wed->wlan.phy_base = pci_resource_start(pci_dev, 0);

	if (hif2) {
		wed->wlan.wpdma_int = wed->wlan.phy_base +
				      MT_INT_PCIE1_SOURCE_CSR_EXT;
		wed->wlan.wpdma_mask = wed->wlan.phy_base +
				       MT_INT_PCIE1_MASK_CSR;
		wed->wlan.wpdma_tx = wed->wlan.phy_base + hif1_ofs +
					     MT_TXQ_RING_BASE(0) +
					     MT7996_TXQ_BAND2 * MT_RING_SIZE;
		if (dev->has_rro) {
			wed->wlan.wpdma_txfree = wed->wlan.phy_base + hif1_ofs +
						 MT_RXQ_RING_BASE(0) +
						 MT7996_RXQ_TXFREE2 * MT_RING_SIZE;
			wed->wlan.txfree_tbit = ffs(MT_INT_RX_TXFREE_EXT) - 1;
		} else {
			wed->wlan.wpdma_txfree = wed->wlan.phy_base + hif1_ofs +
						 MT_RXQ_RING_BASE(0) +
						 MT7996_RXQ_MCU_WA_TRI * MT_RING_SIZE;
			wed->wlan.txfree_tbit = ffs(MT_INT_RX_DONE_WA_TRI) - 1;
		}

		wed->wlan.wpdma_rx_glo = wed->wlan.phy_base + hif1_ofs + MT_WFDMA0_GLO_CFG;
		wed->wlan.wpdma_rx = wed->wlan.phy_base + hif1_ofs +
				     MT_RXQ_RING_BASE(MT7996_RXQ_BAND0) +
				     MT7996_RXQ_BAND0 * MT_RING_SIZE;

		wed->wlan.id = 0x7991;
		wed->wlan.tx_tbit[0] = ffs(MT_INT_TX_DONE_BAND2) - 1;
	} else {
		wed->wlan.hw_rro = dev->has_rro; /* default on */
		wed->wlan.wpdma_int = wed->wlan.phy_base + MT_INT_SOURCE_CSR;
		wed->wlan.wpdma_mask = wed->wlan.phy_base + MT_INT_MASK_CSR;
		wed->wlan.wpdma_tx = wed->wlan.phy_base + MT_TXQ_RING_BASE(0) +
				     MT7996_TXQ_BAND0 * MT_RING_SIZE;

		wed->wlan.wpdma_rx_glo = wed->wlan.phy_base + MT_WFDMA0_GLO_CFG;

		wed->wlan.wpdma_rx = wed->wlan.phy_base +
				     MT_RXQ_RING_BASE(MT7996_RXQ_BAND0) +
				     MT7996_RXQ_BAND0 * MT_RING_SIZE;

		wed->wlan.wpdma_rx_rro[0] = wed->wlan.phy_base +
					    MT_RXQ_RING_BASE(MT7996_RXQ_RRO_BAND0) +
					    MT7996_RXQ_RRO_BAND0 * MT_RING_SIZE;
		wed->wlan.wpdma_rx_rro[1] = wed->wlan.phy_base + hif1_ofs +
					    MT_RXQ_RING_BASE(MT7996_RXQ_RRO_BAND2) +
					    MT7996_RXQ_RRO_BAND2 * MT_RING_SIZE;
		wed->wlan.wpdma_rx_pg = wed->wlan.phy_base +
					MT_RXQ_RING_BASE(MT7996_RXQ_MSDU_PG_BAND0) +
					MT7996_RXQ_MSDU_PG_BAND0 * MT_RING_SIZE;

		wed->wlan.rx_nbuf = 65536;
		wed->wlan.rx_npkt = dev->hif2 ? 32768 : 24576;
		wed->wlan.rx_size = SKB_WITH_OVERHEAD(MT_RX_BUF_SIZE);

		wed->wlan.rx_tbit[0] = ffs(MT_INT_RX_DONE_BAND0) - 1;
		wed->wlan.rx_tbit[1] = ffs(MT_INT_RX_DONE_BAND2) - 1;

		wed->wlan.rro_rx_tbit[0] = ffs(MT_INT_RX_DONE_RRO_BAND0) - 1;
		wed->wlan.rro_rx_tbit[1] = ffs(MT_INT_RX_DONE_RRO_BAND2) - 1;

		wed->wlan.rx_pg_tbit[0] = ffs(MT_INT_RX_DONE_MSDU_PG_BAND0) - 1;
		wed->wlan.rx_pg_tbit[1] = ffs(MT_INT_RX_DONE_MSDU_PG_BAND1) - 1;
		wed->wlan.rx_pg_tbit[2] = ffs(MT_INT_RX_DONE_MSDU_PG_BAND2) - 1;

		wed->wlan.tx_tbit[0] = ffs(MT_INT_TX_DONE_BAND0) - 1;
		wed->wlan.tx_tbit[1] = ffs(MT_INT_TX_DONE_BAND1) - 1;
		if (dev->has_rro) {
			wed->wlan.wpdma_txfree = wed->wlan.phy_base + MT_RXQ_RING_BASE(0) +
						 MT7996_RXQ_TXFREE0 * MT_RING_SIZE;
			wed->wlan.txfree_tbit = ffs(MT_INT_RX_TXFREE_MAIN) - 1;
		} else {
			wed->wlan.txfree_tbit = ffs(MT_INT_RX_DONE_WA_MAIN) - 1;
			wed->wlan.wpdma_txfree = wed->wlan.phy_base + MT_RXQ_RING_BASE(0) +
						  MT7996_RXQ_MCU_WA_MAIN * MT_RING_SIZE;
		}
		dev->mt76.rx_token_size = MT7996_TOKEN_SIZE + wed->wlan.rx_npkt;
	}

	wed->wlan.nbuf = MT7996_HW_TOKEN_SIZE;
	wed->wlan.token_start = MT7996_TOKEN_SIZE - wed->wlan.nbuf;

	wed->wlan.amsdu_max_subframes = 8;
	wed->wlan.amsdu_max_len = 1536;

	wed->wlan.init_buf = mt7996_wed_init_buf;
	wed->wlan.init_rx_buf = mt76_mmio_wed_init_rx_buf;
	wed->wlan.release_rx_buf = mt76_mmio_wed_release_rx_buf;
	wed->wlan.offload_enable = mt76_mmio_wed_offload_enable;
	wed->wlan.offload_disable = mt76_mmio_wed_offload_disable;
	if (!hif2) {
		wed->wlan.reset = mt7996_mmio_wed_reset;
		wed->wlan.reset_complete = mt76_mmio_wed_reset_complete;
	}

	if (mtk_wed_device_attach(wed))
		return 0;

	*irq = wed->irq;
	dev->mt76.dma_dev = wed->dev;

	return 1;
#else
	return 0;
#endif
}

static int mt7996_mmio_init(struct mt76_dev *mdev,
			    void __iomem *mem_base,
			    u32 device_id)
{
	struct mt76_bus_ops *bus_ops;
	struct mt7996_dev *dev;

	dev = container_of(mdev, struct mt7996_dev, mt76);
	mt76_mmio_init(&dev->mt76, mem_base);

	switch (device_id) {
	case 0x7990:
		dev->reg.base = mt7996_reg_base;
		dev->reg.offs_rev = mt7996_offs;
		dev->reg.map = mt7996_reg_map;
		dev->reg.map_size = ARRAY_SIZE(mt7996_reg_map);
		break;
	case 0x7992:
		dev->reg.base = mt7996_reg_base;
		dev->reg.offs_rev = mt7992_offs;
		dev->reg.map = mt7996_reg_map;
		dev->reg.map_size = ARRAY_SIZE(mt7996_reg_map);
		break;
	default:
		return -EINVAL;
	}

	dev->bus_ops = dev->mt76.bus;
	bus_ops = devm_kmemdup(dev->mt76.dev, dev->bus_ops, sizeof(*bus_ops),
			       GFP_KERNEL);
	if (!bus_ops)
		return -ENOMEM;

	bus_ops->rr = mt7996_rr;
	bus_ops->wr = mt7996_wr;
	bus_ops->rmw = mt7996_rmw;
	dev->mt76.bus = bus_ops;

	mdev->rev = (device_id << 16) | (mt76_rr(dev, MT_HW_REV) & 0xff);

	dev_dbg(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	return 0;
}

void mt7996_dual_hif_set_irq_mask(struct mt7996_dev *dev, bool write_reg,
				  u32 clear, u32 set)
{
	struct mt76_dev *mdev = &dev->mt76;
	unsigned long flags;

	spin_lock_irqsave(&mdev->mmio.irq_lock, flags);

	mdev->mmio.irqmask &= ~clear;
	mdev->mmio.irqmask |= set;

	if (write_reg) {
		if (mtk_wed_device_active(&mdev->mmio.wed)) {
			mtk_wed_device_irq_set_mask(&mdev->mmio.wed,
						    mdev->mmio.irqmask);
			if (mtk_wed_device_active(&mdev->mmio.wed_hif2)) {
				mtk_wed_device_irq_set_mask(&mdev->mmio.wed_hif2,
							    mdev->mmio.irqmask);
			}
		} else {
			mt76_wr(dev, MT_INT_MASK_CSR, mdev->mmio.irqmask);
			mt76_wr(dev, MT_INT1_MASK_CSR, mdev->mmio.irqmask);
		}
	}

	spin_unlock_irqrestore(&mdev->mmio.irq_lock, flags);
}

static void mt7996_rx_poll_complete(struct mt76_dev *mdev,
				    enum mt76_rxq_id q)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);

	mt7996_irq_enable(dev, MT_INT_RX(q));
}

/* TODO: support 2/4/6/8 MSI-X vectors */
static void mt7996_irq_tasklet(struct tasklet_struct *t)
{
	struct mt7996_dev *dev = from_tasklet(dev, t, mt76.irq_tasklet);
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
	struct mtk_wed_device *wed_hif2 = &dev->mt76.mmio.wed_hif2;
	u32 i, intr, mask, intr1;

	if (dev->hif2 && mtk_wed_device_active(wed_hif2)) {
		mtk_wed_device_irq_set_mask(wed_hif2, 0);
		intr1 = mtk_wed_device_irq_get(wed_hif2,
					       dev->mt76.mmio.irqmask);
		if (intr1 & MT_INT_RX_TXFREE_EXT)
			napi_schedule(&dev->mt76.napi[MT_RXQ_TXFREE_BAND2]);
	}

	if (mtk_wed_device_active(wed)) {
		mtk_wed_device_irq_set_mask(wed, 0);
		intr = mtk_wed_device_irq_get(wed, dev->mt76.mmio.irqmask);
		intr |= (intr1 & ~MT_INT_RX_TXFREE_EXT);
	} else {
		mt76_wr(dev, MT_INT_MASK_CSR, 0);
		if (dev->hif2)
			mt76_wr(dev, MT_INT1_MASK_CSR, 0);

		intr = mt76_rr(dev, MT_INT_SOURCE_CSR);
		intr &= dev->mt76.mmio.irqmask;
		mt76_wr(dev, MT_INT_SOURCE_CSR, intr);
		if (dev->hif2) {
			intr1 = mt76_rr(dev, MT_INT1_SOURCE_CSR);
			intr1 &= dev->mt76.mmio.irqmask;
			mt76_wr(dev, MT_INT1_SOURCE_CSR, intr1);
			intr |= intr1;
		}
	}

	trace_dev_irq(&dev->mt76, intr, dev->mt76.mmio.irqmask);

	mask = intr & MT_INT_RX_DONE_ALL;
	if (intr & MT_INT_TX_DONE_MCU)
		mask |= MT_INT_TX_DONE_MCU;
	mt7996_irq_disable(dev, mask);

	if (intr & MT_INT_TX_DONE_MCU)
		napi_schedule(&dev->mt76.tx_napi);

	for (i = 0; i < __MT_RXQ_MAX; i++) {
		if ((intr & MT_INT_RX(i)))
			napi_schedule(&dev->mt76.napi[i]);
	}

	if (intr & MT_INT_MCU_CMD) {
		u32 val = mt76_rr(dev, MT_MCU_CMD);

		mt76_wr(dev, MT_MCU_CMD, val);
		if (val & (MT_MCU_CMD_ERROR_MASK | MT_MCU_CMD_WDT_MASK)) {
			dev->recovery.state = val;
			mt7996_reset(dev);
		}
	}
}

irqreturn_t mt7996_irq_handler(int irq, void *dev_instance)
{
	struct mt7996_dev *dev = dev_instance;

	if (mtk_wed_device_active(&dev->mt76.mmio.wed))
		mtk_wed_device_irq_set_mask(&dev->mt76.mmio.wed, 0);
	else
		mt76_wr(dev, MT_INT_MASK_CSR, 0);

	if (dev->hif2) {
		if (mtk_wed_device_active(&dev->mt76.mmio.wed_hif2))
			mtk_wed_device_irq_set_mask(&dev->mt76.mmio.wed_hif2, 0);
		else
			mt76_wr(dev, MT_INT1_MASK_CSR, 0);
	}

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return IRQ_NONE;

	tasklet_schedule(&dev->mt76.irq_tasklet);

	return IRQ_HANDLED;
}

struct mt7996_dev *mt7996_mmio_probe(struct device *pdev,
				     void __iomem *mem_base, u32 device_id)
{
	static const struct mt76_driver_ops drv_ops = {
		/* txwi_size = txd size + txp size */
		.txwi_size = MT_TXD_SIZE + sizeof(struct mt76_connac_fw_txp),
		.drv_flags = MT_DRV_TXWI_NO_FREE |
			     MT_DRV_AMSDU_OFFLOAD |
			     MT_DRV_HW_MGMT_TXQ,
		.survey_flags = SURVEY_INFO_TIME_TX |
				SURVEY_INFO_TIME_RX |
				SURVEY_INFO_TIME_BSS_RX,
		.token_size = MT7996_TOKEN_SIZE,
		.tx_prepare_skb = mt7996_tx_prepare_skb,
		.tx_complete_skb = mt76_connac_tx_complete_skb,
		.rx_skb = mt7996_queue_rx_skb,
		.rx_check = mt7996_rx_check,
		.rx_poll_complete = mt7996_rx_poll_complete,
		.sta_add = mt7996_mac_sta_add,
		.sta_remove = mt7996_mac_sta_remove,
		.update_survey = mt7996_update_channel,
	};
	struct mt7996_dev *dev;
	struct mt76_dev *mdev;
	int ret;

	mdev = mt76_alloc_device(pdev, sizeof(*dev), &mt7996_ops, &drv_ops);
	if (!mdev)
		return ERR_PTR(-ENOMEM);

	dev = container_of(mdev, struct mt7996_dev, mt76);

	ret = mt7996_mmio_init(mdev, mem_base, device_id);
	if (ret)
		goto error;

	tasklet_setup(&mdev->irq_tasklet, mt7996_irq_tasklet);

	mt76_wr(dev, MT_INT_MASK_CSR, 0);

	return dev;

error:
	mt76_free_device(&dev->mt76);

	return ERR_PTR(ret);
}

static int __init mt7996_init(void)
{
	int ret;

	ret = pci_register_driver(&mt7996_hif_driver);
	if (ret)
		return ret;

	ret = pci_register_driver(&mt7996_pci_driver);
	if (ret)
		pci_unregister_driver(&mt7996_hif_driver);

	return ret;
}

static void __exit mt7996_exit(void)
{
	pci_unregister_driver(&mt7996_pci_driver);
	pci_unregister_driver(&mt7996_hif_driver);
}

module_init(mt7996_init);
module_exit(mt7996_exit);
MODULE_DESCRIPTION("MediaTek MT7996 MMIO helpers");
MODULE_LICENSE("Dual BSD/GPL");
