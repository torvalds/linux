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
 ******************************************************************************/
#define _RTW_MLME_EXT_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <wifi.h>
#include <rtw_mlme_ext.h>
#include <wlan_bssdef.h>
#include <mlme_osdep.h>
#include <recv_osdep.h>
#include <ethernet.h>
#include <linux/ieee80211.h>

#ifdef CONFIG_8723AU_BT_COEXIST
#include <rtl8723a_hal.h>
#endif

static struct mlme_handler mlme_sta_tbl[]={
	{"OnAssocReq23a",		&OnAssocReq23a},
	{"OnAssocRsp23a",		&OnAssocRsp23a},
	{"OnReAssocReq",	&OnAssocReq23a},
	{"OnReAssocRsp",	&OnAssocRsp23a},
	{"OnProbeReq23a",		&OnProbeReq23a},
	{"OnProbeRsp23a",		&OnProbeRsp23a},

	/*----------------------------------------------------------
					below 2 are reserved
	-----------------------------------------------------------*/
	{"DoReserved23a",		&DoReserved23a},
	{"DoReserved23a",		&DoReserved23a},
	{"OnBeacon23a",		&OnBeacon23a},
	{"OnATIM",		&OnAtim23a},
	{"OnDisassoc23a",		&OnDisassoc23a},
	{"OnAuth23a",		&OnAuth23aClient23a},
	{"OnDeAuth23a",		&OnDeAuth23a},
	{"OnAction23a",		&OnAction23a},
};

static struct action_handler OnAction23a_tbl[]={
	{WLAN_CATEGORY_SPECTRUM_MGMT, "ACTION_SPECTRUM_MGMT", on_action_spct23a},
	{WLAN_CATEGORY_QOS, "ACTION_QOS", &OnAction23a_qos},
	{WLAN_CATEGORY_DLS, "ACTION_DLS", &OnAction23a_dls},
	{WLAN_CATEGORY_BACK, "ACTION_BACK", &OnAction23a_back23a},
	{WLAN_CATEGORY_PUBLIC, "ACTION_PUBLIC", on_action_public23a},
	{WLAN_CATEGORY_HT, "ACTION_HT",	&OnAction23a_ht},
	{WLAN_CATEGORY_SA_QUERY, "ACTION_SA_QUERY", &DoReserved23a},
	{WLAN_CATEGORY_WMM, "ACTION_WMM", &OnAction23a_wmm},
	{WLAN_CATEGORY_VENDOR_SPECIFIC, "ACTION_P2P", &OnAction23a_p2p},
};

static u8	null_addr[ETH_ALEN]= {0, 0, 0, 0, 0, 0};

/**************************************************
OUI definitions for the vendor specific IE
***************************************************/
unsigned char	RTW_WPA_OUI23A[] = {0x00, 0x50, 0xf2, 0x01};
unsigned char WMM_OUI23A[] = {0x00, 0x50, 0xf2, 0x02};
unsigned char	WPS_OUI23A[] = {0x00, 0x50, 0xf2, 0x04};
unsigned char	P2P_OUI23A[] = {0x50, 0x6F, 0x9A, 0x09};
unsigned char	WFD_OUI23A[] = {0x50, 0x6F, 0x9A, 0x0A};

unsigned char	WMM_INFO_OUI23A[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01};
unsigned char	WMM_PARA_OUI23A[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};

unsigned char WPA_TKIP_CIPHER23A[4] = {0x00, 0x50, 0xf2, 0x02};
unsigned char RSN_TKIP_CIPHER23A[4] = {0x00, 0x0f, 0xac, 0x02};


/********************************************************
MCS rate definitions
*********************************************************/
unsigned char MCS_rate_2R23A[16] = {
	0xff, 0xff, 0x0, 0x0, 0x01, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
unsigned char MCS_rate_1R23A[16] = {
	0xff, 0x00, 0x0, 0x0, 0x01, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

/********************************************************
ChannelPlan definitions
*********************************************************/

static struct rt_channel_plan_2g	RTW_ChannelPlan2G[RT_CHANNEL_DOMAIN_2G_MAX] = {
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},		/*  0x00, RT_CHANNEL_DOMAIN_2G_WORLD , Passive scan CH 12, 13 */
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},		/*  0x01, RT_CHANNEL_DOMAIN_2G_ETSI1 */
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, 11},			/*  0x02, RT_CHANNEL_DOMAIN_2G_FCC1 */
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}, 14},	/*  0x03, RT_CHANNEL_DOMAIN_2G_MIKK1 */
	{{10, 11, 12, 13}, 4},					/*  0x04, RT_CHANNEL_DOMAIN_2G_ETSI2 */
	{{}, 0},									/*  0x05, RT_CHANNEL_DOMAIN_2G_NULL */
};

static struct rt_channel_plan_5g	RTW_ChannelPlan5G[RT_CHANNEL_DOMAIN_5G_MAX] = {
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

	/*  Driver self defined for old channel plan Compatible , Remember to modify if have new channel plan definition ===== */
	{{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165}, 21},				/*  0x11, RT_CHANNEL_DOMAIN_5G_FCC */
	{{36, 40, 44, 48}, 4},																			/*  0x12, RT_CHANNEL_DOMAIN_5G_JAPAN_NO_DFS */
	{{36, 40, 44, 48, 149, 153, 157, 161}, 8},																/*  0x13, RT_CHANNEL_DOMAIN_5G_FCC4_NO_DFS */
};

static struct rt_channel_plan_map	RTW_ChannelPlanMap[RT_CHANNEL_DOMAIN_MAX] = {
	/*  0x00 ~ 0x1F , Old Define ===== */
	{0x02, 0x11},	/* 0x00, RT_CHANNEL_DOMAIN_FCC */
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
	{0x01, 0x12},	/* 0x12, RT_CHANNEL_DOMAIN_JAPAN_NO_DFS */
	{0x00, 0x04},	/* 0x13, RT_CHANNEL_DOMAIN_WORLD_WIDE_5G */
	{0x02, 0x10},	/* 0x14, RT_CHANNEL_DOMAIN_TAIWAN_NO_DFS */
	{0x00, 0x12},	/* 0x15, RT_CHANNEL_DOMAIN_ETSI_NO_DFS */
	{0x00, 0x13},	/* 0x16, RT_CHANNEL_DOMAIN_KOREA_NO_DFS */
	{0x03, 0x12},	/* 0x17, RT_CHANNEL_DOMAIN_JAPAN_NO_DFS */
	{0x05, 0x08},	/* 0x18, RT_CHANNEL_DOMAIN_PAKISTAN_NO_DFS */
	{0x02, 0x08},	/* 0x19, RT_CHANNEL_DOMAIN_TAIWAN2_NO_DFS */
	{0x00, 0x00},	/* 0x1A, */
	{0x00, 0x00},	/* 0x1B, */
	{0x00, 0x00},	/* 0x1C, */
	{0x00, 0x00},	/* 0x1D, */
	{0x00, 0x00},	/* 0x1E, */
	{0x05, 0x04},	/* 0x1F, RT_CHANNEL_DOMAIN_WORLD_WIDE_ONLY_5G */
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
	{0x03, 0x00},	/* 0x41, RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN_2G */
};

static struct rt_channel_plan_map	RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE = {0x03, 0x02}; /* use the conbination for max channel numbers */

static struct fwevent wlanevents[] =
{
	{0, rtw_dummy_event_callback23a},	/*0*/
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, &rtw_survey_event_cb23a},		/*8*/
	{sizeof (struct surveydone_event), &rtw_surveydone_event_callback23a},	/*9*/

	{0, &rtw23a_joinbss_event_cb},		/*10*/
	{sizeof(struct stassoc_event), &rtw_stassoc_event_callback23a},
	{sizeof(struct stadel_event), &rtw_stadel_event_callback23a},
	{0, &rtw_atimdone_event_callback23a},
	{0, rtw_dummy_event_callback23a},
	{0, NULL},	/*15*/
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, rtw23a_fwdbg_event_callback},
	{0, NULL},	 /*20*/
	{0, NULL},
	{0, NULL},
	{0, &rtw_cpwm_event_callback23a},
	{0, NULL},
};


/*
 * Search the @param channel_num in given @param channel_set
 * @ch_set: the given channel set
 * @ch: the given channel number
 *
 * return the index of channel_num in channel_set, -1 if not found
 */
int rtw_ch_set_search_ch23a(struct rt_channel_info *ch_set, const u32 ch)
{
	int i;
	for (i = 0; ch_set[i]. ChannelNum != 0; i++) {
		if (ch == ch_set[i].ChannelNum)
			break;
	}

	if (i >= ch_set[i].ChannelNum)
		return -1;
	return i;
}

/****************************************************************************

Following are the initialization functions for WiFi MLME

*****************************************************************************/

int init_hw_mlme_ext23a(struct rtw_adapter *padapter)
{
	struct	mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	set_channel_bwmode23a(padapter, pmlmeext->cur_channel,
			      pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);
	return _SUCCESS;
}

static void init_mlme_ext_priv23a_value(struct rtw_adapter* padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	unsigned char	mixed_datarate[NumRates] = {
		_1M_RATE_, _2M_RATE_, _5M_RATE_, _11M_RATE_, _6M_RATE_,
		_9M_RATE_, _12M_RATE_, _18M_RATE_, _24M_RATE_, _36M_RATE_,
		_48M_RATE_, _54M_RATE_, 0xff};
	unsigned char	mixed_basicrate[NumRates] = {
		_1M_RATE_, _2M_RATE_, _5M_RATE_, _11M_RATE_, _6M_RATE_,
		_12M_RATE_, _24M_RATE_, 0xff,};

	atomic_set(&pmlmeext->event_seq, 0);
	/* reset to zero when disconnect at client mode */
	pmlmeext->mgnt_seq = 0;

	pmlmeext->cur_channel = padapter->registrypriv.channel;
	pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_20;
	pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	pmlmeext->retry = 0;

	pmlmeext->cur_wireless_mode = padapter->registrypriv.wireless_mode;

	memcpy(pmlmeext->datarate, mixed_datarate, NumRates);
	memcpy(pmlmeext->basicrate, mixed_basicrate, NumRates);

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

static int has_channel(struct rt_channel_info *channel_set,
		       u8 chanset_size, u8 chan) {
	int i;

	for (i = 0; i < chanset_size; i++) {
		if (channel_set[i].ChannelNum == chan)
			return 1;
	}

	return 0;
}

static void init_channel_list(struct rtw_adapter *padapter,
			      struct rt_channel_info *channel_set,
			      u8 chanset_size,
			      struct p2p_channels *channel_list) {

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
			if (!has_channel(channel_set, chanset_size, ch))
				continue;

			if ((0 == padapter->registrypriv.ht_enable) &&
			    (o->inc == 8))
				continue;

			if ((0 == (padapter->registrypriv.cbw40_enable & BIT(1))) &&
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

static u8 init_channel_set(struct rtw_adapter* padapter, u8 ChannelPlan,
			   struct rt_channel_info *channel_set)
{
	u8	index, chanset_size = 0;
	u8	b5GBand = false, b2_4GBand = false;
	u8	Index2G = 0, Index5G = 0;

	memset(channel_set, 0, sizeof(struct rt_channel_info)*MAX_CHANNEL_NUM);

	if (ChannelPlan >= RT_CHANNEL_DOMAIN_MAX &&
	    ChannelPlan != RT_CHANNEL_DOMAIN_REALTEK_DEFINE) {
		DBG_8723A("ChannelPlan ID %x error !!!!!\n", ChannelPlan);
		return chanset_size;
	}

	if (padapter->registrypriv.wireless_mode & WIRELESS_11G) {
		b2_4GBand = true;
		if (RT_CHANNEL_DOMAIN_REALTEK_DEFINE == ChannelPlan)
			Index2G = RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE.Index2G;
		else
			Index2G = RTW_ChannelPlanMap[ChannelPlan].Index2G;
	}

	if (padapter->registrypriv.wireless_mode & WIRELESS_11A) {
		b5GBand = true;
		if (RT_CHANNEL_DOMAIN_REALTEK_DEFINE == ChannelPlan)
			Index5G = RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE.Index5G;
		else
			Index5G = RTW_ChannelPlanMap[ChannelPlan].Index5G;
	}

	if (b2_4GBand) {
		for (index = 0; index<RTW_ChannelPlan2G[Index2G].Len; index++) {
			channel_set[chanset_size].ChannelNum =
				RTW_ChannelPlan2G[Index2G].Channel[index];

			if ((RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN == ChannelPlan) ||
			    /* Channel 1~11 is active, and 12~14 is passive */
			    (RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN_2G == ChannelPlan)){
				if (channel_set[chanset_size].ChannelNum >= 1 &&
				    channel_set[chanset_size].ChannelNum <= 11)
					channel_set[chanset_size].ScanType =
						SCAN_ACTIVE;
				else if ((channel_set[chanset_size].ChannelNum >= 12 &&
					  channel_set[chanset_size].ChannelNum  <= 14))
					channel_set[chanset_size].ScanType =
						SCAN_PASSIVE;
			} else if (RT_CHANNEL_DOMAIN_WORLD_WIDE_13 ==
				   ChannelPlan ||
				   RT_CHANNEL_DOMAIN_WORLD_WIDE_5G ==
				   ChannelPlan ||
				   RT_CHANNEL_DOMAIN_2G_WORLD == Index2G) {
				/*  channel 12~13, passive scan */
				if (channel_set[chanset_size].ChannelNum <= 11)
					channel_set[chanset_size].ScanType =
						SCAN_ACTIVE;
				else
					channel_set[chanset_size].ScanType =
						SCAN_PASSIVE;
			} else
				channel_set[chanset_size].ScanType =
					SCAN_ACTIVE;

			chanset_size++;
		}
	}

	if (b5GBand) {
		for (index = 0;index<RTW_ChannelPlan5G[Index5G].Len;index++) {
			if (RTW_ChannelPlan5G[Index5G].Channel[index] <= 48 ||
			    RTW_ChannelPlan5G[Index5G].Channel[index] >= 149) {
				channel_set[chanset_size].ChannelNum =
					RTW_ChannelPlan5G[Index5G].Channel[index];
				if (RT_CHANNEL_DOMAIN_WORLD_WIDE_5G ==
				    ChannelPlan) {
					/* passive scan for all 5G channels */
					channel_set[chanset_size].ScanType =
						SCAN_PASSIVE;
				} else
					channel_set[chanset_size].ScanType =
						SCAN_ACTIVE;
				DBG_8723A("%s(): channel_set[%d].ChannelNum = "
					  "%d\n", __func__, chanset_size,
					  channel_set[chanset_size].ChannelNum);
				chanset_size++;
			}
		}
	}

	return chanset_size;
}

int init_mlme_ext_priv23a(struct rtw_adapter* padapter)
{
	int	res = _SUCCESS;
	struct registry_priv* pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	pmlmeext->padapter = padapter;

	init_mlme_ext_priv23a_value(padapter);
	pmlmeinfo->bAcceptAddbaReq = pregistrypriv->bAcceptAddbaReq;

	init_mlme_ext_timer23a(padapter);

#ifdef CONFIG_8723AU_AP_MODE
	init_mlme_ap_info23a(padapter);
#endif

	pmlmeext->max_chan_nums = init_channel_set(padapter,
						   pmlmepriv->ChannelPlan,
						   pmlmeext->channel_set);
	init_channel_list(padapter, pmlmeext->channel_set,
			  pmlmeext->max_chan_nums, &pmlmeext->channel_list);

	pmlmeext->chan_scan_time = SURVEY_TO;
	pmlmeext->mlmeext_init = true;

	pmlmeext->active_keep_alive_check = true;
	return res;
}

void free_mlme_ext_priv23a (struct mlme_ext_priv *pmlmeext)
{
	struct rtw_adapter *padapter = pmlmeext->padapter;

	if (!padapter)
		return;

	if (padapter->bDriverStopped == true) {
		del_timer_sync(&pmlmeext->survey_timer);
		del_timer_sync(&pmlmeext->link_timer);
		/* del_timer_sync(&pmlmeext->ADDBA_timer); */
	}
}

static void
_mgt_dispatcher23a(struct rtw_adapter *padapter, struct mlme_handler *ptable,
		   struct recv_frame *precv_frame)
{
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;

	if (ptable->func) {
		/* receive the frames that ra(a1) is my address
		   or ra(a1) is bc address. */
		if (!ether_addr_equal(hdr->addr1, myid(&padapter->eeprompriv))&&
		    !is_broadcast_ether_addr(hdr->addr1))
			return;

		ptable->func(padapter, precv_frame);
        }
}

void mgt_dispatcher23a(struct rtw_adapter *padapter,
		    struct recv_frame *precv_frame)
{
	int index;
	struct mlme_handler *ptable;
#ifdef CONFIG_8723AU_AP_MODE
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
#endif /* CONFIG_8723AU_AP_MODE */
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u16 stype;
	struct sta_info *psta;

	if (!ieee80211_is_mgmt(hdr->frame_control))
		return;

	/* receive the frames that ra(a1) is my address or ra(a1) is
	   bc address. */
	if (!ether_addr_equal(hdr->addr1, myid(&padapter->eeprompriv)) &&
	    !is_broadcast_ether_addr(hdr->addr1))
		return;

	ptable = mlme_sta_tbl;

	stype = le16_to_cpu(hdr->frame_control) & IEEE80211_FCTL_STYPE;
	index = stype >> 4;

	if (index > 13) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("Currently we do not support reserved sub-fr-type ="
			  "%d\n", index));
		return;
	}
	ptable += index;

	psta = rtw_get_stainfo23a(&padapter->stapriv, hdr->addr2);

	if (psta) {
		if (ieee80211_has_retry(hdr->frame_control)) {
			if (precv_frame->attrib.seq_num ==
			    psta->RxMgmtFrameSeqNum) {
				/* drop the duplicate management frame */
				DBG_8723A("Drop duplicate management frame "
					  "with seq_num = %d.\n",
					  precv_frame->attrib.seq_num);
				return;
			}
		}
		psta->RxMgmtFrameSeqNum = precv_frame->attrib.seq_num;
	}

#ifdef CONFIG_8723AU_AP_MODE
	switch (stype)
	{
	case IEEE80211_STYPE_AUTH:
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true)
			ptable->func = &OnAuth23a;
		else
			ptable->func = &OnAuth23aClient23a;
		/* pass through */
	case IEEE80211_STYPE_ASSOC_REQ:
	case IEEE80211_STYPE_REASSOC_REQ:
		_mgt_dispatcher23a(padapter, ptable, precv_frame);
		break;
	case IEEE80211_STYPE_PROBE_REQ:
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true)
			_mgt_dispatcher23a(padapter, ptable, precv_frame);
		else
			_mgt_dispatcher23a(padapter, ptable, precv_frame);
		break;
	case IEEE80211_STYPE_BEACON:
		_mgt_dispatcher23a(padapter, ptable, precv_frame);
		break;
	case IEEE80211_STYPE_ACTION:
		/* if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) */
		_mgt_dispatcher23a(padapter, ptable, precv_frame);
		break;
	default:
		_mgt_dispatcher23a(padapter, ptable, precv_frame);
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true)
			rtw_hostapd_mlme_rx23a(padapter, precv_frame);
		break;
	}
#else
	_mgt_dispatcher23a(padapter, ptable, precv_frame);
#endif
}

#ifdef CONFIG_8723AU_P2P
static u32 p2p_listen_state_process(struct rtw_adapter *padapter,
				    unsigned char *da)
{
	bool response = true;

	if (wdev_to_priv(padapter->rtw_wdev)->p2p_enabled == false ||
	    padapter->mlmepriv.wps_probe_resp_ie == NULL ||
	    padapter->mlmepriv.p2p_probe_resp_ie == NULL) {
		DBG_8723A("DON'T issue_probersp23a_p2p23a: p2p_enabled:%d, "
			  "wps_probe_resp_ie:%p, p2p_probe_resp_ie:%p\n",
			  wdev_to_priv(padapter->rtw_wdev)->p2p_enabled,
			  padapter->mlmepriv.wps_probe_resp_ie,
			  padapter->mlmepriv.p2p_probe_resp_ie);
		response = false;
	}

	if (response == true)
		issue_probersp23a_p2p23a(padapter, da);

	return _SUCCESS;
}
#endif /* CONFIG_8723AU_P2P */

/****************************************************************************

Following are the callback functions for each subtype of the management frames

*****************************************************************************/

unsigned int OnProbeReq23a(struct rtw_adapter *padapter,
			   struct recv_frame *precv_frame)
{
	unsigned int	ielen;
	unsigned char	*p;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *cur = &pmlmeinfo->network;
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u8 *pframe = skb->data;
	uint len = skb->len;
	u8 is_valid_p2p_probereq = false;

#ifdef CONFIG_8723AU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	u8 wifi_test_chk_rate = 1;

	if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) &&
	    !rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE) &&
	    !rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT) &&
	    !rtw_p2p_chk_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH) &&
	    !rtw_p2p_chk_state(pwdinfo, P2P_STATE_SCAN)) {
		/*	mcs_rate = 0 -> CCK 1M rate */
		/*	mcs_rate = 1 -> CCK 2M rate */
		/*	mcs_rate = 2 -> CCK 5.5M rate */
		/*	mcs_rate = 3 -> CCK 11M rate */
		/*	In the P2P mode, the driver should not support
			the CCK rate */

		/*	IOT issue: Google Nexus7 use 1M rate to send
			p2p_probe_req after GO nego completed and Nexus7
			is client */
		if (wifi_test_chk_rate == 1) {
			if ((is_valid_p2p_probereq =
			     process_probe_req_p2p_ie23a(pwdinfo, pframe,
							 len)) == true) {
				if (rtw_p2p_chk_role(pwdinfo,
						     P2P_ROLE_DEVICE)) {
					u8 *sa = ieee80211_get_SA(hdr);
					p2p_listen_state_process(padapter, sa);
					return _SUCCESS;
				}

				if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
					goto _continue;
				}
			}
		}
	}

_continue:
#endif /* CONFIG_8723AU_P2P */

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		return _SUCCESS;
	}

	if (check_fwstate(pmlmepriv, _FW_LINKED) == false &&
		check_fwstate(pmlmepriv,
			      WIFI_ADHOC_MASTER_STATE|WIFI_AP_STATE) == false) {
		return _SUCCESS;
	}

	p = rtw_get_ie23a(pframe + sizeof(struct ieee80211_hdr_3addr) +
			  _PROBEREQ_IE_OFFSET_, _SSID_IE_, (int *)&ielen,
			  len - sizeof(struct ieee80211_hdr_3addr) -
			  _PROBEREQ_IE_OFFSET_);

	/* check (wildcard) SSID */
	if (p) {
		if (is_valid_p2p_probereq == true) {
			goto _issue_probersp23a;
		}

		if ((ielen != 0 &&
		     memcmp((void *)(p+2), cur->Ssid.ssid,
			    cur->Ssid.ssid_len)) ||
		    (ielen == 0 && pmlmeinfo->hidden_ssid_mode)) {
			return _SUCCESS;
		}

_issue_probersp23a:

		if (check_fwstate(pmlmepriv, _FW_LINKED) == true &&
		    pmlmepriv->cur_network.join_res == true) {
			/* DBG_8723A("+issue_probersp23a during ap mode\n"); */
			issue_probersp23a(padapter, ieee80211_get_SA(hdr),
					  is_valid_p2p_probereq);
		}
	}

	return _SUCCESS;
}

unsigned int OnProbeRsp23a(struct rtw_adapter *padapter,
			   struct recv_frame *precv_frame)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
#ifdef CONFIG_8723AU_P2P
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct wifidirect_info	*pwdinfo = &padapter->wdinfo;
#endif

#ifdef CONFIG_8723AU_P2P
	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_PROVISION_DIS_REQ))	{
		if (pwdinfo->tx_prov_disc_info.benable == true) {
			if (ether_addr_equal(pwdinfo->tx_prov_disc_info.peerIFAddr,
				    hdr->addr2)) {
				if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT)) {
					pwdinfo->tx_prov_disc_info.benable = false;
					issue_p2p_provision_request23a(padapter,
								    				pwdinfo->tx_prov_disc_info.ssid.ssid,
												pwdinfo->tx_prov_disc_info.ssid.ssid_len,
												pwdinfo->tx_prov_disc_info.peerDevAddr);
				}
				else if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE) || rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
				{
					pwdinfo->tx_prov_disc_info.benable = false;
					issue_p2p_provision_request23a(padapter,
												NULL,
												0,
												pwdinfo->tx_prov_disc_info.peerDevAddr);
				}
			}
		}
		return _SUCCESS;
	} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING)) {
		if (pwdinfo->nego_req_info.benable == true) {
			DBG_8723A("[%s] P2P State is GONEGO ING!\n", __func__);
			if (ether_addr_equal(pwdinfo->nego_req_info.peerDevAddr,
					     hdr->addr2)) {
				pwdinfo->nego_req_info.benable = false;
				issue_p2p_GO_request23a(padapter, pwdinfo->nego_req_info.peerDevAddr);
			}
		}
	} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_INVITE_REQ)) {
		if (pwdinfo->invitereq_info.benable == true) {
			DBG_8723A("[%s] P2P_STATE_TX_INVITE_REQ!\n", __func__);
			if (ether_addr_equal(
				    pwdinfo->invitereq_info.peer_macaddr,
				    hdr->addr2)) {
				pwdinfo->invitereq_info.benable = false;
				issue_p2p_invitation_request23a(padapter, pwdinfo->invitereq_info.peer_macaddr);
			}
		}
	}
#endif

	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS) {
		report_survey_event23a(padapter, precv_frame);
		return _SUCCESS;
	}

	return _SUCCESS;
}

unsigned int OnBeacon23a(struct rtw_adapter *padapter,
			 struct recv_frame *precv_frame)
{
	int cam_idx;
	struct sta_info	*psta;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_priv	*pstapriv = &padapter->stapriv;
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u8 *pframe = skb->data;
	uint len = skb->len;
	struct wlan_bssid_ex *pbss;
	int ret = _SUCCESS;
	u8 *p = NULL;
	u32 ielen = 0;

	p = rtw_get_ie23a(pframe + sizeof(struct ieee80211_hdr_3addr) +
			  _BEACON_IE_OFFSET_, _EXT_SUPPORTEDRATES_IE_, &ielen,
			  len - sizeof(struct ieee80211_hdr_3addr) -
			  _BEACON_IE_OFFSET_);
	if ((p != NULL) && (ielen > 0)) {
		if ((*(p + 1 + ielen) == 0x2D) && (*(p + 2 + ielen) != 0x2D)) {
			/* Invalid value 0x2D is detected in Extended Supported
			 * Rates (ESR) IE. Try to fix the IE length to avoid
			 * failed Beacon parsing.
			 */
			DBG_8723A("[WIFIDBG] Error in ESR IE is detected in "
				  "Beacon of BSSID: %pM. Fix the length of "
				  "ESR IE to avoid failed Beacon parsing.\n",
				  hdr->addr3);
			*(p + 1) = ielen - 1;
		}
	}

	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS) {
		report_survey_event23a(padapter, precv_frame);
		return _SUCCESS;
	}

	if (ether_addr_equal(hdr->addr3, get_my_bssid23a(&pmlmeinfo->network))){
		if (pmlmeinfo->state & WIFI_FW_AUTH_NULL) {
			/* we should update current network before auth,
			   or some IE is wrong */
			pbss = (struct wlan_bssid_ex *)
				kmalloc(sizeof(struct wlan_bssid_ex),
					GFP_ATOMIC);
			if (pbss) {
				if (collect_bss_info23a(padapter, precv_frame,
							pbss) == _SUCCESS) {
					update_network23a(&pmlmepriv->cur_network.network, pbss, padapter, true);
					rtw_get_bcn_info23a(&pmlmepriv->cur_network);
				}
				kfree(pbss);
			}

			/* check the vendor of the assoc AP */
			pmlmeinfo->assoc_AP_vendor = check_assoc_AP23a(pframe + sizeof(struct ieee80211_hdr_3addr), len-sizeof(struct ieee80211_hdr_3addr));

			/* update TSF Value */
			update_TSF23a(pmlmeext, pframe, len);

			/* start auth */
			start_clnt_auth23a(padapter);

			return _SUCCESS;
		}

		if (((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE) &&
		    (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)) {
			psta = rtw_get_stainfo23a(pstapriv, hdr->addr2);
			if (psta) {
				ret = rtw_check_bcn_info23a(padapter, pframe,
							    len);
				if (!ret) {
					DBG_8723A_LEVEL(_drv_always_,
							"ap has changed, "
							"disconnect now\n");
					receive_disconnect23a(padapter, pmlmeinfo->network.MacAddress, 65535);
					return _SUCCESS;
				}
				/* update WMM, ERP in the beacon */
				/* todo: the timer is used instead of
				   the number of the beacon received */
				if ((sta_rx_pkts(psta) & 0xf) == 0) {
					/* DBG_8723A("update_bcn_info\n"); */
					update_beacon23a_info(padapter, pframe,
							      len, psta);
				}

#ifdef CONFIG_8723AU_P2P
				process_p2p_ps_ie23a(padapter, (pframe + sizeof(struct ieee80211_hdr_3addr)), (len - sizeof(struct ieee80211_hdr_3addr)));
#endif /* CONFIG_8723AU_P2P */
			}
		} else if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) {
			psta = rtw_get_stainfo23a(pstapriv, hdr->addr2);
			if (psta) {
				/* update WMM, ERP in the beacon */
				/* todo: the timer is used instead of the
				   number of the beacon received */
				if ((sta_rx_pkts(psta) & 0xf) == 0) {
					/* DBG_8723A("update_bcn_info\n"); */
					update_beacon23a_info(padapter, pframe,
							      len, psta);
				}
			} else {
				/* allocate a new CAM entry for IBSS station */
				cam_idx = allocate_fw_sta_entry23a(padapter);
				if (cam_idx == NUM_STA)
					goto _END_ONBEACON_;

				/* get supported rate */
				if (update_sta_support_rate23a(padapter, (pframe + sizeof(struct ieee80211_hdr_3addr) + _BEACON_IE_OFFSET_), (len - sizeof(struct ieee80211_hdr_3addr) - _BEACON_IE_OFFSET_), cam_idx) == _FAIL) {
					pmlmeinfo->FW_sta_info[cam_idx].status = 0;
					goto _END_ONBEACON_;
				}

				/* update TSF Value */
				update_TSF23a(pmlmeext, pframe, len);

				/* report sta add event */
				report_add_sta_event23a(padapter, hdr->addr2,
							cam_idx);
			}
		}
	}

_END_ONBEACON_:

	return _SUCCESS;
}

unsigned int OnAuth23a(struct rtw_adapter *padapter,
		       struct recv_frame *precv_frame)
{
#ifdef CONFIG_8723AU_AP_MODE
	unsigned int	auth_mode, seq, ie_len;
	unsigned char	*sa, *p;
	u16	algorithm;
	int	status;
	static struct sta_info stat;
	struct	sta_info	*pstat = NULL;
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u8 *pframe = skb->data;
	uint len = skb->len;

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return _FAIL;

	DBG_8723A("+OnAuth23a\n");

	sa = hdr->addr2;

	auth_mode = psecuritypriv->dot11AuthAlgrthm;
	seq = cpu_to_le16(*(u16*)((unsigned long)pframe +
				  sizeof(struct ieee80211_hdr_3addr) + 2));
	algorithm = cpu_to_le16(*(u16*)((unsigned long)pframe +
					sizeof(struct ieee80211_hdr_3addr)));

	DBG_8723A("auth alg =%x, seq =%X\n", algorithm, seq);

	if (auth_mode == 2 &&
	    psecuritypriv->dot11PrivacyAlgrthm != _WEP40_ &&
	    psecuritypriv->dot11PrivacyAlgrthm != _WEP104_)
		auth_mode = 0;

	/*  rx a shared-key auth but shared not enabled, or */
	/*  rx a open-system auth but shared-key is enabled */
	if ((algorithm > 0 && auth_mode == 0) ||
	    (algorithm == 0 && auth_mode == 1)) {
		DBG_8723A("auth rejected due to bad alg [alg =%d, auth_mib "
			  "=%d] %02X%02X%02X%02X%02X%02X\n",
			  algorithm, auth_mode,
			  sa[0], sa[1], sa[2], sa[3], sa[4], sa[5]);

		status = WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG;

		goto auth_fail;
	}

	if (rtw_access_ctrl23a(padapter, sa) == false) {
		status = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;
		goto auth_fail;
	}

	pstat = rtw_get_stainfo23a(pstapriv, sa);
	if (!pstat) {
		/*  allocate a new one */
		DBG_8723A("going to alloc stainfo for sa ="MAC_FMT"\n",
			  MAC_ARG(sa));
		pstat = rtw_alloc_stainfo23a(pstapriv, sa);
		if (!pstat) {
			DBG_8723A(" Exceed the upper limit of supported "
				  "clients...\n");
			status = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;
			goto auth_fail;
		}

		pstat->state = WIFI_FW_AUTH_NULL;
		pstat->auth_seq = 0;

		/* pstat->flags = 0; */
		/* pstat->capability = 0; */
	} else {
		spin_lock_bh(&pstapriv->asoc_list_lock);
		if (!list_empty(&pstat->asoc_list)) {
			list_del_init(&pstat->asoc_list);
			pstapriv->asoc_list_cnt--;
			if (pstat->expire_to > 0)
			{
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
		DBG_8723A("(1)auth rejected because out of seq [rx_seq =%d, "
			  "exp_seq =%d]!\n", seq, pstat->auth_seq+1);
		status = WLAN_STATUS_UNKNOWN_AUTH_TRANSACTION;
		goto auth_fail;
	}

	if (algorithm == 0 && (auth_mode == 0 || auth_mode == 2)) {
		if (seq == 1) {
			pstat->state &= ~WIFI_FW_AUTH_NULL;
			pstat->state |= WIFI_FW_AUTH_SUCCESS;
			pstat->expire_to = pstapriv->assoc_to;
			pstat->authalg = algorithm;
		} else {
			DBG_8723A("(2)auth rejected because out of seq "
				  "[rx_seq =%d, exp_seq =%d]!\n",
				  seq, pstat->auth_seq+1);
			status = WLAN_STATUS_UNKNOWN_AUTH_TRANSACTION;
			goto auth_fail;
		}
	} else { /*  shared system or auto authentication */
		if (seq == 1) {
			/* prepare for the challenging txt... */
			pstat->state &= ~WIFI_FW_AUTH_NULL;
			pstat->state |= WIFI_FW_AUTH_STATE;
			pstat->authalg = algorithm;
			pstat->auth_seq = 2;
		} else if (seq == 3) {
			/* checking for challenging txt... */
			DBG_8723A("checking for challenging txt...\n");

			p = rtw_get_ie23a(pframe +
					  sizeof(struct ieee80211_hdr_3addr) +
					  4 + _AUTH_IE_OFFSET_, _CHLGETXT_IE_,
					  (int *)&ie_len, len -
					  sizeof(struct ieee80211_hdr_3addr) -
					  _AUTH_IE_OFFSET_ - 4);

			if ((p == NULL) || (ie_len<= 0)) {
				DBG_8723A("auth rejected because challenge "
					  "failure!(1)\n");
				status = WLAN_STATUS_CHALLENGE_FAIL;
				goto auth_fail;
			}

			if (!memcmp((void *)(p + 2), pstat->chg_txt, 128)) {
				pstat->state &= (~WIFI_FW_AUTH_STATE);
				pstat->state |= WIFI_FW_AUTH_SUCCESS;
				/*  challenging txt is correct... */
				pstat->expire_to =  pstapriv->assoc_to;
			} else {
				DBG_8723A("auth rejected because challenge "
					  "failure!\n");
				status = WLAN_STATUS_CHALLENGE_FAIL;
				goto auth_fail;
			}
		} else {
			DBG_8723A("(3)auth rejected because out of seq "
				  "[rx_seq =%d, exp_seq =%d]!\n",
				  seq, pstat->auth_seq+1);
			status = WLAN_STATUS_UNKNOWN_AUTH_TRANSACTION;
			goto auth_fail;
		}
	}

	/*  Now, we are going to issue_auth23a... */
	pstat->auth_seq = seq + 1;

	issue_auth23a(padapter, pstat, (unsigned short)WLAN_STATUS_SUCCESS);

	if (pstat->state & WIFI_FW_AUTH_SUCCESS)
		pstat->auth_seq = 0;

	return _SUCCESS;

auth_fail:

	if (pstat)
		rtw_free_stainfo23a(padapter, pstat);

	pstat = &stat;
	memset((char *)pstat, '\0', sizeof(stat));
	pstat->auth_seq = 2;
	memcpy(pstat->hwaddr, sa, 6);

	issue_auth23a(padapter, pstat, (unsigned short)status);

#endif
	return _FAIL;
}

unsigned int OnAuth23aClient23a(struct rtw_adapter *padapter,
				struct recv_frame *precv_frame)
{
	unsigned int	seq, len, status, algthm, offset;
	unsigned char	*p;
	unsigned int	go2asoc = 0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u8 *pframe = skb->data;
	uint pkt_len = skb->len;

	DBG_8723A("%s\n", __func__);

	/* check A1 matches or not */
	if (!ether_addr_equal(myid(&padapter->eeprompriv),
			      ieee80211_get_DA(hdr)))
		return _SUCCESS;

	if (!(pmlmeinfo->state & WIFI_FW_AUTH_STATE))
		return _SUCCESS;

	offset = ieee80211_has_protected(hdr->frame_control) ? 4: 0;

	algthm	= le16_to_cpu(*(unsigned short *)((unsigned long)pframe + sizeof(struct ieee80211_hdr_3addr) + offset));
	seq	= le16_to_cpu(*(unsigned short *)((unsigned long)pframe + sizeof(struct ieee80211_hdr_3addr) + offset + 2));
	status	= le16_to_cpu(*(unsigned short *)((unsigned long)pframe + sizeof(struct ieee80211_hdr_3addr) + offset + 4));

	if (status != 0)
	{
		DBG_8723A("clnt auth fail, status: %d\n", status);
		if (status == 13)/*  pmlmeinfo->auth_algo == dot11AuthAlgrthm_Auto) */
		{
			if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared)
				pmlmeinfo->auth_algo = dot11AuthAlgrthm_Open;
			else
				pmlmeinfo->auth_algo = dot11AuthAlgrthm_Shared;
			/* pmlmeinfo->reauth_count = 0; */
		}

		set_link_timer(pmlmeext, 1);
		goto authclnt_fail;
	}

	if (seq == 2)
	{
		if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared)
		{
			 /*  legendary shared system */
			p = rtw_get_ie23a(pframe + sizeof(struct ieee80211_hdr_3addr) + _AUTH_IE_OFFSET_, _CHLGETXT_IE_, (int *)&len,
				pkt_len - sizeof(struct ieee80211_hdr_3addr) - _AUTH_IE_OFFSET_);

			if (p == NULL)
			{
				/* DBG_8723A("marc: no challenge text?\n"); */
				goto authclnt_fail;
			}

			memcpy((void *)(pmlmeinfo->chg_txt), (void *)(p + 2), len);
			pmlmeinfo->auth_seq = 3;
			issue_auth23a(padapter, NULL, 0);
			set_link_timer(pmlmeext, REAUTH_TO);

			return _SUCCESS;
		}
		else
		{
			/*  open system */
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
		/*  this is also illegal */
		/* DBG_8723A("marc: clnt auth failed due to illegal seq =%x\n", seq); */
		goto authclnt_fail;
	}

	if (go2asoc)
	{
		DBG_8723A_LEVEL(_drv_always_, "auth success, start assoc\n");
		start_clnt_assoc23a(padapter);
		return _SUCCESS;
	}

authclnt_fail:

	/* pmlmeinfo->state &= ~(WIFI_FW_AUTH_STATE); */

	return _FAIL;
}

unsigned int OnAssocReq23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame)
{
#ifdef CONFIG_8723AU_AP_MODE
	u16 capab_info, listen_interval;
	struct rtw_ieee802_11_elems elems;
	struct sta_info	*pstat;
	unsigned char		reassoc, *p, *pos, *wpa_ie;
	unsigned char WMM_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01};
	int		i, ie_len, wpa_ie_len, left;
	unsigned char		supportRate[16];
	int					supportRateNum;
	unsigned short		status = WLAN_STATUS_SUCCESS;
	unsigned short ie_offset;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *cur = &pmlmeinfo->network;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sk_buff *skb = precv_frame->pkt;
	u8 *pframe = skb->data;
	uint pkt_len = skb->len;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u16 frame_control;
