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
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "../wifi.h"
#include "../usb.h"
#include "../ps.h"
#include "../base.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "rf.h"
#include "dm.h"
#include "mac.h"
#include "trx.h"
#include "../rtl8192c/fw_common.h"

static int _ConfigVerTOutEP(struct ieee80211_hw *hw)
{
	u8 ep_cfg, txqsele;
	u8 ep_nums = 0;

	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_usb_priv *usb_priv = rtl_usbpriv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(usb_priv);

	rtlusb->out_queue_sel = 0;
	ep_cfg = rtl_read_byte(rtlpriv, REG_TEST_SIE_OPTIONAL);
	ep_cfg = (ep_cfg & USB_TEST_EP_MASK) >> USB_TEST_EP_SHIFT;
	switch (ep_cfg)	{
	case 0:		/* 2 bulk OUT, 1 bulk IN */
	case 3:
		rtlusb->out_queue_sel  = TX_SELE_HQ | TX_SELE_LQ;
		ep_nums = 2;
		break;
	case 1:	/* 1 bulk IN/OUT => map all endpoint to Low queue */
	case 2:	/* 1 bulk IN, 1 bulk OUT => map all endpoint to High queue */
		txqsele = rtl_read_byte(rtlpriv, REG_TEST_USB_TXQS);
		if (txqsele & 0x0F) /* /map all endpoint to High queue */
			rtlusb->out_queue_sel =  TX_SELE_HQ;
		else if (txqsele&0xF0) /* map all endpoint to Low queue */
			rtlusb->out_queue_sel =  TX_SELE_LQ;
		ep_nums = 1;
		break;
	default:
		break;
	}
	return (rtlusb->out_ep_nums == ep_nums) ? 0 : -EINVAL;
}

static int _ConfigVerNOutEP(struct ieee80211_hw *hw)
{
	u8 ep_cfg;
	u8 ep_nums = 0;

	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_usb_priv *usb_priv = rtl_usbpriv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(usb_priv);

	rtlusb->out_queue_sel = 0;
	/* Normal and High queue */
	ep_cfg =  rtl_read_byte(rtlpriv, (REG_NORMAL_SIE_EP + 1));
	if (ep_cfg & USB_NORMAL_SIE_EP_MASK) {
		rtlusb->out_queue_sel |= TX_SELE_HQ;
		ep_nums++;
	}
	if ((ep_cfg >> USB_NORMAL_SIE_EP_SHIFT) & USB_NORMAL_SIE_EP_MASK) {
		rtlusb->out_queue_sel |= TX_SELE_NQ;
		ep_nums++;
	}
	/* Low queue */
	ep_cfg =  rtl_read_byte(rtlpriv, (REG_NORMAL_SIE_EP + 2));
	if (ep_cfg & USB_NORMAL_SIE_EP_MASK) {
		rtlusb->out_queue_sel |= TX_SELE_LQ;
		ep_nums++;
	}
	return (rtlusb->out_ep_nums == ep_nums) ? 0 : -EINVAL;
}

static void _TwoOutEpMapping(struct ieee80211_hw *hw, bool bIsChipB,
			     bool  bwificfg, struct rtl_ep_map *ep_map)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (bwificfg) { /* for WMM */
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "USB Chip-B & WMM Setting.....\n");
		ep_map->ep_mapping[RTL_TXQ_BE]	= 2;
		ep_map->ep_mapping[RTL_TXQ_BK]	= 3;
		ep_map->ep_mapping[RTL_TXQ_VI]	= 3;
		ep_map->ep_mapping[RTL_TXQ_VO] = 2;
		ep_map->ep_mapping[RTL_TXQ_MGT] = 2;
		ep_map->ep_mapping[RTL_TXQ_BCN] = 2;
		ep_map->ep_mapping[RTL_TXQ_HI]	= 2;
	} else { /* typical setting */
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "USB typical Setting.....\n");
		ep_map->ep_mapping[RTL_TXQ_BE]	= 3;
		ep_map->ep_mapping[RTL_TXQ_BK]	= 3;
		ep_map->ep_mapping[RTL_TXQ_VI]	= 2;
		ep_map->ep_mapping[RTL_TXQ_VO]	= 2;
		ep_map->ep_mapping[RTL_TXQ_MGT] = 2;
		ep_map->ep_mapping[RTL_TXQ_BCN] = 2;
		ep_map->ep_mapping[RTL_TXQ_HI]	= 2;
	}
}

