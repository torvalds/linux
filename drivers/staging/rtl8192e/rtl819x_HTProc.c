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

void HTUpdateDefaultSetting(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;

	ht_info->bRegShortGI20MHz = 1;
	ht_info->bRegShortGI40MHz = 1;

	ht_info->bRegBW40MHz = 1;

	if (ht_info->bRegBW40MHz)
		ht_info->bRegSuppCCK = 1;
	else
		ht_info->bRegSuppCCK = true;

	ht_info->nAMSDU_MaxSize = 7935UL;
	ht_info->bAMSDU_Support = 0;

	ht_info->bAMPDUEnable = 1;
	ht_info->AMPDU_Factor = 2;
	ht_info->MPDU_Density = 0;

	ht_info->self_mimo_ps = 3;
	if (ht_info->self_mimo_ps == 2)
		ht_info->self_mimo_ps = 3;
	ieee->tx_dis_rate_fallback = 0;
	ieee->tx_use_drv_assinged_rate = 0;

	ieee->bTxEnableFwCalcDur = 1;

	ht_info->reg_rt2rt_aggregation = 1;

	ht_info->reg_rx_reorder_enable = 1;
	ht_info->rx_reorder_win_size = 64;
	ht_info->rx_reorder_pending_time = 30;
}

static u16 HTMcsToDataRate(struct rtllib_device *ieee, u8 nMcsRate)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;

	u8	is40MHz = (ht_info->bCurBW40MHz) ? 1 : 0;
	u8	isShortGI = (ht_info->bCurBW40MHz) ?
			    ((ht_info->bCurShortGI40MHz) ? 1 : 0) :
			    ((ht_info->bCurShortGI20MHz) ? 1 : 0);
	return MCS_DATA_RATE[is40MHz][isShortGI][(nMcsRate & 0x7f)];
}

u16  TxCountToDataRate(struct rtllib_device *ieee, u8 nDataRate)
{
	u16	CCKOFDMRate[12] = {0x02, 0x04, 0x0b, 0x16, 0x0c, 0x12, 0x18,
				   0x24, 0x30, 0x48, 0x60, 0x6c};
	u8	is40MHz = 0;
	u8	isShortGI = 0;

	if (nDataRate < 12)
		return CCKOFDMRate[nDataRate];
	if (nDataRate >= 0x10 && nDataRate <= 0x1f) {
		is40MHz = 0;
		isShortGI = 0;
	} else if (nDataRate >= 0x20  && nDataRate <= 0x2f) {
		is40MHz = 1;
		isShortGI = 0;
	} else if (nDataRate >= 0x30  && nDataRate <= 0x3f) {
		is40MHz = 0;
		isShortGI = 1;
	} else if (nDataRate >= 0x40  && nDataRate <= 0x4f) {
		is40MHz = 1;
		isShortGI = 1;
	}
	return MCS_DATA_RATE[is40MHz][isShortGI][nDataRate & 0xf];
}

bool IsHTHalfNmodeAPs(struct rtllib_device *ieee)
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

