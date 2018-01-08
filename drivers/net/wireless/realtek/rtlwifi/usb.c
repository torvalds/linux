/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 *****************************************************************************/

#include "wifi.h"
#include "core.h"
#include "usb.h"
#include "base.h"
#include "ps.h"
#include "rtl8192c/fw_common.h"
#include <linux/export.h>
#include <linux/module.h>

MODULE_AUTHOR("lizhaoming	<chaoming_li@realsil.com.cn>");
MODULE_AUTHOR("Realtek WlanFAE	<wlanfae@realtek.com>");
MODULE_AUTHOR("Larry Finger	<Larry.FInger@lwfinger.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB basic driver for rtlwifi");

#define	REALTEK_USB_VENQT_READ			0xC0
#define	REALTEK_USB_VENQT_WRITE			0x40
#define REALTEK_USB_VENQT_CMD_REQ		0x05
#define	REALTEK_USB_VENQT_CMD_IDX		0x00

#define MAX_USBCTRL_VENDORREQ_TIMES		10

static void usbctrl_async_callback(struct urb *urb)
{
	if (urb) {
		/* free dr */
		kfree(urb->setup_packet);
		/* free databuf */
		kfree(urb->transfer_buffer);
	}
}

static int _usbctrl_vendorreq_async_write(struct usb_device *udev, u8 request,
					  u16 value, u16 index, void *pdata,
					  u16 len)
{
	int rc;
	unsigned int pipe;
	u8 reqtype;
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	const u16 databuf_maxlen = REALTEK_USB_VENQT_MAX_BUF_SIZE;
	u8 *databuf;

	if (WARN_ON_ONCE(len > databuf_maxlen))
		len = databuf_maxlen;

	pipe = usb_sndctrlpipe(udev, 0); /* write_out */
	reqtype =  REALTEK_USB_VENQT_WRITE;

	dr = kzalloc(sizeof(*dr), GFP_ATOMIC);
	if (!dr)
		return -ENOMEM;

	databuf = kzalloc(databuf_maxlen, GFP_ATOMIC);
	if (!databuf) {
		kfree(dr);
		return -ENOMEM;
	}

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		kfree(databuf);
		kfree(dr);
		return -ENOMEM;
	}

	dr->bRequestType = reqtype;
	dr->bRequest = request;
	dr->wValue = cpu_to_le16(value);
	dr->wIndex = cpu_to_le16(index);
	dr->wLength = cpu_to_le16(len);
	/* data are already in little-endian order */
	memcpy(databuf, pdata, len);
	usb_fill_control_urb(urb, udev, pipe,
			     (unsigned char *)dr, databuf, len,
			     usbctrl_async_callback, NULL);
	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (rc < 0) {
		kfree(databuf);
		kfree(dr);
	}
	usb_free_urb(urb);
	return rc;
}

static int _usbctrl_vendorreq_sync_read(struct usb_device *udev, u8 request,
					u16 value, u16 index, void *pdata,
					u16 len)
{
	unsigned int pipe;
	int status;
	u8 reqtype;
	int vendorreq_times = 0;
	static int count;

	pipe = usb_rcvctrlpipe(udev, 0); /* read_in */
	reqtype =  REALTEK_USB_VENQT_READ;

	do {
		status = usb_control_msg(udev, pipe, request, reqtype, value,
					 index, pdata, len, 1000);
		if (status < 0) {
			/* firmware download is checksumed, don't retry */
			if ((value >= FW_8192C_START_ADDRESS &&
			    value <= FW_8192C_END_ADDRESS))
				break;
		} else {
			break;
		}
	} while (++vendorreq_times < MAX_USBCTRL_VENDORREQ_TIMES);

	if (status < 0 && count++ < 4)
		pr_err("reg 0x%x, usbctrl_vendorreq TimeOut! status:0x%x value=0x%x\n",
		       value, status, *(u32 *)pdata);
	return status;
}

static u32 _usb_read_sync(struct rtl_priv *rtlpriv, u32 addr, u16 len)
{
	struct device *dev = rtlpriv->io.dev;
	struct usb_device *udev = to_usb_device(dev);
	u8 request;
	u16 wvalue;
	u16 index;
	__le32 *data;
	unsigned long flags;

	spin_lock_irqsave(&rtlpriv->locks.usb_lock, flags);
	if (++rtlpriv->usb_data_index >= RTL_USB_MAX_RX_COUNT)
		rtlpriv->usb_data_index = 0;
	data = &rtlpriv->usb_data[rtlpriv->usb_data_index];
	spin_unlock_irqrestore(&rtlpriv->locks.usb_lock, flags);
	request = REALTEK_USB_VENQT_CMD_REQ;
	index = REALTEK_USB_VENQT_CMD_IDX; /* n/a */

	wvalue = (u16)addr;
	_usbctrl_vendorreq_sync_read(udev, request, wvalue, index, data, len);
	return le32_to_cpu(*data);
}

