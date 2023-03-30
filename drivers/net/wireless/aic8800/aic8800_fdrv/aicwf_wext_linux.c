// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <net/netlink.h>
#include <linux/wireless.h>
#include <linux/nl80211.h>
#include <net/iw_handler.h>
#include <uapi/linux/if_arp.h>
#include <linux/vmalloc.h>
#include "aicwf_debug.h"
#include "rwnx_msg_tx.h"
#include "aicwf_wext_linux.h"
#include "rwnx_defs.h"

#define WLAN_CAPABILITY_ESS		(1<<0)
#define WLAN_CAPABILITY_IBSS	(1<<1)
#define WLAN_CAPABILITY_PRIVACY (1<<4)

#define IEEE80211_HT_CAP_SGI_20			0x0020
#define IEEE80211_HT_CAP_SGI_40			0x0040

#define IEEE80211_HE_CH_BW_SET_160_80P80 		1 << 4
#define IEEE80211_HE_CH_BW_SET_160_IN_5G 		1 << 3
#define IEEE80211_HE_CH_BW_SET_40_AND_80_IN_5G 	1 << 2
#define IEEE80211_HE_CH_BW_SET_40_IN_2_4G 		1 << 1

#if WIRELESS_EXT < 17
	#define IW_QUAL_QUAL_INVALID   0x10
	#define IW_QUAL_LEVEL_INVALID  0x20
	#define IW_QUAL_NOISE_INVALID  0x40
	#define IW_QUAL_QUAL_UPDATED   0x1
	#define IW_QUAL_LEVEL_UPDATED  0x2
	#define IW_QUAL_NOISE_UPDATED  0x4
#endif

#define MAX_WPA_IE_LEN (256)


/*				20/40/80,	ShortGI,	MCS Rate  */
const u16 VHT_MCS_DATA_RATE[3][2][30] = {
	{	
		{
			13, 26, 39, 52, 78, 104, 117, 130, 156, 156,
			26, 52, 78, 104, 156, 208, 234, 260, 312, 312,
			39, 78, 117, 156, 234, 312, 351, 390, 468, 520
		},	/* Long GI, 20MHz */
		{
			14, 29, 43, 58, 87, 116, 130, 144, 173, 173,
			29, 58, 87, 116, 173, 231, 260, 289, 347, 347,
			43,	87, 130, 173, 260, 347, 390,	433,	520, 578
		}
	},		/* Short GI, 20MHz */
	{	
		{
			27, 54, 81, 108, 162, 216, 243, 270, 324, 360,
			54, 108, 162, 216, 324, 432, 486, 540, 648, 720,
			81, 162, 243, 324, 486, 648, 729, 810, 972, 1080
		}, 	/* Long GI, 40MHz */
		{
			30, 60, 90, 120, 180, 240, 270, 300, 360, 400,
			60, 120, 180, 240, 360, 480, 540, 600, 720, 800,
			90, 180, 270, 360, 540, 720, 810, 900, 1080, 1200
		}
	},		/* Short GI, 40MHz */
	{	
		{
			59, 117,  176, 234, 351, 468, 527, 585, 702, 780,
			117, 234, 351, 468, 702, 936, 1053, 1170, 1404, 1560,
			176, 351, 527, 702, 1053, 1404, 1580, 1755, 2106, 2340
		},	/* Long GI, 80MHz */
		{
			65, 130, 195, 260, 390, 520, 585, 650, 780, 867,
			130, 260, 390, 520, 780, 1040, 1170, 1300, 1560, 1734,
			195, 390, 585, 780, 1170, 1560, 1755, 1950, 2340, 2600
		}	/* Short GI, 80MHz */
	}	
};


/*HE 20/40/80,MCS Rate  */
const u16 HE_MCS_DATA_RATE[3][30] = {
	{
		9, 17, 26, 34, 52, 69, 77, 86, 103, 115,
		129, 143, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	},	/* 20MHz */
	{
		17, 34, 52, 69, 103, 138, 155, 172, 207, 229,
		258, 286, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	}, 	/* 40MHz */
	{
		36, 72,  108, 144, 216, 288, 324, 360, 432, 480,
		540, 601, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	},	/* 80MHz */
	
};


#if WIRELESS_EXT >= 17
struct iw_statistics iwstats;

static struct iw_statistics *aicwf_get_wireless_stats(struct net_device *dev)
{
	int tmp_level = -100;
	int tmp_qual = 0;
	int tmp_noise = 0;

	struct rwnx_vif* rwnx_vif = netdev_priv(dev);
	struct rwnx_hw* rwnx_hw = rwnx_vif->rwnx_hw;
	struct rwnx_sta *sta = NULL; 

	union rwnx_rate_ctrl_info *rate_info;
	struct mm_get_sta_info_cfm cfm;

	AICWFDBG(LOGTRACE, "%s Enter", __func__);

	if(rwnx_vif->sta.ap){
		sta = rwnx_vif->sta.ap;
		rwnx_send_get_sta_info_req(rwnx_hw, sta->sta_idx, &cfm);
			rate_info = (union rwnx_rate_ctrl_info *)&cfm.rate_info;
			tmp_level = cfm.rssi;
	}


	iwstats.qual.level = tmp_level;
	iwstats.qual.qual = tmp_qual;
	iwstats.qual.noise = tmp_noise;
	iwstats.qual.updated = 0x07;
	iwstats.qual.updated = iwstats.qual.updated | IW_QUAL_DBM;

	return &iwstats;
}
#endif


