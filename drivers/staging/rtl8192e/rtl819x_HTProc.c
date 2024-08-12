// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#include "rtllib.h"
#include "rtl819x_HT.h"
u8 MCS_FILTER_ALL[16] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

u8 MCS_FILTER_1SS[16] = {
	0xff, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
;

u16 MCS_DATA_RATE[2][2][77] = {
	{{13, 26, 39, 52, 78, 104, 117, 130, 26, 52, 78, 104, 156, 208, 234,
	 260, 39, 78, 117, 234, 312, 351, 390, 52, 104, 156, 208, 312, 416,
	 468, 520, 0, 78, 104, 130, 117, 156, 195, 104, 130, 130, 156, 182,
	 182, 208, 156, 195, 195, 234, 273, 273, 312, 130, 156, 181, 156,
	 181, 208, 234, 208, 234, 260, 260, 286, 195, 234, 273, 234, 273,
	 312, 351, 312, 351, 390, 390, 429},
	{14, 29, 43, 58, 87, 116, 130, 144, 29, 58, 87, 116, 173, 231, 260, 289,
	 43, 87, 130, 173, 260, 347, 390, 433, 58, 116, 173, 231, 347, 462, 520,
	 578, 0, 87, 116, 144, 130, 173, 217, 116, 144, 144, 173, 202, 202, 231,
	 173, 217, 217, 260, 303, 303, 347, 144, 173, 202, 173, 202, 231, 260,
	 231, 260, 289, 289, 318, 217, 260, 303, 260, 303, 347, 390, 347, 390,
	 433, 433, 477} },
	{{27, 54, 81, 108, 162, 216, 243, 270, 54, 108, 162, 216, 324, 432, 486,
	 540, 81, 162, 243, 324, 486, 648, 729, 810, 108, 216, 324, 432, 648,
	 864, 972, 1080, 12, 162, 216, 270, 243, 324, 405, 216, 270, 270, 324,
	 378, 378, 432, 324, 405, 405, 486, 567, 567, 648, 270, 324, 378, 324,
	 378, 432, 486, 432, 486, 540, 540, 594, 405, 486, 567, 486, 567, 648,
	 729, 648, 729, 810, 810, 891},
	{30, 60, 90, 120, 180, 240, 270, 300, 60, 120, 180, 240, 360, 480, 540,
	 600, 90, 180, 270, 360, 540, 720, 810, 900, 120, 240, 360, 480, 720,
	 960, 1080, 1200, 13, 180, 240, 300, 270, 360, 450, 240, 300, 300, 360,
	 420, 420, 480, 360, 450, 450, 540, 630, 630, 720, 300, 360, 420, 360,
	 420, 480, 540, 480, 540, 600, 600, 660, 450, 540, 630, 540, 630, 720,
	 810, 720, 810, 900, 900, 990} }
};

static u8 UNKNOWN_BORADCOM[3] = {0x00, 0x14, 0xbf};

static u8 LINKSYSWRT330_LINKSYSWRT300_BROADCOM[3] = {0x00, 0x1a, 0x70};

static u8 LINKSYSWRT350_LINKSYSWRT150_BROADCOM[3] = {0x00, 0x1d, 0x7e};

static u8 BELKINF5D8233V1_RALINK[3] = {0x00, 0x17, 0x3f};

static u8 BELKINF5D82334V3_RALINK[3] = {0x00, 0x1c, 0xdf};

static u8 PCI_RALINK[3] = {0x00, 0x90, 0xcc};

static u8 EDIMAX_RALINK[3] = {0x00, 0x0e, 0x2e};

static u8 AIRLINK_RALINK[3] = {0x00, 0x18, 0x02};

static u8 DLINK_ATHEROS_1[3] = {0x00, 0x1c, 0xf0};

static u8 DLINK_ATHEROS_2[3] = {0x00, 0x21, 0x91};

static u8 CISCO_BROADCOM[3] = {0x00, 0x17, 0x94};

static u8 LINKSYS_MARVELL_4400N[3] = {0x00, 0x14, 0xa4};

void ht_update_default_setting(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;

	ht_info->ampdu_enable = 1;
	ht_info->ampdu_factor = 2;

	ieee->tx_dis_rate_fallback = 0;
	ieee->tx_use_drv_assinged_rate = 0;

	ieee->tx_enable_fw_calc_dur = 1;

	ht_info->rx_reorder_win_size = 64;
	ht_info->rx_reorder_pending_time = 30;
}

static u16 ht_mcs_to_data_rate(struct rtllib_device *ieee, u8 mcs_rate)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;

	u8	is40MHz = (ht_info->cur_bw_40mhz) ? 1 : 0;
	u8	isShortGI = (ht_info->cur_bw_40mhz) ?
			    ((ht_info->cur_short_gi_40mhz) ? 1 : 0) :
			    ((ht_info->cur_short_gi_20mhz) ? 1 : 0);
	return MCS_DATA_RATE[is40MHz][isShortGI][(mcs_rate & 0x7f)];
}

