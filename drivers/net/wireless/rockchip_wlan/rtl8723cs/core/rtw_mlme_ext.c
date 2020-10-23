/******************************************************************************
 *
 * Copyright(c) 2007 - 2019 Realtek Corporation.
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
#define _RTW_MLME_EXT_C_

#include <drv_types.h>
#include <hal_data.h>

struct mlme_handler mlme_sta_tbl[] = {
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
	{WIFI_ACTION_NOACK, "OnActionNoAck",	&OnAction},
};

#ifdef _CONFIG_NATIVEAP_MLME_
struct mlme_handler mlme_ap_tbl[] = {
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
	{WIFI_AUTH,			"OnAuth",		&OnAuth},
	{WIFI_DEAUTH,		"OnDeAuth",		&OnDeAuth},
	{WIFI_ACTION,		"OnAction",		&OnAction},
	{WIFI_ACTION_NOACK, "OnActionNoAck",	&OnAction},
};
#endif

struct action_handler OnAction_tbl[] = {
	{RTW_WLAN_CATEGORY_SPECTRUM_MGMT,	 "ACTION_SPECTRUM_MGMT", on_action_spct},
	{RTW_WLAN_CATEGORY_QOS, "ACTION_QOS", &OnAction_qos},
	{RTW_WLAN_CATEGORY_DLS, "ACTION_DLS", &OnAction_dls},
	{RTW_WLAN_CATEGORY_BACK, "ACTION_BACK", &OnAction_back},
	{RTW_WLAN_CATEGORY_PUBLIC, "ACTION_PUBLIC", on_action_public},
	{RTW_WLAN_CATEGORY_RADIO_MEAS, "ACTION_RADIO_MEAS", &on_action_rm},
	{RTW_WLAN_CATEGORY_FT, "ACTION_FT",	&OnAction_ft},
	{RTW_WLAN_CATEGORY_HT,	"ACTION_HT",	&OnAction_ht},
#ifdef CONFIG_IEEE80211W
	{RTW_WLAN_CATEGORY_SA_QUERY, "ACTION_SA_QUERY", &OnAction_sa_query},
#else
	{RTW_WLAN_CATEGORY_SA_QUERY, "ACTION_SA_QUERY", &DoReserved},
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_RTW_WNM
	{RTW_WLAN_CATEGORY_WNM, "ACTION_WNM", &on_action_wnm},
#endif
	{RTW_WLAN_CATEGORY_UNPROTECTED_WNM, "ACTION_UNPROTECTED_WNM", &DoReserved},
#ifdef CONFIG_RTW_MESH
	{RTW_WLAN_CATEGORY_MESH, "ACTION_MESH", &on_action_mesh},
	{RTW_WLAN_CATEGORY_SELF_PROTECTED, "ACTION_SELF_PROTECTED", &on_action_self_protected},
#endif
	{RTW_WLAN_CATEGORY_WMM, "ACTION_WMM", &OnAction_wmm},
	{RTW_WLAN_CATEGORY_VHT, "ACTION_VHT", &OnAction_vht},
	{RTW_WLAN_CATEGORY_P2P, "ACTION_P2P", &OnAction_p2p},
#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
	{RTW_WLAN_CATEGORY_TBTX, "ACTION_TBTX_TOKEN", &OnAction_tbtx_token}
#endif
};


u8	null_addr[ETH_ALEN] = {0, 0, 0, 0, 0, 0};

/**************************************************
OUI definitions for the vendor specific IE
***************************************************/
unsigned char	RTW_WPA_OUI[] = {0x00, 0x50, 0xf2, 0x01};
unsigned char WMM_OUI[] = {0x00, 0x50, 0xf2, 0x02};
unsigned char	WPS_OUI[] = {0x00, 0x50, 0xf2, 0x04};
unsigned char	P2P_OUI[] = {0x50, 0x6F, 0x9A, 0x09};
unsigned char	WFD_OUI[] = {0x50, 0x6F, 0x9A, 0x0A};
unsigned char	DPP_OUI[] = {0x50, 0x6F, 0x9A, 0x1A};
unsigned char	MULTI_AP_OUI[] = {0x50, 0x6F, 0x9A, 0x1B};

unsigned char	WMM_INFO_OUI[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01};
unsigned char	WMM_PARA_OUI[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};

unsigned char WPA_TKIP_CIPHER[4] = {0x00, 0x50, 0xf2, 0x02};
unsigned char RSN_TKIP_CIPHER[4] = {0x00, 0x0f, 0xac, 0x02};

#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
unsigned char REALTEK_TBTX_IE[] = {0x00, 0xe0, 0x4c, 0x01, 0x00, 0x00, 0x00, 0x00};
#endif
extern unsigned char REALTEK_96B_IE[];

static void init_channel_list(_adapter *padapter, RT_CHANNEL_INFO *channel_set
	, struct p2p_channels *channel_list)
{
	struct registry_priv *regsty = adapter_to_regsty(padapter);

	struct p2p_oper_class_map op_class[] = {
		{ IEEE80211G,  81,   1,  13,  1, BW20 },
		{ IEEE80211G,  82,  14,  14,  1, BW20 },
#if 0 /* Do not enable HT40 on 2 GHz */
		{ IEEE80211G,  83,   1,   9,  1, BW40PLUS },
		{ IEEE80211G,  84,   5,  13,  1, BW40MINUS },
#endif
		{ IEEE80211A, 115,  36,  48,  4, BW20 },
		{ IEEE80211A, 116,  36,  44,  8, BW40PLUS },
		{ IEEE80211A, 117,  40,  48,  8, BW40MINUS },
		{ IEEE80211A, 124, 149, 161,  4, BW20 },
		{ IEEE80211A, 125, 149, 169,  4, BW20 },
		{ IEEE80211A, 126, 149, 157,  8, BW40PLUS },
		{ IEEE80211A, 127, 153, 161,  8, BW40MINUS },
		{ -1, 0, 0, 0, 0, BW20 }
	};

	int cla, op;

	cla = 0;

	for (op = 0; op_class[op].op_class; op++) {
		u8 ch;
		struct p2p_oper_class_map *o = &op_class[op];
		struct p2p_reg_class *reg = NULL;

		for (ch = o->min_chan; ch <= o->max_chan; ch += o->inc) {
			if (rtw_chset_search_ch(channel_set, ch) == -1)
				continue;
#if defined(CONFIG_80211N_HT) || defined(CONFIG_80211AC_VHT)
			if ((padapter->registrypriv.ht_enable == 0) && (o->inc == 8))
				continue;

			if ((REGSTY_IS_BW_5G_SUPPORT(regsty, CHANNEL_WIDTH_40)) &&
			    ((o->bw == BW40MINUS) || (o->bw == BW40PLUS)))
				continue;
#endif
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

#if CONFIG_TXPWR_LIMIT
void rtw_txpwr_init_regd(struct rf_ctl_t *rfctl)
{
	u8 regd;
	struct regd_exc_ent *exc;
	struct txpwr_lmt_ent *ent;
	_irqL irqL;

	_enter_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);

	rfctl->regd_name = NULL;

	if (rfctl->txpwr_regd_num == 0) {
		RTW_PRINT("there is no any txpwr_regd\n");
		goto release_lock;
	}

	/* search from exception mapping */
	exc = _rtw_regd_exc_search(rfctl
		, rfctl->country_ent ? rfctl->country_ent->alpha2 : NULL
		, rfctl->ChannelPlan);
	if (exc) {
		u8 has_country = (exc->country[0] == '\0' && exc->country[1] == '\0') ? 0 : 1;

		if (strcmp(exc->regd_name, regd_str(TXPWR_LMT_NONE)) == 0)
			rfctl->regd_name = regd_str(TXPWR_LMT_NONE);
		else if (strcmp(exc->regd_name, regd_str(TXPWR_LMT_WW)) == 0)
			rfctl->regd_name = regd_str(TXPWR_LMT_WW);
		else {
			ent = _rtw_txpwr_lmt_get_by_name(rfctl, exc->regd_name);
			if (ent)
				rfctl->regd_name = ent->regd_name;
		}

		RTW_PRINT("exception mapping country:%c%c domain:0x%02x to%s regd_name:%s\n"
			, has_country ? exc->country[0] : '0'
			, has_country ? exc->country[1] : '0'
			, exc->domain
			, rfctl->regd_name ? "" : " unknown"
			, exc->regd_name
		);
		if (rfctl->regd_name)
			goto release_lock;
	}

#ifdef CONFIG_REGD_SRC_FROM_OS
	if (rfctl->regd_src == REGD_SRC_OS) {
		if (IS_ALPHA2_WORLDWIDE(rfctl->country_ent->alpha2))
			rfctl->regd_name = regd_str(TXPWR_LMT_WW);
		else {
			char alpha2[3] = {
				rfctl->country_ent->alpha2[0], rfctl->country_ent->alpha2[1], 0};

			ent = _rtw_txpwr_lmt_get_by_name(rfctl, alpha2);
			if (ent)
				rfctl->regd_name = ent->regd_name;
		}

		if (rfctl->regd_name) {
			RTW_PRINT("mapping country:%c%c to regd_name:%s\n"
				, rfctl->country_ent->alpha2[0]
				, rfctl->country_ent->alpha2[1]
				, rfctl->regd_name
			);
			goto release_lock;
		}

		if (rfctl->ChannelPlan == RTW_CHPLAN_UNSPECIFIED) {
			rfctl->regd_name = regd_str(TXPWR_LMT_WW);
			RTW_PRINT("mapping unsupported country:%c%c to regd_name:%s\n"
				, rfctl->country_ent->alpha2[0]
				, rfctl->country_ent->alpha2[1]
				, rfctl->regd_name
			);
			goto release_lock;
		}
	}
#endif

	/* follow default channel plan mapping */
	regd = rtw_chplan_get_default_regd(rfctl->ChannelPlan);
	if (regd == TXPWR_LMT_NONE)
		rfctl->regd_name = regd_str(TXPWR_LMT_NONE);
	else if (regd == TXPWR_LMT_WW)
		rfctl->regd_name = regd_str(TXPWR_LMT_WW);
	else {
		ent = _rtw_txpwr_lmt_get_by_name(rfctl, regd_str(regd));
		if (ent)
			rfctl->regd_name = ent->regd_name;
	}

	RTW_PRINT("default mapping domain:0x%02x to%s regd_name:%s\n"
		, rfctl->ChannelPlan
		, rfctl->regd_name ? "" : " unknown"
		, regd_str(regd)
	);
	if (rfctl->regd_name)
		goto release_lock;

	switch (regd) {
	/*
	* To support older chips without new predefined regd:
	* - use FCC if IC or CHILE or MEXICO not found
	* - use ETSI if KCC or ACMA not found
	*/
	case TXPWR_LMT_IC:
	case TXPWR_LMT_KCC:
	case TXPWR_LMT_NCC:
	case TXPWR_LMT_ACMA:
	case TXPWR_LMT_CHILE:
	case TXPWR_LMT_MEXICO:
		if (regd == TXPWR_LMT_IC || regd == TXPWR_LMT_NCC || regd == TXPWR_LMT_CHILE || regd == TXPWR_LMT_MEXICO)
			regd = TXPWR_LMT_FCC;
		else if (regd == TXPWR_LMT_KCC || regd == TXPWR_LMT_ACMA)
			regd = TXPWR_LMT_ETSI;
		ent = _rtw_txpwr_lmt_get_by_name(rfctl, regd_str(regd));
		if (ent)
			rfctl->regd_name = ent->regd_name;
		RTW_PRINT("alternate regd_name:%s %s\n"
			, regd_str(regd)
			, rfctl->regd_name ? "is used" : "not found"
		);
		if (rfctl->regd_name)
			break;
		/* fall through */
	default:
		rfctl->regd_name = regd_str(TXPWR_LMT_WW);
		RTW_PRINT("assign %s for default case\n", regd_str(TXPWR_LMT_WW));
		break;
	};

release_lock:
	_exit_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);
}
#endif /* CONFIG_TXPWR_LIMIT */

int rtw_rfctl_init(_adapter *adapter)
{
	struct registry_priv *regsty = adapter_to_regsty(adapter);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	int ret;

	_rtw_mutex_init(&rfctl->offch_mutex);

#if CONFIG_TXPWR_LIMIT
	_rtw_mutex_init(&rfctl->txpwr_lmt_mutex);
	_rtw_init_listhead(&rfctl->reg_exc_list);
	_rtw_init_listhead(&rfctl->txpwr_lmt_list);
#endif

	rfctl->ch_sel_within_same_band = 1;

#ifdef CONFIG_DFS_MASTER
	rfctl->dfs_region_domain = regsty->dfs_region_domain;
	rfctl->cac_start_time = rfctl->cac_end_time = RTW_CAC_STOPPED;
	rtw_init_timer(&(rfctl->radar_detect_timer), adapter, rtw_dfs_rd_timer_hdl, rfctl);
#endif
#if CONFIG_DFS_SLAVE_WITH_RADAR_DETECT
	rfctl->dfs_slave_with_rd = 1;
#endif

	if (regsty->antenna_gain != UNSPECIFIED_MBM)
		rfctl->antenna_gain = regsty->antenna_gain;

	ret = op_class_pref_init(adapter);
	if (ret != _SUCCESS)
		op_class_pref_deinit(adapter);

	return ret;
}

void rtw_rfctl_deinit(_adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);

	_rtw_mutex_free(&rfctl->offch_mutex);

#if CONFIG_TXPWR_LIMIT
	rtw_regd_exc_list_free(rfctl);
	rtw_txpwr_lmt_list_free(rfctl);
	_rtw_mutex_free(&rfctl->txpwr_lmt_mutex);
#endif

#ifdef CONFIG_REGD_SRC_FROM_OS
	if (rfctl->regd_src == REGD_SRC_OS)
		rtw_mfree((void *)rfctl->country_ent, sizeof(struct country_chplan));
#endif

	op_class_pref_deinit(adapter);
}

void rtw_rfctl_chplan_init(_adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);

	rfctl->max_chan_nums = init_channel_set(adapter);
	op_class_pref_apply_regulatory(adapter, REG_CHANGE);
	init_channel_list(adapter, rfctl->channel_set, &rfctl->channel_list);
}

void rtw_rfctl_update_op_mode(struct rf_ctl_t *rfctl, u8 ifbmp_mod, u8 if_op)
{
	struct dvobj_priv *dvobj = rfctl_to_dvobj(rfctl);
	_adapter *iface;
	struct mlme_ext_priv *mlmeext;
	u8 op_class = 0;
	u8 op_ch = 0;
	s16 op_txpwr_max;
	u8 if_op_class[CONFIG_IFACE_NUMBER] = {0};
	u8 if_op_ch[CONFIG_IFACE_NUMBER] = {0};
	u8 ch, bw, offset;
	u8 u_ch = 0, u_bw, u_offset;
	bool notify = 0;
	int i;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (!iface)
			continue;
		mlmeext = &iface->mlmeextpriv;

		if (ifbmp_mod & BIT(i)) {
			if (!if_op)
				continue;
		} else if (!MLME_IS_ASOC(iface) || MLME_IS_OPCH_SW(iface))
			continue;

		ch = mlmeext->cur_channel;
		bw = mlmeext->cur_bwmode;
		offset = mlmeext->cur_ch_offset;
		if_op_class[i] = rtw_get_op_class_by_chbw(ch, bw, offset);
		if_op_ch[i] = if_op_class[i] ? ch : 0;

		if (!u_ch) {
			u_ch = ch;
			u_bw = bw;
			u_offset = offset;
		} else {
			rtw_warn_on(!rtw_is_chbw_grouped(u_ch, u_bw, u_offset, ch, bw, offset));
			rtw_sync_chbw(&ch, &bw, &offset, &u_ch, &u_bw, &u_offset);
		}
	}

	op_class = rtw_get_op_class_by_chbw(u_ch, u_bw, u_offset);
	op_ch = op_class ? u_ch : 0;
	op_txpwr_max = rtw_rfctl_get_oper_txpwr_max_mbm(rfctl, u_ch, u_bw, u_offset, ifbmp_mod, if_op, 1);

	if (op_class != rfctl->op_class
		|| op_ch != rfctl->op_ch
		|| op_txpwr_max != rfctl->op_txpwr_max
		|| _rtw_memcmp(if_op_class, rfctl->if_op_class, sizeof(u8) * CONFIG_IFACE_NUMBER) == _FALSE
		|| _rtw_memcmp(if_op_ch, rfctl->if_op_ch, sizeof(u8) * CONFIG_IFACE_NUMBER) == _FALSE)
		notify = 1;

	rfctl->op_class = op_class;
	rfctl->op_ch = op_ch;
	rfctl->op_txpwr_max = op_txpwr_max;
	_rtw_memcpy(rfctl->if_op_class, if_op_class, sizeof(u8) * CONFIG_IFACE_NUMBER);
	_rtw_memcpy(rfctl->if_op_ch, if_op_ch, sizeof(u8) * CONFIG_IFACE_NUMBER);

	if (0)
		RTW_INFO("radio: %u,%u,%u %d notify:%d\n", u_ch, u_bw, u_offset, op_txpwr_max, notify);
	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (!iface)
			continue;
		mlmeext = &iface->mlmeextpriv;

		if (ifbmp_mod & BIT(i)) {
			if (!if_op)
				continue;
		} else if (!MLME_IS_ASOC(iface))
			continue;
		if (0)
			RTW_INFO(ADPT_FMT": %u,%u,%u\n", ADPT_ARG(iface)
				, mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset);
	}

	if (notify)
		rtw_nlrtw_radio_opmode_notify(rfctl);

#ifdef CONFIG_PLATFORM_CMAP_INTFS
	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (!iface)
			continue;
		cmap_intfs_nl_bss_status_event(iface, 0);
	}
#endif
}

inline u8 rtw_rfctl_get_dfs_domain(struct rf_ctl_t *rfctl)
{
#ifdef CONFIG_DFS_MASTER
	return rfctl->dfs_region_domain;
#else
	return RTW_DFS_REGD_NONE;
#endif
}

inline u8 rtw_rfctl_dfs_domain_unknown(struct rf_ctl_t *rfctl)
{
#ifdef CONFIG_DFS_MASTER
	return rtw_rfctl_get_dfs_domain(rfctl) == RTW_DFS_REGD_NONE;
#else
	return 1;
#endif
}

#ifdef CONFIG_DFS_MASTER
/*
* called in rtw_dfs_rd_enable()
* assume the request channel coverage is DFS range
* base on the current status and the request channel coverage to check if need to reset complete CAC time
*/
bool rtw_is_cac_reset_needed(struct rf_ctl_t *rfctl, u8 ch, u8 bw, u8 offset)
{
	bool needed = _FALSE;
	u32 cur_hi, cur_lo, hi, lo;

	if (rfctl->radar_detected == 1) {
		needed = _TRUE;
		goto exit;
	}

	if (rfctl->radar_detect_ch == 0) {
		needed = _TRUE;
		goto exit;
	}

	if (rtw_chbw_to_freq_range(ch, bw, offset, &hi, &lo) == _FALSE) {
		RTW_ERR("request detection range ch:%u, bw:%u, offset:%u\n", ch, bw, offset);
		rtw_warn_on(1);
	}

	if (rtw_chbw_to_freq_range(rfctl->radar_detect_ch, rfctl->radar_detect_bw, rfctl->radar_detect_offset, &cur_hi, &cur_lo) == _FALSE) {
		RTW_ERR("cur detection range ch:%u, bw:%u, offset:%u\n", rfctl->radar_detect_ch, rfctl->radar_detect_bw, rfctl->radar_detect_offset);
		rtw_warn_on(1);
	}

	if (hi <= lo || cur_hi <= cur_lo) {
		RTW_ERR("hi:%u, lo:%u, cur_hi:%u, cur_lo:%u\n", hi, lo, cur_hi, cur_lo);
		rtw_warn_on(1);
	}

	if (rtw_is_range_a_in_b(hi, lo, cur_hi, cur_lo)) {
		/* request is in current detect range */
		goto exit;
	}

	/* check if request channel coverage has new range and the new range is in DFS range */
	if (!rtw_is_range_overlap(hi, lo, cur_hi, cur_lo)) {
		/* request has no overlap with current */
		needed = _TRUE;
	} else if (rtw_is_range_a_in_b(cur_hi, cur_lo, hi, lo)) {
		/* request is supper set of current */
		if ((hi != cur_hi && rtw_chset_is_dfs_range(rfctl->channel_set, hi, cur_hi))
			|| (lo != cur_lo && rtw_chset_is_dfs_range(rfctl->channel_set, cur_lo, lo)))
			needed = _TRUE;
	} else {
		/* request is not supper set of current, but has overlap */
		if ((lo < cur_lo && rtw_chset_is_dfs_range(rfctl->channel_set, cur_lo, lo))
			|| (hi > cur_hi && rtw_chset_is_dfs_range(rfctl->channel_set, hi, cur_hi)))
			needed = _TRUE;
	}

exit:
	return needed;
}

bool _rtw_rfctl_overlap_radar_detect_ch(struct rf_ctl_t *rfctl, u8 ch, u8 bw, u8 offset)
{
	bool ret = _FALSE;
	u32 hi = 0, lo = 0;
	u32 r_hi = 0, r_lo = 0;
	int i;

	if (rfctl->radar_detect_by_others)
		goto exit;

	if (rfctl->radar_detect_ch == 0)
		goto exit;

	if (rtw_chbw_to_freq_range(ch, bw, offset, &hi, &lo) == _FALSE) {
		rtw_warn_on(1);
		goto exit;
	}

	if (rtw_chbw_to_freq_range(rfctl->radar_detect_ch
			, rfctl->radar_detect_bw, rfctl->radar_detect_offset
			, &r_hi, &r_lo) == _FALSE) {
		rtw_warn_on(1);
		goto exit;
	}

	if (rtw_is_range_overlap(hi, lo, r_hi, r_lo))
		ret = _TRUE;

exit:
	return ret;
}

bool rtw_rfctl_overlap_radar_detect_ch(struct rf_ctl_t *rfctl)
{
	return _rtw_rfctl_overlap_radar_detect_ch(rfctl
				, rfctl_to_dvobj(rfctl)->oper_channel
				, rfctl_to_dvobj(rfctl)->oper_bwmode
				, rfctl_to_dvobj(rfctl)->oper_ch_offset);
}

bool rtw_rfctl_is_tx_blocked_by_ch_waiting(struct rf_ctl_t *rfctl)
{
	return rtw_rfctl_overlap_radar_detect_ch(rfctl) && IS_CH_WAITING(rfctl);
}

bool rtw_chset_is_chbw_non_ocp(RT_CHANNEL_INFO *ch_set, u8 ch, u8 bw, u8 offset)
{
	bool ret = _FALSE;
	u32 hi = 0, lo = 0;
	int i;

	if (rtw_chbw_to_freq_range(ch, bw, offset, &hi, &lo) == _FALSE)
		goto exit;

	for (i = 0; i < MAX_CHANNEL_NUM && ch_set[i].ChannelNum != 0; i++) {
		if (!rtw_ch2freq(ch_set[i].ChannelNum)) {
			rtw_warn_on(1);
			continue;
		}

		if (!CH_IS_NON_OCP(&ch_set[i]))
			continue;

		if (lo <= rtw_ch2freq(ch_set[i].ChannelNum)
			&& rtw_ch2freq(ch_set[i].ChannelNum) <= hi
		) {
			ret = _TRUE;
			break;
		}
	}

exit:
	return ret;
}

bool rtw_chset_is_ch_non_ocp(RT_CHANNEL_INFO *ch_set, u8 ch)
{
	return rtw_chset_is_chbw_non_ocp(ch_set, ch, CHANNEL_WIDTH_20, HAL_PRIME_CHNL_OFFSET_DONT_CARE);
}

u32 rtw_chset_get_ch_non_ocp_ms(RT_CHANNEL_INFO *ch_set, u8 ch, u8 bw, u8 offset)
{
	int ms = 0;
	systime current_time;
	u32 hi = 0, lo = 0;
	int i;

	if (rtw_chbw_to_freq_range(ch, bw, offset, &hi, &lo) == _FALSE)
		goto exit;

	current_time = rtw_get_current_time();

	for (i = 0; i < MAX_CHANNEL_NUM && ch_set[i].ChannelNum != 0; i++) {
		if (!rtw_ch2freq(ch_set[i].ChannelNum)) {
			rtw_warn_on(1);
			continue;
		}

		if (!CH_IS_NON_OCP(&ch_set[i]))
			continue;

		if (lo <= rtw_ch2freq(ch_set[i].ChannelNum)
			&& rtw_ch2freq(ch_set[i].ChannelNum) <= hi
		) {
			if (rtw_systime_to_ms(ch_set[i].non_ocp_end_time - current_time) > ms)
				ms = rtw_systime_to_ms(ch_set[i].non_ocp_end_time - current_time);
		}
	}

exit:
	return ms;
}

/**
 * rtw_chset_update_non_ocp - update non_ocp_end_time according to the given @ch, @bw, @offset into @ch_set
 * @ch_set: the given channel set
 * @ch: channel number on which radar is detected
 * @bw: bandwidth on which radar is detected
 * @offset: bandwidth offset on which radar is detected
 * @ms: ms to add from now to update non_ocp_end_time, ms < 0 means use NON_OCP_TIME_MS
 */
static bool _rtw_chset_update_non_ocp(RT_CHANNEL_INFO *ch_set, u8 ch, u8 bw, u8 offset, int ms)
{
	u32 hi = 0, lo = 0;
	int i;
	bool updated = 0;

	if (rtw_chbw_to_freq_range(ch, bw, offset, &hi, &lo) == _FALSE)
		goto exit;

	for (i = 0; i < MAX_CHANNEL_NUM && ch_set[i].ChannelNum != 0; i++) {
		if (!rtw_ch2freq(ch_set[i].ChannelNum)) {
			rtw_warn_on(1);
			continue;
		}

		if (lo <= rtw_ch2freq(ch_set[i].ChannelNum)
			&& rtw_ch2freq(ch_set[i].ChannelNum) <= hi
		) {
			if (ms >= 0)
				ch_set[i].non_ocp_end_time = rtw_get_current_time() + rtw_ms_to_systime(ms);
			else
				ch_set[i].non_ocp_end_time = rtw_get_current_time() + rtw_ms_to_systime(NON_OCP_TIME_MS);
			ch_set[i].flags |= RTW_CHF_NON_OCP;
			updated = 1;
		}
	}

exit:
	return updated;
}

inline bool rtw_chset_update_non_ocp(RT_CHANNEL_INFO *ch_set, u8 ch, u8 bw, u8 offset)
{
	return _rtw_chset_update_non_ocp(ch_set, ch, bw, offset, -1);
}

inline bool rtw_chset_update_non_ocp_ms(RT_CHANNEL_INFO *ch_set, u8 ch, u8 bw, u8 offset, int ms)
{
	return _rtw_chset_update_non_ocp(ch_set, ch, bw, offset, ms);
}

static bool rtw_chset_chk_non_ocp_finish_for_chbw(struct rf_ctl_t *rfctl, u8 ch, u8 bw, u8 offset)
{
	RT_CHANNEL_INFO *ch_set = rfctl->channel_set;
	u8 cch;
	u8 *op_chs;
	u8 op_ch_num;
	int i;
	int ch_idx;
	bool ret = 0;

	cch = rtw_get_center_ch(ch, bw, offset);

	if (!rtw_get_op_chs_by_cch_bw(cch, bw, &op_chs, &op_ch_num))
		goto exit;

	for (i = 0; i < op_ch_num; i++) {
		if (0)
			RTW_INFO("%u,%u,%u - cch:%u, bw:%u, op_ch:%u\n", ch, bw, offset, cch, bw, *(op_chs + i));
		ch_idx = rtw_chset_search_ch(ch_set, *(op_chs + i));
		if (ch_idx == -1)
			break;
		if (!(ch_set[ch_idx].flags & RTW_CHF_NON_OCP) || CH_IS_NON_OCP(&ch_set[ch_idx]))
			break;
	}

	if (op_ch_num != 0 && i == op_ch_num) {
		ret = 1;
		/* clear RTTW_CHF_NON_OCP flag */
		for (i = 0; i < op_ch_num; i++) {
			ch_idx = rtw_chset_search_ch(ch_set, *(op_chs + i));
			ch_set[ch_idx].flags &= ~RTW_CHF_NON_OCP;
		}
		rtw_nlrtw_nop_finish_event(dvobj_get_primary_adapter(rfctl_to_dvobj(rfctl)), cch, bw);
	}

exit:
	return ret;
}

/* called by watchdog to clear RTW_CHF_NON_OCP and generate NON_OCP finish event */
void rtw_chset_chk_non_ocp_finish(struct rf_ctl_t *rfctl)
{
	u8 ch, bw, offset;
	int i;

	bw = CHANNEL_WIDTH_160;
	while (1) {
		for (i = 0; i < rfctl->max_chan_nums; i++) {
			ch = rfctl->channel_set[i].ChannelNum;
			if (!(rfctl->channel_set[i].flags & RTW_CHF_NON_OCP))
				continue;
			if (!rtw_get_offset_by_chbw(ch, bw, &offset))
				continue;

			rtw_chset_chk_non_ocp_finish_for_chbw(rfctl, ch, bw, offset);
		}
		if (bw-- == CHANNEL_WIDTH_20)
			break;
	}
}

u32 rtw_get_ch_waiting_ms(struct rf_ctl_t *rfctl, u8 ch, u8 bw, u8 offset, u32 *r_non_ocp_ms, u32 *r_cac_ms)
{
	struct dvobj_priv *dvobj = rfctl_to_dvobj(rfctl);
	u32 non_ocp_ms;
	u32 cac_ms;
	u8 in_rd_range = 0; /* if in current radar detection range*/

	if (rtw_chset_is_chbw_non_ocp(rfctl->channel_set, ch, bw, offset))
		non_ocp_ms = rtw_chset_get_ch_non_ocp_ms(rfctl->channel_set, ch, bw, offset);
	else
		non_ocp_ms = 0;

	if (rfctl->radar_detect_enabled) {
		u32 cur_hi, cur_lo, hi, lo;

		if (rtw_chbw_to_freq_range(ch, bw, offset, &hi, &lo) == _FALSE) {
			RTW_ERR("input range ch:%u, bw:%u, offset:%u\n", ch, bw, offset);
			rtw_warn_on(1);
		}

		if (rtw_chbw_to_freq_range(rfctl->radar_detect_ch, rfctl->radar_detect_bw, rfctl->radar_detect_offset, &cur_hi, &cur_lo) == _FALSE) {
			RTW_ERR("cur detection range ch:%u, bw:%u, offset:%u\n", rfctl->radar_detect_ch, rfctl->radar_detect_bw, rfctl->radar_detect_offset);
			rtw_warn_on(1);
		}

		if (rtw_is_range_a_in_b(hi, lo, cur_hi, cur_lo))
			in_rd_range = 1;
	}

	if (!rtw_chset_is_dfs_chbw(rfctl->channel_set, ch, bw, offset))
		cac_ms = 0;
	else if (in_rd_range && !non_ocp_ms) {
		if (IS_CH_WAITING(rfctl))
			cac_ms = rtw_systime_to_ms(rfctl->cac_end_time - rtw_get_current_time());
		else
			cac_ms = 0;
	} else if (rtw_is_long_cac_ch(ch, bw, offset, rtw_rfctl_get_dfs_domain(rfctl)))
		cac_ms = CAC_TIME_CE_MS;
	else
		cac_ms = CAC_TIME_MS;

	if (r_non_ocp_ms)
		*r_non_ocp_ms = non_ocp_ms;
	if (r_cac_ms)
		*r_cac_ms = cac_ms;

	return non_ocp_ms + cac_ms;
}

void rtw_reset_cac(struct rf_ctl_t *rfctl, u8 ch, u8 bw, u8 offset)
{
	u32 non_ocp_ms;
	u32 cac_ms;

	rtw_get_ch_waiting_ms(rfctl
		, ch
		, bw
		, offset
		, &non_ocp_ms
		, &cac_ms
	);

	rfctl->cac_start_time = rtw_get_current_time() + rtw_ms_to_systime(non_ocp_ms);
	rfctl->cac_end_time = rfctl->cac_start_time + rtw_ms_to_systime(cac_ms);

	/* skip special value */
	if (rfctl->cac_start_time == RTW_CAC_STOPPED) {
		rfctl->cac_start_time++;
		rfctl->cac_end_time++;
	}
	if (rfctl->cac_end_time == RTW_CAC_STOPPED)
		rfctl->cac_end_time++;
}

u32 rtw_force_stop_cac(struct rf_ctl_t *rfctl, u32 timeout_ms)
{
	struct dvobj_priv *dvobj = rfctl_to_dvobj(rfctl);
	systime start;
	u32 pass_ms;

	start = rtw_get_current_time();

	rfctl->cac_force_stop = 1;

	while (rtw_get_passing_time_ms(start) <= timeout_ms
		&& IS_UNDER_CAC(rfctl)
	) {
		if (dev_is_surprise_removed(dvobj) || dev_is_drv_stopped(dvobj))
			break;
		rtw_msleep_os(20);
	}

	if (IS_UNDER_CAC(rfctl)) {
		if (!dev_is_surprise_removed(dvobj) && !dev_is_drv_stopped(dvobj))
			RTW_INFO("%s waiting for cac stop timeout!\n", __func__);
	}

	rfctl->cac_force_stop = 0;

	pass_ms = rtw_get_passing_time_ms(start);

	return pass_ms;
}
#endif /* CONFIG_DFS_MASTER */

/* choose channel with shortest waiting (non ocp + cac) time */
bool rtw_choose_shortest_waiting_ch(struct rf_ctl_t *rfctl, u8 sel_ch, u8 max_bw
	, u8 *dec_ch, u8 *dec_bw, u8 *dec_offset
	, u8 e_flags, u8 d_flags, u8 cur_ch, bool by_int_info, u8 mesh_only)
{
#ifndef DBG_CHOOSE_SHORTEST_WAITING_CH
#define DBG_CHOOSE_SHORTEST_WAITING_CH 0
#endif
	struct dvobj_priv *dvobj = rfctl_to_dvobj(rfctl);
#ifdef CONFIG_RTW_ACS
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(dvobj_get_primary_adapter(dvobj));
#endif
	struct registry_priv *regsty = dvobj_to_regsty(dvobj);
	u8 ch, bw, offset;
	u8 ch_c = 0, bw_c = 0, offset_c = 0;
	int i;
	u32 min_waiting_ms = 0;
	u16 int_factor_c = 0;

	if (!dec_ch || !dec_bw || !dec_offset) {
		rtw_warn_on(1);
		return _FALSE;
	}

	RTW_INFO("%s: sel_ch:%u max_bw:%u e_flags:0x%02x d_flags:0x%02x cur_ch:%u within_sb:%d%s%s\n"
		, __func__, sel_ch, max_bw, e_flags, d_flags, cur_ch, rfctl->ch_sel_within_same_band
		, by_int_info ? " int" : "", mesh_only ? " mesh_only" : "");

	/* full search and narrow bw judegement first to avoid potetial judegement timing issue */
	for (bw = CHANNEL_WIDTH_20; bw <= max_bw; bw++) {
		if (!hal_is_bw_support(dvobj_get_primary_adapter(dvobj), bw))
			continue;

		for (i = 0; i < rfctl->max_chan_nums; i++) {
			u32 non_ocp_ms = 0;
			u32 cac_ms = 0;
			u32 waiting_ms = 0;
			u16 int_factor = 0;
			bool dfs_ch;
			bool non_ocp;
			bool long_cac;

			ch = rfctl->channel_set[i].ChannelNum;
			if (sel_ch) {
				if (ch != sel_ch)
					continue;
			} else if (rfctl->ch_sel_within_same_band && !rtw_is_same_band(cur_ch, ch))
				continue;

			if (ch > 14) {
				if (bw > REGSTY_BW_5G(regsty))
					continue;
			} else {
				if (bw > REGSTY_BW_2G(regsty))
					continue;
			}

			if (mesh_only && ch >= 5 && ch <= 9 && bw > CHANNEL_WIDTH_20)
				continue;

			if (!rtw_get_offset_by_chbw(ch, bw, &offset))
				continue;

			if (!rtw_chset_is_chbw_valid(rfctl->channel_set, ch, bw, offset, 0, 0))
				continue;

			if ((e_flags & RTW_CHF_DFS) || (d_flags & RTW_CHF_DFS)) {
				dfs_ch = rtw_chset_is_dfs_chbw(rfctl->channel_set, ch, bw, offset);
				if (((e_flags & RTW_CHF_DFS) && !dfs_ch)
					|| ((d_flags & RTW_CHF_DFS) && dfs_ch))
					continue;
			}

			if ((e_flags & RTW_CHF_LONG_CAC) || (d_flags & RTW_CHF_LONG_CAC)) {
				long_cac = rtw_is_long_cac_ch(ch, bw, offset, rtw_rfctl_get_dfs_domain(rfctl));
				if (((e_flags & RTW_CHF_LONG_CAC) && !long_cac)
					|| ((d_flags & RTW_CHF_LONG_CAC) && long_cac))
					continue;
			}

			if ((e_flags & RTW_CHF_NON_OCP) || (d_flags & RTW_CHF_NON_OCP)) {
				non_ocp = rtw_chset_is_chbw_non_ocp(rfctl->channel_set, ch, bw, offset);
				if (((e_flags & RTW_CHF_NON_OCP) && !non_ocp)
					|| ((d_flags & RTW_CHF_NON_OCP) && non_ocp))
					continue;
			}

			#ifdef CONFIG_DFS_MASTER
			waiting_ms = rtw_get_ch_waiting_ms(rfctl, ch, bw, offset, &non_ocp_ms, &cac_ms);
			#endif

			#ifdef CONFIG_RTW_ACS
			if (by_int_info) {
				/* for now, consider only primary channel */
				int_factor = hal_data->acs.interference_time[i];
			}
			#endif

			if (DBG_CHOOSE_SHORTEST_WAITING_CH)
				RTW_INFO("%s:%u,%u,%u %u(non_ocp:%u, cac:%u), int:%u\n"
					, __func__, ch, bw, offset, waiting_ms, non_ocp_ms, cac_ms, int_factor);

			if (ch_c == 0
				/* first: smaller wating time */
				|| min_waiting_ms > waiting_ms
				/* then: less interference */
				|| (min_waiting_ms == waiting_ms && int_factor_c > int_factor)
				/* then: wider bw */
				|| (min_waiting_ms == waiting_ms && int_factor_c == int_factor && bw > bw_c)
				/* if all condition equal, same channel -> same band prefer */
				|| (min_waiting_ms == waiting_ms && int_factor_c == int_factor && bw == bw_c
					&& ((cur_ch != ch_c && cur_ch == ch)
						|| (!rtw_is_same_band(cur_ch, ch_c) && rtw_is_same_band(cur_ch, ch)))
					)
			) {
				ch_c = ch;
				bw_c = bw;
				offset_c = offset;
				min_waiting_ms = waiting_ms;
				int_factor_c = int_factor;
			}
		}
	}

	if (ch_c != 0) {
		RTW_INFO("%s: select %u,%u,%u waiting_ms:%u\n"
			, __func__, ch_c, bw_c, offset_c, min_waiting_ms);
		*dec_ch = ch_c;
		*dec_bw = bw_c;
		*dec_offset = offset_c;
		return _TRUE;
	} else {
		RTW_INFO("%s: not found\n", __func__);
		if (d_flags == 0)
			rtw_warn_on(1);
	}

	return _FALSE;
}

#ifdef CONFIG_PROC_DEBUG
#define RTW_CHF_FMT "%s%s%s%s%s%s"

#define RTW_CHF_ARG_NO_IR(flags)		(flags & RTW_CHF_NO_IR) ? " NO_IR" : ""
#define RTW_CHF_ARG_DFS(flags)			, (flags & RTW_CHF_DFS) ? " DFS" : ""
#define RTW_CHF_ARG_NO_HT40U(flags)		, (flags & RTW_CHF_NO_HT40U) ? " NO_40M+" : ""
#define RTW_CHF_ARG_NO_HT40L(flags)		, (flags & RTW_CHF_NO_HT40L) ? " NO_40M-" : ""
#define RTW_CHF_ARG_NO_80MHZ(flags)		, (flags & RTW_CHF_NO_80MHZ) ? " NO_80M" : ""
#define RTW_CHF_ARG_NO_160MHZ(flags)	, (flags & RTW_CHF_NO_160MHZ) ? " NO_160M" : ""

#define RTW_CHF_ARG(flags) \
	RTW_CHF_ARG_NO_IR(flags) \
	RTW_CHF_ARG_DFS(flags) \
	RTW_CHF_ARG_NO_HT40U(flags) \
	RTW_CHF_ARG_NO_HT40L(flags) \
	RTW_CHF_ARG_NO_80MHZ(flags) \
	RTW_CHF_ARG_NO_160MHZ(flags)

void dump_chset(void *sel, RT_CHANNEL_INFO *ch_set, u8 chset_num)
{
	char buf[8];
	u8 i;

	RTW_PRINT_SEL(sel, "%-3s %-4s %-4s flags\n", "ch", "freq", "nocp");

	for (i = 0; i < MAX_CHANNEL_NUM && i < chset_num && ch_set[i].ChannelNum != 0; i++) {
		#ifdef CONFIG_DFS_MASTER
		if ((ch_set[i].flags & RTW_CHF_DFS) && CH_IS_NON_OCP(&ch_set[i]))
			snprintf(buf, 8, "%d", rtw_systime_to_ms(ch_set[i].non_ocp_end_time - rtw_get_current_time()) / 1000);
		else
		#endif
			snprintf(buf, 8, "0");

		RTW_PRINT_SEL(sel, "%3u %4u %4s"RTW_CHF_FMT"\n"
			, ch_set[i].ChannelNum, rtw_ch2freq(ch_set[i].ChannelNum), buf
			, RTW_CHF_ARG(ch_set[i].flags)
		);
	}

	RTW_PRINT_SEL(sel, "total ch number:%d\n", i);
}

void dump_cur_chset(void *sel, struct rf_ctl_t *rfctl)
{
	struct dvobj_priv *dvobj = rfctl_to_dvobj(rfctl);
	struct registry_priv *regsty = dvobj_to_regsty(dvobj);
	struct get_chplan_resp *chplan;
	int i;

	if (rtw_get_chplan_cmd(dvobj_get_primary_adapter(dvobj), RTW_CMDF_WAIT_ACK, &chplan) == _FAIL)
		return;

	RTW_PRINT_SEL(sel, "regd_src:%s(%d)\n", regd_src_str(chplan->regd_src), chplan->regd_src);

	if (chplan->has_country)
		dump_country_chplan(sel, &chplan->country_ent);
	else
		RTW_PRINT_SEL(sel, "chplan:0x%02X\n", chplan->channel_plan);

#if CONFIG_TXPWR_LIMIT
	RTW_PRINT_SEL(sel, "PLS regd:%s\n", chplan->regd_name);
#endif

#ifdef CONFIG_DFS_MASTER
	RTW_PRINT_SEL(sel, "dfs_domain:%s(%u)\n", rtw_dfs_regd_str(chplan->dfs_domain), chplan->dfs_domain);
#endif

	for (i = 0; i < MAX_CHANNEL_NUM; i++)
		if (regsty->excl_chs[i] != 0)
			break;

	if (i < MAX_CHANNEL_NUM) {
		RTW_PRINT_SEL(sel, "excl_chs:");
		for (i = 0; i < MAX_CHANNEL_NUM; i++) {
			if (regsty->excl_chs[i] == 0)
				break;
			_RTW_PRINT_SEL(sel, "%u ", regsty->excl_chs[i]);
		}
		_RTW_PRINT_SEL(sel, "\n");
	}

	dump_chset(sel, chplan->chset, chplan->chset_num);

	rtw_vmfree(chplan, sizeof(struct get_chplan_resp) + sizeof(RT_CHANNEL_INFO) * chplan->chset_num);
}
#endif

/*
 * Search the @param ch in given @param ch_set
 * @ch_set: the given channel set
 * @ch: the given channel number
 *
 * return the index of channel_num in channel_set, -1 if not found
 */
int rtw_chset_search_ch(RT_CHANNEL_INFO *ch_set, const u32 ch)
{
	int i;

	if (ch == 0)
		return -1;

	for (i = 0; i < MAX_CHANNEL_NUM && ch_set[i].ChannelNum != 0; i++) {
		if (ch == ch_set[i].ChannelNum)
			return i;
	}

	return -1;
}

/*
 * Check if the @param ch, bw, offset is valid for the given @param ch_set
 * @ch_set: the given channel set
 * @ch: the given channel number
 * @bw: the given bandwidth
 * @offset: the given channel offset
 *
 * return valid (1) or not (0)
 */
u8 rtw_chset_is_chbw_valid(RT_CHANNEL_INFO *ch_set, u8 ch, u8 bw, u8 offset
	, bool allow_primary_passive, bool allow_passive)
{
	u8 cch;
	u8 *op_chs;
	u8 op_ch_num;
	u8 valid = 0;
	int i;
	int ch_idx;

	cch = rtw_get_center_ch(ch, bw, offset);

	if (!rtw_get_op_chs_by_cch_bw(cch, bw, &op_chs, &op_ch_num))
		goto exit;

	for (i = 0; i < op_ch_num; i++) {
		if (0)
			RTW_INFO("%u,%u,%u - cch:%u, bw:%u, op_ch:%u\n", ch, bw, offset, cch, bw, *(op_chs + i));
		ch_idx = rtw_chset_search_ch(ch_set, *(op_chs + i));
		if (ch_idx == -1)
			break;
		if (ch_set[ch_idx].flags & RTW_CHF_NO_IR) {
			if ((!allow_primary_passive && ch_set[ch_idx].ChannelNum == ch)
				|| (!allow_passive && ch_set[ch_idx].ChannelNum != ch))
			break;
		}
		if (bw >= CHANNEL_WIDTH_40) {
			if ((ch_set[ch_idx].flags & RTW_CHF_NO_HT40U) && i % 2 == 0)
				break;
			if ((ch_set[ch_idx].flags & RTW_CHF_NO_HT40L) && i % 2 == 1)
				break;
		}
		if (bw >= CHANNEL_WIDTH_80 && (ch_set[ch_idx].flags & RTW_CHF_NO_80MHZ))
			break;
		if (bw >= CHANNEL_WIDTH_160 && (ch_set[ch_idx].flags & RTW_CHF_NO_160MHZ))
			break;
	}

	if (op_ch_num != 0 && i == op_ch_num)
		valid = 1;

exit:
	return valid;
}

/**
 * rtw_chset_sync_chbw - obey g_ch, adjust g_bw, g_offset, bw, offset to fit in channel plan
 * @ch_set: channel plan to check
 * @req_ch: pointer of the request ch, may be modified further
 * @req_bw: pointer of the request bw, may be modified further
 * @req_offset: pointer of the request offset, may be modified further
 * @g_ch: pointer of the ongoing group ch
 * @g_bw: pointer of the ongoing group bw, may be modified further
 * @g_offset: pointer of the ongoing group offset, may be modified further
 * @allow_primary_passive: if allow passive primary ch when deciding chbw
 * @allow_passive: if allow passive ch (not primary) when deciding chbw
 */
void rtw_chset_sync_chbw(RT_CHANNEL_INFO *ch_set, u8 *req_ch, u8 *req_bw, u8 *req_offset
	, u8 *g_ch, u8 *g_bw, u8 *g_offset, bool allow_primary_passive, bool allow_passive)
{
	u8 r_ch, r_bw, r_offset;
	u8 u_ch, u_bw, u_offset;
	u8 cur_bw = *req_bw;

	while (1) {
		r_ch = *req_ch;
		r_bw = cur_bw;
		r_offset = *req_offset;
		u_ch = *g_ch;
		u_bw = *g_bw;
		u_offset = *g_offset;

		rtw_sync_chbw(&r_ch, &r_bw, &r_offset, &u_ch, &u_bw, &u_offset);

		if (rtw_chset_is_chbw_valid(ch_set, r_ch, r_bw, r_offset, allow_primary_passive, allow_passive))
			break;
		if (cur_bw == CHANNEL_WIDTH_20) {
			rtw_warn_on(1);
			break;
		}
		cur_bw--;
	};

	*req_ch = r_ch;
	*req_bw = r_bw;
	*req_offset = r_offset;
	*g_ch = u_ch;
	*g_bw = u_bw;
	*g_offset = u_offset;
}

/*
 * Check the @param ch is fit with setband setting of @param adapter
 * @adapter: the given adapter
 * @ch: the given channel number
 *
 * return _TRUE when check valid, _FALSE not valid
 */
bool rtw_mlme_band_check(_adapter *adapter, const u32 ch)
{
	if (adapter->setband == WIFI_FREQUENCY_BAND_AUTO /* 2.4G and 5G */
		|| (adapter->setband == WIFI_FREQUENCY_BAND_2GHZ && ch < 35) /* 2.4G only */
		|| (adapter->setband == WIFI_FREQUENCY_BAND_5GHZ && ch > 35) /* 5G only */
	)
		return _TRUE;
	return _FALSE;
}
inline void RTW_SET_SCAN_BAND_SKIP(_adapter *padapter, int skip_band)
{
	int bs = ATOMIC_READ(&padapter->bandskip);

	bs |= skip_band;
	ATOMIC_SET(&padapter->bandskip, bs);
}

inline void RTW_CLR_SCAN_BAND_SKIP(_adapter *padapter, int skip_band)
{
	int bs = ATOMIC_READ(&padapter->bandskip);

	bs &= ~(skip_band);
	ATOMIC_SET(&padapter->bandskip, bs);
}
inline int RTW_GET_SCAN_BAND_SKIP(_adapter *padapter)
{
	return ATOMIC_READ(&padapter->bandskip);
}

#define RTW_IS_SCAN_BAND_SKIP(padapter, skip_band) (ATOMIC_READ(&padapter->bandskip) & (skip_band))

bool rtw_mlme_ignore_chan(_adapter *adapter, const u32 ch)
{
	if (RTW_IS_SCAN_BAND_SKIP(adapter, BAND_24G) && ch < 35) /* SKIP 2.4G Band channel */
		return _TRUE;
	if (RTW_IS_SCAN_BAND_SKIP(adapter, BAND_5G)  && ch > 35) /* SKIP 5G Band channel */
		return _TRUE;

	return _FALSE;
}


/****************************************************************************

Following are the initialization functions for WiFi MLME

*****************************************************************************/

int init_hw_mlme_ext(_adapter *padapter)
{
	struct	mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	u8 rx_bar_enble = _TRUE;

	/*
	 * Sync driver status and hardware setting
	 */

	/* Modify to make sure first time change channel(band) would be done properly */
	pHalData->current_channel = 0;
	pHalData->current_channel_bw = CHANNEL_WIDTH_MAX;
	#if CONFIG_IEEE80211_BAND_5GHZ
	pHalData->current_band_type = BAND_MAX;
	#else
	pHalData->current_band_type = BAND_ON_2_4G;
	#endif

	/* set_opmode_cmd(padapter, infra_client_with_mlme); */ /* removed */
	rtw_hal_set_hwreg(padapter, HW_VAR_ENABLE_RX_BAR, &rx_bar_enble);
	set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	return _SUCCESS;
}

void init_mlme_default_rate_set(_adapter *padapter)
{
	struct	mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	unsigned	char end_set[1] = {0xff};
	u8	offset_datarate = 0;
	u8	offset_basicrate = 0;
#ifdef CONFIG_80211N_HT
	unsigned char	supported_mcs_set[16] = {0xff, 0xff, 0xff, 0xff, 0x00, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
#endif

	if (IsSupportedTxCCK(padapter->registrypriv.wireless_mode)) {

		unsigned char	datarate_b[B_MODE_RATE_NUM] ={_1M_RATE_, _2M_RATE_, _5M_RATE_, _11M_RATE_};
		_rtw_memcpy(pmlmeext->datarate, datarate_b, B_MODE_RATE_NUM);
		_rtw_memcpy(pmlmeext->basicrate, datarate_b, B_MODE_RATE_NUM);
		offset_datarate += B_MODE_RATE_NUM;
		offset_basicrate += B_MODE_RATE_NUM;
		RTW_INFO("%s: support CCK\n", __func__);
	}
	if(IsSupportedTxOFDM(padapter->registrypriv.wireless_mode)) {
		unsigned char	datarate_g[G_MODE_RATE_NUM] ={_6M_RATE_, _9M_RATE_, _12M_RATE_, _18M_RATE_,_24M_RATE_, _36M_RATE_, _48M_RATE_, _54M_RATE_};
		unsigned char	basicrate_g[G_MODE_BASIC_RATE_NUM] = {_6M_RATE_, _12M_RATE_, _24M_RATE_};
		_rtw_memcpy(pmlmeext->datarate + offset_datarate, datarate_g, G_MODE_RATE_NUM);
		_rtw_memcpy(pmlmeext->basicrate + offset_basicrate,basicrate_g, G_MODE_BASIC_RATE_NUM);
		offset_datarate += G_MODE_RATE_NUM;
		offset_basicrate += G_MODE_BASIC_RATE_NUM;
		RTW_INFO("%s: support OFDM\n", __func__);

	}
	_rtw_memcpy(pmlmeext->datarate + offset_datarate, end_set, 1);
	_rtw_memcpy(pmlmeext->basicrate + offset_basicrate, end_set, 1);

#ifdef CONFIG_80211N_HT
	if( padapter->registrypriv.ht_enable && is_supported_ht(padapter->registrypriv.wireless_mode))
		_rtw_memcpy(pmlmeext->default_supported_mcs_set, supported_mcs_set, sizeof(pmlmeext->default_supported_mcs_set));
#endif
}

static void init_mlme_ext_priv_value(_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	ATOMIC_SET(&pmlmeext->event_seq, 0);
	pmlmeext->mgnt_seq = 0;/* reset to zero when disconnect at client mode */
#ifdef CONFIG_IEEE80211W
	pmlmeext->sa_query_seq = 0;
#endif
	pmlmeext->cur_channel = padapter->registrypriv.channel;
	pmlmeext->cur_bwmode = CHANNEL_WIDTH_20;
	pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	pmlmeext->retry = 0;

	pmlmeext->cur_wireless_mode = padapter->registrypriv.wireless_mode;
	init_mlme_default_rate_set(padapter);

	if ((pmlmeext->cur_channel > 14) || ((padapter->registrypriv.wireless_mode & WIRELESS_11B) == 0))
		pmlmeext->tx_rate = IEEE80211_OFDM_RATE_6MB;
	else
		pmlmeext->tx_rate = IEEE80211_CCK_RATE_1MB;

	mlmeext_set_scan_state(pmlmeext, SCAN_DISABLE);
	pmlmeext->sitesurvey_res.channel_idx = 0;
	pmlmeext->sitesurvey_res.bss_cnt = 0;
	pmlmeext->sitesurvey_res.scan_ch_ms = SURVEY_TO;
	pmlmeext->sitesurvey_res.rx_ampdu_accept = RX_AMPDU_ACCEPT_INVALID;
	pmlmeext->sitesurvey_res.rx_ampdu_size = RX_AMPDU_SIZE_INVALID;
#ifdef CONFIG_SCAN_BACKOP
	mlmeext_assign_scan_backop_flags_sta(pmlmeext, /*SS_BACKOP_EN|*/SS_BACKOP_PS_ANNC | SS_BACKOP_TX_RESUME);
	#ifdef CONFIG_AP_MODE
		#ifdef CONFIG_CUSTOMER_EZVIZ_CHIME2
			mlmeext_assign_scan_backop_flags_ap(pmlmeext, SS_BACKOP_EN | SS_BACKOP_PS_ANNC | SS_BACKOP_TX_RESUME | SS_BACKOP_EN_NL);
		#else
			mlmeext_assign_scan_backop_flags_ap(pmlmeext, SS_BACKOP_EN | SS_BACKOP_PS_ANNC | SS_BACKOP_TX_RESUME);
		#endif
	#endif
	#ifdef CONFIG_RTW_MESH
	mlmeext_assign_scan_backop_flags_mesh(pmlmeext, /*SS_BACKOP_EN | */SS_BACKOP_PS_ANNC | SS_BACKOP_TX_RESUME);
	#endif
	pmlmeext->sitesurvey_res.scan_cnt = 0;
	pmlmeext->sitesurvey_res.scan_cnt_max = RTW_SCAN_NUM_OF_CH;
	pmlmeext->sitesurvey_res.backop_ms = RTW_BACK_OP_CH_MS;
#endif
#if defined(CONFIG_ANTENNA_DIVERSITY) || defined(DBG_SCAN_SW_ANTDIV_BL)
	pmlmeext->sitesurvey_res.is_sw_antdiv_bl_scan = 0;
#endif
	pmlmeext->scan_abort = _FALSE;

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

	_rtw_memset(pmlmeinfo->chg_txt, 0, 128);

	pmlmeinfo->slotTime = SHORT_SLOT_TIME;
	pmlmeinfo->preamble_mode = PREAMBLE_AUTO;

	pmlmeinfo->dialogToken = 0;

	pmlmeext->action_public_rxseq = 0xffff;
	pmlmeext->action_public_dialog_token = 0xff;
#ifdef ROKU_PRIVATE
/*infra mode, used to store AP's info*/
	_rtw_memset(pmlmeinfo->SupportedRates_infra_ap, 0, NDIS_802_11_LENGTH_RATES_EX);
	pmlmeinfo->ht_vht_received = 0;
#endif /* ROKU_PRIVATE */
}

void init_mlme_ext_timer(_adapter *padapter)
{
	struct	mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	rtw_init_timer(&pmlmeext->survey_timer, padapter, survey_timer_hdl, padapter);
	rtw_init_timer(&pmlmeext->link_timer, padapter, link_timer_hdl, padapter);
#ifdef CONFIG_RTW_80211R
	rtw_init_timer(&pmlmeext->ft_link_timer, padapter, rtw_ft_link_timer_hdl, padapter);
	rtw_init_timer(&pmlmeext->ft_roam_timer, padapter, rtw_ft_roam_timer_hdl, padapter);
#endif

#ifdef CONFIG_RTW_REPEATER_SON
	rtw_init_timer(&pmlmeext->rson_scan_timer, padapter, rson_timer_hdl, padapter);
#endif
#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
	rtw_init_timer(&pmlmeext->tbtx_xmit_timer, padapter, rtw_tbtx_xmit_timer_hdl, padapter);
	rtw_init_timer(&pmlmeext->tbtx_token_dispatch_timer, padapter, rtw_tbtx_token_dispatch_timer_hdl, padapter);
#endif
#ifdef CONFIG_DFS
	rtw_init_timer(&pmlmeext->csa_timer, padapter->pnetdev, csa_timer_hdl, padapter);
#endif
}

int	init_mlme_ext_priv(_adapter *padapter)
{
	int	res = _SUCCESS;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	/* We don't need to memset padapter->XXX to zero, because adapter is allocated by rtw_zvmalloc(). */
	/* _rtw_memset((u8 *)pmlmeext, 0, sizeof(struct mlme_ext_priv)); */

	pmlmeext->padapter = padapter;

	/* fill_fwpriv(padapter, &(pmlmeext->fwpriv)); */

	init_mlme_ext_priv_value(padapter);
	pmlmeinfo->bAcceptAddbaReq = pregistrypriv->bAcceptAddbaReq;

	init_mlme_ext_timer(padapter);

#ifdef CONFIG_AP_MODE
	init_mlme_ap_info(padapter);
#endif

	pmlmeext->last_scan_time = 0;
	pmlmeext->mlmeext_init = _TRUE;


#ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
	pmlmeext->active_keep_alive_check = _TRUE;
#else
	pmlmeext->active_keep_alive_check = _FALSE;
#endif

#ifdef DBG_FIXED_CHAN
	pmlmeext->fixed_chan = 0xFF;
#endif

	pmlmeext->tsf_update_pause_factor = pregistrypriv->tsf_update_pause_factor;
	pmlmeext->tsf_update_restore_factor = pregistrypriv->tsf_update_restore_factor;

#ifdef CONFIG_SUPPORT_STATIC_SMPS
	pmlmeext->ssmps_en = _FALSE;
	pmlmeext->ssmps_tx_tp_th = SSMPS_TX_TP_TH;/*Mbps*/
	pmlmeext->ssmps_rx_tp_th = SSMPS_RX_TP_TH;/*Mbps*/
	#ifdef DBG_STATIC_SMPS
	pmlmeext->ssmps_test = _FALSE;
	#endif
#endif

#ifdef CONFIG_CTRL_TXSS_BY_TP
	pmlmeext->txss_ctrl_en = _TRUE;
	pmlmeext->txss_tp_th = TXSS_TP_TH;
	pmlmeext->txss_tp_chk_cnt = TXSS_TP_CHK_CNT;
#endif

	return res;

}

void free_mlme_ext_priv(struct mlme_ext_priv *pmlmeext)
{
	_adapter *padapter = pmlmeext->padapter;

	if (!padapter)
		return;

	if (rtw_is_drv_stopped(padapter)) {
		_cancel_timer_ex(&pmlmeext->survey_timer);
		_cancel_timer_ex(&pmlmeext->link_timer);
#ifdef CONFIG_DFS
		_cancel_timer_ex(&pmlmeext->csa_timer);
#endif /* CONFIG_DFS */
	}
}

#ifdef CONFIG_PATCH_JOIN_WRONG_CHANNEL
static u8 cmp_pkt_chnl_diff(_adapter *padapter, u8 *pframe, uint packet_len)
{
	/* if the channel is same, return 0. else return channel differential	 */
	uint len;
	u8 channel;
	u8 *p;

	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _BEACON_IE_OFFSET_, _DSSET_IE_, &len, packet_len - _BEACON_IE_OFFSET_);
	if (p) {
		channel = *(p + 2);
		if (padapter->mlmeextpriv.cur_channel >= channel)
			return padapter->mlmeextpriv.cur_channel - channel;
		else
			return channel - padapter->mlmeextpriv.cur_channel;
	} else
		return 0;
}
#endif /* CONFIG_PATCH_JOIN_WRONG_CHANNEL */

static void _mgt_dispatcher(_adapter *padapter, struct mlme_handler *ptable, union recv_frame *precv_frame)
{
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u8 *pframe = precv_frame->u.hdr.rx_data;

	if (ptable->func) {
		/* receive the frames that ra(a1) is my address or ra(a1) is bc address. */
		if (!_rtw_memcmp(GetAddr1Ptr(pframe), adapter_mac_addr(padapter), ETH_ALEN) &&
		    !_rtw_memcmp(GetAddr1Ptr(pframe), bc_addr, ETH_ALEN))
#ifdef CONFIG_RTW_CFGVENDOR_RANDOM_MAC_OUI
		{
			struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);

			if (check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE) != _TRUE)
				return;

		    if (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) == _TRUE)
				return;

		    if ( pwdev_priv->pno_mac_addr[0] == 0xFF)
				return;

		    if (!_rtw_memcmp(GetAddr1Ptr(pframe), adapter_pno_mac_addr(padapter), ETH_ALEN))
				return;
		}
#else
			return;
#endif

		ptable->func(padapter, precv_frame);
	}

}

void mgt_dispatcher(_adapter *padapter, union recv_frame *precv_frame)
{
	int index;
	struct mlme_handler *ptable;
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u8 *pframe = precv_frame->u.hdr.rx_data;
	struct sta_info *psta = rtw_get_stainfo(&padapter->stapriv, get_addr2_ptr(pframe));
	struct recv_priv  *precvpriv = &padapter->recvpriv;


#if 0
	{
		u8 *pbuf;
		pbuf = GetAddr1Ptr(pframe);
		RTW_INFO("A1-%x:%x:%x:%x:%x:%x\n", *pbuf, *(pbuf + 1), *(pbuf + 2), *(pbuf + 3), *(pbuf + 4), *(pbuf + 5));
		pbuf = get_addr2_ptr(pframe);
		RTW_INFO("A2-%x:%x:%x:%x:%x:%x\n", *pbuf, *(pbuf + 1), *(pbuf + 2), *(pbuf + 3), *(pbuf + 4), *(pbuf + 5));
		pbuf = GetAddr3Ptr(pframe);
		RTW_INFO("A3-%x:%x:%x:%x:%x:%x\n", *pbuf, *(pbuf + 1), *(pbuf + 2), *(pbuf + 3), *(pbuf + 4), *(pbuf + 5));
	}
#endif

	if (GetFrameType(pframe) != WIFI_MGT_TYPE) {
		return;
	}

	/* receive the frames that ra(a1) is my address or ra(a1) is bc address. */
	if (!_rtw_memcmp(GetAddr1Ptr(pframe), adapter_mac_addr(padapter), ETH_ALEN) &&
	    !_rtw_memcmp(GetAddr1Ptr(pframe), bc_addr, ETH_ALEN))
#ifdef CONFIG_RTW_CFGVENDOR_RANDOM_MAC_OUI
		{
			struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);

			if (check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE) != _TRUE)
				return;

			if (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) == _TRUE)
				return;

			if ( pwdev_priv->pno_mac_addr[0] == 0xFF)
				return;

			if (!_rtw_memcmp(GetAddr1Ptr(pframe), adapter_pno_mac_addr(padapter), ETH_ALEN))
				return;
		}
#else
		return;
#endif

	ptable = mlme_sta_tbl;

	index = get_frame_sub_type(pframe) >> 4;

#ifdef CONFIG_TDLS
	if ((index << 4) == WIFI_ACTION) {
		/* category==public (4), action==TDLS_DISCOVERY_RESPONSE */
		if (*(pframe + 24) == RTW_WLAN_CATEGORY_PUBLIC && *(pframe + 25) == TDLS_DISCOVERY_RESPONSE) {
			RTW_INFO("[TDLS] Recv %s from "MAC_FMT"\n", rtw_tdls_action_txt(TDLS_DISCOVERY_RESPONSE), MAC_ARG(get_addr2_ptr(pframe)));
			On_TDLS_Dis_Rsp(padapter, precv_frame);
		}
	}
#endif /* CONFIG_TDLS */

	if (index >= (sizeof(mlme_sta_tbl) / sizeof(struct mlme_handler))) {
		return;
	}
	ptable += index;

#if 1
	if (psta != NULL) {
		if (GetRetry(pframe)) {
			if (precv_frame->u.hdr.attrib.seq_num == psta->RxMgmtFrameSeqNum) {
				/* drop the duplicate management frame */
				precvpriv->dbg_rx_dup_mgt_frame_drop_count++;
				RTW_INFO("Drop duplicate management frame with seq_num = %d.\n", precv_frame->u.hdr.attrib.seq_num);
				return;
			}
		}
		psta->RxMgmtFrameSeqNum = precv_frame->u.hdr.attrib.seq_num;
	}
#else

	if (GetRetry(pframe)) {
		/* return; */
	}
#endif

#ifdef CONFIG_AP_MODE
	switch (get_frame_sub_type(pframe)) {
	case WIFI_AUTH:
		if (MLME_IS_AP(padapter) || MLME_IS_MESH(padapter))
			ptable->func = &OnAuth;
		else
			ptable->func = &OnAuthClient;
	/* fall through */
	case WIFI_ASSOCREQ:
	case WIFI_REASSOCREQ:
		_mgt_dispatcher(padapter, ptable, precv_frame);
		#ifdef CONFIG_HOSTAPD_MLME
		if (MLME_IS_AP(padapter))
			rtw_hostapd_mlme_rx(padapter, precv_frame);
		#endif
		break;
	case WIFI_PROBEREQ:
		_mgt_dispatcher(padapter, ptable, precv_frame);
		#ifdef CONFIG_HOSTAPD_MLME
		if (MLME_IS_AP(padapter))
			rtw_hostapd_mlme_rx(padapter, precv_frame);
		#endif
		break;
	case WIFI_BEACON:
		_mgt_dispatcher(padapter, ptable, precv_frame);
		break;
	case WIFI_ACTION:
		_mgt_dispatcher(padapter, ptable, precv_frame);
		break;
	default:
		_mgt_dispatcher(padapter, ptable, precv_frame);
		#ifdef CONFIG_HOSTAPD_MLME
		if (MLME_IS_AP(padapter))
			rtw_hostapd_mlme_rx(padapter, precv_frame);
		#endif
		break;
	}
#else

	_mgt_dispatcher(padapter, ptable, precv_frame);

#endif

}

#ifdef CONFIG_P2P
u32 p2p_listen_state_process(_adapter *padapter, unsigned char *da)
{
	bool response = _TRUE;

#ifdef CONFIG_IOCTL_CFG80211
	if (padapter->wdinfo.driver_interface == DRIVER_CFG80211) {
		if (rtw_cfg80211_get_is_roch(padapter) == _FALSE
			|| rtw_get_oper_ch(padapter) != padapter->wdinfo.listen_channel
			|| adapter_wdev_data(padapter)->p2p_enabled == _FALSE
			|| padapter->mlmepriv.wps_probe_resp_ie == NULL
			|| padapter->mlmepriv.p2p_probe_resp_ie == NULL
		) {
#ifdef CONFIG_DEBUG_CFG80211
			RTW_INFO(ADPT_FMT" DON'T issue_probersp_p2p: p2p_enabled:%d, wps_probe_resp_ie:%p, p2p_probe_resp_ie:%p\n"
				, ADPT_ARG(padapter)
				, adapter_wdev_data(padapter)->p2p_enabled
				, padapter->mlmepriv.wps_probe_resp_ie
				, padapter->mlmepriv.p2p_probe_resp_ie);
			RTW_INFO(ADPT_FMT" DON'T issue_probersp_p2p: is_ro_ch:%d, op_ch:%d, p2p_listen_channel:%d\n"
				, ADPT_ARG(padapter)
				, rtw_cfg80211_get_is_roch(padapter)
				, rtw_get_oper_ch(padapter)
				, padapter->wdinfo.listen_channel);
#endif
			response = _FALSE;
		}
	} else
#endif /* CONFIG_IOCTL_CFG80211 */
		if (padapter->wdinfo.driver_interface == DRIVER_WEXT) {
			/*	do nothing if the device name is empty */
			if (!padapter->wdinfo.device_name_len)
				response	= _FALSE;
		}

	if (response == _TRUE)
		issue_probersp_p2p(padapter, da);

	return _SUCCESS;
}
#endif /* CONFIG_P2P */


/****************************************************************************

Following are the callback functions for each subtype of the management frames

*****************************************************************************/

unsigned int OnProbeReq(_adapter *padapter, union recv_frame *precv_frame)
{
	unsigned int	ielen;
	unsigned char	*p;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX	*cur = &(pmlmeinfo->network);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;
	u8 is_valid_p2p_probereq = _FALSE;

#ifdef CONFIG_ATMEL_RC_PATCH
	u8 *target_ie = NULL, *wps_ie = NULL;
	u8 *start;
	uint search_len = 0, wps_ielen = 0, target_ielen = 0;
	struct sta_info	*psta;
	struct sta_priv *pstapriv = &padapter->stapriv;
#endif

#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	struct rx_pkt_attrib	*pattrib = &precv_frame->u.hdr.attrib;
	u8 wifi_test_chk_rate = 1;

#ifdef CONFIG_IOCTL_CFG80211
	if ((pwdinfo->driver_interface == DRIVER_CFG80211)
	    && !rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)
	    && (GET_CFG80211_REPORT_MGMT(adapter_wdev_data(padapter), IEEE80211_STYPE_PROBE_REQ) == _TRUE)
	) {
		rtw_cfg80211_rx_probe_request(padapter, precv_frame);
		return _SUCCESS;
	}
#endif /* CONFIG_IOCTL_CFG80211 */

	if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) &&
	    !rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE) &&
	    !rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT) &&
	    !rtw_p2p_chk_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH) &&
	    !rtw_p2p_chk_state(pwdinfo, P2P_STATE_SCAN)
	   ) {
		/*	Commented by Albert 2011/03/17 */
		/*	mcs_rate = 0->CCK 1M rate */
		/*	mcs_rate = 1->CCK 2M rate */
		/*	mcs_rate = 2->CCK 5.5M rate */
		/*	mcs_rate = 3->CCK 11M rate */
		/*	In the P2P mode, the driver should not support the CCK rate */

		/*	Commented by Kurt 2012/10/16 */
		/*	IOT issue: Google Nexus7 use 1M rate to send p2p_probe_req after GO nego completed and Nexus7 is client */
		if (padapter->registrypriv.wifi_spec == 1) {
			if (pattrib->data_rate <= DESC_RATE11M)
				wifi_test_chk_rate = 0;
		}

		if (wifi_test_chk_rate == 1) {
			is_valid_p2p_probereq = process_probe_req_p2p_ie(pwdinfo, pframe, len);
			if (is_valid_p2p_probereq == _TRUE) {
				if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE)) {
					/* FIXME */
					if (padapter->wdinfo.driver_interface == DRIVER_WEXT)
						report_survey_event(padapter, precv_frame);

					p2p_listen_state_process(padapter,  get_sa(pframe));

					return _SUCCESS;
				}

				if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
					goto _continue;
			}
		}
	}

_continue:
#endif /* CONFIG_P2P */

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
		return _SUCCESS;

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _FALSE &&
	    check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE | WIFI_AP_STATE | WIFI_MESH_STATE) == _FALSE)
		return _SUCCESS;


	/* RTW_INFO("+OnProbeReq\n"); */


#ifdef CONFIG_ATMEL_RC_PATCH
	wps_ie = rtw_get_wps_ie(
			      pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_,
			      len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_,
			      NULL, &wps_ielen);
	if (wps_ie)
		target_ie = rtw_get_wps_attr_content(wps_ie, wps_ielen, WPS_ATTR_MANUFACTURER, NULL, &target_ielen);
	if ((target_ie && (target_ielen == 4)) && (_TRUE == _rtw_memcmp((void *)target_ie, "Ozmo", 4))) {
		/* psta->flag_atmel_rc = 1; */
		unsigned char *sa_addr = get_sa(pframe);
		printk("%s: Find Ozmo RC -- %02x:%02x:%02x:%02x:%02x:%02x  \n\n",
		       __func__, *sa_addr, *(sa_addr + 1), *(sa_addr + 2), *(sa_addr + 3), *(sa_addr + 4), *(sa_addr + 5));
		_rtw_memcpy(pstapriv->atmel_rc_pattern, get_sa(pframe), ETH_ALEN);
	}
#endif


#ifdef CONFIG_AUTO_AP_MODE
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE &&
	    pmlmepriv->cur_network.join_res == _TRUE) {
		_irqL	irqL;
		struct sta_info	*psta;
		u8 *mac_addr, *peer_addr;
		struct sta_priv *pstapriv = &padapter->stapriv;
		u8 RC_OUI[4] = {0x00, 0xE0, 0x4C, 0x0A};
		/* EID[1] + EID_LEN[1] + RC_OUI[4] + MAC[6] + PairingID[2] + ChannelNum[2] */

		p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_, _VENDOR_SPECIFIC_IE_, (int *)&ielen,
			       len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_);

		if (!p || ielen != 14)
			goto _non_rc_device;

		if (!_rtw_memcmp(p + 2, RC_OUI, sizeof(RC_OUI)))
			goto _non_rc_device;

		if (!_rtw_memcmp(p + 6, get_sa(pframe), ETH_ALEN)) {
			RTW_INFO("%s, do rc pairing ("MAC_FMT"), but mac addr mismatch!("MAC_FMT")\n", __FUNCTION__,
				 MAC_ARG(get_sa(pframe)), MAC_ARG(p + 6));

			goto _non_rc_device;
		}

		RTW_INFO("%s, got the pairing device("MAC_FMT")\n", __FUNCTION__,  MAC_ARG(get_sa(pframe)));

		/* new a station */
		psta = rtw_get_stainfo(pstapriv, get_sa(pframe));
		if (psta == NULL) {
			/* allocate a new one */
			RTW_INFO("going to alloc stainfo for rc="MAC_FMT"\n",  MAC_ARG(get_sa(pframe)));
			psta = rtw_alloc_stainfo(pstapriv, get_sa(pframe));
			if (psta == NULL) {
				/* TODO: */
				RTW_INFO(" Exceed the upper limit of supported clients...\n");
				return _SUCCESS;
			}

			_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
			if (rtw_is_list_empty(&psta->asoc_list)) {
				psta->expire_to = pstapriv->expire_to;
				rtw_list_insert_tail(&psta->asoc_list, &pstapriv->asoc_list);
				pstapriv->asoc_list_cnt++;
				#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
				if (psta->tbtx_enable)
					pstapriv->tbtx_asoc_list_cnt++;
				#endif
			}
			_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

			/* generate pairing ID */
			mac_addr = adapter_mac_addr(padapter);
			peer_addr = psta->cmn.mac_addr;
			psta->pid = (u16)(((mac_addr[4] << 8) + mac_addr[5]) + ((peer_addr[4] << 8) + peer_addr[5]));

			/* update peer stainfo */
			psta->isrc = _TRUE;

			/* AID assignment */
			if (psta->cmn.aid > 0)
				RTW_INFO(FUNC_ADPT_FMT" old AID=%d\n", FUNC_ADPT_ARG(padapter), psta->cmn.aid);
			else {
				if (!rtw_aid_alloc(padapter, psta)) {
					RTW_INFO(FUNC_ADPT_FMT" no room for more AIDs\n", FUNC_ADPT_ARG(padapter));
					return _SUCCESS;
				}
				RTW_INFO(FUNC_ADPT_FMT" allocate new AID=%d\n", FUNC_ADPT_ARG(padapter), psta->cmn.aid);
			}

			psta->qos_option = 1;
			psta->cmn.bw_mode = CHANNEL_WIDTH_20;
			psta->ieee8021x_blocked = _FALSE;
#ifdef CONFIG_80211N_HT
			if(padapter->registrypriv.ht_enable &&
				is_supported_ht(padapter->registrypriv.wireless_mode)) {
				psta->htpriv.ht_option = _TRUE;
				psta->htpriv.ampdu_enable = _FALSE;
				psta->htpriv.sgi_20m = _FALSE;
				psta->htpriv.sgi_40m = _FALSE;
				psta->htpriv.ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
				psta->htpriv.agg_enable_bitmap = 0x0;/* reset */
				psta->htpriv.candidate_tid_bitmap = 0x0;/* reset */
			}
#endif

			rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, psta, _TRUE);

			_rtw_memset((void *)&psta->sta_stats, 0, sizeof(struct stainfo_stats));

			_enter_critical_bh(&psta->lock, &irqL);
			psta->state |= WIFI_ASOC_STATE;
			_exit_critical_bh(&psta->lock, &irqL);

			report_add_sta_event(padapter, psta->cmn.mac_addr);

		}

		issue_probersp(padapter, get_sa(pframe), _FALSE);

		return _SUCCESS;

	}

_non_rc_device:

	return _SUCCESS;

#endif /* CONFIG_AUTO_AP_MODE */


#ifdef CONFIG_CONCURRENT_MODE
	if (((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) &&
	    rtw_mi_buddy_check_fwstate(padapter, WIFI_UNDER_LINKING | WIFI_UNDER_SURVEY)) {
		/* don't process probe req */
		return _SUCCESS;
	}
#endif

	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_, _SSID_IE_, (int *)&ielen,
		       len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_);


	/* check (wildcard) SSID */
	if (p != NULL) {
		if (is_valid_p2p_probereq == _TRUE)
			goto _issue_probersp;

		if ((ielen != 0 && _FALSE == _rtw_memcmp((void *)(p + 2), (void *)cur->Ssid.Ssid, cur->Ssid.SsidLength))
			|| (ielen == 0 && pmlmeinfo->hidden_ssid_mode))
			goto exit;

		#ifdef CONFIG_RTW_MESH
		if (MLME_IS_MESH(padapter)) {
			p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_, WLAN_EID_MESH_ID, (int *)&ielen,
					len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_);

			if (!p)
				goto exit;
			if (ielen != 0 && _rtw_memcmp((void *)(p + 2), (void *)cur->mesh_id.Ssid, cur->mesh_id.SsidLength) == _FALSE)
				goto exit;
		}
		#endif

_issue_probersp:
		if (((check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE &&
		      pmlmepriv->cur_network.join_res == _TRUE)) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) {
			/* RTW_INFO("+issue_probersp during ap mode\n"); */
			issue_probersp(padapter, get_sa(pframe), is_valid_p2p_probereq);
		}

	}

exit:
	return _SUCCESS;

}

unsigned int OnProbeRsp(_adapter *padapter, union recv_frame *precv_frame)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	u8	*pframe = precv_frame->u.hdr.rx_data;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &padapter->wdinfo;
#endif


#ifdef CONFIG_P2P
	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_PROVISION_DIS_REQ)) {
		if (_TRUE == pwdinfo->tx_prov_disc_info.benable) {
			if (_rtw_memcmp(pwdinfo->tx_prov_disc_info.peerIFAddr, get_addr2_ptr(pframe), ETH_ALEN)) {
				if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT)) {
					pwdinfo->tx_prov_disc_info.benable = _FALSE;
					issue_p2p_provision_request(padapter,
						pwdinfo->tx_prov_disc_info.ssid.Ssid,
						pwdinfo->tx_prov_disc_info.ssid.SsidLength,
						pwdinfo->tx_prov_disc_info.peerDevAddr);
				} else if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE) || rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
					pwdinfo->tx_prov_disc_info.benable = _FALSE;
					issue_p2p_provision_request(padapter,
								    NULL,
								    0,
						pwdinfo->tx_prov_disc_info.peerDevAddr);
				}
			}
		}
		return _SUCCESS;
	} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING)) {
		if (_TRUE == pwdinfo->nego_req_info.benable) {
			RTW_INFO("[%s] P2P State is GONEGO ING!\n", __FUNCTION__);
			if (_rtw_memcmp(pwdinfo->nego_req_info.peerDevAddr, get_addr2_ptr(pframe), ETH_ALEN)) {
				pwdinfo->nego_req_info.benable = _FALSE;
				issue_p2p_GO_request(padapter, pwdinfo->nego_req_info.peerDevAddr);
			}
		}
	} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_INVITE_REQ)) {
		if (_TRUE == pwdinfo->invitereq_info.benable) {
			RTW_INFO("[%s] P2P_STATE_TX_INVITE_REQ!\n", __FUNCTION__);
			if (_rtw_memcmp(pwdinfo->invitereq_info.peer_macaddr, get_addr2_ptr(pframe), ETH_ALEN)) {
				pwdinfo->invitereq_info.benable = _FALSE;
				issue_p2p_invitation_request(padapter, pwdinfo->invitereq_info.peer_macaddr);
			}
		}
	}
#endif


	if ((mlmeext_chk_scan_state(pmlmeext, SCAN_PROCESS))
		|| (MLME_IS_MESH(padapter) && check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE))
		#ifdef CONFIG_RTW_REPEATER_SON
		|| (padapter->rtw_rson_scanstage == RSON_SCAN_PROCESS)
		#endif
	) {
		struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

		if (_rtw_memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN)
			&& (pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE && (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
		) {
			if (!rtw_check_bcn_info(padapter, pframe, precv_frame->u.hdr.len)) {
				RTW_PRINT(FUNC_ADPT_FMT" ap has changed, disconnect now\n", FUNC_ADPT_ARG(padapter));
				receive_disconnect(padapter, pmlmeinfo->network.MacAddress , 0, _FALSE);
			}
		}

		rtw_mi_report_survey_event(padapter, precv_frame);
		return _SUCCESS;
	}

#if 0 /* move to validate_recv_mgnt_frame */
	if (_rtw_memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN)) {
		if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) {
			psta = rtw_get_stainfo(pstapriv, get_addr2_ptr(pframe));
			if (psta != NULL)
				psta->sta_stats.rx_mgnt_pkts++;
		}
	}
#endif

	return _SUCCESS;

}

/* for 11n Logo 4.2.31/4.2.32 */
#ifdef CONFIG_AP_MODE
static void rtw_check_legacy_ap(_adapter *padapter, u8 *pframe, u32 len)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (!padapter->registrypriv.wifi_spec)
		return;

	if(!MLME_IS_AP(padapter))
		return;

	if (pmlmeext->bstart_bss == _TRUE) {
		int left;
		unsigned char *pos;
		struct rtw_ieee802_11_elems elems;
#ifdef CONFIG_80211N_HT
		u16 cur_op_mode; 
#endif
		/* checking IEs */
		left = len - sizeof(struct rtw_ieee80211_hdr_3addr) - _BEACON_IE_OFFSET_;
		pos = pframe + sizeof(struct rtw_ieee80211_hdr_3addr) + _BEACON_IE_OFFSET_;
		if (rtw_ieee802_11_parse_elems(pos, left, &elems, 1) == ParseFailed) {
			RTW_INFO("%s: parse fail for "MAC_FMT"\n", __func__, MAC_ARG(GetAddr3Ptr(pframe)));
			return;
		}
#ifdef CONFIG_80211N_HT
		cur_op_mode = pmlmepriv->ht_op_mode & HT_INFO_OPERATION_MODE_OP_MODE_MASK;
#endif
		/* for legacy ap */
		if (elems.ht_capabilities == NULL && elems.ht_capabilities_len == 0) {

			if (0)
				RTW_INFO("%s: "MAC_FMT" is legacy ap\n", __func__, MAC_ARG(GetAddr3Ptr(pframe)));

			ATOMIC_SET(&pmlmepriv->olbc, _TRUE);
			ATOMIC_SET(&pmlmepriv->olbc_ht, _TRUE);
		}
	}
}
#endif /* CONFIG_AP_MODE */

unsigned int OnBeacon(_adapter *padapter, union recv_frame *precv_frame)
{
	struct sta_info	*psta;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_priv	*pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;
	WLAN_BSSID_EX *pbss;
	int ret = _SUCCESS;
#ifdef CONFIG_TDLS
	struct sta_info *ptdls_sta;
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
#ifdef CONFIG_TDLS_CH_SW
	struct tdls_ch_switch *pchsw_info = &padapter->tdlsinfo.chsw_info;
#endif
#endif /* CONFIG_TDLS */

	if (validate_beacon_len(pframe, len) == _FALSE)
		return _SUCCESS;

	if (mlmeext_chk_scan_state(pmlmeext, SCAN_PROCESS)
		|| (MLME_IS_MESH(padapter) && check_fwstate(pmlmepriv, WIFI_ASOC_STATE))
	) {
		if (_rtw_memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN)
			&& (pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE && (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
		) {
			if (!rtw_check_bcn_info(padapter, pframe, len)) {
				RTW_PRINT(FUNC_ADPT_FMT" ap has changed, disconnect now\n", FUNC_ADPT_ARG(padapter));
				receive_disconnect(padapter, pmlmeinfo->network.MacAddress , 0, _FALSE);
			}
		}

		rtw_mi_report_survey_event(padapter, precv_frame);
		return _SUCCESS;
	}
#ifdef CONFIG_RTW_REPEATER_SON
	if (padapter->rtw_rson_scanstage == RSON_SCAN_PROCESS)
		rtw_mi_report_survey_event(padapter, precv_frame);
#endif

#ifdef CONFIG_AP_MODE
	rtw_check_legacy_ap(padapter, pframe, len);
#endif

	if (_rtw_memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN)) {
		if ((pmlmeinfo->state & WIFI_FW_AUTH_NULL)
			&& (rtw_sta_linking_test_wait_done() || pmlmeext->join_abort)
		) {
			if (rtw_sta_linking_test_force_fail() || pmlmeext->join_abort) {
				set_link_timer(pmlmeext, 1);
				return _SUCCESS;
			}

			/* we should update current network before auth, or some IE is wrong */
			pbss = (WLAN_BSSID_EX *)rtw_malloc(sizeof(WLAN_BSSID_EX));
			if (pbss) {
				if (collect_bss_info(padapter, precv_frame, pbss) == _SUCCESS) {
					struct beacon_keys recv_beacon;

					update_network(&(pmlmepriv->cur_network.network), pbss, padapter, _TRUE);

					/* update bcn keys */
					if (rtw_get_bcn_keys(padapter, pframe, len, &recv_beacon) == _TRUE) {
						RTW_INFO("%s: beacon keys ready\n", __func__);
						_rtw_memcpy(&pmlmepriv->cur_beacon_keys,
							&recv_beacon, sizeof(recv_beacon));
						if (is_hidden_ssid(recv_beacon.ssid, recv_beacon.ssid_len)) {
							_rtw_memcpy(pmlmepriv->cur_beacon_keys.ssid, pmlmeinfo->network.Ssid.Ssid, IW_ESSID_MAX_SIZE);
							pmlmepriv->cur_beacon_keys.ssid_len = pmlmeinfo->network.Ssid.SsidLength;
						}
					} else {
						RTW_ERR("%s: get beacon keys failed\n", __func__);
						_rtw_memset(&pmlmepriv->cur_beacon_keys, 0, sizeof(recv_beacon));
					}
					#ifdef CONFIG_BCN_CNT_CONFIRM_HDL
					pmlmepriv->new_beacon_cnts = 0;
					#endif
				}
				rtw_mfree((u8 *)pbss, sizeof(WLAN_BSSID_EX));
			}

			/* check the vendor of the assoc AP */
			pmlmeinfo->assoc_AP_vendor = check_assoc_AP(pframe + sizeof(struct rtw_ieee80211_hdr_3addr), len - sizeof(struct rtw_ieee80211_hdr_3addr));

			/* update TSF Value */
			update_TSF(pmlmeext, pframe, len);
			pmlmeext->bcn_cnt = 0;
			pmlmeext->last_bcn_cnt = 0;

#ifdef CONFIG_P2P_PS
			/* Comment by YiWei , in wifi p2p spec the "3.3 P2P Power Management" , "These mechanisms are available in a P2P Group in which only P2P Devices are associated." */
			/* process_p2p_ps_ie(padapter, (pframe + WLAN_HDR_A3_LEN), (len - WLAN_HDR_A3_LEN)); */
#endif /* CONFIG_P2P_PS */

#if defined(CONFIG_P2P) && defined(CONFIG_CONCURRENT_MODE)
			if (padapter->registrypriv.wifi_spec) {
				if (process_p2p_cross_connect_ie(padapter, (pframe + WLAN_HDR_A3_LEN), (len - WLAN_HDR_A3_LEN)) == _FALSE) {
					if (rtw_mi_buddy_check_mlmeinfo_state(padapter, WIFI_FW_AP_STATE)) {
						RTW_PRINT("no issue auth, P2P cross-connect does not permit\n ");
						return _SUCCESS;
					}
				}
			}
#endif /* CONFIG_P2P CONFIG_P2P and CONFIG_CONCURRENT_MODE */

			/* start auth */
			start_clnt_auth(padapter);

			return _SUCCESS;
		}

		if (((pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE) && (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)) {
			psta = rtw_get_stainfo(pstapriv, get_addr2_ptr(pframe));
			if (psta != NULL) {
#ifdef CONFIG_PATCH_JOIN_WRONG_CHANNEL
				/* Merge from 8712 FW code */
				if (cmp_pkt_chnl_diff(padapter, pframe, len) != 0) {
					/* join wrong channel, deauth and reconnect           */
					issue_deauth(padapter, (&(pmlmeinfo->network))->MacAddress, WLAN_REASON_DEAUTH_LEAVING);

					report_del_sta_event(padapter, (&(pmlmeinfo->network))->MacAddress, WLAN_REASON_JOIN_WRONG_CHANNEL, _TRUE, _FALSE);
					pmlmeinfo->state &= (~WIFI_FW_ASSOC_SUCCESS);
					return _SUCCESS;
				}
#endif /* CONFIG_PATCH_JOIN_WRONG_CHANNEL */
#ifdef CONFIG_RTW_80211R
				rtw_ft_update_bcn(padapter, precv_frame);
#endif
				ret = rtw_check_bcn_info(padapter, pframe, len);
				if (!ret) {
					RTW_PRINT(FUNC_ADPT_FMT" ap has changed, disconnect now\n", FUNC_ADPT_ARG(padapter));
					receive_disconnect(padapter, pmlmeinfo->network.MacAddress , 0, _FALSE);
					return _SUCCESS;
				}
				/* update WMM, ERP in the beacon */
				/* todo: the timer is used instead of the number of the beacon received */
				if ((sta_rx_pkts(psta) & 0xf) == 0) {
					/* RTW_INFO("update_bcn_info\n"); */
					update_beacon_info(padapter, pframe, len, psta);
				}

				pmlmepriv->cur_network_scanned->network.Rssi = precv_frame->u.hdr.attrib.phy_info.recv_signal_power;
				pmlmeext->bcn_cnt++;
#ifdef CONFIG_BCN_RECV_TIME
				rtw_rx_bcn_time_update(padapter, len, precv_frame->u.hdr.attrib.data_rate);
#endif
#ifdef CONFIG_TDLS
#ifdef CONFIG_TDLS_CH_SW
				if (rtw_tdls_is_chsw_allowed(padapter) == _TRUE) {
					/* Send TDLS Channel Switch Request when receiving Beacon */
					if ((padapter->tdlsinfo.chsw_info.ch_sw_state & TDLS_CH_SW_INITIATOR_STATE) && (ATOMIC_READ(&pchsw_info->chsw_on) == _TRUE)
					    && (pmlmeext->cur_channel == rtw_get_oper_ch(padapter))) {
						ptdls_sta = rtw_get_stainfo(&padapter->stapriv, padapter->tdlsinfo.chsw_info.addr);
						if (ptdls_sta != NULL) {
							if (ptdls_sta->tdls_sta_state | TDLS_LINKED_STATE)
								_set_timer(&ptdls_sta->stay_on_base_chnl_timer, TDLS_CH_SW_STAY_ON_BASE_CHNL_TIMEOUT);
						}
					}
				}
#endif
#endif /* CONFIG_TDLS */

				#if CONFIG_DFS
				process_csa_ie(padapter
					, pframe + WLAN_HDR_A3_LEN + _BEACON_IE_OFFSET_
					, len - (WLAN_HDR_A3_LEN + _BEACON_IE_OFFSET_));
				#endif

#ifdef CONFIG_P2P_PS
				process_p2p_ps_ie(padapter, (pframe + WLAN_HDR_A3_LEN), (len - WLAN_HDR_A3_LEN));
#endif /* CONFIG_P2P_PS */

				if (pmlmeext->tsf_update_required && pmlmeext->en_hw_update_tsf)
					rtw_enable_hw_update_tsf_cmd(padapter);

#if 0 /* move to validate_recv_mgnt_frame */
				psta->sta_stats.rx_mgnt_pkts++;
#endif
			}

		} else if ((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) {
			u8 rate_set[16];
			u8 rate_num = 0;

			psta = rtw_get_stainfo(pstapriv, get_addr2_ptr(pframe));
			if (psta != NULL) {
				/*
				* update WMM, ERP in the beacon
				* todo: the timer is used instead of the number of the beacon received
				*/
				if ((sta_rx_pkts(psta) & 0xf) == 0)
					update_beacon_info(padapter, pframe, len, psta);

				if (pmlmeext->tsf_update_required && pmlmeext->en_hw_update_tsf)
					rtw_enable_hw_update_tsf_cmd(padapter);
			} else {
				rtw_ies_get_supported_rate(pframe + WLAN_HDR_A3_LEN + _BEACON_IE_OFFSET_, len - WLAN_HDR_A3_LEN - _BEACON_IE_OFFSET_, rate_set, &rate_num);
				if (rate_num == 0) {
					RTW_INFO(FUNC_ADPT_FMT" RX beacon with no supported rate\n", FUNC_ADPT_ARG(padapter));
					goto _END_ONBEACON_;
				}

				psta = rtw_alloc_stainfo(pstapriv, get_addr2_ptr(pframe));
				if (psta == NULL) {
					RTW_INFO(FUNC_ADPT_FMT" Exceed the upper limit of supported clients\n", FUNC_ADPT_ARG(padapter));
					goto _END_ONBEACON_;
				}

				psta->expire_to = pstapriv->adhoc_expire_to;

				_rtw_memcpy(psta->bssrateset, rate_set, rate_num);
				psta->bssratelen = rate_num;

				/* update TSF Value */
				update_TSF(pmlmeext, pframe, len);

				/* report sta add event */
				report_add_sta_event(padapter, get_addr2_ptr(pframe));
			}
		}
	}

_END_ONBEACON_:

	return _SUCCESS;

}

#ifdef CONFIG_AP_MODE
static u32 rtw_get_sta_num_by_state(_adapter *padapter, u32 state)
{
	_irqL irqL;
	_list	*plist, *phead;
	u32	index, sta_num = 0;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &(padapter->stapriv);

	_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);	
	for (index = 0; index < NUM_STA; index++) {		
		phead = &(pstapriv->sta_hash[index]);
		plist = get_next(phead);
		while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
			psta = LIST_CONTAINOR(plist, struct sta_info , hash_list);
			if ((psta->state & (state)))
				sta_num++;
			plist = get_next(plist);
		}
	}
	_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);

	/* RTW_INFO("%s : waiting for %u sta under linking \n", __func__, sta_num); */
	return sta_num;
}

static u8  rtw_defs_attack_chk(_adapter *padapter)
{
	struct mlme_priv *mlme = &(padapter->mlmepriv);
	u8 is_reject = _FALSE;
	u32 sta_limit = 0;
	u32 stime = rtw_systime_to_ms(rtw_get_current_time());
	static u32 ptime = 0;

	/* RTW_INFO("%s : ptime=%u, stime=%u, diff=%u\n", __func__, ptime, stime, (stime - ptime)); */
	if ((ptime > 0) && ((stime - ptime) < mlme->defs_lmt_time)) {
		sta_limit = rtw_get_sta_num_by_state(padapter, WIFI_FW_LINKING_STATE);
		if (sta_limit >= mlme->defs_lmt_sta)
			is_reject = _TRUE;
	}

	ptime = stime;
	/* RTW_INFO("%s : current linking num=%u\n", __func__, sta_limit); */
	return is_reject;	
}
#endif

unsigned int OnAuth(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE
	_irqL irqL;
	unsigned int	auth_mode, seq, ie_len;
	unsigned char	*sa, *p;
	u16	algorithm;
	int	status;
	static struct sta_info stat;
	struct	sta_info	*pstat = NULL;
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;
	u8	offset = 0;


#ifdef CONFIG_CONCURRENT_MODE
	if (((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) &&
	    rtw_mi_buddy_check_fwstate(padapter, WIFI_UNDER_LINKING | WIFI_UNDER_SURVEY)) {
		/* don't process auth request; */
		return _SUCCESS;
	}
#endif /* CONFIG_CONCURRENT_MODE */

	if ((pmlmeinfo->state & 0x03) != WIFI_FW_AP_STATE)
		return _FAIL;

	if (!MLME_IS_ASOC(padapter))
		return _SUCCESS;

#if defined(CONFIG_IOCTL_CFG80211) && defined(CONFIG_RTW_MESH)
	if (MLME_IS_MESH(padapter))
		return rtw_mesh_on_auth(padapter, precv_frame);
#endif

	RTW_INFO("+OnAuth\n");

	sa = get_addr2_ptr(pframe);

	auth_mode = psecuritypriv->dot11AuthAlgrthm;

	if (GetPrivacy(pframe)) {
		u8	*iv;
		struct rx_pkt_attrib	*prxattrib = &(precv_frame->u.hdr.attrib);

		prxattrib->hdrlen = WLAN_HDR_A3_LEN;
		prxattrib->encrypt = _WEP40_;

		iv = pframe + prxattrib->hdrlen;
		prxattrib->key_index = ((iv[3] >> 6) & 0x3);

		prxattrib->iv_len = 4;
		prxattrib->icv_len = 4;

		rtw_wep_decrypt(padapter, (u8 *)precv_frame);

		offset = 4;
	}

	algorithm = le16_to_cpu(*(u16 *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset));
	seq	= le16_to_cpu(*(u16 *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset + 2));

	RTW_INFO("auth alg=%x, seq=%X\n", algorithm, seq);

	if (rtw_check_invalid_mac_address(sa, _FALSE)){
		RTW_INFO("%s : reject invalid AUTH-req "MAC_FMT"\n", 
			__func__, MAC_ARG(get_addr2_ptr(pframe)));
		return _FAIL;
	}

	if(rtw_defs_attack_chk(padapter))  {
		struct sta_info *_psta;
		_psta = rtw_get_stainfo(pstapriv, sa);
		if ((_psta == NULL) || !(_psta->state & WIFI_FW_ASSOC_SUCCESS)) {
			status = _STATS_REFUSED_TEMPORARILY_;
			RTW_ERR("%s : refused temporarily for sa "MAC_FMT" !\n", __func__, MAC_ARG(sa));
			goto auth_fail;
		}
	}

	if (rtw_ap_linking_test_force_auth_fail()) {
		status = rtw_ap_linking_test_force_auth_fail();
		RTW_INFO(FUNC_ADPT_FMT" force auth fail with status:%u\n"
			, FUNC_ADPT_ARG(padapter), status);
		goto auth_fail;
	}

	if ((auth_mode == 2) && (algorithm != WLAN_AUTH_SAE) &&
	    (psecuritypriv->dot11PrivacyAlgrthm != _WEP40_) &&
	    (psecuritypriv->dot11PrivacyAlgrthm != _WEP104_))
		auth_mode = 0;

	if ((algorithm > 0 && auth_mode == 0) ||	/* rx a shared-key auth but shared not enabled */
	    (algorithm == 0 && auth_mode == 1)) {	/* rx a open-system auth but shared-key is enabled */
		RTW_INFO("auth rejected due to bad alg [alg=%d, auth_mib=%d] %02X%02X%02X%02X%02X%02X\n",
			algorithm, auth_mode, sa[0], sa[1], sa[2], sa[3], sa[4], sa[5]);

		status = _STATS_NO_SUPP_ALG_;

		goto auth_fail;
	}

#if CONFIG_RTW_MACADDR_ACL
	if (rtw_access_ctrl(padapter, sa) == _FALSE) {
		status = _STATS_UNABLE_HANDLE_STA_;
		goto auth_fail;
	}
#endif

	pstat = rtw_get_stainfo(pstapriv, sa);
	if (pstat == NULL) {

		/* allocate a new one */
		RTW_INFO("going to alloc stainfo for sa="MAC_FMT"\n",  MAC_ARG(sa));
		pstat = rtw_alloc_stainfo(pstapriv, sa);
		if (pstat == NULL) {
			RTW_INFO(" Exceed the upper limit of supported clients...\n");
			status = _STATS_UNABLE_HANDLE_STA_;
			goto auth_fail;
		}

		pstat->state = WIFI_FW_AUTH_NULL;
		pstat->auth_seq = 0;

		/* pstat->flags = 0; */
		/* pstat->capability = 0; */
	} else {
#ifdef CONFIG_IEEE80211W
		if ((pstat->bpairwise_key_installed != _TRUE && (pstat->flags & WLAN_STA_MFP)) 
			|| !(pstat->flags & WLAN_STA_MFP))
#endif /* CONFIG_IEEE80211W */
		{

			_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
			if (rtw_is_list_empty(&pstat->asoc_list) == _FALSE) {
				rtw_list_delete(&pstat->asoc_list);
				pstapriv->asoc_list_cnt--;
				#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
				if (pstat->tbtx_enable)
					pstapriv->tbtx_asoc_list_cnt--;
				#endif
				if (pstat->expire_to > 0)
					;/* TODO: STA re_auth within expire_to */
			}
			_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

			if (seq == 1)
				; /* TODO: STA re_auth and auth timeout */

		}
	}

#ifdef CONFIG_IEEE80211W
	if ((pstat->bpairwise_key_installed != _TRUE && (pstat->flags & WLAN_STA_MFP)) 
		|| !(pstat->flags & WLAN_STA_MFP))
#endif /* CONFIG_IEEE80211W */
	{
		_enter_critical_bh(&pstapriv->auth_list_lock, &irqL);
		if (rtw_is_list_empty(&pstat->auth_list)) {

			rtw_list_insert_tail(&pstat->auth_list, &pstapriv->auth_list);
			pstapriv->auth_list_cnt++;
		}
		_exit_critical_bh(&pstapriv->auth_list_lock, &irqL);
	}

	if (pstat->auth_seq == 0)
		pstat->expire_to = pstapriv->auth_to;

#ifdef CONFIG_IOCTL_CFG80211
	if (GET_CFG80211_REPORT_MGMT(adapter_wdev_data(padapter), IEEE80211_STYPE_AUTH) == _TRUE) {
		if ((algorithm == WLAN_AUTH_SAE) &&
			(auth_mode == dot11AuthAlgrthm_8021X)) {
			pstat->authalg = algorithm;

			rtw_cfg80211_rx_mframe(padapter, precv_frame, NULL);
			return _SUCCESS;
		}
	}
#endif /* CONFIG_IOCTL_CFG80211 */

	if ((pstat->auth_seq + 1) != seq) {
		RTW_INFO("(1)auth rejected because out of seq [rx_seq=%d, exp_seq=%d]!\n",
			 seq, pstat->auth_seq + 1);
		status = _STATS_OUT_OF_AUTH_SEQ_;
		goto auth_fail;
	}

	if (algorithm == 0 && (auth_mode == 0 || auth_mode == 2 || auth_mode == 3)) {
		if (seq == 1) {
#ifdef CONFIG_IEEE80211W
			if ((pstat->bpairwise_key_installed != _TRUE && (pstat->flags & WLAN_STA_MFP)) 
				|| !(pstat->flags & WLAN_STA_MFP))
#endif /* CONFIG_IEEE80211W */
			{
				pstat->state &= ~WIFI_FW_AUTH_NULL;
				pstat->state |= WIFI_FW_AUTH_SUCCESS;
				pstat->expire_to = pstapriv->assoc_to;
			}
			pstat->authalg = algorithm;
		} else {
			RTW_INFO("(2)auth rejected because out of seq [rx_seq=%d, exp_seq=%d]!\n",
				 seq, pstat->auth_seq + 1);
			status = _STATS_OUT_OF_AUTH_SEQ_;
			goto auth_fail;
		}
	} else { /* shared system or auto authentication */
		if (seq == 1) {
			/* prepare for the challenging txt... */

			/* get_random_bytes((void *)pstat->chg_txt, 128); */ /* TODO: */
			_rtw_memset((void *)pstat->chg_txt, 78, 128);
#ifdef CONFIG_IEEE80211W
			if ((pstat->bpairwise_key_installed != _TRUE && (pstat->flags & WLAN_STA_MFP)) 
				|| !(pstat->flags & WLAN_STA_MFP))
#endif /* CONFIG_IEEE80211W */
			{
				pstat->state &= ~WIFI_FW_AUTH_NULL;
				pstat->state |= WIFI_FW_AUTH_STATE;
			}
			pstat->authalg = algorithm;
			pstat->auth_seq = 2;
		} else if (seq == 3) {
			/* checking for challenging txt... */
			RTW_INFO("checking for challenging txt...\n");

			p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + 4 + _AUTH_IE_OFFSET_ , _CHLGETXT_IE_, (int *)&ie_len,
				len - WLAN_HDR_A3_LEN - _AUTH_IE_OFFSET_ - 4);

			if ((p == NULL) || (ie_len <= 0)) {
				RTW_INFO("auth rejected because challenge failure!(1)\n");
				status = _STATS_CHALLENGE_FAIL_;
				goto auth_fail;
			}

			if (_rtw_memcmp((void *)(p + 2), pstat->chg_txt, 128)) {
#ifdef CONFIG_IEEE80211W
				if ((pstat->bpairwise_key_installed != _TRUE && (pstat->flags & WLAN_STA_MFP)) 
					|| !(pstat->flags & WLAN_STA_MFP))
#endif /* CONFIG_IEEE80211W */
				{
					pstat->state &= (~WIFI_FW_AUTH_STATE);
					pstat->state |= WIFI_FW_AUTH_SUCCESS;
					/* challenging txt is correct... */
					pstat->expire_to =  pstapriv->assoc_to;
				}
			} else {
				RTW_INFO("auth rejected because challenge failure!\n");
				status = _STATS_CHALLENGE_FAIL_;
				goto auth_fail;
			}
		} else {
			RTW_INFO("(3)auth rejected because out of seq [rx_seq=%d, exp_seq=%d]!\n",
				 seq, pstat->auth_seq + 1);
			status = _STATS_OUT_OF_AUTH_SEQ_;
			goto auth_fail;
		}
	}


	/* Now, we are going to issue_auth... */
	pstat->auth_seq = seq + 1;

#ifdef CONFIG_NATIVEAP_MLME
	issue_auth(padapter, pstat, (unsigned short)(_STATS_SUCCESSFUL_));
#endif

	if ((pstat->state & WIFI_FW_AUTH_SUCCESS) || (pstat->state & WIFI_FW_ASSOC_SUCCESS))
		pstat->auth_seq = 0;


	return _SUCCESS;

auth_fail:

	if (pstat)
		rtw_free_stainfo(padapter , pstat);

	pstat = &stat;
	_rtw_memset((char *)pstat, '\0', sizeof(stat));
	pstat->auth_seq = 2;
	_rtw_memcpy(pstat->cmn.mac_addr, sa, ETH_ALEN);

#ifdef CONFIG_NATIVEAP_MLME
	issue_auth(padapter, pstat, (unsigned short)status);
#endif

#endif /* CONFIG_AP_MODE */
	return _FAIL;

}

unsigned int OnAuthClient(_adapter *padapter, union recv_frame *precv_frame)
{
	unsigned int	seq, len, status, algthm, offset;
	unsigned char	*p;
	unsigned int	go2asoc = 0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint pkt_len = precv_frame->u.hdr.len;

	RTW_INFO("%s\n", __FUNCTION__);

#ifdef CONFIG_IOCTL_CFG80211
	if (GET_CFG80211_REPORT_MGMT(adapter_wdev_data(padapter), IEEE80211_STYPE_AUTH) == _TRUE) {
		if (rtw_sec_chk_auth_type(padapter, MLME_AUTHTYPE_SAE)) {
			if (rtw_cached_pmkid(padapter, get_my_bssid(&pmlmeinfo->network)) != -1) {
				RTW_INFO("SAE: PMKSA cache entry found\n");
				goto normal;
			}
			rtw_cfg80211_rx_mframe(padapter, precv_frame, NULL);
			return _SUCCESS;
		}
	}

normal:
#endif /* CONFIG_IOCTL_CFG80211 */

	/* check A1 matches or not */
	if (!_rtw_memcmp(adapter_mac_addr(padapter), get_da(pframe), ETH_ALEN))
		return _SUCCESS;

	if (!(pmlmeinfo->state & WIFI_FW_AUTH_STATE) || pmlmeext->join_abort)
		return _SUCCESS;

	offset = (GetPrivacy(pframe)) ? 4 : 0;

	algthm	= le16_to_cpu(*(unsigned short *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset));
	seq	= le16_to_cpu(*(unsigned short *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset + 2));
	status	= le16_to_cpu(*(unsigned short *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset + 4));

	if (status != 0) {
		RTW_INFO("clnt auth fail, status: %d\n", status);
		if (status == 13) { /* && pmlmeinfo->auth_algo == dot11AuthAlgrthm_Auto) */
			if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared)
				pmlmeinfo->auth_algo = dot11AuthAlgrthm_Open;
			else
				pmlmeinfo->auth_algo = dot11AuthAlgrthm_Shared;
			/* pmlmeinfo->reauth_count = 0; */
		}

		pmlmeinfo->auth_status = status;
		set_link_timer(pmlmeext, 1);
		goto authclnt_fail;
	}

	if (seq == 2) {
		if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared) {
			/* legendary shared system */
			p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _AUTH_IE_OFFSET_, _CHLGETXT_IE_, (int *)&len,
				pkt_len - WLAN_HDR_A3_LEN - _AUTH_IE_OFFSET_);

			if (p == NULL) {
				/* RTW_INFO("marc: no challenge text?\n"); */
				goto authclnt_fail;
			}

			_rtw_memcpy((void *)(pmlmeinfo->chg_txt), (void *)(p + 2), len);
			pmlmeinfo->auth_seq = 3;
			issue_auth(padapter, NULL, 0);
			set_link_timer(pmlmeext, REAUTH_TO);

			return _SUCCESS;
		} else {
			/* open, or 802.11r FTAA system */
			go2asoc = 1;
		}
	} else if (seq == 4) {
		if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared)
			go2asoc = 1;
		else
			goto authclnt_fail;
	} else {
		/* this is also illegal */
		/* RTW_INFO("marc: clnt auth failed due to illegal seq=%x\n", seq); */
		goto authclnt_fail;
	}

	if (go2asoc) {
#ifdef CONFIG_RTW_80211R
		if (rtw_ft_update_auth_rsp_ies(padapter, pframe, pkt_len))
			return _SUCCESS;
#endif
		RTW_PRINT("auth success, start assoc\n");
		start_clnt_assoc(padapter);
		return _SUCCESS;
	}

authclnt_fail:

	/* pmlmeinfo->state &= ~(WIFI_FW_AUTH_STATE); */

	return _FAIL;

}

unsigned int OnAssocReq(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE
	_irqL irqL;
	u16 listen_interval;
	struct rtw_ieee802_11_elems elems;
	struct sta_info	*pstat;
	unsigned char		reassoc, *pos;
	int		left;
	unsigned short		status = _STATS_SUCCESSFUL_;
	unsigned short		frame_type, ie_offset = 0;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX	*cur = &(pmlmeinfo->network);
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint pkt_len = precv_frame->u.hdr.len;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	u8 p2p_status_code = P2P_STATUS_SUCCESS;
	u8 *p2pie;
	u32 p2pielen = 0;
#endif /* CONFIG_P2P */
#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
	u8 sta_tbtx_enable = _FALSE;
#endif

#ifdef CONFIG_CONCURRENT_MODE
	if (((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) &&
	    rtw_mi_buddy_check_fwstate(padapter, WIFI_UNDER_LINKING | WIFI_UNDER_SURVEY)) {
		/* don't process assoc request; */
		return _SUCCESS;
	}
#endif /* CONFIG_CONCURRENT_MODE */

	if ((pmlmeinfo->state & 0x03) != WIFI_FW_AP_STATE)
		return _FAIL;

	if (rtw_check_invalid_mac_address(get_addr2_ptr(pframe), _FALSE)) {
		RTW_INFO("%s : reject invalid ASSOC-req "MAC_FMT"\n", 
			__func__, MAC_ARG(get_addr2_ptr(pframe)));
		return _FAIL;
	}

	frame_type = get_frame_sub_type(pframe);
	if (frame_type == WIFI_ASSOCREQ) {
		reassoc = 0;
		ie_offset = _ASOCREQ_IE_OFFSET_;
	} else { /* WIFI_REASSOCREQ */
		reassoc = 1;
		ie_offset = _REASOCREQ_IE_OFFSET_;
	}


	if (pkt_len < IEEE80211_3ADDR_LEN + ie_offset) {
		RTW_INFO("handle_assoc(reassoc=%d) - too short payload (len=%lu)"
			 "\n", reassoc, (unsigned long)pkt_len);
		return _FAIL;
	}

	pstat = rtw_get_stainfo(pstapriv, get_addr2_ptr(pframe));
	if (pstat == (struct sta_info *)NULL) {
		status = _RSON_CLS2_;
		goto asoc_class2_error;
	}

	RTW_INFO("%s\n", __FUNCTION__);

	if (pstat->authalg == WLAN_AUTH_SAE) {
		/* WPA3-SAE */
		if (((pstat->state) & WIFI_FW_AUTH_NULL)) {
			/* TODO:
			   Queue AssocReq and Proccess
			   by external auth trigger. */
			RTW_INFO("%s: wait external auth trigger\n", __func__);
			return _SUCCESS;
		}
	}

	/* check if this stat has been successfully authenticated/assocated */
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

#if 0/* todo:tkip_countermeasures */
	if (hapd->tkip_countermeasures) {
		resp = WLAN_REASON_MICHAEL_MIC_FAILURE;
		goto fail;
	}
#endif

	if (rtw_ap_linking_test_force_asoc_fail()) {
		status = rtw_ap_linking_test_force_asoc_fail();
		RTW_INFO(FUNC_ADPT_FMT" force asoc fail with status:%u\n"
			, FUNC_ADPT_ARG(padapter), status);
		goto OnAssocReqFail;
	}

	/* now parse all ieee802_11 ie to point to elems */
	left = pkt_len - (IEEE80211_3ADDR_LEN + ie_offset);
	pos = pframe + (IEEE80211_3ADDR_LEN + ie_offset);
	if (rtw_ieee802_11_parse_elems(pos, left, &elems, 1) == ParseFailed) {
		RTW_INFO("STA " MAC_FMT " sent invalid association request\n",
			 MAC_ARG(pstat->cmn.mac_addr));
		status = _STATS_FAILURE_;
		goto OnAssocReqFail;
	}

	rtw_ap_parse_sta_capability(padapter, pstat, pframe + WLAN_HDR_A3_LEN);

	listen_interval = RTW_GET_LE16(pframe + WLAN_HDR_A3_LEN + 2);
#if 0/* todo: */
	/* check listen_interval */
	if (listen_interval > hapd->conf->max_listen_interval) {
		hostapd_logger(hapd, mgmt->sa, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "Too large Listen Interval (%d)",
			       listen_interval);
		resp = WLAN_STATUS_ASSOC_DENIED_LISTEN_INT_TOO_LARGE;
		goto fail;
	}

	pstat->listen_interval = listen_interval;
#endif

	/* now we should check all the fields... */
	/* checking SSID */
	if (elems.ssid == NULL
		|| elems.ssid_len == 0
		|| elems.ssid_len != cur->Ssid.SsidLength
		|| _rtw_memcmp(elems.ssid, cur->Ssid.Ssid, cur->Ssid.SsidLength) == _FALSE
	) {
		status = _STATS_FAILURE_;
		goto OnAssocReqFail;
	}

	/* (Extended) Supported rates */
	status = rtw_ap_parse_sta_supported_rates(padapter, pstat
		, pframe + WLAN_HDR_A3_LEN + ie_offset, pkt_len - WLAN_HDR_A3_LEN - ie_offset);
	if (status != _STATS_SUCCESSFUL_)
		goto OnAssocReqFail;

	/* check RSN/WPA/WPS */
	status = rtw_ap_parse_sta_security_ie(padapter, pstat, &elems);
	if (status != _STATS_SUCCESSFUL_)
		goto OnAssocReqFail;

	/* check if there is WMM IE & support WWM-PS */
	rtw_ap_parse_sta_wmm_ie(padapter, pstat
		, pframe + WLAN_HDR_A3_LEN + ie_offset, pkt_len - WLAN_HDR_A3_LEN - ie_offset);

#ifdef CONFIG_RTS_FULL_BW
	/*check vendor IE*/
	rtw_parse_sta_vendor_ie_8812(padapter, pstat
		, pframe + WLAN_HDR_A3_LEN + ie_offset, pkt_len - WLAN_HDR_A3_LEN - ie_offset);
#endif/*CONFIG_RTS_FULL_BW*/

#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
	if (elems.tbtx_cap && elems.tbtx_cap_len != 0) {
		if(rtw_is_tbtx_capabilty(elems.tbtx_cap, elems.tbtx_cap_len)) {
			sta_tbtx_enable = _TRUE;
		}
	}

#endif

	rtw_ap_parse_sta_ht_ie(padapter, pstat, &elems);
	rtw_ap_parse_sta_vht_ie(padapter, pstat, &elems);

	if (((pstat->flags & WLAN_STA_HT) || (pstat->flags & WLAN_STA_VHT)) &&
	    ((pstat->wpa2_pairwise_cipher & WPA_CIPHER_TKIP) ||
	     (pstat->wpa_pairwise_cipher & WPA_CIPHER_TKIP))) {

		RTW_INFO("(V)HT: " MAC_FMT " tried to use TKIP with (V)HT association\n", MAC_ARG(pstat->cmn.mac_addr));

		pstat->flags &= ~WLAN_STA_HT;
		pstat->flags &= ~WLAN_STA_VHT;
		/*status = WLAN_STATUS_CIPHER_REJECTED_PER_POLICY;
		  * goto OnAssocReqFail;
		*/
	}

#ifdef CONFIG_P2P
	pstat->is_p2p_device = _FALSE;
	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
		p2pie = rtw_get_p2p_ie(pframe + WLAN_HDR_A3_LEN + ie_offset , pkt_len - WLAN_HDR_A3_LEN - ie_offset , NULL, &p2pielen);
		if (p2pie) {
			pstat->is_p2p_device = _TRUE;
			p2p_status_code = (u8)process_assoc_req_p2p_ie(pwdinfo, pframe, pkt_len, pstat);
			if (p2p_status_code > 0) {
				pstat->p2p_status_code = p2p_status_code;
				status = _STATS_CAP_FAIL_;
				goto OnAssocReqFail;
			}
		}
#ifdef CONFIG_WFD
		rtw_process_wfd_ies(padapter, pframe + WLAN_HDR_A3_LEN + ie_offset, pkt_len - WLAN_HDR_A3_LEN - ie_offset, __func__);
#endif
	}
	pstat->p2p_status_code = p2p_status_code;
#endif /* CONFIG_P2P */

	rtw_ap_parse_sta_multi_ap_ie(padapter, pstat, pos, left);

#ifdef CONFIG_RTW_REPEATER_SON
	if (rtw_rson_ap_check_sta(padapter, pframe, pkt_len, ie_offset))
		goto OnAssocReqFail;
#endif

	/* TODO: identify_proprietary_vendor_ie(); */
	/* Realtek proprietary IE */
	/* identify if this is Broadcom sta */
	/* identify if this is ralink sta */
	/* Customer proprietary IE */

#ifdef CONFIG_RTW_80211K
	rtw_ap_parse_sta_rm_en_cap(padapter, pstat, &elems);
#endif

	/* AID assignment */
	if (pstat->cmn.aid > 0)
		RTW_INFO(FUNC_ADPT_FMT" old AID=%d\n", FUNC_ADPT_ARG(padapter), pstat->cmn.aid);
	else {
		if (!rtw_aid_alloc(padapter, pstat)) {
			RTW_INFO(FUNC_ADPT_FMT" no room for more AIDs\n", FUNC_ADPT_ARG(padapter));
			status = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;
			goto OnAssocReqFail;
		}
		RTW_INFO(FUNC_ADPT_FMT" allocate new AID=%d\n", FUNC_ADPT_ARG(padapter), pstat->cmn.aid);
	}

	pstat->state &= (~WIFI_FW_ASSOC_STATE);
	pstat->state |= WIFI_FW_ASSOC_SUCCESS;
	/* RTW_INFO("==================%s, %d,  (%x), bpairwise_key_installed=%d, MAC:"MAC_FMT"\n"
	, __func__, __LINE__, pstat->state, pstat->bpairwise_key_installed, MAC_ARG(pstat->cmn.mac_addr)); */
#ifdef CONFIG_IEEE80211W
	if ((pstat->bpairwise_key_installed != _TRUE && (pstat->flags & WLAN_STA_MFP)) 
		|| !(pstat->flags & WLAN_STA_MFP))
#endif /* CONFIG_IEEE80211W */
	{
		_enter_critical_bh(&pstapriv->auth_list_lock, &irqL);
		if (!rtw_is_list_empty(&pstat->auth_list)) {
			rtw_list_delete(&pstat->auth_list);
			pstapriv->auth_list_cnt--;
		}
		_exit_critical_bh(&pstapriv->auth_list_lock, &irqL);

		_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
		if (rtw_is_list_empty(&pstat->asoc_list)) {
			pstat->expire_to = pstapriv->expire_to;
			rtw_list_insert_tail(&pstat->asoc_list, &pstapriv->asoc_list);
			pstapriv->asoc_list_cnt++;
#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
			if (sta_tbtx_enable) {
				pstat->tbtx_enable = _TRUE;
				pstapriv->tbtx_asoc_list_cnt++;
			}
#endif
		}
		_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	}

	/* now the station is qualified to join our BSS...	 */
	if (pstat && (pstat->state & WIFI_FW_ASSOC_SUCCESS) && (_STATS_SUCCESSFUL_ == status)) {
#ifdef CONFIG_NATIVEAP_MLME
#ifdef CONFIG_IEEE80211W
		if ((pstat->bpairwise_key_installed != _TRUE && (pstat->flags & WLAN_STA_MFP)) 
			|| !(pstat->flags & WLAN_STA_MFP))
#endif /* CONFIG_IEEE80211W */
		{
			/* .1 bss_cap_update & sta_info_update */
			bss_cap_update_on_sta_join(padapter, pstat);
			sta_info_update(padapter, pstat);
		}
#ifdef CONFIG_IEEE80211W
		if (pstat->bpairwise_key_installed == _TRUE && (pstat->flags & WLAN_STA_MFP))
			status = _STATS_REFUSED_TEMPORARILY_;
#endif /* CONFIG_IEEE80211W */
		/* .2 issue assoc rsp before notify station join event. */
		if (frame_type == WIFI_ASSOCREQ)
			issue_asocrsp(padapter, status, pstat, WIFI_ASSOCRSP);
		else
			issue_asocrsp(padapter, status, pstat, WIFI_REASSOCRSP);

#ifdef CONFIG_IOCTL_CFG80211
		_enter_critical_bh(&pstat->lock, &irqL);
		if (pstat->passoc_req) {
			rtw_mfree(pstat->passoc_req, pstat->assoc_req_len);
			pstat->passoc_req = NULL;
			pstat->assoc_req_len = 0;
		}

		pstat->passoc_req =  rtw_zmalloc(pkt_len);
		if (pstat->passoc_req) {
			_rtw_memcpy(pstat->passoc_req, pframe, pkt_len);
			pstat->assoc_req_len = pkt_len;
		}
		_exit_critical_bh(&pstat->lock, &irqL);
#endif /* CONFIG_IOCTL_CFG80211 */
#ifdef CONFIG_IEEE80211W
		if ((pstat->bpairwise_key_installed != _TRUE && (pstat->flags & WLAN_STA_MFP)) 
			|| !(pstat->flags & WLAN_STA_MFP))
#endif /* CONFIG_IEEE80211W */
		{
			/* .3-(1) report sta add event */
			report_add_sta_event(padapter, pstat->cmn.mac_addr);
		}
#ifdef CONFIG_IEEE80211W
		if (pstat->bpairwise_key_installed == _TRUE && (pstat->flags & WLAN_STA_MFP)) {
			RTW_INFO(MAC_FMT"\n", MAC_ARG(pstat->cmn.mac_addr));
			issue_action_SA_Query(padapter, pstat->cmn.mac_addr, 0, 0, IEEE80211W_RIGHT_KEY);
		}
#endif /* CONFIG_IEEE80211W */
#endif /* CONFIG_NATIVEAP_MLME */
	}

	return _SUCCESS;

asoc_class2_error:

#ifdef CONFIG_NATIVEAP_MLME
	issue_deauth(padapter, (void *)get_addr2_ptr(pframe), status);
#endif

	return _FAIL;

OnAssocReqFail:


#ifdef CONFIG_NATIVEAP_MLME
	/* pstat->cmn.aid = 0; */
	if (frame_type == WIFI_ASSOCREQ)
		issue_asocrsp(padapter, status, pstat, WIFI_ASSOCRSP);
	else
		issue_asocrsp(padapter, status, pstat, WIFI_REASSOCRSP);
#endif


#endif /* CONFIG_AP_MODE */

	return _FAIL;

}

#if defined(CONFIG_LAYER2_ROAMING) && defined(CONFIG_RTW_80211K)
void rtw_roam_nb_discover(_adapter *padapter, u8 bfroce)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);	
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;
	u8 nb_req_issue = _FALSE;

	if (!check_fwstate(pmlmepriv, WIFI_ASOC_STATE))
		return;

	if (!rtw_chk_roam_flags(padapter, RTW_ROAM_ACTIVE))
		return;

	psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress);
	if (!psta)
		return;
	
	if (bfroce || (!pmlmepriv->nb_info.nb_rpt_is_same))
		nb_req_issue = _TRUE;
	
	if (nb_req_issue && (psta->rm_en_cap[0] & RTW_RRM_NB_RPT_EN)) 
		rm_add_nb_req(padapter, psta);
}
#endif

unsigned int OnAssocRsp(_adapter *padapter, union recv_frame *precv_frame)
{
	uint i;
	int res;
	unsigned short	status;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	/* WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network); */
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint pkt_len = precv_frame->u.hdr.len;
#ifdef CONFIG_WAPI_SUPPORT
	PNDIS_802_11_VARIABLE_IEs	pWapiIE = NULL;
#endif

	RTW_INFO("%s\n", __FUNCTION__);

	/* check A1 matches or not */
	if (!_rtw_memcmp(adapter_mac_addr(padapter), get_da(pframe), ETH_ALEN))
		return _SUCCESS;

	if (!(pmlmeinfo->state & (WIFI_FW_AUTH_SUCCESS | WIFI_FW_ASSOC_STATE)) || pmlmeext->join_abort)
		return _SUCCESS;

	if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
		return _SUCCESS;

	_cancel_timer_ex(&pmlmeext->link_timer);

	/* status */
	status = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN + 2));
	if (status > 0) {
		RTW_INFO("assoc reject, status code: %d\n", status);
		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		res = -4;
		goto report_assoc_result;
	}

	/* get capabilities */
	pmlmeinfo->capability = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN));

	/* set slot time */
	pmlmeinfo->slotTime = (pmlmeinfo->capability & BIT(10)) ? 9 : 20;

	/* AID */
	res = pmlmeinfo->aid = (int)(le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN + 4)) & 0x3fff);
	
	/* check aid value */
	if (res < 1 || res > 2007) {
		RTW_INFO("assoc reject, aid: %d\n", res);
		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		res = -4;
		goto report_assoc_result;
	}

	/* following are moved to join event callback function */
	/* to handle HT, WMM, rate adaptive, update MAC reg */
	/* for not to handle the synchronous IO in the tasklet */
	for (i = (6 + WLAN_HDR_A3_LEN); i < pkt_len;) {
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pframe + i);

		switch (pIE->ElementID) {
		case _VENDOR_SPECIFIC_IE_:
			if (_rtw_memcmp(pIE->data, WMM_PARA_OUI, 6))	/* WMM */
				WMM_param_handler(padapter, pIE);
#if defined(CONFIG_P2P) && defined(CONFIG_WFD)
			else if (_rtw_memcmp(pIE->data, WFD_OUI, 4))		/* WFD */
				rtw_process_wfd_ie(padapter, (u8 *)pIE, pIE->Length, __func__);
#endif
			break;

#ifdef CONFIG_WAPI_SUPPORT
		case _WAPI_IE_:
			pWapiIE = pIE;
			break;
#endif

		case _HT_CAPABILITY_IE_:	/* HT caps */
			HT_caps_handler(padapter, pIE);
#ifdef ROKU_PRIVATE
			HT_caps_handler_infra_ap(padapter, pIE);
#endif /* ROKU_PRIVATE */
			break;

		case _HT_EXTRA_INFO_IE_:	/* HT info */
			HT_info_handler(padapter, pIE);
			break;

#ifdef CONFIG_80211AC_VHT
		case EID_VHTCapability:
			VHT_caps_handler(padapter, pIE);
#ifdef ROKU_PRIVATE
			VHT_caps_handler_infra_ap(padapter, pIE);
#endif /* ROKU_PRIVATE */
			break;

		case EID_VHTOperation:
			VHT_operation_handler(padapter, pIE);
			break;
#endif

		case _ERPINFO_IE_:
			ERP_IE_handler(padapter, pIE);
			break;
#ifdef CONFIG_TDLS
		case _EXT_CAP_IE_:
			if (check_ap_tdls_prohibited(pIE->data, pIE->Length) == _TRUE)
				padapter->tdlsinfo.ap_prohibited = _TRUE;
			if (check_ap_tdls_ch_switching_prohibited(pIE->data, pIE->Length) == _TRUE)
				padapter->tdlsinfo.ch_switch_prohibited = _TRUE;
			break;
#endif /* CONFIG_TDLS */

#ifdef CONFIG_RTW_80211K
		case _EID_RRM_EN_CAP_IE_:
			RM_IE_handler(padapter, pIE);
			break;
#endif

#ifdef ROKU_PRIVATE
		/* Infra mode, used to store AP's info , Parse the supported rates from AssocRsp */
		case _SUPPORTEDRATES_IE_:
			Supported_rate_infra_ap(padapter, pIE);
			break;

		case _EXT_SUPPORTEDRATES_IE_:
			Extended_Supported_rate_infra_ap(padapter, pIE);
			break;
#endif /* ROKU_PRIVATE */
		default:
			break;
		}

		i += (pIE->Length + 2);
	}

#ifdef CONFIG_WAPI_SUPPORT
	rtw_wapi_on_assoc_ok(padapter, pIE);
#endif

	pmlmeinfo->state &= (~WIFI_FW_ASSOC_STATE);
	pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;

	/* Update Basic Rate Table for spec, 2010-12-28 , by thomas */
	UpdateBrateTbl(padapter, pmlmeinfo->network.SupportedRates);

report_assoc_result:
	if (res > 0)
		rtw_buf_update(&pmlmepriv->assoc_rsp, &pmlmepriv->assoc_rsp_len, pframe, pkt_len);
	else
		rtw_buf_free(&pmlmepriv->assoc_rsp, &pmlmepriv->assoc_rsp_len);

	report_join_res(padapter, res, status);

#if defined(CONFIG_LAYER2_ROAMING) && defined(CONFIG_RTW_80211K)
	rtw_roam_nb_discover(padapter, _TRUE);
#endif
	return _SUCCESS;
}

unsigned int OnDeAuth(_adapter *padapter, union recv_frame *precv_frame)
{
	unsigned short	reason;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
#endif /* CONFIG_P2P */

	/* check A3 */
	if (!(_rtw_memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN)))
		return _SUCCESS;

	RTW_INFO(FUNC_ADPT_FMT" - Start to Disconnect\n", FUNC_ADPT_ARG(padapter));

#ifdef CONFIG_P2P
	if (pwdinfo->rx_invitereq_info.scan_op_ch_only) {
		_cancel_timer_ex(&pwdinfo->reset_ch_sitesurvey);
		_set_timer(&pwdinfo->reset_ch_sitesurvey, 10);
	}
#endif /* CONFIG_P2P */

	reason = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN));

#ifdef CONFIG_AP_MODE
	if (MLME_IS_AP(padapter)) {
		_irqL irqL;
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		/* _enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		 */
		/* rtw_free_stainfo(padapter, psta); */
		/* _exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		 */

		RTW_PRINT(FUNC_ADPT_FMT" reason=%u, ta=%pM\n"
			, FUNC_ADPT_ARG(padapter), reason, get_addr2_ptr(pframe));

		psta = rtw_get_stainfo(pstapriv, get_addr2_ptr(pframe));
		if (psta) {
			u8 updated = _FALSE;

			_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
			if (rtw_is_list_empty(&psta->asoc_list) == _FALSE) {
				rtw_list_delete(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;
				#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
				if (psta->tbtx_enable)
					pstapriv->tbtx_asoc_list_cnt--;
				#endif
				updated = ap_free_sta(padapter, psta, _FALSE, reason, _TRUE);

			}
			_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

			associated_clients_update(padapter, updated, STA_INFO_UPDATE_ALL);
		}


		return _SUCCESS;
	} else
#endif
	if (!MLME_IS_MESH(padapter)) {
		int	ignore_received_deauth = 0;

		/*	Commented by Albert 20130604 */
		/*	Before sending the auth frame to start the STA/GC mode connection with AP/GO,  */
		/*	we will send the deauth first. */
		/*	However, the Win8.1 with BRCM Wi-Fi will send the deauth with reason code 6 to us after receieving our deauth. */
		/*	Added the following code to avoid this case. */
		if ((pmlmeinfo->state & WIFI_FW_AUTH_STATE) ||
		    (pmlmeinfo->state & WIFI_FW_ASSOC_STATE)) {
			if (reason == WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA)
				ignore_received_deauth = 1;
			else if (WLAN_REASON_PREV_AUTH_NOT_VALID == reason) {
				/* TODO: 802.11r */
				ignore_received_deauth = 1;
			}
		}

		RTW_PRINT(FUNC_ADPT_FMT" reason=%u, ta=%pM, ignore=%d\n"
			, FUNC_ADPT_ARG(padapter), reason, get_addr2_ptr(pframe), ignore_received_deauth);

		if (0 == ignore_received_deauth)
			receive_disconnect(padapter, get_addr2_ptr(pframe), reason, _FALSE);
	}
	pmlmepriv->LinkDetectInfo.bBusyTraffic = _FALSE;
	return _SUCCESS;

}

unsigned int OnDisassoc(_adapter *padapter, union recv_frame *precv_frame)
{
	unsigned short	reason;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
#endif /* CONFIG_P2P */

	/* check A3 */
	if (!(_rtw_memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN)))
		return _SUCCESS;

	RTW_INFO(FUNC_ADPT_FMT" - Start to Disconnect\n", FUNC_ADPT_ARG(padapter));

#ifdef CONFIG_P2P
	if (pwdinfo->rx_invitereq_info.scan_op_ch_only) {
		_cancel_timer_ex(&pwdinfo->reset_ch_sitesurvey);
		_set_timer(&pwdinfo->reset_ch_sitesurvey, 10);
	}
#endif /* CONFIG_P2P */

	reason = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN));

#ifdef CONFIG_AP_MODE
	if (MLME_IS_AP(padapter)) {
		_irqL irqL;
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		/* _enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	 */
		/* rtw_free_stainfo(padapter, psta); */
		/* _exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		 */

		RTW_PRINT(FUNC_ADPT_FMT" reason=%u, ta=%pM\n"
			, FUNC_ADPT_ARG(padapter), reason, get_addr2_ptr(pframe));

		psta = rtw_get_stainfo(pstapriv, get_addr2_ptr(pframe));
		if (psta) {
			u8 updated = _FALSE;

			_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
			if (rtw_is_list_empty(&psta->asoc_list) == _FALSE) {
				rtw_list_delete(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;
				#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
				if (psta->tbtx_enable)
					pstapriv->tbtx_asoc_list_cnt--;
				#endif
				updated = ap_free_sta(padapter, psta, _FALSE, reason, _TRUE);

			}
			_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

			associated_clients_update(padapter, updated, STA_INFO_UPDATE_ALL);
		}

		return _SUCCESS;
	} else
#endif
	if (!MLME_IS_MESH(padapter)) {
		RTW_PRINT(FUNC_ADPT_FMT" reason=%u, ta=%pM\n"
			, FUNC_ADPT_ARG(padapter), reason, get_addr2_ptr(pframe));

	#ifdef CONFIG_RTW_WNM
		if (rtw_wnm_try_btm_roam_imnt(padapter) > 0)
	#endif
		receive_disconnect(padapter, get_addr2_ptr(pframe), reason, _FALSE);
	}
	pmlmepriv->LinkDetectInfo.bBusyTraffic = _FALSE;
	return _SUCCESS;

}

unsigned int OnAtim(_adapter *padapter, union recv_frame *precv_frame)
{
	RTW_INFO("%s\n", __FUNCTION__);
	return _SUCCESS;
}

unsigned int on_action_spct_ch_switch(_adapter *padapter, struct sta_info *psta, u8 *ies, uint ies_len)
{
	unsigned int ret = _FAIL;
	struct mlme_ext_priv *mlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(mlmeext->mlmext_info);

	if (!(pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)) {
		ret = _SUCCESS;
		goto exit;
	}

	if ((pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE) {

		int ch_switch_mode = -1, ch = -1, ch_switch_cnt = -1;
		int ch_offset = -1;
		u8 bwmode;
		struct ieee80211_info_element *ie;

		RTW_INFO(FUNC_NDEV_FMT" from "MAC_FMT"\n",
			FUNC_NDEV_ARG(padapter->pnetdev), MAC_ARG(psta->cmn.mac_addr));

		for_each_ie(ie, ies, ies_len) {
			if (ie->id == WLAN_EID_CHANNEL_SWITCH) {
				ch_switch_mode = ie->data[0];
				ch = ie->data[1];
				ch_switch_cnt = ie->data[2];
				RTW_INFO("ch_switch_mode:%d, ch:%d, ch_switch_cnt:%d\n",
					 ch_switch_mode, ch, ch_switch_cnt);
			} else if (ie->id == WLAN_EID_SECONDARY_CHANNEL_OFFSET) {
				ch_offset = secondary_ch_offset_to_hal_ch_offset(ie->data[0]);
				RTW_INFO("ch_offset:%d\n", ch_offset);
			}
		}

		if (ch == -1)
			return _SUCCESS;

		if (ch_offset == -1)
			bwmode = mlmeext->cur_bwmode;
		else
			bwmode = (ch_offset == HAL_PRIME_CHNL_OFFSET_DONT_CARE) ?
				 CHANNEL_WIDTH_20 : CHANNEL_WIDTH_40;

		ch_offset = (ch_offset == -1) ? mlmeext->cur_ch_offset : ch_offset;

		/* todo:
		 * 1. the decision of channel switching
		 * 2. things after channel switching
		 */

		ret = rtw_set_chbw_cmd(padapter, ch, bwmode, ch_offset, 0);
	}

exit:
	return ret;
}

unsigned int on_action_spct(_adapter *padapter, union recv_frame *precv_frame)
{
	unsigned int ret = _FAIL;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint frame_len = precv_frame->u.hdr.len;
	u8 *frame_body = (u8 *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));
	u8 category;
	u8 action;

	psta = rtw_get_stainfo(pstapriv, get_addr2_ptr(pframe));

	if (!psta)
		goto exit;

	category = frame_body[0];
	if (category != RTW_WLAN_CATEGORY_SPECTRUM_MGMT)
		goto exit;

	action = frame_body[1];

	RTW_INFO(FUNC_ADPT_FMT" action:%u\n", FUNC_ADPT_ARG(padapter), action);

	switch (action) {
	case RTW_WLAN_ACTION_SPCT_MSR_REQ:
	case RTW_WLAN_ACTION_SPCT_MSR_RPRT:
	case RTW_WLAN_ACTION_SPCT_TPC_REQ:
	case RTW_WLAN_ACTION_SPCT_TPC_RPRT:
		break;
	case RTW_WLAN_ACTION_SPCT_CHL_SWITCH:
#ifdef CONFIG_SPCT_CH_SWITCH
		ret = on_action_spct_ch_switch(padapter, psta
				, frame_body + 2, frame_len - (frame_body - pframe) - 2);
#elif CONFIG_DFS
		if (MLME_IS_STA(padapter) && MLME_IS_ASOC(padapter)) {
			process_csa_ie(padapter
				, frame_body + 2, frame_len - (frame_body - pframe) - 2);
		}
#endif
		break;
	default:
		break;
	}

exit:
	return ret;
}

unsigned int OnAction_qos(_adapter *padapter, union recv_frame *precv_frame)
{
	return _SUCCESS;
}

unsigned int OnAction_dls(_adapter *padapter, union recv_frame *precv_frame)
{
	return _SUCCESS;
}

#ifdef CONFIG_RTW_WNM
unsigned int on_action_wnm(_adapter *adapter, union recv_frame *rframe)
{
	unsigned int ret = _FAIL;
	struct sta_info *sta = NULL;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct sta_priv *stapriv = &(adapter->stapriv);
	u8 *frame = rframe->u.hdr.rx_data;
	u32 frame_len = rframe->u.hdr.len;
	u8 *frame_body = (u8 *)(frame + sizeof(struct rtw_ieee80211_hdr_3addr));
	u32 frame_body_len = frame_len - sizeof(struct rtw_ieee80211_hdr_3addr);	
	u8 category, action;
	int cnt = 0;
	char msg[16];

	sta = rtw_get_stainfo(stapriv, get_addr2_ptr(frame));
	if (!sta)
		goto exit;

	category = frame_body[0];
	if (category != RTW_WLAN_CATEGORY_WNM)
		goto exit;

	action = frame_body[1];

	switch (action) {
#ifdef CONFIG_RTW_80211R
	case RTW_WLAN_ACTION_WNM_BTM_REQ:
		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE) {
			RTW_INFO("WNM: BSS Transition Management Request recv.\n");
			rtw_wnm_process_btm_req(adapter, frame_body, frame_body_len);
		}
		ret = _SUCCESS;
		break;
#endif
	case RTW_WLAN_ACTION_WNM_BTM_RSP:
		if ((check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) &&
			(pmlmepriv->nb_info.features & RTW_WNM_FEATURE_BTM_REQ_EN)) {
			struct btm_rsp_hdr rsp;
			u32 sz;

			RTW_INFO("WNM: BSS Transition Management Response recv.\n");
			sz = rtw_wnm_btm_rsp_candidates_sz_get(adapter,
					frame_body, frame_body_len);
			_rtw_memset(&rsp, 0, sizeof(rsp));

			if (sz > 0)
				rsp.pcandidates = rtw_zmalloc(sz);

			rtw_wnm_process_btm_rsp(adapter, frame_body, frame_body_len, &rsp);
			/* TODO : handle candidates info in rsp.pcandidates for upper-layer services */
			#ifdef CONFIG_PLATFORM_CMAP_INTFS
			cmap_intfs_nl_btm_resp_event(adapter, sta->cmn.mac_addr,
						     adapter_mac_addr(adapter),
						     rsp.status, rsp.bssid,
						     rsp.pcandidates,
						     rsp.candidates_num);
			#endif
			if (0 && rsp.pcandidates && (rsp.candidates_num > 0))
				RTW_INFO_DUMP("pcandidates : ", rsp.pcandidates, sz);

			if ((sz > 0) && (rsp.pcandidates != NULL))
				rtw_mfree(rsp.pcandidates, sz);
		}
		 /* fall through */
	default:
		#ifdef CONFIG_IOCTL_CFG80211
		cnt += sprintf((msg + cnt), "ACT_WNM %u", action);
		rtw_cfg80211_rx_action(adapter, rframe, msg);
		#endif
		ret = _SUCCESS;
		break;
	}

exit:
	return ret;
}
#endif /* CONFIG_RTW_WNM */

/**
 * rtw_rx_ampdu_size - Get the target RX AMPDU buffer size for the specific @adapter
 * @adapter: the adapter to get target RX AMPDU buffer size
 *
 * Returns: the target RX AMPDU buffer size
 */
u8 rtw_rx_ampdu_size(_adapter *adapter)
{
	u8 size;
	HT_CAP_AMPDU_FACTOR max_rx_ampdu_factor;

#ifdef CONFIG_BT_COEXIST
	if (rtw_btcoex_IsBTCoexCtrlAMPDUSize(adapter) == _TRUE) {
		size = rtw_btcoex_GetAMPDUSize(adapter);
		goto exit;
	}
#endif

	/* for scan */
	if (!mlmeext_chk_scan_state(&adapter->mlmeextpriv, SCAN_DISABLE)
	    && !mlmeext_chk_scan_state(&adapter->mlmeextpriv, SCAN_COMPLETE)
	    && adapter->mlmeextpriv.sitesurvey_res.rx_ampdu_size != RX_AMPDU_SIZE_INVALID
	   ) {
		size = adapter->mlmeextpriv.sitesurvey_res.rx_ampdu_size;
		goto exit;
	}

	/* default value based on max_rx_ampdu_factor */
	if (adapter->driver_rx_ampdu_factor != 0xFF)
		max_rx_ampdu_factor = (HT_CAP_AMPDU_FACTOR)adapter->driver_rx_ampdu_factor;
	else
		rtw_hal_get_def_var(adapter, HW_VAR_MAX_RX_AMPDU_FACTOR, &max_rx_ampdu_factor);
	
	/* In Maximum A-MPDU Length Exponent subfield of A-MPDU Parameters field of HT Capabilities element,
		the unit of max_rx_ampdu_factor are octets. 8K, 16K, 32K, 64K is right.
		But the buffer size subfield of Block Ack Parameter Set field in ADDBA action frame indicates
		the number of buffers available for this particular TID. Each buffer is equal to max. size of 
		MSDU or AMSDU. 
		The size variable means how many MSDUs or AMSDUs, it's not Kbytes.
	*/
	if (MAX_AMPDU_FACTOR_64K == max_rx_ampdu_factor)
		size = 64;
	else if (MAX_AMPDU_FACTOR_32K == max_rx_ampdu_factor)
		size = 32;
	else if (MAX_AMPDU_FACTOR_16K == max_rx_ampdu_factor)
		size = 16;
	else if (MAX_AMPDU_FACTOR_8K == max_rx_ampdu_factor)
		size = 8;
	else
		size = 64;

exit:

	if (size > 127)
		size = 127;

	return size;
}

/**
 * rtw_rx_ampdu_is_accept - Get the permission if RX AMPDU should be set up for the specific @adapter
 * @adapter: the adapter to get the permission if RX AMPDU should be set up
 *
 * Returns: accept or not
 */
bool rtw_rx_ampdu_is_accept(_adapter *adapter)
{
	bool accept;

	if (adapter->fix_rx_ampdu_accept != RX_AMPDU_ACCEPT_INVALID) {
		accept = adapter->fix_rx_ampdu_accept;
		goto exit;
	}

#ifdef CONFIG_BT_COEXIST
	if (rtw_btcoex_IsBTCoexRejectAMPDU(adapter) == _TRUE) {
		accept = _FALSE;
		goto exit;
	}
#endif

	/* for scan */
	if (!mlmeext_chk_scan_state(&adapter->mlmeextpriv, SCAN_DISABLE)
	    && !mlmeext_chk_scan_state(&adapter->mlmeextpriv, SCAN_COMPLETE)
	    && adapter->mlmeextpriv.sitesurvey_res.rx_ampdu_accept != RX_AMPDU_ACCEPT_INVALID
	   ) {
		accept = adapter->mlmeextpriv.sitesurvey_res.rx_ampdu_accept;
		goto exit;
	}

	/* default value for other cases */
	accept = adapter->mlmeextpriv.mlmext_info.bAcceptAddbaReq;

exit:
	return accept;
}

/**
 * rtw_rx_ampdu_set_size - Set the target RX AMPDU buffer size for the specific @adapter and specific @reason
 * @adapter: the adapter to set target RX AMPDU buffer size
 * @size: the target RX AMPDU buffer size to set
 * @reason: reason for the target RX AMPDU buffer size setting
 *
 * Returns: whether the target RX AMPDU buffer size is changed
 */
bool rtw_rx_ampdu_set_size(_adapter *adapter, u8 size, u8 reason)
{
	bool is_adj = _FALSE;
	struct mlme_ext_priv *mlmeext;
	struct mlme_ext_info *mlmeinfo;

	mlmeext = &adapter->mlmeextpriv;
	mlmeinfo = &mlmeext->mlmext_info;

	if (reason == RX_AMPDU_DRV_FIXED) {
		if (adapter->fix_rx_ampdu_size != size) {
			adapter->fix_rx_ampdu_size = size;
			is_adj = _TRUE;
			RTW_INFO(FUNC_ADPT_FMT" fix_rx_ampdu_size:%u\n", FUNC_ADPT_ARG(adapter), size);
		}
	} else if (reason == RX_AMPDU_DRV_SCAN) {
		struct ss_res *ss = &adapter->mlmeextpriv.sitesurvey_res;

		if (ss->rx_ampdu_size != size) {
			ss->rx_ampdu_size = size;
			is_adj = _TRUE;
			RTW_INFO(FUNC_ADPT_FMT" ss.rx_ampdu_size:%u\n", FUNC_ADPT_ARG(adapter), size);
		}
	}

	return is_adj;
}

/**
 * rtw_rx_ampdu_set_accept - Set the permission if RX AMPDU should be set up for the specific @adapter and specific @reason
 * @adapter: the adapter to set if RX AMPDU should be set up
 * @accept: if RX AMPDU should be set up
 * @reason: reason for the permission if RX AMPDU should be set up
 *
 * Returns: whether the permission if RX AMPDU should be set up is changed
 */
bool rtw_rx_ampdu_set_accept(_adapter *adapter, u8 accept, u8 reason)
{
	bool is_adj = _FALSE;
	struct mlme_ext_priv *mlmeext;
	struct mlme_ext_info *mlmeinfo;

	mlmeext = &adapter->mlmeextpriv;
	mlmeinfo = &mlmeext->mlmext_info;

	if (reason == RX_AMPDU_DRV_FIXED) {
		if (adapter->fix_rx_ampdu_accept != accept) {
			adapter->fix_rx_ampdu_accept = accept;
			is_adj = _TRUE;
			RTW_INFO(FUNC_ADPT_FMT" fix_rx_ampdu_accept:%u\n", FUNC_ADPT_ARG(adapter), accept);
		}
	} else if (reason == RX_AMPDU_DRV_SCAN) {
		if (adapter->mlmeextpriv.sitesurvey_res.rx_ampdu_accept != accept) {
			adapter->mlmeextpriv.sitesurvey_res.rx_ampdu_accept = accept;
			is_adj = _TRUE;
			RTW_INFO(FUNC_ADPT_FMT" ss.rx_ampdu_accept:%u\n", FUNC_ADPT_ARG(adapter), accept);
		}
	}

	return is_adj;
}

/**
 * rx_ampdu_apply_sta_tid - Apply RX AMPDU setting to the specific @sta and @tid
 * @adapter: the adapter to which @sta belongs
 * @sta: the sta to be checked
 * @tid: the tid to be checked
 * @accept: the target permission if RX AMPDU should be set up
 * @size: the target RX AMPDU buffer size
 *
 * Returns:
 * 0: no canceled
 * 1: canceled by no permission
 * 2: canceled by different buffer size
 * 3: canceled by potential mismatched status
 *
 * Blocking function, may sleep
 */
u8 rx_ampdu_apply_sta_tid(_adapter *adapter, struct sta_info *sta, u8 tid, u8 accept, u8 size)
{
	u8 ret = 0;
	struct recv_reorder_ctrl *reorder_ctl = &sta->recvreorder_ctrl[tid];

	if (reorder_ctl->enable == _FALSE) {
		if (reorder_ctl->ampdu_size != RX_AMPDU_SIZE_INVALID) {
			send_delba_sta_tid_wait_ack(adapter, 0, sta, tid, 1);
			ret = 3;
		}
		goto exit;
	}

	if (accept == _FALSE) {
		send_delba_sta_tid_wait_ack(adapter, 0, sta, tid, 0);
		ret = 1;
	} else if (reorder_ctl->ampdu_size != size) {
		send_delba_sta_tid_wait_ack(adapter, 0, sta, tid, 0);
		ret = 2;
	}

exit:
	return ret;
}

u8 rx_ampdu_size_sta_limit(_adapter *adapter, struct sta_info *sta)
{
	u8 sz_limit = 0xFF;

#ifdef CONFIG_80211N_HT
	struct registry_priv *regsty = adapter_to_regsty(adapter);
	struct mlme_priv *mlme = &adapter->mlmepriv;
	struct mlme_ext_info *mlmeinfo = &adapter->mlmeextpriv.mlmext_info;
	s8 nss = -1;
	u8 bw = rtw_min(sta->cmn.bw_mode, adapter->mlmeextpriv.cur_bwmode);

	#ifdef CONFIG_80211AC_VHT
	if (is_supported_vht(sta->wireless_mode)) {
		nss = rtw_min(rtw_vht_mcsmap_to_nss(mlme->vhtpriv.vht_mcs_map)
				, rtw_vht_mcsmap_to_nss(sta->vhtpriv.vht_mcs_map));
	} else
	#endif
	if (is_supported_ht(sta->wireless_mode)) {
		nss = rtw_min(rtw_ht_mcsset_to_nss(mlmeinfo->HT_caps.u.HT_cap_element.MCS_rate)
				, rtw_ht_mcsset_to_nss(sta->htpriv.ht_cap.supp_mcs_set));
	}

	if (nss >= 1)
		sz_limit = regsty->rx_ampdu_sz_limit_by_nss_bw[nss - 1][bw];
#endif /* CONFIG_80211N_HT */

	return sz_limit;
}

/**
 * rx_ampdu_apply_sta - Apply RX AMPDU setting to the specific @sta
 * @adapter: the adapter to which @sta belongs
 * @sta: the sta to be checked
 * @accept: the target permission if RX AMPDU should be set up
 * @size: the target RX AMPDU buffer size
 *
 * Returns: number of the RX AMPDU assciation canceled for applying current target setting
 *
 * Blocking function, may sleep
 */
u8 rx_ampdu_apply_sta(_adapter *adapter, struct sta_info *sta, u8 accept, u8 size)
{
	u8 change_cnt = 0;
	int i;

	for (i = 0; i < TID_NUM; i++) {
		if (rx_ampdu_apply_sta_tid(adapter, sta, i, accept, size) != 0)
			change_cnt++;
	}

	return change_cnt;
}

/**
 * rtw_rx_ampdu_apply - Apply the current target RX AMPDU setting for the specific @adapter
 * @adapter: the adapter to be applied
 *
 * Returns: number of the RX AMPDU assciation canceled for applying current target setting
 */
u16 rtw_rx_ampdu_apply(_adapter *adapter)
{
	u16 adj_cnt = 0;
	struct sta_info *sta;
	u8 accept = rtw_rx_ampdu_is_accept(adapter);
	u8 size;

	if (adapter->fix_rx_ampdu_size != RX_AMPDU_SIZE_INVALID)
		size = adapter->fix_rx_ampdu_size;
	else
		size = rtw_rx_ampdu_size(adapter);

	if (MLME_IS_STA(adapter)) {
		sta = rtw_get_stainfo(&adapter->stapriv, get_bssid(&adapter->mlmepriv));
		if (sta) {
			u8 sta_size = size;

			if (adapter->fix_rx_ampdu_size == RX_AMPDU_SIZE_INVALID)
				sta_size = rtw_min(size, rx_ampdu_size_sta_limit(adapter, sta));
			adj_cnt += rx_ampdu_apply_sta(adapter, sta, accept, sta_size);
		}
		/* TODO: TDLS peer */
#ifdef CONFIG_AP_MODE
	} else if (MLME_IS_AP(adapter) || MLME_IS_MESH(adapter)) {
		_irqL irqL;
		_list *phead, *plist;
		u8 peer_num = 0;
		char peers[NUM_STA];
		struct sta_priv *pstapriv = &adapter->stapriv;
		int i;

		_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);

		phead = &pstapriv->asoc_list;
		plist = get_next(phead);

		while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
			int stainfo_offset;

			sta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
			plist = get_next(plist);

			stainfo_offset = rtw_stainfo_offset(pstapriv, sta);
			if (stainfo_offset_valid(stainfo_offset))
				peers[peer_num++] = stainfo_offset;
		}

		_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

		for (i = 0; i < peer_num; i++) {
			sta = rtw_get_stainfo_by_offset(pstapriv, peers[i]);
			if (sta) {
				u8 sta_size = size;

				if (adapter->fix_rx_ampdu_size == RX_AMPDU_SIZE_INVALID)
					sta_size = rtw_min(size, rx_ampdu_size_sta_limit(adapter, sta));
				adj_cnt += rx_ampdu_apply_sta(adapter, sta, accept, sta_size);
			}
		}
#endif /* CONFIG_AP_MODE */
	}

	/* TODO: ADHOC */

	return adj_cnt;
}

unsigned int OnAction_back(_adapter *padapter, union recv_frame *precv_frame)
{
	u8 *addr;
	struct sta_info *psta = NULL;
	struct recv_reorder_ctrl *preorder_ctrl;
	unsigned char		*frame_body;
	unsigned char		category, action;
	unsigned short	tid, status, reason_code = 0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct registry_priv *pregpriv = &padapter->registrypriv;

#ifdef CONFIG_80211N_HT

	RTW_INFO("%s\n", __FUNCTION__);

	/* check RA matches or not	 */
	if (!_rtw_memcmp(adapter_mac_addr(padapter), GetAddr1Ptr(pframe), ETH_ALEN))
		return _SUCCESS;

#if 0
	/* check A1 matches or not */
	if (!_rtw_memcmp(adapter_mac_addr(padapter), get_da(pframe), ETH_ALEN))
		return _SUCCESS;
#endif

	if ((pmlmeinfo->state & 0x03) != WIFI_FW_AP_STATE)
		if (!(pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS))
			return _SUCCESS;

	addr = get_addr2_ptr(pframe);
	psta = rtw_get_stainfo(pstapriv, addr);

	if (psta == NULL)
		return _SUCCESS;

	frame_body = (unsigned char *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));

	category = frame_body[0];
	if (category == RTW_WLAN_CATEGORY_BACK) { /* representing Block Ack */
#ifdef CONFIG_TDLS
		if ((psta->tdls_sta_state & TDLS_LINKED_STATE) &&
		    (psta->htpriv.ht_option == _TRUE) &&
		    (psta->htpriv.ampdu_enable == _TRUE))
			RTW_INFO("Recv [%s] from direc link\n", __FUNCTION__);
		else
#endif /* CONFIG_TDLS */
			if (!pmlmeinfo->HT_enable)
				return _SUCCESS;

		action = frame_body[1];
		RTW_INFO("%s, action=%d\n", __FUNCTION__, action);
		switch (action) {
		case RTW_WLAN_ACTION_ADDBA_REQ: /* ADDBA request */

			_rtw_memcpy(&(pmlmeinfo->ADDBA_req), &(frame_body[2]), sizeof(struct ADDBA_request));
			/* process_addba_req(padapter, (u8*)&(pmlmeinfo->ADDBA_req), GetAddr3Ptr(pframe)); */
			process_addba_req(padapter, (u8 *)&(pmlmeinfo->ADDBA_req), addr);

			break;

		case RTW_WLAN_ACTION_ADDBA_RESP: /* ADDBA response */

			/* status = frame_body[3] | (frame_body[4] << 8); */ /* endian issue */
			status = RTW_GET_LE16(&frame_body[3]);
			tid = ((frame_body[5] >> 2) & 0x7);
			if (status == 0) {
				/* successful */
				RTW_INFO("agg_enable for TID=%d\n", tid);
				psta->htpriv.agg_enable_bitmap |= 1 << tid;
				psta->htpriv.candidate_tid_bitmap &= ~BIT(tid);
				/* amsdu in ampdu */
				if (pregpriv->tx_ampdu_amsdu == 0)
					psta->htpriv.tx_amsdu_enable = _FALSE;
				else if (pregpriv->tx_ampdu_amsdu == 1)
					psta->htpriv.tx_amsdu_enable = _TRUE;
				else {
					if (frame_body[5] & 1)
						psta->htpriv.tx_amsdu_enable = _TRUE;
				}
			} else
				psta->htpriv.agg_enable_bitmap &= ~BIT(tid);

			#ifdef CONFIG_AP_MODE
			if (psta->state & WIFI_STA_ALIVE_CHK_STATE) {
				RTW_INFO("%s alive check - rx ADDBA response\n", __func__);
				psta->htpriv.agg_enable_bitmap &= ~BIT(tid);
				psta->expire_to = pstapriv->expire_to;
				psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
			}
			#endif

			/* RTW_INFO("marc: ADDBA RSP: %x\n", pmlmeinfo->agg_enable_bitmap); */
			break;

		case RTW_WLAN_ACTION_DELBA: /* DELBA */
			if ((frame_body[3] & BIT(3)) == 0) {
				psta->htpriv.agg_enable_bitmap &= ~(1 << ((frame_body[3] >> 4) & 0xf));
				psta->htpriv.candidate_tid_bitmap &= ~(1 << ((frame_body[3] >> 4) & 0xf));

				/* reason_code = frame_body[4] | (frame_body[5] << 8); */
				reason_code = RTW_GET_LE16(&frame_body[4]);
			} else if ((frame_body[3] & BIT(3)) == BIT(3)) {
				tid = (frame_body[3] >> 4) & 0x0F;

				preorder_ctrl = &psta->recvreorder_ctrl[tid];
				preorder_ctrl->enable = _FALSE;
				preorder_ctrl->ampdu_size = RX_AMPDU_SIZE_INVALID;
			}

			RTW_INFO("%s(): DELBA: %x(%x)\n", __FUNCTION__, pmlmeinfo->agg_enable_bitmap, reason_code);
			/* todo: how to notify the host while receiving DELETE BA */
			break;

		default:
			break;
		}
	}
#endif /* CONFIG_80211N_HT */
	return _SUCCESS;
}

#ifdef CONFIG_APPEND_VENDOR_IE_ENABLE
u32 rtw_build_vendor_ie(_adapter *padapter , unsigned char **pframe , u8 mgmt_frame_tyte)
{
	int vendor_ie_num = 0;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u32 len = 0;

	for (vendor_ie_num = 0 ; vendor_ie_num < WLAN_MAX_VENDOR_IE_NUM ; vendor_ie_num++) {
		if (pmlmepriv->vendor_ielen[vendor_ie_num] > 0 && pmlmepriv->vendor_ie_mask[vendor_ie_num] & mgmt_frame_tyte) {
			_rtw_memcpy(*pframe , pmlmepriv->vendor_ie[vendor_ie_num] , pmlmepriv->vendor_ielen[vendor_ie_num]);
			*pframe +=  pmlmepriv->vendor_ielen[vendor_ie_num];
			len += pmlmepriv->vendor_ielen[vendor_ie_num];
		}
	}

	return len;
}
#endif

#ifdef CONFIG_P2P
int get_reg_classes_full_count(struct p2p_channels *channel_list)
{
	int cnt = 0;
	int i;

	for (i = 0; i < channel_list->reg_classes; i++)
		cnt += channel_list->reg_class[i].channels;

	return cnt;
}

void issue_p2p_GO_request(_adapter *padapter, u8 *raddr)
{
	struct p2p_channels *ch_list = &(adapter_to_rfctl(padapter)->channel_list);
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_GO_NEGO_REQ;
	u8			wpsie[255] = { 0x00 }, p2pie[255] = { 0x00 };
	u8			wpsielen = 0, p2pielen = 0;
	u16			len_channellist_attr = 0;
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
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);


	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return;

	RTW_INFO("[%s] In\n", __FUNCTION__);
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
	pwdinfo->negotiation_dialog_token = 1;	/*	Initialize the dialog value */
	pframe = rtw_set_fixed_ie(pframe, 1, &pwdinfo->negotiation_dialog_token, &(pattrib->pktlen));



	/*	WPS Section */
	wpsielen = 0;
	/*	WPS OUI */
	*(u32 *)(wpsie) = cpu_to_be32(WPSOUI);
	wpsielen += 4;

	/*	WPS version */
	/*	Type: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_VER1);
	wpsielen += 2;

	/*	Length: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
	wpsielen += 2;

	/*	Value: */
	wpsie[wpsielen++] = WPS_VERSION_1;	/*	Version 1.0 */

	/*	Device Password ID */
	/*	Type: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_DEVICE_PWID);
	wpsielen += 2;

	/*	Length: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0002);
	wpsielen += 2;

	/*	Value: */

	if (pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_PEER_DISPLAY_PIN)
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_DPID_USER_SPEC);
	else if (pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_SELF_DISPLAY_PIN)
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_DPID_REGISTRAR_SPEC);
	else if (pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_PBC)
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_DPID_PBC);

	wpsielen += 2;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen);


	/*	P2P IE Section. */

	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*	Commented by Albert 20110306 */
	/*	According to the P2P Specification, the group negoitation request frame should contain 9 P2P attributes */
	/*	1. P2P Capability */
	/*	2. Group Owner Intent */
	/*	3. Configuration Timeout */
	/*	4. Listen Channel */
	/*	5. Extended Listen Timing */
	/*	6. Intended P2P Interface Address */
	/*	7. Channel List */
	/*	8. P2P Device Info */
	/*	9. Operating Channel */


	/*	P2P Capability */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CAPABILITY;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	/*	Device Capability Bitmap, 1 byte */
	p2pie[p2pielen++] = DMP_P2P_DEVCAP_SUPPORT;

	/*	Group Capability Bitmap, 1 byte */
	if (pwdinfo->persistent_supported)
		p2pie[p2pielen++] = P2P_GRPCAP_CROSS_CONN | P2P_GRPCAP_PERSISTENT_GROUP;
	else
		p2pie[p2pielen++] = P2P_GRPCAP_CROSS_CONN;


	/*	Group Owner Intent */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_GO_INTENT;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0001);
	p2pielen += 2;

	/*	Value: */
	/*	Todo the tie breaker bit. */
	p2pie[p2pielen++] = ((pwdinfo->intent << 1) &  0xFE);

	/*	Configuration Timeout */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CONF_TIMEOUT;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	p2pie[p2pielen++] = 200;	/*	2 seconds needed to be the P2P GO */
	p2pie[p2pielen++] = 200;	/*	2 seconds needed to be the P2P Client */


	/*	Listen Channel */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_LISTEN_CH;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0005);
	p2pielen += 2;

	/*	Value: */
	/*	Country String */
	p2pie[p2pielen++] = 'X';
	p2pie[p2pielen++] = 'X';

	/*	The third byte should be set to 0x04. */
	/*	Described in the "Operating Channel Attribute" section. */
	p2pie[p2pielen++] = 0x04;

	/*	Operating Class */
	p2pie[p2pielen++] = 0x51;	/*	Copy from SD7 */

	/*	Channel Number */
	p2pie[p2pielen++] = pwdinfo->listen_channel;	/*	listening channel number */


	/*	Extended Listen Timing ATTR */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_EX_LISTEN_TIMING;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0004);
	p2pielen += 2;

	/*	Value: */
	/*	Availability Period */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0xFFFF);
	p2pielen += 2;

	/*	Availability Interval */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0xFFFF);
	p2pielen += 2;


	/*	Intended P2P Interface Address */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_INTENDED_IF_ADDR;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(ETH_ALEN);
	p2pielen += 2;

	/*	Value: */
	_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
	p2pielen += ETH_ALEN;


	/*	Channel List */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CH_LIST;

	/* Length: */
	/* Country String(3) */
	/* + ( Operating Class (1) + Number of Channels(1) ) * Operation Classes (?) */
	/* + number of channels in all classes */
	len_channellist_attr = 3
		       + (1 + 1) * (u16)(ch_list->reg_classes)
		       + get_reg_classes_full_count(ch_list);

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(padapter, MI_LINKED) && padapter->registrypriv.full_ch_in_p2p_handshake == 0)
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(5 + 1);
	else
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);
#else

	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);

#endif
	p2pielen += 2;

	/*	Value: */
	/*	Country String */
	p2pie[p2pielen++] = 'X';
	p2pie[p2pielen++] = 'X';

	/*	The third byte should be set to 0x04. */
	/*	Described in the "Operating Channel Attribute" section. */
	p2pie[p2pielen++] = 0x04;

	/*	Channel Entry List */

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(padapter, MI_LINKED) && padapter->registrypriv.full_ch_in_p2p_handshake == 0) {
		u8 union_ch = rtw_mi_get_union_chan(padapter);

		/*	Operating Class */
		if (union_ch > 14) {
			if (union_ch >= 149)
				p2pie[p2pielen++] = 0x7c;
			else
				p2pie[p2pielen++] = 0x73;
		} else
			p2pie[p2pielen++] = 0x51;


		/*	Number of Channels */
		/*	Just support 1 channel and this channel is AP's channel */
		p2pie[p2pielen++] = 1;

		/*	Channel List */
		p2pie[p2pielen++] = union_ch;
	} else
#endif /* CONFIG_CONCURRENT_MODE */
	{
		int i, j;
		for (j = 0; j < ch_list->reg_classes; j++) {
			/*	Operating Class */
			p2pie[p2pielen++] = ch_list->reg_class[j].reg_class;

			/*	Number of Channels */
			p2pie[p2pielen++] = ch_list->reg_class[j].channels;

			/*	Channel List */
			for (i = 0; i < ch_list->reg_class[j].channels; i++)
				p2pie[p2pielen++] = ch_list->reg_class[j].channel[i];
		}
	}

	/*	Device Info */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	21->P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes)  */
	/*	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes) */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(21 + pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	/*	P2P Device Address */
	_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	Config Method */
	/*	This field should be big endian. Noted by P2P specification. */

	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(pwdinfo->supported_wps_cm);

	p2pielen += 2;

	/*	Primary Device Type */
	/*	Category ID */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA);
	p2pielen += 2;

	/*	OUI */
	*(u32 *)(p2pie + p2pielen) = cpu_to_be32(WPSOUI);
	p2pielen += 4;

	/*	Sub Category ID */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
	p2pielen += 2;

	/*	Number of Secondary Device Types */
	p2pie[p2pielen++] = 0x00;	/*	No Secondary Device Type List */

	/*	Device Name */
	/*	Type: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_ATTR_DEVICE_NAME);
	p2pielen += 2;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	_rtw_memcpy(p2pie + p2pielen, pwdinfo->device_name , pwdinfo->device_name_len);
	p2pielen += pwdinfo->device_name_len;


	/*	Operating Channel */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_OPERATING_CH;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0005);
	p2pielen += 2;

	/*	Value: */
	/*	Country String */
	p2pie[p2pielen++] = 'X';
	p2pie[p2pielen++] = 'X';

	/*	The third byte should be set to 0x04. */
	/*	Described in the "Operating Channel Attribute" section. */
	p2pie[p2pielen++] = 0x04;

	/*	Operating Class */
	if (pwdinfo->operating_channel <= 14) {
		/*	Operating Class */
		p2pie[p2pielen++] = 0x51;
	} else if ((pwdinfo->operating_channel >= 36) && (pwdinfo->operating_channel <= 48)) {
		/*	Operating Class */
		p2pie[p2pielen++] = 0x73;
	} else {
		/*	Operating Class */
		p2pie[p2pielen++] = 0x7c;
	}

	/*	Channel Number */
	p2pie[p2pielen++] = pwdinfo->operating_channel;	/*	operating channel number */

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen);

#ifdef CONFIG_WFD
	wfdielen = build_nego_req_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}


void issue_p2p_GO_response(_adapter *padapter, u8 *raddr, u8 *frame_body, uint len, u8 result)
{
	struct p2p_channels *ch_list = &(adapter_to_rfctl(padapter)->channel_list);
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_GO_NEGO_RESP;
	u8			wpsie[255] = { 0x00 }, p2pie[255] = { 0x00 };
	u8			p2pielen = 0;
	uint			wpsielen = 0;
	u16			wps_devicepassword_id = 0x0000;
	uint			wps_devicepassword_id_len = 0;
	u16			len_channellist_attr = 0;

	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);

#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return;

	RTW_INFO("[%s] In, result = %d\n", __FUNCTION__,  result);
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
	pwdinfo->negotiation_dialog_token = frame_body[7];	/*	The Dialog Token of provisioning discovery request frame. */
	pframe = rtw_set_fixed_ie(pframe, 1, &(pwdinfo->negotiation_dialog_token), &(pattrib->pktlen));

	/*	Commented by Albert 20110328 */
	/*	Try to get the device password ID from the WPS IE of group negotiation request frame */
	/*	WiFi Direct test plan 5.1.15 */
	rtw_get_wps_ie(frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, wpsie, &wpsielen);
	rtw_get_wps_attr_content(wpsie, wpsielen, WPS_ATTR_DEVICE_PWID, (u8 *) &wps_devicepassword_id, &wps_devicepassword_id_len);
	wps_devicepassword_id = be16_to_cpu(wps_devicepassword_id);

	_rtw_memset(wpsie, 0x00, 255);
	wpsielen = 0;

	/*	WPS Section */
	wpsielen = 0;
	/*	WPS OUI */
	*(u32 *)(wpsie) = cpu_to_be32(WPSOUI);
	wpsielen += 4;

	/*	WPS version */
	/*	Type: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_VER1);
	wpsielen += 2;

	/*	Length: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
	wpsielen += 2;

	/*	Value: */
	wpsie[wpsielen++] = WPS_VERSION_1;	/*	Version 1.0 */

	/*	Device Password ID */
	/*	Type: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_DEVICE_PWID);
	wpsielen += 2;

	/*	Length: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0002);
	wpsielen += 2;

	/*	Value: */
	if (wps_devicepassword_id == WPS_DPID_USER_SPEC)
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_DPID_REGISTRAR_SPEC);
	else if (wps_devicepassword_id == WPS_DPID_REGISTRAR_SPEC)
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_DPID_USER_SPEC);
	else
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_DPID_PBC);
	wpsielen += 2;

	/*	Commented by Kurt 20120113 */
	/*	If some device wants to do p2p handshake without sending prov_disc_req */
	/*	We have to get peer_req_cm from here. */
	if (_rtw_memcmp(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "000", 3)) {
		if (wps_devicepassword_id == WPS_DPID_USER_SPEC)
			_rtw_memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "dis", 3);
		else if (wps_devicepassword_id == WPS_DPID_REGISTRAR_SPEC)
			_rtw_memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pad", 3);
		else
			_rtw_memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pbc", 3);
	}

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen);


	/*	P2P IE Section. */

	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*	Commented by Albert 20100908 */
	/*	According to the P2P Specification, the group negoitation response frame should contain 9 P2P attributes */
	/*	1. Status */
	/*	2. P2P Capability */
	/*	3. Group Owner Intent */
	/*	4. Configuration Timeout */
	/*	5. Operating Channel */
	/*	6. Intended P2P Interface Address */
	/*	7. Channel List */
	/*	8. Device Info */
	/*	9. Group ID	( Only GO ) */


	/*	ToDo: */

	/*	P2P Status */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_STATUS;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0001);
	p2pielen += 2;

	/*	Value: */
	p2pie[p2pielen++] = result;

	/*	P2P Capability */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CAPABILITY;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	/*	Device Capability Bitmap, 1 byte */

	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT)) {
		/*	Commented by Albert 2011/03/08 */
		/*	According to the P2P specification */
		/*	if the sending device will be client, the P2P Capability should be reserved of group negotation response frame */
		p2pie[p2pielen++] = 0;
	} else {
		/*	Be group owner or meet the error case */
		p2pie[p2pielen++] = DMP_P2P_DEVCAP_SUPPORT;
	}

	/*	Group Capability Bitmap, 1 byte */
	if (pwdinfo->persistent_supported)
		p2pie[p2pielen++] = P2P_GRPCAP_CROSS_CONN | P2P_GRPCAP_PERSISTENT_GROUP;
	else
		p2pie[p2pielen++] = P2P_GRPCAP_CROSS_CONN;

	/*	Group Owner Intent */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_GO_INTENT;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0001);
	p2pielen += 2;

	/*	Value: */
	if (pwdinfo->peer_intent & 0x01) {
		/*	Peer's tie breaker bit is 1, our tie breaker bit should be 0 */
		p2pie[p2pielen++] = (pwdinfo->intent << 1);
	} else {
		/*	Peer's tie breaker bit is 0, our tie breaker bit should be 1 */
		p2pie[p2pielen++] = ((pwdinfo->intent << 1) | BIT(0));
	}


	/*	Configuration Timeout */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CONF_TIMEOUT;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	p2pie[p2pielen++] = 200;	/*	2 seconds needed to be the P2P GO */
	p2pie[p2pielen++] = 200;	/*	2 seconds needed to be the P2P Client */

	/*	Operating Channel */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_OPERATING_CH;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0005);
	p2pielen += 2;

	/*	Value: */
	/*	Country String */
	p2pie[p2pielen++] = 'X';
	p2pie[p2pielen++] = 'X';

	/*	The third byte should be set to 0x04. */
	/*	Described in the "Operating Channel Attribute" section. */
	p2pie[p2pielen++] = 0x04;

	/*	Operating Class */
	if (pwdinfo->operating_channel <= 14) {
		/*	Operating Class */
		p2pie[p2pielen++] = 0x51;
	} else if ((pwdinfo->operating_channel >= 36) && (pwdinfo->operating_channel <= 48)) {
		/*	Operating Class */
		p2pie[p2pielen++] = 0x73;
	} else {
		/*	Operating Class */
		p2pie[p2pielen++] = 0x7c;
	}

	/*	Channel Number */
	p2pie[p2pielen++] = pwdinfo->operating_channel;	/*	operating channel number */

	/*	Intended P2P Interface Address	 */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_INTENDED_IF_ADDR;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(ETH_ALEN);
	p2pielen += 2;

	/*	Value: */
	_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	Channel List */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CH_LIST;

	/* Country String(3) */
	/* + ( Operating Class (1) + Number of Channels(1) ) * Operation Classes (?) */
	/* + number of channels in all classes */
	len_channellist_attr = 3
		       + (1 + 1) * (u16)ch_list->reg_classes
		       + get_reg_classes_full_count(ch_list);

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(padapter, MI_LINKED) && padapter->registrypriv.full_ch_in_p2p_handshake == 0)
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(5 + 1);
	else
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);
#else
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);

#endif
	p2pielen += 2;

	/*	Value: */
	/*	Country String */
	p2pie[p2pielen++] = 'X';
	p2pie[p2pielen++] = 'X';

	/*	The third byte should be set to 0x04. */
	/*	Described in the "Operating Channel Attribute" section. */
	p2pie[p2pielen++] = 0x04;

	/*	Channel Entry List */

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(padapter, MI_LINKED) && padapter->registrypriv.full_ch_in_p2p_handshake == 0) {

		u8 union_chan = rtw_mi_get_union_chan(padapter);

		/*Operating Class*/
		if (union_chan > 14) {
			if (union_chan >= 149)
				p2pie[p2pielen++] = 0x7c;
			else
				p2pie[p2pielen++] = 0x73;

		} else
			p2pie[p2pielen++] = 0x51;

		/*	Number of Channels
			Just support 1 channel and this channel is AP's channel*/
		p2pie[p2pielen++] = 1;

		/*Channel List*/
		p2pie[p2pielen++] = union_chan;
	} else
#endif /* CONFIG_CONCURRENT_MODE */
	{
		int i, j;
		for (j = 0; j < ch_list->reg_classes; j++) {
			/*	Operating Class */
			p2pie[p2pielen++] = ch_list->reg_class[j].reg_class;

			/*	Number of Channels */
			p2pie[p2pielen++] = ch_list->reg_class[j].channels;

			/*	Channel List */
			for (i = 0; i < ch_list->reg_class[j].channels; i++)
				p2pie[p2pielen++] = ch_list->reg_class[j].channel[i];
		}
	}

	/*	Device Info */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	21->P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes)  */
	/*	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes) */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(21 + pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	/*	P2P Device Address */
	_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	Config Method */
	/*	This field should be big endian. Noted by P2P specification. */

	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(pwdinfo->supported_wps_cm);

	p2pielen += 2;

	/*	Primary Device Type */
	/*	Category ID */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA);
	p2pielen += 2;

	/*	OUI */
	*(u32 *)(p2pie + p2pielen) = cpu_to_be32(WPSOUI);
	p2pielen += 4;

	/*	Sub Category ID */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
	p2pielen += 2;

	/*	Number of Secondary Device Types */
	p2pie[p2pielen++] = 0x00;	/*	No Secondary Device Type List */

	/*	Device Name */
	/*	Type: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_ATTR_DEVICE_NAME);
	p2pielen += 2;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	_rtw_memcpy(p2pie + p2pielen, pwdinfo->device_name , pwdinfo->device_name_len);
	p2pielen += pwdinfo->device_name_len;

	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
		/*	Group ID Attribute */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_GROUP_ID;

		/*	Length: */
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(ETH_ALEN + pwdinfo->nego_ssidlen);
		p2pielen += 2;

		/*	Value: */
		/*	p2P Device Address */
		_rtw_memcpy(p2pie + p2pielen , pwdinfo->device_addr, ETH_ALEN);
		p2pielen += ETH_ALEN;

		/*	SSID */
		_rtw_memcpy(p2pie + p2pielen, pwdinfo->nego_ssid, pwdinfo->nego_ssidlen);
		p2pielen += pwdinfo->nego_ssidlen;

	}

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen);

#ifdef CONFIG_WFD
	wfdielen = build_nego_resp_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}

void issue_p2p_GO_confirm(_adapter *padapter, u8 *raddr, u8 result)
{

	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_GO_NEGO_CONF;
	u8			p2pie[255] = { 0x00 };
	u8			p2pielen = 0;

	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return;

	RTW_INFO("[%s] In\n", __FUNCTION__);
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
	pframe = rtw_set_fixed_ie(pframe, 1, &(pwdinfo->negotiation_dialog_token), &(pattrib->pktlen));



	/*	P2P IE Section. */

	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*	Commented by Albert 20110306 */
	/*	According to the P2P Specification, the group negoitation request frame should contain 5 P2P attributes */
	/*	1. Status */
	/*	2. P2P Capability */
	/*	3. Operating Channel */
	/*	4. Channel List */
	/*	5. Group ID	( if this WiFi is GO ) */

	/*	P2P Status */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_STATUS;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0001);
	p2pielen += 2;

	/*	Value: */
	p2pie[p2pielen++] = result;

	/*	P2P Capability */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CAPABILITY;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	/*	Device Capability Bitmap, 1 byte */
	p2pie[p2pielen++] = DMP_P2P_DEVCAP_SUPPORT;

	/*	Group Capability Bitmap, 1 byte */
	if (pwdinfo->persistent_supported)
		p2pie[p2pielen++] = P2P_GRPCAP_CROSS_CONN | P2P_GRPCAP_PERSISTENT_GROUP;
	else
		p2pie[p2pielen++] = P2P_GRPCAP_CROSS_CONN;


	/*	Operating Channel */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_OPERATING_CH;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0005);
	p2pielen += 2;

	/*	Value: */
	/*	Country String */
	p2pie[p2pielen++] = 'X';
	p2pie[p2pielen++] = 'X';

	/*	The third byte should be set to 0x04. */
	/*	Described in the "Operating Channel Attribute" section. */
	p2pie[p2pielen++] = 0x04;


	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT)) {
		if (pwdinfo->peer_operating_ch <= 14) {
			/*	Operating Class */
			p2pie[p2pielen++] = 0x51;
		} else if ((pwdinfo->peer_operating_ch >= 36) && (pwdinfo->peer_operating_ch <= 48)) {
			/*	Operating Class */
			p2pie[p2pielen++] = 0x73;
		} else {
			/*	Operating Class */
			p2pie[p2pielen++] = 0x7c;
		}

		p2pie[p2pielen++] = pwdinfo->peer_operating_ch;
	} else {
		if (pwdinfo->operating_channel <= 14) {
			/*	Operating Class */
			p2pie[p2pielen++] = 0x51;
		} else if ((pwdinfo->operating_channel >= 36) && (pwdinfo->operating_channel <= 48)) {
			/*	Operating Class */
			p2pie[p2pielen++] = 0x73;
		} else {
			/*	Operating Class */
			p2pie[p2pielen++] = 0x7c;
		}

		/*	Channel Number */
		p2pie[p2pielen++] = pwdinfo->operating_channel;		/*	Use the listen channel as the operating channel */
	}


	/*	Channel List */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CH_LIST;

	*(u16 *)(p2pie + p2pielen) = 6;
	p2pielen += 2;

	/*	Country String */
	p2pie[p2pielen++] = 'X';
	p2pie[p2pielen++] = 'X';

	/*	The third byte should be set to 0x04. */
	/*	Described in the "Operating Channel Attribute" section. */
	p2pie[p2pielen++] = 0x04;

	/*	Value: */
	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT)) {
		if (pwdinfo->peer_operating_ch <= 14) {
			/*	Operating Class */
			p2pie[p2pielen++] = 0x51;
		} else if ((pwdinfo->peer_operating_ch >= 36) && (pwdinfo->peer_operating_ch <= 48)) {
			/*	Operating Class */
			p2pie[p2pielen++] = 0x73;
		} else {
			/*	Operating Class */
			p2pie[p2pielen++] = 0x7c;
		}
		p2pie[p2pielen++] = 1;
		p2pie[p2pielen++] = pwdinfo->peer_operating_ch;
	} else {
		if (pwdinfo->operating_channel <= 14) {
			/*	Operating Class */
			p2pie[p2pielen++] = 0x51;
		} else if ((pwdinfo->operating_channel >= 36) && (pwdinfo->operating_channel <= 48)) {
			/*	Operating Class */
			p2pie[p2pielen++] = 0x73;
		} else {
			/*	Operating Class */
			p2pie[p2pielen++] = 0x7c;
		}

		/*	Channel Number */
		p2pie[p2pielen++] = 1;
		p2pie[p2pielen++] = pwdinfo->operating_channel;		/*	Use the listen channel as the operating channel */
	}

	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
		/*	Group ID Attribute */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_GROUP_ID;

		/*	Length: */
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(ETH_ALEN + pwdinfo->nego_ssidlen);
		p2pielen += 2;

		/*	Value: */
		/*	p2P Device Address */
		_rtw_memcpy(p2pie + p2pielen , pwdinfo->device_addr, ETH_ALEN);
		p2pielen += ETH_ALEN;

		/*	SSID */
		_rtw_memcpy(p2pie + p2pielen, pwdinfo->nego_ssid, pwdinfo->nego_ssidlen);
		p2pielen += pwdinfo->nego_ssidlen;
	}

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen);

#ifdef CONFIG_WFD
	wfdielen = build_nego_confirm_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}

void issue_p2p_invitation_request(_adapter *padapter, u8 *raddr)
{
	struct p2p_channels *ch_list = &(adapter_to_rfctl(padapter)->channel_list);
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_INVIT_REQ;
	u8			p2pie[255] = { 0x00 };
	u8			p2pielen = 0;
	u8			dialogToken = 3;
	u16			len_channellist_attr = 0;
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
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);


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
	_rtw_memcpy(pwlanhdr->addr3, raddr,  ETH_ALEN);

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

	/*	P2P IE Section. */

	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*	Commented by Albert 20101011 */
	/*	According to the P2P Specification, the P2P Invitation request frame should contain 7 P2P attributes */
	/*	1. Configuration Timeout */
	/*	2. Invitation Flags */
	/*	3. Operating Channel	( Only GO ) */
	/*	4. P2P Group BSSID	( Should be included if I am the GO ) */
	/*	5. Channel List */
	/*	6. P2P Group ID */
	/*	7. P2P Device Info */

	/*	Configuration Timeout */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CONF_TIMEOUT;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	p2pie[p2pielen++] = 200;	/*	2 seconds needed to be the P2P GO */
	p2pie[p2pielen++] = 200;	/*	2 seconds needed to be the P2P Client */

	/*	Invitation Flags */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_INVITATION_FLAGS;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0001);
	p2pielen += 2;

	/*	Value: */
	p2pie[p2pielen++] = P2P_INVITATION_FLAGS_PERSISTENT;


	/*	Operating Channel */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_OPERATING_CH;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0005);
	p2pielen += 2;

	/*	Value: */
	/*	Country String */
	p2pie[p2pielen++] = 'X';
	p2pie[p2pielen++] = 'X';

	/*	The third byte should be set to 0x04. */
	/*	Described in the "Operating Channel Attribute" section. */
	p2pie[p2pielen++] = 0x04;

	/*	Operating Class */
	if (pwdinfo->invitereq_info.operating_ch <= 14)
		p2pie[p2pielen++] = 0x51;
	else if ((pwdinfo->invitereq_info.operating_ch >= 36) && (pwdinfo->invitereq_info.operating_ch <= 48))
		p2pie[p2pielen++] = 0x73;
	else
		p2pie[p2pielen++] = 0x7c;

	/*	Channel Number */
	p2pie[p2pielen++] = pwdinfo->invitereq_info.operating_ch;	/*	operating channel number */

	if (_rtw_memcmp(adapter_mac_addr(padapter), pwdinfo->invitereq_info.go_bssid, ETH_ALEN)) {
		/*	P2P Group BSSID */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_GROUP_BSSID;

		/*	Length: */
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(ETH_ALEN);
		p2pielen += 2;

		/*	Value: */
		/*	P2P Device Address for GO */
		_rtw_memcpy(p2pie + p2pielen, pwdinfo->invitereq_info.go_bssid, ETH_ALEN);
		p2pielen += ETH_ALEN;
	}

	/*	Channel List */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CH_LIST;


	/*	Length: */
	/* Country String(3) */
	/* + ( Operating Class (1) + Number of Channels(1) ) * Operation Classes (?) */
	/* + number of channels in all classes */
	len_channellist_attr = 3
		       + (1 + 1) * (u16)ch_list->reg_classes
		       + get_reg_classes_full_count(ch_list);

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(padapter, MI_LINKED) && padapter->registrypriv.full_ch_in_p2p_handshake == 0)
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(5 + 1);
	else
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);
#else
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);
#endif
	p2pielen += 2;

	/*	Value: */
	/*	Country String */
	p2pie[p2pielen++] = 'X';
	p2pie[p2pielen++] = 'X';

	/*	The third byte should be set to 0x04. */
	/*	Described in the "Operating Channel Attribute" section. */
	p2pie[p2pielen++] = 0x04;

	/*	Channel Entry List */
#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(padapter, MI_LINKED) && padapter->registrypriv.full_ch_in_p2p_handshake == 0) {
		u8 union_ch =  rtw_mi_get_union_chan(padapter);

		/*	Operating Class */
		if (union_ch > 14) {
			if (union_ch >= 149)
				p2pie[p2pielen++] = 0x7c;
			else
				p2pie[p2pielen++] = 0x73;
		} else
			p2pie[p2pielen++] = 0x51;


		/*	Number of Channels */
		/*	Just support 1 channel and this channel is AP's channel */
		p2pie[p2pielen++] = 1;

		/*	Channel List */
		p2pie[p2pielen++] = union_ch;
	} else
#endif /* CONFIG_CONCURRENT_MODE */
	{
		int i, j;
		for (j = 0; j < ch_list->reg_classes; j++) {
			/*	Operating Class */
			p2pie[p2pielen++] = ch_list->reg_class[j].reg_class;

			/*	Number of Channels */
			p2pie[p2pielen++] = ch_list->reg_class[j].channels;

			/*	Channel List */
			for (i = 0; i < ch_list->reg_class[j].channels; i++)
				p2pie[p2pielen++] = ch_list->reg_class[j].channel[i];
		}
	}


	/*	P2P Group ID */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_GROUP_ID;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(6 + pwdinfo->invitereq_info.ssidlen);
	p2pielen += 2;

	/*	Value: */
	/*	P2P Device Address for GO */
	_rtw_memcpy(p2pie + p2pielen, pwdinfo->invitereq_info.go_bssid, ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	SSID */
	_rtw_memcpy(p2pie + p2pielen, pwdinfo->invitereq_info.go_ssid, pwdinfo->invitereq_info.ssidlen);
	p2pielen += pwdinfo->invitereq_info.ssidlen;


	/*	Device Info */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	21->P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes)  */
	/*	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes) */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(21 + pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	/*	P2P Device Address */
	_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	Config Method */
	/*	This field should be big endian. Noted by P2P specification. */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_CONFIG_METHOD_DISPLAY);
	p2pielen += 2;

	/*	Primary Device Type */
	/*	Category ID */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA);
	p2pielen += 2;

	/*	OUI */
	*(u32 *)(p2pie + p2pielen) = cpu_to_be32(WPSOUI);
	p2pielen += 4;

	/*	Sub Category ID */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
	p2pielen += 2;

	/*	Number of Secondary Device Types */
	p2pie[p2pielen++] = 0x00;	/*	No Secondary Device Type List */

	/*	Device Name */
	/*	Type: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_ATTR_DEVICE_NAME);
	p2pielen += 2;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	_rtw_memcpy(p2pie + p2pielen, pwdinfo->device_name, pwdinfo->device_name_len);
	p2pielen += pwdinfo->device_name_len;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen);

#ifdef CONFIG_WFD
	wfdielen = build_invitation_req_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}

void issue_p2p_invitation_response(_adapter *padapter, u8 *raddr, u8 dialogToken, u8 status_code)
{
	struct p2p_channels *ch_list = &(adapter_to_rfctl(padapter)->channel_list);
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_INVIT_RESP;
	u8			p2pie[255] = { 0x00 };
	u8			p2pielen = 0;
	u16			len_channellist_attr = 0;
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
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);


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
	_rtw_memcpy(pwlanhdr->addr3, raddr,  ETH_ALEN);

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

	/*	P2P IE Section. */

	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*	Commented by Albert 20101005 */
	/*	According to the P2P Specification, the P2P Invitation response frame should contain 5 P2P attributes */
	/*	1. Status */
	/*	2. Configuration Timeout */
	/*	3. Operating Channel	( Only GO ) */
	/*	4. P2P Group BSSID	( Only GO ) */
	/*	5. Channel List */

	/*	P2P Status */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_STATUS;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0001);
	p2pielen += 2;

	/*	Value: */
	/*	When status code is P2P_STATUS_FAIL_INFO_UNAVAILABLE. */
	/*	Sent the event receiving the P2P Invitation Req frame to DMP UI. */
	/*	DMP had to compare the MAC address to find out the profile. */
	/*	So, the WiFi driver will send the P2P_STATUS_FAIL_INFO_UNAVAILABLE to NB. */
	/*	If the UI found the corresponding profile, the WiFi driver sends the P2P Invitation Req */
	/*	to NB to rebuild the persistent group. */
	p2pie[p2pielen++] = status_code;

	/*	Configuration Timeout */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CONF_TIMEOUT;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	p2pie[p2pielen++] = 200;	/*	2 seconds needed to be the P2P GO */
	p2pie[p2pielen++] = 200;	/*	2 seconds needed to be the P2P Client */

	if (status_code == P2P_STATUS_SUCCESS) {
		if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
			/*	The P2P Invitation request frame asks this Wi-Fi device to be the P2P GO */
			/*	In this case, the P2P Invitation response frame should carry the two more P2P attributes. */
			/*	First one is operating channel attribute. */
			/*	Second one is P2P Group BSSID attribute. */

			/*	Operating Channel */
			/*	Type: */
			p2pie[p2pielen++] = P2P_ATTR_OPERATING_CH;

			/*	Length: */
			*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0005);
			p2pielen += 2;

			/*	Value: */
			/*	Country String */
			p2pie[p2pielen++] = 'X';
			p2pie[p2pielen++] = 'X';

			/*	The third byte should be set to 0x04. */
			/*	Described in the "Operating Channel Attribute" section. */
			p2pie[p2pielen++] = 0x04;

			/*	Operating Class */
			p2pie[p2pielen++] = 0x51;	/*	Copy from SD7 */

			/*	Channel Number */
			p2pie[p2pielen++] = pwdinfo->operating_channel;	/*	operating channel number */


			/*	P2P Group BSSID */
			/*	Type: */
			p2pie[p2pielen++] = P2P_ATTR_GROUP_BSSID;

			/*	Length: */
			*(u16 *)(p2pie + p2pielen) = cpu_to_le16(ETH_ALEN);
			p2pielen += 2;

			/*	Value: */
			/*	P2P Device Address for GO */
			_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
			p2pielen += ETH_ALEN;

		}

		/*	Channel List */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_CH_LIST;

		/*	Length: */
		/* Country String(3) */
		/* + ( Operating Class (1) + Number of Channels(1) ) * Operation Classes (?) */
		/* + number of channels in all classes */
		len_channellist_attr = 3
			+ (1 + 1) * (u16)ch_list->reg_classes
			+ get_reg_classes_full_count(ch_list);

#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_mi_check_status(padapter, MI_LINKED) && padapter->registrypriv.full_ch_in_p2p_handshake == 0)
			*(u16 *)(p2pie + p2pielen) = cpu_to_le16(5 + 1);
		else
			*(u16 *)(p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);
#else
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);
#endif
		p2pielen += 2;

		/*	Value: */
		/*	Country String */
		p2pie[p2pielen++] = 'X';
		p2pie[p2pielen++] = 'X';

		/*	The third byte should be set to 0x04. */
		/*	Described in the "Operating Channel Attribute" section. */
		p2pie[p2pielen++] = 0x04;

		/*	Channel Entry List */
#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_mi_check_status(padapter, MI_LINKED) && padapter->registrypriv.full_ch_in_p2p_handshake == 0) {
			u8 union_ch = rtw_mi_get_union_chan(padapter);

			/*	Operating Class */
			if (union_ch > 14) {
				if (union_ch >= 149)
					p2pie[p2pielen++]  = 0x7c;
				else
					p2pie[p2pielen++] = 0x73;
			} else
				p2pie[p2pielen++] = 0x51;


			/*	Number of Channels */
			/*	Just support 1 channel and this channel is AP's channel */
			p2pie[p2pielen++] = 1;

			/*	Channel List */
			p2pie[p2pielen++] = union_ch;
		} else
#endif /* CONFIG_CONCURRENT_MODE */
		{
			int i, j;
			for (j = 0; j < ch_list->reg_classes; j++) {
				/*	Operating Class */
				p2pie[p2pielen++] = ch_list->reg_class[j].reg_class;

				/*	Number of Channels */
				p2pie[p2pielen++] = ch_list->reg_class[j].channels;

				/*	Channel List */
				for (i = 0; i < ch_list->reg_class[j].channels; i++)
					p2pie[p2pielen++] = ch_list->reg_class[j].channel[i];
			}
		}
	}

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen);

#ifdef CONFIG_WFD
	wfdielen = build_invitation_resp_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}

void issue_p2p_provision_request(_adapter *padapter, u8 *pssid, u8 ussidlen, u8 *pdev_raddr)
{
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u8			dialogToken = 1;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_PROVISION_DISC_REQ;
	u8			wpsie[100] = { 0x00 };
	u8			wpsielen = 0;
	u32			p2pielen = 0;
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
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);


	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return;

	RTW_INFO("[%s] In\n", __FUNCTION__);
	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, pdev_raddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, pdev_raddr, ETH_ALEN);

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

	p2pielen = build_prov_disc_request_p2p_ie(pwdinfo, pframe, pssid, ussidlen, pdev_raddr);

	pframe += p2pielen;
	pattrib->pktlen += p2pielen;

	wpsielen = 0;
	/*	WPS OUI */
	*(u32 *)(wpsie) = cpu_to_be32(WPSOUI);
	wpsielen += 4;

	/*	WPS version */
	/*	Type: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_VER1);
	wpsielen += 2;

	/*	Length: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
	wpsielen += 2;

	/*	Value: */
	wpsie[wpsielen++] = WPS_VERSION_1;	/*	Version 1.0 */

	/*	Config Method */
	/*	Type: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_CONF_METHOD);
	wpsielen += 2;

	/*	Length: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0002);
	wpsielen += 2;

	/*	Value: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(pwdinfo->tx_prov_disc_info.wps_config_method_request);
	wpsielen += 2;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen);


#ifdef CONFIG_WFD
	wfdielen = build_provdisc_req_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}


u8 is_matched_in_profilelist(u8 *peermacaddr, struct profile_info *profileinfo)
{
	u8 i, match_result = 0;

	RTW_INFO("[%s] peermac = %.2X %.2X %.2X %.2X %.2X %.2X\n", __FUNCTION__,
		peermacaddr[0], peermacaddr[1], peermacaddr[2], peermacaddr[3], peermacaddr[4], peermacaddr[5]);

	for (i = 0; i < P2P_MAX_PERSISTENT_GROUP_NUM; i++, profileinfo++) {
		RTW_INFO("[%s] profileinfo_mac = %.2X %.2X %.2X %.2X %.2X %.2X\n", __FUNCTION__,
			profileinfo->peermac[0], profileinfo->peermac[1], profileinfo->peermac[2], profileinfo->peermac[3], profileinfo->peermac[4], profileinfo->peermac[5]);
		if (_rtw_memcmp(peermacaddr, profileinfo->peermac, ETH_ALEN)) {
			match_result = 1;
			RTW_INFO("[%s] Match!\n", __FUNCTION__);
			break;
		}
	}

	return match_result ;
}

void issue_probersp_p2p(_adapter *padapter, unsigned char *da)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	unsigned char					*mac;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	/* WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network); */
	u16					beacon_interval = 100;
	u16					capInfo = 0;
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	u8					wpsie[255] = { 0x00 };
	u32					wpsielen = 0, p2pielen = 0;
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif

	/* RTW_INFO("%s\n", __FUNCTION__); */

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	if (IS_CCK_RATE(pattrib->rate)) {
		/* force OFDM 6M rate */
		pattrib->rate = MGN_6M;
		pattrib->raid = rtw_get_mgntframe_raid(padapter, WIRELESS_11G);
	}

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	mac = adapter_mac_addr(padapter);

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);

	/*	Use the device address for BSSID field.	 */
	_rtw_memcpy(pwlanhdr->addr3, mac, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(fctrl, WIFI_PROBERSP);

	pattrib->hdrlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = pattrib->hdrlen;
	pframe += pattrib->hdrlen;

	/* timestamp will be inserted by hardware */
	pframe += 8;
	pattrib->pktlen += 8;

	/* beacon interval: 2 bytes */
	_rtw_memcpy(pframe, (unsigned char *) &beacon_interval, 2);
	pframe += 2;
	pattrib->pktlen += 2;

	/*	capability info: 2 bytes */
	/*	ESS and IBSS bits must be 0 (defined in the 3.1.2.1.1 of WiFi Direct Spec) */
	capInfo |= cap_ShortPremble;
	capInfo |= cap_ShortSlot;

	_rtw_memcpy(pframe, (unsigned char *) &capInfo, 2);
	pframe += 2;
	pattrib->pktlen += 2;


	/* SSID */
	pframe = rtw_set_ie(pframe, _SSID_IE_, 7, pwdinfo->p2p_wildcard_ssid, &pattrib->pktlen);

	/* supported rates... */
	/*	Use the OFDM rate in the P2P probe response frame. ( 6(B), 9(B), 12, 18, 24, 36, 48, 54 ) */
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, pwdinfo->support_rate, &pattrib->pktlen);

	/* DS parameter set */
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&pwdinfo->listen_channel, &pattrib->pktlen);

#ifdef CONFIG_IOCTL_CFG80211
	if (adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->driver_interface == DRIVER_CFG80211) {
		if (pmlmepriv->wps_probe_resp_ie != NULL && pmlmepriv->p2p_probe_resp_ie != NULL) {
			/* WPS IE */
			_rtw_memcpy(pframe, pmlmepriv->wps_probe_resp_ie, pmlmepriv->wps_probe_resp_ie_len);
			pattrib->pktlen += pmlmepriv->wps_probe_resp_ie_len;
			pframe += pmlmepriv->wps_probe_resp_ie_len;

			/* P2P IE */
			_rtw_memcpy(pframe, pmlmepriv->p2p_probe_resp_ie, pmlmepriv->p2p_probe_resp_ie_len);
			pattrib->pktlen += pmlmepriv->p2p_probe_resp_ie_len;
			pframe += pmlmepriv->p2p_probe_resp_ie_len;
		}
	} else
#endif /* CONFIG_IOCTL_CFG80211		 */
	{

		/*	Todo: WPS IE */
		/*	Noted by Albert 20100907 */
		/*	According to the WPS specification, all the WPS attribute is presented by Big Endian. */

		wpsielen = 0;
		/*	WPS OUI */
		*(u32 *)(wpsie) = cpu_to_be32(WPSOUI);
		wpsielen += 4;

		/*	WPS version */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_VER1);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
		wpsielen += 2;

		/*	Value: */
		wpsie[wpsielen++] = WPS_VERSION_1;	/*	Version 1.0 */

		/*	WiFi Simple Config State */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_SIMPLE_CONF_STATE);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
		wpsielen += 2;

		/*	Value: */
		wpsie[wpsielen++] = WPS_WSC_STATE_NOT_CONFIG;	/*	Not Configured. */

		/*	Response Type */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_RESP_TYPE);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
		wpsielen += 2;

		/*	Value: */
		wpsie[wpsielen++] = WPS_RESPONSE_TYPE_8021X;

		/*	UUID-E */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_UUID_E);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0010);
		wpsielen += 2;

		/*	Value: */
		if (pwdinfo->external_uuid == 0) {
			_rtw_memset(wpsie + wpsielen, 0x0, 16);
			_rtw_memcpy(wpsie + wpsielen, mac, ETH_ALEN);
		} else
			_rtw_memcpy(wpsie + wpsielen, pwdinfo->uuid, 0x10);
		wpsielen += 0x10;

		/*	Manufacturer */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_MANUFACTURER);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0007);
		wpsielen += 2;

		/*	Value: */
		_rtw_memcpy(wpsie + wpsielen, "Realtek", 7);
		wpsielen += 7;

		/*	Model Name */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_MODEL_NAME);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0006);
		wpsielen += 2;

		/*	Value: */
		_rtw_memcpy(wpsie + wpsielen, "8192CU", 6);
		wpsielen += 6;

		/*	Model Number */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_MODEL_NUMBER);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
		wpsielen += 2;

		/*	Value: */
		wpsie[wpsielen++] = 0x31;		/*	character 1 */

		/*	Serial Number */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_SERIAL_NUMBER);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(ETH_ALEN);
		wpsielen += 2;

		/*	Value: */
		_rtw_memcpy(wpsie + wpsielen, "123456" , ETH_ALEN);
		wpsielen += ETH_ALEN;

		/*	Primary Device Type */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_PRIMARY_DEV_TYPE);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0008);
		wpsielen += 2;

		/*	Value: */
		/*	Category ID */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA);
		wpsielen += 2;

		/*	OUI */
		*(u32 *)(wpsie + wpsielen) = cpu_to_be32(WPSOUI);
		wpsielen += 4;

		/*	Sub Category ID */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
		wpsielen += 2;

		/*	Device Name */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_DEVICE_NAME);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(pwdinfo->device_name_len);
		wpsielen += 2;

		/*	Value: */
		_rtw_memcpy(wpsie + wpsielen, pwdinfo->device_name, pwdinfo->device_name_len);
		wpsielen += pwdinfo->device_name_len;

		/*	Config Method */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_CONF_METHOD);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0002);
		wpsielen += 2;

		/*	Value: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(pwdinfo->supported_wps_cm);
		wpsielen += 2;


		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen);


		p2pielen = build_probe_resp_p2p_ie(pwdinfo, pframe);
		pframe += p2pielen;
		pattrib->pktlen += p2pielen;
	}

#ifdef CONFIG_WFD
	wfdielen = rtw_append_probe_resp_wfd_ie(padapter, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

/* Vendor Specific IE */
#ifdef CONFIG_APPEND_VENDOR_IE_ENABLE
	pattrib->pktlen += rtw_build_vendor_ie(padapter , &pframe , WIFI_P2P_PROBERESP_VENDOR_IE_BIT);
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;


	dump_mgntframe(padapter, pmgntframe);

	return;

}

int _issue_probereq_p2p(_adapter *padapter, u8 *da, int wait_ack)
{
	int ret = _FAIL;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	unsigned char			*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short		*fctrl;
	unsigned char			*mac;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	u8					wpsie[255] = { 0x00 }, p2pie[255] = { 0x00 };
	u16					wpsielen = 0, p2pielen = 0;
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif

	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);


	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	if (IS_CCK_RATE(pattrib->rate)) {
		/* force OFDM 6M rate */
		pattrib->rate = MGN_6M;
		pattrib->raid = rtw_get_mgntframe_raid(padapter, WIRELESS_11G);
	}

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	mac = adapter_mac_addr(padapter);

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	if (da) {
		_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, da, ETH_ALEN);
	} else {
		if ((pwdinfo->p2p_info.scan_op_ch_only) || (pwdinfo->rx_invitereq_info.scan_op_ch_only)) {
			/*	This two flags will be set when this is only the P2P client mode. */
			_rtw_memcpy(pwlanhdr->addr1, pwdinfo->p2p_peer_interface_addr, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, pwdinfo->p2p_peer_interface_addr, ETH_ALEN);
		} else {
			/*	broadcast probe request frame */
			_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, bc_addr, ETH_ALEN);
		}
	}
	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_PROBEREQ);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_PROVISION_DIS_REQ))
		pframe = rtw_set_ie(pframe, _SSID_IE_, pwdinfo->tx_prov_disc_info.ssid.SsidLength, pwdinfo->tx_prov_disc_info.ssid.Ssid, &(pattrib->pktlen));
	else
		pframe = rtw_set_ie(pframe, _SSID_IE_, P2P_WILDCARD_SSID_LEN, pwdinfo->p2p_wildcard_ssid, &(pattrib->pktlen));
	/*	Use the OFDM rate in the P2P probe request frame. ( 6(B), 9(B), 12(B), 24(B), 36, 48, 54 ) */
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, pwdinfo->support_rate, &pattrib->pktlen);

#ifdef CONFIG_IOCTL_CFG80211
	if (adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->driver_interface == DRIVER_CFG80211) {
		if (pmlmepriv->wps_probe_req_ie != NULL && pmlmepriv->p2p_probe_req_ie != NULL) {
			/* WPS IE */
			_rtw_memcpy(pframe, pmlmepriv->wps_probe_req_ie, pmlmepriv->wps_probe_req_ie_len);
			pattrib->pktlen += pmlmepriv->wps_probe_req_ie_len;
			pframe += pmlmepriv->wps_probe_req_ie_len;

			/* P2P IE */
			_rtw_memcpy(pframe, pmlmepriv->p2p_probe_req_ie, pmlmepriv->p2p_probe_req_ie_len);
			pattrib->pktlen += pmlmepriv->p2p_probe_req_ie_len;
			pframe += pmlmepriv->p2p_probe_req_ie_len;
		}
	} else
#endif /* CONFIG_IOCTL_CFG80211 */
	{

		/*	WPS IE */
		/*	Noted by Albert 20110221 */
		/*	According to the WPS specification, all the WPS attribute is presented by Big Endian. */

		wpsielen = 0;
		/*	WPS OUI */
		*(u32 *)(wpsie) = cpu_to_be32(WPSOUI);
		wpsielen += 4;

		/*	WPS version */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_VER1);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
		wpsielen += 2;

		/*	Value: */
		wpsie[wpsielen++] = WPS_VERSION_1;	/*	Version 1.0 */

		if (pmlmepriv->wps_probe_req_ie == NULL) {
			/*	UUID-E */
			/*	Type: */
			*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_UUID_E);
			wpsielen += 2;

			/*	Length: */
			*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0010);
			wpsielen += 2;

			/*	Value: */
			if (pwdinfo->external_uuid == 0) {
				_rtw_memset(wpsie + wpsielen, 0x0, 16);
				_rtw_memcpy(wpsie + wpsielen, mac, ETH_ALEN);
			} else
				_rtw_memcpy(wpsie + wpsielen, pwdinfo->uuid, 0x10);
			wpsielen += 0x10;

			/*	Config Method */
			/*	Type: */
			*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_CONF_METHOD);
			wpsielen += 2;

			/*	Length: */
			*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0002);
			wpsielen += 2;

			/*	Value: */
			*(u16 *)(wpsie + wpsielen) = cpu_to_be16(pwdinfo->supported_wps_cm);
			wpsielen += 2;
		}

		/*	Device Name */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_DEVICE_NAME);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(pwdinfo->device_name_len);
		wpsielen += 2;

		/*	Value: */
		_rtw_memcpy(wpsie + wpsielen, pwdinfo->device_name, pwdinfo->device_name_len);
		wpsielen += pwdinfo->device_name_len;

		/*	Primary Device Type */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_PRIMARY_DEV_TYPE);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0008);
		wpsielen += 2;

		/*	Value: */
		/*	Category ID */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_PDT_CID_RTK_WIDI);
		wpsielen += 2;

		/*	OUI */
		*(u32 *)(wpsie + wpsielen) = cpu_to_be32(WPSOUI);
		wpsielen += 4;

		/*	Sub Category ID */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_PDT_SCID_RTK_DMP);
		wpsielen += 2;

		/*	Device Password ID */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_DEVICE_PWID);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0002);
		wpsielen += 2;

		/*	Value: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_DPID_REGISTRAR_SPEC);	/*	Registrar-specified */
		wpsielen += 2;

		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen);

		/*	P2P OUI */
		p2pielen = 0;
		p2pie[p2pielen++] = 0x50;
		p2pie[p2pielen++] = 0x6F;
		p2pie[p2pielen++] = 0x9A;
		p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

		/*	Commented by Albert 20110221 */
		/*	According to the P2P Specification, the probe request frame should contain 5 P2P attributes */
		/*	1. P2P Capability */
		/*	2. P2P Device ID if this probe request wants to find the specific P2P device */
		/*	3. Listen Channel */
		/*	4. Extended Listen Timing */
		/*	5. Operating Channel if this WiFi is working as the group owner now */

		/*	P2P Capability */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_CAPABILITY;

		/*	Length: */
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0002);
		p2pielen += 2;

		/*	Value: */
		/*	Device Capability Bitmap, 1 byte */
		p2pie[p2pielen++] = DMP_P2P_DEVCAP_SUPPORT;

		/*	Group Capability Bitmap, 1 byte */
		if (pwdinfo->persistent_supported)
			p2pie[p2pielen++] = P2P_GRPCAP_PERSISTENT_GROUP | DMP_P2P_GRPCAP_SUPPORT;
		else
			p2pie[p2pielen++] = DMP_P2P_GRPCAP_SUPPORT;

		/*	Listen Channel */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_LISTEN_CH;

		/*	Length: */
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0005);
		p2pielen += 2;

		/*	Value: */
		/*	Country String */
		p2pie[p2pielen++] = 'X';
		p2pie[p2pielen++] = 'X';

		/*	The third byte should be set to 0x04. */
		/*	Described in the "Operating Channel Attribute" section. */
		p2pie[p2pielen++] = 0x04;

		/*	Operating Class */
		p2pie[p2pielen++] = 0x51;	/*	Copy from SD7 */

		/*	Channel Number */
		p2pie[p2pielen++] = pwdinfo->listen_channel;	/*	listen channel */


		/*	Extended Listen Timing */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_EX_LISTEN_TIMING;

		/*	Length: */
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0004);
		p2pielen += 2;

		/*	Value: */
		/*	Availability Period */
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0xFFFF);
		p2pielen += 2;

		/*	Availability Interval */
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0xFFFF);
		p2pielen += 2;

		if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
			/*	Operating Channel (if this WiFi is working as the group owner now) */
			/*	Type: */
			p2pie[p2pielen++] = P2P_ATTR_OPERATING_CH;

			/*	Length: */
			*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0005);
			p2pielen += 2;

			/*	Value: */
			/*	Country String */
			p2pie[p2pielen++] = 'X';
			p2pie[p2pielen++] = 'X';

			/*	The third byte should be set to 0x04. */
			/*	Described in the "Operating Channel Attribute" section. */
			p2pie[p2pielen++] = 0x04;

			/*	Operating Class */
			p2pie[p2pielen++] = 0x51;	/*	Copy from SD7 */

			/*	Channel Number */
			p2pie[p2pielen++] = pwdinfo->operating_channel;	/*	operating channel number */

		}

		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen);

	}

#ifdef CONFIG_WFD
	wfdielen = rtw_append_probe_req_wfd_ie(padapter, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

/* Vendor Specific IE */
#ifdef CONFIG_APPEND_VENDOR_IE_ENABLE
	pattrib->pktlen += rtw_build_vendor_ie(padapter , &pframe , WIFI_P2P_PROBEREQ_VENDOR_IE_BIT);
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;


	if (wait_ack)
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

inline void issue_probereq_p2p(_adapter *adapter, u8 *da)
{
	_issue_probereq_p2p(adapter, da, _FALSE);
}

/*
 * wait_ms == 0 means that there is no need to wait ack through C2H_CCX_TX_RPT
 * wait_ms > 0 means you want to wait ack through C2H_CCX_TX_RPT, and the value of wait_ms means the interval between each TX
 * try_cnt means the maximal TX count to try
 */
int issue_probereq_p2p_ex(_adapter *adapter, u8 *da, int try_cnt, int wait_ms)
{
	int ret;
	int i = 0;
	systime start = rtw_get_current_time();

	do {
		ret = _issue_probereq_p2p(adapter, da, wait_ms > 0 ? _TRUE : _FALSE);

		i++;

		if (RTW_CANNOT_RUN(adapter))
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			rtw_msleep_os(wait_ms);

	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
#ifndef DBG_XMIT_ACK
		goto exit;
#endif
	}

	if (try_cnt && wait_ms) {
		if (da)
			RTW_INFO(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(adapter), MAC_ARG(da), rtw_get_oper_ch(adapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
		else
			RTW_INFO(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(adapter), rtw_get_oper_ch(adapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
	}
exit:
	return ret;
}

#endif /* CONFIG_P2P */

s32 rtw_action_public_decache(union recv_frame *rframe, u8 token_offset)
{
	_adapter *adapter = rframe->u.hdr.adapter;
	struct mlme_ext_priv *mlmeext = &(adapter->mlmeextpriv);
	u8 *frame = rframe->u.hdr.rx_data;
	u16 seq_ctrl = ((rframe->u.hdr.attrib.seq_num & 0xffff) << 4) | (rframe->u.hdr.attrib.frag_num & 0xf);
	u8 token = *(rframe->u.hdr.rx_data + sizeof(struct rtw_ieee80211_hdr_3addr) + token_offset);

	if (GetRetry(frame)) {
		if ((seq_ctrl == mlmeext->action_public_rxseq)
		    && (token == mlmeext->action_public_dialog_token)
		   ) {
			RTW_INFO(FUNC_ADPT_FMT" seq_ctrl=0x%x, rxseq=0x%x, token:%d\n",
				FUNC_ADPT_ARG(adapter), seq_ctrl, mlmeext->action_public_rxseq, token);
			return _FAIL;
		}
	}

	/* TODO: per sta seq & token */
	mlmeext->action_public_rxseq = seq_ctrl;
	mlmeext->action_public_dialog_token = token;

	return _SUCCESS;
}

unsigned int on_action_public_p2p(union recv_frame *precv_frame)
{
	_adapter *padapter = precv_frame->u.hdr.adapter;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;
	u8 *frame_body;
#ifdef CONFIG_P2P
	u8 *p2p_ie;
	u32	p2p_ielen;
	struct	wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	u8	result = P2P_STATUS_SUCCESS;
	u8	empty_addr[ETH_ALEN] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	u8 *merged_p2pie = NULL;
	u32 merged_p2p_ielen = 0;
#ifdef CONFIG_CONCURRENT_MODE
	struct roch_info *prochinfo = &padapter->rochinfo;
#endif
#endif /* CONFIG_P2P */

	frame_body = (unsigned char *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));

#ifdef CONFIG_P2P
	_cancel_timer_ex(&pwdinfo->reset_ch_sitesurvey);
#ifdef CONFIG_IOCTL_CFG80211
	if (adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->driver_interface == DRIVER_CFG80211)
		rtw_cfg80211_rx_p2p_action_public(padapter, precv_frame);
	else
#endif /* CONFIG_IOCTL_CFG80211 */
	{
		/*	Do nothing if the driver doesn't enable the P2P function. */
		if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) || rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE))
			return _SUCCESS;

		len -= sizeof(struct rtw_ieee80211_hdr_3addr);

		switch (frame_body[6]) { /* OUI Subtype */
		case P2P_GO_NEGO_REQ: {
			RTW_INFO("[%s] Got GO Nego Req Frame\n", __FUNCTION__);
			_rtw_memset(&pwdinfo->groupid_info, 0x00, sizeof(struct group_id_info));

			if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_RX_PROVISION_DIS_REQ))
				rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));

			if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_FAIL)) {
				/*	Commented by Albert 20110526 */
				/*	In this case, this means the previous nego fail doesn't be reset yet. */
				_cancel_timer_ex(&pwdinfo->restore_p2p_state_timer);
				/*	Restore the previous p2p state */
				rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
				RTW_INFO("[%s] Restore the previous p2p state to %d\n", __FUNCTION__, rtw_p2p_state(pwdinfo));
			}
#ifdef CONFIG_CONCURRENT_MODE
			if (rtw_mi_buddy_check_fwstate(padapter, WIFI_ASOC_STATE))
				_cancel_timer_ex(&prochinfo->ap_roch_ch_switch_timer);
#endif /* CONFIG_CONCURRENT_MODE */

			/*	Commented by Kurt 20110902 */
			/* Add if statement to avoid receiving duplicate prov disc req. such that pre_p2p_state would be covered. */
			if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING))
				rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));

			/*	Commented by Kurt 20120113 */
			/*	Get peer_dev_addr here if peer doesn't issue prov_disc frame. */
			if (_rtw_memcmp(pwdinfo->rx_prov_disc_info.peerDevAddr, empty_addr, ETH_ALEN))
				_rtw_memcpy(pwdinfo->rx_prov_disc_info.peerDevAddr, get_addr2_ptr(pframe), ETH_ALEN);

			result = process_p2p_group_negotation_req(pwdinfo, frame_body, len);
			issue_p2p_GO_response(padapter, get_addr2_ptr(pframe), frame_body, len, result);

			/*	Commented by Albert 20110718 */
			/*	No matter negotiating or negotiation failure, the driver should set up the restore P2P state timer. */
#ifdef CONFIG_CONCURRENT_MODE
			/*	Commented by Albert 20120107 */
			_set_timer(&pwdinfo->restore_p2p_state_timer, 3000);
#else /* CONFIG_CONCURRENT_MODE */
			_set_timer(&pwdinfo->restore_p2p_state_timer, 5000);
#endif /* CONFIG_CONCURRENT_MODE */
			break;
		}
		case P2P_GO_NEGO_RESP: {
			RTW_INFO("[%s] Got GO Nego Resp Frame\n", __FUNCTION__);

			if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING)) {
				/*	Commented by Albert 20110425 */
				/*	The restore timer is enabled when issuing the nego request frame of rtw_p2p_connect function. */
				_cancel_timer_ex(&pwdinfo->restore_p2p_state_timer);
				pwdinfo->nego_req_info.benable = _FALSE;
				result = process_p2p_group_negotation_resp(pwdinfo, frame_body, len);
				issue_p2p_GO_confirm(pwdinfo->padapter, get_addr2_ptr(pframe), result);
				if (P2P_STATUS_SUCCESS == result) {
					if (rtw_p2p_role(pwdinfo) == P2P_ROLE_CLIENT) {
						pwdinfo->p2p_info.operation_ch[0] = pwdinfo->peer_operating_ch;
#ifdef CONFIG_P2P_OP_CHK_SOCIAL_CH
						pwdinfo->p2p_info.operation_ch[1] = 1;	/* Check whether GO is operating in channel 1; */
						pwdinfo->p2p_info.operation_ch[2] = 6;	/* Check whether GO is operating in channel 6; */
						pwdinfo->p2p_info.operation_ch[3] = 11;	/* Check whether GO is operating in channel 11; */
#endif /* CONFIG_P2P_OP_CHK_SOCIAL_CH */
						pwdinfo->p2p_info.scan_op_ch_only = 1;
						_set_timer(&pwdinfo->reset_ch_sitesurvey2, P2P_RESET_SCAN_CH);
					}
				}

				/*	Reset the dialog token for group negotiation frames. */
				pwdinfo->negotiation_dialog_token = 1;

				if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_FAIL))
					_set_timer(&pwdinfo->restore_p2p_state_timer, 5000);
			} else
				RTW_INFO("[%s] Skipped GO Nego Resp Frame (p2p_state != P2P_STATE_GONEGO_ING)\n", __FUNCTION__);

			break;
		}
		case P2P_GO_NEGO_CONF: {
			RTW_INFO("[%s] Got GO Nego Confirm Frame\n", __FUNCTION__);
			result = process_p2p_group_negotation_confirm(pwdinfo, frame_body, len);
			if (P2P_STATUS_SUCCESS == result) {
				if (rtw_p2p_role(pwdinfo) == P2P_ROLE_CLIENT) {
					pwdinfo->p2p_info.operation_ch[0] = pwdinfo->peer_operating_ch;
#ifdef CONFIG_P2P_OP_CHK_SOCIAL_CH
					pwdinfo->p2p_info.operation_ch[1] = 1;	/* Check whether GO is operating in channel 1; */
					pwdinfo->p2p_info.operation_ch[2] = 6;	/* Check whether GO is operating in channel 6; */
					pwdinfo->p2p_info.operation_ch[3] = 11;	/* Check whether GO is operating in channel 11; */
#endif /* CONFIG_P2P_OP_CHK_SOCIAL_CH */
					pwdinfo->p2p_info.scan_op_ch_only = 1;
					_set_timer(&pwdinfo->reset_ch_sitesurvey2, P2P_RESET_SCAN_CH);
				}
			}
			break;
		}
		case P2P_INVIT_REQ: {
			/*	Added by Albert 2010/10/05 */
			/*	Received the P2P Invite Request frame. */

			RTW_INFO("[%s] Got invite request frame!\n", __FUNCTION__);
			p2p_ie = rtw_get_p2p_ie(frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &p2p_ielen);
			if (p2p_ie) {
				/*	Parse the necessary information from the P2P Invitation Request frame. */
				/*	For example: The MAC address of sending this P2P Invitation Request frame. */
				u32	attr_contentlen = 0;
				u8	status_code = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
				struct group_id_info group_id;
				u8	invitation_flag = 0;

				merged_p2p_ielen = rtw_get_p2p_merged_ies_len(frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_);

				merged_p2pie = rtw_zmalloc(merged_p2p_ielen + 2);	/* 2 is for EID and Length */
				if (merged_p2pie == NULL) {
					RTW_INFO("[%s] Malloc p2p ie fail\n", __FUNCTION__);
					goto exit;
				}
				_rtw_memset(merged_p2pie, 0x00, merged_p2p_ielen);

				merged_p2p_ielen = rtw_p2p_merge_ies(frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, merged_p2pie);

				rtw_get_p2p_attr_content(merged_p2pie, merged_p2p_ielen, P2P_ATTR_INVITATION_FLAGS, &invitation_flag, &attr_contentlen);
				if (attr_contentlen) {

					rtw_get_p2p_attr_content(merged_p2pie, merged_p2p_ielen, P2P_ATTR_GROUP_BSSID, pwdinfo->p2p_peer_interface_addr, &attr_contentlen);
					/*	Commented by Albert 20120510 */
					/*	Copy to the pwdinfo->p2p_peer_interface_addr. */
					/*	So that the WFD UI ( or Sigma ) can get the peer interface address by using the following command. */
					/*	#> iwpriv wlan0 p2p_get peer_ifa */
					/*	After having the peer interface address, the sigma can find the correct conf file for wpa_supplicant. */

					if (attr_contentlen) {
						RTW_INFO("[%s] GO's BSSID = %.2X %.2X %.2X %.2X %.2X %.2X\n", __FUNCTION__,
							pwdinfo->p2p_peer_interface_addr[0], pwdinfo->p2p_peer_interface_addr[1],
							pwdinfo->p2p_peer_interface_addr[2], pwdinfo->p2p_peer_interface_addr[3],
							pwdinfo->p2p_peer_interface_addr[4], pwdinfo->p2p_peer_interface_addr[5]);
					}

					if (invitation_flag & P2P_INVITATION_FLAGS_PERSISTENT) {
						/*	Re-invoke the persistent group. */

						_rtw_memset(&group_id, 0x00, sizeof(struct group_id_info));
						rtw_get_p2p_attr_content(merged_p2pie, merged_p2p_ielen, P2P_ATTR_GROUP_ID, (u8 *) &group_id, &attr_contentlen);
						if (attr_contentlen) {
							if (_rtw_memcmp(group_id.go_device_addr, adapter_mac_addr(padapter), ETH_ALEN)) {
								/*	The p2p device sending this p2p invitation request wants this Wi-Fi device to be the persistent GO. */
								rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_GO);
								rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
								status_code = P2P_STATUS_SUCCESS;
							} else {
								/*	The p2p device sending this p2p invitation request wants to be the persistent GO. */
								if (is_matched_in_profilelist(pwdinfo->p2p_peer_interface_addr, &pwdinfo->profileinfo[0])) {
									u8 operatingch_info[5] = { 0x00 };
									if (rtw_get_p2p_attr_content(merged_p2pie, merged_p2p_ielen, P2P_ATTR_OPERATING_CH, operatingch_info,
										&attr_contentlen)) {
										if (rtw_chset_search_ch(adapter_to_chset(padapter), (u32)operatingch_info[4]) >= 0) {
											/*	The operating channel is acceptable for this device. */
											pwdinfo->rx_invitereq_info.operation_ch[0] = operatingch_info[4];
#ifdef CONFIG_P2P_OP_CHK_SOCIAL_CH
											pwdinfo->rx_invitereq_info.operation_ch[1] = 1;		/* Check whether GO is operating in channel 1; */
											pwdinfo->rx_invitereq_info.operation_ch[2] = 6;		/* Check whether GO is operating in channel 6; */
											pwdinfo->rx_invitereq_info.operation_ch[3] = 11;		/* Check whether GO is operating in channel 11; */
#endif /* CONFIG_P2P_OP_CHK_SOCIAL_CH */
											pwdinfo->rx_invitereq_info.scan_op_ch_only = 1;
											_set_timer(&pwdinfo->reset_ch_sitesurvey, P2P_RESET_SCAN_CH);
											rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_MATCH);
											rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
											status_code = P2P_STATUS_SUCCESS;
										} else {
											/*	The operating channel isn't supported by this device. */
											rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_DISMATCH);
											rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
											status_code = P2P_STATUS_FAIL_NO_COMMON_CH;
											_set_timer(&pwdinfo->restore_p2p_state_timer, 3000);
										}
									} else {
										/*	Commented by Albert 20121130 */
										/*	Intel will use the different P2P IE to store the operating channel information */
										/*	Workaround for Intel WiDi 3.5 */
										rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_MATCH);
										rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
										status_code = P2P_STATUS_SUCCESS;
									}
								} else {
									rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_DISMATCH);
									status_code = P2P_STATUS_FAIL_UNKNOWN_P2PGROUP;
								}
							}
						} else {
							RTW_INFO("[%s] P2P Group ID Attribute NOT FOUND!\n", __FUNCTION__);
							status_code = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
						}
					} else {
						/*	Received the invitation to join a P2P group. */

						_rtw_memset(&group_id, 0x00, sizeof(struct group_id_info));
						rtw_get_p2p_attr_content(merged_p2pie, merged_p2p_ielen, P2P_ATTR_GROUP_ID, (u8 *) &group_id, &attr_contentlen);
						if (attr_contentlen) {
							if (_rtw_memcmp(group_id.go_device_addr, adapter_mac_addr(padapter), ETH_ALEN)) {
								/*	In this case, the GO can't be myself. */
								rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_DISMATCH);
								status_code = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
							} else {
								/*	The p2p device sending this p2p invitation request wants to join an existing P2P group */
								/*	Commented by Albert 2012/06/28 */
								/*	In this case, this Wi-Fi device should use the iwpriv command to get the peer device address. */
								/*	The peer device address should be the destination address for the provisioning discovery request. */
								/*	Then, this Wi-Fi device should use the iwpriv command to get the peer interface address. */
								/*	The peer interface address should be the address for WPS mac address */
								_rtw_memcpy(pwdinfo->p2p_peer_device_addr, group_id.go_device_addr , ETH_ALEN);
								rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
								rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_JOIN);
								status_code = P2P_STATUS_SUCCESS;
							}
						} else {
							RTW_INFO("[%s] P2P Group ID Attribute NOT FOUND!\n", __FUNCTION__);
							status_code = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
						}
					}
				} else {
					RTW_INFO("[%s] P2P Invitation Flags Attribute NOT FOUND!\n", __FUNCTION__);
					status_code = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
				}

				RTW_INFO("[%s] status_code = %d\n", __FUNCTION__, status_code);

				pwdinfo->inviteresp_info.token = frame_body[7];
				issue_p2p_invitation_response(padapter, get_addr2_ptr(pframe), pwdinfo->inviteresp_info.token, status_code);
				_set_timer(&pwdinfo->restore_p2p_state_timer, 3000);
			}
			break;
		}
		case P2P_INVIT_RESP: {
			u8	attr_content = 0x00;
			u32	attr_contentlen = 0;

			RTW_INFO("[%s] Got invite response frame!\n", __FUNCTION__);
			_cancel_timer_ex(&pwdinfo->restore_p2p_state_timer);
			p2p_ie = rtw_get_p2p_ie(frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &p2p_ielen);
			if (p2p_ie) {
				rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, &attr_content, &attr_contentlen);

				if (attr_contentlen == 1) {
					RTW_INFO("[%s] Status = %d\n", __FUNCTION__, attr_content);
					pwdinfo->invitereq_info.benable = _FALSE;

					if (attr_content == P2P_STATUS_SUCCESS) {
						if (_rtw_memcmp(pwdinfo->invitereq_info.go_bssid, adapter_mac_addr(padapter), ETH_ALEN))
							rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
						else
							rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);

						rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_INVITE_RESP_OK);
					} else {
						rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
						rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_INVITE_RESP_FAIL);
					}
				} else {
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
					rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_INVITE_RESP_FAIL);
				}
			} else {
				rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
				rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_INVITE_RESP_FAIL);
			}

			if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_RX_INVITE_RESP_FAIL))
				_set_timer(&pwdinfo->restore_p2p_state_timer, 5000);
			break;
		}
		case P2P_DEVDISC_REQ:

			process_p2p_devdisc_req(pwdinfo, pframe, len);

			break;

		case P2P_DEVDISC_RESP:

			process_p2p_devdisc_resp(pwdinfo, pframe, len);

			break;

		case P2P_PROVISION_DISC_REQ:
			RTW_INFO("[%s] Got Provisioning Discovery Request Frame\n", __FUNCTION__);
			process_p2p_provdisc_req(pwdinfo, pframe, len);
			_rtw_memcpy(pwdinfo->rx_prov_disc_info.peerDevAddr, get_addr2_ptr(pframe), ETH_ALEN);

			/* 20110902 Kurt */
			/* Add the following statement to avoid receiving duplicate prov disc req. such that pre_p2p_state would be covered. */
			if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_RX_PROVISION_DIS_REQ))
				rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));

			rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_PROVISION_DIS_REQ);
			_set_timer(&pwdinfo->restore_p2p_state_timer, P2P_PROVISION_TIMEOUT);
			break;

		case P2P_PROVISION_DISC_RESP:
			/*	Commented by Albert 20110707 */
			/*	Should we check the pwdinfo->tx_prov_disc_info.bsent flag here?? */
			RTW_INFO("[%s] Got Provisioning Discovery Response Frame\n", __FUNCTION__);
			/*	Commented by Albert 20110426 */
			/*	The restore timer is enabled when issuing the provisioing request frame in rtw_p2p_prov_disc function. */
			_cancel_timer_ex(&pwdinfo->restore_p2p_state_timer);
			rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_PROVISION_DIS_RSP);
			process_p2p_provdisc_resp(pwdinfo, pframe);
			_set_timer(&pwdinfo->restore_p2p_state_timer, P2P_PROVISION_TIMEOUT);
			break;

		}
	}


exit:

	if (merged_p2pie)
		rtw_mfree(merged_p2pie, merged_p2p_ielen + 2);
#endif /* CONFIG_P2P */
	return _SUCCESS;
}

unsigned int on_action_public_vendor(union recv_frame *precv_frame)
{
	unsigned int ret = _FAIL;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	u8 *frame_body = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	_adapter *adapter = precv_frame->u.hdr.adapter;
	int cnt = 0;
	char msg[64];

	if (_rtw_memcmp(frame_body + 2, P2P_OUI, 4) == _TRUE) {
		if (rtw_action_public_decache(precv_frame, 7) == _FAIL)
			goto exit;

		if (!hal_chk_wl_func(precv_frame->u.hdr.adapter, WL_FUNC_MIRACAST))
			rtw_rframe_del_wfd_ie(precv_frame, 8);

		ret = on_action_public_p2p(precv_frame);
	} else if (_rtw_memcmp(frame_body + 2, DPP_OUI, 4) == _TRUE) {
		u8 dpp_type = frame_body[7];

#ifdef CONFIG_IOCTL_CFG80211
		cnt += sprintf((msg + cnt), "DPP(type:%u)", dpp_type);
		rtw_cfg80211_rx_action(adapter, precv_frame, msg);
#endif
	}

exit:
	return ret;
}

unsigned int on_action_public_default(union recv_frame *precv_frame, u8 action)
{
	unsigned int ret = _FAIL;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	u8 *frame_body = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	u8 token;
	_adapter *adapter = precv_frame->u.hdr.adapter;
	int cnt = 0;
	char msg[64];

	token = frame_body[2];

	if (rtw_action_public_decache(precv_frame, 2) == _FAIL)
		goto exit;

#ifdef CONFIG_IOCTL_CFG80211
	cnt += sprintf((msg + cnt), "%s(token:%u)", action_public_str(action), token);
	rtw_cfg80211_rx_action(adapter, precv_frame, msg);
#endif

	ret = _SUCCESS;

exit:
	return ret;
}

unsigned int on_action_public(_adapter *padapter, union recv_frame *precv_frame)
{
	unsigned int ret = _FAIL;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint frame_len = precv_frame->u.hdr.len;
	u8 *frame_body = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	u8 category, action;

	/* check RA matches or broadcast */
	if (!(_rtw_memcmp(adapter_mac_addr(padapter), GetAddr1Ptr(pframe), ETH_ALEN) ||
		is_broadcast_mac_addr(GetAddr1Ptr(pframe))))
		goto exit;

	category = frame_body[0];
	if (category != RTW_WLAN_CATEGORY_PUBLIC)
		goto exit;

	action = frame_body[1];
	switch (action) {
	case ACT_PUBLIC_BSSCOEXIST:
#ifdef CONFIG_80211N_HT
#ifdef CONFIG_AP_MODE
		/*20/40 BSS Coexistence Management frame is a Public Action frame*/
		if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE)
			rtw_process_public_act_bsscoex(padapter, pframe, frame_len);
#endif /*CONFIG_AP_MODE*/
#endif /*CONFIG_80211N_HT*/
		break;
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

unsigned int OnAction_ft(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_RTW_80211R
	u32	ret = _FAIL;
	u32	frame_len = 0;
	u8	action_code = 0;
	u8	category = 0;
	u8	*pframe = NULL;
	u8	*pframe_body = NULL;
	u8 	tgt_addr[ETH_ALEN];
	u8	*pie = NULL;
	u32	ft_ie_len = 0;
	u32 status_code = 0;
	struct mlme_ext_priv *pmlmeext = NULL;
	struct mlme_ext_info *pmlmeinfo = NULL;
	struct mlme_priv *pmlmepriv = NULL;
	struct wlan_network *proam_target = NULL;
	struct ft_roam_info *pft_roam = NULL;
	_irqL  irqL;

	pmlmeext = &(padapter->mlmeextpriv);
	pmlmeinfo = &(pmlmeext->mlmext_info);
	pmlmepriv = &(padapter->mlmepriv);
	pft_roam = &(pmlmepriv->ft_roam);
	pframe = precv_frame->u.hdr.rx_data;
	frame_len = precv_frame->u.hdr.len;
	pframe_body = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	category = pframe_body[0];

	if (category != RTW_WLAN_CATEGORY_FT)
		goto exit;

	action_code = pframe_body[1];
	switch (action_code) {
	case RTW_WLAN_ACTION_FT_RSP:
		RTW_INFO("FT: RTW_WLAN_ACTION_FT_RSP recv.\n");
		if (!_rtw_memcmp(adapter_mac_addr(padapter), &pframe_body[2], ETH_ALEN)) {
			RTW_ERR("FT: Unmatched STA MAC Address "MAC_FMT"\n", MAC_ARG(&pframe_body[2]));
			goto exit;
		}

		status_code = le16_to_cpu(*(u16 *)((SIZE_PTR)pframe +  sizeof(struct rtw_ieee80211_hdr_3addr) + 14));
		if (status_code != 0) {
			RTW_ERR("FT: WLAN ACTION FT RESPONSE fail, status: %d\n", status_code);
			goto exit;
		}

		_rtw_memcpy(tgt_addr, &pframe_body[8], ETH_ALEN);
		if (is_zero_mac_addr(tgt_addr) || is_broadcast_mac_addr(tgt_addr)) {
			RTW_ERR("FT: Invalid Target MAC Address "MAC_FMT"\n", MAC_ARG(tgt_addr));
			goto exit;
		}

		pie = rtw_get_ie(pframe_body, _MDIE_, &ft_ie_len, frame_len);
		if (pie) {
			if (!_rtw_memcmp(&pft_roam->mdid, pie+2, 2)) {
				RTW_ERR("FT: Invalid MDID\n");
				goto exit;
			}
		}

		rtw_ft_set_status(padapter, RTW_FT_REQUESTED_STA);
		_cancel_timer_ex(&pmlmeext->ft_link_timer);

		/*Disconnect current AP*/
		receive_disconnect(padapter, pmlmepriv->cur_network.network.MacAddress, WLAN_REASON_ACTIVE_ROAM, _FALSE);

		pft_roam->ft_action_len = frame_len;
		_rtw_memcpy(pft_roam->ft_action, pframe, rtw_min(frame_len, RTW_FT_MAX_IE_SZ));
		ret = _SUCCESS;
		break;
	case RTW_WLAN_ACTION_FT_REQ:
	case RTW_WLAN_ACTION_FT_CONF:
	case RTW_WLAN_ACTION_FT_ACK:
	default:
		RTW_ERR("FT: Unsupported FT Action!\n");
		break;
	}

exit:
	return ret;
#else
	return _SUCCESS;
#endif
}

unsigned int OnAction_ht(_adapter *padapter, union recv_frame *precv_frame)
{
	u8 *pframe = precv_frame->u.hdr.rx_data;
	u8 *frame_body = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	u8 category, action;

	/* check RA matches or not */
	if (!_rtw_memcmp(adapter_mac_addr(padapter), GetAddr1Ptr(pframe), ETH_ALEN))
		goto exit;

	category = frame_body[0];
	if (category != RTW_WLAN_CATEGORY_HT)
		goto exit;

	action = frame_body[1];
	switch (action) {
	case RTW_WLAN_ACTION_HT_SM_PS:
#ifdef CONFIG_80211N_HT
#ifdef CONFIG_AP_MODE
		if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE)
			rtw_process_ht_action_smps(padapter, get_addr2_ptr(pframe), frame_body[2]);
#endif /*CONFIG_AP_MODE*/
#endif /*CONFIG_80211N_HT*/
		break;
	case RTW_WLAN_ACTION_HT_COMPRESS_BEAMFORMING:
#ifdef CONFIG_BEAMFORMING
		/*RTW_INFO("RTW_WLAN_ACTION_HT_COMPRESS_BEAMFORMING\n");*/
		rtw_beamforming_get_report_frame(padapter, precv_frame);
#endif /*CONFIG_BEAMFORMING*/
		break;
	default:
		break;
	}

exit:

	return _SUCCESS;
}

#ifdef CONFIG_IEEE80211W
unsigned int OnAction_sa_query(_adapter *padapter, union recv_frame *precv_frame)
{
	u8 *pframe = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct sta_info		*psta;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u16 tid;
	/* Baron */

	RTW_INFO("OnAction_sa_query\n");

	switch (pframe[WLAN_HDR_A3_LEN + 1]) {
	case 0: /* SA Query req */
		_rtw_memcpy(&tid, &pframe[WLAN_HDR_A3_LEN + 2], sizeof(u16));
		RTW_INFO("OnAction_sa_query request,action=%d, tid=%04x, pframe=%02x-%02x\n"
			, pframe[WLAN_HDR_A3_LEN + 1], tid, pframe[WLAN_HDR_A3_LEN + 2], pframe[WLAN_HDR_A3_LEN + 3]);
		issue_action_SA_Query(padapter, get_addr2_ptr(pframe), 1, tid, IEEE80211W_RIGHT_KEY);
		break;

	case 1: /* SA Query rsp */
		psta = rtw_get_stainfo(pstapriv, get_addr2_ptr(pframe));
		if (psta != NULL)
			_cancel_timer_ex(&psta->dot11w_expire_timer);

		_rtw_memcpy(&tid, &pframe[WLAN_HDR_A3_LEN + 2], sizeof(u16));
		RTW_INFO("OnAction_sa_query response,action=%d, tid=%04x, cancel timer\n", pframe[WLAN_HDR_A3_LEN + 1], tid);
		break;
	default:
		break;
	}
	if (0) {
		int pp;
		printk("pattrib->pktlen = %d =>", pattrib->pkt_len);
		for (pp = 0; pp < pattrib->pkt_len; pp++)
			printk(" %02x ", pframe[pp]);
		printk("\n");
	}

	return _SUCCESS;
}
#endif /* CONFIG_IEEE80211W */

unsigned int on_action_rm(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_RTW_80211K
	return rm_on_action(padapter, precv_frame);
#else
	return _SUCCESS;
#endif  /* CONFIG_RTW_80211K */
}

unsigned int OnAction_wmm(_adapter *padapter, union recv_frame *precv_frame)
{
	return _SUCCESS;
}

unsigned int OnAction_vht(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_80211AC_VHT
	u8 *pframe = precv_frame->u.hdr.rx_data;
	struct rtw_ieee80211_hdr_3addr *whdr = (struct rtw_ieee80211_hdr_3addr *)pframe;
	u8 *frame_body = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	u8 category, action;
	struct sta_info *psta = NULL;

	/* check RA matches or not */
	if (!_rtw_memcmp(adapter_mac_addr(padapter), GetAddr1Ptr(pframe), ETH_ALEN))
		goto exit;

	category = frame_body[0];
	if (category != RTW_WLAN_CATEGORY_VHT)
		goto exit;

	action = frame_body[1];
	switch (action) {
	case RTW_WLAN_ACTION_VHT_COMPRESSED_BEAMFORMING:
#ifdef CONFIG_BEAMFORMING
		/*RTW_INFO("RTW_WLAN_ACTION_VHT_COMPRESSED_BEAMFORMING\n");*/
		rtw_beamforming_get_report_frame(padapter, precv_frame);
#endif /*CONFIG_BEAMFORMING*/
		break;
	case RTW_WLAN_ACTION_VHT_OPMODE_NOTIFICATION:
		/* CategoryCode(1) + ActionCode(1) + OpModeNotification(1) */
		/* RTW_INFO("RTW_WLAN_ACTION_VHT_OPMODE_NOTIFICATION\n"); */
		psta = rtw_get_stainfo(&padapter->stapriv, whdr->addr2);
		if (psta)
			rtw_process_vht_op_mode_notify(padapter, &frame_body[2], psta);
		break;
	case RTW_WLAN_ACTION_VHT_GROUPID_MANAGEMENT:
#ifdef CONFIG_BEAMFORMING
#ifdef RTW_BEAMFORMING_VERSION_2
		rtw_beamforming_get_vht_gid_mgnt_frame(padapter, precv_frame);
#endif /* RTW_BEAMFORMING_VERSION_2 */
#endif /* CONFIG_BEAMFORMING */
		break;
	default:
		break;
	}

exit:
#endif /* CONFIG_80211AC_VHT */

	return _SUCCESS;
}

unsigned int OnAction_p2p(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_P2P
	u8 *frame_body;
	u8 category, OUI_Subtype, dialogToken = 0;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;
	struct	wifidirect_info	*pwdinfo = &(padapter->wdinfo);

	/* check RA matches or not */
	if (!_rtw_memcmp(adapter_mac_addr(padapter), GetAddr1Ptr(pframe), ETH_ALEN))
		return _SUCCESS;

	frame_body = (unsigned char *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));

	category = frame_body[0];
	if (category != RTW_WLAN_CATEGORY_P2P)
		return _SUCCESS;

	if (cpu_to_be32(*((u32 *)(frame_body + 1))) != P2POUI)
		return _SUCCESS;

#ifdef CONFIG_IOCTL_CFG80211
	if (adapter_wdev_data(padapter)->p2p_enabled
		&& pwdinfo->driver_interface == DRIVER_CFG80211
	) {
		rtw_cfg80211_rx_action_p2p(padapter, precv_frame);
		return _SUCCESS;
	} else
#endif /* CONFIG_IOCTL_CFG80211 */
	{
		len -= sizeof(struct rtw_ieee80211_hdr_3addr);
		OUI_Subtype = frame_body[5];
		dialogToken = frame_body[6];

		switch (OUI_Subtype) {
		case P2P_NOTICE_OF_ABSENCE:

			break;

		case P2P_PRESENCE_REQUEST:

			process_p2p_presence_req(pwdinfo, pframe, len);

			break;

		case P2P_PRESENCE_RESPONSE:

			break;

		case P2P_GO_DISC_REQUEST:

			break;

		default:
			break;

		}
	}
#endif /* CONFIG_P2P */

	return _SUCCESS;

}

#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
unsigned int OnAction_tbtx_token(_adapter *padapter, union recv_frame *precv_frame)
{
#define TOKEN_REQ 0x00
#define TOKEN_REL 0x01

	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;
	u32 xmit_time;
	u8 *src=NULL, *pframe = precv_frame->u.hdr.rx_data;
	u8 tbtx_action_code;
	u8 i, nr_send;
	uint tx_duration = 0;

	if (padapter->tbtx_capability == _FALSE)
		goto exit;

	tbtx_action_code = *(pframe + WLAN_HDR_A3_LEN + 1);


	switch (tbtx_action_code)
	{
		case TOKEN_REQ:
			// parse duration
			tx_duration = le32_to_cpu(*(uint *)(pframe + WLAN_HDR_A3_LEN + 2));
			padapter->tbtx_duration = tx_duration/1000; // Mirocsecond to Millisecond
			ATOMIC_SET(&padapter->tbtx_tx_pause, _FALSE);
			rtw_tx_control_cmd(padapter);
			_set_timer(&pmlmeext->tbtx_xmit_timer, padapter->tbtx_duration);
			ATOMIC_SET(&padapter->tbtx_remove_tx_pause, _FALSE);
#if defined(CONFIG_SDIO_HCI) && !defined(CONFIG_SDIO_TX_TASKLET)
			_rtw_up_sema(&pxmitpriv->SdioXmitSema);
#else
			tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
#endif
			break;
#ifdef CONFIG_AP_MODE
		case TOKEN_REL:
			src = get_addr2_ptr(pframe);
			if (!src)
				goto exit;
			psta = rtw_get_stainfo(&padapter->stapriv, src);
			if (!psta)
				goto exit;

			if (ATOMIC_READ(&pstapriv->nr_token_keeper) < 1)
				goto exit;

			for (i=0; i< NR_MAXSTA_INSLOT; i++) {
				if (pstapriv->token_holder[i] == psta) {
					pstapriv->token_holder[i] = NULL;
					//RTW_INFO("macaddr1:" MAC_FMT "\n", MAC_ARG(psta->cmn.mac_addr));
					ATOMIC_DEC(&pstapriv->nr_token_keeper);
					break;
				}
			}

			if (ATOMIC_READ(&pstapriv->nr_token_keeper) == 0)
				_set_timer(&pmlmeext->tbtx_token_dispatch_timer, 1);

			break;
#endif
		default:
			RTW_INFO("Undefined Action Code\n");
			goto exit;
			break;
	}

exit:
	return _SUCCESS;
}

void rtw_issue_action_token_rel(_adapter *padapter)
{

	// Todo: 
	// gen token
	/* Token Release Format
		Category code : 	1 Byte
		Action code : 		1 Byte */
	int ret = _FAIL;
	//u16	*fctrl;
	u8	val = 0x01;
	u8	category = RTW_WLAN_CATEGORY_TBTX;
	u8	*pframe;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	struct	mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	//struct sta_info			*psta;
	//struct sta_priv			*pstapriv = &padapter->stapriv;
	//struct registry_priv		*pregpriv = &padapter->registrypriv;
	
	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		return;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return;
	
	/*update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->rate = MGN_24M; /* issue action release using OFDM rate? 20190716 Bruce add */ 

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);
	
	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	//fctrl = &(pwlanhdr->frame_ctl);
	//*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	// SetSeqNum??
	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(val), &(pattrib->pktlen));

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	//RTW_INFO("%s\n", __func__);

}
#endif

unsigned int OnAction(_adapter *padapter, union recv_frame *precv_frame)
{
	int i;
	unsigned char	category;
	struct action_handler *ptable;
	unsigned char	*frame_body;
	u8 *pframe = precv_frame->u.hdr.rx_data;

	frame_body = (unsigned char *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));

	category = frame_body[0];

	for (i = 0; i < sizeof(OnAction_tbl) / sizeof(struct action_handler); i++) {
		ptable = &OnAction_tbl[i];

		if (category == ptable->num)
			ptable->func(padapter, precv_frame);

	}

	return _SUCCESS;

}

unsigned int DoReserved(_adapter *padapter, union recv_frame *precv_frame)
{

	/* RTW_INFO("rcvd mgt frame(%x, %x)\n", (get_frame_sub_type(pframe) >> 4), *(unsigned int *)GetAddr1Ptr(pframe)); */
	return _SUCCESS;
}

struct xmit_frame *_alloc_mgtxmitframe(struct xmit_priv *pxmitpriv, bool once)
{
	struct xmit_frame *pmgntframe;
	struct xmit_buf *pxmitbuf;

	if (once)
		pmgntframe = rtw_alloc_xmitframe_once(pxmitpriv);
	else
		pmgntframe = rtw_alloc_xmitframe_ext(pxmitpriv);

	if (pmgntframe == NULL) {
		RTW_INFO(FUNC_ADPT_FMT" alloc xmitframe fail, once:%d\n", FUNC_ADPT_ARG(pxmitpriv->adapter), once);
		goto exit;
	}

	pxmitbuf = rtw_alloc_xmitbuf_ext(pxmitpriv);
	if (pxmitbuf == NULL) {
		RTW_INFO(FUNC_ADPT_FMT" alloc xmitbuf fail\n", FUNC_ADPT_ARG(pxmitpriv->adapter));
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		pmgntframe = NULL;
		goto exit;
	}

	pmgntframe->frame_tag = MGNT_FRAMETAG;
	pmgntframe->pxmitbuf = pxmitbuf;
	pmgntframe->buf_addr = pxmitbuf->pbuf;
	pxmitbuf->priv_data = pmgntframe;

exit:
	return pmgntframe;

}

inline struct xmit_frame *alloc_mgtxmitframe(struct xmit_priv *pxmitpriv)
{
	return _alloc_mgtxmitframe(pxmitpriv, _FALSE);
}

inline struct xmit_frame *alloc_mgtxmitframe_once(struct xmit_priv *pxmitpriv)
{
	return _alloc_mgtxmitframe(pxmitpriv, _TRUE);
}


/****************************************************************************

Following are some TX fuctions for WiFi MLME

*****************************************************************************/

void update_mgnt_tx_rate(_adapter *padapter, u8 rate)
{
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);

	pmlmeext->tx_rate = rate;
	/* RTW_INFO("%s(): rate = %x\n",__FUNCTION__, rate); */
}


void update_monitor_frame_attrib(_adapter *padapter, struct pkt_attrib *pattrib)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8	wireless_mode;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct xmit_priv		*pxmitpriv = &padapter->xmitpriv;
	struct sta_info		*psta = NULL;
	struct sta_priv		*pstapriv = &padapter->stapriv;

	psta = rtw_get_stainfo(pstapriv, pattrib->ra);

	pattrib->hdrlen = 24;
	pattrib->nr_frags = 1;
	pattrib->priority = 7;
	pattrib->mac_id = RTW_DEFAULT_MGMT_MACID;
	pattrib->qsel = QSLT_MGNT;

	pattrib->pktlen = 0;

	if (pmlmeext->tx_rate == IEEE80211_CCK_RATE_1MB)
		wireless_mode = WIRELESS_11B;
	else
		wireless_mode = WIRELESS_11G;

	pattrib->raid = rtw_get_mgntframe_raid(padapter, wireless_mode);
#ifdef CONFIG_80211AC_VHT
	if (pHalData->rf_type == RF_1T1R)
		pattrib->raid = RATEID_IDX_VHT_1SS;
	else if (pHalData->rf_type == RF_2T2R || pHalData->rf_type == RF_2T4R)
		pattrib->raid = RATEID_IDX_VHT_2SS;
	else if (pHalData->rf_type == RF_3T3R)
		pattrib->raid = RATEID_IDX_VHT_3SS;
	else
		pattrib->raid = RATEID_IDX_BGN_40M_1SS;
#endif

#ifdef CONFIG_80211AC_VHT
	pattrib->rate = MGN_VHT1SS_MCS9;
#else
	pattrib->rate = MGN_MCS7;
#endif

	pattrib->encrypt = _NO_PRIVACY_;
	pattrib->bswenc = _FALSE;

	pattrib->qos_en = _FALSE;
	pattrib->ht_en = 1;
	pattrib->bwmode = CHANNEL_WIDTH_20;
	pattrib->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	pattrib->sgi = _FALSE;

	pattrib->seqnum = pmlmeext->mgnt_seq;

	pattrib->retry_ctrl = _TRUE;

	pattrib->mbssid = 0;
	pattrib->hw_ssn_sel = pxmitpriv->hw_ssn_seq_no;

}

#ifdef CONFIG_RTW_MGMT_QUEUE
void update_mgntframe_subtype(_adapter *padapter, struct xmit_frame *pmgntframe)
{
	struct pkt_attrib *pattrib = &pmgntframe->attrib;
	u8 *pframe;
	u8 subtype, category ,action;

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	subtype = get_frame_sub_type(pframe); /* bit(7)~bit(2) */
	pattrib->subtype = subtype;

	rtw_action_frame_parse(pframe, pattrib->pktlen, &category, &action);

	if ((subtype == WIFI_ACTION && !(action == ACT_PUBLIC_FTM_REQ || action == ACT_PUBLIC_FTM)) ||
		subtype == WIFI_DISASSOC || subtype == WIFI_DEAUTH ||
		(subtype == WIFI_PROBERSP && MLME_IS_ADHOC(padapter)))
		pattrib->ps_dontq = 0;
	else
		pattrib->ps_dontq = 1;
}
#endif

void update_mgntframe_attrib(_adapter *padapter, struct pkt_attrib *pattrib)
{
	u8	wireless_mode;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct xmit_priv		*pxmitpriv = &padapter->xmitpriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;

#ifdef CONFIG_P2P_PS_NOA_USE_MACID_SLEEP
	struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
#endif /* CONFIG_P2P_PS_NOA_USE_MACID_SLEEP */

	/* _rtw_memset((u8 *)(pattrib), 0, sizeof(struct pkt_attrib)); */

	pattrib->hdrlen = 24;
	pattrib->nr_frags = 1;
	pattrib->priority = 7;
	pattrib->mac_id = RTW_DEFAULT_MGMT_MACID;
	pattrib->qsel = QSLT_MGNT;

#ifdef CONFIG_MCC_MODE
	update_mcc_mgntframe_attrib(padapter, pattrib);
#endif


#ifdef CONFIG_P2P_PS_NOA_USE_MACID_SLEEP
#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_buddy_check_fwstate(padapter, WIFI_ASOC_STATE))
#endif /* CONFIG_CONCURRENT_MODE */
		if (MLME_IS_GC(padapter)) {
			if (pwdinfo->p2p_ps_mode > P2P_PS_NONE) {
				struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
				struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
				WLAN_BSSID_EX *cur_network = &(pmlmeinfo->network);

				psta = rtw_get_stainfo(pstapriv, cur_network->MacAddress);
				if (psta) {
					/* use macid sleep during NoA, mgmt frame use ac queue & ap macid */
					pattrib->mac_id = psta->cmn.mac_id;
					pattrib->qsel = QSLT_VO;
				} else {
					if (pwdinfo->p2p_ps_state != P2P_PS_DISABLE)
						RTW_ERR("%s , psta was NULL\n", __func__);
				}
			}
		}
#endif /* CONFIG_P2P_PS_NOA_USE_MACID_SLEEP */

	pattrib->pktlen = 0;

	if (IS_CCK_RATE(pmlmeext->tx_rate))
		wireless_mode = WIRELESS_11B;
	else
		wireless_mode = WIRELESS_11G;
	pattrib->raid =  rtw_get_mgntframe_raid(padapter, wireless_mode);
	pattrib->rate = pmlmeext->tx_rate;

	pattrib->encrypt = _NO_PRIVACY_;
	pattrib->bswenc = _FALSE;

	pattrib->qos_en = _FALSE;
	pattrib->ht_en = _FALSE;
	pattrib->bwmode = CHANNEL_WIDTH_20;
	pattrib->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	pattrib->sgi = _FALSE;

	pattrib->seqnum = pmlmeext->mgnt_seq;

	pattrib->retry_ctrl = _TRUE;

	pattrib->mbssid = 0;
	pattrib->hw_ssn_sel = pxmitpriv->hw_ssn_seq_no;
#ifdef CONFIG_RTW_MGMT_QUEUE
	pattrib->ps_dontq = 1;
#endif
}

void update_mgntframe_attrib_addr(_adapter *padapter, struct xmit_frame *pmgntframe)
{
	u8 *pframe;
	struct pkt_attrib *pattrib = &pmgntframe->attrib;
#if defined(CONFIG_BEAMFORMING) || defined(CONFIG_ANTENNA_DIVERSITY) || defined(CONFIG_RTW_MGMT_QUEUE)
	struct sta_info *sta = NULL;
#endif

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	_rtw_memcpy(pattrib->ra, GetAddr1Ptr(pframe), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, get_addr2_ptr(pframe), ETH_ALEN);

#if defined(CONFIG_BEAMFORMING) || defined(CONFIG_ANTENNA_DIVERSITY) || defined(CONFIG_RTW_MGMT_QUEUE)
	sta = pattrib->psta;
	if (!sta) {
		sta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
		pattrib->psta = sta;
	}

	#ifdef CONFIG_BEAMFORMING
	if (sta)
		update_attrib_txbf_info(padapter, pattrib, sta);
	#endif
#endif /* defined(CONFIG_BEAMFORMING) || defined(CONFIG_ANTENNA_DIVERSITY) || defined(CONFIG_RTW_MGMT_QUEUE) */
}

void dump_mgntframe(_adapter *padapter, struct xmit_frame *pmgntframe)
{
	if (RTW_CANNOT_RUN(padapter)) {
		rtw_free_xmitbuf(&padapter->xmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(&padapter->xmitpriv, pmgntframe);
		return;
	}

	rtw_hal_mgnt_xmit(padapter, pmgntframe);
}

s32 dump_mgntframe_and_wait(_adapter *padapter, struct xmit_frame *pmgntframe, int timeout_ms)
{
	s32 ret = _FAIL;
	_irqL irqL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct xmit_buf *pxmitbuf = pmgntframe->pxmitbuf;
	struct submit_ctx sctx;

	if (RTW_CANNOT_RUN(padapter)) {
		rtw_free_xmitbuf(&padapter->xmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(&padapter->xmitpriv, pmgntframe);
		return ret;
	}

	rtw_sctx_init(&sctx, timeout_ms);
	pxmitbuf->sctx = &sctx;

	ret = rtw_hal_mgnt_xmit(padapter, pmgntframe);

	if (ret == _SUCCESS
#ifdef CONFIG_RTW_MGMT_QUEUE
	|| ret == RTW_QUEUE_MGMT
#endif
	)
		ret = rtw_sctx_wait(&sctx, __func__);

	_enter_critical(&pxmitpriv->lock_sctx, &irqL);
	pxmitbuf->sctx = NULL;
	_exit_critical(&pxmitpriv->lock_sctx, &irqL);

	return ret;
}

s32 dump_mgntframe_and_wait_ack_timeout(_adapter *padapter, struct xmit_frame *pmgntframe, int timeout_ms)
{
#ifdef CONFIG_XMIT_ACK
	static u8 seq_no = 0;
	s32 ret = _FAIL;
	struct xmit_priv	*pxmitpriv = &(GET_PRIMARY_ADAPTER(padapter))->xmitpriv;

	if (RTW_CANNOT_RUN(padapter)) {
		rtw_free_xmitbuf(&padapter->xmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(&padapter->xmitpriv, pmgntframe);
		return -1;
	}

	_enter_critical_mutex(&pxmitpriv->ack_tx_mutex, NULL);
	pxmitpriv->ack_tx = _TRUE;
	pxmitpriv->seq_no = seq_no++;
	pmgntframe->ack_report = 1;
	rtw_sctx_init(&(pxmitpriv->ack_tx_ops), timeout_ms);

	ret = rtw_hal_mgnt_xmit(padapter, pmgntframe);

	if (ret == _SUCCESS
#ifdef CONFIG_RTW_MGMT_QUEUE
	|| ret == RTW_QUEUE_MGMT
#endif
	)
		ret = rtw_sctx_wait(&(pxmitpriv->ack_tx_ops), __func__);

	pxmitpriv->ack_tx = _FALSE;
	_exit_critical_mutex(&pxmitpriv->ack_tx_mutex, NULL);

	return ret;
#else /* !CONFIG_XMIT_ACK */
	dump_mgntframe(padapter, pmgntframe);
	rtw_msleep_os(50);
	return _SUCCESS;
#endif /* !CONFIG_XMIT_ACK */
}

s32 dump_mgntframe_and_wait_ack(_adapter *padapter, struct xmit_frame *pmgntframe)
{
	/* In this case, use 500 ms as the default wait_ack timeout */
	return dump_mgntframe_and_wait_ack_timeout(padapter, pmgntframe, 500);
}


int update_hidden_ssid(u8 *ies, u32 ies_len, u8 hidden_ssid_mode)
{
	u8 *ssid_ie;
	sint ssid_len_ori;
	int len_diff = 0;

	ssid_ie = rtw_get_ie(ies,  WLAN_EID_SSID, &ssid_len_ori, ies_len);

	/* RTW_INFO("%s hidden_ssid_mode:%u, ssid_ie:%p, ssid_len_ori:%d\n", __FUNCTION__, hidden_ssid_mode, ssid_ie, ssid_len_ori); */

	if (ssid_ie && ssid_len_ori > 0) {
		switch (hidden_ssid_mode) {
		case 1: {
			u8 *next_ie = ssid_ie + 2 + ssid_len_ori;
			u32 remain_len = 0;

			remain_len = ies_len - (next_ie - ies);

			ssid_ie[1] = 0;
			_rtw_memcpy(ssid_ie + 2, next_ie, remain_len);
			len_diff -= ssid_len_ori;

			break;
		}
		case 2:
			_rtw_memset(&ssid_ie[2], 0, ssid_len_ori);
			break;
		default:
			break;
		}
	}

	return len_diff;
}

#ifdef CONFIG_AP_MODE
void issue_beacon(_adapter *padapter, int timeout_ms)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	unsigned char	*pframe;
	struct rtw_ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	unsigned int	rate_len;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
#if defined(CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	_irqL irqL;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
#endif /* #if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME) */
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*cur_network = &(pmlmeinfo->network);
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#endif /* CONFIG_P2P */


	/* RTW_INFO("%s\n", __FUNCTION__); */

#ifdef CONFIG_BCN_ICF
	pmgntframe = rtw_alloc_bcnxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
#else
	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
#endif
	{
		RTW_INFO("%s, alloc mgnt frame fail\n", __FUNCTION__);
		return;
	}
#if defined(CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	_enter_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);
#endif /* #if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME) */

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->qsel = QSLT_BEACON;

#if defined(CONFIG_CONCURRENT_MODE) && (!defined(CONFIG_SWTIMER_BASED_TXBCN))
	if (padapter->hw_port == HW_PORT1)
		pattrib->mbssid = 1;
#endif
#ifdef CONFIG_FW_HANDLE_TXBCN
	if (padapter->vap_id != CONFIG_LIMITED_AP_NUM)
		pattrib->mbssid = padapter->vap_id;
#endif

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;


	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(cur_network), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	/* pmlmeext->mgnt_seq++; */
	set_frame_sub_type(pframe, WIFI_BEACON);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	if (MLME_IS_AP(padapter) || MLME_IS_MESH(padapter)) {
		/* RTW_INFO("ie len=%d\n", cur_network->IELength); */
#ifdef CONFIG_P2P
		/* for P2P : Primary Device Type & Device Name */
		u32 wpsielen = 0, insert_len = 0;
		u8 *wpsie = NULL;
		wpsie = rtw_get_wps_ie(cur_network->IEs + _FIXED_IE_LENGTH_, cur_network->IELength - _FIXED_IE_LENGTH_, NULL, &wpsielen);

		if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO) && wpsie && wpsielen > 0) {
			uint wps_offset, remainder_ielen;
			u8 *premainder_ie, *pframe_wscie;

			wps_offset = (uint)(wpsie - cur_network->IEs);

			premainder_ie = wpsie + wpsielen;

			remainder_ielen = cur_network->IELength - wps_offset - wpsielen;

#ifdef CONFIG_IOCTL_CFG80211
			if (adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->driver_interface == DRIVER_CFG80211) {
				if (pmlmepriv->wps_beacon_ie && pmlmepriv->wps_beacon_ie_len > 0) {
					_rtw_memcpy(pframe, cur_network->IEs, wps_offset);
					pframe += wps_offset;
					pattrib->pktlen += wps_offset;

					_rtw_memcpy(pframe, pmlmepriv->wps_beacon_ie, pmlmepriv->wps_beacon_ie_len);
					pframe += pmlmepriv->wps_beacon_ie_len;
					pattrib->pktlen += pmlmepriv->wps_beacon_ie_len;

					/* copy remainder_ie to pframe */
					_rtw_memcpy(pframe, premainder_ie, remainder_ielen);
					pframe += remainder_ielen;
					pattrib->pktlen += remainder_ielen;
				} else {
					_rtw_memcpy(pframe, cur_network->IEs, cur_network->IELength);
					pframe += cur_network->IELength;
					pattrib->pktlen += cur_network->IELength;
				}
			} else
#endif /* CONFIG_IOCTL_CFG80211 */
			{
				pframe_wscie = pframe + wps_offset;
				_rtw_memcpy(pframe, cur_network->IEs, wps_offset + wpsielen);
				pframe += (wps_offset + wpsielen);
				pattrib->pktlen += (wps_offset + wpsielen);

				/* now pframe is end of wsc ie, insert Primary Device Type & Device Name */
				/*	Primary Device Type */
				/*	Type: */
				*(u16 *)(pframe + insert_len) = cpu_to_be16(WPS_ATTR_PRIMARY_DEV_TYPE);
				insert_len += 2;

				/*	Length: */
				*(u16 *)(pframe + insert_len) = cpu_to_be16(0x0008);
				insert_len += 2;

				/*	Value: */
				/*	Category ID */
				*(u16 *)(pframe + insert_len) = cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA);
				insert_len += 2;

				/*	OUI */
				*(u32 *)(pframe + insert_len) = cpu_to_be32(WPSOUI);
				insert_len += 4;

				/*	Sub Category ID */
				*(u16 *)(pframe + insert_len) = cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
				insert_len += 2;


				/*	Device Name */
				/*	Type: */
				*(u16 *)(pframe + insert_len) = cpu_to_be16(WPS_ATTR_DEVICE_NAME);
				insert_len += 2;

				/*	Length: */
				*(u16 *)(pframe + insert_len) = cpu_to_be16(pwdinfo->device_name_len);
				insert_len += 2;

				/*	Value: */
				_rtw_memcpy(pframe + insert_len, pwdinfo->device_name, pwdinfo->device_name_len);
				insert_len += pwdinfo->device_name_len;


				/* update wsc ie length */
				*(pframe_wscie + 1) = (wpsielen - 2) + insert_len;

				/* pframe move to end */
				pframe += insert_len;
				pattrib->pktlen += insert_len;

				/* copy remainder_ie to pframe */
				_rtw_memcpy(pframe, premainder_ie, remainder_ielen);
				pframe += remainder_ielen;
				pattrib->pktlen += remainder_ielen;
			}
		} else
#endif /* CONFIG_P2P */
		{
			int len_diff;
			_rtw_memcpy(pframe, cur_network->IEs, cur_network->IELength);
			len_diff = update_hidden_ssid(
					   pframe + _BEACON_IE_OFFSET_
				   , cur_network->IELength - _BEACON_IE_OFFSET_
					   , pmlmeinfo->hidden_ssid_mode
				   );
			pframe += (cur_network->IELength + len_diff);
			pattrib->pktlen += (cur_network->IELength + len_diff);
		}

		{
			u8 *wps_ie;
			uint wps_ielen;
			u8 sr = 0;
			wps_ie = rtw_get_wps_ie(pmgntframe->buf_addr + TXDESC_OFFSET + sizeof(struct rtw_ieee80211_hdr_3addr) + _BEACON_IE_OFFSET_,
				pattrib->pktlen - sizeof(struct rtw_ieee80211_hdr_3addr) - _BEACON_IE_OFFSET_, NULL, &wps_ielen);
			if (wps_ie && wps_ielen > 0)
				rtw_get_wps_attr_content(wps_ie,  wps_ielen, WPS_ATTR_SELECTED_REGISTRAR, (u8 *)(&sr), NULL);
			if (sr != 0)
				set_fwstate(pmlmepriv, WIFI_UNDER_WPS);
			else
				_clr_fwstate_(pmlmepriv, WIFI_UNDER_WPS);
		}

#ifdef CONFIG_P2P
		if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
			u32 len;
#ifdef CONFIG_IOCTL_CFG80211
			if (adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->driver_interface == DRIVER_CFG80211) {
				len = pmlmepriv->p2p_beacon_ie_len;
				if (pmlmepriv->p2p_beacon_ie && len > 0)
					_rtw_memcpy(pframe, pmlmepriv->p2p_beacon_ie, len);
			} else
#endif /* CONFIG_IOCTL_CFG80211 */
			{
				len = build_beacon_p2p_ie(pwdinfo, pframe);
			}

			pframe += len;
			pattrib->pktlen += len;

#ifdef CONFIG_MCC_MODE
			pframe = rtw_hal_mcc_append_go_p2p_ie(padapter, pframe, &pattrib->pktlen);
#endif /* CONFIG_MCC_MODE*/

#ifdef CONFIG_WFD
			len = rtw_append_beacon_wfd_ie(padapter, pframe);
			pframe += len;
			pattrib->pktlen += len;
#endif
		}
#endif /* CONFIG_P2P */
#ifdef CONFIG_RTW_REPEATER_SON
		rtw_rson_append_ie(padapter, pframe, &pattrib->pktlen);
#endif
#ifdef CONFIG_APPEND_VENDOR_IE_ENABLE
		pattrib->pktlen += rtw_build_vendor_ie(padapter , &pframe , WIFI_BEACON_VENDOR_IE_BIT);
#endif

#ifdef CONFIG_RTL8812A 
		pframe = rtw_hal_set_8812a_vendor_ie(padapter, pframe, &pattrib->pktlen );
#endif/*CONFIG_RTL8812A*/

#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
		if (padapter->tbtx_capability == _TRUE)
			pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, 8, REALTEK_TBTX_IE, &pattrib->pktlen);
#endif

		goto _issue_bcn;

	}

	/* below for ad-hoc mode */

	/* timestamp will be inserted by hardware */
	pframe += 8;
	pattrib->pktlen += 8;

	/* beacon interval: 2 bytes */

	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pattrib->pktlen += 2;

	/* capability info: 2 bytes */

	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pattrib->pktlen += 2;

	/* SSID */
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pattrib->pktlen);

	/* supported rates... */
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8) ? 8 : rate_len), cur_network->SupportedRates, &pattrib->pktlen);

	/* DS parameter set */
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pattrib->pktlen);

	/* if( (pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) */
	{
		u8 erpinfo = 0;
		u32 ATIMWindow;
		/* IBSS Parameter Set... */
		/* ATIMWindow = cur->Configuration.ATIMWindow; */
		ATIMWindow = 0;
		pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pattrib->pktlen);

		/* ERP IE */
		pframe = rtw_set_ie(pframe, _ERPINFO_IE_, 1, &erpinfo, &pattrib->pktlen);
	}


	/* EXTERNDED SUPPORTED RATE */
	if (rate_len > 8)
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pattrib->pktlen);


	/* todo:HT for adhoc */

_issue_bcn:

#if defined(CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	pmlmepriv->update_bcn = _FALSE;

	_exit_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);
#endif /* #if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME) */

	if ((pattrib->pktlen + TXDESC_SIZE) > MAX_BEACON_LEN) {
		RTW_ERR("beacon frame too large ,len(%d,%d)\n",
			(pattrib->pktlen + TXDESC_SIZE), MAX_BEACON_LEN);
		rtw_warn_on(1);
		return;
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	/* RTW_INFO("issue bcn_sz=%d\n", pattrib->last_txcmdsz); */
	if (timeout_ms > 0)
		dump_mgntframe_and_wait(padapter, pmgntframe, timeout_ms);
	else
		dump_mgntframe(padapter, pmgntframe);

}
#endif /* CONFIG_AP_MODE */

void issue_probersp(_adapter *padapter, unsigned char *da, u8 is_valid_p2p_probereq)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	unsigned char					*mac, *bssid;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
#if defined(CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	u8 *pwps_ie;
	uint wps_ielen;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
#endif /* #if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME) */
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*cur_network = &(pmlmeinfo->network);
	unsigned int	rate_len;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#endif /* CONFIG_P2P */

	/* RTW_INFO("%s\n", __FUNCTION__); */

	if (da == NULL)
		return;

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		return;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL) {
		RTW_INFO("%s, alloc mgnt frame fail\n", __FUNCTION__);
		return;
	}


	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	mac = adapter_mac_addr(padapter);
	bssid = cur_network->MacAddress;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, bssid, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(fctrl, WIFI_PROBERSP);

	pattrib->hdrlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = pattrib->hdrlen;
	pframe += pattrib->hdrlen;


	if (cur_network->IELength > MAX_IE_SZ)
		return;

#if defined(CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	if ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) {
		pwps_ie = rtw_get_wps_ie(cur_network->IEs + _FIXED_IE_LENGTH_, cur_network->IELength - _FIXED_IE_LENGTH_, NULL, &wps_ielen);

		/* inerset & update wps_probe_resp_ie */
		if ((pmlmepriv->wps_probe_resp_ie != NULL) && pwps_ie && (wps_ielen > 0)) {
			uint wps_offset, remainder_ielen;
			u8 *premainder_ie;

			wps_offset = (uint)(pwps_ie - cur_network->IEs);

			premainder_ie = pwps_ie + wps_ielen;

			remainder_ielen = cur_network->IELength - wps_offset - wps_ielen;

			_rtw_memcpy(pframe, cur_network->IEs, wps_offset);
			pframe += wps_offset;
			pattrib->pktlen += wps_offset;

			wps_ielen = (uint)pmlmepriv->wps_probe_resp_ie[1];/* to get ie data len */
			if ((wps_offset + wps_ielen + 2) <= MAX_IE_SZ) {
				_rtw_memcpy(pframe, pmlmepriv->wps_probe_resp_ie, wps_ielen + 2);
				pframe += wps_ielen + 2;
				pattrib->pktlen += wps_ielen + 2;
			}

			if ((wps_offset + wps_ielen + 2 + remainder_ielen) <= MAX_IE_SZ) {
				_rtw_memcpy(pframe, premainder_ie, remainder_ielen);
				pframe += remainder_ielen;
				pattrib->pktlen += remainder_ielen;
			}
		} else {
			_rtw_memcpy(pframe, cur_network->IEs, cur_network->IELength);
			pframe += cur_network->IELength;
			pattrib->pktlen += cur_network->IELength;
		}

		/* retrieve SSID IE from cur_network->Ssid */
		{
			u8 *ssid_ie;
			sint ssid_ielen;
			sint ssid_ielen_diff;
			u8 buf[MAX_IE_SZ];
			u8 *ies = pmgntframe->buf_addr + TXDESC_OFFSET + sizeof(struct rtw_ieee80211_hdr_3addr);

			ssid_ie = rtw_get_ie(ies + _FIXED_IE_LENGTH_, _SSID_IE_, &ssid_ielen,
				     (pframe - ies) - _FIXED_IE_LENGTH_);

			ssid_ielen_diff = cur_network->Ssid.SsidLength - ssid_ielen;

			if (ssid_ie &&  cur_network->Ssid.SsidLength) {
				uint remainder_ielen;
				u8 *remainder_ie;
				remainder_ie = ssid_ie + 2;
				remainder_ielen = (pframe - remainder_ie);

				if (remainder_ielen > MAX_IE_SZ) {
					RTW_WARN(FUNC_ADPT_FMT" remainder_ielen > MAX_IE_SZ\n", FUNC_ADPT_ARG(padapter));
					remainder_ielen = MAX_IE_SZ;
				}

				_rtw_memcpy(buf, remainder_ie, remainder_ielen);
				_rtw_memcpy(remainder_ie + ssid_ielen_diff, buf, remainder_ielen);
				*(ssid_ie + 1) = cur_network->Ssid.SsidLength;
				_rtw_memcpy(ssid_ie + 2, cur_network->Ssid.Ssid, cur_network->Ssid.SsidLength);

				pframe += ssid_ielen_diff;
				pattrib->pktlen += ssid_ielen_diff;
			}
		}
#ifdef CONFIG_RTW_REPEATER_SON
		rtw_rson_append_ie(padapter, pframe, &pattrib->pktlen);
#endif
#ifdef CONFIG_APPEND_VENDOR_IE_ENABLE
		pattrib->pktlen += rtw_build_vendor_ie(padapter , &pframe , WIFI_PROBERESP_VENDOR_IE_BIT);
#endif
	} else
#endif
	{

		/* timestamp will be inserted by hardware */
		pframe += 8;
		pattrib->pktlen += 8;

		/* beacon interval: 2 bytes */

		_rtw_memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2);

		pframe += 2;
		pattrib->pktlen += 2;

		/* capability info: 2 bytes */

		_rtw_memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

		pframe += 2;
		pattrib->pktlen += 2;

		/* below for ad-hoc mode */

		/* SSID */
		pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pattrib->pktlen);

		/* supported rates... */
		rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8) ? 8 : rate_len), cur_network->SupportedRates, &pattrib->pktlen);

		/* DS parameter set */
		pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pattrib->pktlen);

		if ((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) {
			u8 erpinfo = 0;
			u32 ATIMWindow;
			/* IBSS Parameter Set... */
			/* ATIMWindow = cur->Configuration.ATIMWindow; */
			ATIMWindow = 0;
			pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pattrib->pktlen);

			/* ERP IE */
			pframe = rtw_set_ie(pframe, _ERPINFO_IE_, 1, &erpinfo, &pattrib->pktlen);
		}


		/* EXTERNDED SUPPORTED RATE */
		if (rate_len > 8)
			pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pattrib->pktlen);


		/* todo:HT for adhoc */

	}

#ifdef CONFIG_P2P
	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)
	    /* IOT issue, When wifi_spec is not set, send probe_resp with P2P IE even if probe_req has no P2P IE */
	    && (is_valid_p2p_probereq || !padapter->registrypriv.wifi_spec)) {
		u32 len;
#ifdef CONFIG_IOCTL_CFG80211
		if (adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->driver_interface == DRIVER_CFG80211) {
			/* if pwdinfo->role == P2P_ROLE_DEVICE will call issue_probersp_p2p() */
			len = pmlmepriv->p2p_go_probe_resp_ie_len;
			if (pmlmepriv->p2p_go_probe_resp_ie && len > 0)
				_rtw_memcpy(pframe, pmlmepriv->p2p_go_probe_resp_ie, len);
		} else
#endif /* CONFIG_IOCTL_CFG80211 */
		{
			len = build_probe_resp_p2p_ie(pwdinfo, pframe);
		}

		pframe += len;
		pattrib->pktlen += len;

#ifdef CONFIG_MCC_MODE
		pframe = rtw_hal_mcc_append_go_p2p_ie(padapter, pframe, &pattrib->pktlen);
#endif /* CONFIG_MCC_MODE*/

#ifdef CONFIG_WFD
		len = rtw_append_probe_resp_wfd_ie(padapter, pframe);
		pframe += len;
		pattrib->pktlen += len;
#endif
	}
#endif /* CONFIG_P2P */


#ifdef CONFIG_AUTO_AP_MODE
	{
		struct sta_info	*psta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		RTW_INFO("(%s)\n", __FUNCTION__);

		/* check rc station */
		psta = rtw_get_stainfo(pstapriv, da);
		if (psta && psta->isrc && psta->pid > 0) {
			u8 RC_OUI[4] = {0x00, 0xE0, 0x4C, 0x0A};
			u8 RC_INFO[14] = {0};
			/* EID[1] + EID_LEN[1] + RC_OUI[4] + MAC[6] + PairingID[2] + ChannelNum[2] */
			u16 cu_ch = (u16)cur_network->Configuration.DSConfig;

			RTW_INFO("%s, reply rc(pid=0x%x) device "MAC_FMT" in ch=%d\n", __FUNCTION__,
				 psta->pid, MAC_ARG(psta->cmn.mac_addr), cu_ch);

			/* append vendor specific ie */
			_rtw_memcpy(RC_INFO, RC_OUI, sizeof(RC_OUI));
			_rtw_memcpy(&RC_INFO[4], mac, ETH_ALEN);
			_rtw_memcpy(&RC_INFO[10], (u8 *)&psta->pid, 2);
			_rtw_memcpy(&RC_INFO[12], (u8 *)&cu_ch, 2);

			pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, sizeof(RC_INFO), RC_INFO, &pattrib->pktlen);
		}
	}
#endif /* CONFIG_AUTO_AP_MODE */

#ifdef CONFIG_RTL8812A 
	pframe = rtw_hal_set_8812a_vendor_ie(padapter, pframe, &pattrib->pktlen);
#endif/*CONFIG_RTL8812A*/

	pattrib->last_txcmdsz = pattrib->pktlen;


	dump_mgntframe(padapter, pmgntframe);

	return;

}

int _issue_probereq(_adapter *padapter, const NDIS_802_11_SSID *pssid, const u8 *da, u8 ch, bool append_wps, int wait_ack)
{
	int ret = _FAIL;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	unsigned char			*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short		*fctrl;
	unsigned char			*mac;
	unsigned char			bssrate[NumRates];
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	int	bssrate_len = 0;
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
#ifdef CONFIG_RTW_CFGVENDOR_RANDOM_MAC_OUI
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
#endif

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		goto exit;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);


	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

#ifdef CONFIG_RTW_CFGVENDOR_RANDOM_MAC_OUI
	if ((pwdev_priv->pno_mac_addr[0] != 0xFF)
	    && (check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE) == _TRUE)
	    && (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) == _FALSE))
		mac = pwdev_priv->pno_mac_addr;
	else
#endif
	mac = adapter_mac_addr(padapter);

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	if (da) {
		/*	unicast probe request frame */
		_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, da, ETH_ALEN);
	} else {
		/*	broadcast probe request frame */
		_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, bc_addr, ETH_ALEN);
	}

	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);

#ifdef CONFIG_RTW_CFGVENDOR_RANDOM_MAC_OUI
	if ((pwdev_priv->pno_mac_addr[0] != 0xFF)
	    && (check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE) == _TRUE)
	    && (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) == _FALSE)) {
#ifdef CONFIG_RTW_DEBUG
		RTW_DBG("%s pno_scan_seq_num: %d\n", __func__,
			 pwdev_priv->pno_scan_seq_num);
#endif
		SetSeqNum(pwlanhdr, pwdev_priv->pno_scan_seq_num);
		pattrib->seqnum = pwdev_priv->pno_scan_seq_num;
		pattrib->qos_en = 1;
		pwdev_priv->pno_scan_seq_num++;
	} else
#endif
	{
		SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
		pmlmeext->mgnt_seq++;
	}
	set_frame_sub_type(pframe, WIFI_PROBEREQ);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	if (pssid && !MLME_IS_MESH(padapter))
		pframe = rtw_set_ie(pframe, _SSID_IE_, pssid->SsidLength, pssid->Ssid, &(pattrib->pktlen));
	else
		pframe = rtw_set_ie(pframe, _SSID_IE_, 0, NULL, &(pattrib->pktlen));

	get_rate_set(padapter, bssrate, &bssrate_len);

	if (bssrate_len > 8) {
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , 8, bssrate, &(pattrib->pktlen));
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_ , (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	} else
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , bssrate_len , bssrate, &(pattrib->pktlen));

	if (ch)
		pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, &ch, &pattrib->pktlen);

#ifdef CONFIG_RTW_MESH
	if (MLME_IS_MESH(padapter)) {
		if (pssid)
			pframe = rtw_set_ie_mesh_id(pframe, &pattrib->pktlen, pssid->Ssid, pssid->SsidLength);
		else
			pframe = rtw_set_ie_mesh_id(pframe, &pattrib->pktlen, NULL, 0);
	}
#endif

	if (append_wps) {
		/* add wps_ie for wps2.0 */
		if (pmlmepriv->wps_probe_req_ie_len > 0 && pmlmepriv->wps_probe_req_ie) {
			_rtw_memcpy(pframe, pmlmepriv->wps_probe_req_ie, pmlmepriv->wps_probe_req_ie_len);
			pframe += pmlmepriv->wps_probe_req_ie_len;
			pattrib->pktlen += pmlmepriv->wps_probe_req_ie_len;
			/* pmlmepriv->wps_probe_req_ie_len = 0 ; */ /* reset to zero */
		}
	}
#ifdef CONFIG_APPEND_VENDOR_IE_ENABLE
	pattrib->pktlen += rtw_build_vendor_ie(padapter , &pframe , WIFI_PROBEREQ_VENDOR_IE_BIT);
#endif

#ifdef CONFIG_RTL8812A 
	pframe = rtw_hal_set_8812a_vendor_ie(padapter, pframe, &pattrib->pktlen );
#endif/*CONFIG_RTL8812A*/


#ifdef CONFIG_RTW_MBO
	rtw_mbo_build_probe_req_ies(	padapter, &pframe, pattrib);
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;


	if (wait_ack)
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

inline void issue_probereq(_adapter *padapter, const NDIS_802_11_SSID *pssid, const u8 *da)
{
	_issue_probereq(padapter, pssid, da, 0, 1, _FALSE);
}

/*
 * wait_ms == 0 means that there is no need to wait ack through C2H_CCX_TX_RPT
 * wait_ms > 0 means you want to wait ack through C2H_CCX_TX_RPT, and the value of wait_ms means the interval between each TX
 * try_cnt means the maximal TX count to try
 */
int issue_probereq_ex(_adapter *padapter, const NDIS_802_11_SSID *pssid, const u8 *da, u8 ch, bool append_wps,
		      int try_cnt, int wait_ms)
{
	int ret = _FAIL;
	int i = 0;
	systime start = rtw_get_current_time();

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		goto exit;

	do {
		ret = _issue_probereq(padapter, pssid, da, ch, append_wps, wait_ms > 0 ? _TRUE : _FALSE);

		i++;

		if (RTW_CANNOT_RUN(padapter))
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			rtw_msleep_os(wait_ms);

	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
#ifndef DBG_XMIT_ACK
		goto exit;
#endif
	}

	if (try_cnt && wait_ms) {
		if (da)
			RTW_INFO(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), MAC_ARG(da), rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
		else
			RTW_INFO(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
	}
exit:
	return ret;
}

/* if psta == NULL, indiate we are station(client) now... */
void issue_auth(_adapter *padapter, struct sta_info *psta, unsigned short status)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	unsigned int					val32;
	unsigned short				val16;
	int use_shared_key = 0;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		return;

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

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_AUTH);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);


	if (psta) { /* for AP mode */
#ifdef CONFIG_NATIVEAP_MLME

		_rtw_memcpy(pwlanhdr->addr1, psta->cmn.mac_addr, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, adapter_mac_addr(padapter), ETH_ALEN);


		/* setting auth algo number */
		val16 = (u16)psta->authalg;

		if (status != _STATS_SUCCESSFUL_)
			val16 = 0;

		if (val16)	{
			val16 = cpu_to_le16(val16);
			use_shared_key = 1;
		}

		pframe = rtw_set_fixed_ie(pframe, _AUTH_ALGM_NUM_, (unsigned char *)&val16, &(pattrib->pktlen));

		/* setting auth seq number */
		val16 = (u16)psta->auth_seq;
		val16 = cpu_to_le16(val16);
		pframe = rtw_set_fixed_ie(pframe, _AUTH_SEQ_NUM_, (unsigned char *)&val16, &(pattrib->pktlen));

		/* setting status code... */
		val16 = status;
		val16 = cpu_to_le16(val16);
		pframe = rtw_set_fixed_ie(pframe, _STATUS_CODE_, (unsigned char *)&val16, &(pattrib->pktlen));

		/* added challenging text... */
		if ((psta->auth_seq == 2) && (psta->state & WIFI_FW_AUTH_STATE) && (use_shared_key == 1))
			pframe = rtw_set_ie(pframe, _CHLGETXT_IE_, 128, psta->chg_txt, &(pattrib->pktlen));
#endif
	} else {
		_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);

#ifdef CONFIG_RTW_80211R
		if (rtw_ft_roam(padapter)) {
			/* 2: 802.11R FTAA */
			val16 = cpu_to_le16(2);
		} else
#endif
		{
			/* setting auth algo number */
			val16 = (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared) ? 1 : 0;	/* 0:OPEN System, 1:Shared key */
			if (val16) {
				val16 = cpu_to_le16(val16);
				use_shared_key = 1;
			}
		}

		/* RTW_INFO("%s auth_algo= %s auth_seq=%d\n",__FUNCTION__,(pmlmeinfo->auth_algo==0)?"OPEN":"SHARED",pmlmeinfo->auth_seq); */

		/* setting IV for auth seq #3 */
		if ((pmlmeinfo->auth_seq == 3) && (pmlmeinfo->state & WIFI_FW_AUTH_STATE) && (use_shared_key == 1)) {
			/* RTW_INFO("==> iv(%d),key_index(%d)\n",pmlmeinfo->iv,pmlmeinfo->key_index); */
			val32 = ((pmlmeinfo->iv++) | (pmlmeinfo->key_index << 30));
			val32 = cpu_to_le32(val32);
			pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *)&val32, &(pattrib->pktlen));

			pattrib->iv_len = 4;
		}

		pframe = rtw_set_fixed_ie(pframe, _AUTH_ALGM_NUM_, (unsigned char *)&val16, &(pattrib->pktlen));

		/* setting auth seq number */
		val16 = pmlmeinfo->auth_seq;
		val16 = cpu_to_le16(val16);
		pframe = rtw_set_fixed_ie(pframe, _AUTH_SEQ_NUM_, (unsigned char *)&val16, &(pattrib->pktlen));


		/* setting status code... */
		val16 = status;
		val16 = cpu_to_le16(val16);
		pframe = rtw_set_fixed_ie(pframe, _STATUS_CODE_, (unsigned char *)&val16, &(pattrib->pktlen));

#ifdef CONFIG_RTW_80211R
		rtw_ft_build_auth_req_ies(padapter, pattrib, &pframe);
#endif

		/* then checking to see if sending challenging text... */
		if ((pmlmeinfo->auth_seq == 3) && (pmlmeinfo->state & WIFI_FW_AUTH_STATE) && (use_shared_key == 1)) {
			pframe = rtw_set_ie(pframe, _CHLGETXT_IE_, 128, pmlmeinfo->chg_txt, &(pattrib->pktlen));

			SetPrivacy(fctrl);

			pattrib->hdrlen = sizeof(struct rtw_ieee80211_hdr_3addr);

			pattrib->encrypt = _WEP40_;

			pattrib->icv_len = 4;

			pattrib->pktlen += pattrib->icv_len;

		}

	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	rtw_wep_encrypt(padapter, (u8 *)pmgntframe);
	RTW_INFO("%s\n", __FUNCTION__);
	dump_mgntframe(padapter, pmgntframe);

	return;
}


void issue_asocrsp(_adapter *padapter, unsigned short status, struct sta_info *pstat, int pkt_type)
{
#ifdef CONFIG_AP_MODE
	struct xmit_frame	*pmgntframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	struct pkt_attrib *pattrib;
	unsigned char	*pbuf, *pframe;
	unsigned short val, ie_status;
	unsigned short *fctrl;
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork = &(pmlmeinfo->network);
	u8 *ie = pnetwork->IEs, cap[5], i;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif

#endif /* CONFIG_P2P */

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		return;

	RTW_INFO("%s\n", __FUNCTION__);

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

	_rtw_memcpy((void *)GetAddr1Ptr(pwlanhdr), pstat->cmn.mac_addr, ETH_ALEN);
	_rtw_memcpy((void *)get_addr2_ptr(pwlanhdr), adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy((void *)GetAddr3Ptr(pwlanhdr), get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);


	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	if ((pkt_type == WIFI_ASSOCRSP) || (pkt_type == WIFI_REASSOCRSP))
		set_frame_sub_type(pwlanhdr, pkt_type);
	else
		return;

	pattrib->hdrlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen += pattrib->hdrlen;
	pframe += pattrib->hdrlen;

	/* capability */
	val = *(unsigned short *)rtw_get_capability_from_ie(ie);
#ifdef CONFIG_RTW_80211K
	val |= cap_RM;
#endif
	pframe = rtw_set_fixed_ie(pframe, _CAPABILITY_ , (unsigned char *)&val, &(pattrib->pktlen));

	ie_status = cpu_to_le16(status);
	pframe = rtw_set_fixed_ie(pframe , _STATUS_CODE_ , (unsigned char *)&ie_status, &(pattrib->pktlen));

	val = cpu_to_le16(pstat->cmn.aid | BIT(14) | BIT(15));
	pframe = rtw_set_fixed_ie(pframe, _ASOC_ID_ , (unsigned char *)&val, &(pattrib->pktlen));

	if (pstat->bssratelen <= 8)
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, pstat->bssratelen, pstat->bssrateset, &(pattrib->pktlen));
	else {
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, pstat->bssrateset, &(pattrib->pktlen));
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (pstat->bssratelen - 8), pstat->bssrateset + 8, &(pattrib->pktlen));
	}

#ifdef CONFIG_IEEE80211W
	if (status == _STATS_REFUSED_TEMPORARILY_) {
		u8 timeout_itvl[5];
		u32 timeout_interval = 3000;
		/* Association Comeback time */
		timeout_itvl[0] = 0x03;
		timeout_interval = cpu_to_le32(timeout_interval);
		_rtw_memcpy(timeout_itvl + 1, &timeout_interval, 4);
		pframe = rtw_set_ie(pframe, _TIMEOUT_ITVL_IE_, 5, timeout_itvl, &(pattrib->pktlen));
	}
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_80211N_HT
	if ((pstat->flags & WLAN_STA_HT) && (pmlmepriv->htpriv.ht_option)) {
		uint ie_len = 0;

		/* FILL HT CAP INFO IE */
		/* p = hostapd_eid_ht_capabilities_info(hapd, p); */
		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_CAPABILITY_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if (pbuf && ie_len > 0) {
			_rtw_memcpy(pframe, pbuf, ie_len + 2);
			pframe += (ie_len + 2);
			pattrib->pktlen += (ie_len + 2);
		}

		/* FILL HT ADD INFO IE */
		/* p = hostapd_eid_ht_operation(hapd, p); */
		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_ADD_INFO_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if (pbuf && ie_len > 0) {
			_rtw_memcpy(pframe, pbuf, ie_len + 2);
			pframe += (ie_len + 2);
			pattrib->pktlen += (ie_len + 2);
		}

	}
#endif

	/*adding EXT_CAPAB_IE */
	if (pmlmepriv->ext_capab_ie_len > 0) {
		uint ie_len = 0;

		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _EXT_CAP_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if (pbuf && ie_len > 0) {
			_rtw_memcpy(pframe, pbuf, ie_len + 2);
			pframe += (ie_len + 2);
			pattrib->pktlen += (ie_len + 2);
		}
	}

#ifdef CONFIG_80211AC_VHT
	if ((pstat->flags & WLAN_STA_VHT) && (pmlmepriv->vhtpriv.vht_option)
	    && (pstat->wpa_pairwise_cipher != WPA_CIPHER_TKIP)
	    && (pstat->wpa2_pairwise_cipher != WPA_CIPHER_TKIP)) {
		u32 ie_len = 0;

		/* FILL VHT CAP IE */
		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, EID_VHTCapability, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if (pbuf && ie_len > 0) {
			_rtw_memcpy(pframe, pbuf, ie_len + 2);
			pframe += (ie_len + 2);
			pattrib->pktlen += (ie_len + 2);
		}

		/* FILL VHT OPERATION IE */
		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, EID_VHTOperation, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if (pbuf && ie_len > 0) {
			_rtw_memcpy(pframe, pbuf, ie_len + 2);
			pframe += (ie_len + 2);
			pattrib->pktlen += (ie_len + 2);
		}
	}
#endif /* CONFIG_80211AC_VHT */

#ifdef CONFIG_RTW_80211K
	/* FILL RM Enabled Capabilities with joint capabilities */
	for (i = 0; i < 5; i++) {
		cap[i] = padapter->rmpriv.rm_en_cap_def[i]
			 & pstat->rm_en_cap[i];
	}
	if (pstat->capability & cap_RM)
		pframe = rtw_set_ie(pframe, _EID_RRM_EN_CAP_IE_, 5,
				    (u8 *)cap, &(pattrib->pktlen));
#endif /* CONFIG_RTW_80211K */

	/* FILL WMM IE */
	if ((pstat->flags & WLAN_STA_WME) && (pmlmepriv->qospriv.qos_option)) {
		uint ie_len = 0;
		unsigned char WMM_PARA_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};

		for (pbuf = ie + _BEACON_IE_OFFSET_; ; pbuf += (ie_len + 2)) {
			pbuf = rtw_get_ie(pbuf, _VENDOR_SPECIFIC_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_ - (ie_len + 2)));
			if (pbuf && _rtw_memcmp(pbuf + 2, WMM_PARA_IE, 6)) {
				_rtw_memcpy(pframe, pbuf, ie_len + 2);
				pframe += (ie_len + 2);
				pattrib->pktlen += (ie_len + 2);

				break;
			}

			if ((pbuf == NULL) || (ie_len == 0))
				break;
		}

	}


	if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK)
		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, 6 , REALTEK_96B_IE, &(pattrib->pktlen));

	/* add WPS IE ie for wps 2.0 */
	if (pmlmepriv->wps_assoc_resp_ie && pmlmepriv->wps_assoc_resp_ie_len > 0) {
		_rtw_memcpy(pframe, pmlmepriv->wps_assoc_resp_ie, pmlmepriv->wps_assoc_resp_ie_len);

		pframe += pmlmepriv->wps_assoc_resp_ie_len;
		pattrib->pktlen += pmlmepriv->wps_assoc_resp_ie_len;
	}

#ifdef CONFIG_P2P
	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO) && (pstat->is_p2p_device == _TRUE)) {
		u32 len;

		if (padapter->wdinfo.driver_interface == DRIVER_CFG80211) {
			len = 0;
			if (pmlmepriv->p2p_assoc_resp_ie && pmlmepriv->p2p_assoc_resp_ie_len > 0) {
				len = pmlmepriv->p2p_assoc_resp_ie_len;
				_rtw_memcpy(pframe, pmlmepriv->p2p_assoc_resp_ie, len);
			}
		} else
			len = build_assoc_resp_p2p_ie(pwdinfo, pframe, pstat->p2p_status_code);
		pframe += len;
		pattrib->pktlen += len;
	}

#ifdef CONFIG_WFD
	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
		wfdielen = rtw_append_assoc_resp_wfd_ie(padapter, pframe);
		pframe += wfdielen;
		pattrib->pktlen += wfdielen;
	}
#endif

#endif /* CONFIG_P2P */

#ifdef CONFIG_RTW_MULTI_AP
	if (padapter->multi_ap && (pstat->flags & WLAN_STA_MULTI_AP))
		pframe = rtw_set_multi_ap_ie_ext(pframe, &pattrib->pktlen, padapter->multi_ap);
#endif

#ifdef CONFIG_APPEND_VENDOR_IE_ENABLE
	pattrib->pktlen += rtw_build_vendor_ie(padapter , &pframe , WIFI_ASSOCRESP_VENDOR_IE_BIT);
#endif

#ifdef CONFIG_RTL8812A 
	pframe = rtw_hal_set_8812a_vendor_ie(padapter, pframe, &pattrib->pktlen );
#endif/*CONFIG_RTL8812A*/


#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
	if (padapter->tbtx_capability == _TRUE)
		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, 8, REALTEK_TBTX_IE, &pattrib->pktlen);
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

#endif
}

static u32 rtw_append_assoc_req_owe_ie(_adapter *adapter, u8 *pbuf)
{
	struct security_priv *sec = &adapter->securitypriv;
	u32 len = 0;

	if (sec == NULL)
		goto exit;

	if (sec->owe_ie_len > 0) {
		len = sec->owe_ie_len;
		_rtw_memcpy(pbuf, sec->owe_ie, len);
	}

exit:
	return len;
}

void _issue_assocreq(_adapter *padapter, u8 is_reassoc)
{
	int ret = _FAIL;
	struct xmit_frame				*pmgntframe;
	struct pkt_attrib				*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr			*pwlanhdr;
	unsigned short				*fctrl;
	unsigned short				val16;
	unsigned int					i, j, index = 0;
	unsigned char					bssrate[NumRates], sta_bssrate[NumRates];
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	int	bssrate_len = 0, sta_bssrate_len = 0;
	u8	vs_ie_length = 0;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	u8					p2pie[255] = { 0x00 };
	u16					p2pielen = 0;
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif
#endif /* CONFIG_P2P */

#if CONFIG_DFS
	u16	cap;
#endif

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		goto exit;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	if (is_reassoc == _TRUE)
		set_frame_sub_type(pframe, WIFI_REASSOCREQ);
	else
		set_frame_sub_type(pframe, WIFI_ASSOCREQ);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	/* caps */

#if CONFIG_DFS
	_rtw_memcpy(&cap, rtw_get_capability_from_ie(pmlmeinfo->network.IEs), 2);
	cap |= cap_SpecMgmt;
#ifdef CONFIG_RTW_80211K
	cap |= cap_RM;
#endif
	_rtw_memcpy(pframe, &cap, 2);
#else
	_rtw_memcpy(pframe, rtw_get_capability_from_ie(pmlmeinfo->network.IEs), 2);
#endif

	pframe += 2;
	pattrib->pktlen += 2;

	/* listen interval */
	/* todo: listen interval for power saving */
	val16 = cpu_to_le16(3);
	_rtw_memcpy(pframe , (unsigned char *)&val16, 2);
	pframe += 2;
	pattrib->pktlen += 2;

	/*Construct Current AP Field for Reassoc-Req only*/
	if (is_reassoc == _TRUE) {
		_rtw_memcpy(pframe, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		pframe += ETH_ALEN;
		pattrib->pktlen += ETH_ALEN;
	}

	/* SSID */
	pframe = rtw_set_ie(pframe, _SSID_IE_,  pmlmeinfo->network.Ssid.SsidLength, pmlmeinfo->network.Ssid.Ssid, &(pattrib->pktlen));

#if CONFIG_IEEE80211_BAND_5GHZ && CONFIG_DFS
	/* Dot H */
	if (pmlmeext->cur_channel > 14) {
		struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
		u8 pow_cap_ele[2] = { 0x00 };
		u8 sup_ch[30 * 2] = {0x00 }, sup_ch_idx = 0, idx_5g = 2;	/* For supported channel */

		pow_cap_ele[0] = 13;	/* Minimum transmit power capability */
		pow_cap_ele[1] = 21;	/* Maximum transmit power capability */
		pframe = rtw_set_ie(pframe, EID_PowerCap, 2, pow_cap_ele, &(pattrib->pktlen));

		/* supported channels */
		while (sup_ch_idx < rfctl->max_chan_nums && rfctl->channel_set[sup_ch_idx].ChannelNum != 0) {
			if (rfctl->channel_set[sup_ch_idx].ChannelNum <= 14) {
				/* TODO: fix 2.4G supported channel when channel doesn't start from 1 and continuous */
				sup_ch[0] = 1;	/* First channel number */
				sup_ch[1] = rfctl->channel_set[sup_ch_idx].ChannelNum;	/* Number of channel */
			} else {
				sup_ch[idx_5g++] = rfctl->channel_set[sup_ch_idx].ChannelNum;
				sup_ch[idx_5g++] = 1;
			}
			sup_ch_idx++;
		}
		pframe = rtw_set_ie(pframe, EID_SupportedChannels, idx_5g, sup_ch, &(pattrib->pktlen));
	}
#endif /* CONFIG_IEEE80211_BAND_5GHZ && CONFIG_DFS */

	/* supported rate & extended supported rate */

#if 1	/* Check if the AP's supported rates are also supported by STA. */
	get_rate_set(padapter, sta_bssrate, &sta_bssrate_len);
	/* RTW_INFO("sta_bssrate_len=%d\n", sta_bssrate_len); */

	if (pmlmeext->cur_channel == 14) /* for JAPAN, channel 14 can only uses B Mode(CCK) */
		sta_bssrate_len = 4;


	/* for (i = 0; i < sta_bssrate_len; i++) { */
	/*	RTW_INFO("sta_bssrate[%d]=%02X\n", i, sta_bssrate[i]); */
	/* } */

	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		if (pmlmeinfo->network.SupportedRates[i] == 0)
			break;
		RTW_INFO("network.SupportedRates[%d]=%02X\n", i, pmlmeinfo->network.SupportedRates[i]);
	}


	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		if (pmlmeinfo->network.SupportedRates[i] == 0)
			break;


		/* Check if the AP's supported rates are also supported by STA. */
		for (j = 0; j < sta_bssrate_len; j++) {
			/* Avoid the proprietary data rate (22Mbps) of Handlink WSG-4000 AP */
			if ((pmlmeinfo->network.SupportedRates[i] | IEEE80211_BASIC_RATE_MASK)
			    == (sta_bssrate[j] | IEEE80211_BASIC_RATE_MASK)) {
				/* RTW_INFO("match i = %d, j=%d\n", i, j); */
				break;
			} else {
				/* RTW_INFO("not match: %02X != %02X\n", (pmlmeinfo->network.SupportedRates[i]|IEEE80211_BASIC_RATE_MASK), (sta_bssrate[j]|IEEE80211_BASIC_RATE_MASK)); */
			}
		}

		if (j == sta_bssrate_len) {
			/* the rate is not supported by STA */
			RTW_INFO("%s(): the rate[%d]=%02X is not supported by STA!\n", __FUNCTION__, i, pmlmeinfo->network.SupportedRates[i]);
		} else {
			/* the rate is supported by STA */
			bssrate[index++] = pmlmeinfo->network.SupportedRates[i];
		}
	}

	bssrate_len = index;
	RTW_INFO("bssrate_len = %d\n", bssrate_len);

#else	/* Check if the AP's supported rates are also supported by STA. */
#if 0
	get_rate_set(padapter, bssrate, &bssrate_len);
#else
	for (bssrate_len = 0; bssrate_len < NumRates; bssrate_len++) {
		if (pmlmeinfo->network.SupportedRates[bssrate_len] == 0)
			break;

		if (pmlmeinfo->network.SupportedRates[bssrate_len] == 0x2C) /* Avoid the proprietary data rate (22Mbps) of Handlink WSG-4000 AP */
			break;

		bssrate[bssrate_len] = pmlmeinfo->network.SupportedRates[bssrate_len];
	}
#endif
#endif /* Check if the AP's supported rates are also supported by STA. */

	if ((bssrate_len == 0) && (pmlmeinfo->network.SupportedRates[0] != 0)) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit; /* don't connect to AP if no joint supported rate */
	}


	if (bssrate_len > 8) {
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , 8, bssrate, &(pattrib->pktlen));
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_ , (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	} else if (bssrate_len > 0)
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , bssrate_len , bssrate, &(pattrib->pktlen));
	else
		RTW_INFO("%s: Connect to AP without 11b and 11g data rate!\n", __FUNCTION__);

#ifdef CONFIG_RTW_MBO
	rtw_mbo_build_assoc_req_ies(padapter, &pframe, pattrib);
#endif
#ifdef CONFIG_RTW_80211R
	rtw_ft_build_assoc_req_ies(padapter, is_reassoc, pattrib, &pframe);
#endif
#ifdef CONFIG_RTW_80211K
	pframe = rtw_set_ie(pframe, _EID_RRM_EN_CAP_IE_, 5, 
			(u8 *)padapter->rmpriv.rm_en_cap_def, 
			&(pattrib->pktlen));
#endif /* CONFIG_RTW_80211K */

	/* vendor specific IE, such as WPA, WMM, WPS */
	for (i = sizeof(NDIS_802_11_FIXED_IEs); i < pmlmeinfo->network.IELength;) {
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pmlmeinfo->network.IEs + i);

		switch (pIE->ElementID) {
		case _VENDOR_SPECIFIC_IE_:
			if ((_rtw_memcmp(pIE->data, RTW_WPA_OUI, 4)) ||
			    (_rtw_memcmp(pIE->data, WMM_OUI, 4)) ||
			    (_rtw_memcmp(pIE->data, WPS_OUI, 4))) {
				vs_ie_length = pIE->Length;
				if ((!padapter->registrypriv.wifi_spec) && (_rtw_memcmp(pIE->data, WPS_OUI, 4))) {
					/* Commented by Kurt 20110629 */
					/* In some older APs, WPS handshake */
					/* would be fail if we append vender extensions informations to AP */

					vs_ie_length = 14;
				}

				pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, vs_ie_length, pIE->data, &(pattrib->pktlen));
			}
			break;

		case EID_WPA2:
#ifdef CONFIG_RTW_80211R
			if ((is_reassoc) && (rtw_ft_roam(padapter))) {
				rtw_ft_update_rsnie(padapter, _TRUE, pattrib, &pframe);
			} else
#endif
			{
#ifdef CONFIG_IOCTL_CFG80211
				if (rtw_sec_chk_auth_alg(padapter, WLAN_AUTH_OPEN) &&
					rtw_sec_chk_auth_type(padapter, MLME_AUTHTYPE_SAE)) {
					s32 entry = rtw_cached_pmkid(padapter, pmlmepriv->assoc_bssid);

					rtw_rsn_sync_pmkid(padapter, (u8 *)pIE, (pIE->Length + 2), entry);
				}
#endif /* CONFIG_IOCTL_CFG80211 */

				pframe = rtw_set_ie(pframe, EID_WPA2, pIE->Length, pIE->data, &(pattrib->pktlen));
			}
			break;
#ifdef CONFIG_80211N_HT
		case EID_HTCapability:
			if (padapter->mlmepriv.htpriv.ht_option == _TRUE) {
				if (!(is_ap_in_tkip(padapter))) {
					_rtw_memcpy(&(pmlmeinfo->HT_caps), pIE->data, sizeof(struct HT_caps_element));

					pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info = cpu_to_le16(pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info);

					pframe = rtw_set_ie(pframe, EID_HTCapability, pIE->Length , (u8 *)(&(pmlmeinfo->HT_caps)), &(pattrib->pktlen));
				}
			}
			break;

		case EID_EXTCapability:
			if (padapter->mlmepriv.htpriv.ht_option == _TRUE)
				pframe = rtw_set_ie(pframe, EID_EXTCapability, pIE->Length, pIE->data, &(pattrib->pktlen));
			break;
#endif /* CONFIG_80211N_HT */
#ifdef CONFIG_80211AC_VHT
		case EID_VHTCapability:
			if (padapter->mlmepriv.vhtpriv.vht_option == _TRUE)
				pframe = rtw_set_ie(pframe, EID_VHTCapability, pIE->Length, pIE->data, &(pattrib->pktlen));
			break;

		case EID_OpModeNotification:
			if (padapter->mlmepriv.vhtpriv.vht_option == _TRUE)
				pframe = rtw_set_ie(pframe, EID_OpModeNotification, pIE->Length, pIE->data, &(pattrib->pktlen));
			break;
#endif /* CONFIG_80211AC_VHT */
		default:
			break;
		}

		i += (pIE->Length + 2);
	}
#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
	if (padapter->tbtx_capability == _TRUE)
		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, 8 , REALTEK_TBTX_IE, &(pattrib->pktlen));
#endif

	if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK)
		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, 6 , REALTEK_96B_IE, &(pattrib->pktlen));


#ifdef CONFIG_WAPI_SUPPORT
	rtw_build_assoc_req_wapi_ie(padapter, pframe, pattrib);
#endif


#ifdef CONFIG_P2P

#ifdef CONFIG_IOCTL_CFG80211
	if (adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->driver_interface == DRIVER_CFG80211) {
		if (pmlmepriv->p2p_assoc_req_ie && pmlmepriv->p2p_assoc_req_ie_len > 0) {
			_rtw_memcpy(pframe, pmlmepriv->p2p_assoc_req_ie, pmlmepriv->p2p_assoc_req_ie_len);
			pframe += pmlmepriv->p2p_assoc_req_ie_len;
			pattrib->pktlen += pmlmepriv->p2p_assoc_req_ie_len;
		}
	} else
#endif /* CONFIG_IOCTL_CFG80211 */
	{
		if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) && !rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE)) {
			/*	Should add the P2P IE in the association request frame.	 */
			/*	P2P OUI */

			p2pielen = 0;
			p2pie[p2pielen++] = 0x50;
			p2pie[p2pielen++] = 0x6F;
			p2pie[p2pielen++] = 0x9A;
			p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

			/*	Commented by Albert 20101109 */
			/*	According to the P2P Specification, the association request frame should contain 3 P2P attributes */
			/*	1. P2P Capability */
			/*	2. Extended Listen Timing */
			/*	3. Device Info */
			/*	Commented by Albert 20110516 */
			/*	4. P2P Interface */

			/*	P2P Capability */
			/*	Type: */
			p2pie[p2pielen++] = P2P_ATTR_CAPABILITY;

			/*	Length: */
			*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0002);
			p2pielen += 2;

			/*	Value: */
			/*	Device Capability Bitmap, 1 byte */
			p2pie[p2pielen++] = DMP_P2P_DEVCAP_SUPPORT;

			/*	Group Capability Bitmap, 1 byte */
			if (pwdinfo->persistent_supported)
				p2pie[p2pielen++] = P2P_GRPCAP_PERSISTENT_GROUP | DMP_P2P_GRPCAP_SUPPORT;
			else
				p2pie[p2pielen++] = DMP_P2P_GRPCAP_SUPPORT;

			/*	Extended Listen Timing */
			/*	Type: */
			p2pie[p2pielen++] = P2P_ATTR_EX_LISTEN_TIMING;

			/*	Length: */
			*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0004);
			p2pielen += 2;

			/*	Value: */
			/*	Availability Period */
			*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0xFFFF);
			p2pielen += 2;

			/*	Availability Interval */
			*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0xFFFF);
			p2pielen += 2;

			/*	Device Info */
			/*	Type: */
			p2pie[p2pielen++] = P2P_ATTR_DEVICE_INFO;

			/*	Length: */
			/*	21->P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes)  */
			/*	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes) */
			*(u16 *)(p2pie + p2pielen) = cpu_to_le16(21 + pwdinfo->device_name_len);
			p2pielen += 2;

			/*	Value: */
			/*	P2P Device Address */
			_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
			p2pielen += ETH_ALEN;

			/*	Config Method */
			/*	This field should be big endian. Noted by P2P specification. */
			if ((pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_PEER_DISPLAY_PIN) ||
			    (pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_SELF_DISPLAY_PIN))
				*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_CONFIG_METHOD_DISPLAY);
			else
				*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_CONFIG_METHOD_PBC);

			p2pielen += 2;

			/*	Primary Device Type */
			/*	Category ID */
			*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA);
			p2pielen += 2;

			/*	OUI */
			*(u32 *)(p2pie + p2pielen) = cpu_to_be32(WPSOUI);
			p2pielen += 4;

			/*	Sub Category ID */
			*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
			p2pielen += 2;

			/*	Number of Secondary Device Types */
			p2pie[p2pielen++] = 0x00;	/*	No Secondary Device Type List */

			/*	Device Name */
			/*	Type: */
			*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_ATTR_DEVICE_NAME);
			p2pielen += 2;

			/*	Length: */
			*(u16 *)(p2pie + p2pielen) = cpu_to_be16(pwdinfo->device_name_len);
			p2pielen += 2;

			/*	Value: */
			_rtw_memcpy(p2pie + p2pielen, pwdinfo->device_name, pwdinfo->device_name_len);
			p2pielen += pwdinfo->device_name_len;

			/*	P2P Interface */
			/*	Type: */
			p2pie[p2pielen++] = P2P_ATTR_INTERFACE;

			/*	Length: */
			*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x000D);
			p2pielen += 2;

			/*	Value: */
			_rtw_memcpy(p2pie + p2pielen, pwdinfo->device_addr, ETH_ALEN);	/*	P2P Device Address */
			p2pielen += ETH_ALEN;

			p2pie[p2pielen++] = 1;	/*	P2P Interface Address Count */

			_rtw_memcpy(p2pie + p2pielen, pwdinfo->device_addr, ETH_ALEN);	/*	P2P Interface Address List */
			p2pielen += ETH_ALEN;

			pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen);
		}
	}

#ifdef CONFIG_WFD
	wfdielen = rtw_append_assoc_req_wfd_ie(padapter, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif
#endif /* CONFIG_P2P */

#ifdef CONFIG_RTW_MULTI_AP
	if (padapter->multi_ap)
		pframe = rtw_set_multi_ap_ie_ext(pframe, &pattrib->pktlen, padapter->multi_ap);
#endif

	/* OWE */
	{
	u32 owe_ie_len;
	
	owe_ie_len = rtw_append_assoc_req_owe_ie(padapter, pframe);
	pframe += owe_ie_len;
	pattrib->pktlen += owe_ie_len;
	}

#ifdef CONFIG_RTW_REPEATER_SON
	rtw_rson_append_ie(padapter, pframe, &pattrib->pktlen);
#endif
#ifdef CONFIG_APPEND_VENDOR_IE_ENABLE
	pattrib->pktlen += rtw_build_vendor_ie(padapter , &pframe , WIFI_ASSOCREQ_VENDOR_IE_BIT);
#endif

#ifdef CONFIG_RTL8812A 
	pframe = rtw_hal_set_8812a_vendor_ie(padapter, pframe, &pattrib->pktlen );
#endif/*CONFIG_RTL8812A*/

	pattrib->last_txcmdsz = pattrib->pktlen;
	dump_mgntframe(padapter, pmgntframe);

	ret = _SUCCESS;

exit:
	if (ret == _SUCCESS) {
		rtw_buf_update(&pmlmepriv->assoc_req, &pmlmepriv->assoc_req_len, (u8 *)pwlanhdr, pattrib->pktlen);
	#ifdef CONFIG_RTW_WNM
		if (is_reassoc == _TRUE)
			rtw_wnm_update_reassoc_req_ie(padapter);
	#endif
	} else
		rtw_buf_free(&pmlmepriv->assoc_req, &pmlmepriv->assoc_req_len);

	return;
}

void issue_assocreq(_adapter *padapter)
{
	_issue_assocreq(padapter, _FALSE);
}

void issue_reassocreq(_adapter *padapter)
{
	_issue_assocreq(padapter, _TRUE);
}

/* when wait_ack is ture, this function shoule be called at process context */
static int _issue_nulldata(_adapter *padapter, unsigned char *da, unsigned int power_mode, int wait_ack)
{
	int ret = _FAIL;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv	*pxmitpriv;
	struct mlme_ext_priv	*pmlmeext;
	struct mlme_ext_info	*pmlmeinfo;
	u8 a4_shift;

	/* RTW_INFO("%s:%d\n", __FUNCTION__, power_mode); */

	if (!padapter)
		goto exit;

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		goto exit;

	pxmitpriv = &(padapter->xmitpriv);
	pmlmeext = &(padapter->mlmeextpriv);
	pmlmeinfo = &(pmlmeext->mlmext_info);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->retry_ctrl = _FALSE;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	if (MLME_IS_AP(padapter))
		SetFrDs(fctrl);
	else if (MLME_IS_STA(padapter))
		SetToDs(fctrl);
	else if (MLME_IS_MESH(padapter)) {
		SetToDs(fctrl);
		SetFrDs(fctrl);
	}

	if (power_mode)
		SetPwrMgt(fctrl);

	if (get_tofr_ds(fctrl) == 3) {
		_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, da, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr4, adapter_mac_addr(padapter), ETH_ALEN);
		a4_shift = ETH_ALEN;
		pattrib->hdrlen += ETH_ALEN;
	} else {
		_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		a4_shift = 0;
	}

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_DATA_NULL);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr) + a4_shift;
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr) + a4_shift;

	pattrib->last_txcmdsz = pattrib->pktlen;

	if (wait_ack)
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

/*
 * When wait_ms > 0, this function should be called at process context
 * wait_ms == 0 means that there is no need to wait ack through C2H_CCX_TX_RPT
 * wait_ms > 0 means you want to wait ack through C2H_CCX_TX_RPT, and the value of wait_ms means the interval between each TX
 * try_cnt means the maximal TX count to try
 * da == NULL for station mode
 */
int issue_nulldata(_adapter *padapter, unsigned char *da, unsigned int power_mode, int try_cnt, int wait_ms)
{
	int ret = _FAIL;
	int i = 0;
	systime start = rtw_get_current_time();
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		goto exit;

	/* da == NULL, assum it's null data for sta to ap */
	if (da == NULL)
		da = get_my_bssid(&(pmlmeinfo->network));

	do {
		ret = _issue_nulldata(padapter, da, power_mode, wait_ms > 0 ? _TRUE : _FALSE);

		i++;

		if (RTW_CANNOT_RUN(padapter))
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			rtw_msleep_os(wait_ms);

	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
#ifndef DBG_XMIT_ACK
		goto exit;
#endif
	}

	if (try_cnt && wait_ms) {
		if (da)
			RTW_INFO(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), MAC_ARG(da), rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
		else
			RTW_INFO(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
	}
exit:
	return ret;
}

/* when wait_ack is ture, this function shoule be called at process context */
static int _issue_qos_nulldata(_adapter *padapter, unsigned char *da, u16 tid, u8 ps, int wait_ack)
{
	int ret = _FAIL;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl, *qc;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 a4_shift;

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		goto exit;

	/* RTW_INFO("%s\n", __FUNCTION__); */

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	pattrib->hdrlen += 2;
	pattrib->qos_en = _TRUE;
	pattrib->eosp = 1;
	pattrib->ack_policy = 0;
	pattrib->mdata = 0;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	if (MLME_IS_AP(padapter))
		SetFrDs(fctrl);
	else if (MLME_IS_STA(padapter))
		SetToDs(fctrl);
	else if (MLME_IS_MESH(padapter)) {
		SetToDs(fctrl);
		SetFrDs(fctrl);
	}

	if (ps)
		SetPwrMgt(fctrl);

	if (pattrib->mdata)
		SetMData(fctrl);

	if (get_tofr_ds(fctrl) == 3) {
		_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, da, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr4, adapter_mac_addr(padapter), ETH_ALEN);
		a4_shift = ETH_ALEN;
		pattrib->hdrlen += ETH_ALEN;
	} else {
		_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		a4_shift = 0;
	}

	qc = (unsigned short *)(pframe + pattrib->hdrlen - 2);

	SetPriority(qc, tid);

	SetEOSP(qc, pattrib->eosp);

	SetAckpolicy(qc, pattrib->ack_policy);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_QOS_DATA_NULL);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr_qos) + a4_shift;
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr_qos) + a4_shift;

	pattrib->last_txcmdsz = pattrib->pktlen;

	if (wait_ack)
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

/*
 * when wait_ms >0 , this function should be called at process context
 * wait_ms == 0 means that there is no need to wait ack through C2H_CCX_TX_RPT
 * wait_ms > 0 means you want to wait ack through C2H_CCX_TX_RPT, and the value of wait_ms means the interval between each TX
 * try_cnt means the maximal TX count to try
 * da == NULL for station mode
 */
int issue_qos_nulldata(_adapter *padapter, unsigned char *da, u16 tid, u8 ps, int try_cnt, int wait_ms)
{
	int ret = _FAIL;
	int i = 0;
	systime start = rtw_get_current_time();
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		goto exit;

	/* da == NULL, assum it's null data for sta to ap*/
	if (da == NULL)
		da = get_my_bssid(&(pmlmeinfo->network));

	do {
		ret = _issue_qos_nulldata(padapter, da, tid, ps, wait_ms > 0 ? _TRUE : _FALSE);

		i++;

		if (RTW_CANNOT_RUN(padapter))
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			rtw_msleep_os(wait_ms);

	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
#ifndef DBG_XMIT_ACK
		goto exit;
#endif
	}

	if (try_cnt && wait_ms) {
		if (da)
			RTW_INFO(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), MAC_ARG(da), rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
		else
			RTW_INFO(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
	}
exit:
	return ret;
}

static int _issue_deauth(_adapter *padapter, unsigned char *da, unsigned short reason, u8 wait_ack, u8 key_type)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	int ret = _FAIL;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
#endif /* CONFIG_P2P	 */

	/* RTW_INFO("%s to "MAC_FMT"\n", __func__, MAC_ARG(da)); */

#ifdef CONFIG_P2P
	if (!(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) && (pwdinfo->rx_invitereq_info.scan_op_ch_only)) {
		_cancel_timer_ex(&pwdinfo->reset_ch_sitesurvey);
		_set_timer(&pwdinfo->reset_ch_sitesurvey, 10);
	}
#endif /* CONFIG_P2P */

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		goto exit;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->retry_ctrl = _FALSE;
	pattrib->key_type = key_type;
	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_DEAUTH);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	reason = cpu_to_le16(reason);
	pframe = rtw_set_fixed_ie(pframe, _RSON_CODE_ , (unsigned char *)&reason, &(pattrib->pktlen));

	pattrib->last_txcmdsz = pattrib->pktlen;


	if (wait_ack)
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

int issue_deauth(_adapter *padapter, unsigned char *da, unsigned short reason)
{
	RTW_INFO("%s reason(%u) to "MAC_FMT"\n", __func__, reason, MAC_ARG(da));
	return _issue_deauth(padapter, da, reason, _FALSE, IEEE80211W_RIGHT_KEY);
}

#ifdef CONFIG_IEEE80211W
int issue_deauth_11w(_adapter *padapter, unsigned char *da, unsigned short reason, u8 key_type)
{
	RTW_INFO("%s to "MAC_FMT"\n", __func__, MAC_ARG(da));
	return _issue_deauth(padapter, da, reason, _FALSE, key_type);
}
#endif /* CONFIG_IEEE80211W */

/*
 * wait_ms == 0 means that there is no need to wait ack through C2H_CCX_TX_RPT
 * wait_ms > 0 means you want to wait ack through C2H_CCX_TX_RPT, and the value of wait_ms means the interval between each TX
 * try_cnt means the maximal TX count to try
 */
int issue_deauth_ex(_adapter *padapter, u8 *da, unsigned short reason, int try_cnt,
		    int wait_ms)
{
	int ret = _FAIL;
	int i = 0;
	systime start = rtw_get_current_time();

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		goto exit;

	do {
		ret = _issue_deauth(padapter, da, reason, wait_ms > 0 ? _TRUE : _FALSE, IEEE80211W_RIGHT_KEY);

		i++;

		if (RTW_CANNOT_RUN(padapter))
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			rtw_msleep_os(wait_ms);

	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
#ifndef DBG_XMIT_ACK
		goto exit;
#endif
	}

	if (try_cnt && wait_ms) {
		if (da)
			RTW_INFO(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), MAC_ARG(da), rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
		else
			RTW_INFO(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
	}
exit:
	return ret;
}

void issue_action_spct_ch_switch(_adapter *padapter, u8 *ra, u8 new_ch, u8 ch_offset)
{
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char				*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short			*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		return;

	RTW_INFO(FUNC_NDEV_FMT" ra="MAC_FMT", ch:%u, offset:%u\n",
		FUNC_NDEV_ARG(padapter->pnetdev), MAC_ARG(ra), new_ch, ch_offset);

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

	_rtw_memcpy(pwlanhdr->addr1, ra, ETH_ALEN); /* RA */
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN); /* TA */
	_rtw_memcpy(pwlanhdr->addr3, ra, ETH_ALEN); /* DA = RA */

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	/* category, action */
	{
		u8 category, action;
		category = RTW_WLAN_CATEGORY_SPECTRUM_MGMT;
		action = RTW_WLAN_ACTION_SPCT_CHL_SWITCH;

		pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
		pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	}

	pframe = rtw_set_ie_ch_switch(pframe, &(pattrib->pktlen), 0, new_ch, 0);
	pframe = rtw_set_ie_secondary_ch_offset(pframe, &(pattrib->pktlen),
			hal_ch_offset_to_secondary_ch_offset(ch_offset));

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

}

#ifdef CONFIG_IEEE80211W
void issue_action_SA_Query(_adapter *padapter, unsigned char *raddr, unsigned char action, unsigned short tid, u8 key_type)
{
	u8	category = RTW_WLAN_CATEGORY_SA_QUERY;
	u16	reason_code;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	u8					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_info		*psta;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct registry_priv		*pregpriv = &padapter->registrypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		return;

	RTW_INFO("%s, %04x\n", __FUNCTION__, tid);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL) {
		RTW_INFO("%s: alloc_mgtxmitframe fail\n", __FUNCTION__);
		return;
	}

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->key_type = key_type;
	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	if (raddr)
		_rtw_memcpy(pwlanhdr->addr1, raddr, ETH_ALEN);
	else
		_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 1, &action, &pattrib->pktlen);

	switch (action) {
	case 0: /* SA Query req */
		pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)&pmlmeext->sa_query_seq, &pattrib->pktlen);
		pmlmeext->sa_query_seq++;
		/* send sa query request to AP, AP should reply sa query response in 1 second */
		if (pattrib->key_type == IEEE80211W_RIGHT_KEY) {
			psta = rtw_get_stainfo(pstapriv, pwlanhdr->addr1);
			if (psta != NULL) {
				/* RTW_INFO("%s, %d, set dot11w_expire_timer\n", __func__, __LINE__); */
				_set_timer(&psta->dot11w_expire_timer, 1000);
			}
		}
		break;

	case 1: /* SA Query rsp */
		tid = cpu_to_le16(tid);
		/* RTW_INFO("rtw_set_fixed_ie, %04x\n", tid); */
		pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)&tid, &pattrib->pktlen);
		break;
	default:
		break;
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);
}
#endif /* CONFIG_IEEE80211W */

/**
 * issue_action_ba - internal function to TX Block Ack action frame
 * @padapter: the adapter to TX
 * @raddr: receiver address
 * @action: Block Ack Action
 * @tid: tid
 * @size: the announced AMPDU buffer size. used by ADDBA_RESP
 * @status: status/reason code. used by ADDBA_RESP, DELBA
 * @initiator: if we are the initiator of AMPDU association. used by DELBA
 * @wait_ack: used xmit ack
 *
 * Returns:
 * _SUCCESS: No xmit ack is used or acked
 * _FAIL: not acked when using xmit ack
 */
static int issue_action_ba(_adapter *padapter, unsigned char *raddr, unsigned char action
		   , u8 tid, u8 size, u16 status, u8 initiator, int wait_ack)
{
	int ret = _FAIL;
	u8	category = RTW_WLAN_CATEGORY_BACK;
	u16	start_seq;
	u16	BA_para_set;
	u16	BA_timeout_value;
	u16	BA_starting_seqctrl;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	u8					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_info		*psta;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct registry_priv		*pregpriv = &padapter->registrypriv;

#ifdef CONFIG_80211N_HT

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		goto exit;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	/* _rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN); */
	_rtw_memcpy(pwlanhdr->addr1, raddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));

	if (category == 3) {
		switch (action) {
		case RTW_WLAN_ACTION_ADDBA_REQ:
			do {
				pmlmeinfo->dialogToken++;
			} while (pmlmeinfo->dialogToken == 0);
			pframe = rtw_set_fixed_ie(pframe, 1, &(pmlmeinfo->dialogToken), &(pattrib->pktlen));

#if defined(CONFIG_RTL8188E) && defined(CONFIG_SDIO_HCI)
			BA_para_set = (0x0802 | ((tid & 0xf) << 2)); /* immediate ack & 16 buffer size */
#else
			BA_para_set = (0x1002 | ((tid & 0xf) << 2)); /* immediate ack & 64 buffer size */
#endif

#ifdef CONFIG_TX_AMSDU
			if (padapter->tx_amsdu >= 1) /* TX AMSDU  enabled */
				BA_para_set |= BIT(0);
			else /* TX AMSDU disabled */
				BA_para_set &= ~BIT(0);
#endif
			BA_para_set = cpu_to_le16(BA_para_set);
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(BA_para_set)), &(pattrib->pktlen));

			/* BA_timeout_value = 0xffff; */ /* max: 65535 TUs(~ 65 ms) */
			BA_timeout_value = 5000;/* ~ 5ms */
			BA_timeout_value = cpu_to_le16(BA_timeout_value);
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(BA_timeout_value)), &(pattrib->pktlen));

			/* if ((psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress)) != NULL) */
			psta = rtw_get_stainfo(pstapriv, raddr);
			if (psta != NULL) {
				start_seq = (psta->sta_xmitpriv.txseq_tid[tid & 0x07] & 0xfff) + 1;

				RTW_INFO("BA_starting_seqctrl = %d for TID=%d\n", start_seq, tid & 0x07);

				psta->BA_starting_seqctrl[tid & 0x07] = start_seq;

				BA_starting_seqctrl = start_seq << 4;
			}

			BA_starting_seqctrl = cpu_to_le16(BA_starting_seqctrl);
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(BA_starting_seqctrl)), &(pattrib->pktlen));
			break;

		case RTW_WLAN_ACTION_ADDBA_RESP:
			pframe = rtw_set_fixed_ie(pframe, 1, &(pmlmeinfo->ADDBA_req.dialog_token), &(pattrib->pktlen));
			status = cpu_to_le16(status);
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&status), &(pattrib->pktlen));

			BA_para_set = le16_to_cpu(pmlmeinfo->ADDBA_req.BA_para_set);

			BA_para_set &= ~IEEE80211_ADDBA_PARAM_TID_MASK;
			BA_para_set |= (tid << 2) & IEEE80211_ADDBA_PARAM_TID_MASK;

			BA_para_set &= ~RTW_IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK;
			BA_para_set |= (size << 6) & RTW_IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK;

			if (!padapter->registrypriv.wifi_spec) {
				if (pregpriv->rx_ampdu_amsdu == 0) /* disabled */
					BA_para_set &= ~BIT(0);
				else if (pregpriv->rx_ampdu_amsdu == 1) /* enabled */
					BA_para_set |= BIT(0);
			}

			BA_para_set = cpu_to_le16(BA_para_set);

			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(BA_para_set)), &(pattrib->pktlen));
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(pmlmeinfo->ADDBA_req.BA_timeout_value)), &(pattrib->pktlen));
			break;

		case RTW_WLAN_ACTION_DELBA:
			BA_para_set = 0;
			BA_para_set |= (tid << 12) & IEEE80211_DELBA_PARAM_TID_MASK;
			BA_para_set |= (initiator << 11) & IEEE80211_DELBA_PARAM_INITIATOR_MASK;

			BA_para_set = cpu_to_le16(BA_para_set);
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(BA_para_set)), &(pattrib->pktlen));
			status = cpu_to_le16(status);
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(status)), &(pattrib->pktlen));
			break;
		default:
			break;
		}
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	if (wait_ack)
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
#endif /* CONFIG_80211N_HT */
	return ret;
}

/**
 * issue_addba_req - TX ADDBA_REQ
 * @adapter: the adapter to TX
 * @ra: receiver address
 * @tid: tid
 */
inline void issue_addba_req(_adapter *adapter, unsigned char *ra, u8 tid)
{
	issue_action_ba(adapter, ra, RTW_WLAN_ACTION_ADDBA_REQ
			, tid
			, 0 /* unused */
			, 0 /* unused */
			, 0 /* unused */
			, _FALSE
		       );
	RTW_INFO(FUNC_ADPT_FMT" ra="MAC_FMT" tid=%u\n"
		 , FUNC_ADPT_ARG(adapter), MAC_ARG(ra), tid);

}

/**
 * issue_addba_rsp - TX ADDBA_RESP
 * @adapter: the adapter to TX
 * @ra: receiver address
 * @tid: tid
 * @status: status code
 * @size: the announced AMPDU buffer size
 */
inline void issue_addba_rsp(_adapter *adapter, unsigned char *ra, u8 tid, u16 status, u8 size)
{
	issue_action_ba(adapter, ra, RTW_WLAN_ACTION_ADDBA_RESP
			, tid
			, size
			, status
			, 0 /* unused */
			, _FALSE
		       );
	RTW_INFO(FUNC_ADPT_FMT" ra="MAC_FMT" status=%u, tid=%u, size=%u\n"
		 , FUNC_ADPT_ARG(adapter), MAC_ARG(ra), status, tid, size);
}

/**
 * issue_addba_rsp_wait_ack - TX ADDBA_RESP and wait ack
 * @adapter: the adapter to TX
 * @ra: receiver address
 * @tid: tid
 * @status: status code
 * @size: the announced AMPDU buffer size
 * @try_cnt: the maximal TX count to try
 * @wait_ms: == 0 means that there is no need to wait ack through C2H_CCX_TX_RPT
 *           > 0 means you want to wait ack through C2H_CCX_TX_RPT, and the value of wait_ms means the interval between each TX
 */
inline u8 issue_addba_rsp_wait_ack(_adapter *adapter, unsigned char *ra, u8 tid, u16 status, u8 size, int try_cnt, int wait_ms)
{
	int ret = _FAIL;
	int i = 0;
	systime start = rtw_get_current_time();

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(adapter)))
		goto exit;

	do {
		ret = issue_action_ba(adapter, ra, RTW_WLAN_ACTION_ADDBA_RESP
				      , tid
				      , size
				      , status
				      , 0 /* unused */
				      , _TRUE
				     );

		i++;

		if (RTW_CANNOT_RUN(adapter))
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			rtw_msleep_os(wait_ms);

	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
#ifndef DBG_XMIT_ACK
		/* goto exit; */
#endif
	}

	if (try_cnt && wait_ms) {
		RTW_INFO(FUNC_ADPT_FMT" ra="MAC_FMT" status:=%u tid=%u size:%u%s, %d/%d in %u ms\n"
			, FUNC_ADPT_ARG(adapter), MAC_ARG(ra), status, tid, size
			, ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
	}

exit:
	return ret;
}

/**
 * issue_del_ba - TX DELBA
 * @adapter: the adapter to TX
 * @ra: receiver address
 * @tid: tid
 * @reason: reason code
 * @initiator: if we are the initiator of AMPDU association. used by DELBA
 */
inline void issue_del_ba(_adapter *adapter, unsigned char *ra, u8 tid, u16 reason, u8 initiator)
{
	issue_action_ba(adapter, ra, RTW_WLAN_ACTION_DELBA
			, tid
			, 0 /* unused */
			, reason
			, initiator
			, _FALSE
		       );
	RTW_INFO(FUNC_ADPT_FMT" ra="MAC_FMT" reason=%u, tid=%u, initiator=%u\n"
		 , FUNC_ADPT_ARG(adapter), MAC_ARG(ra), reason, tid, initiator);
}

/**
 * issue_del_ba_ex - TX DELBA with xmit ack options
 * @adapter: the adapter to TX
 * @ra: receiver address
 * @tid: tid
 * @reason: reason code
 * @initiator: if we are the initiator of AMPDU association. used by DELBA
 * @try_cnt: the maximal TX count to try
 * @wait_ms: == 0 means that there is no need to wait ack through C2H_CCX_TX_RPT
 *           > 0 means you want to wait ack through C2H_CCX_TX_RPT, and the value of wait_ms means the interval between each TX
 */
int issue_del_ba_ex(_adapter *adapter, unsigned char *ra, u8 tid, u16 reason, u8 initiator
		    , int try_cnt, int wait_ms)
{
	int ret = _FAIL;
	int i = 0;
	systime start = rtw_get_current_time();

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(adapter)))
		goto exit;

	do {
		ret = issue_action_ba(adapter, ra, RTW_WLAN_ACTION_DELBA
				      , tid
				      , 0 /* unused */
				      , reason
				      , initiator
				      , wait_ms > 0 ? _TRUE : _FALSE
				     );

		i++;

		if (RTW_CANNOT_RUN(adapter))
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			rtw_msleep_os(wait_ms);

	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
#ifndef DBG_XMIT_ACK
		/* goto exit; */
#endif
	}

	if (try_cnt && wait_ms) {
		RTW_INFO(FUNC_ADPT_FMT" ra="MAC_FMT" reason=%u, tid=%u, initiator=%u%s, %d/%d in %u ms\n"
			, FUNC_ADPT_ARG(adapter), MAC_ARG(ra), reason, tid, initiator
			, ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
	}
exit:
	return ret;
}

void issue_action_BSSCoexistPacket(_adapter *padapter)
{
	_irqL	irqL;
	_list		*plist, *phead;
	unsigned char category, action;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char				*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short			*fctrl;
	struct	wlan_network	*pnetwork = NULL;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	_queue		*queue	= &(pmlmepriv->scanned_queue);
	u8 InfoContent[16] = {0};
	u8 ICS[8][15];
#ifdef CONFIG_80211N_HT
	if ((pmlmepriv->num_FortyMHzIntolerant == 0) && (pmlmepriv->num_sta_no_ht == 0))
		return;

	if (_TRUE == pmlmeinfo->bwmode_updated)
		return;

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		return;

	RTW_INFO("%s\n", __FUNCTION__);


	category = RTW_WLAN_CATEGORY_PUBLIC;
	action = ACT_PUBLIC_BSSCOEXIST;

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

	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));

	/* TODO calculate 40Mhz intolerant via ch and ch offset */
	/* if (pmlmepriv->num_FortyMHzIntolerant > 0) */
	{
		u8 iedata = 0;

		iedata |= BIT(2);/* 20 MHz BSS Width Request */
		pframe = rtw_set_ie(pframe, EID_BSSCoexistence,  1, &iedata, &(pattrib->pktlen));
	}

	/*  */
	_rtw_memset(ICS, 0, sizeof(ICS));
	if (pmlmepriv->num_sta_no_ht > 0) {
		int i;

		_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

		phead = get_list_head(queue);
		plist = get_next(phead);

		while (1) {
			int len;
			u8 *p;
			WLAN_BSSID_EX *pbss_network;

			if (rtw_end_of_queue_search(phead, plist) == _TRUE)
				break;

			pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

			plist = get_next(plist);

			pbss_network = (WLAN_BSSID_EX *)&pnetwork->network;

			p = rtw_get_ie(pbss_network->IEs + _FIXED_IE_LENGTH_, _HT_CAPABILITY_IE_, &len, pbss_network->IELength - _FIXED_IE_LENGTH_);
			if ((p == NULL) || (len == 0)) { /* non-HT */
				if ((pbss_network->Configuration.DSConfig <= 0) || (pbss_network->Configuration.DSConfig > 14))
					continue;

				ICS[0][pbss_network->Configuration.DSConfig] = 1;

				if (ICS[0][0] == 0)
					ICS[0][0] = 1;
			}

		}

		_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);


		for (i = 0; i < 8; i++) {
			if (ICS[i][0] == 1) {
				int j, k = 0;

				InfoContent[k] = i;
				/* SET_BSS_INTOLERANT_ELE_REG_CLASS(InfoContent,i); */
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
#endif /* CONFIG_80211N_HT */
}

/* Spatial Multiplexing Powersave (SMPS) action frame */
int _issue_action_SM_PS(_adapter *padapter ,  unsigned char *raddr , u8 NewMimoPsMode ,  u8 wait_ack)
{

	int ret = _FAIL;
	unsigned char category = RTW_WLAN_CATEGORY_HT;
	u8 action = RTW_WLAN_ACTION_HT_SM_PS;
	u8 sm_power_control = 0;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);


	if (NewMimoPsMode == WLAN_HT_CAP_SM_PS_DISABLED) {
		sm_power_control = sm_power_control  & ~(BIT(0)); /* SM Power Save Enable = 0 SM Power Save Disable */
	} else if (NewMimoPsMode == WLAN_HT_CAP_SM_PS_STATIC) {
		sm_power_control = sm_power_control | BIT(0);    /* SM Power Save Enable = 1 SM Power Save Enable  */
		sm_power_control = sm_power_control & ~(BIT(1)); /* SM Mode = 0 Static Mode */
	} else if (NewMimoPsMode == WLAN_HT_CAP_SM_PS_DYNAMIC) {
		sm_power_control = sm_power_control | BIT(0); /* SM Power Save Enable = 1 SM Power Save Enable  */
		sm_power_control = sm_power_control | BIT(1); /* SM Mode = 1 Dynamic Mode */
	} else
		return ret;

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		return ret;

	RTW_INFO("%s, sm_power_control=%u, NewMimoPsMode=%u\n", __FUNCTION__ , sm_power_control , NewMimoPsMode);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return ret;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, raddr, ETH_ALEN); /* RA */
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN); /* TA */
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN); /* DA = RA */

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	/* category, action */
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));

	pframe = rtw_set_fixed_ie(pframe, 1, &(sm_power_control), &(pattrib->pktlen));

	pattrib->last_txcmdsz = pattrib->pktlen;

	if (wait_ack)
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

	if (ret != _SUCCESS)
		RTW_INFO("%s, ack to\n", __func__);

	return ret;
}

/*
 * wait_ms == 0 means that there is no need to wait ack through C2H_CCX_TX_RPT
 * wait_ms > 0 means you want to wait ack through C2H_CCX_TX_RPT, and the value of wait_ms means the interval between each TX
 * try_cnt means the maximal TX count to try
 */
int issue_action_SM_PS_wait_ack(_adapter *padapter, unsigned char *raddr, u8 NewMimoPsMode, int try_cnt, int wait_ms)
{
	int ret = _FAIL;
	int i = 0;
	systime start = rtw_get_current_time();

	if (rtw_rfctl_is_tx_blocked_by_ch_waiting(adapter_to_rfctl(padapter)))
		goto exit;

	do {
		ret = _issue_action_SM_PS(padapter, raddr, NewMimoPsMode , wait_ms > 0 ? _TRUE : _FALSE);

		i++;

		if (RTW_CANNOT_RUN(padapter))
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			rtw_msleep_os(wait_ms);

	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
#ifndef DBG_XMIT_ACK
		goto exit;
#endif
	}

	if (try_cnt && wait_ms) {
		if (raddr)
			RTW_INFO(FUNC_ADPT_FMT" to "MAC_FMT", %s , %d/%d in %u ms\n",
				 FUNC_ADPT_ARG(padapter), MAC_ARG(raddr),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
		else
			RTW_INFO(FUNC_ADPT_FMT", %s , %d/%d in %u ms\n",
				 FUNC_ADPT_ARG(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
	}
exit:

	return ret;
}

int issue_action_SM_PS(_adapter *padapter ,  unsigned char *raddr , u8 NewMimoPsMode)
{
	RTW_INFO("%s to "MAC_FMT"\n", __func__, MAC_ARG(raddr));
	return _issue_action_SM_PS(padapter, raddr, NewMimoPsMode , _FALSE);
}

/**
 * _send_delba_sta_tid - Cancel the AMPDU association for the specific @sta, @tid
 * @adapter: the adapter to which @sta belongs
 * @initiator: if we are the initiator of AMPDU association
 * @sta: the sta to be checked
 * @tid: the tid to be checked
 * @force: cancel and send DELBA even when no AMPDU association is setup
 * @wait_ack: send delba with xmit ack (valid when initiator == 0)
 *
 * Returns:
 * _FAIL if sta is NULL
 * when initiator is 1, always _SUCCESS
 * when initiator is 0, _SUCCESS if DELBA is acked
 */
static unsigned int _send_delba_sta_tid(_adapter *adapter, u8 initiator, struct sta_info *sta, u8 tid
					, u8 force, int wait_ack)
{
	int ret = _SUCCESS;

	if (sta == NULL) {
		ret = _FAIL;
		goto exit;
	}

	if (initiator == 0) {
		/* recipient */
		if (force || sta->recvreorder_ctrl[tid].enable == _TRUE) {
			u8 ampdu_size_bak = sta->recvreorder_ctrl[tid].ampdu_size;

			sta->recvreorder_ctrl[tid].enable = _FALSE;
			sta->recvreorder_ctrl[tid].ampdu_size = RX_AMPDU_SIZE_INVALID;

			if (rtw_del_rx_ampdu_test_trigger_no_tx_fail())
				ret = _FAIL;
			else if (wait_ack)
				ret = issue_del_ba_ex(adapter, sta->cmn.mac_addr, tid, 37, initiator, 3, 1);
			else
				issue_del_ba(adapter, sta->cmn.mac_addr, tid, 37, initiator);

			if (ret == _FAIL && sta->recvreorder_ctrl[tid].enable == _FALSE)
				sta->recvreorder_ctrl[tid].ampdu_size = ampdu_size_bak;
		}
	} else if (initiator == 1) {
		/* originator */
#ifdef CONFIG_80211N_HT
		if (force || sta->htpriv.agg_enable_bitmap & BIT(tid)) {
			sta->htpriv.agg_enable_bitmap &= ~BIT(tid);
			sta->htpriv.candidate_tid_bitmap &= ~BIT(tid);
			issue_del_ba(adapter, sta->cmn.mac_addr, tid, 37, initiator);
		}
#endif
	}

exit:
	return ret;
}

inline unsigned int send_delba_sta_tid(_adapter *adapter, u8 initiator, struct sta_info *sta, u8 tid
				       , u8 force)
{
	return _send_delba_sta_tid(adapter, initiator, sta, tid, force, 0);
}

inline unsigned int send_delba_sta_tid_wait_ack(_adapter *adapter, u8 initiator, struct sta_info *sta, u8 tid
		, u8 force)
{
	return _send_delba_sta_tid(adapter, initiator, sta, tid, force, 1);
}

unsigned int send_delba(_adapter *padapter, u8 initiator, u8 *addr)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta = NULL;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u16 tid;

	if ((pmlmeinfo->state & 0x03) != WIFI_FW_AP_STATE)
		if (!(pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS))
			return _SUCCESS;

	psta = rtw_get_stainfo(pstapriv, addr);
	if (psta == NULL)
		return _SUCCESS;

#if 0
	RTW_INFO("%s:%s\n", __func__, (initiator == 0) ? "RX_DIR" : "TX_DIR");
	if (initiator == 1) /* originator */
		RTW_INFO("tx agg_enable_bitmap(0x%08x)\n", psta->htpriv.agg_enable_bitmap);
#endif

	for (tid = 0; tid < TID_NUM; tid++)
		send_delba_sta_tid(padapter, initiator, psta, tid, 0);

	return _SUCCESS;
}

#ifdef CONFIG_AP_MODE
unsigned int send_beacon(_adapter *padapter)
{
#if defined(CONFIG_PCI_HCI) && !defined(CONFIG_PCI_BCN_POLLING)
	#ifdef CONFIG_FW_HANDLE_TXBCN
	u8 vap_id = padapter->vap_id;

	/* bypass TX BCN because vap_id is invalid*/
	if (vap_id == CONFIG_LIMITED_AP_NUM)
		return _SUCCESS;
	#endif

	/* bypass TX BCN queue because op ch is switching/waiting */
	if (check_fwstate(&padapter->mlmepriv, WIFI_OP_CH_SWITCHING)
		|| IS_CH_WAITING(adapter_to_rfctl(padapter))
	)
		return _SUCCESS;

	/* RTW_INFO("%s\n", __FUNCTION__); */

	rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);

	/* 8192EE Port select for Beacon DL */
	rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);
	#ifdef CONFIG_FW_HANDLE_TXBCN
	rtw_hal_set_hwreg(padapter, HW_VAR_BCN_HEAD_SEL, &vap_id);
	#endif

	issue_beacon(padapter, 0);

	#ifdef CONFIG_FW_HANDLE_TXBCN
	vap_id = 0xFF;
	rtw_hal_set_hwreg(padapter, HW_VAR_BCN_HEAD_SEL, &vap_id);
	#endif

	#ifdef RTL8814AE_SW_BCN
	if (GET_HAL_DATA(padapter)->bCorrectBCN != 0)
		RTW_INFO("%s, line%d, Warnning, pHalData->bCorrectBCN != 0\n", __func__, __LINE__);
	GET_HAL_DATA(padapter)->bCorrectBCN = 1;
	#endif

	return _SUCCESS;
#endif

/* CONFIG_PCI_BCN_POLLING is for pci interface beacon polling mode */
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)|| defined(CONFIG_PCI_BCN_POLLING) 
	u8 bxmitok = _FALSE;
	int issue = 0;
	int poll = 0;
	systime start = rtw_get_current_time();
	#ifdef CONFIG_FW_HANDLE_TXBCN
	u8 vap_id = padapter->vap_id;

	/* bypass TX BCN because vap_id is invalid*/
	if (vap_id == CONFIG_LIMITED_AP_NUM)
		return _SUCCESS;
	#endif

	/* bypass TX BCN queue because op ch is switching/waiting */
	if (check_fwstate(&padapter->mlmepriv, WIFI_OP_CH_SWITCHING)
		|| IS_CH_WAITING(adapter_to_rfctl(padapter))
	)
		return _SUCCESS;

	#if defined(CONFIG_USB_HCI)
	#if defined(CONFIG_RTL8812A)
	if (IS_FULL_SPEED_USB(padapter)) {
		issue_beacon(padapter, 300);
		bxmitok = _TRUE;
	} else
	#endif
	#endif
	{
		rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
		rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);
		#ifdef CONFIG_FW_HANDLE_TXBCN
		rtw_hal_set_hwreg(padapter, HW_VAR_BCN_HEAD_SEL, &vap_id);
		#endif
		do {
			#if defined(CONFIG_PCI_BCN_POLLING) 
			issue_beacon(padapter, 0);
			#else
			issue_beacon(padapter, 100);
			#endif
			issue++;
			do {
				#if defined(CONFIG_PCI_BCN_POLLING) 
				rtw_msleep_os(1);
				#else
				rtw_yield_os();
				#endif
				rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, (u8 *)(&bxmitok));
				poll++;
			} while ((poll % 10) != 0 && _FALSE == bxmitok && !RTW_CANNOT_RUN(padapter));
			#if defined(CONFIG_PCI_BCN_POLLING) 
			rtw_hal_unmap_beacon_icf(padapter);
			#endif
		} while (bxmitok == _FALSE && (issue < 100) && !RTW_CANNOT_RUN(padapter));
		#ifdef CONFIG_FW_HANDLE_TXBCN
		vap_id = 0xFF;
		rtw_hal_set_hwreg(padapter, HW_VAR_BCN_HEAD_SEL, &vap_id);
		#endif
	}
	if (RTW_CANNOT_RUN(padapter))
		return _FAIL;


	if (_FALSE == bxmitok) {
		RTW_INFO("%s fail! %u ms\n", __FUNCTION__, rtw_get_passing_time_ms(start));
		#ifdef CONFIG_BCN_RECOVERY
		GET_HAL_DATA(padapter)->issue_bcn_fail++;
		#endif  /*CONFIG_BCN_RECOVERY*/
		return _FAIL;
	} else {
		u32 passing_time = rtw_get_passing_time_ms(start);

		if (passing_time > 100 || issue > 3)
			RTW_INFO("%s success, issue:%d, poll:%d, %u ms\n", __FUNCTION__, issue, poll, rtw_get_passing_time_ms(start));
		else if (0)
			RTW_INFO("%s success, issue:%d, poll:%d, %u ms\n", __FUNCTION__, issue, poll, rtw_get_passing_time_ms(start));

		#ifdef CONFIG_FW_CORRECT_BCN
		rtw_hal_fw_correct_bcn(padapter);
		#endif
		return _SUCCESS;
	}

#endif /*defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)*/

}
#endif /* CONFIG_AP_MODE */

/****************************************************************************

Following are some utitity fuctions for WiFi MLME

*****************************************************************************/

BOOLEAN IsLegal5GChannel(
	PADAPTER			Adapter,
	u8			channel)
{

	int i = 0;
	u8 Channel_5G[45] = {36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,
		60, 62, 64, 100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122,
		124, 126, 128, 130, 132, 134, 136, 138, 140, 149, 151, 153, 155, 157, 159,
			     161, 163, 165
			    };
	for (i = 0; i < sizeof(Channel_5G); i++)
		if (channel == Channel_5G[i])
			return _TRUE;
	return _FALSE;
}

/* collect bss info from Beacon and Probe request/response frames. */
u8 collect_bss_info(_adapter *padapter, union recv_frame *precv_frame, WLAN_BSSID_EX *bssid)
{
	int	i;
	sint len;
	u8	*p;
	u8	rf_path;
	u16	val16, subtype;
	u8	*pframe = precv_frame->u.hdr.rx_data;
	u32	packet_len = precv_frame->u.hdr.len;
	u8 ie_offset;
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(padapter);
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);


	len = packet_len - sizeof(struct rtw_ieee80211_hdr_3addr);

	if (len > MAX_IE_SZ) {
		/* RTW_INFO("IE too long for survey event\n"); */
		return _FAIL;
	}

	_rtw_memset(bssid, 0, sizeof(WLAN_BSSID_EX));

	subtype = get_frame_sub_type(pframe);

	if (subtype == WIFI_BEACON) {
		bssid->Reserved[0] = BSS_TYPE_BCN;
		ie_offset = _BEACON_IE_OFFSET_;
	} else {
		/* FIXME : more type */
		if (subtype == WIFI_PROBERSP) {
			ie_offset = _PROBERSP_IE_OFFSET_;
			bssid->Reserved[0] = BSS_TYPE_PROB_RSP;
		} else if (subtype == WIFI_PROBEREQ) {
			ie_offset = _PROBEREQ_IE_OFFSET_;
			bssid->Reserved[0] = BSS_TYPE_PROB_REQ;
		} else {
			bssid->Reserved[0] = BSS_TYPE_UNDEF;
			ie_offset = _FIXED_IE_LENGTH_;
		}
	}

	bssid->Length = sizeof(WLAN_BSSID_EX) - MAX_IE_SZ + len;

	/* below is to copy the information element */
	bssid->IELength = len;
	_rtw_memcpy(bssid->IEs, (pframe + sizeof(struct rtw_ieee80211_hdr_3addr)), bssid->IELength);

	/* get the signal strength */
	/* bssid->Rssi = precv_frame->u.hdr.attrib.SignalStrength; */ /* 0-100 index. */
	bssid->Rssi = precv_frame->u.hdr.attrib.phy_info.recv_signal_power; /* in dBM.raw data */
	bssid->PhyInfo.SignalQuality = precv_frame->u.hdr.attrib.phy_info.signal_quality;/* in percentage */
	bssid->PhyInfo.SignalStrength = precv_frame->u.hdr.attrib.phy_info.signal_strength;/* in percentage */

	/* get rx_snr */
	if (precv_frame->u.hdr.attrib.data_rate >= DESC_RATE11M) {
		bssid->PhyInfo.is_cck_rate = 0;
		for (rf_path = 0; rf_path < hal_spec->rf_reg_path_num; rf_path++)
			bssid->PhyInfo.rx_snr[rf_path] =
				precv_frame->u.hdr.attrib.phy_info.rx_snr[rf_path];
	} else
		bssid->PhyInfo.is_cck_rate = 1;

#ifdef CONFIG_ANTENNA_DIVERSITY
	rtw_hal_get_odm_var(padapter, HAL_ODM_ANTDIV_SELECT, &(bssid->PhyInfo.Optimum_antenna), NULL);
#endif

	/* checking SSID */
	p = rtw_get_ie(bssid->IEs + ie_offset, _SSID_IE_, &len, bssid->IELength - ie_offset);
	if (p == NULL) {
		RTW_INFO("marc: cannot find SSID for survey event\n");
		return _FAIL;
	}

	if (*(p + 1)) {
		if (len > NDIS_802_11_LENGTH_SSID) {
			RTW_INFO("%s()-%d: IE too long (%d) for survey event\n", __FUNCTION__, __LINE__, len);
			return _FAIL;
		}
		_rtw_memcpy(bssid->Ssid.Ssid, (p + 2), *(p + 1));
		bssid->Ssid.SsidLength = *(p + 1);
	} else
		bssid->Ssid.SsidLength = 0;

	_rtw_memset(bssid->SupportedRates, 0, NDIS_802_11_LENGTH_RATES_EX);

	/* checking rate info... */
	i = 0;
	p = rtw_get_ie(bssid->IEs + ie_offset, _SUPPORTEDRATES_IE_, &len, bssid->IELength - ie_offset);
	if (p != NULL) {
		if (len > NDIS_802_11_LENGTH_RATES_EX) {
			RTW_INFO("%s()-%d: IE too long (%d) for survey event\n", __FUNCTION__, __LINE__, len);
			return _FAIL;
		}
#ifdef CONFIG_CHECK_SPECIFIC_IE_CONTENT
		if (rtw_validate_value(_SUPPORTEDRATES_IE_, p+2, len) == _FALSE) {
			rtw_absorb_ssid_ifneed(padapter, bssid, pframe);
			RTW_DBG_DUMP("Invalidated Support Rate IE --", p, len+2);
			return _FAIL;
		}
#endif /* #ifdef CONFIG_CHECK_SPECIFIC_IE_CONTENT */
		_rtw_memcpy(bssid->SupportedRates, (p + 2), len);
		i = len;
	}

	p = rtw_get_ie(bssid->IEs + ie_offset, _EXT_SUPPORTEDRATES_IE_, &len, bssid->IELength - ie_offset);
	if (p != NULL) {
		if (len > (NDIS_802_11_LENGTH_RATES_EX - i)) {
			RTW_INFO("%s()-%d: IE too long (%d) for survey event\n", __FUNCTION__, __LINE__, len);
			return _FAIL;
		}
#ifdef CONFIG_CHECK_SPECIFIC_IE_CONTENT
		if (rtw_validate_value(_EXT_SUPPORTEDRATES_IE_, p+2, len) == _FALSE) {
			rtw_absorb_ssid_ifneed(padapter, bssid, pframe);
			RTW_DBG_DUMP("Invalidated EXT Support Rate IE --", p, len+2);
			return _FAIL;
		}
#endif /* #ifdef CONFIG_CHECK_SPECIFIC_IE_CONTENT */
		_rtw_memcpy(bssid->SupportedRates + i, (p + 2), len);
	}

#ifdef CONFIG_P2P
	if (subtype == WIFI_PROBEREQ) {
		u8 *p2p_ie;
		u32	p2p_ielen;
		/* Set Listion Channel */
		p2p_ie = rtw_get_p2p_ie(bssid->IEs, bssid->IELength, NULL, &p2p_ielen);
		if (p2p_ie) {
			u32	attr_contentlen = 0;
			u8 listen_ch[5] = { 0x00 };

			rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_LISTEN_CH, listen_ch, &attr_contentlen);
			bssid->Configuration.DSConfig = listen_ch[4];
		} else {
			/* use current channel */
			bssid->Configuration.DSConfig = padapter->mlmeextpriv.cur_channel;
			RTW_INFO("%s()-%d: Cannot get p2p_ie. set DSconfig to op_ch(%d)\n", __FUNCTION__, __LINE__, bssid->Configuration.DSConfig);
		}

		/* FIXME */
		bssid->InfrastructureMode = Ndis802_11Infrastructure;
		_rtw_memcpy(bssid->MacAddress, get_addr2_ptr(pframe), ETH_ALEN);
		bssid->Privacy = 1;
		return _SUCCESS;
	}
#endif /* CONFIG_P2P */

	if (bssid->IELength < 12)
		return _FAIL;

	/* Checking for DSConfig */
	p = rtw_get_ie(bssid->IEs + ie_offset, _DSSET_IE_, &len, bssid->IELength - ie_offset);

	bssid->Configuration.DSConfig = 0;
	bssid->Configuration.Length = 0;

	if (p)
		bssid->Configuration.DSConfig = *(p + 2);
	else {
		/* In 5G, some ap do not have DSSET IE */
		/* checking HT info for channel */
		p = rtw_get_ie(bssid->IEs + ie_offset, _HT_ADD_INFO_IE_, &len, bssid->IELength - ie_offset);
		if (p) {
			struct HT_info_element *HT_info = (struct HT_info_element *)(p + 2);
			bssid->Configuration.DSConfig = HT_info->primary_channel;
		} else {
			/* use current channel */
			bssid->Configuration.DSConfig = rtw_get_oper_ch(padapter);
		}
	}

	_rtw_memcpy(&bssid->Configuration.BeaconPeriod, rtw_get_beacon_interval_from_ie(bssid->IEs), 2);
	bssid->Configuration.BeaconPeriod = le32_to_cpu(bssid->Configuration.BeaconPeriod);

	val16 = rtw_get_capability((WLAN_BSSID_EX *)bssid);

	if ((val16 & 0x03) == cap_ESS) {
		bssid->InfrastructureMode = Ndis802_11Infrastructure;
		_rtw_memcpy(bssid->MacAddress, get_addr2_ptr(pframe), ETH_ALEN);
	} else if ((val16 & 0x03) == cap_IBSS){
		bssid->InfrastructureMode = Ndis802_11IBSS;
		_rtw_memcpy(bssid->MacAddress, GetAddr3Ptr(pframe), ETH_ALEN);
	} else if ((val16 & 0x03) == 0x00){
		u8 *mesh_id_ie, *mesh_conf_ie;
		sint mesh_id_ie_len, mesh_conf_ie_len;

		mesh_id_ie = rtw_get_ie(bssid->IEs + ie_offset, WLAN_EID_MESH_ID, &mesh_id_ie_len, bssid->IELength - ie_offset);
		mesh_conf_ie = rtw_get_ie(bssid->IEs + ie_offset, WLAN_EID_MESH_CONFIG, &mesh_conf_ie_len, bssid->IELength - ie_offset);
		if (mesh_id_ie || mesh_conf_ie) {
			if (!mesh_id_ie) {
				RTW_INFO("cannot find Mesh ID for survey event\n");
				return _FAIL;
			}
			if (mesh_id_ie_len) {
				if (mesh_id_ie_len > NDIS_802_11_LENGTH_SSID) {
					RTW_INFO("Mesh ID too long (%d) for survey event\n", mesh_id_ie_len);
					return _FAIL;
				}
				_rtw_memcpy(bssid->mesh_id.Ssid, (mesh_id_ie + 2), mesh_id_ie_len);
				bssid->mesh_id.SsidLength = mesh_id_ie_len;
			} else
				bssid->mesh_id.SsidLength = 0;

			if (!mesh_conf_ie) {
				RTW_INFO("cannot find Mesh config for survey event\n");
				return _FAIL;
			}
			if (mesh_conf_ie_len != 7) {
				RTW_INFO("invalid Mesh conf IE len (%d) for survey event\n", mesh_conf_ie_len);
				return _FAIL;
			}

			bssid->InfrastructureMode = Ndis802_11_mesh;
			_rtw_memcpy(bssid->MacAddress, GetAddr3Ptr(pframe), ETH_ALEN);
		} else {
			/* default cases */
			bssid->InfrastructureMode = Ndis802_11IBSS;
			_rtw_memcpy(bssid->MacAddress, GetAddr3Ptr(pframe), ETH_ALEN);
		}
	}

	if (val16 & BIT(4))
		bssid->Privacy = 1;
	else
		bssid->Privacy = 0;

	bssid->Configuration.ATIMWindow = 0;

	/* 20/40 BSS Coexistence check */
	if ((pregistrypriv->wifi_spec == 1) && (_FALSE == pmlmeinfo->bwmode_updated)) {
		struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
#ifdef CONFIG_80211N_HT
		p = rtw_get_ie(bssid->IEs + ie_offset, _HT_CAPABILITY_IE_, &len, bssid->IELength - ie_offset);
		if (p && len > 0) {
			struct HT_caps_element	*pHT_caps;
			pHT_caps = (struct HT_caps_element *)(p + 2);

			if (pHT_caps->u.HT_cap_element.HT_caps_info & BIT(14))
				pmlmepriv->num_FortyMHzIntolerant++;
		} else
			pmlmepriv->num_sta_no_ht++;
#endif /* CONFIG_80211N_HT */

	}

#if defined(DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) & 1
	if (strcmp(bssid->Ssid.Ssid, DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) == 0) {
		RTW_INFO("Receiving %s("MAC_FMT", DSConfig:%u) from ch%u with ss:%3u, sq:%3u, RawRSSI:%3ld\n"
			, bssid->Ssid.Ssid, MAC_ARG(bssid->MacAddress), bssid->Configuration.DSConfig
			 , rtw_get_oper_ch(padapter)
			, bssid->PhyInfo.SignalStrength, bssid->PhyInfo.SignalQuality, bssid->Rssi
			);
	}
#endif

	/* mark bss info receving from nearby channel as SignalQuality 101 */
	if (bssid->Configuration.DSConfig != rtw_get_oper_ch(padapter))
		bssid->PhyInfo.SignalQuality = 101;

#ifdef CONFIG_RTW_80211K
	p = rtw_get_ie(bssid->IEs + ie_offset, _EID_RRM_EN_CAP_IE_, &len, bssid->IELength - ie_offset);
	if (p)
		_rtw_memcpy(bssid->PhyInfo.rm_en_cap, (p + 2), *(p + 1));

	/* save freerun counter */
	bssid->PhyInfo.free_cnt = precv_frame->u.hdr.attrib.free_cnt;
#endif
	return _SUCCESS;
}

#ifdef CONFIG_AP_MODE
void start_create_ibss(_adapter *padapter)
{
	unsigned short	caps;
	u8	val8;
	u8	join_type;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*pnetwork = (WLAN_BSSID_EX *)(&(pmlmeinfo->network));
	u8 doiqk = _FALSE;
	pmlmeext->cur_channel = (u8)pnetwork->Configuration.DSConfig;
	pmlmeinfo->bcn_interval = get_beacon_interval(pnetwork);

	/* update wireless mode */
	update_wireless_mode(padapter);

	/* udpate capability */
	caps = rtw_get_capability((WLAN_BSSID_EX *)pnetwork);
	update_capinfo(padapter, caps);
	if (caps & cap_IBSS) { /* adhoc master */
		/* set_opmode_cmd(padapter, adhoc); */ /* removed */

		val8 = 0xcf;
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		doiqk = _TRUE;
		rtw_hal_set_hwreg(padapter , HW_VAR_DO_IQK , &doiqk);

		/* switch channel */
		set_channel_bwmode(padapter, pmlmeext->cur_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);

		doiqk = _FALSE;
		rtw_hal_set_hwreg(padapter , HW_VAR_DO_IQK , &doiqk);

		beacon_timing_control(padapter);

		/* set msr to WIFI_FW_ADHOC_STATE */
		pmlmeinfo->state = WIFI_FW_ADHOC_STATE;
		Set_MSR(padapter, (pmlmeinfo->state & 0x3));

		/* issue beacon */
		if (send_beacon(padapter) == _FAIL) {

			report_join_res(padapter, -1, WLAN_STATUS_UNSPECIFIED_FAILURE);
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
		} else {
			rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, padapter->registrypriv.dev_network.MacAddress);
			rtw_hal_rcr_set_chk_bssid(padapter, MLME_ADHOC_STARTED);
			join_type = 0;
			rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

			rtw_btcoex_connect_notify(padapter, join_type);

			report_join_res(padapter, 1, WLAN_STATUS_SUCCESS);
			pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;
			rtw_indicate_connect(padapter);
		}
	} else {
		RTW_INFO("start_create_ibss, invalid cap:%x\n", caps);
		return;
	}
	/* update bc/mc sta_info */
	update_bmc_sta(padapter);

}
#endif /* CONFIG_AP_MODE */

void start_clnt_join(_adapter *padapter)
{
	unsigned short	caps;
	u8	val8;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*pnetwork = (WLAN_BSSID_EX *)(&(pmlmeinfo->network));
	int beacon_timeout;
	u8 ASIX_ID[] = {0x00, 0x0E, 0xC6};

	/* update wireless mode */
	update_wireless_mode(padapter);

	/* udpate capability */
	caps = rtw_get_capability((WLAN_BSSID_EX *)pnetwork);
	update_capinfo(padapter, caps);

	/* check if sta is ASIX peer and fix IOT issue if it is. */
	if (_rtw_memcmp(get_my_bssid(&pmlmeinfo->network) , ASIX_ID , 3)) {
		u8 iot_flag = _TRUE;
		rtw_hal_set_hwreg(padapter, HW_VAR_ASIX_IOT, (u8 *)(&iot_flag));
	}

	if (caps & cap_ESS) {
		Set_MSR(padapter, WIFI_FW_STATION_STATE);

		val8 = (pmlmeinfo->auth_algo == dot11AuthAlgrthm_8021X) ? 0xcc : 0xcf;

#ifdef CONFIG_WAPI_SUPPORT
		if (padapter->wapiInfo.bWapiEnable && pmlmeinfo->auth_algo == dot11AuthAlgrthm_WAPI) {
			/* Disable TxUseDefaultKey, RxUseDefaultKey, RxBroadcastUseDefaultKey. */
			val8 = 0x4c;
		}
#endif
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

#ifdef CONFIG_DEAUTH_BEFORE_CONNECT
		/* Because of AP's not receiving deauth before */
		/* AP may: 1)not response auth or 2)deauth us after link is complete */
		/* issue deauth before issuing auth to deal with the situation */

		/*	Commented by Albert 2012/07/21 */
		/*	For the Win8 P2P connection, it will be hard to have a successful connection if this Wi-Fi doesn't connect to it. */
		{
#ifdef CONFIG_P2P
			_queue *queue = &(padapter->mlmepriv.scanned_queue);
			_list	*head = get_list_head(queue);
			_list *pos = get_next(head);
			struct wlan_network *scanned = NULL;
			u8 ie_offset = 0;
			_irqL irqL;
			bool has_p2p_ie = _FALSE;

			_enter_critical_bh(&(padapter->mlmepriv.scanned_queue.lock), &irqL);

			for (pos = get_next(head); !rtw_end_of_queue_search(head, pos); pos = get_next(pos)) {

				scanned = LIST_CONTAINOR(pos, struct wlan_network, list);

				if (_rtw_memcmp(&(scanned->network.Ssid), &(pnetwork->Ssid), sizeof(NDIS_802_11_SSID)) == _TRUE
				    && _rtw_memcmp(scanned->network.MacAddress, pnetwork->MacAddress, sizeof(NDIS_802_11_MAC_ADDRESS)) == _TRUE
				   ) {
					ie_offset = (scanned->network.Reserved[0] == BSS_TYPE_PROB_REQ ? 0 : 12);
					if (rtw_get_p2p_ie(scanned->network.IEs + ie_offset, scanned->network.IELength - ie_offset, NULL, NULL))
						has_p2p_ie = _TRUE;
					break;
				}
			}

			_exit_critical_bh(&(padapter->mlmepriv.scanned_queue.lock), &irqL);

			if (scanned == NULL || rtw_end_of_queue_search(head, pos) || has_p2p_ie == _FALSE)
#endif /* CONFIG_P2P */
				/* To avoid connecting to AP fail during resume process, change retry count from 5 to 1 */
				issue_deauth_ex(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING, 1, 100);
		}
#endif /* CONFIG_DEAUTH_BEFORE_CONNECT */

		/* here wait for receiving the beacon to start auth */
		/* and enable a timer */
		beacon_timeout = decide_wait_for_beacon_timeout(pmlmeinfo->bcn_interval);
		set_link_timer(pmlmeext, beacon_timeout);
		_set_timer(&padapter->mlmepriv.assoc_timer,
			(REAUTH_TO * REAUTH_LIMIT) + (REASSOC_TO * REASSOC_LIMIT) + beacon_timeout);

#ifdef CONFIG_RTW_80211R
		if (rtw_ft_roam(padapter)) {
			rtw_ft_start_clnt_join(padapter);
		} else
#endif
		{
			rtw_sta_linking_test_set_start();
			pmlmeinfo->state = WIFI_FW_AUTH_NULL | WIFI_FW_STATION_STATE;
		}
	} else if (caps & cap_IBSS) { /* adhoc client */
		Set_MSR(padapter, WIFI_FW_ADHOC_STATE);

		val8 = 0xcf;
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		beacon_timing_control(padapter);

		pmlmeinfo->state = WIFI_FW_ADHOC_STATE;

		report_join_res(padapter, 1, WLAN_STATUS_SUCCESS);
	} else {
		/* RTW_INFO("marc: invalid cap:%x\n", caps); */
		return;
	}

}

void start_clnt_auth(_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	_cancel_timer_ex(&pmlmeext->link_timer);

	pmlmeinfo->state &= (~WIFI_FW_AUTH_NULL);
	pmlmeinfo->state |= WIFI_FW_AUTH_STATE;

	pmlmeinfo->auth_seq = 1;
	pmlmeinfo->reauth_count = 0;
	pmlmeinfo->reassoc_count = 0;
	pmlmeinfo->link_count = 0;
	pmlmeext->retry = 0;

#ifdef CONFIG_RTW_80211R
	if (rtw_ft_roam(padapter)) {
		rtw_ft_set_status(padapter, RTW_FT_AUTHENTICATING_STA);
		RTW_PRINT("start ft auth\n");
	} else
#endif
		RTW_PRINT("start auth\n");

#ifdef CONFIG_IOCTL_CFG80211
	if (rtw_sec_chk_auth_type(padapter, MLME_AUTHTYPE_SAE)) {
		if (rtw_cached_pmkid(padapter, get_my_bssid(&pmlmeinfo->network)) != -1) {
			RTW_INFO("SAE: PMKSA cache entry found\n");
			padapter->securitypriv.auth_alg = WLAN_AUTH_OPEN;
			goto no_external_auth;
		}

		RTW_PRINT("SAE: start external auth\n");
		rtw_cfg80211_external_auth_request(padapter, NULL);
		return;
	}
no_external_auth:
#endif /* CONFIG_IOCTL_CFG80211 */

	issue_auth(padapter, NULL, 0);

	set_link_timer(pmlmeext, REAUTH_TO);

}


void start_clnt_assoc(_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	_cancel_timer_ex(&pmlmeext->link_timer);

	pmlmeinfo->state &= (~(WIFI_FW_AUTH_NULL | WIFI_FW_AUTH_STATE));
	pmlmeinfo->state |= (WIFI_FW_AUTH_SUCCESS | WIFI_FW_ASSOC_STATE);

#ifdef CONFIG_RTW_80211R
	if (rtw_ft_roam(padapter)
	#ifdef CONFIG_RTW_WNM
		|| rtw_wnm_btm_reassoc_req(padapter)
	#endif
	)
		issue_reassocreq(padapter);
	else
#endif
		issue_assocreq(padapter);

	set_link_timer(pmlmeext, REASSOC_TO);
}

unsigned int receive_disconnect(_adapter *padapter, unsigned char *MacAddr, unsigned short reason, u8 locally_generated)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (!(_rtw_memcmp(MacAddr, get_my_bssid(&pmlmeinfo->network), ETH_ALEN)))
		return _SUCCESS;

	RTW_INFO("%s\n", __FUNCTION__);

#ifdef CONFIG_RTW_REPEATER_SON
	rtw_rson_do_disconnect(padapter);
#endif
	if ((pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE) {
		if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) {
			if (report_del_sta_event(padapter, MacAddr, reason, _TRUE, locally_generated) != _FAIL)
				pmlmeinfo->state = WIFI_FW_NULL_STATE;
		} else if (pmlmeinfo->state & WIFI_FW_LINKING_STATE) {
			if (report_join_res(padapter, -2, reason) != _FAIL)
				pmlmeinfo->state = WIFI_FW_NULL_STATE;
		} else
			RTW_INFO(FUNC_ADPT_FMT" - End to Disconnect\n", FUNC_ADPT_ARG(padapter));
#ifdef CONFIG_RTW_80211R
		rtw_ft_roam_status_reset(padapter);
#endif
#ifdef CONFIG_RTW_WNM
		rtw_wnm_reset_btm_state(padapter);
#endif
	}

	return _SUCCESS;
}

static void rtw_hidden_ssid_bss_count(_adapter *adapter, WLAN_BSSID_EX *bss)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	RT_CHANNEL_INFO *chset = rfctl->channel_set;
	int chset_idx;

	if (bss->InfrastructureMode != Ndis802_11Infrastructure)
		return;

	if (!hidden_ssid_ap(bss))
		return;

	chset_idx = rtw_chset_search_ch(chset, bss->Configuration.DSConfig);
	if (chset_idx < 0)
		return;
	
	chset[chset_idx].hidden_bss_cnt++;
}

/****************************************************************************

Following are the functions to report events

*****************************************************************************/

void report_survey_event(_adapter *padapter, union recv_frame *precv_frame)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct survey_event *psurvey_evt;
	struct rtw_evt_header *evt_hdr;
	struct mlme_ext_priv *pmlmeext;
	struct cmd_priv *pcmdpriv;
	/* u8 *pframe = precv_frame->u.hdr.rx_data; */
	/* uint len = precv_frame->u.hdr.len; */

	if (!padapter)
		return;

	pmlmeext = &padapter->mlmeextpriv;
	pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd_obj == NULL)
		return;

	cmdsz = (sizeof(struct survey_event) + sizeof(struct rtw_evt_header));
	pevtcmd = (u8 *)rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		return;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = CMD_SET_MLME_EVT;
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	evt_hdr = (struct rtw_evt_header *)(pevtcmd);
	evt_hdr->len = sizeof(struct survey_event);
	evt_hdr->id = EVT_SURVEY;
	evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	psurvey_evt = (struct survey_event *)(pevtcmd + sizeof(struct rtw_evt_header));

	if (collect_bss_info(padapter, precv_frame, (WLAN_BSSID_EX *)&psurvey_evt->bss) == _FAIL) {
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		rtw_mfree((u8 *)pevtcmd, cmdsz);
		return;
	}

	rtw_hidden_ssid_bss_count(padapter, &psurvey_evt->bss);

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	pmlmeext->sitesurvey_res.bss_cnt++;

	return;

}

/*
* @acs: aim to trigger channel selection
*/
void report_surveydone_event(_adapter *padapter, bool acs)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct surveydone_event *psurveydone_evt;
	struct rtw_evt_header *evt_hdr;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd_obj == NULL)
		return;

	cmdsz = (sizeof(struct surveydone_event) + sizeof(struct rtw_evt_header));
	pevtcmd = (u8 *)rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		return;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = CMD_SET_MLME_EVT;
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	evt_hdr = (struct rtw_evt_header *)(pevtcmd);
	evt_hdr->len = sizeof(struct surveydone_event);
	evt_hdr->id = EVT_SURVEY_DONE;
	evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	psurveydone_evt = (struct surveydone_event *)(pevtcmd + sizeof(struct rtw_evt_header));
	psurveydone_evt->bss_cnt = pmlmeext->sitesurvey_res.bss_cnt;
	psurveydone_evt->activate_ch_cnt = pmlmeext->sitesurvey_res.activate_ch_cnt;
	psurveydone_evt->acs = acs;

	RTW_INFO("survey done event(%x) band:%d for "ADPT_FMT"\n", psurveydone_evt->bss_cnt, padapter->setband, ADPT_ARG(padapter));

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;

}

u32 report_join_res(_adapter *padapter, int aid_res, u16 status)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct joinbss_event *pjoinbss_evt;
	struct rtw_evt_header *evt_hdr;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u32 ret = _FAIL;

	pcmd_obj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd_obj == NULL)
		goto exit;

	cmdsz = (sizeof(struct joinbss_event) + sizeof(struct rtw_evt_header));
	pevtcmd = (u8 *)rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		goto exit;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = CMD_SET_MLME_EVT;
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	evt_hdr = (struct rtw_evt_header *)(pevtcmd);
	evt_hdr->len = sizeof(struct joinbss_event);
	evt_hdr->id = EVT_JOINBSS;
	evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	pjoinbss_evt = (struct joinbss_event *)(pevtcmd + sizeof(struct rtw_evt_header));
	_rtw_memcpy((unsigned char *)(&(pjoinbss_evt->network.network)), &(pmlmeinfo->network), sizeof(WLAN_BSSID_EX));
	pjoinbss_evt->network.join_res = pjoinbss_evt->network.aid = aid_res;

	RTW_INFO("report_join_res(%d, %u)\n", aid_res, status);


	rtw_joinbss_event_prehandle(padapter, (u8 *)&pjoinbss_evt->network, status);


	ret = rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

exit:
	return ret;
}

void report_wmm_edca_update(_adapter *padapter)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct wmm_event *pwmm_event;
	struct rtw_evt_header *evt_hdr;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd_obj == NULL)
		return;

	cmdsz = (sizeof(struct wmm_event) + sizeof(struct rtw_evt_header));
	pevtcmd = (u8 *)rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		return;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = CMD_SET_MLME_EVT;
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	evt_hdr = (struct rtw_evt_header *)(pevtcmd);
	evt_hdr->len = sizeof(struct wmm_event);
	evt_hdr->id = EVT_WMM_UPDATE;
	evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	pwmm_event = (struct wmm_event *)(pevtcmd + sizeof(struct rtw_evt_header));
	pwmm_event->wmm = 0;

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;

}

u32 report_del_sta_event(_adapter *padapter, unsigned char *MacAddr, unsigned short reason, bool enqueue, u8 locally_generated)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct sta_info *psta;
	int mac_id = -1;
	struct stadel_event *pdel_sta_evt;
	struct rtw_evt_header *evt_hdr;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	/* prepare cmd parameter */
	cmdsz = (sizeof(struct stadel_event) + sizeof(struct rtw_evt_header));
	pevtcmd = (u8 *)rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		res = _FAIL;
		goto exit;
	}

	evt_hdr = (struct rtw_evt_header *)(pevtcmd);
	evt_hdr->len = sizeof(struct stadel_event);
	evt_hdr->id = EVT_DEL_STA;
	evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	pdel_sta_evt = (struct stadel_event *)(pevtcmd + sizeof(struct rtw_evt_header));
	_rtw_memcpy((unsigned char *)(&(pdel_sta_evt->macaddr)), MacAddr, ETH_ALEN);
	_rtw_memcpy((unsigned char *)(pdel_sta_evt->rsvd), (unsigned char *)(&reason), 2);
	psta = rtw_get_stainfo(&padapter->stapriv, MacAddr);
	if (psta)
		mac_id = (int)psta->cmn.mac_id;
	else
		mac_id = (-1);
	pdel_sta_evt->mac_id = mac_id;
	pdel_sta_evt->locally_generated = locally_generated;

	if (!enqueue) {
		/* do directly */
		rtw_stadel_event_callback(padapter, (u8 *)pdel_sta_evt);
		rtw_mfree(pevtcmd, cmdsz);
	} else {
		pcmd_obj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
		if (pcmd_obj == NULL) {
			rtw_mfree(pevtcmd, cmdsz);
			res = _FAIL;
			goto exit;
		}

		_rtw_init_listhead(&pcmd_obj->list);
		pcmd_obj->cmdcode = CMD_SET_MLME_EVT;
		pcmd_obj->cmdsz = cmdsz;
		pcmd_obj->parmbuf = pevtcmd;

		pcmd_obj->rsp = NULL;
		pcmd_obj->rspsz  = 0;

		res = rtw_enqueue_cmd(pcmdpriv, pcmd_obj);
	}

exit:

	RTW_INFO(FUNC_ADPT_FMT" "MAC_FMT" mac_id=%d, enqueue:%d, res:%u\n"
		, FUNC_ADPT_ARG(padapter), MAC_ARG(MacAddr), mac_id, enqueue, res);

	return res;
}

void report_add_sta_event(_adapter *padapter, unsigned char *MacAddr)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct stassoc_event *padd_sta_evt;
	struct rtw_evt_header *evt_hdr;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd_obj == NULL)
		return;

	cmdsz = (sizeof(struct stassoc_event) + sizeof(struct rtw_evt_header));
	pevtcmd = (u8 *)rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		return;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = CMD_SET_MLME_EVT;
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	evt_hdr = (struct rtw_evt_header *)(pevtcmd);
	evt_hdr->len = sizeof(struct stassoc_event);
	evt_hdr->id = EVT_ADD_STA;
	evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	padd_sta_evt = (struct stassoc_event *)(pevtcmd + sizeof(struct rtw_evt_header));
	_rtw_memcpy((unsigned char *)(&(padd_sta_evt->macaddr)), MacAddr, ETH_ALEN);

	RTW_INFO("report_add_sta_event: add STA\n");

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;
}


bool rtw_port_switch_chk(_adapter *adapter)
{
	bool switch_needed = _FALSE;
#ifdef CONFIG_CONCURRENT_MODE
#ifdef CONFIG_RUNTIME_PORT_SWITCH
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct pwrctrl_priv *pwrctl = dvobj_to_pwrctl(dvobj);
	_adapter *if_port0 = NULL;
	_adapter *if_port1 = NULL;
	struct mlme_ext_info *if_port0_mlmeinfo = NULL;
	struct mlme_ext_info *if_port1_mlmeinfo = NULL;
	int i;

	for (i = 0; i < dvobj->iface_nums; i++) {
		if (get_hw_port(dvobj->padapters[i]) == HW_PORT0) {
			if_port0 = dvobj->padapters[i];
			if_port0_mlmeinfo = &(if_port0->mlmeextpriv.mlmext_info);
		} else if (get_hw_port(dvobj->padapters[i]) == HW_PORT1) {
			if_port1 = dvobj->padapters[i];
			if_port1_mlmeinfo = &(if_port1->mlmeextpriv.mlmext_info);
		}
	}

	if (if_port0 == NULL) {
		rtw_warn_on(1);
		goto exit;
	}

	if (if_port1 == NULL) {
		rtw_warn_on(1);
		goto exit;
	}

#ifdef DBG_RUNTIME_PORT_SWITCH
	RTW_INFO(FUNC_ADPT_FMT" wowlan_mode:%u\n"
		 ADPT_FMT", port0, mlmeinfo->state:0x%08x, p2p_state:%d, %d\n"
		 ADPT_FMT", port1, mlmeinfo->state:0x%08x, p2p_state:%d, %d\n",
		 FUNC_ADPT_ARG(adapter), pwrctl->wowlan_mode,
		ADPT_ARG(if_port0), if_port0_mlmeinfo->state, rtw_p2p_state(&if_port0->wdinfo), rtw_p2p_chk_state(&if_port0->wdinfo, P2P_STATE_NONE),
		ADPT_ARG(if_port1), if_port1_mlmeinfo->state, rtw_p2p_state(&if_port1->wdinfo), rtw_p2p_chk_state(&if_port1->wdinfo, P2P_STATE_NONE));
#endif /* DBG_RUNTIME_PORT_SWITCH */

#ifdef CONFIG_WOWLAN
	/* WOWLAN interface(primary, for now) should be port0 */
	if (pwrctl->wowlan_mode == _TRUE) {
		if (!is_primary_adapter(if_port0)) {
			RTW_INFO("%s "ADPT_FMT" enable WOWLAN\n", __func__, ADPT_ARG(if_port1));
			switch_needed = _TRUE;
		}
		goto exit;
	}
#endif /* CONFIG_WOWLAN */

	/* AP/Mesh should use port0 for ctl frame's ack */
	if ((if_port1_mlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) {
		RTW_INFO("%s "ADPT_FMT" is AP/GO/Mesh\n", __func__, ADPT_ARG(if_port1));
		switch_needed = _TRUE;
		goto exit;
	}

	/* GC should use port0 for p2p ps */
	if (((if_port1_mlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE)
	    && (if_port1_mlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
#ifdef CONFIG_P2P
	    && !rtw_p2p_chk_state(&if_port1->wdinfo, P2P_STATE_NONE)
#endif
	    && !check_fwstate(&if_port1->mlmepriv, WIFI_UNDER_WPS)
	   ) {
		RTW_INFO("%s "ADPT_FMT" is GC\n", __func__, ADPT_ARG(if_port1));
		switch_needed = _TRUE;
		goto exit;
	}

	/* port1 linked, but port0 not linked */
	if ((if_port1_mlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
	    && !(if_port0_mlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
	    && ((if_port0_mlmeinfo->state & 0x03) != WIFI_FW_AP_STATE)
	   ) {
		RTW_INFO("%s "ADPT_FMT" is SINGLE_LINK\n", __func__, ADPT_ARG(if_port1));
		switch_needed = _TRUE;
		goto exit;
	}

exit:
#ifdef DBG_RUNTIME_PORT_SWITCH
	RTW_INFO(FUNC_ADPT_FMT" ret:%d\n", FUNC_ADPT_ARG(adapter), switch_needed);
#endif /* DBG_RUNTIME_PORT_SWITCH */
#endif /* CONFIG_RUNTIME_PORT_SWITCH */
#endif /* CONFIG_CONCURRENT_MODE */
	return switch_needed;
}

/****************************************************************************

Following are the event callback functions

*****************************************************************************/

/* for sta/adhoc mode */
void update_sta_info(_adapter *padapter, struct sta_info *psta)
{
	_irqL	irqL;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	/* ERP */
	VCS_update(padapter, psta);

#ifdef CONFIG_80211N_HT
	/* HT */
	if (pmlmepriv->htpriv.ht_option) {
		psta->htpriv.ht_option = _TRUE;

		psta->htpriv.ampdu_enable = pmlmepriv->htpriv.ampdu_enable;

		psta->htpriv.rx_ampdu_min_spacing = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & IEEE80211_HT_CAP_AMPDU_DENSITY) >> 2;

		if (support_short_GI(padapter, &(pmlmeinfo->HT_caps), CHANNEL_WIDTH_20))
			psta->htpriv.sgi_20m = _TRUE;

		if (support_short_GI(padapter, &(pmlmeinfo->HT_caps), CHANNEL_WIDTH_40))
			psta->htpriv.sgi_40m = _TRUE;

		psta->qos_option = _TRUE;

		psta->htpriv.ldpc_cap = pmlmepriv->htpriv.ldpc_cap;
		psta->htpriv.stbc_cap = pmlmepriv->htpriv.stbc_cap;
		psta->htpriv.beamform_cap = pmlmepriv->htpriv.beamform_cap;

		_rtw_memcpy(&psta->htpriv.ht_cap, &pmlmeinfo->HT_caps, sizeof(struct rtw_ieee80211_ht_cap));
		#ifdef CONFIG_BEAMFORMING
		psta->htpriv.beamform_cap = pmlmepriv->htpriv.beamform_cap;
		psta->cmn.bf_info.ht_beamform_cap = pmlmepriv->htpriv.beamform_cap;
		#endif
	} else
#endif /* CONFIG_80211N_HT */
	{
#ifdef CONFIG_80211N_HT
		psta->htpriv.ht_option = _FALSE;
		psta->htpriv.ampdu_enable = _FALSE;
		psta->htpriv.tx_amsdu_enable = _FALSE;
		psta->htpriv.sgi_20m = _FALSE;
		psta->htpriv.sgi_40m = _FALSE;
#endif /* CONFIG_80211N_HT */
		psta->qos_option = _FALSE;

	}

#ifdef CONFIG_80211N_HT
	psta->htpriv.ch_offset = pmlmeext->cur_ch_offset;

	psta->htpriv.agg_enable_bitmap = 0x0;/* reset */
	psta->htpriv.candidate_tid_bitmap = 0x0;/* reset */
#endif /* CONFIG_80211N_HT */

	psta->cmn.bw_mode = pmlmeext->cur_bwmode;

	/* QoS */
	if (pmlmepriv->qospriv.qos_option)
		psta->qos_option = _TRUE;

#ifdef CONFIG_80211AC_VHT
	_rtw_memcpy(&psta->vhtpriv, &pmlmepriv->vhtpriv, sizeof(struct vht_priv));
	if (psta->vhtpriv.vht_option) {
		psta->cmn.ra_info.is_vht_enable = _TRUE;
		#ifdef CONFIG_BEAMFORMING
		psta->vhtpriv.beamform_cap = pmlmepriv->vhtpriv.beamform_cap;
		psta->cmn.bf_info.vht_beamform_cap = pmlmepriv->vhtpriv.beamform_cap;
		#endif /*CONFIG_BEAMFORMING*/
	}
#endif /* CONFIG_80211AC_VHT */
	psta->cmn.ra_info.is_support_sgi = query_ra_short_GI(psta, rtw_get_tx_bw_mode(padapter, psta));
	update_ldpc_stbc_cap(psta);

	_enter_critical_bh(&psta->lock, &irqL);
	psta->state = WIFI_ASOC_STATE;
	_exit_critical_bh(&psta->lock, &irqL);

}

static void rtw_mlmeext_disconnect(_adapter *padapter)
{
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 self_action = MLME_ACTION_UNKNOWN;
	u8 state_backup = (pmlmeinfo->state & 0x03);
	u8 ASIX_ID[] = {0x00, 0x0E, 0xC6};

	if (MLME_IS_AP(padapter))
		self_action = MLME_AP_STOPPED;
	else if (MLME_IS_MESH(padapter))
		self_action = MLME_MESH_STOPPED;
	else if (MLME_IS_STA(padapter))
		self_action = MLME_STA_DISCONNECTED;
	else if (MLME_IS_ADHOC(padapter) || MLME_IS_ADHOC_MASTER(padapter))
		self_action = MLME_ADHOC_STOPPED;
	else {
		RTW_INFO("state:0x%x\n", MLME_STATE(padapter));
		rtw_warn_on(1);
	}

	/* set_opmode_cmd(padapter, infra_client_with_mlme); */
#ifdef CONFIG_HW_P0_TSF_SYNC
	if (self_action == MLME_STA_DISCONNECTED)
		correct_TSF(padapter, self_action);
#endif
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_DISCONNECT, 0);
	rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, null_addr);
	if (self_action == MLME_STA_DISCONNECTED)
		rtw_hal_rcr_set_chk_bssid(padapter, self_action);

	/* set MSR to no link state->infra. mode */
	Set_MSR(padapter, _HW_STATE_STATION_);

	/* check if sta is ASIX peer and fix IOT issue if it is. */
	if (_rtw_memcmp(get_my_bssid(&pmlmeinfo->network) , ASIX_ID , 3)) {
		u8 iot_flag = _FALSE;
		rtw_hal_set_hwreg(padapter, HW_VAR_ASIX_IOT, (u8 *)(&iot_flag));
	}
	pmlmeinfo->state = WIFI_FW_NULL_STATE;

#ifdef CONFIG_MCC_MODE
	/* mcc disconnect setting before download LPS rsvd page */
	rtw_hal_set_mcc_setting_disconnect(padapter);
#endif /* CONFIG_MCC_MODE */

	if (state_backup == WIFI_FW_STATION_STATE) {
		if (rtw_port_switch_chk(padapter) == _TRUE) {
			rtw_hal_set_hwreg(padapter, HW_VAR_PORT_SWITCH, NULL);
#ifdef CONFIG_LPS
			{
				_adapter *port0_iface = dvobj_get_port0_adapter(adapter_to_dvobj(padapter));
				if (port0_iface)
					rtw_lps_ctrl_wk_cmd(port0_iface, LPS_CTRL_CONNECT, RTW_CMDF_DIRECTLY);
			}
#endif
		}
	}

	/* switch to the 20M Hz mode after disconnect */
	pmlmeext->cur_bwmode = CHANNEL_WIDTH_20;
	pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
#ifdef CONFIG_CTRL_TXSS_BY_TP
	pmlmeext->txss_1ss = _FALSE;
#endif

#ifdef CONFIG_FCS_MODE
	if (EN_FCS(padapter))
		rtw_hal_set_hwreg(padapter, HW_VAR_STOP_FCS_MODE, NULL);
#endif

	if (!(MLME_IS_STA(padapter) && MLME_IS_OPCH_SW(padapter))) {
		/* DFS and channel status no need to check here for STA under OPCH_SW */
		u8 ch, bw, offset;

		#ifdef CONFIG_DFS_MASTER
		rtw_dfs_rd_en_decision(padapter, self_action, 0);
		#endif

		if (rtw_mi_get_ch_setting_union_no_self(padapter, &ch, &bw, &offset) != 0)
			set_channel_bwmode(padapter, ch, offset, bw);
		rtw_mi_update_union_chan_inf(padapter, ch, offset, bw);
		rtw_rfctl_update_op_mode(adapter_to_rfctl(padapter), BIT(padapter->iface_id), 0);
	}

	flush_all_cam_entry(padapter);

	_cancel_timer_ex(&pmlmeext->link_timer);

	/* pmlmepriv->LinkDetectInfo.TrafficBusyState = _FALSE; */
	pmlmepriv->LinkDetectInfo.TrafficTransitionCount = 0;
	pmlmepriv->LinkDetectInfo.LowPowerTransitionCount = 0;

#ifdef CONFIG_TDLS
	padapter->tdlsinfo.ap_prohibited = _FALSE;

	/* For TDLS channel switch, currently we only allow it to work in wifi logo test mode */
	if (padapter->registrypriv.wifi_spec == 1)
		padapter->tdlsinfo.ch_switch_prohibited = _FALSE;
#endif /* CONFIG_TDLS */

#ifdef CONFIG_WMMPS_STA
	 if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		/* reset currently related uapsd setting when the connection has broken */
		pmlmepriv->qospriv.uapsd_max_sp_len = 0;
		pmlmepriv->qospriv.uapsd_tid = 0;
		pmlmepriv->qospriv.uapsd_tid_delivery_enabled = 0;
		pmlmepriv->qospriv.uapsd_tid_trigger_enabled = 0;
		pmlmepriv->qospriv.uapsd_ap_supported = 0;
	}
#endif /* CONFIG_WMMPS_STA */
#ifdef CONFIG_RTS_FULL_BW
	rtw_set_rts_bw(padapter);
#endif/*CONFIG_RTS_FULL_BW*/

}

void mlmeext_joinbss_event_callback(_adapter *padapter, int join_res)
{
	struct sta_info		*psta;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*cur_network = &(pmlmeinfo->network);
	struct sta_priv		*pstapriv = &padapter->stapriv;
	u8	join_type;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

#ifndef CONFIG_IOCTL_CFG80211
	struct security_priv *psecuritypriv = &padapter->securitypriv;
#endif

	if (pmlmepriv->wpa_phase == _TRUE)
		pmlmepriv->wpa_phase = _FALSE;

	if (join_res < 0) {
		join_type = 1;
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));
		rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, null_addr);
		if ((pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE)
			rtw_hal_rcr_set_chk_bssid(padapter, MLME_STA_DISCONNECTED);

		rtw_btcoex_connect_notify(padapter, join_type);

		goto exit_mlmeext_joinbss_event_callback;
	}

#ifdef CONFIG_ARP_KEEP_ALIVE
	pmlmepriv->bGetGateway = 1;
	pmlmepriv->GetGatewayTryCnt = 0;
#endif

#ifdef CONFIG_AP_MODE
	if ((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) {
		/* update bc/mc sta_info */
		update_bmc_sta(padapter);
	}
#endif

	/* turn on dynamic functions */
	/* Switch_DM_Func(padapter, DYNAMIC_ALL_FUNC_ENABLE, _TRUE); */

	/* update IOT-releated issue */
	update_IOT_info(padapter);

	#ifdef CONFIG_RTS_FULL_BW
	rtw_set_rts_bw(padapter);
	#endif/*CONFIG_RTS_FULL_BW*/

	rtw_hal_set_hwreg(padapter, HW_VAR_BASIC_RATE, cur_network->SupportedRates);

	/* BCN interval */
	rtw_hal_set_hwreg(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&pmlmeinfo->bcn_interval));

	/* udpate capability */
	update_capinfo(padapter, pmlmeinfo->capability);

	/* WMM, Update EDCA param */
	WMMOnAssocRsp(padapter);
#ifdef CONFIG_80211N_HT
	/* HT */
	HTOnAssocRsp(padapter);
#endif /* CONFIG_80211N_HT */
#ifdef CONFIG_80211AC_VHT
	/* VHT */
	VHTOnAssocRsp(padapter);
#endif

	psta = rtw_get_stainfo(pstapriv, cur_network->MacAddress);
	if (psta) { /* only for infra. mode */
		psta->wireless_mode = pmlmeext->cur_wireless_mode;

		/* set per sta rate after updating HT cap. */
		set_sta_rate(padapter, psta);

		rtw_sta_media_status_rpt(padapter, psta, 1);

		/* wakeup macid after join bss successfully to ensure
			the subsequent data frames can be sent out normally */
		rtw_hal_macid_wakeup(padapter, psta->cmn.mac_id);

		rtw_xmit_queue_clear(psta);
	}

#ifndef CONFIG_IOCTL_CFG80211
	if (is_wep_enc(psecuritypriv->dot11PrivacyAlgrthm))
		rtw_sec_restore_wep_key(padapter);
#endif /* CONFIG_IOCTL_CFG80211 */

	if (rtw_port_switch_chk(padapter) == _TRUE)
		rtw_hal_set_hwreg(padapter, HW_VAR_PORT_SWITCH, NULL);

	join_type = 2;
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

	if ((pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE) {
		rtw_hal_rcr_set_chk_bssid(padapter, MLME_STA_CONNECTED);

		/* correcting TSF */
		correct_TSF(padapter, MLME_STA_CONNECTED);

		/* set_link_timer(pmlmeext, DISCONNECT_TO); */
	}

#ifdef CONFIG_LPS
	#ifndef CONFIG_FW_MULTI_PORT_SUPPORT
	if (get_hw_port(padapter) == HW_PORT0)
	#endif
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_CONNECT, RTW_CMDF_DIRECTLY);
#endif

	rtw_btcoex_connect_notify(padapter, join_type);

#ifdef CONFIG_BEAMFORMING
	if (psta)
		beamforming_wk_cmd(padapter, BEAMFORMING_CTRL_ENTER, (u8 *)psta, sizeof(struct sta_info), 0);
#endif/*CONFIG_BEAMFORMING*/

exit_mlmeext_joinbss_event_callback:

	rtw_join_done_chk_ch(padapter, join_res);
#ifdef CONFIG_RTW_REPEATER_SON
	rtw_rson_join_done(padapter);
#endif

	RTW_INFO("=>%s - End to Connection without 4-way\n", __FUNCTION__);
}

/* currently only adhoc mode will go here */
void mlmeext_sta_add_event_callback(_adapter *padapter, struct sta_info *psta)
{
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	join_type;

	RTW_INFO("%s\n", __FUNCTION__);

	if ((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) {
		if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) { /* adhoc master or sta_count>1 */
			/* nothing to do */
		} else { /* adhoc client */
			#ifdef CONFIG_AP_MODE
			/* update TSF Value */
			/* update_TSF(pmlmeext, pframe, len);			 */

			/* correcting TSF */
			correct_TSF(padapter, MLME_ADHOC_STARTED);

			/* start beacon */
			if (send_beacon(padapter) == _FAIL)
				rtw_warn_on(1);

			pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;
			#endif
		}

		join_type = 2;
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

		rtw_btcoex_connect_notify(padapter, join_type);
	}

	/* update adhoc sta_info */
	update_sta_info(padapter, psta);

	rtw_hal_update_sta_ra_info(padapter, psta);

	/* ToDo: HT for Ad-hoc */
	psta->wireless_mode = rtw_check_network_type(psta->bssrateset, psta->bssratelen, pmlmeext->cur_channel);
	rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, psta, _TRUE);

	/* rate radaptive */
	Update_RA_Entry(padapter, psta);
}

void mlmeext_sta_del_event_callback(_adapter *padapter)
{
	if (is_client_associated_to_ap(padapter) || is_IBSS_empty(padapter))
		rtw_mlmeext_disconnect(padapter);
}

/****************************************************************************

Following are the functions for the timer handlers

*****************************************************************************/
void _linked_info_dump(_adapter *padapter)
{
	if (padapter->bLinkInfoDump) {
		rtw_hal_get_def_var(padapter, HW_DEF_RA_INFO_DUMP, RTW_DBGDUMP);
		rtw_hal_set_odm_var(padapter, HAL_ODM_RX_INFO_DUMP, RTW_DBGDUMP, _FALSE);
	}
}
/********************************************************************

When station does not receive any packet in MAX_CONTINUAL_NORXPACKET_COUNT*2 seconds,
recipient station will teardown the block ack by issuing DELBA frame.

*********************************************************************/
void rtw_delba_check(_adapter *padapter, struct sta_info *psta, u8 from_timer)
{
	int	i = 0;
	int ret = _SUCCESS;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	/*
		IOT issue,occur Broadcom ap(Buffalo WZR-D1800H,Netgear R6300).
		AP is originator.AP does not transmit unicast packets when STA response its BAR.
		This case probably occur ap issue BAR after AP builds BA.

		Follow 802.11 spec, STA shall maintain an inactivity timer for every negotiated Block Ack setup.
		The inactivity timer is not reset when MPDUs corresponding to other TIDs are received.
	*/
	if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_BROADCOM) {
		for (i = 0; i < TID_NUM ; i++) {
			if ((psta->recvreorder_ctrl[i].enable) && 
                        (sta_rx_data_qos_pkts(psta, i) == sta_last_rx_data_qos_pkts(psta, i)) ) {			
					if (_TRUE == rtw_inc_and_chk_continual_no_rx_packet(psta, i)) {					
						/* send a DELBA frame to the peer STA with the Reason Code field set to TIMEOUT */
						if (!from_timer)
							ret = issue_del_ba_ex(padapter, psta->cmn.mac_addr, i, 39, 0, 3, 1);
						else
							issue_del_ba(padapter,  psta->cmn.mac_addr, i, 39, 0);
						psta->recvreorder_ctrl[i].enable = _FALSE;
						if (ret != _FAIL)
							psta->recvreorder_ctrl[i].ampdu_size = RX_AMPDU_SIZE_INVALID;
						rtw_reset_continual_no_rx_packet(psta, i);
					}				
			} else {
				/* The inactivity timer is reset when MPDUs to the TID is received. */
				rtw_reset_continual_no_rx_packet(psta, i);
			}
		}
	}
}


u8 chk_ap_is_alive(_adapter *padapter, struct sta_info *psta)
{
	u8 ret = _FALSE;
#ifdef DBG_EXPIRATION_CHK
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	RTW_INFO(FUNC_ADPT_FMT" rx:"STA_PKTS_FMT", beacon:%llu, probersp_to_self:%llu"
		/*", probersp_bm:%llu, probersp_uo:%llu, probereq:%llu, BI:%u"*/
		 ", retry:%u\n"
		 , FUNC_ADPT_ARG(padapter)
		 , STA_RX_PKTS_DIFF_ARG(psta)
		, psta->sta_stats.rx_beacon_pkts - psta->sta_stats.last_rx_beacon_pkts
		, psta->sta_stats.rx_probersp_pkts - psta->sta_stats.last_rx_probersp_pkts
		/*, psta->sta_stats.rx_probersp_bm_pkts - psta->sta_stats.last_rx_probersp_bm_pkts
		, psta->sta_stats.rx_probersp_uo_pkts - psta->sta_stats.last_rx_probersp_uo_pkts
		, psta->sta_stats.rx_probereq_pkts - psta->sta_stats.last_rx_probereq_pkts
		 , pmlmeinfo->bcn_interval*/
		 , pmlmeext->retry
		);

	RTW_INFO(FUNC_ADPT_FMT" tx_pkts:%llu, link_count:%u\n", FUNC_ADPT_ARG(padapter)
		 , sta_tx_pkts(psta)
		 , pmlmeinfo->link_count
		);
#endif

	if ((sta_rx_data_pkts(psta) == sta_last_rx_data_pkts(psta))
	    && sta_rx_beacon_pkts(psta) == sta_last_rx_beacon_pkts(psta)
	    && sta_rx_probersp_pkts(psta) == sta_last_rx_probersp_pkts(psta)
	   )
		ret = _FALSE;
	else
		ret = _TRUE;

	sta_update_last_rx_pkts(psta);

	return ret;
}

u8 chk_adhoc_peer_is_alive(struct sta_info *psta)
{
	u8 ret = _TRUE;

#ifdef DBG_EXPIRATION_CHK
	RTW_INFO("sta:"MAC_FMT", rssi:%d, rx:"STA_PKTS_FMT", beacon:%llu, probersp_to_self:%llu"
		/*", probersp_bm:%llu, probersp_uo:%llu, probereq:%llu, BI:%u"*/
		 ", expire_to:%u\n"
		 , MAC_ARG(psta->cmn.mac_addr)
		 , psta->cmn.rssi_stat.rssi
		 , STA_RX_PKTS_DIFF_ARG(psta)
		, psta->sta_stats.rx_beacon_pkts - psta->sta_stats.last_rx_beacon_pkts
		, psta->sta_stats.rx_probersp_pkts - psta->sta_stats.last_rx_probersp_pkts
		/*, psta->sta_stats.rx_probersp_bm_pkts - psta->sta_stats.last_rx_probersp_bm_pkts
		, psta->sta_stats.rx_probersp_uo_pkts - psta->sta_stats.last_rx_probersp_uo_pkts
		, psta->sta_stats.rx_probereq_pkts - psta->sta_stats.last_rx_probereq_pkts
		 , pmlmeinfo->bcn_interval*/
		 , psta->expire_to
		);
#endif

	if (sta_rx_data_pkts(psta) == sta_last_rx_data_pkts(psta)
	    && sta_rx_beacon_pkts(psta) == sta_last_rx_beacon_pkts(psta)
	    && sta_rx_probersp_pkts(psta) == sta_last_rx_probersp_pkts(psta))
		ret = _FALSE;

	sta_update_last_rx_pkts(psta);

	return ret;
}

#ifdef CONFIG_TDLS
u8 chk_tdls_peer_sta_is_alive(_adapter *padapter, struct sta_info *psta)
{
	if ((psta->sta_stats.rx_data_pkts == psta->sta_stats.last_rx_data_pkts)
	    && (psta->sta_stats.rx_tdls_disc_rsp_pkts == psta->sta_stats.last_rx_tdls_disc_rsp_pkts))
		return _FALSE;

	return _TRUE;
}

void linked_status_chk_tdls(_adapter *padapter)
{
	struct candidate_pool {
		struct sta_info *psta;
		u8 addr[ETH_ALEN];
	};
	struct sta_priv *pstapriv = &padapter->stapriv;
	_irqL irqL;
	u8 ack_chk;
	struct sta_info *psta;
	int i, num_teardown = 0, num_checkalive = 0;
	_list	*plist, *phead;
	struct tdls_txmgmt txmgmt;
	struct candidate_pool checkalive[MAX_ALLOWED_TDLS_STA_NUM];
	struct candidate_pool teardown[MAX_ALLOWED_TDLS_STA_NUM];
	u8 tdls_sta_max = _FALSE;

#define ALIVE_MIN 2
#define ALIVE_MAX 5

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	_rtw_memset(checkalive, 0x00, sizeof(checkalive));
	_rtw_memset(teardown, 0x00, sizeof(teardown));

	if ((padapter->tdlsinfo.link_established == _TRUE)) {
		_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);
		for (i = 0; i < NUM_STA; i++) {
			phead = &(pstapriv->sta_hash[i]);
			plist = get_next(phead);

			while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
				psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);
				plist = get_next(plist);

				if (psta->tdls_sta_state & TDLS_LINKED_STATE) {
					psta->alive_count++;
					if (psta->alive_count >= ALIVE_MIN) {
						if (chk_tdls_peer_sta_is_alive(padapter, psta) == _FALSE) {
							if (psta->alive_count < ALIVE_MAX) {
								_rtw_memcpy(checkalive[num_checkalive].addr, psta->cmn.mac_addr, ETH_ALEN);
								checkalive[num_checkalive].psta = psta;
								num_checkalive++;
							} else {
								_rtw_memcpy(teardown[num_teardown].addr, psta->cmn.mac_addr, ETH_ALEN);
								teardown[num_teardown].psta = psta;
								num_teardown++;
							}
						} else
							psta->alive_count = 0;
					}
					psta->sta_stats.last_rx_data_pkts = psta->sta_stats.rx_data_pkts;
					psta->sta_stats.last_rx_tdls_disc_rsp_pkts = psta->sta_stats.rx_tdls_disc_rsp_pkts;

					if ((num_checkalive >= MAX_ALLOWED_TDLS_STA_NUM) || (num_teardown >= MAX_ALLOWED_TDLS_STA_NUM)) {
						tdls_sta_max = _TRUE;
						break;
					}
				}
			}

			if (tdls_sta_max == _TRUE)
				break;
		}
		_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);

		if (num_checkalive > 0) {
			for (i = 0; i < num_checkalive; i++) {
				_rtw_memcpy(txmgmt.peer, checkalive[i].addr, ETH_ALEN);
				issue_tdls_dis_req(padapter, &txmgmt);
				issue_tdls_dis_req(padapter, &txmgmt);
				issue_tdls_dis_req(padapter, &txmgmt);
			}
		}

		if (num_teardown > 0) {
			for (i = 0; i < num_teardown; i++) {
				RTW_INFO("[%s %d] Send teardown to "MAC_FMT"\n", __FUNCTION__, __LINE__, MAC_ARG(teardown[i].addr));
				txmgmt.status_code = _RSON_TDLS_TEAR_TOOFAR_;
				_rtw_memcpy(txmgmt.peer, teardown[i].addr, ETH_ALEN);
				issue_tdls_teardown(padapter, &txmgmt, _FALSE);
			}
		}
	}

}
#endif /* CONFIG_TDLS */

inline int rtw_get_rx_chk_limit(_adapter *adapter)
{
	return adapter->stapriv.rx_chk_limit;
}

inline void rtw_set_rx_chk_limit(_adapter *adapter, int limit)
{
	adapter->stapriv.rx_chk_limit = limit;
}

/* from_timer == 1 means driver is in LPS */
void linked_status_chk(_adapter *padapter, u8 from_timer)
{
	u32	i;
	struct sta_info		*psta;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
#ifdef CONFIG_LAYER2_ROAMING
	struct recv_priv	*precvpriv = &padapter->recvpriv;
#endif

#ifdef CONFIG_RTW_WDS
	rtw_wds_gptr_expire(padapter);
#endif

	if (padapter->registrypriv.mp_mode == _TRUE)
		return;
		
	if (check_fwstate(pmlmepriv, WIFI_CSA_UPDATE_BEACON))
		return;

	if (is_client_associated_to_ap(padapter)) {
		/* linked infrastructure client mode */

		int tx_chk = _SUCCESS, rx_chk = _SUCCESS;
		int rx_chk_limit;
		int link_count_limit;

#if defined(CONFIG_RTW_REPEATER_SON)
	rtw_rson_scan_wk_cmd(padapter, RSON_SCAN_PROCESS);
#elif defined(CONFIG_LAYER2_ROAMING)
		if (rtw_chk_roam_flags(padapter, RTW_ROAM_ACTIVE)) {
			RTW_INFO("signal_strength_data.avg_val = %d\n", precvpriv->signal_strength_data.avg_val);
			if ((precvpriv->signal_strength_data.avg_val < pmlmepriv->roam_rssi_threshold)
				&& (rtw_get_passing_time_ms(pmlmepriv->last_roaming) >= pmlmepriv->roam_scan_int*2000)) {
#ifdef CONFIG_RTW_80211K
				rtw_roam_nb_discover(padapter, _FALSE);
#endif
				pmlmepriv->need_to_roam = _TRUE;
				rtw_drv_scan_by_self(padapter, RTW_AUTO_SCAN_REASON_ROAM);
				pmlmepriv->last_roaming = rtw_get_current_time();
			} else
				pmlmepriv->need_to_roam = _FALSE;
		}
#endif
#ifdef CONFIG_MCC_MODE
		/*
		 * due to tx ps null date to ao, so ap doest not tx pkt to driver
		 * we may check chk_ap_is_alive fail, and may issue_probereq to wrong channel under sitesurvey
		 * don't keep alive check under MCC
		 */
		if (rtw_hal_mcc_link_status_chk(padapter, __func__) == _FALSE)
			return;
#endif

		rx_chk_limit = rtw_get_rx_chk_limit(padapter);

#ifdef CONFIG_ARP_KEEP_ALIVE
		if (!from_timer && pmlmepriv->bGetGateway == 1 && pmlmepriv->GetGatewayTryCnt < 3) {
			RTW_INFO("do rtw_gw_addr_query() : %d\n", pmlmepriv->GetGatewayTryCnt);
			pmlmepriv->GetGatewayTryCnt++;
			if (rtw_gw_addr_query(padapter) == 0)
				pmlmepriv->bGetGateway = 0;
			else {
				_rtw_memset(pmlmepriv->gw_ip, 0, 4);
				_rtw_memset(pmlmepriv->gw_mac_addr, 0, ETH_ALEN);
			}
		}
#endif
#ifdef CONFIG_P2P
		if (!rtw_p2p_chk_state(&padapter->wdinfo, P2P_STATE_NONE)) {
			if (!from_timer)
				link_count_limit = 3; /* 8 sec */
			else
				link_count_limit = 15; /* 32 sec */
		} else
#endif /* CONFIG_P2P */
		{
			if (!from_timer)
				link_count_limit = 7; /* 16 sec */
			else
				link_count_limit = 29; /* 60 sec */
		}

#ifdef CONFIG_TDLS
#ifdef CONFIG_TDLS_CH_SW
		if (ATOMIC_READ(&padapter->tdlsinfo.chsw_info.chsw_on) == _TRUE)
			return;
#endif /* CONFIG_TDLS_CH_SW */

#ifdef CONFIG_TDLS_AUTOCHECKALIVE
		linked_status_chk_tdls(padapter);
#endif /* CONFIG_TDLS_AUTOCHECKALIVE */
#endif /* CONFIG_TDLS */

		psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress);
		if (psta != NULL) {
			bool is_p2p_enable = _FALSE;
#ifdef CONFIG_P2P
			is_p2p_enable = !rtw_p2p_chk_state(&padapter->wdinfo, P2P_STATE_NONE);
#endif

#ifdef CONFIG_ISSUE_DELBA_WHEN_NO_TRAFFIC 
			/*issue delba when ap does not tx data packet that is Broadcom ap */
			rtw_delba_check(padapter, psta, from_timer);
#endif
			if (chk_ap_is_alive(padapter, psta) == _FALSE)
				rx_chk = _FAIL;

			if (sta_last_tx_pkts(psta) == sta_tx_pkts(psta))
				tx_chk = _FAIL;

#ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
			if (!from_timer && pmlmeext->active_keep_alive_check && (rx_chk == _FAIL || tx_chk == _FAIL)
			) {
				u8 backup_ch = 0, backup_bw = 0, backup_offset = 0;
				u8 union_ch = 0, union_bw = 0, union_offset = 0;
				u8 switch_channel_by_drv = _TRUE;

				
#ifdef CONFIG_MCC_MODE
				if (MCC_EN(padapter)) {
					/* driver doesn't switch channel under MCC */
					if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC))
						switch_channel_by_drv = _FALSE;
				}
#endif
				if (switch_channel_by_drv) {
					if (!rtw_mi_get_ch_setting_union(padapter, &union_ch, &union_bw, &union_offset)
						|| pmlmeext->cur_channel != union_ch)
							goto bypass_active_keep_alive;

					/* switch to correct channel of current network  before issue keep-alive frames */
					if (rtw_get_oper_ch(padapter) != pmlmeext->cur_channel) {
						backup_ch = rtw_get_oper_ch(padapter);
						backup_bw = rtw_get_oper_bw(padapter);
						backup_offset = rtw_get_oper_choffset(padapter);
						set_channel_bwmode(padapter, union_ch, union_offset, union_bw);
					}
				}

				if (rx_chk != _SUCCESS)
					issue_probereq_ex(padapter, &pmlmeinfo->network.Ssid, psta->cmn.mac_addr, 0, 0, 3, 1);

				if ((tx_chk != _SUCCESS && pmlmeinfo->link_count++ == link_count_limit) || rx_chk != _SUCCESS) {
					if (rtw_mi_check_fwstate(padapter, WIFI_UNDER_SURVEY))
						tx_chk = issue_nulldata(padapter, psta->cmn.mac_addr, 1, 3, 1);
					else
						tx_chk = issue_nulldata(padapter, psta->cmn.mac_addr, 0, 3, 1);
					/* if tx acked and p2p disabled, set rx_chk _SUCCESS to reset retry count */
					if (tx_chk == _SUCCESS && !is_p2p_enable)
						rx_chk = _SUCCESS;
				}

				/* back to the original operation channel */
				if (backup_ch > 0 && switch_channel_by_drv)
					set_channel_bwmode(padapter, backup_ch, backup_offset, backup_bw);

bypass_active_keep_alive:
				;
			} else
#endif /* CONFIG_ACTIVE_KEEP_ALIVE_CHECK */
			{
				if (rx_chk != _SUCCESS) {
					if (pmlmeext->retry == 0) {
#ifdef DBG_EXPIRATION_CHK
						RTW_INFO("issue_probereq to trigger probersp, retry=%d\n", pmlmeext->retry);
#endif
						issue_probereq_ex(padapter, &pmlmeinfo->network.Ssid, pmlmeinfo->network.MacAddress, 0, 0, 0, (from_timer ? 0 : 1));
						issue_probereq_ex(padapter, &pmlmeinfo->network.Ssid, pmlmeinfo->network.MacAddress, 0, 0, 0, (from_timer ? 0 : 1));
						issue_probereq_ex(padapter, &pmlmeinfo->network.Ssid, pmlmeinfo->network.MacAddress, 0, 0, 0, (from_timer ? 0 : 1));
					}
				}

				if (tx_chk != _SUCCESS && pmlmeinfo->link_count++ == link_count_limit
#ifdef CONFIG_MCC_MODE
				    /* FW tx nulldata under MCC mode, we just check  ap is alive */
				    && (!rtw_hal_check_mcc_status(padapter, MCC_STATUS_NEED_MCC))
#endif /* CONFIG_MCC_MODE */
				) {
					#ifdef DBG_EXPIRATION_CHK
					RTW_INFO("%s issue_nulldata(%d)\n", __FUNCTION__, from_timer ? 1 : 0);
					#endif
					if (from_timer || rtw_mi_check_fwstate(padapter, WIFI_UNDER_SURVEY))
						tx_chk = issue_nulldata(padapter, NULL, 1, 0, 0);
					else
						tx_chk = issue_nulldata(padapter, NULL, 0, 1, 1);
				}
			}

			if (rx_chk == _FAIL) {
				pmlmeext->retry++;
				if (pmlmeext->retry > rx_chk_limit) {
					RTW_PRINT(FUNC_ADPT_FMT" disconnect or roaming\n",
						  FUNC_ADPT_ARG(padapter));
					receive_disconnect(padapter, pmlmeinfo->network.MacAddress
						, WLAN_REASON_EXPIRATION_CHK, _FALSE);
					return;
				}
			} else
				pmlmeext->retry = 0;

			if (tx_chk == _FAIL)
				pmlmeinfo->link_count %= (link_count_limit + 1);
			else {
				psta->sta_stats.last_tx_pkts = psta->sta_stats.tx_pkts;
				pmlmeinfo->link_count = 0;
			}

		} /* end of if ((psta = rtw_get_stainfo(pstapriv, passoc_res->network.MacAddress)) != NULL) */

	} else if (is_client_associated_to_ibss(padapter)) {
		_irqL irqL;
		_list *phead, *plist, dlist;

		_rtw_init_listhead(&dlist);

		_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);

		for (i = 0; i < NUM_STA; i++) {

			phead = &(pstapriv->sta_hash[i]);
			plist = get_next(phead);
			while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
				psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);
				plist = get_next(plist);

				if (is_broadcast_mac_addr(psta->cmn.mac_addr))
					continue;

				if (chk_adhoc_peer_is_alive(psta) || !psta->expire_to)
					psta->expire_to = pstapriv->adhoc_expire_to;
				else
					psta->expire_to--;

				if (psta->expire_to <= 0) {
					rtw_list_delete(&psta->list);
					rtw_list_insert_tail(&psta->list, &dlist);
				}
			}
		}

		_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);

		plist = get_next(&dlist);
		while (rtw_end_of_queue_search(&dlist, plist) == _FALSE) {
			psta = LIST_CONTAINOR(plist, struct sta_info, list);
			plist = get_next(plist);
			rtw_list_delete(&psta->list);
			RTW_INFO(FUNC_ADPT_FMT" ibss expire "MAC_FMT"\n"
				, FUNC_ADPT_ARG(padapter), MAC_ARG(psta->cmn.mac_addr));
			report_del_sta_event(padapter, psta->cmn.mac_addr, WLAN_REASON_EXPIRATION_CHK, from_timer ? _TRUE : _FALSE, _FALSE);
		}
	}

}

void survey_timer_hdl(void *ctx)
{
	_adapter *padapter = (_adapter *)ctx;
	struct cmd_obj *cmd;
	struct sitesurvey_parm *psurveyPara;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	if (mlmeext_scan_state(pmlmeext) > SCAN_DISABLE) {
		cmd = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
		if (cmd == NULL) {
			rtw_warn_on(1);
			goto exit;
		}

		psurveyPara = (struct sitesurvey_parm *)rtw_zmalloc(sizeof(struct sitesurvey_parm));
		if (psurveyPara == NULL) {
			rtw_warn_on(1);
			rtw_mfree((unsigned char *)cmd, sizeof(struct cmd_obj));
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(cmd, psurveyPara, CMD_SITE_SURVEY);
		rtw_enqueue_cmd(pcmdpriv, cmd);
	}

exit:
	return;
}

#ifdef CONFIG_RTW_REPEATER_SON
/*	 100ms pass, stop rson_scan	*/
void rson_timer_hdl(void *ctx)
{
	_adapter *padapter = (_adapter *)ctx;

	rtw_rson_scan_wk_cmd(padapter, RSON_SCAN_DISABLE);
}

#endif

#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
void rtw_tbtx_xmit_timer_hdl(void *ctx)
{
	_adapter *padapter = (_adapter *)ctx;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	if (ATOMIC_READ(&padapter->tbtx_remove_tx_pause) == _TRUE){
		ATOMIC_SET(&padapter->tbtx_tx_pause, _FALSE);
		rtw_tx_control_cmd(padapter);
	}else {
		rtw_issue_action_token_rel(padapter);
		ATOMIC_SET(&padapter->tbtx_tx_pause, _TRUE);
		rtw_tx_control_cmd(padapter);
		_set_timer(&pmlmeext->tbtx_xmit_timer, MAX_TXPAUSE_DURATION);
		ATOMIC_SET(&padapter->tbtx_remove_tx_pause, _TRUE);
	}
}

#ifdef CONFIG_AP_MODE
void rtw_tbtx_token_dispatch_timer_hdl(void *ctx)
{
	_adapter *padapter = (_adapter *)ctx;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	_irqL irqL;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	_list *phead, *plist;
	int i, found = _FALSE;
	u8 nr_send, th_idx = 0;

	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	RTW_DBG("%s:asoc_cnt: %d\n",__func__, pstapriv->tbtx_asoc_list_cnt);

	// check number of TBTX sta
	if (padapter->stapriv.tbtx_asoc_list_cnt < 2)
		goto exit;

	// dispatch token
	
	nr_send = RTW_DIV_ROUND_UP(pstapriv->tbtx_asoc_list_cnt, NR_TBTX_SLOT);

	phead = &pstapriv->asoc_list;
	plist = get_next(phead);
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		/* psta is supporting TBTX */
		if ((!psta) || (!psta->tbtx_enable))
			RTW_DBG("sta tbtx_enable is false\n");
		else {
			for (i = 0; i < nr_send; i++) {
				if (pstapriv->last_token_holder == psta) {
					found = _TRUE;
					goto outof_loop;
				}
			}
		}
		plist = get_next(plist);
	}
outof_loop:
	
	RTW_DBG("rtw_tbtx_token_dispatch_timer_hdl()   th_idx=%d,  nr_send=%d, phead=%p, plist=%p, found=%d\n ", th_idx ,  nr_send, phead, plist, found);
	if (!found) {
		plist = get_next(phead);
		while(rtw_end_of_queue_search(phead, plist) == _FALSE) {
			psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
			if ((!psta) || (!psta->tbtx_enable))
				RTW_DBG("sta tbtx_enable is false\n");
			else {
					pstapriv->token_holder[th_idx] = psta;
					rtw_issue_action_token_req(padapter, pstapriv->token_holder[th_idx++]);
					break;
			}
			plist = get_next(plist);
		}
	}

	for (i=th_idx; i<nr_send;) {
		plist = get_next(plist);
		if (plist == phead)
			plist = get_next(plist);
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		if ((!psta) || (!psta->tbtx_enable))
			RTW_DBG("sta tbtx_enable is false\n");		
		else {
			pstapriv->token_holder[th_idx] = psta;
			rtw_issue_action_token_req(padapter, pstapriv->token_holder[th_idx++]);
			i++;
		}
	}
	ATOMIC_SET(&pstapriv->nr_token_keeper, nr_send);
	

exit:
	// set_timer
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	_set_timer(&pmlmeext->tbtx_token_dispatch_timer, TBTX_TX_DURATION); 
}
#endif /* CONFIG_AP_MODE */
#endif /* CONFIG_RTW_TOKEN_BASED_XMIT */

void link_timer_hdl(void *ctx)
{
	_adapter *padapter = (_adapter *)ctx;
	/* static unsigned int		rx_pkt = 0; */
	/* static u64				tx_cnt = 0; */
	/* struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv); */
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	/* struct sta_priv		*pstapriv = &padapter->stapriv; */
#ifdef CONFIG_RTW_80211R
	struct	sta_priv		*pstapriv = &padapter->stapriv;
	struct	sta_info		*psta = NULL;
	WLAN_BSSID_EX		*pnetwork = (WLAN_BSSID_EX *)(&(pmlmeinfo->network));
#endif

	if (rtw_sta_linking_test_force_fail())
		RTW_INFO("rtw_sta_linking_test_force_fail\n");

	if (pmlmeext->join_abort && pmlmeinfo->state != WIFI_FW_NULL_STATE) {
		RTW_INFO(FUNC_ADPT_FMT" join abort\n", FUNC_ADPT_ARG(padapter));
		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		report_join_res(padapter, -4, WLAN_STATUS_UNSPECIFIED_FAILURE);
		goto exit;
	}

	if (pmlmeinfo->state & WIFI_FW_AUTH_NULL) {
		RTW_INFO("link_timer_hdl:no beacon while connecting\n");
		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		report_join_res(padapter, -3, WLAN_STATUS_UNSPECIFIED_FAILURE);
	} else if (pmlmeinfo->state & WIFI_FW_AUTH_STATE) {

#ifdef CONFIG_IOCTL_CFG80211
		if (rtw_sec_chk_auth_type(padapter, MLME_AUTHTYPE_SAE))
			return;
#endif /* CONFIG_IOCTL_CFG80211 */

		/* re-auth timer */
		if (++pmlmeinfo->reauth_count > REAUTH_LIMIT) {
			/* if (pmlmeinfo->auth_algo != dot11AuthAlgrthm_Auto) */
			/* { */
			pmlmeinfo->state = 0;
			if (pmlmeinfo->auth_status) {
				report_join_res(padapter, -1, pmlmeinfo->auth_status);
				pmlmeinfo->auth_status = 0; /* reset */
			} else
				report_join_res(padapter, -1, WLAN_STATUS_UNSPECIFIED_FAILURE);
			return;
			/* } */
			/* else */
			/* { */
			/*	pmlmeinfo->auth_algo = dot11AuthAlgrthm_Shared; */
			/*	pmlmeinfo->reauth_count = 0; */
			/* } */
		}

		RTW_INFO("link_timer_hdl: auth timeout and try again\n");
		pmlmeinfo->auth_seq = 1;
		issue_auth(padapter, NULL, 0);
		set_link_timer(pmlmeext, REAUTH_TO);
	} else if (pmlmeinfo->state & WIFI_FW_ASSOC_STATE) {
		/* re-assoc timer */
		if (++pmlmeinfo->reassoc_count > REASSOC_LIMIT) {
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
#ifdef CONFIG_RTW_80211R
			if (rtw_ft_roam(padapter)) {
				psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress);
				if (psta)
					rtw_free_stainfo(padapter,  psta);
			}
#endif
			report_join_res(padapter, -2, WLAN_STATUS_UNSPECIFIED_FAILURE);
			return;
		}

#ifdef CONFIG_RTW_80211R
		if (rtw_ft_roam(padapter)) {
			RTW_INFO("link_timer_hdl: reassoc timeout and try again\n");
			issue_reassocreq(padapter);
		} else
#endif
		{
			RTW_INFO("link_timer_hdl: assoc timeout and try again\n");
			issue_assocreq(padapter);
		}

		set_link_timer(pmlmeext, REASSOC_TO);
	}

exit:
	return;
}

void addba_timer_hdl(void *ctx)
{
	struct sta_info *psta = (struct sta_info *)ctx;

#ifdef CONFIG_80211N_HT
	struct ht_priv	*phtpriv;

	if (!psta)
		return;

	phtpriv = &psta->htpriv;

	if ((phtpriv->ht_option == _TRUE) && (phtpriv->ampdu_enable == _TRUE)) {
		if (phtpriv->candidate_tid_bitmap)
			phtpriv->candidate_tid_bitmap = 0x0;

	}
#endif /* CONFIG_80211N_HT */
}

#ifdef CONFIG_IEEE80211W
void report_sta_timeout_event(_adapter *padapter, u8 *MacAddr, unsigned short reason)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct sta_info *psta;
	int	mac_id;
	struct stadel_event *pdel_sta_evt;
	struct rtw_evt_header *evt_hdr;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd_obj == NULL)
		return;

	cmdsz = (sizeof(struct stadel_event) + sizeof(struct rtw_evt_header));
	pevtcmd = (u8 *)rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		return;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = CMD_SET_MLME_EVT;
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	evt_hdr = (struct rtw_evt_header *)(pevtcmd);
	evt_hdr->len = sizeof(struct stadel_event);
	evt_hdr->id = EVT_TIMEOUT_STA;
	evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	pdel_sta_evt = (struct stadel_event *)(pevtcmd + sizeof(struct rtw_evt_header));
	_rtw_memcpy((unsigned char *)(&(pdel_sta_evt->macaddr)), MacAddr, ETH_ALEN);
	_rtw_memcpy((unsigned char *)(pdel_sta_evt->rsvd), (unsigned char *)(&reason), 2);


	psta = rtw_get_stainfo(&padapter->stapriv, MacAddr);
	if (psta)
		mac_id = (int)psta->cmn.mac_id;
	else
		mac_id = (-1);

	pdel_sta_evt->mac_id = mac_id;

	RTW_INFO("report_del_sta_event: delete STA, mac_id=%d\n", mac_id);

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;
}

void clnt_sa_query_timeout(_adapter *padapter)
{
	struct mlme_ext_priv *mlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *mlmeinfo = &(mlmeext->mlmext_info);

	RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));
	receive_disconnect(padapter, get_my_bssid(&(mlmeinfo->network)), WLAN_REASON_SA_QUERY_TIMEOUT, _FALSE);
}

void sa_query_timer_hdl(void *ctx)
{
	struct sta_info *psta = (struct sta_info *)ctx;
	_adapter *padapter = psta->padapter;
	_irqL irqL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE &&
	    check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		clnt_sa_query_timeout(padapter);
	else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
		report_sta_timeout_event(padapter, psta->cmn.mac_addr, WLAN_REASON_PREV_AUTH_NOT_VALID);
}

#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_AUTO_AP_MODE
void rtw_auto_ap_rx_msg_dump(_adapter *padapter, union recv_frame *precv_frame, u8 *ehdr_pos)
{
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_info *psta = precv_frame->u.hdr.psta;
	struct ethhdr *ehdr = (struct ethhdr *)ehdr_pos;

	RTW_INFO("eth rx: got eth_type=0x%x\n", ntohs(ehdr->h_proto));

	if (psta && psta->isrc && psta->pid > 0) {
		u16 rx_pid;

		rx_pid = *(u16 *)(ehdr_pos + ETH_HLEN);

		RTW_INFO("eth rx(pid=0x%x): sta("MAC_FMT") pid=0x%x\n",
			 rx_pid, MAC_ARG(psta->cmn.mac_addr), psta->pid);

		if (rx_pid == psta->pid) {
			int i;
			u16 len = *(u16 *)(ehdr_pos + ETH_HLEN + 2);
			/* u16 ctrl_type = *(u16 *)(ehdr_pos + ETH_HLEN + 4); */

			/* RTW_INFO("eth, RC: len=0x%x, ctrl_type=0x%x\n", len, ctrl_type);  */
			RTW_INFO("eth, RC: len=0x%x\n", len);

			for (i = 0; i < len; i++)
				RTW_INFO("0x%x\n", *(ehdr_pos + ETH_HLEN + 4 + i));
			/* RTW_INFO("0x%x\n", *(ehdr_pos + ETH_HLEN + 6 + i)); */

			RTW_INFO("eth, RC-end\n");
		}
	}

}

void rtw_start_auto_ap(_adapter *adapter)
{
	RTW_INFO("%s\n", __FUNCTION__);

	rtw_set_802_11_infrastructure_mode(adapter, Ndis802_11APMode, 0);

	rtw_setopmode_cmd(adapter, Ndis802_11APMode, RTW_CMDF_WAIT_ACK);
}

static int rtw_auto_ap_start_beacon(_adapter *adapter)
{
	int ret = 0;
	u8 *pbuf = NULL;
	uint len;
	u8	supportRate[16];
	int	sz = 0, rateLen;
	u8	*ie;
	u8	wireless_mode, oper_channel;
	u8 ssid[3] = {0}; /* hidden ssid */
	u32 ssid_len = sizeof(ssid);
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != _TRUE)
		return -EINVAL;


	len = 128;
	pbuf = rtw_zmalloc(len);
	if (!pbuf)
		return -ENOMEM;


	/* generate beacon */
	ie = pbuf;

	/* timestamp will be inserted by hardware */
	sz += 8;
	ie += sz;

	/* beacon interval : 2bytes */
	*(u16 *)ie = cpu_to_le16((u16)100); /* BCN_INTERVAL=100; */
	sz += 2;
	ie += 2;

	/* capability info */
	*(u16 *)ie = 0;
	*(u16 *)ie |= cpu_to_le16(cap_ESS);
	*(u16 *)ie |= cpu_to_le16(cap_ShortPremble);
	/* *(u16*)ie |= cpu_to_le16(cap_Privacy); */
	sz += 2;
	ie += 2;

	/* SSID */
	ie = rtw_set_ie(ie, _SSID_IE_, ssid_len, ssid, &sz);

	/* supported rates */
	wireless_mode = (WIRELESS_11BG_24N & padapter->registrypriv.wireless_mode);
	rtw_set_supported_rate(supportRate, wireless_mode);
	rateLen = rtw_get_rateset_len(supportRate);
	if (rateLen > 8)
		ie = rtw_set_ie(ie, _SUPPORTEDRATES_IE_, 8, supportRate, &sz);
	else
		ie = rtw_set_ie(ie, _SUPPORTEDRATES_IE_, rateLen, supportRate, &sz);


	/* DS parameter set */
	if (rtw_mi_check_status(adapter, MI_LINKED))
		oper_channel = rtw_mi_get_union_chan(adapter);
	else
		oper_channel = adapter_to_dvobj(adapter)->oper_channel;

	ie = rtw_set_ie(ie, _DSSET_IE_, 1, &oper_channel, &sz);

	/* ext supported rates */
	if (rateLen > 8)
		ie = rtw_set_ie(ie, _EXT_SUPPORTEDRATES_IE_, (rateLen - 8), (supportRate + 8), &sz);

	RTW_INFO("%s, start auto ap beacon sz=%d\n", __FUNCTION__, sz);

	/* lunch ap mode & start to issue beacon */
	if (rtw_check_beacon_data(adapter, pbuf,  sz) == _SUCCESS) {

	} else
		ret = -EINVAL;


	rtw_mfree(pbuf, len);

	return ret;

}
#endif/* CONFIG_AUTO_AP_MODE */

#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
u8 tx_control_hdl(_adapter *adapter)
{
	u8 val;

	if(ATOMIC_READ(&adapter->tbtx_tx_pause))
		val = 0xff;
	else
		val = 0x00;

	rtw_hal_set_hwreg(adapter, HW_VAR_TXPAUSE, &val);

	return H2C_SUCCESS;
}
#endif

#ifdef CONFIG_AP_MODE
u8 stop_ap_hdl(_adapter *adapter)
{
	RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(adapter));

	rtw_set_802_11_infrastructure_mode(adapter, Ndis802_11Infrastructure, RTW_CMDF_DIRECTLY);
	rtw_setopmode_cmd(adapter, Ndis802_11Infrastructure, RTW_CMDF_DIRECTLY);

	return H2C_SUCCESS;
}
#endif

u8 setopmode_hdl(_adapter *padapter, u8 *pbuf)
{
	u8	type;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct setopmode_parm *psetop = (struct setopmode_parm *)pbuf;

	if (psetop->mode == Ndis802_11APMode
		|| psetop->mode == Ndis802_11_mesh
	) {
		pmlmeinfo->state = WIFI_FW_AP_STATE;
		type = _HW_STATE_AP_;
	} else if (psetop->mode == Ndis802_11Infrastructure) {
		pmlmeinfo->state &= ~(BIT(0) | BIT(1)); /* clear state */
		pmlmeinfo->state |= WIFI_FW_STATION_STATE;/* set to 	STATION_STATE */
		type = _HW_STATE_STATION_;
	} else if (psetop->mode == Ndis802_11IBSS)
		type = _HW_STATE_ADHOC_;
	else if (psetop->mode == Ndis802_11Monitor)
		type = _HW_STATE_MONITOR_;
	else
		type = _HW_STATE_NOLINK_;

#ifdef CONFIG_AP_PORT_SWAP
	rtw_hal_set_hwreg(padapter, HW_VAR_PORT_SWITCH, (u8 *)(&type));
#endif

	rtw_hal_set_hwreg(padapter, HW_VAR_SET_OPMODE, (u8 *)(&type));

#ifdef CONFIG_AUTO_AP_MODE
	if (psetop->mode == Ndis802_11APMode)
		rtw_auto_ap_start_beacon(padapter);
#endif

	if (rtw_port_switch_chk(padapter) == _TRUE) {
		rtw_hal_set_hwreg(padapter, HW_VAR_PORT_SWITCH, NULL);

		if (psetop->mode == Ndis802_11APMode)
			adapter_to_pwrctl(padapter)->fw_psmode_iface_id = 0xff; /* ap mode won't dowload rsvd pages */
		else if (psetop->mode == Ndis802_11Infrastructure) {
#ifdef CONFIG_LPS
			_adapter *port0_iface = dvobj_get_port0_adapter(adapter_to_dvobj(padapter));
			if (port0_iface)
				rtw_lps_ctrl_wk_cmd(port0_iface, LPS_CTRL_CONNECT, RTW_CMDF_DIRECTLY);
#endif
		}
	}

#ifdef CONFIG_BT_COEXIST
	if (psetop->mode == Ndis802_11APMode
		|| psetop->mode == Ndis802_11_mesh
		|| psetop->mode == Ndis802_11Monitor
	) {
		/* Do this after port switch to */
		/* prevent from downloading rsvd page to wrong port */
		rtw_btcoex_MediaStatusNotify(padapter, 1); /* connect */
	}
#endif /* CONFIG_BT_COEXIST */

	return H2C_SUCCESS;

}

u8 createbss_hdl(_adapter *padapter, u8 *pbuf)
{
#ifdef CONFIG_AP_MODE
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX	*pnetwork = (WLAN_BSSID_EX *)(&(pmlmeinfo->network));
	WLAN_BSSID_EX	*pdev_network = &padapter->registrypriv.dev_network;
	struct createbss_parm *parm = (struct createbss_parm *)pbuf;
	u8 ret = H2C_SUCCESS;
	/* u8	initialgain; */

	if ((parm->req_ch == 0 && pmlmeinfo->state == WIFI_FW_AP_STATE)
		|| parm->req_ch != 0
	) {
		start_bss_network(padapter, parm);
		goto exit;
	}

	/* below is for ad-hoc master */
	if (parm->adhoc) {
		rtw_warn_on(pdev_network->InfrastructureMode != Ndis802_11IBSS);
		rtw_joinbss_reset(padapter);

		pmlmeext->cur_bwmode = CHANNEL_WIDTH_20;
		pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		pmlmeinfo->ERP_enable = 0;
		pmlmeinfo->WMM_enable = 0;
		pmlmeinfo->HT_enable = 0;
		pmlmeinfo->HT_caps_enable = 0;
		pmlmeinfo->HT_info_enable = 0;
		pmlmeinfo->agg_enable_bitmap = 0;
		pmlmeinfo->candidate_tid_bitmap = 0;

		/* cancel link timer */
		_cancel_timer_ex(&pmlmeext->link_timer);

		/* clear CAM */
		flush_all_cam_entry(padapter);

		pdev_network->Length = get_WLAN_BSSID_EX_sz(pdev_network);
		_rtw_memcpy(pnetwork, pdev_network, FIELD_OFFSET(WLAN_BSSID_EX, IELength));
		pnetwork->IELength = pdev_network->IELength;

		if (pnetwork->IELength > MAX_IE_SZ) {
			ret = H2C_PARAMETERS_ERROR;
			goto ibss_post_hdl;
		}

		_rtw_memcpy(pnetwork->IEs, pdev_network->IEs, pnetwork->IELength);
		start_create_ibss(padapter);
	} else {
		rtw_warn_on(1);
		ret = H2C_PARAMETERS_ERROR;
	}

ibss_post_hdl:
	rtw_create_ibss_post_hdl(padapter, ret);

exit:
	return ret;
#else
	return H2C_SUCCESS;
#endif /* CONFIG_AP_MODE */
}

u8 join_cmd_hdl(_adapter *padapter, u8 *pbuf)
{
	u8	join_type;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*pnetwork = (WLAN_BSSID_EX *)(&(pmlmeinfo->network));
#ifdef CONFIG_ANTENNA_DIVERSITY
	struct joinbss_parm	*pparm = (struct joinbss_parm *)pbuf;
#endif /* CONFIG_ANTENNA_DIVERSITY */
	u32 i;
	/* u8	initialgain; */
	/* u32	acparm; */
	u8 u_ch, u_bw, u_offset;
	u8 doiqk = _FALSE;

	/* check already connecting to AP or not */
	if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) {
		if (pmlmeinfo->state & WIFI_FW_STATION_STATE)
			issue_deauth_ex(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING, 1, 100);
		pmlmeinfo->state = WIFI_FW_NULL_STATE;

		/* clear CAM */
		flush_all_cam_entry(padapter);

		_cancel_timer_ex(&pmlmeext->link_timer);

		/* set MSR to nolink->infra. mode		 */
		/* Set_MSR(padapter, _HW_STATE_NOLINK_); */
		Set_MSR(padapter, _HW_STATE_STATION_);


		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_DISCONNECT, 0);
		if (pmlmeinfo->state & WIFI_FW_STATION_STATE)
			rtw_hal_rcr_set_chk_bssid(padapter, MLME_STA_DISCONNECTED);
	}

#ifdef CONFIG_ANTENNA_DIVERSITY
	rtw_antenna_select_cmd(padapter, pparm->network.PhyInfo.Optimum_antenna, _FALSE);
#endif

#ifdef CONFIG_WAPI_SUPPORT
	rtw_wapi_clear_all_cam_entry(padapter);
#endif

	rtw_joinbss_reset(padapter);

	pmlmeinfo->ERP_enable = 0;
	pmlmeinfo->WMM_enable = 0;
	pmlmeinfo->HT_enable = 0;
	pmlmeinfo->HT_caps_enable = 0;
	pmlmeinfo->HT_info_enable = 0;
	pmlmeinfo->agg_enable_bitmap = 0;
	pmlmeinfo->candidate_tid_bitmap = 0;
	pmlmeinfo->bwmode_updated = _FALSE;
	/* pmlmeinfo->assoc_AP_vendor = HT_IOT_PEER_MAX; */
	pmlmeinfo->VHT_enable = 0;
#ifdef ROKU_PRIVATE
	pmlmeinfo->ht_vht_received = 0;
	_rtw_memset(pmlmeinfo->SupportedRates_infra_ap, 0, NDIS_802_11_LENGTH_RATES_EX);
#endif /* ROKU_PRIVATE */
	_rtw_memcpy(pnetwork, pbuf, FIELD_OFFSET(WLAN_BSSID_EX, IELength));
	pnetwork->IELength = ((WLAN_BSSID_EX *)pbuf)->IELength;

	if (pnetwork->IELength > MAX_IE_SZ) /* Check pbuf->IELength */
		return H2C_PARAMETERS_ERROR;

	if (pnetwork->IELength < 2) {
		report_join_res(padapter, (-4), WLAN_STATUS_UNSPECIFIED_FAILURE);
		return H2C_SUCCESS;
	}
	_rtw_memcpy(pnetwork->IEs, ((WLAN_BSSID_EX *)pbuf)->IEs, pnetwork->IELength);

	pmlmeinfo->bcn_interval = get_beacon_interval(pnetwork);

	/* Check AP vendor to move rtw_joinbss_cmd() */
	/* pmlmeinfo->assoc_AP_vendor = check_assoc_AP(pnetwork->IEs, pnetwork->IELength); */

	/* sizeof(NDIS_802_11_FIXED_IEs)	 */
	for (i = _FIXED_IE_LENGTH_ ; i < pnetwork->IELength - 2 ;) {
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pnetwork->IEs + i);

		switch (pIE->ElementID) {
		case _VENDOR_SPECIFIC_IE_: /* Get WMM IE. */
			if (_rtw_memcmp(pIE->data, WMM_OUI, 4))
				WMM_param_handler(padapter, pIE);
			break;

#ifdef CONFIG_80211N_HT
		case _HT_CAPABILITY_IE_:	/* Get HT Cap IE. */
			pmlmeinfo->HT_caps_enable = 1;
			break;

		case _HT_EXTRA_INFO_IE_:	/* Get HT Info IE. */
			pmlmeinfo->HT_info_enable = 1;
			break;
#endif /* CONFIG_80211N_HT */

#ifdef CONFIG_80211AC_VHT
		case EID_VHTCapability: /* Get VHT Cap IE. */
			pmlmeinfo->VHT_enable = 1;
			break;

		case EID_VHTOperation: /* Get VHT Operation IE. */
			break;
#endif /* CONFIG_80211AC_VHT */
		default:
			break;
		}

		i += (pIE->Length + 2);
	}

	rtw_bss_get_chbw(pnetwork
		, &pmlmeext->cur_channel, &pmlmeext->cur_bwmode, &pmlmeext->cur_ch_offset, 1, 1);

	rtw_adjust_chbw(padapter, pmlmeext->cur_channel, &pmlmeext->cur_bwmode, &pmlmeext->cur_ch_offset);

#if 0
	if (padapter->registrypriv.wifi_spec) {
		/* for WiFi test, follow WMM test plan spec */
		acparm = 0x002F431C; /* VO */
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acparm));
		acparm = 0x005E541C; /* VI */
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acparm));
		acparm = 0x0000A525; /* BE */
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acparm));
		acparm = 0x0000A549; /* BK */
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acparm));

		/* for WiFi test, mixed mode with intel STA under bg mode throughput issue */
		if (padapter->mlmepriv.htpriv.ht_option == _FALSE) {
			acparm = 0x00004320;
			rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acparm));
		}
	} else {
		acparm = 0x002F3217; /* VO */
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acparm));
		acparm = 0x005E4317; /* VI */
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acparm));
		acparm = 0x00105320; /* BE */
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acparm));
		acparm = 0x0000A444; /* BK */
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acparm));
	}
#endif

	/* check channel, bandwidth, offset and switch */
	if (rtw_chk_start_clnt_join(padapter, &u_ch, &u_bw, &u_offset) == _FAIL) {
		report_join_res(padapter, (-4), WLAN_STATUS_UNSPECIFIED_FAILURE);
		return H2C_SUCCESS;
	}

	/* disable dynamic functions, such as high power, DIG */
	/*rtw_phydm_func_disable_all(padapter);*/

	/* config the initial gain under linking, need to write the BB registers */
	/* initialgain = 0x1E; */
	/*rtw_hal_set_odm_var(padapter, HAL_ODM_INITIAL_GAIN, &initialgain, _FALSE);*/

	rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, pmlmeinfo->network.MacAddress);
	if (MLME_IS_STA(padapter))
		rtw_hal_rcr_set_chk_bssid(padapter, MLME_STA_CONNECTING);
	else
		rtw_hal_rcr_set_chk_bssid(padapter, MLME_ADHOC_STARTED);

	join_type = 0;
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

	rtw_btcoex_connect_notify(padapter, join_type);

	doiqk = _TRUE;
	rtw_hal_set_hwreg(padapter , HW_VAR_DO_IQK , &doiqk);

	set_channel_bwmode(padapter, u_ch, u_offset, u_bw);

	doiqk = _FALSE;
	rtw_hal_set_hwreg(padapter , HW_VAR_DO_IQK , &doiqk);

	/* cancel link timer */
	_cancel_timer_ex(&pmlmeext->link_timer);

	start_clnt_join(padapter);

	return H2C_SUCCESS;

}

u8 disconnect_hdl(_adapter *padapter, unsigned char *pbuf)
{
#if CONFIG_DFS
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
#endif
	struct disconnect_parm *param = (struct disconnect_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*pnetwork = (WLAN_BSSID_EX *)(&(pmlmeinfo->network));
	u8 val8;

	if (is_client_associated_to_ap(padapter)
		#if CONFIG_DFS
		&& !IS_RADAR_DETECTED(rfctl) && !rfctl->csa_ch
		#endif
	) {
		#ifdef CONFIG_PLATFORM_ROCKCHIPS
		/* To avoid connecting to AP fail during resume process, change retry count from 5 to 1 */
		issue_deauth_ex(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING, 1, 100);
		#else
		issue_deauth_ex(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING, param->deauth_timeout_ms / 100, 100);
		#endif /* CONFIG_PLATFORM_ROCKCHIPS */
	}

#ifndef CONFIG_SUPPORT_MULTI_BCN
	if (((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE)) {
		/* Stop BCN */
		val8 = 0;
		rtw_hal_set_hwreg(padapter, HW_VAR_BCN_FUNC, (u8 *)(&val8));
	}
#endif

	rtw_sta_mstatus_report(padapter);
	
	rtw_mlmeext_disconnect(padapter);

	rtw_free_uc_swdec_pending_queue(padapter);

	return	H2C_SUCCESS;
}

static const char *const _scan_state_str[] = {
	"SCAN_DISABLE",
	"SCAN_START",
	"SCAN_PS_ANNC_WAIT",
	"SCAN_ENTER",
	"SCAN_PROCESS",
	"SCAN_BACKING_OP",
	"SCAN_BACK_OP",
	"SCAN_LEAVING_OP",
	"SCAN_LEAVE_OP",
	"SCAN_SW_ANTDIV_BL",
	"SCAN_TO_P2P_LISTEN",
	"SCAN_P2P_LISTEN",
	"SCAN_COMPLETE",
	"SCAN_STATE_MAX",
};

const char *scan_state_str(u8 state)
{
	state = (state >= SCAN_STATE_MAX) ? SCAN_STATE_MAX : state;
	return _scan_state_str[state];
}

static bool scan_abort_hdl(_adapter *adapter)
{
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct ss_res *ss = &pmlmeext->sitesurvey_res;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &adapter->wdinfo;
#endif
	bool ret = _FALSE;

	if (pmlmeext->scan_abort == _TRUE) {
#ifdef CONFIG_P2P
		if (!rtw_p2p_chk_state(&adapter->wdinfo, P2P_STATE_NONE)) {
			rtw_p2p_findphase_ex_set(pwdinfo, P2P_FINDPHASE_EX_MAX);
			ss->channel_idx = 3;
			RTW_INFO("%s idx:%d, cnt:%u\n", __FUNCTION__
				 , ss->channel_idx
				 , pwdinfo->find_phase_state_exchange_cnt
				);
		} else
#endif
		{
			ss->channel_idx = ss->ch_num;
			RTW_INFO("%s idx:%d\n", __FUNCTION__
				 , ss->channel_idx
				);
		}
		ret = _TRUE;
	}

	return ret;
}

u8 rtw_scan_sparse(_adapter *adapter, struct rtw_ieee80211_channel *ch, u8 ch_num)
{
	/* interval larger than this is treated as backgroud scan */
#ifndef RTW_SCAN_SPARSE_BG_INTERVAL_MS
#define RTW_SCAN_SPARSE_BG_INTERVAL_MS 12000
#endif

#ifndef RTW_SCAN_SPARSE_CH_NUM_MIRACAST
#define RTW_SCAN_SPARSE_CH_NUM_MIRACAST 1
#endif
#ifndef RTW_SCAN_SPARSE_CH_NUM_BG
#define RTW_SCAN_SPARSE_CH_NUM_BG 4
#endif
#ifdef CONFIG_LAYER2_ROAMING
#ifndef RTW_SCAN_SPARSE_CH_NUM_ROAMING_ACTIVE
#define RTW_SCAN_SPARSE_CH_NUM_ROAMING_ACTIVE 1
#endif
#endif

#define SCAN_SPARSE_CH_NUM_INVALID 255

	static u8 token = 255;
	u32 interval;
	bool busy_traffic = _FALSE;
	bool miracast_enabled = _FALSE;
	bool bg_scan = _FALSE;
	u8 max_allow_ch = SCAN_SPARSE_CH_NUM_INVALID;
	u8 scan_division_num;
	u8 ret_num = ch_num;
	struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;

	if (mlmeext->last_scan_time == 0)
		mlmeext->last_scan_time = rtw_get_current_time();

	interval = rtw_get_passing_time_ms(mlmeext->last_scan_time);


	if (rtw_mi_busy_traffic_check(adapter))
		busy_traffic = _TRUE;

	if (rtw_mi_check_miracast_enabled(adapter))
		miracast_enabled = _TRUE;

	if (interval > RTW_SCAN_SPARSE_BG_INTERVAL_MS)
		bg_scan = _TRUE;

	/* max_allow_ch by conditions*/

#if RTW_SCAN_SPARSE_MIRACAST
	if (miracast_enabled == _TRUE && busy_traffic == _TRUE)
		max_allow_ch = rtw_min(max_allow_ch, RTW_SCAN_SPARSE_CH_NUM_MIRACAST);
#endif

#if RTW_SCAN_SPARSE_BG
	if (bg_scan == _TRUE)
		max_allow_ch = rtw_min(max_allow_ch, RTW_SCAN_SPARSE_CH_NUM_BG);
#endif

#if  defined(CONFIG_LAYER2_ROAMING) && defined(RTW_SCAN_SPARSE_ROAMING_ACTIVE)
	if (rtw_chk_roam_flags(adapter, RTW_ROAM_ACTIVE)) {
		if (busy_traffic == _TRUE && adapter->mlmepriv.need_to_roam == _TRUE)
			max_allow_ch = rtw_min(max_allow_ch, RTW_SCAN_SPARSE_CH_NUM_ROAMING_ACTIVE);
	}
#endif


	if (max_allow_ch != SCAN_SPARSE_CH_NUM_INVALID) {
		int i;
		int k = 0;

		scan_division_num = (ch_num / max_allow_ch) + ((ch_num % max_allow_ch) ? 1 : 0);
		token = (token + 1) % scan_division_num;

		if (0)
			RTW_INFO("scan_division_num:%u, token:%u\n", scan_division_num, token);

		for (i = 0; i < ch_num; i++) {
			if (ch[i].hw_value && (i % scan_division_num) == token
			   ) {
				if (i != k)
					_rtw_memcpy(&ch[k], &ch[i], sizeof(struct rtw_ieee80211_channel));
				k++;
			}
		}

		_rtw_memset(&ch[k], 0, sizeof(struct rtw_ieee80211_channel));

		ret_num = k;
		mlmeext->last_scan_time = rtw_get_current_time();
	}

	return ret_num;
}

#ifdef CONFIG_SCAN_BACKOP
u8 rtw_scan_backop_decision(_adapter *adapter)
{
	struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;
	struct mi_state mstate;
	u8 backop_flags = 0;

	rtw_mi_status(adapter, &mstate);

	if ((MSTATE_STA_LD_NUM(&mstate) && mlmeext_chk_scan_backop_flags_sta(mlmeext, SS_BACKOP_EN))
		|| (MSTATE_STA_NUM(&mstate) && mlmeext_chk_scan_backop_flags_sta(mlmeext, SS_BACKOP_EN_NL)))
		backop_flags |= mlmeext_scan_backop_flags_sta(mlmeext);

#ifdef CONFIG_AP_MODE
	if ((MSTATE_AP_LD_NUM(&mstate) && mlmeext_chk_scan_backop_flags_ap(mlmeext, SS_BACKOP_EN))
		|| (MSTATE_AP_NUM(&mstate) && mlmeext_chk_scan_backop_flags_ap(mlmeext, SS_BACKOP_EN_NL)))
		backop_flags |= mlmeext_scan_backop_flags_ap(mlmeext);
#endif

#ifdef CONFIG_RTW_MESH
	if ((MSTATE_MESH_LD_NUM(&mstate) && mlmeext_chk_scan_backop_flags_mesh(mlmeext, SS_BACKOP_EN))
		|| (MSTATE_MESH_NUM(&mstate) && mlmeext_chk_scan_backop_flags_mesh(mlmeext, SS_BACKOP_EN_NL)))
		backop_flags |= mlmeext_scan_backop_flags_mesh(mlmeext);
#endif

	return backop_flags;
}
#endif

#define SCANNING_TIMEOUT_EX	2000
u32 rtw_scan_timeout_decision(_adapter *padapter)
{
	u32 back_op_times= 0;
	u8 max_chan_num;
	u16 scan_ms;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct ss_res *ss = &pmlmeext->sitesurvey_res;

	if (is_supported_5g(padapter->registrypriv.wireless_mode)
		&& IsSupported24G(padapter->registrypriv.wireless_mode)) 
		max_chan_num = MAX_CHANNEL_NUM;/* dual band */
	else
		max_chan_num = MAX_CHANNEL_NUM_2G;/*single band*/

	#ifdef CONFIG_SCAN_BACKOP
	if (rtw_scan_backop_decision(padapter))
		back_op_times = (max_chan_num / ss->scan_cnt_max) * ss->backop_ms;
	#endif

	if (ss->duration)
		scan_ms = ss->duration;
	else
	#if defined(CONFIG_RTW_ACS) && defined(CONFIG_RTW_ACS_DBG)
	if (IS_ACS_ENABLE(padapter) && rtw_is_acs_st_valid(padapter))
		scan_ms = rtw_acs_get_adv_st(padapter);
	else
	#endif /*CONFIG_RTW_ACS*/
		scan_ms = ss->scan_ch_ms;

	ss->scan_timeout_ms = (scan_ms * max_chan_num) + back_op_times + SCANNING_TIMEOUT_EX;
	#ifdef DBG_SITESURVEY
	RTW_INFO("%s , scan_timeout_ms = %d (ms)\n", __func__, ss->scan_timeout_ms);
	#endif /*DBG_SITESURVEY*/
	return ss->scan_timeout_ms;
}

static int rtw_scan_ch_decision(_adapter *padapter, struct rtw_ieee80211_channel *out,
		u32 out_num, struct rtw_ieee80211_channel *in, u32 in_num, bool no_sparse)
{
	int i, j;
	int set_idx;
	u8 chan;
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	struct registry_priv *regsty = dvobj_to_regsty(adapter_to_dvobj(padapter));

	/* clear first */
	_rtw_memset(out, 0, sizeof(struct rtw_ieee80211_channel) * out_num);

	/* acquire channels from in */
	j = 0;
	for (i = 0; i < in_num; i++) {

		if (0)
			RTW_INFO(FUNC_ADPT_FMT" "CHAN_FMT"\n", FUNC_ADPT_ARG(padapter), CHAN_ARG(&in[i]));

		if (!in[i].hw_value || (in[i].flags & RTW_IEEE80211_CHAN_DISABLED))
			continue;
		if (rtw_mlme_band_check(padapter, in[i].hw_value) == _FALSE)
			continue;

		set_idx = rtw_chset_search_ch(rfctl->channel_set, in[i].hw_value);
		if (set_idx >= 0) {
			if (j >= out_num) {
				RTW_PRINT(FUNC_ADPT_FMT" out_num:%u not enough\n",
					  FUNC_ADPT_ARG(padapter), out_num);
				break;
			}

			_rtw_memcpy(&out[j], &in[i], sizeof(struct rtw_ieee80211_channel));

			if (rfctl->channel_set[set_idx].flags & (RTW_CHF_NO_IR | RTW_CHF_DFS))
				out[j].flags |= RTW_IEEE80211_CHAN_PASSIVE_SCAN;

			j++;
		}
		if (j >= out_num)
			break;
	}

	/* if out is empty, use channel_set as default */
	if (j == 0) {
		for (i = 0; i < rfctl->max_chan_nums; i++) {
			chan = rfctl->channel_set[i].ChannelNum;
			if (rtw_mlme_band_check(padapter, chan) == _TRUE) {
				if (rtw_mlme_ignore_chan(padapter, chan) == _TRUE)
					continue;

				if (0)
					RTW_INFO(FUNC_ADPT_FMT" ch:%u\n", FUNC_ADPT_ARG(padapter), chan);

				if (j >= out_num) {
					RTW_PRINT(FUNC_ADPT_FMT" out_num:%u not enough\n",
						FUNC_ADPT_ARG(padapter), out_num);
					break;
				}

				out[j].hw_value = chan;

				if (rfctl->channel_set[i].flags & (RTW_CHF_NO_IR | RTW_CHF_DFS))
					out[j].flags |= RTW_IEEE80211_CHAN_PASSIVE_SCAN;

				j++;
			}
		}
	}

	if (!no_sparse
		&& !regsty->wifi_spec
		&& j > 6 /* assume ch_num > 6 is normal scan */
	) {
		/* scan_sparse */
		j = rtw_scan_sparse(padapter, out, j);
	}

	return j;
}

static void sitesurvey_res_reset(_adapter *adapter, struct sitesurvey_parm *parm)
{
	struct ss_res *ss = &adapter->mlmeextpriv.sitesurvey_res;
	RT_CHANNEL_INFO *chset = adapter_to_chset(adapter);
	int i;

	ss->bss_cnt = 0;
	ss->activate_ch_cnt = 0;
	ss->channel_idx = 0;
	ss->force_ssid_scan = 0;
	ss->igi_scan = 0;
	ss->igi_before_scan = 0;
#ifdef CONFIG_SCAN_BACKOP
	ss->scan_cnt = 0;
#endif
#if defined(CONFIG_ANTENNA_DIVERSITY) || defined(DBG_SCAN_SW_ANTDIV_BL)
	ss->is_sw_antdiv_bl_scan = 0;
#endif
	ss->ssid_num = 0;
	for (i = 0; i < RTW_SSID_SCAN_AMOUNT; i++) {
		if (parm->ssid[i].SsidLength) {
			_rtw_memcpy(ss->ssid[i].Ssid, parm->ssid[i].Ssid, IW_ESSID_MAX_SIZE);
			ss->ssid[i].SsidLength = parm->ssid[i].SsidLength;
			ss->ssid_num++;
		} else
			ss->ssid[i].SsidLength = 0;
	}

	ss->ch_num = rtw_scan_ch_decision(adapter
					, ss->ch, RTW_CHANNEL_SCAN_AMOUNT
					, parm->ch, parm->ch_num
					, parm->acs
				);

	for (i = 0; i < MAX_CHANNEL_NUM; i++)
		chset[i].hidden_bss_cnt = 0;

	ss->bw = parm->bw;
	ss->igi = parm->igi;
	ss->token = parm->token;
	ss->duration = parm->duration;
	ss->scan_mode = parm->scan_mode;
	ss->token = parm->token;
	ss->acs = parm->acs;
}

static u8 sitesurvey_pick_ch_behavior(_adapter *padapter, u8 *ch, RT_SCAN_TYPE *type)
{
	u8 next_state;
	u8 scan_ch = 0;
	RT_SCAN_TYPE scan_type = SCAN_PASSIVE;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct ss_res *ss = &pmlmeext->sitesurvey_res;
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	int ch_set_idx;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif
#ifdef CONFIG_SCAN_BACKOP
	u8 backop_flags = 0;
#endif

	/* handle scan abort request */
	scan_abort_hdl(padapter);

#ifdef CONFIG_P2P
	if (pwdinfo->rx_invitereq_info.scan_op_ch_only || pwdinfo->p2p_info.scan_op_ch_only) {
		if (pwdinfo->rx_invitereq_info.scan_op_ch_only)
			scan_ch = pwdinfo->rx_invitereq_info.operation_ch[ss->channel_idx];
		else
			scan_ch = pwdinfo->p2p_info.operation_ch[ss->channel_idx];
		scan_type = SCAN_ACTIVE;
	} else if (rtw_p2p_findphase_ex_is_social(pwdinfo)) {
		/*
		* Commented by Albert 2011/06/03
		* The driver is in the find phase, it should go through the social channel.
		*/
		scan_ch = pwdinfo->social_chan[ss->channel_idx];
		ch_set_idx = rtw_chset_search_ch(rfctl->channel_set, scan_ch);
		if (ch_set_idx >= 0)
			scan_type = rfctl->channel_set[ch_set_idx].flags & RTW_CHF_NO_IR ? SCAN_PASSIVE : SCAN_ACTIVE;
		else
			scan_type = SCAN_ACTIVE;
	} else
#endif /* CONFIG_P2P */
	{
		struct rtw_ieee80211_channel *ch;

		#ifdef CONFIG_SCAN_BACKOP
		backop_flags = rtw_scan_backop_decision(padapter);
		#endif

		#ifdef CONFIG_SCAN_BACKOP
		if (!(backop_flags && ss->scan_cnt >= ss->scan_cnt_max))
		#endif
		{
			#ifdef CONFIG_RTW_WIFI_HAL
			if (adapter_to_dvobj(padapter)->nodfs) {
				while (ss->channel_idx < ss->ch_num && rtw_chset_is_dfs_ch(rfctl->channel_set, ss->ch[ss->channel_idx].hw_value))
					ss->channel_idx++;
			} else
			#endif
			if (ss->channel_idx != 0 && ss->force_ssid_scan == 0
				&& pmlmeext->sitesurvey_res.ssid_num
				&& (ss->ch[ss->channel_idx - 1].flags & RTW_IEEE80211_CHAN_PASSIVE_SCAN)
			) {
				ch_set_idx = rtw_chset_search_ch(rfctl->channel_set, ss->ch[ss->channel_idx - 1].hw_value);
				if (ch_set_idx != -1 && rfctl->channel_set[ch_set_idx].hidden_bss_cnt
					&& (!IS_DFS_SLAVE_WITH_RD(rfctl)
						|| rtw_rfctl_dfs_domain_unknown(rfctl)
						|| !CH_IS_NON_OCP(&rfctl->channel_set[ch_set_idx]))
				) {
					ss->channel_idx--;
					ss->force_ssid_scan = 1;
				}
			} else
				ss->force_ssid_scan = 0;
		}

		if (ss->channel_idx < ss->ch_num) {
			ch = &ss->ch[ss->channel_idx];
			scan_ch = ch->hw_value;

			#if defined(CONFIG_RTW_ACS) && defined(CONFIG_RTW_ACS_DBG)
			if (IS_ACS_ENABLE(padapter) && rtw_is_acs_passiv_scan(padapter))
				scan_type = SCAN_PASSIVE;
			else
			#endif /*CONFIG_RTW_ACS*/
				scan_type = (ch->flags & RTW_IEEE80211_CHAN_PASSIVE_SCAN) ? SCAN_PASSIVE : SCAN_ACTIVE;
		}
	}

	if (scan_ch != 0) {
		next_state = SCAN_PROCESS;

		#ifdef CONFIG_SCAN_BACKOP
		if (backop_flags) {
			if (ss->scan_cnt < ss->scan_cnt_max)
				ss->scan_cnt++;
			else {
				mlmeext_assign_scan_backop_flags(pmlmeext, backop_flags);
				next_state = SCAN_BACKING_OP;
			}
		}
		#endif

	} else if (rtw_p2p_findphase_ex_is_needed(pwdinfo)) {
		/* go p2p listen */
		next_state = SCAN_TO_P2P_LISTEN;

#ifdef CONFIG_ANTENNA_DIVERSITY
	} else if (rtw_hal_antdiv_before_linked(padapter)) {
		/* go sw antdiv before link */
		next_state = SCAN_SW_ANTDIV_BL;
#endif
	} else {
		next_state = SCAN_COMPLETE;

#if defined(DBG_SCAN_SW_ANTDIV_BL)
		{
			/* for SCAN_SW_ANTDIV_BL state testing */
			struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
			int i;
			bool is_linked = _FALSE;

			for (i = 0; i < dvobj->iface_nums; i++) {
				if (rtw_linked_check(dvobj->padapters[i]))
					is_linked = _TRUE;
			}

			if (!is_linked) {
				static bool fake_sw_antdiv_bl_state = 0;

				if (fake_sw_antdiv_bl_state == 0) {
					next_state = SCAN_SW_ANTDIV_BL;
					fake_sw_antdiv_bl_state = 1;
				} else
					fake_sw_antdiv_bl_state = 0;
			}
		}
#endif /* defined(DBG_SCAN_SW_ANTDIV_BL) */
	}

#ifdef CONFIG_SCAN_BACKOP
	if (next_state != SCAN_PROCESS)
		ss->scan_cnt = 0;
#endif


#ifdef DBG_FIXED_CHAN
	if (pmlmeext->fixed_chan != 0xff && next_state == SCAN_PROCESS)
		scan_ch = pmlmeext->fixed_chan;
#endif

	if (ch)
		*ch = scan_ch;
	if (type)
		*type = scan_type;

	return next_state;
}

void site_survey(_adapter *padapter, u8 survey_channel, RT_SCAN_TYPE ScanType)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct ss_res *ss = &pmlmeext->sitesurvey_res;
	u8 ssid_scan = 0;

#ifdef CONFIG_P2P
#ifndef CONFIG_IOCTL_CFG80211
	struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
#endif
#endif

	if (survey_channel != 0) {
		set_channel_bwmode(padapter, survey_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);

		if (ScanType == SCAN_PASSIVE && ss->force_ssid_scan)
			ssid_scan = 1;
		else if (ScanType == SCAN_ACTIVE) {
#ifdef CONFIG_P2P
			#ifdef CONFIG_IOCTL_CFG80211
			if (rtw_cfg80211_is_p2p_scan(padapter))
			#else
			if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_SCAN)
				|| rtw_p2p_chk_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH))
			#endif
			{
				issue_probereq_p2p(padapter, NULL);
				issue_probereq_p2p(padapter, NULL);
				issue_probereq_p2p(padapter, NULL);
			} else
#endif /* CONFIG_P2P */
			{
				if (pmlmeext->sitesurvey_res.scan_mode == SCAN_ACTIVE) {
					/* IOT issue, When wifi_spec is not set, send one probe req without WPS IE. */
					if (padapter->registrypriv.wifi_spec)
						issue_probereq(padapter, NULL, NULL);
					else
						issue_probereq_ex(padapter, NULL, NULL, 0, 0, 0, 0);
					issue_probereq(padapter, NULL, NULL);
				}

				ssid_scan = 1;
			}
		}

		if (ssid_scan) {
			int i;

			for (i = 0; i < RTW_SSID_SCAN_AMOUNT; i++) {
				if (pmlmeext->sitesurvey_res.ssid[i].SsidLength) {
					/* IOT issue, When wifi_spec is not set, send one probe req without WPS IE. */
					if (padapter->registrypriv.wifi_spec)
						issue_probereq(padapter, &(pmlmeext->sitesurvey_res.ssid[i]), NULL);
					else
						issue_probereq_ex(padapter, &(pmlmeext->sitesurvey_res.ssid[i]), NULL, 0, 0, 0, 0);
					issue_probereq(padapter, &(pmlmeext->sitesurvey_res.ssid[i]), NULL);
				}
			}
		}
	} else {
		/* channel number is 0 or this channel is not valid. */
		rtw_warn_on(1);
	}

	return;
}

void survey_done_set_ch_bw(_adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	u8 cur_channel = 0;
	u8 cur_bwmode;
	u8 cur_ch_offset;

#ifdef CONFIG_MCC_MODE
	if (!rtw_hal_mcc_change_scan_flag(padapter, &cur_channel, &cur_bwmode, &cur_ch_offset)) {
		if (0)
			RTW_INFO(FUNC_ADPT_FMT" back to AP channel - ch:%u, bw:%u, offset:%u\n",
				FUNC_ADPT_ARG(padapter), cur_channel, cur_bwmode, cur_ch_offset);
		goto exit;
	}
#endif

	if (rtw_mi_get_ch_setting_union(padapter, &cur_channel, &cur_bwmode, &cur_ch_offset) != 0) {
		if (0)
			RTW_INFO(FUNC_ADPT_FMT" back to linked/linking union - ch:%u, bw:%u, offset:%u\n",
				FUNC_ADPT_ARG(padapter), cur_channel, cur_bwmode, cur_ch_offset);
	} else {
#ifdef CONFIG_P2P
		struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
		_adapter *iface;
		int i;

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (!iface)
				continue;

#ifdef CONFIG_IOCTL_CFG80211
			if (iface->wdinfo.driver_interface == DRIVER_CFG80211 && !adapter_wdev_data(iface)->p2p_enabled)
				continue;
#endif

			if (rtw_p2p_chk_state(&iface->wdinfo, P2P_STATE_LISTEN)) {
				cur_channel = iface->wdinfo.listen_channel;
				cur_bwmode = CHANNEL_WIDTH_20;
				cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
				if (0)
					RTW_INFO(FUNC_ADPT_FMT" back to "ADPT_FMT"'s listen ch - ch:%u, bw:%u, offset:%u\n",
						FUNC_ADPT_ARG(padapter), ADPT_ARG(iface), cur_channel, cur_bwmode, cur_ch_offset);
				break;
			}
		}
#endif /* CONFIG_P2P */

		if (cur_channel == 0) {
			cur_channel = pmlmeext->cur_channel;
			cur_bwmode = pmlmeext->cur_bwmode;
			cur_ch_offset = pmlmeext->cur_ch_offset;
			if (0)
				RTW_INFO(FUNC_ADPT_FMT" back to ch:%u, bw:%u, offset:%u\n",
					FUNC_ADPT_ARG(padapter), cur_channel, cur_bwmode, cur_ch_offset);
		}
	}
#ifdef CONFIG_MCC_MODE
exit:
#endif
	set_channel_bwmode(padapter, cur_channel, cur_ch_offset, cur_bwmode);
}

/**
 * rtw_ps_annc - check and doing ps announcement for all the adapters
 * @adapter: the requesting adapter
 * @ps: power saving or not
 *
 * Returns: 0: no ps announcement is doing. 1: ps announcement is doing
 */
u8 rtw_ps_annc(_adapter *adapter, bool ps)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	_adapter *iface;
	int i;
	u8 ps_anc = 0;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (!iface)
			continue;

		if (MLME_IS_STA(iface)) {
			if (is_client_associated_to_ap(iface) == _TRUE) {
				/* TODO: TDLS peers */
				#ifdef CONFIG_MCC_MODE
				/* for two station case */
				if (MCC_EN(adapter) && rtw_hal_check_mcc_status(adapter, MCC_STATUS_NEED_MCC)) {
					u8 ch = iface->mlmeextpriv.cur_channel;
					u8 offset = iface->mlmeextpriv.cur_ch_offset;
					u8 bw = iface->mlmeextpriv.cur_bwmode;

					set_channel_bwmode(iface, ch, offset, bw);
				}
				#endif /* CONFIG_MCC_MODE */
				issue_nulldata(iface, NULL, ps, 3, 500);
				ps_anc = 1;
			}
		#ifdef CONFIG_RTW_MESH
		} else if (MLME_IS_MESH(iface)) {
			if (rtw_mesh_ps_annc(iface, ps))
				ps_anc = 1;
		#endif
		}
	}
	return ps_anc;
}

void rtw_leave_opch(_adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(adapter) && rtw_hal_check_mcc_status(adapter, MCC_STATUS_DOING_MCC))
		return;
#endif

	_enter_critical_mutex(&rfctl->offch_mutex, NULL);

	if (rfctl->offch_state == OFFCHS_NONE) {
		/* prepare to leave operating channel */
		rfctl->offch_state = OFFCHS_LEAVING_OP;

		/* clear HW TX queue */
		rtw_hal_set_hwreg(adapter, HW_VAR_CHECK_TXBUF, 0);

		rtw_hal_macid_sleep_all_used(adapter);

		rtw_ps_annc(adapter, 1);

		rfctl->offch_state = OFFCHS_LEAVE_OP;
	}

	_exit_critical_mutex(&rfctl->offch_mutex, NULL);
}

void rtw_back_opch(_adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(adapter) && rtw_hal_check_mcc_status(adapter, MCC_STATUS_DOING_MCC))
		return;
#endif

	_enter_critical_mutex(&rfctl->offch_mutex, NULL);

	if (rfctl->offch_state != OFFCHS_NONE) {
		rfctl->offch_state = OFFCHS_BACKING_OP;
		rtw_hal_macid_wakeup_all_used(adapter);
		rtw_ps_annc(adapter, 0);

		rfctl->offch_state = OFFCHS_NONE;
		rtw_mi_os_xmit_schedule(adapter);
	}

	_exit_critical_mutex(&rfctl->offch_mutex, NULL);
}

void sitesurvey_set_igi(_adapter *adapter)
{
	struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;
	struct ss_res *ss = &mlmeext->sitesurvey_res;
	u8 igi;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &adapter->wdinfo;
#endif

	switch (mlmeext_scan_state(mlmeext)) {
	case SCAN_ENTER:
		#ifdef CONFIG_P2P
		#ifdef CONFIG_IOCTL_CFG80211
		if (pwdinfo->driver_interface == DRIVER_CFG80211 && rtw_cfg80211_is_p2p_scan(adapter))
			igi = 0x30;
		else
		#endif /* CONFIG_IOCTL_CFG80211 */
		if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
			igi = 0x28;
		else
		#endif /* CONFIG_P2P */

		if (ss->igi)
			igi = ss->igi;
		else
		#if defined(CONFIG_RTW_ACS) && defined(CONFIG_RTW_ACS_DBG)
		if (IS_ACS_ENABLE(adapter) && rtw_is_acs_igi_valid(adapter))
			igi = rtw_acs_get_adv_igi(adapter);
		else
		#endif /*CONFIG_RTW_ACS*/
			igi = 0x1e;

		/* record IGI status */
		ss->igi_scan = igi;
		rtw_hal_get_odm_var(adapter, HAL_ODM_INITIAL_GAIN, &ss->igi_before_scan, NULL);

		/* disable DIG and set IGI for scan */
		rtw_hal_set_odm_var(adapter, HAL_ODM_INITIAL_GAIN, &igi, _FALSE);
		break;
	case SCAN_COMPLETE:
	case SCAN_TO_P2P_LISTEN:
		/* enable DIG and restore IGI */
		igi = 0xff;
		rtw_hal_set_odm_var(adapter, HAL_ODM_INITIAL_GAIN, &igi, _FALSE);
		break;
#ifdef CONFIG_SCAN_BACKOP
	case SCAN_BACKING_OP:
		/* write IGI for op channel when DIG is not enabled */
		odm_write_dig(adapter_to_phydm(adapter), ss->igi_before_scan);
		break;
	case SCAN_LEAVE_OP:
		/* write IGI for scan when DIG is not enabled */
		odm_write_dig(adapter_to_phydm(adapter), ss->igi_scan);
		break;
#endif /* CONFIG_SCAN_BACKOP */
	default:
		rtw_warn_on(1);
		break;
	}
}
void sitesurvey_set_msr(_adapter *adapter, bool enter)
{
	u8 network_type;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	if (enter) {
#ifdef CONFIG_MI_WITH_MBSSID_CAM
		rtw_hal_get_hwreg(adapter, HW_VAR_MEDIA_STATUS, (u8 *)(&pmlmeinfo->hw_media_state));
#endif
		/* set MSR to no link state */
		network_type = _HW_STATE_NOLINK_;
	} else {
#ifdef CONFIG_MI_WITH_MBSSID_CAM
		network_type = pmlmeinfo->hw_media_state;
#else
		network_type = pmlmeinfo->state & 0x3;
#endif
	}
	Set_MSR(adapter, network_type);
}

void sitesurvey_set_offch_state(_adapter *adapter, u8 scan_state)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);

	_enter_critical_mutex(&rfctl->offch_mutex, NULL);

	switch (scan_state) {
	case SCAN_DISABLE:
	case SCAN_BACK_OP:
		rfctl->offch_state = OFFCHS_NONE;
		break;
	case SCAN_START:
	case SCAN_LEAVING_OP:
		rfctl->offch_state = OFFCHS_LEAVING_OP;
		break;
	case SCAN_ENTER:
	case SCAN_LEAVE_OP:
		rfctl->offch_state = OFFCHS_LEAVE_OP;
		break;
	case SCAN_COMPLETE:
	case SCAN_BACKING_OP:
		rfctl->offch_state = OFFCHS_BACKING_OP;
		break;
	default:
		break;
	}

	_exit_critical_mutex(&rfctl->offch_mutex, NULL);
}

u8 sitesurvey_cmd_hdl(_adapter *padapter, u8 *pbuf)
{
	struct sitesurvey_parm	*pparm = (struct sitesurvey_parm *)pbuf;
#ifdef DBG_CHECK_FW_PS_STATE
	struct dvobj_priv *dvobj = padapter->dvobj;
	struct debug_priv *pdbgpriv = &dvobj->drv_dbg;
#endif
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct ss_res *ss = &pmlmeext->sitesurvey_res;
#ifdef CONFIG_RTW_CFGVENDOR_RANDOM_MAC_OUI
		struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
#endif
	u8 val8;

#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#ifdef CONFIG_CONCURRENT_MODE
	struct roch_info *prochinfo = &padapter->rochinfo;
#endif
#endif

#ifdef DBG_CHECK_FW_PS_STATE
	if (rtw_fw_ps_state(padapter) == _FAIL) {
		RTW_INFO("scan without leave 32k\n");
		pdbgpriv->dbg_scan_pwr_state_cnt++;
	}
#endif /* DBG_CHECK_FW_PS_STATE */

	/* increase channel idx */
	if (mlmeext_chk_scan_state(pmlmeext, SCAN_PROCESS))
		ss->channel_idx++;

	/* update scan state to next state (assigned by previous cmd hdl) */
	if (mlmeext_scan_state(pmlmeext) != mlmeext_scan_next_state(pmlmeext))
		mlmeext_set_scan_state(pmlmeext, mlmeext_scan_next_state(pmlmeext));

operation_by_state:
	switch (mlmeext_scan_state(pmlmeext)) {

	case SCAN_DISABLE:
		/*
		* SW parameter initialization
		*/

		sitesurvey_res_reset(padapter, pparm);
		mlmeext_set_scan_state(pmlmeext, SCAN_START);
		goto operation_by_state;

	case SCAN_START:
#ifdef CONFIG_RTW_CFGVENDOR_RANDOM_MAC_OUI
		if ((pwdev_priv->pno_mac_addr[0] != 0xFF)
			    && (check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE) == _TRUE)
	    	    && (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) == _FALSE)) {
			u16 seq_num;

			rtw_hal_pno_random_gen_mac_addr(padapter);
			rtw_hal_set_hw_mac_addr(padapter, pwdev_priv->pno_mac_addr);
			get_random_bytes(&seq_num, 2);
			pwdev_priv->pno_scan_seq_num = seq_num & 0xFFF;
			RTW_INFO("%s pno_scan_seq_num %d\n", __func__,
				 pwdev_priv->pno_scan_seq_num);
		}
#endif

		/*
		* prepare to leave operating channel
		*/

#ifdef CONFIG_MCC_MODE
		rtw_hal_set_mcc_setting_scan_start(padapter);
#endif /* CONFIG_MCC_MODE */

		/* apply rx ampdu setting */
		if (ss->rx_ampdu_accept != RX_AMPDU_ACCEPT_INVALID
			|| ss->rx_ampdu_size != RX_AMPDU_SIZE_INVALID)
			rtw_rx_ampdu_apply(padapter);

		/* clear HW TX queue before scan */
		rtw_hal_set_hwreg(padapter, HW_VAR_CHECK_TXBUF, 0);

		rtw_hal_macid_sleep_all_used(padapter);

		/* power save state announcement */
		if (rtw_ps_annc(padapter, 1)) {
			mlmeext_set_scan_state(pmlmeext, SCAN_PS_ANNC_WAIT);
			mlmeext_set_scan_next_state(pmlmeext, SCAN_ENTER);
			set_survey_timer(pmlmeext, 50); /* delay 50ms to protect nulldata(1) */
		} else {
			mlmeext_set_scan_state(pmlmeext, SCAN_ENTER);
			goto operation_by_state;
		}

		break;

	case SCAN_ENTER:
		/*
		* HW register and DM setting for enter scan
		*/

		rtw_phydm_ability_backup(padapter);

		sitesurvey_set_igi(padapter);

		/* config dynamic functions for off channel */
		rtw_phydm_func_for_offchannel(padapter);
		/* set MSR to no link state */
		sitesurvey_set_msr(padapter, _TRUE);

		val8 = 1; /* under site survey */
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

		mlmeext_set_scan_state(pmlmeext, SCAN_PROCESS);
		goto operation_by_state;

	case SCAN_PROCESS: {
		u8 scan_ch;
		RT_SCAN_TYPE scan_type;
		u8 next_state;
		u32 scan_ms;

#ifdef CONFIG_RTW_ACS
		if (IS_ACS_ENABLE(padapter))
			rtw_acs_get_rst(padapter);
#endif

		next_state = sitesurvey_pick_ch_behavior(padapter, &scan_ch, &scan_type);

		if (next_state != SCAN_PROCESS) {
			mlmeext_set_scan_state(pmlmeext, next_state);
			goto operation_by_state;
		}

		/* still SCAN_PROCESS state */
		#ifdef DBG_SITESURVEY
			#ifdef CONFIG_P2P
			RTW_INFO(FUNC_ADPT_FMT" %s ch:%u (cnt:%u,idx:%d) at %dms, %c%c%c%c\n"
				, FUNC_ADPT_ARG(padapter)
				, mlmeext_scan_state_str(pmlmeext)
				, scan_ch
				, pwdinfo->find_phase_state_exchange_cnt, ss->channel_idx
				, rtw_get_passing_time_ms(padapter->mlmepriv.scan_start_time)
				, scan_type ? 'A' : 'P', ss->scan_mode ? 'A' : 'P'
				, ss->ssid[0].SsidLength ? 'S' : ' '
				, ss->force_ssid_scan ? 'F' : ' '
			);
			#else
			RTW_INFO(FUNC_ADPT_FMT" %s ch:%u (idx:%d) at %dms, %c%c%c%c\n"
				, FUNC_ADPT_ARG(padapter)
				, mlmeext_scan_state_str(pmlmeext)
				, scan_ch
				, ss->channel_idx
				, rtw_get_passing_time_ms(padapter->mlmepriv.scan_start_time)
				, scan_type ? 'A' : 'P', ss->scan_mode ? 'A' : 'P'
				, ss->ssid[0].SsidLength ? 'S' : ' '
				, ss->force_ssid_scan ? 'F' : ' '
			);
			#endif /* CONFIG_P2P */
		#endif /*DBG_SITESURVEY*/
#ifdef DBG_FIXED_CHAN
		if (pmlmeext->fixed_chan != 0xff)
			RTW_INFO(FUNC_ADPT_FMT" fixed_chan:%u\n", pmlmeext->fixed_chan);
#endif

		site_survey(padapter, scan_ch, scan_type);

#if defined(CONFIG_ATMEL_RC_PATCH)
		if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
			scan_ms = 20;
		else
			scan_ms = 40;
#else
		#if defined(CONFIG_RTW_ACS) && defined(CONFIG_RTW_ACS_DBG)
		if (IS_ACS_ENABLE(padapter) && rtw_is_acs_st_valid(padapter))
			scan_ms = rtw_acs_get_adv_st(padapter);
		else
		#endif /*CONFIG_RTW_ACS*/
			scan_ms = ss->scan_ch_ms;
#endif

#if defined(CONFIG_ANTENNA_DIVERSITY) || defined(DBG_SCAN_SW_ANTDIV_BL)
		if (ss->is_sw_antdiv_bl_scan)
			scan_ms = scan_ms / 2;
#endif

#ifdef CONFIG_RTW_ACS
		if (IS_ACS_ENABLE(padapter)) {
			if (pparm->token)
				rtw_acs_trigger(padapter, scan_ms, scan_ch, NHM_PID_IEEE_11K_HIGH);
			else
				rtw_acs_trigger(padapter, scan_ms, scan_ch, NHM_PID_ACS);
		}
#endif

#ifdef CONFIG_BACKGROUND_NOISE_MONITOR
		if (IS_NM_ENABLE(padapter))
			rtw_noise_measure(padapter, scan_ch, _FALSE, 0, scan_ms / 2);
#endif
		set_survey_timer(pmlmeext, scan_ms);
		break;
	}

#ifdef CONFIG_SCAN_BACKOP
	case SCAN_BACKING_OP: {
		u8 back_ch, back_bw, back_ch_offset;
		u8 need_ch_setting_union = _TRUE;

#ifdef CONFIG_MCC_MODE
		need_ch_setting_union = rtw_hal_mcc_change_scan_flag(padapter,
				&back_ch, &back_bw, &back_ch_offset);
#endif /* CONFIG_MCC_MODE */

		if (need_ch_setting_union) {
			if (rtw_mi_get_ch_setting_union(padapter, &back_ch, &back_bw, &back_ch_offset) == 0) {
				rtw_warn_on(1);
				back_ch = pmlmeext->cur_channel;
				back_bw = pmlmeext->cur_bwmode;
				back_ch_offset = pmlmeext->cur_ch_offset;
			}
		}

		#ifdef DBG_SITESURVEY
			RTW_INFO(FUNC_ADPT_FMT" %s ch:%u, bw:%u, offset:%u at %dms\n"
				 , FUNC_ADPT_ARG(padapter)
				 , mlmeext_scan_state_str(pmlmeext)
				 , back_ch, back_bw, back_ch_offset
				, rtw_get_passing_time_ms(padapter->mlmepriv.scan_start_time)
				);
		#endif /*DBG_SITESURVEY*/
		set_channel_bwmode(padapter, back_ch, back_ch_offset, back_bw);

		sitesurvey_set_msr(padapter, _FALSE);

		val8 = 0; /* survey done */
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

		if (mlmeext_chk_scan_backop_flags(pmlmeext, SS_BACKOP_PS_ANNC)) {
			sitesurvey_set_igi(padapter);
			rtw_hal_macid_wakeup_all_used(padapter);
			rtw_ps_annc(padapter, 0);
		}

		mlmeext_set_scan_state(pmlmeext, SCAN_BACK_OP);
		ss->backop_time = rtw_get_current_time();

		if (mlmeext_chk_scan_backop_flags(pmlmeext, SS_BACKOP_TX_RESUME))
			rtw_mi_os_xmit_schedule(padapter);


		goto operation_by_state;
	}

	case SCAN_BACK_OP:
		if (rtw_get_passing_time_ms(ss->backop_time) >= ss->backop_ms
		    || pmlmeext->scan_abort
		   ) {
			mlmeext_set_scan_state(pmlmeext, SCAN_LEAVING_OP);
			goto operation_by_state;
		}
		set_survey_timer(pmlmeext, 50);
		break;

	case SCAN_LEAVING_OP:
		/*
		 * prepare to leave operating channel
		 */

		/* clear HW TX queue before scan */
		rtw_hal_set_hwreg(padapter, HW_VAR_CHECK_TXBUF, 0);

		rtw_hal_macid_sleep_all_used(padapter);
		if (mlmeext_chk_scan_backop_flags(pmlmeext, SS_BACKOP_PS_ANNC)
			&& rtw_ps_annc(padapter, 1)
		) {
			mlmeext_set_scan_state(pmlmeext, SCAN_PS_ANNC_WAIT);
			mlmeext_set_scan_next_state(pmlmeext, SCAN_LEAVE_OP);
			set_survey_timer(pmlmeext, 50); /* delay 50ms to protect nulldata(1) */
		} else {
			mlmeext_set_scan_state(pmlmeext, SCAN_LEAVE_OP);
			goto operation_by_state;
		}

		break;

	case SCAN_LEAVE_OP:
		/*
		* HW register and DM setting for enter scan
		*/

		if (mlmeext_chk_scan_backop_flags(pmlmeext, SS_BACKOP_PS_ANNC))
			sitesurvey_set_igi(padapter);

		sitesurvey_set_msr(padapter, _TRUE);

		val8 = 1; /* under site survey */
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

		mlmeext_set_scan_state(pmlmeext, SCAN_PROCESS);
		goto operation_by_state;

#endif /* CONFIG_SCAN_BACKOP */

#if defined(CONFIG_ANTENNA_DIVERSITY) || defined(DBG_SCAN_SW_ANTDIV_BL)
	case SCAN_SW_ANTDIV_BL:
		/*
		* 20100721
		* For SW antenna diversity before link, it needs to switch to another antenna and scan again.
		* It compares the scan result and select better one to do connection.
		*/
		ss->bss_cnt = 0;
		ss->channel_idx = 0;
		ss->is_sw_antdiv_bl_scan = 1;

		mlmeext_set_scan_next_state(pmlmeext, SCAN_PROCESS);
		set_survey_timer(pmlmeext, ss->scan_ch_ms);
		break;
#endif

#ifdef CONFIG_P2P
	case SCAN_TO_P2P_LISTEN:
		/*
		* Set the P2P State to the listen state of find phase
		* and set the current channel to the listen channel
		*/
		set_channel_bwmode(padapter, pwdinfo->listen_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
		rtw_p2p_set_state(pwdinfo, P2P_STATE_FIND_PHASE_LISTEN);

		/* turn on phy-dynamic functions */
		rtw_phydm_ability_restore(padapter);

		sitesurvey_set_igi(padapter);

		mlmeext_set_scan_state(pmlmeext, SCAN_P2P_LISTEN);
		_set_timer(&pwdinfo->find_phase_timer, (u32)((u32)pwdinfo->listen_dwell * 100));
		break;

	case SCAN_P2P_LISTEN:
		mlmeext_set_scan_state(pmlmeext, SCAN_PROCESS);
		ss->channel_idx = 0;
		goto operation_by_state;
#endif /* CONFIG_P2P */

	case SCAN_COMPLETE:
#ifdef CONFIG_RTW_CFGVENDOR_RANDOM_MAC_OUI
		rtw_hal_set_hw_mac_addr(padapter, adapter_mac_addr(padapter));
#endif
#ifdef CONFIG_P2P
		if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_SCAN)
		    || rtw_p2p_chk_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH)
		   ) {
#ifdef CONFIG_CONCURRENT_MODE
			if (pwdinfo->driver_interface == DRIVER_WEXT) {
				if (rtw_mi_check_status(padapter, MI_LINKED))
					_set_timer(&prochinfo->ap_roch_ch_switch_timer, 500);
			}
#endif

			rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
		}
		rtw_p2p_findphase_ex_set(pwdinfo, P2P_FINDPHASE_EX_NONE);
#endif /* CONFIG_P2P */

		/* switch channel */
		survey_done_set_ch_bw(padapter);

		sitesurvey_set_msr(padapter, _FALSE);

		val8 = 0; /* survey done */
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

		/* turn on phy-dynamic functions */
		rtw_phydm_ability_restore(padapter);

		sitesurvey_set_igi(padapter);

#ifdef CONFIG_MCC_MODE
		/* start MCC fail, then tx null data */
		if (!rtw_hal_set_mcc_setting_scan_complete(padapter))
#endif
		{
			rtw_hal_macid_wakeup_all_used(padapter);
			rtw_ps_annc(padapter, 0);
		}

		/* apply rx ampdu setting */
		rtw_rx_ampdu_apply(padapter);

		mlmeext_set_scan_state(pmlmeext, SCAN_DISABLE);

		report_surveydone_event(padapter, ss->acs);
#ifdef CONFIG_RTW_ACS
		if (IS_ACS_ENABLE(padapter))
			rtw_acs_select_best_chan(padapter);
#endif

#if defined(CONFIG_BACKGROUND_NOISE_MONITOR) && defined(DBG_NOISE_MONITOR)
		if (IS_NM_ENABLE(padapter))
			rtw_noise_info_dump(RTW_DBGDUMP, padapter);
#endif
		issue_action_BSSCoexistPacket(padapter);
		issue_action_BSSCoexistPacket(padapter);
		issue_action_BSSCoexistPacket(padapter);

#ifdef CONFIG_RTW_80211K
		if (ss->token)
			rm_post_event(padapter, ss->token, RM_EV_survey_done);
#endif /* CONFIG_RTW_80211K */

		break;
	}

	return H2C_SUCCESS;
}

u8 setauth_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct setauth_parm		*pparm = (struct setauth_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pparm->mode < 4)
		pmlmeinfo->auth_algo = pparm->mode;

	return	H2C_SUCCESS;
}

/*
SEC CAM Entry format (32 bytes)
DW0 - MAC_ADDR[15:0] | Valid[15] | MFB[14:8] | RSVD[7]  | GK[6] | MIC_KEY[5] | SEC_TYPE[4:2] | KID[1:0]
DW0 - MAC_ADDR[15:0] | Valid[15] |RSVD[14:9] | RPT_MODE[8] | SPP_MODE[7]  | GK[6] | MIC_KEY[5] | SEC_TYPE[4:2] | KID[1:0] (92E/8812A/8814A)
DW1 - MAC_ADDR[47:16]
DW2 - KEY[31:0]
DW3 - KEY[63:32]
DW4 - KEY[95:64]
DW5 - KEY[127:96]
DW6 - RSVD
DW7 - RSVD
*/

/*Set WEP key or Group Key*/
u8 setkey_hdl(_adapter *padapter, u8 *pbuf)
{
	u16	ctrl = 0;
	s16 cam_id = 0;
	struct setkey_parm		*pparm = (struct setkey_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	unsigned char null_addr[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	u8 *addr;
	bool used = _FALSE;

	/* main tx key for wep. */
	if (pparm->set_tx)
		pmlmeinfo->key_index = pparm->keyid;

#ifndef SEC_DEFAULT_KEY_SEARCH
	if (((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) || ((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE))
		cam_id = rtw_iface_bcmc_id_get(padapter);
	else
#endif
		cam_id = rtw_camid_alloc(padapter, NULL, pparm->keyid, 1,
			!!(pparm->algorithm & _SEC_TYPE_256_), &used);

	if (cam_id < 0)
		goto enable_mc;

#ifdef SEC_DEFAULT_KEY_SEARCH
	if (cam_id >= 0 && cam_id <= 3) {
		/* default key camid */
		addr = null_addr;
	} else
#endif
	{
		/* not default key camid */
		if (((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) || ((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE)) {
			/* group TX, force sec cam entry_id */
			addr = adapter_mac_addr(padapter);
		} else {
			/* group RX, searched by A2 (TA) */
			addr = get_bssid(&padapter->mlmepriv);
		}
	}

#ifdef CONFIG_LPS_PG
	if (adapter_to_pwrctl(padapter)->lps_level == LPS_PG)
		LPS_Leave(padapter, "SET_KEY");
#endif

	/* cam entry searched is pairwise key */
	if (used == _TRUE && rtw_camid_is_gk(padapter, cam_id) == _FALSE) {
		s16 camid_clr;

		RTW_PRINT(FUNC_ADPT_FMT" group key with "MAC_FMT" id:%u the same key id as pairwise key\n"
			, FUNC_ADPT_ARG(padapter), MAC_ARG(addr), pparm->keyid);

		/* HW has problem to distinguish this group key with existing pairwise key, stop HW enc and dec for BMC */
		rtw_camctl_set_flags(padapter, SEC_STATUS_STA_PK_GK_CONFLICT_DIS_BMC_SEARCH);
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, NULL);

		/* clear group key */
		while ((camid_clr = rtw_camid_search(padapter, addr, -1, 1)) >= 0) {
			RTW_PRINT("clear group key for addr:"MAC_FMT", camid:%d\n", MAC_ARG(addr), camid_clr);
			clear_cam_entry(padapter, camid_clr);
			rtw_camid_free(padapter, camid_clr);
		}

		goto enable_mc;
	}

	ctrl = BIT(15) | BIT(6) | ((pparm->algorithm & 0x07) << 2) | pparm->keyid;

	RTW_PRINT("set group key camid:%d, addr:"MAC_FMT", kid:%d, type:%s\n"
		, cam_id, MAC_ARG(addr), pparm->keyid, security_type_str(pparm->algorithm));

	if (pparm->algorithm & _SEC_TYPE_256_)  {
		RTW_INFO_DUMP("GTK : ", pparm->key, sizeof(pparm->key));
		ctrl |= BIT(9);
	}	

	write_cam(padapter, cam_id, ctrl, addr, pparm->key);

	/* if ((cam_id > 3) && (((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)))*/
#ifndef SEC_DEFAULT_KEY_SEARCH
	if (((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) || ((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE)) {
		if (is_wep_enc(pparm->algorithm)) {
			padapter->securitypriv.dot11Def_camid[pparm->keyid] = cam_id;
			padapter->securitypriv.dot118021x_bmc_cam_id =
				padapter->securitypriv.dot11Def_camid[padapter->securitypriv.dot11PrivacyKeyIndex];
			RTW_PRINT("wep group key - force camid:%d\n", padapter->securitypriv.dot118021x_bmc_cam_id);
		} else {
			/*u8 org_cam_id = padapter->securitypriv.dot118021x_bmc_cam_id;*/

			/*force GK's cam id*/
			padapter->securitypriv.dot118021x_bmc_cam_id = cam_id;

			/* for GTK rekey
			if ((org_cam_id != INVALID_SEC_MAC_CAM_ID) &&
				(org_cam_id != cam_id)) {
				RTW_PRINT("clear group key for addr:"MAC_FMT", org_camid:%d new_camid:%d\n", MAC_ARG(addr), org_cam_id, cam_id);
				clear_cam_entry(padapter, org_cam_id);
				rtw_camid_free(padapter, org_cam_id);
			}*/
		}
	}
#else
	if (cam_id >= 0 && cam_id <= 3)
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_DK_CFG, (u8 *)_TRUE);
#endif

	/* 8814au should set both broadcast and unicast CAM entry for WEP key in STA mode */
	if (is_wep_enc(pparm->algorithm) && check_mlmeinfo_state(pmlmeext, WIFI_FW_STATION_STATE) &&
	    _rtw_camctl_chk_cap(padapter, SEC_CAP_CHK_BMC)) {
		struct set_stakey_parm	sta_pparm;

		_rtw_memset(&sta_pparm, 0, sizeof(struct set_stakey_parm));
		sta_pparm.algorithm = pparm->algorithm;
		sta_pparm.keyid = pparm->keyid;
		_rtw_memcpy(sta_pparm.key, pparm->key, 16);
		_rtw_memcpy(sta_pparm.addr, get_bssid(&padapter->mlmepriv), ETH_ALEN);
		set_stakey_hdl(padapter, (u8 *)&sta_pparm);
	}

enable_mc:
	/* allow multicast packets to driver */
	rtw_hal_set_hwreg(padapter, HW_VAR_ON_RCR_AM, null_addr);

	return H2C_SUCCESS;
}

void rtw_ap_wep_pk_setting(_adapter *adapter, struct sta_info *psta)
{
	struct security_priv *psecuritypriv = &(adapter->securitypriv);
	struct set_stakey_parm	sta_pparm;
	sint keyid;

	if (!is_wep_enc(psecuritypriv->dot11PrivacyAlgrthm))
		return;

	for (keyid = 0; keyid < 4; keyid++) {
		if ((psecuritypriv->key_mask & BIT(keyid)) && (keyid == psecuritypriv->dot11PrivacyKeyIndex)) {
			sta_pparm.algorithm = psecuritypriv->dot11PrivacyAlgrthm;
			sta_pparm.keyid = keyid;
			sta_pparm.gk = 0;
			_rtw_memcpy(sta_pparm.key, &(psecuritypriv->dot11DefKey[keyid].skey[0]), 16);
			_rtw_memcpy(sta_pparm.addr, psta->cmn.mac_addr, ETH_ALEN);

			RTW_PRINT(FUNC_ADPT_FMT"set WEP - PK with "MAC_FMT" keyid:%u\n"
				, FUNC_ADPT_ARG(adapter), MAC_ARG(psta->cmn.mac_addr), keyid);

			set_stakey_hdl(adapter, (u8 *)&sta_pparm);
		}
	}
}

u8 set_stakey_hdl(_adapter *padapter, u8 *pbuf)
{
	u16 ctrl = 0;
	s16 cam_id = 0;
	bool used;
	u8 ret = H2C_SUCCESS;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct set_stakey_parm	*pparm = (struct set_stakey_parm *)pbuf;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;

	if (pparm->algorithm == _NO_PRIVACY_)
		goto write_to_cam;

	psta = rtw_get_stainfo(pstapriv, pparm->addr);
	if (!psta) {
		RTW_PRINT("%s sta:"MAC_FMT" not found\n", __func__, MAC_ARG(pparm->addr));
		ret = H2C_REJECTED;
		goto exit;
	}

	pmlmeinfo->enc_algo = pparm->algorithm;

	cam_id = rtw_camid_alloc(padapter, psta, pparm->keyid, pparm->gk,
		!!(pparm->algorithm & _SEC_TYPE_256_), &used);
	if (cam_id < 0)
		goto exit;

#ifdef CONFIG_LPS_PG
	if (adapter_to_pwrctl(padapter)->lps_level == LPS_PG)
		LPS_Leave(padapter, "SET_KEY");
#endif

	/* cam entry searched is group key when setting pariwise key */
	if (!pparm->gk && used == _TRUE && rtw_camid_is_gk(padapter, cam_id) == _TRUE) {
		s16 camid_clr;

		RTW_PRINT(FUNC_ADPT_FMT" pairwise key with "MAC_FMT" id:%u the same key id as group key\n"
			, FUNC_ADPT_ARG(padapter), MAC_ARG(pparm->addr), pparm->keyid);

		/* HW has problem to distinguish this pairwise key with existing group key, stop HW enc and dec for BMC */
		rtw_camctl_set_flags(padapter, SEC_STATUS_STA_PK_GK_CONFLICT_DIS_BMC_SEARCH);
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, NULL);

		/* clear group key */
		while ((camid_clr = rtw_camid_search(padapter, pparm->addr, -1, 1)) >= 0) {
			RTW_PRINT("clear group key for addr:"MAC_FMT", camid:%d\n", MAC_ARG(pparm->addr), camid_clr);
			clear_cam_entry(padapter, camid_clr);
			rtw_camid_free(padapter, camid_clr);
		}
	}

write_to_cam:
	if (pparm->algorithm == _NO_PRIVACY_) {
		while ((cam_id = rtw_camid_search(padapter, pparm->addr, -1, -1)) >= 0) {
			RTW_PRINT("clear key for addr:"MAC_FMT", camid:%d\n", MAC_ARG(pparm->addr), cam_id);
			clear_cam_entry(padapter, cam_id);
			rtw_camid_free(padapter, cam_id);
		}
	} else {
		RTW_PRINT("set %s key camid:%d, addr:"MAC_FMT", kid:%d, type:%s\n"
			, pparm->gk ? "group" : "pairwise"
			, cam_id, MAC_ARG(pparm->addr), pparm->keyid, security_type_str(pparm->algorithm));
		ctrl = BIT(15) | ((pparm->algorithm & 0x07) << 2) | pparm->keyid;
		if (pparm->gk)
			ctrl |= BIT(6);
		if (pparm->algorithm & _SEC_TYPE_256_) {
			RTW_INFO_DUMP("PTK : ", pparm->key, sizeof(pparm->key));
			ctrl |= BIT(9);
		}

		write_cam(padapter, cam_id, ctrl, pparm->addr, pparm->key);
	}
	ret = H2C_SUCCESS_RSP;

exit:
	return ret;
}

u8 add_ba_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct addBaReq_parm	*pparm = (struct addBaReq_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	struct sta_info *psta = rtw_get_stainfo(&padapter->stapriv, pparm->addr);

	if (!psta)
		return	H2C_SUCCESS;

#ifdef CONFIG_80211N_HT
	if (((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && (pmlmeinfo->HT_enable)) ||
	    ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE)) {
		/* pmlmeinfo->ADDBA_retry_count = 0; */
		/* pmlmeinfo->candidate_tid_bitmap |= (0x1 << pparm->tid);		 */
		/* psta->htpriv.candidate_tid_bitmap |= BIT(pparm->tid); */
		issue_addba_req(padapter, pparm->addr, (u8)pparm->tid);
		_set_timer(&psta->addba_retry_timer, ADDBA_TO);
	}
#ifdef CONFIG_TDLS
	else if ((psta->tdls_sta_state & TDLS_LINKED_STATE) &&
		 (psta->htpriv.ht_option == _TRUE) &&
		 (psta->htpriv.ampdu_enable == _TRUE)) {
		issue_addba_req(padapter, pparm->addr, (u8)pparm->tid);
		_set_timer(&psta->addba_retry_timer, ADDBA_TO);
	}
#endif /* CONFIG */
	else
		psta->htpriv.candidate_tid_bitmap &= ~BIT(pparm->tid);
#endif /* CONFIG_80211N_HT */
	return	H2C_SUCCESS;
}


u8 add_ba_rsp_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct addBaRsp_parm *pparm = (struct addBaRsp_parm *)pbuf;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;
	u8 ret = _TRUE;

	psta = rtw_get_stainfo(pstapriv, pparm->addr);
	if (!psta)
		goto exit;

	preorder_ctrl = &psta->recvreorder_ctrl[pparm->tid];
	ret = issue_addba_rsp_wait_ack(padapter, pparm->addr, pparm->tid, pparm->status, pparm->size, 3, 50);

#ifdef CONFIG_UPDATE_INDICATE_SEQ_WHILE_PROCESS_ADDBA_REQ
	/* status = 0 means accept this addba req, so update indicate seq = start_seq under this compile flag */
	if (pparm->status == 0) {
		preorder_ctrl->indicate_seq = pparm->start_seq;
		#ifdef DBG_RX_SEQ
		RTW_INFO("DBG_RX_SEQ "FUNC_ADPT_FMT" tid:%u SN_UPDATE indicate_seq:%d, start_seq:%d\n"
			, FUNC_ADPT_ARG(padapter), preorder_ctrl->tid, preorder_ctrl->indicate_seq, pparm->start_seq);
		#endif
	}
#else
	rtw_set_bit(RTW_RECV_ACK_OR_TIMEOUT, &preorder_ctrl->rec_abba_rsp_ack);
	#ifdef DBG_RX_SEQ
	RTW_INFO("DBG_RX_SEQ "FUNC_ADPT_FMT" tid:%u SN_CLEAR indicate_seq:%d, start_seq:%d preorder_ctrl->rec_abba_rsp_ack =%lu \n"
		, FUNC_ADPT_ARG(padapter)
		, preorder_ctrl->tid
		, preorder_ctrl->indicate_seq
		, pparm->start_seq
		,preorder_ctrl->rec_abba_rsp_ack
		);
	#endif
#endif

	/*
	  * status = 0 means accept this addba req
	  * status = 37 means reject this addba req
	  */
	if (pparm->status == 0) {
		preorder_ctrl->enable = _TRUE;
		preorder_ctrl->ampdu_size = pparm->size;
	} else if (pparm->status == 37)
		preorder_ctrl->enable = _FALSE;

exit:
	return H2C_SUCCESS;
}

u8 chk_bmc_sleepq_cmd(_adapter *padapter)
{
	struct cmd_obj *ph2c;
	struct cmd_priv *pcmdpriv = &(padapter->cmdpriv);
	u8 res = _SUCCESS;


	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_parm_rsp(ph2c, CMD_CHK_BMCSLEEPQ);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:


	return res;
}

u8 set_tx_beacon_cmd(_adapter *padapter, u8 flags)
{
	struct cmd_obj	*ph2c;
	struct Tx_Beacon_param	*ptxBeacon_parm;
	struct cmd_priv	*pcmdpriv = &(padapter->cmdpriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct submit_ctx sctx;
	u8	res = _SUCCESS;
	int len_diff = 0;

	/*prepare cmd parameter*/
	ptxBeacon_parm = (struct Tx_Beacon_param *)rtw_zmalloc(sizeof(struct Tx_Beacon_param));
	if (ptxBeacon_parm == NULL) {
		res = _FAIL;
		goto exit;
	}

	_rtw_memcpy(&(ptxBeacon_parm->network), &(pmlmeinfo->network), sizeof(WLAN_BSSID_EX));

	len_diff = update_hidden_ssid(
			   ptxBeacon_parm->network.IEs + _BEACON_IE_OFFSET_
		   , ptxBeacon_parm->network.IELength - _BEACON_IE_OFFSET_
			   , pmlmeinfo->hidden_ssid_mode
		   );
	ptxBeacon_parm->network.IELength += len_diff;


	/* need enqueue, prepare cmd_obj and enqueue */
	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		rtw_mfree((u8 *)ptxBeacon_parm, sizeof(*ptxBeacon_parm));
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, ptxBeacon_parm, CMD_TX_BEACON);

	if (flags & RTW_CMDF_WAIT_ACK) {
		ph2c->sctx = &sctx;
		rtw_sctx_init(&sctx, 10 * 1000);
	}

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

	if (res == _SUCCESS && (flags & RTW_CMDF_WAIT_ACK)) {
		rtw_sctx_wait(&sctx, __func__);
		_enter_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
		if (sctx.status == RTW_SCTX_SUBMITTED)
			ph2c->sctx = NULL;
		_exit_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
	}
	

exit:


	return res;
}


u8 mlme_evt_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct rtw_evt_header *evt_hdr;
	u8 *peventbuf;
	void (*event_callback)(_adapter *dev, u8 *pbuf);
	struct evt_priv *pevt_priv = &(padapter->evtpriv);

	if (pbuf == NULL)
		goto _abort_event_;

	evt_hdr = (struct rtw_evt_header *)pbuf;
	peventbuf = pbuf + sizeof(struct rtw_evt_header);

#ifdef CHECK_EVENT_SEQ
	/* checking event sequence...		 */
	if (evt_hdr->seq != (ATOMIC_READ(&pevt_priv->event_seq) & 0x7f)) {
		pevt_priv->event_seq = (evt_hdr->seq + 1) & 0x7f;
		goto _abort_event_;
	}
#endif

	/* checking if event code is valid */
	if (evt_hdr->id >= EVT_ID_MAX) {
		goto _abort_event_;
	}

	/* checking if event size match the event parm size	 */
	if ((wlanevents[evt_hdr->id].parmsize != 0) &&
	    (wlanevents[evt_hdr->id].parmsize != evt_hdr->len)) {

		goto _abort_event_;

	}

	ATOMIC_INC(&pevt_priv->event_seq);

	if (peventbuf) {
		event_callback = wlanevents[evt_hdr->id].event_callback;
		event_callback(padapter, (u8 *)peventbuf);
		pevt_priv->evt_done_cnt++;
	}

_abort_event_:
	return H2C_SUCCESS;

}

u8 chk_bmc_sleepq_hdl(_adapter *padapter, unsigned char *pbuf)
{
#ifdef CONFIG_AP_MODE
	_irqL irqL;
	struct sta_info *psta_bmc;
	_list	*xmitframe_plist, *xmitframe_phead;
	struct xmit_frame *pxmitframe = NULL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct sta_priv  *pstapriv = &padapter->stapriv;

	/* for BC/MC Frames */
	psta_bmc = rtw_get_bcmc_stainfo(padapter);
	if (!psta_bmc)
		return H2C_SUCCESS;

	if ((rtw_tim_map_is_set(padapter, pstapriv->tim_bitmap, 0)) && (psta_bmc->sleepq_len > 0)) {
#ifndef CONFIG_PCI_HCI
		rtw_msleep_os(10);/* 10ms, ATIM(HIQ) Windows */
#endif
		/* _enter_critical_bh(&psta_bmc->sleep_q.lock, &irqL); */
		_enter_critical_bh(&pxmitpriv->lock, &irqL);

		xmitframe_phead = get_list_head(&psta_bmc->sleep_q);
		xmitframe_plist = get_next(xmitframe_phead);

		while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE) {
			pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

			xmitframe_plist = get_next(xmitframe_plist);

			rtw_list_delete(&pxmitframe->list);

			psta_bmc->sleepq_len--;
			if (psta_bmc->sleepq_len > 0)
				pxmitframe->attrib.mdata = 1;
			else
				pxmitframe->attrib.mdata = 0;

			pxmitframe->attrib.triggered = 1;

			if (xmitframe_hiq_filter(pxmitframe) == _TRUE)
				pxmitframe->attrib.qsel = QSLT_HIGH;/* HIQ */

#if 0
			_exit_critical_bh(&psta_bmc->sleep_q.lock, &irqL);
			if (rtw_hal_xmit(padapter, pxmitframe) == _TRUE)
				rtw_os_xmit_complete(padapter, pxmitframe);
			_enter_critical_bh(&psta_bmc->sleep_q.lock, &irqL);
#endif
			rtw_hal_xmitframe_enqueue(padapter, pxmitframe);
		}

		/* _exit_critical_bh(&psta_bmc->sleep_q.lock, &irqL); */
		_exit_critical_bh(&pxmitpriv->lock, &irqL);

		if (rtw_get_intf_type(padapter) != RTW_PCIE) {
			/* check hi queue and bmc_sleepq */
			rtw_chk_hi_queue_cmd(padapter);
		}
	}
#endif

	return H2C_SUCCESS;
}

u8 tx_beacon_hdl(_adapter *padapter, unsigned char *pbuf)
{
#ifdef CONFIG_AP_MODE
	/*RTW_INFO(FUNC_ADPT_FMT, FUNC_ADPT_ARG(padapter));*/
#ifdef CONFIG_SWTIMER_BASED_TXBCN

	tx_beacon_handlder(padapter->dvobj);

#else

	if (send_beacon(padapter) == _FAIL) {
		RTW_INFO("issue_beacon, fail!\n");
		return H2C_PARAMETERS_ERROR;
	}

	/* tx bc/mc frames after update TIM */
	chk_bmc_sleepq_hdl(padapter, NULL);
#endif
#endif /* CONFIG_AP_MODE */
	return H2C_SUCCESS;
}

/*
* according to channel
* add/remove WLAN_BSSID_EX.IEs's ERP ie
* set WLAN_BSSID_EX.SupportedRates
* update WLAN_BSSID_EX.IEs's Supported Rate and Extended Supported Rate ie
*/
#ifdef CONFIG_AP_MODE
void change_band_update_ie(_adapter *padapter, WLAN_BSSID_EX *pnetwork, u8 ch)
{
	u8	network_type, rate_len, total_rate_len, remainder_rate_len;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	u8	erpinfo = 0x4;

	if (ch >= 36) {
		network_type = WIRELESS_11A;
		total_rate_len = IEEE80211_NUM_OFDM_RATESLEN;
		rtw_remove_bcn_ie(padapter, pnetwork, _ERPINFO_IE_);
		#ifdef CONFIG_80211AC_VHT
		/* if channel in 5G band, then add vht ie . */
		if ((pmlmepriv->htpriv.ht_option == _TRUE)
			&& REGSTY_IS_11AC_ENABLE(&padapter->registrypriv)
			&& is_supported_vht(padapter->registrypriv.wireless_mode)
			&& (!rfctl->country_ent || COUNTRY_CHPLAN_EN_11AC(rfctl->country_ent))
		) {
			if (REGSTY_IS_11AC_AUTO(&padapter->registrypriv)
				|| pmlmepriv->ori_vht_en)
				rtw_vht_ies_attach(padapter, pnetwork);
		}
		#endif
	} else {
		network_type = 0;
		total_rate_len = 0;
		if (padapter->registrypriv.wireless_mode & WIRELESS_11B) {
			network_type |= WIRELESS_11B;
			total_rate_len += IEEE80211_CCK_RATE_LEN;
		}
		if (padapter->registrypriv.wireless_mode & WIRELESS_11G) {
			network_type |= WIRELESS_11G;
			total_rate_len += IEEE80211_NUM_OFDM_RATESLEN;
		}
		rtw_add_bcn_ie(padapter, pnetwork, _ERPINFO_IE_, &erpinfo, 1);
		#ifdef CONFIG_80211AC_VHT
		rtw_vht_ies_detach(padapter, pnetwork);
		#endif
	}

	rtw_set_supported_rate(pnetwork->SupportedRates, network_type);

	UpdateBrateTbl(padapter, pnetwork->SupportedRates);

	if (total_rate_len > 8) {
		rate_len = 8;
		remainder_rate_len = total_rate_len - 8;
	} else {
		rate_len = total_rate_len;
		remainder_rate_len = 0;
	}

	rtw_add_bcn_ie(padapter, pnetwork, _SUPPORTEDRATES_IE_, pnetwork->SupportedRates, rate_len);

	if (remainder_rate_len)
		rtw_add_bcn_ie(padapter, pnetwork, _EXT_SUPPORTEDRATES_IE_, (pnetwork->SupportedRates + 8), remainder_rate_len);
	else
		rtw_remove_bcn_ie(padapter, pnetwork, _EXT_SUPPORTEDRATES_IE_);

	pnetwork->Length = get_WLAN_BSSID_EX_sz(pnetwork);
}
#endif /* CONFIG_AP_MODE */

void rtw_join_done_chk_ch(_adapter *adapter, int join_res)
{
#define DUMP_ADAPTERS_STATUS 0

	struct dvobj_priv *dvobj;
	_adapter *iface;
	struct mlme_priv *mlme;
	struct mlme_ext_priv *mlmeext;
	u8 u_ch, u_offset, u_bw;
	int i, ret;

	dvobj = adapter_to_dvobj(adapter);

	if (DUMP_ADAPTERS_STATUS) {
		RTW_INFO(FUNC_ADPT_FMT" enter\n", FUNC_ADPT_ARG(adapter));
		dump_adapters_status(RTW_DBGDUMP , dvobj);
	}

	ret = rtw_mi_get_ch_setting_union(adapter, &u_ch, &u_bw, &u_offset);
	if (join_res >= 0 && ret <= 0) {
		join_res = -1;
		dump_adapters_status(RTW_DBGDUMP , dvobj);
		rtw_warn_on(1);
	}

	if (join_res >= 0) {
#ifdef CONFIG_MCC_MODE
		/* MCC setting success, don't go to ch union process */
		if (rtw_hal_set_mcc_setting_join_done_chk_ch(adapter))
			return;
#endif /* CONFIG_MCC_MODE */

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			mlme = &iface->mlmepriv;
			mlmeext = &iface->mlmeextpriv;

			if (!iface || iface == adapter)
				continue;

#ifdef CONFIG_AP_MODE
			if ((MLME_IS_AP(iface) || MLME_IS_MESH(iface))
				&& check_fwstate(mlme, WIFI_ASOC_STATE)
			) {
				u8 ori_ch, ori_bw, ori_offset;
				bool is_grouped = rtw_is_chbw_grouped(u_ch, u_bw, u_offset
					, mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset);

				if (is_grouped == _FALSE) {
					/* handle AP which need to switch ch setting */

					ori_ch = mlmeext->cur_channel;
					ori_bw = mlmeext->cur_bwmode;
					ori_offset = mlmeext->cur_ch_offset;

					/* restore original bw, adjust bw by registry setting on target ch */
					mlmeext->cur_bwmode = mlme->ori_bw;
					mlmeext->cur_channel = u_ch;
					rtw_adjust_chbw(iface, mlmeext->cur_channel, &mlmeext->cur_bwmode, &mlmeext->cur_ch_offset);
					#ifdef CONFIG_RTW_MESH
					if (MLME_IS_MESH(iface))
						rtw_mesh_adjust_chbw(mlmeext->cur_channel, &mlmeext->cur_bwmode, &mlmeext->cur_ch_offset);
					#endif

					rtw_chset_sync_chbw(adapter_to_chset(adapter)
						, &mlmeext->cur_channel, &mlmeext->cur_bwmode, &mlmeext->cur_ch_offset
						, &u_ch, &u_bw, &u_offset, 1, 0);

					RTW_INFO(FUNC_ADPT_FMT" %u,%u,%u => %u,%u,%u\n", FUNC_ADPT_ARG(iface)
						, ori_ch, ori_bw, ori_offset
						, mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset);

					rtw_ap_update_bss_chbw(iface, &(mlmeext->mlmext_info.network)
						, mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset);

					_rtw_memcpy(&(mlme->cur_network.network), &(mlmeext->mlmext_info.network), sizeof(WLAN_BSSID_EX));

					rtw_start_bss_hdl_after_chbw_decided(iface);

					{
						#if defined(CONFIG_IOCTL_CFG80211) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
						u8 ht_option = 0;

						#ifdef CONFIG_80211N_HT
						ht_option = mlme->htpriv.ht_option;
						#endif

						rtw_cfg80211_ch_switch_notify(iface
							, mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset
							, ht_option, 0);
						#endif
					}
				}

				clr_fwstate(mlme, WIFI_OP_CH_SWITCHING);
				update_beacon(iface, 0xFF, NULL, _TRUE, 0);
			}
#endif /* CONFIG_AP_MODE */
		}

#ifdef CONFIG_DFS_MASTER
		rtw_dfs_rd_en_decision(adapter, MLME_STA_CONNECTED, 0);
#endif
	} else {
		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			mlme = &iface->mlmepriv;
			mlmeext = &iface->mlmeextpriv;

			if (!iface || iface == adapter)
				continue;
#ifdef CONFIG_AP_MODE
			if ((MLME_IS_AP(iface) || MLME_IS_MESH(iface))
				&& check_fwstate(mlme, WIFI_ASOC_STATE)
			) {
				clr_fwstate(mlme, WIFI_OP_CH_SWITCHING);
				update_beacon(iface, 0xFF, NULL, _TRUE, 0);
			}
#endif
		}
#ifdef CONFIG_DFS_MASTER
		rtw_dfs_rd_en_decision(adapter, MLME_STA_DISCONNECTED, 0);
#endif
	}

	if (rtw_mi_get_ch_setting_union(adapter, &u_ch, &u_bw, &u_offset)) {
		RTW_INFO(FUNC_ADPT_FMT" union:%u,%u,%u\n", FUNC_ADPT_ARG(adapter), u_ch, u_bw, u_offset);
		set_channel_bwmode(adapter, u_ch, u_offset, u_bw);
	}

	rtw_mi_update_union_chan_inf(adapter, u_ch, u_offset, u_bw);

	if (join_res >= 0)
		rtw_rfctl_update_op_mode(adapter_to_rfctl(adapter), BIT(adapter->iface_id), 1);

	if (DUMP_ADAPTERS_STATUS) {
		RTW_INFO(FUNC_ADPT_FMT" exit\n", FUNC_ADPT_ARG(adapter));
		dump_adapters_status(RTW_DBGDUMP , dvobj);
	}
}

int rtw_chk_start_clnt_join(_adapter *adapter, u8 *ch, u8 *bw, u8 *offset)
{
#ifdef CONFIG_CONCURRENT_MODE
	bool chbw_allow = _TRUE;
#endif
	bool connect_allow = _TRUE;
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	u8 cur_ch, cur_bw, cur_ch_offset;
	u8 u_ch, u_offset, u_bw;

	u_ch = cur_ch = pmlmeext->cur_channel;
	u_bw = cur_bw = pmlmeext->cur_bwmode;
	u_offset = cur_ch_offset = pmlmeext->cur_ch_offset;

	if (!ch || !bw || !offset) {
		connect_allow = _FALSE;
		rtw_warn_on(1);
		goto exit;
	}

	if (cur_ch == 0) {
		connect_allow = _FALSE;
		RTW_ERR(FUNC_ADPT_FMT" cur_ch:%u\n"
			, FUNC_ADPT_ARG(adapter), cur_ch);
		rtw_warn_on(1);
		goto exit;
	}
	RTW_INFO(FUNC_ADPT_FMT" req: %u,%u,%u\n", FUNC_ADPT_ARG(adapter), u_ch, u_bw, u_offset);

#ifdef CONFIG_CONCURRENT_MODE
	{
		struct dvobj_priv *dvobj;
		_adapter *iface;
		struct mlme_priv *mlme;
		struct mlme_ext_priv *mlmeext;
		struct mi_state mstate;
		int i;

		dvobj = adapter_to_dvobj(adapter);

		rtw_mi_status_no_self(adapter, &mstate);
		RTW_INFO(FUNC_ADPT_FMT" others ld_sta_num:%u, ap_num:%u, mesh_num:%u\n"
			, FUNC_ADPT_ARG(adapter), MSTATE_STA_LD_NUM(&mstate)
			, MSTATE_AP_NUM(&mstate), MSTATE_MESH_NUM(&mstate));

		if (!MSTATE_STA_LD_NUM(&mstate) && !MSTATE_AP_NUM(&mstate) && !MSTATE_MESH_NUM(&mstate)) {
			/* consider linking STA? */
			goto connect_allow_hdl;
		}

		if (rtw_mi_get_ch_setting_union_no_self(adapter, &u_ch, &u_bw, &u_offset) <= 0) {
			dump_adapters_status(RTW_DBGDUMP , dvobj);
			rtw_warn_on(1);
		}
		RTW_INFO(FUNC_ADPT_FMT" others union:%u,%u,%u\n"
			 , FUNC_ADPT_ARG(adapter), u_ch, u_bw, u_offset);

		/* chbw_allow? */
		chbw_allow = rtw_is_chbw_grouped(pmlmeext->cur_channel, pmlmeext->cur_bwmode, pmlmeext->cur_ch_offset
						 , u_ch, u_bw, u_offset);

		RTW_INFO(FUNC_ADPT_FMT" chbw_allow:%d\n"
			 , FUNC_ADPT_ARG(adapter), chbw_allow);

#ifdef CONFIG_MCC_MODE
		/* check setting success, don't go to ch union process */
		if (rtw_hal_set_mcc_setting_chk_start_clnt_join(adapter, &u_ch, &u_bw, &u_offset, chbw_allow))
			goto exit;
#endif

		if (chbw_allow == _TRUE) {
			rtw_sync_chbw(&cur_ch, &cur_bw, &cur_ch_offset, &u_ch, &u_bw, &u_offset);
			rtw_warn_on(cur_ch != pmlmeext->cur_channel);
			rtw_warn_on(cur_bw != pmlmeext->cur_bwmode);
			rtw_warn_on(cur_ch_offset != pmlmeext->cur_ch_offset);
			goto connect_allow_hdl;
		}

#ifdef CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT
		/* chbw_allow is _FALSE, connect allow? */
		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			mlme = &iface->mlmepriv;
			mlmeext = &iface->mlmeextpriv;

			if (check_fwstate(mlme, WIFI_STATION_STATE)
			    && check_fwstate(mlme, WIFI_ASOC_STATE)
#if defined(CONFIG_P2P)
			    && rtw_p2p_chk_state(&(iface->wdinfo), P2P_STATE_NONE)
#endif
			   ) {
				connect_allow = _FALSE;
				break;
			}
		}
#endif /* CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT */

		if (MSTATE_STA_LD_NUM(&mstate) + MSTATE_AP_LD_NUM(&mstate) + MSTATE_MESH_LD_NUM(&mstate) >= 4)
			connect_allow = _FALSE;

		RTW_INFO(FUNC_ADPT_FMT" connect_allow:%d\n"
			 , FUNC_ADPT_ARG(adapter), connect_allow);

		if (connect_allow == _FALSE)
			goto exit;

connect_allow_hdl:
		/* connect_allow == _TRUE */

		if (chbw_allow == _FALSE) {
			u_ch = cur_ch;
			u_bw = cur_bw;
			u_offset = cur_ch_offset;

			for (i = 0; i < dvobj->iface_nums; i++) {
				iface = dvobj->padapters[i];
				mlme = &iface->mlmepriv;
				mlmeext = &iface->mlmeextpriv;

				if (!iface || iface == adapter)
					continue;

				#ifdef CONFIG_AP_MODE
				if ((MLME_IS_AP(iface) || MLME_IS_MESH(iface))
					&& check_fwstate(mlme, WIFI_ASOC_STATE)
				) {
					#ifdef CONFIG_SPCT_CH_SWITCH
					if (1)
						rtw_ap_inform_ch_switch(iface, pmlmeext->cur_channel , pmlmeext->cur_ch_offset);
					else
					#endif
						rtw_sta_flush(iface, _FALSE);

					rtw_hal_set_hwreg(iface, HW_VAR_CHECK_TXBUF, 0);
					set_fwstate(mlme, WIFI_OP_CH_SWITCHING);
				} else
				#endif /* CONFIG_AP_MODE */
				if (check_fwstate(mlme, WIFI_STATION_STATE)
					&& check_fwstate(mlme, WIFI_ASOC_STATE)
				) {
					rtw_disassoc_cmd(iface, 500, RTW_CMDF_DIRECTLY);
					rtw_indicate_disconnect(iface, 0, _FALSE);
					rtw_free_assoc_resources(iface, _TRUE);
				}
			}
		}

		#ifdef CONFIG_DFS_MASTER
		rtw_dfs_rd_en_decision(adapter, MLME_STA_CONNECTING, 0);
		#endif
	}
#endif /* CONFIG_CONCURRENT_MODE */

exit:

	if (connect_allow == _TRUE) {
		RTW_INFO(FUNC_ADPT_FMT" union: %u,%u,%u\n", FUNC_ADPT_ARG(adapter), u_ch, u_bw, u_offset);
		rtw_mi_update_union_chan_inf(adapter, u_ch, u_offset, u_bw);
		*ch = u_ch;
		*bw = u_bw;
		*offset = u_offset;

#if defined(CONFIG_IOCTL_CFG80211) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
		{
			u8 ht_option = 0;

#ifdef CONFIG_80211N_HT
			ht_option = adapter->mlmepriv.htpriv.ht_option;
#endif /* CONFIG_80211N_HT */

			/*
				when supplicant send the mlme frame,
				the bss freq is updated by channel switch event.
			*/
			rtw_cfg80211_ch_switch_notify(adapter,
				cur_ch, cur_bw, cur_ch_offset, ht_option, 1);
		}
#endif
	}

	return connect_allow == _TRUE ? _SUCCESS : _FAIL;
}

void rtw_set_external_auth_status(_adapter *padapter,
	const void *data, int len)
{
#ifdef CONFIG_IOCTL_CFG80211
	struct net_device *dev = padapter->pnetdev;
	struct wiphy *wiphy = adapter_to_wiphy(padapter);
	struct rtw_external_auth_params params;

	/* convert data to external_auth_params */
	params.action = RTW_GET_BE32((u8 *)data);
	_rtw_memcpy(&params.bssid, (u8 *)data + 4, ETH_ALEN);
	_rtw_memcpy(&params.ssid.ssid, (u8 *)data + 10, WLAN_SSID_MAXLEN);
	params.ssid.ssid_len = RTW_GET_BE64((u8 *)data + 42);
	params.key_mgmt_suite = RTW_GET_BE32((u8 *)data + 58);
	params.status = RTW_GET_BE16((u8 *)data + 62);
	_rtw_memcpy(&params.pmkid, (u8 *)data + 64, PMKID_LEN);

	rtw_cfg80211_external_auth_status(wiphy, dev, &params);
#endif /* CONFIG_IOCTL_CFG80211 */
}

u8 rtw_iqk_hdl(_adapter *padapter, unsigned char *pbuf)
{
	rtw_hal_phydm_cal_trigger(padapter);
	return	H2C_SUCCESS;
}

u8 rtw_set_chbw_hdl(_adapter *padapter, u8 *pbuf)
{
	struct set_ch_parm *set_ch_parm;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	u8 ifbmp_s = rtw_mi_get_ld_sta_ifbmp(padapter);
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	u8 u_ch, u_bw, u_offset;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	set_ch_parm = (struct set_ch_parm *)pbuf;

	RTW_INFO(FUNC_NDEV_FMT" ch:%u, bw:%u, ch_offset:%u\n",
		 FUNC_NDEV_ARG(padapter->pnetdev),
		 set_ch_parm->ch, set_ch_parm->bw, set_ch_parm->ch_offset);

	/*  update ch, bw, offset for all asoc STA ifaces */
	if (ifbmp_s) {
		_adapter *iface;
		int i;

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (!iface || !(ifbmp_s & BIT(iface->iface_id)))
				continue;
			
			/* update STA mode ch/bw/offset */
			iface->mlmeextpriv.cur_channel = set_ch_parm->ch;
			iface->mlmeextpriv.cur_bwmode = set_ch_parm->bw;
			iface->mlmeextpriv.cur_ch_offset = set_ch_parm->ch_offset;
			/* updaet STA mode DSConfig , ap mode will update in rtw_change_bss_chbw_cmd */
			iface->mlmepriv.cur_network.network.Configuration.DSConfig = set_ch_parm->ch;
		}
	}
	
	LeaveAllPowerSaveModeDirect(padapter);
	
	set_channel_bwmode(padapter, set_ch_parm->ch, set_ch_parm->ch_offset, set_ch_parm->bw);

	rtw_mi_get_ch_setting_union(padapter, &u_ch, &u_bw, &u_offset);
	rtw_mi_update_union_chan_inf(padapter, u_ch, u_offset, u_bw);
	rtw_rfctl_update_op_mode(dvobj_to_rfctl(dvobj), 0, 0);
	
	return	H2C_SUCCESS;
}

u8 rtw_set_chplan_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct SetChannelPlan_param *param;
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	param = (struct SetChannelPlan_param *)pbuf;

	if (param->regd_src == REGD_SRC_RTK_PRIV
		&& !rtw_is_channel_plan_valid(param->channel_plan))
		return H2C_PARAMETERS_ERROR;

#ifdef CONFIG_REGD_SRC_FROM_OS
	if (rfctl->regd_src == REGD_SRC_OS)
		rtw_mfree((void *)rfctl->country_ent, sizeof(struct country_chplan));
#endif

	rfctl->regd_src = param->regd_src;
	rfctl->country_ent = param->country_ent;
	rfctl->ChannelPlan = param->channel_plan;

#if CONFIG_TXPWR_LIMIT
	rtw_txpwr_init_regd(rfctl);
#endif

	rtw_rfctl_chplan_init(padapter);

	rtw_hal_set_odm_var(padapter, HAL_ODM_REGULATION, NULL, _TRUE);

#ifdef CONFIG_IOCTL_CFG80211
	rtw_regd_apply_flags(adapter_to_wiphy(padapter));
#endif

	rtw_nlrtw_reg_change_event(padapter);

	if (GET_HAL_DATA(padapter)->txpwr_limit_loaded
		&& rtw_get_hw_init_completed(padapter))
		rtw_hal_update_txpwr_level(padapter);

	return	H2C_SUCCESS;
}

u8 rtw_get_chplan_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct get_channel_plan_param *param;
	struct get_chplan_resp *resp;
	struct rf_ctl_t *rfctl;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	rfctl = adapter_to_rfctl(padapter);
	param = (struct get_channel_plan_param *)pbuf;

	resp = rtw_vmalloc(sizeof(struct get_chplan_resp) + sizeof(RT_CHANNEL_INFO) * rfctl->max_chan_nums);
	if (!resp)
		return H2C_CMD_FAIL;

	resp->regd_src = rfctl->regd_src;

	if (rfctl->country_ent) {
		_rtw_memcpy(&resp->country_ent, rfctl->country_ent, sizeof(struct country_chplan));
		resp->has_country = 1;
	} else
		resp->has_country = 0;
	
	resp->channel_plan = rfctl->ChannelPlan;
#if CONFIG_TXPWR_LIMIT
	resp->regd_name = rfctl->regd_name;
#endif
#ifdef CONFIG_DFS_MASTER
	resp->dfs_domain = rtw_rfctl_get_dfs_domain(rfctl);
#endif
	resp->chset_num = rfctl->max_chan_nums;

	_rtw_memcpy(resp->chset, rfctl->channel_set, sizeof(RT_CHANNEL_INFO) * rfctl->max_chan_nums);
	*param->resp = resp;

	return	H2C_SUCCESS;
}

u8 led_blink_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct LedBlink_param *ledBlink_param;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	ledBlink_param = (struct LedBlink_param *)pbuf;

#ifdef CONFIG_RTW_LED_HANDLED_BY_CMD_THREAD
	BlinkHandler((PLED_DATA)ledBlink_param->pLed);
#endif

	return	H2C_SUCCESS;
}

void csa_timer_hdl(void *FunctionContext)
{
	_adapter *padapter = (_adapter *)FunctionContext;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct rf_ctl_t *rfctl = dvobj_to_rfctl(dvobj);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 i, update_beacon = _FALSE;

	for (i = 0; i < dvobj->iface_nums; i++) {
		_adapter *iface;
		iface = dvobj->padapters[i];
		if (!iface)
			continue;
		if (check_fwstate(pmlmepriv, WIFI_CSA_UPDATE_BEACON)) {
			clr_fwstate(pmlmepriv, WIFI_CSA_UPDATE_BEACON);
			update_beacon = _TRUE;
		}
	}
	
	/* wait beacons more than 70 seconds */
	if(update_beacon == _TRUE) {
		RTW_INFO("wait beacons more than 70 seconds\n");
		return ;
	}
	
	if(rfctl->csa_ch == 0) {
		RTW_INFO("channel switch done\n");
		return ;
	}
	
	/* channel switch */
	if (rtw_set_csa_cmd(padapter) != _SUCCESS) {
			rfctl->csa_ch = 0;
			rfctl->csa_switch_cnt = 0;
			rfctl->csa_ch_offset = 0;
			rfctl->csa_ch_width = 0;
			rfctl->csa_ch_freq_seg0 = 0;
			rfctl->csa_ch_freq_seg1 = 0;
	}
}

u8 set_csa_hdl(_adapter *adapter, unsigned char *pbuf)
{
#if CONFIG_DFS
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);

	if (rfctl->csa_ch)
		rtw_dfs_ch_switch_hdl(adapter_to_dvobj(adapter));
#endif
	return	H2C_SUCCESS;
}

u8 tdls_hdl(_adapter *padapter, unsigned char *pbuf)
{
#ifdef CONFIG_TDLS
	_irqL irqL;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
#ifdef CONFIG_TDLS_CH_SW
	struct tdls_ch_switch *pchsw_info = &ptdlsinfo->chsw_info;
#endif
	struct TDLSoption_param *TDLSoption;
	struct sta_info *ptdls_sta = NULL;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct sta_info *ap_sta = rtw_get_stainfo(&padapter->stapriv, get_my_bssid(&(pmlmeinfo->network)));
	u8 survey_channel, i, min, option;
	struct tdls_txmgmt txmgmt;
	u32 setchtime, resp_sleep = 0, wait_time;
	u8 zaddr[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	u8 ret;
	u8 doiqk;
	u64 tx_ra_bitmap = 0;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	TDLSoption = (struct TDLSoption_param *)pbuf;
	option = TDLSoption->option;

	if (!_rtw_memcmp(TDLSoption->addr, zaddr, ETH_ALEN)) {
		ptdls_sta = rtw_get_stainfo(&(padapter->stapriv), TDLSoption->addr);
		if (ptdls_sta == NULL)
			return H2C_REJECTED;
	} else {
		if (!(option == TDLS_RS_RCR))
			return H2C_REJECTED;
	}

	/* _enter_critical_bh(&(ptdlsinfo->hdl_lock), &irqL); */
	/* RTW_INFO("[%s] option:%d\n", __FUNCTION__, option); */

	switch (option) {
	case TDLS_ESTABLISHED: {
		/* As long as TDLS handshake success, we should set RCR_CBSSID_DATA bit to 0 */
		/* So we can receive all kinds of data frames. */
		u8 sta_band = 0;

		/* leave ALL PS when TDLS is established */
		rtw_pwr_wakeup(padapter);

		rtw_hal_rcr_set_chk_bssid(padapter, MLME_TDLS_LINKED);
		RTW_INFO("Created Direct Link with "MAC_FMT"\n", MAC_ARG(ptdls_sta->cmn.mac_addr));

		/* Set TDLS sta rate. */
		/* Update station supportRate */
		rtw_hal_update_sta_ra_info(padapter, ptdls_sta);
		tx_ra_bitmap = ptdls_sta->cmn.ra_info.ramask;

		if (pmlmeext->cur_channel > 14) {
			if (tx_ra_bitmap & 0xffff000)
				sta_band |= WIRELESS_11_5N ;

			if (tx_ra_bitmap & 0xff0)
				sta_band |= WIRELESS_11A;

			/* 5G band */
#ifdef CONFIG_80211AC_VHT
			if (ptdls_sta->vhtpriv.vht_option)
				sta_band = WIRELESS_11_5AC;
#endif

		} else {
			if (tx_ra_bitmap & 0xffff000)
				sta_band |= WIRELESS_11_24N;

			if (tx_ra_bitmap & 0xff0)
				sta_band |= WIRELESS_11G;

			if (tx_ra_bitmap & 0x0f)
				sta_band |= WIRELESS_11B;
		}
		ptdls_sta->wireless_mode = sta_band;
		rtw_hal_update_sta_wset(padapter, ptdls_sta);
		/* Sta mode */
		rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, ptdls_sta, _TRUE);

		set_sta_rate(padapter, ptdls_sta);
		rtw_sta_media_status_rpt(padapter, ptdls_sta, 1);
		break;
	}
	case TDLS_ISSUE_PTI:
		ptdls_sta->tdls_sta_state |= TDLS_WAIT_PTR_STATE;
		issue_tdls_peer_traffic_indication(padapter, ptdls_sta);
		_set_timer(&ptdls_sta->pti_timer, TDLS_PTI_TIME);
		break;
#ifdef CONFIG_TDLS_CH_SW
	case TDLS_CH_SW_RESP:
		_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
		txmgmt.status_code = 0;
		_rtw_memcpy(txmgmt.peer, ptdls_sta->cmn.mac_addr, ETH_ALEN);

		if (ap_sta)
			rtw_hal_macid_sleep(padapter, ap_sta->cmn.mac_id);
		issue_nulldata(padapter, NULL, 1, 3, 3);

		RTW_INFO("[TDLS ] issue tdls channel switch response\n");
		ret = issue_tdls_ch_switch_rsp(padapter, &txmgmt, _TRUE);

		/* If we receive TDLS_CH_SW_REQ at off channel which it's target is AP's channel */
		/* then we just switch to AP's channel*/
		if (padapter->mlmeextpriv.cur_channel == pchsw_info->off_ch_num) {
			rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_CH_SW_END_TO_BASE_CHNL);
			break;
		}

		if (ret == _SUCCESS)
			rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_CH_SW_TO_OFF_CHNL);
		else
			RTW_INFO("[TDLS] issue_tdls_ch_switch_rsp wait ack fail !!!!!!!!!!\n");

		break;
	case TDLS_CH_SW_PREPARE:
		pchsw_info->ch_sw_state |= TDLS_CH_SWITCH_PREPARE_STATE;

		/* to collect IQK info of off-chnl */
		doiqk = _TRUE;
		rtw_hal_set_hwreg(padapter, HW_VAR_DO_IQK, &doiqk);
		set_channel_bwmode(padapter, pchsw_info->off_ch_num, pchsw_info->ch_offset, (pchsw_info->ch_offset) ? CHANNEL_WIDTH_40 : CHANNEL_WIDTH_20);
		doiqk = _FALSE;
		rtw_hal_set_hwreg(padapter, HW_VAR_DO_IQK, &doiqk);

		/* switch back to base-chnl */
		doiqk = _TRUE;
		rtw_hal_set_hwreg(padapter, HW_VAR_DO_IQK, &doiqk);
		set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);
		doiqk = _FALSE;
		rtw_hal_set_hwreg(padapter, HW_VAR_DO_IQK, &doiqk);

		rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_CH_SW_START);

		pchsw_info->ch_sw_state &= ~(TDLS_CH_SWITCH_PREPARE_STATE);

		break;
	case TDLS_CH_SW_START:
		rtw_tdls_set_ch_sw_oper_control(padapter, _TRUE);
		break;
	case TDLS_CH_SW_TO_OFF_CHNL:
		if (ap_sta)
			rtw_hal_macid_sleep(padapter, ap_sta->cmn.mac_id);
		issue_nulldata(padapter, NULL, 1, 3, 3);

		if (padapter->registrypriv.wifi_spec == 0) {
		if (!(pchsw_info->ch_sw_state & TDLS_CH_SW_INITIATOR_STATE))
			_set_timer(&ptdls_sta->ch_sw_timer, (u32)(ptdls_sta->ch_switch_timeout) / 1000);
		}

		if (rtw_tdls_do_ch_sw(padapter, ptdls_sta, TDLS_CH_SW_OFF_CHNL, pchsw_info->off_ch_num,
			pchsw_info->ch_offset, (pchsw_info->ch_offset) ? CHANNEL_WIDTH_40 : CHANNEL_WIDTH_20, ptdls_sta->ch_switch_time) == _SUCCESS) {
			pchsw_info->ch_sw_state &= ~(TDLS_PEER_AT_OFF_STATE);
			if (pchsw_info->ch_sw_state & TDLS_CH_SW_INITIATOR_STATE) {
				if (issue_nulldata_to_TDLS_peer_STA(ptdls_sta->padapter, ptdls_sta->cmn.mac_addr, 0, 1, 
					(padapter->registrypriv.wifi_spec == 0) ? 3 : 0) == _FAIL)
					rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_CH_SW_TO_BASE_CHNL);
			}
		} else {
			if (!(pchsw_info->ch_sw_state & TDLS_CH_SW_INITIATOR_STATE))
				_cancel_timer_ex(&ptdls_sta->ch_sw_timer);
		}


		break;
	case TDLS_CH_SW_END:
	case TDLS_CH_SW_END_TO_BASE_CHNL:
		rtw_tdls_set_ch_sw_oper_control(padapter, _FALSE);
		_cancel_timer_ex(&ptdls_sta->ch_sw_timer);
		_cancel_timer_ex(&ptdls_sta->stay_on_base_chnl_timer);
		_cancel_timer_ex(&ptdls_sta->ch_sw_monitor_timer);
#if 0
		_rtw_memset(pHalData->tdls_ch_sw_iqk_info_base_chnl, 0x00, sizeof(pHalData->tdls_ch_sw_iqk_info_base_chnl));
		_rtw_memset(pHalData->tdls_ch_sw_iqk_info_off_chnl, 0x00, sizeof(pHalData->tdls_ch_sw_iqk_info_off_chnl));
#endif

		if (option == TDLS_CH_SW_END_TO_BASE_CHNL)
			rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_CH_SW_TO_BASE_CHNL);

		break;
	case TDLS_CH_SW_TO_BASE_CHNL_UNSOLICITED:
	case TDLS_CH_SW_TO_BASE_CHNL:
		pchsw_info->ch_sw_state &= ~(TDLS_PEER_AT_OFF_STATE | TDLS_WAIT_CH_RSP_STATE);

		if (option == TDLS_CH_SW_TO_BASE_CHNL_UNSOLICITED) {
			if (ptdls_sta != NULL) {
				/* Send unsolicited channel switch rsp. to peer */
				_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
				txmgmt.status_code = 0;
				_rtw_memcpy(txmgmt.peer, ptdls_sta->cmn.mac_addr, ETH_ALEN);
				issue_tdls_ch_switch_rsp(padapter, &txmgmt, _FALSE);
			}
		}

		if (rtw_tdls_do_ch_sw(padapter, ptdls_sta, TDLS_CH_SW_BASE_CHNL, pmlmeext->cur_channel,
			pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode, ptdls_sta->ch_switch_time) == _SUCCESS) {
			if (ap_sta)
				rtw_hal_macid_wakeup(padapter, ap_sta->cmn.mac_id);
			issue_nulldata(padapter, NULL, 0, 3, 3);
			/* set ch sw monitor timer for responder */
			if (!(pchsw_info->ch_sw_state & TDLS_CH_SW_INITIATOR_STATE))
				_set_timer(&ptdls_sta->ch_sw_monitor_timer, TDLS_CH_SW_MONITOR_TIMEOUT);
		}

		break;
#endif
	case TDLS_RS_RCR:
		rtw_hal_rcr_set_chk_bssid(padapter, MLME_TDLS_NOLINK);
		break;
	case TDLS_TEARDOWN_STA:
	case TDLS_TEARDOWN_STA_NO_WAIT:
		_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
		txmgmt.status_code = _RSON_TDLS_TEAR_UN_RSN_;
		_rtw_memcpy(txmgmt.peer, ptdls_sta->cmn.mac_addr, ETH_ALEN);

		issue_tdls_teardown(padapter, &txmgmt, (option == TDLS_TEARDOWN_STA) ? _TRUE : _FALSE);

		break;
	case TDLS_TEARDOWN_STA_LOCALLY:
	case TDLS_TEARDOWN_STA_LOCALLY_POST:
#ifdef CONFIG_TDLS_CH_SW
		if (_rtw_memcmp(TDLSoption->addr, pchsw_info->addr, ETH_ALEN) == _TRUE) {
			pchsw_info->ch_sw_state &= ~(TDLS_CH_SW_INITIATOR_STATE |
						     TDLS_CH_SWITCH_ON_STATE |
						     TDLS_PEER_AT_OFF_STATE);
			rtw_tdls_set_ch_sw_oper_control(padapter, _FALSE);
			_rtw_memset(pchsw_info->addr, 0x00, ETH_ALEN);
		}
#endif

		if (option == TDLS_TEARDOWN_STA_LOCALLY)
			rtw_tdls_teardown_pre_hdl(padapter, ptdls_sta);

		rtw_tdls_teardown_post_hdl(padapter, ptdls_sta, _FALSE);

		if (ptdlsinfo->tdls_sctx != NULL)
			rtw_sctx_done(&(ptdlsinfo->tdls_sctx));

		break;
	}

	/* _exit_critical_bh(&(ptdlsinfo->hdl_lock), &irqL); */

	return H2C_SUCCESS;
#else
	return H2C_REJECTED;
#endif /* CONFIG_TDLS */

}

u8 run_in_thread_hdl(_adapter *padapter, u8 *pbuf)
{
	struct RunInThread_param *p;


	if (NULL == pbuf)
		return H2C_PARAMETERS_ERROR;
	p = (struct RunInThread_param *)pbuf;

	if (p->func)
		p->func(p->context);

	return H2C_SUCCESS;
}

int rtw_sae_preprocess(_adapter *adapter, const u8 *buf, u32 len, u8 tx)
{
#ifdef CONFIG_IOCTL_CFG80211
	const u8 *frame_body = buf + sizeof(struct rtw_ieee80211_hdr_3addr);
	u16 alg;
	u16 seq;
	u16 status;
	int ret = _FAIL;

	alg = RTW_GET_LE16(frame_body);
	if (alg != WLAN_AUTH_SAE)
		goto exit;

	seq = RTW_GET_LE16(frame_body + 2);
	status = RTW_GET_LE16(frame_body + 4);

	RTW_INFO("RTW_%s:AUTH alg:0x%04x, seq:0x%04x, status:0x%04x, mesg:%s\n",
		(tx == _TRUE) ? "Tx" : "Rx", alg, seq, status,
		(seq == 1) ? "Commit" : "Confirm");

	ret = _SUCCESS;

#ifdef CONFIG_RTW_MESH
	if (MLME_IS_MESH(adapter)) {
		rtw_mesh_sae_check_frames(adapter, buf, len, tx, alg, seq, status);
		goto exit;
	}
#endif

	if (tx && (seq == 2) && (status == 0)) {
		/* quere commit frame until external auth statue update */
		struct sta_priv *pstapriv = &adapter->stapriv;
		struct sta_info	*psta = NULL;
		_irqL irqL;

		psta = rtw_get_stainfo(pstapriv, GetAddr1Ptr(buf));
		if (psta) {
			_enter_critical_bh(&psta->lock, &irqL);
			if (psta->pauth_frame) {
				rtw_mfree(psta->pauth_frame, psta->auth_len);
				psta->pauth_frame = NULL;
				psta->auth_len = 0;
			}

			psta->pauth_frame =  rtw_zmalloc(len);
			if (psta->pauth_frame) {
				_rtw_memcpy(psta->pauth_frame, buf, len);
				psta->auth_len = len;
			}
			_exit_critical_bh(&psta->lock, &irqL);

			ret = 2;
		}
	}
exit:
	return ret;
#else
	return _SUCCESS;
#endif /* CONFIG_IOCTL_CFG80211 */
}

