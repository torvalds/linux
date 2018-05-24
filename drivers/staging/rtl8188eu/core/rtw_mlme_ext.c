/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/
#define _RTW_MLME_EXT_C_

#include <linux/ieee80211.h>
#include <asm/unaligned.h>

#include <osdep_service.h>
#include <drv_types.h>
#include <wifi.h>
#include <rtw_mlme_ext.h>
#include <wlan_bssdef.h>
#include <mlme_osdep.h>
#include <recv_osdep.h>

static u8 null_addr[ETH_ALEN] = {0, 0, 0, 0, 0, 0};

/**************************************************
OUI definitions for the vendor specific IE
***************************************************/
unsigned char	RTW_WPA_OUI[] = {0x00, 0x50, 0xf2, 0x01};
unsigned char WMM_OUI[] = {0x00, 0x50, 0xf2, 0x02};
unsigned char	WPS_OUI[] = {0x00, 0x50, 0xf2, 0x04};
unsigned char	P2P_OUI[] = {0x50, 0x6F, 0x9A, 0x09};
unsigned char	WFD_OUI[] = {0x50, 0x6F, 0x9A, 0x0A};

unsigned char	WMM_INFO_OUI[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01};
unsigned char	WMM_PARA_OUI[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};

unsigned char WPA_TKIP_CIPHER[4] = {0x00, 0x50, 0xf2, 0x02};
unsigned char RSN_TKIP_CIPHER[4] = {0x00, 0x0f, 0xac, 0x02};

extern unsigned char REALTEK_96B_IE[];

/********************************************************
MCS rate definitions
*********************************************************/
unsigned char	MCS_rate_1R[16] = {0xff, 0x00, 0x0, 0x0, 0x01, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

/********************************************************
ChannelPlan definitions
*********************************************************/
static struct rt_channel_plan_2g RTW_ChannelPlan2G[RT_CHANNEL_DOMAIN_2G_MAX] = {
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},		/*  0x00, RT_CHANNEL_DOMAIN_2G_WORLD , Passive scan CH 12, 13 */
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},		/*  0x01, RT_CHANNEL_DOMAIN_2G_ETSI1 */
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, 11},			/*  0x02, RT_CHANNEL_DOMAIN_2G_FCC1 */
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}, 14},	/*  0x03, RT_CHANNEL_DOMAIN_2G_MIKK1 */
	{{10, 11, 12, 13}, 4},					/*  0x04, RT_CHANNEL_DOMAIN_2G_ETSI2 */
	{{}, 0},									/*  0x05, RT_CHANNEL_DOMAIN_2G_NULL */
};

static struct rt_channel_plan_map	RTW_ChannelPlanMap[RT_CHANNEL_DOMAIN_MAX] = {
	/*  0x00 ~ 0x1F , Old Define ===== */
	{0x02},	/* 0x00, RT_CHANNEL_DOMAIN_FCC */
	{0x02},	/* 0x01, RT_CHANNEL_DOMAIN_IC */
	{0x01},	/* 0x02, RT_CHANNEL_DOMAIN_ETSI */
	{0x01},	/* 0x03, RT_CHANNEL_DOMAIN_SPAIN */
	{0x01},	/* 0x04, RT_CHANNEL_DOMAIN_FRANCE */
	{0x03},	/* 0x05, RT_CHANNEL_DOMAIN_MKK */
	{0x03},	/* 0x06, RT_CHANNEL_DOMAIN_MKK1 */
	{0x01},	/* 0x07, RT_CHANNEL_DOMAIN_ISRAEL */
	{0x03},	/* 0x08, RT_CHANNEL_DOMAIN_TELEC */
	{0x03},	/* 0x09, RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN */
	{0x00},	/* 0x0A, RT_CHANNEL_DOMAIN_WORLD_WIDE_13 */
	{0x02},	/* 0x0B, RT_CHANNEL_DOMAIN_TAIWAN */
	{0x01},	/* 0x0C, RT_CHANNEL_DOMAIN_CHINA */
	{0x02},	/* 0x0D, RT_CHANNEL_DOMAIN_SINGAPORE_INDIA_MEXICO */
	{0x02},	/* 0x0E, RT_CHANNEL_DOMAIN_KOREA */
	{0x02},	/* 0x0F, RT_CHANNEL_DOMAIN_TURKEY */
	{0x01},	/* 0x10, RT_CHANNEL_DOMAIN_JAPAN */
	{0x02},	/* 0x11, RT_CHANNEL_DOMAIN_FCC_NO_DFS */
	{0x01},	/* 0x12, RT_CHANNEL_DOMAIN_JAPAN_NO_DFS */
	{0x00},	/* 0x13, RT_CHANNEL_DOMAIN_WORLD_WIDE_5G */
	{0x02},	/* 0x14, RT_CHANNEL_DOMAIN_TAIWAN_NO_DFS */
	{0x00},	/* 0x15, RT_CHANNEL_DOMAIN_ETSI_NO_DFS */
	{0x00},	/* 0x16, RT_CHANNEL_DOMAIN_KOREA_NO_DFS */
	{0x03},	/* 0x17, RT_CHANNEL_DOMAIN_JAPAN_NO_DFS */
	{0x05},	/* 0x18, RT_CHANNEL_DOMAIN_PAKISTAN_NO_DFS */
	{0x02},	/* 0x19, RT_CHANNEL_DOMAIN_TAIWAN2_NO_DFS */
	{0x00},	/* 0x1A, */
	{0x00},	/* 0x1B, */
	{0x00},	/* 0x1C, */
	{0x00},	/* 0x1D, */
	{0x00},	/* 0x1E, */
	{0x05},	/* 0x1F, RT_CHANNEL_DOMAIN_WORLD_WIDE_ONLY_5G */
	/*  0x20 ~ 0x7F , New Define ===== */
	{0x00},	/* 0x20, RT_CHANNEL_DOMAIN_WORLD_NULL */
	{0x01},	/* 0x21, RT_CHANNEL_DOMAIN_ETSI1_NULL */
	{0x02},	/* 0x22, RT_CHANNEL_DOMAIN_FCC1_NULL */
	{0x03},	/* 0x23, RT_CHANNEL_DOMAIN_MKK1_NULL */
	{0x04},	/* 0x24, RT_CHANNEL_DOMAIN_ETSI2_NULL */
	{0x02},	/* 0x25, RT_CHANNEL_DOMAIN_FCC1_FCC1 */
	{0x00},	/* 0x26, RT_CHANNEL_DOMAIN_WORLD_ETSI1 */
	{0x03},	/* 0x27, RT_CHANNEL_DOMAIN_MKK1_MKK1 */
	{0x00},	/* 0x28, RT_CHANNEL_DOMAIN_WORLD_KCC1 */
	{0x00},	/* 0x29, RT_CHANNEL_DOMAIN_WORLD_FCC2 */
	{0x00},	/* 0x2A, */
	{0x00},	/* 0x2B, */
	{0x00},	/* 0x2C, */
	{0x00},	/* 0x2D, */
	{0x00},	/* 0x2E, */
	{0x00},	/* 0x2F, */
	{0x00},	/* 0x30, RT_CHANNEL_DOMAIN_WORLD_FCC3 */
	{0x00},	/* 0x31, RT_CHANNEL_DOMAIN_WORLD_FCC4 */
	{0x00},	/* 0x32, RT_CHANNEL_DOMAIN_WORLD_FCC5 */
	{0x00},	/* 0x33, RT_CHANNEL_DOMAIN_WORLD_FCC6 */
	{0x02},	/* 0x34, RT_CHANNEL_DOMAIN_FCC1_FCC7 */
	{0x00},	/* 0x35, RT_CHANNEL_DOMAIN_WORLD_ETSI2 */
	{0x00},	/* 0x36, RT_CHANNEL_DOMAIN_WORLD_ETSI3 */
	{0x03},	/* 0x37, RT_CHANNEL_DOMAIN_MKK1_MKK2 */
	{0x03},	/* 0x38, RT_CHANNEL_DOMAIN_MKK1_MKK3 */
	{0x02},	/* 0x39, RT_CHANNEL_DOMAIN_FCC1_NCC1 */
	{0x00},	/* 0x3A, */
	{0x00},	/* 0x3B, */
	{0x00},	/* 0x3C, */
	{0x00},	/* 0x3D, */
	{0x00},	/* 0x3E, */
	{0x00},	/* 0x3F, */
	{0x02},	/* 0x40, RT_CHANNEL_DOMAIN_FCC1_NCC2 */
	{0x03},	/* 0x41, RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN_2G */
};

static const struct rt_channel_plan_map RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE = {
	0x03
}; /* use the combination for max channel numbers */

/*
 * Search the @param channel_num in given @param channel_set
 * @ch_set: the given channel set
 * @ch: the given channel number
 *
 * return the index of channel_num in channel_set, -1 if not found
 */
int rtw_ch_set_search_ch(struct rt_channel_info *ch_set, const u32 ch)
{
	int i;

	for (i = 0; ch_set[i].ChannelNum != 0; i++) {
		if (ch == ch_set[i].ChannelNum)
			break;
	}

	if (i >= ch_set[i].ChannelNum)
		return -1;
	return i;
}

struct xmit_frame *alloc_mgtxmitframe(struct xmit_priv *pxmitpriv)
{
	struct xmit_frame			*pmgntframe;
	struct xmit_buf				*pxmitbuf;

	pmgntframe = rtw_alloc_xmitframe(pxmitpriv);
	if (!pmgntframe) {
		DBG_88E("%s, alloc xmitframe fail\n", __func__);
		return NULL;
	}

	pxmitbuf = rtw_alloc_xmitbuf_ext(pxmitpriv);
	if (!pxmitbuf) {
		DBG_88E("%s, alloc xmitbuf fail\n", __func__);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		return NULL;
	}
	pmgntframe->frame_tag = MGNT_FRAMETAG;
	pmgntframe->pxmitbuf = pxmitbuf;
	pmgntframe->buf_addr = pxmitbuf->pbuf;
	pxmitbuf->priv_data = pmgntframe;
	return pmgntframe;
}

/****************************************************************************

Following are some TX functions for WiFi MLME

*****************************************************************************/

void update_mgnt_tx_rate(struct adapter *padapter, u8 rate)
{
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);

	pmlmeext->tx_rate = rate;
	DBG_88E("%s(): rate = %x\n", __func__, rate);
}

void update_mgntframe_attrib(struct adapter *padapter, struct pkt_attrib *pattrib)
{
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);

	memset((u8 *)(pattrib), 0, sizeof(struct pkt_attrib));

	pattrib->hdrlen = 24;
	pattrib->nr_frags = 1;
	pattrib->priority = 7;
	pattrib->mac_id = 0;
	pattrib->qsel = 0x12;

	pattrib->pktlen = 0;

	if (pmlmeext->cur_wireless_mode & WIRELESS_11B)
		pattrib->raid = 6;/* b mode */
	else
		pattrib->raid = 5;/* a/g mode */

	pattrib->encrypt = _NO_PRIVACY_;
	pattrib->bswenc = false;

	pattrib->qos_en = false;
	pattrib->ht_en = false;
	pattrib->bwmode = HT_CHANNEL_WIDTH_20;
	pattrib->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	pattrib->sgi = false;

	pattrib->seqnum = pmlmeext->mgnt_seq;

	pattrib->retry_ctrl = true;
}

static void dump_mgntframe(struct adapter *padapter,
			   struct xmit_frame *pmgntframe)
{
	if (padapter->bSurpriseRemoved || padapter->bDriverStopped)
		return;

	rtw_hal_mgnt_xmit(padapter, pmgntframe);
}

static s32 dump_mgntframe_and_wait(struct adapter *padapter,
				   struct xmit_frame *pmgntframe,
				   int timeout_ms)
{
	s32 ret = _FAIL;
	struct xmit_buf *pxmitbuf = pmgntframe->pxmitbuf;
	struct submit_ctx sctx;

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped)
		return ret;

	rtw_sctx_init(&sctx, timeout_ms);
	pxmitbuf->sctx = &sctx;

	ret = rtw_hal_mgnt_xmit(padapter, pmgntframe);

	if (ret == _SUCCESS)
		ret = rtw_sctx_wait(&sctx);

	return ret;
}

static s32 dump_mgntframe_and_wait_ack(struct adapter *padapter,
				       struct xmit_frame *pmgntframe)
{
	s32 ret = _FAIL;
	u32 timeout_ms = 500;/*   500ms */
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped)
		return -1;

	if (mutex_lock_interruptible(&pxmitpriv->ack_tx_mutex))
		return _FAIL;
	pxmitpriv->ack_tx = true;

	pmgntframe->ack_report = 1;
	if (rtw_hal_mgnt_xmit(padapter, pmgntframe) == _SUCCESS)
		ret = rtw_ack_tx_wait(pxmitpriv, timeout_ms);

	pxmitpriv->ack_tx = false;
	mutex_unlock(&pxmitpriv->ack_tx_mutex);

	return ret;
}

static int update_hidden_ssid(u8 *ies, u32 ies_len, u8 hidden_ssid_mode)
{
	u8 *ssid_ie;
	uint ssid_len_ori;
	int len_diff = 0;

	ssid_ie = rtw_get_ie(ies,  WLAN_EID_SSID, &ssid_len_ori, ies_len);

	if (ssid_ie && ssid_len_ori > 0) {
		switch (hidden_ssid_mode) {
		case 1: {
			u8 *next_ie = ssid_ie + 2 + ssid_len_ori;
			u32 remain_len = 0;

			remain_len = ies_len - (next_ie - ies);

			ssid_ie[1] = 0;
			memcpy(ssid_ie+2, next_ie, remain_len);
			len_diff -= ssid_len_ori;

			break;
		}
		case 2:
			memset(&ssid_ie[2], 0, ssid_len_ori);
			break;
		default:
			break;
		}
	}

	return len_diff;
}

static void issue_beacon(struct adapter *padapter, int timeout_ms)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	unsigned char	*pframe;
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	unsigned int	rate_len;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*cur_network = &(pmlmeinfo->network);
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe) {
		DBG_88E("%s, alloc mgnt frame fail\n", __func__);
		return;
	}
#if defined(CONFIG_88EU_AP_MODE)
	spin_lock_bh(&pmlmepriv->bcn_update_lock);
#endif /* if defined (CONFIG_88EU_AP_MODE) */

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->qsel = 0x10;

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;


	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;

	ether_addr_copy(pwlanhdr->addr1, bc_addr);
	ether_addr_copy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)));
	ether_addr_copy(pwlanhdr->addr3, cur_network->MacAddress);

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	/* pmlmeext->mgnt_seq++; */
	SetFrameSubType(pframe, WIFI_BEACON);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE) {
		int len_diff;
		u8 *wps_ie;
		uint wps_ielen;
		u8 sr = 0;

		memcpy(pframe, cur_network->IEs, cur_network->IELength);
		len_diff = update_hidden_ssid(
			pframe+_BEACON_IE_OFFSET_
			, cur_network->IELength-_BEACON_IE_OFFSET_
			, pmlmeinfo->hidden_ssid_mode
			);
		pframe += (cur_network->IELength+len_diff);
		pattrib->pktlen += (cur_network->IELength+len_diff);
		wps_ie = rtw_get_wps_ie(pmgntframe->buf_addr+TXDESC_OFFSET+sizeof(struct ieee80211_hdr_3addr)+_BEACON_IE_OFFSET_,
			pattrib->pktlen-sizeof(struct ieee80211_hdr_3addr)-_BEACON_IE_OFFSET_, NULL, &wps_ielen);
		if (wps_ie && wps_ielen > 0)
			rtw_get_wps_attr_content(wps_ie,  wps_ielen, WPS_ATTR_SELECTED_REGISTRAR, (u8 *)(&sr), NULL);
		if (sr != 0)
			set_fwstate(pmlmepriv, WIFI_UNDER_WPS);
		else
			_clr_fwstate_(pmlmepriv, WIFI_UNDER_WPS);

		goto _issue_bcn;
	}

	/* below for ad-hoc mode */

	/* timestamp will be inserted by hardware */
	pframe += 8;
	pattrib->pktlen += 8;

	/*  beacon interval: 2 bytes */

	memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pattrib->pktlen += 2;

	/*  capability info: 2 bytes */

	memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pattrib->pktlen += 2;

	/*  SSID */
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pattrib->pktlen);

	/*  supported rates... */
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, min_t(unsigned int, rate_len, 8), cur_network->SupportedRates, &pattrib->pktlen);

	/*  DS parameter set */
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pattrib->pktlen);

	{
		u8 erpinfo = 0;
		u32 ATIMWindow;
		/*  IBSS Parameter Set... */
		ATIMWindow = 0;
		pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pattrib->pktlen);

		/* ERP IE */
		pframe = rtw_set_ie(pframe, _ERPINFO_IE_, 1, &erpinfo, &pattrib->pktlen);
	}

	/*  EXTERNDED SUPPORTED RATE */
	if (rate_len > 8)
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pattrib->pktlen);
	/* todo:HT for adhoc */
_issue_bcn:

#if defined(CONFIG_88EU_AP_MODE)
	pmlmepriv->update_bcn = false;

	spin_unlock_bh(&pmlmepriv->bcn_update_lock);
#endif /* if defined (CONFIG_88EU_AP_MODE) */

	if ((pattrib->pktlen + TXDESC_SIZE) > 512) {
		DBG_88E("beacon frame too large\n");
		return;
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	/* DBG_88E("issue bcn_sz=%d\n", pattrib->last_txcmdsz); */
	if (timeout_ms > 0)
		dump_mgntframe_and_wait(padapter, pmgntframe, timeout_ms);
	else
		dump_mgntframe(padapter, pmgntframe);
}

static void issue_probersp(struct adapter *padapter, unsigned char *da)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	unsigned char					*mac, *bssid;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
#if defined(CONFIG_88EU_AP_MODE)
	u8 *pwps_ie;
	uint wps_ielen;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
#endif /* if defined (CONFIG_88EU_AP_MODE) */
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*cur_network = &(pmlmeinfo->network);
	unsigned int	rate_len;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe) {
		DBG_88E("%s, alloc mgnt frame fail\n", __func__);
		return;
	}

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	mac = myid(&(padapter->eeprompriv));
	bssid = cur_network->MacAddress;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;
	ether_addr_copy(pwlanhdr->addr1, da);
	ether_addr_copy(pwlanhdr->addr2, mac);
	ether_addr_copy(pwlanhdr->addr3, bssid);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(fctrl, WIFI_PROBERSP);

	pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = pattrib->hdrlen;
	pframe += pattrib->hdrlen;

	if (cur_network->IELength > MAX_IE_SZ)
		return;

#if defined(CONFIG_88EU_AP_MODE)
	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE) {
		pwps_ie = rtw_get_wps_ie(cur_network->IEs+_FIXED_IE_LENGTH_, cur_network->IELength-_FIXED_IE_LENGTH_, NULL, &wps_ielen);

		/* inerset & update wps_probe_resp_ie */
		if ((pmlmepriv->wps_probe_resp_ie != NULL) && pwps_ie && (wps_ielen > 0)) {
			uint wps_offset, remainder_ielen;
			u8 *premainder_ie;

			wps_offset = (uint)(pwps_ie - cur_network->IEs);

			premainder_ie = pwps_ie + wps_ielen;

			remainder_ielen = cur_network->IELength - wps_offset - wps_ielen;

			memcpy(pframe, cur_network->IEs, wps_offset);
			pframe += wps_offset;
			pattrib->pktlen += wps_offset;

			wps_ielen = (uint)pmlmepriv->wps_probe_resp_ie[1];/* to get ie data len */
			if ((wps_offset+wps_ielen+2) <= MAX_IE_SZ) {
				memcpy(pframe, pmlmepriv->wps_probe_resp_ie, wps_ielen+2);
				pframe += wps_ielen+2;
				pattrib->pktlen += wps_ielen+2;
			}

			if ((wps_offset+wps_ielen+2+remainder_ielen) <= MAX_IE_SZ) {
				memcpy(pframe, premainder_ie, remainder_ielen);
				pframe += remainder_ielen;
				pattrib->pktlen += remainder_ielen;
			}
		} else {
			memcpy(pframe, cur_network->IEs, cur_network->IELength);
			pframe += cur_network->IELength;
			pattrib->pktlen += cur_network->IELength;
		}
	} else
#endif
	{
		/* timestamp will be inserted by hardware */
		pframe += 8;
		pattrib->pktlen += 8;

		/*  beacon interval: 2 bytes */

		memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2);

		pframe += 2;
		pattrib->pktlen += 2;

		/*  capability info: 2 bytes */

		memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

		pframe += 2;
		pattrib->pktlen += 2;

		/* below for ad-hoc mode */

		/*  SSID */
		pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pattrib->pktlen);

		/*  supported rates... */
		rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, min_t(unsigned int, rate_len, 8), cur_network->SupportedRates, &pattrib->pktlen);

		/*  DS parameter set */
		pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pattrib->pktlen);

		if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) {
			u8 erpinfo = 0;
			u32 ATIMWindow;
			/*  IBSS Parameter Set... */
			/* ATIMWindow = cur->Configuration.ATIMWindow; */
			ATIMWindow = 0;
			pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pattrib->pktlen);

			/* ERP IE */
			pframe = rtw_set_ie(pframe, _ERPINFO_IE_, 1, &erpinfo, &pattrib->pktlen);
		}


		/*  EXTERNDED SUPPORTED RATE */
		if (rate_len > 8)
			pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pattrib->pktlen);
		/* todo:HT for adhoc */
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);
}

static int issue_probereq(struct adapter *padapter,
			  struct ndis_802_11_ssid *pssid, u8 *da,
			  bool wait_ack)
{
	int ret = _FAIL;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	unsigned char			*pframe;
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	unsigned char			*mac;
	unsigned char			bssrate[NumRates];
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	int	bssrate_len = 0;
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+%s\n", __func__));

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);


	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	mac = myid(&(padapter->eeprompriv));

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;

	if (da) {
		/*	unicast probe request frame */
		ether_addr_copy(pwlanhdr->addr1, da);
		ether_addr_copy(pwlanhdr->addr3, da);
	} else {
		/*	broadcast probe request frame */
		ether_addr_copy(pwlanhdr->addr1, bc_addr);
		ether_addr_copy(pwlanhdr->addr3, bc_addr);
	}

	ether_addr_copy(pwlanhdr->addr2, mac);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_PROBEREQ);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	if (pssid)
		pframe = rtw_set_ie(pframe, _SSID_IE_, pssid->SsidLength, pssid->Ssid, &(pattrib->pktlen));
	else
		pframe = rtw_set_ie(pframe, _SSID_IE_, 0, NULL, &(pattrib->pktlen));

	get_rate_set(padapter, bssrate, &bssrate_len);

	if (bssrate_len > 8) {
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, bssrate, &(pattrib->pktlen));
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	} else {
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, bssrate_len, bssrate, &(pattrib->pktlen));
	}

	/* add wps_ie for wps2.0 */
	if (pmlmepriv->wps_probe_req_ie_len > 0 && pmlmepriv->wps_probe_req_ie) {
		memcpy(pframe, pmlmepriv->wps_probe_req_ie, pmlmepriv->wps_probe_req_ie_len);
		pframe += pmlmepriv->wps_probe_req_ie_len;
		pattrib->pktlen += pmlmepriv->wps_probe_req_ie_len;
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
		 ("issuing probe_req, tx_len=%d\n", pattrib->last_txcmdsz));

	if (wait_ack) {
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	} else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

