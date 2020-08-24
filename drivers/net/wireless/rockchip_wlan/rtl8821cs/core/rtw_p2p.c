/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#define _RTW_P2P_C_

#include <drv_types.h>

#ifdef CONFIG_P2P

int rtw_p2p_is_channel_list_ok(u8 desired_ch, u8 *ch_list, u8 ch_cnt)
{
	int found = 0, i = 0;

	for (i = 0; i < ch_cnt; i++) {
		if (ch_list[i] == desired_ch) {
			found = 1;
			break;
		}
	}
	return found ;
}

int is_any_client_associated(_adapter *padapter)
{
	return padapter->stapriv.asoc_list_cnt ? _TRUE : _FALSE;
}

static u32 go_add_group_info_attr(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	_irqL irqL;
	_list	*phead, *plist;
	u32 len = 0;
	u16 attr_len = 0;
	u8 tmplen, *pdata_attr, *pstart, *pcur;
	struct sta_info *psta = NULL;
	_adapter *padapter = pwdinfo->padapter;
	struct sta_priv *pstapriv = &padapter->stapriv;

	RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

	pdata_attr = rtw_zmalloc(MAX_P2P_IE_LEN);

	if (NULL == pdata_attr) {
		RTW_INFO("%s pdata_attr malloc failed\n", __FUNCTION__);
		goto _exit;
	}

	pstart = pdata_attr;
	pcur = pdata_attr;

	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	/* look up sta asoc_queue */
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);

		plist = get_next(plist);


		if (psta->is_p2p_device) {
			tmplen = 0;

			pcur++;

			/* P2P device address */
			_rtw_memcpy(pcur, psta->dev_addr, ETH_ALEN);
			pcur += ETH_ALEN;

			/* P2P interface address */
			_rtw_memcpy(pcur, psta->cmn.mac_addr, ETH_ALEN);
			pcur += ETH_ALEN;

			*pcur = psta->dev_cap;
			pcur++;

			/* *(u16*)(pcur) = cpu_to_be16(psta->config_methods); */
			RTW_PUT_BE16(pcur, psta->config_methods);
			pcur += 2;

			_rtw_memcpy(pcur, psta->primary_dev_type, 8);
			pcur += 8;

			*pcur = psta->num_of_secdev_type;
			pcur++;

			_rtw_memcpy(pcur, psta->secdev_types_list, psta->num_of_secdev_type * 8);
			pcur += psta->num_of_secdev_type * 8;

			if (psta->dev_name_len > 0) {
				/* *(u16*)(pcur) = cpu_to_be16( WPS_ATTR_DEVICE_NAME ); */
				RTW_PUT_BE16(pcur, WPS_ATTR_DEVICE_NAME);
				pcur += 2;

				/* *(u16*)(pcur) = cpu_to_be16( psta->dev_name_len ); */
				RTW_PUT_BE16(pcur, psta->dev_name_len);
				pcur += 2;

				_rtw_memcpy(pcur, psta->dev_name, psta->dev_name_len);
				pcur += psta->dev_name_len;
			}


			tmplen = (u8)(pcur - pstart);

			*pstart = (tmplen - 1);

			attr_len += tmplen;

			/* pstart += tmplen; */
			pstart = pcur;

		}


	}
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	if (attr_len > 0)
		len = rtw_set_p2p_attr_content(pbuf, P2P_ATTR_GROUP_INFO, attr_len, pdata_attr);

	rtw_mfree(pdata_attr, MAX_P2P_IE_LEN);

_exit:
	return len;

}

static void issue_group_disc_req(struct wifidirect_info *pwdinfo, u8 *da)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	_adapter *padapter = pwdinfo->padapter;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	unsigned char category = RTW_WLAN_CATEGORY_P2P;/* P2P action frame	 */
	u32	p2poui = cpu_to_be32(P2POUI);
	u8	oui_subtype = P2P_GO_DISC_REQUEST;
	u8	dialogToken = 0;

	RTW_INFO("[%s]\n", __FUNCTION__);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, pwdinfo->interface_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, pwdinfo->interface_addr, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	/* Build P2P action frame header */
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));

	/* there is no IE in this P2P action frame */

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

}

static void issue_p2p_devdisc_resp(struct wifidirect_info *pwdinfo, u8 *da, u8 status, u8 dialogToken)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	_adapter *padapter = pwdinfo->padapter;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_DEVDISC_RESP;
	u8 p2pie[8] = { 0x00 };
	u32 p2pielen = 0;

	RTW_INFO("[%s]\n", __FUNCTION__);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, pwdinfo->device_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, pwdinfo->device_addr, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	/* Build P2P public action frame header */
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));


	/* Build P2P IE */
	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/* P2P_ATTR_STATUS */
	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_STATUS, 1, &status);

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, p2pie, &pattrib->pktlen);

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

}

static void issue_p2p_provision_resp(struct wifidirect_info *pwdinfo, u8 *raddr, u8 *frame_body, u16 config_method)
{
	_adapter *padapter = pwdinfo->padapter;
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u8			dialogToken = frame_body[7];	/*	The Dialog Token of provisioning discovery request frame. */
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_PROVISION_DISC_RESP;
	u8			wpsie[100] = { 0x00 };
	u8			wpsielen = 0;
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif

	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);


	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, raddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, adapter_mac_addr(padapter), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));

	wpsielen = 0;
	/*	WPS OUI */
	/* *(u32*) ( wpsie ) = cpu_to_be32( WPSOUI ); */
	RTW_PUT_BE32(wpsie, WPSOUI);
	wpsielen += 4;

#if 0
	/*	WPS version */
	/*	Type: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_VER1);
	wpsielen += 2;

	/*	Length: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
	wpsielen += 2;

	/*	Value: */
	wpsie[wpsielen++] = WPS_VERSION_1;	/*	Version 1.0 */
#endif

	/*	Config Method */
	/*	Type: */
	/* *(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_CONF_METHOD ); */
	RTW_PUT_BE16(wpsie + wpsielen, WPS_ATTR_CONF_METHOD);
	wpsielen += 2;

	/*	Length: */
	/* *(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 ); */
	RTW_PUT_BE16(wpsie + wpsielen, 0x0002);
	wpsielen += 2;

	/*	Value: */
	/* *(u16*) ( wpsie + wpsielen ) = cpu_to_be16( config_method ); */
	RTW_PUT_BE16(wpsie + wpsielen, config_method);
	wpsielen += 2;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen);

#ifdef CONFIG_WFD
	wfdielen = build_provdisc_resp_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}

static void issue_p2p_presence_resp(struct wifidirect_info *pwdinfo, u8 *da, u8 status, u8 dialogToken)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	_adapter *padapter = pwdinfo->padapter;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	unsigned char category = RTW_WLAN_CATEGORY_P2P;/* P2P action frame	 */
	u32	p2poui = cpu_to_be32(P2POUI);
	u8	oui_subtype = P2P_PRESENCE_RESPONSE;
	u8 p2pie[MAX_P2P_IE_LEN] = { 0x00 };
	u8 noa_attr_content[32] = { 0x00 };
	u32 p2pielen = 0;

	RTW_INFO("[%s]\n", __FUNCTION__);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, pwdinfo->interface_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, pwdinfo->interface_addr, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	/* Build P2P action frame header */
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));


	/* Add P2P IE header */
	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/* Add Status attribute in P2P IE */
	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_STATUS, 1, &status);

	/* Add NoA attribute in P2P IE */
	noa_attr_content[0] = 0x1;/* index */
	noa_attr_content[1] = 0x0;/* CTWindow and OppPS Parameters */

	/* todo: Notice of Absence Descriptor(s) */

	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_NOA, 2, noa_attr_content);



	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, p2pie, &(pattrib->pktlen));


	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

}

u32 build_beacon_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	u8 p2pie[MAX_P2P_IE_LEN] = { 0x00 };
	u16 capability = 0;
	u32 len = 0, p2pielen = 0;


	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */


	/*	According to the P2P Specification, the beacon frame should contain 3 P2P attributes */
	/*	1. P2P Capability */
	/*	2. P2P Device ID */
	/*	3. Notice of Absence ( NOA )	 */

	/*	P2P Capability ATTR */
	/*	Type: */
	/*	Length: */
	/*	Value: */
	/*	Device Capability Bitmap, 1 byte */
	/*	Be able to participate in additional P2P Groups and */
	/*	support the P2P Invitation Procedure	 */
	/*	Group Capability Bitmap, 1 byte	 */
	capability = P2P_DEVCAP_INVITATION_PROC | P2P_DEVCAP_CLIENT_DISCOVERABILITY;
	capability |= ((P2P_GRPCAP_GO | P2P_GRPCAP_INTRABSS) << 8);
	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_PROVISIONING_ING))
		capability |= (P2P_GRPCAP_GROUP_FORMATION << 8);

	capability = cpu_to_le16(capability);

	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_CAPABILITY, 2, (u8 *)&capability);


	/* P2P Device ID ATTR */
	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_DEVICE_ID, ETH_ALEN, pwdinfo->device_addr);


	/* Notice of Absence ATTR */
	/*	Type:  */
	/*	Length: */
	/*	Value: */

	/* go_add_noa_attr(pwdinfo); */


	pbuf = rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &len);


	return len;

}

#ifdef CONFIG_WFD
u32 build_beacon_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	u8 wfdie[MAX_WFD_IE_LEN] = { 0x00 };
	u16 val16 = 0;
	u32 len = 0, wfdielen = 0;
	_adapter *padapter = pwdinfo->padapter;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wifi_display_info	*pwfd_info = padapter->wdinfo.wfd_info;

	if (!hal_chk_wl_func(padapter, WL_FUNC_MIRACAST))
		goto exit;

	/*	WFD OUI */
	wfdielen = 0;
	wfdie[wfdielen++] = 0x50;
	wfdie[wfdielen++] = 0x6F;
	wfdie[wfdielen++] = 0x9A;
	wfdie[wfdielen++] = 0x0A;	/*	WFA WFD v1.0 */

	/*	Commented by Albert 20110812 */
	/*	According to the WFD Specification, the beacon frame should contain 4 WFD attributes */
	/*	1. WFD Device Information */
	/*	2. Associated BSSID */
	/*	3. Coupled Sink Information */


	/*	WFD Device Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value1: */
	/*	WFD device information */

	if (P2P_ROLE_GO == pwdinfo->role) {
		if (is_any_client_associated(pwdinfo->padapter)) {
			/*	WFD primary sink + WiFi Direct mode + WSD (WFD Service Discovery) */
			val16 = pwfd_info->wfd_device_type | WFD_DEVINFO_WSD;
			RTW_PUT_BE16(wfdie + wfdielen, val16);
		} else {
			/*	WFD primary sink + available for WFD session + WiFi Direct mode + WSD (WFD Service Discovery) */
			val16 = pwfd_info->wfd_device_type | WFD_DEVINFO_SESSION_AVAIL | WFD_DEVINFO_WSD;
			RTW_PUT_BE16(wfdie + wfdielen, val16);
		}

	} else {
		/*	WFD primary sink + available for WFD session + WiFi Direct mode + WSD ( WFD Service Discovery ) */
		val16 = pwfd_info->wfd_device_type | WFD_DEVINFO_SESSION_AVAIL | WFD_DEVINFO_WSD;
		RTW_PUT_BE16(wfdie + wfdielen, val16);
	}

	wfdielen += 2;

	/*	Value2: */
	/*	Session Management Control Port */
	/*	Default TCP port for RTSP messages is 554 */
	RTW_PUT_BE16(wfdie + wfdielen, pwfd_info->rtsp_ctrlport);
	wfdielen += 2;

	/*	Value3: */
	/*	WFD Device Maximum Throughput */
	/*	300Mbps is the maximum throughput */
	RTW_PUT_BE16(wfdie + wfdielen, 300);
	wfdielen += 2;

	/*	Associated BSSID ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_ASSOC_BSSID;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value: */
	/*	Associated BSSID */
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		_rtw_memcpy(wfdie + wfdielen, &pmlmepriv->assoc_bssid[0], ETH_ALEN);
	else
		_rtw_memset(wfdie + wfdielen, 0x00, ETH_ALEN);

	wfdielen += ETH_ALEN;

	/*	Coupled Sink Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_COUPLED_SINK_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0007);
	wfdielen += 2;

	/*	Value: */
	/*	Coupled Sink Status bitmap */
	/*	Not coupled/available for Coupling */
	wfdie[wfdielen++] = 0;
	/* MAC Addr. */
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;

	rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, wfdielen, (unsigned char *) wfdie, &len);

exit:
	return len;
}

u32 build_probe_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	u8 wfdie[MAX_WFD_IE_LEN] = { 0x00 };
	u16 val16 = 0;
	u32 len = 0, wfdielen = 0;
	_adapter *padapter = pwdinfo->padapter;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wifi_display_info	*pwfd_info = padapter->wdinfo.wfd_info;

	if (!hal_chk_wl_func(padapter, WL_FUNC_MIRACAST))
		goto exit;

	/*	WFD OUI */
	wfdielen = 0;
	wfdie[wfdielen++] = 0x50;
	wfdie[wfdielen++] = 0x6F;
	wfdie[wfdielen++] = 0x9A;
	wfdie[wfdielen++] = 0x0A;	/*	WFA WFD v1.0 */

	/*	Commented by Albert 20110812 */
	/*	According to the WFD Specification, the probe request frame should contain 4 WFD attributes */
	/*	1. WFD Device Information */
	/*	2. Associated BSSID */
	/*	3. Coupled Sink Information */


	/*	WFD Device Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value1: */
	/*	WFD device information */

	if (1 == pwdinfo->wfd_tdls_enable) {
		/*	WFD primary sink + available for WFD session + WiFi TDLS mode + WSC ( WFD Service Discovery )	 */
		val16 = pwfd_info->wfd_device_type |
			WFD_DEVINFO_SESSION_AVAIL |
			WFD_DEVINFO_WSD |
			WFD_DEVINFO_PC_TDLS;
		RTW_PUT_BE16(wfdie + wfdielen, val16);
	} else {
		/*	WFD primary sink + available for WFD session + WiFi Direct mode + WSC ( WFD Service Discovery )	 */
		val16 = pwfd_info->wfd_device_type |
			WFD_DEVINFO_SESSION_AVAIL |
			WFD_DEVINFO_WSD;
		RTW_PUT_BE16(wfdie + wfdielen, val16);
	}

	wfdielen += 2;

	/*	Value2: */
	/*	Session Management Control Port */
	/*	Default TCP port for RTSP messages is 554 */
	RTW_PUT_BE16(wfdie + wfdielen, pwfd_info->rtsp_ctrlport);
	wfdielen += 2;

	/*	Value3: */
	/*	WFD Device Maximum Throughput */
	/*	300Mbps is the maximum throughput */
	RTW_PUT_BE16(wfdie + wfdielen, 300);
	wfdielen += 2;

	/*	Associated BSSID ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_ASSOC_BSSID;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value: */
	/*	Associated BSSID */
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		_rtw_memcpy(wfdie + wfdielen, &pmlmepriv->assoc_bssid[0], ETH_ALEN);
	else
		_rtw_memset(wfdie + wfdielen, 0x00, ETH_ALEN);

	wfdielen += ETH_ALEN;

	/*	Coupled Sink Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_COUPLED_SINK_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0007);
	wfdielen += 2;

	/*	Value: */
	/*	Coupled Sink Status bitmap */
	/*	Not coupled/available for Coupling */
	wfdie[wfdielen++] = 0;
	/* MAC Addr. */
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;

	rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, wfdielen, (unsigned char *) wfdie, &len);

exit:
	return len;
}

u32 build_probe_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf, u8 tunneled)
{
	u8 wfdie[MAX_WFD_IE_LEN] = { 0x00 };
	u32 len = 0, wfdielen = 0;
	_adapter *padapter = pwdinfo->padapter;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wifi_display_info	*pwfd_info = padapter->wdinfo.wfd_info;
	u16 v16 = 0;

	if (!hal_chk_wl_func(padapter, WL_FUNC_MIRACAST))
		goto exit;

	/*	WFD OUI */
	wfdielen = 0;
	wfdie[wfdielen++] = 0x50;
	wfdie[wfdielen++] = 0x6F;
	wfdie[wfdielen++] = 0x9A;
	wfdie[wfdielen++] = 0x0A;	/*	WFA WFD v1.0 */

	/*	Commented by Albert 20110812 */
	/*	According to the WFD Specification, the probe response frame should contain 4 WFD attributes */
	/*	1. WFD Device Information */
	/*	2. Associated BSSID */
	/*	3. Coupled Sink Information */
	/*	4. WFD Session Information */


	/*	WFD Device Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value1: */
	/*	WFD device information */
	/*	WFD primary sink + available for WFD session + WiFi Direct mode */

	if (_TRUE == pwdinfo->session_available) {
		if (P2P_ROLE_GO == pwdinfo->role) {
			if (is_any_client_associated(pwdinfo->padapter)) {
				if (pwdinfo->wfd_tdls_enable) {
					/*	TDLS mode + WSD ( WFD Service Discovery ) */
					v16 = pwfd_info->wfd_device_type | WFD_DEVINFO_WSD | WFD_DEVINFO_PC_TDLS | WFD_DEVINFO_HDCP_SUPPORT;
					RTW_PUT_BE16(wfdie + wfdielen, v16);
				} else {
					/*	WiFi Direct mode + WSD ( WFD Service Discovery ) */
					v16 =  pwfd_info->wfd_device_type | WFD_DEVINFO_WSD | WFD_DEVINFO_HDCP_SUPPORT;
					RTW_PUT_BE16(wfdie + wfdielen, v16);
				}
			} else {
				if (pwdinfo->wfd_tdls_enable) {
					/*	available for WFD session + TDLS mode + WSD ( WFD Service Discovery ) */
					v16 = pwfd_info->wfd_device_type | WFD_DEVINFO_SESSION_AVAIL | WFD_DEVINFO_WSD | WFD_DEVINFO_PC_TDLS | WFD_DEVINFO_HDCP_SUPPORT;
					RTW_PUT_BE16(wfdie + wfdielen, v16);
				} else {
					/*	available for WFD session + WiFi Direct mode + WSD ( WFD Service Discovery ) */
					v16 = pwfd_info->wfd_device_type | WFD_DEVINFO_SESSION_AVAIL | WFD_DEVINFO_WSD | WFD_DEVINFO_HDCP_SUPPORT;
					RTW_PUT_BE16(wfdie + wfdielen, v16);
				}
			}
		} else {
			if (pwdinfo->wfd_tdls_enable) {
				/*	available for WFD session + WiFi Direct mode + WSD ( WFD Service Discovery ) */
				v16 = pwfd_info->wfd_device_type | WFD_DEVINFO_SESSION_AVAIL | WFD_DEVINFO_WSD | WFD_DEVINFO_PC_TDLS | WFD_DEVINFO_HDCP_SUPPORT;
				RTW_PUT_BE16(wfdie + wfdielen, v16);
			} else {
				/*	available for WFD session + WiFi Direct mode + WSD ( WFD Service Discovery ) */
				v16 =  pwfd_info->wfd_device_type | WFD_DEVINFO_SESSION_AVAIL | WFD_DEVINFO_WSD | WFD_DEVINFO_HDCP_SUPPORT;
				RTW_PUT_BE16(wfdie + wfdielen, v16);
			}
		}
	} else {
		if (pwdinfo->wfd_tdls_enable) {
			v16 = pwfd_info->wfd_device_type | WFD_DEVINFO_WSD | WFD_DEVINFO_PC_TDLS | WFD_DEVINFO_HDCP_SUPPORT;
			RTW_PUT_BE16(wfdie + wfdielen, v16);
		} else {
			v16 =  pwfd_info->wfd_device_type | WFD_DEVINFO_WSD | WFD_DEVINFO_HDCP_SUPPORT;
			RTW_PUT_BE16(wfdie + wfdielen, v16);
		}
	}

	wfdielen += 2;

	/*	Value2: */
	/*	Session Management Control Port */
	/*	Default TCP port for RTSP messages is 554 */
	RTW_PUT_BE16(wfdie + wfdielen, pwfd_info->rtsp_ctrlport);
	wfdielen += 2;

	/*	Value3: */
	/*	WFD Device Maximum Throughput */
	/*	300Mbps is the maximum throughput */
	RTW_PUT_BE16(wfdie + wfdielen, 300);
	wfdielen += 2;

	/*	Associated BSSID ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_ASSOC_BSSID;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value: */
	/*	Associated BSSID */
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		_rtw_memcpy(wfdie + wfdielen, &pmlmepriv->assoc_bssid[0], ETH_ALEN);
	else
		_rtw_memset(wfdie + wfdielen, 0x00, ETH_ALEN);

	wfdielen += ETH_ALEN;

	/*	Coupled Sink Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_COUPLED_SINK_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0007);
	wfdielen += 2;

	/*	Value: */
	/*	Coupled Sink Status bitmap */
	/*	Not coupled/available for Coupling */
	wfdie[wfdielen++] = 0;
	/* MAC Addr. */
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;

	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
		/*	WFD Session Information ATTR */
		/*	Type: */
		wfdie[wfdielen++] = WFD_ATTR_SESSION_INFO;

		/*	Length: */
		/*	Note: In the WFD specification, the size of length field is 2. */
		RTW_PUT_BE16(wfdie + wfdielen, 0x0000);
		wfdielen += 2;

		/*	Todo: to add the list of WFD device info descriptor in WFD group. */

	}
#ifdef CONFIG_CONCURRENT_MODE
#ifdef CONFIG_TDLS
	{
		int i;
		_adapter *iface = NULL;
		struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if ((iface) && rtw_is_adapter_up(iface)) {
				if (iface == padapter)
					continue;

				if ((tunneled == 0) && (iface->wdinfo.wfd_tdls_enable == 1)) {
					/*	Alternative MAC Address ATTR
						Type:					*/
					wfdie[wfdielen++] = WFD_ATTR_ALTER_MAC;

					/*	Length:
						Note: In the WFD specification, the size of length field is 2.*/
					RTW_PUT_BE16(wfdie + wfdielen,  ETH_ALEN);
					wfdielen += 2;

					/*	Value:
						Alternative MAC Address*/
					_rtw_memcpy(wfdie + wfdielen, adapter_mac_addr(iface), ETH_ALEN);
					wfdielen += ETH_ALEN;
				}
			}
		}
	}

#endif /* CONFIG_TDLS*/
#endif /* CONFIG_CONCURRENT_MODE */

	pbuf = rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, wfdielen, (unsigned char *) wfdie, &len);

exit:
	return len;
}

