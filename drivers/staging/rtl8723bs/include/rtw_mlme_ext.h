/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTW_MLME_EXT_H_
#define __RTW_MLME_EXT_H_


/* 	Commented by Albert 20101105 */
/* 	Increase the SURVEY_TO value from 100 to 150  (100ms to 150ms) */
/* 	The Realtek 8188CE SoftAP will spend around 100ms to send the probe response after receiving the probe request. */
/* 	So, this driver tried to extend the dwell time for each scanning channel. */
/* 	This will increase the chance to receive the probe response from SoftAP. */

#define SURVEY_TO		(100)
#define REAUTH_TO		(300) /* 50) */
#define REASSOC_TO		(300) /* 50) */
/* define DISCONNECT_TO	(3000) */
#define ADDBA_TO			(2000)

#define REAUTH_LIMIT	(4)
#define REASSOC_LIMIT	(4)

#define DYNAMIC_FUNC_DISABLE		(0x0)

/*  ====== ODM_ABILITY_E ======== */
/*  BB ODM section BIT 0-15 */
#define DYNAMIC_BB_DIG				BIT0 /* ODM_BB_DIG */
#define DYNAMIC_BB_DYNAMIC_TXPWR	BIT2 /* ODM_BB_DYNAMIC_TXPWR */
#define DYNAMIC_BB_ANT_DIV			BIT6 /* ODM_BB_ANT_DIV */

/*  RF ODM section BIT 24-31 */
#define DYNAMIC_RF_CALIBRATION		BIT26/* ODM_RF_CALIBRATION */

#define DYNAMIC_ALL_FUNC_ENABLE		0xFFFFFFF

#define _HW_STATE_NOLINK_		0x00
#define _HW_STATE_ADHOC_		0x01
#define _HW_STATE_STATION_	0x02
#define _HW_STATE_AP_			0x03


#define		_1M_RATE_	0
#define		_2M_RATE_	1
#define		_5M_RATE_	2
#define		_11M_RATE_	3
#define		_6M_RATE_	4
#define		_9M_RATE_	5
#define		_12M_RATE_	6
#define		_18M_RATE_	7
#define		_24M_RATE_	8
#define		_36M_RATE_	9
#define		_48M_RATE_	10
#define		_54M_RATE_	11

/********************************************************
MCS rate definitions
*********************************************************/
#define MCS_RATE_1R	(0x000000ff)

extern unsigned char RTW_WPA_OUI[];
extern unsigned char WMM_OUI[];
extern unsigned char WPS_OUI[];
extern unsigned char WFD_OUI[];
extern unsigned char P2P_OUI[];

extern unsigned char WMM_INFO_OUI[];
extern unsigned char WMM_PARA_OUI[];


/*  */
/*  Channel Plan Type. */
/*  Note: */
/* 	We just add new channel plan when the new channel plan is different from any of the following */
/* 	channel plan. */
/* 	If you just want to customize the actions(scan period or join actions) about one of the channel plan, */
/* 	customize them in rt_channel_info in the RT_CHANNEL_LIST. */
/*  */
enum {
	/*  old channel plan mapping ===== */
	RT_CHANNEL_DOMAIN_FCC = 0x00,
	RT_CHANNEL_DOMAIN_IC = 0x01,
	RT_CHANNEL_DOMAIN_ETSI = 0x02,
	RT_CHANNEL_DOMAIN_SPAIN = 0x03,
	RT_CHANNEL_DOMAIN_FRANCE = 0x04,
	RT_CHANNEL_DOMAIN_MKK = 0x05,
	RT_CHANNEL_DOMAIN_MKK1 = 0x06,
	RT_CHANNEL_DOMAIN_ISRAEL = 0x07,
	RT_CHANNEL_DOMAIN_TELEC = 0x08,
	RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN = 0x09,
	RT_CHANNEL_DOMAIN_WORLD_WIDE_13 = 0x0A,
	RT_CHANNEL_DOMAIN_TAIWAN = 0x0B,
	RT_CHANNEL_DOMAIN_CHINA = 0x0C,
	RT_CHANNEL_DOMAIN_SINGAPORE_INDIA_MEXICO = 0x0D,
	RT_CHANNEL_DOMAIN_KOREA = 0x0E,
	RT_CHANNEL_DOMAIN_TURKEY = 0x0F,
	RT_CHANNEL_DOMAIN_JAPAN = 0x10,
	RT_CHANNEL_DOMAIN_FCC_NO_DFS = 0x11,
	RT_CHANNEL_DOMAIN_JAPAN_NO_DFS = 0x12,
	RT_CHANNEL_DOMAIN_WORLD_WIDE_5G = 0x13,
	RT_CHANNEL_DOMAIN_TAIWAN_NO_DFS = 0x14,

