// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include "main.h"
#include "debug.h"
#include "mac.h"
#include "reg.h"
#include "tx.h"
#include "rx.h"
#include "fw.h"
#include "ps.h"
#include "usb.h"

static bool rtw_switch_usb_mode = true;
module_param_named(switch_usb_mode, rtw_switch_usb_mode, bool, 0644);
MODULE_PARM_DESC(switch_usb_mode,
		 "Set to N to disable switching to USB 3 mode to avoid potential interference in the 2.4 GHz band (default: Y)");

#define RTW_USB_MAX_RXQ_LEN	512

struct rtw_usb_txcb {
	struct rtw_dev *rtwdev;
	struct sk_buff_head tx_ack_queue;
};

static void rtw_usb_fill_tx_checksum(struct rtw_usb *rtwusb,
				     struct sk_buff *skb, int agg_num)
{
	struct rtw_tx_desc *tx_desc = (struct rtw_tx_desc *)skb->data;
	struct rtw_dev *rtwdev = rtwusb->rtwdev;
	struct rtw_tx_pkt_info pkt_info;

	le32p_replace_bits(&tx_desc->w7, agg_num, RTW_TX_DESC_W7_DMA_TXAGG_NUM);
	pkt_info.pkt_offset = le32_get_bits(tx_desc->w1, RTW_TX_DESC_W1_PKT_OFFSET);
	rtw_tx_fill_txdesc_checksum(rtwdev, &pkt_info, skb->data);
}

static void rtw_usb_reg_sec(struct rtw_dev *rtwdev, u32 addr, __le32 *data)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	struct usb_device *udev = rtwusb->udev;
	bool reg_on_section = false;
	u16 t_reg = 0x4e0;
	u8 t_len = 1;
	int status;

	/* There are three sections:
	 * 1. on (0x00~0xFF; 0x1000~0x10FF): this section is always powered on
	 * 2. off (< 0xFE00, excluding "on" section): this section could be
	 *    powered off
	 * 3. local (>= 0xFE00): usb specific registers section
	 */
	if (addr <= 0xff || (addr >= 0x1000 && addr <= 0x10ff))
		reg_on_section = true;

	if (!reg_on_section)
		return;

	status = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 RTW_USB_CMD_REQ, RTW_USB_CMD_WRITE,
				 t_reg, 0, data, t_len, 500);

	if (status != t_len && status != -ENODEV)
		rtw_err(rtwdev, "%s: reg 0x%x, usb write %u fail, status: %d\n",
			__func__, t_reg, t_len, status);
}

static u32 rtw_usb_read(struct rtw_dev *rtwdev, u32 addr, u16 len)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	struct usb_device *udev = rtwusb->udev;
	__le32 *data;
	unsigned long flags;
	int idx, ret;
	static int count;

	spin_lock_irqsave(&rtwusb->usb_lock, flags);

	idx = rtwusb->usb_data_index;
	rtwusb->usb_data_index = (idx + 1) & (RTW_USB_MAX_RXTX_COUNT - 1);

	spin_unlock_irqrestore(&rtwusb->usb_lock, flags);

	data = &rtwusb->usb_data[idx];

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			      RTW_USB_CMD_REQ, RTW_USB_CMD_READ, addr,
			      RTW_USB_VENQT_CMD_IDX, data, len, 1000);
	if (ret < 0 && ret != -ENODEV && count++ < 4)
		rtw_err(rtwdev, "read register 0x%x failed with %d\n",
			addr, ret);

	if (rtwdev->chip->id == RTW_CHIP_TYPE_8822C ||
	    rtwdev->chip->id == RTW_CHIP_TYPE_8822B ||
	    rtwdev->chip->id == RTW_CHIP_TYPE_8821C)
		rtw_usb_reg_sec(rtwdev, addr, data);

	return le32_to_cpu(*data);
}

static u8 rtw_usb_read8(struct rtw_dev *rtwdev, u32 addr)
{
	return (u8)rtw_usb_read(rtwdev, addr, 1);
}

static u16 rtw_usb_read16(struct rtw_dev *rtwdev, u32 addr)
{
	return (u16)rtw_usb_read(rtwdev, addr, 2);
}

static u32 rtw_usb_read32(struct rtw_dev *rtwdev, u32 addr)
{
	return (u32)rtw_usb_read(rtwdev, addr, 4);
}

static void rtw_usb_write(struct rtw_dev *rtwdev, u32 addr, u32 val, int len)
{
	struct rtw_usb *rtwusb = (struct rtw_usb *)rtwdev->priv;
	struct usb_device *udev = rtwusb->udev;
	unsigned long flags;
	__le32 *data;
	int idx, ret;
	static int count;

	spin_lock_irqsave(&rtwusb->usb_lock, flags);

	idx = rtwusb->usb_data_index;
	rtwusb->usb_data_index = (idx + 1) & (RTW_USB_MAX_RXTX_COUNT - 1);

	spin_unlock_irqrestore(&rtwusb->usb_lock, flags);

	data = &rtwusb->usb_data[idx];

	*data = cpu_to_le32(val);

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      RTW_USB_CMD_REQ, RTW_USB_CMD_WRITE,
			      addr, 0, data, len, 30000);
	if (ret < 0 && ret != -ENODEV && count++ < 4)
		rtw_err(rtwdev, "write register 0x%x failed with %d\n",
			addr, ret);

	if (rtwdev->chip->id == RTW_CHIP_TYPE_8822C ||
	    rtwdev->chip->id == RTW_CHIP_TYPE_8822B ||
	    rtwdev->chip->id == RTW_CHIP_TYPE_8821C)
		rtw_usb_reg_sec(rtwdev, addr, data);
}

static void rtw_usb_write8(struct rtw_dev *rtwdev, u32 addr, u8 val)
{
	rtw_usb_write(rtwdev, addr, val, 1);
}

static void rtw_usb_write16(struct rtw_dev *rtwdev, u32 addr, u16 val)
{
	rtw_usb_write(rtwdev, addr, val, 2);
}