static int issue_probereq_ex(struct adapter *padapter,
			     struct ndis_802_11_ssid *pssid, u8 *da,
			     int try_cnt, int wait_ms)
{
	int ret;
	int i = 0;
	unsigned long start = jiffies;

	do {
		ret = issue_probereq(padapter, pssid, da, wait_ms > 0);

		i++;

		if (padapter->bDriverStopped || padapter->bSurpriseRemoved)
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			msleep(wait_ms);

	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		goto exit;
	}

	if (try_cnt && wait_ms) {
		if (da)
			DBG_88E(FUNC_ADPT_FMT" to %pM, ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), da, rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt,
				jiffies_to_msecs(jiffies - start));
		else
			DBG_88E(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt,
				jiffies_to_msecs(jiffies - start));
	}
exit:
	return ret;
}

/*  if psta == NULL, indicate we are station(client) now... */
static void issue_auth(struct adapter *padapter, struct sta_info *psta,
		       unsigned short status)
{
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	unsigned int val32;
	u16 val16;
#ifdef CONFIG_88EU_AP_MODE
	__le16 le_val16;
#endif
	int use_shared_key = 0;
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex    *pnetwork = &(pmlmeinfo->network);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_AUTH);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);


	if (psta) {/*  for AP mode */
#ifdef CONFIG_88EU_AP_MODE

		ether_addr_copy(pwlanhdr->addr1, psta->hwaddr);
		ether_addr_copy(pwlanhdr->addr2,
				myid(&(padapter->eeprompriv)));
		ether_addr_copy(pwlanhdr->addr3,
				myid(&(padapter->eeprompriv)));


		/*  setting auth algo number */
		val16 = (u16)psta->authalg;

		if (status != _STATS_SUCCESSFUL_)
			val16 = 0;

		if (val16) {
			le_val16 = cpu_to_le16(val16);
			use_shared_key = 1;
		} else {
			le_val16 = 0;
		}

		pframe = rtw_set_fixed_ie(pframe, _AUTH_ALGM_NUM_, &le_val16,
					  &pattrib->pktlen);

		/*  setting auth seq number */
		val16 = (u16)psta->auth_seq;
		le_val16 = cpu_to_le16(val16);
		pframe = rtw_set_fixed_ie(pframe, _AUTH_SEQ_NUM_, &le_val16,
					  &pattrib->pktlen);

		/*  setting status code... */
		val16 = status;
		le_val16 = cpu_to_le16(val16);
		pframe = rtw_set_fixed_ie(pframe, _STATUS_CODE_,
					  &le_val16, &pattrib->pktlen);

		/*  added challenging text... */
		if ((psta->auth_seq == 2) && (psta->state & WIFI_FW_AUTH_STATE) && (use_shared_key == 1))
			pframe = rtw_set_ie(pframe, _CHLGETXT_IE_, 128, psta->chg_txt, &(pattrib->pktlen));
#endif
	} else {
		__le32 le_tmp32;
		__le16 le_tmp16;

		ether_addr_copy(pwlanhdr->addr1, pnetwork->MacAddress);
		ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
		ether_addr_copy(pwlanhdr->addr3, pnetwork->MacAddress);

		/*  setting auth algo number */
		val16 = (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared) ? 1 : 0;/*  0:OPEN System, 1:Shared key */
		if (val16)
			use_shared_key = 1;

		/* setting IV for auth seq #3 */
		if ((pmlmeinfo->auth_seq == 3) && (pmlmeinfo->state & WIFI_FW_AUTH_STATE) && (use_shared_key == 1)) {
			val32 = (pmlmeinfo->iv++) | (pmlmeinfo->key_index << 30);
			le_tmp32 = cpu_to_le32(val32);
			pframe = rtw_set_fixed_ie(pframe, 4, &le_tmp32,
						  &pattrib->pktlen);

			pattrib->iv_len = 4;
		}

		le_tmp16 = cpu_to_le16(val16);
		pframe = rtw_set_fixed_ie(pframe, _AUTH_ALGM_NUM_, &le_tmp16,
					  &pattrib->pktlen);

		/*  setting auth seq number */
		val16 = pmlmeinfo->auth_seq;
		le_tmp16 = cpu_to_le16(val16);
		pframe = rtw_set_fixed_ie(pframe, _AUTH_SEQ_NUM_, &le_tmp16,
					  &pattrib->pktlen);


		/*  setting status code... */
		le_tmp16 = cpu_to_le16(status);
		pframe = rtw_set_fixed_ie(pframe, _STATUS_CODE_, &le_tmp16,
					  &pattrib->pktlen);

		/*  then checking to see if sending challenging text... */
		if ((pmlmeinfo->auth_seq == 3) && (pmlmeinfo->state & WIFI_FW_AUTH_STATE) && (use_shared_key == 1)) {
			pframe = rtw_set_ie(pframe, _CHLGETXT_IE_, 128, pmlmeinfo->chg_txt, &(pattrib->pktlen));

			SetPrivacy(fctrl);

			pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);

			pattrib->encrypt = _WEP40_;

			pattrib->icv_len = 4;

			pattrib->pktlen += pattrib->icv_len;
		}
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	rtw_wep_encrypt(padapter, (u8 *)pmgntframe);
	DBG_88E("%s\n", __func__);
	dump_mgntframe(padapter, pmgntframe);
}


#ifdef CONFIG_88EU_AP_MODE
static void issue_asocrsp(struct adapter *padapter, unsigned short status,
			  struct sta_info *pstat, int pkt_type)
{
	struct xmit_frame	*pmgntframe;
	struct ieee80211_hdr *pwlanhdr;
	struct pkt_attrib *pattrib;
	unsigned char	*pbuf, *pframe;
	unsigned short val;
	__le16 *fctrl;
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *pnetwork = &(pmlmeinfo->network);
	u8 *ie = pnetwork->IEs;
	__le16 lestatus, leval;

	DBG_88E("%s\n", __func__);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);


	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;

	ether_addr_copy((void *)GetAddr1Ptr(pwlanhdr), pstat->hwaddr);
	ether_addr_copy((void *)GetAddr2Ptr(pwlanhdr),
			myid(&(padapter->eeprompriv)));
	ether_addr_copy((void *)GetAddr3Ptr(pwlanhdr), pnetwork->MacAddress);


	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	if ((pkt_type == WIFI_ASSOCRSP) || (pkt_type == WIFI_REASSOCRSP))
		SetFrameSubType(pwlanhdr, pkt_type);
	else
		return;

	pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen += pattrib->hdrlen;
	pframe += pattrib->hdrlen;

	/* capability */
	val = *(unsigned short *)rtw_get_capability_from_ie(ie);

	pframe = rtw_set_fixed_ie(pframe, _CAPABILITY_, &val, &pattrib->pktlen);

	lestatus = cpu_to_le16(status);
	pframe = rtw_set_fixed_ie(pframe, _STATUS_CODE_, &lestatus,
				  &pattrib->pktlen);

	leval = cpu_to_le16(pstat->aid | BIT(14) | BIT(15));
	pframe = rtw_set_fixed_ie(pframe, _ASOC_ID_, &leval, &pattrib->pktlen);

	if (pstat->bssratelen <= 8) {
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, pstat->bssratelen, pstat->bssrateset, &(pattrib->pktlen));
	} else {
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, pstat->bssrateset, &(pattrib->pktlen));
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (pstat->bssratelen-8), pstat->bssrateset+8, &(pattrib->pktlen));
	}

	if ((pstat->flags & WLAN_STA_HT) && (pmlmepriv->htpriv.ht_option)) {
		uint ie_len = 0;

		/* FILL HT CAP INFO IE */
		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_CAPABILITY_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if (pbuf && ie_len > 0) {
			memcpy(pframe, pbuf, ie_len+2);
			pframe += (ie_len+2);
			pattrib->pktlen += (ie_len+2);
		}

		/* FILL HT ADD INFO IE */
		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_ADD_INFO_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if (pbuf && ie_len > 0) {
			memcpy(pframe, pbuf, ie_len+2);
			pframe += (ie_len+2);
			pattrib->pktlen += (ie_len+2);
		}
	}

	/* FILL WMM IE */
	if ((pstat->flags & WLAN_STA_WME) && (pmlmepriv->qospriv.qos_option)) {
		uint ie_len = 0;
		unsigned char WMM_PARA_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};

		for (pbuf = ie + _BEACON_IE_OFFSET_;; pbuf += (ie_len + 2)) {
			pbuf = rtw_get_ie(pbuf, _VENDOR_SPECIFIC_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_ - (ie_len + 2)));
			if (pbuf && !memcmp(pbuf+2, WMM_PARA_IE, 6)) {
				memcpy(pframe, pbuf, ie_len+2);
				pframe += (ie_len+2);
				pattrib->pktlen += (ie_len+2);
				break;
			}

			if ((pbuf == NULL) || (ie_len == 0))
				break;
		}
	}

	if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK)
		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, 6, REALTEK_96B_IE, &(pattrib->pktlen));

	/* add WPS IE ie for wps 2.0 */
	if (pmlmepriv->wps_assoc_resp_ie && pmlmepriv->wps_assoc_resp_ie_len > 0) {
		memcpy(pframe, pmlmepriv->wps_assoc_resp_ie, pmlmepriv->wps_assoc_resp_ie_len);

		pframe += pmlmepriv->wps_assoc_resp_ie_len;
		pattrib->pktlen += pmlmepriv->wps_assoc_resp_ie_len;
	}

	pattrib->last_txcmdsz = pattrib->pktlen;
	dump_mgntframe(padapter, pmgntframe);
}
#endif /* CONFIG_88EU_AP_MODE */

static void issue_assocreq(struct adapter *padapter)
{
	int ret = _FAIL;
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	unsigned char		*pframe, *p;
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	unsigned int	i, j, ie_len, index = 0;
	unsigned char bssrate[NumRates], sta_bssrate[NumRates];
	struct ndis_802_11_var_ie *pIE;
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	int	bssrate_len = 0, sta_bssrate_len = 0;
	struct wlan_bssid_ex    *pnetwork = &(pmlmeinfo->network);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);
	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;
	ether_addr_copy(pwlanhdr->addr1, pnetwork->MacAddress);
	ether_addr_copy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)));
	ether_addr_copy(pwlanhdr->addr3, pnetwork->MacAddress);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ASSOCREQ);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	/* caps */

	memcpy(pframe, rtw_get_capability_from_ie(pmlmeinfo->network.IEs), 2);

	pframe += 2;
	pattrib->pktlen += 2;

	/* listen interval */
	/* todo: listen interval for power saving */
	put_unaligned_le16(3, pframe);
	pframe += 2;
	pattrib->pktlen += 2;

	/* SSID */
	pframe = rtw_set_ie(pframe, _SSID_IE_,  pmlmeinfo->network.Ssid.SsidLength, pmlmeinfo->network.Ssid.Ssid, &(pattrib->pktlen));

	/* supported rate & extended supported rate */

	/*  Check if the AP's supported rates are also supported by STA. */
	get_rate_set(padapter, sta_bssrate, &sta_bssrate_len);

	if (pmlmeext->cur_channel == 14)/*  for JAPAN, channel 14 can only uses B Mode(CCK) */
		sta_bssrate_len = 4;

	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		if (pmlmeinfo->network.SupportedRates[i] == 0)
			break;
		DBG_88E("network.SupportedRates[%d]=%02X\n", i, pmlmeinfo->network.SupportedRates[i]);
	}

	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		if (pmlmeinfo->network.SupportedRates[i] == 0)
			break;

		/*  Check if the AP's supported rates are also supported by STA. */
		for (j = 0; j < sta_bssrate_len; j++) {
			/*  Avoid the proprietary data rate (22Mbps) of Handlink WSG-4000 AP */
			if ((pmlmeinfo->network.SupportedRates[i]|IEEE80211_BASIC_RATE_MASK)
					== (sta_bssrate[j]|IEEE80211_BASIC_RATE_MASK))
				break;
		}

		if (j == sta_bssrate_len) {
			/*  the rate is not supported by STA */
			DBG_88E("%s(): the rate[%d]=%02X is not supported by STA!\n", __func__, i, pmlmeinfo->network.SupportedRates[i]);
		} else {
			/*  the rate is supported by STA */
			bssrate[index++] = pmlmeinfo->network.SupportedRates[i];
		}
	}

	bssrate_len = index;
	DBG_88E("bssrate_len=%d\n", bssrate_len);

	if (bssrate_len == 0) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit; /* don't connect to AP if no joint supported rate */
	}


	if (bssrate_len > 8) {
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, bssrate, &(pattrib->pktlen));
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	} else {
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, bssrate_len, bssrate, &(pattrib->pktlen));
	}

	/* RSN */
	p = rtw_get_ie((pmlmeinfo->network.IEs + sizeof(struct ndis_802_11_fixed_ie)), _RSN_IE_2_, &ie_len, (pmlmeinfo->network.IELength - sizeof(struct ndis_802_11_fixed_ie)));
	if (p)
		pframe = rtw_set_ie(pframe, _RSN_IE_2_, ie_len, (p + 2), &(pattrib->pktlen));

	/* HT caps */
	if (padapter->mlmepriv.htpriv.ht_option) {
		p = rtw_get_ie((pmlmeinfo->network.IEs + sizeof(struct ndis_802_11_fixed_ie)), _HT_CAPABILITY_IE_, &ie_len, (pmlmeinfo->network.IELength - sizeof(struct ndis_802_11_fixed_ie)));
		if ((p != NULL) && (!(is_ap_in_tkip(padapter)))) {
			memcpy(&pmlmeinfo->HT_caps, p + 2, sizeof(struct ieee80211_ht_cap));

			/* to disable 40M Hz support while gd_bw_40MHz_en = 0 */
			if (pregpriv->cbw40_enable == 0)
				pmlmeinfo->HT_caps.cap_info &= cpu_to_le16(~(BIT(6) | BIT(1)));
			else
				pmlmeinfo->HT_caps.cap_info |= cpu_to_le16(BIT(1));

			/* todo: disable SM power save mode */
			pmlmeinfo->HT_caps.cap_info |= cpu_to_le16(0x000c);

			if (pregpriv->rx_stbc)
				pmlmeinfo->HT_caps.cap_info |= cpu_to_le16(0x0100);/* RX STBC One spatial stream */
			memcpy((u8 *)&pmlmeinfo->HT_caps.mcs, MCS_rate_1R, 16);
			pframe = rtw_set_ie(pframe, _HT_CAPABILITY_IE_, ie_len, (u8 *)(&(pmlmeinfo->HT_caps)), &(pattrib->pktlen));
		}
	}

	/* vendor specific IE, such as WPA, WMM, WPS */
	for (i = sizeof(struct ndis_802_11_fixed_ie); i < pmlmeinfo->network.IELength; i += (pIE->Length + 2)) {
		pIE = (struct ndis_802_11_var_ie *)(pmlmeinfo->network.IEs + i);

		switch (pIE->ElementID) {
		case _VENDOR_SPECIFIC_IE_:
			if ((!memcmp(pIE->data, RTW_WPA_OUI, 4)) ||
			    (!memcmp(pIE->data, WMM_OUI, 4)) ||
			    (!memcmp(pIE->data, WPS_OUI, 4))) {
				if (!padapter->registrypriv.wifi_spec) {
					/* Commented by Kurt 20110629 */
					/* In some older APs, WPS handshake */
					/* would be fail if we append vender extensions informations to AP */
					if (!memcmp(pIE->data, WPS_OUI, 4))
						pIE->Length = 14;
				}
				pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, pIE->Length, pIE->data, &(pattrib->pktlen));
			}
			break;
		default:
			break;
		}
	}

	if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK)
		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, 6, REALTEK_96B_IE, &(pattrib->pktlen));

	pattrib->last_txcmdsz = pattrib->pktlen;
	dump_mgntframe(padapter, pmgntframe);

	ret = _SUCCESS;

exit:
	if (ret == _SUCCESS)
		rtw_buf_update(&pmlmepriv->assoc_req, &pmlmepriv->assoc_req_len, (u8 *)pwlanhdr, pattrib->pktlen);
	else
		rtw_buf_free(&pmlmepriv->assoc_req, &pmlmepriv->assoc_req_len);
}

/* when wait_ack is true, this function should be called at process context */
static int _issue_nulldata(struct adapter *padapter, unsigned char *da,
			   unsigned int power_mode, bool wait_ack)
{
	int ret = _FAIL;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	struct xmit_priv	*pxmitpriv;
	struct mlme_ext_priv	*pmlmeext;
	struct mlme_ext_info	*pmlmeinfo;
	struct wlan_bssid_ex    *pnetwork;

	if (!padapter)
		goto exit;

	pxmitpriv = &(padapter->xmitpriv);
	pmlmeext = &(padapter->mlmeextpriv);
	pmlmeinfo = &(pmlmeext->mlmext_info);
	pnetwork = &(pmlmeinfo->network);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->retry_ctrl = false;

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;

	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
		SetFrDs(fctrl);
	else if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
		SetToDs(fctrl);

	if (power_mode)
		SetPwrMgt(fctrl);

	ether_addr_copy(pwlanhdr->addr1, da);
	ether_addr_copy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)));
	ether_addr_copy(pwlanhdr->addr3, pnetwork->MacAddress);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_DATA_NULL);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pattrib->last_txcmdsz = pattrib->pktlen;

	if (wait_ack) {
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	} else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}


/* when wait_ms > 0 , this function should be called at process context */
/* da == NULL for station mode */
int issue_nulldata(struct adapter *padapter, unsigned char *da,
		   unsigned int power_mode, int try_cnt, int wait_ms)
{
	int ret;
	int i = 0;
	unsigned long start = jiffies;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex    *pnetwork = &(pmlmeinfo->network);

	/* da == NULL, assume it's null data for sta to ap*/
	if (da == NULL)
		da = pnetwork->MacAddress;

	do {
		ret = _issue_nulldata(padapter, da, power_mode, wait_ms > 0);

		i++;

		if (padapter->bDriverStopped || padapter->bSurpriseRemoved)
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			msleep(wait_ms);
	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		goto exit;
	}

	if (try_cnt && wait_ms) {
		if (da)
			DBG_88E(FUNC_ADPT_FMT" to %pM, ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), da, rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt,
				jiffies_to_msecs(jiffies - start));
		else
			DBG_88E(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt,
				jiffies_to_msecs(jiffies - start));
	}
exit:
	return ret;
}

/* when wait_ack is true, this function should be called at process context */
static int _issue_qos_nulldata(struct adapter *padapter, unsigned char *da,
			       u16 tid, bool wait_ack)
{
	int ret = _FAIL;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	unsigned short *qc;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex    *pnetwork = &(pmlmeinfo->network);

	DBG_88E("%s\n", __func__);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	pattrib->hdrlen += 2;
	pattrib->qos_en = true;
	pattrib->eosp = 1;
	pattrib->ack_policy = 0;
	pattrib->mdata = 0;

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;

	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
		SetFrDs(fctrl);
	else if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
		SetToDs(fctrl);

	if (pattrib->mdata)
		SetMData(fctrl);

	qc = (unsigned short *)(pframe + pattrib->hdrlen - 2);

	SetPriority(qc, tid);

	SetEOSP(qc, pattrib->eosp);

	SetAckpolicy(qc, pattrib->ack_policy);

	ether_addr_copy(pwlanhdr->addr1, da);
	ether_addr_copy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)));
	ether_addr_copy(pwlanhdr->addr3, pnetwork->MacAddress);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_QOS_DATA_NULL);

	pframe += sizeof(struct ieee80211_qos_hdr);
	pattrib->pktlen = sizeof(struct ieee80211_qos_hdr);

	pattrib->last_txcmdsz = pattrib->pktlen;

	if (wait_ack) {
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	} else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

/* when wait_ms > 0 , this function should be called at process context */
/* da == NULL for station mode */
int issue_qos_nulldata(struct adapter *padapter, unsigned char *da,
		       u16 tid, int try_cnt, int wait_ms)
{
	int ret;
	int i = 0;
	unsigned long start = jiffies;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex    *pnetwork = &(pmlmeinfo->network);

	/* da == NULL, assume it's null data for sta to ap*/
	if (da == NULL)
		da = pnetwork->MacAddress;

	do {
		ret = _issue_qos_nulldata(padapter, da, tid, wait_ms > 0);

		i++;

		if (padapter->bDriverStopped || padapter->bSurpriseRemoved)
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			msleep(wait_ms);
	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		goto exit;
	}

	if (try_cnt && wait_ms) {
		if (da)
			DBG_88E(FUNC_ADPT_FMT" to %pM, ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), da, rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt,
				jiffies_to_msecs(jiffies - start));
		else
			DBG_88E(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt,
				jiffies_to_msecs(jiffies - start));
	}
exit:
	return ret;
}

static int _issue_deauth(struct adapter *padapter, unsigned char *da,
			 unsigned short reason, bool wait_ack)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex    *pnetwork = &(pmlmeinfo->network);
	int ret = _FAIL;
	__le16 le_tmp;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->retry_ctrl = false;

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;

	ether_addr_copy(pwlanhdr->addr1, da);
	ether_addr_copy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)));
	ether_addr_copy(pwlanhdr->addr3, pnetwork->MacAddress);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_DEAUTH);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	le_tmp = cpu_to_le16(reason);
	pframe = rtw_set_fixed_ie(pframe, _RSON_CODE_, &le_tmp,
				  &pattrib->pktlen);

	pattrib->last_txcmdsz = pattrib->pktlen;


	if (wait_ack) {
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	} else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

