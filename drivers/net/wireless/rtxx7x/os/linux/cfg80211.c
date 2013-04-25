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


#define RTMP_MODULE_OS

/*#include "rt_config.h" */
#include "rtmp_comm.h"
#include "rt_os_util.h"
#include "rt_os_net.h"


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28))
#ifdef RT_CFG80211_SUPPORT

/* 36 ~ 64, 100 ~ 136, 140 ~ 161 */
#define CFG80211_NUM_OF_CHAN_5GHZ			\
							(sizeof(Cfg80211_Chan)-CFG80211_NUM_OF_CHAN_2GHZ)

#ifdef OS_ABL_FUNC_SUPPORT
/*
	Array of bitrates the hardware can operate with
	in this band. Must be sorted to give a valid "supported
	rates" IE, i.e. CCK rates first, then OFDM.

	For HT, assign MCS in another structure, ieee80211_sta_ht_cap.
*/
const struct ieee80211_rate Cfg80211_SupRate[12] = {
	{
		.flags = IEEE80211_RATE_SHORT_PREAMBLE,
		.bitrate = 10,
		.hw_value = 0,
		.hw_value_short = 0,
	},
	{
		.flags = IEEE80211_RATE_SHORT_PREAMBLE,
		.bitrate = 20,
		.hw_value = 1,
		.hw_value_short = 1,
	},
	{
		.flags = IEEE80211_RATE_SHORT_PREAMBLE,
		.bitrate = 55,
		.hw_value = 2,
		.hw_value_short = 2,
	},
	{
		.flags = IEEE80211_RATE_SHORT_PREAMBLE,
		.bitrate = 110,
		.hw_value = 3,
		.hw_value_short = 3,
	},
	{
		.flags = 0,
		.bitrate = 60,
		.hw_value = 4,
		.hw_value_short = 4,
	},
	{
		.flags = 0,
		.bitrate = 90,
		.hw_value = 5,
		.hw_value_short = 5,
	},
	{
		.flags = 0,
		.bitrate = 120,
		.hw_value = 6,
		.hw_value_short = 6,
	},
	{
		.flags = 0,
		.bitrate = 180,
		.hw_value = 7,
		.hw_value_short = 7,
	},
	{
		.flags = 0,
		.bitrate = 240,
		.hw_value = 8,
		.hw_value_short = 8,
	},
	{
		.flags = 0,
		.bitrate = 360,
		.hw_value = 9,
		.hw_value_short = 9,
	},
	{
		.flags = 0,
		.bitrate = 480,
		.hw_value = 10,
		.hw_value_short = 10,
	},
	{
		.flags = 0,
		.bitrate = 540,
		.hw_value = 11,
		.hw_value_short = 11,
	},
};
#endif /* OS_ABL_FUNC_SUPPORT */

/* all available channels */
static const UCHAR Cfg80211_Chan[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,

	/* 802.11 UNI / HyperLan 2 */
	36, 38, 40, 44, 46, 48, 52, 54, 56, 60, 62, 64,

	/* 802.11 HyperLan 2 */
	100, 104, 108, 112, 116, 118, 120, 124, 126, 128, 132, 134, 136,

	/* 802.11 UNII */
	140, 149, 151, 153, 157, 159, 161, 165, 167, 169, 171, 173,

	/* Japan */
	184, 188, 192, 196, 208, 212, 216,
};


static const UINT32 CipherSuites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
};



/*
	The driver's regulatory notification callback.
*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30))
static INT32 CFG80211_RegNotifier(
	IN struct wiphy					*pWiphy,
	IN struct regulatory_request	*pRequest);
#else
static INT32 CFG80211_RegNotifier(
	IN struct wiphy					*pWiphy,
	IN enum reg_set_by				Request);
#endif /* LINUX_VERSION_CODE */




/* =========================== Private Function ============================== */

/* get RALINK pAd control block in 80211 Ops */
#define MAC80211_PAD_GET(__pAd, __pWiphy)							\
	{																\
		ULONG *__pPriv;												\
		__pPriv = (ULONG *)(wiphy_priv(__pWiphy));					\
		__pAd = (VOID *)(*__pPriv);									\
		if (__pAd == NULL)											\
		{															\
			DBGPRINT(RT_DEBUG_ERROR,								\
					("80211> %s but pAd = NULL!", __FUNCTION__));	\
			return -EINVAL;											\
		}															\
	}

/*
========================================================================
Routine Description:
	Set channel.

Arguments:
	pWiphy			- Wireless hardware description
	pChan			- Channel information
	ChannelType		- Channel type

Return Value:
	0				- success
	-x				- fail

Note:
	For iw utility: set channel, set freq

	enum nl80211_channel_type {
		NL80211_CHAN_NO_HT,
		NL80211_CHAN_HT20,
		NL80211_CHAN_HT40MINUS,
		NL80211_CHAN_HT40PLUS
	};
========================================================================
*/
static int CFG80211_OpsSetChannel(
	IN struct wiphy					*pWiphy,
	IN struct ieee80211_channel		*pChan,
	IN enum nl80211_channel_type	ChannelType)
{
	VOID *pAd;
	CFG80211_CB *p80211CB;
	CMD_RTPRIV_IOCTL_80211_CHAN ChanInfo;
	UINT32 ChanId;


	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_OpsSetChannel ==>\n"));
	MAC80211_PAD_GET(pAd, pWiphy);

	/* get channel number */
	ChanId = ieee80211_frequency_to_channel(pChan->center_freq);
	CFG80211DBG(RT_DEBUG_ERROR, ("80211> Channel = %d\n", ChanId));
	CFG80211DBG(RT_DEBUG_ERROR, ("80211> ChannelType = %d\n", ChannelType));

	/* init */
	memset(&ChanInfo, 0, sizeof(ChanInfo));
	ChanInfo.ChanId = ChanId;

	p80211CB = NULL;
	RTMP_DRIVER_80211_CB_GET(pAd, &p80211CB);

	if (p80211CB == NULL)
	{
		CFG80211DBG(RT_DEBUG_ERROR, ("80211> p80211CB == NULL!\n"));
		return 0;
	}

	if (p80211CB->pCfg80211_Wdev->iftype == NL80211_IFTYPE_STATION)
		ChanInfo.IfType = RT_CMD_80211_IFTYPE_STATION;
	else if (p80211CB->pCfg80211_Wdev->iftype == NL80211_IFTYPE_ADHOC)
		ChanInfo.IfType = RT_CMD_80211_IFTYPE_ADHOC;
	else if (p80211CB->pCfg80211_Wdev->iftype == NL80211_IFTYPE_MONITOR)
		ChanInfo.IfType = RT_CMD_80211_IFTYPE_MONITOR;

	if (ChannelType == NL80211_CHAN_NO_HT)
		ChanInfo.ChanType = RT_CMD_80211_CHANTYPE_NOHT;
	else if (ChannelType == NL80211_CHAN_HT20)
		ChanInfo.ChanType = RT_CMD_80211_CHANTYPE_HT20;
	else if (ChannelType == NL80211_CHAN_HT40MINUS)
		ChanInfo.ChanType = RT_CMD_80211_CHANTYPE_HT40MINUS;
	else if (ChannelType == NL80211_CHAN_HT40PLUS)
		ChanInfo.ChanType = RT_CMD_80211_CHANTYPE_HT40PLUS;

	ChanInfo.MonFilterFlag = p80211CB->MonFilterFlag;

	/* set channel */
	RTMP_DRIVER_80211_CHAN_SET(pAd, &ChanInfo);

	return 0;
} /* End of CFG80211_OpsSetChannel */