	/*  new channel plan mapping, (2GDOMAIN_5GDOMAIN) ===== */
	RT_CHANNEL_DOMAIN_WORLD_NULL = 0x20,
	RT_CHANNEL_DOMAIN_ETSI1_NULL = 0x21,
	RT_CHANNEL_DOMAIN_FCC1_NULL = 0x22,
	RT_CHANNEL_DOMAIN_MKK1_NULL = 0x23,
	RT_CHANNEL_DOMAIN_ETSI2_NULL = 0x24,
	RT_CHANNEL_DOMAIN_FCC1_FCC1 = 0x25,
	RT_CHANNEL_DOMAIN_WORLD_ETSI1 = 0x26,
	RT_CHANNEL_DOMAIN_MKK1_MKK1 = 0x27,
	RT_CHANNEL_DOMAIN_WORLD_KCC1 = 0x28,
	RT_CHANNEL_DOMAIN_WORLD_FCC2 = 0x29,
	RT_CHANNEL_DOMAIN_WORLD_FCC3 = 0x30,
	RT_CHANNEL_DOMAIN_WORLD_FCC4 = 0x31,
	RT_CHANNEL_DOMAIN_WORLD_FCC5 = 0x32,
	RT_CHANNEL_DOMAIN_WORLD_FCC6 = 0x33,
	RT_CHANNEL_DOMAIN_FCC1_FCC7 = 0x34,
	RT_CHANNEL_DOMAIN_WORLD_ETSI2 = 0x35,
	RT_CHANNEL_DOMAIN_WORLD_ETSI3 = 0x36,
	RT_CHANNEL_DOMAIN_MKK1_MKK2 = 0x37,
	RT_CHANNEL_DOMAIN_MKK1_MKK3 = 0x38,
	RT_CHANNEL_DOMAIN_FCC1_NCC1 = 0x39,
	RT_CHANNEL_DOMAIN_FCC1_NCC2 = 0x40,
	RT_CHANNEL_DOMAIN_GLOBAL_NULL = 0x41,
	RT_CHANNEL_DOMAIN_ETSI1_ETSI4 = 0x42,
	RT_CHANNEL_DOMAIN_FCC1_FCC2 = 0x43,
	RT_CHANNEL_DOMAIN_FCC1_NCC3 = 0x44,
	RT_CHANNEL_DOMAIN_WORLD_ETSI5 = 0x45,
	RT_CHANNEL_DOMAIN_FCC1_FCC8 = 0x46,
	RT_CHANNEL_DOMAIN_WORLD_ETSI6 = 0x47,
	RT_CHANNEL_DOMAIN_WORLD_ETSI7 = 0x48,
	RT_CHANNEL_DOMAIN_WORLD_ETSI8 = 0x49,
	RT_CHANNEL_DOMAIN_WORLD_ETSI9 = 0x50,
	RT_CHANNEL_DOMAIN_WORLD_ETSI10 = 0x51,
	RT_CHANNEL_DOMAIN_WORLD_ETSI11 = 0x52,
	RT_CHANNEL_DOMAIN_FCC1_NCC4 = 0x53,
	RT_CHANNEL_DOMAIN_WORLD_ETSI12 = 0x54,
	RT_CHANNEL_DOMAIN_FCC1_FCC9 = 0x55,
	RT_CHANNEL_DOMAIN_WORLD_ETSI13 = 0x56,
	RT_CHANNEL_DOMAIN_FCC1_FCC10 = 0x57,
	/*  Add new channel plan above this line =============== */
	RT_CHANNEL_DOMAIN_MAX,
	RT_CHANNEL_DOMAIN_REALTEK_DEFINE = 0x7F,
};

enum {
	RT_CHANNEL_DOMAIN_2G_WORLD = 0x00,		/* Worldwird 13 */
	RT_CHANNEL_DOMAIN_2G_ETSI1 = 0x01,		/* Europe */
	RT_CHANNEL_DOMAIN_2G_FCC1 = 0x02,		/* US */
	RT_CHANNEL_DOMAIN_2G_MKK1 = 0x03,		/* Japan */
	RT_CHANNEL_DOMAIN_2G_ETSI2 = 0x04,		/* France */
	RT_CHANNEL_DOMAIN_2G_GLOBAL = 0x05,		/* Global domain */
	RT_CHANNEL_DOMAIN_2G_NULL = 0x06,
	/*  Add new channel plan above this line =============== */
	RT_CHANNEL_DOMAIN_2G_MAX,
};