static void rtw_usb_write32(struct rtw_dev *rtwdev, u32 addr, u32 val)
{
	rtw_usb_write(rtwdev, addr, val, 4);
}

static int dma_mapping_to_ep(enum rtw_dma_mapping dma_mapping)
{
	switch (dma_mapping) {
	case RTW_DMA_MAPPING_HIGH:
		return 0;
	case RTW_DMA_MAPPING_NORMAL:
		return 1;
	case RTW_DMA_MAPPING_LOW:
		return 2;
	case RTW_DMA_MAPPING_EXTRA:
		return 3;
	default:
		return -EINVAL;
	}
}

static int rtw_usb_parse(struct rtw_dev *rtwdev,
			 struct usb_interface *interface)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	struct usb_host_interface *host_interface = &interface->altsetting[0];
	struct usb_interface_descriptor *interface_desc = &host_interface->desc;
	struct usb_endpoint_descriptor *endpoint;
	int num_out_pipes = 0;
	int i;
	u8 num;
	const struct rtw_chip_info *chip = rtwdev->chip;
	const struct rtw_rqpn *rqpn;

	for (i = 0; i < interface_desc->bNumEndpoints; i++) {
		endpoint = &host_interface->endpoint[i].desc;
		num = usb_endpoint_num(endpoint);

		if (usb_endpoint_dir_in(endpoint) &&
		    usb_endpoint_xfer_bulk(endpoint)) {
			if (rtwusb->pipe_in) {
				rtw_err(rtwdev, "IN pipes overflow\n");
				return -EINVAL;
			}

			rtwusb->pipe_in = num;
		}

		if (usb_endpoint_dir_in(endpoint) &&
		    usb_endpoint_xfer_int(endpoint)) {
			if (rtwusb->pipe_interrupt) {
				rtw_err(rtwdev, "INT pipes overflow\n");
				return -EINVAL;
			}

			rtwusb->pipe_interrupt = num;
		}

		if (usb_endpoint_dir_out(endpoint) &&
		    usb_endpoint_xfer_bulk(endpoint)) {
			if (num_out_pipes >= ARRAY_SIZE(rtwusb->out_ep)) {
				rtw_err(rtwdev, "OUT pipes overflow\n");
				return -EINVAL;
			}

			rtwusb->out_ep[num_out_pipes++] = num;
		}
	}

	rtwdev->hci.bulkout_num = num_out_pipes;

	if (num_out_pipes < 1 || num_out_pipes > 4) {
		rtw_err(rtwdev, "invalid number of endpoints %d\n", num_out_pipes);
		return -EINVAL;
	}

	rqpn = &chip->rqpn_table[num_out_pipes];

	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID0] = dma_mapping_to_ep(rqpn->dma_map_be);
	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID1] = dma_mapping_to_ep(rqpn->dma_map_bk);
	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID2] = dma_mapping_to_ep(rqpn->dma_map_bk);
	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID3] = dma_mapping_to_ep(rqpn->dma_map_be);
	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID4] = dma_mapping_to_ep(rqpn->dma_map_vi);
	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID5] = dma_mapping_to_ep(rqpn->dma_map_vi);
	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID6] = dma_mapping_to_ep(rqpn->dma_map_vo);
	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID7] = dma_mapping_to_ep(rqpn->dma_map_vo);
	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID8] = -EINVAL;
	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID9] = -EINVAL;
	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID10] = -EINVAL;
	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID11] = -EINVAL;
	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID12] = -EINVAL;
	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID13] = -EINVAL;
	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID14] = -EINVAL;
	rtwusb->qsel_to_ep[TX_DESC_QSEL_TID15] = -EINVAL;
	rtwusb->qsel_to_ep[TX_DESC_QSEL_BEACON] = dma_mapping_to_ep(rqpn->dma_map_hi);
	rtwusb->qsel_to_ep[TX_DESC_QSEL_HIGH] = dma_mapping_to_ep(rqpn->dma_map_hi);
	rtwusb->qsel_to_ep[TX_DESC_QSEL_MGMT] = dma_mapping_to_ep(rqpn->dma_map_mg);
	rtwusb->qsel_to_ep[TX_DESC_QSEL_H2C] = dma_mapping_to_ep(rqpn->dma_map_hi);

	return 0;
}

static void rtw_usb_write_port_tx_complete(struct urb *urb)
{
	struct rtw_usb_txcb *txcb = urb->context;
	struct rtw_dev *rtwdev = txcb->rtwdev;
	struct ieee80211_hw *hw = rtwdev->hw;

	while (true) {
		struct sk_buff *skb = skb_dequeue(&txcb->tx_ack_queue);
		struct ieee80211_tx_info *info;
		struct rtw_usb_tx_data *tx_data;

		if (!skb)
			break;

		info = IEEE80211_SKB_CB(skb);
		tx_data = rtw_usb_get_tx_data(skb);

		skb_pull(skb, rtwdev->chip->tx_pkt_desc_sz);

		/* enqueue to wait for tx report */
		if (info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS) {
			rtw_tx_report_enqueue(rtwdev, skb, tx_data->sn);
			continue;
		}

		/* always ACK for others, then they won't be marked as drop */
		ieee80211_tx_info_clear_status(info);
		if (info->flags & IEEE80211_TX_CTL_NO_ACK)
			info->flags |= IEEE80211_TX_STAT_NOACK_TRANSMITTED;
		else
			info->flags |= IEEE80211_TX_STAT_ACK;

		ieee80211_tx_status_irqsafe(hw, skb);
	}

	kfree(txcb);
}

static int qsel_to_ep(struct rtw_usb *rtwusb, unsigned int qsel)
{
	if (qsel >= ARRAY_SIZE(rtwusb->qsel_to_ep))
		return -EINVAL;

	return rtwusb->qsel_to_ep[qsel];
}

