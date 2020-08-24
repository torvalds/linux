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
#define _RTW_MESH_C_

#ifdef CONFIG_RTW_MESH
#include <drv_types.h>

const char *_rtw_mesh_plink_str[] = {
	"UNKNOWN",
	"LISTEN",
	"OPN_SNT",
	"OPN_RCVD",
	"CNF_RCVD",
	"ESTAB",
	"HOLDING",
	"BLOCKED",
};

const char *_rtw_mesh_ps_str[] = {
	"UNKNOWN",
	"ACTIVE",
	"LSLEEP",
	"DSLEEP",
};

const char *_action_self_protected_str[] = {
	"ACT_SELF_PROTECTED_RSVD",
	"MESH_OPEN",
	"MESH_CONF",
	"MESH_CLOSE",
	"MESH_GK_INFORM",
	"MESH_GK_ACK",
};

inline u8 *rtw_set_ie_mesh_id(u8 *buf, u32 *buf_len, const char *mesh_id, u8 id_len)
{
	return rtw_set_ie(buf, WLAN_EID_MESH_ID, id_len, mesh_id, buf_len);
}

inline u8 *rtw_set_ie_mesh_config(u8 *buf, u32 *buf_len
	, u8 path_sel_proto, u8 path_sel_metric, u8 congest_ctl_mode, u8 sync_method, u8 auth_proto
	, u8 num_of_peerings, bool cto_mgate, bool cto_as
	, bool accept_peerings, bool mcca_sup, bool mcca_en, bool forwarding
	, bool mbca_en, bool tbtt_adj, bool ps_level)
{

	u8 conf[7] = {0};

	SET_MESH_CONF_ELE_PATH_SEL_PROTO_ID(conf, path_sel_proto);
	SET_MESH_CONF_ELE_PATH_SEL_METRIC_ID(conf, path_sel_metric);
	SET_MESH_CONF_ELE_CONGEST_CTRL_MODE_ID(conf, congest_ctl_mode);
	SET_MESH_CONF_ELE_SYNC_METHOD_ID(conf, sync_method);
	SET_MESH_CONF_ELE_AUTH_PROTO_ID(conf, auth_proto);

	SET_MESH_CONF_ELE_CTO_MGATE(conf, cto_mgate);
	SET_MESH_CONF_ELE_NUM_OF_PEERINGS(conf, num_of_peerings);
	SET_MESH_CONF_ELE_CTO_AS(conf, cto_as);

	SET_MESH_CONF_ELE_ACCEPT_PEERINGS(conf, accept_peerings);
	SET_MESH_CONF_ELE_MCCA_SUP(conf, mcca_sup);
	SET_MESH_CONF_ELE_MCCA_EN(conf, mcca_en);
	SET_MESH_CONF_ELE_FORWARDING(conf, forwarding);
	SET_MESH_CONF_ELE_MBCA_EN(conf, mbca_en);
	SET_MESH_CONF_ELE_TBTT_ADJ(conf, tbtt_adj);
	SET_MESH_CONF_ELE_PS_LEVEL(conf, ps_level);

	return rtw_set_ie(buf, WLAN_EID_MESH_CONFIG, 7, conf, buf_len);
}

inline u8 *rtw_set_ie_mpm(u8 *buf, u32 *buf_len
	, u8 proto_id, u16 llid, u16 *plid, u16 *reason, u8 *chosen_pmk)
{
	u8 data[24] = {0};
	u8 *pos = data;

	RTW_PUT_LE16(pos, proto_id);
	pos += 2;

	RTW_PUT_LE16(pos, llid);
	pos += 2;

	if (plid) {
		RTW_PUT_LE16(pos, *plid);
		pos += 2;
	}

	if (reason) {
		RTW_PUT_LE16(pos, *reason);
		pos += 2;
	}

	if (chosen_pmk) {
		_rtw_memcpy(pos, chosen_pmk, 16);
		pos += 16;
	}

	return rtw_set_ie(buf, WLAN_EID_MPM, pos - data, data, buf_len);
}

bool rtw_bss_is_forwarding(WLAN_BSSID_EX *bss)
{
	u8 *ie;
	int ie_len;
	bool ret = 0;

	ie = rtw_get_ie(BSS_EX_TLV_IES(bss), WLAN_EID_MESH_CONFIG, &ie_len,
			BSS_EX_TLV_IES_LEN(bss));
	if (!ie || ie_len != 7)
		goto exit;

	ret = GET_MESH_CONF_ELE_FORWARDING(ie + 2);

exit:
	return ret;
}

bool rtw_bss_is_cto_mgate(WLAN_BSSID_EX *bss)
{
	u8 *ie;
	int ie_len;
	bool ret = 0;

	ie = rtw_get_ie(BSS_EX_TLV_IES(bss), WLAN_EID_MESH_CONFIG, &ie_len,
			BSS_EX_TLV_IES_LEN(bss));
	if (!ie || ie_len != 7)
		goto exit;

	ret = GET_MESH_CONF_ELE_CTO_MGATE(ie + 2);

exit:
	return ret;
}

int _rtw_bss_is_same_mbss(WLAN_BSSID_EX *a, WLAN_BSSID_EX *b, u8 **a_mconf_ie_r, u8 **b_mconf_ie_r)
{
	int ret = 0;
	u8 *a_mconf_ie, *b_mconf_ie;
	sint a_mconf_ie_len, b_mconf_ie_len;

	if (a->InfrastructureMode != Ndis802_11_mesh)
		goto exit;
	a_mconf_ie = rtw_get_ie(BSS_EX_TLV_IES(a), WLAN_EID_MESH_CONFIG, &a_mconf_ie_len, BSS_EX_TLV_IES_LEN(a));
	if (!a_mconf_ie || a_mconf_ie_len != 7)
		goto exit;
	if (a_mconf_ie_r)
		*a_mconf_ie_r = a_mconf_ie;

	if (b->InfrastructureMode != Ndis802_11_mesh)
		goto exit;
	b_mconf_ie = rtw_get_ie(BSS_EX_TLV_IES(b), WLAN_EID_MESH_CONFIG, &b_mconf_ie_len, BSS_EX_TLV_IES_LEN(b));
	if (!b_mconf_ie || b_mconf_ie_len != 7)
		goto exit;
	if (b_mconf_ie_r)
		*b_mconf_ie_r = b_mconf_ie;

	if (a->mesh_id.SsidLength != b->mesh_id.SsidLength
		|| _rtw_memcmp(a->mesh_id.Ssid, b->mesh_id.Ssid, a->mesh_id.SsidLength) == _FALSE)
		goto exit;

	if (_rtw_memcmp(a_mconf_ie + 2, b_mconf_ie + 2, 5) == _FALSE)
		goto exit;

	ret = 1;

exit:
	return ret;
}

int rtw_bss_is_same_mbss(WLAN_BSSID_EX *a, WLAN_BSSID_EX *b)
{
	return _rtw_bss_is_same_mbss(a, b, NULL, NULL);
}

int rtw_bss_is_candidate_mesh_peer(_adapter *adapter, WLAN_BSSID_EX *target, u8 ch, u8 add_peer)
{
	int ret = 0;
	WLAN_BSSID_EX *self = &adapter->mlmepriv.cur_network.network;
	u8 *s_mconf_ie, *t_mconf_ie;
	u8 auth_pid;
	int i, j;

	if (ch && self->Configuration.DSConfig != target->Configuration.DSConfig)
		goto exit;

	if (!_rtw_bss_is_same_mbss(self, target, &s_mconf_ie, &t_mconf_ie))
		goto exit;

	if (add_peer) {
		/* Accept additional mesh peerings */
		if (GET_MESH_CONF_ELE_ACCEPT_PEERINGS(t_mconf_ie + 2) == 0)
			goto exit;
	}

	/* BSSBasicRateSet */
	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		if (target->SupportedRates[i] == 0)
			break;	
		if (target->SupportedRates[i] & 0x80) {
			u8 match = 0;

			if (!ch) {
				/* off-channel, check target with our hardcode capability */
				if (target->Configuration.DSConfig > 14)
					match = rtw_is_basic_rate_ofdm(target->SupportedRates[i]);
				else
					match = rtw_is_basic_rate_mix(target->SupportedRates[i]);
			} else { 
				for (j = 0; j < NDIS_802_11_LENGTH_RATES_EX; j++) {
					if (self->SupportedRates[j] == 0)
						break;
					if (self->SupportedRates[j] == target->SupportedRates[i]) {
						match = 1;
						break;
					}
				}
			}
			if (!match)
				goto exit;
		}
	}

	/* BSSBasicMCSSet */


	auth_pid = GET_MESH_CONF_ELE_AUTH_PROTO_ID(s_mconf_ie + 2);
	if (auth_pid && auth_pid <= 2) {
		struct security_priv *sec = &adapter->securitypriv;
		u8 *rsn_ie;
		int rsn_ie_len;
		int group_cipher = 0, pairwise_cipher = 0, gmcs = 0;
		u8 mfp_opt = MFP_NO;

		/* 802.1X connected to AS ? */

		/* RSN */
		rsn_ie = rtw_get_wpa2_ie(BSS_EX_TLV_IES(target), &rsn_ie_len, BSS_EX_TLV_IES_LEN(target));
		if (!rsn_ie || rsn_ie_len == 0)
			goto exit;
		if (rtw_parse_wpa2_ie(rsn_ie, rsn_ie_len + 2, &group_cipher, &pairwise_cipher, &gmcs, NULL, &mfp_opt) != _SUCCESS)
			goto exit;
		if ((sec->mfp_opt == MFP_REQUIRED && mfp_opt < MFP_OPTIONAL)
			|| (mfp_opt == MFP_REQUIRED && sec->mfp_opt < MFP_OPTIONAL))
			goto exit;
		if (!(sec->wpa2_group_cipher & group_cipher))
			goto exit;
		if (!(sec->wpa2_pairwise_cipher & pairwise_cipher))
			goto exit;
		#ifdef CONFIG_IEEE80211W
		if ((sec->mfp_opt >= MFP_OPTIONAL && mfp_opt >= MFP_OPTIONAL)
			&& security_type_bip_to_gmcs(sec->dot11wCipher) != gmcs)
			goto exit;
		#endif
	}

	ret = 1;

exit:
	return ret;
}

void rtw_mesh_bss_peering_status(WLAN_BSSID_EX *bss, u8 *nop, u8 *accept)
{
	u8 *ie;
	int ie_len;

	if (nop)
		*nop = 0;
	if (accept)
		*accept = 0;

	ie = rtw_get_ie(BSS_EX_TLV_IES(bss), WLAN_EID_MESH_CONFIG, &ie_len,
			BSS_EX_TLV_IES_LEN(bss));
	if (!ie || ie_len != 7)
		goto exit;

	if (nop)
		*nop = GET_MESH_CONF_ELE_NUM_OF_PEERINGS(ie + 2);
	if (accept)
		*accept = GET_MESH_CONF_ELE_ACCEPT_PEERINGS(ie + 2);

exit:
	return;
}

#if CONFIG_RTW_MESH_ACNODE_PREVENT
void rtw_mesh_update_scanned_acnode_status(_adapter *adapter, struct wlan_network *scanned)
{
	bool acnode;
	u8 nop, accept;

	rtw_mesh_bss_peering_status(&scanned->network, &nop, &accept);

	acnode = !nop && accept;

	if (acnode && scanned->acnode_stime == 0) {
		scanned->acnode_stime = rtw_get_current_time();
		if (scanned->acnode_stime == 0)
			scanned->acnode_stime++;
	} else if (!acnode) {
		scanned->acnode_stime = 0;
		scanned->acnode_notify_etime = 0;
	}
}

bool rtw_mesh_scanned_is_acnode_confirmed(_adapter *adapter, struct wlan_network *scanned)
{
	return scanned->acnode_stime
			&& rtw_get_passing_time_ms(scanned->acnode_stime)
				> adapter->mesh_cfg.peer_sel_policy.acnode_conf_timeout_ms;
}

static bool rtw_mesh_scanned_is_acnode_allow_notify(_adapter *adapter, struct wlan_network *scanned)
{
	return scanned->acnode_notify_etime
			&& rtw_time_after(scanned->acnode_notify_etime, rtw_get_current_time());
}

bool rtw_mesh_acnode_prevent_allow_sacrifice(_adapter *adapter)
{
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct sta_priv *stapriv = &adapter->stapriv;
	bool allow = 0;

	if (!mcfg->peer_sel_policy.acnode_prevent
		|| mcfg->max_peer_links <= 1
		|| stapriv->asoc_list_cnt < mcfg->max_peer_links)
		goto exit;

#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
	if (rtw_mesh_cto_mgate_required(adapter))
		goto exit;
#endif

	allow = 1;

exit:
	return allow;
}

static bool rtw_mesh_acnode_candidate_exist(_adapter *adapter)
{
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct sta_priv *stapriv = &adapter->stapriv;
	struct mlme_priv *mlme = &adapter->mlmepriv;
	_queue *queue = &(mlme->scanned_queue);
	_list *head, *list;
	_irqL irqL;
	struct wlan_network *scanned = NULL;
	struct sta_info *sta = NULL;
	bool need = 0;

	_enter_critical_bh(&(mlme->scanned_queue.lock), &irqL);

	head = get_list_head(queue);
	list = get_next(head);
	while (!rtw_end_of_queue_search(head, list)) {
		scanned = LIST_CONTAINOR(list, struct wlan_network, list);
		list = get_next(list);

		if (rtw_get_passing_time_ms(scanned->last_scanned) < mcfg->peer_sel_policy.scanr_exp_ms
			&& rtw_mesh_scanned_is_acnode_confirmed(adapter, scanned)
			&& (!mcfg->rssi_threshold || mcfg->rssi_threshold <= scanned->network.Rssi)
			#if CONFIG_RTW_MACADDR_ACL
			&& rtw_access_ctrl(adapter, scanned->network.MacAddress) == _TRUE
			#endif
			&& rtw_bss_is_candidate_mesh_peer(adapter, &scanned->network, 1, 1)
			#if CONFIG_RTW_MESH_PEER_BLACKLIST
			&& !rtw_mesh_peer_blacklist_search(adapter, scanned->network.MacAddress)
			#endif
			#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
			&& rtw_mesh_cto_mgate_network_filter(adapter, scanned)
			#endif
		) {
			need = 1;
			break;
		}
	}

	_exit_critical_bh(&(mlme->scanned_queue.lock), &irqL);

	return need;
}

static int rtw_mesh_acnode_prevent_sacrifice_chk(_adapter *adapter, struct sta_info **sac, struct sta_info *com)
{
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	int updated = 0;

	/*
	* TODO: compare next_hop reference cnt of forwarding info
	* don't sacrifice working next_hop or choose sta with least cnt
	*/

	if (*sac == NULL) {
		updated = 1;
		goto exit;
	}

#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
	if (mcfg->peer_sel_policy.cto_mgate_require
		&& !mcfg->dot11MeshGateAnnouncementProtocol
	) {
		if (IS_CTO_MGATE_CONF_TIMEOUT(com->plink)) {
			if (!IS_CTO_MGATE_CONF_TIMEOUT((*sac)->plink)) {
				/* blacklist > not blacklist */
				updated = 1;
				goto exit;
			}
		} else if (!IS_CTO_MGATE_CONF_DISABLED(com->plink)) {
			if (IS_CTO_MGATE_CONF_DISABLED((*sac)->plink)) {
				/* confirming > disabled */
				updated = 1;
				goto exit;
			}
		}
	}
#endif

exit:
	if (updated)
		*sac = com;

	return updated;
}

struct sta_info *_rtw_mesh_acnode_prevent_pick_sacrifice(_adapter *adapter)
{
	struct sta_priv *stapriv = &adapter->stapriv;
	_list *head, *list;
	struct sta_info *sta, *sacrifice = NULL;
	u8 nop;

	head = &stapriv->asoc_list;
	list = get_next(head);
	while (rtw_end_of_queue_search(head, list) == _FALSE) {
		sta = LIST_CONTAINOR(list, struct sta_info, asoc_list);
		list = get_next(list);

		if (!sta->plink || !sta->plink->scanned) {
			rtw_warn_on(1);
			continue;
		}

		rtw_mesh_bss_peering_status(&sta->plink->scanned->network, &nop, NULL);
		if (nop < 2)
			continue;

		rtw_mesh_acnode_prevent_sacrifice_chk(adapter, &sacrifice, sta);
	}

	return sacrifice;
}

struct sta_info *rtw_mesh_acnode_prevent_pick_sacrifice(_adapter *adapter)
{
	struct sta_priv *stapriv = &adapter->stapriv;
	struct sta_info *sacrifice = NULL;

	enter_critical_bh(&stapriv->asoc_list_lock);

	sacrifice = _rtw_mesh_acnode_prevent_pick_sacrifice(adapter);

	exit_critical_bh(&stapriv->asoc_list_lock);

	return sacrifice;
}

static void rtw_mesh_acnode_rsvd_chk(_adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	u8 acnode_rsvd = 0;

	if (rtw_mesh_acnode_prevent_allow_sacrifice(adapter)
		&& rtw_mesh_acnode_prevent_pick_sacrifice(adapter)
		&& rtw_mesh_acnode_candidate_exist(adapter))
		acnode_rsvd = 1;

	if (plink_ctl->acnode_rsvd != acnode_rsvd) {
		plink_ctl->acnode_rsvd = acnode_rsvd;
		RTW_INFO(FUNC_ADPT_FMT" acnode_rsvd = %d\n", FUNC_ADPT_ARG(adapter), plink_ctl->acnode_rsvd);
		update_beacon(adapter, WLAN_EID_MESH_CONFIG, NULL, 1, 0);
	}
}

static void rtw_mesh_acnode_set_notify_etime(_adapter *adapter, u8 *rframe_whdr)
{
	if (adapter->mesh_info.plink_ctl.acnode_rsvd) {
		struct wlan_network *scanned = rtw_find_network(&adapter->mlmepriv.scanned_queue, get_addr2_ptr(rframe_whdr));

		if (rtw_mesh_scanned_is_acnode_confirmed(adapter, scanned)) {
			scanned->acnode_notify_etime = rtw_get_current_time()
				+ rtw_ms_to_systime(adapter->mesh_cfg.peer_sel_policy.acnode_notify_timeout_ms);
			if (scanned->acnode_notify_etime == 0)
				scanned->acnode_notify_etime++;
		}
	}
}

void dump_mesh_acnode_prevent_settings(void *sel, _adapter *adapter)
{
	struct mesh_peer_sel_policy *peer_sel_policy = &adapter->mesh_cfg.peer_sel_policy;

	RTW_PRINT_SEL(sel, "%-6s %-12s %-14s\n"
		, "enable", "conf_timeout", "nofity_timeout");
	RTW_PRINT_SEL(sel, "%6u %12u %14u\n"
		, peer_sel_policy->acnode_prevent
		, peer_sel_policy->acnode_conf_timeout_ms
		, peer_sel_policy->acnode_notify_timeout_ms);
}
#endif /* CONFIG_RTW_MESH_ACNODE_PREVENT */

#if CONFIG_RTW_MESH_PEER_BLACKLIST
int rtw_mesh_peer_blacklist_add(_adapter *adapter, const u8 *addr)
{
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;

	return rtw_blacklist_add(&plink_ctl->peer_blacklist, addr
		, mcfg->peer_sel_policy.peer_blacklist_timeout_ms);
}

int rtw_mesh_peer_blacklist_del(_adapter *adapter, const u8 *addr)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;

	return rtw_blacklist_del(&plink_ctl->peer_blacklist, addr);
}

int rtw_mesh_peer_blacklist_search(_adapter *adapter, const u8 *addr)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;

	return rtw_blacklist_search(&plink_ctl->peer_blacklist, addr);
}

void rtw_mesh_peer_blacklist_flush(_adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;

	rtw_blacklist_flush(&plink_ctl->peer_blacklist);
}

void dump_mesh_peer_blacklist(void *sel, _adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;

	dump_blacklist(sel, &plink_ctl->peer_blacklist, "blacklist");
}

void dump_mesh_peer_blacklist_settings(void *sel, _adapter *adapter)
{
	struct mesh_peer_sel_policy *peer_sel_policy = &adapter->mesh_cfg.peer_sel_policy;

	RTW_PRINT_SEL(sel, "%-12s %-17s\n"
		, "conf_timeout", "blacklist_timeout");
	RTW_PRINT_SEL(sel, "%12u %17u\n"
		, peer_sel_policy->peer_conf_timeout_ms
		, peer_sel_policy->peer_blacklist_timeout_ms);
}
#endif /* CONFIG_RTW_MESH_PEER_BLACKLIST */

#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
u8 rtw_mesh_cto_mgate_required(_adapter *adapter)
{
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;

	return mcfg->peer_sel_policy.cto_mgate_require
		&& !rtw_bss_is_cto_mgate(&(mlmeext->mlmext_info.network));
}

u8 rtw_mesh_cto_mgate_network_filter(_adapter *adapter, struct wlan_network *scanned)
{
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;

	return !rtw_mesh_cto_mgate_required(adapter)
			|| (rtw_bss_is_cto_mgate(&scanned->network)
				&& !rtw_mesh_cto_mgate_blacklist_search(adapter, scanned->network.MacAddress));
}

int rtw_mesh_cto_mgate_blacklist_add(_adapter *adapter, const u8 *addr)
{
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;

	return rtw_blacklist_add(&plink_ctl->cto_mgate_blacklist, addr
		, mcfg->peer_sel_policy.cto_mgate_blacklist_timeout_ms);
}

int rtw_mesh_cto_mgate_blacklist_del(_adapter *adapter, const u8 *addr)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;

	return rtw_blacklist_del(&plink_ctl->cto_mgate_blacklist, addr);
}