static u8 _usb_read8_sync(struct rtl_priv *rtlpriv, u32 addr)
{
	return (u8)_usb_read_sync(rtlpriv, addr, 1);
}

static u16 _usb_read16_sync(struct rtl_priv *rtlpriv, u32 addr)
{
	return (u16)_usb_read_sync(rtlpriv, addr, 2);
}

static u32 _usb_read32_sync(struct rtl_priv *rtlpriv, u32 addr)
{
	return _usb_read_sync(rtlpriv, addr, 4);
}

static void _usb_write_async(struct usb_device *udev, u32 addr, u32 val,
			     u16 len)
{
	u8 request;
	u16 wvalue;
	u16 index;
	__le32 data;

	request = REALTEK_USB_VENQT_CMD_REQ;
	index = REALTEK_USB_VENQT_CMD_IDX; /* n/a */
	wvalue = (u16)(addr&0x0000ffff);
	data = cpu_to_le32(val);
	_usbctrl_vendorreq_async_write(udev, request, wvalue, index, &data,
				       len);
}

static void _usb_write8_async(struct rtl_priv *rtlpriv, u32 addr, u8 val)
{
	struct device *dev = rtlpriv->io.dev;

	_usb_write_async(to_usb_device(dev), addr, val, 1);
}

static void _usb_write16_async(struct rtl_priv *rtlpriv, u32 addr, u16 val)
{
	struct device *dev = rtlpriv->io.dev;

	_usb_write_async(to_usb_device(dev), addr, val, 2);
}

static void _usb_write32_async(struct rtl_priv *rtlpriv, u32 addr, u32 val)
{
	struct device *dev = rtlpriv->io.dev;

	_usb_write_async(to_usb_device(dev), addr, val, 4);
}

static void _usb_writeN_sync(struct rtl_priv *rtlpriv, u32 addr, void *data,
			     u16 len)
{
	struct device *dev = rtlpriv->io.dev;
	struct usb_device *udev = to_usb_device(dev);
	u8 request = REALTEK_USB_VENQT_CMD_REQ;
	u8 reqtype =  REALTEK_USB_VENQT_WRITE;
	u16 wvalue;
	u16 index = REALTEK_USB_VENQT_CMD_IDX;
	int pipe = usb_sndctrlpipe(udev, 0); /* write_out */
	u8 *buffer;

	wvalue = (u16)(addr & 0x0000ffff);
	buffer = kmemdup(data, len, GFP_ATOMIC);
	if (!buffer)
		return;
	usb_control_msg(udev, pipe, request, reqtype, wvalue,
			index, buffer, len, 50);

	kfree(buffer);
}

static void _rtl_usb_io_handler_init(struct device *dev,
				     struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->io.dev = dev;
	mutex_init(&rtlpriv->io.bb_mutex);
	rtlpriv->io.write8_async	= _usb_write8_async;
	rtlpriv->io.write16_async	= _usb_write16_async;
	rtlpriv->io.write32_async	= _usb_write32_async;
	rtlpriv->io.read8_sync		= _usb_read8_sync;
	rtlpriv->io.read16_sync		= _usb_read16_sync;
	rtlpriv->io.read32_sync		= _usb_read32_sync;
	rtlpriv->io.writeN_sync		= _usb_writeN_sync;
}

static void _rtl_usb_io_handler_release(struct ieee80211_hw *hw)
{
	struct rtl_priv __maybe_unused *rtlpriv = rtl_priv(hw);

	mutex_destroy(&rtlpriv->io.bb_mutex);
}

/**
 *
 *	Default aggregation handler. Do nothing and just return the oldest skb.
 */
static struct sk_buff *_none_usb_tx_aggregate_hdl(struct ieee80211_hw *hw,
						  struct sk_buff_head *list)
{
	return skb_dequeue(list);
}

#define IS_HIGH_SPEED_USB(udev) \
		((USB_SPEED_HIGH == (udev)->speed) ? true : false)

static int _rtl_usb_init_tx(struct ieee80211_hw *hw)
{
	u32 i;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));

	rtlusb->max_bulk_out_size = IS_HIGH_SPEED_USB(rtlusb->udev)
						    ? USB_HIGH_SPEED_BULK_SIZE
						    : USB_FULL_SPEED_BULK_SIZE;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "USB Max Bulk-out Size=%d\n",
		 rtlusb->max_bulk_out_size);

	for (i = 0; i < __RTL_TXQ_NUM; i++) {
		u32 ep_num = rtlusb->ep_map.ep_mapping[i];
		if (!ep_num) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
				 "Invalid endpoint map setting!\n");
			return -EINVAL;
		}
	}

	rtlusb->usb_tx_post_hdl =
		 rtlpriv->cfg->usb_interface_cfg->usb_tx_post_hdl;
	rtlusb->usb_tx_cleanup	=
		 rtlpriv->cfg->usb_interface_cfg->usb_tx_cleanup;
	rtlusb->usb_tx_aggregate_hdl =
		 (rtlpriv->cfg->usb_interface_cfg->usb_tx_aggregate_hdl)
		 ? rtlpriv->cfg->usb_interface_cfg->usb_tx_aggregate_hdl
		 : &_none_usb_tx_aggregate_hdl;

	init_usb_anchor(&rtlusb->tx_submitted);
	for (i = 0; i < RTL_USB_MAX_EP_NUM; i++) {
		skb_queue_head_init(&rtlusb->tx_skb_queue[i]);
		init_usb_anchor(&rtlusb->tx_pending[i]);
	}
	return 0;
}

