/*!
 *  @file	wilc_wfi_cfgopertaions.c
 *  @brief	CFG80211 Function Implementation functionality
 *  @author	aabouzaeid
 *                      mabubakr
 *                      mdaftedar
 *                      zsalah
 *  @sa		wilc_wfi_cfgopertaions.h top level OS wrapper file
 *  @date	31 Aug 2010
 *  @version	1.0
 */

#include "wilc_wfi_cfgoperations.h"
#ifdef WILC_SDIO
#include "linux_wlan_sdio.h"
#endif
#include <linux/errno.h>

#define IS_MANAGMEMENT				0x100
#define IS_MANAGMEMENT_CALLBACK			0x080
#define IS_MGMT_STATUS_SUCCES			0x040
#define GET_PKT_OFFSET(a) (((a) >> 22) & 0x1ff)

extern int linux_wlan_get_firmware(perInterface_wlan_t *p_nic);

extern int mac_open(struct net_device *ndev);
extern int mac_close(struct net_device *ndev);

tstrNetworkInfo astrLastScannedNtwrksShadow[MAX_NUM_SCANNED_NETWORKS_SHADOW];
u32 u32LastScannedNtwrksCountShadow;
struct timer_list hDuringIpTimer;
struct timer_list hAgingTimer;
static u8 op_ifcs;
extern u8 u8ConnectedSSID[6];

u8 g_wilc_initialized = 1;
extern bool g_obtainingIP;

#define CHAN2G(_channel, _freq, _flags) {	 \
		.band             = IEEE80211_BAND_2GHZ, \
		.center_freq      = (_freq),		 \
		.hw_value         = (_channel),		 \
		.flags            = (_flags),		 \
		.max_antenna_gain = 0,			 \
		.max_power        = 30,			 \
}

/*Frequency range for channels*/
static struct ieee80211_channel WILC_WFI_2ghz_channels[] = {
	CHAN2G(1,  2412, 0),
	CHAN2G(2,  2417, 0),
	CHAN2G(3,  2422, 0),
	CHAN2G(4,  2427, 0),
	CHAN2G(5,  2432, 0),
	CHAN2G(6,  2437, 0),
	CHAN2G(7,  2442, 0),
	CHAN2G(8,  2447, 0),
	CHAN2G(9,  2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0),
};

#define RATETAB_ENT(_rate, _hw_value, _flags) {	\
		.bitrate  = (_rate),			\
		.hw_value = (_hw_value),		\
		.flags    = (_flags),			\
}


/* Table 6 in section 3.2.1.1 */
static struct ieee80211_rate WILC_WFI_rates[] = {
	RATETAB_ENT(10,  0,  0),
	RATETAB_ENT(20,  1,  0),
	RATETAB_ENT(55,  2,  0),
	RATETAB_ENT(110, 3,  0),
	RATETAB_ENT(60,  9,  0),
	RATETAB_ENT(90,  6,  0),
	RATETAB_ENT(120, 7,  0),
	RATETAB_ENT(180, 8,  0),
	RATETAB_ENT(240, 9,  0),
	RATETAB_ENT(360, 10, 0),
	RATETAB_ENT(480, 11, 0),
	RATETAB_ENT(540, 12, 0),
};

struct p2p_mgmt_data {
	int size;
	u8 *buff;
};

/*Global variable used to state the current  connected STA channel*/
u8 u8WLANChannel = INVALID_CHANNEL;

u8 curr_channel;

u8 u8P2P_oui[] = {0x50, 0x6f, 0x9A, 0x09};
u8 u8P2Plocalrandom = 0x01;
u8 u8P2Precvrandom = 0x00;
u8 u8P2P_vendorspec[] = {0xdd, 0x05, 0x00, 0x08, 0x40, 0x03};
bool bWilc_ie;

static struct ieee80211_supported_band WILC_WFI_band_2ghz = {
	.channels = WILC_WFI_2ghz_channels,
	.n_channels = ARRAY_SIZE(WILC_WFI_2ghz_channels),
	.bitrates = WILC_WFI_rates,
	.n_bitrates = ARRAY_SIZE(WILC_WFI_rates),
};


struct add_key_params {
	u8 key_idx;
	bool pairwise;
	u8 *mac_addr;
};
struct add_key_params g_add_gtk_key_params;
struct wilc_wfi_key g_key_gtk_params;
struct add_key_params g_add_ptk_key_params;
struct wilc_wfi_key g_key_ptk_params;
struct wilc_wfi_wep_key g_key_wep_params;
bool g_ptk_keys_saved;
bool g_gtk_keys_saved;
bool g_wep_keys_saved;

#define AGING_TIME	(9 * 1000)
#define duringIP_TIME 15000

void clear_shadow_scan(void *pUserVoid)
{
	int i;

	if (op_ifcs == 0) {
		del_timer_sync(&hAgingTimer);
		PRINT_INFO(CORECONFIG_DBG, "destroy aging timer\n");

		for (i = 0; i < u32LastScannedNtwrksCountShadow; i++) {
			if (astrLastScannedNtwrksShadow[u32LastScannedNtwrksCountShadow].pu8IEs != NULL) {
				kfree(astrLastScannedNtwrksShadow[i].pu8IEs);
				astrLastScannedNtwrksShadow[u32LastScannedNtwrksCountShadow].pu8IEs = NULL;
			}

			host_int_freeJoinParams(astrLastScannedNtwrksShadow[i].pJoinParams);
			astrLastScannedNtwrksShadow[i].pJoinParams = NULL;
		}
		u32LastScannedNtwrksCountShadow = 0;
	}

}

u32 get_rssi_avg(tstrNetworkInfo *pstrNetworkInfo)
{
	u8 i;
	int rssi_v = 0;
	u8 num_rssi = (pstrNetworkInfo->strRssi.u8Full) ? NUM_RSSI : (pstrNetworkInfo->strRssi.u8Index);

	for (i = 0; i < num_rssi; i++)
		rssi_v += pstrNetworkInfo->strRssi.as8RSSI[i];

	rssi_v /= num_rssi;
	return rssi_v;
}

void refresh_scan(void *pUserVoid, u8 all, bool bDirectScan)
{
	struct wilc_priv *priv;
	struct wiphy *wiphy;
	struct cfg80211_bss *bss = NULL;
	int i;
	int rssi = 0;

	priv = (struct wilc_priv *)pUserVoid;
	wiphy = priv->dev->ieee80211_ptr->wiphy;

	for (i = 0; i < u32LastScannedNtwrksCountShadow; i++) {
		tstrNetworkInfo *pstrNetworkInfo;

		pstrNetworkInfo = &(astrLastScannedNtwrksShadow[i]);


		if ((!pstrNetworkInfo->u8Found) || all) {
			s32 s32Freq;
			struct ieee80211_channel *channel;

			if (pstrNetworkInfo != NULL) {

				s32Freq = ieee80211_channel_to_frequency((s32)pstrNetworkInfo->u8channel, IEEE80211_BAND_2GHZ);
				channel = ieee80211_get_channel(wiphy, s32Freq);

				rssi = get_rssi_avg(pstrNetworkInfo);
				if (memcmp("DIRECT-", pstrNetworkInfo->au8ssid, 7) || bDirectScan)	{
					bss = cfg80211_inform_bss(wiphy, channel, CFG80211_BSS_FTYPE_UNKNOWN, pstrNetworkInfo->au8bssid, pstrNetworkInfo->u64Tsf, pstrNetworkInfo->u16CapInfo,
								  pstrNetworkInfo->u16BeaconPeriod, (const u8 *)pstrNetworkInfo->pu8IEs,
								  (size_t)pstrNetworkInfo->u16IEsLen, (((s32)rssi) * 100), GFP_KERNEL);
					cfg80211_put_bss(wiphy, bss);
				}
			}

		}
	}

}

void reset_shadow_found(void *pUserVoid)
{
	int i;

	for (i = 0; i < u32LastScannedNtwrksCountShadow; i++) {
		astrLastScannedNtwrksShadow[i].u8Found = 0;

	}
}

void update_scan_time(void *pUserVoid)
{
	int i;

	for (i = 0; i < u32LastScannedNtwrksCountShadow; i++) {
		astrLastScannedNtwrksShadow[i].u32TimeRcvdInScan = jiffies;
	}
}

static void remove_network_from_shadow(unsigned long arg)
{
	unsigned long now = jiffies;
	int i, j;


	for (i = 0; i < u32LastScannedNtwrksCountShadow; i++) {
		if (time_after(now, astrLastScannedNtwrksShadow[i].u32TimeRcvdInScan + (unsigned long)(SCAN_RESULT_EXPIRE))) {
			PRINT_D(CFG80211_DBG, "Network expired in ScanShadow: %s\n", astrLastScannedNtwrksShadow[i].au8ssid);

			kfree(astrLastScannedNtwrksShadow[i].pu8IEs);
			astrLastScannedNtwrksShadow[i].pu8IEs = NULL;

			host_int_freeJoinParams(astrLastScannedNtwrksShadow[i].pJoinParams);

			for (j = i; (j < u32LastScannedNtwrksCountShadow - 1); j++) {
				astrLastScannedNtwrksShadow[j] = astrLastScannedNtwrksShadow[j + 1];
			}
			u32LastScannedNtwrksCountShadow--;
		}
	}

	PRINT_D(CFG80211_DBG, "Number of cached networks: %d\n", u32LastScannedNtwrksCountShadow);
	if (u32LastScannedNtwrksCountShadow != 0) {
		hAgingTimer.data = arg;
		mod_timer(&hAgingTimer, jiffies + msecs_to_jiffies(AGING_TIME));
	} else {
		PRINT_D(CFG80211_DBG, "No need to restart Aging timer\n");
	}
}

static void clear_duringIP(unsigned long arg)
{
	PRINT_D(GENERIC_DBG, "GO:IP Obtained , enable scan\n");
	g_obtainingIP = false;
}

int is_network_in_shadow(tstrNetworkInfo *pstrNetworkInfo, void *pUserVoid)
{
	int state = -1;
	int i;

	if (u32LastScannedNtwrksCountShadow == 0) {
		PRINT_D(CFG80211_DBG, "Starting Aging timer\n");
		hAgingTimer.data = (unsigned long)pUserVoid;
		mod_timer(&hAgingTimer, jiffies + msecs_to_jiffies(AGING_TIME));
		state = -1;
	} else {
		/* Linear search for now */
		for (i = 0; i < u32LastScannedNtwrksCountShadow; i++) {
			if (memcmp(astrLastScannedNtwrksShadow[i].au8bssid,
					pstrNetworkInfo->au8bssid, 6) == 0) {
				state = i;
				break;
			}
		}
	}
	return state;
}

void add_network_to_shadow(tstrNetworkInfo *pstrNetworkInfo, void *pUserVoid, void *pJoinParams)
{
	int ap_found = is_network_in_shadow(pstrNetworkInfo, pUserVoid);
	u32 ap_index = 0;
	u8 rssi_index = 0;

	if (u32LastScannedNtwrksCountShadow >= MAX_NUM_SCANNED_NETWORKS_SHADOW) {
		PRINT_D(CFG80211_DBG, "Shadow network reached its maximum limit\n");
		return;
	}
	if (ap_found == -1) {
		ap_index = u32LastScannedNtwrksCountShadow;
		u32LastScannedNtwrksCountShadow++;

	} else {
		ap_index = ap_found;
	}
	rssi_index = astrLastScannedNtwrksShadow[ap_index].strRssi.u8Index;
	astrLastScannedNtwrksShadow[ap_index].strRssi.as8RSSI[rssi_index++] = pstrNetworkInfo->s8rssi;
	if (rssi_index == NUM_RSSI) {
		rssi_index = 0;
		astrLastScannedNtwrksShadow[ap_index].strRssi.u8Full = 1;
	}
	astrLastScannedNtwrksShadow[ap_index].strRssi.u8Index = rssi_index;

	astrLastScannedNtwrksShadow[ap_index].s8rssi = pstrNetworkInfo->s8rssi;
	astrLastScannedNtwrksShadow[ap_index].u16CapInfo = pstrNetworkInfo->u16CapInfo;

	astrLastScannedNtwrksShadow[ap_index].u8SsidLen = pstrNetworkInfo->u8SsidLen;
	memcpy(astrLastScannedNtwrksShadow[ap_index].au8ssid,
		    pstrNetworkInfo->au8ssid, pstrNetworkInfo->u8SsidLen);

	memcpy(astrLastScannedNtwrksShadow[ap_index].au8bssid,
		    pstrNetworkInfo->au8bssid, ETH_ALEN);

	astrLastScannedNtwrksShadow[ap_index].u16BeaconPeriod = pstrNetworkInfo->u16BeaconPeriod;
	astrLastScannedNtwrksShadow[ap_index].u8DtimPeriod = pstrNetworkInfo->u8DtimPeriod;
	astrLastScannedNtwrksShadow[ap_index].u8channel = pstrNetworkInfo->u8channel;

	astrLastScannedNtwrksShadow[ap_index].u16IEsLen = pstrNetworkInfo->u16IEsLen;
	astrLastScannedNtwrksShadow[ap_index].u64Tsf = pstrNetworkInfo->u64Tsf;
	if (ap_found != -1)
		kfree(astrLastScannedNtwrksShadow[ap_index].pu8IEs);
	astrLastScannedNtwrksShadow[ap_index].pu8IEs =
		kmalloc(pstrNetworkInfo->u16IEsLen, GFP_KERNEL);        /* will be deallocated by the WILC_WFI_CfgScan() function */
	memcpy(astrLastScannedNtwrksShadow[ap_index].pu8IEs,
		    pstrNetworkInfo->pu8IEs, pstrNetworkInfo->u16IEsLen);

	astrLastScannedNtwrksShadow[ap_index].u32TimeRcvdInScan = jiffies;
	astrLastScannedNtwrksShadow[ap_index].u32TimeRcvdInScanCached = jiffies;
	astrLastScannedNtwrksShadow[ap_index].u8Found = 1;
	if (ap_found != -1)
		host_int_freeJoinParams(astrLastScannedNtwrksShadow[ap_index].pJoinParams);
	astrLastScannedNtwrksShadow[ap_index].pJoinParams = pJoinParams;

}


/**
 *  @brief      CfgScanResult
 *  @details  Callback function which returns the scan results found
 *
 *  @param[in] tenuScanEvent enuScanEvent: enum, indicating the scan event triggered, whether that is
 *                        SCAN_EVENT_NETWORK_FOUND or SCAN_EVENT_DONE
 *                        tstrNetworkInfo* pstrNetworkInfo: structure holding the scan results information
 *                        void* pUserVoid: Private structure associated with the wireless interface
 *  @return     NONE
 *  @author	mabubakr
 *  @date
 *  @version	1.0
 */
static void CfgScanResult(enum scan_event enuScanEvent, tstrNetworkInfo *pstrNetworkInfo, void *pUserVoid, void *pJoinParams)
{
	struct wilc_priv *priv;
	struct wiphy *wiphy;
	s32 s32Freq;
	struct ieee80211_channel *channel;
	struct cfg80211_bss *bss = NULL;

	priv = (struct wilc_priv *)pUserVoid;
	if (priv->bCfgScanning) {
		if (enuScanEvent == SCAN_EVENT_NETWORK_FOUND) {
			wiphy = priv->dev->ieee80211_ptr->wiphy;

			if (!wiphy)
				return;

			if (wiphy->signal_type == CFG80211_SIGNAL_TYPE_UNSPEC
			    &&
			    ((((s32)pstrNetworkInfo->s8rssi) * 100) < 0
			     ||
			     (((s32)pstrNetworkInfo->s8rssi) * 100) > 100)
			    ) {
				PRINT_ER("wiphy signal type fial\n");
				return;
			}

			if (pstrNetworkInfo != NULL) {
				s32Freq = ieee80211_channel_to_frequency((s32)pstrNetworkInfo->u8channel, IEEE80211_BAND_2GHZ);
				channel = ieee80211_get_channel(wiphy, s32Freq);

				if (!channel)
					return;

				PRINT_INFO(CFG80211_DBG, "Network Info:: CHANNEL Frequency: %d, RSSI: %d, CapabilityInfo: %d,"
					   "BeaconPeriod: %d\n", channel->center_freq, (((s32)pstrNetworkInfo->s8rssi) * 100),
					   pstrNetworkInfo->u16CapInfo, pstrNetworkInfo->u16BeaconPeriod);

				if (pstrNetworkInfo->bNewNetwork) {
					if (priv->u32RcvdChCount < MAX_NUM_SCANNED_NETWORKS) { /* TODO: mostafa: to be replaced by */
						/*               max_scan_ssids */
						PRINT_D(CFG80211_DBG, "Network %s found\n", pstrNetworkInfo->au8ssid);


						priv->u32RcvdChCount++;



						if (pJoinParams == NULL) {
							PRINT_INFO(CORECONFIG_DBG, ">> Something really bad happened\n");
						}
						add_network_to_shadow(pstrNetworkInfo, priv, pJoinParams);

						/*P2P peers are sent to WPA supplicant and added to shadow table*/

						if (!(memcmp("DIRECT-", pstrNetworkInfo->au8ssid, 7))) {
							bss = cfg80211_inform_bss(wiphy, channel, CFG80211_BSS_FTYPE_UNKNOWN,  pstrNetworkInfo->au8bssid, pstrNetworkInfo->u64Tsf, pstrNetworkInfo->u16CapInfo,
										  pstrNetworkInfo->u16BeaconPeriod, (const u8 *)pstrNetworkInfo->pu8IEs,
										  (size_t)pstrNetworkInfo->u16IEsLen, (((s32)pstrNetworkInfo->s8rssi) * 100), GFP_KERNEL);
							cfg80211_put_bss(wiphy, bss);
						}


					} else {
						PRINT_ER("Discovered networks exceeded the max limit\n");
					}
				} else {
					u32 i;
					/* So this network is discovered before, we'll just update its RSSI */
					for (i = 0; i < priv->u32RcvdChCount; i++) {
						if (memcmp(astrLastScannedNtwrksShadow[i].au8bssid, pstrNetworkInfo->au8bssid, 6) == 0) {
							PRINT_D(CFG80211_DBG, "Update RSSI of %s\n", astrLastScannedNtwrksShadow[i].au8ssid);

							astrLastScannedNtwrksShadow[i].s8rssi = pstrNetworkInfo->s8rssi;
							astrLastScannedNtwrksShadow[i].u32TimeRcvdInScan = jiffies;
							break;
						}
					}
				}
			}
		} else if (enuScanEvent == SCAN_EVENT_DONE)    {
			PRINT_D(CFG80211_DBG, "Scan Done[%p]\n", priv->dev);
			PRINT_D(CFG80211_DBG, "Refreshing Scan ...\n");
			refresh_scan(priv, 1, false);

			if (priv->u32RcvdChCount > 0)
				PRINT_D(CFG80211_DBG, "%d Network(s) found\n", priv->u32RcvdChCount);
			else
				PRINT_D(CFG80211_DBG, "No networks found\n");

			down(&(priv->hSemScanReq));

			if (priv->pstrScanReq != NULL) {
				cfg80211_scan_done(priv->pstrScanReq, false);
				priv->u32RcvdChCount = 0;
				priv->bCfgScanning = false;
				priv->pstrScanReq = NULL;
			}
			up(&(priv->hSemScanReq));

		}
		/*Aborting any scan operation during mac close*/
		else if (enuScanEvent == SCAN_EVENT_ABORTED) {
			down(&(priv->hSemScanReq));

			PRINT_D(CFG80211_DBG, "Scan Aborted\n");
			if (priv->pstrScanReq != NULL) {

				update_scan_time(priv);
				refresh_scan(priv, 1, false);

				cfg80211_scan_done(priv->pstrScanReq, false);
				priv->bCfgScanning = false;
				priv->pstrScanReq = NULL;
			}
			up(&(priv->hSemScanReq));
		}
	}
}