int issue_deauth(struct adapter *padapter, unsigned char *da,
		 unsigned short reason)
{
	DBG_88E("%s to %pM\n", __func__, da);
	return _issue_deauth(padapter, da, reason, false);
}

static int issue_deauth_ex(struct adapter *padapter, u8 *da,
			   unsigned short reason, int try_cnt,
			   int wait_ms)
{
	int ret;
	int i = 0;
	unsigned long start = jiffies;

	do {
		ret = _issue_deauth(padapter, da, reason, wait_ms > 0);

		i++;

		if (padapter->bDriverStopped || padapter->bSurpriseRemoved)
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			msleep(wait_ms);
	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		goto exit;
	}

	if (try_cnt && wait_ms) {
		if (da)
			DBG_88E(FUNC_ADPT_FMT" to %pM, ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), da, rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt,
				jiffies_to_msecs(jiffies - start));
		else
			DBG_88E(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt,
				jiffies_to_msecs(jiffies - start));
	}
exit:
	return ret;
}

static void issue_action_BA(struct adapter *padapter, unsigned char *raddr,
			    unsigned char action, unsigned short status)
{
	u8 category = RTW_WLAN_CATEGORY_BACK;
	u16 start_seq;
	u16 BA_para_set;
	u16 reason_code;
	u16 BA_timeout_value;
	__le16	le_tmp;
	u16 BA_starting_seqctrl = 0;
	enum ht_cap_ampdu_factor max_rx_ampdu_factor;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	u8 *pframe;
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_info *psta;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct registry_priv *pregpriv = &padapter->registrypriv;
	struct wlan_bssid_ex    *pnetwork = &(pmlmeinfo->network);

	DBG_88E("%s, category=%d, action=%d, status=%d\n", __func__, category, action, status);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;

	ether_addr_copy(pwlanhdr->addr1, raddr);
	ether_addr_copy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)));
	ether_addr_copy(pwlanhdr->addr3, pnetwork->MacAddress);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));

	if (category == 3) {
		switch (action) {
		case 0: /* ADDBA req */
			do {
				pmlmeinfo->dialogToken++;
			} while (pmlmeinfo->dialogToken == 0);
			pframe = rtw_set_fixed_ie(pframe, 1, &(pmlmeinfo->dialogToken), &(pattrib->pktlen));

			BA_para_set = 0x1002 | ((status & 0xf) << 2); /* immediate ack & 64 buffer size */
			le_tmp = cpu_to_le16(BA_para_set);
			pframe = rtw_set_fixed_ie(pframe, 2, &(le_tmp),
						  &pattrib->pktlen);

			BA_timeout_value = 5000;/*  5ms */
			le_tmp = cpu_to_le16(BA_timeout_value);
			pframe = rtw_set_fixed_ie(pframe, 2, &(le_tmp),
						  &pattrib->pktlen);

			psta = rtw_get_stainfo(pstapriv, raddr);
			if (psta) {
				start_seq = (psta->sta_xmitpriv.txseq_tid[status & 0x07]&0xfff) + 1;

				DBG_88E("BA_starting_seqctrl=%d for TID=%d\n", start_seq, status & 0x07);

				psta->BA_starting_seqctrl[status & 0x07] = start_seq;

				BA_starting_seqctrl = start_seq << 4;
			}
			le_tmp = cpu_to_le16(BA_starting_seqctrl);
			pframe = rtw_set_fixed_ie(pframe, 2, &(le_tmp),
						  &pattrib->pktlen);
			break;
		case 1: /* ADDBA rsp */
		{
			struct ADDBA_request *ADDBA_req = &pmlmeinfo->ADDBA_req;

			pframe = rtw_set_fixed_ie(pframe, 1,
						  &ADDBA_req->dialog_token,
						  &pattrib->pktlen);
			pframe = rtw_set_fixed_ie(pframe, 2, &status,
						  &pattrib->pktlen);

			BA_para_set = le16_to_cpu(ADDBA_req->BA_para_set) &
				      0x3f;
			rtw_hal_get_def_var(padapter, HW_VAR_MAX_RX_AMPDU_FACTOR, &max_rx_ampdu_factor);
			switch (max_rx_ampdu_factor) {
			case MAX_AMPDU_FACTOR_64K:
				BA_para_set |= 0x1000; /* 64 buffer size */
				break;
			case MAX_AMPDU_FACTOR_32K:
				BA_para_set |= 0x0800; /* 32 buffer size */
				break;
			case MAX_AMPDU_FACTOR_16K:
				BA_para_set |= 0x0400; /* 16 buffer size */
				break;
			case MAX_AMPDU_FACTOR_8K:
				BA_para_set |= 0x0200; /* 8 buffer size */
				break;
			default:
				BA_para_set |= 0x1000; /* 64 buffer size */
				break;
			}

			if (pregpriv->ampdu_amsdu == 0)/* disabled */
				BA_para_set = BA_para_set & ~BIT(0);
			else if (pregpriv->ampdu_amsdu == 1)/* enabled */
				BA_para_set = BA_para_set | BIT(0);
			le_tmp = cpu_to_le16(BA_para_set);

			pframe = rtw_set_fixed_ie(pframe, 2, &(le_tmp),
						  &pattrib->pktlen);
			pframe = rtw_set_fixed_ie(pframe, 2,
						  &ADDBA_req->BA_timeout_value,
						  &pattrib->pktlen);
			break;
		}
		case 2:/* DELBA */
			BA_para_set = (status & 0x1F) << 3;
			le_tmp = cpu_to_le16(BA_para_set);
			pframe = rtw_set_fixed_ie(pframe, 2, &(le_tmp),
						  &pattrib->pktlen);

			reason_code = 37;/* Requested from peer STA as it does not want to use the mechanism */
			le_tmp = cpu_to_le16(reason_code);
			pframe = rtw_set_fixed_ie(pframe, 2, &(le_tmp),
						  &pattrib->pktlen);
			break;
		default:
			break;
		}
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);
}

static void issue_action_BSSCoexistPacket(struct adapter *padapter)
{
	struct list_head *plist, *phead;
	unsigned char category, action;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char				*pframe;
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	struct	wlan_network	*pnetwork = NULL;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct __queue *queue	= &(pmlmepriv->scanned_queue);
	u8 InfoContent[16] = {0};
	u8 ICS[8][15];
	struct wlan_bssid_ex  *cur_network   = &(pmlmeinfo->network);

	if ((pmlmepriv->num_FortyMHzIntolerant == 0) || (pmlmepriv->num_sta_no_ht == 0))
		return;

	if (pmlmeinfo->bwmode_updated)
		return;


	DBG_88E("%s\n", __func__);


	category = RTW_WLAN_CATEGORY_PUBLIC;
	action = ACT_PUBLIC_BSSCOEXIST;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;

	ether_addr_copy(pwlanhdr->addr1, cur_network->MacAddress);
	ether_addr_copy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)));
	ether_addr_copy(pwlanhdr->addr3, cur_network->MacAddress);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));


	/*  */
	if (pmlmepriv->num_FortyMHzIntolerant > 0) {
		u8 iedata = 0;

		iedata |= BIT(2);/* 20 MHz BSS Width Request */

		pframe = rtw_set_ie(pframe, EID_BSSCoexistence,  1, &iedata, &(pattrib->pktlen));
	}


	/*  */
	memset(ICS, 0, sizeof(ICS));
	if (pmlmepriv->num_sta_no_ht > 0) {
		int i;

		spin_lock_bh(&(pmlmepriv->scanned_queue.lock));

		phead = get_list_head(queue);
		plist = phead->next;

		while (phead != plist) {
			uint len;
			u8 *p;
			struct wlan_bssid_ex *pbss_network;

			pnetwork = container_of(plist, struct wlan_network, list);

			plist = plist->next;

			pbss_network = (struct wlan_bssid_ex *)&pnetwork->network;

			p = rtw_get_ie(pbss_network->IEs + _FIXED_IE_LENGTH_, _HT_CAPABILITY_IE_, &len, pbss_network->IELength - _FIXED_IE_LENGTH_);
			if ((p == NULL) || (len == 0)) { /* non-HT */
				if ((pbss_network->Configuration.DSConfig <= 0) || (pbss_network->Configuration.DSConfig > 14))
					continue;

				ICS[0][pbss_network->Configuration.DSConfig] = 1;

				if (ICS[0][0] == 0)
					ICS[0][0] = 1;
			}
		}
		spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

		for (i = 0; i < 8; i++) {
			if (ICS[i][0] == 1) {
				int j, k = 0;

				InfoContent[k] = i;
				/* SET_BSS_INTOLERANT_ELE_REG_CLASS(InfoContent, i); */
				k++;

				for (j = 1; j <= 14; j++) {
					if (ICS[i][j] == 1) {
						if (k < 16) {
							InfoContent[k] = j; /* channel number */
							/* SET_BSS_INTOLERANT_ELE_CHANNEL(InfoContent+k, j); */
							k++;
						}
					}
				}

				pframe = rtw_set_ie(pframe, EID_BSSIntolerantChlReport, k, InfoContent, &(pattrib->pktlen));
			}
		}
	}


	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);
}

unsigned int send_delba(struct adapter *padapter, u8 initiator, u8 *addr)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta = NULL;
	/* struct recv_reorder_ctrl *preorder_ctrl; */
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u16 tid;

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		if (!(pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS))
			return _SUCCESS;

	psta = rtw_get_stainfo(pstapriv, addr);
	if (psta == NULL)
		return _SUCCESS;

	if (initiator == 0) { /*  recipient */
		for (tid = 0; tid < MAXTID; tid++) {
			if (psta->recvreorder_ctrl[tid].enable) {
				DBG_88E("rx agg disable tid(%d)\n", tid);
				issue_action_BA(padapter, addr, RTW_WLAN_ACTION_DELBA, (((tid << 1) | initiator)&0x1F));
				psta->recvreorder_ctrl[tid].enable = false;
				psta->recvreorder_ctrl[tid].indicate_seq = 0xffff;
			}
		}
	} else if (initiator == 1) { /*  originator */
		for (tid = 0; tid < MAXTID; tid++) {
			if (psta->htpriv.agg_enable_bitmap & BIT(tid)) {
				DBG_88E("tx agg disable tid(%d)\n", tid);
				issue_action_BA(padapter, addr, RTW_WLAN_ACTION_DELBA, (((tid << 1) | initiator)&0x1F));
				psta->htpriv.agg_enable_bitmap &= ~BIT(tid);
				psta->htpriv.candidate_tid_bitmap &= ~BIT(tid);
			}
		}
	}

	return _SUCCESS;
}

unsigned int send_beacon(struct adapter *padapter)
{
	u8 bxmitok = false;
	int	issue = 0;
	int poll = 0;

	unsigned long start = jiffies;

	rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
	do {
		issue_beacon(padapter, 100);
		issue++;
		do {
			yield();
			rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, (u8 *)(&bxmitok));
			poll++;
		} while ((poll%10) != 0 && !bxmitok && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);
	} while (!bxmitok && issue < 100 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped)
		return _FAIL;
	if (!bxmitok) {
		DBG_88E("%s fail! %u ms\n", __func__,
			jiffies_to_msecs(jiffies - start));
		return _FAIL;
	} else {
		u32 passing_time = jiffies_to_msecs(jiffies - start);

		if (passing_time > 100 || issue > 3)
			DBG_88E("%s success, issue:%d, poll:%d, %u ms\n",
				__func__, issue, poll,
				jiffies_to_msecs(jiffies - start));
		return _SUCCESS;
	}
}

/****************************************************************************

Following are some utility functions for WiFi MLME

*****************************************************************************/

static void site_survey(struct adapter *padapter)
{
	unsigned char		survey_channel = 0, val8;
	enum rt_scan_type ScanType = SCAN_PASSIVE;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u32 initialgain = 0;
	struct rtw_ieee80211_channel *ch;

	if (pmlmeext->sitesurvey_res.channel_idx < pmlmeext->sitesurvey_res.ch_num) {
		ch = &pmlmeext->sitesurvey_res.ch[pmlmeext->sitesurvey_res.channel_idx];
		survey_channel = ch->hw_value;
		ScanType = (ch->flags & RTW_IEEE80211_CHAN_PASSIVE_SCAN) ? SCAN_PASSIVE : SCAN_ACTIVE;
	}


	if (survey_channel != 0) {
		/* PAUSE 4-AC Queue when site_survey */
		/* rtw_hal_get_hwreg(padapter, HW_VAR_TXPAUSE, (u8 *)(&val8)); */
		/* val8 |= 0x0f; */
		/* rtw_hal_set_hwreg(padapter, HW_VAR_TXPAUSE, (u8 *)(&val8)); */
		if (pmlmeext->sitesurvey_res.channel_idx == 0)
			set_channel_bwmode(padapter, survey_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
		else
			SelectChannel(padapter, survey_channel);

		if (ScanType == SCAN_ACTIVE) { /* obey the channel plan setting... */
			int i;

			for (i = 0; i < RTW_SSID_SCAN_AMOUNT; i++) {
				if (pmlmeext->sitesurvey_res.ssid[i].SsidLength) {
					/* todo: to issue two probe req??? */
					issue_probereq(padapter,
					&(pmlmeext->sitesurvey_res.ssid[i]),
								NULL, false);
					/* msleep(SURVEY_TO>>1); */
					issue_probereq(padapter,
					&(pmlmeext->sitesurvey_res.ssid[i]),
								NULL, false);
				}
			}

			if (pmlmeext->sitesurvey_res.scan_mode == SCAN_ACTIVE) {
				/* todo: to issue two probe req??? */
				issue_probereq(padapter, NULL, NULL, false);
				/* msleep(SURVEY_TO>>1); */
				issue_probereq(padapter, NULL, NULL, false);
			}

			if (pmlmeext->sitesurvey_res.scan_mode == SCAN_ACTIVE) {
				/* todo: to issue two probe req??? */
				issue_probereq(padapter, NULL, NULL, false);
				/* msleep(SURVEY_TO>>1); */
				issue_probereq(padapter, NULL, NULL, false);
			}
		}

		set_survey_timer(pmlmeext, pmlmeext->chan_scan_time);
	} else {

		/*  20100721:Interrupt scan operation here. */
		/*  For SW antenna diversity before link, it needs to switch to another antenna and scan again. */
		/*  It compares the scan result and select better one to do connection. */
		if (rtw_hal_antdiv_before_linked(padapter)) {
			pmlmeext->sitesurvey_res.bss_cnt = 0;
			pmlmeext->sitesurvey_res.channel_idx = -1;
			pmlmeext->chan_scan_time = SURVEY_TO / 2;
			set_survey_timer(pmlmeext, pmlmeext->chan_scan_time);
			return;
		}

		pmlmeext->sitesurvey_res.state = SCAN_COMPLETE;

		/* switch back to the original channel */

		set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

		/* flush 4-AC Queue after site_survey */
		/* val8 = 0; */
		/* rtw_hal_set_hwreg(padapter, HW_VAR_TXPAUSE, (u8 *)(&val8)); */

		/* config MSR */
		Set_MSR(padapter, (pmlmeinfo->state & 0x3));

		initialgain = 0xff; /* restore RX GAIN */
		rtw_hal_set_hwreg(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));
		/* turn on dynamic functions */
		Restore_DM_Func_Flag(padapter);
		/* Switch_DM_Func(padapter, DYNAMIC_ALL_FUNC_ENABLE, true); */

		if (is_client_associated_to_ap(padapter))
			issue_nulldata(padapter, NULL, 0, 3, 500);

		val8 = 0; /* survey done */
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

		report_surveydone_event(padapter);

		pmlmeext->chan_scan_time = SURVEY_TO;
		pmlmeext->sitesurvey_res.state = SCAN_DISABLE;

		issue_action_BSSCoexistPacket(padapter);
		issue_action_BSSCoexistPacket(padapter);
		issue_action_BSSCoexistPacket(padapter);
	}
}

/* collect bss info from Beacon and Probe request/response frames. */
static u8 collect_bss_info(struct adapter *padapter,
			   struct recv_frame *precv_frame,
			   struct wlan_bssid_ex *bssid)
{
	int	i;
	u32	len;
	u8 *p;
	u16 val16, subtype;
	u8 *pframe = precv_frame->pkt->data;
	u32 packet_len = precv_frame->pkt->len;
	u8 ie_offset;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	len = packet_len - sizeof(struct ieee80211_hdr_3addr);

	if (len > MAX_IE_SZ)
		return _FAIL;

	memset(bssid, 0, sizeof(struct wlan_bssid_ex));

	subtype = GetFrameSubType(pframe);

	if (subtype == WIFI_BEACON) {
		bssid->Reserved[0] = 1;
		ie_offset = _BEACON_IE_OFFSET_;
	} else {
		/*  FIXME : more type */
		if (subtype == WIFI_PROBEREQ) {
			ie_offset = _PROBEREQ_IE_OFFSET_;
			bssid->Reserved[0] = 2;
		} else if (subtype == WIFI_PROBERSP) {
			ie_offset = _PROBERSP_IE_OFFSET_;
			bssid->Reserved[0] = 3;
		} else {
			bssid->Reserved[0] = 0;
			ie_offset = _FIXED_IE_LENGTH_;
		}
	}

	bssid->Length = sizeof(struct wlan_bssid_ex) - MAX_IE_SZ + len;

	/* below is to copy the information element */
	bssid->IELength = len;
	memcpy(bssid->IEs, (pframe + sizeof(struct ieee80211_hdr_3addr)), bssid->IELength);

	/* get the signal strength in dBM.raw data */
	bssid->Rssi = precv_frame->attrib.phy_info.recvpower;
	bssid->PhyInfo.SignalQuality = precv_frame->attrib.phy_info.SignalQuality;/* in percentage */
	bssid->PhyInfo.SignalStrength = precv_frame->attrib.phy_info.SignalStrength;/* in percentage */
	rtw_hal_get_def_var(padapter, HAL_DEF_CURRENT_ANTENNA,  &bssid->PhyInfo.Optimum_antenna);

	/*  checking SSID */
	p = rtw_get_ie(bssid->IEs + ie_offset, _SSID_IE_, &len, bssid->IELength - ie_offset);
	if (!p) {
		DBG_88E("marc: cannot find SSID for survey event\n");
		return _FAIL;
	}

	if (len) {
		if (len > NDIS_802_11_LENGTH_SSID) {
			DBG_88E("%s()-%d: IE too long (%d) for survey event\n", __func__, __LINE__, len);
			return _FAIL;
		}
		memcpy(bssid->Ssid.Ssid, (p + 2), len);
		bssid->Ssid.SsidLength = len;
	} else {
		bssid->Ssid.SsidLength = 0;
	}

	memset(bssid->SupportedRates, 0, NDIS_802_11_LENGTH_RATES_EX);

	/* checking rate info... */
	i = 0;
	p = rtw_get_ie(bssid->IEs + ie_offset, _SUPPORTEDRATES_IE_, &len, bssid->IELength - ie_offset);
	if (p != NULL) {
		if (len > NDIS_802_11_LENGTH_RATES_EX) {
			DBG_88E("%s()-%d: IE too long (%d) for survey event\n", __func__, __LINE__, len);
			return _FAIL;
		}
		memcpy(bssid->SupportedRates, (p + 2), len);
		i = len;
	}

	p = rtw_get_ie(bssid->IEs + ie_offset, _EXT_SUPPORTEDRATES_IE_, &len, bssid->IELength - ie_offset);
	if (p) {
		if (len > (NDIS_802_11_LENGTH_RATES_EX-i)) {
			DBG_88E("%s()-%d: IE too long (%d) for survey event\n", __func__, __LINE__, len);
			return _FAIL;
		}
		memcpy(bssid->SupportedRates + i, (p + 2), len);
	}

	/* todo: */
	bssid->NetworkTypeInUse = Ndis802_11OFDM24;

	if (bssid->IELength < 12)
		return _FAIL;

	/*  Checking for DSConfig */
	p = rtw_get_ie(bssid->IEs + ie_offset, _DSSET_IE_, &len, bssid->IELength - ie_offset);

	bssid->Configuration.DSConfig = 0;
	bssid->Configuration.Length = 0;

	if (p) {
		bssid->Configuration.DSConfig = *(p + 2);
	} else {/*  In 5G, some ap do not have DSSET IE */
		/*  checking HT info for channel */
		p = rtw_get_ie(bssid->IEs + ie_offset, _HT_ADD_INFO_IE_, &len, bssid->IELength - ie_offset);
		if (p) {
			struct HT_info_element *HT_info = (struct HT_info_element *)(p + 2);

			bssid->Configuration.DSConfig = HT_info->primary_channel;
		} else { /*  use current channel */
			bssid->Configuration.DSConfig = rtw_get_oper_ch(padapter);
		}
	}

	if (subtype == WIFI_PROBEREQ) {
		/*  FIXME */
		bssid->InfrastructureMode = Ndis802_11Infrastructure;
		ether_addr_copy(bssid->MacAddress, GetAddr2Ptr(pframe));
		bssid->Privacy = 1;
		return _SUCCESS;
	}

	bssid->Configuration.BeaconPeriod =
		get_unaligned_le16(rtw_get_beacon_interval_from_ie(bssid->IEs));

	val16 = rtw_get_capability((struct wlan_bssid_ex *)bssid);

	if (val16 & BIT(0)) {
		bssid->InfrastructureMode = Ndis802_11Infrastructure;
		ether_addr_copy(bssid->MacAddress, GetAddr2Ptr(pframe));
	} else {
		bssid->InfrastructureMode = Ndis802_11IBSS;
		ether_addr_copy(bssid->MacAddress, GetAddr3Ptr(pframe));
	}

	if (val16 & BIT(4))
		bssid->Privacy = 1;
	else
		bssid->Privacy = 0;

	bssid->Configuration.ATIMWindow = 0;

	/* 20/40 BSS Coexistence check */
	if ((pregistrypriv->wifi_spec == 1) && (!pmlmeinfo->bwmode_updated)) {
		struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

		p = rtw_get_ie(bssid->IEs + ie_offset, _HT_CAPABILITY_IE_, &len, bssid->IELength - ie_offset);
		if (p && len > 0) {
			struct ieee80211_ht_cap *pHT_caps =
				(struct ieee80211_ht_cap *)(p + 2);

			if (le16_to_cpu(pHT_caps->cap_info) & BIT(14))
				pmlmepriv->num_FortyMHzIntolerant++;
		} else {
			pmlmepriv->num_sta_no_ht++;
		}
	}