u16  tx_count_to_data_rate(struct rtllib_device *ieee, u8 data_rate)
{
	u16	cck_of_dm_rate[12] = {0x02, 0x04, 0x0b, 0x16, 0x0c, 0x12, 0x18,
				   0x24, 0x30, 0x48, 0x60, 0x6c};
	u8	is40MHz = 0;
	u8	isShortGI = 0;

	if (data_rate < 12)
		return cck_of_dm_rate[data_rate];
	if (data_rate >= 0x10 && data_rate <= 0x1f) {
		is40MHz = 0;
		isShortGI = 0;
	} else if (data_rate >= 0x20  && data_rate <= 0x2f) {
		is40MHz = 1;
		isShortGI = 0;
	} else if (data_rate >= 0x30  && data_rate <= 0x3f) {
		is40MHz = 0;
		isShortGI = 1;
	} else if (data_rate >= 0x40  && data_rate <= 0x4f) {
		is40MHz = 1;
		isShortGI = 1;
	}
	return MCS_DATA_RATE[is40MHz][isShortGI][data_rate & 0xf];
}

bool is_ht_half_nmode_aps(struct rtllib_device *ieee)
{
	bool			retValue = false;
	struct rtllib_network *net = &ieee->current_network;

	if ((memcmp(net->bssid, BELKINF5D8233V1_RALINK, 3) == 0) ||
	    (memcmp(net->bssid, BELKINF5D82334V3_RALINK, 3) == 0) ||
	    (memcmp(net->bssid, PCI_RALINK, 3) == 0) ||
	    (memcmp(net->bssid, EDIMAX_RALINK, 3) == 0) ||
	    (memcmp(net->bssid, AIRLINK_RALINK, 3) == 0) ||
	    (net->ralink_cap_exist))
		retValue = true;
	else if (!memcmp(net->bssid, UNKNOWN_BORADCOM, 3) ||
		 !memcmp(net->bssid, LINKSYSWRT330_LINKSYSWRT300_BROADCOM, 3) ||
		 !memcmp(net->bssid, LINKSYSWRT350_LINKSYSWRT150_BROADCOM, 3) ||
		(net->broadcom_cap_exist))
		retValue = true;
	else if (net->bssht.bd_rt2rt_aggregation)
		retValue = true;
	else
		retValue = false;

	return retValue;
}

