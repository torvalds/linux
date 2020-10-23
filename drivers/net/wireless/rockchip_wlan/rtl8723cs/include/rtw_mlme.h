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
#ifndef __RTW_MLME_H_
#define __RTW_MLME_H_


#define	MAX_BSS_CNT	128
/* #define   MAX_JOIN_TIMEOUT	2000 */
/* #define   MAX_JOIN_TIMEOUT	2500 */
#define   MAX_JOIN_TIMEOUT	6500

/*	Commented by Albert 20101105
 *	Increase the scanning timeout because of increasing the SURVEY_TO value. */
#define	SCANQUEUE_LIFETIME 20000 /* 20sec, unit:msec */

#define MAX_UNASSOC_STA_CNT 128
#define UNASSOC_STA_LIFETIME_MS 60000

/*pmlmepriv->fw_state*/
#define WIFI_NULL_STATE				0x00000000
#define WIFI_ASOC_STATE				0x00000001 /* Linked */
#define WIFI_REASOC_STATE			0x00000002
#define WIFI_SLEEP_STATE			0x00000004
#define WIFI_STATION_STATE			0x00000008
#define WIFI_AP_STATE				0x00000010
#define WIFI_ADHOC_STATE			0x00000020
#define WIFI_ADHOC_MASTER_STATE		0x00000040
#define WIFI_UNDER_LINKING			0x00000080
#define WIFI_UNDER_WPS				0x00000100
#define WIFI_MESH_STATE				0x00000200
#define WIFI_STA_ALIVE_CHK_STATE		0x00000400
#define WIFI_UNDER_SURVEY			0x00000800 /* under site surveying */
/*#define WIFI_UNDEFINED_STATE			0x00001000*/
/*#define WIFI_UNDEFINED_STATE			0x00002000*/
/*#define WIFI_UNDEFINED_STATE			0x00004000*/
/*#define WIFI_UNDEFINED_STATE			0x00008000*/
#define WIFI_MP_STATE				0x00010000
/*#define WIFI_UNDEFINED_STATE			0x00020000*/
/*#define WIFI_UNDEFINED_STATE			0x00040000*/
/*#define WIFI_UNDEFINED_STATE			0x00080000*/
/*#define WIFI_UNDEFINED_STATE			0x00100000*/
/*#define WIFI_UNDEFINED_STATE			0x00200000*/ 
/*#define WIFI_UNDEFINED_STATE			0x00400000*/
#define WIFI_OP_CH_SWITCHING			0x00800000
#define WIFI_UNDER_KEY_HANDSHAKE		0x01000000
/*#define WIFI_UNDEFINED_STATE			0x02000000*/
/*#define WIFI_UNDEFINED_STATE			0x04000000*/
/*#define WIFI_UNDEFINED_STATE			0x08000000*/
/*#define WIFI_UNDEFINED_STATE			0x10000000*/
/*#define WIFI_UNDEFINED_STATE			0x20000000*/
#define WIFI_CSA_UPDATE_BEACON			0x40000000
#define WIFI_MONITOR_STATE			0x80000000


#define MIRACAST_DISABLED	0
#define MIRACAST_SOURCE		BIT0
#define MIRACAST_SINK		BIT1

#define MIRACAST_MODE_REVERSE(mode) \
	((((mode) & MIRACAST_SOURCE) ? MIRACAST_SINK : 0) | (((mode) & MIRACAST_SINK) ? MIRACAST_SOURCE : 0))

bool is_miracast_enabled(_adapter *adapter);
bool rtw_chk_miracast_mode(_adapter *adapter, u8 mode);
const char *get_miracast_mode_str(int mode);
void rtw_wfd_st_switch(struct sta_info *sta, bool on);

#define MLME_STATE(adapter) get_fwstate(&((adapter)->mlmepriv))
#define CHK_MLME_STATE(adapter, state) check_fwstate(&((adapter)->mlmepriv), (state))

#define MLME_IS_NULL(adapter) CHK_MLME_STATE(adapter, WIFI_NULL_STATE)
#define MLME_IS_STA(adapter) CHK_MLME_STATE(adapter, WIFI_STATION_STATE)
#define MLME_IS_AP(adapter) CHK_MLME_STATE(adapter, WIFI_AP_STATE)
#define MLME_IS_ADHOC(adapter) CHK_MLME_STATE(adapter, WIFI_ADHOC_STATE)
#define MLME_IS_ADHOC_MASTER(adapter) CHK_MLME_STATE(adapter, WIFI_ADHOC_MASTER_STATE)
#define MLME_IS_MESH(adapter) CHK_MLME_STATE(adapter, WIFI_MESH_STATE)
#define MLME_IS_MONITOR(adapter) CHK_MLME_STATE(adapter, WIFI_MONITOR_STATE)
#define MLME_IS_MP(adapter) CHK_MLME_STATE(adapter, WIFI_MP_STATE)
#ifdef CONFIG_P2P
	#define MLME_IS_PD(adapter) rtw_p2p_chk_role(&(adapter)->wdinfo, P2P_ROLE_DEVICE)
	#define MLME_IS_GC(adapter) rtw_p2p_chk_role(&(adapter)->wdinfo, P2P_ROLE_CLIENT)
	#define MLME_IS_GO(adapter) rtw_p2p_chk_role(&(adapter)->wdinfo, P2P_ROLE_GO)
#else /* !CONFIG_P2P */
	#define MLME_IS_PD(adapter) 0
	#define MLME_IS_GC(adapter) 0
	#define MLME_IS_GO(adapter) 0
#endif /* !CONFIG_P2P */

#define MLME_IS_MSRC(adapter) rtw_chk_miracast_mode((adapter), MIRACAST_SOURCE)
#define MLME_IS_MSINK(adapter) rtw_chk_miracast_mode((adapter), MIRACAST_SINK)

#define MLME_IS_SCAN(adapter) CHK_MLME_STATE(adapter, WIFI_UNDER_SURVEY)
#define MLME_IS_LINKING(adapter) CHK_MLME_STATE(adapter, WIFI_UNDER_LINKING)
#define MLME_IS_ASOC(adapter) CHK_MLME_STATE(adapter, WIFI_ASOC_STATE)
#define MLME_IS_OPCH_SW(adapter) CHK_MLME_STATE(adapter, WIFI_OP_CH_SWITCHING)
#define MLME_IS_WPS(adapter) CHK_MLME_STATE(adapter, WIFI_UNDER_WPS)

#ifdef CONFIG_IOCTL_CFG80211
#define MLME_IS_ROCH(adapter) (rtw_cfg80211_get_is_roch(adapter) == _TRUE)
#else
#define MLME_IS_ROCH(adapter) 0
#endif

#ifdef CONFIG_IOCTL_CFG80211
#define MLME_IS_MGMT_TX(adapter) rtw_cfg80211_get_is_mgmt_tx(adapter)
#else
#define MLME_IS_MGMT_TX(adapter) 0
#endif

#define MLME_STATE_FMT "%s%s%s%s%s%s%s%s%s%s%s%s"
#define MLME_STATE_ARG(adapter) \
	MLME_IS_STA((adapter)) ? (MLME_IS_GC((adapter)) ? " GC" : " STA") : \
	MLME_IS_AP((adapter)) ? (MLME_IS_GO((adapter)) ? " GO" : " AP") : \
	MLME_IS_ADHOC((adapter)) ? " ADHOC" : \
	MLME_IS_ADHOC_MASTER((adapter)) ? " ADHOC_M" : \
	MLME_IS_MESH((adapter)) ? " MESH" : \
	MLME_IS_MONITOR((adapter)) ? " MONITOR" : \
	MLME_IS_MP((adapter)) ? " MP" : "", \
	MLME_IS_PD((adapter)) ? " PD" : "", \
	MLME_IS_MSRC((adapter)) ? " MSRC" : "", \
	MLME_IS_MSINK((adapter)) ? " MSINK" : "", \
	MLME_IS_SCAN((adapter)) ? " SCAN" : "", \
	MLME_IS_LINKING((adapter)) ? " LINKING" : "", \
	MLME_IS_ASOC((adapter)) ? " ASOC" : "", \
	MLME_IS_OPCH_SW((adapter)) ? " OPCH_SW" : "", \
	MLME_IS_WPS((adapter)) ? " WPS" : "", \
	MLME_IS_ROCH((adapter)) ? " ROCH" : "", \
	MLME_IS_MGMT_TX((adapter)) ? " MGMT_TX" : "", \
	(MLME_STATE((adapter)) & WIFI_SLEEP_STATE) ? " SLEEP" : ""

