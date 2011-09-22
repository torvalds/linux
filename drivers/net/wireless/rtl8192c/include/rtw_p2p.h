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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __RTW_P2P_H_
#define __RTW_P2P_H_

#include <drv_types.h>

u32 build_beacon_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_probe_resp_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);
u32 build_prov_disc_request_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf, u8* pssid, u8 ussidlen, u8* pdev_raddr );
u32 build_assoc_resp_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf, u8 status_code);
u32 build_deauth_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pbuf);

u32 process_probe_req_p2p_ie(struct wifidirect_info *pwdinfo, u8 *pframe, uint len);
u32 process_assoc_req_p2p_ie(struct wifidirect_info *pwdinfo, u8 *p2p_ie, uint p2p_ielen, struct sta_info *psta);
u32 process_p2p_devdisc_req(struct wifidirect_info *pwdinfo, u8 *pframe, uint len);
u32 process_p2p_devdisc_resp(struct wifidirect_info *pwdinfo, u8 *pframe, uint len);
u8 process_p2p_provdisc_req(struct wifidirect_info *pwdinfo,  u8 *pframe, uint len);
u8 process_p2p_provdisc_resp(struct wifidirect_info *pwdinfo,  u8 *pframe);
u8 process_p2p_group_negotation_req( struct wifidirect_info *pwdinfo, u8 *pframe, uint len );
u8 process_p2p_group_negotation_resp( struct wifidirect_info *pwdinfo, u8 *pframe, uint len );
u8 process_p2p_group_negotation_confirm( struct wifidirect_info *pwdinfo, u8 *pframe, uint len );
u8 process_p2p_presence_req(struct wifidirect_info *pwdinfo, u8 *pframe, uint len);

void process_p2p_ps_ie(PADAPTER padapter, u8 *IEs, u32 IELength);
void p2p_ps_wk_hdl(_adapter *padapter, u8 p2p_ps_state);
void p2p_protocol_wk_hdl(_adapter *padapter, int intCmdType);
u8 p2p_ps_wk_cmd(_adapter*padapter, u8 p2p_ps_state, u8 enqueue);

#endif