static int rtw_usb_write_port(struct rtw_dev *rtwdev, u8 qsel, struct sk_buff *skb,
			      usb_complete_t cb, void *context)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	struct usb_device *usbd = rtwusb->udev;
	struct urb *urb;
	unsigned int pipe;
	int ret;
	int ep = qsel_to_ep(rtwusb, qsel);

	if (ep < 0)
		return ep;

	pipe = usb_sndbulkpipe(usbd, rtwusb->out_ep[ep]);
	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		return -ENOMEM;

	usb_fill_bulk_urb(urb, usbd, pipe, skb->data, skb->len, cb, context);
	urb->transfer_flags |= URB_ZERO_PACKET;
	ret = usb_submit_urb(urb, GFP_ATOMIC);

	usb_free_urb(urb);

	return ret;
}

static bool rtw_usb_tx_agg_skb(struct rtw_usb *rtwusb, struct sk_buff_head *list)
{
	struct rtw_dev *rtwdev = rtwusb->rtwdev;
	struct rtw_tx_desc *tx_desc;
	struct rtw_usb_txcb *txcb;
	struct sk_buff *skb_head;
	struct sk_buff *skb_iter;
	int agg_num = 0;
	unsigned int align_next = 0;
	u8 qsel;

	if (skb_queue_empty(list))
		return false;

	txcb = kmalloc(sizeof(*txcb), GFP_ATOMIC);
	if (!txcb)
		return false;

	txcb->rtwdev = rtwdev;
	skb_queue_head_init(&txcb->tx_ack_queue);

	skb_iter = skb_dequeue(list);

	if (skb_queue_empty(list)) {
		skb_head = skb_iter;
		goto queue;
	}

	skb_head = dev_alloc_skb(RTW_USB_MAX_XMITBUF_SZ);
	if (!skb_head) {
		skb_head = skb_iter;
		goto queue;
	}

	while (skb_iter) {
		unsigned long flags;

		skb_put(skb_head, align_next);
		skb_put_data(skb_head, skb_iter->data, skb_iter->len);

		align_next = ALIGN(skb_iter->len, 8) - skb_iter->len;

		agg_num++;

		skb_queue_tail(&txcb->tx_ack_queue, skb_iter);

		spin_lock_irqsave(&list->lock, flags);

		skb_iter = skb_peek(list);

		if (skb_iter &&
		    skb_iter->len + skb_head->len <= RTW_USB_MAX_XMITBUF_SZ &&
		    agg_num < rtwdev->chip->usb_tx_agg_desc_num)
			__skb_unlink(skb_iter, list);
		else
			skb_iter = NULL;
		spin_unlock_irqrestore(&list->lock, flags);
	}

	if (agg_num > 1)
		rtw_usb_fill_tx_checksum(rtwusb, skb_head, agg_num);

queue:
	skb_queue_tail(&txcb->tx_ack_queue, skb_head);
	tx_desc = (struct rtw_tx_desc *)skb_head->data;
	qsel = le32_get_bits(tx_desc->w1, RTW_TX_DESC_W1_QSEL);

	rtw_usb_write_port(rtwdev, qsel, skb_head, rtw_usb_write_port_tx_complete, txcb);

	return true;
}

static void rtw_usb_tx_handler(struct work_struct *work)
{
	struct rtw_usb *rtwusb = container_of(work, struct rtw_usb, tx_work);
	int i, limit;

	for (i = ARRAY_SIZE(rtwusb->tx_queue) - 1; i >= 0; i--) {
		for (limit = 0; limit < 200; limit++) {
			struct sk_buff_head *list = &rtwusb->tx_queue[i];

			if (!rtw_usb_tx_agg_skb(rtwusb, list))
				break;
		}
	}
}

static void rtw_usb_tx_queue_purge(struct rtw_usb *rtwusb)
{
	struct rtw_dev *rtwdev = rtwusb->rtwdev;
	int i;

	for (i = 0; i < ARRAY_SIZE(rtwusb->tx_queue); i++)
		ieee80211_purge_tx_queue(rtwdev->hw, &rtwusb->tx_queue[i]);
}

static void rtw_usb_write_port_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;

	dev_kfree_skb_any(skb);
}

static int rtw_usb_write_data(struct rtw_dev *rtwdev,
			      struct rtw_tx_pkt_info *pkt_info,
			      u8 *buf)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct sk_buff *skb;
	unsigned int size;
	u8 qsel;
	int ret = 0;

	size = pkt_info->tx_pkt_size;
	qsel = pkt_info->qsel;

	skb = dev_alloc_skb(chip->tx_pkt_desc_sz + size);
	if (unlikely(!skb))
		return -ENOMEM;

	skb_reserve(skb, chip->tx_pkt_desc_sz);
	skb_put_data(skb, buf, size);
	skb_push(skb, chip->tx_pkt_desc_sz);
	memset(skb->data, 0, chip->tx_pkt_desc_sz);
	rtw_tx_fill_tx_desc(rtwdev, pkt_info, skb);
	rtw_tx_fill_txdesc_checksum(rtwdev, pkt_info, skb->data);

	ret = rtw_usb_write_port(rtwdev, qsel, skb,
				 rtw_usb_write_port_complete, skb);
	if (unlikely(ret))
		rtw_err(rtwdev, "failed to do USB write, ret=%d\n", ret);

	return ret;
}

static int rtw_usb_write_data_rsvd_page(struct rtw_dev *rtwdev, u8 *buf,
					u32 size)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_tx_pkt_info pkt_info = {0};

	pkt_info.tx_pkt_size = size;
	pkt_info.qsel = TX_DESC_QSEL_BEACON;
	pkt_info.offset = chip->tx_pkt_desc_sz;
	pkt_info.ls = true;

	return rtw_usb_write_data(rtwdev, &pkt_info, buf);
}