int rtw_mesh_cto_mgate_blacklist_search(_adapter *adapter, const u8 *addr)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;

	return rtw_blacklist_search(&plink_ctl->cto_mgate_blacklist, addr);
}

void rtw_mesh_cto_mgate_blacklist_flush(_adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;

	rtw_blacklist_flush(&plink_ctl->cto_mgate_blacklist);
}

void dump_mesh_cto_mgate_blacklist(void *sel, _adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;

	dump_blacklist(sel, &plink_ctl->cto_mgate_blacklist, "blacklist");
}

void dump_mesh_cto_mgate_blacklist_settings(void *sel, _adapter *adapter)
{
	struct mesh_peer_sel_policy *peer_sel_policy = &adapter->mesh_cfg.peer_sel_policy;

	RTW_PRINT_SEL(sel, "%-12s %-17s\n"
		, "conf_timeout", "blacklist_timeout");
	RTW_PRINT_SEL(sel, "%12u %17u\n"
		, peer_sel_policy->cto_mgate_conf_timeout_ms
		, peer_sel_policy->cto_mgate_blacklist_timeout_ms);
}

static void rtw_mesh_cto_mgate_blacklist_chk(_adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	_queue *blist = &plink_ctl->cto_mgate_blacklist;
	_list *list, *head;
	struct blacklist_ent *ent = NULL;
	struct wlan_network *scanned = NULL;

	enter_critical_bh(&blist->lock);
	head = &blist->queue;
	list = get_next(head);
	while (rtw_end_of_queue_search(head, list) == _FALSE) {
		ent = LIST_CONTAINOR(list, struct blacklist_ent, list);
		list = get_next(list);

		if (rtw_time_after(rtw_get_current_time(), ent->exp_time)) {
			rtw_list_delete(&ent->list);
			rtw_mfree(ent, sizeof(struct blacklist_ent));
			continue;
		}

		scanned = rtw_find_network(&adapter->mlmepriv.scanned_queue, ent->addr);
		if (!scanned)
			continue;

		if (rtw_bss_is_forwarding(&scanned->network)) {
			rtw_list_delete(&ent->list);
			rtw_mfree(ent, sizeof(struct blacklist_ent));
		}
	}

	exit_critical_bh(&blist->lock);
}
#endif /* CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST */

void rtw_chk_candidate_peer_notify(_adapter *adapter, struct wlan_network *scanned)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	struct mlme_priv *mlme = &adapter->mlmepriv;
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	bool acnode = 0;

	if (IS_CH_WAITING(rfctl) && !IS_UNDER_CAC(rfctl))
		goto exit;

	if (plink_ctl->num >= RTW_MESH_MAX_PEER_CANDIDATES)
		goto exit;

#if CONFIG_RTW_MESH_ACNODE_PREVENT
	if (plink_ctl->acnode_rsvd) {
		acnode = rtw_mesh_scanned_is_acnode_confirmed(adapter, scanned);
		if (acnode && !rtw_mesh_scanned_is_acnode_allow_notify(adapter, scanned))
			goto exit;
	}
#endif

	/* wpa_supplicant's auto peer will initiate peering when candidate peer is reported without max_peer_links consideration */
	if (plink_ctl->num >= mcfg->max_peer_links + acnode ? 1 : 0)
		goto exit;

	if (rtw_get_passing_time_ms(scanned->last_scanned) >= mcfg->peer_sel_policy.scanr_exp_ms
		|| (mcfg->rssi_threshold && mcfg->rssi_threshold > scanned->network.Rssi)
		|| !rtw_bss_is_candidate_mesh_peer(adapter, &scanned->network, 1, 1)
		#if CONFIG_RTW_MACADDR_ACL
		|| rtw_access_ctrl(adapter, scanned->network.MacAddress) == _FALSE
		#endif
		|| rtw_mesh_plink_get(adapter, scanned->network.MacAddress)
		#if CONFIG_RTW_MESH_PEER_BLACKLIST
		|| rtw_mesh_peer_blacklist_search(adapter, scanned->network.MacAddress)
		#endif
		#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
		|| !rtw_mesh_cto_mgate_network_filter(adapter, scanned)
		#endif
	)
		goto exit;

#if CONFIG_RTW_MESH_ACNODE_PREVENT
	if (acnode) {
		scanned->acnode_notify_etime = 0;
		RTW_INFO(FUNC_ADPT_FMT" acnode "MAC_FMT"\n"
			, FUNC_ADPT_ARG(adapter), MAC_ARG(scanned->network.MacAddress));
	}
#endif

#ifdef CONFIG_IOCTL_CFG80211
	rtw_cfg80211_notify_new_peer_candidate(adapter->rtw_wdev
		, scanned->network.MacAddress
		, BSS_EX_TLV_IES(&scanned->network)
		, BSS_EX_TLV_IES_LEN(&scanned->network)
		, scanned->network.Rssi
		, GFP_ATOMIC
	);
#endif

exit:
	return;
}

void rtw_mesh_peer_status_chk(_adapter *adapter)
{
	struct mlme_priv *mlme = &adapter->mlmepriv;
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	struct mesh_plink_ent *plink;
	_list *head, *list;
	struct sta_info *sta = NULL;
	struct sta_priv *stapriv = &adapter->stapriv;
	int stainfo_offset;
#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
	u8 cto_mgate, forwarding, mgate;
#endif
	u8 flush;
	s8 flush_list[NUM_STA];
	u8 flush_num = 0;
	int i;

#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
	if (rtw_mesh_cto_mgate_required(adapter)) {
		/* active scan on operating channel */
		issue_probereq_ex(adapter, &adapter->mlmepriv.cur_network.network.mesh_id, NULL, 0, 0, 0, 0);
	}
#endif

	enter_critical_bh(&(plink_ctl->lock));

	/* check established peers */
	enter_critical_bh(&stapriv->asoc_list_lock);

	head = &stapriv->asoc_list;
	list = get_next(head);
	while (rtw_end_of_queue_search(head, list) == _FALSE) {
		sta = LIST_CONTAINOR(list, struct sta_info, asoc_list);
		list = get_next(list);

		if (!sta->plink || !sta->plink->scanned) {
			rtw_warn_on(1);
			continue;
		}
		plink = sta->plink;
		flush = 0;

		/* remove unsuitable peer */
		if (!rtw_bss_is_candidate_mesh_peer(adapter, &plink->scanned->network, 1, 0)
			#if CONFIG_RTW_MACADDR_ACL
			|| rtw_access_ctrl(adapter, plink->addr) == _FALSE
			#endif
		) {
			flush = 1;
			goto flush_add;
		}

		#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
		cto_mgate = rtw_bss_is_cto_mgate(&(plink->scanned->network));
		forwarding = rtw_bss_is_forwarding(&(plink->scanned->network));
		mgate = rtw_mesh_gate_search(minfo->mesh_paths, sta->cmn.mac_addr);

		/* CTO_MGATE required, remove peer without CTO_MGATE */
		if (rtw_mesh_cto_mgate_required(adapter) && !cto_mgate) {
			flush = 1;
			goto flush_add;
		}

		/* cto_mgate_conf status update */
		if (IS_CTO_MGATE_CONF_DISABLED(plink)) {
			if (cto_mgate && !forwarding && !mgate)
				SET_CTO_MGATE_CONF_END_TIME(plink, mcfg->peer_sel_policy.cto_mgate_conf_timeout_ms);
			else
				rtw_mesh_cto_mgate_blacklist_del(adapter, sta->cmn.mac_addr);
		} else {
			/* cto_mgate_conf ongoing */
			if (cto_mgate && !forwarding && !mgate) {
				if (IS_CTO_MGATE_CONF_TIMEOUT(plink)) {
					rtw_mesh_cto_mgate_blacklist_add(adapter, sta->cmn.mac_addr);

					/* CTO_MGATE required, remove peering can't achieve CTO_MGATE */
					if (rtw_mesh_cto_mgate_required(adapter)) {
						flush = 1;
						goto flush_add;
					}	
				}
			} else {
				SET_CTO_MGATE_CONF_DISABLED(plink);
				rtw_mesh_cto_mgate_blacklist_del(adapter, sta->cmn.mac_addr);
			}
		}
		#endif /* CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST */

flush_add:
		if (flush) {
			rtw_list_delete(&sta->asoc_list);
			stapriv->asoc_list_cnt--;
#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
			if (sta->tbtx_enable)
				stapriv->tbtx_asoc_list_cnt--;
#endif
			STA_SET_MESH_PLINK(sta, NULL);

			stainfo_offset = rtw_stainfo_offset(stapriv, sta);
			if (stainfo_offset_valid(stainfo_offset))
				flush_list[flush_num++] = stainfo_offset;
			else
				rtw_warn_on(1);
		}
	}

	exit_critical_bh(&stapriv->asoc_list_lock);

	/* check non-established peers */
	for (i = 0; i < RTW_MESH_MAX_PEER_CANDIDATES; i++) {
		plink = &plink_ctl->ent[i];
		if (plink->valid != _TRUE || plink->plink_state == RTW_MESH_PLINK_ESTAB)
			continue;

		/* remove unsuitable peer */
		if (!rtw_bss_is_candidate_mesh_peer(adapter, &plink->scanned->network, 1, 1)
			#if CONFIG_RTW_MACADDR_ACL
			|| rtw_access_ctrl(adapter, plink->addr) == _FALSE
			#endif
		) {
			_rtw_mesh_expire_peer_ent(adapter, plink);
			continue;
		}

		#if CONFIG_RTW_MESH_PEER_BLACKLIST
		/* peer confirm check timeout, add to black list */
		if (IS_PEER_CONF_TIMEOUT(plink)) {
			rtw_mesh_peer_blacklist_add(adapter, plink->addr);
			_rtw_mesh_expire_peer_ent(adapter, plink);
		}
		#endif
	}

	exit_critical_bh(&(plink_ctl->lock));

	if (flush_num) {
		u8 sta_addr[ETH_ALEN];
		u8 updated = _FALSE;

		for (i = 0; i < flush_num; i++) {
			sta = rtw_get_stainfo_by_offset(stapriv, flush_list[i]);
			_rtw_memcpy(sta_addr, sta->cmn.mac_addr, ETH_ALEN);

			updated |= ap_free_sta(adapter, sta, _TRUE, WLAN_REASON_DEAUTH_LEAVING, _FALSE);
			rtw_mesh_expire_peer(adapter, sta_addr);
		}

		associated_clients_update(adapter, updated, STA_INFO_UPDATE_ALL);
	}

#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
	/* loop cto_mgate_blacklist to remove ent according to scan_r */
	rtw_mesh_cto_mgate_blacklist_chk(adapter);
#endif

#if CONFIG_RTW_MESH_ACNODE_PREVENT
	rtw_mesh_acnode_rsvd_chk(adapter);
#endif

	return;
}

#if CONFIG_RTW_MESH_OFFCH_CAND
static u8 rtw_mesh_offch_cto_mgate_required(_adapter *adapter)
{
#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct mlme_priv *mlme = &adapter->mlmepriv;
	_queue *queue = &(mlme->scanned_queue);
	_list *head, *pos;
	struct wlan_network *scanned = NULL;
	u8 ret = 0;

	if (!rtw_mesh_cto_mgate_required(adapter))
		goto exit;

	enter_critical_bh(&(mlme->scanned_queue.lock));

	head = get_list_head(queue);
	pos = get_next(head);
	while (!rtw_end_of_queue_search(head, pos)) {
		scanned = LIST_CONTAINOR(pos, struct wlan_network, list);

		if (rtw_get_passing_time_ms(scanned->last_scanned) < mcfg->peer_sel_policy.scanr_exp_ms
			&& (!mcfg->rssi_threshold || mcfg->rssi_threshold <= scanned->network.Rssi)
			#if CONFIG_RTW_MACADDR_ACL
			&& rtw_access_ctrl(adapter, scanned->network.MacAddress) == _TRUE
			#endif
			&& rtw_bss_is_candidate_mesh_peer(adapter, &scanned->network, 1, 1)
			&& rtw_bss_is_cto_mgate(&scanned->network)
			#if CONFIG_RTW_MESH_PEER_BLACKLIST
			&& !rtw_mesh_peer_blacklist_search(adapter, scanned->network.MacAddress)
			#endif
			&& !rtw_mesh_cto_mgate_blacklist_search(adapter, scanned->network.MacAddress)
		)
			break;

		pos = get_next(pos);
	}

	if (rtw_end_of_queue_search(head, pos))
		ret = 1;

	exit_critical_bh(&(mlme->scanned_queue.lock));

exit:
	return ret;
#else
	return 0;
#endif /* CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST */
}

u8 rtw_mesh_offch_candidate_accepted(_adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	u8 ret = 0;

	if (!adapter->mesh_cfg.peer_sel_policy.offch_cand)
		goto exit;

	ret = MLME_IS_MESH(adapter) && MLME_IS_ASOC(adapter)
		&& (!plink_ctl->num || rtw_mesh_offch_cto_mgate_required(adapter))
		;

#ifdef CONFIG_CONCURRENT_MODE
	if (ret) {
		struct mi_state mstate_no_self;

		rtw_mi_status_no_self(adapter, &mstate_no_self);
		if (MSTATE_STA_LD_NUM(&mstate_no_self))
			ret = 0;
	}
#endif

exit:
	return ret;
}

/*
 * this function is called under off channel candidate is required 
 * the channel with maximum candidate count is selected
*/
u8 rtw_mesh_select_operating_ch(_adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct mlme_priv *mlme = &adapter->mlmepriv;
	_queue *queue = &(mlme->scanned_queue);
	_list *head, *pos;
	_irqL irqL;
	struct wlan_network *scanned = NULL;
	int i;
	/* statistics for candidate accept peering */
	u8 cand_ap_cnt[MAX_CHANNEL_NUM] = {0};
	u8 max_cand_ap_ch = 0;
	u8 max_cand_ap_cnt = 0;
	/* statistics for candidate including not accept peering */
	u8 cand_cnt[MAX_CHANNEL_NUM] = {0};
	u8 max_cand_ch = 0;
	u8 max_cand_cnt = 0;

	_enter_critical_bh(&(mlme->scanned_queue.lock), &irqL);

	head = get_list_head(queue);
	pos = get_next(head);
	while (!rtw_end_of_queue_search(head, pos)) {
		scanned = LIST_CONTAINOR(pos, struct wlan_network, list);
		pos = get_next(pos);

		if (rtw_get_passing_time_ms(scanned->last_scanned) < mcfg->peer_sel_policy.scanr_exp_ms
			&& (!mcfg->rssi_threshold || mcfg->rssi_threshold <= scanned->network.Rssi)
			#if CONFIG_RTW_MACADDR_ACL
			&& rtw_access_ctrl(adapter, scanned->network.MacAddress) == _TRUE
			#endif
			&& rtw_bss_is_candidate_mesh_peer(adapter, &scanned->network, 0, 0)
			#if CONFIG_RTW_MESH_PEER_BLACKLIST
			&& !rtw_mesh_peer_blacklist_search(adapter, scanned->network.MacAddress)
			#endif
			#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
			&& rtw_mesh_cto_mgate_network_filter(adapter, scanned)
			#endif
		) {
			int ch_set_idx = rtw_chset_search_ch(rfctl->channel_set, scanned->network.Configuration.DSConfig);

			if (ch_set_idx >= 0
				&& rfctl->channel_set[ch_set_idx].ScanType != SCAN_PASSIVE
				&& !CH_IS_NON_OCP(&rfctl->channel_set[ch_set_idx])
			) {
				u8 nop, accept;

				rtw_mesh_bss_peering_status(&scanned->network, &nop, &accept);
				cand_cnt[ch_set_idx]++;
				if (max_cand_cnt < cand_cnt[ch_set_idx]) {
					max_cand_cnt = cand_cnt[ch_set_idx];
					max_cand_ch = rfctl->channel_set[ch_set_idx].ChannelNum;
				}
				if (accept) {
					cand_ap_cnt[ch_set_idx]++;
					if (max_cand_ap_cnt < cand_ap_cnt[ch_set_idx]) {
						max_cand_ap_cnt = cand_ap_cnt[ch_set_idx];
						max_cand_ap_ch = rfctl->channel_set[ch_set_idx].ChannelNum;
					}
				}
			}
		}
	}

	_exit_critical_bh(&(mlme->scanned_queue.lock), &irqL);

	return max_cand_ap_ch ? max_cand_ap_ch : max_cand_ch;
}

void dump_mesh_offch_cand_settings(void *sel, _adapter *adapter)
{
	struct mesh_peer_sel_policy *peer_sel_policy = &adapter->mesh_cfg.peer_sel_policy;

	RTW_PRINT_SEL(sel, "%-6s %-11s\n"
		, "enable", "find_int_ms");
	RTW_PRINT_SEL(sel, "%6u %11u\n"
		, peer_sel_policy->offch_cand, peer_sel_policy->offch_find_int_ms);
}
#endif /* CONFIG_RTW_MESH_OFFCH_CAND */

void dump_mesh_peer_sel_policy(void *sel, _adapter *adapter)
{
	struct mesh_peer_sel_policy *peer_sel_policy = &adapter->mesh_cfg.peer_sel_policy;

	RTW_PRINT_SEL(sel, "%-12s\n", "scanr_exp_ms");
	RTW_PRINT_SEL(sel, "%12u\n", peer_sel_policy->scanr_exp_ms);
}

