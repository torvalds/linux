// SPDX-License-Identifier: ISC
/* Copyright (C) 2023 MediaTek Inc.
 *
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "mt792x.h"
#include "mt76_connac2_mac.h"

u32 mt792xu_rr(struct mt76_dev *dev, u32 addr)
{
	u32 ret;

	mutex_lock(&dev->usb.usb_ctrl_mtx);
	ret = ___mt76u_rr(dev, MT_VEND_READ_EXT,
			  USB_DIR_IN | MT_USB_TYPE_VENDOR, addr);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return ret;
}
EXPORT_SYMBOL_GPL(mt792xu_rr);

void mt792xu_wr(struct mt76_dev *dev, u32 addr, u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	___mt76u_wr(dev, MT_VEND_WRITE_EXT,
		    USB_DIR_OUT | MT_USB_TYPE_VENDOR, addr, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);
}
EXPORT_SYMBOL_GPL(mt792xu_wr);

u32 mt792xu_rmw(struct mt76_dev *dev, u32 addr, u32 mask, u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	val |= ___mt76u_rr(dev, MT_VEND_READ_EXT,
			   USB_DIR_IN | MT_USB_TYPE_VENDOR, addr) & ~mask;
	___mt76u_wr(dev, MT_VEND_WRITE_EXT,
		    USB_DIR_OUT | MT_USB_TYPE_VENDOR, addr, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return val;
}
EXPORT_SYMBOL_GPL(mt792xu_rmw);

void mt792xu_copy(struct mt76_dev *dev, u32 offset, const void *data, int len)
{
	struct mt76_usb *usb = &dev->usb;
	int ret, i = 0, batch_len;
	const u8 *val = data;

	len = round_up(len, 4);

	mutex_lock(&usb->usb_ctrl_mtx);
	while (i < len) {
		batch_len = min_t(int, usb->data_len, len - i);
		memcpy(usb->data, val + i, batch_len);
		ret = __mt76u_vendor_request(dev, MT_VEND_WRITE_EXT,
					     USB_DIR_OUT | MT_USB_TYPE_VENDOR,
					     (offset + i) >> 16, offset + i,
					     usb->data, batch_len);
		if (ret < 0)
			break;

		i += batch_len;
	}
	mutex_unlock(&usb->usb_ctrl_mtx);
}
EXPORT_SYMBOL_GPL(mt792xu_copy);

int mt792xu_mcu_power_on(struct mt792x_dev *dev)
{
	int ret;

	ret = mt76u_vendor_request(&dev->mt76, MT_VEND_POWER_ON,
				   USB_DIR_OUT | MT_USB_TYPE_VENDOR,
				   0x0, 0x1, NULL, 0);
	if (ret)
		return ret;

	if (!mt76_poll_msec(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_PWR_ON,
			    MT_TOP_MISC2_FW_PWR_ON, 500)) {
		dev_err(dev->mt76.dev, "Timeout for power on\n");
		ret = -EIO;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mt792xu_mcu_power_on);

static void mt792xu_cleanup(struct mt792x_dev *dev)
{
	clear_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);
	mt792xu_wfsys_reset(dev);
	skb_queue_purge(&dev->mt76.mcu.res_q);
	mt76u_queues_deinit(&dev->mt76);
}

static u32 mt792xu_uhw_rr(struct mt76_dev *dev, u32 addr)
{
	u32 ret;

	mutex_lock(&dev->usb.usb_ctrl_mtx);
	ret = ___mt76u_rr(dev, MT_VEND_DEV_MODE,
			  USB_DIR_IN | MT_USB_TYPE_UHW_VENDOR, addr);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return ret;
}

static void mt792xu_uhw_wr(struct mt76_dev *dev, u32 addr, u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	___mt76u_wr(dev, MT_VEND_WRITE,
		    USB_DIR_OUT | MT_USB_TYPE_UHW_VENDOR, addr, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);
}

static void mt792xu_dma_prefetch(struct mt792x_dev *dev)
{
#define DMA_PREFETCH_CONF(_idx_, _cnt_, _base_) \
	mt76_rmw(dev, MT_UWFDMA0_TX_RING_EXT_CTRL((_idx_)), \
		 MT_WPDMA0_MAX_CNT_MASK | MT_WPDMA0_BASE_PTR_MASK, \
		 FIELD_PREP(MT_WPDMA0_MAX_CNT_MASK, (_cnt_)) | \
		 FIELD_PREP(MT_WPDMA0_BASE_PTR_MASK, (_base_)))

	DMA_PREFETCH_CONF(0, 4, 0x080);
	DMA_PREFETCH_CONF(1, 4, 0x0c0);
	DMA_PREFETCH_CONF(2, 4, 0x100);
	DMA_PREFETCH_CONF(3, 4, 0x140);
	DMA_PREFETCH_CONF(4, 4, 0x180);
	DMA_PREFETCH_CONF(16, 4, 0x280);
	DMA_PREFETCH_CONF(17, 4, 0x2c0);
}

static void mt792xu_wfdma_init(struct mt792x_dev *dev)
{
	int i;

	mt792xu_dma_prefetch(dev);

	mt76_clear(dev, MT_UWFDMA0_GLO_CFG, MT_WFDMA0_GLO_CFG_OMIT_RX_INFO);
	mt76_set(dev, MT_UWFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
		 MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2 |
		 MT_WFDMA0_GLO_CFG_FW_DWLD_BYPASS_DMASHDL |
		 MT_WFDMA0_GLO_CFG_TX_DMA_EN |
		 MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	mt76_rmw(dev, MT_DMASHDL_REFILL, MT_DMASHDL_REFILL_MASK, 0xffe00000);
	mt76_clear(dev, MT_DMASHDL_PAGE, MT_DMASHDL_GROUP_SEQ_ORDER);
	mt76_rmw(dev, MT_DMASHDL_PKT_MAX_SIZE,
		 MT_DMASHDL_PKT_MAX_SIZE_PLE | MT_DMASHDL_PKT_MAX_SIZE_PSE,
		 FIELD_PREP(MT_DMASHDL_PKT_MAX_SIZE_PLE, 1) |
		 FIELD_PREP(MT_DMASHDL_PKT_MAX_SIZE_PSE, 0));
	for (i = 0; i < 5; i++)
		mt76_wr(dev, MT_DMASHDL_GROUP_QUOTA(i),
			FIELD_PREP(MT_DMASHDL_GROUP_QUOTA_MIN, 0x3) |
			FIELD_PREP(MT_DMASHDL_GROUP_QUOTA_MAX, 0xfff));
	for (i = 5; i < 16; i++)
		mt76_wr(dev, MT_DMASHDL_GROUP_QUOTA(i),
			FIELD_PREP(MT_DMASHDL_GROUP_QUOTA_MIN, 0x0) |
			FIELD_PREP(MT_DMASHDL_GROUP_QUOTA_MAX, 0x0));
	mt76_wr(dev, MT_DMASHDL_Q_MAP(0), 0x32013201);
	mt76_wr(dev, MT_DMASHDL_Q_MAP(1), 0x32013201);
	mt76_wr(dev, MT_DMASHDL_Q_MAP(2), 0x55555444);
	mt76_wr(dev, MT_DMASHDL_Q_MAP(3), 0x55555444);

	mt76_wr(dev, MT_DMASHDL_SCHED_SET(0), 0x76540132);
	mt76_wr(dev, MT_DMASHDL_SCHED_SET(1), 0xFEDCBA98);

	mt76_set(dev, MT_WFDMA_DUMMY_CR, MT_WFDMA_NEED_REINIT);
}

static int mt792xu_dma_rx_evt_ep4(struct mt792x_dev *dev)
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

static void mt792xu_epctl_rst_opt(struct mt792x_dev *dev, bool reset)
{
	u32 val;

	/* usb endpoint reset opt
	 * bits[4,9]: out blk ep 4-9
	 * bits[20,21]: in blk ep 4-5
	 * bits[22]: in int ep 6
	 */
	val = mt792xu_uhw_rr(&dev->mt76, MT_SSUSB_EPCTL_CSR_EP_RST_OPT);
	if (reset)
		val |= GENMASK(9, 4) | GENMASK(22, 20);
	else
		val &= ~(GENMASK(9, 4) | GENMASK(22, 20));
	mt792xu_uhw_wr(&dev->mt76, MT_SSUSB_EPCTL_CSR_EP_RST_OPT, val);
}

