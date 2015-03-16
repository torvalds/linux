/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
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
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;

	pHTInfo->bAcceptAddbaReq = 1;

	pHTInfo->bRegShortGI20MHz = 1;
	pHTInfo->bRegShortGI40MHz = 1;

	pHTInfo->bRegBW40MHz = 1;

	if (pHTInfo->bRegBW40MHz)
		pHTInfo->bRegSuppCCK = 1;
	else
		pHTInfo->bRegSuppCCK = true;

	pHTInfo->nAMSDU_MaxSize = 7935UL;
	pHTInfo->bAMSDU_Support = 0;

	pHTInfo->bAMPDUEnable = 1;
	pHTInfo->AMPDU_Factor = 2;
	pHTInfo->MPDU_Density = 0;

	pHTInfo->SelfMimoPs = 3;
	if (pHTInfo->SelfMimoPs == 2)
		pHTInfo->SelfMimoPs = 3;
	ieee->bTxDisableRateFallBack = 0;
	ieee->bTxUseDriverAssingedRate = 0;

	ieee->bTxEnableFwCalcDur = 1;

	pHTInfo->bRegRT2RTAggregation = 1;

	pHTInfo->bRegRxReorderEnable = 1;
	pHTInfo->RxReorderWinSize = 64;
	pHTInfo->RxReorderPendingTime = 30;
}

u16 HTMcsToDataRate(struct rtllib_device *ieee, u8 nMcsRate)
{
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;

	u8	is40MHz = (pHTInfo->bCurBW40MHz) ? 1 : 0;
	u8	isShortGI = (pHTInfo->bCurBW40MHz) ?
			    ((pHTInfo->bCurShortGI40MHz) ? 1 : 0) :
			    ((pHTInfo->bCurShortGI20MHz) ? 1 : 0);
	return MCS_DATA_RATE[is40MHz][isShortGI][(nMcsRate & 0x7f)];
}

u16  TxCountToDataRate(struct rtllib_device *ieee, u8 nDataRate)
{
	u16	CCKOFDMRate[12] = {0x02, 0x04, 0x0b, 0x16, 0x0c, 0x12, 0x18,
				   0x24, 0x30, 0x48, 0x60, 0x6c};
	u8	is40MHz = 0;
	u8	isShortGI = 0;

	if (nDataRate < 12) {
		return CCKOFDMRate[nDataRate];
	} else {
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
		return MCS_DATA_RATE[is40MHz][isShortGI][nDataRate&0xf];
	}
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
	else if (net->bssht.bdRT2RTAggregation)
		retValue = true;
	else
		retValue = false;

	return retValue;
}

