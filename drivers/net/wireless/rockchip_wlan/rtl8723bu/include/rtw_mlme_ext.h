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
#ifndef __RTW_MLME_EXT_H_
#define __RTW_MLME_EXT_H_


//	Commented by Albert 20101105
//	Increase the SURVEY_TO value from 100 to 150  ( 100ms to 150ms )
//	The Realtek 8188CE SoftAP will spend around 100ms to send the probe response after receiving the probe request.
//	So, this driver tried to extend the dwell time for each scanning channel.
//	This will increase the chance to receive the probe response from SoftAP.

#define SURVEY_TO		(100)
#define REAUTH_TO		(300) //(50)
#define REASSOC_TO		(300) //(50)
//#define DISCONNECT_TO	(3000)
#define ADDBA_TO			(2000)

#define LINKED_TO (1) //unit:2 sec, 1x2=2 sec

#define REAUTH_LIMIT	(4)
#define REASSOC_LIMIT	(4)
#define READDBA_LIMIT	(2)

#ifdef CONFIG_GSPI_HCI
	#define ROAMING_LIMIT	5
#else
	#define ROAMING_LIMIT	8
#endif
//#define	IOCMD_REG0		0x10250370		 	
//#define	IOCMD_REG1		0x10250374
//#define	IOCMD_REG2		0x10250378

//#define	FW_DYNAMIC_FUN_SWITCH	0x10250364

//#define	WRITE_BB_CMD		0xF0000001
//#define	SET_CHANNEL_CMD	0xF3000000
//#define	UPDATE_RA_CMD	0xFD0000A2

#define DYNAMIC_FUNC_DISABLE		(0x0)

#define _HW_STATE_NOLINK_		0x00
#define _HW_STATE_ADHOC_		0x01
#define _HW_STATE_STATION_ 	0x02
#define _HW_STATE_AP_			0x03
#define _HW_STATE_MONITOR_ 0x04


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
#define MCS_RATE_2R	(0x0000ffff)
#define MCS_RATE_3R	(0x00ffffff)
#define MCS_RATE_4R	(0xffffffff)
#define MCS_RATE_2R_13TO15_OFF	(0x00001fff)


extern unsigned char RTW_WPA_OUI[];
extern unsigned char WMM_OUI[];
extern unsigned char WPS_OUI[];
extern unsigned char WFD_OUI[];
extern unsigned char P2P_OUI[];

extern unsigned char WMM_INFO_OUI[];
extern unsigned char WMM_PARA_OUI[];


//
// Channel Plan Type.
// Note: 
//	We just add new channel plan when the new channel plan is different from any of the following 
//	channel plan. 
//	If you just wnat to customize the acitions(scan period or join actions) about one of the channel plan,
//	customize them in RT_CHANNEL_INFO in the RT_CHANNEL_LIST.
// 
typedef enum _RT_CHANNEL_DOMAIN
{
	//===== old channel plan mapping =====//
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

	//===== new channel plan mapping, (2GDOMAIN_5GDOMAIN) =====//
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
	RT_CHANNEL_DOMAIN_WORLD_MKK4 = 0x58,
	//===== Add new channel plan above this line===============//
	RT_CHANNEL_DOMAIN_MAX,
	RT_CHANNEL_DOMAIN_REALTEK_DEFINE = 0x7F,
}RT_CHANNEL_DOMAIN, *PRT_CHANNEL_DOMAIN;

typedef enum _RT_CHANNEL_DOMAIN_2G
{
	RT_CHANNEL_DOMAIN_2G_WORLD = 0x00,		//Worldwird 13
	RT_CHANNEL_DOMAIN_2G_ETSI1 = 0x01,		//Europe
	RT_CHANNEL_DOMAIN_2G_FCC1 = 0x02,		//US
	RT_CHANNEL_DOMAIN_2G_MKK1 = 0x03,		//Japan
	RT_CHANNEL_DOMAIN_2G_ETSI2 = 0x04,		//France
	RT_CHANNEL_DOMAIN_2G_GLOBAL = 0x05,		//Global domain
	RT_CHANNEL_DOMAIN_2G_NULL = 0x06,
	//===== Add new channel plan above this line===============//
	RT_CHANNEL_DOMAIN_2G_MAX,
}RT_CHANNEL_DOMAIN_2G, *PRT_CHANNEL_DOMAIN_2G;

