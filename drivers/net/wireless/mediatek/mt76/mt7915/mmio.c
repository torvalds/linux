// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pci.h>

#include "mt7915.h"
#include "mac.h"
#include "../trace.h"

static u32 mt7915_reg_map_l1(struct mt7915_dev *dev, u32 addr)
{
	u32 offset = FIELD_GET(MT_HIF_REMAP_L1_OFFSET, addr);
	u32 base = FIELD_GET(MT_HIF_REMAP_L1_BASE, addr);

	mt76_rmw_field(dev, MT_HIF_REMAP_L1, MT_HIF_REMAP_L1_MASK, base);
	/* use read to push write */
	mt76_rr(dev, MT_HIF_REMAP_L1);

	return MT_HIF_REMAP_BASE_L1 + offset;
}

static u32 mt7915_reg_map_l2(struct mt7915_dev *dev, u32 addr)
{
	u32 offset = FIELD_GET(MT_HIF_REMAP_L2_OFFSET, addr);
	u32 base = FIELD_GET(MT_HIF_REMAP_L2_BASE, addr);

	mt76_rmw_field(dev, MT_HIF_REMAP_L2, MT_HIF_REMAP_L2_MASK, base);
	/* use read to push write */
	mt76_rr(dev, MT_HIF_REMAP_L2);

	return MT_HIF_REMAP_BASE_L2 + offset;
}

static u32 __mt7915_reg_addr(struct mt7915_dev *dev, u32 addr)
{
	static const struct {
		u32 phys;
		u32 mapped;
		u32 size;
	} fixed_map[] = {
		{ 0x00400000, 0x80000, 0x10000 }, /* WF_MCU_SYSRAM */
		{ 0x00410000, 0x90000, 0x10000 }, /* WF_MCU_SYSRAM (configure regs) */
		{ 0x40000000, 0x70000, 0x10000 }, /* WF_UMAC_SYSRAM */
		{ 0x54000000, 0x02000, 0x1000 }, /* WFDMA PCIE0 MCU DMA0 */
		{ 0x55000000, 0x03000, 0x1000 }, /* WFDMA PCIE0 MCU DMA1 */
		{ 0x58000000, 0x06000, 0x1000 }, /* WFDMA PCIE1 MCU DMA0 (MEM_DMA) */
		{ 0x59000000, 0x07000, 0x1000 }, /* WFDMA PCIE1 MCU DMA1 */
		{ 0x7c000000, 0xf0000, 0x10000 }, /* CONN_INFRA */
		{ 0x7c020000, 0xd0000, 0x10000 }, /* CONN_INFRA, WFDMA */
		{ 0x80020000, 0xb0000, 0x10000 }, /* WF_TOP_MISC_OFF */
		{ 0x81020000, 0xc0000, 0x10000 }, /* WF_TOP_MISC_ON */
		{ 0x820c0000, 0x08000, 0x4000 }, /* WF_UMAC_TOP (PLE) */
		{ 0x820c8000, 0x0c000, 0x2000 }, /* WF_UMAC_TOP (PSE) */
		{ 0x820cc000, 0x0e000, 0x2000 }, /* WF_UMAC_TOP (PP) */
		{ 0x820ce000, 0x21c00, 0x0200 }, /* WF_LMAC_TOP (WF_SEC) */
		{ 0x820cf000, 0x22000, 0x1000 }, /* WF_LMAC_TOP (WF_PF) */
		{ 0x820d0000, 0x30000, 0x10000 }, /* WF_LMAC_TOP (WF_WTBLON) */
		{ 0x820e0000, 0x20000, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_CFG) */
		{ 0x820e1000, 0x20400, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_TRB) */
		{ 0x820e2000, 0x20800, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_AGG) */
		{ 0x820e3000, 0x20c00, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_ARB) */
		{ 0x820e4000, 0x21000, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_TMAC) */
		{ 0x820e5000, 0x21400, 0x0800 }, /* WF_LMAC_TOP BN0 (WF_RMAC) */
		{ 0x820e7000, 0x21e00, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_DMA) */
		{ 0x820e9000, 0x23400, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_WTBLOFF) */
		{ 0x820ea000, 0x24000, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_ETBF) */
		{ 0x820eb000, 0x24200, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_LPON) */
		{ 0x820ec000, 0x24600, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_INT) */
		{ 0x820ed000, 0x24800, 0x0800 }, /* WF_LMAC_TOP BN0 (WF_MIB) */
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
	};
	int i;

	if (addr < 0x100000)
		return addr;

	for (i = 0; i < ARRAY_SIZE(fixed_map); i++) {
		u32 ofs;

		if (addr < fixed_map[i].phys)
			continue;

		ofs = addr - fixed_map[i].phys;
		if (ofs > fixed_map[i].size)
			continue;

		return fixed_map[i].mapped + ofs;
	}

	if ((addr >= 0x18000000 && addr < 0x18c00000) ||
	    (addr >= 0x70000000 && addr < 0x78000000))
		return mt7915_reg_map_l1(dev, addr);

	return mt7915_reg_map_l2(dev, addr);
}