static void ht_iot_peer_determine(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;
	struct rtllib_network *net = &ieee->current_network;

	if (net->bssht.bd_rt2rt_aggregation) {
		ht_info->iot_peer = HT_IOT_PEER_REALTEK;
		if (net->bssht.rt2rt_ht_mode & RT_HT_CAP_USE_92SE)
			ht_info->iot_peer = HT_IOT_PEER_REALTEK_92SE;
		if (net->bssht.rt2rt_ht_mode & RT_HT_CAP_USE_SOFTAP)
			ht_info->iot_peer = HT_IOT_PEER_92U_SOFTAP;
	} else if (net->broadcom_cap_exist) {
		ht_info->iot_peer = HT_IOT_PEER_BROADCOM;
	} else if (!memcmp(net->bssid, UNKNOWN_BORADCOM, 3) ||
		 !memcmp(net->bssid, LINKSYSWRT330_LINKSYSWRT300_BROADCOM, 3) ||
		 !memcmp(net->bssid, LINKSYSWRT350_LINKSYSWRT150_BROADCOM, 3)) {
		ht_info->iot_peer = HT_IOT_PEER_BROADCOM;
	} else if ((memcmp(net->bssid, BELKINF5D8233V1_RALINK, 3) == 0) ||
		 (memcmp(net->bssid, BELKINF5D82334V3_RALINK, 3) == 0) ||
		 (memcmp(net->bssid, PCI_RALINK, 3) == 0) ||
		 (memcmp(net->bssid, EDIMAX_RALINK, 3) == 0) ||
		 (memcmp(net->bssid, AIRLINK_RALINK, 3) == 0) ||
		  net->ralink_cap_exist) {
		ht_info->iot_peer = HT_IOT_PEER_RALINK;
	} else if ((net->atheros_cap_exist) ||
		(memcmp(net->bssid, DLINK_ATHEROS_1, 3) == 0) ||
		(memcmp(net->bssid, DLINK_ATHEROS_2, 3) == 0)) {
		ht_info->iot_peer = HT_IOT_PEER_ATHEROS;
	} else if ((memcmp(net->bssid, CISCO_BROADCOM, 3) == 0) ||
		  net->cisco_cap_exist) {
		ht_info->iot_peer = HT_IOT_PEER_CISCO;
	} else if ((memcmp(net->bssid, LINKSYS_MARVELL_4400N, 3) == 0) ||
		  net->marvell_cap_exist) {
		ht_info->iot_peer = HT_IOT_PEER_MARVELL;
	} else if (net->airgo_cap_exist) {
		ht_info->iot_peer = HT_IOT_PEER_AIRGO;
	} else {
		ht_info->iot_peer = HT_IOT_PEER_UNKNOWN;
	}

	netdev_dbg(ieee->dev, "IOTPEER: %x\n", ht_info->iot_peer);
}

static u8 ht_iot_act_is_mgnt_use_cck_6m(struct rtllib_device *ieee,
				 struct rtllib_network *network)
{
	u8	retValue = 0;

	if (ieee->ht_info->iot_peer == HT_IOT_PEER_BROADCOM)
		retValue = 1;

	return retValue;
}

static u8 ht_iot_act_is_ccd_fsync(struct rtllib_device *ieee)
{
	u8	retValue = 0;

	if (ieee->ht_info->iot_peer == HT_IOT_PEER_BROADCOM)
		retValue = 1;
	return retValue;
}

static void ht_iot_act_determine_ra_func(struct rtllib_device *ieee, bool bPeerRx2ss)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;

	ht_info->iot_ra_func &= HT_IOT_RAFUNC_DISABLE_ALL;

	if (ht_info->iot_peer == HT_IOT_PEER_RALINK && !bPeerRx2ss)
		ht_info->iot_ra_func |= HT_IOT_RAFUNC_PEER_1R;

	if (ht_info->iot_action & HT_IOT_ACT_AMSDU_ENABLE)
		ht_info->iot_ra_func |= HT_IOT_RAFUNC_TX_AMSDU;
}

void ht_reset_iot_setting(struct rt_hi_throughput *ht_info)
{
	ht_info->iot_action = 0;
	ht_info->iot_peer = HT_IOT_PEER_UNKNOWN;
	ht_info->iot_ra_func = 0;
}

