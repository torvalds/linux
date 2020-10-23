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

#ifndef __RTW_FT_H_
#define __RTW_FT_H_

enum rtw_ieee80211_ft_actioncode {
	RTW_WLAN_ACTION_FT_RESV,
	RTW_WLAN_ACTION_FT_REQ,
	RTW_WLAN_ACTION_FT_RSP,
	RTW_WLAN_ACTION_FT_CONF,
	RTW_WLAN_ACTION_FT_ACK,
	RTW_WLAN_ACTION_FT_MAX,
};

enum _rtw_ft_sta_status {
	RTW_FT_UNASSOCIATED_STA = 0,
	RTW_FT_AUTHENTICATING_STA,
	RTW_FT_AUTHENTICATED_STA,
	RTW_FT_ASSOCIATING_STA,
	RTW_FT_ASSOCIATED_STA,
	RTW_FT_REQUESTING_STA,
	RTW_FT_REQUESTED_STA,
	RTW_FT_CONFIRMED_STA,
	RTW_FT_UNSPECIFIED_STA
};

#define RTW_FT_ACTION_REQ_LMT	4

#define RTW_FT_MAX_IE_SZ	256

#define rtw_ft_chk_status(a, s) \
	((a)->mlmepriv.ft_roam.ft_status == (s))

#define rtw_ft_roam_status(a, s)	\
	((rtw_to_roam(a) > 0) && rtw_ft_chk_status(a, s))

#define rtw_ft_authed_sta(a)	\
	((rtw_ft_chk_status(a, RTW_FT_AUTHENTICATED_STA)) ||	\
	(rtw_ft_chk_status(a, RTW_FT_ASSOCIATING_STA)) ||	\
	(rtw_ft_chk_status(a, RTW_FT_ASSOCIATED_STA)))

#define rtw_ft_set_status(a, s) \
	do { \
		((a)->mlmepriv.ft_roam.ft_status = (s)); \
	} while (0)

#define rtw_ft_lock_set_status(a, s, irq) \
	do { \
		_enter_critical_bh(&(a)->mlmepriv.lock, ((_irqL *)(irq)));	\
		((a)->mlmepriv.ft_roam.ft_status = (s));	\
		_exit_critical_bh(&(a)->mlmepriv.lock, ((_irqL *)(irq)));	\
	} while (0)

#define rtw_ft_reset_status(a) \
	do { \
		((a)->mlmepriv.ft_roam.ft_status = RTW_FT_UNASSOCIATED_STA); \
	} while (0)

enum rtw_ft_capability {
	RTW_FT_EN = BIT0,
	RTW_FT_OTD_EN = BIT1,
	RTW_FT_PEER_EN = BIT2,
	RTW_FT_PEER_OTD_EN = BIT3,
	RTW_FT_BTM_ROAM = BIT4,
	RTW_FT_TEST_RSSI_ROAM = BIT7,
};

#define rtw_ft_chk_flags(a, f) \
	((a)->mlmepriv.ft_roam.ft_flags & (f))

#define rtw_ft_set_flags(a, f) \
	do { \
		((a)->mlmepriv.ft_roam.ft_flags |= (f)); \
	} while (0)

#define rtw_ft_clr_flags(a, f) \
	do { \
		((a)->mlmepriv.ft_roam.ft_flags &= ~(f)); \
	} while (0)

#define rtw_ft_roam(a)	\
	((rtw_to_roam(a) > 0) && rtw_ft_chk_flags(a, RTW_FT_PEER_EN))
	
#define rtw_ft_valid_akm(a, t)	\
	((rtw_ft_chk_flags(a, RTW_FT_EN)) && \
	(((t) == 3) || ((t) == 4)))

#define rtw_ft_roam_expired(a, r)	\
	((rtw_chk_roam_flags(a, RTW_ROAM_ON_EXPIRED)) \
	&& (r == WLAN_REASON_ACTIVE_ROAM))

#define rtw_ft_otd_roam_en(a)	\
	((rtw_ft_chk_flags(a, RTW_FT_OTD_EN))	\
	&& ((a)->mlmepriv.ft_roam.ft_roam_on_expired == _FALSE)	\
	&& ((a)->mlmepriv.ft_roam.ft_cap & 0x01))
	
#define rtw_ft_otd_roam(a) \
	rtw_ft_chk_flags(a, RTW_FT_PEER_OTD_EN)

#define rtw_ft_valid_otd_candidate(a, p)	\
	((rtw_ft_chk_flags(a, RTW_FT_OTD_EN)) 	\
	&& ((rtw_ft_chk_flags(a, RTW_FT_PEER_OTD_EN)	\
	&& ((*((p)+4) & 0x01) == 0))	\
	|| ((rtw_ft_chk_flags(a, RTW_FT_PEER_OTD_EN) == 0)	\
	&& (*((p)+4) & 0x01))))

struct ft_roam_info {
	u16	mdid;
	u8	ft_cap;	
	/*b0: FT over DS, b1: Resource Req Protocol Cap, b2~b7: Reserved*/
	u8	updated_ft_ies[RTW_FT_MAX_IE_SZ];
	u16	updated_ft_ies_len;
	u8	ft_action[RTW_FT_MAX_IE_SZ];
	u16	ft_action_len;
	struct cfg80211_ft_event_params ft_event;
	u8	ft_roam_on_expired;
	u8	ft_flags;
	u32 ft_status;
	u32 ft_req_retry_cnt;
	bool ft_updated_bcn;	
};

void rtw_ft_info_init(struct ft_roam_info *pft);

int rtw_ft_proc_flags_get(struct seq_file *m, void *v);

ssize_t rtw_ft_proc_flags_set(struct file *file, const char __user *buffer,
	size_t count, loff_t *pos, void *data);

u8 rtw_ft_chk_roaming_candidate(
	_adapter *padapter, struct wlan_network *competitor);

void rtw_ft_update_stainfo(_adapter *padapter, WLAN_BSSID_EX *pnetwork);

void rtw_ft_reassoc_event_callback(_adapter *padapter, u8 *pbuf);

void rtw_ft_validate_akm_type(_adapter  *padapter,
	struct wlan_network *pnetwork);

void rtw_ft_update_bcn(_adapter *padapter, union recv_frame *precv_frame);

void rtw_ft_start_clnt_join(_adapter *padapter);

u8 rtw_ft_update_rsnie(
	_adapter *padapter, u8 bwrite, 
	struct pkt_attrib *pattrib, u8 **pframe);

void rtw_ft_build_auth_req_ies(_adapter *padapter, 
	struct pkt_attrib *pattrib, u8 **pframe);

void rtw_ft_build_assoc_req_ies(_adapter *padapter, 
	u8 is_reassoc, struct pkt_attrib *pattrib, u8 **pframe);

u8 rtw_ft_update_auth_rsp_ies(_adapter *padapter, u8 *pframe, u32 len);

void rtw_ft_start_roam(_adapter *padapter, u8 *pTargetAddr);

void rtw_ft_issue_action_req(_adapter *padapter, u8 *pTargetAddr);

void rtw_ft_report_evt(_adapter *padapter);

void rtw_ft_report_reassoc_evt(_adapter *padapter, u8 *pMacAddr);

void rtw_ft_link_timer_hdl(void *ctx);

void rtw_ft_roam_timer_hdl(void *ctx);

void rtw_ft_roam_status_reset(_adapter *padapter);

#endif /* __RTW_FT_H_ */
