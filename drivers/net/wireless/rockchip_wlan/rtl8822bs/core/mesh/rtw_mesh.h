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
#ifndef __RTW_MESH_H_
#define __RTW_MESH_H_

#ifndef CONFIG_AP_MODE
	#error "CONFIG_RTW_MESH can't be enabled when CONFIG_AP_MODE is not defined\n"
#endif

#define RTW_MESH_TTL				31
#define RTW_MESH_PERR_MIN_INT			100
#define RTW_MESH_DEFAULT_ELEMENT_TTL		31
#define RTW_MESH_RANN_INTERVAL			5000
#define RTW_MESH_PATH_TO_ROOT_TIMEOUT		6000
#define RTW_MESH_DIAM_TRAVERSAL_TIME		50
#define RTW_MESH_PATH_TIMEOUT			5000
#define RTW_MESH_PREQ_MIN_INT			10
#define RTW_MESH_MAX_PREQ_RETRIES		4
#define RTW_MESH_MIN_DISCOVERY_TIMEOUT 		(2 * RTW_MESH_DIAM_TRAVERSAL_TIME)
#define RTW_MESH_ROOT_CONFIRMATION_INTERVAL	2000
#define RTW_MESH_PATH_REFRESH_TIME		1000
#define RTW_MESH_ROOT_INTERVAL			5000

#define RTW_MESH_SANE_METRIC_DELTA		100
#define RTW_MESH_MAX_ROOT_ADD_CHK_CNT		2

#define RTW_MESH_PLINK_UNKNOWN	0
#define RTW_MESH_PLINK_LISTEN	1
#define RTW_MESH_PLINK_OPN_SNT	2
#define RTW_MESH_PLINK_OPN_RCVD 3
#define RTW_MESH_PLINK_CNF_RCVD 4
#define RTW_MESH_PLINK_ESTAB	5
#define RTW_MESH_PLINK_HOLDING	6
#define RTW_MESH_PLINK_BLOCKED	7

extern const char *_rtw_mesh_plink_str[];
#define rtw_mesh_plink_str(s) ((s <= RTW_MESH_PLINK_BLOCKED) ? _rtw_mesh_plink_str[s] : _rtw_mesh_plink_str[RTW_MESH_PLINK_UNKNOWN])

#define RTW_MESH_PS_UNKNOWN 0
#define RTW_MESH_PS_ACTIVE 1
#define RTW_MESH_PS_LSLEEP 2
#define RTW_MESH_PS_DSLEEP 3

extern const char *_rtw_mesh_ps_str[];
#define rtw_mesh_ps_str(mps) ((mps <= RTW_MESH_PS_DSLEEP) ? _rtw_mesh_ps_str[mps] : _rtw_mesh_ps_str[RTW_MESH_PS_UNKNOWN])

#define GET_MESH_CONF_ELE_PATH_SEL_PROTO_ID(_iec)		LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 0, 0, 8)
#define GET_MESH_CONF_ELE_PATH_SEL_METRIC_ID(_iec)		LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 1, 0, 8)
#define GET_MESH_CONF_ELE_CONGEST_CTRL_MODE_ID(_iec)	LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 2, 0, 8)
#define GET_MESH_CONF_ELE_SYNC_METHOD_ID(_iec)			LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 3, 0, 8)
#define GET_MESH_CONF_ELE_AUTH_PROTO_ID(_iec)			LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 4, 0, 8)

#define GET_MESH_CONF_ELE_MESH_FORMATION(_iec)			LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 5, 0, 8)
#define GET_MESH_CONF_ELE_CTO_MGATE(_iec)				LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 5, 0, 1)
#define GET_MESH_CONF_ELE_NUM_OF_PEERINGS(_iec)			LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 5, 1, 6)
#define GET_MESH_CONF_ELE_CTO_AS(_iec)					LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 5, 7, 1)

