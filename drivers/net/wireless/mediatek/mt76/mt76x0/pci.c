// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mt76x0.h"
#include "mcu.h"

static int mt76x0e_start(struct ieee80211_hw *hw)
{
	struct mt76x02_dev *dev = hw->priv;

	mt76x02_mac_start(dev);
	mt76x0_phy_calibrate(dev, true);
	ieee80211_queue_delayed_work(dev->mt76.hw, &dev->mphy.mac_work,
				     MT_MAC_WORK_INTERVAL);
	ieee80211_queue_delayed_work(dev->mt76.hw, &dev->cal_work,
				     MT_CALIBRATE_INTERVAL);
	set_bit(MT76_STATE_RUNNING, &dev->mphy.state);

	return 0;
}

static void mt76x0e_stop_hw(struct mt76x02_dev *dev)
{
	cancel_delayed_work_sync(&dev->cal_work);
	cancel_delayed_work_sync(&dev->mphy.mac_work);
	clear_bit(MT76_RESTART, &dev->mphy.state);

	if (!mt76_poll(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_TX_DMA_BUSY,
		       0, 1000))
		dev_warn(dev->mt76.dev, "TX DMA did not stop\n");
	mt76_clear(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_TX_DMA_EN);

	mt76x0_mac_stop(dev);

	if (!mt76_poll(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_RX_DMA_BUSY,
		       0, 1000))
		dev_warn(dev->mt76.dev, "TX DMA did not stop\n");
	mt76_clear(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_RX_DMA_EN);
}

static void mt76x0e_stop(struct ieee80211_hw *hw)
{
	struct mt76x02_dev *dev = hw->priv;

	clear_bit(MT76_STATE_RUNNING, &dev->mphy.state);
	mt76x0e_stop_hw(dev);
}

static void
mt76x0e_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	      u32 queues, bool drop)
{
}

static const struct ieee80211_ops mt76x0e_ops = {
	.tx = mt76x02_tx,
	.start = mt76x0e_start,
	.stop = mt76x0e_stop,
	.add_interface = mt76x02_add_interface,
	.remove_interface = mt76x02_remove_interface,
	.config = mt76x0_config,
	.configure_filter = mt76x02_configure_filter,
	.bss_info_changed = mt76x02_bss_info_changed,
	.sta_state = mt76_sta_state,
	.sta_pre_rcu_remove = mt76_sta_pre_rcu_remove,
	.set_key = mt76x02_set_key,
	.conf_tx = mt76x02_conf_tx,
	.sw_scan_start = mt76_sw_scan,
	.sw_scan_complete = mt76x02_sw_scan_complete,
	.ampdu_action = mt76x02_ampdu_action,
	.sta_rate_tbl_update = mt76x02_sta_rate_tbl_update,
	.wake_tx_queue = mt76_wake_tx_queue,
	.get_survey = mt76_get_survey,
	.get_txpower = mt76_get_txpower,
	.flush = mt76x0e_flush,
	.set_tim = mt76_set_tim,
	.release_buffered_frames = mt76_release_buffered_frames,
	.set_coverage_class = mt76x02_set_coverage_class,
	.set_rts_threshold = mt76x02_set_rts_threshold,
	.get_antenna = mt76_get_antenna,
	.reconfig_complete = mt76x02_reconfig_complete,
	.set_sar_specs = mt76x0_set_sar_specs,
};

static int mt76x0e_init_hardware(struct mt76x02_dev *dev, bool resume)
{
	int err;

	mt76x0_chip_onoff(dev, true, false);
	if (!mt76x02_wait_for_mac(&dev->mt76))
		return -ETIMEDOUT;

	mt76x02_dma_disable(dev);
	err = mt76x0e_mcu_init(dev);
	if (err < 0)
		return err;

	if (!resume) {
		err = mt76x02_dma_init(dev);
		if (err < 0)
			return err;
	}

	err = mt76x0_init_hardware(dev);
	if (err < 0)
		return err;

	mt76x02e_init_beacon_config(dev);

	if (mt76_chip(&dev->mt76) == 0x7610) {
		u16 val;

		mt76_clear(dev, MT_COEXCFG0, BIT(0));

		val = mt76x02_eeprom_get(dev, MT_EE_NIC_CONF_0);
		if (!(val & MT_EE_NIC_CONF_0_PA_IO_CURRENT))
			mt76_set(dev, MT_XO_CTRL7, 0xc03);
	}

	mt76_clear(dev, 0x110, BIT(9));
	mt76_set(dev, MT_MAX_LEN_CFG, BIT(13));

	return 0;
}

static int mt76x0e_register_device(struct mt76x02_dev *dev)
{
	int err;

	err = mt76x0e_init_hardware(dev, false);
	if (err < 0)
		return err;

	err = mt76x0_register_device(dev);
	if (err < 0)
		return err;

	set_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);

	return 0;
}

