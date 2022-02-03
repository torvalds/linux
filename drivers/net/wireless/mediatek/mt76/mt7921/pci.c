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
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7922) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x0608) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x0616) },
	{ },
};

static bool mt7921_disable_aspm;
module_param_named(disable_aspm, mt7921_disable_aspm, bool, 0644);
MODULE_PARM_DESC(disable_aspm, "disable PCI ASPM support");

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

static int mt7921e_init_reset(struct mt7921_dev *dev)
{
	return mt7921_wpdma_reset(dev, true);
}

static void mt7921e_unregister_device(struct mt7921_dev *dev)
{
	int i;
	struct mt76_connac_pm *pm = &dev->pm;

	mt76_unregister_device(&dev->mt76);
	mt76_for_each_q_rx(&dev->mt76, i)
		napi_disable(&dev->mt76.napi[i]);
	cancel_delayed_work_sync(&pm->ps_work);
	cancel_work_sync(&pm->wake_work);

	mt7921_tx_token_put(dev);
	mt7921_mcu_drv_pmctrl(dev);
	mt7921_dma_cleanup(dev);
	mt7921_wfsys_reset(dev);
	mt7921_mcu_exit(dev);

	tasklet_disable(&dev->irq_tasklet);
	mt76_free_device(&dev->mt76);
}

static int mt7921_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		/* txwi_size = txd size + txp size */
		.txwi_size = MT_TXD_SIZE + sizeof(struct mt7921_txp_common),
		.drv_flags = MT_DRV_TXWI_NO_FREE | MT_DRV_HW_MGMT_TXQ,
		.survey_flags = SURVEY_INFO_TIME_TX |
				SURVEY_INFO_TIME_RX |
				SURVEY_INFO_TIME_BSS_RX,
		.token_size = MT7921_TOKEN_SIZE,
		.tx_prepare_skb = mt7921e_tx_prepare_skb,
		.tx_complete_skb = mt7921e_tx_complete_skb,
		.rx_skb = mt7921e_queue_rx_skb,
		.rx_poll_complete = mt7921_rx_poll_complete,
		.sta_ps = mt7921_sta_ps,
		.sta_add = mt7921_mac_sta_add,
		.sta_assoc = mt7921_mac_sta_assoc,
		.sta_remove = mt7921_mac_sta_remove,
		.update_survey = mt7921_update_channel,
	};

	static const struct mt7921_hif_ops mt7921_pcie_ops = {
		.init_reset = mt7921e_init_reset,
		.reset = mt7921e_mac_reset,
		.mcu_init = mt7921e_mcu_init,
		.drv_own = mt7921e_mcu_drv_pmctrl,
		.fw_own = mt7921e_mcu_fw_pmctrl,
	};

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

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		goto err_free_pci_vec;

	if (mt7921_disable_aspm)
		mt76_pci_disable_aspm(pdev);

	mdev = mt76_alloc_device(&pdev->dev, sizeof(*dev), &mt7921_ops,
				 &drv_ops);
	if (!mdev) {
		ret = -ENOMEM;
		goto err_free_pci_vec;
	}

	dev = container_of(mdev, struct mt7921_dev, mt76);
	dev->hif_ops = &mt7921_pcie_ops;

	mt76_mmio_init(&dev->mt76, pcim_iomap_table(pdev)[0]);
	tasklet_init(&dev->irq_tasklet, mt7921_irq_tasklet, (unsigned long)dev);
	mdev->rev = (mt7921_l1_rr(dev, MT_HW_CHIPID) << 16) |
		    (mt7921_l1_rr(dev, MT_HW_REV) & 0xff);
	dev_info(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	mt76_wr(dev, MT_WFDMA0_HOST_INT_ENA, 0);

	mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);

	ret = devm_request_irq(mdev->dev, pdev->irq, mt7921_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto err_free_dev;

	ret = mt7921_dma_init(dev);
	if (ret)
		goto err_free_irq;

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

	mt7921e_unregister_device(dev);
	devm_free_irq(&pdev->dev, pdev->irq, dev);
	pci_free_irq_vectors(pdev);
}

#ifdef CONFIG_PM
static int mt7921_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	struct mt76_connac_pm *pm = &dev->pm;
	int i, err;

	pm->suspended = true;
	cancel_delayed_work_sync(&pm->ps_work);
	cancel_work_sync(&pm->wake_work);

	err = mt7921_mcu_drv_pmctrl(dev);
	if (err < 0)
		goto restore_suspend;

	err = mt76_connac_mcu_set_hif_suspend(mdev, true);
	if (err)
		goto restore_suspend;

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

	local_bh_disable();
	mt76_for_each_q_rx(mdev, i) {
		napi_enable(&mdev->napi[i]);
		napi_schedule(&mdev->napi[i]);
	}
	napi_enable(&mdev->tx_napi);
	napi_schedule(&mdev->tx_napi);
	local_bh_enable();

	/* restore previous ds setting */
	if (!pm->ds_enable)
		mt76_connac_mcu_set_deep_sleep(&dev->mt76, false);

	err = mt76_connac_mcu_set_hif_suspend(mdev, false);
	if (err)
		return err;

	pm->suspended = false;

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
MODULE_FIRMWARE(MT7922_FIRMWARE_WM);
MODULE_FIRMWARE(MT7922_ROM_PATCH);
MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_LICENSE("Dual BSD/GPL");
