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

#include <linux/kernel.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <net/cfg80211.h>
#include <net/netlink.h>

#include <brcmu_utils.h>
#include <defs.h>
#include <brcmu_wifi.h>
#include "core.h"
#include "debug.h"
#include "tracepoint.h"
#include "fwil_types.h"
#include "p2p.h"
#include "btcoex.h"
#include "cfg80211.h"
#include "feature.h"
#include "fwil.h"
#include "proto.h"
#include "vendor.h"
#include "bus.h"
#include "common.h"

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

#define WPA_OUI				"\x00\x50\xF2"	/* WPA OUI */
#define WPA_OUI_TYPE			1
#define RSN_OUI				"\x00\x0F\xAC"	/* RSN OUI */
#define	WME_OUI_TYPE			2
#define WPS_OUI_TYPE			4

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
#define VNDR_IE_PARSE_LIMIT		5

#define	DOT11_MGMT_HDR_LEN		24	/* d11 management header len */
#define	DOT11_BCN_PRB_FIXED_LEN		12	/* beacon/probe fixed length */

#define BRCMF_SCAN_JOIN_ACTIVE_DWELL_TIME_MS	320
#define BRCMF_SCAN_JOIN_PASSIVE_DWELL_TIME_MS	400
#define BRCMF_SCAN_JOIN_PROBE_INTERVAL_MS	20

#define BRCMF_SCAN_CHANNEL_TIME		40
#define BRCMF_SCAN_UNASSOC_TIME		40
#define BRCMF_SCAN_PASSIVE_TIME		120

#define BRCMF_ND_INFO_TIMEOUT		msecs_to_jiffies(2000)

#define BRCMF_ASSOC_PARAMS_FIXED_SIZE \
	(sizeof(struct brcmf_assoc_params_le) - sizeof(u16))

static bool check_vif_up(struct brcmf_cfg80211_vif *vif)
{
	if (!test_bit(BRCMF_VIF_STATUS_READY, &vif->sme_state)) {
		brcmf_dbg(INFO, "device is not ready : status (%lu)\n",
			  vif->sme_state);
		return false;
	}
	return true;
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

#define wl_g_rates		(__wl_rates + 0)
#define wl_g_rates_size		ARRAY_SIZE(__wl_rates)
#define wl_a_rates		(__wl_rates + 4)
#define wl_a_rates_size		(wl_g_rates_size - 4)

#define CHAN2G(_channel, _freq) {				\
	.band			= IEEE80211_BAND_2GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= (_channel),			\
	.flags			= IEEE80211_CHAN_DISABLED,	\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define CHAN5G(_channel) {					\
	.band			= IEEE80211_BAND_5GHZ,		\
	.center_freq		= 5000 + (5 * (_channel)),	\
	.hw_value		= (_channel),			\
	.flags			= IEEE80211_CHAN_DISABLED,	\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

static struct ieee80211_channel __wl_2ghz_channels[] = {
	CHAN2G(1, 2412), CHAN2G(2, 2417), CHAN2G(3, 2422), CHAN2G(4, 2427),
	CHAN2G(5, 2432), CHAN2G(6, 2437), CHAN2G(7, 2442), CHAN2G(8, 2447),
	CHAN2G(9, 2452), CHAN2G(10, 2457), CHAN2G(11, 2462), CHAN2G(12, 2467),
	CHAN2G(13, 2472), CHAN2G(14, 2484)
};

static struct ieee80211_channel __wl_5ghz_channels[] = {
	CHAN5G(34), CHAN5G(36), CHAN5G(38), CHAN5G(40), CHAN5G(42),
	CHAN5G(44), CHAN5G(46), CHAN5G(48), CHAN5G(52), CHAN5G(56),
	CHAN5G(60), CHAN5G(64), CHAN5G(100), CHAN5G(104), CHAN5G(108),
	CHAN5G(112), CHAN5G(116), CHAN5G(120), CHAN5G(124), CHAN5G(128),
	CHAN5G(132), CHAN5G(136), CHAN5G(140), CHAN5G(144), CHAN5G(149),
	CHAN5G(153), CHAN5G(157), CHAN5G(161), CHAN5G(165)
};

/* Band templates duplicated per wiphy. The channel info
 * above is added to the band during setup.
 */
static const struct ieee80211_supported_band __wl_band_2ghz = {
	.band = IEEE80211_BAND_2GHZ,
	.bitrates = wl_g_rates,
	.n_bitrates = wl_g_rates_size,
};

static const struct ieee80211_supported_band __wl_band_5ghz = {
	.band = IEEE80211_BAND_5GHZ,
	.bitrates = wl_a_rates,
	.n_bitrates = wl_a_rates_size,
};

/* This is to override regulatory domains defined in cfg80211 module (reg.c)
 * By default world regulatory domain defined in reg.c puts the flags
 * NL80211_RRF_NO_IR for 5GHz channels (for * 36..48 and 149..165).
 * With respect to these flags, wpa_supplicant doesn't * start p2p
 * operations on 5GHz channels. All the changes in world regulatory
 * domain are to be done here.
 */
static const struct ieee80211_regdomain brcmf_regdom = {
	.n_reg_rules = 4,
	.alpha2 =  "99",
	.reg_rules = {
		/* IEEE 802.11b/g, channels 1..11 */
		REG_RULE(2412-10, 2472+10, 40, 6, 20, 0),
		/* If any */
		/* IEEE 802.11 channel 14 - Only JP enables
		 * this and for 802.11b only
		 */
		REG_RULE(2484-10, 2484+10, 20, 6, 20, 0),
		/* IEEE 802.11a, channel 36..64 */
		REG_RULE(5150-10, 5350+10, 80, 6, 20, 0),
		/* IEEE 802.11a, channel 100..165 */
		REG_RULE(5470-10, 5850+10, 80, 6, 20, 0), }
};

static const u32 __wl_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
	WLAN_CIPHER_SUITE_AES_CMAC,
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
	struct parsed_vndr_ie_info ie_info[VNDR_IE_PARSE_LIMIT];
};

static u16 chandef_to_chanspec(struct brcmu_d11inf *d11inf,
			       struct cfg80211_chan_def *ch)
{
	struct brcmu_chan ch_inf;
	s32 primary_offset;

	brcmf_dbg(TRACE, "chandef: control %d center %d width %d\n",
		  ch->chan->center_freq, ch->center_freq1, ch->width);
	ch_inf.chnum = ieee80211_frequency_to_channel(ch->center_freq1);
	primary_offset = ch->chan->center_freq - ch->center_freq1;
	switch (ch->width) {
	case NL80211_CHAN_WIDTH_20:
	case NL80211_CHAN_WIDTH_20_NOHT:
		ch_inf.bw = BRCMU_CHAN_BW_20;
		WARN_ON(primary_offset != 0);
		break;
	case NL80211_CHAN_WIDTH_40:
		ch_inf.bw = BRCMU_CHAN_BW_40;
		if (primary_offset > 0)
			ch_inf.sb = BRCMU_CHAN_SB_U;
		else
			ch_inf.sb = BRCMU_CHAN_SB_L;
		break;
	case NL80211_CHAN_WIDTH_80:
		ch_inf.bw = BRCMU_CHAN_BW_80;
		if (primary_offset == -30)
			ch_inf.sb = BRCMU_CHAN_SB_LL;
		else if (primary_offset == -10)
			ch_inf.sb = BRCMU_CHAN_SB_LU;
		else if (primary_offset == 10)
			ch_inf.sb = BRCMU_CHAN_SB_UL;
		else
			ch_inf.sb = BRCMU_CHAN_SB_UU;
		break;
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_160:
	case NL80211_CHAN_WIDTH_5:
	case NL80211_CHAN_WIDTH_10:
	default:
		WARN_ON_ONCE(1);
	}
	switch (ch->chan->band) {
	case IEEE80211_BAND_2GHZ:
		ch_inf.band = BRCMU_CHAN_BAND_2G;
		break;
	case IEEE80211_BAND_5GHZ:
		ch_inf.band = BRCMU_CHAN_BAND_5G;
		break;
	case IEEE80211_BAND_60GHZ:
	default:
		WARN_ON_ONCE(1);
	}
	d11inf->encchspec(&ch_inf);

	return ch_inf.chspec;
}

u16 channel_to_chanspec(struct brcmu_d11inf *d11inf,
			struct ieee80211_channel *ch)
{
	struct brcmu_chan ch_inf;

	ch_inf.chnum = ieee80211_frequency_to_channel(ch->center_freq);
	ch_inf.bw = BRCMU_CHAN_BW_20;
	d11inf->encchspec(&ch_inf);

	return ch_inf.chspec;
}

/* Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag
 */
const struct brcmf_tlv *
brcmf_parse_tlvs(const void *buf, int buflen, uint key)
{
	const struct brcmf_tlv *elt = buf;
	int totlen = buflen;

	/* find tagged parameter */
	while (totlen >= TLV_HDR_LEN) {
		int len = elt->len;

		/* validate remaining totlen */
		if ((elt->id == key) && (totlen >= (len + TLV_HDR_LEN)))
			return elt;

		elt = (struct brcmf_tlv *)((u8 *)elt + (len + TLV_HDR_LEN));
		totlen -= (len + TLV_HDR_LEN);
	}

	return NULL;
}

/* Is any of the tlvs the expected entry? If
 * not update the tlvs buffer pointer/length.
 */
static bool
brcmf_tlv_has_ie(const u8 *ie, const u8 **tlvs, u32 *tlvs_len,
		 const u8 *oui, u32 oui_len, u8 type)
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
brcmf_find_wpaie(const u8 *parse, u32 len)
{
	const struct brcmf_tlv *ie;

	while ((ie = brcmf_parse_tlvs(parse, len, WLAN_EID_VENDOR_SPECIFIC))) {
		if (brcmf_tlv_has_ie((const u8 *)ie, &parse, &len,
				     WPA_OUI, TLV_OUI_LEN, WPA_OUI_TYPE))
			return (struct brcmf_vs_tlv *)ie;
	}
	return NULL;
}

static struct brcmf_vs_tlv *
brcmf_find_wpsie(const u8 *parse, u32 len)
{
	const struct brcmf_tlv *ie;

	while ((ie = brcmf_parse_tlvs(parse, len, WLAN_EID_VENDOR_SPECIFIC))) {
		if (brcmf_tlv_has_ie((u8 *)ie, &parse, &len,
				     WPA_OUI, TLV_OUI_LEN, WPS_OUI_TYPE))
			return (struct brcmf_vs_tlv *)ie;
	}
	return NULL;
}

static int brcmf_vif_change_validate(struct brcmf_cfg80211_info *cfg,
				     struct brcmf_cfg80211_vif *vif,
				     enum nl80211_iftype new_type)
{
	int iftype_num[NUM_NL80211_IFTYPES];
	struct brcmf_cfg80211_vif *pos;
	bool check_combos = false;
	int ret = 0;

	memset(&iftype_num[0], 0, sizeof(iftype_num));
	list_for_each_entry(pos, &cfg->vif_list, list)
		if (pos == vif) {
			iftype_num[new_type]++;
		} else {
			/* concurrent interfaces so need check combinations */
			check_combos = true;
			iftype_num[pos->wdev.iftype]++;
		}

	if (check_combos)
		ret = cfg80211_check_combinations(cfg->wiphy, 1, 0, iftype_num);

	return ret;
}

static int brcmf_vif_add_validate(struct brcmf_cfg80211_info *cfg,
				  enum nl80211_iftype new_type)
{
	int iftype_num[NUM_NL80211_IFTYPES];
	struct brcmf_cfg80211_vif *pos;

	memset(&iftype_num[0], 0, sizeof(iftype_num));
	list_for_each_entry(pos, &cfg->vif_list, list)
		iftype_num[pos->wdev.iftype]++;

	iftype_num[new_type]++;
	return cfg80211_check_combinations(cfg->wiphy, 1, 0, iftype_num);
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
send_key_to_dongle(struct brcmf_if *ifp, struct brcmf_wsec_key *key)
{
	int err;
	struct brcmf_wsec_key_le key_le;

	convert_key_from_CPU(key, &key_le);

	brcmf_netdev_wait_pend8021x(ifp);

	err = brcmf_fil_bsscfg_data_set(ifp, "wsec_key", &key_le,
					sizeof(key_le));

	if (err)
		brcmf_err("wsec_key error (%d)\n", err);
	return err;
}

static s32
brcmf_configure_arp_offload(struct brcmf_if *ifp, bool enable)
{
	s32 err;
	u32 mode;

	if (enable)
		mode = BRCMF_ARP_OL_AGENT | BRCMF_ARP_OL_PEER_AUTO_REPLY;
	else
		mode = 0;

	/* Try to set and enable ARP offload feature, this may fail, then it  */
	/* is simply not supported and err 0 will be returned                 */
	err = brcmf_fil_iovar_int_set(ifp, "arp_ol", mode);
	if (err) {
		brcmf_dbg(TRACE, "failed to set ARP offload mode to 0x%x, err = %d\n",
			  mode, err);
		err = 0;
	} else {
		err = brcmf_fil_iovar_int_set(ifp, "arpoe", enable);
		if (err) {
			brcmf_dbg(TRACE, "failed to configure (%d) ARP offload err = %d\n",
				  enable, err);
			err = 0;
		} else
			brcmf_dbg(TRACE, "successfully configured (%d) ARP offload to 0x%x\n",
				  enable, mode);
	}

	return err;
}

static void
brcmf_cfg80211_update_proto_addr_mode(struct wireless_dev *wdev)
{
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;

	if ((wdev->iftype == NL80211_IFTYPE_ADHOC) ||
	    (wdev->iftype == NL80211_IFTYPE_AP) ||
	    (wdev->iftype == NL80211_IFTYPE_P2P_GO))
		brcmf_proto_configure_addr_mode(ifp->drvr, ifp->ifidx,
						ADDR_DIRECT);
	else
		brcmf_proto_configure_addr_mode(ifp->drvr, ifp->ifidx,
						ADDR_INDIRECT);
}

static int brcmf_cfg80211_request_ap_if(struct brcmf_if *ifp)
{
	struct brcmf_mbss_ssid_le mbss_ssid_le;
	int bsscfgidx;
	int err;

	memset(&mbss_ssid_le, 0, sizeof(mbss_ssid_le));
	bsscfgidx = brcmf_get_next_free_bsscfgidx(ifp->drvr);
	if (bsscfgidx < 0)
		return bsscfgidx;

	mbss_ssid_le.bsscfgidx = cpu_to_le32(bsscfgidx);
	mbss_ssid_le.SSID_len = cpu_to_le32(5);
	sprintf(mbss_ssid_le.SSID, "ssid%d" , bsscfgidx);

	err = brcmf_fil_bsscfg_data_set(ifp, "bsscfg:ssid", &mbss_ssid_le,
					sizeof(mbss_ssid_le));
	if (err < 0)
		brcmf_err("setting ssid failed %d\n", err);

	return err;
}

/**
 * brcmf_ap_add_vif() - create a new AP virtual interface for multiple BSS
 *
 * @wiphy: wiphy device of new interface.
 * @name: name of the new interface.
 * @flags: not used.
 * @params: contains mac address for AP device.
 */
static
struct wireless_dev *brcmf_ap_add_vif(struct wiphy *wiphy, const char *name,
				      u32 *flags, struct vif_params *params)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(cfg_to_ndev(cfg));
	struct brcmf_cfg80211_vif *vif;
	int err;

	if (brcmf_cfg80211_vif_event_armed(cfg))
		return ERR_PTR(-EBUSY);

	brcmf_dbg(INFO, "Adding vif \"%s\"\n", name);

	vif = brcmf_alloc_vif(cfg, NL80211_IFTYPE_AP, false);
	if (IS_ERR(vif))
		return (struct wireless_dev *)vif;

	brcmf_cfg80211_arm_vif_event(cfg, vif);

	err = brcmf_cfg80211_request_ap_if(ifp);
	if (err) {
		brcmf_cfg80211_arm_vif_event(cfg, NULL);
		goto fail;
	}

	/* wait for firmware event */
	err = brcmf_cfg80211_wait_vif_event(cfg, BRCMF_E_IF_ADD,
					    BRCMF_VIF_EVENT_TIMEOUT);
	brcmf_cfg80211_arm_vif_event(cfg, NULL);
	if (!err) {
		brcmf_err("timeout occurred\n");
		err = -EIO;
		goto fail;
	}

	/* interface created in firmware */
	ifp = vif->ifp;
	if (!ifp) {
		brcmf_err("no if pointer provided\n");
		err = -ENOENT;
		goto fail;
	}

	strncpy(ifp->ndev->name, name, sizeof(ifp->ndev->name) - 1);
	err = brcmf_net_attach(ifp, true);
	if (err) {
		brcmf_err("Registering netdevice failed\n");
		goto fail;
	}

	return &ifp->vif->wdev;

fail:
	brcmf_free_vif(vif);
	return ERR_PTR(err);
}

static bool brcmf_is_apmode(struct brcmf_cfg80211_vif *vif)
{
	enum nl80211_iftype iftype;

	iftype = vif->wdev.iftype;
	return iftype == NL80211_IFTYPE_AP || iftype == NL80211_IFTYPE_P2P_GO;
}

static bool brcmf_is_ibssmode(struct brcmf_cfg80211_vif *vif)
{
	return vif->wdev.iftype == NL80211_IFTYPE_ADHOC;
}

static struct wireless_dev *brcmf_cfg80211_add_iface(struct wiphy *wiphy,
						     const char *name,
						     unsigned char name_assign_type,
						     enum nl80211_iftype type,
						     u32 *flags,
						     struct vif_params *params)
{
	struct wireless_dev *wdev;
	int err;

	brcmf_dbg(TRACE, "enter: %s type %d\n", name, type);
	err = brcmf_vif_add_validate(wiphy_to_cfg(wiphy), type);
	if (err) {
		brcmf_err("iface validation failed: err=%d\n", err);
		return ERR_PTR(err);
	}
	switch (type) {
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_MESH_POINT:
		return ERR_PTR(-EOPNOTSUPP);
	case NL80211_IFTYPE_AP:
		wdev = brcmf_ap_add_vif(wiphy, name, flags, params);
		if (!IS_ERR(wdev))
			brcmf_cfg80211_update_proto_addr_mode(wdev);
		return wdev;
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_P2P_DEVICE:
		wdev = brcmf_p2p_add_vif(wiphy, name, name_assign_type, type, flags, params);
		if (!IS_ERR(wdev))
			brcmf_cfg80211_update_proto_addr_mode(wdev);
		return wdev;
	case NL80211_IFTYPE_UNSPECIFIED:
	default:
		return ERR_PTR(-EINVAL);
	}
}

static void brcmf_scan_config_mpc(struct brcmf_if *ifp, int mpc)
{
	if (brcmf_feat_is_quirk_enabled(ifp, BRCMF_FEAT_QUIRK_NEED_MPC))
		brcmf_set_mpc(ifp, mpc);
}

void brcmf_set_mpc(struct brcmf_if *ifp, int mpc)
{
	s32 err = 0;

	if (check_vif_up(ifp->vif)) {
		err = brcmf_fil_iovar_int_set(ifp, "mpc", mpc);
		if (err) {
			brcmf_err("fail to set mpc\n");
			return;
		}
		brcmf_dbg(INFO, "MPC : %d\n", mpc);
	}
}

s32 brcmf_notify_escan_complete(struct brcmf_cfg80211_info *cfg,
				struct brcmf_if *ifp, bool aborted,
				bool fw_abort)
{
	struct brcmf_scan_params_le params_le;
	struct cfg80211_scan_request *scan_request;
	s32 err = 0;

	brcmf_dbg(SCAN, "Enter\n");

	/* clear scan request, because the FW abort can cause a second call */
	/* to this functon and might cause a double cfg80211_scan_done      */
	scan_request = cfg->scan_request;
	cfg->scan_request = NULL;

	if (timer_pending(&cfg->escan_timeout))
		del_timer_sync(&cfg->escan_timeout);

	if (fw_abort) {
		/* Do a scan abort to stop the driver's scan engine */
		brcmf_dbg(SCAN, "ABORT scan in firmware\n");
		memset(&params_le, 0, sizeof(params_le));
		eth_broadcast_addr(params_le.bssid);
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
		err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SCAN,
					     &params_le, sizeof(params_le));
		if (err)
			brcmf_err("Scan abort  failed\n");
	}

	brcmf_scan_config_mpc(ifp, 1);

	/*
	 * e-scan can be initiated by scheduled scan
	 * which takes precedence.
	 */
	if (cfg->sched_escan) {
		brcmf_dbg(SCAN, "scheduled scan completed\n");
		cfg->sched_escan = false;
		if (!aborted)
			cfg80211_sched_scan_results(cfg_to_wiphy(cfg));
	} else if (scan_request) {
		brcmf_dbg(SCAN, "ESCAN Completed scan: %s\n",
			  aborted ? "Aborted" : "Done");
		cfg80211_scan_done(scan_request, aborted);
	}
	if (!test_and_clear_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status))
		brcmf_dbg(SCAN, "Scan complete, probably P2P scan\n");

	return err;
}

static
int brcmf_cfg80211_del_iface(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct brcmf_cfg80211_info *cfg = wiphy_priv(wiphy);
	struct net_device *ndev = wdev->netdev;

	/* vif event pending in firmware */
	if (brcmf_cfg80211_vif_event_armed(cfg))
		return -EBUSY;

	if (ndev) {
		if (test_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status) &&
		    cfg->escan_info.ifp == netdev_priv(ndev))
			brcmf_notify_escan_complete(cfg, netdev_priv(ndev),
						    true, true);

		brcmf_fil_iovar_int_set(netdev_priv(ndev), "mpc", 1);
	}

	switch (wdev->iftype) {
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_MESH_POINT:
		return -EOPNOTSUPP;
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_P2P_DEVICE:
		return brcmf_p2p_del_vif(wiphy, wdev);
	case NL80211_IFTYPE_UNSPECIFIED:
	default:
		return -EINVAL;
	}
	return -EOPNOTSUPP;
}

static s32
brcmf_cfg80211_change_iface(struct wiphy *wiphy, struct net_device *ndev,
			 enum nl80211_iftype type, u32 *flags,
			 struct vif_params *params)
{
	struct brcmf_cfg80211_info *cfg = wiphy_priv(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_vif *vif = ifp->vif;
	s32 infra = 0;
	s32 ap = 0;
	s32 err = 0;

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d, type=%d\n", ifp->bsscfgidx,
		  type);

	/* WAR: There are a number of p2p interface related problems which
	 * need to be handled initially (before doing the validate).
	 * wpa_supplicant tends to do iface changes on p2p device/client/go
	 * which are not always possible/allowed. However we need to return
	 * OK otherwise the wpa_supplicant wont start. The situation differs
	 * on configuration and setup (p2pon=1 module param). The first check
	 * is to see if the request is a change to station for p2p iface.
	 */
	if ((type == NL80211_IFTYPE_STATION) &&
	    ((vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT) ||
	     (vif->wdev.iftype == NL80211_IFTYPE_P2P_GO) ||
	     (vif->wdev.iftype == NL80211_IFTYPE_P2P_DEVICE))) {
		brcmf_dbg(TRACE, "Ignoring cmd for p2p if\n");
		/* Now depending on whether module param p2pon=1 was used the
		 * response needs to be either 0 or EOPNOTSUPP. The reason is
		 * that if p2pon=1 is used, but a newer supplicant is used then
		 * we should return an error, as this combination wont work.
		 * In other situations 0 is returned and supplicant will start
		 * normally. It will give a trace in cfg80211, but it is the
		 * only way to get it working. Unfortunately this will result
		 * in situation where we wont support new supplicant in
		 * combination with module param p2pon=1, but that is the way
		 * it is. If the user tries this then unloading of driver might
		 * fail/lock.
		 */
		if (cfg->p2p.p2pdev_dynamically)
			return -EOPNOTSUPP;
		else
			return 0;
	}
	err = brcmf_vif_change_validate(wiphy_to_cfg(wiphy), vif, type);
	if (err) {
		brcmf_err("iface validation failed: err=%d\n", err);
		return err;
	}
	switch (type) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_WDS:
		brcmf_err("type (%d) : currently we do not support this type\n",
			  type);
		return -EOPNOTSUPP;
	case NL80211_IFTYPE_ADHOC:
		infra = 0;
		break;
	case NL80211_IFTYPE_STATION:
		infra = 1;
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		ap = 1;
		break;
	default:
		err = -EINVAL;
		goto done;
	}

	if (ap) {
		if (type == NL80211_IFTYPE_P2P_GO) {
			brcmf_dbg(INFO, "IF Type = P2P GO\n");
			err = brcmf_p2p_ifchange(cfg, BRCMF_FIL_P2P_IF_GO);
		}
		if (!err) {
			brcmf_dbg(INFO, "IF Type = AP\n");
		}
	} else {
		err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_INFRA, infra);
		if (err) {
			brcmf_err("WLC_SET_INFRA error (%d)\n", err);
			err = -EAGAIN;
			goto done;
		}
		brcmf_dbg(INFO, "IF Type = %s\n", brcmf_is_ibssmode(vif) ?
			  "Adhoc" : "Infra");
	}
	ndev->ieee80211_ptr->iftype = type;

	brcmf_cfg80211_update_proto_addr_mode(&vif->wdev);

done:
	brcmf_dbg(TRACE, "Exit\n");

	return err;
}

static void brcmf_escan_prep(struct brcmf_cfg80211_info *cfg,
			     struct brcmf_scan_params_le *params_le,
			     struct cfg80211_scan_request *request)
{
	u32 n_ssids;
	u32 n_channels;
	s32 i;
	s32 offset;
	u16 chanspec;
	char *ptr;
	struct brcmf_ssid_le ssid_le;