void dump_mesh_networks(void *sel, _adapter *adapter)
{
#if CONFIG_RTW_MESH_ACNODE_PREVENT
#define NSTATE_TITLE_FMT_ACN " %-5s"
#define NSTATE_VALUE_FMT_ACN " %5d"
#define NSTATE_TITLE_ARG_ACN , "acn"
#define NSTATE_VALUE_ARG_ACN , (acn_ms < 99999 ? acn_ms : 99999)
#else
#define NSTATE_TITLE_FMT_ACN ""
#define NSTATE_VALUE_FMT_ACN ""
#define NSTATE_TITLE_ARG_ACN
#define NSTATE_VALUE_ARG_ACN
#endif

	struct mlme_priv *mlme = &(adapter->mlmepriv);
	_queue *queue = &(mlme->scanned_queue);
	struct wlan_network	*network;
	_list *list, *head;
	u8 same_mbss;
	u8 candidate;
	struct mesh_plink_ent *plink;
	u8 blocked;
	u8 established;
	s32 age_ms;
#if CONFIG_RTW_MESH_ACNODE_PREVENT
	s32 acn_ms;
#endif
	u8 *mesh_conf_ie;
	sint mesh_conf_ie_len;
	u8 auth_pid;
	u8 *rsn_ie;
	int rsn_ie_len;
	int gcs, pcs, gmcs;
	u8 mfp_opt;
	struct wlan_network **mesh_networks;
	u8 mesh_network_cnt = 0;
	int i;

	mesh_networks = rtw_zvmalloc(mlme->max_bss_cnt * sizeof(struct wlan_network *));
	if (!mesh_networks)
		return;

	enter_critical_bh(&queue->lock);
	head = get_list_head(queue);
	list = get_next(head);

	while (rtw_end_of_queue_search(head, list) == _FALSE) {
		network = LIST_CONTAINOR(list, struct wlan_network, list);
		list = get_next(list);

		if (network->network.InfrastructureMode != Ndis802_11_mesh)
			continue;

		mesh_conf_ie = rtw_get_ie(BSS_EX_TLV_IES(&network->network), WLAN_EID_MESH_CONFIG
			, &mesh_conf_ie_len, BSS_EX_TLV_IES_LEN(&network->network));
		if (!mesh_conf_ie || mesh_conf_ie_len != 7)
			continue;

		mesh_networks[mesh_network_cnt++] = network;
	}

	exit_critical_bh(&queue->lock);

	RTW_PRINT_SEL(sel, "  %-17s %-3s %-4s %-5s %-32s %-10s"
		" %-3s %-3s %-4s"
		" %-3s %-3s %-3s"
		NSTATE_TITLE_FMT_ACN
		"\n"
		, "bssid", "ch", "rssi", "age", "mesh_id", "P M C S A "
		, "pcs", "gcs", "gmcs"
		, "nop", "fwd", "cto"
		NSTATE_TITLE_ARG_ACN
	);

	if (MLME_IS_MESH(adapter) && MLME_IS_ASOC(adapter)) {
		network = &mlme->cur_network;
		mesh_conf_ie = rtw_get_ie(BSS_EX_TLV_IES(&network->network), WLAN_EID_MESH_CONFIG
			, &mesh_conf_ie_len, BSS_EX_TLV_IES_LEN(&network->network));
		if (mesh_conf_ie && mesh_conf_ie_len == 7) {
			gcs = pcs = gmcs = 0;
			mfp_opt = MFP_NO;
			auth_pid = GET_MESH_CONF_ELE_AUTH_PROTO_ID(mesh_conf_ie + 2);
			if (auth_pid && auth_pid <= 2) {
				rsn_ie = rtw_get_wpa2_ie(BSS_EX_TLV_IES(&network->network)
							, &rsn_ie_len, BSS_EX_TLV_IES_LEN(&network->network));
				if (rsn_ie && rsn_ie_len)
					rtw_parse_wpa2_ie(rsn_ie, rsn_ie_len + 2, &gcs, &pcs, &gmcs, NULL, &mfp_opt);
			}
			RTW_PRINT_SEL(sel, "* "MAC_FMT" %3d            %-32s %2x%2x%2x%2x%2x"
				" %03x %03x %c%03x"
				" %c%2u %3u %c%c "
				"\n"
				, MAC_ARG(network->network.MacAddress)
				, network->network.Configuration.DSConfig
				, network->network.mesh_id.Ssid
				, GET_MESH_CONF_ELE_PATH_SEL_PROTO_ID(mesh_conf_ie + 2)
				, GET_MESH_CONF_ELE_PATH_SEL_METRIC_ID(mesh_conf_ie + 2)
				, GET_MESH_CONF_ELE_CONGEST_CTRL_MODE_ID(mesh_conf_ie + 2)
				, GET_MESH_CONF_ELE_SYNC_METHOD_ID(mesh_conf_ie + 2)
				, auth_pid
				, pcs, gcs, mfp_opt == MFP_REQUIRED ? 'R' : (mfp_opt == MFP_OPTIONAL ? 'C' : ' ')
				, mfp_opt >= MFP_OPTIONAL ? gmcs : 0
				, GET_MESH_CONF_ELE_ACCEPT_PEERINGS(mesh_conf_ie + 2) ? '+' : ' '
				, GET_MESH_CONF_ELE_NUM_OF_PEERINGS(mesh_conf_ie + 2)
				, GET_MESH_CONF_ELE_FORWARDING(mesh_conf_ie + 2)
				, GET_MESH_CONF_ELE_CTO_MGATE(mesh_conf_ie + 2) ? 'G' : ' '
				, GET_MESH_CONF_ELE_CTO_AS(mesh_conf_ie + 2) ? 'A' : ' '
			);
		}
	}

	for (i = 0; i < mesh_network_cnt; i++) {
		network = mesh_networks[i];

		if (network->network.InfrastructureMode != Ndis802_11_mesh)
			continue;

		mesh_conf_ie = rtw_get_ie(BSS_EX_TLV_IES(&network->network), WLAN_EID_MESH_CONFIG
			, &mesh_conf_ie_len, BSS_EX_TLV_IES_LEN(&network->network));
		if (!mesh_conf_ie || mesh_conf_ie_len != 7)
			continue;

		gcs = pcs = gmcs = 0;
		mfp_opt = MFP_NO;
		auth_pid = GET_MESH_CONF_ELE_AUTH_PROTO_ID(mesh_conf_ie + 2);
		if (auth_pid && auth_pid <= 2) {
			rsn_ie = rtw_get_wpa2_ie(BSS_EX_TLV_IES(&network->network), &rsn_ie_len, BSS_EX_TLV_IES_LEN(&network->network));
			if (rsn_ie && rsn_ie_len)
				rtw_parse_wpa2_ie(rsn_ie, rsn_ie_len + 2, &gcs, &pcs, &gmcs, NULL, &mfp_opt);
		}
		age_ms = rtw_get_passing_time_ms(network->last_scanned);
		#if CONFIG_RTW_MESH_ACNODE_PREVENT
		if (network->acnode_stime == 0)
			acn_ms = 0;
		else
			acn_ms = rtw_get_passing_time_ms(network->acnode_stime);
		#endif
		same_mbss = 0;
		candidate = 0;
		plink = NULL;
		blocked = 0;
		established = 0;

		if (MLME_IS_MESH(adapter) && MLME_IS_ASOC(adapter)) {
			plink = rtw_mesh_plink_get(adapter, network->network.MacAddress);
			if (plink && plink->plink_state == RTW_MESH_PLINK_ESTAB)
				established = 1;
			else if (plink && plink->plink_state == RTW_MESH_PLINK_BLOCKED)
				blocked = 1;
			else if (plink)
				;
			else if (rtw_bss_is_candidate_mesh_peer(adapter, &network->network, 0, 1))
				candidate = 1;
			else if (rtw_bss_is_same_mbss(&mlme->cur_network.network, &network->network))
				same_mbss = 1;
		}

		RTW_PRINT_SEL(sel, "%c "MAC_FMT" %3d %4ld %5d %-32s %2x%2x%2x%2x%2x"
			" %03x %03x %c%03x"
			" %c%2u %3u %c%c "
			NSTATE_VALUE_FMT_ACN
			"\n"
			, established ? 'E' : (blocked ? 'B' : (plink ? 'N' : (candidate ? 'C' : (same_mbss ? 'S' : ' '))))
			, MAC_ARG(network->network.MacAddress)
			, network->network.Configuration.DSConfig
			, network->network.Rssi
			, age_ms < 99999 ? age_ms : 99999
			, network->network.mesh_id.Ssid
			, GET_MESH_CONF_ELE_PATH_SEL_PROTO_ID(mesh_conf_ie + 2)
			, GET_MESH_CONF_ELE_PATH_SEL_METRIC_ID(mesh_conf_ie + 2)
			, GET_MESH_CONF_ELE_CONGEST_CTRL_MODE_ID(mesh_conf_ie + 2)
			, GET_MESH_CONF_ELE_SYNC_METHOD_ID(mesh_conf_ie + 2)
			, auth_pid
			, pcs, gcs, mfp_opt == MFP_REQUIRED ? 'R' : (mfp_opt == MFP_OPTIONAL ? 'C' : ' ')
			, mfp_opt >= MFP_OPTIONAL ? gmcs : 0
			, GET_MESH_CONF_ELE_ACCEPT_PEERINGS(mesh_conf_ie + 2) ? '+' : ' '
			, GET_MESH_CONF_ELE_NUM_OF_PEERINGS(mesh_conf_ie + 2)
			, GET_MESH_CONF_ELE_FORWARDING(mesh_conf_ie + 2)
			, GET_MESH_CONF_ELE_CTO_MGATE(mesh_conf_ie + 2) ? 'G' : ' '
			, GET_MESH_CONF_ELE_CTO_AS(mesh_conf_ie + 2) ? 'A' : ' '
			NSTATE_VALUE_ARG_ACN
		);
	}

	rtw_vmfree(mesh_networks, mlme->max_bss_cnt * sizeof(struct wlan_network *));
}

void rtw_mesh_adjust_chbw(u8 req_ch, u8 *req_bw, u8 *req_offset)
{
	if (req_ch >= 5 && req_ch <= 9) {
		/* prevent secondary channel offset mismatch */
		if (*req_bw > CHANNEL_WIDTH_20) {
			*req_bw = CHANNEL_WIDTH_20;
			*req_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		}
	}
}

void rtw_mesh_sae_check_frames(_adapter *adapter, const u8 *buf, u32 len, u8 tx, u16 alg, u16 seq, u16 status)
{
#if CONFIG_RTW_MESH_PEER_BLACKLIST
	if (tx && seq == 1)
		rtw_mesh_plink_set_peer_conf_timeout(adapter, GetAddr1Ptr(buf));
#endif
}

#if CONFIG_RTW_MPM_TX_IES_SYNC_BSS
#ifdef CONFIG_RTW_MESH_AEK
static int rtw_mpm_ampe_dec(_adapter *adapter, struct mesh_plink_ent *plink
	, u8 *fhead, size_t flen, u8* fbody, u8 *mic_ie, u8 *ampe_buf)
{	
	int ret = _FAIL, verify_ret;
	const u8 *aad[] = {adapter_mac_addr(adapter), plink->addr, fbody};
	const size_t aad_len[] = {ETH_ALEN, ETH_ALEN, mic_ie - fbody};
	u8 *iv_crypt;
	size_t iv_crypt_len = flen - (mic_ie + 2 - fhead);

	iv_crypt = rtw_malloc(iv_crypt_len);
	if (!iv_crypt)
		goto exit;

	_rtw_memcpy(iv_crypt, mic_ie + 2, iv_crypt_len);

	verify_ret = rtw_aes_siv_decrypt(plink->aek, 32, iv_crypt, iv_crypt_len
		, 3, aad, aad_len, ampe_buf);

	rtw_mfree(iv_crypt, iv_crypt_len);

	if (verify_ret) {
		RTW_WARN("verify error, aek_valid=%u\n", plink->aek_valid);
		goto exit;
	} else if (*ampe_buf != WLAN_EID_AMPE) {
		RTW_WARN("plaintext is not AMPE IE\n");
		goto exit;
	} else if ( 16 /* AES_BLOCK_SIZE*/ + 2 + *(ampe_buf + 1) > iv_crypt_len) {
		RTW_WARN("plaintext AMPE IE length is not valid\n");
		goto exit;
	}

	ret = _SUCCESS;

exit:
	return ret;
}

static int rtw_mpm_ampe_enc(_adapter *adapter, struct mesh_plink_ent *plink
	, u8* fbody, u8 *mic_ie, u8 *ampe_buf, bool inverse)
{
	int ret = _FAIL, protect_ret;
	const u8 *aad[3];
	const size_t aad_len[3] = {ETH_ALEN, ETH_ALEN, mic_ie - fbody};
	u8 *ampe_ie;
	size_t ampe_ie_len = *(ampe_buf + 1) + 2; /* including id & len */

	if (inverse) {
		aad[0] = plink->addr;
		aad[1] = adapter_mac_addr(adapter);
	} else {
		aad[0] = adapter_mac_addr(adapter);
		aad[1] = plink->addr;
	}
	aad[2] = fbody;

	ampe_ie = rtw_malloc(ampe_ie_len);
	if (!ampe_ie)
		goto exit;

	_rtw_memcpy(ampe_ie, ampe_buf, ampe_ie_len);

	protect_ret = rtw_aes_siv_encrypt(plink->aek, 32, ampe_ie, ampe_ie_len
		, 3, aad, aad_len, mic_ie + 2);

	rtw_mfree(ampe_ie, ampe_ie_len);

	if (protect_ret) {
		RTW_WARN("protect error, aek_valid=%u\n", plink->aek_valid);
		goto exit;
	}

	ret = _SUCCESS;

exit:
	return ret;
}
#endif /* CONFIG_RTW_MESH_AEK */

static int rtw_mpm_tx_ies_sync_bss(_adapter *adapter, struct mesh_plink_ent *plink
	, u8 *fhead, size_t flen, u8* fbody, u8 tlv_ies_offset, u8 *mpm_ie, u8 *mic_ie
	, u8 **nbuf, size_t *nlen)
{
	int ret = _FAIL;
	struct mlme_priv *mlme = &(adapter->mlmepriv);
	struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *mlmeinfo = &(mlmeext->mlmext_info);
	WLAN_BSSID_EX *network = &(mlmeinfo->network);
	uint left;
	u8 *pos;

	uint mpm_ielen = *(mpm_ie + 1);
	u8 *fpos;
	u8 *new_buf = NULL;
	size_t new_len = 0;

	u8 *new_fhead;
	size_t new_flen;
	u8 *new_fbody;
	u8 *new_mic_ie;

#ifdef CONFIG_RTW_MESH_AEK
	u8 *ampe_buf = NULL;
	size_t ampe_buf_len = 0;

	/* decode */
	if (mic_ie) {
		ampe_buf_len = flen - (mic_ie + 2 + 16 /* AES_BLOCK_SIZE */ - fhead);
		ampe_buf = rtw_malloc(ampe_buf_len);
		if (!ampe_buf)
			goto exit;

		if (rtw_mpm_ampe_dec(adapter, plink, fhead, flen, fbody, mic_ie, ampe_buf) != _SUCCESS)
			goto exit;

		if (*(ampe_buf + 1) >= 68) {
			_rtw_memcpy(plink->sel_pcs, ampe_buf + 2, 4);
			_rtw_memcpy(plink->l_nonce, ampe_buf + 6, 32);
			_rtw_memcpy(plink->p_nonce, ampe_buf + 38, 32);
		}
	}
#endif

	/* count for new frame length  */
	new_len = sizeof(struct rtw_ieee80211_hdr_3addr) + tlv_ies_offset;
	left = BSS_EX_TLV_IES_LEN(network);
	pos = BSS_EX_TLV_IES(network);
	while (left >= 2) {
		u8 id, elen;
	
		id = *pos++;
		elen = *pos++;
		left -= 2;

		if (elen > left)
			break;

		switch (id) {
		case WLAN_EID_SSID:
		case WLAN_EID_DS_PARAMS:
		case WLAN_EID_TIM:
			break;
		default:
			new_len += 2 + elen;
		}

		left -= elen;
		pos += elen;
	}
	new_len += mpm_ielen + 2;
	if (mic_ie)
		new_len += 16 /* AES_BLOCK_SIZE*/ + 2 + ampe_buf_len;

	/* alloc new frame */
	new_buf = rtw_malloc(new_len);
	if (!new_buf) {
		rtw_warn_on(1);
		goto exit;
	}

	/* build new frame  */
	_rtw_memcpy(new_buf, fhead, sizeof(struct rtw_ieee80211_hdr_3addr) + tlv_ies_offset);
	new_fhead = new_buf;
	new_flen = new_len;
	new_fbody = new_fhead + sizeof(struct rtw_ieee80211_hdr_3addr);

	fpos = new_fbody + tlv_ies_offset;
	left = BSS_EX_TLV_IES_LEN(network);
	pos = BSS_EX_TLV_IES(network);
	while (left >= 2) {
		u8 id, elen;
	
		id = *pos++;
		elen = *pos++;
		left -= 2;

		if (elen > left)
			break;

		switch (id) {
		case WLAN_EID_SSID:
		case WLAN_EID_DS_PARAMS:
		case WLAN_EID_TIM:
			break;
		default:
			fpos = rtw_set_ie(fpos, id, elen, pos, NULL);
			if (id == WLAN_EID_MESH_CONFIG)
				fpos = rtw_set_ie(fpos, WLAN_EID_MPM, mpm_ielen, mpm_ie + 2, NULL);
		}

		left -= elen;
		pos += elen;
	}
	if (mic_ie) {
		new_mic_ie = fpos;
		*fpos++ = WLAN_EID_MIC;
		*fpos++ = 16 /* AES_BLOCK_SIZE */;
	}

#ifdef CONFIG_RTW_MESH_AEK
	/* encode */
	if (mic_ie) {
		int enc_ret = rtw_mpm_ampe_enc(adapter, plink, new_fbody, new_mic_ie, ampe_buf, 0);
		if (enc_ret != _SUCCESS)
			goto exit;
	}
#endif

	*nlen = new_len;
	*nbuf = new_buf;

	ret = _SUCCESS;

exit:
	if (ret != _SUCCESS && new_buf)
		rtw_mfree(new_buf, new_len);

#ifdef CONFIG_RTW_MESH_AEK
	if (ampe_buf)
		rtw_mfree(ampe_buf, ampe_buf_len);
#endif

	return ret;
}
#endif /* CONFIG_RTW_MPM_TX_IES_SYNC_BSS */

struct mpm_frame_info {
	u8 *aid;
	u16 aid_v;
	u8 *pid;
	u16 pid_v;
	u8 *llid;
	u16 llid_v;
	u8 *plid;
	u16 plid_v;
	u8 *reason;
	u16 reason_v;
	u8 *chosen_pmk;
};

/*
* pid:00000 llid:00000 chosen_pmk:0x00000000000000000000000000000000
* aid:00000 pid:00000 llid:00000 plid:00000 chosen_pmk:0x00000000000000000000000000000000
* pid:00000 llid:00000 plid:00000 reason:00000 chosen_pmk:0x00000000000000000000000000000000
*/
#define MPM_LOG_BUF_LEN 92 /* this length is limited for legal combination */
static void rtw_mpm_info_msg(struct mpm_frame_info *mpm_info, u8 *mpm_log_buf)
{
	int cnt = 0;

	if (mpm_info->aid) {
		cnt += snprintf(mpm_log_buf + cnt, MPM_LOG_BUF_LEN - cnt - 1, "aid:%u ", mpm_info->aid_v);
		if (cnt >= MPM_LOG_BUF_LEN - 1)
			goto exit;
	}
	if (mpm_info->pid) {
		cnt += snprintf(mpm_log_buf + cnt, MPM_LOG_BUF_LEN - cnt - 1, "pid:%u ", mpm_info->pid_v);
		if (cnt >= MPM_LOG_BUF_LEN - 1)
			goto exit;
	}
	if (mpm_info->llid) {
		cnt += snprintf(mpm_log_buf + cnt, MPM_LOG_BUF_LEN - cnt - 1, "llid:%u ", mpm_info->llid_v);
		if (cnt >= MPM_LOG_BUF_LEN - 1)
			goto exit;
	}
	if (mpm_info->plid) {
		cnt += snprintf(mpm_log_buf + cnt, MPM_LOG_BUF_LEN - cnt - 1, "plid:%u ", mpm_info->plid_v);
		if (cnt >= MPM_LOG_BUF_LEN - 1)
			goto exit;
	}
	if (mpm_info->reason) {
		cnt += snprintf(mpm_log_buf + cnt, MPM_LOG_BUF_LEN - cnt - 1, "reason:%u ", mpm_info->reason_v);
		if (cnt >= MPM_LOG_BUF_LEN - 1)
			goto exit;
	}
	if (mpm_info->chosen_pmk) {
		cnt += snprintf(mpm_log_buf + cnt, MPM_LOG_BUF_LEN - cnt - 1, "chosen_pmk:0x"KEY_FMT, KEY_ARG(mpm_info->chosen_pmk));
		if (cnt >= MPM_LOG_BUF_LEN - 1)
			goto exit;
	}

exit:
	return;
}

static int rtw_mpm_check_frames(_adapter *adapter, u8 action, const u8 **buf, size_t *len, u8 tx)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	struct mesh_plink_ent *plink = NULL;
	u8 *nbuf = NULL;
	size_t nlen = 0;
	u8 *fhead = (u8 *)*buf;
	size_t flen = *len;
	u8 *peer_addr = tx ? GetAddr1Ptr(fhead) : get_addr2_ptr(fhead);
	u8 *frame_body = fhead + sizeof(struct rtw_ieee80211_hdr_3addr);
	struct mpm_frame_info mpm_info;
	u8 tlv_ies_offset;
	u8 *mpm_ie = NULL;
	uint mpm_ielen = 0;
	u8 *mic_ie = NULL;
	uint mic_ielen = 0;
	int ret = 0;
	u8 mpm_log_buf[MPM_LOG_BUF_LEN] = {0};

	if (action == RTW_ACT_SELF_PROTECTED_MESH_OPEN)
		tlv_ies_offset = 4;
	else if (action == RTW_ACT_SELF_PROTECTED_MESH_CONF)
		tlv_ies_offset = 6;
	else if (action == RTW_ACT_SELF_PROTECTED_MESH_CLOSE)
		tlv_ies_offset = 2;
	else {
		rtw_warn_on(1);
		goto exit;
	}

	plink = rtw_mesh_plink_get(adapter, peer_addr);
	if (!plink && (tx == _TRUE || action == RTW_ACT_SELF_PROTECTED_MESH_CONF)) {
		/* warning message if no plink when: 1.TX all MPM or 2.RX CONF */
		RTW_WARN("RTW_%s:%s without plink of "MAC_FMT"\n"
			, (tx == _TRUE) ? "Tx" : "Rx", action_self_protected_str(action), MAC_ARG(peer_addr));
		goto exit;
	}

	_rtw_memset(&mpm_info, 0, sizeof(struct mpm_frame_info));

	if (action == RTW_ACT_SELF_PROTECTED_MESH_CONF) {
		mpm_info.aid = (u8 *)frame_body + 4;
		mpm_info.aid_v = RTW_GET_LE16(mpm_info.aid);
	}

	mpm_ie = rtw_get_ie(fhead + sizeof(struct rtw_ieee80211_hdr_3addr) + tlv_ies_offset
		, WLAN_EID_MPM, &mpm_ielen
		, flen - sizeof(struct rtw_ieee80211_hdr_3addr) - tlv_ies_offset);
	if (!mpm_ie || mpm_ielen < 2 + 2)
		goto exit;

	mpm_info.pid = mpm_ie + 2;
	mpm_info.pid_v = RTW_GET_LE16(mpm_info.pid);
	mpm_info.llid = mpm_info.pid + 2;
	mpm_info.llid_v = RTW_GET_LE16(mpm_info.llid);

	switch (action) {
	case RTW_ACT_SELF_PROTECTED_MESH_OPEN:
		/* pid:2, llid:2, (chosen_pmk:16) */
		if (mpm_info.pid_v == 0 && mpm_ielen == 4)
			;
		else if (mpm_info.pid_v == 1 && mpm_ielen == 20)
			mpm_info.chosen_pmk = mpm_info.llid + 2;
		else
			goto exit;
		break;
	case RTW_ACT_SELF_PROTECTED_MESH_CONF:
		/* pid:2, llid:2, plid:2, (chosen_pmk:16) */
		mpm_info.plid = mpm_info.llid + 2;
		mpm_info.plid_v = RTW_GET_LE16(mpm_info.plid);
		if (mpm_info.pid_v == 0 && mpm_ielen == 6)
			;
		else if (mpm_info.pid_v == 1 && mpm_ielen == 22)
			mpm_info.chosen_pmk = mpm_info.plid + 2;
		else
			goto exit;
		break;
	case RTW_ACT_SELF_PROTECTED_MESH_CLOSE:
		/* pid:2, llid:2, (plid:2), reason:2, (chosen_pmk:16) */
		if (mpm_info.pid_v == 0 && mpm_ielen == 6) {
			/* MPM, without plid */
			mpm_info.reason = mpm_info.llid + 2;
			mpm_info.reason_v = RTW_GET_LE16(mpm_info.reason);
		} else if (mpm_info.pid_v == 0 && mpm_ielen == 8) {
			/* MPM, with plid */
			mpm_info.plid = mpm_info.llid + 2;
			mpm_info.plid_v = RTW_GET_LE16(mpm_info.plid);
			mpm_info.reason = mpm_info.plid + 2;
			mpm_info.reason_v = RTW_GET_LE16(mpm_info.reason);
		} else if (mpm_info.pid_v == 1 && mpm_ielen == 22) {
			/* AMPE, without plid */
			mpm_info.reason = mpm_info.llid + 2;
			mpm_info.reason_v = RTW_GET_LE16(mpm_info.reason);
			mpm_info.chosen_pmk = mpm_info.reason + 2;
		} else if (mpm_info.pid_v == 1 && mpm_ielen == 24) {
			/* AMPE, with plid */
			mpm_info.plid = mpm_info.llid + 2;
			mpm_info.plid_v = RTW_GET_LE16(mpm_info.plid);
			mpm_info.reason = mpm_info.plid + 2;
			mpm_info.reason_v = RTW_GET_LE16(mpm_info.reason);
			mpm_info.chosen_pmk = mpm_info.reason + 2;
		} else
			goto exit;
		break;
	};

	if (mpm_info.pid_v == 1) {
		mic_ie = rtw_get_ie(fhead + sizeof(struct rtw_ieee80211_hdr_3addr) + tlv_ies_offset
			, WLAN_EID_MIC, &mic_ielen
			, flen - sizeof(struct rtw_ieee80211_hdr_3addr) - tlv_ies_offset);
		if (!mic_ie || mic_ielen != 16 /* AES_BLOCK_SIZE */)
			goto exit;
	}

