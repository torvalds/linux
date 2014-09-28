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
#ifndef __RTW_MLME_H_
#define __RTW_MLME_H_


#define	MAX_BSS_CNT	128
//#define   MAX_JOIN_TIMEOUT	2000
//#define   MAX_JOIN_TIMEOUT	2500
#define   MAX_JOIN_TIMEOUT	6500

//	Commented by Albert 20101105
//	Increase the scanning timeout because of increasing the SURVEY_TO value.

#define 	SCANNING_TIMEOUT 	8000

#ifdef PALTFORM_OS_WINCE
#define	SCANQUEUE_LIFETIME 12000000 // unit:us
#else
#define	SCANQUEUE_LIFETIME 20000 // 20sec, unit:msec
#endif

#define WIFI_NULL_STATE		0x00000000
#define WIFI_ASOC_STATE		0x00000001		// Under Linked state...
#define WIFI_REASOC_STATE	0x00000002
#define WIFI_SLEEP_STATE	0x00000004
#define WIFI_STATION_STATE	0x00000008
#define	WIFI_AP_STATE			0x00000010
#define	WIFI_ADHOC_STATE		0x00000020
#define WIFI_ADHOC_MASTER_STATE	0x00000040
#define WIFI_UNDER_LINKING	0x00000080

#define WIFI_UNDER_WPS			0x00000100
//#define	WIFI_UNDER_CMD			0x00000200
//#define	WIFI_UNDER_P2P			0x00000400
#define	WIFI_STA_ALIVE_CHK_STATE	0x00000400
#define	WIFI_SITE_MONITOR			0x00000800		//to indicate the station is under site surveying
#ifdef WDS
#define	WIFI_WDS				0x00001000
#define	WIFI_WDS_RX_BEACON	0x00002000		// already rx WDS AP beacon
#endif
#ifdef AUTO_CONFIG
#define	WIFI_AUTOCONF			0x00004000
#define	WIFI_AUTOCONF_IND	0x00008000
#endif

/*
// ========== P2P Section Start ===============
#define	WIFI_P2P_LISTEN_STATE		0x00010000
#define	WIFI_P2P_GROUP_FORMATION_STATE		0x00020000
// ========== P2P Section End ===============
*/

//#ifdef UNDER_MPTEST
#define	WIFI_MP_STATE							0x00010000
#define	WIFI_MP_CTX_BACKGROUND				0x00020000	// in continous tx background
#define	WIFI_MP_CTX_ST						0x00040000	// in continous tx with single-tone
#define	WIFI_MP_CTX_BACKGROUND_PENDING	0x00080000	// pending in continous tx background due to out of skb
#define	WIFI_MP_CTX_CCK_HW					0x00100000	// in continous tx
#define	WIFI_MP_CTX_CCK_CS					0x00200000	// in continous tx with carrier suppression
#define   WIFI_MP_LPBK_STATE					0x00400000
//#endif

//#define _FW_UNDER_CMD		WIFI_UNDER_CMD
#define _FW_UNDER_LINKING	WIFI_UNDER_LINKING
#define _FW_LINKED			WIFI_ASOC_STATE
#define _FW_UNDER_SURVEY	WIFI_SITE_MONITOR


enum dot11AuthAlgrthmNum {
 dot11AuthAlgrthm_Open = 0,
 dot11AuthAlgrthm_Shared,
 dot11AuthAlgrthm_8021X,
 dot11AuthAlgrthm_Auto,
 dot11AuthAlgrthm_WAPI,
 dot11AuthAlgrthm_MaxNum
};

// Scan type including active and passive scan.
typedef enum _RT_SCAN_TYPE
{
	SCAN_PASSIVE,
	SCAN_ACTIVE,
	SCAN_MIX,
}RT_SCAN_TYPE, *PRT_SCAN_TYPE;

enum  _BAND
{
	GHZ24_50 = 0,
	GHZ_50,
	GHZ_24,
	GHZ_MAX,
};

#define rtw_band_valid(band) ((band) >= GHZ24_50 && (band) < GHZ_MAX)

enum DriverInterface {
	DRIVER_WEXT =  1,
	DRIVER_CFG80211 = 2
};

