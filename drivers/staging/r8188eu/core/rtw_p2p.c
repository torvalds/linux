// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _RTW_P2P_C_

#include "../include/drv_types.h"
#include "../include/rtw_p2p.h"
#include "../include/wifi.h"

#ifdef CONFIG_88EU_P2P

static int rtw_p2p_is_channel_list_ok(u8 desired_ch, u8 *ch_list, u8 ch_cnt)
{
	int found = 0, i = 0;

	for (i = 0; i < ch_cnt; i++) {
		if (ch_list[i] == desired_ch) {
			found = 1;
			break;
		}
	}
	return found;
}

static u32 go_add_group_info_attr(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	struct list_head *phead, *plist;
	u32 len = 0;
	u16 attr_len = 0;
	u8 tmplen, *pdata_attr, *pstart, *pcur;
	struct sta_info *psta = NULL;
	struct adapter *padapter = pwdinfo->padapter;
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_88E("%s\n", __func__);

	pdata_attr = kzalloc(MAX_P2P_IE_LEN, GFP_KERNEL);

	pstart = pdata_attr;
	pcur = pdata_attr;

	spin_lock_bh(&pstapriv->asoc_list_lock);
	phead = &pstapriv->asoc_list;
	plist = phead->next;

	/* look up sta asoc_queue */
	while (phead != plist) {
		psta = container_of(plist, struct sta_info, asoc_list);

		plist = plist->next;

		if (psta->is_p2p_device) {
			tmplen = 0;

			pcur++;

			/* P2P device address */
			memcpy(pcur, psta->dev_addr, ETH_ALEN);
			pcur += ETH_ALEN;

			/* P2P interface address */
			memcpy(pcur, psta->hwaddr, ETH_ALEN);
			pcur += ETH_ALEN;

			*pcur = psta->dev_cap;
			pcur++;

			/* u16*)(pcur) = cpu_to_be16(psta->config_methods); */
			RTW_PUT_BE16(pcur, psta->config_methods);
			pcur += 2;

			memcpy(pcur, psta->primary_dev_type, 8);
			pcur += 8;

			*pcur = psta->num_of_secdev_type;
			pcur++;

			memcpy(pcur, psta->secdev_types_list, psta->num_of_secdev_type * 8);
			pcur += psta->num_of_secdev_type * 8;

			if (psta->dev_name_len > 0) {
				/* u16*)(pcur) = cpu_to_be16(WPS_ATTR_DEVICE_NAME); */
				RTW_PUT_BE16(pcur, WPS_ATTR_DEVICE_NAME);
				pcur += 2;

				/* u16*)(pcur) = cpu_to_be16(psta->dev_name_len); */
				RTW_PUT_BE16(pcur, psta->dev_name_len);
				pcur += 2;

				memcpy(pcur, psta->dev_name, psta->dev_name_len);
				pcur += psta->dev_name_len;
			}

			tmplen = (u8)(pcur - pstart);

			*pstart = (tmplen - 1);

			attr_len += tmplen;

			/* pstart += tmplen; */
			pstart = pcur;
		}
	}
	spin_unlock_bh(&pstapriv->asoc_list_lock);

	if (attr_len > 0)
		len = rtw_set_p2p_attr_content(pbuf, P2P_ATTR_GROUP_INFO, attr_len, pdata_attr);

	kfree(pdata_attr);
	return len;
}

static void issue_group_disc_req(struct wifidirect_info *pwdinfo, u8 *da)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	__le16 *fctrl;
	struct adapter *padapter = pwdinfo->padapter;
	struct xmit_priv			*pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	unsigned char category = RTW_WLAN_CATEGORY_P2P;/* P2P action frame */
	__be32	p2poui = cpu_to_be32(P2POUI);
	u8	oui_subtype = P2P_GO_DISC_REQUEST;
	u8	dialogToken = 0;

	DBG_88E("[%s]\n", __func__);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	memcpy(pwlanhdr->addr2, pwdinfo->interface_addr, ETH_ALEN);
	memcpy(pwlanhdr->addr3, pwdinfo->interface_addr, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	/* Build P2P action frame header */
	pframe = rtw_set_fixed_ie(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *)&p2poui, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 1, &oui_subtype, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 1, &dialogToken, &pattrib->pktlen);

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
	__le16 *fctrl;
	struct adapter *padapter = pwdinfo->padapter;
	struct xmit_priv			*pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	__be32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_DEVDISC_RESP;
	u8 p2pie[8] = { 0x00 };
	u32 p2pielen = 0;

	DBG_88E("[%s]\n", __func__);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	memcpy(pwlanhdr->addr2, pwdinfo->device_addr, ETH_ALEN);
	memcpy(pwlanhdr->addr3, pwdinfo->device_addr, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	/* Build P2P public action frame header */
	pframe = rtw_set_fixed_ie(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 1, &action, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *)&p2poui, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 1, &oui_subtype, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 1, &dialogToken, &pattrib->pktlen);

	/* Build P2P IE */
	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*  P2P_ATTR_STATUS */
	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_STATUS, 1, &status);

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, p2pie, &pattrib->pktlen);

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);
}

static void issue_p2p_provision_resp(struct wifidirect_info *pwdinfo, u8 *raddr, u8 *frame_body, u16 config_method)
{
	struct adapter *padapter = pwdinfo->padapter;
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u8			dialogToken = frame_body[7];	/*	The Dialog Token of provisioning discovery request frame. */
	__be32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_PROVISION_DISC_RESP;
	u8			wpsie[100] = { 0x00 };
	u8			wpsielen = 0;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	__le16 *fctrl;
	struct xmit_priv			*pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	memcpy(pwlanhdr->addr1, raddr, ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&padapter->eeprompriv), ETH_ALEN);
	memcpy(pwlanhdr->addr3, myid(&padapter->eeprompriv), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 1, &action, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *)&p2poui, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 1, &oui_subtype, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 1, &dialogToken, &pattrib->pktlen);

	wpsielen = 0;
	/*	WPS OUI */
	RTW_PUT_BE32(wpsie, WPSOUI);
	wpsielen += 4;

	/*	Config Method */
	/*	Type: */
	RTW_PUT_BE16(wpsie + wpsielen, WPS_ATTR_CONF_METHOD);
	wpsielen += 2;

	/*	Length: */
	RTW_PUT_BE16(wpsie + wpsielen, 0x0002);
	wpsielen += 2;

	/*	Value: */
	RTW_PUT_BE16(wpsie + wpsielen, config_method);
	wpsielen += 2;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *)wpsie, &pattrib->pktlen);

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);
}

static void issue_p2p_presence_resp(struct wifidirect_info *pwdinfo, u8 *da, u8 status, u8 dialogToken)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	__le16 *fctrl;
	struct adapter *padapter = pwdinfo->padapter;
	struct xmit_priv			*pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	unsigned char category = RTW_WLAN_CATEGORY_P2P;/* P2P action frame */
	__be32	p2poui = cpu_to_be32(P2POUI);
	u8	oui_subtype = P2P_PRESENCE_RESPONSE;
	u8 p2pie[MAX_P2P_IE_LEN] = { 0x00 };
	u8 noa_attr_content[32] = { 0x00 };
	u32 p2pielen = 0;

	DBG_88E("[%s]\n", __func__);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	memcpy(pwlanhdr->addr2, pwdinfo->interface_addr, ETH_ALEN);
	memcpy(pwlanhdr->addr3, pwdinfo->interface_addr, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	/* Build P2P action frame header */
	pframe = rtw_set_fixed_ie(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *)&p2poui, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 1, &oui_subtype, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 1, &dialogToken, &pattrib->pktlen);

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

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, p2pie, &pattrib->pktlen);

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);
}

