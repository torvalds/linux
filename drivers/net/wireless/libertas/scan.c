/**
  * Functions implementing wlan scan IOCTL and firmware command APIs
  *
  * IOCTL handlers as well as command preperation and response routines
  *  for sending scan commands to the firmware.
  */
#include <linux/ctype.h>
#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <linux/etherdevice.h>

#include <net/ieee80211.h>
#include <net/iw_handler.h>

#include <asm/unaligned.h>

#include "host.h"
#include "decl.h"
#include "dev.h"
#include "scan.h"
#include "join.h"

//! Approximate amount of data needed to pass a scan result back to iwlist
#define MAX_SCAN_CELL_SIZE  (IW_EV_ADDR_LEN             \
                             + IW_ESSID_MAX_SIZE        \
                             + IW_EV_UINT_LEN           \
                             + IW_EV_FREQ_LEN           \
                             + IW_EV_QUAL_LEN           \
                             + IW_ESSID_MAX_SIZE        \
                             + IW_EV_PARAM_LEN          \
                             + 40)	/* 40 for WPAIE */

//! Memory needed to store a max sized channel List TLV for a firmware scan
#define CHAN_TLV_MAX_SIZE  (sizeof(struct mrvlietypesheader)    \
                            + (MRVDRV_MAX_CHANNELS_PER_SCAN     \
                               * sizeof(struct chanscanparamset)))

//! Memory needed to store a max number/size SSID TLV for a firmware scan
#define SSID_TLV_MAX_SIZE  (1 * sizeof(struct mrvlietypes_ssidparamset))

//! Maximum memory needed for a lbs_scan_cmd_config with all TLVs at max
#define MAX_SCAN_CFG_ALLOC (sizeof(struct lbs_scan_cmd_config)  \
                            + CHAN_TLV_MAX_SIZE                 \
                            + SSID_TLV_MAX_SIZE)

//! The maximum number of channels the firmware can scan per command
#define MRVDRV_MAX_CHANNELS_PER_SCAN   14

/**
 * @brief Number of channels to scan per firmware scan command issuance.
 *
 *  Number restricted to prevent hitting the limit on the amount of scan data
 *  returned in a single firmware scan command.
 */
#define MRVDRV_CHANNELS_PER_SCAN_CMD   4

//! Scan time specified in the channel TLV for each channel for passive scans
#define MRVDRV_PASSIVE_SCAN_CHAN_TIME  100

//! Scan time specified in the channel TLV for each channel for active scans
#define MRVDRV_ACTIVE_SCAN_CHAN_TIME   100

static const u8 zeromac[ETH_ALEN] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u8 bcastmac[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };




/*********************************************************************/
/*                                                                   */
/*  Misc helper functions                                            */
/*                                                                   */
/*********************************************************************/

static inline void clear_bss_descriptor (struct bss_descriptor * bss)
{
	/* Don't blow away ->list, just BSS data */
	memset(bss, 0, offsetof(struct bss_descriptor, list));
}

/**
 *  @brief Compare two SSIDs
 *
 *  @param ssid1    A pointer to ssid to compare
 *  @param ssid2    A pointer to ssid to compare
 *
 *  @return         0: ssid is same, otherwise is different
 */
int lbs_ssid_cmp(u8 *ssid1, u8 ssid1_len, u8 *ssid2, u8 ssid2_len)
{
	if (ssid1_len != ssid2_len)
		return -1;

	return memcmp(ssid1, ssid2, ssid1_len);
}

static inline int match_bss_no_security(struct lbs_802_11_security *secinfo,
			struct bss_descriptor * match_bss)
{
	if (   !secinfo->wep_enabled
	    && !secinfo->WPAenabled
	    && !secinfo->WPA2enabled
	    && match_bss->wpa_ie[0] != MFIE_TYPE_GENERIC
	    && match_bss->rsn_ie[0] != MFIE_TYPE_RSN
	    && !(match_bss->capability & WLAN_CAPABILITY_PRIVACY)) {
		return 1;
	}
	return 0;
}

static inline int match_bss_static_wep(struct lbs_802_11_security *secinfo,
			struct bss_descriptor * match_bss)
{
	if ( secinfo->wep_enabled
	   && !secinfo->WPAenabled
	   && !secinfo->WPA2enabled
	   && (match_bss->capability & WLAN_CAPABILITY_PRIVACY)) {
		return 1;
	}
	return 0;
}

static inline int match_bss_wpa(struct lbs_802_11_security *secinfo,
			struct bss_descriptor * match_bss)
{
	if (  !secinfo->wep_enabled
	   && secinfo->WPAenabled
	   && (match_bss->wpa_ie[0] == MFIE_TYPE_GENERIC)
	   /* privacy bit may NOT be set in some APs like LinkSys WRT54G
	      && (match_bss->capability & WLAN_CAPABILITY_PRIVACY)) {
	    */
	   ) {
		return 1;
	}
	return 0;
}

static inline int match_bss_wpa2(struct lbs_802_11_security *secinfo,
			struct bss_descriptor * match_bss)
{
	if (  !secinfo->wep_enabled
	   && secinfo->WPA2enabled
	   && (match_bss->rsn_ie[0] == MFIE_TYPE_RSN)
	   /* privacy bit may NOT be set in some APs like LinkSys WRT54G
	      && (match_bss->capability & WLAN_CAPABILITY_PRIVACY)) {
	    */
	   ) {
		return 1;
	}
	return 0;
}

static inline int match_bss_dynamic_wep(struct lbs_802_11_security *secinfo,
			struct bss_descriptor * match_bss)
{
	if (  !secinfo->wep_enabled
	   && !secinfo->WPAenabled
	   && !secinfo->WPA2enabled
	   && (match_bss->wpa_ie[0] != MFIE_TYPE_GENERIC)
	   && (match_bss->rsn_ie[0] != MFIE_TYPE_RSN)
	   && (match_bss->capability & WLAN_CAPABILITY_PRIVACY)) {
		return 1;
	}
	return 0;
}

static inline int is_same_network(struct bss_descriptor *src,
				  struct bss_descriptor *dst)
{
	/* A network is only a duplicate if the channel, BSSID, and ESSID
	 * all match.  We treat all <hidden> with the same BSSID and channel
	 * as one network */
	return ((src->ssid_len == dst->ssid_len) &&
		(src->channel == dst->channel) &&
		!compare_ether_addr(src->bssid, dst->bssid) &&
		!memcmp(src->ssid, dst->ssid, src->ssid_len));
}

/**
 *  @brief Check if a scanned network compatible with the driver settings
 *
 *   WEP     WPA     WPA2    ad-hoc  encrypt                      Network
 * enabled enabled  enabled   AES     mode   privacy  WPA  WPA2  Compatible
 *    0       0        0       0      NONE      0      0    0   yes No security
 *    1       0        0       0      NONE      1      0    0   yes Static WEP
 *    0       1        0       0       x        1x     1    x   yes WPA
 *    0       0        1       0       x        1x     x    1   yes WPA2
 *    0       0        0       1      NONE      1      0    0   yes Ad-hoc AES
 *    0       0        0       0     !=NONE     1      0    0   yes Dynamic WEP
 *
 *
 *  @param priv A pointer to struct lbs_private
 *  @param index   Index in scantable to check against current driver settings
 *  @param mode    Network mode: Infrastructure or IBSS
 *
 *  @return        Index in scantable, or error code if negative
 */