/**
 *  @brief      WILC_WFI_Set_PMKSA
 *  @details  Check if pmksa is cached and set it.
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
int WILC_WFI_Set_PMKSA(u8 *bssid, struct wilc_priv *priv)
{
	u32 i;
	s32 s32Error = 0;


	for (i = 0; i < priv->pmkid_list.numpmkid; i++)	{

		if (!memcmp(bssid, priv->pmkid_list.pmkidlist[i].bssid,
				 ETH_ALEN)) {
			PRINT_D(CFG80211_DBG, "PMKID successful comparison");

			/*If bssid is found, set the values*/
			s32Error = host_int_set_pmkid_info(priv->hWILCWFIDrv, &priv->pmkid_list);

			if (s32Error != 0)
				PRINT_ER("Error in pmkid\n");

			break;
		}
	}

	return s32Error;


}
int linux_wlan_set_bssid(struct net_device *wilc_netdev, u8 *pBSSID);


/**
 *  @brief      CfgConnectResult
 *  @details
 *  @param[in] tenuConnDisconnEvent enuConnDisconnEvent: Type of connection response either
 *                        connection response or disconnection notification.
 *                        tstrConnectInfo* pstrConnectInfo: COnnection information.
 *                        u8 u8MacStatus: Mac Status from firmware
 *                        tstrDisconnectNotifInfo* pstrDisconnectNotifInfo: Disconnection Notification
 *                        void* pUserVoid: Private data associated with wireless interface
 *  @return     NONE
 *  @author	mabubakr
 *  @date	01 MAR 2012
 *  @version	1.0
 */
int connecting;

static void CfgConnectResult(enum conn_event enuConnDisconnEvent,
			     tstrConnectInfo *pstrConnectInfo,
			     u8 u8MacStatus,
			     tstrDisconnectNotifInfo *pstrDisconnectNotifInfo,
			     void *pUserVoid)
{
	struct wilc_priv *priv;
	struct net_device *dev;
	struct host_if_drv *pstrWFIDrv;
	u8 NullBssid[ETH_ALEN] = {0};
	struct wilc *wl;
	perInterface_wlan_t *nic;

	connecting = 0;

	priv = (struct wilc_priv *)pUserVoid;
	dev = priv->dev;
	nic = netdev_priv(dev);
	wl = nic->wilc;
	pstrWFIDrv = (struct host_if_drv *)priv->hWILCWFIDrv;

	if (enuConnDisconnEvent == CONN_DISCONN_EVENT_CONN_RESP) {
		/*Initialization*/
		u16 u16ConnectStatus;

		u16ConnectStatus = pstrConnectInfo->u16ConnectStatus;

		PRINT_D(CFG80211_DBG, " Connection response received = %d\n", u8MacStatus);

		if ((u8MacStatus == MAC_DISCONNECTED) &&
		    (pstrConnectInfo->u16ConnectStatus == SUCCESSFUL_STATUSCODE)) {
			/* The case here is that our station was waiting for association response frame and has just received it containing status code
			 *  = SUCCESSFUL_STATUSCODE, while mac status is MAC_DISCONNECTED (which means something wrong happened) */
			u16ConnectStatus = WLAN_STATUS_UNSPECIFIED_FAILURE;
			linux_wlan_set_bssid(priv->dev, NullBssid);
			eth_zero_addr(u8ConnectedSSID);

			/*Invalidate u8WLANChannel value on wlan0 disconnect*/
			if (!pstrWFIDrv->u8P2PConnect)
				u8WLANChannel = INVALID_CHANNEL;

			PRINT_ER("Unspecified failure: Connection status %d : MAC status = %d\n", u16ConnectStatus, u8MacStatus);
		}

		if (u16ConnectStatus == WLAN_STATUS_SUCCESS) {
			bool bNeedScanRefresh = false;
			u32 i;

			PRINT_INFO(CFG80211_DBG, "Connection Successful:: BSSID: %x%x%x%x%x%x\n", pstrConnectInfo->au8bssid[0],
				   pstrConnectInfo->au8bssid[1], pstrConnectInfo->au8bssid[2], pstrConnectInfo->au8bssid[3], pstrConnectInfo->au8bssid[4], pstrConnectInfo->au8bssid[5]);
			memcpy(priv->au8AssociatedBss, pstrConnectInfo->au8bssid, ETH_ALEN);


			for (i = 0; i < u32LastScannedNtwrksCountShadow; i++) {
				if (memcmp(astrLastScannedNtwrksShadow[i].au8bssid,
						pstrConnectInfo->au8bssid, ETH_ALEN) == 0) {
					unsigned long now = jiffies;

					if (time_after(now,
						       astrLastScannedNtwrksShadow[i].u32TimeRcvdInScanCached + (unsigned long)(nl80211_SCAN_RESULT_EXPIRE - (1 * HZ)))) {
						bNeedScanRefresh = true;
					}

					break;
				}
			}

			if (bNeedScanRefresh) {
				/*Also, refrsh DIRECT- results if */
				refresh_scan(priv, 1, true);

			}

		}


		PRINT_D(CFG80211_DBG, "Association request info elements length = %zu\n", pstrConnectInfo->ReqIEsLen);

		PRINT_D(CFG80211_DBG, "Association response info elements length = %d\n", pstrConnectInfo->u16RespIEsLen);

		cfg80211_connect_result(dev, pstrConnectInfo->au8bssid,
					pstrConnectInfo->pu8ReqIEs, pstrConnectInfo->ReqIEsLen,
					pstrConnectInfo->pu8RespIEs, pstrConnectInfo->u16RespIEsLen,
					u16ConnectStatus, GFP_KERNEL);                         /* TODO: mostafa: u16ConnectStatus to */
		/* be replaced by pstrConnectInfo->u16ConnectStatus */
	} else if (enuConnDisconnEvent == CONN_DISCONN_EVENT_DISCONN_NOTIF)    {
		g_obtainingIP = false;
		PRINT_ER("Received MAC_DISCONNECTED from firmware with reason %d on dev [%p]\n",
			 pstrDisconnectNotifInfo->u16reason, priv->dev);
		u8P2Plocalrandom = 0x01;
		u8P2Precvrandom = 0x00;
		bWilc_ie = false;
		eth_zero_addr(priv->au8AssociatedBss);
		linux_wlan_set_bssid(priv->dev, NullBssid);
		eth_zero_addr(u8ConnectedSSID);

		/*Invalidate u8WLANChannel value on wlan0 disconnect*/
		if (!pstrWFIDrv->u8P2PConnect)
			u8WLANChannel = INVALID_CHANNEL;
		/*Incase "P2P CLIENT Connected" send deauthentication reason by 3 to force the WPA_SUPPLICANT to directly change
		 *      virtual interface to station*/
		if ((pstrWFIDrv->IFC_UP) && (dev == wl->vif[1].ndev)) {
			pstrDisconnectNotifInfo->u16reason = 3;
		}
		/*Incase "P2P CLIENT during connection(not connected)" send deauthentication reason by 1 to force the WPA_SUPPLICANT
		 *      to scan again and retry the connection*/
		else if ((!pstrWFIDrv->IFC_UP) && (dev == wl->vif[1].ndev)) {
			pstrDisconnectNotifInfo->u16reason = 1;
		}
		cfg80211_disconnected(dev, pstrDisconnectNotifInfo->u16reason, pstrDisconnectNotifInfo->ie,
				      pstrDisconnectNotifInfo->ie_len, false,
				      GFP_KERNEL);

	}

}


/**
 *  @brief      set_channel
 *  @details    Set channel for a given wireless interface. Some devices
 *                      may support multi-channel operation (by channel hopping) so cfg80211
 *                      doesn't verify much. Note, however, that the passed netdev may be
 *                      %NULL as well if the user requested changing the channel for the
 *                      device itself, or for a monitor interface.
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int set_channel(struct wiphy *wiphy,
		       struct cfg80211_chan_def *chandef)
{
	u32 channelnum = 0;
	struct wilc_priv *priv;
	int result = 0;

	priv = wiphy_priv(wiphy);

	channelnum = ieee80211_frequency_to_channel(chandef->chan->center_freq);
	PRINT_D(CFG80211_DBG, "Setting channel %d with frequency %d\n", channelnum, chandef->chan->center_freq);

	curr_channel = channelnum;
	result = host_int_set_mac_chnl_num(priv->hWILCWFIDrv, channelnum);

	if (result != 0)
		PRINT_ER("Error in setting channel %d\n", channelnum);

	return result;
}

/**
 *  @brief      scan
 *  @details    Request to do a scan. If returning zero, the scan request is given
 *                      the driver, and will be valid until passed to cfg80211_scan_done().
 *                      For scan results, call cfg80211_inform_bss(); you can call this outside
 *                      the scan/scan_done bracket too.
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mabubakr
 *  @date	01 MAR 2012
 *  @version	1.0
 */

static int scan(struct wiphy *wiphy, struct cfg80211_scan_request *request)
{
	struct wilc_priv *priv;
	u32 i;
	s32 s32Error = 0;
	u8 au8ScanChanList[MAX_NUM_SCANNED_NETWORKS];
	struct hidden_network strHiddenNetwork;

	priv = wiphy_priv(wiphy);

	priv->pstrScanReq = request;

	priv->u32RcvdChCount = 0;

	host_int_set_wfi_drv_handler(priv->hWILCWFIDrv);


	reset_shadow_found(priv);

	priv->bCfgScanning = true;
	if (request->n_channels <= MAX_NUM_SCANNED_NETWORKS) { /* TODO: mostafa: to be replaced by */
		/*               max_scan_ssids */
		for (i = 0; i < request->n_channels; i++) {
			au8ScanChanList[i] = (u8)ieee80211_frequency_to_channel(request->channels[i]->center_freq);
			PRINT_INFO(CFG80211_DBG, "ScanChannel List[%d] = %d,", i, au8ScanChanList[i]);
		}

		PRINT_D(CFG80211_DBG, "Requested num of scan channel %d\n", request->n_channels);
		PRINT_D(CFG80211_DBG, "Scan Request IE len =  %zu\n", request->ie_len);

		PRINT_D(CFG80211_DBG, "Number of SSIDs %d\n", request->n_ssids);

		if (request->n_ssids >= 1) {


			strHiddenNetwork.pstrHiddenNetworkInfo = kmalloc(request->n_ssids * sizeof(struct hidden_network), GFP_KERNEL);
			strHiddenNetwork.u8ssidnum = request->n_ssids;


			for (i = 0; i < request->n_ssids; i++) {

				if (request->ssids[i].ssid != NULL && request->ssids[i].ssid_len != 0) {
					strHiddenNetwork.pstrHiddenNetworkInfo[i].pu8ssid = kmalloc(request->ssids[i].ssid_len, GFP_KERNEL);
					memcpy(strHiddenNetwork.pstrHiddenNetworkInfo[i].pu8ssid, request->ssids[i].ssid, request->ssids[i].ssid_len);
					strHiddenNetwork.pstrHiddenNetworkInfo[i].u8ssidlen = request->ssids[i].ssid_len;
				} else {
					PRINT_D(CFG80211_DBG, "Received one NULL SSID\n");
					strHiddenNetwork.u8ssidnum -= 1;
				}
			}
			PRINT_D(CFG80211_DBG, "Trigger Scan Request\n");
			s32Error = host_int_scan(priv->hWILCWFIDrv, USER_SCAN, ACTIVE_SCAN,
						 au8ScanChanList, request->n_channels,
						 (const u8 *)request->ie, request->ie_len,
						 CfgScanResult, (void *)priv, &strHiddenNetwork);
		} else {
			PRINT_D(CFG80211_DBG, "Trigger Scan Request\n");
			s32Error = host_int_scan(priv->hWILCWFIDrv, USER_SCAN, ACTIVE_SCAN,
						 au8ScanChanList, request->n_channels,
						 (const u8 *)request->ie, request->ie_len,
						 CfgScanResult, (void *)priv, NULL);
		}

	} else {
		PRINT_ER("Requested num of scanned channels is greater than the max, supported"
			 " channels\n");
	}

	if (s32Error != 0) {
		s32Error = -EBUSY;
		PRINT_WRN(CFG80211_DBG, "Device is busy: Error(%d)\n", s32Error);
	}

	return s32Error;
}