u32 build_beacon_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf)
{
	u8 p2pie[MAX_P2P_IE_LEN] = { 0x00 };
	u16 capability = 0;
	u32 len = 0, p2pielen = 0;
	__le16 le_tmp;

	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*	According to the P2P Specification, the beacon frame should contain 3 P2P attributes */
	/*	1. P2P Capability */
	/*	2. P2P Device ID */
	/*	3. Notice of Absence (NOA) */

	/*	P2P Capability ATTR */
	/*	Type: */
	/*	Length: */
	/*	Value: */
	/*	Device Capability Bitmap, 1 byte */
	/*	Be able to participate in additional P2P Groups and */
	/*	support the P2P Invitation Procedure */
	/*	Group Capability Bitmap, 1 byte */
	capability = P2P_DEVCAP_INVITATION_PROC | P2P_DEVCAP_CLIENT_DISCOVERABILITY;
	capability |=  ((P2P_GRPCAP_GO | P2P_GRPCAP_INTRABSS) << 8);
	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_PROVISIONING_ING))
		capability |= (P2P_GRPCAP_GROUP_FORMATION << 8);

	le_tmp = cpu_to_le16(capability);
	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_CAPABILITY, 2, (u8 *)&le_tmp);

	/*  P2P Device ID ATTR */
	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_DEVICE_ID, ETH_ALEN, pwdinfo->device_addr);

	/*  Notice of Absence ATTR */
	/*	Type: */
	/*	Length: */
	/*	Value: */

	pbuf = rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *)p2pie, &len);
	return len;
}

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
	/*	3. Notice of Absence (NOA)	(Only GO needs this) */
	/*	4. Device Info */
	/*	5. Group Info	(Only GO need this) */

	/*	P2P Capability ATTR */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CAPABILITY;

	/*	Length: */
	/* u16*) (p2pie + p2pielen) = cpu_to_le16(0x0002); */
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
	/* u16*) (p2pie + p2pielen) = cpu_to_le16(0x0004); */
	RTW_PUT_LE16(p2pie + p2pielen, 0x0004);
	p2pielen += 2;

	/*	Value: */
	/*	Availability Period */
	/* u16*) (p2pie + p2pielen) = cpu_to_le16(0xFFFF); */
	RTW_PUT_LE16(p2pie + p2pielen, 0xFFFF);
	p2pielen += 2;

	/*	Availability Interval */
	/* u16*) (p2pie + p2pielen) = cpu_to_le16(0xFFFF); */
	RTW_PUT_LE16(p2pie + p2pielen, 0xFFFF);
	p2pielen += 2;

	/*  Notice of Absence ATTR */
	/*	Type: */
	/*	Length: */
	/*	Value: */

	/*	Device Info ATTR */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes) */
	/*	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes) */
	/* u16*) (p2pie + p2pielen) = cpu_to_le16(21 + pwdinfo->device_name_len); */
	RTW_PUT_LE16(p2pie + p2pielen, 21 + pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	/*	P2P Device Address */
	memcpy(p2pie + p2pielen, pwdinfo->device_addr, ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	Config Method */
	/*	This field should be big endian. Noted by P2P specification. */
	/* u16*) (p2pie + p2pielen) = cpu_to_be16(pwdinfo->supported_wps_cm); */
	RTW_PUT_BE16(p2pie + p2pielen, pwdinfo->supported_wps_cm);
	p2pielen += 2;

	/*	Primary Device Type */
	/*	Category ID */
	/* u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA); */
	RTW_PUT_BE16(p2pie + p2pielen, WPS_PDT_CID_MULIT_MEDIA);
	p2pielen += 2;

	/*	OUI */
	/* u32*) (p2pie + p2pielen) = cpu_to_be32(WPSOUI); */
	RTW_PUT_BE32(p2pie + p2pielen, WPSOUI);
	p2pielen += 4;

	/*	Sub Category ID */
	/* u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER); */
	RTW_PUT_BE16(p2pie + p2pielen, WPS_PDT_SCID_MEDIA_SERVER);
	p2pielen += 2;

	/*	Number of Secondary Device Types */
	p2pie[p2pielen++] = 0x00;	/*	No Secondary Device Type List */

	/*	Device Name */
	/*	Type: */
	/* u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_ATTR_DEVICE_NAME); */
	RTW_PUT_BE16(p2pie + p2pielen, WPS_ATTR_DEVICE_NAME);
	p2pielen += 2;

	/*	Length: */
	/* u16*) (p2pie + p2pielen) = cpu_to_be16(pwdinfo->device_name_len); */
	RTW_PUT_BE16(p2pie + p2pielen, pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	memcpy(p2pie + p2pielen, pwdinfo->device_name, pwdinfo->device_name_len);
	p2pielen += pwdinfo->device_name_len;

	/*  Group Info ATTR */
	/*	Type: */
	/*	Length: */
	/*	Value: */
	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
		p2pielen += go_add_group_info_attr(pwdinfo, p2pie + p2pielen);

	pbuf = rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *)p2pie, &len);

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
	/*	3. Group ID (When joining an operating P2P Group) */

	/*	P2P Capability ATTR */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CAPABILITY;

	/*	Length: */
	/* u16*) (p2pie + p2pielen) = cpu_to_le16(0x0002); */
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
	/*	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes) */
	/*	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes) */
	/* u16*) (p2pie + p2pielen) = cpu_to_le16(21 + pwdinfo->device_name_len); */
	RTW_PUT_LE16(p2pie + p2pielen, 21 + pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	/*	P2P Device Address */
	memcpy(p2pie + p2pielen, pwdinfo->device_addr, ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	Config Method */
	/*	This field should be big endian. Noted by P2P specification. */
	if (pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_PBC) {
		/* u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_CONFIG_METHOD_PBC); */
		RTW_PUT_BE16(p2pie + p2pielen, WPS_CONFIG_METHOD_PBC);
	} else {
		/* u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_CONFIG_METHOD_DISPLAY); */
		RTW_PUT_BE16(p2pie + p2pielen, WPS_CONFIG_METHOD_DISPLAY);
	}

	p2pielen += 2;

	/*	Primary Device Type */
	/*	Category ID */
	/* u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA); */
	RTW_PUT_BE16(p2pie + p2pielen, WPS_PDT_CID_MULIT_MEDIA);
	p2pielen += 2;

	/*	OUI */
	/* u32*) (p2pie + p2pielen) = cpu_to_be32(WPSOUI); */
	RTW_PUT_BE32(p2pie + p2pielen, WPSOUI);
	p2pielen += 4;

	/*	Sub Category ID */
	/* u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER); */
	RTW_PUT_BE16(p2pie + p2pielen, WPS_PDT_SCID_MEDIA_SERVER);
	p2pielen += 2;

	/*	Number of Secondary Device Types */
	p2pie[p2pielen++] = 0x00;	/*	No Secondary Device Type List */

	/*	Device Name */
	/*	Type: */
	/* u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_ATTR_DEVICE_NAME); */
	RTW_PUT_BE16(p2pie + p2pielen, WPS_ATTR_DEVICE_NAME);
	p2pielen += 2;

	/*	Length: */
	/* u16*) (p2pie + p2pielen) = cpu_to_be16(pwdinfo->device_name_len); */
	RTW_PUT_BE16(p2pie + p2pielen, pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	memcpy(p2pie + p2pielen, pwdinfo->device_name, pwdinfo->device_name_len);
	p2pielen += pwdinfo->device_name_len;

	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT)) {
		/*	Added by Albert 2011/05/19 */
		/*	In this case, the pdev_raddr is the device address of the group owner. */

		/*	P2P Group ID ATTR */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_GROUP_ID;

		/*	Length: */
		/* u16*) (p2pie + p2pielen) = cpu_to_le16(ETH_ALEN + ussidlen); */
		RTW_PUT_LE16(p2pie + p2pielen, ETH_ALEN + ussidlen);
		p2pielen += 2;

		/*	Value: */
		memcpy(p2pie + p2pielen, pdev_raddr, ETH_ALEN);
		p2pielen += ETH_ALEN;

		memcpy(p2pie + p2pielen, pssid, ussidlen);
		p2pielen += ussidlen;
	}

	pbuf = rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *)p2pie, &len);

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

	/*  According to the P2P Specification, the Association response frame should contain 2 P2P attributes */
	/*	1. Status */
	/*	2. Extended Listen Timing (optional) */

	/*	Status ATTR */
	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_STATUS, 1, &status_code);

	/*  Extended Listen Timing ATTR */
	/*	Type: */
	/*	Length: */
	/*	Value: */

	pbuf = rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *)p2pie, &len);

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
	u32 ret = false;
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
	/*	4. Destination Address. (Done in mgt_dispatcher function) */
	/*	5. Requested Device Type in WSC IE. (Todo) */
	/*	6. Device ID attribute in P2P IE. (Todo) */

	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_, _SSID_IE_, (int *)&ssid_len,
			len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_);

	ssid_len &= 0xff;	/*	Just last 1 byte is valid for ssid len of the probe request */
	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE) || rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
		p2pie = rtw_get_p2p_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_, len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_, NULL, &p2pielen);
		if (p2pie) {
			if (p && !memcmp((void *)(p + 2), (void *)pwdinfo->p2p_wildcard_ssid, 7)) {
				/* todo: */
				/* Check Requested Device Type attributes in WSC IE. */
				/* Check Device ID attribute in P2P IE */

				ret = true;
			} else if (p && ssid_len == 0) {
				ret = true;
			}
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
	__be16 be_tmp;
	__le16 le_tmp;

	if (!rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
		return P2P_STATUS_FAIL_REQUEST_UNABLE;

	frame_type = GetFrameSubType(pframe);
	if (frame_type == WIFI_ASSOCREQ)
		ie_offset = _ASOCREQ_IE_OFFSET_;
	else /*  WIFI_REASSOCREQ */
		ie_offset = _REASOCREQ_IE_OFFSET_;

	ies = pframe + WLAN_HDR_A3_LEN + ie_offset;
	ies_len = len - WLAN_HDR_A3_LEN - ie_offset;

	p2p_ie = rtw_get_p2p_ie(ies, ies_len, NULL, &p2p_ielen);

	if (!p2p_ie) {
		DBG_88E("[%s] P2P IE not Found!!\n", __func__);
		status_code =  P2P_STATUS_FAIL_INVALID_PARAM;
	} else {
		DBG_88E("[%s] P2P IE Found!!\n", __func__);
	}

	while (p2p_ie) {
		/* Check P2P Capability ATTR */
		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CAPABILITY, (u8 *)&le_tmp, (uint *)&attr_contentlen)) {
			DBG_88E("[%s] Got P2P Capability Attr!!\n", __func__);
			cap_attr = le16_to_cpu(le_tmp);
			psta->dev_cap = cap_attr & 0xff;
		}

		/* Check Extended Listen Timing ATTR */

		/* Check P2P Device Info ATTR */
		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_DEVICE_INFO, NULL, (uint *)&attr_contentlen)) {
			DBG_88E("[%s] Got P2P DEVICE INFO Attr!!\n", __func__);
			pattr_content = kzalloc(attr_contentlen, GFP_KERNEL);
			pbuf = pattr_content;
			if (pattr_content) {
				u8 num_of_secdev_type;
				u16 dev_name_len;

				rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_DEVICE_INFO, pattr_content, (uint *)&attr_contentlen);

				memcpy(psta->dev_addr,	pattr_content, ETH_ALEN);/* P2P Device Address */

				pattr_content += ETH_ALEN;

				memcpy(&be_tmp, pattr_content, 2);/* Config Methods */
				psta->config_methods = be16_to_cpu(be_tmp);

				pattr_content += 2;

				memcpy(psta->primary_dev_type, pattr_content, 8);

				pattr_content += 8;

				num_of_secdev_type = *pattr_content;
				pattr_content += 1;

				if (num_of_secdev_type == 0) {
					psta->num_of_secdev_type = 0;
				} else {
					u32 len;

					psta->num_of_secdev_type = num_of_secdev_type;

					len = (sizeof(psta->secdev_types_list) < (num_of_secdev_type * 8)) ?
					      (sizeof(psta->secdev_types_list)) : (num_of_secdev_type * 8);

					memcpy(psta->secdev_types_list, pattr_content, len);

					pattr_content += (num_of_secdev_type * 8);
				}

				psta->dev_name_len = 0;
				if (WPS_ATTR_DEVICE_NAME == be16_to_cpu(*(__be16 *)pattr_content)) {
					dev_name_len = be16_to_cpu(*(__be16 *)(pattr_content + 2));

					psta->dev_name_len = (sizeof(psta->dev_name) < dev_name_len) ? sizeof(psta->dev_name) : dev_name_len;

					memcpy(psta->dev_name, pattr_content + 4, psta->dev_name_len);
				}
				kfree(pbuf);
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
	struct adapter *padapter = pwdinfo->padapter;
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
			if (!memcmp(pwdinfo->device_addr, groupid, ETH_ALEN) &&
			    !memcmp(pwdinfo->p2p_group_ssid, groupid + ETH_ALEN, pwdinfo->p2p_group_ssid_len)) {
				attr_contentlen = 0;
				if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_DEVICE_ID, dev_addr, &attr_contentlen)) {
					struct list_head *phead, *plist;

					spin_lock_bh(&pstapriv->asoc_list_lock);
					phead = &pstapriv->asoc_list;
					plist = phead->next;

					/* look up sta asoc_queue */
					while (phead != plist) {
						psta = container_of(plist, struct sta_info, asoc_list);

						plist = plist->next;

						if (psta->is_p2p_device && (psta->dev_cap & P2P_DEVCAP_CLIENT_DISCOVERABILITY) &&
						    !memcmp(psta->dev_addr, dev_addr, ETH_ALEN)) {
							/* issue GO Discoverability Request */
							issue_group_disc_req(pwdinfo, psta->hwaddr);
							status = P2P_STATUS_SUCCESS;
							break;
						} else {
							status = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
						}
					}
					spin_unlock_bh(&pstapriv->asoc_list_lock);
				} else {
					status = P2P_STATUS_FAIL_INVALID_PARAM;
				}
			} else {
				status = P2P_STATUS_FAIL_INVALID_PARAM;
			}
		}
	}

	/* issue Device Discoverability Response */
	issue_p2p_devdisc_resp(pwdinfo, GetAddr2Ptr(pframe), status, dialogToken);

	return (status == P2P_STATUS_SUCCESS) ? true : false;
}