#ifdef CONFIG_8723AU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	u8 p2p_status_code = P2P_STATUS_SUCCESS;
	u8 *p2pie;
	u32 p2pielen = 0;
	u8	wfd_ie[MAX_WFD_IE_LEN] = { 0x00 };
	u32	wfd_ielen = 0;
#endif /* CONFIG_8723AU_P2P */

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return _FAIL;

	frame_control = hdr->frame_control;
	if (ieee80211_is_assoc_req(frame_control)) {
		reassoc = 0;
		ie_offset = _ASOCREQ_IE_OFFSET_;
	} else { /*  WIFI_REASSOCREQ */
		reassoc = 1;
		ie_offset = _REASOCREQ_IE_OFFSET_;
	}

	if (pkt_len < sizeof(struct ieee80211_hdr_3addr) + ie_offset) {
		DBG_8723A("handle_assoc(reassoc =%d) - too short payload (len =%lu)"
		       "\n", reassoc, (unsigned long)pkt_len);
		return _FAIL;
	}

	pstat = rtw_get_stainfo23a(pstapriv, hdr->addr2);
	if (!pstat) {
		status = WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA;
		goto asoc_class2_error;
	}

	capab_info = get_unaligned_le16(pframe + sizeof(struct ieee80211_hdr_3addr));
	/* capab_info = le16_to_cpu(*(unsigned short *)(pframe + sizeof(struct ieee80211_hdr_3addr))); */
	/* listen_interval = le16_to_cpu(*(unsigned short *)(pframe + sizeof(struct ieee80211_hdr_3addr)+2)); */
	listen_interval = get_unaligned_le16(pframe + sizeof(struct ieee80211_hdr_3addr)+2);

	left = pkt_len - (sizeof(struct ieee80211_hdr_3addr) + ie_offset);
	pos = pframe + (sizeof(struct ieee80211_hdr_3addr) + ie_offset);

	DBG_8723A("%s\n", __func__);

	/*  check if this stat has been successfully authenticated/assocated */
	if (!((pstat->state) & WIFI_FW_AUTH_SUCCESS))
	{
		if (!((pstat->state) & WIFI_FW_ASSOC_SUCCESS))
		{
			status = WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA;
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

	pstat->capability = capab_info;

	/* now parse all ieee802_11 ie to point to elems */
	if (rtw_ieee802_11_parse_elems23a(pos, left, &elems, 1) == ParseFailed ||
	    !elems.ssid) {
		DBG_8723A("STA " MAC_FMT " sent invalid association request\n",
		       MAC_ARG(pstat->hwaddr));
		status = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto OnAssocReq23aFail;
	}

	/*  now we should check all the fields... */
	/*  checking SSID */
	p = rtw_get_ie23a(pframe + sizeof(struct ieee80211_hdr_3addr) + ie_offset, _SSID_IE_, &ie_len,
		pkt_len - sizeof(struct ieee80211_hdr_3addr) - ie_offset);
	if (p == NULL)
	{
		status = WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	if (ie_len == 0) /*  broadcast ssid, however it is not allowed in assocreq */
		status = WLAN_STATUS_UNSPECIFIED_FAILURE;
	else {
		/*  check if ssid match */
		if (memcmp((void *)(p+2), cur->Ssid.ssid, cur->Ssid.ssid_len))
			status = WLAN_STATUS_UNSPECIFIED_FAILURE;

		if (ie_len != cur->Ssid.ssid_len)
			status = WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	if (WLAN_STATUS_SUCCESS != status)
		goto OnAssocReq23aFail;

	/*  check if the supported rate is ok */
	p = rtw_get_ie23a(pframe + sizeof(struct ieee80211_hdr_3addr) + ie_offset, _SUPPORTEDRATES_IE_, &ie_len, pkt_len - sizeof(struct ieee80211_hdr_3addr) - ie_offset);
	if (p == NULL) {
		DBG_8723A("Rx a sta assoc-req which supported rate is empty!\n");
		/*  use our own rate set as statoin used */
		/* memcpy(supportRate, AP_BSSRATE, AP_BSSRATE_LEN); */
		/* supportRateNum = AP_BSSRATE_LEN; */

		status = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto OnAssocReq23aFail;
	} else {
		memcpy(supportRate, p+2, ie_len);
		supportRateNum = ie_len;

		p = rtw_get_ie23a(pframe + sizeof(struct ieee80211_hdr_3addr) + ie_offset, _EXT_SUPPORTEDRATES_IE_, &ie_len,
				pkt_len - sizeof(struct ieee80211_hdr_3addr) - ie_offset);
		if (p !=  NULL) {

			if (supportRateNum<= sizeof(supportRate))
			{
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
	Update23aTblForSoftAP(pstat->bssrateset, pstat->bssratelen);

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

		if (rtw_parse_wpa2_ie23a(wpa_ie-2, wpa_ie_len+2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			pstat->dot8021xalg = 1;/* psk,  todo:802.1x */
			pstat->wpa_psk |= BIT(1);

			pstat->wpa2_group_cipher = group_cipher&psecuritypriv->wpa2_group_cipher;
			pstat->wpa2_pairwise_cipher = pairwise_cipher&psecuritypriv->wpa2_pairwise_cipher;

			if (!pstat->wpa2_group_cipher)
				status = WLAN_REASON_INVALID_GROUP_CIPHER;

			if (!pstat->wpa2_pairwise_cipher)
				status = WLAN_REASON_INVALID_PAIRWISE_CIPHER;
		} else {
			status = WLAN_STATUS_INVALID_IE;
		}

	} else if ((psecuritypriv->wpa_psk & BIT(0)) && elems.wpa_ie) {

		int group_cipher = 0, pairwise_cipher = 0;

		wpa_ie = elems.wpa_ie;
		wpa_ie_len = elems.wpa_ie_len;

		if (rtw_parse_wpa_ie23a(wpa_ie-2, wpa_ie_len+2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			pstat->dot8021xalg = 1;/* psk,  todo:802.1x */
			pstat->wpa_psk |= BIT(0);

			pstat->wpa_group_cipher = group_cipher&psecuritypriv->wpa_group_cipher;
			pstat->wpa_pairwise_cipher = pairwise_cipher&psecuritypriv->wpa_pairwise_cipher;

			if (!pstat->wpa_group_cipher)
				status = WLAN_STATUS_INVALID_GROUP_CIPHER;

			if (!pstat->wpa_pairwise_cipher)
				status = WLAN_STATUS_INVALID_PAIRWISE_CIPHER;

		} else {
			status = WLAN_STATUS_INVALID_IE;
		}

	} else {
		wpa_ie = NULL;
		wpa_ie_len = 0;
	}

	if (WLAN_STATUS_SUCCESS != status)
		goto OnAssocReq23aFail;

	pstat->flags &= ~(WLAN_STA_WPS | WLAN_STA_MAYBE_WPS);
	if (wpa_ie == NULL) {
		if (elems.wps_ie) {
			DBG_8723A("STA included WPS IE in "
				   "(Re)Association Request - assume WPS is "
				   "used\n");
			pstat->flags |= WLAN_STA_WPS;
		} else {
			DBG_8723A("STA did not include WPA/RSN IE "
				   "in (Re)Association Request - possible WPS "
				   "use\n");
			pstat->flags |= WLAN_STA_MAYBE_WPS;
		}

		/*  AP support WPA/RSN, and sta is going to do WPS, but AP is not ready */
		/*  that the selected registrar of AP is _FLASE */
		if ((psecuritypriv->wpa_psk > 0) &&
		    (pstat->flags & (WLAN_STA_WPS|WLAN_STA_MAYBE_WPS))) {
			if (pmlmepriv->wps_beacon_ie) {
				u8 selected_registrar = 0;

				rtw_get_wps_attr_content23a(pmlmepriv->wps_beacon_ie, pmlmepriv->wps_beacon_ie_len,
							 WPS_ATTR_SELECTED_REGISTRAR, &selected_registrar, NULL);

				if (!selected_registrar) {
					DBG_8723A("selected_registrar is false , or AP is not ready to do WPS\n");

					status = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;

					goto OnAssocReq23aFail;
				}
			}
		}
	} else {
		int copy_len;

		if (psecuritypriv->wpa_psk == 0) {
			DBG_8723A("STA " MAC_FMT ": WPA/RSN IE in association "
			"request, but AP don't support WPA/RSN\n", MAC_ARG(pstat->hwaddr));

			status = WLAN_STATUS_INVALID_IE;

			goto OnAssocReq23aFail;
		}

		if (elems.wps_ie) {
			DBG_8723A("STA included WPS IE in "
				   "(Re)Association Request - WPS is "
				   "used\n");
			pstat->flags |= WLAN_STA_WPS;
			copy_len = 0;
		} else {
			copy_len = ((wpa_ie_len+2) > sizeof(pstat->wpa_ie)) ? (sizeof(pstat->wpa_ie)):(wpa_ie_len+2);
		}

		if (copy_len>0)
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
	if (pmlmepriv->qospriv.qos_option)
	{
		p = pframe + sizeof(struct ieee80211_hdr_3addr) + ie_offset; ie_len = 0;
		for (;;)
		{
			p = rtw_get_ie23a(p, _VENDOR_SPECIFIC_IE_, &ie_len, pkt_len - sizeof(struct ieee80211_hdr_3addr) - ie_offset);
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

					if (pstat->qos_info&0xf)
					{
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
			}
			else {
				break;
			}
			p = p + ie_len + 2;
		}
	}

	/* save HT capabilities in the sta object */
	memset(&pstat->htpriv.ht_cap, 0, sizeof(struct ieee80211_ht_cap));
	if (elems.ht_capabilities && elems.ht_capabilities_len >= sizeof(struct ieee80211_ht_cap))
	{
		pstat->flags |= WLAN_STA_HT;

		pstat->flags |= WLAN_STA_WME;

		memcpy(&pstat->htpriv.ht_cap, elems.ht_capabilities, sizeof(struct ieee80211_ht_cap));

	} else
		pstat->flags &= ~WLAN_STA_HT;

	if ((pmlmepriv->htpriv.ht_option == false) && (pstat->flags&WLAN_STA_HT))
	{
		status = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto OnAssocReq23aFail;
	}

	if ((pstat->flags & WLAN_STA_HT) &&
		    ((pstat->wpa2_pairwise_cipher&WPA_CIPHER_TKIP) ||
		      (pstat->wpa_pairwise_cipher&WPA_CIPHER_TKIP)))
	{
		DBG_8723A("HT: " MAC_FMT " tried to "
				   "use TKIP with HT association\n", MAC_ARG(pstat->hwaddr));

		/* status = WLAN_STATUS_CIPHER_REJECTED_PER_POLICY; */
		/* goto OnAssocReq23aFail; */
	}

       /*  */
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

	if (status != WLAN_STATUS_SUCCESS)
		goto OnAssocReq23aFail;

#ifdef CONFIG_8723AU_P2P
	pstat->is_p2p_device = false;
	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
	{
		if ((p2pie = rtw_get_p2p_ie23a(pframe + sizeof(struct ieee80211_hdr_3addr) + ie_offset, pkt_len - sizeof(struct ieee80211_hdr_3addr) - ie_offset, NULL, &p2pielen)))
		{
			pstat->is_p2p_device = true;
			if ((p2p_status_code = (u8)process_assoc_req_p2p_ie23a(pwdinfo, pframe, pkt_len, pstat))>0)
			{
				pstat->p2p_status_code = p2p_status_code;
				status = WLAN_STATUS_CAPS_UNSUPPORTED;
				goto OnAssocReq23aFail;
			}
		}
#ifdef CONFIG_8723AU_P2P
		if (rtw_get_wfd_ie(pframe + sizeof(struct ieee80211_hdr_3addr) + ie_offset, pkt_len - sizeof(struct ieee80211_hdr_3addr) - ie_offset, wfd_ie, &wfd_ielen))
		{
			u8	attr_content[ 10 ] = { 0x00 };
			u32	attr_contentlen = 0;

			DBG_8723A("[%s] WFD IE Found!!\n", __func__);
			rtw_get_wfd_attr_content(wfd_ie, wfd_ielen, WFD_ATTR_DEVICE_INFO, attr_content, &attr_contentlen);
			if (attr_contentlen)
			{
				pwdinfo->wfd_info->peer_rtsp_ctrlport = get_unaligned_be16(attr_content + 2);
				DBG_8723A("[%s] Peer PORT NUM = %d\n", __func__, pwdinfo->wfd_info->peer_rtsp_ctrlport);
			}
		}
#endif
	}
	pstat->p2p_status_code = p2p_status_code;
#endif /* CONFIG_8723AU_P2P */

	/* TODO: identify_proprietary_vendor_ie(); */
	/*  Realtek proprietary IE */
	/*  identify if this is Broadcom sta */
	/*  identify if this is ralink sta */
	/*  Customer proprietary IE */

	/* get a unique AID */
	if (pstat->aid > 0) {
		DBG_8723A("  old AID %d\n", pstat->aid);
	} else {
		for (pstat->aid = 1; pstat->aid <= NUM_STA; pstat->aid++)
			if (pstapriv->sta_aid[pstat->aid - 1] == NULL)
				break;

		if (pstat->aid > NUM_STA)
			pstat->aid = NUM_STA;
		if (pstat->aid > pstapriv->max_num_sta) {

			pstat->aid = 0;

			DBG_8723A("  no room for more AIDs\n");

			status = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;

			goto OnAssocReq23aFail;

		} else {
			pstapriv->sta_aid[pstat->aid - 1] = pstat;
			DBG_8723A("allocate new AID = (%d)\n", pstat->aid);
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
	if (pstat && (pstat->state & WIFI_FW_ASSOC_SUCCESS) &&
	    (WLAN_STATUS_SUCCESS == status)) {
#ifdef CONFIG_8723AU_AP_MODE
		/* 1 bss_cap_update & sta_info_update23a */
		bss_cap_update_on_sta_join23a(padapter, pstat);
		sta_info_update23a(padapter, pstat);

		/* issue assoc rsp before notify station join event. */
		if (ieee80211_is_assoc_req(frame_control))
			issue_asocrsp23a(padapter, status, pstat, WIFI_ASSOCRSP);
		else
			issue_asocrsp23a(padapter, status, pstat, WIFI_REASSOCRSP);

		/* 2 - report to upper layer */
		DBG_8723A("indicate_sta_join_event to upper layer - hostapd\n");
		rtw_cfg80211_indicate_sta_assoc(padapter, pframe, pkt_len);

		/* 3-(1) report sta add event */
		report_add_sta_event23a(padapter, pstat->hwaddr, pstat->aid);
#endif
	}

	return _SUCCESS;

asoc_class2_error:

#ifdef CONFIG_8723AU_AP_MODE
	issue_deauth23a(padapter, hdr->addr2, status);
#endif

	return _FAIL;

OnAssocReq23aFail:

#ifdef CONFIG_8723AU_AP_MODE
	pstat->aid = 0;
	if (ieee80211_is_assoc_req(frame_control))
		issue_asocrsp23a(padapter, status, pstat, WIFI_ASSOCRSP);
	else
		issue_asocrsp23a(padapter, status, pstat, WIFI_REASSOCRSP);
#endif

#endif /* CONFIG_8723AU_AP_MODE */

	return _FAIL;
}

unsigned int OnAssocRsp23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame)
{
	uint i;
	int res;
	unsigned short	status;
	struct ndis_802_11_var_ies *pIE;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u8 *pframe = skb->data;
	uint pkt_len = skb->len;

	DBG_8723A("%s\n", __func__);

	/* check A1 matches or not */
	if (!ether_addr_equal(myid(&padapter->eeprompriv),
			      ieee80211_get_DA(hdr)))
		return _SUCCESS;

	if (!(pmlmeinfo->state & (WIFI_FW_AUTH_SUCCESS | WIFI_FW_ASSOC_STATE)))
		return _SUCCESS;

	if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
		return _SUCCESS;

	del_timer_sync(&pmlmeext->link_timer);

	/* status */
	if ((status = le16_to_cpu(*(unsigned short *)(pframe + sizeof(struct ieee80211_hdr_3addr) + 2))) > 0)
	{
		DBG_8723A("assoc reject, status code: %d\n", status);
		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		res = -4;
		goto report_assoc_result;
	}

	/* get capabilities */
	pmlmeinfo->capability = le16_to_cpu(*(unsigned short *)(pframe + sizeof(struct ieee80211_hdr_3addr)));

	/* set slot time */
	pmlmeinfo->slotTime = (pmlmeinfo->capability & BIT(10))? 9: 20;

	/* AID */
	res = pmlmeinfo->aid = (int)(le16_to_cpu(*(unsigned short *)(pframe + sizeof(struct ieee80211_hdr_3addr) + 4))&0x3fff);

	/* following are moved to join event callback function */
	/* to handle HT, WMM, rate adaptive, update MAC reg */
	/* for not to handle the synchronous IO in the tasklet */
	for (i = (6 + sizeof(struct ieee80211_hdr_3addr)); i < pkt_len;) {
		pIE = (struct ndis_802_11_var_ies *)(pframe + i);

		switch (pIE->ElementID)
		{
		case _VENDOR_SPECIFIC_IE_:
			if (!memcmp(pIE->data, WMM_PARA_OUI23A, 6))/* WMM */
					WMM_param_handler23a(padapter, pIE);
#if defined(CONFIG_8723AU_P2P)
			else if (!memcmp(pIE->data, WFD_OUI23A, 4)) { /* WFD */
				DBG_8723A("[%s] Found WFD IE\n", __func__);
				WFD_info_handler(padapter, pIE);
			}
#endif
			break;

		case _HT_CAPABILITY_IE_:	/* HT caps */
			HT_caps_handler23a(padapter, pIE);
			break;

		case _HT_EXTRA_INFO_IE_:	/* HT info */
			HT_info_handler23a(padapter, pIE);
			break;

		case _ERPINFO_IE_:
			ERP_IE_handler23a(padapter, pIE);

		default:
			break;
		}

		i += (pIE->Length + 2);
	}

	pmlmeinfo->state &= (~WIFI_FW_ASSOC_STATE);
	pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;

	/* Update Basic Rate Table for spec, 2010-12-28 , by thomas */
	UpdateBrateTbl23a(padapter, pmlmeinfo->network.SupportedRates);

report_assoc_result:
	pmlmepriv->assoc_rsp_len = 0;
	if (res > 0) {
		kfree(pmlmepriv->assoc_rsp);
		pmlmepriv->assoc_rsp = kmalloc(pkt_len, GFP_ATOMIC);
		if (pmlmepriv->assoc_rsp) {
			memcpy(pmlmepriv->assoc_rsp, pframe, pkt_len);
			pmlmepriv->assoc_rsp_len = pkt_len;
		}
	} else
		kfree(pmlmepriv->assoc_rsp);

	report_join_res23a(padapter, res);

	return _SUCCESS;
}

unsigned int OnDeAuth23a(struct rtw_adapter *padapter,
			 struct recv_frame *precv_frame)
{
	unsigned short	reason;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u8 *pframe = skb->data;
#ifdef CONFIG_8723AU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif /* CONFIG_8723AU_P2P */

	/* check A3 */
	if (!ether_addr_equal(hdr->addr3, get_my_bssid23a(&pmlmeinfo->network)))
		return _SUCCESS;

#ifdef CONFIG_8723AU_P2P
	if (pwdinfo->rx_invitereq_info.scan_op_ch_only) {
		mod_timer(&pwdinfo->reset_ch_sitesurvey,
			  jiffies + msecs_to_jiffies(10));
	}
#endif /* CONFIG_8723AU_P2P */

	reason = le16_to_cpu(*(unsigned short *)(pframe + sizeof(struct ieee80211_hdr_3addr)));

	DBG_8723A("%s Reason code(%d)\n", __func__, reason);

#ifdef CONFIG_8723AU_AP_MODE
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		DBG_8723A_LEVEL(_drv_always_, "ap recv deauth reason code(%d) "
				"sta:%pM\n", reason, hdr->addr2);

		psta = rtw_get_stainfo23a(pstapriv, hdr->addr2);
		if (psta) {
			u8 updated = 0;

			spin_lock_bh(&pstapriv->asoc_list_lock);
			if (!list_empty(&psta->asoc_list)) {
				list_del_init(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;
				updated = ap_free_sta23a(padapter, psta,
						      false, reason);
			}
			spin_unlock_bh(&pstapriv->asoc_list_lock);

			associated_clients_update23a(padapter, updated);
		}

		return _SUCCESS;
	}
	else
#endif
	{
		DBG_8723A_LEVEL(_drv_always_, "sta recv deauth reason code(%d) "
				"sta:%pM\n", reason, hdr->addr3);

		receive_disconnect23a(padapter, hdr->addr3, reason);
	}
	pmlmepriv->LinkDetectInfo.bBusyTraffic = false;
	return _SUCCESS;
}

unsigned int OnDisassoc23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame)
{
	unsigned short	reason;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u8 *pframe = skb->data;
#ifdef CONFIG_8723AU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif /* CONFIG_8723AU_P2P */

	/* check A3 */
	if (!ether_addr_equal(hdr->addr3, get_my_bssid23a(&pmlmeinfo->network)))
		return _SUCCESS;

#ifdef CONFIG_8723AU_P2P
	if (pwdinfo->rx_invitereq_info.scan_op_ch_only)
	{
		mod_timer(&pwdinfo->reset_ch_sitesurvey,
			  jiffies + msecs_to_jiffies(10));
	}
#endif /* CONFIG_8723AU_P2P */

	reason = le16_to_cpu(*(unsigned short *)
			     (pframe + sizeof(struct ieee80211_hdr_3addr)));

        DBG_8723A("%s Reason code(%d)\n", __func__, reason);

#ifdef CONFIG_8723AU_AP_MODE
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		DBG_8723A_LEVEL(_drv_always_, "ap recv disassoc reason code(%d)"
				" sta:%pM\n", reason, hdr->addr2);

		psta = rtw_get_stainfo23a(pstapriv, hdr->addr2);
		if (psta) {
			u8 updated = 0;

			spin_lock_bh(&pstapriv->asoc_list_lock);
			if (!list_empty(&psta->asoc_list)) {
				list_del_init(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;
				updated = ap_free_sta23a(padapter, psta,
						      false, reason);
			}
			spin_unlock_bh(&pstapriv->asoc_list_lock);

			associated_clients_update23a(padapter, updated);
		}

		return _SUCCESS;
	}
	else
#endif
	{
		DBG_8723A_LEVEL(_drv_always_, "ap recv disassoc reason "
				"code(%d) sta:%pM\n", reason, hdr->addr3);

		receive_disconnect23a(padapter, hdr->addr3, reason);
	}
	pmlmepriv->LinkDetectInfo.bBusyTraffic = false;
	return _SUCCESS;
}

unsigned int OnAtim23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame)
{
	DBG_8723A("%s\n", __func__);
	return _SUCCESS;
}

unsigned int on_action_spct23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame)
{
	return _FAIL;
}

unsigned int OnAction23a_qos(struct rtw_adapter *padapter, struct recv_frame *precv_frame)
{
	return _SUCCESS;
}

unsigned int OnAction23a_dls(struct rtw_adapter *padapter, struct recv_frame *precv_frame)
{
	return _SUCCESS;
}

unsigned int OnAction23a_back23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame)
{
	u8 *addr;
	struct sta_info *psta = NULL;
	struct recv_reorder_ctrl *preorder_ctrl;
	unsigned char		*frame_body;
	unsigned char		category, action;
	unsigned short	tid, status, reason_code = 0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u8 *pframe = skb->data;
	struct sta_priv *pstapriv = &padapter->stapriv;

	/* check RA matches or not */
	if (!ether_addr_equal(myid(&padapter->eeprompriv), hdr->addr1))
		return _SUCCESS;

	DBG_8723A("%s\n", __func__);

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		if (!(pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS))
			return _SUCCESS;

	addr = hdr->addr2;
	psta = rtw_get_stainfo23a(pstapriv, addr);

	if (!psta)
		return _SUCCESS;

	frame_body = (unsigned char *)
		(pframe + sizeof(struct ieee80211_hdr_3addr));

	category = frame_body[0];
	if (category == WLAN_CATEGORY_BACK) { /*  representing Block Ack */
		if (!pmlmeinfo->HT_enable)
			return _SUCCESS;
		action = frame_body[1];
		DBG_8723A("%s, action =%d\n", __func__, action);
		switch (action) {
		case WLAN_ACTION_ADDBA_REQ: /* ADDBA request */
			memcpy(&pmlmeinfo->ADDBA_req, &frame_body[2],
			       sizeof(struct ADDBA_request));
			process_addba_req23a(padapter,
					     (u8 *)&pmlmeinfo->ADDBA_req, addr);
			if (pmlmeinfo->bAcceptAddbaReq == true)
				issue_action_BA23a(padapter, addr,
						   WLAN_ACTION_ADDBA_RESP, 0);
			else {
				/* reject ADDBA Req */
				issue_action_BA23a(padapter, addr,
						   WLAN_ACTION_ADDBA_RESP, 37);
			}
			break;
		case WLAN_ACTION_ADDBA_RESP: /* ADDBA response */
			status = get_unaligned_le16(&frame_body[3]);
			tid = ((frame_body[5] >> 2) & 0x7);
			if (status == 0) {	/* successful */
				DBG_8723A("agg_enable for TID =%d\n", tid);
				psta->htpriv.agg_enable_bitmap |= 1 << tid;
				psta->htpriv.candidate_tid_bitmap &=
					~CHKBIT(tid);
			} else
				psta->htpriv.agg_enable_bitmap &= ~CHKBIT(tid);
			break;

		case WLAN_ACTION_DELBA: /* DELBA */
			if ((frame_body[3] & BIT(3)) == 0) {
				psta->htpriv.agg_enable_bitmap &=
					~(1 << ((frame_body[3] >> 4) & 0xf));
				psta->htpriv.candidate_tid_bitmap &=
					~(1 << ((frame_body[3] >> 4) & 0xf));

				/* reason_code = frame_body[4] | (frame_body[5] << 8); */
				reason_code = get_unaligned_le16(&frame_body[4]);
			} else if ((frame_body[3] & BIT(3)) == BIT(3)) {
				tid = (frame_body[3] >> 4) & 0x0F;

				preorder_ctrl =  &psta->recvreorder_ctrl[tid];
				preorder_ctrl->enable = false;
				preorder_ctrl->indicate_seq = 0xffff;
			}

			DBG_8723A("%s(): DELBA: %x(%x)\n", __func__,
				  pmlmeinfo->agg_enable_bitmap, reason_code);
			/* todo: how to notify the host while receiving
			   DELETE BA */
			break;
		default:
			break;
		}
	}
	return _SUCCESS;
}

#ifdef CONFIG_8723AU_P2P

static int get_reg_classes_full_count(struct p2p_channels channel_list) {
	int cnt = 0;
	int i;

	for (i = 0; i < channel_list.reg_classes; i++)
		cnt += channel_list.reg_class[i].channels;

	return cnt;
}

void issue_p2p_GO_request23a(struct rtw_adapter *padapter, u8* raddr)
{
	unsigned char category = WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_GO_NEGO_REQ;
	u8			wpsie[ 255 ] = { 0x00 }, p2pie[ 255 ] = { 0x00 };
	u8			wpsielen = 0, p2pielen = 0;
	u16			len_channellist_attr = 0;
#ifdef CONFIG_8723AU_P2P
	u32					wfdielen = 0;
#endif /* CONFIG_8723AU_P2P */

	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		return;

	DBG_8723A("[%s] In\n", __func__);
	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

	ether_addr_copy(pwlanhdr->addr1, raddr);
	ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
	ether_addr_copy(pwlanhdr->addr3, myid(&padapter->eeprompriv));

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie23a(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &action, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 4, (unsigned char *)&p2poui,
				     &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &oui_subtype, &pattrib->pktlen);
	pwdinfo->negotiation_dialog_token = 1;	/*Initialize the dialog value*/
	pframe = rtw_set_fixed_ie23a(pframe, 1,
				     &pwdinfo->negotiation_dialog_token,
				     &pattrib->pktlen);

	/*	WPS Section */
	wpsielen = 0;
	/*	WPS OUI */
	*(u32*) (wpsie) = cpu_to_be32(WPSOUI);
	wpsielen += 4;

	/*	WPS version */
	/*	Type: */
	*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_VER1);
	wpsielen += 2;

	/*	Length: */
	*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0001);
	wpsielen += 2;

	/*	Value: */
	wpsie[wpsielen++] = WPS_VERSION_1;	/*	Version 1.0 */

	/*	Device Password ID */
	/*	Type: */
	*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_DEVICE_PWID);
	wpsielen += 2;

	/*	Length: */
	*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0002);
	wpsielen += 2;

	/*	Value: */

	if (pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_PEER_DISPLAY_PIN)
	{
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_DPID_USER_SPEC);
	}
	else if (pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_SELF_DISPLAY_PIN)
	{
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_DPID_REGISTRAR_SPEC);
	}
	else if (pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_PBC)
	{
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_DPID_PBC);
	}

	wpsielen += 2;

	pframe = rtw_set_ie23a(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen);

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
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	/*	Device Capability Bitmap, 1 byte */
	p2pie[p2pielen++] = DMP_P2P_DEVCAP_SUPPORT;

	/*	Group Capability Bitmap, 1 byte */
	if (pwdinfo->persistent_supported)
	{
		p2pie[p2pielen++] = P2P_GRPCAP_CROSS_CONN | P2P_GRPCAP_PERSISTENT_GROUP;
	}
	else
	{
		p2pie[p2pielen++] = P2P_GRPCAP_CROSS_CONN;
	}

	/*	Group Owner Intent */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_GO_INTENT;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0001);
	p2pielen += 2;

	/*	Value: */
	/*	Todo the tie breaker bit. */
	p2pie[p2pielen++] = ((pwdinfo->intent << 1) | BIT(0));

	/*	Configuration Timeout */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CONF_TIMEOUT;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	p2pie[p2pielen++] = 200;	/*	2 seconds needed to be the P2P GO */
	p2pie[p2pielen++] = 200;	/*	2 seconds needed to be the P2P Client */

	/*	Listen Channel */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_LISTEN_CH;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0005);
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
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0004);
	p2pielen += 2;

	/*	Value: */
	/*	Availability Period */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0xFFFF);
	p2pielen += 2;

	/*	Availability Interval */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0xFFFF);
	p2pielen += 2;

	/*	Intended P2P Interface Address */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_INTENTED_IF_ADDR;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(ETH_ALEN);
	p2pielen += 2;

	/*	Value: */
	memcpy(p2pie + p2pielen, myid(&padapter->eeprompriv), ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	Channel List */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CH_LIST;

	/*  Length: */
	/*  Country String(3) */
	/*  + (Operating Class (1) + Number of Channels(1)) * Operation Classes (?) */
	/*  + number of channels in all classes */
	len_channellist_attr = 3
	   + (1 + 1) * (u16)(pmlmeext->channel_list.reg_classes)
	   + get_reg_classes_full_count(pmlmeext->channel_list);

	*(u16*) (p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);
	p2pielen += 2;

	/*	Value: */
	/*	Country String */
	p2pie[p2pielen++] = 'X';
	p2pie[p2pielen++] = 'X';

	/*	The third byte should be set to 0x04. */
	/*	Described in the "Operating Channel Attribute" section. */
	p2pie[p2pielen++] = 0x04;

	/*	Channel Entry List */

	{
		int i, j;
		for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
			/*	Operating Class */
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

			/*	Number of Channels */
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

			/*	Channel List */
			for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++) {
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
			}
		}
	}

	/*	Device Info */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes) */
	/*	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes) */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(21 + pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	/*	P2P Device Address */
	memcpy(p2pie + p2pielen, myid(&padapter->eeprompriv), ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	Config Method */
	/*	This field should be big endian. Noted by P2P specification. */

	*(u16*) (p2pie + p2pielen) = cpu_to_be16(pwdinfo->supported_wps_cm);

	p2pielen += 2;

	/*	Primary Device Type */
	/*	Category ID */
	*(u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA);
	p2pielen += 2;

	/*	OUI */
	*(u32*) (p2pie + p2pielen) = cpu_to_be32(WPSOUI);
	p2pielen += 4;

	/*	Sub Category ID */
	*(u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
	p2pielen += 2;

	/*	Number of Secondary Device Types */
	p2pie[p2pielen++] = 0x00;	/*	No Secondary Device Type List */

	/*	Device Name */
	/*	Type: */
	*(u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_ATTR_DEVICE_NAME);
	p2pielen += 2;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_be16(pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	memcpy(p2pie + p2pielen, pwdinfo->device_name,
	       pwdinfo->device_name_len);
	p2pielen += pwdinfo->device_name_len;

	/*	Operating Channel */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_OPERATING_CH;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0005);
	p2pielen += 2;

	/*	Value: */
	/*	Country String */
	p2pie[p2pielen++] = 'X';
	p2pie[p2pielen++] = 'X';

	/*	The third byte should be set to 0x04. */
	/*	Described in the "Operating Channel Attribute" section. */
	p2pie[p2pielen++] = 0x04;

	/*	Operating Class */
	if (pwdinfo->operating_channel <= 14)
	{
		/*	Operating Class */
		p2pie[p2pielen++] = 0x51;
	}
	else if ((pwdinfo->operating_channel >= 36) && (pwdinfo->operating_channel <= 48))
	{
		/*	Operating Class */
		p2pie[p2pielen++] = 0x73;
	}
	else
	{
		/*	Operating Class */
		p2pie[p2pielen++] = 0x7c;
	}

	/*	Channel Number */
	p2pie[p2pielen++] = pwdinfo->operating_channel;	/*	operating channel number */

	pframe = rtw_set_ie23a(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen);

#ifdef CONFIG_8723AU_P2P
	wfdielen = build_nego_req_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif /* CONFIG_8723AU_P2P */

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe23a(padapter, pmgntframe);

	return;
}

static void issue_p2p_GO_response(struct rtw_adapter *padapter, u8* raddr, u8* frame_body, uint len, u8 result)
{

	unsigned char category = WLAN_CATEGORY_PUBLIC;
	u8 action = P2P_PUB_ACTION_ACTION;
	u32 p2poui = cpu_to_be32(P2POUI);
	u8 oui_subtype = P2P_GO_NEGO_RESP;
	u8 wpsie[255] = { 0x00 }, p2pie[255] = { 0x00 };
	u8 p2pielen = 0;
	uint wpsielen = 0;
	u16 wps_devicepassword_id = 0x0000;
	uint wps_devicepassword_id_len = 0;
	u16 len_channellist_attr = 0;
	int i, j;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#ifdef CONFIG_8723AU_P2P
	u32 wfdielen = 0;
#endif /* CONFIG_8723AU_P2P */

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		return;

	DBG_8723A("[%s] In, result = %d\n", __func__,  result);
	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

	ether_addr_copy(pwlanhdr->addr1, raddr);
	ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
	ether_addr_copy(pwlanhdr->addr3, myid(&padapter->eeprompriv));

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie23a(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &action, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 4, (unsigned char *) &p2poui,
				     &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &oui_subtype, &pattrib->pktlen);
	/*	The Dialog Token of provisioning discovery request frame. */
	pwdinfo->negotiation_dialog_token = frame_body[7];
	pframe = rtw_set_fixed_ie23a(pframe, 1,
				     &pwdinfo->negotiation_dialog_token,
				     &pattrib->pktlen);

	/*	Commented by Albert 20110328 */
	/*	Try to get the device password ID from the WPS IE of group
		negotiation request frame */
	/*	WiFi Direct test plan 5.1.15 */
	rtw_get_wps_ie23a(frame_body + _PUBLIC_ACTION_IE_OFFSET_,
			  len - _PUBLIC_ACTION_IE_OFFSET_, wpsie, &wpsielen);
	rtw_get_wps_attr_content23a(wpsie, wpsielen, WPS_ATTR_DEVICE_PWID,
				    (u8 *)&wps_devicepassword_id,
				    &wps_devicepassword_id_len);
	wps_devicepassword_id = be16_to_cpu(wps_devicepassword_id);

	memset(wpsie, 0x00, 255);
	wpsielen = 0;

	/*	WPS Section */
	wpsielen = 0;
	/*	WPS OUI */
	*(u32*) (wpsie) = cpu_to_be32(WPSOUI);
	wpsielen += 4;

	/*	WPS version */
	/*	Type: */
	*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_VER1);
	wpsielen += 2;

	/*	Length: */
	*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0001);
	wpsielen += 2;

	/*	Value: */
	wpsie[wpsielen++] = WPS_VERSION_1;	/*	Version 1.0 */

	/*	Device Password ID */
	/*	Type: */
	*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_DEVICE_PWID);
	wpsielen += 2;

	/*	Length: */
	*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0002);
	wpsielen += 2;

	/*	Value: */
	if (wps_devicepassword_id == WPS_DPID_USER_SPEC) {
		*(u16*) (wpsie + wpsielen) =
			cpu_to_be16(WPS_DPID_REGISTRAR_SPEC);
	} else if (wps_devicepassword_id == WPS_DPID_REGISTRAR_SPEC) {
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_DPID_USER_SPEC);
	} else {
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_DPID_PBC);
	}
	wpsielen += 2;

	/*	Commented by Kurt 20120113 */
	/*	If some device wants to do p2p handshake without sending prov_disc_req */
	/*	We have to get peer_req_cm from here. */
	if (!memcmp(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "000", 3)) {
		if (wps_devicepassword_id == WPS_DPID_USER_SPEC) {
			memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "dis", 3);
		} else if (wps_devicepassword_id == WPS_DPID_REGISTRAR_SPEC) {
			memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pad", 3);
		} else {
			memcpy(pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pbc", 3);
		}
	}

	pframe = rtw_set_ie23a(pframe, _VENDOR_SPECIFIC_IE_, wpsielen,
			       (unsigned char *) wpsie, &pattrib->pktlen);

	/*	P2P IE Section. */

	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*	Commented by Albert 20100908 */
	/*	According to the P2P Specification, the group negoitation
		response frame should contain 9 P2P attributes */
	/*	1. Status */
	/*	2. P2P Capability */
	/*	3. Group Owner Intent */
	/*	4. Configuration Timeout */
	/*	5. Operating Channel */
	/*	6. Intended P2P Interface Address */
	/*	7. Channel List */
	/*	8. Device Info */
	/*	9. Group ID	(Only GO) */

	/*	ToDo: */

	/*	P2P Status */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_STATUS;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0001);
	p2pielen += 2;

	/*	Value: */
	p2pie[p2pielen++] = result;

	/*	P2P Capability */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CAPABILITY;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	/*	Device Capability Bitmap, 1 byte */

	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT)) {
		/*	Commented by Albert 2011/03/08 */
		/*	According to the P2P specification */
		/*	if the sending device will be client, the P2P
			Capability should be reserved of group negotation
			response frame */
		p2pie[p2pielen++] = 0;
	} else {
		/*	Be group owner or meet the error case */
		p2pie[p2pielen++] = DMP_P2P_DEVCAP_SUPPORT;
	}

	/*	Group Capability Bitmap, 1 byte */
	if (pwdinfo->persistent_supported) {
		p2pie[p2pielen++] = P2P_GRPCAP_CROSS_CONN |
			P2P_GRPCAP_PERSISTENT_GROUP;
	} else {
		p2pie[p2pielen++] = P2P_GRPCAP_CROSS_CONN;
	}

	/*	Group Owner Intent */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_GO_INTENT;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0001);
	p2pielen += 2;

	/*	Value: */
	if (pwdinfo->peer_intent & 0x01) {
		/*	Peer's tie breaker bit is 1, our tie breaker
			bit should be 0 */
		p2pie[p2pielen++] = (pwdinfo->intent << 1);
	} else {
		/* Peer's tie breaker bit is 0, our tie breaker bit
		   should be 1 */
		p2pie[p2pielen++] = ((pwdinfo->intent << 1) | BIT(0));
	}

	/*	Configuration Timeout */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CONF_TIMEOUT;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	/*	2 seconds needed to be the P2P GO */
	p2pie[p2pielen++] = 200;
	/*	2 seconds needed to be the P2P Client */
	p2pie[p2pielen++] = 200;

	/*	Operating Channel */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_OPERATING_CH;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0005);
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
	} else if ((pwdinfo->operating_channel >= 36) &&
		   (pwdinfo->operating_channel <= 48)) {
		/*	Operating Class */
		p2pie[p2pielen++] = 0x73;
	} else {
		/*	Operating Class */
		p2pie[p2pielen++] = 0x7c;
	}

	/*	Channel Number */
	/*	operating channel number */
	p2pie[p2pielen++] = pwdinfo->operating_channel;

	/*	Intended P2P Interface Address */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_INTENTED_IF_ADDR;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(ETH_ALEN);
	p2pielen += 2;

	/*	Value: */
	memcpy(p2pie + p2pielen, myid(&padapter->eeprompriv), ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	Channel List */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CH_LIST;

	/*  Country String(3) */
	/*  + (Operating Class (1) + Number of Channels(1)) *
	    Operation Classes (?) */
	/*  + number of channels in all classes */
	len_channellist_attr = 3 +
		(1 + 1) * (u16)pmlmeext->channel_list.reg_classes +
		get_reg_classes_full_count(pmlmeext->channel_list);

	*(u16*) (p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);

	p2pielen += 2;

	/*	Value: */
	/*	Country String */
	p2pie[p2pielen++] = 'X';
	p2pie[p2pielen++] = 'X';

	/*	The third byte should be set to 0x04. */
	/*	Described in the "Operating Channel Attribute" section. */
	p2pie[p2pielen++] = 0x04;

	/*	Channel Entry List */

	for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
		/*	Operating Class */
		p2pie[p2pielen++] =
			pmlmeext->channel_list.reg_class[j].reg_class;

		/*	Number of Channels */
		p2pie[p2pielen++] =
			pmlmeext->channel_list.reg_class[j].channels;

		/*	Channel List */
		for (i = 0;
		     i < pmlmeext->channel_list.reg_class[j].channels; i++) {
			p2pie[p2pielen++] =
				pmlmeext->channel_list.reg_class[j].channel[i];
		}
	}

	/*	Device Info */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) +
		Primary Device Type (8bytes) */
	/*	+ NumofSecondDevType (1byte) + WPS Device Name ID field
		(2bytes) + WPS Device Name Len field (2bytes) */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(21 + pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	/*	P2P Device Address */
	memcpy(p2pie + p2pielen, myid(&padapter->eeprompriv), ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	Config Method */
	/*	This field should be big endian. Noted by P2P specification. */

	*(u16*) (p2pie + p2pielen) = cpu_to_be16(pwdinfo->supported_wps_cm);

	p2pielen += 2;

	/*	Primary Device Type */
	/*	Category ID */
	*(u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA);
	p2pielen += 2;

	/*	OUI */
	*(u32*) (p2pie + p2pielen) = cpu_to_be32(WPSOUI);
	p2pielen += 4;

	/*	Sub Category ID */
	*(u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
	p2pielen += 2;

	/*	Number of Secondary Device Types */
	p2pie[p2pielen++] = 0x00;	/*	No Secondary Device Type List */

	/*	Device Name */
	/*	Type: */
	*(u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_ATTR_DEVICE_NAME);
	p2pielen += 2;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_be16(pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	memcpy(p2pie + p2pielen, pwdinfo->device_name,
	       pwdinfo->device_name_len);
	p2pielen += pwdinfo->device_name_len;

	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
	{
		/*	Group ID Attribute */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_GROUP_ID;

		/*	Length: */
		*(u16*) (p2pie + p2pielen) =
			cpu_to_le16(ETH_ALEN + pwdinfo->nego_ssidlen);
		p2pielen += 2;

		/*	Value: */
		/*	p2P Device Address */
		memcpy(p2pie + p2pielen, pwdinfo->device_addr, ETH_ALEN);
		p2pielen += ETH_ALEN;

		/*	SSID */
		memcpy(p2pie + p2pielen, pwdinfo->nego_ssid,
		       pwdinfo->nego_ssidlen);
		p2pielen += pwdinfo->nego_ssidlen;

	}

	pframe = rtw_set_ie23a(pframe, _VENDOR_SPECIFIC_IE_, p2pielen,
			       (unsigned char *) p2pie, &pattrib->pktlen);

#ifdef CONFIG_8723AU_P2P
	wfdielen = build_nego_resp_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif /* CONFIG_8723AU_P2P */

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe23a(padapter, pmgntframe);

	return;
}

static void issue_p2p_GO_confirm(struct rtw_adapter *padapter, u8* raddr,
				 u8 result)
{

	unsigned char category = WLAN_CATEGORY_PUBLIC;
	u8 action = P2P_PUB_ACTION_ACTION;
	u32 p2poui = cpu_to_be32(P2POUI);
	u8 oui_subtype = P2P_GO_NEGO_CONF;
	u8 p2pie[ 255 ] = { 0x00 };
	u8 p2pielen = 0;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#ifdef CONFIG_8723AU_P2P
	u32 wfdielen = 0;
#endif /* CONFIG_8723AU_P2P */

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		return;

	DBG_8723A("[%s] In\n", __func__);
	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

	ether_addr_copy(pwlanhdr->addr1, raddr);
	ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
	ether_addr_copy(pwlanhdr->addr3, myid(&padapter->eeprompriv));

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie23a(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &action, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 4, (unsigned char *)&p2poui,
				     &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &oui_subtype, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1,
				     &pwdinfo->negotiation_dialog_token,
				  &pattrib->pktlen);
	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*	Commented by Albert 20110306 */
	/*	According to the P2P Specification, the group negoitation
		request frame should contain 5 P2P attributes */
	/*	1. Status */
	/*	2. P2P Capability */
	/*	3. Operating Channel */
	/*	4. Channel List */
	/*	5. Group ID	(if this WiFi is GO) */

	/*	P2P Status */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_STATUS;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0001);
	p2pielen += 2;

	/*	Value: */
	p2pie[p2pielen++] = result;

	/*	P2P Capability */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CAPABILITY;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	/*	Device Capability Bitmap, 1 byte */
	p2pie[p2pielen++] = DMP_P2P_DEVCAP_SUPPORT;

	/*	Group Capability Bitmap, 1 byte */
	if (pwdinfo->persistent_supported) {
		p2pie[p2pielen++] = P2P_GRPCAP_CROSS_CONN |
			P2P_GRPCAP_PERSISTENT_GROUP;
	} else {
		p2pie[p2pielen++] = P2P_GRPCAP_CROSS_CONN;
	}

	/*	Operating Channel */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_OPERATING_CH;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0005);
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
		} else if ((pwdinfo->peer_operating_ch >= 36) &&
			 (pwdinfo->peer_operating_ch <= 48)) {
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
		}
		else if ((pwdinfo->operating_channel >= 36) &&
			 (pwdinfo->operating_channel <= 48)) {
			/*	Operating Class */
			p2pie[p2pielen++] = 0x73;
		} else {
			/*	Operating Class */
			p2pie[p2pielen++] = 0x7c;
		}

		/*	Channel Number */
		/*	Use the listen channel as the operating channel */
		p2pie[p2pielen++] = pwdinfo->operating_channel;
	}

	/*	Channel List */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CH_LIST;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) =
		cpu_to_le16(pwdinfo->channel_list_attr_len);
	p2pielen += 2;

	/*	Value: */
	memcpy(p2pie + p2pielen, pwdinfo->channel_list_attr,
	       pwdinfo->channel_list_attr_len);
	p2pielen += pwdinfo->channel_list_attr_len;

	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
		/*	Group ID Attribute */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_GROUP_ID;

		/*	Length: */
		*(u16*) (p2pie + p2pielen) =
			cpu_to_le16(ETH_ALEN + pwdinfo->nego_ssidlen);
		p2pielen += 2;

		/*	Value: */
		/*	p2P Device Address */
		memcpy(p2pie + p2pielen, pwdinfo->device_addr, ETH_ALEN);
		p2pielen += ETH_ALEN;

		/*	SSID */
		memcpy(p2pie + p2pielen, pwdinfo->nego_ssid,
		       pwdinfo->nego_ssidlen);
		p2pielen += pwdinfo->nego_ssidlen;
	}

	pframe = rtw_set_ie23a(pframe, _VENDOR_SPECIFIC_IE_, p2pielen,
			       (unsigned char *)p2pie, &pattrib->pktlen);