/*
========================================================================
Routine Description:
	Change type/configuration of virtual interface.

Arguments:
	pWiphy			- Wireless hardware description
	IfIndex			- Interface index
	Type			- Interface type, managed/adhoc/ap/station, etc.
	pFlags			- Monitor flags
	pParams			- Mesh parameters

Return Value:
	0				- success
	-x				- fail

Note:
	For iw utility: set type, set monitor
========================================================================
*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
static int CFG80211_OpsChgVirtualInf(
	IN struct wiphy					*pWiphy,
	IN struct net_device			*pNetDevIn,
	IN enum nl80211_iftype			Type,
	IN u32							*pFlags,
	struct vif_params				*pParams)
#else
static int CFG80211_OpsChgVirtualInf(
	IN struct wiphy					*pWiphy,
	IN int							IfIndex,
	IN enum nl80211_iftype			Type,
	IN u32							*pFlags,
	struct vif_params				*pParams)
#endif /* LINUX_VERSION_CODE */
{
	VOID *pAd;
	CFG80211_CB *pCfg80211_CB;
	struct net_device *pNetDev;
	UINT32 Filter;


	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_OpsChgVirtualInf ==>\n"));
	MAC80211_PAD_GET(pAd, pWiphy);

	CFG80211DBG(RT_DEBUG_ERROR, ("80211> Type = %d\n", Type));

	/* sanity check */
#ifdef CONFIG_STA_SUPPORT
	if ((Type != NL80211_IFTYPE_ADHOC) &&
		(Type != NL80211_IFTYPE_STATION) &&
		(Type != NL80211_IFTYPE_MONITOR))
#endif /* CONFIG_STA_SUPPORT */
	{
		DBGPRINT(RT_DEBUG_ERROR, ("80211> Wrong interface type %d!\n", Type));
		return -EINVAL;
	} /* End of if */

	/* update interface type */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
	pNetDev = pNetDevIn;
#else
	pNetDev = __dev_get_by_index(&init_net, IfIndex);
#endif /* LINUX_VERSION_CODE */

	if (pNetDev == NULL)
		return -ENODEV;
	/* End of if */

	pNetDev->ieee80211_ptr->iftype = Type;

	if (pFlags != NULL)
	{
		Filter = 0;

		if (((*pFlags) & NL80211_MNTR_FLAG_FCSFAIL) == NL80211_MNTR_FLAG_FCSFAIL)
			Filter |= RT_CMD_80211_FILTER_FCSFAIL;

		if (((*pFlags) & NL80211_MNTR_FLAG_FCSFAIL) == NL80211_MNTR_FLAG_PLCPFAIL)
			Filter |= RT_CMD_80211_FILTER_PLCPFAIL;

		if (((*pFlags) & NL80211_MNTR_FLAG_CONTROL) == NL80211_MNTR_FLAG_CONTROL)
			Filter |= RT_CMD_80211_FILTER_CONTROL;

		if (((*pFlags) & NL80211_MNTR_FLAG_CONTROL) == NL80211_MNTR_FLAG_OTHER_BSS)
			Filter |= RT_CMD_80211_FILTER_OTHER_BSS;
	} /* End of if */

	RTMP_DRIVER_80211_VIF_SET(pAd, Filter, Type);

	RTMP_DRIVER_80211_CB_GET(pAd, &pCfg80211_CB);
	pCfg80211_CB->MonFilterFlag = Filter;
	return 0;
} /* End of CFG80211_OpsChgVirtualInf */


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30))
#if defined(SIOCGIWSCAN) || defined(RT_CFG80211_SUPPORT)
extern int rt_ioctl_siwscan(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wreq, char *extra);
#endif
#ifdef CONFIG_STA_SUPPORT
/*
========================================================================
Routine Description:
	Request to do a scan. If returning zero, the scan request is given
	the driver, and will be valid until passed to cfg80211_scan_done().
	For scan results, call cfg80211_inform_bss(); you can call this outside
	the scan/scan_done bracket too.

Arguments:
	pWiphy			- Wireless hardware description
	pNdev			- Network device interface
	pRequest		- Scan request

Return Value:
	0				- success
	-x				- fail

Note:
	For iw utility: scan

	struct cfg80211_scan_request {
		struct cfg80211_ssid *ssids;
		int n_ssids;
		struct ieee80211_channel **channels;
		u32 n_channels;
		const u8 *ie;
		size_t ie_len;

	 * @ssids: SSIDs to scan for (active scan only)
	 * @n_ssids: number of SSIDs
	 * @channels: channels to scan on.
	 * @n_channels: number of channels for each band
	 * @ie: optional information element(s) to add into Probe Request or %NULL
	 * @ie_len: length of ie in octets
========================================================================
*/
static int CFG80211_OpsScan(
	IN struct wiphy					*pWiphy,
	IN struct net_device			*pNdev,
	IN struct cfg80211_scan_request *pRequest)
{
	VOID *pAd;
	CFG80211_CB *pCfg80211_CB;


	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_OpsScan ==>\n"));
	MAC80211_PAD_GET(pAd, pWiphy);

	/* sanity check */
	if ((pNdev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION) &&
		(pNdev->ieee80211_ptr->iftype != NL80211_IFTYPE_ADHOC))
	{
		return -EOPNOTSUPP;
	} /* End of if */

	if (RTMP_DRIVER_IOCTL_SANITY_CHECK(pAd) != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("80211> Network is down!\n"));
		return -ENETDOWN;
	} /* End of if */

	if (RTMP_DRIVER_80211_SCAN(pAd) != NDIS_STATUS_SUCCESS)
		return -EBUSY; /* scanning */
	/* End of if */

	RTMP_DRIVER_80211_CB_GET(pAd, &pCfg80211_CB);
	pCfg80211_CB->pCfg80211_ScanReq = pRequest; /* used in scan end */

	rt_ioctl_siwscan(pNdev, NULL, NULL, NULL);
	return 0;
} /* End of CFG80211_OpsScan */
#endif /* CONFIG_STA_SUPPORT */
#endif /* LINUX_VERSION_CODE */


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
#ifdef CONFIG_STA_SUPPORT
/*
========================================================================
Routine Description:
	Join the specified IBSS (or create if necessary). Once done, call
	cfg80211_ibss_joined(), also call that function when changing BSSID due
	to a merge.

Arguments:
	pWiphy			- Wireless hardware description
	pNdev			- Network device interface
	pParams			- IBSS parameters

Return Value:
	0				- success
	-x				- fail

Note:
	For iw utility: ibss join

	No fixed-freq and fixed-bssid support.
========================================================================
*/
static int CFG80211_OpsJoinIbss(
	IN struct wiphy					*pWiphy,
	IN struct net_device			*pNdev,
	IN struct cfg80211_ibss_params	*pParams)
{
	VOID *pAd;
	CMD_RTPRIV_IOCTL_80211_IBSS IbssInfo;


	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_OpsJoinIbss ==>\n"));
	MAC80211_PAD_GET(pAd, pWiphy);

	CFG80211DBG(RT_DEBUG_ERROR, ("80211> SSID = %s\n",
				pParams->ssid));
	CFG80211DBG(RT_DEBUG_ERROR, ("80211> Beacon Interval = %d\n",
				pParams->beacon_interval));

	/* init */
	memset(&IbssInfo, 0, sizeof(IbssInfo));
	IbssInfo.BeaconInterval = pParams->beacon_interval;
	IbssInfo.pSsid = pParams->ssid;

	/* ibss join */
	RTMP_DRIVER_80211_IBSS_JOIN(pAd, &IbssInfo);

	return 0;
} /* End of CFG80211_OpsJoinIbss */