#define GET_MESH_CONF_ELE_MESH_CAP(_iec)				LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 6, 0, 8)
#define GET_MESH_CONF_ELE_ACCEPT_PEERINGS(_iec)			LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 6, 0, 1)
#define GET_MESH_CONF_ELE_MCCA_SUP(_iec)				LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 6, 1, 1)
#define GET_MESH_CONF_ELE_MCCA_EN(_iec)					LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 6, 2, 1)
#define GET_MESH_CONF_ELE_FORWARDING(_iec)				LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 6, 3, 1)
#define GET_MESH_CONF_ELE_MBCA_EN(_iec)					LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 6, 4, 1)
#define GET_MESH_CONF_ELE_TBTT_ADJ(_iec)				LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 6, 5, 1)
#define GET_MESH_CONF_ELE_PS_LEVEL(_iec)				LE_BITS_TO_1BYTE(((u8 *)(_iec)) + 6, 6, 1)

#define SET_MESH_CONF_ELE_PATH_SEL_PROTO_ID(_iec, _val)		SET_BITS_TO_LE_1BYTE(((u8 *)(_iec)) + 0, 0, 8, _val)
#define SET_MESH_CONF_ELE_PATH_SEL_METRIC_ID(_iec, _val)	SET_BITS_TO_LE_1BYTE(((u8 *)(_iec)) + 1, 0, 8, _val)
#define SET_MESH_CONF_ELE_CONGEST_CTRL_MODE_ID(_iec, _val)	SET_BITS_TO_LE_1BYTE(((u8 *)(_iec)) + 2, 0, 8, _val)
#define SET_MESH_CONF_ELE_SYNC_METHOD_ID(_iec, _val)		SET_BITS_TO_LE_1BYTE(((u8 *)(_iec)) + 3, 0, 8, _val)
#define SET_MESH_CONF_ELE_AUTH_PROTO_ID(_iec, _val)			SET_BITS_TO_LE_1BYTE(((u8 *)(_iec)) + 4, 0, 8, _val)

#define SET_MESH_CONF_ELE_CTO_MGATE(_iec, _val)				SET_BITS_TO_LE_1BYTE(((u8 *)(_iec)) + 5, 0, 1, _val)
#define SET_MESH_CONF_ELE_NUM_OF_PEERINGS(_iec, _val)		SET_BITS_TO_LE_1BYTE(((u8 *)(_iec)) + 5, 1, 6, _val)
#define SET_MESH_CONF_ELE_CTO_AS(_iec, _val)				SET_BITS_TO_LE_1BYTE(((u8 *)(_iec)) + 5, 7, 1, _val)

#define SET_MESH_CONF_ELE_ACCEPT_PEERINGS(_iec, _val)		SET_BITS_TO_LE_1BYTE(((u8 *)(_iec)) + 6, 0, 1, _val)
#define SET_MESH_CONF_ELE_MCCA_SUP(_iec, _val)				SET_BITS_TO_LE_1BYTE(((u8 *)(_iec)) + 6, 1, 1, _val)
#define SET_MESH_CONF_ELE_MCCA_EN(_iec, _val)				SET_BITS_TO_LE_1BYTE(((u8 *)(_iec)) + 6, 2, 1, _val)
#define SET_MESH_CONF_ELE_FORWARDING(_iec, _val)			SET_BITS_TO_LE_1BYTE(((u8 *)(_iec)) + 6, 3, 1, _val)
#define SET_MESH_CONF_ELE_MBCA_EN(_iec, _val)				SET_BITS_TO_LE_1BYTE(((u8 *)(_iec)) + 6, 4, 1, _val)
#define SET_MESH_CONF_ELE_TBTT_ADJ(_iec, _val)				SET_BITS_TO_LE_1BYTE(((u8 *)(_iec)) + 6, 5, 1, _val)
#define SET_MESH_CONF_ELE_PS_LEVEL(_iec, _val)				SET_BITS_TO_LE_1BYTE(((u8 *)(_iec)) + 6, 6, 1, _val)

/* Mesh flags */
#define MESH_FLAGS_AE		0x3 /* mask */
#define MESH_FLAGS_AE_A4 	0x1
#define MESH_FLAGS_AE_A5_A6	0x2

/* Max number of paths */
#define RTW_MESH_MAX_PATHS 1024

#define RTW_PREQ_Q_F_START	0x1
#define RTW_PREQ_Q_F_REFRESH	0x2
#define RTW_PREQ_Q_F_CHK	0x4
#define RTW_PREQ_Q_F_PEER_AKA	0x8
#define RTW_PREQ_Q_F_BCAST_PREQ	0x10 /* force path_dicover using broadcast */
struct rtw_mesh_preq_queue {
	_list list;
	u8 dst[ETH_ALEN];
	u8 flags;
};

