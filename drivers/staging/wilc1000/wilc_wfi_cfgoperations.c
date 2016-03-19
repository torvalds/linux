#include "wilc_wfi_cfgoperations.h"
#include "host_interface.h"
#include <linux/errno.h>

#define NO_ENCRYPT		0
#define ENCRYPT_ENABLED		BIT(0)
#define WEP			BIT(1)
#define WEP_EXTENDED		BIT(2)
#define WPA			BIT(3)
#define WPA2			BIT(4)
#define AES			BIT(5)
#define TKIP			BIT(6)

#define FRAME_TYPE_ID			0
#define ACTION_CAT_ID			24
#define ACTION_SUBTYPE_ID		25
#define P2P_PUB_ACTION_SUBTYPE		30

#define ACTION_FRAME			0xd0
#define GO_INTENT_ATTR_ID		0x04
#define CHANLIST_ATTR_ID		0x0b
#define OPERCHAN_ATTR_ID		0x11
#define PUB_ACTION_ATTR_ID		0x04
#define P2PELEM_ATTR_ID			0xdd

#define GO_NEG_REQ			0x00
#define GO_NEG_RSP			0x01
#define GO_NEG_CONF			0x02
#define P2P_INV_REQ			0x03
#define P2P_INV_RSP			0x04
#define PUBLIC_ACT_VENDORSPEC		0x09
#define GAS_INTIAL_REQ			0x0a
#define GAS_INTIAL_RSP			0x0b

#define INVALID_CHANNEL			0

#define nl80211_SCAN_RESULT_EXPIRE	(3 * HZ)
#define SCAN_RESULT_EXPIRE		(40 * HZ)

static const u32 cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
	WLAN_CIPHER_SUITE_AES_CMAC,
};

static const struct ieee80211_txrx_stypes
	wilc_wfi_cfg80211_mgmt_types[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_STATION] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_AP] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
			BIT(IEEE80211_STYPE_DISASSOC >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4) |
			BIT(IEEE80211_STYPE_DEAUTH >> 4) |
			BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_P2P_CLIENT] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
			BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_DISASSOC >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4) |
			BIT(IEEE80211_STYPE_DEAUTH >> 4)
	}
};

static const struct wiphy_wowlan_support wowlan_support = {
	.flags = WIPHY_WOWLAN_ANY
};

#define WILC_WFI_DWELL_PASSIVE 100
#define WILC_WFI_DWELL_ACTIVE  40

#define TCP_ACK_FILTER_LINK_SPEED_THRESH	54
#define DEFAULT_LINK_SPEED			72


#define IS_MANAGMEMENT				0x100
#define IS_MANAGMEMENT_CALLBACK			0x080
#define IS_MGMT_STATUS_SUCCES			0x040
#define GET_PKT_OFFSET(a) (((a) >> 22) & 0x1ff)

extern int wilc_mac_open(struct net_device *ndev);
extern int wilc_mac_close(struct net_device *ndev);

static struct network_info last_scanned_shadow[MAX_NUM_SCANNED_NETWORKS_SHADOW];
static u32 last_scanned_cnt;
struct timer_list wilc_during_ip_timer;
static struct timer_list hAgingTimer;
static u8 op_ifcs;

u8 wilc_initialized = 1;

#define CHAN2G(_channel, _freq, _flags) {	 \
		.band             = IEEE80211_BAND_2GHZ, \
		.center_freq      = (_freq),		 \
		.hw_value         = (_channel),		 \
		.flags            = (_flags),		 \
		.max_antenna_gain = 0,			 \
		.max_power        = 30,			 \
}

