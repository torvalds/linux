/*
 * Implement cfg80211 ("iw") support.
 *
 * Copyright (C) 2009 M&N Solutions GmbH, 61191 Rosbach, Germany
 * Holger Schurig <hs4233@mail.mn-solutions.de>
 *
 */

#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <asm/unaligned.h>

#include "decl.h"
#include "cfg.h"
#include "cmd.h"


#define CHAN2G(_channel, _freq, _flags) {        \
	.band             = IEEE80211_BAND_2GHZ, \
	.center_freq      = (_freq),             \
	.hw_value         = (_channel),          \
	.flags            = (_flags),            \
	.max_antenna_gain = 0,                   \
	.max_power        = 30,                  \
}

static struct ieee80211_channel lbs_2ghz_channels[] = {
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

#define RATETAB_ENT(_rate, _hw_value, _flags) { \
	.bitrate  = (_rate),                    \
	.hw_value = (_hw_value),                \
	.flags    = (_flags),                   \
}


/* Table 6 in section 3.2.1.1 */
static struct ieee80211_rate lbs_rates[] = {
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

static struct ieee80211_supported_band lbs_band_2ghz = {
	.channels = lbs_2ghz_channels,
	.n_channels = ARRAY_SIZE(lbs_2ghz_channels),
	.bitrates = lbs_rates,
	.n_bitrates = ARRAY_SIZE(lbs_rates),
};


static const u32 cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
};

/* Time to stay on the channel */
#define LBS_DWELL_PASSIVE 100
#define LBS_DWELL_ACTIVE  40


/***************************************************************************
 * Misc utility functions
 *
 * TLVs are Marvell specific. They are very similar to IEs, they have the
 * same structure: type, length, data*. The only difference: for IEs, the
 * type and length are u8, but for TLVs they're __le16.
 */

/*
 * Convert NL80211's auth_type to the one from Libertas, see chapter 5.9.1
 * in the firmware spec
 */
static u8 lbs_auth_to_authtype(enum nl80211_auth_type auth_type)
{
	int ret = -ENOTSUPP;

	switch (auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
	case NL80211_AUTHTYPE_SHARED_KEY:
		ret = auth_type;
		break;
	case NL80211_AUTHTYPE_AUTOMATIC:
		ret = NL80211_AUTHTYPE_OPEN_SYSTEM;
		break;
	case NL80211_AUTHTYPE_NETWORK_EAP:
		ret = 0x80;
		break;
	default:
		/* silence compiler */
		break;
	}
	return ret;
}


/* Various firmware commands need the list of supported rates, but with
   the hight-bit set for basic rates */
static int lbs_add_rates(u8 *rates)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(lbs_rates); i++) {
		u8 rate = lbs_rates[i].bitrate / 5;
		if (rate == 0x02 || rate == 0x04 ||
		    rate == 0x0b || rate == 0x16)
			rate |= 0x80;
		rates[i] = rate;
	}
	return ARRAY_SIZE(lbs_rates);
}


/***************************************************************************
 * TLV utility functions
 *
 * TLVs are Marvell specific. They are very similar to IEs, they have the
 * same structure: type, length, data*. The only difference: for IEs, the
 * type and length are u8, but for TLVs they're __le16.
 */


/*
 * Add ssid TLV
 */
#define LBS_MAX_SSID_TLV_SIZE			\
	(sizeof(struct mrvl_ie_header)		\
	 + IEEE80211_MAX_SSID_LEN)

static int lbs_add_ssid_tlv(u8 *tlv, const u8 *ssid, int ssid_len)
{
	struct mrvl_ie_ssid_param_set *ssid_tlv = (void *)tlv;

	/*
	 * TLV-ID SSID  00 00
	 * length       06 00
	 * ssid         4d 4e 54 45 53 54
	 */
	ssid_tlv->header.type = cpu_to_le16(TLV_TYPE_SSID);
	ssid_tlv->header.len = cpu_to_le16(ssid_len);
	memcpy(ssid_tlv->ssid, ssid, ssid_len);
	return sizeof(ssid_tlv->header) + ssid_len;
}


/*
 * Add channel list TLV (section 8.4.2)
 *
 * Actual channel data comes from priv->wdev->wiphy->channels.
 */
#define LBS_MAX_CHANNEL_LIST_TLV_SIZE					\
	(sizeof(struct mrvl_ie_header)					\
	 + (LBS_SCAN_BEFORE_NAP * sizeof(struct chanscanparamset)))

static int lbs_add_channel_list_tlv(struct lbs_private *priv, u8 *tlv,
				    int last_channel, int active_scan)
{
	int chanscanparamsize = sizeof(struct chanscanparamset) *
		(last_channel - priv->scan_channel);

	struct mrvl_ie_header *header = (void *) tlv;

	/*
	 * TLV-ID CHANLIST  01 01
	 * length           0e 00
	 * channel          00 01 00 00 00 64 00
	 *   radio type     00
	 *   channel           01
	 *   scan type            00
	 *   min scan time           00 00
	 *   max scan time                 64 00
	 * channel 2        00 02 00 00 00 64 00
	 *
	 */

	header->type = cpu_to_le16(TLV_TYPE_CHANLIST);
	header->len  = cpu_to_le16(chanscanparamsize);
	tlv += sizeof(struct mrvl_ie_header);

	/* lbs_deb_scan("scan: channels %d to %d\n", priv->scan_channel,
		     last_channel); */
	memset(tlv, 0, chanscanparamsize);

	while (priv->scan_channel < last_channel) {
		struct chanscanparamset *param = (void *) tlv;

		param->radiotype = CMD_SCAN_RADIO_TYPE_BG;
		param->channumber =
			priv->scan_req->channels[priv->scan_channel]->hw_value;
		if (active_scan) {
			param->maxscantime = cpu_to_le16(LBS_DWELL_ACTIVE);
		} else {
			param->chanscanmode.passivescan = 1;
			param->maxscantime = cpu_to_le16(LBS_DWELL_PASSIVE);
		}
		tlv += sizeof(struct chanscanparamset);
		priv->scan_channel++;
	}
	return sizeof(struct mrvl_ie_header) + chanscanparamsize;
}


/*
 * Add rates TLV
 *
 * The rates are in lbs_bg_rates[], but for the 802.11b
 * rates the high bit is set. We add this TLV only because
 * there's a firmware which otherwise doesn't report all
 * APs in range.
 */
#define LBS_MAX_RATES_TLV_SIZE			\
	(sizeof(struct mrvl_ie_header)		\
	 + (ARRAY_SIZE(lbs_rates)))

/* Adds a TLV with all rates the hardware supports */
static int lbs_add_supported_rates_tlv(u8 *tlv)
{
	size_t i;
	struct mrvl_ie_rates_param_set *rate_tlv = (void *)tlv;

	/*
	 * TLV-ID RATES  01 00
	 * length        0e 00
	 * rates         82 84 8b 96 0c 12 18 24 30 48 60 6c
	 */
	rate_tlv->header.type = cpu_to_le16(TLV_TYPE_RATES);
	tlv += sizeof(rate_tlv->header);
	i = lbs_add_rates(tlv);
	tlv += i;
	rate_tlv->header.len = cpu_to_le16(i);
	return sizeof(rate_tlv->header) + i;
}

/* Add common rates from a TLV and return the new end of the TLV */
static u8 *
add_ie_rates(u8 *tlv, const u8 *ie, int *nrates)
{
	int hw, ap, ap_max = ie[1];
	u8 hw_rate;

	/* Advance past IE header */
	ie += 2;

	lbs_deb_hex(LBS_DEB_ASSOC, "AP IE Rates", (u8 *) ie, ap_max);

	for (hw = 0; hw < ARRAY_SIZE(lbs_rates); hw++) {
		hw_rate = lbs_rates[hw].bitrate / 5;
		for (ap = 0; ap < ap_max; ap++) {
			if (hw_rate == (ie[ap] & 0x7f)) {
				*tlv++ = ie[ap];
				*nrates = *nrates + 1;
			}
		}
	}
	return tlv;
}

/*
 * Adds a TLV with all rates the hardware *and* BSS supports.
 */
static int lbs_add_common_rates_tlv(u8 *tlv, struct cfg80211_bss *bss)
{
	struct mrvl_ie_rates_param_set *rate_tlv = (void *)tlv;
	const u8 *rates_eid, *ext_rates_eid;
	int n = 0;

	rates_eid = ieee80211_bss_get_ie(bss, WLAN_EID_SUPP_RATES);
	ext_rates_eid = ieee80211_bss_get_ie(bss, WLAN_EID_EXT_SUPP_RATES);

	/*
	 * 01 00                   TLV_TYPE_RATES
	 * 04 00                   len
	 * 82 84 8b 96             rates
	 */
	rate_tlv->header.type = cpu_to_le16(TLV_TYPE_RATES);
	tlv += sizeof(rate_tlv->header);

	/* Add basic rates */
	if (rates_eid) {
		tlv = add_ie_rates(tlv, rates_eid, &n);

		/* Add extended rates, if any */
		if (ext_rates_eid)
			tlv = add_ie_rates(tlv, ext_rates_eid, &n);
	} else {
		lbs_deb_assoc("assoc: bss had no basic rate IE\n");
		/* Fallback: add basic 802.11b rates */
		*tlv++ = 0x82;
		*tlv++ = 0x84;
		*tlv++ = 0x8b;
		*tlv++ = 0x96;
		n = 4;
	}

	rate_tlv->header.len = cpu_to_le16(n);
	return sizeof(rate_tlv->header) + n;
}