static void _rtl_rx_work(unsigned long param);

static int _rtl_usb_init_rx(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_usb_priv *usb_priv = rtl_usbpriv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(usb_priv);

	rtlusb->rx_max_size = rtlpriv->cfg->usb_interface_cfg->rx_max_size;
	rtlusb->rx_urb_num = rtlpriv->cfg->usb_interface_cfg->rx_urb_num;
	rtlusb->in_ep = rtlpriv->cfg->usb_interface_cfg->in_ep_num;
	rtlusb->usb_rx_hdl = rtlpriv->cfg->usb_interface_cfg->usb_rx_hdl;
	rtlusb->usb_rx_segregate_hdl =
		rtlpriv->cfg->usb_interface_cfg->usb_rx_segregate_hdl;

	pr_info("rx_max_size %d, rx_urb_num %d, in_ep %d\n",
		rtlusb->rx_max_size, rtlusb->rx_urb_num, rtlusb->in_ep);
	init_usb_anchor(&rtlusb->rx_submitted);
	init_usb_anchor(&rtlusb->rx_cleanup_urbs);

	skb_queue_head_init(&rtlusb->rx_queue);
	rtlusb->rx_work_tasklet.func = _rtl_rx_work;
	rtlusb->rx_work_tasklet.data = (unsigned long)rtlusb;

	return 0;
}

static int _rtl_usb_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_usb_priv *usb_priv = rtl_usbpriv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(usb_priv);
	int err;
	u8 epidx;
	struct usb_interface	*usb_intf = rtlusb->intf;
	u8 epnums = usb_intf->cur_altsetting->desc.bNumEndpoints;

	rtlusb->out_ep_nums = rtlusb->in_ep_nums = 0;
	for (epidx = 0; epidx < epnums; epidx++) {
		struct usb_endpoint_descriptor *pep_desc;
		pep_desc = &usb_intf->cur_altsetting->endpoint[epidx].desc;

		if (usb_endpoint_dir_in(pep_desc))
			rtlusb->in_ep_nums++;
		else if (usb_endpoint_dir_out(pep_desc))
			rtlusb->out_ep_nums++;

		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "USB EP(0x%02x), MaxPacketSize=%d, Interval=%d\n",
			 pep_desc->bEndpointAddress, pep_desc->wMaxPacketSize,
			 pep_desc->bInterval);
	}
	if (rtlusb->in_ep_nums <  rtlpriv->cfg->usb_interface_cfg->in_ep_num) {
		pr_err("Too few input end points found\n");
		return -EINVAL;
	}
	if (rtlusb->out_ep_nums == 0) {
		pr_err("No output end points found\n");
		return -EINVAL;
	}
	/* usb endpoint mapping */
	err = rtlpriv->cfg->usb_interface_cfg->usb_endpoint_mapping(hw);
	rtlusb->usb_mq_to_hwq =  rtlpriv->cfg->usb_interface_cfg->usb_mq_to_hwq;
	_rtl_usb_init_tx(hw);
	_rtl_usb_init_rx(hw);
	return err;
}

static void rtl_usb_init_sw(struct ieee80211_hw *hw)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));

	rtlhal->hw = hw;
	ppsc->inactiveps = false;
	ppsc->leisure_ps = false;
	ppsc->fwctrl_lps = false;
	ppsc->reg_fwctrl_lps = 3;
	ppsc->reg_max_lps_awakeintvl = 5;
	ppsc->fwctrl_psmode = FW_PS_DTIM_MODE;

	 /* IBSS */
	mac->beacon_interval = 100;

	 /* AMPDU */
	mac->min_space_cfg = 0;
	mac->max_mss_density = 0;

	/* set sane AMPDU defaults */
	mac->current_ampdu_density = 7;
	mac->current_ampdu_factor = 3;

	/* QOS */
	rtlusb->acm_method = EACMWAY2_SW;

	/* IRQ */
	/* HIMR - turn all on */
	rtlusb->irq_mask[0] = 0xFFFFFFFF;
	/* HIMR_EX - turn all on */
	rtlusb->irq_mask[1] = 0xFFFFFFFF;
	rtlusb->disableHWSM =  true;
}

static void _rtl_rx_completed(struct urb *urb);

static int _rtl_prep_rx_urb(struct ieee80211_hw *hw, struct rtl_usb *rtlusb,
			      struct urb *urb, gfp_t gfp_mask)
{
	void *buf;

	buf = usb_alloc_coherent(rtlusb->udev, rtlusb->rx_max_size, gfp_mask,
				 &urb->transfer_dma);
	if (!buf) {
		pr_err("Failed to usb_alloc_coherent!!\n");
		return -ENOMEM;
	}

