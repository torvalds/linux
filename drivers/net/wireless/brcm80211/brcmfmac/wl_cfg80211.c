/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Toplevel file. Relies on dhd_linux.c to send commands to the dongle. */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/bitops.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/uaccess.h>
#include <net/cfg80211.h>
#include <net/netlink.h>

#include <brcmu_utils.h>
#include <defs.h>
#include <brcmu_wifi.h>
#include "dhd.h"
#include "wl_cfg80211.h"
#include "fwil.h"

#define BRCMF_SCAN_IE_LEN_MAX		2048
#define BRCMF_PNO_VERSION		2
#define BRCMF_PNO_TIME			30
#define BRCMF_PNO_REPEAT		4
#define BRCMF_PNO_FREQ_EXPO_MAX		3
#define BRCMF_PNO_MAX_PFN_COUNT		16
#define BRCMF_PNO_ENABLE_ADAPTSCAN_BIT	6
#define BRCMF_PNO_HIDDEN_BIT		2
#define BRCMF_PNO_WPA_AUTH_ANY		0xFFFFFFFF
#define BRCMF_PNO_SCAN_COMPLETE		1
#define BRCMF_PNO_SCAN_INCOMPLETE	0

#define BRCMF_IFACE_MAX_CNT		2

#define TLV_LEN_OFF			1	/* length offset */
#define TLV_HDR_LEN			2	/* header length */
#define TLV_BODY_OFF			2	/* body offset */
#define TLV_OUI_LEN			3	/* oui id length */
#define WPA_OUI				"\x00\x50\xF2"	/* WPA OUI */
#define WPA_OUI_TYPE			1
#define RSN_OUI				"\x00\x0F\xAC"	/* RSN OUI */
#define	WME_OUI_TYPE			2

#define VS_IE_FIXED_HDR_LEN		6
#define WPA_IE_VERSION_LEN		2
#define WPA_IE_MIN_OUI_LEN		4
#define WPA_IE_SUITE_COUNT_LEN		2

#define WPA_CIPHER_NONE			0	/* None */
#define WPA_CIPHER_WEP_40		1	/* WEP (40-bit) */
#define WPA_CIPHER_TKIP			2	/* TKIP: default for WPA */
#define WPA_CIPHER_AES_CCM		4	/* AES (CCM) */
#define WPA_CIPHER_WEP_104		5	/* WEP (104-bit) */

#define RSN_AKM_NONE			0	/* None (IBSS) */
#define RSN_AKM_UNSPECIFIED		1	/* Over 802.1x */
#define RSN_AKM_PSK			2	/* Pre-shared Key */
#define RSN_CAP_LEN			2	/* Length of RSN capabilities */
#define RSN_CAP_PTK_REPLAY_CNTR_MASK	0x000C

#define VNDR_IE_CMD_LEN			4	/* length of the set command
						 * string :"add", "del" (+ NUL)
						 */
#define VNDR_IE_COUNT_OFFSET		4
#define VNDR_IE_PKTFLAG_OFFSET		8
#define VNDR_IE_VSIE_OFFSET		12
#define VNDR_IE_HDR_SIZE		12
#define VNDR_IE_BEACON_FLAG		0x1
#define VNDR_IE_PRBRSP_FLAG		0x2
#define MAX_VNDR_IE_NUMBER		5

#define	DOT11_MGMT_HDR_LEN		24	/* d11 management header len */
#define	DOT11_BCN_PRB_FIXED_LEN		12	/* beacon/probe fixed length */

#define BRCMF_ASSOC_PARAMS_FIXED_SIZE \
	(sizeof(struct brcmf_assoc_params_le) - sizeof(u16))

static u32 brcmf_dbg_level = WL_DBG_ERR;

static bool check_vif_up(struct brcmf_cfg80211_vif *vif)
{
	if (!test_bit(BRCMF_VIF_STATUS_READY, &vif->sme_state)) {
		WL_INFO("device is not ready : status (%lu)\n",
			vif->sme_state);
		return false;
	}
	return true;
}

#define CHAN2G(_channel, _freq, _flags) {			\
	.band			= IEEE80211_BAND_2GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define CHAN5G(_channel, _flags) {				\
	.band			= IEEE80211_BAND_5GHZ,		\
	.center_freq		= 5000 + (5 * (_channel)),	\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define RATE_TO_BASE100KBPS(rate)   (((rate) * 10) / 2)
#define RATETAB_ENT(_rateid, _flags) \
	{                                                               \
		.bitrate        = RATE_TO_BASE100KBPS(_rateid),     \
		.hw_value       = (_rateid),                            \
		.flags          = (_flags),                             \
	}

static struct ieee80211_rate __wl_rates[] = {
	RATETAB_ENT(BRCM_RATE_1M, 0),
	RATETAB_ENT(BRCM_RATE_2M, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(BRCM_RATE_5M5, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(BRCM_RATE_11M, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(BRCM_RATE_6M, 0),
	RATETAB_ENT(BRCM_RATE_9M, 0),
	RATETAB_ENT(BRCM_RATE_12M, 0),
	RATETAB_ENT(BRCM_RATE_18M, 0),
	RATETAB_ENT(BRCM_RATE_24M, 0),
	RATETAB_ENT(BRCM_RATE_36M, 0),
	RATETAB_ENT(BRCM_RATE_48M, 0),
	RATETAB_ENT(BRCM_RATE_54M, 0),
};

#define wl_a_rates		(__wl_rates + 4)
#define wl_a_rates_size	8
#define wl_g_rates		(__wl_rates + 0)
#define wl_g_rates_size	12

static struct ieee80211_channel __wl_2ghz_channels[] = {
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

static struct ieee80211_channel __wl_5ghz_a_channels[] = {
	CHAN5G(34, 0), CHAN5G(36, 0),
	CHAN5G(38, 0), CHAN5G(40, 0),
	CHAN5G(42, 0), CHAN5G(44, 0),
	CHAN5G(46, 0), CHAN5G(48, 0),
	CHAN5G(52, 0), CHAN5G(56, 0),
	CHAN5G(60, 0), CHAN5G(64, 0),
	CHAN5G(100, 0), CHAN5G(104, 0),
	CHAN5G(108, 0), CHAN5G(112, 0),
	CHAN5G(116, 0), CHAN5G(120, 0),
	CHAN5G(124, 0), CHAN5G(128, 0),
	CHAN5G(132, 0), CHAN5G(136, 0),
	CHAN5G(140, 0), CHAN5G(149, 0),
	CHAN5G(153, 0), CHAN5G(157, 0),
	CHAN5G(161, 0), CHAN5G(165, 0),
	CHAN5G(184, 0), CHAN5G(188, 0),
	CHAN5G(192, 0), CHAN5G(196, 0),
	CHAN5G(200, 0), CHAN5G(204, 0),
	CHAN5G(208, 0), CHAN5G(212, 0),
	CHAN5G(216, 0),
};

static struct ieee80211_channel __wl_5ghz_n_channels[] = {
	CHAN5G(32, 0), CHAN5G(34, 0),
	CHAN5G(36, 0), CHAN5G(38, 0),
	CHAN5G(40, 0), CHAN5G(42, 0),
	CHAN5G(44, 0), CHAN5G(46, 0),
	CHAN5G(48, 0), CHAN5G(50, 0),
	CHAN5G(52, 0), CHAN5G(54, 0),
	CHAN5G(56, 0), CHAN5G(58, 0),
	CHAN5G(60, 0), CHAN5G(62, 0),
	CHAN5G(64, 0), CHAN5G(66, 0),
	CHAN5G(68, 0), CHAN5G(70, 0),
	CHAN5G(72, 0), CHAN5G(74, 0),
	CHAN5G(76, 0), CHAN5G(78, 0),
	CHAN5G(80, 0), CHAN5G(82, 0),
	CHAN5G(84, 0), CHAN5G(86, 0),
	CHAN5G(88, 0), CHAN5G(90, 0),
	CHAN5G(92, 0), CHAN5G(94, 0),
	CHAN5G(96, 0), CHAN5G(98, 0),
	CHAN5G(100, 0), CHAN5G(102, 0),
	CHAN5G(104, 0), CHAN5G(106, 0),
	CHAN5G(108, 0), CHAN5G(110, 0),
	CHAN5G(112, 0), CHAN5G(114, 0),
	CHAN5G(116, 0), CHAN5G(118, 0),
	CHAN5G(120, 0), CHAN5G(122, 0),
	CHAN5G(124, 0), CHAN5G(126, 0),
	CHAN5G(128, 0), CHAN5G(130, 0),
	CHAN5G(132, 0), CHAN5G(134, 0),
	CHAN5G(136, 0), CHAN5G(138, 0),
	CHAN5G(140, 0), CHAN5G(142, 0),
	CHAN5G(144, 0), CHAN5G(145, 0),
	CHAN5G(146, 0), CHAN5G(147, 0),
	CHAN5G(148, 0), CHAN5G(149, 0),
	CHAN5G(150, 0), CHAN5G(151, 0),
	CHAN5G(152, 0), CHAN5G(153, 0),
	CHAN5G(154, 0), CHAN5G(155, 0),
	CHAN5G(156, 0), CHAN5G(157, 0),
	CHAN5G(158, 0), CHAN5G(159, 0),
	CHAN5G(160, 0), CHAN5G(161, 0),
	CHAN5G(162, 0), CHAN5G(163, 0),
	CHAN5G(164, 0), CHAN5G(165, 0),
	CHAN5G(166, 0), CHAN5G(168, 0),
	CHAN5G(170, 0), CHAN5G(172, 0),
	CHAN5G(174, 0), CHAN5G(176, 0),
	CHAN5G(178, 0), CHAN5G(180, 0),
	CHAN5G(182, 0), CHAN5G(184, 0),
	CHAN5G(186, 0), CHAN5G(188, 0),
	CHAN5G(190, 0), CHAN5G(192, 0),
	CHAN5G(194, 0), CHAN5G(196, 0),
	CHAN5G(198, 0), CHAN5G(200, 0),
	CHAN5G(202, 0), CHAN5G(204, 0),
	CHAN5G(206, 0), CHAN5G(208, 0),
	CHAN5G(210, 0), CHAN5G(212, 0),
	CHAN5G(214, 0), CHAN5G(216, 0),
	CHAN5G(218, 0), CHAN5G(220, 0),
	CHAN5G(222, 0), CHAN5G(224, 0),
	CHAN5G(226, 0), CHAN5G(228, 0),
};

static struct ieee80211_supported_band __wl_band_2ghz = {
	.band = IEEE80211_BAND_2GHZ,
	.channels = __wl_2ghz_channels,
	.n_channels = ARRAY_SIZE(__wl_2ghz_channels),
	.bitrates = wl_g_rates,
	.n_bitrates = wl_g_rates_size,
};

static struct ieee80211_supported_band __wl_band_5ghz_a = {
	.band = IEEE80211_BAND_5GHZ,
	.channels = __wl_5ghz_a_channels,
	.n_channels = ARRAY_SIZE(__wl_5ghz_a_channels),
	.bitrates = wl_a_rates,
	.n_bitrates = wl_a_rates_size,
};

static struct ieee80211_supported_band __wl_band_5ghz_n = {
	.band = IEEE80211_BAND_5GHZ,
	.channels = __wl_5ghz_n_channels,
	.n_channels = ARRAY_SIZE(__wl_5ghz_n_channels),
	.bitrates = wl_a_rates,
	.n_bitrates = wl_a_rates_size,
};

static const u32 __wl_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
	WLAN_CIPHER_SUITE_AES_CMAC,
};

/* tag_ID/length/value_buffer tuple */
struct brcmf_tlv {
	u8 id;
	u8 len;
	u8 data[1];
};

/* Vendor specific ie. id = 221, oui and type defines exact ie */
struct brcmf_vs_tlv {
	u8 id;
	u8 len;
	u8 oui[3];
	u8 oui_type;
};

struct parsed_vndr_ie_info {
	u8 *ie_ptr;
	u32 ie_len;	/* total length including id & length field */
	struct brcmf_vs_tlv vndrie;
};

struct parsed_vndr_ies {
	u32 count;
	struct parsed_vndr_ie_info ie_info[MAX_VNDR_IE_NUMBER];
};

/* Quarter dBm units to mW
 * Table starts at QDBM_OFFSET, so the first entry is mW for qdBm=153
 * Table is offset so the last entry is largest mW value that fits in
 * a u16.
 */

#define QDBM_OFFSET 153		/* Offset for first entry */
#define QDBM_TABLE_LEN 40	/* Table size */

/* Smallest mW value that will round up to the first table entry, QDBM_OFFSET.
 * Value is ( mW(QDBM_OFFSET - 1) + mW(QDBM_OFFSET) ) / 2
 */
#define QDBM_TABLE_LOW_BOUND 6493	/* Low bound */

/* Largest mW value that will round down to the last table entry,
 * QDBM_OFFSET + QDBM_TABLE_LEN-1.
 * Value is ( mW(QDBM_OFFSET + QDBM_TABLE_LEN - 1) +
 * mW(QDBM_OFFSET + QDBM_TABLE_LEN) ) / 2.
 */
#define QDBM_TABLE_HIGH_BOUND 64938	/* High bound */

static const u16 nqdBm_to_mW_map[QDBM_TABLE_LEN] = {
/* qdBm:	+0	+1	+2	+3	+4	+5	+6	+7 */
/* 153: */ 6683, 7079, 7499, 7943, 8414, 8913, 9441, 10000,
/* 161: */ 10593, 11220, 11885, 12589, 13335, 14125, 14962, 15849,
/* 169: */ 16788, 17783, 18836, 19953, 21135, 22387, 23714, 25119,
/* 177: */ 26607, 28184, 29854, 31623, 33497, 35481, 37584, 39811,
/* 185: */ 42170, 44668, 47315, 50119, 53088, 56234, 59566, 63096
};

static u16 brcmf_qdbm_to_mw(u8 qdbm)
{
	uint factor = 1;
	int idx = qdbm - QDBM_OFFSET;

	if (idx >= QDBM_TABLE_LEN)
		/* clamp to max u16 mW value */
		return 0xFFFF;

	/* scale the qdBm index up to the range of the table 0-40
	 * where an offset of 40 qdBm equals a factor of 10 mW.
	 */
	while (idx < 0) {
		idx += 40;
		factor *= 10;
	}

	/* return the mW value scaled down to the correct factor of 10,
	 * adding in factor/2 to get proper rounding.
	 */
	return (nqdBm_to_mW_map[idx] + factor / 2) / factor;
}

static u8 brcmf_mw_to_qdbm(u16 mw)
{
	u8 qdbm;
	int offset;
	uint mw_uint = mw;
	uint boundary;

	/* handle boundary case */
	if (mw_uint <= 1)
		return 0;

	offset = QDBM_OFFSET;

	/* move mw into the range of the table */
	while (mw_uint < QDBM_TABLE_LOW_BOUND) {
		mw_uint *= 10;
		offset -= 40;
	}

	for (qdbm = 0; qdbm < QDBM_TABLE_LEN - 1; qdbm++) {
		boundary = nqdBm_to_mW_map[qdbm] + (nqdBm_to_mW_map[qdbm + 1] -
						    nqdBm_to_mW_map[qdbm]) / 2;
		if (mw_uint < boundary)
			break;
	}

	qdbm += (u8) offset;

	return qdbm;
}

static u16 channel_to_chanspec(struct ieee80211_channel *ch)
{
	u16 chanspec;

	chanspec = ieee80211_frequency_to_channel(ch->center_freq);
	chanspec &= WL_CHANSPEC_CHAN_MASK;

	if (ch->band == IEEE80211_BAND_2GHZ)
		chanspec |= WL_CHANSPEC_BAND_2G;
	else
		chanspec |= WL_CHANSPEC_BAND_5G;

	if (ch->flags & IEEE80211_CHAN_NO_HT40) {
		chanspec |= WL_CHANSPEC_BW_20;
		chanspec |= WL_CHANSPEC_CTL_SB_NONE;
	} else {
		chanspec |= WL_CHANSPEC_BW_40;
		if (ch->flags & IEEE80211_CHAN_NO_HT40PLUS)
			chanspec |= WL_CHANSPEC_CTL_SB_LOWER;
		else
			chanspec |= WL_CHANSPEC_CTL_SB_UPPER;
	}
	return chanspec;
}

static void convert_key_from_CPU(struct brcmf_wsec_key *key,
				 struct brcmf_wsec_key_le *key_le)
{
	key_le->index = cpu_to_le32(key->index);
	key_le->len = cpu_to_le32(key->len);
	key_le->algo = cpu_to_le32(key->algo);
	key_le->flags = cpu_to_le32(key->flags);
	key_le->rxiv.hi = cpu_to_le32(key->rxiv.hi);
	key_le->rxiv.lo = cpu_to_le16(key->rxiv.lo);
	key_le->iv_initialized = cpu_to_le32(key->iv_initialized);
	memcpy(key_le->data, key->data, sizeof(key->data));
	memcpy(key_le->ea, key->ea, sizeof(key->ea));
}

static int
send_key_to_dongle(struct net_device *ndev, struct brcmf_wsec_key *key)
{
	int err;
	struct brcmf_wsec_key_le key_le;

	convert_key_from_CPU(key, &key_le);

	brcmf_netdev_wait_pend8021x(ndev);

	err = brcmf_fil_bsscfg_data_set(netdev_priv(ndev), "wsec_key", &key_le,
					sizeof(key_le));

	if (err)
		WL_ERR("wsec_key error (%d)\n", err);
	return err;
}

static s32
brcmf_cfg80211_change_iface(struct wiphy *wiphy, struct net_device *ndev,
			 enum nl80211_iftype type, u32 *flags,
			 struct vif_params *params)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	s32 infra = 0;
	s32 ap = 0;
	s32 err = 0;

	WL_TRACE("Enter, ndev=%p, type=%d\n", ndev, type);

	switch (type) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_WDS:
		WL_ERR("type (%d) : currently we do not support this type\n",
		       type);
		return -EOPNOTSUPP;
	case NL80211_IFTYPE_ADHOC:
		cfg->conf->mode = WL_MODE_IBSS;
		infra = 0;
		break;
	case NL80211_IFTYPE_STATION:
		cfg->conf->mode = WL_MODE_BSS;
		infra = 1;
		break;
	case NL80211_IFTYPE_AP:
		cfg->conf->mode = WL_MODE_AP;
		ap = 1;
		break;
	default:
		err = -EINVAL;
		goto done;
	}

	if (ap) {
		set_bit(BRCMF_VIF_STATUS_AP_CREATING, &ifp->vif->sme_state);
		WL_INFO("IF Type = AP\n");
	} else {
		err = brcmf_fil_cmd_int_set(netdev_priv(ndev),
					    BRCMF_C_SET_INFRA, infra);
		if (err) {
			WL_ERR("WLC_SET_INFRA error (%d)\n", err);
			err = -EAGAIN;
			goto done;
		}
		WL_INFO("IF Type = %s\n",
			(cfg->conf->mode == WL_MODE_IBSS) ?
			"Adhoc" : "Infra");
	}
	ndev->ieee80211_ptr->iftype = type;

done:
	WL_TRACE("Exit\n");

	return err;
}

static void brcmf_set_mpc(struct net_device *ndev, int mpc)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err = 0;

	if (check_vif_up(ifp->vif)) {
		err = brcmf_fil_iovar_int_set(ifp, "mpc", mpc);
		if (err) {
			WL_ERR("fail to set mpc\n");
			return;
		}
		WL_INFO("MPC : %d\n", mpc);
	}
}

static void brcmf_escan_prep(struct brcmf_scan_params_le *params_le,
			     struct cfg80211_scan_request *request)
{
	u32 n_ssids;
	u32 n_channels;
	s32 i;
	s32 offset;
	u16 chanspec;
	char *ptr;
	struct brcmf_ssid_le ssid_le;

	memset(params_le->bssid, 0xFF, ETH_ALEN);
	params_le->bss_type = DOT11_BSSTYPE_ANY;
	params_le->scan_type = 0;
	params_le->channel_num = 0;
	params_le->nprobes = cpu_to_le32(-1);
	params_le->active_time = cpu_to_le32(-1);
	params_le->passive_time = cpu_to_le32(-1);
	params_le->home_time = cpu_to_le32(-1);
	memset(&params_le->ssid_le, 0, sizeof(params_le->ssid_le));

	/* if request is null exit so it will be all channel broadcast scan */
	if (!request)
		return;

	n_ssids = request->n_ssids;
	n_channels = request->n_channels;
	/* Copy channel array if applicable */
	WL_SCAN("### List of channelspecs to scan ### %d\n", n_channels);
	if (n_channels > 0) {
		for (i = 0; i < n_channels; i++) {
			chanspec = channel_to_chanspec(request->channels[i]);
			WL_SCAN("Chan : %d, Channel spec: %x\n",
				request->channels[i]->hw_value, chanspec);
			params_le->channel_list[i] = cpu_to_le16(chanspec);
		}
	} else {
		WL_SCAN("Scanning all channels\n");
	}
	/* Copy ssid array if applicable */
	WL_SCAN("### List of SSIDs to scan ### %d\n", n_ssids);
	if (n_ssids > 0) {
		offset = offsetof(struct brcmf_scan_params_le, channel_list) +
				n_channels * sizeof(u16);
		offset = roundup(offset, sizeof(u32));
		ptr = (char *)params_le + offset;
		for (i = 0; i < n_ssids; i++) {
			memset(&ssid_le, 0, sizeof(ssid_le));
			ssid_le.SSID_len =
					cpu_to_le32(request->ssids[i].ssid_len);
			memcpy(ssid_le.SSID, request->ssids[i].ssid,
			       request->ssids[i].ssid_len);
			if (!ssid_le.SSID_len)
				WL_SCAN("%d: Broadcast scan\n", i);
			else
				WL_SCAN("%d: scan for  %s size =%d\n", i,
					ssid_le.SSID, ssid_le.SSID_len);
			memcpy(ptr, &ssid_le, sizeof(ssid_le));
			ptr += sizeof(ssid_le);
		}
	} else {
		WL_SCAN("Broadcast scan %p\n", request->ssids);
		if ((request->ssids) && request->ssids->ssid_len) {
			WL_SCAN("SSID %s len=%d\n", params_le->ssid_le.SSID,
				request->ssids->ssid_len);
			params_le->ssid_le.SSID_len =
				cpu_to_le32(request->ssids->ssid_len);
			memcpy(&params_le->ssid_le.SSID, request->ssids->ssid,
				request->ssids->ssid_len);
		}
	}
	/* Adding mask to channel numbers */
	params_le->channel_num =
		cpu_to_le32((n_ssids << BRCMF_SCAN_PARAMS_NSSID_SHIFT) |
			(n_channels & BRCMF_SCAN_PARAMS_COUNT_MASK));
}

static s32
brcmf_notify_escan_complete(struct brcmf_cfg80211_info *cfg,
			    struct net_device *ndev,
			    bool aborted, bool fw_abort)
{
	struct brcmf_scan_params_le params_le;
	struct cfg80211_scan_request *scan_request;
	s32 err = 0;

	WL_SCAN("Enter\n");

	/* clear scan request, because the FW abort can cause a second call */
	/* to this functon and might cause a double cfg80211_scan_done      */
	scan_request = cfg->scan_request;
	cfg->scan_request = NULL;

	if (timer_pending(&cfg->escan_timeout))
		del_timer_sync(&cfg->escan_timeout);

	if (fw_abort) {
		/* Do a scan abort to stop the driver's scan engine */
		WL_SCAN("ABORT scan in firmware\n");
		memset(&params_le, 0, sizeof(params_le));
		memset(params_le.bssid, 0xFF, ETH_ALEN);
		params_le.bss_type = DOT11_BSSTYPE_ANY;
		params_le.scan_type = 0;
		params_le.channel_num = cpu_to_le32(1);
		params_le.nprobes = cpu_to_le32(1);
		params_le.active_time = cpu_to_le32(-1);
		params_le.passive_time = cpu_to_le32(-1);
		params_le.home_time = cpu_to_le32(-1);
		/* Scan is aborted by setting channel_list[0] to -1 */
		params_le.channel_list[0] = cpu_to_le16(-1);
		/* E-Scan (or anyother type) can be aborted by SCAN */
		err = brcmf_fil_cmd_data_set(netdev_priv(ndev), BRCMF_C_SCAN,
					     &params_le, sizeof(params_le));
		if (err)
			WL_ERR("Scan abort  failed\n");
	}
	/*
	 * e-scan can be initiated by scheduled scan
	 * which takes precedence.
	 */
	if (cfg->sched_escan) {
		WL_SCAN("scheduled scan completed\n");
		cfg->sched_escan = false;
		if (!aborted)
			cfg80211_sched_scan_results(cfg_to_wiphy(cfg));
		brcmf_set_mpc(ndev, 1);
	} else if (scan_request) {
		WL_SCAN("ESCAN Completed scan: %s\n",
				aborted ? "Aborted" : "Done");
		cfg80211_scan_done(scan_request, aborted);
		brcmf_set_mpc(ndev, 1);
	}
	if (!test_and_clear_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status)) {
		WL_ERR("Scan complete while device not scanning\n");
		return -EPERM;
	}

	return err;
}