#ifdef CONFIG_8723AU_P2P
	wfdielen = build_nego_confirm_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif /* CONFIG_8723AU_P2P */

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe23a(padapter, pmgntframe);

	return;
}

void issue_p2p_invitation_request23a(struct rtw_adapter *padapter, u8* raddr)
{
	unsigned char category = WLAN_CATEGORY_PUBLIC;
	u8 action = P2P_PUB_ACTION_ACTION;
	u32 p2poui = cpu_to_be32(P2POUI);
	u8 oui_subtype = P2P_INVIT_REQ;
	u8 p2pie[ 255 ] = { 0x00 };
	u8 p2pielen = 0;
	u8 dialogToken = 3;
	u16 len_channellist_attr = 0;
#ifdef CONFIG_8723AU_P2P
	u32 wfdielen = 0;
#endif /* CONFIG_8723AU_P2P */
	int i, j;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

	ether_addr_copy(pwlanhdr->addr1, raddr);
	ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
	ether_addr_copy(pwlanhdr->addr3, raddr);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie23a(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &action, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 4, (unsigned char *) &p2poui,
				     &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &oui_subtype, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &dialogToken, &pattrib->pktlen);

	/*	P2P IE Section. */

	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*	Commented by Albert 20101011 */
	/*	According to the P2P Specification, the P2P Invitation
		request frame should contain 7 P2P attributes */
	/*	1. Configuration Timeout */
	/*	2. Invitation Flags */
	/*	3. Operating Channel	(Only GO) */
	/*	4. P2P Group BSSID	(Should be included if I am the GO) */
	/*	5. Channel List */
	/*	6. P2P Group ID */
	/*	7. P2P Device Info */

	/*	Configuration Timeout */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CONF_TIMEOUT;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	/*	2 seconds needed to be the P2P GO */
	p2pie[p2pielen++] = 200;
	/*	2 seconds needed to be the P2P Client */
	p2pie[p2pielen++] = 200;

	/*	Invitation Flags */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_INVITATION_FLAGS;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0001);
	p2pielen += 2;

	/*	Value: */
	p2pie[p2pielen++] = P2P_INVITATION_FLAGS_PERSISTENT;

	/*	Operating Channel */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_OPERATING_CH;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0005);
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
	else if ((pwdinfo->invitereq_info.operating_ch >= 36) &&
		 (pwdinfo->invitereq_info.operating_ch <= 48))
		p2pie[p2pielen++] = 0x73;
	else
		p2pie[p2pielen++] = 0x7c;

	/*	Channel Number */
	/*	operating channel number */
	p2pie[p2pielen++] = pwdinfo->invitereq_info.operating_ch;

	if (ether_addr_equal(myid(&padapter->eeprompriv),
			     pwdinfo->invitereq_info.go_bssid)) {
		/*	P2P Group BSSID */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_GROUP_BSSID;

		/*	Length: */
		*(u16*) (p2pie + p2pielen) = cpu_to_le16(ETH_ALEN);
		p2pielen += 2;

		/*	Value: */
		/*	P2P Device Address for GO */
		memcpy(p2pie + p2pielen, pwdinfo->invitereq_info.go_bssid,
		       ETH_ALEN);
		p2pielen += ETH_ALEN;
	}

	/*	Channel List */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CH_LIST;

	/*	Length: */
	/*  Country String(3) */
	/*  + (Operating Class (1) + Number of Channels(1)) *
	    Operation Classes (?) */
	/*  + number of channels in all classes */
	len_channellist_attr = 3 +
		(1 + 1) * (u16)pmlmeext->channel_list.reg_classes +
		get_reg_classes_full_count(pmlmeext->channel_list);

	*(u16*) (p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);
	p2pielen += 2;

	/*	Value: */
	/*	Country String */
	p2pie[p2pielen++] = 'X';
	p2pie[p2pielen++] = 'X';

	/*	The third byte should be set to 0x04. */
	/*	Described in the "Operating Channel Attribute" section. */
	p2pie[p2pielen++] = 0x04;

	/*	Channel Entry List */
	for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
		/*	Operating Class */
		p2pie[p2pielen++] =
			pmlmeext->channel_list.reg_class[j].reg_class;

		/*	Number of Channels */
		p2pie[p2pielen++] =
			pmlmeext->channel_list.reg_class[j].channels;

		/*	Channel List */
		for (i = 0;
		     i < pmlmeext->channel_list.reg_class[j].channels; i++) {
			p2pie[p2pielen++] =
				pmlmeext->channel_list.reg_class[j].channel[i];
		}
	}

	/*	P2P Group ID */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_GROUP_ID;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) =
		cpu_to_le16(6 + pwdinfo->invitereq_info.ssidlen);
	p2pielen += 2;

	/*	Value: */
	/*	P2P Device Address for GO */
	memcpy(p2pie + p2pielen, pwdinfo->invitereq_info.go_bssid, ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	SSID */
	memcpy(p2pie + p2pielen, pwdinfo->invitereq_info.go_ssid,
	       pwdinfo->invitereq_info.ssidlen);
	p2pielen += pwdinfo->invitereq_info.ssidlen;

	/*	Device Info */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) +
		Primary Device Type (8bytes) */
	/*	+ NumofSecondDevType (1byte) + WPS Device Name ID field
		(2bytes) + WPS Device Name Len field (2bytes) */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(21 + pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	/*	P2P Device Address */
	memcpy(p2pie + p2pielen, myid(&padapter->eeprompriv), ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	Config Method */
	/*	This field should be big endian. Noted by P2P specification. */
	*(u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_CONFIG_METHOD_DISPLAY);
	p2pielen += 2;

	/*	Primary Device Type */
	/*	Category ID */
	*(u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA);
	p2pielen += 2;

	/*	OUI */
	*(u32*) (p2pie + p2pielen) = cpu_to_be32(WPSOUI);
	p2pielen += 4;

	/*	Sub Category ID */
	*(u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
	p2pielen += 2;

	/*	Number of Secondary Device Types */
	p2pie[p2pielen++] = 0x00;	/*	No Secondary Device Type List */

	/*	Device Name */
	/*	Type: */
	*(u16*) (p2pie + p2pielen) = cpu_to_be16(WPS_ATTR_DEVICE_NAME);
	p2pielen += 2;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_be16(pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	memcpy(p2pie + p2pielen, pwdinfo->device_name,
	       pwdinfo->device_name_len);
	p2pielen += pwdinfo->device_name_len;

	pframe = rtw_set_ie23a(pframe, _VENDOR_SPECIFIC_IE_, p2pielen,
			       (unsigned char *) p2pie, &pattrib->pktlen);

#ifdef CONFIG_8723AU_P2P
	wfdielen = build_invitation_req_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif /* CONFIG_8723AU_P2P */

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe23a(padapter, pmgntframe);

	return;
}

void issue_p2p_invitation_response23a(struct rtw_adapter *padapter, u8 *raddr,
				      u8 dialogToken, u8 status_code)
{
	unsigned char category = WLAN_CATEGORY_PUBLIC;
	u8 action = P2P_PUB_ACTION_ACTION;
	u32 p2poui = cpu_to_be32(P2POUI);
	u8 oui_subtype = P2P_INVIT_RESP;
	u8 p2pie[ 255 ] = { 0x00 };
	u8 p2pielen = 0;
	u16 len_channellist_attr = 0;
#ifdef CONFIG_8723AU_P2P
	u32 wfdielen = 0;
#endif /* CONFIG_8723AU_P2P */
	int i, j;

	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

	ether_addr_copy(pwlanhdr->addr1, raddr);
	ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
	ether_addr_copy(pwlanhdr->addr3, raddr);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie23a(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &action, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 4, (unsigned char *)&p2poui,
				     &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &oui_subtype, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &dialogToken, &pattrib->pktlen);

	/*	P2P IE Section. */

	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*	Commented by Albert 20101005 */
	/*	According to the P2P Specification, the P2P Invitation
		response frame should contain 5 P2P attributes */
	/*	1. Status */
	/*	2. Configuration Timeout */
	/*	3. Operating Channel	(Only GO) */
	/*	4. P2P Group BSSID	(Only GO) */
	/*	5. Channel List */

	/*	P2P Status */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_STATUS;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0001);
	p2pielen += 2;

	/*	Value: */
	/*	When status code is P2P_STATUS_FAIL_INFO_UNAVAILABLE. */
	/*	Sent the event receiving the P2P Invitation Req frame
		to DMP UI. */
	/*	DMP had to compare the MAC address to find out the profile. */
	/*	So, the WiFi driver will send the
		P2P_STATUS_FAIL_INFO_UNAVAILABLE to NB. */
	/*	If the UI found the corresponding profile, the WiFi driver
		sends the P2P Invitation Req */
	/*	to NB to rebuild the persistent group. */
	p2pie[p2pielen++] = status_code;

	/*	Configuration Timeout */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CONF_TIMEOUT;

	/*	Length: */
	*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	/*	2 seconds needed to be the P2P GO */
	p2pie[p2pielen++] = 200;
	/*	2 seconds needed to be the P2P Client */
	p2pie[p2pielen++] = 200;

	if (status_code == P2P_STATUS_SUCCESS) {
		if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
			/* The P2P Invitation request frame asks this
			   Wi-Fi device to be the P2P GO */
			/* In this case, the P2P Invitation response
			   frame should carry the two more P2P attributes. */
			/* First one is operating channel attribute. */
			/* Second one is P2P Group BSSID attribute. */

			/* Operating Channel */
			/* Type: */
			p2pie[p2pielen++] = P2P_ATTR_OPERATING_CH;

			/* Length: */
			*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0005);
			p2pielen += 2;

			/* Value: */
			/* Country String */
			p2pie[p2pielen++] = 'X';
			p2pie[p2pielen++] = 'X';

			/* The third byte should be set to 0x04. */
			/* Described in the "Operating Channel Attribute"
			   section. */
			p2pie[p2pielen++] = 0x04;

			/* Operating Class */
			/*	Copy from SD7 */
			p2pie[p2pielen++] = 0x51;

			/* Channel Number */
			/*	operating channel number */
			p2pie[p2pielen++] = pwdinfo->operating_channel;

			/*	P2P Group BSSID */
			/*	Type: */
			p2pie[p2pielen++] = P2P_ATTR_GROUP_BSSID;

			/*	Length: */
			*(u16*) (p2pie + p2pielen) = cpu_to_le16(ETH_ALEN);
			p2pielen += 2;

			/*	Value: */
			/*	P2P Device Address for GO */
			memcpy(p2pie + p2pielen, myid(&padapter->eeprompriv),
			       ETH_ALEN);
			p2pielen += ETH_ALEN;
		}

		/*	Channel List */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_CH_LIST;

		/*	Length: */
		/*  Country String(3) */
		/*  + (Operating Class (1) + Number of Channels(1)) *
		    Operation Classes (?) */
		/*  + number of channels in all classes */
		len_channellist_attr = 3 +
			(1 + 1) * (u16)pmlmeext->channel_list.reg_classes +
			get_reg_classes_full_count(pmlmeext->channel_list);

		*(u16*) (p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);
		p2pielen += 2;

		/*	Value: */
		/*	Country String */
		p2pie[p2pielen++] = 'X';
		p2pie[p2pielen++] = 'X';

		/* The third byte should be set to 0x04. */
		/* Described in the "Operating Channel Attribute" section. */
		p2pie[p2pielen++] = 0x04;

		/*	Channel Entry List */
		for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
			/*	Operating Class */
			p2pie[p2pielen++] =
				pmlmeext->channel_list.reg_class[j].reg_class;

			/*	Number of Channels */
			p2pie[p2pielen++] =
				pmlmeext->channel_list.reg_class[j].channels;

			/*	Channel List */
			for (i = 0;
			     i < pmlmeext->channel_list.reg_class[j].channels;
			     i++) {
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
			}
		}
	}

	pframe = rtw_set_ie23a(pframe, _VENDOR_SPECIFIC_IE_, p2pielen,
			       (unsigned char *)p2pie, &pattrib->pktlen);

#ifdef CONFIG_8723AU_P2P
	wfdielen = build_invitation_resp_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif /* CONFIG_8723AU_P2P */

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe23a(padapter, pmgntframe);

	return;
}

void issue_p2p_provision_request23a(struct rtw_adapter *padapter, u8 *pssid,
				    u8 ussidlen, u8 *pdev_raddr)
{
	unsigned char category = WLAN_CATEGORY_PUBLIC;
	u8 action = P2P_PUB_ACTION_ACTION;
	u8 dialogToken = 1;
	u32 p2poui = cpu_to_be32(P2POUI);
	u8 oui_subtype = P2P_PROVISION_DISC_REQ;
	u8 wpsie[100] = { 0x00 };
	u8 wpsielen = 0;
	u32 p2pielen = 0;
#ifdef CONFIG_8723AU_P2P
	u32 wfdielen = 0;
#endif /* CONFIG_8723AU_P2P */
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		return;

	DBG_8723A("[%s] In\n", __func__);
	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

	ether_addr_copy(pwlanhdr->addr1, pdev_raddr);
	ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
	ether_addr_copy(pwlanhdr->addr3, pdev_raddr);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie23a(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &action, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 4, (unsigned char *)&p2poui,
				     &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &oui_subtype, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &dialogToken, &pattrib->pktlen);

	p2pielen = build_prov_disc_request_p2p_ie23a(pwdinfo, pframe, pssid,
						     ussidlen, pdev_raddr);

	pframe += p2pielen;
	pattrib->pktlen += p2pielen;

	wpsielen = 0;
	/*	WPS OUI */
	*(u32*) (wpsie) = cpu_to_be32(WPSOUI);
	wpsielen += 4;

	/*	WPS version */
	/*	Type: */
	*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_VER1);
	wpsielen += 2;

	/*	Length: */
	*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0001);
	wpsielen += 2;

	/*	Value: */
	wpsie[wpsielen++] = WPS_VERSION_1;	/*	Version 1.0 */

	/*	Config Method */
	/*	Type: */
	*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_CONF_METHOD);
	wpsielen += 2;

	/*	Length: */
	*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0002);
	wpsielen += 2;

	/*	Value: */
	*(u16*) (wpsie + wpsielen) = cpu_to_be16(pwdinfo->tx_prov_disc_info.wps_config_method_request);
	wpsielen += 2;

	pframe = rtw_set_ie23a(pframe, _VENDOR_SPECIFIC_IE_, wpsielen,
			       (unsigned char *) wpsie, &pattrib->pktlen);

#ifdef CONFIG_8723AU_P2P
	wfdielen = build_provdisc_req_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif /* CONFIG_8723AU_P2P */

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe23a(padapter, pmgntframe);

	return;
}

static u8 is_matched_in_profilelist(u8 *peermacaddr,
				    struct profile_info *profileinfo)
{
	u8 i, match_result = 0;

	DBG_8723A("[%s] peermac = %.2X %.2X %.2X %.2X %.2X %.2X\n", __func__,
		  peermacaddr[0], peermacaddr[1], peermacaddr[2],
		  peermacaddr[3], peermacaddr[4], peermacaddr[5]);

	for (i = 0; i < P2P_MAX_PERSISTENT_GROUP_NUM; i++, profileinfo++) {
	       DBG_8723A("[%s] profileinfo_mac = %.2X %.2X %.2X %.2X %.2X "
			 "%.2X\n", __func__, profileinfo->peermac[0],
			 profileinfo->peermac[1], profileinfo->peermac[2],
			 profileinfo->peermac[3], profileinfo->peermac[4],
			 profileinfo->peermac[5]);
		if (ether_addr_equal(peermacaddr, profileinfo->peermac)) {
			match_result = 1;
			DBG_8723A("[%s] Match!\n", __func__);
			break;
		}
	}

	return match_result;
}

void issue_probersp23a_p2p23a(struct rtw_adapter *padapter, unsigned char *da)
{
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	unsigned char *mac;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u16 beacon_interval = 100;
	u16 capInfo = 0;
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	u8 wpsie[255] = { 0x00 };
	u32 wpsielen = 0, p2pielen = 0;
#ifdef CONFIG_8723AU_P2P
	u32 wfdielen = 0;
#endif /* CONFIG_8723AU_P2P */
	struct cfg80211_wifidirect_info *pcfg80211_wdinfo =
		&padapter->cfg80211_wdinfo;
	struct ieee80211_channel *ieee_ch =
		&pcfg80211_wdinfo->remain_on_ch_channel;
	u8 listen_channel =
		(u8)ieee80211_frequency_to_channel(ieee_ch->center_freq);

	/* DBG_8723A("%s\n", __func__); */

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
	{
		return;
	}

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	mac = myid(&padapter->eeprompriv);

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;
	ether_addr_copy(pwlanhdr->addr1, da);
	ether_addr_copy(pwlanhdr->addr2, mac);

	/*	Use the device address for BSSID field. */
	ether_addr_copy(pwlanhdr->addr3, mac);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(fctrl, WIFI_PROBERSP);

	pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = pattrib->hdrlen;
	pframe += pattrib->hdrlen;

	/* timestamp will be inserted by hardware */
	pframe += 8;
	pattrib->pktlen += 8;

	/*  beacon interval: 2 bytes */
	memcpy(pframe, (unsigned char *) &beacon_interval, 2);
	pframe += 2;
	pattrib->pktlen += 2;

	/*	capability info: 2 bytes */
	/*	ESS and IBSS bits must be 0 (defined in the 3.1.2.1.1 of
		WiFi Direct Spec) */
	capInfo |= cap_ShortPremble;
	capInfo |= cap_ShortSlot;

	memcpy(pframe, (unsigned char *) &capInfo, 2);
	pframe += 2;
	pattrib->pktlen += 2;

	/*  SSID */
	pframe = rtw_set_ie23a(pframe, _SSID_IE_, 7, pwdinfo->p2p_wildcard_ssid,
			       &pattrib->pktlen);

	/*  supported rates... */
	/*	Use the OFDM rate in the P2P probe response frame.
		(6(B), 9(B), 12, 18, 24, 36, 48, 54) */
	pframe = rtw_set_ie23a(pframe, _SUPPORTEDRATES_IE_, 8,
			       pwdinfo->support_rate, &pattrib->pktlen);

	/*  DS parameter set */
	if (wdev_to_priv(padapter->rtw_wdev)->p2p_enabled &&
	    listen_channel != 0) {
		pframe = rtw_set_ie23a(pframe, _DSSET_IE_, 1, (unsigned char *)
				       &listen_channel, &pattrib->pktlen);
	} else {
		pframe = rtw_set_ie23a(pframe, _DSSET_IE_, 1, (unsigned char *)
				       &pwdinfo->listen_channel,
				       &pattrib->pktlen);
	}

	if (wdev_to_priv(padapter->rtw_wdev)->p2p_enabled) {
		if (pmlmepriv->wps_probe_resp_ie &&
		    pmlmepriv->p2p_probe_resp_ie) {
			/* WPS IE */
			memcpy(pframe, pmlmepriv->wps_probe_resp_ie,
			       pmlmepriv->wps_probe_resp_ie_len);
			pattrib->pktlen += pmlmepriv->wps_probe_resp_ie_len;
			pframe += pmlmepriv->wps_probe_resp_ie_len;

			/* P2P IE */
			memcpy(pframe, pmlmepriv->p2p_probe_resp_ie,
			       pmlmepriv->p2p_probe_resp_ie_len);
			pattrib->pktlen += pmlmepriv->p2p_probe_resp_ie_len;
			pframe += pmlmepriv->p2p_probe_resp_ie_len;
		}
	} else {

		/*	Todo: WPS IE */
		/*	Noted by Albert 20100907 */
		/*	According to the WPS specification, all the WPS
			attribute is presented by Big Endian. */

		wpsielen = 0;
		/*	WPS OUI */
		*(u32*) (wpsie) = cpu_to_be32(WPSOUI);
		wpsielen += 4;

		/*	WPS version */
		/*	Type: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_VER1);
		wpsielen += 2;

		/*	Length: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0001);
		wpsielen += 2;

		/*	Value: */
		wpsie[wpsielen++] = WPS_VERSION_1;	/*	Version 1.0 */

		/*	WiFi Simple Config State */
		/*	Type: */
		*(u16*) (wpsie + wpsielen) =
			cpu_to_be16(WPS_ATTR_SIMPLE_CONF_STATE);
		wpsielen += 2;

		/*	Length: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0001);
		wpsielen += 2;

		/*	Value: */
		wpsie[wpsielen++] = WPS_WSC_STATE_NOT_CONFIG;

		/*	Response Type */
		/*	Type: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_RESP_TYPE);
		wpsielen += 2;

		/*	Length: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0001);
		wpsielen += 2;

		/*	Value: */
		wpsie[wpsielen++] = WPS_RESPONSE_TYPE_8021X;

		/*	UUID-E */
		/*	Type: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_UUID_E);
		wpsielen += 2;

		/*	Length: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0010);
		wpsielen += 2;

		/*	Value: */
		memcpy(wpsie + wpsielen, myid(&padapter->eeprompriv), ETH_ALEN);
		wpsielen += 0x10;

		/*	Manufacturer */
		/*	Type: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_MANUFACTURER);
		wpsielen += 2;

		/*	Length: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0007);
		wpsielen += 2;

		/*	Value: */
		memcpy(wpsie + wpsielen, "Realtek", 7);
		wpsielen += 7;

		/*	Model Name */
		/*	Type: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_MODEL_NAME);
		wpsielen += 2;

		/*	Length: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0006);
		wpsielen += 2;

		/*	Value: */
		memcpy(wpsie + wpsielen, "8192CU", 6);
		wpsielen += 6;

		/*	Model Number */
		/*	Type: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_MODEL_NUMBER);
		wpsielen += 2;

		/*	Length: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0001);
		wpsielen += 2;

		/*	Value: */
		wpsie[ wpsielen++ ] = 0x31;		/*	character 1 */

		/*	Serial Number */
		/*	Type: */
		*(u16*) (wpsie + wpsielen) =
			cpu_to_be16(WPS_ATTR_SERIAL_NUMBER);
		wpsielen += 2;

		/*	Length: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(ETH_ALEN);
		wpsielen += 2;

		/*	Value: */
		memcpy(wpsie + wpsielen, "123456", ETH_ALEN);
		wpsielen += ETH_ALEN;

		/*	Primary Device Type */
		/*	Type: */
		*(u16*) (wpsie + wpsielen) =
			cpu_to_be16(WPS_ATTR_PRIMARY_DEV_TYPE);
		wpsielen += 2;

		/*	Length: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0008);
		wpsielen += 2;

		/*	Value: */
		/*	Category ID */
		*(u16*) (wpsie + wpsielen) =
			cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA);
		wpsielen += 2;

		/*	OUI */
		*(u32*) (wpsie + wpsielen) = cpu_to_be32(WPSOUI);
		wpsielen += 4;

		/*	Sub Category ID */
		*(u16*) (wpsie + wpsielen) =
			cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
		wpsielen += 2;

		/*	Device Name */
		/*	Type: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_DEVICE_NAME);
		wpsielen += 2;

		/*	Length: */
		*(u16*) (wpsie + wpsielen) =
			cpu_to_be16(pwdinfo->device_name_len);
		wpsielen += 2;

		/*	Value: */
		if (pwdinfo->device_name_len) {
			memcpy(wpsie + wpsielen, pwdinfo->device_name,
			       pwdinfo->device_name_len);
			wpsielen += pwdinfo->device_name_len;
		}

		/*	Config Method */
		/*	Type: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_CONF_METHOD);
		wpsielen += 2;

		/*	Length: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0002);
		wpsielen += 2;

		/*	Value: */
		*(u16*) (wpsie + wpsielen) =
			cpu_to_be16(pwdinfo->supported_wps_cm);
		wpsielen += 2;

		pframe = rtw_set_ie23a(pframe, _VENDOR_SPECIFIC_IE_, wpsielen,
				       (unsigned char *)wpsie,
				       &pattrib->pktlen);

		p2pielen = build_probe_resp_p2p_ie23a(pwdinfo, pframe);
		pframe += p2pielen;
		pattrib->pktlen += p2pielen;
	}

