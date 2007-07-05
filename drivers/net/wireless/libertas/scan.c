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

#include "host.h"
#include "decl.h"
#include "dev.h"
#include "scan.h"

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
	    && match_bss->wpa_ie[0] != WPA_IE
	    && match_bss->rsn_ie[0] != WPA2_IE
	    && !match_bss->privacy) {
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
	   && match_bss->privacy) {
		return 1;
	}
	return 0;
}

static inline int match_bss_wpa(struct wlan_802_11_security * secinfo,
			struct bss_descriptor * match_bss)
{
	if (  !secinfo->wep_enabled
	   && secinfo->WPAenabled
	   && (match_bss->wpa_ie[0] == WPA_IE)
	   /* privacy bit may NOT be set in some APs like LinkSys WRT54G
	      && bss->privacy */
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
	   && (match_bss->rsn_ie[0] == WPA2_IE)
	   /* privacy bit may NOT be set in some APs like LinkSys WRT54G
	      && bss->privacy */
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
	   && (match_bss->wpa_ie[0] != WPA_IE)
	   && (match_bss->rsn_ie[0] != WPA2_IE)
	   && match_bss->privacy) {
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

	lbs_deb_enter(LBS_DEB_ASSOC);

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
		       bss->privacy);
		goto done;
	} else if ((matched = match_bss_wpa2(&adapter->secinfo, bss))) {
		lbs_deb_scan(
		       "is_network_compatible() WPA2: wpa_ie=%#x "
		       "wpa2_ie=%#x WEP=%s WPA=%s WPA2=%s "
		       "privacy=%#x\n", bss->wpa_ie[0], bss->rsn_ie[0],
		       adapter->secinfo.wep_enabled ? "e" : "d",
		       adapter->secinfo.WPAenabled ? "e" : "d",
		       adapter->secinfo.WPA2enabled ? "e" : "d",
		       bss->privacy);
		goto done;
	} else if ((matched = match_bss_dynamic_wep(&adapter->secinfo, bss))) {
		lbs_deb_scan(
		       "is_network_compatible() dynamic WEP: "
		       "wpa_ie=%#x wpa2_ie=%#x privacy=%#x\n",
		       bss->wpa_ie[0],
		       bss->rsn_ie[0],
		       bss->privacy);
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
	       bss->privacy);

done:
	lbs_deb_leave(LBS_DEB_SCAN);
	return matched;
}