static int is_network_compatible(struct lbs_private *priv,
		struct bss_descriptor * bss, u8 mode)
{
	int matched = 0;

	lbs_deb_enter(LBS_DEB_SCAN);

	if (bss->mode != mode)
		goto done;

	if ((matched = match_bss_no_security(&priv->secinfo, bss))) {
		goto done;
	} else if ((matched = match_bss_static_wep(&priv->secinfo, bss))) {
		goto done;
	} else if ((matched = match_bss_wpa(&priv->secinfo, bss))) {
		lbs_deb_scan(
		       "is_network_compatible() WPA: wpa_ie 0x%x "
		       "wpa2_ie 0x%x WEP %s WPA %s WPA2 %s "
		       "privacy 0x%x\n", bss->wpa_ie[0], bss->rsn_ie[0],
		       priv->secinfo.wep_enabled ? "e" : "d",
		       priv->secinfo.WPAenabled ? "e" : "d",
		       priv->secinfo.WPA2enabled ? "e" : "d",
		       (bss->capability & WLAN_CAPABILITY_PRIVACY));
		goto done;
	} else if ((matched = match_bss_wpa2(&priv->secinfo, bss))) {
		lbs_deb_scan(
		       "is_network_compatible() WPA2: wpa_ie 0x%x "
		       "wpa2_ie 0x%x WEP %s WPA %s WPA2 %s "
		       "privacy 0x%x\n", bss->wpa_ie[0], bss->rsn_ie[0],
		       priv->secinfo.wep_enabled ? "e" : "d",
		       priv->secinfo.WPAenabled ? "e" : "d",
		       priv->secinfo.WPA2enabled ? "e" : "d",
		       (bss->capability & WLAN_CAPABILITY_PRIVACY));
		goto done;
	} else if ((matched = match_bss_dynamic_wep(&priv->secinfo, bss))) {
		lbs_deb_scan(
		       "is_network_compatible() dynamic WEP: "
		       "wpa_ie 0x%x wpa2_ie 0x%x privacy 0x%x\n",
		       bss->wpa_ie[0], bss->rsn_ie[0],
		       (bss->capability & WLAN_CAPABILITY_PRIVACY));
		goto done;
	}

	/* bss security settings don't match those configured on card */
	lbs_deb_scan(
	       "is_network_compatible() FAILED: wpa_ie 0x%x "
	       "wpa2_ie 0x%x WEP %s WPA %s WPA2 %s privacy 0x%x\n",
	       bss->wpa_ie[0], bss->rsn_ie[0],
	       priv->secinfo.wep_enabled ? "e" : "d",
	       priv->secinfo.WPAenabled ? "e" : "d",
	       priv->secinfo.WPA2enabled ? "e" : "d",
	       (bss->capability & WLAN_CAPABILITY_PRIVACY));

done:
	lbs_deb_leave_args(LBS_DEB_SCAN, "matched: %d", matched);
	return matched;
}




/*********************************************************************/
/*                                                                   */
/*  Main scanning support                                            */
/*                                                                   */
/*********************************************************************/

void lbs_scan_worker(struct work_struct *work)
{
	struct lbs_private *priv =
		container_of(work, struct lbs_private, scan_work.work);

	lbs_deb_enter(LBS_DEB_SCAN);
	lbs_scan_networks(priv, NULL, 0);
	lbs_deb_leave(LBS_DEB_SCAN);
}


/**
 *  @brief Create a channel list for the driver to scan based on region info
 *
 *  Only used from lbs_scan_setup_scan_config()
 *
 *  Use the driver region/band information to construct a comprehensive list
 *    of channels to scan.  This routine is used for any scan that is not
 *    provided a specific channel list to scan.
 *
 *  @param priv          A pointer to struct lbs_private structure
 *  @param scanchanlist  Output parameter: resulting channel list to scan
 *  @param filteredscan  Flag indicating whether or not a BSSID or SSID filter
 *                       is being sent in the command to firmware.  Used to
 *                       increase the number of channels sent in a scan
 *                       command and to disable the firmware channel scan
 *                       filter.
 *
 *  @return              void
 */
static int lbs_scan_create_channel_list(struct lbs_private *priv,
					  struct chanscanparamset * scanchanlist,
					  u8 filteredscan)
{

	struct region_channel *scanregion;
	struct chan_freq_power *cfp;
	int rgnidx;
	int chanidx;
	int nextchan;
	u8 scantype;

	chanidx = 0;

	/* Set the default scan type to the user specified type, will later
	 *   be changed to passive on a per channel basis if restricted by
	 *   regulatory requirements (11d or 11h)
	 */
	scantype = CMD_SCAN_TYPE_ACTIVE;

	for (rgnidx = 0; rgnidx < ARRAY_SIZE(priv->region_channel); rgnidx++) {
		if (priv->enable11d &&
		    (priv->connect_status != LBS_CONNECTED) &&
		    (priv->mesh_connect_status != LBS_CONNECTED)) {
			/* Scan all the supported chan for the first scan */
			if (!priv->universal_channel[rgnidx].valid)
				continue;
			scanregion = &priv->universal_channel[rgnidx];

			/* clear the parsed_region_chan for the first scan */
			memset(&priv->parsed_region_chan, 0x00,
			       sizeof(priv->parsed_region_chan));
		} else {
			if (!priv->region_channel[rgnidx].valid)
				continue;
			scanregion = &priv->region_channel[rgnidx];
		}

		for (nextchan = 0;
		     nextchan < scanregion->nrcfp; nextchan++, chanidx++) {

			cfp = scanregion->CFP + nextchan;

			if (priv->enable11d) {
				scantype =
				    lbs_get_scan_type_11d(cfp->channel,
							   &priv->
							   parsed_region_chan);
			}

			switch (scanregion->band) {
			case BAND_B:
			case BAND_G:
			default:
				scanchanlist[chanidx].radiotype =
				    CMD_SCAN_RADIO_TYPE_BG;
				break;
			}

			if (scantype == CMD_SCAN_TYPE_PASSIVE) {
				scanchanlist[chanidx].maxscantime =
				    cpu_to_le16(MRVDRV_PASSIVE_SCAN_CHAN_TIME);
				scanchanlist[chanidx].chanscanmode.passivescan =
				    1;
			} else {
				scanchanlist[chanidx].maxscantime =
				    cpu_to_le16(MRVDRV_ACTIVE_SCAN_CHAN_TIME);
				scanchanlist[chanidx].chanscanmode.passivescan =
				    0;
			}

			scanchanlist[chanidx].channumber = cfp->channel;

			if (filteredscan) {
				scanchanlist[chanidx].chanscanmode.
				    disablechanfilt = 1;
			}
		}
	}
	return chanidx;
}


/*
 * Add SSID TLV of the form:
 *
 * TLV-ID SSID     00 00
 * length          06 00
 * ssid            4d 4e 54 45 53 54
 */
static int lbs_scan_add_ssid_tlv(u8 *tlv,
	const struct lbs_ioctl_user_scan_cfg *user_cfg)
{
	struct mrvlietypes_ssidparamset *ssid_tlv =
		(struct mrvlietypes_ssidparamset *)tlv;
	ssid_tlv->header.type = cpu_to_le16(TLV_TYPE_SSID);
	ssid_tlv->header.len = cpu_to_le16(user_cfg->ssid_len);
	memcpy(ssid_tlv->ssid, user_cfg->ssid, user_cfg->ssid_len);
	return sizeof(ssid_tlv->header) + user_cfg->ssid_len;
}