#ifdef CONFIG_8723AU_P2P
	if (pwdinfo->wfd_info->wfd_enable) {
		wfdielen = build_probe_resp_wfd_ie(pwdinfo, pframe, 0);
		pframe += wfdielen;
		pattrib->pktlen += wfdielen;
	} else if (pmlmepriv->wfd_probe_resp_ie &&
		 pmlmepriv->wfd_probe_resp_ie_len > 0) {
		/* WFD IE */
		memcpy(pframe, pmlmepriv->wfd_probe_resp_ie,
		       pmlmepriv->wfd_probe_resp_ie_len);
		pattrib->pktlen += pmlmepriv->wfd_probe_resp_ie_len;
		pframe += pmlmepriv->wfd_probe_resp_ie_len;
	}
#endif /* CONFIG_8723AU_P2P */

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe23a(padapter, pmgntframe);

	return;
}

static int _issue23a_probereq_p2p(struct rtw_adapter *padapter, u8 *da,
				  int wait_ack)
{
	int ret = _FAIL;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	unsigned char *mac;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct wifidirect_info	*pwdinfo = &padapter->wdinfo;
	u8 wpsie[255] = {0x00}, p2pie[255] = {0x00};
	u16 wpsielen = 0, p2pielen = 0;
#ifdef CONFIG_8723AU_P2P
	u32 wfdielen = 0;
#endif /* CONFIG_8723AU_P2P */
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	mac = myid(&padapter->eeprompriv);

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

	if (da) {
		ether_addr_copy(pwlanhdr->addr1, da);
		ether_addr_copy(pwlanhdr->addr3, da);
	} else {
		if ((pwdinfo->p2p_info.scan_op_ch_only) ||
		    (pwdinfo->rx_invitereq_info.scan_op_ch_only)) {
			/*	This two flags will be set when this is
				only the P2P client mode. */
			ether_addr_copy(pwlanhdr->addr1,
					pwdinfo->p2p_peer_interface_addr);
			ether_addr_copy(pwlanhdr->addr3,
					pwdinfo->p2p_peer_interface_addr);
		} else {
			/*	broadcast probe request frame */
			ether_addr_copy(pwlanhdr->addr1, bc_addr);
			ether_addr_copy(pwlanhdr->addr3, bc_addr);
		}
	}
	ether_addr_copy(pwlanhdr->addr2, mac);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_PROBEREQ);

	pframe += sizeof (struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof (struct ieee80211_hdr_3addr);

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_PROVISION_DIS_REQ)) {
		pframe = rtw_set_ie23a(pframe, _SSID_IE_,
				    pwdinfo->tx_prov_disc_info.ssid.ssid_len,
				    pwdinfo->tx_prov_disc_info.ssid.ssid,
				    &pattrib->pktlen);
	} else {
		pframe = rtw_set_ie23a(pframe, _SSID_IE_,
				       P2P_WILDCARD_SSID_LEN,
				       pwdinfo->p2p_wildcard_ssid,
				       &pattrib->pktlen);
	}
	/*	Use the OFDM rate in the P2P probe request frame.
		(6(B), 9(B), 12(B), 24(B), 36, 48, 54) */
	pframe = rtw_set_ie23a(pframe, _SUPPORTEDRATES_IE_, 8,
			       pwdinfo->support_rate, &pattrib->pktlen);

	if (wdev_to_priv(padapter->rtw_wdev)->p2p_enabled) {
		if (pmlmepriv->wps_probe_req_ie &&
		    pmlmepriv->p2p_probe_req_ie) {
			/* WPS IE */
			memcpy(pframe, pmlmepriv->wps_probe_req_ie,
			       pmlmepriv->wps_probe_req_ie_len);
			pattrib->pktlen += pmlmepriv->wps_probe_req_ie_len;
			pframe += pmlmepriv->wps_probe_req_ie_len;

			/* P2P IE */
			memcpy(pframe, pmlmepriv->p2p_probe_req_ie,
			       pmlmepriv->p2p_probe_req_ie_len);
			pattrib->pktlen += pmlmepriv->p2p_probe_req_ie_len;
			pframe += pmlmepriv->p2p_probe_req_ie_len;
		}
	} else {

		/*	WPS IE */
		/*	Noted by Albert 20110221 */
		/*	According to the WPS specification, all the WPS
			attribute is presented by Big Endian. */

		wpsielen = 0;
		/*	WPS OUI */
		*(u32*) (wpsie) = cpu_to_be32(WPSOUI);
		wpsielen += 4;

		/*	WPS version */
		/*	Type: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_VER1);
		wpsielen += 2;

		/*	Length: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0001);
		wpsielen += 2;

		/*	Value: */
		wpsie[wpsielen++] = WPS_VERSION_1;	/*	Version 1.0 */

		if (pmlmepriv->wps_probe_req_ie == NULL) {
			/*	UUID-E */
			/*	Type: */
			*(u16*) (wpsie + wpsielen) =
				cpu_to_be16(WPS_ATTR_UUID_E);
			wpsielen += 2;

			/*	Length: */
			*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0010);
			wpsielen += 2;

			/*	Value: */
			memcpy(wpsie + wpsielen, myid(&padapter->eeprompriv),
			       ETH_ALEN);
			wpsielen += 0x10;

			/*	Config Method */
			/*	Type: */
			*(u16*) (wpsie + wpsielen) =
				cpu_to_be16(WPS_ATTR_CONF_METHOD);
			wpsielen += 2;

			/*	Length: */
			*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0002);
			wpsielen += 2;

			/*	Value: */
			*(u16*) (wpsie + wpsielen) =
				cpu_to_be16(pwdinfo->supported_wps_cm);
			wpsielen += 2;
		}

		/*	Device Name */
		/*	Type: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_DEVICE_NAME);
		wpsielen += 2;

		/*	Length: */
		*(u16*) (wpsie + wpsielen) =
			cpu_to_be16(pwdinfo->device_name_len);
		wpsielen += 2;

		/*	Value: */
		memcpy(wpsie + wpsielen, pwdinfo->device_name,
		       pwdinfo->device_name_len);
		wpsielen += pwdinfo->device_name_len;

		/*	Primary Device Type */
		/*	Type: */
		*(u16*) (wpsie + wpsielen) =
			cpu_to_be16(WPS_ATTR_PRIMARY_DEV_TYPE);
		wpsielen += 2;

		/*	Length: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0008);
		wpsielen += 2;

		/*	Value: */
		/*	Category ID */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_PDT_CID_RTK_WIDI);
		wpsielen += 2;

		/*	OUI */
		*(u32*) (wpsie + wpsielen) = cpu_to_be32(WPSOUI);
		wpsielen += 4;

		/*	Sub Category ID */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_PDT_SCID_RTK_DMP);
		wpsielen += 2;

		/*	Device Password ID */
		/*	Type: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_DEVICE_PWID);
		wpsielen += 2;

		/*	Length: */
		*(u16*) (wpsie + wpsielen) = cpu_to_be16(0x0002);
		wpsielen += 2;

		/*	Value: */
		/*	Registrar-specified */
		*(u16*) (wpsie + wpsielen) =
			cpu_to_be16(WPS_DPID_REGISTRAR_SPEC);
		wpsielen += 2;

		pframe = rtw_set_ie23a(pframe, _VENDOR_SPECIFIC_IE_, wpsielen,
				       (unsigned char *)wpsie,
				       &pattrib->pktlen);

		/*	P2P OUI */
		p2pielen = 0;
		p2pie[p2pielen++] = 0x50;
		p2pie[p2pielen++] = 0x6F;
		p2pie[p2pielen++] = 0x9A;
		p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

		/*	Commented by Albert 20110221 */
		/*	According to the P2P Specification, the probe request
			frame should contain 5 P2P attributes */
		/*	1. P2P Capability */
		/*	2. P2P Device ID if this probe request wants to
			find the specific P2P device */
		/*	3. Listen Channel */
		/*	4. Extended Listen Timing */
		/*	5. Operating Channel if this WiFi is working as
			the group owner now */

		/*	P2P Capability */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_CAPABILITY;

		/*	Length: */
		*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0002);
		p2pielen += 2;

		/*	Value: */
		/*	Device Capability Bitmap, 1 byte */
		p2pie[p2pielen++] = DMP_P2P_DEVCAP_SUPPORT;

		/*	Group Capability Bitmap, 1 byte */
		if (pwdinfo->persistent_supported)
			p2pie[p2pielen++] = P2P_GRPCAP_PERSISTENT_GROUP |
				DMP_P2P_GRPCAP_SUPPORT;
		else
			p2pie[p2pielen++] = DMP_P2P_GRPCAP_SUPPORT;

		/*	Listen Channel */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_LISTEN_CH;

		/*	Length: */
		*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0005);
		p2pielen += 2;

		/*	Value: */
		/*	Country String */
		p2pie[p2pielen++] = 'X';
		p2pie[p2pielen++] = 'X';

		/* The third byte should be set to 0x04. */
		/* Described in the "Operating Channel Attribute" section. */
		p2pie[p2pielen++] = 0x04;

		/*	Operating Class */
		p2pie[p2pielen++] = 0x51;	/*	Copy from SD7 */

		/*	Channel Number */
		/*	listen channel */
		p2pie[p2pielen++] = pwdinfo->listen_channel;

		/*	Extended Listen Timing */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_EX_LISTEN_TIMING;

		/*	Length: */
		*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0004);
		p2pielen += 2;

		/*	Value: */
		/*	Availability Period */
		*(u16*) (p2pie + p2pielen) = cpu_to_le16(0xFFFF);
		p2pielen += 2;

		/*	Availability Interval */
		*(u16*) (p2pie + p2pielen) = cpu_to_le16(0xFFFF);
		p2pielen += 2;

		if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
			/* Operating Channel (if this WiFi is working as
			   the group owner now) */
			/* Type: */
			p2pie[p2pielen++] = P2P_ATTR_OPERATING_CH;

			/*	Length: */
			*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0005);
			p2pielen += 2;

			/*	Value: */
			/*	Country String */
			p2pie[p2pielen++] = 'X';
			p2pie[p2pielen++] = 'X';

			/* The third byte should be set to 0x04. */
			/* Described in the "Operating Channel Attribute"
			   section. */
			p2pie[p2pielen++] = 0x04;

			/*	Operating Class */
			p2pie[p2pielen++] = 0x51;	/*	Copy from SD7 */

			/*	Channel Number */
			/*	operating channel number */
			p2pie[p2pielen++] = pwdinfo->operating_channel;
		}

		pframe = rtw_set_ie23a(pframe, _VENDOR_SPECIFIC_IE_, p2pielen,
				       (unsigned char *)p2pie,
				       &pattrib->pktlen);

		if (pmlmepriv->wps_probe_req_ie) {
			/* WPS IE */
			memcpy(pframe, pmlmepriv->wps_probe_req_ie,
			       pmlmepriv->wps_probe_req_ie_len);
			pattrib->pktlen += pmlmepriv->wps_probe_req_ie_len;
			pframe += pmlmepriv->wps_probe_req_ie_len;
		}
	}

#ifdef CONFIG_8723AU_P2P
	if (pwdinfo->wfd_info->wfd_enable) {
		wfdielen = build_probe_req_wfd_ie(pwdinfo, pframe);
		pframe += wfdielen;
		pattrib->pktlen += wfdielen;
	} else if (pmlmepriv->wfd_probe_req_ie &&
		   pmlmepriv->wfd_probe_req_ie_len>0) {
		/* WFD IE */
		memcpy(pframe, pmlmepriv->wfd_probe_req_ie,
		       pmlmepriv->wfd_probe_req_ie_len);
		pattrib->pktlen += pmlmepriv->wfd_probe_req_ie_len;
		pframe += pmlmepriv->wfd_probe_req_ie_len;
	}
#endif /* CONFIG_8723AU_P2P */

	pattrib->last_txcmdsz = pattrib->pktlen;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("issuing probe_req, tx_len =%d\n", pattrib->last_txcmdsz));

	if (wait_ack) {
		ret = dump_mgntframe23a_and_wait_ack23a(padapter, pmgntframe);
	} else {
		dump_mgntframe23a(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

inline void issue23a_probereq_p2p(struct rtw_adapter *adapter, u8 *da)
{
	_issue23a_probereq_p2p(adapter, da, false);
}

int issue23a_probereq_p2p_ex(struct rtw_adapter *adapter, u8 *da,
			     int try_cnt, int wait_ms)
{
	int ret;
	int i = 0;
	unsigned long start = jiffies;

	do {
		ret = _issue23a_probereq_p2p(adapter, da,
					     wait_ms > 0 ? true : false);

		i++;

		if (adapter->bDriverStopped || adapter->bSurpriseRemoved)
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			msleep(wait_ms);

	} while ((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		goto exit;
	}

	if (try_cnt && wait_ms) {
		if (da)
			DBG_8723A(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d "
				  "in %u ms\n", FUNC_ADPT_ARG(adapter),
				  MAC_ARG(da), rtw_get_oper_ch23a(adapter),
				  ret == _SUCCESS?", acked":"", i, try_cnt,
				  jiffies_to_msecs(jiffies - start));
		else
			DBG_8723A(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				  FUNC_ADPT_ARG(adapter),
				  rtw_get_oper_ch23a(adapter),
				  ret == _SUCCESS?", acked":"", i, try_cnt,
				  jiffies_to_msecs(jiffies - start));
	}
exit:
	return ret;
}

#endif /* CONFIG_8723AU_P2P */

static s32 rtw_action_public_decache(struct recv_frame *recv_frame, s32 token)
{
	struct rtw_adapter *adapter = recv_frame->adapter;
	struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;
	struct sk_buff *skb = recv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u16 seq_ctrl;

	seq_ctrl = ((recv_frame->attrib.seq_num&0xffff) << 4) |
		(recv_frame->attrib.frag_num & 0xf);

	if (ieee80211_has_retry(hdr->frame_control)) {
		if (token >= 0) {
			if ((seq_ctrl == mlmeext->action_public_rxseq) &&
			    (token == mlmeext->action_public_dialog_token)) {
				DBG_8723A(FUNC_ADPT_FMT" seq_ctrl = 0x%x, "
					  "rxseq = 0x%x, token:%d\n",
					  FUNC_ADPT_ARG(adapter), seq_ctrl,
					  mlmeext->action_public_rxseq, token);
				return _FAIL;
			}
		} else {
			if (seq_ctrl == mlmeext->action_public_rxseq) {
				DBG_8723A(FUNC_ADPT_FMT" seq_ctrl = 0x%x, "
					  "rxseq = 0x%x\n",
					  FUNC_ADPT_ARG(adapter), seq_ctrl,
					  mlmeext->action_public_rxseq);
				return _FAIL;
			}
		}
	}

	mlmeext->action_public_rxseq = seq_ctrl;

	if (token >= 0)
		mlmeext->action_public_dialog_token = token;

	return _SUCCESS;
}

static unsigned int on_action_public23a_p2p(struct recv_frame *precv_frame)
{
	struct sk_buff *skb = precv_frame->pkt;
	u8 *pframe = skb->data;
	u8 *frame_body;
	u8 dialogToken = 0;
#ifdef CONFIG_8723AU_P2P
	struct rtw_adapter *padapter = precv_frame->adapter;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	uint len = skb->len;
	u8 *p2p_ie;
	u32	p2p_ielen;
	struct	wifidirect_info	*pwdinfo = &padapter->wdinfo;
	u8	result = P2P_STATUS_SUCCESS;
#endif /* CONFIG_8723AU_P2P */

	frame_body = (unsigned char *)
		(pframe + sizeof(struct ieee80211_hdr_3addr));

	dialogToken = frame_body[7];

	if (rtw_action_public_decache(precv_frame, dialogToken) == _FAIL)
		return _FAIL;

#ifdef CONFIG_8723AU_P2P
	del_timer_sync(&pwdinfo->reset_ch_sitesurvey);
	if (wdev_to_priv(padapter->rtw_wdev)->p2p_enabled) {
		rtw_cfg80211_rx_p2p_action_public(padapter, pframe, len);
	} else {
		/*	Do nothing if the driver doesn't enable the P2P function. */
		if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) || rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE))
			return _SUCCESS;

		len -= sizeof(struct ieee80211_hdr_3addr);

		switch (frame_body[ 6 ])/* OUI Subtype */
		{
			case P2P_GO_NEGO_REQ:
				DBG_8723A("[%s] Got GO Nego Req Frame\n", __func__);
				memset(&pwdinfo->groupid_info, 0x00, sizeof(struct group_id_info));

				if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_RX_PROVISION_DIS_REQ))
				{
					rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
				}

				if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_FAIL))
				{
					/*	Commented by Albert 20110526 */
					/*	In this case, this means the previous nego fail doesn't be reset yet. */
					del_timer_sync(&pwdinfo->restore_p2p_state_timer);
					/*	Restore the previous p2p state */
					rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
					DBG_8723A("[%s] Restore the previous p2p state to %d\n", __func__, rtw_p2p_state(pwdinfo));
				}

				/*	Commented by Kurt 20110902 */
				/* Add if statement to avoid receiving duplicate prov disc req. such that pre_p2p_state would be covered. */
				if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING))
					rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));

				/*	Commented by Kurt 20120113 */
				/*	Get peer_dev_addr here if peer doesn't issue prov_disc frame. */
				if (is_zero_ether_addr(pwdinfo->rx_prov_disc_info.peerDevAddr))
					ether_addr_copy(pwdinfo->rx_prov_disc_info.peerDevAddr, hdr->addr2);

				result = process_p2p_group_negotation_req23a(pwdinfo, frame_body, len);
				issue_p2p_GO_response(padapter, hdr->addr2,
						      frame_body, len, result);

				/*	Commented by Albert 20110718 */
				/*	No matter negotiating or negotiation failure, the driver should set up the restore P2P state timer. */
				mod_timer(&pwdinfo->restore_p2p_state_timer,
					  jiffies + msecs_to_jiffies(5000));
				break;

			case P2P_GO_NEGO_RESP:
				DBG_8723A("[%s] Got GO Nego Resp Frame\n", __func__);

				if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING))
				{
					/*	Commented by Albert 20110425 */
					/*	The restore timer is enabled when issuing the nego request frame of rtw_p2p_connect function. */
					del_timer_sync(&pwdinfo->restore_p2p_state_timer);
					pwdinfo->nego_req_info.benable = false;
					result = process_p2p_group_negotation_resp23a(pwdinfo, frame_body, len);
					issue_p2p_GO_confirm(pwdinfo->padapter,
							     hdr->addr2,
							     result);
					if (result == P2P_STATUS_SUCCESS) {
						if (rtw_p2p_role(pwdinfo) ==
						    P2P_ROLE_CLIENT) {
							pwdinfo->p2p_info.operation_ch[ 0 ] = pwdinfo->peer_operating_ch;
							pwdinfo->p2p_info.scan_op_ch_only = 1;
							mod_timer(&pwdinfo->reset_ch_sitesurvey2, jiffies + msecs_to_jiffies(P2P_RESET_SCAN_CH));
						}
					}

					/*	Reset the dialog token for group negotiation frames. */
					pwdinfo->negotiation_dialog_token = 1;

					if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_FAIL))
					{
						mod_timer(&pwdinfo->restore_p2p_state_timer, jiffies + msecs_to_jiffies(5000));
					}
				} else {
					DBG_8723A("[%s] Skipped GO Nego Resp Frame (p2p_state != P2P_STATE_GONEGO_ING)\n", __func__);
				}

				break;

			case P2P_GO_NEGO_CONF:

				DBG_8723A("[%s] Got GO Nego Confirm Frame\n", __func__);
				result = process_p2p_group_negotation_confirm23a(pwdinfo, frame_body, len);
				if (P2P_STATUS_SUCCESS == result)
				{
					if (rtw_p2p_role(pwdinfo) == P2P_ROLE_CLIENT)
					{
						pwdinfo->p2p_info.operation_ch[ 0 ] = pwdinfo->peer_operating_ch;
						pwdinfo->p2p_info.scan_op_ch_only = 1;
						mod_timer(&pwdinfo->reset_ch_sitesurvey2, jiffies + msecs_to_jiffies(P2P_RESET_SCAN_CH));
					}
				}
				break;

			case P2P_INVIT_REQ:
				/*	Added by Albert 2010/10/05 */
				/*	Received the P2P Invite Request frame. */

				DBG_8723A("[%s] Got invite request frame!\n", __func__);
				if ((p2p_ie = rtw_get_p2p_ie23a(frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &p2p_ielen)))
				{
					/*	Parse the necessary information from the P2P Invitation Request frame. */
					/*	For example: The MAC address of sending this P2P Invitation Request frame. */
					u32	attr_contentlen = 0;
					u8	status_code = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
					struct group_id_info group_id;
					u8	invitation_flag = 0;

					rtw_get_p2p_attr23a_content(p2p_ie, p2p_ielen, P2P_ATTR_INVITATION_FLAGS, &invitation_flag, &attr_contentlen);
					if (attr_contentlen)
					{

						rtw_get_p2p_attr23a_content(p2p_ie, p2p_ielen, P2P_ATTR_GROUP_BSSID, pwdinfo->p2p_peer_interface_addr, &attr_contentlen);
						/*	Commented by Albert 20120510 */
						/*	Copy to the pwdinfo->p2p_peer_interface_addr. */
						/*	So that the WFD UI (or Sigma) can get the peer interface address by using the following command. */
						/*	#> iwpriv wlan0 p2p_get peer_ifa */
						/*	After having the peer interface address, the sigma can find the correct conf file for wpa_supplicant. */

						if (attr_contentlen)
						{
							DBG_8723A("[%s] GO's BSSID = %.2X %.2X %.2X %.2X %.2X %.2X\n", __func__,
									pwdinfo->p2p_peer_interface_addr[0], pwdinfo->p2p_peer_interface_addr[1],
									pwdinfo->p2p_peer_interface_addr[2], pwdinfo->p2p_peer_interface_addr[3],
									pwdinfo->p2p_peer_interface_addr[4], pwdinfo->p2p_peer_interface_addr[5]);
						}

						if (invitation_flag & P2P_INVITATION_FLAGS_PERSISTENT)
						{
							/*	Re-invoke the persistent group. */

							memset(&group_id, 0x00, sizeof(struct group_id_info));
							rtw_get_p2p_attr23a_content(p2p_ie, p2p_ielen, P2P_ATTR_GROUP_ID, (u8*) &group_id, &attr_contentlen);
							if (attr_contentlen) {
								if (ether_addr_equal(group_id.go_device_addr, myid(&padapter->eeprompriv))) {
									/*	The p2p device sending this p2p invitation request wants this Wi-Fi device to be the persistent GO. */
									rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_GO);
									rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
									status_code = P2P_STATUS_SUCCESS;
								}
								else
								{
									/*	The p2p device sending this p2p invitation request wants to be the persistent GO. */
									if (is_matched_in_profilelist(pwdinfo->p2p_peer_interface_addr, &pwdinfo->profileinfo[ 0 ]))
									{
										u8 operatingch_info[5] = { 0x00 };
										if (rtw_get_p2p_attr23a_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, operatingch_info, &attr_contentlen))
										{
											if (rtw_ch_set_search_ch23a(padapter->mlmeextpriv.channel_set, (u32)operatingch_info[4]))
											{
												/*	The operating channel is acceptable for this device. */
												pwdinfo->rx_invitereq_info.operation_ch[0]= operatingch_info[4];
												pwdinfo->rx_invitereq_info.scan_op_ch_only = 1;
												mod_timer(&pwdinfo->reset_ch_sitesurvey, jiffies + msecs_to_jiffies(P2P_RESET_SCAN_CH));
												rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_MATCH);
												rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
												status_code = P2P_STATUS_SUCCESS;
												}
											else
											{
												/*	The operating channel isn't supported by this device. */
												rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_DISMATCH);
												rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
												status_code = P2P_STATUS_FAIL_NO_COMMON_CH;
												mod_timer(&pwdinfo->restore_p2p_state_timer, jiffies + msecs_to_jiffies(3000));
											}
										}
										else {
											/*	Commented by Albert 20121130 */
											/*	Intel will use the different P2P IE to store the operating channel information */
											/*	Workaround for Intel WiDi 3.5 */
											rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_MATCH);
											rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
											status_code = P2P_STATUS_SUCCESS;
										}
									}
									else
									{
										rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_DISMATCH);

										status_code = P2P_STATUS_FAIL_UNKNOWN_P2PGROUP;
									}
								}
							}
							else
							{
								DBG_8723A("[%s] P2P Group ID Attribute NOT FOUND!\n", __func__);
								status_code = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
							}
						}
						else
						{
							/*	Received the invitation to join a P2P group. */

							memset(&group_id, 0x00, sizeof(struct group_id_info));
							rtw_get_p2p_attr23a_content(p2p_ie, p2p_ielen, P2P_ATTR_GROUP_ID, (u8*) &group_id, &attr_contentlen);
							if (attr_contentlen)
							{
								if (ether_addr_equal(group_id.go_device_addr, myid(&padapter->eeprompriv))) {
									/*	In this case, the GO can't be myself. */
									rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_DISMATCH);
									status_code = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
								}
								else
								{
									/*	The p2p device sending this p2p invitation request wants to join an existing P2P group */
									/*	Commented by Albert 2012/06/28 */
									/*	In this case, this Wi-Fi device should use the iwpriv command to get the peer device address. */
									/*	The peer device address should be the destination address for the provisioning discovery request. */
									/*	Then, this Wi-Fi device should use the iwpriv command to get the peer interface address. */
									/*	The peer interface address should be the address for WPS mac address */
									ether_addr_copy(pwdinfo->p2p_peer_device_addr, group_id.go_device_addr);
									rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
									rtw_p2p_set_state(pwdinfo, P2P_STATE_RECV_INVITE_REQ_JOIN);
									status_code = P2P_STATUS_SUCCESS;
								}
							}
							else
							{
								DBG_8723A("[%s] P2P Group ID Attribute NOT FOUND!\n", __func__);
								status_code = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
							}
						}
					}
					else
					{
						DBG_8723A("[%s] P2P Invitation Flags Attribute NOT FOUND!\n", __func__);
						status_code = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
					}

					DBG_8723A("[%s] status_code = %d\n", __func__, status_code);

					pwdinfo->inviteresp_info.token = frame_body[ 7 ];
					issue_p2p_invitation_response23a(padapter, hdr->addr2, pwdinfo->inviteresp_info.token, status_code);
				}
				break;

			case P2P_INVIT_RESP:
			{
				u8	attr_content = 0x00;
				u32	attr_contentlen = 0;

				DBG_8723A("[%s] Got invite response frame!\n", __func__);
				del_timer_sync(&pwdinfo->restore_p2p_state_timer);
				if ((p2p_ie = rtw_get_p2p_ie23a(frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &p2p_ielen)))
				{
					rtw_get_p2p_attr23a_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, &attr_content, &attr_contentlen);

					if (attr_contentlen == 1)
					{
						DBG_8723A("[%s] Status = %d\n", __func__, attr_content);
						pwdinfo->invitereq_info.benable = false;

						if (attr_content == P2P_STATUS_SUCCESS)
						{
							if (ether_addr_equal(pwdinfo->invitereq_info.go_bssid, myid(&padapter->eeprompriv))) {
								rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
							}
							else
							{
								rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
							}
							rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_INVITE_RESP_OK);
						}
						else
						{
							rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
							rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_INVITE_RESP_FAIL);
						}
					}
					else
					{
						rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
						rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_INVITE_RESP_FAIL);
					}
				}
				else
				{
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
					rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_INVITE_RESP_FAIL);
				}

				if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_RX_INVITE_RESP_FAIL)) {
					mod_timer(&pwdinfo->restore_p2p_state_timer, jiffies + msecs_to_jiffies(5000));
				}
				break;
			}
			case P2P_DEVDISC_REQ:

				process_p2p_devdisc_req23a(pwdinfo, pframe, len);

				break;

			case P2P_DEVDISC_RESP:

				process_p2p_devdisc_resp23a(pwdinfo, pframe, len);

				break;

			case P2P_PROVISION_DISC_REQ:
				DBG_8723A("[%s] Got Provisioning Discovery Request Frame\n", __func__);
				process_p2p_provdisc_req23a(pwdinfo, pframe, len);
				ether_addr_copy(pwdinfo->rx_prov_disc_info.peerDevAddr, hdr->addr2);

				/* 20110902 Kurt */
				/* Add the following statement to avoid receiving duplicate prov disc req. such that pre_p2p_state would be covered. */
				if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_RX_PROVISION_DIS_REQ))
					rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));

				rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_PROVISION_DIS_REQ);
				mod_timer(&pwdinfo->restore_p2p_state_timer,
					  jiffies + msecs_to_jiffies(P2P_PROVISION_TIMEOUT));
				break;

			case P2P_PROVISION_DISC_RESP:
				/*	Commented by Albert 20110707 */
				/*	Should we check the pwdinfo->tx_prov_disc_info.bsent flag here?? */
				DBG_8723A("[%s] Got Provisioning Discovery Response Frame\n", __func__);
				/*	Commented by Albert 20110426 */
				/*	The restore timer is enabled when issuing the provisioing request frame in rtw_p2p_prov_disc function. */
				del_timer_sync(&pwdinfo->restore_p2p_state_timer);
				rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_PROVISION_DIS_RSP);
				process_p2p_provdisc_resp23a(pwdinfo, pframe);
				mod_timer(&pwdinfo->restore_p2p_state_timer,
					  jiffies + msecs_to_jiffies(P2P_PROVISION_TIMEOUT));
				break;

		}
	}
#endif /* CONFIG_8723AU_P2P */

	return _SUCCESS;
}

static unsigned int on_action_public23a_vendor(struct recv_frame *precv_frame)
{
	unsigned int ret = _FAIL;
	struct sk_buff *skb = precv_frame->pkt;
	u8 *pframe = skb->data;
	u8 *frame_body = pframe + sizeof(struct ieee80211_hdr_3addr);

	if (!memcmp(frame_body + 2, P2P_OUI23A, 4)) {
		ret = on_action_public23a_p2p(precv_frame);
	}

	return ret;
}

static unsigned int
on_action_public23a_default(struct recv_frame *precv_frame, u8 action)
{
	unsigned int ret = _FAIL;
	struct sk_buff *skb = precv_frame->pkt;
	u8 *pframe = skb->data;
	uint frame_len = skb->len;
	u8 *frame_body = pframe + sizeof(struct ieee80211_hdr_3addr);
	u8 token;
	struct rtw_adapter *adapter = precv_frame->adapter;
	int cnt = 0;
	char msg[64];

	token = frame_body[2];

	if (rtw_action_public_decache(precv_frame, token) == _FAIL)
		goto exit;

	cnt += sprintf((msg+cnt), "%s(token:%u)",
		       action_public_str23a(action), token);
	rtw_cfg80211_rx_action(adapter, pframe, frame_len, msg);

	ret = _SUCCESS;

exit:
	return ret;
}

unsigned int on_action_public23a(struct rtw_adapter *padapter,
				 struct recv_frame *precv_frame)
{
	unsigned int ret = _FAIL;
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u8 *pframe = skb->data;
	u8 *frame_body = pframe + sizeof(struct ieee80211_hdr_3addr);
	u8 category, action;

	/* check RA matches or not */
	if (!ether_addr_equal(myid(&padapter->eeprompriv), hdr->addr1))
		goto exit;

	category = frame_body[0];
	if (category != WLAN_CATEGORY_PUBLIC)
		goto exit;

	action = frame_body[1];
	switch (action) {
	case ACT_PUBLIC_VENDOR:
		ret = on_action_public23a_vendor(precv_frame);
		break;
	default:
		ret = on_action_public23a_default(precv_frame, action);
		break;
	}

exit:
	return ret;
}

unsigned int OnAction23a_ht(struct rtw_adapter *padapter,
			    struct recv_frame *precv_frame)
{
	return _SUCCESS;
}

unsigned int OnAction23a_wmm(struct rtw_adapter *padapter,
			     struct recv_frame *precv_frame)
{
	return _SUCCESS;
}

unsigned int OnAction23a_p2p(struct rtw_adapter *padapter,
			     struct recv_frame *precv_frame)
{
#ifdef CONFIG_8723AU_P2P
	u8 *frame_body;
	u8 category, OUI_Subtype, dialogToken = 0;
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u8 *pframe = skb->data;
	uint len = skb->len;
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	DBG_8723A("%s\n", __func__);

	/* check RA matches or not */
	if (!ether_addr_equal(myid(&padapter->eeprompriv), hdr->addr1))
		return _SUCCESS;

	frame_body = (unsigned char *)
		(pframe + sizeof(struct ieee80211_hdr_3addr));

	category = frame_body[0];
	if (category != WLAN_CATEGORY_VENDOR_SPECIFIC)
		return _SUCCESS;

	if (cpu_to_be32(*((u32*) (frame_body + 1))) != P2POUI)
		return _SUCCESS;

	if (wdev_to_priv(padapter->rtw_wdev)->p2p_enabled) {
		rtw_cfg80211_rx_action_p2p(padapter, pframe, len);
		return _SUCCESS;
	} else {
		len -= sizeof(struct ieee80211_hdr_3addr);
		OUI_Subtype = frame_body[5];
		dialogToken = frame_body[6];

		switch (OUI_Subtype)
		{
		case P2P_NOTICE_OF_ABSENCE:
			break;

		case P2P_PRESENCE_REQUEST:
			process_p2p_presence_req23a(pwdinfo, pframe, len);
			break;

		case P2P_PRESENCE_RESPONSE:
			break;

		case P2P_GO_DISC_REQUEST:
			break;

		default:
			break;
		}
	}
#endif /* CONFIG_8723AU_P2P */

	return _SUCCESS;
}

unsigned int OnAction23a(struct rtw_adapter *padapter,
			 struct recv_frame *precv_frame)
{
	int i;
	unsigned char	category;
	struct action_handler *ptable;
	unsigned char	*frame_body;
	struct sk_buff *skb = precv_frame->pkt;
	u8 *pframe = skb->data;

	frame_body = (unsigned char *)
		(pframe + sizeof(struct ieee80211_hdr_3addr));

	category = frame_body[0];

	for (i = 0;
	     i < sizeof(OnAction23a_tbl) / sizeof(struct action_handler); i++) {
		ptable = &OnAction23a_tbl[i];

		if (category == ptable->num)
			ptable->func(padapter, precv_frame);
	}

	return _SUCCESS;
}

unsigned int DoReserved23a(struct rtw_adapter *padapter,
			struct recv_frame *precv_frame)
{
	return _SUCCESS;
}

struct xmit_frame *alloc_mgtxmitframe23a(struct xmit_priv *pxmitpriv)
{
	struct xmit_frame *pmgntframe;
	struct xmit_buf *pxmitbuf;

	pmgntframe = rtw_alloc_xmitframe23a_ext(pxmitpriv);

	if (!pmgntframe) {
		DBG_8723A(FUNC_ADPT_FMT" alloc xmitframe fail\n",
			  FUNC_ADPT_ARG(pxmitpriv->adapter));
		goto exit;
	}

