// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _RTW_MLME_EXT_C_

#include <drv_types.h>
#include <rtw_debug.h>
#include <rtw_wifi_regd.h>
#include <linux/kernel.h>


static struct mlme_handler mlme_sta_tbl[] = {
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

static struct action_handler OnAction_tbl[] = {
	{RTW_WLAN_CATEGORY_SPECTRUM_MGMT,	 "ACTION_SPECTRUM_MGMT", on_action_spct},
	{RTW_WLAN_CATEGORY_QOS, "ACTION_QOS", &DoReserved},
	{RTW_WLAN_CATEGORY_DLS, "ACTION_DLS", &DoReserved},
	{RTW_WLAN_CATEGORY_BACK, "ACTION_BACK", &OnAction_back},
	{RTW_WLAN_CATEGORY_PUBLIC, "ACTION_PUBLIC", on_action_public},
	{RTW_WLAN_CATEGORY_RADIO_MEASUREMENT, "ACTION_RADIO_MEASUREMENT", &DoReserved},
	{RTW_WLAN_CATEGORY_FT, "ACTION_FT",	&DoReserved},
	{RTW_WLAN_CATEGORY_HT,	"ACTION_HT",	&OnAction_ht},
	{RTW_WLAN_CATEGORY_SA_QUERY, "ACTION_SA_QUERY", &OnAction_sa_query},
	{RTW_WLAN_CATEGORY_UNPROTECTED_WNM, "ACTION_UNPROTECTED_WNM", &DoReserved},
	{RTW_WLAN_CATEGORY_SELF_PROTECTED, "ACTION_SELF_PROTECTED", &DoReserved},
	{RTW_WLAN_CATEGORY_WMM, "ACTION_WMM", &DoReserved},
	{RTW_WLAN_CATEGORY_VHT, "ACTION_VHT", &DoReserved},
	{RTW_WLAN_CATEGORY_P2P, "ACTION_P2P", &DoReserved},
};


static u8 null_addr[ETH_ALEN] = {0, 0, 0, 0, 0, 0};

/**************************************************
OUI definitions for the vendor specific IE
***************************************************/
unsigned char RTW_WPA_OUI[] = {0x00, 0x50, 0xf2, 0x01};
unsigned char WMM_OUI[] = {0x00, 0x50, 0xf2, 0x02};
unsigned char WPS_OUI[] = {0x00, 0x50, 0xf2, 0x04};
unsigned char P2P_OUI[] = {0x50, 0x6F, 0x9A, 0x09};
unsigned char WFD_OUI[] = {0x50, 0x6F, 0x9A, 0x0A};

unsigned char WMM_INFO_OUI[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01};
unsigned char WMM_PARA_OUI[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};

static unsigned char REALTEK_96B_IE[] = {0x00, 0xe0, 0x4c, 0x02, 0x01, 0x20};

/********************************************************
ChannelPlan definitions
*********************************************************/
static RT_CHANNEL_PLAN_2G	RTW_ChannelPlan2G[RT_CHANNEL_DOMAIN_2G_MAX] = {
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},		/*  0x00, RT_CHANNEL_DOMAIN_2G_WORLD , Passive scan CH 12, 13 */
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},		/*  0x01, RT_CHANNEL_DOMAIN_2G_ETSI1 */
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, 11},			/*  0x02, RT_CHANNEL_DOMAIN_2G_FCC1 */
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}, 14},	/*  0x03, RT_CHANNEL_DOMAIN_2G_MIKK1 */
	{{10, 11, 12, 13}, 4},						/*  0x04, RT_CHANNEL_DOMAIN_2G_ETSI2 */
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}, 14},	/*  0x05, RT_CHANNEL_DOMAIN_2G_GLOBAL , Passive scan CH 12, 13, 14 */
	{{}, 0},								/*  0x06, RT_CHANNEL_DOMAIN_2G_NULL */
};

static RT_CHANNEL_PLAN_5G	RTW_ChannelPlan5G[RT_CHANNEL_DOMAIN_5G_MAX] = {
	{{}, 0},																					/*  0x00, RT_CHANNEL_DOMAIN_5G_NULL */
	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 19},						/*  0x01, RT_CHANNEL_DOMAIN_5G_ETSI1 */
	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165}, 24},	/*  0x02, RT_CHANNEL_DOMAIN_5G_ETSI2 */
	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 149, 153, 157, 161, 165}, 22},			/*  0x03, RT_CHANNEL_DOMAIN_5G_ETSI3 */
	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165}, 24},	/*  0x04, RT_CHANNEL_DOMAIN_5G_FCC1 */
	{{36, 40, 44, 48, 149, 153, 157, 161, 165}, 9},														/*  0x05, RT_CHANNEL_DOMAIN_5G_FCC2 */
	{{36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165}, 13},											/*  0x06, RT_CHANNEL_DOMAIN_5G_FCC3 */
	{{36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161}, 12},												/*  0x07, RT_CHANNEL_DOMAIN_5G_FCC4 */
	{{149, 153, 157, 161, 165}, 5},																	/*  0x08, RT_CHANNEL_DOMAIN_5G_FCC5 */
	{{36, 40, 44, 48, 52, 56, 60, 64}, 8},																/*  0x09, RT_CHANNEL_DOMAIN_5G_FCC6 */
	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 20},					/*  0x0A, RT_CHANNEL_DOMAIN_5G_FCC7_IC1 */
	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 149, 153, 157, 161, 165}, 20},					/*  0x0B, RT_CHANNEL_DOMAIN_5G_KCC1 */
	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 19},						/*  0x0C, RT_CHANNEL_DOMAIN_5G_MKK1 */
	{{36, 40, 44, 48, 52, 56, 60, 64}, 8},																/*  0x0D, RT_CHANNEL_DOMAIN_5G_MKK2 */
	{{100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 11},											/*  0x0E, RT_CHANNEL_DOMAIN_5G_MKK3 */
	{{56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 15},								/*  0x0F, RT_CHANNEL_DOMAIN_5G_NCC1 */
	{{56, 60, 64, 149, 153, 157, 161, 165}, 8},															/*  0x10, RT_CHANNEL_DOMAIN_5G_NCC2 */
	{{149, 153, 157, 161, 165}, 5},																	/*  0x11, RT_CHANNEL_DOMAIN_5G_NCC3 */
	{{36, 40, 44, 48}, 4},																			/*  0x12, RT_CHANNEL_DOMAIN_5G_ETSI4 */
	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 20},					/*  0x13, RT_CHANNEL_DOMAIN_5G_ETSI5 */
	{{149, 153, 157, 161}, 4},																		/*  0x14, RT_CHANNEL_DOMAIN_5G_FCC8 */
	{{36, 40, 44, 48, 52, 56, 60, 64}, 8},																/*  0x15, RT_CHANNEL_DOMAIN_5G_ETSI6 */
	{{36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165}, 13},											/*  0x16, RT_CHANNEL_DOMAIN_5G_ETSI7 */
	{{36, 40, 44, 48, 149, 153, 157, 161, 165}, 9},														/*  0x17, RT_CHANNEL_DOMAIN_5G_ETSI8 */
	{{100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 11},											/*  0x18, RT_CHANNEL_DOMAIN_5G_ETSI9 */
	{{149, 153, 157, 161, 165}, 5},																	/*  0x19, RT_CHANNEL_DOMAIN_5G_ETSI10 */
	{{36, 40, 44, 48, 52, 56, 60, 64, 132, 136, 140, 149, 153, 157, 161, 165}, 16},									/*  0x1A, RT_CHANNEL_DOMAIN_5G_ETSI11 */
	{{52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165}, 17},							/*  0x1B, RT_CHANNEL_DOMAIN_5G_NCC4 */
	{{149, 153, 157, 161}, 4},																		/*  0x1C, RT_CHANNEL_DOMAIN_5G_ETSI12 */
	{{36, 40, 44, 48, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165}, 17},							/*  0x1D, RT_CHANNEL_DOMAIN_5G_FCC9 */
	{{36, 40, 44, 48, 100, 104, 108, 112, 116, 132, 136, 140}, 12},											/*  0x1E, RT_CHANNEL_DOMAIN_5G_ETSI13 */
	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161}, 20},					/*  0x1F, RT_CHANNEL_DOMAIN_5G_FCC10 */

	/*  Driver self defined for old channel plan Compatible , Remember to modify if have new channel plan definition ===== */
	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165}, 21},				/*  0x20, RT_CHANNEL_DOMAIN_5G_FCC */
	{{36, 40, 44, 48}, 4},																			/*  0x21, RT_CHANNEL_DOMAIN_5G_JAPAN_NO_DFS */
	{{36, 40, 44, 48, 149, 153, 157, 161}, 8},															/*  0x22, RT_CHANNEL_DOMAIN_5G_FCC4_NO_DFS */
};

static RT_CHANNEL_PLAN_MAP	RTW_ChannelPlanMap[RT_CHANNEL_DOMAIN_MAX] = {
	/*  0x00 ~ 0x1F , Old Define ===== */
	{0x02, 0x20},	/* 0x00, RT_CHANNEL_DOMAIN_FCC */
	{0x02, 0x0A},	/* 0x01, RT_CHANNEL_DOMAIN_IC */
	{0x01, 0x01},	/* 0x02, RT_CHANNEL_DOMAIN_ETSI */
	{0x01, 0x00},	/* 0x03, RT_CHANNEL_DOMAIN_SPAIN */
	{0x01, 0x00},	/* 0x04, RT_CHANNEL_DOMAIN_FRANCE */
	{0x03, 0x00},	/* 0x05, RT_CHANNEL_DOMAIN_MKK */
	{0x03, 0x00},	/* 0x06, RT_CHANNEL_DOMAIN_MKK1 */
	{0x01, 0x09},	/* 0x07, RT_CHANNEL_DOMAIN_ISRAEL */
	{0x03, 0x09},	/* 0x08, RT_CHANNEL_DOMAIN_TELEC */
	{0x03, 0x00},	/* 0x09, RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN */
	{0x00, 0x00},	/* 0x0A, RT_CHANNEL_DOMAIN_WORLD_WIDE_13 */
	{0x02, 0x0F},	/* 0x0B, RT_CHANNEL_DOMAIN_TAIWAN */
	{0x01, 0x08},	/* 0x0C, RT_CHANNEL_DOMAIN_CHINA */
	{0x02, 0x06},	/* 0x0D, RT_CHANNEL_DOMAIN_SINGAPORE_INDIA_MEXICO */
	{0x02, 0x0B},	/* 0x0E, RT_CHANNEL_DOMAIN_KOREA */
	{0x02, 0x09},	/* 0x0F, RT_CHANNEL_DOMAIN_TURKEY */
	{0x01, 0x01},	/* 0x10, RT_CHANNEL_DOMAIN_JAPAN */
	{0x02, 0x05},	/* 0x11, RT_CHANNEL_DOMAIN_FCC_NO_DFS */
	{0x01, 0x21},	/* 0x12, RT_CHANNEL_DOMAIN_JAPAN_NO_DFS */
	{0x00, 0x04},	/* 0x13, RT_CHANNEL_DOMAIN_WORLD_WIDE_5G */
	{0x02, 0x10},	/* 0x14, RT_CHANNEL_DOMAIN_TAIWAN_NO_DFS */
	{0x00, 0x21},	/* 0x15, RT_CHANNEL_DOMAIN_ETSI_NO_DFS */
	{0x00, 0x22},	/* 0x16, RT_CHANNEL_DOMAIN_KOREA_NO_DFS */
	{0x03, 0x21},	/* 0x17, RT_CHANNEL_DOMAIN_JAPAN_NO_DFS */
	{0x06, 0x08},	/* 0x18, RT_CHANNEL_DOMAIN_PAKISTAN_NO_DFS */
	{0x02, 0x08},	/* 0x19, RT_CHANNEL_DOMAIN_TAIWAN2_NO_DFS */
	{0x00, 0x00},	/* 0x1A, */
	{0x00, 0x00},	/* 0x1B, */
	{0x00, 0x00},	/* 0x1C, */
	{0x00, 0x00},	/* 0x1D, */
	{0x00, 0x00},	/* 0x1E, */
	{0x06, 0x04},	/* 0x1F, RT_CHANNEL_DOMAIN_WORLD_WIDE_ONLY_5G */
	/*  0x20 ~ 0x7F , New Define ===== */
	{0x00, 0x00},	/* 0x20, RT_CHANNEL_DOMAIN_WORLD_NULL */
	{0x01, 0x00},	/* 0x21, RT_CHANNEL_DOMAIN_ETSI1_NULL */
	{0x02, 0x00},	/* 0x22, RT_CHANNEL_DOMAIN_FCC1_NULL */
	{0x03, 0x00},	/* 0x23, RT_CHANNEL_DOMAIN_MKK1_NULL */
	{0x04, 0x00},	/* 0x24, RT_CHANNEL_DOMAIN_ETSI2_NULL */
	{0x02, 0x04},	/* 0x25, RT_CHANNEL_DOMAIN_FCC1_FCC1 */
	{0x00, 0x01},	/* 0x26, RT_CHANNEL_DOMAIN_WORLD_ETSI1 */
	{0x03, 0x0C},	/* 0x27, RT_CHANNEL_DOMAIN_MKK1_MKK1 */
	{0x00, 0x0B},	/* 0x28, RT_CHANNEL_DOMAIN_WORLD_KCC1 */
	{0x00, 0x05},	/* 0x29, RT_CHANNEL_DOMAIN_WORLD_FCC2 */
	{0x00, 0x00},	/* 0x2A, */
	{0x00, 0x00},	/* 0x2B, */
	{0x00, 0x00},	/* 0x2C, */
	{0x00, 0x00},	/* 0x2D, */
	{0x00, 0x00},	/* 0x2E, */
	{0x00, 0x00},	/* 0x2F, */
	{0x00, 0x06},	/* 0x30, RT_CHANNEL_DOMAIN_WORLD_FCC3 */
	{0x00, 0x07},	/* 0x31, RT_CHANNEL_DOMAIN_WORLD_FCC4 */
	{0x00, 0x08},	/* 0x32, RT_CHANNEL_DOMAIN_WORLD_FCC5 */
	{0x00, 0x09},	/* 0x33, RT_CHANNEL_DOMAIN_WORLD_FCC6 */
	{0x02, 0x0A},	/* 0x34, RT_CHANNEL_DOMAIN_FCC1_FCC7 */
	{0x00, 0x02},	/* 0x35, RT_CHANNEL_DOMAIN_WORLD_ETSI2 */
	{0x00, 0x03},	/* 0x36, RT_CHANNEL_DOMAIN_WORLD_ETSI3 */
	{0x03, 0x0D},	/* 0x37, RT_CHANNEL_DOMAIN_MKK1_MKK2 */
	{0x03, 0x0E},	/* 0x38, RT_CHANNEL_DOMAIN_MKK1_MKK3 */
	{0x02, 0x0F},	/* 0x39, RT_CHANNEL_DOMAIN_FCC1_NCC1 */
	{0x00, 0x00},	/* 0x3A, */
	{0x00, 0x00},	/* 0x3B, */
	{0x00, 0x00},	/* 0x3C, */
	{0x00, 0x00},	/* 0x3D, */
	{0x00, 0x00},	/* 0x3E, */
	{0x00, 0x00},	/* 0x3F, */
	{0x02, 0x10},	/* 0x40, RT_CHANNEL_DOMAIN_FCC1_NCC2 */
	{0x05, 0x00},	/* 0x41, RT_CHANNEL_DOMAIN_GLOBAL_NULL */
	{0x01, 0x12},	/* 0x42, RT_CHANNEL_DOMAIN_ETSI1_ETSI4 */
	{0x02, 0x05},	/* 0x43, RT_CHANNEL_DOMAIN_FCC1_FCC2 */
	{0x02, 0x11},	/* 0x44, RT_CHANNEL_DOMAIN_FCC1_NCC3 */
	{0x00, 0x13},	/* 0x45, RT_CHANNEL_DOMAIN_WORLD_ETSI5 */
	{0x02, 0x14},	/* 0x46, RT_CHANNEL_DOMAIN_FCC1_FCC8 */
	{0x00, 0x15},	/* 0x47, RT_CHANNEL_DOMAIN_WORLD_ETSI6 */
	{0x00, 0x16},	/* 0x48, RT_CHANNEL_DOMAIN_WORLD_ETSI7 */
	{0x00, 0x17},	/* 0x49, RT_CHANNEL_DOMAIN_WORLD_ETSI8 */
	{0x00, 0x18},	/* 0x50, RT_CHANNEL_DOMAIN_WORLD_ETSI9 */
	{0x00, 0x19},	/* 0x51, RT_CHANNEL_DOMAIN_WORLD_ETSI10 */
	{0x00, 0x1A},	/* 0x52, RT_CHANNEL_DOMAIN_WORLD_ETSI11 */
	{0x02, 0x1B},	/* 0x53, RT_CHANNEL_DOMAIN_FCC1_NCC4 */
	{0x00, 0x1C},	/* 0x54, RT_CHANNEL_DOMAIN_WORLD_ETSI12 */
	{0x02, 0x1D},	/* 0x55, RT_CHANNEL_DOMAIN_FCC1_FCC9 */
	{0x00, 0x1E},	/* 0x56, RT_CHANNEL_DOMAIN_WORLD_ETSI13 */
	{0x02, 0x1F},	/* 0x57, RT_CHANNEL_DOMAIN_FCC1_FCC10 */
};

static RT_CHANNEL_PLAN_MAP	RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE = {0x03, 0x02}; /* use the conbination for max channel numbers */

/*
 * Search the @param ch in given @param ch_set
 * @ch_set: the given channel set
 * @ch: the given channel number
 *
 * return the index of channel_num in channel_set, -1 if not found
 */
int rtw_ch_set_search_ch(RT_CHANNEL_INFO *ch_set, const u32 ch)
{
	int i;
	for (i = 0; ch_set[i].ChannelNum != 0; i++) {
		if (ch == ch_set[i].ChannelNum)
			break;
	}

	if (i >= ch_set[i].ChannelNum)
		return -1;
	return i;
}

/*
 * Check the @param ch is fit with setband setting of @param adapter
 * @adapter: the given adapter
 * @ch: the given channel number
 *
 * return true when check valid, false not valid
 */
bool rtw_mlme_band_check(struct adapter *adapter, const u32 ch)
{
	if (adapter->setband == GHZ24_50 /* 2.4G and 5G */
		|| (adapter->setband == GHZ_24 && ch < 35) /* 2.4G only */
		|| (adapter->setband == GHZ_50 && ch > 35) /* 5G only */
	) {
		return true;
	}
	return false;
}

/****************************************************************************

Following are the initialization functions for WiFi MLME

*****************************************************************************/

int init_hw_mlme_ext(struct adapter *padapter)
{
	struct	mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);
	return _SUCCESS;
}

void init_mlme_default_rate_set(struct adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	unsigned char mixed_datarate[NumRates] = {_1M_RATE_, _2M_RATE_, _5M_RATE_, _11M_RATE_, _6M_RATE_, _9M_RATE_, _12M_RATE_, _18M_RATE_, _24M_RATE_, _36M_RATE_, _48M_RATE_, _54M_RATE_, 0xff};
	unsigned char mixed_basicrate[NumRates] = {_1M_RATE_, _2M_RATE_, _5M_RATE_, _11M_RATE_, _6M_RATE_, _12M_RATE_, _24M_RATE_, 0xff,};
	unsigned char supported_mcs_set[16] = {0xff, 0xff, 0x00, 0x00, 0x01, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

	memcpy(pmlmeext->datarate, mixed_datarate, NumRates);
	memcpy(pmlmeext->basicrate, mixed_basicrate, NumRates);

	memcpy(pmlmeext->default_supported_mcs_set, supported_mcs_set, sizeof(pmlmeext->default_supported_mcs_set));
}

static void init_mlme_ext_priv_value(struct adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	atomic_set(&pmlmeext->event_seq, 0);
	pmlmeext->mgnt_seq = 0;/* reset to zero when disconnect at client mode */
	pmlmeext->sa_query_seq = 0;
	pmlmeext->mgnt_80211w_IPN = 0;
	pmlmeext->mgnt_80211w_IPN_rx = 0;
	pmlmeext->cur_channel = padapter->registrypriv.channel;
	pmlmeext->cur_bwmode = CHANNEL_WIDTH_20;
	pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	pmlmeext->retry = 0;

	pmlmeext->cur_wireless_mode = padapter->registrypriv.wireless_mode;

	init_mlme_default_rate_set(padapter);

	if (pmlmeext->cur_channel > 14)
		pmlmeext->tx_rate = IEEE80211_OFDM_RATE_6MB;
	else
		pmlmeext->tx_rate = IEEE80211_CCK_RATE_1MB;

	pmlmeext->sitesurvey_res.state = SCAN_DISABLE;
	pmlmeext->sitesurvey_res.channel_idx = 0;
	pmlmeext->sitesurvey_res.bss_cnt = 0;
	pmlmeext->scan_abort = false;

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

	memset(pmlmeinfo->chg_txt, 0, 128);

	pmlmeinfo->slotTime = SHORT_SLOT_TIME;
	pmlmeinfo->preamble_mode = PREAMBLE_AUTO;

	pmlmeinfo->dialogToken = 0;

	pmlmeext->action_public_rxseq = 0xffff;
	pmlmeext->action_public_dialog_token = 0xff;
}

static int has_channel(RT_CHANNEL_INFO *channel_set,
					   u8 chanset_size,
					   u8 chan)
{
	int i;

	for (i = 0; i < chanset_size; i++) {
		if (channel_set[i].ChannelNum == chan) {
			return 1;
		}
	}

	return 0;
}

static void init_channel_list(struct adapter *padapter, RT_CHANNEL_INFO *channel_set,
							  u8 chanset_size,
							  struct p2p_channels *channel_list)
{

	struct p2p_oper_class_map op_class[] = {
		{ IEEE80211G,  81,   1,  13,  1, BW20 },
		{ IEEE80211G,  82,  14,  14,  1, BW20 },
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
			if (!has_channel(channel_set, chanset_size, ch)) {
				continue;
			}

			if ((0 == padapter->registrypriv.ht_enable) && (8 == o->inc))
				continue;

			if ((0 < (padapter->registrypriv.bw_mode & 0xf0)) &&
				((BW40MINUS == o->bw) || (BW40PLUS == o->bw)))
				continue;

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

static u8 init_channel_set(struct adapter *padapter, u8 ChannelPlan, RT_CHANNEL_INFO *channel_set)
{
	u8 index, chanset_size = 0;
	u8 b5GBand = false, b2_4GBand = false;
	u8 Index2G = 0, Index5G = 0;

	memset(channel_set, 0, sizeof(RT_CHANNEL_INFO)*MAX_CHANNEL_NUM);

	if (ChannelPlan >= RT_CHANNEL_DOMAIN_MAX && ChannelPlan != RT_CHANNEL_DOMAIN_REALTEK_DEFINE) {
		DBG_871X("ChannelPlan ID %x error !!!!!\n", ChannelPlan);
		return chanset_size;
	}

	if (IsSupported24G(padapter->registrypriv.wireless_mode)) {
		b2_4GBand = true;
		if (RT_CHANNEL_DOMAIN_REALTEK_DEFINE == ChannelPlan)
			Index2G = RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE.Index2G;
		else
			Index2G = RTW_ChannelPlanMap[ChannelPlan].Index2G;
	}

	if (b2_4GBand) {
		for (index = 0; index < RTW_ChannelPlan2G[Index2G].Len; index++) {
			channel_set[chanset_size].ChannelNum = RTW_ChannelPlan2G[Index2G].Channel[index];

			if ((RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN == ChannelPlan) ||/* Channel 1~11 is active, and 12~14 is passive */
				(RT_CHANNEL_DOMAIN_GLOBAL_NULL == ChannelPlan)) {
				if (channel_set[chanset_size].ChannelNum >= 1 && channel_set[chanset_size].ChannelNum <= 11)
					channel_set[chanset_size].ScanType = SCAN_ACTIVE;
				else if ((channel_set[chanset_size].ChannelNum  >= 12 && channel_set[chanset_size].ChannelNum  <= 14))
					channel_set[chanset_size].ScanType  = SCAN_PASSIVE;
			} else if (RT_CHANNEL_DOMAIN_WORLD_WIDE_13 == ChannelPlan ||
				RT_CHANNEL_DOMAIN_WORLD_WIDE_5G == ChannelPlan ||
				RT_CHANNEL_DOMAIN_2G_WORLD == Index2G) { /*  channel 12~13, passive scan */
				if (channel_set[chanset_size].ChannelNum <= 11)
					channel_set[chanset_size].ScanType = SCAN_ACTIVE;
				else
					channel_set[chanset_size].ScanType = SCAN_PASSIVE;
			} else
				channel_set[chanset_size].ScanType = SCAN_ACTIVE;

			chanset_size++;
		}
	}

	if (b5GBand) {
		for (index = 0; index < RTW_ChannelPlan5G[Index5G].Len; index++) {
			if (RTW_ChannelPlan5G[Index5G].Channel[index] <= 48
				|| RTW_ChannelPlan5G[Index5G].Channel[index] >= 149) {
				channel_set[chanset_size].ChannelNum = RTW_ChannelPlan5G[Index5G].Channel[index];
				if (RT_CHANNEL_DOMAIN_WORLD_WIDE_5G == ChannelPlan)/* passive scan for all 5G channels */
					channel_set[chanset_size].ScanType = SCAN_PASSIVE;
				else
					channel_set[chanset_size].ScanType = SCAN_ACTIVE;
				DBG_871X("%s(): channel_set[%d].ChannelNum = %d\n", __func__, chanset_size, channel_set[chanset_size].ChannelNum);
				chanset_size++;
			}
		}
	}

	DBG_871X("%s ChannelPlan ID %x Chan num:%d \n", __func__, ChannelPlan, chanset_size);
	return chanset_size;
}

int	init_mlme_ext_priv(struct adapter *padapter)
{
	int	res = _SUCCESS;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	pmlmeext->padapter = padapter;

	/* fill_fwpriv(padapter, &(pmlmeext->fwpriv)); */

	init_mlme_ext_priv_value(padapter);
	pmlmeinfo->accept_addba_req = pregistrypriv->accept_addba_req;

	init_mlme_ext_timer(padapter);

	init_mlme_ap_info(padapter);

	pmlmeext->max_chan_nums = init_channel_set(padapter, pmlmepriv->ChannelPlan, pmlmeext->channel_set);
	init_channel_list(padapter, pmlmeext->channel_set, pmlmeext->max_chan_nums, &pmlmeext->channel_list);
	pmlmeext->last_scan_time = 0;
	pmlmeext->chan_scan_time = SURVEY_TO;
	pmlmeext->mlmeext_init = true;
	pmlmeext->active_keep_alive_check = true;

#ifdef DBG_FIXED_CHAN
	pmlmeext->fixed_chan = 0xFF;
#endif

	return res;

}

void free_mlme_ext_priv(struct mlme_ext_priv *pmlmeext)
{
	struct adapter *padapter = pmlmeext->padapter;

	if (!padapter)
		return;

	if (padapter->bDriverStopped) {
		del_timer_sync(&pmlmeext->survey_timer);
		del_timer_sync(&pmlmeext->link_timer);
		/* del_timer_sync(&pmlmeext->ADDBA_timer); */
	}
}

static void _mgt_dispatcher(struct adapter *padapter, struct mlme_handler *ptable, union recv_frame *precv_frame)
{
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u8 *pframe = precv_frame->u.hdr.rx_data;

	if (ptable->func) {
		/* receive the frames that ra(a1) is my address or ra(a1) is bc address. */
		if (memcmp(GetAddr1Ptr(pframe), myid(&padapter->eeprompriv), ETH_ALEN) &&
		    memcmp(GetAddr1Ptr(pframe), bc_addr, ETH_ALEN))
			return;

		ptable->func(padapter, precv_frame);
	}
}

void mgt_dispatcher(struct adapter *padapter, union recv_frame *precv_frame)
{
	int index;
	struct mlme_handler *ptable;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u8 *pframe = precv_frame->u.hdr.rx_data;
	struct sta_info *psta = rtw_get_stainfo(&padapter->stapriv, GetAddr2Ptr(pframe));
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("+mgt_dispatcher: type(0x%x) subtype(0x%x)\n",
		  GetFrameType(pframe), GetFrameSubType(pframe)));

	if (GetFrameType(pframe) != WIFI_MGT_TYPE) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("mgt_dispatcher: type(0x%x) error!\n", GetFrameType(pframe)));
		return;
	}

	/* receive the frames that ra(a1) is my address or ra(a1) is bc address. */
	if (memcmp(GetAddr1Ptr(pframe), myid(&padapter->eeprompriv), ETH_ALEN) &&
		memcmp(GetAddr1Ptr(pframe), bc_addr, ETH_ALEN)) {
		return;
	}

	ptable = mlme_sta_tbl;

	index = GetFrameSubType(pframe) >> 4;

	if (index >= ARRAY_SIZE(mlme_sta_tbl)) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("Currently we do not support reserved sub-fr-type =%d\n", index));
		return;
	}
	ptable += index;

	if (psta != NULL) {
		if (GetRetry(pframe)) {
			if (precv_frame->u.hdr.attrib.seq_num == psta->RxMgmtFrameSeqNum) {
				/* drop the duplicate management frame */
				pdbgpriv->dbg_rx_dup_mgt_frame_drop_count++;
				DBG_871X("Drop duplicate management frame with seq_num = %d.\n", precv_frame->u.hdr.attrib.seq_num);
				return;
			}
		}
		psta->RxMgmtFrameSeqNum = precv_frame->u.hdr.attrib.seq_num;
	}

	switch (GetFrameSubType(pframe)) {
	case WIFI_AUTH:
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE))
			ptable->func = &OnAuth;
		else
			ptable->func = &OnAuthClient;
		/* fall through */
	case WIFI_ASSOCREQ:
	case WIFI_REASSOCREQ:
		_mgt_dispatcher(padapter, ptable, precv_frame);
		break;
	case WIFI_PROBEREQ:
		_mgt_dispatcher(padapter, ptable, precv_frame);
		break;
	case WIFI_BEACON:
		_mgt_dispatcher(padapter, ptable, precv_frame);
		break;
	case WIFI_ACTION:
		/* if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) */
		_mgt_dispatcher(padapter, ptable, precv_frame);
		break;
	default:
		_mgt_dispatcher(padapter, ptable, precv_frame);
		break;
	}
}