u32 process_p2p_devdisc_resp(struct wifidirect_info *pwdinfo, u8 *pframe, uint len)
{
	return true;
}

u8 process_p2p_provdisc_req(struct wifidirect_info *pwdinfo,  u8 *pframe, uint len)
{
	u8 *frame_body;
	u8 *wpsie;
	uint	wps_ielen = 0, attr_contentlen = 0;
	u16	uconfig_method = 0;
	__be16 be_tmp;

	frame_body = (pframe + sizeof(struct rtw_ieee80211_hdr_3addr));

	wpsie = rtw_get_wps_ie(frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &wps_ielen);
	if (wpsie) {
		if (rtw_get_wps_attr_content(wpsie, wps_ielen, WPS_ATTR_CONF_METHOD, (u8 *)&be_tmp, &attr_contentlen)) {
			uconfig_method = be16_to_cpu(be_tmp);
			switch (uconfig_method) {
			case WPS_CM_DISPLYA:
				memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "dis", 3);
				break;
			case WPS_CM_LABEL:
				memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "lab", 3);
				break;
			case WPS_CM_PUSH_BUTTON:
				memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pbc", 3);
				break;
			case WPS_CM_KEYPAD:
				memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pad", 3);
				break;
			}
			issue_p2p_provision_resp(pwdinfo, GetAddr2Ptr(pframe), frame_body, uconfig_method);
		}
	}
	DBG_88E("[%s] config method = %s\n", __func__, pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req);
	return true;
}