	usb_fill_bulk_urb(urb, rtlusb->udev,
			  usb_rcvbulkpipe(rtlusb->udev, rtlusb->in_ep),
			  buf, rtlusb->rx_max_size, _rtl_rx_completed, rtlusb);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	return 0;
}

static void _rtl_usb_rx_process_agg(struct ieee80211_hw *hw,
				    struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 *rxdesc = skb->data;
	struct ieee80211_hdr *hdr;
	bool unicast = false;
	__le16 fc;
	struct ieee80211_rx_status rx_status = {0};
	struct rtl_stats stats = {
		.signal = 0,
		.rate = 0,
	};

	skb_pull(skb, RTL_RX_DESC_SIZE);
	rtlpriv->cfg->ops->query_rx_desc(hw, &stats, &rx_status, rxdesc, skb);
	skb_pull(skb, (stats.rx_drvinfo_size + stats.rx_bufshift));
	hdr = (struct ieee80211_hdr *)(skb->data);
	fc = hdr->frame_control;
	if (!stats.crc) {
		memcpy(IEEE80211_SKB_RXCB(skb), &rx_status, sizeof(rx_status));

		if (is_broadcast_ether_addr(hdr->addr1)) {
			/*TODO*/;
		} else if (is_multicast_ether_addr(hdr->addr1)) {
			/*TODO*/
		} else {
			unicast = true;
			rtlpriv->stats.rxbytesunicast +=  skb->len;
		}

		if (ieee80211_is_data(fc)) {
			rtlpriv->cfg->ops->led_control(hw, LED_CTL_RX);

			if (unicast)
				rtlpriv->link_info.num_rx_inperiod++;
		}
		/* static bcn for roaming */
		rtl_beacon_statistic(hw, skb);
	}
}

static void _rtl_usb_rx_process_noagg(struct ieee80211_hw *hw,
				      struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 *rxdesc = skb->data;
	struct ieee80211_hdr *hdr;
	bool unicast = false;
	__le16 fc;
	struct ieee80211_rx_status rx_status = {0};
	struct rtl_stats stats = {
		.signal = 0,
		.rate = 0,
	};

	skb_pull(skb, RTL_RX_DESC_SIZE);
	rtlpriv->cfg->ops->query_rx_desc(hw, &stats, &rx_status, rxdesc, skb);
	skb_pull(skb, (stats.rx_drvinfo_size + stats.rx_bufshift));
	hdr = (struct ieee80211_hdr *)(skb->data);
	fc = hdr->frame_control;
	if (!stats.crc) {
		memcpy(IEEE80211_SKB_RXCB(skb), &rx_status, sizeof(rx_status));

		if (is_broadcast_ether_addr(hdr->addr1)) {
			/*TODO*/;
		} else if (is_multicast_ether_addr(hdr->addr1)) {
			/*TODO*/
		} else {
			unicast = true;
			rtlpriv->stats.rxbytesunicast +=  skb->len;
		}

		if (ieee80211_is_data(fc)) {
			rtlpriv->cfg->ops->led_control(hw, LED_CTL_RX);

			if (unicast)
				rtlpriv->link_info.num_rx_inperiod++;
		}

		/* static bcn for roaming */
		rtl_beacon_statistic(hw, skb);

		if (likely(rtl_action_proc(hw, skb, false)))
			ieee80211_rx(hw, skb);
		else
			dev_kfree_skb_any(skb);
	} else {
		dev_kfree_skb_any(skb);
	}
}

static void _rtl_rx_pre_process(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct sk_buff *_skb;
	struct sk_buff_head rx_queue;
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));

	skb_queue_head_init(&rx_queue);
	if (rtlusb->usb_rx_segregate_hdl)
		rtlusb->usb_rx_segregate_hdl(hw, skb, &rx_queue);
	WARN_ON(skb_queue_empty(&rx_queue));
	while (!skb_queue_empty(&rx_queue)) {
		_skb = skb_dequeue(&rx_queue);
		_rtl_usb_rx_process_agg(hw, _skb);
		ieee80211_rx(hw, _skb);
	}
}

#define __RX_SKB_MAX_QUEUED	64

static void _rtl_rx_work(unsigned long param)
{
	struct rtl_usb *rtlusb = (struct rtl_usb *)param;
	struct ieee80211_hw *hw = usb_get_intfdata(rtlusb->intf);
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&rtlusb->rx_queue))) {
		if (unlikely(IS_USB_STOP(rtlusb))) {
			dev_kfree_skb_any(skb);
			continue;
		}

		if (likely(!rtlusb->usb_rx_segregate_hdl)) {
			_rtl_usb_rx_process_noagg(hw, skb);
		} else {
			/* TO DO */
			_rtl_rx_pre_process(hw, skb);
			pr_err("rx agg not supported\n");
		}
	}
}