u32 build_assoc_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	u8 wfdie[MAX_WFD_IE_LEN] = { 0x00 };
	u16 val16 = 0;
	u32 len = 0, wfdielen = 0;
	_adapter					*padapter = NULL;
	struct mlme_priv			*pmlmepriv = NULL;
	struct wifi_display_info		*pwfd_info = NULL;

	padapter = pwdinfo->padapter;
	pmlmepriv = &padapter->mlmepriv;
	pwfd_info = padapter->wdinfo.wfd_info;

	if (!hal_chk_wl_func(padapter, WL_FUNC_MIRACAST))
		goto exit;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) || rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE))
		goto exit;

	/* WFD OUI */
	wfdielen = 0;
	wfdie[wfdielen++] = 0x50;
	wfdie[wfdielen++] = 0x6F;
	wfdie[wfdielen++] = 0x9A;
	wfdie[wfdielen++] = 0x0A;	/*	WFA WFD v1.0 */

	/*	Commented by Albert 20110812 */
	/*	According to the WFD Specification, the probe request frame should contain 4 WFD attributes */
	/*	1. WFD Device Information */
	/*	2. Associated BSSID */
	/*	3. Coupled Sink Information */


	/*	WFD Device Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value1: */
	/*	WFD device information */
	/*	WFD primary sink + available for WFD session + WiFi Direct mode + WSD ( WFD Service Discovery ) */
	val16 = pwfd_info->wfd_device_type | WFD_DEVINFO_SESSION_AVAIL | WFD_DEVINFO_WSD;
	RTW_PUT_BE16(wfdie + wfdielen, val16);
	wfdielen += 2;

	/*	Value2: */
	/*	Session Management Control Port */
	/*	Default TCP port for RTSP messages is 554 */
	RTW_PUT_BE16(wfdie + wfdielen, pwfd_info->rtsp_ctrlport);
	wfdielen += 2;

	/*	Value3: */
	/*	WFD Device Maximum Throughput */
	/*	300Mbps is the maximum throughput */
	RTW_PUT_BE16(wfdie + wfdielen, 300);
	wfdielen += 2;

	/*	Associated BSSID ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_ASSOC_BSSID;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value: */
	/*	Associated BSSID */
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		_rtw_memcpy(wfdie + wfdielen, &pmlmepriv->assoc_bssid[0], ETH_ALEN);
	else
		_rtw_memset(wfdie + wfdielen, 0x00, ETH_ALEN);

	wfdielen += ETH_ALEN;

	/*	Coupled Sink Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_COUPLED_SINK_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0007);
	wfdielen += 2;

	/*	Value: */
	/*	Coupled Sink Status bitmap */
	/*	Not coupled/available for Coupling */
	wfdie[wfdielen++] = 0;
	/* MAC Addr. */
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;

	rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, wfdielen, (unsigned char *) wfdie, &len);

exit:
	return len;
}

u32 build_assoc_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	u8 wfdie[MAX_WFD_IE_LEN] = { 0x00 };
	u32 len = 0, wfdielen = 0;
	u16 val16 = 0;
	_adapter *padapter = pwdinfo->padapter;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wifi_display_info	*pwfd_info = padapter->wdinfo.wfd_info;

	if (!hal_chk_wl_func(padapter, WL_FUNC_MIRACAST))
		goto exit;

	/*	WFD OUI */
	wfdielen = 0;
	wfdie[wfdielen++] = 0x50;
	wfdie[wfdielen++] = 0x6F;
	wfdie[wfdielen++] = 0x9A;
	wfdie[wfdielen++] = 0x0A;	/*	WFA WFD v1.0 */

	/*	Commented by Albert 20110812 */
	/*	According to the WFD Specification, the probe request frame should contain 4 WFD attributes */
	/*	1. WFD Device Information */
	/*	2. Associated BSSID */
	/*	3. Coupled Sink Information */


	/*	WFD Device Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value1: */
	/*	WFD device information */
	/*	WFD primary sink + available for WFD session + WiFi Direct mode + WSD ( WFD Service Discovery ) */
	val16 = pwfd_info->wfd_device_type | WFD_DEVINFO_SESSION_AVAIL | WFD_DEVINFO_WSD;
	RTW_PUT_BE16(wfdie + wfdielen, val16);
	wfdielen += 2;

	/*	Value2: */
	/*	Session Management Control Port */
	/*	Default TCP port for RTSP messages is 554 */
	RTW_PUT_BE16(wfdie + wfdielen, pwfd_info->rtsp_ctrlport);
	wfdielen += 2;

	/*	Value3: */
	/*	WFD Device Maximum Throughput */
	/*	300Mbps is the maximum throughput */
	RTW_PUT_BE16(wfdie + wfdielen, 300);
	wfdielen += 2;

	/*	Associated BSSID ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_ASSOC_BSSID;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value: */
	/*	Associated BSSID */
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		_rtw_memcpy(wfdie + wfdielen, &pmlmepriv->assoc_bssid[0], ETH_ALEN);
	else
		_rtw_memset(wfdie + wfdielen, 0x00, ETH_ALEN);

	wfdielen += ETH_ALEN;

	/*	Coupled Sink Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_COUPLED_SINK_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0007);
	wfdielen += 2;

	/*	Value: */
	/*	Coupled Sink Status bitmap */
	/*	Not coupled/available for Coupling */
	wfdie[wfdielen++] = 0;
	/* MAC Addr. */
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;

	rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, wfdielen, (unsigned char *) wfdie, &len);

exit:
	return len;
}

u32 build_nego_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	u8 wfdie[MAX_WFD_IE_LEN] = { 0x00 };
	u32 len = 0, wfdielen = 0;
	u16 val16 = 0;
	_adapter *padapter = pwdinfo->padapter;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wifi_display_info	*pwfd_info = padapter->wdinfo.wfd_info;

	if (!hal_chk_wl_func(padapter, WL_FUNC_MIRACAST))
		goto exit;

	/*	WFD OUI */
	wfdielen = 0;
	wfdie[wfdielen++] = 0x50;
	wfdie[wfdielen++] = 0x6F;
	wfdie[wfdielen++] = 0x9A;
	wfdie[wfdielen++] = 0x0A;	/*	WFA WFD v1.0 */

	/*	Commented by Albert 20110825 */
	/*	According to the WFD Specification, the negotiation request frame should contain 3 WFD attributes */
	/*	1. WFD Device Information */
	/*	2. Associated BSSID ( Optional ) */
	/*	3. Local IP Adress ( Optional ) */


	/*	WFD Device Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value1: */
	/*	WFD device information */
	/*	WFD primary sink + WiFi Direct mode + WSD ( WFD Service Discovery ) + WFD Session Available */
	val16 = pwfd_info->wfd_device_type | WFD_DEVINFO_WSD | WFD_DEVINFO_SESSION_AVAIL;
	RTW_PUT_BE16(wfdie + wfdielen, val16);
	wfdielen += 2;

	/*	Value2: */
	/*	Session Management Control Port */
	/*	Default TCP port for RTSP messages is 554 */
	RTW_PUT_BE16(wfdie + wfdielen, pwfd_info->rtsp_ctrlport);
	wfdielen += 2;

	/*	Value3: */
	/*	WFD Device Maximum Throughput */
	/*	300Mbps is the maximum throughput */
	RTW_PUT_BE16(wfdie + wfdielen, 300);
	wfdielen += 2;

	/*	Associated BSSID ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_ASSOC_BSSID;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value: */
	/*	Associated BSSID */
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		_rtw_memcpy(wfdie + wfdielen, &pmlmepriv->assoc_bssid[0], ETH_ALEN);
	else
		_rtw_memset(wfdie + wfdielen, 0x00, ETH_ALEN);

	wfdielen += ETH_ALEN;

	/*	Coupled Sink Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_COUPLED_SINK_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0007);
	wfdielen += 2;

	/*	Value: */
	/*	Coupled Sink Status bitmap */
	/*	Not coupled/available for Coupling */
	wfdie[wfdielen++] = 0;
	/* MAC Addr. */
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;

	rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, wfdielen, (unsigned char *) wfdie, &len);

exit:
	return len;
}

u32 build_nego_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	u8 wfdie[MAX_WFD_IE_LEN] = { 0x00 };
	u32 len = 0, wfdielen = 0;
	u16 val16 = 0;
	_adapter *padapter = pwdinfo->padapter;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wifi_display_info	*pwfd_info = padapter->wdinfo.wfd_info;

	if (!hal_chk_wl_func(padapter, WL_FUNC_MIRACAST))
		goto exit;

	/*	WFD OUI */
	wfdielen = 0;
	wfdie[wfdielen++] = 0x50;
	wfdie[wfdielen++] = 0x6F;
	wfdie[wfdielen++] = 0x9A;
	wfdie[wfdielen++] = 0x0A;	/*	WFA WFD v1.0 */

	/*	Commented by Albert 20110825 */
	/*	According to the WFD Specification, the negotiation request frame should contain 3 WFD attributes */
	/*	1. WFD Device Information */
	/*	2. Associated BSSID ( Optional ) */
	/*	3. Local IP Adress ( Optional ) */


	/*	WFD Device Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value1: */
	/*	WFD device information */
	/*	WFD primary sink + WiFi Direct mode + WSD ( WFD Service Discovery ) + WFD Session Available */
	val16 = pwfd_info->wfd_device_type | WFD_DEVINFO_WSD | WFD_DEVINFO_SESSION_AVAIL;
	RTW_PUT_BE16(wfdie + wfdielen, val16);
	wfdielen += 2;

	/*	Value2: */
	/*	Session Management Control Port */
	/*	Default TCP port for RTSP messages is 554 */
	RTW_PUT_BE16(wfdie + wfdielen, pwfd_info->rtsp_ctrlport);
	wfdielen += 2;

	/*	Value3: */
	/*	WFD Device Maximum Throughput */
	/*	300Mbps is the maximum throughput */
	RTW_PUT_BE16(wfdie + wfdielen, 300);
	wfdielen += 2;

	/*	Associated BSSID ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_ASSOC_BSSID;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value: */
	/*	Associated BSSID */
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		_rtw_memcpy(wfdie + wfdielen, &pmlmepriv->assoc_bssid[0], ETH_ALEN);
	else
		_rtw_memset(wfdie + wfdielen, 0x00, ETH_ALEN);

	wfdielen += ETH_ALEN;

	/*	Coupled Sink Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_COUPLED_SINK_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0007);
	wfdielen += 2;

	/*	Value: */
	/*	Coupled Sink Status bitmap */
	/*	Not coupled/available for Coupling */
	wfdie[wfdielen++] = 0;
	/* MAC Addr. */
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;


	rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, wfdielen, (unsigned char *) wfdie, &len);

exit:
	return len;
}

u32 build_nego_confirm_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	u8 wfdie[MAX_WFD_IE_LEN] = { 0x00 };
	u32 len = 0, wfdielen = 0;
	u16 val16 = 0;
	_adapter *padapter = pwdinfo->padapter;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wifi_display_info	*pwfd_info = padapter->wdinfo.wfd_info;

	if (!hal_chk_wl_func(padapter, WL_FUNC_MIRACAST))
		goto exit;

	/*	WFD OUI */
	wfdielen = 0;
	wfdie[wfdielen++] = 0x50;
	wfdie[wfdielen++] = 0x6F;
	wfdie[wfdielen++] = 0x9A;
	wfdie[wfdielen++] = 0x0A;	/*	WFA WFD v1.0 */

	/*	Commented by Albert 20110825 */
	/*	According to the WFD Specification, the negotiation request frame should contain 3 WFD attributes */
	/*	1. WFD Device Information */
	/*	2. Associated BSSID ( Optional ) */
	/*	3. Local IP Adress ( Optional ) */


	/*	WFD Device Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value1: */
	/*	WFD device information */
	/*	WFD primary sink + WiFi Direct mode + WSD ( WFD Service Discovery ) + WFD Session Available */
	val16 = pwfd_info->wfd_device_type | WFD_DEVINFO_WSD | WFD_DEVINFO_SESSION_AVAIL;
	RTW_PUT_BE16(wfdie + wfdielen, val16);
	wfdielen += 2;

	/*	Value2: */
	/*	Session Management Control Port */
	/*	Default TCP port for RTSP messages is 554 */
	RTW_PUT_BE16(wfdie + wfdielen, pwfd_info->rtsp_ctrlport);
	wfdielen += 2;

	/*	Value3: */
	/*	WFD Device Maximum Throughput */
	/*	300Mbps is the maximum throughput */
	RTW_PUT_BE16(wfdie + wfdielen, 300);
	wfdielen += 2;

	/*	Associated BSSID ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_ASSOC_BSSID;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value: */
	/*	Associated BSSID */
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		_rtw_memcpy(wfdie + wfdielen, &pmlmepriv->assoc_bssid[0], ETH_ALEN);
	else
		_rtw_memset(wfdie + wfdielen, 0x00, ETH_ALEN);

	wfdielen += ETH_ALEN;

	/*	Coupled Sink Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_COUPLED_SINK_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0007);
	wfdielen += 2;

	/*	Value: */
	/*	Coupled Sink Status bitmap */
	/*	Not coupled/available for Coupling */
	wfdie[wfdielen++] = 0;
	/* MAC Addr. */
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;


	pbuf = rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, wfdielen, (unsigned char *) wfdie, &len);

exit:
	return len;
}

u32 build_invitation_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	u8 wfdie[MAX_WFD_IE_LEN] = { 0x00 };
	u32 len = 0, wfdielen = 0;
	u16 val16 = 0;
	_adapter *padapter = pwdinfo->padapter;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wifi_display_info	*pwfd_info = padapter->wdinfo.wfd_info;

	if (!hal_chk_wl_func(padapter, WL_FUNC_MIRACAST))
		goto exit;

	/*	WFD OUI */
	wfdielen = 0;
	wfdie[wfdielen++] = 0x50;
	wfdie[wfdielen++] = 0x6F;
	wfdie[wfdielen++] = 0x9A;
	wfdie[wfdielen++] = 0x0A;	/*	WFA WFD v1.0 */

	/*	Commented by Albert 20110825 */
	/*	According to the WFD Specification, the provision discovery request frame should contain 3 WFD attributes */
	/*	1. WFD Device Information */
	/*	2. Associated BSSID ( Optional ) */
	/*	3. Local IP Adress ( Optional ) */


	/*	WFD Device Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value1: */
	/*	WFD device information */
	/*	WFD primary sink + available for WFD session + WiFi Direct mode + WSD ( WFD Service Discovery ) */
	val16 = pwfd_info->wfd_device_type | WFD_DEVINFO_SESSION_AVAIL | WFD_DEVINFO_WSD;
	RTW_PUT_BE16(wfdie + wfdielen, val16);
	wfdielen += 2;

	/*	Value2: */
	/*	Session Management Control Port */
	/*	Default TCP port for RTSP messages is 554 */
	RTW_PUT_BE16(wfdie + wfdielen, pwfd_info->rtsp_ctrlport);
	wfdielen += 2;

	/*	Value3: */
	/*	WFD Device Maximum Throughput */
	/*	300Mbps is the maximum throughput */
	RTW_PUT_BE16(wfdie + wfdielen, 300);
	wfdielen += 2;

	/*	Associated BSSID ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_ASSOC_BSSID;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value: */
	/*	Associated BSSID */
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		_rtw_memcpy(wfdie + wfdielen, &pmlmepriv->assoc_bssid[0], ETH_ALEN);
	else
		_rtw_memset(wfdie + wfdielen, 0x00, ETH_ALEN);

	wfdielen += ETH_ALEN;

	/*	Coupled Sink Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_COUPLED_SINK_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0007);
	wfdielen += 2;

	/*	Value: */
	/*	Coupled Sink Status bitmap */
	/*	Not coupled/available for Coupling */
	wfdie[wfdielen++] = 0;
	/* MAC Addr. */
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;

	if (P2P_ROLE_GO == pwdinfo->role) {
		/*	WFD Session Information ATTR */
		/*	Type: */
		wfdie[wfdielen++] = WFD_ATTR_SESSION_INFO;

		/*	Length: */
		/*	Note: In the WFD specification, the size of length field is 2. */
		RTW_PUT_BE16(wfdie + wfdielen, 0x0000);
		wfdielen += 2;

		/*	Todo: to add the list of WFD device info descriptor in WFD group. */

	}

	rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, wfdielen, (unsigned char *) wfdie, &len);

exit:
	return len;
}

u32 build_invitation_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	u8 wfdie[MAX_WFD_IE_LEN] = { 0x00 };
	u16 val16 = 0;
	u32 len = 0, wfdielen = 0;
	_adapter *padapter = pwdinfo->padapter;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wifi_display_info	*pwfd_info = padapter->wdinfo.wfd_info;

	if (!hal_chk_wl_func(padapter, WL_FUNC_MIRACAST))
		goto exit;

	/*	WFD OUI */
	wfdielen = 0;
	wfdie[wfdielen++] = 0x50;
	wfdie[wfdielen++] = 0x6F;
	wfdie[wfdielen++] = 0x9A;
	wfdie[wfdielen++] = 0x0A;	/*	WFA WFD v1.0 */

	/*	Commented by Albert 20110825 */
	/*	According to the WFD Specification, the provision discovery request frame should contain 3 WFD attributes */
	/*	1. WFD Device Information */
	/*	2. Associated BSSID ( Optional ) */
	/*	3. Local IP Adress ( Optional ) */


	/*	WFD Device Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value1: */
	/*	WFD device information */
	/*	WFD primary sink + available for WFD session + WiFi Direct mode + WSD ( WFD Service Discovery ) */
	val16 = pwfd_info->wfd_device_type | WFD_DEVINFO_SESSION_AVAIL | WFD_DEVINFO_WSD;
	RTW_PUT_BE16(wfdie + wfdielen, val16);
	wfdielen += 2;

	/*	Value2: */
	/*	Session Management Control Port */
	/*	Default TCP port for RTSP messages is 554 */
	RTW_PUT_BE16(wfdie + wfdielen, pwfd_info->rtsp_ctrlport);
	wfdielen += 2;

	/*	Value3: */
	/*	WFD Device Maximum Throughput */
	/*	300Mbps is the maximum throughput */
	RTW_PUT_BE16(wfdie + wfdielen, 300);
	wfdielen += 2;

	/*	Associated BSSID ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_ASSOC_BSSID;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value: */
	/*	Associated BSSID */
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		_rtw_memcpy(wfdie + wfdielen, &pmlmepriv->assoc_bssid[0], ETH_ALEN);
	else
		_rtw_memset(wfdie + wfdielen, 0x00, ETH_ALEN);

	wfdielen += ETH_ALEN;

	/*	Coupled Sink Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_COUPLED_SINK_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0007);
	wfdielen += 2;

	/*	Value: */
	/*	Coupled Sink Status bitmap */
	/*	Not coupled/available for Coupling */
	wfdie[wfdielen++] = 0;
	/* MAC Addr. */
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;

	if (P2P_ROLE_GO == pwdinfo->role) {
		/*	WFD Session Information ATTR */
		/*	Type: */
		wfdie[wfdielen++] = WFD_ATTR_SESSION_INFO;

		/*	Length: */
		/*	Note: In the WFD specification, the size of length field is 2. */
		RTW_PUT_BE16(wfdie + wfdielen, 0x0000);
		wfdielen += 2;

		/*	Todo: to add the list of WFD device info descriptor in WFD group. */

	}

	rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, wfdielen, (unsigned char *) wfdie, &len);

exit:
	return len;
}

u32 build_provdisc_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	u8 wfdie[MAX_WFD_IE_LEN] = { 0x00 };
	u32 len = 0, wfdielen = 0;
	u16 val16 = 0;
	_adapter *padapter = pwdinfo->padapter;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wifi_display_info	*pwfd_info = padapter->wdinfo.wfd_info;

	if (!hal_chk_wl_func(padapter, WL_FUNC_MIRACAST))
		goto exit;

	/*	WFD OUI */
	wfdielen = 0;
	wfdie[wfdielen++] = 0x50;
	wfdie[wfdielen++] = 0x6F;
	wfdie[wfdielen++] = 0x9A;
	wfdie[wfdielen++] = 0x0A;	/*	WFA WFD v1.0 */

	/*	Commented by Albert 20110825 */
	/*	According to the WFD Specification, the provision discovery request frame should contain 3 WFD attributes */
	/*	1. WFD Device Information */
	/*	2. Associated BSSID ( Optional ) */
	/*	3. Local IP Adress ( Optional ) */


	/*	WFD Device Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value1: */
	/*	WFD device information */
	/*	WFD primary sink + available for WFD session + WiFi Direct mode + WSD ( WFD Service Discovery ) */
	val16 = pwfd_info->wfd_device_type | WFD_DEVINFO_SESSION_AVAIL | WFD_DEVINFO_WSD;
	RTW_PUT_BE16(wfdie + wfdielen, val16);
	wfdielen += 2;

	/*	Value2: */
	/*	Session Management Control Port */
	/*	Default TCP port for RTSP messages is 554 */
	RTW_PUT_BE16(wfdie + wfdielen, pwfd_info->rtsp_ctrlport);
	wfdielen += 2;

	/*	Value3: */
	/*	WFD Device Maximum Throughput */
	/*	300Mbps is the maximum throughput */
	RTW_PUT_BE16(wfdie + wfdielen, 300);
	wfdielen += 2;

	/*	Associated BSSID ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_ASSOC_BSSID;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value: */
	/*	Associated BSSID */
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		_rtw_memcpy(wfdie + wfdielen, &pmlmepriv->assoc_bssid[0], ETH_ALEN);
	else
		_rtw_memset(wfdie + wfdielen, 0x00, ETH_ALEN);

	wfdielen += ETH_ALEN;

	/*	Coupled Sink Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_COUPLED_SINK_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0007);
	wfdielen += 2;

	/*	Value: */
	/*	Coupled Sink Status bitmap */
	/*	Not coupled/available for Coupling */
	wfdie[wfdielen++] = 0;
	/* MAC Addr. */
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;


	rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, wfdielen, (unsigned char *) wfdie, &len);

exit:
	return len;
}

u32 build_provdisc_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	u8 wfdie[MAX_WFD_IE_LEN] = { 0x00 };
	u32 len = 0, wfdielen = 0;
	u16 val16 = 0;
	_adapter *padapter = pwdinfo->padapter;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wifi_display_info	*pwfd_info = padapter->wdinfo.wfd_info;

	if (!hal_chk_wl_func(padapter, WL_FUNC_MIRACAST))
		goto exit;

	/*	WFD OUI */
	wfdielen = 0;
	wfdie[wfdielen++] = 0x50;
	wfdie[wfdielen++] = 0x6F;
	wfdie[wfdielen++] = 0x9A;
	wfdie[wfdielen++] = 0x0A;	/*	WFA WFD v1.0 */

	/*	Commented by Albert 20110825 */
	/*	According to the WFD Specification, the provision discovery response frame should contain 3 WFD attributes */
	/*	1. WFD Device Information */
	/*	2. Associated BSSID ( Optional ) */
	/*	3. Local IP Adress ( Optional ) */


	/*	WFD Device Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value1: */
	/*	WFD device information */
	/*	WFD primary sink + available for WFD session + WiFi Direct mode + WSD ( WFD Service Discovery ) */
	val16 = pwfd_info->wfd_device_type | WFD_DEVINFO_SESSION_AVAIL | WFD_DEVINFO_WSD;
	RTW_PUT_BE16(wfdie + wfdielen, val16);
	wfdielen += 2;

	/*	Value2: */
	/*	Session Management Control Port */
	/*	Default TCP port for RTSP messages is 554 */
	RTW_PUT_BE16(wfdie + wfdielen, pwfd_info->rtsp_ctrlport);
	wfdielen += 2;

	/*	Value3: */
	/*	WFD Device Maximum Throughput */
	/*	300Mbps is the maximum throughput */
	RTW_PUT_BE16(wfdie + wfdielen, 300);
	wfdielen += 2;

	/*	Associated BSSID ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_ASSOC_BSSID;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/*	Value: */
	/*	Associated BSSID */
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		_rtw_memcpy(wfdie + wfdielen, &pmlmepriv->assoc_bssid[0], ETH_ALEN);
	else
		_rtw_memset(wfdie + wfdielen, 0x00, ETH_ALEN);

	wfdielen += ETH_ALEN;

	/*	Coupled Sink Information ATTR */
	/*	Type: */
	wfdie[wfdielen++] = WFD_ATTR_COUPLED_SINK_INFO;

	/*	Length: */
	/*	Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0007);
	wfdielen += 2;

	/*	Value: */
	/*	Coupled Sink Status bitmap */
	/*	Not coupled/available for Coupling */
	wfdie[wfdielen++] = 0;
	/* MAC Addr. */
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;
	wfdie[wfdielen++] = 0;

	rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, wfdielen, (unsigned char *) wfdie, &len);

