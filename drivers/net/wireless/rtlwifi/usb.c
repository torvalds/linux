/******************************************************************************
 *
 * Copyright(c) 2009-2011  Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
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
#include <linux/usb.h>
#include "core.h"
#include "wifi.h"
#include "usb.h"
#include "base.h"
#include "ps.h"

#define	REALTEK_USB_VENQT_READ			0xC0
#define	REALTEK_USB_VENQT_WRITE			0x40
#define REALTEK_USB_VENQT_CMD_REQ		0x05
#define	REALTEK_USB_VENQT_CMD_IDX		0x00

#define REALTEK_USB_VENQT_MAX_BUF_SIZE		254

static void usbctrl_async_callback(struct urb *urb)
{
	if (urb)
		kfree(urb->context);
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
	struct rtl819x_async_write_data {
		u8 data[REALTEK_USB_VENQT_MAX_BUF_SIZE];
		struct usb_ctrlrequest dr;
	} *buf;

	pipe = usb_sndctrlpipe(udev, 0); /* write_out */
	reqtype =  REALTEK_USB_VENQT_WRITE;

	buf = kmalloc(sizeof(*buf), GFP_ATOMIC);
	if (!buf)
		return -ENOMEM;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		kfree(buf);
		return -ENOMEM;
	}

	dr = &buf->dr;

	dr->bRequestType = reqtype;
	dr->bRequest = request;
	dr->wValue = cpu_to_le16(value);
	dr->wIndex = cpu_to_le16(index);
	dr->wLength = cpu_to_le16(len);
	memcpy(buf, pdata, len);
	usb_fill_control_urb(urb, udev, pipe,
			     (unsigned char *)dr, buf, len,
			     usbctrl_async_callback, buf);
	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (rc < 0)
		kfree(buf);
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

	pipe = usb_rcvctrlpipe(udev, 0); /* read_in */
	reqtype =  REALTEK_USB_VENQT_READ;

	status = usb_control_msg(udev, pipe, request, reqtype, value, index,
				 pdata, len, 0); /* max. timeout */

	if (status < 0)
		printk(KERN_ERR "reg 0x%x, usbctrl_vendorreq TimeOut! "
		       "status:0x%x value=0x%x\n", value, status,
		       *(u32 *)pdata);
	return status;
}