static int rtw_usb_write_data_h2c(struct rtw_dev *rtwdev, u8 *buf, u32 size)
{
	struct rtw_tx_pkt_info pkt_info = {0};

	pkt_info.tx_pkt_size = size;
	pkt_info.qsel = TX_DESC_QSEL_H2C;

	return rtw_usb_write_data(rtwdev, &pkt_info, buf);
}

static u8 rtw_usb_tx_queue_mapping_to_qsel(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	__le16 fc = hdr->frame_control;
	u8 qsel;

	if (unlikely(ieee80211_is_mgmt(fc) || ieee80211_is_ctl(fc)))
		qsel = TX_DESC_QSEL_MGMT;
	else if (is_broadcast_ether_addr(hdr->addr1) ||
		 is_multicast_ether_addr(hdr->addr1))
		qsel = TX_DESC_QSEL_HIGH;
	else if (skb_get_queue_mapping(skb) <= IEEE80211_AC_BK)
		qsel = skb->priority;
	else
		qsel = TX_DESC_QSEL_BEACON;

	return qsel;
}

static int rtw_usb_tx_write(struct rtw_dev *rtwdev,
			    struct rtw_tx_pkt_info *pkt_info,
			    struct sk_buff *skb)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_usb_tx_data *tx_data;
	u8 *pkt_desc;
	int ep;

	pkt_info->qsel = rtw_usb_tx_queue_mapping_to_qsel(skb);
	pkt_desc = skb_push(skb, chip->tx_pkt_desc_sz);
	memset(pkt_desc, 0, chip->tx_pkt_desc_sz);
	ep = qsel_to_ep(rtwusb, pkt_info->qsel);
	rtw_tx_fill_tx_desc(rtwdev, pkt_info, skb);
	rtw_tx_fill_txdesc_checksum(rtwdev, pkt_info, skb->data);
	tx_data = rtw_usb_get_tx_data(skb);
	tx_data->sn = pkt_info->sn;

	skb_queue_tail(&rtwusb->tx_queue[ep], skb);

	return 0;
}

static void rtw_usb_tx_kick_off(struct rtw_dev *rtwdev)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);

	queue_work(rtwusb->txwq, &rtwusb->tx_work);
}

static void rtw_usb_rx_handler(struct work_struct *work)
{
	struct rtw_usb *rtwusb = container_of(work, struct rtw_usb, rx_work);
	struct rtw_dev *rtwdev = rtwusb->rtwdev;
	struct ieee80211_rx_status rx_status;
	struct rtw_rx_pkt_stat pkt_stat;
	struct sk_buff *rx_skb;
	struct sk_buff *skb;
	u32 pkt_desc_sz = rtwdev->chip->rx_pkt_desc_sz;
	u32 max_skb_len = pkt_desc_sz + PHY_STATUS_SIZE * 8 +
			  IEEE80211_MAX_MPDU_LEN_VHT_11454;
	u32 pkt_offset, next_pkt, skb_len;
	u8 *rx_desc;
	int limit;

	for (limit = 0; limit < 200; limit++) {
		rx_skb = skb_dequeue(&rtwusb->rx_queue);
		if (!rx_skb)
			break;

		if (skb_queue_len(&rtwusb->rx_queue) >= RTW_USB_MAX_RXQ_LEN) {
			dev_dbg_ratelimited(rtwdev->dev, "failed to get rx_queue, overflow\n");
			dev_kfree_skb_any(rx_skb);
			continue;
		}

		rx_desc = rx_skb->data;

		do {
			rtw_rx_query_rx_desc(rtwdev, rx_desc, &pkt_stat,
					     &rx_status);
			pkt_offset = pkt_desc_sz + pkt_stat.drv_info_sz +
				     pkt_stat.shift;

			skb_len = pkt_stat.pkt_len + pkt_offset;
			if (skb_len > max_skb_len) {
				rtw_dbg(rtwdev, RTW_DBG_USB,
					"skipping too big packet: %u\n",
					skb_len);
				goto skip_packet;
			}

			skb = alloc_skb(skb_len, GFP_ATOMIC);
			if (!skb) {
				rtw_dbg(rtwdev, RTW_DBG_USB,
					"failed to allocate RX skb of size %u\n",
					skb_len);
				goto skip_packet;
			}

			skb_put_data(skb, rx_desc, skb_len);

			if (pkt_stat.is_c2h) {
				rtw_fw_c2h_cmd_rx_irqsafe(rtwdev, pkt_offset, skb);
			} else {
				skb_pull(skb, pkt_offset);
				rtw_update_rx_freq_for_invalid(rtwdev, skb,
							       &rx_status,
							       &pkt_stat);
				rtw_rx_stats(rtwdev, pkt_stat.vif, skb);
				memcpy(skb->cb, &rx_status, sizeof(rx_status));
				ieee80211_rx_irqsafe(rtwdev->hw, skb);
			}

skip_packet:
			next_pkt = round_up(skb_len, 8);
			rx_desc += next_pkt;
		} while (rx_desc + pkt_desc_sz < rx_skb->data + rx_skb->len);

		if (skb_queue_len(&rtwusb->rx_free_queue) >= RTW_USB_RX_SKB_NUM)
			dev_kfree_skb_any(rx_skb);
		else
			skb_queue_tail(&rtwusb->rx_free_queue, rx_skb);
	}
}

static void rtw_usb_read_port_complete(struct urb *urb);

