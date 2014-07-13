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

#include <osdep_service.h>
#include <mlme_osdep.h>
#include <drv_types.h>
#include <wlan_bssdef.h>

#define	MAX_BSS_CNT	128
#define   MAX_JOIN_TIMEOUT	6500

/* Increase the scanning timeout because of increasing the SURVEY_TO value. */

#define		SCANNING_TIMEOUT	8000

#define	SCAN_INTERVAL	(30) /*  unit:2sec, 30*2=60sec */

#define	SCANQUEUE_LIFETIME 20 /*  unit:sec */

#define	WIFI_NULL_STATE			0x00000000

#define	WIFI_ASOC_STATE			0x00000001	/* Under Linked state */
#define	WIFI_REASOC_STATE		0x00000002
#define	WIFI_SLEEP_STATE		0x00000004
#define	WIFI_STATION_STATE		0x00000008

#define	WIFI_AP_STATE			0x00000010
#define	WIFI_ADHOC_STATE		0x00000020
#define WIFI_ADHOC_MASTER_STATE		0x00000040
#define WIFI_UNDER_LINKING		0x00000080

#define	WIFI_UNDER_WPS			0x00000100
#define	WIFI_STA_ALIVE_CHK_STATE	0x00000400
#define	WIFI_SITE_MONITOR		0x00000800	/* to indicate the station is under site surveying */

#define	WIFI_MP_STATE			0x00010000
#define	WIFI_MP_CTX_BACKGROUND		0x00020000	/*  in continuous tx background */
#define	WIFI_MP_CTX_ST			0x00040000	/*  in continuous tx with single-tone */
#define	WIFI_MP_CTX_BACKGROUND_PENDING	0x00080000	/*  pending in continuous tx background due to out of skb */
#define	WIFI_MP_CTX_CCK_HW		0x00100000	/*  in continuous tx */
#define	WIFI_MP_CTX_CCK_CS		0x00200000	/*  in continuous tx with carrier suppression */
#define WIFI_MP_LPBK_STATE		0x00400000

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

/*  Scan type including active and passive scan. */
enum rt_scan_type {
	SCAN_PASSIVE,
	SCAN_ACTIVE,
	SCAN_MIX,
};

enum SCAN_RESULT_TYPE {
	SCAN_RESULT_P2P_ONLY = 0,	/* Will return all the P2P devices. */
	SCAN_RESULT_ALL = 1,		/* Will return all the scanned device,
					 * include AP. */
	SCAN_RESULT_WFD_TYPE = 2	/* Will just return the correct WFD
					 * device. */
					/* If this device is Miracast sink
					 * device, it will just return all the
					 * Miracast source devices. */
};

/*
there are several "locks" in mlme_priv,
since mlme_priv is a shared resource between many threads,
like ISR/Call-Back functions, the OID handlers, and even timer functions.

Each _queue has its own locks, already.
Other items are protected by mlme_priv.lock.

To avoid possible dead lock, any thread trying to modifiying mlme_priv
SHALL not lock up more than one lock at a time!
*/

#define traffic_threshold	10
#define	traffic_scan_period	500

struct rt_link_detect {
	u32	NumTxOkInPeriod;
	u32	NumRxOkInPeriod;
	u32	NumRxUnicastOkInPeriod;
	bool	bBusyTraffic;
	bool	bTxBusyTraffic;
	bool	bRxBusyTraffic;
	bool	bHigherBusyTraffic; /*  For interrupt migration purpose. */
	bool	bHigherBusyRxTraffic; /* We may disable Tx interrupt according
				       * to Rx traffic. */
	bool	bHigherBusyTxTraffic; /* We may disable Tx interrupt according
				       * to Tx traffic. */
};

struct profile_info {
	u8	ssidlen;
	u8	ssid[WLAN_SSID_MAXLEN];
	u8	peermac[ETH_ALEN];
};