static u32 _usb_read_sync(struct usb_device *udev, u32 addr, u16 len)
{
	u8 request;
	u16 wvalue;
	u16 index;
	u32 *data;
	u32 ret;

	data = kmalloc(sizeof(u32), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	request = REALTEK_USB_VENQT_CMD_REQ;
	index = REALTEK_USB_VENQT_CMD_IDX; /* n/a */

	wvalue = (u16)addr;
	_usbctrl_vendorreq_sync_read(udev, request, wvalue, index, data, len);
	ret = *data;
	kfree(data);
	return ret;
}

static u8 _usb_read8_sync(struct rtl_priv *rtlpriv, u32 addr)
{
	struct device *dev = rtlpriv->io.dev;

	return (u8)_usb_read_sync(to_usb_device(dev), addr, 1);
}

static u16 _usb_read16_sync(struct rtl_priv *rtlpriv, u32 addr)
{
	struct device *dev = rtlpriv->io.dev;

	return (u16)_usb_read_sync(to_usb_device(dev), addr, 2);
}

static u32 _usb_read32_sync(struct rtl_priv *rtlpriv, u32 addr)
{
	struct device *dev = rtlpriv->io.dev;

	return _usb_read_sync(to_usb_device(dev), addr, 4);
}

static void _usb_write_async(struct usb_device *udev, u32 addr, u32 val,
			     u16 len)
{
	u8 request;
	u16 wvalue;
	u16 index;
	u32 data;

	request = REALTEK_USB_VENQT_CMD_REQ;
	index = REALTEK_USB_VENQT_CMD_IDX; /* n/a */
	wvalue = (u16)(addr&0x0000ffff);
	data = val;
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

static int _usb_nbytes_read_write(struct usb_device *udev, bool read, u32 addr,
				  u16 len, u8 *pdata)
{
	int status;
	u8 request;
	u16 wvalue;
	u16 index;

	request = REALTEK_USB_VENQT_CMD_REQ;
	index = REALTEK_USB_VENQT_CMD_IDX; /* n/a */
	wvalue = (u16)addr;
	if (read)
		status = _usbctrl_vendorreq_sync_read(udev, request, wvalue,
						      index, pdata, len);
	else
		status = _usbctrl_vendorreq_async_write(udev, request, wvalue,
							index, pdata, len);
	return status;
}

static int _usb_readN_sync(struct rtl_priv *rtlpriv, u32 addr, u16 len,
			   u8 *pdata)
{
	struct device *dev = rtlpriv->io.dev;

	return _usb_nbytes_read_write(to_usb_device(dev), true, addr, len,
				       pdata);
}

static int _usb_writeN_async(struct rtl_priv *rtlpriv, u32 addr, u16 len,
			     u8 *pdata)
{
	struct device *dev = rtlpriv->io.dev;

	return _usb_nbytes_read_write(to_usb_device(dev), false, addr, len,
				      pdata);
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
	rtlpriv->io.writeN_async	= _usb_writeN_async;
	rtlpriv->io.read8_sync		= _usb_read8_sync;
	rtlpriv->io.read16_sync		= _usb_read16_sync;
	rtlpriv->io.read32_sync		= _usb_read32_sync;
	rtlpriv->io.readN_sync		= _usb_readN_sync;
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

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, ("USB Max Bulk-out Size=%d\n",
		 rtlusb->max_bulk_out_size));

	for (i = 0; i < __RTL_TXQ_NUM; i++) {
		u32 ep_num = rtlusb->ep_map.ep_mapping[i];
		if (!ep_num) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
				 ("Invalid endpoint map setting!\n"));
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

	printk(KERN_INFO "rtl8192cu: rx_max_size %d, rx_urb_num %d, in_ep %d\n",
		rtlusb->rx_max_size, rtlusb->rx_urb_num, rtlusb->in_ep);
	init_usb_anchor(&rtlusb->rx_submitted);
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
			 ("USB EP(0x%02x), MaxPacketSize=%d ,Interval=%d.\n",
			 pep_desc->bEndpointAddress, pep_desc->wMaxPacketSize,
			 pep_desc->bInterval));
	}
	if (rtlusb->in_ep_nums <  rtlpriv->cfg->usb_interface_cfg->in_ep_num)
		return -EINVAL ;

	/* usb endpoint mapping */
	err = rtlpriv->cfg->usb_interface_cfg->usb_endpoint_mapping(hw);
	rtlusb->usb_mq_to_hwq =  rtlpriv->cfg->usb_interface_cfg->usb_mq_to_hwq;
	_rtl_usb_init_tx(hw);
	_rtl_usb_init_rx(hw);
	return err;
}

static int _rtl_usb_init_sw(struct ieee80211_hw *hw)
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
	rtlusb->acm_method = eAcmWay2_SW;

	/* IRQ */
	/* HIMR - turn all on */
	rtlusb->irq_mask[0] = 0xFFFFFFFF;
	/* HIMR_EX - turn all on */
	rtlusb->irq_mask[1] = 0xFFFFFFFF;
	rtlusb->disableHWSM =  true;
	return 0;
}

#define __RADIO_TAP_SIZE_RSV	32

static void _rtl_rx_completed(struct urb *urb);

static struct sk_buff *_rtl_prep_rx_urb(struct ieee80211_hw *hw,
					struct rtl_usb *rtlusb,
					struct urb *urb,
					gfp_t gfp_mask)
{
	struct sk_buff *skb;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	skb = __dev_alloc_skb((rtlusb->rx_max_size + __RADIO_TAP_SIZE_RSV),
			       gfp_mask);
	if (!skb) {
		RT_TRACE(rtlpriv, COMP_USB, DBG_EMERG,
			 ("Failed to __dev_alloc_skb!!\n"))
		return ERR_PTR(-ENOMEM);
	}

	/* reserve some space for mac80211's radiotap */
	skb_reserve(skb, __RADIO_TAP_SIZE_RSV);
	usb_fill_bulk_urb(urb, rtlusb->udev,
			  usb_rcvbulkpipe(rtlusb->udev, rtlusb->in_ep),
			  skb->data, min(skb_tailroom(skb),
			  (int)rtlusb->rx_max_size),
			  _rtl_rx_completed, skb);

	_rtl_install_trx_info(rtlusb, skb, rtlusb->in_ep);
	return skb;
}