enum {
	MLME_ACTION_UNKNOWN,
	MLME_ACTION_NONE,
	MLME_SCAN_ENABLE, /* WIFI_UNDER_SURVEY */
	MLME_SCAN_ENTER, /* WIFI_UNDER_SURVEY && !SCAN_DISABLE && !SCAN_BACK_OP */
	MLME_SCAN_DONE, /*  WIFI_UNDER_SURVEY && (SCAN_DISABLE || SCAN_BACK_OP) */
	MLME_SCAN_DISABLE, /* WIFI_UNDER_SURVEY is going to be cleared */
	MLME_STA_CONNECTING,
	MLME_STA_CONNECTED,
	MLME_STA_DISCONNECTED,
	MLME_TDLS_LINKED,
	MLME_TDLS_NOLINK,
	MLME_AP_STARTED,
	MLME_AP_STOPPED,
	MLME_ADHOC_STARTED,
	MLME_ADHOC_STOPPED,
	MLME_MESH_STARTED,
	MLME_MESH_STOPPED,
	MLME_OPCH_SWITCH,
};
#ifdef CONFIG_WOW_KEEP_ALIVE_PATTERN
enum MODE_WOW_KEEP_ALIVE_PATTERN {
	wow_keep_alive_pattern_disable = 0,
	wow_keep_alive_pattern_tx,
	wow_keep_alive_pattern_trx,
	wow_keep_alive_pattern_trx_with_ack
};
#endif /*CONFIG_WOW_KEEP_ALIVE_PATTERN*/
enum dot11AuthAlgrthmNum {
	dot11AuthAlgrthm_Open = 0,
	dot11AuthAlgrthm_Shared,
	dot11AuthAlgrthm_8021X,
	dot11AuthAlgrthm_Auto,
	dot11AuthAlgrthm_WAPI,
	dot11AuthAlgrthm_MaxNum
};

/**
 * enum mlme_auth_type - AuthenticationType
 *
 * @MLME_AUTHTYPE_OPEN_SYSTEM: Open System authentication
 * @MLME_AUTHTYPE_SHARED_KEY: Shared Key authentication (WEP only)
 * @MLME_AUTHTYPE_FT: Fast BSS Transition (IEEE 802.11r)
 * @MLME_AUTHTYPE_NETWORK_EAP: Network EAP (some Cisco APs and mainly LEAP)
 * @MLME_AUTHTYPE_SAE: Simultaneous authentication of equals
 * @MLME_AUTHTYPE_FILS_SK: Fast Initial Link Setup shared key
 * @MLME_AUTHTYPE_FILS_SK_PFS: Fast Initial Link Setup shared key with PFS
 * @MLME_AUTHTYPE_FILS_PK: Fast Initial Link Setup public key
 * @__MLME_AUTHTYPE_NUM: internal
 * @MLME_AUTHTYPE_MAX: maximum valid auth algorithm
 * @MLME_AUTHTYPE_AUTOMATIC: determine automatically (if necessary by trying
 *      multiple times); this is invalid in netlink -- leave out the attribute
 *      for this on CONNECT commands.
 */
enum mlme_auth_type {
	MLME_AUTHTYPE_OPEN_SYSTEM,
	MLME_AUTHTYPE_SHARED_KEY,
	MLME_AUTHTYPE_FT,
	MLME_AUTHTYPE_NETWORK_EAP,
	MLME_AUTHTYPE_SAE,
	MLME_AUTHTYPE_FILS_SK,
	MLME_AUTHTYPE_FILS_SK_PFS,
	MLME_AUTHTYPE_FILS_PK,

	/* keep last */
	__MLME_AUTHTYPE_NUM,
	MLME_AUTHTYPE_MAX = __MLME_AUTHTYPE_NUM - 1,
	MLME_AUTHTYPE_AUTOMATIC
};

/* Scan type including active and passive scan. */
typedef enum _RT_SCAN_TYPE {
	SCAN_PASSIVE,
	SCAN_ACTIVE,
	SCAN_MIX,
} RT_SCAN_TYPE, *PRT_SCAN_TYPE;

#define WIFI_FREQUENCY_BAND_AUTO 0
#define WIFI_FREQUENCY_BAND_5GHZ 1
#define WIFI_FREQUENCY_BAND_2GHZ 2

#define rtw_band_valid(band) ((band) <= WIFI_FREQUENCY_BAND_2GHZ)

enum DriverInterface {
	DRIVER_WEXT =  1,
	DRIVER_CFG80211 = 2
};

enum SCAN_RESULT_TYPE {
	SCAN_RESULT_P2P_ONLY = 0,		/*	Will return all the P2P devices. */
	SCAN_RESULT_ALL = 1,			/*	Will return all the scanned device, include AP. */
	SCAN_RESULT_WFD_TYPE = 2		/*	Will just return the correct WFD device. */
									/*	If this device is Miracast sink device, it will just return all the Miracast source devices. */
};

/*

there are several "locks" in mlme_priv,
since mlme_priv is a shared resource between many threads,
like ISR/Call-Back functions, the OID handlers, and even timer functions.


Each _queue has its own locks, already.
Other items are protected by mlme_priv.lock.

To avoid possible dead lock, any thread trying to modifiying mlme_priv
SHALL not lock up more than one locks at a time!

*/


#define traffic_threshold	10
#define	traffic_scan_period	500

typedef struct _RT_LINK_DETECT_T {
	u32				NumTxOkInPeriod;
	u32				NumRxOkInPeriod;
	u32				NumRxUnicastOkInPeriod;
	BOOLEAN			bBusyTraffic;
	BOOLEAN			bTxBusyTraffic;
	BOOLEAN			bRxBusyTraffic;
	BOOLEAN			bHigherBusyTraffic; /* For interrupt migration purpose. */
	BOOLEAN			bHigherBusyRxTraffic; /* We may disable Tx interrupt according as Rx traffic. */
	BOOLEAN			bHigherBusyTxTraffic; /* We may disable Tx interrupt according as Tx traffic. */
	/* u8 TrafficBusyState; */
	u8 TrafficTransitionCount;
	u32 LowPowerTransitionCount;
} RT_LINK_DETECT_T, *PRT_LINK_DETECT_T;

struct profile_info {
	u8	ssidlen;
	u8	ssid[WLAN_SSID_MAXLEN];
	u8	peermac[ETH_ALEN];
};

struct tx_invite_req_info {
	u8					token;
	u8					benable;
	u8					go_ssid[WLAN_SSID_MAXLEN];
	u8					ssidlen;
	u8					go_bssid[ETH_ALEN];
	u8					peer_macaddr[ETH_ALEN];
	u8					operating_ch;	/*	This information will be set by using the p2p_set op_ch=x */
	u8					peer_ch;		/*	The listen channel for peer P2P device */

};

struct tx_invite_resp_info {
	u8					token;	/*	Used to record the dialog token of p2p invitation request frame. */
};

#ifdef CONFIG_WFD