	eth_broadcast_addr(params_le->bssid);
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
	brcmf_dbg(SCAN, "### List of channelspecs to scan ### %d\n",
		  n_channels);
	if (n_channels > 0) {
		for (i = 0; i < n_channels; i++) {
			chanspec = channel_to_chanspec(&cfg->d11inf,
						       request->channels[i]);
			brcmf_dbg(SCAN, "Chan : %d, Channel spec: %x\n",
				  request->channels[i]->hw_value, chanspec);
			params_le->channel_list[i] = cpu_to_le16(chanspec);
		}
	} else {
		brcmf_dbg(SCAN, "Scanning all channels\n");
	}
	/* Copy ssid array if applicable */
	brcmf_dbg(SCAN, "### List of SSIDs to scan ### %d\n", n_ssids);
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
				brcmf_dbg(SCAN, "%d: Broadcast scan\n", i);
			else
				brcmf_dbg(SCAN, "%d: scan for  %s size =%d\n",
					  i, ssid_le.SSID, ssid_le.SSID_len);
			memcpy(ptr, &ssid_le, sizeof(ssid_le));
			ptr += sizeof(ssid_le);
		}
	} else {
		brcmf_dbg(SCAN, "Broadcast scan %p\n", request->ssids);
		if ((request->ssids) && request->ssids->ssid_len) {
			brcmf_dbg(SCAN, "SSID %s len=%d\n",
				  params_le->ssid_le.SSID,
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
brcmf_run_escan(struct brcmf_cfg80211_info *cfg, struct brcmf_if *ifp,
		struct cfg80211_scan_request *request)
{
	s32 params_size = BRCMF_SCAN_PARAMS_FIXED_SIZE +
			  offsetof(struct brcmf_escan_params_le, params_le);
	struct brcmf_escan_params_le *params;
	s32 err = 0;

	brcmf_dbg(SCAN, "E-SCAN START\n");

	if (request != NULL) {
		/* Allocate space for populating ssids in struct */
		params_size += sizeof(u32) * ((request->n_channels + 1) / 2);

		/* Allocate space for populating ssids in struct */
		params_size += sizeof(struct brcmf_ssid_le) * request->n_ssids;
	}

	params = kzalloc(params_size, GFP_KERNEL);
	if (!params) {
		err = -ENOMEM;
		goto exit;
	}
	BUG_ON(params_size + sizeof("escan") >= BRCMF_DCMD_MEDLEN);
	brcmf_escan_prep(cfg, &params->params_le, request);
	params->version = cpu_to_le32(BRCMF_ESCAN_REQ_VERSION);
	params->action = cpu_to_le16(WL_ESCAN_ACTION_START);
	params->sync_id = cpu_to_le16(0x1234);

	err = brcmf_fil_iovar_data_set(ifp, "escan", params, params_size);
	if (err) {
		if (err == -EBUSY)
			brcmf_dbg(INFO, "system busy : escan canceled\n");
		else
			brcmf_err("error (%d)\n", err);
	}

	kfree(params);
exit:
	return err;
}

static s32
brcmf_do_escan(struct brcmf_cfg80211_info *cfg, struct wiphy *wiphy,
	       struct brcmf_if *ifp, struct cfg80211_scan_request *request)
{
	s32 err;
	u32 passive_scan;
	struct brcmf_scan_results *results;
	struct escan_info *escan = &cfg->escan_info;

	brcmf_dbg(SCAN, "Enter\n");
	escan->ifp = ifp;
	escan->wiphy = wiphy;
	escan->escan_state = WL_ESCAN_STATE_SCANNING;
	passive_scan = cfg->active_scan ? 0 : 1;
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_PASSIVE_SCAN,
				    passive_scan);
	if (err) {
		brcmf_err("error (%d)\n", err);
		return err;
	}
	brcmf_scan_config_mpc(ifp, 0);
	results = (struct brcmf_scan_results *)cfg->escan_info.escan_buf;
	results->version = 0;
	results->count = 0;
	results->buflen = WL_ESCAN_RESULTS_FIXED_SIZE;

	err = escan->run(cfg, ifp, request);
	if (err)
		brcmf_scan_config_mpc(ifp, 1);
	return err;
}

static s32
brcmf_cfg80211_escan(struct wiphy *wiphy, struct brcmf_cfg80211_vif *vif,
		     struct cfg80211_scan_request *request,
		     struct cfg80211_ssid *this_ssid)
{
	struct brcmf_if *ifp = vif->ifp;
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct cfg80211_ssid *ssids;
	u32 passive_scan;
	bool escan_req;
	bool spec_scan;
	s32 err;
	struct brcmf_ssid_le ssid_le;
	u32 SSID_len;

	brcmf_dbg(SCAN, "START ESCAN\n");

	if (test_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status)) {
		brcmf_err("Scanning already: status (%lu)\n", cfg->scan_status);
		return -EAGAIN;
	}
	if (test_bit(BRCMF_SCAN_STATUS_ABORT, &cfg->scan_status)) {
		brcmf_err("Scanning being aborted: status (%lu)\n",
			  cfg->scan_status);
		return -EAGAIN;
	}
	if (test_bit(BRCMF_SCAN_STATUS_SUPPRESS, &cfg->scan_status)) {
		brcmf_err("Scanning suppressed: status (%lu)\n",
			  cfg->scan_status);
		return -EAGAIN;
	}
	if (test_bit(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state)) {
		brcmf_err("Connecting: status (%lu)\n", ifp->vif->sme_state);
		return -EAGAIN;
	}

	/* If scan req comes for p2p0, send it over primary I/F */
	if (vif == cfg->p2p.bss_idx[P2PAPI_BSSCFG_DEVICE].vif)
		vif = cfg->p2p.bss_idx[P2PAPI_BSSCFG_PRIMARY].vif;

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
		cfg->escan_info.run = brcmf_run_escan;
		err = brcmf_p2p_scan_prep(wiphy, request, vif);
		if (err)
			goto scan_out;

		err = brcmf_do_escan(cfg, wiphy, vif->ifp, request);
		if (err)
			goto scan_out;
	} else {
		brcmf_dbg(SCAN, "ssid \"%s\", ssid_len (%d)\n",
			  ssids->ssid, ssids->ssid_len);
		memset(&ssid_le, 0, sizeof(ssid_le));
		SSID_len = min_t(u8, sizeof(ssid_le.SSID), ssids->ssid_len);
		ssid_le.SSID_len = cpu_to_le32(0);
		spec_scan = false;
		if (SSID_len) {
			memcpy(ssid_le.SSID, ssids->ssid, SSID_len);
			ssid_le.SSID_len = cpu_to_le32(SSID_len);
			spec_scan = true;
		} else
			brcmf_dbg(SCAN, "Broadcast scan\n");

		passive_scan = cfg->active_scan ? 0 : 1;
		err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_PASSIVE_SCAN,
					    passive_scan);
		if (err) {
			brcmf_err("WLC_SET_PASSIVE_SCAN error (%d)\n", err);
			goto scan_out;
		}
		brcmf_scan_config_mpc(ifp, 0);
		err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SCAN, &ssid_le,
					     sizeof(ssid_le));
		if (err) {
			if (err == -EBUSY)
				brcmf_dbg(INFO, "BUSY: scan for \"%s\" canceled\n",
					  ssid_le.SSID);
			else
				brcmf_err("WLC_SCAN error (%d)\n", err);

			brcmf_scan_config_mpc(ifp, 1);
			goto scan_out;
		}
	}

	/* Arm scan timeout timer */
	mod_timer(&cfg->escan_timeout, jiffies +
			BRCMF_ESCAN_TIMER_INTERVAL_MS * HZ / 1000);

	return 0;

scan_out:
	clear_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status);
	cfg->scan_request = NULL;
	return err;
}

static s32
brcmf_cfg80211_scan(struct wiphy *wiphy, struct cfg80211_scan_request *request)
{
	struct brcmf_cfg80211_vif *vif;
	s32 err = 0;

	brcmf_dbg(TRACE, "Enter\n");
	vif = container_of(request->wdev, struct brcmf_cfg80211_vif, wdev);
	if (!check_vif_up(vif))
		return -EIO;

	err = brcmf_cfg80211_escan(wiphy, vif, request, NULL);

	if (err)
		brcmf_err("scan error (%d)\n", err);

	brcmf_dbg(TRACE, "Exit\n");
	return err;
}

static s32 brcmf_set_rts(struct net_device *ndev, u32 rts_threshold)
{
	s32 err = 0;

	err = brcmf_fil_iovar_int_set(netdev_priv(ndev), "rtsthresh",
				      rts_threshold);
	if (err)
		brcmf_err("Error (%d)\n", err);

	return err;
}

static s32 brcmf_set_frag(struct net_device *ndev, u32 frag_threshold)
{
	s32 err = 0;

	err = brcmf_fil_iovar_int_set(netdev_priv(ndev), "fragthresh",
				      frag_threshold);
	if (err)
		brcmf_err("Error (%d)\n", err);

	return err;
}

static s32 brcmf_set_retry(struct net_device *ndev, u32 retry, bool l)
{
	s32 err = 0;
	u32 cmd = (l ? BRCMF_C_SET_LRL : BRCMF_C_SET_SRL);

	err = brcmf_fil_cmd_int_set(netdev_priv(ndev), cmd, retry);
	if (err) {
		brcmf_err("cmd (%d) , error (%d)\n", cmd, err);
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

	brcmf_dbg(TRACE, "Enter\n");
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
	brcmf_dbg(TRACE, "Exit\n");
	return err;
}

static void brcmf_init_prof(struct brcmf_cfg80211_profile *prof)
{
	memset(prof, 0, sizeof(*prof));
}

static u16 brcmf_map_fw_linkdown_reason(const struct brcmf_event_msg *e)
{
	u16 reason;

	switch (e->event_code) {
	case BRCMF_E_DEAUTH:
	case BRCMF_E_DEAUTH_IND:
	case BRCMF_E_DISASSOC_IND:
		reason = e->reason;
		break;
	case BRCMF_E_LINK:
	default:
		reason = 0;
		break;
	}
	return reason;
}

static void brcmf_link_down(struct brcmf_cfg80211_vif *vif, u16 reason)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(vif->wdev.wiphy);
	s32 err = 0;

	brcmf_dbg(TRACE, "Enter\n");

	if (test_and_clear_bit(BRCMF_VIF_STATUS_CONNECTED, &vif->sme_state)) {
		brcmf_dbg(INFO, "Call WLC_DISASSOC to stop excess roaming\n ");
		err = brcmf_fil_cmd_data_set(vif->ifp,
					     BRCMF_C_DISASSOC, NULL, 0);
		if (err) {
			brcmf_err("WLC_DISASSOC failed (%d)\n", err);
		}
		if ((vif->wdev.iftype == NL80211_IFTYPE_STATION) ||
		    (vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT))
			cfg80211_disconnected(vif->wdev.netdev, reason, NULL, 0,
					      true, GFP_KERNEL);
	}
	clear_bit(BRCMF_VIF_STATUS_CONNECTING, &vif->sme_state);
	clear_bit(BRCMF_SCAN_STATUS_SUPPRESS, &cfg->scan_status);
	brcmf_btcoex_set_mode(vif, BRCMF_BTCOEX_ENABLED, 0);
	brcmf_dbg(TRACE, "Exit\n");
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
	u16 chanspec;
	u32 ssid_len;

	brcmf_dbg(TRACE, "Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	if (params->ssid)
		brcmf_dbg(CONN, "SSID: %s\n", params->ssid);
	else {
		brcmf_dbg(CONN, "SSID: NULL, Not supported\n");
		return -EOPNOTSUPP;
	}

	set_bit(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state);

	if (params->bssid)
		brcmf_dbg(CONN, "BSSID: %pM\n", params->bssid);
	else
		brcmf_dbg(CONN, "No BSSID specified\n");

	if (params->chandef.chan)
		brcmf_dbg(CONN, "channel: %d\n",
			  params->chandef.chan->center_freq);
	else
		brcmf_dbg(CONN, "no channel specified\n");

	if (params->channel_fixed)
		brcmf_dbg(CONN, "fixed channel required\n");
	else
		brcmf_dbg(CONN, "no fixed channel required\n");

	if (params->ie && params->ie_len)
		brcmf_dbg(CONN, "ie len: %d\n", params->ie_len);
	else
		brcmf_dbg(CONN, "no ie specified\n");

	if (params->beacon_interval)
		brcmf_dbg(CONN, "beacon interval: %d\n",
			  params->beacon_interval);
	else
		brcmf_dbg(CONN, "no beacon interval specified\n");

	if (params->basic_rates)
		brcmf_dbg(CONN, "basic rates: %08X\n", params->basic_rates);
	else
		brcmf_dbg(CONN, "no basic rates specified\n");

	if (params->privacy)
		brcmf_dbg(CONN, "privacy required\n");
	else
		brcmf_dbg(CONN, "no privacy required\n");

	/* Configure Privacy for starter */
	if (params->privacy)
		wsec |= WEP_ENABLED;

	err = brcmf_fil_iovar_int_set(ifp, "wsec", wsec);
	if (err) {
		brcmf_err("wsec failed (%d)\n", err);
		goto done;
	}

	/* Configure Beacon Interval for starter */
	if (params->beacon_interval)
		bcnprd = params->beacon_interval;
	else
		bcnprd = 100;

	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_BCNPRD, bcnprd);
	if (err) {
		brcmf_err("WLC_SET_BCNPRD failed (%d)\n", err);
		goto done;
	}

	/* Configure required join parameter */
	memset(&join_params, 0, sizeof(struct brcmf_join_params));

	/* SSID */
	ssid_len = min_t(u32, params->ssid_len, IEEE80211_MAX_SSID_LEN);
	memcpy(join_params.ssid_le.SSID, params->ssid, ssid_len);
	join_params.ssid_le.SSID_len = cpu_to_le32(ssid_len);
	join_params_size = sizeof(join_params.ssid_le);

	/* BSSID */
	if (params->bssid) {
		memcpy(join_params.params_le.bssid, params->bssid, ETH_ALEN);
		join_params_size += BRCMF_ASSOC_PARAMS_FIXED_SIZE;
		memcpy(profile->bssid, params->bssid, ETH_ALEN);
	} else {
		eth_broadcast_addr(join_params.params_le.bssid);
		eth_zero_addr(profile->bssid);
	}

	/* Channel */
	if (params->chandef.chan) {
		u32 target_channel;

		cfg->channel =
			ieee80211_frequency_to_channel(
				params->chandef.chan->center_freq);
		if (params->channel_fixed) {
			/* adding chanspec */
			chanspec = chandef_to_chanspec(&cfg->d11inf,
						       &params->chandef);
			join_params.params_le.chanspec_list[0] =
				cpu_to_le16(chanspec);
			join_params.params_le.chanspec_num = cpu_to_le32(1);
			join_params_size += sizeof(join_params.params_le);
		}

		/* set channel for starter */
		target_channel = cfg->channel;
		err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_CHANNEL,
					    target_channel);
		if (err) {
			brcmf_err("WLC_SET_CHANNEL failed (%d)\n", err);
			goto done;
		}
	} else
		cfg->channel = 0;

	cfg->ibss_starter = false;


	err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_SSID,
				     &join_params, join_params_size);
	if (err) {
		brcmf_err("WLC_SET_SSID failed (%d)\n", err);
		goto done;
	}

done:
	if (err)
		clear_bit(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state);
	brcmf_dbg(TRACE, "Exit\n");
	return err;
}

static s32
brcmf_cfg80211_leave_ibss(struct wiphy *wiphy, struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);

	brcmf_dbg(TRACE, "Enter\n");
	if (!check_vif_up(ifp->vif)) {
		/* When driver is being unloaded, it can end up here. If an
		 * error is returned then later on a debug trace in the wireless
		 * core module will be printed. To avoid this 0 is returned.
		 */
		return 0;
	}

	brcmf_link_down(ifp->vif, WLAN_REASON_DEAUTH_LEAVING);
	brcmf_net_setcarrier(ifp, false);

	brcmf_dbg(TRACE, "Exit\n");

	return 0;
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
	brcmf_dbg(CONN, "setting wpa_auth to 0x%0x\n", val);
	err = brcmf_fil_bsscfg_int_set(netdev_priv(ndev), "wpa_auth", val);
	if (err) {
		brcmf_err("set wpa_auth failed (%d)\n", err);
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
		brcmf_dbg(CONN, "open system\n");
		break;
	case NL80211_AUTHTYPE_SHARED_KEY:
		val = 1;
		brcmf_dbg(CONN, "shared key\n");
		break;
	case NL80211_AUTHTYPE_AUTOMATIC:
		val = 2;
		brcmf_dbg(CONN, "automatic\n");
		break;
	case NL80211_AUTHTYPE_NETWORK_EAP:
		brcmf_dbg(CONN, "network eap\n");
	default:
		val = 2;
		brcmf_err("invalid auth type (%d)\n", sme->auth_type);
		break;
	}

	err = brcmf_fil_bsscfg_int_set(netdev_priv(ndev), "auth", val);
	if (err) {
		brcmf_err("set auth failed (%d)\n", err);
		return err;
	}
	sec = &profile->sec;
	sec->auth_type = sme->auth_type;
	return err;
}

static s32
brcmf_set_wsec_mode(struct net_device *ndev,
		     struct cfg80211_connect_params *sme, bool mfp)
{
	struct brcmf_cfg80211_profile *profile = ndev_to_prof(ndev);
	struct brcmf_cfg80211_security *sec;
	s32 pval = 0;
	s32 gval = 0;
	s32 wsec;
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
			brcmf_err("invalid cipher pairwise (%d)\n",
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
			brcmf_err("invalid cipher group (%d)\n",
				  sme->crypto.cipher_group);
			return -EINVAL;
		}
	}

	brcmf_dbg(CONN, "pval (%d) gval (%d)\n", pval, gval);
	/* In case of privacy, but no security and WPS then simulate */
	/* setting AES. WPS-2.0 allows no security                   */
	if (brcmf_find_wpsie(sme->ie, sme->ie_len) && !pval && !gval &&
	    sme->privacy)
		pval = AES_ENABLED;

	if (mfp)
		wsec = pval | gval | MFP_CAPABLE;
	else
		wsec = pval | gval;
	err = brcmf_fil_bsscfg_int_set(netdev_priv(ndev), "wsec", wsec);
	if (err) {
		brcmf_err("error (%d)\n", err);
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
		err = brcmf_fil_bsscfg_int_get(netdev_priv(ndev),
					       "wpa_auth", &val);
		if (err) {
			brcmf_err("could not get wpa_auth (%d)\n", err);
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
				brcmf_err("invalid cipher group (%d)\n",
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
				brcmf_err("invalid cipher group (%d)\n",
					  sme->crypto.cipher_group);
				return -EINVAL;
			}
		}

		brcmf_dbg(CONN, "setting wpa_auth to %d\n", val);
		err = brcmf_fil_bsscfg_int_set(netdev_priv(ndev),
					       "wpa_auth", val);
		if (err) {
			brcmf_err("could not set wpa_auth (%d)\n", err);
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

	brcmf_dbg(CONN, "key len (%d)\n", sme->key_len);

	if (sme->key_len == 0)
		return 0;

	sec = &profile->sec;
	brcmf_dbg(CONN, "wpa_versions 0x%x cipher_pairwise 0x%x\n",
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
		brcmf_err("Too long key length (%u)\n", key.len);
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
		brcmf_err("Invalid algorithm (%d)\n",
			  sme->crypto.ciphers_pairwise[0]);
		return -EINVAL;
	}
	/* Set the new key/index */
	brcmf_dbg(CONN, "key length (%d) key index (%d) algo (%d)\n",
		  key.len, key.index, key.algo);
	brcmf_dbg(CONN, "key \"%s\"\n", key.data);
	err = send_key_to_dongle(netdev_priv(ndev), &key);
	if (err)
		return err;

	if (sec->auth_type == NL80211_AUTHTYPE_SHARED_KEY) {
		brcmf_dbg(CONN, "set auth_type to shared key\n");
		val = WL_AUTH_SHARED_KEY;	/* shared key */
		err = brcmf_fil_bsscfg_int_set(netdev_priv(ndev), "auth", val);
		if (err)
			brcmf_err("set auth failed (%d)\n", err);
	}
	return err;
}

static
enum nl80211_auth_type brcmf_war_auth_type(struct brcmf_if *ifp,
					   enum nl80211_auth_type type)
{
	if (type == NL80211_AUTHTYPE_AUTOMATIC &&
	    brcmf_feat_is_quirk_enabled(ifp, BRCMF_FEAT_QUIRK_AUTO_AUTH)) {
		brcmf_dbg(CONN, "WAR: use OPEN instead of AUTO\n");
		type = NL80211_AUTHTYPE_OPEN_SYSTEM;
	}
	return type;
}

static s32
brcmf_cfg80211_connect(struct wiphy *wiphy, struct net_device *ndev,
		       struct cfg80211_connect_params *sme)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct ieee80211_channel *chan = sme->channel;
	struct brcmf_join_params join_params;
	size_t join_params_size;
	const struct brcmf_tlv *rsn_ie;
	const struct brcmf_vs_tlv *wpa_ie;
	const void *ie;
	u32 ie_len;
	struct brcmf_ext_join_params_le *ext_join_params;
	u16 chanspec;
	s32 err = 0;
	u32 ssid_len;

	brcmf_dbg(TRACE, "Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	if (!sme->ssid) {
		brcmf_err("Invalid ssid\n");
		return -EOPNOTSUPP;
	}

	if (ifp->vif == cfg->p2p.bss_idx[P2PAPI_BSSCFG_PRIMARY].vif) {
		/* A normal (non P2P) connection request setup. */
		ie = NULL;
		ie_len = 0;
		/* find the WPA_IE */
		wpa_ie = brcmf_find_wpaie((u8 *)sme->ie, sme->ie_len);
		if (wpa_ie) {
			ie = wpa_ie;
			ie_len = wpa_ie->len + TLV_HDR_LEN;
		} else {
			/* find the RSN_IE */
			rsn_ie = brcmf_parse_tlvs((const u8 *)sme->ie,
						  sme->ie_len,
						  WLAN_EID_RSN);
			if (rsn_ie) {
				ie = rsn_ie;
				ie_len = rsn_ie->len + TLV_HDR_LEN;
			}
		}
		brcmf_fil_iovar_data_set(ifp, "wpaie", ie, ie_len);
	}

	err = brcmf_vif_set_mgmt_ie(ifp->vif, BRCMF_VNDR_IE_ASSOCREQ_FLAG,
				    sme->ie, sme->ie_len);
	if (err)
		brcmf_err("Set Assoc REQ IE Failed\n");
	else
		brcmf_dbg(TRACE, "Applied Vndr IEs for Assoc request\n");

	set_bit(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state);

	if (chan) {
		cfg->channel =
			ieee80211_frequency_to_channel(chan->center_freq);
		chanspec = channel_to_chanspec(&cfg->d11inf, chan);
		brcmf_dbg(CONN, "channel=%d, center_req=%d, chanspec=0x%04x\n",
			  cfg->channel, chan->center_freq, chanspec);
	} else {
		cfg->channel = 0;
		chanspec = 0;
	}

	brcmf_dbg(INFO, "ie (%p), ie_len (%zd)\n", sme->ie, sme->ie_len);

	err = brcmf_set_wpa_version(ndev, sme);
	if (err) {
		brcmf_err("wl_set_wpa_version failed (%d)\n", err);
		goto done;
	}

	sme->auth_type = brcmf_war_auth_type(ifp, sme->auth_type);
	err = brcmf_set_auth_type(ndev, sme);
	if (err) {
		brcmf_err("wl_set_auth_type failed (%d)\n", err);
		goto done;
	}

	err = brcmf_set_wsec_mode(ndev, sme, sme->mfp == NL80211_MFP_REQUIRED);
	if (err) {
		brcmf_err("wl_set_set_cipher failed (%d)\n", err);
		goto done;
	}

	err = brcmf_set_key_mgmt(ndev, sme);
	if (err) {
		brcmf_err("wl_set_key_mgmt failed (%d)\n", err);
		goto done;
	}

	err = brcmf_set_sharedkey(ndev, sme);
	if (err) {
		brcmf_err("brcmf_set_sharedkey failed (%d)\n", err);
		goto done;
	}

	/* Join with specific BSSID and cached SSID
	 * If SSID is zero join based on BSSID only
	 */
	join_params_size = offsetof(struct brcmf_ext_join_params_le, assoc_le) +
		offsetof(struct brcmf_assoc_params_le, chanspec_list);
	if (cfg->channel)
		join_params_size += sizeof(u16);
	ext_join_params = kzalloc(join_params_size, GFP_KERNEL);
	if (ext_join_params == NULL) {
		err = -ENOMEM;
		goto done;
	}
	ssid_len = min_t(u32, sme->ssid_len, IEEE80211_MAX_SSID_LEN);
	ext_join_params->ssid_le.SSID_len = cpu_to_le32(ssid_len);
	memcpy(&ext_join_params->ssid_le.SSID, sme->ssid, ssid_len);
	if (ssid_len < IEEE80211_MAX_SSID_LEN)
		brcmf_dbg(CONN, "SSID \"%s\", len (%d)\n",
			  ext_join_params->ssid_le.SSID, ssid_len);

	/* Set up join scan parameters */
	ext_join_params->scan_le.scan_type = -1;
	ext_join_params->scan_le.home_time = cpu_to_le32(-1);

	if (sme->bssid)
		memcpy(&ext_join_params->assoc_le.bssid, sme->bssid, ETH_ALEN);
	else
		eth_broadcast_addr(ext_join_params->assoc_le.bssid);

	if (cfg->channel) {
		ext_join_params->assoc_le.chanspec_num = cpu_to_le32(1);

		ext_join_params->assoc_le.chanspec_list[0] =
			cpu_to_le16(chanspec);
		/* Increase dwell time to receive probe response or detect
		 * beacon from target AP at a noisy air only during connect
		 * command.
		 */
		ext_join_params->scan_le.active_time =
			cpu_to_le32(BRCMF_SCAN_JOIN_ACTIVE_DWELL_TIME_MS);
		ext_join_params->scan_le.passive_time =
			cpu_to_le32(BRCMF_SCAN_JOIN_PASSIVE_DWELL_TIME_MS);
		/* To sync with presence period of VSDB GO send probe request
		 * more frequently. Probe request will be stopped when it gets
		 * probe response from target AP/GO.
		 */
		ext_join_params->scan_le.nprobes =
			cpu_to_le32(BRCMF_SCAN_JOIN_ACTIVE_DWELL_TIME_MS /
				    BRCMF_SCAN_JOIN_PROBE_INTERVAL_MS);
	} else {
		ext_join_params->scan_le.active_time = cpu_to_le32(-1);
		ext_join_params->scan_le.passive_time = cpu_to_le32(-1);
		ext_join_params->scan_le.nprobes = cpu_to_le32(-1);
	}

	err  = brcmf_fil_bsscfg_data_set(ifp, "join", ext_join_params,
					 join_params_size);
	kfree(ext_join_params);
	if (!err)
		/* This is it. join command worked, we are done */
		goto done;

	/* join command failed, fallback to set ssid */
	memset(&join_params, 0, sizeof(join_params));
	join_params_size = sizeof(join_params.ssid_le);

	memcpy(&join_params.ssid_le.SSID, sme->ssid, ssid_len);
	join_params.ssid_le.SSID_len = cpu_to_le32(ssid_len);

	if (sme->bssid)
		memcpy(join_params.params_le.bssid, sme->bssid, ETH_ALEN);
	else
		eth_broadcast_addr(join_params.params_le.bssid);

	if (cfg->channel) {
		join_params.params_le.chanspec_list[0] = cpu_to_le16(chanspec);
		join_params.params_le.chanspec_num = cpu_to_le32(1);
		join_params_size += sizeof(join_params.params_le);
	}
	err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_SSID,
				     &join_params, join_params_size);
	if (err)
		brcmf_err("BRCMF_C_SET_SSID failed (%d)\n", err);

