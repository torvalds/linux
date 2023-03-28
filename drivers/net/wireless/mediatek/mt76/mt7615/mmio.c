// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pci.h>

#include "mt7615.h"
#include "regs.h"
#include "mac.h"
#include "../trace.h"

const u32 mt7615e_reg_map[] = {
	[MT_TOP_CFG_BASE]	= 0x01000,
	[MT_HW_BASE]		= 0x01000,
	[MT_PCIE_REMAP_2]	= 0x02504,
	[MT_ARB_BASE]		= 0x20c00,
	[MT_HIF_BASE]		= 0x04000,
	[MT_CSR_BASE]		= 0x07000,
	[MT_PLE_BASE]		= 0x08000,
	[MT_PSE_BASE]		= 0x0c000,
	[MT_CFG_BASE]		= 0x20200,
	[MT_AGG_BASE]		= 0x20a00,
	[MT_TMAC_BASE]		= 0x21000,
	[MT_RMAC_BASE]		= 0x21200,
	[MT_DMA_BASE]		= 0x21800,
	[MT_PF_BASE]		= 0x22000,
	[MT_WTBL_BASE_ON]	= 0x23000,
	[MT_WTBL_BASE_OFF]	= 0x23400,
	[MT_LPON_BASE]		= 0x24200,
	[MT_MIB_BASE]		= 0x24800,
	[MT_WTBL_BASE_ADDR]	= 0x30000,
	[MT_PCIE_REMAP_BASE2]	= 0x80000,
	[MT_TOP_MISC_BASE]	= 0xc0000,
	[MT_EFUSE_ADDR_BASE]	= 0x81070000,
};

const u32 mt7663e_reg_map[] = {
	[MT_TOP_CFG_BASE]	= 0x01000,
	[MT_HW_BASE]		= 0x02000,
	[MT_DMA_SHDL_BASE]	= 0x06000,
	[MT_PCIE_REMAP_2]	= 0x0700c,
	[MT_ARB_BASE]		= 0x20c00,
	[MT_HIF_BASE]		= 0x04000,
	[MT_CSR_BASE]		= 0x07000,
	[MT_PLE_BASE]		= 0x08000,
	[MT_PSE_BASE]		= 0x0c000,
	[MT_PP_BASE]            = 0x0e000,
	[MT_CFG_BASE]		= 0x20000,
	[MT_AGG_BASE]		= 0x22000,
	[MT_TMAC_BASE]		= 0x24000,
	[MT_RMAC_BASE]		= 0x25000,
	[MT_DMA_BASE]		= 0x27000,
	[MT_PF_BASE]		= 0x28000,
	[MT_WTBL_BASE_ON]	= 0x29000,
	[MT_WTBL_BASE_OFF]	= 0x29800,
	[MT_LPON_BASE]		= 0x2b000,
	[MT_MIB_BASE]		= 0x2d000,
	[MT_WTBL_BASE_ADDR]	= 0x30000,
	[MT_PCIE_REMAP_BASE2]	= 0x90000,
	[MT_TOP_MISC_BASE]	= 0xc0000,
	[MT_EFUSE_ADDR_BASE]	= 0x78011000,
};

static void
mt7615_rx_poll_complete(struct mt76_dev *mdev, enum mt76_rxq_id q)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);

	mt7615_irq_enable(dev, MT_INT_RX_DONE(q));
}

static irqreturn_t mt7615_irq_handler(int irq, void *dev_instance)
{
	struct mt7615_dev *dev = dev_instance;

	mt76_wr(dev, MT_INT_MASK_CSR, 0);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return IRQ_NONE;

	tasklet_schedule(&dev->irq_tasklet);

	return IRQ_HANDLED;
}

static void mt7615_irq_tasklet(struct tasklet_struct *t)
{
	struct mt7615_dev *dev = from_tasklet(dev, t, irq_tasklet);
	u32 intr, mask = 0, tx_mcu_mask = mt7615_tx_mcu_int_mask(dev);
	u32 mcu_int;

	mt76_wr(dev, MT_INT_MASK_CSR, 0);

	intr = mt76_rr(dev, MT_INT_SOURCE_CSR);
	intr &= dev->mt76.mmio.irqmask;
	mt76_wr(dev, MT_INT_SOURCE_CSR, intr);

	trace_dev_irq(&dev->mt76, intr, dev->mt76.mmio.irqmask);

	mask |= intr & MT_INT_RX_DONE_ALL;
	if (intr & tx_mcu_mask)
		mask |= tx_mcu_mask;
	mt76_set_irq_mask(&dev->mt76, MT_INT_MASK_CSR, mask, 0);

	if (intr & tx_mcu_mask)
		napi_schedule(&dev->mt76.tx_napi);

	if (intr & MT_INT_RX_DONE(0))
		napi_schedule(&dev->mt76.napi[0]);

	if (intr & MT_INT_RX_DONE(1))
		napi_schedule(&dev->mt76.napi[1]);

	if (!(intr & (MT_INT_MCU_CMD | MT7663_INT_MCU_CMD)))
		return;

	if (is_mt7663(&dev->mt76)) {
		mcu_int = mt76_rr(dev, MT_MCU2HOST_INT_STATUS);
		mcu_int &= MT7663_MCU_CMD_ERROR_MASK;
		mt76_wr(dev, MT_MCU2HOST_INT_STATUS, mcu_int);
	} else {
		mcu_int = mt76_rr(dev, MT_MCU_CMD);
		mcu_int &= MT_MCU_CMD_ERROR_MASK;
	}

	if (!mcu_int)
		return;

	dev->reset_state = mcu_int;
	queue_work(dev->mt76.wq, &dev->reset_work);
	wake_up(&dev->reset_wait);
}

