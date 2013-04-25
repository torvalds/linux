/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#ifdef RT_CFG80211_SUPPORT

#define RT_CFG80211_REGISTER(__pDev, __pNetDev)								\
	CFG80211_Register(__pDev, __pNetDev);

#define RT_CFG80211_BEACON_CR_PARSE(__pAd, __pVIE, __LenVIE)				\
	CFG80211_BeaconCountryRegionParse((VOID *)__pAd, __pVIE, __LenVIE);

#define RT_CFG80211_CRDA_REG_HINT(__pAd, __pCountryIe, __CountryIeLen)		\
	CFG80211_RegHint((VOID *)__pAd, __pCountryIe, __CountryIeLen);

#define RT_CFG80211_CRDA_REG_HINT11D(__pAd, __pCountryIe, __CountryIeLen)	\
	CFG80211_RegHint11D((VOID *)__pAd, __pCountryIe, __CountryIeLen);

#define RT_CFG80211_CRDA_REG_RULE_APPLY(__pAd)								\
	CFG80211_RegRuleApply((VOID *)__pAd, NULL, __pAd->Cfg80211_Alpha2);

#define RT_CFG80211_SCANNING_INFORM(__pAd, __BssIdx, __ChanId, __pFrame,	\
			__FrameLen, __RSSI)									\
	CFG80211_Scaning((VOID *)__pAd, __BssIdx, __ChanId, __pFrame,			\
						__FrameLen, __RSSI);

#define RT_CFG80211_SCAN_END(__pAd, __FlgIsAborted)							\
	CFG80211_ScanEnd((VOID *)__pAd, __FlgIsAborted);

#define RT_CFG80211_REINIT(__pAd)											\
	CFG80211_SupBandReInit((VOID *)__pAd);									\

#define RT_CFG80211_CONN_RESULT_INFORM(__pAd, __pBSSID, __pReqIe, __ReqIeLen,\
			__pRspIe, __RspIeLen, __FlgIsSuccess)							\
	CFG80211_ConnectResultInform((VOID *)__pAd, __pBSSID,					\
			__pReqIe, __ReqIeLen, __pRspIe, __RspIeLen, __FlgIsSuccess);

#define RT_CFG80211_RFKILL_STATUS_UPDATE(_pAd, _active) \
	CFG80211_RFKillStatusUpdate(_pAd, _active);

#ifdef SINGLE_SKU
#define CFG80211_BANDINFO_FILL(__pAd, __pBandInfo)							\
{																			\
	(__pBandInfo)->RFICType = __pAd->RFICType;								\
	(__pBandInfo)->MpduDensity = __pAd->CommonCfg.BACapability.field.MpduDensity;\
	(__pBandInfo)->TxStream = __pAd->CommonCfg.TxStream;					\
	(__pBandInfo)->RxStream = __pAd->CommonCfg.RxStream;					\
	(__pBandInfo)->MaxTxPwr = __pAd->CommonCfg.DefineMaxTxPwr;				\
	if (__pAd->CommonCfg.PhyMode == PHY_11B)								\
		(__pBandInfo)->FlgIsBMode = TRUE;									\
	else																	\
		(__pBandInfo)->FlgIsBMode = FALSE;									\
	(__pBandInfo)->MaxBssTable = MAX_LEN_OF_BSS_TABLE;						\
	(__pBandInfo)->RtsThreshold = pAd->CommonCfg.RtsThreshold;				\
	(__pBandInfo)->FragmentThreshold = pAd->CommonCfg.FragmentThreshold;	\
	(__pBandInfo)->RetryMaxCnt = 0;											\
	RTMP_IO_READ32(__pAd, TX_RTY_CFG, &((__pBandInfo)->RetryMaxCnt));		\
}
#else
#define CFG80211_BANDINFO_FILL(__pAd, __pBandInfo)							\
{																			\
	(__pBandInfo)->RFICType = __pAd->RFICType;								\
	(__pBandInfo)->MpduDensity = __pAd->CommonCfg.BACapability.field.MpduDensity;\
	(__pBandInfo)->TxStream = __pAd->CommonCfg.TxStream;					\
	(__pBandInfo)->RxStream = __pAd->CommonCfg.RxStream;					\
	(__pBandInfo)->MaxTxPwr = 0;											\
	if (__pAd->CommonCfg.PhyMode == PHY_11B)								\
		(__pBandInfo)->FlgIsBMode = TRUE;									\
	else																	\
		(__pBandInfo)->FlgIsBMode = FALSE;									\
	(__pBandInfo)->MaxBssTable = MAX_LEN_OF_BSS_TABLE;						\
	(__pBandInfo)->RtsThreshold = pAd->CommonCfg.RtsThreshold;				\
	(__pBandInfo)->FragmentThreshold = pAd->CommonCfg.FragmentThreshold;	\
	(__pBandInfo)->RetryMaxCnt = 0;											\
	RTMP_IO_READ32(__pAd, TX_RTY_CFG, &((__pBandInfo)->RetryMaxCnt));		\
}
#endif /* SINGLE_SKU */