u8 process_p2p_provdisc_resp(struct wifidirect_info *pwdinfo,  u8 *pframe)
{
	return true;
}

static u8 rtw_p2p_get_peer_ch_list(struct wifidirect_info *pwdinfo, u8 *ch_content, u8 ch_cnt, u8 *peer_ch_list)
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
		ch_no += temp;
	}

	return ch_no;
}

static u8 rtw_p2p_ch_inclusion(struct mlme_ext_priv *pmlmeext, u8 *peer_ch_list, u8 peer_ch_num, u8 *ch_list_inclusioned)
{
	int	i = 0, j = 0, temp = 0;
	u8 ch_no = 0;

	for (i = 0; i < peer_ch_num; i++) {
		for (j = temp; j < pmlmeext->max_chan_nums; j++) {
			if (*(peer_ch_list + i) == pmlmeext->channel_set[j].ChannelNum) {
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
	struct adapter *padapter = pwdinfo->padapter;
	u8	result = P2P_STATUS_SUCCESS;
	u32	p2p_ielen = 0, wps_ielen = 0;
	u8 *ies;
	u32 ies_len;
	u8 *p2p_ie;
	u8 *wpsie;
	u16		wps_devicepassword_id = 0x0000;
	uint	wps_devicepassword_id_len = 0;
	__be16 be_tmp;

	wpsie = rtw_get_wps_ie(pframe + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &wps_ielen);
	if (wpsie) {
		/*	Commented by Kurt 20120113 */
		/*	If some device wants to do p2p handshake without sending prov_disc_req */
		/*	We have to get peer_req_cm from here. */
		if (!memcmp(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "000", 3)) {
			rtw_get_wps_attr_content(wpsie, wps_ielen, WPS_ATTR_DEVICE_PWID, (u8 *)&be_tmp, &wps_devicepassword_id_len);
			wps_devicepassword_id = be16_to_cpu(be_tmp);

			if (wps_devicepassword_id == WPS_DPID_USER_SPEC)
				memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "dis", 3);
			else if (wps_devicepassword_id == WPS_DPID_REGISTRAR_SPEC)
				memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pad", 3);
			else
				memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pbc", 3);
		}
	} else {
		DBG_88E("[%s] WPS IE not Found!!\n", __func__);
		result = P2P_STATUS_FAIL_INCOMPATIBLE_PARAM;
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
		return result;
	}

	if (pwdinfo->ui_got_wps_info == P2P_NO_WPSINFO) {
		result = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
		rtw_p2p_set_state(pwdinfo, P2P_STATE_TX_INFOR_NOREADY);
		return result;
	}

	ies = pframe + _PUBLIC_ACTION_IE_OFFSET_;
	ies_len = len - _PUBLIC_ACTION_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie(ies, ies_len, NULL, &p2p_ielen);

	if (!p2p_ie) {
		DBG_88E("[%s] P2P IE not Found!!\n", __func__);
		result = P2P_STATUS_FAIL_INCOMPATIBLE_PARAM;
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
	}

	while (p2p_ie) {
		u8	attr_content = 0x00;
		u32	attr_contentlen = 0;
		u8	ch_content[50] = { 0x00 };
		uint	ch_cnt = 0;
		u8	peer_ch_list[50] = { 0x00 };
		u8	peer_ch_num = 0;
		u8	ch_list_inclusioned[50] = { 0x00 };
		u8	ch_num_inclusioned = 0;

		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_ING);

		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GO_INTENT, &attr_content, &attr_contentlen)) {
			DBG_88E("[%s] GO Intent = %d, tie = %d\n", __func__, attr_content >> 1, attr_content & 0x01);
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
			} else if (pwdinfo->intent > (pwdinfo->peer_intent >> 1)) {
				rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
			} else {
				rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
			}

			if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
				/*	Store the group id information. */
				memcpy(pwdinfo->groupid_info.go_device_addr, pwdinfo->device_addr, ETH_ALEN);
				memcpy(pwdinfo->groupid_info.ssid, pwdinfo->nego_ssid, pwdinfo->nego_ssidlen);
			}
		}

		attr_contentlen = 0;
		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_INTENTED_IF_ADDR, pwdinfo->p2p_peer_interface_addr, &attr_contentlen)) {
			if (attr_contentlen != ETH_ALEN)
				memset(pwdinfo->p2p_peer_interface_addr, 0x00, ETH_ALEN);
		}

		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, ch_content, &ch_cnt)) {
			peer_ch_num = rtw_p2p_get_peer_ch_list(pwdinfo, ch_content, ch_cnt, peer_ch_list);
			ch_num_inclusioned = rtw_p2p_ch_inclusion(&padapter->mlmeextpriv, peer_ch_list, peer_ch_num, ch_list_inclusioned);

			if (ch_num_inclusioned == 0) {
				DBG_88E("[%s] No common channel in channel list!\n", __func__);
				result = P2P_STATUS_FAIL_NO_COMMON_CH;
				rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
				break;
			}

			if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
				if (!rtw_p2p_is_channel_list_ok(pwdinfo->operating_channel,
				    ch_list_inclusioned, ch_num_inclusioned)) {
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
						DBG_88E("[%s] Change op ch to %02x as peer's\n", __func__, pwdinfo->operating_channel);
					} else {
						/*  Take first channel of ch_list_inclusioned as operating channel */
						pwdinfo->operating_channel = ch_list_inclusioned[0];
						DBG_88E("[%s] Change op ch to %02x\n", __func__, pwdinfo->operating_channel);
					}
				}
			}
		}

		/* Get the next P2P IE */
		p2p_ie = rtw_get_p2p_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen), NULL, &p2p_ielen);
	}
	return result;
}