/*
 * Add CHANLIST TLV of the form
 *
 * TLV-ID CHANLIST 01 01
 * length          5b 00
 * channel 1       00 01 00 00 00 64 00
 *   radio type    00
 *   channel          01
 *   scan type           00
 *   min scan time          00 00
 *   max scan time                64 00
 * channel 2       00 02 00 00 00 64 00
 * channel 3       00 03 00 00 00 64 00
 * channel 4       00 04 00 00 00 64 00
 * channel 5       00 05 00 00 00 64 00
 * channel 6       00 06 00 00 00 64 00
 * channel 7       00 07 00 00 00 64 00
 * channel 8       00 08 00 00 00 64 00
 * channel 9       00 09 00 00 00 64 00
 * channel 10      00 0a 00 00 00 64 00
 * channel 11      00 0b 00 00 00 64 00
 * channel 12      00 0c 00 00 00 64 00
 * channel 13      00 0d 00 00 00 64 00
 *
 */
static int lbs_scan_add_chanlist_tlv(u8 *tlv,
	struct chanscanparamset *chan_list,
	int chan_count)
{
	size_t size = sizeof(struct chanscanparamset) * chan_count;
	struct mrvlietypes_chanlistparamset *chan_tlv =
		(struct mrvlietypes_chanlistparamset *) tlv;

	chan_tlv->header.type = cpu_to_le16(TLV_TYPE_CHANLIST);
	memcpy(chan_tlv->chanscanparam, chan_list, size);
	chan_tlv->header.len = cpu_to_le16(size);
	return sizeof(chan_tlv->header) + size;
}


/*
 * Add RATES TLV of the form
 *
 * TLV-ID RATES    01 00
 * length          0e 00
 * rates           82 84 8b 96 0c 12 18 24 30 48 60 6c
 *
 * The rates are in lbs_bg_rates[], but for the 802.11b
 * rates the high bit isn't set.
 */
static int lbs_scan_add_rates_tlv(u8 *tlv)
{
	int i;
	struct mrvlietypes_ratesparamset *rate_tlv =
		(struct mrvlietypes_ratesparamset *) tlv;

	rate_tlv->header.type = cpu_to_le16(TLV_TYPE_RATES);
	tlv += sizeof(rate_tlv->header);
	for (i = 0; i < MAX_RATES; i++) {
		*tlv = lbs_bg_rates[i];
		if (*tlv == 0)
			break;
		/* This code makes sure that the 802.11b rates (1 MBit/s, 2
		   MBit/s, 5.5 MBit/s and 11 MBit/s get's the high bit set.
		   Note that the values are MBit/s * 2, to mark them as
		   basic rates so that the firmware likes it better */
		if (*tlv == 0x02 || *tlv == 0x04 ||
		    *tlv == 0x0b || *tlv == 0x16)
			*tlv |= 0x80;
		tlv++;
	}
	rate_tlv->header.len = cpu_to_le16(i);
	return sizeof(rate_tlv->header) + i;
}


/*
 * Generate the CMD_802_11_SCAN command with the proper tlv
 * for a bunch of channels.
 */
static int lbs_do_scan(struct lbs_private *priv,
	u8 bsstype,
	struct chanscanparamset *chan_list,
	int chan_count,
	const struct lbs_ioctl_user_scan_cfg *user_cfg)
{
	int ret = -ENOMEM;
	struct lbs_scan_cmd_config *scan_cmd;
	u8 *tlv;    /* pointer into our current, growing TLV storage area */

	lbs_deb_enter_args(LBS_DEB_SCAN, "bsstype %d, chanlist[].chan %d, "
		"chan_count %d",
		bsstype, chan_list[0].channumber, chan_count);

	/* create the fixed part for scan command */
	scan_cmd = kzalloc(MAX_SCAN_CFG_ALLOC, GFP_KERNEL);
	if (scan_cmd == NULL)
		goto out;
	tlv = scan_cmd->tlvbuffer;
	if (user_cfg)
		memcpy(scan_cmd->bssid, user_cfg->bssid, ETH_ALEN);
	scan_cmd->bsstype = bsstype;

	/* add TLVs */
	if (user_cfg && user_cfg->ssid_len)
		tlv += lbs_scan_add_ssid_tlv(tlv, user_cfg);
	if (chan_list && chan_count)
		tlv += lbs_scan_add_chanlist_tlv(tlv, chan_list, chan_count);
	tlv += lbs_scan_add_rates_tlv(tlv);

	/* This is the final data we are about to send */
	scan_cmd->tlvbufferlen = tlv - scan_cmd->tlvbuffer;
	lbs_deb_hex(LBS_DEB_SCAN, "SCAN_CMD", (void *)scan_cmd, 1+6);
	lbs_deb_hex(LBS_DEB_SCAN, "SCAN_TLV", scan_cmd->tlvbuffer,
		scan_cmd->tlvbufferlen);

	ret = lbs_prepare_and_send_command(priv, CMD_802_11_SCAN, 0,
		CMD_OPTION_WAITFORRSP, 0, scan_cmd);
out:
	kfree(scan_cmd);
	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", ret);
	return ret;
}


/**
 *  @brief Internal function used to start a scan based on an input config
 *
 *  Also used from debugfs
 *
 *  Use the input user scan configuration information when provided in
 *    order to send the appropriate scan commands to firmware to populate or
 *    update the internal driver scan table
 *
 *  @param priv          A pointer to struct lbs_private structure
 *  @param puserscanin   Pointer to the input configuration for the requested
 *                       scan.
 *
 *  @return              0 or < 0 if error
 */