enum SCAN_RESULT_TYPE
{
	SCAN_RESULT_P2P_ONLY = 0,		//	Will return all the P2P devices.
	SCAN_RESULT_ALL = 1,			//	Will return all the scanned device, include AP.
	SCAN_RESULT_WFD_TYPE = 2		//	Will just return the correct WFD device.
									//	If this device is Miracast sink device, it will just return all the Miracast source devices.
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

struct sitesurvey_ctrl {
	u64	last_tx_pkts;
	uint	last_rx_pkts;
	sint	traffic_busy;
	_timer	sitesurvey_ctrl_timer;
};

typedef struct _RT_LINK_DETECT_T{
	u32				NumTxOkInPeriod;
	u32				NumRxOkInPeriod;
	u32				NumRxUnicastOkInPeriod;
	BOOLEAN			bBusyTraffic;
	BOOLEAN			bTxBusyTraffic;
	BOOLEAN			bRxBusyTraffic;
	BOOLEAN			bHigherBusyTraffic; // For interrupt migration purpose.
	BOOLEAN			bHigherBusyRxTraffic; // We may disable Tx interrupt according as Rx traffic.
	BOOLEAN			bHigherBusyTxTraffic; // We may disable Tx interrupt according as Tx traffic.
	//u8 TrafficBusyState;
	u8 TrafficTransitionCount;
	u32 LowPowerTransitionCount;
}RT_LINK_DETECT_T, *PRT_LINK_DETECT_T;

struct profile_info {
	u8	ssidlen;
	u8	ssid[ WLAN_SSID_MAXLEN ];
	u8	peermac[ ETH_ALEN ];
};

struct tx_invite_req_info{
	u8					token;
	u8					benable;
	u8					go_ssid[ WLAN_SSID_MAXLEN ];
	u8					ssidlen;
	u8					go_bssid[ ETH_ALEN ];
	u8					peer_macaddr[ ETH_ALEN ];
	u8					operating_ch;	//	This information will be set by using the p2p_set op_ch=x
	u8					peer_ch;		//	The listen channel for peer P2P device

};

struct tx_invite_resp_info{
	u8					token;	//	Used to record the dialog token of p2p invitation request frame.
};

#ifdef CONFIG_WFD

struct wifi_display_info{
	u16							wfd_enable;			//	Eanble/Disable the WFD function.
	u16							rtsp_ctrlport;		//	TCP port number at which the this WFD device listens for RTSP messages
	u16							peer_rtsp_ctrlport;	//	TCP port number at which the peer WFD device listens for RTSP messages
													//	This filed should be filled when receiving the gropu negotiation request

	u8							peer_session_avail;	//	WFD session is available or not for the peer wfd device.
													//	This variable will be set when sending the provisioning discovery request to peer WFD device.
													//	And this variable will be reset when it is read by using the iwpriv p2p_get wfd_sa command.
	u8							ip_address[4];
	u8							peer_ip_address[4];
	u8							wfd_pc;				//	WFD preferred connection
													//	0 -> Prefer to use the P2P for WFD connection on peer side.
													//	1 -> Prefer to use the TDLS for WFD connection on peer side.
											