struct tx_invite_req_info {
	u8	token;
	u8	benable;
	u8	go_ssid[WLAN_SSID_MAXLEN];
	u8	ssidlen;
	u8	go_bssid[ETH_ALEN];
	u8	peer_macaddr[ETH_ALEN];
	u8	operating_ch;	/* This information will be set by using the
				 * p2p_set op_ch=x */
	u8	peer_ch;	/* The listen channel for peer P2P device */
};

struct tx_invite_resp_info {
	u8	token;	/* Used to record the dialog token of p2p invitation
			 * request frame. */
};

struct tx_provdisc_req_info {
	u16	wps_config_method_request;	/* Used when sending the
						 * provisioning request frame*/
	u16	peer_channel_num[2];		/* The channel number which the
						 * receiver stands. */
	struct ndis_802_11_ssid	ssid;
	u8	peerDevAddr[ETH_ALEN];		/* Peer device address */
	u8	peerIFAddr[ETH_ALEN];		/* Peer interface address */
	u8	benable;			/* This provision discovery
						 * request frame is trigger
						 * to send or not */
};

/* When peer device issue prov_disc_req first, we should store the following
 * information */
/* The UI must know this information to know which config method the
 * remote p2p device needs. */
struct rx_provdisc_req_info {
	u8	peerDevAddr[ETH_ALEN];		/* Peer device address */
	u8	strconfig_method_desc_of_prov_disc_req[4];	/* description
			* for the config method located in the provisioning
			* discovery request frame. */
};

struct tx_nego_req_info {
	u16	peer_channel_num[2];	/* The channel number. */
	u8	peerDevAddr[ETH_ALEN];	/* Peer device address */
	u8	benable;		/* This negotiation request frame is
					 * trigger to send or not */
};

struct group_id_info {
	u8	go_device_addr[ETH_ALEN];	/* The GO's device address of
						 * this P2P group */
	u8	ssid[WLAN_SSID_MAXLEN];	/* The SSID of this P2P group */
};

struct scan_limit_info {
	u8	scan_op_ch_only;	/* When this flag is set, the driver
					 * should only scan the op. channel */
	u8	operation_ch[2];	/* Store the op. chan of invitation */
};

struct wifidirect_info {
	struct adapter *padapter;
	struct timer_list find_phase_timer;
	struct timer_list restore_p2p_state_timer;