/*
 * Add auth type TLV.
 *
 * This is only needed for newer firmware (V9 and up).
 */
#define LBS_MAX_AUTH_TYPE_TLV_SIZE \
	sizeof(struct mrvl_ie_auth_type)

static int lbs_add_auth_type_tlv(u8 *tlv, enum nl80211_auth_type auth_type)
{
	struct mrvl_ie_auth_type *auth = (void *) tlv;

	/*
	 * 1f 01  TLV_TYPE_AUTH_TYPE
	 * 01 00  len
	 * 01     auth type
	 */
	auth->header.type = cpu_to_le16(TLV_TYPE_AUTH_TYPE);
	auth->header.len = cpu_to_le16(sizeof(*auth)-sizeof(auth->header));
	auth->auth = cpu_to_le16(lbs_auth_to_authtype(auth_type));
	return sizeof(*auth);
}


/*
 * Add channel (phy ds) TLV
 */
#define LBS_MAX_CHANNEL_TLV_SIZE \
	sizeof(struct mrvl_ie_header)

static int lbs_add_channel_tlv(u8 *tlv, u8 channel)
{
	struct mrvl_ie_ds_param_set *ds = (void *) tlv;

	/*
	 * 03 00  TLV_TYPE_PHY_DS
	 * 01 00  len
	 * 06     channel
	 */
	ds->header.type = cpu_to_le16(TLV_TYPE_PHY_DS);
	ds->header.len = cpu_to_le16(sizeof(*ds)-sizeof(ds->header));
	ds->channel = channel;
	return sizeof(*ds);
}


/*
 * Add (empty) CF param TLV of the form:
 */
#define LBS_MAX_CF_PARAM_TLV_SIZE		\
	sizeof(struct mrvl_ie_header)

static int lbs_add_cf_param_tlv(u8 *tlv)
{
	struct mrvl_ie_cf_param_set *cf = (void *)tlv;

	/*
	 * 04 00  TLV_TYPE_CF
	 * 06 00  len
	 * 00     cfpcnt
	 * 00     cfpperiod
	 * 00 00  cfpmaxduration
	 * 00 00  cfpdurationremaining
	 */
	cf->header.type = cpu_to_le16(TLV_TYPE_CF);
	cf->header.len = cpu_to_le16(sizeof(*cf)-sizeof(cf->header));
	return sizeof(*cf);
}

/*
 * Add WPA TLV
 */
#define LBS_MAX_WPA_TLV_SIZE			\
	(sizeof(struct mrvl_ie_header)		\
	 + 128 /* TODO: I guessed the size */)

static int lbs_add_wpa_tlv(u8 *tlv, const u8 *ie, u8 ie_len)
{
	size_t tlv_len;

	/*
	 * We need just convert an IE to an TLV. IEs use u8 for the header,
	 *   u8      type
	 *   u8      len
	 *   u8[]    data
	 * but TLVs use __le16 instead:
	 *   __le16  type
	 *   __le16  len
	 *   u8[]    data
	 */
	*tlv++ = *ie++;
	*tlv++ = 0;
	tlv_len = *tlv++ = *ie++;
	*tlv++ = 0;
	while (tlv_len--)
		*tlv++ = *ie++;
	/* the TLV is two bytes larger than the IE */
	return ie_len + 2;
}

/***************************************************************************
 * Set Channel
 */

static int lbs_cfg_set_channel(struct wiphy *wiphy,
	struct net_device *netdev,
	struct ieee80211_channel *channel,
	enum nl80211_channel_type channel_type)
{
	struct lbs_private *priv = wiphy_priv(wiphy);
	int ret = -ENOTSUPP;

	lbs_deb_enter_args(LBS_DEB_CFG80211, "freq %d, type %d",
			   channel->center_freq, channel_type);

	if (channel_type != NL80211_CHAN_NO_HT)
		goto out;

	ret = lbs_set_channel(priv, channel->hw_value);

 out:
	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ret;
}



/***************************************************************************
 * Scanning
 */

/*
 * When scanning, the firmware doesn't send a nul packet with the power-safe
 * bit to the AP. So we cannot stay away from our current channel too long,
 * otherwise we loose data. So take a "nap" while scanning every other
 * while.
 */
#define LBS_SCAN_BEFORE_NAP 4


/*
 * When the firmware reports back a scan-result, it gives us an "u8 rssi",
 * which isn't really an RSSI, as it becomes larger when moving away from
 * the AP. Anyway, we need to convert that into mBm.
 */
#define LBS_SCAN_RSSI_TO_MBM(rssi) \
	((-(int)rssi + 3)*100)

static int lbs_ret_scan(struct lbs_private *priv, unsigned long dummy,
	struct cmd_header *resp)
{
	struct cmd_ds_802_11_scan_rsp *scanresp = (void *)resp;
	int bsssize;
	const u8 *pos;
	const u8 *tsfdesc;
	int tsfsize;
	int i;
	int ret = -EILSEQ;

	lbs_deb_enter(LBS_DEB_CFG80211);

	bsssize = get_unaligned_le16(&scanresp->bssdescriptsize);

	lbs_deb_scan("scan response: %d BSSs (%d bytes); resp size %d bytes\n",
			scanresp->nr_sets, bsssize, le16_to_cpu(resp->size));

	if (scanresp->nr_sets == 0) {
		ret = 0;
		goto done;
	}

	/*
	 * The general layout of the scan response is described in chapter
	 * 5.7.1. Basically we have a common part, then any number of BSS
	 * descriptor sections. Finally we have section with the same number
	 * of TSFs.
	 *
	 * cmd_ds_802_11_scan_rsp
	 *   cmd_header
	 *   pos_size
	 *   nr_sets
	 *   bssdesc 1
	 *     bssid
	 *     rssi
	 *     timestamp
	 *     intvl
	 *     capa
	 *     IEs
	 *   bssdesc 2
	 *   bssdesc n
	 *   MrvlIEtypes_TsfFimestamp_t
	 *     TSF for BSS 1
	 *     TSF for BSS 2
	 *     TSF for BSS n
	 */

	pos = scanresp->bssdesc_and_tlvbuffer;

	lbs_deb_hex(LBS_DEB_SCAN, "SCAN_RSP", scanresp->bssdesc_and_tlvbuffer,
			scanresp->bssdescriptsize);

	tsfdesc = pos + bsssize;
	tsfsize = 4 + 8 * scanresp->nr_sets;
	lbs_deb_hex(LBS_DEB_SCAN, "SCAN_TSF", (u8 *) tsfdesc, tsfsize);

	/* Validity check: we expect a Marvell-Local TLV */
	i = get_unaligned_le16(tsfdesc);
	tsfdesc += 2;
	if (i != TLV_TYPE_TSFTIMESTAMP) {
		lbs_deb_scan("scan response: invalid TSF Timestamp %d\n", i);
		goto done;
	}

	/* Validity check: the TLV holds TSF values with 8 bytes each, so
	 * the size in the TLV must match the nr_sets value */
	i = get_unaligned_le16(tsfdesc);
	tsfdesc += 2;
	if (i / 8 != scanresp->nr_sets) {
		lbs_deb_scan("scan response: invalid number of TSF timestamp "
			     "sets (expected %d got %d)\n", scanresp->nr_sets,
			     i / 8);
		goto done;
	}