/****************************************************************************

Following are the callback functions for each subtype of the management frames

*****************************************************************************/

unsigned int OnProbeReq(struct adapter *padapter, union recv_frame *precv_frame)
{
	unsigned int	ielen;
	unsigned char *p;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex	*cur = &(pmlmeinfo->network);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;
	u8 is_valid_p2p_probereq = false;

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
		return _SUCCESS;

	if (check_fwstate(pmlmepriv, _FW_LINKED) == false &&
		check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE|WIFI_AP_STATE) == false) {
		return _SUCCESS;
	}


	/* DBG_871X("+OnProbeReq\n"); */

#ifdef CONFIG_AUTO_AP_MODE
	if (check_fwstate(pmlmepriv, _FW_LINKED) &&
			pmlmepriv->cur_network.join_res) {
		struct sta_info *psta;
		u8 *mac_addr, *peer_addr;
		struct sta_priv *pstapriv = &padapter->stapriv;
		u8 RC_OUI[4] = {0x00, 0xE0, 0x4C, 0x0A};
		/* EID[1] + EID_LEN[1] + RC_OUI[4] + MAC[6] + PairingID[2] + ChannelNum[2] */

		p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_, _VENDOR_SPECIFIC_IE_, (int *)&ielen,
			len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_);

		if (!p || ielen != 14)
			goto _non_rc_device;

		if (memcmp(p+2, RC_OUI, sizeof(RC_OUI)))
			goto _non_rc_device;

		if (memcmp(p+6, get_sa(pframe), ETH_ALEN)) {
			DBG_871X("%s, do rc pairing ("MAC_FMT"), but mac addr mismatch!("MAC_FMT")\n", __func__,
				MAC_ARG(get_sa(pframe)), MAC_ARG(p+6));

			goto _non_rc_device;
		}

		DBG_871X("%s, got the pairing device("MAC_FMT")\n", __func__,  MAC_ARG(get_sa(pframe)));

		/* new a station */
		psta = rtw_get_stainfo(pstapriv, get_sa(pframe));
		if (psta == NULL) {
			/*  allocate a new one */
			DBG_871X("going to alloc stainfo for rc ="MAC_FMT"\n",  MAC_ARG(get_sa(pframe)));
			psta = rtw_alloc_stainfo(pstapriv, get_sa(pframe));
			if (psta == NULL) {
				/* TODO: */
				DBG_871X(" Exceed the upper limit of supported clients...\n");
				return _SUCCESS;
			}

			spin_lock_bh(&pstapriv->asoc_list_lock);
			if (list_empty(&psta->asoc_list)) {
				psta->expire_to = pstapriv->expire_to;
				list_add_tail(&psta->asoc_list, &pstapriv->asoc_list);
				pstapriv->asoc_list_cnt++;
			}
			spin_unlock_bh(&pstapriv->asoc_list_lock);

			/* generate pairing ID */
			mac_addr = myid(&(padapter->eeprompriv));
			peer_addr = psta->hwaddr;
			psta->pid = (u16)(((mac_addr[4]<<8) + mac_addr[5]) + ((peer_addr[4]<<8) + peer_addr[5]));

			/* update peer stainfo */
			psta->isrc = true;
			/* psta->aid = 0; */
			/* psta->mac_id = 2; */

			/* get a unique AID */
			if (psta->aid > 0) {
				DBG_871X("old AID %d\n", psta->aid);
			} else {
				for (psta->aid = 1; psta->aid <= NUM_STA; psta->aid++)
					if (pstapriv->sta_aid[psta->aid - 1] == NULL)
						break;

				if (psta->aid > pstapriv->max_num_sta) {
					psta->aid = 0;
					DBG_871X("no room for more AIDs\n");
					return _SUCCESS;
				} else {
					pstapriv->sta_aid[psta->aid - 1] = psta;
					DBG_871X("allocate new AID = (%d)\n", psta->aid);
				}
			}

			psta->qos_option = 1;
			psta->bw_mode = CHANNEL_WIDTH_20;
			psta->ieee8021x_blocked = false;
			psta->htpriv.ht_option = true;
			psta->htpriv.ampdu_enable = false;
			psta->htpriv.sgi_20m = false;
			psta->htpriv.sgi_40m = false;
			psta->htpriv.ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			psta->htpriv.agg_enable_bitmap = 0x0;/* reset */
			psta->htpriv.candidate_tid_bitmap = 0x0;/* reset */

			rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, psta, true);

			memset((void *)&psta->sta_stats, 0, sizeof(struct stainfo_stats));

			spin_lock_bh(&psta->lock);
			psta->state |= _FW_LINKED;
			spin_unlock_bh(&psta->lock);

			report_add_sta_event(padapter, psta->hwaddr, psta->aid);

		}

		issue_probersp(padapter, get_sa(pframe), false);

		return _SUCCESS;

	}

_non_rc_device:

	return _SUCCESS;

#endif /* CONFIG_AUTO_AP_MODE */

	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_, _SSID_IE_, (int *)&ielen,
			len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_);


	/* check (wildcard) SSID */
	if (p != NULL) {
		if (is_valid_p2p_probereq)
			goto _issue_probersp;

		if ((ielen != 0 && false == !memcmp((void *)(p+2), (void *)cur->Ssid.Ssid, cur->Ssid.SsidLength))
			|| (ielen == 0 && pmlmeinfo->hidden_ssid_mode)
		)
			return _SUCCESS;

_issue_probersp:
		if ((check_fwstate(pmlmepriv, _FW_LINKED)  &&
			pmlmepriv->cur_network.join_res) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) {
			/* DBG_871X("+issue_probersp during ap mode\n"); */
			issue_probersp(padapter, get_sa(pframe), is_valid_p2p_probereq);
		}

	}

	return _SUCCESS;

}

unsigned int OnProbeRsp(struct adapter *padapter, union recv_frame *precv_frame)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS) {
		report_survey_event(padapter, precv_frame);
		return _SUCCESS;
	}

	return _SUCCESS;

}

unsigned int OnBeacon(struct adapter *padapter, union recv_frame *precv_frame)
{
	int cam_idx;
	struct sta_info *psta;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;
	struct wlan_bssid_ex *pbss;
	int ret = _SUCCESS;
	u8 *p = NULL;
	u32 ielen = 0;

	p = rtw_get_ie(pframe + sizeof(struct ieee80211_hdr_3addr) + _BEACON_IE_OFFSET_, _EXT_SUPPORTEDRATES_IE_, &ielen, precv_frame->u.hdr.len - sizeof(struct ieee80211_hdr_3addr) - _BEACON_IE_OFFSET_);
	if ((p != NULL) && (ielen > 0)) {
		if ((*(p + 1 + ielen) == 0x2D) && (*(p + 2 + ielen) != 0x2D)) {
			/* Invalid value 0x2D is detected in Extended Supported Rates (ESR) IE. Try to fix the IE length to avoid failed Beacon parsing. */
			DBG_871X("[WIFIDBG] Error in ESR IE is detected in Beacon of BSSID:"MAC_FMT". Fix the length of ESR IE to avoid failed Beacon parsing.\n", MAC_ARG(GetAddr3Ptr(pframe)));
			*(p + 1) = ielen - 1;
		}
	}

	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS) {
		report_survey_event(padapter, precv_frame);
		return _SUCCESS;
	}

	if (!memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN)) {
		if (pmlmeinfo->state & WIFI_FW_AUTH_NULL) {
			/* we should update current network before auth, or some IE is wrong */
			pbss = rtw_malloc(sizeof(struct wlan_bssid_ex));
			if (pbss) {
				if (collect_bss_info(padapter, precv_frame, pbss) == _SUCCESS) {
					update_network(&(pmlmepriv->cur_network.network), pbss, padapter, true);
					rtw_get_bcn_info(&(pmlmepriv->cur_network));
				}
				kfree((u8 *)pbss);
			}

			/* check the vendor of the assoc AP */
			pmlmeinfo->assoc_AP_vendor = check_assoc_AP(pframe+sizeof(struct ieee80211_hdr_3addr), len-sizeof(struct ieee80211_hdr_3addr));

			/* update TSF Value */
			update_TSF(pmlmeext, pframe, len);

			/* reset for adaptive_early_32k */
			pmlmeext->adaptive_tsf_done = false;
			pmlmeext->DrvBcnEarly = 0xff;
			pmlmeext->DrvBcnTimeOut = 0xff;
			pmlmeext->bcn_cnt = 0;
			memset(pmlmeext->bcn_delay_cnt, 0, sizeof(pmlmeext->bcn_delay_cnt));
			memset(pmlmeext->bcn_delay_ratio, 0, sizeof(pmlmeext->bcn_delay_ratio));

			/* start auth */
			start_clnt_auth(padapter);

			return _SUCCESS;
		}

		if (((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE) && (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)) {
			psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
			if (psta != NULL) {
				ret = rtw_check_bcn_info(padapter, pframe, len);
				if (!ret) {
						DBG_871X_LEVEL(_drv_always_, "ap has changed, disconnect now\n ");
						receive_disconnect(padapter, pmlmeinfo->network.MacAddress, 0);
						return _SUCCESS;
				}
				/* update WMM, ERP in the beacon */
				/* todo: the timer is used instead of the number of the beacon received */
				if ((sta_rx_pkts(psta) & 0xf) == 0)
					/* DBG_871X("update_bcn_info\n"); */
					update_beacon_info(padapter, pframe, len, psta);

				adaptive_early_32k(pmlmeext, pframe, len);
			}
		} else if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) {
			psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
			if (psta != NULL) {
				/* update WMM, ERP in the beacon */
				/* todo: the timer is used instead of the number of the beacon received */
				if ((sta_rx_pkts(psta) & 0xf) == 0) {
					/* DBG_871X("update_bcn_info\n"); */
					update_beacon_info(padapter, pframe, len, psta);
				}
			} else{
				/* allocate a new CAM entry for IBSS station */
				cam_idx = allocate_fw_sta_entry(padapter);
				if (cam_idx == NUM_STA)
					goto _END_ONBEACON_;

				/* get supported rate */
				if (update_sta_support_rate(padapter, (pframe + WLAN_HDR_A3_LEN + _BEACON_IE_OFFSET_), (len - WLAN_HDR_A3_LEN - _BEACON_IE_OFFSET_), cam_idx) == _FAIL) {
					pmlmeinfo->FW_sta_info[cam_idx].status = 0;
					goto _END_ONBEACON_;
				}

				/* update TSF Value */
				update_TSF(pmlmeext, pframe, len);

				/* report sta add event */
				report_add_sta_event(padapter, GetAddr2Ptr(pframe), cam_idx);
			}
		}
	}

_END_ONBEACON_:

	return _SUCCESS;

}

unsigned int OnAuth(struct adapter *padapter, union recv_frame *precv_frame)
{
	unsigned int	auth_mode, seq, ie_len;
	unsigned char *sa, *p;
	u16 algorithm;
	int	status;
	static struct sta_info stat;
	struct	sta_info *pstat = NULL;
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;
	u8 offset = 0;

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return _FAIL;

	DBG_871X("+OnAuth\n");

	sa = GetAddr2Ptr(pframe);

	auth_mode = psecuritypriv->dot11AuthAlgrthm;

	if (GetPrivacy(pframe)) {
		u8 *iv;
		struct rx_pkt_attrib	 *prxattrib = &(precv_frame->u.hdr.attrib);

		prxattrib->hdrlen = WLAN_HDR_A3_LEN;
		prxattrib->encrypt = _WEP40_;

		iv = pframe+prxattrib->hdrlen;
		prxattrib->key_index = ((iv[3]>>6)&0x3);

		prxattrib->iv_len = 4;
		prxattrib->icv_len = 4;

		rtw_wep_decrypt(padapter, (u8 *)precv_frame);

		offset = 4;
	}

	algorithm = le16_to_cpu(*(__le16 *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset));
	seq	= le16_to_cpu(*(__le16 *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset + 2));

	DBG_871X("auth alg =%x, seq =%X\n", algorithm, seq);

	if (auth_mode == 2 &&
			psecuritypriv->dot11PrivacyAlgrthm != _WEP40_ &&
			psecuritypriv->dot11PrivacyAlgrthm != _WEP104_)
		auth_mode = 0;

	if ((algorithm > 0 && auth_mode == 0) ||	/*  rx a shared-key auth but shared not enabled */
		(algorithm == 0 && auth_mode == 1)) {	/*  rx a open-system auth but shared-key is enabled */
		DBG_871X("auth rejected due to bad alg [alg =%d, auth_mib =%d] %02X%02X%02X%02X%02X%02X\n",
			algorithm, auth_mode, sa[0], sa[1], sa[2], sa[3], sa[4], sa[5]);

		status = _STATS_NO_SUPP_ALG_;

		goto auth_fail;
	}

	if (rtw_access_ctrl(padapter, sa) == false) {
		status = _STATS_UNABLE_HANDLE_STA_;
		goto auth_fail;
	}

	pstat = rtw_get_stainfo(pstapriv, sa);
	if (pstat == NULL) {

		/*  allocate a new one */
		DBG_871X("going to alloc stainfo for sa ="MAC_FMT"\n",  MAC_ARG(sa));
		pstat = rtw_alloc_stainfo(pstapriv, sa);
		if (pstat == NULL) {
			DBG_871X(" Exceed the upper limit of supported clients...\n");
			status = _STATS_UNABLE_HANDLE_STA_;
			goto auth_fail;
		}

		pstat->state = WIFI_FW_AUTH_NULL;
		pstat->auth_seq = 0;

		/* pstat->flags = 0; */
		/* pstat->capability = 0; */
	} else{

		spin_lock_bh(&pstapriv->asoc_list_lock);
		if (list_empty(&pstat->asoc_list) == false) {
			list_del_init(&pstat->asoc_list);
			pstapriv->asoc_list_cnt--;
			if (pstat->expire_to > 0) {
				/* TODO: STA re_auth within expire_to */
			}
		}
		spin_unlock_bh(&pstapriv->asoc_list_lock);

		if (seq == 1) {
			/* TODO: STA re_auth and auth timeout */
		}
	}

	spin_lock_bh(&pstapriv->auth_list_lock);
	if (list_empty(&pstat->auth_list)) {

		list_add_tail(&pstat->auth_list, &pstapriv->auth_list);
		pstapriv->auth_list_cnt++;
	}
	spin_unlock_bh(&pstapriv->auth_list_lock);

	if (pstat->auth_seq == 0)
		pstat->expire_to = pstapriv->auth_to;


	if ((pstat->auth_seq + 1) != seq) {
		DBG_871X("(1)auth rejected because out of seq [rx_seq =%d, exp_seq =%d]!\n",
			seq, pstat->auth_seq+1);
		status = _STATS_OUT_OF_AUTH_SEQ_;
		goto auth_fail;
	}

	if (algorithm == 0 && (auth_mode == 0 || auth_mode == 2 || auth_mode == 3)) {
		if (seq == 1) {
			pstat->state &= ~WIFI_FW_AUTH_NULL;
			pstat->state |= WIFI_FW_AUTH_SUCCESS;
			pstat->expire_to = pstapriv->assoc_to;
			pstat->authalg = algorithm;
		} else{
			DBG_871X("(2)auth rejected because out of seq [rx_seq =%d, exp_seq =%d]!\n",
				seq, pstat->auth_seq+1);
			status = _STATS_OUT_OF_AUTH_SEQ_;
			goto auth_fail;
		}
	} else{ /*  shared system or auto authentication */
		if (seq == 1) {
			/* prepare for the challenging txt... */
			memset((void *)pstat->chg_txt, 78, 128);

			pstat->state &= ~WIFI_FW_AUTH_NULL;
			pstat->state |= WIFI_FW_AUTH_STATE;
			pstat->authalg = algorithm;
			pstat->auth_seq = 2;
		} else if (seq == 3) {
			/* checking for challenging txt... */
			DBG_871X("checking for challenging txt...\n");

			p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + 4 + _AUTH_IE_OFFSET_, _CHLGETXT_IE_, (int *)&ie_len,
					len - WLAN_HDR_A3_LEN - _AUTH_IE_OFFSET_ - 4);

			if ((p == NULL) || (ie_len <= 0)) {
				DBG_871X("auth rejected because challenge failure!(1)\n");
				status = _STATS_CHALLENGE_FAIL_;
				goto auth_fail;
			}

			if (!memcmp((void *)(p + 2), pstat->chg_txt, 128)) {
				pstat->state &= (~WIFI_FW_AUTH_STATE);
				pstat->state |= WIFI_FW_AUTH_SUCCESS;
				/*  challenging txt is correct... */
				pstat->expire_to =  pstapriv->assoc_to;
			} else{
				DBG_871X("auth rejected because challenge failure!\n");
				status = _STATS_CHALLENGE_FAIL_;
				goto auth_fail;
			}
		} else{
			DBG_871X("(3)auth rejected because out of seq [rx_seq =%d, exp_seq =%d]!\n",
				seq, pstat->auth_seq+1);
			status = _STATS_OUT_OF_AUTH_SEQ_;
			goto auth_fail;
		}
	}


	/*  Now, we are going to issue_auth... */
	pstat->auth_seq = seq + 1;

	issue_auth(padapter, pstat, (unsigned short)(_STATS_SUCCESSFUL_));

	if (pstat->state & WIFI_FW_AUTH_SUCCESS)
		pstat->auth_seq = 0;


	return _SUCCESS;

auth_fail:

	if (pstat)
		rtw_free_stainfo(padapter, pstat);

	pstat = &stat;
	memset((char *)pstat, '\0', sizeof(stat));
	pstat->auth_seq = 2;
	memcpy(pstat->hwaddr, sa, 6);

	issue_auth(padapter, pstat, (unsigned short)status);

	return _FAIL;

}

unsigned int OnAuthClient(struct adapter *padapter, union recv_frame *precv_frame)
{
	unsigned int	seq, len, status, algthm, offset;
	unsigned char *p;
	unsigned int	go2asoc = 0;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint pkt_len = precv_frame->u.hdr.len;

	DBG_871X("%s\n", __func__);

	/* check A1 matches or not */
	if (memcmp(myid(&(padapter->eeprompriv)), get_da(pframe), ETH_ALEN))
		return _SUCCESS;

	if (!(pmlmeinfo->state & WIFI_FW_AUTH_STATE))
		return _SUCCESS;

	offset = (GetPrivacy(pframe)) ? 4 : 0;

	algthm	= le16_to_cpu(*(__le16 *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset));
	seq	= le16_to_cpu(*(__le16 *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset + 2));
	status	= le16_to_cpu(*(__le16 *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset + 4));

	if (status != 0) {
		DBG_871X("clnt auth fail, status: %d\n", status);
		if (status == 13) { /*  pmlmeinfo->auth_algo == dot11AuthAlgrthm_Auto) */
			if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared)
				pmlmeinfo->auth_algo = dot11AuthAlgrthm_Open;
			else
				pmlmeinfo->auth_algo = dot11AuthAlgrthm_Shared;
			/* pmlmeinfo->reauth_count = 0; */
		}

		set_link_timer(pmlmeext, 1);
		goto authclnt_fail;
	}

	if (seq == 2) {
		if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared) {
			 /*  legendary shared system */
			p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _AUTH_IE_OFFSET_, _CHLGETXT_IE_, (int *)&len,
				pkt_len - WLAN_HDR_A3_LEN - _AUTH_IE_OFFSET_);

			if (p == NULL) {
				/* DBG_871X("marc: no challenge text?\n"); */
				goto authclnt_fail;
			}

			memcpy((void *)(pmlmeinfo->chg_txt), (void *)(p + 2), len);
			pmlmeinfo->auth_seq = 3;
			issue_auth(padapter, NULL, 0);
			set_link_timer(pmlmeext, REAUTH_TO);

			return _SUCCESS;
		} else{
			/*  open system */
			go2asoc = 1;
		}
	} else if (seq == 4) {
		if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared) {
			go2asoc = 1;
		} else{
			goto authclnt_fail;
		}
	} else{
		/*  this is also illegal */
		/* DBG_871X("marc: clnt auth failed due to illegal seq =%x\n", seq); */
		goto authclnt_fail;
	}

	if (go2asoc) {
		DBG_871X_LEVEL(_drv_always_, "auth success, start assoc\n");
		start_clnt_assoc(padapter);
		return _SUCCESS;
	}

authclnt_fail:

	/* pmlmeinfo->state &= ~(WIFI_FW_AUTH_STATE); */

	return _FAIL;

}