extern const u8 ae_to_mesh_ctrl_len[];

enum mesh_frame_type {
	MESH_UCAST_DATA		= 0x0,
	MESH_BMCAST_DATA	= 0x1,
	MESH_UCAST_PX_DATA	= 0x2,
	MESH_BMCAST_PX_DATA	= 0x3,
	MESH_MHOP_UCAST_ACT	= 0x4,
	MESH_MHOP_BMCAST_ACT	= 0x5,
};

enum mpath_sel_frame_type {
	MPATH_PREQ = 0,
	MPATH_PREP,
	MPATH_PERR,
	MPATH_RANN
};

/**
 * enum rtw_mesh_deferred_task_flags - mesh deferred tasks
 *
 *
 *
 * @RTW_MESH_WORK_HOUSEKEEPING: run the periodic mesh housekeeping tasks
 * @RTW_MESH_WORK_ROOT: the mesh root station needs to send a frame
 * @RTW_MESH_WORK_DRIFT_ADJUST: time to compensate for clock drift relative to other
 * mesh nodes
 * @RTW_MESH_WORK_MBSS_CHANGED: rebuild beacon and notify driver of BSS changes
 */
enum rtw_mesh_deferred_task_flags {
	RTW_MESH_WORK_HOUSEKEEPING,
	RTW_MESH_WORK_ROOT,
	RTW_MESH_WORK_DRIFT_ADJUST,
	RTW_MESH_WORK_MBSS_CHANGED,
};

#define RTW_MESH_MAX_PEER_CANDIDATES 15 /* aid consideration */
#define RTW_MESH_MAX_PEER_LINKS 8
#define RTW_MESH_PEER_LINK_TIMEOUT 20

#define RTW_MESH_PEER_CONF_DISABLED 0 /* special time value means no confirmation ongoing */
#if CONFIG_RTW_MESH_PEER_BLACKLIST
#define IS_PEER_CONF_DISABLED(plink) ((plink)->peer_conf_end_time == RTW_MESH_PEER_CONF_DISABLED)
#define IS_PEER_CONF_TIMEOUT(plink)(!IS_PEER_CONF_DISABLED(plink) && rtw_time_after(rtw_get_current_time(), (plink)->peer_conf_end_time))
#define SET_PEER_CONF_DISABLED(plink) (plink)->peer_conf_end_time = RTW_MESH_PEER_CONF_DISABLED
#define SET_PEER_CONF_END_TIME(plink, timeout_ms) \
	do { \
		(plink)->peer_conf_end_time = rtw_get_current_time() + rtw_ms_to_systime(timeout_ms); \
		if ((plink)->peer_conf_end_time == RTW_MESH_PEER_CONF_DISABLED) \
			(plink)->peer_conf_end_time++; \
	} while (0)
#else
#define IS_PEER_CONF_DISABLED(plink) 1
#define IS_PEER_CONF_TIMEOUT(plink) 0
#define SET_PEER_CONF_DISABLED(plink) do {} while (0)
#define SET_PEER_CONF_END_TIME(plink, timeout_ms) do {} while (0)
#endif /* CONFIG_RTW_MESH_PEER_BLACKLIST */

#define RTW_MESH_CTO_MGATE_CONF_DISABLED 0 /* special time value means no confirmation ongoing */
#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
#define IS_CTO_MGATE_CONF_DISABLED(plink) ((plink)->cto_mgate_conf_end_time == RTW_MESH_CTO_MGATE_CONF_DISABLED)
#define IS_CTO_MGATE_CONF_TIMEOUT(plink)(!IS_CTO_MGATE_CONF_DISABLED(plink) && rtw_time_after(rtw_get_current_time(), (plink)->cto_mgate_conf_end_time))
#define SET_CTO_MGATE_CONF_DISABLED(plink) (plink)->cto_mgate_conf_end_time = RTW_MESH_CTO_MGATE_CONF_DISABLED
#define SET_CTO_MGATE_CONF_END_TIME(plink, timeout_ms) \
	do { \
		(plink)->cto_mgate_conf_end_time = rtw_get_current_time() + rtw_ms_to_systime(timeout_ms); \
		if ((plink)->cto_mgate_conf_end_time == RTW_MESH_CTO_MGATE_CONF_DISABLED) \
			(plink)->cto_mgate_conf_end_time++; \
	} while (0)