int lbs_scan_networks(struct lbs_private *priv,
	const struct lbs_ioctl_user_scan_cfg *user_cfg,
                       int full_scan)
{
	int ret = -ENOMEM;
	struct chanscanparamset *chan_list;
	struct chanscanparamset *curr_chans;
	int chan_count;
	u8 bsstype = CMD_BSS_TYPE_ANY;
	int numchannels = MRVDRV_CHANNELS_PER_SCAN_CMD;
	int filteredscan = 0;
	union iwreq_data wrqu;
#ifdef CONFIG_LIBERTAS_DEBUG
	struct bss_descriptor *iter;
	int i = 0;
	DECLARE_MAC_BUF(mac);
#endif

	lbs_deb_enter_args(LBS_DEB_SCAN, "full_scan %d",
		full_scan);

	/* Cancel any partial outstanding partial scans if this scan
	 * is a full scan.
	 */
	if (full_scan && delayed_work_pending(&priv->scan_work))
		cancel_delayed_work(&priv->scan_work);

	/* Determine same scan parameters */
	if (user_cfg) {
		if (user_cfg->bsstype)
			bsstype = user_cfg->bsstype;
		if (compare_ether_addr(user_cfg->bssid, &zeromac[0]) != 0) {
			numchannels = MRVDRV_MAX_CHANNELS_PER_SCAN;
			filteredscan = 1;
		}
	}
	lbs_deb_scan("numchannels %d, bsstype %d, "
		"filteredscan %d\n",
		numchannels, bsstype, filteredscan);

	/* Create list of channels to scan */
	chan_list = kzalloc(sizeof(struct chanscanparamset) *
				LBS_IOCTL_USER_SCAN_CHAN_MAX, GFP_KERNEL);
	if (!chan_list) {
		lbs_pr_alert("SCAN: chan_list empty\n");
		goto out;
	}

	/* We want to scan all channels */
	chan_count = lbs_scan_create_channel_list(priv, chan_list,
		filteredscan);

	netif_stop_queue(priv->dev);
	netif_carrier_off(priv->dev);
	if (priv->mesh_dev) {
		netif_stop_queue(priv->mesh_dev);
		netif_carrier_off(priv->mesh_dev);
	}

	/* Prepare to continue an interrupted scan */
	lbs_deb_scan("chan_count %d, last_scanned_channel %d\n",
		     chan_count, priv->last_scanned_channel);
	curr_chans = chan_list;
	/* advance channel list by already-scanned-channels */
	if (priv->last_scanned_channel > 0) {
		curr_chans += priv->last_scanned_channel;
		chan_count -= priv->last_scanned_channel;
	}

	/* Send scan command(s)
	 * numchannels contains the number of channels we should maximally scan
	 * chan_count is the total number of channels to scan
	 */

	while (chan_count) {
		int to_scan = min(numchannels, chan_count);
		lbs_deb_scan("scanning %d of %d channels\n",
			to_scan, chan_count);
		ret = lbs_do_scan(priv, bsstype, curr_chans,
			to_scan, user_cfg);
		if (ret) {
			lbs_pr_err("SCAN_CMD failed\n");
			goto out2;
		}
		curr_chans += to_scan;
		chan_count -= to_scan;

		/* somehow schedule the next part of the scan */
		if (chan_count &&
		    !full_scan &&
		    !priv->surpriseremoved) {
			/* -1 marks just that we're currently scanning */
			if (priv->last_scanned_channel < 0)
				priv->last_scanned_channel = to_scan;
			else
				priv->last_scanned_channel += to_scan;
			cancel_delayed_work(&priv->scan_work);
			queue_delayed_work(priv->work_thread, &priv->scan_work,
				msecs_to_jiffies(300));
			/* skip over GIWSCAN event */
			goto out;
		}

	}
	memset(&wrqu, 0, sizeof(union iwreq_data));
	wireless_send_event(priv->dev, SIOCGIWSCAN, &wrqu, NULL);

#ifdef CONFIG_LIBERTAS_DEBUG
	/* Dump the scan table */
	mutex_lock(&priv->lock);
	lbs_deb_scan("scan table:\n");
	list_for_each_entry(iter, &priv->network_list, list)
		lbs_deb_scan("%02d: BSSID %s, RSSI %d, SSID '%s'\n",
		       i++, print_mac(mac, iter->bssid), (s32) iter->rssi,
		       escape_essid(iter->ssid, iter->ssid_len));
	mutex_unlock(&priv->lock);
#endif

out2:
	priv->last_scanned_channel = 0;

out:
	if (priv->connect_status == LBS_CONNECTED) {
		netif_carrier_on(priv->dev);
		if (!priv->tx_pending_len)
			netif_wake_queue(priv->dev);
	}
	if (priv->mesh_dev && (priv->mesh_connect_status == LBS_CONNECTED)) {
		netif_carrier_on(priv->mesh_dev);
		if (!priv->tx_pending_len)
			netif_wake_queue(priv->mesh_dev);
	}
	kfree(chan_list);

	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", ret);
	return ret;
}




/*********************************************************************/
/*                                                                   */
/*  Result interpretation                                            */
/*                                                                   */
/*********************************************************************/

/**
 *  @brief Interpret a BSS scan response returned from the firmware
 *
 *  Parse the various fixed fields and IEs passed back for a a BSS probe
 *  response or beacon from the scan command.  Record information as needed
 *  in the scan table struct bss_descriptor for that entry.
 *
 *  @param bss  Output parameter: Pointer to the BSS Entry
 *
 *  @return             0 or -1
 */