exit:
	return len;
}
#endif /* CONFIG_WFD */

u32 build_probe_resp_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	u8 p2pie[MAX_P2P_IE_LEN] = { 0x00 };
	u32 len = 0, p2pielen = 0;

	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*	Commented by Albert 20100907 */
	/*	According to the P2P Specification, the probe response frame should contain 5 P2P attributes */
	/*	1. P2P Capability */
	/*	2. Extended Listen Timing */
	/*	3. Notice of Absence ( NOA )	( Only GO needs this ) */
	/*	4. Device Info */
	/*	5. Group Info	( Only GO need this ) */

	/*	P2P Capability ATTR */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CAPABILITY;

	/*	Length: */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 ); */
	RTW_PUT_LE16(p2pie + p2pielen, 0x0002);
	p2pielen += 2;

	/*	Value: */
	/*	Device Capability Bitmap, 1 byte */
	p2pie[p2pielen++] = DMP_P2P_DEVCAP_SUPPORT;

	/*	Group Capability Bitmap, 1 byte */
	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
		p2pie[p2pielen] = (P2P_GRPCAP_GO | P2P_GRPCAP_INTRABSS);

		if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_PROVISIONING_ING))
			p2pie[p2pielen] |= P2P_GRPCAP_GROUP_FORMATION;

		p2pielen++;
	} else if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE)) {
		/*	Group Capability Bitmap, 1 byte */
		if (pwdinfo->persistent_supported)
			p2pie[p2pielen++] = P2P_GRPCAP_PERSISTENT_GROUP | DMP_P2P_GRPCAP_SUPPORT;
		else
			p2pie[p2pielen++] = DMP_P2P_GRPCAP_SUPPORT;
	}

	/*	Extended Listen Timing ATTR */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_EX_LISTEN_TIMING;

	/*	Length: */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0004 ); */
	RTW_PUT_LE16(p2pie + p2pielen, 0x0004);
	p2pielen += 2;

	/*	Value: */
	/*	Availability Period */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF ); */
	RTW_PUT_LE16(p2pie + p2pielen, 0xFFFF);
	p2pielen += 2;

	/*	Availability Interval */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF ); */
	RTW_PUT_LE16(p2pie + p2pielen, 0xFFFF);
	p2pielen += 2;


	/* Notice of Absence ATTR */
	/*	Type:  */
	/*	Length: */
	/*	Value: */
	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
		/* go_add_noa_attr(pwdinfo); */
	}

	/*	Device Info ATTR */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	21->P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes)  */
	/*	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes) */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 21 + pwdinfo->device_name_len ); */
	RTW_PUT_LE16(p2pie + p2pielen, 21 + pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	/*	P2P Device Address */
	_rtw_memcpy(p2pie + p2pielen, pwdinfo->device_addr, ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	Config Method */
	/*	This field should be big endian. Noted by P2P specification. */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->supported_wps_cm ); */
	RTW_PUT_BE16(p2pie + p2pielen, pwdinfo->supported_wps_cm);
	p2pielen += 2;

	{
		/*	Primary Device Type */
		/*	Category ID */
		/* *(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_CID_MULIT_MEDIA ); */
		RTW_PUT_BE16(p2pie + p2pielen, WPS_PDT_CID_MULIT_MEDIA);
		p2pielen += 2;

		/*	OUI */
		/* *(u32*) ( p2pie + p2pielen ) = cpu_to_be32( WPSOUI ); */
		RTW_PUT_BE32(p2pie + p2pielen, WPSOUI);
		p2pielen += 4;

		/*	Sub Category ID */
		/* *(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_SCID_MEDIA_SERVER ); */
		RTW_PUT_BE16(p2pie + p2pielen, WPS_PDT_SCID_MEDIA_SERVER);
		p2pielen += 2;
	}

	/*	Number of Secondary Device Types */
	p2pie[p2pielen++] = 0x00;	/*	No Secondary Device Type List */

	/*	Device Name */
	/*	Type: */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME ); */
	RTW_PUT_BE16(p2pie + p2pielen, WPS_ATTR_DEVICE_NAME);
	p2pielen += 2;

	/*	Length: */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->device_name_len ); */
	RTW_PUT_BE16(p2pie + p2pielen, pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	_rtw_memcpy(p2pie + p2pielen, pwdinfo->device_name, pwdinfo->device_name_len);
	p2pielen += pwdinfo->device_name_len;

	/* Group Info ATTR */
	/*	Type: */
	/*	Length: */
	/*	Value: */
	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
		p2pielen += go_add_group_info_attr(pwdinfo, p2pie + p2pielen);


	pbuf = rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &len);


	return len;

}

u32 build_prov_disc_request_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf, u8 *pssid, u8 ussidlen, u8 *pdev_raddr)
{
	u8 p2pie[MAX_P2P_IE_LEN] = { 0x00 };
	u32 len = 0, p2pielen = 0;

	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*	Commented by Albert 20110301 */
	/*	According to the P2P Specification, the provision discovery request frame should contain 3 P2P attributes */
	/*	1. P2P Capability */
	/*	2. Device Info */
	/*	3. Group ID ( When joining an operating P2P Group ) */

	/*	P2P Capability ATTR */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CAPABILITY;

	/*	Length: */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 ); */
	RTW_PUT_LE16(p2pie + p2pielen, 0x0002);
	p2pielen += 2;

	/*	Value: */
	/*	Device Capability Bitmap, 1 byte */
	p2pie[p2pielen++] = DMP_P2P_DEVCAP_SUPPORT;

	/*	Group Capability Bitmap, 1 byte */
	if (pwdinfo->persistent_supported)
		p2pie[p2pielen++] = P2P_GRPCAP_PERSISTENT_GROUP | DMP_P2P_GRPCAP_SUPPORT;
	else
		p2pie[p2pielen++] = DMP_P2P_GRPCAP_SUPPORT;


	/*	Device Info ATTR */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	21->P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes)  */
	/*	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes) */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 21 + pwdinfo->device_name_len ); */
	RTW_PUT_LE16(p2pie + p2pielen, 21 + pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	/*	P2P Device Address */
	_rtw_memcpy(p2pie + p2pielen, pwdinfo->device_addr, ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	Config Method */
	/*	This field should be big endian. Noted by P2P specification. */
	if (pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_PBC) {
		/* *(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_CONFIG_METHOD_PBC ); */
		RTW_PUT_BE16(p2pie + p2pielen, WPS_CONFIG_METHOD_PBC);
	} else {
		/* *(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_CONFIG_METHOD_DISPLAY ); */
		RTW_PUT_BE16(p2pie + p2pielen, WPS_CONFIG_METHOD_DISPLAY);
	}

	p2pielen += 2;

	/*	Primary Device Type */
	/*	Category ID */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_CID_MULIT_MEDIA ); */
	RTW_PUT_BE16(p2pie + p2pielen, WPS_PDT_CID_MULIT_MEDIA);
	p2pielen += 2;

	/*	OUI */
	/* *(u32*) ( p2pie + p2pielen ) = cpu_to_be32( WPSOUI ); */
	RTW_PUT_BE32(p2pie + p2pielen, WPSOUI);
	p2pielen += 4;

	/*	Sub Category ID */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_SCID_MEDIA_SERVER ); */
	RTW_PUT_BE16(p2pie + p2pielen, WPS_PDT_SCID_MEDIA_SERVER);
	p2pielen += 2;

	/*	Number of Secondary Device Types */
	p2pie[p2pielen++] = 0x00;	/*	No Secondary Device Type List */

	/*	Device Name */
	/*	Type: */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME ); */
	RTW_PUT_BE16(p2pie + p2pielen, WPS_ATTR_DEVICE_NAME);
	p2pielen += 2;

	/*	Length: */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->device_name_len ); */
	RTW_PUT_BE16(p2pie + p2pielen, pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	_rtw_memcpy(p2pie + p2pielen, pwdinfo->device_name, pwdinfo->device_name_len);
	p2pielen += pwdinfo->device_name_len;

	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT)) {
		/*	Added by Albert 2011/05/19 */
		/*	In this case, the pdev_raddr is the device address of the group owner. */

		/*	P2P Group ID ATTR */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_GROUP_ID;

		/*	Length: */
		/* *(u16*) ( p2pie + p2pielen ) = cpu_to_le16( ETH_ALEN + ussidlen ); */
		RTW_PUT_LE16(p2pie + p2pielen, ETH_ALEN + ussidlen);
		p2pielen += 2;

		/*	Value: */
		_rtw_memcpy(p2pie + p2pielen, pdev_raddr, ETH_ALEN);
		p2pielen += ETH_ALEN;

		_rtw_memcpy(p2pie + p2pielen, pssid, ussidlen);
		p2pielen += ussidlen;

	}

	pbuf = rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &len);


	return len;

}


u32 build_assoc_resp_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf, u8 status_code)
{
	u8 p2pie[MAX_P2P_IE_LEN] = { 0x00 };
	u32 len = 0, p2pielen = 0;

	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/* According to the P2P Specification, the Association response frame should contain 2 P2P attributes */
	/*	1. Status */
	/*	2. Extended Listen Timing (optional) */


	/*	Status ATTR */
	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_STATUS, 1, &status_code);


	/* Extended Listen Timing ATTR */
	/*	Type: */
	/*	Length: */
	/*	Value: */


	pbuf = rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &len);

	return len;

}

u32 build_deauth_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	u32 len = 0;

	return len;
}

u32 process_probe_req_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pframe, uint len)
{
	u8 *p;
	u32 ret = _FALSE;
	u8 *p2pie;
	u32	p2pielen = 0;
	int ssid_len = 0, rate_cnt = 0;

	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_, _SUPPORTEDRATES_IE_, (int *)&rate_cnt,
		       len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_);

	if (rate_cnt <= 4) {
		int i, g_rate = 0;

		for (i = 0; i < rate_cnt; i++) {
			if (((*(p + 2 + i) & 0xff) != 0x02) &&
			    ((*(p + 2 + i) & 0xff) != 0x04) &&
			    ((*(p + 2 + i) & 0xff) != 0x0B) &&
			    ((*(p + 2 + i) & 0xff) != 0x16))
				g_rate = 1;
		}

		if (g_rate == 0) {
			/*	There is no OFDM rate included in SupportedRates IE of this probe request frame */
			/*	The driver should response this probe request. */
			return ret;
		}
	} else {
		/*	rate_cnt > 4 means the SupportRates IE contains the OFDM rate because the count of CCK rates are 4. */
		/*	We should proceed the following check for this probe request. */
	}

	/*	Added comments by Albert 20100906 */
	/*	There are several items we should check here. */
	/*	1. This probe request frame must contain the P2P IE. (Done) */
	/*	2. This probe request frame must contain the wildcard SSID. (Done) */
	/*	3. Wildcard BSSID. (Todo) */
	/*	4. Destination Address. ( Done in mgt_dispatcher function ) */
	/*	5. Requested Device Type in WSC IE. (Todo) */
	/*	6. Device ID attribute in P2P IE. (Todo) */

	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_, _SSID_IE_, (int *)&ssid_len,
		       len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_);

	ssid_len &= 0xff;	/*	Just last 1 byte is valid for ssid len of the probe request */
	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE) || rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
		p2pie = rtw_get_p2p_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_ , len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_ , NULL, &p2pielen);
		if (p2pie) {
			if ((p != NULL) && _rtw_memcmp((void *)(p + 2), (void *) pwdinfo->p2p_wildcard_ssid , 7)) {
				/* todo: */
				/* Check Requested Device Type attributes in WSC IE. */
				/* Check Device ID attribute in P2P IE */

				ret = _TRUE;
			} else if ((p != NULL) && (ssid_len == 0))
				ret = _TRUE;
		} else {
			/* non -p2p device */
		}

	}


	return ret;

}

u32 process_assoc_req_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pframe, uint len, struct sta_info *psta)
{
	u8 status_code = P2P_STATUS_SUCCESS;
	u8 *pbuf, *pattr_content = NULL;
	u32 attr_contentlen = 0;
	u16 cap_attr = 0;
	unsigned short	frame_type, ie_offset = 0;
	u8 *ies;
	u32 ies_len;
	u8 *p2p_ie;
	u32	p2p_ielen = 0;

	if (!rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
		return P2P_STATUS_FAIL_REQUEST_UNABLE;

	frame_type = get_frame_sub_type(pframe);
	if (frame_type == WIFI_ASSOCREQ)
		ie_offset = _ASOCREQ_IE_OFFSET_;
	else /* WIFI_REASSOCREQ */
		ie_offset = _REASOCREQ_IE_OFFSET_;

	ies = pframe + WLAN_HDR_A3_LEN + ie_offset;
	ies_len = len - WLAN_HDR_A3_LEN - ie_offset;

	p2p_ie = rtw_get_p2p_ie(ies , ies_len , NULL, &p2p_ielen);

	if (!p2p_ie) {
		RTW_INFO("[%s] P2P IE not Found!!\n", __FUNCTION__);
		status_code =  P2P_STATUS_FAIL_INVALID_PARAM;
	} else
		RTW_INFO("[%s] P2P IE Found!!\n", __FUNCTION__);

	while (p2p_ie) {
		/* Check P2P Capability ATTR */
		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CAPABILITY, (u8 *)&cap_attr, (uint *) &attr_contentlen)) {
			RTW_INFO("[%s] Got P2P Capability Attr!!\n", __FUNCTION__);
			cap_attr = le16_to_cpu(cap_attr);
			psta->dev_cap = cap_attr & 0xff;
		}

		/* Check Extended Listen Timing ATTR */


		/* Check P2P Device Info ATTR */
		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_DEVICE_INFO, NULL, (uint *)&attr_contentlen)) {
			RTW_INFO("[%s] Got P2P DEVICE INFO Attr!!\n", __FUNCTION__);
			pattr_content = pbuf = rtw_zmalloc(attr_contentlen);
			if (pattr_content) {
				u8 num_of_secdev_type;
				u16 dev_name_len;


				rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_DEVICE_INFO , pattr_content, (uint *)&attr_contentlen);

				_rtw_memcpy(psta->dev_addr, 	pattr_content, ETH_ALEN);/* P2P Device Address */

				pattr_content += ETH_ALEN;

				_rtw_memcpy(&psta->config_methods, pattr_content, 2);/* Config Methods */
				psta->config_methods = be16_to_cpu(psta->config_methods);

				pattr_content += 2;

				_rtw_memcpy(psta->primary_dev_type, pattr_content, 8);

				pattr_content += 8;

				num_of_secdev_type = *pattr_content;
				pattr_content += 1;

				if (num_of_secdev_type == 0)
					psta->num_of_secdev_type = 0;
				else {
					u32 len;

					psta->num_of_secdev_type = num_of_secdev_type;

					len = (sizeof(psta->secdev_types_list) < (num_of_secdev_type * 8)) ? (sizeof(psta->secdev_types_list)) : (num_of_secdev_type * 8);

					_rtw_memcpy(psta->secdev_types_list, pattr_content, len);

					pattr_content += (num_of_secdev_type * 8);
				}


				/* dev_name_len = attr_contentlen - ETH_ALEN - 2 - 8 - 1 - (num_of_secdev_type*8); */
				psta->dev_name_len = 0;
				if (WPS_ATTR_DEVICE_NAME == be16_to_cpu(*(u16 *)pattr_content)) {
					dev_name_len = be16_to_cpu(*(u16 *)(pattr_content + 2));

					psta->dev_name_len = (sizeof(psta->dev_name) < dev_name_len) ? sizeof(psta->dev_name) : dev_name_len;

					_rtw_memcpy(psta->dev_name, pattr_content + 4, psta->dev_name_len);
				}

				rtw_mfree(pbuf, attr_contentlen);

			}

		}

		/* Get the next P2P IE */
		p2p_ie = rtw_get_p2p_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen), NULL, &p2p_ielen);

	}

	return status_code;

}

u32 process_p2p_devdisc_req(struct wifidirect_info *pwdinfo, u8 *pframe, uint len)
{
	u8 *frame_body;
	u8 status, dialogToken;
	struct sta_info *psta = NULL;
	_adapter *padapter = pwdinfo->padapter;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *p2p_ie;
	u32	p2p_ielen = 0;

	frame_body = (unsigned char *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));

	dialogToken = frame_body[7];
	status = P2P_STATUS_FAIL_UNKNOWN_P2PGROUP;

	p2p_ie = rtw_get_p2p_ie(frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &p2p_ielen);
	if (p2p_ie) {
		u8 groupid[38] = { 0x00 };
		u8 dev_addr[ETH_ALEN] = { 0x00 };
		u32	attr_contentlen = 0;

		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GROUP_ID, groupid, &attr_contentlen)) {
			if (_rtw_memcmp(pwdinfo->device_addr, groupid, ETH_ALEN) &&
			    _rtw_memcmp(pwdinfo->p2p_group_ssid, groupid + ETH_ALEN, pwdinfo->p2p_group_ssid_len)) {
				attr_contentlen = 0;
				if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_DEVICE_ID, dev_addr, &attr_contentlen)) {
					_irqL irqL;
					_list	*phead, *plist;

					_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
					phead = &pstapriv->asoc_list;
					plist = get_next(phead);

					/* look up sta asoc_queue */
					while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
						psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);

						plist = get_next(plist);

						if (psta->is_p2p_device && (psta->dev_cap & P2P_DEVCAP_CLIENT_DISCOVERABILITY) &&
						    _rtw_memcmp(psta->dev_addr, dev_addr, ETH_ALEN)) {

							/* _exit_critical_bh(&pstapriv->asoc_list_lock, &irqL); */
							/* issue GO Discoverability Request */
							issue_group_disc_req(pwdinfo, psta->cmn.mac_addr);
							/* _enter_critical_bh(&pstapriv->asoc_list_lock, &irqL); */

							status = P2P_STATUS_SUCCESS;

							break;
						} else
							status = P2P_STATUS_FAIL_INFO_UNAVAILABLE;

					}
					_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

				} else
					status = P2P_STATUS_FAIL_INVALID_PARAM;

			} else
				status = P2P_STATUS_FAIL_INVALID_PARAM;

		}

	}


	/* issue Device Discoverability Response */
	issue_p2p_devdisc_resp(pwdinfo, get_addr2_ptr(pframe), status, dialogToken);


	return (status == P2P_STATUS_SUCCESS) ? _TRUE : _FALSE;

}

u32 process_p2p_devdisc_resp(struct wifidirect_info *pwdinfo, u8 *pframe, uint len)
{
	return _TRUE;
}

u8 process_p2p_provdisc_req(struct wifidirect_info *pwdinfo,  u8 *pframe, uint len)
{
	u8 *frame_body;
	u8 *wpsie;
	uint	wps_ielen = 0, attr_contentlen = 0;
	u16	uconfig_method = 0;


	frame_body = (pframe + sizeof(struct rtw_ieee80211_hdr_3addr));

	wpsie = rtw_get_wps_ie(frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &wps_ielen);
	if (wpsie) {
		if (rtw_get_wps_attr_content(wpsie, wps_ielen, WPS_ATTR_CONF_METHOD , (u8 *) &uconfig_method, &attr_contentlen)) {
			uconfig_method = be16_to_cpu(uconfig_method);
			switch (uconfig_method) {
			case WPS_CM_DISPLYA: {
				_rtw_memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "dis", 3);
				break;
			}
			case WPS_CM_LABEL: {
				_rtw_memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "lab", 3);
				break;
			}
			case WPS_CM_PUSH_BUTTON: {
				_rtw_memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pbc", 3);
				break;
			}
			case WPS_CM_KEYPAD: {
				_rtw_memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pad", 3);
				break;
			}
			}
			issue_p2p_provision_resp(pwdinfo, get_addr2_ptr(pframe), frame_body, uconfig_method);
		}
	}
	RTW_INFO("[%s] config method = %s\n", __FUNCTION__, pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req);
	return _TRUE;

}

u8 process_p2p_provdisc_resp(struct wifidirect_info *pwdinfo,  u8 *pframe)
{

	return _TRUE;
}

u8 rtw_p2p_get_peer_ch_list(struct wifidirect_info *pwdinfo, u8 *ch_content, u8 ch_cnt, u8 *peer_ch_list)
{
	u8 i = 0, j = 0;
	u8 temp = 0;
	u8 ch_no = 0;
	ch_content += 3;
	ch_cnt -= 3;

	while (ch_cnt > 0) {
		ch_content += 1;
		ch_cnt -= 1;
		temp = *ch_content;
		for (i = 0 ; i < temp ; i++, j++)
			peer_ch_list[j] = *(ch_content + 1 + i);
		ch_content += (temp + 1);
		ch_cnt -= (temp + 1);
		ch_no += temp ;
	}

	return ch_no;
}

u8 rtw_p2p_ch_inclusion(_adapter *adapter, u8 *peer_ch_list, u8 peer_ch_num, u8 *ch_list_inclusioned)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	int	i = 0, j = 0, temp = 0;
	u8 ch_no = 0;

	for (i = 0; i < peer_ch_num; i++) {
		for (j = temp; j < rfctl->max_chan_nums; j++) {
			if (*(peer_ch_list + i) == rfctl->channel_set[j].ChannelNum) {
				ch_list_inclusioned[ch_no++] = *(peer_ch_list + i);
				temp = j;
				break;
			}
		}
	}

	return ch_no;
}