static void HTIOTPeerDetermine(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;
	struct rtllib_network *net = &ieee->current_network;

	if (net->bssht.bd_rt2rt_aggregation) {
		ht_info->IOTPeer = HT_IOT_PEER_REALTEK;
		if (net->bssht.rt2rt_ht_mode & RT_HT_CAP_USE_92SE)
			ht_info->IOTPeer = HT_IOT_PEER_REALTEK_92SE;
		if (net->bssht.rt2rt_ht_mode & RT_HT_CAP_USE_SOFTAP)
			ht_info->IOTPeer = HT_IOT_PEER_92U_SOFTAP;
	} else if (net->broadcom_cap_exist) {
		ht_info->IOTPeer = HT_IOT_PEER_BROADCOM;
	} else if (!memcmp(net->bssid, UNKNOWN_BORADCOM, 3) ||
		 !memcmp(net->bssid, LINKSYSWRT330_LINKSYSWRT300_BROADCOM, 3) ||
		 !memcmp(net->bssid, LINKSYSWRT350_LINKSYSWRT150_BROADCOM, 3)) {
		ht_info->IOTPeer = HT_IOT_PEER_BROADCOM;
	} else if ((memcmp(net->bssid, BELKINF5D8233V1_RALINK, 3) == 0) ||
		 (memcmp(net->bssid, BELKINF5D82334V3_RALINK, 3) == 0) ||
		 (memcmp(net->bssid, PCI_RALINK, 3) == 0) ||
		 (memcmp(net->bssid, EDIMAX_RALINK, 3) == 0) ||
		 (memcmp(net->bssid, AIRLINK_RALINK, 3) == 0) ||
		  net->ralink_cap_exist) {
		ht_info->IOTPeer = HT_IOT_PEER_RALINK;
	} else if ((net->atheros_cap_exist) ||
		(memcmp(net->bssid, DLINK_ATHEROS_1, 3) == 0) ||
		(memcmp(net->bssid, DLINK_ATHEROS_2, 3) == 0)) {
		ht_info->IOTPeer = HT_IOT_PEER_ATHEROS;
	} else if ((memcmp(net->bssid, CISCO_BROADCOM, 3) == 0) ||
		  net->cisco_cap_exist) {
		ht_info->IOTPeer = HT_IOT_PEER_CISCO;
	} else if ((memcmp(net->bssid, LINKSYS_MARVELL_4400N, 3) == 0) ||
		  net->marvell_cap_exist) {
		ht_info->IOTPeer = HT_IOT_PEER_MARVELL;
	} else if (net->airgo_cap_exist) {
		ht_info->IOTPeer = HT_IOT_PEER_AIRGO;
	} else {
		ht_info->IOTPeer = HT_IOT_PEER_UNKNOWN;
	}

	netdev_dbg(ieee->dev, "IOTPEER: %x\n", ht_info->IOTPeer);
}

static u8 HTIOTActIsDisableMCS14(struct rtllib_device *ieee, u8 *PeerMacAddr)
{
	return 0;
}

static bool HTIOTActIsDisableMCS15(struct rtllib_device *ieee)
{
	return false;
}

static bool HTIOTActIsDisableMCSTwoSpatialStream(struct rtllib_device *ieee)
{
	return false;
}

static u8 HTIOTActIsDisableEDCATurbo(struct rtllib_device *ieee,
				     u8 *PeerMacAddr)
{
	return false;
}

static u8 HTIOTActIsMgntUseCCK6M(struct rtllib_device *ieee,
				 struct rtllib_network *network)
{
	u8	retValue = 0;

	if (ieee->ht_info->IOTPeer == HT_IOT_PEER_BROADCOM)
		retValue = 1;

	return retValue;
}

static u8 HTIOTActIsCCDFsync(struct rtllib_device *ieee)
{
	u8	retValue = 0;

	if (ieee->ht_info->IOTPeer == HT_IOT_PEER_BROADCOM)
		retValue = 1;
	return retValue;
}

static void HTIOTActDetermineRaFunc(struct rtllib_device *ieee, bool bPeerRx2ss)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;

	ht_info->iot_ra_func &= HT_IOT_RAFUNC_DISABLE_ALL;

	if (ht_info->IOTPeer == HT_IOT_PEER_RALINK && !bPeerRx2ss)
		ht_info->iot_ra_func |= HT_IOT_RAFUNC_PEER_1R;

	if (ht_info->iot_action & HT_IOT_ACT_AMSDU_ENABLE)
		ht_info->iot_ra_func |= HT_IOT_RAFUNC_TX_AMSDU;
}

void HTResetIOTSetting(struct rt_hi_throughput *ht_info)
{
	ht_info->iot_action = 0;
	ht_info->IOTPeer = HT_IOT_PEER_UNKNOWN;
	ht_info->iot_ra_func = 0;
}