done:
	if (err)
		clear_bit(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state);
	brcmf_dbg(TRACE, "Exit\n");
	return err;
}

static s32
brcmf_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *ndev,
		       u16 reason_code)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_profile *profile = &ifp->vif->profile;
	struct brcmf_scb_val_le scbval;
	s32 err = 0;

	brcmf_dbg(TRACE, "Enter. Reason code = %d\n", reason_code);
	if (!check_vif_up(ifp->vif))
		return -EIO;

	clear_bit(BRCMF_VIF_STATUS_CONNECTED, &ifp->vif->sme_state);
	clear_bit(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state);
	cfg80211_disconnected(ndev, reason_code, NULL, 0, true, GFP_KERNEL);

	memcpy(&scbval.ea, &profile->bssid, ETH_ALEN);
	scbval.val = cpu_to_le32(reason_code);
	err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_DISASSOC,
				     &scbval, sizeof(scbval));
	if (err)
		brcmf_err("error (%d)\n", err);

	brcmf_dbg(TRACE, "Exit\n");
	return err;
}

static s32
brcmf_cfg80211_set_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
			    enum nl80211_tx_power_setting type, s32 mbm)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct net_device *ndev = cfg_to_ndev(cfg);
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err;
	s32 disable;
	u32 qdbm = 127;

	brcmf_dbg(TRACE, "Enter %d %d\n", type, mbm);
	if (!check_vif_up(ifp->vif))
		return -EIO;

	switch (type) {
	case NL80211_TX_POWER_AUTOMATIC:
		break;
	case NL80211_TX_POWER_LIMITED:
	case NL80211_TX_POWER_FIXED:
		if (mbm < 0) {
			brcmf_err("TX_POWER_FIXED - dbm is negative\n");
			err = -EINVAL;
			goto done;
		}
		qdbm =  MBM_TO_DBM(4 * mbm);
		if (qdbm > 127)
			qdbm = 127;
		qdbm |= WL_TXPWR_OVERRIDE;
		break;
	default:
		brcmf_err("Unsupported type %d\n", type);
		err = -EINVAL;
		goto done;
	}
	/* Make sure radio is off or on as far as software is concerned */
	disable = WL_RADIO_SW_DISABLE << 16;
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_RADIO, disable);
	if (err)
		brcmf_err("WLC_SET_RADIO error (%d)\n", err);

	err = brcmf_fil_iovar_int_set(ifp, "qtxpower", qdbm);
	if (err)
		brcmf_err("qtxpower error (%d)\n", err);

done:
	brcmf_dbg(TRACE, "Exit %d (qdbm)\n", qdbm & ~WL_TXPWR_OVERRIDE);
	return err;
}

static s32
brcmf_cfg80211_get_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
			    s32 *dbm)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct net_device *ndev = cfg_to_ndev(cfg);
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 qdbm = 0;
	s32 err;

	brcmf_dbg(TRACE, "Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	err = brcmf_fil_iovar_int_get(ifp, "qtxpower", &qdbm);
	if (err) {
		brcmf_err("error (%d)\n", err);
		goto done;
	}
	*dbm = (qdbm & ~WL_TXPWR_OVERRIDE) / 4;

done:
	brcmf_dbg(TRACE, "Exit (0x%x %d)\n", qdbm, *dbm);
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

	brcmf_dbg(TRACE, "Enter\n");
	brcmf_dbg(CONN, "key index (%d)\n", key_idx);
	if (!check_vif_up(ifp->vif))
		return -EIO;

	err = brcmf_fil_bsscfg_int_get(ifp, "wsec", &wsec);
	if (err) {
		brcmf_err("WLC_GET_WSEC error (%d)\n", err);
		goto done;
	}

	if (wsec & WEP_ENABLED) {
		/* Just select a new current key */
		index = key_idx;
		err = brcmf_fil_cmd_int_set(ifp,
					    BRCMF_C_SET_KEY_PRIMARY, index);
		if (err)
			brcmf_err("error (%d)\n", err);
	}
done:
	brcmf_dbg(TRACE, "Exit\n");
	return err;
}

static s32
brcmf_add_keyext(struct wiphy *wiphy, struct net_device *ndev,
	      u8 key_idx, const u8 *mac_addr, struct key_params *params)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_wsec_key key;
	s32 err = 0;
	u8 keybuf[8];

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
		err = send_key_to_dongle(ifp, &key);
		if (err)
			brcmf_err("key delete error (%d)\n", err);
	} else {
		if (key.len > sizeof(key.data)) {
			brcmf_err("Invalid key length (%d)\n", key.len);
			return -EINVAL;
		}

		brcmf_dbg(CONN, "Setting the key index %d\n", key.index);
		memcpy(key.data, params->key, key.len);

		if (!brcmf_is_apmode(ifp->vif) &&
		    (params->cipher == WLAN_CIPHER_SUITE_TKIP)) {
			brcmf_dbg(CONN, "Swapping RX/TX MIC key\n");
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
			brcmf_dbg(CONN, "WLAN_CIPHER_SUITE_WEP40\n");
			break;
		case WLAN_CIPHER_SUITE_WEP104:
			key.algo = CRYPTO_ALGO_WEP128;
			brcmf_dbg(CONN, "WLAN_CIPHER_SUITE_WEP104\n");
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			key.algo = CRYPTO_ALGO_TKIP;
			brcmf_dbg(CONN, "WLAN_CIPHER_SUITE_TKIP\n");
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			key.algo = CRYPTO_ALGO_AES_CCM;
			brcmf_dbg(CONN, "WLAN_CIPHER_SUITE_AES_CMAC\n");
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			key.algo = CRYPTO_ALGO_AES_CCM;
			brcmf_dbg(CONN, "WLAN_CIPHER_SUITE_CCMP\n");
			break;
		default:
			brcmf_err("Invalid cipher (0x%x)\n", params->cipher);
			return -EINVAL;
		}
		err = send_key_to_dongle(ifp, &key);
		if (err)
			brcmf_err("wsec_key error (%d)\n", err);
	}
	return err;
}

static s32
brcmf_cfg80211_add_key(struct wiphy *wiphy, struct net_device *ndev,
		    u8 key_idx, bool pairwise, const u8 *mac_addr,
		    struct key_params *params)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_wsec_key *key;
	s32 val;
	s32 wsec;
	s32 err = 0;
	u8 keybuf[8];

	brcmf_dbg(TRACE, "Enter\n");
	brcmf_dbg(CONN, "key index (%d)\n", key_idx);
	if (!check_vif_up(ifp->vif))
		return -EIO;

	if (key_idx >= BRCMF_MAX_DEFAULT_KEYS) {
		/* we ignore this key index in this case */
		brcmf_err("invalid key index (%d)\n", key_idx);
		return -EINVAL;
	}

	if (mac_addr &&
		(params->cipher != WLAN_CIPHER_SUITE_WEP40) &&
		(params->cipher != WLAN_CIPHER_SUITE_WEP104)) {
		brcmf_dbg(TRACE, "Exit");
		return brcmf_add_keyext(wiphy, ndev, key_idx, mac_addr, params);
	}

	key = &ifp->vif->profile.key[key_idx];
	memset(key, 0, sizeof(*key));

	if (params->key_len > sizeof(key->data)) {
		brcmf_err("Too long key length (%u)\n", params->key_len);
		err = -EINVAL;
		goto done;
	}
	key->len = params->key_len;
	key->index = key_idx;

	memcpy(key->data, params->key, key->len);

	key->flags = BRCMF_PRIMARY_KEY;
	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		key->algo = CRYPTO_ALGO_WEP1;
		val = WEP_ENABLED;
		brcmf_dbg(CONN, "WLAN_CIPHER_SUITE_WEP40\n");
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		key->algo = CRYPTO_ALGO_WEP128;
		val = WEP_ENABLED;
		brcmf_dbg(CONN, "WLAN_CIPHER_SUITE_WEP104\n");
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		if (!brcmf_is_apmode(ifp->vif)) {
			brcmf_dbg(CONN, "Swapping RX/TX MIC key\n");
			memcpy(keybuf, &key->data[24], sizeof(keybuf));
			memcpy(&key->data[24], &key->data[16], sizeof(keybuf));
			memcpy(&key->data[16], keybuf, sizeof(keybuf));
		}
		key->algo = CRYPTO_ALGO_TKIP;
		val = TKIP_ENABLED;
		brcmf_dbg(CONN, "WLAN_CIPHER_SUITE_TKIP\n");
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		key->algo = CRYPTO_ALGO_AES_CCM;
		val = AES_ENABLED;
		brcmf_dbg(CONN, "WLAN_CIPHER_SUITE_AES_CMAC\n");
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key->algo = CRYPTO_ALGO_AES_CCM;
		val = AES_ENABLED;
		brcmf_dbg(CONN, "WLAN_CIPHER_SUITE_CCMP\n");
		break;
	default:
		brcmf_err("Invalid cipher (0x%x)\n", params->cipher);
		err = -EINVAL;
		goto done;
	}

	err = send_key_to_dongle(ifp, key);
	if (err)
		goto done;

	err = brcmf_fil_bsscfg_int_get(ifp, "wsec", &wsec);
	if (err) {
		brcmf_err("get wsec error (%d)\n", err);
		goto done;
	}
	wsec |= val;
	err = brcmf_fil_bsscfg_int_set(ifp, "wsec", wsec);
	if (err) {
		brcmf_err("set wsec error (%d)\n", err);
		goto done;
	}

done:
	brcmf_dbg(TRACE, "Exit\n");
	return err;
}

static s32
brcmf_cfg80211_del_key(struct wiphy *wiphy, struct net_device *ndev,
		    u8 key_idx, bool pairwise, const u8 *mac_addr)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_wsec_key key;
	s32 err = 0;

	brcmf_dbg(TRACE, "Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	if (key_idx >= BRCMF_MAX_DEFAULT_KEYS) {
		/* we ignore this key index in this case */
		return -EINVAL;
	}

	memset(&key, 0, sizeof(key));

	key.index = (u32) key_idx;
	key.flags = BRCMF_PRIMARY_KEY;
	key.algo = CRYPTO_ALGO_OFF;

	brcmf_dbg(CONN, "key index (%d)\n", key_idx);

	/* Set the new key/index */
	err = send_key_to_dongle(ifp, &key);

	brcmf_dbg(TRACE, "Exit\n");
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

	brcmf_dbg(TRACE, "Enter\n");
	brcmf_dbg(CONN, "key index (%d)\n", key_idx);
	if (!check_vif_up(ifp->vif))
		return -EIO;

	memset(&params, 0, sizeof(params));

	err = brcmf_fil_bsscfg_int_get(ifp, "wsec", &wsec);
	if (err) {
		brcmf_err("WLC_GET_WSEC error (%d)\n", err);
		/* Ignore this error, may happen during DISASSOC */
		err = -EAGAIN;
		goto done;
	}
	if (wsec & WEP_ENABLED) {
		sec = &profile->sec;
		if (sec->cipher_pairwise & WLAN_CIPHER_SUITE_WEP40) {
			params.cipher = WLAN_CIPHER_SUITE_WEP40;
			brcmf_dbg(CONN, "WLAN_CIPHER_SUITE_WEP40\n");
		} else if (sec->cipher_pairwise & WLAN_CIPHER_SUITE_WEP104) {
			params.cipher = WLAN_CIPHER_SUITE_WEP104;
			brcmf_dbg(CONN, "WLAN_CIPHER_SUITE_WEP104\n");
		}
	} else if (wsec & TKIP_ENABLED) {
		params.cipher = WLAN_CIPHER_SUITE_TKIP;
		brcmf_dbg(CONN, "WLAN_CIPHER_SUITE_TKIP\n");
	} else if (wsec & AES_ENABLED) {
		params.cipher = WLAN_CIPHER_SUITE_AES_CMAC;
		brcmf_dbg(CONN, "WLAN_CIPHER_SUITE_AES_CMAC\n");
	} else  {
		brcmf_err("Invalid algo (0x%x)\n", wsec);
		err = -EINVAL;
		goto done;
	}
	callback(cookie, &params);

done:
	brcmf_dbg(TRACE, "Exit\n");
	return err;
}

static s32
brcmf_cfg80211_config_default_mgmt_key(struct wiphy *wiphy,
				    struct net_device *ndev, u8 key_idx)
{
	brcmf_dbg(INFO, "Not supported\n");

	return -EOPNOTSUPP;
}

static void
brcmf_cfg80211_reconfigure_wep(struct brcmf_if *ifp)
{
	s32 err;
	u8 key_idx;
	struct brcmf_wsec_key *key;
	s32 wsec;

	for (key_idx = 0; key_idx < BRCMF_MAX_DEFAULT_KEYS; key_idx++) {
		key = &ifp->vif->profile.key[key_idx];
		if ((key->algo == CRYPTO_ALGO_WEP1) ||
		    (key->algo == CRYPTO_ALGO_WEP128))
			break;
	}
	if (key_idx == BRCMF_MAX_DEFAULT_KEYS)
		return;

	err = send_key_to_dongle(ifp, key);
	if (err) {
		brcmf_err("Setting WEP key failed (%d)\n", err);
		return;
	}
	err = brcmf_fil_bsscfg_int_get(ifp, "wsec", &wsec);
	if (err) {
		brcmf_err("get wsec error (%d)\n", err);
		return;
	}
	wsec |= WEP_ENABLED;
	err = brcmf_fil_bsscfg_int_set(ifp, "wsec", wsec);
	if (err)
		brcmf_err("set wsec error (%d)\n", err);
}

static void brcmf_convert_sta_flags(u32 fw_sta_flags, struct station_info *si)
{
	struct nl80211_sta_flag_update *sfu;

	brcmf_dbg(TRACE, "flags %08x\n", fw_sta_flags);
	si->filled |= BIT(NL80211_STA_INFO_STA_FLAGS);
	sfu = &si->sta_flags;
	sfu->mask = BIT(NL80211_STA_FLAG_WME) |
		    BIT(NL80211_STA_FLAG_AUTHENTICATED) |
		    BIT(NL80211_STA_FLAG_ASSOCIATED) |
		    BIT(NL80211_STA_FLAG_AUTHORIZED);
	if (fw_sta_flags & BRCMF_STA_WME)
		sfu->set |= BIT(NL80211_STA_FLAG_WME);
	if (fw_sta_flags & BRCMF_STA_AUTHE)
		sfu->set |= BIT(NL80211_STA_FLAG_AUTHENTICATED);
	if (fw_sta_flags & BRCMF_STA_ASSOC)
		sfu->set |= BIT(NL80211_STA_FLAG_ASSOCIATED);
	if (fw_sta_flags & BRCMF_STA_AUTHO)
		sfu->set |= BIT(NL80211_STA_FLAG_AUTHORIZED);
}

static void brcmf_fill_bss_param(struct brcmf_if *ifp, struct station_info *si)
{
	struct {
		__le32 len;
		struct brcmf_bss_info_le bss_le;
	} *buf;
	u16 capability;
	int err;

	buf = kzalloc(WL_BSS_INFO_MAX, GFP_KERNEL);
	if (!buf)
		return;

	buf->len = cpu_to_le32(WL_BSS_INFO_MAX);
	err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BSS_INFO, buf,
				     WL_BSS_INFO_MAX);
	if (err) {
		brcmf_err("Failed to get bss info (%d)\n", err);
		return;
	}
	si->filled |= BIT(NL80211_STA_INFO_BSS_PARAM);
	si->bss_param.beacon_interval = le16_to_cpu(buf->bss_le.beacon_period);
	si->bss_param.dtim_period = buf->bss_le.dtim_period;
	capability = le16_to_cpu(buf->bss_le.capability);
	if (capability & IEEE80211_HT_STBC_PARAM_DUAL_CTS_PROT)
		si->bss_param.flags |= BSS_PARAM_FLAGS_CTS_PROT;
	if (capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
		si->bss_param.flags |= BSS_PARAM_FLAGS_SHORT_PREAMBLE;
	if (capability & WLAN_CAPABILITY_SHORT_SLOT_TIME)
		si->bss_param.flags |= BSS_PARAM_FLAGS_SHORT_SLOT_TIME;
}

static s32
brcmf_cfg80211_get_station_ibss(struct brcmf_if *ifp,
				struct station_info *sinfo)
{
	struct brcmf_scb_val_le scbval;
	struct brcmf_pktcnt_le pktcnt;
	s32 err;
	u32 rate;
	u32 rssi;

	/* Get the current tx rate */
	err = brcmf_fil_cmd_int_get(ifp, BRCMF_C_GET_RATE, &rate);
	if (err < 0) {
		brcmf_err("BRCMF_C_GET_RATE error (%d)\n", err);
		return err;
	}
	sinfo->filled |= BIT(NL80211_STA_INFO_TX_BITRATE);
	sinfo->txrate.legacy = rate * 5;

	memset(&scbval, 0, sizeof(scbval));
	err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_RSSI, &scbval,
				     sizeof(scbval));
	if (err) {
		brcmf_err("BRCMF_C_GET_RSSI error (%d)\n", err);
		return err;
	}
	rssi = le32_to_cpu(scbval.val);
	sinfo->filled |= BIT(NL80211_STA_INFO_SIGNAL);
	sinfo->signal = rssi;

	err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_GET_PKTCNTS, &pktcnt,
				     sizeof(pktcnt));
	if (err) {
		brcmf_err("BRCMF_C_GET_GET_PKTCNTS error (%d)\n", err);
		return err;
	}
	sinfo->filled |= BIT(NL80211_STA_INFO_RX_PACKETS) |
			 BIT(NL80211_STA_INFO_RX_DROP_MISC) |
			 BIT(NL80211_STA_INFO_TX_PACKETS) |
			 BIT(NL80211_STA_INFO_TX_FAILED);
	sinfo->rx_packets = le32_to_cpu(pktcnt.rx_good_pkt);
	sinfo->rx_dropped_misc = le32_to_cpu(pktcnt.rx_bad_pkt);
	sinfo->tx_packets = le32_to_cpu(pktcnt.tx_good_pkt);
	sinfo->tx_failed  = le32_to_cpu(pktcnt.tx_bad_pkt);

	return 0;
}

static s32
brcmf_cfg80211_get_station(struct wiphy *wiphy, struct net_device *ndev,
			   const u8 *mac, struct station_info *sinfo)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err = 0;
	struct brcmf_sta_info_le sta_info_le;
	u32 sta_flags;
	u32 is_tdls_peer;
	s32 total_rssi;
	s32 count_rssi;
	u32 i;

	brcmf_dbg(TRACE, "Enter, MAC %pM\n", mac);
	if (!check_vif_up(ifp->vif))
		return -EIO;

	if (brcmf_is_ibssmode(ifp->vif))
		return brcmf_cfg80211_get_station_ibss(ifp, sinfo);

	memset(&sta_info_le, 0, sizeof(sta_info_le));
	memcpy(&sta_info_le, mac, ETH_ALEN);
	err = brcmf_fil_iovar_data_get(ifp, "tdls_sta_info",
				       &sta_info_le,
				       sizeof(sta_info_le));
	is_tdls_peer = !err;
	if (err) {
		err = brcmf_fil_iovar_data_get(ifp, "sta_info",
					       &sta_info_le,
					       sizeof(sta_info_le));
		if (err < 0) {
			brcmf_err("GET STA INFO failed, %d\n", err);
			goto done;
		}
	}
	brcmf_dbg(TRACE, "version %d\n", le16_to_cpu(sta_info_le.ver));
	sinfo->filled = BIT(NL80211_STA_INFO_INACTIVE_TIME);
	sinfo->inactive_time = le32_to_cpu(sta_info_le.idle) * 1000;
	sta_flags = le32_to_cpu(sta_info_le.flags);
	brcmf_convert_sta_flags(sta_flags, sinfo);
	sinfo->sta_flags.mask |= BIT(NL80211_STA_FLAG_TDLS_PEER);
	if (is_tdls_peer)
		sinfo->sta_flags.set |= BIT(NL80211_STA_FLAG_TDLS_PEER);
	else
		sinfo->sta_flags.set &= ~BIT(NL80211_STA_FLAG_TDLS_PEER);
	if (sta_flags & BRCMF_STA_ASSOC) {
		sinfo->filled |= BIT(NL80211_STA_INFO_CONNECTED_TIME);
		sinfo->connected_time = le32_to_cpu(sta_info_le.in);
		brcmf_fill_bss_param(ifp, sinfo);
	}
	if (sta_flags & BRCMF_STA_SCBSTATS) {
		sinfo->filled |= BIT(NL80211_STA_INFO_TX_FAILED);
		sinfo->tx_failed = le32_to_cpu(sta_info_le.tx_failures);
		sinfo->filled |= BIT(NL80211_STA_INFO_TX_PACKETS);
		sinfo->tx_packets = le32_to_cpu(sta_info_le.tx_pkts);
		sinfo->tx_packets += le32_to_cpu(sta_info_le.tx_mcast_pkts);
		sinfo->filled |= BIT(NL80211_STA_INFO_RX_PACKETS);
		sinfo->rx_packets = le32_to_cpu(sta_info_le.rx_ucast_pkts);
		sinfo->rx_packets += le32_to_cpu(sta_info_le.rx_mcast_pkts);
		if (sinfo->tx_packets) {
			sinfo->filled |= BIT(NL80211_STA_INFO_TX_BITRATE);
			sinfo->txrate.legacy =
				le32_to_cpu(sta_info_le.tx_rate) / 100;
		}
		if (sinfo->rx_packets) {
			sinfo->filled |= BIT(NL80211_STA_INFO_RX_BITRATE);
			sinfo->rxrate.legacy =
				le32_to_cpu(sta_info_le.rx_rate) / 100;
		}
		if (le16_to_cpu(sta_info_le.ver) >= 4) {
			sinfo->filled |= BIT(NL80211_STA_INFO_TX_BYTES);
			sinfo->tx_bytes = le64_to_cpu(sta_info_le.tx_tot_bytes);
			sinfo->filled |= BIT(NL80211_STA_INFO_RX_BYTES);
			sinfo->rx_bytes = le64_to_cpu(sta_info_le.rx_tot_bytes);
		}
		total_rssi = 0;
		count_rssi = 0;
		for (i = 0; i < BRCMF_ANT_MAX; i++) {
			if (sta_info_le.rssi[i]) {
				sinfo->chain_signal_avg[count_rssi] =
					sta_info_le.rssi[i];
				sinfo->chain_signal[count_rssi] =
					sta_info_le.rssi[i];
				total_rssi += sta_info_le.rssi[i];
				count_rssi++;
			}
		}
		if (count_rssi) {
			sinfo->filled |= BIT(NL80211_STA_INFO_CHAIN_SIGNAL);
			sinfo->chains = count_rssi;

			sinfo->filled |= BIT(NL80211_STA_INFO_SIGNAL);
			total_rssi /= count_rssi;
			sinfo->signal = total_rssi;
		}
	}
done:
	brcmf_dbg(TRACE, "Exit\n");
	return err;
}

static int
brcmf_cfg80211_dump_station(struct wiphy *wiphy, struct net_device *ndev,
			    int idx, u8 *mac, struct station_info *sinfo)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err;

	brcmf_dbg(TRACE, "Enter, idx %d\n", idx);

	if (idx == 0) {
		cfg->assoclist.count = cpu_to_le32(BRCMF_MAX_ASSOCLIST);
		err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_ASSOCLIST,
					     &cfg->assoclist,
					     sizeof(cfg->assoclist));
		if (err) {
			brcmf_err("BRCMF_C_GET_ASSOCLIST unsupported, err=%d\n",
				  err);
			cfg->assoclist.count = 0;
			return -EOPNOTSUPP;
		}
	}
	if (idx < le32_to_cpu(cfg->assoclist.count)) {
		memcpy(mac, cfg->assoclist.mac[idx], ETH_ALEN);
		return brcmf_cfg80211_get_station(wiphy, ndev, mac, sinfo);
	}
	return -ENOENT;
}

static s32
brcmf_cfg80211_set_power_mgmt(struct wiphy *wiphy, struct net_device *ndev,
			   bool enabled, s32 timeout)
{
	s32 pm;
	s32 err = 0;
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);

	brcmf_dbg(TRACE, "Enter\n");

	/*
	 * Powersave enable/disable request is coming from the
	 * cfg80211 even before the interface is up. In that
	 * scenario, driver will be storing the power save
	 * preference in cfg struct to apply this to
	 * FW later while initializing the dongle
	 */
	cfg->pwr_save = enabled;
	if (!check_vif_up(ifp->vif)) {

		brcmf_dbg(INFO, "Device is not ready, storing the value in cfg_info struct\n");
		goto done;
	}

	pm = enabled ? PM_FAST : PM_OFF;
	/* Do not enable the power save after assoc if it is a p2p interface */
	if (ifp->vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT) {
		brcmf_dbg(INFO, "Do not enable power save for P2P clients\n");
		pm = PM_OFF;
	}
	brcmf_dbg(INFO, "power save %s\n", (pm ? "enabled" : "disabled"));

	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_PM, pm);
	if (err) {
		if (err == -ENODEV)
			brcmf_err("net_device is not ready yet\n");
		else
			brcmf_err("error (%d)\n", err);
	}