struct wifi_display_info {
	u16							wfd_enable;			/*	Eanble/Disable the WFD function. */
	u16							init_rtsp_ctrlport;	/* init value of rtsp_ctrlport when WFD enable */
	u16							rtsp_ctrlport;		/* TCP port number at which the this WFD device listens for RTSP messages, 0 when WFD disable */
	u16							tdls_rtsp_ctrlport;	/* rtsp_ctrlport used by tdls, will sync when rtsp_ctrlport is changed by user */
	u16							peer_rtsp_ctrlport;	/*	TCP port number at which the peer WFD device listens for RTSP messages */
													/*	This filed should be filled when receiving the gropu negotiation request */

	u8							peer_session_avail;	/*	WFD session is available or not for the peer wfd device. */
													/*	This variable will be set when sending the provisioning discovery request to peer WFD device. */
													/*	And this variable will be reset when it is read by using the iwpriv p2p_get wfd_sa command. */
	u8							ip_address[4];
	u8							peer_ip_address[4];
	u8							wfd_pc;				/*	WFD preferred connection */
													/*	0 -> Prefer to use the P2P for WFD connection on peer side. */
													/*	1 -> Prefer to use the TDLS for WFD connection on peer side. */

	u8							wfd_device_type;	/*	WFD Device Type */
													/*	0 -> WFD Source Device */
													/*	1 -> WFD Primary Sink Device */
	enum	SCAN_RESULT_TYPE	scan_result_type;	/*	Used when P2P is enable. This parameter will impact the scan result. */
	u8 op_wfd_mode;
	u8 stack_wfd_mode;
};
#endif /* CONFIG_WFD */

struct tx_provdisc_req_info {
	u16					wps_config_method_request;	/*	Used when sending the provisioning request frame */
	u16					peer_channel_num[2];		/*	The channel number which the receiver stands. */
	NDIS_802_11_SSID	ssid;
	u8					peerDevAddr[ETH_ALEN];		/*	Peer device address */
	u8					peerIFAddr[ETH_ALEN];		/*	Peer interface address */
	u8					benable;					/*	This provision discovery request frame is trigger to send or not */
};

struct rx_provdisc_req_info {	/* When peer device issue prov_disc_req first, we should store the following informations */
	u8					peerDevAddr[ETH_ALEN];		/*	Peer device address */
	u8					strconfig_method_desc_of_prov_disc_req[4];	/*	description for the config method located in the provisioning discovery request frame.	 */
																	/*	The UI must know this information to know which config method the remote p2p device is requiring. */
};

struct tx_nego_req_info {
	u16					peer_channel_num[2];		/*	The channel number which the receiver stands. */
	u8					peerDevAddr[ETH_ALEN];		/*	Peer device address */
	u8					benable;					/*	This negoitation request frame is trigger to send or not */
	u8					peer_ch;					/*	The listen channel for peer P2P device */
};

struct group_id_info {
	u8					go_device_addr[ETH_ALEN];	/*	The GO's device address of this P2P group */
	u8					ssid[WLAN_SSID_MAXLEN];		/*	The SSID of this P2P group */
};

struct scan_limit_info {
	u8					scan_op_ch_only;			/*	When this flag is set, the driver should just scan the operation channel */
#ifndef CONFIG_P2P_OP_CHK_SOCIAL_CH
	u8					operation_ch[2];				/*	Store the operation channel of invitation request frame */
#else
	u8					operation_ch[5];				/*	Store additional channel 1,6,11  for Android 4.2 IOT & Nexus 4 */
#endif /* CONFIG_P2P_OP_CHK_SOCIAL_CH */
};

#ifdef CONFIG_P2P_WOWLAN

enum P2P_WOWLAN_RECV_FRAME_TYPE {
	P2P_WOWLAN_RECV_NEGO_REQ = 0,
	P2P_WOWLAN_RECV_INVITE_REQ = 1,
	P2P_WOWLAN_RECV_PROVISION_REQ = 2,
};

struct p2p_wowlan_info {

	u8						is_trigger;
	enum P2P_WOWLAN_RECV_FRAME_TYPE	wowlan_recv_frame_type;
	u8						wowlan_peer_addr[ETH_ALEN];
	u16						wowlan_peer_wpsconfig;
	u8						wowlan_peer_is_persistent;
	u8						wowlan_peer_invitation_type;
};

#endif /* CONFIG_P2P_WOWLAN */

struct wifidirect_info {
	_adapter				*padapter;
	_timer					find_phase_timer;
	_timer					restore_p2p_state_timer;

	/*	Used to do the scanning. After confirming the peer is availalble, the driver transmits the P2P frame to peer. */
	_timer					pre_tx_scan_timer;
	_timer					reset_ch_sitesurvey;
	_timer					reset_ch_sitesurvey2;	/*	Just for resetting the scan limit function by using p2p nego */

	struct tx_provdisc_req_info	tx_prov_disc_info;
	struct rx_provdisc_req_info rx_prov_disc_info;
	struct tx_invite_req_info	invitereq_info;
	struct profile_info			profileinfo[P2P_MAX_PERSISTENT_GROUP_NUM];	/*	Store the profile information of persistent group */
	struct tx_invite_resp_info	inviteresp_info;
	struct tx_nego_req_info	nego_req_info;
	struct group_id_info		groupid_info;	/*	Store the group id information when doing the group negotiation handshake. */
	struct scan_limit_info		rx_invitereq_info;	/*	Used for get the limit scan channel from the Invitation procedure */
	struct scan_limit_info		p2p_info;		/*	Used for get the limit scan channel from the P2P negotiation handshake */
#ifdef CONFIG_WFD
	struct wifi_display_info		*wfd_info;
#endif

#ifdef CONFIG_P2P_WOWLAN
	struct p2p_wowlan_info		p2p_wow_info;
#endif /* CONFIG_P2P_WOWLAN */

	enum P2P_ROLE			role;
	enum P2P_STATE			pre_p2p_state;
	enum P2P_STATE			p2p_state;
	u8						device_addr[ETH_ALEN];	/*	The device address should be the mac address of this device. */
	u8						interface_addr[ETH_ALEN];
	u8						social_chan[4];
	u8						listen_channel;
	u8						operating_channel;
	u8						listen_dwell;		/*	This value should be between 1 and 3 */
	u8						support_rate[8];
	u8						p2p_wildcard_ssid[P2P_WILDCARD_SSID_LEN];
	u8						intent;		/*	should only include the intent value. */
	u8						p2p_peer_interface_addr[ETH_ALEN];
	u8						p2p_peer_device_addr[ETH_ALEN];
	u8						peer_intent;	/*	Included the intent value and tie breaker value. */
	u8						device_name[WPS_MAX_DEVICE_NAME_LEN];	/*	Device name for displaying on searching device screen */
	u16						device_name_len;
	u8						profileindex;	/*	Used to point to the index of profileinfo array */
	u8						peer_operating_ch;
	u8						find_phase_state_exchange_cnt;
	u16						device_password_id_for_nego;	/*	The device password ID for group negotation */
	u8						negotiation_dialog_token;
	u8						nego_ssid[WLAN_SSID_MAXLEN];	/*	SSID information for group negotitation */
	u8						nego_ssidlen;
	u8						p2p_group_ssid[WLAN_SSID_MAXLEN];
	u8						p2p_group_ssid_len;
	u8						persistent_supported;		/*	Flag to know the persistent function should be supported or not. */
														/*	In the Sigma test, the Sigma will provide this enable from the sta_set_p2p CAPI. */
														/*	0: disable */
														/*	1: enable */
	u8						session_available;			/*	Flag to set the WFD session available to enable or disable "by Sigma" */
														/*	In the Sigma test, the Sigma will disable the session available by using the sta_preset CAPI. */
														/*	0: disable */
														/*	1: enable */

	u8						wfd_tdls_enable;			/*	Flag to enable or disable the TDLS by WFD Sigma */
														/*	0: disable */
														/*	1: enable */
	u8						wfd_tdls_weaksec;			/*	Flag to enable or disable the weak security function for TDLS by WFD Sigma */
														/*	0: disable */
														/*	In this case, the driver can't issue the tdsl setup request frame. */
														/*	1: enable */
														/*	In this case, the driver can issue the tdls setup request frame */
														/*	even the current security is weak security. */