u8 process_p2p_group_negotation_resp(struct wifidirect_info *pwdinfo, u8 *pframe, uint len)
{
	struct adapter *padapter = pwdinfo->padapter;
	u8	result = P2P_STATUS_SUCCESS;
	u32	p2p_ielen, wps_ielen;
	u8 *ies;
	u32 ies_len;
	u8 *p2p_ie;

	ies = pframe + _PUBLIC_ACTION_IE_OFFSET_;
	ies_len = len - _PUBLIC_ACTION_IE_OFFSET_;

	/*	Be able to know which one is the P2P GO and which one is P2P client. */

	if (rtw_get_wps_ie(ies, ies_len, NULL, &wps_ielen)) {
	} else {
		DBG_88E("[%s] WPS IE not Found!!\n", __func__);
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
		u8	peer_ch_list[50] = { 0x00 };
		u8	peer_ch_num = 0;
		u8	ch_list_inclusioned[50] = { 0x00 };
		u8	ch_num_inclusioned = 0;

		while (p2p_ie) {	/*	Found the P2P IE. */
			rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, &attr_content, &attr_contentlen);
			if (attr_contentlen == 1) {
				DBG_88E("[%s] Status = %d\n", __func__, attr_content);
				if (attr_content == P2P_STATUS_SUCCESS) {
					/*	Do nothing. */
				} else {
					if (P2P_STATUS_FAIL_INFO_UNAVAILABLE == attr_content) {
						rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_INFOR_NOREADY);
					} else {
						rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
					}
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
					result = attr_content;
					break;
				}
			}

			/*	Try to get the peer's interface address */
			attr_contentlen = 0;
			if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_INTENTED_IF_ADDR, pwdinfo->p2p_peer_interface_addr, &attr_contentlen)) {
				if (attr_contentlen != ETH_ALEN)
					memset(pwdinfo->p2p_peer_interface_addr, 0x00, ETH_ALEN);
			}

			/*	Try to get the peer's intent and tie breaker value. */
			attr_content = 0x00;
			attr_contentlen = 0;
			if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GO_INTENT, &attr_content, &attr_contentlen)) {
				DBG_88E("[%s] GO Intent = %d, tie = %d\n", __func__, attr_content >> 1, attr_content & 0x01);
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
					memcpy(pwdinfo->groupid_info.go_device_addr, pwdinfo->device_addr, ETH_ALEN);
					memcpy(pwdinfo->groupid_info.ssid, pwdinfo->nego_ssid, pwdinfo->nego_ssidlen);
				}
			}

			/*	Try to get the operation channel information */

			attr_contentlen = 0;
			if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, operatingch_info, &attr_contentlen)) {
				DBG_88E("[%s] Peer's operating channel = %d\n", __func__, operatingch_info[4]);
				pwdinfo->peer_operating_ch = operatingch_info[4];
			}

			/*	Try to get the channel list information */
			if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, pwdinfo->channel_list_attr, &pwdinfo->channel_list_attr_len)) {
				DBG_88E("[%s] channel list attribute found, len = %d\n", __func__,  pwdinfo->channel_list_attr_len);

				peer_ch_num = rtw_p2p_get_peer_ch_list(pwdinfo, pwdinfo->channel_list_attr, pwdinfo->channel_list_attr_len, peer_ch_list);
				ch_num_inclusioned = rtw_p2p_ch_inclusion(&padapter->mlmeextpriv, peer_ch_list, peer_ch_num, ch_list_inclusioned);

				if (ch_num_inclusioned == 0) {
					DBG_88E("[%s] No common channel in channel list!\n", __func__);
					result = P2P_STATUS_FAIL_NO_COMMON_CH;
					rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
					break;
				}

				if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
					if (!rtw_p2p_is_channel_list_ok(pwdinfo->operating_channel,
					    ch_list_inclusioned, ch_num_inclusioned)) {
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
							DBG_88E("[%s] Change op ch to %02x as peer's\n", __func__, pwdinfo->operating_channel);
						} else {
							/*  Take first channel of ch_list_inclusioned as operating channel */
							pwdinfo->operating_channel = ch_list_inclusioned[0];
							DBG_88E("[%s] Change op ch to %02x\n", __func__, pwdinfo->operating_channel);
						}
					}
				}
			} else {
				DBG_88E("[%s] channel list attribute not found!\n", __func__);
			}

			/*	Try to get the group id information if peer is GO */
			attr_contentlen = 0;
			memset(groupid, 0x00, 38);
			if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GROUP_ID, groupid, &attr_contentlen)) {
				memcpy(pwdinfo->groupid_info.go_device_addr, &groupid[0], ETH_ALEN);
				memcpy(pwdinfo->groupid_info.ssid, &groupid[6], attr_contentlen - ETH_ALEN);
			}

			/* Get the next P2P IE */
			p2p_ie = rtw_get_p2p_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen), NULL, &p2p_ielen);
		}
	}
	return result;
}

u8 process_p2p_group_negotation_confirm(struct wifidirect_info *pwdinfo, u8 *pframe, uint len)
{
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
			DBG_88E("[%s] Status = %d\n", __func__, attr_content);
			result = attr_content;

			if (attr_content == P2P_STATUS_SUCCESS) {
				u8	bcancelled = 0;

				_cancel_timer(&pwdinfo->restore_p2p_state_timer, &bcancelled);

				/*	Commented by Albert 20100911 */
				/*	Todo: Need to handle the case which both Intents are the same. */
				rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
				rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_GONEGO_OK);
				if ((pwdinfo->intent) > (pwdinfo->peer_intent >> 1)) {
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
				} else if ((pwdinfo->intent) < (pwdinfo->peer_intent >> 1)) {
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
				} else {
					/*	Have to compare the Tie Breaker */
					if (pwdinfo->peer_intent & 0x01)
						rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
					else
						rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
				}
			} else {
				rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
				rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
				break;
			}
		}

		/*	Try to get the group id information */
		attr_contentlen = 0;
		memset(groupid, 0x00, 38);
		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GROUP_ID, groupid, &attr_contentlen)) {
			DBG_88E("[%s] Ssid = %s, ssidlen = %zu\n", __func__, &groupid[ETH_ALEN], strlen(&groupid[ETH_ALEN]));
			memcpy(pwdinfo->groupid_info.go_device_addr, &groupid[0], ETH_ALEN);
			memcpy(pwdinfo->groupid_info.ssid, &groupid[6], attr_contentlen - ETH_ALEN);
		}

		attr_contentlen = 0;
		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, operatingch_info, &attr_contentlen)) {
			DBG_88E("[%s] Peer's operating channel = %d\n", __func__, operatingch_info[4]);
			pwdinfo->peer_operating_ch = operatingch_info[4];
		}

		/* Get the next P2P IE */
		p2p_ie = rtw_get_p2p_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen), NULL, &p2p_ielen);
	}
	return result;
}