static void _ThreeOutEpMapping(struct ieee80211_hw *hw, bool  bwificfg,
			       struct rtl_ep_map *ep_map)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	if (bwificfg) { /* for WMM */
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "USB 3EP Setting for WMM.....\n");
		ep_map->ep_mapping[RTL_TXQ_BE]	= 5;
		ep_map->ep_mapping[RTL_TXQ_BK]	= 3;
		ep_map->ep_mapping[RTL_TXQ_VI]	= 3;
		ep_map->ep_mapping[RTL_TXQ_VO]	= 2;
		ep_map->ep_mapping[RTL_TXQ_MGT] = 2;
		ep_map->ep_mapping[RTL_TXQ_BCN] = 2;
		ep_map->ep_mapping[RTL_TXQ_HI]	= 2;
	} else { /* typical setting */
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "USB 3EP Setting for typical.....\n");
		ep_map->ep_mapping[RTL_TXQ_BE]	= 5;
		ep_map->ep_mapping[RTL_TXQ_BK]	= 5;
		ep_map->ep_mapping[RTL_TXQ_VI]	= 3;
		ep_map->ep_mapping[RTL_TXQ_VO]	= 2;
		ep_map->ep_mapping[RTL_TXQ_MGT] = 2;
		ep_map->ep_mapping[RTL_TXQ_BCN] = 2;
		ep_map->ep_mapping[RTL_TXQ_HI]	= 2;
	}
}

static void _OneOutEpMapping(struct ieee80211_hw *hw, struct rtl_ep_map *ep_map)
{
	ep_map->ep_mapping[RTL_TXQ_BE]	= 2;
	ep_map->ep_mapping[RTL_TXQ_BK]	= 2;
	ep_map->ep_mapping[RTL_TXQ_VI]	= 2;
	ep_map->ep_mapping[RTL_TXQ_VO] = 2;
	ep_map->ep_mapping[RTL_TXQ_MGT] = 2;
	ep_map->ep_mapping[RTL_TXQ_BCN] = 2;
	ep_map->ep_mapping[RTL_TXQ_HI]	= 2;
}
static int _out_ep_mapping(struct ieee80211_hw *hw)
{
	int err = 0;
	bool bIsChipN, bwificfg = false;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_usb_priv *usb_priv = rtl_usbpriv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(usb_priv);
	struct rtl_ep_map *ep_map = &(rtlusb->ep_map);

	bIsChipN = IS_NORMAL_CHIP(rtlhal->version);
	switch (rtlusb->out_ep_nums) {
	case 2:
		_TwoOutEpMapping(hw, bIsChipN, bwificfg, ep_map);
		break;
	case 3:
		/* Test chip doesn't support three out EPs. */
		if (!bIsChipN) {
			err  =  -EINVAL;
			goto err_out;
		}
		_ThreeOutEpMapping(hw, bIsChipN, ep_map);
		break;
	case 1:
		_OneOutEpMapping(hw, ep_map);
		break;
	default:
		err  =  -EINVAL;
		break;
	}
err_out:
	return err;

}
/* endpoint mapping */
int  rtl8192cu_endpoint_mapping(struct ieee80211_hw *hw)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	int error = 0;
	if (likely(IS_NORMAL_CHIP(rtlhal->version)))
		error = _ConfigVerNOutEP(hw);
	else
		error = _ConfigVerTOutEP(hw);
	if (error)
		goto err_out;
	error = _out_ep_mapping(hw);
	if (error)
		goto err_out;