#if CONFIG_RTW_MPM_TX_IES_SYNC_BSS
	if ((action == RTW_ACT_SELF_PROTECTED_MESH_OPEN || action == RTW_ACT_SELF_PROTECTED_MESH_CONF)
		&& tx == _TRUE
	) {
#define DBG_RTW_MPM_TX_IES_SYNC_BSS 0

		if (mpm_info.pid_v == 1 && (!plink || !MESH_PLINK_AEK_VALID(plink))) {
			RTW_WARN("AEK not ready, IEs can't sync with BSS\n");
			goto bypass_sync_bss;
		}

		if (DBG_RTW_MPM_TX_IES_SYNC_BSS) {
			RTW_INFO(FUNC_ADPT_FMT" before:\n", FUNC_ADPT_ARG(adapter));
			dump_ies(RTW_DBGDUMP
				, fhead + sizeof(struct rtw_ieee80211_hdr_3addr) + tlv_ies_offset
				, flen - sizeof(struct rtw_ieee80211_hdr_3addr) - tlv_ies_offset);
		}

		rtw_mpm_tx_ies_sync_bss(adapter, plink
			, fhead, flen, frame_body, tlv_ies_offset, mpm_ie, mic_ie
			, &nbuf, &nlen);
		if (!nbuf)
			goto exit;

		/* update pointer & len for new frame */
		fhead = nbuf;
		flen = nlen;
		frame_body = fhead + sizeof(struct rtw_ieee80211_hdr_3addr);
		if (mpm_info.pid_v == 1) {
			mic_ie = rtw_get_ie(fhead + sizeof(struct rtw_ieee80211_hdr_3addr) + tlv_ies_offset
				, WLAN_EID_MIC, &mic_ielen
				, flen - sizeof(struct rtw_ieee80211_hdr_3addr) - tlv_ies_offset);
		}

		if (DBG_RTW_MPM_TX_IES_SYNC_BSS) {
			RTW_INFO(FUNC_ADPT_FMT" after:\n", FUNC_ADPT_ARG(adapter));
			dump_ies(RTW_DBGDUMP
				, fhead + sizeof(struct rtw_ieee80211_hdr_3addr) + tlv_ies_offset
				, flen - sizeof(struct rtw_ieee80211_hdr_3addr) - tlv_ies_offset);
		}
	}
bypass_sync_bss:
#endif /* CONFIG_RTW_MPM_TX_IES_SYNC_BSS */

	if (!plink)
		goto mpm_log;

#if CONFIG_RTW_MESH_PEER_BLACKLIST
	if (action == RTW_ACT_SELF_PROTECTED_MESH_OPEN) {
		if (tx)
			rtw_mesh_plink_set_peer_conf_timeout(adapter, peer_addr);

	} else
#endif
#if CONFIG_RTW_MESH_ACNODE_PREVENT
	if (action == RTW_ACT_SELF_PROTECTED_MESH_CLOSE) {
		if (tx && mpm_info.reason && mpm_info.reason_v == WLAN_REASON_MESH_MAX_PEERS) {
			if (rtw_mesh_scanned_is_acnode_confirmed(adapter, plink->scanned)
				&& rtw_mesh_acnode_prevent_allow_sacrifice(adapter)
			) {
				struct sta_info *sac = rtw_mesh_acnode_prevent_pick_sacrifice(adapter);

				if (sac) {
					struct sta_priv *stapriv = &adapter->stapriv;
					_irqL irqL;
					u8 sta_addr[ETH_ALEN];
					u8 updated = _FALSE;

					_enter_critical_bh(&stapriv->asoc_list_lock, &irqL);
					if (!rtw_is_list_empty(&sac->asoc_list)) {
						rtw_list_delete(&sac->asoc_list);
						stapriv->asoc_list_cnt--;
						#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
						if (sac->tbtx_enable)
							stapriv->tbtx_asoc_list_cnt--;
						#endif			
						STA_SET_MESH_PLINK(sac, NULL);
					}
					_exit_critical_bh(&stapriv->asoc_list_lock, &irqL);
					RTW_INFO(FUNC_ADPT_FMT" sacrifice "MAC_FMT" for acnode\n"
						, FUNC_ADPT_ARG(adapter), MAC_ARG(sac->cmn.mac_addr));

					_rtw_memcpy(sta_addr, sac->cmn.mac_addr, ETH_ALEN);
					updated = ap_free_sta(adapter, sac, 0, 0, 1);
					rtw_mesh_expire_peer(stapriv->padapter, sta_addr);

					associated_clients_update(adapter, updated, STA_INFO_UPDATE_ALL);
				}
			}
		}
	} else
#endif
	if (action == RTW_ACT_SELF_PROTECTED_MESH_CONF) {
		_irqL irqL;
		u8 *ies = NULL;
		u16 ies_len = 0;

		_enter_critical_bh(&(plink_ctl->lock), &irqL);

		plink = _rtw_mesh_plink_get(adapter, peer_addr);
		if (!plink)
			goto release_plink_ctl;

		if (tx == _FALSE) {
			ies = plink->rx_conf_ies;
			ies_len = plink->rx_conf_ies_len;
			plink->rx_conf_ies = NULL;
			plink->rx_conf_ies_len = 0;

			plink->llid = mpm_info.plid_v;
			plink->plid = mpm_info.llid_v;
			plink->peer_aid = mpm_info.aid_v;
			if (mpm_info.pid_v == 1)
				_rtw_memcpy(plink->chosen_pmk, mpm_info.chosen_pmk, 16);
		}
		#ifdef CONFIG_RTW_MESH_DRIVER_AID
		else {
			ies = plink->tx_conf_ies;
			ies_len = plink->tx_conf_ies_len;
			plink->tx_conf_ies = NULL;
			plink->tx_conf_ies_len = 0;
		}
		#endif

		if (ies && ies_len)
			rtw_mfree(ies, ies_len);

		#ifndef CONFIG_RTW_MESH_DRIVER_AID
		if (tx == _TRUE)
			goto release_plink_ctl; /* no need to copy tx conf ies */
		#endif

		/* copy mesh confirm IEs */
		if (mpm_info.pid_v == 1) /* not include MIC & encrypted AMPE */
			ies_len = (mic_ie - fhead) - sizeof(struct rtw_ieee80211_hdr_3addr) - 2;
		else
			ies_len = flen - sizeof(struct rtw_ieee80211_hdr_3addr) - 2;

		ies = rtw_zmalloc(ies_len);
		if (ies) {
			_rtw_memcpy(ies, fhead + sizeof(struct rtw_ieee80211_hdr_3addr) + 2, ies_len);
			if (tx == _FALSE) {
				plink->rx_conf_ies = ies;
				plink->rx_conf_ies_len = ies_len;
			}
			#ifdef CONFIG_RTW_MESH_DRIVER_AID	
			else {
				plink->tx_conf_ies = ies;
				plink->tx_conf_ies_len = ies_len;
			}
			#endif
		}

release_plink_ctl:
		_exit_critical_bh(&(plink_ctl->lock), &irqL);
	}

mpm_log:
	rtw_mpm_info_msg(&mpm_info, mpm_log_buf);
	RTW_INFO("RTW_%s:%s %s\n"
		, (tx == _TRUE) ? "Tx" : "Rx"
		, action_self_protected_str(action)
		, mpm_log_buf
	);

	ret = 1;

exit:
	if (nbuf) {
		if (ret == 1) {
			*buf = nbuf;
			*len = nlen;
		} else
			rtw_mfree(nbuf, nlen);
	}

	return ret;
}

static int rtw_mesh_check_frames(_adapter *adapter, const u8 **buf, size_t *len, u8 tx)
{
	int is_mesh_frame = -1;
	const u8 *frame_body;
	u8 category, action;

	frame_body = *buf + sizeof(struct rtw_ieee80211_hdr_3addr);
	category = frame_body[0];

	if (category == RTW_WLAN_CATEGORY_SELF_PROTECTED) {
		action = frame_body[1];
		switch (action) {
		case RTW_ACT_SELF_PROTECTED_MESH_OPEN:
		case RTW_ACT_SELF_PROTECTED_MESH_CONF:
		case RTW_ACT_SELF_PROTECTED_MESH_CLOSE:
			rtw_mpm_check_frames(adapter, action, buf, len, tx);
			is_mesh_frame = action;
			break;
		case RTW_ACT_SELF_PROTECTED_MESH_GK_INFORM:
		case RTW_ACT_SELF_PROTECTED_MESH_GK_ACK:
			RTW_INFO("RTW_%s:%s\n", (tx == _TRUE) ? "Tx" : "Rx", action_self_protected_str(action));
			is_mesh_frame = action;
			break;
		default:
			break;
		};
	}

	return is_mesh_frame;
}

int rtw_mesh_check_frames_tx(_adapter *adapter, const u8 **buf, size_t *len)
{
	return rtw_mesh_check_frames(adapter, buf, len, _TRUE);
}

int rtw_mesh_check_frames_rx(_adapter *adapter, const u8 *buf, size_t len)
{
	return rtw_mesh_check_frames(adapter, &buf, &len, _FALSE);
}

int rtw_mesh_on_auth(_adapter *adapter, union recv_frame *rframe)
{
	u8 *whdr = rframe->u.hdr.rx_data;

#if CONFIG_RTW_MACADDR_ACL
	if (rtw_access_ctrl(adapter, get_addr2_ptr(whdr)) == _FALSE)
		return _SUCCESS;
#endif

	if (!rtw_mesh_plink_get(adapter, get_addr2_ptr(whdr))) {
		#if CONFIG_RTW_MESH_ACNODE_PREVENT
		rtw_mesh_acnode_set_notify_etime(adapter, whdr);
		#endif

		if (adapter_to_rfctl(adapter)->offch_state == OFFCHS_NONE)
			issue_probereq(adapter, &adapter->mlmepriv.cur_network.network.mesh_id, get_addr2_ptr(whdr));

		/* only peer being added (checked by notify conditions) is allowed */
		return _SUCCESS;
	}

	rtw_cfg80211_rx_mframe(adapter, rframe, NULL);
	return _SUCCESS;
}

unsigned int on_action_self_protected(_adapter *adapter, union recv_frame *rframe)
{
	unsigned int ret = _FAIL;
	struct sta_info *sta = NULL;
	u8 *pframe = rframe->u.hdr.rx_data;
	uint frame_len = rframe->u.hdr.len;
	u8 *frame_body = (u8 *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));
	u8 category;
	u8 action;

	/* check RA matches or not */
	if (!_rtw_memcmp(adapter_mac_addr(adapter), GetAddr1Ptr(pframe), ETH_ALEN))
		goto exit;

	category = frame_body[0];
	if (category != RTW_WLAN_CATEGORY_SELF_PROTECTED)
		goto exit;

	action = frame_body[1];
	switch (action) {
	case RTW_ACT_SELF_PROTECTED_MESH_OPEN:
	case RTW_ACT_SELF_PROTECTED_MESH_CONF:
	case RTW_ACT_SELF_PROTECTED_MESH_CLOSE:
	case RTW_ACT_SELF_PROTECTED_MESH_GK_INFORM:
	case RTW_ACT_SELF_PROTECTED_MESH_GK_ACK:
		if (!(MLME_IS_MESH(adapter) && MLME_IS_ASOC(adapter)))
			goto exit;
#ifdef CONFIG_IOCTL_CFG80211
		#if CONFIG_RTW_MACADDR_ACL
		if (rtw_access_ctrl(adapter, get_addr2_ptr(pframe)) == _FALSE)
			goto exit;
		#endif
		#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
		if (rtw_mesh_cto_mgate_required(adapter)
			/* only peer being added (checked by notify conditions) is allowed */
			&& !rtw_mesh_plink_get(adapter, get_addr2_ptr(pframe)))
			goto exit;
		#endif
		rtw_cfg80211_rx_action(adapter, rframe, NULL);
		ret = _SUCCESS;
#endif /* CONFIG_IOCTL_CFG80211 */
		break;
	default:
		break;
	}

exit:
	return ret;
}

const u8 ae_to_mesh_ctrl_len[] = {
	6,
	12, /* MESH_FLAGS_AE_A4 */
	18, /* MESH_FLAGS_AE_A5_A6 */
	0,
};

unsigned int on_action_mesh(_adapter *adapter, union recv_frame *rframe)
{
	unsigned int ret = _FAIL;
	struct sta_info *sta = NULL;
	struct sta_priv *stapriv = &adapter->stapriv;
	u8 *pframe = rframe->u.hdr.rx_data;
	uint frame_len = rframe->u.hdr.len;
	u8 *frame_body = (u8 *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));
	u8 category;
	u8 action;

	if (!MLME_IS_MESH(adapter))
		goto exit;

	/* check stainfo exist? */

	category = frame_body[0];
	if (category != RTW_WLAN_CATEGORY_MESH)
		goto exit;

	action = frame_body[1];
	switch (action) {
	case RTW_ACT_MESH_HWMP_PATH_SELECTION:
		rtw_mesh_rx_path_sel_frame(adapter, rframe);
		ret = _SUCCESS;
		break;
	default:
		break;
	}

exit:
	return ret;
}

bool rtw_mesh_update_bss_peering_status(_adapter *adapter, WLAN_BSSID_EX *bss)
{
	struct sta_priv *stapriv = &adapter->stapriv;
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	u8 num_of_peerings = stapriv->asoc_list_cnt;
	bool accept_peerings = stapriv->asoc_list_cnt < mcfg->max_peer_links;
	u8 *ie;
	int ie_len;
	bool updated = 0;

#if CONFIG_RTW_MESH_ACNODE_PREVENT
	accept_peerings |= plink_ctl->acnode_rsvd;
#endif

	ie = rtw_get_ie(BSS_EX_TLV_IES(bss), WLAN_EID_MESH_CONFIG, &ie_len, BSS_EX_TLV_IES_LEN(bss));
	if (!ie || ie_len != 7) {
		rtw_warn_on(1);
		goto exit;
	}

	if (GET_MESH_CONF_ELE_NUM_OF_PEERINGS(ie + 2) != num_of_peerings) {
		SET_MESH_CONF_ELE_NUM_OF_PEERINGS(ie + 2, num_of_peerings);
		updated = 1;
	}

	if (GET_MESH_CONF_ELE_ACCEPT_PEERINGS(ie + 2) != accept_peerings) {
		SET_MESH_CONF_ELE_ACCEPT_PEERINGS(ie + 2, accept_peerings);
		updated = 1;
	}

exit:
	return updated;
}

bool rtw_mesh_update_bss_formation_info(_adapter *adapter, WLAN_BSSID_EX *bss)
{
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	u8 cto_mgate = (minfo->num_gates || mcfg->dot11MeshGateAnnouncementProtocol);
	u8 cto_as = 0;
	u8 *ie;
	int ie_len;
	bool updated = 0;

	ie = rtw_get_ie(BSS_EX_TLV_IES(bss), WLAN_EID_MESH_CONFIG, &ie_len,
			BSS_EX_TLV_IES_LEN(bss));
	if (!ie || ie_len != 7) {
		rtw_warn_on(1);
		goto exit;
	}

	if (GET_MESH_CONF_ELE_CTO_MGATE(ie + 2) != cto_mgate) {
		SET_MESH_CONF_ELE_CTO_MGATE(ie + 2, cto_mgate);
		updated = 1;
	}

	if (GET_MESH_CONF_ELE_CTO_AS(ie + 2) != cto_as) {
		SET_MESH_CONF_ELE_CTO_AS(ie + 2, cto_as);
		updated = 1;
	}

exit:
	return updated;
}

bool rtw_mesh_update_bss_forwarding_state(_adapter *adapter, WLAN_BSSID_EX *bss)
{
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	u8 forward = mcfg->dot11MeshForwarding;
	u8 *ie;
	int ie_len;
	bool updated = 0;

	ie = rtw_get_ie(BSS_EX_TLV_IES(bss), WLAN_EID_MESH_CONFIG, &ie_len,
			BSS_EX_TLV_IES_LEN(bss));
	if (!ie || ie_len != 7) {
		rtw_warn_on(1);
		goto exit;
	}

	if (GET_MESH_CONF_ELE_FORWARDING(ie + 2) != forward) {
		SET_MESH_CONF_ELE_FORWARDING(ie + 2, forward);
		updated = 1;
	}

exit:
	return updated;
}

struct mesh_plink_ent *_rtw_mesh_plink_get(_adapter *adapter, const u8 *hwaddr)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	struct mesh_plink_ent *ent = NULL;
	int i;

	for (i = 0; i < RTW_MESH_MAX_PEER_CANDIDATES; i++) {
		if (plink_ctl->ent[i].valid == _TRUE
			&& _rtw_memcmp(plink_ctl->ent[i].addr, hwaddr, ETH_ALEN) == _TRUE
		) {
			ent = &plink_ctl->ent[i];
			break;
		}
	}

	return ent;
}

struct mesh_plink_ent *rtw_mesh_plink_get(_adapter *adapter, const u8 *hwaddr)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	struct mesh_plink_ent *ent = NULL;
	_irqL irqL;

	_enter_critical_bh(&(plink_ctl->lock), &irqL);
	ent = _rtw_mesh_plink_get(adapter, hwaddr);
	_exit_critical_bh(&(plink_ctl->lock), &irqL);

	return ent;
}

struct mesh_plink_ent *rtw_mesh_plink_get_no_estab_by_idx(_adapter *adapter, u8 idx)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	struct mesh_plink_ent *ent = NULL;
	int i, j = 0;
	_irqL irqL;

	_enter_critical_bh(&(plink_ctl->lock), &irqL);
	for (i = 0; i < RTW_MESH_MAX_PEER_CANDIDATES; i++) {
		if (plink_ctl->ent[i].valid == _TRUE
			&& plink_ctl->ent[i].plink_state != RTW_MESH_PLINK_ESTAB
		) {
			if (j == idx) {
				ent = &plink_ctl->ent[i];
				break;
			}
			j++;
		}
	}
	_exit_critical_bh(&(plink_ctl->lock), &irqL);

	return ent;
}

int _rtw_mesh_plink_add(_adapter *adapter, const u8 *hwaddr)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	struct mesh_plink_ent *ent = NULL;
	u8 exist = _FALSE;
	int i;

	for (i = 0; i < RTW_MESH_MAX_PEER_CANDIDATES; i++) {
		if (plink_ctl->ent[i].valid == _TRUE
			&& _rtw_memcmp(plink_ctl->ent[i].addr, hwaddr, ETH_ALEN) == _TRUE
		) {
			ent = &plink_ctl->ent[i];
			exist = _TRUE;
			break;
		}

		if (ent == NULL && plink_ctl->ent[i].valid == _FALSE)
			ent = &plink_ctl->ent[i];
	}

	if (exist == _FALSE && ent) {
		_rtw_memcpy(ent->addr, hwaddr, ETH_ALEN);
		ent->valid = _TRUE;
		#ifdef CONFIG_RTW_MESH_AEK
		ent->aek_valid = 0;
		#endif
		ent->llid = 0;
		ent->plid = 0;
		_rtw_memset(ent->chosen_pmk, 0, 16);
		#ifdef CONFIG_RTW_MESH_AEK
		_rtw_memset(ent->sel_pcs, 0, 4);
		_rtw_memset(ent->l_nonce, 0, 32);
		_rtw_memset(ent->p_nonce, 0, 32);
		#endif
		ent->plink_state = RTW_MESH_PLINK_LISTEN;
		#ifndef CONFIG_RTW_MESH_DRIVER_AID
		ent->aid = 0;
		#endif
		ent->peer_aid = 0;
		SET_PEER_CONF_DISABLED(ent);
		SET_CTO_MGATE_CONF_DISABLED(ent);
		plink_ctl->num++;
	}

	return exist == _TRUE ? RTW_ALREADY : (ent ? _SUCCESS : _FAIL);
}

int rtw_mesh_plink_add(_adapter *adapter, const u8 *hwaddr)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	_irqL irqL;
	int ret;

	_enter_critical_bh(&(plink_ctl->lock), &irqL);
	ret = _rtw_mesh_plink_add(adapter, hwaddr);
	_exit_critical_bh(&(plink_ctl->lock), &irqL);

	return ret;
}

int rtw_mesh_plink_set_state(_adapter *adapter, const u8 *hwaddr, u8 state)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	struct mesh_plink_ent *ent = NULL;
	_irqL irqL;

	_enter_critical_bh(&(plink_ctl->lock), &irqL);
	ent = _rtw_mesh_plink_get(adapter, hwaddr);
	if (ent)
		ent->plink_state = state;
	_exit_critical_bh(&(plink_ctl->lock), &irqL);

	return ent ? _SUCCESS : _FAIL;
}

#ifdef CONFIG_RTW_MESH_AEK
int rtw_mesh_plink_set_aek(_adapter *adapter, const u8 *hwaddr, const u8 *aek)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	struct mesh_plink_ent *ent = NULL;
	_irqL irqL;

	_enter_critical_bh(&(plink_ctl->lock), &irqL);
	ent = _rtw_mesh_plink_get(adapter, hwaddr);
	if (ent) {
		_rtw_memcpy(ent->aek, aek, 32);
		ent->aek_valid = 1;
	}
	_exit_critical_bh(&(plink_ctl->lock), &irqL);

	return ent ? _SUCCESS : _FAIL;
}
#endif