	enum	P2P_WPSINFO		ui_got_wps_info;			/*	This field will store the WPS value (PIN value or PBC) that UI had got from the user. */
	u16						supported_wps_cm;			/*	This field describes the WPS config method which this driver supported. */
														/*	The value should be the combination of config method defined in page104 of WPS v2.0 spec.	 */
	u8						external_uuid;				/* UUID flag */
	u8						uuid[16];					/* UUID */
	uint						channel_list_attr_len;	/*	This field will contain the length of body of P2P Channel List attribute of group negotitation response frame. */
	u8						channel_list_attr[100];		/*	This field will contain the body of P2P Channel List attribute of group negotitation response frame. */
														/*	We will use the channel_cnt and channel_list fields when constructing the group negotitation confirm frame. */
	u8						driver_interface;			/*	Indicate DRIVER_WEXT or DRIVER_CFG80211 */

#ifdef CONFIG_CONCURRENT_MODE
	u16						ext_listen_interval;	/*	The interval to be available with legacy AP (ms) */
	u16						ext_listen_period;	/*	The time period to be available for P2P listen state (ms) */
#endif
#ifdef CONFIG_P2P_PS
	enum P2P_PS_MODE		p2p_ps_mode; /* indicate p2p ps mode */
	enum P2P_PS_STATE		p2p_ps_state; /* indicate p2p ps state */
	u8						noa_index; /* Identifies and instance of Notice of Absence timing. */
	u8						ctwindow; /* Client traffic window. A period of time in TU after TBTT. */
	u8						opp_ps; /* opportunistic power save. */
	u8						noa_num; /* number of NoA descriptor in P2P IE. */
	u8						noa_count[P2P_MAX_NOA_NUM]; /* Count for owner, Type of client. */
	u32						noa_duration[P2P_MAX_NOA_NUM]; /* Max duration for owner, preferred or min acceptable duration for client. */
	u32						noa_interval[P2P_MAX_NOA_NUM]; /* Length of interval for owner, preferred or max acceptable interval of client. */
	u32						noa_start_time[P2P_MAX_NOA_NUM]; /* schedule expressed in terms of the lower 4 bytes of the TSF timer. */
#endif /* CONFIG_P2P_PS */
};

struct tdls_ss_record {	/* signal strength record */
	u8		macaddr[ETH_ALEN];
	u8		RxPWDBAll;
	u8		is_tdls_sta;	/* _TRUE: direct link sta, _FALSE: else */
};

struct tdls_temp_mgmt {
	u8	initiator;	/* 0: None, 1: we initiate, 2: peer initiate */
	u8	peer_addr[ETH_ALEN];
};

#ifdef CONFIG_TDLS_CH_SW
struct tdls_ch_switch {
	u32	ch_sw_state;
	ATOMIC_T	chsw_on;
	u8	addr[ETH_ALEN];
	u8	off_ch_num;
	u8	ch_offset;
	u8	bcn_early_reg_bkp;
	u32	cur_time;
	u8	delay_switch_back;
	u8	dump_stack;
	struct submit_ctx	chsw_sctx;
};
#endif

struct tdls_info {
	u8					ap_prohibited;
	u8					ch_switch_prohibited;
	u8					link_established;
	u8					sta_cnt;
	u8					sta_maximum;	/* 1:tdls sta is equal (NUM_STA-1), reach max direct link number; 0: else; */
	struct tdls_ss_record	ss_record;
#ifdef CONFIG_TDLS_CH_SW
	struct tdls_ch_switch	chsw_info;
#endif

	u8					ch_sensing;
	u8					cur_channel;
	u8					collect_pkt_num[MAX_CHANNEL_NUM];
	_lock				cmd_lock;
	_lock				hdl_lock;
	u8					watchdog_count;
	u8					dev_discovered;		/* WFD_TDLS: for sigma test */

	/* Let wpa_supplicant to setup*/
	u8					driver_setup;
#ifdef CONFIG_WFD
	struct wifi_display_info		*wfd_info;
#endif

	struct submit_ctx	*tdls_sctx;
};

struct tdls_txmgmt {
	u8 peer[ETH_ALEN];
	u8 action_code;
	u8 dialog_token;
	u16 status_code;
	u8 *buf;
	size_t len;
};

/* used for mlme_priv.roam_flags */
enum {
	RTW_ROAM_ON_EXPIRED = BIT0,
	RTW_ROAM_ON_RESUME = BIT1,
	RTW_ROAM_ACTIVE = BIT2,
};

#define UNASOC_STA_SRC_RX_BMC		0
#define UNASOC_STA_SRC_RX_NMY_UC	1
#define UNASOC_STA_SRC_NUM			2

#define UNASOC_STA_MODE_DISABLED	0
#define UNASOC_STA_MODE_INTERESTED	1
#define UNASOC_STA_MODE_ALL			2
#define UNASOC_STA_MODE_NUM			3

#define UNASOC_STA_DEL_CHK_SKIP		0
#define UNASOC_STA_DEL_CHK_ALIVE	1
#define UNASOC_STA_DEL_CHK_DELETED	2

#ifdef CONFIG_RTW_MULTI_AP
struct unassoc_sta_info {
	_list list;
	u8 addr[ETH_ALEN];
	u8 interested;
	s8 recv_signal_power;
	systime time;
};
#endif

struct mlme_priv {

	_lock	lock;
	sint	fw_state;	/* shall we protect this variable? maybe not necessarily... */
	u8	to_join; /* flag */
	u16 join_status;
#ifdef CONFIG_LAYER2_ROAMING
	u8 to_roam; /* roaming trying times */
	struct wlan_network *roam_network; /* the target of active roam */
	u8 roam_flags;
	u8 roam_rssi_diff_th; /* rssi difference threshold for active scan candidate selection */
	u32 roam_scan_int; 		/* scan interval for active roam (Unit:2 second)*/
	u32 roam_scanr_exp_ms; /* scan result expire time in ms  for roam */
	u8 roam_tgt_addr[ETH_ALEN]; /* request to roam to speicific target without other consideration */
	u8 roam_rssi_threshold;
	systime last_roaming;
	bool need_to_roam;
#endif

	u32 defs_lmt_sta;
	u32 defs_lmt_time;

	u8	*nic_hdl;
	u32	max_bss_cnt;		/*	The size of scan queue	*/
	_list		*pscanned;
	_queue	free_bss_pool;
	_queue	scanned_queue;
	u8		*free_bss_buf;
	u32	num_of_scanned;

	NDIS_802_11_SSID	assoc_ssid;
	u8	assoc_bssid[6];
	u16	assoc_ch;		/* 0 reserved for no specific channel */

	struct wlan_network	cur_network;
	struct wlan_network *cur_network_scanned;

	/* bcn check info */
	struct beacon_keys cur_beacon_keys; /* save current beacon keys */
#ifdef CONFIG_BCN_CNT_CONFIRM_HDL
	struct beacon_keys new_beacon_keys; /* save new beacon keys */
	u8 new_beacon_cnts; /* if new_beacon_cnts >= threshold, ap beacon is changed */
#endif

#ifdef CONFIG_ARP_KEEP_ALIVE
	/* for arp offload keep alive */
	u8 bGetGateway;
	u8	GetGatewayTryCnt;
	u8	gw_mac_addr[ETH_ALEN];
	u8	gw_ip[4];
#endif

	/* uint wireless_mode; no used, remove it */

	u32	auto_scan_int_ms;

	_timer assoc_timer;

	uint assoc_by_bssid;
	uint assoc_by_rssi;