/**
 *  @brief      connect
 *  @details    Connect to the ESS with the specified parameters. When connected,
 *                      call cfg80211_connect_result() with status code %WLAN_STATUS_SUCCESS.
 *                      If the connection fails for some reason, call cfg80211_connect_result()
 *                      with the status from the AP.
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mabubakr
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int connect(struct wiphy *wiphy, struct net_device *dev,
		   struct cfg80211_connect_params *sme)
{
	s32 s32Error = 0;
	u32 i;
	u8 u8security = NO_ENCRYPT;
	enum AUTHTYPE tenuAuth_type = ANY;
	char *pcgroup_encrypt_val = NULL;
	char *pccipher_group = NULL;
	char *pcwpa_version = NULL;

	struct wilc_priv *priv;
	struct host_if_drv *pstrWFIDrv;
	tstrNetworkInfo *pstrNetworkInfo = NULL;


	connecting = 1;
	priv = wiphy_priv(wiphy);
	pstrWFIDrv = (struct host_if_drv *)(priv->hWILCWFIDrv);

	host_int_set_wfi_drv_handler(priv->hWILCWFIDrv);

	PRINT_D(CFG80211_DBG, "Connecting to SSID [%s] on netdev [%p] host if [%p]\n", sme->ssid, dev, priv->hWILCWFIDrv);
	if (!(strncmp(sme->ssid, "DIRECT-", 7))) {
		PRINT_D(CFG80211_DBG, "Connected to Direct network,OBSS disabled\n");
		pstrWFIDrv->u8P2PConnect = 1;
	} else
		pstrWFIDrv->u8P2PConnect = 0;
	PRINT_INFO(CFG80211_DBG, "Required SSID = %s\n , AuthType = %d\n", sme->ssid, sme->auth_type);

	for (i = 0; i < u32LastScannedNtwrksCountShadow; i++) {
		if ((sme->ssid_len == astrLastScannedNtwrksShadow[i].u8SsidLen) &&
		    memcmp(astrLastScannedNtwrksShadow[i].au8ssid,
				sme->ssid,
				sme->ssid_len) == 0) {
			PRINT_INFO(CFG80211_DBG, "Network with required SSID is found %s\n", sme->ssid);
			if (sme->bssid == NULL)	{
				/* BSSID is not passed from the user, so decision of matching
				 * is done by SSID only */
				PRINT_INFO(CFG80211_DBG, "BSSID is not passed from the user\n");
				break;
			} else {
				/* BSSID is also passed from the user, so decision of matching
				 * should consider also this passed BSSID */
				if (memcmp(astrLastScannedNtwrksShadow[i].au8bssid,
						sme->bssid,
						ETH_ALEN) == 0)	{
					PRINT_INFO(CFG80211_DBG, "BSSID is passed from the user and matched\n");
					break;
				}
			}
		}
	}

	if (i < u32LastScannedNtwrksCountShadow) {
		PRINT_D(CFG80211_DBG, "Required bss is in scan results\n");

		pstrNetworkInfo = &(astrLastScannedNtwrksShadow[i]);

		PRINT_INFO(CFG80211_DBG, "network BSSID to be associated: %x%x%x%x%x%x\n",
			   pstrNetworkInfo->au8bssid[0], pstrNetworkInfo->au8bssid[1],
			   pstrNetworkInfo->au8bssid[2], pstrNetworkInfo->au8bssid[3],
			   pstrNetworkInfo->au8bssid[4], pstrNetworkInfo->au8bssid[5]);
	} else {
		s32Error = -ENOENT;
		if (u32LastScannedNtwrksCountShadow == 0)
			PRINT_D(CFG80211_DBG, "No Scan results yet\n");
		else
			PRINT_D(CFG80211_DBG, "Required bss not in scan results: Error(%d)\n", s32Error);

		goto done;
	}

	priv->WILC_WFI_wep_default = 0;
	memset(priv->WILC_WFI_wep_key, 0, sizeof(priv->WILC_WFI_wep_key));
	memset(priv->WILC_WFI_wep_key_len, 0, sizeof(priv->WILC_WFI_wep_key_len));

	PRINT_INFO(CFG80211_DBG, "sme->crypto.wpa_versions=%x\n", sme->crypto.wpa_versions);
	PRINT_INFO(CFG80211_DBG, "sme->crypto.cipher_group=%x\n", sme->crypto.cipher_group);

	PRINT_INFO(CFG80211_DBG, "sme->crypto.n_ciphers_pairwise=%d\n", sme->crypto.n_ciphers_pairwise);

	if (INFO) {
		for (i = 0; i < sme->crypto.n_ciphers_pairwise; i++)
			PRINT_D(CORECONFIG_DBG, "sme->crypto.ciphers_pairwise[%d]=%x\n", i, sme->crypto.ciphers_pairwise[i]);
	}

	if (sme->crypto.cipher_group != NO_ENCRYPT) {
		/* To determine the u8security value, first we check the group cipher suite then {in case of WPA or WPA2}
		 *  we will add to it the pairwise cipher suite(s) */
		pcwpa_version = "Default";
		PRINT_D(CORECONFIG_DBG, ">> sme->crypto.wpa_versions: %x\n", sme->crypto.wpa_versions);
		if (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP40) {
			u8security = ENCRYPT_ENABLED | WEP;
			pcgroup_encrypt_val = "WEP40";
			pccipher_group = "WLAN_CIPHER_SUITE_WEP40";
			PRINT_INFO(CFG80211_DBG, "WEP Default Key Idx = %d\n", sme->key_idx);

			if (INFO) {
				for (i = 0; i < sme->key_len; i++)
					PRINT_D(CORECONFIG_DBG, "WEP Key Value[%d] = %d\n", i, sme->key[i]);
			}
			priv->WILC_WFI_wep_default = sme->key_idx;
			priv->WILC_WFI_wep_key_len[sme->key_idx] = sme->key_len;
			memcpy(priv->WILC_WFI_wep_key[sme->key_idx], sme->key, sme->key_len);

			g_key_wep_params.key_len = sme->key_len;
			g_key_wep_params.key = kmalloc(sme->key_len, GFP_KERNEL);
			memcpy(g_key_wep_params.key, sme->key, sme->key_len);
			g_key_wep_params.key_idx = sme->key_idx;
			g_wep_keys_saved = true;

			host_int_set_wep_default_key(priv->hWILCWFIDrv, sme->key_idx);
			host_int_add_wep_key_bss_sta(priv->hWILCWFIDrv, sme->key, sme->key_len, sme->key_idx);
		} else if (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP104)   {
			u8security = ENCRYPT_ENABLED | WEP | WEP_EXTENDED;
			pcgroup_encrypt_val = "WEP104";
			pccipher_group = "WLAN_CIPHER_SUITE_WEP104";

			priv->WILC_WFI_wep_default = sme->key_idx;
			priv->WILC_WFI_wep_key_len[sme->key_idx] = sme->key_len;
			memcpy(priv->WILC_WFI_wep_key[sme->key_idx], sme->key, sme->key_len);

			g_key_wep_params.key_len = sme->key_len;
			g_key_wep_params.key = kmalloc(sme->key_len, GFP_KERNEL);
			memcpy(g_key_wep_params.key, sme->key, sme->key_len);
			g_key_wep_params.key_idx = sme->key_idx;
			g_wep_keys_saved = true;

			host_int_set_wep_default_key(priv->hWILCWFIDrv, sme->key_idx);
			host_int_add_wep_key_bss_sta(priv->hWILCWFIDrv, sme->key, sme->key_len, sme->key_idx);
		} else if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_2)   {
			if (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_TKIP)	{
				u8security = ENCRYPT_ENABLED | WPA2 | TKIP;
				pcgroup_encrypt_val = "WPA2_TKIP";
				pccipher_group = "TKIP";
			} else {     /* TODO: mostafa: here we assume that any other encryption type is AES */
				     /* tenuSecurity_t = WPA2_AES; */
				u8security = ENCRYPT_ENABLED | WPA2 | AES;
				pcgroup_encrypt_val = "WPA2_AES";
				pccipher_group = "AES";
			}
			pcwpa_version = "WPA_VERSION_2";
		} else if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_1)   {
			if (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_TKIP)	{
				u8security = ENCRYPT_ENABLED | WPA | TKIP;
				pcgroup_encrypt_val = "WPA_TKIP";
				pccipher_group = "TKIP";
			} else {     /* TODO: mostafa: here we assume that any other encryption type is AES */
				     /* tenuSecurity_t = WPA_AES; */
				u8security = ENCRYPT_ENABLED | WPA | AES;
				pcgroup_encrypt_val = "WPA_AES";
				pccipher_group = "AES";

			}
			pcwpa_version = "WPA_VERSION_1";

		} else {
			s32Error = -ENOTSUPP;
			PRINT_ER("Not supported cipher: Error(%d)\n", s32Error);

			goto done;
		}

	}

	/* After we set the u8security value from checking the group cipher suite, {in case of WPA or WPA2} we will
	 *   add to it the pairwise cipher suite(s) */
	if ((sme->crypto.wpa_versions & NL80211_WPA_VERSION_1)
	    || (sme->crypto.wpa_versions & NL80211_WPA_VERSION_2)) {
		for (i = 0; i < sme->crypto.n_ciphers_pairwise; i++) {
			if (sme->crypto.ciphers_pairwise[i] == WLAN_CIPHER_SUITE_TKIP) {
				u8security = u8security | TKIP;
			} else {     /* TODO: mostafa: here we assume that any other encryption type is AES */
				u8security = u8security | AES;
			}
		}
	}

	PRINT_D(CFG80211_DBG, "Adding key with cipher group = %x\n", sme->crypto.cipher_group);

	PRINT_D(CFG80211_DBG, "Authentication Type = %d\n", sme->auth_type);
	switch (sme->auth_type)	{
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		PRINT_D(CFG80211_DBG, "In OPEN SYSTEM\n");
		tenuAuth_type = OPEN_SYSTEM;
		break;

	case NL80211_AUTHTYPE_SHARED_KEY:
		tenuAuth_type = SHARED_KEY;
		PRINT_D(CFG80211_DBG, "In SHARED KEY\n");
		break;

	default:
		PRINT_D(CFG80211_DBG, "Automatic Authentation type = %d\n", sme->auth_type);
	}


	/* ai: key_mgmt: enterprise case */
	if (sme->crypto.n_akm_suites) {
		switch (sme->crypto.akm_suites[0]) {
		case WLAN_AKM_SUITE_8021X:
			tenuAuth_type = IEEE8021;
			break;

		default:
			break;
		}
	}


	PRINT_INFO(CFG80211_DBG, "Required Channel = %d\n", pstrNetworkInfo->u8channel);

	PRINT_INFO(CFG80211_DBG, "Group encryption value = %s\n Cipher Group = %s\n WPA version = %s\n",
		   pcgroup_encrypt_val, pccipher_group, pcwpa_version);

	curr_channel = pstrNetworkInfo->u8channel;

	if (!pstrWFIDrv->u8P2PConnect) {
		u8WLANChannel = pstrNetworkInfo->u8channel;
	}

	linux_wlan_set_bssid(dev, pstrNetworkInfo->au8bssid);

	s32Error = host_int_set_join_req(priv->hWILCWFIDrv, pstrNetworkInfo->au8bssid, sme->ssid,
					 sme->ssid_len, sme->ie, sme->ie_len,
					 CfgConnectResult, (void *)priv, u8security,
					 tenuAuth_type, pstrNetworkInfo->u8channel,
					 pstrNetworkInfo->pJoinParams);
	if (s32Error != 0) {
		PRINT_ER("host_int_set_join_req(): Error(%d)\n", s32Error);
		s32Error = -ENOENT;
		goto done;
	}

done:

	return s32Error;
}


/**
 *  @brief      disconnect
 *  @details    Disconnect from the BSS/ESS.
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int disconnect(struct wiphy *wiphy, struct net_device *dev, u16 reason_code)
{
	s32 s32Error = 0;
	struct wilc_priv *priv;
	struct host_if_drv *pstrWFIDrv;
	u8 NullBssid[ETH_ALEN] = {0};

	connecting = 0;
	priv = wiphy_priv(wiphy);

	/*Invalidate u8WLANChannel value on wlan0 disconnect*/
	pstrWFIDrv = (struct host_if_drv *)priv->hWILCWFIDrv;
	if (!pstrWFIDrv->u8P2PConnect)
		u8WLANChannel = INVALID_CHANNEL;
	linux_wlan_set_bssid(priv->dev, NullBssid);

	PRINT_D(CFG80211_DBG, "Disconnecting with reason code(%d)\n", reason_code);

	u8P2Plocalrandom = 0x01;
	u8P2Precvrandom = 0x00;
	bWilc_ie = false;
	pstrWFIDrv->u64P2p_MgmtTimeout = 0;

	s32Error = host_int_disconnect(priv->hWILCWFIDrv, reason_code);
	if (s32Error != 0) {
		PRINT_ER("Error in disconnecting: Error(%d)\n", s32Error);
		s32Error = -EINVAL;
	}

	return s32Error;
}

/**
 *  @brief      add_key
 *  @details    Add a key with the given parameters. @mac_addr will be %NULL
 *                      when adding a group key.
 *  @param[in] key : key buffer; TKIP: 16-byte temporal key, 8-byte Tx Mic key, 8-byte Rx Mic Key
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int add_key(struct wiphy *wiphy, struct net_device *netdev, u8 key_index,
		   bool pairwise,
		   const u8 *mac_addr, struct key_params *params)

{
	s32 s32Error = 0, KeyLen = params->key_len;
	u32 i;
	struct wilc_priv *priv;
	const u8 *pu8RxMic = NULL;
	const u8 *pu8TxMic = NULL;
	u8 u8mode = NO_ENCRYPT;
	u8 u8gmode = NO_ENCRYPT;
	u8 u8pmode = NO_ENCRYPT;
	enum AUTHTYPE tenuAuth_type = ANY;
	struct wilc *wl;
	perInterface_wlan_t *nic;

	priv = wiphy_priv(wiphy);
	nic = netdev_priv(netdev);
	wl = nic->wilc;

	PRINT_D(CFG80211_DBG, "Adding key with cipher suite = %x\n", params->cipher);

	PRINT_D(CFG80211_DBG, "%p %p %d\n", wiphy, netdev, key_index);

	PRINT_D(CFG80211_DBG, "key %x %x %x\n", params->key[0],
		params->key[1],
		params->key[2]);


	switch (params->cipher)	{
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		if (priv->wdev->iftype == NL80211_IFTYPE_AP) {

			priv->WILC_WFI_wep_default = key_index;
			priv->WILC_WFI_wep_key_len[key_index] = params->key_len;
			memcpy(priv->WILC_WFI_wep_key[key_index], params->key, params->key_len);

			PRINT_D(CFG80211_DBG, "Adding AP WEP Default key Idx = %d\n", key_index);
			PRINT_D(CFG80211_DBG, "Adding AP WEP Key len= %d\n", params->key_len);

			for (i = 0; i < params->key_len; i++)
				PRINT_D(CFG80211_DBG, "WEP AP key val[%d] = %x\n", i, params->key[i]);

			tenuAuth_type = OPEN_SYSTEM;

			if (params->cipher == WLAN_CIPHER_SUITE_WEP40)
				u8mode = ENCRYPT_ENABLED | WEP;
			else
				u8mode = ENCRYPT_ENABLED | WEP | WEP_EXTENDED;

			host_int_add_wep_key_bss_ap(priv->hWILCWFIDrv, params->key, params->key_len, key_index, u8mode, tenuAuth_type);
			break;
		}
		if (memcmp(params->key, priv->WILC_WFI_wep_key[key_index], params->key_len)) {
			priv->WILC_WFI_wep_default = key_index;
			priv->WILC_WFI_wep_key_len[key_index] = params->key_len;
			memcpy(priv->WILC_WFI_wep_key[key_index], params->key, params->key_len);

			PRINT_D(CFG80211_DBG, "Adding WEP Default key Idx = %d\n", key_index);
			PRINT_D(CFG80211_DBG, "Adding WEP Key length = %d\n", params->key_len);
			if (INFO) {
				for (i = 0; i < params->key_len; i++)
					PRINT_INFO(CFG80211_DBG, "WEP key value[%d] = %d\n", i, params->key[i]);
			}
			host_int_add_wep_key_bss_sta(priv->hWILCWFIDrv, params->key, params->key_len, key_index);
		}

		break;

	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
		if (priv->wdev->iftype == NL80211_IFTYPE_AP || priv->wdev->iftype == NL80211_IFTYPE_P2P_GO) {

			if (priv->wilc_gtk[key_index] == NULL) {
				priv->wilc_gtk[key_index] = kmalloc(sizeof(struct wilc_wfi_key), GFP_KERNEL);
				priv->wilc_gtk[key_index]->key = NULL;
				priv->wilc_gtk[key_index]->seq = NULL;

			}
			if (priv->wilc_ptk[key_index] == NULL) {
				priv->wilc_ptk[key_index] = kmalloc(sizeof(struct wilc_wfi_key), GFP_KERNEL);
				priv->wilc_ptk[key_index]->key = NULL;
				priv->wilc_ptk[key_index]->seq = NULL;
			}



			if (!pairwise) {
				if (params->cipher == WLAN_CIPHER_SUITE_TKIP)
					u8gmode = ENCRYPT_ENABLED | WPA | TKIP;
				else
					u8gmode = ENCRYPT_ENABLED | WPA2 | AES;

				priv->wilc_groupkey = u8gmode;

				if (params->key_len > 16 && params->cipher == WLAN_CIPHER_SUITE_TKIP) {

					pu8TxMic = params->key + 24;
					pu8RxMic = params->key + 16;
					KeyLen = params->key_len - 16;
				}
				/* if there has been previous allocation for the same index through its key, free that memory and allocate again*/
				kfree(priv->wilc_gtk[key_index]->key);

				priv->wilc_gtk[key_index]->key = kmalloc(params->key_len, GFP_KERNEL);
				memcpy(priv->wilc_gtk[key_index]->key, params->key, params->key_len);

				/* if there has been previous allocation for the same index through its seq, free that memory and allocate again*/
				kfree(priv->wilc_gtk[key_index]->seq);

				if ((params->seq_len) > 0) {
					priv->wilc_gtk[key_index]->seq = kmalloc(params->seq_len, GFP_KERNEL);
					memcpy(priv->wilc_gtk[key_index]->seq, params->seq, params->seq_len);
				}

				priv->wilc_gtk[key_index]->cipher = params->cipher;
				priv->wilc_gtk[key_index]->key_len = params->key_len;
				priv->wilc_gtk[key_index]->seq_len = params->seq_len;

				if (INFO) {
					for (i = 0; i < params->key_len; i++)
						PRINT_INFO(CFG80211_DBG, "Adding group key value[%d] = %x\n", i, params->key[i]);
					for (i = 0; i < params->seq_len; i++)
						PRINT_INFO(CFG80211_DBG, "Adding group seq value[%d] = %x\n", i, params->seq[i]);
				}


				host_int_add_rx_gtk(priv->hWILCWFIDrv, params->key, KeyLen,
						    key_index, params->seq_len, params->seq, pu8RxMic, pu8TxMic, AP_MODE, u8gmode);

			} else {
				PRINT_INFO(CFG80211_DBG, "STA Address: %x%x%x%x%x\n", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4]);

				if (params->cipher == WLAN_CIPHER_SUITE_TKIP)
					u8pmode = ENCRYPT_ENABLED | WPA | TKIP;
				else
					u8pmode = priv->wilc_groupkey | AES;


				if (params->key_len > 16 && params->cipher == WLAN_CIPHER_SUITE_TKIP) {

					pu8TxMic = params->key + 24;
					pu8RxMic = params->key + 16;
					KeyLen = params->key_len - 16;
				}

				kfree(priv->wilc_ptk[key_index]->key);

				priv->wilc_ptk[key_index]->key = kmalloc(params->key_len, GFP_KERNEL);

				kfree(priv->wilc_ptk[key_index]->seq);

				if ((params->seq_len) > 0)
					priv->wilc_ptk[key_index]->seq = kmalloc(params->seq_len, GFP_KERNEL);

				if (INFO) {
					for (i = 0; i < params->key_len; i++)
						PRINT_INFO(CFG80211_DBG, "Adding pairwise key value[%d] = %x\n", i, params->key[i]);

					for (i = 0; i < params->seq_len; i++)
						PRINT_INFO(CFG80211_DBG, "Adding group seq value[%d] = %x\n", i, params->seq[i]);
				}

				memcpy(priv->wilc_ptk[key_index]->key, params->key, params->key_len);

				if ((params->seq_len) > 0)
					memcpy(priv->wilc_ptk[key_index]->seq, params->seq, params->seq_len);

				priv->wilc_ptk[key_index]->cipher = params->cipher;
				priv->wilc_ptk[key_index]->key_len = params->key_len;
				priv->wilc_ptk[key_index]->seq_len = params->seq_len;

				host_int_add_ptk(priv->hWILCWFIDrv, params->key, KeyLen, mac_addr,
						 pu8RxMic, pu8TxMic, AP_MODE, u8pmode, key_index);
			}
			break;
		}

		{
			u8mode = 0;
			if (!pairwise) {
				if (params->key_len > 16 && params->cipher == WLAN_CIPHER_SUITE_TKIP) {
					/* swap the tx mic by rx mic */
					pu8RxMic = params->key + 24;
					pu8TxMic = params->key + 16;
					KeyLen = params->key_len - 16;
				}

				/*save keys only on interface 0 (wifi interface)*/
				if (!g_gtk_keys_saved && netdev == wl->vif[0].ndev) {
					g_add_gtk_key_params.key_idx = key_index;
					g_add_gtk_key_params.pairwise = pairwise;
					if (!mac_addr) {
						g_add_gtk_key_params.mac_addr = NULL;
					} else {
						g_add_gtk_key_params.mac_addr = kmalloc(ETH_ALEN, GFP_KERNEL);
						memcpy(g_add_gtk_key_params.mac_addr, mac_addr, ETH_ALEN);
					}
					g_key_gtk_params.key_len = params->key_len;
					g_key_gtk_params.seq_len = params->seq_len;
					g_key_gtk_params.key =  kmalloc(params->key_len, GFP_KERNEL);
					memcpy(g_key_gtk_params.key, params->key, params->key_len);
					if (params->seq_len > 0) {
						g_key_gtk_params.seq =  kmalloc(params->seq_len, GFP_KERNEL);
						memcpy(g_key_gtk_params.seq, params->seq, params->seq_len);
					}
					g_key_gtk_params.cipher = params->cipher;

					PRINT_D(CFG80211_DBG, "key %x %x %x\n", g_key_gtk_params.key[0],
						g_key_gtk_params.key[1],
						g_key_gtk_params.key[2]);
					g_gtk_keys_saved = true;
				}

				host_int_add_rx_gtk(priv->hWILCWFIDrv, params->key, KeyLen,
						    key_index, params->seq_len, params->seq, pu8RxMic, pu8TxMic, STATION_MODE, u8mode);
			} else {
				if (params->key_len > 16 && params->cipher == WLAN_CIPHER_SUITE_TKIP) {
					/* swap the tx mic by rx mic */
					pu8RxMic = params->key + 24;
					pu8TxMic = params->key + 16;
					KeyLen = params->key_len - 16;
				}

				/*save keys only on interface 0 (wifi interface)*/
				if (!g_ptk_keys_saved && netdev == wl->vif[0].ndev) {
					g_add_ptk_key_params.key_idx = key_index;
					g_add_ptk_key_params.pairwise = pairwise;
					if (!mac_addr) {
						g_add_ptk_key_params.mac_addr = NULL;
					} else {
						g_add_ptk_key_params.mac_addr = kmalloc(ETH_ALEN, GFP_KERNEL);
						memcpy(g_add_ptk_key_params.mac_addr, mac_addr, ETH_ALEN);
					}
					g_key_ptk_params.key_len = params->key_len;
					g_key_ptk_params.seq_len = params->seq_len;
					g_key_ptk_params.key =  kmalloc(params->key_len, GFP_KERNEL);
					memcpy(g_key_ptk_params.key, params->key, params->key_len);
					if (params->seq_len > 0) {
						g_key_ptk_params.seq =  kmalloc(params->seq_len, GFP_KERNEL);
						memcpy(g_key_ptk_params.seq, params->seq, params->seq_len);
					}
					g_key_ptk_params.cipher = params->cipher;

					PRINT_D(CFG80211_DBG, "key %x %x %x\n", g_key_ptk_params.key[0],
						g_key_ptk_params.key[1],
						g_key_ptk_params.key[2]);
					g_ptk_keys_saved = true;
				}

				host_int_add_ptk(priv->hWILCWFIDrv, params->key, KeyLen, mac_addr,
						 pu8RxMic, pu8TxMic, STATION_MODE, u8mode, key_index);
				PRINT_D(CFG80211_DBG, "Adding pairwise key\n");
				if (INFO) {
					for (i = 0; i < params->key_len; i++)
						PRINT_INFO(CFG80211_DBG, "Adding pairwise key value[%d] = %d\n", i, params->key[i]);
				}
			}
		}
		break;

	default:
		PRINT_ER("Not supported cipher: Error(%d)\n", s32Error);
		s32Error = -ENOTSUPP;

	}

	return s32Error;
}

