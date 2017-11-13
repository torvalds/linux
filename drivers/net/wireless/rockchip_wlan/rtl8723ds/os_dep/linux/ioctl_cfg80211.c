/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#define  _IOCTL_CFG80211_C_

#include <drv_types.h>
#include <hal_data.h>

#ifdef CONFIG_IOCTL_CFG80211

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
#define STATION_INFO_SIGNAL		BIT(NL80211_STA_INFO_SIGNAL)
#define STATION_INFO_TX_BITRATE		BIT(NL80211_STA_INFO_TX_BITRATE)
#define STATION_INFO_RX_PACKETS		BIT(NL80211_STA_INFO_RX_PACKETS)
#define STATION_INFO_TX_PACKETS		BIT(NL80211_STA_INFO_TX_PACKETS)
#define STATION_INFO_TX_FAILED		BIT(NL80211_STA_INFO_TX_FAILED)
#define STATION_INFO_ASSOC_REQ_IES	0
#endif /* Linux kernel >= 4.0.0 */

#include <rtw_wifi_regd.h>

#define RTW_MAX_MGMT_TX_CNT (8)
#define RTW_MAX_MGMT_TX_MS_GAS (500)

#define RTW_SCAN_IE_LEN_MAX      2304
#define RTW_MAX_REMAIN_ON_CHANNEL_DURATION 5000 /* ms */
#define RTW_MAX_NUM_PMKIDS 4

#define RTW_CH_MAX_2G_CHANNEL               14      /* Max channel in 2G band */

#ifdef CONFIG_WAPI_SUPPORT

#ifndef WLAN_CIPHER_SUITE_SMS4
#define WLAN_CIPHER_SUITE_SMS4          0x00147201
#endif

#ifndef WLAN_AKM_SUITE_WAPI_PSK
#define WLAN_AKM_SUITE_WAPI_PSK         0x000FAC04
#endif

#ifndef WLAN_AKM_SUITE_WAPI_CERT
#define WLAN_AKM_SUITE_WAPI_CERT        0x000FAC12
#endif

#ifndef NL80211_WAPI_VERSION_1
#define NL80211_WAPI_VERSION_1          (1 << 2)
#endif

#endif /* CONFIG_WAPI_SUPPORT */

#ifdef CONFIG_RTW_80211R
#define WLAN_AKM_SUITE_FT_8021X		0x000FAC03
#define WLAN_AKM_SUITE_FT_PSK			0x000FAC04
#endif

static const u32 rtw_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
#ifdef CONFIG_WAPI_SUPPORT
	WLAN_CIPHER_SUITE_SMS4,
#endif /* CONFIG_WAPI_SUPPORT */
#ifdef CONFIG_IEEE80211W
	WLAN_CIPHER_SUITE_AES_CMAC,
#endif /* CONFIG_IEEE80211W */
};

#define RATETAB_ENT(_rate, _rateid, _flags) \
	{								\
		.bitrate	= (_rate),				\
		.hw_value	= (_rateid),				\
		.flags		= (_flags),				\
	}

#define CHAN2G(_channel, _freq, _flags) {			\
		.band			= NL80211_BAND_2GHZ,		\
		.center_freq		= (_freq),			\
		.hw_value		= (_channel),			\
		.flags			= (_flags),			\
		.max_antenna_gain	= 0,				\
		.max_power		= 30,				\
	}

#define CHAN5G(_channel, _flags) {				\
		.band			= NL80211_BAND_5GHZ,		\
		.center_freq		= 5000 + (5 * (_channel)),	\
		.hw_value		= (_channel),			\
		.flags			= (_flags),			\
		.max_antenna_gain	= 0,				\
		.max_power		= 30,				\
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
/* if wowlan is not supported, kernel generate a disconnect at each suspend
 * cf: /net/wireless/sysfs.c, so register a stub wowlan.
 * Moreover wowlan has to be enabled via a the nl80211_set_wowlan callback.
 * (from user space, e.g. iw phy0 wowlan enable)
 */
static const struct wiphy_wowlan_support wowlan_stub = {
	.flags = WIPHY_WOWLAN_ANY,
	.n_patterns = 0,
	.pattern_max_len = 0,
	.pattern_min_len = 0,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
	.max_pkt_offset = 0,
#endif
};
#endif

static struct ieee80211_rate rtw_rates[] = {
	RATETAB_ENT(10,  0x1,   0),
	RATETAB_ENT(20,  0x2,   0),
	RATETAB_ENT(55,  0x4,   0),
	RATETAB_ENT(110, 0x8,   0),
	RATETAB_ENT(60,  0x10,  0),
	RATETAB_ENT(90,  0x20,  0),
	RATETAB_ENT(120, 0x40,  0),
	RATETAB_ENT(180, 0x80,  0),
	RATETAB_ENT(240, 0x100, 0),
	RATETAB_ENT(360, 0x200, 0),
	RATETAB_ENT(480, 0x400, 0),
	RATETAB_ENT(540, 0x800, 0),
};

#define rtw_a_rates		(rtw_rates + 4)
#define RTW_A_RATES_NUM	8
#define rtw_g_rates		(rtw_rates + 0)
#define RTW_G_RATES_NUM	12

/* from center_ch_2g */
static struct ieee80211_channel rtw_2ghz_channels[MAX_CHANNEL_NUM_2G] = {
	CHAN2G(1, 2412, 0),
	CHAN2G(2, 2417, 0),
	CHAN2G(3, 2422, 0),
	CHAN2G(4, 2427, 0),
	CHAN2G(5, 2432, 0),
	CHAN2G(6, 2437, 0),
	CHAN2G(7, 2442, 0),
	CHAN2G(8, 2447, 0),
	CHAN2G(9, 2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0),
};

/* from center_ch_5g_20m */
static struct ieee80211_channel rtw_5ghz_a_channels[MAX_CHANNEL_NUM_5G] = {
	CHAN5G(36, 0),	CHAN5G(40, 0),	CHAN5G(44, 0),	CHAN5G(48, 0),

	CHAN5G(52, 0),	CHAN5G(56, 0),	CHAN5G(60, 0),	CHAN5G(64, 0),

	CHAN5G(100, 0),	CHAN5G(104, 0),	CHAN5G(108, 0),	CHAN5G(112, 0),
	CHAN5G(116, 0),	CHAN5G(120, 0),	CHAN5G(124, 0),	CHAN5G(128, 0),
	CHAN5G(132, 0),	CHAN5G(136, 0),	CHAN5G(140, 0),	CHAN5G(144, 0),

	CHAN5G(149, 0),	CHAN5G(153, 0),	CHAN5G(157, 0),	CHAN5G(161, 0),
	CHAN5G(165, 0),	CHAN5G(169, 0),	CHAN5G(173, 0),	CHAN5G(177, 0),
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
static const char *nl80211_channel_type_str(enum nl80211_channel_type ctype)
{
	switch (ctype) {
	case NL80211_CHAN_NO_HT:
		return "NO_HT";
	case NL80211_CHAN_HT20:
		return "HT20";
	case NL80211_CHAN_HT40MINUS:
		return "HT40-";
	case NL80211_CHAN_HT40PLUS:
		return "HT40+";
	default:
		return "INVALID";
	};
}

static enum nl80211_channel_type rtw_chbw_to_nl80211_channel_type(u8 ch, u8 bw, u8 offset, u8 ht)
{
	rtw_warn_on(!ht && (bw >= CHANNEL_WIDTH_40 || offset != HAL_PRIME_CHNL_OFFSET_DONT_CARE));

	if (!ht)
		return NL80211_CHAN_NO_HT;
	if (bw >= CHANNEL_WIDTH_40) {
		if (offset == HAL_PRIME_CHNL_OFFSET_UPPER)
			return NL80211_CHAN_HT40MINUS;
		else if (offset == HAL_PRIME_CHNL_OFFSET_LOWER)
			return NL80211_CHAN_HT40PLUS;
		else
			rtw_warn_on(1);
	}
	return NL80211_CHAN_HT20;
}

static void rtw_get_chbw_from_nl80211_channel_type(struct ieee80211_channel *chan, enum nl80211_channel_type ctype, u8 *ht, u8 *ch, u8 *bw, u8 *offset)
{
	int pri_freq;

	pri_freq = rtw_ch2freq(chan->hw_value);
	if (!pri_freq) {
		RTW_INFO("invalid channel:%d\n", chan->hw_value);
		rtw_warn_on(1);
		*ch = 0;
		return;
	}
	*ch = chan->hw_value;

	switch (ctype) {
	case NL80211_CHAN_NO_HT:
		*ht = 0;
		*bw = CHANNEL_WIDTH_20;
		*offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		break;
	case NL80211_CHAN_HT20:
		*ht = 1;
		*bw = CHANNEL_WIDTH_20;
		*offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		break;
	case NL80211_CHAN_HT40MINUS:
		*ht = 1;
		*bw = CHANNEL_WIDTH_40;
		*offset = HAL_PRIME_CHNL_OFFSET_UPPER;
		break;
	case NL80211_CHAN_HT40PLUS:
		*ht = 1;
		*bw = CHANNEL_WIDTH_40;
		*offset = HAL_PRIME_CHNL_OFFSET_LOWER;
		break;
	default:
		*ht = 0;
		*bw = CHANNEL_WIDTH_20;
		*offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		RTW_INFO("unsupported ctype:%s\n", nl80211_channel_type_str(ctype));
		rtw_warn_on(1);
	};
}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29)) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
static const char *nl80211_chan_width_str(enum nl80211_chan_width cwidth)
{
	switch (cwidth) {
	case NL80211_CHAN_WIDTH_20_NOHT:
		return "20_NOHT";
	case NL80211_CHAN_WIDTH_20:
		return "20";
	case NL80211_CHAN_WIDTH_40:
		return "40";
	case NL80211_CHAN_WIDTH_80:
		return "80";
	case NL80211_CHAN_WIDTH_80P80:
		return "80+80";
	case NL80211_CHAN_WIDTH_160:
		return "160";
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	case NL80211_CHAN_WIDTH_5:
		return "5";
	case NL80211_CHAN_WIDTH_10:
		return "10";
#endif
	default:
		return "INVALID";
	};
}

static u8 rtw_chbw_to_cfg80211_chan_def(struct wiphy *wiphy, struct cfg80211_chan_def *chdef, u8 ch, u8 bw, u8 offset, u8 ht)
{
	int freq, cfreq;
	struct ieee80211_channel *chan;
	u8 ret = _FAIL;

	freq = rtw_ch2freq(ch);
	if (!freq)
		goto exit;

	cfreq = rtw_get_center_ch(ch, bw, offset);
	if (!cfreq)
		goto exit;
	cfreq = rtw_ch2freq(cfreq);
	if (!cfreq)
		goto exit;

	chan = ieee80211_get_channel(wiphy, freq);
	if (!chan)
		goto exit;

	if (bw == CHANNEL_WIDTH_20) 
		chdef->width = ht ? NL80211_CHAN_WIDTH_20 : NL80211_CHAN_WIDTH_20_NOHT;
	else if (bw == CHANNEL_WIDTH_40)
		chdef->width = NL80211_CHAN_WIDTH_40;
	else if (bw == CHANNEL_WIDTH_80)
		chdef->width = NL80211_CHAN_WIDTH_80;
	else if (bw == CHANNEL_WIDTH_160)
		chdef->width = NL80211_CHAN_WIDTH_160;
	else {
		rtw_warn_on(1);
		goto exit;
	}

	chdef->chan = chan;
	chdef->center_freq1 = cfreq;
	chdef->center_freq2 = 0;

	ret = _SUCCESS;

exit:
	return ret;
}

static void rtw_get_chbw_from_cfg80211_chan_def(struct cfg80211_chan_def *chdef, u8 *ht, u8 *ch, u8 *bw, u8 *offset)
{
	int pri_freq;
	struct ieee80211_channel *chan = chdef->chan;

	pri_freq = rtw_ch2freq(chan->hw_value);
	if (!pri_freq) {
		RTW_INFO("invalid channel:%d\n", chan->hw_value);
		rtw_warn_on(1);
		*ch = 0;
		return;
	}		

	switch (chdef->width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
		*ht = 0;
		*bw = CHANNEL_WIDTH_20;
		*offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		*ch = chan->hw_value;
		break;
	case NL80211_CHAN_WIDTH_20:
		*ht = 1;
		*bw = CHANNEL_WIDTH_20;
		*offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		*ch = chan->hw_value;
		break;
	case NL80211_CHAN_WIDTH_40:
		*ht = 1;
		*bw = CHANNEL_WIDTH_40;
		*offset = pri_freq > chdef->center_freq1 ? HAL_PRIME_CHNL_OFFSET_UPPER : HAL_PRIME_CHNL_OFFSET_LOWER;
		if (rtw_get_offset_by_chbw(chan->hw_value, *bw, offset))
			*ch = chan->hw_value;
		break;
	case NL80211_CHAN_WIDTH_80:
		*ht = 1;
		*bw = CHANNEL_WIDTH_80;
		if (rtw_get_offset_by_chbw(chan->hw_value, *bw, offset))
			*ch = chan->hw_value;
		break;
	case NL80211_CHAN_WIDTH_160:
		*ht = 1;
		*bw = CHANNEL_WIDTH_160;
		if (rtw_get_offset_by_chbw(chan->hw_value, *bw, offset))
			*ch = chan->hw_value;
		break;
	case NL80211_CHAN_WIDTH_80P80:
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	case NL80211_CHAN_WIDTH_5:
	case NL80211_CHAN_WIDTH_10:
	#endif
	default:
		*ht = 0;
		*bw = CHANNEL_WIDTH_20;
		*offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		RTW_INFO("unsupported cwidth:%s\n", nl80211_chan_width_str(chdef->width));
		rtw_warn_on(1);
	};
}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
u8 rtw_cfg80211_ch_switch_notify(_adapter *adapter, u8 ch, u8 bw, u8 offset, u8 ht)
{
	struct wiphy *wiphy = adapter_to_wiphy(adapter);
	u8 ret = _SUCCESS;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	struct cfg80211_chan_def chdef;

	ret = rtw_chbw_to_cfg80211_chan_def(wiphy, &chdef, ch, bw, offset, ht);
	if (ret != _SUCCESS)
		goto exit;

	cfg80211_ch_switch_notify(adapter->pnetdev, &chdef);

#else
	int freq = rtw_ch2freq(ch);
	enum nl80211_channel_type ctype;

	if (!freq) {
		ret = _FAIL;
		goto exit;
	}

	ctype = rtw_chbw_to_nl80211_channel_type(ch, bw, offset, ht);
	cfg80211_ch_switch_notify(adapter->pnetdev, freq, ctype);
#endif

exit:
	return ret;
}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)) */

void rtw_2g_channels_init(struct ieee80211_channel *channels)
{
	_rtw_memcpy((void *)channels, (void *)rtw_2ghz_channels, sizeof(rtw_2ghz_channels));
}

void rtw_5g_channels_init(struct ieee80211_channel *channels)
{
	_rtw_memcpy((void *)channels, (void *)rtw_5ghz_a_channels, sizeof(rtw_5ghz_a_channels));
}

void rtw_2g_rates_init(struct ieee80211_rate *rates)
{
	_rtw_memcpy(rates, rtw_g_rates,
		sizeof(struct ieee80211_rate) * RTW_G_RATES_NUM
	);
}

void rtw_5g_rates_init(struct ieee80211_rate *rates)
{
	_rtw_memcpy(rates, rtw_a_rates,
		sizeof(struct ieee80211_rate) * RTW_A_RATES_NUM
	);
}

struct ieee80211_supported_band *rtw_spt_band_alloc(BAND_TYPE band)
{
	struct ieee80211_supported_band *spt_band = NULL;
	int n_channels, n_bitrates;

	if (band == BAND_ON_2_4G) {
		n_channels = MAX_CHANNEL_NUM_2G;
		n_bitrates = RTW_G_RATES_NUM;
	} else if (band == BAND_ON_5G) {
		n_channels = MAX_CHANNEL_NUM_5G;
		n_bitrates = RTW_A_RATES_NUM;
	} else
		goto exit;

	spt_band = (struct ieee80211_supported_band *)rtw_zmalloc(
		sizeof(struct ieee80211_supported_band)
		+ sizeof(struct ieee80211_channel) * n_channels
		+ sizeof(struct ieee80211_rate) * n_bitrates
	);
	if (!spt_band)
		goto exit;

	spt_band->channels = (struct ieee80211_channel *)(((u8 *)spt_band) + sizeof(struct ieee80211_supported_band));
	spt_band->bitrates = (struct ieee80211_rate *)(((u8 *)spt_band->channels) + sizeof(struct ieee80211_channel) * n_channels);
	spt_band->band = rtw_band_to_nl80211_band(band);
	spt_band->n_channels = n_channels;
	spt_band->n_bitrates = n_bitrates;

	if (band == BAND_ON_2_4G) {
		rtw_2g_channels_init(spt_band->channels);
		rtw_2g_rates_init(spt_band->bitrates);
	} else if (band == BAND_ON_5G) {
		rtw_5g_channels_init(spt_band->channels);
		rtw_5g_rates_init(spt_band->bitrates);
	}

	/* spt_band.ht_cap */

exit:

	return spt_band;
}

void rtw_spt_band_free(struct ieee80211_supported_band *spt_band)
{
	u32 size = 0;

	if (!spt_band)
		return;

	if (spt_band->band == NL80211_BAND_2GHZ) {
		size = sizeof(struct ieee80211_supported_band)
			+ sizeof(struct ieee80211_channel) * MAX_CHANNEL_NUM_2G
			+ sizeof(struct ieee80211_rate) * RTW_G_RATES_NUM;
	} else if (spt_band->band == NL80211_BAND_5GHZ) {
		size = sizeof(struct ieee80211_supported_band)
			+ sizeof(struct ieee80211_channel) * MAX_CHANNEL_NUM_5G
			+ sizeof(struct ieee80211_rate) * RTW_A_RATES_NUM;
	} else {

	}
	rtw_mfree((u8 *)spt_band, size);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
static const struct ieee80211_txrx_stypes
	rtw_cfg80211_default_mgmt_stypes[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_ADHOC] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4)
	},
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
	[NL80211_IFTYPE_AP_VLAN] = {
		/* copy AP */
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
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_P2P_GO] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		BIT(IEEE80211_STYPE_AUTH >> 4) |
		BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		BIT(IEEE80211_STYPE_ACTION >> 4)
	},
#if defined(RTW_DEDICATED_P2P_DEVICE)
	[NL80211_IFTYPE_P2P_DEVICE] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
#endif
};
#endif

static u64 rtw_get_systime_us(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39))
	struct timespec ts;
	get_monotonic_boottime(&ts);
	return ((u64)ts.tv_sec * 1000000) + ts.tv_nsec / 1000;
#else
	struct timeval tv;
	do_gettimeofday(&tv);
	return ((u64)tv.tv_sec * 1000000) + tv.tv_usec;
#endif
}

/* Try to remove non target BSS's SR to reduce PBC overlap rate */
static int rtw_cfg80211_clear_wps_sr_of_non_target_bss(_adapter *padapter, struct wlan_network *pnetwork, struct cfg80211_ssid *req_ssid)
{
	struct rtw_wdev_priv *wdev_data = adapter_wdev_data(padapter);
	int ret = 0;
	u8 *psr = NULL, sr = 0;
	NDIS_802_11_SSID *pssid = &pnetwork->network.Ssid;
	u32 wpsielen = 0;
	u8 *wpsie = NULL;

	if (pssid->SsidLength == req_ssid->ssid_len
		&& _rtw_memcmp(pssid->Ssid, req_ssid->ssid, req_ssid->ssid_len) == _TRUE)
		goto exit;

	wpsie = rtw_get_wps_ie(pnetwork->network.IEs + _FIXED_IE_LENGTH_
		, pnetwork->network.IELength - _FIXED_IE_LENGTH_, NULL, &wpsielen);
	if (wpsie && wpsielen > 0)
		psr = rtw_get_wps_attr_content(wpsie, wpsielen, WPS_ATTR_SELECTED_REGISTRAR, &sr, NULL);

	if (psr && sr) {
		if (0)
			RTW_INFO("clear sr of non target bss:%s("MAC_FMT")\n"
				, pssid->Ssid, MAC_ARG(pnetwork->network.MacAddress));
		*psr = 0; /* clear sr */
		ret = 1;
	}

exit:
	return ret;
}

#define MAX_BSSINFO_LEN 1000
struct cfg80211_bss *rtw_cfg80211_inform_bss(_adapter *padapter, struct wlan_network *pnetwork)
{
	struct ieee80211_channel *notify_channel;
	struct cfg80211_bss *bss = NULL;
	/* struct ieee80211_supported_band *band;       */
	u16 channel;
	u32 freq;
	u64 notify_timestamp;
	u16 notify_capability;
	u16 notify_interval;
	u8 *notify_ie;
	size_t notify_ielen;
	s32 notify_signal;
	/* u8 buf[MAX_BSSINFO_LEN]; */

	u8 *pbuf;
	size_t buf_size = MAX_BSSINFO_LEN;
	size_t len, bssinf_len = 0;
	struct rtw_ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	struct wireless_dev *wdev = padapter->rtw_wdev;
	struct wiphy *wiphy = wdev->wiphy;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	pbuf = rtw_zmalloc(buf_size);
	if (pbuf == NULL) {
		RTW_INFO("%s pbuf allocate failed  !!\n", __FUNCTION__);
		return bss;
	}

	/* RTW_INFO("%s\n", __func__); */

	bssinf_len = pnetwork->network.IELength + sizeof(struct rtw_ieee80211_hdr_3addr);
	if (bssinf_len > buf_size) {
		RTW_INFO("%s IE Length too long > %zu byte\n", __FUNCTION__, buf_size);
		goto exit;
	}

#ifndef CONFIG_WAPI_SUPPORT
	{
		u16 wapi_len = 0;

		if (rtw_get_wapi_ie(pnetwork->network.IEs, pnetwork->network.IELength, NULL, &wapi_len) > 0) {
			if (wapi_len > 0) {
				RTW_INFO("%s, no support wapi!\n", __FUNCTION__);
				goto exit;
			}
		}
	}
#endif /* !CONFIG_WAPI_SUPPORT */

	channel = pnetwork->network.Configuration.DSConfig;
	freq = rtw_ch2freq(channel);
	notify_channel = ieee80211_get_channel(wiphy, freq);

	if (0)
		notify_timestamp = le64_to_cpu(*(u64 *)rtw_get_timestampe_from_ie(pnetwork->network.IEs));
	else
		notify_timestamp = rtw_get_systime_us();

	notify_interval = le16_to_cpu(*(u16 *)rtw_get_beacon_interval_from_ie(pnetwork->network.IEs));
	notify_capability = le16_to_cpu(*(u16 *)rtw_get_capability_from_ie(pnetwork->network.IEs));

	notify_ie = pnetwork->network.IEs + _FIXED_IE_LENGTH_;
	notify_ielen = pnetwork->network.IELength - _FIXED_IE_LENGTH_;

	/* We've set wiphy's signal_type as CFG80211_SIGNAL_TYPE_MBM: signal strength in mBm (100*dBm) */
	if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE &&
		is_same_network(&pmlmepriv->cur_network.network, &pnetwork->network, 0)) {
		notify_signal = 100 * translate_percentage_to_dbm(padapter->recvpriv.signal_strength); /* dbm */
	} else {
		notify_signal = 100 * translate_percentage_to_dbm(pnetwork->network.PhyInfo.SignalStrength); /* dbm */
	}

#if 0
	RTW_INFO("bssid: "MAC_FMT"\n", MAC_ARG(pnetwork->network.MacAddress));
	RTW_INFO("Channel: %d(%d)\n", channel, freq);
	RTW_INFO("Capability: %X\n", notify_capability);
	RTW_INFO("Beacon interval: %d\n", notify_interval);
	RTW_INFO("Signal: %d\n", notify_signal);
	RTW_INFO("notify_timestamp: %llu\n", notify_timestamp);
#endif

	/* pbuf = buf; */

	pwlanhdr = (struct rtw_ieee80211_hdr *)pbuf;
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	/* pmlmeext->mgnt_seq++; */

	if (pnetwork->network.Reserved[0] == BSS_TYPE_BCN) { /* WIFI_BEACON */
		_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
		set_frame_sub_type(pbuf, WIFI_BEACON);
	} else {
		_rtw_memcpy(pwlanhdr->addr1, adapter_mac_addr(padapter), ETH_ALEN);
		set_frame_sub_type(pbuf, WIFI_PROBERSP);
	}

	_rtw_memcpy(pwlanhdr->addr2, pnetwork->network.MacAddress, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, pnetwork->network.MacAddress, ETH_ALEN);


	/* pbuf += sizeof(struct rtw_ieee80211_hdr_3addr); */
	len = sizeof(struct rtw_ieee80211_hdr_3addr);
	_rtw_memcpy((pbuf + len), pnetwork->network.IEs, pnetwork->network.IELength);
	*((u64 *)(pbuf + len)) = cpu_to_le64(notify_timestamp);

	len += pnetwork->network.IELength;

	#if defined(CONFIG_P2P) && 0
	if(rtw_get_p2p_ie(pnetwork->network.IEs+12, pnetwork->network.IELength-12, NULL, NULL))
		RTW_INFO("%s, got p2p_ie\n", __func__);
	#endif

#if 1
	bss = cfg80211_inform_bss_frame(wiphy, notify_channel, (struct ieee80211_mgmt *)pbuf,
					len, notify_signal, GFP_ATOMIC);
#else

	bss = cfg80211_inform_bss(wiphy, notify_channel, (const u8 *)pnetwork->network.MacAddress,
		notify_timestamp, notify_capability, notify_interval, notify_ie,
		notify_ielen, notify_signal, GFP_ATOMIC/*GFP_KERNEL*/);
#endif

	if (unlikely(!bss)) {
		RTW_INFO(FUNC_ADPT_FMT" bss NULL\n", FUNC_ADPT_ARG(padapter));
		goto exit;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 38))
#ifndef COMPAT_KERNEL_RELEASE
	/* patch for cfg80211, update beacon ies to information_elements */
	if (pnetwork->network.Reserved[0] == BSS_TYPE_BCN) { /* WIFI_BEACON */

		if (bss->len_information_elements != bss->len_beacon_ies) {
			bss->information_elements = bss->beacon_ies;
			bss->len_information_elements =  bss->len_beacon_ies;
		}
	}
#endif /* COMPAT_KERNEL_RELEASE */
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 38) */

#if 0
	{
		if (bss->information_elements == bss->proberesp_ies) {
			if (bss->len_information_elements !=  bss->len_proberesp_ies)
				RTW_INFO("error!, len_information_elements != bss->len_proberesp_ies\n");
		} else if (bss->len_information_elements <  bss->len_beacon_ies) {
			bss->information_elements = bss->beacon_ies;
			bss->len_information_elements =  bss->len_beacon_ies;
		}
	}
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
	cfg80211_put_bss(wiphy, bss);
#else
	cfg80211_put_bss(bss);
#endif

exit:
	if (pbuf)
		rtw_mfree(pbuf, buf_size);
	return bss;

}

/*
	Check the given bss is valid by kernel API cfg80211_get_bss()
	@padapter : the given adapter

	return _TRUE if bss is valid,  _FALSE for not found.
*/
int rtw_cfg80211_check_bss(_adapter *padapter)
{
	WLAN_BSSID_EX  *pnetwork = &(padapter->mlmeextpriv.mlmext_info.network);
	struct cfg80211_bss *bss = NULL;
	struct ieee80211_channel *notify_channel = NULL;
	u32 freq;

	if (!(pnetwork) || !(padapter->rtw_wdev))
		return _FALSE;

	freq = rtw_ch2freq(pnetwork->Configuration.DSConfig);
	notify_channel = ieee80211_get_channel(padapter->rtw_wdev->wiphy, freq);
	bss = cfg80211_get_bss(padapter->rtw_wdev->wiphy, notify_channel,
			pnetwork->MacAddress, pnetwork->Ssid.Ssid,
			pnetwork->Ssid.SsidLength,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
			pnetwork->InfrastructureMode == Ndis802_11Infrastructure?IEEE80211_BSS_TYPE_ESS:IEEE80211_BSS_TYPE_IBSS,
			IEEE80211_PRIVACY(pnetwork->Privacy));
#else
			pnetwork->InfrastructureMode == Ndis802_11Infrastructure?WLAN_CAPABILITY_ESS:WLAN_CAPABILITY_IBSS, pnetwork->InfrastructureMode == Ndis802_11Infrastructure?WLAN_CAPABILITY_ESS:WLAN_CAPABILITY_IBSS);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
	cfg80211_put_bss(padapter->rtw_wdev->wiphy, bss);
#else
	cfg80211_put_bss(bss);
#endif

	return bss != NULL;
}

void rtw_cfg80211_ibss_indicate_connect(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network  *cur_network = &(pmlmepriv->cur_network);
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct cfg80211_bss *bss = NULL;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
	struct wiphy *wiphy = pwdev->wiphy;
	int freq = 2412;
	struct ieee80211_channel *notify_channel;
#endif

	RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
	freq = rtw_ch2freq(cur_network->network.Configuration.DSConfig);

	if (0)
		RTW_INFO("chan: %d, freq: %d\n", cur_network->network.Configuration.DSConfig, freq);
#endif

	if (pwdev->iftype != NL80211_IFTYPE_ADHOC)
		return;

	if (!rtw_cfg80211_check_bss(padapter)) {
		WLAN_BSSID_EX  *pnetwork = &(padapter->mlmeextpriv.mlmext_info.network);
		struct wlan_network *scanned = pmlmepriv->cur_network_scanned;

		if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) {

			_rtw_memcpy(&cur_network->network, pnetwork, sizeof(WLAN_BSSID_EX));
			if (cur_network) {
				if (!rtw_cfg80211_inform_bss(padapter, cur_network))
					RTW_INFO(FUNC_ADPT_FMT" inform fail !!\n", FUNC_ADPT_ARG(padapter));
				else
					RTW_INFO(FUNC_ADPT_FMT" inform success !!\n", FUNC_ADPT_ARG(padapter));
			} else {
				RTW_INFO("cur_network is not exist!!!\n");
				return ;
			}
		} else {
			if (scanned == NULL)
				rtw_warn_on(1);

			if (_rtw_memcmp(&(scanned->network.Ssid), &(pnetwork->Ssid), sizeof(NDIS_802_11_SSID)) == _TRUE
				&& _rtw_memcmp(scanned->network.MacAddress, pnetwork->MacAddress, sizeof(NDIS_802_11_MAC_ADDRESS)) == _TRUE
			) {
				if (!rtw_cfg80211_inform_bss(padapter, scanned))
					RTW_INFO(FUNC_ADPT_FMT" inform fail !!\n", FUNC_ADPT_ARG(padapter));
				else {
					/* RTW_INFO(FUNC_ADPT_FMT" inform success !!\n", FUNC_ADPT_ARG(padapter)); */
				}
			} else {
				RTW_INFO("scanned & pnetwork compare fail\n");
				rtw_warn_on(1);
			}
		}

		if (!rtw_cfg80211_check_bss(padapter))
			RTW_PRINT(FUNC_ADPT_FMT" BSS not found !!\n", FUNC_ADPT_ARG(padapter));
	}
	/* notify cfg80211 that device joined an IBSS */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
	notify_channel = ieee80211_get_channel(wiphy, freq);
	cfg80211_ibss_joined(padapter->pnetdev, cur_network->network.MacAddress, notify_channel, GFP_ATOMIC);
#else
	cfg80211_ibss_joined(padapter->pnetdev, cur_network->network.MacAddress, GFP_ATOMIC);
#endif
}

void rtw_cfg80211_indicate_connect(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network  *cur_network = &(pmlmepriv->cur_network);
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
	_irqL irqL;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
#endif
	struct cfg80211_bss *bss = NULL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
	struct cfg80211_roam_info roam_info ={};
#endif

	RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));
	if (pwdev->iftype != NL80211_IFTYPE_STATION
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
		&& pwdev->iftype != NL80211_IFTYPE_P2P_CLIENT
		#endif
	)
		return;

	if (!MLME_IS_STA(padapter))
		return;

#ifdef CONFIG_P2P
	if (pwdinfo->driver_interface == DRIVER_CFG80211) {
		#if !RTW_P2P_GROUP_INTERFACE
		if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
			rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
			rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
			rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
			RTW_INFO("%s, role=%d, p2p_state=%d, pre_p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo), rtw_p2p_pre_state(pwdinfo));
		}
		#endif
	}
#endif /* CONFIG_P2P */

	if (check_fwstate(pmlmepriv, WIFI_MONITOR_STATE) != _TRUE) {
		WLAN_BSSID_EX  *pnetwork = &(padapter->mlmeextpriv.mlmext_info.network);
		struct wlan_network *scanned = pmlmepriv->cur_network_scanned;

		/* RTW_INFO(FUNC_ADPT_FMT" BSS not found\n", FUNC_ADPT_ARG(padapter)); */

		if (scanned == NULL) {
			rtw_warn_on(1);
			goto check_bss;
		}

		if (_rtw_memcmp(scanned->network.MacAddress, pnetwork->MacAddress, sizeof(NDIS_802_11_MAC_ADDRESS)) == _TRUE
			&& _rtw_memcmp(&(scanned->network.Ssid), &(pnetwork->Ssid), sizeof(NDIS_802_11_SSID)) == _TRUE
		) {
			if (!rtw_cfg80211_inform_bss(padapter, scanned))
				RTW_INFO(FUNC_ADPT_FMT" inform fail !!\n", FUNC_ADPT_ARG(padapter));
			else {
				/* RTW_INFO(FUNC_ADPT_FMT" inform success !!\n", FUNC_ADPT_ARG(padapter)); */
			}
		} else {
			RTW_INFO("scanned: %s("MAC_FMT"), cur: %s("MAC_FMT")\n",
				scanned->network.Ssid.Ssid, MAC_ARG(scanned->network.MacAddress),
				pnetwork->Ssid.Ssid, MAC_ARG(pnetwork->MacAddress)
			);
			rtw_warn_on(1);
		}
	}