#define rtw_is_channel_plan_valid(chplan) (chplan < RT_CHANNEL_DOMAIN_MAX || chplan == RT_CHANNEL_DOMAIN_REALTEK_DEFINE)

struct rt_channel_plan {
	unsigned char Channel[MAX_CHANNEL_NUM];
	unsigned char Len;
};

struct rt_channel_plan_2g {
	unsigned char Channel[MAX_CHANNEL_NUM_2G];
	unsigned char Len;
};

struct rt_channel_plan_map {
	unsigned char Index2G;
};

enum {
	HT_IOT_PEER_UNKNOWN			= 0,
	HT_IOT_PEER_REALTEK			= 1,
	HT_IOT_PEER_REALTEK_92SE		= 2,
	HT_IOT_PEER_BROADCOM		= 3,
	HT_IOT_PEER_RALINK			= 4,
	HT_IOT_PEER_ATHEROS			= 5,
	HT_IOT_PEER_CISCO				= 6,
	HT_IOT_PEER_MERU				= 7,
	HT_IOT_PEER_MARVELL			= 8,
	HT_IOT_PEER_REALTEK_SOFTAP	= 9,/*  peer is RealTek SOFT_AP, by Bohn, 2009.12.17 */
	HT_IOT_PEER_SELF_SOFTAP			= 10, /*  Self is SoftAP */
	HT_IOT_PEER_AIRGO				= 11,
	HT_IOT_PEER_INTEL				= 12,
	HT_IOT_PEER_RTK_APCLIENT		= 13,
	HT_IOT_PEER_REALTEK_81XX		= 14,
	HT_IOT_PEER_REALTEK_WOW			= 15,
	HT_IOT_PEER_REALTEK_JAGUAR_BCUTAP = 16,
	HT_IOT_PEER_REALTEK_JAGUAR_CCUTAP = 17,
	HT_IOT_PEER_MAX					= 18
};


enum {
	SCAN_DISABLE = 0,
	SCAN_START = 1,
	SCAN_TXNULL = 2,
	SCAN_PROCESS = 3,
	SCAN_COMPLETE = 4,
	SCAN_STATE_MAX,
};

struct mlme_handler {
	unsigned int   num;
	char *str;
	unsigned int (*func)(struct adapter *padapter, union recv_frame *precv_frame);
};

struct action_handler {
	unsigned int   num;
	char *str;
	unsigned int (*func)(struct adapter *padapter, union recv_frame *precv_frame);
};

struct	ss_res {
	int	state;
	int	bss_cnt;
	int	channel_idx;
	int	scan_mode;
	u8 ssid_num;
	u8 ch_num;
	struct ndis_802_11_ssid ssid[RTW_SSID_SCAN_AMOUNT];
	struct rtw_ieee80211_channel ch[RTW_CHANNEL_SCAN_AMOUNT];
};

/* define AP_MODE				0x0C */
/* define STATION_MODE	0x08 */
/* define AD_HOC_MODE		0x04 */
/* define NO_LINK_MODE	0x00 */

#define		WIFI_FW_NULL_STATE			_HW_STATE_NOLINK_
#define	WIFI_FW_STATION_STATE		_HW_STATE_STATION_
#define	WIFI_FW_AP_STATE				_HW_STATE_AP_
#define	WIFI_FW_ADHOC_STATE			_HW_STATE_ADHOC_

#define	WIFI_FW_AUTH_NULL			0x00000100
#define	WIFI_FW_AUTH_STATE			0x00000200
#define	WIFI_FW_AUTH_SUCCESS			0x00000400

#define	WIFI_FW_ASSOC_STATE			0x00002000
#define	WIFI_FW_ASSOC_SUCCESS		0x00004000

#define	WIFI_FW_LINKING_STATE		(WIFI_FW_AUTH_NULL | WIFI_FW_AUTH_STATE | WIFI_FW_AUTH_SUCCESS | WIFI_FW_ASSOC_STATE)

struct FW_Sta_Info {
	struct sta_info *psta;
	u32 status;
	u32 rx_pkt;
	u32 retry;
	NDIS_802_11_RATES_EX  SupportedRates;
};