void HTConstructCapabilityElement(struct rtllib_device *ieee, u8 *posHTCap,
				  u8 *len, u8 IsEncrypt, bool bAssoc)
{
	struct rt_hi_throughput *pHT = ieee->ht_info;
	struct ht_capab_ele *pCapELE = NULL;

	if (!posHTCap || !pHT) {
		netdev_warn(ieee->dev,
			    "%s(): posHTCap and ht_info are null\n", __func__);
		return;
	}
	memset(posHTCap, 0, *len);

	if ((bAssoc) && (pHT->ePeerHTSpecVer == HT_SPEC_VER_EWC)) {
		static const u8	EWC11NHTCap[] = { 0x00, 0x90, 0x4c, 0x33 };

		memcpy(posHTCap, EWC11NHTCap, sizeof(EWC11NHTCap));
		pCapELE = (struct ht_capab_ele *)&posHTCap[4];
		*len = 30 + 2;
	} else {
		pCapELE = (struct ht_capab_ele *)posHTCap;
		*len = 26 + 2;
	}

	pCapELE->AdvCoding		= 0;
	if (ieee->GetHalfNmodeSupportByAPsHandler(ieee->dev))
		pCapELE->ChlWidth = 0;
	else
		pCapELE->ChlWidth = (pHT->bRegBW40MHz ? 1 : 0);

	pCapELE->MimoPwrSave		= pHT->self_mimo_ps;
	pCapELE->GreenField		= 0;
	pCapELE->ShortGI20Mhz		= 1;
	pCapELE->ShortGI40Mhz		= 1;

	pCapELE->TxSTBC			= 1;
	pCapELE->RxSTBC			= 0;
	pCapELE->DelayBA		= 0;
	pCapELE->MaxAMSDUSize = (MAX_RECEIVE_BUFFER_SIZE >= 7935) ? 1 : 0;
	pCapELE->DssCCk = ((pHT->bRegBW40MHz) ? (pHT->bRegSuppCCK ? 1 : 0) : 0);
	pCapELE->PSMP = 0;
	pCapELE->LSigTxopProtect = 0;

	netdev_dbg(ieee->dev,
		   "TX HT cap/info ele BW=%d MaxAMSDUSize:%d DssCCk:%d\n",
		   pCapELE->ChlWidth, pCapELE->MaxAMSDUSize, pCapELE->DssCCk);

	if (IsEncrypt) {
		pCapELE->MPDUDensity	= 7;
		pCapELE->MaxRxAMPDUFactor	= 2;
	} else {
		pCapELE->MaxRxAMPDUFactor	= 3;
		pCapELE->MPDUDensity	= 0;
	}

	memcpy(pCapELE->MCS, ieee->reg_dot11ht_oper_rate_set, 16);
	memset(&pCapELE->ExtHTCapInfo, 0, 2);
	memset(pCapELE->TxBFCap, 0, 4);

	pCapELE->ASCap = 0;

	if (bAssoc) {
		if (pHT->iot_action & HT_IOT_ACT_DISABLE_MCS15)
			pCapELE->MCS[1] &= 0x7f;

		if (pHT->iot_action & HT_IOT_ACT_DISABLE_MCS14)
			pCapELE->MCS[1] &= 0xbf;

		if (pHT->iot_action & HT_IOT_ACT_DISABLE_ALL_2SS)
			pCapELE->MCS[1] &= 0x00;

		if (pHT->iot_action & HT_IOT_ACT_DISABLE_RX_40MHZ_SHORT_GI)
			pCapELE->ShortGI40Mhz		= 0;

		if (ieee->GetHalfNmodeSupportByAPsHandler(ieee->dev)) {
			pCapELE->ChlWidth = 0;
			pCapELE->MCS[1] = 0;
		}
	}
}

