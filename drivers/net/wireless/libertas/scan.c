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

//! Maximum memory needed for a wlan_scan_cmd_config with all TLVs at max
#define MAX_SCAN_CFG_ALLOC (sizeof(struct wlan_scan_cmd_config)  \
                            + sizeof(struct mrvlietypes_numprobes)   \
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

static inline int match_bss_no_security(struct wlan_802_11_security * secinfo,
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

static inline int match_bss_static_wep(struct wlan_802_11_security * secinfo,
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

static inline int match_bss_wpa(struct wlan_802_11_security * secinfo,
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

static inline int match_bss_wpa2(struct wlan_802_11_security * secinfo,
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

static inline int match_bss_dynamic_wep(struct wlan_802_11_security * secinfo,
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
 *  @param adapter A pointer to wlan_adapter
 *  @param index   Index in scantable to check against current driver settings
 *  @param mode    Network mode: Infrastructure or IBSS
 *
 *  @return        Index in scantable, or error code if negative
 */
static int is_network_compatible(wlan_adapter * adapter,
		struct bss_descriptor * bss, u8 mode)
{
	int matched = 0;

	lbs_deb_enter(LBS_DEB_SCAN);

	if (bss->mode != mode)
		goto done;

	if ((matched = match_bss_no_security(&adapter->secinfo, bss))) {
		goto done;
	} else if ((matched = match_bss_static_wep(&adapter->secinfo, bss))) {
		goto done;
	} else if ((matched = match_bss_wpa(&adapter->secinfo, bss))) {
		lbs_deb_scan(
		       "is_network_compatible() WPA: wpa_ie=%#x "
		       "wpa2_ie=%#x WEP=%s WPA=%s WPA2=%s "
		       "privacy=%#x\n", bss->wpa_ie[0], bss->rsn_ie[0],
		       adapter->secinfo.wep_enabled ? "e" : "d",
		       adapter->secinfo.WPAenabled ? "e" : "d",
		       adapter->secinfo.WPA2enabled ? "e" : "d",
		       (bss->capability & WLAN_CAPABILITY_PRIVACY));
		goto done;
	} else if ((matched = match_bss_wpa2(&adapter->secinfo, bss))) {
		lbs_deb_scan(
		       "is_network_compatible() WPA2: wpa_ie=%#x "
		       "wpa2_ie=%#x WEP=%s WPA=%s WPA2=%s "
		       "privacy=%#x\n", bss->wpa_ie[0], bss->rsn_ie[0],
		       adapter->secinfo.wep_enabled ? "e" : "d",
		       adapter->secinfo.WPAenabled ? "e" : "d",
		       adapter->secinfo.WPA2enabled ? "e" : "d",
		       (bss->capability & WLAN_CAPABILITY_PRIVACY));
		goto done;
	} else if ((matched = match_bss_dynamic_wep(&adapter->secinfo, bss))) {
		lbs_deb_scan(
		       "is_network_compatible() dynamic WEP: "
		       "wpa_ie=%#x wpa2_ie=%#x privacy=%#x\n",
		       bss->wpa_ie[0], bss->rsn_ie[0],
		       (bss->capability & WLAN_CAPABILITY_PRIVACY));
		goto done;
	}

	/* bss security settings don't match those configured on card */
	lbs_deb_scan(
	       "is_network_compatible() FAILED: wpa_ie=%#x "
	       "wpa2_ie=%#x WEP=%s WPA=%s WPA2=%s privacy=%#x\n",
	       bss->wpa_ie[0], bss->rsn_ie[0],
	       adapter->secinfo.wep_enabled ? "e" : "d",
	       adapter->secinfo.WPAenabled ? "e" : "d",
	       adapter->secinfo.WPA2enabled ? "e" : "d",
	       (bss->capability & WLAN_CAPABILITY_PRIVACY));

done:
	lbs_deb_leave_args(LBS_DEB_SCAN, "matched: %d", matched);
	return matched;
}

/**
 *  @brief Compare two SSIDs
 *
 *  @param ssid1    A pointer to ssid to compare
 *  @param ssid2    A pointer to ssid to compare
 *
 *  @return         0--ssid is same, otherwise is different
 */
int libertas_ssid_cmp(u8 *ssid1, u8 ssid1_len, u8 *ssid2, u8 ssid2_len)
{
	if (ssid1_len != ssid2_len)
		return -1;

	return memcmp(ssid1, ssid2, ssid1_len);
}




/*********************************************************************/
/*                                                                   */
/*  Main scanning support                                            */
/*                                                                   */
/*********************************************************************/


/**
 *  @brief Create a channel list for the driver to scan based on region info
 *
 *  Only used from wlan_scan_setup_scan_config()
 *
 *  Use the driver region/band information to construct a comprehensive list
 *    of channels to scan.  This routine is used for any scan that is not
 *    provided a specific channel list to scan.
 *
 *  @param priv          A pointer to wlan_private structure
 *  @param scanchanlist  Output parameter: resulting channel list to scan
 *  @param filteredscan  Flag indicating whether or not a BSSID or SSID filter
 *                       is being sent in the command to firmware.  Used to
 *                       increase the number of channels sent in a scan
 *                       command and to disable the firmware channel scan
 *                       filter.
 *
 *  @return              void
 */
static void wlan_scan_create_channel_list(wlan_private * priv,
					  struct chanscanparamset * scanchanlist,
					  u8 filteredscan)
{

	wlan_adapter *adapter = priv->adapter;
	struct region_channel *scanregion;
	struct chan_freq_power *cfp;
	int rgnidx;
	int chanidx;
	int nextchan;
	u8 scantype;

	lbs_deb_enter_args(LBS_DEB_SCAN, "filteredscan %d", filteredscan);

	chanidx = 0;

	/* Set the default scan type to the user specified type, will later
	 *   be changed to passive on a per channel basis if restricted by
	 *   regulatory requirements (11d or 11h)
	 */
	scantype = CMD_SCAN_TYPE_ACTIVE;

	for (rgnidx = 0; rgnidx < ARRAY_SIZE(adapter->region_channel); rgnidx++) {
		if (priv->adapter->enable11d &&
		    adapter->connect_status != LIBERTAS_CONNECTED) {
			/* Scan all the supported chan for the first scan */
			if (!adapter->universal_channel[rgnidx].valid)
				continue;
			scanregion = &adapter->universal_channel[rgnidx];

			/* clear the parsed_region_chan for the first scan */
			memset(&adapter->parsed_region_chan, 0x00,
			       sizeof(adapter->parsed_region_chan));
		} else {
			if (!adapter->region_channel[rgnidx].valid)
				continue;
			scanregion = &adapter->region_channel[rgnidx];
		}

		for (nextchan = 0;
		     nextchan < scanregion->nrcfp; nextchan++, chanidx++) {

			cfp = scanregion->CFP + nextchan;

			if (priv->adapter->enable11d) {
				scantype =
				    libertas_get_scan_type_11d(cfp->channel,
							   &adapter->
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
}


/* Delayed partial scan worker */
void libertas_scan_worker(struct work_struct *work)
{
	wlan_private *priv = container_of(work, wlan_private, scan_work.work);

	wlan_scan_networks(priv, NULL, 0);
}


/**
 *  @brief Construct a wlan_scan_cmd_config structure to use in issue scan cmds
 *
 *  Application layer or other functions can invoke wlan_scan_networks
 *    with a scan configuration supplied in a wlan_ioctl_user_scan_cfg struct.
 *    This structure is used as the basis of one or many wlan_scan_cmd_config
 *    commands that are sent to the command processing module and sent to
 *    firmware.
 *
 *  Create a wlan_scan_cmd_config based on the following user supplied
 *    parameters (if present):
 *             - SSID filter
 *             - BSSID filter
 *             - Number of Probes to be sent
 *             - channel list
 *
 *  If the SSID or BSSID filter is not present, disable/clear the filter.
 *  If the number of probes is not set, use the adapter default setting
 *  Qualify the channel
 *
 *  @param priv             A pointer to wlan_private structure
 *  @param puserscanin      NULL or pointer to scan configuration parameters
 *  @param ppchantlvout     Output parameter: Pointer to the start of the
 *                          channel TLV portion of the output scan config
 *  @param pscanchanlist    Output parameter: Pointer to the resulting channel
 *                          list to scan
 *  @param pmaxchanperscan  Output parameter: Number of channels to scan for
 *                          each issuance of the firmware scan command
 *  @param pfilteredscan    Output parameter: Flag indicating whether or not
 *                          a BSSID or SSID filter is being sent in the
 *                          command to firmware.  Used to increase the number
 *                          of channels sent in a scan command and to
 *                          disable the firmware channel scan filter.
 *  @param pscancurrentonly Output parameter: Flag indicating whether or not
 *                          we are only scanning our current active channel
 *
 *  @return                 resulting scan configuration
 */
static struct wlan_scan_cmd_config *
wlan_scan_setup_scan_config(wlan_private * priv,
			    const struct wlan_ioctl_user_scan_cfg * puserscanin,
			    struct mrvlietypes_chanlistparamset ** ppchantlvout,
			    struct chanscanparamset * pscanchanlist,
			    int *pmaxchanperscan,
			    u8 * pfilteredscan,
			    u8 * pscancurrentonly)
{
	struct mrvlietypes_numprobes *pnumprobestlv;
	struct mrvlietypes_ssidparamset *pssidtlv;
	struct wlan_scan_cmd_config * pscancfgout = NULL;
	u8 *ptlvpos;
	u16 numprobes;
	int chanidx;
	int scantype;
	int scandur;
	int channel;
	int radiotype;

	lbs_deb_enter(LBS_DEB_SCAN);

	pscancfgout = kzalloc(MAX_SCAN_CFG_ALLOC, GFP_KERNEL);
	if (pscancfgout == NULL)
		goto out;

	/* The tlvbufferlen is calculated for each scan command.  The TLVs added
	 *   in this routine will be preserved since the routine that sends
	 *   the command will append channelTLVs at *ppchantlvout.  The difference
	 *   between the *ppchantlvout and the tlvbuffer start will be used
	 *   to calculate the size of anything we add in this routine.
	 */
	pscancfgout->tlvbufferlen = 0;

	/* Running tlv pointer.  Assigned to ppchantlvout at end of function
	 *  so later routines know where channels can be added to the command buf
	 */
	ptlvpos = pscancfgout->tlvbuffer;

	/*
	 * Set the initial scan paramters for progressive scanning.  If a specific
	 *   BSSID or SSID is used, the number of channels in the scan command
	 *   will be increased to the absolute maximum
	 */
	*pmaxchanperscan = MRVDRV_CHANNELS_PER_SCAN_CMD;

	/* Initialize the scan as un-filtered by firmware, set to TRUE below if
	 *   a SSID or BSSID filter is sent in the command
	 */
	*pfilteredscan = 0;

	/* Initialize the scan as not being only on the current channel.  If
	 *   the channel list is customized, only contains one channel, and
	 *   is the active channel, this is set true and data flow is not halted.
	 */
	*pscancurrentonly = 0;

	if (puserscanin) {
		/* Set the bss type scan filter, use adapter setting if unset */
		pscancfgout->bsstype =
		    puserscanin->bsstype ? puserscanin->bsstype : CMD_BSS_TYPE_ANY;

		/* Set the number of probes to send, use adapter setting if unset */
		numprobes = puserscanin->numprobes ? puserscanin->numprobes : 0;

		/*
		 * Set the BSSID filter to the incoming configuration,
		 *   if non-zero.  If not set, it will remain disabled (all zeros).
		 */
		memcpy(pscancfgout->bssid, puserscanin->bssid,
		       sizeof(pscancfgout->bssid));

		if (puserscanin->ssid_len) {
			pssidtlv =
			    (struct mrvlietypes_ssidparamset *) pscancfgout->
			    tlvbuffer;
			pssidtlv->header.type = cpu_to_le16(TLV_TYPE_SSID);
			pssidtlv->header.len = cpu_to_le16(puserscanin->ssid_len);
			memcpy(pssidtlv->ssid, puserscanin->ssid,
			       puserscanin->ssid_len);
			ptlvpos += sizeof(pssidtlv->header) + puserscanin->ssid_len;
		}

		/*
		 *  The default number of channels sent in the command is low to
		 *    ensure the response buffer from the firmware does not truncate
		 *    scan results.  That is not an issue with an SSID or BSSID
		 *    filter applied to the scan results in the firmware.
		 */
		if (   puserscanin->ssid_len
		    || (compare_ether_addr(pscancfgout->bssid, &zeromac[0]) != 0)) {
			*pmaxchanperscan = MRVDRV_MAX_CHANNELS_PER_SCAN;
			*pfilteredscan = 1;
		}
	} else {
		pscancfgout->bsstype = CMD_BSS_TYPE_ANY;
		numprobes = 0;
	}

	/* If the input config or adapter has the number of Probes set, add tlv */
	if (numprobes) {
		pnumprobestlv = (struct mrvlietypes_numprobes *) ptlvpos;
		pnumprobestlv->header.type = cpu_to_le16(TLV_TYPE_NUMPROBES);
		pnumprobestlv->header.len = cpu_to_le16(2);
		pnumprobestlv->numprobes = cpu_to_le16(numprobes);

		ptlvpos += sizeof(*pnumprobestlv);
	}

	/*
	 * Set the output for the channel TLV to the address in the tlv buffer
	 *   past any TLVs that were added in this fuction (SSID, numprobes).
	 *   channel TLVs will be added past this for each scan command, preserving
	 *   the TLVs that were previously added.
	 */
	*ppchantlvout = (struct mrvlietypes_chanlistparamset *) ptlvpos;

	if (!puserscanin || !puserscanin->chanlist[0].channumber) {
		/* Create a default channel scan list */
		lbs_deb_scan("creating full region channel list\n");
		wlan_scan_create_channel_list(priv, pscanchanlist,
					      *pfilteredscan);
		goto out;
	}

	for (chanidx = 0;
	     chanidx < WLAN_IOCTL_USER_SCAN_CHAN_MAX
	     && puserscanin->chanlist[chanidx].channumber; chanidx++) {

		channel = puserscanin->chanlist[chanidx].channumber;
		(pscanchanlist + chanidx)->channumber = channel;

		radiotype = puserscanin->chanlist[chanidx].radiotype;
		(pscanchanlist + chanidx)->radiotype = radiotype;

		scantype = puserscanin->chanlist[chanidx].scantype;

		if (scantype == CMD_SCAN_TYPE_PASSIVE) {
			(pscanchanlist +
			 chanidx)->chanscanmode.passivescan = 1;
		} else {
			(pscanchanlist +
			 chanidx)->chanscanmode.passivescan = 0;
		}

		if (puserscanin->chanlist[chanidx].scantime) {
			scandur = puserscanin->chanlist[chanidx].scantime;
		} else {
			if (scantype == CMD_SCAN_TYPE_PASSIVE) {
				scandur = MRVDRV_PASSIVE_SCAN_CHAN_TIME;
			} else {
				scandur = MRVDRV_ACTIVE_SCAN_CHAN_TIME;
			}
		}

		(pscanchanlist + chanidx)->minscantime =
		    cpu_to_le16(scandur);
		(pscanchanlist + chanidx)->maxscantime =
		    cpu_to_le16(scandur);
	}

	/* Check if we are only scanning the current channel */
	if ((chanidx == 1) &&
	    (puserscanin->chanlist[0].channumber ==
			       priv->adapter->curbssparams.channel)) {
		*pscancurrentonly = 1;
		lbs_deb_scan("scanning current channel only");
	}

out:
	return pscancfgout;
}

/**
 *  @brief Construct and send multiple scan config commands to the firmware
 *
 *  Only used from wlan_scan_networks()
 *
 *  Previous routines have created a wlan_scan_cmd_config with any requested
 *   TLVs.  This function splits the channel TLV into maxchanperscan lists
 *   and sends the portion of the channel TLV along with the other TLVs
 *   to the wlan_cmd routines for execution in the firmware.
 *
 *  @param priv            A pointer to wlan_private structure
 *  @param maxchanperscan  Maximum number channels to be included in each
 *                         scan command sent to firmware
 *  @param filteredscan    Flag indicating whether or not a BSSID or SSID
 *                         filter is being used for the firmware command
 *                         scan command sent to firmware
 *  @param pscancfgout     Scan configuration used for this scan.
 *  @param pchantlvout     Pointer in the pscancfgout where the channel TLV
 *                         should start.  This is past any other TLVs that
 *                         must be sent down in each firmware command.
 *  @param pscanchanlist   List of channels to scan in maxchanperscan segments
 *
 *  @return                0 or error return otherwise
 */
static int wlan_scan_channel_list(wlan_private * priv,
				  int maxchanperscan,
				  u8 filteredscan,
				  struct wlan_scan_cmd_config * pscancfgout,
				  struct mrvlietypes_chanlistparamset * pchantlvout,
				  struct chanscanparamset * pscanchanlist,
				  const struct wlan_ioctl_user_scan_cfg * puserscanin,
				  int full_scan)
{
	struct chanscanparamset *ptmpchan;
	struct chanscanparamset *pstartchan;
	u8 scanband;
	int doneearly;
	int tlvidx;
	int ret = 0;
	int scanned = 0;
	union iwreq_data wrqu;

	lbs_deb_enter_args(LBS_DEB_SCAN, "maxchanperscan %d, filteredscan %d, "
		"full_scan %d", maxchanperscan, filteredscan, full_scan);

	if (!pscancfgout || !pchantlvout || !pscanchanlist) {
		lbs_deb_scan("pscancfgout, pchantlvout or "
			"pscanchanlist is NULL\n");
		ret = -1;
		goto out;
	}

	pchantlvout->header.type = cpu_to_le16(TLV_TYPE_CHANLIST);

	/* Set the temp channel struct pointer to the start of the desired list */
	ptmpchan = pscanchanlist;

	if (priv->adapter->last_scanned_channel && !puserscanin)
		ptmpchan += priv->adapter->last_scanned_channel;

	/* Loop through the desired channel list, sending a new firmware scan
	 *   commands for each maxchanperscan channels (or for 1,6,11 individually
	 *   if configured accordingly)
	 */
	while (ptmpchan->channumber) {

		tlvidx = 0;
		pchantlvout->header.len = 0;
		scanband = ptmpchan->radiotype;
		pstartchan = ptmpchan;
		doneearly = 0;

		/* Construct the channel TLV for the scan command.  Continue to
		 *  insert channel TLVs until:
		 *    - the tlvidx hits the maximum configured per scan command
		 *    - the next channel to insert is 0 (end of desired channel list)
		 *    - doneearly is set (controlling individual scanning of 1,6,11)
		 */
		while (tlvidx < maxchanperscan && ptmpchan->channumber
		       && !doneearly && scanned < 2) {

			lbs_deb_scan("channel %d, radio %d, passive %d, "
				"dischanflt %d, maxscantime %d\n",
				ptmpchan->channumber,
				ptmpchan->radiotype,
			             ptmpchan->chanscanmode.passivescan,
			             ptmpchan->chanscanmode.disablechanfilt,
			             ptmpchan->maxscantime);

			/* Copy the current channel TLV to the command being prepared */
			memcpy(pchantlvout->chanscanparam + tlvidx,
			       ptmpchan, sizeof(pchantlvout->chanscanparam));

			/* Increment the TLV header length by the size appended */
			/* Ew, it would be _so_ nice if we could just declare the
			   variable little-endian and let GCC handle it for us */
			pchantlvout->header.len =
				cpu_to_le16(le16_to_cpu(pchantlvout->header.len) +
					    sizeof(pchantlvout->chanscanparam));

			/*
			 *  The tlv buffer length is set to the number of bytes of the
			 *    between the channel tlv pointer and the start of the
			 *    tlv buffer.  This compensates for any TLVs that were appended
			 *    before the channel list.
			 */
			pscancfgout->tlvbufferlen = ((u8 *) pchantlvout
						     - pscancfgout->tlvbuffer);

			/*  Add the size of the channel tlv header and the data length */
			pscancfgout->tlvbufferlen +=
			    (sizeof(pchantlvout->header)
			     + le16_to_cpu(pchantlvout->header.len));

			/* Increment the index to the channel tlv we are constructing */
			tlvidx++;

			doneearly = 0;

			/* Stop the loop if the *current* channel is in the 1,6,11 set
			 *   and we are not filtering on a BSSID or SSID.
			 */
			if (!filteredscan && (ptmpchan->channumber == 1
					      || ptmpchan->channumber == 6
					      || ptmpchan->channumber == 11)) {
				doneearly = 1;
			}

			/* Increment the tmp pointer to the next channel to be scanned */
			ptmpchan++;
			scanned++;

			/* Stop the loop if the *next* channel is in the 1,6,11 set.
			 *  This will cause it to be the only channel scanned on the next
			 *  interation
			 */
			if (!filteredscan && (ptmpchan->channumber == 1
					      || ptmpchan->channumber == 6
					      || ptmpchan->channumber == 11)) {
				doneearly = 1;
			}
		}

		/* Send the scan command to the firmware with the specified cfg */
		ret = libertas_prepare_and_send_command(priv, CMD_802_11_SCAN, 0,
					    0, 0, pscancfgout);
		if (scanned >= 2 && !full_scan) {
			ret = 0;
			goto done;
		}
		scanned = 0;
	}

done:
	priv->adapter->last_scanned_channel = ptmpchan->channumber;

	if (priv->adapter->last_scanned_channel) {
		/* Schedule the next part of the partial scan */
		if (!full_scan && !priv->adapter->surpriseremoved) {
			cancel_delayed_work(&priv->scan_work);
			queue_delayed_work(priv->work_thread, &priv->scan_work,
			                   msecs_to_jiffies(300));
		}
	} else {
		/* All done, tell userspace the scan table has been updated */
		memset(&wrqu, 0, sizeof(union iwreq_data));
		wireless_send_event(priv->dev, SIOCGIWSCAN, &wrqu, NULL);
	}

out:
	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", ret);
	return ret;
}

/*
 * Only used from wlan_scan_networks()
*/
static void clear_selected_scan_list_entries(wlan_adapter *adapter,
	const struct wlan_ioctl_user_scan_cfg *scan_cfg)
{
	struct bss_descriptor *bss;
	struct bss_descriptor *safe;
	u32 clear_ssid_flag = 0, clear_bssid_flag = 0;

	lbs_deb_enter(LBS_DEB_SCAN);

	if (!scan_cfg)
		goto out;

	if (scan_cfg->clear_ssid && scan_cfg->ssid_len)
		clear_ssid_flag = 1;

	if (scan_cfg->clear_bssid
	    && (compare_ether_addr(scan_cfg->bssid, &zeromac[0]) != 0)
	    && (compare_ether_addr(scan_cfg->bssid, &bcastmac[0]) != 0)) {
		clear_bssid_flag = 1;
	}

	if (!clear_ssid_flag && !clear_bssid_flag)
		goto out;

	mutex_lock(&adapter->lock);
	list_for_each_entry_safe (bss, safe, &adapter->network_list, list) {
		u32 clear = 0;

		/* Check for an SSID match */
		if (   clear_ssid_flag
		    && (bss->ssid_len == scan_cfg->ssid_len)
		    && !memcmp(bss->ssid, scan_cfg->ssid, bss->ssid_len))
			clear = 1;

		/* Check for a BSSID match */
		if (   clear_bssid_flag
		    && !compare_ether_addr(bss->bssid, scan_cfg->bssid))
			clear = 1;

		if (clear) {
			list_move_tail (&bss->list, &adapter->network_free_list);
			clear_bss_descriptor(bss);
		}
	}
	mutex_unlock(&adapter->lock);
out:
	lbs_deb_leave(LBS_DEB_SCAN);
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
 *  @param priv          A pointer to wlan_private structure
 *  @param puserscanin   Pointer to the input configuration for the requested
 *                       scan.
 *  @param full_scan     ???
 *
 *  @return              0 or < 0 if error
 */
int wlan_scan_networks(wlan_private * priv,
                       const struct wlan_ioctl_user_scan_cfg * puserscanin,
                       int full_scan)
{
	wlan_adapter * adapter = priv->adapter;
	struct mrvlietypes_chanlistparamset *pchantlvout;
	struct chanscanparamset * scan_chan_list = NULL;
	struct wlan_scan_cmd_config * scan_cfg = NULL;
	u8 filteredscan;
	u8 scancurrentchanonly;
	int maxchanperscan;
	int ret;
#ifdef CONFIG_LIBERTAS_DEBUG
	struct bss_descriptor * iter_bss;
	int i = 0;
	DECLARE_MAC_BUF(mac);
#endif

	lbs_deb_enter_args(LBS_DEB_SCAN, "full_scan %d", full_scan);

	/* Cancel any partial outstanding partial scans if this scan
	 * is a full scan.
	 */
	if (full_scan && delayed_work_pending(&priv->scan_work))
		cancel_delayed_work(&priv->scan_work);

	scan_chan_list = kzalloc(sizeof(struct chanscanparamset) *
				WLAN_IOCTL_USER_SCAN_CHAN_MAX, GFP_KERNEL);
	if (scan_chan_list == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	scan_cfg = wlan_scan_setup_scan_config(priv,
					       puserscanin,
					       &pchantlvout,
					       scan_chan_list,
					       &maxchanperscan,
					       &filteredscan,
					       &scancurrentchanonly);
	if (scan_cfg == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	clear_selected_scan_list_entries(adapter, puserscanin);

	/* Keep the data path active if we are only scanning our current channel */
	if (!scancurrentchanonly) {
		netif_stop_queue(priv->dev);
		netif_carrier_off(priv->dev);
		if (priv->mesh_dev) {
			netif_stop_queue(priv->mesh_dev);
			netif_carrier_off(priv->mesh_dev);
		}
	}

	ret = wlan_scan_channel_list(priv,
				     maxchanperscan,
				     filteredscan,
				     scan_cfg,
				     pchantlvout,
				     scan_chan_list,
				     puserscanin,
				     full_scan);

#ifdef CONFIG_LIBERTAS_DEBUG
	/* Dump the scan table */
	mutex_lock(&adapter->lock);
	lbs_deb_scan("The scan table contains:\n");
	list_for_each_entry (iter_bss, &adapter->network_list, list) {
		lbs_deb_scan("scan %02d, %s, RSSI, %d, SSID '%s'\n",
		       i++, print_mac(mac, iter_bss->bssid), (s32) iter_bss->rssi,
		       escape_essid(iter_bss->ssid, iter_bss->ssid_len));
	}
	mutex_unlock(&adapter->lock);
#endif

	if (priv->adapter->connect_status == LIBERTAS_CONNECTED) {
		netif_carrier_on(priv->dev);
		netif_wake_queue(priv->dev);
		if (priv->mesh_dev) {
			netif_carrier_on(priv->mesh_dev);
			netif_wake_queue(priv->mesh_dev);
		}
	}

out:
	if (scan_cfg)
		kfree(scan_cfg);

	if (scan_chan_list)
		kfree(scan_chan_list);

	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", ret);
	return ret;
}

/**
 *  @brief Interpret a BSS scan response returned from the firmware
 *
 *  Parse the various fixed fields and IEs passed back for a a BSS probe
 *   response or beacon from the scan command.  Record information as needed
 *   in the scan table struct bss_descriptor for that entry.
 *
 *  @param bss  Output parameter: Pointer to the BSS Entry
 *
 *  @return             0 or -1
 */
static int libertas_process_bss(struct bss_descriptor * bss,
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
		beaconsize = le16_to_cpu(get_unaligned((u16 *)*pbeaconinfo));
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
	lbs_deb_scan("process_bss: AP BSSID %s\n", print_mac(mac, bss->bssid));
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
	lbs_deb_scan("process_bss: RSSI=%02X\n", *pos);
	pos++;

	/* time stamp is 8 bytes long */
	pos += 8;

	/* beacon interval is 2 bytes long */
	bss->beaconperiod = le16_to_cpup((void *) pos);
	pos += 2;

	/* capability information is 2 bytes long */
	bss->capability = le16_to_cpup((void *) pos);
	lbs_deb_scan("process_bss: capabilities = 0x%4X\n", bss->capability);
	pos += 2;

	if (bss->capability & WLAN_CAPABILITY_PRIVACY)
		lbs_deb_scan("process_bss: AP WEP enabled\n");
	if (bss->capability & WLAN_CAPABILITY_IBSS)
		bss->mode = IW_MODE_ADHOC;
	else
		bss->mode = IW_MODE_INFRA;

	/* rest of the current buffer are IE's */
	lbs_deb_scan("process_bss: IE length for this AP = %zd\n", end - pos);
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
			lbs_deb_scan("ssid '%s', ssid length %u\n",
			             escape_essid(bss->ssid, bss->ssid_len),
			             bss->ssid_len);
			break;

		case MFIE_TYPE_RATES:
			n_basic_rates = min_t(u8, MAX_RATES, elem->len);
			memcpy(bss->rates, elem->data, n_basic_rates);
			got_basic_rates = 1;
			break;

		case MFIE_TYPE_FH_SET:
			pFH = (struct ieeetypes_fhparamset *) pos;
			memmove(&bss->phyparamset.fhparamset, pFH,
				sizeof(struct ieeetypes_fhparamset));
#if 0 /* I think we can store these LE */
			bss->phyparamset.fhparamset.dwelltime
			    = le16_to_cpu(bss->phyparamset.fhparamset.dwelltime);
#endif
			break;

		case MFIE_TYPE_DS_SET:
			pDS = (struct ieeetypes_dsparamset *) pos;
			bss->channel = pDS->currentchan;
			memcpy(&bss->phyparamset.dsparamset, pDS,
			       sizeof(struct ieeetypes_dsparamset));
			break;

		case MFIE_TYPE_CF_SET:
			pCF = (struct ieeetypes_cfparamset *) pos;
			memcpy(&bss->ssparamset.cfparamset, pCF,
			       sizeof(struct ieeetypes_cfparamset));
			break;

		case MFIE_TYPE_IBSS_SET:
			pibss = (struct ieeetypes_ibssparamset *) pos;
			bss->atimwindow = le32_to_cpu(pibss->atimwindow);
			memmove(&bss->ssparamset.ibssparamset, pibss,
				sizeof(struct ieeetypes_ibssparamset));
#if 0
			bss->ssparamset.ibssparamset.atimwindow
			    = le16_to_cpu(bss->ssparamset.ibssparamset.atimwindow);
#endif
			break;

		case MFIE_TYPE_COUNTRY:
			pcountryinfo = (struct ieeetypes_countryinfoset *) pos;
			if (pcountryinfo->len < sizeof(pcountryinfo->countrycode)
			    || pcountryinfo->len > 254) {
				lbs_deb_scan("process_bss: 11D- Err "
				       "CountryInfo len =%d min=%zd max=254\n",
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
			if (!got_basic_rates)
				break;

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
				lbs_deb_hex(LBS_DEB_SCAN, "process_bss: WPA IE", bss->wpa_ie,
				            elem->len);
			} else if (elem->len >= MARVELL_MESH_IE_LENGTH &&
			    elem->data[0] == 0x00 &&
			    elem->data[1] == 0x50 &&
			    elem->data[2] == 0x43 &&
			    elem->data[3] == 0x04) {
				bss->mesh = 1;
			}
			break;

		case MFIE_TYPE_RSN:
			bss->rsn_ie_len = min(elem->len + 2, MAX_WPA_IE_LEN);
			memcpy(bss->rsn_ie, elem, bss->rsn_ie_len);
			lbs_deb_hex(LBS_DEB_SCAN, "process_bss: RSN_IE", bss->rsn_ie, elem->len);
			break;

		default:
			break;
		}

		pos += elem->len + 2;
	}

	/* Timestamp */
	bss->last_scanned = jiffies;
	libertas_unset_basic_rate_flags(bss->rates, sizeof(bss->rates));

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
 *  @param adapter  A pointer to wlan_adapter
 *  @param bssid    BSSID to find in the scan list
 *  @param mode     Network mode: Infrastructure or IBSS
 *
 *  @return         index in BSSID list, or error return code (< 0)
 */
struct bss_descriptor *libertas_find_bssid_in_list(wlan_adapter * adapter,
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
	mutex_lock(&adapter->lock);
	list_for_each_entry (iter_bss, &adapter->network_list, list) {
		if (compare_ether_addr(iter_bss->bssid, bssid))
			continue; /* bssid doesn't match */
		switch (mode) {
		case IW_MODE_INFRA:
		case IW_MODE_ADHOC:
			if (!is_network_compatible(adapter, iter_bss, mode))
				break;
			found_bss = iter_bss;
			break;
		default:
			found_bss = iter_bss;
			break;
		}
	}
	mutex_unlock(&adapter->lock);

out:
	lbs_deb_leave_args(LBS_DEB_SCAN, "found_bss %p", found_bss);
	return found_bss;
}

/**
 *  @brief This function finds ssid in ssid list.
 *
 *  Used in association code
 *
 *  @param adapter  A pointer to wlan_adapter
 *  @param ssid     SSID to find in the list
 *  @param bssid    BSSID to qualify the SSID selection (if provided)
 *  @param mode     Network mode: Infrastructure or IBSS
 *
 *  @return         index in BSSID list
 */
struct bss_descriptor * libertas_find_ssid_in_list(wlan_adapter * adapter,
		   u8 *ssid, u8 ssid_len, u8 * bssid, u8 mode,
		   int channel)
{
	u8 bestrssi = 0;
	struct bss_descriptor * iter_bss = NULL;
	struct bss_descriptor * found_bss = NULL;
	struct bss_descriptor * tmp_oldest = NULL;

	lbs_deb_enter(LBS_DEB_SCAN);

	mutex_lock(&adapter->lock);

	list_for_each_entry (iter_bss, &adapter->network_list, list) {
		if (   !tmp_oldest
		    || (iter_bss->last_scanned < tmp_oldest->last_scanned))
			tmp_oldest = iter_bss;

		if (libertas_ssid_cmp(iter_bss->ssid, iter_bss->ssid_len,
		                      ssid, ssid_len) != 0)
			continue; /* ssid doesn't match */
		if (bssid && compare_ether_addr(iter_bss->bssid, bssid) != 0)
			continue; /* bssid doesn't match */
		if ((channel > 0) && (iter_bss->channel != channel))
			continue; /* channel doesn't match */

		switch (mode) {
		case IW_MODE_INFRA:
		case IW_MODE_ADHOC:
			if (!is_network_compatible(adapter, iter_bss, mode))
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
	mutex_unlock(&adapter->lock);
	lbs_deb_leave_args(LBS_DEB_SCAN, "found_bss %p", found_bss);
	return found_bss;
}

/**
 *  @brief This function finds the best SSID in the Scan List
 *
 *  Search the scan table for the best SSID that also matches the current
 *   adapter network preference (infrastructure or adhoc)
 *
 *  @param adapter  A pointer to wlan_adapter
 *
 *  @return         index in BSSID list
 */
static struct bss_descriptor * libertas_find_best_ssid_in_list(wlan_adapter * adapter,
		u8 mode)
{
	u8 bestrssi = 0;
	struct bss_descriptor * iter_bss;
	struct bss_descriptor * best_bss = NULL;

	lbs_deb_enter(LBS_DEB_SCAN);

	mutex_lock(&adapter->lock);

	list_for_each_entry (iter_bss, &adapter->network_list, list) {
		switch (mode) {
		case IW_MODE_INFRA:
		case IW_MODE_ADHOC:
			if (!is_network_compatible(adapter, iter_bss, mode))
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

	mutex_unlock(&adapter->lock);
	lbs_deb_leave_args(LBS_DEB_SCAN, "best_bss %p", best_bss);
	return best_bss;
}

/**
 *  @brief Find the AP with specific ssid in the scan list
 *
 *  Used from association worker.
 *
 *  @param priv         A pointer to wlan_private structure
 *  @param pSSID        A pointer to AP's ssid
 *
 *  @return             0--success, otherwise--fail
 */
int libertas_find_best_network_ssid(wlan_private * priv,
		u8 *out_ssid, u8 *out_ssid_len, u8 preferred_mode, u8 *out_mode)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = -1;
	struct bss_descriptor * found;

	lbs_deb_enter(LBS_DEB_SCAN);

	wlan_scan_networks(priv, NULL, 1);
	if (adapter->surpriseremoved)
		goto out;

	wait_event_interruptible(adapter->cmd_pending, !adapter->nr_cmd_pending);

	found = libertas_find_best_ssid_in_list(adapter, preferred_mode);
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
 *  @brief Scan Network
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param vwrq         A pointer to iw_param structure
 *  @param extra        A pointer to extra data buf
 *
 *  @return             0 --success, otherwise fail
 */
int libertas_set_scan(struct net_device *dev, struct iw_request_info *info,
		  struct iw_param *vwrq, char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_SCAN);

	if (!delayed_work_pending(&priv->scan_work)) {
		queue_delayed_work(priv->work_thread, &priv->scan_work,
		                   msecs_to_jiffies(50));
	}

	if (adapter->surpriseremoved)
		return -1;

	lbs_deb_leave(LBS_DEB_SCAN);
	return 0;
}


/**
 *  @brief Send a scan command for all available channels filtered on a spec
 *
 *  Used in association code and from debugfs
 *
 *  @param priv             A pointer to wlan_private structure
 *  @param ssid             A pointer to the SSID to scan for
 *  @param ssid_len         Length of the SSID
 *  @param clear_ssid       Should existing scan results with this SSID
 *                          be cleared?
 *  @param prequestedssid   A pointer to AP's ssid
 *  @param keeppreviousscan Flag used to save/clear scan table before scan
 *
 *  @return                0-success, otherwise fail
 */
int libertas_send_specific_ssid_scan(wlan_private * priv,
			u8 *ssid, u8 ssid_len, u8 clear_ssid)
{
	wlan_adapter *adapter = priv->adapter;
	struct wlan_ioctl_user_scan_cfg scancfg;
	int ret = 0;

	lbs_deb_enter_args(LBS_DEB_SCAN, "SSID '%s', clear %d",
		escape_essid(ssid, ssid_len), clear_ssid);

	if (!ssid_len)
		goto out;

	memset(&scancfg, 0x00, sizeof(scancfg));
	memcpy(scancfg.ssid, ssid, ssid_len);
	scancfg.ssid_len = ssid_len;
	scancfg.clear_ssid = clear_ssid;

	wlan_scan_networks(priv, &scancfg, 1);
	if (adapter->surpriseremoved) {
		ret = -1;
		goto out;
	}
	wait_event_interruptible(adapter->cmd_pending, !adapter->nr_cmd_pending);

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

static inline char *libertas_translate_scan(wlan_private *priv,
					char *start, char *stop,
					struct bss_descriptor *bss)
{
	wlan_adapter *adapter = priv->adapter;
	struct chan_freq_power *cfp;
	char *current_val;	/* For rates */
	struct iw_event iwe;	/* Temporary buffer */
	int j;
#define PERFECT_RSSI ((u8)50)
#define WORST_RSSI   ((u8)0)
#define RSSI_DIFF    ((u8)(PERFECT_RSSI - WORST_RSSI))
	u8 rssi;

	lbs_deb_enter(LBS_DEB_SCAN);

	cfp = libertas_find_cfp_by_band_and_channel(adapter, 0, bss->channel);
	if (!cfp) {
		lbs_deb_scan("Invalid channel number %d\n", bss->channel);
		start = NULL;
		goto out;
	}

	/* First entry *MUST* be the AP BSSID */
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

	if (adapter->NF[TYPE_BEACON][TYPE_NOAVG] == 0) {
		iwe.u.qual.noise = MRVDRV_NF_DEFAULT_SCAN_VALUE;
	} else {
		iwe.u.qual.noise =
		    CAL_NF(adapter->NF[TYPE_BEACON][TYPE_NOAVG]);
	}

	/* Locally created ad-hoc BSSs won't have beacons if this is the
	 * only station in the adhoc network; so get signal strength
	 * from receive statistics.
	 */
	if ((adapter->mode == IW_MODE_ADHOC)
	    && adapter->adhoccreate
	    && !libertas_ssid_cmp(adapter->curbssparams.ssid,
	                          adapter->curbssparams.ssid_len,
	                          bss->ssid, bss->ssid_len)) {
		int snr, nf;
		snr = adapter->SNR[TYPE_RXPD][TYPE_AVG] / AVG_SCALE;
		nf = adapter->NF[TYPE_RXPD][TYPE_AVG] / AVG_SCALE;
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
	    && !libertas_ssid_cmp(adapter->curbssparams.ssid,
	                          adapter->curbssparams.ssid_len,
	                          bss->ssid, bss->ssid_len)
	    && adapter->adhoccreate) {
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
 *  @brief  Handle Retrieve scan table ioctl
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param dwrq         A pointer to iw_point structure
 *  @param extra        A pointer to extra data buf
 *
 *  @return             0 --success, otherwise fail
 */
int libertas_get_scan(struct net_device *dev, struct iw_request_info *info,
		  struct iw_point *dwrq, char *extra)
{
#define SCAN_ITEM_SIZE 128
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	int err = 0;
	char *ev = extra;
	char *stop = ev + dwrq->length;
	struct bss_descriptor * iter_bss;
	struct bss_descriptor * safe;

	lbs_deb_enter(LBS_DEB_SCAN);

	/* Update RSSI if current BSS is a locally created ad-hoc BSS */
	if ((adapter->mode == IW_MODE_ADHOC) && adapter->adhoccreate) {
		libertas_prepare_and_send_command(priv, CMD_802_11_RSSI, 0,
					CMD_OPTION_WAITFORRSP, 0, NULL);
	}

	mutex_lock(&adapter->lock);
	list_for_each_entry_safe (iter_bss, safe, &adapter->network_list, list) {
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
			                &adapter->network_free_list);
			clear_bss_descriptor(iter_bss);
			continue;
		}

		/* Translate to WE format this entry */
		next_ev = libertas_translate_scan(priv, ev, stop, iter_bss);
		if (next_ev == NULL)
			continue;
		ev = next_ev;
	}
	mutex_unlock(&adapter->lock);

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
 *  Called from libertas_prepare_and_send_command() in cmd.c
 *
 *  Sends a fixed lenght data part (specifying the BSS type and BSSID filters)
 *  as well as a variable number/length of TLVs to the firmware.
 *
 *  @param priv       A pointer to wlan_private structure
 *  @param cmd        A pointer to cmd_ds_command structure to be sent to
 *                    firmware with the cmd_DS_801_11_SCAN structure
 *  @param pdata_buf  Void pointer cast of a wlan_scan_cmd_config struct used
 *                    to set the fields/TLVs for the command sent to firmware
 *
 *  @return           0 or -1
 */
int libertas_cmd_80211_scan(wlan_private * priv,
			 struct cmd_ds_command *cmd, void *pdata_buf)
{
	struct cmd_ds_802_11_scan *pscan = &cmd->params.scan;
	struct wlan_scan_cmd_config *pscancfg = pdata_buf;

	lbs_deb_enter(LBS_DEB_SCAN);

	/* Set fixed field variables in scan command */
	pscan->bsstype = pscancfg->bsstype;
	memcpy(pscan->bssid, pscancfg->bssid, ETH_ALEN);
	memcpy(pscan->tlvbuffer, pscancfg->tlvbuffer, pscancfg->tlvbufferlen);

	cmd->command = cpu_to_le16(CMD_802_11_SCAN);

	/* size is equal to the sizeof(fixed portions) + the TLV len + header */
	cmd->size = cpu_to_le16(sizeof(pscan->bsstype) + ETH_ALEN
				+ pscancfg->tlvbufferlen + S_DS_GEN);

	lbs_deb_scan("SCAN_CMD: command 0x%04x, size %d, seqnum %d\n",
		     le16_to_cpu(cmd->command), le16_to_cpu(cmd->size),
		     le16_to_cpu(cmd->seqnum));

	lbs_deb_leave(LBS_DEB_SCAN);
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
 *  @param priv    A pointer to wlan_private structure
 *  @param resp    A pointer to cmd_ds_command
 *
 *  @return        0 or -1
 */
int libertas_ret_80211_scan(wlan_private * priv, struct cmd_ds_command *resp)
{
	wlan_adapter *adapter = priv->adapter;
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
	list_for_each_entry_safe (iter_bss, safe, &adapter->network_list, list) {
		unsigned long stale_time = iter_bss->last_scanned + DEFAULT_MAX_SCAN_AGE;
		if (time_before(jiffies, stale_time))
			continue;
		list_move_tail (&iter_bss->list, &adapter->network_free_list);
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

	bytesleft = le16_to_cpu(get_unaligned((u16*)&pscan->bssdescriptsize));
	lbs_deb_scan("SCAN_RESP: bssdescriptsize %d\n", bytesleft);

	scanrespsize = le16_to_cpu(get_unaligned((u16*)&resp->size));
	lbs_deb_scan("SCAN_RESP: returned %d AP before parsing\n",
	       pscan->nr_sets);

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
		if (libertas_process_bss(&new, &pbssinfo, &bytesleft) != 0) {
			/* error parsing the scan response, skipped */
			lbs_deb_scan("SCAN_RESP: process_bss returned ERROR\n");
			continue;
		}

		/* Try to find this bss in the scan table */
		list_for_each_entry (iter_bss, &adapter->network_list, list) {
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
		} else if (!list_empty(&adapter->network_free_list)) {
			/* Pull one from the free list */
			found = list_entry(adapter->network_free_list.next,
					   struct bss_descriptor, list);
			list_move_tail(&found->list, &adapter->network_list);
		} else if (oldest) {
			/* If there are no more slots, expire the oldest */
			found = oldest;
			clear_bss_descriptor(found);
			list_move_tail(&found->list, &adapter->network_list);
		} else {
			continue;
		}

		lbs_deb_scan("SCAN_RESP: BSSID = %s\n",
			     print_mac(mac, new.bssid));

		/* Copy the locally created newbssentry to the scan table */
		memcpy(found, &new, offsetof(struct bss_descriptor, list));
	}

	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", ret);
	return ret;
}