static int aicwf_get_name(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct rwnx_vif* rwnx_vif = netdev_priv(dev);
	struct rwnx_hw* rwnx_hw = rwnx_vif->rwnx_hw;
	struct rwnx_sta *sta = NULL; 

	union rwnx_rate_ctrl_info *rate_info;
	struct mm_get_sta_info_cfm cfm;

	AICWFDBG(LOGTRACE, "%s Enter", __func__);

	if(rwnx_vif->sta.ap){
		sta = rwnx_vif->sta.ap;
		rwnx_send_get_sta_info_req(rwnx_hw, sta->sta_idx, &cfm);
			rate_info = (union rwnx_rate_ctrl_info *)&cfm.rate_info;
		
			switch (rate_info->formatModTx) {
			case FORMATMOD_NON_HT:
			case FORMATMOD_NON_HT_DUP_OFDM:
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bg");
				break;
			case FORMATMOD_HT_MF:
			case FORMATMOD_HT_GF:
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11n");
				break;
			case FORMATMOD_VHT:
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11ac");
				break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
			case FORMATMOD_HE_MU:
			case FORMATMOD_HE_SU:
			case FORMATMOD_HE_ER:
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11ax");
				break;
#else
			//kernel not support he
			case FORMATMOD_HE_MU:
			case FORMATMOD_HE_SU:
			case FORMATMOD_HE_ER:
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11ax");
				break;
#endif
			}

		
	}else{
		snprintf(wrqu->name, IFNAMSIZ, "unassociated");
	}


	return 0;
}

static int aicwf_get_freq(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct rwnx_vif* rwnx_vif = netdev_priv(dev);


	AICWFDBG(LOGTRACE, "%s Enter", __func__);

	if(rwnx_vif->sta.ap){
		wrqu->freq.m = rwnx_vif->sta.ap->center_freq * 100000;
		wrqu->freq.e = 1;
		wrqu->freq.i = ieee80211_frequency_to_channel(rwnx_vif->sta.ap->center_freq);
	}else{
		wrqu->freq.m = 2412 * 100000;
		wrqu->freq.e = 1;
		wrqu->freq.i = 1;
	}

	return 0;
}

static int aicwf_get_mode(struct net_device *dev, struct iw_request_info *a,
			   union iwreq_data *wrqu, char *b)
{

	AICWFDBG(LOGTRACE, "%s Enter", __func__);

	wrqu->mode = IW_MODE_AUTO;
#if 0
	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
		wrqu->mode = IW_MODE_INFRA;
	else if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) ||
		 (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE))

		wrqu->mode = IW_MODE_ADHOC;
	else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
		wrqu->mode = IW_MODE_MASTER;
	else if (check_fwstate(pmlmepriv, WIFI_MONITOR_STATE) == _TRUE)
		wrqu->mode = IW_MODE_MONITOR;
	else
		wrqu->mode = IW_MODE_AUTO;
#endif

	return 0;

}