#if CONFIG_RTW_MESH_PEER_BLACKLIST
int rtw_mesh_plink_set_peer_conf_timeout(_adapter *adapter, const u8 *hwaddr)
{
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	struct mesh_plink_ent *ent = NULL;
	_irqL irqL;

	_enter_critical_bh(&(plink_ctl->lock), &irqL);
	ent = _rtw_mesh_plink_get(adapter, hwaddr);
	if (ent) {
		if (IS_PEER_CONF_DISABLED(ent))
			SET_PEER_CONF_END_TIME(ent, mcfg->peer_sel_policy.peer_conf_timeout_ms);
	}
	_exit_critical_bh(&(plink_ctl->lock), &irqL);

	return ent ? _SUCCESS : _FAIL;
}
#endif

void _rtw_mesh_plink_del_ent(_adapter *adapter, struct mesh_plink_ent *ent)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;

	ent->valid = _FALSE;
	#ifdef CONFIG_RTW_MESH_DRIVER_AID
	if (ent->tx_conf_ies && ent->tx_conf_ies_len)
		rtw_mfree(ent->tx_conf_ies, ent->tx_conf_ies_len);
	ent->tx_conf_ies = NULL;
	ent->tx_conf_ies_len = 0;
	#endif
	if (ent->rx_conf_ies && ent->rx_conf_ies_len)
		rtw_mfree(ent->rx_conf_ies, ent->rx_conf_ies_len);
	ent->rx_conf_ies = NULL;
	ent->rx_conf_ies_len = 0;
	if (ent->scanned)
		ent->scanned = NULL;
	plink_ctl->num--;
}

int rtw_mesh_plink_del(_adapter *adapter, const u8 *hwaddr)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	struct mesh_plink_ent *ent = NULL;
	u8 exist = _FALSE;
	int i;
	_irqL irqL;

	_enter_critical_bh(&(plink_ctl->lock), &irqL);
	for (i = 0; i < RTW_MESH_MAX_PEER_CANDIDATES; i++) {
		if (plink_ctl->ent[i].valid == _TRUE
			&& _rtw_memcmp(plink_ctl->ent[i].addr, hwaddr, ETH_ALEN) == _TRUE
		) {
			ent = &plink_ctl->ent[i];
			exist = _TRUE;
			break;
		}
	}

	if (exist == _TRUE)
		_rtw_mesh_plink_del_ent(adapter, ent);

	_exit_critical_bh(&(plink_ctl->lock), &irqL);

	return exist == _TRUE ? _SUCCESS : RTW_ALREADY;
}

void rtw_mesh_plink_ctl_init(_adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	int i;

	_rtw_spinlock_init(&plink_ctl->lock);
	plink_ctl->num = 0;
	for (i = 0; i < RTW_MESH_MAX_PEER_CANDIDATES; i++)
		plink_ctl->ent[i].valid = _FALSE;

#if CONFIG_RTW_MESH_PEER_BLACKLIST
	_rtw_init_queue(&plink_ctl->peer_blacklist);
#endif
#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
	_rtw_init_queue(&plink_ctl->cto_mgate_blacklist);
#endif
}

void rtw_mesh_plink_ctl_deinit(_adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	struct mesh_plink_ent *ent;
	int i;
	_irqL irqL;

	_enter_critical_bh(&(plink_ctl->lock), &irqL);
	for (i = 0; i < RTW_MESH_MAX_PEER_CANDIDATES; i++) {
		ent = &plink_ctl->ent[i];
		#ifdef CONFIG_RTW_MESH_DRIVER_AID
		if (ent->tx_conf_ies && ent->tx_conf_ies_len)
			rtw_mfree(ent->tx_conf_ies, ent->tx_conf_ies_len);
		#endif
		if (ent->rx_conf_ies && ent->rx_conf_ies_len)
			rtw_mfree(ent->rx_conf_ies, ent->rx_conf_ies_len);
	}
	_exit_critical_bh(&(plink_ctl->lock), &irqL);

	_rtw_spinlock_free(&plink_ctl->lock);

#if CONFIG_RTW_MESH_PEER_BLACKLIST
	rtw_mesh_peer_blacklist_flush(adapter);
	_rtw_deinit_queue(&plink_ctl->peer_blacklist);
#endif
#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
	rtw_mesh_cto_mgate_blacklist_flush(adapter);
	_rtw_deinit_queue(&plink_ctl->cto_mgate_blacklist);
#endif
}

void dump_mesh_plink_ctl(void *sel, _adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	struct mesh_plink_ent *ent;
	int i;

	RTW_PRINT_SEL(sel, "num:%u\n", plink_ctl->num);
	#if CONFIG_RTW_MESH_ACNODE_PREVENT
	RTW_PRINT_SEL(sel, "acnode_rsvd:%u\n", plink_ctl->acnode_rsvd);
	#endif

	for (i = 0; i < RTW_MESH_MAX_PEER_CANDIDATES; i++)  {
		ent = &plink_ctl->ent[i];
		if (!ent->valid)
			continue;

		RTW_PRINT_SEL(sel, "\n");
		RTW_PRINT_SEL(sel, "peer:"MAC_FMT"\n", MAC_ARG(ent->addr));
		RTW_PRINT_SEL(sel, "plink_state:%s\n", rtw_mesh_plink_str(ent->plink_state));

		#ifdef CONFIG_RTW_MESH_AEK
		if (ent->aek_valid)
			RTW_PRINT_SEL(sel, "aek:"KEY_FMT KEY_FMT"\n", KEY_ARG(ent->aek), KEY_ARG(ent->aek + 16));
		#endif

		RTW_PRINT_SEL(sel, "llid:%u, plid:%u\n", ent->llid, ent->plid);
		#ifndef CONFIG_RTW_MESH_DRIVER_AID
		RTW_PRINT_SEL(sel, "aid:%u\n", ent->aid);
		#endif
		RTW_PRINT_SEL(sel, "peer_aid:%u\n", ent->peer_aid);

		RTW_PRINT_SEL(sel, "chosen_pmk:"KEY_FMT"\n", KEY_ARG(ent->chosen_pmk));

		#ifdef CONFIG_RTW_MESH_AEK
		RTW_PRINT_SEL(sel, "sel_pcs:%02x%02x%02x%02x\n"
			, ent->sel_pcs[0], ent->sel_pcs[1], ent->sel_pcs[2], ent->sel_pcs[3]);
		RTW_PRINT_SEL(sel, "l_nonce:"KEY_FMT KEY_FMT"\n", KEY_ARG(ent->l_nonce), KEY_ARG(ent->l_nonce + 16));
		RTW_PRINT_SEL(sel, "p_nonce:"KEY_FMT KEY_FMT"\n", KEY_ARG(ent->p_nonce), KEY_ARG(ent->p_nonce + 16));
		#endif

		#ifdef CONFIG_RTW_MESH_DRIVER_AID
		RTW_PRINT_SEL(sel, "tx_conf_ies:%p, len:%u\n", ent->tx_conf_ies, ent->tx_conf_ies_len);
		#endif
		RTW_PRINT_SEL(sel, "rx_conf_ies:%p, len:%u\n", ent->rx_conf_ies, ent->rx_conf_ies_len);
		RTW_PRINT_SEL(sel, "scanned:%p\n", ent->scanned);

		#if CONFIG_RTW_MESH_PEER_BLACKLIST
		if (!IS_PEER_CONF_DISABLED(ent)) {
			if (!IS_PEER_CONF_TIMEOUT(ent))
				RTW_PRINT_SEL(sel, "peer_conf:%d\n", rtw_systime_to_ms(ent->peer_conf_end_time - rtw_get_current_time()));
			else
				RTW_PRINT_SEL(sel, "peer_conf:TIMEOUT\n");
		}
		#endif

		#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
		if (!IS_CTO_MGATE_CONF_DISABLED(ent)) {
			if (!IS_CTO_MGATE_CONF_TIMEOUT(ent))
				RTW_PRINT_SEL(sel, "cto_mgate_conf:%d\n", rtw_systime_to_ms(ent->cto_mgate_conf_end_time - rtw_get_current_time()));
			else
				RTW_PRINT_SEL(sel, "cto_mgate_conf:TIMEOUT\n");
		}
		#endif
	}
}

/* this function is called with plink_ctl being locked */
static int rtw_mesh_peer_establish(_adapter *adapter, struct mesh_plink_ent *plink, struct sta_info *sta)
{
#ifndef DBG_RTW_MESH_PEER_ESTABLISH
#define DBG_RTW_MESH_PEER_ESTABLISH 0
#endif

	struct sta_priv *stapriv = &adapter->stapriv;
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	u8 *tlv_ies;
	u16 tlv_ieslen;
	struct rtw_ieee802_11_elems elems;
	_irqL irqL;
	int i;
	u16 status = 0;
	int ret = _FAIL;
#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
	u8 sta_tbtx_enable = _FALSE;
#endif

	if (!plink->rx_conf_ies || !plink->rx_conf_ies_len) {
		RTW_INFO(FUNC_ADPT_FMT" no rx confirm from sta "MAC_FMT"\n"
			, FUNC_ADPT_ARG(adapter), MAC_ARG(sta->cmn.mac_addr));
		goto exit;
	}

	if (plink->rx_conf_ies_len < 4) {
		RTW_INFO(FUNC_ADPT_FMT" confirm from sta "MAC_FMT" too short\n"
			, FUNC_ADPT_ARG(adapter), MAC_ARG(sta->cmn.mac_addr));
		goto exit;
	}

#ifdef CONFIG_RTW_MESH_DRIVER_AID
	if (!plink->tx_conf_ies || !plink->tx_conf_ies_len) {
		RTW_INFO(FUNC_ADPT_FMT" no tx confirm to sta "MAC_FMT"\n"
			, FUNC_ADPT_ARG(adapter), MAC_ARG(sta->cmn.mac_addr));
		goto exit;
	}

	if (plink->tx_conf_ies_len < 4) {
		RTW_INFO(FUNC_ADPT_FMT" confirm to sta "MAC_FMT" too short\n"
			, FUNC_ADPT_ARG(adapter), MAC_ARG(sta->cmn.mac_addr));
		goto exit;
	}
#endif

	tlv_ies = plink->rx_conf_ies + 4;
	tlv_ieslen = plink->rx_conf_ies_len - 4;

	if (DBG_RTW_MESH_PEER_ESTABLISH)
		dump_ies(RTW_DBGDUMP, tlv_ies, tlv_ieslen);

	if (rtw_ieee802_11_parse_elems(tlv_ies, tlv_ieslen, &elems, 1) == ParseFailed) {
		RTW_INFO(FUNC_ADPT_FMT" sta "MAC_FMT" sent invalid confirm\n"
			, FUNC_ADPT_ARG(adapter), MAC_ARG(sta->cmn.mac_addr));
		goto exit;
	}

	SET_PEER_CONF_DISABLED(plink);
	if (rtw_bss_is_cto_mgate(&plink->scanned->network)
		&& !rtw_bss_is_forwarding(&plink->scanned->network))
		SET_CTO_MGATE_CONF_END_TIME(plink, mcfg->peer_sel_policy.cto_mgate_conf_timeout_ms);
	else
		SET_CTO_MGATE_CONF_DISABLED(plink);

	sta->state &= (~WIFI_FW_AUTH_SUCCESS);
	sta->state |= WIFI_FW_ASSOC_STATE;

	rtw_ap_parse_sta_capability(adapter, sta, plink->rx_conf_ies);

	status = rtw_ap_parse_sta_supported_rates(adapter, sta, tlv_ies, tlv_ieslen);
	if (status != _STATS_SUCCESSFUL_)
		goto exit;

	status = rtw_ap_parse_sta_security_ie(adapter, sta, &elems);
	if (status != _STATS_SUCCESSFUL_) {
		RTW_INFO(FUNC_ADPT_FMT" security check fail, status=%u\n", FUNC_ADPT_ARG(adapter), status);
		goto exit;
	}

	rtw_ap_parse_sta_wmm_ie(adapter, sta, tlv_ies, tlv_ieslen);
#ifdef CONFIG_RTS_FULL_BW
	/*check vendor IE*/
	rtw_parse_sta_vendor_ie_8812(adapter, sta, tlv_ies, tlv_ieslen);
#endif/*CONFIG_RTS_FULL_BW*/

#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
	if (elems.tbtx_cap && elems.tbtx_cap_len != 0) {
		if(rtw_is_tbtx_capabilty(elems.tbtx_cap, elems.tbtx_cap_len)) {
			sta_tbtx_enable = _TRUE;
		}
	}
#endif

	rtw_ap_parse_sta_ht_ie(adapter, sta, &elems);
	rtw_ap_parse_sta_vht_ie(adapter, sta, &elems);

	/* AID */
#ifdef CONFIG_RTW_MESH_DRIVER_AID
	sta->cmn.aid = RTW_GET_LE16(plink->tx_conf_ies + 2);
#else
	sta->cmn.aid = plink->aid;
#endif
	stapriv->sta_aid[sta->cmn.aid - 1] = sta;
	RTW_INFO(FUNC_ADPT_FMT" sta "MAC_FMT" aid:%u\n"
		, FUNC_ADPT_ARG(adapter), MAC_ARG(sta->cmn.mac_addr), sta->cmn.aid);

	sta->state &= (~WIFI_FW_ASSOC_STATE);
	sta->state |= WIFI_FW_ASSOC_SUCCESS;

	sta->local_mps = RTW_MESH_PS_ACTIVE;

	rtw_ewma_err_rate_init(&sta->metrics.err_rate);
	rtw_ewma_err_rate_add(&sta->metrics.err_rate, 1);
	/* init data_rate to 1M */
	sta->metrics.data_rate = 10;
	sta->alive = _TRUE;

	_enter_critical_bh(&stapriv->asoc_list_lock, &irqL);
	if (rtw_is_list_empty(&sta->asoc_list)) {
		STA_SET_MESH_PLINK(sta, plink);
		/* TBD: up layer timeout mechanism */
		/* sta->expire_to = mcfg->plink_timeout / 2; */
		rtw_list_insert_tail(&sta->asoc_list, &stapriv->asoc_list);
		stapriv->asoc_list_cnt++;
#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
		if (sta_tbtx_enable) {
			sta->tbtx_enable = _TRUE;
			stapriv->tbtx_asoc_list_cnt++;
		}
#endif
	}
	_exit_critical_bh(&stapriv->asoc_list_lock, &irqL);

	bss_cap_update_on_sta_join(adapter, sta);
	sta_info_update(adapter, sta);
	report_add_sta_event(adapter, sta->cmn.mac_addr);

	ret = _SUCCESS;

exit:
	return ret;
}

int rtw_mesh_set_plink_state(_adapter *adapter, const u8 *mac, u8 plink_state)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	struct mesh_plink_ent *plink = NULL;
	_irqL irqL2;
	struct sta_priv *stapriv = &adapter->stapriv;
	struct sta_info *sta = NULL;
	_irqL irqL;
	struct sta_info *del_sta = NULL;
	int ret = _SUCCESS;

	_enter_critical_bh(&(plink_ctl->lock), &irqL2);

	plink = _rtw_mesh_plink_get(adapter, mac);
	if (!plink) {
		ret = _FAIL;
		goto release_plink_ctl;
	}

	plink->plink_state = plink_state;

	#if CONFIG_RTW_MESH_ACNODE_PREVENT
	if (plink_state == RTW_MESH_PLINK_OPN_SNT) {
		if (rtw_mesh_scanned_is_acnode_confirmed(adapter, plink->scanned)
			&& rtw_mesh_acnode_prevent_allow_sacrifice(adapter)
		) {
			struct sta_info *sac = rtw_mesh_acnode_prevent_pick_sacrifice(adapter);

			if (sac) {
				del_sta = sac;
				_enter_critical_bh(&stapriv->asoc_list_lock, &irqL);
				if (!rtw_is_list_empty(&del_sta->asoc_list)) {
					rtw_list_delete(&del_sta->asoc_list);
					stapriv->asoc_list_cnt--;
					#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
					if (del_sta->tbtx_enable)
						stapriv->tbtx_asoc_list_cnt--;
					#endif
					STA_SET_MESH_PLINK(del_sta, NULL);
				}
				_exit_critical_bh(&stapriv->asoc_list_lock, &irqL);
				RTW_INFO(FUNC_ADPT_FMT" sacrifice "MAC_FMT" for acnode\n"
					, FUNC_ADPT_ARG(adapter), MAC_ARG(del_sta->cmn.mac_addr));
			}
		}
	} else
	#endif
	if (plink_state == RTW_MESH_PLINK_OPN_RCVD
		|| plink_state == RTW_MESH_PLINK_CNF_RCVD
		|| plink_state == RTW_MESH_PLINK_ESTAB
	) {
		sta = rtw_get_stainfo(stapriv, mac);
		if (!sta) {
			sta = rtw_alloc_stainfo(stapriv, mac);
			if (!sta)
				goto release_plink_ctl;
		}

		if (plink_state == RTW_MESH_PLINK_ESTAB) {
			if (rtw_mesh_peer_establish(adapter, plink, sta) != _SUCCESS) {
				del_sta = sta;
				ret = _FAIL;
				goto release_plink_ctl;
			}
		}
	}
	else if (plink_state == RTW_MESH_PLINK_HOLDING) {
		del_sta = rtw_get_stainfo(stapriv, mac);
		if (!del_sta)
			goto release_plink_ctl;

		_enter_critical_bh(&stapriv->asoc_list_lock, &irqL);
		if (!rtw_is_list_empty(&del_sta->asoc_list)) {
			rtw_list_delete(&del_sta->asoc_list);
			stapriv->asoc_list_cnt--;
			#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
			if (del_sta->tbtx_enable)
				stapriv->tbtx_asoc_list_cnt--;
			#endif
			STA_SET_MESH_PLINK(del_sta, NULL);
		}
		_exit_critical_bh(&stapriv->asoc_list_lock, &irqL);
	}

release_plink_ctl:
	_exit_critical_bh(&(plink_ctl->lock), &irqL2);

	if (del_sta) {
		u8 sta_addr[ETH_ALEN];
		u8 updated = _FALSE;

		_rtw_memcpy(sta_addr, del_sta->cmn.mac_addr, ETH_ALEN);
		updated = ap_free_sta(adapter, del_sta, 0, 0, 1);
		rtw_mesh_expire_peer(stapriv->padapter, sta_addr);

		associated_clients_update(adapter, updated, STA_INFO_UPDATE_ALL);
	}

	return ret;
}

struct mesh_set_plink_cmd_parm {
	const u8 *mac;
	u8 plink_state;
};

u8 rtw_mesh_set_plink_state_cmd_hdl(_adapter *adapter, u8 *parmbuf)
{
	struct mesh_set_plink_cmd_parm *parm = (struct mesh_set_plink_cmd_parm *)parmbuf;

	if (rtw_mesh_set_plink_state(adapter, parm->mac, parm->plink_state) == _SUCCESS)
		return	H2C_SUCCESS;

	return H2C_CMD_FAIL;
}

u8 rtw_mesh_set_plink_state_cmd(_adapter *adapter, const u8 *mac, u8 plink_state)
{
	struct cmd_obj *cmdobj;
	struct mesh_set_plink_cmd_parm *parm;
	struct cmd_priv *cmdpriv = &adapter->cmdpriv;
	struct submit_ctx sctx;
	u8 res = _SUCCESS;

	/* prepare cmd parameter */
	parm = rtw_zmalloc(sizeof(*parm));
	if (parm == NULL) {
		res = _FAIL;
		goto exit;
	}
	parm->mac = mac;
	parm->plink_state = plink_state;

	/* need enqueue, prepare cmd_obj and enqueue */
	cmdobj = rtw_zmalloc(sizeof(*cmdobj));
	if (cmdobj == NULL) {
		res = _FAIL;
		rtw_mfree(parm, sizeof(*parm));
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(cmdobj, parm, CMD_SET_MESH_PLINK_STATE);
	cmdobj->sctx = &sctx;
	rtw_sctx_init(&sctx, 2000);

	res = rtw_enqueue_cmd(cmdpriv, cmdobj);
	if (res == _SUCCESS) {
		rtw_sctx_wait(&sctx, __func__);
		_enter_critical_mutex(&cmdpriv->sctx_mutex, NULL);
		if (sctx.status == RTW_SCTX_SUBMITTED)
			cmdobj->sctx = NULL;
		_exit_critical_mutex(&cmdpriv->sctx_mutex, NULL);
		if (sctx.status != RTW_SCTX_DONE_SUCCESS)
			res = _FAIL;
	}

exit:
	return res;
}

void rtw_mesh_expire_peer_notify(_adapter *adapter, const u8 *peer_addr)
{
	u8 null_ssid[2] = {0, 0};

#ifdef CONFIG_IOCTL_CFG80211
	rtw_cfg80211_notify_new_peer_candidate(adapter->rtw_wdev
		, peer_addr
		, null_ssid
		, 2
		, 0
		, GFP_ATOMIC
	);
#endif

	return;
}

static u8 *rtw_mesh_construct_peer_mesh_close(_adapter *adapter, struct mesh_plink_ent *plink, u16 reason, u32 *len)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	u8 *frame = NULL, *pos;
	u32 flen;
	struct rtw_ieee80211_hdr *whdr;

	if (minfo->mesh_auth_id && !MESH_PLINK_AEK_VALID(plink))
		goto exit;

	flen = sizeof(struct rtw_ieee80211_hdr_3addr)
		+ 2 /* category, action */
		+ 2 + minfo->mesh_id_len /* mesh id */
		+ 2 + 8 + (minfo->mesh_auth_id ? 16 : 0) /* mpm */
		+ (minfo->mesh_auth_id ? 2 + 16 /* AES_BLOCK_SIZE */ : 0) /* mic */
		+ (minfo->mesh_auth_id ? 70 : 0) /* ampe */
		;

	pos = frame = rtw_zmalloc(flen);
	if (!frame)
		goto exit;

	whdr = (struct rtw_ieee80211_hdr *)frame;
	_rtw_memcpy(whdr->addr1, adapter_mac_addr(adapter), ETH_ALEN);
	_rtw_memcpy(whdr->addr2, plink->addr, ETH_ALEN);
	_rtw_memcpy(whdr->addr3, adapter_mac_addr(adapter), ETH_ALEN);

	set_frame_sub_type(frame, WIFI_ACTION);

	pos += sizeof(struct rtw_ieee80211_hdr_3addr);
	*(pos++) = RTW_WLAN_CATEGORY_SELF_PROTECTED;
	*(pos++) = RTW_ACT_SELF_PROTECTED_MESH_CLOSE;

	pos = rtw_set_ie_mesh_id(pos, NULL, minfo->mesh_id, minfo->mesh_id_len);

	pos = rtw_set_ie_mpm(pos, NULL
		, minfo->mesh_auth_id ? 1 : 0
		, plink->plid
		, &plink->llid
		, &reason
		, minfo->mesh_auth_id ? plink->chosen_pmk : NULL);

#ifdef CONFIG_RTW_MESH_AEK
	if (minfo->mesh_auth_id) {
		u8 ampe_buf[70];
		int enc_ret;

		*pos = WLAN_EID_MIC;
		*(pos + 1) = 16 /* AES_BLOCK_SIZE */;

		ampe_buf[0] = WLAN_EID_AMPE;
		ampe_buf[1] = 68;
		_rtw_memcpy(ampe_buf + 2, plink->sel_pcs, 4);
		_rtw_memcpy(ampe_buf + 6, plink->p_nonce, 32);
		_rtw_memcpy(ampe_buf + 38, plink->l_nonce, 32);

		enc_ret = rtw_mpm_ampe_enc(adapter, plink
			, frame + sizeof(struct rtw_ieee80211_hdr_3addr)
			, pos, ampe_buf, 1);
		if (enc_ret != _SUCCESS) {
			rtw_mfree(frame, flen);
			frame = NULL;
			goto exit;
		}
	}
#endif

	*len = flen;

exit:
	return frame;
}