/*
========================================================================
Routine Description:
	Leave the IBSS.

Arguments:
	pWiphy			- Wireless hardware description
	pNdev			- Network device interface

Return Value:
	0				- success
	-x				- fail

Note:
	For iw utility: ibss leave
========================================================================
*/
static int CFG80211_OpsLeaveIbss(
	IN struct wiphy					*pWiphy,
	IN struct net_device			*pNdev)
{
	VOID *pAd;


	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_OpsLeaveIbss ==>\n"));
	MAC80211_PAD_GET(pAd, pWiphy);

	RTMP_DRIVER_80211_STA_LEAVE(pAd);
	return 0;
} /* End of CFG80211_OpsLeaveIbss */
#endif /* CONFIG_STA_SUPPORT */
#endif /* LINUX_VERSION_CODE */


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
/*
========================================================================
Routine Description:
	Set the transmit power according to the parameters.

Arguments:
	pWiphy			- Wireless hardware description
	Type			- 
	dBm				- dBm

Return Value:
	0				- success
	-x				- fail

Note:
	Type -
	TX_POWER_AUTOMATIC: the dbm parameter is ignored
	TX_POWER_LIMITED: limit TX power by the dbm parameter
	TX_POWER_FIXED: fix TX power to the dbm parameter
========================================================================
*/
static int CFG80211_TxPwrSet(
	IN struct wiphy						*pWiphy,
	IN enum tx_power_setting			Type,
	IN int								dBm)
{
	return -EOPNOTSUPP;
} /* End of CFG80211_TxPwrSet */


/*
========================================================================
Routine Description:
	Store the current TX power into the dbm variable.

Arguments:
	pWiphy			- Wireless hardware description
	pdBm			- dBm

Return Value:
	0				- success
	-x				- fail

Note:
========================================================================
*/
static int CFG80211_TxPwrGet(
	IN struct wiphy						*pWiphy,
	IN int								*pdBm)
{
	return -EOPNOTSUPP;
} /* End of CFG80211_TxPwrGet */


/*
========================================================================
Routine Description:
	Power management.

Arguments:
	pWiphy			- Wireless hardware description
	pNdev			- 
	FlgIsEnabled	-
	Timeout			-

Return Value:
	0				- success
	-x				- fail

Note:
========================================================================
*/
static int CFG80211_PwrMgmt(
	IN struct wiphy						*pWiphy,
	IN struct net_device				*pNdev,
	IN bool								FlgIsEnabled,
	IN int								Timeout)
{
	return -EOPNOTSUPP;
} /* End of CFG80211_PwrMgmt */