	for (i = 0; i < scanresp->nr_sets; i++) {
		const u8 *bssid;
		const u8 *ie;
		int left;
		int ielen;
		int rssi;
		u16 intvl;
		u16 capa;
		int chan_no = -1;
		const u8 *ssid = NULL;
		u8 ssid_len = 0;
		DECLARE_SSID_BUF(ssid_buf);

		int len = get_unaligned_le16(pos);
		pos += 2;

		/* BSSID */
		bssid = pos;
		pos += ETH_ALEN;
		/* RSSI */
		rssi = *pos++;
		/* Packet time stamp */
		pos += 8;
		/* Beacon interval */
		intvl = get_unaligned_le16(pos);
		pos += 2;
		/* Capabilities */
		capa = get_unaligned_le16(pos);
		pos += 2;

		/* To find out the channel, we must parse the IEs */
		ie = pos;
		/* 6+1+8+2+2: size of BSSID, RSSI, time stamp, beacon
		   interval, capabilities */
		ielen = left = len - (6 + 1 + 8 + 2 + 2);
		while (left >= 2) {
			u8 id, elen;
			id = *pos++;
			elen = *pos++;
			left -= 2;
			if (elen > left || elen == 0) {
				lbs_deb_scan("scan response: invalid IE fmt\n");
				goto done;
			}

			if (id == WLAN_EID_DS_PARAMS)
				chan_no = *pos;
			if (id == WLAN_EID_SSID) {
				ssid = pos;
				ssid_len = elen;
			}
			left -= elen;
			pos += elen;
		}

		/* No channel, no luck */
		if (chan_no != -1) {
			struct wiphy *wiphy = priv->wdev->wiphy;
			int freq = ieee80211_channel_to_frequency(chan_no);
			struct ieee80211_channel *channel =
				ieee80211_get_channel(wiphy, freq);

			lbs_deb_scan("scan: %pM, capa %04x, chan %2d, %s, "
				     "%d dBm\n",
				     bssid, capa, chan_no,
				     print_ssid(ssid_buf, ssid, ssid_len),
				     LBS_SCAN_RSSI_TO_MBM(rssi)/100);

			if (channel &&
			    !(channel->flags & IEEE80211_CHAN_DISABLED))
				cfg80211_inform_bss(wiphy, channel,
					bssid, le64_to_cpu(*(__le64 *)tsfdesc),
					capa, intvl, ie, ielen,
					LBS_SCAN_RSSI_TO_MBM(rssi),
					GFP_KERNEL);
		} else
			lbs_deb_scan("scan response: missing BSS channel IE\n");

		tsfdesc += 8;
	}
	ret = 0;

 done:
	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", ret);
	return ret;
}


/*
 * Our scan command contains a TLV, consting of a SSID TLV, a channel list
 * TLV and a rates TLV. Determine the maximum size of them:
 */
#define LBS_SCAN_MAX_CMD_SIZE			\
	(sizeof(struct cmd_ds_802_11_scan)	\
	 + LBS_MAX_SSID_TLV_SIZE		\
	 + LBS_MAX_CHANNEL_LIST_TLV_SIZE	\
	 + LBS_MAX_RATES_TLV_SIZE)

/*
 * Assumes priv->scan_req is initialized and valid
 * Assumes priv->scan_channel is initialized
 */
static void lbs_scan_worker(struct work_struct *work)
{
	struct lbs_private *priv =
		container_of(work, struct lbs_private, scan_work.work);
	struct cmd_ds_802_11_scan *scan_cmd;
	u8 *tlv; /* pointer into our current, growing TLV storage area */
	int last_channel;
	int running, carrier;

	lbs_deb_enter(LBS_DEB_SCAN);

	scan_cmd = kzalloc(LBS_SCAN_MAX_CMD_SIZE, GFP_KERNEL);
	if (scan_cmd == NULL)
		goto out_no_scan_cmd;

	/* prepare fixed part of scan command */
	scan_cmd->bsstype = CMD_BSS_TYPE_ANY;

	/* stop network while we're away from our main channel */
	running = !netif_queue_stopped(priv->dev);
	carrier = netif_carrier_ok(priv->dev);
	if (running)
		netif_stop_queue(priv->dev);
	if (carrier)
		netif_carrier_off(priv->dev);

	/* prepare fixed part of scan command */
	tlv = scan_cmd->tlvbuffer;

	/* add SSID TLV */
	if (priv->scan_req->n_ssids)
		tlv += lbs_add_ssid_tlv(tlv,
					priv->scan_req->ssids[0].ssid,
					priv->scan_req->ssids[0].ssid_len);

	/* add channel TLVs */
	last_channel = priv->scan_channel + LBS_SCAN_BEFORE_NAP;
	if (last_channel > priv->scan_req->n_channels)
		last_channel = priv->scan_req->n_channels;
	tlv += lbs_add_channel_list_tlv(priv, tlv, last_channel,
		priv->scan_req->n_ssids);

	/* add rates TLV */
	tlv += lbs_add_supported_rates_tlv(tlv);

	if (priv->scan_channel < priv->scan_req->n_channels) {
		cancel_delayed_work(&priv->scan_work);
		if (!priv->stopping)
			queue_delayed_work(priv->work_thread, &priv->scan_work,
				msecs_to_jiffies(300));
	}

	/* This is the final data we are about to send */
	scan_cmd->hdr.size = cpu_to_le16(tlv - (u8 *)scan_cmd);
	lbs_deb_hex(LBS_DEB_SCAN, "SCAN_CMD", (void *)scan_cmd,
		    sizeof(*scan_cmd));
	lbs_deb_hex(LBS_DEB_SCAN, "SCAN_TLV", scan_cmd->tlvbuffer,
		    tlv - scan_cmd->tlvbuffer);

	__lbs_cmd(priv, CMD_802_11_SCAN, &scan_cmd->hdr,
		le16_to_cpu(scan_cmd->hdr.size),
		lbs_ret_scan, 0);

	if (priv->scan_channel >= priv->scan_req->n_channels) {
		/* Mark scan done */
		if (priv->internal_scan)
			kfree(priv->scan_req);
		else
			cfg80211_scan_done(priv->scan_req, false);

		priv->scan_req = NULL;
		priv->last_scan = jiffies;
	}

	/* Restart network */
	if (carrier)
		netif_carrier_on(priv->dev);
	if (running && !priv->tx_pending_len)
		netif_wake_queue(priv->dev);

	kfree(scan_cmd);

	/* Wake up anything waiting on scan completion */
	if (priv->scan_req == NULL) {
		lbs_deb_scan("scan: waking up waiters\n");
		wake_up_all(&priv->scan_q);
	}

 out_no_scan_cmd:
	lbs_deb_leave(LBS_DEB_SCAN);
}

static void _internal_start_scan(struct lbs_private *priv, bool internal,
	struct cfg80211_scan_request *request)
{
	lbs_deb_enter(LBS_DEB_CFG80211);

	lbs_deb_scan("scan: ssids %d, channels %d, ie_len %zd\n",
		request->n_ssids, request->n_channels, request->ie_len);

	priv->scan_channel = 0;
	queue_delayed_work(priv->work_thread, &priv->scan_work,
		msecs_to_jiffies(50));

	priv->scan_req = request;
	priv->internal_scan = internal;

	lbs_deb_leave(LBS_DEB_CFG80211);
}

static int lbs_cfg_scan(struct wiphy *wiphy,
	struct net_device *dev,
	struct cfg80211_scan_request *request)
{
	struct lbs_private *priv = wiphy_priv(wiphy);
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CFG80211);

	if (priv->scan_req || delayed_work_pending(&priv->scan_work)) {
		/* old scan request not yet processed */
		ret = -EAGAIN;
		goto out;
	}

	_internal_start_scan(priv, false, request);

	if (priv->surpriseremoved)
		ret = -EIO;

 out:
	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ret;
}




/***************************************************************************
 * Events
 */

void lbs_send_disconnect_notification(struct lbs_private *priv)
{
	lbs_deb_enter(LBS_DEB_CFG80211);

	cfg80211_disconnected(priv->dev,
		0,
		NULL, 0,
		GFP_KERNEL);

	lbs_deb_leave(LBS_DEB_CFG80211);
}

void lbs_send_mic_failureevent(struct lbs_private *priv, u32 event)
{
	lbs_deb_enter(LBS_DEB_CFG80211);

	cfg80211_michael_mic_failure(priv->dev,
		priv->assoc_bss,
		event == MACREG_INT_CODE_MIC_ERR_MULTICAST ?
			NL80211_KEYTYPE_GROUP :
			NL80211_KEYTYPE_PAIRWISE,
		-1,
		NULL,
		GFP_KERNEL);

	lbs_deb_leave(LBS_DEB_CFG80211);
}




/***************************************************************************
 * Connect/disconnect
 */


/*
 * This removes all WEP keys
 */
static int lbs_remove_wep_keys(struct lbs_private *priv)
{
	struct cmd_ds_802_11_set_wep cmd;
	int ret;

	lbs_deb_enter(LBS_DEB_CFG80211);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.keyindex = cpu_to_le16(priv->wep_tx_key);
	cmd.action = cpu_to_le16(CMD_ACT_REMOVE);

	ret = lbs_cmd_with_response(priv, CMD_802_11_SET_WEP, &cmd);

	lbs_deb_leave(LBS_DEB_CFG80211);
	return ret;
}

/*
 * Set WEP keys
 */