	/*  mark bss info receiving from nearby channel as SignalQuality 101 */
	if (bssid->Configuration.DSConfig != rtw_get_oper_ch(padapter))
		bssid->PhyInfo.SignalQuality = 101;
	return _SUCCESS;
}

static void start_create_ibss(struct adapter *padapter)
{
	unsigned short	caps;
	u8 val8;
	u8 join_type;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *pnetwork = (struct wlan_bssid_ex *)(&(pmlmeinfo->network));

	pmlmeext->cur_channel = (u8)pnetwork->Configuration.DSConfig;
	pmlmeinfo->bcn_interval = get_beacon_interval(pnetwork);

	/* update wireless mode */
	update_wireless_mode(padapter);

	/* update capability */
	caps = rtw_get_capability((struct wlan_bssid_ex *)pnetwork);
	update_capinfo(padapter, caps);
	if (caps&cap_IBSS) {/* adhoc master */
		val8 = 0xcf;
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		/* switch channel */
		/* SelectChannel(padapter, pmlmeext->cur_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE); */
		set_channel_bwmode(padapter, pmlmeext->cur_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);

		beacon_timing_control(padapter);

		/* set msr to WIFI_FW_ADHOC_STATE */
		pmlmeinfo->state = WIFI_FW_ADHOC_STATE;
		Set_MSR(padapter, (pmlmeinfo->state & 0x3));

		/* issue beacon */
		if (send_beacon(padapter) == _FAIL) {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("issuing beacon frame fail....\n"));

			report_join_res(padapter, -1);
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
		} else {
			rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, padapter->registrypriv.dev_network.MacAddress);
			join_type = 0;
			rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

			report_join_res(padapter, 1);
			pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;
		}
	} else {
		DBG_88E("%s, invalid cap:%x\n", __func__, caps);
		return;
	}
}

static void start_clnt_join(struct adapter *padapter)
{
	unsigned short	caps;
	u8 val8;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *pnetwork = (struct wlan_bssid_ex *)(&(pmlmeinfo->network));
	int beacon_timeout;

	pmlmeext->cur_channel = (u8)pnetwork->Configuration.DSConfig;
	pmlmeinfo->bcn_interval = get_beacon_interval(pnetwork);

	/* update wireless mode */
	update_wireless_mode(padapter);

	/* update capability */
	caps = rtw_get_capability((struct wlan_bssid_ex *)pnetwork);
	update_capinfo(padapter, caps);
	if (caps&cap_ESS) {
		Set_MSR(padapter, WIFI_FW_STATION_STATE);

		val8 = (pmlmeinfo->auth_algo == dot11AuthAlgrthm_8021X) ? 0xcc : 0xcf;

		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		/* switch channel */
		set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

		/* here wait for receiving the beacon to start auth */
		/* and enable a timer */
		beacon_timeout = decide_wait_for_beacon_timeout(pmlmeinfo->bcn_interval);
		set_link_timer(pmlmeext, beacon_timeout);
		mod_timer(&padapter->mlmepriv.assoc_timer, jiffies +
			  msecs_to_jiffies((REAUTH_TO * REAUTH_LIMIT) + (REASSOC_TO * REASSOC_LIMIT) + beacon_timeout));

		pmlmeinfo->state = WIFI_FW_AUTH_NULL | WIFI_FW_STATION_STATE;
	} else if (caps&cap_IBSS) { /* adhoc client */
		Set_MSR(padapter, WIFI_FW_ADHOC_STATE);

		val8 = 0xcf;
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		/* switch channel */
		set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

		beacon_timing_control(padapter);

		pmlmeinfo->state = WIFI_FW_ADHOC_STATE;

		report_join_res(padapter, 1);
	} else {
		return;
	}
}

static void start_clnt_auth(struct adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	del_timer_sync(&pmlmeext->link_timer);

	pmlmeinfo->state &= (~WIFI_FW_AUTH_NULL);
	pmlmeinfo->state |= WIFI_FW_AUTH_STATE;

	pmlmeinfo->auth_seq = 1;
	pmlmeinfo->reauth_count = 0;
	pmlmeinfo->reassoc_count = 0;
	pmlmeinfo->link_count = 0;
	pmlmeext->retry = 0;


	/*  Because of AP's not receiving deauth before */
	/*  AP may: 1)not response auth or 2)deauth us after link is complete */
	/*  issue deauth before issuing auth to deal with the situation */
	/*	Commented by Albert 2012/07/21 */
	/*	For the Win8 P2P connection, it will be hard to have a successful connection if this Wi-Fi doesn't connect to it. */
	issue_deauth(padapter, (&(pmlmeinfo->network))->MacAddress, WLAN_REASON_DEAUTH_LEAVING);

	DBG_88E_LEVEL(_drv_info_, "start auth\n");
	issue_auth(padapter, NULL, 0);

	set_link_timer(pmlmeext, REAUTH_TO);
}


static void start_clnt_assoc(struct adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	del_timer_sync(&pmlmeext->link_timer);

	pmlmeinfo->state &= (~(WIFI_FW_AUTH_NULL | WIFI_FW_AUTH_STATE));
	pmlmeinfo->state |= (WIFI_FW_AUTH_SUCCESS | WIFI_FW_ASSOC_STATE);

	issue_assocreq(padapter);

	set_link_timer(pmlmeext, REASSOC_TO);
}

static unsigned int receive_disconnect(struct adapter *padapter,
				       unsigned char *MacAddr,
				       unsigned short reason)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex    *pnetwork = &(pmlmeinfo->network);

	/* check A3 */
	if (memcmp(MacAddr, pnetwork->MacAddress, ETH_ALEN))
		return _SUCCESS;

	DBG_88E("%s\n", __func__);

	if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE) {
		if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) {
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
			report_del_sta_event(padapter, MacAddr, reason);
		} else if (pmlmeinfo->state & WIFI_FW_LINKING_STATE) {
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
			report_join_res(padapter, -2);
		}
	}
	return _SUCCESS;
}

static void process_80211d(struct adapter *padapter, struct wlan_bssid_ex *bssid)
{
	struct registry_priv *pregistrypriv;
	struct mlme_ext_priv *pmlmeext;
	struct rt_channel_info *chplan_new;
	u8 channel;
	u8 i;

	pregistrypriv = &padapter->registrypriv;
	pmlmeext = &padapter->mlmeextpriv;

	/*  Adjust channel plan by AP Country IE */
	if (pregistrypriv->enable80211d &&
	    (!pmlmeext->update_channel_plan_by_ap_done)) {
		u8 *ie, *p;
		u32 len;
		struct rt_channel_plan chplan_ap;
		struct rt_channel_info chplan_sta[MAX_CHANNEL_NUM];
		u8 country[4];
		u8 fcn; /*  first channel number */
		u8 noc; /*  number of channel */
		u8 j, k;

		ie = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _COUNTRY_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
		if (!ie)
			return;
		if (len < 6)
			return;
		ie += 2;
		p = ie;
		ie += len;

		memset(country, 0, 4);
		memcpy(country, p, 3);
		p += 3;
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
			 ("%s: 802.11d country =%s\n", __func__, country));

		i = 0;
		while ((ie - p) >= 3) {
			fcn = *(p++);
			noc = *(p++);
			p++;

			for (j = 0; j < noc; j++) {
				if (fcn <= 14)
					channel = fcn + j; /*  2.4 GHz */
				else
					channel = fcn + j*4; /*  5 GHz */

				chplan_ap.Channel[i++] = channel;
			}
		}
		chplan_ap.Len = i;

		memcpy(chplan_sta, pmlmeext->channel_set, sizeof(chplan_sta));

		memset(pmlmeext->channel_set, 0, sizeof(pmlmeext->channel_set));
		chplan_new = pmlmeext->channel_set;

		i = 0;
		j = 0;
		k = 0;
		if (pregistrypriv->wireless_mode & WIRELESS_11G) {
			do {
				if ((i == MAX_CHANNEL_NUM) ||
				    (chplan_sta[i].ChannelNum == 0) ||
				    (chplan_sta[i].ChannelNum > 14))
					break;

				if ((j == chplan_ap.Len) || (chplan_ap.Channel[j] > 14))
					break;

				if (chplan_sta[i].ChannelNum == chplan_ap.Channel[j]) {
					chplan_new[k].ChannelNum = chplan_ap.Channel[j];
					chplan_new[k].ScanType = SCAN_ACTIVE;
					i++;
					j++;
					k++;
				} else if (chplan_sta[i].ChannelNum < chplan_ap.Channel[j]) {
					chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
					chplan_new[k].ScanType = SCAN_PASSIVE;
					i++;
					k++;
				} else if (chplan_sta[i].ChannelNum > chplan_ap.Channel[j]) {
					chplan_new[k].ChannelNum = chplan_ap.Channel[j];
					chplan_new[k].ScanType = SCAN_ACTIVE;
					j++;
					k++;
				}
			} while (1);

			/*  change AP not support channel to Passive scan */
			while ((i < MAX_CHANNEL_NUM) &&
			       (chplan_sta[i].ChannelNum != 0) &&
			       (chplan_sta[i].ChannelNum <= 14)) {
				chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
				chplan_new[k].ScanType = SCAN_PASSIVE;
				i++;
				k++;
			}

			/*  add channel AP supported */
			while ((j < chplan_ap.Len) && (chplan_ap.Channel[j] <= 14)) {
				chplan_new[k].ChannelNum = chplan_ap.Channel[j];
				chplan_new[k].ScanType = SCAN_ACTIVE;
				j++;
				k++;
			}
		} else {
			/*  keep original STA 2.4G channel plan */
			while ((i < MAX_CHANNEL_NUM) &&
			       (chplan_sta[i].ChannelNum != 0) &&
			       (chplan_sta[i].ChannelNum <= 14)) {
				chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
				chplan_new[k].ScanType = chplan_sta[i].ScanType;
				i++;
				k++;
			}

			/*  skip AP 2.4G channel plan */
			while ((j < chplan_ap.Len) && (chplan_ap.Channel[j] <= 14))
				j++;
		}

		/*  keep original STA 5G channel plan */
		while ((i < MAX_CHANNEL_NUM) && (chplan_sta[i].ChannelNum != 0)) {
			chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
			chplan_new[k].ScanType = chplan_sta[i].ScanType;
			i++;
			k++;
		}

		pmlmeext->update_channel_plan_by_ap_done = 1;
	}

	/*  If channel is used by AP, set channel scan type to active */
	channel = bssid->Configuration.DSConfig;
	chplan_new = pmlmeext->channel_set;
	i = 0;
	while ((i < MAX_CHANNEL_NUM) && (chplan_new[i].ChannelNum != 0)) {
		if (chplan_new[i].ChannelNum == channel) {
			if (chplan_new[i].ScanType == SCAN_PASSIVE) {
				chplan_new[i].ScanType = SCAN_ACTIVE;
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
					 ("%s: change channel %d scan type from passive to active\n",
					 __func__, channel));
			}
			break;
		}
		i++;
	}
}

/****************************************************************************

Following are the callback functions for each subtype of the management frames

*****************************************************************************/

static unsigned int OnProbeReq(struct adapter *padapter,
			       struct recv_frame *precv_frame)
{
	unsigned int	ielen;
	unsigned char	*p;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *cur = &(pmlmeinfo->network);
	u8 *pframe = precv_frame->pkt->data;
	uint len = precv_frame->pkt->len;

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
		return _SUCCESS;

	if (!check_fwstate(pmlmepriv, _FW_LINKED) &&
	    !check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE|WIFI_AP_STATE))
		return _SUCCESS;

	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_, _SSID_IE_, &ielen,
			len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_);

	/* check (wildcard) SSID */
	if (p) {
		if ((ielen != 0 && memcmp((void *)(p+2), (void *)cur->Ssid.Ssid, cur->Ssid.SsidLength)) ||
		    (ielen == 0 && pmlmeinfo->hidden_ssid_mode))
			return _SUCCESS;

		if (check_fwstate(pmlmepriv, _FW_LINKED) &&
		    pmlmepriv->cur_network.join_res)
			issue_probersp(padapter, get_sa(pframe));
	}
	return _SUCCESS;
}

static unsigned int OnProbeRsp(struct adapter *padapter,
			       struct recv_frame *precv_frame)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS) {
		report_survey_event(padapter, precv_frame);
		return _SUCCESS;
	}

	return _SUCCESS;
}

static unsigned int OnBeacon(struct adapter *padapter,
			     struct recv_frame *precv_frame)
{
	int cam_idx;
	struct sta_info	*psta;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_priv	*pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->pkt->data;
	uint len = precv_frame->pkt->len;
	struct wlan_bssid_ex *pbss;
	int ret = _SUCCESS;
	struct wlan_bssid_ex *pnetwork = &(pmlmeinfo->network);

	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS) {
		report_survey_event(padapter, precv_frame);
		return _SUCCESS;
	}

	if (!memcmp(GetAddr3Ptr(pframe), pnetwork->MacAddress, ETH_ALEN)) {
		if (pmlmeinfo->state & WIFI_FW_AUTH_NULL) {
			/* we should update current network before auth, or some IE is wrong */
			pbss = (struct wlan_bssid_ex *)rtw_malloc(sizeof(struct wlan_bssid_ex));
			if (pbss) {
				if (collect_bss_info(padapter, precv_frame, pbss) == _SUCCESS) {
					update_network(&(pmlmepriv->cur_network.network), pbss, padapter, true);
					rtw_get_bcn_info(&(pmlmepriv->cur_network));
				}
				kfree(pbss);
			}

			/* check the vendor of the assoc AP */
			pmlmeinfo->assoc_AP_vendor = check_assoc_AP(pframe+sizeof(struct ieee80211_hdr_3addr), len-sizeof(struct ieee80211_hdr_3addr));

			/* update TSF Value */
			update_TSF(pmlmeext, pframe, len);

			/* start auth */
			start_clnt_auth(padapter);

			return _SUCCESS;
		}

		if (((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE) && (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)) {
			psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
			if (psta != NULL) {
				ret = rtw_check_bcn_info(padapter, pframe, len);
				if (!ret) {
						DBG_88E_LEVEL(_drv_info_, "ap has changed, disconnect now\n ");
						receive_disconnect(padapter, pmlmeinfo->network.MacAddress, 65535);
						return _SUCCESS;
				}
				/* update WMM, ERP in the beacon */
				/* todo: the timer is used instead of the number of the beacon received */
				if ((sta_rx_pkts(psta) & 0xf) == 0)
					update_beacon_info(padapter, pframe, len, psta);
			}
		} else if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) {
			psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
			if (psta != NULL) {
				/* update WMM, ERP in the beacon */
				/* todo: the timer is used instead of the number of the beacon received */
				if ((sta_rx_pkts(psta) & 0xf) == 0)
					update_beacon_info(padapter, pframe, len, psta);
			} else {
				/* allocate a new CAM entry for IBSS station */
				cam_idx = allocate_fw_sta_entry(padapter);
				if (cam_idx == NUM_STA)
					goto _END_ONBEACON_;

				/* get supported rate */
				if (update_sta_support_rate(padapter, (pframe + WLAN_HDR_A3_LEN + _BEACON_IE_OFFSET_), (len - WLAN_HDR_A3_LEN - _BEACON_IE_OFFSET_), cam_idx) == _FAIL) {
					pmlmeinfo->FW_sta_info[cam_idx].status = 0;
					goto _END_ONBEACON_;
				}

				/* update TSF Value */
				update_TSF(pmlmeext, pframe, len);

				/* report sta add event */
				report_add_sta_event(padapter, GetAddr2Ptr(pframe), cam_idx);
			}
		}
	}

_END_ONBEACON_:

	return _SUCCESS;
}

#ifdef CONFIG_88EU_AP_MODE
static unsigned int OnAuth(struct adapter *padapter,
			   struct recv_frame *precv_frame)
{
	unsigned int	auth_mode, ie_len;
	u16 seq;
	unsigned char	*sa, *p;
	u16 algorithm;
	int	status;
	static struct sta_info stat;
	struct	sta_info	*pstat = NULL;
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->pkt->data;
	uint len = precv_frame->pkt->len;

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return _FAIL;

	DBG_88E("+%s\n", __func__);

	sa = GetAddr2Ptr(pframe);

	auth_mode = psecuritypriv->dot11AuthAlgrthm;
	seq = le16_to_cpu(*(__le16 *)((size_t)pframe + WLAN_HDR_A3_LEN + 2));
	algorithm = le16_to_cpu(*(__le16 *)((size_t)pframe + WLAN_HDR_A3_LEN));

	DBG_88E("auth alg=%x, seq=%X\n", algorithm, seq);

	if (auth_mode == 2 && psecuritypriv->dot11PrivacyAlgrthm != _WEP40_ &&
	    psecuritypriv->dot11PrivacyAlgrthm != _WEP104_)
		auth_mode = 0;

	if ((algorithm > 0 && auth_mode == 0) ||	/*  rx a shared-key auth but shared not enabled */
	    (algorithm == 0 && auth_mode == 1)) {	/*  rx a open-system auth but shared-key is enabled */
		DBG_88E("auth rejected due to bad alg [alg=%d, auth_mib=%d] %02X%02X%02X%02X%02X%02X\n",
			algorithm, auth_mode, sa[0], sa[1], sa[2], sa[3], sa[4], sa[5]);

		status = _STATS_NO_SUPP_ALG_;

		goto auth_fail;
	}

	if (!rtw_access_ctrl(padapter, sa)) {
		status = _STATS_UNABLE_HANDLE_STA_;
		goto auth_fail;
	}

	pstat = rtw_get_stainfo(pstapriv, sa);
	if (!pstat) {
		/*  allocate a new one */
		DBG_88E("going to alloc stainfo for sa=%pM\n", sa);
		pstat = rtw_alloc_stainfo(pstapriv, sa);
		if (!pstat) {
			DBG_88E(" Exceed the upper limit of supported clients...\n");
			status = _STATS_UNABLE_HANDLE_STA_;
			goto auth_fail;
		}

		pstat->state = WIFI_FW_AUTH_NULL;
		pstat->auth_seq = 0;
	} else {
		spin_lock_bh(&pstapriv->asoc_list_lock);
		if (!list_empty(&pstat->asoc_list)) {
			list_del_init(&pstat->asoc_list);
			pstapriv->asoc_list_cnt--;
		}
		spin_unlock_bh(&pstapriv->asoc_list_lock);

		if (seq == 1) {
			/* TODO: STA re_auth and auth timeout */
		}
	}

	spin_lock_bh(&pstapriv->auth_list_lock);
	if (list_empty(&pstat->auth_list)) {
		list_add_tail(&pstat->auth_list, &pstapriv->auth_list);
		pstapriv->auth_list_cnt++;
	}
	spin_unlock_bh(&pstapriv->auth_list_lock);

	if (pstat->auth_seq == 0)
		pstat->expire_to = pstapriv->auth_to;

	if ((pstat->auth_seq + 1) != seq) {
		DBG_88E("(1)auth rejected because out of seq [rx_seq=%d, exp_seq=%d]!\n",
			seq, pstat->auth_seq+1);
		status = _STATS_OUT_OF_AUTH_SEQ_;
		goto auth_fail;
	}

	if (algorithm == 0 && (auth_mode == 0 || auth_mode == 2)) {
		if (seq == 1) {
			pstat->state &= ~WIFI_FW_AUTH_NULL;
			pstat->state |= WIFI_FW_AUTH_SUCCESS;
			pstat->expire_to = pstapriv->assoc_to;
			pstat->authalg = algorithm;
		} else {
			DBG_88E("(2)auth rejected because out of seq [rx_seq=%d, exp_seq=%d]!\n",
				seq, pstat->auth_seq+1);
			status = _STATS_OUT_OF_AUTH_SEQ_;
			goto auth_fail;
		}
	} else { /*  shared system or auto authentication */
		if (seq == 1) {
			/* prepare for the challenging txt... */

			pstat->state &= ~WIFI_FW_AUTH_NULL;
			pstat->state |= WIFI_FW_AUTH_STATE;
			pstat->authalg = algorithm;
			pstat->auth_seq = 2;
		} else if (seq == 3) {
			/* checking for challenging txt... */
			DBG_88E("checking for challenging txt...\n");

			p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + 4 + _AUTH_IE_OFFSET_, _CHLGETXT_IE_, &ie_len,
					len - WLAN_HDR_A3_LEN - _AUTH_IE_OFFSET_ - 4);

			if ((p == NULL) || (ie_len <= 0)) {
				DBG_88E("auth rejected because challenge failure!(1)\n");
				status = _STATS_CHALLENGE_FAIL_;
				goto auth_fail;
			}

			if (!memcmp((void *)(p + 2), pstat->chg_txt, 128)) {
				pstat->state &= (~WIFI_FW_AUTH_STATE);
				pstat->state |= WIFI_FW_AUTH_SUCCESS;
				/*  challenging txt is correct... */
				pstat->expire_to =  pstapriv->assoc_to;
			} else {
				DBG_88E("auth rejected because challenge failure!\n");
				status = _STATS_CHALLENGE_FAIL_;
				goto auth_fail;
			}
		} else {
			DBG_88E("(3)auth rejected because out of seq [rx_seq=%d, exp_seq=%d]!\n",
				seq, pstat->auth_seq+1);
			status = _STATS_OUT_OF_AUTH_SEQ_;
			goto auth_fail;
		}
	}

	/*  Now, we are going to issue_auth... */
	pstat->auth_seq = seq + 1;

	issue_auth(padapter, pstat, (unsigned short)(_STATS_SUCCESSFUL_));

	if (pstat->state & WIFI_FW_AUTH_SUCCESS)
		pstat->auth_seq = 0;

	return _SUCCESS;

auth_fail:

	if (pstat)
		rtw_free_stainfo(padapter, pstat);

	pstat = &stat;
	memset((char *)pstat, '\0', sizeof(stat));
	pstat->auth_seq = 2;
	memcpy(pstat->hwaddr, sa, 6);

	issue_auth(padapter, pstat, (unsigned short)status);

	return _FAIL;
}
#endif /* CONFIG_88EU_AP_MODE */

static unsigned int OnAuthClient(struct adapter *padapter,
				 struct recv_frame *precv_frame)
{
	unsigned int	seq, len, status, offset;
	unsigned char	*p;
	unsigned int	go2asoc = 0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->pkt->data;
	uint pkt_len = precv_frame->pkt->len;

	DBG_88E("%s\n", __func__);

	/* check A1 matches or not */
	if (memcmp(myid(&(padapter->eeprompriv)), get_da(pframe), ETH_ALEN))
		return _SUCCESS;