/*
========================================================================
Routine Description:
	Get information for a specific station.

Arguments:
	pWiphy			- Wireless hardware description
	pNdev			-
	pMac			- STA MAC
	pSinfo			- STA INFO

Return Value:
	0				- success
	-x				- fail

Note:
========================================================================
*/
static int CFG80211_StaGet(
	IN struct wiphy						*pWiphy,
	IN struct net_device				*pNdev,
	IN UINT8							*pMac,
	IN struct station_info				*pSinfo)
{
	VOID *pAd;
	CMD_RTPRIV_IOCTL_80211_STA StaInfo;


	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_StaGet ==>\n"));
	MAC80211_PAD_GET(pAd, pWiphy);

	/* init */
	memset(pSinfo, 0, sizeof(*pSinfo));
	memset(&StaInfo, 0, sizeof(StaInfo));

	memcpy(StaInfo.MAC, pMac, 6);

	/* get sta information */
	if (RTMP_DRIVER_80211_STA_GET(pAd, &StaInfo) != NDIS_STATUS_SUCCESS)
		return -ENOENT;

	if (StaInfo.TxRateFlags != RT_CMD_80211_TXRATE_LEGACY)
	{
		pSinfo->txrate.flags = RATE_INFO_FLAGS_MCS;
		if (StaInfo.TxRateFlags & RT_CMD_80211_TXRATE_BW_40)
			pSinfo->txrate.flags |= RATE_INFO_FLAGS_40_MHZ_WIDTH;
		/* End of if */
		if (StaInfo.TxRateFlags & RT_CMD_80211_TXRATE_SHORT_GI)
			pSinfo->txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		/* End of if */

		pSinfo->txrate.mcs = StaInfo.TxRateMCS;
	}
	else
	{
		pSinfo->txrate.legacy = StaInfo.TxRateMCS;
	} /* End of if */

	pSinfo->filled |= STATION_INFO_TX_BITRATE;

	/* fill signal */
	pSinfo->signal = StaInfo.Signal;
	pSinfo->filled |= STATION_INFO_SIGNAL;


	return 0;
} /* End of CFG80211_StaGet */


/*
========================================================================
Routine Description:
	List all stations known, e.g. the AP on managed interfaces.

Arguments:
	pWiphy			- Wireless hardware description
	pNdev			-
	Idx				- 
	pMac			-
	pSinfo			-

Return Value:
	0				- success
	-x				- fail

Note:
========================================================================
*/
static int CFG80211_StaDump(
	IN struct wiphy						*pWiphy,
	IN struct net_device				*pNdev,
	IN int								Idx,
	IN UINT8							*pMac,
	IN struct station_info				*pSinfo)
{
	VOID *pAd;


	if (Idx != 0)
		return -ENOENT;
	/* End of if */

	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_StaDump ==>\n"));
	MAC80211_PAD_GET(pAd, pWiphy);

#ifdef CONFIG_STA_SUPPORT
	if (RTMP_DRIVER_AP_SSID_GET(pAd, pMac) != NDIS_STATUS_SUCCESS)
		return -EBUSY;
	else
		return CFG80211_StaGet(pWiphy, pNdev, pMac, pSinfo);
#endif /* CONFIG_STA_SUPPORT */

	return -EOPNOTSUPP;
} /* End of CFG80211_StaDump */


/*
========================================================================
Routine Description:
	Notify that wiphy parameters have changed.

Arguments:
	pWiphy			- Wireless hardware description
	Changed			-

Return Value:
	0				- success
	-x				- fail

Note:
========================================================================
*/
static int CFG80211_WiphyParamsSet(
	IN struct wiphy						*pWiphy,
	IN UINT32							Changed)
{
	return -EOPNOTSUPP;
} /* End of CFG80211_WiphyParamsSet */


/*
========================================================================
Routine Description:
	Add a key with the given parameters.

Arguments:
	pWiphy			- Wireless hardware description
	pNdev			-
	KeyIdx			-
	pMacAddr		-
	pParams			-

Return Value:
	0				- success
	-x				- fail

Note:
	pMacAddr will be NULL when adding a group key.
========================================================================
*/
static int CFG80211_KeyAdd(
	IN struct wiphy						*pWiphy,
	IN struct net_device				*pNdev,
	IN UINT8							KeyIdx,
	IN const UINT8						*pMacAddr,
	IN struct key_params				*pParams)
{
	VOID *pAd;
	CMD_RTPRIV_IOCTL_80211_KEY KeyInfo;


	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_KeyAdd ==>\n"));
	MAC80211_PAD_GET(pAd, pWiphy);

#ifdef RT_CFG80211_DEBUG
	hex_dump("KeyBuf=", (UINT8 *)pParams->key, pParams->key_len);
#endif /* RT_CFG80211_DEBUG */

	CFG80211DBG(RT_DEBUG_ERROR, ("80211> KeyIdx = %d\n", KeyIdx));

	if (pParams->key_len >= sizeof(KeyInfo.KeyBuf))
		return -EINVAL;
	/* End of if */

#ifdef CONFIG_STA_SUPPORT
	/* init */
	memset(&KeyInfo, 0, sizeof(KeyInfo));
	memcpy(KeyInfo.KeyBuf, pParams->key, pParams->key_len);
	KeyInfo.KeyBuf[pParams->key_len] = 0x00;

	if ((pParams->cipher == WLAN_CIPHER_SUITE_WEP40) ||
		(pParams->cipher == WLAN_CIPHER_SUITE_WEP104))
	{
		KeyInfo.KeyType = RT_CMD_80211_KEY_WEP;
	}
	else if ((pParams->cipher == WLAN_CIPHER_SUITE_TKIP) ||
		(pParams->cipher == WLAN_CIPHER_SUITE_CCMP))
	{
		KeyInfo.KeyType = RT_CMD_80211_KEY_WPA;
	}
	else
		return -ENOTSUPP;

	KeyInfo.KeyId = KeyIdx+1;

	/* add key */
	RTMP_DRIVER_80211_KEY_ADD(pAd, &KeyInfo);
	return 0;
#endif /* CONFIG_STA_SUPPORT */

} /* End of CFG80211_KeyAdd */