#else
#define IS_CTO_MGATE_CONF_DISABLED(plink) 1
#define IS_CTO_MGATE_CONF_TIMEOUT(plink) 0
#define SET_CTO_MGATE_CONF_DISABLED(plink) do {} while (0)
#define SET_CTO_MGATE_CONF_END_TIME(plink, timeout_ms) do {} while (0)
#endif /* CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST */

struct mesh_plink_ent {
	u8 valid;
	u8 addr[ETH_ALEN];
	u8 plink_state;

#ifdef CONFIG_RTW_MESH_AEK
	u8 aek_valid;
	u8 aek[32];
#endif

	u16 llid;
	u16 plid;
#ifndef CONFIG_RTW_MESH_DRIVER_AID
	u16 aid; /* aid assigned from upper layer */
#endif
	u16 peer_aid; /* aid assigned from peer */

	u8 chosen_pmk[16];

#ifdef CONFIG_RTW_MESH_AEK
	u8 sel_pcs[4];
	u8 l_nonce[32];
	u8 p_nonce[32];
#endif

#ifdef CONFIG_RTW_MESH_DRIVER_AID
	u8 *tx_conf_ies;
	u16 tx_conf_ies_len;
#endif
	u8 *rx_conf_ies;
	u16 rx_conf_ies_len;

	struct wlan_network *scanned;

#if CONFIG_RTW_MESH_PEER_BLACKLIST
	systime peer_conf_end_time;
#endif
#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
	systime cto_mgate_conf_end_time;
#endif
};

#ifdef CONFIG_RTW_MESH_AEK
#define MESH_PLINK_AEK_VALID(ent) ent->aek_valid
#else
#define MESH_PLINK_AEK_VALID(ent) 0
#endif

struct mesh_plink_pool {
	_lock lock;
	u8 num; /* current ent being used */
	struct mesh_plink_ent ent[RTW_MESH_MAX_PEER_CANDIDATES];

#if CONFIG_RTW_MESH_ACNODE_PREVENT
	u8 acnode_rsvd;
#endif

#if CONFIG_RTW_MESH_PEER_BLACKLIST
	_queue peer_blacklist;
#endif
#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
	_queue cto_mgate_blacklist;
#endif
};

struct mesh_peer_sel_policy {
	u32 scanr_exp_ms;

#if CONFIG_RTW_MESH_ACNODE_PREVENT
	u8 acnode_prevent;
	u32 acnode_conf_timeout_ms;
	u32 acnode_notify_timeout_ms;
#endif

#if CONFIG_RTW_MESH_OFFCH_CAND
	u8 offch_cand;
	u32 offch_find_int_ms; /* 0 means no offch find triggerred by driver self*/
#endif

#if CONFIG_RTW_MESH_PEER_BLACKLIST
	u32 peer_conf_timeout_ms;
	u32 peer_blacklist_timeout_ms;
#endif

#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
	u8 cto_mgate_require;
	u32 cto_mgate_conf_timeout_ms;
	u32 cto_mgate_blacklist_timeout_ms;
#endif
};

/* b2u flags */
#define RTW_MESH_B2U_ALL		BIT0
#define RTW_MESH_B2U_GA_UCAST	BIT1 /* Group addressed unicast frame, forward only */
#define RTW_MESH_B2U_BCAST		BIT2
#define RTW_MESH_B2U_IP_MCAST	BIT3

#define rtw_msrc_b2u_policy_chk(flags, mda) ( \
	(flags & RTW_MESH_B2U_ALL) \
	|| ((flags & RTW_MESH_B2U_BCAST) && is_broadcast_mac_addr(mda)) \
	|| ((flags & RTW_MESH_B2U_IP_MCAST) && (IP_MCAST_MAC(mda) || ICMPV6_MCAST_MAC(mda))) \
	)

#define rtw_mfwd_b2u_policy_chk(flags, mda, ucst) ( \
	(flags & RTW_MESH_B2U_ALL) \
	|| ((flags & RTW_MESH_B2U_GA_UCAST) && ucst) \
	|| ((flags & RTW_MESH_B2U_BCAST) && is_broadcast_mac_addr(mda)) \
	|| ((flags & RTW_MESH_B2U_IP_MCAST) && (IP_MCAST_MAC(mda) || ICMPV6_MCAST_MAC(mda))) \
	)