static struct ieee80211_channel ieee80211_2ghz_channels[] = {
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

static struct ieee80211_rate ieee80211_bitrates[] = {
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

static u8 wlan_channel = INVALID_CHANNEL;
static u8 curr_channel;
static u8 p2p_oui[] = {0x50, 0x6f, 0x9A, 0x09};
static u8 p2p_local_random = 0x01;
static u8 p2p_recv_random;
static u8 p2p_vendor_spec[] = {0xdd, 0x05, 0x00, 0x08, 0x40, 0x03};
static bool wilc_ie;

static struct ieee80211_supported_band WILC_WFI_band_2ghz = {
	.channels = ieee80211_2ghz_channels,
	.n_channels = ARRAY_SIZE(ieee80211_2ghz_channels),
	.bitrates = ieee80211_bitrates,
	.n_bitrates = ARRAY_SIZE(ieee80211_bitrates),
};


struct add_key_params {
	u8 key_idx;
	bool pairwise;
	u8 *mac_addr;
};
static struct add_key_params g_add_gtk_key_params;
static struct wilc_wfi_key g_key_gtk_params;
static struct add_key_params g_add_ptk_key_params;
static struct wilc_wfi_key g_key_ptk_params;
static struct wilc_wfi_wep_key g_key_wep_params;
static bool g_ptk_keys_saved;
static bool g_gtk_keys_saved;
static bool g_wep_keys_saved;

#define AGING_TIME	(9 * 1000)
#define during_ip_time	15000

static void clear_shadow_scan(void)
{
	int i;

	if (op_ifcs == 0) {
		del_timer_sync(&hAgingTimer);

		for (i = 0; i < last_scanned_cnt; i++) {
			if (last_scanned_shadow[last_scanned_cnt].ies) {
				kfree(last_scanned_shadow[i].ies);
				last_scanned_shadow[last_scanned_cnt].ies = NULL;
			}

			kfree(last_scanned_shadow[i].join_params);
			last_scanned_shadow[i].join_params = NULL;
		}
		last_scanned_cnt = 0;
	}
}

static u32 get_rssi_avg(struct network_info *network_info)
{
	u8 i;
	int rssi_v = 0;
	u8 num_rssi = (network_info->str_rssi.u8Full) ?
		       NUM_RSSI : (network_info->str_rssi.u8Index);

	for (i = 0; i < num_rssi; i++)
		rssi_v += network_info->str_rssi.as8RSSI[i];

	rssi_v /= num_rssi;
	return rssi_v;
}

static void refresh_scan(void *user_void, u8 all, bool direct_scan)
{
	struct wilc_priv *priv;
	struct wiphy *wiphy;
	struct cfg80211_bss *bss = NULL;
	int i;
	int rssi = 0;

	priv = user_void;
	wiphy = priv->dev->ieee80211_ptr->wiphy;

	for (i = 0; i < last_scanned_cnt; i++) {
		struct network_info *network_info;

		network_info = &last_scanned_shadow[i];

		if (!network_info->found || all) {
			s32 freq;
			struct ieee80211_channel *channel;

			if (network_info) {
				freq = ieee80211_channel_to_frequency((s32)network_info->ch, IEEE80211_BAND_2GHZ);
				channel = ieee80211_get_channel(wiphy, freq);

				rssi = get_rssi_avg(network_info);
				if (memcmp("DIRECT-", network_info->ssid, 7) ||
				    direct_scan) {
					bss = cfg80211_inform_bss(wiphy,
								  channel,
								  CFG80211_BSS_FTYPE_UNKNOWN,
								  network_info->bssid,
								  network_info->tsf_hi,
								  network_info->cap_info,
								  network_info->beacon_period,
								  (const u8 *)network_info->ies,
								  (size_t)network_info->ies_len,
								  (s32)rssi * 100,
								  GFP_KERNEL);
					cfg80211_put_bss(wiphy, bss);
				}
			}
		}
	}
}

static void reset_shadow_found(void)
{
	int i;

	for (i = 0; i < last_scanned_cnt; i++)
		last_scanned_shadow[i].found = 0;
}

static void update_scan_time(void)
{
	int i;

	for (i = 0; i < last_scanned_cnt; i++)
		last_scanned_shadow[i].time_scan = jiffies;
}

static void remove_network_from_shadow(unsigned long arg)
{
	unsigned long now = jiffies;
	int i, j;


	for (i = 0; i < last_scanned_cnt; i++) {
		if (time_after(now, last_scanned_shadow[i].time_scan +
			       (unsigned long)(SCAN_RESULT_EXPIRE))) {
			kfree(last_scanned_shadow[i].ies);
			last_scanned_shadow[i].ies = NULL;

			kfree(last_scanned_shadow[i].join_params);

			for (j = i; (j < last_scanned_cnt - 1); j++)
				last_scanned_shadow[j] = last_scanned_shadow[j + 1];

			last_scanned_cnt--;
		}
	}

	if (last_scanned_cnt != 0) {
		hAgingTimer.data = arg;
		mod_timer(&hAgingTimer, jiffies + msecs_to_jiffies(AGING_TIME));
	}
}

static void clear_duringIP(unsigned long arg)
{
	wilc_optaining_ip = false;
}

static int is_network_in_shadow(struct network_info *pstrNetworkInfo,
				void *user_void)
{
	int state = -1;
	int i;

	if (last_scanned_cnt == 0) {
		hAgingTimer.data = (unsigned long)user_void;
		mod_timer(&hAgingTimer, jiffies + msecs_to_jiffies(AGING_TIME));
		state = -1;
	} else {
		for (i = 0; i < last_scanned_cnt; i++) {
			if (memcmp(last_scanned_shadow[i].bssid,
				   pstrNetworkInfo->bssid, 6) == 0) {
				state = i;
				break;
			}
		}
	}
	return state;
}

static void add_network_to_shadow(struct network_info *pstrNetworkInfo,
				  void *user_void, void *pJoinParams)
{
	int ap_found = is_network_in_shadow(pstrNetworkInfo, user_void);
	u32 ap_index = 0;
	u8 rssi_index = 0;

	if (last_scanned_cnt >= MAX_NUM_SCANNED_NETWORKS_SHADOW)
		return;

	if (ap_found == -1) {
		ap_index = last_scanned_cnt;
		last_scanned_cnt++;
	} else {
		ap_index = ap_found;
	}
	rssi_index = last_scanned_shadow[ap_index].str_rssi.u8Index;
	last_scanned_shadow[ap_index].str_rssi.as8RSSI[rssi_index++] = pstrNetworkInfo->rssi;
	if (rssi_index == NUM_RSSI) {
		rssi_index = 0;
		last_scanned_shadow[ap_index].str_rssi.u8Full = 1;
	}
	last_scanned_shadow[ap_index].str_rssi.u8Index = rssi_index;
	last_scanned_shadow[ap_index].rssi = pstrNetworkInfo->rssi;
	last_scanned_shadow[ap_index].cap_info = pstrNetworkInfo->cap_info;
	last_scanned_shadow[ap_index].ssid_len = pstrNetworkInfo->ssid_len;
	memcpy(last_scanned_shadow[ap_index].ssid,
	       pstrNetworkInfo->ssid, pstrNetworkInfo->ssid_len);
	memcpy(last_scanned_shadow[ap_index].bssid,
	       pstrNetworkInfo->bssid, ETH_ALEN);
	last_scanned_shadow[ap_index].beacon_period = pstrNetworkInfo->beacon_period;
	last_scanned_shadow[ap_index].dtim_period = pstrNetworkInfo->dtim_period;
	last_scanned_shadow[ap_index].ch = pstrNetworkInfo->ch;
	last_scanned_shadow[ap_index].ies_len = pstrNetworkInfo->ies_len;
	last_scanned_shadow[ap_index].tsf_hi = pstrNetworkInfo->tsf_hi;
	if (ap_found != -1)
		kfree(last_scanned_shadow[ap_index].ies);
	last_scanned_shadow[ap_index].ies = kmalloc(pstrNetworkInfo->ies_len,
						    GFP_KERNEL);
	memcpy(last_scanned_shadow[ap_index].ies,
	       pstrNetworkInfo->ies, pstrNetworkInfo->ies_len);
	last_scanned_shadow[ap_index].time_scan = jiffies;
	last_scanned_shadow[ap_index].time_scan_cached = jiffies;
	last_scanned_shadow[ap_index].found = 1;
	if (ap_found != -1)
		kfree(last_scanned_shadow[ap_index].join_params);
	last_scanned_shadow[ap_index].join_params = pJoinParams;
}

static void CfgScanResult(enum scan_event scan_event,
			  struct network_info *network_info,
			  void *user_void,
			  void *join_params)
{
	struct wilc_priv *priv;
	struct wiphy *wiphy;
	s32 s32Freq;
	struct ieee80211_channel *channel;
	struct cfg80211_bss *bss = NULL;

	priv = user_void;
	if (priv->bCfgScanning) {
		if (scan_event == SCAN_EVENT_NETWORK_FOUND) {
			wiphy = priv->dev->ieee80211_ptr->wiphy;

			if (!wiphy)
				return;

			if (wiphy->signal_type == CFG80211_SIGNAL_TYPE_UNSPEC &&
			    (((s32)network_info->rssi * 100) < 0 ||
			    ((s32)network_info->rssi * 100) > 100))
				return;

			if (network_info) {
				s32Freq = ieee80211_channel_to_frequency((s32)network_info->ch, IEEE80211_BAND_2GHZ);
				channel = ieee80211_get_channel(wiphy, s32Freq);

				if (!channel)
					return;

				if (network_info->new_network) {
					if (priv->u32RcvdChCount < MAX_NUM_SCANNED_NETWORKS) {
						priv->u32RcvdChCount++;

						add_network_to_shadow(network_info, priv, join_params);

						if (!(memcmp("DIRECT-", network_info->ssid, 7))) {
							bss = cfg80211_inform_bss(wiphy,
										  channel,
										  CFG80211_BSS_FTYPE_UNKNOWN,
										  network_info->bssid,
										  network_info->tsf_hi,
										  network_info->cap_info,
										  network_info->beacon_period,
										  (const u8 *)network_info->ies,
										  (size_t)network_info->ies_len,
										  (s32)network_info->rssi * 100,
										  GFP_KERNEL);
							cfg80211_put_bss(wiphy, bss);
						}
					}
				} else {
					u32 i;

					for (i = 0; i < priv->u32RcvdChCount; i++) {
						if (memcmp(last_scanned_shadow[i].bssid, network_info->bssid, 6) == 0) {
							last_scanned_shadow[i].rssi = network_info->rssi;
							last_scanned_shadow[i].time_scan = jiffies;
							break;
						}
					}
				}
			}
		} else if (scan_event == SCAN_EVENT_DONE) {
			refresh_scan(priv, 1, false);

			down(&(priv->hSemScanReq));

			if (priv->pstrScanReq) {
				cfg80211_scan_done(priv->pstrScanReq, false);
				priv->u32RcvdChCount = 0;
				priv->bCfgScanning = false;
				priv->pstrScanReq = NULL;
			}
			up(&(priv->hSemScanReq));
		} else if (scan_event == SCAN_EVENT_ABORTED) {
			down(&(priv->hSemScanReq));

			if (priv->pstrScanReq) {
				update_scan_time();
				refresh_scan(priv, 1, false);

				cfg80211_scan_done(priv->pstrScanReq, false);
				priv->bCfgScanning = false;
				priv->pstrScanReq = NULL;
			}
			up(&(priv->hSemScanReq));
		}
	}
}

int wilc_connecting;

static void CfgConnectResult(enum conn_event enuConnDisconnEvent,
			     struct connect_info *pstrConnectInfo,
			     u8 u8MacStatus,
			     struct disconnect_info *pstrDisconnectNotifInfo,
			     void *pUserVoid)
{
	struct wilc_priv *priv;
	struct net_device *dev;
	struct host_if_drv *pstrWFIDrv;
	u8 NullBssid[ETH_ALEN] = {0};
	struct wilc *wl;
	struct wilc_vif *vif;

	wilc_connecting = 0;

	priv = pUserVoid;
	dev = priv->dev;
	vif = netdev_priv(dev);
	wl = vif->wilc;
	pstrWFIDrv = (struct host_if_drv *)priv->hif_drv;

	if (enuConnDisconnEvent == CONN_DISCONN_EVENT_CONN_RESP) {
		u16 u16ConnectStatus;

		u16ConnectStatus = pstrConnectInfo->status;

		if ((u8MacStatus == MAC_DISCONNECTED) &&
		    (pstrConnectInfo->status == SUCCESSFUL_STATUSCODE)) {
			u16ConnectStatus = WLAN_STATUS_UNSPECIFIED_FAILURE;
			wilc_wlan_set_bssid(priv->dev, NullBssid,
					    STATION_MODE);
			eth_zero_addr(wilc_connected_ssid);

			if (!pstrWFIDrv->p2p_connect)
				wlan_channel = INVALID_CHANNEL;

			netdev_err(dev, "Unspecified failure\n");
		}

		if (u16ConnectStatus == WLAN_STATUS_SUCCESS) {
			bool bNeedScanRefresh = false;
			u32 i;

			memcpy(priv->au8AssociatedBss, pstrConnectInfo->bssid, ETH_ALEN);


			for (i = 0; i < last_scanned_cnt; i++) {
				if (memcmp(last_scanned_shadow[i].bssid,
					   pstrConnectInfo->bssid,
					   ETH_ALEN) == 0) {
					unsigned long now = jiffies;

					if (time_after(now,
						       last_scanned_shadow[i].time_scan_cached +
						       (unsigned long)(nl80211_SCAN_RESULT_EXPIRE - (1 * HZ))))
						bNeedScanRefresh = true;

					break;
				}
			}

			if (bNeedScanRefresh)
				refresh_scan(priv, 1, true);
		}

		cfg80211_connect_result(dev, pstrConnectInfo->bssid,
					pstrConnectInfo->req_ies, pstrConnectInfo->req_ies_len,
					pstrConnectInfo->resp_ies, pstrConnectInfo->resp_ies_len,
					u16ConnectStatus, GFP_KERNEL);
	} else if (enuConnDisconnEvent == CONN_DISCONN_EVENT_DISCONN_NOTIF)    {
		wilc_optaining_ip = false;
		p2p_local_random = 0x01;
		p2p_recv_random = 0x00;
		wilc_ie = false;
		eth_zero_addr(priv->au8AssociatedBss);
		wilc_wlan_set_bssid(priv->dev, NullBssid, STATION_MODE);
		eth_zero_addr(wilc_connected_ssid);

		if (!pstrWFIDrv->p2p_connect)
			wlan_channel = INVALID_CHANNEL;
		if ((pstrWFIDrv->IFC_UP) && (dev == wl->vif[1]->ndev))
			pstrDisconnectNotifInfo->reason = 3;
		else if ((!pstrWFIDrv->IFC_UP) && (dev == wl->vif[1]->ndev))
			pstrDisconnectNotifInfo->reason = 1;

		cfg80211_disconnected(dev, pstrDisconnectNotifInfo->reason, pstrDisconnectNotifInfo->ie,
				      pstrDisconnectNotifInfo->ie_len, false,
				      GFP_KERNEL);
	}
}

static int set_channel(struct wiphy *wiphy,
		       struct cfg80211_chan_def *chandef)
{
	u32 channelnum = 0;
	struct wilc_priv *priv;
	int result = 0;
	struct wilc_vif *vif;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(priv->dev);