err_out:
	return error;
}

u16 rtl8192cu_mq_to_hwq(__le16 fc, u16 mac80211_queue_index)
{
	u16 hw_queue_index;

	if (unlikely(ieee80211_is_beacon(fc))) {
		hw_queue_index = RTL_TXQ_BCN;
		goto out;
	}
	if (ieee80211_is_mgmt(fc)) {
		hw_queue_index = RTL_TXQ_MGT;
		goto out;
	}
	switch (mac80211_queue_index) {
	case 0:
		hw_queue_index = RTL_TXQ_VO;
		break;
	case 1:
		hw_queue_index = RTL_TXQ_VI;
		break;
	case 2:
		hw_queue_index = RTL_TXQ_BE;
		break;
	case 3:
		hw_queue_index = RTL_TXQ_BK;
		break;
	default:
		hw_queue_index = RTL_TXQ_BE;
		RT_ASSERT(false, "QSLT_BE queue, skb_queue:%d\n",
			  mac80211_queue_index);
		break;
	}
out:
	return hw_queue_index;
}

static enum rtl_desc_qsel _rtl8192cu_mq_to_descq(struct ieee80211_hw *hw,
					 __le16 fc, u16 mac80211_queue_index)
{
	enum rtl_desc_qsel qsel;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (unlikely(ieee80211_is_beacon(fc))) {
		qsel = QSLT_BEACON;
		goto out;
	}
	if (ieee80211_is_mgmt(fc)) {
		qsel = QSLT_MGNT;
		goto out;
	}
	switch (mac80211_queue_index) {
	case 0:	/* VO */
		qsel = QSLT_VO;
		RT_TRACE(rtlpriv, COMP_USB, DBG_DMESG,
			 "VO queue, set qsel = 0x%x\n", QSLT_VO);
		break;
	case 1:	/* VI */
		qsel = QSLT_VI;
		RT_TRACE(rtlpriv, COMP_USB, DBG_DMESG,
			 "VI queue, set qsel = 0x%x\n", QSLT_VI);
		break;
	case 3:	/* BK */
		qsel = QSLT_BK;
		RT_TRACE(rtlpriv, COMP_USB, DBG_DMESG,
			 "BK queue, set qsel = 0x%x\n", QSLT_BK);
		break;
	case 2:	/* BE */
	default:
		qsel = QSLT_BE;
		RT_TRACE(rtlpriv, COMP_USB, DBG_DMESG,
			 "BE queue, set qsel = 0x%x\n", QSLT_BE);
		break;
	}
out:
	return qsel;
}

/* =============================================================== */

/*----------------------------------------------------------------------
 *
 *	Rx handler
 *
 *---------------------------------------------------------------------- */