/**
 *  @brief Create a channel list for the driver to scan based on region info
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

	chanidx = 0;

	/* Set the default scan type to the user specified type, will later
	 *   be changed to passive on a per channel basis if restricted by
	 *   regulatory requirements (11d or 11h)
	 */
	scantype = adapter->scantype;

	for (rgnidx = 0; rgnidx < ARRAY_SIZE(adapter->region_channel); rgnidx++) {
		if (priv->adapter->enable11d &&
		    adapter->connect_status != libertas_connected) {
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
				    cmd_scan_radio_type_bg;
				break;
			}

			if (scantype == cmd_scan_type_passive) {
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
	wlan_adapter *adapter = priv->adapter;
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
		    (puserscanin->bsstype ? puserscanin->bsstype : adapter->
		     scanmode);

		/* Set the number of probes to send, use adapter setting if unset */
		numprobes = (puserscanin->numprobes ? puserscanin->numprobes :
			     adapter->scanprobes);

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
		pscancfgout->bsstype = adapter->scanmode;
		numprobes = adapter->scanprobes;
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

	if (puserscanin && puserscanin->chanlist[0].channumber) {

		lbs_deb_scan("Scan: Using supplied channel list\n");

		for (chanidx = 0;
		     chanidx < WLAN_IOCTL_USER_SCAN_CHAN_MAX
		     && puserscanin->chanlist[chanidx].channumber; chanidx++) {

			channel = puserscanin->chanlist[chanidx].channumber;
			(pscanchanlist + chanidx)->channumber = channel;

			radiotype = puserscanin->chanlist[chanidx].radiotype;
			(pscanchanlist + chanidx)->radiotype = radiotype;

			scantype = puserscanin->chanlist[chanidx].scantype;

			if (scantype == cmd_scan_type_passive) {
				(pscanchanlist +
				 chanidx)->chanscanmode.passivescan = 1;
			} else {
				(pscanchanlist +
				 chanidx)->chanscanmode.passivescan = 0;
			}

			if (puserscanin->chanlist[chanidx].scantime) {
				scandur =
				    puserscanin->chanlist[chanidx].scantime;
			} else {
				if (scantype == cmd_scan_type_passive) {
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
		if ((chanidx == 1) && (puserscanin->chanlist[0].channumber
				       ==
				       priv->adapter->curbssparams.channel)) {
			*pscancurrentonly = 1;
			lbs_deb_scan("Scan: Scanning current channel only");
		}

	} else {
		lbs_deb_scan("Scan: Creating full region channel list\n");
		wlan_scan_create_channel_list(priv, pscanchanlist,
					      *pfilteredscan);
	}

out:
	return pscancfgout;
}

/**
 *  @brief Construct and send multiple scan config commands to the firmware
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

	lbs_deb_enter(LBS_DEB_ASSOC);

	if (pscancfgout == 0 || pchantlvout == 0 || pscanchanlist == 0) {
		lbs_deb_scan("Scan: Null detect: %p, %p, %p\n",
		       pscancfgout, pchantlvout, pscanchanlist);
		return -1;
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

            lbs_deb_scan(
                    "Scan: Chan(%3d), Radio(%d), mode(%d,%d), Dur(%d)\n",
                ptmpchan->channumber, ptmpchan->radiotype,
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
		ret = libertas_prepare_and_send_command(priv, cmd_802_11_scan, 0,
					    0, 0, pscancfgout);
		if (scanned >= 2 && !full_scan) {
			ret = 0;
			goto done;
		}
		scanned = 0;
	}

done:
	priv->adapter->last_scanned_channel = ptmpchan->channumber;

	/* Tell userspace the scan table has been updated */
	memset(&wrqu, 0, sizeof(union iwreq_data));
	wireless_send_event(priv->dev, SIOCGIWSCAN, &wrqu, NULL);

	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", ret);
	return ret;
}

static void
clear_selected_scan_list_entries(wlan_adapter * adapter,
                                 const struct wlan_ioctl_user_scan_cfg * scan_cfg)
{
	struct bss_descriptor * bss;
	struct bss_descriptor * safe;
	u32 clear_ssid_flag = 0, clear_bssid_flag = 0;

	if (!scan_cfg)
		return;

	if (scan_cfg->clear_ssid && scan_cfg->ssid_len)
		clear_ssid_flag = 1;

	if (scan_cfg->clear_bssid
	    && (compare_ether_addr(scan_cfg->bssid, &zeromac[0]) != 0)
	    && (compare_ether_addr(scan_cfg->bssid, &bcastmac[0]) != 0)) {
		clear_bssid_flag = 1;
	}

	if (!clear_ssid_flag && !clear_bssid_flag)
		return;

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
}


/**
 *  @brief Internal function used to start a scan based on an input config
 *
 *  Use the input user scan configuration information when provided in
 *    order to send the appropriate scan commands to firmware to populate or
 *    update the internal driver scan table
 *
 *  @param priv          A pointer to wlan_private structure
 *  @param puserscanin   Pointer to the input configuration for the requested
 *                       scan.
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
#endif

	lbs_deb_enter(LBS_DEB_ASSOC);

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
		netif_stop_queue(priv->mesh_dev);
		netif_carrier_off(priv->mesh_dev);
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
	list_for_each_entry (iter_bss, &adapter->network_list, list) {
		lbs_deb_scan("Scan:(%02d) " MAC_FMT ", RSSI[%03d], SSID[%s]\n",
		       i++, MAC_ARG(iter_bss->bssid), (s32) iter_bss->rssi,
		       escape_essid(iter_bss->ssid, iter_bss->ssid_len));
	}
	mutex_unlock(&adapter->lock);
#endif

	if (priv->adapter->connect_status == libertas_connected) {
		netif_carrier_on(priv->dev);
		netif_wake_queue(priv->dev);
		netif_carrier_on(priv->mesh_dev);
		netif_wake_queue(priv->mesh_dev);
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
 *  @brief Inspect the scan response buffer for pointers to expected TLVs
 *
 *  TLVs can be included at the end of the scan response BSS information.
 *    Parse the data in the buffer for pointers to TLVs that can potentially
 *    be passed back in the response
 *
 *  @param ptlv        Pointer to the start of the TLV buffer to parse
 *  @param tlvbufsize  size of the TLV buffer
 *  @param ptsftlv     Output parameter: Pointer to the TSF TLV if found
 *
 *  @return            void
 */
static
void wlan_ret_802_11_scan_get_tlv_ptrs(struct mrvlietypes_data * ptlv,
				       int tlvbufsize,
				       struct mrvlietypes_tsftimestamp ** ptsftlv)
{
	struct mrvlietypes_data *pcurrenttlv;
	int tlvbufleft;
	u16 tlvtype;
	u16 tlvlen;

	pcurrenttlv = ptlv;
	tlvbufleft = tlvbufsize;
	*ptsftlv = NULL;

	lbs_deb_scan("SCAN_RESP: tlvbufsize = %d\n", tlvbufsize);
	lbs_dbg_hex("SCAN_RESP: TLV Buf", (u8 *) ptlv, tlvbufsize);

	while (tlvbufleft >= sizeof(struct mrvlietypesheader)) {
		tlvtype = le16_to_cpu(pcurrenttlv->header.type);
		tlvlen = le16_to_cpu(pcurrenttlv->header.len);

		switch (tlvtype) {
		case TLV_TYPE_TSFTIMESTAMP:
			*ptsftlv = (struct mrvlietypes_tsftimestamp *) pcurrenttlv;
			break;

		default:
			lbs_deb_scan("SCAN_RESP: Unhandled TLV = %d\n",
			       tlvtype);
			/* Give up, this seems corrupted */
			return;
		}		/* switch */

		tlvbufleft -= (sizeof(ptlv->header) + tlvlen);
		pcurrenttlv =
		    (struct mrvlietypes_data *) (pcurrenttlv->Data + tlvlen);
	}			/* while */
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
	enum ieeetypes_elementid elemID;
	struct ieeetypes_fhparamset *pFH;
	struct ieeetypes_dsparamset *pDS;
	struct ieeetypes_cfparamset *pCF;
	struct ieeetypes_ibssparamset *pibss;
	struct ieeetypes_capinfo *pcap;
	struct WLAN_802_11_FIXED_IEs fixedie;
	u8 *pcurrentptr;
	u8 *pRate;
	u8 elemlen;
	u8 bytestocopy;
	u8 ratesize;
	u16 beaconsize;
	u8 founddatarateie;
	int bytesleftforcurrentbeacon;
	int ret;

	struct IE_WPA *pIe;
	const u8 oui01[4] = { 0x00, 0x50, 0xf2, 0x01 };

	struct ieeetypes_countryinfoset *pcountryinfo;

	lbs_deb_enter(LBS_DEB_ASSOC);

	founddatarateie = 0;
	ratesize = 0;
	beaconsize = 0;

	if (*bytesleft >= sizeof(beaconsize)) {
		/* Extract & convert beacon size from the command buffer */
		beaconsize = le16_to_cpup((void *)*pbeaconinfo);
		*bytesleft -= sizeof(beaconsize);
		*pbeaconinfo += sizeof(beaconsize);
	}

	if (beaconsize == 0 || beaconsize > *bytesleft) {

		*pbeaconinfo += *bytesleft;
		*bytesleft = 0;

		return -1;
	}

	/* Initialize the current working beacon pointer for this BSS iteration */
	pcurrentptr = *pbeaconinfo;

	/* Advance the return beacon pointer past the current beacon */
	*pbeaconinfo += beaconsize;
	*bytesleft -= beaconsize;

	bytesleftforcurrentbeacon = beaconsize;

	memcpy(bss->bssid, pcurrentptr, ETH_ALEN);
	lbs_deb_scan("process_bss: AP BSSID " MAC_FMT "\n", MAC_ARG(bss->bssid));

	pcurrentptr += ETH_ALEN;
	bytesleftforcurrentbeacon -= ETH_ALEN;

	if (bytesleftforcurrentbeacon < 12) {
		lbs_deb_scan("process_bss: Not enough bytes left\n");
		return -1;
	}

	/*
	 * next 4 fields are RSSI, time stamp, beacon interval,
	 *   and capability information
	 */

	/* RSSI is 1 byte long */
	bss->rssi = *pcurrentptr;
	lbs_deb_scan("process_bss: RSSI=%02X\n", *pcurrentptr);
	pcurrentptr += 1;
	bytesleftforcurrentbeacon -= 1;

	/* time stamp is 8 bytes long */
	fixedie.timestamp = bss->timestamp = le64_to_cpup((void *)pcurrentptr);
	pcurrentptr += 8;
	bytesleftforcurrentbeacon -= 8;

	/* beacon interval is 2 bytes long */
	fixedie.beaconinterval = bss->beaconperiod = le16_to_cpup((void *)pcurrentptr);
	pcurrentptr += 2;
	bytesleftforcurrentbeacon -= 2;

	/* capability information is 2 bytes long */
        memcpy(&fixedie.capabilities, pcurrentptr, 2);
	lbs_deb_scan("process_bss: fixedie.capabilities=0x%X\n",
	       fixedie.capabilities);
	pcap = (struct ieeetypes_capinfo *) & fixedie.capabilities;
	memcpy(&bss->cap, pcap, sizeof(struct ieeetypes_capinfo));
	pcurrentptr += 2;
	bytesleftforcurrentbeacon -= 2;

	/* rest of the current buffer are IE's */
	lbs_deb_scan("process_bss: IE length for this AP = %d\n",
	       bytesleftforcurrentbeacon);

	lbs_dbg_hex("process_bss: IE info", (u8 *) pcurrentptr,
		bytesleftforcurrentbeacon);

	if (pcap->privacy) {
		lbs_deb_scan("process_bss: AP WEP enabled\n");
		bss->privacy = wlan802_11privfilter8021xWEP;
	} else {
		bss->privacy = wlan802_11privfilteracceptall;
	}

	if (pcap->ibss == 1) {
		bss->mode = IW_MODE_ADHOC;
	} else {
		bss->mode = IW_MODE_INFRA;
	}

	/* process variable IE */
	while (bytesleftforcurrentbeacon >= 2) {
		elemID = (enum ieeetypes_elementid) (*((u8 *) pcurrentptr));
		elemlen = *((u8 *) pcurrentptr + 1);

		if (bytesleftforcurrentbeacon < elemlen) {
			lbs_deb_scan("process_bss: error in processing IE, "
			       "bytes left < IE length\n");
			bytesleftforcurrentbeacon = 0;
			continue;
		}

		switch (elemID) {
		case SSID:
			bss->ssid_len = elemlen;
			memcpy(bss->ssid, (pcurrentptr + 2), elemlen);
			lbs_deb_scan("ssid '%s', ssid length %u\n",
			             escape_essid(bss->ssid, bss->ssid_len),
			             bss->ssid_len);
			break;

		case SUPPORTED_RATES:
			memcpy(bss->datarates, (pcurrentptr + 2), elemlen);
			memmove(bss->libertas_supported_rates, (pcurrentptr + 2),
				elemlen);
			ratesize = elemlen;
			founddatarateie = 1;
			break;

		case EXTRA_IE:
			lbs_deb_scan("process_bss: EXTRA_IE Found!\n");
			break;

		case FH_PARAM_SET:
			pFH = (struct ieeetypes_fhparamset *) pcurrentptr;
			memmove(&bss->phyparamset.fhparamset, pFH,
				sizeof(struct ieeetypes_fhparamset));
#if 0 /* I think we can store these LE */
			bss->phyparamset.fhparamset.dwelltime
			    = le16_to_cpu(bss->phyparamset.fhparamset.dwelltime);
#endif
			break;

		case DS_PARAM_SET:
			pDS = (struct ieeetypes_dsparamset *) pcurrentptr;
			bss->channel = pDS->currentchan;
			memcpy(&bss->phyparamset.dsparamset, pDS,
			       sizeof(struct ieeetypes_dsparamset));
			break;

		case CF_PARAM_SET:
			pCF = (struct ieeetypes_cfparamset *) pcurrentptr;
			memcpy(&bss->ssparamset.cfparamset, pCF,
			       sizeof(struct ieeetypes_cfparamset));
			break;

		case IBSS_PARAM_SET:
			pibss = (struct ieeetypes_ibssparamset *) pcurrentptr;
			bss->atimwindow = le32_to_cpu(pibss->atimwindow);
			memmove(&bss->ssparamset.ibssparamset, pibss,
				sizeof(struct ieeetypes_ibssparamset));
#if 0
			bss->ssparamset.ibssparamset.atimwindow
			    = le16_to_cpu(bss->ssparamset.ibssparamset.atimwindow);
#endif
			break;

			/* Handle Country Info IE */
		case COUNTRY_INFO:
			pcountryinfo = (struct ieeetypes_countryinfoset *) pcurrentptr;
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
			lbs_dbg_hex("process_bss: 11D- CountryInfo:",
				(u8 *) pcountryinfo,
				(u32) (pcountryinfo->len + 2));
			break;

		case EXTENDED_SUPPORTED_RATES:
			/*
			 * only process extended supported rate
			 * if data rate is already found.
			 * data rate IE should come before
			 * extended supported rate IE
			 */
			if (founddatarateie) {
				if ((elemlen + ratesize) > WLAN_SUPPORTED_RATES) {
					bytestocopy =
					    (WLAN_SUPPORTED_RATES - ratesize);
				} else {
					bytestocopy = elemlen;
				}

				pRate = (u8 *) bss->datarates;
				pRate += ratesize;
				memmove(pRate, (pcurrentptr + 2), bytestocopy);
				pRate = (u8 *) bss->libertas_supported_rates;
				pRate += ratesize;
				memmove(pRate, (pcurrentptr + 2), bytestocopy);
			}
			break;

		case VENDOR_SPECIFIC_221:
#define IE_ID_LEN_FIELDS_BYTES 2
			pIe = (struct IE_WPA *)pcurrentptr;

			if (memcmp(pIe->oui, oui01, sizeof(oui01)))
				break;

			bss->wpa_ie_len = min(elemlen + IE_ID_LEN_FIELDS_BYTES,
				MAX_WPA_IE_LEN);
			memcpy(bss->wpa_ie, pcurrentptr, bss->wpa_ie_len);
			lbs_dbg_hex("process_bss: WPA IE", bss->wpa_ie, elemlen);
			break;
		case WPA2_IE:
			pIe = (struct IE_WPA *)pcurrentptr;
			bss->rsn_ie_len = min(elemlen + IE_ID_LEN_FIELDS_BYTES,
				MAX_WPA_IE_LEN);
			memcpy(bss->rsn_ie, pcurrentptr, bss->rsn_ie_len);
			lbs_dbg_hex("process_bss: RSN_IE", bss->rsn_ie, elemlen);
			break;
		case TIM:
			break;

		case CHALLENGE_TEXT:
			break;
		}

		pcurrentptr += elemlen + 2;

		/* need to account for IE ID and IE len */
		bytesleftforcurrentbeacon -= (elemlen + 2);

	}			/* while (bytesleftforcurrentbeacon > 2) */

	/* Timestamp */
	bss->last_scanned = jiffies;

	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", ret);
	return ret;
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

/**
 *  @brief This function finds a specific compatible BSSID in the scan list
 *
 *  @param adapter  A pointer to wlan_adapter
 *  @param bssid    BSSID to find in the scan list
 *  @param mode     Network mode: Infrastructure or IBSS
 *
 *  @return         index in BSSID list, or error return code (< 0)
 */
struct bss_descriptor * libertas_find_bssid_in_list(wlan_adapter * adapter,
		u8 * bssid, u8 mode)
{
	struct bss_descriptor * iter_bss;
	struct bss_descriptor * found_bss = NULL;

	if (!bssid)
		return NULL;

	lbs_dbg_hex("libertas_find_BSSID_in_list: looking for ",
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

	return found_bss;
}

/**
 *  @brief This function finds ssid in ssid list.
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
struct bss_descriptor * libertas_find_best_ssid_in_list(wlan_adapter * adapter,
		u8 mode)
{
	u8 bestrssi = 0;
	struct bss_descriptor * iter_bss;
	struct bss_descriptor * best_bss = NULL;

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
	return best_bss;
}

/**
 *  @brief Find the AP with specific ssid in the scan list
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

	lbs_deb_enter(LBS_DEB_ASSOC);

	wlan_scan_networks(priv, NULL, 1);
	if (adapter->surpriseremoved)
		return -1;

	wait_event_interruptible(adapter->cmd_pending, !adapter->nr_cmd_pending);

	found = libertas_find_best_ssid_in_list(adapter, preferred_mode);
	if (found && (found->ssid_len > 0)) {
		memcpy(out_ssid, &found->ssid, IW_ESSID_MAX_SIZE);
		*out_ssid_len = found->ssid_len;
		*out_mode = found->mode;
		ret = 0;
	}

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

	wlan_scan_networks(priv, NULL, 0);

	if (adapter->surpriseremoved)
		return -1;

	lbs_deb_leave(LBS_DEB_SCAN);
	return 0;
}

/**
 *  @brief Send a scan command for all available channels filtered on a spec
 *
 *  @param priv             A pointer to wlan_private structure
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

	lbs_deb_enter(LBS_DEB_ASSOC);

	if (!ssid_len)
		goto out;

	memset(&scancfg, 0x00, sizeof(scancfg));
	memcpy(scancfg.ssid, ssid, ssid_len);
	scancfg.ssid_len = ssid_len;
	scancfg.clear_ssid = clear_ssid;

	wlan_scan_networks(priv, &scancfg, 1);
	if (adapter->surpriseremoved)
		return -1;
	wait_event_interruptible(adapter->cmd_pending, !adapter->nr_cmd_pending);

out:
	lbs_deb_leave(LBS_DEB_ASSOC);
	return ret;
}

/**
 *  @brief scan an AP with specific BSSID
 *
 *  @param priv             A pointer to wlan_private structure
 *  @param bssid            A pointer to AP's bssid
 *  @param keeppreviousscan Flag used to save/clear scan table before scan
 *
 *  @return          0-success, otherwise fail
 */
int libertas_send_specific_bssid_scan(wlan_private * priv, u8 * bssid, u8 clear_bssid)
{
	struct wlan_ioctl_user_scan_cfg scancfg;

	lbs_deb_enter(LBS_DEB_ASSOC);

	if (bssid == NULL)
		goto out;

	memset(&scancfg, 0x00, sizeof(scancfg));
	memcpy(scancfg.bssid, bssid, ETH_ALEN);
	scancfg.clear_bssid = clear_bssid;

	wlan_scan_networks(priv, &scancfg, 1);
	if (priv->adapter->surpriseremoved)
		return -1;
	wait_event_interruptible(priv->adapter->cmd_pending,
		!priv->adapter->nr_cmd_pending);

out:
	lbs_deb_leave(LBS_DEB_ASSOC);
	return 0;
}

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

	cfp = libertas_find_cfp_by_band_and_channel(adapter, 0, bss->channel);
	if (!cfp) {
		lbs_deb_scan("Invalid channel number %d\n", bss->channel);
		return NULL;
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
	if (bss->privacy) {
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

	for (j = 0; j < sizeof(bss->libertas_supported_rates); j++) {
		u8 rate = bss->libertas_supported_rates[j];
		if (rate == 0)
			break; /* no more rates */
		/* Bit rate given in 500 kb/s units (+ 0x80) */
		iwe.u.bitrate.value = (rate & 0x7f) * 500000;
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

	return start;
}

/**
 *  @brief  Retrieve the scan table entries via wireless tools IOCTL call
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

	lbs_deb_enter(LBS_DEB_ASSOC);

	/* If we've got an uncompleted scan, schedule the next part */
	if (!adapter->nr_cmd_pending && adapter->last_scanned_channel)
		wlan_scan_networks(priv, NULL, 0);

	/* Update RSSI if current BSS is a locally created ad-hoc BSS */
	if ((adapter->mode == IW_MODE_ADHOC) && adapter->adhoccreate) {
		libertas_prepare_and_send_command(priv, cmd_802_11_rssi, 0,
					cmd_option_waitforrsp, 0, NULL);
	}

	mutex_lock(&adapter->lock);
	list_for_each_entry_safe (iter_bss, safe, &adapter->network_list, list) {
		char * next_ev;
		unsigned long stale_time;

		if (stop - ev < SCAN_ITEM_SIZE) {
			err = -E2BIG;
			break;
		}

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

	lbs_deb_leave(LBS_DEB_ASSOC);
	return err;
}

/**
 *  @brief Prepare a scan command to be sent to the firmware
 *
 *  Use the wlan_scan_cmd_config sent to the command processing module in
 *   the libertas_prepare_and_send_command to configure a cmd_ds_802_11_scan command
 *   struct to send to firmware.
 *
 *  The fixed fields specifying the BSS type and BSSID filters as well as a
 *   variable number/length of TLVs are sent in the command to firmware.
 *
 *  @param priv       A pointer to wlan_private structure
 *  @param cmd        A pointer to cmd_ds_command structure to be sent to
 *                    firmware with the cmd_DS_801_11_SCAN structure
 *  @param pdata_buf  Void pointer cast of a wlan_scan_cmd_config struct used
 *                    to set the fields/TLVs for the command sent to firmware
 *
 *  @return           0 or -1
 *
 *  @sa wlan_scan_create_channel_list
 */
int libertas_cmd_80211_scan(wlan_private * priv,
			 struct cmd_ds_command *cmd, void *pdata_buf)
{
	struct cmd_ds_802_11_scan *pscan = &cmd->params.scan;
	struct wlan_scan_cmd_config *pscancfg;

	lbs_deb_enter(LBS_DEB_ASSOC);

	pscancfg = pdata_buf;

	/* Set fixed field variables in scan command */
	pscan->bsstype = pscancfg->bsstype;
	memcpy(pscan->BSSID, pscancfg->bssid, sizeof(pscan->BSSID));
	memcpy(pscan->tlvbuffer, pscancfg->tlvbuffer, pscancfg->tlvbufferlen);

	cmd->command = cpu_to_le16(cmd_802_11_scan);

	/* size is equal to the sizeof(fixed portions) + the TLV len + header */
	cmd->size = cpu_to_le16(sizeof(pscan->bsstype)
				     + sizeof(pscan->BSSID)
				     + pscancfg->tlvbufferlen + S_DS_GEN);

	lbs_deb_scan("SCAN_CMD: command=%x, size=%x, seqnum=%x\n",
		     le16_to_cpu(cmd->command), le16_to_cpu(cmd->size),
		     le16_to_cpu(cmd->seqnum));

	lbs_deb_leave(LBS_DEB_ASSOC);
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
	struct mrvlietypes_data *ptlv;
	struct mrvlietypes_tsftimestamp *ptsftlv;
	struct bss_descriptor * iter_bss;
	struct bss_descriptor * safe;
	u8 *pbssinfo;
	u16 scanrespsize;
	int bytesleft;
	int idx;
	int tlvbufsize;
	int ret;

	lbs_deb_enter(LBS_DEB_ASSOC);

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

	bytesleft = le16_to_cpu(pscan->bssdescriptsize);
	lbs_deb_scan("SCAN_RESP: bssdescriptsize %d\n", bytesleft);

	scanrespsize = le16_to_cpu(resp->size);
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

	ptlv = (struct mrvlietypes_data *) (pscan->bssdesc_and_tlvbuffer + bytesleft);

	/* Search the TLV buffer space in the scan response for any valid TLVs */
	wlan_ret_802_11_scan_get_tlv_ptrs(ptlv, tlvbufsize, &ptsftlv);

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

		lbs_deb_scan("SCAN_RESP: BSSID = " MAC_FMT "\n",
		       new.bssid[0], new.bssid[1], new.bssid[2],
		       new.bssid[3], new.bssid[4], new.bssid[5]);

		/*
		 * If the TSF TLV was appended to the scan results, save the
		 *   this entries TSF value in the networktsf field.  The
		 *   networktsf is the firmware's TSF value at the time the
		 *   beacon or probe response was received.
		 */
		if (ptsftlv) {
			new.networktsf = le64_to_cpup(&ptsftlv->tsftable[idx]);
		}

		/* Copy the locally created newbssentry to the scan table */
		memcpy(found, &new, offsetof(struct bss_descriptor, list));
	}

	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", ret);
	return ret;
}