/*
 * Usage:
 * When one iface acted as AP mode and the other iface is STA mode and scanning,
 * it should switch back to AP's operating channel periodically.
 * Parameters info:
 * When the driver scanned RTW_SCAN_NUM_OF_CH channels, it would switch back to AP's operating channel for
 * RTW_STAY_AP_CH_MILLISECOND * SURVEY_TO milliseconds.
 * Example:
 * For chip supports 2.4G and AP mode is operating in channel 1,
 * RTW_SCAN_NUM_OF_CH is 8, RTW_STAY_AP_CH_MILLISECOND is 3 and SURVEY_TO is 100.
 * When it's STA mode gets set_scan command,
 * it would
 * 1. Doing the scan on channel 1.2.3.4.5.6.7.8
 * 2. Back to channel 1 for 300 milliseconds
 * 3. Go through doing site survey on channel 9.10.11
 * 4. Back to channel 1 for 300 milliseconds
 * 5. ... and so on, till survey done.
 */
struct mlme_ext_info {
	u32 state;
	u32 reauth_count;
	u32 reassoc_count;
	u32 link_count;
	u32 auth_seq;
	u32 auth_algo;	/*  802.11 auth, could be open, shared, auto */
	u32 authModeToggle;
	u32 enc_algo;/* encrypt algorithm; */
	u32 key_index;	/*  this is only valid for legendary wep, 0~3 for key id. */
	u32 iv;
	u8 chg_txt[128];
	u16 aid;
	u16 bcn_interval;
	u16 capability;
	u8 assoc_AP_vendor;
	u8 slotTime;
	u8 preamble_mode;
	u8 WMM_enable;
	u8 ERP_enable;
	u8 ERP_IE;
	u8 HT_enable;
	u8 HT_caps_enable;
	u8 HT_info_enable;
	u8 HT_protection;
	u8 turboMode_cts2self;
	u8 turboMode_rtsen;
	u8 SM_PS;
	u8 agg_enable_bitmap;
	u8 ADDBA_retry_count;
	u8 candidate_tid_bitmap;
	u8 dialogToken;
	/*  Accept ADDBA Request */
	bool accept_addba_req;
	u8 bwmode_updated;
	u8 hidden_ssid_mode;
	u8 VHT_enable;

	struct ADDBA_request		ADDBA_req;
	struct WMM_para_element	WMM_param;
	struct HT_caps_element	HT_caps;
	struct HT_info_element		HT_info;
	struct wlan_bssid_ex			network;/* join network or bss_network, if in ap mode, it is the same to cur_network.network */
	struct FW_Sta_Info		FW_sta_info[NUM_STA];
};

/*  The channel information about this channel including joining, scanning, and power constraints. */
struct rt_channel_info {
	u8 		ChannelNum;		/*  The channel number. */
	enum rt_scan_type	ScanType;		/*  Scan type such as passive or active scan. */
};

int rtw_ch_set_search_ch(struct rt_channel_info *ch_set, const u32 ch);

/*  P2P_MAX_REG_CLASSES - Maximum number of regulatory classes */
#define P2P_MAX_REG_CLASSES 10

/*  P2P_MAX_REG_CLASS_CHANNELS - Maximum number of channels per regulatory class */
#define P2P_MAX_REG_CLASS_CHANNELS 20

/*   struct p2p_channels - List of supported channels */
struct p2p_channels {
	/*  struct p2p_reg_class - Supported regulatory class */
	struct p2p_reg_class {
		/*  reg_class - Regulatory class (IEEE 802.11-2007, Annex J) */
		u8 reg_class;

		/*  channel - Supported channels */
		u8 channel[P2P_MAX_REG_CLASS_CHANNELS];

		/*  channels - Number of channel entries in use */
		size_t channels;
	} reg_class[P2P_MAX_REG_CLASSES];

	/*  reg_classes - Number of reg_class entries in use */
	size_t reg_classes;
};

struct p2p_oper_class_map {
	enum hw_mode {IEEE80211G, IEEE80211A} mode;
	u8 op_class;
	u8 min_chan;
	u8 max_chan;
	u8 inc;
	enum { BW20, BW40PLUS, BW40MINUS } bw;
};

struct mlme_ext_priv {
	struct adapter	*padapter;
	u8 mlmeext_init;
	atomic_t		event_seq;
	u16 mgnt_seq;
	u16 sa_query_seq;
	u64 mgnt_80211w_IPN;
	u64 mgnt_80211w_IPN_rx;
	/* struct fw_priv fwpriv; */

	unsigned char cur_channel;
	unsigned char cur_bwmode;
	unsigned char cur_ch_offset;/* PRIME_CHNL_OFFSET */
	unsigned char cur_wireless_mode;	/*  NETWORK_TYPE */

	unsigned char max_chan_nums;
	struct rt_channel_info		channel_set[MAX_CHANNEL_NUM];
	struct p2p_channels channel_list;
	unsigned char basicrate[NumRates];
	unsigned char datarate[NumRates];
	unsigned char default_supported_mcs_set[16];