typedef enum _RT_CHANNEL_DOMAIN_5G
{
	RT_CHANNEL_DOMAIN_5G_NULL = 0x00,
	RT_CHANNEL_DOMAIN_5G_ETSI1 = 0x01,		//Europe
	RT_CHANNEL_DOMAIN_5G_ETSI2 = 0x02,		//Australia, New Zealand
	RT_CHANNEL_DOMAIN_5G_ETSI3 = 0x03,		//Russia
	RT_CHANNEL_DOMAIN_5G_FCC1 = 0x04,		//US
	RT_CHANNEL_DOMAIN_5G_FCC2 = 0x05,		//FCC o/w DFS Channels
	RT_CHANNEL_DOMAIN_5G_FCC3 = 0x06,		//India, Mexico
	RT_CHANNEL_DOMAIN_5G_FCC4 = 0x07,		//Venezuela
	RT_CHANNEL_DOMAIN_5G_FCC5 = 0x08,		//China
	RT_CHANNEL_DOMAIN_5G_FCC6 = 0x09,		//Israel
	RT_CHANNEL_DOMAIN_5G_FCC7_IC1 = 0x0A,	//US, Canada
	RT_CHANNEL_DOMAIN_5G_KCC1 = 0x0B,		//Korea
	RT_CHANNEL_DOMAIN_5G_MKK1 = 0x0C,		//Japan
	RT_CHANNEL_DOMAIN_5G_MKK2 = 0x0D,		//Japan (W52, W53)
	RT_CHANNEL_DOMAIN_5G_MKK3 = 0x0E,		//Japan (W56)
	RT_CHANNEL_DOMAIN_5G_NCC1 = 0x0F,		//Taiwan
	RT_CHANNEL_DOMAIN_5G_NCC2 = 0x10,		//Taiwan o/w DFS
	RT_CHANNEL_DOMAIN_5G_NCC3 = 0x11,		//Taiwan w/o DFS, Band4 only
	RT_CHANNEL_DOMAIN_5G_ETSI4 = 0x12,		//Europe w/o DFS, Band1 only
	RT_CHANNEL_DOMAIN_5G_ETSI5 = 0x13,		//Australia, New Zealand(w/o Weather radar)
	RT_CHANNEL_DOMAIN_5G_FCC8 = 0x14,		//Latin America
	RT_CHANNEL_DOMAIN_5G_ETSI6 = 0x15,		//Israel, Bahrain, Egypt, India, China, Malaysia
	RT_CHANNEL_DOMAIN_5G_ETSI7 = 0x16,		//China
	RT_CHANNEL_DOMAIN_5G_ETSI8 = 0x17,		//Jordan
	RT_CHANNEL_DOMAIN_5G_ETSI9 = 0x18,		//Lebanon
	RT_CHANNEL_DOMAIN_5G_ETSI10 = 0x19,		//Qatar
	RT_CHANNEL_DOMAIN_5G_ETSI11 = 0x1A,		//Russia
	RT_CHANNEL_DOMAIN_5G_NCC4 = 0x1B,		//Taiwan, (w/o Weather radar)
	RT_CHANNEL_DOMAIN_5G_ETSI12 = 0x1C,		//Indonesia
	RT_CHANNEL_DOMAIN_5G_FCC9 = 0x1D,		//(w/o Weather radar)
	RT_CHANNEL_DOMAIN_5G_ETSI13 = 0x1E,		//(w/o Weather radar)
	RT_CHANNEL_DOMAIN_5G_FCC10 = 0x1F,		//Argentina (w/o Weather radar)
	RT_CHANNEL_DOMAIN_5G_KCC2 = 0x20,		//Korea 5G
	RT_CHANNEL_DOMAIN_5G_FCC11 = 0x21,		//US/Canada
	RT_CHANNEL_DOMAIN_5G_NCC5 = 0x22,		//Taiwan
	RT_CHANNEL_DOMAIN_5G_MKK4 = 0x23,		//Japan W52
	//===== Add new channel plan above this line===============//
	//===== Driver Self Defined =====//
	RT_CHANNEL_DOMAIN_5G_FCC = 0x30,
	RT_CHANNEL_DOMAIN_5G_JAPAN_NO_DFS = 0x31,
	RT_CHANNEL_DOMAIN_5G_FCC4_NO_DFS = 0x32,
	RT_CHANNEL_DOMAIN_5G_MAX,
}RT_CHANNEL_DOMAIN_5G, *PRT_CHANNEL_DOMAIN_5G;

#define rtw_is_channel_plan_valid(chplan) (chplan<RT_CHANNEL_DOMAIN_MAX || chplan == RT_CHANNEL_DOMAIN_REALTEK_DEFINE)

typedef struct _RT_CHANNEL_PLAN
{
	unsigned char	Channel[MAX_CHANNEL_NUM];
	unsigned char	Len;
}RT_CHANNEL_PLAN, *PRT_CHANNEL_PLAN;

typedef struct _RT_CHANNEL_PLAN_2G
{
	unsigned char	Channel[MAX_CHANNEL_NUM_2G];
	unsigned char	Len;
}RT_CHANNEL_PLAN_2G, *PRT_CHANNEL_PLAN_2G;

typedef struct _RT_CHANNEL_PLAN_5G
{
	unsigned char	Channel[MAX_CHANNEL_NUM_5G];
	unsigned char	Len;
}RT_CHANNEL_PLAN_5G, *PRT_CHANNEL_PLAN_5G;

typedef struct _RT_CHANNEL_PLAN_MAP
{
	unsigned char	Index2G;
	unsigned char	Index5G;
}RT_CHANNEL_PLAN_MAP, *PRT_CHANNEL_PLAN_MAP;

enum Associated_AP
{
	atherosAP	= 0,
	broadcomAP	= 1,
	ciscoAP		= 2,
	marvellAP	= 3,
	ralinkAP	= 4,
	realtekAP	= 5,
	airgocapAP 	= 6,
	unknownAP	= 7,
	maxAP,
};

typedef enum _HT_IOT_PEER
{
	HT_IOT_PEER_UNKNOWN 			= 0,
	HT_IOT_PEER_REALTEK 			= 1,
	HT_IOT_PEER_REALTEK_92SE 		= 2,
	HT_IOT_PEER_BROADCOM 		= 3,
	HT_IOT_PEER_RALINK 			= 4,
	HT_IOT_PEER_ATHEROS 			= 5,
	HT_IOT_PEER_CISCO 				= 6,
	HT_IOT_PEER_MERU 				= 7,	
	HT_IOT_PEER_MARVELL 			= 8,
	HT_IOT_PEER_REALTEK_SOFTAP 	= 9,// peer is RealTek SOFT_AP, by Bohn, 2009.12.17
	HT_IOT_PEER_SELF_SOFTAP 		= 10, // Self is SoftAP
	HT_IOT_PEER_AIRGO 				= 11,
	HT_IOT_PEER_INTEL 				= 12, 
	HT_IOT_PEER_RTK_APCLIENT 		= 13, 
	HT_IOT_PEER_REALTEK_81XX 		= 14,	
	HT_IOT_PEER_REALTEK_WOW 		= 15,
	HT_IOT_PEER_REALTEK_JAGUAR_BCUTAP = 16,
	HT_IOT_PEER_REALTEK_JAGUAR_CCUTAP = 17,
	HT_IOT_PEER_MAX 				= 18
}HT_IOT_PEER_E, *PHTIOT_PEER_E;


