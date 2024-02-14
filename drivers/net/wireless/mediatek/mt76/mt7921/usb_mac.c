// SPDX-License-Identifier: ISC
/* Copyright (C) 2022 MediaTek Inc.
 *
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "mt7921.h"
#include "mcu.h"
#include "mac.h"

static u32 mt7921u_uhw_rr(struct mt76_dev *dev, u32 addr)
{
	u32 ret;

	mutex_lock(&dev->usb.usb_ctrl_mtx);
	ret = ___mt76u_rr(dev, MT_VEND_DEV_MODE,
			  USB_DIR_IN | MT_USB_TYPE_UHW_VENDOR, addr);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return ret;
}

static void mt7921u_uhw_wr(struct mt76_dev *dev, u32 addr, u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	___mt76u_wr(dev, MT_VEND_WRITE,
		    USB_DIR_OUT | MT_USB_TYPE_UHW_VENDOR, addr, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);
}

static void mt7921u_dma_prefetch(struct mt7921_dev *dev)
{
	mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL(0),
		 MT_WPDMA0_MAX_CNT_MASK, 4);
	mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL(0),
		 MT_WPDMA0_BASE_PTR_MASK, 0x80);

	mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL(1),
		 MT_WPDMA0_MAX_CNT_MASK, 4);
	mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL(1),
		 MT_WPDMA0_BASE_PTR_MASK, 0xc0);

	mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL(2),
		 MT_WPDMA0_MAX_CNT_MASK, 4);
	mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL(2),
		 MT_WPDMA0_BASE_PTR_MASK, 0x100);

	mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL(3),
		 MT_WPDMA0_MAX_CNT_MASK, 4);
	mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL(3),
		 MT_WPDMA0_BASE_PTR_MASK, 0x140);

	mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL(4),
		 MT_WPDMA0_MAX_CNT_MASK, 4);
	mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL(4),
		 MT_WPDMA0_BASE_PTR_MASK, 0x180);

	mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL(16),
		 MT_WPDMA0_MAX_CNT_MASK, 4);
	mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL(16),
		 MT_WPDMA0_BASE_PTR_MASK, 0x280);

	mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL(17),
		 MT_WPDMA0_MAX_CNT_MASK, 4);
	mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL(17),
		 MT_WPDMA0_BASE_PTR_MASK,  0x2c0);
}

static void mt7921u_wfdma_init(struct mt7921_dev *dev)
{
	mt7921u_dma_prefetch(dev);

	mt76_clear(dev, MT_UWFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_OMIT_RX_INFO);
	mt76_set(dev, MT_UWFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
		 MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2 |
		 MT_WFDMA0_GLO_CFG_FW_DWLD_BYPASS_DMASHDL |
		 MT_WFDMA0_GLO_CFG_TX_DMA_EN |
		 MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	/* disable dmashdl */
	mt76_clear(dev, MT_UWFDMA0_GLO_CFG_EXT0,
		   MT_WFDMA0_CSR_TX_DMASHDL_ENABLE);
	mt76_set(dev, MT_DMASHDL_SW_CONTROL, MT_DMASHDL_DMASHDL_BYPASS);

	mt76_set(dev, MT_WFDMA_DUMMY_CR, MT_WFDMA_NEED_REINIT);
}