static void rtw_usb_rx_resubmit(struct rtw_usb *rtwusb,
				struct rx_usb_ctrl_block *rxcb,
				gfp_t gfp)
{
	struct rtw_dev *rtwdev = rtwusb->rtwdev;
	struct sk_buff *rx_skb;
	int error;

	rx_skb = skb_dequeue(&rtwusb->rx_free_queue);
	if (!rx_skb)
		rx_skb = alloc_skb(RTW_USB_MAX_RECVBUF_SZ, gfp);

	if (!rx_skb)
		goto try_later;

	skb_reset_tail_pointer(rx_skb);
	rx_skb->len = 0;

	rxcb->rx_skb = rx_skb;

	usb_fill_bulk_urb(rxcb->rx_urb, rtwusb->udev,
			  usb_rcvbulkpipe(rtwusb->udev, rtwusb->pipe_in),
			  rxcb->rx_skb->data, RTW_USB_MAX_RECVBUF_SZ,
			  rtw_usb_read_port_complete, rxcb);

	error = usb_submit_urb(rxcb->rx_urb, gfp);
	if (error) {
		skb_queue_tail(&rtwusb->rx_free_queue, rxcb->rx_skb);

		if (error != -ENODEV)
			rtw_err(rtwdev, "Err sending rx data urb %d\n",
				error);

		if (error == -ENOMEM)
			goto try_later;
	}

	return;

try_later:
	rxcb->rx_skb = NULL;
	queue_work(rtwusb->rxwq, &rtwusb->rx_urb_work);
}

static void rtw_usb_rx_resubmit_work(struct work_struct *work)
{
	struct rtw_usb *rtwusb = container_of(work, struct rtw_usb, rx_urb_work);
	struct rx_usb_ctrl_block *rxcb;
	int i;

	for (i = 0; i < RTW_USB_RXCB_NUM; i++) {
		rxcb = &rtwusb->rx_cb[i];

		if (!rxcb->rx_skb)
			rtw_usb_rx_resubmit(rtwusb, rxcb, GFP_ATOMIC);
	}
}

static void rtw_usb_read_port_complete(struct urb *urb)
{
	struct rx_usb_ctrl_block *rxcb = urb->context;
	struct rtw_dev *rtwdev = rxcb->rtwdev;
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	struct sk_buff *skb = rxcb->rx_skb;

	if (urb->status == 0) {
		if (urb->actual_length >= RTW_USB_MAX_RECVBUF_SZ ||
		    urb->actual_length < 24) {
			rtw_err(rtwdev, "failed to get urb length:%d\n",
				urb->actual_length);
			skb_queue_tail(&rtwusb->rx_free_queue, skb);
		} else {
			skb_put(skb, urb->actual_length);
			skb_queue_tail(&rtwusb->rx_queue, skb);
			queue_work(rtwusb->rxwq, &rtwusb->rx_work);
		}
		rtw_usb_rx_resubmit(rtwusb, rxcb, GFP_ATOMIC);
	} else {
		skb_queue_tail(&rtwusb->rx_free_queue, skb);

		switch (urb->status) {
		case -EINVAL:
		case -EPIPE:
		case -ENODEV:
		case -ESHUTDOWN:
		case -ENOENT:
		case -EPROTO:
		case -EILSEQ:
		case -ETIME:
		case -ECOMM:
		case -EOVERFLOW:
		case -EINPROGRESS:
			break;
		default:
			rtw_err(rtwdev, "status %d\n", urb->status);
			break;
		}
	}
}

static void rtw_usb_cancel_rx_bufs(struct rtw_usb *rtwusb)
{
	struct rx_usb_ctrl_block *rxcb;
	int i;

	for (i = 0; i < RTW_USB_RXCB_NUM; i++) {
		rxcb = &rtwusb->rx_cb[i];
		usb_kill_urb(rxcb->rx_urb);
	}
}

static void rtw_usb_free_rx_bufs(struct rtw_usb *rtwusb)
{
	struct rx_usb_ctrl_block *rxcb;
	int i;

	for (i = 0; i < RTW_USB_RXCB_NUM; i++) {
		rxcb = &rtwusb->rx_cb[i];
		usb_kill_urb(rxcb->rx_urb);
		usb_free_urb(rxcb->rx_urb);
	}
}

static int rtw_usb_alloc_rx_bufs(struct rtw_usb *rtwusb)
{
	int i;

	for (i = 0; i < RTW_USB_RXCB_NUM; i++) {
		struct rx_usb_ctrl_block *rxcb = &rtwusb->rx_cb[i];

		rxcb->rtwdev = rtwusb->rtwdev;
		rxcb->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!rxcb->rx_urb)
			goto err;
	}

	return 0;
err:
	rtw_usb_free_rx_bufs(rtwusb);
	return -ENOMEM;
}

static int rtw_usb_setup(struct rtw_dev *rtwdev)
{
	/* empty function for rtw_hci_ops */
	return 0;
}

static int rtw_usb_start(struct rtw_dev *rtwdev)
{
	return 0;
}

static void rtw_usb_stop(struct rtw_dev *rtwdev)
{
}

static void rtw_usb_deep_ps(struct rtw_dev *rtwdev, bool enter)
{
	/* empty function for rtw_hci_ops */
}

static void rtw_usb_link_ps(struct rtw_dev *rtwdev, bool enter)
{
	/* empty function for rtw_hci_ops */
}

static void rtw_usb_init_burst_pkt_len(struct rtw_dev *rtwdev)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	enum usb_device_speed speed = rtwusb->udev->speed;
	u8 rxdma, burst_size;

	rxdma = BIT_DMA_BURST_CNT | BIT_DMA_MODE;

	if (speed == USB_SPEED_SUPER)
		burst_size = BIT_DMA_BURST_SIZE_1024;
	else if (speed == USB_SPEED_HIGH)
		burst_size = BIT_DMA_BURST_SIZE_512;
	else
		burst_size = BIT_DMA_BURST_SIZE_64;

	u8p_replace_bits(&rxdma, burst_size, BIT_DMA_BURST_SIZE);

	rtw_write8(rtwdev, REG_RXDMA_MODE, rxdma);
	rtw_write16_set(rtwdev, REG_TXDMA_OFFSET_CHK, BIT_DROP_DATA_EN);
}

static void rtw_usb_interface_cfg(struct rtw_dev *rtwdev)
{
	rtw_usb_init_burst_pkt_len(rtwdev);
}

