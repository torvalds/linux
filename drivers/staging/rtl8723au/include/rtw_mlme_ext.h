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
#ifndef __RTW_MLME_EXT_H_
#define __RTW_MLME_EXT_H_

#include <osdep_service.h>
#include <drv_types.h>
#include <wlan_bssdef.h>


/*	Commented by Albert 20101105 */
/*	Increase the SURVEY_TO value from 100 to 150  ( 100ms to 150ms ) */
/*	The Realtek 8188CE SoftAP will spend around 100ms to send the probe response after receiving the probe request. */
/*	So, this driver tried to extend the dwell time for each scanning channel. */
/*	This will increase the chance to receive the probe response from SoftAP. */

#define SURVEY_TO		(100)
#define REAUTH_TO		(300) /* 50) */
#define REASSOC_TO		(300) /* 50) */
/* define DISCONNECT_TO	(3000) */
#define ADDBA_TO			(2000)

#define LINKED_TO (1) /* unit:2 sec, 1x2=2 sec */

#define REAUTH_LIMIT	(4)
#define REASSOC_LIMIT	(4)
#define READDBA_LIMIT	(2)

#define ROAMING_LIMIT	8

#define	DYNAMIC_FUNC_DISABLE			(0x0)

/*  ====== enum odm_ability ======== */
/*  BB ODM section BIT 0-15 */
#define	DYNAMIC_BB_DIG				BIT(0)
#define	DYNAMIC_BB_RA_MASK			BIT(1)
#define	DYNAMIC_BB_DYNAMIC_TXPWR	BIT(2)
#define	DYNAMIC_BB_BB_FA_CNT			BIT(3)

#define		DYNAMIC_BB_RSSI_MONITOR		BIT(4)
#define		DYNAMIC_BB_CCK_PD			BIT(5)
#define		DYNAMIC_BB_ANT_DIV			BIT(6)
#define		DYNAMIC_BB_PWR_SAVE			BIT(7)
#define		DYNAMIC_BB_PWR_TRAIN			BIT(8)
#define		DYNAMIC_BB_RATE_ADAPTIVE		BIT(9)
#define		DYNAMIC_BB_PATH_DIV			BIT(10)
#define		DYNAMIC_BB_PSD				BIT(11)

/*  MAC DM section BIT 16-23 */
#define		DYNAMIC_MAC_struct edca_turboURBO		BIT(16)
#define		DYNAMIC_MAC_EARLY_MODE		BIT(17)

/*  RF ODM section BIT 24-31 */
#define		DYNAMIC_RF_TX_PWR_TRACK		BIT(24)
#define		DYNAMIC_RF_RX_GAIN_TRACK		BIT(25)
#define		DYNAMIC_RF_CALIBRATION		BIT(26)

#define		DYNAMIC_ALL_FUNC_ENABLE		0xFFFFFFF

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


extern unsigned char RTW_WPA_OUI23A[];
extern unsigned char WMM_OUI23A[];
extern unsigned char WPS_OUI23A[];
extern unsigned char WFD_OUI23A[];
extern unsigned char P2P_OUI23A[];

extern unsigned char WMM_INFO_OUI23A[];
extern unsigned char WMM_PARA_OUI23A[];


/*  */
/*  Channel Plan Type. */
/*  Note: */
/*	We just add new channel plan when the new channel plan is different from any of the following */
/*	channel plan. */
/*	If you just wnat to customize the acitions(scan period or join actions) about one of the channel plan, */
/*	customize them in struct rt_channel_info in the RT_CHANNEL_LIST. */
/*  */
enum  { /* _RT_CHANNEL_DOMAIN */
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
	RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN_2G = 0x41,
	/*  Add new channel plan above this line=============== */
	RT_CHANNEL_DOMAIN_MAX,
	RT_CHANNEL_DOMAIN_REALTEK_DEFINE = 0x7F,
};

