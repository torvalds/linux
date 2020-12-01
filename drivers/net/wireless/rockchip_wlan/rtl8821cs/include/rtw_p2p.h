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
#ifndef __RTW_P2P_H_
#define __RTW_P2P_H_


u32 build_beacon_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_probe_resp_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_prov_disc_request_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf, u8 *pssid, u8 ussidlen, u8 *pdev_raddr);
u32 build_assoc_resp_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf, u8 status_code);
u32 build_deauth_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
#ifdef CONFIG_WFD
int rtw_init_wifi_display_info(_adapter *padapter);
void rtw_wfd_enable(_adapter *adapter, bool on);
void rtw_wfd_set_ctrl_port(_adapter *adapter, u16 port);
void rtw_tdls_wfd_enable(_adapter *adapter, bool on);

u32 build_probe_req_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_probe_resp_wfd_ie(struct wifidirect_info *pwdinfo, u8 *pbuf, u8 tunneled);
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

u32 rtw_append_beacon_wfd_ie(_adapter *adapter, u8 *pbuf);
u32 rtw_append_probe_req_wfd_ie(_adapter *adapter, u8 *pbuf);
u32 rtw_append_probe_resp_wfd_ie(_adapter *adapter, u8 *pbuf);
u32 rtw_append_assoc_req_wfd_ie(_adapter *adapter, u8 *pbuf);
u32 rtw_append_assoc_resp_wfd_ie(_adapter *adapter, u8 *pbuf);
#endif /*CONFIG_WFD */

void rtw_xframe_chk_wfd_ie(struct xmit_frame *xframe);

u32 process_probe_req_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pframe, uint len);
u32 process_assoc_req_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pframe, uint len, struct sta_info *psta);
u32 process_p2p_devdisc_req(struct wifidirect_info *pwdinfo, u8 *pframe, uint len);
u32 process_p2p_devdisc_resp(struct wifidirect_info *pwdinfo, u8 *pframe, uint len);
u8 process_p2p_provdisc_req(struct wifidirect_info *pwdinfo,  u8 *pframe, uint len);
u8 process_p2p_provdisc_resp(struct wifidirect_info *pwdinfo,  u8 *pframe);
u8 process_p2p_group_negotation_req(struct wifidirect_info *pwdinfo, u8 *pframe, uint len);
u8 process_p2p_group_negotation_resp(struct wifidirect_info *pwdinfo, u8 *pframe, uint len);
u8 process_p2p_group_negotation_confirm(struct wifidirect_info *pwdinfo, u8 *pframe, uint len);
u8 process_p2p_presence_req(struct wifidirect_info *pwdinfo, u8 *pframe, uint len);
int process_p2p_cross_connect_ie(PADAPTER padapter, u8 *IEs, u32 IELength);

s32 p2p_protocol_wk_hdl(_adapter *padapter, int intCmdType, u8 *buf);

#ifdef CONFIG_P2P_PS
void	process_p2p_ps_ie(PADAPTER padapter, u8 *IEs, u32 IELength);
void	p2p_ps_wk_hdl(_adapter *padapter, u8 p2p_ps_state);
u8	p2p_ps_wk_cmd(_adapter *padapter, u8 p2p_ps_state, u8 enqueue);
#endif /* CONFIG_P2P_PS */

#ifdef CONFIG_IOCTL_CFG80211
int rtw_p2p_check_frames(_adapter *padapter, const u8 *buf, u32 len, u8 tx);
#endif /* CONFIG_IOCTL_CFG80211 */

void reset_global_wifidirect_info(_adapter *padapter);
void rtw_init_wifidirect_timers(_adapter *padapter);
void rtw_init_wifidirect_addrs(_adapter *padapter, u8 *dev_addr, u8 *iface_addr);
void init_wifidirect_info(_adapter *padapter, enum P2P_ROLE role);
int rtw_p2p_enable(_adapter *padapter, enum P2P_ROLE role);