static int lbs_set_wep_keys(struct lbs_private *priv)
{
	struct cmd_ds_802_11_set_wep cmd;
	int i;
	int ret;

	lbs_deb_enter(LBS_DEB_CFG80211);

	/*
	 * command         13 00
	 * size            50 00
	 * sequence        xx xx
	 * result          00 00
	 * action          02 00     ACT_ADD
	 * transmit key    00 00
	 * type for key 1  01        WEP40
	 * type for key 2  00
	 * type for key 3  00
	 * type for key 4  00
	 * key 1           39 39 39 39 39 00 00 00
	 *                 00 00 00 00 00 00 00 00
	 * key 2           00 00 00 00 00 00 00 00
	 *                 00 00 00 00 00 00 00 00
	 * key 3           00 00 00 00 00 00 00 00
	 *                 00 00 00 00 00 00 00 00
	 * key 4           00 00 00 00 00 00 00 00
	 */
	if (priv->wep_key_len[0] || priv->wep_key_len[1] ||
	    priv->wep_key_len[2] || priv->wep_key_len[3]) {
		/* Only set wep keys if we have at least one of them */
		memset(&cmd, 0, sizeof(cmd));
		cmd.hdr.size = cpu_to_le16(sizeof(cmd));
		cmd.keyindex = cpu_to_le16(priv->wep_tx_key);
		cmd.action = cpu_to_le16(CMD_ACT_ADD);

		for (i = 0; i < 4; i++) {
			switch (priv->wep_key_len[i]) {
			case WLAN_KEY_LEN_WEP40:
				cmd.keytype[i] = CMD_TYPE_WEP_40_BIT;
				break;
			case WLAN_KEY_LEN_WEP104:
				cmd.keytype[i] = CMD_TYPE_WEP_104_BIT;
				break;
			default:
				cmd.keytype[i] = 0;
				break;
			}
			memcpy(cmd.keymaterial[i], priv->wep_key[i],
			       priv->wep_key_len[i]);
		}

		ret = lbs_cmd_with_response(priv, CMD_802_11_SET_WEP, &cmd);
	} else {
		/* Otherwise remove all wep keys */
		ret = lbs_remove_wep_keys(priv);
	}

	lbs_deb_leave(LBS_DEB_CFG80211);
	return ret;
}


/*
 * Enable/Disable RSN status
 */
static int lbs_enable_rsn(struct lbs_private *priv, int enable)
{
	struct cmd_ds_802_11_enable_rsn cmd;
	int ret;

	lbs_deb_enter_args(LBS_DEB_CFG80211, "%d", enable);

	/*
	 * cmd       2f 00
	 * size      0c 00
	 * sequence  xx xx
	 * result    00 00
	 * action    01 00    ACT_SET
	 * enable    01 00
	 */
	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_SET);
	cmd.enable = cpu_to_le16(enable);

	ret = lbs_cmd_with_response(priv, CMD_802_11_ENABLE_RSN, &cmd);

	lbs_deb_leave(LBS_DEB_CFG80211);
	return ret;
}


/*
 * Set WPA/WPA key material
 */

/* like "struct cmd_ds_802_11_key_material", but with cmd_header. Once we
 * get rid of WEXT, this should go into host.h */

struct cmd_key_material {
	struct cmd_header hdr;

	__le16 action;
	struct MrvlIEtype_keyParamSet param;
} __packed;

static int lbs_set_key_material(struct lbs_private *priv,
				int key_type,
				int key_info,
				u8 *key, u16 key_len)
{
	struct cmd_key_material cmd;
	int ret;

	lbs_deb_enter(LBS_DEB_CFG80211);

	/*
	 * Example for WPA (TKIP):
	 *
	 * cmd       5e 00
	 * size      34 00
	 * sequence  xx xx
	 * result    00 00
	 * action    01 00
	 * TLV type  00 01    key param
	 * length    00 26
	 * key type  01 00    TKIP
	 * key info  06 00    UNICAST | ENABLED
	 * key len   20 00
	 * key       32 bytes
	 */
	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_SET);
	cmd.param.type = cpu_to_le16(TLV_TYPE_KEY_MATERIAL);
	cmd.param.length = cpu_to_le16(sizeof(cmd.param) - 4);
	cmd.param.keytypeid = cpu_to_le16(key_type);
	cmd.param.keyinfo = cpu_to_le16(key_info);
	cmd.param.keylen = cpu_to_le16(key_len);
	if (key && key_len)
		memcpy(cmd.param.key, key, key_len);

	ret = lbs_cmd_with_response(priv, CMD_802_11_KEY_MATERIAL, &cmd);

	lbs_deb_leave(LBS_DEB_CFG80211);
	return ret;
}


/*
 * Sets the auth type (open, shared, etc) in the firmware. That
 * we use CMD_802_11_AUTHENTICATE is misleading, this firmware
 * command doesn't send an authentication frame at all, it just
 * stores the auth_type.
 */
static int lbs_set_authtype(struct lbs_private *priv,
			    struct cfg80211_connect_params *sme)
{
	struct cmd_ds_802_11_authenticate cmd;
	int ret;

	lbs_deb_enter_args(LBS_DEB_CFG80211, "%d", sme->auth_type);

	/*
	 * cmd        11 00
	 * size       19 00
	 * sequence   xx xx
	 * result     00 00
	 * BSS id     00 13 19 80 da 30
	 * auth type  00
	 * reserved   00 00 00 00 00 00 00 00 00 00
	 */
	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	if (sme->bssid)
		memcpy(cmd.bssid, sme->bssid, ETH_ALEN);
	/* convert auth_type */
	ret = lbs_auth_to_authtype(sme->auth_type);
	if (ret < 0)
		goto done;

	cmd.authtype = ret;
	ret = lbs_cmd_with_response(priv, CMD_802_11_AUTHENTICATE, &cmd);

 done:
	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ret;
}


/*
 * Create association request
 */
#define LBS_ASSOC_MAX_CMD_SIZE                     \
	(sizeof(struct cmd_ds_802_11_associate)    \
	 - 512 /* cmd_ds_802_11_associate.iebuf */ \
	 + LBS_MAX_SSID_TLV_SIZE                   \
	 + LBS_MAX_CHANNEL_TLV_SIZE                \
	 + LBS_MAX_CF_PARAM_TLV_SIZE               \
	 + LBS_MAX_AUTH_TYPE_TLV_SIZE              \
	 + LBS_MAX_WPA_TLV_SIZE)

static int lbs_associate(struct lbs_private *priv,
		struct cfg80211_bss *bss,
		struct cfg80211_connect_params *sme)
{
	struct cmd_ds_802_11_associate_response *resp;
	struct cmd_ds_802_11_associate *cmd = kzalloc(LBS_ASSOC_MAX_CMD_SIZE,
						      GFP_KERNEL);
	const u8 *ssid_eid;
	size_t len, resp_ie_len;
	int status;
	int ret;
	u8 *pos = &(cmd->iebuf[0]);
	u8 *tmp;

	lbs_deb_enter(LBS_DEB_CFG80211);

	if (!cmd) {
		ret = -ENOMEM;
		goto done;
	}

	/*
	 * cmd              50 00
	 * length           34 00
	 * sequence         xx xx
	 * result           00 00
	 * BSS id           00 13 19 80 da 30
	 * capabilities     11 00
	 * listen interval  0a 00
	 * beacon interval  00 00
	 * DTIM period      00
	 * TLVs             xx   (up to 512 bytes)
	 */
	cmd->hdr.command = cpu_to_le16(CMD_802_11_ASSOCIATE);

	/* Fill in static fields */
	memcpy(cmd->bssid, bss->bssid, ETH_ALEN);
	cmd->listeninterval = cpu_to_le16(MRVDRV_DEFAULT_LISTEN_INTERVAL);
	cmd->capability = cpu_to_le16(bss->capability);

	/* add SSID TLV */
	ssid_eid = ieee80211_bss_get_ie(bss, WLAN_EID_SSID);
	if (ssid_eid)
		pos += lbs_add_ssid_tlv(pos, ssid_eid + 2, ssid_eid[1]);
	else
		lbs_deb_assoc("no SSID\n");

	/* add DS param TLV */
	if (bss->channel)
		pos += lbs_add_channel_tlv(pos, bss->channel->hw_value);
	else
		lbs_deb_assoc("no channel\n");

	/* add (empty) CF param TLV */
	pos += lbs_add_cf_param_tlv(pos);

	/* add rates TLV */
	tmp = pos + 4; /* skip Marvell IE header */
	pos += lbs_add_common_rates_tlv(pos, bss);
	lbs_deb_hex(LBS_DEB_ASSOC, "Common Rates", tmp, pos - tmp);

	/* add auth type TLV */
	if (MRVL_FW_MAJOR_REV(priv->fwrelease) >= 9)
		pos += lbs_add_auth_type_tlv(pos, sme->auth_type);

	/* add WPA/WPA2 TLV */
	if (sme->ie && sme->ie_len)
		pos += lbs_add_wpa_tlv(pos, sme->ie, sme->ie_len);

	len = (sizeof(*cmd) - sizeof(cmd->iebuf)) +
		(u16)(pos - (u8 *) &cmd->iebuf);
	cmd->hdr.size = cpu_to_le16(len);