	struct ss_res		sitesurvey_res;
	struct mlme_ext_info mlmext_info;/* for sta/adhoc mode, including current scanning/connecting/connected related info. */
                                                     /* for ap mode, network includes ap's cap_info */
	struct timer_list		survey_timer;
	struct timer_list		link_timer;
	struct timer_list		sa_query_timer;
	/* struct timer_list		ADDBA_timer; */
	u16 		chan_scan_time;
	unsigned long last_scan_time;
	u8 scan_abort;
	u8 tx_rate; /*  TXRATE when USERATE is set. */

	u32 retry; /* retry for issue probereq */

	u64 TSFValue;

	/* for LPS-32K to adaptive bcn early and timeout */
	u8 adaptive_tsf_done;
	u32 bcn_delay_cnt[9];
	u32 bcn_delay_ratio[9];
	u32 bcn_cnt;
	u8 DrvBcnEarly;
	u8 DrvBcnTimeOut;

	unsigned char bstart_bss;

	u8 update_channel_plan_by_ap_done;

	/* recv_decache check for Action_public frame */
	u8 action_public_dialog_token;
	u16  action_public_rxseq;

	u8 active_keep_alive_check;
#ifdef DBG_FIXED_CHAN
	u8 fixed_chan;
#endif

};

void init_mlme_default_rate_set(struct adapter *padapter);
void init_mlme_ext_priv(struct adapter *padapter);
int init_hw_mlme_ext(struct adapter *padapter);
void free_mlme_ext_priv(struct mlme_ext_priv *pmlmeext);
extern void init_mlme_ext_timer(struct adapter *padapter);
extern void init_addba_retry_timer(struct adapter *padapter, struct sta_info *psta);
extern struct xmit_frame *alloc_mgtxmitframe(struct xmit_priv *pxmitpriv);

/* void fill_fwpriv(struct adapter *padapter, struct fw_priv *pfwpriv); */

u8 networktype_to_raid_ex(struct adapter *adapter, struct sta_info *psta);

void get_rate_set(struct adapter *padapter, unsigned char *pbssrate, int *bssrate_len);
void set_mcs_rate_by_mask(u8 *mcs_set, u32 mask);
void UpdateBrateTbl(struct adapter *padapter, u8 *mBratesOS);
void UpdateBrateTblForSoftAP(u8 *bssrateset, u32 bssratelen);

void Save_DM_Func_Flag(struct adapter *padapter);
void Restore_DM_Func_Flag(struct adapter *padapter);
void Switch_DM_Func(struct adapter *padapter, u32 mode, u8 enable);

void Set_MSR(struct adapter *padapter, u8 type);

u8 rtw_get_oper_ch(struct adapter *adapter);
void rtw_set_oper_ch(struct adapter *adapter, u8 ch);
u8 rtw_get_oper_bw(struct adapter *adapter);
void rtw_set_oper_bw(struct adapter *adapter, u8 bw);
u8 rtw_get_oper_choffset(struct adapter *adapter);
void rtw_set_oper_choffset(struct adapter *adapter, u8 offset);
u8 rtw_get_center_ch(u8 channel, u8 chnl_bw, u8 chnl_offset);
unsigned long rtw_get_on_cur_ch_time(struct adapter *adapter);

void set_channel_bwmode(struct adapter *padapter, unsigned char channel, unsigned char channel_offset, unsigned short bwmode);
void SelectChannel(struct adapter *padapter, unsigned char channel);

unsigned int decide_wait_for_beacon_timeout(unsigned int bcn_interval);

void read_cam(struct adapter *padapter, u8 entry, u8 *get_key);

/* modify HW only */
void _write_cam(struct adapter *padapter, u8 entry, u16 ctrl, u8 *mac, u8 *key);
void _clear_cam_entry(struct adapter *padapter, u8 entry);

/* modify both HW and cache */
void write_cam(struct adapter *padapter, u8 id, u16 ctrl, u8 *mac, u8 *key);
void clear_cam_entry(struct adapter *padapter, u8 id);

/* modify cache only */
void write_cam_cache(struct adapter *adapter, u8 id, u16 ctrl, u8 *mac, u8 *key);
void clear_cam_cache(struct adapter *adapter, u8 id);

void invalidate_cam_all(struct adapter *padapter);


int allocate_fw_sta_entry(struct adapter *padapter);
void flush_all_cam_entry(struct adapter *padapter);

void site_survey(struct adapter *padapter);
u8 collect_bss_info(struct adapter *padapter, union recv_frame *precv_frame, struct wlan_bssid_ex *bssid);
void update_network(struct wlan_bssid_ex *dst, struct wlan_bssid_ex *src, struct adapter *padapter, bool update_ie);