	channelnum = ieee80211_frequency_to_channel(chandef->chan->center_freq);

	curr_channel = channelnum;
	result = wilc_set_mac_chnl_num(vif, channelnum);

	if (result != 0)
		netdev_err(priv->dev, "Error in setting channel\n");

	return result;
}

static int scan(struct wiphy *wiphy, struct cfg80211_scan_request *request)
{
	struct wilc_priv *priv;
	u32 i;
	s32 s32Error = 0;
	u8 au8ScanChanList[MAX_NUM_SCANNED_NETWORKS];
	struct hidden_network strHiddenNetwork;
	struct wilc_vif *vif;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(priv->dev);

	priv->pstrScanReq = request;

	priv->u32RcvdChCount = 0;

	reset_shadow_found();

	priv->bCfgScanning = true;
	if (request->n_channels <= MAX_NUM_SCANNED_NETWORKS) {
		for (i = 0; i < request->n_channels; i++)
			au8ScanChanList[i] = (u8)ieee80211_frequency_to_channel(request->channels[i]->center_freq);

		if (request->n_ssids >= 1) {
			strHiddenNetwork.net_info =
				kmalloc_array(request->n_ssids,
					      sizeof(struct hidden_network),
					      GFP_KERNEL);
			if (!strHiddenNetwork.net_info)
				return -ENOMEM;
			strHiddenNetwork.n_ssids = request->n_ssids;


			for (i = 0; i < request->n_ssids; i++) {
				if (request->ssids[i].ssid &&
				    request->ssids[i].ssid_len != 0) {
					strHiddenNetwork.net_info[i].ssid = kmalloc(request->ssids[i].ssid_len, GFP_KERNEL);
					memcpy(strHiddenNetwork.net_info[i].ssid, request->ssids[i].ssid, request->ssids[i].ssid_len);
					strHiddenNetwork.net_info[i].ssid_len = request->ssids[i].ssid_len;
				} else {
					strHiddenNetwork.n_ssids -= 1;
				}
			}
			s32Error = wilc_scan(vif, USER_SCAN, ACTIVE_SCAN,
					     au8ScanChanList,
					     request->n_channels,
					     (const u8 *)request->ie,
					     request->ie_len, CfgScanResult,
					     (void *)priv, &strHiddenNetwork);
		} else {
			s32Error = wilc_scan(vif, USER_SCAN, ACTIVE_SCAN,
					     au8ScanChanList,
					     request->n_channels,
					     (const u8 *)request->ie,
					     request->ie_len, CfgScanResult,
					     (void *)priv, NULL);
		}
	} else {
		netdev_err(priv->dev, "Requested scanned channels over\n");
	}

	if (s32Error != 0)
		s32Error = -EBUSY;

	return s32Error;
}

static int connect(struct wiphy *wiphy, struct net_device *dev,
		   struct cfg80211_connect_params *sme)
{
	s32 s32Error = 0;
	u32 i;
	u8 u8security = NO_ENCRYPT;
	enum AUTHTYPE tenuAuth_type = ANY;

	struct wilc_priv *priv;
	struct host_if_drv *pstrWFIDrv;
	struct network_info *pstrNetworkInfo = NULL;
	struct wilc_vif *vif;

	wilc_connecting = 1;
	priv = wiphy_priv(wiphy);
	vif = netdev_priv(priv->dev);
	pstrWFIDrv = (struct host_if_drv *)priv->hif_drv;

	if (!(strncmp(sme->ssid, "DIRECT-", 7)))
		pstrWFIDrv->p2p_connect = 1;
	else
		pstrWFIDrv->p2p_connect = 0;

	for (i = 0; i < last_scanned_cnt; i++) {
		if ((sme->ssid_len == last_scanned_shadow[i].ssid_len) &&
		    memcmp(last_scanned_shadow[i].ssid,
			   sme->ssid,
			   sme->ssid_len) == 0) {
			if (!sme->bssid)
				break;
			else
				if (memcmp(last_scanned_shadow[i].bssid,
					   sme->bssid,
					   ETH_ALEN) == 0)
					break;
		}
	}

	if (i < last_scanned_cnt) {
		pstrNetworkInfo = &last_scanned_shadow[i];
	} else {
		s32Error = -ENOENT;
		wilc_connecting = 0;
		return s32Error;
	}

	memset(priv->WILC_WFI_wep_key, 0, sizeof(priv->WILC_WFI_wep_key));
	memset(priv->WILC_WFI_wep_key_len, 0, sizeof(priv->WILC_WFI_wep_key_len));

	if (sme->crypto.cipher_group != NO_ENCRYPT) {
		if (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP40) {
			u8security = ENCRYPT_ENABLED | WEP;

			priv->WILC_WFI_wep_key_len[sme->key_idx] = sme->key_len;
			memcpy(priv->WILC_WFI_wep_key[sme->key_idx], sme->key, sme->key_len);

			g_key_wep_params.key_len = sme->key_len;
			g_key_wep_params.key = kmalloc(sme->key_len, GFP_KERNEL);
			memcpy(g_key_wep_params.key, sme->key, sme->key_len);
			g_key_wep_params.key_idx = sme->key_idx;
			g_wep_keys_saved = true;

			wilc_set_wep_default_keyid(vif, sme->key_idx);
			wilc_add_wep_key_bss_sta(vif, sme->key, sme->key_len,
						 sme->key_idx);
		} else if (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP104)   {
			u8security = ENCRYPT_ENABLED | WEP | WEP_EXTENDED;

			priv->WILC_WFI_wep_key_len[sme->key_idx] = sme->key_len;
			memcpy(priv->WILC_WFI_wep_key[sme->key_idx], sme->key, sme->key_len);

			g_key_wep_params.key_len = sme->key_len;
			g_key_wep_params.key = kmalloc(sme->key_len, GFP_KERNEL);
			memcpy(g_key_wep_params.key, sme->key, sme->key_len);
			g_key_wep_params.key_idx = sme->key_idx;
			g_wep_keys_saved = true;

			wilc_set_wep_default_keyid(vif, sme->key_idx);
			wilc_add_wep_key_bss_sta(vif, sme->key, sme->key_len,
						 sme->key_idx);
		} else if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_2)   {
			if (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_TKIP)
				u8security = ENCRYPT_ENABLED | WPA2 | TKIP;
			else
				u8security = ENCRYPT_ENABLED | WPA2 | AES;
		} else if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_1)   {
			if (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_TKIP)
				u8security = ENCRYPT_ENABLED | WPA | TKIP;
			else
				u8security = ENCRYPT_ENABLED | WPA | AES;
		} else {
			s32Error = -ENOTSUPP;
			netdev_err(dev, "Not supported cipher\n");
			wilc_connecting = 0;
			return s32Error;
		}
	}

	if ((sme->crypto.wpa_versions & NL80211_WPA_VERSION_1)
	    || (sme->crypto.wpa_versions & NL80211_WPA_VERSION_2)) {
		for (i = 0; i < sme->crypto.n_ciphers_pairwise; i++) {
			if (sme->crypto.ciphers_pairwise[i] == WLAN_CIPHER_SUITE_TKIP)
				u8security = u8security | TKIP;
			else
				u8security = u8security | AES;
		}
	}

	switch (sme->auth_type)	{
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		tenuAuth_type = OPEN_SYSTEM;
		break;

	case NL80211_AUTHTYPE_SHARED_KEY:
		tenuAuth_type = SHARED_KEY;
		break;

	default:
		break;
	}

	if (sme->crypto.n_akm_suites) {
		switch (sme->crypto.akm_suites[0]) {
		case WLAN_AKM_SUITE_8021X:
			tenuAuth_type = IEEE8021;
			break;

		default:
			break;
		}
	}

	curr_channel = pstrNetworkInfo->ch;

	if (!pstrWFIDrv->p2p_connect)
		wlan_channel = pstrNetworkInfo->ch;

	wilc_wlan_set_bssid(dev, pstrNetworkInfo->bssid, STATION_MODE);

	s32Error = wilc_set_join_req(vif, pstrNetworkInfo->bssid, sme->ssid,
				     sme->ssid_len, sme->ie, sme->ie_len,
				     CfgConnectResult, (void *)priv,
				     u8security, tenuAuth_type,
				     pstrNetworkInfo->ch,
				     pstrNetworkInfo->join_params);
	if (s32Error != 0) {
		netdev_err(dev, "wilc_set_join_req(): Error\n");
		s32Error = -ENOENT;
		wilc_connecting = 0;
		return s32Error;
	}

	return s32Error;
}