	lbs_deb_hex(LBS_DEB_ASSOC, "ASSOC_CMD", (u8 *) cmd,
			le16_to_cpu(cmd->hdr.size));

	/* store for later use */
	memcpy(priv->assoc_bss, bss->bssid, ETH_ALEN);

	ret = lbs_cmd_with_response(priv, CMD_802_11_ASSOCIATE, cmd);
	if (ret)
		goto done;

	/* generate connect message to cfg80211 */

	resp = (void *) cmd; /* recast for easier field access */
	status = le16_to_cpu(resp->statuscode);

	/* Older FW versions map the IEEE 802.11 Status Code in the association
	 * response to the following values returned in resp->statuscode:
	 *
	 *    IEEE Status Code                Marvell Status Code
	 *    0                       ->      0x0000 ASSOC_RESULT_SUCCESS
	 *    13                      ->      0x0004 ASSOC_RESULT_AUTH_REFUSED
	 *    14                      ->      0x0004 ASSOC_RESULT_AUTH_REFUSED
	 *    15                      ->      0x0004 ASSOC_RESULT_AUTH_REFUSED
	 *    16                      ->      0x0004 ASSOC_RESULT_AUTH_REFUSED
	 *    others                  ->      0x0003 ASSOC_RESULT_REFUSED
	 *
	 * Other response codes:
	 *    0x0001 -> ASSOC_RESULT_INVALID_PARAMETERS (unused)
	 *    0x0002 -> ASSOC_RESULT_TIMEOUT (internal timer expired waiting for
	 *                                    association response from the AP)
	 */
	if (MRVL_FW_MAJOR_REV(priv->fwrelease) <= 8) {
		switch (status) {
		case 0:
			break;
		case 1:
			lbs_deb_assoc("invalid association parameters\n");
			status = WLAN_STATUS_CAPS_UNSUPPORTED;
			break;
		case 2:
			lbs_deb_assoc("timer expired while waiting for AP\n");
			status = WLAN_STATUS_AUTH_TIMEOUT;
			break;
		case 3:
			lbs_deb_assoc("association refused by AP\n");
			status = WLAN_STATUS_ASSOC_DENIED_UNSPEC;
			break;
		case 4:
			lbs_deb_assoc("authentication refused by AP\n");
			status = WLAN_STATUS_UNKNOWN_AUTH_TRANSACTION;
			break;
		default:
			lbs_deb_assoc("association failure %d\n", status);
			/* v5 OLPC firmware does return the AP status code if
			 * it's not one of the values above.  Let that through.
			 */
			break;
		}
	}

	lbs_deb_assoc("status %d, statuscode 0x%04x, capability 0x%04x, "
		      "aid 0x%04x\n", status, le16_to_cpu(resp->statuscode),
		      le16_to_cpu(resp->capability), le16_to_cpu(resp->aid));

	resp_ie_len = le16_to_cpu(resp->hdr.size)
		- sizeof(resp->hdr)
		- 6;
	cfg80211_connect_result(priv->dev,
				priv->assoc_bss,
				sme->ie, sme->ie_len,
				resp->iebuf, resp_ie_len,
				status,
				GFP_KERNEL);

	if (status == 0) {
		/* TODO: get rid of priv->connect_status */
		priv->connect_status = LBS_CONNECTED;
		netif_carrier_on(priv->dev);
		if (!priv->tx_pending_len)
			netif_tx_wake_all_queues(priv->dev);
	}

done:
	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ret;
}

static struct cfg80211_scan_request *
_new_connect_scan_req(struct wiphy *wiphy, struct cfg80211_connect_params *sme)
{
	struct cfg80211_scan_request *creq = NULL;
	int i, n_channels = 0;
	enum ieee80211_band band;

	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		if (wiphy->bands[band])
			n_channels += wiphy->bands[band]->n_channels;
	}

	creq = kzalloc(sizeof(*creq) + sizeof(struct cfg80211_ssid) +
		       n_channels * sizeof(void *),
		       GFP_ATOMIC);
	if (!creq)
		return NULL;

	/* SSIDs come after channels */
	creq->ssids = (void *)&creq->channels[n_channels];
	creq->n_channels = n_channels;
	creq->n_ssids = 1;

	/* Scan all available channels */
	i = 0;
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		int j;

		if (!wiphy->bands[band])
			continue;

		for (j = 0; j < wiphy->bands[band]->n_channels; j++) {
			/* ignore disabled channels */
			if (wiphy->bands[band]->channels[j].flags &
						IEEE80211_CHAN_DISABLED)
				continue;

			creq->channels[i] = &wiphy->bands[band]->channels[j];
			i++;
		}
	}
	if (i) {
		/* Set real number of channels specified in creq->channels[] */
		creq->n_channels = i;

		/* Scan for the SSID we're going to connect to */
		memcpy(creq->ssids[0].ssid, sme->ssid, sme->ssid_len);
		creq->ssids[0].ssid_len = sme->ssid_len;
	} else {
		/* No channels found... */
		kfree(creq);
		creq = NULL;
	}

	return creq;
}

static int lbs_cfg_connect(struct wiphy *wiphy, struct net_device *dev,
			   struct cfg80211_connect_params *sme)
{
	struct lbs_private *priv = wiphy_priv(wiphy);
	struct cfg80211_bss *bss = NULL;
	int ret = 0;
	u8 preamble = RADIO_PREAMBLE_SHORT;

	lbs_deb_enter(LBS_DEB_CFG80211);

	if (!sme->bssid) {
		/* Run a scan if one isn't in-progress already and if the last
		 * scan was done more than 2 seconds ago.
		 */
		if (priv->scan_req == NULL &&
		    time_after(jiffies, priv->last_scan + (2 * HZ))) {
			struct cfg80211_scan_request *creq;

			creq = _new_connect_scan_req(wiphy, sme);
			if (!creq) {
				ret = -EINVAL;
				goto done;
			}

			lbs_deb_assoc("assoc: scanning for compatible AP\n");
			_internal_start_scan(priv, true, creq);
		}

		/* Wait for any in-progress scan to complete */
		lbs_deb_assoc("assoc: waiting for scan to complete\n");
		wait_event_interruptible_timeout(priv->scan_q,
						 (priv->scan_req == NULL),
						 (15 * HZ));
		lbs_deb_assoc("assoc: scanning competed\n");
	}

	/* Find the BSS we want using available scan results */
	bss = cfg80211_get_bss(wiphy, sme->channel, sme->bssid,
		sme->ssid, sme->ssid_len,
		WLAN_CAPABILITY_ESS, WLAN_CAPABILITY_ESS);
	if (!bss) {
		lbs_pr_err("assoc: bss %pM not in scan results\n",
			   sme->bssid);
		ret = -ENOENT;
		goto done;
	}
	lbs_deb_assoc("trying %pM\n", bss->bssid);
	lbs_deb_assoc("cipher 0x%x, key index %d, key len %d\n",
		      sme->crypto.cipher_group,
		      sme->key_idx, sme->key_len);

	/* As this is a new connection, clear locally stored WEP keys */
	priv->wep_tx_key = 0;
	memset(priv->wep_key, 0, sizeof(priv->wep_key));
	memset(priv->wep_key_len, 0, sizeof(priv->wep_key_len));

	/* set/remove WEP keys */
	switch (sme->crypto.cipher_group) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		/* Store provided WEP keys in priv-> */
		priv->wep_tx_key = sme->key_idx;
		priv->wep_key_len[sme->key_idx] = sme->key_len;
		memcpy(priv->wep_key[sme->key_idx], sme->key, sme->key_len);
		/* Set WEP keys and WEP mode */
		lbs_set_wep_keys(priv);
		priv->mac_control |= CMD_ACT_MAC_WEP_ENABLE;
		lbs_set_mac_control(priv);
		/* No RSN mode for WEP */
		lbs_enable_rsn(priv, 0);
		break;
	case 0: /* there's no WLAN_CIPHER_SUITE_NONE definition */
		/*
		 * If we don't have no WEP, no WPA and no WPA2,
		 * we remove all keys like in the WPA/WPA2 setup,
		 * we just don't set RSN.
		 *
		 * Therefore: fall-throught
		 */
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
		/* Remove WEP keys and WEP mode */
		lbs_remove_wep_keys(priv);
		priv->mac_control &= ~CMD_ACT_MAC_WEP_ENABLE;
		lbs_set_mac_control(priv);