void ht_construct_capability_element(struct rtllib_device *ieee, u8 *pos_ht_cap,
				  u8 *len, u8 is_encrypt, bool assoc)
{
	struct rt_hi_throughput *ht = ieee->ht_info;
	struct ht_capab_ele *cap_ele = NULL;

	if (!pos_ht_cap || !ht) {
		netdev_warn(ieee->dev,
			    "%s(): pos_ht_cap and ht_info are null\n", __func__);
		return;
	}
	memset(pos_ht_cap, 0, *len);

	if ((assoc) && (ht->peer_ht_spec_ver == HT_SPEC_VER_EWC)) {
		static const u8	EWC11NHTCap[] = { 0x00, 0x90, 0x4c, 0x33 };

		memcpy(pos_ht_cap, EWC11NHTCap, sizeof(EWC11NHTCap));
		cap_ele = (struct ht_capab_ele *)&pos_ht_cap[4];
		*len = 30 + 2;
	} else {
		cap_ele = (struct ht_capab_ele *)pos_ht_cap;
		*len = 26 + 2;
	}

	cap_ele->adv_coding		= 0;
	if (ieee->get_half_nmode_support_by_aps_handler(ieee->dev))
		cap_ele->chl_width = 0;
	else
		cap_ele->chl_width = 1;

	cap_ele->mimo_pwr_save		= 3;
	cap_ele->green_field		= 0;
	cap_ele->short_gi_20mhz		= 1;
	cap_ele->short_gi_40mhz		= 1;

	cap_ele->tx_stbc			= 1;
	cap_ele->rx_stbc			= 0;
	cap_ele->delay_ba		= 0;
	cap_ele->max_amsdu_size = (MAX_RECEIVE_BUFFER_SIZE >= 7935) ? 1 : 0;
	cap_ele->dss_cck = 1;
	cap_ele->PSMP = 0;
	cap_ele->lsig_txop_protect = 0;

	netdev_dbg(ieee->dev,
		   "TX HT cap/info ele BW=%d max_amsdu_size:%d dss_cck:%d\n",
		   cap_ele->chl_width, cap_ele->max_amsdu_size, cap_ele->dss_cck);

	if (is_encrypt) {
		cap_ele->mpdu_density	= 7;
		cap_ele->max_rx_ampdu_factor	= 2;
	} else {
		cap_ele->max_rx_ampdu_factor	= 3;
		cap_ele->mpdu_density	= 0;
	}

	memcpy(cap_ele->MCS, ieee->reg_dot11ht_oper_rate_set, 16);
	memset(&cap_ele->ext_ht_cap_info, 0, 2);
	memset(cap_ele->TxBFCap, 0, 4);

	cap_ele->ASCap = 0;

	if (assoc) {
		if (ht->iot_action & HT_IOT_ACT_DISABLE_MCS15)
			cap_ele->MCS[1] &= 0x7f;

		if (ht->iot_action & HT_IOT_ACT_DISABLE_MCS14)
			cap_ele->MCS[1] &= 0xbf;

		if (ht->iot_action & HT_IOT_ACT_DISABLE_ALL_2SS)
			cap_ele->MCS[1] &= 0x00;

		if (ht->iot_action & HT_IOT_ACT_DISABLE_RX_40MHZ_SHORT_GI)
			cap_ele->short_gi_40mhz		= 0;

		if (ieee->get_half_nmode_support_by_aps_handler(ieee->dev)) {
			cap_ele->chl_width = 0;
			cap_ele->MCS[1] = 0;
		}
	}
}

void ht_construct_rt2rt_agg_element(struct rtllib_device *ieee, u8 *posRT2RTAgg,
				u8 *len)
{
	if (!posRT2RTAgg) {
		netdev_warn(ieee->dev, "%s(): posRT2RTAgg is null\n", __func__);
		return;
	}
	memset(posRT2RTAgg, 0, *len);
	*posRT2RTAgg++ = 0x00;
	*posRT2RTAgg++ = 0xe0;
	*posRT2RTAgg++ = 0x4c;
	*posRT2RTAgg++ = 0x02;
	*posRT2RTAgg++ = 0x01;

	*posRT2RTAgg = 0x30;

	if (ieee->bSupportRemoteWakeUp)
		*posRT2RTAgg |= RT_HT_CAP_USE_WOW;

	*len = 6 + 2;
}