static s32
brcmf_run_escan(struct brcmf_cfg80211_info *cfg, struct net_device *ndev,
		struct cfg80211_scan_request *request, u16 action)
{
	s32 params_size = BRCMF_SCAN_PARAMS_FIXED_SIZE +
			  offsetof(struct brcmf_escan_params_le, params_le);
	struct brcmf_escan_params_le *params;
	s32 err = 0;

	WL_SCAN("E-SCAN START\n");

	if (request != NULL) {
		/* Allocate space for populating ssids in struct */
		params_size += sizeof(u32) * ((request->n_channels + 1) / 2);

		/* Allocate space for populating ssids in struct */
		params_size += sizeof(struct brcmf_ssid) * request->n_ssids;
	}

	params = kzalloc(params_size, GFP_KERNEL);
	if (!params) {
		err = -ENOMEM;
		goto exit;
	}
	BUG_ON(params_size + sizeof("escan") >= BRCMF_DCMD_MEDLEN);
	brcmf_escan_prep(&params->params_le, request);
	params->version = cpu_to_le32(BRCMF_ESCAN_REQ_VERSION);
	params->action = cpu_to_le16(action);
	params->sync_id = cpu_to_le16(0x1234);

	err = brcmf_fil_iovar_data_set(netdev_priv(ndev), "escan",
				       params, params_size);
	if (err) {
		if (err == -EBUSY)
			WL_INFO("system busy : escan canceled\n");
		else
			WL_ERR("error (%d)\n", err);
	}

	kfree(params);
exit:
	return err;
}

static s32
brcmf_do_escan(struct brcmf_cfg80211_info *cfg, struct wiphy *wiphy,
	       struct net_device *ndev, struct cfg80211_scan_request *request)
{
	s32 err;
	u32 passive_scan;
	struct brcmf_scan_results *results;

	WL_SCAN("Enter\n");
	cfg->escan_info.ndev = ndev;
	cfg->escan_info.wiphy = wiphy;
	cfg->escan_info.escan_state = WL_ESCAN_STATE_SCANNING;
	passive_scan = cfg->active_scan ? 0 : 1;
	err = brcmf_fil_cmd_int_set(netdev_priv(ndev), BRCMF_C_SET_PASSIVE_SCAN,
				    passive_scan);
	if (err) {
		WL_ERR("error (%d)\n", err);
		return err;
	}
	brcmf_set_mpc(ndev, 0);
	results = (struct brcmf_scan_results *)cfg->escan_info.escan_buf;
	results->version = 0;
	results->count = 0;
	results->buflen = WL_ESCAN_RESULTS_FIXED_SIZE;

	err = brcmf_run_escan(cfg, ndev, request, WL_ESCAN_ACTION_START);
	if (err)
		brcmf_set_mpc(ndev, 1);
	return err;
}

static s32
brcmf_cfg80211_escan(struct wiphy *wiphy, struct net_device *ndev,
		     struct cfg80211_scan_request *request,
		     struct cfg80211_ssid *this_ssid)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_info *cfg = ndev_to_cfg(ndev);
	struct cfg80211_ssid *ssids;
	struct brcmf_cfg80211_scan_req *sr = &cfg->scan_req_int;
	u32 passive_scan;
	bool escan_req;
	bool spec_scan;
	s32 err;
	u32 SSID_len;

	WL_SCAN("START ESCAN\n");

	if (test_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status)) {
		WL_ERR("Scanning already: status (%lu)\n", cfg->scan_status);
		return -EAGAIN;
	}
	if (test_bit(BRCMF_SCAN_STATUS_ABORT, &cfg->scan_status)) {
		WL_ERR("Scanning being aborted: status (%lu)\n",
		       cfg->scan_status);
		return -EAGAIN;
	}
	if (test_bit(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state)) {
		WL_ERR("Connecting: status (%lu)\n", ifp->vif->sme_state);
		return -EAGAIN;
	}

	/* Arm scan timeout timer */
	mod_timer(&cfg->escan_timeout, jiffies +
			WL_ESCAN_TIMER_INTERVAL_MS * HZ / 1000);

	escan_req = false;
	if (request) {
		/* scan bss */
		ssids = request->ssids;
		escan_req = true;
	} else {
		/* scan in ibss */
		/* we don't do escan in ibss */
		ssids = this_ssid;
	}

	cfg->scan_request = request;
	set_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status);
	if (escan_req) {
		err = brcmf_do_escan(cfg, wiphy, ndev, request);
		if (err)
			goto scan_out;
	} else {
		WL_SCAN("ssid \"%s\", ssid_len (%d)\n",
		       ssids->ssid, ssids->ssid_len);
		memset(&sr->ssid_le, 0, sizeof(sr->ssid_le));
		SSID_len = min_t(u8, sizeof(sr->ssid_le.SSID), ssids->ssid_len);
		sr->ssid_le.SSID_len = cpu_to_le32(0);
		spec_scan = false;
		if (SSID_len) {
			memcpy(sr->ssid_le.SSID, ssids->ssid, SSID_len);
			sr->ssid_le.SSID_len = cpu_to_le32(SSID_len);
			spec_scan = true;
		} else
			WL_SCAN("Broadcast scan\n");

		passive_scan = cfg->active_scan ? 0 : 1;
		err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_PASSIVE_SCAN,
					    passive_scan);
		if (err) {
			WL_ERR("WLC_SET_PASSIVE_SCAN error (%d)\n", err);
			goto scan_out;
		}
		brcmf_set_mpc(ndev, 0);
		err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SCAN,
					     &sr->ssid_le, sizeof(sr->ssid_le));
		if (err) {
			if (err == -EBUSY)
				WL_INFO("BUSY: scan for \"%s\" canceled\n",
					sr->ssid_le.SSID);
			else
				WL_ERR("WLC_SCAN error (%d)\n", err);

			brcmf_set_mpc(ndev, 1);
			goto scan_out;
		}
	}

	return 0;

scan_out:
	clear_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status);
	if (timer_pending(&cfg->escan_timeout))
		del_timer_sync(&cfg->escan_timeout);
	cfg->scan_request = NULL;
	return err;
}

static s32
brcmf_cfg80211_scan(struct wiphy *wiphy, struct cfg80211_scan_request *request)
{
	struct net_device *ndev = request->wdev->netdev;
	s32 err = 0;

	WL_TRACE("Enter\n");

	if (!check_vif_up(container_of(request->wdev,
				       struct brcmf_cfg80211_vif, wdev)))
		return -EIO;

	err = brcmf_cfg80211_escan(wiphy, ndev, request, NULL);

	if (err)
		WL_ERR("scan error (%d)\n", err);

	WL_TRACE("Exit\n");
	return err;
}

static s32 brcmf_set_rts(struct net_device *ndev, u32 rts_threshold)
{
	s32 err = 0;

	err = brcmf_fil_iovar_int_set(netdev_priv(ndev), "rtsthresh",
				      rts_threshold);
	if (err)
		WL_ERR("Error (%d)\n", err);

	return err;
}

static s32 brcmf_set_frag(struct net_device *ndev, u32 frag_threshold)
{
	s32 err = 0;

	err = brcmf_fil_iovar_int_set(netdev_priv(ndev), "fragthresh",
				      frag_threshold);
	if (err)
		WL_ERR("Error (%d)\n", err);

	return err;
}

static s32 brcmf_set_retry(struct net_device *ndev, u32 retry, bool l)
{
	s32 err = 0;
	u32 cmd = (l ? BRCM_SET_LRL : BRCM_SET_SRL);

	err = brcmf_fil_cmd_int_set(netdev_priv(ndev), cmd, retry);
	if (err) {
		WL_ERR("cmd (%d) , error (%d)\n", cmd, err);
		return err;
	}
	return err;
}

static s32 brcmf_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct net_device *ndev = cfg_to_ndev(cfg);
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err = 0;

	WL_TRACE("Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	if (changed & WIPHY_PARAM_RTS_THRESHOLD &&
	    (cfg->conf->rts_threshold != wiphy->rts_threshold)) {
		cfg->conf->rts_threshold = wiphy->rts_threshold;
		err = brcmf_set_rts(ndev, cfg->conf->rts_threshold);
		if (!err)
			goto done;
	}
	if (changed & WIPHY_PARAM_FRAG_THRESHOLD &&
	    (cfg->conf->frag_threshold != wiphy->frag_threshold)) {
		cfg->conf->frag_threshold = wiphy->frag_threshold;
		err = brcmf_set_frag(ndev, cfg->conf->frag_threshold);
		if (!err)
			goto done;
	}
	if (changed & WIPHY_PARAM_RETRY_LONG
	    && (cfg->conf->retry_long != wiphy->retry_long)) {
		cfg->conf->retry_long = wiphy->retry_long;
		err = brcmf_set_retry(ndev, cfg->conf->retry_long, true);
		if (!err)
			goto done;
	}
	if (changed & WIPHY_PARAM_RETRY_SHORT
	    && (cfg->conf->retry_short != wiphy->retry_short)) {
		cfg->conf->retry_short = wiphy->retry_short;
		err = brcmf_set_retry(ndev, cfg->conf->retry_short, false);
		if (!err)
			goto done;
	}

done:
	WL_TRACE("Exit\n");
	return err;
}

static void brcmf_init_prof(struct brcmf_cfg80211_profile *prof)
{
	memset(prof, 0, sizeof(*prof));
}

static void brcmf_ch_to_chanspec(int ch, struct brcmf_join_params *join_params,
	size_t *join_params_size)
{
	u16 chanspec = 0;

	if (ch != 0) {
		if (ch <= CH_MAX_2G_CHANNEL)
			chanspec |= WL_CHANSPEC_BAND_2G;
		else
			chanspec |= WL_CHANSPEC_BAND_5G;

		chanspec |= WL_CHANSPEC_BW_20;
		chanspec |= WL_CHANSPEC_CTL_SB_NONE;

		*join_params_size += BRCMF_ASSOC_PARAMS_FIXED_SIZE +
				     sizeof(u16);

		chanspec |= (ch & WL_CHANSPEC_CHAN_MASK);
		join_params->params_le.chanspec_list[0] = cpu_to_le16(chanspec);
		join_params->params_le.chanspec_num = cpu_to_le32(1);

		WL_CONN("join_params->params.chanspec_list[0]= %#X,"
			"channel %d, chanspec %#X\n",
			chanspec, ch, chanspec);
	}
}

static void brcmf_link_down(struct brcmf_cfg80211_info *cfg)
{
	struct net_device *ndev = NULL;
	s32 err = 0;

	WL_TRACE("Enter\n");

	if (cfg->link_up) {
		ndev = cfg_to_ndev(cfg);
		WL_INFO("Call WLC_DISASSOC to stop excess roaming\n ");
		err = brcmf_fil_cmd_data_set(netdev_priv(ndev),
					     BRCMF_C_DISASSOC, NULL, 0);
		if (err)
			WL_ERR("WLC_DISASSOC failed (%d)\n", err);
		cfg->link_up = false;
	}
	WL_TRACE("Exit\n");
}

static s32
brcmf_cfg80211_join_ibss(struct wiphy *wiphy, struct net_device *ndev,
		      struct cfg80211_ibss_params *params)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_profile *profile = &ifp->vif->profile;
	struct brcmf_join_params join_params;
	size_t join_params_size = 0;
	s32 err = 0;
	s32 wsec = 0;
	s32 bcnprd;

	WL_TRACE("Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	if (params->ssid)
		WL_CONN("SSID: %s\n", params->ssid);
	else {
		WL_CONN("SSID: NULL, Not supported\n");
		return -EOPNOTSUPP;
	}

	set_bit(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state);

	if (params->bssid)
		WL_CONN("BSSID: %pM\n", params->bssid);
	else
		WL_CONN("No BSSID specified\n");

	if (params->channel)
		WL_CONN("channel: %d\n", params->channel->center_freq);
	else
		WL_CONN("no channel specified\n");

	if (params->channel_fixed)
		WL_CONN("fixed channel required\n");
	else
		WL_CONN("no fixed channel required\n");

	if (params->ie && params->ie_len)
		WL_CONN("ie len: %d\n", params->ie_len);
	else
		WL_CONN("no ie specified\n");

	if (params->beacon_interval)
		WL_CONN("beacon interval: %d\n", params->beacon_interval);
	else
		WL_CONN("no beacon interval specified\n");

	if (params->basic_rates)
		WL_CONN("basic rates: %08X\n", params->basic_rates);
	else
		WL_CONN("no basic rates specified\n");

	if (params->privacy)
		WL_CONN("privacy required\n");
	else
		WL_CONN("no privacy required\n");

	/* Configure Privacy for starter */
	if (params->privacy)
		wsec |= WEP_ENABLED;

	err = brcmf_fil_iovar_int_set(ifp, "wsec", wsec);
	if (err) {
		WL_ERR("wsec failed (%d)\n", err);
		goto done;
	}

	/* Configure Beacon Interval for starter */
	if (params->beacon_interval)
		bcnprd = params->beacon_interval;
	else
		bcnprd = 100;

	err = brcmf_fil_cmd_int_set(ifp, BRCM_SET_BCNPRD, bcnprd);
	if (err) {
		WL_ERR("WLC_SET_BCNPRD failed (%d)\n", err);
		goto done;
	}

	/* Configure required join parameter */
	memset(&join_params, 0, sizeof(struct brcmf_join_params));

	/* SSID */
	profile->ssid.SSID_len = min_t(u32, params->ssid_len, 32);
	memcpy(profile->ssid.SSID, params->ssid, profile->ssid.SSID_len);
	memcpy(join_params.ssid_le.SSID, params->ssid, profile->ssid.SSID_len);
	join_params.ssid_le.SSID_len = cpu_to_le32(profile->ssid.SSID_len);
	join_params_size = sizeof(join_params.ssid_le);

	/* BSSID */
	if (params->bssid) {
		memcpy(join_params.params_le.bssid, params->bssid, ETH_ALEN);
		join_params_size = sizeof(join_params.ssid_le) +
				   BRCMF_ASSOC_PARAMS_FIXED_SIZE;
		memcpy(profile->bssid, params->bssid, ETH_ALEN);
	} else {
		memset(join_params.params_le.bssid, 0xFF, ETH_ALEN);
		memset(profile->bssid, 0, ETH_ALEN);
	}

	/* Channel */
	if (params->channel) {
		u32 target_channel;

		cfg->channel =
			ieee80211_frequency_to_channel(
				params->channel->center_freq);
		if (params->channel_fixed) {
			/* adding chanspec */
			brcmf_ch_to_chanspec(cfg->channel,
				&join_params, &join_params_size);
		}

		/* set channel for starter */
		target_channel = cfg->channel;
		err = brcmf_fil_cmd_int_set(ifp, BRCM_SET_CHANNEL,
					    target_channel);
		if (err) {
			WL_ERR("WLC_SET_CHANNEL failed (%d)\n", err);
			goto done;
		}
	} else
		cfg->channel = 0;

	cfg->ibss_starter = false;


	err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_SSID,
				     &join_params, join_params_size);
	if (err) {
		WL_ERR("WLC_SET_SSID failed (%d)\n", err);
		goto done;
	}

done:
	if (err)
		clear_bit(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state);
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_leave_ibss(struct wiphy *wiphy, struct net_device *ndev)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err = 0;

	WL_TRACE("Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	brcmf_link_down(cfg);

	WL_TRACE("Exit\n");

	return err;
}

static s32 brcmf_set_wpa_version(struct net_device *ndev,
				 struct cfg80211_connect_params *sme)
{
	struct brcmf_cfg80211_profile *profile = ndev_to_prof(ndev);
	struct brcmf_cfg80211_security *sec;
	s32 val = 0;
	s32 err = 0;