unsigned int OnAssocReq(struct adapter *padapter, union recv_frame *precv_frame)
{
	u16 capab_info, listen_interval;
	struct rtw_ieee802_11_elems elems;
	struct sta_info *pstat;
	unsigned char 	reassoc, *p, *pos, *wpa_ie;
	unsigned char WMM_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01};
	int		i, ie_len, wpa_ie_len, left;
	unsigned char 	supportRate[16];
	int					supportRateNum;
	unsigned short		status = _STATS_SUCCESSFUL_;
	unsigned short		frame_type, ie_offset = 0;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex	*cur = &(pmlmeinfo->network);
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint pkt_len = precv_frame->u.hdr.len;

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return _FAIL;

	frame_type = GetFrameSubType(pframe);
	if (frame_type == WIFI_ASSOCREQ) {
		reassoc = 0;
		ie_offset = _ASOCREQ_IE_OFFSET_;
	} else{ /*  WIFI_REASSOCREQ */
		reassoc = 1;
		ie_offset = _REASOCREQ_IE_OFFSET_;
	}


	if (pkt_len < sizeof(struct ieee80211_hdr_3addr) + ie_offset) {
		DBG_871X("handle_assoc(reassoc =%d) - too short payload (len =%lu)"
		       "\n", reassoc, (unsigned long)pkt_len);
		return _FAIL;
	}

	pstat = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
	if (pstat == NULL) {
		status = _RSON_CLS2_;
		goto asoc_class2_error;
	}

	capab_info = RTW_GET_LE16(pframe + WLAN_HDR_A3_LEN);
	/* capab_info = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN)); */
	/* listen_interval = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN+2)); */
	listen_interval = RTW_GET_LE16(pframe + WLAN_HDR_A3_LEN+2);

	left = pkt_len - (sizeof(struct ieee80211_hdr_3addr) + ie_offset);
	pos = pframe + (sizeof(struct ieee80211_hdr_3addr) + ie_offset);


	DBG_871X("%s\n", __func__);

	/*  check if this stat has been successfully authenticated/assocated */
	if (!((pstat->state) & WIFI_FW_AUTH_SUCCESS)) {
		if (!((pstat->state) & WIFI_FW_ASSOC_SUCCESS)) {
			status = _RSON_CLS2_;
			goto asoc_class2_error;
		} else{
			pstat->state &= (~WIFI_FW_ASSOC_SUCCESS);
			pstat->state |= WIFI_FW_ASSOC_STATE;
		}
	} else{
		pstat->state &= (~WIFI_FW_AUTH_SUCCESS);
		pstat->state |= WIFI_FW_ASSOC_STATE;
	}


	pstat->capability = capab_info;

	/* now parse all ieee802_11 ie to point to elems */
	if (rtw_ieee802_11_parse_elems(pos, left, &elems, 1) == ParseFailed ||
	    !elems.ssid) {
		DBG_871X("STA " MAC_FMT " sent invalid association request\n",
		       MAC_ARG(pstat->hwaddr));
		status = _STATS_FAILURE_;
		goto OnAssocReqFail;
	}


	/*  now we should check all the fields... */
	/*  checking SSID */
	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + ie_offset, _SSID_IE_, &ie_len,
		pkt_len - WLAN_HDR_A3_LEN - ie_offset);
	if (p == NULL) {
		status = _STATS_FAILURE_;
	}

	if (ie_len == 0) /*  broadcast ssid, however it is not allowed in assocreq */
		status = _STATS_FAILURE_;
	else {
		/*  check if ssid match */
		if (memcmp((void *)(p+2), cur->Ssid.Ssid, cur->Ssid.SsidLength))
			status = _STATS_FAILURE_;

		if (ie_len != cur->Ssid.SsidLength)
			status = _STATS_FAILURE_;
	}

	if (_STATS_SUCCESSFUL_ != status)
		goto OnAssocReqFail;

	/*  check if the supported rate is ok */
	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + ie_offset, _SUPPORTEDRATES_IE_, &ie_len, pkt_len - WLAN_HDR_A3_LEN - ie_offset);
	if (p == NULL) {
		DBG_871X("Rx a sta assoc-req which supported rate is empty!\n");
		/*  use our own rate set as statoin used */
		/* memcpy(supportRate, AP_BSSRATE, AP_BSSRATE_LEN); */
		/* supportRateNum = AP_BSSRATE_LEN; */

		status = _STATS_FAILURE_;
		goto OnAssocReqFail;
	} else {
		memcpy(supportRate, p+2, ie_len);
		supportRateNum = ie_len;

		p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + ie_offset, _EXT_SUPPORTEDRATES_IE_, &ie_len,
				pkt_len - WLAN_HDR_A3_LEN - ie_offset);
		if (p !=  NULL) {

			if (supportRateNum <= sizeof(supportRate)) {
				memcpy(supportRate+supportRateNum, p+2, ie_len);
				supportRateNum += ie_len;
			}
		}
	}

	/* todo: mask supportRate between AP & STA -> move to update raid */
	/* get_matched_rate(pmlmeext, supportRate, &supportRateNum, 0); */

	/* update station supportRate */
	pstat->bssratelen = supportRateNum;
	memcpy(pstat->bssrateset, supportRate, supportRateNum);
	UpdateBrateTblForSoftAP(pstat->bssrateset, pstat->bssratelen);

	/* check RSN/WPA/WPS */
	pstat->dot8021xalg = 0;
	pstat->wpa_psk = 0;
	pstat->wpa_group_cipher = 0;
	pstat->wpa2_group_cipher = 0;
	pstat->wpa_pairwise_cipher = 0;
	pstat->wpa2_pairwise_cipher = 0;
	memset(pstat->wpa_ie, 0, sizeof(pstat->wpa_ie));
	if ((psecuritypriv->wpa_psk & BIT(1)) && elems.rsn_ie) {

		int group_cipher = 0, pairwise_cipher = 0;

		wpa_ie = elems.rsn_ie;
		wpa_ie_len = elems.rsn_ie_len;

		if (rtw_parse_wpa2_ie(wpa_ie-2, wpa_ie_len+2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			pstat->dot8021xalg = 1;/* psk,  todo:802.1x */
			pstat->wpa_psk |= BIT(1);

			pstat->wpa2_group_cipher = group_cipher&psecuritypriv->wpa2_group_cipher;
			pstat->wpa2_pairwise_cipher = pairwise_cipher&psecuritypriv->wpa2_pairwise_cipher;

			if (!pstat->wpa2_group_cipher)
				status = WLAN_STATUS_GROUP_CIPHER_NOT_VALID;

			if (!pstat->wpa2_pairwise_cipher)
				status = WLAN_STATUS_PAIRWISE_CIPHER_NOT_VALID;
		} else{
			status = WLAN_STATUS_INVALID_IE;
		}

	} else if ((psecuritypriv->wpa_psk & BIT(0)) && elems.wpa_ie) {

		int group_cipher = 0, pairwise_cipher = 0;

		wpa_ie = elems.wpa_ie;
		wpa_ie_len = elems.wpa_ie_len;

		if (rtw_parse_wpa_ie(wpa_ie-2, wpa_ie_len+2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			pstat->dot8021xalg = 1;/* psk,  todo:802.1x */
			pstat->wpa_psk |= BIT(0);

			pstat->wpa_group_cipher = group_cipher&psecuritypriv->wpa_group_cipher;
			pstat->wpa_pairwise_cipher = pairwise_cipher&psecuritypriv->wpa_pairwise_cipher;

			if (!pstat->wpa_group_cipher)
				status = WLAN_STATUS_GROUP_CIPHER_NOT_VALID;

			if (!pstat->wpa_pairwise_cipher)
				status = WLAN_STATUS_PAIRWISE_CIPHER_NOT_VALID;

		} else{
			status = WLAN_STATUS_INVALID_IE;
		}

	} else {
		wpa_ie = NULL;
		wpa_ie_len = 0;
	}

	if (_STATS_SUCCESSFUL_ != status)
		goto OnAssocReqFail;

	pstat->flags &= ~(WLAN_STA_WPS | WLAN_STA_MAYBE_WPS);
	if (wpa_ie == NULL) {
		if (elems.wps_ie) {
			DBG_871X("STA included WPS IE in "
				   "(Re)Association Request - assume WPS is "
				   "used\n");
			pstat->flags |= WLAN_STA_WPS;
			/* wpabuf_free(sta->wps_ie); */
			/* sta->wps_ie = wpabuf_alloc_copy(elems.wps_ie + 4, */
			/* 				elems.wps_ie_len - 4); */
		} else {
			DBG_871X("STA did not include WPA/RSN IE "
				   "in (Re)Association Request - possible WPS "
				   "use\n");
			pstat->flags |= WLAN_STA_MAYBE_WPS;
		}


		/*  AP support WPA/RSN, and sta is going to do WPS, but AP is not ready */
		/*  that the selected registrar of AP is _FLASE */
		if ((psecuritypriv->wpa_psk > 0)
			&& (pstat->flags & (WLAN_STA_WPS|WLAN_STA_MAYBE_WPS))) {
			if (pmlmepriv->wps_beacon_ie) {
				u8 selected_registrar = 0;

				rtw_get_wps_attr_content(pmlmepriv->wps_beacon_ie, pmlmepriv->wps_beacon_ie_len, WPS_ATTR_SELECTED_REGISTRAR, &selected_registrar, NULL);

				if (!selected_registrar) {
					DBG_871X("selected_registrar is false , or AP is not ready to do WPS\n");

					status = _STATS_UNABLE_HANDLE_STA_;

					goto OnAssocReqFail;
				}
			}
		}

	} else{
		int copy_len;

		if (psecuritypriv->wpa_psk == 0) {
			DBG_871X("STA " MAC_FMT ": WPA/RSN IE in association "
			"request, but AP don't support WPA/RSN\n", MAC_ARG(pstat->hwaddr));

			status = WLAN_STATUS_INVALID_IE;

			goto OnAssocReqFail;

		}

		if (elems.wps_ie) {
			DBG_871X("STA included WPS IE in "
				   "(Re)Association Request - WPS is "
				   "used\n");
			pstat->flags |= WLAN_STA_WPS;
			copy_len = 0;
		} else{
			copy_len = ((wpa_ie_len+2) > sizeof(pstat->wpa_ie)) ? (sizeof(pstat->wpa_ie)):(wpa_ie_len+2);
		}


		if (copy_len > 0)
			memcpy(pstat->wpa_ie, wpa_ie-2, copy_len);

	}


	/*  check if there is WMM IE & support WWM-PS */
	pstat->flags &= ~WLAN_STA_WME;
	pstat->qos_option = 0;
	pstat->qos_info = 0;
	pstat->has_legacy_ac = true;
	pstat->uapsd_vo = 0;
	pstat->uapsd_vi = 0;
	pstat->uapsd_be = 0;
	pstat->uapsd_bk = 0;
	if (pmlmepriv->qospriv.qos_option) {
		p = pframe + WLAN_HDR_A3_LEN + ie_offset; ie_len = 0;
		for (;;) {
			p = rtw_get_ie(p, _VENDOR_SPECIFIC_IE_, &ie_len, pkt_len - WLAN_HDR_A3_LEN - ie_offset);
			if (p != NULL) {
				if (!memcmp(p+2, WMM_IE, 6)) {

					pstat->flags |= WLAN_STA_WME;

					pstat->qos_option = 1;
					pstat->qos_info = *(p+8);

					pstat->max_sp_len = (pstat->qos_info>>5)&0x3;

					if ((pstat->qos_info&0xf) != 0xf)
						pstat->has_legacy_ac = true;
					else
						pstat->has_legacy_ac = false;

					if (pstat->qos_info&0xf) {
						if (pstat->qos_info&BIT(0))
							pstat->uapsd_vo = BIT(0)|BIT(1);
						else
							pstat->uapsd_vo = 0;

						if (pstat->qos_info&BIT(1))
							pstat->uapsd_vi = BIT(0)|BIT(1);
						else
							pstat->uapsd_vi = 0;

						if (pstat->qos_info&BIT(2))
							pstat->uapsd_bk = BIT(0)|BIT(1);
						else
							pstat->uapsd_bk = 0;

						if (pstat->qos_info&BIT(3))
							pstat->uapsd_be = BIT(0)|BIT(1);
						else
							pstat->uapsd_be = 0;

					}

					break;
				}
			} else {
				break;
			}
			p = p + ie_len + 2;
		}
	}

	/* save HT capabilities in the sta object */
	memset(&pstat->htpriv.ht_cap, 0, sizeof(struct rtw_ieee80211_ht_cap));
	if (elems.ht_capabilities && elems.ht_capabilities_len >= sizeof(struct rtw_ieee80211_ht_cap)) {
		pstat->flags |= WLAN_STA_HT;

		pstat->flags |= WLAN_STA_WME;

		memcpy(&pstat->htpriv.ht_cap, elems.ht_capabilities, sizeof(struct rtw_ieee80211_ht_cap));

	} else
		pstat->flags &= ~WLAN_STA_HT;


	if ((pmlmepriv->htpriv.ht_option == false) && (pstat->flags&WLAN_STA_HT)) {
		status = _STATS_FAILURE_;
		goto OnAssocReqFail;
	}


	if ((pstat->flags & WLAN_STA_HT) &&
		    ((pstat->wpa2_pairwise_cipher&WPA_CIPHER_TKIP) ||
		      (pstat->wpa_pairwise_cipher&WPA_CIPHER_TKIP))) {
		DBG_871X("HT: " MAC_FMT " tried to "
				   "use TKIP with HT association\n", MAC_ARG(pstat->hwaddr));

		/* status = WLAN_STATUS_CIPHER_REJECTED_PER_POLICY; */
		/* goto OnAssocReqFail; */
	}
	pstat->flags |= WLAN_STA_NONERP;
	for (i = 0; i < pstat->bssratelen; i++) {
		if ((pstat->bssrateset[i] & 0x7f) > 22) {
			pstat->flags &= ~WLAN_STA_NONERP;
			break;
		}
	}

	if (pstat->capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
		pstat->flags |= WLAN_STA_SHORT_PREAMBLE;
	else
		pstat->flags &= ~WLAN_STA_SHORT_PREAMBLE;



	if (status != _STATS_SUCCESSFUL_)
		goto OnAssocReqFail;

	/* TODO: identify_proprietary_vendor_ie(); */
	/*  Realtek proprietary IE */
	/*  identify if this is Broadcom sta */
	/*  identify if this is ralink sta */
	/*  Customer proprietary IE */



	/* get a unique AID */
	if (pstat->aid > 0) {
		DBG_871X("  old AID %d\n", pstat->aid);
	} else {
		for (pstat->aid = 1; pstat->aid < NUM_STA; pstat->aid++)
			if (pstapriv->sta_aid[pstat->aid - 1] == NULL)
				break;

		/* if (pstat->aid > NUM_STA) { */
		if (pstat->aid > pstapriv->max_num_sta) {

			pstat->aid = 0;

			DBG_871X("  no room for more AIDs\n");

			status = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;

			goto OnAssocReqFail;


		} else {
			pstapriv->sta_aid[pstat->aid - 1] = pstat;
			DBG_871X("allocate new AID = (%d)\n", pstat->aid);
		}
	}


	pstat->state &= (~WIFI_FW_ASSOC_STATE);
	pstat->state |= WIFI_FW_ASSOC_SUCCESS;

	spin_lock_bh(&pstapriv->auth_list_lock);
	if (!list_empty(&pstat->auth_list)) {
		list_del_init(&pstat->auth_list);
		pstapriv->auth_list_cnt--;
	}
	spin_unlock_bh(&pstapriv->auth_list_lock);

	spin_lock_bh(&pstapriv->asoc_list_lock);
	if (list_empty(&pstat->asoc_list)) {
		pstat->expire_to = pstapriv->expire_to;
		list_add_tail(&pstat->asoc_list, &pstapriv->asoc_list);
		pstapriv->asoc_list_cnt++;
	}
	spin_unlock_bh(&pstapriv->asoc_list_lock);

	/*  now the station is qualified to join our BSS... */
	if (pstat && (pstat->state & WIFI_FW_ASSOC_SUCCESS) && (_STATS_SUCCESSFUL_ == status)) {
		/* 1 bss_cap_update & sta_info_update */
		bss_cap_update_on_sta_join(padapter, pstat);
		sta_info_update(padapter, pstat);

		/* 2 issue assoc rsp before notify station join event. */
		if (frame_type == WIFI_ASSOCREQ)
			issue_asocrsp(padapter, status, pstat, WIFI_ASSOCRSP);
		else
			issue_asocrsp(padapter, status, pstat, WIFI_REASSOCRSP);

		spin_lock_bh(&pstat->lock);
		if (pstat->passoc_req) {
			kfree(pstat->passoc_req);
			pstat->passoc_req = NULL;
			pstat->assoc_req_len = 0;
		}

		pstat->passoc_req =  rtw_zmalloc(pkt_len);
		if (pstat->passoc_req) {
			memcpy(pstat->passoc_req, pframe, pkt_len);
			pstat->assoc_req_len = pkt_len;
		}
		spin_unlock_bh(&pstat->lock);

		/* 3-(1) report sta add event */
		report_add_sta_event(padapter, pstat->hwaddr, pstat->aid);
	}

	return _SUCCESS;

asoc_class2_error:

	issue_deauth(padapter, (void *)GetAddr2Ptr(pframe), status);

	return _FAIL;

OnAssocReqFail:

	pstat->aid = 0;
	if (frame_type == WIFI_ASSOCREQ)
		issue_asocrsp(padapter, status, pstat, WIFI_ASSOCRSP);
	else
		issue_asocrsp(padapter, status, pstat, WIFI_REASSOCRSP);

	return _FAIL;
}

unsigned int OnAssocRsp(struct adapter *padapter, union recv_frame *precv_frame)
{
	uint i;
	int res;
	unsigned short	status;
	struct ndis_80211_var_ie *pIE;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	/* struct wlan_bssid_ex			*cur_network = &(pmlmeinfo->network); */
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint pkt_len = precv_frame->u.hdr.len;

	DBG_871X("%s\n", __func__);

	/* check A1 matches or not */
	if (memcmp(myid(&(padapter->eeprompriv)), get_da(pframe), ETH_ALEN))
		return _SUCCESS;

	if (!(pmlmeinfo->state & (WIFI_FW_AUTH_SUCCESS | WIFI_FW_ASSOC_STATE)))
		return _SUCCESS;

	if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
		return _SUCCESS;

	del_timer_sync(&pmlmeext->link_timer);

	/* status */
	status = le16_to_cpu(*(__le16 *)(pframe + WLAN_HDR_A3_LEN + 2));
	if (status > 0) {
		DBG_871X("assoc reject, status code: %d\n", status);
		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		res = -4;
		goto report_assoc_result;
	}

	/* get capabilities */
	pmlmeinfo->capability = le16_to_cpu(*(__le16 *)(pframe + WLAN_HDR_A3_LEN));

	/* set slot time */
	pmlmeinfo->slotTime = (pmlmeinfo->capability & BIT(10)) ? 9 : 20;

	/* AID */
	res = pmlmeinfo->aid = (int)(le16_to_cpu(*(__le16 *)(pframe + WLAN_HDR_A3_LEN + 4))&0x3fff);

	/* following are moved to join event callback function */
	/* to handle HT, WMM, rate adaptive, update MAC reg */
	/* for not to handle the synchronous IO in the tasklet */
	for (i = (6 + WLAN_HDR_A3_LEN); i < pkt_len;) {
		pIE = (struct ndis_80211_var_ie *)(pframe + i);

		switch (pIE->ElementID) {
		case _VENDOR_SPECIFIC_IE_:
			if (!memcmp(pIE->data, WMM_PARA_OUI, 6))	/* WMM */
				WMM_param_handler(padapter, pIE);
			break;

		case _HT_CAPABILITY_IE_:	/* HT caps */
			HT_caps_handler(padapter, pIE);
			break;

		case _HT_EXTRA_INFO_IE_:	/* HT info */
			HT_info_handler(padapter, pIE);
			break;

		case _ERPINFO_IE_:
			ERP_IE_handler(padapter, pIE);

		default:
			break;
		}

		i += (pIE->Length + 2);
	}

	pmlmeinfo->state &= (~WIFI_FW_ASSOC_STATE);
	pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;

	/* Update Basic Rate Table for spec, 2010-12-28 , by thomas */
	UpdateBrateTbl(padapter, pmlmeinfo->network.SupportedRates);

report_assoc_result:
	if (res > 0) {
		rtw_buf_update(&pmlmepriv->assoc_rsp, &pmlmepriv->assoc_rsp_len, pframe, pkt_len);
	} else {
		rtw_buf_free(&pmlmepriv->assoc_rsp, &pmlmepriv->assoc_rsp_len);
	}

	report_join_res(padapter, res);

	return _SUCCESS;
}

unsigned int OnDeAuth(struct adapter *padapter, union recv_frame *precv_frame)
{
	unsigned short	reason;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;

	/* check A3 */
	if (memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN))
		return _SUCCESS;

	reason = le16_to_cpu(*(__le16 *)(pframe + WLAN_HDR_A3_LEN));

	DBG_871X("%s Reason code(%d)\n", __func__, reason);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		/* spin_lock_bh(&(pstapriv->sta_hash_lock)); */
		/* rtw_free_stainfo(padapter, psta); */
		/* spin_unlock_bh(&(pstapriv->sta_hash_lock)); */

		DBG_871X_LEVEL(_drv_always_, "ap recv deauth reason code(%d) sta:%pM\n",
				reason, GetAddr2Ptr(pframe));

		psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
		if (psta) {
			u8 updated = false;

			spin_lock_bh(&pstapriv->asoc_list_lock);
			if (list_empty(&psta->asoc_list) == false) {
				list_del_init(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;
				updated = ap_free_sta(padapter, psta, false, reason);

			}
			spin_unlock_bh(&pstapriv->asoc_list_lock);

			associated_clients_update(padapter, updated);
		}


		return _SUCCESS;
	} else{
		int	ignore_received_deauth = 0;

		/* 	Commented by Albert 20130604 */
		/* 	Before sending the auth frame to start the STA/GC mode connection with AP/GO, */
		/* 	we will send the deauth first. */
		/* 	However, the Win8.1 with BRCM Wi-Fi will send the deauth with reason code 6 to us after receieving our deauth. */
		/* 	Added the following code to avoid this case. */
		if ((pmlmeinfo->state & WIFI_FW_AUTH_STATE) ||
			(pmlmeinfo->state & WIFI_FW_ASSOC_STATE)) {
			if (reason == WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA) {
				ignore_received_deauth = 1;
			} else if (WLAN_REASON_PREV_AUTH_NOT_VALID == reason) {
				/*  TODO: 802.11r */
				ignore_received_deauth = 1;
			}
		}

		DBG_871X_LEVEL(_drv_always_, "sta recv deauth reason code(%d) sta:%pM, ignore = %d\n",
				reason, GetAddr3Ptr(pframe), ignore_received_deauth);

		if (0 == ignore_received_deauth) {
			receive_disconnect(padapter, GetAddr3Ptr(pframe), reason);
		}
	}
	pmlmepriv->LinkDetectInfo.bBusyTraffic = false;
	return _SUCCESS;

}

unsigned int OnDisassoc(struct adapter *padapter, union recv_frame *precv_frame)
{
	unsigned short	reason;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;

	/* check A3 */
	if (memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN))
		return _SUCCESS;

	reason = le16_to_cpu(*(__le16 *)(pframe + WLAN_HDR_A3_LEN));

	DBG_871X("%s Reason code(%d)\n", __func__, reason);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		/* spin_lock_bh(&(pstapriv->sta_hash_lock)); */
		/* rtw_free_stainfo(padapter, psta); */
		/* spin_unlock_bh(&(pstapriv->sta_hash_lock)); */

		DBG_871X_LEVEL(_drv_always_, "ap recv disassoc reason code(%d) sta:%pM\n",
				reason, GetAddr2Ptr(pframe));

		psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
		if (psta) {
			u8 updated = false;

			spin_lock_bh(&pstapriv->asoc_list_lock);
			if (list_empty(&psta->asoc_list) == false) {
				list_del_init(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;
				updated = ap_free_sta(padapter, psta, false, reason);

			}
			spin_unlock_bh(&pstapriv->asoc_list_lock);

			associated_clients_update(padapter, updated);
		}

		return _SUCCESS;
	} else{
		DBG_871X_LEVEL(_drv_always_, "sta recv disassoc reason code(%d) sta:%pM\n",
				reason, GetAddr3Ptr(pframe));

		receive_disconnect(padapter, GetAddr3Ptr(pframe), reason);
	}
	pmlmepriv->LinkDetectInfo.bBusyTraffic = false;
	return _SUCCESS;

}

unsigned int OnAtim(struct adapter *padapter, union recv_frame *precv_frame)
{
	DBG_871X("%s\n", __func__);
	return _SUCCESS;
}

unsigned int on_action_spct(struct adapter *padapter, union recv_frame *precv_frame)
{
	unsigned int ret = _FAIL;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	u8 *frame_body = (u8 *)(pframe + sizeof(struct ieee80211_hdr_3addr));
	u8 category;
	u8 action;

	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(padapter->pnetdev));

	psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));

	if (!psta)
		goto exit;

	category = frame_body[0];
	if (category != RTW_WLAN_CATEGORY_SPECTRUM_MGMT)
		goto exit;

	action = frame_body[1];
	switch (action) {
	case RTW_WLAN_ACTION_SPCT_MSR_REQ:
	case RTW_WLAN_ACTION_SPCT_MSR_RPRT:
	case RTW_WLAN_ACTION_SPCT_TPC_REQ:
	case RTW_WLAN_ACTION_SPCT_TPC_RPRT:
	case RTW_WLAN_ACTION_SPCT_CHL_SWITCH:
		break;
	default:
		break;
	}

exit:
	return ret;
}

unsigned int OnAction_back(struct adapter *padapter, union recv_frame *precv_frame)
{
	u8 *addr;
	struct sta_info *psta = NULL;
	struct recv_reorder_ctrl *preorder_ctrl;
	unsigned char 	*frame_body;
	unsigned char 	category, action;
	unsigned short	tid, status, reason_code = 0;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_871X("%s\n", __func__);

	/* check RA matches or not */
	if (memcmp(myid(&(padapter->eeprompriv)), GetAddr1Ptr(pframe), ETH_ALEN))/* for if1, sta/ap mode */
		return _SUCCESS;

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		if (!(pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS))
			return _SUCCESS;

	addr = GetAddr2Ptr(pframe);
	psta = rtw_get_stainfo(pstapriv, addr);

	if (psta == NULL)
		return _SUCCESS;

	frame_body = (unsigned char *)(pframe + sizeof(struct ieee80211_hdr_3addr));

	category = frame_body[0];
	if (category == RTW_WLAN_CATEGORY_BACK) {/*  representing Block Ack */
		if (!pmlmeinfo->HT_enable) {
			return _SUCCESS;
		}

		action = frame_body[1];
		DBG_871X("%s, action =%d\n", __func__, action);
		switch (action) {
		case RTW_WLAN_ACTION_ADDBA_REQ: /* ADDBA request */

			memcpy(&(pmlmeinfo->ADDBA_req), &(frame_body[2]), sizeof(struct ADDBA_request));
			/* process_addba_req(padapter, (u8 *)&(pmlmeinfo->ADDBA_req), GetAddr3Ptr(pframe)); */
			process_addba_req(padapter, (u8 *)&(pmlmeinfo->ADDBA_req), addr);

			if (pmlmeinfo->accept_addba_req) {
				issue_action_BA(padapter, addr, RTW_WLAN_ACTION_ADDBA_RESP, 0);
			} else{
				issue_action_BA(padapter, addr, RTW_WLAN_ACTION_ADDBA_RESP, 37);/* reject ADDBA Req */
			}

			break;

		case RTW_WLAN_ACTION_ADDBA_RESP: /* ADDBA response */
			status = RTW_GET_LE16(&frame_body[3]);
			tid = ((frame_body[5] >> 2) & 0x7);

			if (status == 0) {
				/* successful */
				DBG_871X("agg_enable for TID =%d\n", tid);
				psta->htpriv.agg_enable_bitmap |= 1 << tid;
				psta->htpriv.candidate_tid_bitmap &= ~BIT(tid);
			} else{
				psta->htpriv.agg_enable_bitmap &= ~BIT(tid);
			}

			if (psta->state & WIFI_STA_ALIVE_CHK_STATE) {
				DBG_871X("%s alive check - rx ADDBA response\n", __func__);
				psta->htpriv.agg_enable_bitmap &= ~BIT(tid);
				psta->expire_to = pstapriv->expire_to;
				psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
			}

			/* DBG_871X("marc: ADDBA RSP: %x\n", pmlmeinfo->agg_enable_bitmap); */
			break;

		case RTW_WLAN_ACTION_DELBA: /* DELBA */
			if ((frame_body[3] & BIT(3)) == 0) {
				psta->htpriv.agg_enable_bitmap &= ~(1 << ((frame_body[3] >> 4) & 0xf));
				psta->htpriv.candidate_tid_bitmap &= ~(1 << ((frame_body[3] >> 4) & 0xf));

				/* reason_code = frame_body[4] | (frame_body[5] << 8); */
				reason_code = RTW_GET_LE16(&frame_body[4]);
			} else if ((frame_body[3] & BIT(3)) == BIT(3)) {
				tid = (frame_body[3] >> 4) & 0x0F;

				preorder_ctrl =  &psta->recvreorder_ctrl[tid];
				preorder_ctrl->enable = false;
				preorder_ctrl->indicate_seq = 0xffff;
				#ifdef DBG_RX_SEQ
				DBG_871X("DBG_RX_SEQ %s:%d indicate_seq:%u\n", __func__, __LINE__,
					preorder_ctrl->indicate_seq);
				#endif
			}

			DBG_871X("%s(): DELBA: %x(%x)\n", __func__, pmlmeinfo->agg_enable_bitmap, reason_code);
			/* todo: how to notify the host while receiving DELETE BA */
			break;

		default:
			break;
		}
	}
	return _SUCCESS;
}

static s32 rtw_action_public_decache(union recv_frame *recv_frame, s32 token)
{
	struct adapter *adapter = recv_frame->u.hdr.adapter;
	struct mlme_ext_priv *mlmeext = &(adapter->mlmeextpriv);
	u8 *frame = recv_frame->u.hdr.rx_data;
	u16 seq_ctrl = ((recv_frame->u.hdr.attrib.seq_num&0xffff) << 4) |
		(recv_frame->u.hdr.attrib.frag_num & 0xf);

	if (GetRetry(frame)) {
		if (token >= 0) {
			if ((seq_ctrl == mlmeext->action_public_rxseq)
				&& (token == mlmeext->action_public_dialog_token)) {
				DBG_871X(FUNC_ADPT_FMT" seq_ctrl = 0x%x, rxseq = 0x%x, token:%d\n",
					FUNC_ADPT_ARG(adapter), seq_ctrl, mlmeext->action_public_rxseq, token);
				return _FAIL;
			}
		} else {
			if (seq_ctrl == mlmeext->action_public_rxseq) {
				DBG_871X(FUNC_ADPT_FMT" seq_ctrl = 0x%x, rxseq = 0x%x\n",
					FUNC_ADPT_ARG(adapter), seq_ctrl, mlmeext->action_public_rxseq);
				return _FAIL;
			}
		}
	}

	mlmeext->action_public_rxseq = seq_ctrl;

	if (token >= 0)
		mlmeext->action_public_dialog_token = token;

	return _SUCCESS;
}

static unsigned int on_action_public_p2p(union recv_frame *precv_frame)
{
	u8 *pframe = precv_frame->u.hdr.rx_data;
	u8 *frame_body;
	u8 dialogToken = 0;

	frame_body = (unsigned char *)(pframe + sizeof(struct ieee80211_hdr_3addr));

	dialogToken = frame_body[7];

	if (rtw_action_public_decache(precv_frame, dialogToken) == _FAIL)
		return _FAIL;

	return _SUCCESS;
}

static unsigned int on_action_public_vendor(union recv_frame *precv_frame)
{
	unsigned int ret = _FAIL;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	u8 *frame_body = pframe + sizeof(struct ieee80211_hdr_3addr);

	if (!memcmp(frame_body + 2, P2P_OUI, 4)) {
		ret = on_action_public_p2p(precv_frame);
	}

	return ret;
}

static unsigned int on_action_public_default(union recv_frame *precv_frame, u8 action)
{
	unsigned int ret = _FAIL;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint frame_len = precv_frame->u.hdr.len;
	u8 *frame_body = pframe + sizeof(struct ieee80211_hdr_3addr);
	u8 token;
	struct adapter *adapter = precv_frame->u.hdr.adapter;
	int cnt = 0;
	char msg[64];

	token = frame_body[2];

	if (rtw_action_public_decache(precv_frame, token) == _FAIL)
		goto exit;

	cnt += sprintf((msg+cnt), "%s(token:%u)", action_public_str(action), token);
	rtw_cfg80211_rx_action(adapter, pframe, frame_len, msg);

	ret = _SUCCESS;

exit:
	return ret;
}