static u8 ht_pick_mcs_rate(struct rtllib_device *ieee, u8 *pOperateMCS)
{
	u8 i;

	if (!pOperateMCS) {
		netdev_warn(ieee->dev, "%s(): pOperateMCS is null\n", __func__);
		return false;
	}

	switch (ieee->mode) {
	case WIRELESS_MODE_B:
	case WIRELESS_MODE_G:
		for (i = 0; i <= 15; i++)
			pOperateMCS[i] = 0;
		break;
	case WIRELESS_MODE_N_24G:
		pOperateMCS[0] &= RATE_ADPT_1SS_MASK;
		pOperateMCS[1] &= RATE_ADPT_2SS_MASK;
		pOperateMCS[3] &= RATE_ADPT_MCS32_MASK;
		break;
	default:
		break;
	}

	return true;
}

u8 ht_get_highest_mcs_rate(struct rtllib_device *ieee, u8 *pMCSRateSet,
		       u8 *pMCSFilter)
{
	u8		i, j;
	u8		bitMap;
	u8		mcsRate = 0;
	u8		availableMcsRate[16];

	if (!pMCSRateSet || !pMCSFilter) {
		netdev_warn(ieee->dev,
			    "%s(): pMCSRateSet and pMCSFilter are null\n",
			    __func__);
		return false;
	}
	for (i = 0; i < 16; i++)
		availableMcsRate[i] = pMCSRateSet[i] & pMCSFilter[i];

	for (i = 0; i < 16; i++) {
		if (availableMcsRate[i] != 0)
			break;
	}
	if (i == 16)
		return false;

	for (i = 0; i < 16; i++) {
		if (availableMcsRate[i] != 0) {
			bitMap = availableMcsRate[i];
			for (j = 0; j < 8; j++) {
				if ((bitMap % 2) != 0) {
					if (ht_mcs_to_data_rate(ieee, (8 * i + j)) >
					    ht_mcs_to_data_rate(ieee, mcsRate))
						mcsRate = 8 * i + j;
				}
				bitMap >>= 1;
			}
		}
	}
	return mcsRate | 0x80;
}

static u8 ht_filter_mcs_rate(struct rtllib_device *ieee, u8 *pSupportMCS,
			  u8 *pOperateMCS)
{
	u8 i;

	for (i = 0; i <= 15; i++)
		pOperateMCS[i] = ieee->reg_dot11tx_ht_oper_rate_set[i] &
				 pSupportMCS[i];

	ht_pick_mcs_rate(ieee, pOperateMCS);

	if (ieee->get_half_nmode_support_by_aps_handler(ieee->dev))
		pOperateMCS[1] = 0;

	for (i = 2; i <= 15; i++)
		pOperateMCS[i] = 0;

	return true;
}

void ht_set_connect_bw_mode(struct rtllib_device *ieee,
			enum ht_channel_width bandwidth,
			enum ht_extchnl_offset Offset);