/**
 * @sane_metric_delta: Controlling if trigger additional path check mechanism
 * @max_root_add_chk_cnt: The retry cnt to send additional root confirmation
 *	PREQ through old(last) path
 */
struct rtw_mesh_cfg {
	u8 max_peer_links; /* peering limit */
	u32 plink_timeout; /* seconds */

	u8 dot11MeshTTL;
	u8 element_ttl;
	u32 path_refresh_time;
	u16 dot11MeshHWMPpreqMinInterval;
	u16 dot11MeshHWMPnetDiameterTraversalTime;
	u32 dot11MeshHWMPactivePathTimeout;
	u8 dot11MeshHWMPmaxPREQretries;
	u16 min_discovery_timeout;
	u16 dot11MeshHWMPconfirmationInterval;
	u16 dot11MeshHWMPperrMinInterval;
	u8 dot11MeshHWMPRootMode;
	BOOLEAN dot11MeshForwarding;
	s32 rssi_threshold; /* in dBm, 0: no specified */
	u16 dot11MeshHWMPRannInterval;
	BOOLEAN dot11MeshGateAnnouncementProtocol;
	u32 dot11MeshHWMPactivePathToRootTimeout;
	u16 dot11MeshHWMProotInterval;
	u8 path_gate_timeout_factor;
#ifdef CONFIG_RTW_MESH_ADD_ROOT_CHK
	u16 sane_metric_delta;
	u8 max_root_add_chk_cnt;
#endif

	struct mesh_peer_sel_policy peer_sel_policy;

#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
	u8 b2u_flags_msrc;
	u8 b2u_flags_mfwd;
#endif
};

struct rtw_mesh_stats {
	u32 fwded_mcast;		/* Mesh forwarded multicast frames */
	u32 fwded_unicast;		/* Mesh forwarded unicast frames */
	u32 fwded_frames;		/* Mesh total forwarded frames */
	u32 dropped_frames_ttl;	/* Not transmitted since mesh_ttl == 0*/
	u32 dropped_frames_no_route;	/* Not transmitted, no route found */
	u32 dropped_frames_congestion;/* Not forwarded due to congestion */
	u32 dropped_frames_duplicate;

	u32 mrc_del_qlen; /* MRC entry deleted cause by queue length limit */
};

struct rtw_mrc;

struct rtw_mesh_info {
	u8 mesh_id[NDIS_802_11_LENGTH_SSID];
	size_t mesh_id_len;
	/* Active Path Selection Protocol Identifier */
	u8 mesh_pp_id;
	/* Active Path Selection Metric Identifier */
	u8 mesh_pm_id;
	/* Congestion Control Mode Identifier */
	u8 mesh_cc_id;
	/* Synchronization Protocol Identifier */
	u8 mesh_sp_id;
	/* Authentication Protocol Identifier */
	u8 mesh_auth_id;

	struct mesh_plink_pool plink_ctl;

	u32 mesh_seqnum;
	/* MSTA's own hwmp sequence number */
	u32 sn;
	systime last_preq;
	systime last_sn_update;
	systime next_perr;
	/* Last used Path Discovery ID */
	u32 preq_id;
	
	ATOMIC_T mpaths;
	struct rtw_mesh_table *mesh_paths;
	struct rtw_mesh_table *mpp_paths;
	int mesh_paths_generation;
	int mpp_paths_generation;

	int num_gates;
	struct rtw_mesh_path *max_addr_gate;
	bool max_addr_gate_is_larger_than_self;

	struct rtw_mesh_stats mshstats;

	_queue mpath_tx_queue;
	u32 mpath_tx_queue_len;
	_tasklet mpath_tx_tasklet;

	struct rtw_mrc *mrc;

	_lock mesh_preq_queue_lock;
	struct rtw_mesh_preq_queue preq_queue;
	int preq_queue_len;
};

extern const char *_action_self_protected_str[];
#define action_self_protected_str(action) ((action < RTW_ACT_SELF_PROTECTED_NUM) ? _action_self_protected_str[action] : _action_self_protected_str[0])