	if (!(pmlmeinfo->state & WIFI_FW_AUTH_STATE))
		return _SUCCESS;

	offset = (GetPrivacy(pframe)) ? 4 : 0;

	seq	= le16_to_cpu(*(__le16 *)((size_t)pframe + WLAN_HDR_A3_LEN + offset + 2));
	status	= le16_to_cpu(*(__le16 *)((size_t)pframe + WLAN_HDR_A3_LEN + offset + 4));

	if (status != 0) {
		DBG_88E("clnt auth fail, status: %d\n", status);
		if (status == 13) { /*  pmlmeinfo->auth_algo == dot11AuthAlgrthm_Auto) */
			if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared)
				pmlmeinfo->auth_algo = dot11AuthAlgrthm_Open;
			else
				pmlmeinfo->auth_algo = dot11AuthAlgrthm_Shared;
		}

		set_link_timer(pmlmeext, 1);
		goto authclnt_fail;
	}

	if (seq == 2) {
		if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared) {
			/*  legendary shared system */
			p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _AUTH_IE_OFFSET_, _CHLGETXT_IE_, &len,
				pkt_len - WLAN_HDR_A3_LEN - _AUTH_IE_OFFSET_);

			if (p == NULL)
				goto authclnt_fail;

			memcpy((void *)(pmlmeinfo->chg_txt), (void *)(p + 2), len);
			pmlmeinfo->auth_seq = 3;
			issue_auth(padapter, NULL, 0);
			set_link_timer(pmlmeext, REAUTH_TO);

			return _SUCCESS;
		} else {
			/*  open system */
			go2asoc = 1;
		}
	} else if (seq == 4) {
		if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared)
			go2asoc = 1;
		else
			goto authclnt_fail;
	} else {
		/*  this is also illegal */
		goto authclnt_fail;
	}

	if (go2asoc) {
		DBG_88E_LEVEL(_drv_info_, "auth success, start assoc\n");
		start_clnt_assoc(padapter);
		return _SUCCESS;
	}
authclnt_fail:
	return _FAIL;
}

static unsigned int OnAssocReq(struct adapter *padapter,
			       struct recv_frame *precv_frame)
{
#ifdef CONFIG_88EU_AP_MODE
	u16 capab_info;
	struct rtw_ieee802_11_elems elems;
	struct sta_info	*pstat;
	unsigned char		reassoc, *p, *pos, *wpa_ie;
	unsigned char WMM_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01};
	int		i, wpa_ie_len, left;
	unsigned char		supportRate[16];
	int					supportRateNum;
	unsigned short		status = _STATS_SUCCESSFUL_;
	unsigned short		frame_type, ie_offset = 0;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *cur = &(pmlmeinfo->network);
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->pkt->data;
	uint ie_len, pkt_len = precv_frame->pkt->len;

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return _FAIL;

	frame_type = GetFrameSubType(pframe);
	if (frame_type == WIFI_ASSOCREQ) {
		reassoc = 0;
		ie_offset = _ASOCREQ_IE_OFFSET_;
	} else { /*  WIFI_REASSOCREQ */
		reassoc = 1;
		ie_offset = _REASOCREQ_IE_OFFSET_;
	}


	if (pkt_len < IEEE80211_3ADDR_LEN + ie_offset) {
		DBG_88E("handle_assoc(reassoc=%d) - too short payload (len=%lu)"
		       "\n", reassoc, (unsigned long)pkt_len);
		return _FAIL;
	}

	pstat = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
	if (!pstat) {
		status = _RSON_CLS2_;
		goto asoc_class2_error;
	}

	capab_info = get_unaligned_le16(pframe + WLAN_HDR_A3_LEN);

	left = pkt_len - (IEEE80211_3ADDR_LEN + ie_offset);
	pos = pframe + (IEEE80211_3ADDR_LEN + ie_offset);


	DBG_88E("%s\n", __func__);

	/*  check if this stat has been successfully authenticated/assocated */
	if (!((pstat->state) & WIFI_FW_AUTH_SUCCESS)) {
		if (!((pstat->state) & WIFI_FW_ASSOC_SUCCESS)) {
			status = _RSON_CLS2_;
			goto asoc_class2_error;
		} else {
			pstat->state &= (~WIFI_FW_ASSOC_SUCCESS);
			pstat->state |= WIFI_FW_ASSOC_STATE;
		}
	} else {
		pstat->state &= (~WIFI_FW_AUTH_SUCCESS);
		pstat->state |= WIFI_FW_ASSOC_STATE;
	}
	pstat->capability = capab_info;
	/* now parse all ieee802_11 ie to point to elems */
	if (rtw_ieee802_11_parse_elems(pos, left, &elems, 1) == ParseFailed ||
	    !elems.ssid) {
		DBG_88E("STA %pM sent invalid association request\n",
			pstat->hwaddr);
		status = _STATS_FAILURE_;
		goto OnAssocReqFail;
	}


	/*  now we should check all the fields... */
	/*  checking SSID */
	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + ie_offset, _SSID_IE_, &ie_len,
		pkt_len - WLAN_HDR_A3_LEN - ie_offset);
	if (!p)
		status = _STATS_FAILURE_;

	if (ie_len == 0) { /*  broadcast ssid, however it is not allowed in assocreq */
		status = _STATS_FAILURE_;
	} else {
		/*  check if ssid match */
		if (memcmp((void *)(p+2), cur->Ssid.Ssid, cur->Ssid.SsidLength))
			status = _STATS_FAILURE_;

		if (ie_len != cur->Ssid.SsidLength)
			status = _STATS_FAILURE_;
	}

	if (_STATS_SUCCESSFUL_ != status)
		goto OnAssocReqFail;

	/*  check if the supported rate is ok */
	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + ie_offset, _SUPPORTEDRATES_IE_, &ie_len, pkt_len - WLAN_HDR_A3_LEN - ie_offset);
	if (p == NULL) {
		DBG_88E("Rx a sta assoc-req which supported rate is empty!\n");
		/*  use our own rate set as statoin used */
		/* memcpy(supportRate, AP_BSSRATE, AP_BSSRATE_LEN); */
		/* supportRateNum = AP_BSSRATE_LEN; */

		status = _STATS_FAILURE_;
		goto OnAssocReqFail;
	} else {
		memcpy(supportRate, p+2, ie_len);
		supportRateNum = ie_len;

		p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + ie_offset, _EXT_SUPPORTEDRATES_IE_, &ie_len,
				pkt_len - WLAN_HDR_A3_LEN - ie_offset);
		if (p !=  NULL) {
			if (supportRateNum <= sizeof(supportRate)) {
				memcpy(supportRate+supportRateNum, p+2, ie_len);
				supportRateNum += ie_len;
			}
		}
	}

	/* todo: mask supportRate between AP & STA -> move to update raid */
	/* get_matched_rate(pmlmeext, supportRate, &supportRateNum, 0); */

	/* update station supportRate */
	pstat->bssratelen = supportRateNum;
	memcpy(pstat->bssrateset, supportRate, supportRateNum);
	UpdateBrateTblForSoftAP(pstat->bssrateset, pstat->bssratelen);

	/* check RSN/WPA/WPS */
	pstat->dot8021xalg = 0;
	pstat->wpa_psk = 0;
	pstat->wpa_group_cipher = 0;
	pstat->wpa2_group_cipher = 0;
	pstat->wpa_pairwise_cipher = 0;
	pstat->wpa2_pairwise_cipher = 0;
	memset(pstat->wpa_ie, 0, sizeof(pstat->wpa_ie));
	if ((psecuritypriv->wpa_psk & BIT(1)) && elems.rsn_ie) {
		int group_cipher = 0, pairwise_cipher = 0;

		wpa_ie = elems.rsn_ie;
		wpa_ie_len = elems.rsn_ie_len;

		if (rtw_parse_wpa2_ie(wpa_ie-2, wpa_ie_len+2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			pstat->dot8021xalg = 1;/* psk,  todo:802.1x */
			pstat->wpa_psk |= BIT(1);

			pstat->wpa2_group_cipher = group_cipher&psecuritypriv->wpa2_group_cipher;
			pstat->wpa2_pairwise_cipher = pairwise_cipher&psecuritypriv->wpa2_pairwise_cipher;

			if (!pstat->wpa2_group_cipher)
				status = WLAN_STATUS_INVALID_GROUP_CIPHER;

			if (!pstat->wpa2_pairwise_cipher)
				status = WLAN_STATUS_INVALID_PAIRWISE_CIPHER;
		} else {
			status = WLAN_STATUS_INVALID_IE;
		}
	} else if ((psecuritypriv->wpa_psk & BIT(0)) && elems.wpa_ie) {
		int group_cipher = 0, pairwise_cipher = 0;

		wpa_ie = elems.wpa_ie;
		wpa_ie_len = elems.wpa_ie_len;

		if (rtw_parse_wpa_ie(wpa_ie-2, wpa_ie_len+2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			pstat->dot8021xalg = 1;/* psk,  todo:802.1x */
			pstat->wpa_psk |= BIT(0);

			pstat->wpa_group_cipher = group_cipher&psecuritypriv->wpa_group_cipher;
			pstat->wpa_pairwise_cipher = pairwise_cipher&psecuritypriv->wpa_pairwise_cipher;

			if (!pstat->wpa_group_cipher)
				status = WLAN_STATUS_INVALID_GROUP_CIPHER;

			if (!pstat->wpa_pairwise_cipher)
				status = WLAN_STATUS_INVALID_PAIRWISE_CIPHER;
		} else {
			status = WLAN_STATUS_INVALID_IE;
		}
	} else {
		wpa_ie = NULL;
		wpa_ie_len = 0;
	}

	if (_STATS_SUCCESSFUL_ != status)
		goto OnAssocReqFail;

	pstat->flags &= ~(WLAN_STA_WPS | WLAN_STA_MAYBE_WPS);
	if (!wpa_ie) {
		if (elems.wps_ie) {
			DBG_88E("STA included WPS IE in "
				   "(Re)Association Request - assume WPS is "
				   "used\n");
			pstat->flags |= WLAN_STA_WPS;
			/* wpabuf_free(sta->wps_ie); */
			/* sta->wps_ie = wpabuf_alloc_copy(elems.wps_ie + 4, */
			/*				elems.wps_ie_len - 4); */
		} else {
			DBG_88E("STA did not include WPA/RSN IE "
				   "in (Re)Association Request - possible WPS "
				   "use\n");
			pstat->flags |= WLAN_STA_MAYBE_WPS;
		}


		/*  AP support WPA/RSN, and sta is going to do WPS, but AP is not ready */
		/*  that the selected registrar of AP is _FLASE */
		if ((psecuritypriv->wpa_psk > 0) && (pstat->flags & (WLAN_STA_WPS|WLAN_STA_MAYBE_WPS))) {
			if (pmlmepriv->wps_beacon_ie) {
				u8 selected_registrar = 0;

				rtw_get_wps_attr_content(pmlmepriv->wps_beacon_ie, pmlmepriv->wps_beacon_ie_len, WPS_ATTR_SELECTED_REGISTRAR, &selected_registrar, NULL);

				if (!selected_registrar) {
					DBG_88E("selected_registrar is false , or AP is not ready to do WPS\n");

					status = _STATS_UNABLE_HANDLE_STA_;

					goto OnAssocReqFail;
				}
			}
		}
	} else {
		int copy_len;

		if (psecuritypriv->wpa_psk == 0) {
			DBG_88E("STA %pM: WPA/RSN IE in association "
			"request, but AP don't support WPA/RSN\n", pstat->hwaddr);

			status = WLAN_STATUS_INVALID_IE;

			goto OnAssocReqFail;
		}

		if (elems.wps_ie) {
			DBG_88E("STA included WPS IE in "
				   "(Re)Association Request - WPS is "
				   "used\n");
			pstat->flags |= WLAN_STA_WPS;
			copy_len = 0;
		} else {
			copy_len = min_t(int, wpa_ie_len + 2, sizeof(pstat->wpa_ie));
		}
		if (copy_len > 0)
			memcpy(pstat->wpa_ie, wpa_ie-2, copy_len);
	}
	/*  check if there is WMM IE & support WWM-PS */
	pstat->flags &= ~WLAN_STA_WME;
	pstat->qos_option = 0;
	pstat->qos_info = 0;
	pstat->has_legacy_ac = true;
	pstat->uapsd_vo = 0;
	pstat->uapsd_vi = 0;
	pstat->uapsd_be = 0;
	pstat->uapsd_bk = 0;
	if (pmlmepriv->qospriv.qos_option) {
		p = pframe + WLAN_HDR_A3_LEN + ie_offset; ie_len = 0;
		for (;;) {
			p = rtw_get_ie(p, _VENDOR_SPECIFIC_IE_, &ie_len, pkt_len - WLAN_HDR_A3_LEN - ie_offset);
			if (p != NULL) {
				if (!memcmp(p+2, WMM_IE, 6)) {
					pstat->flags |= WLAN_STA_WME;

					pstat->qos_option = 1;
					pstat->qos_info = *(p+8);

					pstat->max_sp_len = (pstat->qos_info>>5)&0x3;

					if ((pstat->qos_info&0xf) != 0xf)
						pstat->has_legacy_ac = true;
					else
						pstat->has_legacy_ac = false;

					if (pstat->qos_info&0xf) {
						if (pstat->qos_info&BIT(0))
							pstat->uapsd_vo = BIT(0)|BIT(1);
						else
							pstat->uapsd_vo = 0;

						if (pstat->qos_info&BIT(1))
							pstat->uapsd_vi = BIT(0)|BIT(1);
						else
							pstat->uapsd_vi = 0;

						if (pstat->qos_info&BIT(2))
							pstat->uapsd_bk = BIT(0)|BIT(1);
						else
							pstat->uapsd_bk = 0;

						if (pstat->qos_info&BIT(3))
							pstat->uapsd_be = BIT(0)|BIT(1);
						else
							pstat->uapsd_be = 0;
					}
					break;
				}
			} else {
				break;
			}
			p = p + ie_len + 2;
		}
	}

	/* save HT capabilities in the sta object */
	memset(&pstat->htpriv.ht_cap, 0, sizeof(struct ieee80211_ht_cap));
	if (elems.ht_capabilities &&
	    elems.ht_capabilities_len >= sizeof(struct ieee80211_ht_cap)) {
		pstat->flags |= WLAN_STA_HT;

		pstat->flags |= WLAN_STA_WME;

		memcpy(&pstat->htpriv.ht_cap,
		       elems.ht_capabilities, sizeof(struct ieee80211_ht_cap));
	} else {
		pstat->flags &= ~WLAN_STA_HT;
	}
	if ((!pmlmepriv->htpriv.ht_option) && (pstat->flags&WLAN_STA_HT)) {
		status = _STATS_FAILURE_;
		goto OnAssocReqFail;
	}

	if ((pstat->flags & WLAN_STA_HT) &&
	    ((pstat->wpa2_pairwise_cipher&WPA_CIPHER_TKIP) ||
	    (pstat->wpa_pairwise_cipher&WPA_CIPHER_TKIP))) {
		DBG_88E("HT: %pM tried to "
			"use TKIP with HT association\n", pstat->hwaddr);

		/* status = WLAN_STATUS_CIPHER_REJECTED_PER_POLICY; */
		/* goto OnAssocReqFail; */
	}

	pstat->flags |= WLAN_STA_NONERP;
	for (i = 0; i < pstat->bssratelen; i++) {
		if ((pstat->bssrateset[i] & 0x7f) > 22) {
			pstat->flags &= ~WLAN_STA_NONERP;
			break;
		}
	}

	if (pstat->capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
		pstat->flags |= WLAN_STA_SHORT_PREAMBLE;
	else
		pstat->flags &= ~WLAN_STA_SHORT_PREAMBLE;



	if (status != _STATS_SUCCESSFUL_)
		goto OnAssocReqFail;

	/* TODO: identify_proprietary_vendor_ie(); */
	/*  Realtek proprietary IE */
	/*  identify if this is Broadcom sta */
	/*  identify if this is ralink sta */
	/*  Customer proprietary IE */

	/* get a unique AID */
	if (pstat->aid > 0) {
		DBG_88E("  old AID %d\n", pstat->aid);
	} else {
		for (pstat->aid = 1; pstat->aid <= NUM_STA; pstat->aid++)
			if (pstapriv->sta_aid[pstat->aid - 1] == NULL)
				break;

		/* if (pstat->aid > NUM_STA) { */
		if (pstat->aid > pstapriv->max_num_sta) {
			pstat->aid = 0;

			DBG_88E("  no room for more AIDs\n");

			status = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;

			goto OnAssocReqFail;
		} else {
			pstapriv->sta_aid[pstat->aid - 1] = pstat;
			DBG_88E("allocate new AID=(%d)\n", pstat->aid);
		}
	}

	pstat->state &= (~WIFI_FW_ASSOC_STATE);
	pstat->state |= WIFI_FW_ASSOC_SUCCESS;

	spin_lock_bh(&pstapriv->auth_list_lock);
	if (!list_empty(&pstat->auth_list)) {
		list_del_init(&pstat->auth_list);
		pstapriv->auth_list_cnt--;
	}
	spin_unlock_bh(&pstapriv->auth_list_lock);

	spin_lock_bh(&pstapriv->asoc_list_lock);
	if (list_empty(&pstat->asoc_list)) {
		pstat->expire_to = pstapriv->expire_to;
		list_add_tail(&pstat->asoc_list, &pstapriv->asoc_list);
		pstapriv->asoc_list_cnt++;
	}
	spin_unlock_bh(&pstapriv->asoc_list_lock);

	/*  now the station is qualified to join our BSS... */
	if ((pstat->state & WIFI_FW_ASSOC_SUCCESS) && (_STATS_SUCCESSFUL_ == status)) {
		/* 1 bss_cap_update & sta_info_update */
		bss_cap_update_on_sta_join(padapter, pstat);
		sta_info_update(padapter, pstat);

		/* issue assoc rsp before notify station join event. */
		if (frame_type == WIFI_ASSOCREQ)
			issue_asocrsp(padapter, status, pstat, WIFI_ASSOCRSP);
		else
			issue_asocrsp(padapter, status, pstat, WIFI_REASSOCRSP);

		/* 2 - report to upper layer */
		DBG_88E("indicate_sta_join_event to upper layer - hostapd\n");
		rtw_indicate_sta_assoc_event(padapter, pstat);

		/* 3-(1) report sta add event */
		report_add_sta_event(padapter, pstat->hwaddr, pstat->aid);
	}

	return _SUCCESS;

asoc_class2_error:

	issue_deauth(padapter, (void *)GetAddr2Ptr(pframe), status);

	return _FAIL;

OnAssocReqFail:

	pstat->aid = 0;
	if (frame_type == WIFI_ASSOCREQ)
		issue_asocrsp(padapter, status, pstat, WIFI_ASSOCRSP);
	else
		issue_asocrsp(padapter, status, pstat, WIFI_REASSOCRSP);

#endif /* CONFIG_88EU_AP_MODE */

	return _FAIL;
}

static unsigned int OnAssocRsp(struct adapter *padapter,
			       struct recv_frame *precv_frame)
{
	uint i;
	int res;
	unsigned short	status;
	struct ndis_802_11_var_ie *pIE;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	/* struct wlan_bssid_ex *cur_network = &(pmlmeinfo->network); */
	u8 *pframe = precv_frame->pkt->data;
	uint pkt_len = precv_frame->pkt->len;

	DBG_88E("%s\n", __func__);

	/* check A1 matches or not */
	if (memcmp(myid(&(padapter->eeprompriv)), get_da(pframe), ETH_ALEN))
		return _SUCCESS;

	if (!(pmlmeinfo->state & (WIFI_FW_AUTH_SUCCESS | WIFI_FW_ASSOC_STATE)))
		return _SUCCESS;

	if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
		return _SUCCESS;

	del_timer_sync(&pmlmeext->link_timer);

	/* status */
	status = le16_to_cpu(*(__le16 *)(pframe + WLAN_HDR_A3_LEN + 2));
	if (status > 0) {
		DBG_88E("assoc reject, status code: %d\n", status);
		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		res = -4;
		goto report_assoc_result;
	}

	/* get capabilities */
	pmlmeinfo->capability = le16_to_cpu(*(__le16 *)(pframe + WLAN_HDR_A3_LEN));

	/* set slot time */
	pmlmeinfo->slotTime = (pmlmeinfo->capability & BIT(10)) ? 9 : 20;

	/* AID */
	pmlmeinfo->aid = (int)(le16_to_cpu(*(__le16 *)(pframe + WLAN_HDR_A3_LEN + 4))&0x3fff);
	res = pmlmeinfo->aid;

	/* following are moved to join event callback function */
	/* to handle HT, WMM, rate adaptive, update MAC reg */
	/* for not to handle the synchronous IO in the tasklet */
	for (i = 6 + WLAN_HDR_A3_LEN; i < pkt_len;) {
		pIE = (struct ndis_802_11_var_ie *)(pframe + i);

		switch (pIE->ElementID) {
		case _VENDOR_SPECIFIC_IE_:
			if (!memcmp(pIE->data, WMM_PARA_OUI, 6)) /* WMM */
				WMM_param_handler(padapter, pIE);
			break;
		case _HT_CAPABILITY_IE_:	/* HT caps */
			HT_caps_handler(padapter, pIE);
			break;
		case _HT_EXTRA_INFO_IE_:	/* HT info */
			HT_info_handler(padapter, pIE);
			break;
		case _ERPINFO_IE_:
			ERP_IE_handler(padapter, pIE);
		default:
			break;
		}

		i += (pIE->Length + 2);
	}

	pmlmeinfo->state &= (~WIFI_FW_ASSOC_STATE);
	pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;

	UpdateBrateTbl(padapter, pmlmeinfo->network.SupportedRates);

report_assoc_result:
	if (res > 0)
		rtw_buf_update(&pmlmepriv->assoc_rsp, &pmlmepriv->assoc_rsp_len, pframe, pkt_len);
	else
		rtw_buf_free(&pmlmepriv->assoc_rsp, &pmlmepriv->assoc_rsp_len);

	report_join_res(padapter, res);

	return _SUCCESS;
}

static unsigned int OnDeAuth(struct adapter *padapter,
			     struct recv_frame *precv_frame)
{
	unsigned short	reason;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->pkt->data;
	struct wlan_bssid_ex *pnetwork = &(pmlmeinfo->network);

	/* check A3 */
	if (memcmp(GetAddr3Ptr(pframe), pnetwork->MacAddress, ETH_ALEN))
		return _SUCCESS;

	reason = le16_to_cpu(*(__le16 *)(pframe + WLAN_HDR_A3_LEN));

	DBG_88E("%s Reason code(%d)\n", __func__, reason);

#ifdef CONFIG_88EU_AP_MODE
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		DBG_88E_LEVEL(_drv_always_, "ap recv deauth reason code(%d) sta:%pM\n",
			      reason, GetAddr2Ptr(pframe));

		psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
		if (psta) {
			u8 updated = 0;

			spin_lock_bh(&pstapriv->asoc_list_lock);
			if (!list_empty(&psta->asoc_list)) {
				list_del_init(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;
				updated = ap_free_sta(padapter, psta, false, reason);
			}
			spin_unlock_bh(&pstapriv->asoc_list_lock);

			associated_clients_update(padapter, updated);
		}


		return _SUCCESS;
	} else
#endif
	{
		DBG_88E_LEVEL(_drv_always_, "sta recv deauth reason code(%d) sta:%pM\n",
			      reason, GetAddr3Ptr(pframe));

		receive_disconnect(padapter, GetAddr3Ptr(pframe), reason);
	}
	pmlmepriv->LinkDetectInfo.bBusyTraffic = false;
	return _SUCCESS;
}

static unsigned int OnDisassoc(struct adapter *padapter,
			       struct recv_frame *precv_frame)
{
	u16 reason;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->pkt->data;
	struct wlan_bssid_ex *pnetwork = &(pmlmeinfo->network);

	/* check A3 */
	if (memcmp(GetAddr3Ptr(pframe), pnetwork->MacAddress, ETH_ALEN))
		return _SUCCESS;

	reason = le16_to_cpu(*(__le16 *)(pframe + WLAN_HDR_A3_LEN));

	DBG_88E("%s Reason code(%d)\n", __func__, reason);

#ifdef CONFIG_88EU_AP_MODE
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		DBG_88E_LEVEL(_drv_always_, "ap recv disassoc reason code(%d) sta:%pM\n",
			      reason, GetAddr2Ptr(pframe));

		psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
		if (psta) {
			u8 updated = 0;

			spin_lock_bh(&pstapriv->asoc_list_lock);
			if (!list_empty(&psta->asoc_list)) {
				list_del_init(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;
				updated = ap_free_sta(padapter, psta, false, reason);
			}
			spin_unlock_bh(&pstapriv->asoc_list_lock);

			associated_clients_update(padapter, updated);
		}

		return _SUCCESS;
	} else
#endif
	{
		DBG_88E_LEVEL(_drv_always_, "ap recv disassoc reason code(%d) sta:%pM\n",
			      reason, GetAddr3Ptr(pframe));

		receive_disconnect(padapter, GetAddr3Ptr(pframe), reason);
	}
	pmlmepriv->LinkDetectInfo.bBusyTraffic = false;
	return _SUCCESS;
}

static unsigned int OnAtim(struct adapter *padapter,
			   struct recv_frame *precv_frame)
{
	DBG_88E("%s\n", __func__);
	return _SUCCESS;
}

static unsigned int on_action_spct(struct adapter *padapter,
				   struct recv_frame *precv_frame)
{
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->pkt->data;
	u8 *frame_body = pframe + sizeof(struct ieee80211_hdr_3addr);
	u8 category;
	u8 action;

	DBG_88E(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(padapter->pnetdev));

	psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));

	if (!psta)
		goto exit;

	category = frame_body[0];
	if (category != RTW_WLAN_CATEGORY_SPECTRUM_MGMT)
		goto exit;

	action = frame_body[1];
	switch (action) {
	case RTW_WLAN_ACTION_SPCT_MSR_REQ:
	case RTW_WLAN_ACTION_SPCT_MSR_RPRT:
	case RTW_WLAN_ACTION_SPCT_TPC_REQ:
	case RTW_WLAN_ACTION_SPCT_TPC_RPRT:
		break;
	case RTW_WLAN_ACTION_SPCT_CHL_SWITCH:
		break;
	default:
		break;
	}

exit:
	return _FAIL;
}