	/* Used to do the scanning. After confirming the peer is availalble,
	 * the driver transmits the P2P frame to peer. */
	struct timer_list pre_tx_scan_timer;
	struct timer_list reset_ch_sitesurvey;
	struct timer_list reset_ch_sitesurvey2;	/* Just for resetting the scan
					 * limit function by using p2p nego */
	struct tx_provdisc_req_info	tx_prov_disc_info;
	struct rx_provdisc_req_info rx_prov_disc_info;
	struct tx_invite_req_info	invitereq_info;
	/* Store the profile information of persistent group */
	struct profile_info profileinfo[P2P_MAX_PERSISTENT_GROUP_NUM];
	struct tx_invite_resp_info	inviteresp_info;
	struct tx_nego_req_info	nego_req_info;
	/* Store the group id info when doing the group negot handshake. */
	struct group_id_info groupid_info;
	/* Used for get the limit scan channel from the Invitation procedure */
	struct scan_limit_info rx_invitereq_info;
	/* Used for get the limit scan chan from the P2P negotiation handshake*/
	struct scan_limit_info p2p_info;
	enum P2P_ROLE role;
	enum P2P_STATE pre_p2p_state;
	enum P2P_STATE p2p_state;
	/* The device address should be the mac address of this device. */
	u8 device_addr[ETH_ALEN];
	u8 interface_addr[ETH_ALEN];
	u8 social_chan[4];
	u8 listen_channel;
	u8 operating_channel;
	u8 listen_dwell;	/* This value should be between 1 and 3 */
	u8 support_rate[8];
	u8 p2p_wildcard_ssid[P2P_WILDCARD_SSID_LEN];
	u8 intent;	/* should only include the intent value. */
	u8 p2p_peer_interface_addr[ETH_ALEN];
	u8 p2p_peer_device_addr[ETH_ALEN];
	u8 peer_intent;	/* Included the intent value and tie breaker value. */
	/* Device name for displaying on searching device screen */
	u8 device_name[WPS_MAX_DEVICE_NAME_LEN];
	u8 device_name_len;
	u8 profileindex; /* Used to point to the index of profileinfo array */
	u8 peer_operating_ch;
	u8 find_phase_state_exchange_cnt;
	/* The device password ID for group negotiation */
	u16 device_password_id_for_nego;
	u8 negotiation_dialog_token;
	/* SSID information for group negotitation */
	u8 nego_ssid[WLAN_SSID_MAXLEN];
	u8 nego_ssidlen;
	u8 p2p_group_ssid[WLAN_SSID_MAXLEN];
	u8 p2p_group_ssid_len;
	/* Flag to know if the persistent function should be supported or not.*/
	u8 persistent_supported;
	/* In the Sigma test, the Sigma will provide this enable from the
	 * sta_set_p2p CAPI. */
	/*	0: disable */
	/*	1: enable */
	u8 session_available;	/* Flag to set the WFD session available to
				 * enable or disable "by Sigma" */
	/* In the Sigma test, the Sigma will disable the session available
	 * by using the sta_preset CAPI. */
	/*	0: disable */
	/*	1: enable */
	u8 wfd_tdls_enable; /* Flag to enable or disable the TDLS by WFD Sigma*/
			    /* 0: disable */
			    /*	1: enable */
	u8 wfd_tdls_weaksec; /* Flag to enable or disable the weak security
			      * function for TDLS by WFD Sigma */
			     /* 0: disable */
			     /* In this case, the driver can't issue the tdsl
			      * setup request frame. */
			     /*	1: enable */
			     /* In this case, the driver can issue the tdls
			      * setup request frame */
			     /*	even the current security is weak security. */

	/* This field will store the WPS value (PIN value or PBC) that UI had
	 * got from the user. */
	enum	P2P_WPSINFO ui_got_wps_info;
	u16 supported_wps_cm;	/* This field describes the WPS config method
				 * which this driver supported. */
				/* The value should be the combination of config
				 * method defined in page104 of WPS v2.0 spec.*/
	/* This field will contain the length of body of P2P Channel List
	 * attribute of group negotiation response frame. */
	uint channel_list_attr_len;
	/* This field will contain the body of P2P Channel List attribute of
	 * group negotitation response frame. */
	/* We will use the channel_cnt and channel_list fields when constructing
	 * the group negotiation confirm frame. */
	u8 channel_list_attr[100];
	enum P2P_PS_MODE p2p_ps_mode; /*  indicate p2p ps mode */
	enum P2P_PS_STATE p2p_ps_state; /*  indicate p2p ps state */
	u8 noa_index; /*  Identifies and instance of Notice of Absence timing. */
	u8 ctwindow; /*  Client traffic window. A period of time in TU after TBTT. */
	u8 opp_ps; /*  opportunistic power save. */
	u8 noa_num; /*  number of NoA descriptor in P2P IE. */
	u8 noa_count[P2P_MAX_NOA_NUM]; /*  Count for owner, Type of client. */
	/* Max duration for owner, preferred or min acceptable duration for
	 * client. */
	u32 noa_duration[P2P_MAX_NOA_NUM];
	/* Length of interval for owner, preferred or max acceptable interval
	 * of client. */
	u32 noa_interval[P2P_MAX_NOA_NUM];
	/* schedule expressed in terms of the lower 4 bytes of the TSF timer. */
	u32 noa_start_time[P2P_MAX_NOA_NUM];
};

struct mlme_priv {
	spinlock_t lock;
	int fw_state;	/* shall we protect this variable? maybe not necessarily... */
	u8 bScanInProcess;
	u8 to_join; /* flag */
	u8 to_roaming; /*  roaming trying times */

	u8 *nic_hdl;