check_bss:
	if (!rtw_cfg80211_check_bss(padapter))
		RTW_PRINT(FUNC_ADPT_FMT" BSS not found !!\n", FUNC_ADPT_ARG(padapter));

	_enter_critical_bh(&pwdev_priv->connect_req_lock, &irqL);

	if (rtw_to_roam(padapter) > 0) {
		#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39) || defined(COMPAT_KERNEL_RELEASE)
		struct wiphy *wiphy = pwdev->wiphy;
		struct ieee80211_channel *notify_channel;
		u32 freq;
		u16 channel = cur_network->network.Configuration.DSConfig;

		freq = rtw_ch2freq(channel);
		notify_channel = ieee80211_get_channel(wiphy, freq);
		#endif

		#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
		roam_info.bssid = cur_network->network.MacAddress;
		roam_info.req_ie = pmlmepriv->assoc_req + sizeof(struct rtw_ieee80211_hdr_3addr) + 2;
		roam_info.req_ie_len = pmlmepriv->assoc_req_len - sizeof(struct rtw_ieee80211_hdr_3addr) - 2;
		roam_info.resp_ie = pmlmepriv->assoc_rsp + sizeof(struct rtw_ieee80211_hdr_3addr) + 6;
		roam_info.resp_ie_len = pmlmepriv->assoc_rsp_len - sizeof(struct rtw_ieee80211_hdr_3addr) - 6;

		cfg80211_roamed(padapter->pnetdev, &roam_info, GFP_ATOMIC);
		#else
		cfg80211_roamed(padapter->pnetdev
			#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39) || defined(COMPAT_KERNEL_RELEASE)
			, notify_channel
			#endif
			, cur_network->network.MacAddress
			, pmlmepriv->assoc_req + sizeof(struct rtw_ieee80211_hdr_3addr) + 2
			, pmlmepriv->assoc_req_len - sizeof(struct rtw_ieee80211_hdr_3addr) - 2
			, pmlmepriv->assoc_rsp + sizeof(struct rtw_ieee80211_hdr_3addr) + 6
			, pmlmepriv->assoc_rsp_len - sizeof(struct rtw_ieee80211_hdr_3addr) - 6
			, GFP_ATOMIC);
		#endif /*LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)*/

		RTW_INFO(FUNC_ADPT_FMT" call cfg80211_roamed\n", FUNC_ADPT_ARG(padapter));

#ifdef CONFIG_RTW_80211R
		if ((rtw_to_roam(padapter) > 0) && rtw_chk_ft_flags(padapter, RTW_FT_SUPPORTED))
			rtw_set_ft_status(padapter, RTW_FT_ASSOCIATED_STA);
#endif
	} else {
		#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0) || defined(COMPAT_KERNEL_RELEASE)
		RTW_INFO("pwdev->sme_state(b)=%d\n", pwdev->sme_state);
		#endif

		if (check_fwstate(pmlmepriv, WIFI_MONITOR_STATE) != _TRUE)
			rtw_cfg80211_connect_result(pwdev, cur_network->network.MacAddress
				, pmlmepriv->assoc_req + sizeof(struct rtw_ieee80211_hdr_3addr) + 2
				, pmlmepriv->assoc_req_len - sizeof(struct rtw_ieee80211_hdr_3addr) - 2
				, pmlmepriv->assoc_rsp + sizeof(struct rtw_ieee80211_hdr_3addr) + 6
				, pmlmepriv->assoc_rsp_len - sizeof(struct rtw_ieee80211_hdr_3addr) - 6
				, WLAN_STATUS_SUCCESS, GFP_ATOMIC);
		#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0) || defined(COMPAT_KERNEL_RELEASE)
		RTW_INFO("pwdev->sme_state(a)=%d\n", pwdev->sme_state);
		#endif
	}

	rtw_wdev_free_connect_req(pwdev_priv);

	_exit_critical_bh(&pwdev_priv->connect_req_lock, &irqL);
}

void rtw_cfg80211_indicate_disconnect(_adapter *padapter, u16 reason, u8 locally_generated)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
	_irqL irqL;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
#endif

	RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

	/*always replace privated definitions with wifi reserved value 0*/
	if ((reason == WLAN_REASON_ACTIVE_ROAM) || (reason == WLAN_REASON_JOIN_WRONG_CHANNEL) || (reason == WLAN_REASON_EXPIRATION_CHK))
		reason = 0;

	if (pwdev->iftype != NL80211_IFTYPE_STATION
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
		&& pwdev->iftype != NL80211_IFTYPE_P2P_CLIENT
		#endif
	)
		return;

	if (!MLME_IS_STA(padapter))
		return;

#ifdef CONFIG_P2P
	if (pwdinfo->driver_interface == DRIVER_CFG80211) {
		if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
			rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));

			#if RTW_P2P_GROUP_INTERFACE
			#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
			if (pwdev->iftype != NL80211_IFTYPE_P2P_CLIENT)
			#endif
			#endif
				rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);

			RTW_INFO("%s, role=%d, p2p_state=%d, pre_p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo), rtw_p2p_pre_state(pwdinfo));
		}
	}
#endif /* CONFIG_P2P */

	_enter_critical_bh(&pwdev_priv->connect_req_lock, &irqL);

	if (padapter->ndev_unregistering || !rtw_wdev_not_indic_disco(pwdev_priv)) {
		#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0) || defined(COMPAT_KERNEL_RELEASE)
		RTW_INFO("pwdev->sme_state(b)=%d\n", pwdev->sme_state);

		if (pwdev->sme_state == CFG80211_SME_CONNECTING) {
			RTW_INFO(FUNC_ADPT_FMT" call cfg80211_connect_result\n", FUNC_ADPT_ARG(padapter));
			rtw_cfg80211_connect_result(pwdev, NULL, NULL, 0, NULL, 0,
				WLAN_STATUS_UNSPECIFIED_FAILURE, GFP_ATOMIC);
		} else if (pwdev->sme_state == CFG80211_SME_CONNECTED) {
			RTW_INFO(FUNC_ADPT_FMT" call cfg80211_disconnected\n", FUNC_ADPT_ARG(padapter));
			rtw_cfg80211_disconnected(pwdev, reason, NULL, 0, locally_generated, GFP_ATOMIC);
		}

		RTW_INFO("pwdev->sme_state(a)=%d\n", pwdev->sme_state);
		#else
		if (pwdev_priv->connect_req) {
			RTW_INFO(FUNC_ADPT_FMT" call cfg80211_connect_result\n", FUNC_ADPT_ARG(padapter));
			rtw_cfg80211_connect_result(pwdev, NULL, NULL, 0, NULL, 0,
				WLAN_STATUS_UNSPECIFIED_FAILURE, GFP_ATOMIC);
		} else {
			RTW_INFO(FUNC_ADPT_FMT" call cfg80211_disconnected\n", FUNC_ADPT_ARG(padapter));
			rtw_cfg80211_disconnected(pwdev, reason, NULL, 0, locally_generated, GFP_ATOMIC);
		}
		#endif
	}

	rtw_wdev_free_connect_req(pwdev_priv);

	_exit_critical_bh(&pwdev_priv->connect_req_lock, &irqL);
}


#ifdef CONFIG_AP_MODE
static int rtw_cfg80211_ap_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
{
	int ret = 0;
	u32 wep_key_idx, wep_key_len, wep_total_len;
	struct sta_info *psta = NULL, *pbcmc_sta = NULL;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &(padapter->securitypriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	RTW_INFO("%s\n", __FUNCTION__);

	param->u.crypt.err = 0;
	param->u.crypt.alg[IEEE_CRYPT_ALG_NAME_LEN - 1] = '\0';

	/* sizeof(struct ieee_param) = 64 bytes; */
	/* if (param_len !=  (u32) ((u8 *) param->u.crypt.key - (u8 *) param) + param->u.crypt.key_len) */
	if (param_len !=  sizeof(struct ieee_param) + param->u.crypt.key_len) {
		ret =  -EINVAL;
		goto exit;
	}

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) {
		if (param->u.crypt.idx >= WEP_KEYS
#ifdef CONFIG_IEEE80211W
			&& param->u.crypt.idx > BIP_MAX_KEYID
#endif /* CONFIG_IEEE80211W */
		) {
			ret = -EINVAL;
			goto exit;
		}
	} else {
		psta = rtw_get_stainfo(pstapriv, param->sta_addr);
		if (!psta) {
			/* ret = -EINVAL; */
			RTW_INFO("rtw_set_encryption(), sta has already been removed or never been added\n");
			goto exit;
		}
	}

	if (strcmp(param->u.crypt.alg, "none") == 0 && (psta == NULL)) {
		/* todo:clear default encryption keys */

		RTW_INFO("clear default encryption keys, keyid=%d\n", param->u.crypt.idx);

		goto exit;
	}


	if (strcmp(param->u.crypt.alg, "WEP") == 0 && (psta == NULL)) {
		RTW_INFO("r871x_set_encryption, crypt.alg = WEP\n");

		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;

		RTW_INFO("r871x_set_encryption, wep_key_idx=%d, len=%d\n", wep_key_idx, wep_key_len);

		if ((wep_key_idx >= WEP_KEYS) || (wep_key_len <= 0)) {
			ret = -EINVAL;
			goto exit;
		}

		if (wep_key_len > 0)
			wep_key_len = wep_key_len <= 5 ? 5 : 13;

		if (psecuritypriv->bWepDefaultKeyIdxSet == 0) {
			/* wep default key has not been set, so use this key index as default key. */

			psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;
			psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;
			psecuritypriv->dot11PrivacyAlgrthm = _WEP40_;
			psecuritypriv->dot118021XGrpPrivacy = _WEP40_;

			if (wep_key_len == 13) {
				psecuritypriv->dot11PrivacyAlgrthm = _WEP104_;
				psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
			}

			psecuritypriv->dot11PrivacyKeyIndex = wep_key_idx;
		}

		_rtw_memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), param->u.crypt.key, wep_key_len);

		psecuritypriv->dot11DefKeylen[wep_key_idx] = wep_key_len;

		rtw_ap_set_wep_key(padapter, param->u.crypt.key, wep_key_len, wep_key_idx, 1);

		goto exit;

	}


	if (!psta && check_fwstate(pmlmepriv, WIFI_AP_STATE)) { /* group key */
		if (param->u.crypt.set_tx == 0) { /* group key */
			if (strcmp(param->u.crypt.alg, "WEP") == 0) {
				RTW_INFO("%s, set group_key, WEP\n", __FUNCTION__);

				_rtw_memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));

				psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
				if (param->u.crypt.key_len == 13)
					psecuritypriv->dot118021XGrpPrivacy = _WEP104_;

			} else if (strcmp(param->u.crypt.alg, "TKIP") == 0) {
				RTW_INFO("%s, set group_key, TKIP\n", __FUNCTION__);

				psecuritypriv->dot118021XGrpPrivacy = _TKIP_;

				_rtw_memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));

				/* DEBUG_ERR("set key length :param->u.crypt.key_len=%d\n", param->u.crypt.key_len); */
				/* set mic key */
				_rtw_memcpy(psecuritypriv->dot118021XGrptxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[16]), 8);
				_rtw_memcpy(psecuritypriv->dot118021XGrprxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[24]), 8);

				psecuritypriv->busetkipkey = _TRUE;

			} else if (strcmp(param->u.crypt.alg, "CCMP") == 0) {
				RTW_INFO("%s, set group_key, CCMP\n", __FUNCTION__);

				psecuritypriv->dot118021XGrpPrivacy = _AES_;

				_rtw_memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));
			}
#ifdef CONFIG_IEEE80211W
			else if (strcmp(param->u.crypt.alg, "BIP") == 0) {
				int no;

				RTW_INFO("BIP key_len=%d , index=%d\n", param->u.crypt.key_len, param->u.crypt.idx);
				/* save the IGTK key, length 16 bytes */
				_rtw_memcpy(padapter->securitypriv.dot11wBIPKey[param->u.crypt.idx].skey, param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));
				/* RTW_INFO("IGTK key below:\n");
				for(no=0;no<16;no++)
					printk(" %02x ", padapter->securitypriv.dot11wBIPKey[param->u.crypt.idx].skey[no]);
				RTW_INFO("\n"); */
				padapter->securitypriv.dot11wBIPKeyid = param->u.crypt.idx;
				padapter->securitypriv.binstallBIPkey = _TRUE;
				RTW_INFO(" ~~~~set sta key:IGKT\n");
				goto exit;
			}
#endif /* CONFIG_IEEE80211W */
			else {
				RTW_INFO("%s, set group_key, none\n", __FUNCTION__);

				psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
			}

			psecuritypriv->dot118021XGrpKeyid = param->u.crypt.idx;

			psecuritypriv->binstallGrpkey = _TRUE;

			psecuritypriv->dot11PrivacyAlgrthm = psecuritypriv->dot118021XGrpPrivacy;/* !!! */

			rtw_ap_set_group_key(padapter, param->u.crypt.key, psecuritypriv->dot118021XGrpPrivacy, param->u.crypt.idx);

			pbcmc_sta = rtw_get_bcmc_stainfo(padapter);
			if (pbcmc_sta) {
				pbcmc_sta->ieee8021x_blocked = _FALSE;
				pbcmc_sta->dot118021XPrivacy = psecuritypriv->dot118021XGrpPrivacy; /* rx will use bmc_sta's dot118021XPrivacy			 */
			}

		}

		goto exit;

	}

	if (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X && psta) { /* psk/802_1x */
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
			if (param->u.crypt.set_tx == 1) { /* pairwise key */
				_rtw_memcpy(psta->dot118021x_UncstKey.skey,  param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));

				if (strcmp(param->u.crypt.alg, "WEP") == 0) {
					RTW_INFO("%s, set pairwise key, WEP\n", __FUNCTION__);

					psta->dot118021XPrivacy = _WEP40_;
					if (param->u.crypt.key_len == 13)
						psta->dot118021XPrivacy = _WEP104_;
				} else if (strcmp(param->u.crypt.alg, "TKIP") == 0) {
					RTW_INFO("%s, set pairwise key, TKIP\n", __FUNCTION__);

					psta->dot118021XPrivacy = _TKIP_;

					/* DEBUG_ERR("set key length :param->u.crypt.key_len=%d\n", param->u.crypt.key_len); */
					/* set mic key */
					_rtw_memcpy(psta->dot11tkiptxmickey.skey, &(param->u.crypt.key[16]), 8);
					_rtw_memcpy(psta->dot11tkiprxmickey.skey, &(param->u.crypt.key[24]), 8);

					psecuritypriv->busetkipkey = _TRUE;

				} else if (strcmp(param->u.crypt.alg, "CCMP") == 0) {

					RTW_INFO("%s, set pairwise key, CCMP\n", __FUNCTION__);

					psta->dot118021XPrivacy = _AES_;
				} else {
					RTW_INFO("%s, set pairwise key, none\n", __FUNCTION__);

					psta->dot118021XPrivacy = _NO_PRIVACY_;
				}

				rtw_ap_set_pairwise_key(padapter, psta);

				psta->ieee8021x_blocked = _FALSE;

				psta->bpairwise_key_installed = _TRUE;

			} else { /* group key??? */
				if (strcmp(param->u.crypt.alg, "WEP") == 0) {
					_rtw_memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));

					psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
					if (param->u.crypt.key_len == 13)
						psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
				} else if (strcmp(param->u.crypt.alg, "TKIP") == 0) {
					psecuritypriv->dot118021XGrpPrivacy = _TKIP_;

					_rtw_memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));

					/* DEBUG_ERR("set key length :param->u.crypt.key_len=%d\n", param->u.crypt.key_len); */
					/* set mic key */
					_rtw_memcpy(psecuritypriv->dot118021XGrptxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[16]), 8);
					_rtw_memcpy(psecuritypriv->dot118021XGrprxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[24]), 8);

					psecuritypriv->busetkipkey = _TRUE;

				} else if (strcmp(param->u.crypt.alg, "CCMP") == 0) {
					psecuritypriv->dot118021XGrpPrivacy = _AES_;

					_rtw_memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));
				} else
					psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;

				psecuritypriv->dot118021XGrpKeyid = param->u.crypt.idx;

				psecuritypriv->binstallGrpkey = _TRUE;

				psecuritypriv->dot11PrivacyAlgrthm = psecuritypriv->dot118021XGrpPrivacy;/* !!! */

				rtw_ap_set_group_key(padapter, param->u.crypt.key, psecuritypriv->dot118021XGrpPrivacy, param->u.crypt.idx);

				pbcmc_sta = rtw_get_bcmc_stainfo(padapter);
				if (pbcmc_sta) {
					pbcmc_sta->ieee8021x_blocked = _FALSE;
					pbcmc_sta->dot118021XPrivacy = psecuritypriv->dot118021XGrpPrivacy; /* rx will use bmc_sta's dot118021XPrivacy			 */
				}

			}

		}

	}

exit:

	return ret;

}
#endif

static int rtw_cfg80211_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
{
	int ret = 0;
	u32 wep_key_idx, wep_key_len, wep_total_len;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif /* CONFIG_P2P */


	RTW_INFO("%s\n", __func__);

	param->u.crypt.err = 0;
	param->u.crypt.alg[IEEE_CRYPT_ALG_NAME_LEN - 1] = '\0';

	if (param_len < (u32)((u8 *) param->u.crypt.key - (u8 *) param) + param->u.crypt.key_len) {
		ret =  -EINVAL;
		goto exit;
	}

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) {
		if (param->u.crypt.idx >= WEP_KEYS
#ifdef CONFIG_IEEE80211W
			&& param->u.crypt.idx > BIP_MAX_KEYID
#endif /* CONFIG_IEEE80211W */
		) {
			ret = -EINVAL;
			goto exit;
		}
	} else {
#ifdef CONFIG_WAPI_SUPPORT
		if (strcmp(param->u.crypt.alg, "SMS4"))
#endif
		{
			ret = -EINVAL;
			goto exit;
		}
	}

	if (strcmp(param->u.crypt.alg, "WEP") == 0) {
		RTW_INFO("wpa_set_encryption, crypt.alg = WEP\n");

		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;

		if ((wep_key_idx > WEP_KEYS) || (wep_key_len <= 0)) {
			ret = -EINVAL;
			goto exit;
		}

		if (psecuritypriv->bWepDefaultKeyIdxSet == 0) {
			/* wep default key has not been set, so use this key index as default key. */

			wep_key_len = wep_key_len <= 5 ? 5 : 13;

			psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;
			psecuritypriv->dot11PrivacyAlgrthm = _WEP40_;
			psecuritypriv->dot118021XGrpPrivacy = _WEP40_;

			if (wep_key_len == 13) {
				psecuritypriv->dot11PrivacyAlgrthm = _WEP104_;
				psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
			}

			psecuritypriv->dot11PrivacyKeyIndex = wep_key_idx;
		}

		_rtw_memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), param->u.crypt.key, wep_key_len);

		psecuritypriv->dot11DefKeylen[wep_key_idx] = wep_key_len;

		rtw_set_key(padapter, psecuritypriv, wep_key_idx, 0, _TRUE);

		goto exit;
	}

	if (padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) { /* 802_1x */
		struct sta_info *psta, *pbcmc_sta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		/* RTW_INFO("%s, : dot11AuthAlgrthm == dot11AuthAlgrthm_8021X\n", __func__); */

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_MP_STATE) == _TRUE) { /* sta mode */
#ifdef CONFIG_RTW_80211R
			if ((rtw_to_roam(padapter) > 0) && rtw_chk_ft_flags(padapter, RTW_FT_SUPPORTED))
				psta = rtw_get_stainfo(pstapriv, pmlmepriv->assoc_bssid);
			else
#endif
				psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
			if (psta == NULL) {
				/* DEBUG_ERR( ("Set wpa_set_encryption: Obtain Sta_info fail\n")); */
				RTW_INFO("%s, : Obtain Sta_info fail\n", __func__);
			} else {
				/* Jeff: don't disable ieee8021x_blocked while clearing key */
				if (strcmp(param->u.crypt.alg, "none") != 0)
					psta->ieee8021x_blocked = _FALSE;


				if ((padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption2Enabled) ||
				    (padapter->securitypriv.ndisencryptstatus ==  Ndis802_11Encryption3Enabled))
					psta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;

				if (param->u.crypt.set_tx == 1) { /* pairwise key */

					RTW_INFO("%s, : param->u.crypt.set_tx ==1\n", __func__);

					_rtw_memcpy(psta->dot118021x_UncstKey.skey,  param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));

					if (strcmp(param->u.crypt.alg, "TKIP") == 0) { /* set mic key */
						/* DEBUG_ERR(("\nset key length :param->u.crypt.key_len=%d\n", param->u.crypt.key_len)); */
						_rtw_memcpy(psta->dot11tkiptxmickey.skey, &(param->u.crypt.key[16]), 8);
						_rtw_memcpy(psta->dot11tkiprxmickey.skey, &(param->u.crypt.key[24]), 8);

						padapter->securitypriv.busetkipkey = _FALSE;
					}
					psta->bpairwise_key_installed = _TRUE;
#ifdef CONFIG_RTW_80211R
					psta->ft_pairwise_key_installed = _TRUE;
#endif
					/* DEBUG_ERR((" param->u.crypt.key_len=%d\n",param->u.crypt.key_len)); */
					RTW_INFO(" ~~~~set sta key:unicastkey\n");

					rtw_setstakey_cmd(padapter, psta, UNICAST_KEY, _TRUE);
				} else { /* group key */
					if (strcmp(param->u.crypt.alg, "TKIP") == 0 || strcmp(param->u.crypt.alg, "CCMP") == 0) {
						_rtw_memcpy(padapter->securitypriv.dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key,
							(param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));
						_rtw_memcpy(padapter->securitypriv.dot118021XGrptxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[16]), 8);
						_rtw_memcpy(padapter->securitypriv.dot118021XGrprxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[24]), 8);
						padapter->securitypriv.binstallGrpkey = _TRUE;
						if (param->u.crypt.idx < 4) 
							_rtw_memcpy(padapter->securitypriv.iv_seq[param->u.crypt.idx], param->u.crypt.seq, 8);							

						/* DEBUG_ERR((" param->u.crypt.key_len=%d\n", param->u.crypt.key_len)); */
						RTW_INFO(" ~~~~set sta key:groupkey\n");

						padapter->securitypriv.dot118021XGrpKeyid = param->u.crypt.idx;
						rtw_set_key(padapter, &padapter->securitypriv, param->u.crypt.idx, 1, _TRUE);
					}
#ifdef CONFIG_IEEE80211W
					else if (strcmp(param->u.crypt.alg, "BIP") == 0) {
						int no;
						/* RTW_INFO("BIP key_len=%d , index=%d @@@@@@@@@@@@@@@@@@\n", param->u.crypt.key_len, param->u.crypt.idx); */
						/* save the IGTK key, length 16 bytes */
						_rtw_memcpy(padapter->securitypriv.dot11wBIPKey[param->u.crypt.idx].skey,  param->u.crypt.key,
							(param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));
						/*RTW_INFO("IGTK key below:\n");
						for(no=0;no<16;no++)
							printk(" %02x ", padapter->securitypriv.dot11wBIPKey[param->u.crypt.idx].skey[no]);
						RTW_INFO("\n");*/
						padapter->securitypriv.dot11wBIPKeyid = param->u.crypt.idx;
						padapter->securitypriv.binstallBIPkey = _TRUE;
						RTW_INFO(" ~~~~set sta key:IGKT\n");
					}
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_P2P
					if (pwdinfo->driver_interface == DRIVER_CFG80211) {
						if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_PROVISIONING_ING))
							rtw_p2p_set_state(pwdinfo, P2P_STATE_PROVISIONING_DONE);
					}
#endif /* CONFIG_P2P */

				}
			}

			pbcmc_sta = rtw_get_bcmc_stainfo(padapter);
			if (pbcmc_sta == NULL) {
				/* DEBUG_ERR( ("Set OID_802_11_ADD_KEY: bcmc stainfo is null\n")); */
			} else {
				/* Jeff: don't disable ieee8021x_blocked while clearing key */
				if (strcmp(param->u.crypt.alg, "none") != 0)
					pbcmc_sta->ieee8021x_blocked = _FALSE;

				if ((padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption2Enabled) ||
				    (padapter->securitypriv.ndisencryptstatus ==  Ndis802_11Encryption3Enabled))
					pbcmc_sta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;
			}
		} else if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) { /* adhoc mode */
		}
	}

#ifdef CONFIG_WAPI_SUPPORT
	if (strcmp(param->u.crypt.alg, "SMS4") == 0) {
		PRT_WAPI_T			pWapiInfo = &padapter->wapiInfo;
		PRT_WAPI_STA_INFO	pWapiSta;
		u8					WapiASUEPNInitialValueSrc[16] = {0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C} ;
		u8					WapiAEPNInitialValueSrc[16] = {0x37, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C} ;
		u8					WapiAEMultiCastPNInitialValueSrc[16] = {0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C} ;

		if (param->u.crypt.set_tx == 1) {
			list_for_each_entry(pWapiSta, &pWapiInfo->wapiSTAUsedList, list) {
				if (_rtw_memcmp(pWapiSta->PeerMacAddr, param->sta_addr, 6)) {
					_rtw_memcpy(pWapiSta->lastTxUnicastPN, WapiASUEPNInitialValueSrc, 16);

					pWapiSta->wapiUsk.bSet = true;
					_rtw_memcpy(pWapiSta->wapiUsk.dataKey, param->u.crypt.key, 16);
					_rtw_memcpy(pWapiSta->wapiUsk.micKey, param->u.crypt.key + 16, 16);
					pWapiSta->wapiUsk.keyId = param->u.crypt.idx ;
					pWapiSta->wapiUsk.bTxEnable = true;

					_rtw_memcpy(pWapiSta->lastRxUnicastPNBEQueue, WapiAEPNInitialValueSrc, 16);
					_rtw_memcpy(pWapiSta->lastRxUnicastPNBKQueue, WapiAEPNInitialValueSrc, 16);
					_rtw_memcpy(pWapiSta->lastRxUnicastPNVIQueue, WapiAEPNInitialValueSrc, 16);
					_rtw_memcpy(pWapiSta->lastRxUnicastPNVOQueue, WapiAEPNInitialValueSrc, 16);
					_rtw_memcpy(pWapiSta->lastRxUnicastPN, WapiAEPNInitialValueSrc, 16);
					pWapiSta->wapiUskUpdate.bTxEnable = false;
					pWapiSta->wapiUskUpdate.bSet = false;

					if (psecuritypriv->sw_encrypt == false || psecuritypriv->sw_decrypt == false) {
						/* set unicast key for ASUE */
						rtw_wapi_set_key(padapter, &pWapiSta->wapiUsk, pWapiSta, false, false);
					}
				}
			}
		} else {
			list_for_each_entry(pWapiSta, &pWapiInfo->wapiSTAUsedList, list) {
				if (_rtw_memcmp(pWapiSta->PeerMacAddr, get_bssid(pmlmepriv), 6)) {
					pWapiSta->wapiMsk.bSet = true;
					_rtw_memcpy(pWapiSta->wapiMsk.dataKey, param->u.crypt.key, 16);
					_rtw_memcpy(pWapiSta->wapiMsk.micKey, param->u.crypt.key + 16, 16);
					pWapiSta->wapiMsk.keyId = param->u.crypt.idx ;
					pWapiSta->wapiMsk.bTxEnable = false;
					if (!pWapiSta->bSetkeyOk)
						pWapiSta->bSetkeyOk = true;
					pWapiSta->bAuthenticateInProgress = false;

					_rtw_memcpy(pWapiSta->lastRxMulticastPN, WapiAEMultiCastPNInitialValueSrc, 16);

					if (psecuritypriv->sw_decrypt == false) {
						/* set rx broadcast key for ASUE */
						rtw_wapi_set_key(padapter, &pWapiSta->wapiMsk, pWapiSta, true, false);
					}
				}

			}
		}
	}
#endif


exit:

	RTW_INFO("%s, ret=%d\n", __func__, ret);


	return ret;
}

static int cfg80211_rtw_add_key(struct wiphy *wiphy, struct net_device *ndev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
	u8 key_index, bool pairwise, const u8 *mac_addr,
#else	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) */
	u8 key_index, const u8 *mac_addr,
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) */
	struct key_params *params)
{
	char *alg_name;
	u32 param_len;
	struct ieee_param *param = NULL;
	int ret = 0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct wireless_dev *rtw_wdev = padapter->rtw_wdev;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
#ifdef CONFIG_TDLS
	struct sta_info *ptdls_sta;
#endif /* CONFIG_TDLS */

	RTW_INFO(FUNC_NDEV_FMT" adding key for %pM\n", FUNC_NDEV_ARG(ndev), mac_addr);
	RTW_INFO("cipher=0x%x\n", params->cipher);
	RTW_INFO("key_len=0x%x\n", params->key_len);
	RTW_INFO("seq_len=0x%x\n", params->seq_len);
	RTW_INFO("key_index=%d\n", key_index);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
	RTW_INFO("pairwise=%d\n", pairwise);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) */

	param_len = sizeof(struct ieee_param) + params->key_len;
	param = (struct ieee_param *)rtw_malloc(param_len);
	if (param == NULL)
		return -1;

	_rtw_memset(param, 0, param_len);

	param->cmd = IEEE_CMD_SET_ENCRYPTION;
	_rtw_memset(param->sta_addr, 0xff, ETH_ALEN);

	switch (params->cipher) {
	case IW_AUTH_CIPHER_NONE:
		/* todo: remove key */
		/* remove = 1;	 */
		alg_name = "none";
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		alg_name = "WEP";
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		alg_name = "TKIP";
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		alg_name = "CCMP";
		break;
#ifdef CONFIG_IEEE80211W
	case WLAN_CIPHER_SUITE_AES_CMAC:
		alg_name = "BIP";
		break;
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_WAPI_SUPPORT
	case WLAN_CIPHER_SUITE_SMS4:
		alg_name = "SMS4";
		if (pairwise == NL80211_KEYTYPE_PAIRWISE) {
			if (key_index != 0 && key_index != 1) {
				ret = -ENOTSUPP;
				goto addkey_end;
			}
			_rtw_memcpy((void *)param->sta_addr, (void *)mac_addr, ETH_ALEN);
		} else
			RTW_INFO("mac_addr is null\n");
		RTW_INFO("rtw_wx_set_enc_ext: SMS4 case\n");
		break;
#endif

	default:
		ret = -ENOTSUPP;
		goto addkey_end;
	}

	strncpy((char *)param->u.crypt.alg, alg_name, IEEE_CRYPT_ALG_NAME_LEN);


	if (!mac_addr || is_broadcast_ether_addr(mac_addr)) {
		param->u.crypt.set_tx = 0; /* for wpa/wpa2 group key */
	} else {
		param->u.crypt.set_tx = 1; /* for wpa/wpa2 pairwise key */
	}


	/* param->u.crypt.idx = key_index - 1; */
	param->u.crypt.idx = key_index;

	if (params->seq_len && params->seq)
		_rtw_memcpy(param->u.crypt.seq, (u8 *)params->seq, params->seq_len);

	if (params->key_len && params->key) {
		param->u.crypt.key_len = params->key_len;
		_rtw_memcpy(param->u.crypt.key, (u8 *)params->key, params->key_len);
	}

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE) {
#ifdef CONFIG_TDLS
		if (rtw_tdls_is_driver_setup(padapter) == _FALSE && mac_addr) {
			ptdls_sta = rtw_get_stainfo(&padapter->stapriv, (void *)mac_addr);
			if (ptdls_sta != NULL && ptdls_sta->tdls_sta_state) {
				_rtw_memcpy(ptdls_sta->tpk.tk, params->key, params->key_len);
				rtw_tdls_set_key(padapter, ptdls_sta);
				goto addkey_end;
			}
		}
#endif /* CONFIG_TDLS */

		ret =  rtw_cfg80211_set_encryption(ndev, param, param_len);
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) {
#ifdef CONFIG_AP_MODE
		if (mac_addr)
			_rtw_memcpy(param->sta_addr, (void *)mac_addr, ETH_ALEN);

		ret = rtw_cfg80211_ap_set_encryption(ndev, param, param_len);
#endif
	} else if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE
		|| check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE
	) {
		/* RTW_INFO("@@@@@@@@@@ fw_state=0x%x, iftype=%d\n", pmlmepriv->fw_state, rtw_wdev->iftype); */
		ret =  rtw_cfg80211_set_encryption(ndev, param, param_len);
	} else
		RTW_INFO("error! fw_state=0x%x, iftype=%d\n", pmlmepriv->fw_state, rtw_wdev->iftype);


addkey_end:
	if (param)
		rtw_mfree((u8 *)param, param_len);

	return ret;

}

static int cfg80211_rtw_get_key(struct wiphy *wiphy, struct net_device *ndev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
	u8 key_index, bool pairwise, const u8 *mac_addr,
#else	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) */
	u8 key_index, const u8 *mac_addr,
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) */
	void *cookie,
	void (*callback)(void *cookie, struct key_params *))
{
#if 0
	struct iwm_priv *iwm = ndev_to_iwm(ndev);
	struct iwm_key *key = &iwm->keys[key_index];
	struct key_params params;

	IWM_DBG_WEXT(iwm, DBG, "Getting key %d\n", key_index);

	memset(&params, 0, sizeof(params));

	params.cipher = key->cipher;
	params.key_len = key->key_len;
	params.seq_len = key->seq_len;
	params.seq = key->seq;
	params.key = key->key;

	callback(cookie, &params);

	return key->key_len ? 0 : -ENOENT;
#endif
	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
	return 0;
}

static int cfg80211_rtw_del_key(struct wiphy *wiphy, struct net_device *ndev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
				u8 key_index, bool pairwise, const u8 *mac_addr)
#else	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) */
				u8 key_index, const u8 *mac_addr)
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) */
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	RTW_INFO(FUNC_NDEV_FMT" key_index=%d\n", FUNC_NDEV_ARG(ndev), key_index);

	if (key_index == psecuritypriv->dot11PrivacyKeyIndex) {
		/* clear the flag of wep default key set. */
		psecuritypriv->bWepDefaultKeyIdxSet = 0;
	}

	return 0;
}

static int cfg80211_rtw_set_default_key(struct wiphy *wiphy,
	struct net_device *ndev, u8 key_index
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)) || defined(COMPAT_KERNEL_RELEASE)
	, bool unicast, bool multicast
	#endif
)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct security_priv *psecuritypriv = &padapter->securitypriv;

#define SET_DEF_KEY_PARAM_FMT " key_index=%d"
#define SET_DEF_KEY_PARAM_ARG , key_index
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)) || defined(COMPAT_KERNEL_RELEASE)
	#define SET_DEF_KEY_PARAM_FMT_2_6_38 ", unicast=%d, multicast=%d"
	#define SET_DEF_KEY_PARAM_ARG_2_6_38 , unicast, multicast
#else
	#define SET_DEF_KEY_PARAM_FMT_2_6_38 ""
	#define SET_DEF_KEY_PARAM_ARG_2_6_38
#endif

	RTW_INFO(FUNC_NDEV_FMT
		SET_DEF_KEY_PARAM_FMT
		SET_DEF_KEY_PARAM_FMT_2_6_38
		"\n", FUNC_NDEV_ARG(ndev)
		SET_DEF_KEY_PARAM_ARG
		SET_DEF_KEY_PARAM_ARG_2_6_38
	);

	if ((key_index < WEP_KEYS) && ((psecuritypriv->dot11PrivacyAlgrthm == _WEP40_) || (psecuritypriv->dot11PrivacyAlgrthm == _WEP104_))) { /* set wep default key */
		psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;

		psecuritypriv->dot11PrivacyKeyIndex = key_index;

		psecuritypriv->dot11PrivacyAlgrthm = _WEP40_;
		psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
		if (psecuritypriv->dot11DefKeylen[key_index] == 13) {
			psecuritypriv->dot11PrivacyAlgrthm = _WEP104_;
			psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
		}

		psecuritypriv->bWepDefaultKeyIdxSet = 1; /* set the flag to represent that wep default key has been set */
	}

	return 0;

}
#if defined(CONFIG_GTK_OL) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
static int cfg80211_rtw_set_rekey_data(struct wiphy *wiphy,
	struct net_device *ndev,
	struct cfg80211_gtk_rekey_data *data)
{
	/*int i;*/
	struct sta_info *psta;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct mlme_priv   *pmlmepriv = &padapter->mlmepriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct security_priv *psecuritypriv = &(padapter->securitypriv);

	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
	if (psta == NULL) {
		RTW_INFO("%s, : Obtain Sta_info fail\n", __func__);
		return -1;
	}