static int aicwf_get_range(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;
	u16 val = 0;

	AICWFDBG(LOGTRACE, "%s Enter", __func__);


	wrqu->data.length = sizeof(*range);
	memset(range, 0, sizeof(*range));

	/* Let's try to keep this struct in the same order as in
	 * linux/include/wireless.h
	 */

	/* TODO: See what values we can set, and remove the ones we can't
	 * set, or fill them with some default data.
	 */

	/* ~5 Mb/s real (802.11b) */
	range->throughput = 5 * 1000 * 1000;

	/* TODO: Not used in 802.11b?
	*	range->min_nwid;	 Minimal NWID we are able to set  */
	/* TODO: Not used in 802.11b?
	*	range->max_nwid;	 Maximal NWID we are able to set  */

	/* Old Frequency (backward compat - moved lower ) */
	/*	range->old_num_channels;
	 *	range->old_num_frequency;
	 * 	range->old_freq[6];  Filler to keep "version" at the same offset  */

	/* signal level threshold range */

	/* Quality of link & SNR stuff */
	/* Quality range (link, level, noise)
	 * If the quality is absolute, it will be in the range [0 ; max_qual],
	 * if the quality is dBm, it will be in the range [max_qual ; 0].
	 * Don't forget that we use 8 bit arithmetics...
	 *
	 * If percentage range is 0~100
	 * Signal strength dbm range logical is -100 ~ 0
	 * but usually value is -90 ~ -20
	 */
	range->max_qual.qual = 100;
#ifdef CONFIG_SIGNAL_DISPLAY_DBM
	range->max_qual.level = (u8)-100;
	range->max_qual.noise = (u8)-100;
	range->max_qual.updated = IW_QUAL_ALL_UPDATED; /* Updated all three */
	range->max_qual.updated |= IW_QUAL_DBM;
#else /* !CONFIG_SIGNAL_DISPLAY_DBM */
	/* percent values between 0 and 100. */
	range->max_qual.level = 100;
	range->max_qual.noise = 100;
	range->max_qual.updated = IW_QUAL_ALL_UPDATED; /* Updated all three */
#endif /* !CONFIG_SIGNAL_DISPLAY_DBM */

	/* This should contain the average/typical values of the quality
	 * indicator. This should be the threshold between a "good" and
	 * a "bad" link (example : monitor going from green to orange).
	 * Currently, user space apps like quality monitors don't have any
	 * way to calibrate the measurement. With this, they can split
	 * the range between 0 and max_qual in different quality level
	 * (using a geometric subdivision centered on the average).
	 * I expect that people doing the user space apps will feedback
	 * us on which value we need to put in each driver... */
	range->avg_qual.qual = 92; /* > 8% missed beacons is 'bad' */
#ifdef CONFIG_SIGNAL_DISPLAY_DBM
	/* TODO: Find real 'good' to 'bad' threshold value for RSSI */
	range->avg_qual.level = (u8)-70;
	range->avg_qual.noise = 0;
	range->avg_qual.updated = IW_QUAL_ALL_UPDATED; /* Updated all three */
	range->avg_qual.updated |= IW_QUAL_DBM;
#else /* !CONFIG_SIGNAL_DISPLAY_DBM */
	/* TODO: Find real 'good' to 'bad' threshol value for RSSI */
	range->avg_qual.level = 30;
	range->avg_qual.noise = 100;
	range->avg_qual.updated = IW_QUAL_ALL_UPDATED; /* Updated all three */
#endif /* !CONFIG_SIGNAL_DISPLAY_DBM */
#if 0
	range->num_bitrates = RATE_COUNT;

	for (i = 0; i < RATE_COUNT && i < IW_MAX_BITRATES; i++)
		range->bitrate[i] = rtw_rates[i];

	range->min_frag = MIN_FRAG_THRESHOLD;
	range->max_frag = MAX_FRAG_THRESHOLD;
#endif

	range->pm_capa = 0;

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 16;

	/*	range->retry_capa;	 What retry options are supported
	 *	range->retry_flags;	 How to decode max/min retry limit
	 *	range->r_time_flags;	 How to decode max/min retry life
	 *	range->min_retry;	 Minimal number of retries
	 *	range->max_retry;	 Maximal number of retries
	 *	range->min_r_time;	 Minimal retry lifetime
	 *	range->max_r_time;	 Maximal retry lifetime  */
#if 0 
	for (i = 0, val = 0; i < rfctl->max_chan_nums; i++) {

		/* Include only legal frequencies for some countries */
		if (rfctl->channel_set[i].ChannelNum != 0) {
			range->freq[val].i = rfctl->channel_set[i].ChannelNum;
			range->freq[val].m = rtw_ch2freq(rfctl->channel_set[i].ChannelNum) * 100000;
			range->freq[val].e = 1;
			val++;
		}

		if (val == IW_MAX_FREQUENCIES)
			break;
	}
#endif
	range->num_channels = val;
	range->num_frequency = val;

	/* Commented by Albert 2009/10/13
	 * The following code will proivde the security capability to network manager.
	 * If the driver doesn't provide this capability to network manager,
	 * the WPA/WPA2 routers can't be choosen in the network manager. */

	/*
	#define IW_SCAN_CAPA_NONE		0x00
	#define IW_SCAN_CAPA_ESSID		0x01
	#define IW_SCAN_CAPA_BSSID		0x02
	#define IW_SCAN_CAPA_CHANNEL	0x04
	#define IW_SCAN_CAPA_MODE		0x08
	#define IW_SCAN_CAPA_RATE		0x10
	#define IW_SCAN_CAPA_TYPE		0x20
	#define IW_SCAN_CAPA_TIME		0x40
	*/

#if WIRELESS_EXT > 17
	range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
			  IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;
#endif

#ifdef IW_SCAN_CAPA_ESSID /* WIRELESS_EXT > 21 */
	range->scan_capa = IW_SCAN_CAPA_ESSID | IW_SCAN_CAPA_TYPE | IW_SCAN_CAPA_BSSID |
		   IW_SCAN_CAPA_CHANNEL | IW_SCAN_CAPA_MODE | IW_SCAN_CAPA_RATE;
#endif



	return 0;

}


static char *aicwf_get_iwe_stream_mac_addr(struct rwnx_hw* rwnx_hw,
		struct iw_request_info *info, struct scanu_result_wext *scan_re,
		char *start, char *stop, struct iw_event *iwe)
{
	/*  AP MAC address */
	iwe->cmd = SIOCGIWAP;
	iwe->u.ap_addr.sa_family = ARPHRD_ETHER;

	memcpy(iwe->u.ap_addr.sa_data, scan_re->bss->bssid, ETH_ALEN);
	start = iwe_stream_add_event(info, start, stop, iwe, IW_EV_ADDR_LEN);
	return start;
}


static inline char *aicwf_get_iwe_stream_essid(struct rwnx_hw* rwnx_hw,
		struct iw_request_info *info, struct scanu_result_wext *scan_re,
		char *start, char *stop, struct iw_event *iwe)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt*)scan_re->payload;
	const u8 *ie = mgmt->u.beacon.variable;
	u8 *ssid;
	int ssid_len = 0;

	//get ssid len form ie
	ssid_len = ie[1];

	//get ssid form ie
	ssid = (u8*)vmalloc(sizeof(char)* (ssid_len + 1));
	memset(ssid, 0, (ssid_len + 1));
	memcpy(ssid, &ie[2], ssid_len);

	
	//AICWFDBG(LOGDEBUG, "%s len:%d ssid:%s\r\n", __func__, ssid_len, ssid);

	/* Add the ESSID */
	iwe->cmd = SIOCGIWESSID;
	iwe->u.data.flags = 1;
	iwe->u.data.length = min((u16)ssid_len, (u16)32);
	start = iwe_stream_add_point(info, start, stop, iwe, ssid);

	vfree(ssid);

	return start;
}



static inline char *aicwf_get_iwe_stream_protocol(struct rwnx_hw* rwnx_hw,
		struct iw_request_info *info, struct scanu_result_wext *scan_re,
		char *start, char *stop, struct iw_event *iwe)
{
	u16 ht_cap = false; 
	u16 vht_cap = false;
	u16 he_cap = false;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt*)scan_re->payload;
	u8 *payload = mgmt->u.beacon.variable;
	const u8 *ie_content;

	/* parsing HT_CAP_IE	 */
	ie_content = NULL;
	ie_content = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, payload, scan_re->ind->length);
	if (ie_content != NULL){
		ht_cap = true;
	}

	/* parsing VHT_CAP_IE	 */
	ie_content = NULL;
	ie_content = cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, payload, scan_re->ind->length);
	if (ie_content != NULL){
		vht_cap = true;
	}
	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)|| defined(CONFIG_HE_FOR_OLD_KERNEL)
	/* parsing HE_CAP_IE	 */
	ie_content = NULL;
	ie_content = cfg80211_find_ie(WLAN_EID_EXTENSION, payload, scan_re->ind->length);
	if (ie_content != NULL && ie_content[2] == WLAN_EID_EXT_HE_CAPABILITY){
		he_cap = true;
	}