	u8 not_indic_disco;
	struct list_head *pscanned;
	struct __queue free_bss_pool;
	struct __queue scanned_queue;
	u8 *free_bss_buf;
	u32	num_of_scanned;

	struct ndis_802_11_ssid	assoc_ssid;
	u8	assoc_bssid[6];

	struct wlan_network	cur_network;

	u32	scan_interval;

	struct timer_list assoc_timer;

	uint assoc_by_bssid;
	uint assoc_by_rssi;

	struct timer_list scan_to_timer; /*  driver itself handles scan_timeout status. */
	u32 scan_start_time; /*  used to evaluate the time spent in scanning */

	struct qos_priv qospriv;

	/* Number of non-HT AP/stations */
	int num_sta_no_ht;

	/* Number of HT AP/stations 20 MHz */
	/* int num_sta_ht_20mhz; */

	int num_FortyMHzIntolerant;
	struct ht_priv	htpriv;
	struct rt_link_detect LinkDetectInfo;
	struct timer_list dynamic_chk_timer; /* dynamic/periodic check timer */

	u8	key_mask; /* use for ips to set wep key after ips_leave */
	u8	acm_mask; /*  for wmm acm mask */
	u8	ChannelPlan;
	enum rt_scan_type scan_mode; /*  active: 1, passive: 0 */

	/* u8 probereq_wpsie[MAX_WPS_IE_LEN];added in probe req */
	/* int probereq_wpsie_len; */
	u8 *wps_probe_req_ie;
	u32 wps_probe_req_ie_len;

	u8 *assoc_req;
	u32 assoc_req_len;
	u8 *assoc_rsp;
	u32 assoc_rsp_len;

#if defined(CONFIG_88EU_AP_MODE)
	/* Number of associated Non-ERP stations (i.e., stations using 802.11b
	 * in 802.11g BSS) */
	int num_sta_non_erp;

	/* Number of associated stations that do not support Short Slot Time */
	int num_sta_no_short_slot_time;

	/* Number of associated stations that do not support Short Preamble */
	int num_sta_no_short_preamble;

	int olbc; /* Overlapping Legacy BSS Condition */

	/* Number of HT assoc sta that do not support greenfield */
	int num_sta_ht_no_gf;

	/* Number of associated non-HT stations */
	/* int num_sta_no_ht; */

	/* Number of HT associated stations 20 MHz */
	int num_sta_ht_20mhz;

	/* Overlapping BSS information */
	int olbc_ht;

	u16 ht_op_mode;

	u8 *wps_beacon_ie;
	/* u8 *wps_probe_req_ie; */
	u8 *wps_probe_resp_ie;
	u8 *wps_assoc_resp_ie;

	u32 wps_beacon_ie_len;
	u32 wps_probe_resp_ie_len;
	u32 wps_assoc_resp_ie_len;

	u8 *p2p_beacon_ie;
	u8 *p2p_probe_req_ie;
	u8 *p2p_probe_resp_ie;
	u8 *p2p_go_probe_resp_ie; /* for GO */
	u8 *p2p_assoc_req_ie;

	u32 p2p_beacon_ie_len;
	u32 p2p_probe_req_ie_len;
	u32 p2p_probe_resp_ie_len;
	u32 p2p_go_probe_resp_ie_len; /* for GO */
	u32 p2p_assoc_req_ie_len;
	spinlock_t bcn_update_lock;
	u8		update_bcn;
#endif /* if defined (CONFIG_88EU_AP_MODE) */
};

#ifdef CONFIG_88EU_AP_MODE

struct hostapd_priv {
	struct adapter *padapter;
};

int hostapd_mode_init(struct adapter *padapter);
void hostapd_mode_unload(struct adapter *padapter);
#endif

extern unsigned char WPA_TKIP_CIPHER[4];
extern unsigned char RSN_TKIP_CIPHER[4];
extern unsigned char REALTEK_96B_IE[];
extern unsigned char	MCS_rate_2R[16];
extern unsigned char	MCS_rate_1R[16];