unsigned int on_action_public(struct adapter *padapter, union recv_frame *precv_frame)
{
	unsigned int ret = _FAIL;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	u8 *frame_body = pframe + sizeof(struct ieee80211_hdr_3addr);
	u8 category, action;

	/* check RA matches or not */
	if (memcmp(myid(&(padapter->eeprompriv)), GetAddr1Ptr(pframe), ETH_ALEN))
		goto exit;

	category = frame_body[0];
	if (category != RTW_WLAN_CATEGORY_PUBLIC)
		goto exit;

	action = frame_body[1];
	switch (action) {
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

unsigned int OnAction_ht(struct adapter *padapter, union recv_frame *precv_frame)
{
	u8 *pframe = precv_frame->u.hdr.rx_data;
	u8 *frame_body = pframe + sizeof(struct ieee80211_hdr_3addr);
	u8 category, action;

	/* check RA matches or not */
	if (memcmp(myid(&(padapter->eeprompriv)), GetAddr1Ptr(pframe), ETH_ALEN))
		goto exit;

	category = frame_body[0];
	if (category != RTW_WLAN_CATEGORY_HT)
		goto exit;

	action = frame_body[1];
	switch (action) {
	case RTW_WLAN_ACTION_HT_COMPRESS_BEAMFORMING:
		break;
	default:
		break;
	}

exit:

	return _SUCCESS;
}

unsigned int OnAction_sa_query(struct adapter *padapter, union recv_frame *precv_frame)
{
	u8 *pframe = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	unsigned short tid;
	/* Baron */

	DBG_871X("OnAction_sa_query\n");

	switch (pframe[WLAN_HDR_A3_LEN+1]) {
	case 0: /* SA Query req */
		memcpy(&tid, &pframe[WLAN_HDR_A3_LEN+2], sizeof(unsigned short));
		DBG_871X("OnAction_sa_query request, action =%d, tid =%04x\n", pframe[WLAN_HDR_A3_LEN+1], tid);
		issue_action_SA_Query(padapter, GetAddr2Ptr(pframe), 1, tid);
		break;

	case 1: /* SA Query rsp */
		del_timer_sync(&pmlmeext->sa_query_timer);
		DBG_871X("OnAction_sa_query response, action =%d, tid =%04x, cancel timer\n", pframe[WLAN_HDR_A3_LEN+1], pframe[WLAN_HDR_A3_LEN+2]);
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

unsigned int OnAction(struct adapter *padapter, union recv_frame *precv_frame)
{
	int i;
	unsigned char category;
	struct action_handler *ptable;
	unsigned char *frame_body;
	u8 *pframe = precv_frame->u.hdr.rx_data;

	frame_body = (unsigned char *)(pframe + sizeof(struct ieee80211_hdr_3addr));

	category = frame_body[0];

	for (i = 0; i < ARRAY_SIZE(OnAction_tbl); i++) {
		ptable = &OnAction_tbl[i];

		if (category == ptable->num)
			ptable->func(padapter, precv_frame);

	}

	return _SUCCESS;

}

unsigned int DoReserved(struct adapter *padapter, union recv_frame *precv_frame)
{

	/* DBG_871X("rcvd mgt frame(%x, %x)\n", (GetFrameSubType(pframe) >> 4), *(unsigned int *)GetAddr1Ptr(pframe)); */
	return _SUCCESS;
}

static struct xmit_frame *_alloc_mgtxmitframe(struct xmit_priv *pxmitpriv, bool once)
{
	struct xmit_frame *pmgntframe;
	struct xmit_buf *pxmitbuf;

	if (once)
		pmgntframe = rtw_alloc_xmitframe_once(pxmitpriv);
	else
		pmgntframe = rtw_alloc_xmitframe_ext(pxmitpriv);

	if (pmgntframe == NULL) {
		DBG_871X(FUNC_ADPT_FMT" alloc xmitframe fail, once:%d\n", FUNC_ADPT_ARG(pxmitpriv->adapter), once);
		goto exit;
	}

	pxmitbuf = rtw_alloc_xmitbuf_ext(pxmitpriv);
	if (pxmitbuf == NULL) {
		DBG_871X(FUNC_ADPT_FMT" alloc xmitbuf fail\n", FUNC_ADPT_ARG(pxmitpriv->adapter));
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
	return _alloc_mgtxmitframe(pxmitpriv, false);
}

/****************************************************************************

Following are some TX fuctions for WiFi MLME

*****************************************************************************/

void update_mgnt_tx_rate(struct adapter *padapter, u8 rate)
{
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);

	pmlmeext->tx_rate = rate;
	/* DBG_871X("%s(): rate = %x\n", __func__, rate); */
}

void update_mgntframe_attrib(struct adapter *padapter, struct pkt_attrib *pattrib)
{
	u8 wireless_mode;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);

	/* memset((u8 *)(pattrib), 0, sizeof(struct pkt_attrib)); */

	pattrib->hdrlen = 24;
	pattrib->nr_frags = 1;
	pattrib->priority = 7;
	pattrib->mac_id = 0;
	pattrib->qsel = 0x12;

	pattrib->pktlen = 0;

	if (pmlmeext->tx_rate == IEEE80211_CCK_RATE_1MB)
		wireless_mode = WIRELESS_11B;
	else
		wireless_mode = WIRELESS_11G;
	pattrib->raid =  rtw_get_mgntframe_raid(padapter, wireless_mode);
	pattrib->rate = pmlmeext->tx_rate;

	pattrib->encrypt = _NO_PRIVACY_;
	pattrib->bswenc = false;

	pattrib->qos_en = false;
	pattrib->ht_en = false;
	pattrib->bwmode = CHANNEL_WIDTH_20;
	pattrib->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	pattrib->sgi = false;

	pattrib->seqnum = pmlmeext->mgnt_seq;

	pattrib->retry_ctrl = true;

	pattrib->mbssid = 0;

}

void update_mgntframe_attrib_addr(struct adapter *padapter, struct xmit_frame *pmgntframe)
{
	u8 *pframe;
	struct pkt_attrib	*pattrib = &pmgntframe->attrib;

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	memcpy(pattrib->ra, GetAddr1Ptr(pframe), ETH_ALEN);
	memcpy(pattrib->ta, GetAddr2Ptr(pframe), ETH_ALEN);
}

void dump_mgntframe(struct adapter *padapter, struct xmit_frame *pmgntframe)
{
	if (padapter->bSurpriseRemoved ||
		padapter->bDriverStopped) {
		rtw_free_xmitbuf(&padapter->xmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(&padapter->xmitpriv, pmgntframe);
		return;
	}

	rtw_hal_mgnt_xmit(padapter, pmgntframe);
}

s32 dump_mgntframe_and_wait(struct adapter *padapter, struct xmit_frame *pmgntframe, int timeout_ms)
{
	s32 ret = _FAIL;
	_irqL irqL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct xmit_buf *pxmitbuf = pmgntframe->pxmitbuf;
	struct submit_ctx sctx;

	if (padapter->bSurpriseRemoved ||
		padapter->bDriverStopped) {
		rtw_free_xmitbuf(&padapter->xmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(&padapter->xmitpriv, pmgntframe);
		return ret;
	}

	rtw_sctx_init(&sctx, timeout_ms);
	pxmitbuf->sctx = &sctx;

	ret = rtw_hal_mgnt_xmit(padapter, pmgntframe);

	if (ret == _SUCCESS)
		ret = rtw_sctx_wait(&sctx, __func__);

	spin_lock_irqsave(&pxmitpriv->lock_sctx, irqL);
	pxmitbuf->sctx = NULL;
	spin_unlock_irqrestore(&pxmitpriv->lock_sctx, irqL);

	return ret;
}

s32 dump_mgntframe_and_wait_ack(struct adapter *padapter, struct xmit_frame *pmgntframe)
{
	static u8 seq_no;
	s32 ret = _FAIL;
	u32 timeout_ms = 500;/*   500ms */
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	if (padapter->bSurpriseRemoved ||
		padapter->bDriverStopped) {
		rtw_free_xmitbuf(&padapter->xmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(&padapter->xmitpriv, pmgntframe);
		return -1;
	}

	if (mutex_lock_interruptible(&pxmitpriv->ack_tx_mutex) == 0) {
		pxmitpriv->ack_tx = true;
		pxmitpriv->seq_no = seq_no++;
		pmgntframe->ack_report = 1;
		if (rtw_hal_mgnt_xmit(padapter, pmgntframe) == _SUCCESS) {
			ret = rtw_ack_tx_wait(pxmitpriv, timeout_ms);
		}

		pxmitpriv->ack_tx = false;
		mutex_unlock(&pxmitpriv->ack_tx_mutex);
	}

	return ret;
}

static int update_hidden_ssid(u8 *ies, u32 ies_len, u8 hidden_ssid_mode)
{
	u8 *ssid_ie;
	sint ssid_len_ori;
	int len_diff = 0;

	ssid_ie = rtw_get_ie(ies,  WLAN_EID_SSID, &ssid_len_ori, ies_len);

	/* DBG_871X("%s hidden_ssid_mode:%u, ssid_ie:%p, ssid_len_ori:%d\n", __func__, hidden_ssid_mode, ssid_ie, ssid_len_ori); */

	if (ssid_ie && ssid_len_ori > 0) {
		switch (hidden_ssid_mode) {
		case 1:
		{
			u8 *next_ie = ssid_ie + 2 + ssid_len_ori;
			u32 remain_len = 0;

			remain_len = ies_len - (next_ie-ies);

			ssid_ie[1] = 0;
			memcpy(ssid_ie+2, next_ie, remain_len);
			len_diff -= ssid_len_ori;

			break;
		}
		case 2:
			memset(&ssid_ie[2], 0, ssid_len_ori);
			break;
		default:
			break;
	}
	}

	return len_diff;
}

void issue_beacon(struct adapter *padapter, int timeout_ms)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	unsigned int	rate_len;
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*cur_network = &(pmlmeinfo->network);
	u8 bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	/* DBG_871X("%s\n", __func__); */

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL) {
		DBG_871X("%s, alloc mgnt frame fail\n", __func__);
		return;
	}

	spin_lock_bh(&pmlmepriv->bcn_update_lock);

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->qsel = 0x10;

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;


	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;

	memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	memcpy(pwlanhdr->addr3, get_my_bssid(cur_network), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	/* pmlmeext->mgnt_seq++; */
	SetFrameSubType(pframe, WIFI_BEACON);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE) {
		/* DBG_871X("ie len =%d\n", cur_network->IELength); */
		{
			int len_diff;
			memcpy(pframe, cur_network->IEs, cur_network->IELength);
			len_diff = update_hidden_ssid(
				pframe+_BEACON_IE_OFFSET_
				, cur_network->IELength-_BEACON_IE_OFFSET_
				, pmlmeinfo->hidden_ssid_mode
			);
			pframe += (cur_network->IELength+len_diff);
			pattrib->pktlen += (cur_network->IELength+len_diff);
		}

		{
			u8 *wps_ie;
			uint wps_ielen;
			u8 sr = 0;
			wps_ie = rtw_get_wps_ie(pmgntframe->buf_addr+TXDESC_OFFSET+sizeof(struct ieee80211_hdr_3addr)+_BEACON_IE_OFFSET_,
				pattrib->pktlen-sizeof(struct ieee80211_hdr_3addr)-_BEACON_IE_OFFSET_, NULL, &wps_ielen);
			if (wps_ie && wps_ielen > 0) {
				rtw_get_wps_attr_content(wps_ie,  wps_ielen, WPS_ATTR_SELECTED_REGISTRAR, (u8 *)(&sr), NULL);
			}
			if (sr != 0)
				set_fwstate(pmlmepriv, WIFI_UNDER_WPS);
			else
				_clr_fwstate_(pmlmepriv, WIFI_UNDER_WPS);
		}

		goto _issue_bcn;

	}

	/* below for ad-hoc mode */

	/* timestamp will be inserted by hardware */
	pframe += 8;
	pattrib->pktlen += 8;

	/*  beacon interval: 2 bytes */

	memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pattrib->pktlen += 2;

	/*  capability info: 2 bytes */

	memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pattrib->pktlen += 2;

	/*  SSID */
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pattrib->pktlen);

	/*  supported rates... */
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8) ? 8 : rate_len), cur_network->SupportedRates, &pattrib->pktlen);

	/*  DS parameter set */
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pattrib->pktlen);

	/* if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) */
	{
		u8 erpinfo = 0;
		u32 ATIMWindow;
		/*  IBSS Parameter Set... */
		/* ATIMWindow = cur->Configuration.ATIMWindow; */
		ATIMWindow = 0;
		pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pattrib->pktlen);

		/* ERP IE */
		pframe = rtw_set_ie(pframe, _ERPINFO_IE_, 1, &erpinfo, &pattrib->pktlen);
	}


	/*  EXTERNDED SUPPORTED RATE */
	if (rate_len > 8) {
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pattrib->pktlen);
	}


	/* todo:HT for adhoc */

_issue_bcn:

	pmlmepriv->update_bcn = false;

	spin_unlock_bh(&pmlmepriv->bcn_update_lock);

	if ((pattrib->pktlen + TXDESC_SIZE) > 512) {
		DBG_871X("beacon frame too large\n");
		return;
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	/* DBG_871X("issue bcn_sz =%d\n", pattrib->last_txcmdsz); */
	if (timeout_ms > 0)
		dump_mgntframe_and_wait(padapter, pmgntframe, timeout_ms);
	else
		dump_mgntframe(padapter, pmgntframe);

}

void issue_probersp(struct adapter *padapter, unsigned char *da, u8 is_valid_p2p_probereq)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char 				*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	__le16 *fctrl;
	unsigned char 				*mac, *bssid;
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);

	u8 *pwps_ie;
	uint wps_ielen;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*cur_network = &(pmlmeinfo->network);
	unsigned int	rate_len;

	/* DBG_871X("%s\n", __func__); */

	if (da == NULL)
		return;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL) {
		DBG_871X("%s, alloc mgnt frame fail\n", __func__);
		return;
	}


	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	mac = myid(&(padapter->eeprompriv));
	bssid = cur_network->MacAddress;

	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;
	memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	memcpy(pwlanhdr->addr2, mac, ETH_ALEN);
	memcpy(pwlanhdr->addr3, bssid, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(fctrl, WIFI_PROBERSP);

	pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = pattrib->hdrlen;
	pframe += pattrib->hdrlen;


	if (cur_network->IELength > MAX_IE_SZ)
		return;

	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE) {
		pwps_ie = rtw_get_wps_ie(cur_network->IEs+_FIXED_IE_LENGTH_, cur_network->IELength-_FIXED_IE_LENGTH_, NULL, &wps_ielen);

		/* inerset & update wps_probe_resp_ie */
		if ((pmlmepriv->wps_probe_resp_ie != NULL) && pwps_ie && (wps_ielen > 0)) {
			uint wps_offset, remainder_ielen;
			u8 *premainder_ie;

			wps_offset = (uint)(pwps_ie - cur_network->IEs);

			premainder_ie = pwps_ie + wps_ielen;

			remainder_ielen = cur_network->IELength - wps_offset - wps_ielen;

			memcpy(pframe, cur_network->IEs, wps_offset);
			pframe += wps_offset;
			pattrib->pktlen += wps_offset;

			wps_ielen = (uint)pmlmepriv->wps_probe_resp_ie[1];/* to get ie data len */
			if ((wps_offset+wps_ielen+2) <= MAX_IE_SZ) {
				memcpy(pframe, pmlmepriv->wps_probe_resp_ie, wps_ielen+2);
				pframe += wps_ielen+2;
				pattrib->pktlen += wps_ielen+2;
			}

			if ((wps_offset+wps_ielen+2+remainder_ielen) <= MAX_IE_SZ) {
				memcpy(pframe, premainder_ie, remainder_ielen);
				pframe += remainder_ielen;
				pattrib->pktlen += remainder_ielen;
			}
		} else{
			memcpy(pframe, cur_network->IEs, cur_network->IELength);
			pframe += cur_network->IELength;
			pattrib->pktlen += cur_network->IELength;
		}

		/* retrieve SSID IE from cur_network->Ssid */
		{
			u8 *ssid_ie;
			sint ssid_ielen;
			sint ssid_ielen_diff;
			u8 buf[MAX_IE_SZ];
			u8 *ies = pmgntframe->buf_addr+TXDESC_OFFSET+sizeof(struct ieee80211_hdr_3addr);

			ssid_ie = rtw_get_ie(ies+_FIXED_IE_LENGTH_, _SSID_IE_, &ssid_ielen,
				(pframe-ies)-_FIXED_IE_LENGTH_);

			ssid_ielen_diff = cur_network->Ssid.SsidLength - ssid_ielen;

			if (ssid_ie &&  cur_network->Ssid.SsidLength) {
				uint remainder_ielen;
				u8 *remainder_ie;
				remainder_ie = ssid_ie+2;
				remainder_ielen = (pframe-remainder_ie);

				if (remainder_ielen > MAX_IE_SZ) {
					DBG_871X_LEVEL(_drv_warning_, FUNC_ADPT_FMT" remainder_ielen > MAX_IE_SZ\n", FUNC_ADPT_ARG(padapter));
					remainder_ielen = MAX_IE_SZ;
				}

				memcpy(buf, remainder_ie, remainder_ielen);
				memcpy(remainder_ie+ssid_ielen_diff, buf, remainder_ielen);
				*(ssid_ie+1) = cur_network->Ssid.SsidLength;
				memcpy(ssid_ie+2, cur_network->Ssid.Ssid, cur_network->Ssid.SsidLength);

				pframe += ssid_ielen_diff;
				pattrib->pktlen += ssid_ielen_diff;
			}
		}
	} else{
		/* timestamp will be inserted by hardware */
		pframe += 8;
		pattrib->pktlen += 8;

		/*  beacon interval: 2 bytes */

		memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2);

		pframe += 2;
		pattrib->pktlen += 2;

		/*  capability info: 2 bytes */

		memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

		pframe += 2;
		pattrib->pktlen += 2;

		/* below for ad-hoc mode */

		/*  SSID */
		pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pattrib->pktlen);

		/*  supported rates... */
		rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8) ? 8 : rate_len), cur_network->SupportedRates, &pattrib->pktlen);

		/*  DS parameter set */
		pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pattrib->pktlen);

		if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) {
			u8 erpinfo = 0;
			u32 ATIMWindow;
			/*  IBSS Parameter Set... */
			/* ATIMWindow = cur->Configuration.ATIMWindow; */
			ATIMWindow = 0;
			pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pattrib->pktlen);

			/* ERP IE */
			pframe = rtw_set_ie(pframe, _ERPINFO_IE_, 1, &erpinfo, &pattrib->pktlen);
		}


		/*  EXTERNDED SUPPORTED RATE */
		if (rate_len > 8) {
			pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pattrib->pktlen);
		}


		/* todo:HT for adhoc */

	}

#ifdef CONFIG_AUTO_AP_MODE
{
	struct sta_info *psta;
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_871X("(%s)\n", __func__);

	/* check rc station */
	psta = rtw_get_stainfo(pstapriv, da);
	if (psta && psta->isrc && psta->pid > 0) {
		u8 RC_OUI[4] = {0x00, 0xE0, 0x4C, 0x0A};
		u8 RC_INFO[14] = {0};
		/* EID[1] + EID_LEN[1] + RC_OUI[4] + MAC[6] + PairingID[2] + ChannelNum[2] */
		u16 cu_ch = (u16)cur_network->Configuration.DSConfig;

		DBG_871X("%s, reply rc(pid = 0x%x) device "MAC_FMT" in ch =%d\n", __func__,
			psta->pid, MAC_ARG(psta->hwaddr), cu_ch);

		/* append vendor specific ie */
		memcpy(RC_INFO, RC_OUI, sizeof(RC_OUI));
		memcpy(&RC_INFO[4], mac, ETH_ALEN);
		memcpy(&RC_INFO[10], (u8 *)&psta->pid, 2);
		memcpy(&RC_INFO[12], (u8 *)&cu_ch, 2);

		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, sizeof(RC_INFO), RC_INFO, &pattrib->pktlen);
	}
}
#endif /* CONFIG_AUTO_AP_MODE */


	pattrib->last_txcmdsz = pattrib->pktlen;


	dump_mgntframe(padapter, pmgntframe);

	return;

}

static int _issue_probereq(struct adapter *padapter,
			   struct ndis_802_11_ssid *pssid,
			   u8 *da, u8 ch, bool append_wps, bool wait_ack)
{
	int ret = _FAIL;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	unsigned char 		*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	__le16 *fctrl;
	unsigned char 		*mac;
	unsigned char 		bssrate[NumRates];
	struct xmit_priv 	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	int	bssrate_len = 0;
	u8 bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+issue_probereq\n"));

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);


	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	mac = myid(&(padapter->eeprompriv));

	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;

	if (da) {
		/* 	unicast probe request frame */
		memcpy(pwlanhdr->addr1, da, ETH_ALEN);
		memcpy(pwlanhdr->addr3, da, ETH_ALEN);
	} else{
		/* 	broadcast probe request frame */
		memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
		memcpy(pwlanhdr->addr3, bc_addr, ETH_ALEN);
	}

	memcpy(pwlanhdr->addr2, mac, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_PROBEREQ);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	if (pssid)
		pframe = rtw_set_ie(pframe, _SSID_IE_, pssid->SsidLength, pssid->Ssid, &(pattrib->pktlen));
	else
		pframe = rtw_set_ie(pframe, _SSID_IE_, 0, NULL, &(pattrib->pktlen));

	get_rate_set(padapter, bssrate, &bssrate_len);

	if (bssrate_len > 8) {
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, bssrate, &(pattrib->pktlen));
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	} else{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, bssrate_len, bssrate, &(pattrib->pktlen));
	}

	if (ch)
		pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, &ch, &pattrib->pktlen);

	if (append_wps) {
		/* add wps_ie for wps2.0 */
		if (pmlmepriv->wps_probe_req_ie_len > 0 && pmlmepriv->wps_probe_req_ie) {
			memcpy(pframe, pmlmepriv->wps_probe_req_ie, pmlmepriv->wps_probe_req_ie_len);
			pframe += pmlmepriv->wps_probe_req_ie_len;
			pattrib->pktlen += pmlmepriv->wps_probe_req_ie_len;
		}
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("issuing probe_req, tx_len =%d\n", pattrib->last_txcmdsz));

	if (wait_ack) {
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	} else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

inline void issue_probereq(struct adapter *padapter, struct ndis_802_11_ssid *pssid, u8 *da)
{
	_issue_probereq(padapter, pssid, da, 0, 1, false);
}

int issue_probereq_ex(struct adapter *padapter, struct ndis_802_11_ssid *pssid, u8 *da, u8 ch, bool append_wps,
	int try_cnt, int wait_ms)
{
	int ret;
	int i = 0;

	do {
		ret = _issue_probereq(padapter, pssid, da, ch, append_wps, wait_ms > 0?true:false);

		i++;

		if (padapter->bDriverStopped || padapter->bSurpriseRemoved)
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			msleep(wait_ms);

	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		#ifndef DBG_XMIT_ACK
		goto exit;
		#endif
	}

	if (try_cnt && wait_ms) {
		if (da)
			DBG_871X(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), MAC_ARG(da), rtw_get_oper_ch(padapter),
				ret == _SUCCESS?", acked":"", i, try_cnt, (i + 1) * wait_ms);
		else
			DBG_871X(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret == _SUCCESS?", acked":"", i, try_cnt, (i + 1) * wait_ms);
	}
exit:
	return ret;
}

/*  if psta == NULL, indiate we are station(client) now... */
void issue_auth(struct adapter *padapter, struct sta_info *psta, unsigned short status)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char 				*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	__le16 *fctrl;
	unsigned int					val32;
	unsigned short				val16;
	int use_shared_key = 0;
	struct xmit_priv 		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	__le16 le_tmp;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_AUTH);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);


	if (psta) { /*  for AP mode */
		memcpy(pwlanhdr->addr1, psta->hwaddr, ETH_ALEN);
		memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
		memcpy(pwlanhdr->addr3, myid(&(padapter->eeprompriv)), ETH_ALEN);

		/*  setting auth algo number */
		val16 = (u16)psta->authalg;

		if (status != _STATS_SUCCESSFUL_)
			val16 = 0;

		if (val16)
			use_shared_key = 1;

		le_tmp = cpu_to_le16(val16);

		pframe = rtw_set_fixed_ie(pframe, _AUTH_ALGM_NUM_, (unsigned char *)&le_tmp, &(pattrib->pktlen));

		/*  setting auth seq number */
		val16 = (u16)psta->auth_seq;
		le_tmp = cpu_to_le16(val16);
		pframe = rtw_set_fixed_ie(pframe, _AUTH_SEQ_NUM_, (unsigned char *)&le_tmp, &(pattrib->pktlen));

		/*  setting status code... */
		val16 = status;
		le_tmp = cpu_to_le16(val16);
		pframe = rtw_set_fixed_ie(pframe, _STATUS_CODE_, (unsigned char *)&le_tmp, &(pattrib->pktlen));

		/*  added challenging text... */
		if ((psta->auth_seq == 2) && (psta->state & WIFI_FW_AUTH_STATE) && (use_shared_key == 1))
			pframe = rtw_set_ie(pframe, _CHLGETXT_IE_, 128, psta->chg_txt, &(pattrib->pktlen));

	} else{
		memcpy(pwlanhdr->addr1, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);
		memcpy(pwlanhdr->addr2, myid(&padapter->eeprompriv), ETH_ALEN);
		memcpy(pwlanhdr->addr3, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);

		/*  setting auth algo number */
		val16 = (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared) ? 1 : 0;/*  0:OPEN System, 1:Shared key */
		if (val16) {
			use_shared_key = 1;
		}
		le_tmp = cpu_to_le16(val16);
		/* DBG_871X("%s auth_algo = %s auth_seq =%d\n", __func__, (pmlmeinfo->auth_algo == 0)?"OPEN":"SHARED", pmlmeinfo->auth_seq); */

		/* setting IV for auth seq #3 */
		if ((pmlmeinfo->auth_seq == 3) && (pmlmeinfo->state & WIFI_FW_AUTH_STATE) && (use_shared_key == 1)) {
			__le32 le_tmp32;

			/* DBG_871X("==> iv(%d), key_index(%d)\n", pmlmeinfo->iv, pmlmeinfo->key_index); */
			val32 = ((pmlmeinfo->iv++) | (pmlmeinfo->key_index << 30));
			le_tmp32 = cpu_to_le32(val32);
			pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *)&le_tmp32, &(pattrib->pktlen));

			pattrib->iv_len = 4;
		}

		pframe = rtw_set_fixed_ie(pframe, _AUTH_ALGM_NUM_, (unsigned char *)&le_tmp, &(pattrib->pktlen));

		/*  setting auth seq number */
		le_tmp = cpu_to_le16(pmlmeinfo->auth_seq);
		pframe = rtw_set_fixed_ie(pframe, _AUTH_SEQ_NUM_, (unsigned char *)&le_tmp, &(pattrib->pktlen));


		/*  setting status code... */
		le_tmp = cpu_to_le16(status);
		pframe = rtw_set_fixed_ie(pframe, _STATUS_CODE_, (unsigned char *)&le_tmp, &(pattrib->pktlen));

		/*  then checking to see if sending challenging text... */
		if ((pmlmeinfo->auth_seq == 3) && (pmlmeinfo->state & WIFI_FW_AUTH_STATE) && (use_shared_key == 1)) {
			pframe = rtw_set_ie(pframe, _CHLGETXT_IE_, 128, pmlmeinfo->chg_txt, &(pattrib->pktlen));

			SetPrivacy(fctrl);

			pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);

			pattrib->encrypt = _WEP40_;

			pattrib->icv_len = 4;

			pattrib->pktlen += pattrib->icv_len;

		}

	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	rtw_wep_encrypt(padapter, (u8 *)pmgntframe);
	DBG_871X("%s\n", __func__);
	dump_mgntframe(padapter, pmgntframe);

	return;
}