static int lbs_process_bss(struct bss_descriptor *bss,
				u8 ** pbeaconinfo, int *bytesleft)
{
	struct ieeetypes_fhparamset *pFH;
	struct ieeetypes_dsparamset *pDS;
	struct ieeetypes_cfparamset *pCF;
	struct ieeetypes_ibssparamset *pibss;
	DECLARE_MAC_BUF(mac);
	struct ieeetypes_countryinfoset *pcountryinfo;
	u8 *pos, *end, *p;
	u8 n_ex_rates = 0, got_basic_rates = 0, n_basic_rates = 0;
	u16 beaconsize = 0;
	int ret;

	lbs_deb_enter(LBS_DEB_SCAN);

	if (*bytesleft >= sizeof(beaconsize)) {
		/* Extract & convert beacon size from the command buffer */
		beaconsize = le16_to_cpu(get_unaligned((__le16 *)*pbeaconinfo));
		*bytesleft -= sizeof(beaconsize);
		*pbeaconinfo += sizeof(beaconsize);
	}

	if (beaconsize == 0 || beaconsize > *bytesleft) {
		*pbeaconinfo += *bytesleft;
		*bytesleft = 0;
		ret = -1;
		goto done;
	}

	/* Initialize the current working beacon pointer for this BSS iteration */
	pos = *pbeaconinfo;
	end = pos + beaconsize;

	/* Advance the return beacon pointer past the current beacon */
	*pbeaconinfo += beaconsize;
	*bytesleft -= beaconsize;

	memcpy(bss->bssid, pos, ETH_ALEN);
	lbs_deb_scan("process_bss: BSSID %s\n", print_mac(mac, bss->bssid));
	pos += ETH_ALEN;

	if ((end - pos) < 12) {
		lbs_deb_scan("process_bss: Not enough bytes left\n");
		ret = -1;
		goto done;
	}

	/*
	 * next 4 fields are RSSI, time stamp, beacon interval,
	 *   and capability information
	 */

	/* RSSI is 1 byte long */
	bss->rssi = *pos;
	lbs_deb_scan("process_bss: RSSI %d\n", *pos);
	pos++;

	/* time stamp is 8 bytes long */
	pos += 8;

	/* beacon interval is 2 bytes long */
	bss->beaconperiod = le16_to_cpup((void *) pos);
	pos += 2;

	/* capability information is 2 bytes long */
	bss->capability = le16_to_cpup((void *) pos);
	lbs_deb_scan("process_bss: capabilities 0x%04x\n", bss->capability);
	pos += 2;

	if (bss->capability & WLAN_CAPABILITY_PRIVACY)
		lbs_deb_scan("process_bss: WEP enabled\n");
	if (bss->capability & WLAN_CAPABILITY_IBSS)
		bss->mode = IW_MODE_ADHOC;
	else
		bss->mode = IW_MODE_INFRA;

	/* rest of the current buffer are IE's */
	lbs_deb_scan("process_bss: IE len %zd\n", end - pos);
	lbs_deb_hex(LBS_DEB_SCAN, "process_bss: IE info", pos, end - pos);

	/* process variable IE */
	while (pos <= end - 2) {
		struct ieee80211_info_element * elem =
			(struct ieee80211_info_element *) pos;

		if (pos + elem->len > end) {
			lbs_deb_scan("process_bss: error in processing IE, "
			       "bytes left < IE length\n");
			break;
		}

		switch (elem->id) {
		case MFIE_TYPE_SSID:
			bss->ssid_len = elem->len;
			memcpy(bss->ssid, elem->data, elem->len);
			lbs_deb_scan("got SSID IE: '%s', len %u\n",
			             escape_essid(bss->ssid, bss->ssid_len),
			             bss->ssid_len);
			break;

		case MFIE_TYPE_RATES:
			n_basic_rates = min_t(u8, MAX_RATES, elem->len);
			memcpy(bss->rates, elem->data, n_basic_rates);
			got_basic_rates = 1;
			lbs_deb_scan("got RATES IE\n");
			break;

		case MFIE_TYPE_FH_SET:
			pFH = (struct ieeetypes_fhparamset *) pos;
			memmove(&bss->phyparamset.fhparamset, pFH,
				sizeof(struct ieeetypes_fhparamset));
			lbs_deb_scan("got FH IE\n");
			break;

		case MFIE_TYPE_DS_SET:
			pDS = (struct ieeetypes_dsparamset *) pos;
			bss->channel = pDS->currentchan;
			memcpy(&bss->phyparamset.dsparamset, pDS,
			       sizeof(struct ieeetypes_dsparamset));
			lbs_deb_scan("got DS IE, channel %d\n", bss->channel);
			break;

		case MFIE_TYPE_CF_SET:
			pCF = (struct ieeetypes_cfparamset *) pos;
			memcpy(&bss->ssparamset.cfparamset, pCF,
			       sizeof(struct ieeetypes_cfparamset));
			lbs_deb_scan("got CF IE\n");
			break;

		case MFIE_TYPE_IBSS_SET:
			pibss = (struct ieeetypes_ibssparamset *) pos;
			bss->atimwindow = le16_to_cpu(pibss->atimwindow);
			memmove(&bss->ssparamset.ibssparamset, pibss,
				sizeof(struct ieeetypes_ibssparamset));
			lbs_deb_scan("got IBSS IE\n");
			break;

		case MFIE_TYPE_COUNTRY:
			pcountryinfo = (struct ieeetypes_countryinfoset *) pos;
			lbs_deb_scan("got COUNTRY IE\n");
			if (pcountryinfo->len < sizeof(pcountryinfo->countrycode)
			    || pcountryinfo->len > 254) {
				lbs_deb_scan("process_bss: 11D- Err "
				       "CountryInfo len %d, min %zd, max 254\n",
				       pcountryinfo->len,
				       sizeof(pcountryinfo->countrycode));
				ret = -1;
				goto done;
			}

			memcpy(&bss->countryinfo,
			       pcountryinfo, pcountryinfo->len + 2);
			lbs_deb_hex(LBS_DEB_SCAN, "process_bss: 11d countryinfo",
				(u8 *) pcountryinfo,
				(u32) (pcountryinfo->len + 2));
			break;

		case MFIE_TYPE_RATES_EX:
			/* only process extended supported rate if data rate is
			 * already found. Data rate IE should come before
			 * extended supported rate IE
			 */
			lbs_deb_scan("got RATESEX IE\n");
			if (!got_basic_rates) {
				lbs_deb_scan("... but ignoring it\n");
				break;
			}

			n_ex_rates = elem->len;
			if (n_basic_rates + n_ex_rates > MAX_RATES)
				n_ex_rates = MAX_RATES - n_basic_rates;

			p = bss->rates + n_basic_rates;
			memcpy(p, elem->data, n_ex_rates);
			break;

		case MFIE_TYPE_GENERIC:
			if (elem->len >= 4 &&
			    elem->data[0] == 0x00 &&
			    elem->data[1] == 0x50 &&
			    elem->data[2] == 0xf2 &&
			    elem->data[3] == 0x01) {
				bss->wpa_ie_len = min(elem->len + 2,
				                      MAX_WPA_IE_LEN);
				memcpy(bss->wpa_ie, elem, bss->wpa_ie_len);
				lbs_deb_scan("got WPA IE\n");
				lbs_deb_hex(LBS_DEB_SCAN, "WPA IE", bss->wpa_ie,
				            elem->len);
			} else if (elem->len >= MARVELL_MESH_IE_LENGTH &&
			    elem->data[0] == 0x00 &&
			    elem->data[1] == 0x50 &&
			    elem->data[2] == 0x43 &&
			    elem->data[3] == 0x04) {
				lbs_deb_scan("got mesh IE\n");
				bss->mesh = 1;
			} else {
				lbs_deb_scan("got generiec IE: "
					"%02x:%02x:%02x:%02x, len %d\n",
					elem->data[0], elem->data[1],
					elem->data[2], elem->data[3],
					elem->len);
			}
			break;

		case MFIE_TYPE_RSN:
			lbs_deb_scan("got RSN IE\n");
			bss->rsn_ie_len = min(elem->len + 2, MAX_WPA_IE_LEN);
			memcpy(bss->rsn_ie, elem, bss->rsn_ie_len);
			lbs_deb_hex(LBS_DEB_SCAN, "process_bss: RSN_IE",
				bss->rsn_ie, elem->len);
			break;

		default:
			lbs_deb_scan("got IE 0x%04x, len %d\n",
				elem->id, elem->len);
			break;
		}

		pos += elem->len + 2;
	}

	/* Timestamp */
	bss->last_scanned = jiffies;
	lbs_unset_basic_rate_flags(bss->rates, sizeof(bss->rates));

	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", ret);
	return ret;
}

/**
 *  @brief This function finds a specific compatible BSSID in the scan list
 *
 *  Used in association code
 *
 *  @param priv  A pointer to struct lbs_private
 *  @param bssid    BSSID to find in the scan list
 *  @param mode     Network mode: Infrastructure or IBSS
 *
 *  @return         index in BSSID list, or error return code (< 0)
 */
struct bss_descriptor *lbs_find_bssid_in_list(struct lbs_private *priv,
		u8 * bssid, u8 mode)
{
	struct bss_descriptor * iter_bss;
	struct bss_descriptor * found_bss = NULL;

	lbs_deb_enter(LBS_DEB_SCAN);

	if (!bssid)
		goto out;

	lbs_deb_hex(LBS_DEB_SCAN, "looking for",
		bssid, ETH_ALEN);

	/* Look through the scan table for a compatible match.  The loop will
	 *   continue past a matched bssid that is not compatible in case there
	 *   is an AP with multiple SSIDs assigned to the same BSSID
	 */
	mutex_lock(&priv->lock);
	list_for_each_entry (iter_bss, &priv->network_list, list) {
		if (compare_ether_addr(iter_bss->bssid, bssid))
			continue; /* bssid doesn't match */
		switch (mode) {
		case IW_MODE_INFRA:
		case IW_MODE_ADHOC:
			if (!is_network_compatible(priv, iter_bss, mode))
				break;
			found_bss = iter_bss;
			break;
		default:
			found_bss = iter_bss;
			break;
		}
	}
	mutex_unlock(&priv->lock);

out:
	lbs_deb_leave_args(LBS_DEB_SCAN, "found_bss %p", found_bss);
	return found_bss;
}

/**
 *  @brief This function finds ssid in ssid list.
 *
 *  Used in association code
 *
 *  @param priv  A pointer to struct lbs_private
 *  @param ssid     SSID to find in the list
 *  @param bssid    BSSID to qualify the SSID selection (if provided)
 *  @param mode     Network mode: Infrastructure or IBSS
 *
 *  @return         index in BSSID list
 */