void HTConstructInfoElement(struct rtllib_device *ieee, u8 *posHTInfo,
			    u8 *len, u8 IsEncrypt)
{
	struct rt_hi_throughput *pHT = ieee->ht_info;
	struct ht_info_ele *pHTInfoEle = (struct ht_info_ele *)posHTInfo;

	if (!posHTInfo || !pHTInfoEle) {
		netdev_warn(ieee->dev,
			    "%s(): posHTInfo and pHTInfoEle are null\n",
			    __func__);
		return;
	}

	memset(posHTInfo, 0, *len);
	if ((ieee->iw_mode == IW_MODE_ADHOC) ||
	    (ieee->iw_mode == IW_MODE_MASTER)) {
		pHTInfoEle->ControlChl	= ieee->current_network.channel;
		pHTInfoEle->ExtChlOffset = ((!pHT->bRegBW40MHz) ?
					    HT_EXTCHNL_OFFSET_NO_EXT :
					    (ieee->current_network.channel <= 6)
					    ? HT_EXTCHNL_OFFSET_UPPER :
					    HT_EXTCHNL_OFFSET_LOWER);
		pHTInfoEle->RecommemdedTxWidth	= pHT->bRegBW40MHz;
		pHTInfoEle->RIFS			= 0;
		pHTInfoEle->PSMPAccessOnly		= 0;
		pHTInfoEle->SrvIntGranularity		= 0;
		pHTInfoEle->OptMode			= pHT->current_op_mode;
		pHTInfoEle->NonGFDevPresent		= 0;
		pHTInfoEle->DualBeacon			= 0;
		pHTInfoEle->SecondaryBeacon		= 0;
		pHTInfoEle->LSigTxopProtectFull		= 0;
		pHTInfoEle->PcoActive			= 0;
		pHTInfoEle->PcoPhase			= 0;

		memset(pHTInfoEle->BasicMSC, 0, 16);

		*len = 22 + 2;

	} else {
		*len = 0;
	}
}

void HTConstructRT2RTAggElement(struct rtllib_device *ieee, u8 *posRT2RTAgg,
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

static u8 HT_PickMCSRate(struct rtllib_device *ieee, u8 *pOperateMCS)
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

u8 HTGetHighestMCSRate(struct rtllib_device *ieee, u8 *pMCSRateSet,
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
					if (HTMcsToDataRate(ieee, (8 * i + j)) >
					    HTMcsToDataRate(ieee, mcsRate))
						mcsRate = 8 * i + j;
				}
				bitMap >>= 1;
			}
		}
	}
	return mcsRate | 0x80;
}

static u8 HTFilterMCSRate(struct rtllib_device *ieee, u8 *pSupportMCS,
			  u8 *pOperateMCS)
{
	u8 i;

	for (i = 0; i <= 15; i++)
		pOperateMCS[i] = ieee->reg_dot11tx_ht_oper_rate_set[i] &
				 pSupportMCS[i];

	HT_PickMCSRate(ieee, pOperateMCS);

	if (ieee->GetHalfNmodeSupportByAPsHandler(ieee->dev))
		pOperateMCS[1] = 0;

	for (i = 2; i <= 15; i++)
		pOperateMCS[i] = 0;

	return true;
}

void HTSetConnectBwMode(struct rtllib_device *ieee,
			enum ht_channel_width bandwidth,
			enum ht_extchnl_offset Offset);