void rtw_joinbss_event_prehandle(struct adapter *adapter, u8 *pbuf);
void rtw_survey_event_callback(struct adapter *adapter, u8 *pbuf);
void rtw_surveydone_event_callback(struct adapter *adapter, u8 *pbuf);
void rtw_joinbss_event_callback(struct adapter *adapter, u8 *pbuf);
void rtw_stassoc_event_callback(struct adapter *adapter, u8 *pbuf);
void rtw_stadel_event_callback(struct adapter *adapter, u8 *pbuf);
void rtw_atimdone_event_callback(struct adapter *adapter, u8 *pbuf);
void rtw_cpwm_event_callback(struct adapter *adapter, u8 *pbuf);
void indicate_wx_scan_complete_event(struct adapter *padapter);
void rtw_indicate_wx_assoc_event(struct adapter *padapter);
void rtw_indicate_wx_disassoc_event(struct adapter *padapter);
int event_thread(void *context);
void rtw_join_timeout_handler(void *FunctionContext);
void _rtw_scan_timeout_handler(void *FunctionContext);
void rtw_free_network_queue(struct adapter *adapter, u8 isfreeall);
int rtw_init_mlme_priv(struct adapter *adapter);
void rtw_free_mlme_priv(struct mlme_priv *pmlmepriv);
int rtw_select_and_join_from_scanned_queue(struct mlme_priv *pmlmepriv);
int rtw_set_key(struct adapter *adapter, struct security_priv *psecuritypriv,
		int keyid, u8 set_tx);
int rtw_set_auth(struct adapter *adapter, struct security_priv *psecuritypriv);

static inline u8 *get_bssid(struct mlme_priv *pmlmepriv)
{	/* if sta_mode:pmlmepriv->cur_network.network.MacAddress=> bssid */
	/*  if adhoc_mode:pmlmepriv->cur_network.network.MacAddress=> ibss mac address */
	return pmlmepriv->cur_network.network.MacAddress;
}

static inline int check_fwstate(struct mlme_priv *pmlmepriv, int state)
{
	if (pmlmepriv->fw_state & state)
		return true;

	return false;
}

static inline int get_fwstate(struct mlme_priv *pmlmepriv)
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
static inline void set_fwstate(struct mlme_priv *pmlmepriv, int state)
{
	pmlmepriv->fw_state |= state;
	/* FOR HW integration */
	if (_FW_UNDER_SURVEY == state)
		pmlmepriv->bScanInProcess = true;
}

static inline void _clr_fwstate_(struct mlme_priv *pmlmepriv, int state)
{
	pmlmepriv->fw_state &= ~state;
	/* FOR HW integration */
	if (_FW_UNDER_SURVEY == state)
		pmlmepriv->bScanInProcess = false;
}

/*
 * No Limit on the calling context,
 * therefore set it to be the critical section...
 */
static inline void clr_fwstate(struct mlme_priv *pmlmepriv, int state)
{
	spin_lock_bh(&pmlmepriv->lock);
	if (check_fwstate(pmlmepriv, state) == true)
		pmlmepriv->fw_state ^= state;
	spin_unlock_bh(&pmlmepriv->lock);
}

static inline void clr_fwstate_ex(struct mlme_priv *pmlmepriv, int state)
{
	spin_lock_bh(&pmlmepriv->lock);
	_clr_fwstate_(pmlmepriv, state);
	spin_unlock_bh(&pmlmepriv->lock);
}

static inline void up_scanned_network(struct mlme_priv *pmlmepriv)
{
	spin_lock_bh(&pmlmepriv->lock);
	pmlmepriv->num_of_scanned++;
	spin_unlock_bh(&pmlmepriv->lock);
}

static inline void down_scanned_network(struct mlme_priv *pmlmepriv)
{
	spin_lock_bh(&pmlmepriv->lock);
	pmlmepriv->num_of_scanned--;
	spin_unlock_bh(&pmlmepriv->lock);
}