	if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_1)
		val = WPA_AUTH_PSK | WPA_AUTH_UNSPECIFIED;
	else if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_2)
		val = WPA2_AUTH_PSK | WPA2_AUTH_UNSPECIFIED;
	else
		val = WPA_AUTH_DISABLED;
	WL_CONN("setting wpa_auth to 0x%0x\n", val);
	err = brcmf_fil_iovar_int_set(netdev_priv(ndev), "wpa_auth", val);
	if (err) {
		WL_ERR("set wpa_auth failed (%d)\n", err);
		return err;
	}
	sec = &profile->sec;
	sec->wpa_versions = sme->crypto.wpa_versions;
	return err;
}

static s32 brcmf_set_auth_type(struct net_device *ndev,
			       struct cfg80211_connect_params *sme)
{
	struct brcmf_cfg80211_profile *profile = ndev_to_prof(ndev);
	struct brcmf_cfg80211_security *sec;
	s32 val = 0;
	s32 err = 0;

	switch (sme->auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		val = 0;
		WL_CONN("open system\n");
		break;
	case NL80211_AUTHTYPE_SHARED_KEY:
		val = 1;
		WL_CONN("shared key\n");
		break;
	case NL80211_AUTHTYPE_AUTOMATIC:
		val = 2;
		WL_CONN("automatic\n");
		break;
	case NL80211_AUTHTYPE_NETWORK_EAP:
		WL_CONN("network eap\n");
	default:
		val = 2;
		WL_ERR("invalid auth type (%d)\n", sme->auth_type);
		break;
	}

	err = brcmf_fil_iovar_int_set(netdev_priv(ndev), "auth", val);
	if (err) {
		WL_ERR("set auth failed (%d)\n", err);
		return err;
	}
	sec = &profile->sec;
	sec->auth_type = sme->auth_type;
	return err;
}

static s32
brcmf_set_set_cipher(struct net_device *ndev,
		     struct cfg80211_connect_params *sme)
{
	struct brcmf_cfg80211_profile *profile = ndev_to_prof(ndev);
	struct brcmf_cfg80211_security *sec;
	s32 pval = 0;
	s32 gval = 0;
	s32 err = 0;

	if (sme->crypto.n_ciphers_pairwise) {
		switch (sme->crypto.ciphers_pairwise[0]) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			pval = WEP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			pval = TKIP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			pval = AES_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			pval = AES_ENABLED;
			break;
		default:
			WL_ERR("invalid cipher pairwise (%d)\n",
			       sme->crypto.ciphers_pairwise[0]);
			return -EINVAL;
		}
	}
	if (sme->crypto.cipher_group) {
		switch (sme->crypto.cipher_group) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			gval = WEP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			gval = TKIP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			gval = AES_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			gval = AES_ENABLED;
			break;
		default:
			WL_ERR("invalid cipher group (%d)\n",
			       sme->crypto.cipher_group);
			return -EINVAL;
		}
	}

	WL_CONN("pval (%d) gval (%d)\n", pval, gval);
	err = brcmf_fil_iovar_int_set(netdev_priv(ndev), "wsec", pval | gval);
	if (err) {
		WL_ERR("error (%d)\n", err);
		return err;
	}

	sec = &profile->sec;
	sec->cipher_pairwise = sme->crypto.ciphers_pairwise[0];
	sec->cipher_group = sme->crypto.cipher_group;

	return err;
}

static s32
brcmf_set_key_mgmt(struct net_device *ndev, struct cfg80211_connect_params *sme)
{
	struct brcmf_cfg80211_profile *profile = ndev_to_prof(ndev);
	struct brcmf_cfg80211_security *sec;
	s32 val = 0;
	s32 err = 0;

	if (sme->crypto.n_akm_suites) {
		err = brcmf_fil_iovar_int_get(netdev_priv(ndev),
					      "wpa_auth", &val);
		if (err) {
			WL_ERR("could not get wpa_auth (%d)\n", err);
			return err;
		}
		if (val & (WPA_AUTH_PSK | WPA_AUTH_UNSPECIFIED)) {
			switch (sme->crypto.akm_suites[0]) {
			case WLAN_AKM_SUITE_8021X:
				val = WPA_AUTH_UNSPECIFIED;
				break;
			case WLAN_AKM_SUITE_PSK:
				val = WPA_AUTH_PSK;
				break;
			default:
				WL_ERR("invalid cipher group (%d)\n",
				       sme->crypto.cipher_group);
				return -EINVAL;
			}
		} else if (val & (WPA2_AUTH_PSK | WPA2_AUTH_UNSPECIFIED)) {
			switch (sme->crypto.akm_suites[0]) {
			case WLAN_AKM_SUITE_8021X:
				val = WPA2_AUTH_UNSPECIFIED;
				break;
			case WLAN_AKM_SUITE_PSK:
				val = WPA2_AUTH_PSK;
				break;
			default:
				WL_ERR("invalid cipher group (%d)\n",
				       sme->crypto.cipher_group);
				return -EINVAL;
			}
		}

		WL_CONN("setting wpa_auth to %d\n", val);
		err = brcmf_fil_iovar_int_set(netdev_priv(ndev),
					      "wpa_auth", val);
		if (err) {
			WL_ERR("could not set wpa_auth (%d)\n", err);
			return err;
		}
	}
	sec = &profile->sec;
	sec->wpa_auth = sme->crypto.akm_suites[0];

	return err;
}

static s32
brcmf_set_sharedkey(struct net_device *ndev,
		    struct cfg80211_connect_params *sme)
{
	struct brcmf_cfg80211_profile *profile = ndev_to_prof(ndev);
	struct brcmf_cfg80211_security *sec;
	struct brcmf_wsec_key key;
	s32 val;
	s32 err = 0;

	WL_CONN("key len (%d)\n", sme->key_len);

	if (sme->key_len == 0)
		return 0;

	sec = &profile->sec;
	WL_CONN("wpa_versions 0x%x cipher_pairwise 0x%x\n",
		sec->wpa_versions, sec->cipher_pairwise);

	if (sec->wpa_versions & (NL80211_WPA_VERSION_1 | NL80211_WPA_VERSION_2))
		return 0;

	if (!(sec->cipher_pairwise &
	    (WLAN_CIPHER_SUITE_WEP40 | WLAN_CIPHER_SUITE_WEP104)))
		return 0;

	memset(&key, 0, sizeof(key));
	key.len = (u32) sme->key_len;
	key.index = (u32) sme->key_idx;
	if (key.len > sizeof(key.data)) {
		WL_ERR("Too long key length (%u)\n", key.len);
		return -EINVAL;
	}
	memcpy(key.data, sme->key, key.len);
	key.flags = BRCMF_PRIMARY_KEY;
	switch (sec->cipher_pairwise) {
	case WLAN_CIPHER_SUITE_WEP40:
		key.algo = CRYPTO_ALGO_WEP1;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		key.algo = CRYPTO_ALGO_WEP128;
		break;
	default:
		WL_ERR("Invalid algorithm (%d)\n",
		       sme->crypto.ciphers_pairwise[0]);
		return -EINVAL;
	}
	/* Set the new key/index */
	WL_CONN("key length (%d) key index (%d) algo (%d)\n",
		key.len, key.index, key.algo);
	WL_CONN("key \"%s\"\n", key.data);
	err = send_key_to_dongle(ndev, &key);
	if (err)
		return err;

	if (sec->auth_type == NL80211_AUTHTYPE_SHARED_KEY) {
		WL_CONN("set auth_type to shared key\n");
		val = WL_AUTH_SHARED_KEY;	/* shared key */
		err = brcmf_fil_bsscfg_int_set(netdev_priv(ndev), "auth", val);
		if (err)
			WL_ERR("set auth failed (%d)\n", err);
	}
	return err;
}

static s32
brcmf_cfg80211_connect(struct wiphy *wiphy, struct net_device *ndev,
		    struct cfg80211_connect_params *sme)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_profile *profile = &ifp->vif->profile;
	struct ieee80211_channel *chan = sme->channel;
	struct brcmf_join_params join_params;
	size_t join_params_size;
	struct brcmf_ssid ssid;

	s32 err = 0;

	WL_TRACE("Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	if (!sme->ssid) {
		WL_ERR("Invalid ssid\n");
		return -EOPNOTSUPP;
	}

	set_bit(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state);

	if (chan) {
		cfg->channel =
			ieee80211_frequency_to_channel(chan->center_freq);
		WL_CONN("channel (%d), center_req (%d)\n",
				cfg->channel, chan->center_freq);
	} else
		cfg->channel = 0;

	WL_INFO("ie (%p), ie_len (%zd)\n", sme->ie, sme->ie_len);

	err = brcmf_set_wpa_version(ndev, sme);
	if (err) {
		WL_ERR("wl_set_wpa_version failed (%d)\n", err);
		goto done;
	}

	err = brcmf_set_auth_type(ndev, sme);
	if (err) {
		WL_ERR("wl_set_auth_type failed (%d)\n", err);
		goto done;
	}

	err = brcmf_set_set_cipher(ndev, sme);
	if (err) {
		WL_ERR("wl_set_set_cipher failed (%d)\n", err);
		goto done;
	}

	err = brcmf_set_key_mgmt(ndev, sme);
	if (err) {
		WL_ERR("wl_set_key_mgmt failed (%d)\n", err);
		goto done;
	}

	err = brcmf_set_sharedkey(ndev, sme);
	if (err) {
		WL_ERR("brcmf_set_sharedkey failed (%d)\n", err);
		goto done;
	}

	memset(&join_params, 0, sizeof(join_params));
	join_params_size = sizeof(join_params.ssid_le);

	profile->ssid.SSID_len = min_t(u32,
				       sizeof(ssid.SSID), (u32)sme->ssid_len);
	memcpy(&join_params.ssid_le.SSID, sme->ssid, profile->ssid.SSID_len);
	memcpy(&profile->ssid.SSID, sme->ssid, profile->ssid.SSID_len);
	join_params.ssid_le.SSID_len = cpu_to_le32(profile->ssid.SSID_len);

	memset(join_params.params_le.bssid, 0xFF, ETH_ALEN);

	if (ssid.SSID_len < IEEE80211_MAX_SSID_LEN)
		WL_CONN("ssid \"%s\", len (%d)\n",
		       ssid.SSID, ssid.SSID_len);

	brcmf_ch_to_chanspec(cfg->channel,
			     &join_params, &join_params_size);
	err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_SSID,
				     &join_params, join_params_size);
	if (err)
		WL_ERR("WLC_SET_SSID failed (%d)\n", err);

done:
	if (err)
		clear_bit(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state);
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *ndev,
		       u16 reason_code)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_profile *profile = &ifp->vif->profile;
	struct brcmf_scb_val_le scbval;
	s32 err = 0;

	WL_TRACE("Enter. Reason code = %d\n", reason_code);
	if (!check_vif_up(ifp->vif))
		return -EIO;

	clear_bit(BRCMF_VIF_STATUS_CONNECTED, &ifp->vif->sme_state);

	memcpy(&scbval.ea, &profile->bssid, ETH_ALEN);
	scbval.val = cpu_to_le32(reason_code);
	err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_DISASSOC,
				     &scbval, sizeof(scbval));
	if (err)
		WL_ERR("error (%d)\n", err);

	cfg->link_up = false;

	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_set_tx_power(struct wiphy *wiphy,
			    enum nl80211_tx_power_setting type, s32 mbm)
{

	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct net_device *ndev = cfg_to_ndev(cfg);
	struct brcmf_if *ifp = netdev_priv(ndev);
	u16 txpwrmw;
	s32 err = 0;
	s32 disable = 0;
	s32 dbm = MBM_TO_DBM(mbm);

	WL_TRACE("Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	switch (type) {
	case NL80211_TX_POWER_AUTOMATIC:
		break;
	case NL80211_TX_POWER_LIMITED:
	case NL80211_TX_POWER_FIXED:
		if (dbm < 0) {
			WL_ERR("TX_POWER_FIXED - dbm is negative\n");
			err = -EINVAL;
			goto done;
		}
		break;
	}
	/* Make sure radio is off or on as far as software is concerned */
	disable = WL_RADIO_SW_DISABLE << 16;
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_RADIO, disable);
	if (err)
		WL_ERR("WLC_SET_RADIO error (%d)\n", err);

	if (dbm > 0xffff)
		txpwrmw = 0xffff;
	else
		txpwrmw = (u16) dbm;
	err = brcmf_fil_iovar_int_set(ifp, "qtxpower",
				      (s32)brcmf_mw_to_qdbm(txpwrmw));
	if (err)
		WL_ERR("qtxpower error (%d)\n", err);
	cfg->conf->tx_power = dbm;

done:
	WL_TRACE("Exit\n");
	return err;
}

static s32 brcmf_cfg80211_get_tx_power(struct wiphy *wiphy, s32 *dbm)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(cfg_to_ndev(cfg));
	s32 txpwrdbm;
	u8 result;
	s32 err = 0;

	WL_TRACE("Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	err = brcmf_fil_iovar_int_get(ifp, "qtxpower", &txpwrdbm);
	if (err) {
		WL_ERR("error (%d)\n", err);
		goto done;
	}

	result = (u8) (txpwrdbm & ~WL_TXPWR_OVERRIDE);
	*dbm = (s32) brcmf_qdbm_to_mw(result);

done:
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_config_default_key(struct wiphy *wiphy, struct net_device *ndev,
			       u8 key_idx, bool unicast, bool multicast)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	u32 index;
	u32 wsec;
	s32 err = 0;

	WL_TRACE("Enter\n");
	WL_CONN("key index (%d)\n", key_idx);
	if (!check_vif_up(ifp->vif))
		return -EIO;

	err = brcmf_fil_bsscfg_int_get(ifp, "wsec", &wsec);
	if (err) {
		WL_ERR("WLC_GET_WSEC error (%d)\n", err);
		goto done;
	}

	if (wsec & WEP_ENABLED) {
		/* Just select a new current key */
		index = key_idx;
		err = brcmf_fil_cmd_int_set(ifp,
					    BRCMF_C_SET_KEY_PRIMARY, index);
		if (err)
			WL_ERR("error (%d)\n", err);
	}
done:
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_add_keyext(struct wiphy *wiphy, struct net_device *ndev,
	      u8 key_idx, const u8 *mac_addr, struct key_params *params)
{
	struct brcmf_wsec_key key;
	s32 err = 0;

	memset(&key, 0, sizeof(key));
	key.index = (u32) key_idx;
	/* Instead of bcast for ea address for default wep keys,
		 driver needs it to be Null */
	if (!is_multicast_ether_addr(mac_addr))
		memcpy((char *)&key.ea, (void *)mac_addr, ETH_ALEN);
	key.len = (u32) params->key_len;
	/* check for key index change */
	if (key.len == 0) {
		/* key delete */
		err = send_key_to_dongle(ndev, &key);
		if (err)
			WL_ERR("key delete error (%d)\n", err);
	} else {
		if (key.len > sizeof(key.data)) {
			WL_ERR("Invalid key length (%d)\n", key.len);
			return -EINVAL;
		}

		WL_CONN("Setting the key index %d\n", key.index);
		memcpy(key.data, params->key, key.len);

		if (params->cipher == WLAN_CIPHER_SUITE_TKIP) {
			u8 keybuf[8];
			memcpy(keybuf, &key.data[24], sizeof(keybuf));
			memcpy(&key.data[24], &key.data[16], sizeof(keybuf));
			memcpy(&key.data[16], keybuf, sizeof(keybuf));
		}

		/* if IW_ENCODE_EXT_RX_SEQ_VALID set */
		if (params->seq && params->seq_len == 6) {
			/* rx iv */
			u8 *ivptr;
			ivptr = (u8 *) params->seq;
			key.rxiv.hi = (ivptr[5] << 24) | (ivptr[4] << 16) |
			    (ivptr[3] << 8) | ivptr[2];
			key.rxiv.lo = (ivptr[1] << 8) | ivptr[0];
			key.iv_initialized = true;
		}

		switch (params->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
			key.algo = CRYPTO_ALGO_WEP1;
			WL_CONN("WLAN_CIPHER_SUITE_WEP40\n");
			break;
		case WLAN_CIPHER_SUITE_WEP104:
			key.algo = CRYPTO_ALGO_WEP128;
			WL_CONN("WLAN_CIPHER_SUITE_WEP104\n");
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			key.algo = CRYPTO_ALGO_TKIP;
			WL_CONN("WLAN_CIPHER_SUITE_TKIP\n");
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			key.algo = CRYPTO_ALGO_AES_CCM;
			WL_CONN("WLAN_CIPHER_SUITE_AES_CMAC\n");
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			key.algo = CRYPTO_ALGO_AES_CCM;
			WL_CONN("WLAN_CIPHER_SUITE_CCMP\n");
			break;
		default:
			WL_ERR("Invalid cipher (0x%x)\n", params->cipher);
			return -EINVAL;
		}
		err = send_key_to_dongle(ndev, &key);
		if (err)
			WL_ERR("wsec_key error (%d)\n", err);
	}
	return err;
}

static s32
brcmf_cfg80211_add_key(struct wiphy *wiphy, struct net_device *ndev,
		    u8 key_idx, bool pairwise, const u8 *mac_addr,
		    struct key_params *params)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_wsec_key key;
	s32 val;
	s32 wsec;
	s32 err = 0;
	u8 keybuf[8];

	WL_TRACE("Enter\n");
	WL_CONN("key index (%d)\n", key_idx);
	if (!check_vif_up(ifp->vif))
		return -EIO;

	if (mac_addr) {
		WL_TRACE("Exit");
		return brcmf_add_keyext(wiphy, ndev, key_idx, mac_addr, params);
	}
	memset(&key, 0, sizeof(key));

	key.len = (u32) params->key_len;
	key.index = (u32) key_idx;

	if (key.len > sizeof(key.data)) {
		WL_ERR("Too long key length (%u)\n", key.len);
		err = -EINVAL;
		goto done;
	}
	memcpy(key.data, params->key, key.len);

	key.flags = BRCMF_PRIMARY_KEY;
	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		key.algo = CRYPTO_ALGO_WEP1;
		val = WEP_ENABLED;
		WL_CONN("WLAN_CIPHER_SUITE_WEP40\n");
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		key.algo = CRYPTO_ALGO_WEP128;
		val = WEP_ENABLED;
		WL_CONN("WLAN_CIPHER_SUITE_WEP104\n");
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		if (cfg->conf->mode != WL_MODE_AP) {
			WL_CONN("Swapping key\n");
			memcpy(keybuf, &key.data[24], sizeof(keybuf));
			memcpy(&key.data[24], &key.data[16], sizeof(keybuf));
			memcpy(&key.data[16], keybuf, sizeof(keybuf));
		}
		key.algo = CRYPTO_ALGO_TKIP;
		val = TKIP_ENABLED;
		WL_CONN("WLAN_CIPHER_SUITE_TKIP\n");
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		key.algo = CRYPTO_ALGO_AES_CCM;
		val = AES_ENABLED;
		WL_CONN("WLAN_CIPHER_SUITE_AES_CMAC\n");
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key.algo = CRYPTO_ALGO_AES_CCM;
		val = AES_ENABLED;
		WL_CONN("WLAN_CIPHER_SUITE_CCMP\n");
		break;
	default:
		WL_ERR("Invalid cipher (0x%x)\n", params->cipher);
		err = -EINVAL;
		goto done;
	}

	err = send_key_to_dongle(ndev, &key);
	if (err)
		goto done;

	err = brcmf_fil_bsscfg_int_get(ifp, "wsec", &wsec);
	if (err) {
		WL_ERR("get wsec error (%d)\n", err);
		goto done;
	}
	wsec |= val;
	err = brcmf_fil_bsscfg_int_set(ifp, "wsec", wsec);
	if (err) {
		WL_ERR("set wsec error (%d)\n", err);
		goto done;
	}