void HTOnAssocRsp(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;
	struct ht_capab_ele *pPeerHTCap = NULL;
	struct ht_info_ele *pPeerHTInfo = NULL;
	u16 nMaxAMSDUSize = 0;
	u8 *pMcsFilter = NULL;

	static const u8 EWC11NHTCap[] = { 0x00, 0x90, 0x4c, 0x33 };
	static const u8 EWC11NHTInfo[] = { 0x00, 0x90, 0x4c, 0x34 };

	if (!ht_info->bCurrentHTSupport) {
		netdev_warn(ieee->dev, "%s(): HT_DISABLE\n", __func__);
		return;
	}
	netdev_dbg(ieee->dev, "%s(): HT_ENABLE\n", __func__);

	if (!memcmp(ht_info->PeerHTCapBuf, EWC11NHTCap, sizeof(EWC11NHTCap)))
		pPeerHTCap = (struct ht_capab_ele *)(&ht_info->PeerHTCapBuf[4]);
	else
		pPeerHTCap = (struct ht_capab_ele *)(ht_info->PeerHTCapBuf);

	if (!memcmp(ht_info->PeerHTInfoBuf, EWC11NHTInfo, sizeof(EWC11NHTInfo)))
		pPeerHTInfo = (struct ht_info_ele *)
			     (&ht_info->PeerHTInfoBuf[4]);
	else
		pPeerHTInfo = (struct ht_info_ele *)(ht_info->PeerHTInfoBuf);

#ifdef VERBOSE_DEBUG
	print_hex_dump_bytes("%s: ", __func__, DUMP_PREFIX_NONE,
			     pPeerHTCap, sizeof(struct ht_capab_ele));
#endif
	HTSetConnectBwMode(ieee, (enum ht_channel_width)(pPeerHTCap->ChlWidth),
			   (enum ht_extchnl_offset)(pPeerHTInfo->ExtChlOffset));
	ht_info->cur_tx_bw40mhz = ((pPeerHTInfo->RecommemdedTxWidth == 1) ?
				 true : false);

	ht_info->bCurShortGI20MHz = ((ht_info->bRegShortGI20MHz) ?
				    ((pPeerHTCap->ShortGI20Mhz == 1) ?
				    true : false) : false);
	ht_info->bCurShortGI40MHz = ((ht_info->bRegShortGI40MHz) ?
				     ((pPeerHTCap->ShortGI40Mhz == 1) ?
				     true : false) : false);

	ht_info->bCurSuppCCK = ((ht_info->bRegSuppCCK) ?
			       ((pPeerHTCap->DssCCk == 1) ? true :
			       false) : false);

	ht_info->bCurrent_AMSDU_Support = ht_info->bAMSDU_Support;

	nMaxAMSDUSize = (pPeerHTCap->MaxAMSDUSize == 0) ? 3839 : 7935;

	if (ht_info->nAMSDU_MaxSize > nMaxAMSDUSize)
		ht_info->nCurrent_AMSDU_MaxSize = nMaxAMSDUSize;
	else
		ht_info->nCurrent_AMSDU_MaxSize = ht_info->nAMSDU_MaxSize;

	ht_info->bCurrentAMPDUEnable = ht_info->bAMPDUEnable;
	if (ieee->rtllib_ap_sec_type &&
	    (ieee->rtllib_ap_sec_type(ieee) & (SEC_ALG_WEP | SEC_ALG_TKIP))) {
		if ((ht_info->IOTPeer == HT_IOT_PEER_ATHEROS) ||
		    (ht_info->IOTPeer == HT_IOT_PEER_UNKNOWN))
			ht_info->bCurrentAMPDUEnable = false;
	}

	if (!ht_info->reg_rt2rt_aggregation) {
		if (ht_info->AMPDU_Factor > pPeerHTCap->MaxRxAMPDUFactor)
			ht_info->CurrentAMPDUFactor =
						 pPeerHTCap->MaxRxAMPDUFactor;
		else
			ht_info->CurrentAMPDUFactor = ht_info->AMPDU_Factor;

	} else {
		if (ieee->current_network.bssht.bd_rt2rt_aggregation) {
			if (ieee->pairwise_key_type != KEY_TYPE_NA)
				ht_info->CurrentAMPDUFactor =
						 pPeerHTCap->MaxRxAMPDUFactor;
			else
				ht_info->CurrentAMPDUFactor = HT_AGG_SIZE_64K;
		} else {
			ht_info->CurrentAMPDUFactor = min_t(u32, pPeerHTCap->MaxRxAMPDUFactor,
							    HT_AGG_SIZE_32K);
		}
	}
	ht_info->current_mpdu_density = max_t(u8, ht_info->MPDU_Density,
					      pPeerHTCap->MPDUDensity);
	if (ht_info->iot_action & HT_IOT_ACT_TX_USE_AMSDU_8K) {
		ht_info->bCurrentAMPDUEnable = false;
		ht_info->ForcedAMSDUMode = HT_AGG_FORCE_ENABLE;
	}
	ht_info->cur_rx_reorder_enable = ht_info->reg_rx_reorder_enable;

	if (pPeerHTCap->MCS[0] == 0)
		pPeerHTCap->MCS[0] = 0xff;

	HTIOTActDetermineRaFunc(ieee, ((pPeerHTCap->MCS[1]) != 0));

	HTFilterMCSRate(ieee, pPeerHTCap->MCS, ieee->dot11ht_oper_rate_set);

	ht_info->peer_mimo_ps = pPeerHTCap->MimoPwrSave;
	if (ht_info->peer_mimo_ps == MIMO_PS_STATIC)
		pMcsFilter = MCS_FILTER_1SS;
	else
		pMcsFilter = MCS_FILTER_ALL;
	ieee->HTHighestOperaRate = HTGetHighestMCSRate(ieee,
						       ieee->dot11ht_oper_rate_set,
						       pMcsFilter);
	ieee->HTCurrentOperaRate = ieee->HTHighestOperaRate;

	ht_info->current_op_mode = pPeerHTInfo->OptMode;
}