enum { /* _RT_CHANNEL_DOMAIN_2G */
	RT_CHANNEL_DOMAIN_2G_WORLD = 0x00,		/* Worldwird 13 */
	RT_CHANNEL_DOMAIN_2G_ETSI1 = 0x01,		/* Europe */
	RT_CHANNEL_DOMAIN_2G_FCC1 = 0x02,		/* US */
	RT_CHANNEL_DOMAIN_2G_MKK1 = 0x03,		/* Japan */
	RT_CHANNEL_DOMAIN_2G_ETSI2 = 0x04,		/* France */
	RT_CHANNEL_DOMAIN_2G_NULL = 0x05,
	/*  Add new channel plan above this line=============== */
	RT_CHANNEL_DOMAIN_2G_MAX,
};

enum { /* _RT_CHANNEL_DOMAIN_5G */
	RT_CHANNEL_DOMAIN_5G_NULL = 0x00,
	RT_CHANNEL_DOMAIN_5G_ETSI1 = 0x01,		/* Europe */
	RT_CHANNEL_DOMAIN_5G_ETSI2 = 0x02,		/* Australia, New Zealand */
	RT_CHANNEL_DOMAIN_5G_ETSI3 = 0x03,		/* Russia */
	RT_CHANNEL_DOMAIN_5G_FCC1 = 0x04,		/* US */
	RT_CHANNEL_DOMAIN_5G_FCC2 = 0x05,		/* FCC o/w DFS Channels */
	RT_CHANNEL_DOMAIN_5G_FCC3 = 0x06,		/* India, Mexico */
	RT_CHANNEL_DOMAIN_5G_FCC4 = 0x07,		/* Venezuela */
	RT_CHANNEL_DOMAIN_5G_FCC5 = 0x08,		/* China */
	RT_CHANNEL_DOMAIN_5G_FCC6 = 0x09,		/* Israel */
	RT_CHANNEL_DOMAIN_5G_FCC7_IC1 = 0x0A,	/* US, Canada */
	RT_CHANNEL_DOMAIN_5G_KCC1 = 0x0B,		/* Korea */
	RT_CHANNEL_DOMAIN_5G_MKK1 = 0x0C,		/* Japan */
	RT_CHANNEL_DOMAIN_5G_MKK2 = 0x0D,		/* Japan (W52, W53) */
	RT_CHANNEL_DOMAIN_5G_MKK3 = 0x0E,		/* Japan (W56) */
	RT_CHANNEL_DOMAIN_5G_NCC1 = 0x0F,		/* Taiwan */
	RT_CHANNEL_DOMAIN_5G_NCC2 = 0x10,		/* Taiwan o/w DFS */
	/*  Add new channel plan above this line=============== */
	/*  Driver Self Defined ===== */
	RT_CHANNEL_DOMAIN_5G_FCC = 0x11,
	RT_CHANNEL_DOMAIN_5G_JAPAN_NO_DFS = 0x12,
	RT_CHANNEL_DOMAIN_5G_FCC4_NO_DFS = 0x13,
	RT_CHANNEL_DOMAIN_5G_MAX,
};

#define rtw_is_channel_plan_valid(chplan) (chplan<RT_CHANNEL_DOMAIN_MAX || chplan == RT_CHANNEL_DOMAIN_REALTEK_DEFINE)

struct rt_channel_plan {
	unsigned char	Channel[MAX_CHANNEL_NUM];
	unsigned char	Len;
};

struct rt_channel_plan_2g {
	unsigned char	Channel[MAX_CHANNEL_NUM_2G];
	unsigned char	Len;
};

struct rt_channel_plan_5g {
	unsigned char	Channel[MAX_CHANNEL_NUM_5G];
	unsigned char	Len;
};

struct rt_channel_plan_map {
	unsigned char	Index2G;
	unsigned char	Index5G;
};

enum Associated_AP {
	atherosAP	= 0,
	broadcomAP	= 1,
	ciscoAP		= 2,
	marvellAP	= 3,
	ralinkAP	= 4,
	realtekAP	= 5,
	airgocapAP	= 6,
	unknownAP	= 7,
	maxAP,
};