	_timer scan_to_timer; /* driver itself handles scan_timeout status. */
	systime scan_start_time; /* used to evaluate the time spent in scanning */

#ifdef CONFIG_SET_SCAN_DENY_TIMER
	_timer set_scan_deny_timer;
	ATOMIC_T set_scan_deny; /* 0: allowed, 1: deny */
#endif
	u8 wpa_phase;/*wpa_phase after wps finished*/

	struct qos_priv qospriv;

#ifdef CONFIG_80211N_HT

	/* Number of non-HT AP/stations */
	int num_sta_no_ht;

	/* Number of HT AP/stations 20 MHz */
	/* int num_sta_ht_20mhz; */


	int num_FortyMHzIntolerant;

	struct ht_priv	htpriv;

#endif

#ifdef CONFIG_80211AC_VHT
	struct vht_priv	vhtpriv;
#ifdef ROKU_PRIVATE
	/*infra mode, used to store AP's info*/
	struct vht_priv_infra_ap vhtpriv_infra_ap;
#endif /* ROKU_PRIVATE */
#endif

#ifdef ROKU_PRIVATE
	struct ht_priv_infra_ap htpriv_infra_ap;
#endif /* ROKU_PRIVATE */

#ifdef CONFIG_RTW_80211R
	struct ft_roam_info ft_roam;
#endif
#if defined(CONFIG_RTW_WNM) || defined(CONFIG_RTW_80211K)
	struct roam_nb_info nb_info;
	u8 ch_cnt;
#endif

	RT_LINK_DETECT_T	LinkDetectInfo;

	u8	acm_mask; /* for wmm acm mask */
	RT_SCAN_TYPE	scan_mode; /* active: 1, passive: 0 */

	u8 *wps_probe_req_ie;
	u32 wps_probe_req_ie_len;

	u8 ext_capab_ie_data[8];/*currently for ap mode only*/
	u8 ext_capab_ie_len;

#if defined(CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	/* Number of associated Non-ERP stations (i.e., stations using 802.11b
	 * in 802.11g BSS) */
	int num_sta_non_erp;

	/* Number of associated stations that do not support Short Slot Time */
	int num_sta_no_short_slot_time;

	/* Number of associated stations that do not support Short Preamble */
	int num_sta_no_short_preamble;

	ATOMIC_T olbc; /* Overlapping Legacy BSS Condition (Legacy b/g)*/

	/* Number of HT associated stations that do not support greenfield */
	int num_sta_ht_no_gf;

	/* Number of associated non-HT stations */
	/* int num_sta_no_ht; */

	/* Number of HT associated stations 20 MHz */
	int num_sta_ht_20mhz;

	/* number of associated stations 40MHz intolerant */
	int num_sta_40mhz_intolerant;

	/* Overlapping BSS information */
	ATOMIC_T olbc_ht;

#ifdef CONFIG_80211N_HT
	int ht_20mhz_width_req;
	int ht_intolerant_ch_reported;
	u16 ht_op_mode;
	u8 sw_to_20mhz; /*switch to 20Mhz BW*/
#endif /* CONFIG_80211N_HT */

#ifdef CONFIG_RTW_80211R
	u8 *auth_rsp;
	u32 auth_rsp_len;
#endif
#endif /* CONFIG_AP_MODE and CONFIG_NATIVEAP_MLME */

	u8 *assoc_req;
	u32 assoc_req_len;
	u8 *assoc_rsp;
	u32 assoc_rsp_len;

#if defined(CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	/* u8 *wps_probe_req_ie; */
	/* u32 wps_probe_req_ie_len; */

	u8 *wps_beacon_ie;
	u32 wps_beacon_ie_len;

	u8 *wps_probe_resp_ie;
	u32 wps_probe_resp_ie_len;

	u8 *wps_assoc_resp_ie;
	u32 wps_assoc_resp_ie_len;

	u8 *p2p_beacon_ie;
	u32 p2p_beacon_ie_len;

	u8 *p2p_probe_req_ie;
	u32 p2p_probe_req_ie_len;

	u8 *p2p_probe_resp_ie;
	u32 p2p_probe_resp_ie_len;

	u8 *p2p_go_probe_resp_ie;		/* for GO */
	u32 p2p_go_probe_resp_ie_len;	/* for GO */

	u8 *p2p_assoc_req_ie;
	u32 p2p_assoc_req_ie_len;

	u8 *p2p_assoc_resp_ie;
	u32 p2p_assoc_resp_ie_len;

	_lock	bcn_update_lock;
	u8		update_bcn;

	u8 ori_ch;
	u8 ori_bw;
	u8 ori_offset;
	#ifdef CONFIG_80211AC_VHT
	u8 ori_vht_en;
	#endif

	u8 ap_isolate;
#endif /* #if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME) */

#if defined(CONFIG_WFD) && defined(CONFIG_IOCTL_CFG80211)
	u8 *wfd_beacon_ie;
	u32 wfd_beacon_ie_len;

	u8 *wfd_probe_req_ie;
	u32 wfd_probe_req_ie_len;

	u8 *wfd_probe_resp_ie;
	u32 wfd_probe_resp_ie_len;

	u8 *wfd_go_probe_resp_ie;		/* for GO */
	u32 wfd_go_probe_resp_ie_len;	/* for GO */

	u8 *wfd_assoc_req_ie;
	u32 wfd_assoc_req_ie_len;

	u8 *wfd_assoc_resp_ie;
	u32 wfd_assoc_resp_ie_len;
#endif

#ifdef CONFIG_RTW_MBO
	u8 *pcell_data_cap_ie;
	u32 cell_data_cap_len;
#endif

#ifdef RTK_DMP_PLATFORM
	/* DMP kobject_hotplug function  signal need in passive level */
	_workitem	Linkup_workitem;
	_workitem	Linkdown_workitem;
#endif

#ifdef RTW_BUSY_DENY_SCAN
	systime lastscantime;
#endif

#ifdef CONFIG_CONCURRENT_MODE
	u8	scanning_via_buddy_intf;
#endif

#ifdef CONFIG_APPEND_VENDOR_IE_ENABLE
	u32 vendor_ie_mask[WLAN_MAX_VENDOR_IE_NUM];
	u8 vendor_ie[WLAN_MAX_VENDOR_IE_NUM][WLAN_MAX_VENDOR_IE_LEN];
	u32 vendor_ielen[WLAN_MAX_VENDOR_IE_NUM];
#endif
#ifdef CONFIG_RTW_MULTI_AP
	u8 unassoc_sta_mode_of_stype[UNASOC_STA_SRC_NUM];
	_queue unassoc_sta_queue;
	_queue free_unassoc_sta_queue;
	u8 *free_unassoc_sta_buf;
	u32 interested_unassoc_sta_cnt;
	u32 max_unassoc_sta_cnt;
#ifdef CONFIG_PLATFORM_CMAP_INTFS
	struct unassoc_sta_info cmap_unassoc_sta[CMAP_UNASSOC_METRICS_STA_MAX];
	u8 cmap_unassoc_sta_cnt;
	_timer cmap_unassoc_sta_timer;
#endif
#endif
};

#define mlme_set_scan_to_timer(mlme, ms) \
	do { \
		/* RTW_INFO("%s set_scan_to_timer(%p, %d)\n", __FUNCTION__, (mlme), (ms)); */ \
		_set_timer(&(mlme)->scan_to_timer, (ms)); \
	} while (0)

#define rtw_mlme_set_auto_scan_int(adapter, ms) \
	do { \
		adapter->mlmepriv.auto_scan_int_ms = ms; \
	} while (0)

#define RTW_AUTO_SCAN_REASON_UNSPECIFIED		0
#define RTW_AUTO_SCAN_REASON_2040_BSS			BIT0
#define RTW_AUTO_SCAN_REASON_ACS				BIT1
#define RTW_AUTO_SCAN_REASON_ROAM				BIT2
#define RTW_AUTO_SCAN_REASON_MESH_OFFCH_CAND	BIT3

void rtw_mlme_reset_auto_scan_int(_adapter *adapter, u8 *reason);