static void rtw_usb_dynamic_rx_agg_v1(struct rtw_dev *rtwdev, bool enable)
{
	u8 size, timeout;
	u16 val16;

	rtw_write8_set(rtwdev, REG_TXDMA_PQ_MAP, BIT_RXDMA_AGG_EN);
	rtw_write8_clr(rtwdev, REG_RXDMA_AGG_PG_TH + 3, BIT(7));

	if (enable) {
		size = 0x5;
		timeout = 0x20;
	} else {
		size = 0x0;
		timeout = 0x1;
	}
	val16 = u16_encode_bits(size, BIT_RXDMA_AGG_PG_TH) |
		u16_encode_bits(timeout, BIT_DMA_AGG_TO_V1);

	rtw_write16(rtwdev, REG_RXDMA_AGG_PG_TH, val16);
}

static void rtw_usb_dynamic_rx_agg_v2(struct rtw_dev *rtwdev, bool enable)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	u8 size, timeout;
	u16 val16;

	if (!enable) {
		size = 0x0;
		timeout = 0x1;
	} else if (rtwusb->udev->speed == USB_SPEED_SUPER) {
		size = 0x6;
		timeout = 0x1a;
	} else {
		size = 0x5;
		timeout = 0x20;
	}

	val16 = u16_encode_bits(size, BIT_RXDMA_AGG_PG_TH) |
		u16_encode_bits(timeout, BIT_DMA_AGG_TO_V1);

	rtw_write16(rtwdev, REG_RXDMA_AGG_PG_TH, val16);
	rtw_write8_set(rtwdev, REG_TXDMA_PQ_MAP, BIT_RXDMA_AGG_EN);
}

static void rtw_usb_dynamic_rx_agg(struct rtw_dev *rtwdev, bool enable)
{
	switch (rtwdev->chip->id) {
	case RTW_CHIP_TYPE_8822C:
	case RTW_CHIP_TYPE_8822B:
	case RTW_CHIP_TYPE_8821C:
	case RTW_CHIP_TYPE_8814A:
		rtw_usb_dynamic_rx_agg_v1(rtwdev, enable);
		break;
	case RTW_CHIP_TYPE_8821A:
	case RTW_CHIP_TYPE_8812A:
		rtw_usb_dynamic_rx_agg_v2(rtwdev, enable);
		break;
	case RTW_CHIP_TYPE_8723D:
		/* Doesn't like aggregation. */
		break;
	case RTW_CHIP_TYPE_8703B:
		/* Likely not found in USB devices. */
		break;
	}
}

static const struct rtw_hci_ops rtw_usb_ops = {
	.tx_write = rtw_usb_tx_write,
	.tx_kick_off = rtw_usb_tx_kick_off,
	.setup = rtw_usb_setup,
	.start = rtw_usb_start,
	.stop = rtw_usb_stop,
	.deep_ps = rtw_usb_deep_ps,
	.link_ps = rtw_usb_link_ps,
	.interface_cfg = rtw_usb_interface_cfg,
	.dynamic_rx_agg = rtw_usb_dynamic_rx_agg,

	.write8  = rtw_usb_write8,
	.write16 = rtw_usb_write16,
	.write32 = rtw_usb_write32,
	.read8	= rtw_usb_read8,
	.read16 = rtw_usb_read16,
	.read32 = rtw_usb_read32,

	.write_data_rsvd_page = rtw_usb_write_data_rsvd_page,
	.write_data_h2c = rtw_usb_write_data_h2c,
};

static int rtw_usb_init_rx(struct rtw_dev *rtwdev)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	struct sk_buff *rx_skb;
	int i;

	rtwusb->rxwq = alloc_workqueue("rtw88_usb: rx wq", WQ_BH, 0);
	if (!rtwusb->rxwq) {
		rtw_err(rtwdev, "failed to create RX work queue\n");
		return -ENOMEM;
	}

	skb_queue_head_init(&rtwusb->rx_queue);
	skb_queue_head_init(&rtwusb->rx_free_queue);

	INIT_WORK(&rtwusb->rx_work, rtw_usb_rx_handler);
	INIT_WORK(&rtwusb->rx_urb_work, rtw_usb_rx_resubmit_work);

	for (i = 0; i < RTW_USB_RX_SKB_NUM; i++) {
		rx_skb = alloc_skb(RTW_USB_MAX_RECVBUF_SZ, GFP_KERNEL);
		if (rx_skb)
			skb_queue_tail(&rtwusb->rx_free_queue, rx_skb);
	}

	return 0;
}

static void rtw_usb_setup_rx(struct rtw_dev *rtwdev)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	int i;

	for (i = 0; i < RTW_USB_RXCB_NUM; i++) {
		struct rx_usb_ctrl_block *rxcb = &rtwusb->rx_cb[i];

		rtw_usb_rx_resubmit(rtwusb, rxcb, GFP_KERNEL);
	}
}

static void rtw_usb_deinit_rx(struct rtw_dev *rtwdev)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);

	skb_queue_purge(&rtwusb->rx_queue);

	destroy_workqueue(rtwusb->rxwq);

	skb_queue_purge(&rtwusb->rx_free_queue);
}

static int rtw_usb_init_tx(struct rtw_dev *rtwdev)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	int i;

	rtwusb->txwq = create_singlethread_workqueue("rtw88_usb: tx wq");
	if (!rtwusb->txwq) {
		rtw_err(rtwdev, "failed to create TX work queue\n");
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(rtwusb->tx_queue); i++)
		skb_queue_head_init(&rtwusb->tx_queue[i]);

	INIT_WORK(&rtwusb->tx_work, rtw_usb_tx_handler);

	return 0;
}

static void rtw_usb_deinit_tx(struct rtw_dev *rtwdev)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);

	destroy_workqueue(rtwusb->txwq);
	rtw_usb_tx_queue_purge(rtwusb);
}