static int disconnect(struct wiphy *wiphy, struct net_device *dev, u16 reason_code)
{
	s32 s32Error = 0;
	struct wilc_priv *priv;
	struct host_if_drv *pstrWFIDrv;
	struct wilc_vif *vif;
	u8 NullBssid[ETH_ALEN] = {0};

	wilc_connecting = 0;
	priv = wiphy_priv(wiphy);
	vif = netdev_priv(priv->dev);

	pstrWFIDrv = (struct host_if_drv *)priv->hif_drv;
	if (!pstrWFIDrv->p2p_connect)
		wlan_channel = INVALID_CHANNEL;
	wilc_wlan_set_bssid(priv->dev, NullBssid, STATION_MODE);

	p2p_local_random = 0x01;
	p2p_recv_random = 0x00;
	wilc_ie = false;
	pstrWFIDrv->p2p_timeout = 0;

	s32Error = wilc_disconnect(vif, reason_code);
	if (s32Error != 0) {
		netdev_err(priv->dev, "Error in disconnecting\n");
		s32Error = -EINVAL;
	}

	return s32Error;
}

static int add_key(struct wiphy *wiphy, struct net_device *netdev, u8 key_index,
		   bool pairwise,
		   const u8 *mac_addr, struct key_params *params)

{
	s32 s32Error = 0, KeyLen = params->key_len;
	struct wilc_priv *priv;
	const u8 *pu8RxMic = NULL;
	const u8 *pu8TxMic = NULL;
	u8 u8mode = NO_ENCRYPT;
	u8 u8gmode = NO_ENCRYPT;
	u8 u8pmode = NO_ENCRYPT;
	enum AUTHTYPE tenuAuth_type = ANY;
	struct wilc *wl;
	struct wilc_vif *vif;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(netdev);
	wl = vif->wilc;

	switch (params->cipher)	{
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		if (priv->wdev->iftype == NL80211_IFTYPE_AP) {
			priv->WILC_WFI_wep_key_len[key_index] = params->key_len;
			memcpy(priv->WILC_WFI_wep_key[key_index], params->key, params->key_len);

			tenuAuth_type = OPEN_SYSTEM;

			if (params->cipher == WLAN_CIPHER_SUITE_WEP40)
				u8mode = ENCRYPT_ENABLED | WEP;
			else
				u8mode = ENCRYPT_ENABLED | WEP | WEP_EXTENDED;

			wilc_add_wep_key_bss_ap(vif, params->key,
						params->key_len, key_index,
						u8mode, tenuAuth_type);
			break;
		}
		if (memcmp(params->key, priv->WILC_WFI_wep_key[key_index], params->key_len)) {
			priv->WILC_WFI_wep_key_len[key_index] = params->key_len;
			memcpy(priv->WILC_WFI_wep_key[key_index], params->key, params->key_len);

			wilc_add_wep_key_bss_sta(vif, params->key,
						 params->key_len, key_index);
		}

		break;

	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
		if (priv->wdev->iftype == NL80211_IFTYPE_AP || priv->wdev->iftype == NL80211_IFTYPE_P2P_GO) {
			if (!priv->wilc_gtk[key_index]) {
				priv->wilc_gtk[key_index] = kmalloc(sizeof(struct wilc_wfi_key), GFP_KERNEL);
				priv->wilc_gtk[key_index]->key = NULL;
				priv->wilc_gtk[key_index]->seq = NULL;
			}
			if (!priv->wilc_ptk[key_index]) {
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
				kfree(priv->wilc_gtk[key_index]->key);

				priv->wilc_gtk[key_index]->key = kmalloc(params->key_len, GFP_KERNEL);
				memcpy(priv->wilc_gtk[key_index]->key, params->key, params->key_len);
				kfree(priv->wilc_gtk[key_index]->seq);

				if ((params->seq_len) > 0) {
					priv->wilc_gtk[key_index]->seq = kmalloc(params->seq_len, GFP_KERNEL);
					memcpy(priv->wilc_gtk[key_index]->seq, params->seq, params->seq_len);
				}

				priv->wilc_gtk[key_index]->cipher = params->cipher;
				priv->wilc_gtk[key_index]->key_len = params->key_len;
				priv->wilc_gtk[key_index]->seq_len = params->seq_len;

				wilc_add_rx_gtk(vif, params->key, KeyLen,
						key_index, params->seq_len,
						params->seq, pu8RxMic,
						pu8TxMic, AP_MODE, u8gmode);

			} else {
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

				memcpy(priv->wilc_ptk[key_index]->key, params->key, params->key_len);

				if ((params->seq_len) > 0)
					memcpy(priv->wilc_ptk[key_index]->seq, params->seq, params->seq_len);

				priv->wilc_ptk[key_index]->cipher = params->cipher;
				priv->wilc_ptk[key_index]->key_len = params->key_len;
				priv->wilc_ptk[key_index]->seq_len = params->seq_len;

				wilc_add_ptk(vif, params->key, KeyLen,
					     mac_addr, pu8RxMic, pu8TxMic,
					     AP_MODE, u8pmode, key_index);
			}
			break;
		}

		{
			u8mode = 0;
			if (!pairwise) {
				if (params->key_len > 16 && params->cipher == WLAN_CIPHER_SUITE_TKIP) {
					pu8RxMic = params->key + 24;
					pu8TxMic = params->key + 16;
					KeyLen = params->key_len - 16;
				}

				if (!g_gtk_keys_saved && netdev == wl->vif[0]->ndev) {
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
					g_gtk_keys_saved = true;
				}

				wilc_add_rx_gtk(vif, params->key, KeyLen,
						key_index, params->seq_len,
						params->seq, pu8RxMic,
						pu8TxMic, STATION_MODE,
						u8mode);
			} else {
				if (params->key_len > 16 && params->cipher == WLAN_CIPHER_SUITE_TKIP) {
					pu8RxMic = params->key + 24;
					pu8TxMic = params->key + 16;
					KeyLen = params->key_len - 16;
				}

				if (!g_ptk_keys_saved && netdev == wl->vif[0]->ndev) {
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
					g_ptk_keys_saved = true;
				}

				wilc_add_ptk(vif, params->key, KeyLen,
					     mac_addr, pu8RxMic, pu8TxMic,
					     STATION_MODE, u8mode, key_index);
			}
		}
		break;

	default:
		netdev_err(netdev, "Not supported cipher\n");
		s32Error = -ENOTSUPP;
	}

	return s32Error;
}

static int del_key(struct wiphy *wiphy, struct net_device *netdev,
		   u8 key_index,
		   bool pairwise,
		   const u8 *mac_addr)
{
	struct wilc_priv *priv;
	struct wilc *wl;
	struct wilc_vif *vif;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(netdev);
	wl = vif->wilc;

	if (netdev == wl->vif[0]->ndev) {
		g_ptk_keys_saved = false;
		g_gtk_keys_saved = false;
		g_wep_keys_saved = false;

		kfree(g_key_wep_params.key);
		g_key_wep_params.key = NULL;

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

		kfree(g_key_ptk_params.key);
		g_key_ptk_params.key = NULL;
		kfree(g_key_ptk_params.seq);
		g_key_ptk_params.seq = NULL;

		kfree(g_key_gtk_params.key);
		g_key_gtk_params.key = NULL;
		kfree(g_key_gtk_params.seq);
		g_key_gtk_params.seq = NULL;

	}

	if (key_index >= 0 && key_index <= 3) {
		memset(priv->WILC_WFI_wep_key[key_index], 0, priv->WILC_WFI_wep_key_len[key_index]);
		priv->WILC_WFI_wep_key_len[key_index] = 0;
		wilc_remove_wep_key(vif, key_index);
	} else {
		wilc_remove_key(priv->hif_drv, mac_addr);
	}

	return 0;
}

static int get_key(struct wiphy *wiphy, struct net_device *netdev, u8 key_index,
		   bool pairwise,
		   const u8 *mac_addr, void *cookie, void (*callback)(void *cookie, struct key_params *))
{
	struct wilc_priv *priv;
	struct  key_params key_params;

	priv = wiphy_priv(wiphy);


	if (!pairwise) {
		key_params.key = priv->wilc_gtk[key_index]->key;
		key_params.cipher = priv->wilc_gtk[key_index]->cipher;
		key_params.key_len = priv->wilc_gtk[key_index]->key_len;
		key_params.seq = priv->wilc_gtk[key_index]->seq;
		key_params.seq_len = priv->wilc_gtk[key_index]->seq_len;
	} else {
		key_params.key = priv->wilc_ptk[key_index]->key;
		key_params.cipher = priv->wilc_ptk[key_index]->cipher;
		key_params.key_len = priv->wilc_ptk[key_index]->key_len;
		key_params.seq = priv->wilc_ptk[key_index]->seq;
		key_params.seq_len = priv->wilc_ptk[key_index]->seq_len;
	}

	callback(cookie, &key_params);

	return 0;
}

static int set_default_key(struct wiphy *wiphy, struct net_device *netdev, u8 key_index,
			   bool unicast, bool multicast)
{
	struct wilc_priv *priv;
	struct wilc_vif *vif;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(priv->dev);

	wilc_set_wep_default_keyid(vif, key_index);

	return 0;
}