#ifdef CONFIG_AP_MODE

struct hostapd_priv {
	_adapter *padapter;

#ifdef CONFIG_HOSTAPD_MLME
	struct net_device *pmgnt_netdev;
	struct usb_anchor anchored;
#endif

};

extern int hostapd_mode_init(_adapter *padapter);
extern void hostapd_mode_unload(_adapter *padapter);
#endif


extern void rtw_joinbss_event_prehandle(_adapter *adapter, u8 *pbuf, u16 status);
extern void rtw_survey_event_callback(_adapter *adapter, u8 *pbuf);
extern void rtw_surveydone_event_callback(_adapter *adapter, u8 *pbuf);
extern void rtw_joinbss_event_callback(_adapter *adapter, u8 *pbuf);
extern void rtw_stassoc_event_callback(_adapter *adapter, u8 *pbuf);
extern void rtw_stadel_event_callback(_adapter *adapter, u8 *pbuf);
void rtw_sta_mstatus_disc_rpt(_adapter *adapter, u8 mac_id);
void rtw_sta_mstatus_report(_adapter *adapter);
extern void rtw_wmm_event_callback(PADAPTER padapter, u8 *pbuf);
#ifdef CONFIG_IEEE80211W
void rtw_sta_timeout_event_callback(_adapter *adapter, u8 *pbuf);
#endif /* CONFIG_IEEE80211W */
thread_return event_thread(thread_context context);

extern void rtw_free_network_queue(_adapter *adapter, u8 isfreeall);
extern int rtw_init_mlme_priv(_adapter *adapter);/* (struct mlme_priv *pmlmepriv); */

extern void rtw_free_mlme_priv(struct mlme_priv *pmlmepriv);


extern sint rtw_select_and_join_from_scanned_queue(struct mlme_priv *pmlmepriv);
extern sint rtw_set_key(_adapter *adapter, struct security_priv *psecuritypriv, sint keyid, u8 set_tx, bool enqueue);
extern sint rtw_set_auth(_adapter *adapter, struct security_priv *psecuritypriv);

__inline static u8 *get_bssid(struct mlme_priv *pmlmepriv)
{
	/* if sta_mode:pmlmepriv->cur_network.network.MacAddress=> bssid */
	/* if adhoc_mode:pmlmepriv->cur_network.network.MacAddress=> ibss mac address */
	return pmlmepriv->cur_network.network.MacAddress;
}

__inline static sint check_fwstate(struct mlme_priv *pmlmepriv, sint state)
{
	if ((state == WIFI_NULL_STATE) &&
		(pmlmepriv->fw_state == WIFI_NULL_STATE))
		return _TRUE;

	if (pmlmepriv->fw_state & state)
		return _TRUE;

	return _FALSE;
}

__inline static sint get_fwstate(struct mlme_priv *pmlmepriv)
{
	return pmlmepriv->fw_state;
}

/*
 * No Limit on the calling context,
 * therefore set it to be the critical section...
 *
 * ### NOTE:#### (!!!!)
 * MUST TAKE CARE THAT BEFORE CALLING THIS FUNC, YOU SHOULD HAVE LOCKED pmlmepriv->lock
 */
extern void rtw_mi_update_iface_status(struct mlme_priv *pmlmepriv, sint state);

static inline void set_fwstate(struct mlme_priv *pmlmepriv, sint state)
{
	pmlmepriv->fw_state |= state;
	rtw_mi_update_iface_status(pmlmepriv, state);
}
static inline void init_fwstate(struct mlme_priv *pmlmepriv, sint state)
{
	pmlmepriv->fw_state = state;
	rtw_mi_update_iface_status(pmlmepriv, state);
}

static inline void _clr_fwstate_(struct mlme_priv *pmlmepriv, sint state)
{
	pmlmepriv->fw_state &= ~state;
	rtw_mi_update_iface_status(pmlmepriv, state);
}

/*
 * No Limit on the calling context,
 * therefore set it to be the critical section...
 */
static inline void clr_fwstate(struct mlme_priv *pmlmepriv, sint state)
{
	_irqL irqL;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	_clr_fwstate_(pmlmepriv, state);
	_exit_critical_bh(&pmlmepriv->lock, &irqL);
}

static inline void up_scanned_network(struct mlme_priv *pmlmepriv)
{
	_irqL irqL;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	pmlmepriv->num_of_scanned++;
	_exit_critical_bh(&pmlmepriv->lock, &irqL);
}
u8 rtw_is_adapter_up(_adapter *padapter);

__inline static void down_scanned_network(struct mlme_priv *pmlmepriv)
{
	_irqL irqL;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	pmlmepriv->num_of_scanned--;
	_exit_critical_bh(&pmlmepriv->lock, &irqL);
}

__inline static void set_scanned_network_val(struct mlme_priv *pmlmepriv, sint val)
{
	_irqL irqL;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	pmlmepriv->num_of_scanned = val;
	_exit_critical_bh(&pmlmepriv->lock, &irqL);
}

extern u16 rtw_get_capability(WLAN_BSSID_EX *bss);
extern bool rtw_update_scanned_network(_adapter *adapter, WLAN_BSSID_EX *target);
extern void rtw_disconnect_hdl_under_linked(_adapter *adapter, struct sta_info *psta, u8 free_assoc);
extern void rtw_generate_random_ibss(u8 *pibss);
struct wlan_network *_rtw_find_network(_queue *scanned_queue, const u8 *addr);
struct wlan_network *rtw_find_network(_queue *scanned_queue, const u8 *addr);
extern struct wlan_network *rtw_get_oldest_wlan_network(_queue *scanned_queue);
struct wlan_network *_rtw_find_same_network(_queue *scanned_queue, struct wlan_network *network);
struct wlan_network *rtw_find_same_network(_queue *scanned_queue, struct wlan_network *network);

extern void rtw_free_assoc_resources(_adapter *adapter, u8 lock_scanned_queue);
extern void rtw_indicate_disconnect(_adapter *adapter, u16 reason, u8 locally_generated);
extern void rtw_indicate_connect(_adapter *adapter);
void rtw_indicate_scan_done(_adapter *padapter, bool aborted);

void rtw_drv_scan_by_self(_adapter *padapter, u8 reason);
void rtw_scan_wait_completed(_adapter *adapter);
u32 rtw_scan_abort_timeout(_adapter *adapter, u32 timeout_ms);
void rtw_scan_abort_no_wait(_adapter *adapter);
void rtw_scan_abort(_adapter *adapter);
u32 rtw_join_abort_timeout(_adapter *adapter, u32 timeout_ms);

int rtw_cached_pmkid(_adapter *Adapter, u8 *bssid);
int rtw_rsn_sync_pmkid(_adapter *adapter, u8 *ie, uint ie_len, int i_ent);

extern int rtw_restruct_sec_ie(_adapter *adapter, u8 *out_ie);
#ifdef CONFIG_WMMPS_STA
void rtw_uapsd_use_default_setting(_adapter *padapter);
bool rtw_is_wmmps_mode(_adapter *padapter);
#endif /* CONFIG_WMMPS_STA */
extern int rtw_restruct_wmm_ie(_adapter *adapter, u8 *in_ie, u8 *out_ie, uint in_len, uint initial_out_len);
extern void rtw_init_registrypriv_dev_network(_adapter *adapter);

extern void rtw_update_registrypriv_dev_network(_adapter *adapter);

extern void rtw_get_encrypt_decrypt_from_registrypriv(_adapter *adapter);

extern void rtw_join_timeout_handler(void *ctx);
extern void rtw_scan_timeout_handler(void *ctx);

extern void rtw_dynamic_check_timer_handlder(void *ctx);
extern void rtw_iface_dynamic_check_timer_handlder(_adapter *adapter);