	pxmitbuf = rtw_alloc_xmitbuf23a_ext(pxmitpriv);
	if (!pxmitbuf) {
		DBG_8723A(FUNC_ADPT_FMT" alloc xmitbuf fail\n",
			  FUNC_ADPT_ARG(pxmitpriv->adapter));
		rtw_free_xmitframe23a(pxmitpriv, pmgntframe);
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

/****************************************************************************

Following are some TX fuctions for WiFi MLME

*****************************************************************************/

void update_mgnt_tx_rate23a(struct rtw_adapter *padapter, u8 rate)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	pmlmeext->tx_rate = rate;
	DBG_8723A("%s(): rate = %x\n", __func__, rate);
}

void update_mgntframe_attrib23a(struct rtw_adapter *padapter,
				struct pkt_attrib *pattrib)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	memset((u8 *)pattrib, 0, sizeof(struct pkt_attrib));

	pattrib->hdrlen = 24;
	pattrib->nr_frags = 1;
	pattrib->priority = 7;
	pattrib->mac_id = 0;
	pattrib->qsel = 0x12;

	pattrib->pktlen = 0;

	if (pmlmeext->cur_wireless_mode & WIRELESS_11B)
		pattrib->raid = 6;/* b mode */
	else
		pattrib->raid = 5;/* a/g mode */

	pattrib->encrypt = _NO_PRIVACY_;
	pattrib->bswenc = false;

	pattrib->qos_en = false;
	pattrib->ht_en = false;
	pattrib->bwmode = HT_CHANNEL_WIDTH_20;
	pattrib->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	pattrib->sgi = false;

	pattrib->seqnum = pmlmeext->mgnt_seq;

	pattrib->retry_ctrl = true;
}

void dump_mgntframe23a(struct rtw_adapter *padapter,
		       struct xmit_frame *pmgntframe)
{
	if (padapter->bSurpriseRemoved == true ||
	    padapter->bDriverStopped == true)
		return;

	rtw_hal_mgnt_xmit23a(padapter, pmgntframe);
}

s32 dump_mgntframe23a_and_wait(struct rtw_adapter *padapter,
			       struct xmit_frame *pmgntframe, int timeout_ms)
{
	s32 ret = _FAIL;
	unsigned long irqL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct xmit_buf *pxmitbuf = pmgntframe->pxmitbuf;
	struct submit_ctx sctx;

	if (padapter->bSurpriseRemoved == true ||
	    padapter->bDriverStopped == true)
		return ret;

	rtw_sctx_init23a(&sctx, timeout_ms);
	pxmitbuf->sctx = &sctx;

	ret = rtw_hal_mgnt_xmit23a(padapter, pmgntframe);

	if (ret == _SUCCESS)
		ret = rtw_sctx_wait23a(&sctx);

	spin_lock_irqsave(&pxmitpriv->lock_sctx, irqL);
	pxmitbuf->sctx = NULL;
	spin_unlock_irqrestore(&pxmitpriv->lock_sctx, irqL);

	return ret;
}

s32 dump_mgntframe23a_and_wait_ack23a(struct rtw_adapter *padapter,
				      struct xmit_frame *pmgntframe)
{
	s32 ret = _FAIL;
	u32 timeout_ms = 500;/*   500ms */
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	if (padapter->bSurpriseRemoved == true ||
	    padapter->bDriverStopped == true)
		return -1;

	mutex_lock(&pxmitpriv->ack_tx_mutex);
	pxmitpriv->ack_tx = true;

	pmgntframe->ack_report = 1;
	if (rtw_hal_mgnt_xmit23a(padapter, pmgntframe) == _SUCCESS) {
		ret = rtw_ack_tx_wait23a(pxmitpriv, timeout_ms);
	}

	pxmitpriv->ack_tx = false;
	mutex_unlock(&pxmitpriv->ack_tx_mutex);

	return ret;
}

static int update_hidden_ssid(u8 *ies, u32 ies_len, u8 hidden_ssid_mode)
{
	u8 *ssid_ie;
	int ssid_len_ori;
	int len_diff = 0;
	u8 *next_ie;
	u32 remain_len;

	ssid_ie = rtw_get_ie23a(ies,  WLAN_EID_SSID, &ssid_len_ori, ies_len);

	/* DBG_8723A("%s hidden_ssid_mode:%u, ssid_ie:%p, ssid_len_ori:%d\n",
	   __func__, hidden_ssid_mode, ssid_ie, ssid_len_ori); */

	if (ssid_ie && ssid_len_ori > 0) {
		switch (hidden_ssid_mode)
		{
		case 1:
			next_ie = ssid_ie + 2 + ssid_len_ori;
			remain_len = 0;

			remain_len = ies_len -(next_ie-ies);

			ssid_ie[1] = 0;
			memcpy(ssid_ie+2, next_ie, remain_len);
			len_diff -= ssid_len_ori;

			break;
		case 2:
			memset(&ssid_ie[2], 0, ssid_len_ori);
			break;
		default:
			break;
		}
	}

	return len_diff;
}

void issue_beacon23a(struct rtw_adapter *padapter, int timeout_ms)
{
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	unsigned int rate_len;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *cur_network = &pmlmeinfo->network;
	u8 bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
#ifdef CONFIG_8723AU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif /* CONFIG_8723AU_P2P */
	u8 *wps_ie;
	u32 wps_ielen;
	u8 sr = 0;
	int len_diff;

	/* DBG_8723A("%s\n", __func__); */

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL) {
		DBG_8723A("%s, alloc mgnt frame fail\n", __func__);
		return;
	}
#ifdef CONFIG_8723AU_AP_MODE
	spin_lock_bh(&pmlmepriv->bcn_update_lock);
#endif

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);
	pattrib->qsel = 0x10;

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

	ether_addr_copy(pwlanhdr->addr1, bc_addr);
	ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
	ether_addr_copy(pwlanhdr->addr3, get_my_bssid23a(cur_network));

	SetSeqNum(pwlanhdr, 0 /*pmlmeext->mgnt_seq*/);
	/* pmlmeext->mgnt_seq++; */
	SetFrameSubType(pframe, WIFI_BEACON);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE) {
		/* DBG_8723A("ie len =%d\n", cur_network->IELength); */
#ifdef CONFIG_8723AU_P2P
		/*  for P2P : Primary Device Type & Device Name */
		u32 insert_len = 0;
		wps_ie = rtw_get_wps_ie23a(cur_network->IEs + _FIXED_IE_LENGTH_,
					   cur_network->IELength -
					   _FIXED_IE_LENGTH_, NULL, &wps_ielen);

		if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO) && wps_ie &&
		    wps_ielen > 0) {
			uint wps_offset, remainder_ielen;
			u8 *premainder_ie, *pframe_wscie;

			wps_offset = (uint)(wps_ie - cur_network->IEs);

			premainder_ie = wps_ie + wps_ielen;

			remainder_ielen = cur_network->IELength - wps_offset -
				wps_ielen;

			if (wdev_to_priv(padapter->rtw_wdev)->p2p_enabled) {
				if (pmlmepriv->wps_beacon_ie &&
				    pmlmepriv->wps_beacon_ie_len>0) {
					memcpy(pframe, cur_network->IEs,
					       wps_offset);
					pframe += wps_offset;
					pattrib->pktlen += wps_offset;

					memcpy(pframe, pmlmepriv->wps_beacon_ie,
					       pmlmepriv->wps_beacon_ie_len);
					pframe += pmlmepriv->wps_beacon_ie_len;
					pattrib->pktlen +=
						pmlmepriv->wps_beacon_ie_len;

					/* copy remainder_ie to pframe */
					memcpy(pframe, premainder_ie,
					       remainder_ielen);
					pframe += remainder_ielen;
					pattrib->pktlen += remainder_ielen;
				} else {
					memcpy(pframe, cur_network->IEs,
					       cur_network->IELength);
					pframe += cur_network->IELength;
					pattrib->pktlen +=
						cur_network->IELength;
				}
			} else {
				pframe_wscie = pframe + wps_offset;
				memcpy(pframe, cur_network->IEs,
				       wps_offset + wps_ielen);
				pframe += (wps_offset + wps_ielen);
				pattrib->pktlen += (wps_offset + wps_ielen);

				/* now pframe is end of wsc ie, insert Primary
				   Device Type & Device Name */
				/*	Primary Device Type */
				/*	Type: */
				*(u16*) (pframe + insert_len) =
					cpu_to_be16(WPS_ATTR_PRIMARY_DEV_TYPE);
				insert_len += 2;

				/*	Length: */
				*(u16*) (pframe + insert_len) =
					cpu_to_be16(0x0008);
				insert_len += 2;

				/*	Value: */
				/*	Category ID */
				*(u16*) (pframe + insert_len) =
					cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA);
				insert_len += 2;

				/*	OUI */
				*(u32*) (pframe + insert_len) =
					cpu_to_be32(WPSOUI);
				insert_len += 4;

				/*	Sub Category ID */
				*(u16*) (pframe + insert_len) =
					cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
				insert_len += 2;

				/*	Device Name */
				/*	Type: */
				*(u16*) (pframe + insert_len) =
					cpu_to_be16(WPS_ATTR_DEVICE_NAME);
				insert_len += 2;

				/*	Length: */
				*(u16*) (pframe + insert_len) =
					cpu_to_be16(pwdinfo->device_name_len);
				insert_len += 2;

				/*	Value: */
				memcpy(pframe + insert_len,
				       pwdinfo->device_name,
				       pwdinfo->device_name_len);
				insert_len += pwdinfo->device_name_len;

				/* update wsc ie length */
				*(pframe_wscie+1) = (wps_ielen -2) + insert_len;

				/* pframe move to end */
				pframe+= insert_len;
				pattrib->pktlen += insert_len;

				/* copy remainder_ie to pframe */
				memcpy(pframe, premainder_ie, remainder_ielen);
				pframe += remainder_ielen;
				pattrib->pktlen += remainder_ielen;
			}
		} else
#endif /* CONFIG_8723AU_P2P */
			memcpy(pframe, cur_network->IEs, cur_network->IELength);
		len_diff = update_hidden_ssid(pframe + _BEACON_IE_OFFSET_,
					      cur_network->IELength -
					      _BEACON_IE_OFFSET_,
					      pmlmeinfo->hidden_ssid_mode);
		pframe += (cur_network->IELength+len_diff);
		pattrib->pktlen += (cur_network->IELength+len_diff);

		wps_ie = rtw_get_wps_ie23a(pmgntframe->buf_addr + TXDESC_OFFSET+
					   sizeof (struct ieee80211_hdr_3addr) +
					   _BEACON_IE_OFFSET_, pattrib->pktlen -
					   sizeof (struct ieee80211_hdr_3addr) -
					   _BEACON_IE_OFFSET_, NULL,
					   &wps_ielen);
		if (wps_ie && wps_ielen > 0) {
			rtw_get_wps_attr_content23a(wps_ie, wps_ielen,
						    WPS_ATTR_SELECTED_REGISTRAR,
						    (u8*)&sr, NULL);
		}
		if (sr != 0)
			set_fwstate(pmlmepriv, WIFI_UNDER_WPS);
		else
			_clr_fwstate_(pmlmepriv, WIFI_UNDER_WPS);

#ifdef CONFIG_8723AU_P2P
		if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
			u32 len;
			if (wdev_to_priv(padapter->rtw_wdev)->p2p_enabled) {
				len = pmlmepriv->p2p_beacon_ie_len;
				if (pmlmepriv->p2p_beacon_ie && len > 0)
					memcpy(pframe,
					       pmlmepriv->p2p_beacon_ie, len);
			} else
				len = build_beacon_p2p_ie23a(pwdinfo, pframe);

			pframe += len;
			pattrib->pktlen += len;

			if (true == pwdinfo->wfd_info->wfd_enable) {
				len = build_beacon_wfd_ie(pwdinfo, pframe);
			} else {
				len = 0;
				if (pmlmepriv->wfd_beacon_ie &&
				    pmlmepriv->wfd_beacon_ie_len>0) {
					len = pmlmepriv->wfd_beacon_ie_len;
					memcpy(pframe,
					       pmlmepriv->wfd_beacon_ie, len);
				}
			}
			pframe += len;
			pattrib->pktlen += len;
		}
#endif /* CONFIG_8723AU_P2P */

		goto _issue_bcn;
	}

	/* below for ad-hoc mode */

	/* timestamp will be inserted by hardware */
	pframe += 8;
	pattrib->pktlen += 8;

	/*  beacon interval: 2 bytes */

	memcpy(pframe, (unsigned char *)
	       rtw_get_beacon_interval23a_from_ie(cur_network->IEs), 2);

	pframe += 2;
	pattrib->pktlen += 2;

	/*  capability info: 2 bytes */

	memcpy(pframe, (unsigned char *)
	       rtw_get_capability23a_from_ie(cur_network->IEs), 2);

	pframe += 2;
	pattrib->pktlen += 2;

	/*  SSID */
	pframe = rtw_set_ie23a(pframe, _SSID_IE_, cur_network->Ssid.ssid_len,
			       cur_network->Ssid.ssid, &pattrib->pktlen);

	/*  supported rates... */
	rate_len = rtw_get_rateset_len23a(cur_network->SupportedRates);
	pframe = rtw_set_ie23a(pframe, _SUPPORTEDRATES_IE_,
			       ((rate_len > 8)? 8: rate_len),
			       cur_network->SupportedRates, &pattrib->pktlen);

	/*  DS parameter set */
	pframe = rtw_set_ie23a(pframe, _DSSET_IE_, 1, (unsigned char *)
			       &cur_network->Configuration.DSConfig,
			       &pattrib->pktlen);

	/* if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) */
	{
		u8 erpinfo = 0;
		u32 ATIMWindow;
		/*  IBSS Parameter Set... */
		/* ATIMWindow = cur->Configuration.ATIMWindow; */
		ATIMWindow = 0;
		pframe = rtw_set_ie23a(pframe, _IBSS_PARA_IE_, 2,
				       (unsigned char *)&ATIMWindow,
				       &pattrib->pktlen);

		/* ERP IE */
		pframe = rtw_set_ie23a(pframe, _ERPINFO_IE_, 1,
				       &erpinfo, &pattrib->pktlen);
	}

	/*  EXTERNDED SUPPORTED RATE */
	if (rate_len > 8)
		pframe = rtw_set_ie23a(pframe, _EXT_SUPPORTEDRATES_IE_,
				       rate_len - 8,
				       cur_network->SupportedRates + 8,
				       &pattrib->pktlen);

	/* todo:HT for adhoc */

_issue_bcn:

#ifdef CONFIG_8723AU_AP_MODE
	pmlmepriv->update_bcn = false;

	spin_unlock_bh(&pmlmepriv->bcn_update_lock);
#endif

	if ((pattrib->pktlen + TXDESC_SIZE) > 512) {
		DBG_8723A("beacon frame too large\n");
		return;
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	/* DBG_8723A("issue bcn_sz =%d\n", pattrib->last_txcmdsz); */
	if (timeout_ms > 0)
		dump_mgntframe23a_and_wait(padapter, pmgntframe, timeout_ms);
	else
		dump_mgntframe23a(padapter, pmgntframe);
}

void issue_probersp23a(struct rtw_adapter *padapter, unsigned char *da,
		       u8 is_valid_p2p_probereq)
{
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	unsigned char *mac, *bssid;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
#ifdef CONFIG_8723AU_AP_MODE
	u8 *pwps_ie;
	uint wps_ielen;
	u8 *ssid_ie;
	int ssid_ielen;
	int ssid_ielen_diff;
	u8 buf[MAX_IE_SZ];
	u8 *ies;
#endif
#if defined(CONFIG_8723AU_AP_MODE) || defined(CONFIG_8723AU_P2P)
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
#endif
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *cur_network = &pmlmeinfo->network;
	unsigned int rate_len;
#ifdef CONFIG_8723AU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif /* CONFIG_8723AU_P2P */

	/* DBG_8723A("%s\n", __func__); */

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
	{
		DBG_8723A("%s, alloc mgnt frame fail\n", __func__);
		return;
	}

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	mac = myid(&padapter->eeprompriv);
	bssid = cur_network->MacAddress;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;
	ether_addr_copy(pwlanhdr->addr1, da);
	ether_addr_copy(pwlanhdr->addr2, mac);
	ether_addr_copy(pwlanhdr->addr3, bssid);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(fctrl, WIFI_PROBERSP);

	pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = pattrib->hdrlen;
	pframe += pattrib->hdrlen;

	if (cur_network->IELength > MAX_IE_SZ)
		return;

#ifdef CONFIG_8723AU_AP_MODE
	if ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) {
		pwps_ie = rtw_get_wps_ie23a(cur_network->IEs +
					    _FIXED_IE_LENGTH_,
					    cur_network->IELength -
					    _FIXED_IE_LENGTH_, NULL,
					    &wps_ielen);

		/* inerset & update wps_probe_resp_ie */
		if ((pmlmepriv->wps_probe_resp_ie != NULL) && pwps_ie &&
		    (wps_ielen > 0)) {
			uint wps_offset, remainder_ielen;
			u8 *premainder_ie;

			wps_offset = (uint)(pwps_ie - cur_network->IEs);

			premainder_ie = pwps_ie + wps_ielen;

			remainder_ielen = cur_network->IELength - wps_offset -
				wps_ielen;

			memcpy(pframe, cur_network->IEs, wps_offset);
			pframe += wps_offset;
			pattrib->pktlen += wps_offset;

			/* to get ie data len */
			wps_ielen = (uint)pmlmepriv->wps_probe_resp_ie[1];
			if ((wps_offset+wps_ielen+2)<= MAX_IE_SZ) {
				memcpy(pframe, pmlmepriv->wps_probe_resp_ie,
				       wps_ielen+2);
				pframe += wps_ielen+2;
				pattrib->pktlen += wps_ielen+2;
			}

			if ((wps_offset+wps_ielen+2+remainder_ielen) <=
			    MAX_IE_SZ) {
				memcpy(pframe, premainder_ie, remainder_ielen);
				pframe += remainder_ielen;
				pattrib->pktlen += remainder_ielen;
			}
		} else {
			memcpy(pframe, cur_network->IEs, cur_network->IELength);
			pframe += cur_network->IELength;
			pattrib->pktlen += cur_network->IELength;
		}

		/* retrieve SSID IE from cur_network->Ssid */
		ies = pmgntframe->buf_addr + TXDESC_OFFSET +
			sizeof(struct ieee80211_hdr_3addr);

		ssid_ie = rtw_get_ie23a(ies+_FIXED_IE_LENGTH_, _SSID_IE_,
					&ssid_ielen,
					(pframe-ies)-_FIXED_IE_LENGTH_);

		ssid_ielen_diff = cur_network->Ssid.ssid_len - ssid_ielen;

		if (ssid_ie && cur_network->Ssid.ssid_len) {
			uint remainder_ielen;
			u8 *remainder_ie;
			remainder_ie = ssid_ie + 2;
			remainder_ielen = (pframe-remainder_ie);

			DBG_8723A_LEVEL(_drv_warning_, FUNC_ADPT_FMT
					" remainder_ielen > MAX_IE_SZ\n",
					FUNC_ADPT_ARG(padapter));
			if (remainder_ielen > MAX_IE_SZ) {
				remainder_ielen = MAX_IE_SZ;
			}

			memcpy(buf, remainder_ie, remainder_ielen);
			memcpy(remainder_ie+ssid_ielen_diff, buf,
			       remainder_ielen);
			*(ssid_ie+1) = cur_network->Ssid.ssid_len;
			memcpy(ssid_ie+2, cur_network->Ssid.ssid,
			       cur_network->Ssid.ssid_len);

			pframe += ssid_ielen_diff;
			pattrib->pktlen += ssid_ielen_diff;
		}
	} else
#endif
	{

		/* timestamp will be inserted by hardware */
		pframe += 8;
		pattrib->pktlen += 8;

		/*  beacon interval: 2 bytes */

		memcpy(pframe, (unsigned char *)
		       rtw_get_beacon_interval23a_from_ie(cur_network->IEs), 2);

		pframe += 2;
		pattrib->pktlen += 2;

		/*  capability info: 2 bytes */

		memcpy(pframe, (unsigned char *)
		       rtw_get_capability23a_from_ie(cur_network->IEs), 2);

		pframe += 2;
		pattrib->pktlen += 2;

		/* below for ad-hoc mode */

		/*  SSID */
		pframe = rtw_set_ie23a(pframe, _SSID_IE_,
				    cur_network->Ssid.ssid_len,
				    cur_network->Ssid.ssid, &pattrib->pktlen);

		/*  supported rates... */
		rate_len = rtw_get_rateset_len23a(cur_network->SupportedRates);
		pframe = rtw_set_ie23a(pframe, _SUPPORTEDRATES_IE_,
				       ((rate_len > 8)? 8: rate_len),
				       cur_network->SupportedRates,
				       &pattrib->pktlen);

		/*  DS parameter set */
		pframe = rtw_set_ie23a(pframe, _DSSET_IE_, 1, (unsigned char *)
				       &cur_network->Configuration.DSConfig,
				       &pattrib->pktlen);

		if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) {
			u8 erpinfo = 0;
			u32 ATIMWindow;
			/*  IBSS Parameter Set... */
			/* ATIMWindow = cur->Configuration.ATIMWindow; */
			ATIMWindow = 0;
			pframe = rtw_set_ie23a(pframe, _IBSS_PARA_IE_, 2,
					       (unsigned char *)&ATIMWindow,
					       &pattrib->pktlen);

			/* ERP IE */
			pframe = rtw_set_ie23a(pframe, _ERPINFO_IE_, 1,
					       &erpinfo, &pattrib->pktlen);
		}

		/*  EXTERNDED SUPPORTED RATE */
		if (rate_len > 8)
			pframe = rtw_set_ie23a(pframe, _EXT_SUPPORTEDRATES_IE_,
					       rate_len - 8,
					       cur_network->SupportedRates + 8,
					       &pattrib->pktlen);

		/* todo:HT for adhoc */
	}

#ifdef CONFIG_8723AU_P2P
	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO) && is_valid_p2p_probereq) {
		u32 len;
		if (wdev_to_priv(padapter->rtw_wdev)->p2p_enabled) {
			/* if pwdinfo->role == P2P_ROLE_DEVICE will call
			   issue_probersp23a_p2p23a() */
			len = pmlmepriv->p2p_go_probe_resp_ie_len;
			if (pmlmepriv->p2p_go_probe_resp_ie && len>0)
				memcpy(pframe, pmlmepriv->p2p_go_probe_resp_ie,
				       len);
		} else
			len = build_probe_resp_p2p_ie23a(pwdinfo, pframe);

		pframe += len;
		pattrib->pktlen += len;

		if (true == pwdinfo->wfd_info->wfd_enable) {
			len = build_probe_resp_wfd_ie(pwdinfo, pframe, 0);
		} else {
			len = 0;
			if (pmlmepriv->wfd_probe_resp_ie &&
			    pmlmepriv->wfd_probe_resp_ie_len > 0) {
				len = pmlmepriv->wfd_probe_resp_ie_len;
				memcpy(pframe, pmlmepriv->wfd_probe_resp_ie,
				       len);
			}
		}
		pframe += len;
		pattrib->pktlen += len;
	}
#endif /* CONFIG_8723AU_P2P */

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe23a(padapter, pmgntframe);

	return;
}

static int _issue_probereq23a(struct rtw_adapter *padapter,
			      struct cfg80211_ssid *pssid, u8 *da, int wait_ack)
{
	int ret = _FAIL;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	unsigned char			*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short		*fctrl;
	unsigned char			*mac;
	unsigned char			bssrate[NumRates];
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	int	bssrate_len = 0;
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
		 ("+issue_probereq23a\n"));

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	mac = myid(&padapter->eeprompriv);

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

	if (da) {
		/*	unicast probe request frame */
		ether_addr_copy(pwlanhdr->addr1, da);
		ether_addr_copy(pwlanhdr->addr3, da);
	} else {
		/*	broadcast probe request frame */
		ether_addr_copy(pwlanhdr->addr1, bc_addr);
		ether_addr_copy(pwlanhdr->addr3, bc_addr);
	}

	ether_addr_copy(pwlanhdr->addr2, mac);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_PROBEREQ);

	pframe += sizeof (struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof (struct ieee80211_hdr_3addr);

	if (pssid)
		pframe = rtw_set_ie23a(pframe, _SSID_IE_, pssid->ssid_len,
				       pssid->ssid, &pattrib->pktlen);
	else
		pframe = rtw_set_ie23a(pframe, _SSID_IE_, 0, NULL,
				       &pattrib->pktlen);

	get_rate_set23a(padapter, bssrate, &bssrate_len);

	if (bssrate_len > 8) {
		pframe = rtw_set_ie23a(pframe, _SUPPORTEDRATES_IE_, 8,
				       bssrate, &pattrib->pktlen);
		pframe = rtw_set_ie23a(pframe, _EXT_SUPPORTEDRATES_IE_,
				       (bssrate_len - 8), (bssrate + 8),
				       &pattrib->pktlen);
	} else {
		pframe = rtw_set_ie23a(pframe, _SUPPORTEDRATES_IE_,
				       bssrate_len, bssrate, &pattrib->pktlen);
	}

	/* add wps_ie for wps2.0 */
	if (pmlmepriv->wps_probe_req_ie_len>0 && pmlmepriv->wps_probe_req_ie) {
		memcpy(pframe, pmlmepriv->wps_probe_req_ie,
		       pmlmepriv->wps_probe_req_ie_len);
		pframe += pmlmepriv->wps_probe_req_ie_len;
		pattrib->pktlen += pmlmepriv->wps_probe_req_ie_len;
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
		 ("issuing probe_req, tx_len =%d\n", pattrib->last_txcmdsz));

	if (wait_ack) {
		ret = dump_mgntframe23a_and_wait_ack23a(padapter, pmgntframe);
	} else {
		dump_mgntframe23a(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

inline void issue_probereq23a(struct rtw_adapter *padapter,
			      struct cfg80211_ssid *pssid, u8 *da)
{
	_issue_probereq23a(padapter, pssid, da, false);
}

int issue_probereq23a_ex23a(struct rtw_adapter *padapter,
		      struct cfg80211_ssid *pssid, u8 *da,
		      int try_cnt, int wait_ms)
{
	int ret;
	int i = 0;
	unsigned long start = jiffies;

	do {
		ret = _issue_probereq23a(padapter, pssid, da,
					 wait_ms > 0 ? true : false);

		i++;

		if (padapter->bDriverStopped || padapter->bSurpriseRemoved)
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			msleep(wait_ms);

	} while((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		goto exit;
	}

	if (try_cnt && wait_ms) {
		if (da)
			DBG_8723A(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d "
				  "in %u ms\n",	FUNC_ADPT_ARG(padapter),
				  MAC_ARG(da), rtw_get_oper_ch23a(padapter),
				  ret == _SUCCESS?", acked":"", i, try_cnt,
				  jiffies_to_msecs(jiffies - start));
		else
			DBG_8723A(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				  FUNC_ADPT_ARG(padapter),
				  rtw_get_oper_ch23a(padapter),
				  ret == _SUCCESS?", acked":"", i, try_cnt,
				  jiffies_to_msecs(jiffies - start));
	}
exit:
	return ret;
}

/*  if psta == NULL, indiate we are station(client) now... */
void issue_auth23a(struct rtw_adapter *padapter, struct sta_info *psta,
		   unsigned short status)
{
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	unsigned int val32;
	unsigned short val16;
	int use_shared_key = 0;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_AUTH);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	if (psta) { /*  for AP mode */
#ifdef CONFIG_8723AU_AP_MODE

		ether_addr_copy(pwlanhdr->addr1, psta->hwaddr);
		ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
		ether_addr_copy(pwlanhdr->addr3, myid(&padapter->eeprompriv));

		/*  setting auth algo number */
		val16 = (u16)psta->authalg;

		if (status != WLAN_STATUS_SUCCESS)
			val16 = 0;

		if (val16) {
			val16 = cpu_to_le16(val16);
			use_shared_key = 1;
		}

		pframe = rtw_set_fixed_ie23a(pframe, _AUTH_ALGM_NUM_,
					     (unsigned char *)&val16,
					     &pattrib->pktlen);

		/*  setting auth seq number */
		val16 = (u16)psta->auth_seq;
		val16 = cpu_to_le16(val16);
		pframe = rtw_set_fixed_ie23a(pframe, _AUTH_SEQ_NUM_,
					     (unsigned char *)&val16,
					     &pattrib->pktlen);

		/*  setting status code... */
		val16 = status;
		val16 = cpu_to_le16(val16);
		pframe = rtw_set_fixed_ie23a(pframe, _STATUS_CODE_,
					     (unsigned char *)&val16,
					     &pattrib->pktlen);

		/*  added challenging text... */
		if ((psta->auth_seq == 2) &&
		    (psta->state & WIFI_FW_AUTH_STATE) && (use_shared_key == 1))
			pframe = rtw_set_ie23a(pframe, _CHLGETXT_IE_, 128,
					       psta->chg_txt, &pattrib->pktlen);
#endif
	} else {
		ether_addr_copy(pwlanhdr->addr1,
				get_my_bssid23a(&pmlmeinfo->network));
		ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
		ether_addr_copy(pwlanhdr->addr3,
				get_my_bssid23a(&pmlmeinfo->network));

		/*  setting auth algo number */
		/*  0:OPEN System, 1:Shared key */
		val16 = (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared)? 1: 0;
		if (val16) {
			val16 = cpu_to_le16(val16);
			use_shared_key = 1;
		}
		/* DBG_8723A("%s auth_algo = %s auth_seq =%d\n", __func__,
		   (pmlmeinfo->auth_algo == 0)?"OPEN":"SHARED",
		   pmlmeinfo->auth_seq); */

		/* setting IV for auth seq #3 */
		if ((pmlmeinfo->auth_seq == 3) &&
		    (pmlmeinfo->state & WIFI_FW_AUTH_STATE) &&
		    (use_shared_key == 1)) {
			/* DBG_8723A("==> iv(%d), key_index(%d)\n",
			   pmlmeinfo->iv, pmlmeinfo->key_index); */
			val32 = ((pmlmeinfo->iv++) |
				 (pmlmeinfo->key_index << 30));
			val32 = cpu_to_le32(val32);
			pframe = rtw_set_fixed_ie23a(pframe, 4,
						     (unsigned char *)&val32,
						     &pattrib->pktlen);

			pattrib->iv_len = 4;
		}

		pframe = rtw_set_fixed_ie23a(pframe, _AUTH_ALGM_NUM_,
					     (unsigned char *)&val16,
					     &pattrib->pktlen);

		/*  setting auth seq number */
		val16 = pmlmeinfo->auth_seq;
		val16 = cpu_to_le16(val16);
		pframe = rtw_set_fixed_ie23a(pframe, _AUTH_SEQ_NUM_,
					     (unsigned char *)&val16,
					     &pattrib->pktlen);

		/*  setting status code... */
		val16 = status;
		val16 = cpu_to_le16(val16);
		pframe = rtw_set_fixed_ie23a(pframe, _STATUS_CODE_,
					     (unsigned char *)&val16,
					     &pattrib->pktlen);

		/*  then checking to see if sending challenging text... */
		if ((pmlmeinfo->auth_seq == 3) &&
		    (pmlmeinfo->state & WIFI_FW_AUTH_STATE) &&
		    (use_shared_key == 1)) {
			pframe = rtw_set_ie23a(pframe, _CHLGETXT_IE_, 128,
					       pmlmeinfo->chg_txt,
					       &pattrib->pktlen);

			SetPrivacy(fctrl);

			pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);

			pattrib->encrypt = _WEP40_;

			pattrib->icv_len = 4;

			pattrib->pktlen += pattrib->icv_len;
		}
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	rtw_wep_encrypt23a(padapter, pmgntframe);
	DBG_8723A("%s\n", __func__);
	dump_mgntframe23a(padapter, pmgntframe);

	return;
}

void issue_asocrsp23a(struct rtw_adapter *padapter, unsigned short status,
		      struct sta_info *pstat, int pkt_type)
{
#ifdef CONFIG_8723AU_AP_MODE
	struct xmit_frame *pmgntframe;
	struct ieee80211_hdr *pwlanhdr;
	struct pkt_attrib *pattrib;
	unsigned char *pbuf, *pframe;
	unsigned short val;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *pnetwork = &pmlmeinfo->network;
	u8 *ie = pnetwork->IEs;
#ifdef CONFIG_8723AU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	u32 wfdielen = 0;
#endif /* CONFIG_8723AU_P2P */

	DBG_8723A("%s\n", __func__);

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	pwlanhdr->frame_control = 0;

	ether_addr_copy(pwlanhdr->addr1, pstat->hwaddr);
	ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
	ether_addr_copy(pwlanhdr->addr3, get_my_bssid23a(&pmlmeinfo->network));

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
	val = *(unsigned short *)rtw_get_capability23a_from_ie(ie);

	pframe = rtw_set_fixed_ie23a(pframe, _CAPABILITY_,
				     (unsigned char *)&val, &pattrib->pktlen);

	status = cpu_to_le16(status);
	pframe = rtw_set_fixed_ie23a(pframe, _STATUS_CODE_,
				     (unsigned char *)&status,
				     &pattrib->pktlen);

	val = cpu_to_le16(pstat->aid | BIT(14) | BIT(15));
	pframe = rtw_set_fixed_ie23a(pframe, _ASOC_ID_, (unsigned char *)&val,
				     &pattrib->pktlen);

	if (pstat->bssratelen <= 8) {
		pframe = rtw_set_ie23a(pframe, _SUPPORTEDRATES_IE_,
				       pstat->bssratelen, pstat->bssrateset,
				       &pattrib->pktlen);
	} else {
		pframe = rtw_set_ie23a(pframe, _SUPPORTEDRATES_IE_, 8,
				       pstat->bssrateset, &pattrib->pktlen);
		pframe = rtw_set_ie23a(pframe, _EXT_SUPPORTEDRATES_IE_,
				       pstat->bssratelen - 8,
				       pstat->bssrateset + 8, &pattrib->pktlen);
	}

	if ((pstat->flags & WLAN_STA_HT) && (pmlmepriv->htpriv.ht_option)) {
		uint ie_len = 0;

		/* FILL HT CAP INFO IE */
		/* p = hostapd_eid_ht_capabilities_info(hapd, p); */
		pbuf = rtw_get_ie23a(ie + _BEACON_IE_OFFSET_,
				     _HT_CAPABILITY_IE_, &ie_len,
				     pnetwork->IELength - _BEACON_IE_OFFSET_);
		if (pbuf && ie_len>0) {
			memcpy(pframe, pbuf, ie_len + 2);
			pframe += (ie_len + 2);
			pattrib->pktlen += (ie_len + 2);
		}

		/* FILL HT ADD INFO IE */
		/* p = hostapd_eid_ht_operation(hapd, p); */
		pbuf = rtw_get_ie23a(ie + _BEACON_IE_OFFSET_, _HT_ADD_INFO_IE_,
				     &ie_len,
				     pnetwork->IELength - _BEACON_IE_OFFSET_);
		if (pbuf && ie_len > 0) {
			memcpy(pframe, pbuf, ie_len + 2);
			pframe += (ie_len + 2);
			pattrib->pktlen += (ie_len + 2);
		}
	}

	/* FILL WMM IE */
	if ((pstat->flags & WLAN_STA_WME) && pmlmepriv->qospriv.qos_option) {
		uint ie_len = 0;
		unsigned char WMM_PARA_IE[] = {0x00, 0x50, 0xf2, 0x02,
					       0x01, 0x01};

		for (pbuf = ie + _BEACON_IE_OFFSET_; ; pbuf += (ie_len + 2)) {
			pbuf = rtw_get_ie23a(pbuf, _VENDOR_SPECIFIC_IE_,
					     &ie_len, (pnetwork->IELength -
						       _BEACON_IE_OFFSET_ -
						       (ie_len + 2)));
			if (pbuf && !memcmp(pbuf + 2, WMM_PARA_IE, 6)) {
				memcpy(pframe, pbuf, ie_len + 2);
				pframe += (ie_len + 2);
				pattrib->pktlen += (ie_len + 2);

				break;
			}

			if ((!pbuf) || (ie_len == 0))
				break;
		}
	}

	if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK) {
		pframe = rtw_set_ie23a(pframe, _VENDOR_SPECIFIC_IE_, 6,
				       REALTEK_96B_IE23A, &pattrib->pktlen);
	}

	/* add WPS IE ie for wps 2.0 */
	if (pmlmepriv->wps_assoc_resp_ie &&
	    pmlmepriv->wps_assoc_resp_ie_len > 0) {
		memcpy(pframe, pmlmepriv->wps_assoc_resp_ie,
		       pmlmepriv->wps_assoc_resp_ie_len);

		pframe += pmlmepriv->wps_assoc_resp_ie_len;
		pattrib->pktlen += pmlmepriv->wps_assoc_resp_ie_len;
	}

#ifdef CONFIG_8723AU_P2P
	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO) &&
	    pwdinfo->wfd_info->wfd_enable) {
		wfdielen = build_assoc_resp_wfd_ie(pwdinfo, pframe);
		pframe += wfdielen;
		pattrib->pktlen += wfdielen;
	}
#endif /* CONFIG_8723AU_P2P */

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe23a(padapter, pmgntframe);
#endif
}