	u8							wfd_device_type;	//	WFD Device Type
													//	0 -> WFD Source Device
													//	1 -> WFD Primary Sink Device
	enum	SCAN_RESULT_TYPE	scan_result_type;	//	Used when P2P is enable. This parameter will impact the scan result.

};
#endif //CONFIG_WFD

struct tx_provdisc_req_info{
	u16					wps_config_method_request;	//	Used when sending the provisioning request frame
	u16					peer_channel_num[2];		//	The channel number which the receiver stands.
	NDIS_802_11_SSID	ssid;
	u8					peerDevAddr[ ETH_ALEN ];		//	Peer device address
	u8					peerIFAddr[ ETH_ALEN ];		//	Peer interface address
	u8					benable;					//	This provision discovery request frame is trigger to send or not
};

struct rx_provdisc_req_info{	//When peer device issue prov_disc_req first, we should store the following informations
	u8					peerDevAddr[ ETH_ALEN ];		//	Peer device address
	u8					strconfig_method_desc_of_prov_disc_req[4];	//	description for the config method located in the provisioning discovery request frame.	
																	//	The UI must know this information to know which config method the remote p2p device is requiring.
};

struct tx_nego_req_info{
	u16					peer_channel_num[2];		//	The channel number which the receiver stands.
	u8					peerDevAddr[ ETH_ALEN ];		//	Peer device address
	u8					benable;					//	This negoitation request frame is trigger to send or not
};

struct group_id_info{
	u8					go_device_addr[ ETH_ALEN ];	//	The GO's device address of this P2P group
	u8					ssid[ WLAN_SSID_MAXLEN ];	//	The SSID of this P2P group
};

struct scan_limit_info{
	u8					scan_op_ch_only;			//	When this flag is set, the driver should just scan the operation channel
#ifndef CONFIG_P2P_OP_CHK_SOCIAL_CH
	u8					operation_ch[2];				//	Store the operation channel of invitation request frame
#else
	u8					operation_ch[5];				//	Store additional channel 1,6,11  for Android 4.2 IOT & Nexus 4
#endif //CONFIG_P2P_OP_CHK_SOCIAL_CH
};

#ifdef CONFIG_IOCTL_CFG80211
struct cfg80211_wifidirect_info{
	_timer					remain_on_ch_timer;
	u8						restore_channel;
	struct ieee80211_channel	remain_on_ch_channel;
	enum nl80211_channel_type	remain_on_ch_type;
	u64						remain_on_ch_cookie;
	bool is_ro_ch;
	u32 last_ro_ch_time; /* this will be updated at the beginning and end of ro_ch */
};
#endif //CONFIG_IOCTL_CFG80211

struct wifidirect_info{
	_adapter*				padapter;
	_timer					find_phase_timer;
	_timer					restore_p2p_state_timer;
	
	//	Used to do the scanning. After confirming the peer is availalble, the driver transmits the P2P frame to peer.
	_timer					pre_tx_scan_timer;
	_timer					reset_ch_sitesurvey;
	_timer					reset_ch_sitesurvey2;	//	Just for resetting the scan limit function by using p2p nego
#ifdef CONFIG_CONCURRENT_MODE
	//	Used to switch the channel between legacy AP and listen state.
	_timer					ap_p2p_switch_timer;
#endif
	struct tx_provdisc_req_info	tx_prov_disc_info;
	struct rx_provdisc_req_info rx_prov_disc_info;
	struct tx_invite_req_info	invitereq_info;
	struct profile_info			profileinfo[ P2P_MAX_PERSISTENT_GROUP_NUM ];	//	Store the profile information of persistent group
	struct tx_invite_resp_info	inviteresp_info;
	struct tx_nego_req_info	nego_req_info;
	struct group_id_info		groupid_info;	//	Store the group id information when doing the group negotiation handshake.
	struct scan_limit_info		rx_invitereq_info;	//	Used for get the limit scan channel from the Invitation procedure
	struct scan_limit_info		p2p_info;		//	Used for get the limit scan channel from the P2P negotiation handshake
#ifdef CONFIG_WFD
	struct wifi_display_info		*wfd_info;
#endif	
	enum P2P_ROLE			role;
	enum P2P_STATE			pre_p2p_state;
	enum P2P_STATE			p2p_state;
	u8 						device_addr[ETH_ALEN];	//	The device address should be the mac address of this device.
	u8						interface_addr[ETH_ALEN];
	u8						social_chan[4];
	u8						listen_channel;
	u8						operating_channel;
	u8						listen_dwell;		//	This value should be between 1 and 3
	u8						support_rate[8];
	u8						p2p_wildcard_ssid[P2P_WILDCARD_SSID_LEN];
	u8						intent;		//	should only include the intent value.
	u8						p2p_peer_interface_addr[ ETH_ALEN ];
	u8						p2p_peer_device_addr[ ETH_ALEN ];
	u8						peer_intent;	//	Included the intent value and tie breaker value.
	u8						device_name[ WPS_MAX_DEVICE_NAME_LEN ];	//	Device name for displaying on searching device screen
	u8						device_name_len;
	u8						profileindex;	//	Used to point to the index of profileinfo array
	u8						peer_operating_ch;
	u8						find_phase_state_exchange_cnt;
	u16						device_password_id_for_nego;	//	The device password ID for group negotation
	u8						negotiation_dialog_token;
	u8						nego_ssid[ WLAN_SSID_MAXLEN ];	//	SSID information for group negotitation
	u8						nego_ssidlen;
	u8 						p2p_group_ssid[WLAN_SSID_MAXLEN];
	u8 						p2p_group_ssid_len;
	u8						persistent_supported;		//	Flag to know the persistent function should be supported or not.
														//	In the Sigma test, the Sigma will provide this enable from the sta_set_p2p CAPI.
														//	0: disable
														//	1: enable
	u8						session_available;			//	Flag to set the WFD session available to enable or disable "by Sigma"
														//	In the Sigma test, the Sigma will disable the session available by using the sta_preset CAPI.
														//	0: disable
														//	1: enable