/*
========================================================================
Routine Description:
	Get information about the key with the given parameters.

Arguments:
	pWiphy			- Wireless hardware description
	pNdev			-
	KeyIdx			-
	pMacAddr		-
	pCookie			-
	pCallback		-

Return Value:
	0				- success
	-x				- fail

Note:
	pMacAddr will be NULL when requesting information for a group key.

	All pointers given to the pCallback function need not be valid after
	it returns.

	This function should return an error if it is not possible to
	retrieve the key, -ENOENT if it doesn't exist.
========================================================================
*/
static int CFG80211_KeyGet(
	IN struct wiphy						*pWiphy,
	IN struct net_device				*pNdev,
	IN UINT8							KeyIdx,
	IN const UINT8						*pMacAddr,
	IN void								*pCookie,
	IN void								(*pCallback)(void *cookie,
												 struct key_params *))
{

	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_KeyGet ==>\n"));
	return -ENOTSUPP;
} /* End of CFG80211_KeyGet */


/*
========================================================================
Routine Description:
	Remove a key given the pMacAddr (NULL for a group key) and KeyIdx.

Arguments:
	pWiphy			- Wireless hardware description
	pNdev			-
	KeyIdx			-
	pMacAddr		-

Return Value:
	0				- success
	-x				- fail

Note:
	return -ENOENT if the key doesn't exist.
========================================================================
*/
static int CFG80211_KeyDel(
	IN struct wiphy						*pWiphy,
	IN struct net_device				*pNdev,
	IN UINT8							KeyIdx,
	IN const UINT8						*pMacAddr)
{
	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_KeyDel ==>\n"));
	return -ENOTSUPP;
} /* End of CFG80211_KeyDel */


/*
========================================================================
Routine Description:
	Set the default key on an interface.

Arguments:
	pWiphy			- Wireless hardware description
	pNdev			-
	KeyIdx			-

Return Value:
	0				- success
	-x				- fail

Note:
========================================================================
*/
static int CFG80211_KeyDefaultSet(
	IN struct wiphy						*pWiphy,
	IN struct net_device				*pNdev,
	IN UINT8							KeyIdx)
{
	VOID *pAd;


	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_KeyDefaultSet ==>\n"));
	MAC80211_PAD_GET(pAd, pWiphy);

	CFG80211DBG(RT_DEBUG_ERROR, ("80211> KeyIdx = %d\n", KeyIdx));

	RTMP_DRIVER_80211_KEY_DEFAULT_SET(pAd, KeyIdx);
	return 0;
} /* End of CFG80211_KeyDefaultSet */


#ifdef CONFIG_STA_SUPPORT
/*
========================================================================
Routine Description:
	Connect to the ESS with the specified parameters. When connected,
	call cfg80211_connect_result() with status code %WLAN_STATUS_SUCCESS.
	If the connection fails for some reason, call cfg80211_connect_result()
	with the status from the AP.

Arguments:
	pWiphy			- Wireless hardware description
	pNdev			- Network device interface
	pSme			- 

Return Value:
	0				- success
	-x				- fail

Note:
	For iw utility: connect

	You must use "iw ra0 connect xxx", then "iw ra0 disconnect";
	You can not use "iw ra0 connect xxx" twice without disconnect;
	Or you will suffer "command failed: Operation already in progress (-114)".

	You must support add_key and set_default_key function;
	Or kernel will crash without any error message in linux 2.6.32.
========================================================================
*/
static int CFG80211_Connect(
	IN struct wiphy						*pWiphy,
	IN struct net_device				*pNdev,
	IN struct cfg80211_connect_params	*pSme)
{
	VOID *pAd;
	CMD_RTPRIV_IOCTL_80211_CONNECT ConnInfo;
	struct ieee80211_channel *pChannel = pSme->channel;
	INT32 Pairwise = 0;
	INT32 Groupwise = 0;
	INT32 Keymgmt = 0;
	INT32 WpaVersion = NL80211_WPA_VERSION_2;
	INT32 Chan = -1, Idx, SSIDLen;


	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_Connect ==>\n"));

	/* init */
	MAC80211_PAD_GET(pAd, pWiphy);

	if (pChannel != NULL)
		Chan = ieee80211_frequency_to_channel(pChannel->center_freq);

	Groupwise = pSme->crypto.cipher_group;
	for(Idx=0; Idx<pSme->crypto.n_ciphers_pairwise; Idx++)
		Pairwise |= pSme->crypto.ciphers_pairwise[Idx];
	/* End of for */

	for(Idx=0; Idx<pSme->crypto.n_akm_suites; Idx++)
		Keymgmt |= pSme->crypto.akm_suites[Idx];
	/* End of for */

	WpaVersion = pSme->crypto.wpa_versions;

	memset(&ConnInfo, 0, sizeof(ConnInfo));
	if (WpaVersion & NL80211_WPA_VERSION_2)
		ConnInfo.WpaVer = 2;
	else if (WpaVersion & NL80211_WPA_VERSION_1)
		ConnInfo.WpaVer = 1;
	else
		ConnInfo.WpaVer = 0;

	if (Keymgmt & WLAN_AKM_SUITE_8021X)
		ConnInfo.FlgIs8021x = TRUE;
	else
		ConnInfo.FlgIs8021x = FALSE;

	if (pSme->auth_type == NL80211_AUTHTYPE_SHARED_KEY)
		ConnInfo.FlgIsAuthOpen = FALSE;
	else
		ConnInfo.FlgIsAuthOpen = TRUE;

	if (Pairwise & WLAN_CIPHER_SUITE_CCMP)
		ConnInfo.PairwiseEncrypType |= RT_CMD_80211_CONN_ENCRYPT_CCMP;
	else if (Pairwise & WLAN_CIPHER_SUITE_TKIP)
		ConnInfo.PairwiseEncrypType |= RT_CMD_80211_CONN_ENCRYPT_TKIP;
	else if ((Pairwise & WLAN_CIPHER_SUITE_WEP40) ||
			(Pairwise & WLAN_CIPHER_SUITE_WEP104))
	{
		ConnInfo.PairwiseEncrypType |= RT_CMD_80211_CONN_ENCRYPT_WEP;
	}
	else
		ConnInfo.PairwiseEncrypType |= RT_CMD_80211_CONN_ENCRYPT_NONE;

	if (Groupwise & WLAN_CIPHER_SUITE_CCMP)
		ConnInfo.GroupwiseEncrypType |= RT_CMD_80211_CONN_ENCRYPT_CCMP;
	else if (Groupwise & WLAN_CIPHER_SUITE_TKIP)
		ConnInfo.GroupwiseEncrypType |= RT_CMD_80211_CONN_ENCRYPT_TKIP;
	else
		ConnInfo.GroupwiseEncrypType |= RT_CMD_80211_CONN_ENCRYPT_NONE;
	/* End of if */

	ConnInfo.pKey = pSme->key;
	ConnInfo.KeyLen = pSme->key_len;
	ConnInfo.pSsid = pSme->ssid;
	ConnInfo.SsidLen = pSme->ssid_len;

	CFG80211DBG(RT_DEBUG_ERROR,	("80211> SME %x\n",	pSme->auth_type));

	RTMP_DRIVER_80211_CONNECT(pAd, &ConnInfo);
	return 0;
} /* End of CFG80211_Connect */