u8 *rtw_set_ie_mesh_id(u8 *buf, u32 *buf_len, const char *mesh_id, u8 id_len);
u8 *rtw_set_ie_mesh_config(u8 *buf, u32 *buf_len
	, u8 path_sel_proto, u8 path_sel_metric, u8 congest_ctl_mode, u8 sync_method, u8 auth_proto
	, u8 num_of_peerings, bool cto_mgate, bool cto_as
	, bool accept_peerings, bool mcca_sup, bool mcca_en, bool forwarding
	, bool mbca_en, bool tbtt_adj, bool ps_level);

int rtw_bss_is_same_mbss(WLAN_BSSID_EX *a, WLAN_BSSID_EX *b);
int rtw_bss_is_candidate_mesh_peer(WLAN_BSSID_EX *self, WLAN_BSSID_EX *target, u8 ch, u8 add_peer);

void rtw_chk_candidate_peer_notify(_adapter *adapter, struct wlan_network *scanned);

void rtw_mesh_peer_status_chk(_adapter *adapter);

#if CONFIG_RTW_MESH_ACNODE_PREVENT
void rtw_mesh_update_scanned_acnode_status(_adapter *adapter, struct wlan_network *scanned);
bool rtw_mesh_scanned_is_acnode_confirmed(_adapter *adapter, struct wlan_network *scanned);
bool rtw_mesh_acnode_prevent_allow_sacrifice(_adapter *adapter);
struct sta_info *rtw_mesh_acnode_prevent_pick_sacrifice(_adapter *adapter);
void dump_mesh_acnode_prevent_settings(void *sel, _adapter *adapter);
#endif

#if CONFIG_RTW_MESH_OFFCH_CAND
u8 rtw_mesh_offch_candidate_accepted(_adapter *adapter);
u8 rtw_mesh_select_operating_ch(_adapter *adapter);
void dump_mesh_offch_cand_settings(void *sel, _adapter *adapter);
#endif

#if CONFIG_RTW_MESH_PEER_BLACKLIST
int rtw_mesh_peer_blacklist_add(_adapter *adapter, const u8 *addr);
int rtw_mesh_peer_blacklist_del(_adapter *adapter, const u8 *addr);
int rtw_mesh_peer_blacklist_search(_adapter *adapter, const u8 *addr);
void rtw_mesh_peer_blacklist_flush(_adapter *adapter);
void dump_mesh_peer_blacklist(void *sel, _adapter *adapter);
void dump_mesh_peer_blacklist_settings(void *sel, _adapter *adapter);
#endif
#if CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
u8 rtw_mesh_cto_mgate_required(_adapter *adapter);
u8 rtw_mesh_cto_mgate_network_filter(_adapter *adapter, struct wlan_network *scanned);
int rtw_mesh_cto_mgate_blacklist_add(_adapter *adapter, const u8 *addr);
int rtw_mesh_cto_mgate_blacklist_del(_adapter *adapter, const u8 *addr);
int rtw_mesh_cto_mgate_blacklist_search(_adapter *adapter, const u8 *addr);
void rtw_mesh_cto_mgate_blacklist_flush(_adapter *adapter);
void dump_mesh_cto_mgate_blacklist(void *sel, _adapter *adapter);
void dump_mesh_cto_mgate_blacklist_settings(void *sel, _adapter *adapter);
#endif
void dump_mesh_peer_sel_policy(void *sel, _adapter *adapter);
void dump_mesh_networks(void *sel, _adapter *adapter);

void rtw_mesh_adjust_chbw(u8 req_ch, u8 *req_bw, u8 *req_offset);

void rtw_mesh_sae_check_frames(_adapter *adapter, const u8 *buf, u32 len, u8 tx, u16 alg, u16 seq, u16 status);
int rtw_mesh_check_frames_tx(_adapter *adapter, const u8 **buf, size_t *len);
int rtw_mesh_check_frames_rx(_adapter *adapter, const u8 *buf, size_t len);

int rtw_mesh_on_auth(_adapter *adapter, union recv_frame *rframe);
unsigned int on_action_self_protected(_adapter *adapter, union recv_frame *rframe);

bool rtw_mesh_update_bss_peering_status(_adapter *adapter, WLAN_BSSID_EX *bss);
bool rtw_mesh_update_bss_formation_info(_adapter *adapter, WLAN_BSSID_EX *bss);
bool rtw_mesh_update_bss_forwarding_state(_adapter *adapter, WLAN_BSSID_EX *bss);