bool rtl92cu_rx_query_desc(struct ieee80211_hw *hw,
			   struct rtl_stats *stats,
			   struct ieee80211_rx_status *rx_status,
			   u8 *pdesc, struct sk_buff *skb)
{
	struct rx_fwinfo_92c *p_drvinfo;
	struct rx_desc_92c *p_desc = (struct rx_desc_92c *)pdesc;
	u32 phystatus = GET_RX_DESC_PHY_STATUS(pdesc);

	stats->length = (u16) GET_RX_DESC_PKT_LEN(pdesc);
	stats->rx_drvinfo_size = (u8)GET_RX_DESC_DRVINFO_SIZE(pdesc) *
				 RX_DRV_INFO_SIZE_UNIT;
	stats->rx_bufshift = (u8) (GET_RX_DESC_SHIFT(pdesc) & 0x03);
	stats->icv = (u16) GET_RX_DESC_ICV(pdesc);
	stats->crc = (u16) GET_RX_DESC_CRC32(pdesc);
	stats->hwerror = (stats->crc | stats->icv);
	stats->decrypted = !GET_RX_DESC_SWDEC(pdesc);
	stats->rate = (u8) GET_RX_DESC_RX_MCS(pdesc);
	stats->shortpreamble = (u16) GET_RX_DESC_SPLCP(pdesc);
	stats->isampdu = (bool) (GET_RX_DESC_PAGGR(pdesc) == 1);
	stats->isampdu = (bool) ((GET_RX_DESC_PAGGR(pdesc) == 1)
				   && (GET_RX_DESC_FAGGR(pdesc) == 1));
	stats->timestamp_low = GET_RX_DESC_TSFL(pdesc);
	stats->rx_is40Mhzpacket = (bool) GET_RX_DESC_BW(pdesc);
	rx_status->freq = hw->conf.chandef.chan->center_freq;
	rx_status->band = hw->conf.chandef.chan->band;
	if (GET_RX_DESC_CRC32(pdesc))
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
	if (!GET_RX_DESC_SWDEC(pdesc))
		rx_status->flag |= RX_FLAG_DECRYPTED;
	if (GET_RX_DESC_BW(pdesc))
		rx_status->flag |= RX_FLAG_40MHZ;
	if (GET_RX_DESC_RX_HT(pdesc))
		rx_status->flag |= RX_FLAG_HT;
	rx_status->flag |= RX_FLAG_MACTIME_START;
	if (stats->decrypted)
		rx_status->flag |= RX_FLAG_DECRYPTED;
	rx_status->rate_idx = rtlwifi_rate_mapping(hw,
					(bool)GET_RX_DESC_RX_HT(pdesc),
					(u8)GET_RX_DESC_RX_MCS(pdesc),
					(bool)GET_RX_DESC_PAGGR(pdesc));
	rx_status->mactime = GET_RX_DESC_TSFL(pdesc);
	if (phystatus) {
		p_drvinfo = (struct rx_fwinfo_92c *)(skb->data +
						     stats->rx_bufshift);
		rtl92c_translate_rx_signal_stuff(hw, skb, stats, p_desc,
						 p_drvinfo);
	}
	/*rx_status->qual = stats->signal; */
	rx_status->signal = stats->recvsignalpower + 10;
	return true;
}

#define RTL_RX_DRV_INFO_UNIT		8