u8 *get_my_bssid(struct wlan_bssid_ex *pnetwork);
u16 get_beacon_interval(struct wlan_bssid_ex *bss);

int is_client_associated_to_ap(struct adapter *padapter);
int is_client_associated_to_ibss(struct adapter *padapter);
int is_IBSS_empty(struct adapter *padapter);

unsigned char check_assoc_AP(u8 *pframe, uint len);

int WMM_param_handler(struct adapter *padapter, struct ndis_80211_var_ie *pIE);
void WMMOnAssocRsp(struct adapter *padapter);

void HT_caps_handler(struct adapter *padapter, struct ndis_80211_var_ie *pIE);
void HT_info_handler(struct adapter *padapter, struct ndis_80211_var_ie *pIE);
void HTOnAssocRsp(struct adapter *padapter);

void ERP_IE_handler(struct adapter *padapter, struct ndis_80211_var_ie *pIE);
void VCS_update(struct adapter *padapter, struct sta_info *psta);
void update_ldpc_stbc_cap(struct sta_info *psta);

void update_beacon_info(struct adapter *padapter, u8 *pframe, uint len, struct sta_info *psta);
int rtw_check_bcn_info(struct adapter *Adapter, u8 *pframe, u32 packet_len);
void update_IOT_info(struct adapter *padapter);
void update_capinfo(struct adapter *Adapter, u16 updateCap);
void update_wireless_mode(struct adapter *padapter);
void update_sta_basic_rate(struct sta_info *psta, u8 wireless_mode);
int update_sta_support_rate(struct adapter *padapter, u8 *pvar_ie, uint var_ie_len, int cam_idx);

/* for sta/adhoc mode */
void update_sta_info(struct adapter *padapter, struct sta_info *psta);
void Update_RA_Entry(struct adapter *padapter, struct sta_info *psta);
void set_sta_rate(struct adapter *padapter, struct sta_info *psta);

unsigned int receive_disconnect(struct adapter *padapter, unsigned char *MacAddr, unsigned short reason);

unsigned char get_highest_rate_idx(u32 mask);
int support_short_GI(struct adapter *padapter, struct HT_caps_element *pHT_caps, u8 bwmode);
unsigned int is_ap_in_tkip(struct adapter *padapter);

s16 rtw_camid_search(struct adapter *adapter, u8 *addr, s16 kid);
s16 rtw_camid_alloc(struct adapter *adapter, struct sta_info *sta, u8 kid);
void rtw_camid_free(struct adapter *adapter, u8 cam_id);

extern void rtw_alloc_macid(struct adapter *padapter, struct sta_info *psta);
extern void rtw_release_macid(struct adapter *padapter, struct sta_info *psta);
extern u8 rtw_search_max_mac_id(struct adapter *padapter);

void report_join_res(struct adapter *padapter, int res);
void report_survey_event(struct adapter *padapter, union recv_frame *precv_frame);
void report_surveydone_event(struct adapter *padapter);
void report_del_sta_event(struct adapter *padapter, unsigned char *MacAddr, unsigned short reason);
void report_add_sta_event(struct adapter *padapter, unsigned char *MacAddr, int cam_idx);
void report_wmm_edca_update(struct adapter *padapter);

u8 chk_bmc_sleepq_cmd(struct adapter *padapter);
extern u8 set_tx_beacon_cmd(struct adapter *padapter);
unsigned int setup_beacon_frame(struct adapter *padapter, unsigned char *beacon_frame);
void update_mgnt_tx_rate(struct adapter *padapter, u8 rate);
void update_mgntframe_attrib(struct adapter *padapter, struct pkt_attrib *pattrib);
void update_mgntframe_attrib_addr(struct adapter *padapter, struct xmit_frame *pmgntframe);
void dump_mgntframe(struct adapter *padapter, struct xmit_frame *pmgntframe);
s32 dump_mgntframe_and_wait(struct adapter *padapter, struct xmit_frame *pmgntframe, int timeout_ms);
s32 dump_mgntframe_and_wait_ack(struct adapter *padapter, struct xmit_frame *pmgntframe);