u8 process_p2p_presence_req(struct wifidirect_info *pwdinfo, u8 *pframe, uint len)
{
	u8 *frame_body;
	u8 dialogToken = 0;
	u8 status = P2P_STATUS_SUCCESS;

	frame_body = (unsigned char *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));

	dialogToken = frame_body[6];

	/* todo: check NoA attribute */

	issue_p2p_presence_resp(pwdinfo, GetAddr2Ptr(pframe), status, dialogToken);

	return true;
}

static void find_phase_handler(struct adapter *padapter)
{
	struct wifidirect_info  *pwdinfo = &padapter->wdinfo;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct ndis_802_11_ssid	ssid;

	memset((unsigned char *)&ssid, 0, sizeof(struct ndis_802_11_ssid));
	memcpy(ssid.Ssid, pwdinfo->p2p_wildcard_ssid, P2P_WILDCARD_SSID_LEN);
	ssid.SsidLength = P2P_WILDCARD_SSID_LEN;

	rtw_p2p_set_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH);

	spin_lock_bh(&pmlmepriv->lock);
	spin_unlock_bh(&pmlmepriv->lock);

}

void p2p_concurrent_handler(struct adapter *padapter);

static void restore_p2p_state_handler(struct adapter *padapter)
{
	struct wifidirect_info  *pwdinfo = &padapter->wdinfo;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING) || rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_FAIL))
		rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
	rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));

	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE)) {
		/*	In the P2P client mode, the driver should not switch back to its listen channel */
		/*	because this P2P client should stay at the operating channel of P2P GO. */
		set_channel_bwmode(padapter, pwdinfo->listen_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
	}

}

static void pre_tx_invitereq_handler(struct adapter *padapter)
{
	struct wifidirect_info  *pwdinfo = &padapter->wdinfo;
	u8	val8 = 1;

	set_channel_bwmode(padapter, pwdinfo->invitereq_info.peer_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
	issue_probereq_p2p(padapter, NULL);
	_set_timer(&pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT);

}

static void pre_tx_provdisc_handler(struct adapter *padapter)
{
	struct wifidirect_info  *pwdinfo = &padapter->wdinfo;
	u8	val8 = 1;

	set_channel_bwmode(padapter, pwdinfo->tx_prov_disc_info.peer_channel_num[0], HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
	issue_probereq_p2p(padapter, NULL);
	_set_timer(&pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT);

}

static void pre_tx_negoreq_handler(struct adapter *padapter)
{
	struct wifidirect_info  *pwdinfo = &padapter->wdinfo;
	u8	val8 = 1;

	set_channel_bwmode(padapter, pwdinfo->nego_req_info.peer_channel_num[0], HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
	issue_probereq_p2p(padapter, NULL);
	_set_timer(&pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT);

}

void p2p_protocol_wk_hdl(struct adapter *padapter, int intCmdType)
{

	switch (intCmdType) {
	case P2P_FIND_PHASE_WK:
		find_phase_handler(padapter);
		break;
	case P2P_RESTORE_STATE_WK:
		restore_p2p_state_handler(padapter);
		break;
	case P2P_PRE_TX_PROVDISC_PROCESS_WK:
		pre_tx_provdisc_handler(padapter);
		break;
	case P2P_PRE_TX_INVITEREQ_PROCESS_WK:
		pre_tx_invitereq_handler(padapter);
		break;
	case P2P_PRE_TX_NEGOREQ_PROCESS_WK:
		pre_tx_negoreq_handler(padapter);
		break;
	}

}

void process_p2p_ps_ie(struct adapter *padapter, u8 *IEs, u32 IELength)
{
	u8 *ies;
	u32 ies_len;
	u8 *p2p_ie;
	u32	p2p_ielen = 0;
	u8	noa_attr[MAX_P2P_IE_LEN] = { 0x00 };/*  NoA length should be n*(13) + 2 */
	u32	attr_contentlen = 0;

	struct wifidirect_info	*pwdinfo = &padapter->wdinfo;
	u8	find_p2p = false, find_p2p_ps = false;
	u8	noa_offset, noa_num, noa_index;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;
	if (IELength <= _BEACON_IE_OFFSET_)
		return;

	ies = IEs + _BEACON_IE_OFFSET_;
	ies_len = IELength - _BEACON_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie(ies, ies_len, NULL, &p2p_ielen);

	while (p2p_ie) {
		find_p2p = true;
		/*  Get Notice of Absence IE. */
		if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_NOA, noa_attr, &attr_contentlen)) {
			find_p2p_ps = true;
			noa_index = noa_attr[0];

			if ((pwdinfo->p2p_ps_mode == P2P_PS_NONE) ||
			    (noa_index != pwdinfo->noa_index)) { /*  if index change, driver should reconfigure related setting. */
				pwdinfo->noa_index = noa_index;
				pwdinfo->opp_ps = noa_attr[1] >> 7;
				pwdinfo->ctwindow = noa_attr[1] & 0x7F;

				noa_offset = 2;
				noa_num = 0;
				/*  NoA length should be n*(13) + 2 */
				if (attr_contentlen > 2) {
					while (noa_offset < attr_contentlen) {
						/* memcpy(&wifidirect_info->noa_count[noa_num], &noa_attr[noa_offset], 1); */
						pwdinfo->noa_count[noa_num] = noa_attr[noa_offset];
						noa_offset += 1;

						memcpy(&pwdinfo->noa_duration[noa_num], &noa_attr[noa_offset], 4);
						noa_offset += 4;

						memcpy(&pwdinfo->noa_interval[noa_num], &noa_attr[noa_offset], 4);
						noa_offset += 4;

						memcpy(&pwdinfo->noa_start_time[noa_num], &noa_attr[noa_offset], 4);
						noa_offset += 4;

						noa_num++;
					}
				}
				pwdinfo->noa_num = noa_num;

				if (pwdinfo->opp_ps == 1) {
					pwdinfo->p2p_ps_mode = P2P_PS_CTWINDOW;
					/*  driver should wait LPS for entering CTWindow */
					if (padapter->pwrctrlpriv.bFwCurrentInPSMode)
						p2p_ps_wk_cmd(padapter, P2P_PS_ENABLE, 1);
				} else if (pwdinfo->noa_num > 0) {
					pwdinfo->p2p_ps_mode = P2P_PS_NOA;
					p2p_ps_wk_cmd(padapter, P2P_PS_ENABLE, 1);
				} else if (pwdinfo->p2p_ps_mode > P2P_PS_NONE) {
					p2p_ps_wk_cmd(padapter, P2P_PS_DISABLE, 1);
				}
			}

			break; /*  find target, just break. */
		}

		/* Get the next P2P IE */
		p2p_ie = rtw_get_p2p_ie(p2p_ie + p2p_ielen, ies_len - (p2p_ie - ies + p2p_ielen), NULL, &p2p_ielen);
	}

	if (find_p2p) {
		if ((pwdinfo->p2p_ps_mode > P2P_PS_NONE) && !find_p2p_ps)
			p2p_ps_wk_cmd(padapter, P2P_PS_DISABLE, 1);
	}

}

void p2p_ps_wk_hdl(struct adapter *padapter, u8 p2p_ps_state)
{
	struct pwrctrl_priv		*pwrpriv = &padapter->pwrctrlpriv;
	struct wifidirect_info	*pwdinfo = &padapter->wdinfo;

	/*  Pre action for p2p state */
	switch (p2p_ps_state) {
	case P2P_PS_DISABLE:
		pwdinfo->p2p_ps_state = p2p_ps_state;

		rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_P2P_PS_OFFLOAD, (u8 *)(&p2p_ps_state));

		pwdinfo->noa_index = 0;
		pwdinfo->ctwindow = 0;
		pwdinfo->opp_ps = 0;
		pwdinfo->noa_num = 0;
		pwdinfo->p2p_ps_mode = P2P_PS_NONE;
		if (padapter->pwrctrlpriv.bFwCurrentInPSMode) {
			if (pwrpriv->smart_ps == 0) {
				pwrpriv->smart_ps = 2;
				rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&padapter->pwrctrlpriv.pwr_mode));
			}
		}
		break;
	case P2P_PS_ENABLE:
		if (pwdinfo->p2p_ps_mode > P2P_PS_NONE) {
			pwdinfo->p2p_ps_state = p2p_ps_state;

			if (pwdinfo->ctwindow > 0) {
				if (pwrpriv->smart_ps != 0) {
					pwrpriv->smart_ps = 0;
					DBG_88E("%s(): Enter CTW, change SmartPS\n", __func__);
					rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&padapter->pwrctrlpriv.pwr_mode));
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

}