#undef __RADIO_TAP_SIZE_RSV

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
		.noise = -98,
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

		rtl_is_special_data(hw, skb, false);

		if (ieee80211_is_data(fc)) {
			rtlpriv->cfg->ops->led_control(hw, LED_CTL_RX);

			if (unicast)
				rtlpriv->link_info.num_rx_inperiod++;
		}
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
		.noise = -98,
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

		rtl_is_special_data(hw, skb, false);

		if (ieee80211_is_data(fc)) {
			rtlpriv->cfg->ops->led_control(hw, LED_CTL_RX);

			if (unicast)
				rtlpriv->link_info.num_rx_inperiod++;
		}
		if (likely(rtl_action_proc(hw, skb, false))) {
			struct sk_buff *uskb = NULL;
			u8 *pdata;

			uskb = dev_alloc_skb(skb->len + 128);
			memcpy(IEEE80211_SKB_RXCB(uskb), &rx_status,
			       sizeof(rx_status));
			pdata = (u8 *)skb_put(uskb, skb->len);
			memcpy(pdata, skb->data, skb->len);
			dev_kfree_skb_any(skb);
			ieee80211_rx_irqsafe(hw, uskb);
		} else {
			dev_kfree_skb_any(skb);
		}
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
		_rtl_usb_rx_process_agg(hw, skb);
		ieee80211_rx_irqsafe(hw, skb);
	}
}

static void _rtl_rx_completed(struct urb *_urb)
{
	struct sk_buff *skb = (struct sk_buff *)_urb->context;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct rtl_usb *rtlusb = (struct rtl_usb *)info->rate_driver_data[0];
	struct ieee80211_hw *hw = usb_get_intfdata(rtlusb->intf);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int err = 0;

	if (unlikely(IS_USB_STOP(rtlusb)))
		goto free;

	if (likely(0 == _urb->status)) {
		/* If this code were moved to work queue, would CPU
		 * utilization be improved?  NOTE: We shall allocate another skb
		 * and reuse the original one.
		 */
		skb_put(skb, _urb->actual_length);

		if (likely(!rtlusb->usb_rx_segregate_hdl)) {
			struct sk_buff *_skb;
			_rtl_usb_rx_process_noagg(hw, skb);
			_skb = _rtl_prep_rx_urb(hw, rtlusb, _urb, GFP_ATOMIC);
			if (IS_ERR(_skb)) {
				err = PTR_ERR(_skb);
				RT_TRACE(rtlpriv, COMP_USB, DBG_EMERG,
					("Can't allocate skb for bulk IN!\n"));
				return;
			}
			skb = _skb;
		} else{
			/* TO DO */
			_rtl_rx_pre_process(hw, skb);
			printk(KERN_ERR "rtlwifi: rx agg not supported\n");
		}
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
	skb_reset_tail_pointer(skb);
	skb_trim(skb, 0);

	usb_anchor_urb(_urb, &rtlusb->rx_submitted);
	err = usb_submit_urb(_urb, GFP_ATOMIC);
	if (unlikely(err)) {
		usb_unanchor_urb(_urb);
		goto free;
	}
	return;

free:
	dev_kfree_skb_irq(skb);
}

static int _rtl_usb_receive(struct ieee80211_hw *hw)
{
	struct urb *urb;
	struct sk_buff *skb;
	int err;
	int i;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));

	WARN_ON(0 == rtlusb->rx_urb_num);
	/* 1600 == 1514 + max WLAN header + rtk info */
	WARN_ON(rtlusb->rx_max_size < 1600);

	for (i = 0; i < rtlusb->rx_urb_num; i++) {
		err = -ENOMEM;
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			RT_TRACE(rtlpriv, COMP_USB, DBG_EMERG,
				 ("Failed to alloc URB!!\n"))
			goto err_out;
		}

		skb = _rtl_prep_rx_urb(hw, rtlusb, urb, GFP_KERNEL);
		if (IS_ERR(skb)) {
			RT_TRACE(rtlpriv, COMP_USB, DBG_EMERG,
				 ("Failed to prep_rx_urb!!\n"))
			err = PTR_ERR(skb);
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
	return err;
}