enum { /* HT_IOT_PEER_E */
	HT_IOT_PEER_UNKNOWN		= 0,
	HT_IOT_PEER_REALTEK		= 1,
	HT_IOT_PEER_REALTEK_92SE	= 2,
	HT_IOT_PEER_BROADCOM		= 3,
	HT_IOT_PEER_RALINK		= 4,
	HT_IOT_PEER_ATHEROS		= 5,
	HT_IOT_PEER_CISCO		= 6,
	HT_IOT_PEER_MERU		= 7,
	HT_IOT_PEER_MARVELL		= 8,
	HT_IOT_PEER_REALTEK_SOFTAP	= 9,/*  peer is RealTek SOFT_AP, by Bohn, 2009.12.17 */
	HT_IOT_PEER_SELF_SOFTAP		= 10, /*  Self is SoftAP */
	HT_IOT_PEER_AIRGO		= 11,
	HT_IOT_PEER_INTEL		= 12,
	HT_IOT_PEER_RTK_APCLIENT	= 13,
	HT_IOT_PEER_REALTEK_81XX	= 14,
	HT_IOT_PEER_REALTEK_WOW		= 15,
	HT_IOT_PEER_TENDA		= 16,
	HT_IOT_PEER_MAX			= 17
};

enum SCAN_STATE {
	SCAN_DISABLE = 0,
	SCAN_START = 1,
	SCAN_TXNULL = 2,
	SCAN_PROCESS = 3,
	SCAN_COMPLETE = 4,
	SCAN_STATE_MAX,
};

struct mlme_handler {
	char *str;
	unsigned int (*func)(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
};

struct action_handler {
	unsigned int   num;
	char* str;
	unsigned int (*func)(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
};

struct	ss_res
{
	int	state;
	int	bss_cnt;
	int	channel_idx;
	int	scan_mode;
	u8 ssid_num;
	u8 ch_num;
	struct cfg80211_ssid ssid[RTW_SSID_SCAN_AMOUNT];
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

#define	WIFI_FW_LINKING_STATE		(WIFI_FW_AUTH_NULL | WIFI_FW_AUTH_STATE | WIFI_FW_AUTH_SUCCESS |WIFI_FW_ASSOC_STATE)

struct FW_Sta_Info {
	struct sta_info	*psta;
	u32	status;
	u32	rx_pkt;
	u32	retry;
	unsigned char SupportedRates[NDIS_802_11_LENGTH_RATES_EX];
};

/*
 * Usage:
 * When one iface acted as AP mode and the other iface is STA mode and scanning,
 * it should switch back to AP's operating channel periodically.
 * Parameters info:
 * When the driver scanned RTW_SCAN_NUM_OF_CH channels, it would switch back to AP's operating channel for
 * RTW_STAY_AP_CH_MILLISECOND * SURVEY_TO milliseconds.
 * Example:
 * For chip supports 2.4G + 5GHz and AP mode is operating in channel 1,
 * RTW_SCAN_NUM_OF_CH is 8, RTW_STAY_AP_CH_MILLISECOND is 3 and SURVEY_TO is 100.
 * When it's STA mode gets set_scan command,
 * it would
 * 1. Doing the scan on channel 1.2.3.4.5.6.7.8
 * 2. Back to channel 1 for 300 milliseconds
 * 3. Go through doing site survey on channel 9.10.11.36.40.44.48.52
 * 4. Back to channel 1 for 300 milliseconds
 * 5. ... and so on, till survey done.
 */

struct mlme_ext_info
{
	u32	state;
	u32	reauth_count;
	u32	reassoc_count;
	u32	link_count;
	u32	auth_seq;
	u32	auth_algo;	/*  802.11 auth, could be open, shared, auto */
	u32	authModeToggle;
	u32	enc_algo;/* encrypt algorithm; */
	u32	key_index;	/*  this is only valid for legendary wep, 0~3 for key id. */
	u32	iv;
	u8	chg_txt[128];
	u16	aid;
	u16	bcn_interval;
	u16	capability;
	u8	assoc_AP_vendor;
	u8	slotTime;
	u8	preamble_mode;
	u8	WMM_enable;
	u8	ERP_enable;
	u8	ERP_IE;
	u8	HT_enable;
	u8	HT_caps_enable;
	u8	HT_info_enable;
	u8	HT_protection;
	u8	turboMode_cts2self;
	u8	turboMode_rtsen;
	u8	SM_PS;
	u8	agg_enable_bitmap;
	u8	ADDBA_retry_count;
	u8	candidate_tid_bitmap;
	u8	dialogToken;
	/*  Accept ADDBA Request */
	bool bAcceptAddbaReq;
	u8	bwmode_updated;
	u8	hidden_ssid_mode;