	u8						wfd_tdls_enable;			//	Flag to enable or disable the TDLS by WFD Sigma
														//	0: disable
														//	1: enable
	u8						wfd_tdls_weaksec;			//	Flag to enable or disable the weak security function for TDLS by WFD Sigma
														//	0: disable
														//	In this case, the driver can't issue the tdsl setup request frame.
														//	1: enable
														//	In this case, the driver can issue the tdls setup request frame
														//	even the current security is weak security.

	enum	P2P_WPSINFO		ui_got_wps_info;			//	This field will store the WPS value (PIN value or PBC) that UI had got from the user.
	u16						supported_wps_cm;			//	This field describes the WPS config method which this driver supported.
														//	The value should be the combination of config method defined in page104 of WPS v2.0 spec.	
	u8						external_uuid;				// UUID flag
	u8						uuid[16];					// UUID
	uint						channel_list_attr_len;		//	This field will contain the length of body of P2P Channel List attribute of group negotitation response frame.
	u8						channel_list_attr[100];		//	This field will contain the body of P2P Channel List attribute of group negotitation response frame.
														//	We will use the channel_cnt and channel_list fields when constructing the group negotitation confirm frame.
	u8						driver_interface;			//	Indicate DRIVER_WEXT or DRIVER_CFG80211

#ifdef CONFIG_CONCURRENT_MODE
	u16						ext_listen_interval;	//	The interval to be available with legacy AP (ms)
	u16						ext_listen_period;	//	The time period to be available for P2P listen state (ms)
#endif
#ifdef CONFIG_P2P_PS
	enum P2P_PS_MODE		p2p_ps_mode; // indicate p2p ps mode
	enum P2P_PS_STATE		p2p_ps_state; // indicate p2p ps state
	u8						noa_index; // Identifies and instance of Notice of Absence timing.
	u8						ctwindow; // Client traffic window. A period of time in TU after TBTT.
	u8						opp_ps; // opportunistic power save.
	u8						noa_num; // number of NoA descriptor in P2P IE.
	u8						noa_count[P2P_MAX_NOA_NUM]; // Count for owner, Type of client.
	u32						noa_duration[P2P_MAX_NOA_NUM]; // Max duration for owner, preferred or min acceptable duration for client.
	u32						noa_interval[P2P_MAX_NOA_NUM]; // Length of interval for owner, preferred or max acceptable interval of client.
	u32						noa_start_time[P2P_MAX_NOA_NUM]; // schedule expressed in terms of the lower 4 bytes of the TSF timer.
#endif // CONFIG_P2P_PS
};

struct tdls_ss_record{	//signal strength record
	u8		macaddr[ETH_ALEN];
	u8		RxPWDBAll;
	u8		is_tdls_sta;	// _TRUE: direct link sta, _FALSE: else
};

struct tdls_info{
	u8					ap_prohibited;
	u8					link_established;
	u8					sta_cnt;
	u8					sta_maximum;	// 1:tdls sta is equal (NUM_STA-1), reach max direct link number; 0: else;
	struct tdls_ss_record	ss_record;
	u8					ch_sensing;
	u8					cur_channel;
	u8					candidate_ch;
	u8					collect_pkt_num[MAX_CHANNEL_NUM];
	_lock				cmd_lock;
	_lock				hdl_lock;
	u8					watchdog_count;
	u8					dev_discovered;		//WFD_TDLS: for sigma test
	u8					tdls_enable;
	u8					external_setup;	// _TRUE: setup is handled by wpa_supplicant
#ifdef CONFIG_WFD
	struct wifi_display_info		*wfd_info;
#endif		
};

struct tdls_txmgmt {
	u8 peer[ETH_ALEN];
	u8 action_code;
	u8 dialog_token;
	u16 status_code;
	u8 *buf;
	size_t len;
	u8 external_support;
};

/* used for mlme_priv.roam_flags */
enum {
	RTW_ROAM_ON_EXPIRED = BIT0,
	RTW_ROAM_ON_RESUME = BIT1,
	RTW_ROAM_ACTIVE = BIT2,
};

struct mlme_priv {