#endif

	/* Add the protocol name */
	iwe->cmd = SIOCGIWNAME;

	if (ieee80211_frequency_to_channel(scan_re->ind->center_freq) > 14) {
		if (he_cap == true){
			snprintf(iwe->u.name, IFNAMSIZ, "IEEE 802.11ax");
		}else if(vht_cap == true){
			snprintf(iwe->u.name, IFNAMSIZ, "IEEE 802.11ac");
		}else{
			if (ht_cap == true)
				snprintf(iwe->u.name, IFNAMSIZ, "IEEE 802.11an");
			else
				snprintf(iwe->u.name, IFNAMSIZ, "IEEE 802.11a");
		}
	} else {
		if(he_cap == true){
			snprintf(iwe->u.name, IFNAMSIZ, "IEEE 802.11ax");
		}else if (ht_cap == true){
			snprintf(iwe->u.name, IFNAMSIZ, "IEEE 802.11bgn");
		}else{
			snprintf(iwe->u.name, IFNAMSIZ, "IEEE 802.11bg");
		}
	}
	
	start = iwe_stream_add_event(info, start, stop, iwe, IW_EV_CHAR_LEN);
	return start;
}



static inline char *aicwf_get_iwe_stream_rssi(struct rwnx_hw* rwnx_hw,
		struct iw_request_info *info, struct scanu_result_wext *scan_re,
		char *start, char *stop, struct iw_event *iwe)
{
	iwe->cmd = IWEVQUAL;

	iwe->u.qual.updated = IW_QUAL_QUAL_UPDATED | IW_QUAL_LEVEL_UPDATED
		| IW_QUAL_NOISE_INVALID
		| IW_QUAL_DBM;

	iwe->u.qual.level = (u8)scan_re->ind->rssi;
	iwe->u.qual.qual = 100;//scan_re->bss->signal;
	iwe->u.qual.noise = 0;
	
	start = iwe_stream_add_event(info, start, stop, iwe, IW_EV_QUAL_LEN);
	
	return start;
}

static inline char *aicwf_get_iwe_stream_chan(struct rwnx_hw* rwnx_hw,
		struct iw_request_info *info, struct scanu_result_wext *scan_re,
		char *start, char *stop, struct iw_event *iwe)
{
	/* Add frequency/channel */
	iwe->cmd = SIOCGIWFREQ;
	iwe->u.freq.m = scan_re->ind->center_freq * 100000;
	iwe->u.freq.e = 1;
	iwe->u.freq.i = ieee80211_frequency_to_channel(scan_re->ind->center_freq);
	start = iwe_stream_add_event(info, start, stop, iwe, IW_EV_FREQ_LEN);

	return start;
}


static inline char *aicwf_get_iwe_stream_mode(struct rwnx_hw* rwnx_hw,
		struct iw_request_info *info, struct scanu_result_wext *scan_re,
		char *start, char *stop, struct iw_event *iwe)
{

	u16 cap = scan_re->bss->capability;
	/* Add mode */
	if (cap & (WLAN_CAPABILITY_IBSS | WLAN_CAPABILITY_ESS)) {
		iwe->cmd = SIOCGIWMODE;
		if (cap & WLAN_CAPABILITY_ESS)
			iwe->u.mode = IW_MODE_MASTER;
		else
			iwe->u.mode = IW_MODE_ADHOC;

		start = iwe_stream_add_event(info, start, stop, iwe, IW_EV_UINT_LEN);
	}
	return start;

}

static inline char *aicwf_get_iwe_stream_encryption(struct rwnx_hw* rwnx_hw,
		struct iw_request_info *info, struct scanu_result_wext *scan_re,
		char *start, char *stop, struct iw_event *iwe)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt*)scan_re->payload;
	const u8 *ie = mgmt->u.beacon.variable;
	u16 cap = scan_re->bss->capability;
	u8 *ssid;
	int ssid_len = 0;

	//get ssid len form ie
	ssid_len = ie[1];

	//get ssid form ie
	ssid = (u8*)vmalloc(sizeof(char)* (ssid_len + 1));
	memset(ssid, 0, (ssid_len + 1));
	memcpy(ssid, &ie[2], ssid_len);

	
	/* Add encryption capability */
	iwe->cmd = SIOCGIWENCODE;
	if (cap & WLAN_CAPABILITY_PRIVACY)
		iwe->u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe->u.data.flags = IW_ENCODE_DISABLED;
	iwe->u.data.length = 0;
	start = iwe_stream_add_point(info, start, stop, iwe, ssid);
	vfree(ssid);
	
	return start;

}