/**
 *  @brief      del_key
 *  @details    Remove a key given the @mac_addr (%NULL for a group key)
 *                      and @key_index, return -ENOENT if the key doesn't exist.
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int del_key(struct wiphy *wiphy, struct net_device *netdev,
		   u8 key_index,
		   bool pairwise,
		   const u8 *mac_addr)
{
	struct wilc_priv *priv;
	struct wilc *wl;
	perInterface_wlan_t *nic;

	priv = wiphy_priv(wiphy);
	nic = netdev_priv(netdev);
	wl = nic->wilc;

	/*delete saved keys, if any*/
	if (netdev == wl->vif[0].ndev) {
		g_ptk_keys_saved = false;
		g_gtk_keys_saved = false;
		g_wep_keys_saved = false;

		/*Delete saved WEP keys params, if any*/
		kfree(g_key_wep_params.key);
		g_key_wep_params.key = NULL;

		/*freeing memory allocated by "wilc_gtk" and "wilc_ptk" in "WILC_WIFI_ADD_KEY"*/

		if ((priv->wilc_gtk[key_index]) != NULL) {

			kfree(priv->wilc_gtk[key_index]->key);
			priv->wilc_gtk[key_index]->key = NULL;
			kfree(priv->wilc_gtk[key_index]->seq);
			priv->wilc_gtk[key_index]->seq = NULL;

			kfree(priv->wilc_gtk[key_index]);
			priv->wilc_gtk[key_index] = NULL;

		}

		if ((priv->wilc_ptk[key_index]) != NULL) {

			kfree(priv->wilc_ptk[key_index]->key);
			priv->wilc_ptk[key_index]->key = NULL;
			kfree(priv->wilc_ptk[key_index]->seq);
			priv->wilc_ptk[key_index]->seq = NULL;
			kfree(priv->wilc_ptk[key_index]);
			priv->wilc_ptk[key_index] = NULL;
		}

		/*Delete saved PTK and GTK keys params, if any*/
		kfree(g_key_ptk_params.key);
		g_key_ptk_params.key = NULL;
		kfree(g_key_ptk_params.seq);
		g_key_ptk_params.seq = NULL;

		kfree(g_key_gtk_params.key);
		g_key_gtk_params.key = NULL;
		kfree(g_key_gtk_params.seq);
		g_key_gtk_params.seq = NULL;

		/*Reset WILC_CHANGING_VIR_IF register to allow adding futrue keys to CE H/W*/
		Set_machw_change_vir_if(netdev, false);
	}

	if (key_index >= 0 && key_index <= 3) {
		memset(priv->WILC_WFI_wep_key[key_index], 0, priv->WILC_WFI_wep_key_len[key_index]);
		priv->WILC_WFI_wep_key_len[key_index] = 0;

		PRINT_D(CFG80211_DBG, "Removing WEP key with index = %d\n", key_index);
		host_int_remove_wep_key(priv->hWILCWFIDrv, key_index);
	} else {
		PRINT_D(CFG80211_DBG, "Removing all installed keys\n");
		host_int_remove_key(priv->hWILCWFIDrv, mac_addr);
	}

	return 0;
}

/**
 *  @brief      get_key
 *  @details    Get information about the key with the given parameters.
 *                      @mac_addr will be %NULL when requesting information for a group
 *                      key. All pointers given to the @callback function need not be valid
 *                      after it returns. This function should return an error if it is
 *                      not possible to retrieve the key, -ENOENT if it doesn't exist.
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int get_key(struct wiphy *wiphy, struct net_device *netdev, u8 key_index,
		   bool pairwise,
		   const u8 *mac_addr, void *cookie, void (*callback)(void *cookie, struct key_params *))
{
	struct wilc_priv *priv;
	struct  key_params key_params;
	u32 i;

	priv = wiphy_priv(wiphy);


	if (!pairwise) {
		PRINT_D(CFG80211_DBG, "Getting group key idx: %x\n", key_index);

		key_params.key = priv->wilc_gtk[key_index]->key;
		key_params.cipher = priv->wilc_gtk[key_index]->cipher;
		key_params.key_len = priv->wilc_gtk[key_index]->key_len;
		key_params.seq = priv->wilc_gtk[key_index]->seq;
		key_params.seq_len = priv->wilc_gtk[key_index]->seq_len;
		if (INFO) {
			for (i = 0; i < key_params.key_len; i++)
				PRINT_INFO(CFG80211_DBG, "Retrieved key value %x\n", key_params.key[i]);
		}
	} else {
		PRINT_D(CFG80211_DBG, "Getting pairwise  key\n");

		key_params.key = priv->wilc_ptk[key_index]->key;
		key_params.cipher = priv->wilc_ptk[key_index]->cipher;
		key_params.key_len = priv->wilc_ptk[key_index]->key_len;
		key_params.seq = priv->wilc_ptk[key_index]->seq;
		key_params.seq_len = priv->wilc_ptk[key_index]->seq_len;
	}

	callback(cookie, &key_params);

	return 0;        /* priv->wilc_gtk->key_len ?0 : -ENOENT; */
}

/**
 *  @brief      set_default_key
 *  @details    Set the default management frame key on an interface
 *  @param[in]
 *  @return     int : Return 0 on Success.
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int set_default_key(struct wiphy *wiphy, struct net_device *netdev, u8 key_index,
			   bool unicast, bool multicast)
{
	struct wilc_priv *priv;


	priv = wiphy_priv(wiphy);

	PRINT_D(CFG80211_DBG, "Setting default key with idx = %d\n", key_index);

	if (key_index != priv->WILC_WFI_wep_default) {

		host_int_set_wep_default_key(priv->hWILCWFIDrv, key_index);
	}

	return 0;
}

/**
 *  @brief      get_station
 *  @details    Get station information for the station identified by @mac
 *  @param[in]   NONE
 *  @return     int : Return 0 on Success.
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */

static int get_station(struct wiphy *wiphy, struct net_device *dev,
		       const u8 *mac, struct station_info *sinfo)
{
	struct wilc_priv *priv;
	perInterface_wlan_t *nic;
	u32 i = 0;
	u32 associatedsta = 0;
	u32 inactive_time = 0;
	priv = wiphy_priv(wiphy);
	nic = netdev_priv(dev);

	if (nic->iftype == AP_MODE || nic->iftype == GO_MODE) {
		PRINT_D(HOSTAPD_DBG, "Getting station parameters\n");

		PRINT_INFO(HOSTAPD_DBG, ": %x%x%x%x%x\n", mac[0], mac[1], mac[2], mac[3], mac[4]);

		for (i = 0; i < NUM_STA_ASSOCIATED; i++) {

			if (!(memcmp(mac, priv->assoc_stainfo.au8Sta_AssociatedBss[i], ETH_ALEN))) {
				associatedsta = i;
				break;
			}

		}

		if (associatedsta == -1) {
			PRINT_ER("Station required is not associated\n");
			return -ENOENT;
		}

		sinfo->filled |= BIT(NL80211_STA_INFO_INACTIVE_TIME);

		host_int_get_inactive_time(priv->hWILCWFIDrv, mac, &(inactive_time));
		sinfo->inactive_time = 1000 * inactive_time;
		PRINT_D(CFG80211_DBG, "Inactive time %d\n", sinfo->inactive_time);

	}

	if (nic->iftype == STATION_MODE) {
		struct rf_info strStatistics;

		host_int_get_statistics(priv->hWILCWFIDrv, &strStatistics);

		sinfo->filled |= BIT(NL80211_STA_INFO_SIGNAL) |
						BIT(NL80211_STA_INFO_RX_PACKETS) |
						BIT(NL80211_STA_INFO_TX_PACKETS) |
						BIT(NL80211_STA_INFO_TX_FAILED) |
						BIT(NL80211_STA_INFO_TX_BITRATE);

		sinfo->signal		=  strStatistics.s8RSSI;
		sinfo->rx_packets   =  strStatistics.u32RxCount;
		sinfo->tx_packets   =  strStatistics.u32TxCount + strStatistics.u32TxFailureCount;
		sinfo->tx_failed	=  strStatistics.u32TxFailureCount;
		sinfo->txrate.legacy = strStatistics.u8LinkSpeed * 10;

		if ((strStatistics.u8LinkSpeed > TCP_ACK_FILTER_LINK_SPEED_THRESH) && (strStatistics.u8LinkSpeed != DEFAULT_LINK_SPEED))
			Enable_TCP_ACK_Filter(true);
		else if (strStatistics.u8LinkSpeed != DEFAULT_LINK_SPEED)
			Enable_TCP_ACK_Filter(false);

		PRINT_D(CORECONFIG_DBG, "*** stats[%d][%d][%d][%d][%d]\n", sinfo->signal, sinfo->rx_packets, sinfo->tx_packets,
			sinfo->tx_failed, sinfo->txrate.legacy);
	}
	return 0;
}


/**
 *  @brief      change_bss
 *  @details    Modify parameters for a given BSS.
 *  @param[in]
 *   -use_cts_prot: Whether to use CTS protection
 *	    (0 = no, 1 = yes, -1 = do not change)
 *  -use_short_preamble: Whether the use of short preambles is allowed
 *	    (0 = no, 1 = yes, -1 = do not change)
 *  -use_short_slot_time: Whether the use of short slot time is allowed
 *	    (0 = no, 1 = yes, -1 = do not change)
 *  -basic_rates: basic rates in IEEE 802.11 format
 *	    (or NULL for no change)
 *  -basic_rates_len: number of basic rates
 *  -ap_isolate: do not forward packets between connected stations
 *  -ht_opmode: HT Operation mode
 *         (u16 = opmode, -1 = do not change)
 *  @return     int : Return 0 on Success.
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int change_bss(struct wiphy *wiphy, struct net_device *dev,
		      struct bss_parameters *params)
{
	PRINT_D(CFG80211_DBG, "Changing Bss parametrs\n");
	return 0;
}

/**
 *  @brief      set_wiphy_params
 *  @details    Notify that wiphy parameters have changed;
 *  @param[in]   Changed bitfield (see &enum wiphy_params_flags) describes which values
 *                      have changed.
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	s32 s32Error = 0;
	struct cfg_param_val pstrCfgParamVal;
	struct wilc_priv *priv;

	priv = wiphy_priv(wiphy);

	pstrCfgParamVal.flag = 0;
	PRINT_D(CFG80211_DBG, "Setting Wiphy params\n");

	if (changed & WIPHY_PARAM_RETRY_SHORT) {
		PRINT_D(CFG80211_DBG, "Setting WIPHY_PARAM_RETRY_SHORT %d\n",
			priv->dev->ieee80211_ptr->wiphy->retry_short);
		pstrCfgParamVal.flag  |= RETRY_SHORT;
		pstrCfgParamVal.short_retry_limit = priv->dev->ieee80211_ptr->wiphy->retry_short;
	}
	if (changed & WIPHY_PARAM_RETRY_LONG) {

		PRINT_D(CFG80211_DBG, "Setting WIPHY_PARAM_RETRY_LONG %d\n", priv->dev->ieee80211_ptr->wiphy->retry_long);
		pstrCfgParamVal.flag |= RETRY_LONG;
		pstrCfgParamVal.long_retry_limit = priv->dev->ieee80211_ptr->wiphy->retry_long;

	}
	if (changed & WIPHY_PARAM_FRAG_THRESHOLD) {
		PRINT_D(CFG80211_DBG, "Setting WIPHY_PARAM_FRAG_THRESHOLD %d\n", priv->dev->ieee80211_ptr->wiphy->frag_threshold);
		pstrCfgParamVal.flag |= FRAG_THRESHOLD;
		pstrCfgParamVal.frag_threshold = priv->dev->ieee80211_ptr->wiphy->frag_threshold;

	}

	if (changed & WIPHY_PARAM_RTS_THRESHOLD) {
		PRINT_D(CFG80211_DBG, "Setting WIPHY_PARAM_RTS_THRESHOLD %d\n", priv->dev->ieee80211_ptr->wiphy->rts_threshold);

		pstrCfgParamVal.flag |= RTS_THRESHOLD;
		pstrCfgParamVal.rts_threshold = priv->dev->ieee80211_ptr->wiphy->rts_threshold;

	}

	PRINT_D(CFG80211_DBG, "Setting CFG params in the host interface\n");
	s32Error = hif_set_cfg(priv->hWILCWFIDrv, &pstrCfgParamVal);
	if (s32Error)
		PRINT_ER("Error in setting WIPHY PARAMS\n");


	return s32Error;
}

/**
 *  @brief      set_pmksa
 *  @details    Cache a PMKID for a BSSID. This is mostly useful for fullmac
 *                      devices running firmwares capable of generating the (re) association
 *                      RSN IE. It allows for faster roaming between WPA2 BSSIDs.
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int set_pmksa(struct wiphy *wiphy, struct net_device *netdev,
		     struct cfg80211_pmksa *pmksa)
{
	u32 i;
	s32 s32Error = 0;
	u8 flag = 0;

	struct wilc_priv *priv = wiphy_priv(wiphy);

	PRINT_D(CFG80211_DBG, "Setting PMKSA\n");


	for (i = 0; i < priv->pmkid_list.numpmkid; i++)	{
		if (!memcmp(pmksa->bssid, priv->pmkid_list.pmkidlist[i].bssid,
				 ETH_ALEN)) {
			/*If bssid already exists and pmkid value needs to reset*/
			flag = PMKID_FOUND;
			PRINT_D(CFG80211_DBG, "PMKID already exists\n");
			break;
		}
	}
	if (i < WILC_MAX_NUM_PMKIDS) {
		PRINT_D(CFG80211_DBG, "Setting PMKID in private structure\n");
		memcpy(priv->pmkid_list.pmkidlist[i].bssid, pmksa->bssid,
			    ETH_ALEN);
		memcpy(priv->pmkid_list.pmkidlist[i].pmkid, pmksa->pmkid,
			    PMKID_LEN);
		if (!(flag == PMKID_FOUND))
			priv->pmkid_list.numpmkid++;
	} else {
		PRINT_ER("Invalid PMKID index\n");
		s32Error = -EINVAL;
	}

	if (!s32Error) {
		PRINT_D(CFG80211_DBG, "Setting pmkid in the host interface\n");
		s32Error = host_int_set_pmkid_info(priv->hWILCWFIDrv, &priv->pmkid_list);
	}
	return s32Error;
}