enum {
	SS_DENY_MP_MODE,
	SS_DENY_RSON_SCANING,
	SS_DENY_BLOCK_SCAN,
	SS_DENY_BY_DRV,
	SS_DENY_SELF_AP_UNDER_WPS,
	SS_DENY_SELF_AP_UNDER_LINKING,
	SS_DENY_SELF_AP_UNDER_SURVEY,
	/*SS_DENY_SELF_STA_UNDER_WPS,*/
	SS_DENY_SELF_STA_UNDER_LINKING,
	SS_DENY_SELF_STA_UNDER_SURVEY,
	SS_DENY_BUDDY_UNDER_LINK_WPS,
	SS_DENY_BUDDY_UNDER_SURVEY,
	SS_DENY_BUSY_TRAFFIC,
	SS_ALLOW,
#ifdef DBG_LA_MODE
	SS_DENY_LA_MODE,
#endif
	SS_DENY_ADAPTIVITY,
};

u8 _rtw_sitesurvey_condition_check(const char *caller, _adapter *adapter, bool check_sc_interval);
#define rtw_sitesurvey_condition_check(adapter, check_sc_interval) _rtw_sitesurvey_condition_check(__func__, adapter, check_sc_interval)

#ifdef CONFIG_SET_SCAN_DENY_TIMER
bool rtw_is_scan_deny(_adapter *adapter);
void rtw_clear_scan_deny(_adapter *adapter);
void rtw_set_scan_deny_timer_hdl(void *ctx);
void rtw_set_scan_deny(_adapter *adapter, u32 ms);
#else
#define rtw_is_scan_deny(adapter) _FALSE
#define rtw_clear_scan_deny(adapter) do {} while (0)
#define rtw_set_scan_deny(adapter, ms) do {} while (0)
#endif

void rtw_free_mlme_priv_ie_data(struct mlme_priv *pmlmepriv);

#define MLME_BEACON_IE			0
#define MLME_PROBE_REQ_IE		1
#define MLME_PROBE_RESP_IE		2
#define MLME_GO_PROBE_RESP_IE	3
#define MLME_ASSOC_REQ_IE		4
#define MLME_ASSOC_RESP_IE		5

#if defined(CONFIG_WFD) && defined(CONFIG_IOCTL_CFG80211)
int rtw_mlme_update_wfd_ie_data(struct mlme_priv *mlme, u8 type, u8 *ie, u32 ie_len);
#endif


/* extern struct wlan_network* _rtw_dequeue_network(_queue *queue); */

extern struct wlan_network *_rtw_alloc_network(struct mlme_priv *pmlmepriv);


extern void _rtw_free_network(struct mlme_priv *pmlmepriv, struct wlan_network *pnetwork, u8 isfreeall);
extern void _rtw_free_network_nolock(struct mlme_priv *pmlmepriv, struct wlan_network *pnetwork);

extern void _rtw_free_network_queue(_adapter *padapter, u8 isfreeall);

extern sint rtw_if_up(_adapter *padapter);

sint rtw_linked_check(_adapter *padapter);

u8 *rtw_get_capability_from_ie(u8 *ie);
u8 *rtw_get_timestampe_from_ie(u8 *ie);
u8 *rtw_get_beacon_interval_from_ie(u8 *ie);


void rtw_joinbss_reset(_adapter *padapter);

#ifdef CONFIG_80211N_HT
void	rtw_ht_use_default_setting(_adapter *padapter);
void rtw_build_wmm_ie_ht(_adapter *padapter, u8 *out_ie, uint *pout_len);
unsigned int rtw_restructure_ht_ie(_adapter *padapter, u8 *in_ie, u8 *out_ie, uint in_len, uint *pout_len, u8 channel);
void rtw_update_ht_cap(_adapter *padapter, u8 *pie, uint ie_len, u8 channel);
void rtw_issue_addbareq_cmd(_adapter *padapter, struct xmit_frame *pxmitframe, u8 issue_when_busy);
void rtw_append_exented_cap(_adapter *padapter, u8 *out_ie, uint *pout_len);
#endif

int rtw_is_same_ibss(_adapter *adapter, struct wlan_network *pnetwork);
int is_same_network(WLAN_BSSID_EX *src, WLAN_BSSID_EX *dst, u8 feature);

#ifdef CONFIG_LAYER2_ROAMING
#define rtw_roam_flags(adapter) ((adapter)->mlmepriv.roam_flags)
#define rtw_chk_roam_flags(adapter, flags) ((adapter)->mlmepriv.roam_flags & flags)
#define rtw_clr_roam_flags(adapter, flags) \
	do { \
		((adapter)->mlmepriv.roam_flags &= ~flags); \
	} while (0)

#define rtw_set_roam_flags(adapter, flags) \
	do { \
		((adapter)->mlmepriv.roam_flags |= flags); \
	} while (0)

#define rtw_assign_roam_flags(adapter, flags) \
	do { \
		((adapter)->mlmepriv.roam_flags = flags); \
	} while (0)

void _rtw_roaming(_adapter *adapter, struct wlan_network *tgt_network);
void rtw_roaming(_adapter *adapter, struct wlan_network *tgt_network);
void rtw_set_to_roam(_adapter *adapter, u8 to_roam);
u8 rtw_dec_to_roam(_adapter *adapter);
u8 rtw_to_roam(_adapter *adapter);
int rtw_select_roaming_candidate(struct mlme_priv *pmlmepriv);
#else
#define rtw_roam_flags(adapter) 0
#define rtw_chk_roam_flags(adapter, flags) 0
#define rtw_clr_roam_flags(adapter, flags) do {} while (0)
#define rtw_set_roam_flags(adapter, flags) do {} while (0)
#define rtw_assign_roam_flags(adapter, flags) do {} while (0)
#define _rtw_roaming(adapter, tgt_network) do {} while (0)
#define rtw_roaming(adapter, tgt_network) do {} while (0)
#define rtw_set_to_roam(adapter, to_roam) do {} while (0)
#define rtw_dec_to_roam(adapter) 0
#define rtw_to_roam(adapter) 0
#define rtw_select_roaming_candidate(mlme) _FAIL
#endif /* CONFIG_LAYER2_ROAMING */

bool rtw_adjust_chbw(_adapter *adapter, u8 req_ch, u8 *req_bw, u8 *req_offset);

struct sta_media_status_rpt_cmd_parm {
	struct sta_info *sta;
	bool connected;
};

#ifdef CONFIG_RTW_MULTI_AP
void rtw_unassoc_sta_set_mode(_adapter *adapter, u8 stype, u8 mode);
bool rtw_unassoc_sta_src_chk(_adapter *adapter, u8 stype);
void dump_unassoc_sta(void *sel, _adapter *adapter);
void rtw_del_unassoc_sta_queue(_adapter *adapter);
void rtw_del_unassoc_sta(_adapter *adapter, u8 *addr);
void rtw_rx_add_unassoc_sta(_adapter *adapter, u8 stype, u8 *addr, s8 recv_signal_power);
void rtw_add_interested_unassoc_sta(_adapter *adapter, u8 *addr);
void rtw_undo_interested_unassoc_sta(_adapter *adapter, u8 *addr);
void rtw_undo_all_interested_unassoc_sta(_adapter *adapter);
u8 rtw_search_unassoc_sta(_adapter *adapter, u8 *addr, struct unassoc_sta_info *ret_sta);
#endif

void rtw_sta_media_status_rpt(_adapter *adapter, struct sta_info *sta, bool connected);
u8 rtw_sta_media_status_rpt_cmd(_adapter *adapter, struct sta_info *sta, bool connected);
void rtw_sta_media_status_rpt_cmd_hdl(_adapter *adapter, struct sta_media_status_rpt_cmd_parm *parm);
void rtw_sta_traffic_info(void *sel, _adapter *adapter);

