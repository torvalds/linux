/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
#ifndef __RTW_P2P_H_
#define __RTW_P2P_H_

#include <drv_types.h>

u32 build_beacon_p2p_ie23a(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_probe_resp_p2p_ie23a(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_prov_disc_request_p2p_ie23a(struct wifidirect_info *pwdinfo, u8 *pbuf,
				   u8 *pssid, u8 ussidlen, u8 *pdev_raddr);
u32 build_assoc_resp_p2p_ie23a(struct wifidirect_info *pwdinfo, u8 *pbuf,
			    u8 status_code);
u32 build_deauth_p2p_ie23a(struct wifidirect_info *pwdinfo, u8 *pbuf);
#ifdef CONFIG_8723AU_P2P
u32 build_probe_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_probe_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf,
			    u8 tunneled);
u32 build_beacon_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_nego_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_nego_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_nego_confirm_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_invitation_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_invitation_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_assoc_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_assoc_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_provdisc_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_provdisc_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
#endif /* CONFIG_8723AU_P2P */

u32 process_probe_req_p2p_ie23a(struct wifidirect_info *pwdinfo, u8 *pframe,
			     uint len);
u32 process_assoc_req_p2p_ie23a(struct wifidirect_info *pwdinfo, u8 *pframe,
			     uint len, struct sta_info *psta);
u32 process_p2p_devdisc_req23a(struct wifidirect_info *pwdinfo, u8 *pframe,
			    uint len);
u32 process_p2p_devdisc_resp23a(struct wifidirect_info *pwdinfo, u8 *pframe,
			     uint len);
u8 process_p2p_provdisc_req23a(struct wifidirect_info *pwdinfo, u8 *pframe,
			    uint len);
u8 process_p2p_provdisc_resp23a(struct wifidirect_info *pwdinfo, u8 *pframe);
u8 process_p2p_group_negotation_req23a(struct wifidirect_info *pwdinfo,
				    u8 *pframe, uint len);
u8 process_p2p_group_negotation_resp23a(struct wifidirect_info *pwdinfo,
				     u8 *pframe, uint len);
u8 process_p2p_group_negotation_confirm23a(struct wifidirect_info *pwdinfo,
					u8 *pframe, uint len);
u8 process_p2p_presence_req23a(struct wifidirect_info *pwdinfo,
			    u8 *pframe, uint len);

void p2p_protocol_wk_hdl23a(struct rtw_adapter *padapter, int cmdtype);

#ifdef CONFIG_8723AU_P2P
void	process_p2p_ps_ie23a(struct rtw_adapter *padapter, u8 *IEs, u32 IELength);
void	p2p_ps_wk_hdl23a(struct rtw_adapter *padapter, u8 p2p_ps_state);
u8 p2p_ps_wk_cmd23a(struct rtw_adapter *padapter, u8 p2p_ps_state, u8 enqueue);
#endif /*  CONFIG_8723AU_P2P */

void rtw_init_cfg80211_wifidirect_info(struct rtw_adapter *padapter);
int rtw_p2p_check_frames(struct rtw_adapter *padapter, const u8 *buf,
			 u32 len, u8 tx);
void rtw_append_wfd_ie(struct rtw_adapter *padapter, u8 *buf, u32 *len);

void reset_global_wifidirect_info23a(struct rtw_adapter *padapter);
int rtw_init_wifi_display_info(struct rtw_adapter *padapter);
void rtw_init_wifidirect_timers23a(struct rtw_adapter *padapter);
void rtw_init_wifidirect_addrs23a(struct rtw_adapter *padapter, u8 *dev_addr,
			       u8 *iface_addr);
void init_wifidirect_info23a(struct rtw_adapter *padapter, enum P2P_ROLE role);
int rtw_p2p_enable23a(struct rtw_adapter *padapter, enum P2P_ROLE role);

static inline void _rtw_p2p_set_state(struct wifidirect_info *wdinfo,
				      enum P2P_STATE state)
{
	if (wdinfo->p2p_state != state) {
		/* wdinfo->pre_p2p_state = wdinfo->p2p_state; */
		wdinfo->p2p_state = state;
	}
}

static inline void _rtw_p2p_set_pre_state(struct wifidirect_info *wdinfo,
					  enum P2P_STATE state)
{
	if (wdinfo->pre_p2p_state != state)
		wdinfo->pre_p2p_state = state;
}

static inline void _rtw_p2p_set_role(struct wifidirect_info *wdinfo,
				     enum P2P_ROLE role)
{
	if (wdinfo->role != role)
		wdinfo->role = role;
}

static inline int _rtw_p2p_state(struct wifidirect_info *wdinfo)
{
	return wdinfo->p2p_state;
}

static inline int _rtw_p2p_pre_state(struct wifidirect_info *wdinfo)
{
	return wdinfo->pre_p2p_state;
}

static inline int _rtw_p2p_role(struct wifidirect_info *wdinfo)
{
	return wdinfo->role;
}

static inline bool _rtw_p2p_chk_state(struct wifidirect_info *wdinfo,
				      enum P2P_STATE state)
{
	return wdinfo->p2p_state == state;
}

static inline bool _rtw_p2p_chk_role(struct wifidirect_info *wdinfo,
				     enum P2P_ROLE role)
{
	return wdinfo->role == role;
}

#define rtw_p2p_set_state(wdinfo, state) _rtw_p2p_set_state(wdinfo, state)
#define rtw_p2p_set_pre_state(wdinfo, state)			\
	_rtw_p2p_set_pre_state(wdinfo, state)
#define rtw_p2p_set_role(wdinfo, role) _rtw_p2p_set_role(wdinfo, role)

#define rtw_p2p_state(wdinfo) _rtw_p2p_state(wdinfo)
#define rtw_p2p_pre_state(wdinfo) _rtw_p2p_pre_state(wdinfo)
#define rtw_p2p_role(wdinfo) _rtw_p2p_role(wdinfo)
#define rtw_p2p_chk_state(wdinfo, state) _rtw_p2p_chk_state(wdinfo, state)
#define rtw_p2p_chk_role(wdinfo, role) _rtw_p2p_chk_role(wdinfo, role)

#define rtw_p2p_findphase_ex_set(wdinfo, value) \
	((wdinfo)->find_phase_state_exchange_cnt = (value))

/* is this find phase exchange for social channel scan? */
#define rtw_p2p_findphase_ex_is_social(wdinfo)			\
	((wdinfo)->find_phase_state_exchange_cnt >=		\
	 P2P_FINDPHASE_EX_SOCIAL_FIRST)

/* should we need find phase exchange anymore? */
#define rtw_p2p_findphase_ex_is_needed(wdinfo) \
	((wdinfo)->find_phase_state_exchange_cnt < P2P_FINDPHASE_EX_MAX && \
	(wdinfo)->find_phase_state_exchange_cnt != P2P_FINDPHASE_EX_NONE)

#endif
