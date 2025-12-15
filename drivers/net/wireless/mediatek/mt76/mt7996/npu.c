// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 AIROHA Inc
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 */
#include <linux/kernel.h>
#include <linux/soc/airoha/airoha_offload.h>

#include "mt7996.h"

static int mt7996_npu_offload_init(struct mt7996_dev *dev,
				   struct airoha_npu *npu)
{
	phys_addr_t phy_addr = dev->mt76.mmio.phy_addr;
	u32 val, hif1_ofs = 0, dma_addr;
	int i, err;

	err = mt76_npu_get_msg(npu, 0, WLAN_FUNC_GET_WAIT_NPU_VERSION,
			       &val, GFP_KERNEL);
	if (err) {
		dev_warn(dev->mt76.dev, "failed getting NPU fw version\n");
		return err;
	}

	dev_info(dev->mt76.dev, "NPU version: %0d.%d\n",
		 (val >> 16) & 0xffff, val & 0xffff);

	err = mt76_npu_send_msg(npu, 0, WLAN_FUNC_SET_WAIT_PCIE_PORT_TYPE,
				dev->mt76.mmio.npu_type, GFP_KERNEL);
	if (err) {
		dev_warn(dev->mt76.dev,
			 "failed setting NPU wlan PCIe port type\n");
		return err;
	}

	if (dev->hif2)
		hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

	for (i = MT_BAND0; i < MT_BAND2; i++) {
		dma_addr = phy_addr;
		if (i)
			dma_addr += MT_RXQ_RING_BASE(MT_RXQ_RRO_BAND1) + 0x90 +
				    hif1_ofs;
		else
			dma_addr += MT_RXQ_RING_BASE(MT_RXQ_RRO_BAND0) + 0x80;

		err = mt76_npu_send_msg(npu, i, WLAN_FUNC_SET_WAIT_PCIE_ADDR,
					dma_addr, GFP_KERNEL);
		if (err) {
			dev_warn(dev->mt76.dev,
				 "failed setting NPU wlan PCIe desc addr\n");
			return err;
		}

		err = mt76_npu_send_msg(npu, i, WLAN_FUNC_SET_WAIT_DESC,
					MT7996_RX_RING_SIZE, GFP_KERNEL);
		if (err) {
			dev_warn(dev->mt76.dev,
				 "failed setting NPU wlan PCIe desc size\n");
			return err;
		}

		dma_addr = phy_addr;
		if (i)
			dma_addr += MT_TXQ_RING_BASE(0) + 0x150 + hif1_ofs;
		else
			dma_addr += MT_TXQ_RING_BASE(0) + 0x120;

		err = mt76_npu_send_msg(npu, i,
					WLAN_FUNC_SET_WAIT_TX_RING_PCIE_ADDR,
					dma_addr, GFP_KERNEL);
		if (err) {
			dev_warn(dev->mt76.dev,
				 "failed setting NPU wlan tx desc addr\n");
			return err;
		}
	}

	err = mt76_npu_send_msg(npu, 9, WLAN_FUNC_SET_WAIT_PCIE_ADDR,
				phy_addr + MT_RXQ_RRO_AP_RING_BASE,
				GFP_KERNEL);
	if (err) {
		dev_warn(dev->mt76.dev,
			 "failed setting NPU wlan rxdmad_c addr\n");
		return err;
	}

	err = mt76_npu_send_msg(npu, 9, WLAN_FUNC_SET_WAIT_DESC,
				MT7996_RX_RING_SIZE, GFP_KERNEL);
	if (err) {
		dev_warn(dev->mt76.dev,
			 "failed setting NPU wlan rxdmad_c desc size\n");
		return err;
	}

	err = mt76_npu_send_msg(npu, 2, WLAN_FUNC_SET_WAIT_TX_RING_PCIE_ADDR,
				phy_addr + MT_RRO_ACK_SN_CTRL, GFP_KERNEL);
	if (err) {
		dev_warn(dev->mt76.dev,
			 "failed setting NPU wlan rro_ack_sn desc addr\n");
		return err;
	}

	err = mt76_npu_send_msg(npu, 0, WLAN_FUNC_SET_WAIT_TOKEN_ID_SIZE,
				MT7996_HW_TOKEN_SIZE, GFP_KERNEL);
	if (err)
		return err;

