/* SPDX-License-Identifier: ISC */

#include "mt7603.h"

void mt7603_rx_poll_complete(struct mt76_dev *mdev, enum mt76_rxq_id q)
{
	struct mt7603_dev *dev = container_of(mdev, struct mt7603_dev, mt76);

	mt7603_irq_enable(dev, MT_INT_RX_DONE(q));
}

irqreturn_t mt7603_irq_handler(int irq, void *dev_instance)
{
	struct mt7603_dev *dev = dev_instance;
	u32 intr;

	intr = mt76_rr(dev, MT_INT_SOURCE_CSR);
	mt76_wr(dev, MT_INT_SOURCE_CSR, intr);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mt76.state))
		return IRQ_NONE;

	intr &= dev->mt76.mmio.irqmask;

	if (intr & MT_INT_MAC_IRQ3) {
		u32 hwintr = mt76_rr(dev, MT_HW_INT_STATUS(3));

		mt76_wr(dev, MT_HW_INT_STATUS(3), hwintr);
		if (hwintr & MT_HW_INT3_PRE_TBTT0)
			tasklet_schedule(&dev->mt76.pre_tbtt_tasklet);

		if ((hwintr & MT_HW_INT3_TBTT0) && dev->mt76.csa_complete)
			mt76_csa_finish(&dev->mt76);
	}

	if (intr & MT_INT_TX_DONE_ALL) {
		mt7603_irq_disable(dev, MT_INT_TX_DONE_ALL);
		tasklet_schedule(&dev->mt76.tx_tasklet);
	}

	if (intr & MT_INT_RX_DONE(0)) {
		mt7603_irq_disable(dev, MT_INT_RX_DONE(0));
		napi_schedule(&dev->mt76.napi[0]);
	}

	if (intr & MT_INT_RX_DONE(1)) {
		mt7603_irq_disable(dev, MT_INT_RX_DONE(1));
		napi_schedule(&dev->mt76.napi[1]);
	}

	return IRQ_HANDLED;
}

u32 mt7603_reg_map(struct mt7603_dev *dev, u32 addr)
{
	u32 base = addr & MT_MCU_PCIE_REMAP_2_BASE;
	u32 offset = addr & MT_MCU_PCIE_REMAP_2_OFFSET;

	dev->bus_ops->wr(&dev->mt76, MT_MCU_PCIE_REMAP_2, base);

	return MT_PCIE_REMAP_BASE_2 + offset;
}
