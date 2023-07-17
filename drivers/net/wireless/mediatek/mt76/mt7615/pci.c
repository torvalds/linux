// SPDX-License-Identifier: ISC
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 *         Felix Fietkau <nbd@nbd.name>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mt7615.h"
#include "mcu.h"

static const struct pci_device_id mt7615_pci_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7615) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7663) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7611) },
	{ },
};

static int mt7615_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	const u32 *map;
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
		goto error;

	mt76_pci_disable_aspm(pdev);

	map = id->device == 0x7663 ? mt7663e_reg_map : mt7615e_reg_map;
	ret = mt7615_mmio_probe(&pdev->dev, pcim_iomap_table(pdev)[0],
				pdev->irq, map);
	if (ret)
		goto error;

	return 0;
error:
	pci_free_irq_vectors(pdev);

	return ret;
}

static void mt7615_pci_remove(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);

	mt7615_unregister_device(dev);
	devm_free_irq(&pdev->dev, pdev->irq, dev);
	pci_free_irq_vectors(pdev);
}

#ifdef CONFIG_PM
static int mt7615_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	bool hif_suspend;
	int i, err;

	err = mt76_connac_pm_wake(&dev->mphy, &dev->pm);
	if (err < 0)
		return err;

	hif_suspend = !test_bit(MT76_STATE_SUSPEND, &dev->mphy.state) &&
		      mt7615_firmware_offload(dev);
	if (hif_suspend) {
		err = mt76_connac_mcu_set_hif_suspend(mdev, true);
		if (err)
			return err;
	}

	napi_disable(&mdev->tx_napi);
	mt76_worker_disable(&mdev->tx_worker);

	mt76_for_each_q_rx(mdev, i) {
		napi_disable(&mdev->napi[i]);
	}
	tasklet_kill(&mdev->irq_tasklet);

	mt7615_dma_reset(dev);

	err = mt7615_wait_pdma_busy(dev);
	if (err)
		goto restore;

	if (is_mt7663(mdev)) {
		mt76_set(dev, MT_PDMA_SLP_PROT, MT_PDMA_AXI_SLPPROT_ENABLE);
		if (!mt76_poll_msec(dev, MT_PDMA_SLP_PROT,
				    MT_PDMA_AXI_SLPPROT_RDY,
				    MT_PDMA_AXI_SLPPROT_RDY, 1000)) {
			dev_err(mdev->dev, "PDMA sleep protection failed\n");
			err = -EIO;
			goto restore;
		}
	}

	pci_enable_wake(pdev, pci_choose_state(pdev, state), true);
	pci_save_state(pdev);
	err = pci_set_power_state(pdev, pci_choose_state(pdev, state));
	if (err)
		goto restore;

	err = mt7615_mcu_set_fw_ctrl(dev);
	if (err)
		goto restore;

	return 0;

restore:
	mt76_for_each_q_rx(mdev, i) {
		napi_enable(&mdev->napi[i]);
	}
	napi_enable(&mdev->tx_napi);
	if (hif_suspend)
		mt76_connac_mcu_set_hif_suspend(mdev, false);

	return err;
}

static int mt7615_pci_resume(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	bool pdma_reset;
	int i, err;

	err = mt7615_mcu_set_drv_ctrl(dev);
	if (err < 0)
		return err;

	err = pci_set_power_state(pdev, PCI_D0);
	if (err)
		return err;

	pci_restore_state(pdev);

	if (is_mt7663(&dev->mt76)) {
		mt76_clear(dev, MT_PDMA_SLP_PROT, MT_PDMA_AXI_SLPPROT_ENABLE);
		mt76_wr(dev, MT_PCIE_IRQ_ENABLE, 1);
	}

	pdma_reset = !mt76_rr(dev, MT_WPDMA_TX_RING0_CTRL0) &&
		     !mt76_rr(dev, MT_WPDMA_TX_RING0_CTRL1);
	if (pdma_reset)
		dev_err(mdev->dev, "PDMA engine must be reinitialized\n");

	mt76_worker_enable(&mdev->tx_worker);
	local_bh_disable();
	mt76_for_each_q_rx(mdev, i) {
		napi_enable(&mdev->napi[i]);
		napi_schedule(&mdev->napi[i]);
	}
	napi_enable(&mdev->tx_napi);
	napi_schedule(&mdev->tx_napi);
	local_bh_enable();

	if (!test_bit(MT76_STATE_SUSPEND, &dev->mphy.state) &&
	    mt7615_firmware_offload(dev))
		err = mt76_connac_mcu_set_hif_suspend(mdev, false);

	return err;
}
#endif /* CONFIG_PM */

struct pci_driver mt7615_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt7615_pci_device_table,
	.probe		= mt7615_pci_probe,
	.remove		= mt7615_pci_remove,
#ifdef CONFIG_PM
	.suspend	= mt7615_pci_suspend,
	.resume		= mt7615_pci_resume,
#endif /* CONFIG_PM */
};

MODULE_DEVICE_TABLE(pci, mt7615_pci_device_table);
MODULE_FIRMWARE(MT7615_FIRMWARE_CR4);
MODULE_FIRMWARE(MT7615_FIRMWARE_N9);
MODULE_FIRMWARE(MT7615_ROM_PATCH);
MODULE_FIRMWARE(MT7663_OFFLOAD_FIRMWARE_N9);
MODULE_FIRMWARE(MT7663_OFFLOAD_ROM_PATCH);
MODULE_FIRMWARE(MT7663_FIRMWARE_N9);
MODULE_FIRMWARE(MT7663_ROM_PATCH);