static int
mt76x0e_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		.txwi_size = sizeof(struct mt76x02_txwi),
		.drv_flags = MT_DRV_TX_ALIGNED4_SKBS |
			     MT_DRV_SW_RX_AIRTIME,
		.survey_flags = SURVEY_INFO_TIME_TX,
		.update_survey = mt76x02_update_channel,
		.tx_prepare_skb = mt76x02_tx_prepare_skb,
		.tx_complete_skb = mt76x02_tx_complete_skb,
		.rx_skb = mt76x02_queue_rx_skb,
		.rx_poll_complete = mt76x02_rx_poll_complete,
		.sta_ps = mt76x02_sta_ps,
		.sta_add = mt76x02_sta_add,
		.sta_remove = mt76x02_sta_remove,
	};
	struct mt76x02_dev *dev;
	struct mt76_dev *mdev;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	mt76_pci_disable_aspm(pdev);

	mdev = mt76_alloc_device(&pdev->dev, sizeof(*dev), &mt76x0e_ops,
				 &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt76x02_dev, mt76);
	mutex_init(&dev->phy_mutex);

	mt76_mmio_init(mdev, pcim_iomap_table(pdev)[0]);

	mdev->rev = mt76_rr(dev, MT_ASIC_VERSION);
	dev_info(mdev->dev, "ASIC revision: %08x\n", mdev->rev);

	mt76_wr(dev, MT_INT_MASK_CSR, 0);

	ret = devm_request_irq(mdev->dev, pdev->irq, mt76x02_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto error;

	ret = mt76x0e_register_device(dev);
	if (ret < 0)
		goto error;

	return 0;

error:
	mt76_free_device(&dev->mt76);

	return ret;
}

static void mt76x0e_cleanup(struct mt76x02_dev *dev)
{
	clear_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);
	tasklet_disable(&dev->mt76.pre_tbtt_tasklet);
	mt76x0_chip_onoff(dev, false, false);
	mt76x0e_stop_hw(dev);
	mt76_dma_cleanup(&dev->mt76);
	mt76x02_mcu_cleanup(dev);
}

static void
mt76x0e_remove(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);

	mt76_unregister_device(mdev);
	mt76x0e_cleanup(dev);
	mt76_free_device(mdev);
}

#ifdef CONFIG_PM
static int mt76x0e_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);
	int i;

	mt76_worker_disable(&mdev->tx_worker);
	for (i = 0; i < ARRAY_SIZE(mdev->phy.q_tx); i++)
		mt76_queue_tx_cleanup(dev, mdev->phy.q_tx[i], true);
	for (i = 0; i < ARRAY_SIZE(mdev->q_mcu); i++)
		mt76_queue_tx_cleanup(dev, mdev->q_mcu[i], true);
	napi_disable(&mdev->tx_napi);

	mt76_for_each_q_rx(mdev, i)
		napi_disable(&mdev->napi[i]);

	mt76x02_dma_disable(dev);
	mt76x02_mcu_cleanup(dev);
	mt76x0_chip_onoff(dev, false, false);

	pci_enable_wake(pdev, pci_choose_state(pdev, state), true);
	pci_save_state(pdev);

	return pci_set_power_state(pdev, pci_choose_state(pdev, state));
}

static int mt76x0e_resume(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);
	int err, i;

	err = pci_set_power_state(pdev, PCI_D0);
	if (err)
		return err;

	pci_restore_state(pdev);

	mt76_worker_enable(&mdev->tx_worker);

	local_bh_disable();
	mt76_for_each_q_rx(mdev, i) {
		mt76_queue_rx_reset(dev, i);
		napi_enable(&mdev->napi[i]);
		napi_schedule(&mdev->napi[i]);
	}

	napi_enable(&mdev->tx_napi);
	napi_schedule(&mdev->tx_napi);
	local_bh_enable();

	return mt76x0e_init_hardware(dev, true);
}
#endif /* CONFIG_PM */

static const struct pci_device_id mt76x0e_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7610) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7630) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7650) },
	{ },
};

MODULE_DEVICE_TABLE(pci, mt76x0e_device_table);
MODULE_FIRMWARE(MT7610E_FIRMWARE);
MODULE_FIRMWARE(MT7650E_FIRMWARE);
MODULE_DESCRIPTION("MediaTek MT76x0E (PCIe) wireless driver");
MODULE_LICENSE("Dual BSD/GPL");

static struct pci_driver mt76x0e_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt76x0e_device_table,
	.probe		= mt76x0e_probe,
	.remove		= mt76x0e_remove,
#ifdef CONFIG_PM
	.suspend	= mt76x0e_suspend,
	.resume		= mt76x0e_resume,
#endif /* CONFIG_PM */
};

module_pci_driver(mt76x0e_driver);