done:
	brcmf_dbg(TRACE, "Exit\n");
	return err;
}

static s32 brcmf_inform_single_bss(struct brcmf_cfg80211_info *cfg,
				   struct brcmf_bss_info_le *bi)
{
	struct wiphy *wiphy = cfg_to_wiphy(cfg);
	struct ieee80211_channel *notify_channel;
	struct cfg80211_bss *bss;
	struct ieee80211_supported_band *band;
	struct brcmu_chan ch;
	u16 channel;
	u32 freq;
	u16 notify_capability;
	u16 notify_interval;
	u8 *notify_ie;
	size_t notify_ielen;
	s32 notify_signal;

	if (le32_to_cpu(bi->length) > WL_BSS_INFO_MAX) {
		brcmf_err("Bss info is larger than buffer. Discarding\n");
		return 0;
	}

	if (!bi->ctl_ch) {
		ch.chspec = le16_to_cpu(bi->chanspec);
		cfg->d11inf.decchspec(&ch);
		bi->ctl_ch = ch.chnum;
	}
	channel = bi->ctl_ch;

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

	brcmf_dbg(CONN, "bssid: %pM\n", bi->BSSID);
	brcmf_dbg(CONN, "Channel: %d(%d)\n", channel, freq);
	brcmf_dbg(CONN, "Capability: %X\n", notify_capability);
	brcmf_dbg(CONN, "Beacon interval: %d\n", notify_interval);
	brcmf_dbg(CONN, "Signal: %d\n", notify_signal);

	bss = cfg80211_inform_bss(wiphy, notify_channel,
				  CFG80211_BSS_FTYPE_UNKNOWN,
				  (const u8 *)bi->BSSID,
				  0, notify_capability,
				  notify_interval, notify_ie,
				  notify_ielen, notify_signal,
				  GFP_KERNEL);

	if (!bss)
		return -ENOMEM;

	cfg80211_put_bss(wiphy, bss);

	return 0;
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

	bss_list = (struct brcmf_scan_results *)cfg->escan_info.escan_buf;
	if (bss_list->count != 0 &&
	    bss_list->version != BRCMF_BSS_INFO_VERSION) {
		brcmf_err("Version %d != WL_BSS_INFO_VERSION\n",
			  bss_list->version);
		return -EOPNOTSUPP;
	}
	brcmf_dbg(SCAN, "scanned AP count (%d)\n", bss_list->count);
	for (i = 0; i < bss_list->count; i++) {
		bi = next_bss_le(bss_list, bi);
		err = brcmf_inform_single_bss(cfg, bi);
		if (err)
			break;
	}
	return err;
}

static s32 brcmf_inform_ibss(struct brcmf_cfg80211_info *cfg,
			     struct net_device *ndev, const u8 *bssid)
{
	struct wiphy *wiphy = cfg_to_wiphy(cfg);
	struct ieee80211_channel *notify_channel;
	struct brcmf_bss_info_le *bi = NULL;
	struct ieee80211_supported_band *band;
	struct cfg80211_bss *bss;
	struct brcmu_chan ch;
	u8 *buf = NULL;
	s32 err = 0;
	u32 freq;
	u16 notify_capability;
	u16 notify_interval;
	u8 *notify_ie;
	size_t notify_ielen;
	s32 notify_signal;

	brcmf_dbg(TRACE, "Enter\n");

	buf = kzalloc(WL_BSS_INFO_MAX, GFP_KERNEL);
	if (buf == NULL) {
		err = -ENOMEM;
		goto CleanUp;
	}

	*(__le32 *)buf = cpu_to_le32(WL_BSS_INFO_MAX);

	err = brcmf_fil_cmd_data_get(netdev_priv(ndev), BRCMF_C_GET_BSS_INFO,
				     buf, WL_BSS_INFO_MAX);
	if (err) {
		brcmf_err("WLC_GET_BSS_INFO failed: %d\n", err);
		goto CleanUp;
	}

	bi = (struct brcmf_bss_info_le *)(buf + 4);

	ch.chspec = le16_to_cpu(bi->chanspec);
	cfg->d11inf.decchspec(&ch);

	if (ch.band == BRCMU_CHAN_BAND_2G)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];

	freq = ieee80211_channel_to_frequency(ch.chnum, band->band);
	cfg->channel = freq;
	notify_channel = ieee80211_get_channel(wiphy, freq);

	notify_capability = le16_to_cpu(bi->capability);
	notify_interval = le16_to_cpu(bi->beacon_period);
	notify_ie = (u8 *)bi + le16_to_cpu(bi->ie_offset);
	notify_ielen = le32_to_cpu(bi->ie_length);
	notify_signal = (s16)le16_to_cpu(bi->RSSI) * 100;

	brcmf_dbg(CONN, "channel: %d(%d)\n", ch.chnum, freq);
	brcmf_dbg(CONN, "capability: %X\n", notify_capability);
	brcmf_dbg(CONN, "beacon interval: %d\n", notify_interval);
	brcmf_dbg(CONN, "signal: %d\n", notify_signal);

	bss = cfg80211_inform_bss(wiphy, notify_channel,
				  CFG80211_BSS_FTYPE_UNKNOWN, bssid, 0,
				  notify_capability, notify_interval,
				  notify_ie, notify_ielen, notify_signal,
				  GFP_KERNEL);

	if (!bss) {
		err = -ENOMEM;
		goto CleanUp;
	}

	cfg80211_put_bss(wiphy, bss);

CleanUp:

	kfree(buf);

	brcmf_dbg(TRACE, "Exit\n");

	return err;
}

static s32 brcmf_update_bss_info(struct brcmf_cfg80211_info *cfg,
				 struct brcmf_if *ifp)
{
	struct brcmf_bss_info_le *bi;
	const struct brcmf_tlv *tim;
	u16 beacon_interval;
	u8 dtim_period;
	size_t ie_len;
	u8 *ie;
	s32 err = 0;

	brcmf_dbg(TRACE, "Enter\n");
	if (brcmf_is_ibssmode(ifp->vif))
		return err;

	*(__le32 *)cfg->extra_buf = cpu_to_le32(WL_EXTRA_BUF_MAX);
	err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BSS_INFO,
				     cfg->extra_buf, WL_EXTRA_BUF_MAX);
	if (err) {
		brcmf_err("Could not get bss info %d\n", err);
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
			brcmf_err("wl dtim_assoc failed (%d)\n", err);
			goto update_bss_info_out;
		}
		dtim_period = (u8)var;
	}

update_bss_info_out:
	brcmf_dbg(TRACE, "Exit");
	return err;
}

void brcmf_abort_scanning(struct brcmf_cfg80211_info *cfg)
{
	struct escan_info *escan = &cfg->escan_info;

	set_bit(BRCMF_SCAN_STATUS_ABORT, &cfg->scan_status);
	if (cfg->scan_request) {
		escan->escan_state = WL_ESCAN_STATE_IDLE;
		brcmf_notify_escan_complete(cfg, escan->ifp, true, true);
	}
	clear_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status);
	clear_bit(BRCMF_SCAN_STATUS_ABORT, &cfg->scan_status);
}

static void brcmf_cfg80211_escan_timeout_worker(struct work_struct *work)
{
	struct brcmf_cfg80211_info *cfg =
			container_of(work, struct brcmf_cfg80211_info,
				     escan_timeout_work);

	brcmf_inform_bss(cfg);
	brcmf_notify_escan_complete(cfg, cfg->escan_info.ifp, true, true);
}

static void brcmf_escan_timeout(unsigned long data)
{
	struct brcmf_cfg80211_info *cfg =
			(struct brcmf_cfg80211_info *)data;

	if (cfg->scan_request) {
		brcmf_err("timer expired\n");
		schedule_work(&cfg->escan_timeout_work);
	}
}

static s32
brcmf_compare_update_same_bss(struct brcmf_cfg80211_info *cfg,
			      struct brcmf_bss_info_le *bss,
			      struct brcmf_bss_info_le *bss_info_le)
{
	struct brcmu_chan ch_bss, ch_bss_info_le;

	ch_bss.chspec = le16_to_cpu(bss->chanspec);
	cfg->d11inf.decchspec(&ch_bss);
	ch_bss_info_le.chspec = le16_to_cpu(bss_info_le->chanspec);
	cfg->d11inf.decchspec(&ch_bss_info_le);

	if (!memcmp(&bss_info_le->BSSID, &bss->BSSID, ETH_ALEN) &&
		ch_bss.band == ch_bss_info_le.band &&
		bss_info_le->SSID_len == bss->SSID_len &&
		!memcmp(bss_info_le->SSID, bss->SSID, bss_info_le->SSID_len)) {
		if ((bss->flags & BRCMF_BSS_RSSI_ON_CHANNEL) ==
			(bss_info_le->flags & BRCMF_BSS_RSSI_ON_CHANNEL)) {
			s16 bss_rssi = le16_to_cpu(bss->RSSI);
			s16 bss_info_rssi = le16_to_cpu(bss_info_le->RSSI);

			/* preserve max RSSI if the measurements are
			* both on-channel or both off-channel
			*/
			if (bss_info_rssi > bss_rssi)
				bss->RSSI = bss_info_le->RSSI;
		} else if ((bss->flags & BRCMF_BSS_RSSI_ON_CHANNEL) &&
			(bss_info_le->flags & BRCMF_BSS_RSSI_ON_CHANNEL) == 0) {
			/* preserve the on-channel rssi measurement
			* if the new measurement is off channel
			*/
			bss->RSSI = bss_info_le->RSSI;
			bss->flags |= BRCMF_BSS_RSSI_ON_CHANNEL;
		}
		return 1;
	}
	return 0;
}

static s32
brcmf_cfg80211_escan_handler(struct brcmf_if *ifp,
			     const struct brcmf_event_msg *e, void *data)
{
	struct brcmf_cfg80211_info *cfg = ifp->drvr->config;
	s32 status;
	struct brcmf_escan_result_le *escan_result_le;
	struct brcmf_bss_info_le *bss_info_le;
	struct brcmf_bss_info_le *bss = NULL;
	u32 bi_length;
	struct brcmf_scan_results *list;
	u32 i;
	bool aborted;

	status = e->status;

	if (!test_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status)) {
		brcmf_err("scan not ready, bsscfgidx=%d\n", ifp->bsscfgidx);
		return -EPERM;
	}

	if (status == BRCMF_E_STATUS_PARTIAL) {
		brcmf_dbg(SCAN, "ESCAN Partial result\n");
		escan_result_le = (struct brcmf_escan_result_le *) data;
		if (!escan_result_le) {
			brcmf_err("Invalid escan result (NULL pointer)\n");
			goto exit;
		}
		if (le16_to_cpu(escan_result_le->bss_count) != 1) {
			brcmf_err("Invalid bss_count %d: ignoring\n",
				  escan_result_le->bss_count);
			goto exit;
		}
		bss_info_le = &escan_result_le->bss_info_le;

		if (brcmf_p2p_scan_finding_common_channel(cfg, bss_info_le))
			goto exit;

		if (!cfg->scan_request) {
			brcmf_dbg(SCAN, "result without cfg80211 request\n");
			goto exit;
		}

		bi_length = le32_to_cpu(bss_info_le->length);
		if (bi_length != (le32_to_cpu(escan_result_le->buflen) -
					WL_ESCAN_RESULTS_FIXED_SIZE)) {
			brcmf_err("Invalid bss_info length %d: ignoring\n",
				  bi_length);
			goto exit;
		}

		if (!(cfg_to_wiphy(cfg)->interface_modes &
					BIT(NL80211_IFTYPE_ADHOC))) {
			if (le16_to_cpu(bss_info_le->capability) &
						WLAN_CAPABILITY_IBSS) {
				brcmf_err("Ignoring IBSS result\n");
				goto exit;
			}
		}

		list = (struct brcmf_scan_results *)
				cfg->escan_info.escan_buf;
		if (bi_length > BRCMF_ESCAN_BUF_SIZE - list->buflen) {
			brcmf_err("Buffer is too small: ignoring\n");
			goto exit;
		}

		for (i = 0; i < list->count; i++) {
			bss = bss ? (struct brcmf_bss_info_le *)
				((unsigned char *)bss +
				le32_to_cpu(bss->length)) : list->bss_info_le;
			if (brcmf_compare_update_same_bss(cfg, bss,
							  bss_info_le))
				goto exit;
		}
		memcpy(&cfg->escan_info.escan_buf[list->buflen], bss_info_le,
		       bi_length);
		list->version = le32_to_cpu(bss_info_le->version);
		list->buflen += bi_length;
		list->count++;
	} else {
		cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
		if (brcmf_p2p_scan_finding_common_channel(cfg, NULL))
			goto exit;
		if (cfg->scan_request) {
			brcmf_inform_bss(cfg);
			aborted = status != BRCMF_E_STATUS_SUCCESS;
			brcmf_notify_escan_complete(cfg, ifp, aborted, false);
		} else
			brcmf_dbg(SCAN, "Ignored scan complete result 0x%x\n",
				  status);
	}
exit:
	return 0;
}

static void brcmf_init_escan(struct brcmf_cfg80211_info *cfg)
{
	brcmf_fweh_register(cfg->pub, BRCMF_E_ESCAN_RESULT,
			    brcmf_cfg80211_escan_handler);
	cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
	/* Init scan_timeout timer */
	init_timer(&cfg->escan_timeout);
	cfg->escan_timeout.data = (unsigned long) cfg;
	cfg->escan_timeout.function = brcmf_escan_timeout;
	INIT_WORK(&cfg->escan_timeout_work,
		  brcmf_cfg80211_escan_timeout_worker);
}

/* PFN result doesn't have all the info which are required by the supplicant
 * (For e.g IEs) Do a target Escan so that sched scan results are reported
 * via wl_inform_single_bss in the required format. Escan does require the
 * scan request in the form of cfg80211_scan_request. For timebeing, create
 * cfg80211_scan_request one out of the received PNO event.
 */
static s32
brcmf_notify_sched_scan_results(struct brcmf_if *ifp,
				const struct brcmf_event_msg *e, void *data)
{
	struct brcmf_cfg80211_info *cfg = ifp->drvr->config;
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

	brcmf_dbg(SCAN, "Enter\n");

	if (e->event_code == BRCMF_E_PFN_NET_LOST) {
		brcmf_dbg(SCAN, "PFN NET LOST event. Do Nothing\n");
		return 0;
	}

	pfn_result = (struct brcmf_pno_scanresults_le *)data;
	result_count = le32_to_cpu(pfn_result->count);
	status = le32_to_cpu(pfn_result->status);

	/* PFN event is limited to fit 512 bytes so we may get
	 * multiple NET_FOUND events. For now place a warning here.
	 */
	WARN_ON(status != BRCMF_PNO_SCAN_COMPLETE);
	brcmf_dbg(SCAN, "PFN NET FOUND event. count: %d\n", result_count);
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
				brcmf_err("Invalid netinfo ptr. index: %d\n",
					  i);
				err = -EINVAL;
				goto out_err;
			}

			brcmf_dbg(SCAN, "SSID:%s Channel:%d\n",
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
		cfg->escan_info.run = brcmf_run_escan;
		err = brcmf_do_escan(cfg, wiphy, ifp, request);
		if (err) {
			clear_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status);
			goto out_err;
		}
		cfg->sched_escan = true;
		cfg->scan_request = request;
	} else {
		brcmf_err("FALSE PNO Event. (pfn_count == 0)\n");
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
		brcmf_err("failed code %d\n", ret);

	return ret;
}

static int brcmf_dev_pno_config(struct brcmf_if *ifp,
				struct cfg80211_sched_scan_request *request)
{
	struct brcmf_pno_param_le pfn_param;
	struct brcmf_pno_macaddr_le pfn_mac;
	s32 err;
	u8 *mac_mask;
	int i;

	memset(&pfn_param, 0, sizeof(pfn_param));
	pfn_param.version = cpu_to_le32(BRCMF_PNO_VERSION);

	/* set extra pno params */
	pfn_param.flags = cpu_to_le16(1 << BRCMF_PNO_ENABLE_ADAPTSCAN_BIT);
	pfn_param.repeat = BRCMF_PNO_REPEAT;
	pfn_param.exp = BRCMF_PNO_FREQ_EXPO_MAX;

	/* set up pno scan fr */
	pfn_param.scan_freq = cpu_to_le32(BRCMF_PNO_TIME);

	err = brcmf_fil_iovar_data_set(ifp, "pfn_set", &pfn_param,
				       sizeof(pfn_param));
	if (err) {
		brcmf_err("pfn_set failed, err=%d\n", err);
		return err;
	}

	/* Find out if mac randomization should be turned on */
	if (!(request->flags & NL80211_SCAN_FLAG_RANDOM_ADDR))
		return 0;

	pfn_mac.version = BRCMF_PFN_MACADDR_CFG_VER;
	pfn_mac.flags = BRCMF_PFN_MAC_OUI_ONLY | BRCMF_PFN_SET_MAC_UNASSOC;

	memcpy(pfn_mac.mac, request->mac_addr, ETH_ALEN);
	mac_mask = request->mac_addr_mask;
	for (i = 0; i < ETH_ALEN; i++) {
		pfn_mac.mac[i] &= mac_mask[i];
		pfn_mac.mac[i] |= get_random_int() & ~(mac_mask[i]);
	}
	/* Clear multi bit */
	pfn_mac.mac[0] &= 0xFE;
	/* Set locally administered */
	pfn_mac.mac[0] |= 0x02;

	err = brcmf_fil_iovar_data_set(ifp, "pfn_macaddr", &pfn_mac,
				       sizeof(pfn_mac));
	if (err)
		brcmf_err("pfn_macaddr failed, err=%d\n", err);

	return err;
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

	brcmf_dbg(SCAN, "Enter n_match_sets:%d n_ssids:%d\n",
		  request->n_match_sets, request->n_ssids);
	if (test_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status)) {
		brcmf_err("Scanning already: status (%lu)\n", cfg->scan_status);
		return -EAGAIN;
	}
	if (test_bit(BRCMF_SCAN_STATUS_SUPPRESS, &cfg->scan_status)) {
		brcmf_err("Scanning suppressed: status (%lu)\n",
			  cfg->scan_status);
		return -EAGAIN;
	}

	if (!request->n_ssids || !request->n_match_sets) {
		brcmf_dbg(SCAN, "Invalid sched scan req!! n_ssids:%d\n",
			  request->n_ssids);
		return -EINVAL;
	}

	if (request->n_ssids > 0) {
		for (i = 0; i < request->n_ssids; i++) {
			/* Active scan req for ssids */
			brcmf_dbg(SCAN, ">>> Active scan req for ssid (%s)\n",
				  request->ssids[i].ssid);

			/* match_set ssids is a supert set of n_ssid list,
			 * so we need not add these set separately.
			 */
		}
	}

	if (request->n_match_sets > 0) {
		/* clean up everything */
		ret = brcmf_dev_pno_clean(ndev);
		if  (ret < 0) {
			brcmf_err("failed error=%d\n", ret);
			return ret;
		}

		/* configure pno */
		if (brcmf_dev_pno_config(ifp, request))
			return -EINVAL;

		/* configure each match set */
		for (i = 0; i < request->n_match_sets; i++) {
			struct cfg80211_ssid *ssid;
			u32 ssid_len;

			ssid = &request->match_sets[i].ssid;
			ssid_len = ssid->ssid_len;

			if (!ssid_len) {
				brcmf_err("skip broadcast ssid\n");
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
			brcmf_dbg(SCAN, ">>> PNO filter %s for ssid (%s)\n",
				  ret == 0 ? "set" : "failed", ssid->ssid);
		}
		/* Enable the PNO */
		if (brcmf_fil_iovar_int_set(ifp, "pfn", 1) < 0) {
			brcmf_err("PNO enable failed!! ret=%d\n", ret);
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

	brcmf_dbg(SCAN, "enter\n");
	brcmf_dev_pno_clean(ndev);
	if (cfg->sched_escan)
		brcmf_notify_escan_complete(cfg, netdev_priv(ndev), true, true);
	return 0;
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

static s32 brcmf_config_wowl_pattern(struct brcmf_if *ifp, u8 cmd[4],
				     u8 *pattern, u32 patternsize, u8 *mask,
				     u32 packet_offset)
{
	struct brcmf_fil_wowl_pattern_le *filter;
	u32 masksize;
	u32 patternoffset;
	u8 *buf;
	u32 bufsize;
	s32 ret;

	masksize = (patternsize + 7) / 8;
	patternoffset = sizeof(*filter) - sizeof(filter->cmd) + masksize;

	bufsize = sizeof(*filter) + patternsize + masksize;
	buf = kzalloc(bufsize, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	filter = (struct brcmf_fil_wowl_pattern_le *)buf;

	memcpy(filter->cmd, cmd, 4);
	filter->masksize = cpu_to_le32(masksize);
	filter->offset = cpu_to_le32(packet_offset);
	filter->patternoffset = cpu_to_le32(patternoffset);
	filter->patternsize = cpu_to_le32(patternsize);
	filter->type = cpu_to_le32(BRCMF_WOWL_PATTERN_TYPE_BITMAP);

	if ((mask) && (masksize))
		memcpy(buf + sizeof(*filter), mask, masksize);
	if ((pattern) && (patternsize))
		memcpy(buf + sizeof(*filter) + masksize, pattern, patternsize);

	ret = brcmf_fil_iovar_data_set(ifp, "wowl_pattern", buf, bufsize);

	kfree(buf);
	return ret;
}

static s32
brcmf_wowl_nd_results(struct brcmf_if *ifp, const struct brcmf_event_msg *e,
		      void *data)
{
	struct brcmf_cfg80211_info *cfg = ifp->drvr->config;
	struct brcmf_pno_scanresults_le *pfn_result;
	struct brcmf_pno_net_info_le *netinfo;

	brcmf_dbg(SCAN, "Enter\n");

	pfn_result = (struct brcmf_pno_scanresults_le *)data;

	if (e->event_code == BRCMF_E_PFN_NET_LOST) {
		brcmf_dbg(SCAN, "PFN NET LOST event. Ignore\n");
		return 0;
	}

	if (le32_to_cpu(pfn_result->count) < 1) {
		brcmf_err("Invalid result count, expected 1 (%d)\n",
			  le32_to_cpu(pfn_result->count));
		return -EINVAL;
	}

	data += sizeof(struct brcmf_pno_scanresults_le);
	netinfo = (struct brcmf_pno_net_info_le *)data;
	memcpy(cfg->wowl.nd->ssid.ssid, netinfo->SSID, netinfo->SSID_len);
	cfg->wowl.nd->ssid.ssid_len = netinfo->SSID_len;
	cfg->wowl.nd->n_channels = 1;
	cfg->wowl.nd->channels[0] =
		ieee80211_channel_to_frequency(netinfo->channel,
			netinfo->channel <= CH_MAX_2G_CHANNEL ?
					NL80211_BAND_2GHZ : NL80211_BAND_5GHZ);
	cfg->wowl.nd_info->n_matches = 1;
	cfg->wowl.nd_info->matches[0] = cfg->wowl.nd;

	/* Inform (the resume task) that the net detect information was recvd */
	cfg->wowl.nd_data_completed = true;
	wake_up(&cfg->wowl.nd_data_wait);

	return 0;
}

#ifdef CONFIG_PM

static void brcmf_report_wowl_wakeind(struct wiphy *wiphy, struct brcmf_if *ifp)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_wowl_wakeind_le wake_ind_le;
	struct cfg80211_wowlan_wakeup wakeup_data;
	struct cfg80211_wowlan_wakeup *wakeup;
	u32 wakeind;
	s32 err;
	int timeout;

	err = brcmf_fil_iovar_data_get(ifp, "wowl_wakeind", &wake_ind_le,
				       sizeof(wake_ind_le));
	if (err) {
		brcmf_err("Get wowl_wakeind failed, err = %d\n", err);
		return;
	}

	wakeind = le32_to_cpu(wake_ind_le.ucode_wakeind);
	if (wakeind & (BRCMF_WOWL_MAGIC | BRCMF_WOWL_DIS | BRCMF_WOWL_BCN |
		       BRCMF_WOWL_RETR | BRCMF_WOWL_NET |
		       BRCMF_WOWL_PFN_FOUND)) {
		wakeup = &wakeup_data;
		memset(&wakeup_data, 0, sizeof(wakeup_data));
		wakeup_data.pattern_idx = -1;

		if (wakeind & BRCMF_WOWL_MAGIC) {
			brcmf_dbg(INFO, "WOWL Wake indicator: BRCMF_WOWL_MAGIC\n");
			wakeup_data.magic_pkt = true;
		}
		if (wakeind & BRCMF_WOWL_DIS) {
			brcmf_dbg(INFO, "WOWL Wake indicator: BRCMF_WOWL_DIS\n");
			wakeup_data.disconnect = true;
		}
		if (wakeind & BRCMF_WOWL_BCN) {
			brcmf_dbg(INFO, "WOWL Wake indicator: BRCMF_WOWL_BCN\n");
			wakeup_data.disconnect = true;
		}
		if (wakeind & BRCMF_WOWL_RETR) {
			brcmf_dbg(INFO, "WOWL Wake indicator: BRCMF_WOWL_RETR\n");
			wakeup_data.disconnect = true;
		}
		if (wakeind & BRCMF_WOWL_NET) {
			brcmf_dbg(INFO, "WOWL Wake indicator: BRCMF_WOWL_NET\n");
			/* For now always map to pattern 0, no API to get
			 * correct information available at the moment.
			 */
			wakeup_data.pattern_idx = 0;
		}
		if (wakeind & BRCMF_WOWL_PFN_FOUND) {
			brcmf_dbg(INFO, "WOWL Wake indicator: BRCMF_WOWL_PFN_FOUND\n");
			timeout = wait_event_timeout(cfg->wowl.nd_data_wait,
				cfg->wowl.nd_data_completed,
				BRCMF_ND_INFO_TIMEOUT);
			if (!timeout)
				brcmf_err("No result for wowl net detect\n");
			else
				wakeup_data.net_detect = cfg->wowl.nd_info;
		}
	} else {
		wakeup = NULL;
	}
	cfg80211_report_wowlan_wakeup(&ifp->vif->wdev, wakeup, GFP_KERNEL);
}

#else

static void brcmf_report_wowl_wakeind(struct wiphy *wiphy, struct brcmf_if *ifp)
{
}

#endif /* CONFIG_PM */

static s32 brcmf_cfg80211_resume(struct wiphy *wiphy)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct net_device *ndev = cfg_to_ndev(cfg);
	struct brcmf_if *ifp = netdev_priv(ndev);

	brcmf_dbg(TRACE, "Enter\n");

	if (cfg->wowl.active) {
		brcmf_report_wowl_wakeind(wiphy, ifp);
		brcmf_fil_iovar_int_set(ifp, "wowl_clear", 0);
		brcmf_config_wowl_pattern(ifp, "clr", NULL, 0, NULL, 0);
		brcmf_configure_arp_offload(ifp, true);
		brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_PM,
				      cfg->wowl.pre_pmmode);
		cfg->wowl.active = false;
		if (cfg->wowl.nd_enabled) {
			brcmf_cfg80211_sched_scan_stop(cfg->wiphy, ifp->ndev);
			brcmf_fweh_unregister(cfg->pub, BRCMF_E_PFN_NET_FOUND);
			brcmf_fweh_register(cfg->pub, BRCMF_E_PFN_NET_FOUND,
					    brcmf_notify_sched_scan_results);
			cfg->wowl.nd_enabled = false;
		}
	}
	return 0;
}

static void brcmf_configure_wowl(struct brcmf_cfg80211_info *cfg,
				 struct brcmf_if *ifp,
				 struct cfg80211_wowlan *wowl)
{
	u32 wowl_config;
	u32 i;

	brcmf_dbg(TRACE, "Suspend, wowl config.\n");

	brcmf_configure_arp_offload(ifp, false);
	brcmf_fil_cmd_int_get(ifp, BRCMF_C_GET_PM, &cfg->wowl.pre_pmmode);
	brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_PM, PM_MAX);

	wowl_config = 0;
	if (wowl->disconnect)
		wowl_config = BRCMF_WOWL_DIS | BRCMF_WOWL_BCN | BRCMF_WOWL_RETR;
	if (wowl->magic_pkt)
		wowl_config |= BRCMF_WOWL_MAGIC;
	if ((wowl->patterns) && (wowl->n_patterns)) {
		wowl_config |= BRCMF_WOWL_NET;
		for (i = 0; i < wowl->n_patterns; i++) {
			brcmf_config_wowl_pattern(ifp, "add",
				(u8 *)wowl->patterns[i].pattern,
				wowl->patterns[i].pattern_len,
				(u8 *)wowl->patterns[i].mask,
				wowl->patterns[i].pkt_offset);
		}
	}
	if (wowl->nd_config) {
		brcmf_cfg80211_sched_scan_start(cfg->wiphy, ifp->ndev,
						wowl->nd_config);
		wowl_config |= BRCMF_WOWL_PFN_FOUND;

		cfg->wowl.nd_data_completed = false;
		cfg->wowl.nd_enabled = true;
		/* Now reroute the event for PFN to the wowl function. */
		brcmf_fweh_unregister(cfg->pub, BRCMF_E_PFN_NET_FOUND);
		brcmf_fweh_register(cfg->pub, BRCMF_E_PFN_NET_FOUND,
				    brcmf_wowl_nd_results);
	}
	if (!test_bit(BRCMF_VIF_STATUS_CONNECTED, &ifp->vif->sme_state))
		wowl_config |= BRCMF_WOWL_UNASSOC;

	brcmf_fil_iovar_data_set(ifp, "wowl_wakeind", "clear", strlen("clear"));
	brcmf_fil_iovar_int_set(ifp, "wowl", wowl_config);
	brcmf_fil_iovar_int_set(ifp, "wowl_activate", 1);
	brcmf_bus_wowl_config(cfg->pub->bus_if, true);
	cfg->wowl.active = true;
}