static int rtl_usb_start(struct ieee80211_hw *hw)
{
	int err;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));

	err = rtlpriv->cfg->ops->hw_init(hw);
	rtl_init_rx_config(hw);

	/* Enable software */
	SET_USB_START(rtlusb);
	/* should after adapter start and interrupt enable. */
	set_hal_start(rtlhal);

	/* Start bulk IN */
	_rtl_usb_receive(hw);

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

	SET_USB_STOP(rtlusb);

	/* clean up rx stuff. */
	usb_kill_anchored_urbs(&rtlusb->rx_submitted);

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

	/* should after adapter start and interrupt enable. */
	set_hal_stop(rtlhal);
	/* Enable software */
	SET_USB_STOP(rtlusb);
	rtl_usb_deinit(hw);
	rtlpriv->cfg->ops->hw_disable(hw);
}

static void _rtl_submit_tx_urb(struct ieee80211_hw *hw, struct urb *_urb)
{
	int err;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));

	usb_anchor_urb(_urb, &rtlusb->tx_submitted);
	err = usb_submit_urb(_urb, GFP_ATOMIC);
	if (err < 0) {
		struct sk_buff *skb;

		RT_TRACE(rtlpriv, COMP_USB, DBG_EMERG,
			 ("Failed to submit urb.\n"));
		usb_unanchor_urb(_urb);
		skb = (struct sk_buff *)_urb->context;
		kfree_skb(skb);
	}
	usb_free_urb(_urb);
}

static int _usb_tx_post(struct ieee80211_hw *hw, struct urb *urb,
			struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct ieee80211_tx_info *txinfo;

	rtlusb->usb_tx_post_hdl(hw, urb, skb);
	skb_pull(skb, RTL_TX_HEADER_SIZE);
	txinfo = IEEE80211_SKB_CB(skb);
	ieee80211_tx_info_clear_status(txinfo);
	txinfo->flags |= IEEE80211_TX_STAT_ACK;

	if (urb->status) {
		RT_TRACE(rtlpriv, COMP_USB, DBG_EMERG,
			 ("Urb has error status 0x%X\n", urb->status));
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
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct urb *_urb;

	WARN_ON(NULL == skb);
	_urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!_urb) {
		RT_TRACE(rtlpriv, COMP_USB, DBG_EMERG,
			 ("Can't allocate URB for bulk out!\n"));
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
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	u32 ep_num;
	struct urb *_urb = NULL;
	struct sk_buff *_skb = NULL;
	struct sk_buff_head *skb_list;
	struct usb_anchor *urb_list;

	WARN_ON(NULL == rtlusb->usb_tx_aggregate_hdl);
	if (unlikely(IS_USB_STOP(rtlusb))) {
		RT_TRACE(rtlpriv, COMP_USB, DBG_EMERG,
			 ("USB device is stopping...\n"));
		kfree_skb(skb);
		return;
	}
	ep_num = rtlusb->ep_map.ep_mapping[qnum];
	skb_list = &rtlusb->tx_skb_queue[ep_num];
	_skb = skb;
	_urb = _rtl_usb_tx_urb_setup(hw, _skb, ep_num);
	if (unlikely(!_urb)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("Can't allocate urb. Drop skb!\n"));
		return;
	}
	urb_list = &rtlusb->tx_pending[ep_num];
	_rtl_submit_tx_urb(hw, _urb);
}