	_rtw_memcpy(psta->kek, data->kek, NL80211_KEK_LEN);
	/*printk("\ncfg80211_rtw_set_rekey_data KEK:");
	for(i=0;i<NL80211_KEK_LEN; i++)
		printk(" %02x ", psta->kek[i]);*/
	_rtw_memcpy(psta->kck, data->kck, NL80211_KCK_LEN);
	/*printk("\ncfg80211_rtw_set_rekey_data KCK:");
	for(i=0;i<NL80211_KCK_LEN; i++)
		printk(" %02x ", psta->kck[i]);*/
	_rtw_memcpy(psta->replay_ctr, data->replay_ctr, NL80211_REPLAY_CTR_LEN);
	psecuritypriv->binstallKCK_KEK = _TRUE;
	/*printk("\nREPLAY_CTR: ");
	for(i=0;i<RTW_REPLAY_CTR_LEN; i++)
		printk(" %02x ", psta->replay_ctr[i]);*/

	return 0;
}
#endif /*CONFIG_GTK_OL*/
static int cfg80211_rtw_get_station(struct wiphy *wiphy,
	struct net_device *ndev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0))
	u8 *mac,
#else
	const u8 *mac,
#endif
	struct station_info *sinfo)
{
	int ret = 0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;

	sinfo->filled = 0;

	if (!mac) {
		RTW_INFO(FUNC_NDEV_FMT" mac==%p\n", FUNC_NDEV_ARG(ndev), mac);
		ret = -ENOENT;
		goto exit;
	}

	psta = rtw_get_stainfo(pstapriv, (u8 *)mac);
	if (psta == NULL) {
		RTW_INFO("%s, sta_info is null\n", __func__);
		ret = -ENOENT;
		goto exit;
	}

#ifdef CONFIG_DEBUG_CFG80211
	RTW_INFO(FUNC_NDEV_FMT" mac="MAC_FMT"\n", FUNC_NDEV_ARG(ndev), MAC_ARG(mac));
#endif

	/* for infra./P2PClient mode */
	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)
		&& check_fwstate(pmlmepriv, _FW_LINKED)
	) {
		struct wlan_network  *cur_network = &(pmlmepriv->cur_network);

		if (_rtw_memcmp((u8 *)mac, cur_network->network.MacAddress, ETH_ALEN) == _FALSE) {
			RTW_INFO("%s, mismatch bssid="MAC_FMT"\n", __func__, MAC_ARG(cur_network->network.MacAddress));
			ret = -ENOENT;
			goto exit;
		}

		sinfo->filled |= STATION_INFO_SIGNAL;
		sinfo->signal = translate_percentage_to_dbm(padapter->recvpriv.signal_strength);

		sinfo->filled |= STATION_INFO_TX_BITRATE;
		sinfo->txrate.legacy = rtw_get_cur_max_rate(padapter);

		sinfo->filled |= STATION_INFO_RX_PACKETS;
		sinfo->rx_packets = sta_rx_data_pkts(psta);

		sinfo->filled |= STATION_INFO_TX_PACKETS;
		sinfo->tx_packets = psta->sta_stats.tx_pkts;

        sinfo->filled |= STATION_INFO_TX_FAILED;
        sinfo->tx_failed = psta->sta_stats.tx_fail_cnt;

	}

	/* for Ad-Hoc/AP mode */
	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)
		|| check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)
		|| check_fwstate(pmlmepriv, WIFI_AP_STATE))
		&& check_fwstate(pmlmepriv, _FW_LINKED)
	) {
		/* TODO: should acquire station info... */
	}

exit:
	return ret;
}

extern int netdev_open(struct net_device *pnetdev);

#if 0
enum nl80211_iftype {
	NL80211_IFTYPE_UNSPECIFIED,
	NL80211_IFTYPE_ADHOC, /* 1 */
	NL80211_IFTYPE_STATION, /* 2 */
	NL80211_IFTYPE_AP, /* 3 */
	NL80211_IFTYPE_AP_VLAN,
	NL80211_IFTYPE_WDS,
	NL80211_IFTYPE_MONITOR, /* 6 */
	NL80211_IFTYPE_MESH_POINT,
	NL80211_IFTYPE_P2P_CLIENT, /* 8 */
	NL80211_IFTYPE_P2P_GO, /* 9 */
	/* keep last */
	NUM_NL80211_IFTYPES,
	NL80211_IFTYPE_MAX = NUM_NL80211_IFTYPES - 1
};
#endif
static int cfg80211_rtw_change_iface(struct wiphy *wiphy,
				     struct net_device *ndev,
				     enum nl80211_iftype type,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
				     u32 *flags,
#endif
				     struct vif_params *params)
{
	enum nl80211_iftype old_type;
	NDIS_802_11_NETWORK_INFRASTRUCTURE networkType;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct wireless_dev *rtw_wdev = padapter->rtw_wdev;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
	u8 is_p2p = _FALSE;
#endif
	int ret = 0;
	u8 change = _FALSE;

	RTW_INFO(FUNC_NDEV_FMT" type=%d, hw_port:%d\n", FUNC_NDEV_ARG(ndev), type, padapter->hw_port);

	if (adapter_to_dvobj(padapter)->processing_dev_remove == _TRUE) {
		ret = -EPERM;
		goto exit;
	}


	RTW_INFO(FUNC_NDEV_FMT" call netdev_open\n", FUNC_NDEV_ARG(ndev));
	if (netdev_open(ndev) != 0) {
		RTW_INFO(FUNC_NDEV_FMT" call netdev_open fail\n", FUNC_NDEV_ARG(ndev));
		ret = -EPERM;
		goto exit;
	}


	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		RTW_INFO(FUNC_NDEV_FMT" call rtw_pwr_wakeup fail\n", FUNC_NDEV_ARG(ndev));
		ret = -EPERM;
		goto exit;
	}

	old_type = rtw_wdev->iftype;
	RTW_INFO(FUNC_NDEV_FMT" old_iftype=%d, new_iftype=%d\n",
		FUNC_NDEV_ARG(ndev), old_type, type);

	if (old_type != type) {
		change = _TRUE;
		pmlmeext->action_public_rxseq = 0xffff;
		pmlmeext->action_public_dialog_token = 0xff;
	}

	/* initial default type */
	ndev->type = ARPHRD_ETHER;

	/*
	 * Disable Power Save in moniter mode,
	 * and enable it after leaving moniter mode.
	 */
	if (type == NL80211_IFTYPE_MONITOR) {
		rtw_ps_deny(padapter, PS_DENY_MONITOR_MODE);
		LeaveAllPowerSaveMode(padapter);
	} else if (old_type == NL80211_IFTYPE_MONITOR) {
		/* driver in moniter mode in last time */
		rtw_ps_deny_cancel(padapter, PS_DENY_MONITOR_MODE);
	}

	switch (type) {
	case NL80211_IFTYPE_ADHOC:
		networkType = Ndis802_11IBSS;
		break;

	#if defined(CONFIG_P2P) && ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE))
	case NL80211_IFTYPE_P2P_CLIENT:
		is_p2p = _TRUE;
	#endif
	case NL80211_IFTYPE_STATION:
		networkType = Ndis802_11Infrastructure;

		#ifdef CONFIG_P2P
		if (change && pwdinfo->driver_interface == DRIVER_CFG80211) {
			if (is_p2p == _TRUE)
				rtw_p2p_enable(padapter, P2P_ROLE_CLIENT);
			#if !RTW_P2P_GROUP_INTERFACE
			else if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT)
					|| rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)
			) {
				/* it means remove GC/GO and change mode from GC/GO to station(P2P DEVICE) */
				rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
			}
			#endif
		}
		#endif /* CONFIG_P2P */

		break;

	#if defined(CONFIG_P2P) && ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE))
	case NL80211_IFTYPE_P2P_GO:
		is_p2p = _TRUE;
	#endif
	case NL80211_IFTYPE_AP:
		networkType = Ndis802_11APMode;

		#ifdef CONFIG_P2P
		if (change && pwdinfo->driver_interface == DRIVER_CFG80211) {
			if (is_p2p == _TRUE)
				rtw_p2p_enable(padapter, P2P_ROLE_GO);
			#if !RTW_P2P_GROUP_INTERFACE
			else if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
				/* it means P2P Group created, we will be GO and change mode from  P2P DEVICE to AP(GO) */
				rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
			}
			#endif
		}
		#endif /* CONFIG_P2P */

		break;

	case NL80211_IFTYPE_MONITOR:
		networkType = Ndis802_11Monitor;
#if 0
		ndev->type = ARPHRD_IEEE80211; /* IEEE 802.11 : 801 */
#endif
		ndev->type = ARPHRD_IEEE80211_RADIOTAP; /* IEEE 802.11 + radiotap header : 803 */
		break;
	default:
		ret = -EOPNOTSUPP;
		goto exit;
	}

	rtw_wdev->iftype = type;

	if (rtw_set_802_11_infrastructure_mode(padapter, networkType) == _FALSE) {
		rtw_wdev->iftype = old_type;
		ret = -EPERM;
		goto exit;
	}

	rtw_setopmode_cmd(padapter, networkType, _TRUE);

exit:

	RTW_INFO(FUNC_NDEV_FMT" ret:%d\n", FUNC_NDEV_ARG(ndev), ret);
	return ret;
}

void rtw_cfg80211_indicate_scan_done(_adapter *adapter, bool aborted)
{
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(adapter);
	_irqL	irqL;

#if (KERNEL_VERSION(4, 7, 0) <= LINUX_VERSION_CODE)
	struct cfg80211_scan_info info;

	memset(&info, 0, sizeof(info));
	info.aborted = aborted;
#endif

	_enter_critical_bh(&pwdev_priv->scan_req_lock, &irqL);
	if (pwdev_priv->scan_request != NULL) {
		#ifdef CONFIG_DEBUG_CFG80211
		RTW_INFO("%s with scan req\n", __FUNCTION__);
		#endif

		/* avoid WARN_ON(request != wiphy_to_dev(request->wiphy)->scan_req); */
		if (pwdev_priv->scan_request->wiphy != pwdev_priv->rtw_wdev->wiphy)
			RTW_INFO("error wiphy compare\n");
		else
#if (KERNEL_VERSION(4, 7, 0) <= LINUX_VERSION_CODE)
			cfg80211_scan_done(pwdev_priv->scan_request, &info);
#else
			cfg80211_scan_done(pwdev_priv->scan_request, aborted);
#endif

		pwdev_priv->scan_request = NULL;
	} else {
		#ifdef CONFIG_DEBUG_CFG80211
		RTW_INFO("%s without scan req\n", __FUNCTION__);
		#endif
	}
	_exit_critical_bh(&pwdev_priv->scan_req_lock, &irqL);
}

u32 rtw_cfg80211_wait_scan_req_empty(_adapter *adapter, u32 timeout_ms)
{
	struct rtw_wdev_priv *wdev_priv = adapter_wdev_data(adapter);
	u8 empty = _FALSE;
	systime start;
	u32 pass_ms;

	start = rtw_get_current_time();

	while (rtw_get_passing_time_ms(start) <= timeout_ms) {

		if (RTW_CANNOT_RUN(adapter))
			break;

		if (!wdev_priv->scan_request) {
			empty = _TRUE;
			break;
		}

		rtw_msleep_os(10);
	}

	pass_ms = rtw_get_passing_time_ms(start);

	if (empty == _FALSE && pass_ms > timeout_ms)
		RTW_PRINT(FUNC_ADPT_FMT" pass_ms:%u, timeout\n"
			, FUNC_ADPT_ARG(adapter), pass_ms);

	return pass_ms;
}

void rtw_cfg80211_unlink_bss(_adapter *padapter, struct wlan_network *pnetwork)
{
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct wiphy *wiphy = pwdev->wiphy;
	struct cfg80211_bss *bss = NULL;
	WLAN_BSSID_EX select_network = pnetwork->network;

	bss = cfg80211_get_bss(wiphy, NULL/*notify_channel*/,
		select_network.MacAddress, select_network.Ssid.Ssid,
		select_network.Ssid.SsidLength,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
		select_network.InfrastructureMode == Ndis802_11Infrastructure?IEEE80211_BSS_TYPE_ESS:IEEE80211_BSS_TYPE_IBSS,
		IEEE80211_PRIVACY(select_network.Privacy));
#else
		select_network.InfrastructureMode == Ndis802_11Infrastructure?WLAN_CAPABILITY_ESS:WLAN_CAPABILITY_IBSS,
		select_network.InfrastructureMode == Ndis802_11Infrastructure?WLAN_CAPABILITY_ESS:WLAN_CAPABILITY_IBSS);
#endif

	if (bss) {
		cfg80211_unlink_bss(wiphy, bss);
		RTW_INFO("%s(): cfg80211_unlink %s!!\n", __func__, select_network.Ssid.Ssid);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
		cfg80211_put_bss(padapter->rtw_wdev->wiphy, bss);
#else
		cfg80211_put_bss(bss);
#endif
	}
	return;
}

/* if target wps scan ongoing, target_ssid is filled */
int rtw_cfg80211_is_target_wps_scan(struct cfg80211_scan_request *scan_req, struct cfg80211_ssid *target_ssid)
{
	int ret = 0;

	if (scan_req->n_ssids != 1
		|| scan_req->ssids[0].ssid_len == 0
		|| scan_req->n_channels != 1
	)
		goto exit;

	/* under target WPS scan */
	_rtw_memcpy(target_ssid, scan_req->ssids, sizeof(struct cfg80211_ssid));
	ret = 1;

exit:
	return ret;
}

static void _rtw_cfg80211_surveydone_event_callback(_adapter *padapter, struct cfg80211_scan_request *scan_req)
{
	_irqL	irqL;
	_list					*plist, *phead;
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	_queue				*queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	u32 cnt = 0;
	u32 wait_for_surveydone;
	sint wait_status;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
	struct cfg80211_ssid target_ssid;
	u8 target_wps_scan = 0;

#ifdef CONFIG_DEBUG_CFG80211
	RTW_INFO("%s\n", __func__);
#endif

	if (scan_req)
		target_wps_scan = rtw_cfg80211_is_target_wps_scan(scan_req, &target_ssid);
	else {
		_enter_critical_bh(&pwdev_priv->scan_req_lock, &irqL);
		if (pwdev_priv->scan_request != NULL)
			target_wps_scan = rtw_cfg80211_is_target_wps_scan(pwdev_priv->scan_request, &target_ssid);
		_exit_critical_bh(&pwdev_priv->scan_req_lock, &irqL);
	}

	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	phead = get_list_head(queue);
	plist = get_next(phead);

	while (1) {
		if (rtw_end_of_queue_search(phead, plist) == _TRUE)
			break;

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

		/* report network only if the current channel set contains the channel to which this network belongs */
		if (rtw_chset_search_ch(adapter_to_chset(padapter), pnetwork->network.Configuration.DSConfig) >= 0
			&& rtw_mlme_band_check(padapter, pnetwork->network.Configuration.DSConfig) == _TRUE
			&& _TRUE == rtw_validate_ssid(&(pnetwork->network.Ssid))
		) {
			if (target_wps_scan)
				rtw_cfg80211_clear_wps_sr_of_non_target_bss(padapter, pnetwork, &target_ssid);
			rtw_cfg80211_inform_bss(padapter, pnetwork);
		}
#if 0
		/* check ralink testbed RSN IE length */
		{
			if (_rtw_memcmp(pnetwork->network.Ssid.Ssid, "Ralink_11n_AP", 13)) {
				uint ie_len = 0;
				u8 *p = NULL;
				p = rtw_get_ie(pnetwork->network.IEs + _BEACON_IE_OFFSET_, _RSN_IE_2_, &ie_len, (pnetwork->network.IELength - _BEACON_IE_OFFSET_));
				RTW_INFO("ie_len=%d\n", ie_len);
			}
		}
#endif
		plist = get_next(plist);

	}

	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
}

inline void rtw_cfg80211_surveydone_event_callback(_adapter *padapter)
{
	_rtw_cfg80211_surveydone_event_callback(padapter, NULL);
}

static int rtw_cfg80211_set_probe_req_wpsp2pie(_adapter *padapter, char *buf, int len)
{
	int ret = 0;
	uint wps_ielen = 0;
	u8 *wps_ie;
	u32	p2p_ielen = 0;
	u8 *p2p_ie;
	u32	wfd_ielen = 0;
	u8 *wfd_ie;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

#ifdef CONFIG_DEBUG_CFG80211
	RTW_INFO("%s, ielen=%d\n", __func__, len);
#endif

	if (len > 0) {
		wps_ie = rtw_get_wps_ie(buf, len, NULL, &wps_ielen);
		if (wps_ie) {
			#ifdef CONFIG_DEBUG_CFG80211
			RTW_INFO("probe_req_wps_ielen=%d\n", wps_ielen);
			#endif

			if (pmlmepriv->wps_probe_req_ie) {
				u32 free_len = pmlmepriv->wps_probe_req_ie_len;
				pmlmepriv->wps_probe_req_ie_len = 0;
				rtw_mfree(pmlmepriv->wps_probe_req_ie, free_len);
				pmlmepriv->wps_probe_req_ie = NULL;
			}

			pmlmepriv->wps_probe_req_ie = rtw_malloc(wps_ielen);
			if (pmlmepriv->wps_probe_req_ie == NULL) {
				RTW_INFO("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;

			}
			_rtw_memcpy(pmlmepriv->wps_probe_req_ie, wps_ie, wps_ielen);
			pmlmepriv->wps_probe_req_ie_len = wps_ielen;
		}

		/* buf += wps_ielen; */
		/* len -= wps_ielen; */

		#ifdef CONFIG_P2P
		p2p_ie = rtw_get_p2p_ie(buf, len, NULL, &p2p_ielen);
		if (p2p_ie) {
			struct wifidirect_info *wdinfo = &padapter->wdinfo;
			u32 attr_contentlen = 0;
			u8 listen_ch_attr[5];

			#ifdef CONFIG_DEBUG_CFG80211
			RTW_INFO("probe_req_p2p_ielen=%d\n", p2p_ielen);
			#endif

			if (pmlmepriv->p2p_probe_req_ie) {
				u32 free_len = pmlmepriv->p2p_probe_req_ie_len;
				pmlmepriv->p2p_probe_req_ie_len = 0;
				rtw_mfree(pmlmepriv->p2p_probe_req_ie, free_len);
				pmlmepriv->p2p_probe_req_ie = NULL;
			}

			pmlmepriv->p2p_probe_req_ie = rtw_malloc(p2p_ielen);
			if (pmlmepriv->p2p_probe_req_ie == NULL) {
				RTW_INFO("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;

			}
			_rtw_memcpy(pmlmepriv->p2p_probe_req_ie, p2p_ie, p2p_ielen);
			pmlmepriv->p2p_probe_req_ie_len = p2p_ielen;

			if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_LISTEN_CH, (u8 *)listen_ch_attr, (uint *) &attr_contentlen)
				&& attr_contentlen == 5) {
				if (wdinfo->listen_channel !=  listen_ch_attr[4]) {
					RTW_INFO(FUNC_ADPT_FMT" listen channel - country:%c%c%c, class:%u, ch:%u\n",
						FUNC_ADPT_ARG(padapter), listen_ch_attr[0], listen_ch_attr[1], listen_ch_attr[2],
						listen_ch_attr[3], listen_ch_attr[4]);
					wdinfo->listen_channel = listen_ch_attr[4];
				}
			}
		}
		#endif /* CONFIG_P2P */

		#ifdef CONFIG_WFD
		wfd_ie = rtw_get_wfd_ie(buf, len, NULL, &wfd_ielen);
		if (wfd_ie) {
			#ifdef CONFIG_DEBUG_CFG80211
			RTW_INFO("probe_req_wfd_ielen=%d\n", wfd_ielen);
			#endif

			if (rtw_mlme_update_wfd_ie_data(pmlmepriv, MLME_PROBE_REQ_IE, wfd_ie, wfd_ielen) != _SUCCESS)
				return -EINVAL;
		}
		#endif /* CONFIG_WFD */
	}

	return ret;

}

#ifdef CONFIG_CONCURRENT_MODE
u8 rtw_cfg80211_scan_via_buddy(_adapter *padapter, struct cfg80211_scan_request *request)
{
	int i;
	u8 ret = _FALSE;
	_adapter *iface = NULL;
	_irqL	irqL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	for (i = 0; i < dvobj->iface_nums; i++) {
		struct mlme_priv *buddy_mlmepriv;
		struct rtw_wdev_priv *buddy_wdev_priv;

		iface = dvobj->padapters[i];
		if (iface == NULL)
			continue;

		if (iface == padapter)
			continue;

		if (rtw_is_adapter_up(iface) == _FALSE)
			continue;

		buddy_mlmepriv = &iface->mlmepriv;
		if (!check_fwstate(buddy_mlmepriv, _FW_UNDER_SURVEY))
			continue;

		buddy_wdev_priv = adapter_wdev_data(iface);
		_enter_critical_bh(&pwdev_priv->scan_req_lock, &irqL);
		_enter_critical_bh(&buddy_wdev_priv->scan_req_lock, &irqL);
		if (buddy_wdev_priv->scan_request) {
			pmlmepriv->scanning_via_buddy_intf = _TRUE;
			_enter_critical_bh(&pmlmepriv->lock, &irqL);
			set_fwstate(pmlmepriv, _FW_UNDER_SURVEY);
			_exit_critical_bh(&pmlmepriv->lock, &irqL);
			pwdev_priv->scan_request = request;
			ret = _TRUE;
		}
		_exit_critical_bh(&buddy_wdev_priv->scan_req_lock, &irqL);
		_exit_critical_bh(&pwdev_priv->scan_req_lock, &irqL);

		if (ret == _TRUE)
			goto exit;
	}

exit:
	return ret;
}

void rtw_cfg80211_indicate_scan_done_for_buddy(_adapter *padapter, bool bscan_aborted)
{
	int i;
	u8 ret = 0;
	_adapter *iface = NULL;
	_irqL	irqL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mlme_priv *mlmepriv;
	struct rtw_wdev_priv *wdev_priv;
	bool indicate_buddy_scan;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && rtw_is_adapter_up(iface)) {

			if (iface == padapter)
				continue;

			mlmepriv = &(iface->mlmepriv);
			wdev_priv = adapter_wdev_data(iface);

			indicate_buddy_scan = _FALSE;
			_enter_critical_bh(&wdev_priv->scan_req_lock, &irqL);
			if (wdev_priv->scan_request && mlmepriv->scanning_via_buddy_intf == _TRUE) {
				mlmepriv->scanning_via_buddy_intf = _FALSE;
				clr_fwstate(mlmepriv, _FW_UNDER_SURVEY);
				indicate_buddy_scan = _TRUE;
			}
			_exit_critical_bh(&wdev_priv->scan_req_lock, &irqL);

			if (indicate_buddy_scan == _TRUE) {
				rtw_cfg80211_surveydone_event_callback(iface);
				rtw_indicate_scan_done(iface, bscan_aborted);
			}

		}
	}
}
#endif /* CONFIG_CONCURRENT_MODE */

static int cfg80211_rtw_scan(struct wiphy *wiphy
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
	, struct net_device *ndev
	#endif
	, struct cfg80211_scan_request *request)
{
	int i, chan_num = 0;
	u8 _status = _FALSE;
	int ret = 0;
	struct sitesurvey_parm parm;
	_irqL	irqL;
	u8 *wps_ie = NULL;
	uint wps_ielen = 0;
	u8 *p2p_ie = NULL;
	uint p2p_ielen = 0;
	u8 survey_times = 3;
	u8 survey_times_for_one_ch = 6;
	struct cfg80211_ssid *ssids = request->ssids;
	int social_channel = 0, j = 0;
	bool need_indicate_scan_done = _FALSE;
	bool ps_denied = _FALSE;

	_adapter *padapter;
	struct wireless_dev *wdev;
	struct rtw_wdev_priv *pwdev_priv;
	struct mlme_priv *pmlmepriv;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo;
#endif /* CONFIG_P2P */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	wdev = request->wdev;
	#if defined(RTW_DEDICATED_P2P_DEVICE)
	if (wdev == wiphy_to_pd_wdev(wiphy))
		padapter = wiphy_to_adapter(wiphy);
	else
	#endif
	if (wdev_to_ndev(wdev))
		padapter = (_adapter *)rtw_netdev_priv(wdev_to_ndev(wdev));
	else {
		ret = -EINVAL;
		goto exit;
	}
#else
	if (ndev == NULL) {
		ret = -EINVAL;
		goto exit;
	}
	padapter = (_adapter *)rtw_netdev_priv(ndev);
	wdev = ndev_to_wdev(ndev);
#endif

	pwdev_priv = adapter_wdev_data(padapter);
	pmlmepriv = &padapter->mlmepriv;
#ifdef CONFIG_P2P
	pwdinfo = &(padapter->wdinfo);
#endif /* CONFIG_P2P */

	RTW_INFO(FUNC_ADPT_FMT"%s\n", FUNC_ADPT_ARG(padapter)
		, wdev == wiphy_to_pd_wdev(wiphy) ? " PD" : "");

#ifdef CONFIG_MP_INCLUDED
	if (rtw_mp_mode_check(padapter)) {
		RTW_INFO("MP mode block Scan request\n");
		ret = -EPERM;
		goto exit;
	}
#endif

#ifdef CONFIG_RTW_REPEATER_SON
	if (padapter->rtw_rson_scanstage == RSON_SCAN_PROCESS) {
		RTW_INFO(FUNC_ADPT_FMT" blocking scan for under rson scanning process\n", FUNC_ADPT_ARG(padapter));
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;
	}
#endif

	if (adapter_wdev_data(padapter)->block_scan == _TRUE) {
		RTW_INFO(FUNC_ADPT_FMT" wdev_priv.block_scan is set\n", FUNC_ADPT_ARG(padapter));
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;
	}

	rtw_ps_deny(padapter, PS_DENY_SCAN);
	ps_denied = _TRUE;
	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;
	}

#ifdef CONFIG_P2P
	if (pwdinfo->driver_interface == DRIVER_CFG80211) {
		if (ssids->ssid != NULL
			&& _rtw_memcmp(ssids->ssid, "DIRECT-", 7)
			&& rtw_get_p2p_ie((u8 *)request->ie, request->ie_len, NULL, NULL)
		) {
			if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
				rtw_p2p_enable(padapter, P2P_ROLE_DEVICE);
			else {
				rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
				#ifdef CONFIG_DEBUG_CFG80211
				RTW_INFO("%s, role=%d, p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo));
				#endif
			}
			rtw_p2p_set_state(pwdinfo, P2P_STATE_LISTEN);

			if (request->n_channels == 3 &&
				request->channels[0]->hw_value == 1 &&
				request->channels[1]->hw_value == 6 &&
				request->channels[2]->hw_value == 11
			)
				social_channel = 1;
		}
	}
#endif /*CONFIG_P2P*/

	if (request->ie && request->ie_len > 0)
		rtw_cfg80211_set_probe_req_wpsp2pie(padapter, (u8 *)request->ie, request->ie_len);

	if (rtw_is_scan_deny(padapter)) {
		RTW_INFO(FUNC_ADPT_FMT	": scan deny\n", FUNC_ADPT_ARG(padapter));
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;
	}

	/* check fw state*/
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) {

#ifdef CONFIG_DEBUG_CFG80211
		RTW_INFO(FUNC_ADPT_FMT" under WIFI_AP_STATE\n", FUNC_ADPT_ARG(padapter));
#endif

		if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS | _FW_UNDER_SURVEY | _FW_UNDER_LINKING) == _TRUE) {
			RTW_INFO("%s, fwstate=0x%x\n", __func__, pmlmepriv->fw_state);

			if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS))
				RTW_INFO("AP mode process WPS\n");

			need_indicate_scan_done = _TRUE;
			goto check_need_indicate_scan_done;
		}
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == _TRUE) {
		RTW_INFO("%s, fwstate=0x%x\n", __func__, pmlmepriv->fw_state);
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;
	} else if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE) {
		RTW_INFO("%s, fwstate=0x%x\n", __func__, pmlmepriv->fw_state);
		ret = -EBUSY;
		goto check_need_indicate_scan_done;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_buddy_check_fwstate(padapter, _FW_UNDER_LINKING | WIFI_UNDER_WPS)) {
		RTW_INFO("%s exit due to buddy_intf's mlme state under linking or wps\n", __func__);
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;

	} else if (rtw_mi_buddy_check_fwstate(padapter, _FW_UNDER_SURVEY)) {
		bool scan_via_buddy = rtw_cfg80211_scan_via_buddy(padapter, request);

		if (scan_via_buddy == _FALSE)
			need_indicate_scan_done = _TRUE;

		goto check_need_indicate_scan_done;
	}
#endif /* CONFIG_CONCURRENT_MODE */

	/* busy traffic check*/
	if (rtw_mi_busy_traffic_check(padapter, _TRUE)) {
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;
	}

#ifdef CONFIG_P2P
	if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) && !rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE)) {
		rtw_p2p_set_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH);
		rtw_free_network_queue(padapter, _TRUE);

		if (social_channel == 0)
			rtw_p2p_findphase_ex_set(pwdinfo, P2P_FINDPHASE_EX_NONE);
		else
			rtw_p2p_findphase_ex_set(pwdinfo, P2P_FINDPHASE_EX_SOCIAL_LAST);
	}
#endif /* CONFIG_P2P */

	rtw_init_sitesurvey_parm(padapter, &parm);

	/* parsing request ssids, n_ssids */
	for (i = 0; i < request->n_ssids && i < RTW_SSID_SCAN_AMOUNT; i++) {
		#ifdef CONFIG_DEBUG_CFG80211
		RTW_INFO("ssid=%s, len=%d\n", ssids[i].ssid, ssids[i].ssid_len);
		#endif
		_rtw_memcpy(&parm.ssid[i].Ssid, ssids[i].ssid, ssids[i].ssid_len);
		parm.ssid[i].SsidLength = ssids[i].ssid_len;
	}
	parm.ssid_num = i;

	/* parsing channels, n_channels */
	for (i = 0; i < request->n_channels && i < RTW_CHANNEL_SCAN_AMOUNT; i++) {
		#ifdef CONFIG_DEBUG_CFG80211
		RTW_INFO(FUNC_ADPT_FMT CHAN_FMT"\n", FUNC_ADPT_ARG(padapter), CHAN_ARG(request->channels[i]));
		#endif
		parm.ch[i].hw_value = request->channels[i]->hw_value;
		parm.ch[i].flags = request->channels[i]->flags;
	}
	parm.ch_num = i;

	if (request->n_channels == 1) {
		for (i = 1; i < survey_times_for_one_ch; i++)
			_rtw_memcpy(&parm.ch[i], &parm.ch[0], sizeof(struct rtw_ieee80211_channel));
		parm.ch_num = survey_times_for_one_ch;
	} else if (request->n_channels <= 4) {
		for (j = request->n_channels - 1; j >= 0; j--)
			for (i = 0; i < survey_times; i++)
				_rtw_memcpy(&parm.ch[j * survey_times + i], &parm.ch[j], sizeof(struct rtw_ieee80211_channel));
		parm.ch_num = survey_times * request->n_channels;
	}

	_enter_critical_bh(&pwdev_priv->scan_req_lock, &irqL);
	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	_status = rtw_sitesurvey_cmd(padapter, &parm);
	if (_status == _SUCCESS)
		pwdev_priv->scan_request = request;
	else
		ret = -1;
	_exit_critical_bh(&pmlmepriv->lock, &irqL);
	_exit_critical_bh(&pwdev_priv->scan_req_lock, &irqL);

check_need_indicate_scan_done:
	if (_TRUE == need_indicate_scan_done) {
#if (KERNEL_VERSION(4, 7, 0) <= LINUX_VERSION_CODE)
		struct cfg80211_scan_info info;

		memset(&info, 0, sizeof(info));
		info.aborted = 0;
#endif

		_rtw_cfg80211_surveydone_event_callback(padapter, request);
#if (KERNEL_VERSION(4, 7, 0) <= LINUX_VERSION_CODE)
		cfg80211_scan_done(request, &info);
#else
		cfg80211_scan_done(request, 0);
#endif
	}

cancel_ps_deny:
	if (ps_denied == _TRUE)
		rtw_ps_deny_cancel(padapter, PS_DENY_SCAN);

exit:
	return ret;

}

static int cfg80211_rtw_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
#if 0
	struct iwm_priv *iwm = wiphy_to_iwm(wiphy);

	if (changed & WIPHY_PARAM_RTS_THRESHOLD &&
	    (iwm->conf.rts_threshold != wiphy->rts_threshold)) {
		int ret;

		iwm->conf.rts_threshold = wiphy->rts_threshold;

		ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
				CFG_RTS_THRESHOLD,
				iwm->conf.rts_threshold);
		if (ret < 0)
			return ret;
	}

	if (changed & WIPHY_PARAM_FRAG_THRESHOLD &&
	    (iwm->conf.frag_threshold != wiphy->frag_threshold)) {
		int ret;

		iwm->conf.frag_threshold = wiphy->frag_threshold;

		ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_FA_CFG_FIX,
				CFG_FRAG_THRESHOLD,
				iwm->conf.frag_threshold);
		if (ret < 0)
			return ret;
	}
#endif
	RTW_INFO("%s\n", __func__);
	return 0;
}



static int rtw_cfg80211_set_wpa_version(struct security_priv *psecuritypriv, u32 wpa_version)
{
	RTW_INFO("%s, wpa_version=%d\n", __func__, wpa_version);

	if (!wpa_version) {
		psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;
		return 0;
	}


	if (wpa_version & (NL80211_WPA_VERSION_1 | NL80211_WPA_VERSION_2))
		psecuritypriv->ndisauthtype = Ndis802_11AuthModeWPAPSK;

#if 0
	if (wpa_version & NL80211_WPA_VERSION_2)
		psecuritypriv->ndisauthtype = Ndis802_11AuthModeWPA2PSK;
#endif

	#ifdef CONFIG_WAPI_SUPPORT
	if (wpa_version & NL80211_WAPI_VERSION_1)
		psecuritypriv->ndisauthtype = Ndis802_11AuthModeWAPI;
	#endif

	return 0;

}

static int rtw_cfg80211_set_auth_type(struct security_priv *psecuritypriv,
		enum nl80211_auth_type sme_auth_type)
{
	RTW_INFO("%s, nl80211_auth_type=%d\n", __func__, sme_auth_type);


	switch (sme_auth_type) {
	case NL80211_AUTHTYPE_AUTOMATIC:

		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;

		break;
	case NL80211_AUTHTYPE_OPEN_SYSTEM:

		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;

		if (psecuritypriv->ndisauthtype > Ndis802_11AuthModeWPA)
			psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;

#ifdef CONFIG_WAPI_SUPPORT
		if (psecuritypriv->ndisauthtype == Ndis802_11AuthModeWAPI)
			psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_WAPI;
#endif

		break;
	case NL80211_AUTHTYPE_SHARED_KEY:

		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Shared;

		psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;


		break;
	default:
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
		/* return -ENOTSUPP; */
	}