static u32 __mt7615_reg_addr(struct mt7615_dev *dev, u32 addr)
{
	if (addr < 0x100000)
		return addr;

	return mt7615_reg_map(dev, addr);
}

static u32 mt7615_rr(struct mt76_dev *mdev, u32 offset)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	u32 addr = __mt7615_reg_addr(dev, offset);

	return dev->bus_ops->rr(mdev, addr);
}

static void mt7615_wr(struct mt76_dev *mdev, u32 offset, u32 val)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	u32 addr = __mt7615_reg_addr(dev, offset);

	dev->bus_ops->wr(mdev, addr, val);
}

static u32 mt7615_rmw(struct mt76_dev *mdev, u32 offset, u32 mask, u32 val)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	u32 addr = __mt7615_reg_addr(dev, offset);

	return dev->bus_ops->rmw(mdev, addr, mask, val);
}

int mt7615_mmio_probe(struct device *pdev, void __iomem *mem_base,
		      int irq, const u32 *map)
{
	static const struct mt76_driver_ops drv_ops = {
		/* txwi_size = txd size + txp size */
		.txwi_size = MT_TXD_SIZE + sizeof(struct mt76_connac_txp_common),
		.drv_flags = MT_DRV_TXWI_NO_FREE | MT_DRV_HW_MGMT_TXQ,
		.survey_flags = SURVEY_INFO_TIME_TX |
				SURVEY_INFO_TIME_RX |
				SURVEY_INFO_TIME_BSS_RX,
		.token_size = MT7615_TOKEN_SIZE,
		.tx_prepare_skb = mt7615_tx_prepare_skb,
		.tx_complete_skb = mt76_connac_tx_complete_skb,
		.rx_check = mt7615_rx_check,
		.rx_skb = mt7615_queue_rx_skb,
		.rx_poll_complete = mt7615_rx_poll_complete,
		.sta_ps = mt7615_sta_ps,
		.sta_add = mt7615_mac_sta_add,
		.sta_remove = mt7615_mac_sta_remove,
		.update_survey = mt7615_update_channel,
	};
	struct mt76_bus_ops *bus_ops;
	struct ieee80211_ops *ops;
	struct mt7615_dev *dev;
	struct mt76_dev *mdev;
	int ret;

	ops = devm_kmemdup(pdev, &mt7615_ops, sizeof(mt7615_ops), GFP_KERNEL);
	if (!ops)
		return -ENOMEM;

	mdev = mt76_alloc_device(pdev, sizeof(*dev), ops, &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt7615_dev, mt76);
	mt76_mmio_init(&dev->mt76, mem_base);
	tasklet_setup(&dev->irq_tasklet, mt7615_irq_tasklet);

	dev->reg_map = map;
	dev->ops = ops;
	mdev->rev = (mt76_rr(dev, MT_HW_CHIPID) << 16) |
		    (mt76_rr(dev, MT_HW_REV) & 0xff);
	dev_dbg(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	dev->bus_ops = dev->mt76.bus;
	bus_ops = devm_kmemdup(dev->mt76.dev, dev->bus_ops, sizeof(*bus_ops),
			       GFP_KERNEL);
	if (!bus_ops) {
		ret = -ENOMEM;
		goto err_free_dev;
	}

	bus_ops->rr = mt7615_rr;
	bus_ops->wr = mt7615_wr;
	bus_ops->rmw = mt7615_rmw;
	dev->mt76.bus = bus_ops;

	mt76_wr(dev, MT_INT_MASK_CSR, 0);

	ret = devm_request_irq(mdev->dev, irq, mt7615_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto err_free_dev;

	if (is_mt7663(mdev))
		mt76_wr(dev, MT_PCIE_IRQ_ENABLE, 1);

	ret = mt7615_register_device(dev);
	if (ret)
		goto err_free_irq;

	return 0;

err_free_irq:
	devm_free_irq(pdev, irq, dev);
err_free_dev:
	mt76_free_device(&dev->mt76);

	return ret;
}

static int __init mt7615_init(void)
{
	int ret;

	ret = pci_register_driver(&mt7615_pci_driver);
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_MT7622_WMAC)) {
		ret = platform_driver_register(&mt7622_wmac_driver);
		if (ret)
			pci_unregister_driver(&mt7615_pci_driver);
	}

	return ret;
}

static void __exit mt7615_exit(void)
{
	if (IS_ENABLED(CONFIG_MT7622_WMAC))
		platform_driver_unregister(&mt7622_wmac_driver);
	pci_unregister_driver(&mt7615_pci_driver);
}

module_init(mt7615_init);
module_exit(mt7615_exit);
MODULE_LICENSE("Dual BSD/GPL");