static void _rtl_rx_process(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status =
		 (struct ieee80211_rx_status *)IEEE80211_SKB_RXCB(skb);
	u32 skb_len, pkt_len, drvinfo_len;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 *rxdesc;
	struct rtl_stats stats = {
		.signal = 0,
		.rate = 0,
	};
	struct rx_fwinfo_92c *p_drvinfo;
	bool bv;
	__le16 fc;
	struct ieee80211_hdr *hdr;

	memset(rx_status, 0, sizeof(*rx_status));
	rxdesc	= skb->data;
	skb_len	= skb->len;
	drvinfo_len = (GET_RX_DESC_DRVINFO_SIZE(rxdesc) * RTL_RX_DRV_INFO_UNIT);
	pkt_len		= GET_RX_DESC_PKT_LEN(rxdesc);
	/* TODO: Error recovery. drop this skb or something. */
	WARN_ON(skb_len < (pkt_len + RTL_RX_DESC_SIZE + drvinfo_len));
	stats.length = (u16) GET_RX_DESC_PKT_LEN(rxdesc);
	stats.rx_drvinfo_size = (u8)GET_RX_DESC_DRVINFO_SIZE(rxdesc) *
				RX_DRV_INFO_SIZE_UNIT;
	stats.rx_bufshift = (u8) (GET_RX_DESC_SHIFT(rxdesc) & 0x03);
	stats.icv = (u16) GET_RX_DESC_ICV(rxdesc);
	stats.crc = (u16) GET_RX_DESC_CRC32(rxdesc);
	stats.hwerror = (stats.crc | stats.icv);
	stats.decrypted = !GET_RX_DESC_SWDEC(rxdesc);
	stats.rate = (u8) GET_RX_DESC_RX_MCS(rxdesc);
	stats.shortpreamble = (u16) GET_RX_DESC_SPLCP(rxdesc);
	stats.isampdu = (bool) ((GET_RX_DESC_PAGGR(rxdesc) == 1)
				   && (GET_RX_DESC_FAGGR(rxdesc) == 1));
	stats.timestamp_low = GET_RX_DESC_TSFL(rxdesc);
	stats.rx_is40Mhzpacket = (bool) GET_RX_DESC_BW(rxdesc);
	/* TODO: is center_freq changed when doing scan? */
	/* TODO: Shall we add protection or just skip those two step? */
	rx_status->freq = hw->conf.chandef.chan->center_freq;
	rx_status->band = hw->conf.chandef.chan->band;
	if (GET_RX_DESC_CRC32(rxdesc))
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
	if (!GET_RX_DESC_SWDEC(rxdesc))
		rx_status->flag |= RX_FLAG_DECRYPTED;
	if (GET_RX_DESC_BW(rxdesc))
		rx_status->flag |= RX_FLAG_40MHZ;
	if (GET_RX_DESC_RX_HT(rxdesc))
		rx_status->flag |= RX_FLAG_HT;
	/* Data rate */
	rx_status->rate_idx = rtlwifi_rate_mapping(hw,
					(bool)GET_RX_DESC_RX_HT(rxdesc),
					(u8)GET_RX_DESC_RX_MCS(rxdesc),
					(bool)GET_RX_DESC_PAGGR(rxdesc));
	/*  There is a phy status after this rx descriptor. */
	if (GET_RX_DESC_PHY_STATUS(rxdesc)) {
		p_drvinfo = (struct rx_fwinfo_92c *)(rxdesc + RTL_RX_DESC_SIZE);
		rtl92c_translate_rx_signal_stuff(hw, skb, &stats,
				 (struct rx_desc_92c *)rxdesc, p_drvinfo);
	}
	skb_pull(skb, (drvinfo_len + RTL_RX_DESC_SIZE));
	hdr = (struct ieee80211_hdr *)(skb->data);
	fc = hdr->frame_control;
	bv = ieee80211_is_probe_resp(fc);
	if (bv)
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "Got probe response frame\n");
	if (ieee80211_is_beacon(fc))
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "Got beacon frame\n");
	if (ieee80211_is_data(fc))
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "Got data frame\n");
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		 "Fram: fc = 0x%X addr1 = 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X\n",
		 fc,
		 (u32)hdr->addr1[0], (u32)hdr->addr1[1],
		 (u32)hdr->addr1[2], (u32)hdr->addr1[3],
		 (u32)hdr->addr1[4], (u32)hdr->addr1[5]);
	memcpy(IEEE80211_SKB_RXCB(skb), rx_status, sizeof(*rx_status));
	ieee80211_rx(hw, skb);
}

void  rtl8192cu_rx_hdl(struct ieee80211_hw *hw, struct sk_buff * skb)
{
	_rtl_rx_process(hw, skb);
}

void rtl8192c_rx_segregate_hdl(
	struct ieee80211_hw *hw,
	struct sk_buff *skb,
	struct sk_buff_head *skb_list)
{
}

/*----------------------------------------------------------------------
 *
 *	Tx handler
 *
 *---------------------------------------------------------------------- */
void rtl8192c_tx_cleanup(struct ieee80211_hw *hw, struct sk_buff  *skb)
{
}

int rtl8192c_tx_post_hdl(struct ieee80211_hw *hw, struct urb *urb,
			 struct sk_buff *skb)
{
	return 0;
}

struct sk_buff *rtl8192c_tx_aggregate_hdl(struct ieee80211_hw *hw,
					   struct sk_buff_head *list)
{
	return skb_dequeue(list);
}