static unsigned int _rtl_rx_get_padding(struct ieee80211_hdr *hdr,
					unsigned int len)
{
#if NET_IP_ALIGN != 0
	unsigned int padding = 0;
#endif

	/* make function no-op when possible */
	if (NET_IP_ALIGN == 0 || len < sizeof(*hdr))
		return 0;

#if NET_IP_ALIGN != 0
	/* alignment calculation as in lbtf_rx() / carl9170_rx_copy_data() */
	/* TODO: deduplicate common code, define helper function instead? */

	if (ieee80211_is_data_qos(hdr->frame_control)) {
		u8 *qc = ieee80211_get_qos_ctl(hdr);

		padding ^= NET_IP_ALIGN;

		/* Input might be invalid, avoid accessing memory outside
		 * the buffer.
		 */
		if ((unsigned long)qc - (unsigned long)hdr < len &&
		    *qc & IEEE80211_QOS_CTL_A_MSDU_PRESENT)
			padding ^= NET_IP_ALIGN;
	}

	if (ieee80211_has_a4(hdr->frame_control))
		padding ^= NET_IP_ALIGN;

	return padding;
#endif
}

#define __RADIO_TAP_SIZE_RSV	32

static void _rtl_rx_completed(struct urb *_urb)
{
	struct rtl_usb *rtlusb = (struct rtl_usb *)_urb->context;
	int err = 0;

	if (unlikely(IS_USB_STOP(rtlusb)))
		goto free;

	if (likely(0 == _urb->status)) {
		unsigned int padding;
		struct sk_buff *skb;
		unsigned int qlen;
		unsigned int size = _urb->actual_length;
		struct ieee80211_hdr *hdr;

		if (size < RTL_RX_DESC_SIZE + sizeof(struct ieee80211_hdr)) {
			pr_err("Too short packet from bulk IN! (len: %d)\n",
			       size);
			goto resubmit;
		}

		qlen = skb_queue_len(&rtlusb->rx_queue);
		if (qlen >= __RX_SKB_MAX_QUEUED) {
			pr_err("Pending RX skbuff queue full! (qlen: %d)\n",
			       qlen);
			goto resubmit;
		}

		hdr = (void *)(_urb->transfer_buffer + RTL_RX_DESC_SIZE);
		padding = _rtl_rx_get_padding(hdr, size - RTL_RX_DESC_SIZE);

		skb = dev_alloc_skb(size + __RADIO_TAP_SIZE_RSV + padding);
		if (!skb) {
			pr_err("Can't allocate skb for bulk IN!\n");
			goto resubmit;
		}

		_rtl_install_trx_info(rtlusb, skb, rtlusb->in_ep);

		/* Make sure the payload data is 4 byte aligned. */
		skb_reserve(skb, padding);

		/* reserve some space for mac80211's radiotap */
		skb_reserve(skb, __RADIO_TAP_SIZE_RSV);

		skb_put_data(skb, _urb->transfer_buffer, size);

		skb_queue_tail(&rtlusb->rx_queue, skb);
		tasklet_schedule(&rtlusb->rx_work_tasklet);

		goto resubmit;
	}

	switch (_urb->status) {
	/* disconnect */
	case -ENOENT:
	case -ECONNRESET:
	case -ENODEV:
	case -ESHUTDOWN:
		goto free;
	default:
		break;
	}

resubmit:
	usb_anchor_urb(_urb, &rtlusb->rx_submitted);
	err = usb_submit_urb(_urb, GFP_ATOMIC);
	if (unlikely(err)) {
		usb_unanchor_urb(_urb);
		goto free;
	}
	return;

free:
	/* On some architectures, usb_free_coherent must not be called from
	 * hardirq context. Queue urb to cleanup list.
	 */
	usb_anchor_urb(_urb, &rtlusb->rx_cleanup_urbs);
}

#undef __RADIO_TAP_SIZE_RSV

static void _rtl_usb_cleanup_rx(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct urb *urb;

	usb_kill_anchored_urbs(&rtlusb->rx_submitted);

	tasklet_kill(&rtlusb->rx_work_tasklet);
	cancel_work_sync(&rtlpriv->works.lps_change_work);

	flush_workqueue(rtlpriv->works.rtl_wq);
	destroy_workqueue(rtlpriv->works.rtl_wq);

	skb_queue_purge(&rtlusb->rx_queue);

	while ((urb = usb_get_from_anchor(&rtlusb->rx_cleanup_urbs))) {
		usb_free_coherent(urb->dev, urb->transfer_buffer_length,
				urb->transfer_buffer, urb->transfer_dma);
		usb_free_urb(urb);
	}
}