	_lock	lock;
	sint	fw_state;	//shall we protect this variable? maybe not necessarily...
	u8 bScanInProcess;
	u8	to_join; //flag
	#ifdef CONFIG_LAYER2_ROAMING
	u8 to_roam; /* roaming trying times */
	struct wlan_network *roam_network; /* the target of active roam */
	u8 roam_flags;
	u8 roam_rssi_diff_th; /* rssi difference threshold for active scan candidate selection */
	u32 roam_scan_int_ms; /* scan interval for active roam */
	u32 roam_scanr_exp_ms; /* scan result expire time in ms  for roam */
	u8 roam_tgt_addr[ETH_ALEN]; /* request to roam to speicific target without other consideration */
	#endif

	u8	*nic_hdl;

	u8	not_indic_disco;
	_list		*pscanned;
	_queue	free_bss_pool;
	_queue	scanned_queue;
	u8		*free_bss_buf;
	u32	num_of_scanned;

	NDIS_802_11_SSID	assoc_ssid;
	u8	assoc_bssid[6];

	struct wlan_network	cur_network;
	struct wlan_network *cur_network_scanned;
#ifdef CONFIG_ARP_KEEP_ALIVE
	// for arp offload keep alive
	u8	gw_mac_addr[6];
	u8	gw_ip[4];
#endif

	//uint wireless_mode; no used, remove it

	u32	auto_scan_int_ms;

	_timer assoc_timer;

	uint assoc_by_bssid;
	uint assoc_by_rssi;

	_timer scan_to_timer; // driver itself handles scan_timeout status.
	u32 scan_start_time; // used to evaluate the time spent in scanning

	#ifdef CONFIG_SET_SCAN_DENY_TIMER
	_timer set_scan_deny_timer;
	ATOMIC_T set_scan_deny; //0: allowed, 1: deny
	#endif

	struct qos_priv qospriv;

#ifdef CONFIG_80211N_HT

	/* Number of non-HT AP/stations */
	int num_sta_no_ht;

	/* Number of HT AP/stations 20 MHz */
	//int num_sta_ht_20mhz; 


	int num_FortyMHzIntolerant;

	struct ht_priv	htpriv;

#endif

#ifdef CONFIG_80211AC_VHT
	struct vht_priv	vhtpriv;
#endif
#ifdef CONFIG_BEAMFORMING
	struct beamforming_info	beamforming_info;
#endif

#ifdef CONFIG_DFS
	u8	handle_dfs;
#endif //CONFIG_DFS

	RT_LINK_DETECT_T	LinkDetectInfo;
	_timer	dynamic_chk_timer; //dynamic/periodic check timer

	u8	acm_mask; // for wmm acm mask
	u8	ChannelPlan;
	RT_SCAN_TYPE 	scan_mode; // active: 1, passive: 0

	u8 *wps_probe_req_ie;
	u32 wps_probe_req_ie_len;

#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	/* Number of associated Non-ERP stations (i.e., stations using 802.11b
	 * in 802.11g BSS) */
	int num_sta_non_erp;

	/* Number of associated stations that do not support Short Slot Time */
	int num_sta_no_short_slot_time;

	/* Number of associated stations that do not support Short Preamble */
	int num_sta_no_short_preamble;

	int olbc; /* Overlapping Legacy BSS Condition */

	/* Number of HT associated stations that do not support greenfield */
	int num_sta_ht_no_gf;

	/* Number of associated non-HT stations */
	//int num_sta_no_ht;

	/* Number of HT associated stations 20 MHz */
	int num_sta_ht_20mhz;

	/* Overlapping BSS information */
	int olbc_ht;
	
#ifdef CONFIG_80211N_HT
	u16 ht_op_mode;
#endif /* CONFIG_80211N_HT */	

	u8 *assoc_req;
	u32 assoc_req_len;
	u8 *assoc_rsp;
	u32 assoc_rsp_len;