/* utilities used in DRV module */
BOOLEAN CFG80211DRV_OpsSetChannel(
	VOID						*pAdOrg,
	VOID						*pData);

BOOLEAN CFG80211DRV_OpsChgVirtualInf(
	VOID						*pAdOrg,
	VOID						*pFlgFilter,
	UINT8						IfType);

BOOLEAN CFG80211DRV_OpsScan(
	VOID						*pAdOrg);

BOOLEAN CFG80211DRV_OpsJoinIbss(
	VOID						*pAdOrg,
	VOID						*pData);

BOOLEAN CFG80211DRV_OpsLeave(
	VOID						*pAdOrg);

BOOLEAN CFG80211DRV_StaGet(
	VOID						*pAdOrg,
	VOID						*pData);

BOOLEAN CFG80211DRV_Connect(
	VOID						*pAdOrg,
	VOID						*pData);

BOOLEAN CFG80211DRV_KeyAdd(
	VOID						*pAdOrg,
	VOID						*pData);

VOID CFG80211DRV_RegNotify(
	VOID						*pAdOrg,
	VOID						*pData);

VOID CFG80211_RegHint(
	IN VOID						*pAdCB,
	IN UCHAR					*pCountryIe,
	IN ULONG					CountryIeLen);

VOID CFG80211_RegHint11D(
	IN VOID						*pAdCB,
	IN UCHAR					*pCountryIe,
	IN ULONG					CountryIeLen);

VOID CFG80211_ScanEnd(
	IN VOID						*pAdCB,
	IN BOOLEAN					FlgIsAborted);

VOID CFG80211_ConnectResultInform(
	IN VOID						*pAdCB,
	IN UCHAR					*pBSSID,
	IN UCHAR					*pReqIe,
	IN UINT32					ReqIeLen,
	IN UCHAR					*pRspIe,
	IN UINT32					RspIeLen,
	IN UCHAR					FlgIsSuccess);

BOOLEAN CFG80211_SupBandReInit(
	IN VOID						*pAdCB);

VOID CFG80211_RegRuleApply(
	IN VOID						*pAdCB,
	IN VOID						*pWiphy,
	IN UCHAR					*pAlpha2);

VOID CFG80211_Scaning(
	IN VOID						*pAdCB,
	IN UINT32					BssIdx,
	IN UINT32					ChanId,
	IN UCHAR					*pFrame,
	IN UINT32					FrameLen,
	IN INT32					RSSI);

#ifdef RFKILL_HW_SUPPORT
VOID CFG80211_RFKillStatusUpdate(
	IN PVOID	pAd,
	IN BOOLEAN	active);
#endif /* RFKILL_HW_SUPPORT */

#endif /* RT_CFG80211_SUPPORT */

/* End of cfg80211extr.h */