	return 0;

}

static int rtw_cfg80211_set_cipher(struct security_priv *psecuritypriv, u32 cipher, bool ucast)
{
	u32 ndisencryptstatus = Ndis802_11EncryptionDisabled;

	u32 *profile_cipher = ucast ? &psecuritypriv->dot11PrivacyAlgrthm :
		&psecuritypriv->dot118021XGrpPrivacy;

	RTW_INFO("%s, ucast=%d, cipher=0x%x\n", __func__, ucast, cipher);


	if (!cipher) {
		*profile_cipher = _NO_PRIVACY_;
		psecuritypriv->ndisencryptstatus = ndisencryptstatus;
		return 0;
	}

	switch (cipher) {
	case IW_AUTH_CIPHER_NONE:
		*profile_cipher = _NO_PRIVACY_;
		ndisencryptstatus = Ndis802_11EncryptionDisabled;
#ifdef CONFIG_WAPI_SUPPORT
		if (psecuritypriv->dot11PrivacyAlgrthm == _SMS4_)
			*profile_cipher = _SMS4_;
#endif
		break;
	case WLAN_CIPHER_SUITE_WEP40:
		*profile_cipher = _WEP40_;
		ndisencryptstatus = Ndis802_11Encryption1Enabled;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		*profile_cipher = _WEP104_;
		ndisencryptstatus = Ndis802_11Encryption1Enabled;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		*profile_cipher = _TKIP_;
		ndisencryptstatus = Ndis802_11Encryption2Enabled;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		*profile_cipher = _AES_;
		ndisencryptstatus = Ndis802_11Encryption3Enabled;
		break;
#ifdef CONFIG_WAPI_SUPPORT
	case WLAN_CIPHER_SUITE_SMS4:
		*profile_cipher = _SMS4_;
		ndisencryptstatus = Ndis802_11_EncrypteionWAPI;
		break;
#endif
	default:
		RTW_INFO("Unsupported cipher: 0x%x\n", cipher);
		return -ENOTSUPP;
	}

	if (ucast) {
		psecuritypriv->ndisencryptstatus = ndisencryptstatus;

		/* if(psecuritypriv->dot11PrivacyAlgrthm >= _AES_) */
		/*	psecuritypriv->ndisauthtype = Ndis802_11AuthModeWPA2PSK; */
	}

	return 0;
}

static int rtw_cfg80211_set_key_mgt(struct security_priv *psecuritypriv, u32 key_mgt)
{
	RTW_INFO("%s, key_mgt=0x%x\n", __func__, key_mgt);

	if (key_mgt == WLAN_AKM_SUITE_8021X) {
		/* *auth_type = UMAC_AUTH_TYPE_8021X; */
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
		psecuritypriv->rsn_akm_suite_type = 1;
	} else if (key_mgt == WLAN_AKM_SUITE_PSK) {
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
		psecuritypriv->rsn_akm_suite_type = 2;
	}
#ifdef CONFIG_WAPI_SUPPORT
	else if (key_mgt == WLAN_AKM_SUITE_WAPI_PSK)
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_WAPI;
	else if (key_mgt == WLAN_AKM_SUITE_WAPI_CERT)
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_WAPI;
#endif
#ifdef CONFIG_RTW_80211R
	else if (key_mgt == WLAN_AKM_SUITE_FT_8021X) {
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
		psecuritypriv->rsn_akm_suite_type = 3;
	} else if (key_mgt == WLAN_AKM_SUITE_FT_PSK) {
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
		psecuritypriv->rsn_akm_suite_type = 4;
	}
#endif
	else {
		RTW_INFO("Invalid key mgt: 0x%x\n", key_mgt);
		/* return -EINVAL; */
	}

	return 0;
}

static int rtw_cfg80211_set_wpa_ie(_adapter *padapter, u8 *pie, size_t ielen)
{
	u8 *buf = NULL, *pos = NULL;
	u32 left;
	int group_cipher = 0, pairwise_cipher = 0;
	int ret = 0;
	int wpa_ielen = 0;
	int wpa2_ielen = 0;
	u8 *pwpa, *pwpa2;
	u8 null_addr[] = {0, 0, 0, 0, 0, 0};

	if (pie == NULL || !ielen) {
		/* Treat this as normal case, but need to clear WIFI_UNDER_WPS */
		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
		goto exit;
	}

	if (ielen > MAX_WPA_IE_LEN + MAX_WPS_IE_LEN + MAX_P2P_IE_LEN) {
		ret = -EINVAL;
		goto exit;
	}

	buf = rtw_zmalloc(ielen);
	if (buf == NULL) {
		ret =  -ENOMEM;
		goto exit;
	}

	_rtw_memcpy(buf, pie , ielen);

	/* dump */
	{
		int i;
		RTW_INFO("set wpa_ie(length:%zu):\n", ielen);
		for (i = 0; i < ielen; i = i + 8)
			RTW_INFO("0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x\n", buf[i], buf[i + 1], buf[i + 2], buf[i + 3], buf[i + 4], buf[i + 5], buf[i + 6], buf[i + 7]);
	}

	pos = buf;
	if (ielen < RSN_HEADER_LEN) {
		ret  = -1;
		goto exit;
	}

	pwpa = rtw_get_wpa_ie(buf, &wpa_ielen, ielen);
	if (pwpa && wpa_ielen > 0) {
		if (rtw_parse_wpa_ie(pwpa, wpa_ielen + 2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeWPAPSK;
			_rtw_memcpy(padapter->securitypriv.supplicant_ie, &pwpa[0], wpa_ielen + 2);

			RTW_INFO("got wpa_ie, wpa_ielen:%u\n", wpa_ielen);
		}
	}

	pwpa2 = rtw_get_wpa2_ie(buf, &wpa2_ielen, ielen);
	if (pwpa2 && wpa2_ielen > 0) {
		if (rtw_parse_wpa2_ie(pwpa2, wpa2_ielen + 2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeWPA2PSK;
			_rtw_memcpy(padapter->securitypriv.supplicant_ie, &pwpa2[0], wpa2_ielen + 2);

			RTW_INFO("got wpa2_ie, wpa2_ielen:%u\n", wpa2_ielen);
		}
	}

	if (group_cipher == 0)
		group_cipher = WPA_CIPHER_NONE;
	if (pairwise_cipher == 0)
		pairwise_cipher = WPA_CIPHER_NONE;

	switch (group_cipher) {
	case WPA_CIPHER_NONE:
		padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
		padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled;
		break;
	case WPA_CIPHER_WEP40:
		padapter->securitypriv.dot118021XGrpPrivacy = _WEP40_;
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		break;
	case WPA_CIPHER_TKIP:
		padapter->securitypriv.dot118021XGrpPrivacy = _TKIP_;
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption2Enabled;
		break;
	case WPA_CIPHER_CCMP:
		padapter->securitypriv.dot118021XGrpPrivacy = _AES_;
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption3Enabled;
		break;
	case WPA_CIPHER_WEP104:
		padapter->securitypriv.dot118021XGrpPrivacy = _WEP104_;
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		break;
	}

	switch (pairwise_cipher) {
	case WPA_CIPHER_NONE:
		padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled;
		break;
	case WPA_CIPHER_WEP40:
		padapter->securitypriv.dot11PrivacyAlgrthm = _WEP40_;
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		break;
	case WPA_CIPHER_TKIP:
		padapter->securitypriv.dot11PrivacyAlgrthm = _TKIP_;
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption2Enabled;
		break;
	case WPA_CIPHER_CCMP:
		padapter->securitypriv.dot11PrivacyAlgrthm = _AES_;
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption3Enabled;
		break;
	case WPA_CIPHER_WEP104:
		padapter->securitypriv.dot11PrivacyAlgrthm = _WEP104_;
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		break;
	}

	{/* handle wps_ie */
		uint wps_ielen;
		u8 *wps_ie;

		wps_ie = rtw_get_wps_ie(buf, ielen, NULL, &wps_ielen);
		if (wps_ie && wps_ielen > 0) {
			RTW_INFO("got wps_ie, wps_ielen:%u\n", wps_ielen);
			padapter->securitypriv.wps_ie_len = wps_ielen < MAX_WPS_IE_LEN ? wps_ielen : MAX_WPS_IE_LEN;
			_rtw_memcpy(padapter->securitypriv.wps_ie, wps_ie, padapter->securitypriv.wps_ie_len);
			set_fwstate(&padapter->mlmepriv, WIFI_UNDER_WPS);
		} else
			_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
	}

	#ifdef CONFIG_P2P
	{/* check p2p_ie for assoc req; */
		uint p2p_ielen = 0;
		u8 *p2p_ie;
		struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

		p2p_ie = rtw_get_p2p_ie(buf, ielen, NULL, &p2p_ielen);
		if (p2p_ie) {
			#ifdef CONFIG_DEBUG_CFG80211
			RTW_INFO("%s p2p_assoc_req_ielen=%d\n", __FUNCTION__, p2p_ielen);
			#endif

			if (pmlmepriv->p2p_assoc_req_ie) {
				u32 free_len = pmlmepriv->p2p_assoc_req_ie_len;
				pmlmepriv->p2p_assoc_req_ie_len = 0;
				rtw_mfree(pmlmepriv->p2p_assoc_req_ie, free_len);
				pmlmepriv->p2p_assoc_req_ie = NULL;
			}

			pmlmepriv->p2p_assoc_req_ie = rtw_malloc(p2p_ielen);
			if (pmlmepriv->p2p_assoc_req_ie == NULL) {
				RTW_INFO("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				goto exit;
			}
			_rtw_memcpy(pmlmepriv->p2p_assoc_req_ie, p2p_ie, p2p_ielen);
			pmlmepriv->p2p_assoc_req_ie_len = p2p_ielen;
		}
	}
	#endif /* CONFIG_P2P */

	#ifdef CONFIG_WFD
	{
		uint wfd_ielen = 0;
		u8 *wfd_ie;
		struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

		wfd_ie = rtw_get_wfd_ie(buf, ielen, NULL, &wfd_ielen);
		if (wfd_ie) {
			#ifdef CONFIG_DEBUG_CFG80211
			RTW_INFO("%s wfd_assoc_req_ielen=%d\n", __FUNCTION__, wfd_ielen);
			#endif

			if (rtw_mlme_update_wfd_ie_data(pmlmepriv, MLME_ASSOC_REQ_IE, wfd_ie, wfd_ielen) != _SUCCESS)
				goto exit;
		}
	}
	#endif /* CONFIG_WFD */

	/* TKIP and AES disallow multicast packets until installing group key */
	if (padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_
		|| padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_WTMIC_
		|| padapter->securitypriv.dot11PrivacyAlgrthm == _AES_)
		/* WPS open need to enable multicast */
		/* || check_fwstate(&padapter->mlmepriv, WIFI_UNDER_WPS) == _TRUE) */
		rtw_hal_set_hwreg(padapter, HW_VAR_OFF_RCR_AM, null_addr);


exit:
	if (buf)
		rtw_mfree(buf, ielen);
	if (ret)
		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);

	return ret;
}

static int cfg80211_rtw_join_ibss(struct wiphy *wiphy, struct net_device *ndev,
				  struct cfg80211_ibss_params *params)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	NDIS_802_11_SSID ndis_ssid;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork = (WLAN_BSSID_EX *)(&(pmlmeinfo->network));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	struct cfg80211_chan_def *pch_def;
#endif
	struct ieee80211_channel *pch;
	int ret = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	pch_def = (struct cfg80211_chan_def *)(&params->chandef);
	pch = (struct ieee80211_channel *) pch_def->chan;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
	pch = (struct ieee80211_channel *)(params->channel);
#endif

	if (!params->ssid || !params->ssid_len) {
		ret = -EINVAL;
		goto exit;
	}

	if (params->ssid_len > IW_ESSID_MAX_SIZE) {
		ret = -E2BIG;
		goto exit;
	}

	rtw_ps_deny(padapter, PS_DENY_JOIN);
	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -EPERM;
		goto cancel_ps_deny;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_buddy_check_fwstate(padapter, _FW_UNDER_LINKING)) {
		RTW_INFO("%s, but buddy_intf is under linking\n", __FUNCTION__);
		ret = -EINVAL;
		goto cancel_ps_deny;
	}
	rtw_mi_buddy_scan_abort(padapter, _TRUE); /* OR rtw_mi_scan_abort(padapter, _TRUE);*/
#endif /*CONFIG_CONCURRENT_MODE*/


	_rtw_memset(&ndis_ssid, 0, sizeof(NDIS_802_11_SSID));
	ndis_ssid.SsidLength = params->ssid_len;
	_rtw_memcpy(ndis_ssid.Ssid, (u8 *)params->ssid, params->ssid_len);

	/* RTW_INFO("ssid=%s, len=%zu\n", ndis_ssid.Ssid, params->ssid_len); */

	psecuritypriv->ndisencryptstatus = Ndis802_11EncryptionDisabled;
	psecuritypriv->dot11PrivacyAlgrthm = _NO_PRIVACY_;
	psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
	psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open; /* open system */
	psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;

	ret = rtw_cfg80211_set_auth_type(psecuritypriv, NL80211_AUTHTYPE_OPEN_SYSTEM);
	rtw_set_802_11_authentication_mode(padapter, psecuritypriv->ndisauthtype);

	RTW_INFO("%s: center_freq = %d\n", __func__, pch->center_freq);
	pmlmeext->cur_channel = rtw_freq2ch(pch->center_freq);

	if (rtw_set_802_11_ssid(padapter, &ndis_ssid) == _FALSE) {
		ret = -1;
		goto cancel_ps_deny;
	}

cancel_ps_deny:
	rtw_ps_deny_cancel(padapter, PS_DENY_JOIN);
exit:
	return ret;
}

static int cfg80211_rtw_leave_ibss(struct wiphy *wiphy, struct net_device *ndev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct wireless_dev *rtw_wdev = padapter->rtw_wdev;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
	enum nl80211_iftype old_type;
	int ret = 0;

	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	rtw_wdev_set_not_indic_disco(pwdev_priv, 1);

	old_type = rtw_wdev->iftype;

	rtw_set_to_roam(padapter, 0);

	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
		rtw_scan_abort(padapter);
		LeaveAllPowerSaveMode(padapter);

		rtw_wdev->iftype = NL80211_IFTYPE_STATION;

		if (rtw_set_802_11_infrastructure_mode(padapter, Ndis802_11Infrastructure) == _FALSE) {
			rtw_wdev->iftype = old_type;
			ret = -EPERM;
			goto leave_ibss;
		}
		rtw_setopmode_cmd(padapter, Ndis802_11Infrastructure, _TRUE);
	}

leave_ibss:
	rtw_wdev_set_not_indic_disco(pwdev_priv, 0);

	return 0;
}

static int cfg80211_rtw_connect(struct wiphy *wiphy, struct net_device *ndev,
				struct cfg80211_connect_params *sme)
{
	int ret = 0;
	struct wlan_network *pnetwork = NULL;
	NDIS_802_11_AUTHENTICATION_MODE authmode;
	NDIS_802_11_SSID ndis_ssid;
	u8 *dst_ssid, *src_ssid;
	u8 *dst_bssid, *src_bssid;
	/* u8 matched_by_bssid=_FALSE; */
	/* u8 matched_by_ssid=_FALSE; */
	u8 matched = _FALSE;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	_queue *queue = &pmlmepriv->scanned_queue;
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
	_irqL irqL;

	rtw_wdev_set_not_indic_disco(pwdev_priv, 1);

	RTW_INFO("=>"FUNC_NDEV_FMT" - Start to Connection\n", FUNC_NDEV_ARG(ndev));
	RTW_INFO("privacy=%d, key=%p, key_len=%d, key_idx=%d, auth_type=%d\n",
		sme->privacy, sme->key, sme->key_len, sme->key_idx, sme->auth_type);


	if (pwdev_priv->block == _TRUE) {
		ret = -EBUSY;
		RTW_INFO("%s wdev_priv.block is set\n", __FUNCTION__);
		goto exit;
	}

#ifdef CONFIG_PLATFORM_MSTAR_SCAN_BEFORE_CONNECT
	printk("MStar Android!\n");
	if (pwdev_priv->bandroid_scan == _FALSE) {
#ifdef CONFIG_P2P
		struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
		if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
#endif /* CONFIG_P2P */
		{
			ret = -EBUSY;
			printk("Android hasn't attached yet!\n");
			goto exit;
		}
	}
#endif

	if (!sme->ssid || !sme->ssid_len) {
		ret = -EINVAL;
		goto exit;
	}

	if (sme->ssid_len > IW_ESSID_MAX_SIZE) {
		ret = -E2BIG;
		goto exit;
	}

	rtw_ps_deny(padapter, PS_DENY_JOIN);
	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -EPERM;
		goto cancel_ps_deny;
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE) {
		ret = -EBUSY;
		RTW_INFO("%s, fw_state=0x%x, goto exit\n", __func__, pmlmepriv->fw_state);
		goto cancel_ps_deny;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_buddy_check_fwstate(padapter, _FW_UNDER_LINKING)) {
		ret = -EINVAL;
		goto cancel_ps_deny;
	}
#endif

	rtw_mi_scan_abort(padapter, _TRUE);

	_rtw_memset(&ndis_ssid, 0, sizeof(NDIS_802_11_SSID));
	ndis_ssid.SsidLength = sme->ssid_len;
	_rtw_memcpy(ndis_ssid.Ssid, (u8 *)sme->ssid, sme->ssid_len);

	RTW_INFO("ssid=%s, len=%zu\n", ndis_ssid.Ssid, sme->ssid_len);


	if (sme->bssid)
		RTW_INFO("bssid="MAC_FMT"\n", MAC_ARG(sme->bssid));


	psecuritypriv->ndisencryptstatus = Ndis802_11EncryptionDisabled;
	psecuritypriv->dot11PrivacyAlgrthm = _NO_PRIVACY_;
	psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
	psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open; /* open system */
	psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;

#ifdef CONFIG_WAPI_SUPPORT
	padapter->wapiInfo.bWapiEnable = false;
#endif

	ret = rtw_cfg80211_set_wpa_version(psecuritypriv, sme->crypto.wpa_versions);
	if (ret < 0)
		goto cancel_ps_deny;

#ifdef CONFIG_WAPI_SUPPORT
	if (sme->crypto.wpa_versions & NL80211_WAPI_VERSION_1) {
		padapter->wapiInfo.bWapiEnable = true;
		padapter->wapiInfo.extra_prefix_len = WAPI_EXT_LEN;
		padapter->wapiInfo.extra_postfix_len = SMS4_MIC_LEN;
	}
#endif

	ret = rtw_cfg80211_set_auth_type(psecuritypriv, sme->auth_type);

#ifdef CONFIG_WAPI_SUPPORT
	if (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_WAPI)
		padapter->mlmeextpriv.mlmext_info.auth_algo = psecuritypriv->dot11AuthAlgrthm;
#endif


	if (ret < 0)
		goto cancel_ps_deny;

	RTW_INFO("%s, ie_len=%zu\n", __func__, sme->ie_len);

	ret = rtw_cfg80211_set_wpa_ie(padapter, (u8 *)sme->ie, sme->ie_len);
	if (ret < 0)
		goto cancel_ps_deny;

	if (sme->crypto.n_ciphers_pairwise) {
		ret = rtw_cfg80211_set_cipher(psecuritypriv, sme->crypto.ciphers_pairwise[0], _TRUE);
		if (ret < 0)
			goto cancel_ps_deny;
	}

	/* For WEP Shared auth */
	if (sme->key_len > 0 && sme->key) {
		u32 wep_key_idx, wep_key_len, wep_total_len;
		NDIS_802_11_WEP	*pwep = NULL;
		RTW_INFO("%s(): Shared/Auto WEP\n", __FUNCTION__);

		wep_key_idx = sme->key_idx;
		wep_key_len = sme->key_len;

		if (sme->key_idx > WEP_KEYS) {
			ret = -EINVAL;
			goto cancel_ps_deny;
		}

		if (wep_key_len > 0) {
			wep_key_len = wep_key_len <= 5 ? 5 : 13;
			wep_total_len = wep_key_len + FIELD_OFFSET(NDIS_802_11_WEP, KeyMaterial);
			pwep = (NDIS_802_11_WEP *) rtw_malloc(wep_total_len);
			if (pwep == NULL) {
				RTW_INFO(" wpa_set_encryption: pwep allocate fail !!!\n");
				ret = -ENOMEM;
				goto cancel_ps_deny;
			}

			_rtw_memset(pwep, 0, wep_total_len);

			pwep->KeyLength = wep_key_len;
			pwep->Length = wep_total_len;

			if (wep_key_len == 13) {
				padapter->securitypriv.dot11PrivacyAlgrthm = _WEP104_;
				padapter->securitypriv.dot118021XGrpPrivacy = _WEP104_;
			}
		} else {
			ret = -EINVAL;
			goto cancel_ps_deny;
		}

		pwep->KeyIndex = wep_key_idx;
		pwep->KeyIndex |= 0x80000000;

		_rtw_memcpy(pwep->KeyMaterial, (void *)sme->key, pwep->KeyLength);

		if (rtw_set_802_11_add_wep(padapter, pwep) == (u8)_FAIL)
			ret = -EOPNOTSUPP ;

		if (pwep)
			rtw_mfree((u8 *)pwep, wep_total_len);

		if (ret < 0)
			goto cancel_ps_deny;
	}

	ret = rtw_cfg80211_set_cipher(psecuritypriv, sme->crypto.cipher_group, _FALSE);
	if (ret < 0)
		return ret;

	if (sme->crypto.n_akm_suites) {
		ret = rtw_cfg80211_set_key_mgt(psecuritypriv, sme->crypto.akm_suites[0]);
		if (ret < 0)
			goto cancel_ps_deny;
	}
#ifdef CONFIG_8011R
	else {
		/*It could be a connection without RSN IEs*/
		psecuritypriv->rsn_akm_suite_type = 0;
	}
#endif

#ifdef CONFIG_WAPI_SUPPORT
	if (sme->crypto.akm_suites[0] == WLAN_AKM_SUITE_WAPI_PSK)
		padapter->wapiInfo.bWapiPSK = true;
	else if (sme->crypto.akm_suites[0] == WLAN_AKM_SUITE_WAPI_CERT)
		padapter->wapiInfo.bWapiPSK = false;
#endif

	authmode = psecuritypriv->ndisauthtype;
	rtw_set_802_11_authentication_mode(padapter, authmode);

	/* rtw_set_802_11_encryption_mode(padapter, padapter->securitypriv.ndisencryptstatus); */

	if (rtw_set_802_11_connect(padapter, (u8 *)sme->bssid, &ndis_ssid) == _FALSE) {
		ret = -1;
		goto cancel_ps_deny;
	}


	_enter_critical_bh(&pwdev_priv->connect_req_lock, &irqL);

	if (pwdev_priv->connect_req) {
		rtw_wdev_free_connect_req(pwdev_priv);
		RTW_INFO(FUNC_NDEV_FMT" free existing connect_req\n", FUNC_NDEV_ARG(ndev));
	}

	pwdev_priv->connect_req = (struct cfg80211_connect_params *)rtw_malloc(sizeof(*pwdev_priv->connect_req));
	if (pwdev_priv->connect_req)
		_rtw_memcpy(pwdev_priv->connect_req, sme, sizeof(*pwdev_priv->connect_req));
	else
		RTW_WARN(FUNC_NDEV_FMT" alloc connect_req fail\n", FUNC_NDEV_ARG(ndev));

	_exit_critical_bh(&pwdev_priv->connect_req_lock, &irqL);

	RTW_INFO("set ssid:dot11AuthAlgrthm=%d, dot11PrivacyAlgrthm=%d, dot118021XGrpPrivacy=%d\n", psecuritypriv->dot11AuthAlgrthm, psecuritypriv->dot11PrivacyAlgrthm,
		psecuritypriv->dot118021XGrpPrivacy);

cancel_ps_deny:
	rtw_ps_deny_cancel(padapter, PS_DENY_JOIN);

exit:
	RTW_INFO("<=%s, ret %d\n", __FUNCTION__, ret);

	rtw_wdev_set_not_indic_disco(pwdev_priv, 0);

	return ret;
}

static int cfg80211_rtw_disconnect(struct wiphy *wiphy, struct net_device *ndev,
				   u16 reason_code)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);

	RTW_INFO(FUNC_NDEV_FMT" - Start to Disconnect\n", FUNC_NDEV_ARG(ndev));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	if (!wiphy->dev.power.is_prepared)
#endif
		rtw_wdev_set_not_indic_disco(pwdev_priv, 1);

	rtw_set_to_roam(padapter, 0);

	/* if(check_fwstate(&padapter->mlmepriv, _FW_LINKED)) */
	{
		rtw_scan_abort(padapter);
		LeaveAllPowerSaveMode(padapter);
		rtw_disassoc_cmd(padapter, 500, RTW_CMDF_WAIT_ACK);
#ifdef CONFIG_RTW_REPEATER_SON
		rtw_rson_do_disconnect(padapter);
#endif
		RTW_INFO("%s...call rtw_indicate_disconnect\n", __func__);

		rtw_free_assoc_resources(padapter, 1);
		rtw_indicate_disconnect(padapter, 0, wiphy->dev.power.is_prepared ? _FALSE : _TRUE);

		rtw_pwr_wakeup(padapter);
	}

	rtw_wdev_set_not_indic_disco(pwdev_priv, 0);

	RTW_INFO(FUNC_NDEV_FMT" return 0\n", FUNC_NDEV_ARG(ndev));
	return 0;
}

static int cfg80211_rtw_set_txpower(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	struct wireless_dev *wdev,
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)) || defined(COMPAT_KERNEL_RELEASE)
	enum nl80211_tx_power_setting type, int mbm)
#else
	enum tx_power_setting type, int dbm)
#endif
{
#if 0
	struct iwm_priv *iwm = wiphy_to_iwm(wiphy);
	int ret;

	switch (type) {
	case NL80211_TX_POWER_AUTOMATIC:
		return 0;
	case NL80211_TX_POWER_FIXED:
		if (mbm < 0 || (mbm % 100))
			return -EOPNOTSUPP;

		if (!test_bit(IWM_STATUS_READY, &iwm->status))
			return 0;

		ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
					      CFG_TX_PWR_LIMIT_USR,
					      MBM_TO_DBM(mbm) * 2);
		if (ret < 0)
			return ret;

		return iwm_tx_power_trigger(iwm);
	default:
		IWM_ERR(iwm, "Unsupported power type: %d\n", type);
		return -EOPNOTSUPP;
	}
#endif
	RTW_INFO("%s\n", __func__);
	return 0;
}

static int cfg80211_rtw_get_txpower(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	struct wireless_dev *wdev,
#endif
	int *dbm)
{
	RTW_INFO("%s\n", __func__);

	*dbm = (12);

	return 0;
}

inline bool rtw_cfg80211_pwr_mgmt(_adapter *adapter)
{
	struct rtw_wdev_priv *rtw_wdev_priv = adapter_wdev_data(adapter);
	return rtw_wdev_priv->power_mgmt;
}

static int cfg80211_rtw_set_power_mgmt(struct wiphy *wiphy,
				       struct net_device *ndev,
				       bool enabled, int timeout)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct rtw_wdev_priv *rtw_wdev_priv = adapter_wdev_data(padapter);

	RTW_INFO(FUNC_NDEV_FMT" enabled:%u, timeout:%d\n", FUNC_NDEV_ARG(ndev),
		enabled, timeout);

	rtw_wdev_priv->power_mgmt = enabled;

#ifdef CONFIG_LPS
	if (!enabled)
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_LEAVE_CFG80211_PWRMGMT, 1);
#endif

	return 0;
}

static int cfg80211_rtw_set_pmksa(struct wiphy *wiphy,
				  struct net_device *ndev,
				  struct cfg80211_pmksa *pmksa)
{
	u8	index, blInserted = _FALSE;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct mlme_priv *mlme = &padapter->mlmepriv;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	u8	strZeroMacAddress[ETH_ALEN] = { 0x00 };

	RTW_INFO(FUNC_NDEV_FMT" "MAC_FMT" "KEY_FMT"\n", FUNC_NDEV_ARG(ndev)
		, MAC_ARG(pmksa->bssid), KEY_ARG(pmksa->pmkid));

	if (_rtw_memcmp((u8 *)pmksa->bssid, strZeroMacAddress, ETH_ALEN) == _TRUE)
		return -EINVAL;

	if (check_fwstate(mlme, _FW_LINKED) == _FALSE) {
		RTW_INFO(FUNC_NDEV_FMT" not set pmksa cause not in linked state\n", FUNC_NDEV_ARG(ndev));
		return -EINVAL;
	}

	blInserted = _FALSE;

	/* overwrite PMKID */
	for (index = 0 ; index < NUM_PMKID_CACHE; index++) {
		if (_rtw_memcmp(psecuritypriv->PMKIDList[index].Bssid, (u8 *)pmksa->bssid, ETH_ALEN) == _TRUE) {
			/* BSSID is matched, the same AP => rewrite with new PMKID. */
			RTW_INFO(FUNC_NDEV_FMT" BSSID exists in the PMKList.\n", FUNC_NDEV_ARG(ndev));

			_rtw_memcpy(psecuritypriv->PMKIDList[index].PMKID, (u8 *)pmksa->pmkid, WLAN_PMKID_LEN);
			psecuritypriv->PMKIDList[index].bUsed = _TRUE;
			psecuritypriv->PMKIDIndex = index + 1;
			blInserted = _TRUE;
			break;
		}
	}

	if (!blInserted) {
		/* Find a new entry */
		RTW_INFO(FUNC_NDEV_FMT" Use the new entry index = %d for this PMKID.\n",
			FUNC_NDEV_ARG(ndev), psecuritypriv->PMKIDIndex);

		_rtw_memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].Bssid, (u8 *)pmksa->bssid, ETH_ALEN);
		_rtw_memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].PMKID, (u8 *)pmksa->pmkid, WLAN_PMKID_LEN);

		psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].bUsed = _TRUE;
		psecuritypriv->PMKIDIndex++ ;
		if (psecuritypriv->PMKIDIndex == 16)
			psecuritypriv->PMKIDIndex = 0;
	}

	return 0;
}

static int cfg80211_rtw_del_pmksa(struct wiphy *wiphy,
				  struct net_device *ndev,
				  struct cfg80211_pmksa *pmksa)
{
	u8	index, bMatched = _FALSE;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct security_priv	*psecuritypriv = &padapter->securitypriv;

	RTW_INFO(FUNC_NDEV_FMT" "MAC_FMT" "KEY_FMT"\n", FUNC_NDEV_ARG(ndev)
		, MAC_ARG(pmksa->bssid), KEY_ARG(pmksa->pmkid));

	for (index = 0 ; index < NUM_PMKID_CACHE; index++) {
		if (_rtw_memcmp(psecuritypriv->PMKIDList[index].Bssid, (u8 *)pmksa->bssid, ETH_ALEN) == _TRUE) {
			/* BSSID is matched, the same AP => Remove this PMKID information and reset it. */
			_rtw_memset(psecuritypriv->PMKIDList[index].Bssid, 0x00, ETH_ALEN);
			_rtw_memset(psecuritypriv->PMKIDList[index].PMKID, 0x00, WLAN_PMKID_LEN);
			psecuritypriv->PMKIDList[index].bUsed = _FALSE;
			bMatched = _TRUE;
			RTW_INFO(FUNC_NDEV_FMT" clear id:%hhu\n", FUNC_NDEV_ARG(ndev), index);
			break;
		}
	}

	if (_FALSE == bMatched) {
		RTW_INFO(FUNC_NDEV_FMT" do not have matched BSSID\n"
			, FUNC_NDEV_ARG(ndev));
		return -EINVAL;
	}

	return 0;
}

static int cfg80211_rtw_flush_pmksa(struct wiphy *wiphy,
				    struct net_device *ndev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct security_priv	*psecuritypriv = &padapter->securitypriv;

	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	_rtw_memset(&psecuritypriv->PMKIDList[0], 0x00, sizeof(RT_PMKID_LIST) * NUM_PMKID_CACHE);
	psecuritypriv->PMKIDIndex = 0;

	return 0;
}

#ifdef CONFIG_AP_MODE
void rtw_cfg80211_indicate_sta_assoc(_adapter *padapter, u8 *pmgmt_frame, uint frame_len)
{
	s32 freq;
	int channel;
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct net_device *ndev = padapter->pnetdev;

	RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

#if defined(RTW_USE_CFG80211_STA_EVENT) || defined(COMPAT_KERNEL_RELEASE)
	{
		struct station_info sinfo;
		u8 ie_offset;
		if (get_frame_sub_type(pmgmt_frame) == WIFI_ASSOCREQ)
			ie_offset = _ASOCREQ_IE_OFFSET_;
		else /* WIFI_REASSOCREQ */
			ie_offset = _REASOCREQ_IE_OFFSET_;

		memset(&sinfo, 0, sizeof(sinfo));
		sinfo.filled = STATION_INFO_ASSOC_REQ_IES;
		sinfo.assoc_req_ies = pmgmt_frame + WLAN_HDR_A3_LEN + ie_offset;
		sinfo.assoc_req_ies_len = frame_len - WLAN_HDR_A3_LEN - ie_offset;
		cfg80211_new_sta(ndev, get_addr2_ptr(pmgmt_frame), &sinfo, GFP_ATOMIC);
	}
#else /* defined(RTW_USE_CFG80211_STA_EVENT) */
	channel = pmlmeext->cur_channel;
	freq = rtw_ch2freq(channel);

	#ifdef COMPAT_KERNEL_RELEASE
	rtw_cfg80211_rx_mgmt(pwdev, freq, 0, pmgmt_frame, frame_len, GFP_ATOMIC);
	#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) && !defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER)
	rtw_cfg80211_rx_mgmt(pwdev, freq, 0, pmgmt_frame, frame_len, GFP_ATOMIC);
	#else /* COMPAT_KERNEL_RELEASE */
	{
		/* to avoid WARN_ON(wdev->iftype != NL80211_IFTYPE_STATION)  when calling cfg80211_send_rx_assoc() */
		#ifndef CONFIG_PLATFORM_MSTAR
		pwdev->iftype = NL80211_IFTYPE_STATION;
		#endif /* CONFIG_PLATFORM_MSTAR */
		RTW_INFO("iftype=%d before call cfg80211_send_rx_assoc()\n", pwdev->iftype);
		rtw_cfg80211_send_rx_assoc(padapter, NULL, pmgmt_frame, frame_len);
		RTW_INFO("iftype=%d after call cfg80211_send_rx_assoc()\n", pwdev->iftype);
		pwdev->iftype = NL80211_IFTYPE_AP;
		/* cfg80211_rx_action(padapter->pnetdev, freq, pmgmt_frame, frame_len, GFP_ATOMIC); */
	}
	#endif /* COMPAT_KERNEL_RELEASE */
#endif /* defined(RTW_USE_CFG80211_STA_EVENT) */

}