static void HTIOTPeerDetermine(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;
	struct rtllib_network *net = &ieee->current_network;

	if (net->bssht.bdRT2RTAggregation) {
		pHTInfo->IOTPeer = HT_IOT_PEER_REALTEK;
		if (net->bssht.RT2RT_HT_Mode & RT_HT_CAP_USE_92SE)
			pHTInfo->IOTPeer = HT_IOT_PEER_REALTEK_92SE;
		if (net->bssht.RT2RT_HT_Mode & RT_HT_CAP_USE_SOFTAP)
			pHTInfo->IOTPeer = HT_IOT_PEER_92U_SOFTAP;
	} else if (net->broadcom_cap_exist)
		pHTInfo->IOTPeer = HT_IOT_PEER_BROADCOM;
	else if (!memcmp(net->bssid, UNKNOWN_BORADCOM, 3) ||
		 !memcmp(net->bssid, LINKSYSWRT330_LINKSYSWRT300_BROADCOM, 3) ||
		 !memcmp(net->bssid, LINKSYSWRT350_LINKSYSWRT150_BROADCOM, 3))
		pHTInfo->IOTPeer = HT_IOT_PEER_BROADCOM;
	else if ((memcmp(net->bssid, BELKINF5D8233V1_RALINK, 3) == 0) ||
		 (memcmp(net->bssid, BELKINF5D82334V3_RALINK, 3) == 0) ||
		 (memcmp(net->bssid, PCI_RALINK, 3) == 0) ||
		 (memcmp(net->bssid, EDIMAX_RALINK, 3) == 0) ||
		 (memcmp(net->bssid, AIRLINK_RALINK, 3) == 0) ||
		  net->ralink_cap_exist)
		pHTInfo->IOTPeer = HT_IOT_PEER_RALINK;
	else if ((net->atheros_cap_exist) ||
		(memcmp(net->bssid, DLINK_ATHEROS_1, 3) == 0) ||
		(memcmp(net->bssid, DLINK_ATHEROS_2, 3) == 0))
		pHTInfo->IOTPeer = HT_IOT_PEER_ATHEROS;
	else if ((memcmp(net->bssid, CISCO_BROADCOM, 3) == 0) ||
		  net->cisco_cap_exist)
		pHTInfo->IOTPeer = HT_IOT_PEER_CISCO;
	else if ((memcmp(net->bssid, LINKSYS_MARVELL_4400N, 3) == 0) ||
		  net->marvell_cap_exist)
		pHTInfo->IOTPeer = HT_IOT_PEER_MARVELL;
	else if (net->airgo_cap_exist)
		pHTInfo->IOTPeer = HT_IOT_PEER_AIRGO;
	else
		pHTInfo->IOTPeer = HT_IOT_PEER_UNKNOWN;

	RTLLIB_DEBUG(RTLLIB_DL_IOT, "Joseph debug!! IOTPEER: %x\n",
		     pHTInfo->IOTPeer);
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

static u8 HTIOTActIsDisableEDCATurbo(struct rtllib_device *ieee, u8 *PeerMacAddr)
{
	return false;
}

static u8 HTIOTActIsMgntUseCCK6M(struct rtllib_device *ieee,
				 struct rtllib_network *network)
{
	u8	retValue = 0;


	if (ieee->pHTInfo->IOTPeer == HT_IOT_PEER_BROADCOM)
		retValue = 1;

	return retValue;
}

static u8 HTIOTActIsCCDFsync(struct rtllib_device *ieee)
{
	u8	retValue = 0;

	if (ieee->pHTInfo->IOTPeer == HT_IOT_PEER_BROADCOM)
		retValue = 1;
	return retValue;
}

static void HTIOTActDetermineRaFunc(struct rtllib_device *ieee, bool bPeerRx2ss)
{
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;

	pHTInfo->IOTRaFunc &= HT_IOT_RAFUNC_DISABLE_ALL;

	if (pHTInfo->IOTPeer == HT_IOT_PEER_RALINK && !bPeerRx2ss)
		pHTInfo->IOTRaFunc |= HT_IOT_RAFUNC_PEER_1R;

	if (pHTInfo->IOTAction & HT_IOT_ACT_AMSDU_ENABLE)
		pHTInfo->IOTRaFunc |= HT_IOT_RAFUNC_TX_AMSDU;

}

void HTResetIOTSetting(struct rt_hi_throughput *pHTInfo)
{
	pHTInfo->IOTAction = 0;
	pHTInfo->IOTPeer = HT_IOT_PEER_UNKNOWN;
	pHTInfo->IOTRaFunc = 0;
}

void HTConstructCapabilityElement(struct rtllib_device *ieee, u8 *posHTCap,
				  u8 *len, u8 IsEncrypt, bool bAssoc)
{
	struct rt_hi_throughput *pHT = ieee->pHTInfo;
	struct ht_capab_ele *pCapELE = NULL;

	if ((posHTCap == NULL) || (pHT == NULL)) {
		RTLLIB_DEBUG(RTLLIB_DL_ERR,
			     "posHTCap or pHTInfo can't be null in HTConstructCapabilityElement()\n");
		return;
	}
	memset(posHTCap, 0, *len);

	if ((bAssoc) && (pHT->ePeerHTSpecVer == HT_SPEC_VER_EWC)) {
		u8	EWC11NHTCap[] = {0x00, 0x90, 0x4c, 0x33};

		memcpy(posHTCap, EWC11NHTCap, sizeof(EWC11NHTCap));
		pCapELE = (struct ht_capab_ele *)&(posHTCap[4]);
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

	pCapELE->MimoPwrSave		= pHT->SelfMimoPs;
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


	RTLLIB_DEBUG(RTLLIB_DL_HT,
		     "TX HT cap/info ele BW=%d MaxAMSDUSize:%d DssCCk:%d\n",
		     pCapELE->ChlWidth, pCapELE->MaxAMSDUSize, pCapELE->DssCCk);

	if (IsEncrypt) {
		pCapELE->MPDUDensity	= 7;
		pCapELE->MaxRxAMPDUFactor	= 2;
	} else {
		pCapELE->MaxRxAMPDUFactor	= 3;
		pCapELE->MPDUDensity	= 0;
	}

	memcpy(pCapELE->MCS, ieee->Regdot11HTOperationalRateSet, 16);
	memset(&pCapELE->ExtHTCapInfo, 0, 2);
	memset(pCapELE->TxBFCap, 0, 4);

	pCapELE->ASCap = 0;

	if (bAssoc) {
		if (pHT->IOTAction & HT_IOT_ACT_DISABLE_MCS15)
			pCapELE->MCS[1] &= 0x7f;

		if (pHT->IOTAction & HT_IOT_ACT_DISABLE_MCS14)
			pCapELE->MCS[1] &= 0xbf;

		if (pHT->IOTAction & HT_IOT_ACT_DISABLE_ALL_2SS)
			pCapELE->MCS[1] &= 0x00;

		if (pHT->IOTAction & HT_IOT_ACT_DISABLE_RX_40MHZ_SHORT_GI)
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
	struct rt_hi_throughput *pHT = ieee->pHTInfo;
	struct ht_info_ele *pHTInfoEle = (struct ht_info_ele *)posHTInfo;

	if ((posHTInfo == NULL) || (pHTInfoEle == NULL)) {
		RTLLIB_DEBUG(RTLLIB_DL_ERR,
			     "posHTInfo or pHTInfoEle can't be null in HTConstructInfoElement()\n");
		return;
	}

	memset(posHTInfo, 0, *len);
	if ((ieee->iw_mode == IW_MODE_ADHOC) ||
	    (ieee->iw_mode == IW_MODE_MASTER)) {
		pHTInfoEle->ControlChl	= ieee->current_network.channel;
		pHTInfoEle->ExtChlOffset = ((pHT->bRegBW40MHz == false) ?
					    HT_EXTCHNL_OFFSET_NO_EXT :
					    (ieee->current_network.channel <= 6)
					    ? HT_EXTCHNL_OFFSET_UPPER :
					    HT_EXTCHNL_OFFSET_LOWER);
		pHTInfoEle->RecommemdedTxWidth	= pHT->bRegBW40MHz;
		pHTInfoEle->RIFS			= 0;
		pHTInfoEle->PSMPAccessOnly		= 0;
		pHTInfoEle->SrvIntGranularity		= 0;
		pHTInfoEle->OptMode			= pHT->CurrentOpMode;
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
	if (posRT2RTAgg == NULL) {
		RTLLIB_DEBUG(RTLLIB_DL_ERR,
			     "posRT2RTAgg can't be null in HTConstructRT2RTAggElement()\n");
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

	if (pOperateMCS == NULL) {
		RTLLIB_DEBUG(RTLLIB_DL_ERR,
			     "pOperateMCS can't be null in HT_PickMCSRate()\n");
		return false;
	}

	switch (ieee->mode) {
	case IEEE_A:
	case IEEE_B:
	case IEEE_G:
		for (i = 0; i <= 15; i++)
			pOperateMCS[i] = 0;
		break;
	case IEEE_N_24G:
	case IEEE_N_5G:
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

	if (pMCSRateSet == NULL || pMCSFilter == NULL) {
		RTLLIB_DEBUG(RTLLIB_DL_ERR,
			     "pMCSRateSet or pMCSFilter can't be null in HTGetHighestMCSRate()\n");
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
				if ((bitMap%2) != 0) {
					if (HTMcsToDataRate(ieee, (8*i+j)) >
					    HTMcsToDataRate(ieee, mcsRate))
						mcsRate = (8*i+j);
				}
				bitMap >>= 1;
			}
		}
	}
	return mcsRate | 0x80;
}

u8 HTFilterMCSRate(struct rtllib_device *ieee, u8 *pSupportMCS, u8 *pOperateMCS)
{

	u8 i;

	for (i = 0; i <= 15; i++)
		pOperateMCS[i] = ieee->Regdot11TxHTOperationalRateSet[i] &
				 pSupportMCS[i];

	HT_PickMCSRate(ieee, pOperateMCS);

	if (ieee->GetHalfNmodeSupportByAPsHandler(ieee->dev))
		pOperateMCS[1] = 0;

	for (i = 2; i <= 15; i++)
		pOperateMCS[i] = 0;

	return true;
}

void HTSetConnectBwMode(struct rtllib_device *ieee,
			enum ht_channel_width Bandwidth,
			enum ht_extchnl_offset Offset);

void HTOnAssocRsp(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;
	struct ht_capab_ele *pPeerHTCap = NULL;
	struct ht_info_ele *pPeerHTInfo = NULL;
	u16 nMaxAMSDUSize = 0;
	u8 *pMcsFilter = NULL;

	static u8 EWC11NHTCap[] = {0x00, 0x90, 0x4c, 0x33};
	static u8 EWC11NHTInfo[] = {0x00, 0x90, 0x4c, 0x34};

	if (pHTInfo->bCurrentHTSupport == false) {
		RTLLIB_DEBUG(RTLLIB_DL_ERR,
			     "<=== HTOnAssocRsp(): HT_DISABLE\n");
		return;
	}
	RTLLIB_DEBUG(RTLLIB_DL_HT, "===> HTOnAssocRsp_wq(): HT_ENABLE\n");

	if (!memcmp(pHTInfo->PeerHTCapBuf, EWC11NHTCap, sizeof(EWC11NHTCap)))
		pPeerHTCap = (struct ht_capab_ele *)(&pHTInfo->PeerHTCapBuf[4]);
	else
		pPeerHTCap = (struct ht_capab_ele *)(pHTInfo->PeerHTCapBuf);

	if (!memcmp(pHTInfo->PeerHTInfoBuf, EWC11NHTInfo, sizeof(EWC11NHTInfo)))
		pPeerHTInfo = (struct ht_info_ele *)
			     (&pHTInfo->PeerHTInfoBuf[4]);
	else
		pPeerHTInfo = (struct ht_info_ele *)(pHTInfo->PeerHTInfoBuf);

	RTLLIB_DEBUG_DATA(RTLLIB_DL_DATA | RTLLIB_DL_HT, pPeerHTCap,
			  sizeof(struct ht_capab_ele));
	HTSetConnectBwMode(ieee, (enum ht_channel_width)(pPeerHTCap->ChlWidth),
			  (enum ht_extchnl_offset)(pPeerHTInfo->ExtChlOffset));
	pHTInfo->bCurTxBW40MHz = ((pPeerHTInfo->RecommemdedTxWidth == 1) ?
				 true : false);

	pHTInfo->bCurShortGI20MHz = ((pHTInfo->bRegShortGI20MHz) ?
				    ((pPeerHTCap->ShortGI20Mhz == 1) ?
				    true : false) : false);
	pHTInfo->bCurShortGI40MHz = ((pHTInfo->bRegShortGI40MHz) ?
				     ((pPeerHTCap->ShortGI40Mhz == 1) ?
				     true : false) : false);

	pHTInfo->bCurSuppCCK = ((pHTInfo->bRegSuppCCK) ?
			       ((pPeerHTCap->DssCCk == 1) ? true :
			       false) : false);


	pHTInfo->bCurrent_AMSDU_Support = pHTInfo->bAMSDU_Support;

	nMaxAMSDUSize = (pPeerHTCap->MaxAMSDUSize == 0) ? 3839 : 7935;

	if (pHTInfo->nAMSDU_MaxSize > nMaxAMSDUSize)
		pHTInfo->nCurrent_AMSDU_MaxSize = nMaxAMSDUSize;
	else
		pHTInfo->nCurrent_AMSDU_MaxSize = pHTInfo->nAMSDU_MaxSize;

	pHTInfo->bCurrentAMPDUEnable = pHTInfo->bAMPDUEnable;
	if (ieee->rtllib_ap_sec_type &&
	   (ieee->rtllib_ap_sec_type(ieee)&(SEC_ALG_WEP|SEC_ALG_TKIP))) {
		if ((pHTInfo->IOTPeer == HT_IOT_PEER_ATHEROS) ||
				(pHTInfo->IOTPeer == HT_IOT_PEER_UNKNOWN))
			pHTInfo->bCurrentAMPDUEnable = false;
	}

	if (!pHTInfo->bRegRT2RTAggregation) {
		if (pHTInfo->AMPDU_Factor > pPeerHTCap->MaxRxAMPDUFactor)
			pHTInfo->CurrentAMPDUFactor =
						 pPeerHTCap->MaxRxAMPDUFactor;
		else
			pHTInfo->CurrentAMPDUFactor = pHTInfo->AMPDU_Factor;

	} else {
		if (ieee->current_network.bssht.bdRT2RTAggregation) {
			if (ieee->pairwise_key_type != KEY_TYPE_NA)
				pHTInfo->CurrentAMPDUFactor =
						 pPeerHTCap->MaxRxAMPDUFactor;
			else
				pHTInfo->CurrentAMPDUFactor = HT_AGG_SIZE_64K;
		} else {
			if (pPeerHTCap->MaxRxAMPDUFactor < HT_AGG_SIZE_32K)
				pHTInfo->CurrentAMPDUFactor =
						 pPeerHTCap->MaxRxAMPDUFactor;
			else
				pHTInfo->CurrentAMPDUFactor = HT_AGG_SIZE_32K;
		}
	}
	if (pHTInfo->MPDU_Density > pPeerHTCap->MPDUDensity)
		pHTInfo->CurrentMPDUDensity = pHTInfo->MPDU_Density;
	else
		pHTInfo->CurrentMPDUDensity = pPeerHTCap->MPDUDensity;
	if (pHTInfo->IOTAction & HT_IOT_ACT_TX_USE_AMSDU_8K) {
		pHTInfo->bCurrentAMPDUEnable = false;
		pHTInfo->ForcedAMSDUMode = HT_AGG_FORCE_ENABLE;
		pHTInfo->ForcedAMSDUMaxSize = 7935;
	}
	pHTInfo->bCurRxReorderEnable = pHTInfo->bRegRxReorderEnable;

	if (pPeerHTCap->MCS[0] == 0)
		pPeerHTCap->MCS[0] = 0xff;

	HTIOTActDetermineRaFunc(ieee, ((pPeerHTCap->MCS[1]) != 0));

	HTFilterMCSRate(ieee, pPeerHTCap->MCS, ieee->dot11HTOperationalRateSet);

	pHTInfo->PeerMimoPs = pPeerHTCap->MimoPwrSave;
	if (pHTInfo->PeerMimoPs == MIMO_PS_STATIC)
		pMcsFilter = MCS_FILTER_1SS;
	else
		pMcsFilter = MCS_FILTER_ALL;
	ieee->HTHighestOperaRate = HTGetHighestMCSRate(ieee,
				   ieee->dot11HTOperationalRateSet, pMcsFilter);
	ieee->HTCurrentOperaRate = ieee->HTHighestOperaRate;

	pHTInfo->CurrentOpMode = pPeerHTInfo->OptMode;
}

void HTInitializeHTInfo(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;

	RTLLIB_DEBUG(RTLLIB_DL_HT, "===========>%s()\n", __func__);
	pHTInfo->bCurrentHTSupport = false;

	pHTInfo->bCurBW40MHz = false;
	pHTInfo->bCurTxBW40MHz = false;

	pHTInfo->bCurShortGI20MHz = false;
	pHTInfo->bCurShortGI40MHz = false;
	pHTInfo->bForcedShortGI = false;

	pHTInfo->bCurSuppCCK = true;

	pHTInfo->bCurrent_AMSDU_Support = false;
	pHTInfo->nCurrent_AMSDU_MaxSize = pHTInfo->nAMSDU_MaxSize;
	pHTInfo->CurrentMPDUDensity = pHTInfo->MPDU_Density;
	pHTInfo->CurrentAMPDUFactor = pHTInfo->AMPDU_Factor;

	memset((void *)(&(pHTInfo->SelfHTCap)), 0,
		sizeof(pHTInfo->SelfHTCap));
	memset((void *)(&(pHTInfo->SelfHTInfo)), 0,
		sizeof(pHTInfo->SelfHTInfo));
	memset((void *)(&(pHTInfo->PeerHTCapBuf)), 0,
		sizeof(pHTInfo->PeerHTCapBuf));
	memset((void *)(&(pHTInfo->PeerHTInfoBuf)), 0,
		sizeof(pHTInfo->PeerHTInfoBuf));

	pHTInfo->bSwBwInProgress = false;
	pHTInfo->ChnlOp = CHNLOP_NONE;

	pHTInfo->ePeerHTSpecVer = HT_SPEC_VER_IEEE;

	pHTInfo->bCurrentRT2RTAggregation = false;
	pHTInfo->bCurrentRT2RTLongSlotTime = false;
	pHTInfo->RT2RT_HT_Mode = (enum rt_ht_capability)0;

	pHTInfo->IOTPeer = 0;
	pHTInfo->IOTAction = 0;
	pHTInfo->IOTRaFunc = 0;

	{
		u8 *RegHTSuppRateSets = &(ieee->RegHTSuppRateSet[0]);

		RegHTSuppRateSets[0] = 0xFF;
		RegHTSuppRateSets[1] = 0xFF;
		RegHTSuppRateSets[4] = 0x01;
	}
}

void HTInitializeBssDesc(struct bss_ht *pBssHT)
{

	pBssHT->bdSupportHT = false;
	memset(pBssHT->bdHTCapBuf, 0, sizeof(pBssHT->bdHTCapBuf));
	pBssHT->bdHTCapLen = 0;
	memset(pBssHT->bdHTInfoBuf, 0, sizeof(pBssHT->bdHTInfoBuf));
	pBssHT->bdHTInfoLen = 0;

	pBssHT->bdHTSpecVer = HT_SPEC_VER_IEEE;

	pBssHT->bdRT2RTAggregation = false;
	pBssHT->bdRT2RTLongSlotTime = false;
	pBssHT->RT2RT_HT_Mode = (enum rt_ht_capability)0;
}

void HTResetSelfAndSavePeerSetting(struct rtllib_device *ieee,
				   struct rtllib_network *pNetwork)
{
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;
	u8	bIOTAction = 0;

	RTLLIB_DEBUG(RTLLIB_DL_HT, "==============>%s()\n", __func__);
	/* unmark bEnableHT flag here is the same reason why unmarked in
	 * function rtllib_softmac_new_net. WB 2008.09.10*/
	if (pNetwork->bssht.bdSupportHT) {
		pHTInfo->bCurrentHTSupport = true;
		pHTInfo->ePeerHTSpecVer = pNetwork->bssht.bdHTSpecVer;

		if (pNetwork->bssht.bdHTCapLen > 0 &&
		    pNetwork->bssht.bdHTCapLen <= sizeof(pHTInfo->PeerHTCapBuf))
			memcpy(pHTInfo->PeerHTCapBuf,
			       pNetwork->bssht.bdHTCapBuf,
			       pNetwork->bssht.bdHTCapLen);

		if (pNetwork->bssht.bdHTInfoLen > 0 &&
		    pNetwork->bssht.bdHTInfoLen <=
		    sizeof(pHTInfo->PeerHTInfoBuf))
			memcpy(pHTInfo->PeerHTInfoBuf,
			       pNetwork->bssht.bdHTInfoBuf,
			       pNetwork->bssht.bdHTInfoLen);

		if (pHTInfo->bRegRT2RTAggregation) {
			pHTInfo->bCurrentRT2RTAggregation =
				 pNetwork->bssht.bdRT2RTAggregation;
			pHTInfo->bCurrentRT2RTLongSlotTime =
				 pNetwork->bssht.bdRT2RTLongSlotTime;
			pHTInfo->RT2RT_HT_Mode = pNetwork->bssht.RT2RT_HT_Mode;
		} else {
			pHTInfo->bCurrentRT2RTAggregation = false;
			pHTInfo->bCurrentRT2RTLongSlotTime = false;
			pHTInfo->RT2RT_HT_Mode = (enum rt_ht_capability)0;
		}

		HTIOTPeerDetermine(ieee);

		pHTInfo->IOTAction = 0;
		bIOTAction = HTIOTActIsDisableMCS14(ieee, pNetwork->bssid);
		if (bIOTAction)
			pHTInfo->IOTAction |= HT_IOT_ACT_DISABLE_MCS14;

		bIOTAction = HTIOTActIsDisableMCS15(ieee);
		if (bIOTAction)
			pHTInfo->IOTAction |= HT_IOT_ACT_DISABLE_MCS15;

		bIOTAction = HTIOTActIsDisableMCSTwoSpatialStream(ieee);
		if (bIOTAction)
			pHTInfo->IOTAction |= HT_IOT_ACT_DISABLE_ALL_2SS;


		bIOTAction = HTIOTActIsDisableEDCATurbo(ieee, pNetwork->bssid);
		if (bIOTAction)
			pHTInfo->IOTAction |= HT_IOT_ACT_DISABLE_EDCA_TURBO;

		bIOTAction = HTIOTActIsMgntUseCCK6M(ieee, pNetwork);
		if (bIOTAction)
			pHTInfo->IOTAction |= HT_IOT_ACT_MGNT_USE_CCK_6M;
		bIOTAction = HTIOTActIsCCDFsync(ieee);
		if (bIOTAction)
			pHTInfo->IOTAction |= HT_IOT_ACT_CDD_FSYNC;
	} else {
		pHTInfo->bCurrentHTSupport = false;
		pHTInfo->bCurrentRT2RTAggregation = false;
		pHTInfo->bCurrentRT2RTLongSlotTime = false;
		pHTInfo->RT2RT_HT_Mode = (enum rt_ht_capability)0;

		pHTInfo->IOTAction = 0;
		pHTInfo->IOTRaFunc = 0;
	}
}

void HT_update_self_and_peer_setting(struct rtllib_device *ieee,
				     struct rtllib_network *pNetwork)
{
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;
	struct ht_info_ele *pPeerHTInfo =
		 (struct ht_info_ele *)pNetwork->bssht.bdHTInfoBuf;

	if (pHTInfo->bCurrentHTSupport) {
		if (pNetwork->bssht.bdHTInfoLen != 0)
			pHTInfo->CurrentOpMode = pPeerHTInfo->OptMode;
	}
}
EXPORT_SYMBOL(HT_update_self_and_peer_setting);

void HTUseDefaultSetting(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;

	if (pHTInfo->bEnableHT) {
		pHTInfo->bCurrentHTSupport = true;
		pHTInfo->bCurSuppCCK = pHTInfo->bRegSuppCCK;

		pHTInfo->bCurBW40MHz = pHTInfo->bRegBW40MHz;
		pHTInfo->bCurShortGI20MHz = pHTInfo->bRegShortGI20MHz;

		pHTInfo->bCurShortGI40MHz = pHTInfo->bRegShortGI40MHz;

		if (ieee->iw_mode == IW_MODE_ADHOC)
			ieee->current_network.qos_data.active =
				 ieee->current_network.qos_data.supported;
		pHTInfo->bCurrent_AMSDU_Support = pHTInfo->bAMSDU_Support;
		pHTInfo->nCurrent_AMSDU_MaxSize = pHTInfo->nAMSDU_MaxSize;

		pHTInfo->bCurrentAMPDUEnable = pHTInfo->bAMPDUEnable;
		pHTInfo->CurrentAMPDUFactor = pHTInfo->AMPDU_Factor;

		pHTInfo->CurrentMPDUDensity = pHTInfo->CurrentMPDUDensity;

		HTFilterMCSRate(ieee, ieee->Regdot11TxHTOperationalRateSet,
				ieee->dot11HTOperationalRateSet);
		ieee->HTHighestOperaRate = HTGetHighestMCSRate(ieee,
					   ieee->dot11HTOperationalRateSet,
					   MCS_FILTER_ALL);
		ieee->HTCurrentOperaRate = ieee->HTHighestOperaRate;

	} else {
		pHTInfo->bCurrentHTSupport = false;
	}
}

u8 HTCCheck(struct rtllib_device *ieee, u8 *pFrame)
{
	if (ieee->pHTInfo->bCurrentHTSupport) {
		if ((IsQoSDataFrame(pFrame) && Frame_Order(pFrame)) == 1) {
			RTLLIB_DEBUG(RTLLIB_DL_HT,
				     "HT CONTROL FILED EXIST!!\n");
			return true;
		}
	}
	return false;
}

static void HTSetConnectBwModeCallback(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;

	RTLLIB_DEBUG(RTLLIB_DL_HT, "======>%s()\n", __func__);
	if (pHTInfo->bCurBW40MHz) {
		if (pHTInfo->CurSTAExtChnlOffset == HT_EXTCHNL_OFFSET_UPPER)
			ieee->set_chan(ieee->dev,
				       ieee->current_network.channel + 2);
		else if (pHTInfo->CurSTAExtChnlOffset ==
			 HT_EXTCHNL_OFFSET_LOWER)
			ieee->set_chan(ieee->dev,
				       ieee->current_network.channel - 2);
		else
			ieee->set_chan(ieee->dev,
				       ieee->current_network.channel);

		ieee->SetBWModeHandler(ieee->dev, HT_CHANNEL_WIDTH_20_40,
				       pHTInfo->CurSTAExtChnlOffset);
	} else {
		ieee->set_chan(ieee->dev, ieee->current_network.channel);
		ieee->SetBWModeHandler(ieee->dev, HT_CHANNEL_WIDTH_20,
				       HT_EXTCHNL_OFFSET_NO_EXT);
	}

	pHTInfo->bSwBwInProgress = false;
}

void HTSetConnectBwMode(struct rtllib_device *ieee,
			enum ht_channel_width Bandwidth,
			enum ht_extchnl_offset Offset)
{
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;

	if (pHTInfo->bRegBW40MHz == false)
		return;

	if (ieee->GetHalfNmodeSupportByAPsHandler(ieee->dev))
		Bandwidth = HT_CHANNEL_WIDTH_20;

	if (pHTInfo->bSwBwInProgress) {
		pr_info("%s: bSwBwInProgress!!\n", __func__);
		return;
	}
	if (Bandwidth == HT_CHANNEL_WIDTH_20_40) {
		if (ieee->current_network.channel < 2 &&
		    Offset == HT_EXTCHNL_OFFSET_LOWER)
			Offset = HT_EXTCHNL_OFFSET_NO_EXT;
		if (Offset == HT_EXTCHNL_OFFSET_UPPER ||
		    Offset == HT_EXTCHNL_OFFSET_LOWER) {
			pHTInfo->bCurBW40MHz = true;
			pHTInfo->CurSTAExtChnlOffset = Offset;
		} else {
			pHTInfo->bCurBW40MHz = false;
			pHTInfo->CurSTAExtChnlOffset = HT_EXTCHNL_OFFSET_NO_EXT;
		}
	} else {
		pHTInfo->bCurBW40MHz = false;
		pHTInfo->CurSTAExtChnlOffset = HT_EXTCHNL_OFFSET_NO_EXT;
	}

	pr_info("%s():pHTInfo->bCurBW40MHz:%x\n", __func__,
	       pHTInfo->bCurBW40MHz);

	pHTInfo->bSwBwInProgress = true;

	HTSetConnectBwModeCallback(ieee);
}
