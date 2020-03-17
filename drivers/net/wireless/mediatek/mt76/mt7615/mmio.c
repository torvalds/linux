#include <linux/kernel.h>
#include <linux/module.h>

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
	[MT_PHY_BASE]		= 0x10000,
	[MT_CFG_BASE]		= 0x20200,
	[MT_AGG_BASE]		= 0x20a00,
	[MT_TMAC_BASE]		= 0x21000,
	[MT_RMAC_BASE]		= 0x21200,
	[MT_DMA_BASE]		= 0x21800,
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
	[MT_PHY_BASE]		= 0x10000,
	[MT_CFG_BASE]		= 0x20000,
	[MT_AGG_BASE]		= 0x22000,
	[MT_TMAC_BASE]		= 0x24000,
	[MT_RMAC_BASE]		= 0x25000,
	[MT_DMA_BASE]		= 0x27000,
	[MT_WTBL_BASE_ON]	= 0x29000,
	[MT_WTBL_BASE_OFF]	= 0x29800,
	[MT_LPON_BASE]		= 0x2b000,
	[MT_MIB_BASE]		= 0x2d000,
	[MT_WTBL_BASE_ADDR]	= 0x30000,
	[MT_PCIE_REMAP_BASE2]	= 0x90000,
	[MT_TOP_MISC_BASE]	= 0xc0000,
	[MT_EFUSE_ADDR_BASE]	= 0x78011000,
};

u32 mt7615_reg_map(struct mt7615_dev *dev, u32 addr)
{
	u32 base, offset;

	if (is_mt7663(&dev->mt76)) {
		base = addr & MT7663_MCU_PCIE_REMAP_2_BASE;
		offset = addr & MT7663_MCU_PCIE_REMAP_2_OFFSET;
	} else {
		base = addr & MT_MCU_PCIE_REMAP_2_BASE;
		offset = addr & MT_MCU_PCIE_REMAP_2_OFFSET;
	}
	mt76_wr(dev, MT_MCU_PCIE_REMAP_2, base);

	return MT_PCIE_REMAP_BASE_2 + offset;
}

static void
mt7615_rx_poll_complete(struct mt76_dev *mdev, enum mt76_rxq_id q)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);

	mt7615_irq_enable(dev, MT_INT_RX_DONE(q));
}

static irqreturn_t mt7615_irq_handler(int irq, void *dev_instance)
{
	struct mt7615_dev *dev = dev_instance;
	u32 intr;

	intr = mt76_rr(dev, MT_INT_SOURCE_CSR);
	mt76_wr(dev, MT_INT_SOURCE_CSR, intr);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return IRQ_NONE;

	trace_dev_irq(&dev->mt76, intr, dev->mt76.mmio.irqmask);

	intr &= dev->mt76.mmio.irqmask;

	if (intr & MT_INT_TX_DONE_ALL) {
		mt7615_irq_disable(dev, MT_INT_TX_DONE_ALL);
		napi_schedule(&dev->mt76.tx_napi);
	}

	if (intr & MT_INT_RX_DONE(0)) {
		mt7615_irq_disable(dev, MT_INT_RX_DONE(0));
		napi_schedule(&dev->mt76.napi[0]);
	}

	if (intr & MT_INT_RX_DONE(1)) {
		mt7615_irq_disable(dev, MT_INT_RX_DONE(1));
		napi_schedule(&dev->mt76.napi[1]);
	}

	if (intr & MT_INT_MCU_CMD) {
		u32 val = mt76_rr(dev, MT_MCU_CMD);

		if (val & MT_MCU_CMD_ERROR_MASK) {
			dev->reset_state = val;
			ieee80211_queue_work(mt76_hw(dev), &dev->reset_work);
			wake_up(&dev->reset_wait);
		}
	}

	return IRQ_HANDLED;
}

int mt7615_mmio_probe(struct device *pdev, void __iomem *mem_base,
		      int irq, const u32 *map)
{
	static const struct mt76_driver_ops drv_ops = {
		/* txwi_size = txd size + txp size */
		.txwi_size = MT_TXD_SIZE + sizeof(struct mt7615_txp_common),
		.drv_flags = MT_DRV_TXWI_NO_FREE,
		.survey_flags = SURVEY_INFO_TIME_TX |
				SURVEY_INFO_TIME_RX |
				SURVEY_INFO_TIME_BSS_RX,
		.tx_prepare_skb = mt7615_tx_prepare_skb,
		.tx_complete_skb = mt7615_tx_complete_skb,
		.rx_skb = mt7615_queue_rx_skb,
		.rx_poll_complete = mt7615_rx_poll_complete,
		.sta_ps = mt7615_sta_ps,
		.sta_add = mt7615_mac_sta_add,
		.sta_remove = mt7615_mac_sta_remove,
		.update_survey = mt7615_update_channel,
	};
	struct mt7615_dev *dev;
	struct mt76_dev *mdev;
	int ret;

	mdev = mt76_alloc_device(pdev, sizeof(*dev), &mt7615_ops, &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt7615_dev, mt76);
	mt76_mmio_init(&dev->mt76, mem_base);

	dev->reg_map = map;
	mdev->rev = (mt76_rr(dev, MT_HW_CHIPID) << 16) |
		    (mt76_rr(dev, MT_HW_REV) & 0xff);
	dev_dbg(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	ret = devm_request_irq(mdev->dev, irq, mt7615_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto error;

	if (is_mt7663(mdev))
		mt76_wr(dev, MT_PCIE_IRQ_ENABLE, 1);

	ret = mt7615_register_device(dev);
	if (ret)
		goto error;

	return 0;
error:
	ieee80211_free_hw(mt76_hw(dev));
	return ret;
}