void issue_assocreq23a(struct rtw_adapter *padapter)
{
	int ret = _FAIL;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe, *p;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	unsigned short val16;
	unsigned int i, j, ie_len, index = 0;
	unsigned char rf_type, bssrate[NumRates], sta_bssrate[NumRates];
	struct ndis_802_11_var_ies *pIE;
	struct registry_priv *pregpriv = &padapter->registrypriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	int bssrate_len = 0, sta_bssrate_len = 0;
#ifdef CONFIG_8723AU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	u8 p2pie[255] = { 0x00 };
	u16 p2pielen = 0;
	u32 wfdielen = 0;
#endif /* CONFIG_8723AU_P2P */

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;
	ether_addr_copy(pwlanhdr->addr1, get_my_bssid23a(&pmlmeinfo->network));
	ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
	ether_addr_copy(pwlanhdr->addr3, get_my_bssid23a(&pmlmeinfo->network));

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ASSOCREQ);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	/* caps */
	memcpy(pframe, rtw_get_capability23a_from_ie(pmlmeinfo->network.IEs),
	       2);

	pframe += 2;
	pattrib->pktlen += 2;

	/* listen interval */
	/* todo: listen interval for power saving */
	val16 = cpu_to_le16(3);
	memcpy(pframe, (unsigned char *)&val16, 2);
	pframe += 2;
	pattrib->pktlen += 2;

	/* SSID */
	pframe = rtw_set_ie23a(pframe, _SSID_IE_,
			       pmlmeinfo->network.Ssid.ssid_len,
			       pmlmeinfo->network.Ssid.ssid, &pattrib->pktlen);

	/* supported rate & extended supported rate */

	get_rate_set23a(padapter, sta_bssrate, &sta_bssrate_len);
	/* DBG_8723A("sta_bssrate_len =%d\n", sta_bssrate_len); */

	/*  for JAPAN, channel 14 can only uses B Mode(CCK) */
	if (pmlmeext->cur_channel == 14)
		sta_bssrate_len = 4;

	/* for (i = 0; i < sta_bssrate_len; i++) { */
	/*	DBG_8723A("sta_bssrate[%d]=%02X\n", i, sta_bssrate[i]); */
	/*  */

	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		if (pmlmeinfo->network.SupportedRates[i] == 0)
			break;
		DBG_8723A("network.SupportedRates[%d]=%02X\n", i,
			  pmlmeinfo->network.SupportedRates[i]);
	}

	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		if (pmlmeinfo->network.SupportedRates[i] == 0)
			break;

		/*  Check if the AP's supported rates are also
		    supported by STA. */
		for (j = 0; j < sta_bssrate_len; j++) {
			 /*  Avoid the proprietary data rate (22Mbps) of
			     Handlink WSG-4000 AP */
			if ((pmlmeinfo->network.SupportedRates[i] |
			     IEEE80211_BASIC_RATE_MASK) ==
			    (sta_bssrate[j]|IEEE80211_BASIC_RATE_MASK)) {
				/* DBG_8723A("match i = %d, j =%d\n", i, j); */
				break;
			}
		}

		if (j == sta_bssrate_len) {
			/*  the rate is not supported by STA */
			DBG_8723A("%s(): the rate[%d]=%02X is not supported by "
				  "STA!\n", __func__, i,
				  pmlmeinfo->network.SupportedRates[i]);
		} else {
			/*  the rate is supported by STA */
			bssrate[index++] = pmlmeinfo->network.SupportedRates[i];
		}
	}

	bssrate_len = index;
	DBG_8723A("bssrate_len = %d\n", bssrate_len);

	if (bssrate_len == 0) {
		rtw_free_xmitbuf23a(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe23a(pxmitpriv, pmgntframe);
		goto exit; /* don't connect to AP if no joint supported rate */
	}

	if (bssrate_len > 8) {
		pframe = rtw_set_ie23a(pframe, _SUPPORTEDRATES_IE_, 8,
				       bssrate, &pattrib->pktlen);
		pframe = rtw_set_ie23a(pframe, _EXT_SUPPORTEDRATES_IE_,
				       (bssrate_len - 8), (bssrate + 8),
				       &pattrib->pktlen);
	} else
		pframe = rtw_set_ie23a(pframe, _SUPPORTEDRATES_IE_,
				       bssrate_len, bssrate, &pattrib->pktlen);

	/* RSN */
	p = rtw_get_ie23a((pmlmeinfo->network.IEs +
			   sizeof(struct ndis_802_11_fixed_ies)), _RSN_IE_2_,
			  &ie_len, (pmlmeinfo->network.IELength -
				    sizeof(struct ndis_802_11_fixed_ies)));
	if (p)
		pframe = rtw_set_ie23a(pframe, _RSN_IE_2_, ie_len, (p + 2),
				       &pattrib->pktlen);

	/* HT caps */
	if (padapter->mlmepriv.htpriv.ht_option == true) {
		p = rtw_get_ie23a((pmlmeinfo->network.IEs +
				   sizeof(struct ndis_802_11_fixed_ies)),
				  _HT_CAPABILITY_IE_, &ie_len,
				  (pmlmeinfo->network.IELength -
				   sizeof(struct ndis_802_11_fixed_ies)));
		if ((p != NULL) && (!(is_ap_in_tkip23a(padapter)))) {
			memcpy(&pmlmeinfo->HT_caps, (p + 2),
			       sizeof(struct HT_caps_element));

			/* to disable 40M Hz support while gd_bw_40MHz_en = 0 */
			if (pregpriv->cbw40_enable == 0) {
				pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info &= (~(BIT(6) | BIT(1)));
			} else {
				pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info |= BIT(1);
			}

			/* todo: disable SM power save mode */
			pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info |=
				0x000c;

			rtw23a_hal_get_hwreg(padapter, HW_VAR_RF_TYPE,
					     (u8 *)(&rf_type));
			/* switch (pregpriv->rf_config) */
			switch (rf_type)
			{
			case RF_1T1R:

				if (pregpriv->rx_stbc)
					pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info |= cpu_to_le16(0x0100);/* RX STBC One spatial stream */

				memcpy(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_rate_1R23A, 16);
				break;

			case RF_2T2R:
			case RF_1T2R:
			default:

				/* enable for 2.4/5 GHz */
				if ((pregpriv->rx_stbc == 0x3) ||
				    ((pmlmeext->cur_wireless_mode &
				      WIRELESS_11_24N) &&
				     /* enable for 2.4GHz */
				     (pregpriv->rx_stbc == 0x1)) ||
				    ((pmlmeext->cur_wireless_mode &
				      WIRELESS_11_5N) &&
				     (pregpriv->rx_stbc == 0x2)) ||
				    /* enable for 5GHz */
				    (pregpriv->wifi_spec == 1)) {
					DBG_8723A("declare supporting RX "
						  "STBC\n");
					pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info |= cpu_to_le16(0x0200);/* RX STBC two spatial stream */
				}
				memcpy(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_rate_2R23A, 16);
				break;
			}
			pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info =
				cpu_to_le16(pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info);

#ifdef CONFIG_8723AU_BT_COEXIST
			if (BT_1Ant(padapter) == true) {
				/*  set to 8K */
				pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para &= (u8)~IEEE80211_HT_AMPDU_PARM_FACTOR;
/*				pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para |= MAX_AMPDU_FACTOR_8K */
			}
#endif

			pframe = rtw_set_ie23a(pframe, _HT_CAPABILITY_IE_,
					       ie_len,
					       (u8 *)&pmlmeinfo->HT_caps,
					       &pattrib->pktlen);
		}
	}

	/* vendor specific IE, such as WPA, WMM, WPS */
	for (i = sizeof(struct ndis_802_11_fixed_ies);
	     i < pmlmeinfo->network.IELength;) {
		pIE = (struct ndis_802_11_var_ies *)
			(pmlmeinfo->network.IEs + i);

		switch (pIE->ElementID)
		{
		case _VENDOR_SPECIFIC_IE_:
			if (!memcmp(pIE->data, RTW_WPA_OUI23A, 4) ||
			    !memcmp(pIE->data, WMM_OUI23A, 4) ||
			    !memcmp(pIE->data, WPS_OUI23A, 4)) {
				if (!padapter->registrypriv.wifi_spec) {
					/* Commented by Kurt 20110629 */
					/* In some older APs, WPS handshake */
					/* would be fail if we append vender
					   extensions informations to AP */
					if (!memcmp(pIE->data, WPS_OUI23A, 4))
						pIE->Length = 14;
				}
				pframe = rtw_set_ie23a(pframe,
						       _VENDOR_SPECIFIC_IE_,
						       pIE->Length, pIE->data,
						       &pattrib->pktlen);
			}
			break;

		default:
			break;
		}

		i += (pIE->Length + 2);
	}

	if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK)
		pframe = rtw_set_ie23a(pframe, _VENDOR_SPECIFIC_IE_, 6,
				       REALTEK_96B_IE23A, &pattrib->pktlen);

#ifdef CONFIG_8723AU_P2P

	if (wdev_to_priv(padapter->rtw_wdev)->p2p_enabled) {
		if (pmlmepriv->p2p_assoc_req_ie &&
		    pmlmepriv->p2p_assoc_req_ie_len>0) {
			memcpy(pframe, pmlmepriv->p2p_assoc_req_ie,
			       pmlmepriv->p2p_assoc_req_ie_len);
			pframe += pmlmepriv->p2p_assoc_req_ie_len;
			pattrib->pktlen += pmlmepriv->p2p_assoc_req_ie_len;
		}
	} else {
		if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) &&
		    !rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE)) {
			/*	Should add the P2P IE in the association
				request frame. */
			/*	P2P OUI */

			p2pielen = 0;
			p2pie[p2pielen++] = 0x50;
			p2pie[p2pielen++] = 0x6F;
			p2pie[p2pielen++] = 0x9A;
			p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

			/*	Commented by Albert 20101109 */
			/*	According to the P2P Specification, the
				association request frame should contain
				3 P2P attributes */
			/*	1. P2P Capability */
			/*	2. Extended Listen Timing */
			/*	3. Device Info */
			/*	Commented by Albert 20110516 */
			/*	4. P2P Interface */

			/*	P2P Capability */
			/*	Type: */
			p2pie[p2pielen++] = P2P_ATTR_CAPABILITY;

			/*	Length: */
			*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0002);
			p2pielen += 2;

			/*	Value: */
			/*	Device Capability Bitmap, 1 byte */
			p2pie[p2pielen++] = DMP_P2P_DEVCAP_SUPPORT;

			/*	Group Capability Bitmap, 1 byte */
			if (pwdinfo->persistent_supported)
				p2pie[p2pielen++] =
					P2P_GRPCAP_PERSISTENT_GROUP |
					DMP_P2P_GRPCAP_SUPPORT;
			else
				p2pie[p2pielen++] = DMP_P2P_GRPCAP_SUPPORT;

			/*	Extended Listen Timing */
			/*	Type: */
			p2pie[p2pielen++] = P2P_ATTR_EX_LISTEN_TIMING;

			/*	Length: */
			*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x0004);
			p2pielen += 2;

			/*	Value: */
			/*	Availability Period */
			*(u16*) (p2pie + p2pielen) = cpu_to_le16(0xFFFF);
			p2pielen += 2;

			/*	Availability Interval */
			*(u16*) (p2pie + p2pielen) = cpu_to_le16(0xFFFF);
			p2pielen += 2;

			/*	Device Info */
			/*	Type: */
			p2pie[p2pielen++] = P2P_ATTR_DEVICE_INFO;

			/*	Length: */
			/*	21 -> P2P Device Address (6bytes) + Config
				Methods (2bytes) + Primary Device
				Type (8bytes) */
			/*	+ NumofSecondDevType (1byte) + WPS Device
				Name ID field (2bytes) + WPS Device Name
				Len field (2bytes) */
			*(u16*) (p2pie + p2pielen) =
				cpu_to_le16(21 + pwdinfo->device_name_len);
			p2pielen += 2;

			/*	Value: */
			/*	P2P Device Address */
			memcpy(p2pie + p2pielen,
			       myid(&padapter->eeprompriv), ETH_ALEN);
			p2pielen += ETH_ALEN;

			/*	Config Method */
			/*	This field should be big endian.
				Noted by P2P specification. */
			if ((pwdinfo->ui_got_wps_info ==
			     P2P_GOT_WPSINFO_PEER_DISPLAY_PIN) ||
			    (pwdinfo->ui_got_wps_info ==
			     P2P_GOT_WPSINFO_SELF_DISPLAY_PIN))
				*(u16*) (p2pie + p2pielen) =
					cpu_to_be16(WPS_CONFIG_METHOD_DISPLAY);
			else
				*(u16*) (p2pie + p2pielen) =
					cpu_to_be16(WPS_CONFIG_METHOD_PBC);

			p2pielen += 2;

			/*	Primary Device Type */
			/*	Category ID */
			*(u16*) (p2pie + p2pielen) =
				cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA);
			p2pielen += 2;

			/*	OUI */
			*(u32*) (p2pie + p2pielen) = cpu_to_be32(WPSOUI);
			p2pielen += 4;

			/*	Sub Category ID */
			*(u16*) (p2pie + p2pielen) =
				cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
			p2pielen += 2;

			/*	Number of Secondary Device Types */
			/*	No Secondary Device Type List */
			p2pie[p2pielen++] = 0x00;

			/*	Device Name */
			/*	Type: */
			*(u16*) (p2pie + p2pielen) =
				cpu_to_be16(WPS_ATTR_DEVICE_NAME);
			p2pielen += 2;

			/*	Length: */
			*(u16*) (p2pie + p2pielen) =
				cpu_to_be16(pwdinfo->device_name_len);
			p2pielen += 2;

			/*	Value: */
			memcpy(p2pie + p2pielen, pwdinfo->device_name,
			       pwdinfo->device_name_len);
			p2pielen += pwdinfo->device_name_len;

			/*	P2P Interface */
			/*	Type: */
			p2pie[p2pielen++] = P2P_ATTR_INTERFACE;

			/*	Length: */
			*(u16*) (p2pie + p2pielen) = cpu_to_le16(0x000D);
			p2pielen += 2;

			/*	Value: */
			memcpy(p2pie + p2pielen, pwdinfo->device_addr,
			       ETH_ALEN);	/* P2P Device Address */
			p2pielen += ETH_ALEN;

			/* P2P Interface Address Count */
			p2pie[p2pielen++] = 1;

			memcpy(p2pie + p2pielen, pwdinfo->device_addr,
			       ETH_ALEN);	/* P2P Interface Address List */
			p2pielen += ETH_ALEN;

			pframe = rtw_set_ie23a(pframe, _VENDOR_SPECIFIC_IE_,
					       p2pielen, (unsigned char *)p2pie,
					       &pattrib->pktlen);

			/* wfdielen = build_assoc_req_wfd_ie(pwdinfo, pframe);*/
			/* pframe += wfdielen; */
			/* pattrib->pktlen += wfdielen; */
		}
	}

	if (true == pwdinfo->wfd_info->wfd_enable) {
		wfdielen = build_assoc_req_wfd_ie(pwdinfo, pframe);
		pframe += wfdielen;
		pattrib->pktlen += wfdielen;
	} else if (pmlmepriv->wfd_assoc_req_ie != NULL &&
		   pmlmepriv->wfd_assoc_req_ie_len > 0) {
		/* WFD IE */
		memcpy(pframe, pmlmepriv->wfd_assoc_req_ie,
		       pmlmepriv->wfd_assoc_req_ie_len);
		pattrib->pktlen += pmlmepriv->wfd_assoc_req_ie_len;
		pframe += pmlmepriv->wfd_assoc_req_ie_len;
	}
#endif /* CONFIG_8723AU_P2P */

	pattrib->last_txcmdsz = pattrib->pktlen;
	dump_mgntframe23a(padapter, pmgntframe);

	ret = _SUCCESS;

exit:
	pmlmepriv->assoc_req_len = 0;
	if (ret == _SUCCESS) {
		kfree(pmlmepriv->assoc_req);
		pmlmepriv->assoc_req = kmalloc(pattrib->pktlen, GFP_ATOMIC);
		if (pmlmepriv->assoc_req) {
			memcpy(pmlmepriv->assoc_req, pwlanhdr,
			       pattrib->pktlen);
			pmlmepriv->assoc_req_len = pattrib->pktlen;
		}
	} else
		kfree(pmlmepriv->assoc_req);

	return;
}

/* when wait_ack is ture, this function shoule be called at process context */
static int _issue_nulldata23a(struct rtw_adapter *padapter, unsigned char *da,
			      unsigned int power_mode, int wait_ack)
{
	int ret = _FAIL;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	struct xmit_priv *pxmitpriv;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;

	/* DBG_8723A("%s:%d\n", __func__, power_mode); */

	if (!padapter)
		goto exit;

	pxmitpriv = &padapter->xmitpriv;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);
	pattrib->retry_ctrl = false;

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
		SetFrDs(fctrl);
	else if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
		SetToDs(fctrl);

	if (power_mode)
		SetPwrMgt(fctrl);

	ether_addr_copy(pwlanhdr->addr1, da);
	ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
	ether_addr_copy(pwlanhdr->addr3, get_my_bssid23a(&pmlmeinfo->network));

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_DATA_NULL);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pattrib->last_txcmdsz = pattrib->pktlen;

	if (wait_ack)
		ret = dump_mgntframe23a_and_wait_ack23a(padapter, pmgntframe);
	else {
		dump_mgntframe23a(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

/* when wait_ms >0 , this function shoule be called at process context */
/* da == NULL for station mode */
int issue_nulldata23a(struct rtw_adapter *padapter, unsigned char *da,
		      unsigned int power_mode, int try_cnt, int wait_ms)
{
	int ret;
	int i = 0;
	unsigned long start = jiffies;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	/* da == NULL, assum it's null data for sta to ap*/
	if (da == NULL)
		da = get_my_bssid23a(&pmlmeinfo->network);

	do {
		ret = _issue_nulldata23a(padapter, da, power_mode,
					 wait_ms > 0 ? true : false);

		i++;

		if (padapter->bDriverStopped || padapter->bSurpriseRemoved)
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			msleep(wait_ms);

	} while((i < try_cnt) && ((ret == _FAIL) || (wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		goto exit;
	}

	if (try_cnt && wait_ms) {
		if (da)
			DBG_8723A(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d "
				  "in %u ms\n", FUNC_ADPT_ARG(padapter),
				  MAC_ARG(da), rtw_get_oper_ch23a(padapter),
				  ret == _SUCCESS?", acked":"", i, try_cnt,
				  jiffies_to_msecs(jiffies - start));
		else
			DBG_8723A(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				  FUNC_ADPT_ARG(padapter),
				  rtw_get_oper_ch23a(padapter),
				  ret == _SUCCESS?", acked":"", i, try_cnt,
				  jiffies_to_msecs(jiffies - start));
	}
exit:
	return ret;
}

/* when wait_ack is ture, this function shoule be called at process context */
static int _issue_qos_nulldata23a(struct rtw_adapter *padapter,
				  unsigned char *da, u16 tid, int wait_ack)
{
	int ret = _FAIL;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl, *qc;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	DBG_8723A("%s\n", __func__);

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	pattrib->hdrlen += 2;
	pattrib->qos_en = true;
	pattrib->eosp = 1;
	pattrib->ack_policy = 0;
	pattrib->mdata = 0;

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

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

	ether_addr_copy(pwlanhdr->addr1, da);
	ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
	ether_addr_copy(pwlanhdr->addr3, get_my_bssid23a(&pmlmeinfo->network));

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_QOS_DATA_NULL);

	pframe += sizeof(struct ieee80211_qos_hdr);
	pattrib->pktlen = sizeof(struct ieee80211_qos_hdr);

	pattrib->last_txcmdsz = pattrib->pktlen;

	if (wait_ack)
		ret = dump_mgntframe23a_and_wait_ack23a(padapter, pmgntframe);
	else {
		dump_mgntframe23a(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

/* when wait_ms >0 , this function shoule be called at process context */
/* da == NULL for station mode */
int issue_qos_nulldata23a(struct rtw_adapter *padapter, unsigned char *da,
			  u16 tid, int try_cnt, int wait_ms)
{
	int ret;
	int i = 0;
	unsigned long start = jiffies;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	/* da == NULL, assum it's null data for sta to ap*/
	if (da == NULL)
		da = get_my_bssid23a(&pmlmeinfo->network);

	do {
		ret = _issue_qos_nulldata23a(padapter, da, tid,
					     wait_ms > 0 ? true : false);

		i++;

		if (padapter->bDriverStopped || padapter->bSurpriseRemoved)
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			msleep(wait_ms);
	} while((i < try_cnt) && ((ret == _FAIL)||(wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		goto exit;
	}

	if (try_cnt && wait_ms) {
		if (da)
			DBG_8723A(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d "
				  "in %u ms\n", FUNC_ADPT_ARG(padapter),
				  MAC_ARG(da), rtw_get_oper_ch23a(padapter),
				  ret == _SUCCESS?", acked":"", i, try_cnt,
				  jiffies_to_msecs(jiffies - start));
		else
			DBG_8723A(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				  FUNC_ADPT_ARG(padapter),
				  rtw_get_oper_ch23a(padapter),
				  ret == _SUCCESS?", acked":"", i, try_cnt,
				  jiffies_to_msecs(jiffies - start));
	}
exit:
	return ret;
}

static int _issue_deauth23a(struct rtw_adapter *padapter, unsigned char *da,
			    unsigned short reason, u8 wait_ack)
{
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	int ret = _FAIL;
#ifdef CONFIG_8723AU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif /* CONFIG_8723AU_P2P */

	/* DBG_8723A("%s to "MAC_FMT"\n", __func__, MAC_ARG(da)); */

#ifdef CONFIG_8723AU_P2P
	if (!(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) &&
	    (pwdinfo->rx_invitereq_info.scan_op_ch_only)) {
		mod_timer(&pwdinfo->reset_ch_sitesurvey,
			  jiffies + msecs_to_jiffies(10));
	}
#endif /* CONFIG_8723AU_P2P */

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		goto exit;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);
	pattrib->retry_ctrl = false;

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

	ether_addr_copy(pwlanhdr->addr1, da);
	ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
	ether_addr_copy(pwlanhdr->addr3, get_my_bssid23a(&pmlmeinfo->network));

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_DEAUTH);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	reason = cpu_to_le16(reason);
	pframe = rtw_set_fixed_ie23a(pframe, WLAN_REASON_PREV_AUTH_NOT_VALID,
				     (unsigned char *)&reason,
				     &pattrib->pktlen);

	pattrib->last_txcmdsz = pattrib->pktlen;

	if (wait_ack)
		ret = dump_mgntframe23a_and_wait_ack23a(padapter, pmgntframe);
	else {
		dump_mgntframe23a(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;
}

int issue_deauth23a(struct rtw_adapter *padapter, unsigned char *da,
		    unsigned short reason)
{
	DBG_8723A("%s to "MAC_FMT"\n", __func__, MAC_ARG(da));
	return _issue_deauth23a(padapter, da, reason, false);
}

int issue_deauth23a_ex23a(struct rtw_adapter *padapter, u8 *da,
			  unsigned short reason, int try_cnt, int wait_ms)
{
	int ret;
	int i = 0;
	unsigned long start = jiffies;

	do {
		ret = _issue_deauth23a(padapter, da, reason,
				       wait_ms >0 ? true : false);

		i++;

		if (padapter->bDriverStopped || padapter->bSurpriseRemoved)
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			msleep(wait_ms);

	} while((i < try_cnt) && ((ret == _FAIL)||(wait_ms == 0)));

	if (ret != _FAIL) {
		ret = _SUCCESS;
		goto exit;
	}

	if (try_cnt && wait_ms) {
		if (da)
			DBG_8723A(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d "
				  "in %u ms\n", FUNC_ADPT_ARG(padapter),
				  MAC_ARG(da), rtw_get_oper_ch23a(padapter),
				  ret == _SUCCESS?", acked":"", i, try_cnt,
				  jiffies_to_msecs(jiffies - start));
		else
			DBG_8723A(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				  FUNC_ADPT_ARG(padapter),
				  rtw_get_oper_ch23a(padapter),
				  ret == _SUCCESS?", acked":"", i, try_cnt,
				  jiffies_to_msecs(jiffies - start));
	}
exit:
	return ret;
}

void issue_action_spct_ch_switch23a(struct rtw_adapter *padapter,
				    u8 *ra, u8 new_ch, u8 ch_offset)
{
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	u8 category, action;

	DBG_8723A(FUNC_NDEV_FMT" ra ="MAC_FMT", ch:%u, offset:%u\n",
		FUNC_NDEV_ARG(padapter->pnetdev), MAC_ARG(ra),
		  new_ch, ch_offset);

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

	ether_addr_copy(pwlanhdr->addr1, ra); /* RA */
	ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv)); /* TA */
	ether_addr_copy(pwlanhdr->addr3, ra); /* DA = RA */

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	/* category, action */
	category = WLAN_CATEGORY_SPECTRUM_MGMT;
	action = WLAN_ACTION_SPCT_CHL_SWITCH;

	pframe = rtw_set_fixed_ie23a(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &action, &pattrib->pktlen);

	pframe = rtw_set_ie23a_ch_switch (pframe, &pattrib->pktlen, 0,
					  new_ch, 0);
	pframe = rtw_set_ie23a_secondary_ch_offset(pframe, &pattrib->pktlen,
		hal_ch_offset_to_secondary_ch_offset23a(ch_offset));

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe23a(padapter, pmgntframe);
}

void issue_action_BA23a(struct rtw_adapter *padapter, unsigned char *raddr,
			unsigned char action, unsigned short status)
{
	u8 category = WLAN_CATEGORY_BACK;
	u16 start_seq;
	u16 BA_para_set;
	u16 reason_code;
	u16 BA_timeout_value;
	u16 BA_starting_seqctrl;
	int max_rx_ampdu_factor;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	u8 *pframe;
	struct ieee80211_hdr *pwlanhdr;
	u16 *fctrl;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct sta_info *psta;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct registry_priv *pregpriv = &padapter->registrypriv;
#ifdef CONFIG_8723AU_BT_COEXIST
	u8 tendaAPMac[] = {0xC8, 0x3A, 0x35};
#endif

	DBG_8723A("%s, category =%d, action =%d, status =%d\n",
		  __func__, category, action, status);

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
		return;

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

	/* memcpy(pwlanhdr->addr1, get_my_bssid23a(&pmlmeinfo->network), ETH_ALEN); */
	ether_addr_copy(pwlanhdr->addr1, raddr);
	ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
	ether_addr_copy(pwlanhdr->addr3, get_my_bssid23a(&pmlmeinfo->network));

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie23a(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &action, &pattrib->pktlen);

	status = cpu_to_le16(status);

	if (category != 3)
		goto out;

	switch (action)
	{
	case 0: /* ADDBA req */
		do {
			pmlmeinfo->dialogToken++;
		} while (pmlmeinfo->dialogToken == 0);
		pframe = rtw_set_fixed_ie23a(pframe, 1, &pmlmeinfo->dialogToken,
					     &pattrib->pktlen);

#ifdef CONFIG_8723AU_BT_COEXIST
		if ((BT_1Ant(padapter) == true) &&
		    ((pmlmeinfo->assoc_AP_vendor != broadcomAP) ||
		     memcmp(raddr, tendaAPMac, 3))) {
			/*  A-MSDU NOT Supported */
			BA_para_set = 0;
			/*  immediate Block Ack */
			BA_para_set |= (1 << 1) &
				IEEE80211_ADDBA_PARAM_POLICY_MASK;
			/*  TID */
			BA_para_set |= (status << 2) &
				IEEE80211_ADDBA_PARAM_TID_MASK;
			/*  max buffer size is 8 MSDU */
			BA_para_set |= (8 << 6) &
				IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK;
		} else
#endif
		{
			/* immediate ack & 64 buffer size */
			BA_para_set = (0x1002 | ((status & 0xf) << 2));
		}
		BA_para_set = cpu_to_le16(BA_para_set);
		pframe = rtw_set_fixed_ie23a(pframe, 2,
					     (unsigned char *)&BA_para_set,
					     &pattrib->pktlen);

		BA_timeout_value = 5000;/*  5ms */
		BA_timeout_value = cpu_to_le16(BA_timeout_value);
		pframe = rtw_set_fixed_ie23a(pframe, 2, (unsigned char *)
					     &BA_timeout_value,
					     &pattrib->pktlen);

		/* if ((psta = rtw_get_stainfo23a(pstapriv,
		   pmlmeinfo->network.MacAddress)) != NULL) */
		if ((psta = rtw_get_stainfo23a(pstapriv, raddr))) {
			start_seq = (psta->sta_xmitpriv.txseq_tid[status & 0x07]&0xfff) + 1;

			DBG_8723A("BA_starting_seqctrl = %d for TID =%d\n",
				  start_seq, status & 0x07);

			psta->BA_starting_seqctrl[status & 0x07] = start_seq;

			BA_starting_seqctrl = start_seq << 4;
		}

		BA_starting_seqctrl = cpu_to_le16(BA_starting_seqctrl);
		pframe = rtw_set_fixed_ie23a(pframe, 2, (unsigned char *)&BA_starting_seqctrl, &pattrib->pktlen);
		break;

	case 1: /* ADDBA rsp */
		pframe = rtw_set_fixed_ie23a(pframe, 1, &pmlmeinfo->ADDBA_req.dialog_token, &pattrib->pktlen);
		pframe = rtw_set_fixed_ie23a(pframe, 2,
					     (unsigned char *)&status,
					     &pattrib->pktlen);
		rtw_hal_get_def_var23a(padapter, HW_VAR_MAX_RX_AMPDU_FACTOR,
				       &max_rx_ampdu_factor);
		if (max_rx_ampdu_factor == IEEE80211_HT_MAX_AMPDU_64K)
			BA_para_set = ((le16_to_cpu(pmlmeinfo->ADDBA_req.BA_para_set) & 0x3f) | 0x1000); /* 64 buffer size */
		else if (max_rx_ampdu_factor == IEEE80211_HT_MAX_AMPDU_32K)
			BA_para_set = ((le16_to_cpu(pmlmeinfo->ADDBA_req.BA_para_set) & 0x3f) | 0x0800); /* 32 buffer size */
		else if (max_rx_ampdu_factor == IEEE80211_HT_MAX_AMPDU_16K)
			BA_para_set = ((le16_to_cpu(pmlmeinfo->ADDBA_req.BA_para_set) & 0x3f) | 0x0400); /* 16 buffer size */
		else if (max_rx_ampdu_factor == IEEE80211_HT_MAX_AMPDU_8K)
			BA_para_set = ((le16_to_cpu(pmlmeinfo->ADDBA_req.BA_para_set) & 0x3f) | 0x0200); /* 8 buffer size */
		else
			BA_para_set = ((le16_to_cpu(pmlmeinfo->ADDBA_req.BA_para_set) & 0x3f) | 0x1000); /* 64 buffer size */

#ifdef CONFIG_8723AU_BT_COEXIST
		if ((BT_1Ant(padapter) == true) &&
		    ((pmlmeinfo->assoc_AP_vendor != broadcomAP) ||
		     memcmp(raddr, tendaAPMac, 3))) {
			/*  max buffer size is 8 MSDU */
			BA_para_set &= ~IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK;
			BA_para_set |= (8 << 6) &
				IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK;
		}
#endif

		if (pregpriv->ampdu_amsdu == 0)/* disabled */
			BA_para_set = cpu_to_le16(BA_para_set & ~BIT(0));
		else if (pregpriv->ampdu_amsdu == 1)/* enabled */
			BA_para_set = cpu_to_le16(BA_para_set | BIT(0));
		else /* auto */
			BA_para_set = cpu_to_le16(BA_para_set);

		pframe = rtw_set_fixed_ie23a(pframe, 2,
					     (unsigned char *)&BA_para_set,
					     &pattrib->pktlen);
		pframe = rtw_set_fixed_ie23a(pframe, 2, (unsigned char *)&pmlmeinfo->ADDBA_req.BA_timeout_value, &pattrib->pktlen);
		break;
	case 2:/* DELBA */
		BA_para_set = (status & 0x1F) << 3;
		BA_para_set = cpu_to_le16(BA_para_set);
		pframe = rtw_set_fixed_ie23a(pframe, 2,
					     (unsigned char *)&BA_para_set,
					     &pattrib->pktlen);

		reason_code = 37;/* Requested from peer STA as it does not
				    want to use the mechanism */
		reason_code = cpu_to_le16(reason_code);
		pframe = rtw_set_fixed_ie23a(pframe, 2,
					     (unsigned char *)&reason_code,
					     &pattrib->pktlen);
		break;
	default:
		break;
	}

out:
	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe23a(padapter, pmgntframe);
}

static void issue_action_BSSCoexistPacket(struct rtw_adapter *padapter)
{
	struct list_head *plist, *phead, *ptmp;
	unsigned char category, action;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char				*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short			*fctrl;
	struct	wlan_network	*pnetwork = NULL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct rtw_queue	*queue	= &pmlmepriv->scanned_queue;
	u8 InfoContent[16] = {0};
	u8 ICS[8][15];

	if ((pmlmepriv->num_FortyMHzIntolerant == 0) || (pmlmepriv->num_sta_no_ht == 0))
		return;

	if (true == pmlmeinfo->bwmode_updated)
		return;

	DBG_8723A("%s\n", __func__);

	category = WLAN_CATEGORY_PUBLIC;
	action = ACT_PUBLIC_BSSCOEXIST;

	if ((pmgntframe = alloc_mgtxmitframe23a(pxmitpriv)) == NULL)
	{
		return;
	}

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;

	ether_addr_copy(pwlanhdr->addr1, get_my_bssid23a(&pmlmeinfo->network));
	ether_addr_copy(pwlanhdr->addr2, myid(&padapter->eeprompriv));
	ether_addr_copy(pwlanhdr->addr3, get_my_bssid23a(&pmlmeinfo->network));

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie23a(pframe, 1, &category, &pattrib->pktlen);
	pframe = rtw_set_fixed_ie23a(pframe, 1, &action, &pattrib->pktlen);

	/*  */
	if (pmlmepriv->num_FortyMHzIntolerant>0)
	{
		u8 iedata = 0;

		iedata |= BIT(2);/* 20 MHz BSS Width Request */

		pframe = rtw_set_ie23a(pframe, EID_BSSCoexistence,  1, &iedata, &pattrib->pktlen);

	}

	/*  */
	memset(ICS, 0, sizeof(ICS));
	if (pmlmepriv->num_sta_no_ht>0)
	{
		int i;

		spin_lock_bh(&pmlmepriv->scanned_queue.lock);

		phead = get_list_head(queue);
		plist = phead->next;

		list_for_each_safe(plist, ptmp, phead) {
			int len;
			u8 *p;
			struct wlan_bssid_ex *pbss_network;

			pnetwork = container_of(plist, struct wlan_network,
						list);

			pbss_network = &pnetwork->network;

			p = rtw_get_ie23a(pbss_network->IEs + _FIXED_IE_LENGTH_, _HT_CAPABILITY_IE_, &len, pbss_network->IELength - _FIXED_IE_LENGTH_);
			if ((p == NULL) || (len == 0))/* non-HT */
			{
				if ((pbss_network->Configuration.DSConfig<= 0) || (pbss_network->Configuration.DSConfig>14))
					continue;

				ICS[0][pbss_network->Configuration.DSConfig]= 1;

				if (ICS[0][0] == 0)
					ICS[0][0] = 1;
			}

		}

		spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

		for (i = 0;i<8;i++)
		{
			if (ICS[i][0] == 1)
			{
				int j, k = 0;

				InfoContent[k] = i;
				/* SET_BSS_INTOLERANT_ELE_REG_CLASS(InfoContent, i); */
				k++;

				for (j = 1;j<= 14;j++)
				{
					if (ICS[i][j]== 1)
					{
						if (k<16)
						{
							InfoContent[k] = j; /* channel number */
							/* SET_BSS_INTOLERANT_ELE_CHANNEL(InfoContent+k, j); */
							k++;
						}
					}
				}

				pframe = rtw_set_ie23a(pframe, EID_BSSIntolerantChlReport, k, InfoContent, &pattrib->pktlen);

			}

		}

	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe23a(padapter, pmgntframe);
}

unsigned int send_delba23a(struct rtw_adapter *padapter, u8 initiator, u8 *addr)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta = NULL;
	/* struct recv_reorder_ctrl *preorder_ctrl; */
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u16 tid;

	if ((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		if (!(pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS))
			return _SUCCESS;

	psta = rtw_get_stainfo23a(pstapriv, addr);
	if (psta == NULL)
		return _SUCCESS;

	if (initiator == 0) {  /*  recipient */
		for (tid = 0; tid < MAXTID; tid++) {
			if (psta->recvreorder_ctrl[tid].enable == true) {
				DBG_8723A("rx agg disable tid(%d)\n", tid);
				issue_action_BA23a(padapter, addr, WLAN_ACTION_DELBA, (((tid <<1) |initiator)&0x1F));
				psta->recvreorder_ctrl[tid].enable = false;
				psta->recvreorder_ctrl[tid].indicate_seq = 0xffff;
			}
		}
	} else if (initiator == 1) { /*  originator */
		for (tid = 0; tid < MAXTID; tid++) {
			if (psta->htpriv.agg_enable_bitmap & BIT(tid)) {
				DBG_8723A("tx agg disable tid(%d)\n", tid);
				issue_action_BA23a(padapter, addr, WLAN_ACTION_DELBA, (((tid <<1) |initiator)&0x1F));
				psta->htpriv.agg_enable_bitmap &= ~BIT(tid);
				psta->htpriv.candidate_tid_bitmap &= ~BIT(tid);

			}
		}
	}
	return _SUCCESS;
}

unsigned int send_beacon23a(struct rtw_adapter *padapter)
{
	u8	bxmitok = false;
	int	issue = 0;
	int poll = 0;
	unsigned long start = jiffies;
	unsigned int passing_time;

	rtw_hal_set_hwreg23a(padapter, HW_VAR_BCN_VALID, NULL);
	do {
		issue_beacon23a(padapter, 100);
		issue++;
		do {
			yield();
			rtw23a_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, (u8 *)(&bxmitok));
			poll++;
		} while ((poll%10)!= 0 && false == bxmitok &&
			 !padapter->bSurpriseRemoved &&
			 !padapter->bDriverStopped);

	} while (!bxmitok && issue<100 && !padapter->bSurpriseRemoved &&
		 !padapter->bDriverStopped);

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped)
		return _FAIL;

	passing_time = jiffies_to_msecs(jiffies - start);

	if (!bxmitok) {
		DBG_8723A("%s fail! %u ms\n", __func__, passing_time);
		return _FAIL;
	} else {

		if (passing_time > 100 || issue > 3)
			DBG_8723A("%s success, issue:%d, poll:%d, %u ms\n",
				  __func__, issue, poll, passing_time);
		return _SUCCESS;
	}
}

/****************************************************************************

Following are some utitity fuctions for WiFi MLME

*****************************************************************************/

bool IsLegal5GChannel(struct rtw_adapter *Adapter, u8 channel)
{

	int i = 0;
	u8 Channel_5G[45] = {36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,
		60, 62, 64, 100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122,
		124, 126, 128, 130, 132, 134, 136, 138, 140, 149, 151, 153, 155, 157, 159,
		161, 163, 165};
	for (i = 0; i < sizeof(Channel_5G); i++)
		if (channel == Channel_5G[i])
			return true;
	return false;
}

void site_survey23a(struct rtw_adapter *padapter)
{
	unsigned char survey_channel = 0, val8;
	enum rt_scan_type ScanType = SCAN_PASSIVE;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u32 initialgain = 0;
#ifdef CONFIG_8723AU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	if ((pwdinfo->rx_invitereq_info.scan_op_ch_only) ||
	    (pwdinfo->p2p_info.scan_op_ch_only)) {
		if (pwdinfo->rx_invitereq_info.scan_op_ch_only)
			survey_channel = pwdinfo->rx_invitereq_info.operation_ch[pmlmeext->sitesurvey_res.channel_idx];
		else
			survey_channel = pwdinfo->p2p_info.operation_ch[pmlmeext->sitesurvey_res.channel_idx];
		ScanType = SCAN_ACTIVE;
	} else if (rtw_p2p_findphase_ex_is_social(pwdinfo)) {
		/* The driver is in the find phase, it should go through the social channel. */
		int ch_set_idx;
		survey_channel = pwdinfo->social_chan[pmlmeext->sitesurvey_res.channel_idx];
		ch_set_idx = rtw_ch_set_search_ch23a(pmlmeext->channel_set, survey_channel);
		if (ch_set_idx >= 0)
			ScanType = pmlmeext->channel_set[ch_set_idx].ScanType;
		else
			ScanType = SCAN_ACTIVE;
	} else
#endif /* CONFIG_8723AU_P2P */
	{
		struct rtw_ieee80211_channel *ch;
		if (pmlmeext->sitesurvey_res.channel_idx < pmlmeext->sitesurvey_res.ch_num) {
			ch = &pmlmeext->sitesurvey_res.ch[pmlmeext->sitesurvey_res.channel_idx];
			survey_channel = ch->hw_value;
			ScanType = (ch->flags & IEEE80211_CHAN_NO_IR) ? SCAN_PASSIVE : SCAN_ACTIVE;
}
	}

	if (survey_channel != 0) {
		/* PAUSE 4-AC Queue when site_survey23a */
		/* rtw23a_hal_get_hwreg(padapter, HW_VAR_TXPAUSE, (u8 *)(&val8)); */
		/* val8 |= 0x0f; */
		/* rtw_hal_set_hwreg23a(padapter, HW_VAR_TXPAUSE, (u8 *)(&val8)); */
		if (pmlmeext->sitesurvey_res.channel_idx == 0)
			set_channel_bwmode23a(padapter, survey_channel,
					      HAL_PRIME_CHNL_OFFSET_DONT_CARE,
					      HT_CHANNEL_WIDTH_20);
		else
			SelectChannel23a(padapter, survey_channel);

		if (ScanType == SCAN_ACTIVE) /* obey the channel plan setting... */
		{
#ifdef CONFIG_8723AU_P2P
			if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_SCAN) ||
				rtw_p2p_chk_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH)
			)
			{
				issue23a_probereq_p2p(padapter, NULL);
				issue23a_probereq_p2p(padapter, NULL);
				issue23a_probereq_p2p(padapter, NULL);
			}
			else
#endif /* CONFIG_8723AU_P2P */
			{
				int i;
				for (i = 0;i<RTW_SSID_SCAN_AMOUNT;i++) {
					if (pmlmeext->sitesurvey_res.ssid[i].ssid_len) {
						/* todo: to issue two probe req??? */
						issue_probereq23a(padapter, &pmlmeext->sitesurvey_res.ssid[i], NULL);
						/* msleep(SURVEY_TO>>1); */
						issue_probereq23a(padapter, &pmlmeext->sitesurvey_res.ssid[i], NULL);
					}
				}

				if (pmlmeext->sitesurvey_res.scan_mode == SCAN_ACTIVE) {
					/* todo: to issue two probe req??? */
					issue_probereq23a(padapter, NULL, NULL);
					/* msleep(SURVEY_TO>>1); */
					issue_probereq23a(padapter, NULL, NULL);
				}
			}
		}

		set_survey_timer(pmlmeext, pmlmeext->chan_scan_time);
	} else {

		/*	channel number is 0 or this channel is not valid. */


#ifdef CONFIG_8723AU_P2P
		if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_SCAN) || rtw_p2p_chk_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH))
		{
			if ((pwdinfo->rx_invitereq_info.scan_op_ch_only) || (pwdinfo->p2p_info.scan_op_ch_only))
			{
				/*	Set the find_phase_state_exchange_cnt to P2P_FINDPHASE_EX_CNT. */
				/*	This will let the following flow to run the scanning end. */
				rtw_p2p_findphase_ex_set(pwdinfo, P2P_FINDPHASE_EX_MAX);
			}
		}

		if (rtw_p2p_findphase_ex_is_needed(pwdinfo))
		{
			/*	Set the P2P State to the listen state of find phase and set the current channel to the listen channel */
			set_channel_bwmode23a(padapter, pwdinfo->listen_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
			rtw_p2p_set_state(pwdinfo, P2P_STATE_FIND_PHASE_LISTEN);
			pmlmeext->sitesurvey_res.state = SCAN_DISABLE;

			initialgain = 0xff; /* restore RX GAIN */
			rtw_hal_set_hwreg23a(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));
			/* turn on dynamic functions */
			Restore_DM_Func_Flag23a(padapter);
			/* Switch_DM_Func23a(padapter, DYNAMIC_FUNC_DIG|DYNAMIC_FUNC_HP|DYNAMIC_FUNC_SS, true); */

			mod_timer(&pwdinfo->find_phase_timer, jiffies +
				  msecs_to_jiffies(pwdinfo->listen_dwell * 100));
		} else
#endif /* CONFIG_8723AU_P2P */
		{
#ifdef CONFIG_8723AU_P2P
			if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_SCAN) || rtw_p2p_chk_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH))
				rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
			rtw_p2p_findphase_ex_set(pwdinfo, P2P_FINDPHASE_EX_NONE);