	u8 *wps_beacon_ie;	
	//u8 *wps_probe_req_ie;
	u8 *wps_probe_resp_ie;
	u8 *wps_assoc_resp_ie; // for CONFIG_IOCTL_CFG80211, this IE could include p2p ie / wfd ie

	u32 wps_beacon_ie_len;
	//u32 wps_probe_req_ie_len;
	u32 wps_probe_resp_ie_len;
	u32 wps_assoc_resp_ie_len; // for CONFIG_IOCTL_CFG80211, this IE len could include p2p ie / wfd ie
	
	u8 *p2p_beacon_ie;
	u8 *p2p_probe_req_ie;
	u8 *p2p_probe_resp_ie;	
	u8 *p2p_go_probe_resp_ie; //for GO	
	u8 *p2p_assoc_req_ie;

	u32 p2p_beacon_ie_len;
	u32 p2p_probe_req_ie_len;
	u32 p2p_probe_resp_ie_len;
	u32 p2p_go_probe_resp_ie_len; //for GO
	u32 p2p_assoc_req_ie_len;
/*
#if defined(CONFIG_P2P) && defined(CONFIG_IOCTL_CFG80211)
	//u8 *wps_p2p_beacon_ie;
	u8 *p2p_beacon_ie;
	u8 *wps_p2p_probe_resp_ie;
	u8 *wps_p2p_assoc_resp_ie;
	//u32 wps_p2p_beacon_ie_len;
	u32 p2p_beacon_ie_len;
	u32 wps_p2p_probe_resp_ie_len;
	u32 wps_p2p_assoc_resp_ie_len;
#endif
*/
	
	_lock	bcn_update_lock;
	u8		update_bcn;
	
	
#endif //#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)

#if defined(CONFIG_WFD) && defined(CONFIG_IOCTL_CFG80211)
	
	u8 *wfd_beacon_ie;
	u8 *wfd_probe_req_ie;
	u8 *wfd_probe_resp_ie;	
	u8 *wfd_go_probe_resp_ie; //for GO	
	u8 *wfd_assoc_req_ie;

	u32 wfd_beacon_ie_len;
	u32 wfd_probe_req_ie_len;
	u32 wfd_probe_resp_ie_len;
	u32 wfd_go_probe_resp_ie_len; //for GO
	u32 wfd_assoc_req_ie_len;

#endif

#ifdef RTK_DMP_PLATFORM
	// DMP kobject_hotplug function  signal need in passive level
	_workitem	Linkup_workitem;
	_workitem	Linkdown_workitem;
#endif

#ifdef CONFIG_INTEL_WIDI
	int	widi_state;
	int	listen_state;
	_timer	listen_timer;
	ATOMIC_T	rx_probe_rsp; // 1:receive probe respone from RDS source.
	u8	*l2sdTaBuffer;
	u8	channel_idx;
	u8	group_cnt;	//In WiDi 3.5, they specified another scan algo. for WFD/RDS co-existed
	u8	sa_ext[L2SDTA_SERVICE_VE_LEN];

	u8	widi_enable;
	/**
	 * For WiDi 4; upper layer would set
	 * p2p_primary_device_type_category_id
	 * p2p_primary_device_type_sub_category_id
	 * p2p_secondary_device_type_category_id
	 * p2p_secondary_device_type_sub_category_id
	 */
	u16	p2p_pdt_cid;
	u16	p2p_pdt_scid;
	u8	num_p2p_sdt;
	u16	p2p_sdt_cid[MAX_NUM_P2P_SDT];
	u16	p2p_sdt_scid[MAX_NUM_P2P_SDT];
	u8	p2p_reject_disable;	//When starting NL80211 wpa_supplicant/hostapd, it will call netdev_close
							//such that it will cause p2p disabled. Use this flag to reject.
#endif // CONFIG_INTEL_WIDI

#ifdef CONFIG_CONCURRENT_MODE
	u8	scanning_via_buddy_intf;
#endif

	u8 	NumOfBcnInfoChkFail;
	u32	timeBcnInfoChkStart;
};