static unsigned int OnAction_qos(struct adapter *padapter,
				 struct recv_frame *precv_frame)
{
	return _SUCCESS;
}

static unsigned int OnAction_dls(struct adapter *padapter,
				 struct recv_frame *precv_frame)
{
	return _SUCCESS;
}

static unsigned int OnAction_back(struct adapter *padapter,
				  struct recv_frame *precv_frame)
{
	u8 *addr;
	struct sta_info *psta = NULL;
	struct recv_reorder_ctrl *preorder_ctrl;
	unsigned char		*frame_body;
	unsigned char		category, action;
	unsigned short	tid, status, reason_code = 0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->pkt->data;
	struct sta_priv *pstapriv = &padapter->stapriv;
	/* check RA matches or not */
	if (memcmp(myid(&(padapter->eeprompriv)), GetAddr1Ptr(pframe),
		   ETH_ALEN))/* for if1, sta/ap mode */
		return _SUCCESS;

	DBG_88E("%s\n", __func__);

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		if (!(pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS))
			return _SUCCESS;

	addr = GetAddr2Ptr(pframe);
	psta = rtw_get_stainfo(pstapriv, addr);

	if (!psta)
		return _SUCCESS;

	frame_body = (unsigned char *)(pframe + sizeof(struct ieee80211_hdr_3addr));

	category = frame_body[0];
	if (category == RTW_WLAN_CATEGORY_BACK) { /*  representing Block Ack */
		if (!pmlmeinfo->HT_enable)
			return _SUCCESS;
		action = frame_body[1];
		DBG_88E("%s, action=%d\n", __func__, action);
		switch (action) {
		case RTW_WLAN_ACTION_ADDBA_REQ: /* ADDBA request */
			memcpy(&(pmlmeinfo->ADDBA_req), &(frame_body[2]), sizeof(struct ADDBA_request));
			process_addba_req(padapter, (u8 *)&(pmlmeinfo->ADDBA_req), addr);

			/* 37 = reject ADDBA Req */
			issue_action_BA(padapter, addr,
					RTW_WLAN_ACTION_ADDBA_RESP,
					pmlmeinfo->accept_addba_req ? 0 : 37);
			break;
		case RTW_WLAN_ACTION_ADDBA_RESP: /* ADDBA response */
			status = get_unaligned_le16(&frame_body[3]);
			tid = (frame_body[5] >> 2) & 0x7;
			if (status == 0) {	/* successful */
				DBG_88E("agg_enable for TID=%d\n", tid);
				psta->htpriv.agg_enable_bitmap |= 1 << tid;
				psta->htpriv.candidate_tid_bitmap &= ~BIT(tid);
			} else {
				psta->htpriv.agg_enable_bitmap &= ~BIT(tid);
			}
			break;
		case RTW_WLAN_ACTION_DELBA: /* DELBA */
			if ((frame_body[3] & BIT(3)) == 0) {
				psta->htpriv.agg_enable_bitmap &= ~(1 << ((frame_body[3] >> 4) & 0xf));
				psta->htpriv.candidate_tid_bitmap &= ~(1 << ((frame_body[3] >> 4) & 0xf));
				reason_code = get_unaligned_le16(&frame_body[4]);
			} else if ((frame_body[3] & BIT(3)) == BIT(3)) {
				tid = (frame_body[3] >> 4) & 0x0F;
				preorder_ctrl =  &psta->recvreorder_ctrl[tid];
				preorder_ctrl->enable = false;
				preorder_ctrl->indicate_seq = 0xffff;
			}
			DBG_88E("%s(): DELBA: %x(%x)\n", __func__, pmlmeinfo->agg_enable_bitmap, reason_code);
			/* todo: how to notify the host while receiving DELETE BA */
			break;
		default:
			break;
		}
	}
	return _SUCCESS;
}

static s32 rtw_action_public_decache(struct recv_frame *recv_frame, s32 token)
{
	struct adapter *adapter = recv_frame->adapter;
	struct mlme_ext_priv *mlmeext = &(adapter->mlmeextpriv);
	u8 *frame = recv_frame->pkt->data;
	u16 seq_ctrl = ((recv_frame->attrib.seq_num&0xffff) << 4) |
		(recv_frame->attrib.frag_num & 0xf);

	if (GetRetry(frame)) {
		if (token >= 0) {
			if ((seq_ctrl == mlmeext->action_public_rxseq) && (token == mlmeext->action_public_dialog_token)) {
				DBG_88E(FUNC_ADPT_FMT" seq_ctrl = 0x%x, rxseq = 0x%x, token:%d\n",
					FUNC_ADPT_ARG(adapter), seq_ctrl, mlmeext->action_public_rxseq, token);
				return _FAIL;
			}
		} else {
			if (seq_ctrl == mlmeext->action_public_rxseq) {
				DBG_88E(FUNC_ADPT_FMT" seq_ctrl = 0x%x, rxseq = 0x%x\n",
					FUNC_ADPT_ARG(adapter), seq_ctrl, mlmeext->action_public_rxseq);
				return _FAIL;
			}
		}
	}

	mlmeext->action_public_rxseq = seq_ctrl;

	if (token >= 0)
		mlmeext->action_public_dialog_token = token;

	return _SUCCESS;
}

static unsigned int on_action_public_p2p(struct recv_frame *precv_frame)
{
	u8 *pframe = precv_frame->pkt->data;
	u8 *frame_body;
	u8 dialogToken = 0;

	frame_body = (unsigned char *)(pframe + sizeof(struct ieee80211_hdr_3addr));
	dialogToken = frame_body[7];

	if (rtw_action_public_decache(precv_frame, dialogToken) == _FAIL)
		return _FAIL;

	return _SUCCESS;
}

static unsigned int on_action_public_vendor(struct recv_frame *precv_frame)
{
	unsigned int ret = _FAIL;
	u8 *pframe = precv_frame->pkt->data;
	u8 *frame_body = pframe + sizeof(struct ieee80211_hdr_3addr);

	if (!memcmp(frame_body + 2, P2P_OUI, 4))
		ret = on_action_public_p2p(precv_frame);

	return ret;
}

static unsigned int on_action_public_default(struct recv_frame *precv_frame,
					     u8 action)
{
	unsigned int ret = _FAIL;
	u8 *pframe = precv_frame->pkt->data;
	u8 *frame_body = pframe + sizeof(struct ieee80211_hdr_3addr);
	u8 token;

	token = frame_body[2];

	if (rtw_action_public_decache(precv_frame, token) == _FAIL)
		goto exit;

	ret = _SUCCESS;

exit:
	return ret;
}

static unsigned int on_action_public(struct adapter *padapter,
				     struct recv_frame *precv_frame)
{
	unsigned int ret = _FAIL;
	u8 *pframe = precv_frame->pkt->data;
	u8 *frame_body = pframe + sizeof(struct ieee80211_hdr_3addr);
	u8 category, action;

	/* check RA matches or not */
	if (memcmp(myid(&(padapter->eeprompriv)), GetAddr1Ptr(pframe), ETH_ALEN))
		goto exit;

	category = frame_body[0];
	if (category != RTW_WLAN_CATEGORY_PUBLIC)
		goto exit;

	action = frame_body[1];
	switch (action) {
	case ACT_PUBLIC_VENDOR:
		ret = on_action_public_vendor(precv_frame);
		break;
	default:
		ret = on_action_public_default(precv_frame, action);
		break;
	}

exit:
	return ret;
}

static unsigned int OnAction_ht(struct adapter *padapter,
				struct recv_frame *precv_frame)
{
	return _SUCCESS;
}

static unsigned int OnAction_wmm(struct adapter *padapter,
				 struct recv_frame *precv_frame)
{
	return _SUCCESS;
}

static unsigned int OnAction_p2p(struct adapter *padapter,
				 struct recv_frame *precv_frame)
{
	return _SUCCESS;
}

static unsigned int DoReserved(struct adapter *padapter,
			       struct recv_frame *precv_frame)
{
	return _SUCCESS;
}

static struct action_handler OnAction_tbl[] = {
	{RTW_WLAN_CATEGORY_SPECTRUM_MGMT,	 "ACTION_SPECTRUM_MGMT", on_action_spct},
	{RTW_WLAN_CATEGORY_QOS, "ACTION_QOS", &OnAction_qos},
	{RTW_WLAN_CATEGORY_DLS, "ACTION_DLS", &OnAction_dls},
	{RTW_WLAN_CATEGORY_BACK, "ACTION_BACK", &OnAction_back},
	{RTW_WLAN_CATEGORY_PUBLIC, "ACTION_PUBLIC", on_action_public},
	{RTW_WLAN_CATEGORY_RADIO_MEASUREMENT, "ACTION_RADIO_MEASUREMENT", &DoReserved},
	{RTW_WLAN_CATEGORY_FT, "ACTION_FT",	&DoReserved},
	{RTW_WLAN_CATEGORY_HT,	"ACTION_HT",	&OnAction_ht},
	{RTW_WLAN_CATEGORY_SA_QUERY, "ACTION_SA_QUERY", &DoReserved},
	{RTW_WLAN_CATEGORY_WMM, "ACTION_WMM", &OnAction_wmm},
	{RTW_WLAN_CATEGORY_P2P, "ACTION_P2P", &OnAction_p2p},
};

static unsigned int OnAction(struct adapter *padapter,
			     struct recv_frame *precv_frame)
{
	int i;
	unsigned char	category;
	struct action_handler *ptable;
	unsigned char	*frame_body;
	u8 *pframe = precv_frame->pkt->data;

	frame_body = (unsigned char *)(pframe + sizeof(struct ieee80211_hdr_3addr));

	category = frame_body[0];

	for (i = 0; i < ARRAY_SIZE(OnAction_tbl); i++) {
		ptable = &OnAction_tbl[i];
		if (category == ptable->num)
			ptable->func(padapter, precv_frame);
	}
	return _SUCCESS;
}

/****************************************************************************

Following are the initialization functions for WiFi MLME

*****************************************************************************/

static struct mlme_handler mlme_sta_tbl[] = {
	{WIFI_ASSOCREQ,		"OnAssocReq",	&OnAssocReq},
	{WIFI_ASSOCRSP,		"OnAssocRsp",	&OnAssocRsp},
	{WIFI_REASSOCREQ,	"OnReAssocReq",	&OnAssocReq},
	{WIFI_REASSOCRSP,	"OnReAssocRsp",	&OnAssocRsp},
	{WIFI_PROBEREQ,		"OnProbeReq",	&OnProbeReq},
	{WIFI_PROBERSP,		"OnProbeRsp",		&OnProbeRsp},

	/*----------------------------------------------------------
					below 2 are reserved
	-----------------------------------------------------------*/
	{0,					"DoReserved",		&DoReserved},
	{0,					"DoReserved",		&DoReserved},
	{WIFI_BEACON,		"OnBeacon",		&OnBeacon},
	{WIFI_ATIM,			"OnATIM",		&OnAtim},
	{WIFI_DISASSOC,		"OnDisassoc",		&OnDisassoc},
	{WIFI_AUTH,			"OnAuth",		&OnAuthClient},
	{WIFI_DEAUTH,		"OnDeAuth",		&OnDeAuth},
	{WIFI_ACTION,		"OnAction",		&OnAction},
};

int init_hw_mlme_ext(struct adapter *padapter)
{
	struct	mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);
	return _SUCCESS;
}

static void init_mlme_ext_priv_value(struct adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	unsigned char	mixed_datarate[NumRates] = {
		_1M_RATE_, _2M_RATE_, _5M_RATE_, _11M_RATE_, _6M_RATE_,
		_9M_RATE_, _12M_RATE_, _18M_RATE_, _24M_RATE_, _36M_RATE_,
		 _48M_RATE_, _54M_RATE_, 0xff
	};
	unsigned char	mixed_basicrate[NumRates] = {
		_1M_RATE_, _2M_RATE_, _5M_RATE_, _11M_RATE_, _6M_RATE_,
		_12M_RATE_, _24M_RATE_, 0xff,
	};

	atomic_set(&pmlmeext->event_seq, 0);
	pmlmeext->mgnt_seq = 0;/* reset to zero when disconnect at client mode */

	pmlmeext->cur_channel = padapter->registrypriv.channel;
	pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_20;
	pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	pmlmeext->oper_channel = pmlmeext->cur_channel;
	pmlmeext->oper_bwmode = pmlmeext->cur_bwmode;
	pmlmeext->oper_ch_offset = pmlmeext->cur_ch_offset;
	pmlmeext->retry = 0;

	pmlmeext->cur_wireless_mode = padapter->registrypriv.wireless_mode;

	memcpy(pmlmeext->datarate, mixed_datarate, NumRates);
	memcpy(pmlmeext->basicrate, mixed_basicrate, NumRates);

	pmlmeext->tx_rate = IEEE80211_CCK_RATE_1MB;

	pmlmeext->sitesurvey_res.state = SCAN_DISABLE;
	pmlmeext->sitesurvey_res.channel_idx = 0;
	pmlmeext->sitesurvey_res.bss_cnt = 0;
	pmlmeext->scan_abort = false;

	pmlmeinfo->state = WIFI_FW_NULL_STATE;
	pmlmeinfo->reauth_count = 0;
	pmlmeinfo->reassoc_count = 0;
	pmlmeinfo->link_count = 0;
	pmlmeinfo->auth_seq = 0;
	pmlmeinfo->auth_algo = dot11AuthAlgrthm_Open;
	pmlmeinfo->key_index = 0;
	pmlmeinfo->iv = 0;

	pmlmeinfo->enc_algo = _NO_PRIVACY_;
	pmlmeinfo->authModeToggle = 0;

	memset(pmlmeinfo->chg_txt, 0, 128);

	pmlmeinfo->slotTime = SHORT_SLOT_TIME;
	pmlmeinfo->preamble_mode = PREAMBLE_AUTO;

	pmlmeinfo->dialogToken = 0;

	pmlmeext->action_public_rxseq = 0xffff;
	pmlmeext->action_public_dialog_token = 0xff;
}

static int has_channel(struct rt_channel_info *channel_set,
					   u8 chanset_size,
					   u8 chan)
{
	int i;

	for (i = 0; i < chanset_size; i++) {
		if (channel_set[i].ChannelNum == chan)
			return 1;
	}
	return 0;
}

static void init_channel_list(struct adapter *padapter,
			      struct rt_channel_info *channel_set,
			      u8 chanset_size,
			      struct p2p_channels *channel_list)
{
	struct p2p_oper_class_map op_class[] = {
		{ IEEE80211G,  81,   1,  13,  1, BW20 },
		{ IEEE80211G,  82,  14,  14,  1, BW20 },
		{ -1, 0, 0, 0, 0, BW20 }
	};

	int cla, op;

	cla = 0;

	for (op = 0; op_class[op].op_class; op++) {
		u8 ch;
		struct p2p_oper_class_map *o = &op_class[op];
		struct p2p_reg_class *reg = NULL;

		for (ch = o->min_chan; ch <= o->max_chan; ch += o->inc) {
			if (!has_channel(channel_set, chanset_size, ch))
				continue;

			if ((0 == padapter->registrypriv.ht_enable) && (8 == o->inc))
				continue;

			if ((0 == (padapter->registrypriv.cbw40_enable & BIT(1))) &&
			    ((o->bw == BW40MINUS) || (o->bw == BW40PLUS)))
				continue;

			if (reg == NULL) {
				reg = &channel_list->reg_class[cla];
				cla++;
				reg->reg_class = o->op_class;
				reg->channels = 0;
			}
			reg->channel[reg->channels] = ch;
			reg->channels++;
		}
	}
	channel_list->reg_classes = cla;
}

static u8 init_channel_set(struct adapter *padapter, u8 ChannelPlan,
			   struct rt_channel_info *channel_set)
{
	u8 index, chanset_size = 0;
	u8 b2_4GBand = false;
	u8 Index2G = 0;

	memset(channel_set, 0, sizeof(struct rt_channel_info) * MAX_CHANNEL_NUM);

	if (ChannelPlan >= RT_CHANNEL_DOMAIN_MAX && ChannelPlan != RT_CHANNEL_DOMAIN_REALTEK_DEFINE) {
		DBG_88E("ChannelPlan ID %x error !!!!!\n", ChannelPlan);
		return chanset_size;
	}

	if (padapter->registrypriv.wireless_mode & WIRELESS_11G) {
		b2_4GBand = true;
		if (ChannelPlan == RT_CHANNEL_DOMAIN_REALTEK_DEFINE)
			Index2G = RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE.Index2G;
		else
			Index2G = RTW_ChannelPlanMap[ChannelPlan].Index2G;
	}

	if (b2_4GBand) {
		for (index = 0; index < RTW_ChannelPlan2G[Index2G].Len; index++) {
			channel_set[chanset_size].ChannelNum = RTW_ChannelPlan2G[Index2G].Channel[index];

			if ((ChannelPlan == RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN) ||/* Channel 1~11 is active, and 12~14 is passive */
			    (ChannelPlan == RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN_2G)) {
				if (channel_set[chanset_size].ChannelNum >= 1 && channel_set[chanset_size].ChannelNum <= 11)
					channel_set[chanset_size].ScanType = SCAN_ACTIVE;
				else if ((channel_set[chanset_size].ChannelNum  >= 12 && channel_set[chanset_size].ChannelNum  <= 14))
					channel_set[chanset_size].ScanType  = SCAN_PASSIVE;
			} else if (ChannelPlan == RT_CHANNEL_DOMAIN_WORLD_WIDE_13 ||
				   Index2G == RT_CHANNEL_DOMAIN_2G_WORLD) {/*  channel 12~13, passive scan */
				if (channel_set[chanset_size].ChannelNum <= 11)
					channel_set[chanset_size].ScanType = SCAN_ACTIVE;
				else
					channel_set[chanset_size].ScanType = SCAN_PASSIVE;
			} else {
				channel_set[chanset_size].ScanType = SCAN_ACTIVE;
			}

			chanset_size++;
		}
	}
	return chanset_size;
}

int	init_mlme_ext_priv(struct adapter *padapter)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	pmlmeext->padapter = padapter;

	init_mlme_ext_priv_value(padapter);
	pmlmeinfo->accept_addba_req = pregistrypriv->accept_addba_req;

	init_mlme_ext_timer(padapter);

#ifdef CONFIG_88EU_AP_MODE
	init_mlme_ap_info(padapter);