enum SCAN_STATE
{
	SCAN_DISABLE = 0,
	SCAN_START = 1,
	SCAN_TXNULL = 2,
	SCAN_PROCESS = 3,
	SCAN_COMPLETE = 4,
	SCAN_STATE_MAX,
};

struct mlme_handler {
	unsigned int   num;
	char* str;
	unsigned int (*func)(_adapter *padapter, union recv_frame *precv_frame);
};

struct action_handler {
	unsigned int   num;
	char* str;
	unsigned int (*func)(_adapter *padapter, union recv_frame *precv_frame);
};

struct	ss_res	
{
	int	state;
	int	bss_cnt;
	int	channel_idx;
	int	scan_mode;
	u8 ssid_num;
	u8 ch_num;
	NDIS_802_11_SSID ssid[RTW_SSID_SCAN_AMOUNT];
	struct rtw_ieee80211_channel ch[RTW_CHANNEL_SCAN_AMOUNT];
};

//#define AP_MODE				0x0C
//#define STATION_MODE	0x08
//#define AD_HOC_MODE		0x04
//#define NO_LINK_MODE	0x00

#define 	WIFI_FW_NULL_STATE			_HW_STATE_NOLINK_
#define	WIFI_FW_STATION_STATE		_HW_STATE_STATION_
#define	WIFI_FW_AP_STATE				_HW_STATE_AP_
#define	WIFI_FW_ADHOC_STATE			_HW_STATE_ADHOC_

#define	WIFI_FW_AUTH_NULL			0x00000100
#define	WIFI_FW_AUTH_STATE			0x00000200
#define	WIFI_FW_AUTH_SUCCESS			0x00000400

#define	WIFI_FW_ASSOC_STATE			0x00002000
#define	WIFI_FW_ASSOC_SUCCESS		0x00004000

#define	WIFI_FW_LINKING_STATE		(WIFI_FW_AUTH_NULL | WIFI_FW_AUTH_STATE | WIFI_FW_AUTH_SUCCESS |WIFI_FW_ASSOC_STATE)

#ifdef CONFIG_TDLS
enum TDLS_option
{
	TDLS_ESTABLISHED	= 	1,
	TDLS_ISSUE_PTI				=	2,
	TDLS_CH_SW_RESP			=	3,
	TDLS_CH_SW				=	4,
	TDLS_CH_SW_BACK			=	5,
	TDLS_RS_RCR				=	6,
	TDLS_TEAR_STA				=	7,
	maxTDLS,
};

#endif //CONFIG_TDLS

struct FW_Sta_Info
{
	struct sta_info	*psta;
	u32	status;
	u32	rx_pkt;
	u32	retry;
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
#if defined(CONFIG_ATMEL_RC_PATCH) 
#define RTW_SCAN_NUM_OF_CH			2
#define RTW_STAY_AP_CH_MILLISECOND		2	// this value is a multiplier,for example, when this value is 3, 
							// it would stay AP's op ch for  3 * SURVEY_TO millisecond.
#elif defined(CONFIG_STA_MODE_SCAN_UNDER_AP_MODE)
#define RTW_SCAN_NUM_OF_CH			3
#define RTW_STAY_AP_CH_MILLISECOND		4	// this value is a multiplier,for example, when this value is 3, 
							// it would stay AP's op ch for  3 * SURVEY_TO millisecond.
#endif 

struct mlme_ext_info
{
	u32	state;
	u32	reauth_count;
	u32	reassoc_count;
	u32	link_count;
	u32	auth_seq;
	u32	auth_algo;	// 802.11 auth, could be open, shared, auto
	u32	authModeToggle;
	u32	enc_algo;//encrypt algorithm;
	u32	key_index;	// this is only valid for legendary wep, 0~3 for key id.
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
	// Accept ADDBA Request
	BOOLEAN bAcceptAddbaReq;
	u8	bwmode_updated;
	u8	hidden_ssid_mode;
	u8	VHT_enable;

	struct ADDBA_request		ADDBA_req;
	struct WMM_para_element	WMM_param;
	struct HT_caps_element	HT_caps;
	struct HT_info_element		HT_info;
	WLAN_BSSID_EX			network;//join network or bss_network, if in ap mode, it is the same to cur_network.network
	struct FW_Sta_Info		FW_sta_info[NUM_STA];

#if defined(CONFIG_STA_MODE_SCAN_UNDER_AP_MODE) || defined(CONFIG_ATMEL_RC_PATCH)
	u8 scan_cnt;
	u8 backop_cnt;
#endif //CONFIG_STA_MODE_SCAN_UNDER_AP_MODE
};

// The channel information about this channel including joining, scanning, and power constraints.
typedef struct _RT_CHANNEL_INFO
{
	u8				ChannelNum;		// The channel number.
	RT_SCAN_TYPE	ScanType;		// Scan type such as passive or active scan.
	//u16				ScanPeriod;		// Listen time in millisecond in this channel.
	//s32				MaxTxPwrDbm;	// Max allowed tx power.
	//u32				ExInfo;			// Extended Information for this channel.
#ifdef CONFIG_FIND_BEST_CHANNEL
	u32				rx_count;
#endif
}RT_CHANNEL_INFO, *PRT_CHANNEL_INFO;

int rtw_ch_set_search_ch(RT_CHANNEL_INFO *ch_set, const u32 ch);
bool rtw_mlme_band_check(_adapter *adapter, const u32 ch);

// P2P_MAX_REG_CLASSES - Maximum number of regulatory classes
#define P2P_MAX_REG_CLASSES 10

// P2P_MAX_REG_CLASS_CHANNELS - Maximum number of channels per regulatory class
#define P2P_MAX_REG_CLASS_CHANNELS 20

//  struct p2p_channels - List of supported channels
struct p2p_channels {
	// struct p2p_reg_class - Supported regulatory class
	struct p2p_reg_class {
		// reg_class - Regulatory class (IEEE 802.11-2007, Annex J)
		u8 reg_class;