static inline char *aicwf_get_iwe_stream_rate(struct rwnx_hw* rwnx_hw,
		struct iw_request_info *info, struct scanu_result_wext *scan_re,
		char *start, char *stop, struct iw_event *iwe)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt*)scan_re->payload;
	u8 *payload = mgmt->u.beacon.variable;
	const u8 *ie_content;
	
	u16 mcs_rate = 0;
	u8 bw_40MHz = 0;
	u8 short_GI = 0;
	u16 max_rate = 0;
	u8 bw_160MHz = 0;
	
	u16 ht_cap = false;
	struct ieee80211_ht_cap *ht_capie;
	
	u16 vht_cap = false;
	u8 tx_mcs_map[2];
	u8 tx_mcs_index = 0;
	u16 vht_data_rate = 0;

	u16 he_cap = false;
	u8 he_ch_width_set = 0;
	u8 he_bw = 0;

	/* parsing HT_CAP_IE	 */
	ie_content = NULL;
	ie_content = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, payload, scan_re->ind->length);
	if (ie_content != NULL){
		ht_cap = true;
		ht_capie = (struct ieee80211_ht_cap *)(ie_content + 2);
		bw_40MHz = (ht_capie->cap_info & NL80211_CHAN_WIDTH_40) ? 1 : 0;
		short_GI = (ht_capie->cap_info & (IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40)) ? 1 : 0;
		memcpy(&mcs_rate, ht_capie->mcs.rx_mask, 2);
	}



	/* parsing VHT_CAP_IE	 */
	ie_content = NULL;
	ie_content = cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, payload, scan_re->ind->length);
	if (ie_content != NULL){
		
		vht_cap = true;
		bw_160MHz = ((*(u8*)(ie_content + 2)) >> 2) & ((u8)(0xFF >> (8 - (2))));
		if (bw_160MHz){
			short_GI = ((*(u8*)(ie_content + 2)) >> 6) & ((u8)(0xFF >> (8 - (1))));
		}else{
			short_GI = ((*(u8*)(ie_content + 2)) >> 5) & ((u8)(0xFF >> (8 - (1))));
		}

		memcpy(tx_mcs_map, ((ie_content + 2)+8), 2);

		tx_mcs_index = (tx_mcs_map[0] & 0x0F) - 1;
		if(ieee80211_frequency_to_channel(scan_re->ind->center_freq) > 14){
			vht_data_rate = VHT_MCS_DATA_RATE[2][short_GI][tx_mcs_index];
		}else{
			vht_data_rate = VHT_MCS_DATA_RATE[1][short_GI][tx_mcs_index];
		}
		//TO DO:
		//need to counter antenna number for AC
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)|| defined(CONFIG_HE_FOR_OLD_KERNEL)
	/* parsing HE_CAP_IE	 */
	ie_content = NULL;
	/*0xFF+len+WLAN_EID_EXTENSION+playload */
	ie_content = cfg80211_find_ie(WLAN_EID_EXTENSION, payload, scan_re->ind->length);
	if (ie_content != NULL && ie_content[2] == WLAN_EID_EXT_HE_CAPABILITY){
		he_cap = true;
		he_ch_width_set = ie_content[8];
		if(he_ch_width_set & IEEE80211_HE_CH_BW_SET_160_80P80){
			he_bw = NL80211_CHAN_WIDTH_80;
		}else if(he_ch_width_set & IEEE80211_HE_CH_BW_SET_160_IN_5G){
			he_bw = NL80211_CHAN_WIDTH_160;
		}else if(he_ch_width_set & IEEE80211_HE_CH_BW_SET_40_AND_80_IN_5G){
			he_bw = NL80211_CHAN_WIDTH_40;
		}else if(he_ch_width_set & IEEE80211_HE_CH_BW_SET_40_IN_2_4G){
			he_bw = NL80211_CHAN_WIDTH_40;
		}else{
			he_bw = NL80211_CHAN_WIDTH_20;
		}
		//TO DO:
		//need to counter antenna number for AX
	}
#endif

	if(he_cap == true){
		if(he_bw == NL80211_CHAN_WIDTH_20){
			max_rate = 144;
		}else if(he_bw == NL80211_CHAN_WIDTH_40){
			max_rate = 287;
		}else if(he_bw == NL80211_CHAN_WIDTH_80){
			max_rate = 601;
		}else if(he_bw == NL80211_CHAN_WIDTH_160){
			max_rate = 1147;
		}
		max_rate = max_rate * 2;
	}else if(vht_cap == true){
		max_rate = vht_data_rate;
	}else if (ht_cap == true) {
		if (mcs_rate & 0x8000) /* MCS15 */
			max_rate = (bw_40MHz) ? ((short_GI) ? 300 : 270) : ((short_GI) ? 144 : 130);
	
		else if (mcs_rate & 0x0080) /* MCS7 */
			max_rate = (bw_40MHz) ? ((short_GI) ? 150 : 135) : ((short_GI) ? 72 : 65);
		else { /* default MCS7 */
			max_rate = (bw_40MHz) ? ((short_GI) ? 150 : 135) : ((short_GI) ? 72 : 65);
		}
	}

	iwe->cmd = SIOCGIWRATE;
	iwe->u.bitrate.fixed = iwe->u.bitrate.disabled = 0;
	iwe->u.bitrate.value = max_rate * 1000000;
	start = iwe_stream_add_event(info, start, stop, iwe, IW_EV_PARAM_LEN);
	return start ;
}



int aic_get_sec_ie(u8 *in_ie, uint in_len, u8 *rsn_ie, u16 *rsn_len, u8 *wpa_ie, u16 *wpa_len)
{

	u8 authmode;
	u8 sec_idx;
	u8 wpa_oui[4] = {0x00, 0x50, 0xf2, 0x01};
	uint cnt = 0;

	/* Search required WPA or WPA2 IE and copy to sec_ie[ ] */

	cnt = 0;

	sec_idx = 0;

	while (cnt < in_len) {
		authmode = in_ie[cnt];

		if ((authmode == 0xdd/*_WPA_IE_ID_*/) && (memcmp(&in_ie[cnt + 2], &wpa_oui[0], 4) == 0)) {
			if (wpa_ie)
				memcpy(wpa_ie, &in_ie[cnt], in_ie[cnt + 1] + 2);

			*wpa_len = in_ie[cnt + 1] + 2;
			cnt += in_ie[cnt + 1] + 2; /* get next */
		} else {
			if (authmode == 0x30/*_WPA2_IE_ID_*/) {
				if (rsn_ie)
					memcpy(rsn_ie, &in_ie[cnt], in_ie[cnt + 1] + 2);

				*rsn_len = in_ie[cnt + 1] + 2;
				cnt += in_ie[cnt + 1] + 2; /* get next */
			} else {
				cnt += in_ie[cnt + 1] + 2; /* get next */
			}
		}

	}


	return *rsn_len + *wpa_len;

}