static int mt7921u_dma_rx_evt_ep4(struct mt7921_dev *dev)
{
	if (!mt76_poll(dev, MT_UWFDMA0_GLO_CFG,
		       MT_WFDMA0_GLO_CFG_RX_DMA_BUSY, 0, 1000))
		return -ETIMEDOUT;

	mt76_clear(dev, MT_UWFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_RX_DMA_EN);
	mt76_set(dev, MT_WFDMA_HOST_CONFIG,
		 MT_WFDMA_HOST_CONFIG_USB_RXEVT_EP4_EN);
	mt76_set(dev, MT_UWFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	return 0;
}

static void mt7921u_epctl_rst_opt(struct mt7921_dev *dev, bool reset)
{
	u32 val;

	/* usb endpoint reset opt
	 * bits[4,9]: out blk ep 4-9
	 * bits[20,21]: in blk ep 4-5
	 * bits[22]: in int ep 6
	 */
	val = mt7921u_uhw_rr(&dev->mt76, MT_SSUSB_EPCTL_CSR_EP_RST_OPT);
	if (reset)
		val |= GENMASK(9, 4) | GENMASK(22, 20);
	else
		val &= ~(GENMASK(9, 4) | GENMASK(22, 20));
	mt7921u_uhw_wr(&dev->mt76, MT_SSUSB_EPCTL_CSR_EP_RST_OPT, val);
}

int mt7921u_dma_init(struct mt7921_dev *dev, bool resume)
{
	int err;

	mt7921u_wfdma_init(dev);

	mt76_clear(dev, MT_UDMA_WLCFG_0, MT_WL_RX_FLUSH);

	mt76_set(dev, MT_UDMA_WLCFG_0,
		 MT_WL_RX_EN | MT_WL_TX_EN |
		 MT_WL_RX_MPSZ_PAD0 | MT_TICK_1US_EN);
	mt76_clear(dev, MT_UDMA_WLCFG_0,
		   MT_WL_RX_AGG_TO | MT_WL_RX_AGG_LMT);
	mt76_clear(dev, MT_UDMA_WLCFG_1, MT_WL_RX_AGG_PKT_LMT);

	if (resume)
		return 0;

	err = mt7921u_dma_rx_evt_ep4(dev);
	if (err)
		return err;

	mt7921u_epctl_rst_opt(dev, false);

	return 0;
}

int mt7921u_wfsys_reset(struct mt7921_dev *dev)
{
	u32 val;
	int i;

	mt7921u_epctl_rst_opt(dev, false);

	val = mt7921u_uhw_rr(&dev->mt76, MT_CBTOP_RGU_WF_SUBSYS_RST);
	val |= MT_CBTOP_RGU_WF_SUBSYS_RST_WF_WHOLE_PATH;
	mt7921u_uhw_wr(&dev->mt76, MT_CBTOP_RGU_WF_SUBSYS_RST, val);

	usleep_range(10, 20);

	val = mt7921u_uhw_rr(&dev->mt76, MT_CBTOP_RGU_WF_SUBSYS_RST);
	val &= ~MT_CBTOP_RGU_WF_SUBSYS_RST_WF_WHOLE_PATH;
	mt7921u_uhw_wr(&dev->mt76, MT_CBTOP_RGU_WF_SUBSYS_RST, val);

	mt7921u_uhw_wr(&dev->mt76, MT_UDMA_CONN_INFRA_STATUS_SEL, 0);
	for (i = 0; i < MT7921_WFSYS_INIT_RETRY_COUNT; i++) {
		val = mt7921u_uhw_rr(&dev->mt76, MT_UDMA_CONN_INFRA_STATUS);
		if (val & MT_UDMA_CONN_WFSYS_INIT_DONE)
			break;

		msleep(100);
	}

	if (i == MT7921_WFSYS_INIT_RETRY_COUNT)
		return -ETIMEDOUT;

	return 0;
}

int mt7921u_init_reset(struct mt7921_dev *dev)
{
	set_bit(MT76_RESET, &dev->mphy.state);

	wake_up(&dev->mt76.mcu.wait);
	skb_queue_purge(&dev->mt76.mcu.res_q);

	mt76u_stop_rx(&dev->mt76);
	mt76u_stop_tx(&dev->mt76);

	mt7921u_wfsys_reset(dev);

	clear_bit(MT76_RESET, &dev->mphy.state);

	return mt76u_resume_rx(&dev->mt76);
}

int mt7921u_mac_reset(struct mt7921_dev *dev)
{
	int err;

	mt76_txq_schedule_all(&dev->mphy);
	mt76_worker_disable(&dev->mt76.tx_worker);

	set_bit(MT76_RESET, &dev->mphy.state);
	set_bit(MT76_MCU_RESET, &dev->mphy.state);

	wake_up(&dev->mt76.mcu.wait);
	skb_queue_purge(&dev->mt76.mcu.res_q);

	mt76u_stop_rx(&dev->mt76);
	mt76u_stop_tx(&dev->mt76);

	mt7921u_wfsys_reset(dev);

	clear_bit(MT76_MCU_RESET, &dev->mphy.state);
	err = mt76u_resume_rx(&dev->mt76);
	if (err)
		goto out;

	err = mt7921u_mcu_power_on(dev);
	if (err)
		goto out;

	err = mt7921u_dma_init(dev, false);
	if (err)
		goto out;

	mt76_wr(dev, MT_SWDEF_MODE, MT_SWDEF_NORMAL_MODE);
	mt76_set(dev, MT_UDMA_TX_QSEL, MT_FW_DL_EN);

	err = mt7921_run_firmware(dev);
	if (err)
		goto out;

	mt76_clear(dev, MT_UDMA_TX_QSEL, MT_FW_DL_EN);

	err = mt7921_mcu_set_eeprom(dev);
	if (err)
		goto out;

	err = mt7921_mac_init(dev);
	if (err)
		goto out;

	err = __mt7921_start(&dev->phy);
out:
	clear_bit(MT76_RESET, &dev->mphy.state);

	mt76_worker_enable(&dev->mt76.tx_worker);

	return err;
}