		/* clear the WPA/WPA2 keys */
		lbs_set_key_material(priv,
			KEY_TYPE_ID_WEP, /* doesn't matter */
			KEY_INFO_WPA_UNICAST,
			NULL, 0);
		lbs_set_key_material(priv,
			KEY_TYPE_ID_WEP, /* doesn't matter */
			KEY_INFO_WPA_MCAST,
			NULL, 0);
		/* RSN mode for WPA/WPA2 */
		lbs_enable_rsn(priv, sme->crypto.cipher_group != 0);
		break;
	default:
		lbs_pr_err("unsupported cipher group 0x%x\n",
			   sme->crypto.cipher_group);
		ret = -ENOTSUPP;
		goto done;
	}

	lbs_set_authtype(priv, sme);
	lbs_set_radio(priv, preamble, 1);

	/* Do the actual association */
	ret = lbs_associate(priv, bss, sme);

 done:
	if (bss)
		cfg80211_put_bss(bss);
	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ret;
}

static int lbs_cfg_disconnect(struct wiphy *wiphy, struct net_device *dev,
	u16 reason_code)
{
	struct lbs_private *priv = wiphy_priv(wiphy);
	struct cmd_ds_802_11_deauthenticate cmd;

	lbs_deb_enter_args(LBS_DEB_CFG80211, "reason_code %d", reason_code);

	/* store for lbs_cfg_ret_disconnect() */
	priv->disassoc_reason = reason_code;

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	/* Mildly ugly to use a locally store my own BSSID ... */
	memcpy(cmd.macaddr, &priv->assoc_bss, ETH_ALEN);
	cmd.reasoncode = cpu_to_le16(reason_code);

	if (lbs_cmd_with_response(priv, CMD_802_11_DEAUTHENTICATE, &cmd))
		return -EFAULT;

	cfg80211_disconnected(priv->dev,
			priv->disassoc_reason,
			NULL, 0,
			GFP_KERNEL);
	priv->connect_status = LBS_DISCONNECTED;

	return 0;
}


static int lbs_cfg_set_default_key(struct wiphy *wiphy,
				   struct net_device *netdev,
				   u8 key_index, bool unicast,
				   bool multicast)
{
	struct lbs_private *priv = wiphy_priv(wiphy);

	lbs_deb_enter(LBS_DEB_CFG80211);

	if (key_index != priv->wep_tx_key) {
		lbs_deb_assoc("set_default_key: to %d\n", key_index);
		priv->wep_tx_key = key_index;
		lbs_set_wep_keys(priv);
	}

	return 0;
}


static int lbs_cfg_add_key(struct wiphy *wiphy, struct net_device *netdev,
			   u8 idx, bool pairwise, const u8 *mac_addr,
			   struct key_params *params)
{
	struct lbs_private *priv = wiphy_priv(wiphy);
	u16 key_info;
	u16 key_type;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CFG80211);

	lbs_deb_assoc("add_key: cipher 0x%x, mac_addr %pM\n",
		      params->cipher, mac_addr);
	lbs_deb_assoc("add_key: key index %d, key len %d\n",
		      idx, params->key_len);
	if (params->key_len)
		lbs_deb_hex(LBS_DEB_CFG80211, "KEY",
			    params->key, params->key_len);

	lbs_deb_assoc("add_key: seq len %d\n", params->seq_len);
	if (params->seq_len)
		lbs_deb_hex(LBS_DEB_CFG80211, "SEQ",
			    params->seq, params->seq_len);

	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		/* actually compare if something has changed ... */
		if ((priv->wep_key_len[idx] != params->key_len) ||
			memcmp(priv->wep_key[idx],
			       params->key, params->key_len) != 0) {
			priv->wep_key_len[idx] = params->key_len;
			memcpy(priv->wep_key[idx],
			       params->key, params->key_len);
			lbs_set_wep_keys(priv);
		}
		break;
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
		key_info = KEY_INFO_WPA_ENABLED | ((idx == 0)
						   ? KEY_INFO_WPA_UNICAST
						   : KEY_INFO_WPA_MCAST);
		key_type = (params->cipher == WLAN_CIPHER_SUITE_TKIP)
			? KEY_TYPE_ID_TKIP
			: KEY_TYPE_ID_AES;
		lbs_set_key_material(priv,
				     key_type,
				     key_info,
				     params->key, params->key_len);
		break;
	default:
		lbs_pr_err("unhandled cipher 0x%x\n", params->cipher);
		ret = -ENOTSUPP;
		break;
	}

	return ret;
}


static int lbs_cfg_del_key(struct wiphy *wiphy, struct net_device *netdev,
			   u8 key_index, bool pairwise, const u8 *mac_addr)
{

	lbs_deb_enter(LBS_DEB_CFG80211);

	lbs_deb_assoc("del_key: key_idx %d, mac_addr %pM\n",
		      key_index, mac_addr);

#ifdef TODO
	struct lbs_private *priv = wiphy_priv(wiphy);
	/*
	 * I think can keep this a NO-OP, because:

	 * - we clear all keys whenever we do lbs_cfg_connect() anyway
	 * - neither "iw" nor "wpa_supplicant" won't call this during
	 *   an ongoing connection
	 * - TODO: but I have to check if this is still true when
	 *   I set the AP to periodic re-keying
	 * - we've not kzallec() something when we've added a key at
	 *   lbs_cfg_connect() or lbs_cfg_add_key().
	 *
	 * This causes lbs_cfg_del_key() only called at disconnect time,
	 * where we'd just waste time deleting a key that is not going
	 * to be used anyway.
	 */
	if (key_index < 3 && priv->wep_key_len[key_index]) {
		priv->wep_key_len[key_index] = 0;
		lbs_set_wep_keys(priv);
	}
#endif

	return 0;
}


/***************************************************************************
 * Get station
 */

static int lbs_cfg_get_station(struct wiphy *wiphy, struct net_device *dev,
			      u8 *mac, struct station_info *sinfo)
{
	struct lbs_private *priv = wiphy_priv(wiphy);
	s8 signal, noise;
	int ret;
	size_t i;

	lbs_deb_enter(LBS_DEB_CFG80211);

	sinfo->filled |= STATION_INFO_TX_BYTES |
			 STATION_INFO_TX_PACKETS |
			 STATION_INFO_RX_BYTES |
			 STATION_INFO_RX_PACKETS;
	sinfo->tx_bytes = priv->dev->stats.tx_bytes;
	sinfo->tx_packets = priv->dev->stats.tx_packets;
	sinfo->rx_bytes = priv->dev->stats.rx_bytes;
	sinfo->rx_packets = priv->dev->stats.rx_packets;

	/* Get current RSSI */
	ret = lbs_get_rssi(priv, &signal, &noise);
	if (ret == 0) {
		sinfo->signal = signal;
		sinfo->filled |= STATION_INFO_SIGNAL;
	}

	/* Convert priv->cur_rate from hw_value to NL80211 value */
	for (i = 0; i < ARRAY_SIZE(lbs_rates); i++) {
		if (priv->cur_rate == lbs_rates[i].hw_value) {
			sinfo->txrate.legacy = lbs_rates[i].bitrate;
			sinfo->filled |= STATION_INFO_TX_BITRATE;
			break;
		}
	}

	return 0;
}




/***************************************************************************
 * "Site survey", here just current channel and noise level
 */

static int lbs_get_survey(struct wiphy *wiphy, struct net_device *dev,
	int idx, struct survey_info *survey)
{
	struct lbs_private *priv = wiphy_priv(wiphy);
	s8 signal, noise;
	int ret;

	if (idx != 0)
		ret = -ENOENT;

	lbs_deb_enter(LBS_DEB_CFG80211);

	survey->channel = ieee80211_get_channel(wiphy,
		ieee80211_channel_to_frequency(priv->channel));

	ret = lbs_get_rssi(priv, &signal, &noise);
	if (ret == 0) {
		survey->filled = SURVEY_INFO_NOISE_DBM;
		survey->noise = noise;
	}

	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ret;
}




/***************************************************************************
 * Change interface
 */

static int lbs_change_intf(struct wiphy *wiphy, struct net_device *dev,
	enum nl80211_iftype type, u32 *flags,
	       struct vif_params *params)
{
	struct lbs_private *priv = wiphy_priv(wiphy);
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CFG80211);

	switch (type) {
	case NL80211_IFTYPE_MONITOR:
		ret = lbs_set_monitor_mode(priv, 1);
		break;
	case NL80211_IFTYPE_STATION:
		if (priv->wdev->iftype == NL80211_IFTYPE_MONITOR)
			ret = lbs_set_monitor_mode(priv, 0);
		if (!ret)
			ret = lbs_set_snmp_mib(priv, SNMP_MIB_OID_BSS_TYPE, 1);
		break;
	case NL80211_IFTYPE_ADHOC:
		if (priv->wdev->iftype == NL80211_IFTYPE_MONITOR)
			ret = lbs_set_monitor_mode(priv, 0);
		if (!ret)
			ret = lbs_set_snmp_mib(priv, SNMP_MIB_OID_BSS_TYPE, 2);
		break;
	default:
		ret = -ENOTSUPP;
	}

	if (!ret)
		priv->wdev->iftype = type;

	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ret;
}



/***************************************************************************
 * IBSS (Ad-Hoc)
 */