u8 process_p2p_group_negotation_req(struct wifidirect_info *pwdinfo, u8 *pframe, uint len)
{
	_adapter *padapter = pwdinfo->padapter;
	u8	result = P2P_STATUS_SUCCESS;
	u32	p2p_ielen = 0, wps_ielen = 0;
	u8 *ies;
	u32 ies_len;
	u8 *p2p_ie;
	u8 *wpsie;
	u16		wps_devicepassword_id = 0x0000;
	uint	wps_devicepassword_id_len = 0;
#ifdef CONFIG_WFD
#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
#endif /* CONFIG_TDLS	 */
#endif /* CONFIG_WFD */
	wpsie = rtw_get_wps_ie(pframe + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &wps_ielen);
	if (wpsie) {
		/*	Commented by Kurt 20120113 */
		/*	If some device wants to do p2p handshake without sending prov_disc_req */
		/*	We have to get peer_req_cm from here. */
		if (_rtw_memcmp(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "000", 3)) {
			rtw_get_wps_attr_content(wpsie, wps_ielen, WPS_ATTR_DEVICE_PWID, (u8 *) &wps_devicepassword_id, &wps_devicepassword_id_len);
			wps_devicepassword_id = be16_to_cpu(wps_devicepassword_id);

			if (wps_devicepassword_id == WPS_DPID_USER_SPEC)
				_rtw_memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "dis", 3);
			else if (wps_devicepassword_id == WPS_DPID_REGISTRAR_SPEC)
				_rtw_memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pad", 3);
			else
				_rtw_memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pbc", 3);
		}
	} else {
		RTW_INFO("[%s] WPS IE not Found!!\n", __FUNCTION__);
		result = P2P_STATUS_FAIL_INCOMPATIBLE_PARAM;
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
		return result ;
	}

	ies = pframe + _PUBLIC_ACTION_IE_OFFSET_;
	ies_len = len - _PUBLIC_ACTION_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie(ies, ies_len, NULL, &p2p_ielen);

	if (!p2p_ie) {
		RTW_INFO("[%s] P2P IE not Found!!\n", __FUNCTION__);
		result = P2P_STATUS_FAIL_INCOMPATIBLE_PARAM;
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
	}

	while (p2p_ie) {
		u8	attr_content = 0x00;
		u32	attr_contentlen = 0;
		u8	ch_content[100] = { 0x00 };
		uint	ch_cnt = 0;
		u8	peer_ch_list[100] = { 0x00 };
		u8	peer_ch_num = 0;
		u8	ch_list_inclusioned[100] = { 0x00 };
		u8	ch_num_inclusioned = 0;
		u16	cap_attr;
		u8 listen_ch_attr[5] = { 0x00 };

		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_ING);

		/* Check P2P Capability ATTR */
		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CAPABILITY, (u8 *)&cap_attr, (uint *)&attr_contentlen)) {
			cap_attr = le16_to_cpu(cap_attr);

#if defined(CONFIG_WFD) && defined(CONFIG_TDLS)
			if (!(cap_attr & P2P_GRPCAP_INTRABSS))
				ptdlsinfo->ap_prohibited = _TRUE;
#endif /* defined(CONFIG_WFD) && defined(CONFIG_TDLS) */
		}

		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GO_INTENT , &attr_content, &attr_contentlen)) {
			RTW_INFO("[%s] GO Intent = %d, tie = %d\n", __FUNCTION__, attr_content >> 1, attr_content & 0x01);
			pwdinfo->peer_intent = attr_content;	/*	include both intent and tie breaker values. */

			if (pwdinfo->intent == (pwdinfo->peer_intent >> 1)) {
				/*	Try to match the tie breaker value */
				if (pwdinfo->intent == P2P_MAX_INTENT) {
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
					result = P2P_STATUS_FAIL_BOTH_GOINTENT_15;
				} else {
					if (attr_content & 0x01)
						rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
					else
						rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
				}
			} else if (pwdinfo->intent > (pwdinfo->peer_intent >> 1))
				rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
			else
				rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);

			if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
				/*	Store the group id information. */
				_rtw_memcpy(pwdinfo->groupid_info.go_device_addr, pwdinfo->device_addr, ETH_ALEN);
				_rtw_memcpy(pwdinfo->groupid_info.ssid, pwdinfo->nego_ssid, pwdinfo->nego_ssidlen);
			}
		}

		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_LISTEN_CH, (u8 *)listen_ch_attr, (uint *) &attr_contentlen) && attr_contentlen == 5)
			pwdinfo->nego_req_info.peer_ch = listen_ch_attr[4];

		RTW_INFO(FUNC_ADPT_FMT" listen channel :%u\n", FUNC_ADPT_ARG(padapter), pwdinfo->nego_req_info.peer_ch);

		attr_contentlen = 0;
		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_INTENDED_IF_ADDR, pwdinfo->p2p_peer_interface_addr, &attr_contentlen)) {
			if (attr_contentlen != ETH_ALEN)
				_rtw_memset(pwdinfo->p2p_peer_interface_addr, 0x00, ETH_ALEN);
		}

		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, ch_content, &ch_cnt)) {
			peer_ch_num = rtw_p2p_get_peer_ch_list(pwdinfo, ch_content, ch_cnt, peer_ch_list);
			ch_num_inclusioned = rtw_p2p_ch_inclusion(padapter, peer_ch_list, peer_ch_num, ch_list_inclusioned);

			if (ch_num_inclusioned == 0) {
				RTW_INFO("[%s] No common channel in channel list!\n", __FUNCTION__);
				result = P2P_STATUS_FAIL_NO_COMMON_CH;
				rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
				break;
			}

			if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
				if (!rtw_p2p_is_channel_list_ok(pwdinfo->operating_channel,
					ch_list_inclusioned, ch_num_inclusioned)) {
#ifdef CONFIG_CONCURRENT_MODE
					if (rtw_mi_check_status(padapter, MI_LINKED)
					    && padapter->registrypriv.full_ch_in_p2p_handshake == 0) {
						RTW_INFO("[%s] desired channel NOT Found!\n", __FUNCTION__);
						result = P2P_STATUS_FAIL_NO_COMMON_CH;
						rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
						break;
					} else
#endif /* CONFIG_CONCURRENT_MODE */
					{
						u8 operatingch_info[5] = { 0x00 }, peer_operating_ch = 0;
						attr_contentlen = 0;

						if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, operatingch_info, &attr_contentlen))
							peer_operating_ch = operatingch_info[4];

						if (rtw_p2p_is_channel_list_ok(peer_operating_ch,
							ch_list_inclusioned, ch_num_inclusioned)) {
							/**
							 *	Change our operating channel as peer's for compatibility.
							 */
							pwdinfo->operating_channel = peer_operating_ch;
							RTW_INFO("[%s] Change op ch to %02x as peer's\n", __FUNCTION__, pwdinfo->operating_channel);
						} else {
							/* Take first channel of ch_list_inclusioned as operating channel */
							pwdinfo->operating_channel = ch_list_inclusioned[0];
							RTW_INFO("[%s] Change op ch to %02x\n", __FUNCTION__, pwdinfo->operating_channel);
						}
					}

				}
			}
		}

		/* Get the next P2P IE */
		p2p_ie = rtw_get_p2p_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen), NULL, &p2p_ielen);
	}

	if (pwdinfo->ui_got_wps_info == P2P_NO_WPSINFO) {
		result = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
		rtw_p2p_set_state(pwdinfo, P2P_STATE_TX_INFOR_NOREADY);
		return result;
	}

#ifdef CONFIG_WFD
	rtw_process_wfd_ies(padapter, pframe + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, __func__);
#endif

	return result ;
}

u8 process_p2p_group_negotation_resp(struct wifidirect_info *pwdinfo, u8 *pframe, uint len)
{
	_adapter *padapter = pwdinfo->padapter;
	u8	result = P2P_STATUS_SUCCESS;
	u32	p2p_ielen, wps_ielen;
	u8 *ies;
	u32 ies_len;
	u8 *p2p_ie;
#ifdef CONFIG_WFD
#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
#endif /* CONFIG_TDLS	 */
#endif /* CONFIG_WFD */

	ies = pframe + _PUBLIC_ACTION_IE_OFFSET_;
	ies_len = len - _PUBLIC_ACTION_IE_OFFSET_;

	/*	Be able to know which one is the P2P GO and which one is P2P client. */

	if (rtw_get_wps_ie(ies, ies_len, NULL, &wps_ielen)) {

	} else {
		RTW_INFO("[%s] WPS IE not Found!!\n", __FUNCTION__);
		result = P2P_STATUS_FAIL_INCOMPATIBLE_PARAM;
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
	}

	p2p_ie = rtw_get_p2p_ie(ies, ies_len, NULL, &p2p_ielen);
	if (!p2p_ie) {
		rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
		result = P2P_STATUS_FAIL_INCOMPATIBLE_PARAM;
	} else {

		u8	attr_content = 0x00;
		u32	attr_contentlen = 0;
		u8	operatingch_info[5] = { 0x00 };
		u8	groupid[38];
		u16	cap_attr;
		u8	peer_ch_list[100] = { 0x00 };
		u8	peer_ch_num = 0;
		u8	ch_list_inclusioned[100] = { 0x00 };
		u8	ch_num_inclusioned = 0;

		while (p2p_ie) {	/*	Found the P2P IE. */

			/* Check P2P Capability ATTR */
			if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CAPABILITY, (u8 *)&cap_attr, (uint *)&attr_contentlen)) {
				cap_attr = le16_to_cpu(cap_attr);
#ifdef CONFIG_TDLS
				if (!(cap_attr & P2P_GRPCAP_INTRABSS))
					ptdlsinfo->ap_prohibited = _TRUE;
#endif /* CONFIG_TDLS */
			}

			rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, &attr_content, &attr_contentlen);
			if (attr_contentlen == 1) {
				RTW_INFO("[%s] Status = %d\n", __FUNCTION__, attr_content);
				if (attr_content == P2P_STATUS_SUCCESS) {
					/*	Do nothing. */
				} else {
					if (P2P_STATUS_FAIL_INFO_UNAVAILABLE == attr_content)
						rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_INFOR_NOREADY);
					else
						rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
					result = attr_content;
					break;
				}
			}

			/*	Try to get the peer's interface address */
			attr_contentlen = 0;
			if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_INTENDED_IF_ADDR, pwdinfo->p2p_peer_interface_addr, &attr_contentlen)) {
				if (attr_contentlen != ETH_ALEN)
					_rtw_memset(pwdinfo->p2p_peer_interface_addr, 0x00, ETH_ALEN);
			}

			/*	Try to get the peer's intent and tie breaker value. */
			attr_content = 0x00;
			attr_contentlen = 0;
			if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GO_INTENT , &attr_content, &attr_contentlen)) {
				RTW_INFO("[%s] GO Intent = %d, tie = %d\n", __FUNCTION__, attr_content >> 1, attr_content & 0x01);
				pwdinfo->peer_intent = attr_content;	/*	include both intent and tie breaker values. */

				if (pwdinfo->intent == (pwdinfo->peer_intent >> 1)) {
					/*	Try to match the tie breaker value */
					if (pwdinfo->intent == P2P_MAX_INTENT) {
						rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
						result = P2P_STATUS_FAIL_BOTH_GOINTENT_15;
						rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
					} else {
						rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
						rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_GONEGO_OK);
						if (attr_content & 0x01)
							rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
						else
							rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
					}
				} else if (pwdinfo->intent > (pwdinfo->peer_intent >> 1)) {
					rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
					rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_GONEGO_OK);
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
				} else {
					rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
					rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_GONEGO_OK);
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
				}

				if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
					/*	Store the group id information. */
					_rtw_memcpy(pwdinfo->groupid_info.go_device_addr, pwdinfo->device_addr, ETH_ALEN);
					_rtw_memcpy(pwdinfo->groupid_info.ssid, pwdinfo->nego_ssid, pwdinfo->nego_ssidlen);

				}
			}

			/*	Try to get the operation channel information */

			attr_contentlen = 0;
			if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, operatingch_info, &attr_contentlen)) {
				RTW_INFO("[%s] Peer's operating channel = %d\n", __FUNCTION__, operatingch_info[4]);
				pwdinfo->peer_operating_ch = operatingch_info[4];
			}

			/*	Try to get the channel list information */
			if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, pwdinfo->channel_list_attr, &pwdinfo->channel_list_attr_len)) {
				RTW_INFO("[%s] channel list attribute found, len = %d\n", __FUNCTION__,  pwdinfo->channel_list_attr_len);

				peer_ch_num = rtw_p2p_get_peer_ch_list(pwdinfo, pwdinfo->channel_list_attr, pwdinfo->channel_list_attr_len, peer_ch_list);
				ch_num_inclusioned = rtw_p2p_ch_inclusion(padapter, peer_ch_list, peer_ch_num, ch_list_inclusioned);

				if (ch_num_inclusioned == 0) {
					RTW_INFO("[%s] No common channel in channel list!\n", __FUNCTION__);
					result = P2P_STATUS_FAIL_NO_COMMON_CH;
					rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
					break;
				}

				if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
					if (!rtw_p2p_is_channel_list_ok(pwdinfo->operating_channel,
						ch_list_inclusioned, ch_num_inclusioned)) {
#ifdef CONFIG_CONCURRENT_MODE
						if (rtw_mi_check_status(padapter, MI_LINKED)
						    && padapter->registrypriv.full_ch_in_p2p_handshake == 0) {
							RTW_INFO("[%s] desired channel NOT Found!\n", __FUNCTION__);
							result = P2P_STATUS_FAIL_NO_COMMON_CH;
							rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
							break;
						} else
#endif /* CONFIG_CONCURRENT_MODE */
						{
							u8 operatingch_info[5] = { 0x00 }, peer_operating_ch = 0;
							attr_contentlen = 0;

							if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, operatingch_info, &attr_contentlen))
								peer_operating_ch = operatingch_info[4];

							if (rtw_p2p_is_channel_list_ok(peer_operating_ch,
								ch_list_inclusioned, ch_num_inclusioned)) {
								/**
								 *	Change our operating channel as peer's for compatibility.
								 */
								pwdinfo->operating_channel = peer_operating_ch;
								RTW_INFO("[%s] Change op ch to %02x as peer's\n", __FUNCTION__, pwdinfo->operating_channel);
							} else {
								/* Take first channel of ch_list_inclusioned as operating channel */
								pwdinfo->operating_channel = ch_list_inclusioned[0];
								RTW_INFO("[%s] Change op ch to %02x\n", __FUNCTION__, pwdinfo->operating_channel);
							}
						}

					}
				}

			} else
				RTW_INFO("[%s] channel list attribute not found!\n", __FUNCTION__);

			/*	Try to get the group id information if peer is GO */
			attr_contentlen = 0;
			_rtw_memset(groupid, 0x00, 38);
			if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GROUP_ID, groupid, &attr_contentlen)) {
				_rtw_memcpy(pwdinfo->groupid_info.go_device_addr, &groupid[0], ETH_ALEN);
				_rtw_memcpy(pwdinfo->groupid_info.ssid, &groupid[6], attr_contentlen - ETH_ALEN);
			}

			/* Get the next P2P IE */
			p2p_ie = rtw_get_p2p_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen), NULL, &p2p_ielen);
		}

	}

#ifdef CONFIG_WFD
	rtw_process_wfd_ies(padapter, pframe + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, __func__);
#endif

	return result ;

}

u8 process_p2p_group_negotation_confirm(struct wifidirect_info *pwdinfo, u8 *pframe, uint len)
{
#ifdef CONFIG_CONCURRENT_MODE
	_adapter *padapter = pwdinfo->padapter;
#endif
	u8 *ies;
	u32 ies_len;
	u8 *p2p_ie;
	u32	p2p_ielen = 0;
	u8	result = P2P_STATUS_SUCCESS;
	ies = pframe + _PUBLIC_ACTION_IE_OFFSET_;
	ies_len = len - _PUBLIC_ACTION_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie(ies, ies_len, NULL, &p2p_ielen);
	while (p2p_ie) {	/*	Found the P2P IE. */
		u8	attr_content = 0x00, operatingch_info[5] = { 0x00 };
		u8	groupid[38] = { 0x00 };
		u32	attr_contentlen = 0;

		pwdinfo->negotiation_dialog_token = 1;
		rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, &attr_content, &attr_contentlen);
		if (attr_contentlen == 1) {
			RTW_INFO("[%s] Status = %d\n", __FUNCTION__, attr_content);
			result = attr_content;

			if (attr_content == P2P_STATUS_SUCCESS) {

				_cancel_timer_ex(&pwdinfo->restore_p2p_state_timer);

				/*	Commented by Albert 20100911 */
				/*	Todo: Need to handle the case which both Intents are the same. */
				rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
				rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_GONEGO_OK);
				if ((pwdinfo->intent) > (pwdinfo->peer_intent >> 1))
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
				else if ((pwdinfo->intent) < (pwdinfo->peer_intent >> 1))
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
				else {
					/*	Have to compare the Tie Breaker */
					if (pwdinfo->peer_intent & 0x01)
						rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
					else
						rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
				}

#ifdef CONFIG_CONCURRENT_MODE
				if (rtw_mi_check_status(padapter, MI_LINKED)
				    && padapter->registrypriv.full_ch_in_p2p_handshake == 0) {
					/*	Switch back to the AP channel soon. */
					_set_timer(&pwdinfo->ap_p2p_switch_timer, 100);
				}
#endif
			} else {
				rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
				rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
				break;
			}
		}

		/*	Try to get the group id information */
		attr_contentlen = 0;
		_rtw_memset(groupid, 0x00, 38);
		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GROUP_ID, groupid, &attr_contentlen)) {
			RTW_INFO("[%s] Ssid = %s, ssidlen = %zu\n", __FUNCTION__, &groupid[ETH_ALEN], strlen(&groupid[ETH_ALEN]));
			_rtw_memcpy(pwdinfo->groupid_info.go_device_addr, &groupid[0], ETH_ALEN);
			_rtw_memcpy(pwdinfo->groupid_info.ssid, &groupid[6], attr_contentlen - ETH_ALEN);
		}

		attr_contentlen = 0;
		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, operatingch_info, &attr_contentlen)) {
			RTW_INFO("[%s] Peer's operating channel = %d\n", __FUNCTION__, operatingch_info[4]);
			pwdinfo->peer_operating_ch = operatingch_info[4];
		}

		/* Get the next P2P IE */
		p2p_ie = rtw_get_p2p_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen), NULL, &p2p_ielen);

	}

	return result ;
}

u8 process_p2p_presence_req(struct wifidirect_info *pwdinfo, u8 *pframe, uint len)
{
	u8 *frame_body;
	u8 dialogToken = 0;
	u8 status = P2P_STATUS_SUCCESS;

	frame_body = (unsigned char *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));

	dialogToken = frame_body[6];

	/* todo: check NoA attribute */

	issue_p2p_presence_resp(pwdinfo, get_addr2_ptr(pframe), status, dialogToken);

	return _TRUE;
}

void find_phase_handler(_adapter	*padapter)
{
	struct wifidirect_info  *pwdinfo = &padapter->wdinfo;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct sitesurvey_parm parm;
	_irqL				irqL;
	u8					_status = 0;


	rtw_init_sitesurvey_parm(padapter, &parm);
	_rtw_memcpy(&parm.ssid[0].Ssid, pwdinfo->p2p_wildcard_ssid, P2P_WILDCARD_SSID_LEN);
	parm.ssid[0].SsidLength = P2P_WILDCARD_SSID_LEN;
	parm.ssid_num = 1;

	rtw_p2p_set_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH);

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	_status = rtw_sitesurvey_cmd(padapter, &parm);
	_exit_critical_bh(&pmlmepriv->lock, &irqL);


}

void p2p_concurrent_handler(_adapter *padapter);

void restore_p2p_state_handler(_adapter	*padapter)
{
	struct wifidirect_info  *pwdinfo = &padapter->wdinfo;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING) || rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_FAIL))
		rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(padapter, MI_LINKED)) {
		u8 union_ch = rtw_mi_get_union_chan(padapter);
		u8 union_bw = rtw_mi_get_union_bw(padapter);
		u8 union_offset = rtw_mi_get_union_offset(padapter);

		if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_PROVISION_DIS_REQ) || rtw_p2p_chk_state(pwdinfo, P2P_STATE_RX_PROVISION_DIS_RSP)) {
			set_channel_bwmode(padapter, union_ch, union_offset, union_bw);
			rtw_back_opch(padapter);
		}
	}
#endif

	rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));

	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE)) {
#ifdef CONFIG_CONCURRENT_MODE
		p2p_concurrent_handler(padapter);
#else
		/*	In the P2P client mode, the driver should not switch back to its listen channel */
		/*	because this P2P client should stay at the operating channel of P2P GO. */
		set_channel_bwmode(padapter, pwdinfo->listen_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
#endif
	}
}