void rtw_cfg80211_indicate_sta_disassoc(_adapter *padapter, unsigned char *da, unsigned short reason)
{
	s32 freq;
	int channel;
	u8 *pmgmt_frame;
	uint frame_len;
	struct rtw_ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	u8 mgmt_buf[128] = {0};
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wireless_dev *wdev = padapter->rtw_wdev;
	struct net_device *ndev = padapter->pnetdev;

	RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

#if defined(RTW_USE_CFG80211_STA_EVENT) || defined(COMPAT_KERNEL_RELEASE)
	cfg80211_del_sta(ndev, da, GFP_ATOMIC);
#else /* defined(RTW_USE_CFG80211_STA_EVENT) */
	channel = pmlmeext->cur_channel;
	freq = rtw_ch2freq(channel);

	pmgmt_frame = mgmt_buf;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pmgmt_frame;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pmgmt_frame, WIFI_DEAUTH);

	pmgmt_frame += sizeof(struct rtw_ieee80211_hdr_3addr);
	frame_len = sizeof(struct rtw_ieee80211_hdr_3addr);

	reason = cpu_to_le16(reason);
	pmgmt_frame = rtw_set_fixed_ie(pmgmt_frame, _RSON_CODE_ , (unsigned char *)&reason, &frame_len);

	#ifdef COMPAT_KERNEL_RELEASE
	rtw_cfg80211_rx_mgmt(wdev, freq, 0, mgmt_buf, frame_len, GFP_ATOMIC);
	#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) && !defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER)
	rtw_cfg80211_rx_mgmt(wdev, freq, 0, mgmt_buf, frame_len, GFP_ATOMIC);
	#else /* COMPAT_KERNEL_RELEASE */
	cfg80211_send_disassoc(padapter->pnetdev, mgmt_buf, frame_len);
	/* cfg80211_rx_action(padapter->pnetdev, freq, mgmt_buf, frame_len, GFP_ATOMIC); */
	#endif /* COMPAT_KERNEL_RELEASE */
#endif /* defined(RTW_USE_CFG80211_STA_EVENT) */
}

static int rtw_cfg80211_monitor_if_open(struct net_device *ndev)
{
	int ret = 0;

	RTW_INFO("%s\n", __func__);

	return ret;
}

static int rtw_cfg80211_monitor_if_close(struct net_device *ndev)
{
	int ret = 0;

	RTW_INFO("%s\n", __func__);

	return ret;
}

static int rtw_cfg80211_monitor_if_xmit_entry(struct sk_buff *skb, struct net_device *ndev)
{
	int ret = 0;
	int rtap_len;
	int qos_len = 0;
	int dot11_hdr_len = 24;
	int snap_len = 6;
	unsigned char *pdata;
	u16 frame_ctl;
	unsigned char src_mac_addr[6];
	unsigned char dst_mac_addr[6];
	struct rtw_ieee80211_hdr *dot11_hdr;
	struct ieee80211_radiotap_header *rtap_hdr;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);

	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	if (skb)
		rtw_mstat_update(MSTAT_TYPE_SKB, MSTAT_ALLOC_SUCCESS, skb->truesize);

	if (unlikely(skb->len < sizeof(struct ieee80211_radiotap_header)))
		goto fail;

	rtap_hdr = (struct ieee80211_radiotap_header *)skb->data;
	if (unlikely(rtap_hdr->it_version))
		goto fail;

	rtap_len = ieee80211_get_radiotap_len(skb->data);
	if (unlikely(skb->len < rtap_len))
		goto fail;

	if (rtap_len != 14) {
		RTW_INFO("radiotap len (should be 14): %d\n", rtap_len);
		goto fail;
	}

	/* Skip the ratio tap header */
	skb_pull(skb, rtap_len);

	dot11_hdr = (struct rtw_ieee80211_hdr *)skb->data;
	frame_ctl = le16_to_cpu(dot11_hdr->frame_ctl);
	/* Check if the QoS bit is set */
	if ((frame_ctl & RTW_IEEE80211_FCTL_FTYPE) == RTW_IEEE80211_FTYPE_DATA) {
		/* Check if this ia a Wireless Distribution System (WDS) frame
		 * which has 4 MAC addresses
		 */
		if (dot11_hdr->frame_ctl & 0x0080)
			qos_len = 2;
		if ((dot11_hdr->frame_ctl & 0x0300) == 0x0300)
			dot11_hdr_len += 6;

		memcpy(dst_mac_addr, dot11_hdr->addr1, sizeof(dst_mac_addr));
		memcpy(src_mac_addr, dot11_hdr->addr2, sizeof(src_mac_addr));

		/* Skip the 802.11 header, QoS (if any) and SNAP, but leave spaces for
		 * for two MAC addresses
		 */
		skb_pull(skb, dot11_hdr_len + qos_len + snap_len - sizeof(src_mac_addr) * 2);
		pdata = (unsigned char *)skb->data;
		memcpy(pdata, dst_mac_addr, sizeof(dst_mac_addr));
		memcpy(pdata + sizeof(dst_mac_addr), src_mac_addr, sizeof(src_mac_addr));

		RTW_INFO("should be eapol packet\n");

		/* Use the real net device to transmit the packet */
		ret = _rtw_xmit_entry(skb, padapter->pnetdev);

		return ret;

	} else if ((frame_ctl & (RTW_IEEE80211_FCTL_FTYPE | RTW_IEEE80211_FCTL_STYPE))
		== (RTW_IEEE80211_FTYPE_MGMT | RTW_IEEE80211_STYPE_ACTION)
	) {
		/* only for action frames */
		struct xmit_frame		*pmgntframe;
		struct pkt_attrib	*pattrib;
		unsigned char	*pframe;
		/* u8 category, action, OUI_Subtype, dialogToken=0; */
		/* unsigned char	*frame_body; */
		struct rtw_ieee80211_hdr *pwlanhdr;
		struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
		struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
		u8 *buf = skb->data;
		u32 len = skb->len;
		u8 category, action;
		int type = -1;

		if (rtw_action_frame_parse(buf, len, &category, &action) == _FALSE) {
			RTW_INFO(FUNC_NDEV_FMT" frame_control:0x%x\n", FUNC_NDEV_ARG(ndev),
				le16_to_cpu(((struct rtw_ieee80211_hdr_3addr *)buf)->frame_ctl));
			goto fail;
		}

		RTW_INFO("RTW_Tx:da="MAC_FMT" via "FUNC_NDEV_FMT"\n",
			MAC_ARG(GetAddr1Ptr(buf)), FUNC_NDEV_ARG(ndev));
		#ifdef CONFIG_P2P
		type = rtw_p2p_check_frames(padapter, buf, len, _TRUE);
		if (type >= 0)
			goto dump;
		#endif
		if (category == RTW_WLAN_CATEGORY_PUBLIC)
			RTW_INFO("RTW_Tx:%s\n", action_public_str(action));
		else
			RTW_INFO("RTW_Tx:category(%u), action(%u)\n", category, action);

dump:
		/* starting alloc mgmt frame to dump it */
		pmgntframe = alloc_mgtxmitframe(pxmitpriv);
		if (pmgntframe == NULL)
			goto fail;

		/* update attribute */
		pattrib = &pmgntframe->attrib;
		update_mgntframe_attrib(padapter, pattrib);
		pattrib->retry_ctrl = _FALSE;

		_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

		pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

		_rtw_memcpy(pframe, (void *)buf, len);
		pattrib->pktlen = len;

#ifdef CONFIG_P2P
		if (type >= 0)
			rtw_xframe_chk_wfd_ie(pmgntframe);
#endif /* CONFIG_P2P */

		pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;
		/* update seq number */
		pmlmeext->mgnt_seq = GetSequence(pwlanhdr);
		pattrib->seqnum = pmlmeext->mgnt_seq;
		pmlmeext->mgnt_seq++;


		pattrib->last_txcmdsz = pattrib->pktlen;

		dump_mgntframe(padapter, pmgntframe);

	} else
		RTW_INFO("frame_ctl=0x%x\n", frame_ctl & (RTW_IEEE80211_FCTL_FTYPE | RTW_IEEE80211_FCTL_STYPE));


fail:

	rtw_skb_free(skb);

	return 0;

}

static void rtw_cfg80211_monitor_if_set_multicast_list(struct net_device *ndev)
{
	RTW_INFO("%s\n", __func__);
}

static int rtw_cfg80211_monitor_if_set_mac_address(struct net_device *ndev, void *addr)
{
	int ret = 0;

	RTW_INFO("%s\n", __func__);

	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
static const struct net_device_ops rtw_cfg80211_monitor_if_ops = {
	.ndo_open = rtw_cfg80211_monitor_if_open,
	.ndo_stop = rtw_cfg80211_monitor_if_close,
	.ndo_start_xmit = rtw_cfg80211_monitor_if_xmit_entry,
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0))
	.ndo_set_multicast_list = rtw_cfg80211_monitor_if_set_multicast_list,
	#endif
	.ndo_set_mac_address = rtw_cfg80211_monitor_if_set_mac_address,
};
#endif

static int rtw_cfg80211_add_monitor_if(_adapter *padapter, char *name, struct net_device **ndev)
{
	int ret = 0;
	struct net_device *mon_ndev = NULL;
	struct wireless_dev *mon_wdev = NULL;
	struct rtw_netdev_priv_indicator *pnpi;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);

	if (!name) {
		RTW_INFO(FUNC_ADPT_FMT" without specific name\n", FUNC_ADPT_ARG(padapter));
		ret = -EINVAL;
		goto out;
	}

	if (pwdev_priv->pmon_ndev) {
		RTW_INFO(FUNC_ADPT_FMT" monitor interface exist: "NDEV_FMT"\n",
			FUNC_ADPT_ARG(padapter), NDEV_ARG(pwdev_priv->pmon_ndev));
		ret = -EBUSY;
		goto out;
	}

	mon_ndev = alloc_etherdev(sizeof(struct rtw_netdev_priv_indicator));
	if (!mon_ndev) {
		RTW_INFO(FUNC_ADPT_FMT" allocate ndev fail\n", FUNC_ADPT_ARG(padapter));
		ret = -ENOMEM;
		goto out;
	}

	mon_ndev->type = ARPHRD_IEEE80211_RADIOTAP;
	strncpy(mon_ndev->name, name, IFNAMSIZ);
	mon_ndev->name[IFNAMSIZ - 1] = 0;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 11, 8))
	mon_ndev->priv_destructor = rtw_ndev_destructor;
#else
	mon_ndev->destructor = rtw_ndev_destructor;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
	mon_ndev->netdev_ops = &rtw_cfg80211_monitor_if_ops;
#else
	mon_ndev->open = rtw_cfg80211_monitor_if_open;
	mon_ndev->stop = rtw_cfg80211_monitor_if_close;
	mon_ndev->hard_start_xmit = rtw_cfg80211_monitor_if_xmit_entry;
	mon_ndev->set_mac_address = rtw_cfg80211_monitor_if_set_mac_address;
#endif

	pnpi = netdev_priv(mon_ndev);
	pnpi->priv = padapter;
	pnpi->sizeof_priv = sizeof(_adapter);

	/*  wdev */
	mon_wdev = (struct wireless_dev *)rtw_zmalloc(sizeof(struct wireless_dev));
	if (!mon_wdev) {
		RTW_INFO(FUNC_ADPT_FMT" allocate mon_wdev fail\n", FUNC_ADPT_ARG(padapter));
		ret = -ENOMEM;
		goto out;
	}

	mon_wdev->wiphy = padapter->rtw_wdev->wiphy;
	mon_wdev->netdev = mon_ndev;
	mon_wdev->iftype = NL80211_IFTYPE_MONITOR;
	mon_ndev->ieee80211_ptr = mon_wdev;

	ret = register_netdevice(mon_ndev);
	if (ret)
		goto out;

	*ndev = pwdev_priv->pmon_ndev = mon_ndev;
	_rtw_memcpy(pwdev_priv->ifname_mon, name, IFNAMSIZ + 1);

out:
	if (ret && mon_wdev) {
		rtw_mfree((u8 *)mon_wdev, sizeof(struct wireless_dev));
		mon_wdev = NULL;
	}

	if (ret && mon_ndev) {
		free_netdev(mon_ndev);
		*ndev = mon_ndev = NULL;
	}

	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
static struct wireless_dev *
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)) || defined(COMPAT_KERNEL_RELEASE)
static struct net_device *
#else
static int
#endif
	cfg80211_rtw_add_virtual_intf(
		struct wiphy *wiphy,
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
		const char *name,
		#else
		char *name,
		#endif
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
		unsigned char name_assign_type,
		#endif
		enum nl80211_iftype type,
		#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
		u32 *flags,
		#endif
		struct vif_params *params)
{
	int ret = 0;
	struct wireless_dev *wdev = NULL;
	struct net_device *ndev = NULL;
	_adapter *padapter;
	struct dvobj_priv *dvobj = wiphy_to_dvobj(wiphy);

	rtw_set_rtnl_lock_holder(dvobj, current);

	RTW_INFO(FUNC_WIPHY_FMT" name:%s, type:%d\n", FUNC_WIPHY_ARG(wiphy), name, type);

	switch (type) {
	case NL80211_IFTYPE_MONITOR:
		padapter = wiphy_to_adapter(wiphy); /* TODO: get ap iface ? */
		ret = rtw_cfg80211_add_monitor_if(padapter, (char *)name, &ndev);
		if (ret == 0)
			wdev = ndev->ieee80211_ptr;
		break;

#if defined(CONFIG_P2P) && ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE))
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_P2P_GO:
#endif
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_AP:
		padapter = dvobj_get_unregisterd_adapter(dvobj);
		if (!padapter) {
			RTW_WARN("adapter pool empty!\n");
			ret = -ENODEV;
			break;
		}
		if (rtw_os_ndev_init(padapter, name) != _SUCCESS) {
			RTW_WARN("ndev init fail!\n");
			ret = -ENODEV;
			break;
		}
		#if defined(CONFIG_P2P) && ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE))
		if (type == NL80211_IFTYPE_P2P_CLIENT || type == NL80211_IFTYPE_P2P_GO)
			rtw_p2p_enable(padapter, P2P_ROLE_DEVICE);
		#endif
		ndev = padapter->pnetdev;
		wdev = ndev->ieee80211_ptr;
		break;

#if defined(CONFIG_P2P) && defined(RTW_DEDICATED_P2P_DEVICE)
	case NL80211_IFTYPE_P2P_DEVICE:
		ret = rtw_pd_iface_alloc(wiphy, name, &wdev);
		break;
#endif

	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_MESH_POINT:
	default:
		ret = -ENODEV;
		RTW_INFO("Unsupported interface type\n");
		break;
	}

	if (ndev)
		RTW_INFO(FUNC_WIPHY_FMT" ndev:%p, ret:%d\n", FUNC_WIPHY_ARG(wiphy), ndev, ret);
	else
		RTW_INFO(FUNC_WIPHY_FMT" wdev:%p, ret:%d\n", FUNC_WIPHY_ARG(wiphy), wdev, ret);

	rtw_set_rtnl_lock_holder(dvobj, NULL);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	return wdev ? wdev : ERR_PTR(ret);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)) || defined(COMPAT_KERNEL_RELEASE)
	return ndev ? ndev : ERR_PTR(ret);
#else
	return ret;
#endif
}

static int cfg80211_rtw_del_virtual_intf(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	struct wireless_dev *wdev
#else
	struct net_device *ndev
#endif
)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	struct net_device *ndev = wdev_to_ndev(wdev);
#endif
	int ret = 0;
	struct dvobj_priv *dvobj = wiphy_to_dvobj(wiphy);
	_adapter *adapter;
	struct rtw_wdev_priv *pwdev_priv;

	rtw_set_rtnl_lock_holder(dvobj, current);

	if (ndev) {
		adapter = (_adapter *)rtw_netdev_priv(ndev);
		pwdev_priv = adapter_wdev_data(adapter);

		if (ndev == pwdev_priv->pmon_ndev) {
			unregister_netdevice(ndev);
			pwdev_priv->pmon_ndev = NULL;
			pwdev_priv->ifname_mon[0] = '\0';
			RTW_INFO(FUNC_NDEV_FMT" remove monitor ndev\n", FUNC_NDEV_ARG(ndev));
		} else {
			RTW_INFO(FUNC_NDEV_FMT" unregister ndev\n", FUNC_NDEV_ARG(ndev));
			rtw_os_ndev_unregister(adapter);
		}
	} else
#if defined(CONFIG_P2P) && defined(RTW_DEDICATED_P2P_DEVICE)
	if (wdev->iftype == NL80211_IFTYPE_P2P_DEVICE) {
		if (wdev == wiphy_to_pd_wdev(wiphy))
			rtw_pd_iface_free(wiphy);
		else {
			RTW_ERR(FUNC_WIPHY_FMT" unknown P2P Device wdev:%p\n", FUNC_WIPHY_ARG(wiphy), wdev);
			rtw_warn_on(1);
		}
	} else
#endif
	{
		ret = -EINVAL;
		goto exit;
	}

exit:
	rtw_set_rtnl_lock_holder(dvobj, NULL);
	return ret;
}

static int rtw_add_beacon(_adapter *adapter, const u8 *head, size_t head_len, const u8 *tail, size_t tail_len)
{
	int ret = 0;
	u8 *pbuf = NULL;
	uint len, wps_ielen = 0;
	uint p2p_ielen = 0;
	u8 *p2p_ie;
	u8 got_p2p_ie = _FALSE;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	/* struct sta_priv *pstapriv = &padapter->stapriv; */


	RTW_INFO("%s beacon_head_len=%zu, beacon_tail_len=%zu\n", __FUNCTION__, head_len, tail_len);


	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != _TRUE)
		return -EINVAL;

	if (head_len < 24)
		return -EINVAL;


	pbuf = rtw_zmalloc(head_len + tail_len);
	if (!pbuf)
		return -ENOMEM;


	/* _rtw_memcpy(&pstapriv->max_num_sta, param->u.bcn_ie.reserved, 2); */

	/* if((pstapriv->max_num_sta>NUM_STA) || (pstapriv->max_num_sta<=0)) */
	/*	pstapriv->max_num_sta = NUM_STA; */


	_rtw_memcpy(pbuf, (void *)head + 24, head_len - 24); /* 24=beacon header len. */
	_rtw_memcpy(pbuf + head_len - 24, (void *)tail, tail_len);

	len = head_len + tail_len - 24;

	/* check wps ie if inclued */
	if (rtw_get_wps_ie(pbuf + _FIXED_IE_LENGTH_, len - _FIXED_IE_LENGTH_, NULL, &wps_ielen))
		RTW_INFO("add bcn, wps_ielen=%d\n", wps_ielen);

#ifdef CONFIG_P2P
	if (adapter->wdinfo.driver_interface == DRIVER_CFG80211) {
		/* check p2p if enable */
		if (rtw_get_p2p_ie(pbuf + _FIXED_IE_LENGTH_, len - _FIXED_IE_LENGTH_, NULL, &p2p_ielen)) {
			struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
			struct wifidirect_info *pwdinfo = &(adapter->wdinfo);

			RTW_INFO("got p2p_ie, len=%d\n", p2p_ielen);

			got_p2p_ie = _TRUE;

			if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
				RTW_INFO("Enable P2P function for the first time\n");
				rtw_p2p_enable(adapter, P2P_ROLE_GO);

				adapter->stapriv.expire_to = 3; /* 3x2 = 6 sec in p2p mode */
			} else {
				RTW_INFO("enter GO Mode, p2p_ielen=%d\n", p2p_ielen);

				rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
				rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
				pwdinfo->intent = 15;
			}
		}
	}
#endif /* CONFIG_P2P */

	/* pbss_network->IEs will not include p2p_ie, wfd ie */
	rtw_ies_remove_ie(pbuf, &len, _BEACON_IE_OFFSET_, _VENDOR_SPECIFIC_IE_, P2P_OUI, 4);
	rtw_ies_remove_ie(pbuf, &len, _BEACON_IE_OFFSET_, _VENDOR_SPECIFIC_IE_, WFD_OUI, 4);

	if (rtw_check_beacon_data(adapter, pbuf,  len) == _SUCCESS) {
#ifdef CONFIG_P2P
		/* check p2p if enable */
		if (got_p2p_ie == _TRUE) {
			struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
			struct wifidirect_info *pwdinfo = &(adapter->wdinfo);
			pwdinfo->operating_channel = pmlmeext->cur_channel;
		}
#endif /* CONFIG_P2P */
		ret = 0;
	} else
		ret = -EINVAL;


	rtw_mfree(pbuf, head_len + tail_len);

	return ret;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)) && !defined(COMPAT_KERNEL_RELEASE)
static int cfg80211_rtw_add_beacon(struct wiphy *wiphy, struct net_device *ndev,
		struct beacon_parameters *info)
{
	int ret = 0;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(ndev);

	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
	ret = rtw_add_beacon(adapter, info->head, info->head_len, info->tail, info->tail_len);

	return ret;
}

static int cfg80211_rtw_set_beacon(struct wiphy *wiphy, struct net_device *ndev,
		struct beacon_parameters *info)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(ndev);
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);

	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	pmlmeext->bstart_bss = _TRUE;

	cfg80211_rtw_add_beacon(wiphy, ndev, info);

	return 0;
}

static int	cfg80211_rtw_del_beacon(struct wiphy *wiphy, struct net_device *ndev)
{
	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	return 0;
}
#else
static int cfg80211_rtw_start_ap(struct wiphy *wiphy, struct net_device *ndev,
		struct cfg80211_ap_settings *settings)
{
	int ret = 0;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(ndev);

	RTW_INFO(FUNC_NDEV_FMT" hidden_ssid:%d, auth_type:%d\n", FUNC_NDEV_ARG(ndev),
		settings->hidden_ssid, settings->auth_type);

	ret = rtw_add_beacon(adapter, settings->beacon.head, settings->beacon.head_len,
		settings->beacon.tail, settings->beacon.tail_len);

	adapter->mlmeextpriv.mlmext_info.hidden_ssid_mode = settings->hidden_ssid;

	if (settings->ssid && settings->ssid_len) {
		WLAN_BSSID_EX *pbss_network = &adapter->mlmepriv.cur_network.network;
		WLAN_BSSID_EX *pbss_network_ext = &adapter->mlmeextpriv.mlmext_info.network;

		if (0)
			RTW_INFO(FUNC_ADPT_FMT" ssid:(%s,%zu), from ie:(%s,%d)\n", FUNC_ADPT_ARG(adapter),
				settings->ssid, settings->ssid_len,
				pbss_network->Ssid.Ssid, pbss_network->Ssid.SsidLength);

		_rtw_memcpy(pbss_network->Ssid.Ssid, (void *)settings->ssid, settings->ssid_len);
		pbss_network->Ssid.SsidLength = settings->ssid_len;
		_rtw_memcpy(pbss_network_ext->Ssid.Ssid, (void *)settings->ssid, settings->ssid_len);
		pbss_network_ext->Ssid.SsidLength = settings->ssid_len;

		if (0)
			RTW_INFO(FUNC_ADPT_FMT" after ssid:(%s,%d), (%s,%d)\n", FUNC_ADPT_ARG(adapter),
				pbss_network->Ssid.Ssid, pbss_network->Ssid.SsidLength,
				pbss_network_ext->Ssid.Ssid, pbss_network_ext->Ssid.SsidLength);
	}

	return ret;
}

static int cfg80211_rtw_change_beacon(struct wiphy *wiphy, struct net_device *ndev,
		struct cfg80211_beacon_data *info)
{
	int ret = 0;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(ndev);

	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	ret = rtw_add_beacon(adapter, info->head, info->head_len, info->tail, info->tail_len);

	return ret;
}

static int cfg80211_rtw_stop_ap(struct wiphy *wiphy, struct net_device *ndev)
{
	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
	return 0;
}

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)) */

#if CONFIG_RTW_MACADDR_ACL && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
static int cfg80211_rtw_set_mac_acl(struct wiphy *wiphy, struct net_device *ndev,
		const struct cfg80211_acl_data *params)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(ndev);
	u8 acl_mode = RTW_ACL_MODE_DISABLED;
	int ret = -1;
	int i;

	if (!params) {
		RTW_WARN(FUNC_ADPT_FMT" params NULL\n", FUNC_ADPT_ARG(adapter));
		goto exit;
	}

	RTW_INFO(FUNC_ADPT_FMT" acl_policy:%d, entry_num:%d\n"
		, FUNC_ADPT_ARG(adapter), params->acl_policy, params->n_acl_entries);

	if (params->acl_policy == NL80211_ACL_POLICY_ACCEPT_UNLESS_LISTED)
		acl_mode = RTW_ACL_MODE_ACCEPT_UNLESS_LISTED;
	else if (params->acl_policy == NL80211_ACL_POLICY_DENY_UNLESS_LISTED)
		acl_mode = RTW_ACL_MODE_DENY_UNLESS_LISTED;

	if (!params->n_acl_entries) {
		if (acl_mode != RTW_ACL_MODE_DISABLED)
			RTW_WARN(FUNC_ADPT_FMT" acl_policy:%d with no entry\n"
				, FUNC_ADPT_ARG(adapter), params->acl_policy);
		acl_mode = RTW_ACL_MODE_DISABLED;
		goto exit;
	}

	for (i = 0; i < params->n_acl_entries; i++)
		rtw_acl_add_sta(adapter, params->mac_addrs[i].addr);

	ret = 0;

exit:
	rtw_set_macaddr_acl(adapter, acl_mode);
	return ret;
}
#endif /* CONFIG_RTW_MACADDR_ACL && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)) */

static int	cfg80211_rtw_add_station(struct wiphy *wiphy, struct net_device *ndev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0))
	u8 *mac,
#else
	const u8 *mac,
#endif
	struct station_parameters *params)
{
	int ret = 0;
#ifdef CONFIG_TDLS
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;
#endif /* CONFIG_TDLS */
	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

#ifdef CONFIG_TDLS
	psta = rtw_get_stainfo(pstapriv, (u8 *)mac);
	if (psta == NULL) {
		psta = rtw_alloc_stainfo(pstapriv, (u8 *)mac);
		if (psta == NULL) {
			RTW_INFO("[%s] Alloc station for "MAC_FMT" fail\n", __FUNCTION__, MAC_ARG(mac));
			ret = -EOPNOTSUPP;
			goto exit;
		}
	}
#endif /* CONFIG_TDLS */

exit:
	return ret;
}

static int	cfg80211_rtw_del_station(struct wiphy *wiphy, struct net_device *ndev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0))
	u8 *mac
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0))
	const u8 *mac
#else
	struct station_del_parameters *params
#endif
)
{
	int ret = 0;
	_irqL irqL;
	_list	*phead, *plist;
	u8 updated = _FALSE;
	const u8 *target_mac;
	struct sta_info *psta = NULL;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	RTW_INFO("+"FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));


#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0))
	target_mac = mac;
#else
	target_mac = params->mac;
#endif

	if (check_fwstate(pmlmepriv, (_FW_LINKED | WIFI_AP_STATE | WIFI_MESH_STATE)) != _TRUE) {
		RTW_INFO("%s, fw_state != FW_LINKED|WIFI_AP_STATE|WIFI_MESH_STATE\n", __func__);
		return -EINVAL;
	}


	if (!target_mac) {
		RTW_INFO("flush all sta, and cam_entry\n");

		flush_all_cam_entry(padapter);	/* clear CAM */

#ifdef CONFIG_AP_MODE
		ret = rtw_sta_flush(padapter, _TRUE);
#endif
		return ret;
	}


	RTW_INFO("free sta macaddr =" MAC_FMT "\n", MAC_ARG(target_mac));

	if (target_mac[0] == 0xff && target_mac[1] == 0xff &&
	    target_mac[2] == 0xff && target_mac[3] == 0xff &&
	    target_mac[4] == 0xff && target_mac[5] == 0xff)
		return -EINVAL;


	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	/* check asoc_queue */
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);

		plist = get_next(plist);

		if (_rtw_memcmp((u8 *)target_mac, psta->cmn.mac_addr, ETH_ALEN)) {
			if (psta->dot8021xalg == 1 && psta->bpairwise_key_installed == _FALSE)
				RTW_INFO("%s, sta's dot8021xalg = 1 and key_installed = _FALSE\n", __func__);
			else {
				RTW_INFO("free psta=%p, aid=%d\n", psta, psta->cmn.aid);

				rtw_list_delete(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;

				/* _exit_critical_bh(&pstapriv->asoc_list_lock, &irqL); */
				if (MLME_IS_AP(padapter))
					updated = ap_free_sta(padapter, psta, _TRUE, WLAN_REASON_PREV_AUTH_NOT_VALID, _TRUE);
				else
					updated = ap_free_sta(padapter, psta, _TRUE, WLAN_REASON_DEAUTH_LEAVING, _TRUE);
				/* _enter_critical_bh(&pstapriv->asoc_list_lock, &irqL); */

				psta = NULL;

				break;
			}

		}

	}

	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	associated_clients_update(padapter, updated, STA_INFO_UPDATE_ALL);

	RTW_INFO("-"FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	return ret;

}

static int	cfg80211_rtw_change_station(struct wiphy *wiphy, struct net_device *ndev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0))
	u8 *mac,
#else
	const u8 *mac,
#endif
	struct station_parameters *params)
{
	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	return 0;
}

struct sta_info *rtw_sta_info_get_by_idx(const int idx, struct sta_priv *pstapriv)

{

	_list	*phead, *plist;
	struct sta_info *psta = NULL;
	int i = 0;

	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	/* check asoc_queue */
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		if (idx == i)
			psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		plist = get_next(plist);
		i++;
	}
	return psta;
}

static int	cfg80211_rtw_dump_station(struct wiphy *wiphy, struct net_device *ndev,
		int idx, u8 *mac, struct station_info *sinfo)
{

	int ret = 0;
	_irqL irqL;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	psta = rtw_sta_info_get_by_idx(idx, pstapriv);
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	if (NULL == psta) {
		RTW_INFO("Station is not found\n");
		ret = -ENOENT;
		goto exit;
	}
	_rtw_memcpy(mac, psta->cmn.mac_addr, ETH_ALEN);
	sinfo->filled = 0;
	sinfo->filled |= STATION_INFO_SIGNAL;
	sinfo->signal = translate_percentage_to_dbm(psta->cmn.rssi_stat.rssi);

exit:
	return ret;
}

static int	cfg80211_rtw_change_bss(struct wiphy *wiphy, struct net_device *ndev,
		struct bss_parameters *params)
{
	u8 i;

	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
/*
	RTW_INFO("use_cts_prot=%d\n", params->use_cts_prot);
	RTW_INFO("use_short_preamble=%d\n", params->use_short_preamble);
	RTW_INFO("use_short_slot_time=%d\n", params->use_short_slot_time);
	RTW_INFO("ap_isolate=%d\n", params->ap_isolate);

	RTW_INFO("basic_rates_len=%d\n", params->basic_rates_len);
	for(i = 0; i < params->basic_rates_len; i++)
		RTW_INFO("basic_rates=%d\n", params->basic_rates[i]);
*/
	return 0;

}

static int	cfg80211_rtw_set_channel(struct wiphy *wiphy
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	, struct net_device *ndev
	#endif
	, struct ieee80211_channel *chan, enum nl80211_channel_type channel_type)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
#else
	_adapter *padapter = wiphy_to_adapter(wiphy);
#endif
	int chan_target = (u8) ieee80211_frequency_to_channel(chan->center_freq);
	int chan_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	int chan_width = CHANNEL_WIDTH_20;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
#endif

	switch (channel_type) {
	case NL80211_CHAN_NO_HT:
	case NL80211_CHAN_HT20:
		chan_width = CHANNEL_WIDTH_20;
		chan_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		break;
	case NL80211_CHAN_HT40MINUS:
		chan_width = CHANNEL_WIDTH_40;
		chan_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
		break;
	case NL80211_CHAN_HT40PLUS:
		chan_width = CHANNEL_WIDTH_40;
		chan_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
		break;
	default:
		chan_width = CHANNEL_WIDTH_20;
		chan_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		break;
	}

	RTW_INFO(FUNC_ADPT_FMT" ch:%d bw:%d, offset:%d\n"
		, FUNC_ADPT_ARG(padapter), chan_target, chan_width, chan_offset);

	rtw_set_chbw_cmd(padapter, chan_target, chan_width, chan_offset, RTW_CMDF_WAIT_ACK);

	return 0;
}

static int cfg80211_rtw_set_monitor_channel(struct wiphy *wiphy
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	, struct cfg80211_chan_def *chandef
#else
	, struct ieee80211_channel *chan
	, enum nl80211_channel_type channel_type
#endif
)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	struct ieee80211_channel *chan = chandef->chan;
#endif

	_adapter *padapter = wiphy_to_adapter(wiphy);
	int target_channal = chan->hw_value;
	int target_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	int target_width = CHANNEL_WIDTH_20;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
#ifdef CONFIG_DEBUG_CFG80211
	RTW_INFO("center_freq %u Mhz ch %u width %u freq1 %u freq2 %u\n"
		, chan->center_freq
		, chan->hw_value
		, chandef->width
		, chandef->center_freq1
		, chandef->center_freq2);
#endif /* CONFIG_DEBUG_CFG80211 */

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		target_width = CHANNEL_WIDTH_20;
		target_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		break;
	case NL80211_CHAN_WIDTH_40:
		target_width = CHANNEL_WIDTH_40;
		if (chandef->center_freq1 > chan->center_freq)
			target_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
		else
			target_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
		break;
	case NL80211_CHAN_WIDTH_80:
		target_width = CHANNEL_WIDTH_80;
		target_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		break;
	case NL80211_CHAN_WIDTH_80P80:
		target_width = CHANNEL_WIDTH_80_80;
		target_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		break;
	case NL80211_CHAN_WIDTH_160:
		target_width = CHANNEL_WIDTH_160;
		target_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		break;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	case NL80211_CHAN_WIDTH_5:
	case NL80211_CHAN_WIDTH_10:
#endif
	default:
		target_width = CHANNEL_WIDTH_20;
		target_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		break;
	}
#else
#ifdef CONFIG_DEBUG_CFG80211
	RTW_INFO("center_freq %u Mhz ch %u channel_type %u\n"
		, chan->center_freq
		, chan->hw_value
		, channel_type);
#endif /* CONFIG_DEBUG_CFG80211 */

	switch (channel_type) {
	case NL80211_CHAN_NO_HT:
	case NL80211_CHAN_HT20:
		target_width = CHANNEL_WIDTH_20;
		target_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		break;
	case NL80211_CHAN_HT40MINUS:
		target_width = CHANNEL_WIDTH_40;
		target_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
		break;
	case NL80211_CHAN_HT40PLUS:
		target_width = CHANNEL_WIDTH_40;
		target_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
		break;
	default:
		target_width = CHANNEL_WIDTH_20;
		target_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		break;
	}
#endif
	RTW_INFO(FUNC_ADPT_FMT" ch:%d bw:%d, offset:%d\n"
		, FUNC_ADPT_ARG(padapter), target_channal, target_width, target_offset);

	rtw_set_chbw_cmd(padapter, target_channal, target_width, target_offset, RTW_CMDF_WAIT_ACK);

	return 0;
}

static int	cfg80211_rtw_auth(struct wiphy *wiphy, struct net_device *ndev,
		struct cfg80211_auth_request *req)
{
	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	return 0;
}