done:
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_del_key(struct wiphy *wiphy, struct net_device *ndev,
		    u8 key_idx, bool pairwise, const u8 *mac_addr)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_wsec_key key;
	s32 err = 0;

	WL_TRACE("Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	memset(&key, 0, sizeof(key));

	key.index = (u32) key_idx;
	key.flags = BRCMF_PRIMARY_KEY;
	key.algo = CRYPTO_ALGO_OFF;

	WL_CONN("key index (%d)\n", key_idx);

	/* Set the new key/index */
	err = send_key_to_dongle(ndev, &key);
	if (err) {
		if (err == -EINVAL) {
			if (key.index >= DOT11_MAX_DEFAULT_KEYS)
				/* we ignore this key index in this case */
				WL_ERR("invalid key index (%d)\n", key_idx);
		}
		/* Ignore this error, may happen during DISASSOC */
		err = -EAGAIN;
	}

	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_get_key(struct wiphy *wiphy, struct net_device *ndev,
		    u8 key_idx, bool pairwise, const u8 *mac_addr, void *cookie,
		    void (*callback) (void *cookie, struct key_params * params))
{
	struct key_params params;
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_profile *profile = &ifp->vif->profile;
	struct brcmf_cfg80211_security *sec;
	s32 wsec;
	s32 err = 0;

	WL_TRACE("Enter\n");
	WL_CONN("key index (%d)\n", key_idx);
	if (!check_vif_up(ifp->vif))
		return -EIO;

	memset(&params, 0, sizeof(params));

	err = brcmf_fil_bsscfg_int_get(ifp, "wsec", &wsec);
	if (err) {
		WL_ERR("WLC_GET_WSEC error (%d)\n", err);
		/* Ignore this error, may happen during DISASSOC */
		err = -EAGAIN;
		goto done;
	}
	switch (wsec & ~SES_OW_ENABLED) {
	case WEP_ENABLED:
		sec = &profile->sec;
		if (sec->cipher_pairwise & WLAN_CIPHER_SUITE_WEP40) {
			params.cipher = WLAN_CIPHER_SUITE_WEP40;
			WL_CONN("WLAN_CIPHER_SUITE_WEP40\n");
		} else if (sec->cipher_pairwise & WLAN_CIPHER_SUITE_WEP104) {
			params.cipher = WLAN_CIPHER_SUITE_WEP104;
			WL_CONN("WLAN_CIPHER_SUITE_WEP104\n");
		}
		break;
	case TKIP_ENABLED:
		params.cipher = WLAN_CIPHER_SUITE_TKIP;
		WL_CONN("WLAN_CIPHER_SUITE_TKIP\n");
		break;
	case AES_ENABLED:
		params.cipher = WLAN_CIPHER_SUITE_AES_CMAC;
		WL_CONN("WLAN_CIPHER_SUITE_AES_CMAC\n");
		break;
	default:
		WL_ERR("Invalid algo (0x%x)\n", wsec);
		err = -EINVAL;
		goto done;
	}
	callback(cookie, &params);

done:
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_config_default_mgmt_key(struct wiphy *wiphy,
				    struct net_device *ndev, u8 key_idx)
{
	WL_INFO("Not supported\n");

	return -EOPNOTSUPP;
}

static s32
brcmf_cfg80211_get_station(struct wiphy *wiphy, struct net_device *ndev,
			   u8 *mac, struct station_info *sinfo)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_profile *profile = &ifp->vif->profile;
	struct brcmf_scb_val_le scb_val;
	int rssi;
	s32 rate;
	s32 err = 0;
	u8 *bssid = profile->bssid;
	struct brcmf_sta_info_le sta_info_le;

	WL_TRACE("Enter, MAC %pM\n", mac);
	if (!check_vif_up(ifp->vif))
		return -EIO;

	if (cfg->conf->mode == WL_MODE_AP) {
		memcpy(&sta_info_le, mac, ETH_ALEN);
		err = brcmf_fil_iovar_data_get(ifp, "sta_info",
					       &sta_info_le,
					       sizeof(sta_info_le));
		if (err < 0) {
			WL_ERR("GET STA INFO failed, %d\n", err);
			goto done;
		}
		sinfo->filled = STATION_INFO_INACTIVE_TIME;
		sinfo->inactive_time = le32_to_cpu(sta_info_le.idle) * 1000;
		if (le32_to_cpu(sta_info_le.flags) & BRCMF_STA_ASSOC) {
			sinfo->filled |= STATION_INFO_CONNECTED_TIME;
			sinfo->connected_time = le32_to_cpu(sta_info_le.in);
		}
		WL_TRACE("STA idle time : %d ms, connected time :%d sec\n",
			 sinfo->inactive_time, sinfo->connected_time);
	} else if (cfg->conf->mode == WL_MODE_BSS) {
		if (memcmp(mac, bssid, ETH_ALEN)) {
			WL_ERR("Wrong Mac address cfg_mac-%pM wl_bssid-%pM\n",
			       mac, bssid);
			err = -ENOENT;
			goto done;
		}
		/* Report the current tx rate */
	err = brcmf_fil_cmd_int_get(ifp, BRCMF_C_GET_RATE, &rate);
		if (err) {
			WL_ERR("Could not get rate (%d)\n", err);
			goto done;
		} else {
			sinfo->filled |= STATION_INFO_TX_BITRATE;
			sinfo->txrate.legacy = rate * 5;
			WL_CONN("Rate %d Mbps\n", rate / 2);
		}

		if (test_bit(BRCMF_VIF_STATUS_CONNECTED,
			     &ifp->vif->sme_state)) {
			memset(&scb_val, 0, sizeof(scb_val));
			err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_RSSI,
						     &scb_val, sizeof(scb_val));
			if (err) {
				WL_ERR("Could not get rssi (%d)\n", err);
				goto done;
			} else {
				rssi = le32_to_cpu(scb_val.val);
				sinfo->filled |= STATION_INFO_SIGNAL;
				sinfo->signal = rssi;
				WL_CONN("RSSI %d dBm\n", rssi);
			}
		}
	} else
		err = -EPERM;
done:
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_set_power_mgmt(struct wiphy *wiphy, struct net_device *ndev,
			   bool enabled, s32 timeout)
{
	s32 pm;
	s32 err = 0;
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);

	WL_TRACE("Enter\n");

	/*
	 * Powersave enable/disable request is coming from the
	 * cfg80211 even before the interface is up. In that
	 * scenario, driver will be storing the power save
	 * preference in cfg struct to apply this to
	 * FW later while initializing the dongle
	 */
	cfg->pwr_save = enabled;
	if (!check_vif_up(ifp->vif)) {

		WL_INFO("Device is not ready, storing the value in cfg_info struct\n");
		goto done;
	}

	pm = enabled ? PM_FAST : PM_OFF;
	WL_INFO("power save %s\n", (pm ? "enabled" : "disabled"));

	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_PM, pm);
	if (err) {
		if (err == -ENODEV)
			WL_ERR("net_device is not ready yet\n");
		else
			WL_ERR("error (%d)\n", err);
	}
done:
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_set_bitrate_mask(struct wiphy *wiphy, struct net_device *ndev,
			     const u8 *addr,
			     const struct cfg80211_bitrate_mask *mask)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcm_rateset_le rateset_le;
	s32 rate;
	s32 val;
	s32 err_bg;
	s32 err_a;
	u32 legacy;
	s32 err = 0;

	WL_TRACE("Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	/* addr param is always NULL. ignore it */
	/* Get current rateset */
	err = brcmf_fil_cmd_data_get(ifp, BRCM_GET_CURR_RATESET,
				     &rateset_le, sizeof(rateset_le));
	if (err) {
		WL_ERR("could not get current rateset (%d)\n", err);
		goto done;
	}

	legacy = ffs(mask->control[IEEE80211_BAND_2GHZ].legacy & 0xFFFF);
	if (!legacy)
		legacy = ffs(mask->control[IEEE80211_BAND_5GHZ].legacy &
			     0xFFFF);

	val = wl_g_rates[legacy - 1].bitrate * 100000;

	if (val < le32_to_cpu(rateset_le.count))
		/* Select rate by rateset index */
		rate = rateset_le.rates[val] & 0x7f;
	else
		/* Specified rate in bps */
		rate = val / 500000;

	WL_CONN("rate %d mbps\n", rate / 2);

	/*
	 *
	 *      Set rate override,
	 *      Since the is a/b/g-blind, both a/bg_rate are enforced.
	 */
	err_bg = brcmf_fil_iovar_int_set(ifp, "bg_rate", rate);
	err_a = brcmf_fil_iovar_int_set(ifp, "a_rate", rate);
	if (err_bg && err_a) {
		WL_ERR("could not set fixed rate (%d) (%d)\n", err_bg, err_a);
		err = err_bg | err_a;
	}

done:
	WL_TRACE("Exit\n");
	return err;
}

static s32 brcmf_inform_single_bss(struct brcmf_cfg80211_info *cfg,
				   struct brcmf_bss_info_le *bi)
{
	struct wiphy *wiphy = cfg_to_wiphy(cfg);
	struct ieee80211_channel *notify_channel;
	struct cfg80211_bss *bss;
	struct ieee80211_supported_band *band;
	s32 err = 0;
	u16 channel;
	u32 freq;
	u16 notify_capability;
	u16 notify_interval;
	u8 *notify_ie;
	size_t notify_ielen;
	s32 notify_signal;

	if (le32_to_cpu(bi->length) > WL_BSS_INFO_MAX) {
		WL_ERR("Bss info is larger than buffer. Discarding\n");
		return 0;
	}

	channel = bi->ctl_ch ? bi->ctl_ch :
				CHSPEC_CHANNEL(le16_to_cpu(bi->chanspec));

	if (channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];

	freq = ieee80211_channel_to_frequency(channel, band->band);
	notify_channel = ieee80211_get_channel(wiphy, freq);

	notify_capability = le16_to_cpu(bi->capability);
	notify_interval = le16_to_cpu(bi->beacon_period);
	notify_ie = (u8 *)bi + le16_to_cpu(bi->ie_offset);
	notify_ielen = le32_to_cpu(bi->ie_length);
	notify_signal = (s16)le16_to_cpu(bi->RSSI) * 100;

	WL_CONN("bssid: %2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X\n",
			bi->BSSID[0], bi->BSSID[1], bi->BSSID[2],
			bi->BSSID[3], bi->BSSID[4], bi->BSSID[5]);
	WL_CONN("Channel: %d(%d)\n", channel, freq);
	WL_CONN("Capability: %X\n", notify_capability);
	WL_CONN("Beacon interval: %d\n", notify_interval);
	WL_CONN("Signal: %d\n", notify_signal);

	bss = cfg80211_inform_bss(wiphy, notify_channel, (const u8 *)bi->BSSID,
		0, notify_capability, notify_interval, notify_ie,
		notify_ielen, notify_signal, GFP_KERNEL);

	if (!bss)
		return -ENOMEM;

	cfg80211_put_bss(bss);

	return err;
}

static struct brcmf_bss_info_le *
next_bss_le(struct brcmf_scan_results *list, struct brcmf_bss_info_le *bss)
{
	if (bss == NULL)
		return list->bss_info_le;
	return (struct brcmf_bss_info_le *)((unsigned long)bss +
					    le32_to_cpu(bss->length));
}

static s32 brcmf_inform_bss(struct brcmf_cfg80211_info *cfg)
{
	struct brcmf_scan_results *bss_list;
	struct brcmf_bss_info_le *bi = NULL;	/* must be initialized */
	s32 err = 0;
	int i;

	bss_list = cfg->bss_list;
	if (bss_list->count != 0 &&
	    bss_list->version != BRCMF_BSS_INFO_VERSION) {
		WL_ERR("Version %d != WL_BSS_INFO_VERSION\n",
		       bss_list->version);
		return -EOPNOTSUPP;
	}
	WL_SCAN("scanned AP count (%d)\n", bss_list->count);
	for (i = 0; i < bss_list->count; i++) {
		bi = next_bss_le(bss_list, bi);
		err = brcmf_inform_single_bss(cfg, bi);
		if (err)
			break;
	}
	return err;
}

static s32 wl_inform_ibss(struct brcmf_cfg80211_info *cfg,
			  struct net_device *ndev, const u8 *bssid)
{
	struct wiphy *wiphy = cfg_to_wiphy(cfg);
	struct ieee80211_channel *notify_channel;
	struct brcmf_bss_info_le *bi = NULL;
	struct ieee80211_supported_band *band;
	struct cfg80211_bss *bss;
	u8 *buf = NULL;
	s32 err = 0;
	u16 channel;
	u32 freq;
	u16 notify_capability;
	u16 notify_interval;
	u8 *notify_ie;
	size_t notify_ielen;
	s32 notify_signal;

	WL_TRACE("Enter\n");

	buf = kzalloc(WL_BSS_INFO_MAX, GFP_KERNEL);
	if (buf == NULL) {
		err = -ENOMEM;
		goto CleanUp;
	}

	*(__le32 *)buf = cpu_to_le32(WL_BSS_INFO_MAX);

	err = brcmf_fil_cmd_data_get(netdev_priv(ndev), BRCMF_C_GET_BSS_INFO,
				     buf, WL_BSS_INFO_MAX);
	if (err) {
		WL_ERR("WLC_GET_BSS_INFO failed: %d\n", err);
		goto CleanUp;
	}

	bi = (struct brcmf_bss_info_le *)(buf + 4);

	channel = bi->ctl_ch ? bi->ctl_ch :
				CHSPEC_CHANNEL(le16_to_cpu(bi->chanspec));

	if (channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];

	freq = ieee80211_channel_to_frequency(channel, band->band);
	notify_channel = ieee80211_get_channel(wiphy, freq);

	notify_capability = le16_to_cpu(bi->capability);
	notify_interval = le16_to_cpu(bi->beacon_period);
	notify_ie = (u8 *)bi + le16_to_cpu(bi->ie_offset);
	notify_ielen = le32_to_cpu(bi->ie_length);
	notify_signal = (s16)le16_to_cpu(bi->RSSI) * 100;

	WL_CONN("channel: %d(%d)\n", channel, freq);
	WL_CONN("capability: %X\n", notify_capability);
	WL_CONN("beacon interval: %d\n", notify_interval);
	WL_CONN("signal: %d\n", notify_signal);

	bss = cfg80211_inform_bss(wiphy, notify_channel, bssid,
		0, notify_capability, notify_interval,
		notify_ie, notify_ielen, notify_signal, GFP_KERNEL);

	if (!bss) {
		err = -ENOMEM;
		goto CleanUp;
	}

	cfg80211_put_bss(bss);

CleanUp:

	kfree(buf);

	WL_TRACE("Exit\n");

	return err;
}

static bool brcmf_is_ibssmode(struct brcmf_cfg80211_info *cfg)
{
	return cfg->conf->mode == WL_MODE_IBSS;
}

/*
 * Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag
 */
static struct brcmf_tlv *brcmf_parse_tlvs(void *buf, int buflen, uint key)
{
	struct brcmf_tlv *elt;
	int totlen;

	elt = (struct brcmf_tlv *) buf;
	totlen = buflen;

	/* find tagged parameter */
	while (totlen >= TLV_HDR_LEN) {
		int len = elt->len;

		/* validate remaining totlen */
		if ((elt->id == key) && (totlen >= (len + TLV_HDR_LEN)))
			return elt;

		elt = (struct brcmf_tlv *) ((u8 *) elt + (len + TLV_HDR_LEN));
		totlen -= (len + TLV_HDR_LEN);
	}

	return NULL;
}

/* Is any of the tlvs the expected entry? If
 * not update the tlvs buffer pointer/length.
 */
static bool
brcmf_tlv_has_ie(u8 *ie, u8 **tlvs, u32 *tlvs_len,
		 u8 *oui, u32 oui_len, u8 type)
{
	/* If the contents match the OUI and the type */
	if (ie[TLV_LEN_OFF] >= oui_len + 1 &&
	    !memcmp(&ie[TLV_BODY_OFF], oui, oui_len) &&
	    type == ie[TLV_BODY_OFF + oui_len]) {
		return true;
	}

	if (tlvs == NULL)
		return false;
	/* point to the next ie */
	ie += ie[TLV_LEN_OFF] + TLV_HDR_LEN;
	/* calculate the length of the rest of the buffer */
	*tlvs_len -= (int)(ie - *tlvs);
	/* update the pointer to the start of the buffer */
	*tlvs = ie;

	return false;
}

static struct brcmf_vs_tlv *
brcmf_find_wpaie(u8 *parse, u32 len)
{
	struct brcmf_tlv *ie;

	while ((ie = brcmf_parse_tlvs(parse, len, WLAN_EID_VENDOR_SPECIFIC))) {
		if (brcmf_tlv_has_ie((u8 *)ie, &parse, &len,
				     WPA_OUI, TLV_OUI_LEN, WPA_OUI_TYPE))
			return (struct brcmf_vs_tlv *)ie;
	}
	return NULL;
}

static s32 brcmf_update_bss_info(struct brcmf_cfg80211_info *cfg)
{
	struct net_device *ndev = cfg_to_ndev(cfg);
	struct brcmf_cfg80211_profile *profile = ndev_to_prof(ndev);
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_bss_info_le *bi;
	struct brcmf_ssid *ssid;
	struct brcmf_tlv *tim;
	u16 beacon_interval;
	u8 dtim_period;
	size_t ie_len;
	u8 *ie;
	s32 err = 0;

	WL_TRACE("Enter\n");
	if (brcmf_is_ibssmode(cfg))
		return err;

	ssid = &profile->ssid;

	*(__le32 *)cfg->extra_buf = cpu_to_le32(WL_EXTRA_BUF_MAX);
	err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BSS_INFO,
				     cfg->extra_buf, WL_EXTRA_BUF_MAX);
	if (err) {
		WL_ERR("Could not get bss info %d\n", err);
		goto update_bss_info_out;
	}

	bi = (struct brcmf_bss_info_le *)(cfg->extra_buf + 4);
	err = brcmf_inform_single_bss(cfg, bi);
	if (err)
		goto update_bss_info_out;

	ie = ((u8 *)bi) + le16_to_cpu(bi->ie_offset);
	ie_len = le32_to_cpu(bi->ie_length);
	beacon_interval = le16_to_cpu(bi->beacon_period);

	tim = brcmf_parse_tlvs(ie, ie_len, WLAN_EID_TIM);
	if (tim)
		dtim_period = tim->data[1];
	else {
		/*
		* active scan was done so we could not get dtim
		* information out of probe response.
		* so we speficially query dtim information to dongle.
		*/
		u32 var;
		err = brcmf_fil_iovar_int_get(ifp, "dtim_assoc", &var);
		if (err) {
			WL_ERR("wl dtim_assoc failed (%d)\n", err);
			goto update_bss_info_out;
		}
		dtim_period = (u8)var;
	}

update_bss_info_out:
	WL_TRACE("Exit");
	return err;
}

static void brcmf_abort_scanning(struct brcmf_cfg80211_info *cfg)
{
	struct escan_info *escan = &cfg->escan_info;

	set_bit(BRCMF_SCAN_STATUS_ABORT, &cfg->scan_status);
	if (cfg->scan_request) {
		escan->escan_state = WL_ESCAN_STATE_IDLE;
		brcmf_notify_escan_complete(cfg, escan->ndev, true, true);
	}
	clear_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status);
	clear_bit(BRCMF_SCAN_STATUS_ABORT, &cfg->scan_status);
}

static void brcmf_cfg80211_escan_timeout_worker(struct work_struct *work)
{
	struct brcmf_cfg80211_info *cfg =
			container_of(work, struct brcmf_cfg80211_info,
				     escan_timeout_work);

	brcmf_notify_escan_complete(cfg,
		cfg->escan_info.ndev, true, true);
}

static void brcmf_escan_timeout(unsigned long data)
{
	struct brcmf_cfg80211_info *cfg =
			(struct brcmf_cfg80211_info *)data;

	if (cfg->scan_request) {
		WL_ERR("timer expired\n");
		schedule_work(&cfg->escan_timeout_work);
	}
}

static s32
brcmf_compare_update_same_bss(struct brcmf_bss_info_le *bss,
			      struct brcmf_bss_info_le *bss_info_le)
{
	if (!memcmp(&bss_info_le->BSSID, &bss->BSSID, ETH_ALEN) &&
		(CHSPEC_BAND(le16_to_cpu(bss_info_le->chanspec)) ==
		CHSPEC_BAND(le16_to_cpu(bss->chanspec))) &&
		bss_info_le->SSID_len == bss->SSID_len &&
		!memcmp(bss_info_le->SSID, bss->SSID, bss_info_le->SSID_len)) {
		if ((bss->flags & WLC_BSS_RSSI_ON_CHANNEL) ==
			(bss_info_le->flags & WLC_BSS_RSSI_ON_CHANNEL)) {
			s16 bss_rssi = le16_to_cpu(bss->RSSI);
			s16 bss_info_rssi = le16_to_cpu(bss_info_le->RSSI);

			/* preserve max RSSI if the measurements are
			* both on-channel or both off-channel
			*/
			if (bss_info_rssi > bss_rssi)
				bss->RSSI = bss_info_le->RSSI;
		} else if ((bss->flags & WLC_BSS_RSSI_ON_CHANNEL) &&
			(bss_info_le->flags & WLC_BSS_RSSI_ON_CHANNEL) == 0) {
			/* preserve the on-channel rssi measurement
			* if the new measurement is off channel
			*/
			bss->RSSI = bss_info_le->RSSI;
			bss->flags |= WLC_BSS_RSSI_ON_CHANNEL;
		}
		return 1;
	}
	return 0;
}