void pre_tx_invitereq_handler(_adapter	*padapter)
{
	struct wifidirect_info  *pwdinfo = &padapter->wdinfo;
	u8	val8 = 1;

	set_channel_bwmode(padapter, pwdinfo->invitereq_info.peer_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
	issue_probereq_p2p(padapter, NULL);
	_set_timer(&pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT);

}

void pre_tx_provdisc_handler(_adapter	*padapter)
{
	struct wifidirect_info  *pwdinfo = &padapter->wdinfo;
	u8	val8 = 1;

	set_channel_bwmode(padapter, pwdinfo->tx_prov_disc_info.peer_channel_num[0], HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
	issue_probereq_p2p(padapter, NULL);
	_set_timer(&pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT);

}

void pre_tx_negoreq_handler(_adapter	*padapter)
{
	struct wifidirect_info  *pwdinfo = &padapter->wdinfo;
	u8	val8 = 1;

	set_channel_bwmode(padapter, pwdinfo->nego_req_info.peer_channel_num[0], HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
	issue_probereq_p2p(padapter , NULL);
	/* WIN Phone only accept unicast probe request when nego back */
	issue_probereq_p2p(padapter , pwdinfo->nego_req_info.peerDevAddr);
	_set_timer(&pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT);

}

#ifdef CONFIG_CONCURRENT_MODE
void p2p_concurrent_handler(_adapter	*padapter)
{
	struct wifidirect_info	*pwdinfo = &padapter->wdinfo;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	u8					val8;

#ifdef CONFIG_IOCTL_CFG80211
	if (pwdinfo->driver_interface == DRIVER_CFG80211
		&& !rtw_cfg80211_get_is_roch(padapter))
		return;
#endif

	if (rtw_mi_check_status(padapter, MI_LINKED)) {
		u8 union_ch = rtw_mi_get_union_chan(padapter);
		u8 union_bw = rtw_mi_get_union_bw(padapter);
		u8 union_offset = rtw_mi_get_union_offset(padapter);

		pwdinfo->operating_channel = union_ch;

		if (pwdinfo->driver_interface == DRIVER_CFG80211) {
			RTW_INFO("%s, switch ch back to union=%u,%u, %u\n"
				, __func__, union_ch, union_bw, union_offset);
			set_channel_bwmode(padapter, union_ch, union_offset, union_bw);
			rtw_back_opch(padapter);

		} else if (pwdinfo->driver_interface == DRIVER_WEXT) {
			if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE)) {
				/*	Now, the driver stays on the AP's channel. */
				/*	If the pwdinfo->ext_listen_period = 0, that means the P2P listen state is not available on listen channel. */
				if (pwdinfo->ext_listen_period > 0) {
					RTW_INFO("[%s] P2P_STATE_IDLE, ext_listen_period = %d\n", __FUNCTION__, pwdinfo->ext_listen_period);

					if (union_ch != pwdinfo->listen_channel) {
						rtw_leave_opch(padapter);
						set_channel_bwmode(padapter, pwdinfo->listen_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
					}

					rtw_p2p_set_state(pwdinfo, P2P_STATE_LISTEN);

					if (!rtw_mi_check_mlmeinfo_state(padapter, WIFI_FW_AP_STATE)) {
						val8 = 1;
						rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
					}
					/*	Todo: To check the value of pwdinfo->ext_listen_period is equal to 0 or not. */
					_set_timer(&pwdinfo->ap_p2p_switch_timer, pwdinfo->ext_listen_period);
				}

			} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_LISTEN) ||
				rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_FAIL) ||
				(rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING) && pwdinfo->nego_req_info.benable == _FALSE) ||
				rtw_p2p_chk_state(pwdinfo, P2P_STATE_RX_PROVISION_DIS_REQ)) {
				/*	Now, the driver is in the listen state of P2P mode. */
				RTW_INFO("[%s] P2P_STATE_IDLE, ext_listen_interval = %d\n", __FUNCTION__, pwdinfo->ext_listen_interval);

				/*	Commented by Albert 2012/11/01 */
				/*	If the AP's channel is the same as the listen channel, we should still be in the listen state */
				/*	Other P2P device is still able to find this device out even this device is in the AP's channel. */
				/*	So, configure this device to be able to receive the probe request frame and set it to listen state. */
				if (union_ch != pwdinfo->listen_channel) {

					set_channel_bwmode(padapter, union_ch, union_offset, union_bw);
					if (!rtw_mi_check_status(padapter, MI_AP_MODE)) {
						val8 = 0;
						rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
					}
					rtw_p2p_set_state(pwdinfo, P2P_STATE_IDLE);
					rtw_back_opch(padapter);
				}

				/*	Todo: To check the value of pwdinfo->ext_listen_interval is equal to 0 or not. */
				_set_timer(&pwdinfo->ap_p2p_switch_timer, pwdinfo->ext_listen_interval);

			} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_OK)) {
				/*	The driver had finished the P2P handshake successfully. */
				val8 = 0;
				rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
				set_channel_bwmode(padapter, union_ch, union_offset, union_bw);
				rtw_back_opch(padapter);

			} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_PROVISION_DIS_REQ)) {
				val8 = 1;
				set_channel_bwmode(padapter, pwdinfo->tx_prov_disc_info.peer_channel_num[0], HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
				rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
				issue_probereq_p2p(padapter, NULL);
				_set_timer(&pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT);
			} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING) && pwdinfo->nego_req_info.benable == _TRUE) {
				val8 = 1;
				set_channel_bwmode(padapter, pwdinfo->nego_req_info.peer_channel_num[0], HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
				rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
				issue_probereq_p2p(padapter, NULL);
				_set_timer(&pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT);
			} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_INVITE_REQ) && pwdinfo->invitereq_info.benable == _TRUE) {
				/*
				val8 = 1;
				set_channel_bwmode(padapter, , HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
				rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
				issue_probereq_p2p(padapter, NULL);
				_set_timer( &pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT );
				*/
			}
		}
	} else {
		/* In p2p+softap. When in P2P_STATE_GONEGO_OK, not back to listen channel.*/
		if (!rtw_p2p_chk_state(pwdinfo , P2P_STATE_GONEGO_OK) || padapter->registrypriv.full_ch_in_p2p_handshake == 0)
			set_channel_bwmode(padapter, pwdinfo->listen_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
		else
			RTW_INFO("%s, buddy not linked, go nego ok, not back to listen channel\n", __func__);
	}

}
#endif

#ifdef CONFIG_IOCTL_CFG80211
u8 roch_stay_in_cur_chan(_adapter *padapter)
{
	int i;
	_adapter *iface;
	struct mlme_priv *pmlmepriv;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	u8 rst = _FALSE;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface) {
			pmlmepriv = &iface->mlmepriv;

			if (check_fwstate(pmlmepriv, WIFI_UNDER_LINKING | WIFI_UNDER_WPS | WIFI_UNDER_KEY_HANDSHAKE) == _TRUE) {
				RTW_INFO(ADPT_FMT"- WIFI_UNDER_LINKING |WIFI_UNDER_WPS | WIFI_UNDER_KEY_HANDSHAKE (mlme state:0x%x)\n",
						ADPT_ARG(iface), get_fwstate(&iface->mlmepriv));
				rst = _TRUE;
				break;
			}
			#ifdef CONFIG_AP_MODE
			if (MLME_IS_AP(iface) || MLME_IS_MESH(iface)) {
				if (rtw_ap_sta_states_check(iface) == _TRUE) {
					rst = _TRUE;
					break;
				}
			}
			#endif
		}
	}

	return rst;
}

static int ro_ch_handler(_adapter *adapter, u8 *buf)
{
	int ret = H2C_SUCCESS;
	struct p2p_roch_parm *roch_parm = (struct p2p_roch_parm *)buf;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(adapter);
	struct cfg80211_wifidirect_info *pcfg80211_wdinfo = &adapter->cfg80211_wdinfo;
#ifdef CONFIG_CONCURRENT_MODE
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
#ifdef RTW_ROCH_BACK_OP
	struct wifidirect_info *pwdinfo = &adapter->wdinfo;
#endif
#endif
	u8 ready_on_channel = _FALSE;
	u8 remain_ch;
	unsigned int duration;

	_enter_critical_mutex(&pwdev_priv->roch_mutex, NULL);

	if (rtw_cfg80211_get_is_roch(adapter) != _TRUE)
		goto exit;

	remain_ch = (u8)ieee80211_frequency_to_channel(roch_parm->ch.center_freq);
	duration = roch_parm->duration;

	RTW_INFO(FUNC_ADPT_FMT" ch:%u duration:%d, cookie:0x%llx\n"
		, FUNC_ADPT_ARG(adapter), remain_ch, roch_parm->duration, roch_parm->cookie);

	if (roch_parm->wdev && roch_parm->cookie) {
		if (pcfg80211_wdinfo->ro_ch_wdev != roch_parm->wdev) {
			RTW_WARN(FUNC_ADPT_FMT" ongoing wdev:%p, wdev:%p\n"
				, FUNC_ADPT_ARG(adapter), pcfg80211_wdinfo->ro_ch_wdev, roch_parm->wdev);
			rtw_warn_on(1);
		}

		if (pcfg80211_wdinfo->remain_on_ch_cookie != roch_parm->cookie) {
			RTW_WARN(FUNC_ADPT_FMT" ongoing cookie:0x%llx, cookie:0x%llx\n"
				, FUNC_ADPT_ARG(adapter), pcfg80211_wdinfo->remain_on_ch_cookie, roch_parm->cookie);
			rtw_warn_on(1);
		}
	}

	if (roch_stay_in_cur_chan(adapter) == _TRUE) {
		remain_ch = rtw_mi_get_union_chan(adapter);
		RTW_INFO(FUNC_ADPT_FMT" stay in union ch:%d\n", FUNC_ADPT_ARG(adapter), remain_ch);
	}

	#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(adapter, MI_LINKED) && (0 != rtw_mi_get_union_chan(adapter))) {
		if ((remain_ch != rtw_mi_get_union_chan(adapter)) && !check_fwstate(&adapter->mlmepriv, WIFI_ASOC_STATE)) {
			if (remain_ch != pmlmeext->cur_channel
				#ifdef RTW_ROCH_BACK_OP
				|| ATOMIC_READ(&pwdev_priv->switch_ch_to) == 1
				#endif
			) {
				rtw_leave_opch(adapter);

				#ifdef RTW_ROCH_BACK_OP
				RTW_INFO("%s, set switch ch timer, duration=%d\n", __func__, duration - pwdinfo->ext_listen_interval);
				ATOMIC_SET(&pwdev_priv->switch_ch_to, 0);
				_set_timer(&pwdinfo->ap_p2p_switch_timer, duration - pwdinfo->ext_listen_interval);
				#endif
			}
		}
		ready_on_channel = _TRUE;
	} else
	#endif /* CONFIG_CONCURRENT_MODE */
	{
		if (remain_ch != rtw_get_oper_ch(adapter))
			ready_on_channel = _TRUE;
	}

	if (ready_on_channel == _TRUE) {
		#ifndef RTW_SINGLE_WIPHY
		if (!check_fwstate(&adapter->mlmepriv, WIFI_ASOC_STATE))
		#endif
		{
			#ifdef CONFIG_CONCURRENT_MODE
			if (rtw_get_oper_ch(adapter) != remain_ch)
			#endif
			{
				/* if (!padapter->mlmepriv.LinkDetectInfo.bBusyTraffic) */
				set_channel_bwmode(adapter, remain_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
			}
		}
	}

	#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_ScanNotify(adapter, _TRUE);
	#endif

	RTW_INFO("%s, set ro ch timer, duration=%d\n", __func__, duration);
	_set_timer(&pcfg80211_wdinfo->remain_on_ch_timer, duration);

exit:
	_exit_critical_mutex(&pwdev_priv->roch_mutex, NULL);

	return ret;
}

static int cancel_ro_ch_handler(_adapter *padapter, u8 *buf)
{
	int ret = H2C_SUCCESS;
	struct p2p_roch_parm *roch_parm = (struct p2p_roch_parm *)buf;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
	struct cfg80211_wifidirect_info *pcfg80211_wdinfo = &padapter->cfg80211_wdinfo;
	struct wireless_dev *wdev;
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	u8 ch, bw, offset;

	_enter_critical_mutex(&pwdev_priv->roch_mutex, NULL);

	if (rtw_cfg80211_get_is_roch(padapter) != _TRUE)
		goto exit;

	if (roch_parm->wdev && roch_parm->cookie) {
		if (pcfg80211_wdinfo->ro_ch_wdev != roch_parm->wdev) {
			RTW_WARN(FUNC_ADPT_FMT" ongoing wdev:%p, wdev:%p\n"
				, FUNC_ADPT_ARG(padapter), pcfg80211_wdinfo->ro_ch_wdev, roch_parm->wdev);
			rtw_warn_on(1);
		}

		if (pcfg80211_wdinfo->remain_on_ch_cookie != roch_parm->cookie) {
			RTW_WARN(FUNC_ADPT_FMT" ongoing cookie:0x%llx, cookie:0x%llx\n"
				, FUNC_ADPT_ARG(padapter), pcfg80211_wdinfo->remain_on_ch_cookie, roch_parm->cookie);
			rtw_warn_on(1);
		}
	}

#if defined(RTW_ROCH_BACK_OP) && defined(CONFIG_CONCURRENT_MODE)
	_cancel_timer_ex(&pwdinfo->ap_p2p_switch_timer);
	ATOMIC_SET(&pwdev_priv->switch_ch_to, 1);
#endif

	if (rtw_mi_get_ch_setting_union(padapter, &ch, &bw, &offset) != 0) {
		if (0)
			RTW_INFO(FUNC_ADPT_FMT" back to linked/linking union - ch:%u, bw:%u, offset:%u\n",
				 FUNC_ADPT_ARG(padapter), ch, bw, offset);
	} else if (adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->listen_channel) {
		ch = pwdinfo->listen_channel;
		bw = CHANNEL_WIDTH_20;
		offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		if (0)
			RTW_INFO(FUNC_ADPT_FMT" back to listen ch - ch:%u, bw:%u, offset:%u\n",
				 FUNC_ADPT_ARG(padapter), ch, bw, offset);
	} else {
		ch = pcfg80211_wdinfo->restore_channel;
		bw = CHANNEL_WIDTH_20;
		offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		if (0)
			RTW_INFO(FUNC_ADPT_FMT" back to restore ch - ch:%u, bw:%u, offset:%u\n",
				 FUNC_ADPT_ARG(padapter), ch, bw, offset);
	}

	set_channel_bwmode(padapter, ch, offset, bw);
	rtw_back_opch(padapter);

	rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
#ifdef CONFIG_DEBUG_CFG80211
	RTW_INFO("%s, role=%d, p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo));
#endif

	wdev = pcfg80211_wdinfo->ro_ch_wdev;

	rtw_cfg80211_set_is_roch(padapter, _FALSE);
	pcfg80211_wdinfo->ro_ch_wdev = NULL;
	rtw_cfg80211_set_last_ro_ch_time(padapter);

	rtw_cfg80211_remain_on_channel_expired(wdev
		, pcfg80211_wdinfo->remain_on_ch_cookie
		, &pcfg80211_wdinfo->remain_on_ch_channel
		, pcfg80211_wdinfo->remain_on_ch_type, GFP_KERNEL);

	RTW_INFO("cfg80211_remain_on_channel_expired cookie:0x%llx\n"
		, pcfg80211_wdinfo->remain_on_ch_cookie);

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_ScanNotify(padapter, _FALSE);
#endif

exit:
	_exit_critical_mutex(&pwdev_priv->roch_mutex, NULL);

	return ret;
}

static void ro_ch_timer_process(void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;

	p2p_cancel_roch_cmd(adapter, 0, NULL, 0);
}

#if 0
static void rtw_change_p2pie_op_ch(_adapter *padapter, const u8 *frame_body, u32 len, u8 ch)
{
	u8 *ies, *p2p_ie;
	u32 ies_len, p2p_ielen;

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(padapter))
		return;
#endif /* CONFIG_MCC_MODE */

	ies = (u8 *)(frame_body + _PUBLIC_ACTION_IE_OFFSET_);
	ies_len = len - _PUBLIC_ACTION_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie(ies, ies_len, NULL, &p2p_ielen);

	while (p2p_ie) {
		u32	attr_contentlen = 0;
		u8 *pattr = NULL;

		/* Check P2P_ATTR_OPERATING_CH */
		attr_contentlen = 0;
		pattr = NULL;
		pattr = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL, (uint *)&attr_contentlen);
		if (pattr != NULL)
			*(pattr + 4) = ch;

		/* Get the next P2P IE */
		p2p_ie = rtw_get_p2p_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen), NULL, &p2p_ielen);
	}
}
#endif

#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT)
static void rtw_change_p2pie_ch_list(_adapter *padapter, const u8 *frame_body, u32 len, u8 ch)
{
	u8 *ies, *p2p_ie;
	u32 ies_len, p2p_ielen;

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(padapter))
		return;
#endif /* CONFIG_MCC_MODE */

	ies = (u8 *)(frame_body + _PUBLIC_ACTION_IE_OFFSET_);
	ies_len = len - _PUBLIC_ACTION_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie(ies, ies_len, NULL, &p2p_ielen);

	while (p2p_ie) {
		u32	attr_contentlen = 0;
		u8 *pattr = NULL;

		/* Check P2P_ATTR_CH_LIST */
		pattr = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, NULL, (uint *)&attr_contentlen);
		if (pattr != NULL) {
			int i;
			u32 num_of_ch;
			u8 *pattr_temp = pattr + 3 ;

			attr_contentlen -= 3;

			while (attr_contentlen > 0) {
				num_of_ch = *(pattr_temp + 1);

				for (i = 0; i < num_of_ch; i++)
					*(pattr_temp + 2 + i) = ch;

				pattr_temp += (2 + num_of_ch);
				attr_contentlen -= (2 + num_of_ch);
			}
		}

		/* Get the next P2P IE */
		p2p_ie = rtw_get_p2p_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen), NULL, &p2p_ielen);
	}
}
#endif

#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT)
static bool rtw_chk_p2pie_ch_list_with_buddy(_adapter *padapter, const u8 *frame_body, u32 len)
{
	bool fit = _FALSE;
	u8 *ies, *p2p_ie;
	u32 ies_len, p2p_ielen;
	u8 union_ch = rtw_mi_get_union_chan(padapter);

	ies = (u8 *)(frame_body + _PUBLIC_ACTION_IE_OFFSET_);
	ies_len = len - _PUBLIC_ACTION_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie(ies, ies_len, NULL, &p2p_ielen);

	while (p2p_ie) {
		u32	attr_contentlen = 0;
		u8 *pattr = NULL;

		/* Check P2P_ATTR_CH_LIST */
		pattr = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, NULL, (uint *)&attr_contentlen);
		if (pattr != NULL) {
			int i;
			u32 num_of_ch;
			u8 *pattr_temp = pattr + 3 ;

			attr_contentlen -= 3;

			while (attr_contentlen > 0) {
				num_of_ch = *(pattr_temp + 1);

				for (i = 0; i < num_of_ch; i++) {
					if (*(pattr_temp + 2 + i) == union_ch) {
						RTW_INFO(FUNC_ADPT_FMT" ch_list fit buddy_ch:%u\n", FUNC_ADPT_ARG(padapter), union_ch);
						fit = _TRUE;
						break;
					}
				}

				pattr_temp += (2 + num_of_ch);
				attr_contentlen -= (2 + num_of_ch);
			}
		}

		/* Get the next P2P IE */
		p2p_ie = rtw_get_p2p_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen), NULL, &p2p_ielen);
	}

	return fit;
}

#if defined(CONFIG_P2P_INVITE_IOT)
static bool rtw_chk_p2pie_op_ch_with_buddy(_adapter *padapter, const u8 *frame_body, u32 len)
{
	bool fit = _FALSE;
	u8 *ies, *p2p_ie;
	u32 ies_len, p2p_ielen;
	u8 union_ch = rtw_mi_get_union_chan(padapter);

	ies = (u8 *)(frame_body + _PUBLIC_ACTION_IE_OFFSET_);
	ies_len = len - _PUBLIC_ACTION_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie(ies, ies_len, NULL, &p2p_ielen);

	while (p2p_ie) {
		u32	attr_contentlen = 0;
		u8 *pattr = NULL;

		/* Check P2P_ATTR_OPERATING_CH */
		attr_contentlen = 0;
		pattr = NULL;
		pattr = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL, (uint *)&attr_contentlen);
		if (pattr != NULL) {
			if (*(pattr + 4) == union_ch) {
				RTW_INFO(FUNC_ADPT_FMT" op_ch fit buddy_ch:%u\n", FUNC_ADPT_ARG(padapter), union_ch);
				fit = _TRUE;
				break;
			}
		}

		/* Get the next P2P IE */
		p2p_ie = rtw_get_p2p_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen), NULL, &p2p_ielen);
	}

	return fit;
}
#endif

static void rtw_cfg80211_adjust_p2pie_channel(_adapter *padapter, const u8 *frame_body, u32 len)
{
	u8 *ies, *p2p_ie;
	u32 ies_len, p2p_ielen;
	u8 union_ch = rtw_mi_get_union_chan(padapter);

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(padapter))
		return;
#endif /* CONFIG_MCC_MODE */

	ies = (u8 *)(frame_body + _PUBLIC_ACTION_IE_OFFSET_);
	ies_len = len - _PUBLIC_ACTION_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie(ies, ies_len, NULL, &p2p_ielen);

	while (p2p_ie) {
		u32	attr_contentlen = 0;
		u8 *pattr = NULL;

		/* Check P2P_ATTR_CH_LIST */
		pattr = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, NULL, (uint *)&attr_contentlen);
		if (pattr != NULL) {
			int i;
			u32 num_of_ch;
			u8 *pattr_temp = pattr + 3 ;

			attr_contentlen -= 3;

			while (attr_contentlen > 0) {
				num_of_ch = *(pattr_temp + 1);

				for (i = 0; i < num_of_ch; i++) {
					if (*(pattr_temp + 2 + i) && *(pattr_temp + 2 + i) != union_ch) {
						#ifdef RTW_SINGLE_WIPHY
						RTW_ERR("replace ch_list:%u with:%u\n", *(pattr_temp + 2 + i), union_ch);
						#endif
						*(pattr_temp + 2 + i) = union_ch; /*forcing to the same channel*/
					}
				}

				pattr_temp += (2 + num_of_ch);
				attr_contentlen -= (2 + num_of_ch);
			}
		}

		/* Check P2P_ATTR_OPERATING_CH */
		attr_contentlen = 0;
		pattr = NULL;
		pattr = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL, (uint *)&attr_contentlen);
		if (pattr != NULL) {
			if (*(pattr + 4) && *(pattr + 4) != union_ch) {
				#ifdef RTW_SINGLE_WIPHY
				RTW_ERR("replace op_ch:%u with:%u\n", *(pattr + 4), union_ch);
				#endif
				*(pattr + 4) = union_ch; /*forcing to the same channel	*/
			}
		}

		/* Get the next P2P IE */
		p2p_ie = rtw_get_p2p_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen), NULL, &p2p_ielen);

	}

}
#endif

#ifdef CONFIG_WFD
u32 rtw_xframe_build_wfd_ie(struct xmit_frame *xframe)
{
	_adapter *adapter = xframe->padapter;
	struct wifidirect_info *wdinfo = &adapter->wdinfo;
	u8 *frame = xframe->buf_addr + TXDESC_OFFSET;
	u8 *frame_body = frame + sizeof(struct rtw_ieee80211_hdr_3addr);
	u8 *frame_tail = frame + xframe->attrib.pktlen;
	u8 category, action, OUI_Subtype, dialogToken = 0;
	u32	wfdielen = 0;

	category = frame_body[0];
	if (category == RTW_WLAN_CATEGORY_PUBLIC) {
		action = frame_body[1];
		if (action == ACT_PUBLIC_VENDOR
		    && _rtw_memcmp(frame_body + 2, P2P_OUI, 4) == _TRUE
		   ) {
			OUI_Subtype = frame_body[6];
			dialogToken = frame_body[7];

			switch (OUI_Subtype) {
			case P2P_GO_NEGO_REQ:
				wfdielen = build_nego_req_wfd_ie(wdinfo, frame_tail);
				break;
			case P2P_GO_NEGO_RESP:
				wfdielen = build_nego_resp_wfd_ie(wdinfo, frame_tail);
				break;
			case P2P_GO_NEGO_CONF:
				wfdielen = build_nego_confirm_wfd_ie(wdinfo, frame_tail);
				break;
			case P2P_INVIT_REQ:
				wfdielen = build_invitation_req_wfd_ie(wdinfo, frame_tail);
				break;
			case P2P_INVIT_RESP:
				wfdielen = build_invitation_resp_wfd_ie(wdinfo, frame_tail);
				break;
			case P2P_PROVISION_DISC_REQ:
				wfdielen = build_provdisc_req_wfd_ie(wdinfo, frame_tail);
				break;
			case P2P_PROVISION_DISC_RESP:
				wfdielen = build_provdisc_resp_wfd_ie(wdinfo, frame_tail);
				break;
			case P2P_DEVDISC_REQ:
			case P2P_DEVDISC_RESP:
			default:
				break;
			}

		}
	} else if (category == RTW_WLAN_CATEGORY_P2P) {
		OUI_Subtype = frame_body[5];
		dialogToken = frame_body[6];

#ifdef CONFIG_DEBUG_CFG80211
		RTW_INFO("ACTION_CATEGORY_P2P: OUI=0x%x, OUI_Subtype=%d, dialogToken=%d\n"
			, cpu_to_be32(*((u32 *)(frame_body + 1))), OUI_Subtype, dialogToken);
#endif

		switch (OUI_Subtype) {
		case P2P_NOTICE_OF_ABSENCE:
			break;
		case P2P_PRESENCE_REQUEST:
			break;
		case P2P_PRESENCE_RESPONSE:
			break;
		case P2P_GO_DISC_REQUEST:
			break;
		default:
			break;
		}
	} else
		RTW_INFO("%s, action frame category=%d\n", __func__, category);

	xframe->attrib.pktlen += wfdielen;

	return wfdielen;
}
#endif /* CONFIG_WFD */