static int	cfg80211_rtw_assoc(struct wiphy *wiphy, struct net_device *ndev,
		struct cfg80211_assoc_request *req)
{
	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	return 0;
}
#endif /* CONFIG_AP_MODE */

void rtw_cfg80211_rx_probe_request(_adapter *adapter, union recv_frame *rframe)
{
	struct wireless_dev *wdev = adapter->rtw_wdev;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(adapter);
	u8 *frame = get_recvframe_data(rframe);
	uint frame_len = rframe->u.hdr.len;
	s32 freq;
	u8 ch, sch = rtw_get_oper_ch(adapter);

	ch = rframe->u.hdr.attrib.ch ? rframe->u.hdr.attrib.ch : sch;
	freq = rtw_ch2freq(ch);

#ifdef CONFIG_DEBUG_CFG80211
	RTW_INFO("RTW_Rx: probe request, ch=%d(%d), ta="MAC_FMT"\n"
		, ch, sch, MAC_ARG(get_addr2_ptr(frame)));
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
	rtw_cfg80211_rx_mgmt(wdev, freq, 0, frame, frame_len, GFP_ATOMIC);
#else
	cfg80211_rx_action(adapter->pnetdev, freq, frame, frame_len, GFP_ATOMIC);
#endif
}

void rtw_cfg80211_rx_action_p2p(_adapter *adapter, union recv_frame *rframe)
{
	struct wireless_dev *wdev = adapter->rtw_wdev;
	u8 *frame = get_recvframe_data(rframe);
	uint frame_len = rframe->u.hdr.len;
	s32 freq;
	u8 ch, sch = rtw_get_oper_ch(adapter);
	u8 category, action;
	int type;

	ch = rframe->u.hdr.attrib.ch ? rframe->u.hdr.attrib.ch : sch;
	freq = rtw_ch2freq(ch);

	RTW_INFO("RTW_Rx:ch=%d(%d), ta="MAC_FMT"\n"
		, ch, sch, MAC_ARG(get_addr2_ptr(frame)));
#ifdef CONFIG_P2P
	type = rtw_p2p_check_frames(adapter, frame, frame_len, _FALSE);
	if (type >= 0)
		goto indicate;
#endif
	rtw_action_frame_parse(frame, frame_len, &category, &action);
	RTW_INFO("RTW_Rx:category(%u), action(%u)\n", category, action);

indicate:

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
	rtw_cfg80211_rx_mgmt(wdev, freq, 0, frame, frame_len, GFP_ATOMIC);
#else
	cfg80211_rx_action(adapter->pnetdev, freq, frame, frame_len, GFP_ATOMIC);
#endif
}

void rtw_cfg80211_rx_p2p_action_public(_adapter *adapter, union recv_frame *rframe)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct wireless_dev *wdev = adapter->rtw_wdev;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(adapter);
	u8 *frame = get_recvframe_data(rframe);
	uint frame_len = rframe->u.hdr.len;
	s32 freq;
	u8 ch, sch = rtw_get_oper_ch(adapter);
	u8 category, action;
	int type;

	ch = rframe->u.hdr.attrib.ch ? rframe->u.hdr.attrib.ch : sch;
	freq = rtw_ch2freq(ch);

	RTW_INFO("RTW_Rx:ch=%d(%d, ta="MAC_FMT")\n"
		, ch, sch, MAC_ARG(get_addr2_ptr(frame)));
	#ifdef CONFIG_P2P
	type = rtw_p2p_check_frames(adapter, frame, frame_len, _FALSE);
	if (type >= 0) {
		switch (type) {
		case P2P_GO_NEGO_CONF:
			if (0) {
				RTW_INFO(FUNC_ADPT_FMT" Nego confirm. state=%u, status=%u, iaddr="MAC_FMT"\n"
					, FUNC_ADPT_ARG(adapter), pwdev_priv->nego_info.state, pwdev_priv->nego_info.status
					, MAC_ARG(pwdev_priv->nego_info.iface_addr));
			}
			if (pwdev_priv->nego_info.state == 2
				&& pwdev_priv->nego_info.status == 0
				&& rtw_check_invalid_mac_address(pwdev_priv->nego_info.iface_addr, _FALSE) == _FALSE
			) {
				_adapter *intended_iface = dvobj_get_adapter_by_addr(dvobj, pwdev_priv->nego_info.iface_addr);

				if (intended_iface) {
					RTW_INFO(FUNC_ADPT_FMT" Nego confirm. Allow only "ADPT_FMT" to scan for 2000 ms\n"
						, FUNC_ADPT_ARG(adapter), ADPT_ARG(intended_iface));
					/* allow only intended_iface to do scan for 2000 ms */
					rtw_mi_set_scan_deny(adapter, 2000);
					rtw_clear_scan_deny(intended_iface);
				}
			}
			break;
		case P2P_PROVISION_DISC_RESP:
		case P2P_INVIT_RESP:
			#if !RTW_P2P_GROUP_INTERFACE
			rtw_mi_buddy_set_scan_deny(adapter, 2000);
			#endif
			break;
		}
		goto indicate;
	}
	#endif
	rtw_action_frame_parse(frame, frame_len, &category, &action);
	RTW_INFO("RTW_Rx:category(%u), action(%u)\n", category, action);

indicate:
	#if defined(RTW_DEDICATED_P2P_DEVICE)
	if (rtw_cfg80211_redirect_pd_wdev(dvobj_to_wiphy(dvobj), get_ra(frame), &wdev))
		if (0)
			RTW_INFO("redirect to pd_wdev:%p\n", wdev);
	#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
	rtw_cfg80211_rx_mgmt(wdev, freq, 0, frame, frame_len, GFP_ATOMIC);
#else
	cfg80211_rx_action(adapter->pnetdev, freq, frame, frame_len, GFP_ATOMIC);
#endif
}

void rtw_cfg80211_rx_action(_adapter *adapter, union recv_frame *rframe, const char *msg)
{
	struct wireless_dev *wdev = adapter->rtw_wdev;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(adapter);
	u8 *frame = get_recvframe_data(rframe);
	uint frame_len = rframe->u.hdr.len;
	s32 freq;
	u8 ch, sch = rtw_get_oper_ch(adapter);
	u8 category, action;

	ch = rframe->u.hdr.attrib.ch ? rframe->u.hdr.attrib.ch : sch;
	freq = rtw_ch2freq(ch);

	RTW_INFO("RTW_Rx:ch=%d(%d), ta="MAC_FMT"\n"
		, ch, sch, MAC_ARG(get_addr2_ptr(frame)));

	rtw_action_frame_parse(frame, frame_len, &category, &action);
	if (category == RTW_WLAN_CATEGORY_PUBLIC) {
		if (action == ACT_PUBLIC_GAS_INITIAL_REQ) {
			rtw_mi_set_scan_deny(adapter, 200);
			rtw_mi_scan_abort(adapter, _FALSE); /*rtw_scan_abort_no_wait*/
		}
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
	rtw_cfg80211_rx_mgmt(wdev, freq, 0, frame, frame_len, GFP_ATOMIC);
#else
	cfg80211_rx_action(adapter->pnetdev, freq, frame, frame_len, GFP_ATOMIC);
#endif

	if (msg)
		RTW_INFO("RTW_Rx:%s\n", msg);
	else
		RTW_INFO("RTW_Rx:category(%u), action(%u)\n", category, action);
}

#ifdef CONFIG_RTW_80211K
void rtw_cfg80211_rx_rrm_action(_adapter *adapter, union recv_frame *rframe)
{
	struct wireless_dev *wdev = adapter->rtw_wdev;
	u8 *frame = get_recvframe_data(rframe);
	uint frame_len = rframe->u.hdr.len;
	s32 ch, freq;


	ch = rtw_get_oper_ch(adapter);
	freq = rtw_ch2freq(ch);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
	rtw_cfg80211_rx_mgmt(wdev, freq, 0, frame, frame_len, GFP_ATOMIC);
#else
	cfg80211_rx_action(adapter->pnetdev, freq, frame, frame_len, GFP_ATOMIC);
#endif
	RTW_INFO("RTW_Rx:ch=%d, freq=%d\n", ch, freq);
}
#endif /* CONFIG_RTW_80211K */

#ifdef CONFIG_P2P
void rtw_cfg80211_issue_p2p_provision_request(_adapter *padapter, const u8 *buf, size_t len)
{
	u16	wps_devicepassword_id = 0x0000;
	uint	wps_devicepassword_id_len = 0;
	u8			wpsie[255] = { 0x00 }, p2p_ie[255] = { 0x00 };
	uint			p2p_ielen = 0;
	uint			wpsielen = 0;
	u32	devinfo_contentlen = 0;
	u8	devinfo_content[64] = { 0x00 };
	u16	capability = 0;
	uint capability_len = 0;

	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u8			dialogToken = 1;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_PROVISION_DISC_REQ;
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

	struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
	u8 *frame_body = (unsigned char *)(buf + sizeof(struct rtw_ieee80211_hdr_3addr));
	size_t frame_body_len = len - sizeof(struct rtw_ieee80211_hdr_3addr);


	RTW_INFO("[%s] In\n", __FUNCTION__);

	/* prepare for building provision_request frame	 */
	_rtw_memcpy(pwdinfo->tx_prov_disc_info.peerIFAddr, GetAddr1Ptr(buf), ETH_ALEN);
	_rtw_memcpy(pwdinfo->tx_prov_disc_info.peerDevAddr, GetAddr1Ptr(buf), ETH_ALEN);

	pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_PUSH_BUTTON;

	rtw_get_wps_ie(frame_body + _PUBLIC_ACTION_IE_OFFSET_, frame_body_len - _PUBLIC_ACTION_IE_OFFSET_, wpsie, &wpsielen);
	rtw_get_wps_attr_content(wpsie, wpsielen, WPS_ATTR_DEVICE_PWID, (u8 *) &wps_devicepassword_id, &wps_devicepassword_id_len);
	wps_devicepassword_id = be16_to_cpu(wps_devicepassword_id);

	switch (wps_devicepassword_id) {
	case WPS_DPID_PIN:
		pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_LABEL;
		break;
	case WPS_DPID_USER_SPEC:
		pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_DISPLYA;
		break;
	case WPS_DPID_MACHINE_SPEC:
		break;
	case WPS_DPID_REKEY:
		break;
	case WPS_DPID_PBC:
		pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_PUSH_BUTTON;
		break;
	case WPS_DPID_REGISTRAR_SPEC:
		pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_KEYPAD;
		break;
	default:
		break;
	}


	if (rtw_get_p2p_ie(frame_body + _PUBLIC_ACTION_IE_OFFSET_, frame_body_len - _PUBLIC_ACTION_IE_OFFSET_, p2p_ie, &p2p_ielen)) {

		rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_DEVICE_INFO, devinfo_content, &devinfo_contentlen);
		rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CAPABILITY, (u8 *)&capability, &capability_len);

	}


	/* start to build provision_request frame	 */
	_rtw_memset(wpsie, 0, sizeof(wpsie));
	_rtw_memset(p2p_ie, 0, sizeof(p2p_ie));
	p2p_ielen = 0;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		return;


	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, pwdinfo->tx_prov_disc_info.peerDevAddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, pwdinfo->tx_prov_disc_info.peerDevAddr, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));


	/* build_prov_disc_request_p2p_ie	 */
	/*	P2P OUI */
	p2pielen = 0;
	p2p_ie[p2pielen++] = 0x50;
	p2p_ie[p2pielen++] = 0x6F;
	p2p_ie[p2pielen++] = 0x9A;
	p2p_ie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*	Commented by Albert 20110301 */
	/*	According to the P2P Specification, the provision discovery request frame should contain 3 P2P attributes */
	/*	1. P2P Capability */
	/*	2. Device Info */
	/*	3. Group ID ( When joining an operating P2P Group ) */

	/*	P2P Capability ATTR */
	/*	Type:	 */
	p2p_ie[p2pielen++] = P2P_ATTR_CAPABILITY;

	/*	Length: */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 ); */
	RTW_PUT_LE16(p2p_ie + p2pielen, 0x0002);
	p2pielen += 2;

	/*	Value: */
	/*	Device Capability Bitmap, 1 byte */
	/*	Group Capability Bitmap, 1 byte */
	_rtw_memcpy(p2p_ie + p2pielen, &capability, 2);
	p2pielen += 2;


	/*	Device Info ATTR */
	/*	Type: */
	p2p_ie[p2pielen++] = P2P_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	21->P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes)  */
	/*	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes) */
	/* *(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 21 + pwdinfo->device_name_len ); */
	RTW_PUT_LE16(p2p_ie + p2pielen, devinfo_contentlen);
	p2pielen += 2;

	/*	Value: */
	_rtw_memcpy(p2p_ie + p2pielen, devinfo_content, devinfo_contentlen);
	p2pielen += devinfo_contentlen;


	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2p_ie, &p2p_ielen);
	/* p2pielen = build_prov_disc_request_p2p_ie( pwdinfo, pframe, NULL, 0, pwdinfo->tx_prov_disc_info.peerDevAddr); */
	/* pframe += p2pielen; */
	pattrib->pktlen += p2p_ielen;

	wpsielen = 0;
	/*	WPS OUI */
	*(u32 *)(wpsie) = cpu_to_be32(WPSOUI);
	wpsielen += 4;

	/*	WPS version */
	/*	Type: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_VER1);
	wpsielen += 2;

	/*	Length: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
	wpsielen += 2;

	/*	Value: */
	wpsie[wpsielen++] = WPS_VERSION_1;	/*	Version 1.0 */

	/*	Config Method */
	/*	Type: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_CONF_METHOD);
	wpsielen += 2;

	/*	Length: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0002);
	wpsielen += 2;

	/*	Value: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(pwdinfo->tx_prov_disc_info.wps_config_method_request);
	wpsielen += 2;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen);


#ifdef CONFIG_WFD
	wfdielen = build_provdisc_req_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif

	pattrib->last_txcmdsz = pattrib->pktlen;

	/* dump_mgntframe(padapter, pmgntframe); */
	if (dump_mgntframe_and_wait_ack(padapter, pmgntframe) != _SUCCESS)
		RTW_INFO("%s, ack to\n", __func__);

	#if 0
	if(wps_devicepassword_id == WPS_DPID_REGISTRAR_SPEC) {
		RTW_INFO("waiting for p2p peer key-in PIN CODE\n");
		rtw_msleep_os(15000); /* 15 sec for key in PIN CODE, workaround for GS2 before issuing Nego Req. */
	}
	#endif

}

#ifdef CONFIG_RTW_80211R
static s32 cfg80211_rtw_update_ft_ies(struct wiphy *wiphy,
	struct net_device *ndev,
	struct cfg80211_update_ft_ies_params *ftie)
{
	_adapter *padapter = NULL;
	struct mlme_priv *pmlmepriv = NULL;
	ft_priv *pftpriv = NULL;
	_irqL irqL;
	u8 *p;
	u8 *pie = NULL;
	u32 ie_len = 0;

	if (ndev == NULL)
		return  -EINVAL;

	padapter = (_adapter *)rtw_netdev_priv(ndev);
	pmlmepriv = &(padapter->mlmepriv);
	pftpriv = &pmlmepriv->ftpriv;

	p = (u8 *)ftie->ie;
	if (ftie->ie_len <= sizeof(pftpriv->updated_ft_ies)) {
		_enter_critical_bh(&pmlmepriv->lock, &irqL);
		_rtw_memcpy(pftpriv->updated_ft_ies, ftie->ie, ftie->ie_len);
		pftpriv->updated_ft_ies_len = ftie->ie_len;
		_exit_critical_bh(&pmlmepriv->lock, &irqL);
	} else {
		RTW_ERR("FTIEs parsing fail!\n");
		return -EINVAL;
	}

	if ((rtw_to_roam(padapter) > 0) && rtw_chk_ft_status(padapter, RTW_FT_AUTHENTICATED_STA)) {
		RTW_PRINT("auth success, start reassoc\n");
		_enter_critical_bh(&pmlmepriv->lock, &irqL);
		rtw_set_ft_status(padapter, RTW_FT_ASSOCIATING_STA);
		_exit_critical_bh(&pmlmepriv->lock, &irqL);
		start_clnt_assoc(padapter);
	}

	return 0;
}
#endif

inline void rtw_cfg80211_set_is_roch(_adapter *adapter, bool val)
{
	adapter->cfg80211_wdinfo.is_ro_ch = val;
	rtw_mi_update_iface_status(&(adapter->mlmepriv), 0);
}

inline bool rtw_cfg80211_get_is_roch(_adapter *adapter)
{
	return adapter->cfg80211_wdinfo.is_ro_ch;
}

static s32 cfg80211_rtw_remain_on_channel(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	struct wireless_dev *wdev,
#else
	struct net_device *ndev,
#endif
	struct ieee80211_channel *channel,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
	enum nl80211_channel_type channel_type,
#endif
	unsigned int duration, u64 *cookie)
{
	s32 err = 0;
	u8 remain_ch = (u8) ieee80211_frequency_to_channel(channel->center_freq);
	u8 union_ch = 0, union_bw = 0, union_offset = 0;
	u8 i;
	_adapter *padapter = NULL;
	struct rtw_wdev_priv *pwdev_priv;
	struct wifidirect_info *pwdinfo;
	struct cfg80211_wifidirect_info *pcfg80211_wdinfo;
	u8 is_p2p_find = _FALSE;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	#if defined(RTW_DEDICATED_P2P_DEVICE)
	if (wdev == wiphy_to_pd_wdev(wiphy))
		padapter = wiphy_to_adapter(wiphy);
	else
	#endif
	if (wdev_to_ndev(wdev))
		padapter = (_adapter *)rtw_netdev_priv(wdev_to_ndev(wdev));
	else {
		err = -EINVAL;
		goto exit;
	}
#else
	struct wireless_dev *wdev;

	if (ndev == NULL) {
		err = -EINVAL;
		goto exit;
	}
	padapter = (_adapter *)rtw_netdev_priv(ndev);
	wdev = ndev_to_wdev(ndev);
#endif

	pwdev_priv = adapter_wdev_data(padapter);
	pwdinfo = &padapter->wdinfo;
	pcfg80211_wdinfo = &padapter->cfg80211_wdinfo;
#ifdef CONFIG_CONCURRENT_MODE
	is_p2p_find = (duration < (pwdinfo->ext_listen_interval)) ? _TRUE : _FALSE;
#endif

	*cookie = ATOMIC_INC_RETURN(&pcfg80211_wdinfo->ro_ch_cookie_gen);

	RTW_INFO(FUNC_ADPT_FMT"%s ch:%u duration:%d, cookie:0x%llx\n"
		, FUNC_ADPT_ARG(padapter), wdev == wiphy_to_pd_wdev(wiphy) ? " PD" : ""
		, remain_ch, duration, *cookie);

	if (rtw_chset_search_ch(adapter_to_chset(padapter), remain_ch) < 0) {
		RTW_WARN(FUNC_ADPT_FMT" invalid ch:%u\n", FUNC_ADPT_ARG(padapter), remain_ch);
		err = -EFAULT;
		goto exit;
	}

#ifdef CONFIG_MP_INCLUDED
	if (rtw_mp_mode_check(padapter)) {
		RTW_INFO("MP mode block remain_on_channel request\n");
		err = -EFAULT;
		goto exit;
	}
#endif

	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		err = -EFAULT;
		goto exit;
	}

	rtw_scan_abort(padapter);
#ifdef CONFIG_CONCURRENT_MODE
	/*don't scan_abort during p2p_listen.*/
	if (is_p2p_find)
		rtw_mi_buddy_scan_abort(padapter, _TRUE);
#endif /*CONFIG_CONCURRENT_MODE*/

	if (rtw_cfg80211_get_is_roch(padapter) == _TRUE) {
		_cancel_timer_ex(&padapter->cfg80211_wdinfo.remain_on_ch_timer);
		p2p_cancel_roch_cmd(padapter, 0, NULL, RTW_CMDF_WAIT_ACK);
	}

	/* if(!rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT) && !rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) */
	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
		rtw_p2p_enable(padapter, P2P_ROLE_DEVICE);
		padapter->wdinfo.listen_channel = remain_ch;
		RTW_INFO(FUNC_ADPT_FMT" init listen_channel %u\n"
			, FUNC_ADPT_ARG(padapter), padapter->wdinfo.listen_channel);
	} else if (rtw_p2p_chk_state(pwdinfo , P2P_STATE_LISTEN)
		&& (time_after_eq(rtw_get_current_time(), pwdev_priv->probe_resp_ie_update_time)
			&& rtw_get_passing_time_ms(pwdev_priv->probe_resp_ie_update_time) < 50)
	) {
		if (padapter->wdinfo.listen_channel != remain_ch) {
			padapter->wdinfo.listen_channel = remain_ch;
			RTW_INFO(FUNC_ADPT_FMT" update listen_channel %u\n"
				, FUNC_ADPT_ARG(padapter), padapter->wdinfo.listen_channel);
		}
	} else {
		rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
#ifdef CONFIG_DEBUG_CFG80211
		RTW_INFO("%s, role=%d, p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo));
#endif
	}

	rtw_p2p_set_state(pwdinfo, P2P_STATE_LISTEN);

	#ifdef RTW_ROCH_DURATION_ENLARGE
	if (duration < 400)
		duration = duration * 3; /* extend from exper */
	#endif

#if defined(RTW_ROCH_BACK_OP) && defined(CONFIG_CONCURRENT_MODE)
	if (rtw_mi_check_status(padapter, MI_LINKED)) {
		if (is_p2p_find) /* p2p_find , duration<1000 */
			duration = duration + pwdinfo->ext_listen_interval;
		else /* p2p_listen, duration=5000 */
			duration = pwdinfo->ext_listen_interval + (pwdinfo->ext_listen_interval / 4);
	}
#endif /*defined (RTW_ROCH_BACK_OP) && defined(CONFIG_CONCURRENT_MODE) */

	rtw_cfg80211_set_is_roch(padapter, _TRUE);
	pcfg80211_wdinfo->ro_ch_wdev = wdev;
	pcfg80211_wdinfo->remain_on_ch_cookie = *cookie;
	pcfg80211_wdinfo->last_ro_ch_time = rtw_get_current_time();
	_rtw_memcpy(&pcfg80211_wdinfo->remain_on_ch_channel, channel, sizeof(struct ieee80211_channel));
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
	pcfg80211_wdinfo->remain_on_ch_type = channel_type;
	#endif
	pcfg80211_wdinfo->restore_channel = rtw_get_oper_ch(padapter);

	p2p_roch_cmd(padapter, *cookie, wdev, channel, pcfg80211_wdinfo->remain_on_ch_type,
		duration, RTW_CMDF_WAIT_ACK);

	rtw_cfg80211_ready_on_channel(wdev, *cookie, channel, channel_type, duration, GFP_KERNEL);
exit:
	return err;
}

static s32 cfg80211_rtw_cancel_remain_on_channel(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	struct wireless_dev *wdev,
#else
	struct net_device *ndev,
#endif
	u64 cookie)
{
	s32 err = 0;
	_adapter *padapter;
	struct rtw_wdev_priv *pwdev_priv;
	struct wifidirect_info *pwdinfo;
	struct cfg80211_wifidirect_info *pcfg80211_wdinfo;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	#if defined(RTW_DEDICATED_P2P_DEVICE)
	if (wdev == wiphy_to_pd_wdev(wiphy))
		padapter = wiphy_to_adapter(wiphy);
	else
	#endif
	if (wdev_to_ndev(wdev))
		padapter = (_adapter *)rtw_netdev_priv(wdev_to_ndev(wdev));
	else {
		err = -EINVAL;
		goto exit;
	}
#else
	struct wireless_dev *wdev;

	if (ndev == NULL) {
		err = -EINVAL;
		goto exit;
	}
	padapter = (_adapter *)rtw_netdev_priv(ndev);
	wdev = ndev_to_wdev(ndev);
#endif

	pwdev_priv = adapter_wdev_data(padapter);
	pwdinfo = &padapter->wdinfo;
	pcfg80211_wdinfo = &padapter->cfg80211_wdinfo;

	RTW_INFO(FUNC_ADPT_FMT"%s cookie:0x%llx\n"
		, FUNC_ADPT_ARG(padapter), wdev == wiphy_to_pd_wdev(wiphy) ? " PD" : ""
		, cookie);

	if (rtw_cfg80211_get_is_roch(padapter) == _TRUE) {
		_cancel_timer_ex(&padapter->cfg80211_wdinfo.remain_on_ch_timer);
		p2p_cancel_roch_cmd(padapter, cookie, wdev, RTW_CMDF_WAIT_ACK);
	}

exit:
	return err;
}

inline int rtw_cfg80211_iface_has_p2p_group_cap(_adapter *adapter)
{
	struct wiphy *wiphy = adapter_to_wiphy(adapter);
	struct rtw_wdev_priv *wdev_data = adapter_wdev_data(adapter);

#if RTW_P2P_GROUP_INTERFACE
	if (is_primary_adapter(adapter))
		return 0;
#endif
	return 1;
}

inline int rtw_cfg80211_is_p2p_scan(_adapter *adapter)
{
#if RTW_P2P_GROUP_INTERFACE
	if (rtw_cfg80211_iface_has_p2p_group_cap(adapter))
#endif
	{
		struct wifidirect_info *wdinfo = &adapter->wdinfo;

		return rtw_p2p_chk_state(wdinfo, P2P_STATE_SCAN)
			|| rtw_p2p_chk_state(wdinfo, P2P_STATE_FIND_PHASE_SEARCH);
	}

#if RTW_P2P_GROUP_INTERFACE
	#if defined(RTW_DEDICATED_P2P_DEVICE)
	if (wiphy_to_pd_wdev(adapter_to_wiphy(adapter))) /* pd_wdev exist */
		return rtw_cfg80211_is_scan_by_pd_wdev(adapter);
	#endif
	{
		/*
		* For 2 RTW_P2P_GROUP_INTERFACE cases:
		* 1. RTW_DEDICATED_P2P_DEVICE defined but upper layer don't use pd_wdev or
		* 2. RTW_DEDICATED_P2P_DEVICE not defined
		*/
		struct rtw_wdev_priv *wdev_data = adapter_wdev_data(adapter);
		_irqL irqL;
		int is_p2p_scan = 0;

		_enter_critical_bh(&wdev_data->scan_req_lock, &irqL);
		if (wdev_data->scan_request
			&& wdev_data->scan_request->ssids
			&& wdev_data->scan_request->ie
		) {
			if (_rtw_memcmp(wdev_data->scan_request->ssids->ssid, "DIRECT-", 7)
				&& rtw_get_p2p_ie((u8 *)wdev_data->scan_request->ie, wdev_data->scan_request->ie_len, NULL, NULL))
				is_p2p_scan = 1;
		}
		_exit_critical_bh(&wdev_data->scan_req_lock, &irqL);

		return is_p2p_scan;
	}
#endif
}

#if defined(RTW_DEDICATED_P2P_DEVICE)
int rtw_pd_iface_alloc(struct wiphy *wiphy, const char *name, struct wireless_dev **pd_wdev)
{
	struct rtw_wiphy_data *wiphy_data = rtw_wiphy_priv(wiphy);
	struct wireless_dev *wdev = NULL;
	struct rtw_netdev_priv_indicator *npi;
	_adapter *primary_adpt = wiphy_to_adapter(wiphy);
	int ret = 0;

	if (wiphy_data->pd_wdev) {
		RTW_WARN(FUNC_WIPHY_FMT" pd_wdev already exists\n", FUNC_WIPHY_ARG(wiphy));
		ret = -EBUSY;
		goto exit;
	}

	wdev = (struct wireless_dev *)rtw_zmalloc(sizeof(struct wireless_dev));
	if (!wdev) {
		RTW_WARN(FUNC_WIPHY_FMT" allocate wdev fail\n", FUNC_WIPHY_ARG(wiphy));
		ret = -ENOMEM;
		goto exit;
	}

	wdev->wiphy = wiphy;
	wdev->iftype = NL80211_IFTYPE_P2P_DEVICE;
	_rtw_memcpy(wdev->address, adapter_mac_addr(primary_adpt), ETH_ALEN);

	wiphy_data->pd_wdev = wdev;
	*pd_wdev = wdev;

	RTW_INFO(FUNC_WIPHY_FMT" pd_wdev:%p, addr="MAC_FMT" added\n"
		, FUNC_WIPHY_ARG(wiphy), wdev, MAC_ARG(wdev_address(wdev)));

exit:
	if (ret && wdev) {
		rtw_mfree((u8 *)wdev, sizeof(struct wireless_dev));
		wdev = NULL;
	}

	return ret;
}

void rtw_pd_iface_free(struct wiphy *wiphy)
{
	struct dvobj_priv *dvobj = wiphy_to_dvobj(wiphy);
	struct rtw_wiphy_data *wiphy_data = rtw_wiphy_priv(wiphy);
	u8 rtnl_lock_needed;

	if (!wiphy_data->pd_wdev)
		goto exit;

	RTW_INFO(FUNC_WIPHY_FMT" pd_wdev:%p, addr="MAC_FMT"\n"
		, FUNC_WIPHY_ARG(wiphy), wiphy_data->pd_wdev
		, MAC_ARG(wdev_address(wiphy_data->pd_wdev)));

	rtnl_lock_needed = rtw_rtnl_lock_needed(dvobj);
	if (rtnl_lock_needed)
		rtnl_lock();
	cfg80211_unregister_wdev(wiphy_data->pd_wdev);
	if (rtnl_lock_needed)
		rtnl_unlock();

	rtw_mfree((u8 *)wiphy_data->pd_wdev, sizeof(struct wireless_dev));
	wiphy_data->pd_wdev = NULL;

exit:
	return;
}

static int cfg80211_rtw_start_p2p_device(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	_adapter *adapter = wiphy_to_adapter(wiphy);

	RTW_INFO(FUNC_WIPHY_FMT" wdev=%p\n", FUNC_WIPHY_ARG(wiphy), wdev);

	rtw_p2p_enable(adapter, P2P_ROLE_DEVICE);
	return 0;
}

static void cfg80211_rtw_stop_p2p_device(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	_adapter *adapter = wiphy_to_adapter(wiphy);

	RTW_INFO(FUNC_WIPHY_FMT" wdev=%p\n", FUNC_WIPHY_ARG(wiphy), wdev);

	if (rtw_cfg80211_is_p2p_scan(adapter))
		rtw_scan_abort(adapter);

	rtw_p2p_enable(adapter, P2P_ROLE_DISABLE);
}

inline int rtw_cfg80211_redirect_pd_wdev(struct wiphy *wiphy, u8 *ra, struct wireless_dev **wdev)
{
	struct wireless_dev *pd_wdev = wiphy_to_pd_wdev(wiphy);

	if (pd_wdev && pd_wdev != *wdev
		&& _rtw_memcmp(wdev_address(pd_wdev), ra, ETH_ALEN) == _TRUE
	) {
		*wdev = pd_wdev;
		return 1;
	}
	return 0;
}

inline int rtw_cfg80211_is_scan_by_pd_wdev(_adapter *adapter)
{
	struct wiphy *wiphy = adapter_to_wiphy(adapter);
	struct rtw_wdev_priv *wdev_data = adapter_wdev_data(adapter);
	struct wireless_dev *wdev = NULL;
	_irqL irqL;

	_enter_critical_bh(&wdev_data->scan_req_lock, &irqL);
	if (wdev_data->scan_request)
		wdev = wdev_data->scan_request->wdev;
	_exit_critical_bh(&wdev_data->scan_req_lock, &irqL);

	if (wdev && wdev == wiphy_to_pd_wdev(wiphy))
		return 1;

	return 0;
}
#endif /* RTW_DEDICATED_P2P_DEVICE */
#endif /* CONFIG_P2P */

inline void rtw_cfg80211_set_is_mgmt_tx(_adapter *adapter, u8 val)
{
	struct rtw_wdev_priv *wdev_priv = adapter_wdev_data(adapter);

	wdev_priv->is_mgmt_tx = val;
	rtw_mi_update_iface_status(&(adapter->mlmepriv), 0);
}

inline u8 rtw_cfg80211_get_is_mgmt_tx(_adapter *adapter)
{
	struct rtw_wdev_priv *wdev_priv = adapter_wdev_data(adapter);

	return wdev_priv->is_mgmt_tx;
}

static int _cfg80211_rtw_mgmt_tx(_adapter *padapter, u8 tx_ch, u8 no_cck, const u8 *buf, size_t len, int wait_ack)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	unsigned char	*pframe;
	int ret = _FAIL;
	bool ack = _TRUE;
	struct rtw_ieee80211_hdr *pwlanhdr;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);

#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif /* CONFIG_P2P */
	/* struct cfg80211_wifidirect_info *pcfg80211_wdinfo = &padapter->cfg80211_wdinfo; */

	rtw_cfg80211_set_is_mgmt_tx(padapter, 1);

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_ScanNotify(padapter, _TRUE);
#endif

#ifdef CONFIG_P2P
	if (rtw_cfg80211_get_is_roch(padapter) == _TRUE) {
		#ifdef CONFIG_CONCURRENT_MODE
		if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
			RTW_INFO("%s, extend ro ch time\n", __func__);
			_set_timer(&padapter->cfg80211_wdinfo.remain_on_ch_timer, pwdinfo->ext_listen_period);
		}
		#endif /* CONFIG_CONCURRENT_MODE */
	}
#endif /* CONFIG_P2P */

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(padapter)) {
		if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC))
			/* don't set channel, issue frame directly */
			goto issue_mgmt_frame;
	}
#endif /* CONFIG_MCC_MODE */

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(padapter, MI_LINKED)) {
		u8 union_ch = rtw_mi_get_union_chan(padapter);
		u8 co_channel = 0xff;
		co_channel = rtw_get_oper_ch(padapter);

		if (tx_ch != union_ch) {
			u16 ext_listen_period;

			if (ATOMIC_READ(&pwdev_priv->switch_ch_to) == 1) {
				rtw_leave_opch(padapter);

				ATOMIC_SET(&pwdev_priv->switch_ch_to, 0);
				/* RTW_INFO("%s, set switch ch timer, period=%d\n", __func__, pwdinfo->ext_listen_period); */
				/* _set_timer(&pwdinfo->ap_p2p_switch_timer, pwdinfo->ext_listen_period); */
			}

			if (check_fwstate(&padapter->mlmepriv, _FW_LINKED))
				ext_listen_period = 500;/*500ms*/
#ifdef CONFIG_P2P				
			else
				ext_listen_period = pwdinfo->ext_listen_period;

			_set_timer(&pwdinfo->ap_p2p_switch_timer, ext_listen_period);
#endif
			RTW_INFO("%s, set switch ch timer, period=%d\n", __func__, ext_listen_period);
		}

		if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED))
			pmlmeext->cur_channel = tx_ch;

		if (tx_ch != co_channel)
			set_channel_bwmode(padapter, tx_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	} else
#endif /* CONFIG_CONCURRENT_MODE */
	/* if (tx_ch != pmlmeext->cur_channel) { */
	if (tx_ch != rtw_get_oper_ch(padapter)) {
		if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED))
			pmlmeext->cur_channel = tx_ch;
		set_channel_bwmode(padapter, tx_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	}