static int _rtl_usb_receive(struct ieee80211_hw *hw)
{
	struct urb *urb;
	int err;
	int i;
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));

	WARN_ON(0 == rtlusb->rx_urb_num);
	/* 1600 == 1514 + max WLAN header + rtk info */
	WARN_ON(rtlusb->rx_max_size < 1600);

	for (i = 0; i < rtlusb->rx_urb_num; i++) {
		err = -ENOMEM;
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb)
			goto err_out;

		err = _rtl_prep_rx_urb(hw, rtlusb, urb, GFP_KERNEL);
		if (err < 0) {
			pr_err("Failed to prep_rx_urb!!\n");
			usb_free_urb(urb);
			goto err_out;
		}

		usb_anchor_urb(urb, &rtlusb->rx_submitted);
		err = usb_submit_urb(urb, GFP_KERNEL);
		if (err)
			goto err_out;
		usb_free_urb(urb);
	}
	return 0;

err_out:
	usb_kill_anchored_urbs(&rtlusb->rx_submitted);
	_rtl_usb_cleanup_rx(hw);
	return err;
}

static int rtl_usb_start(struct ieee80211_hw *hw)
{
	int err;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));

	err = rtlpriv->cfg->ops->hw_init(hw);
	if (!err) {
		rtl_init_rx_config(hw);

		/* Enable software */
		SET_USB_START(rtlusb);
		/* should after adapter start and interrupt enable. */
		set_hal_start(rtlhal);

		/* Start bulk IN */
		err = _rtl_usb_receive(hw);
	}

	return err;
}
/**
 *
 *
 */

/*=======================  tx =========================================*/
static void rtl_usb_cleanup(struct ieee80211_hw *hw)
{
	u32 i;
	struct sk_buff *_skb;
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct ieee80211_tx_info *txinfo;

	/* clean up rx stuff. */
	_rtl_usb_cleanup_rx(hw);

	/* clean up tx stuff */
	for (i = 0; i < RTL_USB_MAX_EP_NUM; i++) {
		while ((_skb = skb_dequeue(&rtlusb->tx_skb_queue[i]))) {
			rtlusb->usb_tx_cleanup(hw, _skb);
			txinfo = IEEE80211_SKB_CB(_skb);
			ieee80211_tx_info_clear_status(txinfo);
			txinfo->flags |= IEEE80211_TX_STAT_ACK;
			ieee80211_tx_status_irqsafe(hw, _skb);
		}
		usb_kill_anchored_urbs(&rtlusb->tx_pending[i]);
	}
	usb_kill_anchored_urbs(&rtlusb->tx_submitted);
}

/**
 *
 * We may add some struct into struct rtl_usb later. Do deinit here.
 *
 */
static void rtl_usb_deinit(struct ieee80211_hw *hw)
{
	rtl_usb_cleanup(hw);
}

static void rtl_usb_stop(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct urb *urb;

	/* should after adapter start and interrupt enable. */
	set_hal_stop(rtlhal);
	cancel_work_sync(&rtlpriv->works.fill_h2c_cmd);
	/* Enable software */
	SET_USB_STOP(rtlusb);

	/* free pre-allocated URBs from rtl_usb_start() */
	usb_kill_anchored_urbs(&rtlusb->rx_submitted);

	tasklet_kill(&rtlusb->rx_work_tasklet);
	cancel_work_sync(&rtlpriv->works.lps_change_work);

	flush_workqueue(rtlpriv->works.rtl_wq);

	skb_queue_purge(&rtlusb->rx_queue);

	while ((urb = usb_get_from_anchor(&rtlusb->rx_cleanup_urbs))) {
		usb_free_coherent(urb->dev, urb->transfer_buffer_length,
				urb->transfer_buffer, urb->transfer_dma);
		usb_free_urb(urb);
	}

	rtlpriv->cfg->ops->hw_disable(hw);
}

static void _rtl_submit_tx_urb(struct ieee80211_hw *hw, struct urb *_urb)
{
	int err;
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));

	usb_anchor_urb(_urb, &rtlusb->tx_submitted);
	err = usb_submit_urb(_urb, GFP_ATOMIC);
	if (err < 0) {
		struct sk_buff *skb;

		pr_err("Failed to submit urb\n");
		usb_unanchor_urb(_urb);
		skb = (struct sk_buff *)_urb->context;
		kfree_skb(skb);
	}
	usb_free_urb(_urb);
}

static int _usb_tx_post(struct ieee80211_hw *hw, struct urb *urb,
			struct sk_buff *skb)
{
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct ieee80211_tx_info *txinfo;

	rtlusb->usb_tx_post_hdl(hw, urb, skb);
	skb_pull(skb, RTL_TX_HEADER_SIZE);
	txinfo = IEEE80211_SKB_CB(skb);
	ieee80211_tx_info_clear_status(txinfo);
	txinfo->flags |= IEEE80211_TX_STAT_ACK;

	if (urb->status) {
		pr_err("Urb has error status 0x%X\n", urb->status);
		goto out;
	}
	/*  TODO:	statistics */
out:
	ieee80211_tx_status_irqsafe(hw, skb);
	return urb->status;
}

static void _rtl_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *)urb->context;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct rtl_usb *rtlusb = (struct rtl_usb *)info->rate_driver_data[0];
	struct ieee80211_hw *hw = usb_get_intfdata(rtlusb->intf);
	int err;

	if (unlikely(IS_USB_STOP(rtlusb)))
		return;
	err = _usb_tx_post(hw, urb, skb);
	if (err) {
		/* Ignore error and keep issuiing other urbs */
		return;
	}
}