void _rtw_mesh_expire_peer_ent(_adapter *adapter, struct mesh_plink_ent *plink)
{
#if defined(CONFIG_RTW_MESH_STA_DEL_DISASOC)
	_rtw_mesh_plink_del_ent(adapter, plink);
	rtw_cfg80211_indicate_sta_disassoc(adapter, plink->addr, 0);
#else
	u8 *frame = NULL;
	u32 flen;

	if (plink->plink_state == RTW_MESH_PLINK_ESTAB)
		frame = rtw_mesh_construct_peer_mesh_close(adapter, plink, WLAN_REASON_MESH_CLOSE, &flen);

	if (frame) {
		struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;
		struct wireless_dev *wdev = adapter->rtw_wdev;
		s32 freq = rtw_ch2freq(mlmeext->cur_channel);

		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
		rtw_cfg80211_rx_mgmt(wdev, freq, 0, frame, flen, GFP_ATOMIC);
		#else
		cfg80211_rx_action(adapter->pnetdev, freq, frame, flen, GFP_ATOMIC);
		#endif

		rtw_mfree(frame, flen);
	} else {
		rtw_mesh_expire_peer_notify(adapter, plink->addr);
		RTW_INFO(FUNC_ADPT_FMT" set "MAC_FMT" plink unknown\n"
			, FUNC_ADPT_ARG(adapter), MAC_ARG(plink->addr));
		plink->plink_state = RTW_MESH_PLINK_UNKNOWN;
	}
#endif
}

void rtw_mesh_expire_peer(_adapter *adapter, const u8 *peer_addr)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct mesh_plink_pool *plink_ctl = &minfo->plink_ctl;
	struct mesh_plink_ent *plink;
	_irqL irqL;

	_enter_critical_bh(&(plink_ctl->lock), &irqL);

	plink = _rtw_mesh_plink_get(adapter, peer_addr);
	if (!plink)
		goto exit;

	_rtw_mesh_expire_peer_ent(adapter, plink);

exit:
	_exit_critical_bh(&(plink_ctl->lock), &irqL);
}

u8 rtw_mesh_ps_annc(_adapter *adapter, u8 ps)
{
	_irqL irqL;
	_list *head, *list;
	struct sta_info *sta;
	struct sta_priv *stapriv = &adapter->stapriv;
	u8 sta_alive_num = 0, i;
	char sta_alive_list[NUM_STA];
	u8 annc_cnt = 0;

	if (rtw_linked_check(adapter) == _FALSE)
		goto exit;

	_enter_critical_bh(&stapriv->asoc_list_lock, &irqL);

	head = &stapriv->asoc_list;
	list = get_next(head);
	while ((rtw_end_of_queue_search(head, list)) == _FALSE) {
		int stainfo_offset;

		sta = LIST_CONTAINOR(list, struct sta_info, asoc_list);
		list = get_next(list);

		stainfo_offset = rtw_stainfo_offset(stapriv, sta);
		if (stainfo_offset_valid(stainfo_offset))
			sta_alive_list[sta_alive_num++] = stainfo_offset;
	}
	_exit_critical_bh(&stapriv->asoc_list_lock, &irqL);

	for (i = 0; i < sta_alive_num; i++) {
		sta = rtw_get_stainfo_by_offset(stapriv, sta_alive_list[i]);
		if (!sta)
			continue;

		issue_qos_nulldata(adapter, sta->cmn.mac_addr, 7, ps, 3, 500);
		annc_cnt++;
	}

exit:
	return annc_cnt;
}

static void mpath_tx_tasklet_hdl(void *priv)
{
	_adapter *adapter = (_adapter *)priv;
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct xmit_frame *xframe;
	_list *list, *head;
	_list tmp;
	u32 tmp_len;
	s32 res;

	_rtw_init_listhead(&tmp);

	while (1) {
		tmp_len = 0;
		enter_critical_bh(&minfo->mpath_tx_queue.lock);
		if (minfo->mpath_tx_queue_len) {
			rtw_list_splice_init(&minfo->mpath_tx_queue.queue, &tmp);
			tmp_len = minfo->mpath_tx_queue_len;
			minfo->mpath_tx_queue_len = 0;
		}
		exit_critical_bh(&minfo->mpath_tx_queue.lock);

		if (!tmp_len)
			break;

		head = &tmp;
		list = get_next(head);
		while (rtw_end_of_queue_search(head, list) == _FALSE) {
			xframe = LIST_CONTAINOR(list, struct xmit_frame, list);
			list = get_next(list);
			rtw_list_delete(&xframe->list);
			res = rtw_xmit_posthandle(adapter, xframe, xframe->pkt);
			if (res < 0) {
				#ifdef DBG_TX_DROP_FRAME
				RTW_INFO("DBG_TX_DROP_FRAME %s rtw_xmit fail\n", __FUNCTION__);
				#endif
				adapter->xmitpriv.tx_drop++;
			}
		}
	}
}

static void rtw_mpath_tx_queue_flush(_adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct xmit_frame *xframe;
	_list *list, *head;
	_list tmp;

	_rtw_init_listhead(&tmp);

	enter_critical_bh(&minfo->mpath_tx_queue.lock);
	rtw_list_splice_init(&minfo->mpath_tx_queue.queue, &tmp);
	minfo->mpath_tx_queue_len = 0;
	exit_critical_bh(&minfo->mpath_tx_queue.lock);

	head = &tmp;
	list = get_next(head);
	while (rtw_end_of_queue_search(head, list) == _FALSE) {
		xframe = LIST_CONTAINOR(list, struct xmit_frame, list);
		list = get_next(list);
		rtw_list_delete(&xframe->list);
		rtw_free_xmitframe(&adapter->xmitpriv, xframe);
	}
}

#ifdef PLATFORM_LINUX /* 3.10 ~ 4.13 checked */
#if defined(CONFIG_SLUB)
#include <linux/slub_def.h>
#elif defined(CONFIG_SLAB)
#include <linux/slab_def.h>
#endif
typedef struct kmem_cache rtw_mcache;
#endif

rtw_mcache *rtw_mcache_create(const char *name, size_t size)
{
#ifdef PLATFORM_LINUX /* 3.10 ~ 4.13 checked */
	return kmem_cache_create(name, size, 0, 0, NULL);
#else
	#error "TBD\n";
#endif
}

void rtw_mcache_destroy(rtw_mcache *s)
{
#ifdef PLATFORM_LINUX /* 3.10 ~ 4.13 checked */
	kmem_cache_destroy(s);
#else
	#error "TBD\n";
#endif
}

void *_rtw_mcache_alloc(rtw_mcache *cachep)
{
#ifdef PLATFORM_LINUX /* 3.10 ~ 4.13 checked */
	return kmem_cache_alloc(cachep, GFP_ATOMIC);
#else
	#error "TBD\n";
#endif
}

void _rtw_mcache_free(rtw_mcache *cachep, void *objp)
{
#ifdef PLATFORM_LINUX /* 3.10 ~ 4.13 checked */
	kmem_cache_free(cachep, objp);
#else
	#error "TBD\n";
#endif
}

#ifdef DBG_MEM_ALLOC
inline void *dbg_rtw_mcache_alloc(rtw_mcache *cachep, const enum mstat_f flags, const char *func, const int line)
{
	void *p;
	u32 sz = cachep->size;

	if (match_mstat_sniff_rules(flags, sz))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s(%u)\n", func, line, __func__, sz);

	p = _rtw_mcache_alloc(cachep);

	rtw_mstat_update(
		flags
		, p ? MSTAT_ALLOC_SUCCESS : MSTAT_ALLOC_FAIL
		, sz
	);

	return p;
}

inline void dbg_rtw_mcache_free(rtw_mcache *cachep, void *pbuf, const enum mstat_f flags, const char *func, const int line)
{
	u32 sz = cachep->size;

	if (match_mstat_sniff_rules(flags, sz))
		RTW_INFO("DBG_MEM_ALLOC %s:%d %s(%u)\n", func, line, __func__, sz);

	_rtw_mcache_free(cachep, pbuf);

	rtw_mstat_update(
		flags
		, MSTAT_FREE
		, sz
	);
}

#define rtw_mcache_alloc(cachep) dbg_rtw_mcache_alloc(cachep, MSTAT_TYPE_PHY, __FUNCTION__, __LINE__)
#define rtw_mcache_free(cachep, objp) dbg_rtw_mcache_free(cachep, objp, MSTAT_TYPE_PHY, __FUNCTION__, __LINE__)
#else
#define rtw_mcache_alloc(cachep) _rtw_mcache_alloc(cachep)
#define rtw_mcache_free(cachep, objp) _rtw_mcache_free(cachep, objp)
#endif /* DBG_MEM_ALLOC */

/* Mesh Received Cache */
#define RTW_MRC_BUCKETS			256 /* must be a power of 2 */
#define RTW_MRC_QUEUE_MAX_LEN	4
#define RTW_MRC_TIMEOUT_MS		(3 * 1000)

/**
 * struct rtw_mrc_entry - entry in the Mesh Received Cache
 *
 * @seqnum: mesh sequence number of the frame
 * @exp_time: expiration time of the entry
 * @msa: mesh source address of the frame
 * @list: hashtable list pointer
 *
 * The Mesh Received Cache keeps track of the latest received frames that
 * have been received by a mesh interface and discards received frames
 * that are found in the cache.
 */
struct rtw_mrc_entry {
	rtw_hlist_node list;
	systime exp_time;
	u32 seqnum;
	u8 msa[ETH_ALEN];
};

struct rtw_mrc {
	rtw_hlist_head bucket[RTW_MRC_BUCKETS];
	u32 idx_mask;
	rtw_mcache *cache;
};

static int rtw_mrc_init(_adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	char cache_name[IFNAMSIZ + 8 + 1];
	int i;

	minfo->mrc = rtw_malloc(sizeof(struct rtw_mrc));
	if (!minfo->mrc)
		return -ENOMEM;
	minfo->mrc->idx_mask = RTW_MRC_BUCKETS - 1;
	for (i = 0; i < RTW_MRC_BUCKETS; i++)
		rtw_hlist_head_init(&minfo->mrc->bucket[i]);

	sprintf(cache_name, "rtw_mrc_%s", ADPT_ARG(adapter));
	minfo->mrc->cache = rtw_mcache_create(cache_name, sizeof(struct rtw_mrc_entry));

	return 0;
}

static void rtw_mrc_free(_adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct rtw_mrc *mrc = minfo->mrc;
	struct rtw_mrc_entry *p;
	rtw_hlist_node *np, *n;
	int i;

	if (!mrc)
		return;

	for (i = 0; i < RTW_MRC_BUCKETS; i++) {
		rtw_hlist_for_each_entry_safe(p, np, n, &mrc->bucket[i], list) {
			rtw_hlist_del(&p->list);
			rtw_mcache_free(mrc->cache, p);
		}
	}

	rtw_mcache_destroy(mrc->cache);

	rtw_mfree(mrc, sizeof(struct rtw_mrc));
	minfo->mrc = NULL;
}

/**
 * rtw_mrc_check - Check frame in mesh received cache and add if absent.
 *
 * @adapter:	interface
 * @msa:		mesh source address
 * @seq:		mesh seq number
 *
 * Returns: 0 if the frame is not in the cache, nonzero otherwise.
 *
 * Checks using the mesh source address and the mesh sequence number if we have
 * received this frame lately. If the frame is not in the cache, it is added to
 * it.
 */
static int rtw_mrc_check(_adapter *adapter, const u8 *msa, u32 seq)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct rtw_mrc *mrc = minfo->mrc;
	int entries = 0;
	u8 idx;
	struct rtw_mrc_entry *p;
	rtw_hlist_node *np, *n;
	u8 timeout;

	if (!mrc)
		return -1;

	idx = seq & mrc->idx_mask;
	rtw_hlist_for_each_entry_safe(p, np, n, &mrc->bucket[idx], list) {
		++entries;
		timeout = rtw_time_after(rtw_get_current_time(), p->exp_time);
		if (timeout || entries == RTW_MRC_QUEUE_MAX_LEN) {
			if (!timeout)
				minfo->mshstats.mrc_del_qlen++;

			rtw_hlist_del(&p->list);
			rtw_mcache_free(mrc->cache, p);
			--entries;
		} else if ((seq == p->seqnum) && _rtw_memcmp(msa, p->msa, ETH_ALEN) == _TRUE)
			return -1;
	}

	p = rtw_mcache_alloc(mrc->cache);
	if (!p)
		return 0;

	p->seqnum = seq;
	p->exp_time = rtw_get_current_time() + rtw_ms_to_systime(RTW_MRC_TIMEOUT_MS);
	_rtw_memcpy(p->msa, msa, ETH_ALEN);
	rtw_hlist_add_head(&p->list, &mrc->bucket[idx]);
	return 0;
}

static int rtw_mesh_decache(_adapter *adapter, const u8 *msa, u32 seq)
{
	return rtw_mrc_check(adapter, msa, seq);
}

#ifndef RTW_MESH_SCAN_RESULT_EXP_MS
#define RTW_MESH_SCAN_RESULT_EXP_MS (10 * 1000)
#endif

#ifndef RTW_MESH_ACNODE_PREVENT
#define RTW_MESH_ACNODE_PREVENT 0
#endif
#ifndef RTW_MESH_ACNODE_CONF_TIMEOUT_MS
#define RTW_MESH_ACNODE_CONF_TIMEOUT_MS (20 * 1000)
#endif
#ifndef RTW_MESH_ACNODE_NOTIFY_TIMEOUT_MS
#define RTW_MESH_ACNODE_NOTIFY_TIMEOUT_MS (2 * 1000)
#endif

#ifndef RTW_MESH_OFFCH_CAND
#define RTW_MESH_OFFCH_CAND 1
#endif
#ifndef RTW_MESH_OFFCH_CAND_FIND_INT_MS
#define RTW_MESH_OFFCH_CAND_FIND_INT_MS (10 * 1000)
#endif

#ifndef RTW_MESH_PEER_CONF_TIMEOUT_MS
#define RTW_MESH_PEER_CONF_TIMEOUT_MS (20 * 1000)
#endif
#ifndef RTW_MESH_PEER_BLACKLIST_TIMEOUT_MS
#define RTW_MESH_PEER_BLACKLIST_TIMEOUT_MS (20 * 1000)
#endif

#ifndef RTW_MESH_CTO_MGATE_REQUIRE
#define RTW_MESH_CTO_MGATE_REQUIRE 0
#endif
#ifndef RTW_MESH_CTO_MGATE_CONF_TIMEOUT_MS
#define RTW_MESH_CTO_MGATE_CONF_TIMEOUT_MS (20 * 1000)
#endif
#ifndef RTW_MESH_CTO_MGATE_BLACKLIST_TIMEOUT_MS
#define RTW_MESH_CTO_MGATE_BLACKLIST_TIMEOUT_MS (20 * 1000)
#endif

void rtw_mesh_cfg_init_peer_sel_policy(struct rtw_mesh_cfg *mcfg)
{
	struct mesh_peer_sel_policy *sel_policy = &mcfg->peer_sel_policy;

	sel_policy->scanr_exp_ms = RTW_MESH_SCAN_RESULT_EXP_MS;

#if CONFIG_RTW_MESH_ACNODE_PREVENT
	sel_policy->acnode_prevent = RTW_MESH_ACNODE_PREVENT;
	sel_policy->acnode_conf_timeout_ms = RTW_MESH_ACNODE_CONF_TIMEOUT_MS;
	sel_policy->acnode_notify_timeout_ms = RTW_MESH_ACNODE_NOTIFY_TIMEOUT_MS;
#endif

#if CONFIG_RTW_MESH_OFFCH_CAND
	sel_policy->offch_cand = RTW_MESH_OFFCH_CAND;
	sel_policy->offch_find_int_ms = RTW_MESH_OFFCH_CAND_FIND_INT_MS;
#endif

#if CONFIG_RTW_MESH_PEER_BLACKLIST
	sel_policy->peer_conf_timeout_ms = RTW_MESH_PEER_CONF_TIMEOUT_MS;
	sel_policy->peer_blacklist_timeout_ms = RTW_MESH_PEER_BLACKLIST_TIMEOUT_MS;
#endif

#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
	sel_policy->cto_mgate_require = RTW_MESH_CTO_MGATE_REQUIRE;
	sel_policy->cto_mgate_conf_timeout_ms = RTW_MESH_CTO_MGATE_CONF_TIMEOUT_MS;
	sel_policy->cto_mgate_blacklist_timeout_ms = RTW_MESH_CTO_MGATE_BLACKLIST_TIMEOUT_MS;
#endif
}

void rtw_mesh_cfg_init(_adapter *adapter)
{
	struct registry_priv *regsty = adapter_to_regsty(adapter);
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;

	mcfg->max_peer_links = RTW_MESH_MAX_PEER_LINKS;
	mcfg->plink_timeout = RTW_MESH_PEER_LINK_TIMEOUT;

	mcfg->dot11MeshTTL = RTW_MESH_TTL;
	mcfg->element_ttl = RTW_MESH_DEFAULT_ELEMENT_TTL;
	mcfg->dot11MeshHWMPmaxPREQretries = RTW_MESH_MAX_PREQ_RETRIES;
	mcfg->path_refresh_time = RTW_MESH_PATH_REFRESH_TIME;
	mcfg->min_discovery_timeout = RTW_MESH_MIN_DISCOVERY_TIMEOUT;
	mcfg->dot11MeshHWMPactivePathTimeout = RTW_MESH_PATH_TIMEOUT;
	mcfg->dot11MeshHWMPpreqMinInterval = RTW_MESH_PREQ_MIN_INT;
	mcfg->dot11MeshHWMPperrMinInterval = RTW_MESH_PERR_MIN_INT;
	mcfg->dot11MeshHWMPnetDiameterTraversalTime = RTW_MESH_DIAM_TRAVERSAL_TIME;
	mcfg->dot11MeshHWMPRootMode = RTW_IEEE80211_ROOTMODE_NO_ROOT;
	mcfg->dot11MeshHWMPRannInterval = RTW_MESH_RANN_INTERVAL;
	mcfg->dot11MeshGateAnnouncementProtocol = _FALSE;
	mcfg->dot11MeshForwarding = _TRUE;
	mcfg->rssi_threshold = 0;
	mcfg->dot11MeshHWMPactivePathToRootTimeout = RTW_MESH_PATH_TO_ROOT_TIMEOUT;
	mcfg->dot11MeshHWMProotInterval = RTW_MESH_ROOT_INTERVAL;
	mcfg->dot11MeshHWMPconfirmationInterval = RTW_MESH_ROOT_CONFIRMATION_INTERVAL;
	mcfg->path_gate_timeout_factor = 3;
	rtw_mesh_cfg_init_peer_sel_policy(mcfg);
#ifdef CONFIG_RTW_MESH_ADD_ROOT_CHK
	mcfg->sane_metric_delta = RTW_MESH_SANE_METRIC_DELTA;
	mcfg->max_root_add_chk_cnt = RTW_MESH_MAX_ROOT_ADD_CHK_CNT;
#endif

#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
	mcfg->b2u_flags_msrc = regsty->msrc_b2u_flags;
	mcfg->b2u_flags_mfwd = regsty->mfwd_b2u_flags;
#endif
}

void rtw_mesh_cfg_init_max_peer_links(_adapter *adapter, u8 stack_conf)
{
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;

	mcfg->max_peer_links = RTW_MESH_MAX_PEER_LINKS;

	if (mcfg->max_peer_links > stack_conf)
		mcfg->max_peer_links = stack_conf;
}