bool rtw_xframe_del_wfd_ie(struct xmit_frame *xframe)
{
#define DBG_XFRAME_DEL_WFD_IE 0
	u8 *frame = xframe->buf_addr + TXDESC_OFFSET;
	u8 *frame_body = frame + sizeof(struct rtw_ieee80211_hdr_3addr);
	u8 *frame_tail = frame + xframe->attrib.pktlen;
	u8 category, action, OUI_Subtype;
	u8 *ies = NULL;
	uint ies_len_ori = 0;
	uint ies_len = 0;

	category = frame_body[0];
	if (category == RTW_WLAN_CATEGORY_PUBLIC) {
		action = frame_body[1];
		if (action == ACT_PUBLIC_VENDOR
		    && _rtw_memcmp(frame_body + 2, P2P_OUI, 4) == _TRUE
		   ) {
			OUI_Subtype = frame_body[6];

			switch (OUI_Subtype) {
			case P2P_GO_NEGO_REQ:
			case P2P_GO_NEGO_RESP:
			case P2P_GO_NEGO_CONF:
			case P2P_INVIT_REQ:
			case P2P_INVIT_RESP:
			case P2P_PROVISION_DISC_REQ:
			case P2P_PROVISION_DISC_RESP:
				ies = frame_body + 8;
				ies_len_ori = frame_tail - (frame_body + 8);
				break;
			}
		}
	}

	if (ies && ies_len_ori) {
		ies_len = rtw_del_wfd_ie(ies, ies_len_ori, DBG_XFRAME_DEL_WFD_IE ? __func__ : NULL);
		xframe->attrib.pktlen -= (ies_len_ori - ies_len);
	}

	return ies_len_ori != ies_len;
}

/*
* rtw_xframe_chk_wfd_ie -
*
*/
void rtw_xframe_chk_wfd_ie(struct xmit_frame *xframe)
{
	_adapter *adapter = xframe->padapter;
#ifdef CONFIG_IOCTL_CFG80211
	struct wifidirect_info *wdinfo = &adapter->wdinfo;
#endif
	u8 build = 0;
	u8 del = 0;

	if (!hal_chk_wl_func(adapter, WL_FUNC_MIRACAST))
		del = 1;

#ifdef CONFIG_IOCTL_CFG80211
	if (wdinfo->wfd_info->wfd_enable == _TRUE)
#endif
		del = build = 1;

	if (del)
		rtw_xframe_del_wfd_ie(xframe);

#ifdef CONFIG_WFD
	if (build)
		rtw_xframe_build_wfd_ie(xframe);
#endif
}

u8 *dump_p2p_attr_ch_list(u8 *p2p_ie, uint p2p_ielen, u8 *buf, u32 buf_len)
{
	uint attr_contentlen = 0;
	u8 *pattr = NULL;
	int w_sz = 0;
	u8 ch_cnt = 0;
	u8 ch_list[40];

	pattr = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, NULL, &attr_contentlen);
	if (pattr != NULL) {
		int i, j;
		u32 num_of_ch;
		u8 *pattr_temp = pattr + 3 ;

		attr_contentlen -= 3;

		_rtw_memset(ch_list, 0, 40);

		while (attr_contentlen > 0) {
			num_of_ch = *(pattr_temp + 1);

			for (i = 0; i < num_of_ch; i++) {
				for (j = 0; j < ch_cnt; j++) {
					if (ch_list[j] == *(pattr_temp + 2 + i))
						break;
				}
				if (j >= ch_cnt)
					ch_list[ch_cnt++] = *(pattr_temp + 2 + i);

			}

			pattr_temp += (2 + num_of_ch);
			attr_contentlen -= (2 + num_of_ch);
		}

		for (j = 0; j < ch_cnt; j++) {
			if (j == 0)
				w_sz += snprintf(buf + w_sz, buf_len - w_sz, "%u", ch_list[j]);
			else if (ch_list[j] - ch_list[j - 1] != 1)
				w_sz += snprintf(buf + w_sz, buf_len - w_sz, ", %u", ch_list[j]);
			else if (j != ch_cnt - 1 && ch_list[j + 1] - ch_list[j] == 1) {
				/* empty */
			} else
				w_sz += snprintf(buf + w_sz, buf_len - w_sz, "-%u", ch_list[j]);
		}
	}
	return buf;
}

/*
 * return _TRUE if requester is GO, _FALSE if responder is GO
 */
bool rtw_p2p_nego_intent_compare(u8 req, u8 resp)
{
	if (req >> 1 == resp >> 1)
		return  req & 0x01 ? _TRUE : _FALSE;
	else if (req >> 1 > resp >> 1)
		return _TRUE;
	else
		return _FALSE;
}

int rtw_p2p_check_frames(_adapter *padapter, const u8 *buf, u32 len, u8 tx)
{
	int is_p2p_frame = (-1);
	unsigned char	*frame_body;
	u8 category, action, OUI_Subtype, dialogToken = 0;
	u8 *p2p_ie = NULL;
	uint p2p_ielen = 0;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
	int status = -1;
	u8 ch_list_buf[128] = {'\0'};
	int op_ch = -1;
	int listen_ch = -1;
	u8 intent = 0;
	u8 *iaddr = NULL;
	u8 *gbssid = NULL;

	frame_body = (unsigned char *)(buf + sizeof(struct rtw_ieee80211_hdr_3addr));
	category = frame_body[0];
	/* just for check */
	if (category == RTW_WLAN_CATEGORY_PUBLIC) {
		action = frame_body[1];
		if (action == ACT_PUBLIC_VENDOR
			&& _rtw_memcmp(frame_body + 2, P2P_OUI, 4) == _TRUE
		) {
			OUI_Subtype = frame_body[6];
			dialogToken = frame_body[7];
			is_p2p_frame = OUI_Subtype;

			#ifdef CONFIG_DEBUG_CFG80211
			RTW_INFO("ACTION_CATEGORY_PUBLIC: ACT_PUBLIC_VENDOR, OUI=0x%x, OUI_Subtype=%d, dialogToken=%d\n",
				cpu_to_be32(*((u32 *)(frame_body + 2))), OUI_Subtype, dialogToken);
			#endif

			p2p_ie = rtw_get_p2p_ie(
				(u8 *)buf + sizeof(struct rtw_ieee80211_hdr_3addr) + _PUBLIC_ACTION_IE_OFFSET_
				, len - sizeof(struct rtw_ieee80211_hdr_3addr) - _PUBLIC_ACTION_IE_OFFSET_
				, NULL, &p2p_ielen);

			switch (OUI_Subtype) { /* OUI Subtype */
				u8 *cont;
				uint cont_len;
			case P2P_GO_NEGO_REQ: {
				struct rtw_wdev_nego_info *nego_info = &pwdev_priv->nego_info;

				if (tx) {
					#ifdef CONFIG_DRV_ISSUE_PROV_REQ /* IOT FOR S2 */
					if (pwdev_priv->provdisc_req_issued == _FALSE)
						rtw_cfg80211_issue_p2p_provision_request(padapter, buf, len);
					#endif /* CONFIG_DRV_ISSUE_PROV_REQ */

					/* pwdev_priv->provdisc_req_issued = _FALSE; */

					#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT)
					if (rtw_mi_check_status(padapter, MI_LINKED) && padapter->registrypriv.full_ch_in_p2p_handshake == 0)
						rtw_cfg80211_adjust_p2pie_channel(padapter, frame_body, len - sizeof(struct rtw_ieee80211_hdr_3addr));
					#endif
				}

				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL, &cont_len);
				if (cont)
					op_ch = *(cont + 4);
				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_LISTEN_CH, NULL, &cont_len);
				if (cont)
					listen_ch = *(cont + 4);
				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GO_INTENT, NULL, &cont_len);
				if (cont)
					intent = *cont;
				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_INTENDED_IF_ADDR, NULL, &cont_len);
				if (cont && cont_len == 6)
					iaddr = cont;

				if (nego_info->token != dialogToken)
					rtw_wdev_nego_info_init(nego_info);

				_rtw_memcpy(nego_info->peer_mac, tx ? GetAddr1Ptr(buf) : get_addr2_ptr(buf), ETH_ALEN);
				if (iaddr)
					_rtw_memcpy(tx ? nego_info->iface_addr : nego_info->peer_iface_addr, iaddr, ETH_ALEN);
				nego_info->active = tx ? 1 : 0;
				nego_info->token = dialogToken;
				nego_info->req_op_ch = op_ch;
				nego_info->req_listen_ch = listen_ch;
				nego_info->req_intent = intent;
				nego_info->state = 0;

				dump_p2p_attr_ch_list(p2p_ie, p2p_ielen, ch_list_buf, 128);
				RTW_INFO("RTW_%s:P2P_GO_NEGO_REQ, dialogToken=%d, intent:%u%s, listen_ch:%d, op_ch:%d, ch_list:%s"
					, (tx == _TRUE) ? "Tx" : "Rx" , dialogToken , (intent >> 1) , intent & 0x1 ? "+" : "-" , listen_ch , op_ch , ch_list_buf);
				if (iaddr)
					_RTW_INFO(", iaddr:"MAC_FMT, MAC_ARG(iaddr));
				_RTW_INFO("\n");

				if (!tx) {
					#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT)
					if (rtw_mi_check_status(padapter, MI_LINKED)
					    && rtw_chk_p2pie_ch_list_with_buddy(padapter, frame_body, len - sizeof(struct rtw_ieee80211_hdr_3addr)) == _FALSE
					    && padapter->registrypriv.full_ch_in_p2p_handshake == 0) {
						RTW_INFO(FUNC_ADPT_FMT" ch_list has no intersect with buddy\n", FUNC_ADPT_ARG(padapter));
						rtw_change_p2pie_ch_list(padapter, frame_body, len - sizeof(struct rtw_ieee80211_hdr_3addr), 0);
					}
					#endif
				}

				break;
			}
			case P2P_GO_NEGO_RESP: {
				struct rtw_wdev_nego_info *nego_info = &pwdev_priv->nego_info;

				if (tx) {
					#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT)
					if (rtw_mi_check_status(padapter, MI_LINKED) && padapter->registrypriv.full_ch_in_p2p_handshake == 0)
						rtw_cfg80211_adjust_p2pie_channel(padapter, frame_body, len - sizeof(struct rtw_ieee80211_hdr_3addr));
					#endif
				}

				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL, &cont_len);
				if (cont)
					op_ch = *(cont + 4);
				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GO_INTENT, NULL, &cont_len);
				if (cont)
					intent = *cont;
				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, NULL, &cont_len);
				if (cont)
					status = *cont;
				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_INTENDED_IF_ADDR, NULL, &cont_len);
				if (cont && cont_len == 6)
					iaddr = cont;

				if (nego_info->token == dialogToken && nego_info->state == 0
					&& _rtw_memcmp(nego_info->peer_mac, tx ? GetAddr1Ptr(buf) : get_addr2_ptr(buf), ETH_ALEN) == _TRUE
				) {
					if (iaddr)
						_rtw_memcpy(tx ? nego_info->iface_addr : nego_info->peer_iface_addr, iaddr, ETH_ALEN);
					nego_info->status = (status == -1) ? 0xff : status;
					nego_info->rsp_op_ch = op_ch;
					nego_info->rsp_intent = intent;
					nego_info->state = 1;
					if (status != 0)
						nego_info->token = 0; /* init */
				}

				dump_p2p_attr_ch_list(p2p_ie, p2p_ielen, ch_list_buf, 128);
				RTW_INFO("RTW_%s:P2P_GO_NEGO_RESP, dialogToken=%d, intent:%u%s, status:%d, op_ch:%d, ch_list:%s"
					, (tx == _TRUE) ? "Tx" : "Rx", dialogToken, (intent >> 1), intent & 0x1 ? "+" : "-", status, op_ch, ch_list_buf);
				if (iaddr)
					_RTW_INFO(", iaddr:"MAC_FMT, MAC_ARG(iaddr));
				_RTW_INFO("\n");

				if (!tx) {
					pwdev_priv->provdisc_req_issued = _FALSE;
					#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT)
					if (rtw_mi_check_status(padapter, MI_LINKED)
					    && rtw_chk_p2pie_ch_list_with_buddy(padapter, frame_body, len - sizeof(struct rtw_ieee80211_hdr_3addr)) == _FALSE
					    && padapter->registrypriv.full_ch_in_p2p_handshake == 0) {
						RTW_INFO(FUNC_ADPT_FMT" ch_list has no intersect with buddy\n", FUNC_ADPT_ARG(padapter));
						rtw_change_p2pie_ch_list(padapter, frame_body, len - sizeof(struct rtw_ieee80211_hdr_3addr), 0);
					}
					#endif
				}

				break;
			}
			case P2P_GO_NEGO_CONF: {
				struct rtw_wdev_nego_info *nego_info = &pwdev_priv->nego_info;
				bool is_go = _FALSE;

				if (tx) {
					#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT)
					if (rtw_mi_check_status(padapter, MI_LINKED) && padapter->registrypriv.full_ch_in_p2p_handshake == 0)
						rtw_cfg80211_adjust_p2pie_channel(padapter, frame_body, len - sizeof(struct rtw_ieee80211_hdr_3addr));
					#endif
				}

				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL, &cont_len);
				if (cont)
					op_ch = *(cont + 4);
				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, NULL, &cont_len);
				if (cont)
					status = *cont;

				if (nego_info->token == dialogToken && nego_info->state == 1
				    && _rtw_memcmp(nego_info->peer_mac, tx ? GetAddr1Ptr(buf) : get_addr2_ptr(buf), ETH_ALEN) == _TRUE
				   ) {
					nego_info->status = (status == -1) ? 0xff : status;
					nego_info->conf_op_ch = (op_ch == -1) ? 0 : op_ch;
					nego_info->state = 2;

					if (status == 0) {
						if (rtw_p2p_nego_intent_compare(nego_info->req_intent, nego_info->rsp_intent) ^ !tx)
							is_go = _TRUE;
					}

					nego_info->token = 0; /* init */
				}

				dump_p2p_attr_ch_list(p2p_ie, p2p_ielen, ch_list_buf, 128);
				RTW_INFO("RTW_%s:P2P_GO_NEGO_CONF, dialogToken=%d, status:%d, op_ch:%d, ch_list:%s\n"
					, (tx == _TRUE) ? "Tx" : "Rx", dialogToken, status, op_ch, ch_list_buf);

				if (!tx) {
				}

				break;
			}
			case P2P_INVIT_REQ: {
				struct rtw_wdev_invit_info *invit_info = &pwdev_priv->invit_info;
				int flags = -1;

				if (tx) {
					#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT)
					if (rtw_mi_check_status(padapter, MI_LINKED)
					    && padapter->registrypriv.full_ch_in_p2p_handshake == 0)
						rtw_cfg80211_adjust_p2pie_channel(padapter, frame_body, len - sizeof(struct rtw_ieee80211_hdr_3addr));
					#endif
				}

				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_INVITATION_FLAGS, NULL, &cont_len);
				if (cont)
					flags = *cont;
				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL, &cont_len);
				if (cont)
					op_ch = *(cont + 4);
				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GROUP_BSSID, NULL, &cont_len);
				if (cont && cont_len == 6)
					gbssid = cont;

				if (invit_info->token != dialogToken)
					rtw_wdev_invit_info_init(invit_info);

				_rtw_memcpy(invit_info->peer_mac, tx ? GetAddr1Ptr(buf) : get_addr2_ptr(buf), ETH_ALEN);
				if (gbssid)
					_rtw_memcpy(invit_info->group_bssid, gbssid, ETH_ALEN);
				invit_info->active = tx ? 1 : 0;
				invit_info->token = dialogToken;
				invit_info->flags = (flags == -1) ? 0x0 : flags;
				invit_info->req_op_ch = op_ch;
				invit_info->state = 0;

				dump_p2p_attr_ch_list(p2p_ie, p2p_ielen, ch_list_buf, 128);
				RTW_INFO("RTW_%s:P2P_INVIT_REQ, dialogToken=%d, flags:0x%02x, op_ch:%d, ch_list:%s"
					, (tx == _TRUE) ? "Tx" : "Rx", dialogToken, flags, op_ch, ch_list_buf);
				if (gbssid)
					_RTW_INFO(", gbssid:"MAC_FMT, MAC_ARG(gbssid));
				_RTW_INFO("\n");

				if (!tx) {
					#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT)
					if (rtw_mi_check_status(padapter, MI_LINKED) && padapter->registrypriv.full_ch_in_p2p_handshake == 0) {
						#if defined(CONFIG_P2P_INVITE_IOT)
						if (op_ch != -1 && rtw_chk_p2pie_op_ch_with_buddy(padapter, frame_body, len - sizeof(struct rtw_ieee80211_hdr_3addr)) == _FALSE) {
							RTW_INFO(FUNC_ADPT_FMT" op_ch:%u has no intersect with buddy\n", FUNC_ADPT_ARG(padapter), op_ch);
							rtw_change_p2pie_ch_list(padapter, frame_body, len - sizeof(struct rtw_ieee80211_hdr_3addr), 0);
						} else
						#endif
						if (rtw_chk_p2pie_ch_list_with_buddy(padapter, frame_body, len - sizeof(struct rtw_ieee80211_hdr_3addr)) == _FALSE) {
							RTW_INFO(FUNC_ADPT_FMT" ch_list has no intersect with buddy\n", FUNC_ADPT_ARG(padapter));
							rtw_change_p2pie_ch_list(padapter, frame_body, len - sizeof(struct rtw_ieee80211_hdr_3addr), 0);
						}
					}
					#endif
				}

				break;
			}
			case P2P_INVIT_RESP: {
				struct rtw_wdev_invit_info *invit_info = &pwdev_priv->invit_info;

				if (tx) {
					#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT)
					if (rtw_mi_check_status(padapter, MI_LINKED) && padapter->registrypriv.full_ch_in_p2p_handshake == 0)
						rtw_cfg80211_adjust_p2pie_channel(padapter, frame_body, len - sizeof(struct rtw_ieee80211_hdr_3addr));
					#endif
				}

				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, NULL, &cont_len);
				if (cont) {
					#ifdef CONFIG_P2P_INVITE_IOT
					if (tx && *cont == 7) {
						RTW_INFO("TX_P2P_INVITE_RESP, status is no common channel, change to unknown group\n");
						*cont = 8; /* unknow group status */
					}
					#endif /* CONFIG_P2P_INVITE_IOT */
					status = *cont;
				}
				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL, &cont_len);
				if (cont)
					op_ch = *(cont + 4);
				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GROUP_BSSID, NULL, &cont_len);
				if (cont && cont_len == 6)
					gbssid = cont;

				if (invit_info->token == dialogToken && invit_info->state == 0
				    && _rtw_memcmp(invit_info->peer_mac, tx ? GetAddr1Ptr(buf) : get_addr2_ptr(buf), ETH_ALEN) == _TRUE
				   ) {
					invit_info->status = (status == -1) ? 0xff : status;
					invit_info->rsp_op_ch = op_ch;
					invit_info->state = 1;
					invit_info->token = 0; /* init */
				}

				dump_p2p_attr_ch_list(p2p_ie, p2p_ielen, ch_list_buf, 128);
				RTW_INFO("RTW_%s:P2P_INVIT_RESP, dialogToken=%d, status:%d, op_ch:%d, ch_list:%s"
					, (tx == _TRUE) ? "Tx" : "Rx", dialogToken, status, op_ch, ch_list_buf);
				if (gbssid)
					_RTW_INFO(", gbssid:"MAC_FMT, MAC_ARG(gbssid));
				_RTW_INFO("\n");

				if (!tx) {
				}

				break;
			}
			case P2P_DEVDISC_REQ:
				RTW_INFO("RTW_%s:P2P_DEVDISC_REQ, dialogToken=%d\n", (tx == _TRUE) ? "Tx" : "Rx", dialogToken);
				break;
			case P2P_DEVDISC_RESP:
				cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, NULL, &cont_len);
				RTW_INFO("RTW_%s:P2P_DEVDISC_RESP, dialogToken=%d, status:%d\n", (tx == _TRUE) ? "Tx" : "Rx", dialogToken, cont ? *cont : -1);
				break;
			case P2P_PROVISION_DISC_REQ: {
				size_t frame_body_len = len - sizeof(struct rtw_ieee80211_hdr_3addr);
				u8 *p2p_ie;
				uint p2p_ielen = 0;
				uint contentlen = 0;

				RTW_INFO("RTW_%s:P2P_PROVISION_DISC_REQ, dialogToken=%d\n", (tx == _TRUE) ? "Tx" : "Rx", dialogToken);

				/* if(tx) */
				{
					pwdev_priv->provdisc_req_issued = _FALSE;

					p2p_ie = rtw_get_p2p_ie(frame_body + _PUBLIC_ACTION_IE_OFFSET_, frame_body_len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &p2p_ielen);
					if (p2p_ie) {

						if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GROUP_ID, NULL, &contentlen)) {
							pwdev_priv->provdisc_req_issued = _FALSE;/* case: p2p_client join p2p GO */
						} else {
							#ifdef CONFIG_DEBUG_CFG80211
							RTW_INFO("provdisc_req_issued is _TRUE\n");
							#endif /*CONFIG_DEBUG_CFG80211*/
							pwdev_priv->provdisc_req_issued = _TRUE;/* case: p2p_devices connection before Nego req. */
						}

					}
				}
			}
			break;
			case P2P_PROVISION_DISC_RESP:
				RTW_INFO("RTW_%s:P2P_PROVISION_DISC_RESP, dialogToken=%d\n", (tx == _TRUE) ? "Tx" : "Rx", dialogToken);
				break;
			default:
				RTW_INFO("RTW_%s:OUI_Subtype=%d, dialogToken=%d\n", (tx == _TRUE) ? "Tx" : "Rx", OUI_Subtype, dialogToken);
				break;
			}

		}

	} else if (category == RTW_WLAN_CATEGORY_P2P) {
		OUI_Subtype = frame_body[5];
		dialogToken = frame_body[6];

		#ifdef CONFIG_DEBUG_CFG80211
		RTW_INFO("ACTION_CATEGORY_P2P: OUI=0x%x, OUI_Subtype=%d, dialogToken=%d\n",
			cpu_to_be32(*((u32 *)(frame_body + 1))), OUI_Subtype, dialogToken);
		#endif

		is_p2p_frame = OUI_Subtype;

		switch (OUI_Subtype) {
		case P2P_NOTICE_OF_ABSENCE:
			RTW_INFO("RTW_%s:P2P_NOTICE_OF_ABSENCE, dialogToken=%d\n", (tx == _TRUE) ? "Tx" : "Rx", dialogToken);
			break;
		case P2P_PRESENCE_REQUEST:
			RTW_INFO("RTW_%s:P2P_PRESENCE_REQUEST, dialogToken=%d\n", (tx == _TRUE) ? "Tx" : "Rx", dialogToken);
			break;
		case P2P_PRESENCE_RESPONSE:
			RTW_INFO("RTW_%s:P2P_PRESENCE_RESPONSE, dialogToken=%d\n", (tx == _TRUE) ? "Tx" : "Rx", dialogToken);
			break;
		case P2P_GO_DISC_REQUEST:
			RTW_INFO("RTW_%s:P2P_GO_DISC_REQUEST, dialogToken=%d\n", (tx == _TRUE) ? "Tx" : "Rx", dialogToken);
			break;
		default:
			RTW_INFO("RTW_%s:OUI_Subtype=%d, dialogToken=%d\n", (tx == _TRUE) ? "Tx" : "Rx", OUI_Subtype, dialogToken);
			break;
		}

	}

	return is_p2p_frame;
}