static int get_station(struct wiphy *wiphy, struct net_device *dev,
		       const u8 *mac, struct station_info *sinfo)
{
	struct wilc_priv *priv;
	struct wilc_vif *vif;
	u32 i = 0;
	u32 associatedsta = 0;
	u32 inactive_time = 0;
	priv = wiphy_priv(wiphy);
	vif = netdev_priv(dev);

	if (vif->iftype == AP_MODE || vif->iftype == GO_MODE) {
		for (i = 0; i < NUM_STA_ASSOCIATED; i++) {
			if (!(memcmp(mac, priv->assoc_stainfo.au8Sta_AssociatedBss[i], ETH_ALEN))) {
				associatedsta = i;
				break;
			}
		}

		if (associatedsta == -1) {
			netdev_err(dev, "sta required is not associated\n");
			return -ENOENT;
		}

		sinfo->filled |= BIT(NL80211_STA_INFO_INACTIVE_TIME);

		wilc_get_inactive_time(vif, mac, &inactive_time);
		sinfo->inactive_time = 1000 * inactive_time;
	}

	if (vif->iftype == STATION_MODE) {
		struct rf_info strStatistics;

		wilc_get_statistics(vif, &strStatistics);

		sinfo->filled |= BIT(NL80211_STA_INFO_SIGNAL) |
						BIT(NL80211_STA_INFO_RX_PACKETS) |
						BIT(NL80211_STA_INFO_TX_PACKETS) |
						BIT(NL80211_STA_INFO_TX_FAILED) |
						BIT(NL80211_STA_INFO_TX_BITRATE);

		sinfo->signal = strStatistics.rssi;
		sinfo->rx_packets = strStatistics.rx_cnt;
		sinfo->tx_packets = strStatistics.tx_cnt + strStatistics.tx_fail_cnt;
		sinfo->tx_failed = strStatistics.tx_fail_cnt;
		sinfo->txrate.legacy = strStatistics.link_speed * 10;

		if ((strStatistics.link_speed > TCP_ACK_FILTER_LINK_SPEED_THRESH) &&
		    (strStatistics.link_speed != DEFAULT_LINK_SPEED))
			wilc_enable_tcp_ack_filter(true);
		else if (strStatistics.link_speed != DEFAULT_LINK_SPEED)
			wilc_enable_tcp_ack_filter(false);
	}
	return 0;
}

static int change_bss(struct wiphy *wiphy, struct net_device *dev,
		      struct bss_parameters *params)
{
	return 0;
}

static int set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	s32 s32Error = 0;
	struct cfg_param_attr pstrCfgParamVal;
	struct wilc_priv *priv;
	struct wilc_vif *vif;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(priv->dev);

	pstrCfgParamVal.flag = 0;

	if (changed & WIPHY_PARAM_RETRY_SHORT) {
		pstrCfgParamVal.flag  |= RETRY_SHORT;
		pstrCfgParamVal.short_retry_limit = priv->dev->ieee80211_ptr->wiphy->retry_short;
	}
	if (changed & WIPHY_PARAM_RETRY_LONG) {
		pstrCfgParamVal.flag |= RETRY_LONG;
		pstrCfgParamVal.long_retry_limit = priv->dev->ieee80211_ptr->wiphy->retry_long;
	}
	if (changed & WIPHY_PARAM_FRAG_THRESHOLD) {
		pstrCfgParamVal.flag |= FRAG_THRESHOLD;
		pstrCfgParamVal.frag_threshold = priv->dev->ieee80211_ptr->wiphy->frag_threshold;
	}

	if (changed & WIPHY_PARAM_RTS_THRESHOLD) {
		pstrCfgParamVal.flag |= RTS_THRESHOLD;
		pstrCfgParamVal.rts_threshold = priv->dev->ieee80211_ptr->wiphy->rts_threshold;
	}

	s32Error = wilc_hif_set_cfg(vif, &pstrCfgParamVal);
	if (s32Error)
		netdev_err(priv->dev, "Error in setting WIPHY PARAMS\n");

	return s32Error;
}

static int set_pmksa(struct wiphy *wiphy, struct net_device *netdev,
		     struct cfg80211_pmksa *pmksa)
{
	u32 i;
	s32 s32Error = 0;
	u8 flag = 0;
	struct wilc_vif *vif;
	struct wilc_priv *priv = wiphy_priv(wiphy);

	vif = netdev_priv(priv->dev);


	for (i = 0; i < priv->pmkid_list.numpmkid; i++)	{
		if (!memcmp(pmksa->bssid, priv->pmkid_list.pmkidlist[i].bssid,
				 ETH_ALEN)) {
			flag = PMKID_FOUND;
			break;
		}
	}
	if (i < WILC_MAX_NUM_PMKIDS) {
		memcpy(priv->pmkid_list.pmkidlist[i].bssid, pmksa->bssid,
			    ETH_ALEN);
		memcpy(priv->pmkid_list.pmkidlist[i].pmkid, pmksa->pmkid,
			    PMKID_LEN);
		if (!(flag == PMKID_FOUND))
			priv->pmkid_list.numpmkid++;
	} else {
		netdev_err(netdev, "Invalid PMKID index\n");
		s32Error = -EINVAL;
	}

	if (!s32Error)
		s32Error = wilc_set_pmkid_info(vif, &priv->pmkid_list);

	return s32Error;
}

static int del_pmksa(struct wiphy *wiphy, struct net_device *netdev,
		     struct cfg80211_pmksa *pmksa)
{
	u32 i;
	s32 s32Error = 0;

	struct wilc_priv *priv = wiphy_priv(wiphy);