static inline void _rtw_p2p_set_state(struct wifidirect_info *wdinfo, enum P2P_STATE state)
{
	if (wdinfo->p2p_state != state) {
		/* wdinfo->pre_p2p_state = wdinfo->p2p_state; */
		wdinfo->p2p_state = state;
	}
}
static inline void _rtw_p2p_set_pre_state(struct wifidirect_info *wdinfo, enum P2P_STATE state)
{
	if (wdinfo->pre_p2p_state != state)
		wdinfo->pre_p2p_state = state;
}
#if 0
static inline void _rtw_p2p_restore_state(struct wifidirect_info *wdinfo)
{
	if (wdinfo->pre_p2p_state != -1) {
		wdinfo->p2p_state = wdinfo->pre_p2p_state;
		wdinfo->pre_p2p_state = -1;
	}
}
#endif
void _rtw_p2p_set_role(struct wifidirect_info *wdinfo, enum P2P_ROLE role);

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
static inline bool _rtw_p2p_chk_state(struct wifidirect_info *wdinfo, enum P2P_STATE state)
{
	return wdinfo->p2p_state == state;
}
static inline bool _rtw_p2p_chk_role(struct wifidirect_info *wdinfo, enum P2P_ROLE role)
{
	return wdinfo->role == role;
}

#ifdef CONFIG_DBG_P2P
void dbg_rtw_p2p_set_state(struct wifidirect_info *wdinfo, enum P2P_STATE state, const char *caller, int line);
void dbg_rtw_p2p_set_pre_state(struct wifidirect_info *wdinfo, enum P2P_STATE state, const char *caller, int line);
/* void dbg_rtw_p2p_restore_state(struct wifidirect_info *wdinfo, const char *caller, int line); */
void dbg_rtw_p2p_set_role(struct wifidirect_info *wdinfo, enum P2P_ROLE role, const char *caller, int line);
#define rtw_p2p_set_state(wdinfo, state) dbg_rtw_p2p_set_state(wdinfo, state, __FUNCTION__, __LINE__)
#define rtw_p2p_set_pre_state(wdinfo, state) dbg_rtw_p2p_set_pre_state(wdinfo, state, __FUNCTION__, __LINE__)
#define rtw_p2p_set_role(wdinfo, role) dbg_rtw_p2p_set_role(wdinfo, role, __FUNCTION__, __LINE__)
/* #define rtw_p2p_restore_state(wdinfo) dbg_rtw_p2p_restore_state(wdinfo, __FUNCTION__, __LINE__) */
#else /* CONFIG_DBG_P2P */
#define rtw_p2p_set_state(wdinfo, state) _rtw_p2p_set_state(wdinfo, state)
#define rtw_p2p_set_pre_state(wdinfo, state) _rtw_p2p_set_pre_state(wdinfo, state)
#define rtw_p2p_set_role(wdinfo, role) _rtw_p2p_set_role(wdinfo, role)
/* #define rtw_p2p_restore_state(wdinfo) _rtw_p2p_restore_state(wdinfo) */
#endif /* CONFIG_DBG_P2P */

#define rtw_p2p_state(wdinfo) _rtw_p2p_state(wdinfo)
#define rtw_p2p_pre_state(wdinfo) _rtw_p2p_pre_state(wdinfo)
#define rtw_p2p_role(wdinfo) _rtw_p2p_role(wdinfo)
#define rtw_p2p_chk_state(wdinfo, state) _rtw_p2p_chk_state(wdinfo, state)
#define rtw_p2p_chk_role(wdinfo, role) _rtw_p2p_chk_role(wdinfo, role)

#define rtw_p2p_findphase_ex_set(wdinfo, value) \
	(wdinfo)->find_phase_state_exchange_cnt = (value)

#ifdef CONFIG_P2P
/* is this find phase exchange for social channel scan? */
#define rtw_p2p_findphase_ex_is_social(wdinfo)   \
	(wdinfo)->find_phase_state_exchange_cnt >= P2P_FINDPHASE_EX_SOCIAL_FIRST

/* should we need find phase exchange anymore? */
#define rtw_p2p_findphase_ex_is_needed(wdinfo) \
	((wdinfo)->find_phase_state_exchange_cnt < P2P_FINDPHASE_EX_MAX && \
	 (wdinfo)->find_phase_state_exchange_cnt != P2P_FINDPHASE_EX_NONE && \
	 !(wdinfo)->rx_invitereq_info.scan_op_ch_only && \
	 !(wdinfo)->p2p_info.scan_op_ch_only)
#else
#define rtw_p2p_findphase_ex_is_social(wdinfo) 0
#define rtw_p2p_findphase_ex_is_needed(wdinfo) 0
#endif /* CONFIG_P2P */

#endif
