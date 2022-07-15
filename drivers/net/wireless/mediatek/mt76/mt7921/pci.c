// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mt7921.h"
#include "mac.h"
#include "mcu.h"
#include "../trace.h"

static const struct pci_device_id mt7921_pci_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7961) },
	{ },
};

static void
mt7921_rx_poll_complete(struct mt76_dev *mdev, enum mt76_rxq_id q)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);

	if (q == MT_RXQ_MAIN)
		mt7921_irq_enable(dev, MT_INT_RX_DONE_DATA);
	else if (q == MT_RXQ_MCU_WA)
		mt7921_irq_enable(dev, MT_INT_RX_DONE_WM2);
	else
		mt7921_irq_enable(dev, MT_INT_RX_DONE_WM);
}

static irqreturn_t mt7921_irq_handler(int irq, void *dev_instance)
{
	struct mt7921_dev *dev = dev_instance;

	mt76_wr(dev, MT_WFDMA0_HOST_INT_ENA, 0);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return IRQ_NONE;

	tasklet_schedule(&dev->irq_tasklet);

	return IRQ_HANDLED;
}

static void mt7921_irq_tasklet(unsigned long data)
{
	struct mt7921_dev *dev = (struct mt7921_dev *)data;
	u32 intr, mask = 0;

	mt76_wr(dev, MT_WFDMA0_HOST_INT_ENA, 0);

	intr = mt76_rr(dev, MT_WFDMA0_HOST_INT_STA);
	intr &= dev->mt76.mmio.irqmask;
	mt76_wr(dev, MT_WFDMA0_HOST_INT_STA, intr);

	trace_dev_irq(&dev->mt76, intr, dev->mt76.mmio.irqmask);

	mask |= intr & MT_INT_RX_DONE_ALL;
	if (intr & MT_INT_TX_DONE_MCU)
		mask |= MT_INT_TX_DONE_MCU;

	if (intr & MT_INT_MCU_CMD) {
		u32 intr_sw;

		intr_sw = mt76_rr(dev, MT_MCU_CMD);
		/* ack MCU2HOST_SW_INT_STA */
		mt76_wr(dev, MT_MCU_CMD, intr_sw);
		if (intr_sw & MT_MCU_CMD_WAKE_RX_PCIE) {
			mask |= MT_INT_RX_DONE_DATA;
			intr |= MT_INT_RX_DONE_DATA;
		}
	}

	mt76_set_irq_mask(&dev->mt76, MT_WFDMA0_HOST_INT_ENA, mask, 0);

	if (intr & MT_INT_TX_DONE_ALL)
		napi_schedule(&dev->mt76.tx_napi);

	if (intr & MT_INT_RX_DONE_WM)
		napi_schedule(&dev->mt76.napi[MT_RXQ_MCU]);

	if (intr & MT_INT_RX_DONE_WM2)
		napi_schedule(&dev->mt76.napi[MT_RXQ_MCU_WA]);

	if (intr & MT_INT_RX_DONE_DATA)
		napi_schedule(&dev->mt76.napi[MT_RXQ_MAIN]);
}

static u32 __mt7921_reg_addr(struct mt7921_dev *dev, u32 addr)
{
	static const struct {
		u32 phys;
		u32 mapped;
		u32 size;
	} fixed_map[] = {
		{ 0x00400000, 0x80000, 0x10000}, /* WF_MCU_SYSRAM */
		{ 0x00410000, 0x90000, 0x10000}, /* WF_MCU_SYSRAM (configure register) */
		{ 0x40000000, 0x70000, 0x10000}, /* WF_UMAC_SYSRAM */
		{ 0x54000000, 0x02000, 0x1000 }, /* WFDMA PCIE0 MCU DMA0 */
		{ 0x55000000, 0x03000, 0x1000 }, /* WFDMA PCIE0 MCU DMA1 */
		{ 0x58000000, 0x06000, 0x1000 }, /* WFDMA PCIE1 MCU DMA0 (MEM_DMA) */
		{ 0x59000000, 0x07000, 0x1000 }, /* WFDMA PCIE1 MCU DMA1 */
		{ 0x7c000000, 0xf0000, 0x10000 }, /* CONN_INFRA */
		{ 0x7c020000, 0xd0000, 0x10000 }, /* CONN_INFRA, WFDMA */
		{ 0x7c060000, 0xe0000, 0x10000}, /* CONN_INFRA, conn_host_csr_top */
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
	    (addr >= 0x70000000 && addr < 0x78000000) ||
	    (addr >= 0x7c000000 && addr < 0x7c400000))
		return mt7921_reg_map_l1(dev, addr);

	dev_err(dev->mt76.dev, "Access currently unsupported address %08x\n",
		addr);

	return 0;
}