void HTInitializeHTInfo(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;

	ht_info->bCurrentHTSupport = false;

	ht_info->bCurBW40MHz = false;
	ht_info->cur_tx_bw40mhz = false;

	ht_info->bCurShortGI20MHz = false;
	ht_info->bCurShortGI40MHz = false;
	ht_info->forced_short_gi = false;

	ht_info->bCurSuppCCK = true;

	ht_info->bCurrent_AMSDU_Support = false;
	ht_info->nCurrent_AMSDU_MaxSize = ht_info->nAMSDU_MaxSize;
	ht_info->current_mpdu_density = ht_info->MPDU_Density;
	ht_info->CurrentAMPDUFactor = ht_info->AMPDU_Factor;

	memset((void *)(&ht_info->SelfHTCap), 0,
	       sizeof(ht_info->SelfHTCap));
	memset((void *)(&ht_info->SelfHTInfo), 0,
	       sizeof(ht_info->SelfHTInfo));
	memset((void *)(&ht_info->PeerHTCapBuf), 0,
	       sizeof(ht_info->PeerHTCapBuf));
	memset((void *)(&ht_info->PeerHTInfoBuf), 0,
	       sizeof(ht_info->PeerHTInfoBuf));

	ht_info->sw_bw_in_progress = false;

	ht_info->ePeerHTSpecVer = HT_SPEC_VER_IEEE;

	ht_info->current_rt2rt_aggregation = false;
	ht_info->current_rt2rt_long_slot_time = false;
	ht_info->RT2RT_HT_Mode = (enum rt_ht_capability)0;

	ht_info->IOTPeer = 0;
	ht_info->iot_action = 0;
	ht_info->iot_ra_func = 0;

	{
		u8 *RegHTSuppRateSets = &ieee->reg_ht_supp_rate_set[0];

		RegHTSuppRateSets[0] = 0xFF;
		RegHTSuppRateSets[1] = 0xFF;
		RegHTSuppRateSets[4] = 0x01;
	}
}

void HTInitializeBssDesc(struct bss_ht *pBssHT)
{
	pBssHT->bd_support_ht = false;
	memset(pBssHT->bd_ht_cap_buf, 0, sizeof(pBssHT->bd_ht_cap_buf));
	pBssHT->bd_ht_cap_len = 0;
	memset(pBssHT->bd_ht_info_buf, 0, sizeof(pBssHT->bd_ht_info_buf));
	pBssHT->bd_ht_info_len = 0;

	pBssHT->bd_ht_spec_ver = HT_SPEC_VER_IEEE;

	pBssHT->bd_rt2rt_aggregation = false;
	pBssHT->bd_rt2rt_long_slot_time = false;
	pBssHT->rt2rt_ht_mode = (enum rt_ht_capability)0;
}