void issue_beacon(struct adapter *padapter, int timeout_ms);
void issue_probersp(struct adapter *padapter, unsigned char *da, u8 is_valid_p2p_probereq);
void issue_assocreq(struct adapter *padapter);
void issue_asocrsp(struct adapter *padapter, unsigned short status, struct sta_info *pstat, int pkt_type);
void issue_auth(struct adapter *padapter, struct sta_info *psta, unsigned short status);
void issue_probereq(struct adapter *padapter, struct ndis_802_11_ssid *pssid, u8 *da);
s32 issue_probereq_ex(struct adapter *padapter, struct ndis_802_11_ssid *pssid, u8 *da, u8 ch, bool append_wps, int try_cnt, int wait_ms);
int issue_nulldata(struct adapter *padapter, unsigned char *da, unsigned int power_mode, int try_cnt, int wait_ms);
s32 issue_nulldata_in_interrupt(struct adapter *padapter, u8 *da);
int issue_qos_nulldata(struct adapter *padapter, unsigned char *da, u16 tid, int try_cnt, int wait_ms);
int issue_deauth(struct adapter *padapter, unsigned char *da, unsigned short reason);
int issue_deauth_ex(struct adapter *padapter, u8 *da, unsigned short reason, int try_cnt, int wait_ms);
void issue_action_BA(struct adapter *padapter, unsigned char *raddr, unsigned char action, unsigned short status);
void issue_action_SA_Query(struct adapter *padapter, unsigned char *raddr, unsigned char action, unsigned short tid);
unsigned int send_delba(struct adapter *padapter, u8 initiator, u8 *addr);
unsigned int send_beacon(struct adapter *padapter);

void start_clnt_assoc(struct adapter *padapter);
void start_clnt_auth(struct adapter *padapter);
void start_clnt_join(struct adapter *padapter);
void start_create_ibss(struct adapter *padapter);