void rtw_init_cfg80211_wifidirect_info(_adapter	*padapter)
{
	struct cfg80211_wifidirect_info *pcfg80211_wdinfo = &padapter->cfg80211_wdinfo;

	_rtw_memset(pcfg80211_wdinfo, 0x00, sizeof(struct cfg80211_wifidirect_info));

	rtw_init_timer(&pcfg80211_wdinfo->remain_on_ch_timer, padapter, ro_ch_timer_process, padapter);
}
#endif /* CONFIG_IOCTL_CFG80211	 */

s32 p2p_protocol_wk_hdl(_adapter *padapter, int intCmdType, u8 *buf)
{
	int ret = H2C_SUCCESS;

	switch (intCmdType) {
	case P2P_FIND_PHASE_WK:
		find_phase_handler(padapter);
		break;

	case P2P_RESTORE_STATE_WK:
		restore_p2p_state_handler(padapter);
		break;

	case P2P_PRE_TX_PROVDISC_PROCESS_WK:
#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_mi_check_status(padapter, MI_LINKED))
			p2p_concurrent_handler(padapter);
		else
			pre_tx_provdisc_handler(padapter);
#else
		pre_tx_provdisc_handler(padapter);
#endif
		break;

	case P2P_PRE_TX_INVITEREQ_PROCESS_WK:
#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_mi_check_status(padapter, MI_LINKED))
			p2p_concurrent_handler(padapter);
		else
			pre_tx_invitereq_handler(padapter);
#else
		pre_tx_invitereq_handler(padapter);
#endif
		break;

	case P2P_PRE_TX_NEGOREQ_PROCESS_WK:
#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_mi_check_status(padapter, MI_LINKED))
			p2p_concurrent_handler(padapter);
		else
			pre_tx_negoreq_handler(padapter);
#else
		pre_tx_negoreq_handler(padapter);
#endif
		break;

#ifdef CONFIG_CONCURRENT_MODE
	case P2P_AP_P2P_CH_SWITCH_PROCESS_WK:
		p2p_concurrent_handler(padapter);
		break;
#endif

#ifdef CONFIG_IOCTL_CFG80211
	case P2P_RO_CH_WK:
		ret = ro_ch_handler(padapter, buf);
		break;
	case P2P_CANCEL_RO_CH_WK:
		ret = cancel_ro_ch_handler(padapter, buf);
		break;
#endif

	default:
		rtw_warn_on(1);
		break;
	}

	return ret;
}

int process_p2p_cross_connect_ie(PADAPTER padapter, u8 *IEs, u32 IELength)
{
	int ret = _TRUE;
	u8 *ies;
	u32 ies_len;
	u8 *p2p_ie;
	u32	p2p_ielen = 0;
	u8	p2p_attr[MAX_P2P_IE_LEN] = { 0x00 };/* NoA length should be n*(13) + 2 */
	u32	attr_contentlen = 0;



	if (IELength <= _BEACON_IE_OFFSET_)
		return ret;

	ies = IEs + _BEACON_IE_OFFSET_;
	ies_len = IELength - _BEACON_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie(ies, ies_len, NULL, &p2p_ielen);

	while (p2p_ie) {
		/* Get P2P Manageability IE. */
		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_MANAGEABILITY, p2p_attr, &attr_contentlen)) {
			if ((p2p_attr[0] & (BIT(0) | BIT(1))) == 0x01)
				ret = _FALSE;
			break;
		}
		/* Get the next P2P IE */
		p2p_ie = rtw_get_p2p_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen), NULL, &p2p_ielen);
	}

	return ret;
}

#ifdef CONFIG_P2P_PS
void process_p2p_ps_ie(PADAPTER padapter, u8 *IEs, u32 IELength)
{
	u8 *ies;
	u32 ies_len;
	u8 *p2p_ie;
	u32	p2p_ielen = 0;
	u8 *noa_attr; /* NoA length should be n*(13) + 2 */
	u32	attr_contentlen = 0;

	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	u8	find_p2p = _FALSE, find_p2p_ps = _FALSE;
	u8	noa_offset, noa_num, noa_index;


	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;
#ifdef CONFIG_CONCURRENT_MODE
#ifndef CONFIG_FW_MULTI_PORT_SUPPORT
	if (padapter->hw_port != HW_PORT0)
		return;
#endif
#endif
	if (IELength <= _BEACON_IE_OFFSET_)
		return;

	ies = IEs + _BEACON_IE_OFFSET_;
	ies_len = IELength - _BEACON_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie(ies, ies_len, NULL, &p2p_ielen);

	while (p2p_ie) {
		find_p2p = _TRUE;
		/* Get Notice of Absence IE. */
		noa_attr = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_NOA, NULL, &attr_contentlen);
		if (noa_attr) {
			find_p2p_ps = _TRUE;
			noa_index = noa_attr[0];

			if ((pwdinfo->p2p_ps_mode == P2P_PS_NONE) ||
			    (noa_index != pwdinfo->noa_index)) { /* if index change, driver should reconfigure related setting. */
				pwdinfo->noa_index = noa_index;
				pwdinfo->opp_ps = noa_attr[1] >> 7;
				if (pwdinfo->opp_ps != 1)
					pwdinfo->ctwindow = 0;
				else
					pwdinfo->ctwindow = noa_attr[1] & 0x7F;
				noa_offset = 2;
				noa_num = 0;
				/* NoA length should be n*(13) + 2 */
				if (attr_contentlen > 2 && (attr_contentlen - 2) % 13 == 0) {
					while (noa_offset < attr_contentlen && noa_num < P2P_MAX_NOA_NUM) {
						/* _rtw_memcpy(&wifidirect_info->noa_count[noa_num], &noa_attr[noa_offset], 1); */
						pwdinfo->noa_count[noa_num] = noa_attr[noa_offset];
						noa_offset += 1;

						_rtw_memcpy(&pwdinfo->noa_duration[noa_num], &noa_attr[noa_offset], 4);
						noa_offset += 4;

						_rtw_memcpy(&pwdinfo->noa_interval[noa_num], &noa_attr[noa_offset], 4);
						noa_offset += 4;

						_rtw_memcpy(&pwdinfo->noa_start_time[noa_num], &noa_attr[noa_offset], 4);
						noa_offset += 4;

						noa_num++;
					}
				}
				pwdinfo->noa_num = noa_num;

				if (pwdinfo->opp_ps == 1) {
					pwdinfo->p2p_ps_mode = P2P_PS_CTWINDOW;
					/* driver should wait LPS for entering CTWindow */
					if (adapter_to_pwrctl(padapter)->bFwCurrentInPSMode == _TRUE)
						p2p_ps_wk_cmd(padapter, P2P_PS_ENABLE, 1);
				} else if (pwdinfo->noa_num > 0) {
					pwdinfo->p2p_ps_mode = P2P_PS_NOA;
					p2p_ps_wk_cmd(padapter, P2P_PS_ENABLE, 1);
				} else if (pwdinfo->p2p_ps_mode > P2P_PS_NONE)
					p2p_ps_wk_cmd(padapter, P2P_PS_DISABLE, 1);
			}

			break; /* find target, just break. */
		}

		/* Get the next P2P IE */
		p2p_ie = rtw_get_p2p_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen), NULL, &p2p_ielen);

	}

	if (find_p2p == _TRUE) {
		if ((pwdinfo->p2p_ps_mode > P2P_PS_NONE) && (find_p2p_ps == _FALSE))
			p2p_ps_wk_cmd(padapter, P2P_PS_DISABLE, 1);
	}

}

void p2p_ps_wk_hdl(_adapter *padapter, u8 p2p_ps_state)
{
	struct pwrctrl_priv		*pwrpriv = adapter_to_pwrctl(padapter);
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	u32 ps_deny = 0;

	/* Pre action for p2p state */
	switch (p2p_ps_state) {
	case P2P_PS_DISABLE:
		pwdinfo->p2p_ps_state = p2p_ps_state;

		rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_P2P_PS_OFFLOAD, (u8 *)(&p2p_ps_state));

		if (pwdinfo->opp_ps == 1) {
			if (pwrpriv->smart_ps == 0) {
				pwrpriv->smart_ps = 2;
				if (pwrpriv->pwr_mode != PS_MODE_ACTIVE)
					rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&(pwrpriv->pwr_mode)));
			}
		}
		pwdinfo->noa_index = 0;
		pwdinfo->ctwindow = 0;
		pwdinfo->opp_ps = 0;
		pwdinfo->noa_num = 0;
		pwdinfo->p2p_ps_mode = P2P_PS_NONE;

		break;
	case P2P_PS_ENABLE:
		_enter_pwrlock(&adapter_to_pwrctl(padapter)->lock);
		ps_deny = rtw_ps_deny_get(padapter);
		_exit_pwrlock(&adapter_to_pwrctl(padapter)->lock);

		if ((ps_deny & (PS_DENY_SCAN | PS_DENY_JOIN))
			|| rtw_mi_check_fwstate(padapter, (WIFI_UNDER_SURVEY | WIFI_UNDER_LINKING))) {
			pwdinfo->p2p_ps_mode = P2P_PS_NONE;
			RTW_DBG(FUNC_ADPT_FMT" Block P2P PS under site survey or LINKING\n", FUNC_ADPT_ARG(padapter));
			return;
		}
		if (pwdinfo->p2p_ps_mode > P2P_PS_NONE) {
#ifdef CONFIG_MCC_MODE
			if (MCC_EN(padapter)) {
				if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC)) {
					RTW_INFO("P2P PS enble under MCC\n");
					rtw_warn_on(1);
				}

			}
#endif /* CONFIG_MCC_MODE */
			pwdinfo->p2p_ps_state = p2p_ps_state;

			if (pwdinfo->ctwindow > 0) {
				if (pwrpriv->smart_ps != 0) {
					pwrpriv->smart_ps = 0;
					RTW_INFO("%s(): Enter CTW, change SmartPS\n", __FUNCTION__);
					if (pwrpriv->pwr_mode != PS_MODE_ACTIVE)
						rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&(pwrpriv->pwr_mode)));
				}
			}
			rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_P2P_PS_OFFLOAD, (u8 *)(&p2p_ps_state));
		}
		break;
	case P2P_PS_SCAN:
	case P2P_PS_SCAN_DONE:
	case P2P_PS_ALLSTASLEEP:
		if (pwdinfo->p2p_ps_mode > P2P_PS_NONE) {
			pwdinfo->p2p_ps_state = p2p_ps_state;
			rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_P2P_PS_OFFLOAD, (u8 *)(&p2p_ps_state));
		}
		break;
	default:
		break;
	}

#ifdef CONFIG_MCC_MODE
	rtw_hal_mcc_process_noa(padapter);
#endif /* CONFIG_MCC_MODE */
}

u8 p2p_ps_wk_cmd(_adapter *padapter, u8 p2p_ps_state, u8 enqueue)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;


	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)
#ifdef CONFIG_CONCURRENT_MODE
#ifndef CONFIG_FW_MULTI_PORT_SUPPORT
	    || (padapter->hw_port != HW_PORT0)
#endif
#endif
	   )
		return res;

	if (enqueue) {
		ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
		if (ph2c == NULL) {
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if (pdrvextra_cmd_parm == NULL) {
			rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm->ec_id = P2P_PS_WK_CID;
		pdrvextra_cmd_parm->type = p2p_ps_state;
		pdrvextra_cmd_parm->size = 0;
		pdrvextra_cmd_parm->pbuf = NULL;

		init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	} else
		p2p_ps_wk_hdl(padapter, p2p_ps_state);

exit:


	return res;

}
#endif /* CONFIG_P2P_PS */

static void reset_ch_sitesurvey_timer_process(void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;
	struct	wifidirect_info		*pwdinfo = &adapter->wdinfo;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

	RTW_INFO("[%s] In\n", __FUNCTION__);
	/*	Reset the operation channel information */
	pwdinfo->rx_invitereq_info.operation_ch[0] = 0;
#ifdef CONFIG_P2P_OP_CHK_SOCIAL_CH
	pwdinfo->rx_invitereq_info.operation_ch[1] = 0;
	pwdinfo->rx_invitereq_info.operation_ch[2] = 0;
	pwdinfo->rx_invitereq_info.operation_ch[3] = 0;
#endif /* CONFIG_P2P_OP_CHK_SOCIAL_CH */
	pwdinfo->rx_invitereq_info.scan_op_ch_only = 0;
}

static void reset_ch_sitesurvey_timer_process2(void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;
	struct	wifidirect_info		*pwdinfo = &adapter->wdinfo;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

	RTW_INFO("[%s] In\n", __FUNCTION__);
	/*	Reset the operation channel information */
	pwdinfo->p2p_info.operation_ch[0] = 0;
#ifdef CONFIG_P2P_OP_CHK_SOCIAL_CH
	pwdinfo->p2p_info.operation_ch[1] = 0;
	pwdinfo->p2p_info.operation_ch[2] = 0;
	pwdinfo->p2p_info.operation_ch[3] = 0;
#endif /* CONFIG_P2P_OP_CHK_SOCIAL_CH */
	pwdinfo->p2p_info.scan_op_ch_only = 0;
}

static void restore_p2p_state_timer_process(void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;
	struct	wifidirect_info		*pwdinfo = &adapter->wdinfo;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

	p2p_protocol_wk_cmd(adapter, P2P_RESTORE_STATE_WK);
}

static void pre_tx_scan_timer_process(void *FunctionContext)
{
	_adapter							*adapter = (_adapter *) FunctionContext;
	struct	wifidirect_info				*pwdinfo = &adapter->wdinfo;
	_irqL							irqL;
	struct mlme_priv					*pmlmepriv = &adapter->mlmepriv;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);


	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_PROVISION_DIS_REQ)) {
		if (_TRUE == pwdinfo->tx_prov_disc_info.benable) {	/*	the provision discovery request frame is trigger to send or not */
			p2p_protocol_wk_cmd(adapter, P2P_PRE_TX_PROVDISC_PROCESS_WK);
			/* issue_probereq_p2p(adapter, NULL); */
			/* _set_timer( &pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT ); */
		}
	} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING)) {
		if (_TRUE == pwdinfo->nego_req_info.benable)
			p2p_protocol_wk_cmd(adapter, P2P_PRE_TX_NEGOREQ_PROCESS_WK);
	} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_INVITE_REQ)) {
		if (_TRUE == pwdinfo->invitereq_info.benable)
			p2p_protocol_wk_cmd(adapter, P2P_PRE_TX_INVITEREQ_PROCESS_WK);
	} else
		RTW_INFO("[%s] p2p_state is %d, ignore!!\n", __FUNCTION__, rtw_p2p_state(pwdinfo));

	_exit_critical_bh(&pmlmepriv->lock, &irqL);
}

static void find_phase_timer_process(void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;
	struct	wifidirect_info		*pwdinfo = &adapter->wdinfo;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

	adapter->wdinfo.find_phase_state_exchange_cnt++;

	p2p_protocol_wk_cmd(adapter, P2P_FIND_PHASE_WK);
}

#ifdef CONFIG_CONCURRENT_MODE
void ap_p2p_switch_timer_process(void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;
	struct	wifidirect_info		*pwdinfo = &adapter->wdinfo;
#ifdef CONFIG_IOCTL_CFG80211
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(adapter);
#endif

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

#ifdef CONFIG_IOCTL_CFG80211
	ATOMIC_SET(&pwdev_priv->switch_ch_to, 1);
#endif

	p2p_protocol_wk_cmd(adapter, P2P_AP_P2P_CH_SWITCH_PROCESS_WK);
}
#endif

void reset_global_wifidirect_info(_adapter *padapter)
{
	struct wifidirect_info	*pwdinfo;

	pwdinfo = &padapter->wdinfo;
	pwdinfo->persistent_supported = 0;
	pwdinfo->session_available = _TRUE;
	rtw_tdls_wfd_enable(padapter, 0);
	pwdinfo->wfd_tdls_weaksec = _TRUE;
}

#ifdef CONFIG_WFD
int rtw_init_wifi_display_info(_adapter *padapter)
{
	int	res = _SUCCESS;
	struct wifi_display_info *pwfd_info = &padapter->wfd_info;

	/* Used in P2P and TDLS */
	pwfd_info->init_rtsp_ctrlport = 554;
#ifdef CONFIG_IOCTL_CFG80211
	pwfd_info->rtsp_ctrlport = 0;
#else
	pwfd_info->rtsp_ctrlport = pwfd_info->init_rtsp_ctrlport; /* set non-zero value for legacy wfd */
#endif
	pwfd_info->tdls_rtsp_ctrlport = 0;
	pwfd_info->peer_rtsp_ctrlport = 0;	/*	Reset to 0 */
	pwfd_info->wfd_enable = _FALSE;
	pwfd_info->wfd_device_type = WFD_DEVINFO_PSINK;
	pwfd_info->scan_result_type = SCAN_RESULT_P2P_ONLY;

	/* Used in P2P */
	pwfd_info->peer_session_avail = _TRUE;
	pwfd_info->wfd_pc = _FALSE;

	/* Used in TDLS */
	_rtw_memset(pwfd_info->ip_address, 0x00, 4);
	_rtw_memset(pwfd_info->peer_ip_address, 0x00, 4);
	return res;

}

inline void rtw_wfd_enable(_adapter *adapter, bool on)
{
	struct wifi_display_info *wfdinfo = &adapter->wfd_info;

	if (on) {
		wfdinfo->rtsp_ctrlport = wfdinfo->init_rtsp_ctrlport;
		wfdinfo->wfd_enable = _TRUE;

	} else {
		wfdinfo->wfd_enable = _FALSE;
		wfdinfo->rtsp_ctrlport = 0;
	}
}

inline void rtw_wfd_set_ctrl_port(_adapter *adapter, u16 port)
{
	struct wifi_display_info *wfdinfo = &adapter->wfd_info;

	wfdinfo->init_rtsp_ctrlport = port;
	if (wfdinfo->wfd_enable == _TRUE)
		wfdinfo->rtsp_ctrlport = port;
	if (adapter->wdinfo.wfd_tdls_enable == 1)
		wfdinfo->tdls_rtsp_ctrlport = port;
}

inline void rtw_tdls_wfd_enable(_adapter *adapter, bool on)
{
	struct wifi_display_info *wfdinfo = &adapter->wfd_info;

	if (on) {
		wfdinfo->tdls_rtsp_ctrlport = wfdinfo->init_rtsp_ctrlport;
		adapter->wdinfo.wfd_tdls_enable = 1;

	} else {
		adapter->wdinfo.wfd_tdls_enable = 0;
		wfdinfo->tdls_rtsp_ctrlport = 0;
	}
}

u32 rtw_append_beacon_wfd_ie(_adapter *adapter, u8 *pbuf)
{
	struct wifidirect_info *wdinfo = &adapter->wdinfo;
	struct mlme_priv *mlme = &adapter->mlmepriv;
	u8 build_ie_by_self = 0;
	u32 len = 0;

	if (!hal_chk_wl_func(adapter, WL_FUNC_MIRACAST))
		goto exit;

#ifdef CONFIG_IOCTL_CFG80211
	if (_TRUE == wdinfo->wfd_info->wfd_enable)
#endif
		build_ie_by_self = 1;

	if (build_ie_by_self)
		len = build_beacon_wfd_ie(wdinfo, pbuf);
#ifdef CONFIG_IOCTL_CFG80211
	else if (mlme->wfd_beacon_ie && mlme->wfd_beacon_ie_len > 0) {
		len = mlme->wfd_beacon_ie_len;
		_rtw_memcpy(pbuf, mlme->wfd_beacon_ie, len);
	}
#endif

exit:
	return len;
}

u32 rtw_append_probe_req_wfd_ie(_adapter *adapter, u8 *pbuf)
{
	struct wifidirect_info *wdinfo = &adapter->wdinfo;
	struct mlme_priv *mlme = &adapter->mlmepriv;
	u8 build_ie_by_self = 0;
	u32 len = 0;

	if (!hal_chk_wl_func(adapter, WL_FUNC_MIRACAST))
		goto exit;

#ifdef CONFIG_IOCTL_CFG80211
	if (_TRUE == wdinfo->wfd_info->wfd_enable)
#endif
		build_ie_by_self = 1;

	if (build_ie_by_self)
		len = build_probe_req_wfd_ie(wdinfo, pbuf);
#ifdef CONFIG_IOCTL_CFG80211
	else if (mlme->wfd_probe_req_ie && mlme->wfd_probe_req_ie_len > 0) {
		len = mlme->wfd_probe_req_ie_len;
		_rtw_memcpy(pbuf, mlme->wfd_probe_req_ie, len);
	}
#endif

exit:
	return len;
}

u32 rtw_append_probe_resp_wfd_ie(_adapter *adapter, u8 *pbuf)
{
	struct wifidirect_info *wdinfo = &adapter->wdinfo;
	struct mlme_priv *mlme = &adapter->mlmepriv;
	u8 build_ie_by_self = 0;
	u32 len = 0;

	if (!hal_chk_wl_func(adapter, WL_FUNC_MIRACAST))
		goto exit;

#ifdef CONFIG_IOCTL_CFG80211
	if (_TRUE == wdinfo->wfd_info->wfd_enable)
#endif
		build_ie_by_self = 1;

	if (build_ie_by_self)
		len = build_probe_resp_wfd_ie(wdinfo, pbuf, 0);
#ifdef CONFIG_IOCTL_CFG80211
	else if (mlme->wfd_probe_resp_ie && mlme->wfd_probe_resp_ie_len > 0) {
		len = mlme->wfd_probe_resp_ie_len;
		_rtw_memcpy(pbuf, mlme->wfd_probe_resp_ie, len);
	}
#endif

exit:
	return len;
}

u32 rtw_append_assoc_req_wfd_ie(_adapter *adapter, u8 *pbuf)
{
	struct wifidirect_info *wdinfo = &adapter->wdinfo;
	struct mlme_priv *mlme = &adapter->mlmepriv;
	u8 build_ie_by_self = 0;
	u32 len = 0;

	if (!hal_chk_wl_func(adapter, WL_FUNC_MIRACAST))
		goto exit;

#ifdef CONFIG_IOCTL_CFG80211
	if (_TRUE == wdinfo->wfd_info->wfd_enable)
#endif
		build_ie_by_self = 1;

	if (build_ie_by_self)
		len = build_assoc_req_wfd_ie(wdinfo, pbuf);
#ifdef CONFIG_IOCTL_CFG80211
	else if (mlme->wfd_assoc_req_ie && mlme->wfd_assoc_req_ie_len > 0) {
		len = mlme->wfd_assoc_req_ie_len;
		_rtw_memcpy(pbuf, mlme->wfd_assoc_req_ie, len);
	}
#endif

exit:
	return len;
}