void issue_asocrsp(struct adapter *padapter, unsigned short status, struct sta_info *pstat, int pkt_type)
{
	struct xmit_frame	*pmgntframe;
	struct ieee80211_hdr	*pwlanhdr;
	struct pkt_attrib *pattrib;
	unsigned char *pbuf, *pframe;
	unsigned short val;
	__le16 *fctrl;
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *pnetwork = &(pmlmeinfo->network);
	u8 *ie = pnetwork->IEs;
	__le16 lestatus, le_tmp;

	DBG_871X("%s\n", __func__);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);


	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;

	memcpy((void *)GetAddr1Ptr(pwlanhdr), pstat->hwaddr, ETH_ALEN);
	memcpy((void *)GetAddr2Ptr(pwlanhdr), myid(&(padapter->eeprompriv)), ETH_ALEN);
	memcpy((void *)GetAddr3Ptr(pwlanhdr), get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);


	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	if ((pkt_type == WIFI_ASSOCRSP) || (pkt_type == WIFI_REASSOCRSP))
		SetFrameSubType(pwlanhdr, pkt_type);
	else
		return;

	pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen += pattrib->hdrlen;
	pframe += pattrib->hdrlen;

	/* capability */
	val = *(unsigned short *)rtw_get_capability_from_ie(ie);

	pframe = rtw_set_fixed_ie(pframe, _CAPABILITY_, (unsigned char *)&val, &(pattrib->pktlen));

	lestatus = cpu_to_le16(status);
	pframe = rtw_set_fixed_ie(pframe, _STATUS_CODE_, (unsigned char *)&lestatus, &(pattrib->pktlen));

	le_tmp = cpu_to_le16(pstat->aid | BIT(14) | BIT(15));
	pframe = rtw_set_fixed_ie(pframe, _ASOC_ID_, (unsigned char *)&le_tmp, &(pattrib->pktlen));

	if (pstat->bssratelen <= 8) {
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, pstat->bssratelen, pstat->bssrateset, &(pattrib->pktlen));
	} else{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, pstat->bssrateset, &(pattrib->pktlen));
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (pstat->bssratelen-8), pstat->bssrateset+8, &(pattrib->pktlen));
	}

	if ((pstat->flags & WLAN_STA_HT) && (pmlmepriv->htpriv.ht_option)) {
		uint ie_len = 0;

		/* FILL HT CAP INFO IE */
		/* p = hostapd_eid_ht_capabilities_info(hapd, p); */
		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_CAPABILITY_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if (pbuf && ie_len > 0) {
			memcpy(pframe, pbuf, ie_len+2);
			pframe += (ie_len+2);
			pattrib->pktlen += (ie_len+2);
		}

		/* FILL HT ADD INFO IE */
		/* p = hostapd_eid_ht_operation(hapd, p); */
		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_ADD_INFO_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if (pbuf && ie_len > 0) {
			memcpy(pframe, pbuf, ie_len+2);
			pframe += (ie_len+2);
			pattrib->pktlen += (ie_len+2);
		}

	}

	/* FILL WMM IE */
	if ((pstat->flags & WLAN_STA_WME) && (pmlmepriv->qospriv.qos_option)) {
		uint ie_len = 0;
		unsigned char WMM_PARA_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};

		for (pbuf = ie + _BEACON_IE_OFFSET_; ; pbuf += (ie_len + 2)) {
			pbuf = rtw_get_ie(pbuf, _VENDOR_SPECIFIC_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_ - (ie_len + 2)));
			if (pbuf && !memcmp(pbuf+2, WMM_PARA_IE, 6)) {
				memcpy(pframe, pbuf, ie_len+2);
				pframe += (ie_len+2);
				pattrib->pktlen += (ie_len+2);

				break;
			}

			if ((pbuf == NULL) || (ie_len == 0)) {
				break;
			}
		}

	}


	if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK) {
		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, 6, REALTEK_96B_IE, &(pattrib->pktlen));
	}

	/* add WPS IE ie for wps 2.0 */
	if (pmlmepriv->wps_assoc_resp_ie && pmlmepriv->wps_assoc_resp_ie_len > 0) {
		memcpy(pframe, pmlmepriv->wps_assoc_resp_ie, pmlmepriv->wps_assoc_resp_ie_len);

		pframe += pmlmepriv->wps_assoc_resp_ie_len;
		pattrib->pktlen += pmlmepriv->wps_assoc_resp_ie_len;
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);
}

void issue_assocreq(struct adapter *padapter)
{
	int ret = _FAIL;
	struct xmit_frame				*pmgntframe;
	struct pkt_attrib				*pattrib;
	unsigned char 				*pframe;
	struct ieee80211_hdr			*pwlanhdr;
	__le16 *fctrl;
	__le16 val16;
	unsigned int					i, j, index = 0;
	unsigned char bssrate[NumRates], sta_bssrate[NumRates];
	struct ndis_80211_var_ie *pIE;
	struct xmit_priv 	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	int	bssrate_len = 0, sta_bssrate_len = 0;
	u8 vs_ie_length = 0;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);


	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;
	memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ASSOCREQ);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	/* caps */
	memcpy(pframe, rtw_get_capability_from_ie(pmlmeinfo->network.IEs), 2);

	pframe += 2;
	pattrib->pktlen += 2;

	/* listen interval */
	/* todo: listen interval for power saving */
	val16 = cpu_to_le16(3);
	memcpy(pframe, (unsigned char *)&val16, 2);
	pframe += 2;
	pattrib->pktlen += 2;

	/* SSID */
	pframe = rtw_set_ie(pframe, _SSID_IE_,  pmlmeinfo->network.Ssid.SsidLength, pmlmeinfo->network.Ssid.Ssid, &(pattrib->pktlen));

	/* supported rate & extended supported rate */

	/*  Check if the AP's supported rates are also supported by STA. */
	get_rate_set(padapter, sta_bssrate, &sta_bssrate_len);
	/* DBG_871X("sta_bssrate_len =%d\n", sta_bssrate_len); */

	if (pmlmeext->cur_channel == 14) /*  for JAPAN, channel 14 can only uses B Mode(CCK) */
		sta_bssrate_len = 4;


	/* for (i = 0; i < sta_bssrate_len; i++) { */
	/* 	DBG_871X("sta_bssrate[%d]=%02X\n", i, sta_bssrate[i]); */
	/*  */

	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		if (pmlmeinfo->network.SupportedRates[i] == 0)
			break;
		DBG_871X("network.SupportedRates[%d]=%02X\n", i, pmlmeinfo->network.SupportedRates[i]);
	}


	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		if (pmlmeinfo->network.SupportedRates[i] == 0)
			break;


		/*  Check if the AP's supported rates are also supported by STA. */
		for (j = 0; j < sta_bssrate_len; j++) {
			 /*  Avoid the proprietary data rate (22Mbps) of Handlink WSG-4000 AP */
			if ((pmlmeinfo->network.SupportedRates[i]|IEEE80211_BASIC_RATE_MASK)
					== (sta_bssrate[j]|IEEE80211_BASIC_RATE_MASK)) {
				/* DBG_871X("match i = %d, j =%d\n", i, j); */
				break;
			} else {
				/* DBG_871X("not match: %02X != %02X\n", (pmlmeinfo->network.SupportedRates[i]|IEEE80211_BASIC_RATE_MASK), (sta_bssrate[j]|IEEE80211_BASIC_RATE_MASK)); */
			}
		}

		if (j == sta_bssrate_len) {
			/*  the rate is not supported by STA */
			DBG_871X("%s(): the rate[%d]=%02X is not supported by STA!\n", __func__, i, pmlmeinfo->network.SupportedRates[i]);
		} else {
			/*  the rate is supported by STA */
			bssrate[index++] = pmlmeinfo->network.SupportedRates[i];
		}
	}

	bssrate_len = index;
	DBG_871X("bssrate_len = %d\n", bssrate_len);

	if (bssrate_len == 0) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit; /* don't connect to AP if no joint supported rate */
	}


	if (bssrate_len > 8) {
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, bssrate, &(pattrib->pktlen));
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	} else
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, bssrate_len, bssrate, &(pattrib->pktlen));

	/* vendor specific IE, such as WPA, WMM, WPS */
	for (i = sizeof(struct ndis_802_11_fix_ie); i < pmlmeinfo->network.IELength;) {
		pIE = (struct ndis_80211_var_ie *)(pmlmeinfo->network.IEs + i);

		switch (pIE->ElementID) {
		case _VENDOR_SPECIFIC_IE_:
			if ((!memcmp(pIE->data, RTW_WPA_OUI, 4)) ||
					(!memcmp(pIE->data, WMM_OUI, 4)) ||
					(!memcmp(pIE->data, WPS_OUI, 4))) {
				vs_ie_length = pIE->Length;
				if ((!padapter->registrypriv.wifi_spec) && (!memcmp(pIE->data, WPS_OUI, 4))) {
					/* Commented by Kurt 20110629 */
					/* In some older APs, WPS handshake */
					/* would be fail if we append vender extensions informations to AP */

					vs_ie_length = 14;
				}

				pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, vs_ie_length, pIE->data, &(pattrib->pktlen));
			}
			break;

		case EID_WPA2:
			pframe = rtw_set_ie(pframe, EID_WPA2, pIE->Length, pIE->data, &(pattrib->pktlen));
			break;
		case EID_HTCapability:
			if (padapter->mlmepriv.htpriv.ht_option) {
				if (!(is_ap_in_tkip(padapter))) {
					memcpy(&(pmlmeinfo->HT_caps), pIE->data, sizeof(struct HT_caps_element));
					pframe = rtw_set_ie(pframe, EID_HTCapability, pIE->Length, (u8 *)(&(pmlmeinfo->HT_caps)), &(pattrib->pktlen));
				}
			}
			break;

		case EID_EXTCapability:
			if (padapter->mlmepriv.htpriv.ht_option)
				pframe = rtw_set_ie(pframe, EID_EXTCapability, pIE->Length, pIE->data, &(pattrib->pktlen));
			break;
		default:
			break;
		}

		i += (pIE->Length + 2);
	}

	if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK)
		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, 6, REALTEK_96B_IE, &(pattrib->pktlen));


	pattrib->last_txcmdsz = pattrib->pktlen;
	dump_mgntframe(padapter, pmgntframe);

	ret = _SUCCESS;

exit:
	if (ret == _SUCCESS)
		rtw_buf_update(&pmlmepriv->assoc_req, &pmlmepriv->assoc_req_len, (u8 *)pwlanhdr, pattrib->pktlen);
	else
		rtw_buf_free(&pmlmepriv->assoc_req, &pmlmepriv->assoc_req_len);

	return;
}

/* when wait_ack is ture, this function shoule be called at process context */
static int _issue_nulldata(struct adapter *padapter, unsigned char *da,
			   unsigned int power_mode, bool wait_ack)
{
	int ret = _FAIL;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char 				*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	__le16 *fctrl;
	struct xmit_priv *pxmitpriv;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;

	/* DBG_871X("%s:%d\n", __func__, power_mode); */

	if (!padapter)
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
	pattrib->retry_ctrl = false;

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;

	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
		SetFrDs(fctrl);
	else if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
		SetToDs(fctrl);

	if (power_mode)
		SetPwrMgt(fctrl);

	memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_DATA_NULL);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pattrib->last_txcmdsz = pattrib->pktlen;

	if (wait_ack) {
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	} else{
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

/*
 * [IMPORTANT] Don't call this function in interrupt context
 *
 * When wait_ms > 0, this function shoule be called at process context
 * da == NULL for station mode
 */
int issue_nulldata(struct adapter *padapter, unsigned char *da, unsigned int power_mode, int try_cnt, int wait_ms)
{
	int ret;
	int i = 0;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_info *psta;


	/* da == NULL, assum it's null data for sta to ap*/
	if (da == NULL)
		da = get_my_bssid(&(pmlmeinfo->network));

	psta = rtw_get_stainfo(&padapter->stapriv, da);
	if (psta) {
		if (power_mode)
			rtw_hal_macid_sleep(padapter, psta->mac_id);
		else
			rtw_hal_macid_wakeup(padapter, psta->mac_id);
	} else {
		DBG_871X(FUNC_ADPT_FMT ": Can't find sta info for " MAC_FMT ", skip macid %s!!\n",
			FUNC_ADPT_ARG(padapter), MAC_ARG(da), power_mode?"sleep":"wakeup");
		rtw_warn_on(1);
	}

	do {
		ret = _issue_nulldata(padapter, da, power_mode, wait_ms > 0?true:false);

		i++;

		if (padapter->bDriverStopped || padapter->bSurpriseRemoved)
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			msleep(wait_ms);

	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		#ifndef DBG_XMIT_ACK
		goto exit;
		#endif
	}

	if (try_cnt && wait_ms) {
		if (da)
			DBG_871X(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), MAC_ARG(da), rtw_get_oper_ch(padapter),
				ret == _SUCCESS?", acked":"", i, try_cnt, (i + 1) * wait_ms);
		else
			DBG_871X(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret == _SUCCESS?", acked":"", i, try_cnt, (i + 1) * wait_ms);
	}
exit:
	return ret;
}

/*
 * [IMPORTANT] This function run in interrupt context
 *
 * The null data packet would be sent without power bit,
 * and not guarantee success.
 */
s32 issue_nulldata_in_interrupt(struct adapter *padapter, u8 *da)
{
	int ret;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;


	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	/* da == NULL, assum it's null data for sta to ap*/
	if (da == NULL)
		da = get_my_bssid(&(pmlmeinfo->network));

	ret = _issue_nulldata(padapter, da, 0, false);

	return ret;
}

/* when wait_ack is ture, this function shoule be called at process context */
static int _issue_qos_nulldata(struct adapter *padapter, unsigned char *da,
			       u16 tid, bool wait_ack)
{
	int ret = _FAIL;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char 				*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	__le16 *fctrl;
	u16 *qc;
	struct xmit_priv 		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	DBG_871X("%s\n", __func__);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	pattrib->hdrlen += 2;
	pattrib->qos_en = true;
	pattrib->eosp = 1;
	pattrib->ack_policy = 0;
	pattrib->mdata = 0;

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;

	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
		SetFrDs(fctrl);
	else if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
		SetToDs(fctrl);

	if (pattrib->mdata)
		SetMData(fctrl);

	qc = (unsigned short *)(pframe + pattrib->hdrlen - 2);

	SetPriority(qc, tid);

	SetEOSP(qc, pattrib->eosp);

	SetAckpolicy(qc, pattrib->ack_policy);

	memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_QOS_DATA_NULL);

	pframe += sizeof(struct ieee80211_qos_hdr);
	pattrib->pktlen = sizeof(struct ieee80211_qos_hdr);

	pattrib->last_txcmdsz = pattrib->pktlen;

	if (wait_ack) {
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	} else{
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

/* when wait_ms >0 , this function shoule be called at process context */
/* da == NULL for station mode */
int issue_qos_nulldata(struct adapter *padapter, unsigned char *da, u16 tid, int try_cnt, int wait_ms)
{
	int ret;
	int i = 0;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	/* da == NULL, assum it's null data for sta to ap*/
	if (da == NULL)
		da = get_my_bssid(&(pmlmeinfo->network));

	do {
		ret = _issue_qos_nulldata(padapter, da, tid, wait_ms > 0?true:false);

		i++;

		if (padapter->bDriverStopped || padapter->bSurpriseRemoved)
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			msleep(wait_ms);

	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		#ifndef DBG_XMIT_ACK
		goto exit;
		#endif
	}

	if (try_cnt && wait_ms) {
		if (da)
			DBG_871X(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), MAC_ARG(da), rtw_get_oper_ch(padapter),
				ret == _SUCCESS?", acked":"", i, try_cnt, (i + 1) * wait_ms);
		else
			DBG_871X(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret == _SUCCESS?", acked":"", i, try_cnt, (i + 1) * wait_ms);
	}
exit:
	return ret;
}

static int _issue_deauth(struct adapter *padapter, unsigned char *da,
			 unsigned short reason, bool wait_ack)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char 				*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	__le16 *fctrl;
	struct xmit_priv 		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	int ret = _FAIL;
	__le16 le_tmp;

	/* DBG_871X("%s to "MAC_FMT"\n", __func__, MAC_ARG(da)); */

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL) {
		goto exit;
	}

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->retry_ctrl = false;

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;

	memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_DEAUTH);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	le_tmp = cpu_to_le16(reason);
	pframe = rtw_set_fixed_ie(pframe, _RSON_CODE_, (unsigned char *)&le_tmp, &(pattrib->pktlen));

	pattrib->last_txcmdsz = pattrib->pktlen;


	if (wait_ack) {
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	} else{
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

int issue_deauth(struct adapter *padapter, unsigned char *da, unsigned short reason)
{
	DBG_871X("%s to "MAC_FMT"\n", __func__, MAC_ARG(da));
	return _issue_deauth(padapter, da, reason, false);
}

int issue_deauth_ex(struct adapter *padapter, u8 *da, unsigned short reason, int try_cnt,
	int wait_ms)
{
	int ret;
	int i = 0;

	do {
		ret = _issue_deauth(padapter, da, reason, wait_ms > 0?true:false);

		i++;

		if (padapter->bDriverStopped || padapter->bSurpriseRemoved)
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			msleep(wait_ms);

	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		#ifndef DBG_XMIT_ACK
		goto exit;
		#endif
	}

	if (try_cnt && wait_ms) {
		if (da)
			DBG_871X(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), MAC_ARG(da), rtw_get_oper_ch(padapter),
				ret == _SUCCESS?", acked":"", i, try_cnt, (i + 1) * wait_ms);
		else
			DBG_871X(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret == _SUCCESS?", acked":"", i, try_cnt, (i + 1) * wait_ms);
	}
exit:
	return ret;
}

void issue_action_SA_Query(struct adapter *padapter, unsigned char *raddr, unsigned char action, unsigned short tid)
{
	u8 category = RTW_WLAN_CATEGORY_SA_QUERY;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	u8 			*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	__le16 *fctrl;
	struct xmit_priv 	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	__le16 le_tmp;

	DBG_871X("%s\n", __func__);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL) {
		DBG_871X("%s: alloc_mgtxmitframe fail\n", __func__);
		return;
	}

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;

	if (raddr)
		memcpy(pwlanhdr->addr1, raddr, ETH_ALEN);
	else
		memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 1, &action, &pattrib->pktlen);

	switch (action) {
	case 0: /* SA Query req */
		pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)&pmlmeext->sa_query_seq, &pattrib->pktlen);
		pmlmeext->sa_query_seq++;
		/* send sa query request to AP, AP should reply sa query response in 1 second */
		set_sa_query_timer(pmlmeext, 1000);
		break;

	case 1: /* SA Query rsp */
		le_tmp = cpu_to_le16(tid);
		pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)&le_tmp, &pattrib->pktlen);
		break;
	default:
		break;
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);
}

void issue_action_BA(struct adapter *padapter, unsigned char *raddr, unsigned char action, unsigned short status)
{
	u8 category = RTW_WLAN_CATEGORY_BACK;
	u16 start_seq;
	u16 BA_para_set;
	u16 reason_code;
	u16 BA_timeout_value;
	u16 BA_starting_seqctrl = 0;
	enum HT_CAP_AMPDU_FACTOR max_rx_ampdu_factor;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	u8 			*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	__le16 *fctrl;
	struct xmit_priv 	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_info 	*psta;
	struct sta_priv 	*pstapriv = &padapter->stapriv;
	struct registry_priv 	*pregpriv = &padapter->registrypriv;
	__le16 le_tmp;

	DBG_871X("%s, category =%d, action =%d, status =%d\n", __func__, category, action, status);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;

	/* memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN); */
	memcpy(pwlanhdr->addr1, raddr, ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));

	if (category == 3) {
		switch (action) {
		case 0: /* ADDBA req */
			do {
				pmlmeinfo->dialogToken++;
			} while (pmlmeinfo->dialogToken == 0);
			pframe = rtw_set_fixed_ie(pframe, 1, &(pmlmeinfo->dialogToken), &(pattrib->pktlen));

			if (rtw_btcoex_IsBTCoexCtrlAMPDUSize(padapter)) {
				/*  A-MSDU NOT Supported */
				BA_para_set = 0;
				/*  immediate Block Ack */
				BA_para_set |= (1 << 1) & IEEE80211_ADDBA_PARAM_POLICY_MASK;
				/*  TID */
				BA_para_set |= (status << 2) & IEEE80211_ADDBA_PARAM_TID_MASK;
				/*  max buffer size is 8 MSDU */
				BA_para_set |= (8 << 6) & RTW_IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK;
			} else {
				BA_para_set = (0x1002 | ((status & 0xf) << 2)); /* immediate ack & 64 buffer size */
			}
			le_tmp = cpu_to_le16(BA_para_set);
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(le_tmp)), &(pattrib->pktlen));

			BA_timeout_value = 5000;/*  5ms */
			le_tmp = cpu_to_le16(BA_timeout_value);
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(le_tmp)), &(pattrib->pktlen));

			/* if ((psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress)) != NULL) */
			psta = rtw_get_stainfo(pstapriv, raddr);
			if (psta != NULL) {
				start_seq = (psta->sta_xmitpriv.txseq_tid[status & 0x07]&0xfff) + 1;

				DBG_871X("BA_starting_seqctrl = %d for TID =%d\n", start_seq, status & 0x07);

				psta->BA_starting_seqctrl[status & 0x07] = start_seq;

				BA_starting_seqctrl = start_seq << 4;
			}

			le_tmp = cpu_to_le16(BA_starting_seqctrl);
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(le_tmp)), &(pattrib->pktlen));
			break;

		case 1: /* ADDBA rsp */
			pframe = rtw_set_fixed_ie(pframe, 1, &(pmlmeinfo->ADDBA_req.dialog_token), &(pattrib->pktlen));
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&status), &(pattrib->pktlen));
			if (padapter->driver_rx_ampdu_factor != 0xFF)
				max_rx_ampdu_factor =
				  (enum HT_CAP_AMPDU_FACTOR)padapter->driver_rx_ampdu_factor;
			else
				rtw_hal_get_def_var(padapter,
						    HW_VAR_MAX_RX_AMPDU_FACTOR, &max_rx_ampdu_factor);

			if (MAX_AMPDU_FACTOR_64K == max_rx_ampdu_factor)
				BA_para_set = ((le16_to_cpu(pmlmeinfo->ADDBA_req.BA_para_set) & 0x3f) | 0x1000); /* 64 buffer size */
			else if (MAX_AMPDU_FACTOR_32K == max_rx_ampdu_factor)
				BA_para_set = ((le16_to_cpu(pmlmeinfo->ADDBA_req.BA_para_set) & 0x3f) | 0x0800); /* 32 buffer size */
			else if (MAX_AMPDU_FACTOR_16K == max_rx_ampdu_factor)
				BA_para_set = ((le16_to_cpu(pmlmeinfo->ADDBA_req.BA_para_set) & 0x3f) | 0x0400); /* 16 buffer size */
			else if (MAX_AMPDU_FACTOR_8K == max_rx_ampdu_factor)
				BA_para_set = ((le16_to_cpu(pmlmeinfo->ADDBA_req.BA_para_set) & 0x3f) | 0x0200); /* 8 buffer size */
			else
				BA_para_set = ((le16_to_cpu(pmlmeinfo->ADDBA_req.BA_para_set) & 0x3f) | 0x1000); /* 64 buffer size */

			if (rtw_btcoex_IsBTCoexCtrlAMPDUSize(padapter) &&
			    padapter->driver_rx_ampdu_factor == 0xFF) {
				/*  max buffer size is 8 MSDU */
				BA_para_set &= ~RTW_IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK;
				BA_para_set |= (8 << 6) & RTW_IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK;
			}

			if (pregpriv->ampdu_amsdu == 0)/* disabled */
				le_tmp = cpu_to_le16(BA_para_set & ~BIT(0));
			else if (pregpriv->ampdu_amsdu == 1)/* enabled */
				le_tmp = cpu_to_le16(BA_para_set | BIT(0));
			else /* auto */
				le_tmp = cpu_to_le16(BA_para_set);

			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(le_tmp)), &(pattrib->pktlen));
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(pmlmeinfo->ADDBA_req.BA_timeout_value)), &(pattrib->pktlen));
			break;
		case 2:/* DELBA */
			BA_para_set = (status & 0x1F) << 3;
			le_tmp = cpu_to_le16(BA_para_set);
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(le_tmp)), &(pattrib->pktlen));

			reason_code = 37;
			le_tmp = cpu_to_le16(reason_code);
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(le_tmp)), &(pattrib->pktlen));
			break;
		default:
			break;
		}
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);
}

static void issue_action_BSSCoexistPacket(struct adapter *padapter)
{
	struct list_head		*plist, *phead;
	unsigned char category, action;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char 			*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	__le16 *fctrl;
	struct	wlan_network	*pnetwork = NULL;
	struct xmit_priv 		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct __queue		*queue	= &(pmlmepriv->scanned_queue);
	u8 InfoContent[16] = {0};
	u8 ICS[8][15];

	if ((pmlmepriv->num_FortyMHzIntolerant == 0) || (pmlmepriv->num_sta_no_ht == 0))
		return;

	if (true == pmlmeinfo->bwmode_updated)
		return;


	DBG_871X("%s\n", __func__);


	category = RTW_WLAN_CATEGORY_PUBLIC;
	action = ACT_PUBLIC_BSSCOEXIST;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL) {
		return;
	}

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;

	memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));


	/*  */
	if (pmlmepriv->num_FortyMHzIntolerant > 0) {
		u8 iedata = 0;

		iedata |= BIT(2);/* 20 MHz BSS Width Request */

		pframe = rtw_set_ie(pframe, EID_BSSCoexistence,  1, &iedata, &(pattrib->pktlen));

	}


	/*  */
	memset(ICS, 0, sizeof(ICS));
	if (pmlmepriv->num_sta_no_ht > 0) {
		int i;

		spin_lock_bh(&(pmlmepriv->scanned_queue.lock));

		phead = get_list_head(queue);
		plist = get_next(phead);

		while (1) {
			int len;
			u8 *p;
			struct wlan_bssid_ex *pbss_network;

			if (phead == plist)
				break;

			pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

			plist = get_next(plist);

			pbss_network = (struct wlan_bssid_ex *)&pnetwork->network;

			p = rtw_get_ie(pbss_network->IEs + _FIXED_IE_LENGTH_, _HT_CAPABILITY_IE_, &len, pbss_network->IELength - _FIXED_IE_LENGTH_);
			if ((p == NULL) || (len == 0)) {/* non-HT */

				if ((pbss_network->Configuration.DSConfig <= 0) || (pbss_network->Configuration.DSConfig > 14))
					continue;

				ICS[0][pbss_network->Configuration.DSConfig] = 1;

				if (ICS[0][0] == 0)
					ICS[0][0] = 1;
			}

		}

		spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));


		for (i = 0; i < 8; i++) {
			if (ICS[i][0] == 1) {
				int j, k = 0;

				InfoContent[k] = i;
				/* SET_BSS_INTOLERANT_ELE_REG_CLASS(InfoContent, i); */
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
}

unsigned int send_delba(struct adapter *padapter, u8 initiator, u8 *addr)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta = NULL;
	/* struct recv_reorder_ctrl *preorder_ctrl; */
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u16 tid;

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		if (!(pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS))
			return _SUCCESS;

	psta = rtw_get_stainfo(pstapriv, addr);
	if (psta == NULL)
		return _SUCCESS;

	/* DBG_871X("%s:%s\n", __func__, (initiator == 0)?"RX_DIR":"TX_DIR"); */

	if (initiator == 0) {/*  recipient */
		for (tid = 0; tid < MAXTID; tid++) {
			if (psta->recvreorder_ctrl[tid].enable) {
				DBG_871X("rx agg disable tid(%d)\n", tid);
				issue_action_BA(padapter, addr, RTW_WLAN_ACTION_DELBA, (((tid << 1) | initiator)&0x1F));
				psta->recvreorder_ctrl[tid].enable = false;
				psta->recvreorder_ctrl[tid].indicate_seq = 0xffff;
				#ifdef DBG_RX_SEQ
				DBG_871X("DBG_RX_SEQ %s:%d indicate_seq:%u\n", __func__, __LINE__,
					psta->recvreorder_ctrl[tid].indicate_seq);
				#endif
			}
		}
	} else if (initiator == 1) {/*  originator */
		/* DBG_871X("tx agg_enable_bitmap(0x%08x)\n", psta->htpriv.agg_enable_bitmap); */
		for (tid = 0; tid < MAXTID; tid++) {
			if (psta->htpriv.agg_enable_bitmap & BIT(tid)) {
				DBG_871X("tx agg disable tid(%d)\n", tid);
				issue_action_BA(padapter, addr, RTW_WLAN_ACTION_DELBA, (((tid << 1) | initiator)&0x1F));
				psta->htpriv.agg_enable_bitmap &= ~BIT(tid);
				psta->htpriv.candidate_tid_bitmap &= ~BIT(tid);

			}
		}
	}

	return _SUCCESS;

}