static s32 brcmf_cfg80211_suspend(struct wiphy *wiphy,
				  struct cfg80211_wowlan *wowl)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct net_device *ndev = cfg_to_ndev(cfg);
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_vif *vif;

	brcmf_dbg(TRACE, "Enter\n");

	/* if the primary net_device is not READY there is nothing
	 * we can do but pray resume goes smoothly.
	 */
	if (!check_vif_up(ifp->vif))
		goto exit;

	/* Stop scheduled scan */
	if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_PNO))
		brcmf_cfg80211_sched_scan_stop(wiphy, ndev);

	/* end any scanning */
	if (test_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status))
		brcmf_abort_scanning(cfg);

	if (wowl == NULL) {
		brcmf_bus_wowl_config(cfg->pub->bus_if, false);
		list_for_each_entry(vif, &cfg->vif_list, list) {
			if (!test_bit(BRCMF_VIF_STATUS_READY, &vif->sme_state))
				continue;
			/* While going to suspend if associated with AP
			 * disassociate from AP to save power while system is
			 * in suspended state
			 */
			brcmf_link_down(vif, WLAN_REASON_UNSPECIFIED);
			/* Make sure WPA_Supplicant receives all the event
			 * generated due to DISASSOC call to the fw to keep
			 * the state fw and WPA_Supplicant state consistent
			 */
			brcmf_delay(500);
		}
		/* Configure MPC */
		brcmf_set_mpc(ifp, 1);

	} else {
		/* Configure WOWL paramaters */
		brcmf_configure_wowl(cfg, ifp, wowl);
	}

exit:
	brcmf_dbg(TRACE, "Exit\n");
	/* clear any scanning activity */
	cfg->scan_status = 0;
	return 0;
}

static __used s32
brcmf_update_pmklist(struct brcmf_cfg80211_info *cfg, struct brcmf_if *ifp)
{
	struct brcmf_pmk_list_le *pmk_list;
	int i;
	u32 npmk;
	s32 err;

	pmk_list = &cfg->pmk_list;
	npmk = le32_to_cpu(pmk_list->npmk);

	brcmf_dbg(CONN, "No of elements %d\n", npmk);
	for (i = 0; i < npmk; i++)
		brcmf_dbg(CONN, "PMK[%d]: %pM\n", i, &pmk_list->pmk[i].bssid);

	err = brcmf_fil_iovar_data_set(ifp, "pmkid_info", pmk_list,
				       sizeof(*pmk_list));

	return err;
}

static s32
brcmf_cfg80211_set_pmksa(struct wiphy *wiphy, struct net_device *ndev,
			 struct cfg80211_pmksa *pmksa)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pmksa *pmk = &cfg->pmk_list.pmk[0];
	s32 err;
	u32 npmk, i;

	brcmf_dbg(TRACE, "Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	npmk = le32_to_cpu(cfg->pmk_list.npmk);
	for (i = 0; i < npmk; i++)
		if (!memcmp(pmksa->bssid, pmk[i].bssid, ETH_ALEN))
			break;
	if (i < BRCMF_MAXPMKID) {
		memcpy(pmk[i].bssid, pmksa->bssid, ETH_ALEN);
		memcpy(pmk[i].pmkid, pmksa->pmkid, WLAN_PMKID_LEN);
		if (i == npmk) {
			npmk++;
			cfg->pmk_list.npmk = cpu_to_le32(npmk);
		}
	} else {
		brcmf_err("Too many PMKSA entries cached %d\n", npmk);
		return -EINVAL;
	}

	brcmf_dbg(CONN, "set_pmksa - PMK bssid: %pM =\n", pmk[npmk].bssid);
	for (i = 0; i < WLAN_PMKID_LEN; i += 4)
		brcmf_dbg(CONN, "%02x %02x %02x %02x\n", pmk[npmk].pmkid[i],
			  pmk[npmk].pmkid[i + 1], pmk[npmk].pmkid[i + 2],
			  pmk[npmk].pmkid[i + 3]);

	err = brcmf_update_pmklist(cfg, ifp);

	brcmf_dbg(TRACE, "Exit\n");
	return err;
}

static s32
brcmf_cfg80211_del_pmksa(struct wiphy *wiphy, struct net_device *ndev,
			 struct cfg80211_pmksa *pmksa)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pmksa *pmk = &cfg->pmk_list.pmk[0];
	s32 err;
	u32 npmk, i;

	brcmf_dbg(TRACE, "Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	brcmf_dbg(CONN, "del_pmksa - PMK bssid = %pM\n", &pmksa->bssid);

	npmk = le32_to_cpu(cfg->pmk_list.npmk);
	for (i = 0; i < npmk; i++)
		if (!memcmp(&pmksa->bssid, &pmk[i].bssid, ETH_ALEN))
			break;

	if ((npmk > 0) && (i < npmk)) {
		for (; i < (npmk - 1); i++) {
			memcpy(&pmk[i].bssid, &pmk[i + 1].bssid, ETH_ALEN);
			memcpy(&pmk[i].pmkid, &pmk[i + 1].pmkid,
			       WLAN_PMKID_LEN);
		}
		memset(&pmk[i], 0, sizeof(*pmk));
		cfg->pmk_list.npmk = cpu_to_le32(npmk - 1);
	} else {
		brcmf_err("Cache entry not found\n");
		return -EINVAL;
	}

	err = brcmf_update_pmklist(cfg, ifp);

	brcmf_dbg(TRACE, "Exit\n");
	return err;

}

static s32
brcmf_cfg80211_flush_pmksa(struct wiphy *wiphy, struct net_device *ndev)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err;

	brcmf_dbg(TRACE, "Enter\n");
	if (!check_vif_up(ifp->vif))
		return -EIO;

	memset(&cfg->pmk_list, 0, sizeof(cfg->pmk_list));
	err = brcmf_update_pmklist(cfg, ifp);

	brcmf_dbg(TRACE, "Exit\n");
	return err;

}

static s32 brcmf_configure_opensecurity(struct brcmf_if *ifp)
{
	s32 err;

	/* set auth */
	err = brcmf_fil_bsscfg_int_set(ifp, "auth", 0);
	if (err < 0) {
		brcmf_err("auth error %d\n", err);
		return err;
	}
	/* set wsec */
	err = brcmf_fil_bsscfg_int_set(ifp, "wsec", 0);
	if (err < 0) {
		brcmf_err("wsec error %d\n", err);
		return err;
	}
	/* set upper-layer auth */
	err = brcmf_fil_bsscfg_int_set(ifp, "wpa_auth", WPA_AUTH_NONE);
	if (err < 0) {
		brcmf_err("wpa_auth error %d\n", err);
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
brcmf_configure_wpaie(struct brcmf_if *ifp,
		      const struct brcmf_vs_tlv *wpa_ie,
		      bool is_rsn_ie)
{
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

	brcmf_dbg(TRACE, "Enter\n");
	if (wpa_ie == NULL)
		goto exit;

	len = wpa_ie->len + TLV_HDR_LEN;
	data = (u8 *)wpa_ie;
	offset = TLV_HDR_LEN;
	if (!is_rsn_ie)
		offset += VS_IE_FIXED_HDR_LEN;
	else
		offset += WPA_IE_VERSION_LEN;

	/* check for multicast cipher suite */
	if (offset + WPA_IE_MIN_OUI_LEN > len) {
		err = -EINVAL;
		brcmf_err("no multicast cipher suite\n");
		goto exit;
	}

	if (!brcmf_valid_wpa_oui(&data[offset], is_rsn_ie)) {
		err = -EINVAL;
		brcmf_err("ivalid OUI\n");
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
		brcmf_err("Invalid multi cast cipher info\n");
		goto exit;
	}

	offset++;
	/* walk thru unicast cipher list and pick up what we recognize */
	count = data[offset] + (data[offset + 1] << 8);
	offset += WPA_IE_SUITE_COUNT_LEN;
	/* Check for unicast suite(s) */
	if (offset + (WPA_IE_MIN_OUI_LEN * count) > len) {
		err = -EINVAL;
		brcmf_err("no unicast cipher suite\n");
		goto exit;
	}
	for (i = 0; i < count; i++) {
		if (!brcmf_valid_wpa_oui(&data[offset], is_rsn_ie)) {
			err = -EINVAL;
			brcmf_err("ivalid OUI\n");
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
			brcmf_err("Ivalid unicast security info\n");
		}
		offset++;
	}
	/* walk thru auth management suite list and pick up what we recognize */
	count = data[offset] + (data[offset + 1] << 8);
	offset += WPA_IE_SUITE_COUNT_LEN;
	/* Check for auth key management suite(s) */
	if (offset + (WPA_IE_MIN_OUI_LEN * count) > len) {
		err = -EINVAL;
		brcmf_err("no auth key mgmt suite\n");
		goto exit;
	}
	for (i = 0; i < count; i++) {
		if (!brcmf_valid_wpa_oui(&data[offset], is_rsn_ie)) {
			err = -EINVAL;
			brcmf_err("ivalid OUI\n");
			goto exit;
		}
		offset += TLV_OUI_LEN;
		switch (data[offset]) {
		case RSN_AKM_NONE:
			brcmf_dbg(TRACE, "RSN_AKM_NONE\n");
			wpa_auth |= WPA_AUTH_NONE;
			break;
		case RSN_AKM_UNSPECIFIED:
			brcmf_dbg(TRACE, "RSN_AKM_UNSPECIFIED\n");
			is_rsn_ie ? (wpa_auth |= WPA2_AUTH_UNSPECIFIED) :
				    (wpa_auth |= WPA_AUTH_UNSPECIFIED);
			break;
		case RSN_AKM_PSK:
			brcmf_dbg(TRACE, "RSN_AKM_PSK\n");
			is_rsn_ie ? (wpa_auth |= WPA2_AUTH_PSK) :
				    (wpa_auth |= WPA_AUTH_PSK);
			break;
		default:
			brcmf_err("Ivalid key mgmt info\n");
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
			brcmf_err("wme_bss_disable error %d\n", err);
			goto exit;
		}
	}
	/* FOR WPS , set SES_OW_ENABLED */
	wsec = (pval | gval | SES_OW_ENABLED);

	/* set auth */
	err = brcmf_fil_bsscfg_int_set(ifp, "auth", auth);
	if (err < 0) {
		brcmf_err("auth error %d\n", err);
		goto exit;
	}
	/* set wsec */
	err = brcmf_fil_bsscfg_int_set(ifp, "wsec", wsec);
	if (err < 0) {
		brcmf_err("wsec error %d\n", err);
		goto exit;
	}
	/* set upper-layer auth */
	err = brcmf_fil_bsscfg_int_set(ifp, "wpa_auth", wpa_auth);
	if (err < 0) {
		brcmf_err("wpa_auth error %d\n", err);
		goto exit;
	}

exit:
	return err;
}

static s32
brcmf_parse_vndr_ies(const u8 *vndr_ie_buf, u32 vndr_ie_len,
		     struct parsed_vndr_ies *vndr_ies)
{
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
			brcmf_err("invalid vndr ie. length is too small %d\n",
				  vndrie->len);
			goto next;
		}
		/* if wpa or wme ie, do not add ie */
		if (!memcmp(vndrie->oui, (u8 *)WPA_OUI, TLV_OUI_LEN) &&
		    ((vndrie->oui_type == WPA_OUI_TYPE) ||
		    (vndrie->oui_type == WME_OUI_TYPE))) {
			brcmf_dbg(TRACE, "Found WPA/WME oui. Do not add it\n");
			goto next;
		}

		parsed_info = &vndr_ies->ie_info[vndr_ies->count];

		/* save vndr ie information */
		parsed_info->ie_ptr = (char *)vndrie;
		parsed_info->ie_len = vndrie->len + TLV_HDR_LEN;
		memcpy(&parsed_info->vndrie, vndrie, sizeof(*vndrie));

		vndr_ies->count++;

		brcmf_dbg(TRACE, "** OUI %02x %02x %02x, type 0x%02x\n",
			  parsed_info->vndrie.oui[0],
			  parsed_info->vndrie.oui[1],
			  parsed_info->vndrie.oui[2],
			  parsed_info->vndrie.oui_type);

		if (vndr_ies->count >= VNDR_IE_PARSE_LIMIT)
			break;
next:
		remaining_len -= (ie->len + TLV_HDR_LEN);
		if (remaining_len <= TLV_HDR_LEN)
			ie = NULL;
		else
			ie = (struct brcmf_tlv *)(((u8 *)ie) + ie->len +
				TLV_HDR_LEN);
	}
	return 0;
}

static u32
brcmf_vndr_ie(u8 *iebuf, s32 pktflag, u8 *ie_ptr, u32 ie_len, s8 *add_del_cmd)
{

	strncpy(iebuf, add_del_cmd, VNDR_IE_CMD_LEN - 1);
	iebuf[VNDR_IE_CMD_LEN - 1] = '\0';

	put_unaligned_le32(1, &iebuf[VNDR_IE_COUNT_OFFSET]);

	put_unaligned_le32(pktflag, &iebuf[VNDR_IE_PKTFLAG_OFFSET]);

	memcpy(&iebuf[VNDR_IE_VSIE_OFFSET], ie_ptr, ie_len);

	return ie_len + VNDR_IE_HDR_SIZE;
}

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

	brcmf_dbg(TRACE, "bsscfgidx %d, pktflag : 0x%02X\n", ifp->bsscfgidx,
		  pktflag);
	iovar_ie_buf = kzalloc(WL_EXTRA_BUF_MAX, GFP_KERNEL);
	if (!iovar_ie_buf)
		return -ENOMEM;
	curr_ie_buf = iovar_ie_buf;
	switch (pktflag) {
	case BRCMF_VNDR_IE_PRBREQ_FLAG:
		mgmt_ie_buf = saved_ie->probe_req_ie;
		mgmt_ie_len = &saved_ie->probe_req_ie_len;
		mgmt_ie_buf_len = sizeof(saved_ie->probe_req_ie);
		break;
	case BRCMF_VNDR_IE_PRBRSP_FLAG:
		mgmt_ie_buf = saved_ie->probe_res_ie;
		mgmt_ie_len = &saved_ie->probe_res_ie_len;
		mgmt_ie_buf_len = sizeof(saved_ie->probe_res_ie);
		break;
	case BRCMF_VNDR_IE_BEACON_FLAG:
		mgmt_ie_buf = saved_ie->beacon_ie;
		mgmt_ie_len = &saved_ie->beacon_ie_len;
		mgmt_ie_buf_len = sizeof(saved_ie->beacon_ie);
		break;
	case BRCMF_VNDR_IE_ASSOCREQ_FLAG:
		mgmt_ie_buf = saved_ie->assoc_req_ie;
		mgmt_ie_len = &saved_ie->assoc_req_ie_len;
		mgmt_ie_buf_len = sizeof(saved_ie->assoc_req_ie);
		break;
	default:
		err = -EPERM;
		brcmf_err("not suitable type\n");
		goto exit;
	}

	if (vndr_ie_len > mgmt_ie_buf_len) {
		err = -ENOMEM;
		brcmf_err("extra IE size too big\n");
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

	if (mgmt_ie_buf && *mgmt_ie_len) {
		if (parsed_ie_buf_len && (parsed_ie_buf_len == *mgmt_ie_len) &&
		    (memcmp(mgmt_ie_buf, curr_ie_buf,
			    parsed_ie_buf_len) == 0)) {
			brcmf_dbg(TRACE, "Previous mgmt IE equals to current IE\n");
			goto exit;
		}

		/* parse old vndr_ie */
		brcmf_parse_vndr_ies(mgmt_ie_buf, *mgmt_ie_len, &old_vndr_ies);

		/* make a command to delete old ie */
		for (i = 0; i < old_vndr_ies.count; i++) {
			vndrie_info = &old_vndr_ies.ie_info[i];

			brcmf_dbg(TRACE, "DEL ID : %d, Len: %d , OUI:%02x:%02x:%02x\n",
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

			/* verify remained buf size before copy data */
			if (remained_buf_len < (vndrie_info->vndrie.len +
							VNDR_IE_VSIE_OFFSET)) {
				brcmf_err("no space in mgmt_ie_buf: len left %d",
					  remained_buf_len);
				break;
			}
			remained_buf_len -= (vndrie_info->ie_len +
					     VNDR_IE_VSIE_OFFSET);

			brcmf_dbg(TRACE, "ADDED ID : %d, Len: %d, OUI:%02x:%02x:%02x\n",
				  vndrie_info->vndrie.id,
				  vndrie_info->vndrie.len,
				  vndrie_info->vndrie.oui[0],
				  vndrie_info->vndrie.oui[1],
				  vndrie_info->vndrie.oui[2]);

			del_add_ie_buf_len = brcmf_vndr_ie(curr_ie_buf, pktflag,
							   vndrie_info->ie_ptr,
							   vndrie_info->ie_len,
							   "add");

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
			brcmf_err("vndr ie set error : %d\n", err);
	}

exit:
	kfree(iovar_ie_buf);
	return err;
}

s32 brcmf_vif_clear_mgmt_ies(struct brcmf_cfg80211_vif *vif)
{
	s32 pktflags[] = {
		BRCMF_VNDR_IE_PRBREQ_FLAG,
		BRCMF_VNDR_IE_PRBRSP_FLAG,
		BRCMF_VNDR_IE_BEACON_FLAG
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(pktflags); i++)
		brcmf_vif_set_mgmt_ie(vif, pktflags[i], NULL, 0);

	memset(&vif->saved_ie, 0, sizeof(vif->saved_ie));
	return 0;
}

static s32
brcmf_config_ap_mgmt_ie(struct brcmf_cfg80211_vif *vif,
			struct cfg80211_beacon_data *beacon)
{
	s32 err;

	/* Set Beacon IEs to FW */
	err = brcmf_vif_set_mgmt_ie(vif, BRCMF_VNDR_IE_BEACON_FLAG,
				    beacon->tail, beacon->tail_len);
	if (err) {
		brcmf_err("Set Beacon IE Failed\n");
		return err;
	}
	brcmf_dbg(TRACE, "Applied Vndr IEs for Beacon\n");

	/* Set Probe Response IEs to FW */
	err = brcmf_vif_set_mgmt_ie(vif, BRCMF_VNDR_IE_PRBRSP_FLAG,
				    beacon->proberesp_ies,
				    beacon->proberesp_ies_len);
	if (err)
		brcmf_err("Set Probe Resp IE Failed\n");
	else
		brcmf_dbg(TRACE, "Applied Vndr IEs for Probe Resp\n");

	return err;
}

static s32
brcmf_cfg80211_start_ap(struct wiphy *wiphy, struct net_device *ndev,
			struct cfg80211_ap_settings *settings)
{
	s32 ie_offset;
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(ndev);
	const struct brcmf_tlv *ssid_ie;
	const struct brcmf_tlv *country_ie;
	struct brcmf_ssid_le ssid_le;
	s32 err = -EPERM;
	const struct brcmf_tlv *rsn_ie;
	const struct brcmf_vs_tlv *wpa_ie;
	struct brcmf_join_params join_params;
	enum nl80211_iftype dev_role;
	struct brcmf_fil_bss_enable_le bss_enable;
	u16 chanspec;
	bool mbss;
	int is_11d;

	brcmf_dbg(TRACE, "ctrlchn=%d, center=%d, bw=%d, beacon_interval=%d, dtim_period=%d,\n",
		  settings->chandef.chan->hw_value,
		  settings->chandef.center_freq1, settings->chandef.width,
		  settings->beacon_interval, settings->dtim_period);
	brcmf_dbg(TRACE, "ssid=%s(%zu), auth_type=%d, inactivity_timeout=%d\n",
		  settings->ssid, settings->ssid_len, settings->auth_type,
		  settings->inactivity_timeout);
	dev_role = ifp->vif->wdev.iftype;
	mbss = ifp->vif->mbss;

	/* store current 11d setting */
	brcmf_fil_cmd_int_get(ifp, BRCMF_C_GET_REGULATORY, &ifp->vif->is_11d);
	country_ie = brcmf_parse_tlvs((u8 *)settings->beacon.tail,
				      settings->beacon.tail_len,
				      WLAN_EID_COUNTRY);
	is_11d = country_ie ? 1 : 0;

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
		brcmf_dbg(TRACE, "SSID is (%s) in Head\n", ssid_le.SSID);
	} else {
		memcpy(ssid_le.SSID, settings->ssid, settings->ssid_len);
		ssid_le.SSID_len = cpu_to_le32((u32)settings->ssid_len);
	}

	if (!mbss) {
		brcmf_set_mpc(ifp, 0);
		brcmf_configure_arp_offload(ifp, false);
	}

	/* find the RSN_IE */
	rsn_ie = brcmf_parse_tlvs((u8 *)settings->beacon.tail,
				  settings->beacon.tail_len, WLAN_EID_RSN);

	/* find the WPA_IE */
	wpa_ie = brcmf_find_wpaie((u8 *)settings->beacon.tail,
				  settings->beacon.tail_len);

	if ((wpa_ie != NULL || rsn_ie != NULL)) {
		brcmf_dbg(TRACE, "WPA(2) IE is found\n");
		if (wpa_ie != NULL) {
			/* WPA IE */
			err = brcmf_configure_wpaie(ifp, wpa_ie, false);
			if (err < 0)
				goto exit;
		} else {
			struct brcmf_vs_tlv *tmp_ie;

			tmp_ie = (struct brcmf_vs_tlv *)rsn_ie;

			/* RSN IE */
			err = brcmf_configure_wpaie(ifp, tmp_ie, true);
			if (err < 0)
				goto exit;
		}
	} else {
		brcmf_dbg(TRACE, "No WPA(2) IEs found\n");
		brcmf_configure_opensecurity(ifp);
	}

	brcmf_config_ap_mgmt_ie(ifp->vif, &settings->beacon);

	if (!mbss) {
		chanspec = chandef_to_chanspec(&cfg->d11inf,
					       &settings->chandef);
		err = brcmf_fil_iovar_int_set(ifp, "chanspec", chanspec);
		if (err < 0) {
			brcmf_err("Set Channel failed: chspec=%d, %d\n",
				  chanspec, err);
			goto exit;
		}

		if (is_11d != ifp->vif->is_11d) {
			err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_REGULATORY,
						    is_11d);
			if (err < 0) {
				brcmf_err("Regulatory Set Error, %d\n", err);
				goto exit;
			}
		}
		if (settings->beacon_interval) {
			err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_BCNPRD,
						    settings->beacon_interval);
			if (err < 0) {
				brcmf_err("Beacon Interval Set Error, %d\n",
					  err);
				goto exit;
			}
		}
		if (settings->dtim_period) {
			err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_DTIMPRD,
						    settings->dtim_period);
			if (err < 0) {
				brcmf_err("DTIM Interval Set Error, %d\n", err);
				goto exit;
			}
		}

		if ((dev_role == NL80211_IFTYPE_AP) &&
		    ((ifp->ifidx == 0) ||
		     !brcmf_feat_is_enabled(ifp, BRCMF_FEAT_RSDB))) {
			err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_DOWN, 1);
			if (err < 0) {
				brcmf_err("BRCMF_C_DOWN error %d\n", err);
				goto exit;
			}
			brcmf_fil_iovar_int_set(ifp, "apsta", 0);
		}

		err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_INFRA, 1);
		if (err < 0) {
			brcmf_err("SET INFRA error %d\n", err);
			goto exit;
		}
	} else if (WARN_ON(is_11d != ifp->vif->is_11d)) {
		/* Multiple-BSS should use same 11d configuration */
		err = -EINVAL;
		goto exit;
	}
	if (dev_role == NL80211_IFTYPE_AP) {
		if ((brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MBSS)) && (!mbss))
			brcmf_fil_iovar_int_set(ifp, "mbss", 1);

		err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_AP, 1);
		if (err < 0) {
			brcmf_err("setting AP mode failed %d\n", err);
			goto exit;
		}
		err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_UP, 1);
		if (err < 0) {
			brcmf_err("BRCMF_C_UP error (%d)\n", err);
			goto exit;
		}
		/* On DOWN the firmware removes the WEP keys, reconfigure
		 * them if they were set.
		 */
		brcmf_cfg80211_reconfigure_wep(ifp);

		memset(&join_params, 0, sizeof(join_params));
		/* join parameters starts with ssid */
		memcpy(&join_params.ssid_le, &ssid_le, sizeof(ssid_le));
		/* create softap */
		err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_SSID,
					     &join_params, sizeof(join_params));
		if (err < 0) {
			brcmf_err("SET SSID error (%d)\n", err);
			goto exit;
		}
		brcmf_dbg(TRACE, "AP mode configuration complete\n");
	} else {
		err = brcmf_fil_bsscfg_data_set(ifp, "ssid", &ssid_le,
						sizeof(ssid_le));
		if (err < 0) {
			brcmf_err("setting ssid failed %d\n", err);
			goto exit;
		}
		bss_enable.bsscfgidx = cpu_to_le32(ifp->bsscfgidx);
		bss_enable.enable = cpu_to_le32(1);
		err = brcmf_fil_iovar_data_set(ifp, "bss", &bss_enable,
					       sizeof(bss_enable));
		if (err < 0) {
			brcmf_err("bss_enable config failed %d\n", err);
			goto exit;
		}

		brcmf_dbg(TRACE, "GO mode configuration complete\n");
	}
	set_bit(BRCMF_VIF_STATUS_AP_CREATED, &ifp->vif->sme_state);
	brcmf_net_setcarrier(ifp, true);