	struct ADDBA_request		ADDBA_req;
	struct WMM_para_element	WMM_param;
	struct HT_caps_element	HT_caps;
	struct HT_info_element		HT_info;
	struct wlan_bssid_ex			network;/* join network or bss_network, if in ap mode, it is the same to cur_network.network */
	struct FW_Sta_Info		FW_sta_info[NUM_STA];
};

/*  The channel information about this channel including joining, scanning, and power constraints. */
struct rt_channel_info {
	u8		ChannelNum;		/*  The channel number. */
	enum rt_scan_type ScanType;		/*  Scan type such as passive or active scan. */
};

int rtw_ch_set_search_ch23a(struct rt_channel_info *ch_set, const u32 ch);

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
	enum hw_mode {IEEE80211G,IEEE80211A} mode;
	u8 op_class;
	u8 min_chan;
	u8 max_chan;
	u8 inc;
	enum {
		BW20, BW40PLUS, BW40MINUS
	} bw;
};

struct mlme_ext_priv {
	struct rtw_adapter	*padapter;
	u8	mlmeext_init;
	atomic_t		event_seq;
	u16	mgnt_seq;

	/* struct fw_priv	fwpriv; */

	unsigned char	cur_channel;
	unsigned char	cur_bwmode;
	unsigned char	cur_ch_offset;/* PRIME_CHNL_OFFSET */
	unsigned char	cur_wireless_mode;	/*  NETWORK_TYPE */

	unsigned char	max_chan_nums;
	struct rt_channel_info		channel_set[MAX_CHANNEL_NUM];
	struct p2p_channels channel_list;
	unsigned char	basicrate[NumRates];
	unsigned char	datarate[NumRates];

	struct ss_res		sitesurvey_res;
	struct mlme_ext_info	mlmext_info;/* for sta/adhoc mode, including current scanning/connecting/connected related info. */
                                                     /* for ap mode, network includes ap's cap_info */
	struct timer_list		survey_timer;
	struct timer_list		link_timer;
	u16			chan_scan_time;

	u8	scan_abort;
	u8	tx_rate; /*  TXRATE when USERATE is set. */

	u32	retry; /* retry for issue probereq */

	u64 TSFValue;

