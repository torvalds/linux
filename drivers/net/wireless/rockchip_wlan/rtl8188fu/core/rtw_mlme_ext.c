/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
#define _RTW_MLME_EXT_C_

#include <drv_types.h>
#ifdef CONFIG_IOCTL_CFG80211
#include <rtw_wifi_regd.h>
#endif //CONFIG_IOCTL_CFG80211
#include <hal_data.h>


struct mlme_handler mlme_sta_tbl[]={
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
	{WIFI_ACTION_NOACK,"OnActionNoAck",	&OnAction},
};

#ifdef _CONFIG_NATIVEAP_MLME_
struct mlme_handler mlme_ap_tbl[]={
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
	{WIFI_ACTION_NOACK,"OnActionNoAck",	&OnAction},
};
#endif

struct action_handler OnAction_tbl[]={
	{RTW_WLAN_CATEGORY_SPECTRUM_MGMT,	 "ACTION_SPECTRUM_MGMT", on_action_spct},
	{RTW_WLAN_CATEGORY_QOS, "ACTION_QOS", &OnAction_qos},
	{RTW_WLAN_CATEGORY_DLS, "ACTION_DLS", &OnAction_dls},
	{RTW_WLAN_CATEGORY_BACK, "ACTION_BACK", &OnAction_back},
	{RTW_WLAN_CATEGORY_PUBLIC, "ACTION_PUBLIC", on_action_public},
	{RTW_WLAN_CATEGORY_RADIO_MEASUREMENT, "ACTION_RADIO_MEASUREMENT", &DoReserved},
	{RTW_WLAN_CATEGORY_FT, "ACTION_FT",	&DoReserved},
	{RTW_WLAN_CATEGORY_HT,	"ACTION_HT",	&OnAction_ht},
#ifdef CONFIG_IEEE80211W
	{RTW_WLAN_CATEGORY_SA_QUERY, "ACTION_SA_QUERY", &OnAction_sa_query},
#else
	{RTW_WLAN_CATEGORY_SA_QUERY, "ACTION_SA_QUERY", &DoReserved},
#endif //CONFIG_IEEE80211W
	//add for CONFIG_IEEE80211W
	{RTW_WLAN_CATEGORY_UNPROTECTED_WNM, "ACTION_UNPROTECTED_WNM", &DoReserved},
	{RTW_WLAN_CATEGORY_SELF_PROTECTED, "ACTION_SELF_PROTECTED", &DoReserved},
	{RTW_WLAN_CATEGORY_WMM, "ACTION_WMM", &OnAction_wmm},
	{RTW_WLAN_CATEGORY_VHT, "ACTION_VHT", &OnAction_vht},
	{RTW_WLAN_CATEGORY_P2P, "ACTION_P2P", &OnAction_p2p},	
};


u8	null_addr[ETH_ALEN]= {0,0,0,0,0,0};

/**************************************************
OUI definitions for the vendor specific IE
***************************************************/
unsigned char	RTW_WPA_OUI[] = {0x00, 0x50, 0xf2, 0x01};
unsigned char WMM_OUI[] = {0x00, 0x50, 0xf2, 0x02};
unsigned char	WPS_OUI[] = {0x00, 0x50, 0xf2, 0x04};
unsigned char	P2P_OUI[] = {0x50,0x6F,0x9A,0x09};
unsigned char	WFD_OUI[] = {0x50,0x6F,0x9A,0x0A};

unsigned char	WMM_INFO_OUI[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01};
unsigned char	WMM_PARA_OUI[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};

unsigned char WPA_TKIP_CIPHER[4] = {0x00, 0x50, 0xf2, 0x02};
unsigned char RSN_TKIP_CIPHER[4] = {0x00, 0x0f, 0xac, 0x02};

extern unsigned char REALTEK_96B_IE[];

#ifdef LEGACY_CHANNEL_PLAN_REF
/********************************************************
ChannelPlan definitions
*********************************************************/
static RT_CHANNEL_PLAN legacy_channel_plan[] = {
	/* 0x00, RTW_CHPLAN_FCC */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165}, 32},
	/* 0x01, RTW_CHPLAN_IC */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 31},
	/* 0x02, RTW_CHPLAN_ETSI */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 32},
	/* 0x03, RTW_CHPLAN_SPAIN */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	/* 0x04, RTW_CHPLAN_FRANCE */					{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	/* 0x05, RTW_CHPLAN_MKK */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	/* 0x06, RTW_CHPLAN_MKK1 */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	/* 0x07, RTW_CHPLAN_ISRAEL */					{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48, 52, 56, 60, 64}, 21},
	/* 0x08, RTW_CHPLAN_TELEC */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 36, 40, 44, 48, 52, 56, 60, 64}, 22},
	/* 0x09, RTW_CHPLAN_GLOBAL_DOAMIN */			{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}, 14},
	/* 0x0A, RTW_CHPLAN_WORLD_WIDE_13 */			{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	/* 0x0B, RTW_CHPLAN_TAIWAN */					{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 26},
	/* 0x0C, RTW_CHPLAN_CHINA */					{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 149, 153, 157, 161, 165}, 18},
	/* 0x0D, RTW_CHPLAN_SINGAPORE_INDIA_MEXICO */	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165}, 24},
	/* 0x0E, RTW_CHPLAN_KOREA */					{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 149, 153, 157, 161, 165}, 31},
	/* 0x0F, RTW_CHPLAN_TURKEY */					{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 36, 40, 44, 48, 52, 56, 60, 64}, 19},
	/* 0x10, RTW_CHPLAN_JAPAN */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 32},
	/* 0x11, RTW_CHPLAN_FCC_NO_DFS */				{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 36, 40, 44, 48, 149, 153, 157, 161, 165}, 20},
	/* 0x12, RTW_CHPLAN_JAPAN_NO_DFS */				{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48}, 17},
	/* 0x13, RTW_CHPLAN_WORLD_WIDE_5G */			{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165}, 37},
	/* 0x14, RTW_CHPLAN_TAIWAN_NO_DFS */			{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 56, 60, 64, 149, 153, 157, 161, 165}, 19},
};
#endif

static RT_CHANNEL_PLAN_2G RTW_ChannelPlan2G[] = {
	/* 0, RTW_RD_2G_NULL */		{{}, 0},
	/* 1, RTW_RD_2G_WORLD */	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	/* 2, RTW_RD_2G_ETSI1 */		{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	/* 3, RTW_RD_2G_FCC1 */		{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, 11},
	/* 4, RTW_RD_2G_MKK1 */		{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}, 14},
	/* 5, RTW_RD_2G_ETSI2 */		{{10, 11, 12, 13}, 4},
	/* 6, RTW_RD_2G_GLOBAL */	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}, 14},
	/* 7, RTW_RD_2G_MKK2 */		{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	/* 8, RTW_RD_2G_FCC2 */		{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
};

static RT_CHANNEL_PLAN_5G RTW_ChannelPlan5G[] = {
	/* 0, RTW_RD_5G_NULL */		{{}, 0},
	/* 1, RTW_RD_5G_ETSI1 */		{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 19},
	/* 2, RTW_RD_5G_ETSI2 */		{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165}, 24},
	/* 3, RTW_RD_5G_ETSI3 */		{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 149, 153, 157, 161, 165}, 22},
	/* 4, RTW_RD_5G_FCC1 */		{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165}, 24},
	/* 5, RTW_RD_5G_FCC2 */		{{36, 40, 44, 48, 149, 153, 157, 161, 165}, 9},
	/* 6, RTW_RD_5G_FCC3 */		{{36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165}, 13},
	/* 7, RTW_RD_5G_FCC4 */		{{36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161}, 12},
	/* 8, RTW_RD_5G_FCC5 */		{{149, 153, 157, 161, 165}, 5},
	/* 9, RTW_RD_5G_FCC6 */		{{36, 40, 44, 48, 52, 56, 60, 64}, 8},
	/* 10, RTW_RD_5G_FCC7 */	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165}, 21},
	/* 11, RTW_RD_5G_KCC1 */	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 149, 153, 157, 161}, 19},
	/* 12, RTW_RD_5G_MKK1 */	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 19},
	/* 13, RTW_RD_5G_MKK2 */	{{36, 40, 44, 48, 52, 56, 60, 64}, 8},
	/* 14, RTW_RD_5G_MKK3 */	{{100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 11},
	/* 15, RTW_RD_5G_NCC1 */	{{56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165}, 16},
	/* 16, RTW_RD_5G_NCC2 */	{{56, 60, 64, 149, 153, 157, 161, 165}, 8},
	/* 17, RTW_RD_5G_NCC3 */	{{149, 153, 157, 161, 165}, 5},
	/* 18, RTW_RD_5G_ETSI4 */	{{36, 40, 44, 48}, 4},
	/* 19, RTW_RD_5G_ETSI5 */	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165}, 21},
	/* 20, RTW_RD_5G_FCC8 */	{{149, 153, 157, 161}, 4},
	/* 21, RTW_RD_5G_ETSI6 */	{{36, 40, 44, 48, 52, 56, 60, 64}, 8},
	/* 22, RTW_RD_5G_ETSI7 */	{{36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165}, 13},
	/* 23, RTW_RD_5G_ETSI8 */	{{36, 40, 44, 48, 149, 153, 157, 161, 165}, 9},
	/* 24, RTW_RD_5G_ETSI9 */	{{100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 11},
	/* 25, RTW_RD_5G_ETSI10 */	{{149, 153, 157, 161, 165}, 5},
	/* 26, RTW_RD_5G_ETSI11 */	{{36, 40, 44, 48, 52, 56, 60, 64, 132, 136, 140, 149, 153, 157, 161, 165}, 16},
	/* 27, RTW_RD_5G_NCC4 */	{{52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165}, 17},
	/* 28, RTW_RD_5G_ETSI12 */	{{149, 153, 157, 161}, 4},
	/* 29, RTW_RD_5G_FCC9 */	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165}, 21},
	/* 30, RTW_RD_5G_ETSI13 */	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140}, 16},
	/* 31, RTW_RD_5G_FCC10 */	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161}, 20},
	/* 32, RTW_RD_5G_MKK4 */	{{36, 40, 44, 48}, 4},
	/* 33, RTW_RD_5G_ETSI14 */	{{36, 40, 44, 48, 52, 56, 60, 64, 132, 136, 140}, 11},

	/* === Below are driver defined for legacy channel plan compatible, NO static index assigned ==== */
	/* RTW_RD_5G_OLD_FCC1 */	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 20},
	/* RTW_RD_5G_OLD_NCC1 */	{{56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 15},
	/* RTW_RD_5G_OLD_KCC1 */	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 149, 153, 157, 161, 165}, 20},
};

static RT_CHANNEL_PLAN_MAP	RTW_ChannelPlanMap[] = {
	/* ===== 0x00 ~ 0x1F, legacy channel plan ===== */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_KCC1,		TXPWR_LMT_FCC},		/* 0x00, RTW_CHPLAN_FCC */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_OLD_FCC1,	TXPWR_LMT_FCC},		/* 0x01, RTW_CHPLAN_IC */
	{RTW_RD_2G_ETSI1,	RTW_RD_5G_ETSI1,	TXPWR_LMT_ETSI},	/* 0x02, RTW_CHPLAN_ETSI */
	{RTW_RD_2G_ETSI1,	RTW_RD_5G_NULL,		TXPWR_LMT_ETSI},	/* 0x03, RTW_CHPLAN_SPAIN */
	{RTW_RD_2G_ETSI1,	RTW_RD_5G_NULL,		TXPWR_LMT_ETSI},	/* 0x04, RTW_CHPLAN_FRANCE */
	{RTW_RD_2G_MKK1,	RTW_RD_5G_NULL,		TXPWR_LMT_MKK},		/* 0x05, RTW_CHPLAN_MKK */
	{RTW_RD_2G_MKK1,	RTW_RD_5G_NULL,		TXPWR_LMT_MKK},		/* 0x06, RTW_CHPLAN_MKK1 */
	{RTW_RD_2G_ETSI1,	RTW_RD_5G_FCC6,		TXPWR_LMT_ETSI},	/* 0x07, RTW_CHPLAN_ISRAEL */
	{RTW_RD_2G_MKK1,	RTW_RD_5G_FCC6,		TXPWR_LMT_MKK},		/* 0x08, RTW_CHPLAN_TELEC */
	{RTW_RD_2G_MKK1,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x09, RTW_CHPLAN_GLOBAL_DOAMIN */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x0A, RTW_CHPLAN_WORLD_WIDE_13 */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_OLD_NCC1,	TXPWR_LMT_FCC},		/* 0x0B, RTW_CHPLAN_TAIWAN */
	{RTW_RD_2G_ETSI1,	RTW_RD_5G_FCC5,		TXPWR_LMT_ETSI},	/* 0x0C, RTW_CHPLAN_CHINA */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_FCC3,		TXPWR_LMT_WW},		/* 0x0D, RTW_CHPLAN_SINGAPORE_INDIA_MEXICO */ /* ETSI:Singapore, India. FCC:Mexico => WW */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_OLD_KCC1,	TXPWR_LMT_ETSI},	/* 0x0E, RTW_CHPLAN_KOREA */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_FCC6,		TXPWR_LMT_ETSI},	/* 0x0F, RTW_CHPLAN_TURKEY */
	{RTW_RD_2G_ETSI1,	RTW_RD_5G_ETSI1,	TXPWR_LMT_MKK},		/* 0x10, RTW_CHPLAN_JAPAN */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_FCC2,		TXPWR_LMT_FCC},		/* 0x11, RTW_CHPLAN_FCC_NO_DFS */
	{RTW_RD_2G_ETSI1,	RTW_RD_5G_FCC7,		TXPWR_LMT_MKK},		/* 0x12, RTW_CHPLAN_JAPAN_NO_DFS */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_FCC1,		TXPWR_LMT_WW},		/* 0x13, RTW_CHPLAN_WORLD_WIDE_5G */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_NCC2,		TXPWR_LMT_FCC},		/* 0x14, RTW_CHPLAN_TAIWAN_NO_DFS */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_FCC7,		TXPWR_LMT_ETSI},	/* 0x15, RTW_CHPLAN_ETSI_NO_DFS */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_NCC1,		TXPWR_LMT_ETSI},	/* 0x16, RTW_CHPLAN_KOREA_NO_DFS */
	{RTW_RD_2G_MKK1,	RTW_RD_5G_FCC7,		TXPWR_LMT_MKK},		/* 0x17, RTW_CHPLAN_JAPAN_NO_DFS */
	{RTW_RD_2G_NULL,	RTW_RD_5G_FCC5,		TXPWR_LMT_ETSI},	/* 0x18, RTW_CHPLAN_PAKISTAN_NO_DFS */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_FCC5,		TXPWR_LMT_FCC},		/* 0x19, RTW_CHPLAN_TAIWAN2_NO_DFS */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x1A, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x1B, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x1C, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x1D, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x1E, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_FCC1,		TXPWR_LMT_WW},		/* 0x1F, RTW_CHPLAN_WORLD_WIDE_ONLY_5G */

	/* ===== 0x20 ~ 0x7F, new channel plan ===== */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x20, RTW_CHPLAN_WORLD_NULL */
	{RTW_RD_2G_ETSI1,	RTW_RD_5G_NULL,		TXPWR_LMT_ETSI},	/* 0x21, RTW_CHPLAN_ETSI1_NULL */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_NULL,		TXPWR_LMT_FCC},		/* 0x22, RTW_CHPLAN_FCC1_NULL */
	{RTW_RD_2G_MKK1,	RTW_RD_5G_NULL,		TXPWR_LMT_MKK},		/* 0x23, RTW_CHPLAN_MKK1_NULL */
	{RTW_RD_2G_ETSI2,	RTW_RD_5G_NULL,		TXPWR_LMT_ETSI},	/* 0x24, RTW_CHPLAN_ETSI2_NULL */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_FCC1,		TXPWR_LMT_FCC},		/* 0x25, RTW_CHPLAN_FCC1_FCC1 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_ETSI1,	TXPWR_LMT_ETSI},	/* 0x26, RTW_CHPLAN_WORLD_ETSI1 */
	{RTW_RD_2G_MKK1,	RTW_RD_5G_MKK1,		TXPWR_LMT_MKK},		/* 0x27, RTW_CHPLAN_MKK1_MKK1 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_KCC1,		TXPWR_LMT_ETSI},	/* 0x28, RTW_CHPLAN_WORLD_KCC1 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_FCC2,		TXPWR_LMT_FCC},		/* 0x29, RTW_CHPLAN_WORLD_FCC2 */
	{RTW_RD_2G_FCC2,	RTW_RD_5G_NULL,		TXPWR_LMT_FCC},		/* 0x2A, RTW_CHPLAN_FCC2_NULL */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x2B, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x2C, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x2D, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x2E, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x2F, */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_FCC3,		TXPWR_LMT_FCC},		/* 0x30, RTW_CHPLAN_WORLD_FCC3 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_FCC4,		TXPWR_LMT_FCC},		/* 0x31, RTW_CHPLAN_WORLD_FCC4 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_FCC5,		TXPWR_LMT_FCC},		/* 0x32, RTW_CHPLAN_WORLD_FCC5 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_FCC6,		TXPWR_LMT_FCC},		/* 0x33, RTW_CHPLAN_WORLD_FCC6 */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_FCC7,		TXPWR_LMT_FCC},		/* 0x34, RTW_CHPLAN_FCC1_FCC7 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_ETSI2,	TXPWR_LMT_ETSI},	/* 0x35, RTW_CHPLAN_WORLD_ETSI2 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_ETSI3,	TXPWR_LMT_ETSI},	/* 0x36, RTW_CHPLAN_WORLD_ETSI3 */
	{RTW_RD_2G_MKK1,	RTW_RD_5G_MKK2,		TXPWR_LMT_MKK},		/* 0x37, RTW_CHPLAN_MKK1_MKK2 */
	{RTW_RD_2G_MKK1,	RTW_RD_5G_MKK3,		TXPWR_LMT_MKK},		/* 0x38, RTW_CHPLAN_MKK1_MKK3 */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_NCC1,		TXPWR_LMT_FCC},		/* 0x39, RTW_CHPLAN_FCC1_NCC1 */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x3A, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x3B, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x3C, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x3D, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x3E, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x3F, */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_NCC2,		TXPWR_LMT_FCC},		/* 0x40, RTW_CHPLAN_FCC1_NCC2 */
	{RTW_RD_2G_GLOBAL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x41, RTW_CHPLAN_GLOBAL_NULL */
	{RTW_RD_2G_ETSI1,	RTW_RD_5G_ETSI4,	TXPWR_LMT_ETSI},	/* 0x42, RTW_CHPLAN_ETSI1_ETSI4 */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_FCC2,		TXPWR_LMT_FCC},		/* 0x43, RTW_CHPLAN_FCC1_FCC2 */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_NCC3,		TXPWR_LMT_FCC},		/* 0x44, RTW_CHPLAN_FCC1_NCC3 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_ETSI5,	TXPWR_LMT_ETSI},	/* 0x45, RTW_CHPLAN_WORLD_ETSI5 */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_FCC8,		TXPWR_LMT_FCC},		/* 0x46, RTW_CHPLAN_FCC1_FCC8 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_ETSI6,	TXPWR_LMT_ETSI},	/* 0x47, RTW_CHPLAN_WORLD_ETSI6 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_ETSI7,	TXPWR_LMT_ETSI},	/* 0x48, RTW_CHPLAN_WORLD_ETSI7 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_ETSI8,	TXPWR_LMT_ETSI},	/* 0x49, RTW_CHPLAN_WORLD_ETSI8 */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x4A, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x4B, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x4C, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x4D, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x4E, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x4F, */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_ETSI9,	TXPWR_LMT_ETSI},	/* 0x50, RTW_CHPLAN_WORLD_ETSI9 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_ETSI10,	TXPWR_LMT_ETSI},	/* 0x51, RTW_CHPLAN_WORLD_ETSI10 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_ETSI11,	TXPWR_LMT_ETSI},	/* 0x52, RTW_CHPLAN_WORLD_ETSI11 */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_NCC4,		TXPWR_LMT_FCC},		/* 0x53, RTW_CHPLAN_FCC1_NCC4 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_ETSI12,	TXPWR_LMT_ETSI},	/* 0x54, RTW_CHPLAN_WORLD_ETSI12 */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_FCC9,		TXPWR_LMT_FCC},		/* 0x55, RTW_CHPLAN_FCC1_FCC9 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_ETSI13,	TXPWR_LMT_ETSI},	/* 0x56, RTW_CHPLAN_WORLD_ETSI13 */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_FCC10,	TXPWR_LMT_FCC},		/* 0x57, RTW_CHPLAN_FCC1_FCC10 */
	{RTW_RD_2G_MKK2,	RTW_RD_5G_MKK4,		TXPWR_LMT_MKK},		/* 0x58, RTW_CHPLAN_MKK2_MKK4 */
	{RTW_RD_2G_WORLD,	RTW_RD_5G_ETSI14,	TXPWR_LMT_ETSI},	/* 0x59, RTW_CHPLAN_WORLD_ETSI14 */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x5A, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x5B, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x5C, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x5D, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x5E, */
	{RTW_RD_2G_NULL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW},		/* 0x5F, */
	{RTW_RD_2G_FCC1,	RTW_RD_5G_FCC5,		TXPWR_LMT_FCC},		/* 0x60, RTW_CHPLAN_FCC1_FCC5 */
};

static RT_CHANNEL_PLAN_MAP RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE = {
	RTW_RD_2G_WORLD,	RTW_RD_5G_FCC1,		TXPWR_LMT_FCC		/* 0x7F, Realtek Define */
};

bool rtw_chplan_is_empty(u8 id)
{
	RT_CHANNEL_PLAN_MAP *chplan_map;

	if (id == RTW_CHPLAN_REALTEK_DEFINE)
		chplan_map = &RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE;
	else
		chplan_map = &RTW_ChannelPlanMap[id];

	if (chplan_map->Index2G == RTW_RD_2G_NULL
		&& chplan_map->Index5G == RTW_RD_5G_NULL)
		return _TRUE;

	return _FALSE;
}

#ifdef CONFIG_DFS_MASTER
void rtw_rfctl_reset_cac(struct rf_ctl_t *rfctl)
{
	if (rtw_is_long_cac_ch(rfctl->radar_detect_ch, rfctl->radar_detect_bw, rfctl->radar_detect_offset))
		rfctl->cac_end_time = rtw_get_current_time() + rtw_ms_to_systime(CAC_TIME_CE_MS);
	else
		rfctl->cac_end_time = rtw_get_current_time() + rtw_ms_to_systime(CAC_TIME_MS);
}

/*
* check if channel coverage includes new range and the new range is in DFS range
* called after radar_detect_ch,bw,offset is updated
*/
bool rtw_is_cac_reset_needed(_adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	bool needed = _FALSE;
	u32 pre_hi, pre_lo, hi, lo;

	if (0)
		DBG_871X("pre_radar_detect_ch:%d, pre_radar_detect_by_sta_link:%d\n"
			, rfctl->pre_radar_detect_ch, rfctl->pre_radar_detect_by_sta_link);

	if (rfctl->pre_radar_detect_by_sta_link == _TRUE)
		goto exit;

	if (rfctl->pre_radar_detect_ch == 0) {
		needed = _TRUE;
		goto exit;
	}

	if (rtw_chbw_to_freq_range(rfctl->radar_detect_ch, rfctl->radar_detect_bw, rfctl->radar_detect_offset, &hi, &lo) == _FALSE)
		rtw_warn_on(1);
	if (rtw_chbw_to_freq_range(rfctl->pre_radar_detect_ch, rfctl->pre_radar_detect_bw, rfctl->pre_radar_detect_offset, &pre_hi, &pre_lo) == _FALSE)
		rtw_warn_on(1);

	if (!rtw_is_range_a_in_b(hi, lo, pre_hi, pre_lo)) {
		if (rtw_is_range_a_in_b(pre_hi, pre_lo, hi, lo)) {
			/* currrent is supper set of previous */
			if (rtw_is_dfs_range(hi, lo))
				needed = _TRUE;
		} else if (rtw_is_range_overlap(hi, lo, pre_hi, pre_lo)) {
			/* currrent is not supper set of previous, but has overlap */
			u32 new_hi, new_lo;

			if (lo < pre_lo) {
				new_hi = pre_lo;
				new_lo = lo;
				if (hi <= pre_lo || hi >= pre_hi) {
					DBG_871X_LEVEL(_drv_err_, "hi:%u, lo:%u, pre_hi:%u, pre_lo:%u\n"
						, hi, lo, pre_hi, pre_lo);
					rtw_warn_on(1);
					goto exit;
				}
			} else if (hi > pre_hi) {
				new_hi = hi;
				new_lo = pre_hi;
				if (lo >= pre_hi && lo <= pre_lo) {
					DBG_871X_LEVEL(_drv_err_, "hi:%u, lo:%u, pre_hi:%u, pre_lo:%u\n"
						, hi, lo, pre_hi, pre_lo);
					rtw_warn_on(1);
					goto exit;
				}
			} else {
				DBG_871X_LEVEL(_drv_err_, "hi:%u, lo:%u, pre_hi:%u, pre_lo:%u\n"
					, hi, lo, pre_hi, pre_lo);
				rtw_warn_on(1);
				goto exit;
			}

			if (rtw_is_dfs_range(new_hi, new_lo))
				needed = _TRUE;
			
		} else {
			/* no overlap */
			if (rtw_is_dfs_range(hi, lo))
				needed = _TRUE;
		}
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

bool rtw_rfctl_is_tx_blocked_by_cac(struct rf_ctl_t *rfctl)
{
	return (rtw_rfctl_overlap_radar_detect_ch(rfctl) && IS_UNDER_CAC(rfctl));
}

bool rtw_chset_is_ch_non_ocp(RT_CHANNEL_INFO *ch_set, u8 ch, u8 bw, u8 offset)
{
	bool ret = _FALSE;
	u32 hi = 0, lo = 0;
	int i;

	if (rtw_chbw_to_freq_range(ch, bw, offset, &hi, &lo) == _FALSE)
		goto exit;

	for (i = 0; ch_set[i].ChannelNum != 0; i++) {
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

/**
 * rtw_chset_update_non_ocp - update non_ocp_end_time according to the given @ch, @bw, @offset into @ch_set
 * @ch_set: the given channel set
 * @ch: channel number on which radar is detected
 * @bw: bandwidth on which radar is detected
 * @offset: bandwidth offset on which radar is detected
 * @ms: ms to add from now to update non_ocp_end_time, ms < 0 means use NON_OCP_TIME_MS
 */
static void _rtw_chset_update_non_ocp(RT_CHANNEL_INFO *ch_set, u8 ch, u8 bw, u8 offset, int ms)
{
	u32 hi = 0, lo = 0;
	int i;

	if (rtw_chbw_to_freq_range(ch, bw, offset, &hi, &lo) == _FALSE)
		goto exit;

	for (i = 0; ch_set[i].ChannelNum != 0; i++) {
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
		}
	}	

exit:
	return;
}

inline void rtw_chset_update_non_ocp(RT_CHANNEL_INFO *ch_set, u8 ch, u8 bw, u8 offset)
{
	_rtw_chset_update_non_ocp(ch_set, ch, bw, offset, -1);
}

inline void rtw_chset_update_non_ocp_ms(RT_CHANNEL_INFO *ch_set, u8 ch, u8 bw, u8 offset, int ms)
{
	_rtw_chset_update_non_ocp(ch_set, ch, bw, offset, ms);
}
#endif /* CONFIG_DFS_MASTER */

bool rtw_choose_available_chbw(_adapter *adapter, u8 req_bw, u8 *dec_ch, u8 *dec_bw, u8 *dec_offset, u8 d_flags)
{
	struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;
	int i;

	if (!dec_ch || !dec_bw || !dec_offset) {
		rtw_warn_on(1);
		return _FAIL;
	}

	for (i = 0; i < mlmeext->max_chan_nums; i++) {

		*dec_ch = mlmeext->channel_set[i].ChannelNum;
		*dec_bw = req_bw;
		if (*dec_bw == CHANNEL_WIDTH_20)
			*dec_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		else
			*dec_offset = rtw_get_offset_by_ch(*dec_ch);

		if ((d_flags & RTW_CHF_2G) && *dec_ch <= 14)
			continue;

		if ((d_flags & RTW_CHF_5G)  && *dec_ch > 14)
			continue;

		rtw_adjust_chbw(adapter, *dec_ch, dec_bw, dec_offset);

		if ((d_flags & RTW_CHF_DFS) && rtw_is_dfs_ch(*dec_ch, *dec_bw, *dec_offset))
			continue;

		if ((d_flags & RTW_CHF_LONG_CAC) && rtw_is_long_cac_ch(*dec_ch, *dec_bw, *dec_offset))
			continue;

		if ((d_flags & RTW_CHF_NON_DFS) && !rtw_is_dfs_ch(*dec_ch, *dec_bw, *dec_offset))
			continue;

		if ((d_flags & RTW_CHF_NON_LONG_CAC) && !rtw_is_long_cac_ch(*dec_ch, *dec_bw, *dec_offset))
			continue;

		if (!rtw_chset_is_ch_non_ocp(mlmeext->channel_set, *dec_ch, *dec_bw, *dec_offset))
			break;
	}

	return (i < mlmeext->max_chan_nums)?_TRUE:_FALSE;
}

void dump_country_chplan(void *sel, const struct country_chplan *ent)
{
	DBG_871X_SEL(sel, "\"%c%c\", 0x%02X%s\n"
		, ent->alpha2[0], ent->alpha2[1], ent->chplan
		, COUNTRY_CHPLAN_EN_11AC(ent) ? " ac" : ""
	);
}

void dump_country_chplan_map(void *sel)
{
	const struct country_chplan *ent;
	u8 code[2];

#if RTW_DEF_MODULE_REGULATORY_CERT
	DBG_871X_SEL(sel, "RTW_DEF_MODULE_REGULATORY_CERT:0x%x\n", RTW_DEF_MODULE_REGULATORY_CERT);
#endif
#ifdef CONFIG_CUSTOMIZED_COUNTRY_CHPLAN_MAP
	DBG_871X_SEL(sel, "CONFIG_CUSTOMIZED_COUNTRY_CHPLAN_MAP\n");
#endif

	for (code[0] = 'A'; code[0] <= 'Z'; code[0]++) {
		for (code[1] = 'A'; code[1] <= 'Z'; code[1]++) {
			ent = rtw_get_chplan_from_country(code);
			if (!ent)
				continue;

			dump_country_chplan(sel, ent);
		}
	}
}

void dump_chplan_id_list(void *sel)
{
	int i;

	for (i = 0; i < RTW_CHPLAN_MAX; i++) {
		if (!rtw_is_channel_plan_valid(i))
			continue;

		DBG_871X_SEL(sel, "0x%02X ", i);
	}

	DBG_871X_SEL_NL(sel, "0x7F\n");
}

void dump_chplan_test(void *sel)
{
	int i, j;

	/* check invalid channel */
	for (i = 0; i < RTW_RD_2G_MAX; i++) {
		for (j = 0; j < RTW_ChannelPlan2G[i].Len; j++) {
			if (rtw_ch2freq(RTW_ChannelPlan2G[i].Channel[j]) == 0)
				DBG_871X_SEL_NL(sel, "invalid ch:%u at (%d,%d)\n", RTW_ChannelPlan2G[i].Channel[j], i, j);
		}
	}

	for (i = 0; i < RTW_RD_5G_MAX; i++) {
		for (j = 0; j < RTW_ChannelPlan5G[i].Len; j++) {
			if (rtw_ch2freq(RTW_ChannelPlan5G[i].Channel[j]) == 0)
				DBG_871X_SEL_NL(sel, "invalid ch:%u at (%d,%d)\n", RTW_ChannelPlan5G[i].Channel[j], i, j);
		}
	}
}

void dump_chset(void *sel, RT_CHANNEL_INFO *ch_set)
{
	u8	i;

	for (i = 0; ch_set[i].ChannelNum != 0; i++) {
		DBG_871X_SEL_NL(sel, "ch:%3u, freq:%u, scan_type:%d"
			, ch_set[i].ChannelNum, rtw_ch2freq(ch_set[i].ChannelNum), ch_set[i].ScanType);
	
		#ifdef CONFIG_FIND_BEST_CHANNEL
		DBG_871X_SEL(sel, ", rx_count:%u", ch_set[i].rx_count);
		#endif
		
		#ifdef CONFIG_DFS_MASTER
		if (rtw_is_dfs_ch(ch_set[i].ChannelNum, CHANNEL_WIDTH_20, HAL_PRIME_CHNL_OFFSET_DONT_CARE)) {
			if (CH_IS_NON_OCP(&ch_set[i]))
				DBG_871X_SEL(sel, ", non_ocp:%d"
					, rtw_systime_to_ms(ch_set[i].non_ocp_end_time - rtw_get_current_time()));
			else
				DBG_871X_SEL(sel, ", non_ocp:N/A");
		}
		#endif

		DBG_871X_SEL(sel, "\n");
	}

	DBG_871X_SEL_NL(sel, "total ch number:%d\n", i);
}

void dump_cur_chset(void *sel, _adapter *adapter)
{
	struct mlme_priv *mlme = &adapter->mlmepriv;
	struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	if (mlme->country_ent)
		dump_country_chplan(sel, mlme->country_ent);
	else
		DBG_871X_SEL_NL(sel, "chplan:0x%02X\n", mlme->ChannelPlan);

	DBG_871X_SEL_NL(sel, "2G_PLS:%u, 5G_PLS:%u\n"
		, hal_data->Regulation2_4G, hal_data->Regulation5G);
	dump_chset(sel, mlmeext->channel_set);
}

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
	for(i=0;ch_set[i].ChannelNum!=0;i++){
		if(ch == ch_set[i].ChannelNum)
			break;
	}
	
	if(i >= ch_set[i].ChannelNum)
		return -1;
	return i;
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
	) {
		return _TRUE;
	}
	return _FALSE;
}

/****************************************************************************

Following are the initialization functions for WiFi MLME

*****************************************************************************/

int init_hw_mlme_ext(_adapter *padapter)
{
	struct	mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	//set_opmode_cmd(padapter, infra_client_with_mlme);//removed

	set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	return _SUCCESS;
}

void init_mlme_default_rate_set(_adapter* padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	unsigned char	mixed_datarate[NumRates] = {_1M_RATE_, _2M_RATE_, _5M_RATE_, _11M_RATE_, _6M_RATE_,_9M_RATE_, _12M_RATE_, _18M_RATE_, _24M_RATE_, _36M_RATE_, _48M_RATE_, _54M_RATE_, 0xff};
	unsigned char	mixed_basicrate[NumRates] ={_1M_RATE_, _2M_RATE_, _5M_RATE_, _11M_RATE_, _6M_RATE_, _12M_RATE_, _24M_RATE_, 0xff,};
	unsigned char	supported_mcs_set[16] = {0xff, 0xff, 0xff, 0x00, 0x00, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

	_rtw_memcpy(pmlmeext->datarate, mixed_datarate, NumRates);
	_rtw_memcpy(pmlmeext->basicrate, mixed_basicrate, NumRates);

	_rtw_memcpy(pmlmeext->default_supported_mcs_set, supported_mcs_set, sizeof(pmlmeext->default_supported_mcs_set));
}

static void init_mlme_ext_priv_value(_adapter* padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	ATOMIC_SET(&pmlmeext->event_seq, 0);
	pmlmeext->mgnt_seq = 0;//reset to zero when disconnect at client mode
#ifdef CONFIG_IEEE80211W
	pmlmeext->sa_query_seq = 0;
	pmlmeext->mgnt_80211w_IPN=0;
	pmlmeext->mgnt_80211w_IPN_rx=0;
#endif //CONFIG_IEEE80211W
	pmlmeext->cur_channel = padapter->registrypriv.channel;
	pmlmeext->cur_bwmode = CHANNEL_WIDTH_20;
	pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	
	pmlmeext->retry = 0;

	pmlmeext->cur_wireless_mode = padapter->registrypriv.wireless_mode;

	init_mlme_default_rate_set(padapter);

	if(pmlmeext->cur_channel > 14)
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
	mlmeext_assign_scan_backop_flags_sta(pmlmeext, /*SS_BACKOP_EN|*/SS_BACKOP_PS_ANNC|SS_BACKOP_TX_RESUME);
	mlmeext_assign_scan_backop_flags_ap(pmlmeext, SS_BACKOP_EN|SS_BACKOP_PS_ANNC|SS_BACKOP_TX_RESUME);
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
}

static int has_channel(RT_CHANNEL_INFO *channel_set,
					   u8 chanset_size,
					   u8 chan) {
	int i;

	for (i = 0; i < chanset_size; i++) {
		if (channel_set[i].ChannelNum == chan) {
			return 1;
		}
	}

	return 0;
}

static void init_channel_list(_adapter *padapter, RT_CHANNEL_INFO *channel_set,
							  u8 chanset_size,
							  struct p2p_channels *channel_list) {
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
			if (!has_channel(channel_set, chanset_size, ch)) {
				continue;
			}

			if ((0 == padapter->registrypriv.ht_enable) && (8 == o->inc))
				continue;

			if ((REGSTY_IS_BW_5G_SUPPORT(regsty, CHANNEL_WIDTH_40)) &&
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

static u8 init_channel_set(_adapter* padapter, u8 ChannelPlan, RT_CHANNEL_INFO *channel_set)
{
	u8	index,chanset_size = 0;
	u8	b5GBand = _FALSE, b2_4GBand = _FALSE;
	u8	Index2G = 0, Index5G=0;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);

	if (!rtw_is_channel_plan_valid(ChannelPlan)) {
		DBG_871X_LEVEL(_drv_err_, "ChannelPlan ID 0x%02X error !!!!!\n", ChannelPlan);
		return chanset_size;
	}

	_rtw_memset(channel_set, 0, sizeof(RT_CHANNEL_INFO)*MAX_CHANNEL_NUM);

	if (IsSupported24G(padapter->registrypriv.wireless_mode))
		b2_4GBand = _TRUE;

	if (IsSupported5G(padapter->registrypriv.wireless_mode))
		b5GBand = _TRUE;

	if (b2_4GBand) {
		if (RTW_CHPLAN_REALTEK_DEFINE == ChannelPlan)
			Index2G = RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE.Index2G;
		else
			Index2G = RTW_ChannelPlanMap[ChannelPlan].Index2G;

		for (index = 0; index < RTW_ChannelPlan2G[Index2G].Len; index++) {
			channel_set[chanset_size].ChannelNum = RTW_ChannelPlan2G[Index2G].Channel[index];

			if (RTW_CHPLAN_GLOBAL_DOAMIN == ChannelPlan
				|| RTW_CHPLAN_GLOBAL_NULL == ChannelPlan
			) {
				/* Channel 1~11 is active, and 12~14 is passive */
				if(channel_set[chanset_size].ChannelNum >= 1 && channel_set[chanset_size].ChannelNum <= 11)
					channel_set[chanset_size].ScanType = SCAN_ACTIVE;
				else if((channel_set[chanset_size].ChannelNum  >= 12 && channel_set[chanset_size].ChannelNum  <= 14))
					channel_set[chanset_size].ScanType  = SCAN_PASSIVE;			
			} else if (RTW_CHPLAN_WORLD_WIDE_13 == ChannelPlan
				|| RTW_CHPLAN_WORLD_WIDE_5G == ChannelPlan
				|| RTW_RD_2G_WORLD == Index2G
			) {
				/* channel 12~13, passive scan */
				if(channel_set[chanset_size].ChannelNum <= 11)
					channel_set[chanset_size].ScanType = SCAN_ACTIVE;
				else
					channel_set[chanset_size].ScanType = SCAN_PASSIVE;
			} else {
				channel_set[chanset_size].ScanType = SCAN_ACTIVE;
			}

			chanset_size++;
		}
	}

	if (b5GBand) {
		if (RTW_CHPLAN_REALTEK_DEFINE == ChannelPlan)
			Index5G = RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE.Index5G;
		else
			Index5G = RTW_ChannelPlanMap[ChannelPlan].Index5G;

		for (index = 0; index < RTW_ChannelPlan5G[Index5G].Len; index++) {
#ifdef CONFIG_DFS
			channel_set[chanset_size].ChannelNum = RTW_ChannelPlan5G[Index5G].Channel[index];
			if ( channel_set[chanset_size].ChannelNum <= 48 
				|| channel_set[chanset_size].ChannelNum >= 149 )
			{
				if (RTW_CHPLAN_WORLD_WIDE_5G == ChannelPlan) /* passive scan for all 5G channels */
					channel_set[chanset_size].ScanType = SCAN_PASSIVE;
				else
					channel_set[chanset_size].ScanType = SCAN_ACTIVE;
			}
			else
			{
				channel_set[chanset_size].ScanType = SCAN_PASSIVE;
			}
			chanset_size++;
#else /* CONFIG_DFS */
			if (RTW_ChannelPlan5G[Index5G].Channel[index] <= 48
				|| RTW_ChannelPlan5G[Index5G].Channel[index] >= 149
			) {
				channel_set[chanset_size].ChannelNum = RTW_ChannelPlan5G[Index5G].Channel[index];
				if (RTW_CHPLAN_WORLD_WIDE_5G == ChannelPlan) /* passive scan for all 5G channels */
					channel_set[chanset_size].ScanType = SCAN_PASSIVE;
				else
					channel_set[chanset_size].ScanType = SCAN_ACTIVE;
				DBG_871X("%s(): channel_set[%d].ChannelNum = %d\n", __FUNCTION__, chanset_size, channel_set[chanset_size].ChannelNum);
				chanset_size++;
			}
#endif /* CONFIG_DFS */
		}
	}

	if (RTW_CHPLAN_REALTEK_DEFINE == ChannelPlan) {
		hal_data->Regulation2_4G = RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE.regd;
		hal_data->Regulation5G = RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE.regd;
	} else {
		hal_data->Regulation2_4G = RTW_ChannelPlanMap[ChannelPlan].regd;
		hal_data->Regulation5G = RTW_ChannelPlanMap[ChannelPlan].regd;
	}

	DBG_871X(FUNC_ADPT_FMT" ChannelPlan ID:0x%02x, ch num:%d\n"
		, FUNC_ADPT_ARG(padapter), ChannelPlan, chanset_size);

	return chanset_size;
}

int	init_mlme_ext_priv(_adapter* padapter)
{
	int	res = _SUCCESS;
	struct registry_priv* pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	// We don't need to memset padapter->XXX to zero, because adapter is allocated by rtw_zvmalloc().
	//_rtw_memset((u8 *)pmlmeext, 0, sizeof(struct mlme_ext_priv));

	pmlmeext->padapter = padapter;

	//fill_fwpriv(padapter, &(pmlmeext->fwpriv));

	init_mlme_ext_priv_value(padapter);
	pmlmeinfo->bAcceptAddbaReq = pregistrypriv->bAcceptAddbaReq;
	
	init_mlme_ext_timer(padapter);

#ifdef CONFIG_AP_MODE
	init_mlme_ap_info(padapter);	
#endif

	pmlmeext->max_chan_nums = init_channel_set(padapter, pmlmepriv->ChannelPlan,pmlmeext->channel_set);
	init_channel_list(padapter, pmlmeext->channel_set, pmlmeext->max_chan_nums, &pmlmeext->channel_list);
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

	return res;

}

void free_mlme_ext_priv (struct mlme_ext_priv *pmlmeext)
{
	_adapter *padapter = pmlmeext->padapter;

	if (!padapter)
		return;

	if (rtw_is_drv_stopped(padapter)) {
		_cancel_timer_ex(&pmlmeext->survey_timer);
		_cancel_timer_ex(&pmlmeext->link_timer);
		//_cancel_timer_ex(&pmlmeext->ADDBA_timer);
	}
}

static u8 cmp_pkt_chnl_diff(_adapter *padapter,u8* pframe,uint packet_len)
{	// if the channel is same, return 0. else return channel differential	
	uint len;
	u8 channel;	
	u8 *p;		
	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _BEACON_IE_OFFSET_, _DSSET_IE_, &len, packet_len - _BEACON_IE_OFFSET_);	
	if (p)	
	{	
		channel = *(p + 2);		
		if(padapter->mlmeextpriv.cur_channel >= channel)		
		{			
			return (padapter->mlmeextpriv.cur_channel - channel);		
		}		
		else		
		{			
			return (channel-padapter->mlmeextpriv.cur_channel);		
		}	
	}	
	else
	{		
		return 0;	
	}
}

static void _mgt_dispatcher(_adapter *padapter, struct mlme_handler *ptable, union recv_frame *precv_frame)
{
	u8 bc_addr[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
	u8 *pframe = precv_frame->u.hdr.rx_data; 

	  if(ptable->func)
        {
       	 //receive the frames that ra(a1) is my address or ra(a1) is bc address.
		if (!_rtw_memcmp(GetAddr1Ptr(pframe), adapter_mac_addr(padapter), ETH_ALEN) &&
			!_rtw_memcmp(GetAddr1Ptr(pframe), bc_addr, ETH_ALEN)) 
		{
			return;
		}
		
		ptable->func(padapter, precv_frame);
        }
	
}

void mgt_dispatcher(_adapter *padapter, union recv_frame *precv_frame)
{
	int index;
	struct mlme_handler *ptable;
#ifdef CONFIG_AP_MODE
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
#endif //CONFIG_AP_MODE
	u8 bc_addr[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
	u8 *pframe = precv_frame->u.hdr.rx_data;
	struct sta_info *psta = rtw_get_stainfo(&padapter->stapriv, GetAddr2Ptr(pframe));
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("+mgt_dispatcher: type(0x%x) subtype(0x%x)\n",
		  GetFrameType(pframe), GetFrameSubType(pframe)));

#if 0
	{
		u8 *pbuf;
		pbuf = GetAddr1Ptr(pframe);
		DBG_871X("A1-%x:%x:%x:%x:%x:%x\n", *pbuf, *(pbuf+1), *(pbuf+2), *(pbuf+3), *(pbuf+4), *(pbuf+5));
		pbuf = GetAddr2Ptr(pframe);
		DBG_871X("A2-%x:%x:%x:%x:%x:%x\n", *pbuf, *(pbuf+1), *(pbuf+2), *(pbuf+3), *(pbuf+4), *(pbuf+5));
		pbuf = GetAddr3Ptr(pframe);
		DBG_871X("A3-%x:%x:%x:%x:%x:%x\n", *pbuf, *(pbuf+1), *(pbuf+2), *(pbuf+3), *(pbuf+4), *(pbuf+5));
	}
#endif

	if (GetFrameType(pframe) != WIFI_MGT_TYPE)
	{
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("mgt_dispatcher: type(0x%x) error!\n", GetFrameType(pframe)));
		return;
	}

	//receive the frames that ra(a1) is my address or ra(a1) is bc address.
	if (!_rtw_memcmp(GetAddr1Ptr(pframe), adapter_mac_addr(padapter), ETH_ALEN) &&
		!_rtw_memcmp(GetAddr1Ptr(pframe), bc_addr, ETH_ALEN))
	{
		return;
	}

	ptable = mlme_sta_tbl;

	index = GetFrameSubType(pframe) >> 4;

#ifdef CONFIG_TDLS
	if((index << 4)==WIFI_ACTION){
		/* category==public (4), action==TDLS_DISCOVERY_RESPONSE */
		if (*(pframe+24) == RTW_WLAN_CATEGORY_PUBLIC && *(pframe+25) == TDLS_DISCOVERY_RESPONSE) {
			DBG_871X("[TDLS] Recv %s from "MAC_FMT"\n", rtw_tdls_action_txt(TDLS_DISCOVERY_RESPONSE), MAC_ARG(GetAddr2Ptr(pframe)));
			On_TDLS_Dis_Rsp(padapter, precv_frame);
		}
	}
#endif //CONFIG_TDLS

	if (index >= (sizeof(mlme_sta_tbl) /sizeof(struct mlme_handler)))
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Currently we do not support reserved sub-fr-type=%d\n", index));
		return;
	}
	ptable += index;

#if 1
	if (psta != NULL)
	{
		if (GetRetry(pframe))
		{
			if (precv_frame->u.hdr.attrib.seq_num == psta->RxMgmtFrameSeqNum)
			{
				/* drop the duplicate management frame */
				pdbgpriv->dbg_rx_dup_mgt_frame_drop_count++;
				DBG_871X("Drop duplicate management frame with seq_num = %d.\n", precv_frame->u.hdr.attrib.seq_num);
				return;
			}
		}
		psta->RxMgmtFrameSeqNum = precv_frame->u.hdr.attrib.seq_num;
	}
#else

	if(GetRetry(pframe))
	{
		//RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("drop due to decache!\n"));
		//return;
	}
#endif

#ifdef CONFIG_AP_MODE
	switch (GetFrameSubType(pframe)) 
	{
		case WIFI_AUTH:
			if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
				ptable->func = &OnAuth;
			else
				ptable->func = &OnAuthClient;
			//pass through
		case WIFI_ASSOCREQ:
		case WIFI_REASSOCREQ:
			_mgt_dispatcher(padapter, ptable, precv_frame);	
#ifdef CONFIG_HOSTAPD_MLME				
			if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
				rtw_hostapd_mlme_rx(padapter, precv_frame);
#endif			
			break;
		case WIFI_PROBEREQ:
			if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
			{
#ifdef CONFIG_HOSTAPD_MLME		
				rtw_hostapd_mlme_rx(padapter, precv_frame);		
#else
				_mgt_dispatcher(padapter, ptable, precv_frame);
#endif
			}
			else
				_mgt_dispatcher(padapter, ptable, precv_frame);
			break;
		case WIFI_BEACON:			
			_mgt_dispatcher(padapter, ptable, precv_frame);
			break;
		case WIFI_ACTION:
			//if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
			_mgt_dispatcher(padapter, ptable, precv_frame);		
			break;
		default:
			_mgt_dispatcher(padapter, ptable, precv_frame);	
			if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
				rtw_hostapd_mlme_rx(padapter, precv_frame);			
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
	if( padapter->wdinfo.driver_interface == DRIVER_CFG80211 )
	{
		if(padapter->cfg80211_wdinfo.is_ro_ch == _FALSE
			|| rtw_get_oper_ch(padapter) != padapter->wdinfo.listen_channel
			|| adapter_wdev_data(padapter)->p2p_enabled == _FALSE
			|| padapter->mlmepriv.wps_probe_resp_ie == NULL
			|| padapter->mlmepriv.p2p_probe_resp_ie == NULL
		)
		{
#ifdef CONFIG_DEBUG_CFG80211
			DBG_871X("DON'T issue_probersp_p2p: p2p_enabled:%d, wps_probe_resp_ie:%p, p2p_probe_resp_ie:%p, ",
				adapter_wdev_data(padapter)->p2p_enabled,
				padapter->mlmepriv.wps_probe_resp_ie,
				padapter->mlmepriv.p2p_probe_resp_ie);
			DBG_871X("is_ro_ch:%d, op_ch:%d, p2p_listen_channel:%d\n", 
				padapter->cfg80211_wdinfo.is_ro_ch,
				rtw_get_oper_ch(padapter),
				padapter->wdinfo.listen_channel);
#endif
			response = _FALSE;
		}
	}
	else
#endif //CONFIG_IOCTL_CFG80211
	if( padapter->wdinfo.driver_interface == DRIVER_WEXT )
	{
		//	do nothing if the device name is empty
		if ( !padapter->wdinfo.device_name_len )
		{
			response	= _FALSE;
		}
	}

	if (response == _TRUE)
		issue_probersp_p2p( padapter, da);
	
	return _SUCCESS;
}
#endif //CONFIG_P2P


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
	WLAN_BSSID_EX 	*cur = &(pmlmeinfo->network);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;
	u8 is_valid_p2p_probereq = _FALSE;

#ifdef CONFIG_ATMEL_RC_PATCH
	u8 *target_ie=NULL, *wps_ie=NULL;
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
		rtw_cfg80211_rx_probe_request(padapter, pframe, len);
		return _SUCCESS;
	}
#endif /* CONFIG_IOCTL_CFG80211 */

	if (	!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) && 
		!rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE) && 
		!rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT) &&
		!rtw_p2p_chk_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH) &&
		!rtw_p2p_chk_state(pwdinfo, P2P_STATE_SCAN)
	   )
	{
		//	Commented by Albert 2011/03/17
		//	mcs_rate = 0 -> CCK 1M rate
		//	mcs_rate = 1 -> CCK 2M rate
		//	mcs_rate = 2 -> CCK 5.5M rate
		//	mcs_rate = 3 -> CCK 11M rate
		//	In the P2P mode, the driver should not support the CCK rate

		//	Commented by Kurt 2012/10/16
		//	IOT issue: Google Nexus7 use 1M rate to send p2p_probe_req after GO nego completed and Nexus7 is client
        if (padapter->registrypriv.wifi_spec == 1)
        {
            if ( pattrib->data_rate <= 3 )
            {
                wifi_test_chk_rate = 0;
            }
        }

		if( wifi_test_chk_rate == 1 )
		{
			if((is_valid_p2p_probereq = process_probe_req_p2p_ie(pwdinfo, pframe, len)) == _TRUE)
			{
				if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE))
				{
					// FIXME
					if( padapter->wdinfo.driver_interface == DRIVER_WEXT )
						report_survey_event(padapter, precv_frame);

					p2p_listen_state_process( padapter,  get_sa(pframe));

					return _SUCCESS;	
				}

				if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
				{
					goto _continue;
				}
			}
		}
	}

_continue:
#endif //CONFIG_P2P

	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE))
	{
		return _SUCCESS;
	}

	if(check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE && 
		check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE|WIFI_AP_STATE)==_FALSE)
	{
		return _SUCCESS;
	}


	//DBG_871X("+OnProbeReq\n");


#ifdef CONFIG_ATMEL_RC_PATCH
		if ((wps_ie = rtw_get_wps_ie(
			pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_,
			len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_,
			 NULL, &wps_ielen))) {
		
			target_ie = rtw_get_wps_attr_content( wps_ie, wps_ielen, WPS_ATTR_MANUFACTURER, NULL, &target_ielen);
		}
		if ((target_ie && (target_ielen == 4)) && (_TRUE ==_rtw_memcmp((void *)target_ie, "Ozmo",4 ))) {
			//psta->flag_atmel_rc = 1;
			unsigned char *sa_addr = get_sa(pframe);
			printk("%s: Find Ozmo RC -- %02x:%02x:%02x:%02x:%02x:%02x  \n\n",
				__func__, *sa_addr, *(sa_addr+1), *(sa_addr+2), *(sa_addr+3), *(sa_addr+4), *(sa_addr+5));
			_rtw_memcpy(  pstapriv->atmel_rc_pattern, get_sa(pframe), ETH_ALEN);
		}
#endif


#ifdef CONFIG_AUTO_AP_MODE
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE &&
			pmlmepriv->cur_network.join_res == _TRUE)
	{
		_irqL	irqL;
		struct sta_info	*psta;
		u8 *mac_addr, *peer_addr;
		struct sta_priv *pstapriv = &padapter->stapriv;
		u8 RC_OUI[4]={0x00,0xE0,0x4C,0x0A};
		//EID[1] + EID_LEN[1] + RC_OUI[4] + MAC[6] + PairingID[2] + ChannelNum[2]

		p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_, _VENDOR_SPECIFIC_IE_, (int *)&ielen,
			len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_);

		if(!p || ielen !=14)
			goto _non_rc_device;

		if(!_rtw_memcmp(p+2, RC_OUI, sizeof(RC_OUI)))
			goto _non_rc_device;

		if(!_rtw_memcmp(p+6, get_sa(pframe), ETH_ALEN))
		{
			DBG_871X("%s, do rc pairing ("MAC_FMT"), but mac addr mismatch!("MAC_FMT")\n", __FUNCTION__,
				MAC_ARG(get_sa(pframe)), MAC_ARG(p+6));

			goto _non_rc_device;
		}

		DBG_871X("%s, got the pairing device("MAC_FMT")\n", __FUNCTION__,  MAC_ARG(get_sa(pframe)));

		//new a station
		psta = rtw_get_stainfo(pstapriv, get_sa(pframe));
		if (psta == NULL)
		{
			// allocate a new one
			DBG_871X("going to alloc stainfo for rc="MAC_FMT"\n",  MAC_ARG(get_sa(pframe)));
			psta = rtw_alloc_stainfo(pstapriv, get_sa(pframe));
			if (psta == NULL)
			{
				//TODO:
				DBG_871X(" Exceed the upper limit of supported clients...\n");
				return _SUCCESS;
			}

			_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
			if (rtw_is_list_empty(&psta->asoc_list))
			{
				psta->expire_to = pstapriv->expire_to;
				rtw_list_insert_tail(&psta->asoc_list, &pstapriv->asoc_list);
				pstapriv->asoc_list_cnt++;
			}
			_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

			//generate pairing ID
			mac_addr = adapter_mac_addr(padapter);
			peer_addr = psta->hwaddr;
			psta->pid = (u16)(((mac_addr[4]<<8) + mac_addr[5]) + ((peer_addr[4]<<8) + peer_addr[5]));

			//update peer stainfo
			psta->isrc = _TRUE;
			//psta->aid = 0;
			//psta->mac_id = 2;

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
			psta->ieee8021x_blocked = _FALSE;
#ifdef CONFIG_80211N_HT
			psta->htpriv.ht_option = _TRUE;
			psta->htpriv.ampdu_enable = _FALSE;
			psta->htpriv.sgi_20m = _FALSE;
			psta->htpriv.sgi_40m = _FALSE;
			psta->htpriv.ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			psta->htpriv.agg_enable_bitmap = 0x0;//reset
			psta->htpriv.candidate_tid_bitmap = 0x0;//reset
#endif

			rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, psta, _TRUE);

			_rtw_memset((void*)&psta->sta_stats, 0, sizeof(struct stainfo_stats));

			_enter_critical_bh(&psta->lock, &irqL);
			psta->state |= _FW_LINKED;
			_exit_critical_bh(&psta->lock, &irqL);

			report_add_sta_event(padapter, psta->hwaddr);

		}

		issue_probersp(padapter, get_sa(pframe), _FALSE);

		return _SUCCESS;

	}

_non_rc_device:

	return _SUCCESS;

#endif //CONFIG_AUTO_AP_MODE
	

#ifdef CONFIG_CONCURRENT_MODE
	if(((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE) &&
		check_buddy_fwstate(padapter, _FW_UNDER_LINKING|_FW_UNDER_SURVEY))
	{
		//don't process probe req
		return _SUCCESS;
	}
#endif	

	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_, _SSID_IE_, (int *)&ielen,
			len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_);


	//check (wildcard) SSID 
	if (p != NULL)
	{
		if(is_valid_p2p_probereq == _TRUE)
		{
			goto _issue_probersp;
		}

		if ( (ielen != 0 && _FALSE ==_rtw_memcmp((void *)(p+2), (void *)cur->Ssid.Ssid, cur->Ssid.SsidLength))
			|| (ielen == 0 && pmlmeinfo->hidden_ssid_mode)
		)
		{
			return _SUCCESS;
		}

_issue_probersp:
		if(((check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE && 
			pmlmepriv->cur_network.join_res == _TRUE)) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE))
		{
			//DBG_871X("+issue_probersp during ap mode\n");
			issue_probersp(padapter, get_sa(pframe), is_valid_p2p_probereq);		
		}

	}

	return _SUCCESS;

}

unsigned int OnProbeRsp(_adapter *padapter, union recv_frame *precv_frame)
{
	struct sta_info		*psta;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv		*pstapriv = &padapter->stapriv;
	u8	*pframe = precv_frame->u.hdr.rx_data;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &padapter->wdinfo;
#endif


#ifdef CONFIG_P2P
	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_PROVISION_DIS_REQ))
	{
		if ( _TRUE == pwdinfo->tx_prov_disc_info.benable )
		{
			if( _rtw_memcmp( pwdinfo->tx_prov_disc_info.peerIFAddr, GetAddr2Ptr(pframe), ETH_ALEN ) )
			{
				if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT))
				{
					pwdinfo->tx_prov_disc_info.benable = _FALSE;
					issue_p2p_provision_request( padapter,
												pwdinfo->tx_prov_disc_info.ssid.Ssid, 
												pwdinfo->tx_prov_disc_info.ssid.SsidLength,
												pwdinfo->tx_prov_disc_info.peerDevAddr );
				}
				else if ( rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE) || rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO) )
				{
					pwdinfo->tx_prov_disc_info.benable = _FALSE;
					issue_p2p_provision_request( padapter,
												NULL, 
												0,
												pwdinfo->tx_prov_disc_info.peerDevAddr );
				}
			}		
		}
		return _SUCCESS;
	}
	else if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING))
	{
		if ( _TRUE == pwdinfo->nego_req_info.benable )
		{
			DBG_871X( "[%s] P2P State is GONEGO ING!\n", __FUNCTION__ );
			if( _rtw_memcmp( pwdinfo->nego_req_info.peerDevAddr, GetAddr2Ptr(pframe), ETH_ALEN ) )
			{
				pwdinfo->nego_req_info.benable = _FALSE;
				issue_p2p_GO_request( padapter, pwdinfo->nego_req_info.peerDevAddr);
			}
		}
	}
	else if( rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_INVITE_REQ ) )
	{
		if ( _TRUE == pwdinfo->invitereq_info.benable )
		{
			DBG_871X( "[%s] P2P_STATE_TX_INVITE_REQ!\n", __FUNCTION__ );
			if( _rtw_memcmp( pwdinfo->invitereq_info.peer_macaddr, GetAddr2Ptr(pframe), ETH_ALEN ) )
			{
				pwdinfo->invitereq_info.benable = _FALSE;
				issue_p2p_invitation_request( padapter, pwdinfo->invitereq_info.peer_macaddr );
			}
		}
	}
#endif


	if (mlmeext_chk_scan_state(pmlmeext, SCAN_PROCESS)) {
		report_survey_event(padapter, precv_frame);	
#ifdef CONFIG_CONCURRENT_MODE
		report_survey_event(padapter->pbuddy_adapter, precv_frame);	
#endif
		return _SUCCESS;
	}

	#if 0 //move to validate_recv_mgnt_frame
	if (_rtw_memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN))
	{
		if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
		{
			if ((psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe))) != NULL)
			{
				psta->sta_stats.rx_mgnt_pkts++;
			}
		}
	}
	#endif
	
	return _SUCCESS;
	
}

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
	u8 *p = NULL;
	u32 ielen = 0;
#ifdef CONFIG_TDLS
	struct sta_info *ptdls_sta;
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
#ifdef CONFIG_TDLS_CH_SW
	struct tdls_ch_switch *pchsw_info = &padapter->tdlsinfo.chsw_info;
#endif
#endif /* CONFIG_TDLS */

#ifdef CONFIG_ATTEMPT_TO_FIX_AP_BEACON_ERROR
	p = rtw_get_ie(pframe + sizeof(struct rtw_ieee80211_hdr_3addr) + _BEACON_IE_OFFSET_, _EXT_SUPPORTEDRATES_IE_, &ielen, precv_frame->u.hdr.len -sizeof(struct rtw_ieee80211_hdr_3addr) - _BEACON_IE_OFFSET_);
	if ((p != NULL) && (ielen > 0))
	{
		if ((*(p + 1 + ielen) == 0x2D) && (*(p + 2 + ielen) != 0x2D))
		{
			/* Invalid value 0x2D is detected in Extended Supported Rates (ESR) IE. Try to fix the IE length to avoid failed Beacon parsing. */	
		       	DBG_871X("[WIFIDBG] Error in ESR IE is detected in Beacon of BSSID:"MAC_FMT". Fix the length of ESR IE to avoid failed Beacon parsing.\n", MAC_ARG(GetAddr3Ptr(pframe)));
		       	*(p + 1) = ielen - 1;
		}
	}
#endif

	if (mlmeext_chk_scan_state(pmlmeext, SCAN_PROCESS)) {
		report_survey_event(padapter, precv_frame);
#ifdef CONFIG_CONCURRENT_MODE
		report_survey_event(padapter->pbuddy_adapter, precv_frame);	
#endif
		return _SUCCESS;
	}

	if (_rtw_memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN))
	{
		if (pmlmeinfo->state & WIFI_FW_AUTH_NULL)
		{
			//we should update current network before auth, or some IE is wrong
			pbss = (WLAN_BSSID_EX*)rtw_malloc(sizeof(WLAN_BSSID_EX));
			if (pbss) {
				if (collect_bss_info(padapter, precv_frame, pbss) == _SUCCESS) {
					struct beacon_keys recv_beacon;

					update_network(&(pmlmepriv->cur_network.network), pbss, padapter, _TRUE);
					rtw_get_bcn_info(&(pmlmepriv->cur_network));

					// update bcn keys
					if (rtw_get_bcn_keys(padapter, pframe, len, &recv_beacon) == _TRUE) {
						DBG_871X("%s: beacon keys ready\n", __func__);
						_rtw_memcpy(&pmlmepriv->cur_beacon_keys,
							&recv_beacon, sizeof(recv_beacon));
						pmlmepriv->new_beacon_cnts = 0;
					}
					else {
						DBG_871X_LEVEL(_drv_err_, "%s: get beacon keys failed\n", __func__);
						_rtw_memset(&pmlmepriv->cur_beacon_keys, 0, sizeof(recv_beacon));
						pmlmepriv->new_beacon_cnts = 0;
					}
				}
				rtw_mfree((u8*)pbss, sizeof(WLAN_BSSID_EX));
			}

			//check the vendor of the assoc AP
			pmlmeinfo->assoc_AP_vendor = check_assoc_AP(pframe+sizeof(struct rtw_ieee80211_hdr_3addr), len-sizeof(struct rtw_ieee80211_hdr_3addr));				

			//update TSF Value
			update_TSF(pmlmeext, pframe, len);

			//reset for adaptive_early_32k
			pmlmeext->adaptive_tsf_done = _FALSE;
			pmlmeext->DrvBcnEarly = 0xff;
			pmlmeext->DrvBcnTimeOut = 0xff;
			pmlmeext->bcn_cnt = 0;
			_rtw_memset(pmlmeext->bcn_delay_cnt, 0, sizeof(pmlmeext->bcn_delay_cnt));
			_rtw_memset(pmlmeext->bcn_delay_ratio, 0, sizeof(pmlmeext->bcn_delay_ratio));

#ifdef CONFIG_P2P_PS
			process_p2p_ps_ie(padapter, (pframe + WLAN_HDR_A3_LEN), (len - WLAN_HDR_A3_LEN));
#endif //CONFIG_P2P_PS

#if defined(CONFIG_P2P)&&defined(CONFIG_CONCURRENT_MODE)
			if (padapter->registrypriv.wifi_spec) {
				if (process_p2p_cross_connect_ie(padapter, (pframe + WLAN_HDR_A3_LEN), (len - WLAN_HDR_A3_LEN)) == _FALSE) {
					if((padapter->pbuddy_adapter->mlmeextpriv.mlmext_info.state&0x03) == WIFI_FW_AP_STATE) {
						DBG_871X_LEVEL(_drv_always_, "no issue auth, P2P cross-connect does not permit\n ");
						return _SUCCESS;
					}
				}
			}
#endif // CONFIG_P2P CONFIG_P2P and CONFIG_CONCURRENT_MODE

			//start auth
			start_clnt_auth(padapter);

			return _SUCCESS;
		}

		if(((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE) && (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS))
		{
			if ((psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe))) != NULL)
			{
				#ifdef CONFIG_PATCH_JOIN_WRONG_CHANNEL        
				//Merge from 8712 FW code
				if (cmp_pkt_chnl_diff(padapter,pframe,len) != 0)        
				{            // join wrong channel, deauth and reconnect           
					issue_deauth(padapter, (&(pmlmeinfo->network))->MacAddress, WLAN_REASON_DEAUTH_LEAVING);

					report_del_sta_event(padapter, (&(pmlmeinfo->network))->MacAddress, WLAN_REASON_JOIN_WRONG_CHANNEL, _TRUE, _FALSE);
					pmlmeinfo->state &= (~WIFI_FW_ASSOC_SUCCESS);    		
					return _SUCCESS;
				}        
				#endif //CONFIG_PATCH_JOIN_WRONG_CHANNEL

				ret = rtw_check_bcn_info(padapter, pframe, len);
				if (!ret) {
						DBG_871X_LEVEL(_drv_always_, "ap has changed, disconnect now\n ");
						receive_disconnect(padapter, pmlmeinfo->network.MacAddress , 0, _FALSE);
						return _SUCCESS;
				}
				//update WMM, ERP in the beacon
				//todo: the timer is used instead of the number of the beacon received
				if ((sta_rx_pkts(psta) & 0xf) == 0)
				{
					//DBG_871X("update_bcn_info\n");
					update_beacon_info(padapter, pframe, len, psta);
				}

				adaptive_early_32k(pmlmeext, pframe, len);			 	
				
#ifdef CONFIG_TDLS
#ifdef CONFIG_TDLS_CH_SW
				if (rtw_tdls_is_chsw_allowed(padapter) == _TRUE)
				{
					/* Send TDLS Channel Switch Request when receiving Beacon */
					if ((padapter->tdlsinfo.chsw_info.ch_sw_state & TDLS_CH_SW_INITIATOR_STATE) && (ATOMIC_READ(&pchsw_info->chsw_on) == _TRUE)
						&& (pmlmeext->cur_channel == rtw_get_oper_ch(padapter))) {
						ptdls_sta = rtw_get_stainfo(&padapter->stapriv, padapter->tdlsinfo.chsw_info.addr);
						if (ptdls_sta != NULL) {
							if (ptdls_sta->tdls_sta_state | TDLS_LINKED_STATE) {
								_set_timer(&ptdls_sta->stay_on_base_chnl_timer, TDLS_CH_SW_STAY_ON_BASE_CHNL_TIMEOUT);
							}
						}
					}
				}
#endif				
#endif /* CONFIG_TDLS */

#ifdef CONFIG_DFS
				process_csa_ie(padapter, pframe, len);	//channel switch announcement
#endif //CONFIG_DFS

#ifdef CONFIG_P2P_PS
				process_p2p_ps_ie(padapter, (pframe + WLAN_HDR_A3_LEN), (len - WLAN_HDR_A3_LEN));
#endif //CONFIG_P2P_PS

				if (pmlmeext->en_hw_update_tsf)
					rtw_enable_hw_update_tsf_cmd(padapter);

				#if 0 //move to validate_recv_mgnt_frame
				psta->sta_stats.rx_mgnt_pkts++;
				#endif
			}

		} else if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) {
			_irqL irqL;
			u8 rate_set[16];
			u8 rate_num = 0;

			psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
			if (psta != NULL) {
				/*
				* update WMM, ERP in the beacon
				* todo: the timer is used instead of the number of the beacon received
				*/
				if ((sta_rx_pkts(psta) & 0xf) == 0)
					update_beacon_info(padapter, pframe, len, psta);

				if (pmlmeext->en_hw_update_tsf)
					rtw_enable_hw_update_tsf_cmd(padapter);

			} else {
				rtw_ies_get_supported_rate(pframe + WLAN_HDR_A3_LEN + _BEACON_IE_OFFSET_, len - WLAN_HDR_A3_LEN - _BEACON_IE_OFFSET_, rate_set, &rate_num);
				if (rate_num == 0) {
					DBG_871X(FUNC_ADPT_FMT" RX beacon with no supported rate\n", FUNC_ADPT_ARG(padapter));
					goto _END_ONBEACON_;
				}

				psta = rtw_alloc_stainfo(pstapriv, GetAddr2Ptr(pframe));
				if (psta == NULL) {
					DBG_871X(FUNC_ADPT_FMT" Exceed the upper limit of supported clients\n", FUNC_ADPT_ARG(padapter));
					goto _END_ONBEACON_;
				}

				psta->expire_to = pstapriv->adhoc_expire_to;

				_rtw_memcpy(psta->bssrateset, rate_set, rate_num);
				psta->bssratelen = rate_num;

				//update TSF Value
				update_TSF(pmlmeext, pframe, len);			

				//report sta add event
				report_add_sta_event(padapter, GetAddr2Ptr(pframe));
			}
		}
	}

_END_ONBEACON_:

	return _SUCCESS;

}

unsigned int OnAuth(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE
	_irqL irqL;
	unsigned int	auth_mode, seq, ie_len;
	unsigned char	*sa, *p;	
	u16	algorithm;
	int	status;
	static struct sta_info stat;	
	struct	sta_info	*pstat=NULL;	
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data; 
	uint len = precv_frame->u.hdr.len;
	u8	offset = 0;

	
#ifdef CONFIG_CONCURRENT_MODE	
	if(((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE) &&
		check_buddy_fwstate(padapter, _FW_UNDER_LINKING|_FW_UNDER_SURVEY))
	{
		//don't process auth request;
		return _SUCCESS;
	}
#endif //CONFIG_CONCURRENT_MODE

	if((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return _FAIL;

	DBG_871X("+OnAuth\n");

	sa = GetAddr2Ptr(pframe);

	auth_mode = psecuritypriv->dot11AuthAlgrthm;

	if (GetPrivacy(pframe))
	{
		u8	*iv;
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

	algorithm = le16_to_cpu(*(u16*)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset));
	seq 	= le16_to_cpu(*(u16*)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset + 2));

	DBG_871X("auth alg=%x, seq=%X\n", algorithm, seq);

	if (auth_mode == 2 &&
			psecuritypriv->dot11PrivacyAlgrthm != _WEP40_ &&
			psecuritypriv->dot11PrivacyAlgrthm != _WEP104_)
		auth_mode = 0;

	if ((algorithm > 0 && auth_mode == 0) ||	// rx a shared-key auth but shared not enabled
		(algorithm == 0 && auth_mode == 1) )	// rx a open-system auth but shared-key is enabled
	{		
		DBG_871X("auth rejected due to bad alg [alg=%d, auth_mib=%d] %02X%02X%02X%02X%02X%02X\n",
			algorithm, auth_mode, sa[0], sa[1], sa[2], sa[3], sa[4], sa[5]);
		
		status = _STATS_NO_SUPP_ALG_;
		
		goto auth_fail;
	}
	
#if 0 //ACL control	
	phead = &priv->wlan_acl_list;
	plist = phead->next;
	//check sa
	if (acl_mode == 1)		// 1: positive check, only those on acl_list can be connected.
		res = FAIL;
	else
		res = SUCCESS;

	while(plist != phead)
	{
		paclnode = list_entry(plist, struct rtw_wlan_acl_node, list);
		plist = plist->next;
		if (!memcmp((void *)sa, paclnode->addr, 6)) {
			if (paclnode->mode & 2) { // deny
				res = FAIL;
				break;
			}
			else {
				res = SUCCESS;
				break;
			}
		}
	}

	if (res != SUCCESS) {
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,"auth abort because ACL!\n");
		return FAIL;
	}
#else
	if(rtw_access_ctrl(padapter, sa) == _FALSE)
	{
		status = _STATS_UNABLE_HANDLE_STA_;
		goto auth_fail;
	}	
#endif

	pstat = rtw_get_stainfo(pstapriv, sa);
	if (pstat == NULL)
	{

		// allocate a new one
		DBG_871X("going to alloc stainfo for sa="MAC_FMT"\n",  MAC_ARG(sa));
		pstat = rtw_alloc_stainfo(pstapriv, sa);
		if (pstat == NULL)
		{
			DBG_871X(" Exceed the upper limit of supported clients...\n");
			status = _STATS_UNABLE_HANDLE_STA_;
			goto auth_fail;
		}
		
		pstat->state = WIFI_FW_AUTH_NULL;
		pstat->auth_seq = 0;
		
		//pstat->flags = 0;
		//pstat->capability = 0;
	} else {
#ifdef CONFIG_IEEE80211W
		if (pstat->bpairwise_key_installed != _TRUE && !(pstat->state & WIFI_FW_ASSOC_SUCCESS))
#endif /* CONFIG_IEEE80211W */
		{

			_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
			if (rtw_is_list_empty(&pstat->asoc_list) == _FALSE) {			
				rtw_list_delete(&pstat->asoc_list);
				pstapriv->asoc_list_cnt--;
				if (pstat->expire_to > 0)
					;/* TODO: STA re_auth within expire_to */
			}
			_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

			if (seq == 1)
				; /* TODO: STA re_auth and auth timeout */

		}
	}

#ifdef CONFIG_IEEE80211W
	if (pstat->bpairwise_key_installed != _TRUE && !(pstat->state & WIFI_FW_ASSOC_SUCCESS)) 
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


	if ((pstat->auth_seq + 1) != seq)
	{
		DBG_871X("(1)auth rejected because out of seq [rx_seq=%d, exp_seq=%d]!\n",
			seq, pstat->auth_seq+1);
		status = _STATS_OUT_OF_AUTH_SEQ_;
		goto auth_fail;
	}

	if (algorithm==0 && (auth_mode == 0 || auth_mode == 2 || auth_mode == 3))
	{
		if (seq == 1)
		{
#ifdef CONFIG_IEEE80211W
			if (pstat->bpairwise_key_installed != _TRUE && !(pstat->state & WIFI_FW_ASSOC_SUCCESS)) 
#endif /* CONFIG_IEEE80211W */
			{
				pstat->state &= ~WIFI_FW_AUTH_NULL;
				pstat->state |= WIFI_FW_AUTH_SUCCESS;
				pstat->expire_to = pstapriv->assoc_to;
			}
			pstat->authalg = algorithm;
		}
		else
		{
			DBG_871X("(2)auth rejected because out of seq [rx_seq=%d, exp_seq=%d]!\n",
				seq, pstat->auth_seq+1);
			status = _STATS_OUT_OF_AUTH_SEQ_;
			goto auth_fail;
		}
	}
	else // shared system or auto authentication
	{
		if (seq == 1)
		{
			//prepare for the challenging txt...

			//get_random_bytes((void *)pstat->chg_txt, 128);//TODO:
			_rtw_memset((void *)pstat->chg_txt, 78, 128);
#ifdef CONFIG_IEEE80211W
			if (pstat->bpairwise_key_installed != _TRUE && !(pstat->state & WIFI_FW_ASSOC_SUCCESS)) 
#endif /* CONFIG_IEEE80211W */
			{
				pstat->state &= ~WIFI_FW_AUTH_NULL;
				pstat->state |= WIFI_FW_AUTH_STATE;
			}
			pstat->authalg = algorithm;
			pstat->auth_seq = 2;
		}
		else if (seq == 3)
		{
			//checking for challenging txt...
			DBG_871X("checking for challenging txt...\n");
			
			p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + 4 + _AUTH_IE_OFFSET_ , _CHLGETXT_IE_, (int *)&ie_len,
					len - WLAN_HDR_A3_LEN - _AUTH_IE_OFFSET_ - 4);

			if((p==NULL) || (ie_len<=0))
			{
				DBG_871X("auth rejected because challenge failure!(1)\n");
				status = _STATS_CHALLENGE_FAIL_;
				goto auth_fail;
			}
			
			if (_rtw_memcmp((void *)(p + 2), pstat->chg_txt, 128))
			{
#ifdef CONFIG_IEEE80211W
				if (pstat->bpairwise_key_installed != _TRUE && !(pstat->state & WIFI_FW_ASSOC_SUCCESS)) 
#endif /* CONFIG_IEEE80211W */
				{
					pstat->state &= (~WIFI_FW_AUTH_STATE);
					pstat->state |= WIFI_FW_AUTH_SUCCESS;
					/* challenging txt is correct... */
					pstat->expire_to =  pstapriv->assoc_to;
				}
			}
			else
			{
				DBG_871X("auth rejected because challenge failure!\n");
				status = _STATS_CHALLENGE_FAIL_;
				goto auth_fail;
			}
		}
		else
		{
			DBG_871X("(3)auth rejected because out of seq [rx_seq=%d, exp_seq=%d]!\n",
				seq, pstat->auth_seq+1);
			status = _STATS_OUT_OF_AUTH_SEQ_;
			goto auth_fail;
		}
	}


	// Now, we are going to issue_auth...
	pstat->auth_seq = seq + 1;	
	
#ifdef CONFIG_NATIVEAP_MLME
	issue_auth(padapter, pstat, (unsigned short)(_STATS_SUCCESSFUL_));
#endif

	if ((pstat->state & WIFI_FW_AUTH_SUCCESS) || (pstat->state & WIFI_FW_ASSOC_SUCCESS))
		pstat->auth_seq = 0;

		
	return _SUCCESS;

auth_fail:

	if(pstat)
		rtw_free_stainfo(padapter , pstat);
	
	pstat = &stat;
	_rtw_memset((char *)pstat, '\0', sizeof(stat));
	pstat->auth_seq = 2;
	_rtw_memcpy(pstat->hwaddr, sa, 6);	
	
#ifdef CONFIG_NATIVEAP_MLME
	issue_auth(padapter, pstat, (unsigned short)status);	
#endif

#endif
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

	DBG_871X("%s\n", __FUNCTION__);

	//check A1 matches or not
	if (!_rtw_memcmp(adapter_mac_addr(padapter), get_da(pframe), ETH_ALEN))
		return _SUCCESS;

	if (!(pmlmeinfo->state & WIFI_FW_AUTH_STATE))
		return _SUCCESS;

	offset = (GetPrivacy(pframe))? 4: 0;

	algthm 	= le16_to_cpu(*(unsigned short *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset));
	seq 	= le16_to_cpu(*(unsigned short *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset + 2));
	status 	= le16_to_cpu(*(unsigned short *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset + 4));

	if (status != 0)
	{
		DBG_871X("clnt auth fail, status: %d\n", status);
		if(status == 13)//&& pmlmeinfo->auth_algo == dot11AuthAlgrthm_Auto)
		{
			if(pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared)
				pmlmeinfo->auth_algo = dot11AuthAlgrthm_Open;
			else
				pmlmeinfo->auth_algo = dot11AuthAlgrthm_Shared;
			//pmlmeinfo->reauth_count = 0;
		}
		
		set_link_timer(pmlmeext, 1);
		goto authclnt_fail;
	}

	if (seq == 2)
	{
		if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared)
		{
			 // legendary shared system
			p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _AUTH_IE_OFFSET_, _CHLGETXT_IE_, (int *)&len,
				pkt_len - WLAN_HDR_A3_LEN - _AUTH_IE_OFFSET_);

			if (p == NULL)
			{
				//DBG_871X("marc: no challenge text?\n");
				goto authclnt_fail;
			}

			_rtw_memcpy((void *)(pmlmeinfo->chg_txt), (void *)(p + 2), len);
			pmlmeinfo->auth_seq = 3;
			issue_auth(padapter, NULL, 0);
			set_link_timer(pmlmeext, REAUTH_TO);

			return _SUCCESS;
		}
		else
		{
			// open system
			go2asoc = 1;
		}
	}
	else if (seq == 4)
	{
		if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared)
		{
			go2asoc = 1;
		}
		else
		{
			goto authclnt_fail;
		}
	}
	else
	{
		// this is also illegal
		//DBG_871X("marc: clnt auth failed due to illegal seq=%x\n", seq);
		goto authclnt_fail;
	}

	if (go2asoc)
	{
		DBG_871X_LEVEL(_drv_always_, "auth success, start assoc\n");
		start_clnt_assoc(padapter);
		return _SUCCESS;
	}

authclnt_fail:

	//pmlmeinfo->state &= ~(WIFI_FW_AUTH_STATE);

	return _FAIL;

}

unsigned int OnAssocReq(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE
	_irqL irqL;
	u16 capab_info, listen_interval;
	struct rtw_ieee802_11_elems elems;	
	struct sta_info	*pstat;
	unsigned char		reassoc, *p, *pos, *wpa_ie;
	unsigned char WMM_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01};
	int		i, ie_len, wpa_ie_len, left;
	u8 rate_set[16];
	u8 rate_num;
	unsigned short		status = _STATS_SUCCESSFUL_;
	unsigned short		frame_type, ie_offset=0;	
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);	
	WLAN_BSSID_EX 	*cur = &(pmlmeinfo->network);
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint pkt_len = precv_frame->u.hdr.len;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	u8 p2p_status_code = P2P_STATUS_SUCCESS;
	u8 *p2pie;
	u32 p2pielen = 0;
#endif //CONFIG_P2P

#ifdef CONFIG_CONCURRENT_MODE
	if(((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE) &&
		check_buddy_fwstate(padapter, _FW_UNDER_LINKING|_FW_UNDER_SURVEY))
	{
		//don't process assoc request;
		return _SUCCESS;
	}
#endif //CONFIG_CONCURRENT_MODE

	if((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return _FAIL;
	
	frame_type = GetFrameSubType(pframe);
	if (frame_type == WIFI_ASSOCREQ)
	{
		reassoc = 0;
		ie_offset = _ASOCREQ_IE_OFFSET_;
	}	
	else // WIFI_REASSOCREQ
	{
		reassoc = 1;
		ie_offset = _REASOCREQ_IE_OFFSET_;
	}
	

	if (pkt_len < IEEE80211_3ADDR_LEN + ie_offset) {
		DBG_871X("handle_assoc(reassoc=%d) - too short payload (len=%lu)"
		       "\n", reassoc, (unsigned long)pkt_len);
		return _FAIL;
	}
	
	pstat = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
	if (pstat == (struct sta_info *)NULL)
	{
		status = _RSON_CLS2_;
		goto asoc_class2_error;
	}

	capab_info = RTW_GET_LE16(pframe + WLAN_HDR_A3_LEN);
	//capab_info = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN));	
	//listen_interval = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN+2));
	listen_interval = RTW_GET_LE16(pframe + WLAN_HDR_A3_LEN+2);

	left = pkt_len - (IEEE80211_3ADDR_LEN + ie_offset);
	pos = pframe + (IEEE80211_3ADDR_LEN + ie_offset);
	

	DBG_871X("%s\n", __FUNCTION__);

	// check if this stat has been successfully authenticated/assocated
	if (!((pstat->state) & WIFI_FW_AUTH_SUCCESS))
	{
		if (!((pstat->state) & WIFI_FW_ASSOC_SUCCESS))
		{
			status = _RSON_CLS2_;
			goto asoc_class2_error;
		}
		else
		{
			pstat->state &= (~WIFI_FW_ASSOC_SUCCESS);
			pstat->state |= WIFI_FW_ASSOC_STATE;				
		}
	}
	else
	{
		pstat->state &= (~WIFI_FW_AUTH_SUCCESS);
		pstat->state |= WIFI_FW_ASSOC_STATE;
	}


#if 0// todo:tkip_countermeasures
	if (hapd->tkip_countermeasures) {
		resp = WLAN_REASON_MICHAEL_MIC_FAILURE;
		goto fail;
	}
#endif

	pstat->capability = capab_info;

#if 0//todo:
	//check listen_interval
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

	//now parse all ieee802_11 ie to point to elems
	if (rtw_ieee802_11_parse_elems(pos, left, &elems, 1) == ParseFailed ||
	    !elems.ssid) {
		DBG_871X("STA " MAC_FMT " sent invalid association request\n",
		       MAC_ARG(pstat->hwaddr));
		status = _STATS_FAILURE_;		
		goto OnAssocReqFail;
	}


	// now we should check all the fields...
	// checking SSID
	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + ie_offset, _SSID_IE_, &ie_len,
		pkt_len - WLAN_HDR_A3_LEN - ie_offset);
	if (p == NULL)
	{
		status = _STATS_FAILURE_;		
	}

	if (ie_len == 0) // broadcast ssid, however it is not allowed in assocreq
		status = _STATS_FAILURE_;
	else
	{
		// check if ssid match
		if (!_rtw_memcmp((void *)(p+2), cur->Ssid.Ssid, cur->Ssid.SsidLength))
			status = _STATS_FAILURE_;

		if (ie_len != cur->Ssid.SsidLength)
			status = _STATS_FAILURE_;
	}

	if(_STATS_SUCCESSFUL_ != status)
		goto OnAssocReqFail;

	rtw_ies_get_supported_rate(pframe + WLAN_HDR_A3_LEN + ie_offset, pkt_len - WLAN_HDR_A3_LEN - ie_offset, rate_set, &rate_num);
	if (rate_num == 0) {
		DBG_871X(FUNC_ADPT_FMT" RX assoc-req with no supported rate\n", FUNC_ADPT_ARG(padapter));
		status = _STATS_FAILURE_;
		goto OnAssocReqFail;
	}
	_rtw_memcpy(pstat->bssrateset, rate_set, rate_num);
	pstat->bssratelen = rate_num;
	UpdateBrateTblForSoftAP(pstat->bssrateset, pstat->bssratelen);

	//check RSN/WPA/WPS
	pstat->dot8021xalg = 0;
      	pstat->wpa_psk = 0;
	pstat->wpa_group_cipher = 0;
	pstat->wpa2_group_cipher = 0;
	pstat->wpa_pairwise_cipher = 0;
	pstat->wpa2_pairwise_cipher = 0;
	_rtw_memset(pstat->wpa_ie, 0, sizeof(pstat->wpa_ie));
	if((psecuritypriv->wpa_psk & BIT(1)) && elems.rsn_ie) {

		int group_cipher=0, pairwise_cipher=0;	
		
		wpa_ie = elems.rsn_ie;
		wpa_ie_len = elems.rsn_ie_len;

		if(rtw_parse_wpa2_ie(wpa_ie-2, wpa_ie_len+2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS)
		{
			pstat->dot8021xalg = 1;//psk,  todo:802.1x						
			pstat->wpa_psk |= BIT(1);

			pstat->wpa2_group_cipher = group_cipher&psecuritypriv->wpa2_group_cipher;				
			pstat->wpa2_pairwise_cipher = pairwise_cipher&psecuritypriv->wpa2_pairwise_cipher;
			
			if(!pstat->wpa2_group_cipher)
				status = WLAN_STATUS_GROUP_CIPHER_NOT_VALID;

			if(!pstat->wpa2_pairwise_cipher)
				status = WLAN_STATUS_PAIRWISE_CIPHER_NOT_VALID;
		}
		else
		{
			status = WLAN_STATUS_INVALID_IE;
		}	
			
	} else if ((psecuritypriv->wpa_psk & BIT(0)) && elems.wpa_ie) {

		int group_cipher=0, pairwise_cipher=0;	
		
		wpa_ie = elems.wpa_ie;
		wpa_ie_len = elems.wpa_ie_len;

		if(rtw_parse_wpa_ie(wpa_ie-2, wpa_ie_len+2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS)
		{
			pstat->dot8021xalg = 1;//psk,  todo:802.1x						
			pstat->wpa_psk |= BIT(0);

			pstat->wpa_group_cipher = group_cipher&psecuritypriv->wpa_group_cipher;				
			pstat->wpa_pairwise_cipher = pairwise_cipher&psecuritypriv->wpa_pairwise_cipher;
			
			if(!pstat->wpa_group_cipher)
				status = WLAN_STATUS_GROUP_CIPHER_NOT_VALID;

			if(!pstat->wpa_pairwise_cipher)
				status = WLAN_STATUS_PAIRWISE_CIPHER_NOT_VALID;
		
		}
		else
		{
			status = WLAN_STATUS_INVALID_IE;
		}
		
	} else {
		wpa_ie = NULL;
		wpa_ie_len = 0;
	}

	if(_STATS_SUCCESSFUL_ != status)
		goto OnAssocReqFail;

	pstat->flags &= ~(WLAN_STA_WPS | WLAN_STA_MAYBE_WPS);
	//if (hapd->conf->wps_state && wpa_ie == NULL) { //todo: to check ap if supporting WPS
	if(wpa_ie == NULL) {
		if (elems.wps_ie) {
			DBG_871X("STA included WPS IE in "
				   "(Re)Association Request - assume WPS is "
				   "used\n");
			pstat->flags |= WLAN_STA_WPS;
			//wpabuf_free(sta->wps_ie);
			//sta->wps_ie = wpabuf_alloc_copy(elems.wps_ie + 4,
			//				elems.wps_ie_len - 4);
		} else {
			DBG_871X("STA did not include WPA/RSN IE "
				   "in (Re)Association Request - possible WPS "
				   "use\n");
			pstat->flags |= WLAN_STA_MAYBE_WPS;
		}


		// AP support WPA/RSN, and sta is going to do WPS, but AP is not ready
		// that the selected registrar of AP is _FLASE
		if((psecuritypriv->wpa_psk >0)  
			&& (pstat->flags & (WLAN_STA_WPS|WLAN_STA_MAYBE_WPS)))
		{
			if(pmlmepriv->wps_beacon_ie)
			{	
				u8 selected_registrar = 0;
				
				rtw_get_wps_attr_content(pmlmepriv->wps_beacon_ie, pmlmepriv->wps_beacon_ie_len, WPS_ATTR_SELECTED_REGISTRAR , &selected_registrar, NULL);

				if(!selected_registrar)
				{						
					DBG_871X("selected_registrar is _FALSE , or AP is not ready to do WPS\n");
						
					status = _STATS_UNABLE_HANDLE_STA_;
			
					goto OnAssocReqFail;
				}						
			}			
		}
			
	}
	else
	{
		int copy_len;

		if(psecuritypriv->wpa_psk == 0)
		{
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
			copy_len=0;
		}
		else
		{
			copy_len = ((wpa_ie_len+2) > sizeof(pstat->wpa_ie)) ? (sizeof(pstat->wpa_ie)):(wpa_ie_len+2);
		}

		
		if(copy_len>0)
			_rtw_memcpy(pstat->wpa_ie, wpa_ie-2, copy_len);
		
	}


	// check if there is WMM IE & support WWM-PS
	pstat->flags &= ~WLAN_STA_WME;
	pstat->qos_option = 0;
	pstat->qos_info = 0;
	pstat->has_legacy_ac = _TRUE;
	pstat->uapsd_vo = 0;
	pstat->uapsd_vi = 0;
	pstat->uapsd_be = 0;
	pstat->uapsd_bk = 0;
	if (pmlmepriv->qospriv.qos_option) 
	{
		p = pframe + WLAN_HDR_A3_LEN + ie_offset; ie_len = 0;
		for (;;) 
		{
			p = rtw_get_ie(p, _VENDOR_SPECIFIC_IE_, &ie_len, pkt_len - WLAN_HDR_A3_LEN - ie_offset);
			if (p != NULL) {
				if (_rtw_memcmp(p+2, WMM_IE, 6)) {

					pstat->flags |= WLAN_STA_WME;
					
					pstat->qos_option = 1;				
					pstat->qos_info = *(p+8);
					
					pstat->max_sp_len = (pstat->qos_info>>5)&0x3;

					if((pstat->qos_info&0xf) !=0xf)
						pstat->has_legacy_ac = _TRUE;
					else
						pstat->has_legacy_ac = _FALSE;
					
					if(pstat->qos_info&0xf)
					{
						if(pstat->qos_info&BIT(0))
							pstat->uapsd_vo = BIT(0)|BIT(1);
						else
							pstat->uapsd_vo = 0;
		
						if(pstat->qos_info&BIT(1))
							pstat->uapsd_vi = BIT(0)|BIT(1);
						else
							pstat->uapsd_vi = 0;
			
						if(pstat->qos_info&BIT(2))
							pstat->uapsd_bk = BIT(0)|BIT(1);
						else
							pstat->uapsd_bk = 0;
			
						if(pstat->qos_info&BIT(3))			
							pstat->uapsd_be = BIT(0)|BIT(1);
						else
							pstat->uapsd_be = 0;
		
					}
	
					break;
				}
			}
			else {
				break;
			}
			p = p + ie_len + 2;
		}
	}


#ifdef CONFIG_80211N_HT
	if (pmlmepriv->htpriv.ht_option == _FALSE)
		goto bypass_ht_chk;

	/* save HT capabilities in the sta object */
	_rtw_memset(&pstat->htpriv.ht_cap, 0, sizeof(struct rtw_ieee80211_ht_cap));
	if (elems.ht_capabilities && elems.ht_capabilities_len >= sizeof(struct rtw_ieee80211_ht_cap)) 
	{
		pstat->flags |= WLAN_STA_HT;
		
		pstat->flags |= WLAN_STA_WME;
		
		_rtw_memcpy(&pstat->htpriv.ht_cap, elems.ht_capabilities, sizeof(struct rtw_ieee80211_ht_cap));			
		
	} else
		pstat->flags &= ~WLAN_STA_HT;
bypass_ht_chk:

	if ((pmlmepriv->htpriv.ht_option == _FALSE) && (pstat->flags&WLAN_STA_HT)) {
		rtw_warn_on(1);
		status = _STATS_FAILURE_;
		goto OnAssocReqFail;
	}
#endif /* CONFIG_80211N_HT */

#ifdef CONFIG_80211AC_VHT
	if (pmlmepriv->vhtpriv.vht_option == _FALSE)
		goto bypass_vht_chk;

	_rtw_memset(&pstat->vhtpriv, 0, sizeof(struct vht_priv));
	if (elems.vht_capabilities && elems.vht_capabilities_len == 12) {
		pstat->flags |= WLAN_STA_VHT;

		_rtw_memcpy(pstat->vhtpriv.vht_cap, elems.vht_capabilities, 12);

		if (elems.vht_op_mode_notify && elems.vht_op_mode_notify_len == 1) {
			_rtw_memcpy(&pstat->vhtpriv.vht_op_mode_notify, elems.vht_op_mode_notify, 1);
		}
		else // for Frame without Operating Mode notify ie; default: 80M
		{
			pstat->vhtpriv.vht_op_mode_notify = CHANNEL_WIDTH_80;
		}
	}
	else {
		pstat->flags &= ~WLAN_STA_VHT;
	}
bypass_vht_chk:

	if ((pmlmepriv->vhtpriv.vht_option == _FALSE) && (pstat->flags&WLAN_STA_VHT)) {
		rtw_warn_on(1);
		status = _STATS_FAILURE_;
		goto OnAssocReqFail;
	}
#endif /* CONFIG_80211AC_VHT */

	if (((pstat->flags & WLAN_STA_HT) || (pstat->flags & WLAN_STA_VHT)) && 
		((pstat->wpa2_pairwise_cipher & WPA_CIPHER_TKIP) || 
		(pstat->wpa_pairwise_cipher & WPA_CIPHER_TKIP))) {

		DBG_871X("(V)HT: " MAC_FMT " tried to use TKIP with (V)HT association\n", MAC_ARG(pstat->hwaddr));

		pstat->flags &= ~WLAN_STA_HT;
		pstat->flags &= ~WLAN_STA_VHT;
		/*status = WLAN_STATUS_CIPHER_REJECTED_PER_POLICY;
		  * goto OnAssocReqFail;
		*/
	}


       //
       //if (hapd->iface->current_mode->mode == HOSTAPD_MODE_IEEE80211G)//?
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

#ifdef CONFIG_P2P
	pstat->is_p2p_device = _FALSE;
	if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
	{		
		if( (p2pie=rtw_get_p2p_ie(pframe + WLAN_HDR_A3_LEN + ie_offset , pkt_len - WLAN_HDR_A3_LEN - ie_offset , NULL, &p2pielen)))
		{
			pstat->is_p2p_device = _TRUE;
			if((p2p_status_code=(u8)process_assoc_req_p2p_ie(pwdinfo, pframe, pkt_len, pstat))>0)
			{
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
#endif //CONFIG_P2P

	//TODO: identify_proprietary_vendor_ie();
	// Realtek proprietary IE
	// identify if this is Broadcom sta
	// identify if this is ralink sta
	// Customer proprietary IE

	

	/* get a unique AID */
	if (pstat->aid > 0) {
		DBG_871X("  old AID %d\n", pstat->aid);
	} else {
		for (pstat->aid = 1; pstat->aid <= NUM_STA; pstat->aid++) {
			if (pstapriv->sta_aid[pstat->aid - 1] == NULL) {
				if (pstat->aid > pstapriv->max_num_sta) {
					pstat->aid = 0;
				
					DBG_871X("  no room for more AIDs\n");

					status = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;
				
					goto OnAssocReqFail;
				
			
				} else {
					pstapriv->sta_aid[pstat->aid - 1] = pstat;
					DBG_871X("allocate new AID = (%d)\n", pstat->aid);
					break;
				}				
			}
		}	
	}


	pstat->state &= (~WIFI_FW_ASSOC_STATE);	
	pstat->state |= WIFI_FW_ASSOC_SUCCESS;
	/* DBG_871X("==================%s, %d,  (%x), bpairwise_key_installed=%d, MAC:"MAC_FMT"\n"
	, __func__, __LINE__, pstat->state, pstat->bpairwise_key_installed, MAC_ARG(pstat->hwaddr)); */
#ifdef CONFIG_IEEE80211W
	if (pstat->bpairwise_key_installed != _TRUE)
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
		}
		_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	}

	// now the station is qualified to join our BSS...	
	if(pstat && (pstat->state & WIFI_FW_ASSOC_SUCCESS) && (_STATS_SUCCESSFUL_==status))
	{
#ifdef CONFIG_NATIVEAP_MLME
#ifdef CONFIG_IEEE80211W
		if (pstat->bpairwise_key_installed != _TRUE)
#endif /* CONFIG_IEEE80211W */
		{
			/* .1 bss_cap_update & sta_info_update */
			bss_cap_update_on_sta_join(padapter, pstat);
			sta_info_update(padapter, pstat);
		}
#ifdef CONFIG_IEEE80211W
		if (pstat->bpairwise_key_installed == _TRUE)
			status = _STATS_REFUSED_TEMPORARILY_;
#endif /* CONFIG_IEEE80211W */
		//.2 issue assoc rsp before notify station join event.
		if (frame_type == WIFI_ASSOCREQ)
			issue_asocrsp(padapter, status, pstat, WIFI_ASSOCRSP);
		else
			issue_asocrsp(padapter, status, pstat, WIFI_REASSOCRSP);

#ifdef CONFIG_IOCTL_CFG80211
		_enter_critical_bh(&pstat->lock, &irqL);
		if(pstat->passoc_req)
		{
			rtw_mfree(pstat->passoc_req, pstat->assoc_req_len);
			pstat->passoc_req = NULL;
			pstat->assoc_req_len = 0;
		}

		pstat->passoc_req =  rtw_zmalloc(pkt_len);
		if(pstat->passoc_req)
		{
			_rtw_memcpy(pstat->passoc_req, pframe, pkt_len);
			pstat->assoc_req_len = pkt_len;
		}
		_exit_critical_bh(&pstat->lock, &irqL);
#endif //CONFIG_IOCTL_CFG80211
#ifdef CONFIG_IEEE80211W
		if (pstat->bpairwise_key_installed != _TRUE)
#endif /* CONFIG_IEEE80211W */
		{
			/* .3-(1) report sta add event */
			report_add_sta_event(padapter, pstat->hwaddr);
		}
#ifdef CONFIG_IEEE80211W
		if (pstat->bpairwise_key_installed == _TRUE && padapter->securitypriv.binstallBIPkey == _TRUE) {
			DBG_871X(MAC_FMT"\n", MAC_ARG(pstat->hwaddr));
			issue_action_SA_Query(padapter, pstat->hwaddr, 0, 0, IEEE80211W_RIGHT_KEY);
		}
#endif /* CONFIG_IEEE80211W */
#endif //CONFIG_NATIVEAP_MLME
	}

	return _SUCCESS;

asoc_class2_error:

#ifdef CONFIG_NATIVEAP_MLME
	issue_deauth(padapter, (void *)GetAddr2Ptr(pframe), status);
#endif

	return _FAIL;		

OnAssocReqFail:


#ifdef CONFIG_NATIVEAP_MLME
	pstat->aid = 0;
	if (frame_type == WIFI_ASSOCREQ)
		issue_asocrsp(padapter, status, pstat, WIFI_ASSOCRSP);
	else
		issue_asocrsp(padapter, status, pstat, WIFI_REASSOCRSP);
#endif


#endif /* CONFIG_AP_MODE */

	return _FAIL;		

}

unsigned int OnAssocRsp(_adapter *padapter, union recv_frame *precv_frame)
{
	uint i;
	int res;
	unsigned short	status;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	//WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint pkt_len = precv_frame->u.hdr.len;
	PNDIS_802_11_VARIABLE_IEs	pWapiIE = NULL;

	DBG_871X("%s\n", __FUNCTION__);
	
	//check A1 matches or not
	if (!_rtw_memcmp(adapter_mac_addr(padapter), get_da(pframe), ETH_ALEN))
		return _SUCCESS;

	if (!(pmlmeinfo->state & (WIFI_FW_AUTH_SUCCESS | WIFI_FW_ASSOC_STATE)))
		return _SUCCESS;

	if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
		return _SUCCESS;

	_cancel_timer_ex(&pmlmeext->link_timer);

	//status
	if ((status = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN + 2))) > 0)
	{
		DBG_871X("assoc reject, status code: %d\n", status);
		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		res = -4;
		goto report_assoc_result;
	}

	//get capabilities
	pmlmeinfo->capability = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN));

	//set slot time
	pmlmeinfo->slotTime = (pmlmeinfo->capability & BIT(10))? 9: 20;

	//AID
	res = pmlmeinfo->aid = (int)(le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN + 4))&0x3fff);

	//following are moved to join event callback function
	//to handle HT, WMM, rate adaptive, update MAC reg
	//for not to handle the synchronous IO in the tasklet
	for (i = (6 + WLAN_HDR_A3_LEN); i < pkt_len;)
	{
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pframe + i);

		switch (pIE->ElementID)
		{
			case _VENDOR_SPECIFIC_IE_:
				if (_rtw_memcmp(pIE->data, WMM_PARA_OUI, 6))	//WMM
				{
					WMM_param_handler(padapter, pIE);
				}
#if defined(CONFIG_P2P) && defined(CONFIG_WFD)
				else if ( _rtw_memcmp(pIE->data, WFD_OUI, 4))		//WFD
				{
					rtw_process_wfd_ie(padapter, (u8 *)pIE, pIE->Length, __func__);
				}
#endif				
				break;

#ifdef CONFIG_WAPI_SUPPORT
			case _WAPI_IE_:
				pWapiIE = pIE;
				break;
#endif

			case _HT_CAPABILITY_IE_:	//HT caps
				HT_caps_handler(padapter, pIE);
				break;

			case _HT_EXTRA_INFO_IE_:	//HT info
				HT_info_handler(padapter, pIE);
				break;

#ifdef CONFIG_80211AC_VHT
			case EID_VHTCapability:
				VHT_caps_handler(padapter, pIE);
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

	//Update Basic Rate Table for spec, 2010-12-28 , by thomas
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

unsigned int OnDeAuth(_adapter *padapter, union recv_frame *precv_frame)
{
	unsigned short	reason;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
#endif //CONFIG_P2P

	//check A3
	if (!(_rtw_memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN)))
		return _SUCCESS;

	DBG_871X(FUNC_ADPT_FMT" - Start to Disconnect\n", FUNC_ADPT_ARG(padapter));

#ifdef CONFIG_P2P
	if ( pwdinfo->rx_invitereq_info.scan_op_ch_only )
	{
		_cancel_timer_ex( &pwdinfo->reset_ch_sitesurvey );
		_set_timer( &pwdinfo->reset_ch_sitesurvey, 10 );
	}
#endif //CONFIG_P2P

	reason = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN));

	rtw_lock_rx_suspend_timeout(8000);

#ifdef CONFIG_AP_MODE
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{		
		_irqL irqL;
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;
		
		//_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		
		//rtw_free_stainfo(padapter, psta);
		//_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		

		DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" reason=%u, ta=%pM\n"
			, FUNC_ADPT_ARG(padapter), reason, GetAddr2Ptr(pframe));

		psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));	
		if(psta)
		{
			u8 updated = _FALSE;
		
			_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
			if(rtw_is_list_empty(&psta->asoc_list)==_FALSE)
			{			
				rtw_list_delete(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;
				updated = ap_free_sta(padapter, psta, _FALSE, reason, _TRUE);

			}
			_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

			associated_clients_update(padapter, updated, STA_INFO_UPDATE_ALL);
		}
		

		return _SUCCESS;
	}
	else
#endif
	{
		int	ignore_received_deauth = 0;

		//	Commented by Albert 20130604
		//	Before sending the auth frame to start the STA/GC mode connection with AP/GO, 
		//	we will send the deauth first.
		//	However, the Win8.1 with BRCM Wi-Fi will send the deauth with reason code 6 to us after receieving our deauth.
		//	Added the following code to avoid this case.
		if ( ( pmlmeinfo->state & WIFI_FW_AUTH_STATE ) ||
			( pmlmeinfo->state & WIFI_FW_ASSOC_STATE ) )
		{
			if ( reason == WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA )
			{
				ignore_received_deauth = 1;
			} else if (WLAN_REASON_PREV_AUTH_NOT_VALID == reason) {
				// TODO: 802.11r
				ignore_received_deauth = 1;
			}
		}

		DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" reason=%u, ta=%pM, ignore=%d\n"
			, FUNC_ADPT_ARG(padapter), reason, GetAddr2Ptr(pframe), ignore_received_deauth);

		if ( 0 == ignore_received_deauth )
		{
			receive_disconnect(padapter, GetAddr2Ptr(pframe), reason, _FALSE);
		}
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
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
#endif //CONFIG_P2P

	//check A3
	if (!(_rtw_memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN)))
		return _SUCCESS;

	DBG_871X(FUNC_ADPT_FMT" - Start to Disconnect\n", FUNC_ADPT_ARG(padapter));

#ifdef CONFIG_P2P
	if ( pwdinfo->rx_invitereq_info.scan_op_ch_only )
	{
		_cancel_timer_ex( &pwdinfo->reset_ch_sitesurvey );
		_set_timer( &pwdinfo->reset_ch_sitesurvey, 10 );
	}
#endif //CONFIG_P2P

	reason = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN));

	rtw_lock_rx_suspend_timeout(8000);
	
#ifdef CONFIG_AP_MODE
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{	
		_irqL irqL;
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;
		
		//_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	
		//rtw_free_stainfo(padapter, psta);
		//_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		

		DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" reason=%u, ta=%pM\n"
			, FUNC_ADPT_ARG(padapter), reason, GetAddr2Ptr(pframe));

		psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));	
		if(psta)
		{
			u8 updated = _FALSE;
			
			_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
			if(rtw_is_list_empty(&psta->asoc_list)==_FALSE)
			{
				rtw_list_delete(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;
				updated = ap_free_sta(padapter, psta, _FALSE, reason, _TRUE);
			
			}
			_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

			associated_clients_update(padapter, updated, STA_INFO_UPDATE_ALL);
		}

		return _SUCCESS;
	}
	else
#endif
	{
		DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" reason=%u, ta=%pM\n"
			, FUNC_ADPT_ARG(padapter), reason, GetAddr2Ptr(pframe));

		receive_disconnect(padapter, GetAddr2Ptr(pframe), reason, _FALSE);
	}	
	pmlmepriv->LinkDetectInfo.bBusyTraffic = _FALSE;
	return _SUCCESS;

}

unsigned int OnAtim(_adapter *padapter, union recv_frame *precv_frame)
{
	DBG_871X("%s\n", __FUNCTION__);
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

		DBG_871X(FUNC_NDEV_FMT" from "MAC_FMT"\n",
			FUNC_NDEV_ARG(padapter->pnetdev), MAC_ARG(psta->hwaddr));

		for_each_ie(ie, ies, ies_len) {
			if (ie->id == WLAN_EID_CHANNEL_SWITCH) {
				ch_switch_mode = ie->data[0];
				ch = ie->data[1];
				ch_switch_cnt = ie->data[2];
				DBG_871X("ch_switch_mode:%d, ch:%d, ch_switch_cnt:%d\n",
					ch_switch_mode, ch, ch_switch_cnt);
			}
			else if (ie->id == WLAN_EID_SECONDARY_CHANNEL_OFFSET) {
				ch_offset = secondary_ch_offset_to_hal_ch_offset(ie->data[0]);
				DBG_871X("ch_offset:%d\n", ch_offset);
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

		ret = rtw_set_ch_cmd(padapter, ch, bwmode, ch_offset, _TRUE);
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

	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(padapter->pnetdev));

	psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));

	if (!psta)
		goto exit;

	category = frame_body[0];
	if(category != RTW_WLAN_CATEGORY_SPECTRUM_MGMT)
		goto exit;

	action = frame_body[1];
	switch (action) {
	case RTW_WLAN_ACTION_SPCT_MSR_REQ:
	case RTW_WLAN_ACTION_SPCT_MSR_RPRT:
	case RTW_WLAN_ACTION_SPCT_TPC_REQ:
	case RTW_WLAN_ACTION_SPCT_TPC_RPRT:
		break;
	case RTW_WLAN_ACTION_SPCT_CHL_SWITCH:
		#ifdef CONFIG_SPCT_CH_SWITCH
		ret = on_action_spct_ch_switch(padapter, psta, &frame_body[2],
			frame_len-(frame_body-pframe)-2);
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

	if (adapter->fix_rx_ampdu_size != RX_AMPDU_SIZE_INVALID) {
		size = adapter->fix_rx_ampdu_size;
		goto exit;
	}

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
			DBG_871X(FUNC_ADPT_FMT" fix_rx_ampdu_size:%u\n", FUNC_ADPT_ARG(adapter), size);
		}
	} else if (reason == RX_AMPDU_DRV_SCAN) {
		struct ss_res *ss = &adapter->mlmeextpriv.sitesurvey_res;

		if (ss->rx_ampdu_size != size) {
			ss->rx_ampdu_size = size;
			is_adj = _TRUE;
			DBG_871X(FUNC_ADPT_FMT" ss.rx_ampdu_size:%u\n", FUNC_ADPT_ARG(adapter), size);
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
			DBG_871X(FUNC_ADPT_FMT" fix_rx_ampdu_accept:%u\n", FUNC_ADPT_ARG(adapter), accept);
		}
	} else if (reason == RX_AMPDU_DRV_SCAN) {
		if (adapter->mlmeextpriv.sitesurvey_res.rx_ampdu_accept != accept) {
			adapter->mlmeextpriv.sitesurvey_res.rx_ampdu_accept = accept;
			is_adj = _TRUE;
			DBG_871X(FUNC_ADPT_FMT" ss.rx_ampdu_accept:%u\n", FUNC_ADPT_ARG(adapter), accept);
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
	struct mlme_ext_priv *mlmeext;
	struct sta_info *sta;
	u8 accept = rtw_rx_ampdu_is_accept(adapter);
	u8 size = rtw_rx_ampdu_size(adapter);

	mlmeext = &adapter->mlmeextpriv;

	if (mlmeext_msr(mlmeext) == WIFI_FW_STATION_STATE) {
		sta = rtw_get_stainfo(&adapter->stapriv, get_bssid(&adapter->mlmepriv));
		if (sta)
			adj_cnt += rx_ampdu_apply_sta(adapter, sta, accept, size);

	} else if (mlmeext_msr(mlmeext) == WIFI_FW_AP_STATE) {
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
			if (sta)
				adj_cnt += rx_ampdu_apply_sta(adapter, sta, accept, size);
		}
	}

	return adj_cnt;
}

unsigned int OnAction_back(_adapter *padapter, union recv_frame *precv_frame)
{
	u8 *addr;
	struct sta_info *psta=NULL;
	struct recv_reorder_ctrl *preorder_ctrl;
	unsigned char		*frame_body;
	unsigned char		category, action;
	unsigned short	tid, status, reason_code = 0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	struct sta_priv *pstapriv = &padapter->stapriv;
#ifdef CONFIG_80211N_HT

	DBG_871X("%s\n", __FUNCTION__);

	//check RA matches or not	
	if (!_rtw_memcmp(adapter_mac_addr(padapter), GetAddr1Ptr(pframe), ETH_ALEN))
		return _SUCCESS;

/*
	//check A1 matches or not
	if (!_rtw_memcmp(adapter_mac_addr(padapter), get_da(pframe), ETH_ALEN))
		return _SUCCESS;
*/

	if((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)	
		if (!(pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS))
			return _SUCCESS;

	addr = GetAddr2Ptr(pframe);
	psta = rtw_get_stainfo(pstapriv, addr);

	if(psta==NULL)
		return _SUCCESS;

	frame_body = (unsigned char *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));

	category = frame_body[0];
	if (category == RTW_WLAN_CATEGORY_BACK)// representing Block Ack
	{
#ifdef CONFIG_TDLS
		if((psta->tdls_sta_state & TDLS_LINKED_STATE) && 
			(psta->htpriv.ht_option==_TRUE) &&
			(psta->htpriv.ampdu_enable==_TRUE))
		{
			DBG_871X("Recv [%s] from direc link\n", __FUNCTION__);
		}
		else
#endif //CONFIG_TDLS
		if (!pmlmeinfo->HT_enable)
		{
			return _SUCCESS;
		}

		action = frame_body[1];
		DBG_871X("%s, action=%d\n", __FUNCTION__, action);
		switch (action)
		{
			case RTW_WLAN_ACTION_ADDBA_REQ: //ADDBA request

				_rtw_memcpy(&(pmlmeinfo->ADDBA_req), &(frame_body[2]), sizeof(struct ADDBA_request));
				//process_addba_req(padapter, (u8*)&(pmlmeinfo->ADDBA_req), GetAddr3Ptr(pframe));
				process_addba_req(padapter, (u8*)&(pmlmeinfo->ADDBA_req), addr);
								
				break;

			case RTW_WLAN_ACTION_ADDBA_RESP: //ADDBA response

				//status = frame_body[3] | (frame_body[4] << 8); //endian issue
				status = RTW_GET_LE16(&frame_body[3]);
				tid = ((frame_body[5] >> 2) & 0x7);

				if (status == 0)
				{	//successful					
					DBG_871X("agg_enable for TID=%d\n", tid);
					psta->htpriv.agg_enable_bitmap |= 1 << tid;					
					psta->htpriv.candidate_tid_bitmap &= ~BIT(tid);				
				}
				else
				{					
					psta->htpriv.agg_enable_bitmap &= ~BIT(tid);					
				}

				if(psta->state & WIFI_STA_ALIVE_CHK_STATE)
				{
					DBG_871X("%s alive check - rx ADDBA response\n", __func__);
					psta->htpriv.agg_enable_bitmap &= ~BIT(tid);
					psta->expire_to = pstapriv->expire_to;
					psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
				}

				//DBG_871X("marc: ADDBA RSP: %x\n", pmlmeinfo->agg_enable_bitmap);
				break;

			case RTW_WLAN_ACTION_DELBA: //DELBA
				if ((frame_body[3] & BIT(3)) == 0)
				{
					psta->htpriv.agg_enable_bitmap &= ~(1 << ((frame_body[3] >> 4) & 0xf));
					psta->htpriv.candidate_tid_bitmap &= ~(1 << ((frame_body[3] >> 4) & 0xf));
					
					//reason_code = frame_body[4] | (frame_body[5] << 8);
					reason_code = RTW_GET_LE16(&frame_body[4]);
				}
				else if((frame_body[3] & BIT(3)) == BIT(3))
				{						
					tid = (frame_body[3] >> 4) & 0x0F;
				
					preorder_ctrl = &psta->recvreorder_ctrl[tid];
					preorder_ctrl->enable = _FALSE;
					preorder_ctrl->ampdu_size = RX_AMPDU_SIZE_INVALID;
				}
				
				DBG_871X("%s(): DELBA: %x(%x)\n", __FUNCTION__,pmlmeinfo->agg_enable_bitmap, reason_code);
				//todo: how to notify the host while receiving DELETE BA
				break;

			default:
				break;
		}
	}
#endif //CONFIG_80211N_HT
	return _SUCCESS;
}

#ifdef CONFIG_P2P

static int get_reg_classes_full_count(struct p2p_channels channel_list) {
	int cnt = 0;
	int i;

	for (i = 0; i < channel_list.reg_classes; i++) {
		cnt += channel_list.reg_class[i].channels;
	}

	return cnt;
}

static void get_channel_cnt_24g_5gl_5gh(  struct mlme_ext_priv *pmlmeext, u8* p24g_cnt, u8* p5gl_cnt, u8* p5gh_cnt )
{
	int	i = 0;

	*p24g_cnt = 0;
	*p5gl_cnt = 0;
	*p5gh_cnt = 0;	
	
	for( i = 0; i < pmlmeext->max_chan_nums; i++ )
	{
		if ( pmlmeext->channel_set[ i ].ChannelNum <= 14 )
		{
			(*p24g_cnt)++;
		}
		else if ( ( pmlmeext->channel_set[ i ].ChannelNum > 14 ) && ( pmlmeext->channel_set[ i ].ChannelNum <= 48 ) )
		{
			//	Just include the channel 36, 40, 44, 48 channels for 5G low
			(*p5gl_cnt)++;
		}
		else if ( ( pmlmeext->channel_set[ i ].ChannelNum >= 149 ) && ( pmlmeext->channel_set[ i ].ChannelNum <= 161 ) )
		{
			//	Just include the channel 149, 153, 157, 161 channels for 5G high
			(*p5gh_cnt)++;
		}
	}
}

void issue_p2p_GO_request(_adapter *padapter, u8* raddr)
{

	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_GO_NEGO_REQ;
	u8			wpsie[ 255 ] = { 0x00 }, p2pie[ 255 ] = { 0x00 };
	u8			wpsielen = 0, p2pielen = 0, i;
	u8			channel_cnt_24g = 0, channel_cnt_5gl = 0, channel_cnt_5gh = 0;
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
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo);


	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	DBG_871X( "[%s] In\n", __FUNCTION__ );
	//update attribute
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
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));	
	pwdinfo->negotiation_dialog_token = 1;	//	Initialize the dialog value
	pframe = rtw_set_fixed_ie(pframe, 1, &pwdinfo->negotiation_dialog_token, &(pattrib->pktlen));

	

	//	WPS Section
	wpsielen = 0;
	//	WPS OUI
	*(u32*) ( wpsie ) = cpu_to_be32( WPSOUI );
	wpsielen += 4;

	//	WPS version
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_VER1 );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
	wpsielen += 2;

	//	Value:
	wpsie[wpsielen++] = WPS_VERSION_1;	//	Version 1.0

	//	Device Password ID
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_DEVICE_PWID );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
	wpsielen += 2;

	//	Value:

	if ( pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_PEER_DISPLAY_PIN )
	{
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_USER_SPEC );
	}
	else if ( pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_SELF_DISPLAY_PIN )
	{
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_REGISTRAR_SPEC );
	}
	else if ( pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_PBC )
	{
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_PBC );
	}

	wpsielen += 2;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen );


	//	P2P IE Section.

	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20110306
	//	According to the P2P Specification, the group negoitation request frame should contain 9 P2P attributes
	//	1. P2P Capability
	//	2. Group Owner Intent
	//	3. Configuration Timeout
	//	4. Listen Channel
	//	5. Extended Listen Timing
	//	6. Intended P2P Interface Address
	//	7. Channel List
	//	8. P2P Device Info
	//	9. Operating Channel


	//	P2P Capability
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CAPABILITY;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	//	Device Capability Bitmap, 1 byte
	p2pie[ p2pielen++ ] = DMP_P2P_DEVCAP_SUPPORT;

	//	Group Capability Bitmap, 1 byte
	if ( pwdinfo->persistent_supported )
	{
		p2pie[ p2pielen++ ] = P2P_GRPCAP_CROSS_CONN | P2P_GRPCAP_PERSISTENT_GROUP;
	}
	else
	{
		p2pie[ p2pielen++ ] = P2P_GRPCAP_CROSS_CONN;
	}


	//	Group Owner Intent
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_GO_INTENT;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0001 );
	p2pielen += 2;

	//	Value:
	//	Todo the tie breaker bit.
	p2pie[ p2pielen++ ] = ( ( pwdinfo->intent << 1 ) &  0xFE );

	//	Configuration Timeout
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CONF_TIMEOUT;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P GO
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P Client


	//	Listen Channel
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_LISTEN_CH;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'X';
	p2pie[ p2pielen++ ] = 'X';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Operating Class
	p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7
	
	//	Channel Number
	p2pie[ p2pielen++ ] = pwdinfo->listen_channel;	//	listening channel number
	

	//	Extended Listen Timing ATTR
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_EX_LISTEN_TIMING;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0004 );
	p2pielen += 2;

	//	Value:
	//	Availability Period
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF );
	p2pielen += 2;

	//	Availability Interval
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF );
	p2pielen += 2;


	//	Intended P2P Interface Address
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_INTENTED_IF_ADDR;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( ETH_ALEN );
	p2pielen += 2;

	//	Value:
	_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
	p2pielen += ETH_ALEN;


	//	Channel List
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CH_LIST;

	// Length:
	// Country String(3)
	// + ( Operating Class (1) + Number of Channels(1) ) * Operation Classes (?)
	// + number of channels in all classes
	len_channellist_attr = 3
	   + (1 + 1) * (u16)(pmlmeext->channel_list.reg_classes)
	   + get_reg_classes_full_count(pmlmeext->channel_list);

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter , _FW_LINKED)
		&& padapter->registrypriv.full_ch_in_p2p_handshake == 0)
	{
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 5 + 1 );
	}
	else
	{
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( len_channellist_attr );
	}
#else

	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( len_channellist_attr );

#endif
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'X';
	p2pie[ p2pielen++ ] = 'X';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Channel Entry List

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter , _FW_LINKED)
		&& padapter->registrypriv.full_ch_in_p2p_handshake == 0)
	{
		_adapter *pbuddy_adapter = padapter->pbuddy_adapter;	
		struct mlme_ext_priv	*pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;

		//	Operating Class
		if ( pbuddy_mlmeext->cur_channel > 14 )
		{
			if ( pbuddy_mlmeext->cur_channel >= 149 )
			{
				p2pie[ p2pielen++ ] = 0x7c;
			}
			else
			{
				p2pie[ p2pielen++ ] = 0x73;
			}
		}
		else
		{
			p2pie[ p2pielen++ ] = 0x51;
		}

		//	Number of Channels
		//	Just support 1 channel and this channel is AP's channel
		p2pie[ p2pielen++ ] = 1;

		//	Channel List
		p2pie[ p2pielen++ ] = pbuddy_mlmeext->cur_channel;
	}
	else
	{
		int i,j;
		for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
			//	Operating Class
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

			//	Number of Channels
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

			//	Channel List
			for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++) {
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
			}
		}
	}
#else // CONFIG_CONCURRENT_MODE
	{
		int i,j;
		for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
			//	Operating Class
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

			//	Number of Channels
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

			//	Channel List
			for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++) {
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
			}
		}
	}
#endif // CONFIG_CONCURRENT_MODE

	//	Device Info
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_DEVICE_INFO;

	//	Length:
	//	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes) 
	//	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes)
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 21 + pwdinfo->device_name_len );
	p2pielen += 2;

	//	Value:
	//	P2P Device Address
	_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
	p2pielen += ETH_ALEN;

	//	Config Method
	//	This field should be big endian. Noted by P2P specification.

	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->supported_wps_cm );

	p2pielen += 2;

	//	Primary Device Type
	//	Category ID
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_CID_MULIT_MEDIA );
	p2pielen += 2;

	//	OUI
	*(u32*) ( p2pie + p2pielen ) = cpu_to_be32( WPSOUI );
	p2pielen += 4;

	//	Sub Category ID
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_SCID_MEDIA_SERVER );
	p2pielen += 2;

	//	Number of Secondary Device Types
	p2pie[ p2pielen++ ] = 0x00;	//	No Secondary Device Type List

	//	Device Name
	//	Type:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
	p2pielen += 2;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->device_name_len );
	p2pielen += 2;

	//	Value:
	_rtw_memcpy( p2pie + p2pielen, pwdinfo->device_name , pwdinfo->device_name_len );
	p2pielen += pwdinfo->device_name_len;	
	

	//	Operating Channel
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_OPERATING_CH;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'X';
	p2pie[ p2pielen++ ] = 'X';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Operating Class
	if ( pwdinfo->operating_channel <= 14 )
	{
		//	Operating Class
		p2pie[ p2pielen++ ] = 0x51;
	}
	else if ( ( pwdinfo->operating_channel >= 36 ) && ( pwdinfo->operating_channel <= 48 ) )
	{
		//	Operating Class
		p2pie[ p2pielen++ ] = 0x73;
	}
	else
	{
		//	Operating Class
		p2pie[ p2pielen++ ] = 0x7c;
	}

	//	Channel Number
	p2pie[ p2pielen++ ] = pwdinfo->operating_channel;	//	operating channel number

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen );		

#ifdef CONFIG_WFD
	wfdielen = build_nego_req_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}


void issue_p2p_GO_response(_adapter *padapter, u8* raddr, u8* frame_body,uint len, u8 result)
{

	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_GO_NEGO_RESP;
	u8			wpsie[ 255 ] = { 0x00 }, p2pie[ 255 ] = { 0x00 };
	u8			p2pielen = 0, i;
	uint			wpsielen = 0;
	u16			wps_devicepassword_id = 0x0000;
	uint			wps_devicepassword_id_len = 0;
	u8			channel_cnt_24g = 0, channel_cnt_5gl = 0, channel_cnt_5gh;
	u16			len_channellist_attr = 0;
	
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo);

#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	DBG_871X( "[%s] In, result = %d\n", __FUNCTION__,  result );
	//update attribute
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
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));	
	pwdinfo->negotiation_dialog_token = frame_body[7];	//	The Dialog Token of provisioning discovery request frame.
	pframe = rtw_set_fixed_ie(pframe, 1, &(pwdinfo->negotiation_dialog_token), &(pattrib->pktlen));

	//	Commented by Albert 20110328
	//	Try to get the device password ID from the WPS IE of group negotiation request frame
	//	WiFi Direct test plan 5.1.15
	rtw_get_wps_ie( frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, wpsie, &wpsielen);
	rtw_get_wps_attr_content( wpsie, wpsielen, WPS_ATTR_DEVICE_PWID, (u8*) &wps_devicepassword_id, &wps_devicepassword_id_len);
	wps_devicepassword_id = be16_to_cpu( wps_devicepassword_id );

	_rtw_memset( wpsie, 0x00, 255 );
	wpsielen = 0;

	//	WPS Section
	wpsielen = 0;
	//	WPS OUI
	*(u32*) ( wpsie ) = cpu_to_be32( WPSOUI );
	wpsielen += 4;

	//	WPS version
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_VER1 );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
	wpsielen += 2;

	//	Value:
	wpsie[wpsielen++] = WPS_VERSION_1;	//	Version 1.0

	//	Device Password ID
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_DEVICE_PWID );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
	wpsielen += 2;

	//	Value:
	if ( wps_devicepassword_id == WPS_DPID_USER_SPEC )
	{
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_REGISTRAR_SPEC );
	}
	else if ( wps_devicepassword_id == WPS_DPID_REGISTRAR_SPEC )
	{
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_USER_SPEC );
	}
	else
	{
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_PBC );
	}
	wpsielen += 2;

	//	Commented by Kurt 20120113
	//	If some device wants to do p2p handshake without sending prov_disc_req
	//	We have to get peer_req_cm from here.
	if(_rtw_memcmp( pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "000", 3) )
	{
		if ( wps_devicepassword_id == WPS_DPID_USER_SPEC )
		{
			_rtw_memcpy( pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "dis", 3 );
		}
		else if ( wps_devicepassword_id == WPS_DPID_REGISTRAR_SPEC )
		{
			_rtw_memcpy( pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pad", 3 );	
		}
		else
		{
			_rtw_memcpy( pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pbc", 3 );	
		}
	}

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen );


	//	P2P IE Section.

	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20100908
	//	According to the P2P Specification, the group negoitation response frame should contain 9 P2P attributes
	//	1. Status
	//	2. P2P Capability
	//	3. Group Owner Intent
	//	4. Configuration Timeout
	//	5. Operating Channel
	//	6. Intended P2P Interface Address
	//	7. Channel List
	//	8. Device Info
	//	9. Group ID	( Only GO )


	//	ToDo:

	//	P2P Status
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_STATUS;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0001 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = result;
	
	//	P2P Capability
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CAPABILITY;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	//	Device Capability Bitmap, 1 byte

	if ( rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT) )
	{
		//	Commented by Albert 2011/03/08
		//	According to the P2P specification
		//	if the sending device will be client, the P2P Capability should be reserved of group negotation response frame
		p2pie[ p2pielen++ ] = 0;
	}
	else
	{
		//	Be group owner or meet the error case
		p2pie[ p2pielen++ ] = DMP_P2P_DEVCAP_SUPPORT;
	}
	
	//	Group Capability Bitmap, 1 byte
	if ( pwdinfo->persistent_supported )
	{
		p2pie[ p2pielen++ ] = P2P_GRPCAP_CROSS_CONN | P2P_GRPCAP_PERSISTENT_GROUP;
	}
	else
	{
		p2pie[ p2pielen++ ] = P2P_GRPCAP_CROSS_CONN;
	}

	//	Group Owner Intent
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_GO_INTENT;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0001 );
	p2pielen += 2;

	//	Value:
	if ( pwdinfo->peer_intent & 0x01 )
	{
		//	Peer's tie breaker bit is 1, our tie breaker bit should be 0
		p2pie[ p2pielen++ ] = ( pwdinfo->intent << 1 );
	}
	else
	{
		//	Peer's tie breaker bit is 0, our tie breaker bit should be 1
		p2pie[ p2pielen++ ] = ( ( pwdinfo->intent << 1 ) | BIT(0) );
	}


	//	Configuration Timeout
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CONF_TIMEOUT;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P GO
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P Client

	//	Operating Channel
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_OPERATING_CH;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'X';
	p2pie[ p2pielen++ ] = 'X';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Operating Class
	if ( pwdinfo->operating_channel <= 14 )
	{
		//	Operating Class
		p2pie[ p2pielen++ ] = 0x51;
	}
	else if ( ( pwdinfo->operating_channel >= 36 ) && ( pwdinfo->operating_channel <= 48 ) )
	{
		//	Operating Class
		p2pie[ p2pielen++ ] = 0x73;
	}
	else
	{
		//	Operating Class
		p2pie[ p2pielen++ ] = 0x7c;
	}
	
	//	Channel Number
	p2pie[ p2pielen++ ] = pwdinfo->operating_channel;	//	operating channel number

	//	Intended P2P Interface Address	
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_INTENTED_IF_ADDR;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( ETH_ALEN );
	p2pielen += 2;

	//	Value:
	_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
	p2pielen += ETH_ALEN;

	//	Channel List
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CH_LIST;

	// Country String(3)
	// + ( Operating Class (1) + Number of Channels(1) ) * Operation Classes (?)
	// + number of channels in all classes
	len_channellist_attr = 3
	   + (1 + 1) * (u16)pmlmeext->channel_list.reg_classes
	   + get_reg_classes_full_count(pmlmeext->channel_list);

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter , _FW_LINKED)
		&& padapter->registrypriv.full_ch_in_p2p_handshake == 0)
	{
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 5 + 1 );
	}
	else
	{
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( len_channellist_attr );
	}
#else

	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( len_channellist_attr );

 #endif
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'X';
	p2pie[ p2pielen++ ] = 'X';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Channel Entry List

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter , _FW_LINKED)
		&& padapter->registrypriv.full_ch_in_p2p_handshake == 0)
	{
		_adapter *pbuddy_adapter = padapter->pbuddy_adapter;	
		struct mlme_ext_priv	*pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;

		//	Operating Class
		if ( pbuddy_mlmeext->cur_channel > 14 )
		{
			if ( pbuddy_mlmeext->cur_channel >= 149 )
			{
				p2pie[ p2pielen++ ] = 0x7c;
			}
			else
			{
				p2pie[ p2pielen++ ] = 0x73;
			}
		}
		else
		{
			p2pie[ p2pielen++ ] = 0x51;
		}

		//	Number of Channels
		//	Just support 1 channel and this channel is AP's channel
		p2pie[ p2pielen++ ] = 1;

		//	Channel List
		p2pie[ p2pielen++ ] = pbuddy_mlmeext->cur_channel;
	}
	else
	{
		int i, j;
		for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
			//	Operating Class
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

			//	Number of Channels
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

			//	Channel List
			for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++) {
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
			}
		}
	}
#else // CONFIG_CONCURRENT_MODE
	{
		int i, j;
		for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
			//	Operating Class
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

			//	Number of Channels
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

			//	Channel List
			for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++) {
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
			}
		}
	}
#endif // CONFIG_CONCURRENT_MODE

	
	//	Device Info
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_DEVICE_INFO;

	//	Length:
	//	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes) 
	//	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes)
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 21 + pwdinfo->device_name_len );
	p2pielen += 2;

	//	Value:
	//	P2P Device Address
	_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
	p2pielen += ETH_ALEN;

	//	Config Method
	//	This field should be big endian. Noted by P2P specification.

	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->supported_wps_cm );

	p2pielen += 2;

	//	Primary Device Type
	//	Category ID
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_CID_MULIT_MEDIA );
	p2pielen += 2;

	//	OUI
	*(u32*) ( p2pie + p2pielen ) = cpu_to_be32( WPSOUI );
	p2pielen += 4;

	//	Sub Category ID
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_SCID_MEDIA_SERVER );
	p2pielen += 2;

	//	Number of Secondary Device Types
	p2pie[ p2pielen++ ] = 0x00;	//	No Secondary Device Type List

	//	Device Name
	//	Type:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
	p2pielen += 2;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->device_name_len );
	p2pielen += 2;

	//	Value:
	_rtw_memcpy( p2pie + p2pielen, pwdinfo->device_name , pwdinfo->device_name_len );
	p2pielen += pwdinfo->device_name_len;	
	
	if ( rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO) )
	{
		//	Group ID Attribute
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_GROUP_ID;

		//	Length:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( ETH_ALEN + pwdinfo->nego_ssidlen );
		p2pielen += 2;

		//	Value:
		//	p2P Device Address
		_rtw_memcpy( p2pie + p2pielen , pwdinfo->device_addr, ETH_ALEN );
		p2pielen += ETH_ALEN;

		//	SSID
		_rtw_memcpy( p2pie + p2pielen, pwdinfo->nego_ssid, pwdinfo->nego_ssidlen );
		p2pielen += pwdinfo->nego_ssidlen;
		
	}
	
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen );	
	
#ifdef CONFIG_WFD
	wfdielen = build_nego_resp_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}

void issue_p2p_GO_confirm(_adapter *padapter, u8* raddr, u8 result)
{

	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_GO_NEGO_CONF;
	u8			wpsie[ 255 ] = { 0x00 }, p2pie[ 255 ] = { 0x00 };
	u8			wpsielen = 0, p2pielen = 0;
	
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo);
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	DBG_871X( "[%s] In\n", __FUNCTION__ );
	//update attribute
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
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));	
	pframe = rtw_set_fixed_ie(pframe, 1, &(pwdinfo->negotiation_dialog_token), &(pattrib->pktlen));

	

	//	P2P IE Section.

	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20110306
	//	According to the P2P Specification, the group negoitation request frame should contain 5 P2P attributes
	//	1. Status
	//	2. P2P Capability
	//	3. Operating Channel
	//	4. Channel List
	//	5. Group ID	( if this WiFi is GO )

	//	P2P Status
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_STATUS;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0001 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = result;

	//	P2P Capability
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CAPABILITY;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	//	Device Capability Bitmap, 1 byte
	p2pie[ p2pielen++ ] = DMP_P2P_DEVCAP_SUPPORT;
	
	//	Group Capability Bitmap, 1 byte
	if ( pwdinfo->persistent_supported )
	{
		p2pie[ p2pielen++ ] = P2P_GRPCAP_CROSS_CONN | P2P_GRPCAP_PERSISTENT_GROUP;
	}
	else
	{
		p2pie[ p2pielen++ ] = P2P_GRPCAP_CROSS_CONN;
	}


	//	Operating Channel
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_OPERATING_CH;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'X';
	p2pie[ p2pielen++ ] = 'X';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;


	if ( rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT) )
	{
		if ( pwdinfo->peer_operating_ch <= 14 )
		{
			//	Operating Class
			p2pie[ p2pielen++ ] = 0x51;
		}
		else if ( ( pwdinfo->peer_operating_ch >= 36 ) && ( pwdinfo->peer_operating_ch <= 48 ) )
		{
			//	Operating Class
			p2pie[ p2pielen++ ] = 0x73;
		}
		else
		{
			//	Operating Class
			p2pie[ p2pielen++ ] = 0x7c;
		}
		
		p2pie[ p2pielen++ ] = pwdinfo->peer_operating_ch;
	}
	else
	{
		if ( pwdinfo->operating_channel <= 14 )
		{
			//	Operating Class
			p2pie[ p2pielen++ ] = 0x51;
		}
		else if ( ( pwdinfo->operating_channel >= 36 ) && ( pwdinfo->operating_channel <= 48 ) )
		{
			//	Operating Class
			p2pie[ p2pielen++ ] = 0x73;
		}
		else
		{
			//	Operating Class
			p2pie[ p2pielen++ ] = 0x7c;
		}
		
		//	Channel Number
		p2pie[ p2pielen++ ] = pwdinfo->operating_channel;		//	Use the listen channel as the operating channel
	}


	//	Channel List
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CH_LIST;

	*(u16*) ( p2pie + p2pielen ) = 6;
	p2pielen += 2;

	//	Country String
	p2pie[ p2pielen++ ] = 'X';
	p2pie[ p2pielen++ ] = 'X';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Value:
	if ( rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT) )
	{
		if ( pwdinfo->peer_operating_ch <= 14 )
		{
			//	Operating Class
			p2pie[ p2pielen++ ] = 0x51;
		}
		else if ( ( pwdinfo->peer_operating_ch >= 36 ) && ( pwdinfo->peer_operating_ch <= 48 ) )
		{
			//	Operating Class
			p2pie[ p2pielen++ ] = 0x73;
		}
		else
		{
			//	Operating Class
			p2pie[ p2pielen++ ] = 0x7c;
		}
		p2pie[ p2pielen++ ] = 1;
		p2pie[ p2pielen++ ] = pwdinfo->peer_operating_ch;
	}
	else
	{
		if ( pwdinfo->operating_channel <= 14 )
		{
			//	Operating Class
			p2pie[ p2pielen++ ] = 0x51;
		}
		else if ( ( pwdinfo->operating_channel >= 36 ) && ( pwdinfo->operating_channel <= 48 ) )
		{
			//	Operating Class
			p2pie[ p2pielen++ ] = 0x73;
		}
		else
		{
			//	Operating Class
			p2pie[ p2pielen++ ] = 0x7c;
		}
		
		//	Channel Number
		p2pie[ p2pielen++ ] = 1;
		p2pie[ p2pielen++ ] = pwdinfo->operating_channel;		//	Use the listen channel as the operating channel
	}

	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO) )
	{
		//	Group ID Attribute
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_GROUP_ID;

		//	Length:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( ETH_ALEN + pwdinfo->nego_ssidlen );
		p2pielen += 2;

		//	Value:
		//	p2P Device Address
		_rtw_memcpy( p2pie + p2pielen , pwdinfo->device_addr, ETH_ALEN );
		p2pielen += ETH_ALEN;

		//	SSID
		_rtw_memcpy( p2pie + p2pielen, pwdinfo->nego_ssid, pwdinfo->nego_ssidlen );
		p2pielen += pwdinfo->nego_ssidlen;
	}
	
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen );
	
#ifdef CONFIG_WFD
	wfdielen = build_nego_confirm_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}

void issue_p2p_invitation_request(_adapter *padapter, u8* raddr )
{

	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_INVIT_REQ;
	u8			p2pie[ 255 ] = { 0x00 };
	u8			p2pielen = 0, i;
	u8			dialogToken = 3;
	u8			channel_cnt_24g = 0, channel_cnt_5gl = 0, channel_cnt_5gh = 0;
	u16			len_channellist_attr = 0;	
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif
#ifdef CONFIG_CONCURRENT_MODE
	_adapter				*pbuddy_adapter = padapter->pbuddy_adapter;
	struct wifidirect_info	*pbuddy_wdinfo = &pbuddy_adapter->wdinfo;
	struct mlme_priv		*pbuddy_mlmepriv = &pbuddy_adapter->mlmepriv;
	struct mlme_ext_priv	*pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;
#endif

	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo);


	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
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
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));	
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));

	//	P2P IE Section.

	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20101011
	//	According to the P2P Specification, the P2P Invitation request frame should contain 7 P2P attributes
	//	1. Configuration Timeout
	//	2. Invitation Flags
	//	3. Operating Channel	( Only GO )
	//	4. P2P Group BSSID	( Should be included if I am the GO )
	//	5. Channel List
	//	6. P2P Group ID
	//	7. P2P Device Info

	//	Configuration Timeout
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CONF_TIMEOUT;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P GO
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P Client

	//	Invitation Flags
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_INVITATION_FLAGS;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0001 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = P2P_INVITATION_FLAGS_PERSISTENT;


	//	Operating Channel
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_OPERATING_CH;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'X';
	p2pie[ p2pielen++ ] = 'X';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Operating Class
	if ( pwdinfo->invitereq_info.operating_ch <= 14 )
		p2pie[ p2pielen++ ] = 0x51;
	else if ( ( pwdinfo->invitereq_info.operating_ch >= 36 ) && ( pwdinfo->invitereq_info.operating_ch <= 48 ) )
		p2pie[ p2pielen++ ] = 0x73;
	else
		p2pie[ p2pielen++ ] = 0x7c;
	
	//	Channel Number
	p2pie[ p2pielen++ ] = pwdinfo->invitereq_info.operating_ch;	//	operating channel number

	if (_rtw_memcmp(adapter_mac_addr(padapter), pwdinfo->invitereq_info.go_bssid, ETH_ALEN))
	{
		//	P2P Group BSSID
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_GROUP_BSSID;

		//	Length:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( ETH_ALEN );
		p2pielen += 2;

		//	Value:
		//	P2P Device Address for GO
		_rtw_memcpy( p2pie + p2pielen, pwdinfo->invitereq_info.go_bssid, ETH_ALEN );
		p2pielen += ETH_ALEN;
	}

	//	Channel List
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CH_LIST;

	
	//	Length:
	// Country String(3)
	// + ( Operating Class (1) + Number of Channels(1) ) * Operation Classes (?)
	// + number of channels in all classes
	len_channellist_attr = 3
	   + (1 + 1) * (u16)pmlmeext->channel_list.reg_classes
	   + get_reg_classes_full_count(pmlmeext->channel_list);

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter , _FW_LINKED)
		&& padapter->registrypriv.full_ch_in_p2p_handshake == 0)
	{
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 5 + 1 );
	}
	else
	{
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( len_channellist_attr );
	}
#else

	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( len_channellist_attr );

 #endif
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'X';
	p2pie[ p2pielen++ ] = 'X';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Channel Entry List
#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter, _FW_LINKED)
		&& padapter->registrypriv.full_ch_in_p2p_handshake == 0)
	{
		_adapter *pbuddy_adapter = padapter->pbuddy_adapter;	
		struct mlme_ext_priv	*pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;

		//	Operating Class
		if ( pbuddy_mlmeext->cur_channel > 14 )
		{
			if ( pbuddy_mlmeext->cur_channel >= 149 )
			{
				p2pie[ p2pielen++ ] = 0x7c;
			}
			else
			{
				p2pie[ p2pielen++ ] = 0x73;
			}
		}
		else
		{
			p2pie[ p2pielen++ ] = 0x51;
		}

		//	Number of Channels
		//	Just support 1 channel and this channel is AP's channel
		p2pie[ p2pielen++ ] = 1;

		//	Channel List
		p2pie[ p2pielen++ ] = pbuddy_mlmeext->cur_channel;
	}
	else
	{
		int i, j;
		for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
			//	Operating Class
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

			//	Number of Channels
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

			//	Channel List
			for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++) {
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
			}
		}
	}
#else // CONFIG_CONCURRENT_MODE
	{
		int i, j;
		for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
			//	Operating Class
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

			//	Number of Channels
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

			//	Channel List
			for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++) {
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
			}
		}
	}
#endif // CONFIG_CONCURRENT_MODE


	//	P2P Group ID
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_GROUP_ID;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 6 + pwdinfo->invitereq_info.ssidlen );
	p2pielen += 2;

	//	Value:
	//	P2P Device Address for GO
	_rtw_memcpy( p2pie + p2pielen, pwdinfo->invitereq_info.go_bssid, ETH_ALEN );
	p2pielen += ETH_ALEN;

	//	SSID
	_rtw_memcpy( p2pie + p2pielen, pwdinfo->invitereq_info.go_ssid, pwdinfo->invitereq_info.ssidlen );
	p2pielen += pwdinfo->invitereq_info.ssidlen;
	

	//	Device Info
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_DEVICE_INFO;

	//	Length:
	//	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes) 
	//	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes)
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 21 + pwdinfo->device_name_len );
	p2pielen += 2;
	
	//	Value:
	//	P2P Device Address
	_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
	p2pielen += ETH_ALEN;

	//	Config Method
	//	This field should be big endian. Noted by P2P specification.
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_CONFIG_METHOD_DISPLAY );
	p2pielen += 2;

	//	Primary Device Type
	//	Category ID
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_CID_MULIT_MEDIA );
	p2pielen += 2;

	//	OUI
	*(u32*) ( p2pie + p2pielen ) = cpu_to_be32( WPSOUI );
	p2pielen += 4;

	//	Sub Category ID
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_SCID_MEDIA_SERVER );
	p2pielen += 2;

	//	Number of Secondary Device Types
	p2pie[ p2pielen++ ] = 0x00;	//	No Secondary Device Type List

	//	Device Name
	//	Type:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
	p2pielen += 2;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->device_name_len );
	p2pielen += 2;

	//	Value:
	_rtw_memcpy( p2pie + p2pielen, pwdinfo->device_name, pwdinfo->device_name_len );
	p2pielen += pwdinfo->device_name_len;
		
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen );	

#ifdef CONFIG_WFD
	wfdielen = build_invitation_req_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}

void issue_p2p_invitation_response(_adapter *padapter, u8* raddr, u8 dialogToken, u8 status_code)
{

	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_INVIT_RESP;
	u8			p2pie[ 255 ] = { 0x00 };
	u8			p2pielen = 0, i;
	u8			channel_cnt_24g = 0, channel_cnt_5gl = 0, channel_cnt_5gh = 0;
	u16			len_channellist_attr = 0;
#ifdef CONFIG_CONCURRENT_MODE
	_adapter				*pbuddy_adapter = padapter->pbuddy_adapter;
	struct wifidirect_info	*pbuddy_wdinfo = &pbuddy_adapter->wdinfo;
	struct mlme_priv		*pbuddy_mlmepriv = &pbuddy_adapter->mlmepriv;
	struct mlme_ext_priv	*pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;
#endif	
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
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo);


	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
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
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));	
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));

	//	P2P IE Section.

	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20101005
	//	According to the P2P Specification, the P2P Invitation response frame should contain 5 P2P attributes
	//	1. Status
	//	2. Configuration Timeout
	//	3. Operating Channel	( Only GO )
	//	4. P2P Group BSSID	( Only GO )
	//	5. Channel List

	//	P2P Status
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_STATUS;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0001 );
	p2pielen += 2;

	//	Value:
	//	When status code is P2P_STATUS_FAIL_INFO_UNAVAILABLE.
	//	Sent the event receiving the P2P Invitation Req frame to DMP UI.
	//	DMP had to compare the MAC address to find out the profile.
	//	So, the WiFi driver will send the P2P_STATUS_FAIL_INFO_UNAVAILABLE to NB.
	//	If the UI found the corresponding profile, the WiFi driver sends the P2P Invitation Req
	//	to NB to rebuild the persistent group.
	p2pie[ p2pielen++ ] = status_code;
	
	//	Configuration Timeout
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CONF_TIMEOUT;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P GO
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P Client

	if( status_code == P2P_STATUS_SUCCESS )
	{
		if( rtw_p2p_chk_role( pwdinfo, P2P_ROLE_GO ) )
		{
			//	The P2P Invitation request frame asks this Wi-Fi device to be the P2P GO
			//	In this case, the P2P Invitation response frame should carry the two more P2P attributes.
			//	First one is operating channel attribute.
			//	Second one is P2P Group BSSID attribute.

			//	Operating Channel
			//	Type:
			p2pie[ p2pielen++ ] = P2P_ATTR_OPERATING_CH;

			//	Length:
			*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
			p2pielen += 2;

			//	Value:
			//	Country String
			p2pie[ p2pielen++ ] = 'X';
			p2pie[ p2pielen++ ] = 'X';
		
			//	The third byte should be set to 0x04.
			//	Described in the "Operating Channel Attribute" section.
			p2pie[ p2pielen++ ] = 0x04;

			//	Operating Class
			p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7
		
			//	Channel Number
			p2pie[ p2pielen++ ] = pwdinfo->operating_channel;	//	operating channel number
			

			//	P2P Group BSSID
			//	Type:
			p2pie[ p2pielen++ ] = P2P_ATTR_GROUP_BSSID;

			//	Length:
			*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( ETH_ALEN );
			p2pielen += 2;

			//	Value:
			//	P2P Device Address for GO
			_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
			p2pielen += ETH_ALEN;

		}

		//	Channel List
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_CH_LIST;

		//	Length:
		// Country String(3)
		// + ( Operating Class (1) + Number of Channels(1) ) * Operation Classes (?)
		// + number of channels in all classes
		len_channellist_attr = 3
			+ (1 + 1) * (u16)pmlmeext->channel_list.reg_classes
			+ get_reg_classes_full_count(pmlmeext->channel_list);

#ifdef CONFIG_CONCURRENT_MODE
		if (check_buddy_fwstate(padapter, _FW_LINKED)
			&& padapter->registrypriv.full_ch_in_p2p_handshake == 0)
		{
			*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 5 + 1 );
		}
		else
		{
			*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( len_channellist_attr );
		}
#else

		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( len_channellist_attr );

#endif
		p2pielen += 2;

		//	Value:
		//	Country String
		p2pie[ p2pielen++ ] = 'X';
		p2pie[ p2pielen++ ] = 'X';

		//	The third byte should be set to 0x04.
		//	Described in the "Operating Channel Attribute" section.
		p2pie[ p2pielen++ ] = 0x04;

		//	Channel Entry List
#ifdef CONFIG_CONCURRENT_MODE
		if (check_buddy_fwstate(padapter , _FW_LINKED)
			&& padapter->registrypriv.full_ch_in_p2p_handshake == 0)
		{
			_adapter *pbuddy_adapter = padapter->pbuddy_adapter;	
			struct mlme_ext_priv	*pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;

			//	Operating Class
			if ( pbuddy_mlmeext->cur_channel > 14 )
			{
				if ( pbuddy_mlmeext->cur_channel >= 149 )
				{
					p2pie[ p2pielen++ ] = 0x7c;
				}
				else
				{
					p2pie[ p2pielen++ ] = 0x73;
				}
			}
			else
			{
				p2pie[ p2pielen++ ] = 0x51;
			}

			//	Number of Channels
			//	Just support 1 channel and this channel is AP's channel
			p2pie[ p2pielen++ ] = 1;

			//	Channel List
			p2pie[ p2pielen++ ] = pbuddy_mlmeext->cur_channel;
		}
		else
		{
			int i, j;
			for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
				//	Operating Class
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

				//	Number of Channels
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

				//	Channel List
				for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++) {
					p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
				}
			}
		}
#else // CONFIG_CONCURRENT_MODE
		{
			int i, j;
			for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
				//	Operating Class
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

				//	Number of Channels
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

				//	Channel List
				for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++) {
					p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
				}
			}
		}
#endif // CONFIG_CONCURRENT_MODE
	}
		
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen );	
	
#ifdef CONFIG_WFD
	wfdielen = build_invitation_resp_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}

void issue_p2p_provision_request(_adapter *padapter, u8* pssid, u8 ussidlen, u8* pdev_raddr )
{
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u8			dialogToken = 1;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_PROVISION_DISC_REQ;
	u8			wpsie[ 100 ] = { 0x00 };
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
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);


	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	DBG_871X( "[%s] In\n", __FUNCTION__ );
	//update attribute
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
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));	
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));		

	p2pielen = build_prov_disc_request_p2p_ie( pwdinfo, pframe, pssid, ussidlen, pdev_raddr );

	pframe += p2pielen;
	pattrib->pktlen += p2pielen;

	wpsielen = 0;
	//	WPS OUI
	*(u32*) ( wpsie ) = cpu_to_be32( WPSOUI );
	wpsielen += 4;

	//	WPS version
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_VER1 );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
	wpsielen += 2;

	//	Value:
	wpsie[wpsielen++] = WPS_VERSION_1;	//	Version 1.0

	//	Config Method
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_CONF_METHOD );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
	wpsielen += 2;

	//	Value:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( pwdinfo->tx_prov_disc_info.wps_config_method_request );
	wpsielen += 2;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen );


#ifdef CONFIG_WFD
	wfdielen = build_provdisc_req_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}


u8 is_matched_in_profilelist( u8* peermacaddr, struct profile_info* profileinfo )
{
	u8 i, match_result = 0;

	DBG_871X( "[%s] peermac = %.2X %.2X %.2X %.2X %.2X %.2X\n", __FUNCTION__,
	  	    peermacaddr[0], peermacaddr[1],peermacaddr[2],peermacaddr[3],peermacaddr[4],peermacaddr[5]);
	
	for( i = 0; i < P2P_MAX_PERSISTENT_GROUP_NUM; i++, profileinfo++ )
	{
	       DBG_871X( "[%s] profileinfo_mac = %.2X %.2X %.2X %.2X %.2X %.2X\n", __FUNCTION__,
		   	    profileinfo->peermac[0], profileinfo->peermac[1],profileinfo->peermac[2],profileinfo->peermac[3],profileinfo->peermac[4],profileinfo->peermac[5]);		   
		if ( _rtw_memcmp( peermacaddr, profileinfo->peermac, ETH_ALEN ) )
		{
			match_result = 1;
			DBG_871X( "[%s] Match!\n", __FUNCTION__ );
			break;
		}
	}
	
	return (match_result );
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
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	//WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	u16					beacon_interval = 100;
	u16					capInfo = 0;
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	u8					wpsie[255] = { 0x00 };
	u32					wpsielen = 0, p2pielen = 0;
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif
#ifdef CONFIG_INTEL_WIDI
	u8 zero_array_check[L2SDTA_SERVICE_VE_LEN] = { 0x00 };
#endif //CONFIG_INTEL_WIDI

	//DBG_871X("%s\n", __FUNCTION__);
	
	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);	
	
	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);
		
	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;	
	
	mac = adapter_mac_addr(padapter);
	
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);
	
	//	Use the device address for BSSID field.	
	_rtw_memcpy(pwlanhdr->addr3, mac, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(fctrl, WIFI_PROBERSP);
	
	pattrib->hdrlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = pattrib->hdrlen;
	pframe += pattrib->hdrlen;

	//timestamp will be inserted by hardware
	pframe += 8;
	pattrib->pktlen += 8;

	// beacon interval: 2 bytes
	_rtw_memcpy(pframe, (unsigned char *) &beacon_interval, 2); 
	pframe += 2;
	pattrib->pktlen += 2;

	//	capability info: 2 bytes
	//	ESS and IBSS bits must be 0 (defined in the 3.1.2.1.1 of WiFi Direct Spec)
	capInfo |= cap_ShortPremble;
	capInfo |= cap_ShortSlot;
	
	_rtw_memcpy(pframe, (unsigned char *) &capInfo, 2);
	pframe += 2;
	pattrib->pktlen += 2;


	// SSID
	pframe = rtw_set_ie(pframe, _SSID_IE_, 7, pwdinfo->p2p_wildcard_ssid, &pattrib->pktlen);

	// supported rates...
	//	Use the OFDM rate in the P2P probe response frame. ( 6(B), 9(B), 12, 18, 24, 36, 48, 54 )
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, pwdinfo->support_rate, &pattrib->pktlen);

	// DS parameter set
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&pwdinfo->listen_channel, &pattrib->pktlen);

#ifdef CONFIG_IOCTL_CFG80211
	if(adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->driver_interface == DRIVER_CFG80211 )
	{
		if( pmlmepriv->wps_probe_resp_ie != NULL && pmlmepriv->p2p_probe_resp_ie != NULL )
		{
			//WPS IE
			_rtw_memcpy(pframe, pmlmepriv->wps_probe_resp_ie, pmlmepriv->wps_probe_resp_ie_len);
			pattrib->pktlen += pmlmepriv->wps_probe_resp_ie_len;
			pframe += pmlmepriv->wps_probe_resp_ie_len;

			//P2P IE
			_rtw_memcpy(pframe, pmlmepriv->p2p_probe_resp_ie, pmlmepriv->p2p_probe_resp_ie_len);
			pattrib->pktlen += pmlmepriv->p2p_probe_resp_ie_len;
			pframe += pmlmepriv->p2p_probe_resp_ie_len;
		}
	}
	else
#endif //CONFIG_IOCTL_CFG80211		
	{

		//	Todo: WPS IE
		//	Noted by Albert 20100907
		//	According to the WPS specification, all the WPS attribute is presented by Big Endian.

		wpsielen = 0;
		//	WPS OUI
		*(u32*) ( wpsie ) = cpu_to_be32( WPSOUI );
		wpsielen += 4;

		//	WPS version
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_VER1 );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
		wpsielen += 2;

		//	Value:
		wpsie[wpsielen++] = WPS_VERSION_1;	//	Version 1.0

#ifdef CONFIG_INTEL_WIDI
		//	Commented by Kurt
		//	Appended WiDi info. only if we did issued_probereq_widi(), and then we saved ven. ext. in pmlmepriv->sa_ext.
		if(  _rtw_memcmp(pmlmepriv->sa_ext, zero_array_check, L2SDTA_SERVICE_VE_LEN) == _FALSE 
			|| pmlmepriv->num_p2p_sdt != 0 )
		{
			//Sec dev type
			*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_SEC_DEV_TYPE_LIST );
			wpsielen += 2;

			//	Length:
			*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0008 );
			wpsielen += 2;

			//	Value:
			//	Category ID
			*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_PDT_CID_DISPLAYS );
			wpsielen += 2;

			//	OUI
			*(u32*) ( wpsie + wpsielen ) = cpu_to_be32( INTEL_DEV_TYPE_OUI );
			wpsielen += 4;

			*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_PDT_SCID_WIDI_CONSUMER_SINK );
			wpsielen += 2;

			if(  _rtw_memcmp(pmlmepriv->sa_ext, zero_array_check, L2SDTA_SERVICE_VE_LEN) == _FALSE )
			{
				//	Vendor Extension
				_rtw_memcpy( wpsie + wpsielen, pmlmepriv->sa_ext, L2SDTA_SERVICE_VE_LEN );
				wpsielen += L2SDTA_SERVICE_VE_LEN;
			}
		}
#endif //CONFIG_INTEL_WIDI

		//	WiFi Simple Config State
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_SIMPLE_CONF_STATE );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
		wpsielen += 2;

		//	Value:
		wpsie[wpsielen++] = WPS_WSC_STATE_NOT_CONFIG;	//	Not Configured.

		//	Response Type
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_RESP_TYPE );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
		wpsielen += 2;

		//	Value:
		wpsie[wpsielen++] = WPS_RESPONSE_TYPE_8021X;

		//	UUID-E
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_UUID_E );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0010 );
		wpsielen += 2;

		//	Value:
		if (pwdinfo->external_uuid == 0) {
			_rtw_memset( wpsie + wpsielen, 0x0, 16 );
			_rtw_memcpy(wpsie + wpsielen, mac, ETH_ALEN);
		} else {
			_rtw_memcpy( wpsie + wpsielen, pwdinfo->uuid, 0x10 );
		}
		wpsielen += 0x10;

		//	Manufacturer
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_MANUFACTURER );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0007 );
		wpsielen += 2;

		//	Value:
		_rtw_memcpy( wpsie + wpsielen, "Realtek", 7 );
		wpsielen += 7;

		//	Model Name
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_MODEL_NAME );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0006 );
		wpsielen += 2;	

		//	Value:
		_rtw_memcpy( wpsie + wpsielen, "8192CU", 6 );
		wpsielen += 6;

		//	Model Number
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_MODEL_NUMBER );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
		wpsielen += 2;

		//	Value:
		wpsie[ wpsielen++ ] = 0x31;		//	character 1

		//	Serial Number
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_SERIAL_NUMBER );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( ETH_ALEN );
		wpsielen += 2;

		//	Value:
		_rtw_memcpy( wpsie + wpsielen, "123456" , ETH_ALEN );
		wpsielen += ETH_ALEN;

		//	Primary Device Type
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_PRIMARY_DEV_TYPE );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0008 );
		wpsielen += 2;

		//	Value:
		//	Category ID
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_PDT_CID_MULIT_MEDIA );
		wpsielen += 2;

		//	OUI
		*(u32*) ( wpsie + wpsielen ) = cpu_to_be32( WPSOUI );
		wpsielen += 4;

		//	Sub Category ID
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_PDT_SCID_MEDIA_SERVER );
		wpsielen += 2;

		//	Device Name
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( pwdinfo->device_name_len );
		wpsielen += 2;

		//	Value:
		_rtw_memcpy( wpsie + wpsielen, pwdinfo->device_name, pwdinfo->device_name_len );
		wpsielen += pwdinfo->device_name_len;

		//	Config Method
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_CONF_METHOD );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
		wpsielen += 2;

		//	Value:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( pwdinfo->supported_wps_cm );
		wpsielen += 2;
		

		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen );
		

		p2pielen = build_probe_resp_p2p_ie(pwdinfo, pframe);
		pframe += p2pielen;
		pattrib->pktlen += p2pielen;
	}

#ifdef CONFIG_WFD
	wfdielen = rtw_append_probe_resp_wfd_ie(padapter, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
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
	unsigned char			bssrate[NumRates];
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	int	bssrate_len = 0;
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);	
	u8					wpsie[255] = { 0x00 }, p2pie[ 255 ] = { 0x00 };
	u16					wpsielen = 0, p2pielen = 0;
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif

	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);


	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);


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
		if ( ( pwdinfo->p2p_info.scan_op_ch_only ) || ( pwdinfo->rx_invitereq_info.scan_op_ch_only ) )
		{
			//	This two flags will be set when this is only the P2P client mode.
			_rtw_memcpy(pwlanhdr->addr1, pwdinfo->p2p_peer_interface_addr, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, pwdinfo->p2p_peer_interface_addr, ETH_ALEN);
		}
		else
		{
			//	broadcast probe request frame
			_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, bc_addr, ETH_ALEN);
		}
	}
	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_PROBEREQ);

	pframe += sizeof (struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof (struct rtw_ieee80211_hdr_3addr);

	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_PROVISION_DIS_REQ))
	{
		pframe = rtw_set_ie(pframe, _SSID_IE_, pwdinfo->tx_prov_disc_info.ssid.SsidLength, pwdinfo->tx_prov_disc_info.ssid.Ssid, &(pattrib->pktlen));
	}
	else
	{
		pframe = rtw_set_ie(pframe, _SSID_IE_, P2P_WILDCARD_SSID_LEN, pwdinfo->p2p_wildcard_ssid, &(pattrib->pktlen));
	}
	//	Use the OFDM rate in the P2P probe request frame. ( 6(B), 9(B), 12(B), 24(B), 36, 48, 54 )
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, pwdinfo->support_rate, &pattrib->pktlen);

#ifdef CONFIG_IOCTL_CFG80211
	if(adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->driver_interface == DRIVER_CFG80211 )
	{
		if( pmlmepriv->wps_probe_req_ie != NULL && pmlmepriv->p2p_probe_req_ie != NULL )
		{
			//WPS IE
			_rtw_memcpy(pframe, pmlmepriv->wps_probe_req_ie, pmlmepriv->wps_probe_req_ie_len);
			pattrib->pktlen += pmlmepriv->wps_probe_req_ie_len;
			pframe += pmlmepriv->wps_probe_req_ie_len;

			//P2P IE
			_rtw_memcpy(pframe, pmlmepriv->p2p_probe_req_ie, pmlmepriv->p2p_probe_req_ie_len);
			pattrib->pktlen += pmlmepriv->p2p_probe_req_ie_len;
			pframe += pmlmepriv->p2p_probe_req_ie_len;
		}
	}
	else
#endif //CONFIG_IOCTL_CFG80211
	{

		//	WPS IE
		//	Noted by Albert 20110221
		//	According to the WPS specification, all the WPS attribute is presented by Big Endian.

		wpsielen = 0;
		//	WPS OUI
		*(u32*) ( wpsie ) = cpu_to_be32( WPSOUI );
		wpsielen += 4;

		//	WPS version
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_VER1 );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
		wpsielen += 2;

		//	Value:
		wpsie[wpsielen++] = WPS_VERSION_1;	//	Version 1.0

		if( pmlmepriv->wps_probe_req_ie == NULL )
		{
			//	UUID-E
			//	Type:
			*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_UUID_E );
			wpsielen += 2;

			//	Length:
			*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0010 );
			wpsielen += 2;

			//	Value:
			if (pwdinfo->external_uuid == 0) {
				_rtw_memset( wpsie + wpsielen, 0x0, 16 );
				_rtw_memcpy(wpsie + wpsielen, mac, ETH_ALEN);
			} else {
				_rtw_memcpy( wpsie + wpsielen, pwdinfo->uuid, 0x10 );
			}
			wpsielen += 0x10;

			//	Config Method
			//	Type:
			*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_CONF_METHOD );
			wpsielen += 2;

			//	Length:
			*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
			wpsielen += 2;

			//	Value:
			*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( pwdinfo->supported_wps_cm );
			wpsielen += 2;
		}

		//	Device Name
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( pwdinfo->device_name_len );
		wpsielen += 2;

		//	Value:
		_rtw_memcpy( wpsie + wpsielen, pwdinfo->device_name, pwdinfo->device_name_len );
		wpsielen += pwdinfo->device_name_len;

		//	Primary Device Type
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_PRIMARY_DEV_TYPE );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0008 );
		wpsielen += 2;

		//	Value:
		//	Category ID
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_PDT_CID_RTK_WIDI );
		wpsielen += 2;

		//	OUI
		*(u32*) ( wpsie + wpsielen ) = cpu_to_be32( WPSOUI );
		wpsielen += 4;

		//	Sub Category ID
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_PDT_SCID_RTK_DMP );
		wpsielen += 2;

		//	Device Password ID
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_DEVICE_PWID );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
		wpsielen += 2;

		//	Value:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_REGISTRAR_SPEC );	//	Registrar-specified
		wpsielen += 2;	

		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen );
		
		//	P2P OUI
		p2pielen = 0;
		p2pie[ p2pielen++ ] = 0x50;
		p2pie[ p2pielen++ ] = 0x6F;
		p2pie[ p2pielen++ ] = 0x9A;
		p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

		//	Commented by Albert 20110221
		//	According to the P2P Specification, the probe request frame should contain 5 P2P attributes
		//	1. P2P Capability
		//	2. P2P Device ID if this probe request wants to find the specific P2P device
		//	3. Listen Channel
		//	4. Extended Listen Timing
		//	5. Operating Channel if this WiFi is working as the group owner now

		//	P2P Capability
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_CAPABILITY;

		//	Length:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
		p2pielen += 2;

		//	Value:
		//	Device Capability Bitmap, 1 byte
		p2pie[ p2pielen++ ] = DMP_P2P_DEVCAP_SUPPORT;
		
		//	Group Capability Bitmap, 1 byte
		if ( pwdinfo->persistent_supported )
			p2pie[ p2pielen++ ] = P2P_GRPCAP_PERSISTENT_GROUP | DMP_P2P_GRPCAP_SUPPORT;
		else
			p2pie[ p2pielen++ ] = DMP_P2P_GRPCAP_SUPPORT;

		//	Listen Channel
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_LISTEN_CH;

		//	Length:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
		p2pielen += 2;

		//	Value:
		//	Country String
		p2pie[ p2pielen++ ] = 'X';
		p2pie[ p2pielen++ ] = 'X';
		
		//	The third byte should be set to 0x04.
		//	Described in the "Operating Channel Attribute" section.
		p2pie[ p2pielen++ ] = 0x04;

		//	Operating Class
		p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7
		
		//	Channel Number
		p2pie[ p2pielen++ ] = pwdinfo->listen_channel;	//	listen channel
		

		//	Extended Listen Timing
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_EX_LISTEN_TIMING;

		//	Length:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0004 );
		p2pielen += 2;

		//	Value:
		//	Availability Period
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF );
		p2pielen += 2;

		//	Availability Interval
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF );
		p2pielen += 2;

		if ( rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO) )
		{
			//	Operating Channel (if this WiFi is working as the group owner now)
			//	Type:
			p2pie[ p2pielen++ ] = P2P_ATTR_OPERATING_CH;

			//	Length:
			*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
			p2pielen += 2;

			//	Value:
			//	Country String
			p2pie[ p2pielen++ ] = 'X';
			p2pie[ p2pielen++ ] = 'X';
		
			//	The third byte should be set to 0x04.
			//	Described in the "Operating Channel Attribute" section.
			p2pie[ p2pielen++ ] = 0x04;

			//	Operating Class
			p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7
		
			//	Channel Number
			p2pie[ p2pielen++ ] = pwdinfo->operating_channel;	//	operating channel number
			
		}
		
		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen );	

	}

#ifdef CONFIG_WFD
	wfdielen = rtw_append_probe_req_wfd_ie(padapter, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("issuing probe_req, tx_len=%d\n", pattrib->last_txcmdsz));

	if (wait_ack) {
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	} else {
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
	u32 start = rtw_get_current_time();

	do
	{
		ret = _issue_probereq_p2p(adapter, da, wait_ms>0?_TRUE:_FALSE);

		i++;

		if (RTW_CANNOT_RUN(adapter))
			break;

		if(i < try_cnt && wait_ms > 0 && ret==_FAIL)
			rtw_msleep_os(wait_ms);

	}while((i<try_cnt) && ((ret==_FAIL)||(wait_ms==0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		#ifndef DBG_XMIT_ACK
		goto exit;
		#endif
	}

	if (try_cnt && wait_ms) {
		if (da)
			DBG_871X(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(adapter), MAC_ARG(da), rtw_get_oper_ch(adapter),
				ret==_SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
		else
			DBG_871X(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(adapter), rtw_get_oper_ch(adapter),
				ret==_SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
	}
exit:
	return ret;
}

#endif //CONFIG_P2P

s32 rtw_action_public_decache(union recv_frame *rframe, u8 token_offset)
{
	_adapter *adapter = rframe->u.hdr.adapter;
	struct mlme_ext_priv *mlmeext = &(adapter->mlmeextpriv);
	u8 *frame = rframe->u.hdr.rx_data;
	u16 seq_ctrl = ((rframe->u.hdr.attrib.seq_num&0xffff) << 4) | (rframe->u.hdr.attrib.frag_num & 0xf);
	u8 token = *(rframe->u.hdr.rx_data + sizeof(struct rtw_ieee80211_hdr_3addr) + token_offset);

	if (GetRetry(frame)) {
		if ((seq_ctrl == mlmeext->action_public_rxseq)
			&& (token == mlmeext->action_public_dialog_token)
		) {
			DBG_871X(FUNC_ADPT_FMT" seq_ctrl=0x%x, rxseq=0x%x, token:%d\n",
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
	u32	p2p_ielen, wps_ielen;
	struct	wifidirect_info	*pwdinfo = &( padapter->wdinfo );
	u8	result = P2P_STATUS_SUCCESS;
	u8	empty_addr[ETH_ALEN] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	u8 *merged_p2pie = NULL;
	u32 merged_p2p_ielen= 0;
#endif //CONFIG_P2P

	frame_body = (unsigned char *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));

#ifdef CONFIG_P2P
	_cancel_timer_ex( &pwdinfo->reset_ch_sitesurvey );
#ifdef CONFIG_IOCTL_CFG80211
	if(adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->driver_interface == DRIVER_CFG80211)
	{
		rtw_cfg80211_rx_p2p_action_public(padapter, pframe, len);
	}
	else
#endif //CONFIG_IOCTL_CFG80211
	{
		//	Do nothing if the driver doesn't enable the P2P function.
		if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) || rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE))
			return _SUCCESS;

		len -= sizeof(struct rtw_ieee80211_hdr_3addr);

		switch( frame_body[ 6 ] )//OUI Subtype
		{
			case P2P_GO_NEGO_REQ:
			{
				DBG_871X( "[%s] Got GO Nego Req Frame\n", __FUNCTION__);
				_rtw_memset( &pwdinfo->groupid_info, 0x00, sizeof( struct group_id_info ) );

				if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_RX_PROVISION_DIS_REQ))
				{
					rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
				}

				if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_FAIL))
				{
					//	Commented by Albert 20110526
					//	In this case, this means the previous nego fail doesn't be reset yet.
					_cancel_timer_ex( &pwdinfo->restore_p2p_state_timer );
					//	Restore the previous p2p state
					rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
					DBG_871X( "[%s] Restore the previous p2p state to %d\n", __FUNCTION__, rtw_p2p_state(pwdinfo) );						
				}					
#ifdef CONFIG_CONCURRENT_MODE
				if ( check_buddy_fwstate(padapter, _FW_LINKED ) )
				{
					_cancel_timer_ex( &pwdinfo->ap_p2p_switch_timer );
				}
#endif // CONFIG_CONCURRENT_MODE

				//	Commented by Kurt 20110902
				//Add if statement to avoid receiving duplicate prov disc req. such that pre_p2p_state would be covered.
				if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING))
					rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));

				//	Commented by Kurt 20120113
				//	Get peer_dev_addr here if peer doesn't issue prov_disc frame.
				if( _rtw_memcmp(pwdinfo->rx_prov_disc_info.peerDevAddr, empty_addr, ETH_ALEN) )
					_rtw_memcpy(pwdinfo->rx_prov_disc_info.peerDevAddr, GetAddr2Ptr(pframe), ETH_ALEN);

				result = process_p2p_group_negotation_req( pwdinfo, frame_body, len );
				issue_p2p_GO_response( padapter, GetAddr2Ptr(pframe), frame_body, len, result );
#ifdef CONFIG_INTEL_WIDI
				if (padapter->mlmepriv.widi_state == INTEL_WIDI_STATE_LISTEN) {
					padapter->mlmepriv.widi_state = INTEL_WIDI_STATE_WFD_CONNECTION;
					_cancel_timer_ex(&(padapter->mlmepriv.listen_timer));
					intel_widi_wk_cmd(padapter, INTEL_WIDI_LISTEN_STOP_WK, NULL, 0);
				}
#endif //CONFIG_INTEL_WIDI

				//	Commented by Albert 20110718
				//	No matter negotiating or negotiation failure, the driver should set up the restore P2P state timer.
#ifdef CONFIG_CONCURRENT_MODE
				//	Commented by Albert 20120107
				_set_timer( &pwdinfo->restore_p2p_state_timer, 3000 );
#else // CONFIG_CONCURRENT_MODE
				_set_timer( &pwdinfo->restore_p2p_state_timer, 5000 );
#endif // CONFIG_CONCURRENT_MODE
				break;					
			}
			case P2P_GO_NEGO_RESP:
			{
				DBG_871X( "[%s] Got GO Nego Resp Frame\n", __FUNCTION__);

				if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING))
				{
					//	Commented by Albert 20110425
					//	The restore timer is enabled when issuing the nego request frame of rtw_p2p_connect function.
					_cancel_timer_ex( &pwdinfo->restore_p2p_state_timer );
					pwdinfo->nego_req_info.benable = _FALSE;
					result = process_p2p_group_negotation_resp( pwdinfo, frame_body, len);
					issue_p2p_GO_confirm( pwdinfo->padapter, GetAddr2Ptr(pframe), result);
					if ( P2P_STATUS_SUCCESS == result )
					{
						if ( rtw_p2p_role(pwdinfo) == P2P_ROLE_CLIENT )
						{
							pwdinfo->p2p_info.operation_ch[ 0 ] = pwdinfo->peer_operating_ch;
							#ifdef CONFIG_P2P_OP_CHK_SOCIAL_CH
							pwdinfo->p2p_info.operation_ch[ 1 ] = 1;	//Check whether GO is operating in channel 1;
							pwdinfo->p2p_info.operation_ch[ 2 ] = 6;	//Check whether GO is operating in channel 6;
							pwdinfo->p2p_info.operation_ch[ 3 ] = 11;	//Check whether GO is operating in channel 11;
							#endif //CONFIG_P2P_OP_CHK_SOCIAL_CH
							pwdinfo->p2p_info.scan_op_ch_only = 1;
							_set_timer( &pwdinfo->reset_ch_sitesurvey2, P2P_RESET_SCAN_CH );
						}
					}

					//	Reset the dialog token for group negotiation frames.
					pwdinfo->negotiation_dialog_token = 1;

					if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_FAIL))
					{
						_set_timer( &pwdinfo->restore_p2p_state_timer, 5000 );
					}
				}
				else
				{
					DBG_871X( "[%s] Skipped GO Nego Resp Frame (p2p_state != P2P_STATE_GONEGO_ING)\n", __FUNCTION__);
				}
				
				break;
			}
			case P2P_GO_NEGO_CONF:
			{
				DBG_871X( "[%s] Got GO Nego Confirm Frame\n", __FUNCTION__);
				result = process_p2p_group_negotation_confirm( pwdinfo, frame_body, len);
				if ( P2P_STATUS_SUCCESS == result )
				{
					if ( rtw_p2p_role(pwdinfo) == P2P_ROLE_CLIENT )
					{
						pwdinfo->p2p_info.operation_ch[ 0 ] = pwdinfo->peer_operating_ch;
						#ifdef CONFIG_P2P_OP_CHK_SOCIAL_CH
						pwdinfo->p2p_info.operation_ch[ 1 ] = 1;	//Check whether GO is operating in channel 1;
						pwdinfo->p2p_info.operation_ch[ 2 ] = 6;	//Check whether GO is operating in channel 6;
						pwdinfo->p2p_info.operation_ch[ 3 ] = 11;	//Check whether GO is operating in channel 11;
						#endif //CONFIG_P2P_OP_CHK_SOCIAL_CH
						pwdinfo->p2p_info.scan_op_ch_only = 1;
						_set_timer( &pwdinfo->reset_ch_sitesurvey2, P2P_RESET_SCAN_CH );
					}
				}
				break;
			}
			case P2P_INVIT_REQ:
			{
				//	Added by Albert 2010/10/05
				//	Received the P2P Invite Request frame.
				
				DBG_871X( "[%s] Got invite request frame!\n", __FUNCTION__ );
				if ( (p2p_ie=rtw_get_p2p_ie( frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &p2p_ielen)) )
				{
					//	Parse the necessary information from the P2P Invitation Request frame.
					//	For example: The MAC address of sending this P2P Invitation Request frame.
					u32	attr_contentlen = 0;
					u8	status_code = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
					struct group_id_info group_id;
					u8	invitation_flag = 0;
					int j=0;

					merged_p2p_ielen = rtw_get_p2p_merged_ies_len(frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_);

					merged_p2pie = rtw_zmalloc(merged_p2p_ielen + 2);	// 2 is for EID and Length
					if (merged_p2pie == NULL)
					{
						DBG_871X( "[%s] Malloc p2p ie fail\n", __FUNCTION__);
						goto exit;
					}
					_rtw_memset(merged_p2pie, 0x00, merged_p2p_ielen);					

					merged_p2p_ielen = rtw_p2p_merge_ies(frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, merged_p2pie);

					rtw_get_p2p_attr_content( merged_p2pie, merged_p2p_ielen, P2P_ATTR_INVITATION_FLAGS, &invitation_flag, &attr_contentlen);
					if ( attr_contentlen )
					{

						rtw_get_p2p_attr_content( merged_p2pie, merged_p2p_ielen, P2P_ATTR_GROUP_BSSID, pwdinfo->p2p_peer_interface_addr, &attr_contentlen);
						//	Commented by Albert 20120510
						//	Copy to the pwdinfo->p2p_peer_interface_addr.
						//	So that the WFD UI ( or Sigma ) can get the peer interface address by using the following command.
						//	#> iwpriv wlan0 p2p_get peer_ifa
						//	After having the peer interface address, the sigma can find the correct conf file for wpa_supplicant.

						if ( attr_contentlen )
						{
							DBG_871X( "[%s] GO's BSSID = %.2X %.2X %.2X %.2X %.2X %.2X\n", __FUNCTION__,
									pwdinfo->p2p_peer_interface_addr[0], pwdinfo->p2p_peer_interface_addr[1],
									pwdinfo->p2p_peer_interface_addr[2], pwdinfo->p2p_peer_interface_addr[3],
									pwdinfo->p2p_peer_interface_addr[4], pwdinfo->p2p_peer_interface_addr[5] );
						}								

						if ( invitation_flag & P2P_INVITATION_FLAGS_PERSISTENT )
						{
							//	Re-invoke the persistent group.
							
							_rtw_memset( &group_id, 0x00, sizeof( struct group_id_info ) );
							rtw_get_p2p_attr_content( merged_p2pie, merged_p2p_ielen, P2P_ATTR_GROUP_ID, ( u8* ) &group_id, &attr_contentlen);
							if ( attr_contentlen )
							{
								if (_rtw_memcmp(group_id.go_device_addr, adapter_mac_addr(padapter), ETH_ALEN))
								{
									//	The p2p device sending this p2p invitation request wants this Wi-Fi device to be the persistent GO.
									rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_GO );
									rtw_p2p_set_role( pwdinfo, P2P_ROLE_GO );
									status_code = P2P_STATUS_SUCCESS;
								}
								else
								{
									//	The p2p device sending this p2p invitation request wants to be the persistent GO.
									if ( is_matched_in_profilelist( pwdinfo->p2p_peer_interface_addr, &pwdinfo->profileinfo[ 0 ] ) )
									{
										u8 operatingch_info[5] = { 0x00 };
										if ( rtw_get_p2p_attr_content(merged_p2pie, merged_p2p_ielen, P2P_ATTR_OPERATING_CH, operatingch_info, &attr_contentlen) )
										{
											if( rtw_ch_set_search_ch(padapter->mlmeextpriv.channel_set, (u32)operatingch_info[4] ) >= 0 )
											{
												//	The operating channel is acceptable for this device.
												pwdinfo->rx_invitereq_info.operation_ch[0]= operatingch_info[4];
												#ifdef CONFIG_P2P_OP_CHK_SOCIAL_CH
												pwdinfo->rx_invitereq_info.operation_ch[1]= 1;		//Check whether GO is operating in channel 1;
												pwdinfo->rx_invitereq_info.operation_ch[2]= 6;		//Check whether GO is operating in channel 6;
												pwdinfo->rx_invitereq_info.operation_ch[3]= 11;		//Check whether GO is operating in channel 11;
												#endif //CONFIG_P2P_OP_CHK_SOCIAL_CH
												pwdinfo->rx_invitereq_info.scan_op_ch_only = 1;
												_set_timer( &pwdinfo->reset_ch_sitesurvey, P2P_RESET_SCAN_CH );
												rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_MATCH );
												rtw_p2p_set_role( pwdinfo, P2P_ROLE_CLIENT );
												status_code = P2P_STATUS_SUCCESS;
											}
											else
											{
												//	The operating channel isn't supported by this device.
												rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_DISMATCH );
												rtw_p2p_set_role( pwdinfo, P2P_ROLE_DEVICE );
												status_code = P2P_STATUS_FAIL_NO_COMMON_CH;
												_set_timer( &pwdinfo->restore_p2p_state_timer, 3000 );
											}
										}
										else
										{
											//	Commented by Albert 20121130
											//	Intel will use the different P2P IE to store the operating channel information
											//	Workaround for Intel WiDi 3.5
											rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_MATCH );
											rtw_p2p_set_role( pwdinfo, P2P_ROLE_CLIENT );
											status_code = P2P_STATUS_SUCCESS;
										}								
									}
									else
									{
										rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_DISMATCH );
										#ifdef CONFIG_INTEL_WIDI
										_rtw_memcpy( pwdinfo->p2p_peer_device_addr, group_id.go_device_addr , ETH_ALEN );
										rtw_p2p_set_role( pwdinfo, P2P_ROLE_CLIENT );
										#endif //CONFIG_INTEL_WIDI

										status_code = P2P_STATUS_FAIL_UNKNOWN_P2PGROUP;
									}
								}
							}
							else
							{
								DBG_871X( "[%s] P2P Group ID Attribute NOT FOUND!\n", __FUNCTION__ );
								status_code = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
							}									
						}
						else
						{
							//	Received the invitation to join a P2P group.

							_rtw_memset( &group_id, 0x00, sizeof( struct group_id_info ) );
							rtw_get_p2p_attr_content( merged_p2pie, merged_p2p_ielen, P2P_ATTR_GROUP_ID, ( u8* ) &group_id, &attr_contentlen);
							if ( attr_contentlen )
							{
								if (_rtw_memcmp(group_id.go_device_addr, adapter_mac_addr(padapter), ETH_ALEN))
								{
									//	In this case, the GO can't be myself.
									rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_DISMATCH );
									status_code = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
								}
								else
								{
									//	The p2p device sending this p2p invitation request wants to join an existing P2P group
									//	Commented by Albert 2012/06/28
									//	In this case, this Wi-Fi device should use the iwpriv command to get the peer device address.
									//	The peer device address should be the destination address for the provisioning discovery request.
									//	Then, this Wi-Fi device should use the iwpriv command to get the peer interface address.
									//	The peer interface address should be the address for WPS mac address
									_rtw_memcpy( pwdinfo->p2p_peer_device_addr, group_id.go_device_addr , ETH_ALEN );											
									rtw_p2p_set_role( pwdinfo, P2P_ROLE_CLIENT );
									rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_JOIN );
									status_code = P2P_STATUS_SUCCESS;
								}
							}
							else
							{
								DBG_871X( "[%s] P2P Group ID Attribute NOT FOUND!\n", __FUNCTION__ );
								status_code = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
							}
						}
					}
					else
					{
						DBG_871X( "[%s] P2P Invitation Flags Attribute NOT FOUND!\n", __FUNCTION__ );
						status_code = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
					}							

					DBG_871X( "[%s] status_code = %d\n", __FUNCTION__, status_code );

					pwdinfo->inviteresp_info.token = frame_body[ 7 ];
					issue_p2p_invitation_response( padapter, GetAddr2Ptr(pframe), pwdinfo->inviteresp_info.token, status_code );
					_set_timer( &pwdinfo->restore_p2p_state_timer, 3000 );
				}
#ifdef CONFIG_INTEL_WIDI
				if (padapter->mlmepriv.widi_state == INTEL_WIDI_STATE_LISTEN) {
					padapter->mlmepriv.widi_state = INTEL_WIDI_STATE_WFD_CONNECTION;
					_cancel_timer_ex(&(padapter->mlmepriv.listen_timer));
					intel_widi_wk_cmd(padapter, INTEL_WIDI_LISTEN_STOP_WK, NULL, 0);
				}
#endif //CONFIG_INTEL_WIDI
				break;
			}
			case P2P_INVIT_RESP:
			{
				u8	attr_content = 0x00;
				u32	attr_contentlen = 0;
				
				DBG_871X( "[%s] Got invite response frame!\n", __FUNCTION__ );
				_cancel_timer_ex( &pwdinfo->restore_p2p_state_timer );
				if ( (p2p_ie=rtw_get_p2p_ie( frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &p2p_ielen)) )
				{
					rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, &attr_content, &attr_contentlen);
									
					if ( attr_contentlen == 1 )
					{
						DBG_871X( "[%s] Status = %d\n", __FUNCTION__, attr_content );
						pwdinfo->invitereq_info.benable = _FALSE;

						if ( attr_content == P2P_STATUS_SUCCESS )
						{
							if (_rtw_memcmp(pwdinfo->invitereq_info.go_bssid, adapter_mac_addr(padapter), ETH_ALEN))
								rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO );
							else
								rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);

							rtw_p2p_set_state( pwdinfo, P2P_STATE_RX_INVITE_RESP_OK );
						}
						else
						{
							rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
							rtw_p2p_set_state( pwdinfo, P2P_STATE_RX_INVITE_RESP_FAIL );
						}
					}
					else
					{
						rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
						rtw_p2p_set_state( pwdinfo, P2P_STATE_RX_INVITE_RESP_FAIL );
					}
				}
				else
				{
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
					rtw_p2p_set_state( pwdinfo, P2P_STATE_RX_INVITE_RESP_FAIL );
				}

				if ( rtw_p2p_chk_state( pwdinfo, P2P_STATE_RX_INVITE_RESP_FAIL ) )
				{
					_set_timer( &pwdinfo->restore_p2p_state_timer, 5000 );
				}
				break;
			}
			case P2P_DEVDISC_REQ:

				process_p2p_devdisc_req(pwdinfo, pframe, len);

				break;

			case P2P_DEVDISC_RESP:

				process_p2p_devdisc_resp(pwdinfo, pframe, len);

				break;

			case P2P_PROVISION_DISC_REQ:
				DBG_871X( "[%s] Got Provisioning Discovery Request Frame\n", __FUNCTION__ );
				process_p2p_provdisc_req(pwdinfo, pframe, len);
				_rtw_memcpy(pwdinfo->rx_prov_disc_info.peerDevAddr, GetAddr2Ptr(pframe), ETH_ALEN);

				//20110902 Kurt
				//Add the following statement to avoid receiving duplicate prov disc req. such that pre_p2p_state would be covered.
				if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_RX_PROVISION_DIS_REQ))
					rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
				
				rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_PROVISION_DIS_REQ);
				_set_timer( &pwdinfo->restore_p2p_state_timer, P2P_PROVISION_TIMEOUT );
#ifdef CONFIG_INTEL_WIDI
				if (padapter->mlmepriv.widi_state == INTEL_WIDI_STATE_LISTEN) {
					padapter->mlmepriv.widi_state = INTEL_WIDI_STATE_WFD_CONNECTION;
					_cancel_timer_ex(&(padapter->mlmepriv.listen_timer));
					intel_widi_wk_cmd(padapter, INTEL_WIDI_LISTEN_STOP_WK, NULL, 0);
				}
#endif //CONFIG_INTEL_WIDI
				break;

			case P2P_PROVISION_DISC_RESP:
				//	Commented by Albert 20110707
				//	Should we check the pwdinfo->tx_prov_disc_info.bsent flag here??
				DBG_871X( "[%s] Got Provisioning Discovery Response Frame\n", __FUNCTION__ );
				//	Commented by Albert 20110426
				//	The restore timer is enabled when issuing the provisioing request frame in rtw_p2p_prov_disc function.
				_cancel_timer_ex( &pwdinfo->restore_p2p_state_timer );
				rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_PROVISION_DIS_RSP);
				process_p2p_provdisc_resp(pwdinfo, pframe);
				_set_timer( &pwdinfo->restore_p2p_state_timer, P2P_PROVISION_TIMEOUT );
				break;

		}
	}


exit:

	if(merged_p2pie)
	{
		rtw_mfree(merged_p2pie, merged_p2p_ielen + 2);
	}
#endif //CONFIG_P2P
	return _SUCCESS;
}

unsigned int on_action_public_vendor(union recv_frame *precv_frame)
{
	unsigned int ret = _FAIL;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint frame_len = precv_frame->u.hdr.len;
	u8 *frame_body = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);

	if (_rtw_memcmp(frame_body + 2, P2P_OUI, 4) == _TRUE) {
		if (rtw_action_public_decache(precv_frame, 7) == _FAIL)
			goto exit;

		if (!hal_chk_wl_func(precv_frame->u.hdr.adapter, WL_FUNC_MIRACAST))
			rtw_rframe_del_wfd_ie(precv_frame, 8);

		ret = on_action_public_p2p(precv_frame);
	}

exit:
	return ret;
}

unsigned int on_action_public_default(union recv_frame *precv_frame, u8 action)
{
	unsigned int ret = _FAIL;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint frame_len = precv_frame->u.hdr.len;
	u8 *frame_body = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	u8 token;
	_adapter *adapter = precv_frame->u.hdr.adapter;
	int cnt = 0;
	char msg[64];

	token = frame_body[2];

	if (rtw_action_public_decache(precv_frame, 2) == _FAIL)
		goto exit;

	#ifdef CONFIG_IOCTL_CFG80211
	cnt += sprintf((msg+cnt), "%s(token:%u)", action_public_str(action), token);
	rtw_cfg80211_rx_action(adapter, pframe, frame_len, msg);
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

	/* check RA matches or not */
	if (!_rtw_memcmp(adapter_mac_addr(padapter), GetAddr1Ptr(pframe), ETH_ALEN))
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

unsigned int OnAction_ht(_adapter *padapter, union recv_frame *precv_frame)
{
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint frame_len = precv_frame->u.hdr.len;
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
			rtw_process_ht_action_smps(padapter, GetAddr2Ptr(pframe), frame_body[2]);
#endif /*CONFIG_AP_MODE*/
#endif /*CONFIG_80211N_HT*/		
			break;
	case RTW_WLAN_ACTION_HT_COMPRESS_BEAMFORMING:
#ifdef CONFIG_BEAMFORMING
		/*DBG_871X("RTW_WLAN_ACTION_HT_COMPRESS_BEAMFORMING\n");*/
		beamforming_get_report_frame(padapter, precv_frame);
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
	//Baron
	
	DBG_871X("OnAction_sa_query\n");
	
	switch (pframe[WLAN_HDR_A3_LEN+1])
	{
		case 0: //SA Query req
			_rtw_memcpy(&tid, &pframe[WLAN_HDR_A3_LEN+2], sizeof(u16));
			DBG_871X("OnAction_sa_query request,action=%d, tid=%04x, pframe=%02x-%02x\n"
			, pframe[WLAN_HDR_A3_LEN+1], tid, pframe[WLAN_HDR_A3_LEN+2], pframe[WLAN_HDR_A3_LEN+3]);
			issue_action_SA_Query(padapter, GetAddr2Ptr(pframe), 1, tid, IEEE80211W_RIGHT_KEY);
			break;

		case 1: //SA Query rsp
			psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
			if (psta != NULL)
				_cancel_timer_ex(&psta->dot11w_expire_timer);
			
			_rtw_memcpy(&tid, &pframe[WLAN_HDR_A3_LEN+2], sizeof(u16));
			DBG_871X("OnAction_sa_query response,action=%d, tid=%04x, cancel timer\n", pframe[WLAN_HDR_A3_LEN+1], tid);
			break;
		default:
			break;
	}
	if(0)
	{
		int pp;
		printk("pattrib->pktlen = %d =>", pattrib->pkt_len);
		for(pp=0;pp< pattrib->pkt_len; pp++)
			printk(" %02x ", pframe[pp]);
		printk("\n");
	}	
	
	return _SUCCESS;
}
#endif //CONFIG_IEEE80211W

unsigned int OnAction_wmm(_adapter *padapter, union recv_frame *precv_frame)
{
	return _SUCCESS;
}

unsigned int OnAction_vht(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_80211AC_VHT
	struct rx_pkt_attrib *prxattrib = &precv_frame->u.hdr.attrib;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint frame_len = precv_frame->u.hdr.len;
	struct rtw_ieee80211_hdr_3addr *whdr = (struct rtw_ieee80211_hdr_3addr *)pframe;
	u8 *frame_body = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	u8 category, action;
	struct sta_info *psta = NULL;

	/* check RA matches or not */
	if (!_rtw_memcmp(adapter_mac_addr(padapter), GetAddr1Ptr(pframe), ETH_ALEN))
		goto exit;

	category = frame_body[0];
	if(category != RTW_WLAN_CATEGORY_VHT)
		goto exit;

	action = frame_body[1];
	switch (action) {
	case RTW_WLAN_ACTION_VHT_COMPRESSED_BEAMFORMING:
#ifdef CONFIG_BEAMFORMING
		/*DBG_871X("RTW_WLAN_ACTION_VHT_COMPRESSED_BEAMFORMING\n");*/
		beamforming_get_report_frame(padapter, precv_frame);
#endif /*CONFIG_BEAMFORMING*/
		break;
	case RTW_WLAN_ACTION_VHT_OPMODE_NOTIFICATION:
		// CategoryCode(1) + ActionCode(1) + OpModeNotification(1)
		//DBG_871X("RTW_WLAN_ACTION_VHT_OPMODE_NOTIFICATION\n");
		psta = rtw_get_stainfo(&padapter->stapriv, whdr->addr2);
		if (psta)
			rtw_process_vht_op_mode_notify(padapter, &frame_body[2], psta);
		break;
	default:
		break;
	}

exit:
#endif //CONFIG_80211AC_VHT

	return _SUCCESS;
}

unsigned int OnAction_p2p(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_P2P
	u8 *frame_body;
	u8 category, OUI_Subtype, dialogToken=0;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;
	struct	wifidirect_info	*pwdinfo = &( padapter->wdinfo );

	//check RA matches or not
	if (!_rtw_memcmp(adapter_mac_addr(padapter), GetAddr1Ptr(pframe), ETH_ALEN))
		return _SUCCESS;

	frame_body = (unsigned char *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));

	category = frame_body[0];
	if(category != RTW_WLAN_CATEGORY_P2P)
		return _SUCCESS;

	if ( cpu_to_be32( *( ( u32* ) ( frame_body + 1 ) ) ) != P2POUI )
		return _SUCCESS;

#ifdef CONFIG_IOCTL_CFG80211
	if (adapter_wdev_data(padapter)->p2p_enabled
		&& pwdinfo->driver_interface == DRIVER_CFG80211
		) {
		rtw_cfg80211_rx_action_p2p(padapter, pframe, len);
		return _SUCCESS;
	}
	else
#endif //CONFIG_IOCTL_CFG80211
	{
		len -= sizeof(struct rtw_ieee80211_hdr_3addr);
		OUI_Subtype = frame_body[5];
		dialogToken = frame_body[6];

		switch(OUI_Subtype)
		{
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
#endif //CONFIG_P2P

	return _SUCCESS;

}

unsigned int OnAction(_adapter *padapter, union recv_frame *precv_frame)
{
	int i;
	unsigned char	category;
	struct action_handler *ptable;
	unsigned char	*frame_body;
	u8 *pframe = precv_frame->u.hdr.rx_data; 

	frame_body = (unsigned char *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));
	
	category = frame_body[0];
	
	for(i = 0; i < sizeof(OnAction_tbl)/sizeof(struct action_handler); i++)	
	{
		ptable = &OnAction_tbl[i];
		
		if(category == ptable->num)
			ptable->func(padapter, precv_frame);
	
	}

	return _SUCCESS;

}

unsigned int DoReserved(_adapter *padapter, union recv_frame *precv_frame)
{

	//DBG_871X("rcvd mgt frame(%x, %x)\n", (GetFrameSubType(pframe) >> 4), *(unsigned int *)GetAddr1Ptr(pframe));
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
		DBG_871X(FUNC_ADPT_FMT" alloc xmitframe fail, once:%d\n", FUNC_ADPT_ARG(pxmitpriv->adapter), once);
		goto exit;
	}

	if ((pxmitbuf = rtw_alloc_xmitbuf_ext(pxmitpriv)) == NULL) {
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
	//DBG_871X("%s(): rate = %x\n",__FUNCTION__, rate);
}


void update_monitor_frame_attrib(_adapter *padapter, struct pkt_attrib *pattrib)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8	wireless_mode;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct xmit_priv		*pxmitpriv = &padapter->xmitpriv;
	struct sta_info		*psta = NULL;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct sta_info *pbcmc_sta = NULL;

	psta = rtw_get_stainfo(pstapriv, pattrib->ra);
	pbcmc_sta = rtw_get_bcmc_stainfo(padapter);

	pattrib->hdrlen = 24;
	pattrib->nr_frags = 1;
	pattrib->priority = 7;

	if (pbcmc_sta)
		pattrib->mac_id = pbcmc_sta->mac_id;
	else {
		pattrib->mac_id = 0;
		DBG_871X("mgmt use mac_id 0 will affect RA\n");
	}
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


void update_mgntframe_attrib(_adapter *padapter, struct pkt_attrib *pattrib)
{
	u8	wireless_mode;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct xmit_priv 		*pxmitpriv = &padapter->xmitpriv;
	struct sta_info		*psta = NULL;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct sta_info *pbcmc_sta = NULL;
	//_rtw_memset((u8 *)(pattrib), 0, sizeof(struct pkt_attrib));

	pbcmc_sta = rtw_get_bcmc_stainfo(padapter);

	pattrib->hdrlen = 24;
	pattrib->nr_frags = 1;
	pattrib->priority = 7;

	if (pbcmc_sta)
		pattrib->mac_id = pbcmc_sta->mac_id;
	else {
		pattrib->mac_id = 0;
		DBG_871X("mgmt use mac_id 0 will affect RA\n");
	}
	pattrib->qsel = QSLT_MGNT;

	pattrib->pktlen = 0;

	if (pmlmeext->tx_rate == IEEE80211_CCK_RATE_1MB)
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

	#ifdef CONFIG_BEAMFORMING
	psta = rtw_get_stainfo(pstapriv, pattrib->ra);
	if (psta)
		update_attrib_txbf_info(padapter, pattrib, psta);
	#endif

}

void update_mgntframe_attrib_addr(_adapter *padapter, struct xmit_frame *pmgntframe)
{
	u8	*pframe;
	struct pkt_attrib	*pattrib = &pmgntframe->attrib;

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	_rtw_memcpy(pattrib->ra, GetAddr1Ptr(pframe), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, GetAddr2Ptr(pframe), ETH_ALEN);
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

	if (ret == _SUCCESS)
		ret = rtw_sctx_wait(&sctx, __func__);

	_enter_critical(&pxmitpriv->lock_sctx, &irqL);
	pxmitbuf->sctx = NULL;
	_exit_critical(&pxmitpriv->lock_sctx, &irqL);

	 return ret;
}

s32 dump_mgntframe_and_wait_ack(_adapter *padapter, struct xmit_frame *pmgntframe)
{
#ifdef CONFIG_XMIT_ACK
	static u8 seq_no = 0;
	s32 ret = _FAIL;
	u32 timeout_ms = 500;//  500ms
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->pbuddy_adapter && !padapter->isprimary)
		pxmitpriv = &(padapter->pbuddy_adapter->xmitpriv);
	#endif

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
	if (rtw_hal_mgnt_xmit(padapter, pmgntframe) == _SUCCESS) {
#ifdef CONFIG_XMIT_ACK_POLLING
		ret = rtw_ack_tx_polling(pxmitpriv, timeout_ms);
#else
		ret = rtw_sctx_wait(&(pxmitpriv->ack_tx_ops), __func__);
#endif
	}

	pxmitpriv->ack_tx = _FALSE;
	_exit_critical_mutex(&pxmitpriv->ack_tx_mutex, NULL);

	 return ret;
#else //!CONFIG_XMIT_ACK
	dump_mgntframe(padapter, pmgntframe);
	rtw_msleep_os(50);
	return _SUCCESS;
#endif //!CONFIG_XMIT_ACK	 
}

s32 dump_mgntframe_and_wait_ack_timeout(_adapter *padapter, struct xmit_frame *pmgntframe, int timeout_ms)
{
#ifdef CONFIG_XMIT_ACK
	static u8 seq_no = 0;
	s32 ret = _FAIL;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->pbuddy_adapter && !padapter->isprimary)
		pxmitpriv = &(padapter->pbuddy_adapter->xmitpriv);
	#endif

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
	if (rtw_hal_mgnt_xmit(padapter, pmgntframe) == _SUCCESS) {
#ifdef CONFIG_XMIT_ACK_POLLING
		ret = rtw_ack_tx_polling(pxmitpriv, timeout_ms);
#else
		ret = rtw_sctx_wait(&(pxmitpriv->ack_tx_ops), __func__);
#endif
	}

	pxmitpriv->ack_tx = _FALSE;
	_exit_critical_mutex(&pxmitpriv->ack_tx_mutex, NULL);

	 return ret;
#else //!CONFIG_XMIT_ACK
	dump_mgntframe(padapter, pmgntframe);
	rtw_msleep_os(50);
	return _SUCCESS;
#endif //!CONFIG_XMIT_ACK	 
}

int update_hidden_ssid(u8 *ies, u32 ies_len, u8 hidden_ssid_mode)
{
	u8 *ssid_ie;
	sint ssid_len_ori;
	int len_diff = 0;
	
	ssid_ie = rtw_get_ie(ies,  WLAN_EID_SSID, &ssid_len_ori, ies_len);

	//DBG_871X("%s hidden_ssid_mode:%u, ssid_ie:%p, ssid_len_ori:%d\n", __FUNCTION__, hidden_ssid_mode, ssid_ie, ssid_len_ori);
	
	if(ssid_ie && ssid_len_ori>0)
	{
		switch(hidden_ssid_mode)
		{
			case 1:
			{
				u8 *next_ie = ssid_ie + 2 + ssid_len_ori;
				u32 remain_len = 0;
				
				remain_len = ies_len -(next_ie-ies);
				
				ssid_ie[1] = 0;				
				_rtw_memcpy(ssid_ie+2, next_ie, remain_len);
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

void issue_beacon(_adapter *padapter, int timeout_ms)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	unsigned char	*pframe;
	struct rtw_ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	unsigned int	rate_len;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	_irqL irqL;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
#endif //#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#endif //CONFIG_P2P


	//DBG_871X("%s\n", __FUNCTION__);

#ifdef CONFIG_BCN_ICF
	if ((pmgntframe = rtw_alloc_bcnxmitframe(pxmitpriv)) == NULL)
#else
	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
#endif
	{
		DBG_871X("%s, alloc mgnt frame fail\n", __FUNCTION__);
		return;
	}
#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	_enter_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);
#endif //#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->qsel = QSLT_BEACON;
	#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->iface_type == IFACE_PORT1)	
		pattrib->mbssid = 1;
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
	//pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_BEACON);
	
	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);	
	pattrib->pktlen = sizeof (struct rtw_ieee80211_hdr_3addr);
	
	if( (pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	{
		//DBG_871X("ie len=%d\n", cur_network->IELength);
#ifdef CONFIG_P2P
		// for P2P : Primary Device Type & Device Name
		u32 wpsielen=0, insert_len=0;
		u8 *wpsie=NULL;		
		wpsie = rtw_get_wps_ie(cur_network->IEs+_FIXED_IE_LENGTH_, cur_network->IELength-_FIXED_IE_LENGTH_, NULL, &wpsielen);
		
		if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO) && wpsie && wpsielen>0)
		{
			uint wps_offset, remainder_ielen;
			u8 *premainder_ie, *pframe_wscie;
	
			wps_offset = (uint)(wpsie - cur_network->IEs);

			premainder_ie = wpsie + wpsielen;

			remainder_ielen = cur_network->IELength - wps_offset - wpsielen;

#ifdef CONFIG_IOCTL_CFG80211
			if(adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->driver_interface == DRIVER_CFG80211 )
			{
				if(pmlmepriv->wps_beacon_ie && pmlmepriv->wps_beacon_ie_len>0)
				{
					_rtw_memcpy(pframe, cur_network->IEs, wps_offset);
					pframe += wps_offset;
					pattrib->pktlen += wps_offset;

					_rtw_memcpy(pframe, pmlmepriv->wps_beacon_ie, pmlmepriv->wps_beacon_ie_len);
					pframe += pmlmepriv->wps_beacon_ie_len;
					pattrib->pktlen += pmlmepriv->wps_beacon_ie_len;

					//copy remainder_ie to pframe
					_rtw_memcpy(pframe, premainder_ie, remainder_ielen);
					pframe += remainder_ielen;		
					pattrib->pktlen += remainder_ielen;
				}
				else
				{
					_rtw_memcpy(pframe, cur_network->IEs, cur_network->IELength);
					pframe += cur_network->IELength;
					pattrib->pktlen += cur_network->IELength;
				}
			}
			else
#endif //CONFIG_IOCTL_CFG80211
			{
				pframe_wscie = pframe + wps_offset;
				_rtw_memcpy(pframe, cur_network->IEs, wps_offset+wpsielen);			
				pframe += (wps_offset + wpsielen);		
				pattrib->pktlen += (wps_offset + wpsielen);

				//now pframe is end of wsc ie, insert Primary Device Type & Device Name
				//	Primary Device Type
				//	Type:
				*(u16*) ( pframe + insert_len) = cpu_to_be16( WPS_ATTR_PRIMARY_DEV_TYPE );
				insert_len += 2;
				
				//	Length:
				*(u16*) ( pframe + insert_len ) = cpu_to_be16( 0x0008 );
				insert_len += 2;
				
				//	Value:
				//	Category ID
				*(u16*) ( pframe + insert_len ) = cpu_to_be16( WPS_PDT_CID_MULIT_MEDIA );
				insert_len += 2;

				//	OUI
				*(u32*) ( pframe + insert_len ) = cpu_to_be32( WPSOUI );
				insert_len += 4;

				//	Sub Category ID
				*(u16*) ( pframe + insert_len ) = cpu_to_be16( WPS_PDT_SCID_MEDIA_SERVER );
				insert_len += 2;


				//	Device Name
				//	Type:
				*(u16*) ( pframe + insert_len ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
				insert_len += 2;

				//	Length:
				*(u16*) ( pframe + insert_len ) = cpu_to_be16( pwdinfo->device_name_len );
				insert_len += 2;

				//	Value:
				_rtw_memcpy( pframe + insert_len, pwdinfo->device_name, pwdinfo->device_name_len );
				insert_len += pwdinfo->device_name_len;


				//update wsc ie length
				*(pframe_wscie+1) = (wpsielen -2) + insert_len;

				//pframe move to end
				pframe+=insert_len;
				pattrib->pktlen += insert_len;

				//copy remainder_ie to pframe
				_rtw_memcpy(pframe, premainder_ie, remainder_ielen);
				pframe += remainder_ielen;		
				pattrib->pktlen += remainder_ielen;
			}
		}
		else
#endif //CONFIG_P2P
		{
			int len_diff;
			_rtw_memcpy(pframe, cur_network->IEs, cur_network->IELength);
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
			wps_ie = rtw_get_wps_ie(pmgntframe->buf_addr+TXDESC_OFFSET+sizeof (struct rtw_ieee80211_hdr_3addr)+_BEACON_IE_OFFSET_,
				pattrib->pktlen-sizeof (struct rtw_ieee80211_hdr_3addr)-_BEACON_IE_OFFSET_, NULL, &wps_ielen);
			if (wps_ie && wps_ielen>0) {
				rtw_get_wps_attr_content(wps_ie,  wps_ielen, WPS_ATTR_SELECTED_REGISTRAR, (u8*)(&sr), NULL);
			}
			if (sr != 0)
				set_fwstate(pmlmepriv, WIFI_UNDER_WPS);
			else
				_clr_fwstate_(pmlmepriv, WIFI_UNDER_WPS);
		}

#ifdef CONFIG_P2P
		if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
		{
			u32 len;
#ifdef CONFIG_IOCTL_CFG80211
			if(adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->driver_interface == DRIVER_CFG80211 )
			{
				len = pmlmepriv->p2p_beacon_ie_len;
				if(pmlmepriv->p2p_beacon_ie && len>0)				
					_rtw_memcpy(pframe, pmlmepriv->p2p_beacon_ie, len);
			}
			else
#endif //CONFIG_IOCTL_CFG80211
			{
				len = build_beacon_p2p_ie(pwdinfo, pframe);
			}

			pframe += len;
			pattrib->pktlen += len;

			#ifdef CONFIG_WFD
			len = rtw_append_beacon_wfd_ie(padapter, pframe);
			pframe += len;
			pattrib->pktlen += len;
			#endif
		}
#endif //CONFIG_P2P

		goto _issue_bcn;

	}

	//below for ad-hoc mode

	//timestamp will be inserted by hardware
	pframe += 8;
	pattrib->pktlen += 8;

	// beacon interval: 2 bytes

	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2); 

	pframe += 2;
	pattrib->pktlen += 2;

	// capability info: 2 bytes

	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pattrib->pktlen += 2;

	// SSID
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pattrib->pktlen);

	// supported rates...
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8)? 8: rate_len), cur_network->SupportedRates, &pattrib->pktlen);

	// DS parameter set
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pattrib->pktlen);

	//if( (pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
	{
		u8 erpinfo=0;
		u32 ATIMWindow;
		// IBSS Parameter Set...
		//ATIMWindow = cur->Configuration.ATIMWindow;
		ATIMWindow = 0;
		pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pattrib->pktlen);

		//ERP IE
		pframe = rtw_set_ie(pframe, _ERPINFO_IE_, 1, &erpinfo, &pattrib->pktlen);
	}	


	// EXTERNDED SUPPORTED RATE
	if (rate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pattrib->pktlen);
	}


	//todo:HT for adhoc

_issue_bcn:

#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	pmlmepriv->update_bcn = _FALSE;
	
	_exit_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);	
#endif //#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)

	if ((pattrib->pktlen + TXDESC_SIZE) > 512)
	{
		DBG_871X("beacon frame too large\n");
		return;
	}
	
	pattrib->last_txcmdsz = pattrib->pktlen;

	//DBG_871X("issue bcn_sz=%d\n", pattrib->last_txcmdsz);
	if(timeout_ms > 0)
		dump_mgntframe_and_wait(padapter, pmgntframe, timeout_ms);
	else
		dump_mgntframe(padapter, pmgntframe);

}

void issue_probersp(_adapter *padapter, unsigned char *da, u8 is_valid_p2p_probereq)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;	
	unsigned char					*mac, *bssid;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	u8 *pwps_ie;
	uint wps_ielen;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
#endif //#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	unsigned int	rate_len;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif
#endif //CONFIG_P2P

	//DBG_871X("%s\n", __FUNCTION__);

	if(da == NULL)
		return;

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		return;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		DBG_871X("%s, alloc mgnt frame fail\n", __FUNCTION__);
		return;
	}


	//update attribute
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
	SetFrameSubType(fctrl, WIFI_PROBERSP);
	
	pattrib->hdrlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = pattrib->hdrlen;
	pframe += pattrib->hdrlen;


	if(cur_network->IELength>MAX_IE_SZ)
		return;
	
#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	if( (pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	{
		pwps_ie = rtw_get_wps_ie(cur_network->IEs+_FIXED_IE_LENGTH_, cur_network->IELength-_FIXED_IE_LENGTH_, NULL, &wps_ielen);
	
		//inerset & update wps_probe_resp_ie
		if((pmlmepriv->wps_probe_resp_ie!=NULL) && pwps_ie && (wps_ielen>0))
		{
			uint wps_offset, remainder_ielen;
			u8 *premainder_ie;		
	
			wps_offset = (uint)(pwps_ie - cur_network->IEs);

			premainder_ie = pwps_ie + wps_ielen;

			remainder_ielen = cur_network->IELength - wps_offset - wps_ielen;

			_rtw_memcpy(pframe, cur_network->IEs, wps_offset);		
			pframe += wps_offset;		
			pattrib->pktlen += wps_offset;		

			wps_ielen = (uint)pmlmepriv->wps_probe_resp_ie[1];//to get ie data len
			if((wps_offset+wps_ielen+2)<=MAX_IE_SZ)
			{
				_rtw_memcpy(pframe, pmlmepriv->wps_probe_resp_ie, wps_ielen+2);
				pframe += wps_ielen+2;		
				pattrib->pktlen += wps_ielen+2;	
			}

			if((wps_offset+wps_ielen+2+remainder_ielen)<=MAX_IE_SZ)
			{
				_rtw_memcpy(pframe, premainder_ie, remainder_ielen);
				pframe += remainder_ielen;		
				pattrib->pktlen += remainder_ielen;	
			}
		}
		else
		{
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
			u8 *ies = pmgntframe->buf_addr+TXDESC_OFFSET+sizeof(struct rtw_ieee80211_hdr_3addr);

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

				_rtw_memcpy(buf, remainder_ie, remainder_ielen);
				_rtw_memcpy(remainder_ie+ssid_ielen_diff, buf, remainder_ielen);
				*(ssid_ie+1) = cur_network->Ssid.SsidLength;
				_rtw_memcpy(ssid_ie+2, cur_network->Ssid.Ssid, cur_network->Ssid.SsidLength);

				pframe += ssid_ielen_diff;
				pattrib->pktlen += ssid_ielen_diff;
			}
		}
	}	
	else		
#endif		
	{
	
		//timestamp will be inserted by hardware
		pframe += 8;
		pattrib->pktlen += 8;

		// beacon interval: 2 bytes

		_rtw_memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2); 

		pframe += 2;
		pattrib->pktlen += 2;

		// capability info: 2 bytes

		_rtw_memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

		pframe += 2;
		pattrib->pktlen += 2;

		//below for ad-hoc mode

		// SSID
		pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pattrib->pktlen);

		// supported rates...
		rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8)? 8: rate_len), cur_network->SupportedRates, &pattrib->pktlen);

		// DS parameter set
		pframe =rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pattrib->pktlen);

		if( (pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
		{
			u8 erpinfo=0;
			u32 ATIMWindow;
			// IBSS Parameter Set...
			//ATIMWindow = cur->Configuration.ATIMWindow;
			ATIMWindow = 0;
			pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pattrib->pktlen);

			//ERP IE
			pframe = rtw_set_ie(pframe, _ERPINFO_IE_, 1, &erpinfo, &pattrib->pktlen);
		}

		
		// EXTERNDED SUPPORTED RATE
		if (rate_len > 8)
		{
			pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pattrib->pktlen);
		}


		//todo:HT for adhoc

	}	

#ifdef CONFIG_P2P
	if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)
		/* IOT issue, When wifi_spec is not set, send probe_resp with P2P IE even if probe_req has no P2P IE */
		&& (is_valid_p2p_probereq || !padapter->registrypriv.wifi_spec))
	{
		u32 len;
#ifdef CONFIG_IOCTL_CFG80211
		if(adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->driver_interface == DRIVER_CFG80211 )
		{
			//if pwdinfo->role == P2P_ROLE_DEVICE will call issue_probersp_p2p()
			len = pmlmepriv->p2p_go_probe_resp_ie_len;
			if(pmlmepriv->p2p_go_probe_resp_ie && len>0)
				_rtw_memcpy(pframe, pmlmepriv->p2p_go_probe_resp_ie, len);
		}
		else
#endif //CONFIG_IOCTL_CFG80211
		{
			len = build_probe_resp_p2p_ie(pwdinfo, pframe);
		}

		pframe += len;
		pattrib->pktlen += len;
		
		#ifdef CONFIG_WFD
		len = rtw_append_probe_resp_wfd_ie(padapter, pframe);
		pframe += len;
		pattrib->pktlen += len;
		#endif
	}
#endif //CONFIG_P2P


#ifdef CONFIG_AUTO_AP_MODE
{
	struct sta_info	*psta;
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_871X("(%s)\n", __FUNCTION__);

	//check rc station
	psta = rtw_get_stainfo(pstapriv, da);
	if (psta && psta->isrc && psta->pid>0)
	{
		u8 RC_OUI[4]={0x00,0xE0,0x4C,0x0A};
		u8 RC_INFO[14] = {0};
		//EID[1] + EID_LEN[1] + RC_OUI[4] + MAC[6] + PairingID[2] + ChannelNum[2]
		u16 cu_ch = (u16)cur_network->Configuration.DSConfig;

		DBG_871X("%s, reply rc(pid=0x%x) device "MAC_FMT" in ch=%d\n", __FUNCTION__,
			psta->pid, MAC_ARG(psta->hwaddr), cu_ch);

		//append vendor specific ie
		_rtw_memcpy(RC_INFO, RC_OUI, sizeof(RC_OUI));
		_rtw_memcpy(&RC_INFO[4], mac, ETH_ALEN);
		_rtw_memcpy(&RC_INFO[10], (u8*)&psta->pid, 2);
		_rtw_memcpy(&RC_INFO[12], (u8*)&cu_ch, 2);

		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, sizeof(RC_INFO), RC_INFO, &pattrib->pktlen);
	}
}
#endif //CONFIG_AUTO_AP_MODE


	pattrib->last_txcmdsz = pattrib->pktlen;
	

	dump_mgntframe(padapter, pmgntframe);
	
	return;

}

int _issue_probereq(_adapter *padapter, NDIS_802_11_SSID *pssid, u8 *da, u8 ch, bool append_wps, int wait_ack)
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
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	int	bssrate_len = 0;
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		goto exit;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);


	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	mac = adapter_mac_addr(padapter);

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	if (da)
	{
		//	unicast probe request frame
		_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, da, ETH_ALEN);
	}
	else
	{
		//	broadcast probe request frame
		_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, bc_addr, ETH_ALEN);
	}

	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_PROBEREQ);

	pframe += sizeof (struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof (struct rtw_ieee80211_hdr_3addr);

	if(pssid)
		pframe = rtw_set_ie(pframe, _SSID_IE_, pssid->SsidLength, pssid->Ssid, &(pattrib->pktlen));
	else
		pframe = rtw_set_ie(pframe, _SSID_IE_, 0, NULL, &(pattrib->pktlen));

	get_rate_set(padapter, bssrate, &bssrate_len);

	if (bssrate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , 8, bssrate, &(pattrib->pktlen));
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_ , (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	}
	else
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , bssrate_len , bssrate, &(pattrib->pktlen));
	}

	if (ch)
		pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, &ch, &pattrib->pktlen);

	if (append_wps) {
		//add wps_ie for wps2.0
		if(pmlmepriv->wps_probe_req_ie_len>0 && pmlmepriv->wps_probe_req_ie)
		{
			_rtw_memcpy(pframe, pmlmepriv->wps_probe_req_ie, pmlmepriv->wps_probe_req_ie_len);
			pframe += pmlmepriv->wps_probe_req_ie_len;
			pattrib->pktlen += pmlmepriv->wps_probe_req_ie_len;
			//pmlmepriv->wps_probe_req_ie_len = 0 ;//reset to zero
		}
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_notice_,("issuing probe_req, tx_len=%d\n", pattrib->last_txcmdsz));

	if (wait_ack) {
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	} else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

inline void issue_probereq(_adapter *padapter, NDIS_802_11_SSID *pssid, u8 *da)
{
	_issue_probereq(padapter, pssid, da, 0, 1, _FALSE);
}

/*
 * wait_ms == 0 means that there is no need to wait ack through C2H_CCX_TX_RPT
 * wait_ms > 0 means you want to wait ack through C2H_CCX_TX_RPT, and the value of wait_ms means the interval between each TX
 * try_cnt means the maximal TX count to try
 */
int issue_probereq_ex(_adapter *padapter, NDIS_802_11_SSID *pssid, u8 *da, u8 ch, bool append_wps,
	int try_cnt, int wait_ms)
{
	int ret = _FAIL;
	int i = 0;
	u32 start = rtw_get_current_time();

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		goto exit;

	do
	{
		ret = _issue_probereq(padapter, pssid, da, ch, append_wps, wait_ms>0?_TRUE:_FALSE);

		i++;

		if (RTW_CANNOT_RUN(padapter))
			break;

		if(i < try_cnt && wait_ms > 0 && ret==_FAIL)
			rtw_msleep_os(wait_ms);

	}while((i<try_cnt) && ((ret==_FAIL)||(wait_ms==0)));

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
				ret==_SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
		else
			DBG_871X(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret==_SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
	}
exit:
	return ret;
}

// if psta == NULL, indiate we are station(client) now...
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

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		return;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_AUTH);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);


	if(psta)// for AP mode
	{
#ifdef CONFIG_NATIVEAP_MLME

		_rtw_memcpy(pwlanhdr->addr1, psta->hwaddr, ETH_ALEN);		
		_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, adapter_mac_addr(padapter), ETH_ALEN);

	
		// setting auth algo number
		val16 = (u16)psta->authalg;

		if(status != _STATS_SUCCESSFUL_)
			val16 = 0;

		if (val16)	{
			val16 = cpu_to_le16(val16);	
			use_shared_key = 1;
		}

		pframe = rtw_set_fixed_ie(pframe, _AUTH_ALGM_NUM_, (unsigned char *)&val16, &(pattrib->pktlen));

		// setting auth seq number
		val16 =(u16)psta->auth_seq;
		val16 = cpu_to_le16(val16);	
		pframe = rtw_set_fixed_ie(pframe, _AUTH_SEQ_NUM_, (unsigned char *)&val16, &(pattrib->pktlen));

		// setting status code...
		val16 = status;
		val16 = cpu_to_le16(val16);	
		pframe = rtw_set_fixed_ie(pframe, _STATUS_CODE_, (unsigned char *)&val16, &(pattrib->pktlen));

		// added challenging text...
		if ((psta->auth_seq == 2) && (psta->state & WIFI_FW_AUTH_STATE) && (use_shared_key==1))
		{
			pframe = rtw_set_ie(pframe, _CHLGETXT_IE_, 128, psta->chg_txt, &(pattrib->pktlen));
		}
#endif
	}
	else
	{		
		_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);
	
		// setting auth algo number		
		val16 = (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared)? 1: 0;// 0:OPEN System, 1:Shared key
		if (val16)	{
			val16 = cpu_to_le16(val16);	
			use_shared_key = 1;
		}	
		//DBG_871X("%s auth_algo= %s auth_seq=%d\n",__FUNCTION__,(pmlmeinfo->auth_algo==0)?"OPEN":"SHARED",pmlmeinfo->auth_seq);
		
		//setting IV for auth seq #3
		if ((pmlmeinfo->auth_seq == 3) && (pmlmeinfo->state & WIFI_FW_AUTH_STATE) && (use_shared_key==1))
		{
			//DBG_871X("==> iv(%d),key_index(%d)\n",pmlmeinfo->iv,pmlmeinfo->key_index);
			val32 = ((pmlmeinfo->iv++) | (pmlmeinfo->key_index << 30));
			val32 = cpu_to_le32(val32);
			pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *)&val32, &(pattrib->pktlen));

			pattrib->iv_len = 4;
		}

		pframe = rtw_set_fixed_ie(pframe, _AUTH_ALGM_NUM_, (unsigned char *)&val16, &(pattrib->pktlen));
		
		// setting auth seq number
		val16 = pmlmeinfo->auth_seq;
		val16 = cpu_to_le16(val16);	
		pframe = rtw_set_fixed_ie(pframe, _AUTH_SEQ_NUM_, (unsigned char *)&val16, &(pattrib->pktlen));

		
		// setting status code...
		val16 = status;
		val16 = cpu_to_le16(val16);	
		pframe = rtw_set_fixed_ie(pframe, _STATUS_CODE_, (unsigned char *)&val16, &(pattrib->pktlen));

		// then checking to see if sending challenging text...
		if ((pmlmeinfo->auth_seq == 3) && (pmlmeinfo->state & WIFI_FW_AUTH_STATE) && (use_shared_key==1))
		{
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
	DBG_871X("%s\n", __FUNCTION__);
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
	u8 *ie = pnetwork->IEs; 
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif

#endif //CONFIG_P2P

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		return;

	DBG_871X("%s\n", __FUNCTION__);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);


	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy((void *)GetAddr1Ptr(pwlanhdr), pstat->hwaddr, ETH_ALEN);
	_rtw_memcpy((void *)GetAddr2Ptr(pwlanhdr), adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy((void *)GetAddr3Ptr(pwlanhdr), get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);


	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	if ((pkt_type == WIFI_ASSOCRSP) || (pkt_type == WIFI_REASSOCRSP))
		SetFrameSubType(pwlanhdr, pkt_type);		
	else
		return;

	pattrib->hdrlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen += pattrib->hdrlen;
	pframe += pattrib->hdrlen;

	//capability
	val = *(unsigned short *)rtw_get_capability_from_ie(ie);

	pframe = rtw_set_fixed_ie(pframe, _CAPABILITY_ , (unsigned char *)&val, &(pattrib->pktlen));

	ie_status = cpu_to_le16(status);
	pframe = rtw_set_fixed_ie(pframe , _STATUS_CODE_ , (unsigned char *)&ie_status, &(pattrib->pktlen));
	
	val = cpu_to_le16(pstat->aid | BIT(14) | BIT(15));
	pframe = rtw_set_fixed_ie(pframe, _ASOC_ID_ , (unsigned char *)&val, &(pattrib->pktlen));

	if (pstat->bssratelen <= 8)
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, pstat->bssratelen, pstat->bssrateset, &(pattrib->pktlen));
	}	
	else 
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, pstat->bssrateset, &(pattrib->pktlen));
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (pstat->bssratelen-8), pstat->bssrateset+8, &(pattrib->pktlen));
	}

#ifdef CONFIG_IEEE80211W	
	if (status == _STATS_REFUSED_TEMPORARILY_) {
		u8 timeout_itvl[5];
		u32 timeout_interval = 3000;
		/* Association Comeback time */
		timeout_itvl[0] = 0x03;
		timeout_interval = cpu_to_le32(timeout_interval);
		_rtw_memcpy(timeout_itvl+1, &timeout_interval, 4);
		pframe = rtw_set_ie(pframe, _TIMEOUT_ITVL_IE_, 5, timeout_itvl, &(pattrib->pktlen));
	}
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_80211N_HT
	if ((pstat->flags & WLAN_STA_HT) && (pmlmepriv->htpriv.ht_option))
	{
		uint ie_len=0;
		
		//FILL HT CAP INFO IE
		//p = hostapd_eid_ht_capabilities_info(hapd, p);
		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_CAPABILITY_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if(pbuf && ie_len>0)
		{
			_rtw_memcpy(pframe, pbuf, ie_len+2);
			pframe += (ie_len+2);
			pattrib->pktlen +=(ie_len+2);
		}

		//FILL HT ADD INFO IE
		//p = hostapd_eid_ht_operation(hapd, p);
		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_ADD_INFO_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if(pbuf && ie_len>0)
		{
			_rtw_memcpy(pframe, pbuf, ie_len+2);
			pframe += (ie_len+2);
			pattrib->pktlen +=(ie_len+2);
		}
		
	}	
#endif

	/*adding EXT_CAPAB_IE */
	if (pmlmepriv->ext_capab_ie_len > 0) {
		uint ie_len = 0;
		
		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _EXT_CAP_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if (pbuf && ie_len > 0) {
			_rtw_memcpy(pframe, pbuf, ie_len+2);
			pframe += (ie_len+2);
			pattrib->pktlen += (ie_len+2);
		}
	}	
	
#ifdef CONFIG_80211AC_VHT
	if ((pstat->flags & WLAN_STA_VHT) && (pmlmepriv->vhtpriv.vht_option)
		&& (pstat->wpa_pairwise_cipher != WPA_CIPHER_TKIP)
		&& (pstat->wpa2_pairwise_cipher != WPA_CIPHER_TKIP))
	{
		u32 ie_len=0;

		//FILL VHT CAP IE
		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, EID_VHTCapability, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if(pbuf && ie_len>0)
		{
			_rtw_memcpy(pframe, pbuf, ie_len+2);
			pframe += (ie_len+2);
			pattrib->pktlen +=(ie_len+2);
		}

		//FILL VHT OPERATION IE
		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, EID_VHTOperation, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if(pbuf && ie_len>0)
		{
			_rtw_memcpy(pframe, pbuf, ie_len+2);
			pframe += (ie_len+2);
			pattrib->pktlen +=(ie_len+2);
		}
	}
#endif //CONFIG_80211AC_VHT

	//FILL WMM IE
	if ((pstat->flags & WLAN_STA_WME) && (pmlmepriv->qospriv.qos_option))
	{
		uint ie_len=0;
		unsigned char WMM_PARA_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};	
		
		for (pbuf = ie + _BEACON_IE_OFFSET_; ;pbuf+= (ie_len + 2))
		{			
			pbuf = rtw_get_ie(pbuf, _VENDOR_SPECIFIC_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_ - (ie_len + 2)));	
			if(pbuf && _rtw_memcmp(pbuf+2, WMM_PARA_IE, 6)) 
			{				
				_rtw_memcpy(pframe, pbuf, ie_len+2);
				pframe += (ie_len+2);
				pattrib->pktlen +=(ie_len+2);
				
				break;				
			}
			
			if ((pbuf == NULL) || (ie_len == 0))
			{
				break;
			}			
		}
		
	}


	if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK)
	{
		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, 6 , REALTEK_96B_IE, &(pattrib->pktlen));
	}

	//add WPS IE ie for wps 2.0
	if(pmlmepriv->wps_assoc_resp_ie && pmlmepriv->wps_assoc_resp_ie_len>0)
	{
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
		} else {
			len = build_assoc_resp_p2p_ie(pwdinfo, pframe, pstat->p2p_status_code);
		}
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

	pattrib->last_txcmdsz = pattrib->pktlen;
	
	dump_mgntframe(padapter, pmgntframe);
	
#endif
}

void issue_assocreq(_adapter *padapter)
{
	int ret = _FAIL;
	struct xmit_frame				*pmgntframe;
	struct pkt_attrib				*pattrib;
	unsigned char					*pframe, *p;
	struct rtw_ieee80211_hdr			*pwlanhdr;
	unsigned short				*fctrl;
	unsigned short				val16;
	unsigned int					i, j, ie_len, index=0;
	unsigned char					rf_type, bssrate[NumRates], sta_bssrate[NumRates];
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	int	bssrate_len = 0, sta_bssrate_len = 0;
	u8	vs_ie_length = 0;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	u8					p2pie[ 255 ] = { 0x00 };
	u16					p2pielen = 0;	
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif
#endif //CONFIG_P2P

#ifdef CONFIG_DFS
	u16	cap;

	/* Dot H */
	u8 pow_cap_ele[2] = { 0x00 };
	u8 sup_ch[ 30 * 2 ] = {0x00 }, sup_ch_idx = 0, idx_5g = 2;	//For supported channel
#endif //CONFIG_DFS

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		goto exit;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
		goto exit;

	//update attribute
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
	SetFrameSubType(pframe, WIFI_ASSOCREQ);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	//caps

#ifdef CONFIG_DFS
	_rtw_memcpy(&cap, rtw_get_capability_from_ie(pmlmeinfo->network.IEs), 2);
	cap |= cap_SpecMgmt;
	_rtw_memcpy(pframe, &cap, 2);
#else
	_rtw_memcpy(pframe, rtw_get_capability_from_ie(pmlmeinfo->network.IEs), 2);
#endif //CONFIG_DFS

	pframe += 2;
	pattrib->pktlen += 2;

	//listen interval
	//todo: listen interval for power saving
	val16 = cpu_to_le16(3);
	_rtw_memcpy(pframe ,(unsigned char *)&val16, 2);
	pframe += 2;
	pattrib->pktlen += 2;

	//SSID
	pframe = rtw_set_ie(pframe, _SSID_IE_,  pmlmeinfo->network.Ssid.SsidLength, pmlmeinfo->network.Ssid.Ssid, &(pattrib->pktlen));

#ifdef CONFIG_DFS
	/* Dot H */
	if(pmlmeext->cur_channel > 14)
	{
		pow_cap_ele[0] = 13;	// Minimum transmit power capability
		pow_cap_ele[1] = 21;	// Maximum transmit power capability
		pframe = rtw_set_ie(pframe, EID_PowerCap, 2, pow_cap_ele, &(pattrib->pktlen));

		//supported channels
		do{
			if( pmlmeext->channel_set[sup_ch_idx].ChannelNum <= 14 )
			{
				sup_ch[0] = 1;	//First channel number
				sup_ch[1] = pmlmeext->channel_set[sup_ch_idx].ChannelNum;	//Number of channel
			}
			else
			{
				sup_ch[idx_5g++] = pmlmeext->channel_set[sup_ch_idx].ChannelNum;
				sup_ch[idx_5g++] = 1;
			}
			sup_ch_idx++;
		}
		while( pmlmeext->channel_set[sup_ch_idx].ChannelNum != 0 );
		pframe = rtw_set_ie(pframe, EID_SupportedChannels, idx_5g, sup_ch, &(pattrib->pktlen));
	}
#endif //CONFIG_DFS

	//supported rate & extended supported rate

#if 1	// Check if the AP's supported rates are also supported by STA.
	get_rate_set(padapter, sta_bssrate, &sta_bssrate_len);
	//DBG_871X("sta_bssrate_len=%d\n", sta_bssrate_len);
	
	if(pmlmeext->cur_channel == 14)// for JAPAN, channel 14 can only uses B Mode(CCK) 
	{
		sta_bssrate_len = 4;
	}

	
	//for (i = 0; i < sta_bssrate_len; i++) {
	//	DBG_871X("sta_bssrate[%d]=%02X\n", i, sta_bssrate[i]);
	//}

	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		if (pmlmeinfo->network.SupportedRates[i] == 0) break;
		DBG_871X("network.SupportedRates[%d]=%02X\n", i, pmlmeinfo->network.SupportedRates[i]);
	}
	

	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		if (pmlmeinfo->network.SupportedRates[i] == 0) break;

		
		// Check if the AP's supported rates are also supported by STA.
		for (j=0; j < sta_bssrate_len; j++) {
			 // Avoid the proprietary data rate (22Mbps) of Handlink WSG-4000 AP
			if ( (pmlmeinfo->network.SupportedRates[i]|IEEE80211_BASIC_RATE_MASK) 
					== (sta_bssrate[j]|IEEE80211_BASIC_RATE_MASK)) {
				//DBG_871X("match i = %d, j=%d\n", i, j);
				break;
			} else {
				//DBG_871X("not match: %02X != %02X\n", (pmlmeinfo->network.SupportedRates[i]|IEEE80211_BASIC_RATE_MASK), (sta_bssrate[j]|IEEE80211_BASIC_RATE_MASK));
			}
		}
		
		if (j == sta_bssrate_len) {
			// the rate is not supported by STA
			DBG_871X("%s(): the rate[%d]=%02X is not supported by STA!\n",__FUNCTION__, i, pmlmeinfo->network.SupportedRates[i]);
		} else {
			// the rate is supported by STA
			bssrate[index++] = pmlmeinfo->network.SupportedRates[i];
		}
	}
	
	bssrate_len = index;
	DBG_871X("bssrate_len = %d\n", bssrate_len);

#else	// Check if the AP's supported rates are also supported by STA.
#if 0
	get_rate_set(padapter, bssrate, &bssrate_len);
#else
	for (bssrate_len = 0; bssrate_len < NumRates; bssrate_len++) {
		if (pmlmeinfo->network.SupportedRates[bssrate_len] == 0) break;

		if (pmlmeinfo->network.SupportedRates[bssrate_len] == 0x2C) // Avoid the proprietary data rate (22Mbps) of Handlink WSG-4000 AP
			break;

		bssrate[bssrate_len] = pmlmeinfo->network.SupportedRates[bssrate_len];
	}
#endif
#endif	// Check if the AP's supported rates are also supported by STA.

	if ((bssrate_len == 0) && (pmlmeinfo->network.SupportedRates[0] != 0)) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit; //don't connect to AP if no joint supported rate
	}


	if (bssrate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , 8, bssrate, &(pattrib->pktlen));
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_ , (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	}
	else if (bssrate_len > 0)
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , bssrate_len , bssrate, &(pattrib->pktlen));
	} else {
		DBG_871X("%s: Connect to AP without 11b and 11g data rate!\n",__FUNCTION__);
	}

	//vendor specific IE, such as WPA, WMM, WPS
	for (i = sizeof(NDIS_802_11_FIXED_IEs); i < pmlmeinfo->network.IELength;)
	{
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pmlmeinfo->network.IEs + i);

		switch (pIE->ElementID)
		{
			case _VENDOR_SPECIFIC_IE_:
				if ((_rtw_memcmp(pIE->data, RTW_WPA_OUI, 4)) ||
						(_rtw_memcmp(pIE->data, WMM_OUI, 4)) ||
						(_rtw_memcmp(pIE->data, WPS_OUI, 4)))
				{	
					vs_ie_length = pIE->Length;
					if((!padapter->registrypriv.wifi_spec) && (_rtw_memcmp(pIE->data, WPS_OUI, 4)))
					{
						//Commented by Kurt 20110629
						//In some older APs, WPS handshake
						//would be fail if we append vender extensions informations to AP

						vs_ie_length = 14;
					}
					
					pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, vs_ie_length, pIE->data, &(pattrib->pktlen));
				}
				break;

			case EID_WPA2:
				pframe = rtw_set_ie(pframe, EID_WPA2, pIE->Length, pIE->data, &(pattrib->pktlen));
				break;
#ifdef CONFIG_80211N_HT
			case EID_HTCapability:
				if(padapter->mlmepriv.htpriv.ht_option==_TRUE) {
					if (!(is_ap_in_tkip(padapter)))
					{
						_rtw_memcpy(&(pmlmeinfo->HT_caps), pIE->data, sizeof(struct HT_caps_element));

						pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info = cpu_to_le16(pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info);

						pframe = rtw_set_ie(pframe, EID_HTCapability, pIE->Length , (u8 *)(&(pmlmeinfo->HT_caps)), &(pattrib->pktlen));
					}
				}
				break;

			case EID_EXTCapability:
				if(padapter->mlmepriv.htpriv.ht_option==_TRUE) {
					pframe = rtw_set_ie(pframe, EID_EXTCapability, pIE->Length, pIE->data, &(pattrib->pktlen));
				}
				break;
#endif //CONFIG_80211N_HT
#ifdef CONFIG_80211AC_VHT
			case EID_VHTCapability:
				if (padapter->mlmepriv.vhtpriv.vht_option ==_TRUE) {
					pframe = rtw_set_ie(pframe, EID_VHTCapability, pIE->Length, pIE->data, &(pattrib->pktlen));
				}
				break;

			case EID_OpModeNotification:
				if (padapter->mlmepriv.vhtpriv.vht_option ==_TRUE) {
					pframe = rtw_set_ie(pframe, EID_OpModeNotification, pIE->Length, pIE->data, &(pattrib->pktlen));
				}
				break;
#endif // CONFIG_80211AC_VHT
			default:
				break;
		}

		i += (pIE->Length + 2);
	}

	if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK)
	{
		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, 6 , REALTEK_96B_IE, &(pattrib->pktlen));
	}


#ifdef CONFIG_WAPI_SUPPORT
	rtw_build_assoc_req_wapi_ie(padapter, pframe, pattrib);
#endif


#ifdef CONFIG_P2P

#ifdef CONFIG_IOCTL_CFG80211
	if(adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->driver_interface == DRIVER_CFG80211 )
	{
		if(pmlmepriv->p2p_assoc_req_ie && pmlmepriv->p2p_assoc_req_ie_len>0)
		{
			_rtw_memcpy(pframe, pmlmepriv->p2p_assoc_req_ie, pmlmepriv->p2p_assoc_req_ie_len);
			pframe += pmlmepriv->p2p_assoc_req_ie_len;
			pattrib->pktlen += pmlmepriv->p2p_assoc_req_ie_len;
		}
	}
	else
#endif //CONFIG_IOCTL_CFG80211
	{
		if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) && !rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE))
		{
			//	Should add the P2P IE in the association request frame.	
			//	P2P OUI
			
			p2pielen = 0;
			p2pie[ p2pielen++ ] = 0x50;
			p2pie[ p2pielen++ ] = 0x6F;
			p2pie[ p2pielen++ ] = 0x9A;
			p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

			//	Commented by Albert 20101109
			//	According to the P2P Specification, the association request frame should contain 3 P2P attributes
			//	1. P2P Capability
			//	2. Extended Listen Timing
			//	3. Device Info
			//	Commented by Albert 20110516
			//	4. P2P Interface

			//	P2P Capability
			//	Type:
			p2pie[ p2pielen++ ] = P2P_ATTR_CAPABILITY;

			//	Length:
			*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
			p2pielen += 2;

			//	Value:
			//	Device Capability Bitmap, 1 byte
			p2pie[ p2pielen++ ] = DMP_P2P_DEVCAP_SUPPORT;

			//	Group Capability Bitmap, 1 byte
			if ( pwdinfo->persistent_supported )
				p2pie[ p2pielen++ ] = P2P_GRPCAP_PERSISTENT_GROUP | DMP_P2P_GRPCAP_SUPPORT;
			else
				p2pie[ p2pielen++ ] = DMP_P2P_GRPCAP_SUPPORT;

			//	Extended Listen Timing
			//	Type:
			p2pie[ p2pielen++ ] = P2P_ATTR_EX_LISTEN_TIMING;

			//	Length:
			*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0004 );
			p2pielen += 2;

			//	Value:
			//	Availability Period
			*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF );
			p2pielen += 2;

			//	Availability Interval
			*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF );
			p2pielen += 2;

			//	Device Info
			//	Type:
			p2pie[ p2pielen++ ] = P2P_ATTR_DEVICE_INFO;

			//	Length:
			//	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes) 
			//	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes)
			*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 21 + pwdinfo->device_name_len );
			p2pielen += 2;

			//	Value:
			//	P2P Device Address
			_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
			p2pielen += ETH_ALEN;

			//	Config Method
			//	This field should be big endian. Noted by P2P specification.
			if ( ( pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_PEER_DISPLAY_PIN ) ||
				( pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_SELF_DISPLAY_PIN ) )
			{
				*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_CONFIG_METHOD_DISPLAY );
			}
			else
			{
				*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_CONFIG_METHOD_PBC );
			}

			p2pielen += 2;

			//	Primary Device Type
			//	Category ID
			*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_CID_MULIT_MEDIA );
			p2pielen += 2;

			//	OUI
			*(u32*) ( p2pie + p2pielen ) = cpu_to_be32( WPSOUI );
			p2pielen += 4;

			//	Sub Category ID
			*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_SCID_MEDIA_SERVER );
			p2pielen += 2;

			//	Number of Secondary Device Types
			p2pie[ p2pielen++ ] = 0x00;	//	No Secondary Device Type List

			//	Device Name
			//	Type:
			*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
			p2pielen += 2;

			//	Length:
			*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->device_name_len );
			p2pielen += 2;

			//	Value:
			_rtw_memcpy( p2pie + p2pielen, pwdinfo->device_name, pwdinfo->device_name_len );
			p2pielen += pwdinfo->device_name_len;
		
			//	P2P Interface
			//	Type:
			p2pie[ p2pielen++ ] = P2P_ATTR_INTERFACE;
			
			//	Length:
			*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x000D );
			p2pielen += 2;
			
			//	Value:
			_rtw_memcpy( p2pie + p2pielen, pwdinfo->device_addr, ETH_ALEN );	//	P2P Device Address
			p2pielen += ETH_ALEN;

			p2pie[ p2pielen++ ] = 1;	//	P2P Interface Address Count
			
			_rtw_memcpy( p2pie + p2pielen, pwdinfo->device_addr, ETH_ALEN );	//	P2P Interface Address List
			p2pielen += ETH_ALEN;
		
			pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen );
		}
	}

#endif //CONFIG_P2P

#ifdef CONFIG_WFD
	wfdielen = rtw_append_assoc_req_wfd_ie(padapter, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

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

//when wait_ack is ture, this function shoule be called at process context
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

	//DBG_871X("%s:%d\n", __FUNCTION__, power_mode);

	if(!padapter)
		goto exit;

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		goto exit;

	pxmitpriv = &(padapter->xmitpriv);
	pmlmeext = &(padapter->mlmeextpriv);
	pmlmeinfo = &(pmlmeext->mlmext_info);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->retry_ctrl = _FALSE;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	if((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	{
		SetFrDs(fctrl);
	}
	else if((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
	{
		SetToDs(fctrl);
	}
	
	if (power_mode)
	{
		SetPwrMgt(fctrl);
	}

	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_DATA_NULL);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pattrib->last_txcmdsz = pattrib->pktlen;

	if(wait_ack)
	{
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	}
	else
	{
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

/*
 * [IMPORTANT] Don't call this function in interrupt context
 *
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
	u32 start = rtw_get_current_time();
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_info *psta;

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		goto exit;

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
		ret = _issue_nulldata(padapter, da, power_mode, wait_ms>0?_TRUE:_FALSE);

		i++;

		if (RTW_CANNOT_RUN(padapter))
			break;

		if(i < try_cnt && wait_ms > 0 && ret==_FAIL)
			rtw_msleep_os(wait_ms);

	}while((i<try_cnt) && ((ret==_FAIL)||(wait_ms==0)));

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
				ret==_SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
		else
			DBG_871X(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret==_SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
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
s32 issue_nulldata_in_interrupt(PADAPTER padapter, u8 *da, unsigned int power_mode)
{
	int ret;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;


	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	/* da == NULL, assum it's null data for sta to ap*/
	if (da == NULL)
		da = get_my_bssid(&(pmlmeinfo->network));

	ret = _issue_nulldata(padapter, da, power_mode, _FALSE);

	return ret;
}

//when wait_ack is ture, this function shoule be called at process context
static int _issue_qos_nulldata(_adapter *padapter, unsigned char *da, u16 tid, int wait_ack)
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

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		goto exit;

	DBG_871X("%s\n", __FUNCTION__);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	pattrib->hdrlen +=2;
	pattrib->qos_en = _TRUE;
	pattrib->eosp = 1;
	pattrib->ack_policy = 0;
	pattrib->mdata = 0;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	if((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	{
		SetFrDs(fctrl);
	}
	else if((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
	{
		SetToDs(fctrl);
	}

	if(pattrib->mdata)
		SetMData(fctrl);

	qc = (unsigned short *)(pframe + pattrib->hdrlen - 2);
	
	SetPriority(qc, tid);

	SetEOSP(qc, pattrib->eosp);

	SetAckpolicy(qc, pattrib->ack_policy);

	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_QOS_DATA_NULL);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr_qos);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr_qos);

	pattrib->last_txcmdsz = pattrib->pktlen;
	
	if(wait_ack)
	{
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	}
	else
	{
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
int issue_qos_nulldata(_adapter *padapter, unsigned char *da, u16 tid, int try_cnt, int wait_ms)
{
	int ret = _FAIL;
	int i = 0;
	u32 start = rtw_get_current_time();
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		goto exit;

	/* da == NULL, assum it's null data for sta to ap*/
	if (da == NULL)
		da = get_my_bssid(&(pmlmeinfo->network));
	
	do
	{
		ret = _issue_qos_nulldata(padapter, da, tid, wait_ms>0?_TRUE:_FALSE);

		i++;

		if (RTW_CANNOT_RUN(padapter))
			break;

		if(i < try_cnt && wait_ms > 0 && ret==_FAIL)
			rtw_msleep_os(wait_ms);

	}while((i<try_cnt) && ((ret==_FAIL)||(wait_ms==0)));

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
				ret==_SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
		else
			DBG_871X(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret==_SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
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
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
#endif //CONFIG_P2P	

	//DBG_871X("%s to "MAC_FMT"\n", __func__, MAC_ARG(da));

#ifdef CONFIG_P2P
	if ( !( rtw_p2p_chk_state( pwdinfo, P2P_STATE_NONE ) ) && ( pwdinfo->rx_invitereq_info.scan_op_ch_only ) )
	{
		_cancel_timer_ex( &pwdinfo->reset_ch_sitesurvey );
		_set_timer( &pwdinfo->reset_ch_sitesurvey, 10 );
	}
#endif //CONFIG_P2P

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		goto exit;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		goto exit;
	}

	//update attribute
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
	SetFrameSubType(pframe, WIFI_DEAUTH);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	reason = cpu_to_le16(reason);
	pframe = rtw_set_fixed_ie(pframe, _RSON_CODE_ , (unsigned char *)&reason, &(pattrib->pktlen));

	pattrib->last_txcmdsz = pattrib->pktlen;


	if(wait_ack)
	{
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	}
	else
	{
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

int issue_deauth(_adapter *padapter, unsigned char *da, unsigned short reason)
{
	DBG_871X("%s to "MAC_FMT"\n", __func__, MAC_ARG(da));
	return _issue_deauth(padapter, da, reason, _FALSE, IEEE80211W_RIGHT_KEY);
}

#ifdef CONFIG_IEEE80211W
int issue_deauth_11w(_adapter *padapter, unsigned char *da, unsigned short reason, u8 key_type)
{
	DBG_871X("%s to "MAC_FMT"\n", __func__, MAC_ARG(da));
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
	u32 start = rtw_get_current_time();

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		goto exit;

	do
	{
		ret = _issue_deauth(padapter, da, reason, wait_ms > 0 ? _TRUE:_FALSE, IEEE80211W_RIGHT_KEY);

		i++;

		if (RTW_CANNOT_RUN(padapter))
			break;

		if(i < try_cnt && wait_ms > 0 && ret==_FAIL)
			rtw_msleep_os(wait_ms);

	}while((i<try_cnt) && ((ret==_FAIL)||(wait_ms==0)));

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
				ret==_SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
		else
			DBG_871X(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret==_SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
	}
exit:
	return ret;
}

void issue_action_spct_ch_switch(_adapter *padapter, u8 *ra, u8 new_ch, u8 ch_offset)
{	
	_irqL	irqL;
	_list		*plist, *phead;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char				*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short			*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		return;

	DBG_871X(FUNC_NDEV_FMT" ra="MAC_FMT", ch:%u, offset:%u\n",
		FUNC_NDEV_ARG(padapter->pnetdev), MAC_ARG(ra), new_ch, ch_offset);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
		return;

	//update attribute
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
	SetFrameSubType(pframe, WIFI_ACTION);

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
	struct registry_priv	 	*pregpriv = &padapter->registrypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		return;

	DBG_871X("%s, %04x\n", __FUNCTION__, tid);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		DBG_871X("%s: alloc_mgtxmitframe fail\n", __FUNCTION__);
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->key_type = key_type;
	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	if(raddr)
		_rtw_memcpy(pwlanhdr->addr1, raddr, ETH_ALEN);
	else
		_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie(pframe, 1, &action, &pattrib->pktlen);

	switch (action)
	{
		case 0: //SA Query req
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)&pmlmeext->sa_query_seq, &pattrib->pktlen);
			pmlmeext->sa_query_seq++;
			/* send sa query request to AP, AP should reply sa query response in 1 second */
			if (pattrib->key_type == IEEE80211W_RIGHT_KEY) {
				psta = rtw_get_stainfo(pstapriv, raddr);
				if (psta != NULL) {
					/* DBG_871X("%s, %d, set dot11w_expire_timer\n", __func__, __LINE__); */
					_set_timer(&psta->dot11w_expire_timer, 1000);
				}
			}
			break;

		case 1: //SA Query rsp
			tid = cpu_to_le16(tid);
			/* DBG_871X("rtw_set_fixed_ie, %04x\n", tid); */
			pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)&tid, &pattrib->pktlen);
			break;
		default:
			break;
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);
}
#endif //CONFIG_IEEE80211W

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
	struct registry_priv	 	*pregpriv = &padapter->registrypriv;

#ifdef CONFIG_80211N_HT

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		goto exit;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
		goto exit;

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	//_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr1, raddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));

	if (category == 3)
	{
		switch (action)
		{
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

				BA_para_set = cpu_to_le16(BA_para_set);
				pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(BA_para_set)), &(pattrib->pktlen));

				//BA_timeout_value = 0xffff;//max: 65535 TUs(~ 65 ms)
				BA_timeout_value = 5000;//~ 5ms
				BA_timeout_value = cpu_to_le16(BA_timeout_value);
				pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(BA_timeout_value)), &(pattrib->pktlen));

				//if ((psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress)) != NULL)
				if ((psta = rtw_get_stainfo(pstapriv, raddr)) != NULL)
				{
					start_seq = (psta->sta_xmitpriv.txseq_tid[tid & 0x07]&0xfff) + 1;

					DBG_871X("BA_starting_seqctrl = %d for TID=%d\n", start_seq, tid & 0x07);
					
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
					if(pregpriv->ampdu_amsdu==0)//disabled
						BA_para_set &= ~BIT(0);
					else if(pregpriv->ampdu_amsdu==1)//enabled
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

	if (wait_ack) {
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	} else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
#endif //CONFIG_80211N_HT
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
	DBG_871X(FUNC_ADPT_FMT" ra="MAC_FMT" tid=%u\n"
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
	DBG_871X(FUNC_ADPT_FMT" ra="MAC_FMT" status=%u, tid=%u, size=%u\n"
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
	u32 start = rtw_get_current_time();

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(adapter)))
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
		DBG_871X(FUNC_ADPT_FMT" ra="MAC_FMT" tid=%u%s, %d/%d in %u ms\n"
			, FUNC_ADPT_ARG(adapter), MAC_ARG(ra), tid
			, ret == _SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
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
	DBG_871X(FUNC_ADPT_FMT" ra="MAC_FMT" reason=%u, tid=%u, initiator=%u\n"
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
	u32 start = rtw_get_current_time();

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(adapter)))
		goto exit;

	do {
		ret = issue_action_ba(adapter, ra, RTW_WLAN_ACTION_DELBA
			, tid
			, 0 /* unused */
			, reason
			, initiator
			, wait_ms > 0?_TRUE:_FALSE
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
		DBG_871X(FUNC_ADPT_FMT" ra="MAC_FMT" reason=%u, tid=%u, initiator=%u%s, %d/%d in %u ms\n"
			, FUNC_ADPT_ARG(adapter), MAC_ARG(ra), reason, tid, initiator
			, ret == _SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
	}
exit:
	return ret;
}

static void issue_action_BSSCoexistPacket(_adapter *padapter)
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
	if((pmlmepriv->num_FortyMHzIntolerant==0) || (pmlmepriv->num_sta_no_ht==0))
		return;

	if(_TRUE == pmlmeinfo->bwmode_updated)
		return;

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		return;	

	DBG_871X("%s\n", __FUNCTION__);


	category = RTW_WLAN_CATEGORY_PUBLIC;
	action = ACT_PUBLIC_BSSCOEXIST;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
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
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));


	//
	if(pmlmepriv->num_FortyMHzIntolerant>0)
	{
		u8 iedata=0;
		
		iedata |= BIT(2);//20 MHz BSS Width Request

		pframe = rtw_set_ie(pframe, EID_BSSCoexistence,  1, &iedata, &(pattrib->pktlen));
		
	}
	

	//
	_rtw_memset(ICS, 0, sizeof(ICS));
	if(pmlmepriv->num_sta_no_ht>0)
	{	
		int i;
	
		_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

		phead = get_list_head(queue);
		plist = get_next(phead);
       
		while(1)
		{
			int len;
			u8 *p;
			WLAN_BSSID_EX *pbss_network;
	
			if (rtw_end_of_queue_search(phead,plist)== _TRUE)
				break;		

			pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);      
		
			plist = get_next(plist);

			pbss_network = (WLAN_BSSID_EX *)&pnetwork->network;

			p = rtw_get_ie(pbss_network->IEs + _FIXED_IE_LENGTH_, _HT_CAPABILITY_IE_, &len, pbss_network->IELength - _FIXED_IE_LENGTH_);
			if((p==NULL) || (len==0))//non-HT
			{
				if((pbss_network->Configuration.DSConfig<=0) || (pbss_network->Configuration.DSConfig>14))
					continue;
				
				ICS[0][pbss_network->Configuration.DSConfig]=1;
				
				if(ICS[0][0] == 0)
					ICS[0][0] = 1;		
			}		
	
		}        

		_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);


		for(i= 0;i<8;i++)
		{
			if(ICS[i][0] == 1)
			{
				int j, k = 0;
				
				InfoContent[k] = i;				
				//SET_BSS_INTOLERANT_ELE_REG_CLASS(InfoContent,i);
				k++;
				
				for(j=1;j<=14;j++)
				{
					if(ICS[i][j]==1)
					{
						if(k<16)
						{
							InfoContent[k] = j; //channel number
							//SET_BSS_INTOLERANT_ELE_CHANNEL(InfoContent+k, j);
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
#endif //CONFIG_80211N_HT
}

// Spatial Multiplexing Powersave (SMPS) action frame
int _issue_action_SM_PS(_adapter *padapter ,  unsigned char *raddr , u8 NewMimoPsMode ,  u8 wait_ack)
{

	int ret = _FAIL;
	unsigned char category = RTW_WLAN_CATEGORY_HT;
	u8 action = RTW_WLAN_ACTION_HT_SM_PS; 	
	u8 sm_power_control=0;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);


	if(NewMimoPsMode==WLAN_HT_CAP_SM_PS_DISABLED)
	{
		sm_power_control = sm_power_control  & ~(BIT(0)); // SM Power Save Enable = 0 SM Power Save Disable 
	}
        else if(NewMimoPsMode==WLAN_HT_CAP_SM_PS_STATIC)
        {
                sm_power_control = sm_power_control | BIT(0);    // SM Power Save Enable = 1 SM Power Save Enable  
                sm_power_control = sm_power_control & ~(BIT(1)); // SM Mode = 0 Static Mode
        }
        else if(NewMimoPsMode==WLAN_HT_CAP_SM_PS_DYNAMIC)
        {
                sm_power_control = sm_power_control | BIT(0); // SM Power Save Enable = 1 SM Power Save Enable  
                sm_power_control = sm_power_control | BIT(1); // SM Mode = 1 Dynamic Mode
        }
        else 
		return ret;

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		return ret;

        DBG_871X("%s, sm_power_control=%u, NewMimoPsMode=%u\n", __FUNCTION__ , sm_power_control , NewMimoPsMode );
    
	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
		return ret;

	//update attribute
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
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	/* category, action */
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));

	pframe = rtw_set_fixed_ie(pframe, 1, &(sm_power_control), &(pattrib->pktlen));

	pattrib->last_txcmdsz = pattrib->pktlen;

	if(wait_ack)
	{
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	}
	else
	{
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

        if (ret != _SUCCESS)
            DBG_8192C("%s, ack to\n", __func__);

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
	u32 start = rtw_get_current_time();

	if (rtw_rfctl_is_tx_blocked_by_cac(adapter_to_rfctl(padapter)))
		goto exit;

	do {
		ret = _issue_action_SM_PS(padapter, raddr, NewMimoPsMode , wait_ms>0?_TRUE:_FALSE );

		i++;

		if (RTW_CANNOT_RUN(padapter))
			break;

		if(i < try_cnt && wait_ms > 0 && ret==_FAIL)
			rtw_msleep_os(wait_ms);

	}while((i<try_cnt) && ((ret==_FAIL)||(wait_ms==0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		#ifndef DBG_XMIT_ACK
		goto exit;
		#endif
	}

	if (try_cnt && wait_ms) {
		if (raddr)
			DBG_871X(FUNC_ADPT_FMT" to "MAC_FMT", %s , %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), MAC_ARG(raddr),
				ret==_SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
		else
			DBG_871X(FUNC_ADPT_FMT", %s , %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter),
				ret==_SUCCESS?", acked":"", i, try_cnt, rtw_get_passing_time_ms(start));
	}
exit:

	return ret;
}

int issue_action_SM_PS(_adapter *padapter ,  unsigned char *raddr , u8 NewMimoPsMode )
{
	DBG_871X("%s to "MAC_FMT"\n", __func__, MAC_ARG(raddr));
	return _issue_action_SM_PS(padapter, raddr, NewMimoPsMode , _FALSE );
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
				ret = issue_del_ba_ex(adapter, sta->hwaddr, tid, 37, initiator, 3, 1);
			else
				issue_del_ba(adapter, sta->hwaddr, tid, 37, initiator);

			if (ret == _FAIL && sta->recvreorder_ctrl[tid].enable == _FALSE)
				sta->recvreorder_ctrl[tid].ampdu_size = ampdu_size_bak;
		}
	} else if (initiator == 1) {
		/* originator */
#ifdef CONFIG_80211N_HT
		if (force || sta->htpriv.agg_enable_bitmap & BIT(tid)) {
			sta->htpriv.agg_enable_bitmap &= ~BIT(tid);
			sta->htpriv.candidate_tid_bitmap &= ~BIT(tid);
			issue_del_ba(adapter, sta->hwaddr, tid, 37, initiator);
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

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		if (!(pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS))
			return _SUCCESS;

	psta = rtw_get_stainfo(pstapriv, addr);
	if(psta==NULL)
		return _SUCCESS;

	#if 0
	DBG_871X("%s:%s\n", __func__, (initiator == 0)?"RX_DIR":"TX_DIR");
	if (initiator == 1) /* originator */
		DBG_871X("tx agg_enable_bitmap(0x%08x)\n", psta->htpriv.agg_enable_bitmap);
	#endif

	for (tid = 0; tid < TID_NUM; tid++)
		send_delba_sta_tid(padapter, initiator, psta, tid, 0);

	return _SUCCESS;
}

unsigned int send_beacon(_adapter *padapter)
{
	u8	bxmitok = _FALSE;
	int	issue=0;
	int poll = 0;
#if defined(CONFIG_PCI_HCI) && defined(RTL8814AE_SW_BCN)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
#endif
//#ifdef CONFIG_CONCURRENT_MODE
	//struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	//struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	//_adapter *pbuddy_adapter = padapter->pbuddy_adapter;
	//struct mlme_priv *pbuddy_mlmepriv = &(pbuddy_adapter->mlmepriv);
//#endif		

#ifdef CONFIG_PCI_HCI
	//DBG_871X("%s\n", __FUNCTION__);

	rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);

	/* 8192EE Port select for Beacon DL */
	rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);

	issue_beacon(padapter, 0);

#ifdef RTL8814AE_SW_BCN
	if (pHalData->bCorrectBCN != 0)
		DBG_871X("%s, line%d, Warnning, pHalData->bCorrectBCN != 0\n", __func__, __LINE__);
	pHalData->bCorrectBCN = 1;
#endif

	return _SUCCESS;
#endif

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	u32 start = rtw_get_current_time();

	rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
	rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);
	do{
		issue_beacon(padapter, 100);
		issue++;
		do {
			rtw_yield_os();
			rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, (u8 *)(&bxmitok));
			poll++;
		} while ((poll%10) != 0 && _FALSE == bxmitok && !RTW_CANNOT_RUN(padapter));

	} while (_FALSE == bxmitok && issue < 100 && !RTW_CANNOT_RUN(padapter));

	if (RTW_CANNOT_RUN(padapter))
		return _FAIL;

	
	if(_FALSE == bxmitok)
	{
		DBG_871X("%s fail! %u ms\n", __FUNCTION__, rtw_get_passing_time_ms(start));
		return _FAIL;
	}
	else
	{
		u32 passing_time = rtw_get_passing_time_ms(start);

		if(passing_time > 100 || issue > 3)
			DBG_871X("%s success, issue:%d, poll:%d, %u ms\n", __FUNCTION__, issue, poll, rtw_get_passing_time_ms(start));
		//else
		//	DBG_871X("%s success, issue:%d, poll:%d, %u ms\n", __FUNCTION__, issue, poll, rtw_get_passing_time_ms(start));
		
		rtw_hal_fw_correct_bcn(padapter);

		return _SUCCESS;
	}

#endif

}

/****************************************************************************

Following are some utitity fuctions for WiFi MLME

*****************************************************************************/

BOOLEAN IsLegal5GChannel(
	IN PADAPTER			Adapter,
	IN u8			channel)
{
	
	int i=0;
	u8 Channel_5G[45] = {36,38,40,42,44,46,48,50,52,54,56,58,
		60,62,64,100,102,104,106,108,110,112,114,116,118,120,122,
		124,126,128,130,132,134,136,138,140,149,151,153,155,157,159,
		161,163,165};
	for(i=0;i<sizeof(Channel_5G);i++)
		if(channel == Channel_5G[i])
			return _TRUE;
	return _FALSE;
}

//collect bss info from Beacon and Probe request/response frames.
u8 collect_bss_info(_adapter *padapter, union recv_frame *precv_frame, WLAN_BSSID_EX *bssid)
{
	int	i;
	u32	len;
	u8	*p;
	u16	val16, subtype;
	u8	*pframe = precv_frame->u.hdr.rx_data;
	u32	packet_len = precv_frame->u.hdr.len;
	u8 ie_offset;
	struct registry_priv 	*pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	len = packet_len - sizeof(struct rtw_ieee80211_hdr_3addr);

	if (len > MAX_IE_SZ)
	{
		//DBG_871X("IE too long for survey event\n");
		return _FAIL;
	}

	_rtw_memset(bssid, 0, sizeof(WLAN_BSSID_EX));

	subtype = GetFrameSubType(pframe);

	if(subtype==WIFI_BEACON) {
		bssid->Reserved[0] = 1;
		ie_offset = _BEACON_IE_OFFSET_;
	} else {
		// FIXME : more type
		if (subtype == WIFI_PROBERSP) {
			ie_offset = _PROBERSP_IE_OFFSET_;
			bssid->Reserved[0] = 3;
		}
		else if (subtype == WIFI_PROBEREQ) {
			ie_offset = _PROBEREQ_IE_OFFSET_;
			bssid->Reserved[0] = 2;
		}
		else {
			bssid->Reserved[0] = 0;
			ie_offset = _FIXED_IE_LENGTH_;
		}
	}
		
	bssid->Length = sizeof(WLAN_BSSID_EX) - MAX_IE_SZ + len;

	//below is to copy the information element
	bssid->IELength = len;
	_rtw_memcpy(bssid->IEs, (pframe + sizeof(struct rtw_ieee80211_hdr_3addr)), bssid->IELength);

	//get the signal strength
	//bssid->Rssi = precv_frame->u.hdr.attrib.SignalStrength; // 0-100 index.
	bssid->Rssi = precv_frame->u.hdr.attrib.phy_info.RecvSignalPower; // in dBM.raw data	
	bssid->PhyInfo.SignalQuality = precv_frame->u.hdr.attrib.phy_info.SignalQuality;//in percentage 
	bssid->PhyInfo.SignalStrength = precv_frame->u.hdr.attrib.phy_info.SignalStrength;//in percentage
#ifdef CONFIG_ANTENNA_DIVERSITY
	rtw_hal_get_odm_var(padapter, HAL_ODM_ANTDIV_SELECT, &(bssid->PhyInfo.Optimum_antenna), NULL);
#endif

	// checking SSID
	if ((p = rtw_get_ie(bssid->IEs + ie_offset, _SSID_IE_, &len, bssid->IELength - ie_offset)) == NULL)
	{
		DBG_871X("marc: cannot find SSID for survey event\n");
		return _FAIL;
	}

	if (*(p + 1))
	{
		if (len > NDIS_802_11_LENGTH_SSID)
		{
			DBG_871X("%s()-%d: IE too long (%d) for survey event\n", __FUNCTION__, __LINE__, len);
			return _FAIL;
		}
		_rtw_memcpy(bssid->Ssid.Ssid, (p + 2), *(p + 1));
		bssid->Ssid.SsidLength = *(p + 1);
	}
	else
	{
		bssid->Ssid.SsidLength = 0;
	}

	_rtw_memset(bssid->SupportedRates, 0, NDIS_802_11_LENGTH_RATES_EX);

	//checking rate info...
	i = 0;
	p = rtw_get_ie(bssid->IEs + ie_offset, _SUPPORTEDRATES_IE_, &len, bssid->IELength - ie_offset);
	if (p != NULL)
	{
		if (len > NDIS_802_11_LENGTH_RATES_EX)
		{
			DBG_871X("%s()-%d: IE too long (%d) for survey event\n", __FUNCTION__, __LINE__, len);
			return _FAIL;
		}
		_rtw_memcpy(bssid->SupportedRates, (p + 2), len);
		i = len;
	}

	p = rtw_get_ie(bssid->IEs + ie_offset, _EXT_SUPPORTEDRATES_IE_, &len, bssid->IELength - ie_offset);
	if (p != NULL)
	{
		if (len > (NDIS_802_11_LENGTH_RATES_EX-i))
		{
			DBG_871X("%s()-%d: IE too long (%d) for survey event\n", __FUNCTION__, __LINE__, len);
			return _FAIL;
		}
		_rtw_memcpy(bssid->SupportedRates + i, (p + 2), len);
	}

	//todo:
#if 0
	if (judge_network_type(bssid->SupportedRates, (len + i)) == WIRELESS_11B)
	{
		bssid->NetworkTypeInUse = Ndis802_11DS;
	}
	else
#endif
	{
		bssid->NetworkTypeInUse = Ndis802_11OFDM24;
	}

#ifdef CONFIG_P2P
	if (subtype == WIFI_PROBEREQ)
	{
		u8 *p2p_ie;
		u32	p2p_ielen;
		// Set Listion Channel
		if ((p2p_ie = rtw_get_p2p_ie(bssid->IEs, bssid->IELength, NULL, &p2p_ielen)))
		{
			u32	attr_contentlen = 0;
			u8 listen_ch[5] = { 0x00 };

			rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_LISTEN_CH, listen_ch, &attr_contentlen);
			bssid->Configuration.DSConfig = listen_ch[4];
		} else
		{ // use current channel
			bssid->Configuration.DSConfig = padapter->mlmeextpriv.cur_channel;
			DBG_871X("%s()-%d: Cannot get p2p_ie. set DSconfig to op_ch(%d)\n", __FUNCTION__, __LINE__, bssid->Configuration.DSConfig);
		}

		// FIXME
		bssid->InfrastructureMode = Ndis802_11Infrastructure;
		_rtw_memcpy(bssid->MacAddress, GetAddr2Ptr(pframe), ETH_ALEN);
		bssid->Privacy = 1;
		return _SUCCESS;
	}
#endif //CONFIG_P2P

	if (bssid->IELength < 12)
		return _FAIL;

	// Checking for DSConfig
	p = rtw_get_ie(bssid->IEs + ie_offset, _DSSET_IE_, &len, bssid->IELength - ie_offset);

	bssid->Configuration.DSConfig = 0;
	bssid->Configuration.Length = 0;

	if (p)
	{
		bssid->Configuration.DSConfig = *(p + 2);
	}
	else
	{// In 5G, some ap do not have DSSET IE
		// checking HT info for channel
		p = rtw_get_ie(bssid->IEs + ie_offset, _HT_ADD_INFO_IE_, &len, bssid->IELength - ie_offset);
		if(p)
		{
			struct HT_info_element *HT_info = (struct HT_info_element *)(p + 2);
			bssid->Configuration.DSConfig = HT_info->primary_channel;
		}
		else
		{ // use current channel
			bssid->Configuration.DSConfig = rtw_get_oper_ch(padapter);
		}
	}

	_rtw_memcpy(&bssid->Configuration.BeaconPeriod, rtw_get_beacon_interval_from_ie(bssid->IEs), 2);
	bssid->Configuration.BeaconPeriod = le32_to_cpu(bssid->Configuration.BeaconPeriod);

	val16 = rtw_get_capability((WLAN_BSSID_EX *)bssid);

	if (val16 & BIT(0))
	{
		bssid->InfrastructureMode = Ndis802_11Infrastructure;
		_rtw_memcpy(bssid->MacAddress, GetAddr2Ptr(pframe), ETH_ALEN);
	}
	else
	{
		bssid->InfrastructureMode = Ndis802_11IBSS;
		_rtw_memcpy(bssid->MacAddress, GetAddr3Ptr(pframe), ETH_ALEN);
	}

	if (val16 & BIT(4))
		bssid->Privacy = 1;
	else
		bssid->Privacy = 0;

	bssid->Configuration.ATIMWindow = 0;

	//20/40 BSS Coexistence check
	if((pregistrypriv->wifi_spec==1) && (_FALSE == pmlmeinfo->bwmode_updated))
	{	
		struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
#ifdef CONFIG_80211N_HT
		p = rtw_get_ie(bssid->IEs + ie_offset, _HT_CAPABILITY_IE_, &len, bssid->IELength - ie_offset);
		if(p && len>0)
		{
			struct HT_caps_element	*pHT_caps;
			pHT_caps = (struct HT_caps_element	*)(p + 2);
			
			if(pHT_caps->u.HT_cap_element.HT_caps_info&BIT(14))
			{				
				pmlmepriv->num_FortyMHzIntolerant++;
			}
		}
		else
		{
			pmlmepriv->num_sta_no_ht++;
		}
#endif //CONFIG_80211N_HT
		
	}

#ifdef CONFIG_INTEL_WIDI
	//process_intel_widi_query_or_tigger(padapter, bssid);
	if(process_intel_widi_query_or_tigger(padapter, bssid))
	{
		return _FAIL;
	}
#endif // CONFIG_INTEL_WIDI

	#if defined(DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) & 1
	if(strcmp(bssid->Ssid.Ssid, DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) == 0) {
		DBG_871X("Receiving %s("MAC_FMT", DSConfig:%u) from ch%u with ss:%3u, sq:%3u, RawRSSI:%3ld\n"
			, bssid->Ssid.Ssid, MAC_ARG(bssid->MacAddress), bssid->Configuration.DSConfig
			, rtw_get_oper_ch(padapter)
			, bssid->PhyInfo.SignalStrength, bssid->PhyInfo.SignalQuality, bssid->Rssi
		);
	}
	#endif

	// mark bss info receving from nearby channel as SignalQuality 101
	if(bssid->Configuration.DSConfig != rtw_get_oper_ch(padapter))
	{
		bssid->PhyInfo.SignalQuality= 101;
	}

	return _SUCCESS;
}

void start_create_ibss(_adapter* padapter)
{
	unsigned short	caps;
	u8	val8;
	u8	join_type;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*pnetwork = (WLAN_BSSID_EX*)(&(pmlmeinfo->network));
	u8 doiqk = _FALSE;
	pmlmeext->cur_channel = (u8)pnetwork->Configuration.DSConfig;
	pmlmeinfo->bcn_interval = get_beacon_interval(pnetwork);

	//update wireless mode
	update_wireless_mode(padapter);

	//udpate capability
	caps = rtw_get_capability((WLAN_BSSID_EX *)pnetwork);
	update_capinfo(padapter, caps);
	if(caps&cap_IBSS)//adhoc master
	{
		//set_opmode_cmd(padapter, adhoc);//removed

		val8 = 0xcf;
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		doiqk = _TRUE;
		rtw_hal_set_hwreg(padapter , HW_VAR_DO_IQK , &doiqk);

		//switch channel
		//SelectChannel(padapter, pmlmeext->cur_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE);
		set_channel_bwmode(padapter, pmlmeext->cur_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);

		doiqk = _FALSE;
		rtw_hal_set_hwreg(padapter , HW_VAR_DO_IQK , &doiqk);

		beacon_timing_control(padapter);

		//set msr to WIFI_FW_ADHOC_STATE
		pmlmeinfo->state = WIFI_FW_ADHOC_STATE;
		Set_MSR(padapter, (pmlmeinfo->state & 0x3));

		//issue beacon
		if(send_beacon(padapter)==_FAIL)
		{
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("issuing beacon frame fail....\n"));

			report_join_res(padapter, -1);
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
		}
		else
		{
			rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, padapter->registrypriv.dev_network.MacAddress);
			join_type = 0;
			rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

			report_join_res(padapter, 1);
			pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;
			rtw_indicate_connect(padapter);
		}
	}
	else
	{
		DBG_871X("start_create_ibss, invalid cap:%x\n", caps);
		return;
	}
	//update bc/mc sta_info
	update_bmc_sta(padapter);

}

void start_clnt_join(_adapter* padapter)
{
	unsigned short	caps;
	u8	val8;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*pnetwork = (WLAN_BSSID_EX*)(&(pmlmeinfo->network));
	int beacon_timeout;
	u8 ASIX_ID[]= {0x00, 0x0E, 0xC6};

	//update wireless mode
	update_wireless_mode(padapter);

	//udpate capability
	caps = rtw_get_capability((WLAN_BSSID_EX *)pnetwork);
	update_capinfo(padapter, caps);
	
	//check if sta is ASIX peer and fix IOT issue if it is.
	if (_rtw_memcmp(get_my_bssid(&pmlmeinfo->network) ,ASIX_ID ,3)) {
		u8 iot_flag = _TRUE;
		rtw_hal_set_hwreg(padapter, HW_VAR_ASIX_IOT, (u8 *)(&iot_flag));
	}
	
	if (caps&cap_ESS)
	{
		Set_MSR(padapter, WIFI_FW_STATION_STATE);

		val8 = (pmlmeinfo->auth_algo == dot11AuthAlgrthm_8021X)? 0xcc: 0xcf;

#ifdef CONFIG_WAPI_SUPPORT
		if (padapter->wapiInfo.bWapiEnable && pmlmeinfo->auth_algo == dot11AuthAlgrthm_WAPI)
		{
			//Disable TxUseDefaultKey, RxUseDefaultKey, RxBroadcastUseDefaultKey.
			val8 = 0x4c;
		}
#endif
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		#ifdef CONFIG_DEAUTH_BEFORE_CONNECT
		// Because of AP's not receiving deauth before
		// AP may: 1)not response auth or 2)deauth us after link is complete
		// issue deauth before issuing auth to deal with the situation

		//	Commented by Albert 2012/07/21
		//	For the Win8 P2P connection, it will be hard to have a successful connection if this Wi-Fi doesn't connect to it.
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

			for (pos = get_next(head);!rtw_end_of_queue_search(head, pos); pos = get_next(pos)) {
				
				scanned = LIST_CONTAINOR(pos, struct wlan_network, list);

				if (_rtw_memcmp(&(scanned->network.Ssid), &(pnetwork->Ssid), sizeof(NDIS_802_11_SSID)) == _TRUE
					&& _rtw_memcmp(scanned->network.MacAddress, pnetwork->MacAddress, sizeof(NDIS_802_11_MAC_ADDRESS)) == _TRUE
				) {
					ie_offset = (scanned->network.Reserved[0] == 2? 0:12);
					if (rtw_get_p2p_ie(scanned->network.IEs+ie_offset, scanned->network.IELength-ie_offset, NULL, NULL))
						has_p2p_ie = _TRUE;
					break;
				}
			}
	
			_exit_critical_bh(&(padapter->mlmepriv.scanned_queue.lock), &irqL);

			if (scanned == NULL || rtw_end_of_queue_search(head, pos) || has_p2p_ie == _FALSE)
			#endif /* CONFIG_P2P */
				//To avoid connecting to AP fail during resume process, change retry count from 5 to 1
				issue_deauth_ex(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING, 1, 100);
		}
		#endif /* CONFIG_DEAUTH_BEFORE_CONNECT */

		//here wait for receiving the beacon to start auth
		//and enable a timer
		beacon_timeout = decide_wait_for_beacon_timeout(pmlmeinfo->bcn_interval);
		set_link_timer(pmlmeext, beacon_timeout);	
		_set_timer( &padapter->mlmepriv.assoc_timer, 
			(REAUTH_TO * REAUTH_LIMIT) + (REASSOC_TO*REASSOC_LIMIT) +beacon_timeout);
		
		pmlmeinfo->state = WIFI_FW_AUTH_NULL | WIFI_FW_STATION_STATE;
	}
	else if (caps&cap_IBSS) //adhoc client
	{
		Set_MSR(padapter, WIFI_FW_ADHOC_STATE);

		val8 = 0xcf;
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		beacon_timing_control(padapter);

		pmlmeinfo->state = WIFI_FW_ADHOC_STATE;

		report_join_res(padapter, 1);
	}
	else
	{
		//DBG_871X("marc: invalid cap:%x\n", caps);
		return;
	}

}

void start_clnt_auth(_adapter* padapter)
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


	DBG_871X_LEVEL(_drv_always_, "start auth\n");
	issue_auth(padapter, NULL, 0);

	set_link_timer(pmlmeext, REAUTH_TO);

}


void start_clnt_assoc(_adapter* padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	_cancel_timer_ex(&pmlmeext->link_timer);

	pmlmeinfo->state &= (~(WIFI_FW_AUTH_NULL | WIFI_FW_AUTH_STATE));
	pmlmeinfo->state |= (WIFI_FW_AUTH_SUCCESS | WIFI_FW_ASSOC_STATE);

	issue_assocreq(padapter);

	set_link_timer(pmlmeext, REASSOC_TO);
}

unsigned int receive_disconnect(_adapter *padapter, unsigned char *MacAddr, unsigned short reason, u8 locally_generated)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (!(_rtw_memcmp(MacAddr, get_my_bssid(&pmlmeinfo->network), ETH_ALEN)))
		return _SUCCESS;

	DBG_871X("%s\n", __FUNCTION__);

	if((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
	{
		if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) {
			if (report_del_sta_event(padapter, MacAddr, reason, _TRUE, locally_generated) != _FAIL)
				pmlmeinfo->state = WIFI_FW_NULL_STATE;
		} else if (pmlmeinfo->state & WIFI_FW_LINKING_STATE) {
			if (report_join_res(padapter, -2) != _FAIL)
				pmlmeinfo->state = WIFI_FW_NULL_STATE;
		} else
			DBG_871X(FUNC_ADPT_FMT" - End to Disconnect\n", FUNC_ADPT_ARG(padapter));
	}

	return _SUCCESS;
}

#ifdef CONFIG_80211D
static void process_80211d(PADAPTER padapter, WLAN_BSSID_EX *bssid)
{
	struct registry_priv *pregistrypriv;
	struct mlme_ext_priv *pmlmeext;
	RT_CHANNEL_INFO *chplan_new;
	u8 channel;
	u8 i;


	pregistrypriv = &padapter->registrypriv;
	pmlmeext = &padapter->mlmeextpriv;

	// Adjust channel plan by AP Country IE
	if (pregistrypriv->enable80211d &&
		(!pmlmeext->update_channel_plan_by_ap_done))
	{
		u8 *ie, *p;
		u32 len;
		RT_CHANNEL_PLAN chplan_ap;
		RT_CHANNEL_INFO chplan_sta[MAX_CHANNEL_NUM];
		u8 country[4];
		u8 fcn; // first channel number
		u8 noc; // number of channel
		u8 j, k;

		ie = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _COUNTRY_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
		if (!ie) return;
		if (len < 6) return;

		ie += 2;
		p = ie;
		ie += len;

		_rtw_memset(country, 0, 4);
		_rtw_memcpy(country, p, 3);
		p += 3;
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
				("%s: 802.11d country=%s\n", __FUNCTION__, country));

		i = 0;
		while ((ie - p) >= 3)
		{
			fcn = *(p++);
			noc = *(p++);
			p++;

			for (j = 0; j < noc; j++)
			{
				if (fcn <= 14) channel = fcn + j; // 2.4 GHz
				else channel = fcn + j*4; // 5 GHz

				chplan_ap.Channel[i++] = channel;
			}
		}
		chplan_ap.Len = i;

#ifdef CONFIG_DEBUG_RTL871X
		i = 0;
		DBG_871X("%s: AP[%s] channel plan {", __FUNCTION__, bssid->Ssid.Ssid);
		while ((i < chplan_ap.Len) && (chplan_ap.Channel[i] != 0))
		{
			DBG_8192C("%02d,", chplan_ap.Channel[i]);
			i++;
		}
		DBG_871X("}\n");
#endif

		_rtw_memcpy(chplan_sta, pmlmeext->channel_set, sizeof(chplan_sta));
#ifdef CONFIG_DEBUG_RTL871X
		i = 0;
		DBG_871X("%s: STA channel plan {", __FUNCTION__);
		while ((i < MAX_CHANNEL_NUM) && (chplan_sta[i].ChannelNum != 0))
		{
			DBG_871X("%02d(%c),", chplan_sta[i].ChannelNum, chplan_sta[i].ScanType==SCAN_PASSIVE?'p':'a');
			i++;
		}
		DBG_871X("}\n");
#endif

		_rtw_memset(pmlmeext->channel_set, 0, sizeof(pmlmeext->channel_set));
		chplan_new = pmlmeext->channel_set;

		i = j = k = 0;
		if (pregistrypriv->wireless_mode & WIRELESS_11G)
		{
			do {
				if ((i == MAX_CHANNEL_NUM) ||
					(chplan_sta[i].ChannelNum == 0) ||
					(chplan_sta[i].ChannelNum > 14))
					break;

				if ((j == chplan_ap.Len) || (chplan_ap.Channel[j] > 14))
					break;

				if (chplan_sta[i].ChannelNum == chplan_ap.Channel[j])
				{
					chplan_new[k].ChannelNum = chplan_ap.Channel[j];
					chplan_new[k].ScanType = SCAN_ACTIVE;
					i++;
					j++;
					k++;
				}
				else if (chplan_sta[i].ChannelNum < chplan_ap.Channel[j])
				{
					chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
//					chplan_new[k].ScanType = chplan_sta[i].ScanType;
					chplan_new[k].ScanType = SCAN_PASSIVE;
					i++;
					k++;
				}
				else if (chplan_sta[i].ChannelNum > chplan_ap.Channel[j])
				{
					chplan_new[k].ChannelNum = chplan_ap.Channel[j];
					chplan_new[k].ScanType = SCAN_ACTIVE;
					j++;
					k++;
				}
			} while (1);

			// change AP not support channel to Passive scan
			while ((i < MAX_CHANNEL_NUM) &&
				(chplan_sta[i].ChannelNum != 0) &&
				(chplan_sta[i].ChannelNum <= 14))
			{
				chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
//				chplan_new[k].ScanType = chplan_sta[i].ScanType;
				chplan_new[k].ScanType = SCAN_PASSIVE;
				i++;
				k++;
			}

			// add channel AP supported
			while ((j < chplan_ap.Len) && (chplan_ap.Channel[j] <= 14))
			{
				chplan_new[k].ChannelNum = chplan_ap.Channel[j];
				chplan_new[k].ScanType = SCAN_ACTIVE;
				j++;
				k++;
			}
		}
		else
		{
			// keep original STA 2.4G channel plan
			while ((i < MAX_CHANNEL_NUM) &&
				(chplan_sta[i].ChannelNum != 0) &&
				(chplan_sta[i].ChannelNum <= 14))
			{
				chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
				chplan_new[k].ScanType = chplan_sta[i].ScanType;
				i++;
				k++;
			}

			// skip AP 2.4G channel plan
			while ((j < chplan_ap.Len) && (chplan_ap.Channel[j] <= 14))
			{
				j++;
			}
		}

		if (pregistrypriv->wireless_mode & WIRELESS_11A)
		{
			do {
				if ((i >= MAX_CHANNEL_NUM) ||
					(chplan_sta[i].ChannelNum == 0))
					break;

				if ((j == chplan_ap.Len) || (chplan_ap.Channel[j] == 0))
					break;

				if (chplan_sta[i].ChannelNum == chplan_ap.Channel[j])
				{
					chplan_new[k].ChannelNum = chplan_ap.Channel[j];
					chplan_new[k].ScanType = SCAN_ACTIVE;
					i++;
					j++;
					k++;
				}
				else if (chplan_sta[i].ChannelNum < chplan_ap.Channel[j])
				{
					chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
//					chplan_new[k].ScanType = chplan_sta[i].ScanType;
					chplan_new[k].ScanType = SCAN_PASSIVE;
					i++;
					k++;
				}
				else if (chplan_sta[i].ChannelNum > chplan_ap.Channel[j])
				{
					chplan_new[k].ChannelNum = chplan_ap.Channel[j];
					chplan_new[k].ScanType = SCAN_ACTIVE;
					j++;
					k++;
				}
			} while (1);

			// change AP not support channel to Passive scan
			while ((i < MAX_CHANNEL_NUM) && (chplan_sta[i].ChannelNum != 0))
			{
				chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
//				chplan_new[k].ScanType = chplan_sta[i].ScanType;
				chplan_new[k].ScanType = SCAN_PASSIVE;
				i++;
				k++;
			}

			// add channel AP supported
			while ((j < chplan_ap.Len) && (chplan_ap.Channel[j] != 0))
			{
				chplan_new[k].ChannelNum = chplan_ap.Channel[j];
				chplan_new[k].ScanType = SCAN_ACTIVE;
				j++;
				k++;
			}
		}
		else
		{
			// keep original STA 5G channel plan
			while ((i < MAX_CHANNEL_NUM) && (chplan_sta[i].ChannelNum != 0))
			{
				chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
				chplan_new[k].ScanType = chplan_sta[i].ScanType;
				i++;
				k++;
			}
		}

		pmlmeext->update_channel_plan_by_ap_done = 1;

#ifdef CONFIG_DEBUG_RTL871X
		k = 0;
		DBG_871X("%s: new STA channel plan {", __FUNCTION__);
		while ((k < MAX_CHANNEL_NUM) && (chplan_new[k].ChannelNum != 0))
		{
			DBG_871X("%02d(%c),", chplan_new[k].ChannelNum, chplan_new[k].ScanType==SCAN_PASSIVE?'p':'c');
			k++;
		}
		DBG_871X("}\n");
#endif

#if 0
		// recover the right channel index
		channel = chplan_sta[pmlmeext->sitesurvey_res.channel_idx].ChannelNum;
		k = 0;
		while ((k < MAX_CHANNEL_NUM) && (chplan_new[k].ChannelNum != 0))
		{
			if (chplan_new[k].ChannelNum == channel) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
						 ("%s: change mlme_ext sitesurvey channel index from %d to %d\n",
						  __FUNCTION__, pmlmeext->sitesurvey_res.channel_idx, k));
				pmlmeext->sitesurvey_res.channel_idx = k;
				break;
			}
			k++;
		}
#endif
	}

	// If channel is used by AP, set channel scan type to active
	channel = bssid->Configuration.DSConfig;
	chplan_new = pmlmeext->channel_set;
	i = 0;
	while ((i < MAX_CHANNEL_NUM) && (chplan_new[i].ChannelNum != 0))
	{
		if (chplan_new[i].ChannelNum == channel)
		{
			if (chplan_new[i].ScanType == SCAN_PASSIVE)
			{
				//5G Bnad 2, 3 (DFS) doesn't change to active scan
				if(channel >= 52 && channel <= 144)
					break;
				
				chplan_new[i].ScanType = SCAN_ACTIVE;
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
						 ("%s: change channel %d scan type from passive to active\n",
						  __FUNCTION__, channel));
			}
			break;
		}
		i++;
	}
}
#endif

/****************************************************************************

Following are the functions to report events

*****************************************************************************/

void report_survey_event(_adapter *padapter, union recv_frame *precv_frame)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct survey_event	*psurvey_evt;
	struct C2HEvent_Header *pc2h_evt_hdr;
	struct mlme_ext_priv *pmlmeext;
	struct cmd_priv *pcmdpriv;
	//u8 *pframe = precv_frame->u.hdr.rx_data;
	//uint len = precv_frame->u.hdr.len;

	if(!padapter)
		return;

	pmlmeext = &padapter->mlmeextpriv;
	pcmdpriv = &padapter->cmdpriv;
	

	if ((pcmd_obj = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
	{
		return;
	}

	cmdsz = (sizeof(struct survey_event) + sizeof(struct C2HEvent_Header));
	if ((pevtcmd = (u8*)rtw_zmalloc(cmdsz)) == NULL)
	{
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		return;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct survey_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_Survey);
	pc2h_evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	psurvey_evt = (struct survey_event*)(pevtcmd + sizeof(struct C2HEvent_Header));

	if (collect_bss_info(padapter, precv_frame, (WLAN_BSSID_EX *)&psurvey_evt->bss) == _FAIL)
	{
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		rtw_mfree((u8 *)pevtcmd, cmdsz);
		return;
	}

#ifdef CONFIG_80211D
	process_80211d(padapter, &psurvey_evt->bss);
#endif

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	pmlmeext->sitesurvey_res.bss_cnt++;

	return;

}

void report_surveydone_event(_adapter *padapter)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct surveydone_event *psurveydone_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	if ((pcmd_obj = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
	{
		return;
	}

	cmdsz = (sizeof(struct surveydone_event) + sizeof(struct C2HEvent_Header));
	if ((pevtcmd = (u8*)rtw_zmalloc(cmdsz)) == NULL)
	{
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		return;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct surveydone_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_SurveyDone);
	pc2h_evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	psurveydone_evt = (struct surveydone_event*)(pevtcmd + sizeof(struct C2HEvent_Header));
	psurveydone_evt->bss_cnt = pmlmeext->sitesurvey_res.bss_cnt;

	DBG_871X("survey done event(%x) band:%d for "ADPT_FMT"\n", psurveydone_evt->bss_cnt, padapter->setband, ADPT_ARG(padapter));

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;

}

u32 report_join_res(_adapter *padapter, int res)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct joinbss_event		*pjoinbss_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u32 ret = _FAIL;

	if ((pcmd_obj = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
		goto exit;

	cmdsz = (sizeof(struct joinbss_event) + sizeof(struct C2HEvent_Header));
	pevtcmd = (u8 *)rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		goto exit;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct joinbss_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_JoinBss);
	pc2h_evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	pjoinbss_evt = (struct joinbss_event*)(pevtcmd + sizeof(struct C2HEvent_Header));
	_rtw_memcpy((unsigned char *)(&(pjoinbss_evt->network.network)), &(pmlmeinfo->network), sizeof(WLAN_BSSID_EX));
	pjoinbss_evt->network.join_res 	= pjoinbss_evt->network.aid = res;

	DBG_871X("report_join_res(%d)\n", res);
	
	
	rtw_joinbss_event_prehandle(padapter, (u8 *)&pjoinbss_evt->network);
	
	
	ret = rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

exit:
	return ret;
}

void report_wmm_edca_update(_adapter *padapter)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct wmm_event		*pwmm_event;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	if ((pcmd_obj = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
	{
		return;
	}

	cmdsz = (sizeof(struct wmm_event) + sizeof(struct C2HEvent_Header));
	if ((pevtcmd = (u8*)rtw_zmalloc(cmdsz)) == NULL)
	{
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		return;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct wmm_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_WMM);
	pc2h_evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	pwmm_event = (struct wmm_event*)(pevtcmd + sizeof(struct C2HEvent_Header));
	pwmm_event->wmm =0;
	
	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;

}

u32 report_del_sta_event(_adapter *padapter, unsigned char *MacAddr, unsigned short reason, bool enqueue, u8 locally_generated)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct sta_info *psta;
	int	mac_id = -1;
	struct stadel_event			*pdel_sta_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	/* prepare cmd parameter */
	cmdsz = (sizeof(struct stadel_event) + sizeof(struct C2HEvent_Header));
	pevtcmd = (u8 *)rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		res = _FAIL;
		goto exit;
	}

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct stadel_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_DelSTA);
	pc2h_evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	pdel_sta_evt = (struct stadel_event*)(pevtcmd + sizeof(struct C2HEvent_Header));
	_rtw_memcpy((unsigned char *)(&(pdel_sta_evt->macaddr)), MacAddr, ETH_ALEN);
	_rtw_memcpy((unsigned char *)(pdel_sta_evt->rsvd),(unsigned char *)(&reason),2);
	psta = rtw_get_stainfo(&padapter->stapriv, MacAddr);
	if(psta)
		mac_id = (int)psta->mac_id;	
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
		pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
		pcmd_obj->cmdsz = cmdsz;
		pcmd_obj->parmbuf = pevtcmd;

		pcmd_obj->rsp = NULL;
		pcmd_obj->rspsz  = 0;

		res = rtw_enqueue_cmd(pcmdpriv, pcmd_obj);
	}

exit:

	DBG_871X(FUNC_ADPT_FMT" "MAC_FMT" mac_id=%d, enqueue:%d, res:%u\n"
		, FUNC_ADPT_ARG(padapter), MAC_ARG(MacAddr), mac_id, enqueue, res);

	return res;
}

void report_add_sta_event(_adapter *padapter, unsigned char *MacAddr)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct stassoc_event		*padd_sta_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	if ((pcmd_obj = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
	{
		return;
	}

	cmdsz = (sizeof(struct stassoc_event) + sizeof(struct C2HEvent_Header));
	if ((pevtcmd = (u8*)rtw_zmalloc(cmdsz)) == NULL)
	{
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		return;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct stassoc_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_AddSTA);
	pc2h_evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	padd_sta_evt = (struct stassoc_event*)(pevtcmd + sizeof(struct C2HEvent_Header));
	_rtw_memcpy((unsigned char *)(&(padd_sta_evt->macaddr)), MacAddr, ETH_ALEN);

	DBG_871X("report_add_sta_event: add STA\n");

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
		if (get_iface_type(dvobj->padapters[i]) == IFACE_PORT0) {
			if_port0 = dvobj->padapters[i];
			if_port0_mlmeinfo = &(if_port0->mlmeextpriv.mlmext_info);
		}
		else if (get_iface_type(dvobj->padapters[i]) == IFACE_PORT1) {
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
	DBG_871X(FUNC_ADPT_FMT" wowlan_mode:%u\n"
		ADPT_FMT", port0, mlmeinfo->state:0x%08x, p2p_state:%d, %d\n"
		ADPT_FMT", port1, mlmeinfo->state:0x%08x, p2p_state:%d, %d\n",
		FUNC_ADPT_ARG(adapter), pwrctl->wowlan_mode,
		ADPT_ARG(if_port0), if_port0_mlmeinfo->state, rtw_p2p_state(&if_port0->wdinfo), rtw_p2p_chk_state(&if_port0->wdinfo, P2P_STATE_NONE),
		ADPT_ARG(if_port1), if_port1_mlmeinfo->state, rtw_p2p_state(&if_port1->wdinfo), rtw_p2p_chk_state(&if_port1->wdinfo, P2P_STATE_NONE));
#endif /* DBG_RUNTIME_PORT_SWITCH */

#ifdef CONFIG_WOWLAN
	/* WOWLAN interface(primary, for now) should be port0 */
	if (pwrctl->wowlan_mode == _TRUE) {
		if(!is_primary_adapter(if_port0)) {
			DBG_871X("%s "ADPT_FMT" enable WOWLAN\n", __func__, ADPT_ARG(if_port1));
			switch_needed = _TRUE;
		}
		goto exit;
	}
#endif /* CONFIG_WOWLAN */

	/* AP should use port0 for ctl frame's ack */
	if ((if_port1_mlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) {
		DBG_871X("%s "ADPT_FMT" is AP/GO\n", __func__, ADPT_ARG(if_port1));
		switch_needed = _TRUE;
		goto exit;
	}

	/* GC should use port0 for p2p ps */	
	if (((if_port1_mlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE)
		&& (if_port1_mlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
		&& !rtw_p2p_chk_state(&if_port1->wdinfo, P2P_STATE_NONE)
		&& !check_fwstate(&if_port1->mlmepriv, WIFI_UNDER_WPS)
	) {
		DBG_871X("%s "ADPT_FMT" is GC\n", __func__, ADPT_ARG(if_port1));
		switch_needed = _TRUE;
		goto exit;
	}

	/* port1 linked, but port0 not linked */
	if ((if_port1_mlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
		&& !(if_port0_mlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
		&& ((if_port0_mlmeinfo->state & 0x03) != WIFI_FW_AP_STATE)
	) {
		DBG_871X("%s "ADPT_FMT" is SINGLE_LINK\n", __func__, ADPT_ARG(if_port1));
		switch_needed = _TRUE;
		goto exit;
	}

exit:
#ifdef DBG_RUNTIME_PORT_SWITCH
	DBG_871X(FUNC_ADPT_FMT" ret:%d\n", FUNC_ADPT_ARG(adapter), switch_needed);
#endif /* DBG_RUNTIME_PORT_SWITCH */
#endif /* CONFIG_RUNTIME_PORT_SWITCH */
#endif /* CONFIG_CONCURRENT_MODE */
	return switch_needed;
}

/****************************************************************************

Following are the event callback functions

*****************************************************************************/

//for sta/adhoc mode
void update_sta_info(_adapter *padapter, struct sta_info *psta)
{
	_irqL	irqL;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	//ERP
	VCS_update(padapter, psta);

#ifdef CONFIG_80211N_HT
	//HT
	if(pmlmepriv->htpriv.ht_option)
	{
		psta->htpriv.ht_option = _TRUE;

		psta->htpriv.ampdu_enable = pmlmepriv->htpriv.ampdu_enable;

		psta->htpriv.rx_ampdu_min_spacing = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para&IEEE80211_HT_CAP_AMPDU_DENSITY)>>2;

		if (support_short_GI(padapter, &(pmlmeinfo->HT_caps), CHANNEL_WIDTH_20))
			psta->htpriv.sgi_20m = _TRUE;

		if (support_short_GI(padapter, &(pmlmeinfo->HT_caps), CHANNEL_WIDTH_40))
			psta->htpriv.sgi_40m = _TRUE;

		psta->qos_option = _TRUE;

		psta->htpriv.ldpc_cap = pmlmepriv->htpriv.ldpc_cap;
		psta->htpriv.stbc_cap = pmlmepriv->htpriv.stbc_cap;
		psta->htpriv.beamform_cap = pmlmepriv->htpriv.beamform_cap;

		_rtw_memcpy(&psta->htpriv.ht_cap, &pmlmeinfo->HT_caps, sizeof(struct rtw_ieee80211_ht_cap));
	}
	else
#endif //CONFIG_80211N_HT
	{
#ifdef CONFIG_80211N_HT
		psta->htpriv.ht_option = _FALSE;

		psta->htpriv.ampdu_enable = _FALSE;
		
		psta->htpriv.sgi_20m = _FALSE;
		psta->htpriv.sgi_40m = _FALSE;
#endif //CONFIG_80211N_HT
		psta->qos_option = _FALSE;

	}

#ifdef CONFIG_80211N_HT
	psta->htpriv.ch_offset = pmlmeext->cur_ch_offset;
	
	psta->htpriv.agg_enable_bitmap = 0x0;//reset
	psta->htpriv.candidate_tid_bitmap = 0x0;//reset
#endif //CONFIG_80211N_HT

	psta->bw_mode = pmlmeext->cur_bwmode;

	//QoS
	if(pmlmepriv->qospriv.qos_option)
		psta->qos_option = _TRUE;

#ifdef CONFIG_80211AC_VHT
	_rtw_memcpy(&psta->vhtpriv, &pmlmepriv->vhtpriv, sizeof(struct vht_priv));
#endif //CONFIG_80211AC_VHT

	update_ldpc_stbc_cap(psta);

	_enter_critical_bh(&psta->lock, &irqL);
	psta->state = _FW_LINKED;
	_exit_critical_bh(&psta->lock, &irqL);

}

static void rtw_mlmeext_disconnect(_adapter *padapter)
{
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*pnetwork = (WLAN_BSSID_EX*)(&(pmlmeinfo->network));
	u8 state_backup = (pmlmeinfo->state&0x03);
	u8 ASIX_ID[]= {0x00, 0x0E, 0xC6};

	//set_opmode_cmd(padapter, infra_client_with_mlme);

	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_DISCONNECT, 0);
	rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, null_addr);

	//set MSR to no link state -> infra. mode
	Set_MSR(padapter, _HW_STATE_STATION_);

	//check if sta is ASIX peer and fix IOT issue if it is.
	if (_rtw_memcmp(get_my_bssid(&pmlmeinfo->network) ,ASIX_ID ,3)) {
		u8 iot_flag = _FALSE;
		rtw_hal_set_hwreg(padapter, HW_VAR_ASIX_IOT, (u8 *)(&iot_flag));
	}
	pmlmeinfo->state = WIFI_FW_NULL_STATE;

	if(state_backup == WIFI_FW_STATION_STATE)
	{
		if (rtw_port_switch_chk(padapter) == _TRUE) {
			rtw_hal_set_hwreg(padapter, HW_VAR_PORT_SWITCH, NULL);
			#ifdef CONFIG_LPS
			{
				_adapter *port0_iface = dvobj_get_port0_adapter(adapter_to_dvobj(padapter));
				if (port0_iface)
					rtw_lps_ctrl_wk_cmd(port0_iface, LPS_CTRL_CONNECT, 0);
			}
			#endif
		}
	}

	/* switch to the 20M Hz mode after disconnect */
	pmlmeext->cur_bwmode = CHANNEL_WIDTH_20;
	pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

#ifdef CONFIG_FCS_MODE
	if (EN_FCS(padapter))
		rtw_hal_set_hwreg(padapter, HW_VAR_STOP_FCS_MODE, NULL);
#endif

#ifdef CONFIG_DFS_MASTER
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE))
		rtw_dfs_master_status_apply(padapter, MLME_AP_STOPPED);
	else if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
		rtw_dfs_master_status_apply(padapter, MLME_STA_DISCONNECTED);
#endif

	{
		u8 ch, bw, offset;

		if (rtw_get_ch_setting_union_no_self(padapter, &ch, &bw, &offset) != 0)
			set_channel_bwmode(padapter, ch, offset, bw);
	}

	flush_all_cam_entry(padapter);

	_cancel_timer_ex(&pmlmeext->link_timer);

	//pmlmepriv->LinkDetectInfo.TrafficBusyState = _FALSE;
	pmlmepriv->LinkDetectInfo.TrafficTransitionCount = 0;
	pmlmepriv->LinkDetectInfo.LowPowerTransitionCount = 0;

#ifdef CONFIG_TDLS
	padapter->tdlsinfo.ap_prohibited = _FALSE;

	/* For TDLS channel switch, currently we only allow it to work in wifi logo test mode */
	if (padapter->registrypriv.wifi_spec == 1)
	{
		padapter->tdlsinfo.ch_switch_prohibited = _FALSE;
	}
#endif /* CONFIG_TDLS */

}

void mlmeext_joinbss_event_callback(_adapter *padapter, int join_res)
{
	struct sta_info		*psta, *psta_bmc;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	struct sta_priv		*pstapriv = &padapter->stapriv;
	u8	join_type;
#ifdef CONFIG_ARP_KEEP_ALIVE
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
#endif
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	if(join_res < 0)
	{
		join_type = 1;
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));
		rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, null_addr);

		goto exit_mlmeext_joinbss_event_callback;
	}
#ifdef CONFIG_ARP_KEEP_ALIVE
	pmlmepriv->bGetGateway = 1;
#endif

	if((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
	{
		//update bc/mc sta_info
		update_bmc_sta(padapter);
	}


	//turn on dynamic functions
	/* Switch_DM_Func(padapter, DYNAMIC_ALL_FUNC_ENABLE, _TRUE); */

	// update IOT-releated issue
	update_IOT_info(padapter);

	rtw_hal_set_hwreg(padapter, HW_VAR_BASIC_RATE, cur_network->SupportedRates);

	//BCN interval
	rtw_hal_set_hwreg(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&pmlmeinfo->bcn_interval));

	//udpate capability
	update_capinfo(padapter, pmlmeinfo->capability);

	//WMM, Update EDCA param
	WMMOnAssocRsp(padapter);

	//HT
	HTOnAssocRsp(padapter);

#ifdef CONFIG_80211AC_VHT
	//VHT
	VHTOnAssocRsp(padapter);
#endif

	psta = rtw_get_stainfo(pstapriv, cur_network->MacAddress);
	if (psta) //only for infra. mode
	{
		//DBG_871X("set_sta_rate\n");

		psta->wireless_mode = pmlmeext->cur_wireless_mode;
	
		//set per sta rate after updating HT cap.
		set_sta_rate(padapter, psta);
		
		rtw_sta_media_status_rpt(padapter, psta, 1);

		/* wakeup macid after join bss successfully to ensure 
			the subsequent data frames can be sent out normally */
		rtw_hal_macid_wakeup(padapter, psta->mac_id);
	}

#ifndef CONFIG_IOCTL_CFG80211
	if (is_wep_enc(psecuritypriv->dot11PrivacyAlgrthm))
		rtw_sec_restore_wep_key(padapter);
#endif /* CONFIG_IOCTL_CFG80211 */

	if (rtw_port_switch_chk(padapter) == _TRUE)
		rtw_hal_set_hwreg(padapter, HW_VAR_PORT_SWITCH, NULL);

	join_type = 2;
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

	if((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
	{
		// correcting TSF
		correct_TSF(padapter, pmlmeext);
	
		//set_link_timer(pmlmeext, DISCONNECT_TO);
	}

#ifdef CONFIG_LPS
	if(get_iface_type(padapter) == IFACE_PORT0)
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_CONNECT, 0);
#endif

#ifdef CONFIG_BEAMFORMING
	if (psta)
		beamforming_wk_cmd(padapter, BEAMFORMING_CTRL_ENTER, (u8 *)psta, sizeof(struct sta_info), 0);
#endif/*CONFIG_BEAMFORMING*/

exit_mlmeext_joinbss_event_callback:

	rtw_join_done_chk_ch(padapter, join_res);

	DBG_871X("=>%s - End to Connection without 4-way\n", __FUNCTION__);
}

//currently only adhoc mode will go here
void mlmeext_sta_add_event_callback(_adapter *padapter, struct sta_info *psta)
{
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	join_type;

	DBG_871X("%s\n", __FUNCTION__);

	if((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
	{
		if(pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)//adhoc master or sta_count>1
		{
			//nothing to do
		}
		else//adhoc client
		{
			//update TSF Value
			//update_TSF(pmlmeext, pframe, len);			

			// correcting TSF
			correct_TSF(padapter, pmlmeext);

			//start beacon
			if (send_beacon(padapter) == _FAIL)
				rtw_warn_on(1);

			pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;
		}

		join_type = 2;
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));
	}

	//update adhoc sta_info
	update_sta_info(padapter, psta);

	rtw_hal_update_sta_rate_mask(padapter, psta);

	// ToDo: HT for Ad-hoc 
	psta->wireless_mode = rtw_check_network_type(psta->bssrateset, psta->bssratelen, pmlmeext->cur_channel);
	psta->raid = rtw_hal_networktype_to_raid(padapter, psta);

	//rate radaptive
	Update_RA_Entry(padapter, psta);
}

void mlmeext_sta_del_event_callback(_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (is_client_associated_to_ap(padapter) || is_IBSS_empty(padapter))
	{
		rtw_mlmeext_disconnect(padapter);
	}

}

/****************************************************************************

Following are the functions for the timer handlers

*****************************************************************************/
void _linked_info_dump(_adapter *padapter)
{
	int i;
	struct mlme_ext_priv    *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info    *pmlmeinfo = &(pmlmeext->mlmext_info);
	HAL_DATA_TYPE *HalData = GET_HAL_DATA(padapter);
	int UndecoratedSmoothedPWDB = 0;
	
	if(padapter->bLinkInfoDump){

		DBG_871X("\n============["ADPT_FMT"] linked status check ===================\n",ADPT_ARG(padapter));	

		if((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
		{
			rtw_hal_get_def_var(padapter, HAL_DEF_UNDERCORATEDSMOOTHEDPWDB, &UndecoratedSmoothedPWDB);	
		
			DBG_871X("AP[" MAC_FMT "] - UndecoratedSmoothedPWDB:%d\n",
				MAC_ARG(padapter->mlmepriv.cur_network.network.MacAddress),UndecoratedSmoothedPWDB);
		}
		else if((pmlmeinfo->state&0x03) == _HW_STATE_AP_)
		{
			_irqL irqL;
			_list	*phead, *plist;
	
			struct sta_info *psta=NULL;	
			struct sta_priv *pstapriv = &padapter->stapriv;
			
			_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);	
			phead = &pstapriv->asoc_list;
			plist = get_next(phead);
			while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
			{
				psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
				plist = get_next(plist);			
				
				DBG_871X("STA[" MAC_FMT "]:UndecoratedSmoothedPWDB:%d\n", 
					MAC_ARG(psta->hwaddr),psta->rssi_stat.UndecoratedSmoothedPWDB);
			}
			_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);		
			
		}
		
		/*============  tx info ============	*/
		rtw_hal_get_def_var(padapter, HW_DEF_RA_INFO_DUMP, RTW_DBGDUMP);

		rtw_hal_set_odm_var(padapter, HAL_ODM_RX_INFO_DUMP, RTW_DBGDUMP, _FALSE);

	}


}
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
			if (sta_rx_data_qos_pkts(psta, i) == sta_last_rx_data_qos_pkts(psta, i)) {			
				if (_TRUE == rtw_inc_and_chk_continual_no_rx_packet(psta, i)) {
					if (psta->recvreorder_ctrl[i].enable) {						
						/* send a DELBA frame to the peer STA with the Reason Code field set to TIMEOUT */		
						if (!from_timer)
							ret = issue_del_ba_ex(padapter, psta->hwaddr, i, 39, 0, 3, 1);
						else
							issue_del_ba(padapter,  psta->hwaddr, i, 39, 0);							
							psta->recvreorder_ctrl[i].enable = _FALSE;
						if (ret != _FAIL)
							psta->recvreorder_ctrl[i].ampdu_size = RX_AMPDU_SIZE_INVALID;
							rtw_reset_continual_no_rx_packet(psta, i);
						}				
					}
				}
			else{   
				/* The inactivity timer is reset when MPDUs to the TID is received. */
				rtw_reset_continual_no_rx_packet(psta, i);	
			}
		}
	}
}

u8 chk_ap_is_alive(_adapter *padapter, struct sta_info *psta)
{
	u8 ret = _FALSE;
	int i = 0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

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

	if((sta_rx_data_pkts(psta) == sta_last_rx_data_pkts(psta))
		&& sta_rx_beacon_pkts(psta) == sta_last_rx_beacon_pkts(psta)
		&& sta_rx_probersp_pkts(psta) == sta_last_rx_probersp_pkts(psta)
	)
	{
		ret = _FALSE;
	}
	else
	{
		ret = _TRUE;
	}

	sta_update_last_rx_pkts(psta);

	/*
		record last rx data packets for every tid.
	*/
	for (i = 0; i < TID_NUM; i++)	
		psta->sta_stats.last_rx_data_qos_pkts[i] = psta->sta_stats.rx_data_qos_pkts[i];

	return ret;
}

u8 chk_adhoc_peer_is_alive(struct sta_info *psta)
{
	u8 ret = _TRUE;

	#ifdef DBG_EXPIRATION_CHK
	DBG_871X("sta:"MAC_FMT", rssi:%d, rx:"STA_PKTS_FMT", beacon:%llu, probersp_to_self:%llu"
		/*", probersp_bm:%llu, probersp_uo:%llu, probereq:%llu, BI:%u"*/
		", expire_to:%u\n"
		, MAC_ARG(psta->hwaddr)
		, psta->rssi_stat.UndecoratedSmoothedPWDB
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
	int i, num_teardown=0, num_checkalive=0;
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

	if((padapter->tdlsinfo.link_established == _TRUE)){
		_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);
		for(i=0; i< NUM_STA; i++)
		{
			phead = &(pstapriv->sta_hash[i]);
			plist = get_next(phead);
			
			while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
			{
				psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);
				plist = get_next(plist);

				if(psta->tdls_sta_state & TDLS_LINKED_STATE)
				{
					psta->alive_count++;
					if(psta->alive_count >= ALIVE_MIN)
					{
						if (chk_tdls_peer_sta_is_alive(padapter, psta) == _FALSE) {
							if (psta->alive_count < ALIVE_MAX) {
								_rtw_memcpy(checkalive[num_checkalive].addr, psta->hwaddr, ETH_ALEN);
								checkalive[num_checkalive].psta = psta;
								num_checkalive++;
							}
							else
							{
								_rtw_memcpy(teardown[num_teardown].addr, psta->hwaddr, ETH_ALEN);
								teardown[num_teardown].psta = psta;
								num_teardown++;
							}
						}
						else
						{
							psta->alive_count = 0;
						}
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

		if(num_teardown > 0)
		{
			for(i=0; i< num_teardown; i++)
			{
				DBG_871X("[%s %d] Send teardown to "MAC_FMT" \n", __FUNCTION__, __LINE__, MAC_ARG(teardown[i].addr));
				txmgmt.status_code = _RSON_TDLS_TEAR_TOOFAR_;
				_rtw_memcpy(txmgmt.peer, teardown[i].addr, ETH_ALEN);
				issue_tdls_teardown(padapter, &txmgmt, _FALSE);
			}
		}
	}

}
#endif //CONFIG_TDLS

//from_timer == 1 means driver is in LPS
void linked_status_chk(_adapter *padapter, u8 from_timer)
{
	u32	i;
	struct sta_info		*psta;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv		*pstapriv = &padapter->stapriv;
#ifdef CONFIG_ARP_KEEP_ALIVE
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
#endif
	

	if (is_client_associated_to_ap(padapter))
	{
		//linked infrastructure client mode

		int tx_chk = _SUCCESS, rx_chk = _SUCCESS;
		int rx_chk_limit;
		int link_count_limit;

		#if defined(DBG_ROAMING_TEST)
		rx_chk_limit = 1;
		#elif defined(CONFIG_ACTIVE_KEEP_ALIVE_CHECK) && !defined(CONFIG_LPS_LCLK_WD_TIMER)
		rx_chk_limit = 4;
		#else
		rx_chk_limit = 8;
		#endif
#ifdef CONFIG_ARP_KEEP_ALIVE
		if (!from_timer && pmlmepriv->bGetGateway == 1) {
			DBG_871X("do rtw_gw_addr_query()");
			if (rtw_gw_addr_query(padapter) == 0) {
				pmlmepriv->bGetGateway = 0;
			} else {
				_rtw_memset(pmlmepriv->gw_ip, 0, 4);
				_rtw_memset(pmlmepriv->gw_mac_addr, 0, 6);
			}
		}
#endif
#ifdef CONFIG_P2P
		if (!rtw_p2p_chk_state(&padapter->wdinfo, P2P_STATE_NONE))
		{
			if(!from_timer)
				link_count_limit = 3; // 8 sec
			else
				link_count_limit = 15; // 32 sec
		}
		else
#endif // CONFIG_P2P
		{
			if(!from_timer)
				link_count_limit = 7; // 16 sec
			else
				link_count_limit = 29; // 60 sec
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

		if ((psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress)) != NULL)
		{
			bool is_p2p_enable = _FALSE;
			#ifdef CONFIG_P2P
			is_p2p_enable = !rtw_p2p_chk_state(&padapter->wdinfo, P2P_STATE_NONE);
			#endif

			/*issue delba when ap does not tx data packet that is Broadcom ap */
			rtw_delba_check(padapter, psta, from_timer);

			if (chk_ap_is_alive(padapter, psta) == _FALSE)
				rx_chk = _FAIL;

			if (pxmitpriv->last_tx_pkts == pxmitpriv->tx_pkts)
				tx_chk = _FAIL;

			#if defined(CONFIG_ACTIVE_KEEP_ALIVE_CHECK) && !defined(CONFIG_LPS_LCLK_WD_TIMER)
			if (pmlmeext->active_keep_alive_check && (rx_chk == _FAIL || tx_chk == _FAIL)) {
				u8 backup_oper_channel=0;

				/* switch to correct channel of current network  before issue keep-alive frames */
				if (rtw_get_oper_ch(padapter) != pmlmeext->cur_channel) {
					backup_oper_channel = rtw_get_oper_ch(padapter);
					SelectChannel(padapter, pmlmeext->cur_channel);
				}

				if (rx_chk != _SUCCESS)
					issue_probereq_ex(padapter, &pmlmeinfo->network.Ssid, psta->hwaddr, 0, 0, 3, 1);

				if ((tx_chk != _SUCCESS && pmlmeinfo->link_count++ == link_count_limit) || rx_chk != _SUCCESS) {
					tx_chk = issue_nulldata(padapter, psta->hwaddr, 0, 3, 1);
					/* if tx acked and p2p disabled, set rx_chk _SUCCESS to reset retry count */
					if (tx_chk == _SUCCESS && !is_p2p_enable)
						rx_chk = _SUCCESS;
				}

				/* back to the original operation channel */
				if(backup_oper_channel>0)
					SelectChannel(padapter, backup_oper_channel);

			}
			else
			#endif /* CONFIG_ACTIVE_KEEP_ALIVE_CHECK */
			{
				if (rx_chk != _SUCCESS) {
					if (pmlmeext->retry == 0) {
						#ifdef DBG_EXPIRATION_CHK
						DBG_871X("issue_probereq to trigger probersp, retry=%d\n", pmlmeext->retry);
						#endif
						issue_probereq_ex(padapter, &pmlmeinfo->network.Ssid, pmlmeinfo->network.MacAddress, 0, 0, 0, 0);
						issue_probereq_ex(padapter, &pmlmeinfo->network.Ssid, pmlmeinfo->network.MacAddress, 0, 0, 0, 0);
						issue_probereq_ex(padapter, &pmlmeinfo->network.Ssid, pmlmeinfo->network.MacAddress, 0, 0, 0, 0);
					}
				}

				if (tx_chk != _SUCCESS && pmlmeinfo->link_count++ == link_count_limit) {
					#ifdef DBG_EXPIRATION_CHK
					DBG_871X("%s issue_nulldata(%d)\n", __FUNCTION__, from_timer?1:0);
					#endif
					tx_chk = issue_nulldata_in_interrupt(padapter, NULL, from_timer?1:0);
				}
			}

			if (rx_chk == _FAIL) {
				pmlmeext->retry++;
				if (pmlmeext->retry > rx_chk_limit) {
					DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" disconnect or roaming\n",
						FUNC_ADPT_ARG(padapter));
					receive_disconnect(padapter, pmlmeinfo->network.MacAddress
						, WLAN_REASON_EXPIRATION_CHK, _FALSE);
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

		} //end of if ((psta = rtw_get_stainfo(pstapriv, passoc_res->network.MacAddress)) != NULL)

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

				if (is_broadcast_mac_addr(psta->hwaddr))
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
			DBG_871X(FUNC_ADPT_FMT" ibss expire "MAC_FMT"\n"
				, FUNC_ADPT_ARG(padapter), MAC_ARG(psta->hwaddr));
			report_del_sta_event(padapter, psta->hwaddr, WLAN_REASON_EXPIRATION_CHK, from_timer ? _TRUE : _FALSE, _FALSE);
		}
	}

}

void survey_timer_hdl(_adapter *padapter)
{
	struct cmd_obj *cmd;
	struct sitesurvey_parm *psurveyPara;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
#endif

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

		init_h2fwcmd_w_parm_no_rsp(cmd, psurveyPara, GEN_CMD_CODE(_SiteSurvey));
		rtw_enqueue_cmd(pcmdpriv, cmd);
	}

exit:
	return;
}

void link_timer_hdl(_adapter *padapter)
{
	//static unsigned int		rx_pkt = 0;
	//static u64				tx_cnt = 0;
	//struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	//struct sta_priv		*pstapriv = &padapter->stapriv;


	if (pmlmeinfo->state & WIFI_FW_AUTH_NULL)
	{
		DBG_871X("link_timer_hdl:no beacon while connecting\n");
		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		report_join_res(padapter, -3);
	}
	else if (pmlmeinfo->state & WIFI_FW_AUTH_STATE)
	{
		//re-auth timer
		if (++pmlmeinfo->reauth_count > REAUTH_LIMIT)
		{
			//if (pmlmeinfo->auth_algo != dot11AuthAlgrthm_Auto)
			//{
				pmlmeinfo->state = 0;
				report_join_res(padapter, -1);
				return;
			//}
			//else
			//{
			//	pmlmeinfo->auth_algo = dot11AuthAlgrthm_Shared;
			//	pmlmeinfo->reauth_count = 0;
			//}
		}
		
		DBG_871X("link_timer_hdl: auth timeout and try again\n");
		pmlmeinfo->auth_seq = 1;
		issue_auth(padapter, NULL, 0);
		set_link_timer(pmlmeext, REAUTH_TO);
	}
	else if (pmlmeinfo->state & WIFI_FW_ASSOC_STATE)
	{
		//re-assoc timer
		if (++pmlmeinfo->reassoc_count > REASSOC_LIMIT)
		{
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

void addba_timer_hdl(struct sta_info *psta)
{
#ifdef CONFIG_80211N_HT
	struct ht_priv	*phtpriv;

	if(!psta)
		return;
	
	phtpriv = &psta->htpriv;

	if((phtpriv->ht_option==_TRUE) && (phtpriv->ampdu_enable==_TRUE)) 
	{
		if(phtpriv->candidate_tid_bitmap)
			phtpriv->candidate_tid_bitmap=0x0;
		
	}
#endif //CONFIG_80211N_HT
}

#ifdef CONFIG_IEEE80211W
void report_sta_timeout_event(_adapter *padapter, u8 *MacAddr, unsigned short reason)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct sta_info *psta;
	int	mac_id;
	struct stadel_event			*pdel_sta_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	
	pcmd_obj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd_obj == NULL)
		return;

	cmdsz = (sizeof(struct stadel_event) + sizeof(struct C2HEvent_Header));
	pevtcmd = (u8 *)rtw_zmalloc(cmdsz);
	if (pevtcmd == NULL) {
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		return;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header *)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct stadel_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_TimeoutSTA);
	pc2h_evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	pdel_sta_evt = (struct stadel_event *)(pevtcmd + sizeof(struct C2HEvent_Header));
	_rtw_memcpy((unsigned char *)(&(pdel_sta_evt->macaddr)), MacAddr, ETH_ALEN);
	_rtw_memcpy((unsigned char *)(pdel_sta_evt->rsvd), (unsigned char *)(&reason), 2);


	psta = rtw_get_stainfo(&padapter->stapriv, MacAddr);
	if (psta)
		mac_id = (int)psta->mac_id;	
	else
		mac_id = (-1);

	pdel_sta_evt->mac_id = mac_id;

	DBG_871X("report_del_sta_event: delete STA, mac_id=%d\n", mac_id);

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;
}

void clnt_sa_query_timeout(_adapter *padapter)
{

	rtw_disassoc_cmd(padapter, 0, _TRUE);
	rtw_indicate_disconnect(padapter, 0,  _FALSE);
	rtw_free_assoc_resources(padapter, 1);	

	DBG_871X("SA query timeout client disconnect\n");
}

void sa_query_timer_hdl(struct sta_info *psta)
{
	_adapter *padapter = psta->padapter;
	_irqL irqL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE &&
					check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
		clnt_sa_query_timeout(padapter);
	else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
		report_sta_timeout_event(padapter, psta->hwaddr, WLAN_REASON_PREV_AUTH_NOT_VALID);
}

#endif //CONFIG_IEEE80211W

u8 NULL_hdl(_adapter *padapter, u8 *pbuf)
{
	return H2C_SUCCESS;
}

#ifdef CONFIG_AUTO_AP_MODE
void rtw_start_auto_ap(_adapter *adapter)
{
	DBG_871X("%s\n", __FUNCTION__);

	rtw_set_802_11_infrastructure_mode(adapter, Ndis802_11APMode);

	rtw_setopmode_cmd(adapter, Ndis802_11APMode,_TRUE);
}

static int rtw_auto_ap_start_beacon(_adapter *adapter)
{
	int ret=0;
	u8 *pbuf = NULL;
	uint len;
	u8	supportRate[16];
	int 	sz = 0, rateLen;
	u8 *	ie;
	u8	wireless_mode, oper_channel;
	u8 ssid[3] = {0}; //hidden ssid
	u32 ssid_len = sizeof(ssid);
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);


	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) != _TRUE)
		return -EINVAL;


	len = 128;
	pbuf = rtw_zmalloc(len);
	if(!pbuf)
		return -ENOMEM;


	//generate beacon
	ie = pbuf;

	//timestamp will be inserted by hardware
	sz += 8;
	ie += sz;

	//beacon interval : 2bytes
	*(u16*)ie = cpu_to_le16((u16)100);//BCN_INTERVAL=100;
	sz += 2;
	ie += 2;

	//capability info
	*(u16*)ie = 0;
	*(u16*)ie |= cpu_to_le16(cap_ESS);
	*(u16*)ie |= cpu_to_le16(cap_ShortPremble);
	//*(u16*)ie |= cpu_to_le16(cap_Privacy);
	sz += 2;
	ie += 2;

	//SSID
	ie = rtw_set_ie(ie, _SSID_IE_, ssid_len, ssid, &sz);

	//supported rates
	wireless_mode = WIRELESS_11BG_24N;
	rtw_set_supported_rate(supportRate, wireless_mode) ;
	rateLen = rtw_get_rateset_len(supportRate);
	if (rateLen > 8)
	{
		ie = rtw_set_ie(ie, _SUPPORTEDRATES_IE_, 8, supportRate, &sz);
	}
	else
	{
		ie = rtw_set_ie(ie, _SUPPORTEDRATES_IE_, rateLen, supportRate, &sz);
	}


	//DS parameter set
	if(check_buddy_fwstate(adapter, _FW_LINKED) &&
		check_buddy_fwstate(adapter, WIFI_STATION_STATE))
	{
		PADAPTER pbuddy_adapter = adapter->pbuddy_adapter;
		struct mlme_ext_priv *pbuddy_mlmeext  = &pbuddy_adapter->mlmeextpriv;

		oper_channel = pbuddy_mlmeext->cur_channel;
	}
	else
	{
		oper_channel = adapter_to_dvobj(adapter)->oper_channel;
	}
	ie = rtw_set_ie(ie, _DSSET_IE_, 1, &oper_channel, &sz);

	//ext supported rates
	if (rateLen > 8)
	{
		ie = rtw_set_ie(ie, _EXT_SUPPORTEDRATES_IE_, (rateLen - 8), (supportRate + 8), &sz);
	}

	DBG_871X("%s, start auto ap beacon sz=%d\n", __FUNCTION__, sz);

	//lunch ap mode & start to issue beacon
	if(rtw_check_beacon_data(adapter, pbuf,  sz) == _SUCCESS)
	{

	}
	else
	{
		ret = -EINVAL;
	}


	rtw_mfree(pbuf, len);

	return ret;

}
#endif//CONFIG_AUTO_AP_MODE

u8 setopmode_hdl(_adapter *padapter, u8 *pbuf)
{
	u8	type;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct setopmode_parm *psetop = (struct setopmode_parm *)pbuf;

	if(psetop->mode == Ndis802_11APMode)
	{
		pmlmeinfo->state = WIFI_FW_AP_STATE;
		type = _HW_STATE_AP_;
#ifdef CONFIG_NATIVEAP_MLME
		//start_ap_mode(padapter);
#endif
	}
	else if(psetop->mode == Ndis802_11Infrastructure)
	{
		pmlmeinfo->state &= ~(BIT(0)|BIT(1));// clear state
		pmlmeinfo->state |= WIFI_FW_STATION_STATE;//set to 	STATION_STATE
		type = _HW_STATE_STATION_;
	}
	else if(psetop->mode == Ndis802_11IBSS)
	{
		type = _HW_STATE_ADHOC_;
	} else if (psetop->mode == Ndis802_11Monitor) {
		type = _HW_STATE_MONITOR_;
	}
	else
	{
		type = _HW_STATE_NOLINK_;
	}

	rtw_hal_set_hwreg(padapter, HW_VAR_SET_OPMODE, (u8 *)(&type));
	//Set_NETYPE0_MSR(padapter, type);


#ifdef CONFIG_AUTO_AP_MODE
	if(psetop->mode == Ndis802_11APMode)
		rtw_auto_ap_start_beacon(padapter);
#endif

	if (rtw_port_switch_chk(padapter) == _TRUE)
	{
		rtw_hal_set_hwreg(padapter, HW_VAR_PORT_SWITCH, NULL);

		if(psetop->mode == Ndis802_11APMode)
			adapter_to_pwrctl(padapter)->fw_psmode_iface_id = 0xff; //ap mode won't dowload rsvd pages
		else if (psetop->mode == Ndis802_11Infrastructure) {
			#ifdef CONFIG_LPS
			_adapter *port0_iface = dvobj_get_port0_adapter(adapter_to_dvobj(padapter));
			if (port0_iface)
				rtw_lps_ctrl_wk_cmd(port0_iface, LPS_CTRL_CONNECT, 0);
			#endif	
		}
	}	

#ifdef CONFIG_BT_COEXIST
	if (psetop->mode == Ndis802_11APMode)
	{
		// Do this after port switch to
		// prevent from downloading rsvd page to wrong port
		rtw_btcoex_MediaStatusNotify(padapter, 1); //connect 
	}
#endif // CONFIG_BT_COEXIST

	return H2C_SUCCESS;
	
}

u8 createbss_hdl(_adapter *padapter, u8 *pbuf)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX	*pnetwork = (WLAN_BSSID_EX*)(&(pmlmeinfo->network));
	WLAN_BSSID_EX	*pdev_network = &padapter->registrypriv.dev_network;
	struct createbss_parm *parm = (struct createbss_parm *)pbuf;
	u8 ret = H2C_SUCCESS;
	//u8	initialgain;
	
#ifdef CONFIG_AP_MODE
	if (pmlmeinfo->state == WIFI_FW_AP_STATE) {
		start_bss_network(padapter, parm);
		goto exit;
	}
#endif

	/* below is for ad-hoc master */
	if (parm->adhoc) {
		rtw_warn_on(pdev_network->InfrastructureMode != Ndis802_11IBSS);
		rtw_joinbss_reset(padapter);
	
		pmlmeext->cur_bwmode = CHANNEL_WIDTH_20;
		pmlmeext->cur_ch_offset= HAL_PRIME_CHNL_OFFSET_DONT_CARE;	
		pmlmeinfo->ERP_enable = 0;
		pmlmeinfo->WMM_enable = 0;
		pmlmeinfo->HT_enable = 0;
		pmlmeinfo->HT_caps_enable = 0;
		pmlmeinfo->HT_info_enable = 0;
		pmlmeinfo->agg_enable_bitmap = 0;
		pmlmeinfo->candidate_tid_bitmap = 0;

		//config the initial gain under linking, need to write the BB registers
		//initialgain = 0x1E;
		/*rtw_hal_set_odm_var(padapter, HAL_ODM_INITIAL_GAIN, &initialgain, _FALSE);*/

		//disable dynamic functions, such as high power, DIG
		rtw_phydm_ability_backup(padapter);
		rtw_phydm_func_disable_all(padapter);
		
		//cancel link timer 
		_cancel_timer_ex(&pmlmeext->link_timer);

		//clear CAM
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
}

u8 join_cmd_hdl(_adapter *padapter, u8 *pbuf)
{
	u8	join_type;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*pnetwork = (WLAN_BSSID_EX*)(&(pmlmeinfo->network));
#ifdef CONFIG_ANTENNA_DIVERSITY
	struct joinbss_parm	*pparm = (struct joinbss_parm *)pbuf;
#endif //CONFIG_ANTENNA_DIVERSITY
	u32 i;
	//u8	initialgain;
	//u32	acparm;
	u8 u_ch, u_bw, u_offset;
	u8 doiqk = _FALSE;

	//check already connecting to AP or not
	if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
	{
		if (pmlmeinfo->state & WIFI_FW_STATION_STATE)
		{
			issue_deauth_ex(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING, 1, 100);
		}
		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		
		//clear CAM
		flush_all_cam_entry(padapter);		
		
		_cancel_timer_ex(&pmlmeext->link_timer);
		
		//set MSR to nolink -> infra. mode		
		//Set_MSR(padapter, _HW_STATE_NOLINK_);
		Set_MSR(padapter, _HW_STATE_STATION_);	
		

		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_DISCONNECT, 0);
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
	//pmlmeinfo->assoc_AP_vendor = HT_IOT_PEER_MAX;
	pmlmeinfo->VHT_enable = 0;

	_rtw_memcpy(pnetwork, pbuf, FIELD_OFFSET(WLAN_BSSID_EX, IELength)); 
	pnetwork->IELength = ((WLAN_BSSID_EX *)pbuf)->IELength;
	
	if(pnetwork->IELength>MAX_IE_SZ)//Check pbuf->IELength
		return H2C_PARAMETERS_ERROR;	
	
	if (pnetwork->IELength < 2) {
		report_join_res(padapter, (-4));
		return H2C_SUCCESS;
	}
	_rtw_memcpy(pnetwork->IEs, ((WLAN_BSSID_EX *)pbuf)->IEs, pnetwork->IELength); 

	pmlmeinfo->bcn_interval = get_beacon_interval(pnetwork);

	//Check AP vendor to move rtw_joinbss_cmd()
	//pmlmeinfo->assoc_AP_vendor = check_assoc_AP(pnetwork->IEs, pnetwork->IELength);

	//sizeof(NDIS_802_11_FIXED_IEs)	
	for (i = _FIXED_IE_LENGTH_ ; i < pnetwork->IELength - 2 ;) {
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pnetwork->IEs + i);

		switch (pIE->ElementID)
		{
			case _VENDOR_SPECIFIC_IE_://Get WMM IE.
				if ( _rtw_memcmp(pIE->data, WMM_OUI, 4) )
				{
					WMM_param_handler(padapter, pIE);
				}
				break;

#ifdef CONFIG_80211N_HT
			case _HT_CAPABILITY_IE_:	//Get HT Cap IE.
				pmlmeinfo->HT_caps_enable = 1;
				break;

			case _HT_EXTRA_INFO_IE_:	//Get HT Info IE.
				pmlmeinfo->HT_info_enable = 1;
				break;
#endif /* CONFIG_80211N_HT */

#ifdef CONFIG_80211AC_VHT
			case EID_VHTCapability://Get VHT Cap IE.
				pmlmeinfo->VHT_enable = 1;
				break;

			case EID_VHTOperation://Get VHT Operation IE.
				break;
#endif /* CONFIG_80211AC_VHT */
			default:
				break;
		}

		i += (pIE->Length + 2);
	}

	rtw_bss_get_chbw(pnetwork
		, &pmlmeext->cur_channel, &pmlmeext->cur_bwmode, &pmlmeext->cur_ch_offset);

	rtw_adjust_chbw(padapter, pmlmeext->cur_channel, &pmlmeext->cur_bwmode, &pmlmeext->cur_ch_offset);

#if 0
	if (padapter->registrypriv.wifi_spec) {
		// for WiFi test, follow WMM test plan spec
		acparm = 0x002F431C; // VO
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acparm));
		acparm = 0x005E541C; // VI
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acparm));
		acparm = 0x0000A525; // BE
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acparm));
		acparm = 0x0000A549; // BK
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acparm));
	
		// for WiFi test, mixed mode with intel STA under bg mode throughput issue
		if (padapter->mlmepriv.htpriv.ht_option == _FALSE){
			acparm = 0x00004320;
			rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acparm));
		}
	}
	else {
		acparm = 0x002F3217; // VO
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acparm));
		acparm = 0x005E4317; // VI
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acparm));
		acparm = 0x00105320; // BE
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acparm));
		acparm = 0x0000A444; // BK
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acparm));
	}
#endif

	/* check channel, bandwidth, offset and switch */
	if (rtw_chk_start_clnt_join(padapter, &u_ch, &u_bw, &u_offset) == _FAIL) {
		report_join_res(padapter, (-4));
		return H2C_SUCCESS;
	}

	//disable dynamic functions, such as high power, DIG
	/*rtw_phydm_func_disable_all(padapter);*/

	//config the initial gain under linking, need to write the BB registers
	//initialgain = 0x1E;
	/*rtw_hal_set_odm_var(padapter, HAL_ODM_INITIAL_GAIN, &initialgain, _FALSE);*/

	rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, pmlmeinfo->network.MacAddress);
	join_type = 0;
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));
	doiqk = _TRUE;
	rtw_hal_set_hwreg(padapter , HW_VAR_DO_IQK , &doiqk);

	set_channel_bwmode(padapter, u_ch, u_offset, u_bw);

	doiqk = _FALSE;
	rtw_hal_set_hwreg(padapter , HW_VAR_DO_IQK , &doiqk);

	//cancel link timer 
	_cancel_timer_ex(&pmlmeext->link_timer);
	
	start_clnt_join(padapter);
	
	return H2C_SUCCESS;
	
}

u8 disconnect_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct disconnect_parm *param = (struct disconnect_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*pnetwork = (WLAN_BSSID_EX*)(&(pmlmeinfo->network));
	u8 val8;

	if (is_client_associated_to_ap(padapter))
	{
#ifdef CONFIG_DFS
		if(padapter->mlmepriv.handle_dfs == _FALSE)
#endif //CONFIG_DFS
#ifdef CONFIG_PLATFORM_ROCKCHIPS
			//To avoid connecting to AP fail during resume process, change retry count from 5 to 1
			issue_deauth_ex(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING, 1, 100);
#else
			issue_deauth_ex(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING, param->deauth_timeout_ms/100, 100);
#endif //CONFIG_PLATFORM_ROCKCHIPS
	}

#ifdef CONFIG_DFS
	if( padapter->mlmepriv.handle_dfs == _TRUE )
		padapter->mlmepriv.handle_dfs = _FALSE;
#endif //CONFIG_DFS

	if(((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
	{
		//Stop BCN
		val8 = 0;
		rtw_hal_set_hwreg(padapter, HW_VAR_BCN_FUNC, (u8 *)(&val8));
	}

	rtw_mlmeext_disconnect(padapter);

	rtw_free_uc_swdec_pending_queue(padapter);

	return 	H2C_SUCCESS;
}

static const char * const _scan_state_str[] = {
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
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
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
			DBG_871X("%s idx:%d, cnt:%u\n", __FUNCTION__
				, ss->channel_idx
				, pwdinfo->find_phase_state_exchange_cnt
			);
		} else
		#endif
		{
			ss->channel_idx = ss->ch_num;
			DBG_871X("%s idx:%d\n", __FUNCTION__
				, ss->channel_idx
			);
		}
		pmlmeext->scan_abort = _FALSE;
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

#define SCAN_SPARSE_CH_NUM_INVALID 255

	static u8 token = 255;
	u32 interval;
	bool busy_traffic = _FALSE;
	bool miracast_enabled = _FALSE;
	bool bg_scan = _FALSE;
	u8 max_allow_ch = SCAN_SPARSE_CH_NUM_INVALID;
	u8 scan_division_num;
	u8 ret_num = ch_num;
	struct registry_priv *regsty = dvobj_to_regsty(adapter_to_dvobj(adapter));
	struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;

	if (regsty->wifi_spec)
		goto exit;

	/* assume ch_num > 6 is normal scan */
	if (ch_num <= 6)
		goto exit;

	if (mlmeext->last_scan_time == 0)
		mlmeext->last_scan_time = rtw_get_current_time();

	interval = rtw_get_passing_time_ms(mlmeext->last_scan_time);

	if (adapter->mlmepriv.LinkDetectInfo.bBusyTraffic == _TRUE
		#ifdef CONFIG_CONCURRENT_MODE
		|| (adapter->pbuddy_adapter && adapter->pbuddy_adapter->mlmepriv.LinkDetectInfo.bBusyTraffic == _TRUE)
		#endif
	)
			busy_traffic = _TRUE;

	if (is_miracast_enabled(adapter)
		#ifdef CONFIG_CONCURRENT_MODE
		|| (adapter->pbuddy_adapter && is_miracast_enabled(adapter->pbuddy_adapter))
		#endif
	)
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


	if (max_allow_ch != SCAN_SPARSE_CH_NUM_INVALID) {
		int i;
		int k = 0;

		scan_division_num = (ch_num / max_allow_ch) + ((ch_num % max_allow_ch)?1:0);
		token = (token + 1) % scan_division_num;
		
		if (0)
			DBG_871X("scan_division_num:%u, token:%u\n", scan_division_num, token);
		
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

exit:
	return ret_num;
}

static int rtw_scan_ch_decision(_adapter *padapter, struct rtw_ieee80211_channel *out,
	u32 out_num, struct rtw_ieee80211_channel *in, u32 in_num)
{
	int i, j;
	int scan_ch_num = 0;
	int set_idx;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	/* clear first */
	_rtw_memset(out, 0, sizeof(struct rtw_ieee80211_channel)*out_num);

	/* acquire channels from in */
	j = 0;
	for (i=0;i<in_num;i++) {

		if (0)
			DBG_871X(FUNC_ADPT_FMT" "CHAN_FMT"\n", FUNC_ADPT_ARG(padapter), CHAN_ARG(&in[i]));

		if(in[i].hw_value && !(in[i].flags & RTW_IEEE80211_CHAN_DISABLED)
			&& (set_idx=rtw_ch_set_search_ch(pmlmeext->channel_set, in[i].hw_value)) >=0
			&& rtw_mlme_band_check(padapter, in[i].hw_value) == _TRUE
		)
		{
			if (j >= out_num) {
				DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" out_num:%u not enough\n",
					FUNC_ADPT_ARG(padapter), out_num);
				break;
			}

			_rtw_memcpy(&out[j], &in[i], sizeof(struct rtw_ieee80211_channel));
			
			if(pmlmeext->channel_set[set_idx].ScanType == SCAN_PASSIVE)
				out[j].flags |= RTW_IEEE80211_CHAN_PASSIVE_SCAN;
				
			j++;
		}
		if(j>=out_num)
			break;
	}
	
	/* if out is empty, use channel_set as default */
	if(j == 0) {
		for (i=0;i<pmlmeext->max_chan_nums;i++) {

			if (0)
				DBG_871X(FUNC_ADPT_FMT" ch:%u\n", FUNC_ADPT_ARG(padapter), pmlmeext->channel_set[i].ChannelNum);

			if (rtw_mlme_band_check(padapter, pmlmeext->channel_set[i].ChannelNum) == _TRUE) {

				if (j >= out_num) {
					DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" out_num:%u not enough\n",
						FUNC_ADPT_ARG(padapter), out_num);
					break;
				}

				out[j].hw_value = pmlmeext->channel_set[i].ChannelNum;
			
				if(pmlmeext->channel_set[i].ScanType == SCAN_PASSIVE)
					out[j].flags |= RTW_IEEE80211_CHAN_PASSIVE_SCAN;

				j++;
			}
		}
	}

	/* scan_sparse */
	j = rtw_scan_sparse(padapter, out, j);

	return j;
}

static void sitesurvey_res_reset(_adapter *adapter, struct sitesurvey_parm *parm)
{
	struct ss_res *ss = &adapter->mlmeextpriv.sitesurvey_res;
	int i;

	ss->bss_cnt = 0;
	ss->channel_idx = 0;
	ss->igi_scan = 0;
	ss->igi_before_scan = 0;
#ifdef CONFIG_SCAN_BACKOP
	ss->scan_cnt = 0;
#endif
#if defined(CONFIG_ANTENNA_DIVERSITY) || defined(DBG_SCAN_SW_ANTDIV_BL)
	ss->is_sw_antdiv_bl_scan = 0;
#endif
	
	for (i = 0; i < RTW_SSID_SCAN_AMOUNT; i++) {
		if (parm->ssid[i].SsidLength) {
			_rtw_memcpy(ss->ssid[i].Ssid, parm->ssid[i].Ssid, IW_ESSID_MAX_SIZE);
			ss->ssid[i].SsidLength = parm->ssid[i].SsidLength;
		} else {
			ss->ssid[i].SsidLength = 0;
		}
	}

	ss->ch_num = rtw_scan_ch_decision(adapter
		, ss->ch, RTW_CHANNEL_SCAN_AMOUNT
		, parm->ch, parm->ch_num
	);

	ss->scan_mode = parm->scan_mode;
}

static u8 sitesurvey_pick_ch_behavior(_adapter *padapter, u8 *ch, RT_SCAN_TYPE *type)
{
	u8 next_state;
	u8 scan_ch = 0;
	RT_SCAN_TYPE scan_type = SCAN_PASSIVE;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct ss_res *ss = &pmlmeext->sitesurvey_res;

#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
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
		int ch_set_idx;

		scan_ch = pwdinfo->social_chan[ss->channel_idx];
		ch_set_idx = rtw_ch_set_search_ch(pmlmeext->channel_set, scan_ch);
		if (ch_set_idx >= 0)
			scan_type = pmlmeext->channel_set[ch_set_idx].ScanType;
		else
			scan_type = SCAN_ACTIVE;
	} else
#endif /* CONFIG_P2P */
	{
		struct rtw_ieee80211_channel *ch;

		if (ss->channel_idx < ss->ch_num) {
			ch = &ss->ch[ss->channel_idx];
			scan_ch = ch->hw_value;
			scan_type = (ch->flags & RTW_IEEE80211_CHAN_PASSIVE_SCAN) ? SCAN_PASSIVE : SCAN_ACTIVE;
		}
	}

	if (scan_ch != 0) {
		next_state = SCAN_PROCESS;
		#ifdef CONFIG_SCAN_BACKOP
		{
			u8 sta_num;
			u8 ld_sta_num;
			u8 ap_num;
			u8 ld_ap_num;
			u8 backop_flags = 0;

			rtw_dev_iface_status(padapter, &sta_num, &ld_sta_num, NULL, &ap_num, &ld_ap_num);

			if ((ld_sta_num > 0 && mlmeext_chk_scan_backop_flags_sta(pmlmeext, SS_BACKOP_EN))
					|| (sta_num > 0 && mlmeext_chk_scan_backop_flags_sta(pmlmeext, SS_BACKOP_EN_NL))
			) {
				backop_flags |= mlmeext_scan_backop_flags_sta(pmlmeext);
			}

			if ((ld_ap_num > 0 && mlmeext_chk_scan_backop_flags_ap(pmlmeext, SS_BACKOP_EN))
					|| (ap_num > 0 && mlmeext_chk_scan_backop_flags_ap(pmlmeext, SS_BACKOP_EN_NL))
			) {
				backop_flags |= mlmeext_scan_backop_flags_ap(pmlmeext);
			}

			if (backop_flags) {
				if (ss->scan_cnt < ss->scan_cnt_max) {
					ss->scan_cnt++;
				} else {
					mlmeext_assign_scan_backop_flags(pmlmeext, backop_flags);
					next_state = SCAN_BACKING_OP;
				}
			}
		}
		#endif /* CONFIG_SCAN_BACKOP */
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
				} else {
					fake_sw_antdiv_bl_state = 0;
				}
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
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
#endif

	if (survey_channel != 0) {
		set_channel_bwmode(padapter, survey_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);

		#ifdef CONFIG_AUTO_CHNL_SEL_NHM
		if (ACS_ENABLE == GET_ACS_STATE(padapter)) {
			ACS_OP acs_op = ACS_RESET;

			rtw_hal_set_odm_var(padapter, HAL_ODM_AUTO_CHNL_SEL, &acs_op, _TRUE);
			rtw_set_acs_channel(padapter, survey_channel);
			#ifdef DBG_AUTO_CHNL_SEL_NHM
			DBG_871X("[ACS-"ADPT_FMT"]-set ch:%u\n",
				ADPT_ARG(padapter), rtw_get_acs_channel(padapter));
			#endif
		}
		#endif

		if (ScanType == SCAN_ACTIVE) {
			#ifdef CONFIG_P2P
			if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_SCAN) || 
				rtw_p2p_chk_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH)
			) {
				issue_probereq_p2p(padapter, NULL);
				issue_probereq_p2p(padapter, NULL);
				issue_probereq_p2p(padapter, NULL);
			} else
			#endif /* CONFIG_P2P */
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
	
	if (rtw_get_ch_setting_union(padapter, &cur_channel, &cur_bwmode, &cur_ch_offset) != 0) {
		if (0)
			DBG_871X(FUNC_ADPT_FMT" back to linked/linking union - ch:%u, bw:%u, offset:%u\n",
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
					DBG_871X(FUNC_ADPT_FMT" back to "ADPT_FMT"'s listen ch - ch:%u, bw:%u, offset:%u\n",
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
				DBG_871X(FUNC_ADPT_FMT" back to ch:%u, bw:%u, offset:%u\n",
					FUNC_ADPT_ARG(padapter), cur_channel, cur_bwmode, cur_ch_offset);
		}
	}

	set_channel_bwmode(padapter, cur_channel, cur_ch_offset, cur_bwmode);
}

/**
 * sitesurvey_ps_annc - check and doing ps announcement for all the adapters of given @dvobj
 * @dvobj: the dvobj to check
 * @ps: power saving or not
 *
 * Returns: 0: no ps announcement is doing. 1: ps announcement is doing
 */
u8 sitesurvey_ps_annc(struct dvobj_priv *dvobj, bool ps)
{
	_adapter *adapter;
	int i;
	u8 ps_anc = 0;

	for (i = 0; i < dvobj->iface_nums; i++) {
		adapter = dvobj->padapters[i];
		if (!adapter)
			continue;

		if (ps) {
			if (is_client_associated_to_ap(adapter) == _TRUE) {
				/* TODO: TDLS peers */
				issue_nulldata(adapter, NULL, 1, 3, 500);
				ps_anc = 1;
			}
		} else {
			if (is_client_associated_to_ap(adapter) == _TRUE) {
				/* TODO: TDLS peers */
				issue_nulldata(adapter, NULL, 0, 3, 500);
				ps_anc = 1;
			}
		}
	}

	return ps_anc;
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
		if (adapter_wdev_data(adapter)->p2p_enabled == _TRUE && pwdinfo->driver_interface == DRIVER_CFG80211)
			igi = 0x30;
		else
		#endif /* CONFIG_IOCTL_CFG80211 */
		if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
			igi = 0x28;
		else
		#endif /* CONFIG_P2P */
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
		ODM_Write_DIG(GET_ODM(adapter), ss->igi_before_scan);
		break;
	case SCAN_LEAVE_OP:
		/* write IGI for scan when DIG is not enabled */
		ODM_Write_DIG(GET_ODM(adapter), ss->igi_scan);
		break;
#endif /* CONFIG_SCAN_BACKOP */
	default:
		rtw_warn_on(1);
		break;
	}
}


u8 sitesurvey_cmd_hdl(_adapter *padapter, u8 *pbuf)
{
	struct sitesurvey_parm	*pparm = (struct sitesurvey_parm *)pbuf;
	struct dvobj_priv *dvobj = padapter->dvobj;
	struct debug_priv *pdbgpriv = &dvobj->drv_dbg;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct ss_res *ss = &pmlmeext->sitesurvey_res;
	u8 val8;

#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif

#ifdef DBG_CHECK_FW_PS_STATE
	if (rtw_fw_ps_state(padapter) == _FAIL) {
		DBG_871X("scan without leave 32k\n");
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
		/*
		* prepare to leave operating channel
		*/

		/* apply rx ampdu setting */
		if (ss->rx_ampdu_accept != RX_AMPDU_ACCEPT_INVALID
			|| ss->rx_ampdu_size != RX_AMPDU_SIZE_INVALID
		) {
			rtw_rx_ampdu_apply(padapter);
		}

		/* clear HW TX queue before scan */
		rtw_hal_set_hwreg(padapter, HW_VAR_CHECK_TXBUF, 0);

		/* power save state announcement */
		if (sitesurvey_ps_annc(adapter_to_dvobj(padapter), 1)) {
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
		Set_MSR(padapter, _HW_STATE_NOLINK_);
		val8 = 1; /* under site survey */
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

		mlmeext_set_scan_state(pmlmeext, SCAN_PROCESS);
		goto operation_by_state;

	case SCAN_PROCESS:
	{
		u8 scan_ch;
		RT_SCAN_TYPE scan_type;
		u8 next_state;
		u32 scan_ms;

		#ifdef CONFIG_AUTO_CHNL_SEL_NHM
		if ((ACS_ENABLE == GET_ACS_STATE(padapter)) && (0 != rtw_get_acs_channel(padapter))) {
			ACS_OP acs_op = ACS_SELECT;

			rtw_hal_set_odm_var(padapter, HAL_ODM_AUTO_CHNL_SEL, &acs_op, _TRUE);
		}
		#endif

		next_state = sitesurvey_pick_ch_behavior(padapter, &scan_ch, &scan_type);
		if (next_state != SCAN_PROCESS) {
			#ifdef CONFIG_AUTO_CHNL_SEL_NHM
			if (ACS_ENABLE == GET_ACS_STATE(padapter)) {
				rtw_set_acs_channel(padapter, 0);
				#ifdef DBG_AUTO_CHNL_SEL_NHM
				DBG_871X("[ACS-"ADPT_FMT"]-set ch:%u\n", ADPT_ARG(padapter), rtw_get_acs_channel(padapter));
				#endif
			}
			#endif

			mlmeext_set_scan_state(pmlmeext, next_state);
			goto operation_by_state;
		}

		/* still SCAN_PROCESS state */
		if (0)
			#ifdef CONFIG_P2P
			DBG_871X(FUNC_ADPT_FMT" %s ch:%u (cnt:%u,idx:%d) at %dms, %c%c%c\n"
				, FUNC_ADPT_ARG(padapter)
				, mlmeext_scan_state_str(pmlmeext)
				, scan_ch
				, pwdinfo->find_phase_state_exchange_cnt, ss->channel_idx
				, rtw_get_passing_time_ms(padapter->mlmepriv.scan_start_time)
				, scan_type?'A':'P', ss->scan_mode?'A':'P'
				, ss->ssid[0].SsidLength?'S':' ' 
			);
			#else
			DBG_871X(FUNC_ADPT_FMT" %s ch:%u (idx:%d) at %dms, %c%c%c\n"
				, FUNC_ADPT_ARG(padapter)
				, mlmeext_scan_state_str(pmlmeext)
				, scan_ch
				, ss->channel_idx
				, rtw_get_passing_time_ms(padapter->mlmepriv.scan_start_time)
				, scan_type?'A':'P', ss->scan_mode?'A':'P'
				, ss->ssid[0].SsidLength?'S':' '
			);
			#endif /* CONFIG_P2P */

		#ifdef DBG_FIXED_CHAN
		if (pmlmeext->fixed_chan != 0xff)
			DBG_871X(FUNC_ADPT_FMT" fixed_chan:%u\n", pmlmeext->fixed_chan);
		#endif

		site_survey(padapter, scan_ch, scan_type);

		#if defined(CONFIG_ATMEL_RC_PATCH)
		if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
			scan_ms = 20;
		else
			scan_ms = 40;
		#else
		scan_ms = ss->scan_ch_ms;
		#endif

		#if defined(CONFIG_ANTENNA_DIVERSITY) || defined(DBG_SCAN_SW_ANTDIV_BL)
		if (ss->is_sw_antdiv_bl_scan)
			scan_ms = scan_ms/2;
		#endif

		#if defined(CONFIG_SIGNAL_DISPLAY_DBM) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
		{
			struct noise_info info;

			info.bPauseDIG = _FALSE;
			info.IGIValue = 0;
			info.max_time = scan_ms/2;
			info.chan = scan_ch;
			rtw_hal_set_odm_var(padapter, HAL_ODM_NOISE_MONITOR, &info, _FALSE); 
		}
		#endif

		set_survey_timer(pmlmeext, scan_ms);
		break;
	}

	#ifdef CONFIG_SCAN_BACKOP
	case SCAN_BACKING_OP:
	{
		u8 back_ch, back_bw, back_ch_offset;

		if (rtw_get_ch_setting_union(padapter, &back_ch, &back_bw, &back_ch_offset) == 0)
			rtw_warn_on(1);

		if (0)
			DBG_871X(FUNC_ADPT_FMT" %s ch:%u, bw:%u, offset:%u at %dms\n"
				, FUNC_ADPT_ARG(padapter)
				, mlmeext_scan_state_str(pmlmeext)
				, back_ch, back_bw, back_ch_offset
				, rtw_get_passing_time_ms(padapter->mlmepriv.scan_start_time)
			);

		set_channel_bwmode(padapter, back_ch, back_ch_offset, back_bw);

		Set_MSR(padapter, (pmlmeinfo->state & 0x3));		
		val8 = 0; /* survey done */
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

		if (mlmeext_chk_scan_backop_flags(pmlmeext, SS_BACKOP_PS_ANNC)) {
			sitesurvey_set_igi(padapter);
			sitesurvey_ps_annc(adapter_to_dvobj(padapter), 0);
		}

		mlmeext_set_scan_state(pmlmeext, SCAN_BACK_OP);
		ss->backop_time = rtw_get_current_time();

		if (mlmeext_chk_scan_backop_flags(pmlmeext, SS_BACKOP_TX_RESUME)) {
			int	i;

			/* resume TX */
			for (i = 0; i < dvobj->iface_nums; i++) {
				if (!dvobj->padapters[i])
					continue;

				rtw_os_xmit_schedule(dvobj->padapters[i]);
			}
		}

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

		if (mlmeext_chk_scan_backop_flags(pmlmeext, SS_BACKOP_PS_ANNC)
			&& sitesurvey_ps_annc(adapter_to_dvobj(padapter), 1)
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

		/* set MSR to no link state */
		Set_MSR(padapter, _HW_STATE_NOLINK_);
		val8 = 1; //under site survey
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
		#ifdef CONFIG_P2P
		if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_SCAN)
			|| rtw_p2p_chk_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH)
		) {
			#ifdef CONFIG_CONCURRENT_MODE
			if (pwdinfo->driver_interface == DRIVER_WEXT) {
				if (check_buddy_fwstate(padapter, _FW_LINKED))
					_set_timer(&pwdinfo->ap_p2p_switch_timer, 500);
			}
			rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
			#else
			rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
			#endif
		}
		rtw_p2p_findphase_ex_set(pwdinfo, P2P_FINDPHASE_EX_NONE);
		#endif /* CONFIG_P2P */

		/* switch channel */
		survey_done_set_ch_bw(padapter);

		/* config MSR */
		Set_MSR(padapter, (pmlmeinfo->state & 0x3));
		val8 = 0; /* survey done */
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

		/* turn on phy-dynamic functions */
		rtw_phydm_ability_restore(padapter);

		sitesurvey_set_igi(padapter);

		sitesurvey_ps_annc(adapter_to_dvobj(padapter), 0);

		/* apply rx ampdu setting */
		rtw_rx_ampdu_apply(padapter);

		mlmeext_set_scan_state(pmlmeext, SCAN_DISABLE);

		report_surveydone_event(padapter);

		issue_action_BSSCoexistPacket(padapter);
		issue_action_BSSCoexistPacket(padapter);
		issue_action_BSSCoexistPacket(padapter);
	}

	return H2C_SUCCESS;
}

u8 setauth_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct setauth_parm		*pparm = (struct setauth_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	if (pparm->mode < 4)
	{
		pmlmeinfo->auth_algo = pparm->mode;
	}

	return 	H2C_SUCCESS;
}

u8 setkey_hdl(_adapter *padapter, u8 *pbuf)
{
	u16	ctrl = 0;
	s16 cam_id = 0;
	struct setkey_parm		*pparm = (struct setkey_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	unsigned char null_addr[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	struct set_stakey_parm	sta_pparm;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	u8 *addr;
	bool used;

	//main tx key for wep.
	if(pparm->set_tx)
		pmlmeinfo->key_index = pparm->keyid;

	cam_id = rtw_camid_alloc(padapter, NULL, pparm->keyid, &used);

	if (cam_id < 0)
		goto enable_mc;

	if (cam_id > 3) /* not default key, searched by A2 */
		addr = get_bssid(&padapter->mlmepriv);
	else
		addr = null_addr;

	#ifdef DYNAMIC_CAMID_ALLOC
	/* cam entry searched is pairwise key */
	if (used == _TRUE && rtw_camid_is_gk(padapter, cam_id) == _FALSE) {
		s16 camid_clr;

		DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" group key with "MAC_FMT" id:%u the same key id as pairwise key\n"
			, FUNC_ADPT_ARG(padapter), MAC_ARG(addr), pparm->keyid);

		/* HW has problem to distinguish this group key with existing pairwise key, stop HW enc and dec for BMC */
		rtw_camctl_set_flags(padapter, SEC_STATUS_STA_PK_GK_CONFLICT_DIS_BMC_SEARCH);
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, NULL);

		/* clear group key */
		while ((camid_clr = rtw_camid_search(padapter, addr, -1, 1)) >= 0) {
			DBG_871X_LEVEL(_drv_always_, "clear group key for addr:"MAC_FMT", camid:%d\n", MAC_ARG(addr), camid_clr);
			clear_cam_entry(padapter, camid_clr);
			rtw_camid_free(padapter, camid_clr);
		}

		goto enable_mc;
	}
	#endif

	ctrl = BIT(15) | BIT6 | ((pparm->algorithm) << 2) | pparm->keyid;
	DBG_871X_LEVEL(_drv_always_, "set group key camid:%d, addr:"MAC_FMT", kid:%d, type:%s\n"
		, cam_id, MAC_ARG(addr), pparm->keyid, security_type_str(pparm->algorithm));
	write_cam(padapter, cam_id, ctrl, addr, pparm->key);

	#ifdef DYNAMIC_CAMID_ALLOC
	if (cam_id >=0 && cam_id <=3)
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_DK_CFG, (u8*)_TRUE);
	#endif
	/* 8814au should set both broadcast and unicast CAM entry for WEP key in STA mode */
	if (_rtw_camctl_chk_cap(padapter, SEC_CAP_CHK_BMC) && is_wep_enc(psecuritypriv->dot11PrivacyAlgrthm)) {
		struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

		sta_pparm.algorithm = pparm->algorithm;
		sta_pparm.keyid = pparm->keyid;
		_rtw_memcpy(sta_pparm.key, pparm->key, 16);
		_rtw_memcpy(sta_pparm.addr, get_bssid(pmlmepriv), ETH_ALEN);
		set_stakey_hdl(padapter, (u8 *) &sta_pparm);
	}

enable_mc:
	//allow multicast packets to driver
	rtw_hal_set_hwreg(padapter, HW_VAR_ON_RCR_AM, null_addr);

	return H2C_SUCCESS;
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

	if(pparm->algorithm == _NO_PRIVACY_)
		goto write_to_cam;

	psta = rtw_get_stainfo(pstapriv, pparm->addr);
	if (!psta) {
		DBG_871X_LEVEL(_drv_always_, "%s sta:"MAC_FMT" not found\n", __func__, MAC_ARG(pparm->addr));
		ret = H2C_REJECTED;
		goto exit;
	}

	pmlmeinfo->enc_algo = pparm->algorithm;
	cam_id = rtw_camid_alloc(padapter, psta, 0, &used);
	if (cam_id < 0)
		goto exit;

	#ifdef DYNAMIC_CAMID_ALLOC
	/* cam entry searched is group key */
	if (used == _TRUE && rtw_camid_is_gk(padapter, cam_id) == _TRUE) {
		s16 camid_clr;

		DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" pairwise key with "MAC_FMT" id:%u the same key id as group key\n"
			, FUNC_ADPT_ARG(padapter), MAC_ARG(pparm->addr), pparm->keyid);

		/* HW has problem to distinguish this pairwise key with existing group key, stop HW enc and dec for BMC */
		rtw_camctl_set_flags(padapter, SEC_STATUS_STA_PK_GK_CONFLICT_DIS_BMC_SEARCH);
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, NULL);

		/* clear group key */
		while ((camid_clr = rtw_camid_search(padapter, pparm->addr, -1, 1)) >= 0) {
			DBG_871X_LEVEL(_drv_always_, "clear group key for addr:"MAC_FMT", camid:%d\n", MAC_ARG(pparm->addr), camid_clr);
			clear_cam_entry(padapter, camid_clr);
			rtw_camid_free(padapter, camid_clr);
		}
	}
	#endif

write_to_cam:
	if(pparm->algorithm == _NO_PRIVACY_) {
		while ((cam_id = rtw_camid_search(padapter, pparm->addr, -1, -1)) >= 0) {
			DBG_871X_LEVEL(_drv_always_, "clear key for addr:"MAC_FMT", camid:%d\n", MAC_ARG(pparm->addr), cam_id);
			clear_cam_entry(padapter, cam_id);
			rtw_camid_free(padapter,cam_id);
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

u8 add_ba_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct addBaReq_parm 	*pparm = (struct addBaReq_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	struct sta_info *psta = rtw_get_stainfo(&padapter->stapriv, pparm->addr);
	
	if(!psta)
		return 	H2C_SUCCESS;
		
#ifdef CONFIG_80211N_HT
	if (((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && (pmlmeinfo->HT_enable)) ||
		((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
	{
		//pmlmeinfo->ADDBA_retry_count = 0;
		//pmlmeinfo->candidate_tid_bitmap |= (0x1 << pparm->tid);		
		//psta->htpriv.candidate_tid_bitmap |= BIT(pparm->tid);
		issue_addba_req(padapter, pparm->addr, (u8)pparm->tid);
		//_set_timer(&pmlmeext->ADDBA_timer, ADDBA_TO);
		_set_timer(&psta->addba_retry_timer, ADDBA_TO);
	}
#ifdef CONFIG_TDLS
	else if((psta->tdls_sta_state & TDLS_LINKED_STATE)&& 
		(psta->htpriv.ht_option==_TRUE) && 
		(psta->htpriv.ampdu_enable==_TRUE) )
	{
		issue_addba_req(padapter, pparm->addr, (u8)pparm->tid);
		_set_timer(&psta->addba_retry_timer, ADDBA_TO);
	}
#endif //CONFIG
	else
	{		
		psta->htpriv.candidate_tid_bitmap &= ~BIT(pparm->tid);		
	}
#endif //CONFIG_80211N_HT
	return 	H2C_SUCCESS;
}


u8 add_ba_rsp_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct addBaRsp_parm *pparm = (struct addBaRsp_parm *)pbuf;
	u8 ret = _TRUE, i = 0, try_cnt = 3, wait_ms = 50;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;

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
		DBG_871X("DBG_RX_SEQ %s:%d IndicateSeq: %d, start_seq: %d\n", __func__, __LINE__,
			preorder_ctrl->indicate_seq, pparm->start_seq);
		#endif
	}
	#else
	preorder_ctrl->indicate_seq = 0xffff;
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

u8 chk_bmc_sleepq_cmd(_adapter* padapter)
{
	struct cmd_obj *ph2c;
	struct cmd_priv *pcmdpriv = &(padapter->cmdpriv);
	u8 res = _SUCCESS;

_func_enter_;

	if ((ph2c = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
	{
		res= _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_parm_rsp(ph2c, GEN_CMD_CODE(_ChkBMCSleepq));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

_func_exit_;

	return res;
}

u8 set_tx_beacon_cmd(_adapter* padapter)
{
	struct cmd_obj	*ph2c;
	struct Tx_Beacon_param 	*ptxBeacon_parm;	
	struct cmd_priv	*pcmdpriv = &(padapter->cmdpriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	res = _SUCCESS;
	int len_diff = 0;
	
_func_enter_;	

	if ((ph2c = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
	{
		res= _FAIL;
		goto exit;
	}

	if ((ptxBeacon_parm = (struct Tx_Beacon_param *)rtw_zmalloc(sizeof(struct Tx_Beacon_param))) == NULL)
	{
		rtw_mfree((unsigned char *)ph2c, sizeof(struct	cmd_obj));
		res= _FAIL;
		goto exit;
	}

	_rtw_memcpy(&(ptxBeacon_parm->network), &(pmlmeinfo->network), sizeof(WLAN_BSSID_EX));

	len_diff = update_hidden_ssid(
		ptxBeacon_parm->network.IEs+_BEACON_IE_OFFSET_
		, ptxBeacon_parm->network.IELength-_BEACON_IE_OFFSET_
		, pmlmeinfo->hidden_ssid_mode
	);
	ptxBeacon_parm->network.IELength += len_diff;

	init_h2fwcmd_w_parm_no_rsp(ph2c, ptxBeacon_parm, GEN_CMD_CODE(_TX_Beacon));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

	
exit:
	
_func_exit_;

	return res;
}


u8 mlme_evt_hdl(_adapter *padapter, unsigned char *pbuf)
{
	u8 evt_code, evt_seq;
	u16 evt_sz;
	uint 	*peventbuf;
	void (*event_callback)(_adapter *dev, u8 *pbuf);
	struct evt_priv *pevt_priv = &(padapter->evtpriv);

	if (pbuf == NULL)
		goto _abort_event_;

	peventbuf = (uint*)pbuf;
	evt_sz = (u16)(*peventbuf&0xffff);
	evt_seq = (u8)((*peventbuf>>24)&0x7f);
	evt_code = (u8)((*peventbuf>>16)&0xff);
	
		
	#ifdef CHECK_EVENT_SEQ
	// checking event sequence...		
	if (evt_seq != (ATOMIC_READ(&pevt_priv->event_seq) & 0x7f) )
	{
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("Evetn Seq Error! %d vs %d\n", (evt_seq & 0x7f), (ATOMIC_READ(&pevt_priv->event_seq) & 0x7f)));
	
		pevt_priv->event_seq = (evt_seq+1)&0x7f;

		goto _abort_event_;
	}
	#endif

	// checking if event code is valid
	if (evt_code >= MAX_C2HEVT)
	{
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\nEvent Code(%d) mismatch!\n", evt_code));
		goto _abort_event_;
	}

	// checking if event size match the event parm size	
	if ((wlanevents[evt_code].parmsize != 0) && 
			(wlanevents[evt_code].parmsize != evt_sz))
	{
			
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\nEvent(%d) Parm Size mismatch (%d vs %d)!\n", 
			evt_code, wlanevents[evt_code].parmsize, evt_sz));
		goto _abort_event_;	
			
	}

	ATOMIC_INC(&pevt_priv->event_seq);

	peventbuf += 2;
				
	if(peventbuf)
	{
		event_callback = wlanevents[evt_code].event_callback;
		event_callback(padapter, (u8*)peventbuf);

		pevt_priv->evt_done_cnt++;
	}


_abort_event_:


	return H2C_SUCCESS;
		
}

u8 h2c_msg_hdl(_adapter *padapter, unsigned char *pbuf)
{
	if(!pbuf)
		return H2C_PARAMETERS_ERROR;

	return H2C_SUCCESS;
}

u8 chk_bmc_sleepq_hdl(_adapter *padapter, unsigned char *pbuf)
{
#ifdef CONFIG_AP_MODE
	_irqL irqL;
	struct sta_info *psta_bmc;
	_list	*xmitframe_plist, *xmitframe_phead;
	struct xmit_frame *pxmitframe=NULL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct sta_priv  *pstapriv = &padapter->stapriv;

	//for BC/MC Frames
	psta_bmc = rtw_get_bcmc_stainfo(padapter);
	if(!psta_bmc)
		return H2C_SUCCESS;

	if((pstapriv->tim_bitmap&BIT(0)) && (psta_bmc->sleepq_len>0))
	{
#ifndef CONFIG_PCI_HCI
		rtw_msleep_os(10);// 10ms, ATIM(HIQ) Windows
#endif
		//_enter_critical_bh(&psta_bmc->sleep_q.lock, &irqL);
		_enter_critical_bh(&pxmitpriv->lock, &irqL);

		xmitframe_phead = get_list_head(&psta_bmc->sleep_q);
		xmitframe_plist = get_next(xmitframe_phead);

		while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE)
		{
			pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

			xmitframe_plist = get_next(xmitframe_plist);

			rtw_list_delete(&pxmitframe->list);

			psta_bmc->sleepq_len--;
			if(psta_bmc->sleepq_len>0)
				pxmitframe->attrib.mdata = 1;
			else
				pxmitframe->attrib.mdata = 0;

			pxmitframe->attrib.triggered=1;

			if (xmitframe_hiq_filter(pxmitframe) == _TRUE)
				pxmitframe->attrib.qsel = QSLT_HIGH;//HIQ

			#if 0
			_exit_critical_bh(&psta_bmc->sleep_q.lock, &irqL);
			if(rtw_hal_xmit(padapter, pxmitframe) == _TRUE)
			{
				rtw_os_xmit_complete(padapter, pxmitframe);
			}
			_enter_critical_bh(&psta_bmc->sleep_q.lock, &irqL);
			#endif
			rtw_hal_xmitframe_enqueue(padapter, pxmitframe);
		}

		//_exit_critical_bh(&psta_bmc->sleep_q.lock, &irqL);
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
	if(send_beacon(padapter)==_FAIL)
	{
		DBG_871X("issue_beacon, fail!\n");
		return H2C_PARAMETERS_ERROR;
	}


	if (padapter->registrypriv.wifi_spec == 1)
		return H2C_SUCCESS;
	
	/* tx bc/mc frames after update TIM */
	chk_bmc_sleepq_hdl(padapter, NULL);

	return H2C_SUCCESS;
}

/*
* according to channel
* add/remove WLAN_BSSID_EX.IEs's ERP ie
* set WLAN_BSSID_EX.SupportedRates
* update WLAN_BSSID_EX.IEs's Supported Rate and Extended Supported Rate ie
*/
void change_band_update_ie(_adapter *padapter, WLAN_BSSID_EX *pnetwork, u8 ch)
{
	u8	network_type,rate_len, total_rate_len,remainder_rate_len;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	erpinfo=0x4;

	if (ch >= 36) {
		network_type = WIRELESS_11A;
		total_rate_len = IEEE80211_NUM_OFDM_RATESLEN;
		rtw_remove_bcn_ie(padapter, pnetwork, _ERPINFO_IE_);
	} else {
		network_type = WIRELESS_11BG;
		total_rate_len = IEEE80211_CCK_RATE_LEN+IEEE80211_NUM_OFDM_RATESLEN;
		rtw_add_bcn_ie(padapter, pnetwork, _ERPINFO_IE_, &erpinfo, 1);
	}

	rtw_set_supported_rate(pnetwork->SupportedRates, network_type);

	UpdateBrateTbl(padapter, pnetwork->SupportedRates);

	if(total_rate_len > 8)
	{
		rate_len = 8;
		remainder_rate_len = total_rate_len - 8;
	}
	else
	{
		rate_len = total_rate_len;
		remainder_rate_len = 0;
	}

	rtw_add_bcn_ie(padapter, pnetwork, _SUPPORTEDRATES_IE_, pnetwork->SupportedRates, rate_len);

	if(remainder_rate_len)
	{
		rtw_add_bcn_ie(padapter, pnetwork, _EXT_SUPPORTEDRATES_IE_, (pnetwork->SupportedRates+8), remainder_rate_len);
	}
	else
	{
		rtw_remove_bcn_ie(padapter, pnetwork, _EXT_SUPPORTEDRATES_IE_);
	}

	pnetwork->Length = get_WLAN_BSSID_EX_sz(pnetwork);
}

#ifdef CONFIG_CONCURRENT_MODE
sint check_buddy_mlmeinfo_state(_adapter *padapter, u32 state)
{
	PADAPTER pbuddy_adapter;
	struct mlme_ext_priv *pbuddy_mlmeext;
	struct mlme_ext_info *pbuddy_mlmeinfo;

	if(padapter == NULL)
		return _FALSE;	
	
	pbuddy_adapter = padapter->pbuddy_adapter;

	if(pbuddy_adapter == NULL)
		return _FALSE;	


	pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;
	pbuddy_mlmeinfo = &(pbuddy_mlmeext->mlmext_info);
		
	if((pbuddy_mlmeinfo->state&0x03) == state)
		return _TRUE;		

	return _FALSE;
	
}
#endif /* CONFIG_CONCURRENT_MODE */

void rtw_join_done_chk_ch(_adapter *adapter, int join_res)
{
#define DUMP_ADAPTERS_STATUS 0

	struct dvobj_priv *dvobj;
	_adapter *iface;
	struct mlme_priv *mlme;
	struct mlme_ext_priv *mlmeext;
	u8 u_ch, u_offset, u_bw;
	int i;

	dvobj = adapter_to_dvobj(adapter);

	if (DUMP_ADAPTERS_STATUS) {
		DBG_871X(FUNC_ADPT_FMT" enter\n", FUNC_ADPT_ARG(adapter));
		dump_adapters_status(RTW_DBGDUMP , dvobj);
	}

	if (join_res >= 0) {
		if (rtw_get_ch_setting_union(adapter, &u_ch, &u_bw, &u_offset) <= 0) {
			dump_adapters_status(RTW_DBGDUMP , dvobj);
			rtw_warn_on(1);
		}

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			mlme = &iface->mlmepriv;
			mlmeext = &iface->mlmeextpriv;

			if (!iface || iface == adapter)
				continue;

			if (check_fwstate(mlme, WIFI_AP_STATE)
				&& check_fwstate(mlme, WIFI_ASOC_STATE)
			) {
				bool is_grouped = rtw_is_chbw_grouped(u_ch, u_bw, u_offset
					, mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset);

				if (is_grouped == _FALSE) {
					/* handle AP which need to switch ch setting */

					/* restore original bw, adjust bw by registry setting on target ch */
					mlmeext->cur_bwmode = mlme->ori_bw;
					mlmeext->cur_channel = u_ch;
					rtw_adjust_chbw(iface
						, mlmeext->cur_channel, &mlmeext->cur_bwmode, &mlmeext->cur_ch_offset);
					
					rtw_sync_chbw(&mlmeext->cur_channel, &mlmeext->cur_bwmode, &mlmeext->cur_ch_offset
						, &u_ch, &u_bw, &u_offset);

					rtw_ap_update_bss_chbw(iface, &(mlmeext->mlmext_info.network)
						, mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset);

					_rtw_memcpy(&(mlme->cur_network.network), &(mlmeext->mlmext_info.network), sizeof(WLAN_BSSID_EX));

					rtw_start_bss_hdl_after_chbw_decided(iface);
				}

				update_beacon(iface, 0, NULL, _TRUE);
			}

			clr_fwstate(mlme, WIFI_OP_CH_SWITCHING);
		}

		#ifdef CONFIG_DFS_MASTER
		rtw_dfs_master_status_apply(adapter, MLME_STA_CONNECTED);
		#endif
	} else {
		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			mlme = &iface->mlmepriv;
			mlmeext = &iface->mlmeextpriv;

			if (!iface || iface == adapter)
				continue;

			if (check_fwstate(mlme, WIFI_AP_STATE)
				&& check_fwstate(mlme, WIFI_ASOC_STATE))
				update_beacon(iface, 0, NULL, _TRUE);

			clr_fwstate(mlme, WIFI_OP_CH_SWITCHING);
		}
		#ifdef CONFIG_DFS_MASTER
		rtw_dfs_master_status_apply(adapter, MLME_STA_DISCONNECTED);
		#endif
	}

	if (rtw_get_ch_setting_union(adapter, &u_ch, &u_bw, &u_offset))
		set_channel_bwmode(adapter, u_ch, u_offset, u_bw);

	if (DUMP_ADAPTERS_STATUS) {
		DBG_871X(FUNC_ADPT_FMT" exit\n", FUNC_ADPT_ARG(adapter));
		dump_adapters_status(RTW_DBGDUMP , dvobj);
	}
}

int rtw_chk_start_clnt_join(_adapter *adapter, u8 *ch, u8 *bw, u8 *offset)
{
	bool chbw_allow = _TRUE;
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
		DBG_871X_LEVEL(_drv_err_, FUNC_ADPT_FMT" cur_ch:%u\n"
			, FUNC_ADPT_ARG(adapter), cur_ch);
		rtw_warn_on(1);
		goto exit;
	}
	DBG_871X(FUNC_ADPT_FMT" req: %u,%u,%u\n", FUNC_ADPT_ARG(adapter), u_ch, u_bw, u_offset);

#ifdef CONFIG_CONCURRENT_MODE
{
	struct dvobj_priv *dvobj;
	_adapter *iface;
	struct mlme_priv *mlme;
	struct mlme_ext_priv *mlmeext;
	u8 sta_num;
	u8 ld_sta_num;
	u8 lg_sta_num;
	u8 ap_num;
	u8 ld_ap_num;
	int i;

	dvobj = adapter_to_dvobj(adapter);

	rtw_dev_iface_status_no_self(adapter, &sta_num, &ld_sta_num, &lg_sta_num, &ap_num, &ld_ap_num);
	DBG_871X(FUNC_ADPT_FMT" ld_sta_num:%u, ap_num:%u\n"
		, FUNC_ADPT_ARG(adapter), ld_sta_num, ap_num);

	if (!ld_sta_num && !ap_num) {
		/* consider linking STA? */
		goto connect_allow_hdl;
	}

	if (rtw_get_ch_setting_union_no_self(adapter, &u_ch, &u_bw, &u_offset) <= 0) {
		dump_adapters_status(RTW_DBGDUMP , dvobj);
		rtw_warn_on(1);
	}
	DBG_871X(FUNC_ADPT_FMT" union no self: %u,%u,%u\n"
		, FUNC_ADPT_ARG(adapter), u_ch, u_bw, u_offset);

	/* chbw_allow? */
	chbw_allow = rtw_is_chbw_grouped(pmlmeext->cur_channel, pmlmeext->cur_bwmode, pmlmeext->cur_ch_offset
		, u_ch, u_bw, u_offset);

	DBG_871X(FUNC_ADPT_FMT" chbw_allow:%d\n"
		, FUNC_ADPT_ARG(adapter), chbw_allow);

	if (chbw_allow == _TRUE) {
		rtw_sync_chbw(&cur_ch, &cur_bw, &cur_ch_offset, &u_ch, &u_bw, &u_offset);
		rtw_warn_on(cur_ch != pmlmeext->cur_channel);
		rtw_warn_on(cur_bw != pmlmeext->cur_bwmode);
		rtw_warn_on(cur_ch_offset != pmlmeext->cur_ch_offset);
		goto connect_allow_hdl;
	}

	/* chbw_allow is _FALSE, connect allow? */
	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		mlme = &iface->mlmepriv;
		mlmeext = &iface->mlmeextpriv;

		#ifdef CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT
		if (check_fwstate(mlme, WIFI_STATION_STATE)
			&& check_fwstate(mlme, WIFI_ASOC_STATE)
			#if defined(CONFIG_P2P)
			&& rtw_p2p_chk_state(&(iface->wdinfo), P2P_STATE_NONE)
			#endif
		) {
			connect_allow = _FALSE;
			break;
		}
		#endif /* CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT */
	}
	DBG_871X(FUNC_ADPT_FMT" connect_allow:%d\n"
		, FUNC_ADPT_ARG(adapter), connect_allow);

	if (connect_allow == _FALSE)
		goto exit;

connect_allow_hdl:
	/* connect_allow == _TRUE */

	#ifdef CONFIG_DFS_MASTER
	rtw_dfs_master_status_apply(adapter, MLME_STA_CONNECTING);
	#endif

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

			if (check_fwstate(mlme, WIFI_AP_STATE)
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
			} else if (check_fwstate(mlme, WIFI_STATION_STATE)
				&& check_fwstate(mlme, WIFI_ASOC_STATE)
			) {
				rtw_disassoc_cmd(iface, 500, _FALSE);
				rtw_indicate_disconnect(iface, 0, _FALSE);
				rtw_free_assoc_resources(iface, 1);
			}
		}
	}
}
#endif /* CONFIG_CONCURRENT_MODE */

exit:

	if (connect_allow == _TRUE) {
		DBG_871X(FUNC_ADPT_FMT" union: %u,%u,%u\n", FUNC_ADPT_ARG(adapter), u_ch, u_bw, u_offset);
		*ch = u_ch;
		*bw = u_bw;
		*offset = u_offset;
	}

	return connect_allow == _TRUE ? _SUCCESS : _FAIL;
}

/* Find union about ch, bw, ch_offset of all linked/linking interfaces */
int _rtw_get_ch_setting_union(_adapter *adapter, u8 *ch, u8 *bw, u8 *offset, bool include_self)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	_adapter *iface;
	struct mlme_ext_priv *mlmeext;
	int i;
	u8 ch_ret = 0;
	u8 bw_ret = CHANNEL_WIDTH_20;
	u8 offset_ret = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	int num = 0;

	if (ch) *ch = 0;
	if (bw) *bw = CHANNEL_WIDTH_20;
	if (offset) *offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	for (i = 0; i<dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		mlmeext = &iface->mlmeextpriv;

		if (!check_fwstate(&iface->mlmepriv, _FW_LINKED|_FW_UNDER_LINKING))
			continue;

		if (check_fwstate(&iface->mlmepriv, WIFI_OP_CH_SWITCHING))
			continue;

		if (include_self == _FALSE && adapter == iface)
			continue;

		if (num == 0) {
			ch_ret = mlmeext->cur_channel;
			bw_ret = mlmeext->cur_bwmode;
			offset_ret = mlmeext->cur_ch_offset;
			num++;
			continue;
		}

		if (ch_ret != mlmeext->cur_channel) {
			num = 0;
			break;
		}

		if (bw_ret < mlmeext->cur_bwmode) {
			bw_ret = mlmeext->cur_bwmode;
			offset_ret = mlmeext->cur_ch_offset;
		} else if (bw_ret == mlmeext->cur_bwmode && offset_ret != mlmeext->cur_ch_offset) {
			num = 0;
			break;
		}

		num++;
	}

	if (num) {
		if (ch) *ch = ch_ret;
		if (bw) *bw = bw_ret;
		if (offset) *offset = offset_ret;
	}

	return num;
}

inline int rtw_get_ch_setting_union(_adapter *adapter, u8 *ch, u8 *bw, u8 *offset)
{
	return _rtw_get_ch_setting_union(adapter, ch, bw, offset, 1);
}

inline int rtw_get_ch_setting_union_no_self(_adapter *adapter, u8 *ch, u8 *bw, u8 *offset)
{
	return _rtw_get_ch_setting_union(adapter, ch, bw, offset, 0);
}

void _rtw_dev_iface_status(_adapter *adapter, u8 *sta_num, u8 *ld_sta_num, u8 *lg_sta_num
	, u8 *ap_num, u8 *ld_ap_num, bool include_self)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	_adapter *iface;
	struct mlme_ext_priv *mlmeext;
	struct mlme_ext_info *mlmeextinfo;
	int i;
	u8 sta_num_ret = 0;
	u8 ld_sta_num_ret = 0;
	u8 lg_sta_num_ret = 0;
	u8 ap_num_ret = 0;
	u8 ld_ap_num_ret = 0;

	if (sta_num)
		*sta_num = 0;
	if (ld_sta_num)
		*ld_sta_num = 0;
	if (lg_sta_num)
		*lg_sta_num = 0;
	if (ap_num)
		*ap_num = 0;
	if (ld_ap_num)
		*ld_ap_num = 0;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];

		if (include_self == _FALSE && iface == adapter)
			continue;

		mlmeext = &iface->mlmeextpriv;

		if (mlmeext_msr(mlmeext) == WIFI_FW_STATION_STATE) {
			sta_num_ret++;
			if (check_fwstate(&iface->mlmepriv, _FW_LINKED) == _TRUE)
				ld_sta_num_ret++;
			if (check_fwstate(&iface->mlmepriv, _FW_UNDER_LINKING) == _TRUE)
				lg_sta_num_ret++;
		}

		if (mlmeext_msr(mlmeext) == WIFI_FW_AP_STATE
			&& check_fwstate(&iface->mlmepriv, _FW_LINKED) == _TRUE
		) {
			ap_num_ret++;
			if (iface->stapriv.asoc_sta_count > 2)
				ld_ap_num_ret++;
		}
	}

	if (sta_num)
		*sta_num = sta_num_ret;
	if (ld_sta_num)
		*ld_sta_num = ld_sta_num_ret;
	if (lg_sta_num)
		*lg_sta_num = lg_sta_num_ret;
	if (ap_num)
		*ap_num = ap_num_ret;
	if (ld_ap_num)
		*ld_ap_num = ld_ap_num_ret;
}

inline void rtw_dev_iface_status(_adapter *adapter, u8 *sta_num, u8 *ld_sta_num, u8 *lg_sta_num
	, u8 *ap_num, u8 *ld_ap_num)
{
	return _rtw_dev_iface_status(adapter, sta_num, ld_sta_num, lg_sta_num, ap_num, ld_ap_num, 1);
}

inline void rtw_dev_iface_status_no_self(_adapter *adapter, u8 *sta_num, u8 *ld_sta_num, u8 *lg_sta_num
	, u8 *ap_num, u8 *ld_ap_num)
{
	return _rtw_dev_iface_status(adapter, sta_num, ld_sta_num, lg_sta_num, ap_num, ld_ap_num, 0);
}

u8 set_ch_hdl(_adapter *padapter, u8 *pbuf)
{
	struct set_ch_parm *set_ch_parm;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	if(!pbuf)
		return H2C_PARAMETERS_ERROR;

	set_ch_parm = (struct set_ch_parm *)pbuf;

	DBG_871X(FUNC_NDEV_FMT" ch:%u, bw:%u, ch_offset:%u\n",
		FUNC_NDEV_ARG(padapter->pnetdev),
		set_ch_parm->ch, set_ch_parm->bw, set_ch_parm->ch_offset);

	pmlmeext->cur_channel = set_ch_parm->ch;
	pmlmeext->cur_ch_offset = set_ch_parm->ch_offset;
	pmlmeext->cur_bwmode = set_ch_parm->bw;

	set_channel_bwmode(padapter, set_ch_parm->ch, set_ch_parm->ch_offset, set_ch_parm->bw);

	return 	H2C_SUCCESS;
}

u8 set_chplan_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct SetChannelPlan_param *setChannelPlan_param;
	struct mlme_priv *mlme = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	if(!pbuf)
		return H2C_PARAMETERS_ERROR;

	setChannelPlan_param = (struct SetChannelPlan_param *)pbuf;

	if(!rtw_is_channel_plan_valid(setChannelPlan_param->channel_plan)) {
		return H2C_PARAMETERS_ERROR;
	}

	mlme->country_ent = setChannelPlan_param->country_ent;
	mlme->ChannelPlan = setChannelPlan_param->channel_plan;

	pmlmeext->max_chan_nums = init_channel_set(padapter, setChannelPlan_param->channel_plan, pmlmeext->channel_set);
	init_channel_list(padapter, pmlmeext->channel_set, pmlmeext->max_chan_nums, &pmlmeext->channel_list);	

	rtw_hal_set_odm_var(padapter,HAL_ODM_REGULATION,NULL,_TRUE);
	
#ifdef CONFIG_IOCTL_CFG80211
	rtw_reg_notify_by_driver(padapter);
#endif //CONFIG_IOCTL_CFG80211

	return 	H2C_SUCCESS;
}

u8 led_blink_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct LedBlink_param *ledBlink_param;

	if(!pbuf)
		return H2C_PARAMETERS_ERROR;

	ledBlink_param = (struct LedBlink_param *)pbuf;

	#ifdef CONFIG_LED_HANDLED_BY_CMD_THREAD
	BlinkHandler((PLED_DATA)ledBlink_param->pLed);
	#endif

	return 	H2C_SUCCESS;
}

u8 set_csa_hdl(_adapter *padapter, unsigned char *pbuf)
{
#ifdef CONFIG_DFS
	struct SetChannelSwitch_param *setChannelSwitch_param;
	u8 new_ch_no;
	u8 gval8 = 0x00, sval8 = 0xff;

	if(!pbuf)
		return H2C_PARAMETERS_ERROR;

	setChannelSwitch_param = (struct SetChannelSwitch_param *)pbuf;
	new_ch_no = setChannelSwitch_param->new_ch_no;

	rtw_hal_get_hwreg(padapter, HW_VAR_TXPAUSE, &gval8);

	rtw_hal_set_hwreg(padapter, HW_VAR_TXPAUSE, &sval8);

	DBG_871X("DFS detected! Swiching channel to %d!\n", new_ch_no);
	SelectChannel(padapter, new_ch_no);

	rtw_hal_set_hwreg(padapter, HW_VAR_TXPAUSE, &gval8);

	rtw_disassoc_cmd(padapter, 0, _FALSE);
	rtw_indicate_disconnect(iface, 0, _FALSE);
	rtw_free_assoc_resources(padapter, 1);
	rtw_free_network_queue(padapter, _TRUE);

	if ( ((new_ch_no >= 52) && (new_ch_no <= 64)) ||((new_ch_no >= 100) && (new_ch_no <= 140)) ) {
		DBG_871X("Switched to DFS band (ch %02x) again!!\n", new_ch_no);
	}

	return 	H2C_SUCCESS;
#else
	return	H2C_REJECTED;
#endif //CONFIG_DFS

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
	u8 survey_channel, i, min, option;
	struct tdls_txmgmt txmgmt;
	u32 setchtime, resp_sleep = 0, wait_time;
	u8 zaddr[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	u8 ret;
	u8 doiqk;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	TDLSoption = (struct TDLSoption_param *)pbuf;
	option = TDLSoption->option;

	if (!_rtw_memcmp(TDLSoption->addr, zaddr, ETH_ALEN)) {
		ptdls_sta = rtw_get_stainfo( &(padapter->stapriv), TDLSoption->addr );
		if (ptdls_sta == NULL) {
			return H2C_REJECTED;
		}
	} else {
		if (!(option == TDLS_RS_RCR))
			return H2C_REJECTED;
	}

	//_enter_critical_bh(&(ptdlsinfo->hdl_lock), &irqL);
	//DBG_871X("[%s] option:%d\n", __FUNCTION__, option);
		
	switch (option) {
	case TDLS_ESTABLISHED:
	{
		/* As long as TDLS handshake success, we should set RCR_CBSSID_DATA bit to 0 */
		/* So we can receive all kinds of data frames. */
		u8 sta_band = 0;

		//leave ALL PS when TDLS is established
			rtw_pwr_wakeup(padapter);

		rtw_hal_set_hwreg(padapter, HW_VAR_TDLS_WRCR, 0);
		DBG_871X("Created Direct Link with "MAC_FMT"\n", MAC_ARG(ptdls_sta->hwaddr));

		/* Set TDLS sta rate. */
		/* Update station supportRate */
		rtw_hal_update_sta_rate_mask(padapter, ptdls_sta);
		if (pmlmeext->cur_channel > 14) {
			if (ptdls_sta->ra_mask & 0xffff000)
				sta_band |= WIRELESS_11_5N ;

			if (ptdls_sta->ra_mask & 0xff0)
				sta_band |= WIRELESS_11A;

			/* 5G band */
			#ifdef CONFIG_80211AC_VHT
			if (ptdls_sta->vhtpriv.vht_option)
				sta_band = WIRELESS_11_5AC;
			#endif
			
		} else {
			if (ptdls_sta->ra_mask & 0xffff000)
				sta_band |= WIRELESS_11_24N;

			if (ptdls_sta->ra_mask & 0xff0)
				sta_band |= WIRELESS_11G;

			if (ptdls_sta->ra_mask & 0x0f)
				sta_band |= WIRELESS_11B;
		}
		ptdls_sta->wireless_mode = sta_band;
		ptdls_sta->raid = rtw_hal_networktype_to_raid(padapter,ptdls_sta);
		set_sta_rate(padapter, ptdls_sta);
		rtw_sta_media_status_rpt(padapter, ptdls_sta, 1);
		/* Sta mode */
		rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, ptdls_sta,_TRUE);
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
		_rtw_memcpy(txmgmt.peer, ptdls_sta->hwaddr, ETH_ALEN);

		issue_nulldata(padapter, NULL, 1, 3, 3);

		DBG_871X("[TDLS ] issue tdls channel switch response\n");
		ret = issue_tdls_ch_switch_rsp(padapter, &txmgmt, _TRUE);

		/* If we receive TDLS_CH_SW_REQ at off channel which it's target is AP's channel */
		/* then we just SelectChannel to AP's channel*/
		if (padapter->mlmeextpriv.cur_channel == pchsw_info->off_ch_num) {
			rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_CH_SW_END_TO_BASE_CHNL);
			break;
		}

		if (ret == _SUCCESS)
			rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_CH_SW_TO_OFF_CHNL);
		else
			DBG_871X("[TDLS] issue_tdls_ch_switch_rsp wait ack fail !!!!!!!!!!\n");
		
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
		set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

		rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_CH_SW_START);

		pchsw_info->ch_sw_state &= ~(TDLS_CH_SWITCH_PREPARE_STATE);
		
		break;
	case TDLS_CH_SW_START:
		rtw_tdls_set_ch_sw_oper_control(padapter, _TRUE);
		break;
	case TDLS_CH_SW_TO_OFF_CHNL:
		issue_nulldata(padapter, NULL, 1, 3, 3);

		if (!(pchsw_info->ch_sw_state & TDLS_CH_SW_INITIATOR_STATE))
			_set_timer(&ptdls_sta->ch_sw_timer, (u32)(ptdls_sta->ch_switch_timeout) /1000);

		if (rtw_tdls_do_ch_sw(padapter, ptdls_sta, TDLS_CH_SW_OFF_CHNL, pchsw_info->off_ch_num,
			pchsw_info->ch_offset, (pchsw_info->ch_offset) ? CHANNEL_WIDTH_40 : CHANNEL_WIDTH_20, ptdls_sta->ch_switch_time) == _SUCCESS) {
				pchsw_info->ch_sw_state &= ~(TDLS_PEER_AT_OFF_STATE);
				if (pchsw_info->ch_sw_state & TDLS_CH_SW_INITIATOR_STATE) {
					if (issue_nulldata_to_TDLS_peer_STA(ptdls_sta->padapter, ptdls_sta->hwaddr, 0, 1, 3) == _FAIL)
						rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_CH_SW_TO_BASE_CHNL);
				}
		}
		else {
			u8 bcancelled;
					
			if (!(pchsw_info->ch_sw_state & TDLS_CH_SW_INITIATOR_STATE))
				_cancel_timer(&ptdls_sta->ch_sw_timer,&bcancelled);
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
			rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_CH_SW_TO_BASE_CHNL);

		break;
	case TDLS_CH_SW_TO_BASE_CHNL_UNSOLICITED:
	case TDLS_CH_SW_TO_BASE_CHNL:	
		pchsw_info->ch_sw_state &= ~(TDLS_PEER_AT_OFF_STATE | TDLS_WAIT_CH_RSP_STATE);

		if (option == TDLS_CH_SW_TO_BASE_CHNL_UNSOLICITED) {
			if (ptdls_sta != NULL) {
				/* Send unsolicited channel switch rsp. to peer */ 
				_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
				txmgmt.status_code = 0;
				_rtw_memcpy(txmgmt.peer, ptdls_sta->hwaddr, ETH_ALEN);
				issue_tdls_ch_switch_rsp(padapter, &txmgmt, _FALSE);
			}
		}

		if (rtw_tdls_do_ch_sw(padapter, ptdls_sta, TDLS_CH_SW_BASE_CHNL, pmlmeext->cur_channel,
			pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode, ptdls_sta->ch_switch_time) == _SUCCESS) {
			issue_nulldata(padapter, NULL, 0, 3, 3);
			/* set ch sw monitor timer for responder */
			if (!(pchsw_info->ch_sw_state & TDLS_CH_SW_INITIATOR_STATE))
				_set_timer(&ptdls_sta->ch_sw_monitor_timer, TDLS_CH_SW_MONITOR_TIMEOUT);
		}

		break;
#endif		
	case TDLS_RS_RCR:
		rtw_hal_set_hwreg(padapter, HW_VAR_TDLS_RS_RCR, 0);
		DBG_871X("[TDLS] wirte REG_RCR, set bit6 on\n");
		break;
	case TDLS_TEARDOWN_STA:
		_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
		txmgmt.status_code = 0;
		_rtw_memcpy(txmgmt.peer, ptdls_sta->hwaddr, ETH_ALEN);

		issue_tdls_teardown(padapter, &txmgmt, _TRUE);
		break;
	case TDLS_TEARDOWN_STA_LOCALLY:
#ifdef CONFIG_TDLS_CH_SW	
		if (_rtw_memcmp(TDLSoption->addr, pchsw_info->addr, ETH_ALEN) == _TRUE) {
			pchsw_info->ch_sw_state &= ~(TDLS_CH_SW_INITIATOR_STATE |
										TDLS_CH_SWITCH_ON_STATE |
										TDLS_PEER_AT_OFF_STATE);
			rtw_tdls_set_ch_sw_oper_control(padapter, _FALSE);
			_rtw_memset(pchsw_info->addr, 0x00, ETH_ALEN);
		}
#endif		
		rtw_sta_media_status_rpt(padapter, ptdls_sta, 0);
		free_tdls_sta(padapter, ptdls_sta);
		break;			
	}

	//_exit_critical_bh(&(ptdlsinfo->hdl_lock), &irqL);

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
	p = (struct RunInThread_param*)pbuf;

	if (p->func)
		p->func(p->context);

	return H2C_SUCCESS;
}

u8 rtw_getmacreg_hdl(_adapter *padapter, u8 *pbuf)
{

	struct readMAC_parm *preadmacparm = NULL;
	u8 sz = 0;
	u32	addr = 0;
	u32	value = 0;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	preadmacparm = (struct readMAC_parm *) pbuf;
	sz = preadmacparm->len;
	addr = preadmacparm->addr;
	value = 0;

	switch (sz) {
	case 1:
		value = rtw_read8(padapter, addr);
		break;
	case 2:
		value = rtw_read16(padapter, addr);
		break;
	case 4:
		value = rtw_read32(padapter, addr);
		break;
	default:
		DBG_871X("%s: Unknown size\n", __func__);
		break;
	}
	DBG_871X("%s: addr:0x%02x valeu:0x%02x\n", __func__, addr, value);

	return H2C_SUCCESS;
}