/*======================================== trx ===============================*/

static void _rtl_fill_usb_tx_desc(u8 *txdesc)
{
	SET_TX_DESC_OWN(txdesc, 1);
	SET_TX_DESC_LAST_SEG(txdesc, 1);
	SET_TX_DESC_FIRST_SEG(txdesc, 1);
}
/**
 *	For HW recovery information
 */
static void _rtl_tx_desc_checksum(u8 *txdesc)
{
	u16 *ptr = (u16 *)txdesc;
	u16	checksum = 0;
	u32 index;

	/* Clear first */
	SET_TX_DESC_TX_DESC_CHECKSUM(txdesc, 0);
	for (index = 0; index < 16; index++)
		checksum = checksum ^ (*(ptr + index));
	SET_TX_DESC_TX_DESC_CHECKSUM(txdesc, checksum);
}

void rtl92cu_tx_fill_desc(struct ieee80211_hw *hw,
			  struct ieee80211_hdr *hdr, u8 *pdesc_tx,
			  u8 *pbd_desc_tx, struct ieee80211_tx_info *info,
			  struct ieee80211_sta *sta,
			  struct sk_buff *skb,
			  u8 queue_index,
			  struct rtl_tcb_desc *tcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool defaultadapter = true;
	u8 *qc = ieee80211_get_qos_ctl(hdr);
	u8 tid = qc[0] & IEEE80211_QOS_CTL_TID_MASK;
	u16 seq_number;
	__le16 fc = hdr->frame_control;
	u8 rate_flag = info->control.rates[0].flags;
	u16 pktlen = skb->len;
	enum rtl_desc_qsel fw_qsel = _rtl8192cu_mq_to_descq(hw, fc,
						skb_get_queue_mapping(skb));
	u8 *txdesc;

	seq_number = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;
	rtl_get_tcb_desc(hw, info, sta, skb, tcb_desc);
	txdesc = (u8 *)skb_push(skb, RTL_TX_HEADER_SIZE);
	memset(txdesc, 0, RTL_TX_HEADER_SIZE);
	SET_TX_DESC_PKT_SIZE(txdesc, pktlen);
	SET_TX_DESC_LINIP(txdesc, 0);
	SET_TX_DESC_PKT_OFFSET(txdesc, RTL_DUMMY_OFFSET);
	SET_TX_DESC_OFFSET(txdesc, RTL_TX_HEADER_SIZE);
	SET_TX_DESC_TX_RATE(txdesc, tcb_desc->hw_rate);
	if (tcb_desc->use_shortgi || tcb_desc->use_shortpreamble)
		SET_TX_DESC_DATA_SHORTGI(txdesc, 1);
	if (mac->tids[tid].agg.agg_state == RTL_AGG_ON &&
		    info->flags & IEEE80211_TX_CTL_AMPDU) {
		SET_TX_DESC_AGG_ENABLE(txdesc, 1);
		SET_TX_DESC_MAX_AGG_NUM(txdesc, 0x14);
	} else {
		SET_TX_DESC_AGG_BREAK(txdesc, 1);
	}
	SET_TX_DESC_SEQ(txdesc, seq_number);
	SET_TX_DESC_RTS_ENABLE(txdesc, ((tcb_desc->rts_enable &&
			       !tcb_desc->cts_enable) ? 1 : 0));
	SET_TX_DESC_HW_RTS_ENABLE(txdesc, ((tcb_desc->rts_enable ||
				  tcb_desc->cts_enable) ? 1 : 0));
	SET_TX_DESC_CTS2SELF(txdesc, ((tcb_desc->cts_enable) ? 1 : 0));
	SET_TX_DESC_RTS_STBC(txdesc, ((tcb_desc->rts_stbc) ? 1 : 0));
	SET_TX_DESC_RTS_RATE(txdesc, tcb_desc->rts_rate);
	SET_TX_DESC_RTS_BW(txdesc, 0);
	SET_TX_DESC_RTS_SC(txdesc, tcb_desc->rts_sc);
	SET_TX_DESC_RTS_SHORT(txdesc,
			      ((tcb_desc->rts_rate <= DESC92_RATE54M) ?
			       (tcb_desc->rts_use_shortpreamble ? 1 : 0)
			       : (tcb_desc->rts_use_shortgi ? 1 : 0)));
	if (mac->bw_40) {
		if (rate_flag & IEEE80211_TX_RC_DUP_DATA) {
			SET_TX_DESC_DATA_BW(txdesc, 1);
			SET_TX_DESC_DATA_SC(txdesc, 3);
		} else if(rate_flag & IEEE80211_TX_RC_40_MHZ_WIDTH){
			SET_TX_DESC_DATA_BW(txdesc, 1);
			SET_TX_DESC_DATA_SC(txdesc, mac->cur_40_prime_sc);
		} else {
			SET_TX_DESC_DATA_BW(txdesc, 0);
			SET_TX_DESC_DATA_SC(txdesc, 0);
		}
	} else {
		SET_TX_DESC_DATA_BW(txdesc, 0);
		SET_TX_DESC_DATA_SC(txdesc, 0);
	}
	rcu_read_lock();
	sta = ieee80211_find_sta(mac->vif, mac->bssid);
	if (sta) {
		u8 ampdu_density = sta->ht_cap.ampdu_density;
		SET_TX_DESC_AMPDU_DENSITY(txdesc, ampdu_density);
	}
	rcu_read_unlock();
	if (info->control.hw_key) {
		struct ieee80211_key_conf *keyconf = info->control.hw_key;
		switch (keyconf->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
		case WLAN_CIPHER_SUITE_TKIP:
			SET_TX_DESC_SEC_TYPE(txdesc, 0x1);
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			SET_TX_DESC_SEC_TYPE(txdesc, 0x3);
			break;
		default:
			SET_TX_DESC_SEC_TYPE(txdesc, 0x0);
			break;
		}
	}
	SET_TX_DESC_PKT_ID(txdesc, 0);
	SET_TX_DESC_QUEUE_SEL(txdesc, fw_qsel);
	SET_TX_DESC_DATA_RATE_FB_LIMIT(txdesc, 0x1F);
	SET_TX_DESC_RTS_RATE_FB_LIMIT(txdesc, 0xF);
	SET_TX_DESC_DISABLE_FB(txdesc, 0);
	SET_TX_DESC_USE_RATE(txdesc, tcb_desc->use_driver_rate ? 1 : 0);
	if (ieee80211_is_data_qos(fc)) {
		if (mac->rdg_en) {
			RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
				 "Enable RDG function\n");
			SET_TX_DESC_RDG_ENABLE(txdesc, 1);
			SET_TX_DESC_HTC(txdesc, 1);
		}
	}
	if (rtlpriv->dm.useramask) {
		SET_TX_DESC_RATE_ID(txdesc, tcb_desc->ratr_index);
		SET_TX_DESC_MACID(txdesc, tcb_desc->mac_id);
	} else {
		SET_TX_DESC_RATE_ID(txdesc, 0xC + tcb_desc->ratr_index);
		SET_TX_DESC_MACID(txdesc, tcb_desc->ratr_index);
	}
	if ((!ieee80211_is_data_qos(fc)) && ppsc->leisure_ps &&
	      ppsc->fwctrl_lps) {
		SET_TX_DESC_HWSEQ_EN(txdesc, 1);
		SET_TX_DESC_PKT_ID(txdesc, 8);
		if (!defaultadapter)
			SET_TX_DESC_QOS(txdesc, 1);
	}
	if (ieee80211_has_morefrags(fc))
		SET_TX_DESC_MORE_FRAG(txdesc, 1);
	if (is_multicast_ether_addr(ieee80211_get_DA(hdr)) ||
	    is_broadcast_ether_addr(ieee80211_get_DA(hdr)))
		SET_TX_DESC_BMC(txdesc, 1);
	_rtl_fill_usb_tx_desc(txdesc);
	_rtl_tx_desc_checksum(txdesc);
	RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE, "==>\n");
}