int mt792xu_dma_init(struct mt792x_dev *dev, bool resume)
{
	int err;

	mt792xu_wfdma_init(dev);

	mt76_clear(dev, MT_UDMA_WLCFG_0, MT_WL_RX_FLUSH);

	mt76_set(dev, MT_UDMA_WLCFG_0,
		 MT_WL_RX_EN | MT_WL_TX_EN |
		 MT_WL_RX_MPSZ_PAD0 | MT_TICK_1US_EN);
	mt76_clear(dev, MT_UDMA_WLCFG_0,
		   MT_WL_RX_AGG_TO | MT_WL_RX_AGG_LMT);
	mt76_clear(dev, MT_UDMA_WLCFG_1, MT_WL_RX_AGG_PKT_LMT);

	if (resume)
		return 0;

	err = mt792xu_dma_rx_evt_ep4(dev);
	if (err)
		return err;

	mt792xu_epctl_rst_opt(dev, false);

	return 0;
}
EXPORT_SYMBOL_GPL(mt792xu_dma_init);

int mt792xu_wfsys_reset(struct mt792x_dev *dev)
{
	u32 val;
	int i;

	mt792xu_epctl_rst_opt(dev, false);

	val = mt792xu_uhw_rr(&dev->mt76, MT_CBTOP_RGU_WF_SUBSYS_RST);
	val |= MT_CBTOP_RGU_WF_SUBSYS_RST_WF_WHOLE_PATH;
	mt792xu_uhw_wr(&dev->mt76, MT_CBTOP_RGU_WF_SUBSYS_RST, val);

	usleep_range(10, 20);

	val = mt792xu_uhw_rr(&dev->mt76, MT_CBTOP_RGU_WF_SUBSYS_RST);
	val &= ~MT_CBTOP_RGU_WF_SUBSYS_RST_WF_WHOLE_PATH;
	mt792xu_uhw_wr(&dev->mt76, MT_CBTOP_RGU_WF_SUBSYS_RST, val);

	mt792xu_uhw_wr(&dev->mt76, MT_UDMA_CONN_INFRA_STATUS_SEL, 0);
	for (i = 0; i < MT792x_WFSYS_INIT_RETRY_COUNT; i++) {
		val = mt792xu_uhw_rr(&dev->mt76, MT_UDMA_CONN_INFRA_STATUS);
		if (val & MT_UDMA_CONN_WFSYS_INIT_DONE)
			break;

		msleep(100);
	}

	if (i == MT792x_WFSYS_INIT_RETRY_COUNT)
		return -ETIMEDOUT;

	return 0;
}
EXPORT_SYMBOL_GPL(mt792xu_wfsys_reset);