static s32
brcmf_cfg80211_escan_handler(struct brcmf_cfg80211_info *cfg,
			     struct net_device *ndev,
			     const struct brcmf_event_msg *e, void *data)
{
	s32 status;
	s32 err = 0;
	struct brcmf_escan_result_le *escan_result_le;
	struct brcmf_bss_info_le *bss_info_le;
	struct brcmf_bss_info_le *bss = NULL;
	u32 bi_length;
	struct brcmf_scan_results *list;
	u32 i;
	bool aborted;

	status = be32_to_cpu(e->status);

	if (!ndev || !test_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status)) {
		WL_ERR("scan not ready ndev %p drv_status %x\n", ndev,
		       !test_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status));
		return -EPERM;
	}

	if (status == BRCMF_E_STATUS_PARTIAL) {
		WL_SCAN("ESCAN Partial result\n");
		escan_result_le = (struct brcmf_escan_result_le *) data;
		if (!escan_result_le) {
			WL_ERR("Invalid escan result (NULL pointer)\n");
			goto exit;
		}
		if (!cfg->scan_request) {
			WL_SCAN("result without cfg80211 request\n");
			goto exit;
		}

		if (le16_to_cpu(escan_result_le->bss_count) != 1) {
			WL_ERR("Invalid bss_count %d: ignoring\n",
				escan_result_le->bss_count);
			goto exit;
		}
		bss_info_le = &escan_result_le->bss_info_le;

		bi_length = le32_to_cpu(bss_info_le->length);
		if (bi_length != (le32_to_cpu(escan_result_le->buflen) -
					WL_ESCAN_RESULTS_FIXED_SIZE)) {
			WL_ERR("Invalid bss_info length %d: ignoring\n",
				bi_length);
			goto exit;
		}

		if (!(cfg_to_wiphy(cfg)->interface_modes &
					BIT(NL80211_IFTYPE_ADHOC))) {
			if (le16_to_cpu(bss_info_le->capability) &
						WLAN_CAPABILITY_IBSS) {
				WL_ERR("Ignoring IBSS result\n");
				goto exit;
			}
		}

		list = (struct brcmf_scan_results *)
				cfg->escan_info.escan_buf;
		if (bi_length > WL_ESCAN_BUF_SIZE - list->buflen) {
			WL_ERR("Buffer is too small: ignoring\n");
			goto exit;
		}

		for (i = 0; i < list->count; i++) {
			bss = bss ? (struct brcmf_bss_info_le *)
				((unsigned char *)bss +
				le32_to_cpu(bss->length)) : list->bss_info_le;
			if (brcmf_compare_update_same_bss(bss, bss_info_le))
				goto exit;
		}
		memcpy(&(cfg->escan_info.escan_buf[list->buflen]),
			bss_info_le, bi_length);
		list->version = le32_to_cpu(bss_info_le->version);
		list->buflen += bi_length;
		list->count++;
	} else {
		cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
		if (cfg->scan_request) {
			cfg->bss_list = (struct brcmf_scan_results *)
				cfg->escan_info.escan_buf;
			brcmf_inform_bss(cfg);
			aborted = status != BRCMF_E_STATUS_SUCCESS;
			brcmf_notify_escan_complete(cfg, ndev, aborted,
						    false);
		} else
			WL_ERR("Unexpected scan result 0x%x\n", status);
	}
exit:
	return err;
}

static void brcmf_init_escan(struct brcmf_cfg80211_info *cfg)
{

	cfg->el.handler[BRCMF_E_ESCAN_RESULT] =
		brcmf_cfg80211_escan_handler;
	cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
	/* Init scan_timeout timer */
	init_timer(&cfg->escan_timeout);
	cfg->escan_timeout.data = (unsigned long) cfg;
	cfg->escan_timeout.function = brcmf_escan_timeout;
	INIT_WORK(&cfg->escan_timeout_work,
		  brcmf_cfg80211_escan_timeout_worker);
}

static __always_inline void brcmf_delay(u32 ms)
{
	if (ms < 1000 / HZ) {
		cond_resched();
		mdelay(ms);
	} else {
		msleep(ms);
	}
}

static s32 brcmf_cfg80211_resume(struct wiphy *wiphy)
{
	WL_TRACE("Enter\n");

	return 0;
}

static s32 brcmf_cfg80211_suspend(struct wiphy *wiphy,
				  struct cfg80211_wowlan *wow)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct net_device *ndev = cfg_to_ndev(cfg);
	struct brcmf_cfg80211_vif *vif;

	WL_TRACE("Enter\n");

	/*
	 * if the primary net_device is not READY there is nothing
	 * we can do but pray resume goes smoothly.
	 */
	vif = ((struct brcmf_if *)netdev_priv(ndev))->vif;
	if (!check_vif_up(vif))
		goto exit;

	list_for_each_entry(vif, &cfg->vif_list, list) {
		if (!test_bit(BRCMF_VIF_STATUS_READY, &vif->sme_state))
			continue;
		/*
		 * While going to suspend if associated with AP disassociate
		 * from AP to save power while system is in suspended state
		 */
		if (test_bit(BRCMF_VIF_STATUS_CONNECTED, &vif->sme_state) ||
		    test_bit(BRCMF_VIF_STATUS_CONNECTING, &vif->sme_state)) {
			WL_INFO("Disassociating from AP before suspend\n");
			brcmf_link_down(cfg);

			/* Make sure WPA_Supplicant receives all the event
			 * generated due to DISASSOC call to the fw to keep
			 * the state fw and WPA_Supplicant state consistent
			 */
			brcmf_delay(500);
		}
	}

	/* end any scanning */
	if (test_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status))
		brcmf_abort_scanning(cfg);

	/* Turn off watchdog timer */
	brcmf_set_mpc(ndev, 1);

exit:
	WL_TRACE("Exit\n");
	/* clear any scanning activity */
	cfg->scan_status = 0;
	return 0;
}

static __used s32
brcmf_update_pmklist(struct net_device *ndev,
		     struct brcmf_cfg80211_pmk_list *pmk_list, s32 err)
{
	int i, j;
	int pmkid_len;

	pmkid_len = le32_to_cpu(pmk_list->pmkids.npmkid);

	WL_CONN("No of elements %d\n", pmkid_len);
	for (i = 0; i < pmkid_len; i++) {
		WL_CONN("PMKID[%d]: %pM =\n", i,
			&pmk_list->pmkids.pmkid[i].BSSID);
		for (j = 0; j < WLAN_PMKID_LEN; j++)
			WL_CONN("%02x\n", pmk_list->pmkids.pmkid[i].PMKID[j]);
	}

	if (!err)
		brcmf_fil_iovar_data_set(netdev_priv(ndev), "pmkid_info",
					 (char *)pmk_list, sizeof(*pmk_list));

	return err;
}

static s32
brcmf_cfg80211_set_pmksa(struct wiphy *wiphy, struct net_device *ndev,
			 struct cfg80211_pmksa *pmksa)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct pmkid_list *pmkids = &cfg->pmk_list->pmkids;
	s32 err = 0;
	int i;
	int pmkid_len;

	WL_TRACE("Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	pmkid_len = le32_to_cpu(pmkids->npmkid);
	for (i = 0; i < pmkid_len; i++)
		if (!memcmp(pmksa->bssid, pmkids->pmkid[i].BSSID, ETH_ALEN))
			break;
	if (i < WL_NUM_PMKIDS_MAX) {
		memcpy(pmkids->pmkid[i].BSSID, pmksa->bssid, ETH_ALEN);
		memcpy(pmkids->pmkid[i].PMKID, pmksa->pmkid, WLAN_PMKID_LEN);
		if (i == pmkid_len) {
			pmkid_len++;
			pmkids->npmkid = cpu_to_le32(pmkid_len);
		}
	} else
		err = -EINVAL;

	WL_CONN("set_pmksa,IW_PMKSA_ADD - PMKID: %pM =\n",
		pmkids->pmkid[pmkid_len].BSSID);
	for (i = 0; i < WLAN_PMKID_LEN; i++)
		WL_CONN("%02x\n", pmkids->pmkid[pmkid_len].PMKID[i]);

	err = brcmf_update_pmklist(ndev, cfg->pmk_list, err);

	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_del_pmksa(struct wiphy *wiphy, struct net_device *ndev,
		      struct cfg80211_pmksa *pmksa)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct pmkid_list pmkid;
	s32 err = 0;
	int i, pmkid_len;

	WL_TRACE("Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	memcpy(&pmkid.pmkid[0].BSSID, pmksa->bssid, ETH_ALEN);
	memcpy(&pmkid.pmkid[0].PMKID, pmksa->pmkid, WLAN_PMKID_LEN);

	WL_CONN("del_pmksa,IW_PMKSA_REMOVE - PMKID: %pM =\n",
	       &pmkid.pmkid[0].BSSID);
	for (i = 0; i < WLAN_PMKID_LEN; i++)
		WL_CONN("%02x\n", pmkid.pmkid[0].PMKID[i]);

	pmkid_len = le32_to_cpu(cfg->pmk_list->pmkids.npmkid);
	for (i = 0; i < pmkid_len; i++)
		if (!memcmp
		    (pmksa->bssid, &cfg->pmk_list->pmkids.pmkid[i].BSSID,
		     ETH_ALEN))
			break;

	if ((pmkid_len > 0)
	    && (i < pmkid_len)) {
		memset(&cfg->pmk_list->pmkids.pmkid[i], 0,
		       sizeof(struct pmkid));
		for (; i < (pmkid_len - 1); i++) {
			memcpy(&cfg->pmk_list->pmkids.pmkid[i].BSSID,
			       &cfg->pmk_list->pmkids.pmkid[i + 1].BSSID,
			       ETH_ALEN);
			memcpy(&cfg->pmk_list->pmkids.pmkid[i].PMKID,
			       &cfg->pmk_list->pmkids.pmkid[i + 1].PMKID,
			       WLAN_PMKID_LEN);
		}
		cfg->pmk_list->pmkids.npmkid = cpu_to_le32(pmkid_len - 1);
	} else
		err = -EINVAL;

	err = brcmf_update_pmklist(ndev, cfg->pmk_list, err);

	WL_TRACE("Exit\n");
	return err;

}

static s32
brcmf_cfg80211_flush_pmksa(struct wiphy *wiphy, struct net_device *ndev)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err = 0;

	WL_TRACE("Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	memset(cfg->pmk_list, 0, sizeof(*cfg->pmk_list));
	err = brcmf_update_pmklist(ndev, cfg->pmk_list, err);

	WL_TRACE("Exit\n");
	return err;

}

/*
 * PFN result doesn't have all the info which are
 * required by the supplicant
 * (For e.g IEs) Do a target Escan so that sched scan results are reported
 * via wl_inform_single_bss in the required format. Escan does require the
 * scan request in the form of cfg80211_scan_request. For timebeing, create
 * cfg80211_scan_request one out of the received PNO event.
 */
static s32
brcmf_notify_sched_scan_results(struct brcmf_cfg80211_info *cfg,
				struct net_device *ndev,
				const struct brcmf_event_msg *e, void *data)
{
	struct brcmf_pno_net_info_le *netinfo, *netinfo_start;
	struct cfg80211_scan_request *request = NULL;
	struct cfg80211_ssid *ssid = NULL;
	struct ieee80211_channel *channel = NULL;
	struct wiphy *wiphy = cfg_to_wiphy(cfg);
	int err = 0;
	int channel_req = 0;
	int band = 0;
	struct brcmf_pno_scanresults_le *pfn_result;
	u32 result_count;
	u32 status;

	WL_SCAN("Enter\n");

	if (e->event_type == cpu_to_be32(BRCMF_E_PFN_NET_LOST)) {
		WL_SCAN("PFN NET LOST event. Do Nothing\n");
		return 0;
	}

	pfn_result = (struct brcmf_pno_scanresults_le *)data;
	result_count = le32_to_cpu(pfn_result->count);
	status = le32_to_cpu(pfn_result->status);

	/*
	 * PFN event is limited to fit 512 bytes so we may get
	 * multiple NET_FOUND events. For now place a warning here.
	 */
	WARN_ON(status != BRCMF_PNO_SCAN_COMPLETE);
	WL_SCAN("PFN NET FOUND event. count: %d\n", result_count);
	if (result_count > 0) {
		int i;

		request = kzalloc(sizeof(*request), GFP_KERNEL);
		ssid = kcalloc(result_count, sizeof(*ssid), GFP_KERNEL);
		channel = kcalloc(result_count, sizeof(*channel), GFP_KERNEL);
		if (!request || !ssid || !channel) {
			err = -ENOMEM;
			goto out_err;
		}

		request->wiphy = wiphy;
		data += sizeof(struct brcmf_pno_scanresults_le);
		netinfo_start = (struct brcmf_pno_net_info_le *)data;

		for (i = 0; i < result_count; i++) {
			netinfo = &netinfo_start[i];
			if (!netinfo) {
				WL_ERR("Invalid netinfo ptr. index: %d\n", i);
				err = -EINVAL;
				goto out_err;
			}

			WL_SCAN("SSID:%s Channel:%d\n",
			netinfo->SSID, netinfo->channel);
			memcpy(ssid[i].ssid, netinfo->SSID, netinfo->SSID_len);
			ssid[i].ssid_len = netinfo->SSID_len;
			request->n_ssids++;

			channel_req = netinfo->channel;
			if (channel_req <= CH_MAX_2G_CHANNEL)
				band = NL80211_BAND_2GHZ;
			else
				band = NL80211_BAND_5GHZ;
			channel[i].center_freq =
				ieee80211_channel_to_frequency(channel_req,
							       band);
			channel[i].band = band;
			channel[i].flags |= IEEE80211_CHAN_NO_HT40;
			request->channels[i] = &channel[i];
			request->n_channels++;
		}

		/* assign parsed ssid array */
		if (request->n_ssids)
			request->ssids = &ssid[0];

		if (test_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status)) {
			/* Abort any on-going scan */
			brcmf_abort_scanning(cfg);
		}

		set_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status);
		err = brcmf_do_escan(cfg, wiphy, ndev, request);
		if (err) {
			clear_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status);
			goto out_err;
		}
		cfg->sched_escan = true;
		cfg->scan_request = request;
	} else {
		WL_ERR("FALSE PNO Event. (pfn_count == 0)\n");
		goto out_err;
	}

	kfree(ssid);
	kfree(channel);
	kfree(request);
	return 0;

out_err:
	kfree(ssid);
	kfree(channel);
	kfree(request);
	cfg80211_sched_scan_stopped(wiphy);
	return err;
}

static int brcmf_dev_pno_clean(struct net_device *ndev)
{
	int ret;

	/* Disable pfn */
	ret = brcmf_fil_iovar_int_set(netdev_priv(ndev), "pfn", 0);
	if (ret == 0) {
		/* clear pfn */
		ret = brcmf_fil_iovar_data_set(netdev_priv(ndev), "pfnclear",
					       NULL, 0);
	}
	if (ret < 0)
		WL_ERR("failed code %d\n", ret);

	return ret;
}

static int brcmf_dev_pno_config(struct net_device *ndev)
{
	struct brcmf_pno_param_le pfn_param;

	memset(&pfn_param, 0, sizeof(pfn_param));
	pfn_param.version = cpu_to_le32(BRCMF_PNO_VERSION);

	/* set extra pno params */
	pfn_param.flags = cpu_to_le16(1 << BRCMF_PNO_ENABLE_ADAPTSCAN_BIT);
	pfn_param.repeat = BRCMF_PNO_REPEAT;
	pfn_param.exp = BRCMF_PNO_FREQ_EXPO_MAX;

	/* set up pno scan fr */
	pfn_param.scan_freq = cpu_to_le32(BRCMF_PNO_TIME);

	return brcmf_fil_iovar_data_set(netdev_priv(ndev), "pfn_set",
					&pfn_param, sizeof(pfn_param));
}

static int
brcmf_cfg80211_sched_scan_start(struct wiphy *wiphy,
				struct net_device *ndev,
				struct cfg80211_sched_scan_request *request)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_info *cfg = wiphy_priv(wiphy);
	struct brcmf_pno_net_param_le pfn;
	int i;
	int ret = 0;

	WL_SCAN("Enter n_match_sets:%d   n_ssids:%d\n",
		request->n_match_sets, request->n_ssids);
	if (test_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status)) {
		WL_ERR("Scanning already: status (%lu)\n", cfg->scan_status);
		return -EAGAIN;
	}

	if (!request || !request->n_ssids || !request->n_match_sets) {
		WL_ERR("Invalid sched scan req!! n_ssids:%d\n",
		       request ? request->n_ssids : 0);
		return -EINVAL;
	}

	if (request->n_ssids > 0) {
		for (i = 0; i < request->n_ssids; i++) {
			/* Active scan req for ssids */
			WL_SCAN(">>> Active scan req for ssid (%s)\n",
				request->ssids[i].ssid);

			/*
			 * match_set ssids is a supert set of n_ssid list,
			 * so we need not add these set seperately.
			 */
		}
	}

	if (request->n_match_sets > 0) {
		/* clean up everything */
		ret = brcmf_dev_pno_clean(ndev);
		if  (ret < 0) {
			WL_ERR("failed error=%d\n", ret);
			return ret;
		}

		/* configure pno */
		ret = brcmf_dev_pno_config(ndev);
		if (ret < 0) {
			WL_ERR("PNO setup failed!! ret=%d\n", ret);
			return -EINVAL;
		}

		/* configure each match set */
		for (i = 0; i < request->n_match_sets; i++) {
			struct cfg80211_ssid *ssid;
			u32 ssid_len;

			ssid = &request->match_sets[i].ssid;
			ssid_len = ssid->ssid_len;

			if (!ssid_len) {
				WL_ERR("skip broadcast ssid\n");
				continue;
			}
			pfn.auth = cpu_to_le32(WLAN_AUTH_OPEN);
			pfn.wpa_auth = cpu_to_le32(BRCMF_PNO_WPA_AUTH_ANY);
			pfn.wsec = cpu_to_le32(0);
			pfn.infra = cpu_to_le32(1);
			pfn.flags = cpu_to_le32(1 << BRCMF_PNO_HIDDEN_BIT);
			pfn.ssid.SSID_len = cpu_to_le32(ssid_len);
			memcpy(pfn.ssid.SSID, ssid->ssid, ssid_len);
			ret = brcmf_fil_iovar_data_set(ifp, "pfn_add", &pfn,
						       sizeof(pfn));
			WL_SCAN(">>> PNO filter %s for ssid (%s)\n",
				ret == 0 ? "set" : "failed",
				ssid->ssid);
		}
		/* Enable the PNO */
		if (brcmf_fil_iovar_int_set(ifp, "pfn", 1) < 0) {
			WL_ERR("PNO enable failed!! ret=%d\n", ret);
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

static int brcmf_cfg80211_sched_scan_stop(struct wiphy *wiphy,
					  struct net_device *ndev)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);

	WL_SCAN("enter\n");
	brcmf_dev_pno_clean(ndev);
	if (cfg->sched_escan)
		brcmf_notify_escan_complete(cfg, ndev, true, true);
	return 0;
}

#ifdef CONFIG_NL80211_TESTMODE
static int brcmf_cfg80211_testmode(struct wiphy *wiphy, void *data, int len)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct net_device *ndev = cfg_to_ndev(cfg);
	struct brcmf_dcmd *dcmd = data;
	struct sk_buff *reply;
	int ret;

	WL_TRACE("cmd %x set %d buf %p len %d\n", dcmd->cmd, dcmd->set,
		 dcmd->buf, dcmd->len);

	if (dcmd->set)
		ret = brcmf_fil_cmd_data_set(netdev_priv(ndev), dcmd->cmd,
					     dcmd->buf, dcmd->len);
	else
		ret = brcmf_fil_cmd_data_get(netdev_priv(ndev), dcmd->cmd,
					     dcmd->buf, dcmd->len);
	if (ret == 0) {
		reply = cfg80211_testmode_alloc_reply_skb(wiphy, sizeof(*dcmd));
		nla_put(reply, NL80211_ATTR_TESTDATA, sizeof(*dcmd), dcmd);
		ret = cfg80211_testmode_reply(reply);
	}
	return ret;
}
#endif