		// channel - Supported channels
		u8 channel[P2P_MAX_REG_CLASS_CHANNELS];

		// channels - Number of channel entries in use
		size_t channels;
	} reg_class[P2P_MAX_REG_CLASSES];

	// reg_classes - Number of reg_class entries in use
	size_t reg_classes;
};

struct p2p_oper_class_map {
	enum hw_mode {IEEE80211G,IEEE80211A} mode;
	u8 op_class;
	u8 min_chan;
	u8 max_chan;
	u8 inc;
	enum { BW20, BW40PLUS, BW40MINUS } bw;
};

struct mlme_ext_priv
{
	_adapter	*padapter;
	u8	mlmeext_init;
	ATOMIC_T		event_seq;
	u16	mgnt_seq;
#ifdef CONFIG_IEEE80211W
	u16	sa_query_seq;
	u64 mgnt_80211w_IPN;
	u64 mgnt_80211w_IPN_rx;
#endif //CONFIG_IEEE80211W
	//struct fw_priv 	fwpriv;
	
	unsigned char	cur_channel;
	unsigned char	cur_bwmode;
	unsigned char	cur_ch_offset;//PRIME_CHNL_OFFSET
	unsigned char	cur_wireless_mode;	// NETWORK_TYPE
	
	unsigned char	max_chan_nums;
	RT_CHANNEL_INFO		channel_set[MAX_CHANNEL_NUM];
	struct p2p_channels channel_list;
	unsigned char	basicrate[NumRates];
	unsigned char	datarate[NumRates];
#ifdef CONFIG_80211N_HT
	unsigned char default_supported_mcs_set[16];
#endif
	
	struct ss_res		sitesurvey_res;		
	struct mlme_ext_info	mlmext_info;//for sta/adhoc mode, including current scanning/connecting/connected related info.
                                                     //for ap mode, network includes ap's cap_info
	_timer		survey_timer;
	_timer		link_timer;
#ifdef CONFIG_IEEE80211W
	_timer		sa_query_timer;
#endif //CONFIG_IEEE80211W
	//_timer		ADDBA_timer;
	u16			chan_scan_time;
	u32 last_scan_time;
	u8	scan_abort;
	u8	tx_rate; // TXRATE when USERATE is set.

	u32	retry; //retry for issue probereq
	
	u64 TSFValue;

	//for LPS-32K to adaptive bcn early and timeout
	u8 adaptive_tsf_done;
	u32 bcn_delay_cnt[9];
	u32 bcn_delay_ratio[9];
	u32 bcn_cnt;
	u8 DrvBcnEarly;
	u8 DrvBcnTimeOut;

#ifdef CONFIG_AP_MODE	
	unsigned char bstart_bss;
#endif

#ifdef CONFIG_80211D
	u8 update_channel_plan_by_ap_done;
#endif
	//recv_decache check for Action_public frame 
	u8 action_public_dialog_token;
	u16 	 action_public_rxseq;

//#ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
	u8 active_keep_alive_check;
//#endif
#ifdef DBG_FIXED_CHAN
	u8 fixed_chan;
#endif
	
};

#define mlmeext_msr(mlmeext) ((mlmeext)->mlmext_info.state & 0x03)

void init_mlme_default_rate_set(_adapter* padapter);
int init_mlme_ext_priv(_adapter* padapter);
int init_hw_mlme_ext(_adapter *padapter);
void free_mlme_ext_priv (struct mlme_ext_priv *pmlmeext);
extern void init_mlme_ext_timer(_adapter *padapter);
extern void init_addba_retry_timer(_adapter *padapter, struct sta_info *psta);
extern struct xmit_frame *alloc_mgtxmitframe(struct xmit_priv *pxmitpriv);
struct xmit_frame *alloc_mgtxmitframe_once(struct xmit_priv *pxmitpriv);

//void fill_fwpriv(_adapter * padapter, struct fw_priv *pfwpriv);

unsigned char networktype_to_raid(_adapter *adapter,struct sta_info *psta);
unsigned char networktype_to_raid_ex(_adapter *adapter, struct sta_info *psta);

u8 judge_network_type(_adapter *padapter, unsigned char *rate, int ratelen);
void get_rate_set(_adapter *padapter, unsigned char *pbssrate, int *bssrate_len);
void set_mcs_rate_by_mask(u8 *mcs_set, u32 mask);
void UpdateBrateTbl(_adapter *padapter,u8 *mBratesOS);
void UpdateBrateTblForSoftAP(u8 *bssrateset, u32 bssratelen);
void change_band_update_ie(_adapter *padapter, WLAN_BSSID_EX *pnetwork);

void Save_DM_Func_Flag(_adapter *padapter);
void Restore_DM_Func_Flag(_adapter *padapter);
void Switch_DM_Func(_adapter *padapter, u32 mode, u8 enable);

//void Set_NETYPE1_MSR(_adapter *padapter, u8 type);
//void Set_NETYPE0_MSR(_adapter *padapter, u8 type);
void Set_MSR(_adapter *padapter, u8 type);

u8 rtw_get_oper_ch(_adapter *adapter);
void rtw_set_oper_ch(_adapter *adapter, u8 ch);
u8 rtw_get_oper_bw(_adapter *adapter);
void rtw_set_oper_bw(_adapter *adapter, u8 bw);
u8 rtw_get_oper_choffset(_adapter *adapter);
void rtw_set_oper_choffset(_adapter *adapter, u8 offset);
u8	rtw_get_center_ch(u8 channel, u8 chnl_bw, u8 chnl_offset);
u32 rtw_get_on_oper_ch_time(_adapter *adapter);
u32 rtw_get_on_cur_ch_time(_adapter *adapter);

u8 rtw_get_offset_by_ch(u8 channel);

void set_channel_bwmode(_adapter *padapter, unsigned char channel, unsigned char channel_offset, unsigned short bwmode);
void SelectChannel(_adapter *padapter, unsigned char channel);
void SetBWMode(_adapter *padapter, unsigned short bwmode, unsigned char channel_offset);

unsigned int decide_wait_for_beacon_timeout(unsigned int bcn_interval);

void read_cam(_adapter *padapter ,u8 entry, u8 *get_key);
void dump_cam_table(_adapter *padapter);

/* modify HW only */
void _write_cam(_adapter *padapter, u8 entry, u16 ctrl, u8 *mac, u8 *key);
void _clear_cam_entry(_adapter *padapter, u8 entry);
void write_cam_from_cache(_adapter *adapter, u8 id);

/* modify both HW and cache */
void write_cam(_adapter *padapter, u8 id, u16 ctrl, u8 *mac, u8 *key);
void clear_cam_entry(_adapter *padapter, u8 id);

/* modify cache only */
void write_cam_cache(_adapter *adapter, u8 id, u16 ctrl, u8 *mac, u8 *key);
void clear_cam_cache(_adapter *adapter, u8 id);

void invalidate_cam_all(_adapter *padapter);
void CAM_empty_entry(PADAPTER Adapter, u8 ucIndex);


int allocate_fw_sta_entry(_adapter *padapter);
void flush_all_cam_entry(_adapter *padapter);

BOOLEAN IsLegal5GChannel(PADAPTER Adapter, u8 channel);

void site_survey(_adapter *padapter);
u8 collect_bss_info(_adapter *padapter, union recv_frame *precv_frame, WLAN_BSSID_EX *bssid);
void update_network(WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src, _adapter * padapter, bool update_ie);

int get_bsstype(unsigned short capability);
u8* get_my_bssid(WLAN_BSSID_EX *pnetwork);
u16 get_beacon_interval(WLAN_BSSID_EX *bss);

int is_client_associated_to_ap(_adapter *padapter);
int is_client_associated_to_ibss(_adapter *padapter);
int is_IBSS_empty(_adapter *padapter);

unsigned char check_assoc_AP(u8 *pframe, uint len);

int WMM_param_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs	pIE);
#ifdef CONFIG_WFD
int WFD_info_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs	pIE);
#endif
void WMMOnAssocRsp(_adapter *padapter);