/*
========================================================================
Routine Description:
	Disconnect from the BSS/ESS.

Arguments:
	pWiphy			- Wireless hardware description
	pNdev			- Network device interface
	ReasonCode		- 

Return Value:
	0				- success
	-x				- fail

Note:
	For iw utility: connect
========================================================================
*/
static int CFG80211_Disconnect(
	IN struct wiphy						*pWiphy,
	IN struct net_device				*pNdev,
	IN u16								ReasonCode)
{
	VOID *pAd;


	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_Disconnect ==>\n"));
	CFG80211DBG(RT_DEBUG_ERROR, ("80211> ReasonCode = %d\n", ReasonCode));

	MAC80211_PAD_GET(pAd, pWiphy);

	RTMP_DRIVER_80211_STA_LEAVE(pAd);
	return 0;
} /* End of CFG80211_Disconnect */
#endif /* CONFIG_STA_SUPPORT */
#endif /* LINUX_VERSION_CODE */


#ifdef RFKILL_HW_SUPPORT
static int CFG80211_RFKill(
	IN struct wiphy						*pWiphy)
{
	VOID		*pAd;
	BOOLEAN		active;


	MAC80211_PAD_GET(pAd, pWiphy);

	RTMP_DRIVER_80211_RFKILL(pAd, &active);
	wiphy_rfkill_set_hw_state(pWiphy, !active);	
	return active;
}

VOID CFG80211_RFKillStatusUpdate(
        IN PVOID	pAd,
        IN BOOLEAN	active)
{
        struct wiphy *pWiphy;
		CFG80211_CB *pCfg80211_CB;

		RTMP_DRIVER_80211_CB_GET(pAd, &pCfg80211_CB);
        pWiphy = pCfg80211_CB->pCfg80211_Wdev->wiphy;
        wiphy_rfkill_set_hw_state(pWiphy, !active);
        return;
}
#endif /* RFKILL_HW_SUPPORT */


struct cfg80211_ops CFG80211_Ops = {
	.set_channel			= CFG80211_OpsSetChannel,
	.change_virtual_intf	= CFG80211_OpsChgVirtualInf,

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30))
#ifdef CONFIG_STA_SUPPORT
	.scan					= CFG80211_OpsScan,
#endif /* CONFIG_STA_SUPPORT */
#endif /* LINUX_VERSION_CODE */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
#ifdef CONFIG_STA_SUPPORT
	.join_ibss				= CFG80211_OpsJoinIbss,
	.leave_ibss				= CFG80211_OpsLeaveIbss,
#endif /* CONFIG_STA_SUPPORT */
#endif /* LINUX_VERSION_CODE */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
	.set_tx_power			= CFG80211_TxPwrSet,
	.get_tx_power			= CFG80211_TxPwrGet,
	.set_power_mgmt			= CFG80211_PwrMgmt,
	.get_station			= CFG80211_StaGet,
	.dump_station			= CFG80211_StaDump,
	.set_wiphy_params		= CFG80211_WiphyParamsSet,
	.add_key				= CFG80211_KeyAdd,
	.get_key				= CFG80211_KeyGet,
	.del_key				= CFG80211_KeyDel,
	.set_default_key		= CFG80211_KeyDefaultSet,
#ifdef CONFIG_STA_SUPPORT
	.connect				= CFG80211_Connect,
	.disconnect				= CFG80211_Disconnect,
#endif /* CONFIG_STA_SUPPORT */
#endif /* LINUX_VERSION_CODE */
#ifdef RFKILL_HW_SUPPORT
	.rfkill_poll			= CFG80211_RFKill,
#endif /* RFKILL_HW_SUPPORT */
};




/* =========================== Global Function ============================== */

/*
========================================================================
Routine Description:
	Allocate a wireless device.

Arguments:
	pAd				- WLAN control block pointer
	pDev			- Generic device interface

Return Value:
	wireless device

Note:
========================================================================
*/
static struct wireless_dev *CFG80211_WdevAlloc(
	IN CFG80211_CB					*pCfg80211_CB,
	IN CFG80211_BAND				*pBandInfo,
	IN VOID 						*pAd,
	IN struct device				*pDev)
{
	struct wireless_dev *pWdev;
	ULONG *pPriv;


	/*
	 * We're trying to have the following memory layout:
	 *
	 * +------------------------+
	 * | struct wiphy			|
	 * +------------------------+
	 * | pAd pointer			|
	 * +------------------------+
	 */

	pWdev = kzalloc(sizeof(struct wireless_dev), GFP_KERNEL);
	if (pWdev == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("80211> Wireless device allocation fail!\n"));
		return NULL;
	} /* End of if */

	pWdev->wiphy = wiphy_new(&CFG80211_Ops, sizeof(ULONG *));
	if (pWdev->wiphy == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("80211> Wiphy device allocation fail!\n"));
		goto LabelErrWiphyNew;
	} /* End of if */

	/* keep pAd pointer */
	pPriv = (ULONG *)(wiphy_priv(pWdev->wiphy));
	*pPriv = (ULONG)pAd;

	set_wiphy_dev(pWdev->wiphy, pDev);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30))
	pWdev->wiphy->max_scan_ssids = pBandInfo->MaxBssTable;