unsigned int send_beacon(struct adapter *padapter)
{
	u8 bxmitok = false;
	int	issue = 0;
	int poll = 0;
	unsigned long start = jiffies;

	rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
	rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);
	do {
		issue_beacon(padapter, 100);
		issue++;
		do {
			cond_resched();
			rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, (u8 *)(&bxmitok));
			poll++;
		} while ((poll%10) != 0 && false == bxmitok && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);

	} while (false == bxmitok && issue < 100 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped) {
		return _FAIL;
	}


	if (false == bxmitok) {
		DBG_871X("%s fail! %u ms\n", __func__, jiffies_to_msecs(jiffies - start));
		return _FAIL;
	} else{
		unsigned long passing_time = jiffies_to_msecs(jiffies - start);

		if (passing_time > 100 || issue > 3)
			DBG_871X("%s success, issue:%d, poll:%d, %lu ms\n", __func__, issue, poll, passing_time);
		/* else */
		/* 	DBG_871X("%s success, issue:%d, poll:%d, %u ms\n", __func__, issue, poll, passing_time); */

		return _SUCCESS;
	}
}

/****************************************************************************

Following are some utitity fuctions for WiFi MLME

*****************************************************************************/

void site_survey(struct adapter *padapter)
{
	unsigned char 	survey_channel = 0, val8;
	RT_SCAN_TYPE	ScanType = SCAN_PASSIVE;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u32 initialgain = 0;
	u32 channel_scan_time_ms = 0;

	{
		struct rtw_ieee80211_channel *ch;
		if (pmlmeext->sitesurvey_res.channel_idx < pmlmeext->sitesurvey_res.ch_num) {
			ch = &pmlmeext->sitesurvey_res.ch[pmlmeext->sitesurvey_res.channel_idx];
			survey_channel = ch->hw_value;
			ScanType = (ch->flags & RTW_IEEE80211_CHAN_PASSIVE_SCAN) ? SCAN_PASSIVE : SCAN_ACTIVE;
		}
	}

	DBG_871X(FUNC_ADPT_FMT" ch:%u (cnt:%u) at %dms, %c%c%c\n"
		 , FUNC_ADPT_ARG(padapter)
		 , survey_channel
		 , pmlmeext->sitesurvey_res.channel_idx
		 , jiffies_to_msecs(jiffies - padapter->mlmepriv.scan_start_time)
		 , ScanType?'A':'P', pmlmeext->sitesurvey_res.scan_mode?'A':'P'
		 , pmlmeext->sitesurvey_res.ssid[0].SsidLength?'S':' '
		);
#ifdef DBG_FIXED_CHAN
	DBG_871X(FUNC_ADPT_FMT" fixed_chan:%u\n", pmlmeext->fixed_chan);
#endif

	if (survey_channel != 0) {
		/* PAUSE 4-AC Queue when site_survey */
		/* rtw_hal_get_hwreg(padapter, HW_VAR_TXPAUSE, (u8 *)(&val8)); */
		/* val8 |= 0x0f; */
		/* rtw_hal_set_hwreg(padapter, HW_VAR_TXPAUSE, (u8 *)(&val8)); */
		if (pmlmeext->sitesurvey_res.channel_idx == 0) {
#ifdef DBG_FIXED_CHAN
			if (pmlmeext->fixed_chan != 0xff)
				set_channel_bwmode(padapter, pmlmeext->fixed_chan, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
			else
#endif
				set_channel_bwmode(padapter, survey_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
		} else{
#ifdef DBG_FIXED_CHAN
			if (pmlmeext->fixed_chan != 0xff)
				SelectChannel(padapter, pmlmeext->fixed_chan);
			else
#endif
				SelectChannel(padapter, survey_channel);
		}

		if (ScanType == SCAN_ACTIVE) { /* obey the channel plan setting... */
			{
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

				if (pmlmeext->sitesurvey_res.scan_mode == SCAN_ACTIVE) {
					/* IOT issue, When wifi_spec is not set, send one probe req without WPS IE. */
					if (padapter->registrypriv.wifi_spec)
						issue_probereq(padapter, NULL, NULL);
					else
						issue_probereq_ex(padapter, NULL, NULL, 0, 0, 0, 0);
					issue_probereq(padapter, NULL, NULL);
				}
			}
		}

		channel_scan_time_ms = pmlmeext->chan_scan_time;

		set_survey_timer(pmlmeext, channel_scan_time_ms);
#if defined(CONFIG_SIGNAL_DISPLAY_DBM) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
		{
			struct noise_info info;
			info.bPauseDIG = false;
			info.IGIValue = 0;
			info.max_time = channel_scan_time_ms/2;/* ms */
			info.chan = survey_channel;
			rtw_hal_set_odm_var(padapter, HAL_ODM_NOISE_MONITOR, &info, false);
		}
#endif

	} else{

		/* 	channel number is 0 or this channel is not valid. */

		{
			pmlmeext->sitesurvey_res.state = SCAN_COMPLETE;

			/* switch back to the original channel */
			/* SelectChannel(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset); */

			set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

			/* flush 4-AC Queue after site_survey */
			/* val8 = 0; */
			/* rtw_hal_set_hwreg(padapter, HW_VAR_TXPAUSE, (u8 *)(&val8)); */

			/* config MSR */
			Set_MSR(padapter, (pmlmeinfo->state & 0x3));

			initialgain = 0xff; /* restore RX GAIN */
			rtw_hal_set_hwreg(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));
			/* turn on dynamic functions */
			Restore_DM_Func_Flag(padapter);
			/* Switch_DM_Func(padapter, DYNAMIC_ALL_FUNC_ENABLE, true); */

			if (is_client_associated_to_ap(padapter))
				issue_nulldata(padapter, NULL, 0, 3, 500);

			val8 = 0; /* survey done */
			rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

			report_surveydone_event(padapter);

			pmlmeext->chan_scan_time = SURVEY_TO;
			pmlmeext->sitesurvey_res.state = SCAN_DISABLE;

			issue_action_BSSCoexistPacket(padapter);
			issue_action_BSSCoexistPacket(padapter);
			issue_action_BSSCoexistPacket(padapter);
		}
	}

	return;

}

/* collect bss info from Beacon and Probe request/response frames. */
u8 collect_bss_info(struct adapter *padapter, union recv_frame *precv_frame, struct wlan_bssid_ex *bssid)
{
	int	i;
	u32 len;
	u8 *p;
	u16 val16, subtype;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	u32 packet_len = precv_frame->u.hdr.len;
	u8 ie_offset;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	__le32 le32_tmp;

	len = packet_len - sizeof(struct ieee80211_hdr_3addr);

	if (len > MAX_IE_SZ) {
		/* DBG_871X("IE too long for survey event\n"); */
		return _FAIL;
	}

	memset(bssid, 0, sizeof(struct wlan_bssid_ex));

	subtype = GetFrameSubType(pframe);

	if (subtype == WIFI_BEACON) {
		bssid->Reserved[0] = 1;
		ie_offset = _BEACON_IE_OFFSET_;
	} else {
		/*  FIXME : more type */
		if (subtype == WIFI_PROBERSP) {
			ie_offset = _PROBERSP_IE_OFFSET_;
			bssid->Reserved[0] = 3;
		} else if (subtype == WIFI_PROBEREQ) {
			ie_offset = _PROBEREQ_IE_OFFSET_;
			bssid->Reserved[0] = 2;
		} else {
			bssid->Reserved[0] = 0;
			ie_offset = _FIXED_IE_LENGTH_;
		}
	}

	bssid->Length = sizeof(struct wlan_bssid_ex) - MAX_IE_SZ + len;

	/* below is to copy the information element */
	bssid->IELength = len;
	memcpy(bssid->IEs, (pframe + sizeof(struct ieee80211_hdr_3addr)), bssid->IELength);

	/* get the signal strength */
	bssid->Rssi = precv_frame->u.hdr.attrib.phy_info.RecvSignalPower; /*  in dBM.raw data */
	bssid->PhyInfo.SignalQuality = precv_frame->u.hdr.attrib.phy_info.SignalQuality;/* in percentage */
	bssid->PhyInfo.SignalStrength = precv_frame->u.hdr.attrib.phy_info.SignalStrength;/* in percentage */

	/*  checking SSID */
	p = rtw_get_ie(bssid->IEs + ie_offset, _SSID_IE_, &len, bssid->IELength - ie_offset);
	if (p == NULL) {
		DBG_871X("marc: cannot find SSID for survey event\n");
		return _FAIL;
	}

	if (*(p + 1)) {
		if (len > NDIS_802_11_LENGTH_SSID) {
			DBG_871X("%s()-%d: IE too long (%d) for survey event\n", __func__, __LINE__, len);
			return _FAIL;
		}
		memcpy(bssid->Ssid.Ssid, (p + 2), *(p + 1));
		bssid->Ssid.SsidLength = *(p + 1);
	} else
		bssid->Ssid.SsidLength = 0;

	memset(bssid->SupportedRates, 0, NDIS_802_11_LENGTH_RATES_EX);

	/* checking rate info... */
	i = 0;
	p = rtw_get_ie(bssid->IEs + ie_offset, _SUPPORTEDRATES_IE_, &len, bssid->IELength - ie_offset);
	if (p != NULL) {
		if (len > NDIS_802_11_LENGTH_RATES_EX) {
			DBG_871X("%s()-%d: IE too long (%d) for survey event\n", __func__, __LINE__, len);
			return _FAIL;
		}
		memcpy(bssid->SupportedRates, (p + 2), len);
		i = len;
	}

	p = rtw_get_ie(bssid->IEs + ie_offset, _EXT_SUPPORTEDRATES_IE_, &len, bssid->IELength - ie_offset);
	if (p != NULL) {
		if (len > (NDIS_802_11_LENGTH_RATES_EX-i)) {
			DBG_871X("%s()-%d: IE too long (%d) for survey event\n", __func__, __LINE__, len);
			return _FAIL;
		}
		memcpy(bssid->SupportedRates + i, (p + 2), len);
	}

	bssid->NetworkTypeInUse = Ndis802_11OFDM24;

	if (bssid->IELength < 12)
		return _FAIL;

	/*  Checking for DSConfig */
	p = rtw_get_ie(bssid->IEs + ie_offset, _DSSET_IE_, &len, bssid->IELength - ie_offset);

	bssid->Configuration.DSConfig = 0;
	bssid->Configuration.Length = 0;

	if (p) {
		bssid->Configuration.DSConfig = *(p + 2);
	} else {
		/*  In 5G, some ap do not have DSSET IE */
		/*  checking HT info for channel */
		p = rtw_get_ie(bssid->IEs + ie_offset, _HT_ADD_INFO_IE_, &len, bssid->IELength - ie_offset);
		if (p) {
			struct HT_info_element *HT_info = (struct HT_info_element *)(p + 2);
			bssid->Configuration.DSConfig = HT_info->primary_channel;
		} else { /*  use current channel */
			bssid->Configuration.DSConfig = rtw_get_oper_ch(padapter);
		}
	}

	memcpy(&le32_tmp, rtw_get_beacon_interval_from_ie(bssid->IEs), 2);
	bssid->Configuration.BeaconPeriod = le32_to_cpu(le32_tmp);

	val16 = rtw_get_capability((struct wlan_bssid_ex *)bssid);

	if (val16 & BIT(0)) {
		bssid->InfrastructureMode = Ndis802_11Infrastructure;
		memcpy(bssid->MacAddress, GetAddr2Ptr(pframe), ETH_ALEN);
	} else {
		bssid->InfrastructureMode = Ndis802_11IBSS;
		memcpy(bssid->MacAddress, GetAddr3Ptr(pframe), ETH_ALEN);
	}

	if (val16 & BIT(4))
		bssid->Privacy = 1;
	else
		bssid->Privacy = 0;

	bssid->Configuration.ATIMWindow = 0;

	/* 20/40 BSS Coexistence check */
	if ((pregistrypriv->wifi_spec == 1) && (false == pmlmeinfo->bwmode_updated)) {
		struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

		p = rtw_get_ie(bssid->IEs + ie_offset, _HT_CAPABILITY_IE_, &len, bssid->IELength - ie_offset);
		if (p && len > 0) {
			struct HT_caps_element	*pHT_caps;
			pHT_caps = (struct HT_caps_element	*)(p + 2);

			if (le16_to_cpu(pHT_caps->u.HT_cap_element.HT_caps_info) & BIT(14))
				pmlmepriv->num_FortyMHzIntolerant++;
		} else
			pmlmepriv->num_sta_no_ht++;
	}

#ifdef CONFIG_INTEL_WIDI
	/* process_intel_widi_query_or_tigger(padapter, bssid); */
	if (process_intel_widi_query_or_tigger(padapter, bssid))
		return _FAIL;
#endif /*  CONFIG_INTEL_WIDI */

	#if defined(DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) & 1
	if (strcmp(bssid->Ssid.Ssid, DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) == 0) {
		DBG_871X("Receiving %s("MAC_FMT", DSConfig:%u) from ch%u with ss:%3u, sq:%3u, RawRSSI:%3ld\n"
			, bssid->Ssid.Ssid, MAC_ARG(bssid->MacAddress), bssid->Configuration.DSConfig
			, rtw_get_oper_ch(padapter)
			, bssid->PhyInfo.SignalStrength, bssid->PhyInfo.SignalQuality, bssid->Rssi
		);
	}
	#endif

	/*  mark bss info receving from nearby channel as SignalQuality 101 */
	if (bssid->Configuration.DSConfig != rtw_get_oper_ch(padapter))
		bssid->PhyInfo.SignalQuality = 101;

	return _SUCCESS;
}

void start_create_ibss(struct adapter *padapter)
{
	unsigned short	caps;
	u8 val8;
	u8 join_type;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*pnetwork = (struct wlan_bssid_ex *)(&(pmlmeinfo->network));
	pmlmeext->cur_channel = (u8)pnetwork->Configuration.DSConfig;
	pmlmeinfo->bcn_interval = get_beacon_interval(pnetwork);

	/* update wireless mode */
	update_wireless_mode(padapter);

	/* udpate capability */
	caps = rtw_get_capability((struct wlan_bssid_ex *)pnetwork);
	update_capinfo(padapter, caps);
	if (caps&cap_IBSS) {/* adhoc master */
		val8 = 0xcf;
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		rtw_hal_set_hwreg(padapter, HW_VAR_DO_IQK, NULL);

		/* switch channel */
		/* SelectChannel(padapter, pmlmeext->cur_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE); */
		set_channel_bwmode(padapter, pmlmeext->cur_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);

		beacon_timing_control(padapter);

		/* set msr to WIFI_FW_ADHOC_STATE */
		pmlmeinfo->state = WIFI_FW_ADHOC_STATE;
		Set_MSR(padapter, (pmlmeinfo->state & 0x3));

		/* issue beacon */
		if (send_beacon(padapter) == _FAIL) {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("issuing beacon frame fail....\n"));

			report_join_res(padapter, -1);
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
		} else{
			rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, padapter->registrypriv.dev_network.MacAddress);
			join_type = 0;
			rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

			report_join_res(padapter, 1);
			pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;
			rtw_indicate_connect(padapter);
		}
	} else{
		DBG_871X("start_create_ibss, invalid cap:%x\n", caps);
		return;
	}
	/* update bc/mc sta_info */
	update_bmc_sta(padapter);

}

void start_clnt_join(struct adapter *padapter)
{
	unsigned short	caps;
	u8 val8;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*pnetwork = (struct wlan_bssid_ex *)(&(pmlmeinfo->network));
	int beacon_timeout;

	/* update wireless mode */
	update_wireless_mode(padapter);

	/* udpate capability */
	caps = rtw_get_capability((struct wlan_bssid_ex *)pnetwork);
	update_capinfo(padapter, caps);
	if (caps&cap_ESS) {
		Set_MSR(padapter, WIFI_FW_STATION_STATE);

		val8 = (pmlmeinfo->auth_algo == dot11AuthAlgrthm_8021X) ? 0xcc : 0xcf;

		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		/*  Because of AP's not receiving deauth before */
		/*  AP may: 1)not response auth or 2)deauth us after link is complete */
		/*  issue deauth before issuing auth to deal with the situation */

		/* 	Commented by Albert 2012/07/21 */
		/* 	For the Win8 P2P connection, it will be hard to have a successful connection if this Wi-Fi doesn't connect to it. */
		{
				/* To avoid connecting to AP fail during resume process, change retry count from 5 to 1 */
				issue_deauth_ex(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING, 1, 100);
		}

		/* here wait for receiving the beacon to start auth */
		/* and enable a timer */
		beacon_timeout = decide_wait_for_beacon_timeout(pmlmeinfo->bcn_interval);
		set_link_timer(pmlmeext, beacon_timeout);
		_set_timer(&padapter->mlmepriv.assoc_timer,
			(REAUTH_TO * REAUTH_LIMIT) + (REASSOC_TO*REASSOC_LIMIT) + beacon_timeout);

		pmlmeinfo->state = WIFI_FW_AUTH_NULL | WIFI_FW_STATION_STATE;
	} else if (caps&cap_IBSS) { /* adhoc client */
		Set_MSR(padapter, WIFI_FW_ADHOC_STATE);

		val8 = 0xcf;
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		beacon_timing_control(padapter);

		pmlmeinfo->state = WIFI_FW_ADHOC_STATE;

		report_join_res(padapter, 1);
	} else{
		/* DBG_871X("marc: invalid cap:%x\n", caps); */
		return;
	}

}

void start_clnt_auth(struct adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	del_timer_sync(&pmlmeext->link_timer);

	pmlmeinfo->state &= (~WIFI_FW_AUTH_NULL);
	pmlmeinfo->state |= WIFI_FW_AUTH_STATE;

	pmlmeinfo->auth_seq = 1;
	pmlmeinfo->reauth_count = 0;
	pmlmeinfo->reassoc_count = 0;
	pmlmeinfo->link_count = 0;
	pmlmeext->retry = 0;


	DBG_871X_LEVEL(_drv_always_, "start auth\n");
	issue_auth(padapter, NULL, 0);

	set_link_timer(pmlmeext, REAUTH_TO);

}


void start_clnt_assoc(struct adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	del_timer_sync(&pmlmeext->link_timer);

	pmlmeinfo->state &= (~(WIFI_FW_AUTH_NULL | WIFI_FW_AUTH_STATE));
	pmlmeinfo->state |= (WIFI_FW_AUTH_SUCCESS | WIFI_FW_ASSOC_STATE);

	issue_assocreq(padapter);

	set_link_timer(pmlmeext, REASSOC_TO);
}

unsigned int receive_disconnect(struct adapter *padapter, unsigned char *MacAddr, unsigned short reason)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	/* check A3 */
	if (!(!memcmp(MacAddr, get_my_bssid(&pmlmeinfo->network), ETH_ALEN)))
		return _SUCCESS;

	DBG_871X("%s\n", __func__);

	if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE) {
		if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) {
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
			report_del_sta_event(padapter, MacAddr, reason);

		} else if (pmlmeinfo->state & WIFI_FW_LINKING_STATE) {
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
			report_join_res(padapter, -2);
		}
	}

	return _SUCCESS;
}

static void process_80211d(struct adapter *padapter, struct wlan_bssid_ex *bssid)
{
	struct registry_priv *pregistrypriv;
	struct mlme_ext_priv *pmlmeext;
	RT_CHANNEL_INFO *chplan_new;
	u8 channel;
	u8 i;


	pregistrypriv = &padapter->registrypriv;
	pmlmeext = &padapter->mlmeextpriv;

	/*  Adjust channel plan by AP Country IE */
	if (pregistrypriv->enable80211d &&
		(!pmlmeext->update_channel_plan_by_ap_done)) {
		u8 *ie, *p;
		u32 len;
		RT_CHANNEL_PLAN chplan_ap;
		RT_CHANNEL_INFO chplan_sta[MAX_CHANNEL_NUM];
		u8 country[4];
		u8 fcn; /*  first channel number */
		u8 noc; /*  number of channel */
		u8 j, k;

		ie = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _COUNTRY_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
		if (!ie)
			return;
		if (len < 6)
			return;

		ie += 2;
		p = ie;
		ie += len;

		memset(country, 0, 4);
		memcpy(country, p, 3);
		p += 3;
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
				("%s: 802.11d country =%s\n", __func__, country));

		i = 0;
		while ((ie - p) >= 3) {
			fcn = *(p++);
			noc = *(p++);
			p++;

			for (j = 0; j < noc; j++) {
				if (fcn <= 14)
					channel = fcn + j; /*  2.4 GHz */
				else
					channel = fcn + j*4; /*  5 GHz */

				chplan_ap.Channel[i++] = channel;
			}
		}
		chplan_ap.Len = i;

#ifdef DEBUG_RTL871X
		i = 0;
		DBG_871X("%s: AP[%s] channel plan {", __func__, bssid->Ssid.Ssid);
		while ((i < chplan_ap.Len) && (chplan_ap.Channel[i] != 0)) {
			DBG_8192C("%02d,", chplan_ap.Channel[i]);
			i++;
		}
		DBG_871X("}\n");
#endif

		memcpy(chplan_sta, pmlmeext->channel_set, sizeof(chplan_sta));
#ifdef DEBUG_RTL871X
		i = 0;
		DBG_871X("%s: STA channel plan {", __func__);
		while ((i < MAX_CHANNEL_NUM) && (chplan_sta[i].ChannelNum != 0)) {
			DBG_871X("%02d(%c),", chplan_sta[i].ChannelNum, chplan_sta[i].ScanType == SCAN_PASSIVE?'p':'a');
			i++;
		}
		DBG_871X("}\n");
#endif

		memset(pmlmeext->channel_set, 0, sizeof(pmlmeext->channel_set));
		chplan_new = pmlmeext->channel_set;

		i = j = k = 0;
		if (pregistrypriv->wireless_mode & WIRELESS_11G) {
			do {
				if ((i == MAX_CHANNEL_NUM) ||
					(chplan_sta[i].ChannelNum == 0) ||
					(chplan_sta[i].ChannelNum > 14))
					break;

				if ((j == chplan_ap.Len) || (chplan_ap.Channel[j] > 14))
					break;

				if (chplan_sta[i].ChannelNum == chplan_ap.Channel[j]) {
					chplan_new[k].ChannelNum = chplan_ap.Channel[j];
					chplan_new[k].ScanType = SCAN_ACTIVE;
					i++;
					j++;
					k++;
				} else if (chplan_sta[i].ChannelNum < chplan_ap.Channel[j]) {
					chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
/* 					chplan_new[k].ScanType = chplan_sta[i].ScanType; */
					chplan_new[k].ScanType = SCAN_PASSIVE;
					i++;
					k++;
				} else if (chplan_sta[i].ChannelNum > chplan_ap.Channel[j]) {
					chplan_new[k].ChannelNum = chplan_ap.Channel[j];
					chplan_new[k].ScanType = SCAN_ACTIVE;
					j++;
					k++;
				}
			} while (1);

			/*  change AP not support channel to Passive scan */
			while ((i < MAX_CHANNEL_NUM) &&
				(chplan_sta[i].ChannelNum != 0) &&
				(chplan_sta[i].ChannelNum <= 14)) {

				chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
/* 				chplan_new[k].ScanType = chplan_sta[i].ScanType; */
				chplan_new[k].ScanType = SCAN_PASSIVE;
				i++;
				k++;
			}

			/*  add channel AP supported */
			while ((j < chplan_ap.Len) && (chplan_ap.Channel[j] <= 14)) {
				chplan_new[k].ChannelNum = chplan_ap.Channel[j];
				chplan_new[k].ScanType = SCAN_ACTIVE;
				j++;
				k++;
			}
		} else{
			/*  keep original STA 2.4G channel plan */
			while ((i < MAX_CHANNEL_NUM) &&
				(chplan_sta[i].ChannelNum != 0) &&
				(chplan_sta[i].ChannelNum <= 14)) {
				chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
				chplan_new[k].ScanType = chplan_sta[i].ScanType;
				i++;
				k++;
			}

			/*  skip AP 2.4G channel plan */
			while ((j < chplan_ap.Len) && (chplan_ap.Channel[j] <= 14)) {
				j++;
			}
		}

		if (pregistrypriv->wireless_mode & WIRELESS_11A) {
			do {
				if ((i == MAX_CHANNEL_NUM) ||
					(chplan_sta[i].ChannelNum == 0))
					break;

				if ((j == chplan_ap.Len) || (chplan_ap.Channel[j] == 0))
					break;

				if (chplan_sta[i].ChannelNum == chplan_ap.Channel[j]) {
					chplan_new[k].ChannelNum = chplan_ap.Channel[j];
					chplan_new[k].ScanType = SCAN_ACTIVE;
					i++;
					j++;
					k++;
				} else if (chplan_sta[i].ChannelNum < chplan_ap.Channel[j]) {
					chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
/* 					chplan_new[k].ScanType = chplan_sta[i].ScanType; */
					chplan_new[k].ScanType = SCAN_PASSIVE;
					i++;
					k++;
				} else if (chplan_sta[i].ChannelNum > chplan_ap.Channel[j]) {
					chplan_new[k].ChannelNum = chplan_ap.Channel[j];
					chplan_new[k].ScanType = SCAN_ACTIVE;
					j++;
					k++;
				}
			} while (1);

			/*  change AP not support channel to Passive scan */
			while ((i < MAX_CHANNEL_NUM) && (chplan_sta[i].ChannelNum != 0)) {
				chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
/* 				chplan_new[k].ScanType = chplan_sta[i].ScanType; */
				chplan_new[k].ScanType = SCAN_PASSIVE;
				i++;
				k++;
			}

			/*  add channel AP supported */
			while ((j < chplan_ap.Len) && (chplan_ap.Channel[j] != 0)) {
				chplan_new[k].ChannelNum = chplan_ap.Channel[j];
				chplan_new[k].ScanType = SCAN_ACTIVE;
				j++;
				k++;
			}
		} else{
			/*  keep original STA 5G channel plan */
			while ((i < MAX_CHANNEL_NUM) && (chplan_sta[i].ChannelNum != 0)) {
				chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
				chplan_new[k].ScanType = chplan_sta[i].ScanType;
				i++;
				k++;
			}
		}

		pmlmeext->update_channel_plan_by_ap_done = 1;

#ifdef DEBUG_RTL871X
		k = 0;
		DBG_871X("%s: new STA channel plan {", __func__);
		while ((k < MAX_CHANNEL_NUM) && (chplan_new[k].ChannelNum != 0)) {
			DBG_871X("%02d(%c),", chplan_new[k].ChannelNum, chplan_new[k].ScanType == SCAN_PASSIVE?'p':'c');
			k++;
		}
		DBG_871X("}\n");
#endif
	}

	/*  If channel is used by AP, set channel scan type to active */
	channel = bssid->Configuration.DSConfig;
	chplan_new = pmlmeext->channel_set;
	i = 0;
	while ((i < MAX_CHANNEL_NUM) && (chplan_new[i].ChannelNum != 0)) {
		if (chplan_new[i].ChannelNum == channel) {
			if (chplan_new[i].ScanType == SCAN_PASSIVE) {
				/* 5G Bnad 2, 3 (DFS) doesn't change to active scan */
				if (channel >= 52 && channel <= 144)
					break;

				chplan_new[i].ScanType = SCAN_ACTIVE;
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
						 ("%s: change channel %d scan type from passive to active\n",
						  __func__, channel));
			}
			break;
		}
		i++;
	}
}

/****************************************************************************

Following are the functions to report events

*****************************************************************************/

void report_survey_event(struct adapter *padapter, union recv_frame *precv_frame)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct survey_event	*psurvey_evt;
	struct C2HEvent_Header *pc2h_evt_hdr;
	struct mlme_ext_priv *pmlmeext;
	struct cmd_priv *pcmdpriv;
	/* u8 *pframe = precv_frame->u.hdr.rx_data; */
	/* uint len = precv_frame->u.hdr.len; */

	if (!padapter)
		return;

	pmlmeext = &padapter->mlmeextpriv;
	pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd_obj == NULL)
		return;

	cmdsz = (sizeof(struct survey_event) + sizeof(struct C2HEvent_Header));
	pevtcmd = rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		kfree((u8 *)pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header *)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct survey_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_Survey);
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	psurvey_evt = (struct survey_event *)(pevtcmd + sizeof(struct C2HEvent_Header));

	if (collect_bss_info(padapter, precv_frame, (struct wlan_bssid_ex *)&psurvey_evt->bss) == _FAIL) {
		kfree((u8 *)pcmd_obj);
		kfree((u8 *)pevtcmd);
		return;
	}

	process_80211d(padapter, &psurvey_evt->bss);

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	pmlmeext->sitesurvey_res.bss_cnt++;

	return;

}