static inline char *aicwf_get_iwe_stream_wpa_wpa2(struct rwnx_hw* rwnx_hw,
		struct iw_request_info *info, struct scanu_result_wext *scan_re,
		char *start, char *stop, struct iw_event *iwe)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt*)scan_re->payload;
	u8 *payload = mgmt->u.beacon.variable;
	int buf_size = MAX_WPA_IE_LEN * 2;
	u8 *pbuf;

	u8 wpa_ie[255] = {0}, rsn_ie[255] = {0};
	u16 i, wpa_len = 0, rsn_len = 0;
	u8 *p;
	int out_len = 0;

	pbuf = (u8*)vmalloc(sizeof(u8) * buf_size);

	if (pbuf) {
		p = pbuf;

		/* parsing WPA/WPA2 IE */
			out_len = aic_get_sec_ie(payload , scan_re->ind->length, rsn_ie, &rsn_len, wpa_ie, &wpa_len);

			if (wpa_len > 0) {

				memset(pbuf, 0, buf_size);
				p += sprintf(p, "wpa_ie=");
				for (i = 0; i < wpa_len; i++)
					p += sprintf(p, "%02x", wpa_ie[i]);

				if (wpa_len > 100) {
					printk("-----------------Len %d----------------\n", wpa_len);
					for (i = 0; i < wpa_len; i++)
						printk("%02x ", wpa_ie[i]);
					printk("\n");
					printk("-----------------Len %d----------------\n", wpa_len);
				}

				memset(iwe, 0, sizeof(*iwe));
				iwe->cmd = IWEVCUSTOM;
				iwe->u.data.length = strlen(pbuf);
				start = iwe_stream_add_point(info, start, stop, iwe, pbuf);

				memset(iwe, 0, sizeof(*iwe));
				iwe->cmd = IWEVGENIE;
				iwe->u.data.length = wpa_len;
				start = iwe_stream_add_point(info, start, stop, iwe, wpa_ie);
			}
			if (rsn_len > 0) {

				memset(pbuf, 0, buf_size);
				p += sprintf(p, "rsn_ie=");
				for (i = 0; i < rsn_len; i++)
					p += sprintf(p, "%02x", rsn_ie[i]);
				memset(iwe, 0, sizeof(*iwe));
				iwe->cmd = IWEVCUSTOM;
				iwe->u.data.length = strlen(pbuf);
				start = iwe_stream_add_point(info, start, stop, iwe, pbuf);

				memset(iwe, 0, sizeof(*iwe));
				iwe->cmd = IWEVGENIE;
				iwe->u.data.length = rsn_len;
				start = iwe_stream_add_point(info, start, stop, iwe, rsn_ie);
			}

		vfree(pbuf);
	}
	return start;
}


u8 aicwf_get_is_wps_ie(u8 *ie_ptr, uint *wps_ielen)
{
	u8 match = false;
	u8 eid, wps_oui[4] = {0x0, 0x50, 0xf2, 0x04};

	if (ie_ptr == NULL)
		return match;

	eid = ie_ptr[0];

	if ((eid == 0xdd/*_WPA_IE_ID_*/) && (memcmp(&ie_ptr[2], wps_oui, 4) == 0)) {
		*wps_ielen = ie_ptr[1] + 2;
		match = true;
	}
	return match;
}


static inline char *aicwf_get_iwe_stream_wps(struct rwnx_hw* rwnx_hw,
		struct iw_request_info *info, struct scanu_result_wext *scan_re,
		char *start, char *stop, struct iw_event *iwe)
{

	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt*)scan_re->payload;
	u8 *payload = mgmt->u.beacon.variable;

	/* parsing WPS IE */
	uint cnt = 0, total_ielen;
	u8 *wpsie_ptr = NULL;
	uint wps_ielen = 0;
	u8 *ie_ptr = payload;
	
	total_ielen = scan_re->ind->length;
	
	while (cnt < total_ielen) {
		if (aicwf_get_is_wps_ie(&ie_ptr[cnt], &wps_ielen) && (wps_ielen > 2)) {
			wpsie_ptr = &ie_ptr[cnt];
			iwe->cmd = IWEVGENIE;
			iwe->u.data.length = (u16)wps_ielen;
			start = iwe_stream_add_point(info, start, stop, iwe, wpsie_ptr);
		}
		cnt += ie_ptr[cnt + 1] + 2; /* goto next */
	}
	return start;
}