#endif /* KERNEL_VERSION */


#ifdef CONFIG_STA_SUPPORT
	pWdev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
							       BIT(NL80211_IFTYPE_ADHOC) |
							       BIT(NL80211_IFTYPE_MONITOR);
#endif /* CONFIG_STA_SUPPORT */
	pWdev->wiphy->reg_notifier = CFG80211_RegNotifier;

	/* init channel information */
	CFG80211_SupBandInit(pCfg80211_CB, pBandInfo, pWdev->wiphy, NULL, NULL);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30))
	/* CFG80211_SIGNAL_TYPE_MBM: signal strength in mBm (100*dBm) */
	pWdev->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
#endif /* KERNEL_VERSION */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
	pWdev->wiphy->cipher_suites = CipherSuites;
	pWdev->wiphy->n_cipher_suites = ARRAY_SIZE(CipherSuites);
#endif /* LINUX_VERSION_CODE */

	if (wiphy_register(pWdev->wiphy) < 0)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("80211> Register wiphy device fail!\n"));
		goto LabelErrReg;
	} /* End of if */

	return pWdev;

 LabelErrReg:
	wiphy_free(pWdev->wiphy);

 LabelErrWiphyNew:
	os_free_mem(NULL, pWdev);

	return NULL;
} /* End of CFG80211_WdevAlloc */


/*
========================================================================
Routine Description:
	Register MAC80211 Module.

Arguments:
	pAdCB			- WLAN control block pointer
	pDev			- Generic device interface
	pNetDev			- Network device

Return Value:
	NONE

Note:
	pDev != pNetDev
	#define SET_NETDEV_DEV(net, pdev)	((net)->dev.parent = (pdev))

	Can not use pNetDev to replace pDev; Or kernel panic.
========================================================================
*/
BOOLEAN CFG80211_Register(
	IN VOID						*pAd,
	IN struct device			*pDev,
	IN struct net_device		*pNetDev)
{
	CFG80211_CB *pCfg80211_CB = NULL;
	CFG80211_BAND BandInfo;


	/* allocate MAC80211 structure */
	os_alloc_mem(NULL, (UCHAR **)&pCfg80211_CB, sizeof(CFG80211_CB));
	if (pCfg80211_CB == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("80211> Allocate MAC80211 CB fail!\n"));
		return FALSE;
	} /* End of if */

	/* allocate wireless device */
	RTMP_DRIVER_80211_BANDINFO_GET(pAd, &BandInfo);

	pCfg80211_CB->pCfg80211_Wdev = \
				CFG80211_WdevAlloc(pCfg80211_CB, &BandInfo, pAd, pDev);
	if (pCfg80211_CB->pCfg80211_Wdev == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("80211> Allocate Wdev fail!\n"));
		os_free_mem(NULL, pCfg80211_CB);
		return FALSE;
	} /* End of if */

	/* bind wireless device with net device */

#ifdef CONFIG_STA_SUPPORT
	/* default we are station mode */
	pCfg80211_CB->pCfg80211_Wdev->iftype = NL80211_IFTYPE_STATION;
#endif /* CONFIG_STA_SUPPORT */

	pNetDev->ieee80211_ptr = pCfg80211_CB->pCfg80211_Wdev;
	SET_NETDEV_DEV(pNetDev, wiphy_dev(pCfg80211_CB->pCfg80211_Wdev->wiphy));
	pCfg80211_CB->pCfg80211_Wdev->netdev = pNetDev;

#ifdef RFKILL_HW_SUPPORT
	wiphy_rfkill_start_polling(pCfg80211_CB->pCfg80211_Wdev->wiphy);
#endif /* RFKILL_HW_SUPPORT */

	RTMP_DRIVER_80211_CB_SET(pAd, pCfg80211_CB);

	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_Register\n"));
	return TRUE;
} /* End of CFG80211_Register */




/* =========================== Local Function =============================== */