static struct urb *_rtl_usb_tx_urb_setup(struct ieee80211_hw *hw,
				struct sk_buff *skb, u32 ep_num)
{
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct urb *_urb;

	WARN_ON(NULL == skb);
	_urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!_urb) {
		kfree_skb(skb);
		return NULL;
	}
	_rtl_install_trx_info(rtlusb, skb, ep_num);
	usb_fill_bulk_urb(_urb, rtlusb->udev, usb_sndbulkpipe(rtlusb->udev,
			  ep_num), skb->data, skb->len, _rtl_tx_complete, skb);
	_urb->transfer_flags |= URB_ZERO_PACKET;
	return _urb;
}

static void _rtl_usb_transmit(struct ieee80211_hw *hw, struct sk_buff *skb,
		       enum rtl_txq qnum)
{
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	u32 ep_num;
	struct urb *_urb = NULL;
	struct sk_buff *_skb = NULL;

	WARN_ON(NULL == rtlusb->usb_tx_aggregate_hdl);
	if (unlikely(IS_USB_STOP(rtlusb))) {
		pr_err("USB device is stopping...\n");
		kfree_skb(skb);
		return;
	}
	ep_num = rtlusb->ep_map.ep_mapping[qnum];
	_skb = skb;
	_urb = _rtl_usb_tx_urb_setup(hw, _skb, ep_num);
	if (unlikely(!_urb)) {
		pr_err("Can't allocate urb. Drop skb!\n");
		kfree_skb(skb);
		return;
	}
	_rtl_submit_tx_urb(hw, _urb);
}

static void _rtl_usb_tx_preprocess(struct ieee80211_hw *hw,
				   struct ieee80211_sta *sta,
				   struct sk_buff *skb,
				   u16 hw_queue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct rtl_tx_desc *pdesc = NULL;
	struct rtl_tcb_desc tcb_desc;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(skb->data);
	__le16 fc = hdr->frame_control;
	u8 *pda_addr = hdr->addr1;

	memset(&tcb_desc, 0, sizeof(struct rtl_tcb_desc));
	if (ieee80211_is_auth(fc)) {
		RT_TRACE(rtlpriv, COMP_SEND, DBG_DMESG, "MAC80211_LINKING\n");
		rtl_ips_nic_on(hw);
	}

	if (rtlpriv->psc.sw_ps_enabled) {
		if (ieee80211_is_data(fc) && !ieee80211_is_nullfunc(fc) &&
		    !ieee80211_has_pm(fc))
			hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);
	}

	rtl_action_proc(hw, skb, true);
	if (is_multicast_ether_addr(pda_addr))
		rtlpriv->stats.txbytesmulticast += skb->len;
	else if (is_broadcast_ether_addr(pda_addr))
		rtlpriv->stats.txbytesbroadcast += skb->len;
	else
		rtlpriv->stats.txbytesunicast += skb->len;
	rtlpriv->cfg->ops->fill_tx_desc(hw, hdr, (u8 *)pdesc, NULL, info, sta, skb,
					hw_queue, &tcb_desc);
	if (ieee80211_is_data(fc))
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_TX);
}

static int rtl_usb_tx(struct ieee80211_hw *hw,
		      struct ieee80211_sta *sta,
		      struct sk_buff *skb,
		      struct rtl_tcb_desc *dummy)
{
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(skb->data);
	__le16 fc = hdr->frame_control;
	u16 hw_queue;

	if (unlikely(is_hal_stop(rtlhal)))
		goto err_free;
	hw_queue = rtlusb->usb_mq_to_hwq(fc, skb_get_queue_mapping(skb));
	_rtl_usb_tx_preprocess(hw, sta, skb, hw_queue);
	_rtl_usb_transmit(hw, skb, hw_queue);
	return NETDEV_TX_OK;

err_free:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static bool rtl_usb_tx_chk_waitq_insert(struct ieee80211_hw *hw,
					struct ieee80211_sta *sta,
					struct sk_buff *skb)
{
	return false;
}

static void rtl_fill_h2c_cmd_work_callback(struct work_struct *work)
{
	struct rtl_works *rtlworks =
	    container_of(work, struct rtl_works, fill_h2c_cmd);
	struct ieee80211_hw *hw = rtlworks->hw;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->cfg->ops->fill_h2c_cmd(hw, H2C_RA_MASK, 5, rtlpriv->rate_mask);
}

static const struct rtl_intf_ops rtl_usb_ops = {
	.adapter_start = rtl_usb_start,
	.adapter_stop = rtl_usb_stop,
	.adapter_tx = rtl_usb_tx,
	.waitq_insert = rtl_usb_tx_chk_waitq_insert,
};

int rtl_usb_probe(struct usb_interface *intf,
		  const struct usb_device_id *id,
		  struct rtl_hal_cfg *rtl_hal_cfg)
{
	int err;
	struct ieee80211_hw *hw = NULL;
	struct rtl_priv *rtlpriv = NULL;
	struct usb_device	*udev;
	struct rtl_usb_priv *usb_priv;

