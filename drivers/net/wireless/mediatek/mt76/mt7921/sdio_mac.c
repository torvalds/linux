// SPDX-License-Identifier: ISC
/* Copyright (C) 2021 MediaTek Inc. */

#include <linux/iopoll.h>
#include <linux/mmc/sdio_func.h>
#include "mt7921.h"
#include "../mt76_connac2_mac.h"
#include "../sdio.h"

static void mt7921s_enable_irq(struct mt76_dev *dev)
{
	struct mt76_sdio *sdio = &dev->sdio;

	sdio_claim_host(sdio->func);
	sdio_writel(sdio->func, WHLPCR_INT_EN_SET, MCR_WHLPCR, NULL);
	sdio_release_host(sdio->func);
}

static void mt7921s_disable_irq(struct mt76_dev *dev)
{
	struct mt76_sdio *sdio = &dev->sdio;

	sdio_claim_host(sdio->func);
	sdio_writel(sdio->func, WHLPCR_INT_EN_CLR, MCR_WHLPCR, NULL);
	sdio_release_host(sdio->func);
}

static u32 mt7921s_read_whcr(struct mt76_dev *dev)
{
	return sdio_readl(dev->sdio.func, MCR_WHCR, NULL);
}

int mt7921s_wfsys_reset(struct mt792x_dev *dev)
{
	struct mt76_sdio *sdio = &dev->mt76.sdio;
	u32 val, status;

	mt7921s_mcu_drv_pmctrl(dev);

	sdio_claim_host(sdio->func);

	val = sdio_readl(sdio->func, MCR_WHCR, NULL);
	val &= ~WF_WHOLE_PATH_RSTB;
	sdio_writel(sdio->func, val, MCR_WHCR, NULL);

	msleep(50);

	val = sdio_readl(sdio->func, MCR_WHCR, NULL);
	val &= ~WF_SDIO_WF_PATH_RSTB;
	sdio_writel(sdio->func, val, MCR_WHCR, NULL);

	usleep_range(1000, 2000);

	val = sdio_readl(sdio->func, MCR_WHCR, NULL);
	val |= WF_WHOLE_PATH_RSTB;
	sdio_writel(sdio->func, val, MCR_WHCR, NULL);

	readx_poll_timeout(mt7921s_read_whcr, &dev->mt76, status,
			   status & WF_RST_DONE, 50000, 2000000);

	sdio_release_host(sdio->func);

	clear_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);

	/* activate mt7921s again */
	mt7921s_mcu_drv_pmctrl(dev);
	mt76_clear(dev, MT_CONN_STATUS, MT_WIFI_PATCH_DL_STATE);
	mt7921s_mcu_fw_pmctrl(dev);
	mt7921s_mcu_drv_pmctrl(dev);

	return 0;
}

int mt7921s_init_reset(struct mt792x_dev *dev)
{
	set_bit(MT76_MCU_RESET, &dev->mphy.state);

	wake_up(&dev->mt76.mcu.wait);
	skb_queue_purge(&dev->mt76.mcu.res_q);
	wait_event_timeout(dev->mt76.sdio.wait,
			   mt76s_txqs_empty(&dev->mt76), 5 * HZ);
	mt76_worker_disable(&dev->mt76.sdio.txrx_worker);

	mt7921s_disable_irq(&dev->mt76);
	mt7921s_wfsys_reset(dev);

	mt76_worker_enable(&dev->mt76.sdio.txrx_worker);
	clear_bit(MT76_MCU_RESET, &dev->mphy.state);
	mt7921s_enable_irq(&dev->mt76);

	return 0;
}

int mt7921s_mac_reset(struct mt792x_dev *dev)
{
	int err;

	mt76_connac_free_pending_tx_skbs(&dev->pm, NULL);
	mt76_txq_schedule_all(&dev->mphy);
	mt76_worker_disable(&dev->mt76.tx_worker);
	set_bit(MT76_MCU_RESET, &dev->mphy.state);
	wake_up(&dev->mt76.mcu.wait);
	skb_queue_purge(&dev->mt76.mcu.res_q);
	wait_event_timeout(dev->mt76.sdio.wait,
			   mt76s_txqs_empty(&dev->mt76), 5 * HZ);
	mt76_worker_disable(&dev->mt76.sdio.txrx_worker);
	mt76_worker_disable(&dev->mt76.sdio.status_worker);
	mt76_worker_disable(&dev->mt76.sdio.net_worker);
	mt76_worker_disable(&dev->mt76.sdio.stat_worker);

	mt7921s_disable_irq(&dev->mt76);
	mt7921s_wfsys_reset(dev);

	mt76_worker_enable(&dev->mt76.sdio.txrx_worker);
	mt76_worker_enable(&dev->mt76.sdio.status_worker);
	mt76_worker_enable(&dev->mt76.sdio.net_worker);
	mt76_worker_enable(&dev->mt76.sdio.stat_worker);

	dev->fw_assert = false;
	clear_bit(MT76_MCU_RESET, &dev->mphy.state);
	mt7921s_enable_irq(&dev->mt76);

	err = mt7921_run_firmware(dev);
	if (err)
		goto out;

	err = mt7921_mcu_set_eeprom(dev);
	if (err)
		goto out;

	err = mt7921_mac_init(dev);
	if (err)
		goto out;

	err = __mt7921_start(&dev->phy);
out:

	mt76_worker_enable(&dev->mt76.tx_worker);

	return err;
}