void ht_on_assoc_rsp(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;
	struct ht_capab_ele *pPeerHTCap = NULL;
	struct ht_info_ele *pPeerHTInfo = NULL;
	u8 *pMcsFilter = NULL;

	static const u8 EWC11NHTCap[] = { 0x00, 0x90, 0x4c, 0x33 };
	static const u8 EWC11NHTInfo[] = { 0x00, 0x90, 0x4c, 0x34 };

	if (!ht_info->current_ht_support) {
		netdev_warn(ieee->dev, "%s(): HT_DISABLE\n", __func__);
		return;
	}
	netdev_dbg(ieee->dev, "%s(): HT_ENABLE\n", __func__);

	if (!memcmp(ht_info->peer_ht_cap_buf, EWC11NHTCap, sizeof(EWC11NHTCap)))
		pPeerHTCap = (struct ht_capab_ele *)(&ht_info->peer_ht_cap_buf[4]);
	else
		pPeerHTCap = (struct ht_capab_ele *)(ht_info->peer_ht_cap_buf);

	if (!memcmp(ht_info->peer_ht_info_buf, EWC11NHTInfo, sizeof(EWC11NHTInfo)))
		pPeerHTInfo = (struct ht_info_ele *)
			     (&ht_info->peer_ht_info_buf[4]);
	else
		pPeerHTInfo = (struct ht_info_ele *)(ht_info->peer_ht_info_buf);

#ifdef VERBOSE_DEBUG
	print_hex_dump_bytes("%s: ", __func__, DUMP_PREFIX_NONE,
			     pPeerHTCap, sizeof(struct ht_capab_ele));
#endif
	ht_set_connect_bw_mode(ieee, (enum ht_channel_width)(pPeerHTCap->chl_width),
			   (enum ht_extchnl_offset)(pPeerHTInfo->ExtChlOffset));
	ht_info->cur_tx_bw40mhz = ((pPeerHTInfo->RecommemdedTxWidth == 1) ?
				 true : false);

	ht_info->cur_short_gi_20mhz = ((pPeerHTCap->short_gi_20mhz == 1) ? true : false);
	ht_info->cur_short_gi_40mhz = ((pPeerHTCap->short_gi_40mhz == 1) ? true : false);

	ht_info->current_ampdu_enable = ht_info->ampdu_enable;
	if (ieee->rtllib_ap_sec_type &&
	    (ieee->rtllib_ap_sec_type(ieee) & (SEC_ALG_WEP | SEC_ALG_TKIP))) {
		if ((ht_info->iot_peer == HT_IOT_PEER_ATHEROS) ||
		    (ht_info->iot_peer == HT_IOT_PEER_UNKNOWN))
			ht_info->current_ampdu_enable = false;
	}

	if (ieee->current_network.bssht.bd_rt2rt_aggregation) {
		if (ieee->pairwise_key_type != KEY_TYPE_NA)
			ht_info->current_ampdu_factor =
					 pPeerHTCap->max_rx_ampdu_factor;
		else
			ht_info->current_ampdu_factor = HT_AGG_SIZE_64K;
	} else {
		ht_info->current_ampdu_factor = min_t(u32, pPeerHTCap->max_rx_ampdu_factor,
						      HT_AGG_SIZE_32K);
	}

	ht_info->current_mpdu_density = pPeerHTCap->mpdu_density;
	if (ht_info->iot_action & HT_IOT_ACT_TX_USE_AMSDU_8K)
		ht_info->current_ampdu_enable = false;

	ht_info->cur_rx_reorder_enable = 1;

	if (pPeerHTCap->MCS[0] == 0)
		pPeerHTCap->MCS[0] = 0xff;

	ht_iot_act_determine_ra_func(ieee, ((pPeerHTCap->MCS[1]) != 0));

	ht_filter_mcs_rate(ieee, pPeerHTCap->MCS, ieee->dot11ht_oper_rate_set);

	pMcsFilter = MCS_FILTER_ALL;
	ieee->HTHighestOperaRate = ht_get_highest_mcs_rate(ieee,
						       ieee->dot11ht_oper_rate_set,
						       pMcsFilter);
	ieee->ht_curr_op_rate = ieee->HTHighestOperaRate;

	ht_info->current_op_mode = pPeerHTInfo->opt_mode;
}