int mt792xu_init_reset(struct mt792x_dev *dev)
{
	set_bit(MT76_RESET, &dev->mphy.state);

	wake_up(&dev->mt76.mcu.wait);
	skb_queue_purge(&dev->mt76.mcu.res_q);

	mt76u_stop_rx(&dev->mt76);
	mt76u_stop_tx(&dev->mt76);

	mt792xu_wfsys_reset(dev);

	clear_bit(MT76_RESET, &dev->mphy.state);

	return mt76u_resume_rx(&dev->mt76);
}
EXPORT_SYMBOL_GPL(mt792xu_init_reset);

void mt792xu_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);

	mt76u_stop_tx(&dev->mt76);
	mt792x_stop(hw, false);
}
EXPORT_SYMBOL_GPL(mt792xu_stop);

void mt792xu_disconnect(struct usb_interface *usb_intf)
{
	struct mt792x_dev *dev = usb_get_intfdata(usb_intf);

	cancel_work_sync(&dev->init_work);
	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return;

	mt76_unregister_device(&dev->mt76);
	mt792xu_cleanup(dev);

	usb_set_intfdata(usb_intf, NULL);
	usb_put_dev(interface_to_usbdev(usb_intf));

	mt76_free_device(&dev->mt76);
}
EXPORT_SYMBOL_GPL(mt792xu_disconnect);

MODULE_DESCRIPTION("MediaTek MT792x USB helpers");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