struct bss_descriptor *lbs_find_ssid_in_list(struct lbs_private *priv,
		   u8 *ssid, u8 ssid_len, u8 * bssid, u8 mode,
		   int channel)
{
	u8 bestrssi = 0;
	struct bss_descriptor * iter_bss = NULL;
	struct bss_descriptor * found_bss = NULL;
	struct bss_descriptor * tmp_oldest = NULL;

	lbs_deb_enter(LBS_DEB_SCAN);

	mutex_lock(&priv->lock);

	list_for_each_entry (iter_bss, &priv->network_list, list) {
		if (   !tmp_oldest
		    || (iter_bss->last_scanned < tmp_oldest->last_scanned))
			tmp_oldest = iter_bss;

		if (lbs_ssid_cmp(iter_bss->ssid, iter_bss->ssid_len,
		                      ssid, ssid_len) != 0)
			continue; /* ssid doesn't match */
		if (bssid && compare_ether_addr(iter_bss->bssid, bssid) != 0)
			continue; /* bssid doesn't match */
		if ((channel > 0) && (iter_bss->channel != channel))
			continue; /* channel doesn't match */

		switch (mode) {
		case IW_MODE_INFRA:
		case IW_MODE_ADHOC:
			if (!is_network_compatible(priv, iter_bss, mode))
				break;

			if (bssid) {
				/* Found requested BSSID */
				found_bss = iter_bss;
				goto out;
			}

			if (SCAN_RSSI(iter_bss->rssi) > bestrssi) {
				bestrssi = SCAN_RSSI(iter_bss->rssi);
				found_bss = iter_bss;
			}
			break;
		case IW_MODE_AUTO:
		default:
			if (SCAN_RSSI(iter_bss->rssi) > bestrssi) {
				bestrssi = SCAN_RSSI(iter_bss->rssi);
				found_bss = iter_bss;
			}
			break;
		}
	}

out:
	mutex_unlock(&priv->lock);
	lbs_deb_leave_args(LBS_DEB_SCAN, "found_bss %p", found_bss);
	return found_bss;
}

/**
 *  @brief This function finds the best SSID in the Scan List
 *
 *  Search the scan table for the best SSID that also matches the current
 *   adapter network preference (infrastructure or adhoc)
 *
 *  @param priv  A pointer to struct lbs_private
 *
 *  @return         index in BSSID list
 */
static struct bss_descriptor *lbs_find_best_ssid_in_list(
	struct lbs_private *priv,
	u8 mode)
{
	u8 bestrssi = 0;
	struct bss_descriptor * iter_bss;
	struct bss_descriptor * best_bss = NULL;

	lbs_deb_enter(LBS_DEB_SCAN);

	mutex_lock(&priv->lock);

	list_for_each_entry (iter_bss, &priv->network_list, list) {
		switch (mode) {
		case IW_MODE_INFRA:
		case IW_MODE_ADHOC:
			if (!is_network_compatible(priv, iter_bss, mode))
				break;
			if (SCAN_RSSI(iter_bss->rssi) <= bestrssi)
				break;
			bestrssi = SCAN_RSSI(iter_bss->rssi);
			best_bss = iter_bss;
			break;
		case IW_MODE_AUTO:
		default:
			if (SCAN_RSSI(iter_bss->rssi) <= bestrssi)
				break;
			bestrssi = SCAN_RSSI(iter_bss->rssi);
			best_bss = iter_bss;
			break;
		}
	}

	mutex_unlock(&priv->lock);
	lbs_deb_leave_args(LBS_DEB_SCAN, "best_bss %p", best_bss);
	return best_bss;
}

/**
 *  @brief Find the AP with specific ssid in the scan list
 *
 *  Used from association worker.
 *
 *  @param priv         A pointer to struct lbs_private structure
 *  @param pSSID        A pointer to AP's ssid
 *
 *  @return             0--success, otherwise--fail
 */
int lbs_find_best_network_ssid(struct lbs_private *priv,
		u8 *out_ssid, u8 *out_ssid_len, u8 preferred_mode, u8 *out_mode)
{
	int ret = -1;
	struct bss_descriptor * found;

	lbs_deb_enter(LBS_DEB_SCAN);

	lbs_scan_networks(priv, NULL, 1);
	if (priv->surpriseremoved)
		goto out;

	found = lbs_find_best_ssid_in_list(priv, preferred_mode);
	if (found && (found->ssid_len > 0)) {
		memcpy(out_ssid, &found->ssid, IW_ESSID_MAX_SIZE);
		*out_ssid_len = found->ssid_len;
		*out_mode = found->mode;
		ret = 0;
	}

out:
	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", ret);
	return ret;
}


/**
 *  @brief Send a scan command for all available channels filtered on a spec
 *
 *  Used in association code and from debugfs
 *
 *  @param priv             A pointer to struct lbs_private structure
 *  @param ssid             A pointer to the SSID to scan for
 *  @param ssid_len         Length of the SSID
 *  @param clear_ssid       Should existing scan results with this SSID
 *                          be cleared?
 *
 *  @return                0-success, otherwise fail
 */
int lbs_send_specific_ssid_scan(struct lbs_private *priv,
			u8 *ssid, u8 ssid_len, u8 clear_ssid)
{
	struct lbs_ioctl_user_scan_cfg scancfg;
	int ret = 0;

	lbs_deb_enter_args(LBS_DEB_SCAN, "SSID '%s', clear %d",
		escape_essid(ssid, ssid_len), clear_ssid);

	if (!ssid_len)
		goto out;

	memset(&scancfg, 0x00, sizeof(scancfg));
	memcpy(scancfg.ssid, ssid, ssid_len);
	scancfg.ssid_len = ssid_len;
	scancfg.clear_ssid = clear_ssid;

	lbs_scan_networks(priv, &scancfg, 1);
	if (priv->surpriseremoved) {
		ret = -1;
		goto out;
	}

out:
	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", ret);
	return ret;
}




/*********************************************************************/
/*                                                                   */
/*  Support for Wireless Extensions                                  */
/*                                                                   */
/*********************************************************************/


#define MAX_CUSTOM_LEN 64

static inline char *lbs_translate_scan(struct lbs_private *priv,
					char *start, char *stop,
					struct bss_descriptor *bss)
{
	struct chan_freq_power *cfp;
	char *current_val;	/* For rates */
	struct iw_event iwe;	/* Temporary buffer */
	int j;
#define PERFECT_RSSI ((u8)50)
#define WORST_RSSI   ((u8)0)
#define RSSI_DIFF    ((u8)(PERFECT_RSSI - WORST_RSSI))
	u8 rssi;

	lbs_deb_enter(LBS_DEB_SCAN);

	cfp = lbs_find_cfp_by_band_and_channel(priv, 0, bss->channel);
	if (!cfp) {
		lbs_deb_scan("Invalid channel number %d\n", bss->channel);
		start = NULL;
		goto out;
	}