u8 p2p_ps_wk_cmd(struct adapter *padapter, u8 p2p_ps_state, u8 enqueue)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct wifidirect_info	*pwdinfo = &padapter->wdinfo;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return res;

	if (enqueue) {
		ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
		if (!ph2c) {
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm = kzalloc(sizeof(struct drvextra_cmd_parm), GFP_ATOMIC);
		if (!pdrvextra_cmd_parm) {
			kfree(ph2c);
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm->ec_id = P2P_PS_WK_CID;
		pdrvextra_cmd_parm->type_size = p2p_ps_state;
		pdrvextra_cmd_parm->pbuf = NULL;

		init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	} else {
		p2p_ps_wk_hdl(padapter, p2p_ps_state);
	}

exit:

	return res;
}

static void reset_ch_sitesurvey_timer_process(struct timer_list *t)
{
	struct adapter *adapter = from_timer(adapter, t, pwrctrlpriv.pwr_state_check_timer);
	struct	wifidirect_info		*pwdinfo = &adapter->wdinfo;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

	DBG_88E("[%s] In\n", __func__);
	/*	Reset the operation channel information */
	pwdinfo->rx_invitereq_info.operation_ch[0] = 0;
	pwdinfo->rx_invitereq_info.scan_op_ch_only = 0;
}

static void reset_ch_sitesurvey_timer_process2(struct timer_list *t)
{
	struct adapter *adapter = from_timer(adapter, t, pwrctrlpriv.pwr_state_check_timer);
	struct	wifidirect_info		*pwdinfo = &adapter->wdinfo;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

	DBG_88E("[%s] In\n", __func__);
	/*	Reset the operation channel information */
	pwdinfo->p2p_info.operation_ch[0] = 0;
	pwdinfo->p2p_info.scan_op_ch_only = 0;
}

static void restore_p2p_state_timer_process(struct timer_list *t)
{
	struct adapter *adapter = from_timer(adapter, t, wdinfo.restore_p2p_state_timer);
	struct	wifidirect_info		*pwdinfo = &adapter->wdinfo;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

	p2p_protocol_wk_cmd(adapter, P2P_RESTORE_STATE_WK);
}

static void pre_tx_scan_timer_process(struct timer_list *t)
{
	struct adapter *adapter = from_timer(adapter, t, wdinfo.pre_tx_scan_timer);
	struct	wifidirect_info *pwdinfo = &adapter->wdinfo;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

	spin_lock_bh(&pmlmepriv->lock);

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_PROVISION_DIS_REQ)) {
		if (pwdinfo->tx_prov_disc_info.benable) {	/*	the provision discovery request frame is trigger to send or not */
			p2p_protocol_wk_cmd(adapter, P2P_PRE_TX_PROVDISC_PROCESS_WK);
			/* issue_probereq_p2p(adapter, NULL); */
			/* _set_timer(&pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT); */
		}
	} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING)) {
		if (pwdinfo->nego_req_info.benable)
			p2p_protocol_wk_cmd(adapter, P2P_PRE_TX_NEGOREQ_PROCESS_WK);
	} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_INVITE_REQ)) {
		if (pwdinfo->invitereq_info.benable)
			p2p_protocol_wk_cmd(adapter, P2P_PRE_TX_INVITEREQ_PROCESS_WK);
	} else {
		DBG_88E("[%s] p2p_state is %d, ignore!!\n", __func__, rtw_p2p_state(pwdinfo));
	}

	spin_unlock_bh(&pmlmepriv->lock);
}

static void find_phase_timer_process(struct timer_list *t)
{
	struct adapter *adapter = from_timer(adapter, t, wdinfo.find_phase_timer);
	struct	wifidirect_info		*pwdinfo = &adapter->wdinfo;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

	adapter->wdinfo.find_phase_state_exchange_cnt++;

	p2p_protocol_wk_cmd(adapter, P2P_FIND_PHASE_WK);
}

void reset_global_wifidirect_info(struct adapter *padapter)
{
	struct wifidirect_info	*pwdinfo;

	pwdinfo = &padapter->wdinfo;
	pwdinfo->persistent_supported = 0;
	pwdinfo->session_available = true;
	pwdinfo->wfd_tdls_enable = 0;
	pwdinfo->wfd_tdls_weaksec = 0;
}

void rtw_init_wifidirect_timers(struct adapter *padapter)
{
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	timer_setup(&pwdinfo->find_phase_timer, find_phase_timer_process, 0);
	timer_setup(&pwdinfo->restore_p2p_state_timer, restore_p2p_state_timer_process, 0);
	timer_setup(&pwdinfo->pre_tx_scan_timer, pre_tx_scan_timer_process, 0);
	timer_setup(&pwdinfo->reset_ch_sitesurvey, reset_ch_sitesurvey_timer_process, 0);
	timer_setup(&pwdinfo->reset_ch_sitesurvey2, reset_ch_sitesurvey_timer_process2, 0);
}

