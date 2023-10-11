// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

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

static int configvertoutep(struct ieee80211_hw *hw)
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

static int configvernoutep(struct ieee80211_hw *hw)
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

static void twooutepmapping(struct ieee80211_hw *hw, bool is_chip8,
			     bool  bwificfg, struct rtl_ep_map *ep_map)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (bwificfg) { /* for WMM */
		rtl_dbg(rtlpriv, COMP_INIT, DBG_DMESG,
			"USB Chip-B & WMM Setting.....\n");
		ep_map->ep_mapping[RTL_TXQ_BE]	= 2;
		ep_map->ep_mapping[RTL_TXQ_BK]	= 3;
		ep_map->ep_mapping[RTL_TXQ_VI]	= 3;
		ep_map->ep_mapping[RTL_TXQ_VO] = 2;
		ep_map->ep_mapping[RTL_TXQ_MGT] = 2;
		ep_map->ep_mapping[RTL_TXQ_BCN] = 2;
		ep_map->ep_mapping[RTL_TXQ_HI]	= 2;
	} else { /* typical setting */
		rtl_dbg(rtlpriv, COMP_INIT, DBG_DMESG,
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

static void threeoutepmapping(struct ieee80211_hw *hw, bool  bwificfg,
			       struct rtl_ep_map *ep_map)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (bwificfg) { /* for WMM */
		rtl_dbg(rtlpriv, COMP_INIT, DBG_DMESG,
			"USB 3EP Setting for WMM.....\n");
		ep_map->ep_mapping[RTL_TXQ_BE]	= 5;
		ep_map->ep_mapping[RTL_TXQ_BK]	= 3;
		ep_map->ep_mapping[RTL_TXQ_VI]	= 3;
		ep_map->ep_mapping[RTL_TXQ_VO]	= 2;
		ep_map->ep_mapping[RTL_TXQ_MGT] = 2;
		ep_map->ep_mapping[RTL_TXQ_BCN] = 2;
		ep_map->ep_mapping[RTL_TXQ_HI]	= 2;
	} else { /* typical setting */
		rtl_dbg(rtlpriv, COMP_INIT, DBG_DMESG,
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

static void oneoutepmapping(struct ieee80211_hw *hw, struct rtl_ep_map *ep_map)
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
	bool ischipn, bwificfg = false;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_usb_priv *usb_priv = rtl_usbpriv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(usb_priv);
	struct rtl_ep_map *ep_map = &(rtlusb->ep_map);

	ischipn = IS_NORMAL_CHIP(rtlhal->version);
	switch (rtlusb->out_ep_nums) {
	case 2:
		twooutepmapping(hw, ischipn, bwificfg, ep_map);
		break;
	case 3:
		/* Test chip doesn't support three out EPs. */
		if (!ischipn) {
			err  =  -EINVAL;
			goto err_out;
		}
		threeoutepmapping(hw, ischipn, ep_map);
		break;
	case 1:
		oneoutepmapping(hw, ep_map);
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
		error = configvernoutep(hw);
	else
		error = configvertoutep(hw);
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
		WARN_ONCE(true, "rtl8192cu: QSLT_BE queue, skb_queue:%d\n",
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
		rtl_dbg(rtlpriv, COMP_USB, DBG_DMESG,
			"VO queue, set qsel = 0x%x\n", QSLT_VO);
		break;
	case 1:	/* VI */
		qsel = QSLT_VI;
		rtl_dbg(rtlpriv, COMP_USB, DBG_DMESG,
			"VI queue, set qsel = 0x%x\n", QSLT_VI);
		break;
	case 3:	/* BK */
		qsel = QSLT_BK;
		rtl_dbg(rtlpriv, COMP_USB, DBG_DMESG,
			"BK queue, set qsel = 0x%x\n", QSLT_BK);
		break;
	case 2:	/* BE */
	default:
		qsel = QSLT_BE;
		rtl_dbg(rtlpriv, COMP_USB, DBG_DMESG,
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
			   u8 *pdesc8, struct sk_buff *skb)
{
	struct rx_fwinfo_92c *p_drvinfo;
	struct rx_desc_92c *p_desc = (struct rx_desc_92c *)pdesc8;
	__le32 *pdesc = (__le32 *)pdesc8;
	u32 phystatus = get_rx_desc_phy_status(pdesc);

	stats->length = (u16)get_rx_desc_pkt_len(pdesc);
	stats->rx_drvinfo_size = (u8)get_rx_desc_drvinfo_size(pdesc) *
				 RX_DRV_INFO_SIZE_UNIT;
	stats->rx_bufshift = (u8)(get_rx_desc_shift(pdesc) & 0x03);
	stats->icv = (u16)get_rx_desc_icv(pdesc);
	stats->crc = (u16)get_rx_desc_crc32(pdesc);
	stats->hwerror = (stats->crc | stats->icv);
	stats->decrypted = !get_rx_desc_swdec(pdesc);
	stats->rate = (u8)get_rx_desc_rx_mcs(pdesc);
	stats->shortpreamble = (u16)get_rx_desc_splcp(pdesc);
	stats->isampdu = (bool)(get_rx_desc_paggr(pdesc) == 1);
	stats->isfirst_ampdu = (bool)((get_rx_desc_paggr(pdesc) == 1) &&
				      (get_rx_desc_faggr(pdesc) == 1));
	stats->timestamp_low = get_rx_desc_tsfl(pdesc);
	stats->rx_is40mhzpacket = (bool)get_rx_desc_bw(pdesc);
	stats->is_ht = (bool)get_rx_desc_rx_ht(pdesc);
	rx_status->freq = hw->conf.chandef.chan->center_freq;
	rx_status->band = hw->conf.chandef.chan->band;
	if (get_rx_desc_crc32(pdesc))
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
	if (!get_rx_desc_swdec(pdesc))
		rx_status->flag |= RX_FLAG_DECRYPTED;
	if (get_rx_desc_bw(pdesc))
		rx_status->bw = RATE_INFO_BW_40;
	if (get_rx_desc_rx_ht(pdesc))
		rx_status->encoding = RX_ENC_HT;
	rx_status->flag |= RX_FLAG_MACTIME_START;
	if (stats->decrypted)
		rx_status->flag |= RX_FLAG_DECRYPTED;
	rx_status->rate_idx = rtlwifi_rate_mapping(hw, stats->is_ht,
						   false, stats->rate);
	rx_status->mactime = get_rx_desc_tsfl(pdesc);
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
	__le32 *rxdesc;
	struct rtl_stats stats = {
		.signal = 0,
		.rate = 0,
	};
	struct rx_fwinfo_92c *p_drvinfo;
	bool bv;
	__le16 fc;
	struct ieee80211_hdr *hdr;

	memset(rx_status, 0, sizeof(*rx_status));
	rxdesc	= (__le32 *)skb->data;
	skb_len	= skb->len;
	drvinfo_len = (get_rx_desc_drvinfo_size(rxdesc) * RTL_RX_DRV_INFO_UNIT);
	pkt_len		= get_rx_desc_pkt_len(rxdesc);
	/* TODO: Error recovery. drop this skb or something. */
	WARN_ON(skb_len < (pkt_len + RTL_RX_DESC_SIZE + drvinfo_len));
	stats.length = (u16)get_rx_desc_pkt_len(rxdesc);
	stats.rx_drvinfo_size = (u8)get_rx_desc_drvinfo_size(rxdesc) *
				RX_DRV_INFO_SIZE_UNIT;
	stats.rx_bufshift = (u8)(get_rx_desc_shift(rxdesc) & 0x03);
	stats.icv = (u16)get_rx_desc_icv(rxdesc);
	stats.crc = (u16)get_rx_desc_crc32(rxdesc);
	stats.hwerror = (stats.crc | stats.icv);
	stats.decrypted = !get_rx_desc_swdec(rxdesc);
	stats.rate = (u8)get_rx_desc_rx_mcs(rxdesc);
	stats.shortpreamble = (u16)get_rx_desc_splcp(rxdesc);
	stats.isampdu = (bool)((get_rx_desc_paggr(rxdesc) == 1) &&
			       (get_rx_desc_faggr(rxdesc) == 1));
	stats.timestamp_low = get_rx_desc_tsfl(rxdesc);
	stats.rx_is40mhzpacket = (bool)get_rx_desc_bw(rxdesc);
	stats.is_ht = (bool)get_rx_desc_rx_ht(rxdesc);
	/* TODO: is center_freq changed when doing scan? */
	/* TODO: Shall we add protection or just skip those two step? */
	rx_status->freq = hw->conf.chandef.chan->center_freq;
	rx_status->band = hw->conf.chandef.chan->band;
	if (get_rx_desc_crc32(rxdesc))
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
	if (!get_rx_desc_swdec(rxdesc))
		rx_status->flag |= RX_FLAG_DECRYPTED;
	if (get_rx_desc_bw(rxdesc))
		rx_status->bw = RATE_INFO_BW_40;
	if (get_rx_desc_rx_ht(rxdesc))
		rx_status->encoding = RX_ENC_HT;
	/* Data rate */
	rx_status->rate_idx = rtlwifi_rate_mapping(hw, stats.is_ht,
						   false, stats.rate);
	/*  There is a phy status after this rx descriptor. */
	if (get_rx_desc_phy_status(rxdesc)) {
		p_drvinfo = (struct rx_fwinfo_92c *)(rxdesc + RTL_RX_DESC_SIZE);
		rtl92c_translate_rx_signal_stuff(hw, skb, &stats,
				 (struct rx_desc_92c *)rxdesc, p_drvinfo);
	}
	skb_pull(skb, (drvinfo_len + RTL_RX_DESC_SIZE));
	hdr = rtl_get_hdr(skb);
	fc = hdr->frame_control;
	bv = ieee80211_is_probe_resp(fc);
	if (bv)
		rtl_dbg(rtlpriv, COMP_INIT, DBG_DMESG,
			"Got probe response frame\n");
	if (ieee80211_is_beacon(fc))
		rtl_dbg(rtlpriv, COMP_INIT, DBG_DMESG, "Got beacon frame\n");
	if (ieee80211_is_data(fc))
		rtl_dbg(rtlpriv, COMP_INIT, DBG_DMESG, "Got data frame\n");
	rtl_dbg(rtlpriv, COMP_INIT, DBG_DMESG,
		"Fram: fc = 0x%X addr1 = 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X\n",
		fc,
		(u32)hdr->addr1[0], (u32)hdr->addr1[1],
		(u32)hdr->addr1[2], (u32)hdr->addr1[3],
		(u32)hdr->addr1[4], (u32)hdr->addr1[5]);
	ieee80211_rx(hw, skb);
}

void  rtl8192cu_rx_hdl(struct ieee80211_hw *hw, struct sk_buff * skb)
{
	_rtl_rx_process(hw, skb);
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

static void _rtl_fill_usb_tx_desc(__le32 *txdesc)
{
	set_tx_desc_own(txdesc, 1);
	set_tx_desc_last_seg(txdesc, 1);
	set_tx_desc_first_seg(txdesc, 1);
}

/*
 *	For HW recovery information
 */
static void _rtl_tx_desc_checksum(__le32 *txdesc)
{
	__le16 *ptr = (__le16 *)txdesc;
	u16	checksum = 0;
	u32 index;

	/* Clear first */
	set_tx_desc_tx_desc_checksum(txdesc, 0);
	for (index = 0; index < 16; index++)
		checksum = checksum ^ le16_to_cpu(*(ptr + index));
	set_tx_desc_tx_desc_checksum(txdesc, checksum);
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
	u8 *txdesc8;
	__le32 *txdesc;

	seq_number = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;
	rtl_get_tcb_desc(hw, info, sta, skb, tcb_desc);
	txdesc8 = skb_push(skb, RTL_TX_HEADER_SIZE);
	txdesc = (__le32 *)txdesc8;
	memset(txdesc, 0, RTL_TX_HEADER_SIZE);
	set_tx_desc_pkt_size(txdesc, pktlen);
	set_tx_desc_linip(txdesc, 0);
	set_tx_desc_pkt_offset(txdesc, RTL_DUMMY_OFFSET);
	set_tx_desc_offset(txdesc, RTL_TX_HEADER_SIZE);
	set_tx_desc_tx_rate(txdesc, tcb_desc->hw_rate);
	if (tcb_desc->use_shortgi || tcb_desc->use_shortpreamble)
		set_tx_desc_data_shortgi(txdesc, 1);
	if (mac->tids[tid].agg.agg_state == RTL_AGG_ON &&
		    info->flags & IEEE80211_TX_CTL_AMPDU) {
		set_tx_desc_agg_enable(txdesc, 1);
		set_tx_desc_max_agg_num(txdesc, 0x14);
	} else {
		set_tx_desc_agg_break(txdesc, 1);
	}
	set_tx_desc_seq(txdesc, seq_number);
	set_tx_desc_rts_enable(txdesc,
			       ((tcb_desc->rts_enable &&
				!tcb_desc->cts_enable) ? 1 : 0));
	set_tx_desc_hw_rts_enable(txdesc,
				  ((tcb_desc->rts_enable ||
				   tcb_desc->cts_enable) ? 1 : 0));
	set_tx_desc_cts2self(txdesc, ((tcb_desc->cts_enable) ? 1 : 0));
	set_tx_desc_rts_stbc(txdesc, ((tcb_desc->rts_stbc) ? 1 : 0));
	set_tx_desc_rts_rate(txdesc, tcb_desc->rts_rate);
	set_tx_desc_rts_bw(txdesc, 0);
	set_tx_desc_rts_sc(txdesc, tcb_desc->rts_sc);
	set_tx_desc_rts_short(txdesc,
			      ((tcb_desc->rts_rate <= DESC_RATE54M) ?
			       (tcb_desc->rts_use_shortpreamble ? 1 : 0)
			       : (tcb_desc->rts_use_shortgi ? 1 : 0)));
	if (mac->bw_40) {
		if (rate_flag & IEEE80211_TX_RC_DUP_DATA) {
			set_tx_desc_data_bw(txdesc, 1);
			set_tx_desc_data_sc(txdesc, 3);
		} else if(rate_flag & IEEE80211_TX_RC_40_MHZ_WIDTH){
			set_tx_desc_data_bw(txdesc, 1);
			set_tx_desc_data_sc(txdesc, mac->cur_40_prime_sc);
		} else {
			set_tx_desc_data_bw(txdesc, 0);
			set_tx_desc_data_sc(txdesc, 0);
		}
	} else {
		set_tx_desc_data_bw(txdesc, 0);
		set_tx_desc_data_sc(txdesc, 0);
	}
	rcu_read_lock();
	sta = ieee80211_find_sta(mac->vif, mac->bssid);
	if (sta) {
		u8 ampdu_density = sta->deflink.ht_cap.ampdu_density;

		set_tx_desc_ampdu_density(txdesc, ampdu_density);
	}
	rcu_read_unlock();
	if (info->control.hw_key) {
		struct ieee80211_key_conf *keyconf = info->control.hw_key;

		switch (keyconf->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
		case WLAN_CIPHER_SUITE_TKIP:
			set_tx_desc_sec_type(txdesc, 0x1);
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			set_tx_desc_sec_type(txdesc, 0x3);
			break;
		default:
			set_tx_desc_sec_type(txdesc, 0x0);
			break;
		}
	}
	set_tx_desc_pkt_id(txdesc, 0);
	set_tx_desc_queue_sel(txdesc, fw_qsel);
	set_tx_desc_data_rate_fb_limit(txdesc, 0x1F);
	set_tx_desc_rts_rate_fb_limit(txdesc, 0xF);
	set_tx_desc_disable_fb(txdesc, 0);
	set_tx_desc_use_rate(txdesc, tcb_desc->use_driver_rate ? 1 : 0);
	if (ieee80211_is_data_qos(fc)) {
		if (mac->rdg_en) {
			rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE,
				"Enable RDG function\n");
			set_tx_desc_rdg_enable(txdesc, 1);
			set_tx_desc_htc(txdesc, 1);
		}
	}
	if (rtlpriv->dm.useramask) {
		set_tx_desc_rate_id(txdesc, tcb_desc->ratr_index);
		set_tx_desc_macid(txdesc, tcb_desc->mac_id);
	} else {
		set_tx_desc_rate_id(txdesc, 0xC + tcb_desc->ratr_index);
		set_tx_desc_macid(txdesc, tcb_desc->ratr_index);
	}
	if ((!ieee80211_is_data_qos(fc)) && ppsc->leisure_ps &&
	      ppsc->fwctrl_lps) {
		set_tx_desc_hwseq_en(txdesc, 1);
		set_tx_desc_pkt_id(txdesc, 8);
		if (!defaultadapter)
			set_tx_desc_qos(txdesc, 1);
	}
	if (ieee80211_has_morefrags(fc))
		set_tx_desc_more_frag(txdesc, 1);
	if (is_multicast_ether_addr(ieee80211_get_DA(hdr)) ||
	    is_broadcast_ether_addr(ieee80211_get_DA(hdr)))
		set_tx_desc_bmc(txdesc, 1);
	_rtl_fill_usb_tx_desc(txdesc);
	_rtl_tx_desc_checksum(txdesc);
	rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE, "==>\n");
}

void rtl92cu_fill_fake_txdesc(struct ieee80211_hw *hw, u8 *pdesc8,
			      u32 buffer_len, bool is_pspoll)
{
	__le32 *pdesc = (__le32 *)pdesc8;

	/* Clear all status */
	memset(pdesc, 0, RTL_TX_HEADER_SIZE);
	set_tx_desc_first_seg(pdesc, 1); /* bFirstSeg; */
	set_tx_desc_last_seg(pdesc, 1); /* bLastSeg; */
	set_tx_desc_offset(pdesc, RTL_TX_HEADER_SIZE); /* Offset = 32 */
	set_tx_desc_pkt_size(pdesc, buffer_len); /* Buffer size + command hdr */
	set_tx_desc_queue_sel(pdesc, QSLT_MGNT); /* Fixed queue of Mgnt queue */
	/* Set NAVUSEHDR to prevent Ps-poll AId filed to be changed to error
	 * vlaue by Hw. */
	if (is_pspoll) {
		set_tx_desc_nav_use_hdr(pdesc, 1);
	} else {
		set_tx_desc_hwseq_en(pdesc, 1); /* Hw set sequence number */
		set_tx_desc_pkt_id(pdesc, BIT(3)); /* set bit3 to 1. */
	}
	set_tx_desc_use_rate(pdesc, 1); /* use data rate which is set by Sw */
	set_tx_desc_own(pdesc, 1);
	set_tx_desc_tx_rate(pdesc, DESC_RATE1M);
	_rtl_tx_desc_checksum(pdesc);
}

void rtl92cu_tx_fill_cmddesc(struct ieee80211_hw *hw, u8 *pdesc8,
			     struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 fw_queue = QSLT_BEACON;
	struct ieee80211_hdr *hdr = rtl_get_hdr(skb);
	__le16 fc = hdr->frame_control;
	__le32 *pdesc = (__le32 *)pdesc8;

	memset((void *)pdesc, 0, RTL_TX_HEADER_SIZE);
	set_tx_desc_offset(pdesc, RTL_TX_HEADER_SIZE);
	set_tx_desc_tx_rate(pdesc, DESC_RATE1M);
	set_tx_desc_seq(pdesc, 0);
	set_tx_desc_linip(pdesc, 0);
	set_tx_desc_queue_sel(pdesc, fw_queue);
	set_tx_desc_first_seg(pdesc, 1);
	set_tx_desc_last_seg(pdesc, 1);
	set_tx_desc_rate_id(pdesc, 7);
	set_tx_desc_macid(pdesc, 0);
	set_tx_desc_own(pdesc, 1);
	set_tx_desc_pkt_size(pdesc, (u16)skb->len);
	set_tx_desc_first_seg(pdesc, 1);
	set_tx_desc_last_seg(pdesc, 1);
	set_tx_desc_offset(pdesc, 0x20);
	set_tx_desc_use_rate(pdesc, 1);
	if (!ieee80211_is_data_qos(fc)) {
		set_tx_desc_hwseq_en(pdesc, 1);
		set_tx_desc_pkt_id(pdesc, 8);
	}
	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_LOUD, "H2C Tx Cmd Content",
		      pdesc, RTL_TX_DESC_SIZE);
}