void HTResetSelfAndSavePeerSetting(struct rtllib_device *ieee,
				   struct rtllib_network *pNetwork)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;
	u8	bIOTAction = 0;

	/* unmark enable_ht flag here is the same reason why unmarked in
	 * function rtllib_softmac_new_net. WB 2008.09.10
	 */
	if (pNetwork->bssht.bd_support_ht) {
		ht_info->bCurrentHTSupport = true;
		ht_info->ePeerHTSpecVer = pNetwork->bssht.bd_ht_spec_ver;

		if (pNetwork->bssht.bd_ht_cap_len > 0 &&
		    pNetwork->bssht.bd_ht_cap_len <= sizeof(ht_info->PeerHTCapBuf))
			memcpy(ht_info->PeerHTCapBuf,
			       pNetwork->bssht.bd_ht_cap_buf,
			       pNetwork->bssht.bd_ht_cap_len);

		if (pNetwork->bssht.bd_ht_info_len > 0 &&
		    pNetwork->bssht.bd_ht_info_len <=
		    sizeof(ht_info->PeerHTInfoBuf))
			memcpy(ht_info->PeerHTInfoBuf,
			       pNetwork->bssht.bd_ht_info_buf,
			       pNetwork->bssht.bd_ht_info_len);

		if (ht_info->reg_rt2rt_aggregation) {
			ht_info->current_rt2rt_aggregation =
				 pNetwork->bssht.bd_rt2rt_aggregation;
			ht_info->current_rt2rt_long_slot_time =
				 pNetwork->bssht.bd_rt2rt_long_slot_time;
			ht_info->RT2RT_HT_Mode = pNetwork->bssht.rt2rt_ht_mode;
		} else {
			ht_info->current_rt2rt_aggregation = false;
			ht_info->current_rt2rt_long_slot_time = false;
			ht_info->RT2RT_HT_Mode = (enum rt_ht_capability)0;
		}

		HTIOTPeerDetermine(ieee);

		ht_info->iot_action = 0;
		bIOTAction = HTIOTActIsDisableMCS14(ieee, pNetwork->bssid);
		if (bIOTAction)
			ht_info->iot_action |= HT_IOT_ACT_DISABLE_MCS14;

		bIOTAction = HTIOTActIsDisableMCS15(ieee);
		if (bIOTAction)
			ht_info->iot_action |= HT_IOT_ACT_DISABLE_MCS15;

		bIOTAction = HTIOTActIsDisableMCSTwoSpatialStream(ieee);
		if (bIOTAction)
			ht_info->iot_action |= HT_IOT_ACT_DISABLE_ALL_2SS;

		bIOTAction = HTIOTActIsDisableEDCATurbo(ieee, pNetwork->bssid);
		if (bIOTAction)
			ht_info->iot_action |= HT_IOT_ACT_DISABLE_EDCA_TURBO;

		bIOTAction = HTIOTActIsMgntUseCCK6M(ieee, pNetwork);
		if (bIOTAction)
			ht_info->iot_action |= HT_IOT_ACT_MGNT_USE_CCK_6M;
		bIOTAction = HTIOTActIsCCDFsync(ieee);
		if (bIOTAction)
			ht_info->iot_action |= HT_IOT_ACT_CDD_FSYNC;
	} else {
		ht_info->bCurrentHTSupport = false;
		ht_info->current_rt2rt_aggregation = false;
		ht_info->current_rt2rt_long_slot_time = false;
		ht_info->RT2RT_HT_Mode = (enum rt_ht_capability)0;

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

	if (ht_info->bCurrentHTSupport) {
		if (pNetwork->bssht.bd_ht_info_len != 0)
			ht_info->current_op_mode = pPeerHTInfo->OptMode;
	}
}
EXPORT_SYMBOL(HT_update_self_and_peer_setting);