#endif /* CONFIG_8723AU_P2P */

			pmlmeext->sitesurvey_res.state = SCAN_COMPLETE;

			/* switch back to the original channel */

			set_channel_bwmode23a(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

			/* flush 4-AC Queue after site_survey23a */
			/* val8 = 0; */
			/* rtw_hal_set_hwreg23a(padapter, HW_VAR_TXPAUSE, (u8 *)(&val8)); */

			/* config MSR */
			Set_MSR23a(padapter, (pmlmeinfo->state & 0x3));

			initialgain = 0xff; /* restore RX GAIN */
			rtw_hal_set_hwreg23a(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));
			/* turn on dynamic functions */
			Restore_DM_Func_Flag23a(padapter);
			/* Switch_DM_Func23a(padapter, DYNAMIC_ALL_FUNC_ENABLE, true); */

			if (is_client_associated_to_ap23a(padapter) == true)
			{
				issue_nulldata23a(padapter, NULL, 0, 3, 500);

			}

			val8 = 0; /* survey done */
			rtw_hal_set_hwreg23a(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

			report_surveydone_event23a(padapter);

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
u8 collect_bss_info23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame, struct wlan_bssid_ex *bssid)
{
	int	i;
	u32	len;
	u8	*p;
	u16	val16;
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u8	*pframe = skb->data;
	u32	packet_len = skb->len;
	u8 ie_offset;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	len = packet_len - sizeof(struct ieee80211_hdr_3addr);

	if (len > MAX_IE_SZ)
	{
		/* DBG_8723A("IE too long for survey event\n"); */
		return _FAIL;
	}

	memset(bssid, 0, sizeof(struct wlan_bssid_ex));

	if (ieee80211_is_beacon(hdr->frame_control)) {
		bssid->reserved = 1;
		ie_offset = _BEACON_IE_OFFSET_;
	} else {
		/*  FIXME : more type */
		if (ieee80211_is_probe_req(hdr->frame_control)) {
			ie_offset = _PROBEREQ_IE_OFFSET_;
			bssid->reserved = 2;
		} else if (ieee80211_is_probe_resp(hdr->frame_control)) {
			ie_offset = _PROBERSP_IE_OFFSET_;
			bssid->reserved = 3;
		} else {
			bssid->reserved = 0;
			ie_offset = _FIXED_IE_LENGTH_;
		}
	}

	bssid->Length = sizeof(struct wlan_bssid_ex) - MAX_IE_SZ + len;

	/* below is to copy the information element */
	bssid->IELength = len;
	memcpy(bssid->IEs, (pframe + sizeof(struct ieee80211_hdr_3addr)), bssid->IELength);

	/* get the signal strength */
	bssid->Rssi = precv_frame->attrib.phy_info.RecvSignalPower; /*  in dBM.raw data */
	bssid->PhyInfo.SignalQuality = precv_frame->attrib.phy_info.SignalQuality;/* in percentage */
	bssid->PhyInfo.SignalStrength = precv_frame->attrib.phy_info.SignalStrength;/* in percentage */

	/*  checking SSID */
	if ((p = rtw_get_ie23a(bssid->IEs + ie_offset, _SSID_IE_, &len, bssid->IELength - ie_offset)) == NULL)
	{
		DBG_8723A("marc: cannot find SSID for survey event\n");
		return _FAIL;
	}

	if (*(p + 1)) {
		if (len > IEEE80211_MAX_SSID_LEN) {
			DBG_8723A("%s()-%d: IE too long (%d) for survey "
				  "event\n", __func__, __LINE__, len);
			return _FAIL;
		}
		memcpy(bssid->Ssid.ssid, (p + 2), *(p + 1));
		bssid->Ssid.ssid_len = *(p + 1);
	} else {
		bssid->Ssid.ssid_len = 0;
	}

	memset(bssid->SupportedRates, 0, NDIS_802_11_LENGTH_RATES_EX);

	/* checking rate info... */
	i = 0;
	p = rtw_get_ie23a(bssid->IEs + ie_offset, _SUPPORTEDRATES_IE_, &len, bssid->IELength - ie_offset);
	if (p != NULL)
	{
		if (len > NDIS_802_11_LENGTH_RATES_EX)
		{
			DBG_8723A("%s()-%d: IE too long (%d) for survey event\n", __func__, __LINE__, len);
			return _FAIL;
		}
		memcpy(bssid->SupportedRates, (p + 2), len);
		i = len;
	}

	p = rtw_get_ie23a(bssid->IEs + ie_offset, _EXT_SUPPORTEDRATES_IE_, &len, bssid->IELength - ie_offset);
	if (p != NULL)
	{
		if (len > (NDIS_802_11_LENGTH_RATES_EX-i))
		{
			DBG_8723A("%s()-%d: IE too long (%d) for survey event\n", __func__, __LINE__, len);
			return _FAIL;
		}
		memcpy(bssid->SupportedRates + i, (p + 2), len);
	}

	/* todo: */
	{
		bssid->NetworkTypeInUse = Ndis802_11OFDM24;
	}

	if (bssid->IELength < 12)
		return _FAIL;

	/*  Checking for DSConfig */
	p = rtw_get_ie23a(bssid->IEs + ie_offset, _DSSET_IE_, &len, bssid->IELength - ie_offset);

	bssid->Configuration.DSConfig = 0;
	bssid->Configuration.Length = 0;

	if (p)
	{
		bssid->Configuration.DSConfig = *(p + 2);
	}
	else
	{/*  In 5G, some ap do not have DSSET IE */
		/*  checking HT info for channel */
		p = rtw_get_ie23a(bssid->IEs + ie_offset, _HT_ADD_INFO_IE_, &len, bssid->IELength - ie_offset);
		if (p)
		{
			struct HT_info_element *HT_info = (struct HT_info_element *)(p + 2);
			bssid->Configuration.DSConfig = HT_info->primary_channel;
		}
		else
		{ /*  use current channel */
			bssid->Configuration.DSConfig = rtw_get_oper_ch23a(padapter);
		}
	}

	if (ieee80211_is_probe_req(hdr->frame_control)) {
		/*  FIXME */
		bssid->InfrastructureMode = Ndis802_11Infrastructure;
		ether_addr_copy(bssid->MacAddress, hdr->addr2);
		bssid->Privacy = 1;
		return _SUCCESS;
	}

	memcpy(&bssid->Configuration.BeaconPeriod, rtw_get_beacon_interval23a_from_ie(bssid->IEs), 2);
	bssid->Configuration.BeaconPeriod = le32_to_cpu(bssid->Configuration.BeaconPeriod);

	val16 = rtw_get_capability23a(bssid);

	if (val16 & BIT(0)) {
		bssid->InfrastructureMode = Ndis802_11Infrastructure;
		ether_addr_copy(bssid->MacAddress, hdr->addr2);
	} else {
		bssid->InfrastructureMode = Ndis802_11IBSS;
		ether_addr_copy(bssid->MacAddress, hdr->addr3);
	}

	if (val16 & BIT(4))
		bssid->Privacy = 1;
	else
		bssid->Privacy = 0;

	bssid->Configuration.ATIMWindow = 0;

	/* 20/40 BSS Coexistence check */
	if ((pregistrypriv->wifi_spec == 1) && (false == pmlmeinfo->bwmode_updated))
	{
		struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

		p = rtw_get_ie23a(bssid->IEs + ie_offset, _HT_CAPABILITY_IE_, &len, bssid->IELength - ie_offset);
		if (p && len > 0) {
			struct HT_caps_element	*pHT_caps;
			pHT_caps = (struct HT_caps_element	*)(p + 2);

			if (pHT_caps->u.HT_cap_element.HT_caps_info & BIT(14))
				pmlmepriv->num_FortyMHzIntolerant++;
		} else
		{
			pmlmepriv->num_sta_no_ht++;
		}
	}


	/*  mark bss info receving from nearby channel as SignalQuality 101 */
	if (bssid->Configuration.DSConfig != rtw_get_oper_ch23a(padapter))
		bssid->PhyInfo.SignalQuality = 101;

	return _SUCCESS;
}

void start_create_ibss23a(struct rtw_adapter* padapter)
{
	unsigned short	caps;
	u8	val8;
	u8	join_type;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *pnetwork = &pmlmeinfo->network;
	pmlmeext->cur_channel = (u8)pnetwork->Configuration.DSConfig;
	pmlmeinfo->bcn_interval = get_beacon_interval23a(pnetwork);

	/* update wireless mode */
	update_wireless_mode23a(padapter);

	/* udpate capability */
	caps = rtw_get_capability23a(pnetwork);
	update_capinfo23a(padapter, caps);
	if (caps&cap_IBSS)/* adhoc master */
	{
		val8 = 0xcf;
		rtw_hal_set_hwreg23a(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		/* switch channel */
		/* SelectChannel23a(padapter, pmlmeext->cur_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE); */
		set_channel_bwmode23a(padapter, pmlmeext->cur_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);

		beacon_timing_control23a(padapter);

		/* set msr to WIFI_FW_ADHOC_STATE */
		pmlmeinfo->state = WIFI_FW_ADHOC_STATE;
		Set_MSR23a(padapter, (pmlmeinfo->state & 0x3));

		/* issue beacon */
		if (send_beacon23a(padapter) == _FAIL)
		{
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("issuing beacon frame fail....\n"));

			report_join_res23a(padapter, -1);
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
		}
		else
		{
			rtw_hal_set_hwreg23a(padapter, HW_VAR_BSSID, padapter->registrypriv.dev_network.MacAddress);
			join_type = 0;
			rtw_hal_set_hwreg23a(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

			report_join_res23a(padapter, 1);
			pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;
		}
	}
	else
	{
		DBG_8723A("start_create_ibss23a, invalid cap:%x\n", caps);
		return;
	}
}

void start_clnt_join23a(struct rtw_adapter* padapter)
{
	unsigned short	caps;
	u8	val8;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *pnetwork = &pmlmeinfo->network;
	int beacon_timeout;

	pmlmeext->cur_channel = (u8)pnetwork->Configuration.DSConfig;
	pmlmeinfo->bcn_interval = get_beacon_interval23a(pnetwork);

	/* update wireless mode */
	update_wireless_mode23a(padapter);

	/* udpate capability */
	caps = rtw_get_capability23a(pnetwork);
	update_capinfo23a(padapter, caps);
	if (caps&cap_ESS) {
		/* switch channel */
		set_channel_bwmode23a(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

		Set_MSR23a(padapter, WIFI_FW_STATION_STATE);

		val8 = (pmlmeinfo->auth_algo == dot11AuthAlgrthm_8021X)? 0xcc: 0xcf;

		rtw_hal_set_hwreg23a(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		/* switch channel */
		/* set_channel_bwmode23a(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode); */

		/* here wait for receiving the beacon to start auth */
		/* and enable a timer */
		beacon_timeout = decide_wait_for_beacon_timeout23a(pmlmeinfo->bcn_interval);
		set_link_timer(pmlmeext, beacon_timeout);
		mod_timer(&padapter->mlmepriv.assoc_timer, jiffies +
			  msecs_to_jiffies((REAUTH_TO * REAUTH_LIMIT) + (REASSOC_TO*REASSOC_LIMIT) + beacon_timeout));
		pmlmeinfo->state = WIFI_FW_AUTH_NULL | WIFI_FW_STATION_STATE;
	}
	else if (caps&cap_IBSS) /* adhoc client */
	{
		Set_MSR23a(padapter, WIFI_FW_ADHOC_STATE);

		val8 = 0xcf;
		rtw_hal_set_hwreg23a(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		/* switch channel */
		set_channel_bwmode23a(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

		beacon_timing_control23a(padapter);

		pmlmeinfo->state = WIFI_FW_ADHOC_STATE;

		report_join_res23a(padapter, 1);
	}
	else
	{
		/* DBG_8723A("marc: invalid cap:%x\n", caps); */
		return;
	}
}

void start_clnt_auth23a(struct rtw_adapter* padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	del_timer_sync(&pmlmeext->link_timer);

	pmlmeinfo->state &= (~WIFI_FW_AUTH_NULL);
	pmlmeinfo->state |= WIFI_FW_AUTH_STATE;

	pmlmeinfo->auth_seq = 1;
	pmlmeinfo->reauth_count = 0;
	pmlmeinfo->reassoc_count = 0;
	pmlmeinfo->link_count = 0;
	pmlmeext->retry = 0;

	/*  Because of AP's not receiving deauth before */
	/*  AP may: 1)not response auth or 2)deauth us after link is complete */
	/*  issue deauth before issuing auth to deal with the situation */
	/*	Commented by Albert 2012/07/21 */
	/*	For the Win8 P2P connection, it will be hard to have a successful connection if this Wi-Fi doesn't connect to it. */
	issue_deauth23a(padapter, (&pmlmeinfo->network)->MacAddress, WLAN_REASON_DEAUTH_LEAVING);

	DBG_8723A_LEVEL(_drv_always_, "start auth\n");
	issue_auth23a(padapter, NULL, 0);

	set_link_timer(pmlmeext, REAUTH_TO);
}

void start_clnt_assoc23a(struct rtw_adapter* padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	del_timer_sync(&pmlmeext->link_timer);

	pmlmeinfo->state &= (~(WIFI_FW_AUTH_NULL | WIFI_FW_AUTH_STATE));
	pmlmeinfo->state |= (WIFI_FW_AUTH_SUCCESS | WIFI_FW_ASSOC_STATE);

	issue_assocreq23a(padapter);

	set_link_timer(pmlmeext, REASSOC_TO);
}

unsigned int receive_disconnect23a(struct rtw_adapter *padapter, unsigned char *MacAddr, unsigned short reason)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	/* check A3 */
	if (!ether_addr_equal(MacAddr, get_my_bssid23a(&pmlmeinfo->network)))
		return _SUCCESS;

	DBG_8723A("%s\n", __func__);

	if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
	{
		if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
		{
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
			report_del_sta_event23a(padapter, MacAddr, reason);

		}
		else if (pmlmeinfo->state & WIFI_FW_LINKING_STATE)
		{
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
			report_join_res23a(padapter, -2);
		}
	}

	return _SUCCESS;
}

static void process_80211d(struct rtw_adapter *padapter, struct wlan_bssid_ex *bssid)
{
	struct registry_priv *pregistrypriv;
	struct mlme_ext_priv *pmlmeext;
	struct rt_channel_info *chplan_new;
	u8 channel;
	u8 i;

	pregistrypriv = &padapter->registrypriv;
	pmlmeext = &padapter->mlmeextpriv;

	/*  Adjust channel plan by AP Country IE */
	if (pregistrypriv->enable80211d &&
		(!pmlmeext->update_channel_plan_by_ap_done))
	{
		u8 *ie, *p;
		u32 len;
		struct rt_channel_plan chplan_ap;
		struct rt_channel_info chplan_sta[MAX_CHANNEL_NUM];
		u8 country[4];
		u8 fcn; /*  first channel number */
		u8 noc; /*  number of channel */
		u8 j, k;

		ie = rtw_get_ie23a(bssid->IEs + _FIXED_IE_LENGTH_, _COUNTRY_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
		if (!ie) return;
		if (len < 6) return;

		ie += 2;
		p = ie;
		ie += len;

		memset(country, 0, 4);
		memcpy(country, p, 3);
		p += 3;
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
				("%s: 802.11d country =%s\n", __func__, country));

		i = 0;
		while ((ie - p) >= 3)
		{
			fcn = *(p++);
			noc = *(p++);
			p++;

			for (j = 0; j < noc; j++)
			{
				if (fcn <= 14) channel = fcn + j; /*  2.4 GHz */
				else channel = fcn + j*4; /*  5 GHz */

				chplan_ap.Channel[i++] = channel;
			}
		}
		chplan_ap.Len = i;

		memcpy(chplan_sta, pmlmeext->channel_set, sizeof(chplan_sta));
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
		} else {
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
/*					chplan_new[k].ScanType = chplan_sta[i].ScanType; */
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

			/*  change AP not support channel to Passive scan */
			while ((i < MAX_CHANNEL_NUM) && (chplan_sta[i].ChannelNum != 0)) {
				chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
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
		} else {
			/*  keep original STA 5G channel plan */
			while ((i < MAX_CHANNEL_NUM) && (chplan_sta[i].ChannelNum != 0)) {
				chplan_new[k].ChannelNum = chplan_sta[i].ChannelNum;
				chplan_new[k].ScanType = chplan_sta[i].ScanType;
				i++;
				k++;
			}
		}
		pmlmeext->update_channel_plan_by_ap_done = 1;
	}

	/*  If channel is used by AP, set channel scan type to active */
	channel = bssid->Configuration.DSConfig;
	chplan_new = pmlmeext->channel_set;
	i = 0;
	while ((i < MAX_CHANNEL_NUM) && (chplan_new[i].ChannelNum != 0)) {
		if (chplan_new[i].ChannelNum == channel)
		{
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

void report_survey_event23a(struct rtw_adapter *padapter, struct recv_frame *precv_frame)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct survey_event	*psurvey_evt;
	struct C2HEvent_Header *pc2h_evt_hdr;
	struct mlme_ext_priv *pmlmeext;
	struct cmd_priv *pcmdpriv;

	if (!padapter)
		return;

	pmlmeext = &padapter->mlmeextpriv;
	pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj),
					     GFP_ATOMIC);
	if (!pcmd_obj)
		return;

	cmdsz = (sizeof(struct survey_event) + sizeof(struct C2HEvent_Header));
	pevtcmd = kzalloc(cmdsz, GFP_ATOMIC);
	if (!pevtcmd) {
		kfree(pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct survey_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_Survey);
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	psurvey_evt = (struct survey_event*)(pevtcmd + sizeof(struct C2HEvent_Header));

	if (collect_bss_info23a(padapter, precv_frame, &psurvey_evt->bss) == _FAIL) {
		kfree(pcmd_obj);
		kfree(pevtcmd);
		return;
	}

	process_80211d(padapter, &psurvey_evt->bss);

	rtw_enqueue_cmd23a(pcmdpriv, pcmd_obj);

	pmlmeext->sitesurvey_res.bss_cnt++;

	return;
}

void report_surveydone_event23a(struct rtw_adapter *padapter)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct surveydone_event *psurveydone_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj),
					     GFP_ATOMIC);
	if (!pcmd_obj)
		return;

	cmdsz = (sizeof(struct surveydone_event) + sizeof(struct C2HEvent_Header));
	pevtcmd = kzalloc(cmdsz, GFP_ATOMIC);
	if (!pevtcmd) {
		kfree(pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct surveydone_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_SurveyDone);
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	psurveydone_evt = (struct surveydone_event*)(pevtcmd + sizeof(struct C2HEvent_Header));
	psurveydone_evt->bss_cnt = pmlmeext->sitesurvey_res.bss_cnt;

	DBG_8723A("survey done event(%x)\n", psurveydone_evt->bss_cnt);

	rtw_enqueue_cmd23a(pcmdpriv, pcmd_obj);

	return;
}

void report_join_res23a(struct rtw_adapter *padapter, int res)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct joinbss_event		*pjoinbss_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj),
					     GFP_ATOMIC);
	if (!pcmd_obj)
		return;

	cmdsz = (sizeof(struct joinbss_event) + sizeof(struct C2HEvent_Header));
	pevtcmd = kzalloc(cmdsz, GFP_ATOMIC);
	if (!pevtcmd) {
		kfree(pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct joinbss_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_JoinBss);
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	pjoinbss_evt = (struct joinbss_event*)(pevtcmd + sizeof(struct C2HEvent_Header));
	memcpy((unsigned char *)&pjoinbss_evt->network.network,
	       &pmlmeinfo->network, sizeof(struct wlan_bssid_ex));
	pjoinbss_evt->network.join_res	= pjoinbss_evt->network.aid = res;

	DBG_8723A("report_join_res23a(%d)\n", res);

	rtw_joinbss_event_prehandle23a(padapter, (u8 *)&pjoinbss_evt->network);

	rtw_enqueue_cmd23a(pcmdpriv, pcmd_obj);

	return;
}

void report_del_sta_event23a(struct rtw_adapter *padapter, unsigned char* MacAddr, unsigned short reason)
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

	pcmd_obj = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj),
					     GFP_ATOMIC);
	if (!pcmd_obj)
		return;

	cmdsz = (sizeof(struct stadel_event) + sizeof(struct C2HEvent_Header));
	pevtcmd = kzalloc(cmdsz, GFP_ATOMIC);
	if (!pevtcmd) {
		kfree(pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct stadel_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_DelSTA);
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	pdel_sta_evt = (struct stadel_event*)(pevtcmd + sizeof(struct C2HEvent_Header));
	ether_addr_copy((unsigned char *)&pdel_sta_evt->macaddr, MacAddr);
	memcpy((unsigned char *)pdel_sta_evt->rsvd, (unsigned char *)&reason,
	       2);

	psta = rtw_get_stainfo23a(&padapter->stapriv, MacAddr);
	if (psta)
		mac_id = (int)psta->mac_id;
	else
		mac_id = (-1);

	pdel_sta_evt->mac_id = mac_id;

	DBG_8723A("report_del_sta_event23a: delete STA, mac_id =%d\n", mac_id);

	rtw_enqueue_cmd23a(pcmdpriv, pcmd_obj);

	return;
}

void report_add_sta_event23a(struct rtw_adapter *padapter, unsigned char* MacAddr, int cam_idx)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct stassoc_event		*padd_sta_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	pcmd_obj = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj),
					     GFP_ATOMIC);
	if (!pcmd_obj)
		return;

	cmdsz = (sizeof(struct stassoc_event) + sizeof(struct C2HEvent_Header));
	pevtcmd = kzalloc(cmdsz, GFP_ATOMIC);
	if (!pevtcmd) {
		kfree(pcmd_obj);
		return;
	}

	INIT_LIST_HEAD(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct stassoc_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_AddSTA);
	pc2h_evt_hdr->seq = atomic_inc_return(&pmlmeext->event_seq);

	padd_sta_evt = (struct stassoc_event*)(pevtcmd + sizeof(struct C2HEvent_Header));
	ether_addr_copy((unsigned char *)&padd_sta_evt->macaddr, MacAddr);
	padd_sta_evt->cam_id = cam_idx;

	DBG_8723A("report_add_sta_event23a: add STA\n");

	rtw_enqueue_cmd23a(pcmdpriv, pcmd_obj);

	return;
}

/****************************************************************************

Following are the event callback functions

*****************************************************************************/

/* for sta/adhoc mode */
void update_sta_info23a(struct rtw_adapter *padapter, struct sta_info *psta)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;

	/* ERP */
	VCS_update23a(padapter, psta);

	/* HT */
	if (pmlmepriv->htpriv.ht_option)
	{
		psta->htpriv.ht_option = true;

		psta->htpriv.ampdu_enable = pmlmepriv->htpriv.ampdu_enable;

		if (support_short_GI23a(padapter, &pmlmeinfo->HT_caps))
			psta->htpriv.sgi = true;

		psta->qos_option = true;

	}
	else
	{
		psta->htpriv.ht_option = false;

		psta->htpriv.ampdu_enable = false;

		psta->htpriv.sgi = false;
		psta->qos_option = false;

	}
	psta->htpriv.bwmode = pmlmeext->cur_bwmode;
	psta->htpriv.ch_offset = pmlmeext->cur_ch_offset;

	psta->htpriv.agg_enable_bitmap = 0x0;/* reset */
	psta->htpriv.candidate_tid_bitmap = 0x0;/* reset */

	/* QoS */
	if (pmlmepriv->qospriv.qos_option)
		psta->qos_option = true;

	psta->state = _FW_LINKED;
}

void mlmeext_joinbss_event_callback23a(struct rtw_adapter *padapter, int join_res)
{
	struct sta_info		*psta, *psta_bmc;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *cur_network = &pmlmeinfo->network;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	u8	join_type;
	u16 media_status;

	if (join_res < 0)
	{
		join_type = 1;
		rtw_hal_set_hwreg23a(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));
		rtw_hal_set_hwreg23a(padapter, HW_VAR_BSSID, null_addr);

		/* restore to initial setting. */
		update_tx_basic_rate23a(padapter, padapter->registrypriv.wireless_mode);

		goto exit_mlmeext_joinbss_event_callback23a;
	}

	if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
	{
		/* for bc/mc */
		psta_bmc = rtw_get_bcmc_stainfo23a(padapter);
		if (psta_bmc)
		{
			pmlmeinfo->FW_sta_info[psta_bmc->mac_id].psta = psta_bmc;
			update_bmc_sta_support_rate23a(padapter, psta_bmc->mac_id);
			Update_RA_Entry23a(padapter, psta_bmc);
		}
	}

	/* turn on dynamic functions */
	Switch_DM_Func23a(padapter, DYNAMIC_ALL_FUNC_ENABLE, true);

	/*  update IOT-releated issue */
	update_IOT_info23a(padapter);

	rtw_hal_set_hwreg23a(padapter, HW_VAR_BASIC_RATE, cur_network->SupportedRates);

	/* BCN interval */
	rtw_hal_set_hwreg23a(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&pmlmeinfo->bcn_interval));

	/* udpate capability */
	update_capinfo23a(padapter, pmlmeinfo->capability);

	/* WMM, Update EDCA param */
	WMMOnAssocRsp23a(padapter);

	/* HT */
	HTOnAssocRsp23a(padapter);

	/* Set cur_channel&cur_bwmode&cur_ch_offset */
	set_channel_bwmode23a(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	psta = rtw_get_stainfo23a(pstapriv, cur_network->MacAddress);
	if (psta) /* only for infra. mode */
	{
		pmlmeinfo->FW_sta_info[psta->mac_id].psta = psta;

		/* DBG_8723A("set_sta_rate23a\n"); */

		psta->wireless_mode = pmlmeext->cur_wireless_mode;

		/* set per sta rate after updating HT cap. */
		set_sta_rate23a(padapter, psta);

		media_status = (psta->mac_id<<8)|1; /*   MACID|OPMODE: 1 means connect */
		rtw_hal_set_hwreg23a(padapter, HW_VAR_H2C_MEDIA_STATUS_RPT, (u8 *)&media_status);
	}

	join_type = 2;
	rtw_hal_set_hwreg23a(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

	if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
	{
		/*  correcting TSF */
		correct_TSF23a(padapter, pmlmeext);

		/* set_link_timer(pmlmeext, DISCONNECT_TO); */
	}

	rtw_lps_ctrl_wk_cmd23a(padapter, LPS_CTRL_CONNECT, 0);

exit_mlmeext_joinbss_event_callback23a:
	DBG_8723A("=>%s\n", __func__);
}

void mlmeext_sta_add_event_callback23a(struct rtw_adapter *padapter, struct sta_info *psta)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u8	join_type;

	DBG_8723A("%s\n", __func__);

	if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
	{
		if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)/* adhoc master or sta_count>1 */
		{
			/* nothing to do */
		}
		else/* adhoc client */
		{
			/* update TSF Value */
			/* update_TSF23a(pmlmeext, pframe, len); */

			/*  correcting TSF */
			correct_TSF23a(padapter, pmlmeext);

			/* start beacon */
			if (send_beacon23a(padapter) == _FAIL)
			{
				pmlmeinfo->FW_sta_info[psta->mac_id].status = 0;

				pmlmeinfo->state ^= WIFI_FW_ADHOC_STATE;

				return;
			}

			pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;

		}

		join_type = 2;
		rtw_hal_set_hwreg23a(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));
	}

	pmlmeinfo->FW_sta_info[psta->mac_id].psta = psta;

	/* rate radaptive */
	Update_RA_Entry23a(padapter, psta);

	/* update adhoc sta_info */
	update_sta_info23a(padapter, psta);
}