issue_mgmt_frame:
	/* starting alloc mgmt frame to dump it */
	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL) {
		/* ret = -ENOMEM; */
		ret = _FAIL;
		goto exit;
	}

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	if (no_cck && IS_CCK_RATE(pattrib->rate)) {
		/* force OFDM 6M rate*/
		pattrib->rate = MGN_6M;
		pattrib->raid = rtw_get_mgntframe_raid(padapter, WIRELESS_11G);
	}

	pattrib->retry_ctrl = _FALSE;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	_rtw_memcpy(pframe, (void *)buf, len);
	pattrib->pktlen = len;

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;
	/* update seq number */
	pmlmeext->mgnt_seq = GetSequence(pwlanhdr);
	pattrib->seqnum = pmlmeext->mgnt_seq;
	pmlmeext->mgnt_seq++;

#ifdef CONFIG_P2P
	rtw_xframe_chk_wfd_ie(pmgntframe);
#endif /* CONFIG_P2P */

	pattrib->last_txcmdsz = pattrib->pktlen;

	if (wait_ack) {
		if (dump_mgntframe_and_wait_ack(padapter, pmgntframe) != _SUCCESS) {
			ack = _FALSE;
			ret = _FAIL;

#ifdef CONFIG_DEBUG_CFG80211
			RTW_INFO("%s, ack == _FAIL\n", __func__);
#endif
		} else {

#ifdef CONFIG_XMIT_ACK
			rtw_msleep_os(50);
#endif
#ifdef CONFIG_DEBUG_CFG80211
			RTW_INFO("%s, ack=%d, ok!\n", __func__, ack);
#endif
			ret = _SUCCESS;
		}
	} else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}
exit:
	rtw_cfg80211_set_is_mgmt_tx(padapter, 0);

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_ScanNotify(padapter, _FALSE);
#endif

#ifdef CONFIG_DEBUG_CFG80211
	RTW_INFO("%s, ret=%d\n", __func__, ret);
#endif

	return ret;

}

u8 rtw_mgnt_tx_handler(_adapter *adapter, u8 *buf)
{
	u8 rst = H2C_CMD_FAIL;
	struct mgnt_tx_parm *mgnt_parm = (struct mgnt_tx_parm *)buf;

	if (_cfg80211_rtw_mgmt_tx(adapter, mgnt_parm->tx_ch, mgnt_parm->no_cck,
		mgnt_parm->buf, mgnt_parm->len, mgnt_parm->wait_ack) == _SUCCESS)
		rst = H2C_SUCCESS;

	return rst;
}

static int cfg80211_rtw_mgmt_tx(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	struct wireless_dev *wdev,
#else
	struct net_device *ndev,
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)) || defined(COMPAT_KERNEL_RELEASE)
	struct ieee80211_channel *chan,
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)) || defined(COMPAT_KERNEL_RELEASE)
	bool offchan,
	#endif
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34)) && (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
	enum nl80211_channel_type channel_type,
	#endif
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)) && (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
	bool channel_type_valid,
	#endif
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)) || defined(COMPAT_KERNEL_RELEASE)
	unsigned int wait,
	#endif
	const u8 *buf, size_t len,
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	bool no_cck,
	#endif
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0))
	bool dont_wait_for_ack,
	#endif
#else
	struct cfg80211_mgmt_tx_params *params,
#endif
	u64 *cookie)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)) || defined(COMPAT_KERNEL_RELEASE)
	struct ieee80211_channel *chan = params->chan;
	bool offchan = params->offchan;
	unsigned int wait = params->wait;
	const u8 *buf = params->buf;
	size_t len = params->len;
	bool no_cck = params->no_cck;
	bool dont_wait_for_ack = params->dont_wait_for_ack;
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0))
	bool no_cck = 0;
#endif
	int ret = 0;
	u8 tx_ret;
	int wait_ack = 1;
	u32 dump_limit = RTW_MAX_MGMT_TX_CNT;
	u32 dump_cnt = 0;
	u32 sleep_ms = 0;
	u32 retry_guarantee_ms = 0;
	bool ack = _TRUE;
	u8 tx_ch;
	u8 category, action;
	u8 frame_styp;
#ifdef CONFIG_P2P
	u8 is_p2p = 0;
#endif
	int type = (-1);
	systime start = rtw_get_current_time();
	_adapter *padapter;
	struct dvobj_priv *dvobj;
	struct rtw_wdev_priv *pwdev_priv;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	#if defined(RTW_DEDICATED_P2P_DEVICE)
	if (wdev == wiphy_to_pd_wdev(wiphy))
		padapter = wiphy_to_adapter(wiphy);
	else
	#endif
	if (wdev_to_ndev(wdev))
		padapter = (_adapter *)rtw_netdev_priv(wdev_to_ndev(wdev));
	else {
		ret = -EINVAL;
		goto exit;
	}
#else
	struct wireless_dev *wdev;

	if (ndev == NULL) {
		ret = -EINVAL;
		goto exit;
	}
	padapter = (_adapter *)rtw_netdev_priv(ndev);
	wdev = ndev_to_wdev(ndev);
#endif

	if (chan == NULL) {
		ret = -EINVAL;
		goto exit;
	}

	tx_ch = (u8)ieee80211_frequency_to_channel(chan->center_freq);

	dvobj = adapter_to_dvobj(padapter);
	pwdev_priv = adapter_wdev_data(padapter);

	/* cookie generation */
	*cookie = (unsigned long) buf;

#ifdef CONFIG_DEBUG_CFG80211
	RTW_INFO(FUNC_ADPT_FMT"%s len=%zu, ch=%d"
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34)) && (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
		", ch_type=%d"
		#endif
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)) && (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
		", channel_type_valid=%d"
		#endif
		"\n", FUNC_ADPT_ARG(padapter), wdev == wiphy_to_pd_wdev(wiphy) ? " PD" : ""
		, len, tx_ch
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34)) && (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
		, channel_type
		#endif
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34)) && (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
		, channel_type_valid
		#endif
	);
#endif /* CONFIG_DEBUG_CFG80211 */

	/* indicate ack before issue frame to avoid racing with rsp frame */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
	rtw_cfg80211_mgmt_tx_status(wdev, *cookie, buf, len, ack, GFP_KERNEL);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34) && LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 36))
	cfg80211_action_tx_status(ndev, *cookie, buf, len, ack, GFP_KERNEL);
#endif

	frame_styp = le16_to_cpu(((struct rtw_ieee80211_hdr_3addr *)buf)->frame_ctl) & IEEE80211_FCTL_STYPE;
	if (IEEE80211_STYPE_PROBE_RESP == frame_styp) {
#ifdef CONFIG_DEBUG_CFG80211
		RTW_INFO("RTW_Tx: probe_resp tx_ch=%d, no_cck=%u, da="MAC_FMT"\n", tx_ch, no_cck, MAC_ARG(GetAddr1Ptr(buf)));
#endif /* CONFIG_DEBUG_CFG80211 */
		wait_ack = 0;
		goto dump;
	}

	if (rtw_action_frame_parse(buf, len, &category, &action) == _FALSE) {
		RTW_INFO(FUNC_ADPT_FMT" frame_control:0x%02x\n", FUNC_ADPT_ARG(padapter),
			le16_to_cpu(((struct rtw_ieee80211_hdr_3addr *)buf)->frame_ctl));
		goto exit;
	}

	RTW_INFO("RTW_Tx:tx_ch=%d, no_cck=%u, da="MAC_FMT"\n", tx_ch, no_cck, MAC_ARG(GetAddr1Ptr(buf)));
#ifdef CONFIG_P2P
	type = rtw_p2p_check_frames(padapter, buf, len, _TRUE);
	if (type >= 0) {
		is_p2p = 1;
		no_cck = 1; /* force no CCK for P2P frames */
		goto dump;
	}
#endif
	if (category == RTW_WLAN_CATEGORY_PUBLIC) {
		RTW_INFO("RTW_Tx:%s\n", action_public_str(action));
		switch (action) {
		case ACT_PUBLIC_GAS_INITIAL_REQ:
		case ACT_PUBLIC_GAS_INITIAL_RSP:
			sleep_ms = 50;
			retry_guarantee_ms = RTW_MAX_MGMT_TX_MS_GAS;
			break;
		}
	}
#ifdef CONFIG_RTW_80211K
	else if (category == RTW_WLAN_CATEGORY_RADIO_MEAS)
		RTW_INFO("RTW_Tx: RRM Action\n");
#endif
	else
		RTW_INFO("RTW_Tx:category(%u), action(%u)\n", category, action);

dump:

	rtw_ps_deny(padapter, PS_DENY_MGNT_TX);
	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -EFAULT;
		goto cancel_ps_deny;
	}

	while (1) {
		dump_cnt++;

		rtw_mi_set_scan_deny(padapter, 1000);
		rtw_mi_scan_abort(padapter, _TRUE);
		tx_ret = rtw_mgnt_tx_cmd(padapter, tx_ch, no_cck, buf, len, wait_ack, RTW_CMDF_WAIT_ACK);
		if (tx_ret == _SUCCESS
			|| (dump_cnt >= dump_limit && rtw_get_passing_time_ms(start) >= retry_guarantee_ms))
			break;

		if (sleep_ms > 0)
			rtw_msleep_os(sleep_ms);
	}

	if (tx_ret != _SUCCESS || dump_cnt > 1) {
		RTW_INFO(FUNC_ADPT_FMT" %s (%d/%d) in %d ms\n", FUNC_ADPT_ARG(padapter),
			tx_ret == _SUCCESS ? "OK" : "FAIL", dump_cnt, dump_limit, rtw_get_passing_time_ms(start));
	}

#ifdef CONFIG_P2P
	if (is_p2p) {
		switch (type) {
		case P2P_GO_NEGO_CONF:
			if (0) {
				RTW_INFO(FUNC_ADPT_FMT" Nego confirm. state=%u, status=%u, iaddr="MAC_FMT"\n"
					, FUNC_ADPT_ARG(padapter), pwdev_priv->nego_info.state, pwdev_priv->nego_info.status
					, MAC_ARG(pwdev_priv->nego_info.iface_addr));
			}
			if (pwdev_priv->nego_info.state == 2
				&& pwdev_priv->nego_info.status == 0
				&& rtw_check_invalid_mac_address(pwdev_priv->nego_info.iface_addr, _FALSE) == _FALSE
			) {
				_adapter *intended_iface = dvobj_get_adapter_by_addr(dvobj, pwdev_priv->nego_info.iface_addr);

				if (intended_iface) {
					RTW_INFO(FUNC_ADPT_FMT" Nego confirm. Allow only "ADPT_FMT" to scan for 2000 ms\n"
						, FUNC_ADPT_ARG(padapter), ADPT_ARG(intended_iface));
					/* allow only intended_iface to do scan for 2000 ms */
					rtw_mi_set_scan_deny(padapter, 2000);
					rtw_clear_scan_deny(intended_iface);
				}
			}
			break;
		case P2P_INVIT_RESP:
			if (pwdev_priv->invit_info.flags & BIT(0)
				&& pwdev_priv->invit_info.status == 0
			) {
				RTW_INFO(FUNC_ADPT_FMT" agree with invitation of persistent group\n",
					FUNC_ADPT_ARG(padapter));
				#if !RTW_P2P_GROUP_INTERFACE
				rtw_mi_buddy_set_scan_deny(padapter, 5000);
				#endif
				rtw_pwr_wakeup_ex(padapter, 5000);
			}
			break;
		}
	}
#endif /* CONFIG_P2P */

cancel_ps_deny:
	rtw_ps_deny_cancel(padapter, PS_DENY_MGNT_TX);
exit:
	return ret;
}

static void cfg80211_rtw_mgmt_frame_register(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	struct wireless_dev *wdev,
#else
	struct net_device *ndev,
#endif
	u16 frame_type, bool reg)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	struct net_device *ndev = wdev_to_ndev(wdev);
#endif
	_adapter *adapter;

	struct rtw_wdev_priv *pwdev_priv;

	if (ndev == NULL)
		goto exit;

	adapter = (_adapter *)rtw_netdev_priv(ndev);
	pwdev_priv = adapter_wdev_data(adapter);

#ifdef CONFIG_DEBUG_CFG80211
	RTW_INFO(FUNC_ADPT_FMT" frame_type:%x, reg:%d\n", FUNC_ADPT_ARG(adapter),
		frame_type, reg);
#endif

	/* Wait QC Verify */
	return;

	switch (frame_type) {
	case IEEE80211_STYPE_PROBE_REQ: /* 0x0040 */
		SET_CFG80211_REPORT_MGMT(pwdev_priv, IEEE80211_STYPE_PROBE_REQ, reg);
		break;
	case IEEE80211_STYPE_ACTION: /* 0x00D0 */
		SET_CFG80211_REPORT_MGMT(pwdev_priv, IEEE80211_STYPE_ACTION, reg);
		break;
	default:
		break;
	}

exit:
	return;
}

#if defined(CONFIG_TDLS) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
static int cfg80211_rtw_tdls_mgmt(struct wiphy *wiphy,
	struct net_device *ndev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
	const u8 *peer,
#else
	u8 *peer,
#endif
	u8 action_code,
	u8 dialog_token,
	u16 status_code,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
	u32 peer_capability,
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0))
	bool initiator,
#endif
	const u8 *buf,
	size_t len)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	int ret = 0;
	struct tdls_txmgmt txmgmt;

	if (hal_chk_wl_func(padapter, WL_FUNC_TDLS) == _FALSE) {
		RTW_INFO("Discard tdls action:%d, since hal doesn't support tdls\n", action_code);
		goto discard;
	}

	if (rtw_is_tdls_enabled(padapter) == _FALSE) {
		RTW_INFO("TDLS is not enabled\n");
		goto discard;
	}

	if (rtw_tdls_is_driver_setup(padapter)) {
		RTW_INFO("Discard tdls action:%d, let driver to set up direct link\n", action_code);
		goto discard;
	}

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	_rtw_memcpy(txmgmt.peer, peer, ETH_ALEN);
	txmgmt.action_code = action_code;
	txmgmt.dialog_token = dialog_token;
	txmgmt.status_code = status_code;
	txmgmt.len = len;
	txmgmt.buf = (u8 *)rtw_malloc(txmgmt.len);
	if (txmgmt.buf == NULL) {
		ret = -ENOMEM;
		goto bad;
	}
	_rtw_memcpy(txmgmt.buf, (void *)buf, txmgmt.len);

	/* Debug purpose */
#if 1
	RTW_INFO("%s %d\n", __FUNCTION__, __LINE__);
	RTW_INFO("peer:"MAC_FMT", action code:%d, dialog:%d, status code:%d\n",
		MAC_ARG(txmgmt.peer), txmgmt.action_code,
		txmgmt.dialog_token, txmgmt.status_code);
	if (txmgmt.len > 0) {
		int i = 0;
		for (; i < len; i++)
			printk("%02x ", *(txmgmt.buf + i));
		RTW_INFO("len:%d\n", (u32)txmgmt.len);
	}
#endif

	switch (txmgmt.action_code) {
	case TDLS_SETUP_REQUEST:
		issue_tdls_setup_req(padapter, &txmgmt, _TRUE);
		break;
	case TDLS_SETUP_RESPONSE:
		issue_tdls_setup_rsp(padapter, &txmgmt);
		break;
	case TDLS_SETUP_CONFIRM:
		issue_tdls_setup_cfm(padapter, &txmgmt);
		break;
	case TDLS_TEARDOWN:
		issue_tdls_teardown(padapter, &txmgmt, _TRUE);
		break;
	case TDLS_DISCOVERY_REQUEST:
		issue_tdls_dis_req(padapter, &txmgmt);
		break;
	case TDLS_DISCOVERY_RESPONSE:
		issue_tdls_dis_rsp(padapter, &txmgmt, pmlmeinfo->enc_algo ? _TRUE : _FALSE);
		break;
	}

bad:
	if (txmgmt.buf)
		rtw_mfree(txmgmt.buf, txmgmt.len);

discard:
	return ret;
}

static int cfg80211_rtw_tdls_oper(struct wiphy *wiphy,
	struct net_device *ndev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
	const u8 *peer,
#else
	u8 *peer,
#endif
	enum nl80211_tdls_operation oper)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	struct tdls_txmgmt	txmgmt;
	struct sta_info *ptdls_sta = NULL;

	RTW_INFO(FUNC_NDEV_FMT", nl80211_tdls_operation:%d\n", FUNC_NDEV_ARG(ndev), oper);

	if (hal_chk_wl_func(padapter, WL_FUNC_TDLS) == _FALSE) {
		RTW_INFO("Discard tdls oper:%d, since hal doesn't support tdls\n", oper);
		return 0;
	}

	if (rtw_is_tdls_enabled(padapter) == _FALSE) {
		RTW_INFO("TDLS is not enabled\n");
		return 0;
	}

#ifdef CONFIG_LPS
	rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_LEAVE, 1);
#endif /* CONFIG_LPS */

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	if (peer)
		_rtw_memcpy(txmgmt.peer, peer, ETH_ALEN);

	if (rtw_tdls_is_driver_setup(padapter)) {
		/* these two cases are done by driver itself */
		if (oper == NL80211_TDLS_ENABLE_LINK || oper == NL80211_TDLS_DISABLE_LINK)
			return 0;
	}

	switch (oper) {
	case NL80211_TDLS_DISCOVERY_REQ:
		issue_tdls_dis_req(padapter, &txmgmt);
		break;
	case NL80211_TDLS_SETUP:
#ifdef CONFIG_WFD
		if (_AES_ != padapter->securitypriv.dot11PrivacyAlgrthm) {
			if (padapter->wdinfo.wfd_tdls_weaksec == _TRUE)
				issue_tdls_setup_req(padapter, &txmgmt, _TRUE);
			else
				RTW_INFO("[%s] Current link is not AES, SKIP sending the tdls setup request!!\n", __FUNCTION__);
		} else
#endif /* CONFIG_WFD */
		{
			issue_tdls_setup_req(padapter, &txmgmt, _TRUE);
		}
		break;
	case NL80211_TDLS_TEARDOWN:
		ptdls_sta = rtw_get_stainfo(&(padapter->stapriv), txmgmt.peer);
		if (ptdls_sta != NULL) {
			txmgmt.status_code = _RSON_TDLS_TEAR_UN_RSN_;
			issue_tdls_teardown(padapter, &txmgmt, _TRUE);
		} else
			RTW_INFO("TDLS peer not found\n");
		break;
	case NL80211_TDLS_ENABLE_LINK:
		RTW_INFO(FUNC_NDEV_FMT", NL80211_TDLS_ENABLE_LINK;mac:"MAC_FMT"\n", FUNC_NDEV_ARG(ndev), MAC_ARG(peer));
		ptdls_sta = rtw_get_stainfo(&(padapter->stapriv), (u8 *)peer);
		if (ptdls_sta != NULL) {
			rtw_tdls_set_link_established(padapter, _TRUE);
			ptdls_sta->tdls_sta_state |= TDLS_LINKED_STATE;
			ptdls_sta->state |= _FW_LINKED;
			rtw_tdls_cmd(padapter, txmgmt.peer, TDLS_ESTABLISHED);
		}
		break;
	case NL80211_TDLS_DISABLE_LINK:
		RTW_INFO(FUNC_NDEV_FMT", NL80211_TDLS_DISABLE_LINK;mac:"MAC_FMT"\n", FUNC_NDEV_ARG(ndev), MAC_ARG(peer));
		ptdls_sta = rtw_get_stainfo(&(padapter->stapriv), (u8 *)peer);
		if (ptdls_sta != NULL)
			rtw_tdls_cmd(padapter, (u8 *)peer, TDLS_TEARDOWN_STA_LOCALLY);
		break;
	}
	return 0;
}
#endif /* CONFIG_TDLS */

#if defined(CONFIG_PNO_SUPPORT) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
static int cfg80211_rtw_sched_scan_start(struct wiphy *wiphy,
		struct net_device *dev,
		struct cfg80211_sched_scan_request *request)
{

	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct cfg80211_ssid *ssids;
	int n_ssids = 0;
	int interval = 0;
	int i = 0;
	u8 ret;

	if (padapter->bup == _FALSE) {
		RTW_INFO("%s: net device is down.\n", __func__);
		return -EIO;
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == _TRUE ||
		check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE  ||
		check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE) {
		RTW_INFO("%s: device is busy.\n", __func__);
		rtw_scan_abort(padapter);
	}

	if (request == NULL) {
		RTW_INFO("%s: invalid cfg80211_requests parameters.\n", __func__);
		return -EINVAL;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	interval = request->interval;
	n_ssids = request->n_match_sets;
	ssids = (struct cfg80211_ssid *)rtw_zmalloc(n_ssids * sizeof(struct cfg80211_ssid));
	if(ssids == NULL) {
		RTW_ERR("Fail to allocate ssids for PNO\n");
		return -ENOMEM;
	}
	for (i=0;i<request->n_match_sets;i++) {
			ssids[i].ssid_len = request->match_sets[i].ssid.ssid_len;
			memcpy(ssids[i].ssid, request->match_sets[i].ssid.ssid,
					request->match_sets[i].ssid.ssid_len);
	}
#else
	interval = request->interval;
	n_ssids = request->n_ssids;
	ssids = request->ssids;
#endif
ret = rtw_android_cfg80211_pno_setup(dev, ssids,
			n_ssids, interval);
	if (ret < 0) {
		RTW_INFO("%s ret: %d\n", __func__, ret);
		goto exit;
	}

	ret = rtw_android_pno_enable(dev, _TRUE);
	if (ret < 0) {
		RTW_INFO("%s ret: %d\n", __func__, ret);
		goto exit;
	}
exit:
	return ret;
}

static int cfg80211_rtw_sched_scan_stop(struct wiphy *wiphy,
		struct net_device *dev)
{
	return rtw_android_pno_enable(dev, _FALSE);
}

int	cfg80211_rtw_suspend(struct wiphy *wiphy, struct cfg80211_wowlan *wow) {
	RTW_DBG("==> %s\n",__func__);
	RTW_DBG("<== %s\n",__func__);
	return 0;
}

int	cfg80211_rtw_resume(struct wiphy *wiphy) {

	_adapter *padapter;
	struct pwrctrl_priv *pwrpriv;
	struct mlme_priv *pmlmepriv;
	padapter = wiphy_to_adapter(wiphy);
	pwrpriv = adapter_to_pwrctl(padapter);
	pmlmepriv = &padapter->mlmepriv;
	struct sitesurvey_parm parm;
	int i, len;


	RTW_DBG("==> %s\n",__func__);
	if (pwrpriv->wowlan_last_wake_reason == RX_PNO) {

		struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
		_irqL irqL;
		int PNOWakeupScanWaitCnt = 0;

		rtw_cfg80211_disconnected(padapter->rtw_wdev, 0, NULL, 0, 1, GFP_ATOMIC);

		rtw_init_sitesurvey_parm(padapter, &parm);
		for (i=0;i<pwrpriv->pnlo_info->ssid_num && i < RTW_SSID_SCAN_AMOUNT; i++) {
			len = pwrpriv->pno_ssid_list->node[i].SSID_len;
			_rtw_memcpy(&parm.ssid[i].Ssid, pwrpriv->pno_ssid_list->node[i].SSID, len);
			parm->ssid[i].SsidLength = len;
		}
		prm->ssid_num = pwrpriv->pnlo_info->ssid_num;

		rtw_lock_ext_suspend_timeout(10000);

		_enter_critical_bh(&pmlmepriv->lock, &irqL);
		//This modification fix PNO wakeup reconnect issue with hidden SSID AP.
		//rtw_sitesurvey_cmd(padapter, NULL);
		rtw_sitesurvey_cmd(padapter, &parm);
		_exit_critical_bh(&pmlmepriv->lock, &irqL);
		
		for (PNOWakeupScanWaitCnt = 0; PNOWakeupScanWaitCnt < 10; PNOWakeupScanWaitCnt++) {
			if(check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == _FALSE)
				break;
			rtw_msleep_os(1000);
		}
		
		_enter_critical_bh(&pmlmepriv->lock, &irqL);
		cfg80211_sched_scan_results(padapter->rtw_wdev->wiphy);
		_exit_critical_bh(&pmlmepriv->lock, &irqL);

	}
	RTW_DBG("<== %s\n",__func__);
	return 0;
	
}
#endif /* CONFIG_PNO_SUPPORT */

static int rtw_cfg80211_set_beacon_wpsp2pie(struct net_device *ndev, char *buf, int len)
{
	int ret = 0;
	uint wps_ielen = 0;
	u8 *wps_ie;
	u32	p2p_ielen = 0;
	u8 wps_oui[8] = {0x0, 0x50, 0xf2, 0x04};
	u8 *p2p_ie;
	u32	wfd_ielen = 0;
	u8 *wfd_ie;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);

	RTW_INFO(FUNC_NDEV_FMT" ielen=%d\n", FUNC_NDEV_ARG(ndev), len);

	if (len > 0) {
		wps_ie = rtw_get_wps_ie(buf, len, NULL, &wps_ielen);
		if (wps_ie) {
			#ifdef CONFIG_DEBUG_CFG80211
			RTW_INFO("bcn_wps_ielen=%d\n", wps_ielen);
			#endif

			if (pmlmepriv->wps_beacon_ie) {
				u32 free_len = pmlmepriv->wps_beacon_ie_len;
				pmlmepriv->wps_beacon_ie_len = 0;
				rtw_mfree(pmlmepriv->wps_beacon_ie, free_len);
				pmlmepriv->wps_beacon_ie = NULL;
			}

			pmlmepriv->wps_beacon_ie = rtw_malloc(wps_ielen);
			if (pmlmepriv->wps_beacon_ie == NULL) {
				RTW_INFO("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;

			}

			_rtw_memcpy(pmlmepriv->wps_beacon_ie, wps_ie, wps_ielen);
			pmlmepriv->wps_beacon_ie_len = wps_ielen;

			update_beacon(padapter, _VENDOR_SPECIFIC_IE_, wps_oui, _TRUE);

		}

		/* buf += wps_ielen; */
		/* len -= wps_ielen; */

		#ifdef CONFIG_P2P
		p2p_ie = rtw_get_p2p_ie(buf, len, NULL, &p2p_ielen);
		if (p2p_ie) {
			#ifdef CONFIG_DEBUG_CFG80211
			RTW_INFO("bcn_p2p_ielen=%d\n", p2p_ielen);
			#endif

			if (pmlmepriv->p2p_beacon_ie) {
				u32 free_len = pmlmepriv->p2p_beacon_ie_len;
				pmlmepriv->p2p_beacon_ie_len = 0;
				rtw_mfree(pmlmepriv->p2p_beacon_ie, free_len);
				pmlmepriv->p2p_beacon_ie = NULL;
			}

			pmlmepriv->p2p_beacon_ie = rtw_malloc(p2p_ielen);
			if (pmlmepriv->p2p_beacon_ie == NULL) {
				RTW_INFO("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;

			}

			_rtw_memcpy(pmlmepriv->p2p_beacon_ie, p2p_ie, p2p_ielen);
			pmlmepriv->p2p_beacon_ie_len = p2p_ielen;

		}
		#endif /* CONFIG_P2P */


		#ifdef CONFIG_WFD
		wfd_ie = rtw_get_wfd_ie(buf, len, NULL, &wfd_ielen);
		if (wfd_ie) {
			#ifdef CONFIG_DEBUG_CFG80211
			RTW_INFO("bcn_wfd_ielen=%d\n", wfd_ielen);
			#endif

			if (rtw_mlme_update_wfd_ie_data(pmlmepriv, MLME_BEACON_IE, wfd_ie, wfd_ielen) != _SUCCESS)
				return -EINVAL;
		}
		#endif /* CONFIG_WFD */

		pmlmeext->bstart_bss = _TRUE;

	}

	return ret;

}

static int rtw_cfg80211_set_probe_resp_wpsp2pie(struct net_device *net, char *buf, int len)
{
	int ret = 0;
	uint wps_ielen = 0;
	u8 *wps_ie;
	u32	p2p_ielen = 0;
	u8 *p2p_ie;
	u32	wfd_ielen = 0;
	u8 *wfd_ie;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

#ifdef CONFIG_DEBUG_CFG80211
	RTW_INFO("%s, ielen=%d\n", __func__, len);
#endif

	if (len > 0) {
		wps_ie = rtw_get_wps_ie(buf, len, NULL, &wps_ielen);
		if (wps_ie) {
			uint	attr_contentlen = 0;
			u16	uconfig_method, *puconfig_method = NULL;

			#ifdef CONFIG_DEBUG_CFG80211
			RTW_INFO("probe_resp_wps_ielen=%d\n", wps_ielen);
			#endif

			if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS)) {
				u8 sr = 0;
				rtw_get_wps_attr_content(wps_ie,  wps_ielen, WPS_ATTR_SELECTED_REGISTRAR, (u8 *)(&sr), NULL);

				if (sr != 0)
					RTW_INFO("%s, got sr\n", __func__);
				else {
					RTW_INFO("GO mode process WPS under site-survey,  sr no set\n");
					return ret;
				}
			}

			if (pmlmepriv->wps_probe_resp_ie) {
				u32 free_len = pmlmepriv->wps_probe_resp_ie_len;
				pmlmepriv->wps_probe_resp_ie_len = 0;
				rtw_mfree(pmlmepriv->wps_probe_resp_ie, free_len);
				pmlmepriv->wps_probe_resp_ie = NULL;
			}

			pmlmepriv->wps_probe_resp_ie = rtw_malloc(wps_ielen);
			if (pmlmepriv->wps_probe_resp_ie == NULL) {
				RTW_INFO("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;

			}

			/* add PUSH_BUTTON config_method by driver self in wpsie of probe_resp at GO Mode */
			puconfig_method = (u16 *)rtw_get_wps_attr_content(wps_ie, wps_ielen, WPS_ATTR_CONF_METHOD , NULL, &attr_contentlen);
			if (puconfig_method != NULL) {
				/* struct registry_priv *pregistrypriv = &padapter->registrypriv; */
				struct wireless_dev *wdev = padapter->rtw_wdev;

				#ifdef CONFIG_DEBUG_CFG80211
				/* printk("config_method in wpsie of probe_resp = 0x%x\n", be16_to_cpu(*puconfig_method)); */
				#endif

				#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
				/* for WIFI-DIRECT LOGO 4.2.2, AUTO GO can't set PUSH_BUTTON flags */
				if (wdev->iftype == NL80211_IFTYPE_P2P_GO) {
					uconfig_method = WPS_CM_PUSH_BUTTON;
					uconfig_method = cpu_to_be16(uconfig_method);

					*puconfig_method &= ~uconfig_method;
				}
				#endif
			}

			_rtw_memcpy(pmlmepriv->wps_probe_resp_ie, wps_ie, wps_ielen);
			pmlmepriv->wps_probe_resp_ie_len = wps_ielen;

		}

		/* buf += wps_ielen; */
		/* len -= wps_ielen; */

		#ifdef CONFIG_P2P
		p2p_ie = rtw_get_p2p_ie(buf, len, NULL, &p2p_ielen);
		if (p2p_ie) {
			u8 is_GO = _FALSE;
			u32 attr_contentlen = 0;
			u16 cap_attr = 0;

			#ifdef CONFIG_DEBUG_CFG80211
			RTW_INFO("probe_resp_p2p_ielen=%d\n", p2p_ielen);
			#endif

			/* Check P2P Capability ATTR */
			if (rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CAPABILITY, (u8 *)&cap_attr, (uint *) &attr_contentlen)) {
				u8 grp_cap = 0;
				/* RTW_INFO( "[%s] Got P2P Capability Attr!!\n", __FUNCTION__ ); */
				cap_attr = le16_to_cpu(cap_attr);
				grp_cap = (u8)((cap_attr >> 8) & 0xff);

				is_GO = (grp_cap & BIT(0)) ? _TRUE : _FALSE;

				if (is_GO)
					RTW_INFO("Got P2P Capability Attr, grp_cap=0x%x, is_GO\n", grp_cap);
			}


			if (is_GO == _FALSE) {
				if (pmlmepriv->p2p_probe_resp_ie) {
					u32 free_len = pmlmepriv->p2p_probe_resp_ie_len;
					pmlmepriv->p2p_probe_resp_ie_len = 0;
					rtw_mfree(pmlmepriv->p2p_probe_resp_ie, free_len);
					pmlmepriv->p2p_probe_resp_ie = NULL;
				}

				pmlmepriv->p2p_probe_resp_ie = rtw_malloc(p2p_ielen);
				if (pmlmepriv->p2p_probe_resp_ie == NULL) {
					RTW_INFO("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
					return -EINVAL;

				}
				_rtw_memcpy(pmlmepriv->p2p_probe_resp_ie, p2p_ie, p2p_ielen);
				pmlmepriv->p2p_probe_resp_ie_len = p2p_ielen;
			} else {
				if (pmlmepriv->p2p_go_probe_resp_ie) {
					u32 free_len = pmlmepriv->p2p_go_probe_resp_ie_len;
					pmlmepriv->p2p_go_probe_resp_ie_len = 0;
					rtw_mfree(pmlmepriv->p2p_go_probe_resp_ie, free_len);
					pmlmepriv->p2p_go_probe_resp_ie = NULL;
				}

				pmlmepriv->p2p_go_probe_resp_ie = rtw_malloc(p2p_ielen);
				if (pmlmepriv->p2p_go_probe_resp_ie == NULL) {
					RTW_INFO("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
					return -EINVAL;

				}
				_rtw_memcpy(pmlmepriv->p2p_go_probe_resp_ie, p2p_ie, p2p_ielen);
				pmlmepriv->p2p_go_probe_resp_ie_len = p2p_ielen;
			}

		}
		#endif /* CONFIG_P2P */


		#ifdef CONFIG_WFD
		wfd_ie = rtw_get_wfd_ie(buf, len, NULL, &wfd_ielen);
		if (wfd_ie) {
			#ifdef CONFIG_DEBUG_CFG80211
			RTW_INFO("probe_resp_wfd_ielen=%d\n", wfd_ielen);
			#endif

			if (rtw_mlme_update_wfd_ie_data(pmlmepriv, MLME_PROBE_RESP_IE, wfd_ie, wfd_ielen) != _SUCCESS)
				return -EINVAL;
		}
		#endif /* CONFIG_WFD */

	}

	return ret;

}

static int rtw_cfg80211_set_assoc_resp_wpsp2pie(struct net_device *net, char *buf, int len)
{
	int ret = 0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u8 *ie;
	u32 ie_len;

	RTW_INFO("%s, ielen=%d\n", __func__, len);

	if (len <= 0)
		goto exit;

	ie = rtw_get_wps_ie(buf, len, NULL, &ie_len);
	if (ie && ie_len) {
		if (pmlmepriv->wps_assoc_resp_ie) {
			u32 free_len = pmlmepriv->wps_assoc_resp_ie_len;

			pmlmepriv->wps_assoc_resp_ie_len = 0;
			rtw_mfree(pmlmepriv->wps_assoc_resp_ie, free_len);
			pmlmepriv->wps_assoc_resp_ie = NULL;
		}

		pmlmepriv->wps_assoc_resp_ie = rtw_malloc(ie_len);
		if (pmlmepriv->wps_assoc_resp_ie == NULL) {
			RTW_INFO("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
			return -EINVAL;
		}
		_rtw_memcpy(pmlmepriv->wps_assoc_resp_ie, ie, ie_len);
		pmlmepriv->wps_assoc_resp_ie_len = ie_len;
	}

	ie = rtw_get_p2p_ie(buf, len, NULL, &ie_len);
	if (ie && ie_len) {
		if (pmlmepriv->p2p_assoc_resp_ie) {
			u32 free_len = pmlmepriv->p2p_assoc_resp_ie_len;

			pmlmepriv->p2p_assoc_resp_ie_len = 0;
			rtw_mfree(pmlmepriv->p2p_assoc_resp_ie, free_len);
			pmlmepriv->p2p_assoc_resp_ie = NULL;
		}

		pmlmepriv->p2p_assoc_resp_ie = rtw_malloc(ie_len);
		if (pmlmepriv->p2p_assoc_resp_ie == NULL) {
			RTW_INFO("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
			return -EINVAL;
		}
		_rtw_memcpy(pmlmepriv->p2p_assoc_resp_ie, ie, ie_len);
		pmlmepriv->p2p_assoc_resp_ie_len = ie_len;
	}

#ifdef CONFIG_WFD
	ie = rtw_get_wfd_ie(buf, len, NULL, &ie_len);
	if (rtw_mlme_update_wfd_ie_data(pmlmepriv, MLME_ASSOC_RESP_IE, ie, ie_len) != _SUCCESS)
		return -EINVAL;
#endif

exit:
	return ret;
}

int rtw_cfg80211_set_mgnt_wpsp2pie(struct net_device *net, char *buf, int len,
	int type)
{
	int ret = 0;
	uint wps_ielen = 0;
	u32	p2p_ielen = 0;

#ifdef CONFIG_DEBUG_CFG80211
	RTW_INFO("%s, ielen=%d\n", __func__, len);
#endif

	if ((rtw_get_wps_ie(buf, len, NULL, &wps_ielen) && (wps_ielen > 0))
		#ifdef CONFIG_P2P
		|| (rtw_get_p2p_ie(buf, len, NULL, &p2p_ielen) && (p2p_ielen > 0))
		#endif
	) {
		if (net != NULL) {
			switch (type) {
			case 0x1: /* BEACON */
				ret = rtw_cfg80211_set_beacon_wpsp2pie(net, buf, len);
				break;
			case 0x2: /* PROBE_RESP */
				ret = rtw_cfg80211_set_probe_resp_wpsp2pie(net, buf, len);
				#ifdef CONFIG_P2P
				if (ret == 0)
					adapter_wdev_data((_adapter *)rtw_netdev_priv(net))->probe_resp_ie_update_time = rtw_get_current_time();
				#endif
				break;
			case 0x4: /* ASSOC_RESP */
				ret = rtw_cfg80211_set_assoc_resp_wpsp2pie(net, buf, len);
				break;
			}
		}
	}

	return ret;

}

static void rtw_cfg80211_init_ht_capab_ex(_adapter *padapter
	, struct ieee80211_sta_ht_cap *ht_cap, BAND_TYPE band, u8 rf_type)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;
	u8 stbc_rx_enable = _FALSE;

	rtw_ht_use_default_setting(padapter);

	/* RX LDPC */
	if (TEST_FLAG(phtpriv->ldpc_cap, LDPC_HT_ENABLE_RX))
		ht_cap->cap |= IEEE80211_HT_CAP_LDPC_CODING;

	/* TX STBC */
	if (TEST_FLAG(phtpriv->stbc_cap, STBC_HT_ENABLE_TX))
		ht_cap->cap |= IEEE80211_HT_CAP_TX_STBC;

	/* RX STBC */
	if (TEST_FLAG(phtpriv->stbc_cap, STBC_HT_ENABLE_RX)) {
		/*rtw_rx_stbc 0: disable, bit(0):enable 2.4g, bit(1):enable 5g*/
		if (band == BAND_ON_2_4G)
			stbc_rx_enable = (pregistrypriv->rx_stbc & BIT(0)) ? _TRUE : _FALSE;
		if (band == BAND_ON_5G)
			stbc_rx_enable = (pregistrypriv->rx_stbc & BIT(1)) ? _TRUE : _FALSE;

		if (stbc_rx_enable) {
			switch (rf_type) {
			case RF_1T1R:
				ht_cap->cap |= IEEE80211_HT_CAP_RX_STBC_1R;/*RX STBC One spatial stream*/
				break;

			case RF_2T2R:
			case RF_1T2R:
				ht_cap->cap |= IEEE80211_HT_CAP_RX_STBC_1R;/* Only one spatial-stream STBC RX is supported */
				break;
			case RF_3T3R:
			case RF_3T4R:
			case RF_4T4R:
				ht_cap->cap |= IEEE80211_HT_CAP_RX_STBC_1R;/* Only one spatial-stream STBC RX is supported */
				break;
			default:
				RTW_INFO("[warning] rf_type %d is not expected\n", rf_type);
				break;
			}
		}
	}
}

static void rtw_cfg80211_init_ht_capab(_adapter *padapter
	, struct ieee80211_sta_ht_cap *ht_cap, BAND_TYPE band, u8 rf_type)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(padapter);
	u8 rx_nss = 0;

	ht_cap->ht_supported = _TRUE;

	ht_cap->cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
				IEEE80211_HT_CAP_SGI_40 | IEEE80211_HT_CAP_SGI_20 |
				IEEE80211_HT_CAP_DSSSCCK40 | IEEE80211_HT_CAP_MAX_AMSDU;
	rtw_cfg80211_init_ht_capab_ex(padapter, ht_cap, band, rf_type);

	/*
	 *Maximum length of AMPDU that the STA can receive.
	 *Length = 2 ^ (13 + max_ampdu_length_exp) - 1 (octets)
	 */
	ht_cap->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;

	/*Minimum MPDU start spacing , */
	ht_cap->ampdu_density = IEEE80211_HT_MPDU_DENSITY_16;

	ht_cap->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;

	rx_nss = rtw_min(rf_type_to_rf_rx_cnt(rf_type), hal_spec->rx_nss_num);
	switch (rx_nss) {
	case 1:
		ht_cap->mcs.rx_mask[0] = 0xFF;
		break;
	case 2:
		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0xFF;
		break;
	case 3:
		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0xFF;
		ht_cap->mcs.rx_mask[2] = 0xFF;
		break;
	case 4:
		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0xFF;
		ht_cap->mcs.rx_mask[2] = 0xFF;
		ht_cap->mcs.rx_mask[3] = 0xFF;
		break;
	default:
		rtw_warn_on(1);
		RTW_INFO("%s, error rf_type=%d\n", __func__, rf_type);
	};

	ht_cap->mcs.rx_highest = cpu_to_le16(
		rtw_mcs_rate(rf_type
			, hal_is_bw_support(padapter, CHANNEL_WIDTH_40)
			, hal_is_bw_support(padapter, CHANNEL_WIDTH_40) ? ht_cap->cap & IEEE80211_HT_CAP_SGI_40 : ht_cap->cap & IEEE80211_HT_CAP_SGI_20
			, ht_cap->mcs.rx_mask) / 10);
}

void rtw_cfg80211_init_wdev_data(_adapter *padapter)
{
#ifdef CONFIG_CONCURRENT_MODE
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);

	ATOMIC_SET(&pwdev_priv->switch_ch_to, 1);
#endif
}

void rtw_cfg80211_init_wiphy(_adapter *padapter)
{
	u8 rf_type;
	struct ieee80211_supported_band *band;
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct wiphy *wiphy = pwdev->wiphy;

	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

	RTW_INFO("%s:rf_type=%d\n", __func__, rf_type);

	if (IsSupported24G(padapter->registrypriv.wireless_mode)) {
		band = wiphy->bands[NL80211_BAND_2GHZ];
		if (band)
			rtw_cfg80211_init_ht_capab(padapter, &band->ht_cap, BAND_ON_2_4G, rf_type);
	}
#ifdef CONFIG_IEEE80211_BAND_5GHZ
	if (is_supported_5g(padapter->registrypriv.wireless_mode)) {
		band = wiphy->bands[NL80211_BAND_5GHZ];
		if (band)
			rtw_cfg80211_init_ht_capab(padapter, &band->ht_cap, BAND_ON_5G, rf_type);
	}
#endif
	/* init regulary domain */
	rtw_regd_init(padapter);

	/* copy mac_addr to wiphy */
	_rtw_memcpy(wiphy->perm_addr, adapter_mac_addr(padapter), ETH_ALEN);

}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
struct ieee80211_iface_limit rtw_limits[] = {
	{
		.max = 2,
		.types = BIT(NL80211_IFTYPE_STATION)
			#if defined(CONFIG_P2P) && ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE))
			| BIT(NL80211_IFTYPE_P2P_CLIENT)
			#endif
	},
	#ifdef CONFIG_AP_MODE
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_AP)
			#if defined(CONFIG_P2P) && ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE))
			| BIT(NL80211_IFTYPE_P2P_GO)
			#endif
	},
	#endif
	#if defined(RTW_DEDICATED_P2P_DEVICE)
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_P2P_DEVICE)
	}
	#endif
};