unsigned int OnAssocReq(struct adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAssocRsp(struct adapter *padapter, union recv_frame *precv_frame);
unsigned int OnProbeReq(struct adapter *padapter, union recv_frame *precv_frame);
unsigned int OnProbeRsp(struct adapter *padapter, union recv_frame *precv_frame);
unsigned int DoReserved(struct adapter *padapter, union recv_frame *precv_frame);
unsigned int OnBeacon(struct adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAtim(struct adapter *padapter, union recv_frame *precv_frame);
unsigned int OnDisassoc(struct adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAuth(struct adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAuthClient(struct adapter *padapter, union recv_frame *precv_frame);
unsigned int OnDeAuth(struct adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction(struct adapter *padapter, union recv_frame *precv_frame);

unsigned int on_action_spct(struct adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction_back(struct adapter *padapter, union recv_frame *precv_frame);
unsigned int on_action_public(struct adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction_ht(struct adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction_sa_query(struct adapter *padapter, union recv_frame *precv_frame);

void mlmeext_joinbss_event_callback(struct adapter *padapter, int join_res);
void mlmeext_sta_del_event_callback(struct adapter *padapter);
void mlmeext_sta_add_event_callback(struct adapter *padapter, struct sta_info *psta);

void linked_status_chk(struct adapter *padapter);

void _linked_info_dump(struct adapter *padapter);

void survey_timer_hdl(struct timer_list *t);
void link_timer_hdl(struct timer_list *t);
void addba_timer_hdl(struct timer_list *t);
void sa_query_timer_hdl(struct timer_list *t);
/* void reauth_timer_hdl(struct adapter *padapter); */
/* void reassoc_timer_hdl(struct adapter *padapter); */

#define set_survey_timer(mlmeext, ms) \
	do { \
		_set_timer(&(mlmeext)->survey_timer, (ms)); \
	} while (0)

#define set_link_timer(mlmeext, ms) \
	do { \
		_set_timer(&(mlmeext)->link_timer, (ms)); \
	} while (0)
#define set_sa_query_timer(mlmeext, ms) \
	do { \
		_set_timer(&(mlmeext)->sa_query_timer, (ms)); \
	} while (0)

extern void process_addba_req(struct adapter *padapter, u8 *paddba_req, u8 *addr);

extern void update_TSF(struct mlme_ext_priv *pmlmeext, u8 *pframe, uint len);
extern void correct_TSF(struct adapter *padapter, struct mlme_ext_priv *pmlmeext);
extern void adaptive_early_32k(struct mlme_ext_priv *pmlmeext, u8 *pframe, uint len);
extern u8 traffic_status_watchdog(struct adapter *padapter, u8 from_timer);

int rtw_chk_start_clnt_join(struct adapter *padapter, u8 *ch, u8 *bw, u8 *offset);

struct cmd_hdl {
	uint	parmsize;
	u8 (*h2cfuns)(struct adapter *padapter, u8 *pbuf);
};


u8 read_macreg_hdl(struct adapter *padapter, u8 *pbuf);
u8 write_macreg_hdl(struct adapter *padapter, u8 *pbuf);
u8 read_bbreg_hdl(struct adapter *padapter, u8 *pbuf);
u8 write_bbreg_hdl(struct adapter *padapter, u8 *pbuf);
u8 read_rfreg_hdl(struct adapter *padapter, u8 *pbuf);
u8 write_rfreg_hdl(struct adapter *padapter, u8 *pbuf);


u8 NULL_hdl(struct adapter *padapter, u8 *pbuf);
u8 join_cmd_hdl(struct adapter *padapter, u8 *pbuf);
u8 disconnect_hdl(struct adapter *padapter, u8 *pbuf);
u8 createbss_hdl(struct adapter *padapter, u8 *pbuf);
u8 setopmode_hdl(struct adapter *padapter, u8 *pbuf);
u8 sitesurvey_cmd_hdl(struct adapter *padapter, u8 *pbuf);
u8 setauth_hdl(struct adapter *padapter, u8 *pbuf);
u8 setkey_hdl(struct adapter *padapter, u8 *pbuf);
u8 set_stakey_hdl(struct adapter *padapter, u8 *pbuf);
u8 set_assocsta_hdl(struct adapter *padapter, u8 *pbuf);
u8 del_assocsta_hdl(struct adapter *padapter, u8 *pbuf);
u8 add_ba_hdl(struct adapter *padapter, unsigned char *pbuf);

u8 mlme_evt_hdl(struct adapter *padapter, unsigned char *pbuf);
u8 h2c_msg_hdl(struct adapter *padapter, unsigned char *pbuf);
u8 chk_bmc_sleepq_hdl(struct adapter *padapter, unsigned char *pbuf);
u8 tx_beacon_hdl(struct adapter *padapter, unsigned char *pbuf);
u8 set_ch_hdl(struct adapter *padapter, u8 *pbuf);
u8 set_chplan_hdl(struct adapter *padapter, unsigned char *pbuf);
u8 set_csa_hdl(struct adapter *padapter, unsigned char *pbuf);	/* Kurt: Handling DFS channel switch announcement ie. */
u8 tdls_hdl(struct adapter *padapter, unsigned char *pbuf);
u8 run_in_thread_hdl(struct adapter *padapter, u8 *pbuf);


#define GEN_DRV_CMD_HANDLER(size, cmd)	{size, &cmd ## _hdl},
#define GEN_MLME_EXT_HANDLER(size, cmd)	{size, cmd},

struct C2HEvent_Header {

#ifdef __LITTLE_ENDIAN

	unsigned int len:16;
	unsigned int ID:8;
	unsigned int seq:8;
#else
	unsigned int seq:8;
	unsigned int ID:8;
	unsigned int len:16;
#endif
	unsigned int rsvd;
};

void rtw_dummy_event_callback(struct adapter *adapter, u8 *pbuf);
void rtw_fwdbg_event_callback(struct adapter *adapter, u8 *pbuf);

enum {
	GEN_EVT_CODE(_Read_MACREG) = 0, /*0*/
	GEN_EVT_CODE(_Read_BBREG),
	GEN_EVT_CODE(_Read_RFREG),
	GEN_EVT_CODE(_Read_EEPROM),
	GEN_EVT_CODE(_Read_EFUSE),
	GEN_EVT_CODE(_Read_CAM),			/*5*/
	GEN_EVT_CODE(_Get_BasicRate),
	GEN_EVT_CODE(_Get_DataRate),
	GEN_EVT_CODE(_Survey),	 /*8*/
	GEN_EVT_CODE(_SurveyDone),	 /*9*/

	GEN_EVT_CODE(_JoinBss), /*10*/
	GEN_EVT_CODE(_AddSTA),
	GEN_EVT_CODE(_DelSTA),
	GEN_EVT_CODE(_AtimDone),
	GEN_EVT_CODE(_TX_Report),
	GEN_EVT_CODE(_CCX_Report),			/*15*/
	GEN_EVT_CODE(_DTM_Report),
	GEN_EVT_CODE(_TX_Rate_Statistics),
	GEN_EVT_CODE(_C2HLBK),
	GEN_EVT_CODE(_FWDBG),
	GEN_EVT_CODE(_C2HFEEDBACK),               /*20*/
	GEN_EVT_CODE(_ADDBA),
	GEN_EVT_CODE(_C2HBCN),
	GEN_EVT_CODE(_ReportPwrState),		/* filen: only for PCIE, USB */
	GEN_EVT_CODE(_CloseRF),				/* filen: only for PCIE, work around ASPM */
	GEN_EVT_CODE(_WMM),					/*25*/
	MAX_C2HEVT
};


#ifdef _RTW_MLME_EXT_C_

#endif/* _RTL8192C_CMD_C_ */

#endif