static s32 brcmf_configure_opensecurity(struct net_device *ndev, s32 bssidx)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err;

	/* set auth */
	err = brcmf_fil_bsscfg_int_set(ifp, "auth", 0);
	if (err < 0) {
		WL_ERR("auth error %d\n", err);
		return err;
	}
	/* set wsec */
	err = brcmf_fil_bsscfg_int_set(ifp, "wsec", 0);
	if (err < 0) {
		WL_ERR("wsec error %d\n", err);
		return err;
	}
	/* set upper-layer auth */
	err = brcmf_fil_bsscfg_int_set(ifp, "wpa_auth", WPA_AUTH_NONE);
	if (err < 0) {
		WL_ERR("wpa_auth error %d\n", err);
		return err;
	}

	return 0;
}

static bool brcmf_valid_wpa_oui(u8 *oui, bool is_rsn_ie)
{
	if (is_rsn_ie)
		return (memcmp(oui, RSN_OUI, TLV_OUI_LEN) == 0);

	return (memcmp(oui, WPA_OUI, TLV_OUI_LEN) == 0);
}

static s32
brcmf_configure_wpaie(struct net_device *ndev, struct brcmf_vs_tlv *wpa_ie,
		     bool is_rsn_ie)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	u32 auth = 0; /* d11 open authentication */
	u16 count;
	s32 err = 0;
	s32 len = 0;
	u32 i;
	u32 wsec;
	u32 pval = 0;
	u32 gval = 0;
	u32 wpa_auth = 0;
	u32 offset;
	u8 *data;
	u16 rsn_cap;
	u32 wme_bss_disable;

	WL_TRACE("Enter\n");
	if (wpa_ie == NULL)
		goto exit;

	len = wpa_ie->len + TLV_HDR_LEN;
	data = (u8 *)wpa_ie;
	offset = 0;
	if (!is_rsn_ie)
		offset += VS_IE_FIXED_HDR_LEN;
	offset += WPA_IE_VERSION_LEN;

	/* check for multicast cipher suite */
	if (offset + WPA_IE_MIN_OUI_LEN > len) {
		err = -EINVAL;
		WL_ERR("no multicast cipher suite\n");
		goto exit;
	}

	if (!brcmf_valid_wpa_oui(&data[offset], is_rsn_ie)) {
		err = -EINVAL;
		WL_ERR("ivalid OUI\n");
		goto exit;
	}
	offset += TLV_OUI_LEN;

	/* pick up multicast cipher */
	switch (data[offset]) {
	case WPA_CIPHER_NONE:
		gval = 0;
		break;
	case WPA_CIPHER_WEP_40:
	case WPA_CIPHER_WEP_104:
		gval = WEP_ENABLED;
		break;
	case WPA_CIPHER_TKIP:
		gval = TKIP_ENABLED;
		break;
	case WPA_CIPHER_AES_CCM:
		gval = AES_ENABLED;
		break;
	default:
		err = -EINVAL;
		WL_ERR("Invalid multi cast cipher info\n");
		goto exit;
	}

	offset++;
	/* walk thru unicast cipher list and pick up what we recognize */
	count = data[offset] + (data[offset + 1] << 8);
	offset += WPA_IE_SUITE_COUNT_LEN;
	/* Check for unicast suite(s) */
	if (offset + (WPA_IE_MIN_OUI_LEN * count) > len) {
		err = -EINVAL;
		WL_ERR("no unicast cipher suite\n");
		goto exit;
	}
	for (i = 0; i < count; i++) {
		if (!brcmf_valid_wpa_oui(&data[offset], is_rsn_ie)) {
			err = -EINVAL;
			WL_ERR("ivalid OUI\n");
			goto exit;
		}
		offset += TLV_OUI_LEN;
		switch (data[offset]) {
		case WPA_CIPHER_NONE:
			break;
		case WPA_CIPHER_WEP_40:
		case WPA_CIPHER_WEP_104:
			pval |= WEP_ENABLED;
			break;
		case WPA_CIPHER_TKIP:
			pval |= TKIP_ENABLED;
			break;
		case WPA_CIPHER_AES_CCM:
			pval |= AES_ENABLED;
			break;
		default:
			WL_ERR("Ivalid unicast security info\n");
		}
		offset++;
	}
	/* walk thru auth management suite list and pick up what we recognize */
	count = data[offset] + (data[offset + 1] << 8);
	offset += WPA_IE_SUITE_COUNT_LEN;
	/* Check for auth key management suite(s) */
	if (offset + (WPA_IE_MIN_OUI_LEN * count) > len) {
		err = -EINVAL;
		WL_ERR("no auth key mgmt suite\n");
		goto exit;
	}
	for (i = 0; i < count; i++) {
		if (!brcmf_valid_wpa_oui(&data[offset], is_rsn_ie)) {
			err = -EINVAL;
			WL_ERR("ivalid OUI\n");
			goto exit;
		}
		offset += TLV_OUI_LEN;
		switch (data[offset]) {
		case RSN_AKM_NONE:
			WL_TRACE("RSN_AKM_NONE\n");
			wpa_auth |= WPA_AUTH_NONE;
			break;
		case RSN_AKM_UNSPECIFIED:
			WL_TRACE("RSN_AKM_UNSPECIFIED\n");
			is_rsn_ie ? (wpa_auth |= WPA2_AUTH_UNSPECIFIED) :
				    (wpa_auth |= WPA_AUTH_UNSPECIFIED);
			break;
		case RSN_AKM_PSK:
			WL_TRACE("RSN_AKM_PSK\n");
			is_rsn_ie ? (wpa_auth |= WPA2_AUTH_PSK) :
				    (wpa_auth |= WPA_AUTH_PSK);
			break;
		default:
			WL_ERR("Ivalid key mgmt info\n");
		}
		offset++;
	}

	if (is_rsn_ie) {
		wme_bss_disable = 1;
		if ((offset + RSN_CAP_LEN) <= len) {
			rsn_cap = data[offset] + (data[offset + 1] << 8);
			if (rsn_cap & RSN_CAP_PTK_REPLAY_CNTR_MASK)
				wme_bss_disable = 0;
		}
		/* set wme_bss_disable to sync RSN Capabilities */
		err = brcmf_fil_bsscfg_int_set(ifp, "wme_bss_disable",
					       wme_bss_disable);
		if (err < 0) {
			WL_ERR("wme_bss_disable error %d\n", err);
			goto exit;
		}
	}
	/* FOR WPS , set SES_OW_ENABLED */
	wsec = (pval | gval | SES_OW_ENABLED);

	/* set auth */
	err = brcmf_fil_bsscfg_int_set(ifp, "auth", auth);
	if (err < 0) {
		WL_ERR("auth error %d\n", err);
		goto exit;
	}
	/* set wsec */
	err = brcmf_fil_bsscfg_int_set(ifp, "wsec", wsec);
	if (err < 0) {
		WL_ERR("wsec error %d\n", err);
		goto exit;
	}
	/* set upper-layer auth */
	err = brcmf_fil_bsscfg_int_set(ifp, "wpa_auth", wpa_auth);
	if (err < 0) {
		WL_ERR("wpa_auth error %d\n", err);
		goto exit;
	}

exit:
	return err;
}

static s32
brcmf_parse_vndr_ies(const u8 *vndr_ie_buf, u32 vndr_ie_len,
		     struct parsed_vndr_ies *vndr_ies)
{
	s32 err = 0;
	struct brcmf_vs_tlv *vndrie;
	struct brcmf_tlv *ie;
	struct parsed_vndr_ie_info *parsed_info;
	s32 remaining_len;

	remaining_len = (s32)vndr_ie_len;
	memset(vndr_ies, 0, sizeof(*vndr_ies));

	ie = (struct brcmf_tlv *)vndr_ie_buf;
	while (ie) {
		if (ie->id != WLAN_EID_VENDOR_SPECIFIC)
			goto next;
		vndrie = (struct brcmf_vs_tlv *)ie;
		/* len should be bigger than OUI length + one */
		if (vndrie->len < (VS_IE_FIXED_HDR_LEN - TLV_HDR_LEN + 1)) {
			WL_ERR("invalid vndr ie. length is too small %d\n",
				vndrie->len);
			goto next;
		}
		/* if wpa or wme ie, do not add ie */
		if (!memcmp(vndrie->oui, (u8 *)WPA_OUI, TLV_OUI_LEN) &&
		    ((vndrie->oui_type == WPA_OUI_TYPE) ||
		    (vndrie->oui_type == WME_OUI_TYPE))) {
			WL_TRACE("Found WPA/WME oui. Do not add it\n");
			goto next;
		}

		parsed_info = &vndr_ies->ie_info[vndr_ies->count];

		/* save vndr ie information */
		parsed_info->ie_ptr = (char *)vndrie;
		parsed_info->ie_len = vndrie->len + TLV_HDR_LEN;
		memcpy(&parsed_info->vndrie, vndrie, sizeof(*vndrie));

		vndr_ies->count++;

		WL_TRACE("** OUI %02x %02x %02x, type 0x%02x\n",
			 parsed_info->vndrie.oui[0],
			 parsed_info->vndrie.oui[1],
			 parsed_info->vndrie.oui[2],
			 parsed_info->vndrie.oui_type);

		if (vndr_ies->count >= MAX_VNDR_IE_NUMBER)
			break;
next:
		remaining_len -= ie->len;
		if (remaining_len <= 2)
			ie = NULL;
		else
			ie = (struct brcmf_tlv *)(((u8 *)ie) + ie->len);
	}
	return err;
}

static u32
brcmf_vndr_ie(u8 *iebuf, s32 pktflag, u8 *ie_ptr, u32 ie_len, s8 *add_del_cmd)
{

	__le32 iecount_le;
	__le32 pktflag_le;

	strncpy(iebuf, add_del_cmd, VNDR_IE_CMD_LEN - 1);
	iebuf[VNDR_IE_CMD_LEN - 1] = '\0';

	iecount_le = cpu_to_le32(1);
	memcpy(&iebuf[VNDR_IE_COUNT_OFFSET], &iecount_le, sizeof(iecount_le));

	pktflag_le = cpu_to_le32(pktflag);
	memcpy(&iebuf[VNDR_IE_PKTFLAG_OFFSET], &pktflag_le, sizeof(pktflag_le));

	memcpy(&iebuf[VNDR_IE_VSIE_OFFSET], ie_ptr, ie_len);

	return ie_len + VNDR_IE_HDR_SIZE;
}

static
s32 brcmf_vif_set_mgmt_ie(struct brcmf_cfg80211_vif *vif, s32 pktflag,
			  const u8 *vndr_ie_buf, u32 vndr_ie_len)
{
	struct brcmf_if *ifp;
	struct vif_saved_ie *saved_ie;
	s32 err = 0;
	u8  *iovar_ie_buf;
	u8  *curr_ie_buf;
	u8  *mgmt_ie_buf = NULL;
	int mgmt_ie_buf_len;
	u32 *mgmt_ie_len;
	u32 del_add_ie_buf_len = 0;
	u32 total_ie_buf_len = 0;
	u32 parsed_ie_buf_len = 0;
	struct parsed_vndr_ies old_vndr_ies;
	struct parsed_vndr_ies new_vndr_ies;
	struct parsed_vndr_ie_info *vndrie_info;
	s32 i;
	u8 *ptr;
	int remained_buf_len;

	if (!vif)
		return -ENODEV;
	ifp = vif->ifp;
	saved_ie = &vif->saved_ie;

	WL_TRACE("bssidx %d, pktflag : 0x%02X\n", ifp->bssidx, pktflag);
	iovar_ie_buf = kzalloc(WL_EXTRA_BUF_MAX, GFP_KERNEL);
	if (!iovar_ie_buf)
		return -ENOMEM;
	curr_ie_buf = iovar_ie_buf;
	if (ifp->vif->mode == WL_MODE_AP) {
		switch (pktflag) {
		case VNDR_IE_PRBRSP_FLAG:
			mgmt_ie_buf = saved_ie->probe_res_ie;
			mgmt_ie_len = &saved_ie->probe_res_ie_len;
			mgmt_ie_buf_len = sizeof(saved_ie->probe_res_ie);
			break;
		case VNDR_IE_BEACON_FLAG:
			mgmt_ie_buf = saved_ie->beacon_ie;
			mgmt_ie_len = &saved_ie->beacon_ie_len;
			mgmt_ie_buf_len = sizeof(saved_ie->beacon_ie);
			break;
		default:
			err = -EPERM;
			WL_ERR("not suitable type\n");
			goto exit;
		}
	} else {
		err = -EPERM;
		WL_ERR("not suitable type\n");
		goto exit;
	}

	if (vndr_ie_len > mgmt_ie_buf_len) {
		err = -ENOMEM;
		WL_ERR("extra IE size too big\n");
		goto exit;
	}

	/* parse and save new vndr_ie in curr_ie_buff before comparing it */
	if (vndr_ie_buf && vndr_ie_len && curr_ie_buf) {
		ptr = curr_ie_buf;
		brcmf_parse_vndr_ies(vndr_ie_buf, vndr_ie_len, &new_vndr_ies);
		for (i = 0; i < new_vndr_ies.count; i++) {
			vndrie_info = &new_vndr_ies.ie_info[i];
			memcpy(ptr + parsed_ie_buf_len, vndrie_info->ie_ptr,
			       vndrie_info->ie_len);
			parsed_ie_buf_len += vndrie_info->ie_len;
		}
	}

	if (mgmt_ie_buf != NULL) {
		if (parsed_ie_buf_len && (parsed_ie_buf_len == *mgmt_ie_len) &&
		    (memcmp(mgmt_ie_buf, curr_ie_buf,
			    parsed_ie_buf_len) == 0)) {
			WL_TRACE("Previous mgmt IE is equals to current IE");
			goto exit;
		}

		/* parse old vndr_ie */
		brcmf_parse_vndr_ies(mgmt_ie_buf, *mgmt_ie_len, &old_vndr_ies);

		/* make a command to delete old ie */
		for (i = 0; i < old_vndr_ies.count; i++) {
			vndrie_info = &old_vndr_ies.ie_info[i];

			WL_TRACE("DEL ID : %d, Len: %d , OUI:%02x:%02x:%02x\n",
				 vndrie_info->vndrie.id,
				 vndrie_info->vndrie.len,
				 vndrie_info->vndrie.oui[0],
				 vndrie_info->vndrie.oui[1],
				 vndrie_info->vndrie.oui[2]);

			del_add_ie_buf_len = brcmf_vndr_ie(curr_ie_buf, pktflag,
							   vndrie_info->ie_ptr,
							   vndrie_info->ie_len,
							   "del");
			curr_ie_buf += del_add_ie_buf_len;
			total_ie_buf_len += del_add_ie_buf_len;
		}
	}

	*mgmt_ie_len = 0;
	/* Add if there is any extra IE */
	if (mgmt_ie_buf && parsed_ie_buf_len) {
		ptr = mgmt_ie_buf;

		remained_buf_len = mgmt_ie_buf_len;

		/* make a command to add new ie */
		for (i = 0; i < new_vndr_ies.count; i++) {
			vndrie_info = &new_vndr_ies.ie_info[i];

			WL_TRACE("ADDED ID : %d, Len: %d, OUI:%02x:%02x:%02x\n",
				 vndrie_info->vndrie.id,
				 vndrie_info->vndrie.len,
				 vndrie_info->vndrie.oui[0],
				 vndrie_info->vndrie.oui[1],
				 vndrie_info->vndrie.oui[2]);

			del_add_ie_buf_len = brcmf_vndr_ie(curr_ie_buf, pktflag,
							   vndrie_info->ie_ptr,
							   vndrie_info->ie_len,
							   "add");
			/* verify remained buf size before copy data */
			remained_buf_len -= vndrie_info->ie_len;
			if (remained_buf_len < 0) {
				WL_ERR("no space in mgmt_ie_buf: len left %d",
					remained_buf_len);
				break;
			}

			/* save the parsed IE in wl struct */
			memcpy(ptr + (*mgmt_ie_len), vndrie_info->ie_ptr,
			       vndrie_info->ie_len);
			*mgmt_ie_len += vndrie_info->ie_len;

			curr_ie_buf += del_add_ie_buf_len;
			total_ie_buf_len += del_add_ie_buf_len;
		}
	}
	if (total_ie_buf_len) {
		err  = brcmf_fil_bsscfg_data_set(ifp, "vndr_ie", iovar_ie_buf,
						 total_ie_buf_len);
		if (err)
			WL_ERR("vndr ie set error : %d\n", err);
	}

exit:
	kfree(iovar_ie_buf);
	return err;
}

static s32
brcmf_cfg80211_start_ap(struct wiphy *wiphy, struct net_device *ndev,
			struct cfg80211_ap_settings *settings)
{
	s32 ie_offset;
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_tlv *ssid_ie;
	struct brcmf_ssid_le ssid_le;
	s32 err = -EPERM;
	struct brcmf_tlv *rsn_ie;
	struct brcmf_vs_tlv *wpa_ie;
	struct brcmf_join_params join_params;
	s32 bssidx = 0;

	WL_TRACE("channel_type=%d, beacon_interval=%d, dtim_period=%d,\n",
		 settings->channel_type, settings->beacon_interval,
		 settings->dtim_period);
	WL_TRACE("ssid=%s(%zu), auth_type=%d, inactivity_timeout=%d\n",
		 settings->ssid, settings->ssid_len, settings->auth_type,
		 settings->inactivity_timeout);

	if (!test_bit(BRCMF_VIF_STATUS_AP_CREATING, &ifp->vif->sme_state)) {
		WL_ERR("Not in AP creation mode\n");
		return -EPERM;
	}

	memset(&ssid_le, 0, sizeof(ssid_le));
	if (settings->ssid == NULL || settings->ssid_len == 0) {
		ie_offset = DOT11_MGMT_HDR_LEN + DOT11_BCN_PRB_FIXED_LEN;
		ssid_ie = brcmf_parse_tlvs(
				(u8 *)&settings->beacon.head[ie_offset],
				settings->beacon.head_len - ie_offset,
				WLAN_EID_SSID);
		if (!ssid_ie)
			return -EINVAL;

		memcpy(ssid_le.SSID, ssid_ie->data, ssid_ie->len);
		ssid_le.SSID_len = cpu_to_le32(ssid_ie->len);
		WL_TRACE("SSID is (%s) in Head\n", ssid_le.SSID);
	} else {
		memcpy(ssid_le.SSID, settings->ssid, settings->ssid_len);
		ssid_le.SSID_len = cpu_to_le32((u32)settings->ssid_len);
	}

	brcmf_set_mpc(ndev, 0);
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_DOWN, 1);
	if (err < 0) {
		WL_ERR("BRCMF_C_DOWN error %d\n", err);
		goto exit;
	}
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_INFRA, 1);
	if (err < 0) {
		WL_ERR("SET INFRA error %d\n", err);
		goto exit;
	}
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_AP, 1);
	if (err < 0) {
		WL_ERR("setting AP mode failed %d\n", err);
		goto exit;
	}

	/* find the RSN_IE */
	rsn_ie = brcmf_parse_tlvs((u8 *)settings->beacon.tail,
				  settings->beacon.tail_len, WLAN_EID_RSN);

	/* find the WPA_IE */
	wpa_ie = brcmf_find_wpaie((u8 *)settings->beacon.tail,
				  settings->beacon.tail_len);

	if ((wpa_ie != NULL || rsn_ie != NULL)) {
		WL_TRACE("WPA(2) IE is found\n");
		if (wpa_ie != NULL) {
			/* WPA IE */
			err = brcmf_configure_wpaie(ndev, wpa_ie, false);
			if (err < 0)
				goto exit;
		} else {
			/* RSN IE */
			err = brcmf_configure_wpaie(ndev,
				(struct brcmf_vs_tlv *)rsn_ie, true);
			if (err < 0)
				goto exit;
		}
	} else {
		WL_TRACE("No WPA(2) IEs found\n");
		brcmf_configure_opensecurity(ndev, bssidx);
	}
	/* Set Beacon IEs to FW */
	err = brcmf_vif_set_mgmt_ie(ndev_to_vif(ndev),
				    VNDR_IE_BEACON_FLAG,
				    settings->beacon.tail,
				    settings->beacon.tail_len);
	if (err)
		WL_ERR("Set Beacon IE Failed\n");
	else
		WL_TRACE("Applied Vndr IEs for Beacon\n");

	/* Set Probe Response IEs to FW */
	err = brcmf_vif_set_mgmt_ie(ndev_to_vif(ndev),
				    VNDR_IE_PRBRSP_FLAG,
				    settings->beacon.proberesp_ies,
				    settings->beacon.proberesp_ies_len);
	if (err)
		WL_ERR("Set Probe Resp IE Failed\n");
	else
		WL_TRACE("Applied Vndr IEs for Probe Resp\n");

	if (settings->beacon_interval) {
		err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_BCNPRD,
					    settings->beacon_interval);
		if (err < 0) {
			WL_ERR("Beacon Interval Set Error, %d\n", err);
			goto exit;
		}
	}
	if (settings->dtim_period) {
		err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_DTIMPRD,
					    settings->dtim_period);
		if (err < 0) {
			WL_ERR("DTIM Interval Set Error, %d\n", err);
			goto exit;
		}
	}
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_UP, 1);
	if (err < 0) {
		WL_ERR("BRCMF_C_UP error (%d)\n", err);
		goto exit;
	}

	memset(&join_params, 0, sizeof(join_params));
	/* join parameters starts with ssid */
	memcpy(&join_params.ssid_le, &ssid_le, sizeof(ssid_le));
	/* create softap */
	err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_SSID,
				     &join_params, sizeof(join_params));
	if (err < 0) {
		WL_ERR("SET SSID error (%d)\n", err);
		goto exit;
	}
	clear_bit(BRCMF_VIF_STATUS_AP_CREATING, &ifp->vif->sme_state);
	set_bit(BRCMF_VIF_STATUS_AP_CREATED, &ifp->vif->sme_state);