static inline void set_scanned_network_val(struct mlme_priv *pmlmepriv, int val)
{
	spin_lock_bh(&pmlmepriv->lock);
	pmlmepriv->num_of_scanned = val;
	spin_unlock_bh(&pmlmepriv->lock);
}

u16 rtw_get_capability(struct wlan_bssid_ex *bss);
void rtw_update_scanned_network(struct adapter *adapter,
				struct wlan_bssid_ex *target);
void rtw_disconnect_hdl_under_linked(struct adapter *adapter,
				     struct sta_info *psta, u8 free_assoc);
void rtw_generate_random_ibss(u8 *pibss);
struct wlan_network *rtw_find_network(struct __queue *scanned_queue, u8 *addr);
struct wlan_network *rtw_get_oldest_wlan_network(struct __queue *scanned_queue);

void rtw_free_assoc_resources(struct adapter *adapter, int lock_scanned_queue);
void rtw_indicate_disconnect(struct adapter *adapter);
void rtw_indicate_connect(struct adapter *adapter);
void rtw_indicate_scan_done(struct adapter *padapter, bool aborted);
void rtw_scan_abort(struct adapter *adapter);

int rtw_restruct_sec_ie(struct adapter *adapter, u8 *in_ie, u8 *out_ie,
			uint in_len);
int rtw_restruct_wmm_ie(struct adapter *adapter, u8 *in_ie, u8 *out_ie,
		        uint in_len, uint initial_out_len);
void rtw_init_registrypriv_dev_network(struct adapter *adapter);

void rtw_update_registrypriv_dev_network(struct adapter *adapter);

void rtw_get_encrypt_decrypt_from_registrypriv(struct adapter *adapter);

void _rtw_join_timeout_handler(struct adapter *adapter);
void rtw_scan_timeout_handler(struct adapter *adapter);

void rtw_dynamic_check_timer_handlder(struct adapter *adapter);
#define rtw_is_scan_deny(adapter) false
#define rtw_clear_scan_deny(adapter) do {} while (0)
#define rtw_set_scan_deny_timer_hdl(adapter) do {} while (0)
#define rtw_set_scan_deny(adapter, ms) do {} while (0)


int _rtw_init_mlme_priv(struct adapter *padapter);

void rtw_free_mlme_priv_ie_data(struct mlme_priv *pmlmepriv);

void _rtw_free_mlme_priv(struct mlme_priv *pmlmepriv);

int _rtw_enqueue_network(struct __queue *queue, struct wlan_network *pnetwork);

struct wlan_network *_rtw_dequeue_network(struct __queue *queue);

struct wlan_network *_rtw_alloc_network(struct mlme_priv *pmlmepriv);


void _rtw_free_network(struct mlme_priv *pmlmepriv,
		       struct wlan_network *pnetwork, u8 isfreeall);
void _rtw_free_network_nolock(struct mlme_priv *pmlmepriv,
			      struct wlan_network *pnetwork);

int rtw_if_up(struct adapter *padapter);

u8 *rtw_get_capability_from_ie(u8 *ie);
u8 *rtw_get_beacon_interval_from_ie(u8 *ie);

void rtw_joinbss_reset(struct adapter *padapter);

unsigned int rtw_restructure_ht_ie(struct adapter *padapter, u8 *in_ie,
				   u8 *out_ie, uint in_len, uint *pout_len);
void rtw_update_ht_cap(struct adapter *padapter, u8 *pie, uint ie_len);
void rtw_issue_addbareq_cmd(struct adapter *padapter,
			    struct xmit_frame *pxmitframe);

int rtw_is_same_ibss(struct adapter *adapter, struct wlan_network *pnetwork);
int is_same_network(struct wlan_bssid_ex *src, struct wlan_bssid_ex *dst);

void rtw_roaming(struct adapter *padapter, struct wlan_network *tgt_network);
void _rtw_roaming(struct adapter *padapter, struct wlan_network *tgt_network);

void rtw_stassoc_hw_rpt(struct adapter *adapter, struct sta_info *psta);

#endif /* __RTL871X_MLME_H_ */