#define rtw_mlme_set_auto_scan_int(adapter, ms) \
	do { \
		adapter->mlmepriv.auto_scan_int_ms = ms; \
	while (0)

void rtw_mlme_reset_auto_scan_int(_adapter *adapter);

#ifdef CONFIG_AP_MODE

struct hostapd_priv
{
	_adapter *padapter;

#ifdef CONFIG_HOSTAPD_MLME
	struct net_device *pmgnt_netdev;
	struct usb_anchor anchored;
#endif	
	
};

extern int hostapd_mode_init(_adapter *padapter);
extern void hostapd_mode_unload(_adapter *padapter);
#endif


extern void rtw_joinbss_event_prehandle(_adapter *adapter, u8 *pbuf);
extern void rtw_survey_event_callback(_adapter *adapter, u8 *pbuf);
extern void rtw_surveydone_event_callback(_adapter *adapter, u8 *pbuf);
extern void rtw_joinbss_event_callback(_adapter *adapter, u8 *pbuf);
extern void rtw_stassoc_event_callback(_adapter *adapter, u8 *pbuf);
extern void rtw_stadel_event_callback(_adapter *adapter, u8 *pbuf);
extern void rtw_atimdone_event_callback(_adapter *adapter, u8 *pbuf);
extern void rtw_cpwm_event_callback(_adapter *adapter, u8 *pbuf);
extern void rtw_wmm_event_callback(PADAPTER padapter, u8 *pbuf);

extern void rtw_join_timeout_handler(RTW_TIMER_HDL_ARGS);
extern void _rtw_scan_timeout_handler(RTW_TIMER_HDL_ARGS);

thread_return event_thread(thread_context context);

extern void rtw_free_network_queue(_adapter *adapter,u8 isfreeall);
extern int rtw_init_mlme_priv(_adapter *adapter);// (struct mlme_priv *pmlmepriv);

extern void rtw_free_mlme_priv (struct mlme_priv *pmlmepriv);


extern sint rtw_select_and_join_from_scanned_queue(struct mlme_priv *pmlmepriv);
extern sint rtw_set_key(_adapter *adapter,struct security_priv *psecuritypriv,sint keyid, u8 set_tx, bool enqueue);
extern sint rtw_set_auth(_adapter *adapter,struct security_priv *psecuritypriv);

__inline static u8 *get_bssid(struct mlme_priv *pmlmepriv)
{	//if sta_mode:pmlmepriv->cur_network.network.MacAddress=> bssid
	// if adhoc_mode:pmlmepriv->cur_network.network.MacAddress=> ibss mac address
	return pmlmepriv->cur_network.network.MacAddress;
}

__inline static sint check_fwstate(struct mlme_priv *pmlmepriv, sint state)
{
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
__inline static void set_fwstate(struct mlme_priv *pmlmepriv, sint state)
{
	pmlmepriv->fw_state |= state;
	//FOR HW integration
	if(_FW_UNDER_SURVEY==state){
		pmlmepriv->bScanInProcess = _TRUE;
	}
}

__inline static void _clr_fwstate_(struct mlme_priv *pmlmepriv, sint state)
{
	pmlmepriv->fw_state &= ~state;
	//FOR HW integration
	if(_FW_UNDER_SURVEY==state){
		pmlmepriv->bScanInProcess = _FALSE;
	}
}

/*
 * No Limit on the calling context,
 * therefore set it to be the critical section...
 */
__inline static void clr_fwstate(struct mlme_priv *pmlmepriv, sint state)
{
	_irqL irqL;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	if (check_fwstate(pmlmepriv, state) == _TRUE)
		pmlmepriv->fw_state ^= state;
	_exit_critical_bh(&pmlmepriv->lock, &irqL);
}

__inline static void clr_fwstate_ex(struct mlme_priv *pmlmepriv, sint state)
{
	_irqL irqL;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	_clr_fwstate_(pmlmepriv, state);
	_exit_critical_bh(&pmlmepriv->lock, &irqL);
}

__inline static void up_scanned_network(struct mlme_priv *pmlmepriv)
{
	_irqL irqL;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	pmlmepriv->num_of_scanned++;
	_exit_critical_bh(&pmlmepriv->lock, &irqL);
}

#ifdef CONFIG_CONCURRENT_MODE
sint rtw_buddy_adapter_up(_adapter *padapter);
sint check_buddy_fwstate(_adapter *padapter, sint state);
u8 rtw_get_buddy_bBusyTraffic(_adapter *padapter);
#endif //CONFIG_CONCURRENT_MODE

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
extern void rtw_update_scanned_network(_adapter *adapter, WLAN_BSSID_EX *target);
extern void rtw_disconnect_hdl_under_linked(_adapter* adapter, struct sta_info *psta, u8 free_assoc);
extern void rtw_generate_random_ibss(u8 *pibss);
extern struct wlan_network* rtw_find_network(_queue *scanned_queue, u8 *addr);
extern struct wlan_network* rtw_get_oldest_wlan_network(_queue *scanned_queue);
struct wlan_network *_rtw_find_same_network(_queue *scanned_queue, struct wlan_network *network);
struct wlan_network *rtw_find_same_network(_queue *scanned_queue, struct wlan_network *network);

extern void rtw_free_assoc_resources(_adapter* adapter, int lock_scanned_queue);
extern void rtw_indicate_disconnect(_adapter* adapter);
extern void rtw_indicate_connect(_adapter* adapter);
void rtw_indicate_scan_done( _adapter *padapter, bool aborted);
void rtw_scan_abort(_adapter *adapter);

extern int rtw_restruct_sec_ie(_adapter *adapter,u8 *in_ie,u8 *out_ie,uint in_len);
extern int rtw_restruct_wmm_ie(_adapter *adapter, u8 *in_ie, u8 *out_ie, uint in_len, uint initial_out_len);
extern void rtw_init_registrypriv_dev_network(_adapter *adapter);

extern void rtw_update_registrypriv_dev_network(_adapter *adapter);

extern void rtw_get_encrypt_decrypt_from_registrypriv(_adapter *adapter);

extern void _rtw_join_timeout_handler(_adapter *adapter);
extern void rtw_scan_timeout_handler(_adapter *adapter);

extern void rtw_dynamic_check_timer_handlder(_adapter *adapter);
#ifdef CONFIG_SET_SCAN_DENY_TIMER
bool rtw_is_scan_deny(_adapter *adapter);
void rtw_clear_scan_deny(_adapter *adapter);
void rtw_set_scan_deny_timer_hdl(_adapter *adapter);
void rtw_set_scan_deny(_adapter *adapter, u32 ms);
#else
#define rtw_is_scan_deny(adapter) _FALSE
#define rtw_clear_scan_deny(adapter) do {} while (0)
#define rtw_set_scan_deny_timer_hdl(adapter) do {} while (0)
#define rtw_set_scan_deny(adapter, ms) do {} while (0)
#endif


extern int _rtw_init_mlme_priv(_adapter *padapter);

void rtw_free_mlme_priv_ie_data(struct mlme_priv *pmlmepriv);

extern void _rtw_free_mlme_priv(struct mlme_priv *pmlmepriv);

extern int _rtw_enqueue_network(_queue *queue, struct wlan_network *pnetwork);

//extern struct wlan_network* _rtw_dequeue_network(_queue *queue);

extern struct wlan_network* _rtw_alloc_network(struct mlme_priv *pmlmepriv);


extern void _rtw_free_network(struct mlme_priv *pmlmepriv, struct wlan_network *pnetwork, u8 isfreeall);
extern void _rtw_free_network_nolock(struct mlme_priv *pmlmepriv, struct wlan_network *pnetwork);


extern struct wlan_network* _rtw_find_network(_queue *scanned_queue, u8 *addr);

extern void _rtw_free_network_queue(_adapter* padapter, u8 isfreeall);

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
void rtw_issue_addbareq_cmd(_adapter *padapter, struct xmit_frame *pxmitframe);
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
#define _rtw_roaming(adapter, tgt_network) do {} while(0)
#define rtw_roaming(adapter, tgt_network) do {} while(0)
#define rtw_set_to_roam(adapter, to_roam) do {} while(0)
#define rtw_dec_to_roam(adapter) 0
#define rtw_to_roam(adapter) 0
#define rtw_select_roaming_candidate(mlme) _FAIL
#endif /* CONFIG_LAYER2_ROAMING */

void rtw_sta_media_status_rpt(_adapter *adapter,struct sta_info *psta, u32 mstatus);

#ifdef CONFIG_INTEL_PROXIM
void rtw_proxim_enable(_adapter *padapter);
void rtw_proxim_disable(_adapter *padapter);
void rtw_proxim_send_packet(_adapter *padapter,u8 *pbuf,u16 len,u8 hw_rate);
#endif //CONFIG_INTEL_PROXIM
#endif //__RTL871X_MLME_H_