static u32 mt7915_rr(struct mt76_dev *mdev, u32 offset)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	u32 addr = __mt7915_reg_addr(dev, offset);

	return dev->bus_ops->rr(mdev, addr);
}

static void mt7915_wr(struct mt76_dev *mdev, u32 offset, u32 val)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	u32 addr = __mt7915_reg_addr(dev, offset);

	dev->bus_ops->wr(mdev, addr, val);
}

static u32 mt7915_rmw(struct mt76_dev *mdev, u32 offset, u32 mask, u32 val)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	u32 addr = __mt7915_reg_addr(dev, offset);

	return dev->bus_ops->rmw(mdev, addr, mask, val);
}

static int mt7915_mmio_init(struct mt76_dev *mdev,
			    void __iomem *mem_base,
			    u32 device_id)
{
	struct mt76_bus_ops *bus_ops;
	struct mt7915_dev *dev;

	dev = container_of(mdev, struct mt7915_dev, mt76);
	mt76_mmio_init(&dev->mt76, mem_base);

	dev->bus_ops = dev->mt76.bus;
	bus_ops = devm_kmemdup(dev->mt76.dev, dev->bus_ops, sizeof(*bus_ops),
			       GFP_KERNEL);
	if (!bus_ops)
		return -ENOMEM;

	bus_ops->rr = mt7915_rr;
	bus_ops->wr = mt7915_wr;
	bus_ops->rmw = mt7915_rmw;
	dev->mt76.bus = bus_ops;

	mdev->rev = (mt76_rr(dev, MT_HW_CHIPID) << 16) |
		    (mt76_rr(dev, MT_HW_REV) & 0xff);
	dev_dbg(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	return 0;
}

void mt7915_dual_hif_set_irq_mask(struct mt7915_dev *dev,
				  bool write_reg,
				  u32 clear, u32 set)
{
	struct mt76_dev *mdev = &dev->mt76;
	unsigned long flags;

	spin_lock_irqsave(&mdev->mmio.irq_lock, flags);

	mdev->mmio.irqmask &= ~clear;
	mdev->mmio.irqmask |= set;

	if (write_reg) {
		mt76_wr(dev, MT_INT_MASK_CSR, mdev->mmio.irqmask);
		mt76_wr(dev, MT_INT1_MASK_CSR, mdev->mmio.irqmask);
	}

	spin_unlock_irqrestore(&mdev->mmio.irq_lock, flags);
}

static void mt7915_rx_poll_complete(struct mt76_dev *mdev,
				    enum mt76_rxq_id q)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	static const u32 rx_irq_mask[] = {
		[MT_RXQ_MAIN] = MT_INT_RX_DONE_DATA0,
		[MT_RXQ_EXT] = MT_INT_RX_DONE_DATA1,
		[MT_RXQ_MCU] = MT_INT_RX_DONE_WM,
		[MT_RXQ_MCU_WA] = MT_INT_RX_DONE_WA,
		[MT_RXQ_EXT_WA] = MT_INT_RX_DONE_WA_EXT,
	};

	mt7915_irq_enable(dev, rx_irq_mask[q]);
}

/* TODO: support 2/4/6/8 MSI-X vectors */
static void mt7915_irq_tasklet(struct tasklet_struct *t)
{
	struct mt7915_dev *dev = from_tasklet(dev, t, irq_tasklet);
	u32 intr, intr1, mask;

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

	trace_dev_irq(&dev->mt76, intr, dev->mt76.mmio.irqmask);

	mask = intr & MT_INT_RX_DONE_ALL;
	if (intr & MT_INT_TX_DONE_MCU)
		mask |= MT_INT_TX_DONE_MCU;

	mt7915_irq_disable(dev, mask);

	if (intr & MT_INT_TX_DONE_MCU)
		napi_schedule(&dev->mt76.tx_napi);

	if (intr & MT_INT_RX_DONE_DATA0)
		napi_schedule(&dev->mt76.napi[MT_RXQ_MAIN]);

	if (intr & MT_INT_RX_DONE_DATA1)
		napi_schedule(&dev->mt76.napi[MT_RXQ_EXT]);

	if (intr & MT_INT_RX_DONE_WM)
		napi_schedule(&dev->mt76.napi[MT_RXQ_MCU]);

	if (intr & MT_INT_RX_DONE_WA)
		napi_schedule(&dev->mt76.napi[MT_RXQ_MCU_WA]);

	if (intr & MT_INT_RX_DONE_WA_EXT)
		napi_schedule(&dev->mt76.napi[MT_RXQ_EXT_WA]);

	if (intr & MT_INT_MCU_CMD) {
		u32 val = mt76_rr(dev, MT_MCU_CMD);

		mt76_wr(dev, MT_MCU_CMD, val);
		if (val & MT_MCU_CMD_ERROR_MASK) {
			dev->reset_state = val;
			ieee80211_queue_work(mt76_hw(dev), &dev->reset_work);
			wake_up(&dev->reset_wait);
		}
	}
}