void rtl92cu_fill_fake_txdesc(struct ieee80211_hw *hw, u8 * pDesc,
			      u32 buffer_len, bool bIsPsPoll)
{
	/* Clear all status */
	memset(pDesc, 0, RTL_TX_HEADER_SIZE);
	SET_TX_DESC_FIRST_SEG(pDesc, 1); /* bFirstSeg; */
	SET_TX_DESC_LAST_SEG(pDesc, 1); /* bLastSeg; */
	SET_TX_DESC_OFFSET(pDesc, RTL_TX_HEADER_SIZE); /* Offset = 32 */
	SET_TX_DESC_PKT_SIZE(pDesc, buffer_len); /* Buffer size + command hdr */
	SET_TX_DESC_QUEUE_SEL(pDesc, QSLT_MGNT); /* Fixed queue of Mgnt queue */
	/* Set NAVUSEHDR to prevent Ps-poll AId filed to be changed to error
	 * vlaue by Hw. */
	if (bIsPsPoll) {
		SET_TX_DESC_NAV_USE_HDR(pDesc, 1);
	} else {
		SET_TX_DESC_HWSEQ_EN(pDesc, 1); /* Hw set sequence number */
		SET_TX_DESC_PKT_ID(pDesc, 0x100); /* set bit3 to 1. */
	}
	SET_TX_DESC_USE_RATE(pDesc, 1); /* use data rate which is set by Sw */
	SET_TX_DESC_OWN(pDesc, 1);
	SET_TX_DESC_TX_RATE(pDesc, DESC92_RATE1M);
	_rtl_tx_desc_checksum(pDesc);
}