void report_surveydone_event(struct adapter *padapter)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct surveydone_event *psurveydone_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv 	*pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd_obj == NULL)
		return;

	cmdsz = (sizeof(struct surveydone_event) + sizeof(struct C2HEvent_Header));
	pevtcmd = rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		kfree((u8 *)pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header *)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct surveydone_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_SurveyDone);
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	psurveydone_evt = (struct surveydone_event *)(pevtcmd + sizeof(struct C2HEvent_Header));
	psurveydone_evt->bss_cnt = pmlmeext->sitesurvey_res.bss_cnt;

	DBG_871X("survey done event(%x) band:%d for "ADPT_FMT"\n", psurveydone_evt->bss_cnt, padapter->setband, ADPT_ARG(padapter));

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;

}

void report_join_res(struct adapter *padapter, int res)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct joinbss_event		*pjoinbss_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv 	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd_obj == NULL)
		return;

	cmdsz = (sizeof(struct joinbss_event) + sizeof(struct C2HEvent_Header));
	pevtcmd = rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		kfree((u8 *)pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header *)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct joinbss_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_JoinBss);
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	pjoinbss_evt = (struct joinbss_event *)(pevtcmd + sizeof(struct C2HEvent_Header));
	memcpy((unsigned char *)(&(pjoinbss_evt->network.network)), &(pmlmeinfo->network), sizeof(struct wlan_bssid_ex));
	pjoinbss_evt->network.join_res	= pjoinbss_evt->network.aid = res;

	DBG_871X("report_join_res(%d)\n", res);


	rtw_joinbss_event_prehandle(padapter, (u8 *)&pjoinbss_evt->network);


	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;

}

void report_wmm_edca_update(struct adapter *padapter)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct wmm_event		*pwmm_event;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv 	*pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd_obj == NULL)
		return;

	cmdsz = (sizeof(struct wmm_event) + sizeof(struct C2HEvent_Header));
	pevtcmd = rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		kfree((u8 *)pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header *)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct wmm_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_WMM);
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	pwmm_event = (struct wmm_event *)(pevtcmd + sizeof(struct C2HEvent_Header));
	pwmm_event->wmm = 0;

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;

}

void report_del_sta_event(struct adapter *padapter, unsigned char *MacAddr, unsigned short reason)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct sta_info *psta;
	int	mac_id;
	struct stadel_event			*pdel_sta_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv 	*pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd_obj == NULL) {
		return;
	}

	cmdsz = (sizeof(struct stadel_event) + sizeof(struct C2HEvent_Header));
	pevtcmd = rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		kfree((u8 *)pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header *)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct stadel_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_DelSTA);
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	pdel_sta_evt = (struct stadel_event *)(pevtcmd + sizeof(struct C2HEvent_Header));
	memcpy((unsigned char *)(&(pdel_sta_evt->macaddr)), MacAddr, ETH_ALEN);
	memcpy((unsigned char *)(pdel_sta_evt->rsvd), (unsigned char *)(&reason), 2);


	psta = rtw_get_stainfo(&padapter->stapriv, MacAddr);
	if (psta)
		mac_id = (int)psta->mac_id;
	else
		mac_id = (-1);

	pdel_sta_evt->mac_id = mac_id;

	DBG_871X("report_del_sta_event: delete STA, mac_id =%d\n", mac_id);

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;
}

void report_add_sta_event(struct adapter *padapter, unsigned char *MacAddr, int cam_idx)
{
	struct cmd_obj *pcmd_obj;
	u8 *pevtcmd;
	u32 cmdsz;
	struct stassoc_event		*padd_sta_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv 	*pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd_obj == NULL)
		return;

	cmdsz = (sizeof(struct stassoc_event) + sizeof(struct C2HEvent_Header));
	pevtcmd = rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		kfree((u8 *)pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header *)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct stassoc_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_AddSTA);
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	padd_sta_evt = (struct stassoc_event *)(pevtcmd + sizeof(struct C2HEvent_Header));
	memcpy((unsigned char *)(&(padd_sta_evt->macaddr)), MacAddr, ETH_ALEN);
	padd_sta_evt->cam_id = cam_idx;

	DBG_871X("report_add_sta_event: add STA\n");

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;
}


bool rtw_port_switch_chk(struct adapter *adapter)
{
	bool switch_needed = false;
	return switch_needed;
}

/****************************************************************************

Following are the event callback functions

*****************************************************************************/

/* for sta/adhoc mode */
void update_sta_info(struct adapter *padapter, struct sta_info *psta)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	/* ERP */
	VCS_update(padapter, psta);

	/* HT */
	if (pmlmepriv->htpriv.ht_option) {
		psta->htpriv.ht_option = true;

		psta->htpriv.ampdu_enable = pmlmepriv->htpriv.ampdu_enable;

		psta->htpriv.rx_ampdu_min_spacing = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para&IEEE80211_HT_CAP_AMPDU_DENSITY)>>2;

		if (support_short_GI(padapter, &(pmlmeinfo->HT_caps), CHANNEL_WIDTH_20))
			psta->htpriv.sgi_20m = true;

		if (support_short_GI(padapter, &(pmlmeinfo->HT_caps), CHANNEL_WIDTH_40))
			psta->htpriv.sgi_40m = true;

		psta->qos_option = true;

		psta->htpriv.ldpc_cap = pmlmepriv->htpriv.ldpc_cap;
		psta->htpriv.stbc_cap = pmlmepriv->htpriv.stbc_cap;
		psta->htpriv.beamform_cap = pmlmepriv->htpriv.beamform_cap;

		memcpy(&psta->htpriv.ht_cap, &pmlmeinfo->HT_caps, sizeof(struct rtw_ieee80211_ht_cap));
	} else{
		psta->htpriv.ht_option = false;

		psta->htpriv.ampdu_enable = false;

		psta->htpriv.sgi_20m = false;
		psta->htpriv.sgi_40m = false;
		psta->qos_option = false;

	}

	psta->htpriv.ch_offset = pmlmeext->cur_ch_offset;

	psta->htpriv.agg_enable_bitmap = 0x0;/* reset */
	psta->htpriv.candidate_tid_bitmap = 0x0;/* reset */

	psta->bw_mode = pmlmeext->cur_bwmode;

	/* QoS */
	if (pmlmepriv->qospriv.qos_option)
		psta->qos_option = true;

	update_ldpc_stbc_cap(psta);

	spin_lock_bh(&psta->lock);
	psta->state = _FW_LINKED;
	spin_unlock_bh(&psta->lock);

}

static void rtw_mlmeext_disconnect(struct adapter *padapter)
{
	struct mlme_priv 	*pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*pnetwork = (struct wlan_bssid_ex *)(&(pmlmeinfo->network));
	u8 state_backup = (pmlmeinfo->state&0x03);

	/* set_opmode_cmd(padapter, infra_client_with_mlme); */

	/*
	 * For safety, prevent from keeping macid sleep.
	 * If we can sure all power mode enter/leave are paired,
	 * this check can be removed.
	 * Lucas@20131113
	 */
	/* wakeup macid after disconnect. */
	{
		struct sta_info *psta;
		psta = rtw_get_stainfo(&padapter->stapriv, get_my_bssid(pnetwork));
		if (psta)
			rtw_hal_macid_wakeup(padapter, psta->mac_id);
	}

	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_DISCONNECT, NULL);
	rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, null_addr);

	/* set MSR to no link state -> infra. mode */
	Set_MSR(padapter, _HW_STATE_STATION_);

	pmlmeinfo->state = WIFI_FW_NULL_STATE;

	if (state_backup == WIFI_FW_STATION_STATE) {
		if (rtw_port_switch_chk(padapter)) {
			rtw_hal_set_hwreg(padapter, HW_VAR_PORT_SWITCH, NULL);
			{
				struct adapter *port0_iface = dvobj_get_port0_adapter(adapter_to_dvobj(padapter));
				if (port0_iface)
					rtw_lps_ctrl_wk_cmd(port0_iface, LPS_CTRL_CONNECT, 0);
			}
		}
	}

	/* switch to the 20M Hz mode after disconnect */
	pmlmeext->cur_bwmode = CHANNEL_WIDTH_20;
	pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	flush_all_cam_entry(padapter);

	del_timer_sync(&pmlmeext->link_timer);

	/* pmlmepriv->LinkDetectInfo.TrafficBusyState = false; */
	pmlmepriv->LinkDetectInfo.TrafficTransitionCount = 0;
	pmlmepriv->LinkDetectInfo.LowPowerTransitionCount = 0;

}

void mlmeext_joinbss_event_callback(struct adapter *padapter, int join_res)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*cur_network = &(pmlmeinfo->network);
	struct sta_priv 	*pstapriv = &padapter->stapriv;
	u8 join_type;
	struct sta_info *psta;
	if (join_res < 0) {
		join_type = 1;
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));
		rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, null_addr);

		goto exit_mlmeext_joinbss_event_callback;
	}

	if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
		/* update bc/mc sta_info */
		update_bmc_sta(padapter);


	/* turn on dynamic functions */
	Switch_DM_Func(padapter, DYNAMIC_ALL_FUNC_ENABLE, true);

	/*  update IOT-releated issue */
	update_IOT_info(padapter);

	rtw_hal_set_hwreg(padapter, HW_VAR_BASIC_RATE, cur_network->SupportedRates);

	/* BCN interval */
	rtw_hal_set_hwreg(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&pmlmeinfo->bcn_interval));

	/* udpate capability */
	update_capinfo(padapter, pmlmeinfo->capability);

	/* WMM, Update EDCA param */
	WMMOnAssocRsp(padapter);

	/* HT */
	HTOnAssocRsp(padapter);

	/* Set cur_channel&cur_bwmode&cur_ch_offset */
	set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	psta = rtw_get_stainfo(pstapriv, cur_network->MacAddress);
	if (psta) { /* only for infra. mode */

		pmlmeinfo->FW_sta_info[psta->mac_id].psta = psta;

		/* DBG_871X("set_sta_rate\n"); */

		psta->wireless_mode = pmlmeext->cur_wireless_mode;

		/* set per sta rate after updating HT cap. */
		set_sta_rate(padapter, psta);

		rtw_sta_media_status_rpt(padapter, psta, 1);

		/* wakeup macid after join bss successfully to ensure
			the subsequent data frames can be sent out normally */
		rtw_hal_macid_wakeup(padapter, psta->mac_id);
	}

	if (rtw_port_switch_chk(padapter))
		rtw_hal_set_hwreg(padapter, HW_VAR_PORT_SWITCH, NULL);

	join_type = 2;
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

	if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE) {
		/*  correcting TSF */
		correct_TSF(padapter, pmlmeext);

		/* set_link_timer(pmlmeext, DISCONNECT_TO); */
	}

	if (get_iface_type(padapter) == IFACE_PORT0)
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_CONNECT, 0);

exit_mlmeext_joinbss_event_callback:

	DBG_871X("=>%s\n", __func__);

}

/* currently only adhoc mode will go here */
void mlmeext_sta_add_event_callback(struct adapter *padapter, struct sta_info *psta)
{
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 join_type;

	DBG_871X("%s\n", __func__);

	if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) {
		if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) { /* adhoc master or sta_count>1 */

			/* nothing to do */
		} else{ /* adhoc client */
			/* update TSF Value */
			/* update_TSF(pmlmeext, pframe, len); */

			/*  correcting TSF */
			correct_TSF(padapter, pmlmeext);

			/* start beacon */
			if (send_beacon(padapter) == _FAIL) {
				pmlmeinfo->FW_sta_info[psta->mac_id].status = 0;

				pmlmeinfo->state ^= WIFI_FW_ADHOC_STATE;

				return;
			}

			pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;

		}

		join_type = 2;
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));
	}

	pmlmeinfo->FW_sta_info[psta->mac_id].psta = psta;

	psta->bssratelen = rtw_get_rateset_len(pmlmeinfo->FW_sta_info[psta->mac_id].SupportedRates);
	memcpy(psta->bssrateset, pmlmeinfo->FW_sta_info[psta->mac_id].SupportedRates, psta->bssratelen);

	/* update adhoc sta_info */
	update_sta_info(padapter, psta);

	rtw_hal_update_sta_rate_mask(padapter, psta);

	/*  ToDo: HT for Ad-hoc */
	psta->wireless_mode = rtw_check_network_type(psta->bssrateset, psta->bssratelen, pmlmeext->cur_channel);
	psta->raid = rtw_hal_networktype_to_raid(padapter, psta);

	/* rate radaptive */
	Update_RA_Entry(padapter, psta);
}

void mlmeext_sta_del_event_callback(struct adapter *padapter)
{
	if (is_client_associated_to_ap(padapter) || is_IBSS_empty(padapter))
		rtw_mlmeext_disconnect(padapter);
}

/****************************************************************************

Following are the functions for the timer handlers

*****************************************************************************/
void _linked_info_dump(struct adapter *padapter)
{
	int i;
	struct mlme_ext_priv    *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info    *pmlmeinfo = &(pmlmeext->mlmext_info);
	int UndecoratedSmoothedPWDB;
	struct dvobj_priv *pdvobj = adapter_to_dvobj(padapter);

	if (padapter->bLinkInfoDump) {

		DBG_871X("\n ============["ADPT_FMT"] linked status check ===================\n", ADPT_ARG(padapter));

		if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE) {
			rtw_hal_get_def_var(padapter, HAL_DEF_UNDERCORATEDSMOOTHEDPWDB, &UndecoratedSmoothedPWDB);

			DBG_871X("AP[" MAC_FMT "] - UndecoratedSmoothedPWDB:%d\n",
				MAC_ARG(padapter->mlmepriv.cur_network.network.MacAddress), UndecoratedSmoothedPWDB);
		} else if ((pmlmeinfo->state&0x03) == _HW_STATE_AP_) {
			struct list_head	*phead, *plist;

			struct sta_info *psta = NULL;
			struct sta_priv *pstapriv = &padapter->stapriv;

			spin_lock_bh(&pstapriv->asoc_list_lock);
			phead = &pstapriv->asoc_list;
			plist = get_next(phead);
			while (phead != plist) {
				psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
				plist = get_next(plist);

				DBG_871X("STA[" MAC_FMT "]:UndecoratedSmoothedPWDB:%d\n",
					MAC_ARG(psta->hwaddr), psta->rssi_stat.UndecoratedSmoothedPWDB);
			}
			spin_unlock_bh(&pstapriv->asoc_list_lock);

		}
		for (i = 0; i < NUM_STA; i++) {
			if (pdvobj->macid[i]) {
				if (i != 1) /* skip bc/mc sta */
					/*   tx info ============ */
					rtw_hal_get_def_var(padapter, HW_DEF_RA_INFO_DUMP, &i);
			}
		}
		rtw_hal_set_def_var(padapter, HAL_DEF_DBG_RX_INFO_DUMP, NULL);


	}


}

static u8 chk_ap_is_alive(struct adapter *padapter, struct sta_info *psta)
{
	u8 ret = false;

	#ifdef DBG_EXPIRATION_CHK
	DBG_871X(FUNC_ADPT_FMT" rx:"STA_PKTS_FMT", beacon:%llu, probersp_to_self:%llu"
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

	DBG_871X(FUNC_ADPT_FMT" tx_pkts:%llu, link_count:%u\n", FUNC_ADPT_ARG(padapter)
		, padapter->xmitpriv.tx_pkts
		, pmlmeinfo->link_count
	);
	#endif

	if ((sta_rx_data_pkts(psta) == sta_last_rx_data_pkts(psta))
		&& sta_rx_beacon_pkts(psta) == sta_last_rx_beacon_pkts(psta)
		&& sta_rx_probersp_pkts(psta) == sta_last_rx_probersp_pkts(psta)
	) {
		ret = false;
	} else{
		ret = true;
	}

	sta_update_last_rx_pkts(psta);

	return ret;
}

void linked_status_chk(struct adapter *padapter)
{
	u32 i;
	struct sta_info 	*psta;
	struct xmit_priv 	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv 	*pstapriv = &padapter->stapriv;


	if (is_client_associated_to_ap(padapter)) {
		/* linked infrastructure client mode */

		int tx_chk = _SUCCESS, rx_chk = _SUCCESS;
		int rx_chk_limit;
		int link_count_limit;

		#if defined(DBG_ROAMING_TEST)
		rx_chk_limit = 1;
		#else
		rx_chk_limit = 8;
		#endif
		link_count_limit = 7; /*  16 sec */

		/*  Marked by Kurt 20130715 */
		/*  For WiDi 3.5 and latered on, they don't ask WiDi sink to do roaming, so we could not check rx limit that strictly. */
		/*  todo: To check why we under miracast session, rx_chk would be false */
		/* ifdef CONFIG_INTEL_WIDI */
		/* if (padapter->mlmepriv.widi_state != INTEL_WIDI_STATE_NONE) */
		/* 	rx_chk_limit = 1; */
		/* endif */

		psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress);
		if (psta != NULL) {
			if (chk_ap_is_alive(padapter, psta) == false)
				rx_chk = _FAIL;

			if (pxmitpriv->last_tx_pkts == pxmitpriv->tx_pkts)
				tx_chk = _FAIL;

			{
				if (rx_chk != _SUCCESS) {
					if (pmlmeext->retry == 0) {
						#ifdef DBG_EXPIRATION_CHK
						DBG_871X("issue_probereq to trigger probersp, retry =%d\n", pmlmeext->retry);
						#endif
						issue_probereq_ex(padapter, &pmlmeinfo->network.Ssid, pmlmeinfo->network.MacAddress, 0, 0, 0, 0);
						issue_probereq_ex(padapter, &pmlmeinfo->network.Ssid, pmlmeinfo->network.MacAddress, 0, 0, 0, 0);
						issue_probereq_ex(padapter, &pmlmeinfo->network.Ssid, pmlmeinfo->network.MacAddress, 0, 0, 0, 0);
					}
				}

				if (tx_chk != _SUCCESS && pmlmeinfo->link_count++ == link_count_limit) {
					#ifdef DBG_EXPIRATION_CHK
					DBG_871X("%s issue_nulldata 0\n", __func__);
					#endif
					tx_chk = issue_nulldata_in_interrupt(padapter, NULL);
				}
			}

			if (rx_chk == _FAIL) {
				pmlmeext->retry++;
				if (pmlmeext->retry > rx_chk_limit) {
					DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" disconnect or roaming\n",
						FUNC_ADPT_ARG(padapter));
					receive_disconnect(padapter, pmlmeinfo->network.MacAddress
						, WLAN_REASON_EXPIRATION_CHK);
					return;
				}
			} else {
				pmlmeext->retry = 0;
			}

			if (tx_chk == _FAIL) {
				pmlmeinfo->link_count %= (link_count_limit+1);
			} else {
				pxmitpriv->last_tx_pkts = pxmitpriv->tx_pkts;
				pmlmeinfo->link_count = 0;
			}

		} /* end of if ((psta = rtw_get_stainfo(pstapriv, passoc_res->network.MacAddress)) != NULL) */
	} else if (is_client_associated_to_ibss(padapter)) {
		/* linked IBSS mode */
		/* for each assoc list entry to check the rx pkt counter */
		for (i = IBSS_START_MAC_ID; i < NUM_STA; i++) {
			if (pmlmeinfo->FW_sta_info[i].status == 1) {
				psta = pmlmeinfo->FW_sta_info[i].psta;

				if (NULL == psta)
					continue;

				if (pmlmeinfo->FW_sta_info[i].rx_pkt == sta_rx_pkts(psta)) {

					if (pmlmeinfo->FW_sta_info[i].retry < 3) {
						pmlmeinfo->FW_sta_info[i].retry++;
					} else{
						pmlmeinfo->FW_sta_info[i].retry = 0;
						pmlmeinfo->FW_sta_info[i].status = 0;
						report_del_sta_event(padapter, psta->hwaddr
							, 65535/*  indicate disconnect caused by no rx */
						);
					}
				} else{
					pmlmeinfo->FW_sta_info[i].retry = 0;
					pmlmeinfo->FW_sta_info[i].rx_pkt = (u32)sta_rx_pkts(psta);
				}
			}
		}

		/* set_link_timer(pmlmeext, DISCONNECT_TO); */

	}

}

void survey_timer_hdl(struct timer_list *t)
{
	struct adapter *padapter =
		from_timer(padapter, t, mlmeextpriv.survey_timer);
	struct cmd_obj	*ph2c;
	struct sitesurvey_parm	*psurveyPara;
	struct cmd_priv 				*pcmdpriv = &padapter->cmdpriv;
	struct mlme_ext_priv 	*pmlmeext = &padapter->mlmeextpriv;

	/* DBG_871X("marc: survey timer\n"); */

	/* issue rtw_sitesurvey_cmd */
	if (pmlmeext->sitesurvey_res.state > SCAN_START) {
		if (pmlmeext->sitesurvey_res.state ==  SCAN_PROCESS) {
			pmlmeext->sitesurvey_res.channel_idx++;
		}

		if (pmlmeext->scan_abort) {
			{
				pmlmeext->sitesurvey_res.channel_idx = pmlmeext->sitesurvey_res.ch_num;
				DBG_871X("%s idx:%d\n", __func__
					, pmlmeext->sitesurvey_res.channel_idx
				);
			}

			pmlmeext->scan_abort = false;/* reset */
		}

		ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
		if (ph2c == NULL) {
			goto exit_survey_timer_hdl;
		}

		psurveyPara = rtw_zmalloc(sizeof(struct sitesurvey_parm));
		if (psurveyPara == NULL) {
			kfree((unsigned char *)ph2c);
			goto exit_survey_timer_hdl;
		}

		init_h2fwcmd_w_parm_no_rsp(ph2c, psurveyPara, GEN_CMD_CODE(_SiteSurvey));
		rtw_enqueue_cmd(pcmdpriv, ph2c);
	}


exit_survey_timer_hdl:

	return;
}

void link_timer_hdl(struct timer_list *t)
{
	struct adapter *padapter =
		from_timer(padapter, t, mlmeextpriv.link_timer);
	/* static unsigned int		rx_pkt = 0; */
	/* static u64				tx_cnt = 0; */
	/* struct xmit_priv 	*pxmitpriv = &(padapter->xmitpriv); */
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	/* struct sta_priv 	*pstapriv = &padapter->stapriv; */


	if (pmlmeinfo->state & WIFI_FW_AUTH_NULL) {
		DBG_871X("link_timer_hdl:no beacon while connecting\n");
		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		report_join_res(padapter, -3);
	} else if (pmlmeinfo->state & WIFI_FW_AUTH_STATE) {
		/* re-auth timer */
		if (++pmlmeinfo->reauth_count > REAUTH_LIMIT) {
			/* if (pmlmeinfo->auth_algo != dot11AuthAlgrthm_Auto) */
			/*  */
				pmlmeinfo->state = 0;
				report_join_res(padapter, -1);
				return;
			/*  */
			/* else */
			/*  */
			/* 	pmlmeinfo->auth_algo = dot11AuthAlgrthm_Shared; */
			/* 	pmlmeinfo->reauth_count = 0; */
			/*  */
		}

		DBG_871X("link_timer_hdl: auth timeout and try again\n");
		pmlmeinfo->auth_seq = 1;
		issue_auth(padapter, NULL, 0);
		set_link_timer(pmlmeext, REAUTH_TO);
	} else if (pmlmeinfo->state & WIFI_FW_ASSOC_STATE) {
		/* re-assoc timer */
		if (++pmlmeinfo->reassoc_count > REASSOC_LIMIT) {
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
			report_join_res(padapter, -2);
			return;
		}

		DBG_871X("link_timer_hdl: assoc timeout and try again\n");
		issue_assocreq(padapter);
		set_link_timer(pmlmeext, REASSOC_TO);
	}

	return;
}

void addba_timer_hdl(struct timer_list *t)
{
	struct sta_info *psta = from_timer(psta, t, addba_retry_timer);
	struct ht_priv *phtpriv;

	if (!psta)
		return;

	phtpriv = &psta->htpriv;

	if (phtpriv->ht_option && phtpriv->ampdu_enable) {
		if (phtpriv->candidate_tid_bitmap)
			phtpriv->candidate_tid_bitmap = 0x0;

	}
}

void sa_query_timer_hdl(struct timer_list *t)
{
	struct adapter *padapter =
		from_timer(padapter, t, mlmeextpriv.sa_query_timer);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	/* disconnect */
	spin_lock_bh(&pmlmepriv->lock);

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		rtw_disassoc_cmd(padapter, 0, true);
		rtw_indicate_disconnect(padapter);
		rtw_free_assoc_resources(padapter, 1);
	}

	spin_unlock_bh(&pmlmepriv->lock);
	DBG_871X("SA query timeout disconnect\n");
}

u8 NULL_hdl(struct adapter *padapter, u8 *pbuf)
{
	return H2C_SUCCESS;
}

#ifdef CONFIG_AUTO_AP_MODE
static int rtw_auto_ap_start_beacon(struct adapter *adapter)
{
	int ret = 0;
	u8 *pbuf = NULL;
	uint len;
	u8 supportRate[16];
	int	sz = 0, rateLen;
	u8 *ie;
	u8 wireless_mode, oper_channel;
	u8 ssid[3] = {0}; /* hidden ssid */
	u32 ssid_len = sizeof(ssid);
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);


	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
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
	*(u16 *)ie = cpu_to_le16((u16)100);/* BCN_INTERVAL = 100; */
	sz += 2;
	ie += 2;

	/* capability info */
	*(u16 *)ie = 0;
	*(u16 *)ie |= cpu_to_le16(cap_ESS);
	*(u16 *)ie |= cpu_to_le16(cap_ShortPremble);
	/* u16*)ie |= cpu_to_le16(cap_Privacy); */
	sz += 2;
	ie += 2;

	/* SSID */
	ie = rtw_set_ie(ie, _SSID_IE_, ssid_len, ssid, &sz);

	/* supported rates */
	wireless_mode = WIRELESS_11BG_24N;
	rtw_set_supported_rate(supportRate, wireless_mode);
	rateLen = rtw_get_rateset_len(supportRate);
	if (rateLen > 8) {
		ie = rtw_set_ie(ie, _SUPPORTEDRATES_IE_, 8, supportRate, &sz);
	} else{
		ie = rtw_set_ie(ie, _SUPPORTEDRATES_IE_, rateLen, supportRate, &sz);
	}


	/* DS parameter set */
	if (check_buddy_fwstate(adapter, _FW_LINKED) &&
		check_buddy_fwstate(adapter, WIFI_STATION_STATE)) {
		struct adapter *pbuddystruct adapter = adapter->pbuddystruct adapter;
		struct mlme_ext_priv *pbuddy_mlmeext  = &pbuddystruct adapter->mlmeextpriv;

		oper_channel = pbuddy_mlmeext->cur_channel;
	} else{
		oper_channel = adapter_to_dvobj(adapter)->oper_channel;
	}
	ie = rtw_set_ie(ie, _DSSET_IE_, 1, &oper_channel, &sz);

	/* ext supported rates */
	if (rateLen > 8) {
		ie = rtw_set_ie(ie, _EXT_SUPPORTEDRATES_IE_, (rateLen - 8), (supportRate + 8), &sz);
	}

	DBG_871X("%s, start auto ap beacon sz =%d\n", __func__, sz);

	/* lunch ap mode & start to issue beacon */
	if (rtw_check_beacon_data(adapter, pbuf,  sz) == _SUCCESS) {

	} else{
		ret = -EINVAL;
	}


	kfree(pbuf);

	return ret;

}
#endif/* CONFIG_AUTO_AP_MODE */