/**
 *  @brief      del_pmksa
 *  @details    Delete a cached PMKID.
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int del_pmksa(struct wiphy *wiphy, struct net_device *netdev,
		     struct cfg80211_pmksa *pmksa)
{

	u32 i;
	s32 s32Error = 0;

	struct wilc_priv *priv = wiphy_priv(wiphy);

	PRINT_D(CFG80211_DBG, "Deleting PMKSA keys\n");

	for (i = 0; i < priv->pmkid_list.numpmkid; i++)	{
		if (!memcmp(pmksa->bssid, priv->pmkid_list.pmkidlist[i].bssid,
				 ETH_ALEN)) {
			/*If bssid is found, reset the values*/
			PRINT_D(CFG80211_DBG, "Reseting PMKID values\n");
			memset(&priv->pmkid_list.pmkidlist[i], 0, sizeof(struct host_if_pmkid));
			break;
		}
	}

	if (i < priv->pmkid_list.numpmkid && priv->pmkid_list.numpmkid > 0) {
		for (; i < (priv->pmkid_list.numpmkid - 1); i++) {
			memcpy(priv->pmkid_list.pmkidlist[i].bssid,
				    priv->pmkid_list.pmkidlist[i + 1].bssid,
				    ETH_ALEN);
			memcpy(priv->pmkid_list.pmkidlist[i].pmkid,
				    priv->pmkid_list.pmkidlist[i].pmkid,
				    PMKID_LEN);
		}
		priv->pmkid_list.numpmkid--;
	} else {
		s32Error = -EINVAL;
	}

	return s32Error;
}

/**
 *  @brief      flush_pmksa
 *  @details    Flush all cached PMKIDs.
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int flush_pmksa(struct wiphy *wiphy, struct net_device *netdev)
{
	struct wilc_priv *priv = wiphy_priv(wiphy);

	PRINT_D(CFG80211_DBG,  "Flushing  PMKID key values\n");

	/*Get cashed Pmkids and set all with zeros*/
	memset(&priv->pmkid_list, 0, sizeof(struct host_if_pmkid_attr));

	return 0;
}


/**
 *  @brief      WILC_WFI_CfgParseRxAction
 *  @details Function parses the received  frames and modifies the following attributes:
 *                -GO Intent
 *		    -Channel list
 *		    -Operating Channel
 *
 *  @param[in] u8* Buffer, u32 length
 *  @return     NONE.
 *  @author	mdaftedar
 *  @date	12 DEC 2012
 *  @version
 */

void WILC_WFI_CfgParseRxAction(u8 *buf, u32 len)
{
	u32 index = 0;
	u32 i = 0, j = 0;

	u8 op_channel_attr_index = 0;
	u8 channel_list_attr_index = 0;

	while (index < len) {
		if (buf[index] == GO_INTENT_ATTR_ID) {
			buf[index + 3] = (buf[index + 3]  & 0x01) | (0x00 << 1);
		}

		if (buf[index] ==  CHANLIST_ATTR_ID)
			channel_list_attr_index = index;
		else if (buf[index] ==  OPERCHAN_ATTR_ID)
			op_channel_attr_index = index;
		index += buf[index + 1] + 3; /* ID,Length byte */
	}
	if (u8WLANChannel != INVALID_CHANNEL) {

		/*Modify channel list attribute*/
		if (channel_list_attr_index) {
			PRINT_D(GENERIC_DBG, "Modify channel list attribute\n");
			for (i = channel_list_attr_index + 3; i < ((channel_list_attr_index + 3) + buf[channel_list_attr_index + 1]); i++) {
				if (buf[i] == 0x51) {
					for (j = i + 2; j < ((i + 2) + buf[i + 1]); j++) {
						buf[j] = u8WLANChannel;
					}
					break;
				}
			}
		}
		/*Modify operating channel attribute*/
		if (op_channel_attr_index) {
			PRINT_D(GENERIC_DBG, "Modify operating channel attribute\n");
			buf[op_channel_attr_index + 6] = 0x51;
			buf[op_channel_attr_index + 7] = u8WLANChannel;
		}
	}
}

/**
 *  @brief      WILC_WFI_CfgParseTxAction
 *  @details Function parses the transmitted  action frames and modifies the
 *               GO Intent attribute
 *  @param[in] u8* Buffer, u32 length, bool bOperChan, u8 iftype
 *  @return     NONE.
 *  @author	mdaftedar
 *  @date	12 DEC 2012
 *  @version
 */
void WILC_WFI_CfgParseTxAction(u8 *buf, u32 len, bool bOperChan, u8 iftype)
{
	u32 index = 0;
	u32 i = 0, j = 0;

	u8 op_channel_attr_index = 0;
	u8 channel_list_attr_index = 0;

	while (index < len) {
		if (buf[index] == GO_INTENT_ATTR_ID) {
			buf[index + 3] = (buf[index + 3]  & 0x01) | (0x0f << 1);

			break;
		}

		if (buf[index] ==  CHANLIST_ATTR_ID)
			channel_list_attr_index = index;
		else if (buf[index] ==  OPERCHAN_ATTR_ID)
			op_channel_attr_index = index;
		index += buf[index + 1] + 3; /* ID,Length byte */
	}
	if (u8WLANChannel != INVALID_CHANNEL && bOperChan) {

		/*Modify channel list attribute*/
		if (channel_list_attr_index) {
			PRINT_D(GENERIC_DBG, "Modify channel list attribute\n");
			for (i = channel_list_attr_index + 3; i < ((channel_list_attr_index + 3) + buf[channel_list_attr_index + 1]); i++) {
				if (buf[i] == 0x51) {
					for (j = i + 2; j < ((i + 2) + buf[i + 1]); j++) {
						buf[j] = u8WLANChannel;
					}
					break;
				}
			}
		}
		/*Modify operating channel attribute*/
		if (op_channel_attr_index) {
			PRINT_D(GENERIC_DBG, "Modify operating channel attribute\n");
			buf[op_channel_attr_index + 6] = 0x51;
			buf[op_channel_attr_index + 7] = u8WLANChannel;
		}
	}
}

/*  @brief                       WILC_WFI_p2p_rx
 *  @details
 *  @param[in]
 *
 *  @return             None
 *  @author		Mai Daftedar
 *  @date			2 JUN 2013
 *  @version		1.0
 */