static char *translate_scan(struct rwnx_hw* rwnx_hw,
		struct iw_request_info *info, struct scanu_result_wext *scan_re,
		char *start, char *stop)
{
	struct iw_event iwe;
	memset(&iwe, 0, sizeof(iwe));

	
	start = aicwf_get_iwe_stream_mac_addr(rwnx_hw, info, scan_re, start, stop, &iwe);
	start = aicwf_get_iwe_stream_essid(rwnx_hw, info, scan_re, start, stop, &iwe);
	start = aicwf_get_iwe_stream_protocol(rwnx_hw, info, scan_re, start, stop, &iwe);
	start = aicwf_get_iwe_stream_chan(rwnx_hw, info, scan_re, start, stop, &iwe);
	start = aicwf_get_iwe_stream_mode(rwnx_hw, info, scan_re, start, stop, &iwe);
	start = aicwf_get_iwe_stream_encryption(rwnx_hw, info, scan_re, start, stop, &iwe);
	start = aicwf_get_iwe_stream_rate(rwnx_hw, info, scan_re, start, stop, &iwe);
	start = aicwf_get_iwe_stream_wpa_wpa2(rwnx_hw, info, scan_re, start, stop, &iwe);
	start = aicwf_get_iwe_stream_wps(rwnx_hw, info, scan_re, start, stop, &iwe);
	start = aicwf_get_iwe_stream_rssi(rwnx_hw, info, scan_re, start, stop, &iwe);

	return start;
}