exit:
	if ((err) && (!mbss)) {
		brcmf_set_mpc(ifp, 1);
		brcmf_configure_arp_offload(ifp, true);
	}
	return err;
}

static int brcmf_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err;
	struct brcmf_fil_bss_enable_le bss_enable;
	struct brcmf_join_params join_params;

	brcmf_dbg(TRACE, "Enter\n");

	if (ifp->vif->wdev.iftype == NL80211_IFTYPE_AP) {
		/* Due to most likely deauths outstanding we sleep */
		/* first to make sure they get processed by fw. */
		msleep(400);

		if (ifp->vif->mbss) {
			err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_DOWN, 1);
			return err;
		}

		memset(&join_params, 0, sizeof(join_params));
		err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_SSID,
					     &join_params, sizeof(join_params));
		if (err < 0)
			brcmf_err("SET SSID error (%d)\n", err);
		err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_DOWN, 1);
		if (err < 0)
			brcmf_err("BRCMF_C_DOWN error %d\n", err);
		err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_AP, 0);
		if (err < 0)
			brcmf_err("setting AP mode failed %d\n", err);
		err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_INFRA, 0);
		if (err < 0)
			brcmf_err("setting INFRA mode failed %d\n", err);
		if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MBSS))
			brcmf_fil_iovar_int_set(ifp, "mbss", 0);
		err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_REGULATORY,
					    ifp->vif->is_11d);
		if (err < 0)
			brcmf_err("restoring REGULATORY setting failed %d\n",
				  err);
		/* Bring device back up so it can be used again */
		err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_UP, 1);
		if (err < 0)
			brcmf_err("BRCMF_C_UP error %d\n", err);
	} else {
		bss_enable.bsscfgidx = cpu_to_le32(ifp->bsscfgidx);
		bss_enable.enable = cpu_to_le32(0);
		err = brcmf_fil_iovar_data_set(ifp, "bss", &bss_enable,
					       sizeof(bss_enable));
		if (err < 0)
			brcmf_err("bss_enable config failed %d\n", err);
	}
	brcmf_set_mpc(ifp, 1);
	brcmf_configure_arp_offload(ifp, true);
	clear_bit(BRCMF_VIF_STATUS_AP_CREATED, &ifp->vif->sme_state);
	brcmf_net_setcarrier(ifp, false);

	return err;
}

static s32
brcmf_cfg80211_change_beacon(struct wiphy *wiphy, struct net_device *ndev,
			     struct cfg80211_beacon_data *info)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err;

	brcmf_dbg(TRACE, "Enter\n");

	err = brcmf_config_ap_mgmt_ie(ifp->vif, info);

	return err;
}

static int
brcmf_cfg80211_del_station(struct wiphy *wiphy, struct net_device *ndev,
			   struct station_del_parameters *params)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_scb_val_le scbval;
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err;

	if (!params->mac)
		return -EFAULT;

	brcmf_dbg(TRACE, "Enter %pM\n", params->mac);

	if (ifp->vif == cfg->p2p.bss_idx[P2PAPI_BSSCFG_DEVICE].vif)
		ifp = cfg->p2p.bss_idx[P2PAPI_BSSCFG_PRIMARY].vif->ifp;
	if (!check_vif_up(ifp->vif))
		return -EIO;

	memcpy(&scbval.ea, params->mac, ETH_ALEN);
	scbval.val = cpu_to_le32(params->reason_code);
	err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SCB_DEAUTHENTICATE_FOR_REASON,
				     &scbval, sizeof(scbval));
	if (err)
		brcmf_err("SCB_DEAUTHENTICATE_FOR_REASON failed %d\n", err);

	brcmf_dbg(TRACE, "Exit\n");
	return err;
}

static int
brcmf_cfg80211_change_station(struct wiphy *wiphy, struct net_device *ndev,
			      const u8 *mac, struct station_parameters *params)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	s32 err;

	brcmf_dbg(TRACE, "Enter, MAC %pM, mask 0x%04x set 0x%04x\n", mac,
		  params->sta_flags_mask, params->sta_flags_set);

	/* Ignore all 00 MAC */
	if (is_zero_ether_addr(mac))
		return 0;

	if (!(params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED)))
		return 0;

	if (params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED))
		err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_SCB_AUTHORIZE,
					     (void *)mac, ETH_ALEN);
	else
		err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_SCB_DEAUTHORIZE,
					     (void *)mac, ETH_ALEN);
	if (err < 0)
		brcmf_err("Setting SCB (de-)authorize failed, %d\n", err);

	return err;
}

static void
brcmf_cfg80211_mgmt_frame_register(struct wiphy *wiphy,
				   struct wireless_dev *wdev,
				   u16 frame_type, bool reg)
{
	struct brcmf_cfg80211_vif *vif;
	u16 mgmt_type;

	brcmf_dbg(TRACE, "Enter, frame_type %04x, reg=%d\n", frame_type, reg);

	mgmt_type = (frame_type & IEEE80211_FCTL_STYPE) >> 4;
	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	if (reg)
		vif->mgmt_rx_reg |= BIT(mgmt_type);
	else
		vif->mgmt_rx_reg &= ~BIT(mgmt_type);
}


static int
brcmf_cfg80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
		       struct cfg80211_mgmt_tx_params *params, u64 *cookie)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct ieee80211_channel *chan = params->chan;
	const u8 *buf = params->buf;
	size_t len = params->len;
	const struct ieee80211_mgmt *mgmt;
	struct brcmf_cfg80211_vif *vif;
	s32 err = 0;
	s32 ie_offset;
	s32 ie_len;
	struct brcmf_fil_action_frame_le *action_frame;
	struct brcmf_fil_af_params_le *af_params;
	bool ack;
	s32 chan_nr;
	u32 freq;

	brcmf_dbg(TRACE, "Enter\n");

	*cookie = 0;

	mgmt = (const struct ieee80211_mgmt *)buf;

	if (!ieee80211_is_mgmt(mgmt->frame_control)) {
		brcmf_err("Driver only allows MGMT packet type\n");
		return -EPERM;
	}

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);

	if (ieee80211_is_probe_resp(mgmt->frame_control)) {
		/* Right now the only reason to get a probe response */
		/* is for p2p listen response or for p2p GO from     */
		/* wpa_supplicant. Unfortunately the probe is send   */
		/* on primary ndev, while dongle wants it on the p2p */
		/* vif. Since this is only reason for a probe        */
		/* response to be sent, the vif is taken from cfg.   */
		/* If ever desired to send proberesp for non p2p     */
		/* response then data should be checked for          */
		/* "DIRECT-". Note in future supplicant will take    */
		/* dedicated p2p wdev to do this and then this 'hack'*/
		/* is not needed anymore.                            */
		ie_offset =  DOT11_MGMT_HDR_LEN +
			     DOT11_BCN_PRB_FIXED_LEN;
		ie_len = len - ie_offset;
		if (vif == cfg->p2p.bss_idx[P2PAPI_BSSCFG_PRIMARY].vif)
			vif = cfg->p2p.bss_idx[P2PAPI_BSSCFG_DEVICE].vif;
		err = brcmf_vif_set_mgmt_ie(vif,
					    BRCMF_VNDR_IE_PRBRSP_FLAG,
					    &buf[ie_offset],
					    ie_len);
		cfg80211_mgmt_tx_status(wdev, *cookie, buf, len, true,
					GFP_KERNEL);
	} else if (ieee80211_is_action(mgmt->frame_control)) {
		af_params = kzalloc(sizeof(*af_params), GFP_KERNEL);
		if (af_params == NULL) {
			brcmf_err("unable to allocate frame\n");
			err = -ENOMEM;
			goto exit;
		}
		action_frame = &af_params->action_frame;
		/* Add the packet Id */
		action_frame->packet_id = cpu_to_le32(*cookie);
		/* Add BSSID */
		memcpy(&action_frame->da[0], &mgmt->da[0], ETH_ALEN);
		memcpy(&af_params->bssid[0], &mgmt->bssid[0], ETH_ALEN);
		/* Add the length exepted for 802.11 header  */
		action_frame->len = cpu_to_le16(len - DOT11_MGMT_HDR_LEN);
		/* Add the channel. Use the one specified as parameter if any or
		 * the current one (got from the firmware) otherwise
		 */
		if (chan)
			freq = chan->center_freq;
		else
			brcmf_fil_cmd_int_get(vif->ifp, BRCMF_C_GET_CHANNEL,
					      &freq);
		chan_nr = ieee80211_frequency_to_channel(freq);
		af_params->channel = cpu_to_le32(chan_nr);

		memcpy(action_frame->data, &buf[DOT11_MGMT_HDR_LEN],
		       le16_to_cpu(action_frame->len));

		brcmf_dbg(TRACE, "Action frame, cookie=%lld, len=%d, freq=%d\n",
			  *cookie, le16_to_cpu(action_frame->len), freq);

		ack = brcmf_p2p_send_action_frame(cfg, cfg_to_ndev(cfg),
						  af_params);

		cfg80211_mgmt_tx_status(wdev, *cookie, buf, len, ack,
					GFP_KERNEL);
		kfree(af_params);
	} else {
		brcmf_dbg(TRACE, "Unhandled, fc=%04x!!\n", mgmt->frame_control);
		brcmf_dbg_hex_dump(true, buf, len, "payload, len=%Zu\n", len);
	}

exit:
	return err;
}


static int
brcmf_cfg80211_cancel_remain_on_channel(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					u64 cookie)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_cfg80211_vif *vif;
	int err = 0;

	brcmf_dbg(TRACE, "Enter p2p listen cancel\n");

	vif = cfg->p2p.bss_idx[P2PAPI_BSSCFG_DEVICE].vif;
	if (vif == NULL) {
		brcmf_err("No p2p device available for probe response\n");
		err = -ENODEV;
		goto exit;
	}
	brcmf_p2p_cancel_remain_on_channel(vif->ifp);
exit:
	return err;
}

static int brcmf_cfg80211_crit_proto_start(struct wiphy *wiphy,
					   struct wireless_dev *wdev,
					   enum nl80211_crit_proto_id proto,
					   u16 duration)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_cfg80211_vif *vif;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);

	/* only DHCP support for now */
	if (proto != NL80211_CRIT_PROTO_DHCP)
		return -EINVAL;

	/* suppress and abort scanning */
	set_bit(BRCMF_SCAN_STATUS_SUPPRESS, &cfg->scan_status);
	brcmf_abort_scanning(cfg);

	return brcmf_btcoex_set_mode(vif, BRCMF_BTCOEX_DISABLED, duration);
}

static void brcmf_cfg80211_crit_proto_stop(struct wiphy *wiphy,
					   struct wireless_dev *wdev)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_cfg80211_vif *vif;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);

	brcmf_btcoex_set_mode(vif, BRCMF_BTCOEX_ENABLED, 0);
	clear_bit(BRCMF_SCAN_STATUS_SUPPRESS, &cfg->scan_status);
}

static s32
brcmf_notify_tdls_peer_event(struct brcmf_if *ifp,
			     const struct brcmf_event_msg *e, void *data)
{
	switch (e->reason) {
	case BRCMF_E_REASON_TDLS_PEER_DISCOVERED:
		brcmf_dbg(TRACE, "TDLS Peer Discovered\n");
		break;
	case BRCMF_E_REASON_TDLS_PEER_CONNECTED:
		brcmf_dbg(TRACE, "TDLS Peer Connected\n");
		brcmf_proto_add_tdls_peer(ifp->drvr, ifp->ifidx, (u8 *)e->addr);
		break;
	case BRCMF_E_REASON_TDLS_PEER_DISCONNECTED:
		brcmf_dbg(TRACE, "TDLS Peer Disconnected\n");
		brcmf_proto_delete_peer(ifp->drvr, ifp->ifidx, (u8 *)e->addr);
		break;
	}

	return 0;
}

static int brcmf_convert_nl80211_tdls_oper(enum nl80211_tdls_operation oper)
{
	int ret;

	switch (oper) {
	case NL80211_TDLS_DISCOVERY_REQ:
		ret = BRCMF_TDLS_MANUAL_EP_DISCOVERY;
		break;
	case NL80211_TDLS_SETUP:
		ret = BRCMF_TDLS_MANUAL_EP_CREATE;
		break;
	case NL80211_TDLS_TEARDOWN:
		ret = BRCMF_TDLS_MANUAL_EP_DELETE;
		break;
	default:
		brcmf_err("unsupported operation: %d\n", oper);
		ret = -EOPNOTSUPP;
	}
	return ret;
}

static int brcmf_cfg80211_tdls_oper(struct wiphy *wiphy,
				    struct net_device *ndev, const u8 *peer,
				    enum nl80211_tdls_operation oper)
{
	struct brcmf_if *ifp;
	struct brcmf_tdls_iovar_le info;
	int ret = 0;

	ret = brcmf_convert_nl80211_tdls_oper(oper);
	if (ret < 0)
		return ret;

	ifp = netdev_priv(ndev);
	memset(&info, 0, sizeof(info));
	info.mode = (u8)ret;
	if (peer)
		memcpy(info.ea, peer, ETH_ALEN);

	ret = brcmf_fil_iovar_data_set(ifp, "tdls_endpoint",
				       &info, sizeof(info));
	if (ret < 0)
		brcmf_err("tdls_endpoint iovar failed: ret=%d\n", ret);

	return ret;
}

static struct cfg80211_ops wl_cfg80211_ops = {
	.add_virtual_intf = brcmf_cfg80211_add_iface,
	.del_virtual_intf = brcmf_cfg80211_del_iface,
	.change_virtual_intf = brcmf_cfg80211_change_iface,
	.scan = brcmf_cfg80211_scan,
	.set_wiphy_params = brcmf_cfg80211_set_wiphy_params,
	.join_ibss = brcmf_cfg80211_join_ibss,
	.leave_ibss = brcmf_cfg80211_leave_ibss,
	.get_station = brcmf_cfg80211_get_station,
	.dump_station = brcmf_cfg80211_dump_station,
	.set_tx_power = brcmf_cfg80211_set_tx_power,
	.get_tx_power = brcmf_cfg80211_get_tx_power,
	.add_key = brcmf_cfg80211_add_key,
	.del_key = brcmf_cfg80211_del_key,
	.get_key = brcmf_cfg80211_get_key,
	.set_default_key = brcmf_cfg80211_config_default_key,
	.set_default_mgmt_key = brcmf_cfg80211_config_default_mgmt_key,
	.set_power_mgmt = brcmf_cfg80211_set_power_mgmt,
	.connect = brcmf_cfg80211_connect,
	.disconnect = brcmf_cfg80211_disconnect,
	.suspend = brcmf_cfg80211_suspend,
	.resume = brcmf_cfg80211_resume,
	.set_pmksa = brcmf_cfg80211_set_pmksa,
	.del_pmksa = brcmf_cfg80211_del_pmksa,
	.flush_pmksa = brcmf_cfg80211_flush_pmksa,
	.start_ap = brcmf_cfg80211_start_ap,
	.stop_ap = brcmf_cfg80211_stop_ap,
	.change_beacon = brcmf_cfg80211_change_beacon,
	.del_station = brcmf_cfg80211_del_station,
	.change_station = brcmf_cfg80211_change_station,
	.sched_scan_start = brcmf_cfg80211_sched_scan_start,
	.sched_scan_stop = brcmf_cfg80211_sched_scan_stop,
	.mgmt_frame_register = brcmf_cfg80211_mgmt_frame_register,
	.mgmt_tx = brcmf_cfg80211_mgmt_tx,
	.remain_on_channel = brcmf_p2p_remain_on_channel,
	.cancel_remain_on_channel = brcmf_cfg80211_cancel_remain_on_channel,
	.start_p2p_device = brcmf_p2p_start_device,
	.stop_p2p_device = brcmf_p2p_stop_device,
	.crit_proto_start = brcmf_cfg80211_crit_proto_start,
	.crit_proto_stop = brcmf_cfg80211_crit_proto_stop,
	.tdls_oper = brcmf_cfg80211_tdls_oper,
};

struct brcmf_cfg80211_vif *brcmf_alloc_vif(struct brcmf_cfg80211_info *cfg,
					   enum nl80211_iftype type,
					   bool pm_block)
{
	struct brcmf_cfg80211_vif *vif_walk;
	struct brcmf_cfg80211_vif *vif;
	bool mbss;

	brcmf_dbg(TRACE, "allocating virtual interface (size=%zu)\n",
		  sizeof(*vif));
	vif = kzalloc(sizeof(*vif), GFP_KERNEL);
	if (!vif)
		return ERR_PTR(-ENOMEM);

	vif->wdev.wiphy = cfg->wiphy;
	vif->wdev.iftype = type;

	vif->pm_block = pm_block;

	brcmf_init_prof(&vif->profile);

	if (type == NL80211_IFTYPE_AP) {
		mbss = false;
		list_for_each_entry(vif_walk, &cfg->vif_list, list) {
			if (vif_walk->wdev.iftype == NL80211_IFTYPE_AP) {
				mbss = true;
				break;
			}
		}
		vif->mbss = mbss;
	}

	list_add_tail(&vif->list, &cfg->vif_list);
	return vif;
}

void brcmf_free_vif(struct brcmf_cfg80211_vif *vif)
{
	list_del(&vif->list);
	kfree(vif);
}

void brcmf_cfg80211_free_netdev(struct net_device *ndev)
{
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;

	ifp = netdev_priv(ndev);
	vif = ifp->vif;

	if (vif)
		brcmf_free_vif(vif);
	free_netdev(ndev);
}

static bool brcmf_is_linkup(const struct brcmf_event_msg *e)
{
	u32 event = e->event_code;
	u32 status = e->status;

	if (event == BRCMF_E_SET_SSID && status == BRCMF_E_STATUS_SUCCESS) {
		brcmf_dbg(CONN, "Processing set ssid\n");
		return true;
	}

	return false;
}

static bool brcmf_is_linkdown(const struct brcmf_event_msg *e)
{
	u32 event = e->event_code;
	u16 flags = e->flags;

	if ((event == BRCMF_E_DEAUTH) || (event == BRCMF_E_DEAUTH_IND) ||
	    (event == BRCMF_E_DISASSOC_IND) ||
	    ((event == BRCMF_E_LINK) && (!(flags & BRCMF_EVENT_MSG_LINK)))) {
		brcmf_dbg(CONN, "Processing link down\n");
		return true;
	}
	return false;
}