void ht_initialize_ht_info(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;

	ht_info->current_ht_support = false;

	ht_info->cur_bw_40mhz = false;
	ht_info->cur_tx_bw40mhz = false;

	ht_info->cur_short_gi_20mhz = false;
	ht_info->cur_short_gi_40mhz = false;

	ht_info->current_mpdu_density = 0;
	ht_info->current_ampdu_factor = ht_info->ampdu_factor;

	memset((void *)(&ht_info->self_ht_cap), 0,
	       sizeof(ht_info->self_ht_cap));
	memset((void *)(&ht_info->peer_ht_cap_buf), 0,
	       sizeof(ht_info->peer_ht_cap_buf));
	memset((void *)(&ht_info->peer_ht_info_buf), 0,
	       sizeof(ht_info->peer_ht_info_buf));

	ht_info->sw_bw_in_progress = false;

	ht_info->peer_ht_spec_ver = HT_SPEC_VER_IEEE;

	ht_info->current_rt2rt_aggregation = false;
	ht_info->current_rt2rt_long_slot_time = false;

	ht_info->iot_peer = 0;
	ht_info->iot_action = 0;
	ht_info->iot_ra_func = 0;

	{
		u8 *RegHTSuppRateSets = &ieee->reg_ht_supp_rate_set[0];

		RegHTSuppRateSets[0] = 0xFF;
		RegHTSuppRateSets[1] = 0xFF;
		RegHTSuppRateSets[4] = 0x01;
	}
}

void ht_initialize_bss_desc(struct bss_ht *bss_ht)
{
	bss_ht->bd_support_ht = false;
	memset(bss_ht->bd_ht_cap_buf, 0, sizeof(bss_ht->bd_ht_cap_buf));
	bss_ht->bd_ht_cap_len = 0;
	memset(bss_ht->bd_ht_info_buf, 0, sizeof(bss_ht->bd_ht_info_buf));
	bss_ht->bd_ht_info_len = 0;

	bss_ht->bd_ht_spec_ver = HT_SPEC_VER_IEEE;

	bss_ht->bd_rt2rt_aggregation = false;
	bss_ht->bd_rt2rt_long_slot_time = false;
	bss_ht->rt2rt_ht_mode = (enum rt_ht_capability)0;
}

void ht_reset_self_and_save_peer_setting(struct rtllib_device *ieee,
				   struct rtllib_network *pNetwork)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;
	u8	bIOTAction = 0;

	/* unmark enable_ht flag here is the same reason why unmarked in
	 * function rtllib_softmac_new_net. WB 2008.09.10
	 */
	if (pNetwork->bssht.bd_support_ht) {
		ht_info->current_ht_support = true;
		ht_info->peer_ht_spec_ver = pNetwork->bssht.bd_ht_spec_ver;

		if (pNetwork->bssht.bd_ht_cap_len > 0 &&
		    pNetwork->bssht.bd_ht_cap_len <= sizeof(ht_info->peer_ht_cap_buf))
			memcpy(ht_info->peer_ht_cap_buf,
			       pNetwork->bssht.bd_ht_cap_buf,
			       pNetwork->bssht.bd_ht_cap_len);

		if (pNetwork->bssht.bd_ht_info_len > 0 &&
		    pNetwork->bssht.bd_ht_info_len <=
		    sizeof(ht_info->peer_ht_info_buf))
			memcpy(ht_info->peer_ht_info_buf,
			       pNetwork->bssht.bd_ht_info_buf,
			       pNetwork->bssht.bd_ht_info_len);

		ht_info->current_rt2rt_aggregation =
			 pNetwork->bssht.bd_rt2rt_aggregation;
		ht_info->current_rt2rt_long_slot_time =
			 pNetwork->bssht.bd_rt2rt_long_slot_time;

		ht_iot_peer_determine(ieee);

		ht_info->iot_action = 0;
		bIOTAction = ht_iot_act_is_mgnt_use_cck_6m(ieee, pNetwork);
		if (bIOTAction)
			ht_info->iot_action |= HT_IOT_ACT_MGNT_USE_CCK_6M;
		bIOTAction = ht_iot_act_is_ccd_fsync(ieee);
		if (bIOTAction)
			ht_info->iot_action |= HT_IOT_ACT_CDD_FSYNC;
	} else {
		ht_info->current_ht_support = false;
		ht_info->current_rt2rt_aggregation = false;
		ht_info->current_rt2rt_long_slot_time = false;

		ht_info->iot_action = 0;
		ht_info->iot_ra_func = 0;
	}
}