/*
========================================================================
Routine Description:
	The driver's regulatory notification callback.

Arguments:
	pWiphy			- Wireless hardware description
	pRequest		- Regulatory request

Return Value:
	0

Note:
========================================================================
*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30))
static INT32 CFG80211_RegNotifier(
	IN struct wiphy					*pWiphy,
	IN struct regulatory_request	*pRequest)
{
	VOID *pAd;
	ULONG *pPriv;


	/* sanity check */
	pPriv = (ULONG *)(wiphy_priv(pWiphy));
	pAd = (VOID *)(*pPriv);

	if (pAd == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("crda> reg notify but pAd = NULL!"));
		return 0;
	} /* End of if */

	/*
		Change the band settings (PASS scan, IBSS allow, or DFS) in mac80211
		based on EEPROM.

		IEEE80211_CHAN_DISABLED: This channel is disabled.
		IEEE80211_CHAN_PASSIVE_SCAN: Only passive scanning is permitted
					on this channel.
		IEEE80211_CHAN_NO_IBSS: IBSS is not allowed on this channel.
		IEEE80211_CHAN_RADAR: Radar detection is required on this channel.
		IEEE80211_CHAN_NO_FAT_ABOVE: extension channel above this channel
					is not permitted.
		IEEE80211_CHAN_NO_FAT_BELOW: extension channel below this channel
					is not permitted.
	*/

	/*
		Change regulatory rule here.

		struct ieee80211_channel {
			enum ieee80211_band band;
			u16 center_freq;
			u8 max_bandwidth;
			u16 hw_value;
			u32 flags;
			int max_antenna_gain;
			int max_power;
			bool beacon_found;
			u32 orig_flags;
			int orig_mag, orig_mpwr;
		};

		In mac80211 layer, it will change flags, max_antenna_gain,
		max_bandwidth, max_power.
	*/

	switch(pRequest->initiator)
	{
		case NL80211_REGDOM_SET_BY_CORE:
			/*
				Core queried CRDA for a dynamic world regulatory domain.
			*/
			CFG80211DBG(RT_DEBUG_ERROR, ("crda> requlation requestion by core: "));
			break;

		case NL80211_REGDOM_SET_BY_USER:
			/*
				User asked the wireless core to set the regulatory domain.
				(when iw, network manager, wpa supplicant, etc.)
			*/
			CFG80211DBG(RT_DEBUG_ERROR, ("crda> requlation requestion by user: "));
			break;

		case NL80211_REGDOM_SET_BY_DRIVER:
			/*
				A wireless drivers has hinted to the wireless core it thinks
				its knows the regulatory domain we should be in.
				(when driver initialization, calling regulatory_hint)
			*/
			CFG80211DBG(RT_DEBUG_ERROR, ("crda> requlation requestion by driver: "));
			break;

		case NL80211_REGDOM_SET_BY_COUNTRY_IE:
			/*
				The wireless core has received an 802.11 country information
				element with regulatory information it thinks we should consider.
				(when beacon receive, calling regulatory_hint_11d)
			*/
			CFG80211DBG(RT_DEBUG_ERROR, ("crda> requlation requestion by country IE: "));
			break;
	} /* End of switch */

	CFG80211DBG(RT_DEBUG_ERROR,
				("%c%c\n", pRequest->alpha2[0], pRequest->alpha2[1]));

	/* only follow rules from user */
	if (pRequest->initiator == NL80211_REGDOM_SET_BY_USER)
	{
		/* keep Alpha2 and we can re-call the function when interface is up */
		CMD_RTPRIV_IOCTL_80211_REG_NOTIFY RegInfo;

		RegInfo.Alpha2[0] = pRequest->alpha2[0];
		RegInfo.Alpha2[1] = pRequest->alpha2[1];
		RegInfo.pWiphy = pWiphy;

		RTMP_DRIVER_80211_REG_NOTIFY(pAd, &RegInfo);
	} /* End of if */

	return 0;
} /* End of CFG80211_RegNotifier */

#else

static INT32 CFG80211_RegNotifier(
	IN struct wiphy					*pWiphy,
	IN enum reg_set_by				Request)
{
	struct device *pDev = pWiphy->dev.parent;
	struct net_device *pNetDev = dev_get_drvdata(pDev);
	VOID *pAd = (VOID *)RTMP_OS_NETDEV_GET_PRIV(pNetDev);
	UINT32 ReqType = Request;


	/* sanity check */
	if (pAd == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("crda> reg notify but pAd = NULL!"));
		return 0;
	} /* End of if */

	/*
		Change the band settings (PASS scan, IBSS allow, or DFS) in mac80211
		based on EEPROM.

		IEEE80211_CHAN_DISABLED: This channel is disabled.
		IEEE80211_CHAN_PASSIVE_SCAN: Only passive scanning is permitted
					on this channel.
		IEEE80211_CHAN_NO_IBSS: IBSS is not allowed on this channel.
		IEEE80211_CHAN_RADAR: Radar detection is required on this channel.
		IEEE80211_CHAN_NO_FAT_ABOVE: extension channel above this channel
					is not permitted.
		IEEE80211_CHAN_NO_FAT_BELOW: extension channel below this channel
					is not permitted.
	*/

	/*
		Change regulatory rule here.

		struct ieee80211_channel {
			enum ieee80211_band band;
			u16 center_freq;
			u8 max_bandwidth;
			u16 hw_value;
			u32 flags;
			int max_antenna_gain;
			int max_power;
			bool beacon_found;
			u32 orig_flags;
			int orig_mag, orig_mpwr;
		};

		In mac80211 layer, it will change flags, max_antenna_gain,
		max_bandwidth, max_power.
	*/

	switch(ReqType)
	{
		case REGDOM_SET_BY_CORE:
			/*
				Core queried CRDA for a dynamic world regulatory domain.
			*/
			CFG80211DBG(RT_DEBUG_ERROR, ("crda> requlation requestion by core: "));
			break;

		case REGDOM_SET_BY_USER:
			/*
				User asked the wireless core to set the regulatory domain.
				(when iw, network manager, wpa supplicant, etc.)
			*/
			CFG80211DBG(RT_DEBUG_ERROR, ("crda> requlation requestion by user: "));
			break;

		case REGDOM_SET_BY_DRIVER:
			/*
				A wireless drivers has hinted to the wireless core it thinks
				its knows the regulatory domain we should be in.
				(when driver initialization, calling regulatory_hint)
			*/
			CFG80211DBG(RT_DEBUG_ERROR, ("crda> requlation requestion by driver: "));
			break;

		case REGDOM_SET_BY_COUNTRY_IE:
			/*
				The wireless core has received an 802.11 country information
				element with regulatory information it thinks we should consider.
				(when beacon receive, calling regulatory_hint_11d)
			*/
			CFG80211DBG(RT_DEBUG_ERROR, ("crda> requlation requestion by country IE: "));
			break;
	} /* End of switch */

	DBGPRINT(RT_DEBUG_ERROR, ("00\n"));

	/* only follow rules from user */
	if (ReqType == REGDOM_SET_BY_USER)
	{
		/* keep Alpha2 and we can re-call the function when interface is up */
		CMD_RTPRIV_IOCTL_80211_REG_NOTIFY RegInfo;

		RegInfo.Alpha2[0] = '0';
		RegInfo.Alpha2[1] = '0';
		RegInfo.pWiphy = pWiphy;

		RTMP_DRIVER_80211_REG_NOTIFY(pAd, &RegInfo);
	} /* End of if */

	return 0;
} /* End of CFG80211_RegNotifier */
#endif /* LINUX_VERSION_CODE */


#endif /* RT_CFG80211_SUPPORT */
#endif /* LINUX_VERSION_CODE */

/* End of crda.c */