#endif

	pmlmeext->max_chan_nums = init_channel_set(padapter, pmlmepriv->ChannelPlan, pmlmeext->channel_set);
	init_channel_list(padapter, pmlmeext->channel_set, pmlmeext->max_chan_nums, &pmlmeext->channel_list);

	pmlmeext->chan_scan_time = SURVEY_TO;
	pmlmeext->mlmeext_init = true;


	pmlmeext->active_keep_alive_check = true;

	return _SUCCESS;
}

void free_mlme_ext_priv(struct mlme_ext_priv *pmlmeext)
{
	struct adapter *padapter = pmlmeext->padapter;

	if (!padapter)
		return;

	if (padapter->bDriverStopped) {
		del_timer_sync(&pmlmeext->survey_timer);
		del_timer_sync(&pmlmeext->link_timer);
	}
}

static void _mgt_dispatcher(struct adapter *padapter,
			    struct mlme_handler *ptable,
			    struct recv_frame *precv_frame)
{
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u8 *pframe = precv_frame->pkt->data;

	if (ptable->func) {
		/* receive the frames that ra(a1) is my address or ra(a1) is bc address. */
		if (memcmp(GetAddr1Ptr(pframe), myid(&padapter->eeprompriv), ETH_ALEN) &&
		    memcmp(GetAddr1Ptr(pframe), bc_addr, ETH_ALEN))
			return;
		ptable->func(padapter, precv_frame);
	}
}

void mgt_dispatcher(struct adapter *padapter, struct recv_frame *precv_frame)
{
	int index;
	struct mlme_handler *ptable;
#ifdef CONFIG_88EU_AP_MODE
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
#endif /* CONFIG_88EU_AP_MODE */
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u8 *pframe = precv_frame->pkt->data;
	struct sta_info *psta = rtw_get_stainfo(&padapter->stapriv, GetAddr2Ptr(pframe));

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("+%s: type(0x%x) subtype(0x%x)\n", __func__,
		  (unsigned int)GetFrameType(pframe),
		  (unsigned int)GetFrameSubType(pframe)));

	if (GetFrameType(pframe) != WIFI_MGT_TYPE) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("%s: type(0x%x) error!\n", __func__,
			  (unsigned int)GetFrameType(pframe)));
		return;
	}

	/* receive the frames that ra(a1) is my address or ra(a1) is bc address. */
	if (memcmp(GetAddr1Ptr(pframe), myid(&padapter->eeprompriv), ETH_ALEN) &&
	    memcmp(GetAddr1Ptr(pframe), bc_addr, ETH_ALEN))
		return;

	ptable = mlme_sta_tbl;

	index = GetFrameSubType(pframe) >> 4;

	if (index > 13) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("Currently we do not support reserved sub-fr-type=%d\n", index));
		return;
	}
	ptable += index;

	if (psta) {
		if (GetRetry(pframe)) {
			if (precv_frame->attrib.seq_num ==
			    psta->RxMgmtFrameSeqNum) {
				/* drop the duplicate management frame */
				DBG_88E("Drop duplicate management frame with seq_num=%d.\n",
					precv_frame->attrib.seq_num);
				return;
			}
		}
		psta->RxMgmtFrameSeqNum = precv_frame->attrib.seq_num;
	}

#ifdef CONFIG_88EU_AP_MODE
	switch (GetFrameSubType(pframe)) {
	case WIFI_AUTH:
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE))
			ptable->func = &OnAuth;
		else
			ptable->func = &OnAuthClient;
		/* fall through */
	case WIFI_ASSOCREQ:
	case WIFI_REASSOCREQ:
	case WIFI_PROBEREQ:
	case WIFI_BEACON:
	case WIFI_ACTION:
		_mgt_dispatcher(padapter, ptable, precv_frame);
		break;
	default:
		_mgt_dispatcher(padapter, ptable, precv_frame);
		break;
	}
#else
	_mgt_dispatcher(padapter, ptable, precv_frame);
#endif
}

/****************************************************************************

Following are the functions to report events

*****************************************************************************/

void report_survey_event(struct adapter *padapter,
			 struct recv_frame *precv_frame)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct survey_event	*psurvey_evt;
	struct C2HEvent_Header *pc2h_evt_hdr;
	struct mlme_ext_priv *pmlmeext;
	struct cmd_priv *pcmdpriv;

	if (!padapter)
		return;

	pmlmeext = &padapter->mlmeextpriv;
	pcmdpriv = &padapter->cmdpriv;


	pcmd_obj = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!pcmd_obj)
		return;

	cmdsz = sizeof(struct survey_event) + sizeof(struct C2HEvent_Header);
	pevtcmd = kzalloc(cmdsz, GFP_ATOMIC);
	if (!pevtcmd) {
		kfree(pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = _Set_MLME_EVT_CMD_;
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header *)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct survey_event);
	pc2h_evt_hdr->ID = _Survey_EVT_;
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	psurvey_evt = (struct survey_event *)(pevtcmd + sizeof(struct C2HEvent_Header));

	if (collect_bss_info(padapter, precv_frame, (struct wlan_bssid_ex *)&psurvey_evt->bss) == _FAIL) {
		kfree(pcmd_obj);
		kfree(pevtcmd);
		return;
	}

	process_80211d(padapter, &psurvey_evt->bss);

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	pmlmeext->sitesurvey_res.bss_cnt++;
}

void report_surveydone_event(struct adapter *padapter)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct surveydone_event *psurveydone_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (!pcmd_obj)
		return;

	cmdsz = sizeof(struct surveydone_event) + sizeof(struct C2HEvent_Header);
	pevtcmd = kzalloc(cmdsz, GFP_KERNEL);
	if (!pevtcmd) {
		kfree(pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = _Set_MLME_EVT_CMD_;
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header *)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct surveydone_event);
	pc2h_evt_hdr->ID = _SurveyDone_EVT_;
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	psurveydone_evt = (struct surveydone_event *)(pevtcmd + sizeof(struct C2HEvent_Header));
	psurveydone_evt->bss_cnt = pmlmeext->sitesurvey_res.bss_cnt;

	DBG_88E("survey done event(%x)\n", psurveydone_evt->bss_cnt);

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);
}

void report_join_res(struct adapter *padapter, int res)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct joinbss_event		*pjoinbss_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!pcmd_obj)
		return;

	cmdsz = sizeof(struct joinbss_event) + sizeof(struct C2HEvent_Header);
	pevtcmd = kzalloc(cmdsz, GFP_ATOMIC);
	if (!pevtcmd) {
		kfree(pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = _Set_MLME_EVT_CMD_;
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header *)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct joinbss_event);
	pc2h_evt_hdr->ID = _JoinBss_EVT_;
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	pjoinbss_evt = (struct joinbss_event *)(pevtcmd + sizeof(struct C2HEvent_Header));
	memcpy((unsigned char *)(&(pjoinbss_evt->network.network)), &(pmlmeinfo->network), sizeof(struct wlan_bssid_ex));
	pjoinbss_evt->network.join_res	= res;
	pjoinbss_evt->network.aid = res;

	DBG_88E("%s(%d)\n", __func__, res);


	rtw_joinbss_event_prehandle(padapter, (u8 *)&pjoinbss_evt->network);


	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);
}

void report_del_sta_event(struct adapter *padapter, unsigned char *MacAddr,
			  unsigned short reason)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct sta_info *psta;
	int	mac_id;
	struct stadel_event			*pdel_sta_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (!pcmd_obj)
		return;

	cmdsz = sizeof(struct stadel_event) + sizeof(struct C2HEvent_Header);
	pevtcmd = kzalloc(cmdsz, GFP_KERNEL);
	if (!pevtcmd) {
		kfree(pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = _Set_MLME_EVT_CMD_;
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header *)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct stadel_event);
	pc2h_evt_hdr->ID = _DelSTA_EVT_;
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	pdel_sta_evt = (struct stadel_event *)(pevtcmd + sizeof(struct C2HEvent_Header));
	ether_addr_copy((unsigned char *)(&(pdel_sta_evt->macaddr)), MacAddr);
	memcpy((unsigned char *)(pdel_sta_evt->rsvd), (unsigned char *)(&reason), 2);


	psta = rtw_get_stainfo(&padapter->stapriv, MacAddr);
	if (psta)
		mac_id = (int)psta->mac_id;
	else
		mac_id = -1;

	pdel_sta_evt->mac_id = mac_id;

	DBG_88E("%s: delete STA, mac_id =%d\n", __func__, mac_id);

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);
}

void report_add_sta_event(struct adapter *padapter, unsigned char *MacAddr,
			  int cam_idx)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct stassoc_event		*padd_sta_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (!pcmd_obj)
		return;

	cmdsz = sizeof(struct stassoc_event) + sizeof(struct C2HEvent_Header);
	pevtcmd = kzalloc(cmdsz, GFP_KERNEL);
	if (!pevtcmd) {
		kfree(pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = _Set_MLME_EVT_CMD_;
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header *)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct stassoc_event);
	pc2h_evt_hdr->ID = _AddSTA_EVT_;
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	padd_sta_evt = (struct stassoc_event *)(pevtcmd + sizeof(struct C2HEvent_Header));
	ether_addr_copy((unsigned char *)(&(padd_sta_evt->macaddr)), MacAddr);
	padd_sta_evt->cam_id = cam_idx;

	DBG_88E("%s: add STA\n", __func__);

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);
}


/****************************************************************************

Following are the event callback functions

*****************************************************************************/

/* for sta/adhoc mode */
void update_sta_info(struct adapter *padapter, struct sta_info *psta)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	/* ERP */
	VCS_update(padapter, psta);

	/* HT */
	if (pmlmepriv->htpriv.ht_option) {
		psta->htpriv.ht_option = true;

		psta->htpriv.ampdu_enable = pmlmepriv->htpriv.ampdu_enable;

		if (support_short_GI(padapter, &pmlmeinfo->HT_caps))
			psta->htpriv.sgi = true;

		psta->qos_option = true;
	} else {
		psta->htpriv.ht_option = false;

		psta->htpriv.ampdu_enable = false;

		psta->htpriv.sgi = false;
		psta->qos_option = false;
	}
	psta->htpriv.bwmode = pmlmeext->cur_bwmode;
	psta->htpriv.ch_offset = pmlmeext->cur_ch_offset;

	psta->htpriv.agg_enable_bitmap = 0x0;/* reset */
	psta->htpriv.candidate_tid_bitmap = 0x0;/* reset */

	/* QoS */
	if (pmlmepriv->qospriv.qos_option)
		psta->qos_option = true;


	psta->state = _FW_LINKED;
}

void mlmeext_joinbss_event_callback(struct adapter *padapter, int join_res)
{
	struct sta_info		*psta, *psta_bmc;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *cur_network = &(pmlmeinfo->network);
	struct sta_priv		*pstapriv = &padapter->stapriv;
	u8 join_type;
	u16 media_status;

	if (join_res < 0) {
		join_type = 1;
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));
		rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, null_addr);

		/* restore to initial setting. */
		update_tx_basic_rate(padapter, padapter->registrypriv.wireless_mode);

		goto exit_mlmeext_joinbss_event_callback;
	}

	if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) {
		/* for bc/mc */
		psta_bmc = rtw_get_bcmc_stainfo(padapter);
		if (psta_bmc) {
			pmlmeinfo->FW_sta_info[psta_bmc->mac_id].psta = psta_bmc;
			update_bmc_sta_support_rate(padapter, psta_bmc->mac_id);
			Update_RA_Entry(padapter, psta_bmc->mac_id);
		}
	}


	/* turn on dynamic functions */
	Switch_DM_Func(padapter, DYNAMIC_ALL_FUNC_ENABLE, true);

	/*  update IOT-related issue */
	update_IOT_info(padapter);

	rtw_hal_set_hwreg(padapter, HW_VAR_BASIC_RATE, cur_network->SupportedRates);

	/* BCN interval */
	rtw_hal_set_hwreg(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&pmlmeinfo->bcn_interval));

	/* update capability */
	update_capinfo(padapter, pmlmeinfo->capability);

	/* WMM, Update EDCA param */
	WMMOnAssocRsp(padapter);

	/* HT */
	HTOnAssocRsp(padapter);

	set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	psta = rtw_get_stainfo(pstapriv, cur_network->MacAddress);
	if (psta) { /* only for infra. mode */
		pmlmeinfo->FW_sta_info[psta->mac_id].psta = psta;

		psta->wireless_mode = pmlmeext->cur_wireless_mode;

		/* set per sta rate after updating HT cap. */
		set_sta_rate(padapter, psta);
		rtw_hal_set_hwreg(padapter, HW_VAR_TX_RPT_MAX_MACID, (u8 *)&psta->mac_id);
		media_status = (psta->mac_id<<8)|1; /*   MACID|OPMODE: 1 means connect */
		rtw_hal_set_hwreg(padapter, HW_VAR_H2C_MEDIA_STATUS_RPT, (u8 *)&media_status);
	}

	join_type = 2;
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

	if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE) {
		/*  correcting TSF */
		correct_TSF(padapter, pmlmeext);
	}
	rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_CONNECT, 0);

exit_mlmeext_joinbss_event_callback:

	DBG_88E("=>%s\n", __func__);
}

void mlmeext_sta_add_event_callback(struct adapter *padapter, struct sta_info *psta)
{
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 join_type;

	DBG_88E("%s\n", __func__);

	if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) {
		if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) {/* adhoc master or sta_count>1 */
			/* nothing to do */
		} else { /* adhoc client */
			/*  correcting TSF */
			correct_TSF(padapter, pmlmeext);

			/* start beacon */
			if (send_beacon(padapter) == _FAIL) {
				pmlmeinfo->FW_sta_info[psta->mac_id].status = 0;
				pmlmeinfo->state ^= WIFI_FW_ADHOC_STATE;
				return;
			}
			pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;
		}

		join_type = 2;
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));
	}

	pmlmeinfo->FW_sta_info[psta->mac_id].psta = psta;

	/* rate radaptive */
	Update_RA_Entry(padapter, psta->mac_id);

	/* update adhoc sta_info */
	update_sta_info(padapter, psta);
}

void mlmeext_sta_del_event_callback(struct adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (is_client_associated_to_ap(padapter) || is_IBSS_empty(padapter)) {
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_DISCONNECT, NULL);
		rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, null_addr);

		/* restore to initial setting. */
		update_tx_basic_rate(padapter, padapter->registrypriv.wireless_mode);

		/* switch to the 20M Hz mode after disconnect */
		pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_20;
		pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

		/* SelectChannel(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset); */
		set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);


		flush_all_cam_entry(padapter);

		pmlmeinfo->state = WIFI_FW_NULL_STATE;

		/* set MSR to no link state -> infra. mode */
		Set_MSR(padapter, _HW_STATE_STATION_);

		del_timer_sync(&pmlmeext->link_timer);
	}
}

/****************************************************************************

Following are the functions for the timer handlers

*****************************************************************************/

static u8 chk_ap_is_alive(struct adapter *padapter, struct sta_info *psta)
{
	u8 ret = false;

	if ((sta_rx_data_pkts(psta) == sta_last_rx_data_pkts(psta)) &&
	    sta_rx_beacon_pkts(psta) == sta_last_rx_beacon_pkts(psta) &&
	    sta_rx_probersp_pkts(psta) == sta_last_rx_probersp_pkts(psta))
		ret = false;
	else
		ret = true;

	sta_update_last_rx_pkts(psta);

	return ret;
}

void linked_status_chk(struct adapter *padapter)
{
	u32	i;
	struct sta_info		*psta;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv		*pstapriv = &padapter->stapriv;

	if (is_client_associated_to_ap(padapter)) {
		/* linked infrastructure client mode */

		int tx_chk = _SUCCESS, rx_chk = _SUCCESS;
		int rx_chk_limit;

		rx_chk_limit = 4;
		psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress);
		if (psta) {
			bool is_p2p_enable = false;

			if (!chk_ap_is_alive(padapter, psta))
				rx_chk = _FAIL;

			if (pxmitpriv->last_tx_pkts == pxmitpriv->tx_pkts)
				tx_chk = _FAIL;

			if (pmlmeext->active_keep_alive_check && (rx_chk == _FAIL || tx_chk == _FAIL)) {
				u8 backup_oper_channel = 0;

				/* switch to correct channel of current network  before issue keep-alive frames */
				if (rtw_get_oper_ch(padapter) != pmlmeext->cur_channel) {
					backup_oper_channel = rtw_get_oper_ch(padapter);
					SelectChannel(padapter, pmlmeext->cur_channel);
				}

				if (rx_chk != _SUCCESS)
					issue_probereq_ex(padapter, &pmlmeinfo->network.Ssid, psta->hwaddr, 3, 1);

				if ((tx_chk != _SUCCESS && pmlmeinfo->link_count++ == 0xf) || rx_chk != _SUCCESS) {
					tx_chk = issue_nulldata(padapter, psta->hwaddr, 0, 3, 1);
					/* if tx acked and p2p disabled, set rx_chk _SUCCESS to reset retry count */
					if (tx_chk == _SUCCESS && !is_p2p_enable)
						rx_chk = _SUCCESS;
				}

				/* back to the original operation channel */
				if (backup_oper_channel > 0)
					SelectChannel(padapter, backup_oper_channel);
			} else {
				if (rx_chk != _SUCCESS) {
					if (pmlmeext->retry == 0) {
						issue_probereq(padapter,
						&pmlmeinfo->network.Ssid,
						pmlmeinfo->network.MacAddress,
									false);
						issue_probereq(padapter,
						&pmlmeinfo->network.Ssid,
						pmlmeinfo->network.MacAddress,
									false);
						issue_probereq(padapter,
						&pmlmeinfo->network.Ssid,
						pmlmeinfo->network.MacAddress,
									false);
					}
				}

				if (tx_chk != _SUCCESS && pmlmeinfo->link_count++ == 0xf)
					tx_chk = issue_nulldata(padapter, NULL, 0, 1, 0);
			}

			if (rx_chk == _FAIL) {
				pmlmeext->retry++;
				if (pmlmeext->retry > rx_chk_limit) {
					DBG_88E_LEVEL(_drv_always_, FUNC_ADPT_FMT" disconnect or roaming\n",
						      FUNC_ADPT_ARG(padapter));
					receive_disconnect(padapter, pmlmeinfo->network.MacAddress,
							   WLAN_REASON_EXPIRATION_CHK);
					return;
				}
			} else {
				pmlmeext->retry = 0;
			}

			if (tx_chk == _FAIL) {
				pmlmeinfo->link_count &= 0xf;
			} else {
				pxmitpriv->last_tx_pkts = pxmitpriv->tx_pkts;
				pmlmeinfo->link_count = 0;
			}
		} /* end of if ((psta = rtw_get_stainfo(pstapriv, passoc_res->network.MacAddress)) != NULL) */
	} else if (is_client_associated_to_ibss(padapter)) {
		/* linked IBSS mode */
		/* for each assoc list entry to check the rx pkt counter */
		for (i = IBSS_START_MAC_ID; i < NUM_STA; i++) {
			if (pmlmeinfo->FW_sta_info[i].status == 1) {
				psta = pmlmeinfo->FW_sta_info[i].psta;

				if (!psta)
					continue;
				if (pmlmeinfo->FW_sta_info[i].rx_pkt == sta_rx_pkts(psta)) {
					if (pmlmeinfo->FW_sta_info[i].retry < 3) {
						pmlmeinfo->FW_sta_info[i].retry++;
					} else {
						pmlmeinfo->FW_sta_info[i].retry = 0;
						pmlmeinfo->FW_sta_info[i].status = 0;
						report_del_sta_event(padapter, psta->hwaddr
							, 65535/*  indicate disconnect caused by no rx */
					);
					}
				} else {
					pmlmeinfo->FW_sta_info[i].retry = 0;
					pmlmeinfo->FW_sta_info[i].rx_pkt = (u32)sta_rx_pkts(psta);
				}
			}
		}
	}
}

void survey_timer_hdl(struct timer_list *t)
{
	struct adapter *padapter = from_timer(padapter, t,
					      mlmeextpriv.survey_timer);
	struct cmd_obj	*ph2c;
	struct sitesurvey_parm	*psurveyPara;
	struct cmd_priv					*pcmdpriv = &padapter->cmdpriv;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;

	/* issue rtw_sitesurvey_cmd */
	if (pmlmeext->sitesurvey_res.state > SCAN_START) {
		if (pmlmeext->sitesurvey_res.state ==  SCAN_PROCESS)
			pmlmeext->sitesurvey_res.channel_idx++;

		if (pmlmeext->scan_abort) {
			pmlmeext->sitesurvey_res.channel_idx = pmlmeext->sitesurvey_res.ch_num;
			DBG_88E("%s idx:%d\n", __func__
				, pmlmeext->sitesurvey_res.channel_idx);

			pmlmeext->scan_abort = false;/* reset */
		}

		ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
		if (!ph2c)
			goto exit_survey_timer_hdl;

		psurveyPara = kzalloc(sizeof(struct sitesurvey_parm), GFP_ATOMIC);
		if (!psurveyPara) {
			kfree(ph2c);
			goto exit_survey_timer_hdl;
		}

		init_h2fwcmd_w_parm_no_rsp(ph2c, psurveyPara, _SiteSurvey_CMD_);
		rtw_enqueue_cmd(pcmdpriv, ph2c);
	}


exit_survey_timer_hdl:
	return;
}

void link_timer_hdl(struct timer_list *t)
{
	struct adapter *padapter = from_timer(padapter, t,
					    mlmeextpriv.link_timer);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pmlmeinfo->state & WIFI_FW_AUTH_NULL) {
		DBG_88E("%s:no beacon while connecting\n", __func__);
		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		report_join_res(padapter, -3);
	} else if (pmlmeinfo->state & WIFI_FW_AUTH_STATE) {
		/* re-auth timer */
		if (++pmlmeinfo->reauth_count > REAUTH_LIMIT) {
			pmlmeinfo->state = 0;
			report_join_res(padapter, -1);
			return;
		}

		DBG_88E("%s: auth timeout and try again\n", __func__);
		pmlmeinfo->auth_seq = 1;
		issue_auth(padapter, NULL, 0);
		set_link_timer(pmlmeext, REAUTH_TO);
	} else if (pmlmeinfo->state & WIFI_FW_ASSOC_STATE) {
		/* re-assoc timer */
		if (++pmlmeinfo->reassoc_count > REASSOC_LIMIT) {
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
			report_join_res(padapter, -2);
			return;
		}

		DBG_88E("%s: assoc timeout and try again\n", __func__);
		issue_assocreq(padapter);
		set_link_timer(pmlmeext, REASSOC_TO);
	}
}