u32 rtw_append_assoc_resp_wfd_ie(_adapter *adapter, u8 *pbuf)
{
	struct wifidirect_info *wdinfo = &adapter->wdinfo;
	struct mlme_priv *mlme = &adapter->mlmepriv;
	u8 build_ie_by_self = 0;
	u32 len = 0;

	if (!hal_chk_wl_func(adapter, WL_FUNC_MIRACAST))
		goto exit;

#ifdef CONFIG_IOCTL_CFG80211
	if (_TRUE == wdinfo->wfd_info->wfd_enable)
#endif
		build_ie_by_self = 1;

	if (build_ie_by_self)
		len = build_assoc_resp_wfd_ie(wdinfo, pbuf);
#ifdef CONFIG_IOCTL_CFG80211
	else if (mlme->wfd_assoc_resp_ie && mlme->wfd_assoc_resp_ie_len > 0) {
		len = mlme->wfd_assoc_resp_ie_len;
		_rtw_memcpy(pbuf, mlme->wfd_assoc_resp_ie, len);
	}
#endif

exit:
	return len;
}

#endif /* CONFIG_WFD */

void rtw_init_wifidirect_timers(_adapter *padapter)
{
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	rtw_init_timer(&pwdinfo->find_phase_timer, padapter, find_phase_timer_process, padapter);
	rtw_init_timer(&pwdinfo->restore_p2p_state_timer, padapter, restore_p2p_state_timer_process, padapter);
	rtw_init_timer(&pwdinfo->pre_tx_scan_timer, padapter, pre_tx_scan_timer_process, padapter);
	rtw_init_timer(&pwdinfo->reset_ch_sitesurvey, padapter, reset_ch_sitesurvey_timer_process, padapter);
	rtw_init_timer(&pwdinfo->reset_ch_sitesurvey2, padapter, reset_ch_sitesurvey_timer_process2, padapter);
#ifdef CONFIG_CONCURRENT_MODE
	rtw_init_timer(&pwdinfo->ap_p2p_switch_timer, padapter, ap_p2p_switch_timer_process, padapter);
#endif
}

void rtw_init_wifidirect_addrs(_adapter *padapter, u8 *dev_addr, u8 *iface_addr)
{
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	/*init device&interface address */
	if (dev_addr)
		_rtw_memcpy(pwdinfo->device_addr, dev_addr, ETH_ALEN);
	if (iface_addr)
		_rtw_memcpy(pwdinfo->interface_addr, iface_addr, ETH_ALEN);
#endif
}

void init_wifidirect_info(_adapter *padapter, enum P2P_ROLE role)
{
	struct wifidirect_info	*pwdinfo;
#ifdef CONFIG_WFD
	struct wifi_display_info	*pwfd_info = &padapter->wfd_info;
#endif
	pwdinfo = &padapter->wdinfo;

	pwdinfo->padapter = padapter;

	/*	1, 6, 11 are the social channel defined in the WiFi Direct specification. */
	pwdinfo->social_chan[0] = 1;
	pwdinfo->social_chan[1] = 6;
	pwdinfo->social_chan[2] = 11;
	pwdinfo->social_chan[3] = 0;	/*	channel 0 for scanning ending in site survey function. */

	if (role != P2P_ROLE_DISABLE
		&& pwdinfo->driver_interface != DRIVER_CFG80211
	) {
		#ifdef CONFIG_CONCURRENT_MODE
		u8 union_ch = 0;

		if (rtw_mi_check_status(padapter, MI_LINKED))
			union_ch = rtw_mi_get_union_chan(padapter);

		if (union_ch != 0 &&
			(union_ch == 1 || union_ch == 6 || union_ch == 11)
		) {
			/* Use the AP's channel as the listen channel */
			/* This will avoid the channel switch between AP's channel and listen channel */
			pwdinfo->listen_channel = union_ch;
		} else
		#endif /* CONFIG_CONCURRENT_MODE */
		{
			/* Use the channel 11 as the listen channel */
			pwdinfo->listen_channel = 11;
		}
	}

	if (role == P2P_ROLE_DEVICE) {
		rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_mi_check_status(padapter, MI_LINKED))
			rtw_p2p_set_state(pwdinfo, P2P_STATE_IDLE);
		else
#endif
			rtw_p2p_set_state(pwdinfo, P2P_STATE_LISTEN);

		pwdinfo->intent = 1;
		rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_LISTEN);
	} else if (role == P2P_ROLE_CLIENT) {
		rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
		pwdinfo->intent = 1;
		rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_GONEGO_OK);
	} else if (role == P2P_ROLE_GO) {
		rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
		pwdinfo->intent = 15;
		rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_GONEGO_OK);
	}

	/*	Use the OFDM rate in the P2P probe response frame. ( 6(B), 9(B), 12, 18, 24, 36, 48, 54 )	 */
	pwdinfo->support_rate[0] = 0x8c;	/*	6(B) */
	pwdinfo->support_rate[1] = 0x92;	/*	9(B) */
	pwdinfo->support_rate[2] = 0x18;	/*	12 */
	pwdinfo->support_rate[3] = 0x24;	/*	18 */
	pwdinfo->support_rate[4] = 0x30;	/*	24 */
	pwdinfo->support_rate[5] = 0x48;	/*	36 */
	pwdinfo->support_rate[6] = 0x60;	/*	48 */
	pwdinfo->support_rate[7] = 0x6c;	/*	54 */

	_rtw_memcpy((void *) pwdinfo->p2p_wildcard_ssid, "DIRECT-", 7);

	_rtw_memset(pwdinfo->device_name, 0x00, WPS_MAX_DEVICE_NAME_LEN);
	pwdinfo->device_name_len = 0;

	_rtw_memset(&pwdinfo->invitereq_info, 0x00, sizeof(struct tx_invite_req_info));
	pwdinfo->invitereq_info.token = 3;	/*	Token used for P2P invitation request frame. */

	_rtw_memset(&pwdinfo->inviteresp_info, 0x00, sizeof(struct tx_invite_resp_info));
	pwdinfo->inviteresp_info.token = 0;

	pwdinfo->profileindex = 0;
	_rtw_memset(&pwdinfo->profileinfo[0], 0x00, sizeof(struct profile_info) * P2P_MAX_PERSISTENT_GROUP_NUM);

	rtw_p2p_findphase_ex_set(pwdinfo, P2P_FINDPHASE_EX_NONE);

	pwdinfo->listen_dwell = (u8)((rtw_get_current_time() % 3) + 1);
	/* RTW_INFO( "[%s] listen_dwell time is %d00ms\n", __FUNCTION__, pwdinfo->listen_dwell ); */

	_rtw_memset(&pwdinfo->tx_prov_disc_info, 0x00, sizeof(struct tx_provdisc_req_info));
	pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_NONE;

	_rtw_memset(&pwdinfo->nego_req_info, 0x00, sizeof(struct tx_nego_req_info));

	pwdinfo->device_password_id_for_nego = WPS_DPID_PBC;
	pwdinfo->negotiation_dialog_token = 1;

	_rtw_memset(pwdinfo->nego_ssid, 0x00, WLAN_SSID_MAXLEN);
	pwdinfo->nego_ssidlen = 0;

	pwdinfo->ui_got_wps_info = P2P_NO_WPSINFO;
#ifdef CONFIG_WFD
	pwdinfo->supported_wps_cm = WPS_CONFIG_METHOD_DISPLAY  | WPS_CONFIG_METHOD_PBC;
	pwdinfo->wfd_info = pwfd_info;
#else
	pwdinfo->supported_wps_cm = WPS_CONFIG_METHOD_DISPLAY | WPS_CONFIG_METHOD_PBC | WPS_CONFIG_METHOD_KEYPAD;
#endif /* CONFIG_WFD */
	pwdinfo->channel_list_attr_len = 0;
	_rtw_memset(pwdinfo->channel_list_attr, 0x00, 100);

	_rtw_memset(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, 0x00, 4);
	_rtw_memset(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, '0', 3);
	_rtw_memset(&pwdinfo->groupid_info, 0x00, sizeof(struct group_id_info));
#ifdef CONFIG_CONCURRENT_MODE
#ifdef CONFIG_IOCTL_CFG80211
	pwdinfo->ext_listen_interval = 1000; /* The interval to be available with legacy AP during p2p0-find/scan */
	pwdinfo->ext_listen_period = 3000; /* The time period to be available for P2P during nego */
#else /* !CONFIG_IOCTL_CFG80211 */
	/* pwdinfo->ext_listen_interval = 3000; */
	/* pwdinfo->ext_listen_period = 400; */
	pwdinfo->ext_listen_interval = 1000;
	pwdinfo->ext_listen_period = 1000;
#endif /* !CONFIG_IOCTL_CFG80211 */
#endif

	/* Commented by Kurt 20130319
	 * For WiDi purpose: Use CFG80211 interface but controled WFD/RDS frame by driver itself. */
#ifdef CONFIG_IOCTL_CFG80211
	pwdinfo->driver_interface = DRIVER_CFG80211;
#else
	pwdinfo->driver_interface = DRIVER_WEXT;
#endif /* CONFIG_IOCTL_CFG80211 */

	pwdinfo->wfd_tdls_enable = 0;
	_rtw_memset(pwdinfo->p2p_peer_interface_addr, 0x00, ETH_ALEN);
	_rtw_memset(pwdinfo->p2p_peer_device_addr, 0x00, ETH_ALEN);

	pwdinfo->rx_invitereq_info.operation_ch[0] = 0;
	pwdinfo->rx_invitereq_info.operation_ch[1] = 0;	/*	Used to indicate the scan end in site survey function */
#ifdef CONFIG_P2P_OP_CHK_SOCIAL_CH
	pwdinfo->rx_invitereq_info.operation_ch[2] = 0;
	pwdinfo->rx_invitereq_info.operation_ch[3] = 0;
	pwdinfo->rx_invitereq_info.operation_ch[4] = 0;
#endif /* CONFIG_P2P_OP_CHK_SOCIAL_CH */
	pwdinfo->rx_invitereq_info.scan_op_ch_only = 0;
	pwdinfo->p2p_info.operation_ch[0] = 0;
	pwdinfo->p2p_info.operation_ch[1] = 0;			/*	Used to indicate the scan end in site survey function */
#ifdef CONFIG_P2P_OP_CHK_SOCIAL_CH
	pwdinfo->p2p_info.operation_ch[2] = 0;
	pwdinfo->p2p_info.operation_ch[3] = 0;
	pwdinfo->p2p_info.operation_ch[4] = 0;
#endif /* CONFIG_P2P_OP_CHK_SOCIAL_CH */
	pwdinfo->p2p_info.scan_op_ch_only = 0;
}

void _rtw_p2p_set_role(struct wifidirect_info *wdinfo, enum P2P_ROLE role)
{
	if (wdinfo->role != role) {
		wdinfo->role = role;
		rtw_mi_update_iface_status(&(wdinfo->padapter->mlmepriv), 0);
	}
}

#ifdef CONFIG_DBG_P2P

/**
 * rtw_p2p_role_txt - Get the p2p role name as a text string
 * @role: P2P role
 * Returns: The state name as a printable text string
 */
const char *rtw_p2p_role_txt(enum P2P_ROLE role)
{
	switch (role) {
	case P2P_ROLE_DISABLE:
		return "P2P_ROLE_DISABLE";
	case P2P_ROLE_DEVICE:
		return "P2P_ROLE_DEVICE";
	case P2P_ROLE_CLIENT:
		return "P2P_ROLE_CLIENT";
	case P2P_ROLE_GO:
		return "P2P_ROLE_GO";
	default:
		return "UNKNOWN";
	}
}

/**
 * rtw_p2p_state_txt - Get the p2p state name as a text string
 * @state: P2P state
 * Returns: The state name as a printable text string
 */
const char *rtw_p2p_state_txt(enum P2P_STATE state)
{
	switch (state) {
	case P2P_STATE_NONE:
		return "P2P_STATE_NONE";
	case P2P_STATE_IDLE:
		return "P2P_STATE_IDLE";
	case P2P_STATE_LISTEN:
		return "P2P_STATE_LISTEN";
	case P2P_STATE_SCAN:
		return "P2P_STATE_SCAN";
	case P2P_STATE_FIND_PHASE_LISTEN:
		return "P2P_STATE_FIND_PHASE_LISTEN";
	case P2P_STATE_FIND_PHASE_SEARCH:
		return "P2P_STATE_FIND_PHASE_SEARCH";
	case P2P_STATE_TX_PROVISION_DIS_REQ:
		return "P2P_STATE_TX_PROVISION_DIS_REQ";
	case P2P_STATE_RX_PROVISION_DIS_RSP:
		return "P2P_STATE_RX_PROVISION_DIS_RSP";
	case P2P_STATE_RX_PROVISION_DIS_REQ:
		return "P2P_STATE_RX_PROVISION_DIS_REQ";
	case P2P_STATE_GONEGO_ING:
		return "P2P_STATE_GONEGO_ING";
	case P2P_STATE_GONEGO_OK:
		return "P2P_STATE_GONEGO_OK";
	case P2P_STATE_GONEGO_FAIL:
		return "P2P_STATE_GONEGO_FAIL";
	case P2P_STATE_RECV_INVITE_REQ_MATCH:
		return "P2P_STATE_RECV_INVITE_REQ_MATCH";
	case P2P_STATE_PROVISIONING_ING:
		return "P2P_STATE_PROVISIONING_ING";
	case P2P_STATE_PROVISIONING_DONE:
		return "P2P_STATE_PROVISIONING_DONE";
	case P2P_STATE_TX_INVITE_REQ:
		return "P2P_STATE_TX_INVITE_REQ";
	case P2P_STATE_RX_INVITE_RESP_OK:
		return "P2P_STATE_RX_INVITE_RESP_OK";
	case P2P_STATE_RECV_INVITE_REQ_DISMATCH:
		return "P2P_STATE_RECV_INVITE_REQ_DISMATCH";
	case P2P_STATE_RECV_INVITE_REQ_GO:
		return "P2P_STATE_RECV_INVITE_REQ_GO";
	case P2P_STATE_RECV_INVITE_REQ_JOIN:
		return "P2P_STATE_RECV_INVITE_REQ_JOIN";
	case P2P_STATE_RX_INVITE_RESP_FAIL:
		return "P2P_STATE_RX_INVITE_RESP_FAIL";
	case P2P_STATE_RX_INFOR_NOREADY:
		return "P2P_STATE_RX_INFOR_NOREADY";
	case P2P_STATE_TX_INFOR_NOREADY:
		return "P2P_STATE_TX_INFOR_NOREADY";
	default:
		return "UNKNOWN";
	}
}

void dbg_rtw_p2p_set_state(struct wifidirect_info *wdinfo, enum P2P_STATE state, const char *caller, int line)
{
	if (!_rtw_p2p_chk_state(wdinfo, state)) {
		enum P2P_STATE old_state = _rtw_p2p_state(wdinfo);
		_rtw_p2p_set_state(wdinfo, state);
		RTW_INFO("[CONFIG_DBG_P2P]%s:%d set_state from %s to %s\n", caller, line
			, rtw_p2p_state_txt(old_state), rtw_p2p_state_txt(_rtw_p2p_state(wdinfo))
			);
	} else {
		RTW_INFO("[CONFIG_DBG_P2P]%s:%d set_state to same state %s\n", caller, line
			 , rtw_p2p_state_txt(_rtw_p2p_state(wdinfo))
			);
	}
}
void dbg_rtw_p2p_set_pre_state(struct wifidirect_info *wdinfo, enum P2P_STATE state, const char *caller, int line)
{
	if (_rtw_p2p_pre_state(wdinfo) != state) {
		enum P2P_STATE old_state = _rtw_p2p_pre_state(wdinfo);
		_rtw_p2p_set_pre_state(wdinfo, state);
		RTW_INFO("[CONFIG_DBG_P2P]%s:%d set_pre_state from %s to %s\n", caller, line
			, rtw_p2p_state_txt(old_state), rtw_p2p_state_txt(_rtw_p2p_pre_state(wdinfo))
			);
	} else {
		RTW_INFO("[CONFIG_DBG_P2P]%s:%d set_pre_state to same state %s\n", caller, line
			 , rtw_p2p_state_txt(_rtw_p2p_pre_state(wdinfo))
			);
	}
}
#if 0
void dbg_rtw_p2p_restore_state(struct wifidirect_info *wdinfo, const char *caller, int line)
{
	if (wdinfo->pre_p2p_state != -1) {
		RTW_INFO("[CONFIG_DBG_P2P]%s:%d restore from %s to %s\n", caller, line
			, p2p_state_str[wdinfo->p2p_state], p2p_state_str[wdinfo->pre_p2p_state]
			);
		_rtw_p2p_restore_state(wdinfo);
	} else {
		RTW_INFO("[CONFIG_DBG_P2P]%s:%d restore no pre state, cur state %s\n", caller, line
			 , p2p_state_str[wdinfo->p2p_state]
			);
	}
}
#endif
void dbg_rtw_p2p_set_role(struct wifidirect_info *wdinfo, enum P2P_ROLE role, const char *caller, int line)
{
	if (wdinfo->role != role) {
		enum P2P_ROLE old_role = wdinfo->role;
		_rtw_p2p_set_role(wdinfo, role);
		RTW_INFO("[CONFIG_DBG_P2P]%s:%d set_role from %s to %s\n", caller, line
			, rtw_p2p_role_txt(old_role), rtw_p2p_role_txt(wdinfo->role)
			);
	} else {
		RTW_INFO("[CONFIG_DBG_P2P]%s:%d set_role to same role %s\n", caller, line
			 , rtw_p2p_role_txt(wdinfo->role)
			);
	}
}
#endif /* CONFIG_DBG_P2P */


int rtw_p2p_enable(_adapter *padapter, enum P2P_ROLE role)
{
	int ret = _SUCCESS;
	struct wifidirect_info *pwdinfo = &(padapter->wdinfo);

	if (role == P2P_ROLE_DEVICE || role == P2P_ROLE_CLIENT || role == P2P_ROLE_GO) {
#if defined(CONFIG_CONCURRENT_MODE) && (!defined(RTW_P2P_GROUP_INTERFACE) || !RTW_P2P_GROUP_INTERFACE)
		/*	Commented by Albert 2011/12/30 */
		/*	The driver just supports 1 P2P group operation. */
		/*	So, this function will do nothing if the buddy adapter had enabled the P2P function. */
		/*if(!rtw_p2p_chk_state(pbuddy_wdinfo, P2P_STATE_NONE))
			return ret;*/
		/* Only selected interface can be P2P interface */
		if (padapter->iface_id != padapter->registrypriv.sel_p2p_iface) {
			RTW_ERR("%s, iface_id:%d is not P2P interface!\n", __func__, padapter->iface_id);
			ret = _FAIL;
			return ret;
		}
#endif /* CONFIG_CONCURRENT_MODE */

		/* leave IPS/Autosuspend */
		if (_FAIL == rtw_pwr_wakeup(padapter)) {
			ret = _FAIL;
			goto exit;
		}

		/*	Added by Albert 2011/03/22 */
		/*	In the P2P mode, the driver should not support the b mode. */
		/*	So, the Tx packet shouldn't use the CCK rate */
		#ifdef CONFIG_IOCTL_CFG80211
		if (rtw_cfg80211_iface_has_p2p_group_cap(padapter))
		#endif
			update_tx_basic_rate(padapter, WIRELESS_11AGN);

		/* Enable P2P function */
		init_wifidirect_info(padapter, role);

		#ifdef CONFIG_IOCTL_CFG80211
		if (padapter->wdinfo.driver_interface == DRIVER_CFG80211)
			adapter_wdev_data(padapter)->p2p_enabled = _TRUE;
		#endif

		rtw_hal_set_odm_var(padapter, HAL_ODM_P2P_STATE, NULL, _TRUE);
#ifdef CONFIG_WFD
		if (hal_chk_wl_func(padapter, WL_FUNC_MIRACAST))
			rtw_hal_set_odm_var(padapter, HAL_ODM_WIFI_DISPLAY_STATE, NULL, _TRUE);
#endif

	} else if (role == P2P_ROLE_DISABLE) {

		#ifdef CONFIG_IOCTL_CFG80211
		if (padapter->wdinfo.driver_interface == DRIVER_CFG80211)
			adapter_wdev_data(padapter)->p2p_enabled = _FALSE;
		#endif

		pwdinfo->listen_channel = 0;

		/* Disable P2P function */
		if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
			_cancel_timer_ex(&pwdinfo->find_phase_timer);
			_cancel_timer_ex(&pwdinfo->restore_p2p_state_timer);
			_cancel_timer_ex(&pwdinfo->pre_tx_scan_timer);
			_cancel_timer_ex(&pwdinfo->reset_ch_sitesurvey);
			_cancel_timer_ex(&pwdinfo->reset_ch_sitesurvey2);
			reset_ch_sitesurvey_timer_process(padapter);
			reset_ch_sitesurvey_timer_process2(padapter);
#ifdef CONFIG_CONCURRENT_MODE
			_cancel_timer_ex(&pwdinfo->ap_p2p_switch_timer);
#endif
			rtw_p2p_set_state(pwdinfo, P2P_STATE_NONE);
			rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_NONE);
			rtw_p2p_set_role(pwdinfo, P2P_ROLE_DISABLE);
			_rtw_memset(&pwdinfo->rx_prov_disc_info, 0x00, sizeof(struct rx_provdisc_req_info));

			/* Remove profiles in wifidirect_info structure. */
			_rtw_memset(&pwdinfo->profileinfo[0], 0x00, sizeof(struct profile_info) * P2P_MAX_PERSISTENT_GROUP_NUM);
			pwdinfo->profileindex = 0;
		}

		rtw_hal_set_odm_var(padapter, HAL_ODM_P2P_STATE, NULL, _FALSE);
#ifdef CONFIG_WFD
		if (hal_chk_wl_func(padapter, WL_FUNC_MIRACAST))
			rtw_hal_set_odm_var(padapter, HAL_ODM_WIFI_DISPLAY_STATE, NULL, _FALSE);
#endif

		if (_FAIL == rtw_pwr_wakeup(padapter)) {
			ret = _FAIL;
			goto exit;
		}

		/* Restore to initial setting. */
		update_tx_basic_rate(padapter, padapter->registrypriv.wireless_mode);

		/* For WiDi purpose. */
#ifdef CONFIG_IOCTL_CFG80211
		pwdinfo->driver_interface = DRIVER_CFG80211;
#else
		pwdinfo->driver_interface = DRIVER_WEXT;
#endif /* CONFIG_IOCTL_CFG80211 */

	}

exit:
	return ret;
}

#endif /* CONFIG_P2P */