void rtw_mesh_cfg_init_plink_timeout(_adapter *adapter, u32 stack_conf)
{
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;

	mcfg->plink_timeout = stack_conf;
}

void rtw_mesh_init_mesh_info(_adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;

	_rtw_memset(minfo, 0, sizeof(struct rtw_mesh_info));

	rtw_mesh_plink_ctl_init(adapter);
	
	minfo->last_preq = rtw_get_current_time();
	/* minfo->last_sn_update = rtw_get_current_time(); */
	minfo->next_perr = rtw_get_current_time();
	
	ATOMIC_SET(&minfo->mpaths, 0);
	rtw_mesh_pathtbl_init(adapter);

	_rtw_init_queue(&minfo->mpath_tx_queue);
	tasklet_init(&minfo->mpath_tx_tasklet
		, (void(*)(unsigned long))mpath_tx_tasklet_hdl
		, (unsigned long)adapter);

	rtw_mrc_init(adapter);

	_rtw_init_listhead(&minfo->preq_queue.list);
	_rtw_spinlock_init(&minfo->mesh_preq_queue_lock);
	
	rtw_init_timer(&adapter->mesh_path_timer, adapter, rtw_ieee80211_mesh_path_timer, adapter);
	rtw_init_timer(&adapter->mesh_path_root_timer, adapter, rtw_ieee80211_mesh_path_root_timer, adapter);
	rtw_init_timer(&adapter->mesh_atlm_param_req_timer, adapter, rtw_mesh_atlm_param_req_timer, adapter);
	_init_workitem(&adapter->mesh_work, rtw_mesh_work_hdl, NULL);
}

void rtw_mesh_deinit_mesh_info(_adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;

	tasklet_kill(&minfo->mpath_tx_tasklet);
	rtw_mpath_tx_queue_flush(adapter);
	_rtw_deinit_queue(&adapter->mesh_info.mpath_tx_queue);

	rtw_mrc_free(adapter);

	rtw_mesh_pathtbl_unregister(adapter);

	rtw_mesh_plink_ctl_deinit(adapter);

	_cancel_workitem_sync(&adapter->mesh_work);
	_cancel_timer_ex(&adapter->mesh_path_timer);
	_cancel_timer_ex(&adapter->mesh_path_root_timer);
	_cancel_timer_ex(&adapter->mesh_atlm_param_req_timer);
}

/**
 * rtw_mesh_nexthop_resolve - lookup next hop; conditionally start path discovery
 *
 * @skb: 802.11 frame to be sent
 * @sdata: network subif the frame will be sent through
 *
 * Lookup next hop for given skb and start path discovery if no
 * forwarding information is found.
 *
 * Returns: 0 if the next hop was found and -ENOENT if the frame was queued.
 * skb is freeed here if no mpath could be allocated.
 */
int rtw_mesh_nexthop_resolve(_adapter *adapter,
			struct xmit_frame *xframe)
{
	struct pkt_attrib *attrib = &xframe->attrib;
	struct rtw_mesh_path *mpath;
	struct xmit_frame *xframe_to_free = NULL;
	u8 *target_addr = attrib->mda;
	int err = 0;
	int ret = _SUCCESS;

	rtw_rcu_read_lock();
	err = rtw_mesh_nexthop_lookup(adapter, target_addr, attrib->msa, attrib->ra);
	if (!err)
		goto endlookup;

	/* no nexthop found, start resolving */
	mpath = rtw_mesh_path_lookup(adapter, target_addr);
	if (!mpath) {
		mpath = rtw_mesh_path_add(adapter, target_addr);
		if (IS_ERR(mpath)) {
			xframe->pkt = NULL; /* free pkt outside */
			rtw_mesh_path_discard_frame(adapter, xframe);
			err = PTR_ERR(mpath);
			ret = _FAIL;
			goto endlookup;
		}
	}

	if (!(mpath->flags & RTW_MESH_PATH_RESOLVING))
		rtw_mesh_queue_preq(mpath, RTW_PREQ_Q_F_START);

	enter_critical_bh(&mpath->frame_queue.lock);

	if (mpath->frame_queue_len >= RTW_MESH_FRAME_QUEUE_LEN) {
		xframe_to_free = LIST_CONTAINOR(get_next(get_list_head(&mpath->frame_queue)), struct xmit_frame, list);
		rtw_list_delete(&(xframe_to_free->list));
		mpath->frame_queue_len--;
	}

	rtw_list_insert_tail(&xframe->list, get_list_head(&mpath->frame_queue));
	mpath->frame_queue_len++;

	exit_critical_bh(&mpath->frame_queue.lock);

	ret = RTW_RA_RESOLVING;
	if (xframe_to_free)
		rtw_mesh_path_discard_frame(adapter, xframe_to_free);

endlookup:
	rtw_rcu_read_unlock();
	return ret;
}

/**
 * rtw_mesh_nexthop_lookup - put the appropriate next hop on a mesh frame. Calling
 * this function is considered "using" the associated mpath, so preempt a path
 * refresh if this mpath expires soon.
 *
 * @skb: 802.11 frame to be sent
 * @sdata: network subif the frame will be sent through
 *
 * Returns: 0 if the next hop was found. Nonzero otherwise.
 */
int rtw_mesh_nexthop_lookup(_adapter *adapter,
	const u8 *mda, const u8 *msa, u8 *ra)
{
	struct rtw_mesh_path *mpath;
	struct sta_info *next_hop;
	const u8 *target_addr = mda;
	int err = -ENOENT;
	struct registry_priv  *registry_par = &adapter->registrypriv;
	u8 peer_alive_based_preq = registry_par->peer_alive_based_preq;
	BOOLEAN nexthop_alive = _TRUE;

	rtw_rcu_read_lock();
	mpath = rtw_mesh_path_lookup(adapter, target_addr);

	if (!mpath || !(mpath->flags & RTW_MESH_PATH_ACTIVE))
		goto endlookup;

	next_hop = rtw_rcu_dereference(mpath->next_hop);
	if (next_hop) {
		_rtw_memcpy(ra, next_hop->cmn.mac_addr, ETH_ALEN);
		err = 0;
	}

	if (peer_alive_based_preq && next_hop)
		nexthop_alive = next_hop->alive;

	if (_rtw_memcmp(adapter_mac_addr(adapter), msa, ETH_ALEN) == _TRUE &&
	    !(mpath->flags & RTW_MESH_PATH_RESOLVING) &&
	    !(mpath->flags & RTW_MESH_PATH_FIXED)) {
		u8 flags = RTW_PREQ_Q_F_START | RTW_PREQ_Q_F_REFRESH;

		if (peer_alive_based_preq && nexthop_alive == _FALSE) {
			flags |= RTW_PREQ_Q_F_BCAST_PREQ;
			rtw_mesh_queue_preq(mpath, flags);
		} else if (rtw_time_after(rtw_get_current_time(),
			mpath->exp_time -
			rtw_ms_to_systime(adapter->mesh_cfg.path_refresh_time))) {
			rtw_mesh_queue_preq(mpath, flags);
		}
	/* Avoid keeping trying unicast PREQ toward root,
	   when next_hop leaves */
	} else if (peer_alive_based_preq &&
		   _rtw_memcmp(adapter_mac_addr(adapter), msa, ETH_ALEN) == _TRUE &&
		   (mpath->flags & RTW_MESH_PATH_RESOLVING) &&
		   !(mpath->flags & RTW_MESH_PATH_FIXED) &&
		   !(mpath->flags & RTW_MESH_PATH_BCAST_PREQ) &&
		   mpath->is_root && nexthop_alive == _FALSE) {
		enter_critical_bh(&mpath->state_lock);
		mpath->flags |= RTW_MESH_PATH_BCAST_PREQ;
		exit_critical_bh(&mpath->state_lock);
	}

endlookup:
	rtw_rcu_read_unlock();
	return err;
}

#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
static bool rtw_mesh_data_bmc_to_uc(_adapter *adapter
	, const u8 *da, const u8 *sa, const u8 *mda, const u8 *msa
	, u8 ae_need, const u8 *ori_ta, u8 mfwd_ttl
	, u16 os_qid, _list *b2u_list, u8 *b2u_num, u32 *b2u_mseq)
{
	struct sta_priv *stapriv = &adapter->stapriv;
	struct xmit_priv *xmitpriv = &adapter->xmitpriv;
	_irqL irqL;
	_list *head, *list;
	struct sta_info *sta;
	char b2u_sta_id[NUM_STA];
	u8 b2u_sta_num = 0;
	bool bmc_need = _FALSE;
	int i;

	_enter_critical_bh(&stapriv->asoc_list_lock, &irqL);
	head = &stapriv->asoc_list;
	list = get_next(head);

	while ((rtw_end_of_queue_search(head, list)) == _FALSE) {
		int stainfo_offset;

		sta = LIST_CONTAINOR(list, struct sta_info, asoc_list);
		list = get_next(list);
	
		stainfo_offset = rtw_stainfo_offset(stapriv, sta);
		if (stainfo_offset_valid(stainfo_offset))
			b2u_sta_id[b2u_sta_num++] = stainfo_offset;
	}
	_exit_critical_bh(&stapriv->asoc_list_lock, &irqL);

	if (!b2u_sta_num)
		goto exit;

	for (i = 0; i < b2u_sta_num; i++) {
		struct xmit_frame *b2uframe;
		struct pkt_attrib *attrib;

		sta = rtw_get_stainfo_by_offset(stapriv, b2u_sta_id[i]);
		if (!(sta->state & WIFI_ASOC_STATE)
			|| _rtw_memcmp(sta->cmn.mac_addr, msa, ETH_ALEN) == _TRUE
			|| (ori_ta && _rtw_memcmp(sta->cmn.mac_addr, ori_ta, ETH_ALEN) == _TRUE)
			|| is_broadcast_mac_addr(sta->cmn.mac_addr)
			|| is_zero_mac_addr(sta->cmn.mac_addr))
			continue;

		b2uframe = rtw_alloc_xmitframe(xmitpriv, os_qid);
		if (!b2uframe) {
			bmc_need = _TRUE;
			break;
		}

		if ((*b2u_num)++ == 0 && !ori_ta) {
			*b2u_mseq = (cpu_to_le32(adapter->mesh_info.mesh_seqnum));
			adapter->mesh_info.mesh_seqnum++;
		}

		attrib = &b2uframe->attrib;

		attrib->mb2u = 1;
		attrib->mseq = *b2u_mseq;
		attrib->mfwd_ttl = ori_ta ? mfwd_ttl : 0;
		_rtw_memcpy(attrib->ra, sta->cmn.mac_addr, ETH_ALEN);
		_rtw_memcpy(attrib->ta, adapter_mac_addr(adapter), ETH_ALEN);
		_rtw_memcpy(attrib->mda, mda, ETH_ALEN);
		_rtw_memcpy(attrib->msa, msa, ETH_ALEN);
		_rtw_memcpy(attrib->dst, da, ETH_ALEN);
		_rtw_memcpy(attrib->src, sa, ETH_ALEN);
		attrib->mesh_frame_mode = ae_need ? MESH_UCAST_PX_DATA : MESH_UCAST_DATA;

		rtw_list_insert_tail(&b2uframe->list, b2u_list);
	}

exit:
	return bmc_need;
}

void dump_mesh_b2u_flags(void *sel, _adapter *adapter)
{
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;

	RTW_PRINT_SEL(sel, "%4s %4s\n", "msrc", "mfwd");
	RTW_PRINT_SEL(sel, "0x%02x 0x%02x\n", mcfg->b2u_flags_msrc, mcfg->b2u_flags_mfwd);
}
#endif /* CONFIG_RTW_MESH_DATA_BMC_TO_UC */

int rtw_mesh_addr_resolve(_adapter *adapter, u16 os_qid, struct xmit_frame *xframe, _pkt *pkt, _list *b2u_list)
{
	struct pkt_file pktfile;
	struct ethhdr etherhdr;
	struct pkt_attrib *attrib;
	struct rtw_mesh_path *mpath = NULL, *mppath = NULL;
	u8 is_da_mcast;
	u8 ae_need;
#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
	bool bmc_need = _TRUE;
	u8 b2u_num = 0;
	u32 b2u_mseq = 0;
#endif
	int res = _SUCCESS;

	_rtw_open_pktfile(pkt, &pktfile);
	if (_rtw_pktfile_read(&pktfile, (u8 *)&etherhdr, ETH_HLEN) != ETH_HLEN) {
		res = _FAIL;
		goto exit;
	}
	
	xframe->pkt = pkt;
#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
	_rtw_init_listhead(b2u_list);
#endif

	is_da_mcast = IS_MCAST(etherhdr.h_dest);
	if (!is_da_mcast) {
		struct sta_info *next_hop; 
		bool mpp_lookup = 1;
	
		mpath = rtw_mesh_path_lookup(adapter, etherhdr.h_dest);
		if (mpath) {
			mpp_lookup = 0;
			next_hop = rtw_rcu_dereference(mpath->next_hop);
			if (!next_hop
				|| !(mpath->flags & (RTW_MESH_PATH_ACTIVE | RTW_MESH_PATH_RESOLVING))
			) {
				/* mpath is not valid, search mppath */
				mpp_lookup = 1;
			}
		}

		if (mpp_lookup) {
			mppath = rtw_mpp_path_lookup(adapter, etherhdr.h_dest);
			if (mppath)
				mppath->exp_time = rtw_get_current_time();
		}

		if (mppath && mpath)
			rtw_mesh_path_del(adapter, mpath->dst);

		ae_need = _rtw_memcmp(adapter_mac_addr(adapter), etherhdr.h_source, ETH_ALEN) == _FALSE
			|| (mppath && _rtw_memcmp(mppath->mpp, etherhdr.h_dest, ETH_ALEN) == _FALSE);
	} else {
		ae_need = _rtw_memcmp(adapter_mac_addr(adapter), etherhdr.h_source, ETH_ALEN) == _FALSE;

		#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
		if (rtw_msrc_b2u_policy_chk(adapter->mesh_cfg.b2u_flags_msrc, etherhdr.h_dest)) {
			bmc_need = rtw_mesh_data_bmc_to_uc(adapter
				, etherhdr.h_dest, etherhdr.h_source
				, etherhdr.h_dest, adapter_mac_addr(adapter), ae_need, NULL, 0
				, os_qid, b2u_list, &b2u_num, &b2u_mseq);
			if (bmc_need == _FALSE) {
				res = RTW_BMC_NO_NEED;
				goto exit;
			}
		}
		#endif
	}

	attrib = &xframe->attrib;

#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
	if (b2u_num) {
		attrib->mb2u = 1;
		attrib->mseq = b2u_mseq;
	} else
		attrib->mb2u = 0;
#endif

	attrib->mfwd_ttl = 0;
	_rtw_memcpy(attrib->dst, etherhdr.h_dest, ETH_ALEN);
	_rtw_memcpy(attrib->src, etherhdr.h_source, ETH_ALEN);
	_rtw_memcpy(attrib->ta, adapter_mac_addr(adapter), ETH_ALEN);

	if (is_da_mcast) {
		attrib->mesh_frame_mode = ae_need ? MESH_BMCAST_PX_DATA : MESH_BMCAST_DATA;
		_rtw_memcpy(attrib->ra, attrib->dst, ETH_ALEN);
		_rtw_memcpy(attrib->msa, adapter_mac_addr(adapter), ETH_ALEN);
	} else {
		attrib->mesh_frame_mode = ae_need ? MESH_UCAST_PX_DATA : MESH_UCAST_DATA;
		_rtw_memcpy(attrib->mda, (mppath && ae_need) ? mppath->mpp : attrib->dst, ETH_ALEN);
		_rtw_memcpy(attrib->msa, adapter_mac_addr(adapter), ETH_ALEN);
		/* RA needs to be resolved */
		res = rtw_mesh_nexthop_resolve(adapter, xframe);
	}

exit:
	return res;
}

s8 rtw_mesh_tx_set_whdr_mctrl_len(u8 mesh_frame_mode, struct pkt_attrib *attrib)
{
	u8 ret = 0;
	switch (mesh_frame_mode) {
	case MESH_UCAST_DATA:
		attrib->hdrlen = WLAN_HDR_A4_QOS_LEN;
		/* mesh flag + mesh TTL + Mesh SN. no ext addr. */
		attrib->meshctrl_len = 6;
		break;
	case MESH_BMCAST_DATA:
		attrib->hdrlen = WLAN_HDR_A3_QOS_LEN;
		/* mesh flag + mesh TTL + Mesh SN. no ext addr. */
		attrib->meshctrl_len = 6;
		break;
	case MESH_UCAST_PX_DATA:
		attrib->hdrlen = WLAN_HDR_A4_QOS_LEN;
		/* mesh flag + mesh TTL + Mesh SN + extaddr1 + extaddr2. */
		attrib->meshctrl_len = 18;
		break;
	case MESH_BMCAST_PX_DATA:
		attrib->hdrlen = WLAN_HDR_A3_QOS_LEN;
		/* mesh flag + mesh TTL + Mesh SN + extaddr1 */
		attrib->meshctrl_len = 12;
		break;
	default:
		RTW_WARN("Invalid mesh frame mode:%u\n", mesh_frame_mode);
		ret = -1;
		break;
	}				

	return ret;
}

void rtw_mesh_tx_build_mctrl(_adapter *adapter, struct pkt_attrib *attrib, u8 *buf)
{
	struct rtw_ieee80211s_hdr *mctrl = (struct rtw_ieee80211s_hdr *)buf;

	_rtw_memset(mctrl, 0, XATTRIB_GET_MCTRL_LEN(attrib));

	if (attrib->mfwd_ttl
		#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
		|| attrib->mb2u
		#endif
	) {
		#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
		if (!attrib->mfwd_ttl)
			mctrl->ttl = adapter->mesh_cfg.dot11MeshTTL;
		else
		#endif
			mctrl->ttl = attrib->mfwd_ttl;

		mctrl->seqnum = (cpu_to_le32(attrib->mseq));
	} else {
		mctrl->ttl = adapter->mesh_cfg.dot11MeshTTL;
		mctrl->seqnum = (cpu_to_le32(adapter->mesh_info.mesh_seqnum));
		adapter->mesh_info.mesh_seqnum++;
	}

	switch (attrib->mesh_frame_mode){
	case MESH_UCAST_DATA:
	case MESH_BMCAST_DATA:
		break;
	case MESH_UCAST_PX_DATA:
		mctrl->flags |= MESH_FLAGS_AE_A5_A6;
		_rtw_memcpy(mctrl->eaddr1, attrib->dst, ETH_ALEN);
		_rtw_memcpy(mctrl->eaddr2, attrib->src, ETH_ALEN);
		break;
	case MESH_BMCAST_PX_DATA:
		mctrl->flags |= MESH_FLAGS_AE_A4;
		_rtw_memcpy(mctrl->eaddr1, attrib->src, ETH_ALEN);
		break;
	case MESH_MHOP_UCAST_ACT:
		/* TBD */
		break;
	case MESH_MHOP_BMCAST_ACT:
		/* TBD */
		break;
	default:
		break;
	}
}

u8 rtw_mesh_tx_build_whdr(_adapter *adapter, struct pkt_attrib *attrib
	, u16 *fctrl, struct rtw_ieee80211_hdr *whdr)
{
	switch (attrib->mesh_frame_mode) {
	case MESH_UCAST_DATA:		/* 1, 1, RA, TA, mDA(=DA),	mSA(=SA) */
	case MESH_UCAST_PX_DATA:	/* 1, 1, RA, TA, mDA,		mSA,		[DA, SA] */
		SetToDs(fctrl);
		SetFrDs(fctrl);
		_rtw_memcpy(whdr->addr1, attrib->ra, ETH_ALEN);
		_rtw_memcpy(whdr->addr2, attrib->ta, ETH_ALEN);
		_rtw_memcpy(whdr->addr3, attrib->mda, ETH_ALEN);
		_rtw_memcpy(whdr->addr4, attrib->msa, ETH_ALEN);
		break;
	case MESH_BMCAST_DATA:		/* 0, 1, RA(DA), TA, mSA(SA) */
	case MESH_BMCAST_PX_DATA:	/* 0, 1, RA(DA), TA, mSA,		[SA] */
		SetFrDs(fctrl);
		_rtw_memcpy(whdr->addr1, attrib->ra, ETH_ALEN);
		_rtw_memcpy(whdr->addr2, attrib->ta, ETH_ALEN);
		_rtw_memcpy(whdr->addr3, attrib->msa, ETH_ALEN);
		break;
	case MESH_MHOP_UCAST_ACT:
		/* TBD */
		RTW_INFO("MESH_MHOP_UCAST_ACT\n");
		break;
	case MESH_MHOP_BMCAST_ACT:
		/* TBD */
		RTW_INFO("MESH_MHOP_BMCAST_ACT\n");
		break;
	default:
		RTW_WARN("Invalid mesh frame mode\n");
		break;
	}
	
	return 0;
}