static int rtw_usb_intf_init(struct rtw_dev *rtwdev,
			     struct usb_interface *intf)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	struct usb_device *udev = usb_get_dev(interface_to_usbdev(intf));
	int ret;

	rtwusb->udev = udev;
	ret = rtw_usb_parse(rtwdev, intf);
	if (ret)
		return ret;

	rtwusb->usb_data = kcalloc(RTW_USB_MAX_RXTX_COUNT, sizeof(u32),
				   GFP_KERNEL);
	if (!rtwusb->usb_data)
		return -ENOMEM;

	usb_set_intfdata(intf, rtwdev->hw);

	SET_IEEE80211_DEV(rtwdev->hw, &intf->dev);
	spin_lock_init(&rtwusb->usb_lock);

	return 0;
}

static void rtw_usb_intf_deinit(struct rtw_dev *rtwdev,
				struct usb_interface *intf)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);

	usb_put_dev(rtwusb->udev);
	kfree(rtwusb->usb_data);
	usb_set_intfdata(intf, NULL);
}

static int rtw_usb_switch_mode_old(struct rtw_dev *rtwdev)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	enum usb_device_speed cur_speed = rtwusb->udev->speed;
	u8 hci_opt;

	if (cur_speed == USB_SPEED_HIGH) {
		hci_opt = rtw_read8(rtwdev, REG_HCI_OPT_CTRL);

		if ((hci_opt & (BIT(2) | BIT(3))) != BIT(3)) {
			rtw_write8(rtwdev, REG_HCI_OPT_CTRL, 0x8);
			rtw_write8(rtwdev, REG_SYS_SDIO_CTRL, 0x2);
			rtw_write8(rtwdev, REG_ACLK_MON, 0x1);
			rtw_write8(rtwdev, 0x3d, 0x3);
			/* usb disconnect */
			rtw_write8(rtwdev, REG_SYS_PW_CTRL + 1, 0x80);
			return 1;
		}
	} else if (cur_speed == USB_SPEED_SUPER) {
		rtw_write8_clr(rtwdev, REG_SYS_SDIO_CTRL, BIT(1));
		rtw_write8_clr(rtwdev, REG_ACLK_MON, BIT(0));
	}

	return 0;
}

static int rtw_usb_switch_mode_new(struct rtw_dev *rtwdev)
{
	enum usb_device_speed cur_speed;
	u8 id = rtwdev->chip->id;
	bool can_switch;
	u32 pad_ctrl2;

	if (rtw_read8(rtwdev, REG_SYS_CFG2 + 3) == 0x20)
		cur_speed = USB_SPEED_SUPER;
	else
		cur_speed = USB_SPEED_HIGH;

	if (cur_speed == USB_SPEED_SUPER)
		return 0;

	pad_ctrl2 = rtw_read32(rtwdev, REG_PAD_CTRL2);

	can_switch = !!(pad_ctrl2 & (BIT_MASK_USB23_SW_MODE_V1 |
				     BIT_USB3_USB2_TRANSITION));

	if (!can_switch) {
		rtw_dbg(rtwdev, RTW_DBG_USB,
			"Switching to USB 3 mode unsupported by the chip\n");
		return 0;
	}

	/* At this point cur_speed is USB_SPEED_HIGH. If we already tried
	 * to switch don't try again - it's a USB 2 port.
	 */
	if (u32_get_bits(pad_ctrl2, BIT_MASK_USB23_SW_MODE_V1) == BIT_USB_MODE_U3)
		return 0;

	/* Enable IO wrapper timeout */
	if (id == RTW_CHIP_TYPE_8822B || id == RTW_CHIP_TYPE_8821C)
		rtw_write8_clr(rtwdev, REG_SW_MDIO + 3, BIT(0));

	u32p_replace_bits(&pad_ctrl2, BIT_USB_MODE_U3, BIT_MASK_USB23_SW_MODE_V1);
	pad_ctrl2 |= BIT_RSM_EN_V1;

	rtw_write32(rtwdev, REG_PAD_CTRL2, pad_ctrl2);
	rtw_write8(rtwdev, REG_PAD_CTRL2 + 1, 4);

	rtw_write16_set(rtwdev, REG_SYS_PW_CTRL, BIT_APFM_OFFMAC);
	usleep_range(1000, 1001);
	rtw_write32_set(rtwdev, REG_PAD_CTRL2, BIT_NO_PDN_CHIPOFF_V1);

	return 1;
}

static bool rtw_usb3_chip_old(u8 chip_id)
{
	return chip_id == RTW_CHIP_TYPE_8812A ||
	       chip_id == RTW_CHIP_TYPE_8814A;
}

static bool rtw_usb3_chip_new(u8 chip_id)
{
	return chip_id == RTW_CHIP_TYPE_8822C ||
	       chip_id == RTW_CHIP_TYPE_8822B;
}

static int rtw_usb_switch_mode(struct rtw_dev *rtwdev)
{
	u8 id = rtwdev->chip->id;

	if (!rtw_usb3_chip_new(id) && !rtw_usb3_chip_old(id))
		return 0;

	if (!rtwdev->efuse.usb_mode_switch) {
		rtw_dbg(rtwdev, RTW_DBG_USB,
			"Switching to USB 3 mode disabled by chip's efuse\n");
		return 0;
	}

	if (!rtw_switch_usb_mode) {
		rtw_dbg(rtwdev, RTW_DBG_USB,
			"Switching to USB 3 mode disabled by module parameter\n");
		return 0;
	}

	if (rtw_usb3_chip_old(id))
		return rtw_usb_switch_mode_old(rtwdev);
	else
		return rtw_usb_switch_mode_new(rtwdev);
}

#define USB_REG_PAGE	0xf4
#define USB_PHY_PAGE0	0x9b
#define USB_PHY_PAGE1	0xbb