	/* First entry *MUST* be the BSSID */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, &bss->bssid, ETH_ALEN);
	start = iwe_stream_add_event(start, stop, &iwe, IW_EV_ADDR_LEN);

	/* SSID */
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
	iwe.u.data.length = min((u32) bss->ssid_len, (u32) IW_ESSID_MAX_SIZE);
	start = iwe_stream_add_point(start, stop, &iwe, bss->ssid);

	/* Mode */
	iwe.cmd = SIOCGIWMODE;
	iwe.u.mode = bss->mode;
	start = iwe_stream_add_event(start, stop, &iwe, IW_EV_UINT_LEN);

	/* Frequency */
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = (long)cfp->freq * 100000;
	iwe.u.freq.e = 1;
	start = iwe_stream_add_event(start, stop, &iwe, IW_EV_FREQ_LEN);

	/* Add quality statistics */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.updated = IW_QUAL_ALL_UPDATED;
	iwe.u.qual.level = SCAN_RSSI(bss->rssi);

	rssi = iwe.u.qual.level - MRVDRV_NF_DEFAULT_SCAN_VALUE;
	iwe.u.qual.qual =
	    (100 * RSSI_DIFF * RSSI_DIFF - (PERFECT_RSSI - rssi) *
	     (15 * (RSSI_DIFF) + 62 * (PERFECT_RSSI - rssi))) /
	    (RSSI_DIFF * RSSI_DIFF);
	if (iwe.u.qual.qual > 100)
		iwe.u.qual.qual = 100;

	if (priv->NF[TYPE_BEACON][TYPE_NOAVG] == 0) {
		iwe.u.qual.noise = MRVDRV_NF_DEFAULT_SCAN_VALUE;
	} else {
		iwe.u.qual.noise =
		    CAL_NF(priv->NF[TYPE_BEACON][TYPE_NOAVG]);
	}

	/* Locally created ad-hoc BSSs won't have beacons if this is the
	 * only station in the adhoc network; so get signal strength
	 * from receive statistics.
	 */
	if ((priv->mode == IW_MODE_ADHOC)
	    && priv->adhoccreate
	    && !lbs_ssid_cmp(priv->curbssparams.ssid,
	                          priv->curbssparams.ssid_len,
	                          bss->ssid, bss->ssid_len)) {
		int snr, nf;
		snr = priv->SNR[TYPE_RXPD][TYPE_AVG] / AVG_SCALE;
		nf = priv->NF[TYPE_RXPD][TYPE_AVG] / AVG_SCALE;
		iwe.u.qual.level = CAL_RSSI(snr, nf);
	}
	start = iwe_stream_add_event(start, stop, &iwe, IW_EV_QUAL_LEN);

	/* Add encryption capability */
	iwe.cmd = SIOCGIWENCODE;
	if (bss->capability & WLAN_CAPABILITY_PRIVACY) {
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	} else {
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	}
	iwe.u.data.length = 0;
	start = iwe_stream_add_point(start, stop, &iwe, bss->ssid);

	current_val = start + IW_EV_LCP_LEN;

	iwe.cmd = SIOCGIWRATE;
	iwe.u.bitrate.fixed = 0;
	iwe.u.bitrate.disabled = 0;
	iwe.u.bitrate.value = 0;

	for (j = 0; bss->rates[j] && (j < sizeof(bss->rates)); j++) {
		/* Bit rate given in 500 kb/s units */
		iwe.u.bitrate.value = bss->rates[j] * 500000;
		current_val = iwe_stream_add_value(start, current_val,
					 stop, &iwe, IW_EV_PARAM_LEN);
	}
	if ((bss->mode == IW_MODE_ADHOC)
	    && !lbs_ssid_cmp(priv->curbssparams.ssid,
	                          priv->curbssparams.ssid_len,
	                          bss->ssid, bss->ssid_len)
	    && priv->adhoccreate) {
		iwe.u.bitrate.value = 22 * 500000;
		current_val = iwe_stream_add_value(start, current_val,
					 stop, &iwe, IW_EV_PARAM_LEN);
	}
	/* Check if we added any event */
	if((current_val - start) > IW_EV_LCP_LEN)
		start = current_val;

	memset(&iwe, 0, sizeof(iwe));
	if (bss->wpa_ie_len) {
		char buf[MAX_WPA_IE_LEN];
		memcpy(buf, bss->wpa_ie, bss->wpa_ie_len);
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = bss->wpa_ie_len;
		start = iwe_stream_add_point(start, stop, &iwe, buf);
	}

	memset(&iwe, 0, sizeof(iwe));
	if (bss->rsn_ie_len) {
		char buf[MAX_WPA_IE_LEN];
		memcpy(buf, bss->rsn_ie, bss->rsn_ie_len);
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = bss->rsn_ie_len;
		start = iwe_stream_add_point(start, stop, &iwe, buf);
	}

	if (bss->mesh) {
		char custom[MAX_CUSTOM_LEN];
		char *p = custom;

		iwe.cmd = IWEVCUSTOM;
		p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
		              "mesh-type: olpc");
		iwe.u.data.length = p - custom;
		if (iwe.u.data.length)
			start = iwe_stream_add_point(start, stop, &iwe, custom);
	}

out:
	lbs_deb_leave_args(LBS_DEB_SCAN, "start %p", start);
	return start;
}


/**
 *  @brief Handle Scan Network ioctl
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param vwrq         A pointer to iw_param structure
 *  @param extra        A pointer to extra data buf
 *
 *  @return             0 --success, otherwise fail
 */
int lbs_set_scan(struct net_device *dev, struct iw_request_info *info,
		  struct iw_param *wrqu, char *extra)
{
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_SCAN);

	if (!netif_running(dev))
		return -ENETDOWN;

	/* mac80211 does this:
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->type != IEEE80211_IF_TYPE_xxx)
		return -EOPNOTSUPP;

	if (wrqu->data.length == sizeof(struct iw_scan_req) &&
	    wrqu->data.flags & IW_SCAN_THIS_ESSID) {
		req = (struct iw_scan_req *)extra;
			ssid = req->essid;
		ssid_len = req->essid_len;
	}
	*/

	if (!delayed_work_pending(&priv->scan_work))
		queue_delayed_work(priv->work_thread, &priv->scan_work,
			msecs_to_jiffies(50));
	/* set marker that currently a scan is taking place */
	priv->last_scanned_channel = -1;

	if (priv->surpriseremoved)
		return -EIO;

	lbs_deb_leave(LBS_DEB_SCAN);
	return 0;
}


/**
 *  @brief  Handle Retrieve scan table ioctl
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param dwrq         A pointer to iw_point structure
 *  @param extra        A pointer to extra data buf
 *
 *  @return             0 --success, otherwise fail
 */
int lbs_get_scan(struct net_device *dev, struct iw_request_info *info,
		  struct iw_point *dwrq, char *extra)
{
#define SCAN_ITEM_SIZE 128
	struct lbs_private *priv = dev->priv;
	int err = 0;
	char *ev = extra;
	char *stop = ev + dwrq->length;
	struct bss_descriptor * iter_bss;
	struct bss_descriptor * safe;

	lbs_deb_enter(LBS_DEB_SCAN);

	/* iwlist should wait until the current scan is finished */
	if (priv->last_scanned_channel)
		return -EAGAIN;

	/* Update RSSI if current BSS is a locally created ad-hoc BSS */
	if ((priv->mode == IW_MODE_ADHOC) && priv->adhoccreate) {
		lbs_prepare_and_send_command(priv, CMD_802_11_RSSI, 0,
					CMD_OPTION_WAITFORRSP, 0, NULL);
	}

	mutex_lock(&priv->lock);
	list_for_each_entry_safe (iter_bss, safe, &priv->network_list, list) {
		char * next_ev;
		unsigned long stale_time;

		if (stop - ev < SCAN_ITEM_SIZE) {
			err = -E2BIG;
			break;
		}

		/* For mesh device, list only mesh networks */
		if (dev == priv->mesh_dev && !iter_bss->mesh)
			continue;

		/* Prune old an old scan result */
		stale_time = iter_bss->last_scanned + DEFAULT_MAX_SCAN_AGE;
		if (time_after(jiffies, stale_time)) {
			list_move_tail (&iter_bss->list,
			                &priv->network_free_list);
			clear_bss_descriptor(iter_bss);
			continue;
		}

		/* Translate to WE format this entry */
		next_ev = lbs_translate_scan(priv, ev, stop, iter_bss);
		if (next_ev == NULL)
			continue;
		ev = next_ev;
	}
	mutex_unlock(&priv->lock);