	unsigned char bstart_bss;
	u8 update_channel_plan_by_ap_done;
	/* recv_decache check for Action_public frame */
	u8 action_public_dialog_token;
	u16	 action_public_rxseq;
	u8 active_keep_alive_check;
};

int init_mlme_ext_priv23a(struct rtw_adapter* padapter);
int init_hw_mlme_ext23a(struct rtw_adapter *padapter);
void free_mlme_ext_priv23a (struct mlme_ext_priv *pmlmeext);
void init_mlme_ext_timer23a(struct rtw_adapter *padapter);
void init_addba_retry_timer23a(struct sta_info *psta);
struct xmit_frame *alloc_mgtxmitframe23a(struct xmit_priv *pxmitpriv);

unsigned char networktype_to_raid23a(unsigned char network_type);
u8 judge_network_type23a(struct rtw_adapter *padapter, unsigned char *rate,
		      int ratelen);
void get_rate_set23a(struct rtw_adapter *padapter, unsigned char *pbssrate,
		  int *bssrate_len);
void UpdateBrateTbl23a(struct rtw_adapter *padapter,u8 *mBratesOS);
void Update23aTblForSoftAP(u8 *bssrateset, u32 bssratelen);

void Save_DM_Func_Flag23a(struct rtw_adapter *padapter);
void Restore_DM_Func_Flag23a(struct rtw_adapter *padapter);
void Switch_DM_Func23a(struct rtw_adapter *padapter, unsigned long mode, u8 enable);

void Set_MSR23a(struct rtw_adapter *padapter, u8 type);

u8 rtw_get_oper_ch23a(struct rtw_adapter *adapter);
void rtw_set_oper_ch23a(struct rtw_adapter *adapter, u8 ch);
u8 rtw_get_oper_bw23a(struct rtw_adapter *adapter);
void rtw_set_oper_bw23a(struct rtw_adapter *adapter, u8 bw);
u8 rtw_get_oper_ch23aoffset(struct rtw_adapter *adapter);
void rtw_set_oper_ch23aoffset23a(struct rtw_adapter *adapter, u8 offset);

void set_channel_bwmode23a(struct rtw_adapter *padapter, unsigned char channel,
			unsigned char channel_offset, unsigned short bwmode);
void SelectChannel23a(struct rtw_adapter *padapter, unsigned char channel);
void SetBWMode23a(struct rtw_adapter *padapter, unsigned short bwmode,
	       unsigned char channel_offset);

unsigned int decide_wait_for_beacon_timeout23a(unsigned int bcn_interval);

void write_cam23a(struct rtw_adapter *padapter, u8 entry, u16 ctrl,
	       u8 *mac, u8 *key);
void clear_cam_entry23a(struct rtw_adapter *padapter, u8 entry);

void invalidate_cam_all23a(struct rtw_adapter *padapter);
void CAM_empty_entry23a(struct rtw_adapter *Adapter, u8 ucIndex);

int allocate_fw_sta_entry23a(struct rtw_adapter *padapter);
void flush_all_cam_entry23a(struct rtw_adapter *padapter);

bool IsLegal5GChannel(struct rtw_adapter *Adapter, u8 channel);

void site_survey23a(struct rtw_adapter *padapter);
u8 collect_bss_info23a(struct rtw_adapter *padapter,
		    struct recv_frame *precv_frame,
		    struct wlan_bssid_ex *bssid);
void update_network23a(struct wlan_bssid_ex *dst, struct wlan_bssid_ex *src,
		    struct rtw_adapter *padapter, bool update_ie);

int get_bsstype23a(unsigned short capability);
u8 *get_my_bssid23a(struct wlan_bssid_ex *pnetwork);
u16 get_beacon_interval23a(struct wlan_bssid_ex *bss);

int is_client_associated_to_ap23a(struct rtw_adapter *padapter);
int is_client_associated_to_ibss23a(struct rtw_adapter *padapter);
int is_IBSS_empty23a(struct rtw_adapter *padapter);

unsigned char check_assoc_AP23a(u8 *pframe, uint len);

int WMM_param_handler23a(struct rtw_adapter *padapter,
		      struct ndis_802_11_var_ies *pIE);
#ifdef CONFIG_8723AU_P2P
int WFD_info_handler(struct rtw_adapter *padapter,
		     struct ndis_802_11_var_ies *pIE);
#endif
void WMMOnAssocRsp23a(struct rtw_adapter *padapter);

void HT_caps_handler23a(struct rtw_adapter *padapter,
		     struct ndis_802_11_var_ies *pIE);
void HT_info_handler23a(struct rtw_adapter *padapter,
		     struct ndis_802_11_var_ies *pIE);
void HTOnAssocRsp23a(struct rtw_adapter *padapter);

void ERP_IE_handler23a(struct rtw_adapter *padapter,
		    struct ndis_802_11_var_ies *pIE);
void VCS_update23a(struct rtw_adapter *padapter, struct sta_info *psta);

void update_beacon23a_info(struct rtw_adapter *padapter, u8 *pframe, uint len,
			struct sta_info *psta);
int rtw_check_bcn_info23a(struct rtw_adapter *Adapter, u8 *pframe, u32 packet_len);
void update_IOT_info23a(struct rtw_adapter *padapter);
void update_capinfo23a(struct rtw_adapter *Adapter, u16 updateCap);
void update_wireless_mode23a(struct rtw_adapter * padapter);
void update_tx_basic_rate23a(struct rtw_adapter *padapter, u8 modulation);
void update_bmc_sta_support_rate23a(struct rtw_adapter *padapter, u32 mac_id);
int update_sta_support_rate23a(struct rtw_adapter *padapter, u8* pvar_ie,
			    uint var_ie_len, int cam_idx);

/* for sta/adhoc mode */
void update_sta_info23a(struct rtw_adapter *padapter, struct sta_info *psta);
unsigned int update_basic_rate23a(unsigned char *ptn, unsigned int ptn_sz);
unsigned int update_supported_rate23a(unsigned char *ptn, unsigned int ptn_sz);
unsigned int update_MSC_rate23a(struct HT_caps_element *pHT_caps);
void Update_RA_Entry23a(struct rtw_adapter *padapter, struct sta_info *psta);
void set_sta_rate23a(struct rtw_adapter *padapter, struct sta_info *psta);

unsigned int receive_disconnect23a(struct rtw_adapter *padapter,
				unsigned char *MacAddr, unsigned short reason);

unsigned char get_highest_rate_idx23a(u32 mask);
int support_short_GI23a(struct rtw_adapter *padapter,
		     struct HT_caps_element *pHT_caps);
unsigned int is_ap_in_tkip23a(struct rtw_adapter *padapter);
unsigned int is_ap_in_wep23a(struct rtw_adapter *padapter);
unsigned int should_forbid_n_rate23a(struct rtw_adapter *padapter);

void report_join_res23a(struct rtw_adapter *padapter, int res);
void report_survey_event23a(struct rtw_adapter *padapter,
			 struct recv_frame *precv_frame);
void report_surveydone_event23a(struct rtw_adapter *padapter);
void report_del_sta_event23a(struct rtw_adapter *padapter,
			  unsigned char *MacAddr, unsigned short reason);
void report_add_sta_event23a(struct rtw_adapter *padapter,
			  unsigned char *MacAddr, int cam_idx);

void beacon_timing_control23a(struct rtw_adapter *padapter);
u8 set_tx_beacon_cmd23a(struct rtw_adapter*padapter);
unsigned int setup_beacon_frame(struct rtw_adapter *padapter,
				unsigned char *beacon_frame);
void update_mgnt_tx_rate23a(struct rtw_adapter *padapter, u8 rate);
void update_mgntframe_attrib23a(struct rtw_adapter *padapter,
			     struct pkt_attrib *pattrib);
void dump_mgntframe23a(struct rtw_adapter *padapter,
		    struct xmit_frame *pmgntframe);
s32 dump_mgntframe23a_and_wait(struct rtw_adapter *padapter,
			    struct xmit_frame *pmgntframe, int timeout_ms);
s32 dump_mgntframe23a_and_wait_ack23a(struct rtw_adapter *padapter,
				struct xmit_frame *pmgntframe);

#ifdef CONFIG_8723AU_P2P
void issue_probersp23a_p2p23a(struct rtw_adapter *padapter, unsigned char *da);
void issue_p2p_provision_request23a(struct rtw_adapter *padapter, u8 *pssid,
				 u8 ussidlen, u8* pdev_raddr);
void issue_p2p_GO_request23a(struct rtw_adapter *padapter, u8* raddr);
void issue23a_probereq_p2p(struct rtw_adapter *padapter, u8 *da);
int issue23a_probereq_p2p_ex(struct rtw_adapter *adapter, u8 *da, int try_cnt,
			  int wait_ms);
void issue_p2p_invitation_response23a(struct rtw_adapter *padapter, u8* raddr,
				   u8 dialogToken, u8 success);
void issue_p2p_invitation_request23a(struct rtw_adapter *padapter, u8* raddr);
#endif /* CONFIG_8723AU_P2P */
void issue_beacon23a(struct rtw_adapter *padapter, int timeout_ms);
void issue_probersp23a(struct rtw_adapter *padapter, unsigned char *da,
		    u8 is_valid_p2p_probereq);
void issue_assocreq23a(struct rtw_adapter *padapter);
void issue_asocrsp23a(struct rtw_adapter *padapter, unsigned short status,
		   struct sta_info *pstat, int pkt_type);
void issue_auth23a(struct rtw_adapter *padapter, struct sta_info *psta,
		unsigned short status);
void issue_probereq23a(struct rtw_adapter *padapter, struct cfg80211_ssid *pssid,
		    u8 *da);
s32 issue_probereq23a_ex23a(struct rtw_adapter *padapter, struct cfg80211_ssid *pssid,
		      u8 *da, int try_cnt, int wait_ms);
int issue_nulldata23a(struct rtw_adapter *padapter, unsigned char *da,
		   unsigned int power_mode, int try_cnt, int wait_ms);
int issue_qos_nulldata23a(struct rtw_adapter *padapter, unsigned char *da, u16 tid,
		       int try_cnt, int wait_ms);
int issue_deauth23a(struct rtw_adapter *padapter, unsigned char *da,
		 unsigned short reason);
int issue_deauth23a_ex23a(struct rtw_adapter *padapter, u8 *da, unsigned short reason,
		    int try_cnt, int wait_ms);
void issue_action_spct_ch_switch23a(struct rtw_adapter *padapter, u8 *ra,
				 u8 new_ch, u8 ch_offset);
void issue_action_BA23a(struct rtw_adapter *padapter, unsigned char *raddr,
		     unsigned char action, unsigned short status);
unsigned int send_delba23a(struct rtw_adapter *padapter, u8 initiator, u8 *addr);
unsigned int send_beacon23a(struct rtw_adapter *padapter);

void start_clnt_assoc23a(struct rtw_adapter *padapter);
void start_clnt_auth23a(struct rtw_adapter *padapter);
void start_clnt_join23a(struct rtw_adapter *padapter);
void start_create_ibss23a(struct rtw_adapter *padapter);

unsigned int OnAssocReq23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnAssocRsp23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnProbeReq23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnProbeRsp23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int DoReserved23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnBeacon23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnAtim23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnDisassoc23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnAuth23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnAuth23aClient23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnDeAuth23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnAction23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame);

unsigned int on_action_spct23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnAction23a_qos(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnAction23a_dls(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnAction23a_back23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int on_action_public23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnAction23a_ht(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnAction23a_wmm(struct rtw_adapter *padapter, struct recv_frame *precv_frame);
unsigned int OnAction23a_p2p(struct rtw_adapter *padapter, struct recv_frame *precv_frame);


void mlmeext_joinbss_event_callback23a(struct rtw_adapter *padapter, int join_res);
void mlmeext_sta_del_event_callback23a(struct rtw_adapter *padapter);
void mlmeext_sta_add_event_callback23a(struct rtw_adapter *padapter, struct sta_info *psta);

void linked_status_chk23a(struct rtw_adapter *padapter);

#define set_survey_timer(mlmeext, ms) \
	/*DBG_8723A("%s set_survey_timer(%p, %d)\n", __FUNCTION__, (mlmeext), (ms));*/ \
	mod_timer(&mlmeext->survey_timer, jiffies + msecs_to_jiffies(ms));

#define set_link_timer(mlmeext, ms) \
	/*DBG_8723A("%s set_link_timer(%p, %d)\n", __FUNCTION__, (mlmeext), (ms));*/ \
	mod_timer(&mlmeext->link_timer, jiffies + msecs_to_jiffies(ms));

int cckrates_included23a(unsigned char *rate, int ratelen);
int cckratesonly_included23a(unsigned char *rate, int ratelen);

void process_addba_req23a(struct rtw_adapter *padapter, u8 *paddba_req, u8 *addr);

void update_TSF23a(struct mlme_ext_priv *pmlmeext, u8 *pframe, uint len);
void correct_TSF23a(struct rtw_adapter *padapter, struct mlme_ext_priv *pmlmeext);

struct cmd_hdl {
	uint	parmsize;
	u8 (*h2cfuns)(struct rtw_adapter *padapter, u8 *pbuf);
};


u8 read_macreg_hdl(struct rtw_adapter *padapter, u8 *pbuf);
u8 write_macreg_hdl(struct rtw_adapter *padapter, u8 *pbuf);
u8 read_bbreg_hdl(struct rtw_adapter *padapter, u8 *pbuf);
u8 write_bbreg_hdl(struct rtw_adapter *padapter, u8 *pbuf);
u8 read_rfreg_hdl(struct rtw_adapter *padapter, u8 *pbuf);
u8 write_rfreg_hdl(struct rtw_adapter *padapter, u8 *pbuf);


u8 NULL_hdl23a(struct rtw_adapter *padapter, u8 *pbuf);
u8 join_cmd_hdl23a(struct rtw_adapter *padapter, u8 *pbuf);
u8 disconnect_hdl23a(struct rtw_adapter *padapter, u8 *pbuf);
u8 createbss_hdl23a(struct rtw_adapter *padapter, u8 *pbuf);
u8 setopmode_hdl23a(struct rtw_adapter *padapter, u8 *pbuf);
u8 sitesurvey_cmd_hdl23a(struct rtw_adapter *padapter, u8 *pbuf);
u8 setauth_hdl23a(struct rtw_adapter *padapter, u8 *pbuf);
u8 setkey_hdl23a(struct rtw_adapter *padapter, u8 *pbuf);
u8 set_stakey_hdl23a(struct rtw_adapter *padapter, u8 *pbuf);
u8 set_assocsta_hdl(struct rtw_adapter *padapter, u8 *pbuf);
u8 del_assocsta_hdl(struct rtw_adapter *padapter, u8 *pbuf);
u8 add_ba_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf);

u8 mlme_evt_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf);
u8 h2c_msg_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf);
u8 tx_beacon_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf);
u8 set_ch_hdl23a(struct rtw_adapter *padapter, u8 *pbuf);
u8 set_chplan_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf);
u8 led_blink_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf);
u8 set_csa_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf);	/* Kurt: Handling DFS channel switch announcement ie. */
u8 tdls_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf);

#define GEN_DRV_CMD_HANDLER(size, cmd)	{size, &cmd ## _hdl23a},
#define GEN_MLME_EXT_HANDLER(size, cmd)	{size, cmd},

struct C2HEvent_Header {
#ifdef __LITTLE_ENDIAN

	unsigned int len:16;
	unsigned int ID:8;
	unsigned int seq:8;

#elif defined(__BIG_ENDIAN)

	unsigned int seq:8;
	unsigned int ID:8;
	unsigned int len:16;

#else

#  error "Must be LITTLE or BIG Endian"

#endif

	unsigned int rsvd;
};

void rtw_dummy_event_callback23a(struct rtw_adapter *adapter , u8 *pbuf);
void rtw23a_fwdbg_event_callback(struct rtw_adapter *adapter , u8 *pbuf);

enum rtw_c2h_event {
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

	GEN_EVT_CODE(_JoinBss) , /*10*/
	GEN_EVT_CODE(_AddSTA),
	GEN_EVT_CODE(_DelSTA),
	GEN_EVT_CODE(_AtimDone) ,
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
	MAX_C2HEVT
};

#endif