struct mesh_plink_ent *_rtw_mesh_plink_get(_adapter *adapter, const u8 *hwaddr);
struct mesh_plink_ent *rtw_mesh_plink_get(_adapter *adapter, const u8 *hwaddr);
struct mesh_plink_ent *rtw_mesh_plink_get_no_estab_by_idx(_adapter *adapter, u8 idx);
int _rtw_mesh_plink_add(_adapter *adapter, const u8 *hwaddr);
int rtw_mesh_plink_add(_adapter *adapter, const u8 *hwaddr);
int rtw_mesh_plink_set_state(_adapter *adapter, const u8 *hwaddr, u8 state);
#ifdef CONFIG_RTW_MESH_AEK
int rtw_mesh_plink_set_aek(_adapter *adapter, const u8 *hwaddr, const u8 *aek);
#endif
#if CONFIG_RTW_MESH_PEER_BLACKLIST
int rtw_mesh_plink_set_peer_conf_timeout(_adapter *adapter, const u8 *hwaddr);
#endif
void _rtw_mesh_plink_del_ent(_adapter *adapter, struct mesh_plink_ent *ent);
int rtw_mesh_plink_del(_adapter *adapter, const u8 *hwaddr);
void rtw_mesh_plink_ctl_init(_adapter *adapter);
void rtw_mesh_plink_ctl_deinit(_adapter *adapter);
void dump_mesh_plink_ctl(void *sel, _adapter *adapter);

int rtw_mesh_peer_establish(_adapter *adapter, struct mesh_plink_ent *plink, struct sta_info *sta);
void _rtw_mesh_expire_peer_ent(_adapter *adapter, struct mesh_plink_ent *plink);
void rtw_mesh_expire_peer(_adapter *adapter, const u8 *peer_addr);
u8 rtw_mesh_ps_annc(_adapter *adapter, u8 ps);

unsigned int on_action_mesh(_adapter *adapter, union recv_frame *rframe);

void rtw_mesh_cfg_init(_adapter *adapter);
void rtw_mesh_cfg_init_max_peer_links(_adapter *adapter, u8 stack_conf);
void rtw_mesh_cfg_init_plink_timeout(_adapter *adapter, u32 stack_conf);
void rtw_mesh_init_mesh_info(_adapter *adapter);
void rtw_mesh_deinit_mesh_info(_adapter *adapter);

#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
void dump_mesh_b2u_flags(void *sel, _adapter *adapter);
#endif

int rtw_mesh_addr_resolve(_adapter *adapter, struct xmit_frame *xframe, _pkt *pkt, _list *b2u_list);

s8 rtw_mesh_tx_set_whdr_mctrl_len(u8 mesh_frame_mode, struct pkt_attrib *attrib);
void rtw_mesh_tx_build_mctrl(_adapter *adapter, struct pkt_attrib *attrib, u8 *buf);
u8 rtw_mesh_tx_build_whdr(_adapter *adapter, struct pkt_attrib *attrib
	, u16 *fctrl, struct rtw_ieee80211_hdr *whdr);

int rtw_mesh_rx_data_validate_hdr(_adapter *adapter, union recv_frame *rframe, struct sta_info **sta);
int rtw_mesh_rx_data_validate_mctrl(_adapter *adapter, union recv_frame *rframe
	, const struct rtw_ieee80211s_hdr *mctrl, const u8 *mda, const u8 *msa
	, u8 *mctrl_len, const u8 **da, const u8 **sa);
int rtw_mesh_rx_validate_mctrl_non_amsdu(_adapter *adapter, union recv_frame *rframe);

int rtw_mesh_rx_msdu_act_check(union recv_frame *rframe
	, const u8 *mda, const u8 *msa
	, const u8 *da, const u8 *sa
	, struct rtw_ieee80211s_hdr *mctrl
	, struct xmit_frame **fwd_frame, _list *b2u_list);

void dump_mesh_stats(void *sel, _adapter *adapter);

#if defined(PLATFORM_LINUX) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
#define rtw_lockdep_assert_held(l) lockdep_assert_held(l)
#define rtw_lockdep_is_held(l) lockdep_is_held(l)
#else
#error "TBD\n"
#endif

#include "rtw_mesh_pathtbl.h"
#include "rtw_mesh_hwmp.h"
#endif /* __RTW_MESH_H_ */