void WILC_WFI_p2p_rx (struct net_device *dev, u8 *buff, u32 size)
{

	struct wilc_priv *priv;
	u32 header, pkt_offset;
	struct host_if_drv *pstrWFIDrv;
	u32 i = 0;
	s32 s32Freq;

	priv = wiphy_priv(dev->ieee80211_ptr->wiphy);
	pstrWFIDrv = (struct host_if_drv *)priv->hWILCWFIDrv;

	/* Get WILC header */
	memcpy(&header, (buff - HOST_HDR_OFFSET), HOST_HDR_OFFSET);

	/* The packet offset field conain info about what type of managment frame */
	/* we are dealing with and ack status */
	pkt_offset = GET_PKT_OFFSET(header);

	if (pkt_offset & IS_MANAGMEMENT_CALLBACK) {
		if (buff[FRAME_TYPE_ID] == IEEE80211_STYPE_PROBE_RESP) {
			PRINT_D(GENERIC_DBG, "Probe response ACK\n");
			cfg80211_mgmt_tx_status(priv->wdev, priv->u64tx_cookie, buff, size, true, GFP_KERNEL);
			return;
		} else {
			if (pkt_offset & IS_MGMT_STATUS_SUCCES)	{
				PRINT_D(GENERIC_DBG, "Success Ack - Action frame category: %x Action Subtype: %d Dialog T: %x OR %x\n", buff[ACTION_CAT_ID], buff[ACTION_SUBTYPE_ID],
					buff[ACTION_SUBTYPE_ID + 1], buff[P2P_PUB_ACTION_SUBTYPE + 1]);
				cfg80211_mgmt_tx_status(priv->wdev, priv->u64tx_cookie, buff, size, true, GFP_KERNEL);
			} else {
				PRINT_D(GENERIC_DBG, "Fail Ack - Action frame category: %x Action Subtype: %d Dialog T: %x OR %x\n", buff[ACTION_CAT_ID], buff[ACTION_SUBTYPE_ID],
					buff[ACTION_SUBTYPE_ID + 1], buff[P2P_PUB_ACTION_SUBTYPE + 1]);
				cfg80211_mgmt_tx_status(priv->wdev, priv->u64tx_cookie, buff, size, false, GFP_KERNEL);
			}
			return;
		}
	} else {

		PRINT_D(GENERIC_DBG, "Rx Frame Type:%x\n", buff[FRAME_TYPE_ID]);

		/*Upper layer is informed that the frame is received on this freq*/
		s32Freq = ieee80211_channel_to_frequency(curr_channel, IEEE80211_BAND_2GHZ);

		if (ieee80211_is_action(buff[FRAME_TYPE_ID])) {
			PRINT_D(GENERIC_DBG, "Rx Action Frame Type: %x %x\n", buff[ACTION_SUBTYPE_ID], buff[P2P_PUB_ACTION_SUBTYPE]);

			if (priv->bCfgScanning && time_after_eq(jiffies, (unsigned long)pstrWFIDrv->u64P2p_MgmtTimeout)) {
				PRINT_D(GENERIC_DBG, "Receiving action frames from wrong channels\n");
				return;
			}
			if (buff[ACTION_CAT_ID] == PUB_ACTION_ATTR_ID) {

				switch (buff[ACTION_SUBTYPE_ID]) {
				case GAS_INTIAL_REQ:
					PRINT_D(GENERIC_DBG, "GAS INITIAL REQ %x\n", buff[ACTION_SUBTYPE_ID]);
					break;

				case GAS_INTIAL_RSP:
					PRINT_D(GENERIC_DBG, "GAS INITIAL RSP %x\n", buff[ACTION_SUBTYPE_ID]);
					break;

				case PUBLIC_ACT_VENDORSPEC:
					/*Now we have a public action vendor specific action frame, check if its a p2p public action frame
					 * based on the standard its should have the p2p_oui attribute with the following values 50 6f 9A 09*/
					if (!memcmp(u8P2P_oui, &buff[ACTION_SUBTYPE_ID + 1], 4)) {
						if ((buff[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_REQ || buff[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_RSP))	{
							if (!bWilc_ie) {
								for (i = P2P_PUB_ACTION_SUBTYPE; i < size; i++)	{
									if (!memcmp(u8P2P_vendorspec, &buff[i], 6)) {
										u8P2Precvrandom = buff[i + 6];
										bWilc_ie = true;
										PRINT_D(GENERIC_DBG, "WILC Vendor specific IE:%02x\n", u8P2Precvrandom);
										break;
									}
								}
							}
						}
						if (u8P2Plocalrandom > u8P2Precvrandom)	{
							if ((buff[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_REQ || buff[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_RSP
							      || buff[P2P_PUB_ACTION_SUBTYPE] == P2P_INV_REQ || buff[P2P_PUB_ACTION_SUBTYPE] == P2P_INV_RSP)) {
								for (i = P2P_PUB_ACTION_SUBTYPE + 2; i < size; i++) {
									if (buff[i] == P2PELEM_ATTR_ID && !(memcmp(u8P2P_oui, &buff[i + 2], 4))) {
										WILC_WFI_CfgParseRxAction(&buff[i + 6], size - (i + 6));
										break;
									}
								}
							}
						} else
							PRINT_D(GENERIC_DBG, "PEER WILL BE GO LocaRand=%02x RecvRand %02x\n", u8P2Plocalrandom, u8P2Precvrandom);
					}


					if ((buff[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_REQ || buff[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_RSP) && (bWilc_ie))	{
						PRINT_D(GENERIC_DBG, "Sending P2P to host without extra elemnt\n");
						/* extra attribute for sig_dbm: signal strength in mBm, or 0 if unknown */
						cfg80211_rx_mgmt(priv->wdev, s32Freq, 0, buff, size - 7, 0);
						return;
					}
					break;

				default:
					PRINT_D(GENERIC_DBG, "NOT HANDLED PUBLIC ACTION FRAME TYPE:%x\n", buff[ACTION_SUBTYPE_ID]);
					break;
				}
			}
		}

		cfg80211_rx_mgmt(priv->wdev, s32Freq, 0, buff, size - 7, 0);
	}
}

/**
 *  @brief                      WILC_WFI_mgmt_tx_complete
 *  @details            Returns result of writing mgmt frame to VMM (Tx buffers are freed here)
 *  @param[in]          priv
 *                              transmitting status
 *  @return             None
 *  @author		Amr Abdelmoghny
 *  @date			20 MAY 2013
 *  @version		1.0
 */
static void WILC_WFI_mgmt_tx_complete(void *priv, int status)
{
	struct p2p_mgmt_data *pv_data = (struct p2p_mgmt_data *)priv;


	kfree(pv_data->buff);
	kfree(pv_data);
}

/**
 * @brief               WILC_WFI_RemainOnChannelReady
 *  @details    Callback function, called from handle_remain_on_channel on being ready on channel
 *  @param
 *  @return     none
 *  @author	Amr abdelmoghny
 *  @date		9 JUNE 2013
 *  @version
 */

static void WILC_WFI_RemainOnChannelReady(void *pUserVoid)
{
	struct wilc_priv *priv;

	priv = (struct wilc_priv *)pUserVoid;

	PRINT_D(HOSTINF_DBG, "Remain on channel ready\n");

	priv->bInP2PlistenState = true;

	cfg80211_ready_on_channel(priv->wdev,
				  priv->strRemainOnChanParams.u64ListenCookie,
				  priv->strRemainOnChanParams.pstrListenChan,
				  priv->strRemainOnChanParams.u32ListenDuration,
				  GFP_KERNEL);
}

/**
 * @brief               WILC_WFI_RemainOnChannelExpired
 *  @details    Callback function, called on expiration of remain-on-channel duration
 *  @param
 *  @return     none
 *  @author	Amr abdelmoghny
 *  @date		15 MAY 2013
 *  @version
 */

static void WILC_WFI_RemainOnChannelExpired(void *pUserVoid, u32 u32SessionID)
{
	struct wilc_priv *priv;

	priv = (struct wilc_priv *)pUserVoid;

	if (u32SessionID == priv->strRemainOnChanParams.u32ListenSessionID) {
		PRINT_D(GENERIC_DBG, "Remain on channel expired\n");

		priv->bInP2PlistenState = false;

		/*Inform wpas of remain-on-channel expiration*/
		cfg80211_remain_on_channel_expired(priv->wdev,
						   priv->strRemainOnChanParams.u64ListenCookie,
						   priv->strRemainOnChanParams.pstrListenChan,
						   GFP_KERNEL);
	} else {
		PRINT_D(GENERIC_DBG, "Received ID 0x%x Expected ID 0x%x (No match)\n", u32SessionID
			, priv->strRemainOnChanParams.u32ListenSessionID);
	}
}


/**
 *  @brief      remain_on_channel
 *  @details    Request the driver to remain awake on the specified
 *                      channel for the specified duration to complete an off-channel
 *                      operation (e.g., public action frame exchange). When the driver is
 *                      ready on the requested channel, it must indicate this with an event
 *                      notification by calling cfg80211_ready_on_channel().
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int remain_on_channel(struct wiphy *wiphy,
			     struct wireless_dev *wdev,
			     struct ieee80211_channel *chan,
			     unsigned int duration, u64 *cookie)
{
	s32 s32Error = 0;
	struct wilc_priv *priv;

	priv = wiphy_priv(wiphy);

	PRINT_D(GENERIC_DBG, "Remaining on channel %d\n", chan->hw_value);


	if (wdev->iftype == NL80211_IFTYPE_AP) {
		PRINT_D(GENERIC_DBG, "Required remain-on-channel while in AP mode");
		return s32Error;
	}

	curr_channel = chan->hw_value;

	/*Setting params needed by WILC_WFI_RemainOnChannelExpired()*/
	priv->strRemainOnChanParams.pstrListenChan = chan;
	priv->strRemainOnChanParams.u64ListenCookie = *cookie;
	priv->strRemainOnChanParams.u32ListenDuration = duration;
	priv->strRemainOnChanParams.u32ListenSessionID++;

	s32Error = host_int_remain_on_channel(priv->hWILCWFIDrv
					      , priv->strRemainOnChanParams.u32ListenSessionID
					      , duration
					      , chan->hw_value
					      , WILC_WFI_RemainOnChannelExpired
					      , WILC_WFI_RemainOnChannelReady
					      , (void *)priv);

	return s32Error;
}

/**
 *  @brief      cancel_remain_on_channel
 *  @details    Cancel an on-going remain-on-channel operation.
 *                      This allows the operation to be terminated prior to timeout based on
 *                      the duration value.
 *  @param[in]   struct wiphy *wiphy,
 *  @param[in]  struct net_device *dev
 *  @param[in]  u64 cookie,
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int cancel_remain_on_channel(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    u64 cookie)
{
	s32 s32Error = 0;
	struct wilc_priv *priv;

	priv = wiphy_priv(wiphy);

	PRINT_D(CFG80211_DBG, "Cancel remain on channel\n");

	s32Error = host_int_ListenStateExpired(priv->hWILCWFIDrv, priv->strRemainOnChanParams.u32ListenSessionID);
	return s32Error;
}
/**
 *  @brief      WILC_WFI_mgmt_tx_frame
 *  @details
 *
 *  @param[in]
 *  @return     NONE.
 *  @author	mdaftedar
 *  @date	01 JUL 2012
 *  @version
 */
extern bool bEnablePS;
static int mgmt_tx(struct wiphy *wiphy,
		   struct wireless_dev *wdev,
		   struct cfg80211_mgmt_tx_params *params,
		   u64 *cookie)
{
	struct ieee80211_channel *chan = params->chan;
	unsigned int wait = params->wait;
	const u8 *buf = params->buf;
	size_t len = params->len;
	const struct ieee80211_mgmt *mgmt;
	struct p2p_mgmt_data *mgmt_tx;
	struct wilc_priv *priv;
	struct host_if_drv *pstrWFIDrv;
	u32 i;
	perInterface_wlan_t *nic;
	u32 buf_len = len + sizeof(u8P2P_vendorspec) + sizeof(u8P2Plocalrandom);

	nic = netdev_priv(wdev->netdev);
	priv = wiphy_priv(wiphy);
	pstrWFIDrv = (struct host_if_drv *)priv->hWILCWFIDrv;

	*cookie = (unsigned long)buf;
	priv->u64tx_cookie = *cookie;
	mgmt = (const struct ieee80211_mgmt *) buf;

	if (ieee80211_is_mgmt(mgmt->frame_control)) {

		/*mgmt frame allocation*/
		mgmt_tx = kmalloc(sizeof(struct p2p_mgmt_data), GFP_KERNEL);
		if (mgmt_tx == NULL) {
			PRINT_ER("Failed to allocate memory for mgmt_tx structure\n");
			return -EFAULT;
		}
		mgmt_tx->buff = kmalloc(buf_len, GFP_KERNEL);
		if (mgmt_tx->buff == NULL) {
			PRINT_ER("Failed to allocate memory for mgmt_tx buff\n");
			kfree(mgmt_tx);
			return -EFAULT;
		}
		memcpy(mgmt_tx->buff, buf, len);
		mgmt_tx->size = len;


		if (ieee80211_is_probe_resp(mgmt->frame_control)) {
			PRINT_D(GENERIC_DBG, "TX: Probe Response\n");
			PRINT_D(GENERIC_DBG, "Setting channel: %d\n", chan->hw_value);
			host_int_set_mac_chnl_num(priv->hWILCWFIDrv, chan->hw_value);
			/*Save the current channel after we tune to it*/
			curr_channel = chan->hw_value;
		} else if (ieee80211_is_action(mgmt->frame_control))   {
			PRINT_D(GENERIC_DBG, "ACTION FRAME:%x\n", (u16)mgmt->frame_control);


			if (buf[ACTION_CAT_ID] == PUB_ACTION_ATTR_ID) {
				/*Only set the channel, if not a negotiation confirmation frame
				 * (If Negotiation confirmation frame, force it
				 * to be transmitted on the same negotiation channel)*/

				if (buf[ACTION_SUBTYPE_ID] != PUBLIC_ACT_VENDORSPEC ||
				    buf[P2P_PUB_ACTION_SUBTYPE] != GO_NEG_CONF)	{
					PRINT_D(GENERIC_DBG, "Setting channel: %d\n", chan->hw_value);
					host_int_set_mac_chnl_num(priv->hWILCWFIDrv, chan->hw_value);
					/*Save the current channel after we tune to it*/
					curr_channel = chan->hw_value;
				}
				switch (buf[ACTION_SUBTYPE_ID])	{
				case GAS_INTIAL_REQ:
				{
					PRINT_D(GENERIC_DBG, "GAS INITIAL REQ %x\n", buf[ACTION_SUBTYPE_ID]);
					break;
				}

				case GAS_INTIAL_RSP:
				{
					PRINT_D(GENERIC_DBG, "GAS INITIAL RSP %x\n", buf[ACTION_SUBTYPE_ID]);
					break;
				}

				case PUBLIC_ACT_VENDORSPEC:
				{
					/*Now we have a public action vendor specific action frame, check if its a p2p public action frame
					 * based on the standard its should have the p2p_oui attribute with the following values 50 6f 9A 09*/
					if (!memcmp(u8P2P_oui, &buf[ACTION_SUBTYPE_ID + 1], 4)) {
						/*For the connection of two WILC's connection generate a rand number to determine who will be a GO*/
						if ((buf[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_REQ || buf[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_RSP)) {
							if (u8P2Plocalrandom == 1 && u8P2Precvrandom < u8P2Plocalrandom) {
								get_random_bytes(&u8P2Plocalrandom, 1);
								/*Increment the number to prevent if its 0*/
								u8P2Plocalrandom++;
							}
						}

						if ((buf[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_REQ || buf[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_RSP
						      || buf[P2P_PUB_ACTION_SUBTYPE] == P2P_INV_REQ || buf[P2P_PUB_ACTION_SUBTYPE] == P2P_INV_RSP)) {
							if (u8P2Plocalrandom > u8P2Precvrandom)	{
								PRINT_D(GENERIC_DBG, "LOCAL WILL BE GO LocaRand=%02x RecvRand %02x\n", u8P2Plocalrandom, u8P2Precvrandom);

								/*Search for the p2p information information element , after the Public action subtype theres a byte for teh dialog token, skip that*/
								for (i = P2P_PUB_ACTION_SUBTYPE + 2; i < len; i++) {
									if (buf[i] == P2PELEM_ATTR_ID && !(memcmp(u8P2P_oui, &buf[i + 2], 4))) {
										if (buf[P2P_PUB_ACTION_SUBTYPE] == P2P_INV_REQ || buf[P2P_PUB_ACTION_SUBTYPE] == P2P_INV_RSP)
											WILC_WFI_CfgParseTxAction(&mgmt_tx->buff[i + 6], len - (i + 6), true, nic->iftype);

										/*If using supplicant go intent, no need at all*/
										/*to parse transmitted negotiation frames*/
										else
											WILC_WFI_CfgParseTxAction(&mgmt_tx->buff[i + 6], len - (i + 6), false, nic->iftype);
										break;
									}
								}

								if (buf[P2P_PUB_ACTION_SUBTYPE] != P2P_INV_REQ && buf[P2P_PUB_ACTION_SUBTYPE] != P2P_INV_RSP) {
									/*
									 * Adding WILC information element to allow two WILC devices to
									 * identify each other and connect
									 */
									memcpy(&mgmt_tx->buff[len], u8P2P_vendorspec, sizeof(u8P2P_vendorspec));
									mgmt_tx->buff[len + sizeof(u8P2P_vendorspec)] = u8P2Plocalrandom;
									mgmt_tx->size = buf_len;
								}
							} else
								PRINT_D(GENERIC_DBG, "PEER WILL BE GO LocaRand=%02x RecvRand %02x\n", u8P2Plocalrandom, u8P2Precvrandom);
						}

					} else {
						PRINT_D(GENERIC_DBG, "Not a P2P public action frame\n");
					}

					break;
				}

				default:
				{
					PRINT_D(GENERIC_DBG, "NOT HANDLED PUBLIC ACTION FRAME TYPE:%x\n", buf[ACTION_SUBTYPE_ID]);
					break;
				}
				}

			}

			PRINT_D(GENERIC_DBG, "TX: ACTION FRAME Type:%x : Chan:%d\n", buf[ACTION_SUBTYPE_ID], chan->hw_value);
			pstrWFIDrv->u64P2p_MgmtTimeout = (jiffies + msecs_to_jiffies(wait));

			PRINT_D(GENERIC_DBG, "Current Jiffies: %lu Timeout:%llu\n", jiffies, pstrWFIDrv->u64P2p_MgmtTimeout);

		}

		wilc_wlan_txq_add_mgmt_pkt(mgmt_tx, mgmt_tx->buff,
					   mgmt_tx->size,
					   WILC_WFI_mgmt_tx_complete);
	} else {
		PRINT_D(GENERIC_DBG, "This function transmits only management frames\n");
	}
	return 0;
}

static int mgmt_tx_cancel_wait(struct wiphy *wiphy,
			       struct wireless_dev *wdev,
			       u64 cookie)
{
	struct wilc_priv *priv;
	struct host_if_drv *pstrWFIDrv;

	priv = wiphy_priv(wiphy);
	pstrWFIDrv = (struct host_if_drv *)priv->hWILCWFIDrv;


	PRINT_D(GENERIC_DBG, "Tx Cancel wait :%lu\n", jiffies);
	pstrWFIDrv->u64P2p_MgmtTimeout = jiffies;

	if (!priv->bInP2PlistenState) {
		cfg80211_remain_on_channel_expired(priv->wdev,
						   priv->strRemainOnChanParams.u64ListenCookie,
						   priv->strRemainOnChanParams.pstrListenChan,
						   GFP_KERNEL);
	}

	return 0;
}

/**
 *  @brief      wilc_mgmt_frame_register
 *  @details Notify driver that a management frame type was
 *              registered. Note that this callback may not sleep, and cannot run
 *                      concurrently with itself.
 *  @param[in]
 *  @return     NONE.
 *  @author	mdaftedar
 *  @date	01 JUL 2012
 *  @version
 */
void wilc_mgmt_frame_register(struct wiphy *wiphy, struct wireless_dev *wdev,
			      u16 frame_type, bool reg)
{

	struct wilc_priv *priv;
	perInterface_wlan_t *nic;
	struct wilc *wl;

	priv = wiphy_priv(wiphy);
	nic = netdev_priv(priv->wdev->netdev);
	wl = nic->wilc;

	if (!frame_type)
		return;

	PRINT_D(GENERIC_DBG, "Frame registering Frame Type: %x: Boolean: %d\n", frame_type, reg);
	switch (frame_type) {
	case PROBE_REQ:
	{
		nic->g_struct_frame_reg[0].frame_type = frame_type;
		nic->g_struct_frame_reg[0].reg = reg;
	}
	break;

	case ACTION:
	{
		nic->g_struct_frame_reg[1].frame_type = frame_type;
		nic->g_struct_frame_reg[1].reg = reg;
	}
	break;

	default:
	{
		break;
	}

	}
	/*If mac is closed, then return*/
	if (!wl->initialized) {
		PRINT_D(GENERIC_DBG, "Return since mac is closed\n");
		return;
	}
	host_int_frame_register(priv->hWILCWFIDrv, frame_type, reg);


}

/**
 *  @brief      set_cqm_rssi_config
 *  @details    Configure connection quality monitor RSSI threshold.
 *  @param[in]   struct wiphy *wiphy:
 *  @param[in]	struct net_device *dev:
 *  @param[in]          s32 rssi_thold:
 *  @param[in]	u32 rssi_hyst:
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int set_cqm_rssi_config(struct wiphy *wiphy, struct net_device *dev,
			       s32 rssi_thold, u32 rssi_hyst)
{
	PRINT_D(CFG80211_DBG, "Setting CQM RSSi Function\n");
	return 0;

}
/**
 *  @brief      dump_station
 *  @details    Configure connection quality monitor RSSI threshold.
 *  @param[in]   struct wiphy *wiphy:
 *  @param[in]	struct net_device *dev
 *  @param[in]          int idx
 *  @param[in]	u8 *mac
 *  @param[in]	struct station_info *sinfo
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int dump_station(struct wiphy *wiphy, struct net_device *dev,
			int idx, u8 *mac, struct station_info *sinfo)
{
	struct wilc_priv *priv;

	PRINT_D(CFG80211_DBG, "Dumping station information\n");

	if (idx != 0)
		return -ENOENT;

	priv = wiphy_priv(wiphy);

	sinfo->filled |= BIT(NL80211_STA_INFO_SIGNAL);

	host_int_get_rssi(priv->hWILCWFIDrv, &(sinfo->signal));

	return 0;

}


/**
 *  @brief      set_power_mgmt
 *  @details
 *  @param[in]
 *  @return     int : Return 0 on Success.
 *  @author	mdaftedar
 *  @date	01 JUL 2012
 *  @version	1.0
 */
static int set_power_mgmt(struct wiphy *wiphy, struct net_device *dev,
			  bool enabled, int timeout)
{
	struct wilc_priv *priv;

	PRINT_D(CFG80211_DBG, " Power save Enabled= %d , TimeOut = %d\n", enabled, timeout);

	if (wiphy == NULL)
		return -ENOENT;

	priv = wiphy_priv(wiphy);
	if (priv->hWILCWFIDrv == NULL) {
		PRINT_ER("Driver is NULL\n");
		return -EIO;
	}

	if (bEnablePS)
		host_int_set_power_mgmt(priv->hWILCWFIDrv, enabled, timeout);


	return 0;

}

/**
 *  @brief      change_virtual_intf
 *  @details    Change type/configuration of virtual interface,
 *                      keep the struct wireless_dev's iftype updated.
 *  @param[in]   NONE
 *  @return     int : Return 0 on Success.
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
int wilc1000_wlan_init(struct net_device *dev, perInterface_wlan_t *p_nic);

static int change_virtual_intf(struct wiphy *wiphy, struct net_device *dev,
			       enum nl80211_iftype type, u32 *flags, struct vif_params *params)
{
	struct wilc_priv *priv;
	perInterface_wlan_t *nic;
	u8 interface_type;
	u16 TID = 0;
	u8 i;
	struct wilc *wl;

	nic = netdev_priv(dev);
	priv = wiphy_priv(wiphy);
	wl = nic->wilc;

	PRINT_D(HOSTAPD_DBG, "In Change virtual interface function\n");
	PRINT_D(HOSTAPD_DBG, "Wireless interface name =%s\n", dev->name);
	u8P2Plocalrandom = 0x01;
	u8P2Precvrandom = 0x00;

	bWilc_ie = false;

	g_obtainingIP = false;
	del_timer(&hDuringIpTimer);
	PRINT_D(GENERIC_DBG, "Changing virtual interface, enable scan\n");
	/*Set WILC_CHANGING_VIR_IF register to disallow adding futrue keys to CE H/W*/
	if (g_ptk_keys_saved && g_gtk_keys_saved) {
		Set_machw_change_vir_if(dev, true);
	}

	switch (type) {
	case NL80211_IFTYPE_STATION:
		connecting = 0;
		PRINT_D(HOSTAPD_DBG, "Interface type = NL80211_IFTYPE_STATION\n");

		/* send delba over wlan interface */


		dev->ieee80211_ptr->iftype = type;
		priv->wdev->iftype = type;
		nic->monitor_flag = 0;
		nic->iftype = STATION_MODE;

		/*Remove the enteries of the previously connected clients*/
		memset(priv->assoc_stainfo.au8Sta_AssociatedBss, 0, MAX_NUM_STA * ETH_ALEN);
		interface_type = nic->iftype;
		nic->iftype = STATION_MODE;

		if (wl->initialized) {
			host_int_del_All_Rx_BASession(priv->hWILCWFIDrv,
						      wl->vif[0].bssid, TID);
			/* ensure that the message Q is empty */
			host_int_wait_msg_queue_idle();

			/*Eliminate host interface blocking state*/
			up(&wl->cfg_event);

			wilc1000_wlan_deinit(dev);
			wilc1000_wlan_init(dev, nic);
			g_wilc_initialized = 1;
			nic->iftype = interface_type;

			/*Setting interface 1 drv handler and mac address in newly downloaded FW*/
			host_int_set_wfi_drv_handler(wl->vif[0].hif_drv);
			host_int_set_MacAddress(wl->vif[0].hif_drv,
						wl->vif[0].src_addr);
			host_int_set_operation_mode(priv->hWILCWFIDrv, STATION_MODE);

			/*Add saved WEP keys, if any*/
			if (g_wep_keys_saved) {
				host_int_set_wep_default_key(wl->vif[0].hif_drv,
							     g_key_wep_params.key_idx);
				host_int_add_wep_key_bss_sta(wl->vif[0].hif_drv,
							     g_key_wep_params.key,
							     g_key_wep_params.key_len,
							     g_key_wep_params.key_idx);
			}

			/*No matter the driver handler passed here, it will be overwriiten*/
			/*in Handle_FlushConnect() with gu8FlushedJoinReqDrvHandler*/
			host_int_flush_join_req(priv->hWILCWFIDrv);

			/*Add saved PTK and GTK keys, if any*/
			if (g_ptk_keys_saved && g_gtk_keys_saved) {
				PRINT_D(CFG80211_DBG, "ptk %x %x %x\n", g_key_ptk_params.key[0],
					g_key_ptk_params.key[1],
					g_key_ptk_params.key[2]);
				PRINT_D(CFG80211_DBG, "gtk %x %x %x\n", g_key_gtk_params.key[0],
					g_key_gtk_params.key[1],
					g_key_gtk_params.key[2]);
				add_key(wl->vif[0].ndev->ieee80211_ptr->wiphy,
					wl->vif[0].ndev,
					g_add_ptk_key_params.key_idx,
					g_add_ptk_key_params.pairwise,
					g_add_ptk_key_params.mac_addr,
					(struct key_params *)(&g_key_ptk_params));

				add_key(wl->vif[0].ndev->ieee80211_ptr->wiphy,
					wl->vif[0].ndev,
					g_add_gtk_key_params.key_idx,
					g_add_gtk_key_params.pairwise,
					g_add_gtk_key_params.mac_addr,
					(struct key_params *)(&g_key_gtk_params));
			}

			if (wl->initialized)	{
				for (i = 0; i < num_reg_frame; i++) {
					PRINT_D(INIT_DBG, "Frame registering Type: %x - Reg: %d\n", nic->g_struct_frame_reg[i].frame_type,
						nic->g_struct_frame_reg[i].reg);
					host_int_frame_register(priv->hWILCWFIDrv,
								nic->g_struct_frame_reg[i].frame_type,
								nic->g_struct_frame_reg[i].reg);
				}
			}

			bEnablePS = true;
			host_int_set_power_mgmt(priv->hWILCWFIDrv, 1, 0);
		}
		break;

	case NL80211_IFTYPE_P2P_CLIENT:
		bEnablePS = false;
		host_int_set_power_mgmt(priv->hWILCWFIDrv, 0, 0);
		connecting = 0;
		PRINT_D(HOSTAPD_DBG, "Interface type = NL80211_IFTYPE_P2P_CLIENT\n");

		host_int_del_All_Rx_BASession(priv->hWILCWFIDrv,
					      wl->vif[0].bssid, TID);

		dev->ieee80211_ptr->iftype = type;
		priv->wdev->iftype = type;
		nic->monitor_flag = 0;

		PRINT_D(HOSTAPD_DBG, "Downloading P2P_CONCURRENCY_FIRMWARE\n");
		nic->iftype = CLIENT_MODE;


		if (wl->initialized)	{
			/* ensure that the message Q is empty */
			host_int_wait_msg_queue_idle();

			wilc1000_wlan_deinit(dev);
			wilc1000_wlan_init(dev, nic);
			g_wilc_initialized = 1;

			host_int_set_wfi_drv_handler(wl->vif[0].hif_drv);
			host_int_set_MacAddress(wl->vif[0].hif_drv,
						wl->vif[0].src_addr);
			host_int_set_operation_mode(priv->hWILCWFIDrv, STATION_MODE);

			/*Add saved WEP keys, if any*/
			if (g_wep_keys_saved) {
				host_int_set_wep_default_key(wl->vif[0].hif_drv,
							     g_key_wep_params.key_idx);
				host_int_add_wep_key_bss_sta(wl->vif[0].hif_drv,
							     g_key_wep_params.key,
							     g_key_wep_params.key_len,
							     g_key_wep_params.key_idx);
			}

			/*No matter the driver handler passed here, it will be overwriiten*/
			/*in Handle_FlushConnect() with gu8FlushedJoinReqDrvHandler*/
			host_int_flush_join_req(priv->hWILCWFIDrv);

			/*Add saved PTK and GTK keys, if any*/
			if (g_ptk_keys_saved && g_gtk_keys_saved) {
				PRINT_D(CFG80211_DBG, "ptk %x %x %x\n", g_key_ptk_params.key[0],
					g_key_ptk_params.key[1],
					g_key_ptk_params.key[2]);
				PRINT_D(CFG80211_DBG, "gtk %x %x %x\n", g_key_gtk_params.key[0],
					g_key_gtk_params.key[1],
					g_key_gtk_params.key[2]);
				add_key(wl->vif[0].ndev->ieee80211_ptr->wiphy,
					wl->vif[0].ndev,
					g_add_ptk_key_params.key_idx,
					g_add_ptk_key_params.pairwise,
					g_add_ptk_key_params.mac_addr,
					(struct key_params *)(&g_key_ptk_params));

				add_key(wl->vif[0].ndev->ieee80211_ptr->wiphy,
					wl->vif[0].ndev,
					g_add_gtk_key_params.key_idx,
					g_add_gtk_key_params.pairwise,
					g_add_gtk_key_params.mac_addr,
					(struct key_params *)(&g_key_gtk_params));
			}

			/*Refresh scan, to refresh the scan results to the wpa_supplicant. Set MachHw to false to enable further key installments*/
			refresh_scan(priv, 1, true);
			Set_machw_change_vir_if(dev, false);

			if (wl->initialized)	{
				for (i = 0; i < num_reg_frame; i++) {
					PRINT_D(INIT_DBG, "Frame registering Type: %x - Reg: %d\n", nic->g_struct_frame_reg[i].frame_type,
						nic->g_struct_frame_reg[i].reg);
					host_int_frame_register(priv->hWILCWFIDrv,
								nic->g_struct_frame_reg[i].frame_type,
								nic->g_struct_frame_reg[i].reg);
				}
			}
		}
		break;

	case NL80211_IFTYPE_AP:
		bEnablePS = false;
		PRINT_D(HOSTAPD_DBG, "Interface type = NL80211_IFTYPE_AP %d\n", type);
		dev->ieee80211_ptr->iftype = type;
		priv->wdev->iftype = type;
		nic->iftype = AP_MODE;
		PRINT_D(CORECONFIG_DBG, "priv->hWILCWFIDrv[%p]\n", priv->hWILCWFIDrv);

		PRINT_D(HOSTAPD_DBG, "Downloading AP firmware\n");
		linux_wlan_get_firmware(nic);
		/*If wilc is running, then close-open to actually get new firmware running (serves P2P)*/
		if (wl->initialized)	{
			nic->iftype = AP_MODE;
			mac_close(dev);
			mac_open(dev);

			for (i = 0; i < num_reg_frame; i++) {
				PRINT_D(INIT_DBG, "Frame registering Type: %x - Reg: %d\n", nic->g_struct_frame_reg[i].frame_type,
					nic->g_struct_frame_reg[i].reg);
				host_int_frame_register(priv->hWILCWFIDrv,
							nic->g_struct_frame_reg[i].frame_type,
							nic->g_struct_frame_reg[i].reg);
			}
		}
		break;

	case NL80211_IFTYPE_P2P_GO:
		PRINT_D(GENERIC_DBG, "start duringIP timer\n");

		g_obtainingIP = true;
		mod_timer(&hDuringIpTimer, jiffies + msecs_to_jiffies(duringIP_TIME));
		host_int_set_power_mgmt(priv->hWILCWFIDrv, 0, 0);
		/*Delete block ack has to be the latest config packet*/
		/*sent before downloading new FW. This is because it blocks on*/
		/*hWaitResponse semaphore, which allows previous config*/
		/*packets to actually take action on old FW*/
		host_int_del_All_Rx_BASession(priv->hWILCWFIDrv,
					      wl->vif[0].bssid, TID);
		bEnablePS = false;
		PRINT_D(HOSTAPD_DBG, "Interface type = NL80211_IFTYPE_GO\n");
		dev->ieee80211_ptr->iftype = type;
		priv->wdev->iftype = type;

		PRINT_D(CORECONFIG_DBG, "priv->hWILCWFIDrv[%p]\n", priv->hWILCWFIDrv);

		PRINT_D(HOSTAPD_DBG, "Downloading P2P_CONCURRENCY_FIRMWARE\n");


		nic->iftype = GO_MODE;

		/* ensure that the message Q is empty */
		host_int_wait_msg_queue_idle();
		wilc1000_wlan_deinit(dev);
		wilc1000_wlan_init(dev, nic);
		g_wilc_initialized = 1;


		/*Setting interface 1 drv handler and mac address in newly downloaded FW*/
		host_int_set_wfi_drv_handler(wl->vif[0].hif_drv);
		host_int_set_MacAddress(wl->vif[0].hif_drv,
					wl->vif[0].src_addr);
		host_int_set_operation_mode(priv->hWILCWFIDrv, AP_MODE);

		/*Add saved WEP keys, if any*/
		if (g_wep_keys_saved) {
			host_int_set_wep_default_key(wl->vif[0].hif_drv,
						     g_key_wep_params.key_idx);
			host_int_add_wep_key_bss_sta(wl->vif[0].hif_drv,
						     g_key_wep_params.key,
						     g_key_wep_params.key_len,
						     g_key_wep_params.key_idx);
		}

		/*No matter the driver handler passed here, it will be overwriiten*/
		/*in Handle_FlushConnect() with gu8FlushedJoinReqDrvHandler*/
		host_int_flush_join_req(priv->hWILCWFIDrv);

		/*Add saved PTK and GTK keys, if any*/
		if (g_ptk_keys_saved && g_gtk_keys_saved) {
			PRINT_D(CFG80211_DBG, "ptk %x %x %x cipher %x\n", g_key_ptk_params.key[0],
				g_key_ptk_params.key[1],
				g_key_ptk_params.key[2],
				g_key_ptk_params.cipher);
			PRINT_D(CFG80211_DBG, "gtk %x %x %x cipher %x\n", g_key_gtk_params.key[0],
				g_key_gtk_params.key[1],
				g_key_gtk_params.key[2],
				g_key_gtk_params.cipher);
			add_key(wl->vif[0].ndev->ieee80211_ptr->wiphy,
				wl->vif[0].ndev,
				g_add_ptk_key_params.key_idx,
				g_add_ptk_key_params.pairwise,
				g_add_ptk_key_params.mac_addr,
				(struct key_params *)(&g_key_ptk_params));

			add_key(wl->vif[0].ndev->ieee80211_ptr->wiphy,
				wl->vif[0].ndev,
				g_add_gtk_key_params.key_idx,
				g_add_gtk_key_params.pairwise,
				g_add_gtk_key_params.mac_addr,
				(struct key_params *)(&g_key_gtk_params));
		}

		if (wl->initialized)	{
			for (i = 0; i < num_reg_frame; i++) {
				PRINT_D(INIT_DBG, "Frame registering Type: %x - Reg: %d\n", nic->g_struct_frame_reg[i].frame_type,
					nic->g_struct_frame_reg[i].reg);
				host_int_frame_register(priv->hWILCWFIDrv,
							nic->g_struct_frame_reg[i].frame_type,
							nic->g_struct_frame_reg[i].reg);
			}
		}
		break;

	default:
		PRINT_ER("Unknown interface type= %d\n", type);
		return -EINVAL;
	}

	return 0;
}

/* (austin.2013-07-23)
 *
 *      To support revised cfg80211_ops
 *
 *              add_beacon --> start_ap
 *              set_beacon --> change_beacon
 *              del_beacon --> stop_ap
 *
 *              beacon_parameters  -->	cfg80211_ap_settings
 *                                                              cfg80211_beacon_data
 *
 *      applicable for linux kernel 3.4+
 */

/**
 *  @brief      start_ap
 *  @details    Add a beacon with given parameters, @head, @interval
 *                      and @dtim_period will be valid, @tail is optional.
 *  @param[in]   wiphy
 *  @param[in]   dev	The net device structure
 *  @param[in]   settings	cfg80211_ap_settings parameters for the beacon to be added
 *  @return     int : Return 0 on Success.
 *  @author	austin
 *  @date	23 JUL 2013
 *  @version	1.0
 */
static int start_ap(struct wiphy *wiphy, struct net_device *dev,
		    struct cfg80211_ap_settings *settings)
{
	struct cfg80211_beacon_data *beacon = &(settings->beacon);
	struct wilc_priv *priv;
	s32 s32Error = 0;
	struct wilc *wl;
	perInterface_wlan_t *nic;

	priv = wiphy_priv(wiphy);
	nic = netdev_priv(dev);
	wl = nic->wilc;
	PRINT_D(HOSTAPD_DBG, "Starting ap\n");

	PRINT_D(HOSTAPD_DBG, "Interval = %d\n DTIM period = %d\n Head length = %zu Tail length = %zu\n",
		settings->beacon_interval, settings->dtim_period, beacon->head_len, beacon->tail_len);

	s32Error = set_channel(wiphy, &settings->chandef);

	if (s32Error != 0)
		PRINT_ER("Error in setting channel\n");

	linux_wlan_set_bssid(dev, wl->vif[0].src_addr);

	s32Error = host_int_add_beacon(priv->hWILCWFIDrv,
					settings->beacon_interval,
					settings->dtim_period,
					beacon->head_len, (u8 *)beacon->head,
					beacon->tail_len, (u8 *)beacon->tail);

	return s32Error;
}

/**
 *  @brief      change_beacon
 *  @details    Add a beacon with given parameters, @head, @interval
 *                      and @dtim_period will be valid, @tail is optional.
 *  @param[in]   wiphy
 *  @param[in]   dev	The net device structure
 *  @param[in]   beacon	cfg80211_beacon_data for the beacon to be changed
 *  @return     int : Return 0 on Success.
 *  @author	austin
 *  @date	23 JUL 2013
 *  @version	1.0
 */
static int change_beacon(struct wiphy *wiphy, struct net_device *dev,
			 struct cfg80211_beacon_data *beacon)
{
	struct wilc_priv *priv;
	s32 s32Error = 0;

	priv = wiphy_priv(wiphy);
	PRINT_D(HOSTAPD_DBG, "Setting beacon\n");


	s32Error = host_int_add_beacon(priv->hWILCWFIDrv,
					0,
					0,
					beacon->head_len, (u8 *)beacon->head,
					beacon->tail_len, (u8 *)beacon->tail);

	return s32Error;
}

/**
 *  @brief      stop_ap
 *  @details    Remove beacon configuration and stop sending the beacon.
 *  @param[in]
 *  @return     int : Return 0 on Success.
 *  @author	austin
 *  @date	23 JUL 2013
 *  @version	1.0
 */
static int stop_ap(struct wiphy *wiphy, struct net_device *dev)
{
	s32 s32Error = 0;
	struct wilc_priv *priv;
	u8 NullBssid[ETH_ALEN] = {0};

	if (!wiphy)
		return -EFAULT;

	priv = wiphy_priv(wiphy);

	PRINT_D(HOSTAPD_DBG, "Deleting beacon\n");

	linux_wlan_set_bssid(dev, NullBssid);

	s32Error = host_int_del_beacon(priv->hWILCWFIDrv);

	if (s32Error)
		PRINT_ER("Host delete beacon fail\n");

	return s32Error;
}

/**
 *  @brief      add_station
 *  @details    Add a new station.
 *  @param[in]
 *  @return     int : Return 0 on Success.
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int add_station(struct wiphy *wiphy, struct net_device *dev,
		       const u8 *mac, struct station_parameters *params)
{
	s32 s32Error = 0;
	struct wilc_priv *priv;
	struct add_sta_param strStaParams = { {0} };
	perInterface_wlan_t *nic;

	if (!wiphy)
		return -EFAULT;

	priv = wiphy_priv(wiphy);
	nic = netdev_priv(dev);

	if (nic->iftype == AP_MODE || nic->iftype == GO_MODE) {
		memcpy(strStaParams.au8BSSID, mac, ETH_ALEN);
		memcpy(priv->assoc_stainfo.au8Sta_AssociatedBss[params->aid], mac, ETH_ALEN);
		strStaParams.u16AssocID = params->aid;
		strStaParams.u8NumRates = params->supported_rates_len;
		strStaParams.pu8Rates = params->supported_rates;

		PRINT_D(CFG80211_DBG, "Adding station parameters %d\n", params->aid);

		PRINT_D(CFG80211_DBG, "BSSID = %x%x%x%x%x%x\n", priv->assoc_stainfo.au8Sta_AssociatedBss[params->aid][0], priv->assoc_stainfo.au8Sta_AssociatedBss[params->aid][1], priv->assoc_stainfo.au8Sta_AssociatedBss[params->aid][2], priv->assoc_stainfo.au8Sta_AssociatedBss[params->aid][3], priv->assoc_stainfo.au8Sta_AssociatedBss[params->aid][4],
			priv->assoc_stainfo.au8Sta_AssociatedBss[params->aid][5]);
		PRINT_D(HOSTAPD_DBG, "ASSOC ID = %d\n", strStaParams.u16AssocID);
		PRINT_D(HOSTAPD_DBG, "Number of supported rates = %d\n", strStaParams.u8NumRates);

		if (params->ht_capa == NULL) {
			strStaParams.bIsHTSupported = false;
		} else {
			strStaParams.bIsHTSupported = true;
			strStaParams.u16HTCapInfo = params->ht_capa->cap_info;
			strStaParams.u8AmpduParams = params->ht_capa->ampdu_params_info;
			memcpy(strStaParams.au8SuppMCsSet, &params->ht_capa->mcs, WILC_SUPP_MCS_SET_SIZE);
			strStaParams.u16HTExtParams = params->ht_capa->extended_ht_cap_info;
			strStaParams.u32TxBeamformingCap = params->ht_capa->tx_BF_cap_info;
			strStaParams.u8ASELCap = params->ht_capa->antenna_selection_info;
		}

		strStaParams.u16FlagsMask = params->sta_flags_mask;
		strStaParams.u16FlagsSet = params->sta_flags_set;

		PRINT_D(HOSTAPD_DBG, "IS HT supported = %d\n", strStaParams.bIsHTSupported);
		PRINT_D(HOSTAPD_DBG, "Capability Info = %d\n", strStaParams.u16HTCapInfo);
		PRINT_D(HOSTAPD_DBG, "AMPDU Params = %d\n", strStaParams.u8AmpduParams);
		PRINT_D(HOSTAPD_DBG, "HT Extended params = %d\n", strStaParams.u16HTExtParams);
		PRINT_D(HOSTAPD_DBG, "Tx Beamforming Cap = %d\n", strStaParams.u32TxBeamformingCap);
		PRINT_D(HOSTAPD_DBG, "Antenna selection info = %d\n", strStaParams.u8ASELCap);
		PRINT_D(HOSTAPD_DBG, "Flag Mask = %d\n", strStaParams.u16FlagsMask);
		PRINT_D(HOSTAPD_DBG, "Flag Set = %d\n", strStaParams.u16FlagsSet);

		s32Error = host_int_add_station(priv->hWILCWFIDrv, &strStaParams);
		if (s32Error)
			PRINT_ER("Host add station fail\n");
	}

	return s32Error;
}

/**
 *  @brief      del_station
 *  @details    Remove a station; @mac may be NULL to remove all stations.
 *  @param[in]
 *  @return     int : Return 0 on Success.
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int del_station(struct wiphy *wiphy, struct net_device *dev,
		       struct station_del_parameters *params)
{
	const u8 *mac = params->mac;
	s32 s32Error = 0;
	struct wilc_priv *priv;
	perInterface_wlan_t *nic;

	if (!wiphy)
		return -EFAULT;

	priv = wiphy_priv(wiphy);
	nic = netdev_priv(dev);

	if (nic->iftype == AP_MODE || nic->iftype == GO_MODE) {
		PRINT_D(HOSTAPD_DBG, "Deleting station\n");


		if (mac == NULL) {
			PRINT_D(HOSTAPD_DBG, "All associated stations\n");
			s32Error = host_int_del_allstation(priv->hWILCWFIDrv, priv->assoc_stainfo.au8Sta_AssociatedBss);
		} else {
			PRINT_D(HOSTAPD_DBG, "With mac address: %x%x%x%x%x%x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		}

		s32Error = host_int_del_station(priv->hWILCWFIDrv, mac);

		if (s32Error)
			PRINT_ER("Host delete station fail\n");
	}
	return s32Error;
}

/**
 *  @brief      change_station
 *  @details    Modify a given station.
 *  @param[in]
 *  @return     int : Return 0 on Success.
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
static int change_station(struct wiphy *wiphy, struct net_device *dev,
			  const u8 *mac, struct station_parameters *params)
{
	s32 s32Error = 0;
	struct wilc_priv *priv;
	struct add_sta_param strStaParams = { {0} };
	perInterface_wlan_t *nic;


	PRINT_D(HOSTAPD_DBG, "Change station paramters\n");

	if (!wiphy)
		return -EFAULT;

	priv = wiphy_priv(wiphy);
	nic = netdev_priv(dev);

	if (nic->iftype == AP_MODE || nic->iftype == GO_MODE) {
		memcpy(strStaParams.au8BSSID, mac, ETH_ALEN);
		strStaParams.u16AssocID = params->aid;
		strStaParams.u8NumRates = params->supported_rates_len;
		strStaParams.pu8Rates = params->supported_rates;

		PRINT_D(HOSTAPD_DBG, "BSSID = %x%x%x%x%x%x\n", strStaParams.au8BSSID[0], strStaParams.au8BSSID[1], strStaParams.au8BSSID[2], strStaParams.au8BSSID[3], strStaParams.au8BSSID[4],
			strStaParams.au8BSSID[5]);
		PRINT_D(HOSTAPD_DBG, "ASSOC ID = %d\n", strStaParams.u16AssocID);
		PRINT_D(HOSTAPD_DBG, "Number of supported rates = %d\n", strStaParams.u8NumRates);

		if (params->ht_capa == NULL) {
			strStaParams.bIsHTSupported = false;
		} else {
			strStaParams.bIsHTSupported = true;
			strStaParams.u16HTCapInfo = params->ht_capa->cap_info;
			strStaParams.u8AmpduParams = params->ht_capa->ampdu_params_info;
			memcpy(strStaParams.au8SuppMCsSet, &params->ht_capa->mcs, WILC_SUPP_MCS_SET_SIZE);
			strStaParams.u16HTExtParams = params->ht_capa->extended_ht_cap_info;
			strStaParams.u32TxBeamformingCap = params->ht_capa->tx_BF_cap_info;
			strStaParams.u8ASELCap = params->ht_capa->antenna_selection_info;

		}

		strStaParams.u16FlagsMask = params->sta_flags_mask;
		strStaParams.u16FlagsSet = params->sta_flags_set;

		PRINT_D(HOSTAPD_DBG, "IS HT supported = %d\n", strStaParams.bIsHTSupported);
		PRINT_D(HOSTAPD_DBG, "Capability Info = %d\n", strStaParams.u16HTCapInfo);
		PRINT_D(HOSTAPD_DBG, "AMPDU Params = %d\n", strStaParams.u8AmpduParams);
		PRINT_D(HOSTAPD_DBG, "HT Extended params = %d\n", strStaParams.u16HTExtParams);
		PRINT_D(HOSTAPD_DBG, "Tx Beamforming Cap = %d\n", strStaParams.u32TxBeamformingCap);
		PRINT_D(HOSTAPD_DBG, "Antenna selection info = %d\n", strStaParams.u8ASELCap);
		PRINT_D(HOSTAPD_DBG, "Flag Mask = %d\n", strStaParams.u16FlagsMask);
		PRINT_D(HOSTAPD_DBG, "Flag Set = %d\n", strStaParams.u16FlagsSet);

		s32Error = host_int_edit_station(priv->hWILCWFIDrv, &strStaParams);
		if (s32Error)
			PRINT_ER("Host edit station fail\n");
	}
	return s32Error;
}


/**
 *  @brief      add_virtual_intf
 *  @details
 *  @param[in]
 *  @return     int : Return 0 on Success.
 *  @author	mdaftedar
 *  @date	01 JUL 2012
 *  @version	1.0
 */
static struct wireless_dev *add_virtual_intf(struct wiphy *wiphy,
					     const char *name,
					     unsigned char name_assign_type,
					     enum nl80211_iftype type,
					     u32 *flags,
					     struct vif_params *params)
{
	perInterface_wlan_t *nic;
	struct wilc_priv *priv;
	struct net_device *new_ifc = NULL;

	priv = wiphy_priv(wiphy);



	PRINT_D(HOSTAPD_DBG, "Adding monitor interface[%p]\n", priv->wdev->netdev);

	nic = netdev_priv(priv->wdev->netdev);


	if (type == NL80211_IFTYPE_MONITOR) {
		PRINT_D(HOSTAPD_DBG, "Monitor interface mode: Initializing mon interface virtual device driver\n");
		PRINT_D(HOSTAPD_DBG, "Adding monitor interface[%p]\n", nic->wilc_netdev);
		new_ifc = WILC_WFI_init_mon_interface(name, nic->wilc_netdev);
		if (new_ifc != NULL) {
			PRINT_D(HOSTAPD_DBG, "Setting monitor flag in private structure\n");
			nic = netdev_priv(priv->wdev->netdev);
			nic->monitor_flag = 1;
		} else
			PRINT_ER("Error in initializing monitor interface\n ");
	}
	return priv->wdev;
}

/**
 *  @brief      del_virtual_intf
 *  @details
 *  @param[in]
 *  @return     int : Return 0 on Success.
 *  @author	mdaftedar
 *  @date	01 JUL 2012
 *  @version	1.0
 */
static int del_virtual_intf(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	PRINT_D(HOSTAPD_DBG, "Deleting virtual interface\n");
	return 0;
}

static struct cfg80211_ops wilc_cfg80211_ops = {

	.set_monitor_channel = set_channel,
	.scan = scan,
	.connect = connect,
	.disconnect = disconnect,
	.add_key = add_key,
	.del_key = del_key,
	.get_key = get_key,
	.set_default_key = set_default_key,
	.add_virtual_intf = add_virtual_intf,
	.del_virtual_intf = del_virtual_intf,
	.change_virtual_intf = change_virtual_intf,

	.start_ap = start_ap,
	.change_beacon = change_beacon,
	.stop_ap = stop_ap,
	.add_station = add_station,
	.del_station = del_station,
	.change_station = change_station,
	.get_station = get_station,
	.dump_station = dump_station,
	.change_bss = change_bss,
	.set_wiphy_params = set_wiphy_params,

	.set_pmksa = set_pmksa,
	.del_pmksa = del_pmksa,
	.flush_pmksa = flush_pmksa,
	.remain_on_channel = remain_on_channel,
	.cancel_remain_on_channel = cancel_remain_on_channel,
	.mgmt_tx_cancel_wait = mgmt_tx_cancel_wait,
	.mgmt_tx = mgmt_tx,
	.mgmt_frame_register = wilc_mgmt_frame_register,
	.set_power_mgmt = set_power_mgmt,
	.set_cqm_rssi_config = set_cqm_rssi_config,

};





/**
 *  @brief      WILC_WFI_update_stats
 *  @details    Modify parameters for a given BSS.
 *  @param[in]
 *  @return     int : Return 0 on Success.
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
int WILC_WFI_update_stats(struct wiphy *wiphy, u32 pktlen, u8 changed)
{

	struct wilc_priv *priv;

	priv = wiphy_priv(wiphy);
	switch (changed) {

	case WILC_WFI_RX_PKT:
	{
		priv->netstats.rx_packets++;
		priv->netstats.rx_bytes += pktlen;
		priv->netstats.rx_time = get_jiffies_64();
	}
	break;

	case WILC_WFI_TX_PKT:
	{
		priv->netstats.tx_packets++;
		priv->netstats.tx_bytes += pktlen;
		priv->netstats.tx_time = get_jiffies_64();

	}
	break;

	default:
		break;
	}
	return 0;
}

/**
 *  @brief      WILC_WFI_CfgAlloc
 *  @details    Allocation of the wireless device structure and assigning it
 *		to the cfg80211 operations structure.
 *  @param[in]   NONE
 *  @return     wireless_dev : Returns pointer to wireless_dev structure.
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
struct wireless_dev *WILC_WFI_CfgAlloc(void)
{

	struct wireless_dev *wdev;


	PRINT_D(CFG80211_DBG, "Allocating wireless device\n");
	/*Allocating the wireless device structure*/
	wdev = kzalloc(sizeof(struct wireless_dev), GFP_KERNEL);
	if (!wdev) {
		PRINT_ER("Cannot allocate wireless device\n");
		goto _fail_;
	}

	/*Creating a new wiphy, linking wireless structure with the wiphy structure*/
	wdev->wiphy = wiphy_new(&wilc_cfg80211_ops, sizeof(struct wilc_priv));
	if (!wdev->wiphy) {
		PRINT_ER("Cannot allocate wiphy\n");
		goto _fail_mem_;

	}

	/* enable 802.11n HT */
	WILC_WFI_band_2ghz.ht_cap.ht_supported = 1;
	WILC_WFI_band_2ghz.ht_cap.cap |= (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);
	WILC_WFI_band_2ghz.ht_cap.mcs.rx_mask[0] = 0xff;
	WILC_WFI_band_2ghz.ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K;
	WILC_WFI_band_2ghz.ht_cap.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE;

	/*wiphy bands*/
	wdev->wiphy->bands[IEEE80211_BAND_2GHZ] = &WILC_WFI_band_2ghz;

	return wdev;

_fail_mem_:
	kfree(wdev);
_fail_:
	return NULL;

}
/**
 *  @brief      wilc_create_wiphy
 *  @details    Registering of the wiphy structure and interface modes
 *  @param[in]   NONE
 *  @return     NONE
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
struct wireless_dev *wilc_create_wiphy(struct net_device *net)
{
	struct wilc_priv *priv;
	struct wireless_dev *wdev;
	s32 s32Error = 0;

	PRINT_D(CFG80211_DBG, "Registering wifi device\n");

	wdev = WILC_WFI_CfgAlloc();
	if (wdev == NULL) {
		PRINT_ER("CfgAlloc Failed\n");
		return NULL;
	}


	/*Return hardware description structure (wiphy)'s priv*/
	priv = wdev_priv(wdev);
	sema_init(&(priv->SemHandleUpdateStats), 1);

	/*Link the wiphy with wireless structure*/
	priv->wdev = wdev;

	/*Maximum number of probed ssid to be added by user for the scan request*/
	wdev->wiphy->max_scan_ssids = MAX_NUM_PROBED_SSID;
	/*Maximum number of pmkids to be cashed*/
	wdev->wiphy->max_num_pmkids = WILC_MAX_NUM_PMKIDS;
	PRINT_INFO(CFG80211_DBG, "Max number of PMKIDs = %d\n", wdev->wiphy->max_num_pmkids);

	wdev->wiphy->max_scan_ie_len = 1000;

	/*signal strength in mBm (100*dBm) */
	wdev->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	/*Set the availaible cipher suites*/
	wdev->wiphy->cipher_suites = cipher_suites;
	wdev->wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);
	/*Setting default managment types: for register action frame:  */
	wdev->wiphy->mgmt_stypes = wilc_wfi_cfg80211_mgmt_types;

	wdev->wiphy->max_remain_on_channel_duration = 500;
	/*Setting the wiphy interfcae mode and type before registering the wiphy*/
	wdev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP) | BIT(NL80211_IFTYPE_MONITOR) | BIT(NL80211_IFTYPE_P2P_GO) |
		BIT(NL80211_IFTYPE_P2P_CLIENT);
	wdev->wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
	wdev->iftype = NL80211_IFTYPE_STATION;



	PRINT_INFO(CFG80211_DBG, "Max scan ids = %d,Max scan IE len = %d,Signal Type = %d,Interface Modes = %d,Interface Type = %d\n",
		   wdev->wiphy->max_scan_ssids, wdev->wiphy->max_scan_ie_len, wdev->wiphy->signal_type,
		   wdev->wiphy->interface_modes, wdev->iftype);

	#ifdef WILC_SDIO
	set_wiphy_dev(wdev->wiphy, &local_sdio_func->dev);
	#endif

	/*Register wiphy structure*/
	s32Error = wiphy_register(wdev->wiphy);
	if (s32Error) {
		PRINT_ER("Cannot register wiphy device\n");
		/*should define what action to be taken in such failure*/
	} else {
		PRINT_D(CFG80211_DBG, "Successful Registering\n");
	}

	priv->dev = net;
	return wdev;


}
/**
 *  @brief      WILC_WFI_WiphyFree
 *  @details    Freeing allocation of the wireless device structure
 *  @param[in]   NONE
 *  @return     NONE
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
int wilc_init_host_int(struct net_device *net)
{

	int s32Error = 0;

	struct wilc_priv *priv;

	PRINT_D(INIT_DBG, "Host[%p][%p]\n", net, net->ieee80211_ptr);
	priv = wdev_priv(net->ieee80211_ptr);
	if (op_ifcs == 0) {
		setup_timer(&hAgingTimer, remove_network_from_shadow, 0);
		setup_timer(&hDuringIpTimer, clear_duringIP, 0);
	}
	op_ifcs++;
	if (s32Error < 0) {
		PRINT_ER("Failed to creat refresh Timer\n");
		return s32Error;
	}

	priv->gbAutoRateAdjusted = false;

	priv->bInP2PlistenState = false;

	sema_init(&(priv->hSemScanReq), 1);
	s32Error = host_int_init(net, &priv->hWILCWFIDrv);
	if (s32Error)
		PRINT_ER("Error while initializing hostinterface\n");

	return s32Error;
}

/**
 *  @brief      WILC_WFI_WiphyFree
 *  @details    Freeing allocation of the wireless device structure
 *  @param[in]   NONE
 *  @return     NONE
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
int wilc_deinit_host_int(struct net_device *net)
{
	int s32Error = 0;

	struct wilc_priv *priv;

	priv = wdev_priv(net->ieee80211_ptr);

	priv->gbAutoRateAdjusted = false;

	priv->bInP2PlistenState = false;

	op_ifcs--;

	s32Error = host_int_deinit(priv->hWILCWFIDrv);

	/* Clear the Shadow scan */
	clear_shadow_scan(priv);
	if (op_ifcs == 0) {
		PRINT_D(CORECONFIG_DBG, "destroy during ip\n");
		del_timer_sync(&hDuringIpTimer);
	}

	if (s32Error)
		PRINT_ER("Error while deintializing host interface\n");

	return s32Error;
}


/**
 *  @brief      WILC_WFI_WiphyFree
 *  @details    Freeing allocation of the wireless device structure
 *  @param[in]   NONE
 *  @return     NONE
 *  @author	mdaftedar
 *  @date	01 MAR 2012
 *  @version	1.0
 */
void wilc_free_wiphy(struct net_device *net)
{
	PRINT_D(CFG80211_DBG, "Unregistering wiphy\n");

	if (!net) {
		PRINT_D(INIT_DBG, "net_device is NULL\n");
		return;
	}

	if (!net->ieee80211_ptr) {
		PRINT_D(INIT_DBG, "ieee80211_ptr is NULL\n");
		return;
	}

	if (!net->ieee80211_ptr->wiphy) {
		PRINT_D(INIT_DBG, "wiphy is NULL\n");
		return;
	}

	wiphy_unregister(net->ieee80211_ptr->wiphy);

	PRINT_D(INIT_DBG, "Freeing wiphy\n");
	wiphy_free(net->ieee80211_ptr->wiphy);
	kfree(net->ieee80211_ptr);
}