static int aicwf_get_wap(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{

	struct rwnx_vif* rwnx_vif = netdev_priv(dev);


	AICWFDBG(LOGTRACE, "%s Enter", __func__);
	
	wrqu->ap_addr.sa_family = ARPHRD_ETHER;

	memset(wrqu->ap_addr.sa_data, 0, ETH_ALEN);

	if(rwnx_vif->sta.ap){
		memcpy(wrqu->ap_addr.sa_data, rwnx_vif->sta.ap->mac_addr, ETH_ALEN);
	}else{
		memset(wrqu->ap_addr.sa_data, 0, ETH_ALEN);
	}

	return 0;

}



static int aicwf_set_scan(struct net_device *dev, struct iw_request_info *a,
			   union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct rwnx_vif* rwnx_vif = netdev_priv(dev);
	struct rwnx_hw* rwnx_hw = rwnx_vif->rwnx_hw;
	struct cfg80211_scan_request *request;
	int index = 0;
	struct wiphy *wiphy = priv_to_wiphy(rwnx_hw);
	unsigned long wext_scan_timeout;

	AICWFDBG(LOGTRACE, "%s Enter", __func__);

	if(wiphy == NULL){
		printk("aic_wiphy error \r\n");

	}
	
	rwnx_hw->wext_scan = 1;

	request = (struct cfg80211_scan_request *)vmalloc(sizeof(struct cfg80211_scan_request));

	request->n_channels = rwnx_hw->support_freqs_number;
	request->n_ssids = 0;
	request->no_cck = false;
	request->ie = NULL;
	request->ie_len = 0;

	for(index = 0;index < rwnx_hw->support_freqs_number; index++){
		request->channels[index] = ieee80211_get_channel(wiphy, 
			rwnx_hw->support_freqs[index]);
		if(request->channels[index] == NULL){
			AICWFDBG(LOGERROR, "%s ERROR!!! channels is NULL", __func__);
			continue;
		}
	}

	if ((ret = rwnx_send_scanu_req(rwnx_hw, rwnx_vif, request))){
        return ret;
	}

	rwnx_vif->rwnx_hw->scan_request = request;
	wext_scan_timeout = msecs_to_jiffies(5000);

	if (!wait_for_completion_killable_timeout(&rwnx_hw->wext_scan_com, wext_scan_timeout)) {
		AICWFDBG(LOGERROR, "%s WEXT scan timeout", __func__);
	}

	return 0;
}

static int aicwf_get_scan(struct net_device *dev, struct iw_request_info *a,
			   union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct rwnx_vif* rwnx_vif = netdev_priv(dev);
	struct rwnx_hw* rwnx_hw = rwnx_vif->rwnx_hw;
	struct scanu_result_wext *scan_re;
	struct scanu_result_wext *tmp;
	char *start = extra;
	char *stop = start + wrqu->data.length;
	
	AICWFDBG(LOGDEBUG, "%s Enter %p %p len:%d \r\n", __func__, start, stop, wrqu->data.length);
	

	//TODO: spinlock
	list_for_each_entry_safe(scan_re, tmp, &rwnx_hw->wext_scanre_list, scanu_re_list) {
		start = translate_scan(rwnx_hw, a, scan_re, start, stop);
		if ((stop - start) < 768) {
			return -E2BIG;
		}
		
	}

	list_for_each_entry_safe(scan_re, tmp, &rwnx_hw->wext_scanre_list, scanu_re_list) {
		list_del(&scan_re->scanu_re_list);
		vfree(scan_re->payload);
		vfree(scan_re->ind);
		vfree(scan_re);
		scan_re = NULL;
	}

	
	wrqu->data.length = start - extra;
	wrqu->data.flags = 0;

	return ret ;

}


static int aicwf_get_essid(struct net_device *dev,
			    struct iw_request_info *a,
			    union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct rwnx_vif* rwnx_vif = netdev_priv(dev);
	struct rwnx_hw* rwnx_hw = rwnx_vif->rwnx_hw;
	
	AICWFDBG(LOGTRACE, "%s Enter", __func__);

	wrqu->essid.length = strlen(rwnx_hw->wext_essid);
	memcpy(extra, rwnx_hw->wext_essid, strlen(rwnx_hw->wext_essid));
	wrqu->essid.flags = 1;


	return ret;

}


static int aicwf_get_nick(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
			   
	AICWFDBG(LOGTRACE, "%s Enter", __func__);

	if (extra) {
		wrqu->data.length = 8;
		wrqu->data.flags = 1;
		memcpy(extra, "AIC@8800", 8);
	}

	return 0;

}

static int aicwf_get_rate(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	u16 max_rate = 0;
	struct rwnx_vif* rwnx_vif = netdev_priv(dev);
	struct rwnx_hw* rwnx_hw = rwnx_vif->rwnx_hw;
	struct rwnx_sta *sta = NULL; 
	
	union rwnx_rate_ctrl_info *rate_info;
	struct mm_get_sta_info_cfm cfm;
	
	AICWFDBG(LOGTRACE, "%s Enter", __func__);
	
	if(rwnx_vif->sta.ap){
		sta = rwnx_vif->sta.ap;
		rwnx_send_get_sta_info_req(rwnx_hw, sta->sta_idx, &cfm);
			rate_info = (union rwnx_rate_ctrl_info *)&cfm.rate_info;
			
			switch (rate_info->formatModTx) {
			case FORMATMOD_NON_HT:
			case FORMATMOD_NON_HT_DUP_OFDM:
				//get bg mode datarate
				max_rate = tx_legrates_lut_rate[rate_info->mcsIndexTx]/10;
				break;
			case FORMATMOD_HT_MF:
			case FORMATMOD_HT_GF:
			case FORMATMOD_VHT:
				//get bg mode MCS index
				max_rate = VHT_MCS_DATA_RATE[rate_info->bwTx][1][rate_info->mcsIndexTx]/2;
				break;
			case FORMATMOD_HE_MU:
			case FORMATMOD_HE_SU:
			case FORMATMOD_HE_ER:
				//get bg mode MCS index
				max_rate = HE_MCS_DATA_RATE[rate_info->bwTx][rate_info->mcsIndexTx];
				break;
			}
	}

	wrqu->bitrate.fixed = 0;	/* no auto select */
	wrqu->bitrate.value = max_rate * 1000000;

	return 0;
}


static int aicwf_get_enc(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *keybuf)
{
	int ret = 0;
	struct iw_point *erq = &(wrqu->encoding);

	AICWFDBG(LOGTRACE, "%s Enter", __func__);

	erq->length = 0;
	erq->flags |= IW_ENCODE_ENABLED;


	return ret;

}


static iw_handler aic_handlers[] = {
	NULL,					/* SIOCSIWCOMMIT */
	aicwf_get_name,			/* SIOCGIWNAME */
	NULL,					/* SIOCSIWNWID */
	NULL,					/* SIOCGIWNWID */
	NULL,					/* SIOCSIWFREQ */
	aicwf_get_freq,			/* SIOCGIWFREQ */
	NULL,					/* SIOCSIWMODE */
	aicwf_get_mode,			/* SIOCGIWMODE */
	NULL,					/* SIOCSIWSENS */
	NULL,					/* SIOCGIWSENS */
	NULL,					/* SIOCSIWRANGE */
	aicwf_get_range,		/* SIOCGIWRANGE */
	NULL,					/* SIOCSIWPRIV */
	NULL,					/* SIOCGIWPRIV */
	NULL,					/* SIOCSIWSTATS */
	NULL,					/* SIOCGIWSTATS */
	NULL,					/* SIOCSIWSPY */
	NULL,					/* SIOCGIWSPY */
	NULL,					/* SIOCGIWTHRSPY */
	NULL,					/* SIOCWIWTHRSPY */
	NULL,					/* SIOCSIWAP */
	aicwf_get_wap,			/* SIOCGIWAP */
	NULL,					/* request MLME operation; uses struct iw_mlme */
	NULL,					/* SIOCGIWAPLIST -- depricated */
	aicwf_set_scan,			/* SIOCSIWSCAN */
	aicwf_get_scan,			/* SIOCGIWSCAN */
	NULL,					/* SIOCSIWESSID */
	aicwf_get_essid,		/* SIOCGIWESSID */
	NULL,					/* SIOCSIWNICKN */
	aicwf_get_nick,			/* SIOCGIWNICKN */
	NULL,					/* -- hole -- */
	NULL,					/* -- hole -- */
	NULL,					/* SIOCSIWRATE */
	aicwf_get_rate,			/* SIOCGIWRATE */
	NULL,					/* SIOCSIWRTS */
	NULL,					/* SIOCGIWRTS */
	NULL,					/* SIOCSIWFRAG */
	NULL,					/* SIOCGIWFRAG */
	NULL,					/* SIOCSIWTXPOW */
	NULL,					/* SIOCGIWTXPOW */
	NULL,					/* SIOCSIWRETRY */
	NULL,					/* SIOCGIWRETRY */
	NULL,					/* SIOCSIWENCODE */
	aicwf_get_enc,			/* SIOCGIWENCODE */
	NULL,					/* SIOCSIWPOWER */
	NULL,					/* SIOCGIWPOWER */
	NULL,					/*---hole---*/
	NULL,					/*---hole---*/
	NULL,					/* SIOCSIWGENIE */
	NULL,					/* SIOCGWGENIE */
	NULL,					/* SIOCSIWAUTH */
	NULL,					/* SIOCGIWAUTH */
	NULL,					/* SIOCSIWENCODEEXT */
	NULL,					/* SIOCGIWENCODEEXT */
	NULL,					/* SIOCSIWPMKSA */
	NULL,					/*---hole---*/
};

struct iw_handler_def aic_handlers_def = {
		.standard = aic_handlers,
		.num_standard = sizeof(aic_handlers) / sizeof(iw_handler),
#if WIRELESS_EXT >= 17
		.get_wireless_stats = aicwf_get_wireless_stats,
#endif
};

void aicwf_set_wireless_ext( struct net_device *ndev, struct rwnx_hw *rwnx_hw){

	AICWFDBG(LOGINFO, "%s Enter", __func__);
	
	init_completion(&rwnx_hw->wext_scan_com);
	INIT_LIST_HEAD(&rwnx_hw->wext_scanre_list);
	
	ndev->wireless_handlers = (struct iw_handler_def *)&aic_handlers_def;
}