void addba_timer_hdl(struct timer_list *t)
{
	struct sta_info *psta = from_timer(psta, t, addba_retry_timer);
	struct ht_priv	*phtpriv;

	if (!psta)
		return;

	phtpriv = &psta->htpriv;

	if ((phtpriv->ht_option) && (phtpriv->ampdu_enable)) {
		if (phtpriv->candidate_tid_bitmap)
			phtpriv->candidate_tid_bitmap = 0x0;
	}
}

u8 setopmode_hdl(struct adapter *padapter, u8 *pbuf)
{
	u8 type;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct setopmode_parm *psetop = (struct setopmode_parm *)pbuf;

	if (psetop->mode == Ndis802_11APMode) {
		pmlmeinfo->state = WIFI_FW_AP_STATE;
		type = _HW_STATE_AP_;
	} else if (psetop->mode == Ndis802_11Infrastructure) {
		pmlmeinfo->state &= ~(BIT(0)|BIT(1));/*  clear state */
		pmlmeinfo->state |= WIFI_FW_STATION_STATE;/* set to	STATION_STATE */
		type = _HW_STATE_STATION_;
	} else if (psetop->mode == Ndis802_11IBSS) {
		type = _HW_STATE_ADHOC_;
	} else {
		type = _HW_STATE_NOLINK_;
	}

	rtw_hal_set_hwreg(padapter, HW_VAR_SET_OPMODE, (u8 *)(&type));
	/* Set_NETYPE0_MSR(padapter, type); */

	return H2C_SUCCESS;
}

u8 createbss_hdl(struct adapter *padapter, u8 *pbuf)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *pnetwork = (struct wlan_bssid_ex *)(&(pmlmeinfo->network));
	struct wlan_bssid_ex *pparm = (struct wlan_bssid_ex *)pbuf;
	/* u32	initialgain; */


	if (pparm->InfrastructureMode == Ndis802_11APMode) {
#ifdef CONFIG_88EU_AP_MODE

		if (pmlmeinfo->state == WIFI_FW_AP_STATE) {
			/* todo: */
			return H2C_SUCCESS;
		}
#endif
	}

	/* below is for ad-hoc master */
	if (pparm->InfrastructureMode == Ndis802_11IBSS) {
		rtw_joinbss_reset(padapter);

		pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_20;
		pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		pmlmeinfo->ERP_enable = 0;
		pmlmeinfo->WMM_enable = 0;
		pmlmeinfo->HT_enable = 0;
		pmlmeinfo->HT_caps_enable = 0;
		pmlmeinfo->HT_info_enable = 0;
		pmlmeinfo->agg_enable_bitmap = 0;
		pmlmeinfo->candidate_tid_bitmap = 0;

		/* disable dynamic functions, such as high power, DIG */
		Save_DM_Func_Flag(padapter);
		Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, false);

		/* config the initial gain under linking, need to write the BB registers */
		/* initialgain = 0x1E; */
		/* rtw_hal_set_hwreg(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain)); */

		/* cancel link timer */
		del_timer_sync(&pmlmeext->link_timer);

		/* clear CAM */
		flush_all_cam_entry(padapter);

		memcpy(pnetwork, pbuf, offsetof(struct wlan_bssid_ex, IELength));
		pnetwork->IELength = ((struct wlan_bssid_ex *)pbuf)->IELength;

		if (pnetwork->IELength > MAX_IE_SZ)/* Check pbuf->IELength */
			return H2C_PARAMETERS_ERROR;

		memcpy(pnetwork->IEs, ((struct wlan_bssid_ex *)pbuf)->IEs, pnetwork->IELength);

		start_create_ibss(padapter);
	}

	return H2C_SUCCESS;
}

u8 join_cmd_hdl(struct adapter *padapter, u8 *pbuf)
{
	u8 join_type;
	struct ndis_802_11_var_ie *pIE;
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *pnetwork = (struct wlan_bssid_ex *)(&(pmlmeinfo->network));
	struct wlan_bssid_ex *pparm = (struct wlan_bssid_ex *)pbuf;
	u32 i;

	/* check already connecting to AP or not */
	if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) {
		if (pmlmeinfo->state & WIFI_FW_STATION_STATE)
			issue_deauth_ex(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING, 5, 100);

		pmlmeinfo->state = WIFI_FW_NULL_STATE;

		/* clear CAM */
		flush_all_cam_entry(padapter);

		del_timer_sync(&pmlmeext->link_timer);

		/* set MSR to nolink -> infra. mode */
		Set_MSR(padapter, _HW_STATE_STATION_);


		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_DISCONNECT, NULL);
	}

	rtw_antenna_select_cmd(padapter, pparm->PhyInfo.Optimum_antenna, false);

	rtw_joinbss_reset(padapter);

	pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_20;
	pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	pmlmeinfo->ERP_enable = 0;
	pmlmeinfo->WMM_enable = 0;
	pmlmeinfo->HT_enable = 0;
	pmlmeinfo->HT_caps_enable = 0;
	pmlmeinfo->HT_info_enable = 0;
	pmlmeinfo->agg_enable_bitmap = 0;
	pmlmeinfo->candidate_tid_bitmap = 0;
	pmlmeinfo->bwmode_updated = false;

	memcpy(pnetwork, pbuf, offsetof(struct wlan_bssid_ex, IELength));
	pnetwork->IELength = ((struct wlan_bssid_ex *)pbuf)->IELength;

	if (pnetwork->IELength > MAX_IE_SZ)/* Check pbuf->IELength */
		return H2C_PARAMETERS_ERROR;

	memcpy(pnetwork->IEs, ((struct wlan_bssid_ex *)pbuf)->IEs, pnetwork->IELength);

	/* Check AP vendor to move rtw_joinbss_cmd() */

	for (i = sizeof(struct ndis_802_11_fixed_ie); i < pnetwork->IELength;) {
		pIE = (struct ndis_802_11_var_ie *)(pnetwork->IEs + i);

		switch (pIE->ElementID) {
		case _VENDOR_SPECIFIC_IE_:/* Get WMM IE. */
			if (!memcmp(pIE->data, WMM_OUI, 4))
				pmlmeinfo->WMM_enable = 1;
			break;
		case _HT_CAPABILITY_IE_:	/* Get HT Cap IE. */
			pmlmeinfo->HT_caps_enable = 1;
			break;
		case _HT_EXTRA_INFO_IE_:	/* Get HT Info IE. */
			pmlmeinfo->HT_info_enable = 1;

			/* spec case only for cisco's ap because cisco's ap issue assoc rsp using mcs rate @40MHz or @20MHz */
			{
				struct HT_info_element *pht_info = (struct HT_info_element *)(pIE->data);

				if ((pregpriv->cbw40_enable) &&	 (pht_info->infos[0] & BIT(2))) {
					/* switch to the 40M Hz mode according to the AP */
					pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_40;
					switch (pht_info->infos[0] & 0x3) {
					case 1:
						pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
						break;
					case 3:
						pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
						break;
					default:
						pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
						break;
				}

					DBG_88E("set ch/bw before connected\n");
				}
			}
			break;
		default:
			break;
		}

		i += (pIE->Length + 2);
	}
	/* disable dynamic functions, such as high power, DIG */

	/* config the initial gain under linking, need to write the BB registers */

	rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, pmlmeinfo->network.MacAddress);
	join_type = 0;
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

	/* cancel link timer */
	del_timer_sync(&pmlmeext->link_timer);

	start_clnt_join(padapter);

	return H2C_SUCCESS;
}

u8 disconnect_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	struct disconnect_parm *param = (struct disconnect_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *pnetwork = (struct wlan_bssid_ex *)(&(pmlmeinfo->network));
	u8 val8;

	if (is_client_associated_to_ap(padapter))
		issue_deauth_ex(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING, param->deauth_timeout_ms/100, 100);

	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_DISCONNECT, NULL);
	rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, null_addr);

	/* restore to initial setting. */
	update_tx_basic_rate(padapter, padapter->registrypriv.wireless_mode);

	if (((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)) {
		/* Stop BCN */
		val8 = 0;
		rtw_hal_set_hwreg(padapter, HW_VAR_BCN_FUNC, (u8 *)(&val8));
	}


	/* set MSR to no link state -> infra. mode */
	Set_MSR(padapter, _HW_STATE_STATION_);

	pmlmeinfo->state = WIFI_FW_NULL_STATE;

	/* switch to the 20M Hz mode after disconnect */
	pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_20;
	pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	flush_all_cam_entry(padapter);

	del_timer_sync(&pmlmeext->link_timer);

	rtw_free_uc_swdec_pending_queue(padapter);

	return	H2C_SUCCESS;
}

static int rtw_scan_ch_decision(struct adapter *padapter,
				struct rtw_ieee80211_channel *out,
				u32 out_num,
				struct rtw_ieee80211_channel *in, u32 in_num)
{
	int i, j;
	int set_idx;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	/* clear out first */
	memset(out, 0, sizeof(struct rtw_ieee80211_channel)*out_num);

	/* acquire channels from in */
	j = 0;
	for (i = 0; i < in_num; i++) {
		set_idx = rtw_ch_set_search_ch(pmlmeext->channel_set, in[i].hw_value);
		if (in[i].hw_value && !(in[i].flags & RTW_IEEE80211_CHAN_DISABLED) &&
		    set_idx >= 0) {
			out[j] = in[i];

			if (pmlmeext->channel_set[set_idx].ScanType == SCAN_PASSIVE)
				out[j].flags &= RTW_IEEE80211_CHAN_PASSIVE_SCAN;

			j++;
		}
		if (j >= out_num)
			break;
	}

	/* if out is empty, use channel_set as default */
	if (j == 0) {
		for (i = 0; i < pmlmeext->max_chan_nums; i++) {
			out[i].hw_value = pmlmeext->channel_set[i].ChannelNum;

			if (pmlmeext->channel_set[i].ScanType == SCAN_PASSIVE)
				out[i].flags &= RTW_IEEE80211_CHAN_PASSIVE_SCAN;

			j++;
		}
	}

	return j;
}

u8 sitesurvey_cmd_hdl(struct adapter *padapter, u8 *pbuf)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct sitesurvey_parm	*pparm = (struct sitesurvey_parm *)pbuf;
	u8 bdelayscan = false;
	u8 val8;
	u32	initialgain;
	u32	i;

	if (pmlmeext->sitesurvey_res.state == SCAN_DISABLE) {
		/* for first time sitesurvey_cmd */
		rtw_hal_set_hwreg(padapter, HW_VAR_CHECK_TXBUF, NULL);

		pmlmeext->sitesurvey_res.state = SCAN_START;
		pmlmeext->sitesurvey_res.bss_cnt = 0;
		pmlmeext->sitesurvey_res.channel_idx = 0;

		for (i = 0; i < RTW_SSID_SCAN_AMOUNT; i++) {
			if (pparm->ssid[i].SsidLength) {
				memcpy(pmlmeext->sitesurvey_res.ssid[i].Ssid, pparm->ssid[i].Ssid, IW_ESSID_MAX_SIZE);
				pmlmeext->sitesurvey_res.ssid[i].SsidLength = pparm->ssid[i].SsidLength;
			} else {
				pmlmeext->sitesurvey_res.ssid[i].SsidLength = 0;
			}
		}

		pmlmeext->sitesurvey_res.ch_num = rtw_scan_ch_decision(padapter
			, pmlmeext->sitesurvey_res.ch, RTW_CHANNEL_SCAN_AMOUNT
			, pparm->ch, pparm->ch_num
	);

		pmlmeext->sitesurvey_res.scan_mode = pparm->scan_mode;

		/* issue null data if associating to the AP */
		if (is_client_associated_to_ap(padapter)) {
			pmlmeext->sitesurvey_res.state = SCAN_TXNULL;

			issue_nulldata(padapter, NULL, 1, 3, 500);

			bdelayscan = true;
		}
		if (bdelayscan) {
			/* delay 50ms to protect nulldata(1). */
			set_survey_timer(pmlmeext, 50);
			return H2C_SUCCESS;
		}
	}

	if ((pmlmeext->sitesurvey_res.state == SCAN_START) || (pmlmeext->sitesurvey_res.state == SCAN_TXNULL)) {
		/* disable dynamic functions, such as high power, DIG */
		Save_DM_Func_Flag(padapter);
		Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, false);

		/* config the initial gain under scanning, need to write the BB registers */
		initialgain = 0x1E;

		rtw_hal_set_hwreg(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));

		/* set MSR to no link state */
		Set_MSR(padapter, _HW_STATE_NOLINK_);

		val8 = 1; /* under site survey */
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

		pmlmeext->sitesurvey_res.state = SCAN_PROCESS;
	}

	site_survey(padapter);

	return H2C_SUCCESS;
}

u8 setauth_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	struct setauth_parm		*pparm = (struct setauth_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pparm->mode < 4)
		pmlmeinfo->auth_algo = pparm->mode;
	return	H2C_SUCCESS;
}

u8 setkey_hdl(struct adapter *padapter, u8 *pbuf)
{
	unsigned short				ctrl;
	struct setkey_parm		*pparm = (struct setkey_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	unsigned char					null_sta[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	/* main tx key for wep. */
	if (pparm->set_tx)
		pmlmeinfo->key_index = pparm->keyid;

	/* write cam */
	ctrl = BIT(15) | ((pparm->algorithm) << 2) | pparm->keyid;

	DBG_88E_LEVEL(_drv_info_, "set group key to hw: alg:%d(WEP40-1 WEP104-5 TKIP-2 AES-4) "
			"keyid:%d\n", pparm->algorithm, pparm->keyid);
	write_cam(padapter, pparm->keyid, ctrl, null_sta, pparm->key);

	return H2C_SUCCESS;
}

u8 set_stakey_hdl(struct adapter *padapter, u8 *pbuf)
{
	u16 ctrl = 0;
	u8 cam_id;/* cam_entry */
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct set_stakey_parm	*pparm = (struct set_stakey_parm *)pbuf;

	/* cam_entry: */
	/* 0~3 for default key */

	/* for concurrent mode (ap+sta): */
	/* default key is disable, using sw encrypt/decrypt */
	/* cam_entry = 4 for sta mode (macid = 0) */
	/* cam_entry(macid+3) = 5 ~ N for ap mode (aid = 1~N, macid = 2 ~N) */

	/* for concurrent mode (sta+sta): */
	/* default key is disable, using sw encrypt/decrypt */
	/* cam_entry = 4 mapping to macid = 0 */
	/* cam_entry = 5 mapping to macid = 2 */

	cam_id = 4;

	DBG_88E_LEVEL(_drv_info_, "set pairwise key to hw: alg:%d(WEP40-1 WEP104-5 TKIP-2 AES-4) camid:%d\n",
		      pparm->algorithm, cam_id);
	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE) {
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		if (pparm->algorithm == _NO_PRIVACY_)	/*  clear cam entry */ {
			clear_cam_entry(padapter, pparm->id);
			return H2C_SUCCESS_RSP;
		}

		psta = rtw_get_stainfo(pstapriv, pparm->addr);
		if (psta) {
			ctrl = BIT(15) | ((pparm->algorithm) << 2);

			DBG_88E("r871x_set_stakey_hdl(): enc_algorithm=%d\n", pparm->algorithm);

			if ((psta->mac_id < 1) || (psta->mac_id > (NUM_STA-4))) {
				DBG_88E("r871x_set_stakey_hdl():set_stakey failed, mac_id(aid)=%d\n", psta->mac_id);
				return H2C_REJECTED;
			}

			cam_id = psta->mac_id + 3;/* 0~3 for default key, cmd_id = macid + 3, macid = aid+1; */

			DBG_88E("Write CAM, mac_addr =%pM, cam_entry=%d\n",
				pparm->addr, cam_id);

			write_cam(padapter, cam_id, ctrl, pparm->addr, pparm->key);

			return H2C_SUCCESS_RSP;
		} else {
			DBG_88E("r871x_set_stakey_hdl(): sta has been free\n");
			return H2C_REJECTED;
		}
	}

	/* below for sta mode */

	if (pparm->algorithm == _NO_PRIVACY_) {	/*  clear cam entry */
		clear_cam_entry(padapter, pparm->id);
		return H2C_SUCCESS;
	}
	ctrl = BIT(15) | ((pparm->algorithm) << 2);
	write_cam(padapter, cam_id, ctrl, pparm->addr, pparm->key);
	pmlmeinfo->enc_algo = pparm->algorithm;
	return H2C_SUCCESS;
}

u8 add_ba_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	struct addBaReq_parm	*pparm = (struct addBaReq_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	struct sta_info *psta = rtw_get_stainfo(&padapter->stapriv, pparm->addr);

	if (!psta)
		return	H2C_SUCCESS;

	if (((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && (pmlmeinfo->HT_enable)) ||
	    ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)) {
		issue_action_BA(padapter, pparm->addr, RTW_WLAN_ACTION_ADDBA_REQ, (u16)pparm->tid);
		mod_timer(&psta->addba_retry_timer,
			  jiffies + msecs_to_jiffies(ADDBA_TO));
	} else {
		psta->htpriv.candidate_tid_bitmap &= ~BIT(pparm->tid);
	}
	return	H2C_SUCCESS;
}

u8 set_tx_beacon_cmd(struct adapter *padapter)
{
	struct cmd_obj	*ph2c;
	struct wlan_bssid_ex	*ptxBeacon_parm;
	struct cmd_priv	*pcmdpriv = &(padapter->cmdpriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 res = _SUCCESS;
	int len_diff = 0;


	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	ptxBeacon_parm = kmemdup(&(pmlmeinfo->network),
				sizeof(struct wlan_bssid_ex), GFP_ATOMIC);
	if (ptxBeacon_parm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	len_diff = update_hidden_ssid(ptxBeacon_parm->IEs+_BEACON_IE_OFFSET_,
				      ptxBeacon_parm->IELength-_BEACON_IE_OFFSET_,
				      pmlmeinfo->hidden_ssid_mode);
	ptxBeacon_parm->IELength += len_diff;

	init_h2fwcmd_w_parm_no_rsp(ph2c, ptxBeacon_parm, _TX_Beacon_CMD_);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);


exit:


	return res;
}

u8 mlme_evt_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	u8 evt_code;
	u16 evt_sz;
	uint	*peventbuf;
	void (*event_callback)(struct adapter *dev, u8 *pbuf);

	peventbuf = (uint *)pbuf;
	evt_sz = (u16)(*peventbuf&0xffff);
	evt_code = (u8)((*peventbuf>>16)&0xff);

	/*  checking if event code is valid */
	if (evt_code >= MAX_C2HEVT) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("\nEvent Code(%d) mismatch!\n", evt_code));
		goto _abort_event_;
	}

	/*  checking if event size match the event parm size */
	if ((wlanevents[evt_code].parmsize != 0) &&
	    (wlanevents[evt_code].parmsize != evt_sz)) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_,
			 ("\nEvent(%d) Parm Size mismatch (%d vs %d)!\n",
			 evt_code, wlanevents[evt_code].parmsize, evt_sz));
		goto _abort_event_;
	}

	peventbuf += 2;

	if (peventbuf) {
		event_callback = wlanevents[evt_code].event_callback;
		event_callback(padapter, (u8 *)peventbuf);

	}

_abort_event_:
	return H2C_SUCCESS;
}

u8 tx_beacon_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	if (send_beacon(padapter) == _FAIL) {
		DBG_88E("issue_beacon, fail!\n");
		return H2C_PARAMETERS_ERROR;
	}
#ifdef CONFIG_88EU_AP_MODE
	else { /* tx bc/mc frames after update TIM */
		struct sta_info *psta_bmc;
		struct list_head *xmitframe_plist, *xmitframe_phead;
		struct xmit_frame *pxmitframe = NULL;
		struct sta_priv  *pstapriv = &padapter->stapriv;

		/* for BC/MC Frames */
		psta_bmc = rtw_get_bcmc_stainfo(padapter);
		if (!psta_bmc)
			return H2C_SUCCESS;

		if ((pstapriv->tim_bitmap&BIT(0)) && (psta_bmc->sleepq_len > 0)) {
			msleep(10);/*  10ms, ATIM(HIQ) Windows */
			spin_lock_bh(&psta_bmc->sleep_q.lock);

			xmitframe_phead = get_list_head(&psta_bmc->sleep_q);
			xmitframe_plist = xmitframe_phead->next;

			while (xmitframe_phead != xmitframe_plist) {
				pxmitframe = container_of(xmitframe_plist, struct xmit_frame, list);

				xmitframe_plist = xmitframe_plist->next;

				list_del_init(&pxmitframe->list);

				psta_bmc->sleepq_len--;
				if (psta_bmc->sleepq_len > 0)
					pxmitframe->attrib.mdata = 1;
				else
					pxmitframe->attrib.mdata = 0;

				pxmitframe->attrib.triggered = 1;

				pxmitframe->attrib.qsel = 0x11;/* HIQ */

				spin_unlock_bh(&psta_bmc->sleep_q.lock);
				if (rtw_hal_xmit(padapter, pxmitframe))
					rtw_os_xmit_complete(padapter, pxmitframe);
				spin_lock_bh(&psta_bmc->sleep_q.lock);
			}
			spin_unlock_bh(&psta_bmc->sleep_q.lock);
		}
	}
#endif
	return H2C_SUCCESS;
}

u8 set_ch_hdl(struct adapter *padapter, u8 *pbuf)
{
	struct set_ch_parm *set_ch_parm;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	set_ch_parm = (struct set_ch_parm *)pbuf;

	DBG_88E(FUNC_NDEV_FMT" ch:%u, bw:%u, ch_offset:%u\n",
		FUNC_NDEV_ARG(padapter->pnetdev),
		set_ch_parm->ch, set_ch_parm->bw, set_ch_parm->ch_offset);

	pmlmeext->cur_channel = set_ch_parm->ch;
	pmlmeext->cur_ch_offset = set_ch_parm->ch_offset;
	pmlmeext->cur_bwmode = set_ch_parm->bw;

	set_channel_bwmode(padapter, set_ch_parm->ch, set_ch_parm->ch_offset, set_ch_parm->bw);

	return	H2C_SUCCESS;
}

u8 set_chplan_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	struct SetChannelPlan_param *setChannelPlan_param;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	setChannelPlan_param = (struct SetChannelPlan_param *)pbuf;

	pmlmeext->max_chan_nums = init_channel_set(padapter, setChannelPlan_param->channel_plan, pmlmeext->channel_set);
	init_channel_list(padapter, pmlmeext->channel_set, pmlmeext->max_chan_nums, &pmlmeext->channel_list);

	return	H2C_SUCCESS;
}