	dev->mt76.token_start = MT7996_HW_TOKEN_SIZE;

	return 0;
}

static int mt7996_npu_rxd_init(struct mt7996_dev *dev, struct airoha_npu *npu)
{
	u32 val;
	int err;

	err = mt76_npu_get_msg(npu, 0, WLAN_FUNC_GET_WAIT_RXDESC_BASE,
			       &val, GFP_KERNEL);
	if (err) {
		dev_warn(dev->mt76.dev,
			 "failed retriving NPU wlan rx ring0 addr\n");
		return err;
	}
	writel(val, &dev->mt76.q_rx[MT_RXQ_RRO_BAND0].regs->desc_base);

	err = mt76_npu_get_msg(npu, 1, WLAN_FUNC_GET_WAIT_RXDESC_BASE,
			       &val, GFP_KERNEL);
	if (err) {
		dev_warn(dev->mt76.dev,
			 "failed retriving NPU wlan rx ring1 addr\n");
		return err;
	}
	writel(val, &dev->mt76.q_rx[MT_RXQ_RRO_BAND1].regs->desc_base);

	err = mt76_npu_get_msg(npu, 9, WLAN_FUNC_GET_WAIT_RXDESC_BASE,
			       &val, GFP_KERNEL);
	if (err) {
		dev_warn(dev->mt76.dev,
			 "failed retriving NPU wlan rxdmad_c ring addr\n");
		return err;
	}
	writel(val, &dev->mt76.q_rx[MT_RXQ_RRO_RXDMAD_C].regs->desc_base);

	return 0;
}

static int mt7996_npu_txd_init(struct mt7996_dev *dev, struct airoha_npu *npu)
{
	int i, err;

	for (i = MT_BAND0; i < MT_BAND2; i++) {
		dma_addr_t dma_addr;
		u32 val;

		err = mt76_npu_get_msg(npu, i + 5,
				       WLAN_FUNC_GET_WAIT_RXDESC_BASE,
				       &val, GFP_KERNEL);
		if (err) {
			dev_warn(dev->mt76.dev,
				 "failed retriving NPU wlan tx ring addr\n");
			return err;
		}
		writel(val, &dev->mt76.phys[i]->q_tx[0]->regs->desc_base);

		if (!dmam_alloc_coherent(dev->mt76.dma_dev,
					 256 * MT7996_TX_RING_SIZE,
					 &dma_addr, GFP_KERNEL))
			return -ENOMEM;

		err = mt76_npu_send_msg(npu, i,
					WLAN_FUNC_SET_WAIT_TX_BUF_SPACE_HW_BASE,
					dma_addr, GFP_KERNEL);
		if (err) {
			dev_warn(dev->mt76.dev,
				 "failed setting NPU wlan queue buf addr\n");
			return err;
		}

		if (!dmam_alloc_coherent(dev->mt76.dma_dev,
					 256 * MT7996_TX_RING_SIZE,
					 &dma_addr, GFP_KERNEL))
			return -ENOMEM;

		err = mt76_npu_send_msg(npu, i + 5,
					WLAN_FUNC_SET_WAIT_TX_BUF_SPACE_HW_BASE,
					dma_addr, GFP_KERNEL);
		if (err) {
			dev_warn(dev->mt76.dev,
				 "failed setting NPU wlan tx buf addr\n");
			return err;
		}

		if (!dmam_alloc_coherent(dev->mt76.dma_dev, 256 * 1024,
					 &dma_addr, GFP_KERNEL))
			return -ENOMEM;

		err = mt76_npu_send_msg(npu, i + 10,
					WLAN_FUNC_SET_WAIT_TX_BUF_SPACE_HW_BASE,
					dma_addr, GFP_KERNEL);
		if (err) {
			dev_warn(dev->mt76.dev,
				 "failed setting NPU wlan tx buf base\n");
			return err;
		}
	}

	return 0;
}

static int mt7996_npu_rx_event_init(struct mt7996_dev *dev,
				    struct airoha_npu *npu)
{
	struct mt76_queue *q = &dev->mt76.q_rx[MT_RXQ_MAIN_WA];
	phys_addr_t phy_addr = dev->mt76.mmio.phy_addr;
	int err;