/* The firmware needs the following bits masked out of the beacon-derived
 * capability field when associating/joining to a BSS:
 *  9 (QoS), 11 (APSD), 12 (unused), 14 (unused), 15 (unused)
 */
#define CAPINFO_MASK (~(0xda00))


static void lbs_join_post(struct lbs_private *priv,
			  struct cfg80211_ibss_params *params,
			  u8 *bssid, u16 capability)
{
	u8 fake_ie[2 + IEEE80211_MAX_SSID_LEN + /* ssid */
		   2 + 4 +                      /* basic rates */
		   2 + 1 +                      /* DS parameter */
		   2 + 2 +                      /* atim */
		   2 + 8];                      /* extended rates */
	u8 *fake = fake_ie;

	lbs_deb_enter(LBS_DEB_CFG80211);

	/*
	 * For cfg80211_inform_bss, we'll need a fake IE, as we can't get
	 * the real IE from the firmware. So we fabricate a fake IE based on
	 * what the firmware actually sends (sniffed with wireshark).
	 */
	/* Fake SSID IE */
	*fake++ = WLAN_EID_SSID;
	*fake++ = params->ssid_len;
	memcpy(fake, params->ssid, params->ssid_len);
	fake += params->ssid_len;
	/* Fake supported basic rates IE */
	*fake++ = WLAN_EID_SUPP_RATES;
	*fake++ = 4;
	*fake++ = 0x82;
	*fake++ = 0x84;
	*fake++ = 0x8b;
	*fake++ = 0x96;
	/* Fake DS channel IE */
	*fake++ = WLAN_EID_DS_PARAMS;
	*fake++ = 1;
	*fake++ = params->channel->hw_value;
	/* Fake IBSS params IE */
	*fake++ = WLAN_EID_IBSS_PARAMS;
	*fake++ = 2;
	*fake++ = 0; /* ATIM=0 */
	*fake++ = 0;
	/* Fake extended rates IE, TODO: don't add this for 802.11b only,
	 * but I don't know how this could be checked */
	*fake++ = WLAN_EID_EXT_SUPP_RATES;
	*fake++ = 8;
	*fake++ = 0x0c;
	*fake++ = 0x12;
	*fake++ = 0x18;
	*fake++ = 0x24;
	*fake++ = 0x30;
	*fake++ = 0x48;
	*fake++ = 0x60;
	*fake++ = 0x6c;
	lbs_deb_hex(LBS_DEB_CFG80211, "IE", fake_ie, fake - fake_ie);

	cfg80211_inform_bss(priv->wdev->wiphy,
			    params->channel,
			    bssid,
			    0,
			    capability,
			    params->beacon_interval,
			    fake_ie, fake - fake_ie,
			    0, GFP_KERNEL);

	memcpy(priv->wdev->ssid, params->ssid, params->ssid_len);
	priv->wdev->ssid_len = params->ssid_len;

	cfg80211_ibss_joined(priv->dev, bssid, GFP_KERNEL);

	/* TODO: consider doing this at MACREG_INT_CODE_LINK_SENSED time */
	priv->connect_status = LBS_CONNECTED;
	netif_carrier_on(priv->dev);
	if (!priv->tx_pending_len)
		netif_wake_queue(priv->dev);

	lbs_deb_leave(LBS_DEB_CFG80211);
}

static int lbs_ibss_join_existing(struct lbs_private *priv,
	struct cfg80211_ibss_params *params,
	struct cfg80211_bss *bss)
{
	const u8 *rates_eid = ieee80211_bss_get_ie(bss, WLAN_EID_SUPP_RATES);
	struct cmd_ds_802_11_ad_hoc_join cmd;
	u8 preamble = RADIO_PREAMBLE_SHORT;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CFG80211);

	/* TODO: set preamble based on scan result */
	ret = lbs_set_radio(priv, preamble, 1);
	if (ret)
		goto out;

	/*
	 * Example CMD_802_11_AD_HOC_JOIN command:
	 *
	 * command         2c 00         CMD_802_11_AD_HOC_JOIN
	 * size            65 00
	 * sequence        xx xx
	 * result          00 00
	 * bssid           02 27 27 97 2f 96
	 * ssid            49 42 53 53 00 00 00 00
	 *                 00 00 00 00 00 00 00 00
	 *                 00 00 00 00 00 00 00 00
	 *                 00 00 00 00 00 00 00 00
	 * type            02            CMD_BSS_TYPE_IBSS
	 * beacon period   64 00
	 * dtim period     00
	 * timestamp       00 00 00 00 00 00 00 00
	 * localtime       00 00 00 00 00 00 00 00
	 * IE DS           03
	 * IE DS len       01
	 * IE DS channel   01
	 * reserveed       00 00 00 00
	 * IE IBSS         06
	 * IE IBSS len     02
	 * IE IBSS atim    00 00
	 * reserved        00 00 00 00
	 * capability      02 00
	 * rates           82 84 8b 96 0c 12 18 24 30 48 60 6c 00
	 * fail timeout    ff 00
	 * probe delay     00 00
	 */
	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));

	memcpy(cmd.bss.bssid, bss->bssid, ETH_ALEN);
	memcpy(cmd.bss.ssid, params->ssid, params->ssid_len);
	cmd.bss.type = CMD_BSS_TYPE_IBSS;
	cmd.bss.beaconperiod = cpu_to_le16(params->beacon_interval);
	cmd.bss.ds.header.id = WLAN_EID_DS_PARAMS;
	cmd.bss.ds.header.len = 1;
	cmd.bss.ds.channel = params->channel->hw_value;
	cmd.bss.ibss.header.id = WLAN_EID_IBSS_PARAMS;
	cmd.bss.ibss.header.len = 2;
	cmd.bss.ibss.atimwindow = 0;
	cmd.bss.capability = cpu_to_le16(bss->capability & CAPINFO_MASK);

	/* set rates to the intersection of our rates and the rates in the
	   bss */
	if (!rates_eid) {
		lbs_add_rates(cmd.bss.rates);
	} else {
		int hw, i;
		u8 rates_max = rates_eid[1];
		u8 *rates = cmd.bss.rates;
		for (hw = 0; hw < ARRAY_SIZE(lbs_rates); hw++) {
			u8 hw_rate = lbs_rates[hw].bitrate / 5;
			for (i = 0; i < rates_max; i++) {
				if (hw_rate == (rates_eid[i+2] & 0x7f)) {
					u8 rate = rates_eid[i+2];
					if (rate == 0x02 || rate == 0x04 ||
					    rate == 0x0b || rate == 0x16)
						rate |= 0x80;
					*rates++ = rate;
				}
			}
		}
	}

	/* Only v8 and below support setting this */
	if (MRVL_FW_MAJOR_REV(priv->fwrelease) <= 8) {
		cmd.failtimeout = cpu_to_le16(MRVDRV_ASSOCIATION_TIME_OUT);
		cmd.probedelay = cpu_to_le16(CMD_SCAN_PROBE_DELAY_TIME);
	}
	ret = lbs_cmd_with_response(priv, CMD_802_11_AD_HOC_JOIN, &cmd);
	if (ret)
		goto out;

	/*
	 * This is a sample response to CMD_802_11_AD_HOC_JOIN:
	 *
	 * response        2c 80
	 * size            09 00
	 * sequence        xx xx
	 * result          00 00
	 * reserved        00
	 */
	lbs_join_post(priv, params, bss->bssid, bss->capability);

 out:
	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ret;
}



static int lbs_ibss_start_new(struct lbs_private *priv,
	struct cfg80211_ibss_params *params)
{
	struct cmd_ds_802_11_ad_hoc_start cmd;
	struct cmd_ds_802_11_ad_hoc_result *resp =
		(struct cmd_ds_802_11_ad_hoc_result *) &cmd;
	u8 preamble = RADIO_PREAMBLE_SHORT;
	int ret = 0;
	u16 capability;

	lbs_deb_enter(LBS_DEB_CFG80211);

	ret = lbs_set_radio(priv, preamble, 1);
	if (ret)
		goto out;