struct ieee80211_iface_combination rtw_combinations[] = {
	{
		.limits = rtw_limits,
		.n_limits = ARRAY_SIZE(rtw_limits),
		#if defined(RTW_DEDICATED_P2P_DEVICE)
		.max_interfaces = 3,
		#else
		.max_interfaces = 2,
		#endif
		.num_different_channels = 1,
	},
};
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) */

static void rtw_cfg80211_preinit_wiphy(_adapter *adapter, struct wiphy *wiphy)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct registry_priv *regsty = dvobj_to_regsty(dvobj);

	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	wiphy->max_scan_ssids = RTW_SSID_SCAN_AMOUNT;
	wiphy->max_scan_ie_len = RTW_SCAN_IE_LEN_MAX;
	wiphy->max_num_pmkids = RTW_MAX_NUM_PMKIDS;

#if CONFIG_RTW_MACADDR_ACL && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
	wiphy->max_acl_mac_addrs = NUM_ACL;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)) || defined(COMPAT_KERNEL_RELEASE)
	wiphy->max_remain_on_channel_duration = RTW_MAX_REMAIN_ON_CHANNEL_DURATION;
#endif

	wiphy->interface_modes =	BIT(NL80211_IFTYPE_STATION)
								| BIT(NL80211_IFTYPE_ADHOC)
#ifdef CONFIG_AP_MODE
								| BIT(NL80211_IFTYPE_AP)
								#ifdef CONFIG_WIFI_MONITOR
								| BIT(NL80211_IFTYPE_MONITOR)
								#endif
#endif
#if defined(CONFIG_P2P) && ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE))
								| BIT(NL80211_IFTYPE_P2P_CLIENT)
								| BIT(NL80211_IFTYPE_P2P_GO)
								#if defined(RTW_DEDICATED_P2P_DEVICE)
								| BIT(NL80211_IFTYPE_P2P_DEVICE)
								#endif
#endif
								;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
#ifdef CONFIG_AP_MODE
	wiphy->mgmt_stypes = rtw_cfg80211_default_mgmt_stypes;
#endif /* CONFIG_AP_MODE */
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	#ifdef CONFIG_WIFI_MONITOR
	wiphy->software_iftypes |= BIT(NL80211_IFTYPE_MONITOR);
	#endif
#endif

#if defined(RTW_SINGLE_WIPHY) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	wiphy->iface_combinations = rtw_combinations;
	wiphy->n_iface_combinations = ARRAY_SIZE(rtw_combinations);
#endif

	wiphy->cipher_suites = rtw_cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(rtw_cipher_suites);

	if (IsSupported24G(adapter->registrypriv.wireless_mode))
		wiphy->bands[NL80211_BAND_2GHZ] = rtw_spt_band_alloc(BAND_ON_2_4G);

#ifdef CONFIG_IEEE80211_BAND_5GHZ
	if (is_supported_5g(adapter->registrypriv.wireless_mode))
		wiphy->bands[NL80211_BAND_5GHZ] = rtw_spt_band_alloc(BAND_ON_5G);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38) && LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0))
	wiphy->flags |= WIPHY_FLAG_SUPPORTS_SEPARATE_DEFAULT_KEYS;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0))
	wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
	wiphy->flags |= WIPHY_FLAG_HAVE_AP_SME;
	/* remove WIPHY_FLAG_OFFCHAN_TX, because we not support this feature */
	/* wiphy->flags |= WIPHY_FLAG_OFFCHAN_TX | WIPHY_FLAG_HAVE_AP_SME; */
#endif

#if defined(CONFIG_PM) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) && \
			   LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
	wiphy->flags |= WIPHY_FLAG_SUPPORTS_SCHED_SCAN;
#ifdef CONFIG_PNO_SUPPORT
	wiphy->max_sched_scan_ssids = MAX_PNO_LIST_COUNT;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	wiphy->max_match_sets = MAX_PNO_LIST_COUNT;
#endif
#endif
#endif

#if defined(CONFIG_PM) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0))
	wiphy->wowlan = wowlan_stub;
#else
	wiphy->wowlan = &wowlan_stub;
#endif
#endif

#if defined(CONFIG_TDLS) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;
#ifndef CONFIG_TDLS_DRIVER_SETUP
	wiphy->flags |= WIPHY_FLAG_TDLS_EXTERNAL_SETUP;	/* Driver handles key exchange */
	wiphy->flags |= NL80211_ATTR_HT_CAPABILITY;
#endif /* CONFIG_TDLS_DRIVER_SETUP */
#endif /* CONFIG_TDLS */

	if (regsty->power_mgnt != PS_MODE_ACTIVE)
		wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
	else
		wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	/* wiphy->flags |= WIPHY_FLAG_SUPPORTS_FW_ROAM; */
#endif
}

#ifdef CONFIG_RFKILL_POLL
void rtw_cfg80211_init_rfkill(struct wiphy *wiphy)
{
	wiphy_rfkill_set_hw_state(wiphy, 0);
	wiphy_rfkill_start_polling(wiphy);
}

void rtw_cfg80211_deinit_rfkill(struct wiphy *wiphy)
{
	wiphy_rfkill_stop_polling(wiphy);
}

static void cfg80211_rtw_rfkill_poll(struct wiphy *wiphy)
{
	_adapter *padapter = NULL;
	bool blocked = _FALSE;
	u8 valid = 0;

	padapter = wiphy_to_adapter(wiphy);

	if (adapter_to_dvobj(padapter)->processing_dev_remove == _TRUE) {
		/*RTW_INFO("cfg80211_rtw_rfkill_poll: device is removed!\n");*/
		return;
	}

	blocked = rtw_hal_rfkill_poll(padapter, &valid);
	/*RTW_INFO("cfg80211_rtw_rfkill_poll: valid=%d, blocked=%d\n",
			valid, blocked);*/

	if (valid)
		wiphy_rfkill_set_hw_state(wiphy, blocked);
}
#endif

#if defined(CONFIG_RTW_HOSTAPD_ACS) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33))

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
#define SURVEY_INFO_TIME			SURVEY_INFO_CHANNEL_TIME
#define SURVEY_INFO_TIME_BUSY		SURVEY_INFO_CHANNEL_TIME_BUSY
#define SURVEY_INFO_TIME_EXT_BUSY	SURVEY_INFO_CHANNEL_TIME_EXT_BUSY
#define SURVEY_INFO_TIME_RX			SURVEY_INFO_CHANNEL_TIME_RX
#define SURVEY_INFO_TIME_TX			SURVEY_INFO_CHANNEL_TIME_TX
#endif

#ifdef CONFIG_FIND_BEST_CHANNEL
static void rtw_cfg80211_set_survey_info_with_find_best_channel(struct wiphy *wiphy
	, struct net_device *netdev, int idx, struct survey_info *info)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(netdev);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	RT_CHANNEL_INFO *ch_set = rfctl->channel_set;
	u8 ch_num = rfctl->max_chan_nums;
	u32 total_rx_cnt = 0;
	int i;

	s8 noise = -50;		/*channel noise in dBm. This and all following fields are optional */
	u64 time = 100;		/*amount of time in ms the radio was turn on (on the channel)*/
	u64 time_busy = 0;	/*amount of time the primary channel was sensed busy*/

	info->filled  = SURVEY_INFO_NOISE_DBM
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
		| SURVEY_INFO_TIME | SURVEY_INFO_TIME_BUSY
		#endif
		;

	for (i = 0; i < ch_num; i++)
		total_rx_cnt += ch_set[i].rx_count;

	time_busy = ch_set[idx].rx_count * time / total_rx_cnt;
	noise += ch_set[idx].rx_count * 50 / total_rx_cnt;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	info->channel_time = time;
	info->channel_time_busy = time_busy;
	#else
	info->time = time;
	info->time_busy = time_busy;
	#endif
#endif
	info->noise = noise;

	/* reset if final channel is got */
	if (idx == ch_num - 1) {
		for (i = 0; i < ch_num; i++)
			ch_set[i].rx_count = 0;
	}
}
#endif /* CONFIG_FIND_BEST_CHANNEL */

#if defined(CONFIG_RTW_ACS) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
static void rtw_cfg80211_set_survey_info_with_clm(PADAPTER padapter, int idx, struct survey_info *pinfo)
{
	s8 noise = -50;			/*channel noise in dBm. This and all following fields are optional */
	u64 time = SURVEY_TO;	/*amount of time in ms the radio was turn on (on the channel)*/
	u64 time_busy = 0;		/*amount of time the primary channel was sensed busy*/
	u8 chan = (u8)idx;

	if ((idx < 0) || (pinfo == NULL))
		return;

	pinfo->filled  = SURVEY_INFO_NOISE_DBM
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
		| SURVEY_INFO_TIME | SURVEY_INFO_TIME_BUSY
		#endif
		;

	time_busy = rtw_acs_get_clm_ratio_by_ch_idx(padapter, chan);
	noise = rtw_noise_query_by_chan_idx(padapter, chan);
	/* RTW_INFO("%s: ch-idx:%d time=%llu(ms), time_busy=%llu(ms), noise=%d(dbm)\n", __func__, idx, time, time_busy, noise); */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	pinfo->channel_time = time;
	pinfo->channel_time_busy = time_busy;
	#else
	pinfo->time = time;
	pinfo->time_busy = time_busy;
	#endif
#endif
	pinfo->noise = noise;
}
#endif

int rtw_hostapd_acs_dump_survey(struct wiphy *wiphy, struct net_device *netdev, int idx, struct survey_info *info)
{
	PADAPTER padapter = (_adapter *)rtw_netdev_priv(netdev);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	RT_CHANNEL_INFO *pch_set = rfctl->channel_set;
	u8 max_chan_nums = rfctl->max_chan_nums;
	u32 freq = 0;
	u8 ret = 0;
	u16 channel = 0;

	if (!netdev || !info) {
		RTW_INFO("%s: invial parameters.\n", __func__);
		return -EINVAL;
	}

	_rtw_memset(info, 0, sizeof(struct survey_info));
	if (padapter->bup == _FALSE) {
		RTW_INFO("%s: net device is down.\n", __func__);
		return -EIO;
	}

	if (idx >= max_chan_nums)
		return -ENOENT;

	channel = pch_set[idx].ChannelNum;
	freq = rtw_ch2freq(channel);
	info->channel = ieee80211_get_channel(wiphy, freq);
	/* RTW_INFO("%s: channel %d, freq %d\n", __func__, channel, freq); */

	if (!info->channel)
		return -EINVAL;

	if (info->channel->flags == IEEE80211_CHAN_DISABLED)
		return ret;

#ifdef CONFIG_FIND_BEST_CHANNEL
	rtw_cfg80211_set_survey_info_with_find_best_channel(wiphy, netdev, idx, info);
#elif defined(CONFIG_RTW_ACS) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
	rtw_cfg80211_set_survey_info_with_clm(padapter, idx, info);
#else
	RTW_ERR("%s: unknown acs operation!\n", __func__); 
#endif

	return ret;
}
#endif /* defined(CONFIG_RTW_HOSTAPD_ACS) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)) */

static struct cfg80211_ops rtw_cfg80211_ops = {
	.change_virtual_intf = cfg80211_rtw_change_iface,
	.add_key = cfg80211_rtw_add_key,
	.get_key = cfg80211_rtw_get_key,
	.del_key = cfg80211_rtw_del_key,
	.set_default_key = cfg80211_rtw_set_default_key,
#if defined(CONFIG_GTK_OL) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
	.set_rekey_data = cfg80211_rtw_set_rekey_data,
#endif /*CONFIG_GTK_OL*/
	.get_station = cfg80211_rtw_get_station,
	.scan = cfg80211_rtw_scan,
	.set_wiphy_params = cfg80211_rtw_set_wiphy_params,
	.connect = cfg80211_rtw_connect,
	.disconnect = cfg80211_rtw_disconnect,
	.join_ibss = cfg80211_rtw_join_ibss,
	.leave_ibss = cfg80211_rtw_leave_ibss,
	.set_tx_power = cfg80211_rtw_set_txpower,
	.get_tx_power = cfg80211_rtw_get_txpower,
	.set_power_mgmt = cfg80211_rtw_set_power_mgmt,
	.set_pmksa = cfg80211_rtw_set_pmksa,
	.del_pmksa = cfg80211_rtw_del_pmksa,
	.flush_pmksa = cfg80211_rtw_flush_pmksa,

#ifdef CONFIG_AP_MODE
	.add_virtual_intf = cfg80211_rtw_add_virtual_intf,
	.del_virtual_intf = cfg80211_rtw_del_virtual_intf,

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)) && !defined(COMPAT_KERNEL_RELEASE)
	.add_beacon = cfg80211_rtw_add_beacon,
	.set_beacon = cfg80211_rtw_set_beacon,
	.del_beacon = cfg80211_rtw_del_beacon,
#else
	.start_ap = cfg80211_rtw_start_ap,
	.change_beacon = cfg80211_rtw_change_beacon,
	.stop_ap = cfg80211_rtw_stop_ap,
#endif

#if CONFIG_RTW_MACADDR_ACL && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
	.set_mac_acl = cfg80211_rtw_set_mac_acl,
#endif

	.add_station = cfg80211_rtw_add_station,
	.del_station = cfg80211_rtw_del_station,
	.change_station = cfg80211_rtw_change_station,
	.dump_station = cfg80211_rtw_dump_station,
	.change_bss = cfg80211_rtw_change_bss,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
	.set_channel = cfg80211_rtw_set_channel,
#endif
	/* .auth = cfg80211_rtw_auth, */
	/* .assoc = cfg80211_rtw_assoc,	 */
#endif /* CONFIG_AP_MODE */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	.set_monitor_channel = cfg80211_rtw_set_monitor_channel,
#endif

#ifdef CONFIG_P2P
	.remain_on_channel = cfg80211_rtw_remain_on_channel,
	.cancel_remain_on_channel = cfg80211_rtw_cancel_remain_on_channel,
	#if defined(RTW_DEDICATED_P2P_DEVICE)
	.start_p2p_device = cfg80211_rtw_start_p2p_device,
	.stop_p2p_device = cfg80211_rtw_stop_p2p_device,
	#endif
#endif /* CONFIG_P2P */

#ifdef CONFIG_RTW_80211R
	.update_ft_ies = cfg80211_rtw_update_ft_ies,
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)) || defined(COMPAT_KERNEL_RELEASE)
	.mgmt_tx = cfg80211_rtw_mgmt_tx,
	.mgmt_frame_register = cfg80211_rtw_mgmt_frame_register,
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34) && LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
	.action = cfg80211_rtw_mgmt_tx,
#endif

#if defined(CONFIG_TDLS) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	.tdls_mgmt = cfg80211_rtw_tdls_mgmt,
	.tdls_oper = cfg80211_rtw_tdls_oper,
#endif /* CONFIG_TDLS */

#if defined(CONFIG_PNO_SUPPORT) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	.sched_scan_start = cfg80211_rtw_sched_scan_start,
	.sched_scan_stop = cfg80211_rtw_sched_scan_stop,
	.suspend = cfg80211_rtw_suspend,
	.resume = cfg80211_rtw_resume,
#endif /* CONFIG_PNO_SUPPORT */
#ifdef CONFIG_RFKILL_POLL
	.rfkill_poll = cfg80211_rtw_rfkill_poll,
#endif
#if defined(CONFIG_RTW_HOSTAPD_ACS) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33))
	.dump_survey = rtw_hostapd_acs_dump_survey,
#endif
};

struct wiphy *rtw_wiphy_alloc(_adapter *padapter, struct device *dev)
{
	struct wiphy *wiphy;
	struct rtw_wiphy_data *wiphy_data;

	/* wiphy */
	wiphy = wiphy_new(&rtw_cfg80211_ops, sizeof(struct rtw_wiphy_data));
	if (!wiphy) {
		RTW_INFO("Couldn't allocate wiphy device\n");
		goto exit;
	}
	set_wiphy_dev(wiphy, dev);

	/* wiphy_data */
	wiphy_data = rtw_wiphy_priv(wiphy);
	wiphy_data->dvobj = adapter_to_dvobj(padapter);
#ifndef RTW_SINGLE_WIPHY
	wiphy_data->adapter = padapter;
#endif

	rtw_cfg80211_preinit_wiphy(padapter, wiphy);

	RTW_INFO(FUNC_WIPHY_FMT"\n", FUNC_WIPHY_ARG(wiphy));

exit:
	return wiphy;
}

void rtw_wiphy_free(struct wiphy *wiphy)
{
	if (!wiphy)
		return;

	RTW_INFO(FUNC_WIPHY_FMT"\n", FUNC_WIPHY_ARG(wiphy));

	if (wiphy->bands[NL80211_BAND_2GHZ]) {
		rtw_spt_band_free(wiphy->bands[NL80211_BAND_2GHZ]);
		wiphy->bands[NL80211_BAND_2GHZ] = NULL;
	}
	if (wiphy->bands[NL80211_BAND_5GHZ]) {
		rtw_spt_band_free(wiphy->bands[NL80211_BAND_5GHZ]);
		wiphy->bands[NL80211_BAND_5GHZ] = NULL;
	}

	wiphy_free(wiphy);
}

int rtw_wiphy_register(struct wiphy *wiphy)
{
	RTW_INFO(FUNC_WIPHY_FMT"\n", FUNC_WIPHY_ARG(wiphy));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)) || defined(RTW_VENDOR_EXT_SUPPORT)
	rtw_cfgvendor_attach(wiphy);
#endif

	return wiphy_register(wiphy);
}

void rtw_wiphy_unregister(struct wiphy *wiphy)
{
	RTW_INFO(FUNC_WIPHY_FMT"\n", FUNC_WIPHY_ARG(wiphy));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)) || defined(RTW_VENDOR_EXT_SUPPORT)
	rtw_cfgvendor_detach(wiphy);
#endif

	#if defined(RTW_DEDICATED_P2P_DEVICE)
	rtw_pd_iface_free(wiphy);
	#endif

	return wiphy_unregister(wiphy);
}

int rtw_wdev_alloc(_adapter *padapter, struct wiphy *wiphy)
{
	int ret = 0;
	struct net_device *pnetdev = padapter->pnetdev;
	struct wireless_dev *wdev;
	struct rtw_wdev_priv *pwdev_priv;

	RTW_INFO("%s(padapter=%p)\n", __func__, padapter);

	/*  wdev */
	wdev = (struct wireless_dev *)rtw_zmalloc(sizeof(struct wireless_dev));
	if (!wdev) {
		RTW_INFO("Couldn't allocate wireless device\n");
		ret = -ENOMEM;
		goto exit;
	}
	wdev->wiphy = wiphy;
	wdev->netdev = pnetdev;

	wdev->iftype = NL80211_IFTYPE_STATION;	/* will be init in rtw_hal_init() */
											/* Must sync with _rtw_init_mlme_priv() */
											/* pmlmepriv->fw_state = WIFI_STATION_STATE */
	/* wdev->iftype = NL80211_IFTYPE_MONITOR; */ /* for rtw_setopmode_cmd() in cfg80211_rtw_change_iface() */
	padapter->rtw_wdev = wdev;
	pnetdev->ieee80211_ptr = wdev;

	/* init pwdev_priv */
	pwdev_priv = adapter_wdev_data(padapter);
	pwdev_priv->rtw_wdev = wdev;
	pwdev_priv->pmon_ndev = NULL;
	pwdev_priv->ifname_mon[0] = '\0';
	pwdev_priv->padapter = padapter;
	pwdev_priv->scan_request = NULL;
	_rtw_spinlock_init(&pwdev_priv->scan_req_lock);
	pwdev_priv->connect_req = NULL;
	_rtw_spinlock_init(&pwdev_priv->connect_req_lock);

	pwdev_priv->p2p_enabled = _FALSE;
	pwdev_priv->probe_resp_ie_update_time = rtw_get_current_time();
	pwdev_priv->provdisc_req_issued = _FALSE;
	rtw_wdev_invit_info_init(&pwdev_priv->invit_info);
	rtw_wdev_nego_info_init(&pwdev_priv->nego_info);

	pwdev_priv->bandroid_scan = _FALSE;

	if (padapter->registrypriv.power_mgnt != PS_MODE_ACTIVE)
		pwdev_priv->power_mgmt = _TRUE;
	else
		pwdev_priv->power_mgmt = _FALSE;

	_rtw_mutex_init(&pwdev_priv->roch_mutex);

#ifdef CONFIG_CONCURRENT_MODE
	ATOMIC_SET(&pwdev_priv->switch_ch_to, 1);
#endif

exit:
	return ret;
}

void rtw_wdev_free(struct wireless_dev *wdev)
{
	if (!wdev)
		return;

	RTW_INFO("%s(wdev=%p)\n", __func__, wdev);

	if (wdev_to_ndev(wdev)) {
		_adapter *adapter = (_adapter *)rtw_netdev_priv(wdev_to_ndev(wdev));
		struct rtw_wdev_priv *wdev_priv = adapter_wdev_data(adapter);
		_irqL irqL;

		_rtw_spinlock_free(&wdev_priv->scan_req_lock);

		_enter_critical_bh(&wdev_priv->connect_req_lock, &irqL);
		rtw_wdev_free_connect_req(wdev_priv);
		_exit_critical_bh(&wdev_priv->connect_req_lock, &irqL);
		_rtw_spinlock_free(&wdev_priv->connect_req_lock);

		_rtw_mutex_free(&wdev_priv->roch_mutex);
	}

	rtw_mfree((u8 *)wdev, sizeof(struct wireless_dev));
}

void rtw_wdev_unregister(struct wireless_dev *wdev)
{
	struct net_device *ndev;
	_adapter *adapter;
	struct rtw_wdev_priv *pwdev_priv;

	if (!wdev)
		return;

	RTW_INFO("%s(wdev=%p)\n", __func__, wdev);

	ndev = wdev_to_ndev(wdev);
	if (!ndev)
		return;

	adapter = (_adapter *)rtw_netdev_priv(ndev);
	pwdev_priv = adapter_wdev_data(adapter);

	rtw_cfg80211_indicate_scan_done(adapter, _TRUE);

	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)) || defined(COMPAT_KERNEL_RELEASE)
	if (wdev->current_bss) {
		RTW_INFO(FUNC_ADPT_FMT" clear current_bss by cfg80211_disconnected\n", FUNC_ADPT_ARG(adapter));
		rtw_cfg80211_indicate_disconnect(adapter, 0, 1);
	}
	#endif

	if (pwdev_priv->pmon_ndev) {
		RTW_INFO("%s, unregister monitor interface\n", __func__);
		unregister_netdev(pwdev_priv->pmon_ndev);
	}
}

int rtw_cfg80211_ndev_res_alloc(_adapter *adapter)
{
	int ret = _FAIL;

#if !defined(RTW_SINGLE_WIPHY)
	struct wiphy *wiphy;
	struct device *dev = dvobj_to_dev(adapter_to_dvobj(adapter));

	wiphy = rtw_wiphy_alloc(adapter, dev);
	if (wiphy == NULL)
		goto exit;

	adapter->wiphy = wiphy;
#endif

	if (rtw_wdev_alloc(adapter, adapter_to_wiphy(adapter)) == 0)
		ret = _SUCCESS;

#if !defined(RTW_SINGLE_WIPHY)
	if (ret != _SUCCESS) {
		rtw_wiphy_free(wiphy);
		adapter->wiphy = NULL;
	}
#endif

exit:
	return ret;
}

void rtw_cfg80211_ndev_res_free(_adapter *adapter)
{
	rtw_wdev_free(adapter->rtw_wdev);
	adapter->rtw_wdev = NULL;
#if !defined(RTW_SINGLE_WIPHY)
	rtw_wiphy_free(adapter_to_wiphy(adapter));
	adapter->wiphy = NULL;
#endif
}

int rtw_cfg80211_ndev_res_register(_adapter *adapter)
{
	int ret = _FAIL;

#if !defined(RTW_SINGLE_WIPHY)
	if (rtw_wiphy_register(adapter_to_wiphy(adapter)) < 0) {
		RTW_INFO("%s rtw_wiphy_register fail for if%d\n", __func__, (adapter->iface_id + 1));
		goto exit;
	}

#ifdef CONFIG_RFKILL_POLL
	rtw_cfg80211_init_rfkill(adapter_to_wiphy(adapter));
#endif
#endif

	ret = _SUCCESS;

exit:
	return ret;
}

void rtw_cfg80211_ndev_res_unregister(_adapter *adapter)
{
	rtw_wdev_unregister(adapter->rtw_wdev);
}

int rtw_cfg80211_dev_res_alloc(struct dvobj_priv *dvobj)
{
	int ret = _FAIL;

#if defined(RTW_SINGLE_WIPHY)
	struct wiphy *wiphy;
	struct device *dev = dvobj_to_dev(dvobj);

	wiphy = rtw_wiphy_alloc(dvobj_get_primary_adapter(dvobj), dev);
	if (wiphy == NULL)
		goto exit;

	dvobj->wiphy = wiphy;
#endif

	ret = _SUCCESS;

exit:
	return ret;
}

void rtw_cfg80211_dev_res_free(struct dvobj_priv *dvobj)
{
#if defined(RTW_SINGLE_WIPHY)
	rtw_wiphy_free(dvobj_to_wiphy(dvobj));
	dvobj->wiphy = NULL;
#endif
}

int rtw_cfg80211_dev_res_register(struct dvobj_priv *dvobj)
{
	int ret = _FAIL;

#if defined(RTW_SINGLE_WIPHY)
	if (rtw_wiphy_register(dvobj_to_wiphy(dvobj)) != 0)
		goto exit;

#ifdef CONFIG_RFKILL_POLL
	rtw_cfg80211_init_rfkill(dvobj_to_wiphy(dvobj));
#endif
#endif

	ret = _SUCCESS;

exit:
	return ret;
}

void rtw_cfg80211_dev_res_unregister(struct dvobj_priv *dvobj)
{
#if defined(RTW_SINGLE_WIPHY)
#ifdef CONFIG_RFKILL_POLL
	rtw_cfg80211_deinit_rfkill(dvobj_to_wiphy(dvobj));
#endif
	rtw_wiphy_unregister(dvobj_to_wiphy(dvobj));
#endif
}

#endif /* CONFIG_IOCTL_CFG80211 */