void HTUseDefaultSetting(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;

	if (ht_info->enable_ht) {
		ht_info->bCurrentHTSupport = true;
		ht_info->bCurSuppCCK = ht_info->bRegSuppCCK;

		ht_info->bCurBW40MHz = ht_info->bRegBW40MHz;
		ht_info->bCurShortGI20MHz = ht_info->bRegShortGI20MHz;

		ht_info->bCurShortGI40MHz = ht_info->bRegShortGI40MHz;

		if (ieee->iw_mode == IW_MODE_ADHOC)
			ieee->current_network.qos_data.active =
				 ieee->current_network.qos_data.supported;
		ht_info->bCurrent_AMSDU_Support = ht_info->bAMSDU_Support;
		ht_info->nCurrent_AMSDU_MaxSize = ht_info->nAMSDU_MaxSize;

		ht_info->bCurrentAMPDUEnable = ht_info->bAMPDUEnable;
		ht_info->CurrentAMPDUFactor = ht_info->AMPDU_Factor;

		ht_info->current_mpdu_density = ht_info->current_mpdu_density;

		HTFilterMCSRate(ieee, ieee->reg_dot11tx_ht_oper_rate_set,
				ieee->dot11ht_oper_rate_set);
		ieee->HTHighestOperaRate = HTGetHighestMCSRate(ieee,
							       ieee->dot11ht_oper_rate_set,
							       MCS_FILTER_ALL);
		ieee->HTCurrentOperaRate = ieee->HTHighestOperaRate;

	} else {
		ht_info->bCurrentHTSupport = false;
	}
}

u8 HTCCheck(struct rtllib_device *ieee, u8 *pFrame)
{
	if (ieee->ht_info->bCurrentHTSupport) {
		if ((IsQoSDataFrame(pFrame) && Frame_Order(pFrame)) == 1) {
			netdev_dbg(ieee->dev, "HT CONTROL FILED EXIST!!\n");
			return true;
		}
	}
	return false;
}

static void HTSetConnectBwModeCallback(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;

	if (ht_info->bCurBW40MHz) {
		if (ht_info->CurSTAExtChnlOffset == HT_EXTCHNL_OFFSET_UPPER)
			ieee->set_chan(ieee->dev,
				       ieee->current_network.channel + 2);
		else if (ht_info->CurSTAExtChnlOffset ==
			 HT_EXTCHNL_OFFSET_LOWER)
			ieee->set_chan(ieee->dev,
				       ieee->current_network.channel - 2);
		else
			ieee->set_chan(ieee->dev,
				       ieee->current_network.channel);

		ieee->set_bw_mode_handler(ieee->dev, HT_CHANNEL_WIDTH_20_40,
				       ht_info->CurSTAExtChnlOffset);
	} else {
		ieee->set_chan(ieee->dev, ieee->current_network.channel);
		ieee->set_bw_mode_handler(ieee->dev, HT_CHANNEL_WIDTH_20,
				       HT_EXTCHNL_OFFSET_NO_EXT);
	}

	ht_info->sw_bw_in_progress = false;
}

void HTSetConnectBwMode(struct rtllib_device *ieee,
			enum ht_channel_width bandwidth,
			enum ht_extchnl_offset Offset)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;

	if (!ht_info->bRegBW40MHz)
		return;

	if (ieee->GetHalfNmodeSupportByAPsHandler(ieee->dev))
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
			ht_info->bCurBW40MHz = true;
			ht_info->CurSTAExtChnlOffset = Offset;
		} else {
			ht_info->bCurBW40MHz = false;
			ht_info->CurSTAExtChnlOffset = HT_EXTCHNL_OFFSET_NO_EXT;
		}
	} else {
		ht_info->bCurBW40MHz = false;
		ht_info->CurSTAExtChnlOffset = HT_EXTCHNL_OFFSET_NO_EXT;
	}

	netdev_dbg(ieee->dev, "%s():ht_info->bCurBW40MHz:%x\n", __func__,
		   ht_info->bCurBW40MHz);

	ht_info->sw_bw_in_progress = true;

	HTSetConnectBwModeCallback(ieee);
}