void HT_caps_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE);
void HT_info_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE);
void HTOnAssocRsp(_adapter *padapter);

void ERP_IE_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE);
void VCS_update(_adapter *padapter, struct sta_info *psta);
void	update_ldpc_stbc_cap(struct sta_info *psta);

int rtw_get_bcn_keys(ADAPTER *Adapter, u8 *pframe, u32 packet_len,
		struct beacon_keys *recv_beacon);
void rtw_dump_bcn_keys(struct beacon_keys *recv_beacon);
int rtw_check_bcn_info(ADAPTER *Adapter, u8 *pframe, u32 packet_len);
void update_beacon_info(_adapter *padapter, u8 *pframe, uint len, struct sta_info *psta);
#ifdef CONFIG_DFS
void process_csa_ie(_adapter *padapter, u8 *pframe, uint len);
#endif //CONFIG_DFS
void update_capinfo(PADAPTER Adapter, u16 updateCap);
void update_wireless_mode(_adapter * padapter);
void update_tx_basic_rate(_adapter *padapter, u8 modulation);
void update_sta_basic_rate(struct sta_info *psta, u8 wireless_mode);
int update_sta_support_rate(_adapter *padapter, u8* pvar_ie, uint var_ie_len, int cam_idx);

//for sta/adhoc mode
void update_sta_info(_adapter *padapter, struct sta_info *psta);
unsigned int update_basic_rate(unsigned char *ptn, unsigned int ptn_sz);
unsigned int update_supported_rate(unsigned char *ptn, unsigned int ptn_sz);
unsigned int update_MCS_rate(struct HT_caps_element *pHT_caps);
void Update_RA_Entry(_adapter *padapter, struct sta_info *psta);
void set_sta_rate(_adapter *padapter, struct sta_info *psta);

unsigned int receive_disconnect(_adapter *padapter, unsigned char *MacAddr, unsigned short reason);

unsigned char get_highest_rate_idx(u32 mask);
int support_short_GI(_adapter *padapter, struct HT_caps_element *pHT_caps, u8 bwmode);
unsigned int is_ap_in_tkip(_adapter *padapter);
unsigned int is_ap_in_wep(_adapter *padapter);
unsigned int should_forbid_n_rate(_adapter * padapter);

s16 rtw_get_camid(_adapter *adapter, struct sta_info* sta, s16 kid);
s16 rtw_camid_search(_adapter *adapter, u8 *addr, s16 kid);
s16 rtw_camid_alloc(_adapter *adapter, struct sta_info *sta, u8 kid);
void rtw_camid_free(_adapter *adapter, u8 cam_id);
bool rtw_camid_is_gk(_adapter *padapter, u8 entry);
bool read_phy_cam_is_gtk(_adapter *padapter, u8 entry);

struct macid_bmp;
struct macid_ctl_t;
void dump_macid_map(void *sel, struct macid_bmp *map, u8 max_num);
bool rtw_macid_is_set(struct macid_bmp *map, u8 id);
bool rtw_macid_is_used(struct macid_ctl_t *macid_ctl, u8 id);
bool rtw_macid_is_bmc(struct macid_ctl_t *macid_ctl, u8 id);
s8 rtw_macid_get_if_g(struct macid_ctl_t *macid_ctl, u8 id);
s8 rtw_macid_get_ch_g(struct macid_ctl_t *macid_ctl, u8 id);
void rtw_alloc_macid(_adapter *padapter, struct sta_info *psta);
void rtw_release_macid(_adapter *padapter, struct sta_info *psta);
u8 rtw_search_max_mac_id(_adapter *padapter);
void rtw_macid_ctl_init(struct macid_ctl_t *macid_ctl);
void rtw_macid_ctl_deinit(struct macid_ctl_t *macid_ctl);