	err = mt76_npu_send_msg(npu, 0,
				WLAN_FUNC_SET_WAIT_RX_RING_FOR_TXDONE_HW_BASE,
				q->desc_dma, GFP_KERNEL);
	if (err) {
		dev_warn(dev->mt76.dev,
			 "failed setting NPU wlan tx-done ring\n");
		return err;
	}

	err = mt76_npu_send_msg(npu, 10, WLAN_FUNC_SET_WAIT_DESC,
				MT7996_RX_MCU_RING_SIZE, GFP_KERNEL);
	if (err) {
		dev_warn(dev->mt76.dev,
			 "failed setting NPU wlan descriptors\n");
		return err;
	}

	phy_addr += MT_RXQ_RING_BASE(MT_RXQ_MAIN_WA) + 0x20;
	err = mt76_npu_send_msg(npu, 10, WLAN_FUNC_SET_WAIT_PCIE_ADDR,
				phy_addr, GFP_KERNEL);
	if (err)
		dev_warn(dev->mt76.dev,
			 "failed setting NPU wlan rx pcie address\n");
	return err;
}

static int mt7996_npu_tx_done_init(struct mt7996_dev *dev,
				   struct airoha_npu *npu)
{
	int err;

	err = mt76_npu_send_msg(npu, 2, WLAN_FUNC_SET_WAIT_INODE_TXRX_REG_ADDR,
				0, GFP_KERNEL);
	if (err) {
		dev_warn(dev->mt76.dev, "failed setting NPU wlan txrx addr2\n");
		return err;
	}

	err = mt76_npu_send_msg(npu, 7, WLAN_FUNC_SET_WAIT_INODE_TXRX_REG_ADDR,
				0, GFP_KERNEL);
	if (err)
		dev_warn(dev->mt76.dev, "failed setting NPU wlan txrx addr7\n");

	return err;
}

int mt7996_npu_rx_queues_init(struct mt7996_dev *dev)
{
	int err;

	if (!mt76_npu_device_active(&dev->mt76))
		return 0;

	err = mt76_npu_rx_queue_init(&dev->mt76,
				     &dev->mt76.q_rx[MT_RXQ_NPU0]);
	if (err)
		return err;

	return mt76_npu_rx_queue_init(&dev->mt76,
				      &dev->mt76.q_rx[MT_RXQ_NPU1]);
}

int mt7996_npu_hw_init(struct mt7996_dev *dev)
{
	struct airoha_npu *npu;
	int i, err = 0;

	mutex_lock(&dev->mt76.mutex);

	npu = rcu_dereference_protected(dev->mt76.mmio.npu, &dev->mt76.mutex);
	if (!npu)
		goto unlock;

	err = mt7996_npu_offload_init(dev, npu);
	if (err)
		goto unlock;

	err = mt7996_npu_rxd_init(dev, npu);
	if (err)
		goto unlock;

	err = mt7996_npu_txd_init(dev, npu);
	if (err)
		goto unlock;

	err = mt7996_npu_rx_event_init(dev, npu);
	if (err)
		goto unlock;

	err = mt7996_npu_tx_done_init(dev, npu);
	if (err)
		goto unlock;

	for (i = MT_RXQ_NPU0; i <= MT_RXQ_NPU1; i++)
		airoha_npu_wlan_enable_irq(npu, i - MT_RXQ_NPU0);
unlock:
	mutex_unlock(&dev->mt76.mutex);

	return err;
}

int mt7996_npu_hw_stop(struct mt7996_dev *dev)
{
	struct airoha_npu *npu;
	int i, err;
	u32 info;

	npu = rcu_dereference_protected(dev->mt76.mmio.npu, &dev->mt76.mutex);
	if (!npu)
		return 0;

	err = mt76_npu_send_msg(npu, 4, WLAN_FUNC_SET_WAIT_INODE_TXRX_REG_ADDR,
				0, GFP_KERNEL);
	if (err)
		return err;

	for (i = 0; i < 10; i++) {
		err = mt76_npu_get_msg(npu, 3, WLAN_FUNC_GET_WAIT_NPU_INFO,
				       &info, GFP_KERNEL);
		if (err)
			continue;

		if (info) {
			err = -ETIMEDOUT;
			continue;
		}
	}

	if (!err)
		err = mt76_npu_send_msg(npu, 6,
					WLAN_FUNC_SET_WAIT_INODE_TXRX_REG_ADDR,
					0, GFP_KERNEL);
	return err;
}