	for (i = 0; i < priv->pmkid_list.numpmkid; i++)	{
		if (!memcmp(pmksa->bssid, priv->pmkid_list.pmkidlist[i].bssid,
				 ETH_ALEN)) {
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

static int flush_pmksa(struct wiphy *wiphy, struct net_device *netdev)
{
	struct wilc_priv *priv = wiphy_priv(wiphy);

	memset(&priv->pmkid_list, 0, sizeof(struct host_if_pmkid_attr));

	return 0;
}

static void WILC_WFI_CfgParseRxAction(u8 *buf, u32 len)
{
	u32 index = 0;
	u32 i = 0, j = 0;

	u8 op_channel_attr_index = 0;
	u8 channel_list_attr_index = 0;

	while (index < len) {
		if (buf[index] == GO_INTENT_ATTR_ID)
			buf[index + 3] = (buf[index + 3]  & 0x01) | (0x00 << 1);

		if (buf[index] ==  CHANLIST_ATTR_ID)
			channel_list_attr_index = index;
		else if (buf[index] ==  OPERCHAN_ATTR_ID)
			op_channel_attr_index = index;
		index += buf[index + 1] + 3;
	}
	if (wlan_channel != INVALID_CHANNEL) {
		if (channel_list_attr_index) {
			for (i = channel_list_attr_index + 3; i < ((channel_list_attr_index + 3) + buf[channel_list_attr_index + 1]); i++) {
				if (buf[i] == 0x51) {
					for (j = i + 2; j < ((i + 2) + buf[i + 1]); j++)
						buf[j] = wlan_channel;
					break;
				}
			}
		}

		if (op_channel_attr_index) {
			buf[op_channel_attr_index + 6] = 0x51;
			buf[op_channel_attr_index + 7] = wlan_channel;
		}
	}
}

static void WILC_WFI_CfgParseTxAction(u8 *buf, u32 len, bool bOperChan, u8 iftype)
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
		index += buf[index + 1] + 3;
	}
	if (wlan_channel != INVALID_CHANNEL && bOperChan) {
		if (channel_list_attr_index) {
			for (i = channel_list_attr_index + 3; i < ((channel_list_attr_index + 3) + buf[channel_list_attr_index + 1]); i++) {
				if (buf[i] == 0x51) {
					for (j = i + 2; j < ((i + 2) + buf[i + 1]); j++)
						buf[j] = wlan_channel;
					break;
				}
			}
		}

		if (op_channel_attr_index) {
			buf[op_channel_attr_index + 6] = 0x51;
			buf[op_channel_attr_index + 7] = wlan_channel;
		}
	}
}

void WILC_WFI_p2p_rx(struct net_device *dev, u8 *buff, u32 size)
{
	struct wilc_priv *priv;
	u32 header, pkt_offset;
	struct host_if_drv *pstrWFIDrv;
	u32 i = 0;
	s32 s32Freq;

	priv = wiphy_priv(dev->ieee80211_ptr->wiphy);
	pstrWFIDrv = (struct host_if_drv *)priv->hif_drv;

	memcpy(&header, (buff - HOST_HDR_OFFSET), HOST_HDR_OFFSET);

	pkt_offset = GET_PKT_OFFSET(header);

	if (pkt_offset & IS_MANAGMEMENT_CALLBACK) {
		if (buff[FRAME_TYPE_ID] == IEEE80211_STYPE_PROBE_RESP) {
			cfg80211_mgmt_tx_status(priv->wdev, priv->u64tx_cookie, buff, size, true, GFP_KERNEL);
			return;
		} else {
			if (pkt_offset & IS_MGMT_STATUS_SUCCES)
				cfg80211_mgmt_tx_status(priv->wdev, priv->u64tx_cookie, buff, size, true, GFP_KERNEL);
			else
				cfg80211_mgmt_tx_status(priv->wdev, priv->u64tx_cookie, buff, size, false, GFP_KERNEL);
			return;
		}
	} else {
		s32Freq = ieee80211_channel_to_frequency(curr_channel, IEEE80211_BAND_2GHZ);

		if (ieee80211_is_action(buff[FRAME_TYPE_ID])) {
			if (priv->bCfgScanning && time_after_eq(jiffies, (unsigned long)pstrWFIDrv->p2p_timeout)) {
				netdev_dbg(dev, "Receiving action wrong ch\n");
				return;
			}
			if (buff[ACTION_CAT_ID] == PUB_ACTION_ATTR_ID) {
				switch (buff[ACTION_SUBTYPE_ID]) {
				case GAS_INTIAL_REQ:
					break;

				case GAS_INTIAL_RSP:
					break;

				case PUBLIC_ACT_VENDORSPEC:
					if (!memcmp(p2p_oui, &buff[ACTION_SUBTYPE_ID + 1], 4)) {
						if ((buff[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_REQ || buff[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_RSP))	{
							if (!wilc_ie) {
								for (i = P2P_PUB_ACTION_SUBTYPE; i < size; i++)	{
									if (!memcmp(p2p_vendor_spec, &buff[i], 6)) {
										p2p_recv_random = buff[i + 6];
										wilc_ie = true;
										break;
									}
								}
							}
						}
						if (p2p_local_random > p2p_recv_random)	{
							if ((buff[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_REQ || buff[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_RSP
							      || buff[P2P_PUB_ACTION_SUBTYPE] == P2P_INV_REQ || buff[P2P_PUB_ACTION_SUBTYPE] == P2P_INV_RSP)) {
								for (i = P2P_PUB_ACTION_SUBTYPE + 2; i < size; i++) {
									if (buff[i] == P2PELEM_ATTR_ID && !(memcmp(p2p_oui, &buff[i + 2], 4))) {
										WILC_WFI_CfgParseRxAction(&buff[i + 6], size - (i + 6));
										break;
									}
								}
							}
						} else {
							netdev_dbg(dev, "PEER WILL BE GO LocaRand=%02x RecvRand %02x\n", p2p_local_random, p2p_recv_random);
						}
					}


					if ((buff[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_REQ || buff[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_RSP) && (wilc_ie))	{
						cfg80211_rx_mgmt(priv->wdev, s32Freq, 0, buff, size - 7, 0);
						return;
					}
					break;

				default:
					netdev_dbg(dev, "NOT HANDLED PUBLIC ACTION FRAME TYPE:%x\n", buff[ACTION_SUBTYPE_ID]);
					break;
				}
			}
		}

		cfg80211_rx_mgmt(priv->wdev, s32Freq, 0, buff, size, 0);
	}
}

static void WILC_WFI_mgmt_tx_complete(void *priv, int status)
{
	struct p2p_mgmt_data *pv_data = priv;


	kfree(pv_data->buff);
	kfree(pv_data);
}

static void WILC_WFI_RemainOnChannelReady(void *pUserVoid)
{
	struct wilc_priv *priv;

	priv = pUserVoid;

	priv->bInP2PlistenState = true;

	cfg80211_ready_on_channel(priv->wdev,
				  priv->strRemainOnChanParams.u64ListenCookie,
				  priv->strRemainOnChanParams.pstrListenChan,
				  priv->strRemainOnChanParams.u32ListenDuration,
				  GFP_KERNEL);
}

static void WILC_WFI_RemainOnChannelExpired(void *pUserVoid, u32 u32SessionID)
{
	struct wilc_priv *priv;

	priv = pUserVoid;

	if (u32SessionID == priv->strRemainOnChanParams.u32ListenSessionID) {
		priv->bInP2PlistenState = false;

		cfg80211_remain_on_channel_expired(priv->wdev,
						   priv->strRemainOnChanParams.u64ListenCookie,
						   priv->strRemainOnChanParams.pstrListenChan,
						   GFP_KERNEL);
	}
}

static int remain_on_channel(struct wiphy *wiphy,
			     struct wireless_dev *wdev,
			     struct ieee80211_channel *chan,
			     unsigned int duration, u64 *cookie)
{
	s32 s32Error = 0;
	struct wilc_priv *priv;
	struct wilc_vif *vif;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(priv->dev);

	if (wdev->iftype == NL80211_IFTYPE_AP) {
		netdev_dbg(vif->ndev, "Required while in AP mode\n");
		return s32Error;
	}

	curr_channel = chan->hw_value;

	priv->strRemainOnChanParams.pstrListenChan = chan;
	priv->strRemainOnChanParams.u64ListenCookie = *cookie;
	priv->strRemainOnChanParams.u32ListenDuration = duration;
	priv->strRemainOnChanParams.u32ListenSessionID++;

	s32Error = wilc_remain_on_channel(vif,
				priv->strRemainOnChanParams.u32ListenSessionID,
				duration, chan->hw_value,
				WILC_WFI_RemainOnChannelExpired,
				WILC_WFI_RemainOnChannelReady, (void *)priv);

	return s32Error;
}

static int cancel_remain_on_channel(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    u64 cookie)
{
	s32 s32Error = 0;
	struct wilc_priv *priv;
	struct wilc_vif *vif;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(priv->dev);

	s32Error = wilc_listen_state_expired(vif, priv->strRemainOnChanParams.u32ListenSessionID);
	return s32Error;
}

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
	struct wilc_vif *vif;
	u32 buf_len = len + sizeof(p2p_vendor_spec) + sizeof(p2p_local_random);

	vif = netdev_priv(wdev->netdev);
	priv = wiphy_priv(wiphy);
	pstrWFIDrv = (struct host_if_drv *)priv->hif_drv;

	*cookie = (unsigned long)buf;
	priv->u64tx_cookie = *cookie;
	mgmt = (const struct ieee80211_mgmt *) buf;

	if (ieee80211_is_mgmt(mgmt->frame_control)) {
		mgmt_tx = kmalloc(sizeof(struct p2p_mgmt_data), GFP_KERNEL);
		if (!mgmt_tx)
			return -EFAULT;

		mgmt_tx->buff = kmalloc(buf_len, GFP_KERNEL);
		if (!mgmt_tx->buff) {
			kfree(mgmt_tx);
			return -ENOMEM;
		}

		memcpy(mgmt_tx->buff, buf, len);
		mgmt_tx->size = len;


		if (ieee80211_is_probe_resp(mgmt->frame_control)) {
			wilc_set_mac_chnl_num(vif, chan->hw_value);
			curr_channel = chan->hw_value;
		} else if (ieee80211_is_action(mgmt->frame_control))   {
			if (buf[ACTION_CAT_ID] == PUB_ACTION_ATTR_ID) {
				if (buf[ACTION_SUBTYPE_ID] != PUBLIC_ACT_VENDORSPEC ||
				    buf[P2P_PUB_ACTION_SUBTYPE] != GO_NEG_CONF)	{
					wilc_set_mac_chnl_num(vif,
							      chan->hw_value);
					curr_channel = chan->hw_value;
				}
				switch (buf[ACTION_SUBTYPE_ID])	{
				case GAS_INTIAL_REQ:
					break;

				case GAS_INTIAL_RSP:
					break;

				case PUBLIC_ACT_VENDORSPEC:
				{
					if (!memcmp(p2p_oui, &buf[ACTION_SUBTYPE_ID + 1], 4)) {
						if ((buf[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_REQ || buf[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_RSP)) {
							if (p2p_local_random == 1 && p2p_recv_random < p2p_local_random) {
								get_random_bytes(&p2p_local_random, 1);
								p2p_local_random++;
							}
						}

						if ((buf[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_REQ || buf[P2P_PUB_ACTION_SUBTYPE] == GO_NEG_RSP
						      || buf[P2P_PUB_ACTION_SUBTYPE] == P2P_INV_REQ || buf[P2P_PUB_ACTION_SUBTYPE] == P2P_INV_RSP)) {
							if (p2p_local_random > p2p_recv_random)	{
								for (i = P2P_PUB_ACTION_SUBTYPE + 2; i < len; i++) {
									if (buf[i] == P2PELEM_ATTR_ID && !(memcmp(p2p_oui, &buf[i + 2], 4))) {
										if (buf[P2P_PUB_ACTION_SUBTYPE] == P2P_INV_REQ || buf[P2P_PUB_ACTION_SUBTYPE] == P2P_INV_RSP)
											WILC_WFI_CfgParseTxAction(&mgmt_tx->buff[i + 6], len - (i + 6), true, vif->iftype);
										else
											WILC_WFI_CfgParseTxAction(&mgmt_tx->buff[i + 6], len - (i + 6), false, vif->iftype);
										break;
									}
								}

								if (buf[P2P_PUB_ACTION_SUBTYPE] != P2P_INV_REQ && buf[P2P_PUB_ACTION_SUBTYPE] != P2P_INV_RSP) {
									memcpy(&mgmt_tx->buff[len], p2p_vendor_spec, sizeof(p2p_vendor_spec));
									mgmt_tx->buff[len + sizeof(p2p_vendor_spec)] = p2p_local_random;
									mgmt_tx->size = buf_len;
								}
							}
						}

					} else {
						netdev_dbg(vif->ndev, "Not a P2P public action frame\n");
					}

					break;
				}

				default:
				{
					netdev_dbg(vif->ndev, "NOT HANDLED PUBLIC ACTION FRAME TYPE:%x\n", buf[ACTION_SUBTYPE_ID]);
					break;
				}
				}
			}

			pstrWFIDrv->p2p_timeout = (jiffies + msecs_to_jiffies(wait));
		}

		wilc_wlan_txq_add_mgmt_pkt(wdev->netdev, mgmt_tx,
					   mgmt_tx->buff, mgmt_tx->size,
					   WILC_WFI_mgmt_tx_complete);
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
	pstrWFIDrv = (struct host_if_drv *)priv->hif_drv;
	pstrWFIDrv->p2p_timeout = jiffies;

	if (!priv->bInP2PlistenState) {
		cfg80211_remain_on_channel_expired(priv->wdev,
						   priv->strRemainOnChanParams.u64ListenCookie,
						   priv->strRemainOnChanParams.pstrListenChan,
						   GFP_KERNEL);
	}

	return 0;
}

void wilc_mgmt_frame_register(struct wiphy *wiphy, struct wireless_dev *wdev,
			      u16 frame_type, bool reg)
{
	struct wilc_priv *priv;
	struct wilc_vif *vif;
	struct wilc *wl;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(priv->wdev->netdev);
	wl = vif->wilc;

	if (!frame_type)
		return;

	switch (frame_type) {
	case PROBE_REQ:
	{
		vif->g_struct_frame_reg[0].frame_type = frame_type;
		vif->g_struct_frame_reg[0].reg = reg;
	}
	break;

	case ACTION:
	{
		vif->g_struct_frame_reg[1].frame_type = frame_type;
		vif->g_struct_frame_reg[1].reg = reg;
	}
	break;

	default:
	{
		break;
	}
	}

	if (!wl->initialized)
		return;
	wilc_frame_register(vif, frame_type, reg);
}

static int set_cqm_rssi_config(struct wiphy *wiphy, struct net_device *dev,
			       s32 rssi_thold, u32 rssi_hyst)
{
	return 0;
}

static int dump_station(struct wiphy *wiphy, struct net_device *dev,
			int idx, u8 *mac, struct station_info *sinfo)
{
	struct wilc_priv *priv;
	struct wilc_vif *vif;

	if (idx != 0)
		return -ENOENT;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(priv->dev);

	sinfo->filled |= BIT(NL80211_STA_INFO_SIGNAL);

	wilc_get_rssi(vif, &sinfo->signal);

	return 0;
}

static int set_power_mgmt(struct wiphy *wiphy, struct net_device *dev,
			  bool enabled, int timeout)
{
	struct wilc_priv *priv;
	struct wilc_vif *vif;

	if (!wiphy)
		return -ENOENT;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(priv->dev);
	if (!priv->hif_drv)
		return -EIO;

	if (wilc_enable_ps)
		wilc_set_power_mgmt(vif, enabled, timeout);


	return 0;
}

static int change_virtual_intf(struct wiphy *wiphy, struct net_device *dev,
			       enum nl80211_iftype type, u32 *flags, struct vif_params *params)
{
	struct wilc_priv *priv;
	struct wilc_vif *vif;
	struct wilc *wl;

	vif = netdev_priv(dev);
	priv = wiphy_priv(wiphy);
	wl = vif->wilc;
	p2p_local_random = 0x01;
	p2p_recv_random = 0x00;
	wilc_ie = false;
	wilc_optaining_ip = false;
	del_timer(&wilc_during_ip_timer);

	switch (type) {
	case NL80211_IFTYPE_STATION:
		wilc_connecting = 0;
		dev->ieee80211_ptr->iftype = type;
		priv->wdev->iftype = type;
		vif->monitor_flag = 0;
		vif->iftype = STATION_MODE;
		wilc_set_operation_mode(vif, STATION_MODE);

		memset(priv->assoc_stainfo.au8Sta_AssociatedBss, 0, MAX_NUM_STA * ETH_ALEN);

		wilc_enable_ps = true;
		wilc_set_power_mgmt(vif, 1, 0);
		break;

	case NL80211_IFTYPE_P2P_CLIENT:
		wilc_connecting = 0;
		dev->ieee80211_ptr->iftype = type;
		priv->wdev->iftype = type;
		vif->monitor_flag = 0;
		vif->iftype = CLIENT_MODE;
		wilc_set_operation_mode(vif, STATION_MODE);

		wilc_enable_ps = false;
		wilc_set_power_mgmt(vif, 0, 0);
		break;

	case NL80211_IFTYPE_AP:
		wilc_enable_ps = false;
		dev->ieee80211_ptr->iftype = type;
		priv->wdev->iftype = type;
		vif->iftype = AP_MODE;

		if (wl->initialized) {
			wilc_set_wfi_drv_handler(vif, wilc_get_vif_idx(vif),
						 0);
			wilc_set_operation_mode(vif, AP_MODE);
			wilc_set_power_mgmt(vif, 0, 0);
		}
		break;

	case NL80211_IFTYPE_P2P_GO:
		wilc_optaining_ip = true;
		mod_timer(&wilc_during_ip_timer,
			  jiffies + msecs_to_jiffies(during_ip_time));
		wilc_set_operation_mode(vif, AP_MODE);
		dev->ieee80211_ptr->iftype = type;
		priv->wdev->iftype = type;
		vif->iftype = GO_MODE;

		wilc_enable_ps = false;
		wilc_set_power_mgmt(vif, 0, 0);
		break;

	default:
		netdev_err(dev, "Unknown interface type= %d\n", type);
		return -EINVAL;
	}

	return 0;
}

static int start_ap(struct wiphy *wiphy, struct net_device *dev,
		    struct cfg80211_ap_settings *settings)
{
	struct cfg80211_beacon_data *beacon = &(settings->beacon);
	struct wilc_priv *priv;
	s32 s32Error = 0;
	struct wilc *wl;
	struct wilc_vif *vif;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(dev);
	wl = vif->wilc;

	s32Error = set_channel(wiphy, &settings->chandef);

	if (s32Error != 0)
		netdev_err(dev, "Error in setting channel\n");

	wilc_wlan_set_bssid(dev, wl->vif[vif->idx]->src_addr, AP_MODE);
	wilc_set_power_mgmt(vif, 0, 0);

	s32Error = wilc_add_beacon(vif, settings->beacon_interval,
				   settings->dtim_period, beacon->head_len,
				   (u8 *)beacon->head, beacon->tail_len,
				   (u8 *)beacon->tail);

	return s32Error;
}

static int change_beacon(struct wiphy *wiphy, struct net_device *dev,
			 struct cfg80211_beacon_data *beacon)
{
	struct wilc_priv *priv;
	struct wilc_vif *vif;
	s32 s32Error = 0;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(priv->dev);

	s32Error = wilc_add_beacon(vif, 0, 0, beacon->head_len,
				   (u8 *)beacon->head, beacon->tail_len,
				   (u8 *)beacon->tail);

	return s32Error;
}

static int stop_ap(struct wiphy *wiphy, struct net_device *dev)
{
	s32 s32Error = 0;
	struct wilc_priv *priv;
	struct wilc_vif *vif;
	u8 NullBssid[ETH_ALEN] = {0};

	if (!wiphy)
		return -EFAULT;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(priv->dev);

	wilc_wlan_set_bssid(dev, NullBssid, AP_MODE);

	s32Error = wilc_del_beacon(vif);

	if (s32Error)
		netdev_err(dev, "Host delete beacon fail\n");

	return s32Error;
}

static int add_station(struct wiphy *wiphy, struct net_device *dev,
		       const u8 *mac, struct station_parameters *params)
{
	s32 s32Error = 0;
	struct wilc_priv *priv;
	struct add_sta_param strStaParams = { {0} };
	struct wilc_vif *vif;

	if (!wiphy)
		return -EFAULT;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(dev);

	if (vif->iftype == AP_MODE || vif->iftype == GO_MODE) {
		memcpy(strStaParams.bssid, mac, ETH_ALEN);
		memcpy(priv->assoc_stainfo.au8Sta_AssociatedBss[params->aid], mac, ETH_ALEN);
		strStaParams.aid = params->aid;
		strStaParams.rates_len = params->supported_rates_len;
		strStaParams.rates = params->supported_rates;

		if (!params->ht_capa) {
			strStaParams.ht_supported = false;
		} else {
			strStaParams.ht_supported = true;
			strStaParams.ht_capa_info = params->ht_capa->cap_info;
			strStaParams.ht_ampdu_params = params->ht_capa->ampdu_params_info;
			memcpy(strStaParams.ht_supp_mcs_set,
			       &params->ht_capa->mcs,
			       WILC_SUPP_MCS_SET_SIZE);
			strStaParams.ht_ext_params = params->ht_capa->extended_ht_cap_info;
			strStaParams.ht_tx_bf_cap = params->ht_capa->tx_BF_cap_info;
			strStaParams.ht_ante_sel = params->ht_capa->antenna_selection_info;
		}

		strStaParams.flags_mask = params->sta_flags_mask;
		strStaParams.flags_set = params->sta_flags_set;

		s32Error = wilc_add_station(vif, &strStaParams);
		if (s32Error)
			netdev_err(dev, "Host add station fail\n");
	}

	return s32Error;
}

static int del_station(struct wiphy *wiphy, struct net_device *dev,
		       struct station_del_parameters *params)
{
	const u8 *mac = params->mac;
	s32 s32Error = 0;
	struct wilc_priv *priv;
	struct wilc_vif *vif;

	if (!wiphy)
		return -EFAULT;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(dev);

	if (vif->iftype == AP_MODE || vif->iftype == GO_MODE) {
		if (!mac)
			s32Error = wilc_del_allstation(vif,
				     priv->assoc_stainfo.au8Sta_AssociatedBss);

		s32Error = wilc_del_station(vif, mac);

		if (s32Error)
			netdev_err(dev, "Host delete station fail\n");
	}
	return s32Error;
}

static int change_station(struct wiphy *wiphy, struct net_device *dev,
			  const u8 *mac, struct station_parameters *params)
{
	s32 s32Error = 0;
	struct wilc_priv *priv;
	struct add_sta_param strStaParams = { {0} };
	struct wilc_vif *vif;

	if (!wiphy)
		return -EFAULT;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(dev);

	if (vif->iftype == AP_MODE || vif->iftype == GO_MODE) {
		memcpy(strStaParams.bssid, mac, ETH_ALEN);
		strStaParams.aid = params->aid;
		strStaParams.rates_len = params->supported_rates_len;
		strStaParams.rates = params->supported_rates;

		if (!params->ht_capa) {
			strStaParams.ht_supported = false;
		} else {
			strStaParams.ht_supported = true;
			strStaParams.ht_capa_info = params->ht_capa->cap_info;
			strStaParams.ht_ampdu_params = params->ht_capa->ampdu_params_info;
			memcpy(strStaParams.ht_supp_mcs_set,
			       &params->ht_capa->mcs,
			       WILC_SUPP_MCS_SET_SIZE);
			strStaParams.ht_ext_params = params->ht_capa->extended_ht_cap_info;
			strStaParams.ht_tx_bf_cap = params->ht_capa->tx_BF_cap_info;
			strStaParams.ht_ante_sel = params->ht_capa->antenna_selection_info;
		}

		strStaParams.flags_mask = params->sta_flags_mask;
		strStaParams.flags_set = params->sta_flags_set;

		s32Error = wilc_edit_station(vif, &strStaParams);
		if (s32Error)
			netdev_err(dev, "Host edit station fail\n");
	}
	return s32Error;
}

static struct wireless_dev *add_virtual_intf(struct wiphy *wiphy,
					     const char *name,
					     unsigned char name_assign_type,
					     enum nl80211_iftype type,
					     u32 *flags,
					     struct vif_params *params)
{
	struct wilc_vif *vif;
	struct wilc_priv *priv;
	struct net_device *new_ifc = NULL;

	priv = wiphy_priv(wiphy);
	vif = netdev_priv(priv->wdev->netdev);


	if (type == NL80211_IFTYPE_MONITOR) {
		new_ifc = WILC_WFI_init_mon_interface(name, vif->ndev);
		if (new_ifc) {
			vif = netdev_priv(priv->wdev->netdev);
			vif->monitor_flag = 1;
		}
	}
	return priv->wdev;
}

static int del_virtual_intf(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	return 0;
}

static int wilc_suspend(struct wiphy *wiphy, struct cfg80211_wowlan *wow)
{
	struct wilc_priv *priv = wiphy_priv(wiphy);
	struct wilc_vif *vif = netdev_priv(priv->dev);

	if (!wow && wilc_wlan_get_num_conn_ifcs(vif->wilc))
		vif->wilc->suspend_event = true;
	else
		vif->wilc->suspend_event = false;

	return 0;
}

static int wilc_resume(struct wiphy *wiphy)
{
	struct wilc_priv *priv = wiphy_priv(wiphy);
	struct wilc_vif *vif = netdev_priv(priv->dev);

	netdev_info(vif->ndev, "cfg resume\n");
	return 0;
}

static void wilc_set_wakeup(struct wiphy *wiphy, bool enabled)
{
	struct wilc_priv *priv = wiphy_priv(wiphy);
	struct wilc_vif *vif = netdev_priv(priv->dev);

	netdev_info(vif->ndev, "cfg set wake up = %d\n", enabled);
}

static int set_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
			enum nl80211_tx_power_setting type, int mbm)
{
	int ret;
	s32 tx_power = MBM_TO_DBM(mbm);
	struct wilc_priv *priv = wiphy_priv(wiphy);
	struct wilc_vif *vif = netdev_priv(priv->dev);

	if (tx_power < 0)
		tx_power = 0;
	else if (tx_power > 18)
		tx_power = 18;
	ret = wilc_set_tx_power(vif, tx_power);
	if (ret)
		netdev_err(vif->ndev, "Failed to set tx power\n");

	return ret;
}

static int get_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
			int *dbm)
{
	int ret;
	struct wilc_priv *priv = wiphy_priv(wiphy);
	struct wilc_vif *vif = netdev_priv(priv->dev);
	struct wilc *wl;

	wl = vif->wilc;

	/* If firmware is not started, return. */
	if (!wl->initialized)
		return -EIO;

	ret = wilc_get_tx_power(vif, (u8 *)dbm);
	if (ret)
		netdev_err(vif->ndev, "Failed to get tx power\n");

	return ret;
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

	.suspend = wilc_suspend,
	.resume = wilc_resume,
	.set_wakeup = wilc_set_wakeup,
	.set_tx_power = set_tx_power,
	.get_tx_power = get_tx_power,

};

static struct wireless_dev *WILC_WFI_CfgAlloc(void)
{
	struct wireless_dev *wdev;

	wdev = kzalloc(sizeof(struct wireless_dev), GFP_KERNEL);
	if (!wdev)
		goto _fail_;

	wdev->wiphy = wiphy_new(&wilc_cfg80211_ops, sizeof(struct wilc_priv));
	if (!wdev->wiphy)
		goto _fail_mem_;

	WILC_WFI_band_2ghz.ht_cap.ht_supported = 1;
	WILC_WFI_band_2ghz.ht_cap.cap |= (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);
	WILC_WFI_band_2ghz.ht_cap.mcs.rx_mask[0] = 0xff;
	WILC_WFI_band_2ghz.ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K;
	WILC_WFI_band_2ghz.ht_cap.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE;

	wdev->wiphy->bands[IEEE80211_BAND_2GHZ] = &WILC_WFI_band_2ghz;

	return wdev;

_fail_mem_:
	kfree(wdev);
_fail_:
	return NULL;
}

struct wireless_dev *wilc_create_wiphy(struct net_device *net, struct device *dev)
{
	struct wilc_priv *priv;
	struct wireless_dev *wdev;
	s32 s32Error = 0;

	wdev = WILC_WFI_CfgAlloc();
	if (!wdev) {
		netdev_err(net, "wiphy new allocate failed\n");
		return NULL;
	}

	priv = wdev_priv(wdev);
	sema_init(&(priv->SemHandleUpdateStats), 1);
	priv->wdev = wdev;
	wdev->wiphy->max_scan_ssids = MAX_NUM_PROBED_SSID;
#ifdef CONFIG_PM
	wdev->wiphy->wowlan = &wowlan_support;
#endif
	wdev->wiphy->max_num_pmkids = WILC_MAX_NUM_PMKIDS;
	wdev->wiphy->max_scan_ie_len = 1000;
	wdev->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wdev->wiphy->cipher_suites = cipher_suites;
	wdev->wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);
	wdev->wiphy->mgmt_stypes = wilc_wfi_cfg80211_mgmt_types;

	wdev->wiphy->max_remain_on_channel_duration = 500;
	wdev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP) | BIT(NL80211_IFTYPE_MONITOR) | BIT(NL80211_IFTYPE_P2P_GO) |
		BIT(NL80211_IFTYPE_P2P_CLIENT);
	wdev->wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
	wdev->iftype = NL80211_IFTYPE_STATION;

	set_wiphy_dev(wdev->wiphy, dev);

	s32Error = wiphy_register(wdev->wiphy);
	if (s32Error)
		netdev_err(net, "Cannot register wiphy device\n");

	priv->dev = net;
	return wdev;
}

int wilc_init_host_int(struct net_device *net)
{
	int s32Error = 0;

	struct wilc_priv *priv;

	priv = wdev_priv(net->ieee80211_ptr);
	if (op_ifcs == 0) {
		setup_timer(&hAgingTimer, remove_network_from_shadow, 0);
		setup_timer(&wilc_during_ip_timer, clear_duringIP, 0);
	}
	op_ifcs++;

	priv->gbAutoRateAdjusted = false;

	priv->bInP2PlistenState = false;

	sema_init(&(priv->hSemScanReq), 1);
	s32Error = wilc_init(net, &priv->hif_drv);
	if (s32Error)
		netdev_err(net, "Error while initializing hostinterface\n");

	return s32Error;
}

int wilc_deinit_host_int(struct net_device *net)
{
	int s32Error = 0;
	struct wilc_vif *vif;
	struct wilc_priv *priv;

	priv = wdev_priv(net->ieee80211_ptr);
	vif = netdev_priv(priv->dev);

	priv->gbAutoRateAdjusted = false;

	priv->bInP2PlistenState = false;

	op_ifcs--;

	s32Error = wilc_deinit(vif);

	clear_shadow_scan();
	if (op_ifcs == 0)
		del_timer_sync(&wilc_during_ip_timer);

	if (s32Error)
		netdev_err(net, "Error while deintializing host interface\n");

	return s32Error;
}

void wilc_free_wiphy(struct net_device *net)
{
	if (!net)
		return;

	if (!net->ieee80211_ptr)
		return;

	if (!net->ieee80211_ptr->wiphy)
		return;

	wiphy_unregister(net->ieee80211_ptr->wiphy);

	wiphy_free(net->ieee80211_ptr->wiphy);
	kfree(net->ieee80211_ptr);
}