void report_join_res(_adapter *padapter, int res);
void report_survey_event(_adapter *padapter, union recv_frame *precv_frame);
void report_surveydone_event(_adapter *padapter);
void report_del_sta_event(_adapter *padapter, unsigned char* MacAddr, unsigned short reason);
void report_add_sta_event(_adapter *padapter, unsigned char* MacAddr, int cam_idx);
bool rtw_port_switch_chk(_adapter *adapter);
void report_wmm_edca_update(_adapter *padapter);

void beacon_timing_control(_adapter *padapter);
u8 chk_bmc_sleepq_cmd(_adapter* padapter);
extern u8 set_tx_beacon_cmd(_adapter*padapter);
unsigned int setup_beacon_frame(_adapter *padapter, unsigned char *beacon_frame);
void update_mgnt_tx_rate(_adapter *padapter, u8 rate);
void update_mgntframe_attrib(_adapter *padapter, struct pkt_attrib *pattrib);
void update_mgntframe_attrib_addr(_adapter *padapter, struct xmit_frame *pmgntframe);
void dump_mgntframe(_adapter *padapter, struct xmit_frame *pmgntframe);
s32 dump_mgntframe_and_wait(_adapter *padapter, struct xmit_frame *pmgntframe, int timeout_ms);
s32 dump_mgntframe_and_wait_ack(_adapter *padapter, struct xmit_frame *pmgntframe);

#ifdef CONFIG_P2P
void issue_probersp_p2p(_adapter *padapter, unsigned char *da);
void issue_p2p_provision_request( _adapter *padapter, u8* pssid, u8 ussidlen, u8* pdev_raddr);
void issue_p2p_GO_request(_adapter *padapter, u8* raddr);
void issue_probereq_p2p(_adapter *padapter, u8 *da);
int issue_probereq_p2p_ex(_adapter *adapter, u8 *da, int try_cnt, int wait_ms);
void issue_p2p_invitation_response(_adapter *padapter, u8* raddr, u8 dialogToken, u8 success);
void issue_p2p_invitation_request(_adapter *padapter, u8* raddr );
#endif //CONFIG_P2P
void issue_beacon(_adapter *padapter, int timeout_ms);
void issue_probersp(_adapter *padapter, unsigned char *da, u8 is_valid_p2p_probereq);
void issue_assocreq(_adapter *padapter);
void issue_asocrsp(_adapter *padapter, unsigned short status, struct sta_info *pstat, int pkt_type);
void issue_auth(_adapter *padapter, struct sta_info *psta, unsigned short status);
void issue_probereq(_adapter *padapter, NDIS_802_11_SSID *pssid, u8 *da);
s32 issue_probereq_ex(_adapter *padapter, NDIS_802_11_SSID *pssid, u8* da, u8 ch, bool append_wps, int try_cnt, int wait_ms);
int issue_nulldata(_adapter *padapter, unsigned char *da, unsigned int power_mode, int try_cnt, int wait_ms);
s32 issue_nulldata_in_interrupt(PADAPTER padapter, u8 *da, unsigned int power_mode);
int issue_qos_nulldata(_adapter *padapter, unsigned char *da, u16 tid, int try_cnt, int wait_ms);
int issue_deauth(_adapter *padapter, unsigned char *da, unsigned short reason);
int issue_deauth_ex(_adapter *padapter, u8 *da, unsigned short reason, int try_cnt, int wait_ms);
void issue_action_spct_ch_switch(_adapter *padapter, u8 *ra, u8 new_ch, u8 ch_offset);
void issue_addba_req(_adapter *adapter, unsigned char *ra, u8 tid);
void issue_addba_rsp(_adapter *adapter, unsigned char *ra, u8 tid, u16 status, u8 size);
void issue_del_ba(_adapter *adapter, unsigned char *ra, u8 tid, u16 reason, u8 initiator);
int issue_del_ba_ex(_adapter *adapter, unsigned char *ra, u8 tid, u16 reason, u8 initiator, int try_cnt, int wait_ms);

#ifdef CONFIG_IEEE80211W
void issue_action_SA_Query(_adapter *padapter, unsigned char *raddr, unsigned char action, unsigned short tid);
#endif //CONFIG_IEEE80211W
int issue_action_SM_PS(_adapter *padapter ,  unsigned char *raddr , u8 NewMimoPsMode);
int issue_action_SM_PS_wait_ack(_adapter *padapter, unsigned char *raddr, u8 NewMimoPsMode, int try_cnt, int wait_ms);

unsigned int send_delba_sta_tid(_adapter *adapter, u8 initiator, struct sta_info *sta, u8 tid, u8 force);
unsigned int send_delba_sta_tid_wait_ack(_adapter *adapter, u8 initiator, struct sta_info *sta, u8 tid, u8 force);

unsigned int send_delba(_adapter *padapter, u8 initiator, u8 *addr);
unsigned int send_beacon(_adapter *padapter);

void start_clnt_assoc(_adapter *padapter);
void start_clnt_auth(_adapter* padapter);
void start_clnt_join(_adapter* padapter);
void start_create_ibss(_adapter* padapter);