void HT_update_self_and_peer_setting(struct rtllib_device *ieee,
				     struct rtllib_network *pNetwork)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;
	struct ht_info_ele *pPeerHTInfo =
		 (struct ht_info_ele *)pNetwork->bssht.bd_ht_info_buf;

	if (ht_info->current_ht_support) {
		if (pNetwork->bssht.bd_ht_info_len != 0)
			ht_info->current_op_mode = pPeerHTInfo->opt_mode;
	}
}
EXPORT_SYMBOL(HT_update_self_and_peer_setting);

u8 ht_c_check(struct rtllib_device *ieee, u8 *pFrame)
{
	if (ieee->ht_info->current_ht_support) {
		if ((is_qos_data_frame(pFrame) && frame_order(pFrame)) == 1) {
			netdev_dbg(ieee->dev, "HT CONTROL FILED EXIST!!\n");
			return true;
		}
	}
	return false;
}

static void ht_set_connect_bw_mode_callback(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;

	if (ht_info->cur_bw_40mhz) {
		if (ht_info->cur_sta_ext_chnl_offset == HT_EXTCHNL_OFFSET_UPPER)
			ieee->set_chan(ieee->dev,
				       ieee->current_network.channel + 2);
		else if (ht_info->cur_sta_ext_chnl_offset ==
			 HT_EXTCHNL_OFFSET_LOWER)
			ieee->set_chan(ieee->dev,
				       ieee->current_network.channel - 2);
		else
			ieee->set_chan(ieee->dev,
				       ieee->current_network.channel);

		ieee->set_bw_mode_handler(ieee->dev, HT_CHANNEL_WIDTH_20_40,
				       ht_info->cur_sta_ext_chnl_offset);
	} else {
		ieee->set_chan(ieee->dev, ieee->current_network.channel);
		ieee->set_bw_mode_handler(ieee->dev, HT_CHANNEL_WIDTH_20,
				       HT_EXTCHNL_OFFSET_NO_EXT);
	}

	ht_info->sw_bw_in_progress = false;
}

void ht_set_connect_bw_mode(struct rtllib_device *ieee,
			enum ht_channel_width bandwidth,
			enum ht_extchnl_offset Offset)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;

	if (ieee->get_half_nmode_support_by_aps_handler(ieee->dev))
		bandwidth = HT_CHANNEL_WIDTH_20;

	if (ht_info->sw_bw_in_progress) {
		pr_info("%s: sw_bw_in_progress!!\n", __func__);
		return;
	}
	if (bandwidth == HT_CHANNEL_WIDTH_20_40) {
		if (ieee->current_network.channel < 2 &&
		    Offset == HT_EXTCHNL_OFFSET_LOWER)
			Offset = HT_EXTCHNL_OFFSET_NO_EXT;
		if (Offset == HT_EXTCHNL_OFFSET_UPPER ||
		    Offset == HT_EXTCHNL_OFFSET_LOWER) {
			ht_info->cur_bw_40mhz = true;
			ht_info->cur_sta_ext_chnl_offset = Offset;
		} else {
			ht_info->cur_bw_40mhz = false;
			ht_info->cur_sta_ext_chnl_offset = HT_EXTCHNL_OFFSET_NO_EXT;
		}
	} else {
		ht_info->cur_bw_40mhz = false;
		ht_info->cur_sta_ext_chnl_offset = HT_EXTCHNL_OFFSET_NO_EXT;
	}

	netdev_dbg(ieee->dev, "%s():ht_info->bCurBW40MHz:%x\n", __func__,
		   ht_info->cur_bw_40mhz);

	ht_info->sw_bw_in_progress = true;

	ht_set_connect_bw_mode_callback(ieee);
}