u8 setopmode_hdl(struct adapter *padapter, u8 *pbuf)
{
	u8 type;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct setopmode_parm *psetop = (struct setopmode_parm *)pbuf;

	if (psetop->mode == Ndis802_11APMode) {
		pmlmeinfo->state = WIFI_FW_AP_STATE;
		type = _HW_STATE_AP_;
		/* start_ap_mode(padapter); */
	} else if (psetop->mode == Ndis802_11Infrastructure) {
		pmlmeinfo->state &= ~(BIT(0)|BIT(1));/*  clear state */
		pmlmeinfo->state |= WIFI_FW_STATION_STATE;/* set to	STATION_STATE */
		type = _HW_STATE_STATION_;
	} else if (psetop->mode == Ndis802_11IBSS) {
		type = _HW_STATE_ADHOC_;
	} else{
		type = _HW_STATE_NOLINK_;
	}

	rtw_hal_set_hwreg(padapter, HW_VAR_SET_OPMODE, (u8 *)(&type));
	/* Set_NETYPE0_MSR(padapter, type); */


#ifdef CONFIG_AUTO_AP_MODE
	if (psetop->mode == Ndis802_11APMode)
		rtw_auto_ap_start_beacon(padapter);
#endif

	if (rtw_port_switch_chk(padapter)) {
		rtw_hal_set_hwreg(padapter, HW_VAR_PORT_SWITCH, NULL);

		if (psetop->mode == Ndis802_11APMode)
			adapter_to_pwrctl(padapter)->fw_psmode_iface_id = 0xff; /* ap mode won't dowload rsvd pages */
		else if (psetop->mode == Ndis802_11Infrastructure) {
			struct adapter *port0_iface = dvobj_get_port0_adapter(adapter_to_dvobj(padapter));
			if (port0_iface)
				rtw_lps_ctrl_wk_cmd(port0_iface, LPS_CTRL_CONNECT, 0);
		}
	}

	if (psetop->mode == Ndis802_11APMode) {
		/*  Do this after port switch to */
		/*  prevent from downloading rsvd page to wrong port */
		rtw_btcoex_MediaStatusNotify(padapter, 1); /* connect */
	}

	return H2C_SUCCESS;

}

u8 createbss_hdl(struct adapter *padapter, u8 *pbuf)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex	*pnetwork = (struct wlan_bssid_ex *)(&(pmlmeinfo->network));
	struct joinbss_parm *pparm = (struct joinbss_parm *)pbuf;
	/* u32 initialgain; */

	if (pmlmeinfo->state == WIFI_FW_AP_STATE) {
		struct wlan_bssid_ex *network = &padapter->mlmepriv.cur_network.network;
		start_bss_network(padapter, (u8 *)network);
		return H2C_SUCCESS;
	}

	/* below is for ad-hoc master */
	if (pparm->network.InfrastructureMode == Ndis802_11IBSS) {
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

		/* disable dynamic functions, such as high power, DIG */
		Save_DM_Func_Flag(padapter);
		Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, false);

		/* config the initial gain under linking, need to write the BB registers */
		/* initialgain = 0x1E; */
		/* rtw_hal_set_hwreg(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain)); */

		/* cancel link timer */
		del_timer_sync(&pmlmeext->link_timer);

		/* clear CAM */
		flush_all_cam_entry(padapter);

		memcpy(pnetwork, pbuf, FIELD_OFFSET(struct wlan_bssid_ex, IELength));
		pnetwork->IELength = ((struct wlan_bssid_ex *)pbuf)->IELength;

		if (pnetwork->IELength > MAX_IE_SZ)/* Check pbuf->IELength */
			return H2C_PARAMETERS_ERROR;

		memcpy(pnetwork->IEs, ((struct wlan_bssid_ex *)pbuf)->IEs, pnetwork->IELength);

		start_create_ibss(padapter);

	}

	return H2C_SUCCESS;

}

u8 join_cmd_hdl(struct adapter *padapter, u8 *pbuf)
{
	u8 join_type;
	struct ndis_80211_var_ie *pIE;
	struct registry_priv *pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*pnetwork = (struct wlan_bssid_ex *)(&(pmlmeinfo->network));
	u32 i;
	u8 cbw40_enable = 0;
	/* u32 initialgain; */
	/* u32 acparm; */
	u8 ch, bw, offset;

	/* check already connecting to AP or not */
	if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) {
		if (pmlmeinfo->state & WIFI_FW_STATION_STATE) {
			issue_deauth_ex(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING, 1, 100);
		}
		pmlmeinfo->state = WIFI_FW_NULL_STATE;

		/* clear CAM */
		flush_all_cam_entry(padapter);

		del_timer_sync(&pmlmeext->link_timer);

		/* set MSR to nolink -> infra. mode */
		/* Set_MSR(padapter, _HW_STATE_NOLINK_); */
		Set_MSR(padapter, _HW_STATE_STATION_);


		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_DISCONNECT, NULL);
	}

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
	pmlmeinfo->bwmode_updated = false;
	/* pmlmeinfo->assoc_AP_vendor = HT_IOT_PEER_MAX; */
	pmlmeinfo->VHT_enable = 0;

	memcpy(pnetwork, pbuf, FIELD_OFFSET(struct wlan_bssid_ex, IELength));
	pnetwork->IELength = ((struct wlan_bssid_ex *)pbuf)->IELength;

	if (pnetwork->IELength > MAX_IE_SZ)/* Check pbuf->IELength */
		return H2C_PARAMETERS_ERROR;

	memcpy(pnetwork->IEs, ((struct wlan_bssid_ex *)pbuf)->IEs, pnetwork->IELength);

	pmlmeext->cur_channel = (u8)pnetwork->Configuration.DSConfig;
	pmlmeinfo->bcn_interval = get_beacon_interval(pnetwork);

	/* Check AP vendor to move rtw_joinbss_cmd() */
	/* pmlmeinfo->assoc_AP_vendor = check_assoc_AP(pnetwork->IEs, pnetwork->IELength); */

	/* sizeof(struct ndis_802_11_fix_ie) */
	for (i = _FIXED_IE_LENGTH_; i < pnetwork->IELength;) {
		pIE = (struct ndis_80211_var_ie *)(pnetwork->IEs + i);

		switch (pIE->ElementID) {
		case _VENDOR_SPECIFIC_IE_:/* Get WMM IE. */
			if (!memcmp(pIE->data, WMM_OUI, 4))
				WMM_param_handler(padapter, pIE);
			break;

		case _HT_CAPABILITY_IE_:	/* Get HT Cap IE. */
			pmlmeinfo->HT_caps_enable = 1;
			break;

		case _HT_EXTRA_INFO_IE_:	/* Get HT Info IE. */
			pmlmeinfo->HT_info_enable = 1;

			/* spec case only for cisco's ap because cisco's ap issue assoc rsp using mcs rate @40MHz or @20MHz */
			{
				struct HT_info_element *pht_info = (struct HT_info_element *)(pIE->data);

				if (pnetwork->Configuration.DSConfig > 14) {
					if ((pregpriv->bw_mode >> 4) > CHANNEL_WIDTH_20)
						cbw40_enable = 1;
				} else {
					if ((pregpriv->bw_mode & 0x0f) > CHANNEL_WIDTH_20)
						cbw40_enable = 1;
				}

				if ((cbw40_enable) && (pht_info->infos[0] & BIT(2))) {
					/* switch to the 40M Hz mode according to the AP */
					pmlmeext->cur_bwmode = CHANNEL_WIDTH_40;
					switch (pht_info->infos[0] & 0x3) {
					case 1:
						pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
						break;

					case 3:
						pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
						break;

					default:
						pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
						pmlmeext->cur_bwmode = CHANNEL_WIDTH_20;
						break;
					}

					DBG_871X("set HT ch/bw before connected\n");
				}
			}
			break;
		default:
			break;
		}

		i += (pIE->Length + 2);
	}

	/* check channel, bandwidth, offset and switch */
	if (rtw_chk_start_clnt_join(padapter, &ch, &bw, &offset) == _FAIL) {
		report_join_res(padapter, (-4));
		return H2C_SUCCESS;
	}

	/* disable dynamic functions, such as high power, DIG */
	/* Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, false); */

	/* config the initial gain under linking, need to write the BB registers */
	/* initialgain = 0x1E; */
	/* rtw_hal_set_hwreg(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain)); */

	rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, pmlmeinfo->network.MacAddress);
	join_type = 0;
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));
	rtw_hal_set_hwreg(padapter, HW_VAR_DO_IQK, NULL);

	set_channel_bwmode(padapter, ch, offset, bw);

	/* cancel link timer */
	del_timer_sync(&pmlmeext->link_timer);

	start_clnt_join(padapter);

	return H2C_SUCCESS;

}

u8 disconnect_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	struct disconnect_parm *param = (struct disconnect_parm *)pbuf;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*pnetwork = (struct wlan_bssid_ex *)(&(pmlmeinfo->network));
	u8 val8;

	if (is_client_associated_to_ap(padapter)) {
			issue_deauth_ex(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING, param->deauth_timeout_ms/100, 100);
	}

	if (((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)) {
		/* Stop BCN */
		val8 = 0;
		rtw_hal_set_hwreg(padapter, HW_VAR_BCN_FUNC, (u8 *)(&val8));
	}

	rtw_mlmeext_disconnect(padapter);

	rtw_free_uc_swdec_pending_queue(padapter);

	return	H2C_SUCCESS;
}

static int rtw_scan_ch_decision(struct adapter *padapter, struct rtw_ieee80211_channel *out,
	u32 out_num, struct rtw_ieee80211_channel *in, u32 in_num)
{
	int i, j;
	int set_idx;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	/* clear first */
	memset(out, 0, sizeof(struct rtw_ieee80211_channel)*out_num);

	/* acquire channels from in */
	j = 0;
	for (i = 0; i < in_num; i++) {

		DBG_871X(FUNC_ADPT_FMT" "CHAN_FMT"\n", FUNC_ADPT_ARG(padapter), CHAN_ARG(&in[i]));

		set_idx = rtw_ch_set_search_ch(pmlmeext->channel_set, in[i].hw_value);
		if (in[i].hw_value && !(in[i].flags & RTW_IEEE80211_CHAN_DISABLED)
			&& set_idx >= 0
			&& rtw_mlme_band_check(padapter, in[i].hw_value)
		) {
			if (j >= out_num) {
				DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" out_num:%u not enough\n",
					FUNC_ADPT_ARG(padapter), out_num);
				break;
			}

			memcpy(&out[j], &in[i], sizeof(struct rtw_ieee80211_channel));

			if (pmlmeext->channel_set[set_idx].ScanType == SCAN_PASSIVE)
				out[j].flags |= RTW_IEEE80211_CHAN_PASSIVE_SCAN;

			j++;
		}
		if (j >= out_num)
			break;
	}

	/* if out is empty, use channel_set as default */
	if (j == 0) {
		for (i = 0; i < pmlmeext->max_chan_nums; i++) {

			DBG_871X(FUNC_ADPT_FMT" ch:%u\n", FUNC_ADPT_ARG(padapter), pmlmeext->channel_set[i].ChannelNum);

			if (rtw_mlme_band_check(padapter, pmlmeext->channel_set[i].ChannelNum)) {

				if (j >= out_num) {
					DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" out_num:%u not enough\n",
						FUNC_ADPT_ARG(padapter), out_num);
					break;
				}

				out[j].hw_value = pmlmeext->channel_set[i].ChannelNum;

				if (pmlmeext->channel_set[i].ScanType == SCAN_PASSIVE)
					out[j].flags |= RTW_IEEE80211_CHAN_PASSIVE_SCAN;

				j++;
			}
		}
	}

	return j;
}

u8 sitesurvey_cmd_hdl(struct adapter *padapter, u8 *pbuf)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct sitesurvey_parm	*pparm = (struct sitesurvey_parm *)pbuf;
	u8 bdelayscan = false;
	u8 val8;
	u32 initialgain;
	u32 i;

	if (pmlmeext->sitesurvey_res.state == SCAN_DISABLE) {
		pmlmeext->sitesurvey_res.state = SCAN_START;
		pmlmeext->sitesurvey_res.bss_cnt = 0;
		pmlmeext->sitesurvey_res.channel_idx = 0;

		for (i = 0; i < RTW_SSID_SCAN_AMOUNT; i++) {
			if (pparm->ssid[i].SsidLength) {
				memcpy(pmlmeext->sitesurvey_res.ssid[i].Ssid, pparm->ssid[i].Ssid, IW_ESSID_MAX_SIZE);
				pmlmeext->sitesurvey_res.ssid[i].SsidLength = pparm->ssid[i].SsidLength;
			} else {
				pmlmeext->sitesurvey_res.ssid[i].SsidLength = 0;
			}
		}

		pmlmeext->sitesurvey_res.ch_num = rtw_scan_ch_decision(padapter
			, pmlmeext->sitesurvey_res.ch, RTW_CHANNEL_SCAN_AMOUNT
			, pparm->ch, pparm->ch_num
		);

		pmlmeext->sitesurvey_res.scan_mode = pparm->scan_mode;

		/* issue null data if associating to the AP */
		if (is_client_associated_to_ap(padapter)) {
			pmlmeext->sitesurvey_res.state = SCAN_TXNULL;

			issue_nulldata(padapter, NULL, 1, 3, 500);

			bdelayscan = true;
		}
		if (bdelayscan) {
			/* delay 50ms to protect nulldata(1). */
			set_survey_timer(pmlmeext, 50);
			return H2C_SUCCESS;
		}
	}

	if ((pmlmeext->sitesurvey_res.state == SCAN_START) || (pmlmeext->sitesurvey_res.state == SCAN_TXNULL)) {
		/* disable dynamic functions, such as high power, DIG */
		Save_DM_Func_Flag(padapter);
		Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, false);

		/* config the initial gain under scaning, need to write the BB registers */
		initialgain = 0x1e;

		rtw_hal_set_hwreg(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));

		/* set MSR to no link state */
		Set_MSR(padapter, _HW_STATE_NOLINK_);

		val8 = 1; /* under site survey */
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

		pmlmeext->sitesurvey_res.state = SCAN_PROCESS;
	}

	site_survey(padapter);

	return H2C_SUCCESS;

}

u8 setauth_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	struct setauth_parm		*pparm = (struct setauth_parm *)pbuf;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pparm->mode < 4) {
		pmlmeinfo->auth_algo = pparm->mode;
	}

	return	H2C_SUCCESS;
}

u8 setkey_hdl(struct adapter *padapter, u8 *pbuf)
{
	u16 ctrl = 0;
	s16 cam_id = 0;
	struct setkey_parm		*pparm = (struct setkey_parm *)pbuf;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	unsigned char null_addr[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	u8 *addr;

	/* main tx key for wep. */
	if (pparm->set_tx)
		pmlmeinfo->key_index = pparm->keyid;

	cam_id = rtw_camid_alloc(padapter, NULL, pparm->keyid);

	if (cam_id < 0) {
	} else {
		if (cam_id > 3) /* not default key, searched by A2 */
			addr = get_bssid(&padapter->mlmepriv);
		else
			addr = null_addr;

		ctrl = BIT(15) | BIT6 | ((pparm->algorithm) << 2) | pparm->keyid;
		write_cam(padapter, cam_id, ctrl, addr, pparm->key);
		DBG_871X_LEVEL(_drv_always_, "set group key camid:%d, addr:"MAC_FMT", kid:%d, type:%s\n"
			, cam_id, MAC_ARG(addr), pparm->keyid, security_type_str(pparm->algorithm));
	}

	if (cam_id >= 0 && cam_id <= 3)
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_DK_CFG, (u8 *)true);

	/* allow multicast packets to driver */
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_ON_RCR_AM, null_addr);

	return H2C_SUCCESS;
}

u8 set_stakey_hdl(struct adapter *padapter, u8 *pbuf)
{
	u16 ctrl = 0;
	s16 cam_id = 0;
	u8 ret = H2C_SUCCESS;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct set_stakey_parm	*pparm = (struct set_stakey_parm *)pbuf;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;

	if (pparm->algorithm == _NO_PRIVACY_)
		goto write_to_cam;

	psta = rtw_get_stainfo(pstapriv, pparm->addr);
	if (!psta) {
		DBG_871X_LEVEL(_drv_always_, "%s sta:"MAC_FMT" not found\n", __func__, MAC_ARG(pparm->addr));
		ret = H2C_REJECTED;
		goto exit;
	}

	pmlmeinfo->enc_algo = pparm->algorithm;
	cam_id = rtw_camid_alloc(padapter, psta, 0);
	if (cam_id < 0)
		goto exit;

write_to_cam:
	if (pparm->algorithm == _NO_PRIVACY_) {
		while ((cam_id = rtw_camid_search(padapter, pparm->addr, -1)) >= 0) {
			DBG_871X_LEVEL(_drv_always_, "clear key for addr:"MAC_FMT", camid:%d\n", MAC_ARG(pparm->addr), cam_id);
			clear_cam_entry(padapter, cam_id);
			rtw_camid_free(padapter, cam_id);
		}
	} else {
		DBG_871X_LEVEL(_drv_always_, "set pairwise key camid:%d, addr:"MAC_FMT", kid:%d, type:%s\n",
			cam_id, MAC_ARG(pparm->addr), pparm->keyid, security_type_str(pparm->algorithm));
		ctrl = BIT(15) | ((pparm->algorithm) << 2) | pparm->keyid;
		write_cam(padapter, cam_id, ctrl, pparm->addr, pparm->key);
	}
	ret = H2C_SUCCESS_RSP;

exit:
	return ret;
}

u8 add_ba_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	struct addBaReq_parm	*pparm = (struct addBaReq_parm *)pbuf;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	struct sta_info *psta = rtw_get_stainfo(&padapter->stapriv, pparm->addr);

	if (!psta)
		return	H2C_SUCCESS;

	if (((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && (pmlmeinfo->HT_enable)) ||
		((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)) {
		/* pmlmeinfo->ADDBA_retry_count = 0; */
		/* pmlmeinfo->candidate_tid_bitmap |= (0x1 << pparm->tid); */
		/* psta->htpriv.candidate_tid_bitmap |= BIT(pparm->tid); */
		issue_action_BA(padapter, pparm->addr, RTW_WLAN_ACTION_ADDBA_REQ, (u16)pparm->tid);
		/* _set_timer(&pmlmeext->ADDBA_timer, ADDBA_TO); */
		_set_timer(&psta->addba_retry_timer, ADDBA_TO);
	} else{
		psta->htpriv.candidate_tid_bitmap &= ~BIT(pparm->tid);
	}
	return	H2C_SUCCESS;
}


u8 chk_bmc_sleepq_cmd(struct adapter *padapter)
{
	struct cmd_obj *ph2c;
	struct cmd_priv *pcmdpriv = &(padapter->cmdpriv);
	u8 res = _SUCCESS;

	ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_parm_rsp(ph2c, GEN_CMD_CODE(_ChkBMCSleepq));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return res;
}

u8 set_tx_beacon_cmd(struct adapter *padapter)
{
	struct cmd_obj	*ph2c;
	struct Tx_Beacon_param	*ptxBeacon_parm;
	struct cmd_priv *pcmdpriv = &(padapter->cmdpriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 res = _SUCCESS;
	int len_diff = 0;

	ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	ptxBeacon_parm = rtw_zmalloc(sizeof(struct Tx_Beacon_param));
	if (ptxBeacon_parm == NULL) {
		kfree((unsigned char *)ph2c);
		res = _FAIL;
		goto exit;
	}

	memcpy(&(ptxBeacon_parm->network), &(pmlmeinfo->network), sizeof(struct wlan_bssid_ex));

	len_diff = update_hidden_ssid(
		ptxBeacon_parm->network.IEs+_BEACON_IE_OFFSET_
		, ptxBeacon_parm->network.IELength-_BEACON_IE_OFFSET_
		, pmlmeinfo->hidden_ssid_mode
	);
	ptxBeacon_parm->network.IELength += len_diff;

	init_h2fwcmd_w_parm_no_rsp(ph2c, ptxBeacon_parm, GEN_CMD_CODE(_TX_Beacon));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return res;
}


u8 mlme_evt_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	u8 evt_code, evt_seq;
	u16 evt_sz;
	uint	*peventbuf;
	void (*event_callback)(struct adapter *dev, u8 *pbuf);
	struct evt_priv *pevt_priv = &(padapter->evtpriv);

	if (pbuf == NULL)
		goto _abort_event_;

	peventbuf = (uint *)pbuf;
	evt_sz = (u16)(*peventbuf&0xffff);
	evt_seq = (u8)((*peventbuf>>24)&0x7f);
	evt_code = (u8)((*peventbuf>>16)&0xff);


	#ifdef CHECK_EVENT_SEQ
	/*  checking event sequence... */
	if (evt_seq != (atomic_read(&pevt_priv->event_seq) & 0x7f)) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_,
			 ("Event Seq Error! %d vs %d\n", (evt_seq & 0x7f),
			  (atomic_read(&pevt_priv->event_seq) & 0x7f)));

		pevt_priv->event_seq = (evt_seq+1)&0x7f;

		goto _abort_event_;
	}
	#endif

	/*  checking if event code is valid */
	if (evt_code >= MAX_C2HEVT) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("\nEvent Code(%d) mismatch!\n", evt_code));
		goto _abort_event_;
	}

	/*  checking if event size match the event parm size */
	if ((wlanevents[evt_code].parmsize != 0) &&
			(wlanevents[evt_code].parmsize != evt_sz)) {

		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("\nEvent(%d) Parm Size mismatch (%d vs %d)!\n",
			evt_code, wlanevents[evt_code].parmsize, evt_sz));
		goto _abort_event_;

	}

	atomic_inc(&pevt_priv->event_seq);

	peventbuf += 2;

	if (peventbuf) {
		event_callback = wlanevents[evt_code].event_callback;
		event_callback(padapter, (u8 *)peventbuf);

		pevt_priv->evt_done_cnt++;
	}


_abort_event_:


	return H2C_SUCCESS;

}

u8 h2c_msg_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	return H2C_SUCCESS;
}

u8 chk_bmc_sleepq_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	struct sta_info *psta_bmc;
	struct list_head	*xmitframe_plist, *xmitframe_phead;
	struct xmit_frame *pxmitframe = NULL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct sta_priv  *pstapriv = &padapter->stapriv;

	/* for BC/MC Frames */
	psta_bmc = rtw_get_bcmc_stainfo(padapter);
	if (!psta_bmc)
		return H2C_SUCCESS;

	if ((pstapriv->tim_bitmap&BIT(0)) && (psta_bmc->sleepq_len > 0)) {
		msleep(10);/*  10ms, ATIM(HIQ) Windows */

		/* spin_lock_bh(&psta_bmc->sleep_q.lock); */
		spin_lock_bh(&pxmitpriv->lock);

		xmitframe_phead = get_list_head(&psta_bmc->sleep_q);
		xmitframe_plist = get_next(xmitframe_phead);

		while (xmitframe_phead != xmitframe_plist) {
			pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

			xmitframe_plist = get_next(xmitframe_plist);

			list_del_init(&pxmitframe->list);

			psta_bmc->sleepq_len--;
			if (psta_bmc->sleepq_len > 0)
				pxmitframe->attrib.mdata = 1;
			else
				pxmitframe->attrib.mdata = 0;

			pxmitframe->attrib.triggered = 1;

			if (xmitframe_hiq_filter(pxmitframe))
				pxmitframe->attrib.qsel = 0x11;/* HIQ */

			rtw_hal_xmitframe_enqueue(padapter, pxmitframe);
		}

		/* spin_unlock_bh(&psta_bmc->sleep_q.lock); */
		spin_unlock_bh(&pxmitpriv->lock);

		/* check hi queue and bmc_sleepq */
		rtw_chk_hi_queue_cmd(padapter);
	}

	return H2C_SUCCESS;
}

u8 tx_beacon_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	if (send_beacon(padapter) == _FAIL) {
		DBG_871X("issue_beacon, fail!\n");
		return H2C_PARAMETERS_ERROR;
	}

	/* tx bc/mc frames after update TIM */
	chk_bmc_sleepq_hdl(padapter, NULL);

	return H2C_SUCCESS;
}

int rtw_chk_start_clnt_join(struct adapter *padapter, u8 *ch, u8 *bw, u8 *offset)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	unsigned char cur_ch = pmlmeext->cur_channel;
	unsigned char cur_bw = pmlmeext->cur_bwmode;
	unsigned char cur_ch_offset = pmlmeext->cur_ch_offset;
	bool connect_allow = true;

	if (!ch || !bw || !offset) {
		rtw_warn_on(1);
		connect_allow = false;
	}

	if (connect_allow) {
		DBG_871X("start_join_set_ch_bw: ch =%d, bwmode =%d, ch_offset =%d\n", cur_ch, cur_bw, cur_ch_offset);
		*ch = cur_ch;
		*bw = cur_bw;
		*offset = cur_ch_offset;
	}

	return connect_allow == true ? _SUCCESS : _FAIL;
}

/* Find union about ch, bw, ch_offset of all linked/linking interfaces */
int rtw_get_ch_setting_union(struct adapter *adapter, u8 *ch, u8 *bw, u8 *offset)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct adapter *iface;
	struct mlme_ext_priv *mlmeext;
	u8 ch_ret = 0;
	u8 bw_ret = CHANNEL_WIDTH_20;
	u8 offset_ret = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	if (ch)
		*ch = 0;
	if (bw)
		*bw = CHANNEL_WIDTH_20;
	if (offset)
		*offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	iface = dvobj->padapters;
	mlmeext = &iface->mlmeextpriv;

	if (!check_fwstate(&iface->mlmepriv, _FW_LINKED|_FW_UNDER_LINKING))
		return 0;

	ch_ret = mlmeext->cur_channel;
	bw_ret = mlmeext->cur_bwmode;
	offset_ret = mlmeext->cur_ch_offset;

	return 1;
}

u8 set_ch_hdl(struct adapter *padapter, u8 *pbuf)
{
	struct set_ch_parm *set_ch_parm;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	set_ch_parm = (struct set_ch_parm *)pbuf;

	DBG_871X(FUNC_NDEV_FMT" ch:%u, bw:%u, ch_offset:%u\n",
		FUNC_NDEV_ARG(padapter->pnetdev),
		set_ch_parm->ch, set_ch_parm->bw, set_ch_parm->ch_offset);

	pmlmeext->cur_channel = set_ch_parm->ch;
	pmlmeext->cur_ch_offset = set_ch_parm->ch_offset;
	pmlmeext->cur_bwmode = set_ch_parm->bw;

	set_channel_bwmode(padapter, set_ch_parm->ch, set_ch_parm->ch_offset, set_ch_parm->bw);

	return	H2C_SUCCESS;
}

u8 set_chplan_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	struct SetChannelPlan_param *setChannelPlan_param;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	setChannelPlan_param = (struct SetChannelPlan_param *)pbuf;

	pmlmeext->max_chan_nums = init_channel_set(padapter, setChannelPlan_param->channel_plan, pmlmeext->channel_set);
	init_channel_list(padapter, pmlmeext->channel_set, pmlmeext->max_chan_nums, &pmlmeext->channel_list);

	if ((padapter->rtw_wdev != NULL) && (padapter->rtw_wdev->wiphy)) {
		struct regulatory_request request;
		request.initiator = NL80211_REGDOM_SET_BY_DRIVER;
		rtw_reg_notifier(padapter->rtw_wdev->wiphy, &request);
	}

	return	H2C_SUCCESS;
}

u8 led_blink_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	struct LedBlink_param *ledBlink_param;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	ledBlink_param = (struct LedBlink_param *)pbuf;
	return	H2C_SUCCESS;
}

u8 set_csa_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	return	H2C_REJECTED;
}

/*  TDLS_ESTABLISHED	: write RCR DATA BIT */
/*  TDLS_CS_OFF		: go back to the channel linked with AP, terminating channel switch procedure */
/*  TDLS_INIT_CH_SEN	: init channel sensing, receive all data and mgnt frame */
/*  TDLS_DONE_CH_SEN: channel sensing and report candidate channel */
/*  TDLS_OFF_CH		: first time set channel to off channel */
/*  TDLS_BASE_CH		: go back tp the channel linked with AP when set base channel as target channel */
/*  TDLS_P_OFF_CH	: periodically go to off channel */
/*  TDLS_P_BASE_CH	: periodically go back to base channel */
/*  TDLS_RS_RCR		: restore RCR */
/*  TDLS_TEAR_STA	: free tdls sta */
u8 tdls_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	return H2C_REJECTED;
}

u8 run_in_thread_hdl(struct adapter *padapter, u8 *pbuf)
{
	struct RunInThread_param *p;


	if (NULL == pbuf)
		return H2C_PARAMETERS_ERROR;
	p = (struct RunInThread_param *)pbuf;

	if (p->func)
		p->func(p->context);

	return H2C_SUCCESS;
}