static bool brcmf_is_nonetwork(struct brcmf_cfg80211_info *cfg,
			       const struct brcmf_event_msg *e)
{
	u32 event = e->event_code;
	u32 status = e->status;

	if (event == BRCMF_E_LINK && status == BRCMF_E_STATUS_NO_NETWORKS) {
		brcmf_dbg(CONN, "Processing Link %s & no network found\n",
			  e->flags & BRCMF_EVENT_MSG_LINK ? "up" : "down");
		return true;
	}

	if (event == BRCMF_E_SET_SSID && status != BRCMF_E_STATUS_SUCCESS) {
		brcmf_dbg(CONN, "Processing connecting & no network found\n");
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

static s32 brcmf_get_assoc_ies(struct brcmf_cfg80211_info *cfg,
			       struct brcmf_if *ifp)
{
	struct brcmf_cfg80211_assoc_ielen_le *assoc_info;
	struct brcmf_cfg80211_connect_info *conn_info = cfg_to_conn(cfg);
	u32 req_len;
	u32 resp_len;
	s32 err = 0;

	brcmf_clear_assoc_ies(cfg);

	err = brcmf_fil_iovar_data_get(ifp, "assoc_info",
				       cfg->extra_buf, WL_ASSOC_INFO_MAX);
	if (err) {
		brcmf_err("could not get assoc info (%d)\n", err);
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
			brcmf_err("could not get assoc req (%d)\n", err);
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
			brcmf_err("could not get assoc resp (%d)\n", err);
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
	brcmf_dbg(CONN, "req len (%d) resp len (%d)\n",
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
	struct brcmu_chan ch;
	u32 freq;
	s32 err = 0;
	u8 *buf;

	brcmf_dbg(TRACE, "Enter\n");

	brcmf_get_assoc_ies(cfg, ifp);
	memcpy(profile->bssid, e->addr, ETH_ALEN);
	brcmf_update_bss_info(cfg, ifp);

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
	ch.chspec = le16_to_cpu(bi->chanspec);
	cfg->d11inf.decchspec(&ch);

	if (ch.band == BRCMU_CHAN_BAND_2G)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];

	freq = ieee80211_channel_to_frequency(ch.chnum, band->band);
	notify_channel = ieee80211_get_channel(wiphy, freq);

done:
	kfree(buf);
	cfg80211_roamed(ndev, notify_channel, (u8 *)profile->bssid,
			conn_info->req_ie, conn_info->req_ie_len,
			conn_info->resp_ie, conn_info->resp_ie_len, GFP_KERNEL);
	brcmf_dbg(CONN, "Report roaming result\n");

	set_bit(BRCMF_VIF_STATUS_CONNECTED, &ifp->vif->sme_state);
	brcmf_dbg(TRACE, "Exit\n");
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

	brcmf_dbg(TRACE, "Enter\n");

	if (test_and_clear_bit(BRCMF_VIF_STATUS_CONNECTING,
			       &ifp->vif->sme_state)) {
		if (completed) {
			brcmf_get_assoc_ies(cfg, ifp);
			memcpy(profile->bssid, e->addr, ETH_ALEN);
			brcmf_update_bss_info(cfg, ifp);
			set_bit(BRCMF_VIF_STATUS_CONNECTED,
				&ifp->vif->sme_state);
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
		brcmf_dbg(CONN, "Report connect result - connection %s\n",
			  completed ? "succeeded" : "failed");
	}
	brcmf_dbg(TRACE, "Exit\n");
	return 0;
}

static s32
brcmf_notify_connect_status_ap(struct brcmf_cfg80211_info *cfg,
			       struct net_device *ndev,
			       const struct brcmf_event_msg *e, void *data)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	static int generation;
	u32 event = e->event_code;
	u32 reason = e->reason;
	struct station_info sinfo;

	brcmf_dbg(CONN, "event %d, reason %d\n", event, reason);
	if (event == BRCMF_E_LINK && reason == BRCMF_E_REASON_LINK_BSSCFG_DIS &&
	    ndev != cfg_to_ndev(cfg)) {
		brcmf_dbg(CONN, "AP mode link down\n");
		complete(&cfg->vif_disabled);
		if (ifp->vif->mbss)
			brcmf_remove_interface(ifp);
		return 0;
	}

	if (((event == BRCMF_E_ASSOC_IND) || (event == BRCMF_E_REASSOC_IND)) &&
	    (reason == BRCMF_E_STATUS_SUCCESS)) {
		memset(&sinfo, 0, sizeof(sinfo));
		if (!data) {
			brcmf_err("No IEs present in ASSOC/REASSOC_IND");
			return -EINVAL;
		}
		sinfo.assoc_req_ies = data;
		sinfo.assoc_req_ies_len = e->datalen;
		generation++;
		sinfo.generation = generation;
		cfg80211_new_sta(ndev, e->addr, &sinfo, GFP_KERNEL);
	} else if ((event == BRCMF_E_DISASSOC_IND) ||
		   (event == BRCMF_E_DEAUTH_IND) ||
		   (event == BRCMF_E_DEAUTH)) {
		cfg80211_del_sta(ndev, e->addr, GFP_KERNEL);
	}
	return 0;
}

static s32
brcmf_notify_connect_status(struct brcmf_if *ifp,
			    const struct brcmf_event_msg *e, void *data)
{
	struct brcmf_cfg80211_info *cfg = ifp->drvr->config;
	struct net_device *ndev = ifp->ndev;
	struct brcmf_cfg80211_profile *profile = &ifp->vif->profile;
	struct ieee80211_channel *chan;
	s32 err = 0;

	if ((e->event_code == BRCMF_E_DEAUTH) ||
	    (e->event_code == BRCMF_E_DEAUTH_IND) ||
	    (e->event_code == BRCMF_E_DISASSOC_IND) ||
	    ((e->event_code == BRCMF_E_LINK) && (!e->flags))) {
		brcmf_proto_delete_peer(ifp->drvr, ifp->ifidx, (u8 *)e->addr);
	}

	if (brcmf_is_apmode(ifp->vif)) {
		err = brcmf_notify_connect_status_ap(cfg, ndev, e, data);
	} else if (brcmf_is_linkup(e)) {
		brcmf_dbg(CONN, "Linkup\n");
		if (brcmf_is_ibssmode(ifp->vif)) {
			brcmf_inform_ibss(cfg, ndev, e->addr);
			chan = ieee80211_get_channel(cfg->wiphy, cfg->channel);
			memcpy(profile->bssid, e->addr, ETH_ALEN);
			cfg80211_ibss_joined(ndev, e->addr, chan, GFP_KERNEL);
			clear_bit(BRCMF_VIF_STATUS_CONNECTING,
				  &ifp->vif->sme_state);
			set_bit(BRCMF_VIF_STATUS_CONNECTED,
				&ifp->vif->sme_state);
		} else
			brcmf_bss_connect_done(cfg, ndev, e, true);
		brcmf_net_setcarrier(ifp, true);
	} else if (brcmf_is_linkdown(e)) {
		brcmf_dbg(CONN, "Linkdown\n");
		if (!brcmf_is_ibssmode(ifp->vif)) {
			brcmf_bss_connect_done(cfg, ndev, e, false);
			brcmf_link_down(ifp->vif,
					brcmf_map_fw_linkdown_reason(e));
			brcmf_init_prof(ndev_to_prof(ndev));
			if (ndev != cfg_to_ndev(cfg))
				complete(&cfg->vif_disabled);
			brcmf_net_setcarrier(ifp, false);
		}
	} else if (brcmf_is_nonetwork(cfg, e)) {
		if (brcmf_is_ibssmode(ifp->vif))
			clear_bit(BRCMF_VIF_STATUS_CONNECTING,
				  &ifp->vif->sme_state);
		else
			brcmf_bss_connect_done(cfg, ndev, e, false);
	}

	return err;
}

static s32
brcmf_notify_roaming_status(struct brcmf_if *ifp,
			    const struct brcmf_event_msg *e, void *data)
{
	struct brcmf_cfg80211_info *cfg = ifp->drvr->config;
	u32 event = e->event_code;
	u32 status = e->status;

	if (event == BRCMF_E_ROAM && status == BRCMF_E_STATUS_SUCCESS) {
		if (test_bit(BRCMF_VIF_STATUS_CONNECTED, &ifp->vif->sme_state))
			brcmf_bss_roaming_done(cfg, ifp->ndev, e);
		else
			brcmf_bss_connect_done(cfg, ifp->ndev, e, true);
	}

	return 0;
}

static s32
brcmf_notify_mic_status(struct brcmf_if *ifp,
			const struct brcmf_event_msg *e, void *data)
{
	u16 flags = e->flags;
	enum nl80211_key_type key_type;

	if (flags & BRCMF_EVENT_MSG_GROUP)
		key_type = NL80211_KEYTYPE_GROUP;
	else
		key_type = NL80211_KEYTYPE_PAIRWISE;

	cfg80211_michael_mic_failure(ifp->ndev, (u8 *)&e->addr, key_type, -1,
				     NULL, GFP_KERNEL);

	return 0;
}

static s32 brcmf_notify_vif_event(struct brcmf_if *ifp,
				  const struct brcmf_event_msg *e, void *data)
{
	struct brcmf_cfg80211_info *cfg = ifp->drvr->config;
	struct brcmf_if_event *ifevent = (struct brcmf_if_event *)data;
	struct brcmf_cfg80211_vif_event *event = &cfg->vif_event;
	struct brcmf_cfg80211_vif *vif;

	brcmf_dbg(TRACE, "Enter: action %u flags %u ifidx %u bsscfgidx %u\n",
		  ifevent->action, ifevent->flags, ifevent->ifidx,
		  ifevent->bsscfgidx);

	mutex_lock(&event->vif_event_lock);
	event->action = ifevent->action;
	vif = event->vif;

	switch (ifevent->action) {
	case BRCMF_E_IF_ADD:
		/* waiting process may have timed out */
		if (!cfg->vif_event.vif) {
			mutex_unlock(&event->vif_event_lock);
			return -EBADF;
		}

		ifp->vif = vif;
		vif->ifp = ifp;
		if (ifp->ndev) {
			vif->wdev.netdev = ifp->ndev;
			ifp->ndev->ieee80211_ptr = &vif->wdev;
			SET_NETDEV_DEV(ifp->ndev, wiphy_dev(cfg->wiphy));
		}
		mutex_unlock(&event->vif_event_lock);
		wake_up(&event->vif_wq);
		return 0;

	case BRCMF_E_IF_DEL:
		mutex_unlock(&event->vif_event_lock);
		/* event may not be upon user request */
		if (brcmf_cfg80211_vif_event_armed(cfg))
			wake_up(&event->vif_wq);
		return 0;

	case BRCMF_E_IF_CHANGE:
		mutex_unlock(&event->vif_event_lock);
		wake_up(&event->vif_wq);
		return 0;

	default:
		mutex_unlock(&event->vif_event_lock);
		break;
	}
	return -EINVAL;
}

static void brcmf_init_conf(struct brcmf_cfg80211_conf *conf)
{
	conf->frag_threshold = (u32)-1;
	conf->rts_threshold = (u32)-1;
	conf->retry_short = (u32)-1;
	conf->retry_long = (u32)-1;
}

static void brcmf_register_event_handlers(struct brcmf_cfg80211_info *cfg)
{
	brcmf_fweh_register(cfg->pub, BRCMF_E_LINK,
			    brcmf_notify_connect_status);
	brcmf_fweh_register(cfg->pub, BRCMF_E_DEAUTH_IND,
			    brcmf_notify_connect_status);
	brcmf_fweh_register(cfg->pub, BRCMF_E_DEAUTH,
			    brcmf_notify_connect_status);
	brcmf_fweh_register(cfg->pub, BRCMF_E_DISASSOC_IND,
			    brcmf_notify_connect_status);
	brcmf_fweh_register(cfg->pub, BRCMF_E_ASSOC_IND,
			    brcmf_notify_connect_status);
	brcmf_fweh_register(cfg->pub, BRCMF_E_REASSOC_IND,
			    brcmf_notify_connect_status);
	brcmf_fweh_register(cfg->pub, BRCMF_E_ROAM,
			    brcmf_notify_roaming_status);
	brcmf_fweh_register(cfg->pub, BRCMF_E_MIC_ERROR,
			    brcmf_notify_mic_status);
	brcmf_fweh_register(cfg->pub, BRCMF_E_SET_SSID,
			    brcmf_notify_connect_status);
	brcmf_fweh_register(cfg->pub, BRCMF_E_PFN_NET_FOUND,
			    brcmf_notify_sched_scan_results);
	brcmf_fweh_register(cfg->pub, BRCMF_E_IF,
			    brcmf_notify_vif_event);
	brcmf_fweh_register(cfg->pub, BRCMF_E_P2P_PROBEREQ_MSG,
			    brcmf_p2p_notify_rx_mgmt_p2p_probereq);
	brcmf_fweh_register(cfg->pub, BRCMF_E_P2P_DISC_LISTEN_COMPLETE,
			    brcmf_p2p_notify_listen_complete);
	brcmf_fweh_register(cfg->pub, BRCMF_E_ACTION_FRAME_RX,
			    brcmf_p2p_notify_action_frame_rx);
	brcmf_fweh_register(cfg->pub, BRCMF_E_ACTION_FRAME_COMPLETE,
			    brcmf_p2p_notify_action_tx_complete);
	brcmf_fweh_register(cfg->pub, BRCMF_E_ACTION_FRAME_OFF_CHAN_COMPLETE,
			    brcmf_p2p_notify_action_tx_complete);
}

static void brcmf_deinit_priv_mem(struct brcmf_cfg80211_info *cfg)
{
	kfree(cfg->conf);
	cfg->conf = NULL;
	kfree(cfg->extra_buf);
	cfg->extra_buf = NULL;
	kfree(cfg->wowl.nd);
	cfg->wowl.nd = NULL;
	kfree(cfg->wowl.nd_info);
	cfg->wowl.nd_info = NULL;
	kfree(cfg->escan_info.escan_buf);
	cfg->escan_info.escan_buf = NULL;
}

static s32 brcmf_init_priv_mem(struct brcmf_cfg80211_info *cfg)
{
	cfg->conf = kzalloc(sizeof(*cfg->conf), GFP_KERNEL);
	if (!cfg->conf)
		goto init_priv_mem_out;
	cfg->extra_buf = kzalloc(WL_EXTRA_BUF_MAX, GFP_KERNEL);
	if (!cfg->extra_buf)
		goto init_priv_mem_out;
	cfg->wowl.nd = kzalloc(sizeof(*cfg->wowl.nd) + sizeof(u32), GFP_KERNEL);
	if (!cfg->wowl.nd)
		goto init_priv_mem_out;
	cfg->wowl.nd_info = kzalloc(sizeof(*cfg->wowl.nd_info) +
				    sizeof(struct cfg80211_wowlan_nd_match *),
				    GFP_KERNEL);
	if (!cfg->wowl.nd_info)
		goto init_priv_mem_out;
	cfg->escan_info.escan_buf = kzalloc(BRCMF_ESCAN_BUF_SIZE, GFP_KERNEL);
	if (!cfg->escan_info.escan_buf)
		goto init_priv_mem_out;

	return 0;

init_priv_mem_out:
	brcmf_deinit_priv_mem(cfg);

	return -ENOMEM;
}

static s32 wl_init_priv(struct brcmf_cfg80211_info *cfg)
{
	s32 err = 0;

	cfg->scan_request = NULL;
	cfg->pwr_save = true;
	cfg->active_scan = true;	/* we do active scan per default */
	cfg->dongle_up = false;		/* dongle is not up yet */
	err = brcmf_init_priv_mem(cfg);
	if (err)
		return err;
	brcmf_register_event_handlers(cfg);
	mutex_init(&cfg->usr_sync);
	brcmf_init_escan(cfg);
	brcmf_init_conf(cfg->conf);
	init_completion(&cfg->vif_disabled);
	return err;
}

static void wl_deinit_priv(struct brcmf_cfg80211_info *cfg)
{
	cfg->dongle_up = false;	/* dongle down */
	brcmf_abort_scanning(cfg);
	brcmf_deinit_priv_mem(cfg);
}

static void init_vif_event(struct brcmf_cfg80211_vif_event *event)
{
	init_waitqueue_head(&event->vif_wq);
	mutex_init(&event->vif_event_lock);
}

static s32 brcmf_dongle_roam(struct brcmf_if *ifp)
{
	s32 err;
	u32 bcn_timeout;
	__le32 roamtrigger[2];
	__le32 roam_delta[2];

	/* Configure beacon timeout value based upon roaming setting */
	if (ifp->drvr->settings->roamoff)
		bcn_timeout = BRCMF_DEFAULT_BCN_TIMEOUT_ROAM_OFF;
	else
		bcn_timeout = BRCMF_DEFAULT_BCN_TIMEOUT_ROAM_ON;
	err = brcmf_fil_iovar_int_set(ifp, "bcn_timeout", bcn_timeout);
	if (err) {
		brcmf_err("bcn_timeout error (%d)\n", err);
		goto roam_setup_done;
	}

	/* Enable/Disable built-in roaming to allow supplicant to take care of
	 * roaming.
	 */
	brcmf_dbg(INFO, "Internal Roaming = %s\n",
		  ifp->drvr->settings->roamoff ? "Off" : "On");
	err = brcmf_fil_iovar_int_set(ifp, "roam_off",
				      ifp->drvr->settings->roamoff);
	if (err) {
		brcmf_err("roam_off error (%d)\n", err);
		goto roam_setup_done;
	}

	roamtrigger[0] = cpu_to_le32(WL_ROAM_TRIGGER_LEVEL);
	roamtrigger[1] = cpu_to_le32(BRCM_BAND_ALL);
	err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_ROAM_TRIGGER,
				     (void *)roamtrigger, sizeof(roamtrigger));
	if (err) {
		brcmf_err("WLC_SET_ROAM_TRIGGER error (%d)\n", err);
		goto roam_setup_done;
	}

	roam_delta[0] = cpu_to_le32(WL_ROAM_DELTA);
	roam_delta[1] = cpu_to_le32(BRCM_BAND_ALL);
	err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_ROAM_DELTA,
				     (void *)roam_delta, sizeof(roam_delta));
	if (err) {
		brcmf_err("WLC_SET_ROAM_DELTA error (%d)\n", err);
		goto roam_setup_done;
	}

roam_setup_done:
	return err;
}

static s32
brcmf_dongle_scantime(struct brcmf_if *ifp)
{
	s32 err = 0;

	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_CHANNEL_TIME,
				    BRCMF_SCAN_CHANNEL_TIME);
	if (err) {
		brcmf_err("Scan assoc time error (%d)\n", err);
		goto dongle_scantime_out;
	}
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_UNASSOC_TIME,
				    BRCMF_SCAN_UNASSOC_TIME);
	if (err) {
		brcmf_err("Scan unassoc time error (%d)\n", err);
		goto dongle_scantime_out;
	}

	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_PASSIVE_TIME,
				    BRCMF_SCAN_PASSIVE_TIME);
	if (err) {
		brcmf_err("Scan passive time error (%d)\n", err);
		goto dongle_scantime_out;
	}

dongle_scantime_out:
	return err;
}

static void brcmf_update_bw40_channel_flag(struct ieee80211_channel *channel,
					   struct brcmu_chan *ch)
{
	u32 ht40_flag;

	ht40_flag = channel->flags & IEEE80211_CHAN_NO_HT40;
	if (ch->sb == BRCMU_CHAN_SB_U) {
		if (ht40_flag == IEEE80211_CHAN_NO_HT40)
			channel->flags &= ~IEEE80211_CHAN_NO_HT40;
		channel->flags |= IEEE80211_CHAN_NO_HT40PLUS;
	} else {
		/* It should be one of
		 * IEEE80211_CHAN_NO_HT40 or
		 * IEEE80211_CHAN_NO_HT40PLUS
		 */
		channel->flags &= ~IEEE80211_CHAN_NO_HT40;
		if (ht40_flag == IEEE80211_CHAN_NO_HT40)
			channel->flags |= IEEE80211_CHAN_NO_HT40MINUS;
	}
}

static int brcmf_construct_chaninfo(struct brcmf_cfg80211_info *cfg,
				    u32 bw_cap[])
{
	struct brcmf_if *ifp = netdev_priv(cfg_to_ndev(cfg));
	struct ieee80211_supported_band *band;
	struct ieee80211_channel *channel;
	struct wiphy *wiphy;
	struct brcmf_chanspec_list *list;
	struct brcmu_chan ch;
	int err;
	u8 *pbuf;
	u32 i, j;
	u32 total;
	u32 chaninfo;
	u32 index;

	pbuf = kzalloc(BRCMF_DCMD_MEDLEN, GFP_KERNEL);

	if (pbuf == NULL)
		return -ENOMEM;

	list = (struct brcmf_chanspec_list *)pbuf;

	err = brcmf_fil_iovar_data_get(ifp, "chanspecs", pbuf,
				       BRCMF_DCMD_MEDLEN);
	if (err) {
		brcmf_err("get chanspecs error (%d)\n", err);
		goto fail_pbuf;
	}

	wiphy = cfg_to_wiphy(cfg);
	band = wiphy->bands[IEEE80211_BAND_2GHZ];
	if (band)
		for (i = 0; i < band->n_channels; i++)
			band->channels[i].flags = IEEE80211_CHAN_DISABLED;
	band = wiphy->bands[IEEE80211_BAND_5GHZ];
	if (band)
		for (i = 0; i < band->n_channels; i++)
			band->channels[i].flags = IEEE80211_CHAN_DISABLED;

	total = le32_to_cpu(list->count);
	for (i = 0; i < total; i++) {
		ch.chspec = (u16)le32_to_cpu(list->element[i]);
		cfg->d11inf.decchspec(&ch);

		if (ch.band == BRCMU_CHAN_BAND_2G) {
			band = wiphy->bands[IEEE80211_BAND_2GHZ];
		} else if (ch.band == BRCMU_CHAN_BAND_5G) {
			band = wiphy->bands[IEEE80211_BAND_5GHZ];
		} else {
			brcmf_err("Invalid channel Spec. 0x%x.\n", ch.chspec);
			continue;
		}
		if (!band)
			continue;
		if (!(bw_cap[band->band] & WLC_BW_40MHZ_BIT) &&
		    ch.bw == BRCMU_CHAN_BW_40)
			continue;
		if (!(bw_cap[band->band] & WLC_BW_80MHZ_BIT) &&
		    ch.bw == BRCMU_CHAN_BW_80)
			continue;

		channel = band->channels;
		index = band->n_channels;
		for (j = 0; j < band->n_channels; j++) {
			if (channel[j].hw_value == ch.chnum) {
				index = j;
				break;
			}
		}
		channel[index].center_freq =
			ieee80211_channel_to_frequency(ch.chnum, band->band);
		channel[index].hw_value = ch.chnum;

		/* assuming the chanspecs order is HT20,
		 * HT40 upper, HT40 lower, and VHT80.
		 */
		if (ch.bw == BRCMU_CHAN_BW_80) {
			channel[index].flags &= ~IEEE80211_CHAN_NO_80MHZ;
		} else if (ch.bw == BRCMU_CHAN_BW_40) {
			brcmf_update_bw40_channel_flag(&channel[index], &ch);
		} else {
			/* enable the channel and disable other bandwidths
			 * for now as mentioned order assure they are enabled
			 * for subsequent chanspecs.
			 */
			channel[index].flags = IEEE80211_CHAN_NO_HT40 |
					       IEEE80211_CHAN_NO_80MHZ;
			ch.bw = BRCMU_CHAN_BW_20;
			cfg->d11inf.encchspec(&ch);
			chaninfo = ch.chspec;
			err = brcmf_fil_bsscfg_int_get(ifp, "per_chan_info",
						       &chaninfo);
			if (!err) {
				if (chaninfo & WL_CHAN_RADAR)
					channel[index].flags |=
						(IEEE80211_CHAN_RADAR |
						 IEEE80211_CHAN_NO_IR);
				if (chaninfo & WL_CHAN_PASSIVE)
					channel[index].flags |=
						IEEE80211_CHAN_NO_IR;
			}
		}
	}

fail_pbuf:
	kfree(pbuf);
	return err;
}

static int brcmf_enable_bw40_2g(struct brcmf_cfg80211_info *cfg)
{
	struct brcmf_if *ifp = netdev_priv(cfg_to_ndev(cfg));
	struct ieee80211_supported_band *band;
	struct brcmf_fil_bwcap_le band_bwcap;
	struct brcmf_chanspec_list *list;
	u8 *pbuf;
	u32 val;
	int err;
	struct brcmu_chan ch;
	u32 num_chan;
	int i, j;

	/* verify support for bw_cap command */
	val = WLC_BAND_5G;
	err = brcmf_fil_iovar_int_get(ifp, "bw_cap", &val);

	if (!err) {
		/* only set 2G bandwidth using bw_cap command */
		band_bwcap.band = cpu_to_le32(WLC_BAND_2G);
		band_bwcap.bw_cap = cpu_to_le32(WLC_BW_CAP_40MHZ);
		err = brcmf_fil_iovar_data_set(ifp, "bw_cap", &band_bwcap,
					       sizeof(band_bwcap));
	} else {
		brcmf_dbg(INFO, "fallback to mimo_bw_cap\n");
		val = WLC_N_BW_40ALL;
		err = brcmf_fil_iovar_int_set(ifp, "mimo_bw_cap", val);
	}

	if (!err) {
		/* update channel info in 2G band */
		pbuf = kzalloc(BRCMF_DCMD_MEDLEN, GFP_KERNEL);

		if (pbuf == NULL)
			return -ENOMEM;

		ch.band = BRCMU_CHAN_BAND_2G;
		ch.bw = BRCMU_CHAN_BW_40;
		ch.sb = BRCMU_CHAN_SB_NONE;
		ch.chnum = 0;
		cfg->d11inf.encchspec(&ch);

		/* pass encoded chanspec in query */
		*(__le16 *)pbuf = cpu_to_le16(ch.chspec);

		err = brcmf_fil_iovar_data_get(ifp, "chanspecs", pbuf,
					       BRCMF_DCMD_MEDLEN);
		if (err) {
			brcmf_err("get chanspecs error (%d)\n", err);
			kfree(pbuf);
			return err;
		}

		band = cfg_to_wiphy(cfg)->bands[IEEE80211_BAND_2GHZ];
		list = (struct brcmf_chanspec_list *)pbuf;
		num_chan = le32_to_cpu(list->count);
		for (i = 0; i < num_chan; i++) {
			ch.chspec = (u16)le32_to_cpu(list->element[i]);
			cfg->d11inf.decchspec(&ch);
			if (WARN_ON(ch.band != BRCMU_CHAN_BAND_2G))
				continue;
			if (WARN_ON(ch.bw != BRCMU_CHAN_BW_40))
				continue;
			for (j = 0; j < band->n_channels; j++) {
				if (band->channels[j].hw_value == ch.chnum)
					break;
			}
			if (WARN_ON(j == band->n_channels))
				continue;

			brcmf_update_bw40_channel_flag(&band->channels[j], &ch);
		}
		kfree(pbuf);
	}
	return err;
}

static void brcmf_get_bwcap(struct brcmf_if *ifp, u32 bw_cap[])
{
	u32 band, mimo_bwcap;
	int err;

	band = WLC_BAND_2G;
	err = brcmf_fil_iovar_int_get(ifp, "bw_cap", &band);
	if (!err) {
		bw_cap[IEEE80211_BAND_2GHZ] = band;
		band = WLC_BAND_5G;
		err = brcmf_fil_iovar_int_get(ifp, "bw_cap", &band);
		if (!err) {
			bw_cap[IEEE80211_BAND_5GHZ] = band;
			return;
		}
		WARN_ON(1);
		return;
	}
	brcmf_dbg(INFO, "fallback to mimo_bw_cap info\n");
	mimo_bwcap = 0;
	err = brcmf_fil_iovar_int_get(ifp, "mimo_bw_cap", &mimo_bwcap);
	if (err)
		/* assume 20MHz if firmware does not give a clue */
		mimo_bwcap = WLC_N_BW_20ALL;

	switch (mimo_bwcap) {
	case WLC_N_BW_40ALL:
		bw_cap[IEEE80211_BAND_2GHZ] |= WLC_BW_40MHZ_BIT;
		/* fall-thru */
	case WLC_N_BW_20IN2G_40IN5G:
		bw_cap[IEEE80211_BAND_5GHZ] |= WLC_BW_40MHZ_BIT;
		/* fall-thru */
	case WLC_N_BW_20ALL:
		bw_cap[IEEE80211_BAND_2GHZ] |= WLC_BW_20MHZ_BIT;
		bw_cap[IEEE80211_BAND_5GHZ] |= WLC_BW_20MHZ_BIT;
		break;
	default:
		brcmf_err("invalid mimo_bw_cap value\n");
	}
}

static void brcmf_update_ht_cap(struct ieee80211_supported_band *band,
				u32 bw_cap[2], u32 nchain)
{
	band->ht_cap.ht_supported = true;
	if (bw_cap[band->band] & WLC_BW_40MHZ_BIT) {
		band->ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;
		band->ht_cap.cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
	}
	band->ht_cap.cap |= IEEE80211_HT_CAP_SGI_20;
	band->ht_cap.cap |= IEEE80211_HT_CAP_DSSSCCK40;
	band->ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	band->ht_cap.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16;
	memset(band->ht_cap.mcs.rx_mask, 0xff, nchain);
	band->ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
}

static __le16 brcmf_get_mcs_map(u32 nchain, enum ieee80211_vht_mcs_support supp)
{
	u16 mcs_map;
	int i;

	for (i = 0, mcs_map = 0xFFFF; i < nchain; i++)
		mcs_map = (mcs_map << 2) | supp;

	return cpu_to_le16(mcs_map);
}

static void brcmf_update_vht_cap(struct ieee80211_supported_band *band,
				 u32 bw_cap[2], u32 nchain, u32 txstreams,
				 u32 txbf_bfe_cap, u32 txbf_bfr_cap)
{
	__le16 mcs_map;

	/* not allowed in 2.4G band */
	if (band->band == IEEE80211_BAND_2GHZ)
		return;

	band->vht_cap.vht_supported = true;
	/* 80MHz is mandatory */
	band->vht_cap.cap |= IEEE80211_VHT_CAP_SHORT_GI_80;
	if (bw_cap[band->band] & WLC_BW_160MHZ_BIT) {
		band->vht_cap.cap |= IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
		band->vht_cap.cap |= IEEE80211_VHT_CAP_SHORT_GI_160;
	}
	/* all support 256-QAM */
	mcs_map = brcmf_get_mcs_map(nchain, IEEE80211_VHT_MCS_SUPPORT_0_9);
	band->vht_cap.vht_mcs.rx_mcs_map = mcs_map;
	band->vht_cap.vht_mcs.tx_mcs_map = mcs_map;

	/* Beamforming support information */
	if (txbf_bfe_cap & BRCMF_TXBF_SU_BFE_CAP)
		band->vht_cap.cap |= IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;
	if (txbf_bfe_cap & BRCMF_TXBF_MU_BFE_CAP)
		band->vht_cap.cap |= IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;
	if (txbf_bfr_cap & BRCMF_TXBF_SU_BFR_CAP)
		band->vht_cap.cap |= IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE;
	if (txbf_bfr_cap & BRCMF_TXBF_MU_BFR_CAP)
		band->vht_cap.cap |= IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE;

	if ((txbf_bfe_cap || txbf_bfr_cap) && (txstreams > 1)) {
		band->vht_cap.cap |=
			(2 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT);
		band->vht_cap.cap |= ((txstreams - 1) <<
				IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT);
		band->vht_cap.cap |=
			IEEE80211_VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB;
	}
}