unsigned int OnAssocReq(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAssocRsp(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnProbeReq(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnProbeRsp(_adapter *padapter, union recv_frame *precv_frame);
unsigned int DoReserved(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnBeacon(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAtim(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnDisassoc(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAuth(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAuthClient(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnDeAuth(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction(_adapter *padapter, union recv_frame *precv_frame);

unsigned int on_action_spct(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction_qos(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction_dls(_adapter *padapter, union recv_frame *precv_frame);

#define RX_AMPDU_ACCEPT_INVALID 0xFF
#define RX_AMPDU_SIZE_INVALID 0xFF

enum rx_ampdu_reason {
	RX_AMPDU_DRV_FIXED = 1,
	RX_AMPDU_BTCOEX = 2, /* not used, because BTCOEX has its own variable management */
};
u8 rtw_rx_ampdu_size(_adapter *adapter);
bool rtw_rx_ampdu_is_accept(_adapter *adapter);
bool rtw_rx_ampdu_set_size(_adapter *adapter, u8 size, u8 reason);
bool rtw_rx_ampdu_set_accept(_adapter *adapter, u8 accept, u8 reason);
u8 rx_ampdu_apply_sta_tid(_adapter *adapter, struct sta_info *sta, u8 tid, u8 accept, u8 size);
u8 rx_ampdu_apply_sta(_adapter *adapter, struct sta_info *sta, u8 accept, u8 size);
u16 rtw_rx_ampdu_apply(_adapter *adapter);

unsigned int OnAction_back(_adapter *padapter, union recv_frame *precv_frame);
unsigned int on_action_public(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction_ht(_adapter *padapter, union recv_frame *precv_frame);
#ifdef CONFIG_IEEE80211W
unsigned int OnAction_sa_query(_adapter *padapter, union recv_frame *precv_frame);
#endif //CONFIG_IEEE80211W
unsigned int OnAction_wmm(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction_vht(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction_p2p(_adapter *padapter, union recv_frame *precv_frame);


void mlmeext_joinbss_event_callback(_adapter *padapter, int join_res);
void mlmeext_sta_del_event_callback(_adapter *padapter);
void mlmeext_sta_add_event_callback(_adapter *padapter, struct sta_info *psta);

void linked_status_chk(_adapter *padapter, u8 from_timer);

void _linked_info_dump(_adapter *padapter);

void survey_timer_hdl (_adapter *padapter);
void link_timer_hdl (_adapter *padapter);
void addba_timer_hdl(struct sta_info *psta);
#ifdef CONFIG_IEEE80211W
void sa_query_timer_hdl(_adapter *padapter);
#endif //CONFIG_IEEE80211W
//void reauth_timer_hdl(_adapter *padapter);
//void reassoc_timer_hdl(_adapter *padapter);

#define set_survey_timer(mlmeext, ms) \
	do { \
		/*DBG_871X("%s set_survey_timer(%p, %d)\n", __FUNCTION__, (mlmeext), (ms));*/ \
		_set_timer(&(mlmeext)->survey_timer, (ms)); \
	} while(0)

#define set_link_timer(mlmeext, ms) \
	do { \
		/*DBG_871X("%s set_link_timer(%p, %d)\n", __FUNCTION__, (mlmeext), (ms));*/ \
		_set_timer(&(mlmeext)->link_timer, (ms)); \
	} while(0)
#ifdef CONFIG_IEEE80211W
#define set_sa_query_timer(mlmeext, ms) \
	do { \
		DBG_871X("%s set_sa_query_timer(%p, %d)\n", __FUNCTION__, (mlmeext), (ms)); \
		_set_timer(&(mlmeext)->sa_query_timer, (ms)); \
	} while(0)
#endif //CONFIG_IEEE80211W
extern int cckrates_included(unsigned char *rate, int ratelen);
extern int cckratesonly_included(unsigned char *rate, int ratelen);

extern void process_addba_req(_adapter *padapter, u8 *paddba_req, u8 *addr);

extern void update_TSF(struct mlme_ext_priv *pmlmeext, u8 *pframe, uint len);
extern void correct_TSF(_adapter *padapter, struct mlme_ext_priv *pmlmeext);
extern void adaptive_early_32k(struct mlme_ext_priv *pmlmeext, u8 *pframe, uint len);
extern u8 traffic_status_watchdog(_adapter *padapter, u8 from_timer);


#ifdef CONFIG_CONCURRENT_MODE
 sint check_buddy_mlmeinfo_state(_adapter *padapter, u32 state);
void concurrent_chk_joinbss_done(_adapter *padapter, int join_res);
#endif //CONFIG_CONCURRENT_MODE

int rtw_chk_start_clnt_join(_adapter *padapter, u8 *ch, u8 *bw, u8 *offset);
int rtw_get_ch_setting_union(_adapter *adapter, u8 *ch, u8 *bw, u8 *offset);

struct cmd_hdl {
	uint	parmsize;
	u8 (*h2cfuns)(struct _ADAPTER *padapter, u8 *pbuf);	
};


u8 read_macreg_hdl(_adapter *padapter, u8 *pbuf);
u8 write_macreg_hdl(_adapter *padapter, u8 *pbuf);
u8 read_bbreg_hdl(_adapter *padapter, u8 *pbuf);
u8 write_bbreg_hdl(_adapter *padapter, u8 *pbuf);
u8 read_rfreg_hdl(_adapter *padapter, u8 *pbuf);
u8 write_rfreg_hdl(_adapter *padapter, u8 *pbuf);


u8 NULL_hdl(_adapter *padapter, u8 *pbuf);
u8 join_cmd_hdl(_adapter *padapter, u8 *pbuf);
u8 disconnect_hdl(_adapter *padapter, u8 *pbuf);
u8 createbss_hdl(_adapter *padapter, u8 *pbuf);
u8 setopmode_hdl(_adapter *padapter, u8 *pbuf);
u8 sitesurvey_cmd_hdl(_adapter *padapter, u8 *pbuf);	
u8 setauth_hdl(_adapter *padapter, u8 *pbuf);
u8 setkey_hdl(_adapter *padapter, u8 *pbuf);
u8 set_stakey_hdl(_adapter *padapter, u8 *pbuf);
u8 set_assocsta_hdl(_adapter *padapter, u8 *pbuf);
u8 del_assocsta_hdl(_adapter *padapter, u8 *pbuf);
u8 add_ba_hdl(_adapter *padapter, unsigned char *pbuf);

u8 mlme_evt_hdl(_adapter *padapter, unsigned char *pbuf);
u8 h2c_msg_hdl(_adapter *padapter, unsigned char *pbuf);
u8 chk_bmc_sleepq_hdl(_adapter *padapter, unsigned char *pbuf);
u8 tx_beacon_hdl(_adapter *padapter, unsigned char *pbuf);
u8 set_ch_hdl(_adapter *padapter, u8 *pbuf);
u8 set_chplan_hdl(_adapter *padapter, unsigned char *pbuf);
u8 led_blink_hdl(_adapter *padapter, unsigned char *pbuf);
u8 set_csa_hdl(_adapter *padapter, unsigned char *pbuf);	//Kurt: Handling DFS channel switch announcement ie.
u8 tdls_hdl(_adapter *padapter, unsigned char *pbuf);
u8 run_in_thread_hdl(_adapter *padapter, u8 *pbuf);


#define GEN_DRV_CMD_HANDLER(size, cmd)	{size, &cmd ## _hdl},
#define GEN_MLME_EXT_HANDLER(size, cmd)	{size, cmd},

#ifdef _RTW_CMD_C_

struct cmd_hdl wlancmds[] = 
{
	GEN_DRV_CMD_HANDLER(0, NULL) /*0*/
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL) /*10*/
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)		
	GEN_MLME_EXT_HANDLER(sizeof (struct joinbss_parm), join_cmd_hdl) /*14*/
	GEN_MLME_EXT_HANDLER(sizeof (struct disconnect_parm), disconnect_hdl)
	GEN_MLME_EXT_HANDLER(sizeof (struct createbss_parm), createbss_hdl)
	GEN_MLME_EXT_HANDLER(sizeof (struct setopmode_parm), setopmode_hdl)
	GEN_MLME_EXT_HANDLER(sizeof (struct sitesurvey_parm), sitesurvey_cmd_hdl) /*18*/
	GEN_MLME_EXT_HANDLER(sizeof (struct setauth_parm), setauth_hdl)
	GEN_MLME_EXT_HANDLER(sizeof (struct setkey_parm), setkey_hdl) /*20*/
	GEN_MLME_EXT_HANDLER(sizeof (struct set_stakey_parm), set_stakey_hdl)
	GEN_MLME_EXT_HANDLER(sizeof (struct set_assocsta_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct del_assocsta_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct setstapwrstate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct setbasicrate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct getbasicrate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct setdatarate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct getdatarate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct setphyinfo_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct getphyinfo_parm), NULL)  /*30*/
	GEN_MLME_EXT_HANDLER(sizeof (struct setphy_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct getphy_parm), NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)	/*40*/
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct addBaReq_parm), add_ba_hdl)	
	GEN_MLME_EXT_HANDLER(sizeof(struct set_ch_parm), set_ch_hdl) /* 46 */
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL) /*50*/
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL) 
	GEN_MLME_EXT_HANDLER(sizeof(struct Tx_Beacon_param), tx_beacon_hdl) /*55*/

	GEN_MLME_EXT_HANDLER(0, mlme_evt_hdl) /*56*/
	GEN_MLME_EXT_HANDLER(0, rtw_drvextra_cmd_hdl) /*57*/

	GEN_MLME_EXT_HANDLER(0, h2c_msg_hdl) /*58*/
	GEN_MLME_EXT_HANDLER(sizeof(struct SetChannelPlan_param), set_chplan_hdl) /*59*/
	GEN_MLME_EXT_HANDLER(sizeof(struct LedBlink_param), led_blink_hdl) /*60*/

	GEN_MLME_EXT_HANDLER(sizeof(struct SetChannelSwitch_param), set_csa_hdl) /*61*/
	GEN_MLME_EXT_HANDLER(sizeof(struct TDLSoption_param), tdls_hdl) /*62*/
	GEN_MLME_EXT_HANDLER(0, chk_bmc_sleepq_hdl) /*63*/
	GEN_MLME_EXT_HANDLER(sizeof(struct RunInThread_param), run_in_thread_hdl) /*64*/
};

#endif

struct C2HEvent_Header
{

#ifdef CONFIG_LITTLE_ENDIAN

	unsigned int len:16;
	unsigned int ID:8;
	unsigned int seq:8;
	
#elif defined(CONFIG_BIG_ENDIAN)

	unsigned int seq:8;
	unsigned int ID:8;
	unsigned int len:16;
	
#else

#  error "Must be LITTLE or BIG Endian"

#endif

	unsigned int rsvd;

};

void rtw_dummy_event_callback(_adapter *adapter , u8 *pbuf);
void rtw_fwdbg_event_callback(_adapter *adapter , u8 *pbuf);

enum rtw_c2h_event
{
	GEN_EVT_CODE(_Read_MACREG)=0, /*0*/
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
	GEN_EVT_CODE(_ReportPwrState),		//filen: only for PCIE, USB	
	GEN_EVT_CODE(_CloseRF),				//filen: only for PCIE, work around ASPM
	GEN_EVT_CODE(_WMM),					/*25*/
 	MAX_C2HEVT
};


#ifdef _RTW_MLME_EXT_C_		

static struct fwevent wlanevents[] = 
{
	{0, rtw_dummy_event_callback}, 	/*0*/
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, &rtw_survey_event_callback},		/*8*/
	{sizeof (struct surveydone_event), &rtw_surveydone_event_callback},	/*9*/
		
	{0, &rtw_joinbss_event_callback},		/*10*/
	{sizeof(struct stassoc_event), &rtw_stassoc_event_callback},
	{sizeof(struct stadel_event), &rtw_stadel_event_callback},	
	{0, &rtw_atimdone_event_callback},
	{0, rtw_dummy_event_callback},
	{0, NULL},	/*15*/
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, rtw_fwdbg_event_callback},
	{0, NULL},	 /*20*/
	{0, NULL},
	{0, NULL},
	{0, &rtw_cpwm_event_callback},
	{0, NULL},
	{0, &rtw_wmm_event_callback},

};

#endif//_RTW_MLME_EXT_C_

#endif