exit:
	if (err)
		brcmf_set_mpc(ndev, 1);
	return err;
}

static int brcmf_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	s32 err = -EPERM;

	WL_TRACE("Enter\n");

	if (cfg->conf->mode == WL_MODE_AP) {
		/* Due to most likely deauths outstanding we sleep */
		/* first to make sure they get processed by fw. */
		msleep(400);
		err = brcmf_fil_cmd_int_set(netdev_priv(ndev),
					    BRCMF_C_SET_AP, 0);
		if (err < 0) {
			WL_ERR("setting AP mode failed %d\n", err);
			goto exit;
		}
		err = brcmf_fil_cmd_int_set(netdev_priv(ndev), BRCMF_C_UP, 0);
		if (err < 0) {
			WL_ERR("BRCMF_C_UP error %d\n", err);
			goto exit;
		}
		brcmf_set_mpc(ndev, 1);
		clear_bit(BRCMF_VIF_STATUS_AP_CREATING, &ifp->vif->sme_state);
		clear_bit(BRCMF_VIF_STATUS_AP_CREATED, &ifp->vif->sme_state);
	}
exit:
	return err;
}

static int
brcmf_cfg80211_del_station(struct wiphy *wiphy, struct net_device *ndev,
			   u8 *mac)
{
	struct brcmf_scb_val_le scbval;
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err;

	if (!mac)
		return -EFAULT;

	WL_TRACE("Enter %pM\n", mac);

	if (!check_vif_up(ifp->vif))
		return -EIO;

	memcpy(&scbval.ea, mac, ETH_ALEN);
	scbval.val = cpu_to_le32(WLAN_REASON_DEAUTH_LEAVING);
	err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SCB_DEAUTHENTICATE_FOR_REASON,
				     &scbval, sizeof(scbval));
	if (err)
		WL_ERR("SCB_DEAUTHENTICATE_FOR_REASON failed %d\n", err);

	WL_TRACE("Exit\n");
	return err;
}

static struct cfg80211_ops wl_cfg80211_ops = {
	.change_virtual_intf = brcmf_cfg80211_change_iface,
	.scan = brcmf_cfg80211_scan,
	.set_wiphy_params = brcmf_cfg80211_set_wiphy_params,
	.join_ibss = brcmf_cfg80211_join_ibss,
	.leave_ibss = brcmf_cfg80211_leave_ibss,
	.get_station = brcmf_cfg80211_get_station,
	.set_tx_power = brcmf_cfg80211_set_tx_power,
	.get_tx_power = brcmf_cfg80211_get_tx_power,
	.add_key = brcmf_cfg80211_add_key,
	.del_key = brcmf_cfg80211_del_key,
	.get_key = brcmf_cfg80211_get_key,
	.set_default_key = brcmf_cfg80211_config_default_key,
	.set_default_mgmt_key = brcmf_cfg80211_config_default_mgmt_key,
	.set_power_mgmt = brcmf_cfg80211_set_power_mgmt,
	.set_bitrate_mask = brcmf_cfg80211_set_bitrate_mask,
	.connect = brcmf_cfg80211_connect,
	.disconnect = brcmf_cfg80211_disconnect,
	.suspend = brcmf_cfg80211_suspend,
	.resume = brcmf_cfg80211_resume,
	.set_pmksa = brcmf_cfg80211_set_pmksa,
	.del_pmksa = brcmf_cfg80211_del_pmksa,
	.flush_pmksa = brcmf_cfg80211_flush_pmksa,
	.start_ap = brcmf_cfg80211_start_ap,
	.stop_ap = brcmf_cfg80211_stop_ap,
	.del_station = brcmf_cfg80211_del_station,
	.sched_scan_start = brcmf_cfg80211_sched_scan_start,
	.sched_scan_stop = brcmf_cfg80211_sched_scan_stop,
#ifdef CONFIG_NL80211_TESTMODE
	.testmode_cmd = brcmf_cfg80211_testmode
#endif
};

static s32 brcmf_mode_to_nl80211_iftype(s32 mode)
{
	s32 err = 0;

	switch (mode) {
	case WL_MODE_BSS:
		return NL80211_IFTYPE_STATION;
	case WL_MODE_IBSS:
		return NL80211_IFTYPE_ADHOC;
	default:
		return NL80211_IFTYPE_UNSPECIFIED;
	}

	return err;
}

static void brcmf_wiphy_pno_params(struct wiphy *wiphy)
{
	/* scheduled scan settings */
	wiphy->max_sched_scan_ssids = BRCMF_PNO_MAX_PFN_COUNT;
	wiphy->max_match_sets = BRCMF_PNO_MAX_PFN_COUNT;
	wiphy->max_sched_scan_ie_len = BRCMF_SCAN_IE_LEN_MAX;
	wiphy->flags |= WIPHY_FLAG_SUPPORTS_SCHED_SCAN;
}

static struct wiphy *brcmf_setup_wiphy(struct device *phydev)
{
	struct wiphy *wiphy;
	s32 err = 0;

	wiphy = wiphy_new(&wl_cfg80211_ops, sizeof(struct brcmf_cfg80211_info));
	if (!wiphy) {
		WL_ERR("Could not allocate wiphy device\n");
		return ERR_PTR(-ENOMEM);
	}
	set_wiphy_dev(wiphy, phydev);
	wiphy->max_scan_ssids = WL_NUM_SCAN_MAX;
	wiphy->max_num_pmkids = WL_NUM_PMKIDS_MAX;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				 BIT(NL80211_IFTYPE_ADHOC) |
				 BIT(NL80211_IFTYPE_AP);
	wiphy->bands[IEEE80211_BAND_2GHZ] = &__wl_band_2ghz;
	wiphy->bands[IEEE80211_BAND_5GHZ] = &__wl_band_5ghz_a;	/* Set
						* it as 11a by default.
						* This will be updated with
						* 11n phy tables in
						* "ifconfig up"
						* if phy has 11n capability
						*/
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wiphy->cipher_suites = __wl_cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(__wl_cipher_suites);
	wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;	/* enable power
								 * save mode
								 * by default
								 */
	brcmf_wiphy_pno_params(wiphy);
	err = wiphy_register(wiphy);
	if (err < 0) {
		WL_ERR("Could not register wiphy device (%d)\n", err);
		wiphy_free(wiphy);
		return ERR_PTR(err);
	}
	return wiphy;
}

static
struct brcmf_cfg80211_vif *brcmf_alloc_vif(struct brcmf_cfg80211_info *cfg,
					   struct net_device *netdev,
					   s32 mode, bool pm_block)
{
	struct brcmf_cfg80211_vif *vif;

	if (cfg->vif_cnt == BRCMF_IFACE_MAX_CNT)
		return ERR_PTR(-ENOSPC);

	vif = kzalloc(sizeof(*vif), GFP_KERNEL);
	if (!vif)
		return ERR_PTR(-ENOMEM);

	vif->wdev.wiphy = cfg->wiphy;
	vif->wdev.netdev = netdev;
	vif->wdev.iftype = brcmf_mode_to_nl80211_iftype(mode);

	if (netdev) {
		vif->ifp = netdev_priv(netdev);
		netdev->ieee80211_ptr = &vif->wdev;
		SET_NETDEV_DEV(netdev, wiphy_dev(cfg->wiphy));
	}

	vif->mode = mode;
	vif->pm_block = pm_block;
	vif->roam_off = -1;

	brcmf_init_prof(&vif->profile);

	list_add_tail(&vif->list, &cfg->vif_list);
	cfg->vif_cnt++;
	return vif;
}

static void brcmf_free_vif(struct brcmf_cfg80211_vif *vif)
{
	struct brcmf_cfg80211_info *cfg;
	struct wiphy *wiphy;

	wiphy = vif->wdev.wiphy;
	cfg = wiphy_priv(wiphy);
	list_del(&vif->list);
	cfg->vif_cnt--;

	kfree(vif);
	if (!cfg->vif_cnt) {
		wiphy_unregister(wiphy);
		wiphy_free(wiphy);
	}
}

static bool brcmf_is_linkup(struct brcmf_cfg80211_info *cfg,
			    const struct brcmf_event_msg *e)
{
	u32 event = be32_to_cpu(e->event_type);
	u32 status = be32_to_cpu(e->status);

	if (event == BRCMF_E_SET_SSID && status == BRCMF_E_STATUS_SUCCESS) {
		WL_CONN("Processing set ssid\n");
		cfg->link_up = true;
		return true;
	}

	return false;
}

static bool brcmf_is_linkdown(struct brcmf_cfg80211_info *cfg,
			      const struct brcmf_event_msg *e)
{
	u32 event = be32_to_cpu(e->event_type);
	u16 flags = be16_to_cpu(e->flags);

	if (event == BRCMF_E_LINK && (!(flags & BRCMF_EVENT_MSG_LINK))) {
		WL_CONN("Processing link down\n");
		return true;
	}
	return false;
}

static bool brcmf_is_nonetwork(struct brcmf_cfg80211_info *cfg,
			       const struct brcmf_event_msg *e)
{
	u32 event = be32_to_cpu(e->event_type);
	u32 status = be32_to_cpu(e->status);

	if (event == BRCMF_E_LINK && status == BRCMF_E_STATUS_NO_NETWORKS) {
		WL_CONN("Processing Link %s & no network found\n",
				be16_to_cpu(e->flags) & BRCMF_EVENT_MSG_LINK ?
				"up" : "down");
		return true;
	}

	if (event == BRCMF_E_SET_SSID && status != BRCMF_E_STATUS_SUCCESS) {
		WL_CONN("Processing connecting & no network found\n");
		return true;
	}

	return false;
}

static void brcmf_clear_assoc_ies(struct brcmf_cfg80211_info *cfg)
{
	struct brcmf_cfg80211_connect_info *conn_info = cfg_to_conn(cfg);

	kfree(conn_info->req_ie);
	conn_info->req_ie = NULL;
	conn_info->req_ie_len = 0;
	kfree(conn_info->resp_ie);
	conn_info->resp_ie = NULL;
	conn_info->resp_ie_len = 0;
}

static s32 brcmf_get_assoc_ies(struct brcmf_cfg80211_info *cfg)
{
	struct brcmf_if *ifp = netdev_priv(cfg_to_ndev(cfg));
	struct brcmf_cfg80211_assoc_ielen_le *assoc_info;
	struct brcmf_cfg80211_connect_info *conn_info = cfg_to_conn(cfg);
	u32 req_len;
	u32 resp_len;
	s32 err = 0;

	brcmf_clear_assoc_ies(cfg);

	err = brcmf_fil_iovar_data_get(ifp, "assoc_info",
				       cfg->extra_buf, WL_ASSOC_INFO_MAX);
	if (err) {
		WL_ERR("could not get assoc info (%d)\n", err);
		return err;
	}
	assoc_info =
		(struct brcmf_cfg80211_assoc_ielen_le *)cfg->extra_buf;
	req_len = le32_to_cpu(assoc_info->req_len);
	resp_len = le32_to_cpu(assoc_info->resp_len);
	if (req_len) {
		err = brcmf_fil_iovar_data_get(ifp, "assoc_req_ies",
					       cfg->extra_buf,
					       WL_ASSOC_INFO_MAX);
		if (err) {
			WL_ERR("could not get assoc req (%d)\n", err);
			return err;
		}
		conn_info->req_ie_len = req_len;
		conn_info->req_ie =
		    kmemdup(cfg->extra_buf, conn_info->req_ie_len,
			    GFP_KERNEL);
	} else {
		conn_info->req_ie_len = 0;
		conn_info->req_ie = NULL;
	}
	if (resp_len) {
		err = brcmf_fil_iovar_data_get(ifp, "assoc_resp_ies",
					       cfg->extra_buf,
					       WL_ASSOC_INFO_MAX);
		if (err) {
			WL_ERR("could not get assoc resp (%d)\n", err);
			return err;
		}
		conn_info->resp_ie_len = resp_len;
		conn_info->resp_ie =
		    kmemdup(cfg->extra_buf, conn_info->resp_ie_len,
			    GFP_KERNEL);
	} else {
		conn_info->resp_ie_len = 0;
		conn_info->resp_ie = NULL;
	}
	WL_CONN("req len (%d) resp len (%d)\n",
	       conn_info->req_ie_len, conn_info->resp_ie_len);

	return err;
}

static s32
brcmf_bss_roaming_done(struct brcmf_cfg80211_info *cfg,
		       struct net_device *ndev,
		       const struct brcmf_event_msg *e)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_profile *profile = &ifp->vif->profile;
	struct brcmf_cfg80211_connect_info *conn_info = cfg_to_conn(cfg);
	struct wiphy *wiphy = cfg_to_wiphy(cfg);
	struct ieee80211_channel *notify_channel = NULL;
	struct ieee80211_supported_band *band;
	struct brcmf_bss_info_le *bi;
	u32 freq;
	s32 err = 0;
	u32 target_channel;
	u8 *buf;

	WL_TRACE("Enter\n");

	brcmf_get_assoc_ies(cfg);
	memcpy(profile->bssid, e->addr, ETH_ALEN);
	brcmf_update_bss_info(cfg);

	buf = kzalloc(WL_BSS_INFO_MAX, GFP_KERNEL);
	if (buf == NULL) {
		err = -ENOMEM;
		goto done;
	}

	/* data sent to dongle has to be little endian */
	*(__le32 *)buf = cpu_to_le32(WL_BSS_INFO_MAX);
	err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BSS_INFO,
				     buf, WL_BSS_INFO_MAX);

	if (err)
		goto done;

	bi = (struct brcmf_bss_info_le *)(buf + 4);
	target_channel = bi->ctl_ch ? bi->ctl_ch :
				      CHSPEC_CHANNEL(le16_to_cpu(bi->chanspec));

	if (target_channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];

	freq = ieee80211_channel_to_frequency(target_channel, band->band);
	notify_channel = ieee80211_get_channel(wiphy, freq);

done:
	kfree(buf);
	cfg80211_roamed(ndev, notify_channel, (u8 *)profile->bssid,
			conn_info->req_ie, conn_info->req_ie_len,
			conn_info->resp_ie, conn_info->resp_ie_len, GFP_KERNEL);
	WL_CONN("Report roaming result\n");

	set_bit(BRCMF_VIF_STATUS_CONNECTED, &ifp->vif->sme_state);
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_bss_connect_done(struct brcmf_cfg80211_info *cfg,
		       struct net_device *ndev, const struct brcmf_event_msg *e,
		       bool completed)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_profile *profile = &ifp->vif->profile;
	struct brcmf_cfg80211_connect_info *conn_info = cfg_to_conn(cfg);
	s32 err = 0;

	WL_TRACE("Enter\n");

	if (test_and_clear_bit(BRCMF_VIF_STATUS_CONNECTING,
			       &ifp->vif->sme_state)) {
		if (completed) {
			brcmf_get_assoc_ies(cfg);
			memcpy(profile->bssid, e->addr, ETH_ALEN);
			brcmf_update_bss_info(cfg);
		}
		cfg80211_connect_result(ndev,
					(u8 *)profile->bssid,
					conn_info->req_ie,
					conn_info->req_ie_len,
					conn_info->resp_ie,
					conn_info->resp_ie_len,
					completed ? WLAN_STATUS_SUCCESS :
						    WLAN_STATUS_AUTH_TIMEOUT,
					GFP_KERNEL);
		if (completed)
			set_bit(BRCMF_VIF_STATUS_CONNECTED,
				&ifp->vif->sme_state);
		WL_CONN("Report connect result - connection %s\n",
				completed ? "succeeded" : "failed");
	}
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_notify_connect_status_ap(struct brcmf_cfg80211_info *cfg,
			       struct net_device *ndev,
			       const struct brcmf_event_msg *e, void *data)
{
	s32 err = 0;
	u32 event = be32_to_cpu(e->event_type);
	u32 reason = be32_to_cpu(e->reason);
	u32 len = be32_to_cpu(e->datalen);
	static int generation;

	struct station_info sinfo;

	WL_CONN("event %d, reason %d\n", event, reason);
	memset(&sinfo, 0, sizeof(sinfo));

	sinfo.filled = 0;
	if (((event == BRCMF_E_ASSOC_IND) || (event == BRCMF_E_REASSOC_IND)) &&
	    reason == BRCMF_E_STATUS_SUCCESS) {
		sinfo.filled = STATION_INFO_ASSOC_REQ_IES;
		if (!data) {
			WL_ERR("No IEs present in ASSOC/REASSOC_IND");
			return -EINVAL;
		}
		sinfo.assoc_req_ies = data;
		sinfo.assoc_req_ies_len = len;
		generation++;
		sinfo.generation = generation;
		cfg80211_new_sta(ndev, e->addr, &sinfo, GFP_ATOMIC);
	} else if ((event == BRCMF_E_DISASSOC_IND) ||
		   (event == BRCMF_E_DEAUTH_IND) ||
		   (event == BRCMF_E_DEAUTH)) {
		generation++;
		sinfo.generation = generation;
		cfg80211_del_sta(ndev, e->addr, GFP_ATOMIC);
	}
	return err;
}

static s32
brcmf_notify_connect_status(struct brcmf_cfg80211_info *cfg,
			    struct net_device *ndev,
			    const struct brcmf_event_msg *e, void *data)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_profile *profile = &ifp->vif->profile;
	s32 err = 0;

	if (cfg->conf->mode == WL_MODE_AP) {
		err = brcmf_notify_connect_status_ap(cfg, ndev, e, data);
	} else if (brcmf_is_linkup(cfg, e)) {
		WL_CONN("Linkup\n");
		if (brcmf_is_ibssmode(cfg)) {
			memcpy(profile->bssid, e->addr, ETH_ALEN);
			wl_inform_ibss(cfg, ndev, e->addr);
			cfg80211_ibss_joined(ndev, e->addr, GFP_KERNEL);
			clear_bit(BRCMF_VIF_STATUS_CONNECTING,
				  &ifp->vif->sme_state);
			set_bit(BRCMF_VIF_STATUS_CONNECTED,
				&ifp->vif->sme_state);
		} else
			brcmf_bss_connect_done(cfg, ndev, e, true);
	} else if (brcmf_is_linkdown(cfg, e)) {
		WL_CONN("Linkdown\n");
		if (brcmf_is_ibssmode(cfg)) {
			clear_bit(BRCMF_VIF_STATUS_CONNECTING,
				  &ifp->vif->sme_state);
			if (test_and_clear_bit(BRCMF_VIF_STATUS_CONNECTED,
					       &ifp->vif->sme_state))
				brcmf_link_down(cfg);
		} else {
			brcmf_bss_connect_done(cfg, ndev, e, false);
			if (test_and_clear_bit(BRCMF_VIF_STATUS_CONNECTED,
					       &ifp->vif->sme_state)) {
				cfg80211_disconnected(ndev, 0, NULL, 0,
						      GFP_KERNEL);
				brcmf_link_down(cfg);
			}
		}
		brcmf_init_prof(ndev_to_prof(ndev));
	} else if (brcmf_is_nonetwork(cfg, e)) {
		if (brcmf_is_ibssmode(cfg))
			clear_bit(BRCMF_VIF_STATUS_CONNECTING,
				  &ifp->vif->sme_state);
		else
			brcmf_bss_connect_done(cfg, ndev, e, false);
	}

	return err;
}