	hw = ieee80211_alloc_hw(sizeof(struct rtl_priv) +
				sizeof(struct rtl_usb_priv), &rtl_ops);
	if (!hw) {
		WARN_ONCE(true, "rtl_usb: ieee80211 alloc failed\n");
		return -ENOMEM;
	}
	rtlpriv = hw->priv;
	rtlpriv->hw = hw;
	rtlpriv->usb_data = kzalloc(RTL_USB_MAX_RX_COUNT * sizeof(u32),
				    GFP_KERNEL);
	if (!rtlpriv->usb_data)
		return -ENOMEM;

	/* this spin lock must be initialized early */
	spin_lock_init(&rtlpriv->locks.usb_lock);
	INIT_WORK(&rtlpriv->works.fill_h2c_cmd,
		  rtl_fill_h2c_cmd_work_callback);
	INIT_WORK(&rtlpriv->works.lps_change_work,
		  rtl_lps_change_work_callback);

	rtlpriv->usb_data_index = 0;
	init_completion(&rtlpriv->firmware_loading_complete);
	SET_IEEE80211_DEV(hw, &intf->dev);
	udev = interface_to_usbdev(intf);
	usb_get_dev(udev);
	usb_priv = rtl_usbpriv(hw);
	memset(usb_priv, 0, sizeof(*usb_priv));
	usb_priv->dev.intf = intf;
	usb_priv->dev.udev = udev;
	usb_set_intfdata(intf, hw);
	/* init cfg & intf_ops */
	rtlpriv->rtlhal.interface = INTF_USB;
	rtlpriv->cfg = rtl_hal_cfg;
	rtlpriv->intf_ops = &rtl_usb_ops;
	/* Init IO handler */
	_rtl_usb_io_handler_init(&udev->dev, hw);
	rtlpriv->cfg->ops->read_chip_version(hw);
	/*like read eeprom and so on */
	rtlpriv->cfg->ops->read_eeprom_info(hw);
	err = _rtl_usb_init(hw);
	if (err)
		goto error_out;
	rtl_usb_init_sw(hw);
	/* Init mac80211 sw */
	err = rtl_init_core(hw);
	if (err) {
		pr_err("Can't allocate sw for mac80211\n");
		goto error_out;
	}
	if (rtlpriv->cfg->ops->init_sw_vars(hw)) {
		pr_err("Can't init_sw_vars\n");
		goto error_out;
	}
	rtlpriv->cfg->ops->init_sw_leds(hw);

	err = ieee80211_register_hw(hw);
	if (err) {
		pr_err("Can't register mac80211 hw.\n");
		err = -ENODEV;
		goto error_out;
	}
	rtlpriv->mac80211.mac80211_registered = 1;

	set_bit(RTL_STATUS_INTERFACE_START, &rtlpriv->status);
	return 0;

error_out:
	rtl_deinit_core(hw);
	_rtl_usb_io_handler_release(hw);
	usb_put_dev(udev);
	complete(&rtlpriv->firmware_loading_complete);
	return -ENODEV;
}
EXPORT_SYMBOL(rtl_usb_probe);

void rtl_usb_disconnect(struct usb_interface *intf)
{
	struct ieee80211_hw *hw = usb_get_intfdata(intf);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *rtlmac = rtl_mac(rtl_priv(hw));
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));

	if (unlikely(!rtlpriv))
		return;
	/* just in case driver is removed before firmware callback */
	wait_for_completion(&rtlpriv->firmware_loading_complete);
	clear_bit(RTL_STATUS_INTERFACE_START, &rtlpriv->status);
	/*ieee80211_unregister_hw will call ops_stop */
	if (rtlmac->mac80211_registered == 1) {
		ieee80211_unregister_hw(hw);
		rtlmac->mac80211_registered = 0;
	} else {
		rtl_deinit_deferred_work(hw);
		rtlpriv->intf_ops->adapter_stop(hw);
	}
	/*deinit rfkill */
	/* rtl_deinit_rfkill(hw); */
	rtl_usb_deinit(hw);
	rtl_deinit_core(hw);
	kfree(rtlpriv->usb_data);
	rtlpriv->cfg->ops->deinit_sw_leds(hw);
	rtlpriv->cfg->ops->deinit_sw_vars(hw);
	_rtl_usb_io_handler_release(hw);
	usb_put_dev(rtlusb->udev);
	usb_set_intfdata(intf, NULL);
	ieee80211_free_hw(hw);
}
EXPORT_SYMBOL(rtl_usb_disconnect);

int rtl_usb_suspend(struct usb_interface *pusb_intf, pm_message_t message)
{
	return 0;
}
EXPORT_SYMBOL(rtl_usb_suspend);

int rtl_usb_resume(struct usb_interface *pusb_intf)
{
	return 0;
}
EXPORT_SYMBOL(rtl_usb_resume);