	/*
	 * Example CMD_802_11_AD_HOC_START command:
	 *
	 * command         2b 00         CMD_802_11_AD_HOC_START
	 * size            b1 00
	 * sequence        xx xx
	 * result          00 00
	 * ssid            54 45 53 54 00 00 00 00
	 *                 00 00 00 00 00 00 00 00
	 *                 00 00 00 00 00 00 00 00
	 *                 00 00 00 00 00 00 00 00
	 * bss type        02
	 * beacon period   64 00
	 * dtim period     00
	 * IE IBSS         06
	 * IE IBSS len     02
	 * IE IBSS atim    00 00
	 * reserved        00 00 00 00
	 * IE DS           03
	 * IE DS len       01
	 * IE DS channel   01
	 * reserved        00 00 00 00
	 * probe delay     00 00
	 * capability      02 00
	 * rates           82 84 8b 96   (basic rates with have bit 7 set)
	 *                 0c 12 18 24 30 48 60 6c
	 * padding         100 bytes
	 */
	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	memcpy(cmd.ssid, params->ssid, params->ssid_len);
	cmd.bsstype = CMD_BSS_TYPE_IBSS;
	cmd.beaconperiod = cpu_to_le16(params->beacon_interval);
	cmd.ibss.header.id = WLAN_EID_IBSS_PARAMS;
	cmd.ibss.header.len = 2;
	cmd.ibss.atimwindow = 0;
	cmd.ds.header.id = WLAN_EID_DS_PARAMS;
	cmd.ds.header.len = 1;
	cmd.ds.channel = params->channel->hw_value;
	/* Only v8 and below support setting probe delay */
	if (MRVL_FW_MAJOR_REV(priv->fwrelease) <= 8)
		cmd.probedelay = cpu_to_le16(CMD_SCAN_PROBE_DELAY_TIME);
	/* TODO: mix in WLAN_CAPABILITY_PRIVACY */
	capability = WLAN_CAPABILITY_IBSS;
	cmd.capability = cpu_to_le16(capability);
	lbs_add_rates(cmd.rates);


	ret = lbs_cmd_with_response(priv, CMD_802_11_AD_HOC_START, &cmd);
	if (ret)
		goto out;

	/*
	 * This is a sample response to CMD_802_11_AD_HOC_JOIN:
	 *
	 * response        2b 80
	 * size            14 00
	 * sequence        xx xx
	 * result          00 00
	 * reserved        00
	 * bssid           02 2b 7b 0f 86 0e
	 */
	lbs_join_post(priv, params, resp->bssid, capability);

 out:
	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ret;
}


static int lbs_join_ibss(struct wiphy *wiphy, struct net_device *dev,
		struct cfg80211_ibss_params *params)
{
	struct lbs_private *priv = wiphy_priv(wiphy);
	int ret = 0;
	struct cfg80211_bss *bss;
	DECLARE_SSID_BUF(ssid_buf);

	lbs_deb_enter(LBS_DEB_CFG80211);

	if (!params->channel) {
		ret = -ENOTSUPP;
		goto out;
	}

	ret = lbs_set_channel(priv, params->channel->hw_value);
	if (ret)
		goto out;

	/* Search if someone is beaconing. This assumes that the
	 * bss list is populated already */
	bss = cfg80211_get_bss(wiphy, params->channel, params->bssid,
		params->ssid, params->ssid_len,
		WLAN_CAPABILITY_IBSS, WLAN_CAPABILITY_IBSS);

	if (bss) {
		ret = lbs_ibss_join_existing(priv, params, bss);
		cfg80211_put_bss(bss);
	} else
		ret = lbs_ibss_start_new(priv, params);


 out:
	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ret;
}


static int lbs_leave_ibss(struct wiphy *wiphy, struct net_device *dev)
{
	struct lbs_private *priv = wiphy_priv(wiphy);
	struct cmd_ds_802_11_ad_hoc_stop cmd;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CFG80211);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	ret = lbs_cmd_with_response(priv, CMD_802_11_AD_HOC_STOP, &cmd);

	/* TODO: consider doing this at MACREG_INT_CODE_ADHOC_BCN_LOST time */
	lbs_mac_event_disconnected(priv);

	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ret;
}




/***************************************************************************
 * Initialization
 */

static struct cfg80211_ops lbs_cfg80211_ops = {
	.set_channel = lbs_cfg_set_channel,
	.scan = lbs_cfg_scan,
	.connect = lbs_cfg_connect,
	.disconnect = lbs_cfg_disconnect,
	.add_key = lbs_cfg_add_key,
	.del_key = lbs_cfg_del_key,
	.set_default_key = lbs_cfg_set_default_key,
	.get_station = lbs_cfg_get_station,
	.dump_survey = lbs_get_survey,
	.change_virtual_intf = lbs_change_intf,
	.join_ibss = lbs_join_ibss,
	.leave_ibss = lbs_leave_ibss,
};


/*
 * At this time lbs_private *priv doesn't even exist, so we just allocate
 * memory and don't initialize the wiphy further. This is postponed until we
 * can talk to the firmware and happens at registration time in
 * lbs_cfg_wiphy_register().
 */
struct wireless_dev *lbs_cfg_alloc(struct device *dev)
{
	int ret = 0;
	struct wireless_dev *wdev;

	lbs_deb_enter(LBS_DEB_CFG80211);

	wdev = kzalloc(sizeof(struct wireless_dev), GFP_KERNEL);
	if (!wdev) {
		dev_err(dev, "cannot allocate wireless device\n");
		return ERR_PTR(-ENOMEM);
	}

	wdev->wiphy = wiphy_new(&lbs_cfg80211_ops, sizeof(struct lbs_private));
	if (!wdev->wiphy) {
		dev_err(dev, "cannot allocate wiphy\n");
		ret = -ENOMEM;
		goto err_wiphy_new;
	}

	lbs_deb_leave(LBS_DEB_CFG80211);
	return wdev;

 err_wiphy_new:
	kfree(wdev);
	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ERR_PTR(ret);
}


static void lbs_cfg_set_regulatory_hint(struct lbs_private *priv)
{
	struct region_code_mapping {
		const char *cn;
		int code;
	};

	/* Section 5.17.2 */
	static const struct region_code_mapping regmap[] = {
		{"US ", 0x10}, /* US FCC */
		{"CA ", 0x20}, /* Canada */
		{"EU ", 0x30}, /* ETSI   */
		{"ES ", 0x31}, /* Spain  */
		{"FR ", 0x32}, /* France */
		{"JP ", 0x40}, /* Japan  */
	};
	size_t i;

	lbs_deb_enter(LBS_DEB_CFG80211);

	for (i = 0; i < ARRAY_SIZE(regmap); i++)
		if (regmap[i].code == priv->regioncode) {
			regulatory_hint(priv->wdev->wiphy, regmap[i].cn);
			break;
		}

	lbs_deb_leave(LBS_DEB_CFG80211);
}


/*
 * This function get's called after lbs_setup_firmware() determined the
 * firmware capabities. So we can setup the wiphy according to our
 * hardware/firmware.
 */
int lbs_cfg_register(struct lbs_private *priv)
{
	struct wireless_dev *wdev = priv->wdev;
	int ret;

	lbs_deb_enter(LBS_DEB_CFG80211);

	wdev->wiphy->max_scan_ssids = 1;
	wdev->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	wdev->wiphy->interface_modes =
			BIT(NL80211_IFTYPE_STATION) |
			BIT(NL80211_IFTYPE_ADHOC);
	if (lbs_rtap_supported(priv))
		wdev->wiphy->interface_modes |= BIT(NL80211_IFTYPE_MONITOR);

	wdev->wiphy->bands[IEEE80211_BAND_2GHZ] = &lbs_band_2ghz;

	/*
	 * We could check priv->fwcapinfo && FW_CAPINFO_WPA, but I have
	 * never seen a firmware without WPA
	 */
	wdev->wiphy->cipher_suites = cipher_suites;
	wdev->wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);
	wdev->wiphy->reg_notifier = lbs_reg_notifier;

	ret = wiphy_register(wdev->wiphy);
	if (ret < 0)
		lbs_pr_err("cannot register wiphy device\n");

	priv->wiphy_registered = true;

	ret = register_netdev(priv->dev);
	if (ret)
		lbs_pr_err("cannot register network device\n");

	INIT_DELAYED_WORK(&priv->scan_work, lbs_scan_worker);

	lbs_cfg_set_regulatory_hint(priv);

	lbs_deb_leave_args(LBS_DEB_CFG80211, "ret %d", ret);
	return ret;
}

int lbs_reg_notifier(struct wiphy *wiphy,
		struct regulatory_request *request)
{
	struct lbs_private *priv = wiphy_priv(wiphy);
	int ret;

	lbs_deb_enter_args(LBS_DEB_CFG80211, "cfg80211 regulatory domain "
			"callback for domain %c%c\n", request->alpha2[0],
			request->alpha2[1]);

	ret = lbs_set_11d_domain_info(priv, request, wiphy->bands);

	lbs_deb_leave(LBS_DEB_CFG80211);
	return ret;
}

void lbs_scan_deinit(struct lbs_private *priv)
{
	lbs_deb_enter(LBS_DEB_CFG80211);
	cancel_delayed_work_sync(&priv->scan_work);
}


void lbs_cfg_free(struct lbs_private *priv)
{
	struct wireless_dev *wdev = priv->wdev;

	lbs_deb_enter(LBS_DEB_CFG80211);

	if (!wdev)
		return;

	if (priv->wiphy_registered)
		wiphy_unregister(wdev->wiphy);

	if (wdev->wiphy)
		wiphy_free(wdev->wiphy);

	kfree(wdev);
}