	dwrq->length = (ev - extra);
	dwrq->flags = 0;

	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", err);
	return err;
}




/*********************************************************************/
/*                                                                   */
/*  Command execution                                                */
/*                                                                   */
/*********************************************************************/


/**
 *  @brief Prepare a scan command to be sent to the firmware
 *
 *  Called via lbs_prepare_and_send_command(priv, CMD_802_11_SCAN, ...)
 *  from cmd.c
 *
 *  Sends a fixed length data part (specifying the BSS type and BSSID filters)
 *  as well as a variable number/length of TLVs to the firmware.
 *
 *  @param priv       A pointer to struct lbs_private structure
 *  @param cmd        A pointer to cmd_ds_command structure to be sent to
 *                    firmware with the cmd_DS_801_11_SCAN structure
 *  @param pdata_buf  Void pointer cast of a lbs_scan_cmd_config struct used
 *                    to set the fields/TLVs for the command sent to firmware
 *
 *  @return           0 or -1
 */
int lbs_cmd_80211_scan(struct lbs_private *priv,
	struct cmd_ds_command *cmd, void *pdata_buf)
{
	struct cmd_ds_802_11_scan *pscan = &cmd->params.scan;
	struct lbs_scan_cmd_config *pscancfg = pdata_buf;

	lbs_deb_enter(LBS_DEB_SCAN);

	/* Set fixed field variables in scan command */
	pscan->bsstype = pscancfg->bsstype;
	memcpy(pscan->bssid, pscancfg->bssid, ETH_ALEN);
	memcpy(pscan->tlvbuffer, pscancfg->tlvbuffer, pscancfg->tlvbufferlen);

	/* size is equal to the sizeof(fixed portions) + the TLV len + header */
	cmd->size = cpu_to_le16(sizeof(pscan->bsstype) + ETH_ALEN
				+ pscancfg->tlvbufferlen + S_DS_GEN);

	lbs_deb_leave(LBS_DEB_SCAN);
	return 0;
}

/**
 *  @brief This function handles the command response of scan
 *
 *  Called from handle_cmd_response() in cmdrespc.
 *
 *   The response buffer for the scan command has the following
 *      memory layout:
 *
 *     .-----------------------------------------------------------.
 *     |  header (4 * sizeof(u16)):  Standard command response hdr |
 *     .-----------------------------------------------------------.
 *     |  bufsize (u16) : sizeof the BSS Description data          |
 *     .-----------------------------------------------------------.
 *     |  NumOfSet (u8) : Number of BSS Descs returned             |
 *     .-----------------------------------------------------------.
 *     |  BSSDescription data (variable, size given in bufsize)    |
 *     .-----------------------------------------------------------.
 *     |  TLV data (variable, size calculated using header->size,  |
 *     |            bufsize and sizeof the fixed fields above)     |
 *     .-----------------------------------------------------------.
 *
 *  @param priv    A pointer to struct lbs_private structure
 *  @param resp    A pointer to cmd_ds_command
 *
 *  @return        0 or -1
 */
int lbs_ret_80211_scan(struct lbs_private *priv, struct cmd_ds_command *resp)
{
	struct cmd_ds_802_11_scan_rsp *pscan;
	struct bss_descriptor * iter_bss;
	struct bss_descriptor * safe;
	u8 *pbssinfo;
	u16 scanrespsize;
	int bytesleft;
	int idx;
	int tlvbufsize;
	int ret;

	lbs_deb_enter(LBS_DEB_SCAN);

	/* Prune old entries from scan table */
	list_for_each_entry_safe (iter_bss, safe, &priv->network_list, list) {
		unsigned long stale_time = iter_bss->last_scanned + DEFAULT_MAX_SCAN_AGE;
		if (time_before(jiffies, stale_time))
			continue;
		list_move_tail (&iter_bss->list, &priv->network_free_list);
		clear_bss_descriptor(iter_bss);
	}

	pscan = &resp->params.scanresp;

	if (pscan->nr_sets > MAX_NETWORK_COUNT) {
		lbs_deb_scan(
		       "SCAN_RESP: too many scan results (%d, max %d)!!\n",
		       pscan->nr_sets, MAX_NETWORK_COUNT);
		ret = -1;
		goto done;
	}

	bytesleft = le16_to_cpu(pscan->bssdescriptsize);
	lbs_deb_scan("SCAN_RESP: bssdescriptsize %d\n", bytesleft);

	scanrespsize = le16_to_cpu(resp->size);
	lbs_deb_scan("SCAN_RESP: scan results %d\n", pscan->nr_sets);

	pbssinfo = pscan->bssdesc_and_tlvbuffer;

	/* The size of the TLV buffer is equal to the entire command response
	 *   size (scanrespsize) minus the fixed fields (sizeof()'s), the
	 *   BSS Descriptions (bssdescriptsize as bytesLef) and the command
	 *   response header (S_DS_GEN)
	 */
	tlvbufsize = scanrespsize - (bytesleft + sizeof(pscan->bssdescriptsize)
				     + sizeof(pscan->nr_sets)
				     + S_DS_GEN);

	/*
	 *  Process each scan response returned (pscan->nr_sets).  Save
	 *    the information in the newbssentry and then insert into the
	 *    driver scan table either as an update to an existing entry
	 *    or as an addition at the end of the table
	 */
	for (idx = 0; idx < pscan->nr_sets && bytesleft; idx++) {
		struct bss_descriptor new;
		struct bss_descriptor * found = NULL;
		struct bss_descriptor * oldest = NULL;
		DECLARE_MAC_BUF(mac);

		/* Process the data fields and IEs returned for this BSS */
		memset(&new, 0, sizeof (struct bss_descriptor));
		if (lbs_process_bss(&new, &pbssinfo, &bytesleft) != 0) {
			/* error parsing the scan response, skipped */
			lbs_deb_scan("SCAN_RESP: process_bss returned ERROR\n");
			continue;
		}

		/* Try to find this bss in the scan table */
		list_for_each_entry (iter_bss, &priv->network_list, list) {
			if (is_same_network(iter_bss, &new)) {
				found = iter_bss;
				break;
			}

			if ((oldest == NULL) ||
			    (iter_bss->last_scanned < oldest->last_scanned))
				oldest = iter_bss;
		}

		if (found) {
			/* found, clear it */
			clear_bss_descriptor(found);
		} else if (!list_empty(&priv->network_free_list)) {
			/* Pull one from the free list */
			found = list_entry(priv->network_free_list.next,
					   struct bss_descriptor, list);
			list_move_tail(&found->list, &priv->network_list);
		} else if (oldest) {
			/* If there are no more slots, expire the oldest */
			found = oldest;
			clear_bss_descriptor(found);
			list_move_tail(&found->list, &priv->network_list);
		} else {
			continue;
		}

		lbs_deb_scan("SCAN_RESP: BSSID %s\n",
			     print_mac(mac, new.bssid));

		/* Copy the locally created newbssentry to the scan table */
		memcpy(found, &new, offsetof(struct bss_descriptor, list));
	}

	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", ret);
	return ret;
}