int rtw_mesh_rx_data_validate_hdr(_adapter *adapter, union recv_frame *rframe, struct sta_info **sta)
{
	struct sta_priv *stapriv = &adapter->stapriv;
	struct rx_pkt_attrib *rattrib = &rframe->u.hdr.attrib;
	u8 *whdr = get_recvframe_data(rframe);
	u8 is_ra_bmc = 0;
	u8 a4_shift = 0;
	u8 ps;
	u8 *qc;
	u8 mps_mode = RTW_MESH_PS_UNKNOWN;
	sint ret = _FAIL;

	if (!(MLME_STATE(adapter) & WIFI_ASOC_STATE))
		goto exit;

	if (!rattrib->qos)
		goto exit;

	switch (rattrib->to_fr_ds) {
	case 1:
		if (!IS_MCAST(GetAddr1Ptr(whdr)))
			goto exit;
		*sta = rtw_get_stainfo(stapriv, get_addr2_ptr(whdr));
		if (*sta == NULL) {
			ret = _SUCCESS; /* return _SUCCESS to drop at sta checking */
			goto exit;
		}
		_rtw_memcpy(rattrib->ra, GetAddr1Ptr(whdr), ETH_ALEN);
		_rtw_memcpy(rattrib->ta, get_addr2_ptr(whdr), ETH_ALEN);
		_rtw_memcpy(rattrib->mda, GetAddr1Ptr(whdr), ETH_ALEN);
		_rtw_memcpy(rattrib->msa, GetAddr3Ptr(whdr), ETH_ALEN); /* may change after checking AMSDU subframe header */
		_rtw_memcpy(rattrib->dst, GetAddr1Ptr(whdr), ETH_ALEN);
		_rtw_memcpy(rattrib->src, GetAddr3Ptr(whdr), ETH_ALEN); /* may change after checking mesh ctrl field */
		_rtw_memcpy(rattrib->bssid, get_addr2_ptr(whdr), ETH_ALEN);
		is_ra_bmc = 1;
		break;
	case 3:
		if (IS_MCAST(GetAddr1Ptr(whdr)))
			goto exit;
		*sta = rtw_get_stainfo(stapriv, get_addr2_ptr(whdr));
		if (*sta == NULL) {
			ret = _SUCCESS; /* return _SUCCESS to drop at sta checking */
			goto exit;
		}
		_rtw_memcpy(rattrib->ra, GetAddr1Ptr(whdr), ETH_ALEN);
		_rtw_memcpy(rattrib->ta, get_addr2_ptr(whdr), ETH_ALEN);
		_rtw_memcpy(rattrib->mda, GetAddr3Ptr(whdr), ETH_ALEN); /* may change after checking AMSDU subframe header */
		_rtw_memcpy(rattrib->msa, GetAddr4Ptr(whdr), ETH_ALEN); /* may change after checking AMSDU subframe header */
		_rtw_memcpy(rattrib->dst, GetAddr3Ptr(whdr), ETH_ALEN); /* may change after checking mesh ctrl field */
		_rtw_memcpy(rattrib->src, GetAddr4Ptr(whdr), ETH_ALEN); /* may change after checking mesh ctrl field */
		_rtw_memcpy(rattrib->bssid, get_addr2_ptr(whdr), ETH_ALEN);
		a4_shift = ETH_ALEN;
		break;
	default:
		goto exit;
	}

	qc = whdr + WLAN_HDR_A3_LEN + a4_shift;
	ps = GetPwrMgt(whdr);
	mps_mode = ps ? (is_ra_bmc || (get_mps_lv(qc)) ? RTW_MESH_PS_DSLEEP : RTW_MESH_PS_LSLEEP) : RTW_MESH_PS_ACTIVE;

	if (ps) {
		if (!((*sta)->state & WIFI_SLEEP_STATE))
			stop_sta_xmit(adapter, *sta);
	} else {
		if ((*sta)->state & WIFI_SLEEP_STATE)
			wakeup_sta_to_xmit(adapter, *sta);
	}

	if (is_ra_bmc)
		(*sta)->nonpeer_mps = mps_mode;
	else {
		(*sta)->peer_mps = mps_mode;
		if (mps_mode != RTW_MESH_PS_ACTIVE && (*sta)->nonpeer_mps == RTW_MESH_PS_ACTIVE)
			(*sta)->nonpeer_mps = RTW_MESH_PS_DSLEEP;
	}

	if (get_frame_sub_type(whdr) & BIT(6)) {
		/* No data, will not indicate to upper layer, temporily count it here */
		count_rx_stats(adapter, rframe, *sta);
		ret = RTW_RX_HANDLED;
		goto exit;
	}

	rattrib->mesh_ctrl_present = get_mctrl_present(qc) ? 1 : 0;
	if (!rattrib->mesh_ctrl_present)
		goto exit;

	ret = _SUCCESS;

exit:
	return ret;
}

int rtw_mesh_rx_data_validate_mctrl(_adapter *adapter, union recv_frame *rframe
	, const struct rtw_ieee80211s_hdr *mctrl, const u8 *mda, const u8 *msa
	, u8 *mctrl_len
	, const u8 **da, const u8 **sa)
{
	struct rx_pkt_attrib *rattrib = &rframe->u.hdr.attrib;
	u8 mlen;
	u8 ae;
	int ret = _SUCCESS;

	ae = mctrl->flags & MESH_FLAGS_AE;
	mlen = ae_to_mesh_ctrl_len[ae];
	switch (rattrib->to_fr_ds) {
	case 1:
		*da = mda;
		if (ae == MESH_FLAGS_AE_A4)
			*sa = mctrl->eaddr1;
		else if (ae == 0)
			*sa = msa;
		else
			ret = _FAIL;
		break;
	case 3:
		if (ae == MESH_FLAGS_AE_A5_A6) {
			*da = mctrl->eaddr1;
			*sa = mctrl->eaddr2;
		} else if (ae == 0) {
			*da = mda;
			*sa = msa;
		} else
			ret = _FAIL;
		break;
	default:
		ret = _FAIL;
	}

	if (ret == _FAIL) {
		#ifdef DBG_RX_DROP_FRAME
		RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" invalid tfDS:%u AE:%u combination ra="MAC_FMT" ta="MAC_FMT"\n"
			, FUNC_ADPT_ARG(adapter), rattrib->to_fr_ds, ae, MAC_ARG(rattrib->ra), MAC_ARG(rattrib->ta));
		#endif
		*mctrl_len = 0;
	} else
		*mctrl_len = mlen;

	return ret;	
}

inline int rtw_mesh_rx_validate_mctrl_non_amsdu(_adapter *adapter, union recv_frame *rframe)
{
	struct rx_pkt_attrib *rattrib = &rframe->u.hdr.attrib;
	const u8 *da, *sa;
	int ret;

	ret = rtw_mesh_rx_data_validate_mctrl(adapter, rframe
			, (struct rtw_ieee80211s_hdr *)(get_recvframe_data(rframe) + rattrib->hdrlen + rattrib->iv_len)
			, rattrib->mda, rattrib->msa
			, &rattrib->mesh_ctrl_len
			, &da, &sa);

	if (ret == _SUCCESS) {
		_rtw_memcpy(rattrib->dst, da, ETH_ALEN);
		_rtw_memcpy(rattrib->src, sa, ETH_ALEN);
	}

	return ret;
}

/**
 * rtw_mesh_rx_nexthop_resolve - lookup next hop; conditionally start path discovery
 *
 * @skb: 802.11 frame to be sent
 * @sdata: network subif the frame will be sent through
 *
 * Lookup next hop for given skb and start path discovery if no
 * forwarding information is found.
 *
 * Returns: 0 if the next hop was found and -ENOENT if the frame was queued.
 * skb is freeed here if no mpath could be allocated.
 */
static int rtw_mesh_rx_nexthop_resolve(_adapter *adapter,
	const u8 *mda, const u8 *msa, u8 *ra)
{
	struct rtw_mesh_path *mpath;
	struct xmit_frame *xframe_to_free = NULL;
	int err = 0;
	int ret = _SUCCESS;

	rtw_rcu_read_lock();
	err = rtw_mesh_nexthop_lookup(adapter, mda, msa, ra);
	if (!err)
		goto endlookup;

	/* no nexthop found, start resolving */
	mpath = rtw_mesh_path_lookup(adapter, mda);
	if (!mpath) {
		mpath = rtw_mesh_path_add(adapter, mda);
		if (IS_ERR(mpath)) {
			err = PTR_ERR(mpath);
			ret = _FAIL;
			goto endlookup;
		}
	}

	if (!(mpath->flags & RTW_MESH_PATH_RESOLVING))
		rtw_mesh_queue_preq(mpath, RTW_PREQ_Q_F_START);

	ret = _FAIL;

endlookup:
	rtw_rcu_read_unlock();
	return ret;
}

#define RTW_MESH_DECACHE_BMC 1
#define RTW_MESH_DECACHE_UC 0

#define RTW_MESH_FORWARD_MDA_SELF_COND 0
#define DBG_RTW_MESH_FORWARD_MDA_SELF_COND 0
int rtw_mesh_rx_msdu_act_check(union recv_frame *rframe
	, const u8 *mda, const u8 *msa
	, const u8 *da, const u8 *sa
	, struct rtw_ieee80211s_hdr *mctrl
	, u8 *msdu, enum rtw_rx_llc_hdl llc_hdl
	, struct xmit_frame **fwd_frame, _list *b2u_list)
{
	_adapter *adapter = rframe->u.hdr.adapter;
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct rx_pkt_attrib *rattrib = &rframe->u.hdr.attrib;
	struct rtw_mesh_path *mppath;
	u8 is_mda_bmc = IS_MCAST(mda); 
	u8 is_mda_self = !is_mda_bmc && _rtw_memcmp(mda, adapter_mac_addr(adapter), ETH_ALEN);
	u16 os_qid;
	struct xmit_frame *xframe;
	struct pkt_attrib *xattrib;
	u8 fwd_ra[ETH_ALEN] = {0};
	u8 fwd_mpp[ETH_ALEN] = {0}; /* forward to other gate */
	u32 fwd_mseq;
	int act = 0;
	u8 ae_need;
#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
	bool bmc_need = _TRUE;
	u8 b2u_num = 0;
#endif

	/* fwd info lifetime update */
	#if 0
	if (!is_mda_self)
		mDA(A3) fwinfo.lifetime
	mSA(A4) fwinfo.lifetime
	Precursor-to-mDA(A2) fwinfo.lifetime
	#endif

	/* update/create pxoxy info for SA, mSA */
	if ((mctrl->flags & MESH_FLAGS_AE)
		&& sa != msa && _rtw_memcmp(sa, msa, ETH_ALEN) == _FALSE
	) {
		const u8 *proxied_addr = sa;
		const u8 *mpp_addr = msa;

		rtw_rcu_read_lock();
		mppath = rtw_mpp_path_lookup(adapter, proxied_addr);
		if (!mppath)
			rtw_mpp_path_add(adapter, proxied_addr, mpp_addr);
		else {
			enter_critical_bh(&mppath->state_lock);
			if (_rtw_memcmp(mppath->mpp, mpp_addr, ETH_ALEN) == _FALSE)
				_rtw_memcpy(mppath->mpp, mpp_addr, ETH_ALEN);
			mppath->exp_time = rtw_get_current_time();
			exit_critical_bh(&mppath->state_lock);
		}
		rtw_rcu_read_unlock();
	}

	/* mSA is self, need no further process */
	if (_rtw_memcmp(msa, adapter_mac_addr(adapter), ETH_ALEN) == _TRUE)
		goto exit;

	fwd_mseq = le32_to_cpu(mctrl->seqnum);

	/* check duplicate MSDU from mSA */
	if (((RTW_MESH_DECACHE_BMC && is_mda_bmc)
			|| (RTW_MESH_DECACHE_UC && !is_mda_bmc))
		&& rtw_mesh_decache(adapter, msa, fwd_mseq)
	) {
		minfo->mshstats.dropped_frames_duplicate++;
		goto exit;
	}

	if (is_mda_bmc) {
		/* mDA is bmc addr */
		act |= RTW_RX_MSDU_ACT_INDICATE;
		if (!mcfg->dot11MeshForwarding)
			goto exit;
		goto fwd_chk;

	} else if (!is_mda_self) {
		/* mDA is unicast but not self */
		if (!mcfg->dot11MeshForwarding) {
			rtw_mesh_path_error_tx(adapter
				, adapter->mesh_cfg.element_ttl
				, mda, 0
				, WLAN_REASON_MESH_PATH_NOFORWARD
				, rattrib->ta
			);
			#ifdef DBG_RX_DROP_FRAME
			RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" mDA("MAC_FMT") not self, !dot11MeshForwarding\n"
				, FUNC_ADPT_ARG(adapter), MAC_ARG(mda));
			#endif
			goto exit;
		}

		if (rtw_mesh_rx_nexthop_resolve(adapter, mda, msa, fwd_ra) != _SUCCESS) {
			/* mDA is unknown */
			rtw_mesh_path_error_tx(adapter
				, adapter->mesh_cfg.element_ttl
				, mda, 0
				, WLAN_REASON_MESH_PATH_NOFORWARD
				, rattrib->ta
			);
			#ifdef DBG_RX_DROP_FRAME
			RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" mDA("MAC_FMT") unknown\n"
				, FUNC_ADPT_ARG(adapter), MAC_ARG(mda));
			#endif
			minfo->mshstats.dropped_frames_no_route++;
			goto exit;

		} else {
			/* mDA is known in fwd info */
			#if 0
			if	(TA is not in precursors)
				goto exit;
			#endif
			goto fwd_chk;
		}

	} else {
		/* mDA is self */
		#if RTW_MESH_FORWARD_MDA_SELF_COND
		if (da == mda
			|| _rtw_memcmp(da, adapter_mac_addr(adapter), ETH_ALEN)
		) {
			/* DA is self, indicate */
			act |= RTW_RX_MSDU_ACT_INDICATE;
			goto exit;
		}

		if (rtw_get_iface_by_macddr(adapter, da)) {
			/* DA is buddy, indicate */
			act |= RTW_RX_MSDU_ACT_INDICATE;
			#if DBG_RTW_MESH_FORWARD_MDA_SELF_COND
			RTW_INFO(FUNC_ADPT_FMT" DA("MAC_FMT") is buddy("ADPT_FMT")\n"
				, FUNC_ADPT_ARG(adapter), MAC_ARG(da), ADPT_ARG(rtw_get_iface_by_macddr(adapter, da)));
			#endif
			goto exit;
		}

		/* DA is not self or buddy */
		if (rtw_mesh_nexthop_lookup(adapter, da, msa, fwd_ra) == 0) {
			/* DA is known in fwd info */
			if (!mcfg->dot11MeshForwarding) {
				/* path error to? */
				#if defined(DBG_RX_DROP_FRAME) || DBG_RTW_MESH_FORWARD_MDA_SELF_COND
				RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" DA("MAC_FMT") not self, !dot11MeshForwarding\n"
					, FUNC_ADPT_ARG(adapter), MAC_ARG(da));
				#endif
				goto exit;
			}
			mda = da;
			#if DBG_RTW_MESH_FORWARD_MDA_SELF_COND
			RTW_INFO(FUNC_ADPT_FMT" fwd to DA("MAC_FMT"), fwd_RA("MAC_FMT")\n"
				, FUNC_ADPT_ARG(adapter), MAC_ARG(da), MAC_ARG(fwd_ra));
			#endif
			goto fwd_chk;
		}

		rtw_rcu_read_lock();
		mppath = rtw_mpp_path_lookup(adapter, da);
		if (mppath) {
			if (_rtw_memcmp(mppath->mpp, adapter_mac_addr(adapter), ETH_ALEN) == _FALSE) {
				/* DA is proxied by others */
				if (!mcfg->dot11MeshForwarding) {
					/* path error to? */
					#if defined(DBG_RX_DROP_FRAME) || DBG_RTW_MESH_FORWARD_MDA_SELF_COND
					RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" DA("MAC_FMT") is proxied by ("MAC_FMT"), !dot11MeshForwarding\n"
						, FUNC_ADPT_ARG(adapter), MAC_ARG(da), MAC_ARG(mppath->mpp));
					#endif
					rtw_rcu_read_unlock();
					goto exit;
				}
				_rtw_memcpy(fwd_mpp, mppath->mpp, ETH_ALEN);
				mda = fwd_mpp;
				msa = adapter_mac_addr(adapter);
				rtw_rcu_read_unlock();

				/* resolve RA */
				if (rtw_mesh_nexthop_lookup(adapter, mda, msa, fwd_ra) != 0) {
					minfo->mshstats.dropped_frames_no_route++;
					#if defined(DBG_RX_DROP_FRAME) || DBG_RTW_MESH_FORWARD_MDA_SELF_COND
					RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" DA("MAC_FMT") is proxied by ("MAC_FMT"), RA resolve fail\n"
						, FUNC_ADPT_ARG(adapter), MAC_ARG(da), MAC_ARG(mppath->mpp));
					#endif
					goto exit;
				}
				#if DBG_RTW_MESH_FORWARD_MDA_SELF_COND
				RTW_INFO(FUNC_ADPT_FMT" DA("MAC_FMT") is proxied by ("MAC_FMT"), fwd_RA("MAC_FMT")\n"
					, FUNC_ADPT_ARG(adapter), MAC_ARG(da), MAC_ARG(mppath->mpp), MAC_ARG(fwd_ra));
				#endif
				goto fwd_chk; /*  forward to other gate */
			} else {
				#if DBG_RTW_MESH_FORWARD_MDA_SELF_COND
				RTW_INFO(FUNC_ADPT_FMT" DA("MAC_FMT") is proxied by self\n"
					, FUNC_ADPT_ARG(adapter), MAC_ARG(da));
				#endif
			}
		}
		rtw_rcu_read_unlock();

		if (!mppath) {
			#if DBG_RTW_MESH_FORWARD_MDA_SELF_COND
			RTW_INFO(FUNC_ADPT_FMT" DA("MAC_FMT") unknown\n"
				, FUNC_ADPT_ARG(adapter), MAC_ARG(da));
			#endif
			/* DA is unknown */
			#if 0 /* TODO: flags with AE bit */
			rtw_mesh_path_error_tx(adapter
				, adapter->mesh_cfg.element_ttl
				, mda, adapter->mesh_info.last_sn_update
				, WLAN_REASON_MESH_PATH_NOPROXY
				, msa
			);
			#endif
		}

		/*
		* indicate to DS for both cases:
		* 1.) DA is proxied by self
		* 2.) DA is unknown
		*/
		#endif /* RTW_MESH_FORWARD_MDA_SELF_COND */
		act |= RTW_RX_MSDU_ACT_INDICATE;
		goto exit;
	}

fwd_chk:

	if (adapter->stapriv.asoc_list_cnt <= 1)
		goto exit;

	if (mctrl->ttl == 1) {
		minfo->mshstats.dropped_frames_ttl++;
		if (!act) {
			#ifdef DBG_RX_DROP_FRAME
			RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" ttl reaches 0, not forwarding\n"
				, FUNC_ADPT_ARG(adapter));
			#endif
		}
		goto exit;
	}

	os_qid = rtw_os_recv_select_queue(msdu, llc_hdl);

#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
	_rtw_init_listhead(b2u_list);
#endif

	ae_need = _rtw_memcmp(da , mda, ETH_ALEN) == _FALSE
		|| _rtw_memcmp(sa , msa, ETH_ALEN) == _FALSE;

#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
	if (is_mda_bmc
		&& rtw_mfwd_b2u_policy_chk(mcfg->b2u_flags_mfwd, mda, rattrib->to_fr_ds == 3)
	) {
		bmc_need = rtw_mesh_data_bmc_to_uc(adapter
			, da, sa, mda, msa, ae_need, rframe->u.hdr.psta->cmn.mac_addr, mctrl->ttl - 1
			, os_qid, b2u_list, &b2u_num, &fwd_mseq);
	}

	if (bmc_need == _TRUE)
#endif
	{
		xframe = rtw_alloc_xmitframe(&adapter->xmitpriv, os_qid);
		if (!xframe) {
			#ifdef DBG_TX_DROP_FRAME
			RTW_INFO("DBG_TX_DROP_FRAME "FUNC_ADPT_FMT" rtw_alloc_xmitframe fail\n"
				, FUNC_ADPT_ARG(adapter));
			#endif
			goto exit;
		}

		xattrib = &xframe->attrib;

#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
		if (b2u_num)
			xattrib->mb2u = 1;
		else
			xattrib->mb2u = 0;
#endif
		xattrib->mfwd_ttl = mctrl->ttl - 1;
		xattrib->mseq = fwd_mseq;
		_rtw_memcpy(xattrib->dst, da, ETH_ALEN);
		_rtw_memcpy(xattrib->src, sa, ETH_ALEN);
		_rtw_memcpy(xattrib->mda, mda, ETH_ALEN);
		_rtw_memcpy(xattrib->msa, msa, ETH_ALEN);
		_rtw_memcpy(xattrib->ta, adapter_mac_addr(adapter), ETH_ALEN);

		if (is_mda_bmc) {
			xattrib->mesh_frame_mode = ae_need ? MESH_BMCAST_PX_DATA : MESH_BMCAST_DATA;
			_rtw_memcpy(xattrib->ra, mda, ETH_ALEN);
		} else {
			xattrib->mesh_frame_mode = ae_need ? MESH_UCAST_PX_DATA : MESH_UCAST_DATA;
			_rtw_memcpy(xattrib->ra, fwd_ra, ETH_ALEN);
		}

		*fwd_frame = xframe;
	}

	act |= RTW_RX_MSDU_ACT_FORWARD;
	if (is_mda_bmc)
		minfo->mshstats.fwded_mcast++;
	else
		minfo->mshstats.fwded_unicast++;
	minfo->mshstats.fwded_frames++;

exit:
	return act;
}

void dump_mesh_stats(void *sel, _adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct rtw_mesh_stats *stats = &minfo->mshstats;

	RTW_PRINT_SEL(sel, "fwd_bmc:%u\n", stats->fwded_mcast);
	RTW_PRINT_SEL(sel, "fwd_uc:%u\n", stats->fwded_unicast);

	RTW_PRINT_SEL(sel, "drop_ttl:%u\n", stats->dropped_frames_ttl);
	RTW_PRINT_SEL(sel, "drop_no_route:%u\n", stats->dropped_frames_no_route);
	RTW_PRINT_SEL(sel, "drop_congestion:%u\n", stats->dropped_frames_congestion);
	RTW_PRINT_SEL(sel, "drop_dup:%u\n", stats->dropped_frames_duplicate);

	RTW_PRINT_SEL(sel, "mrc_del_qlen:%u\n", stats->mrc_del_qlen);
}
#endif /* CONFIG_RTW_MESH */