static void _rtl_usb_tx_preprocess(struct ieee80211_hw *hw, struct sk_buff *skb,
			    u16 hw_queue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct rtl_tx_desc *pdesc = NULL;
	struct rtl_tcb_desc tcb_desc;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(skb->data);
	__le16 fc = hdr->frame_control;
	u8 *pda_addr = hdr->addr1;
	/* ssn */
	u8 *qc = NULL;
	u8 tid = 0;
	u16 seq_number = 0;

	memset(&tcb_desc, 0, sizeof(struct rtl_tcb_desc));
	if (ieee80211_is_auth(fc)) {
		RT_TRACE(rtlpriv, COMP_SEND, DBG_DMESG, ("MAC80211_LINKING\n"));
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
	if (ieee80211_is_data_qos(fc)) {
		qc = ieee80211_get_qos_ctl(hdr);
		tid = qc[0] & IEEE80211_QOS_CTL_TID_MASK;
		seq_number = (le16_to_cpu(hdr->seq_ctrl) &
			     IEEE80211_SCTL_SEQ) >> 4;
		seq_number += 1;
		seq_number <<= 4;
	}
	rtlpriv->cfg->ops->fill_tx_desc(hw, hdr, (u8 *)pdesc, info, skb,
					hw_queue, &tcb_desc);
	if (!ieee80211_has_morefrags(hdr->frame_control)) {
		if (qc)
			mac->tids[tid].seq_number = seq_number;
	}
	if (ieee80211_is_data(fc))
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_TX);
}

static int rtl_usb_tx(struct ieee80211_hw *hw, struct sk_buff *skb,
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
	_rtl_usb_tx_preprocess(hw, skb, hw_queue);
	_rtl_usb_transmit(hw, skb, hw_queue);
	return NETDEV_TX_OK;

err_free:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static bool rtl_usb_tx_chk_waitq_insert(struct ieee80211_hw *hw,
					struct sk_buff *skb)
{
	return false;
}

static struct rtl_intf_ops rtl_usb_ops = {
	.adapter_start = rtl_usb_start,
	.adapter_stop = rtl_usb_stop,
	.adapter_tx = rtl_usb_tx,
	.waitq_insert = rtl_usb_tx_chk_waitq_insert,
};

int __devinit rtl_usb_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	int err;
	struct ieee80211_hw *hw = NULL;
	struct rtl_priv *rtlpriv = NULL;
	struct usb_device	*udev;
	struct rtl_usb_priv *usb_priv;

	hw = ieee80211_alloc_hw(sizeof(struct rtl_priv) +
				sizeof(struct rtl_usb_priv), &rtl_ops);
	if (!hw) {
		RT_ASSERT(false, ("%s : ieee80211 alloc failed\n", __func__));
		return -ENOMEM;
	}
	rtlpriv = hw->priv;
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
	rtlpriv->cfg = (struct rtl_hal_cfg *)(id->driver_info);
	rtlpriv->intf_ops = &rtl_usb_ops;
	rtl_dbgp_flag_init(hw);
	/* Init IO handler */
	_rtl_usb_io_handler_init(&udev->dev, hw);
	rtlpriv->cfg->ops->read_chip_version(hw);
	/*like read eeprom and so on */
	rtlpriv->cfg->ops->read_eeprom_info(hw);
	if (rtlpriv->cfg->ops->init_sw_vars(hw)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("Can't init_sw_vars.\n"));
		goto error_out;
	}
	rtlpriv->cfg->ops->init_sw_leds(hw);
	err = _rtl_usb_init(hw);
	err = _rtl_usb_init_sw(hw);
	/* Init mac80211 sw */
	err = rtl_init_core(hw);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("Can't allocate sw for mac80211.\n"));
		goto error_out;
	}

	/*init rfkill */
	/* rtl_init_rfkill(hw); */

	err = ieee80211_register_hw(hw);
	if (err) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_EMERG,
			 ("Can't register mac80211 hw.\n"));
		goto error_out;
	} else {
		rtlpriv->mac80211.mac80211_registered = 1;
	}
	set_bit(RTL_STATUS_INTERFACE_START, &rtlpriv->status);
	return 0;
error_out:
	rtl_deinit_core(hw);
	_rtl_usb_io_handler_release(hw);
	ieee80211_free_hw(hw);
	usb_put_dev(udev);
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