static u32 mt7921_rr(struct mt76_dev *mdev, u32 offset)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	u32 addr = __mt7921_reg_addr(dev, offset);

	return dev->bus_ops->rr(mdev, addr);
}

static void mt7921_wr(struct mt76_dev *mdev, u32 offset, u32 val)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	u32 addr = __mt7921_reg_addr(dev, offset);

	dev->bus_ops->wr(mdev, addr, val);
}

static u32 mt7921_rmw(struct mt76_dev *mdev, u32 offset, u32 mask, u32 val)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	u32 addr = __mt7921_reg_addr(dev, offset);

	return dev->bus_ops->rmw(mdev, addr, mask, val);
}


static int mt7921_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		/* txwi_size = txd size + txp size */
		.txwi_size = MT_TXD_SIZE + sizeof(struct mt7921_txp_common),
		.drv_flags = MT_DRV_TXWI_NO_FREE | MT_DRV_HW_MGMT_TXQ |
			     MT_DRV_AMSDU_OFFLOAD,
		.survey_flags = SURVEY_INFO_TIME_TX |
				SURVEY_INFO_TIME_RX |
				SURVEY_INFO_TIME_BSS_RX,
		.token_size = MT7921_TOKEN_SIZE,
		.tx_prepare_skb = mt7921_tx_prepare_skb,
		.tx_complete_skb = mt7921_tx_complete_skb,
		.rx_skb = mt7921_queue_rx_skb,
		.rx_poll_complete = mt7921_rx_poll_complete,
		.sta_ps = mt7921_sta_ps,
		.sta_add = mt7921_mac_sta_add,
		.sta_assoc = mt7921_mac_sta_assoc,
		.sta_remove = mt7921_mac_sta_remove,
		.update_survey = mt7921_update_channel,
	};
	struct mt76_bus_ops *bus_ops;
	struct mt7921_dev *dev;
	struct mt76_dev *mdev;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		return ret;

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret)
		goto err_free_pci_vec;

	mt76_pci_disable_aspm(pdev);

	mdev = mt76_alloc_device(&pdev->dev, sizeof(*dev), &mt7921_ops,
				 &drv_ops);
	if (!mdev) {
		ret = -ENOMEM;
		goto err_free_pci_vec;
	}

	dev = container_of(mdev, struct mt7921_dev, mt76);

	mt76_mmio_init(&dev->mt76, pcim_iomap_table(pdev)[0]);
	tasklet_init(&dev->irq_tasklet, mt7921_irq_tasklet, (unsigned long)dev);

	dev->bus_ops = dev->mt76.bus;
	bus_ops = devm_kmemdup(dev->mt76.dev, dev->bus_ops, sizeof(*bus_ops),
			       GFP_KERNEL);
	if (!bus_ops)
		return -ENOMEM;

	bus_ops->rr = mt7921_rr;
	bus_ops->wr = mt7921_wr;
	bus_ops->rmw = mt7921_rmw;
	dev->mt76.bus = bus_ops;

	ret = __mt7921_mcu_drv_pmctrl(dev);
	if (ret)
		return ret;

	mdev->rev = (mt7921_l1_rr(dev, MT_HW_CHIPID) << 16) |
		    (mt7921_l1_rr(dev, MT_HW_REV) & 0xff);
	dev_err(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	mt76_wr(dev, MT_WFDMA0_HOST_INT_ENA, 0);

	mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);

	ret = devm_request_irq(mdev->dev, pdev->irq, mt7921_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto err_free_dev;

	ret = mt7921_register_device(dev);
	if (ret)
		goto err_free_irq;

	return 0;

err_free_irq:
	devm_free_irq(&pdev->dev, pdev->irq, dev);
err_free_dev:
	mt76_free_device(&dev->mt76);
err_free_pci_vec:
	pci_free_irq_vectors(pdev);

	return ret;
}

static void mt7921_pci_remove(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);

	mt7921_unregister_device(dev);
	devm_free_irq(&pdev->dev, pdev->irq, dev);
	pci_free_irq_vectors(pdev);
}