static void rtw_usb_phy_write(struct rtw_dev *rtwdev, u8 addr, u16 data,
			      enum usb_device_speed speed)
{
	if (speed == USB_SPEED_SUPER) {
		rtw_write8(rtwdev, REG_USB3_PHY_DAT_L, data & 0xff);
		rtw_write8(rtwdev, REG_USB3_PHY_DAT_H, data >> 8);
		rtw_write8(rtwdev, REG_USB3_PHY_ADR, addr | BIT_USB3_PHY_ADR_WR);
	} else if (speed == USB_SPEED_HIGH) {
		rtw_write8(rtwdev, REG_USB2_PHY_DAT, data);
		rtw_write8(rtwdev, REG_USB2_PHY_ADR, addr);
		rtw_write8(rtwdev, REG_USB2_PHY_CMD, BIT_USB2_PHY_CMD_TRG);
	}
}

static void rtw_usb_page_switch(struct rtw_dev *rtwdev,
				enum usb_device_speed speed, u8 page)
{
	if (speed == USB_SPEED_SUPER)
		return;

	rtw_usb_phy_write(rtwdev, USB_REG_PAGE, page, speed);
}

static void rtw_usb_phy_cfg(struct rtw_dev *rtwdev,
			    enum usb_device_speed speed)
{
	const struct rtw_intf_phy_para *para = NULL;
	u16 offset;

	if (!rtwdev->chip->intf_table)
		return;

	if (speed == USB_SPEED_SUPER)
		para = rtwdev->chip->intf_table->usb3_para;
	else if (speed == USB_SPEED_HIGH)
		para = rtwdev->chip->intf_table->usb2_para;

	if (!para)
		return;

	for ( ; para->offset != 0xffff; para++) {
		if (!(para->cut_mask & BIT(rtwdev->hal.cut_version)))
			continue;

		offset = para->offset;

		if (para->ip_sel == RTW_IP_SEL_MAC) {
			rtw_write8(rtwdev, offset, para->value);
		} else {
			if (offset > 0x100)
				rtw_usb_page_switch(rtwdev, speed, USB_PHY_PAGE1);
			else
				rtw_usb_page_switch(rtwdev, speed, USB_PHY_PAGE0);

			offset &= 0xff;

			rtw_usb_phy_write(rtwdev, offset, para->value, speed);
		}
	}
}

int rtw_usb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct rtw_dev *rtwdev;
	struct ieee80211_hw *hw;
	struct rtw_usb *rtwusb;
	int drv_data_size;
	int ret;

	drv_data_size = sizeof(struct rtw_dev) + sizeof(struct rtw_usb);
	hw = ieee80211_alloc_hw(drv_data_size, &rtw_ops);
	if (!hw)
		return -ENOMEM;

	rtwdev = hw->priv;
	rtwdev->hw = hw;
	rtwdev->dev = &intf->dev;
	rtwdev->chip = (struct rtw_chip_info *)id->driver_info;
	rtwdev->hci.ops = &rtw_usb_ops;
	rtwdev->hci.type = RTW_HCI_TYPE_USB;

	rtwusb = rtw_get_usb_priv(rtwdev);
	rtwusb->rtwdev = rtwdev;

	ret = rtw_usb_alloc_rx_bufs(rtwusb);
	if (ret)
		goto err_release_hw;

	ret = rtw_core_init(rtwdev);
	if (ret)
		goto err_free_rx_bufs;

	ret = rtw_usb_intf_init(rtwdev, intf);
	if (ret) {
		rtw_err(rtwdev, "failed to init USB interface\n");
		goto err_deinit_core;
	}

	ret = rtw_usb_init_tx(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to init USB TX\n");
		goto err_destroy_usb;
	}

	ret = rtw_usb_init_rx(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to init USB RX\n");
		goto err_destroy_txwq;
	}

	ret = rtw_chip_info_setup(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup chip information\n");
		goto err_destroy_rxwq;
	}

	rtw_usb_phy_cfg(rtwdev, USB_SPEED_HIGH);
	rtw_usb_phy_cfg(rtwdev, USB_SPEED_SUPER);

	ret = rtw_usb_switch_mode(rtwdev);
	if (ret) {
		/* Not a fail, but we do need to skip rtw_register_hw. */
		rtw_dbg(rtwdev, RTW_DBG_USB, "switching to USB 3 mode\n");
		ret = 0;
		goto err_destroy_rxwq;
	}

	ret = rtw_register_hw(rtwdev, rtwdev->hw);
	if (ret) {
		rtw_err(rtwdev, "failed to register hw\n");
		goto err_destroy_rxwq;
	}

	rtw_usb_setup_rx(rtwdev);

	return 0;

err_destroy_rxwq:
	rtw_usb_deinit_rx(rtwdev);

err_destroy_txwq:
	rtw_usb_deinit_tx(rtwdev);

err_destroy_usb:
	rtw_usb_intf_deinit(rtwdev, intf);

err_deinit_core:
	rtw_core_deinit(rtwdev);

err_free_rx_bufs:
	rtw_usb_free_rx_bufs(rtwusb);

err_release_hw:
	ieee80211_free_hw(hw);

	return ret;
}
EXPORT_SYMBOL(rtw_usb_probe);

void rtw_usb_disconnect(struct usb_interface *intf)
{
	struct ieee80211_hw *hw = usb_get_intfdata(intf);
	struct rtw_dev *rtwdev;
	struct rtw_usb *rtwusb;

	if (!hw)
		return;

	rtwdev = hw->priv;
	rtwusb = rtw_get_usb_priv(rtwdev);

	rtw_usb_cancel_rx_bufs(rtwusb);

	rtw_unregister_hw(rtwdev, hw);
	rtw_usb_deinit_tx(rtwdev);
	rtw_usb_deinit_rx(rtwdev);

	if (rtwusb->udev->state != USB_STATE_NOTATTACHED)
		usb_reset_device(rtwusb->udev);

	rtw_usb_free_rx_bufs(rtwusb);

	rtw_usb_intf_deinit(rtwdev, intf);
	rtw_core_deinit(rtwdev);
	ieee80211_free_hw(hw);
}
EXPORT_SYMBOL(rtw_usb_disconnect);

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek USB 802.11ac wireless driver");
MODULE_LICENSE("Dual BSD/GPL");