void rtl92cu_tx_fill_cmddesc(struct ieee80211_hw *hw,
			     u8 *pdesc, bool firstseg,
			     bool lastseg, struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 fw_queue = QSLT_BEACON;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(skb->data);
	__le16 fc = hdr->frame_control;

	memset((void *)pdesc, 0, RTL_TX_HEADER_SIZE);
	if (firstseg)
		SET_TX_DESC_OFFSET(pdesc, RTL_TX_HEADER_SIZE);
	SET_TX_DESC_TX_RATE(pdesc, DESC92_RATE1M);
	SET_TX_DESC_SEQ(pdesc, 0);
	SET_TX_DESC_LINIP(pdesc, 0);
	SET_TX_DESC_QUEUE_SEL(pdesc, fw_queue);
	SET_TX_DESC_FIRST_SEG(pdesc, 1);
	SET_TX_DESC_LAST_SEG(pdesc, 1);
	SET_TX_DESC_RATE_ID(pdesc, 7);
	SET_TX_DESC_MACID(pdesc, 0);
	SET_TX_DESC_OWN(pdesc, 1);
	SET_TX_DESC_PKT_SIZE(pdesc, (u16)skb->len);
	SET_TX_DESC_FIRST_SEG(pdesc, 1);
	SET_TX_DESC_LAST_SEG(pdesc, 1);
	SET_TX_DESC_OFFSET(pdesc, 0x20);
	SET_TX_DESC_USE_RATE(pdesc, 1);
	if (!ieee80211_is_data_qos(fc)) {
		SET_TX_DESC_HWSEQ_EN(pdesc, 1);
		SET_TX_DESC_PKT_ID(pdesc, 8);
	}
	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_LOUD, "H2C Tx Cmd Content",
		      pdesc, RTL_TX_DESC_SIZE);
}

bool rtl92cu_cmd_send_packet(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	return true;
}