#ifdef CONFIG_PM
static int mt7921_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	struct mt76_connac_pm *pm = &dev->pm;
	bool hif_suspend;
	int i, err;

	pm->suspended = true;
	cancel_delayed_work_sync(&pm->ps_work);
	cancel_work_sync(&pm->wake_work);

	err = mt7921_mcu_drv_pmctrl(dev);
	if (err < 0)
		goto restore_suspend;

	hif_suspend = !test_bit(MT76_STATE_SUSPEND, &dev->mphy.state);
	if (hif_suspend) {
		err = mt76_connac_mcu_set_hif_suspend(mdev, true);
		if (err)
			goto restore_suspend;
	}

	/* always enable deep sleep during suspend to reduce
	 * power consumption
	 */
	mt76_connac_mcu_set_deep_sleep(&dev->mt76, true);

	napi_disable(&mdev->tx_napi);
	mt76_worker_disable(&mdev->tx_worker);

	mt76_for_each_q_rx(mdev, i) {
		napi_disable(&mdev->napi[i]);
	}

	pci_enable_wake(pdev, pci_choose_state(pdev, state), true);

	/* wait until dma is idle  */
	mt76_poll(dev, MT_WFDMA0_GLO_CFG,
		  MT_WFDMA0_GLO_CFG_TX_DMA_BUSY |
		  MT_WFDMA0_GLO_CFG_RX_DMA_BUSY, 0, 1000);

	/* put dma disabled */
	mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	/* disable interrupt */
	mt76_wr(dev, MT_WFDMA0_HOST_INT_ENA, 0);
	mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0x0);
	synchronize_irq(pdev->irq);
	tasklet_kill(&dev->irq_tasklet);

	err = mt7921_mcu_fw_pmctrl(dev);
	if (err)
		goto restore_napi;

	pci_save_state(pdev);
	err = pci_set_power_state(pdev, pci_choose_state(pdev, state));
	if (err)
		goto restore_napi;

	return 0;

restore_napi:
	mt76_for_each_q_rx(mdev, i) {
		napi_enable(&mdev->napi[i]);
	}
	napi_enable(&mdev->tx_napi);

	if (!pm->ds_enable)
		mt76_connac_mcu_set_deep_sleep(&dev->mt76, false);

	if (hif_suspend)
		mt76_connac_mcu_set_hif_suspend(mdev, false);

restore_suspend:
	pm->suspended = false;

	return err;
}

static int mt7921_pci_resume(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	struct mt76_connac_pm *pm = &dev->pm;
	int i, err;

	pm->suspended = false;
	err = pci_set_power_state(pdev, PCI_D0);
	if (err)
		return err;

	pci_restore_state(pdev);

	err = mt7921_mcu_drv_pmctrl(dev);
	if (err < 0)
		return err;

	mt7921_wpdma_reinit_cond(dev);

	/* enable interrupt */
	mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);
	mt7921_irq_enable(dev, MT_INT_RX_DONE_ALL | MT_INT_TX_DONE_ALL |
			  MT_INT_MCU_CMD);
	mt76_set(dev, MT_MCU2HOST_SW_INT_ENA, MT_MCU_CMD_WAKE_RX_PCIE);

	/* put dma enabled */
	mt76_set(dev, MT_WFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	mt76_worker_enable(&mdev->tx_worker);
	mt76_for_each_q_rx(mdev, i) {
		napi_enable(&mdev->napi[i]);
		napi_schedule(&mdev->napi[i]);
	}
	napi_enable(&mdev->tx_napi);
	napi_schedule(&mdev->tx_napi);

	/* restore previous ds setting */
	if (!pm->ds_enable)
		mt76_connac_mcu_set_deep_sleep(&dev->mt76, false);

	if (!test_bit(MT76_STATE_SUSPEND, &dev->mphy.state))
		err = mt76_connac_mcu_set_hif_suspend(mdev, false);

	return err;
}
#endif /* CONFIG_PM */

struct pci_driver mt7921_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt7921_pci_device_table,
	.probe		= mt7921_pci_probe,
	.remove		= mt7921_pci_remove,
#ifdef CONFIG_PM
	.suspend	= mt7921_pci_suspend,
	.resume		= mt7921_pci_resume,
#endif /* CONFIG_PM */
};

module_pci_driver(mt7921_pci_driver);

MODULE_DEVICE_TABLE(pci, mt7921_pci_device_table);
MODULE_FIRMWARE(MT7921_FIRMWARE_WM);
MODULE_FIRMWARE(MT7921_ROM_PATCH);
MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_LICENSE("Dual BSD/GPL");