static s32
brcmf_notify_roaming_status(struct brcmf_cfg80211_info *cfg,
			    struct net_device *ndev,
			    const struct brcmf_event_msg *e, void *data)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err = 0;
	u32 event = be32_to_cpu(e->event_type);
	u32 status = be32_to_cpu(e->status);

	if (event == BRCMF_E_ROAM && status == BRCMF_E_STATUS_SUCCESS) {
		if (test_bit(BRCMF_VIF_STATUS_CONNECTED, &ifp->vif->sme_state))
			brcmf_bss_roaming_done(cfg, ndev, e);
		else
			brcmf_bss_connect_done(cfg, ndev, e, true);
	}

	return err;
}

static s32
brcmf_notify_mic_status(struct brcmf_cfg80211_info *cfg,
			struct net_device *ndev,
			const struct brcmf_event_msg *e, void *data)
{
	u16 flags = be16_to_cpu(e->flags);
	enum nl80211_key_type key_type;

	if (flags & BRCMF_EVENT_MSG_GROUP)
		key_type = NL80211_KEYTYPE_GROUP;
	else
		key_type = NL80211_KEYTYPE_PAIRWISE;

	cfg80211_michael_mic_failure(ndev, (u8 *)&e->addr, key_type, -1,
				     NULL, GFP_KERNEL);

	return 0;
}

static void brcmf_init_conf(struct brcmf_cfg80211_conf *conf)
{
	conf->mode = (u32)-1;
	conf->frag_threshold = (u32)-1;
	conf->rts_threshold = (u32)-1;
	conf->retry_short = (u32)-1;
	conf->retry_long = (u32)-1;
	conf->tx_power = -1;
}

static void brcmf_init_eloop_handler(struct brcmf_cfg80211_event_loop *el)
{
	memset(el, 0, sizeof(*el));
	el->handler[BRCMF_E_LINK] = brcmf_notify_connect_status;
	el->handler[BRCMF_E_DEAUTH_IND] = brcmf_notify_connect_status;
	el->handler[BRCMF_E_DEAUTH] = brcmf_notify_connect_status;
	el->handler[BRCMF_E_DISASSOC_IND] = brcmf_notify_connect_status;
	el->handler[BRCMF_E_ASSOC_IND] = brcmf_notify_connect_status;
	el->handler[BRCMF_E_REASSOC_IND] = brcmf_notify_connect_status;
	el->handler[BRCMF_E_ROAM] = brcmf_notify_roaming_status;
	el->handler[BRCMF_E_MIC_ERROR] = brcmf_notify_mic_status;
	el->handler[BRCMF_E_SET_SSID] = brcmf_notify_connect_status;
	el->handler[BRCMF_E_PFN_NET_FOUND] = brcmf_notify_sched_scan_results;
}

static void brcmf_deinit_priv_mem(struct brcmf_cfg80211_info *cfg)
{
	kfree(cfg->conf);
	cfg->conf = NULL;
	kfree(cfg->escan_ioctl_buf);
	cfg->escan_ioctl_buf = NULL;
	kfree(cfg->extra_buf);
	cfg->extra_buf = NULL;
	kfree(cfg->pmk_list);
	cfg->pmk_list = NULL;
}

static s32 brcmf_init_priv_mem(struct brcmf_cfg80211_info *cfg)
{
	cfg->conf = kzalloc(sizeof(*cfg->conf), GFP_KERNEL);
	if (!cfg->conf)
		goto init_priv_mem_out;
	cfg->escan_ioctl_buf = kzalloc(BRCMF_DCMD_MEDLEN, GFP_KERNEL);
	if (!cfg->escan_ioctl_buf)
		goto init_priv_mem_out;
	cfg->extra_buf = kzalloc(WL_EXTRA_BUF_MAX, GFP_KERNEL);
	if (!cfg->extra_buf)
		goto init_priv_mem_out;
	cfg->pmk_list = kzalloc(sizeof(*cfg->pmk_list), GFP_KERNEL);
	if (!cfg->pmk_list)
		goto init_priv_mem_out;

	return 0;

init_priv_mem_out:
	brcmf_deinit_priv_mem(cfg);

	return -ENOMEM;
}

/*
* retrieve first queued event from head
*/

static struct brcmf_cfg80211_event_q *brcmf_deq_event(
	struct brcmf_cfg80211_info *cfg)
{
	struct brcmf_cfg80211_event_q *e = NULL;

	spin_lock_irq(&cfg->evt_q_lock);
	if (!list_empty(&cfg->evt_q_list)) {
		e = list_first_entry(&cfg->evt_q_list,
				     struct brcmf_cfg80211_event_q, evt_q_list);
		list_del(&e->evt_q_list);
	}
	spin_unlock_irq(&cfg->evt_q_lock);

	return e;
}

/*
*	push event to tail of the queue
*
*	remark: this function may not sleep as it is called in atomic context.
*/

static s32
brcmf_enq_event(struct brcmf_cfg80211_info *cfg, u32 event,
		const struct brcmf_event_msg *msg, void *data)
{
	struct brcmf_cfg80211_event_q *e;
	s32 err = 0;
	ulong flags;
	u32 data_len;
	u32 total_len;

	total_len = sizeof(struct brcmf_cfg80211_event_q);
	if (data)
		data_len = be32_to_cpu(msg->datalen);
	else
		data_len = 0;
	total_len += data_len;
	e = kzalloc(total_len, GFP_ATOMIC);
	if (!e)
		return -ENOMEM;

	e->etype = event;
	memcpy(&e->emsg, msg, sizeof(struct brcmf_event_msg));
	if (data)
		memcpy(&e->edata, data, data_len);

	spin_lock_irqsave(&cfg->evt_q_lock, flags);
	list_add_tail(&e->evt_q_list, &cfg->evt_q_list);
	spin_unlock_irqrestore(&cfg->evt_q_lock, flags);

	return err;
}

static void brcmf_put_event(struct brcmf_cfg80211_event_q *e)
{
	kfree(e);
}

static void brcmf_cfg80211_event_handler(struct work_struct *work)
{
	struct brcmf_cfg80211_info *cfg =
			container_of(work, struct brcmf_cfg80211_info,
				     event_work);
	struct brcmf_cfg80211_event_q *e;

	e = brcmf_deq_event(cfg);
	if (unlikely(!e)) {
		WL_ERR("event queue empty...\n");
		return;
	}

	do {
		WL_INFO("event type (%d)\n", e->etype);
		if (cfg->el.handler[e->etype])
			cfg->el.handler[e->etype](cfg,
						       cfg_to_ndev(cfg),
						       &e->emsg, e->edata);
		else
			WL_INFO("Unknown Event (%d): ignoring\n", e->etype);
		brcmf_put_event(e);
	} while ((e = brcmf_deq_event(cfg)));

}

static void brcmf_init_eq(struct brcmf_cfg80211_info *cfg)
{
	spin_lock_init(&cfg->evt_q_lock);
	INIT_LIST_HEAD(&cfg->evt_q_list);
}

static void brcmf_flush_eq(struct brcmf_cfg80211_info *cfg)
{
	struct brcmf_cfg80211_event_q *e;

	spin_lock_irq(&cfg->evt_q_lock);
	while (!list_empty(&cfg->evt_q_list)) {
		e = list_first_entry(&cfg->evt_q_list,
				     struct brcmf_cfg80211_event_q, evt_q_list);
		list_del(&e->evt_q_list);
		kfree(e);
	}
	spin_unlock_irq(&cfg->evt_q_lock);
}

static s32 wl_init_priv(struct brcmf_cfg80211_info *cfg)
{
	s32 err = 0;

	cfg->scan_request = NULL;
	cfg->pwr_save = true;
	cfg->roam_on = true;	/* roam on & off switch.
				 we enable roam per default */
	cfg->active_scan = true;	/* we do active scan for
				 specific scan per default */
	cfg->dongle_up = false;	/* dongle is not up yet */
	brcmf_init_eq(cfg);
	err = brcmf_init_priv_mem(cfg);
	if (err)
		return err;
	INIT_WORK(&cfg->event_work, brcmf_cfg80211_event_handler);
	brcmf_init_eloop_handler(&cfg->el);
	mutex_init(&cfg->usr_sync);
	brcmf_init_escan(cfg);
	brcmf_init_conf(cfg->conf);
	brcmf_link_down(cfg);

	return err;
}

static void wl_deinit_priv(struct brcmf_cfg80211_info *cfg)
{
	cancel_work_sync(&cfg->event_work);
	cfg->dongle_up = false;	/* dongle down */
	brcmf_flush_eq(cfg);
	brcmf_link_down(cfg);
	brcmf_abort_scanning(cfg);
	brcmf_deinit_priv_mem(cfg);
}

struct brcmf_cfg80211_info *brcmf_cfg80211_attach(struct brcmf_pub *drvr)
{
	struct net_device *ndev = drvr->iflist[0]->ndev;
	struct device *busdev = drvr->dev;
	struct brcmf_cfg80211_info *cfg;
	struct wiphy *wiphy;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	s32 err = 0;

	if (!ndev) {
		WL_ERR("ndev is invalid\n");
		return NULL;
	}

	ifp = netdev_priv(ndev);
	wiphy = brcmf_setup_wiphy(busdev);
	if (IS_ERR(wiphy))
		return NULL;

	cfg = wiphy_priv(wiphy);
	cfg->wiphy = wiphy;
	cfg->pub = drvr;
	INIT_LIST_HEAD(&cfg->vif_list);

	vif = brcmf_alloc_vif(cfg, ndev, WL_MODE_BSS, false);
	if (IS_ERR(vif)) {
		wiphy_free(wiphy);
		return NULL;
	}

	err = wl_init_priv(cfg);
	if (err) {
		WL_ERR("Failed to init iwm_priv (%d)\n", err);
		goto cfg80211_attach_out;
	}

	ifp->vif = vif;
	return cfg;

cfg80211_attach_out:
	brcmf_free_vif(vif);
	return NULL;
}

void brcmf_cfg80211_detach(struct brcmf_cfg80211_info *cfg)
{
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_cfg80211_vif *tmp;

	wl_deinit_priv(cfg);
	list_for_each_entry_safe(vif, tmp, &cfg->vif_list, list) {
		brcmf_free_vif(vif);
	}
}

void
brcmf_cfg80211_event(struct net_device *ndev,
		  const struct brcmf_event_msg *e, void *data)
{
	u32 event_type = be32_to_cpu(e->event_type);
	struct brcmf_cfg80211_info *cfg = ndev_to_cfg(ndev);

	if (!brcmf_enq_event(cfg, event_type, e, data))
		schedule_work(&cfg->event_work);
}

static s32 brcmf_dongle_eventmsg(struct net_device *ndev)
{
	s8 eventmask[BRCMF_EVENTING_MASK_LEN];
	s32 err = 0;

	WL_TRACE("Enter\n");

	/* Setup event_msgs */
	err = brcmf_fil_iovar_data_get(netdev_priv(ndev), "event_msgs",
				       eventmask, BRCMF_EVENTING_MASK_LEN);
	if (err) {
		WL_ERR("Get event_msgs error (%d)\n", err);
		goto dongle_eventmsg_out;
	}

	setbit(eventmask, BRCMF_E_SET_SSID);
	setbit(eventmask, BRCMF_E_ROAM);
	setbit(eventmask, BRCMF_E_PRUNE);
	setbit(eventmask, BRCMF_E_AUTH);
	setbit(eventmask, BRCMF_E_REASSOC);
	setbit(eventmask, BRCMF_E_REASSOC_IND);
	setbit(eventmask, BRCMF_E_DEAUTH_IND);
	setbit(eventmask, BRCMF_E_DISASSOC_IND);
	setbit(eventmask, BRCMF_E_DISASSOC);
	setbit(eventmask, BRCMF_E_JOIN);
	setbit(eventmask, BRCMF_E_ASSOC_IND);
	setbit(eventmask, BRCMF_E_PSK_SUP);
	setbit(eventmask, BRCMF_E_LINK);
	setbit(eventmask, BRCMF_E_NDIS_LINK);
	setbit(eventmask, BRCMF_E_MIC_ERROR);
	setbit(eventmask, BRCMF_E_PMKID_CACHE);
	setbit(eventmask, BRCMF_E_TXFAIL);
	setbit(eventmask, BRCMF_E_JOIN_START);
	setbit(eventmask, BRCMF_E_ESCAN_RESULT);
	setbit(eventmask, BRCMF_E_PFN_NET_FOUND);

	err = brcmf_fil_iovar_data_set(netdev_priv(ndev), "event_msgs",
				       eventmask, BRCMF_EVENTING_MASK_LEN);
	if (err) {
		WL_ERR("Set event_msgs error (%d)\n", err);
		goto dongle_eventmsg_out;
	}

dongle_eventmsg_out:
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_dongle_roam(struct net_device *ndev, u32 roamvar, u32 bcn_timeout)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err = 0;
	__le32 roamtrigger[2];
	__le32 roam_delta[2];

	/*
	 * Setup timeout if Beacons are lost and roam is
	 * off to report link down
	 */
	if (roamvar) {
		err = brcmf_fil_iovar_int_set(ifp, "bcn_timeout", bcn_timeout);
		if (err) {
			WL_ERR("bcn_timeout error (%d)\n", err);
			goto dongle_rom_out;
		}
	}

	/*
	 * Enable/Disable built-in roaming to allow supplicant
	 * to take care of roaming
	 */
	WL_INFO("Internal Roaming = %s\n", roamvar ? "Off" : "On");
	err = brcmf_fil_iovar_int_set(ifp, "roam_off", roamvar);
	if (err) {
		WL_ERR("roam_off error (%d)\n", err);
		goto dongle_rom_out;
	}

	roamtrigger[0] = cpu_to_le32(WL_ROAM_TRIGGER_LEVEL);
	roamtrigger[1] = cpu_to_le32(BRCM_BAND_ALL);
	err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_ROAM_TRIGGER,
				     (void *)roamtrigger, sizeof(roamtrigger));
	if (err) {
		WL_ERR("WLC_SET_ROAM_TRIGGER error (%d)\n", err);
		goto dongle_rom_out;
	}

	roam_delta[0] = cpu_to_le32(WL_ROAM_DELTA);
	roam_delta[1] = cpu_to_le32(BRCM_BAND_ALL);
	err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_ROAM_DELTA,
				     (void *)roam_delta, sizeof(roam_delta));
	if (err) {
		WL_ERR("WLC_SET_ROAM_DELTA error (%d)\n", err);
		goto dongle_rom_out;
	}

dongle_rom_out:
	return err;
}

static s32
brcmf_dongle_scantime(struct net_device *ndev, s32 scan_assoc_time,
		      s32 scan_unassoc_time, s32 scan_passive_time)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err = 0;

	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_CHANNEL_TIME,
				    scan_assoc_time);
	if (err) {
		if (err == -EOPNOTSUPP)
			WL_INFO("Scan assoc time is not supported\n");
		else
			WL_ERR("Scan assoc time error (%d)\n", err);
		goto dongle_scantime_out;
	}
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_UNASSOC_TIME,
				    scan_unassoc_time);
	if (err) {
		if (err == -EOPNOTSUPP)
			WL_INFO("Scan unassoc time is not supported\n");
		else
			WL_ERR("Scan unassoc time error (%d)\n", err);
		goto dongle_scantime_out;
	}

	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_PASSIVE_TIME,
				    scan_passive_time);
	if (err) {
		if (err == -EOPNOTSUPP)
			WL_INFO("Scan passive time is not supported\n");
		else
			WL_ERR("Scan passive time error (%d)\n", err);
		goto dongle_scantime_out;
	}

dongle_scantime_out:
	return err;
}

static s32 wl_update_wiphybands(struct brcmf_cfg80211_info *cfg)
{
	struct brcmf_if *ifp = netdev_priv(cfg_to_ndev(cfg));
	struct wiphy *wiphy;
	s32 phy_list;
	s8 phy;
	s32 err = 0;

	err = brcmf_fil_cmd_data_get(ifp, BRCM_GET_PHYLIST,
				     &phy_list, sizeof(phy_list));
	if (err) {
		WL_ERR("error (%d)\n", err);
		return err;
	}

	phy = ((char *)&phy_list)[0];
	WL_INFO("%c phy\n", phy);
	if (phy == 'n' || phy == 'a') {
		wiphy = cfg_to_wiphy(cfg);
		wiphy->bands[IEEE80211_BAND_5GHZ] = &__wl_band_5ghz_n;
	}

	return err;
}

static s32 brcmf_dongle_probecap(struct brcmf_cfg80211_info *cfg)
{
	return wl_update_wiphybands(cfg);
}

static s32 brcmf_config_dongle(struct brcmf_cfg80211_info *cfg)
{
	struct net_device *ndev;
	struct wireless_dev *wdev;
	s32 power_mode;
	s32 err = 0;

	if (cfg->dongle_up)
		return err;

	ndev = cfg_to_ndev(cfg);
	wdev = ndev->ieee80211_ptr;

	brcmf_dongle_scantime(ndev, WL_SCAN_CHANNEL_TIME,
			WL_SCAN_UNASSOC_TIME, WL_SCAN_PASSIVE_TIME);

	err = brcmf_dongle_eventmsg(ndev);
	if (err)
		goto default_conf_out;

	power_mode = cfg->pwr_save ? PM_FAST : PM_OFF;
	err = brcmf_fil_cmd_int_set(netdev_priv(ndev), BRCMF_C_SET_PM,
				    power_mode);
	if (err)
		goto default_conf_out;
	WL_INFO("power save set to %s\n",
		(power_mode ? "enabled" : "disabled"));

	err = brcmf_dongle_roam(ndev, (cfg->roam_on ? 0 : 1),
				WL_BEACON_TIMEOUT);
	if (err)
		goto default_conf_out;
	err = brcmf_cfg80211_change_iface(wdev->wiphy, ndev, wdev->iftype,
					  NULL, NULL);
	if (err && err != -EINPROGRESS)
		goto default_conf_out;
	err = brcmf_dongle_probecap(cfg);
	if (err)
		goto default_conf_out;

	/* -EINPROGRESS: Call commit handler */

default_conf_out:

	cfg->dongle_up = true;

	return err;

}

static s32 __brcmf_cfg80211_up(struct brcmf_cfg80211_info *cfg)
{
	struct brcmf_if *ifp = netdev_priv(cfg_to_ndev(cfg));

	set_bit(BRCMF_VIF_STATUS_READY, &ifp->vif->sme_state);

	return brcmf_config_dongle(cfg);
}

static s32 __brcmf_cfg80211_down(struct brcmf_cfg80211_info *cfg)
{
	struct net_device *ndev = cfg_to_ndev(cfg);
	struct brcmf_if *ifp = netdev_priv(ndev);

	/*
	 * While going down, if associated with AP disassociate
	 * from AP to save power
	 */
	if ((test_bit(BRCMF_VIF_STATUS_CONNECTED, &ifp->vif->sme_state) ||
	     test_bit(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state)) &&
	     check_vif_up(ifp->vif)) {
		WL_INFO("Disassociating from AP");
		brcmf_link_down(cfg);

		/* Make sure WPA_Supplicant receives all the event
		   generated due to DISASSOC call to the fw to keep
		   the state fw and WPA_Supplicant state consistent
		 */
		brcmf_delay(500);
	}

	brcmf_abort_scanning(cfg);
	clear_bit(BRCMF_VIF_STATUS_READY, &ifp->vif->sme_state);

	return 0;
}

s32 brcmf_cfg80211_up(struct brcmf_cfg80211_info *cfg)
{
	s32 err = 0;

	mutex_lock(&cfg->usr_sync);
	err = __brcmf_cfg80211_up(cfg);
	mutex_unlock(&cfg->usr_sync);

	return err;
}

s32 brcmf_cfg80211_down(struct brcmf_cfg80211_info *cfg)
{
	s32 err = 0;

	mutex_lock(&cfg->usr_sync);
	err = __brcmf_cfg80211_down(cfg);
	mutex_unlock(&cfg->usr_sync);

	return err;
}