static int brcmf_setup_wiphybands(struct wiphy *wiphy)
{
	struct brcmf_cfg80211_info *cfg = wiphy_priv(wiphy);
	struct brcmf_if *ifp = netdev_priv(cfg_to_ndev(cfg));
	u32 nmode = 0;
	u32 vhtmode = 0;
	u32 bw_cap[2] = { WLC_BW_20MHZ_BIT, WLC_BW_20MHZ_BIT };
	u32 rxchain;
	u32 nchain;
	int err;
	s32 i;
	struct ieee80211_supported_band *band;
	u32 txstreams = 0;
	u32 txbf_bfe_cap = 0;
	u32 txbf_bfr_cap = 0;

	(void)brcmf_fil_iovar_int_get(ifp, "vhtmode", &vhtmode);
	err = brcmf_fil_iovar_int_get(ifp, "nmode", &nmode);
	if (err) {
		brcmf_err("nmode error (%d)\n", err);
	} else {
		brcmf_get_bwcap(ifp, bw_cap);
	}
	brcmf_dbg(INFO, "nmode=%d, vhtmode=%d, bw_cap=(%d, %d)\n",
		  nmode, vhtmode, bw_cap[IEEE80211_BAND_2GHZ],
		  bw_cap[IEEE80211_BAND_5GHZ]);

	err = brcmf_fil_iovar_int_get(ifp, "rxchain", &rxchain);
	if (err) {
		brcmf_err("rxchain error (%d)\n", err);
		nchain = 1;
	} else {
		for (nchain = 0; rxchain; nchain++)
			rxchain = rxchain & (rxchain - 1);
	}
	brcmf_dbg(INFO, "nchain=%d\n", nchain);

	err = brcmf_construct_chaninfo(cfg, bw_cap);
	if (err) {
		brcmf_err("brcmf_construct_chaninfo failed (%d)\n", err);
		return err;
	}

	if (vhtmode) {
		(void)brcmf_fil_iovar_int_get(ifp, "txstreams", &txstreams);
		(void)brcmf_fil_iovar_int_get(ifp, "txbf_bfe_cap",
					      &txbf_bfe_cap);
		(void)brcmf_fil_iovar_int_get(ifp, "txbf_bfr_cap",
					      &txbf_bfr_cap);
	}

	wiphy = cfg_to_wiphy(cfg);
	for (i = 0; i < ARRAY_SIZE(wiphy->bands); i++) {
		band = wiphy->bands[i];
		if (band == NULL)
			continue;

		if (nmode)
			brcmf_update_ht_cap(band, bw_cap, nchain);
		if (vhtmode)
			brcmf_update_vht_cap(band, bw_cap, nchain, txstreams,
					     txbf_bfe_cap, txbf_bfr_cap);
	}

	return 0;
}

static const struct ieee80211_txrx_stypes
brcmf_txrx_stypes[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_STATION] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		      BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
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
	[NL80211_IFTYPE_P2P_DEVICE] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		      BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	}
};

/**
 * brcmf_setup_ifmodes() - determine interface modes and combinations.
 *
 * @wiphy: wiphy object.
 * @ifp: interface object needed for feat module api.
 *
 * The interface modes and combinations are determined dynamically here
 * based on firmware functionality.
 *
 * no p2p and no mbss:
 *
 *	#STA <= 1, #AP <= 1, channels = 1, 2 total
 *
 * no p2p and mbss:
 *
 *	#STA <= 1, #AP <= 1, channels = 1, 2 total
 *	#AP <= 4, matching BI, channels = 1, 4 total
 *
 * p2p, no mchan, and mbss:
 *
 *	#STA <= 1, #P2P-DEV <= 1, #{P2P-CL, P2P-GO} <= 1, channels = 1, 3 total
 *	#STA <= 1, #P2P-DEV <= 1, #AP <= 1, #P2P-CL <= 1, channels = 1, 4 total
 *	#AP <= 4, matching BI, channels = 1, 4 total
 *
 * p2p, mchan, and mbss:
 *
 *	#STA <= 1, #P2P-DEV <= 1, #{P2P-CL, P2P-GO} <= 1, channels = 2, 3 total
 *	#STA <= 1, #P2P-DEV <= 1, #AP <= 1, #P2P-CL <= 1, channels = 1, 4 total
 *	#AP <= 4, matching BI, channels = 1, 4 total
 */
static int brcmf_setup_ifmodes(struct wiphy *wiphy, struct brcmf_if *ifp)
{
	struct ieee80211_iface_combination *combo = NULL;
	struct ieee80211_iface_limit *c0_limits = NULL;
	struct ieee80211_iface_limit *p2p_limits = NULL;
	struct ieee80211_iface_limit *mbss_limits = NULL;
	bool mbss, p2p;
	int i, c, n_combos;

	mbss = brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MBSS);
	p2p = brcmf_feat_is_enabled(ifp, BRCMF_FEAT_P2P);

	n_combos = 1 + !!p2p + !!mbss;
	combo = kcalloc(n_combos, sizeof(*combo), GFP_KERNEL);
	if (!combo)
		goto err;

	c0_limits = kcalloc(p2p ? 3 : 2, sizeof(*c0_limits), GFP_KERNEL);
	if (!c0_limits)
		goto err;

	if (p2p) {
		p2p_limits = kcalloc(4, sizeof(*p2p_limits), GFP_KERNEL);
		if (!p2p_limits)
			goto err;
	}

	if (mbss) {
		mbss_limits = kcalloc(1, sizeof(*mbss_limits), GFP_KERNEL);
		if (!mbss_limits)
			goto err;
	}

	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				 BIT(NL80211_IFTYPE_ADHOC) |
				 BIT(NL80211_IFTYPE_AP);

	c = 0;
	i = 0;
	combo[c].num_different_channels = 1;
	c0_limits[i].max = 1;
	c0_limits[i++].types = BIT(NL80211_IFTYPE_STATION);
	if (p2p) {
		if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MCHAN))
			combo[c].num_different_channels = 2;
		wiphy->interface_modes |= BIT(NL80211_IFTYPE_P2P_CLIENT) |
					  BIT(NL80211_IFTYPE_P2P_GO) |
					  BIT(NL80211_IFTYPE_P2P_DEVICE);
		c0_limits[i].max = 1;
		c0_limits[i++].types = BIT(NL80211_IFTYPE_P2P_DEVICE);
		c0_limits[i].max = 1;
		c0_limits[i++].types = BIT(NL80211_IFTYPE_P2P_CLIENT) |
				       BIT(NL80211_IFTYPE_P2P_GO);
	} else {
		c0_limits[i].max = 1;
		c0_limits[i++].types = BIT(NL80211_IFTYPE_AP);
	}
	combo[c].max_interfaces = i;
	combo[c].n_limits = i;
	combo[c].limits = c0_limits;

	if (p2p) {
		c++;
		i = 0;
		combo[c].num_different_channels = 1;
		p2p_limits[i].max = 1;
		p2p_limits[i++].types = BIT(NL80211_IFTYPE_STATION);
		p2p_limits[i].max = 1;
		p2p_limits[i++].types = BIT(NL80211_IFTYPE_AP);
		p2p_limits[i].max = 1;
		p2p_limits[i++].types = BIT(NL80211_IFTYPE_P2P_CLIENT);
		p2p_limits[i].max = 1;
		p2p_limits[i++].types = BIT(NL80211_IFTYPE_P2P_DEVICE);
		combo[c].max_interfaces = i;
		combo[c].n_limits = i;
		combo[c].limits = p2p_limits;
	}

	if (mbss) {
		c++;
		combo[c].beacon_int_infra_match = true;
		combo[c].num_different_channels = 1;
		mbss_limits[0].max = 4;
		mbss_limits[0].types = BIT(NL80211_IFTYPE_AP);
		combo[c].max_interfaces = 4;
		combo[c].n_limits = 1;
		combo[c].limits = mbss_limits;
	}
	wiphy->n_iface_combinations = n_combos;
	wiphy->iface_combinations = combo;
	return 0;

err:
	kfree(c0_limits);
	kfree(p2p_limits);
	kfree(mbss_limits);
	kfree(combo);
	return -ENOMEM;
}

static void brcmf_wiphy_pno_params(struct wiphy *wiphy)
{
	/* scheduled scan settings */
	wiphy->max_sched_scan_ssids = BRCMF_PNO_MAX_PFN_COUNT;
	wiphy->max_match_sets = BRCMF_PNO_MAX_PFN_COUNT;
	wiphy->max_sched_scan_ie_len = BRCMF_SCAN_IE_LEN_MAX;
	wiphy->flags |= WIPHY_FLAG_SUPPORTS_SCHED_SCAN;
}

#ifdef CONFIG_PM
static struct wiphy_wowlan_support brcmf_wowlan_support = {
	.flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_DISCONNECT,
	.n_patterns = BRCMF_WOWL_MAXPATTERNS,
	.pattern_max_len = BRCMF_WOWL_MAXPATTERNSIZE,
	.pattern_min_len = 1,
	.max_pkt_offset = 1500,
};
#endif

static void brcmf_wiphy_wowl_params(struct wiphy *wiphy, struct brcmf_if *ifp)
{
#ifdef CONFIG_PM
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	s32 err;
	u32 wowl_cap;

	if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_PNO)) {
		err = brcmf_fil_iovar_int_get(ifp, "wowl_cap", &wowl_cap);
		if (!err) {
			if (wowl_cap & BRCMF_WOWL_PFN_FOUND) {
				brcmf_wowlan_support.flags |=
							WIPHY_WOWLAN_NET_DETECT;
				init_waitqueue_head(&cfg->wowl.nd_data_wait);
			}
		}
	}
	wiphy->wowlan = &brcmf_wowlan_support;
#endif
}

static int brcmf_setup_wiphy(struct wiphy *wiphy, struct brcmf_if *ifp)
{
	struct brcmf_pub *drvr = ifp->drvr;
	const struct ieee80211_iface_combination *combo;
	struct ieee80211_supported_band *band;
	u16 max_interfaces = 0;
	__le32 bandlist[3];
	u32 n_bands;
	int err, i;

	wiphy->max_scan_ssids = WL_NUM_SCAN_MAX;
	wiphy->max_scan_ie_len = BRCMF_SCAN_IE_LEN_MAX;
	wiphy->max_num_pmkids = BRCMF_MAXPMKID;

	err = brcmf_setup_ifmodes(wiphy, ifp);
	if (err)
		return err;

	for (i = 0, combo = wiphy->iface_combinations;
	     i < wiphy->n_iface_combinations; i++, combo++) {
		max_interfaces = max(max_interfaces, combo->max_interfaces);
	}

	for (i = 0; i < max_interfaces && i < ARRAY_SIZE(drvr->addresses);
	     i++) {
		u8 *addr = drvr->addresses[i].addr;

		memcpy(addr, drvr->mac, ETH_ALEN);
		if (i) {
			addr[0] |= BIT(1);
			addr[ETH_ALEN - 1] ^= i;
		}
	}
	wiphy->addresses = drvr->addresses;
	wiphy->n_addresses = i;

	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wiphy->cipher_suites = __wl_cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(__wl_cipher_suites);
	wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT |
			WIPHY_FLAG_OFFCHAN_TX |
			WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
	if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_TDLS))
		wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;
	if (!ifp->drvr->settings->roamoff)
		wiphy->flags |= WIPHY_FLAG_SUPPORTS_FW_ROAM;
	wiphy->mgmt_stypes = brcmf_txrx_stypes;
	wiphy->max_remain_on_channel_duration = 5000;
	if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_PNO))
		brcmf_wiphy_pno_params(wiphy);

	/* vendor commands/events support */
	wiphy->vendor_commands = brcmf_vendor_cmds;
	wiphy->n_vendor_commands = BRCMF_VNDR_CMDS_LAST - 1;

	if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_WOWL))
		brcmf_wiphy_wowl_params(wiphy, ifp);
	err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BANDLIST, &bandlist,
				     sizeof(bandlist));
	if (err) {
		brcmf_err("could not obtain band info: err=%d\n", err);
		return err;
	}
	/* first entry in bandlist is number of bands */
	n_bands = le32_to_cpu(bandlist[0]);
	for (i = 1; i <= n_bands && i < ARRAY_SIZE(bandlist); i++) {
		if (bandlist[i] == cpu_to_le32(WLC_BAND_2G)) {
			band = kmemdup(&__wl_band_2ghz, sizeof(__wl_band_2ghz),
				       GFP_KERNEL);
			if (!band)
				return -ENOMEM;

			band->channels = kmemdup(&__wl_2ghz_channels,
						 sizeof(__wl_2ghz_channels),
						 GFP_KERNEL);
			if (!band->channels) {
				kfree(band);
				return -ENOMEM;
			}

			band->n_channels = ARRAY_SIZE(__wl_2ghz_channels);
			wiphy->bands[IEEE80211_BAND_2GHZ] = band;
		}
		if (bandlist[i] == cpu_to_le32(WLC_BAND_5G)) {
			band = kmemdup(&__wl_band_5ghz, sizeof(__wl_band_5ghz),
				       GFP_KERNEL);
			if (!band)
				return -ENOMEM;

			band->channels = kmemdup(&__wl_5ghz_channels,
						 sizeof(__wl_5ghz_channels),
						 GFP_KERNEL);
			if (!band->channels) {
				kfree(band);
				return -ENOMEM;
			}

			band->n_channels = ARRAY_SIZE(__wl_5ghz_channels);
			wiphy->bands[IEEE80211_BAND_5GHZ] = band;
		}
	}
	err = brcmf_setup_wiphybands(wiphy);
	return err;
}

static s32 brcmf_config_dongle(struct brcmf_cfg80211_info *cfg)
{
	struct net_device *ndev;
	struct wireless_dev *wdev;
	struct brcmf_if *ifp;
	s32 power_mode;
	s32 err = 0;

	if (cfg->dongle_up)
		return err;

	ndev = cfg_to_ndev(cfg);
	wdev = ndev->ieee80211_ptr;
	ifp = netdev_priv(ndev);

	/* make sure RF is ready for work */
	brcmf_fil_cmd_int_set(ifp, BRCMF_C_UP, 0);

	brcmf_dongle_scantime(ifp);

	power_mode = cfg->pwr_save ? PM_FAST : PM_OFF;
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_PM, power_mode);
	if (err)
		goto default_conf_out;
	brcmf_dbg(INFO, "power save set to %s\n",
		  (power_mode ? "enabled" : "disabled"));

	err = brcmf_dongle_roam(ifp);
	if (err)
		goto default_conf_out;
	err = brcmf_cfg80211_change_iface(wdev->wiphy, ndev, wdev->iftype,
					  NULL, NULL);
	if (err)
		goto default_conf_out;

	brcmf_configure_arp_offload(ifp, true);

	cfg->dongle_up = true;
default_conf_out:

	return err;

}

static s32 __brcmf_cfg80211_up(struct brcmf_if *ifp)
{
	set_bit(BRCMF_VIF_STATUS_READY, &ifp->vif->sme_state);

	return brcmf_config_dongle(ifp->drvr->config);
}

static s32 __brcmf_cfg80211_down(struct brcmf_if *ifp)
{
	struct brcmf_cfg80211_info *cfg = ifp->drvr->config;

	/*
	 * While going down, if associated with AP disassociate
	 * from AP to save power
	 */
	if (check_vif_up(ifp->vif)) {
		brcmf_link_down(ifp->vif, WLAN_REASON_UNSPECIFIED);

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

s32 brcmf_cfg80211_up(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_info *cfg = ifp->drvr->config;
	s32 err = 0;

	mutex_lock(&cfg->usr_sync);
	err = __brcmf_cfg80211_up(ifp);
	mutex_unlock(&cfg->usr_sync);

	return err;
}

s32 brcmf_cfg80211_down(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_cfg80211_info *cfg = ifp->drvr->config;
	s32 err = 0;

	mutex_lock(&cfg->usr_sync);
	err = __brcmf_cfg80211_down(ifp);
	mutex_unlock(&cfg->usr_sync);

	return err;
}

enum nl80211_iftype brcmf_cfg80211_get_iftype(struct brcmf_if *ifp)
{
	struct wireless_dev *wdev = &ifp->vif->wdev;

	return wdev->iftype;
}

bool brcmf_get_vif_state_any(struct brcmf_cfg80211_info *cfg,
			     unsigned long state)
{
	struct brcmf_cfg80211_vif *vif;

	list_for_each_entry(vif, &cfg->vif_list, list) {
		if (test_bit(state, &vif->sme_state))
			return true;
	}
	return false;
}

static inline bool vif_event_equals(struct brcmf_cfg80211_vif_event *event,
				    u8 action)
{
	u8 evt_action;

	mutex_lock(&event->vif_event_lock);
	evt_action = event->action;
	mutex_unlock(&event->vif_event_lock);
	return evt_action == action;
}

void brcmf_cfg80211_arm_vif_event(struct brcmf_cfg80211_info *cfg,
				  struct brcmf_cfg80211_vif *vif)
{
	struct brcmf_cfg80211_vif_event *event = &cfg->vif_event;

	mutex_lock(&event->vif_event_lock);
	event->vif = vif;
	event->action = 0;
	mutex_unlock(&event->vif_event_lock);
}

bool brcmf_cfg80211_vif_event_armed(struct brcmf_cfg80211_info *cfg)
{
	struct brcmf_cfg80211_vif_event *event = &cfg->vif_event;
	bool armed;

	mutex_lock(&event->vif_event_lock);
	armed = event->vif != NULL;
	mutex_unlock(&event->vif_event_lock);

	return armed;
}

int brcmf_cfg80211_wait_vif_event(struct brcmf_cfg80211_info *cfg,
				  u8 action, ulong timeout)
{
	struct brcmf_cfg80211_vif_event *event = &cfg->vif_event;

	return wait_event_timeout(event->vif_wq,
				  vif_event_equals(event, action), timeout);
}

static void brcmf_cfg80211_reg_notifier(struct wiphy *wiphy,
					struct regulatory_request *req)
{
	struct brcmf_cfg80211_info *cfg = wiphy_priv(wiphy);
	struct brcmf_if *ifp = netdev_priv(cfg_to_ndev(cfg));
	struct brcmf_fil_country_le ccreq;
	int i;

	brcmf_dbg(TRACE, "enter: initiator=%d, alpha=%c%c\n", req->initiator,
		  req->alpha2[0], req->alpha2[1]);

	/* ignore non-ISO3166 country codes */
	for (i = 0; i < sizeof(req->alpha2); i++)
		if (req->alpha2[i] < 'A' || req->alpha2[i] > 'Z') {
			brcmf_err("not a ISO3166 code\n");
			return;
		}
	memset(&ccreq, 0, sizeof(ccreq));
	ccreq.rev = cpu_to_le32(-1);
	memcpy(ccreq.ccode, req->alpha2, sizeof(req->alpha2));
	if (brcmf_fil_iovar_data_set(ifp, "country", &ccreq, sizeof(ccreq))) {
		brcmf_err("firmware rejected country setting\n");
		return;
	}
	brcmf_setup_wiphybands(wiphy);
}

static void brcmf_free_wiphy(struct wiphy *wiphy)
{
	int i;

	if (!wiphy)
		return;

	if (wiphy->iface_combinations) {
		for (i = 0; i < wiphy->n_iface_combinations; i++)
			kfree(wiphy->iface_combinations[i].limits);
	}
	kfree(wiphy->iface_combinations);
	if (wiphy->bands[IEEE80211_BAND_2GHZ]) {
		kfree(wiphy->bands[IEEE80211_BAND_2GHZ]->channels);
		kfree(wiphy->bands[IEEE80211_BAND_2GHZ]);
	}
	if (wiphy->bands[IEEE80211_BAND_5GHZ]) {
		kfree(wiphy->bands[IEEE80211_BAND_5GHZ]->channels);
		kfree(wiphy->bands[IEEE80211_BAND_5GHZ]);
	}
	wiphy_free(wiphy);
}

struct brcmf_cfg80211_info *brcmf_cfg80211_attach(struct brcmf_pub *drvr,
						  struct device *busdev,
						  bool p2pdev_forced)
{
	struct net_device *ndev = brcmf_get_ifp(drvr, 0)->ndev;
	struct brcmf_cfg80211_info *cfg;
	struct wiphy *wiphy;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	s32 err = 0;
	s32 io_type;
	u16 *cap = NULL;

	if (!ndev) {
		brcmf_err("ndev is invalid\n");
		return NULL;
	}

	ifp = netdev_priv(ndev);
	wiphy = wiphy_new(&wl_cfg80211_ops, sizeof(struct brcmf_cfg80211_info));
	if (!wiphy) {
		brcmf_err("Could not allocate wiphy device\n");
		return NULL;
	}
	memcpy(wiphy->perm_addr, drvr->mac, ETH_ALEN);
	set_wiphy_dev(wiphy, busdev);

	cfg = wiphy_priv(wiphy);
	cfg->wiphy = wiphy;
	cfg->pub = drvr;
	init_vif_event(&cfg->vif_event);
	INIT_LIST_HEAD(&cfg->vif_list);

	vif = brcmf_alloc_vif(cfg, NL80211_IFTYPE_STATION, false);
	if (IS_ERR(vif))
		goto wiphy_out;

	vif->ifp = ifp;
	vif->wdev.netdev = ndev;
	ndev->ieee80211_ptr = &vif->wdev;
	SET_NETDEV_DEV(ndev, wiphy_dev(cfg->wiphy));

	err = wl_init_priv(cfg);
	if (err) {
		brcmf_err("Failed to init iwm_priv (%d)\n", err);
		brcmf_free_vif(vif);
		goto wiphy_out;
	}
	ifp->vif = vif;

	/* determine d11 io type before wiphy setup */
	err = brcmf_fil_cmd_int_get(ifp, BRCMF_C_GET_VERSION, &io_type);
	if (err) {
		brcmf_err("Failed to get D11 version (%d)\n", err);
		goto priv_out;
	}
	cfg->d11inf.io_type = (u8)io_type;
	brcmu_d11_attach(&cfg->d11inf);

	err = brcmf_setup_wiphy(wiphy, ifp);
	if (err < 0)
		goto priv_out;

	brcmf_dbg(INFO, "Registering custom regulatory\n");
	wiphy->reg_notifier = brcmf_cfg80211_reg_notifier;
	wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG;
	wiphy_apply_custom_regulatory(wiphy, &brcmf_regdom);

	/* firmware defaults to 40MHz disabled in 2G band. We signal
	 * cfg80211 here that we do and have it decide we can enable
	 * it. But first check if device does support 2G operation.
	 */
	if (wiphy->bands[IEEE80211_BAND_2GHZ]) {
		cap = &wiphy->bands[IEEE80211_BAND_2GHZ]->ht_cap.cap;
		*cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
	}
	err = wiphy_register(wiphy);
	if (err < 0) {
		brcmf_err("Could not register wiphy device (%d)\n", err);
		goto priv_out;
	}

	/* If cfg80211 didn't disable 40MHz HT CAP in wiphy_register(),
	 * setup 40MHz in 2GHz band and enable OBSS scanning.
	 */
	if (cap && (*cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40)) {
		err = brcmf_enable_bw40_2g(cfg);
		if (!err)
			err = brcmf_fil_iovar_int_set(ifp, "obss_coex",
						      BRCMF_OBSS_COEX_AUTO);
		else
			*cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;
	}
	/* p2p might require that "if-events" get processed by fweh. So
	 * activate the already registered event handlers now and activate
	 * the rest when initialization has completed. drvr->config needs to
	 * be assigned before activating events.
	 */
	drvr->config = cfg;
	err = brcmf_fweh_activate_events(ifp);
	if (err) {
		brcmf_err("FWEH activation failed (%d)\n", err);
		goto wiphy_unreg_out;
	}

	err = brcmf_p2p_attach(cfg, p2pdev_forced);
	if (err) {
		brcmf_err("P2P initilisation failed (%d)\n", err);
		goto wiphy_unreg_out;
	}
	err = brcmf_btcoex_attach(cfg);
	if (err) {
		brcmf_err("BT-coex initialisation failed (%d)\n", err);
		brcmf_p2p_detach(&cfg->p2p);
		goto wiphy_unreg_out;
	}

	if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_TDLS)) {
		err = brcmf_fil_iovar_int_set(ifp, "tdls_enable", 1);
		if (err) {
			brcmf_dbg(INFO, "TDLS not enabled (%d)\n", err);
			wiphy->flags &= ~WIPHY_FLAG_SUPPORTS_TDLS;
		} else {
			brcmf_fweh_register(cfg->pub, BRCMF_E_TDLS_PEER_EVENT,
					    brcmf_notify_tdls_peer_event);
		}
	}

	/* (re-) activate FWEH event handling */
	err = brcmf_fweh_activate_events(ifp);
	if (err) {
		brcmf_err("FWEH activation failed (%d)\n", err);
		goto wiphy_unreg_out;
	}

	/* Fill in some of the advertised nl80211 supported features */
	if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_SCAN_RANDOM_MAC)) {
		wiphy->features |= NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR;
#ifdef CONFIG_PM
		if (wiphy->wowlan &&
		    wiphy->wowlan->flags & WIPHY_WOWLAN_NET_DETECT)
			wiphy->features |= NL80211_FEATURE_ND_RANDOM_MAC_ADDR;
#endif
	}

	return cfg;

wiphy_unreg_out:
	wiphy_unregister(cfg->wiphy);
priv_out:
	wl_deinit_priv(cfg);
	brcmf_free_vif(vif);
	ifp->vif = NULL;
wiphy_out:
	brcmf_free_wiphy(wiphy);
	return NULL;
}

void brcmf_cfg80211_detach(struct brcmf_cfg80211_info *cfg)
{
	if (!cfg)
		return;

	brcmf_btcoex_detach(cfg);
	wiphy_unregister(cfg->wiphy);
	wl_deinit_priv(cfg);
	brcmf_free_wiphy(cfg->wiphy);
}