#define GET_ARP_HTYPE(_arp)	BE_BITS_TO_2BYTE(((u8 *)(_arp)) + 0, 0, 16)
#define GET_ARP_PTYPE(_arp)	BE_BITS_TO_2BYTE(((u8 *)(_arp)) + 2, 0, 16)
#define GET_ARP_HLEN(_arp)	BE_BITS_TO_1BYTE(((u8 *)(_arp)) + 4, 0, 8)
#define GET_ARP_PLEN(_arp)	BE_BITS_TO_1BYTE(((u8 *)(_arp)) + 5, 0, 8)
#define GET_ARP_OPER(_arp)	BE_BITS_TO_2BYTE(((u8 *)(_arp)) + 6, 0, 16)

#define SET_ARP_HTYPE(_arp, _val)	SET_BITS_TO_BE_2BYTE(((u8 *)(_arp)) + 0, 0, 16, _val)
#define SET_ARP_PTYPE(_arp, _val)	SET_BITS_TO_BE_2BYTE(((u8 *)(_arp)) + 2, 0, 16, _val)
#define SET_ARP_HLEN(_arp, _val)	SET_BITS_TO_BE_1BYTE(((u8 *)(_arp)) + 4, 0, 8, _val)
#define SET_ARP_PLEN(_arp, _val)	SET_BITS_TO_BE_1BYTE(((u8 *)(_arp)) + 5, 0, 8, _val)
#define SET_ARP_OPER(_arp, _val)	SET_BITS_TO_BE_2BYTE(((u8 *)(_arp)) + 6, 0, 16, _val)

#define ARP_SHA(_arp, _hlen, _plen)	(((u8 *)(_arp)) + 8)
#define ARP_SPA(_arp, _hlen, _plen)	(((u8 *)(_arp)) + 8 + (_hlen))
#define ARP_THA(_arp, _hlen, _plen)	(((u8 *)(_arp)) + 8 + (_hlen) + (_plen))
#define ARP_TPA(_arp, _hlen, _plen)	(((u8 *)(_arp)) + 8 + 2 * (_hlen) + (_plen))

#define ARP_SENDER_MAC_ADDR(_arp)	ARP_SHA(_arp, ETH_ALEN, RTW_IP_ADDR_LEN)
#define ARP_SENDER_IP_ADDR(_arp)	ARP_SPA(_arp, ETH_ALEN, RTW_IP_ADDR_LEN)
#define ARP_TARGET_MAC_ADDR(_arp)	ARP_THA(_arp, ETH_ALEN, RTW_IP_ADDR_LEN)
#define ARP_TARGET_IP_ADDR(_arp)	ARP_TPA(_arp, ETH_ALEN, RTW_IP_ADDR_LEN)

#define GET_ARP_SENDER_MAC_ADDR(_arp, _val)	_rtw_memcpy(_val, ARP_SENDER_MAC_ADDR(_arp), ETH_ALEN)
#define GET_ARP_SENDER_IP_ADDR(_arp, _val)	_rtw_memcpy(_val, ARP_SENDER_IP_ADDR(_arp), RTW_IP_ADDR_LEN)
#define GET_ARP_TARGET_MAC_ADDR(_arp, _val)	_rtw_memcpy(_val, ARP_TARGET_MAC_ADDR(_arp), ETH_ALEN)
#define GET_ARP_TARGET_IP_ADDR(_arp, _val)	_rtw_memcpy(_val, ARP_TARGET_IP_ADDR(_arp), RTW_IP_ADDR_LEN)

#define SET_ARP_SENDER_MAC_ADDR(_arp, _val)	_rtw_memcpy(ARP_SENDER_MAC_ADDR(_arp), _val, ETH_ALEN)
#define SET_ARP_SENDER_IP_ADDR(_arp, _val)	_rtw_memcpy(ARP_SENDER_IP_ADDR(_arp), _val, RTW_IP_ADDR_LEN)
#define SET_ARP_TARGET_MAC_ADDR(_arp, _val)	_rtw_memcpy(ARP_TARGET_MAC_ADDR(_arp), _val, ETH_ALEN)
#define SET_ARP_TARGET_IP_ADDR(_arp, _val)	_rtw_memcpy(ARP_TARGET_IP_ADDR(_arp), _val, RTW_IP_ADDR_LEN)

void dump_arp_pkt(void *sel, u8 *da, u8 *sa, u8 *arp, bool tx);

#define IPV4_SRC(_iphdr)			(((u8 *)(_iphdr)) + 12)
#define IPV4_DST(_iphdr)			(((u8 *)(_iphdr)) + 16)
#define GET_IPV4_IHL(_iphdr)		BE_BITS_TO_1BYTE(((u8 *)(_iphdr)) + 0, 0, 4)
#define GET_IPV4_PROTOCOL(_iphdr)	BE_BITS_TO_1BYTE(((u8 *)(_iphdr)) + 9, 0, 8)
#define GET_IPV4_SRC(_iphdr)		BE_BITS_TO_4BYTE(((u8 *)(_iphdr)) + 12, 0, 32)
#define GET_IPV4_DST(_iphdr)		BE_BITS_TO_4BYTE(((u8 *)(_iphdr)) + 16, 0, 32)

#define GET_UDP_SRC(_udphdr)			BE_BITS_TO_2BYTE(((u8 *)(_udphdr)) + 0, 0, 16)
#define GET_UDP_DST(_udphdr)			BE_BITS_TO_2BYTE(((u8 *)(_udphdr)) + 2, 0, 16)
#define GET_UDP_SIG1(_udphdr)			BE_BITS_TO_1BYTE(((u8 *)(_udphdr)) + 8, 0, 8)
#define GET_UDP_SIG2(_udphdr)			BE_BITS_TO_1BYTE(((u8 *)(_udphdr)) + 23, 0, 8)

#define TCP_SRC(_tcphdr)				(((u8 *)(_tcphdr)) + 0)
#define TCP_DST(_tcphdr)				(((u8 *)(_tcphdr)) + 2)
#define GET_TCP_SRC(_tcphdr)			BE_BITS_TO_2BYTE(((u8 *)(_tcphdr)) + 0, 0, 16)
#define GET_TCP_DST(_tcphdr)			BE_BITS_TO_2BYTE(((u8 *)(_tcphdr)) + 2, 0, 16)
#define GET_TCP_SEQ(_tcphdr)			BE_BITS_TO_4BYTE(((u8 *)(_tcphdr)) + 4, 0, 32)
#define GET_TCP_ACK_SEQ(_tcphdr)		BE_BITS_TO_4BYTE(((u8 *)(_tcphdr)) + 8, 0, 32)
#define GET_TCP_DOFF(_tcphdr)			BE_BITS_TO_1BYTE(((u8 *)(_tcphdr)) + 12, 4, 4)
#define GET_TCP_FIN(_tcphdr)			BE_BITS_TO_1BYTE(((u8 *)(_tcphdr)) + 13, 0, 1)
#define GET_TCP_SYN(_tcphdr)			BE_BITS_TO_1BYTE(((u8 *)(_tcphdr)) + 13, 1, 1)
#define GET_TCP_RST(_tcphdr)			BE_BITS_TO_1BYTE(((u8 *)(_tcphdr)) + 13, 2, 1)
#define GET_TCP_PSH(_tcphdr)			BE_BITS_TO_1BYTE(((u8 *)(_tcphdr)) + 13, 3, 1)
#define GET_TCP_ACK(_tcphdr)			BE_BITS_TO_1BYTE(((u8 *)(_tcphdr)) + 13, 4, 1)
#define GET_TCP_URG(_tcphdr)			BE_BITS_TO_1BYTE(((u8 *)(_tcphdr)) + 13, 5, 1)
#define GET_TCP_ECE(_tcphdr)			BE_BITS_TO_1BYTE(((u8 *)(_tcphdr)) + 13, 6, 1)
#define GET_TCP_CWR(_tcphdr)			BE_BITS_TO_1BYTE(((u8 *)(_tcphdr)) + 13, 7, 1)

#endif /* __RTL871X_MLME_H_ */