void rtw_init_wifidirect_addrs(struct adapter *padapter, u8 *dev_addr, u8 *iface_addr)
{
#ifdef CONFIG_88EU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	/*init device&interface address */
	if (dev_addr)
		memcpy(pwdinfo->device_addr, dev_addr, ETH_ALEN);
	if (iface_addr)
		memcpy(pwdinfo->interface_addr, iface_addr, ETH_ALEN);
#endif
}

void init_wifidirect_info(struct adapter *padapter, enum P2P_ROLE role)
{
	struct wifidirect_info	*pwdinfo;

	pwdinfo = &padapter->wdinfo;
	pwdinfo->padapter = padapter;

	/*	1, 6, 11 are the social channel defined in the WiFi Direct specification. */
	pwdinfo->social_chan[0] = 1;
	pwdinfo->social_chan[1] = 6;
	pwdinfo->social_chan[2] = 11;
	pwdinfo->social_chan[3] = 0;	/*	channel 0 for scanning ending in site survey function. */

	/*	Use the channel 11 as the listen channel */
	pwdinfo->listen_channel = 11;

	if (role == P2P_ROLE_DEVICE) {
		rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
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

/*	Use the OFDM rate in the P2P probe response frame. (6(B), 9(B), 12, 18, 24, 36, 48, 54) */
	pwdinfo->support_rate[0] = 0x8c;	/*	6(B) */
	pwdinfo->support_rate[1] = 0x92;	/*	9(B) */
	pwdinfo->support_rate[2] = 0x18;	/*	12 */
	pwdinfo->support_rate[3] = 0x24;	/*	18 */
	pwdinfo->support_rate[4] = 0x30;	/*	24 */
	pwdinfo->support_rate[5] = 0x48;	/*	36 */
	pwdinfo->support_rate[6] = 0x60;	/*	48 */
	pwdinfo->support_rate[7] = 0x6c;	/*	54 */

	memcpy(pwdinfo->p2p_wildcard_ssid, "DIRECT-", 7);

	memset(pwdinfo->device_name, 0x00, WPS_MAX_DEVICE_NAME_LEN);
	pwdinfo->device_name_len = 0;

	memset(&pwdinfo->invitereq_info, 0x00, sizeof(struct tx_invite_req_info));
	pwdinfo->invitereq_info.token = 3;	/*	Token used for P2P invitation request frame. */

	memset(&pwdinfo->inviteresp_info, 0x00, sizeof(struct tx_invite_resp_info));
	pwdinfo->inviteresp_info.token = 0;

	pwdinfo->profileindex = 0;
	memset(&pwdinfo->profileinfo[0], 0x00, sizeof(struct profile_info) * P2P_MAX_PERSISTENT_GROUP_NUM);

	rtw_p2p_findphase_ex_set(pwdinfo, P2P_FINDPHASE_EX_NONE);

	pwdinfo->listen_dwell = (u8)((jiffies % 3) + 1);

	memset(&pwdinfo->tx_prov_disc_info, 0x00, sizeof(struct tx_provdisc_req_info));
	pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_NONE;

	memset(&pwdinfo->nego_req_info, 0x00, sizeof(struct tx_nego_req_info));

	pwdinfo->device_password_id_for_nego = WPS_DPID_PBC;
	pwdinfo->negotiation_dialog_token = 1;

	memset(pwdinfo->nego_ssid, 0x00, WLAN_SSID_MAXLEN);
	pwdinfo->nego_ssidlen = 0;

	pwdinfo->ui_got_wps_info = P2P_NO_WPSINFO;
	pwdinfo->supported_wps_cm = WPS_CONFIG_METHOD_DISPLAY | WPS_CONFIG_METHOD_PBC | WPS_CONFIG_METHOD_KEYPAD;
	pwdinfo->channel_list_attr_len = 0;
	memset(pwdinfo->channel_list_attr, 0x00, 100);

	memset(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, 0x00, 4);
	memset(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, '0', 3);
	memset(&pwdinfo->groupid_info, 0x00, sizeof(struct group_id_info));
	pwdinfo->wfd_tdls_enable = 0;
	memset(pwdinfo->p2p_peer_interface_addr, 0x00, ETH_ALEN);
	memset(pwdinfo->p2p_peer_device_addr, 0x00, ETH_ALEN);

	pwdinfo->rx_invitereq_info.operation_ch[0] = 0;
	pwdinfo->rx_invitereq_info.operation_ch[1] = 0;	/*	Used to indicate the scan end in site survey function */
	pwdinfo->rx_invitereq_info.scan_op_ch_only = 0;
	pwdinfo->p2p_info.operation_ch[0] = 0;
	pwdinfo->p2p_info.operation_ch[1] = 0;			/*	Used to indicate the scan end in site survey function */
	pwdinfo->p2p_info.scan_op_ch_only = 0;
}

int rtw_p2p_enable(struct adapter *padapter, enum P2P_ROLE role)
{
	int ret = _SUCCESS;
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	if (role == P2P_ROLE_DEVICE || role == P2P_ROLE_CLIENT || role == P2P_ROLE_GO) {
		/* leave IPS/Autosuspend */
		if (_FAIL == rtw_pwr_wakeup(padapter)) {
			ret = _FAIL;
			goto exit;
		}

		/*	Added by Albert 2011/03/22 */
		/*	In the P2P mode, the driver should not support the b mode. */
		/*	So, the Tx packet shouldn't use the CCK rate */
		update_tx_basic_rate(padapter, (WIRELESS_11G | WIRELESS_11_24N));

		/* Enable P2P function */
		init_wifidirect_info(padapter, role);

		rtl8188e_SetHalODMVar(padapter, HAL_ODM_P2P_STATE, NULL, true);
	} else if (role == P2P_ROLE_DISABLE) {
		if (_FAIL == rtw_pwr_wakeup(padapter)) {
			ret = _FAIL;
			goto exit;
		}

		/* Disable P2P function */
		if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
			_cancel_timer_ex(&pwdinfo->find_phase_timer);
			_cancel_timer_ex(&pwdinfo->restore_p2p_state_timer);
			_cancel_timer_ex(&pwdinfo->pre_tx_scan_timer);
			_cancel_timer_ex(&pwdinfo->reset_ch_sitesurvey);
			_cancel_timer_ex(&pwdinfo->reset_ch_sitesurvey2);
			rtw_p2p_set_state(pwdinfo, P2P_STATE_NONE);
			rtw_p2p_set_role(pwdinfo, P2P_ROLE_DISABLE);
			memset(&pwdinfo->rx_prov_disc_info, 0x00, sizeof(struct rx_provdisc_req_info));
		}

		rtl8188e_SetHalODMVar(padapter, HAL_ODM_P2P_STATE, NULL, false);

		/* Restore to initial setting. */
		update_tx_basic_rate(padapter, padapter->registrypriv.wireless_mode);
	}

exit:
	return ret;
}

#else
u8 p2p_ps_wk_cmd(struct adapter *padapter, u8 p2p_ps_state, u8 enqueue)
{
	return _FAIL;
}

void process_p2p_ps_ie(struct adapter *padapter, u8 *IEs, u32 IELength)
{
}

#endif /* CONFIG_88EU_P2P */