static irqreturn_t mt7915_irq_handler(int irq, void *dev_instance)
{
	struct mt7915_dev *dev = dev_instance;

	mt76_wr(dev, MT_INT_MASK_CSR, 0);
	if (dev->hif2)
		mt76_wr(dev, MT_INT1_MASK_CSR, 0);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return IRQ_NONE;

	tasklet_schedule(&dev->irq_tasklet);

	return IRQ_HANDLED;
}

int mt7915_mmio_probe(struct device *pdev,
		      void __iomem *mem_base,
		      u32 device_id,
		      int irq, struct mt7915_hif *hif2)
{
	static const struct mt76_driver_ops drv_ops = {
		/* txwi_size = txd size + txp size */
		.txwi_size = MT_TXD_SIZE + sizeof(struct mt7915_txp),
		.drv_flags = MT_DRV_TXWI_NO_FREE | MT_DRV_HW_MGMT_TXQ,
		.survey_flags = SURVEY_INFO_TIME_TX |
				SURVEY_INFO_TIME_RX |
				SURVEY_INFO_TIME_BSS_RX,
		.token_size = MT7915_TOKEN_SIZE,
		.tx_prepare_skb = mt7915_tx_prepare_skb,
		.tx_complete_skb = mt7915_tx_complete_skb,
		.rx_skb = mt7915_queue_rx_skb,
		.rx_check = mt7915_rx_check,
		.rx_poll_complete = mt7915_rx_poll_complete,
		.sta_ps = mt7915_sta_ps,
		.sta_add = mt7915_mac_sta_add,
		.sta_remove = mt7915_mac_sta_remove,
		.update_survey = mt7915_update_channel,
	};
	struct ieee80211_ops *ops;
	struct mt7915_dev *dev;
	struct mt76_dev *mdev;
	int ret;

	ops = devm_kmemdup(pdev, &mt7915_ops, sizeof(mt7915_ops), GFP_KERNEL);
	if (!ops)
		return -ENOMEM;

	mdev = mt76_alloc_device(pdev, sizeof(*dev), ops, &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt7915_dev, mt76);

	ret = mt7915_mmio_init(mdev, mem_base, device_id);
	if (ret)
		goto error;

	tasklet_setup(&dev->irq_tasklet, mt7915_irq_tasklet);

	mt76_wr(dev, MT_INT_MASK_CSR, 0);
	/* master switch of PCIe tnterrupt enable */
	if (dev_is_pci(pdev))
		mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);

	ret = devm_request_irq(mdev->dev, irq, mt7915_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto error;

	if (hif2) {
		dev->hif2 = hif2;

		mt76_wr(dev, MT_INT1_MASK_CSR, 0);
		/* master switch of PCIe tnterrupt enable */
		if (dev_is_pci(pdev))
			mt76_wr(dev, MT_PCIE1_MAC_INT_ENABLE, 0xff);

		ret = devm_request_irq(mdev->dev, dev->hif2->irq,
				       mt7915_irq_handler, IRQF_SHARED,
				       KBUILD_MODNAME "-hif", dev);
		if (ret) {
			put_device(dev->hif2->dev);
			goto free_irq;
		}
	}

	ret = mt7915_register_device(dev);
	if (ret)
		goto free_hif2_irq;

	return 0;

free_hif2_irq:
	if (dev->hif2)
		devm_free_irq(mdev->dev, dev->hif2->irq, dev);
free_irq:
	devm_free_irq(mdev->dev, irq, dev);
error:
	mt76_free_device(&dev->mt76);

	return ret;
}

static int __init mt7915_init(void)
{
	int ret;

	ret = pci_register_driver(&mt7915_hif_driver);
	if (ret)
		return ret;

	ret = pci_register_driver(&mt7915_pci_driver);
	if (ret)
		pci_unregister_driver(&mt7915_hif_driver);

	return ret;
}

static void __exit mt7915_exit(void)
{
	pci_unregister_driver(&mt7915_pci_driver);
	pci_unregister_driver(&mt7915_hif_driver);
}

module_init(mt7915_init);
module_exit(mt7915_exit);
MODULE_LICENSE("Dual BSD/GPL");