void mlmeext_sta_del_event_callback23a(struct rtw_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	if (is_client_associated_to_ap23a(padapter) || is_IBSS_empty23a(padapter))
	{
		/* set_opmode_cmd(padapter, infra_client_with_mlme); */

		rtw_hal_set_hwreg23a(padapter, HW_VAR_MLME_DISCONNECT, NULL);
		rtw_hal_set_hwreg23a(padapter, HW_VAR_BSSID, null_addr);

		/* restore to initial setting. */
		update_tx_basic_rate23a(padapter, padapter->registrypriv.wireless_mode);

		/* switch to the 20M Hz mode after disconnect */
		pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_20;
		pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

		/* SelectChannel23a(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset); */
		set_channel_bwmode23a(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

		flush_all_cam_entry23a(padapter);

		pmlmeinfo->state = WIFI_FW_NULL_STATE;

		/* set MSR to no link state -> infra. mode */
		Set_MSR23a(padapter, _HW_STATE_STATION_);

		del_timer_sync(&pmlmeext->link_timer);
	}
}

/****************************************************************************

Following are the functions for the timer handlers

*****************************************************************************/
void linked23a_rx_sig_stren_disp(struct rtw_adapter *padapter)
{
	struct mlme_ext_priv    *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u8 mac_id;
	int UndecoratedSmoothedPWDB;
	if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
		mac_id = 0;
	else if ((pmlmeinfo->state&0x03) == _HW_STATE_AP_)
		mac_id = 2;

	rtw_hal_get_def_var23a(padapter, HW_DEF_RA_INFO_DUMP,&mac_id);

	rtw_hal_get_def_var23a(padapter, HAL_DEF_UNDERCORATEDSMOOTHEDPWDB, &UndecoratedSmoothedPWDB);
	DBG_8723A("UndecoratedSmoothedPWDB:%d\n", UndecoratedSmoothedPWDB);
}

static u8 chk_ap_is_alive(struct rtw_adapter *padapter, struct sta_info *psta)
{
	u8 ret = false;

	if ((sta_rx_data_pkts(psta) == sta_last_rx_data_pkts(psta)) &&
	    sta_rx_beacon_pkts(psta) == sta_last_rx_beacon_pkts(psta) &&
	    sta_rx_probersp_pkts(psta) == sta_last_rx_probersp_pkts(psta))
		ret = false;
	else
		ret = true;

	sta_update_last_rx_pkts(psta);
	return ret;
}

void linked_status_chk23a(struct rtw_adapter *padapter)
{
	u32	i;
	struct sta_info		*psta;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct sta_priv		*pstapriv = &padapter->stapriv;

	if (padapter->bRxRSSIDisplay)
		 linked23a_rx_sig_stren_disp(padapter);

	rtw_hal_sreset_linked_status_check23a(padapter);

	if (is_client_associated_to_ap23a(padapter))
	{
		/* linked infrastructure client mode */

		int tx_chk = _SUCCESS, rx_chk = _SUCCESS;
		int rx_chk_limit;

		rx_chk_limit = 4;

		if ((psta = rtw_get_stainfo23a(pstapriv, pmlmeinfo->network.MacAddress)) != NULL)
		{
			bool is_p2p_enable = false;
#ifdef CONFIG_8723AU_P2P
			is_p2p_enable = !rtw_p2p_chk_state(&padapter->wdinfo, P2P_STATE_NONE);
#endif

			if (chk_ap_is_alive(padapter, psta) == false)
				rx_chk = _FAIL;

			if (pxmitpriv->last_tx_pkts == pxmitpriv->tx_pkts)
				tx_chk = _FAIL;

			if (pmlmeext->active_keep_alive_check && (rx_chk == _FAIL || tx_chk == _FAIL)) {
				u8 backup_oper_channel = 0;

				/* switch to correct channel of current network  before issue keep-alive frames */
				if (rtw_get_oper_ch23a(padapter) != pmlmeext->cur_channel) {
					backup_oper_channel = rtw_get_oper_ch23a(padapter);
					SelectChannel23a(padapter, pmlmeext->cur_channel);
				}

				if (rx_chk != _SUCCESS)
					issue_probereq23a_ex23a(padapter, &pmlmeinfo->network.Ssid, psta->hwaddr, 3, 1);

				if ((tx_chk != _SUCCESS && pmlmeinfo->link_count++ == 0xf) || rx_chk != _SUCCESS) {
					tx_chk = issue_nulldata23a(padapter, psta->hwaddr, 0, 3, 1);
					/* if tx acked and p2p disabled, set rx_chk _SUCCESS to reset retry count */
					if (tx_chk == _SUCCESS && !is_p2p_enable)
						rx_chk = _SUCCESS;
				}

				/* back to the original operation channel */
				if (backup_oper_channel>0)
					SelectChannel23a(padapter, backup_oper_channel);

			} else {
				if (rx_chk != _SUCCESS) {
					if (pmlmeext->retry == 0) {
						issue_probereq23a(padapter, &pmlmeinfo->network.Ssid, pmlmeinfo->network.MacAddress);
						issue_probereq23a(padapter, &pmlmeinfo->network.Ssid, pmlmeinfo->network.MacAddress);
						issue_probereq23a(padapter, &pmlmeinfo->network.Ssid, pmlmeinfo->network.MacAddress);
					}
				}

				if (tx_chk != _SUCCESS && pmlmeinfo->link_count++ == 0xf)
					tx_chk = issue_nulldata23a(padapter, NULL, 0, 1, 0);
			}

			if (rx_chk == _FAIL) {
				pmlmeext->retry++;
				if (pmlmeext->retry > rx_chk_limit) {
					DBG_8723A_LEVEL(_drv_always_, FUNC_ADPT_FMT" disconnect or roaming\n",
						FUNC_ADPT_ARG(padapter));
					receive_disconnect23a(padapter, pmlmeinfo->network.MacAddress,
						WLAN_REASON_EXPIRATION_CHK);
					return;
				}
			} else {
				pmlmeext->retry = 0;
			}

			if (tx_chk == _FAIL) {
				pmlmeinfo->link_count &= 0xf;
			} else {
				pxmitpriv->last_tx_pkts = pxmitpriv->tx_pkts;
				pmlmeinfo->link_count = 0;
			}

		} /* end of if ((psta = rtw_get_stainfo23a(pstapriv, passoc_res->network.MacAddress)) != NULL) */
	}
	else if (is_client_associated_to_ibss23a(padapter))
	{
		/* linked IBSS mode */
		/* for each assoc list entry to check the rx pkt counter */
		for (i = IBSS_START_MAC_ID; i < NUM_STA; i++)
		{
			if (pmlmeinfo->FW_sta_info[i].status == 1)
			{
				psta = pmlmeinfo->FW_sta_info[i].psta;

				if (NULL == psta) continue;

				if (pmlmeinfo->FW_sta_info[i].rx_pkt == sta_rx_pkts(psta))
				{

					if (pmlmeinfo->FW_sta_info[i].retry<3)
					{
						pmlmeinfo->FW_sta_info[i].retry++;
					}
					else
					{
						pmlmeinfo->FW_sta_info[i].retry = 0;
						pmlmeinfo->FW_sta_info[i].status = 0;
						report_del_sta_event23a(padapter, psta->hwaddr,
							65535/*  indicate disconnect caused by no rx */
						);
					}
				}
				else
				{
					pmlmeinfo->FW_sta_info[i].retry = 0;
					pmlmeinfo->FW_sta_info[i].rx_pkt = (u32)sta_rx_pkts(psta);
				}
			}
		}

		/* set_link_timer(pmlmeext, DISCONNECT_TO); */

	}
}

static void survey_timer_hdl(unsigned long data)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)data;
	struct cmd_obj *ph2c;
	struct sitesurvey_parm *psurveyPara;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
#ifdef CONFIG_8723AU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif

	/* issue rtw_sitesurvey_cmd23a */
	if (pmlmeext->sitesurvey_res.state > SCAN_START) {
		if (pmlmeext->sitesurvey_res.state ==  SCAN_PROCESS)
			pmlmeext->sitesurvey_res.channel_idx++;

		if (pmlmeext->scan_abort == true)
		{
#ifdef CONFIG_8723AU_P2P
			if (!rtw_p2p_chk_state(&padapter->wdinfo, P2P_STATE_NONE))
			{
				rtw_p2p_findphase_ex_set(pwdinfo, P2P_FINDPHASE_EX_MAX);
				pmlmeext->sitesurvey_res.channel_idx = 3;
				DBG_8723A("%s idx:%d, cnt:%u\n", __func__,
					  pmlmeext->sitesurvey_res.channel_idx,
					  pwdinfo->find_phase_state_exchange_cnt);
			} else
			#endif
			{
				pmlmeext->sitesurvey_res.channel_idx = pmlmeext->sitesurvey_res.ch_num;
				DBG_8723A("%s idx:%d\n", __func__,
					  pmlmeext->sitesurvey_res.channel_idx);
			}

			pmlmeext->scan_abort = false;/* reset */
		}

		ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj),
			GFP_ATOMIC);
		if (!ph2c)
			goto exit_survey_timer_hdl;

		psurveyPara = (struct sitesurvey_parm*)
			kzalloc(sizeof(struct sitesurvey_parm), GFP_ATOMIC);
		if (!psurveyPara) {
			kfree(ph2c);
			goto exit_survey_timer_hdl;
		}

		init_h2fwcmd_w_parm_no_rsp(ph2c, psurveyPara, GEN_CMD_CODE(_SiteSurvey));
		rtw_enqueue_cmd23a(pcmdpriv, ph2c);
	}

exit_survey_timer_hdl:
	return;
}

static void link_timer_hdl(unsigned long data)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)data;
	/* static unsigned int		rx_pkt = 0; */
	/* static u64				tx_cnt = 0; */
	/* struct xmit_priv *pxmitpriv = &padapter->xmitpriv; */
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	/* struct sta_priv		*pstapriv = &padapter->stapriv; */

	if (pmlmeinfo->state & WIFI_FW_AUTH_NULL)
	{
		DBG_8723A("link_timer_hdl:no beacon while connecting\n");
		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		report_join_res23a(padapter, -3);
	}
	else if (pmlmeinfo->state & WIFI_FW_AUTH_STATE)
	{
		/* re-auth timer */
		if (++pmlmeinfo->reauth_count > REAUTH_LIMIT)
		{
			/* if (pmlmeinfo->auth_algo != dot11AuthAlgrthm_Auto) */
			/*  */
				pmlmeinfo->state = 0;
				report_join_res23a(padapter, -1);
				return;
			/*  */
			/* else */
			/*  */
			/*	pmlmeinfo->auth_algo = dot11AuthAlgrthm_Shared; */
			/*	pmlmeinfo->reauth_count = 0; */
			/*  */
		}

		DBG_8723A("link_timer_hdl: auth timeout and try again\n");
		pmlmeinfo->auth_seq = 1;
		issue_auth23a(padapter, NULL, 0);
		set_link_timer(pmlmeext, REAUTH_TO);
	}
	else if (pmlmeinfo->state & WIFI_FW_ASSOC_STATE)
	{
		/* re-assoc timer */
		if (++pmlmeinfo->reassoc_count > REASSOC_LIMIT)
		{
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
			report_join_res23a(padapter, -2);
			return;
		}

		DBG_8723A("link_timer_hdl: assoc timeout and try again\n");
		issue_assocreq23a(padapter);
		set_link_timer(pmlmeext, REASSOC_TO);
	}

	return;
}

static void addba_timer_hdl(unsigned long data)
{
	struct sta_info *psta = (struct sta_info *)data;
	struct ht_priv	*phtpriv;

	if (!psta)
		return;

	phtpriv = &psta->htpriv;

	if ((phtpriv->ht_option == true) && (phtpriv->ampdu_enable == true))
	{
		if (phtpriv->candidate_tid_bitmap)
			phtpriv->candidate_tid_bitmap = 0x0;

	}
}

void init_addba_retry_timer23a(struct sta_info *psta)
{
	setup_timer(&psta->addba_retry_timer, addba_timer_hdl,
		    (unsigned long)psta);
}

void init_mlme_ext_timer23a(struct rtw_adapter *padapter)
{
	struct	mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	setup_timer(&pmlmeext->survey_timer, survey_timer_hdl,
		    (unsigned long)padapter);

	setup_timer(&pmlmeext->link_timer, link_timer_hdl,
		    (unsigned long)padapter);
}

u8 NULL_hdl23a(struct rtw_adapter *padapter, u8 *pbuf)
{
	return H2C_SUCCESS;
}

u8 setopmode_hdl23a(struct rtw_adapter *padapter, u8 *pbuf)
{
	u8	type;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct setopmode_parm *psetop = (struct setopmode_parm *)pbuf;

	if (psetop->mode == Ndis802_11APMode)
	{
		pmlmeinfo->state = WIFI_FW_AP_STATE;
		type = _HW_STATE_AP_;
	}
	else if (psetop->mode == Ndis802_11Infrastructure)
	{
		pmlmeinfo->state &= ~(BIT(0)|BIT(1));/*  clear state */
		pmlmeinfo->state |= WIFI_FW_STATION_STATE;/* set to	STATION_STATE */
		type = _HW_STATE_STATION_;
	}
	else if (psetop->mode == Ndis802_11IBSS)
	{
		type = _HW_STATE_ADHOC_;
	}
	else
	{
		type = _HW_STATE_NOLINK_;
	}

	rtw_hal_set_hwreg23a(padapter, HW_VAR_SET_OPMODE, (u8 *)(&type));
	/* Set_NETYPE0_MSR(padapter, type); */

	return H2C_SUCCESS;
}

u8 createbss_hdl23a(struct rtw_adapter *padapter, u8 *pbuf)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *pnetwork = &pmlmeinfo->network;
	struct wlan_bssid_ex *pparm = (struct wlan_bssid_ex *)pbuf;
	/* u32	initialgain; */

	if (pparm->InfrastructureMode == Ndis802_11APMode) {
#ifdef CONFIG_8723AU_AP_MODE

		if (pmlmeinfo->state == WIFI_FW_AP_STATE)
		{
			/* todo: */
			return H2C_SUCCESS;
		}
#endif
	}

	/* below is for ad-hoc master */
	if (pparm->InfrastructureMode == Ndis802_11IBSS) {
		rtw_joinbss_reset23a(padapter);

		pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_20;
		pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		pmlmeinfo->ERP_enable = 0;
		pmlmeinfo->WMM_enable = 0;
		pmlmeinfo->HT_enable = 0;
		pmlmeinfo->HT_caps_enable = 0;
		pmlmeinfo->HT_info_enable = 0;
		pmlmeinfo->agg_enable_bitmap = 0;
		pmlmeinfo->candidate_tid_bitmap = 0;

		/* disable dynamic functions, such as high power, DIG */
		Save_DM_Func_Flag23a(padapter);
		Switch_DM_Func23a(padapter, DYNAMIC_FUNC_DISABLE, false);

		/* config the initial gain under linking, need to write the BB registers */
		/* initialgain = 0x1E; */
		/* rtw_hal_set_hwreg23a(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain)); */

		/* cancel link timer */
		del_timer_sync(&pmlmeext->link_timer);

		/* clear CAM */
		flush_all_cam_entry23a(padapter);

		if (pparm->IELength > MAX_IE_SZ)/* Check pbuf->IELength */
			return H2C_PARAMETERS_ERROR;

		memcpy(pnetwork, pparm, sizeof(struct wlan_bssid_ex));

		start_create_ibss23a(padapter);
	}

	return H2C_SUCCESS;
}

u8 join_cmd_hdl23a(struct rtw_adapter *padapter, u8 *pbuf)
{
	u8	join_type;
	struct ndis_802_11_var_ies *	pIE;
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *pnetwork = &pmlmeinfo->network;
	struct wlan_bssid_ex *pparm = (struct wlan_bssid_ex *)pbuf;
	struct HT_info_element *pht_info;
	u32 i;
        /* u32	initialgain; */
	/* u32	acparm; */

	/* check already connecting to AP or not */
	if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
	{
		if (pmlmeinfo->state & WIFI_FW_STATION_STATE)
			issue_deauth23a_ex23a(padapter, pnetwork->MacAddress,
					WLAN_REASON_DEAUTH_LEAVING, 5, 100);

		pmlmeinfo->state = WIFI_FW_NULL_STATE;

		/* clear CAM */
		flush_all_cam_entry23a(padapter);

		del_timer_sync(&pmlmeext->link_timer);

		/* set MSR to nolink -> infra. mode */
		/* Set_MSR23a(padapter, _HW_STATE_NOLINK_); */
		Set_MSR23a(padapter, _HW_STATE_STATION_);

		rtw_hal_set_hwreg23a(padapter, HW_VAR_MLME_DISCONNECT, NULL);
	}

	rtw_joinbss_reset23a(padapter);

	pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_20;
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

	if (pparm->IELength > MAX_IE_SZ)/* Check pbuf->IELength */
		return H2C_PARAMETERS_ERROR;

	memcpy(pnetwork, pbuf, sizeof(struct wlan_bssid_ex));

	/* Check AP vendor to move rtw_joinbss_cmd23a() */
	/* pmlmeinfo->assoc_AP_vendor = check_assoc_AP23a(pnetwork->IEs,
	   pnetwork->IELength); */

	for (i = sizeof(struct ndis_802_11_fixed_ies); i < pnetwork->IELength;)
	{
		pIE = (struct ndis_802_11_var_ies *)(pnetwork->IEs + i);

		switch (pIE->ElementID)
		{
		case _VENDOR_SPECIFIC_IE_:/* Get WMM IE. */
			if (!memcmp(pIE->data, WMM_OUI23A, 4))
				pmlmeinfo->WMM_enable = 1;
			break;

		case _HT_CAPABILITY_IE_:	/* Get HT Cap IE. */
			pmlmeinfo->HT_caps_enable = 1;
			break;

		case _HT_EXTRA_INFO_IE_:	/* Get HT Info IE. */
			pmlmeinfo->HT_info_enable = 1;

			/* spec case only for cisco's ap because cisco's ap
			 * issue assoc rsp using mcs rate @40MHz or @20MHz */
			pht_info = (struct HT_info_element *)(pIE->data);

			if ((pregpriv->cbw40_enable) &&
			    (pht_info->infos[0] & BIT(2))) {
				/* switch to the 40M Hz mode according to AP */
				pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_40;
				switch (pht_info->infos[0] & 0x3)
				{
				case 1:
					pmlmeext->cur_ch_offset =
						HAL_PRIME_CHNL_OFFSET_LOWER;
					break;

				case 3:
					pmlmeext->cur_ch_offset =
						HAL_PRIME_CHNL_OFFSET_UPPER;
					break;

				default:
					pmlmeext->cur_ch_offset =
						HAL_PRIME_CHNL_OFFSET_DONT_CARE;
					break;
				}

				DBG_8723A("set ch/bw before connected\n");
			}
			break;

		default:
			break;
		}

		i += (pIE->Length + 2);
	}
	/* disable dynamic functions, such as high power, DIG */
	/* Switch_DM_Func23a(padapter, DYNAMIC_FUNC_DISABLE, false); */

	/* config the initial gain under linking, need to write the BB
	   registers */
	/* initialgain = 0x1E; */
	/* rtw_hal_set_hwreg23a(padapter, HW_VAR_INITIAL_GAIN,
	   (u8 *)(&initialgain)); */

	rtw_hal_set_hwreg23a(padapter, HW_VAR_BSSID,
			  pmlmeinfo->network.MacAddress);
	join_type = 0;
	rtw_hal_set_hwreg23a(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

	/* cancel link timer */
	del_timer_sync(&pmlmeext->link_timer);

	start_clnt_join23a(padapter);

	return H2C_SUCCESS;
}

u8 disconnect_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf)
{
	struct disconnect_parm *param = (struct disconnect_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *pnetwork = &pmlmeinfo->network;
	u8	val8;

	if (is_client_associated_to_ap23a(padapter))
	{
		issue_deauth23a_ex23a(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING, param->deauth_timeout_ms/100, 100);
	}

	/* set_opmode_cmd(padapter, infra_client_with_mlme); */

	/* pmlmeinfo->state = WIFI_FW_NULL_STATE; */

	rtw_hal_set_hwreg23a(padapter, HW_VAR_MLME_DISCONNECT, NULL);
	rtw_hal_set_hwreg23a(padapter, HW_VAR_BSSID, null_addr);

	/* restore to initial setting. */
	update_tx_basic_rate23a(padapter, padapter->registrypriv.wireless_mode);

	if (((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
	{
		/* Stop BCN */
		val8 = 0;
		rtw_hal_set_hwreg23a(padapter, HW_VAR_BCN_FUNC, (u8 *)(&val8));
	}

	/* set MSR to no link state -> infra. mode */
	Set_MSR23a(padapter, _HW_STATE_STATION_);

	pmlmeinfo->state = WIFI_FW_NULL_STATE;

	/* switch to the 20M Hz mode after disconnect */
	pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_20;
	pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	set_channel_bwmode23a(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	flush_all_cam_entry23a(padapter);

	del_timer_sync(&pmlmeext->link_timer);

	rtw_free_uc_swdec_pending_queue23a(padapter);

	return	H2C_SUCCESS;
}

static int rtw_scan_ch_decision(struct rtw_adapter *padapter, struct rtw_ieee80211_channel *out,
	u32 out_num, struct rtw_ieee80211_channel *in, u32 in_num)
{
	int i, j;
	int scan_ch_num = 0;
	int set_idx;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	/* clear out first */
	memset(out, 0, sizeof(struct rtw_ieee80211_channel)*out_num);

	/* acquire channels from in */
	j = 0;
	for (i = 0;i<in_num;i++) {
		if (0)
		DBG_8723A(FUNC_ADPT_FMT" "CHAN_FMT"\n", FUNC_ADPT_ARG(padapter), CHAN_ARG(&in[i]));
		if (in[i].hw_value && !(in[i].flags & IEEE80211_CHAN_DISABLED)
			&& (set_idx = rtw_ch_set_search_ch23a(pmlmeext->channel_set, in[i].hw_value)) >= 0
		)
		{
			memcpy(&out[j], &in[i], sizeof(struct rtw_ieee80211_channel));

			if (pmlmeext->channel_set[set_idx].ScanType == SCAN_PASSIVE)
				out[j].flags &= IEEE80211_CHAN_NO_IR;

			j++;
		}
		if (j>= out_num)
			break;
	}

	/* if out is empty, use channel_set as default */
	if (j == 0) {
		for (i = 0;i<pmlmeext->max_chan_nums;i++) {
			out[i].hw_value = pmlmeext->channel_set[i].ChannelNum;

			if (pmlmeext->channel_set[i].ScanType == SCAN_PASSIVE)
				out[i].flags &= IEEE80211_CHAN_NO_IR;

			j++;
		}
	}

	if (padapter->setband == GHZ_24) {				/*  2.4G */
		for (i = 0; i < j ; i++) {
			if (out[i].hw_value > 35)
				memset(&out[i], 0,
				       sizeof(struct rtw_ieee80211_channel));
			else
				scan_ch_num++;
		}
		j = scan_ch_num;
	} else if  (padapter->setband == GHZ_50) {			/*  5G */
		for (i = 0; i < j ; i++) {
			if (out[i].hw_value > 35) {
				memcpy(&out[scan_ch_num++], &out[i], sizeof(struct rtw_ieee80211_channel));
			}
		}
		j = scan_ch_num;
	} else
		{}

	return j;
}

u8 sitesurvey_cmd_hdl23a(struct rtw_adapter *padapter, u8 *pbuf)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct sitesurvey_parm *pparm = (struct sitesurvey_parm *)pbuf;
	u8 bdelayscan = false;
	u8 val8;
	u32 initialgain;
	u32 i;

	if (pmlmeext->sitesurvey_res.state == SCAN_DISABLE) {
		/* for first time sitesurvey_cmd */
		rtw_hal_set_hwreg23a(padapter, HW_VAR_CHECK_TXBUF, NULL);

		pmlmeext->sitesurvey_res.state = SCAN_START;
		pmlmeext->sitesurvey_res.bss_cnt = 0;
		pmlmeext->sitesurvey_res.channel_idx = 0;

		for (i = 0; i < RTW_SSID_SCAN_AMOUNT; i++) {
			if (pparm->ssid[i].ssid_len) {
				memcpy(pmlmeext->sitesurvey_res.ssid[i].ssid,
				       pparm->ssid[i].ssid, IW_ESSID_MAX_SIZE);
				pmlmeext->sitesurvey_res.ssid[i].ssid_len =
					pparm->ssid[i].ssid_len;
			} else {
				pmlmeext->sitesurvey_res.ssid[i].ssid_len = 0;
			}
		}

		pmlmeext->sitesurvey_res.ch_num =
			rtw_scan_ch_decision(padapter,
					     pmlmeext->sitesurvey_res.ch,
					     RTW_CHANNEL_SCAN_AMOUNT,
					     pparm->ch, pparm->ch_num);

		pmlmeext->sitesurvey_res.scan_mode = pparm->scan_mode;

		/* issue null data if associating to the AP */
		if (is_client_associated_to_ap23a(padapter)) {
			pmlmeext->sitesurvey_res.state = SCAN_TXNULL;

			/* switch to correct channel of current network
			   before issue keep-alive frames */
			if (rtw_get_oper_ch23a(padapter) != pmlmeext->cur_channel)
				SelectChannel23a(padapter, pmlmeext->cur_channel);

			issue_nulldata23a(padapter, NULL, 1, 3, 500);

			bdelayscan = true;
		}

		if (bdelayscan) {
			/* delay 50ms to protect nulldata(1). */
			set_survey_timer(pmlmeext, 50);
			return H2C_SUCCESS;
		}
	}

	if ((pmlmeext->sitesurvey_res.state == SCAN_START) ||
	    (pmlmeext->sitesurvey_res.state == SCAN_TXNULL)) {
		/* disable dynamic functions, such as high power, DIG */
		Save_DM_Func_Flag23a(padapter);
		Switch_DM_Func23a(padapter, DYNAMIC_FUNC_DISABLE, false);

		/* config the initial gain under scaning, need to
		   write the BB registers */
		if ((wdev_to_priv(padapter->rtw_wdev))->p2p_enabled == true) {
			initialgain = 0x30;
		} else
			initialgain = 0x1E;

		rtw_hal_set_hwreg23a(padapter, HW_VAR_INITIAL_GAIN,
				  (u8 *)(&initialgain));

		/* set MSR to no link state */
		Set_MSR23a(padapter, _HW_STATE_NOLINK_);

		val8 = 1; /* under site survey */
		rtw_hal_set_hwreg23a(padapter, HW_VAR_MLME_SITESURVEY,
				  (u8 *)(&val8));

		pmlmeext->sitesurvey_res.state = SCAN_PROCESS;
	}

	site_survey23a(padapter);

	return H2C_SUCCESS;
}

u8 setauth_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf)
{
	struct setauth_parm		*pparm = (struct setauth_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	if (pparm->mode < 4)
	{
		pmlmeinfo->auth_algo = pparm->mode;
	}

	return	H2C_SUCCESS;
}

u8 setkey_hdl23a(struct rtw_adapter *padapter, u8 *pbuf)
{
	unsigned short				ctrl;
	struct setkey_parm		*pparm = (struct setkey_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	unsigned char					null_sta[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	/* main tx key for wep. */
	if (pparm->set_tx)
		pmlmeinfo->key_index = pparm->keyid;

	/* write cam */
	ctrl = BIT(15) | ((pparm->algorithm) << 2) | pparm->keyid;

	DBG_8723A_LEVEL(_drv_always_, "set group key to hw: alg:%d(WEP40-1 WEP104-5 TKIP-2 AES-4) "
			"keyid:%d\n", pparm->algorithm, pparm->keyid);
	write_cam23a(padapter, pparm->keyid, ctrl, null_sta, pparm->key);

	/* allow multicast packets to driver */
        padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_ON_RCR_AM, null_addr);

	return H2C_SUCCESS;
}

u8 set_stakey_hdl23a(struct rtw_adapter *padapter, u8 *pbuf)
{
	u16 ctrl = 0;
	u8 cam_id;/* cam_entry */
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct set_stakey_parm	*pparm = (struct set_stakey_parm *)pbuf;

	/* cam_entry: */
	/* 0~3 for default key */

	/* for concurrent mode (ap+sta): */
	/* default key is disable, using sw encrypt/decrypt */
	/* cam_entry = 4  for sta mode (macid = 0) */
	/* cam_entry(macid+3) = 5 ~ N for ap mode (aid = 1~N, macid = 2 ~N) */

	/* for concurrent mode (sta+sta): */
	/* default key is disable, using sw encrypt/decrypt */
	/* cam_entry = 4 mapping to macid = 0 */
	/* cam_entry = 5 mapping to macid = 2 */

	cam_id = 4;

	DBG_8723A_LEVEL(_drv_always_, "set pairwise key to hw: alg:%d(WEP40-1 WEP104-5 TKIP-2 AES-4) camid:%d\n",
			pparm->algorithm, cam_id);
	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	{

		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		if (pparm->algorithm == _NO_PRIVACY_)	/*  clear cam entry */
		{
			clear_cam_entry23a(padapter, pparm->id);
			return H2C_SUCCESS_RSP;
		}

		psta = rtw_get_stainfo23a(pstapriv, pparm->addr);
		if (psta)
		{
			ctrl = (BIT(15) | ((pparm->algorithm) << 2));

			DBG_8723A("r871x_set_stakey_hdl23a(): enc_algorithm =%d\n", pparm->algorithm);

			if ((psta->mac_id<1) || (psta->mac_id>(NUM_STA-4)))
			{
				DBG_8723A("r871x_set_stakey_hdl23a():set_stakey failed, mac_id(aid) =%d\n", psta->mac_id);
				return H2C_REJECTED;
			}

			cam_id = (psta->mac_id + 3);/* 0~3 for default key, cmd_id = macid + 3, macid = aid+1; */

			DBG_8723A("Write CAM, mac_addr =%x:%x:%x:%x:%x:%x, cam_entry =%d\n", pparm->addr[0],
						pparm->addr[1], pparm->addr[2], pparm->addr[3], pparm->addr[4],
						pparm->addr[5], cam_id);

			write_cam23a(padapter, cam_id, ctrl, pparm->addr, pparm->key);

			return H2C_SUCCESS_RSP;

		}
		else
		{
			DBG_8723A("r871x_set_stakey_hdl23a(): sta has been free\n");
			return H2C_REJECTED;
		}

	}

	/* below for sta mode */

	if (pparm->algorithm == _NO_PRIVACY_)	/*  clear cam entry */
	{
		clear_cam_entry23a(padapter, pparm->id);
		return H2C_SUCCESS;
	}

	ctrl = BIT(15) | ((pparm->algorithm) << 2);

	write_cam23a(padapter, cam_id, ctrl, pparm->addr, pparm->key);

	pmlmeinfo->enc_algo = pparm->algorithm;

	return H2C_SUCCESS;
}

u8 add_ba_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf)
{
	struct addBaReq_parm	*pparm = (struct addBaReq_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	struct sta_info *psta = rtw_get_stainfo23a(&padapter->stapriv, pparm->addr);

	if (!psta)
		return	H2C_SUCCESS;

	if (((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) &&
	     (pmlmeinfo->HT_enable)) ||
	    ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE)) {
		issue_action_BA23a(padapter, pparm->addr,
				WLAN_ACTION_ADDBA_REQ, (u16)pparm->tid);
		mod_timer(&psta->addba_retry_timer,
			  jiffies + msecs_to_jiffies(ADDBA_TO));
	} else {
		psta->htpriv.candidate_tid_bitmap &= ~CHKBIT(pparm->tid);
	}
	return	H2C_SUCCESS;
}

u8 set_tx_beacon_cmd23a(struct rtw_adapter* padapter)
{
	struct cmd_obj	*ph2c;
	struct Tx_Beacon_param	*ptxBeacon_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u8	res = _SUCCESS;
	int len_diff = 0;



	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	ptxBeacon_parm = (struct Tx_Beacon_param *)
		kzalloc(sizeof(struct Tx_Beacon_param), GFP_ATOMIC);
	if (!ptxBeacon_parm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	memcpy(&ptxBeacon_parm->network, &pmlmeinfo->network,
	       sizeof(struct wlan_bssid_ex));

	len_diff = update_hidden_ssid(
		ptxBeacon_parm->network.IEs+_BEACON_IE_OFFSET_,
		ptxBeacon_parm->network.IELength-_BEACON_IE_OFFSET_,
		pmlmeinfo->hidden_ssid_mode);
	ptxBeacon_parm->network.IELength += len_diff;

	init_h2fwcmd_w_parm_no_rsp(ph2c, ptxBeacon_parm, GEN_CMD_CODE(_TX_Beacon));

	res = rtw_enqueue_cmd23a(pcmdpriv, ph2c);

exit:



	return res;
}

u8 mlme_evt_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf)
{
	u8 evt_code, evt_seq;
	u16 evt_sz;
	uint	*peventbuf;
	void (*event_callback)(struct rtw_adapter *dev, u8 *pbuf);
	struct evt_priv *pevt_priv = &padapter->evtpriv;

	peventbuf = (uint*)pbuf;
	evt_sz = (u16)(*peventbuf&0xffff);
	evt_seq = (u8)((*peventbuf>>24)&0x7f);
	evt_code = (u8)((*peventbuf>>16)&0xff);

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
		event_callback(padapter, (u8*)peventbuf);

		pevt_priv->evt_done_cnt++;
	}

_abort_event_:

	return H2C_SUCCESS;
}

u8 h2c_msg_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf)
{
	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	return H2C_SUCCESS;
}

u8 tx_beacon_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf)
{
	if (send_beacon23a(padapter) == _FAIL)
	{
		DBG_8723A("issue_beacon23a, fail!\n");
		return H2C_PARAMETERS_ERROR;
	}
#ifdef CONFIG_8723AU_AP_MODE
	else /* tx bc/mc frames after update TIM */
	{
		struct sta_info *psta_bmc;
		struct list_head *plist, *phead, *ptmp;
		struct xmit_frame *pxmitframe;
		struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
		struct sta_priv  *pstapriv = &padapter->stapriv;

		/* for BC/MC Frames */
		psta_bmc = rtw_get_bcmc_stainfo23a(padapter);
		if (!psta_bmc)
			return H2C_SUCCESS;

		if ((pstapriv->tim_bitmap&BIT(0)) && (psta_bmc->sleepq_len>0))
		{
			msleep(10);/*  10ms, ATIM(HIQ) Windows */
			/* spin_lock_bh(&psta_bmc->sleep_q.lock); */
			spin_lock_bh(&pxmitpriv->lock);

			phead = get_list_head(&psta_bmc->sleep_q);

			list_for_each_safe(plist, ptmp, phead) {
				pxmitframe = container_of(plist,
							  struct xmit_frame,
							  list);

				list_del_init(&pxmitframe->list);

				psta_bmc->sleepq_len--;
				if (psta_bmc->sleepq_len>0)
					pxmitframe->attrib.mdata = 1;
				else
					pxmitframe->attrib.mdata = 0;

				pxmitframe->attrib.triggered = 1;

				pxmitframe->attrib.qsel = 0x11;/* HIQ */

				rtw_hal_xmit23aframe_enqueue(padapter, pxmitframe);
			}

			/* spin_unlock_bh(&psta_bmc->sleep_q.lock); */
			spin_unlock_bh(&pxmitpriv->lock);
		}

	}
#endif

	return H2C_SUCCESS;
}

u8 set_ch_hdl23a(struct rtw_adapter *padapter, u8 *pbuf)
{
	struct set_ch_parm *set_ch_parm;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	set_ch_parm = (struct set_ch_parm *)pbuf;

	DBG_8723A(FUNC_NDEV_FMT" ch:%u, bw:%u, ch_offset:%u\n",
		FUNC_NDEV_ARG(padapter->pnetdev),
		set_ch_parm->ch, set_ch_parm->bw, set_ch_parm->ch_offset);

	pmlmeext->cur_channel = set_ch_parm->ch;
	pmlmeext->cur_ch_offset = set_ch_parm->ch_offset;
	pmlmeext->cur_bwmode = set_ch_parm->bw;

	set_channel_bwmode23a(padapter, set_ch_parm->ch, set_ch_parm->ch_offset, set_ch_parm->bw);

	return	H2C_SUCCESS;
}

u8 set_chplan_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf)
{
	struct SetChannelPlan_param *setChannelPlan_param;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	setChannelPlan_param = (struct SetChannelPlan_param *)pbuf;

	pmlmeext->max_chan_nums = init_channel_set(padapter, setChannelPlan_param->channel_plan, pmlmeext->channel_set);
	init_channel_list(padapter, pmlmeext->channel_set, pmlmeext->max_chan_nums, &pmlmeext->channel_list);

	return	H2C_SUCCESS;
}

u8 led_blink_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf)
{
	struct LedBlink_param *ledBlink_param;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	ledBlink_param = (struct LedBlink_param *)pbuf;

	return	H2C_SUCCESS;
}

u8 set_csa_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf)
{
	return	H2C_REJECTED;
}

/*  TDLS_WRCR		: write RCR DATA BIT */
/*  TDLS_SD_PTI		: issue peer traffic indication */
/*  TDLS_CS_OFF		: go back to the channel linked with AP, terminating channel switch procedure */
/*  TDLS_INIT_CH_SEN	: init channel sensing, receive all data and mgnt frame */
/*  TDLS_DONE_CH_SEN: channel sensing and report candidate channel */
/*  TDLS_OFF_CH		: first time set channel to off channel */
/*  TDLS_BASE_CH		: go back tp the channel linked with AP when set base channel as target channel */
/*  TDLS_P_OFF_CH	: periodically go to off channel */
/*  TDLS_P_BASE_CH	: periodically go back to base channel */
/*  TDLS_RS_RCR		: restore RCR */
/*  TDLS_CKALV_PH1	: check alive timer phase1 */
/*  TDLS_CKALV_PH2	: check alive timer phase2 */
/*  TDLS_FREE_STA	: free tdls sta */
u8 tdls_hdl23a(struct rtw_adapter *padapter, unsigned char *pbuf)
{
	return H2C_REJECTED;
}
