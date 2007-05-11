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

//! Macro to enable/disable SSID checking before storing a scan table
#ifdef DISCARD_BAD_SSID
#define CHECK_SSID_IS_VALID(x) ssid_valid(&bssidEntry.ssid)
#else
#define CHECK_SSID_IS_VALID(x) 1
#endif

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
static int is_network_compatible(wlan_adapter * adapter, int index, u8 mode)
{
	ENTER();

	if (adapter->scantable[index].mode == mode) {
		if (   !adapter->secinfo.wep_enabled
		    && !adapter->secinfo.WPAenabled
		    && !adapter->secinfo.WPA2enabled
		    && adapter->scantable[index].wpa_ie[0] != WPA_IE
		    && adapter->scantable[index].rsn_ie[0] != WPA2_IE
		    && !adapter->scantable[index].privacy) {
			/* no security */
			LEAVE();
			return index;
		} else if (   adapter->secinfo.wep_enabled
			   && !adapter->secinfo.WPAenabled
			   && !adapter->secinfo.WPA2enabled
			   && adapter->scantable[index].privacy) {
			/* static WEP enabled */
			LEAVE();
			return index;
		} else if (   !adapter->secinfo.wep_enabled
			   && adapter->secinfo.WPAenabled
			   && !adapter->secinfo.WPA2enabled
			   && (adapter->scantable[index].wpa_ie[0] == WPA_IE)
			   /* privacy bit may NOT be set in some APs like LinkSys WRT54G
			      && adapter->scantable[index].privacy */
		    ) {
			/* WPA enabled */
			lbs_pr_debug(1,
			       "is_network_compatible() WPA: index=%d wpa_ie=%#x "
			       "wpa2_ie=%#x WEP=%s WPA=%s WPA2=%s "
			       "privacy=%#x\n", index,
			       adapter->scantable[index].wpa_ie[0],
			       adapter->scantable[index].rsn_ie[0],
			       adapter->secinfo.wep_enabled ? "e" : "d",
			       adapter->secinfo.WPAenabled ? "e" : "d",
			       adapter->secinfo.WPA2enabled ? "e" : "d",
			       adapter->scantable[index].privacy);
			LEAVE();
			return index;
		} else if (   !adapter->secinfo.wep_enabled
			   && !adapter->secinfo.WPAenabled
			   && adapter->secinfo.WPA2enabled
			   && (adapter->scantable[index].rsn_ie[0] == WPA2_IE)
			   /* privacy bit may NOT be set in some APs like LinkSys WRT54G
			      && adapter->scantable[index].privacy */
		    ) {
			/* WPA2 enabled */
			lbs_pr_debug(1,
			       "is_network_compatible() WPA2: index=%d wpa_ie=%#x "
			       "wpa2_ie=%#x WEP=%s WPA=%s WPA2=%s "
			       "privacy=%#x\n", index,
			       adapter->scantable[index].wpa_ie[0],
			       adapter->scantable[index].rsn_ie[0],
			       adapter->secinfo.wep_enabled ? "e" : "d",
			       adapter->secinfo.WPAenabled ? "e" : "d",
			       adapter->secinfo.WPA2enabled ? "e" : "d",
			       adapter->scantable[index].privacy);
			LEAVE();
			return index;
		} else if (   !adapter->secinfo.wep_enabled
			   && !adapter->secinfo.WPAenabled
			   && !adapter->secinfo.WPA2enabled
			   && (adapter->scantable[index].wpa_ie[0] != WPA_IE)
			   && (adapter->scantable[index].rsn_ie[0] != WPA2_IE)
			   && adapter->scantable[index].privacy) {
			/* dynamic WEP enabled */
			lbs_pr_debug(1,
			       "is_network_compatible() dynamic WEP: index=%d "
			       "wpa_ie=%#x wpa2_ie=%#x privacy=%#x\n",
			       index,
			       adapter->scantable[index].wpa_ie[0],
			       adapter->scantable[index].rsn_ie[0],
			       adapter->scantable[index].privacy);
			LEAVE();
			return index;
		}

		/* security doesn't match */
		lbs_pr_debug(1,
		       "is_network_compatible() FAILED: index=%d wpa_ie=%#x "
		       "wpa2_ie=%#x WEP=%s WPA=%s WPA2=%s privacy=%#x\n",
		       index,
		       adapter->scantable[index].wpa_ie[0],
		       adapter->scantable[index].rsn_ie[0],
		       adapter->secinfo.wep_enabled ? "e" : "d",
		       adapter->secinfo.WPAenabled ? "e" : "d",
		       adapter->secinfo.WPA2enabled ? "e" : "d",
		       adapter->scantable[index].privacy);
		LEAVE();
		return -ECONNREFUSED;
	}

	/* mode doesn't match */
	LEAVE();
	return -ENETUNREACH;
}

/**
 *  @brief This function validates a SSID as being able to be printed
 *
 *  @param pssid   SSID structure to validate
 *
 *  @return        TRUE or FALSE
 */
static u8 ssid_valid(struct WLAN_802_11_SSID *pssid)
{
	int ssididx;

	for (ssididx = 0; ssididx < pssid->ssidlength; ssididx++) {
		if (!isprint(pssid->ssid[ssididx])) {
			return 0;
		}
	}

	return 1;
}

/**
 *  @brief Post process the scan table after a new scan command has completed
 *
 *  Inspect each entry of the scan table and try to find an entry that
 *    matches our current associated/joined network from the scan.  If
 *    one is found, update the stored copy of the bssdescriptor for our
 *    current network.
 *
 *  Debug dump the current scan table contents if compiled accordingly.
 *
 *  @param priv   A pointer to wlan_private structure
 *
 *  @return       void
 */
static void wlan_scan_process_results(wlan_private * priv)
{
	wlan_adapter *adapter = priv->adapter;
	int foundcurrent;
	int i;

	foundcurrent = 0;

	if (adapter->connect_status == libertas_connected) {
		/* try to find the current BSSID in the new scan list */
		for (i = 0; i < adapter->numinscantable; i++) {
			if (!libertas_SSID_cmp(&adapter->scantable[i].ssid,
				     &adapter->curbssparams.ssid) &&
			    !memcmp(adapter->curbssparams.bssid,
				    adapter->scantable[i].macaddress,
				    ETH_ALEN)) {
				foundcurrent = 1;
			}
		}

		if (foundcurrent) {
			/* Make a copy of current BSSID descriptor */
			memcpy(&adapter->curbssparams.bssdescriptor,
			       &adapter->scantable[i],
			       sizeof(adapter->curbssparams.bssdescriptor));
		}
	}

	for (i = 0; i < adapter->numinscantable; i++) {
		lbs_pr_debug(1, "Scan:(%02d) %02x:%02x:%02x:%02x:%02x:%02x, "
		       "RSSI[%03d], SSID[%s]\n",
		       i,
		       adapter->scantable[i].macaddress[0],
		       adapter->scantable[i].macaddress[1],
		       adapter->scantable[i].macaddress[2],
		       adapter->scantable[i].macaddress[3],
		       adapter->scantable[i].macaddress[4],
		       adapter->scantable[i].macaddress[5],
		       (s32) adapter->scantable[i].rssi,
		       adapter->scantable[i].ssid.ssid);
	}
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
				    cpu_to_le16
				    (MRVDRV_PASSIVE_SCAN_CHAN_TIME);
				scanchanlist[chanidx].chanscanmode.passivescan =
				    1;
			} else {
				scanchanlist[chanidx].maxscantime =
				    cpu_to_le16
				    (MRVDRV_ACTIVE_SCAN_CHAN_TIME);
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
	const u8 zeromac[ETH_ALEN] = { 0, 0, 0, 0, 0, 0 };
	struct mrvlietypes_numprobes *pnumprobestlv;
	struct mrvlietypes_ssidparamset *pssidtlv;
	struct wlan_scan_cmd_config * pscancfgout = NULL;
	u8 *ptlvpos;
	u16 numprobes;
	u16 ssidlen;
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
		memcpy(pscancfgout->specificBSSID,
		       puserscanin->specificBSSID,
		       sizeof(pscancfgout->specificBSSID));

		ssidlen = strlen(puserscanin->specificSSID);

		if (ssidlen) {
			pssidtlv =
			    (struct mrvlietypes_ssidparamset *) pscancfgout->
			    tlvbuffer;
			pssidtlv->header.type = cpu_to_le16(TLV_TYPE_SSID);
			pssidtlv->header.len = cpu_to_le16(ssidlen);
			memcpy(pssidtlv->ssid, puserscanin->specificSSID,
			       ssidlen);
			ptlvpos += sizeof(pssidtlv->header) + ssidlen;
		}

		/*
		 *  The default number of channels sent in the command is low to
		 *    ensure the response buffer from the firmware does not truncate
		 *    scan results.  That is not an issue with an SSID or BSSID
		 *    filter applied to the scan results in the firmware.
		 */
		if (ssidlen || (memcmp(pscancfgout->specificBSSID,
				       &zeromac, sizeof(zeromac)) != 0)) {
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
		pnumprobestlv->header.type =
		    cpu_to_le16(TLV_TYPE_NUMPROBES);
		pnumprobestlv->header.len = sizeof(pnumprobestlv->numprobes);
		pnumprobestlv->numprobes = cpu_to_le16(numprobes);

		ptlvpos +=
		    sizeof(pnumprobestlv->header) + pnumprobestlv->header.len;

		pnumprobestlv->header.len =
		    cpu_to_le16(pnumprobestlv->header.len);
	}

	/*
	 * Set the output for the channel TLV to the address in the tlv buffer
	 *   past any TLVs that were added in this fuction (SSID, numprobes).
	 *   channel TLVs will be added past this for each scan command, preserving
	 *   the TLVs that were previously added.
	 */
	*ppchantlvout = (struct mrvlietypes_chanlistparamset *) ptlvpos;

	if (puserscanin && puserscanin->chanlist[0].channumber) {

		lbs_pr_debug(1, "Scan: Using supplied channel list\n");

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
			lbs_pr_debug(1, "Scan: Scanning current channel only");
		}

	} else {
		lbs_pr_debug(1, "Scan: Creating full region channel list\n");
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
				  struct chanscanparamset * pscanchanlist)
{
	struct chanscanparamset *ptmpchan;
	struct chanscanparamset *pstartchan;
	u8 scanband;
	int doneearly;
	int tlvidx;
	int ret = 0;

	ENTER();

	if (pscancfgout == 0 || pchantlvout == 0 || pscanchanlist == 0) {
		lbs_pr_debug(1, "Scan: Null detect: %p, %p, %p\n",
		       pscancfgout, pchantlvout, pscanchanlist);
		return -1;
	}

	pchantlvout->header.type = cpu_to_le16(TLV_TYPE_CHANLIST);

	/* Set the temp channel struct pointer to the start of the desired list */
	ptmpchan = pscanchanlist;

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
		       && !doneearly) {

            lbs_pr_debug(1,
                    "Scan: Chan(%3d), Radio(%d), mode(%d,%d), Dur(%d)\n",
                ptmpchan->channumber, ptmpchan->radiotype,
                ptmpchan->chanscanmode.passivescan,
                ptmpchan->chanscanmode.disablechanfilt,
                ptmpchan->maxscantime);

			/* Copy the current channel TLV to the command being prepared */
			memcpy(pchantlvout->chanscanparam + tlvidx,
			       ptmpchan, sizeof(pchantlvout->chanscanparam));

			/* Increment the TLV header length by the size appended */
			pchantlvout->header.len +=
			    sizeof(pchantlvout->chanscanparam);

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
			     + pchantlvout->header.len);

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
	}

	LEAVE();
	return ret;
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
			      const struct wlan_ioctl_user_scan_cfg * puserscanin)
{
	wlan_adapter *adapter = priv->adapter;
	struct mrvlietypes_chanlistparamset *pchantlvout;
	struct chanscanparamset * scan_chan_list = NULL;
	struct wlan_scan_cmd_config * scan_cfg = NULL;
	u8 keeppreviousscan;
	u8 filteredscan;
	u8 scancurrentchanonly;
	int maxchanperscan;
	int ret;

	ENTER();

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

	keeppreviousscan = 0;

	if (puserscanin) {
		keeppreviousscan = puserscanin->keeppreviousscan;
	}

	if (!keeppreviousscan) {
		memset(adapter->scantable, 0x00,
		       sizeof(struct bss_descriptor) * MRVDRV_MAX_BSSID_LIST);
		adapter->numinscantable = 0;
	}

	/* Keep the data path active if we are only scanning our current channel */
	if (!scancurrentchanonly) {
		netif_stop_queue(priv->wlan_dev.netdev);
		netif_carrier_off(priv->wlan_dev.netdev);
	}

	ret = wlan_scan_channel_list(priv,
				     maxchanperscan,
				     filteredscan,
				     scan_cfg,
				     pchantlvout,
				     scan_chan_list);

	/*  Process the resulting scan table:
	 *    - Remove any bad ssids
	 *    - Update our current BSS information from scan data
	 */
	wlan_scan_process_results(priv);

	if (priv->adapter->connect_status == libertas_connected) {
		netif_carrier_on(priv->wlan_dev.netdev);
		netif_wake_queue(priv->wlan_dev.netdev);
	}

out:
	if (scan_cfg)
		kfree(scan_cfg);

	if (scan_chan_list)
		kfree(scan_chan_list);

	LEAVE();
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

	lbs_pr_debug(1, "SCAN_RESP: tlvbufsize = %d\n", tlvbufsize);
	lbs_dbg_hex("SCAN_RESP: TLV Buf", (u8 *) ptlv, tlvbufsize);

	while (tlvbufleft >= sizeof(struct mrvlietypesheader)) {
		tlvtype = le16_to_cpu(pcurrenttlv->header.type);
		tlvlen = le16_to_cpu(pcurrenttlv->header.len);

		switch (tlvtype) {
		case TLV_TYPE_TSFTIMESTAMP:
			*ptsftlv = (struct mrvlietypes_tsftimestamp *) pcurrenttlv;
			break;

		default:
			lbs_pr_debug(1, "SCAN_RESP: Unhandled TLV = %d\n",
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
 *  @param pBSSIDEntry  Output parameter: Pointer to the BSS Entry
 *
 *  @return             0 or -1
 */
static int InterpretBSSDescriptionWithIE(struct bss_descriptor * pBSSEntry,
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

	struct IE_WPA *pIe;
	const u8 oui01[4] = { 0x00, 0x50, 0xf2, 0x01 };

	struct ieeetypes_countryinfoset *pcountryinfo;

	ENTER();

	founddatarateie = 0;
	ratesize = 0;
	beaconsize = 0;

	if (*bytesleft >= sizeof(beaconsize)) {
		/* Extract & convert beacon size from the command buffer */
		memcpy(&beaconsize, *pbeaconinfo, sizeof(beaconsize));
		beaconsize = le16_to_cpu(beaconsize);
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

	memcpy(pBSSEntry->macaddress, pcurrentptr, ETH_ALEN);
	lbs_pr_debug(1, "InterpretIE: AP MAC Addr-%x:%x:%x:%x:%x:%x\n",
	       pBSSEntry->macaddress[0], pBSSEntry->macaddress[1],
	       pBSSEntry->macaddress[2], pBSSEntry->macaddress[3],
	       pBSSEntry->macaddress[4], pBSSEntry->macaddress[5]);

	pcurrentptr += ETH_ALEN;
	bytesleftforcurrentbeacon -= ETH_ALEN;

	if (bytesleftforcurrentbeacon < 12) {
		lbs_pr_debug(1, "InterpretIE: Not enough bytes left\n");
		return -1;
	}

	/*
	 * next 4 fields are RSSI, time stamp, beacon interval,
	 *   and capability information
	 */

	/* RSSI is 1 byte long */
	pBSSEntry->rssi = le32_to_cpu((long)(*pcurrentptr));
	lbs_pr_debug(1, "InterpretIE: RSSI=%02X\n", *pcurrentptr);
	pcurrentptr += 1;
	bytesleftforcurrentbeacon -= 1;

	/* time stamp is 8 bytes long */
	memcpy(fixedie.timestamp, pcurrentptr, 8);
	memcpy(pBSSEntry->timestamp, pcurrentptr, 8);
	pcurrentptr += 8;
	bytesleftforcurrentbeacon -= 8;

	/* beacon interval is 2 bytes long */
	memcpy(&fixedie.beaconinterval, pcurrentptr, 2);
	pBSSEntry->beaconperiod = le16_to_cpu(fixedie.beaconinterval);
	pcurrentptr += 2;
	bytesleftforcurrentbeacon -= 2;

	/* capability information is 2 bytes long */
	memcpy(&fixedie.capabilities, pcurrentptr, 2);
	lbs_pr_debug(1, "InterpretIE: fixedie.capabilities=0x%X\n",
	       fixedie.capabilities);
	fixedie.capabilities = le16_to_cpu(fixedie.capabilities);
	pcap = (struct ieeetypes_capinfo *) & fixedie.capabilities;
	memcpy(&pBSSEntry->cap, pcap, sizeof(struct ieeetypes_capinfo));
	pcurrentptr += 2;
	bytesleftforcurrentbeacon -= 2;

	/* rest of the current buffer are IE's */
	lbs_pr_debug(1, "InterpretIE: IElength for this AP = %d\n",
	       bytesleftforcurrentbeacon);

	lbs_dbg_hex("InterpretIE: IE info", (u8 *) pcurrentptr,
		bytesleftforcurrentbeacon);

	if (pcap->privacy) {
		lbs_pr_debug(1, "InterpretIE: AP WEP enabled\n");
		pBSSEntry->privacy = wlan802_11privfilter8021xWEP;
	} else {
		pBSSEntry->privacy = wlan802_11privfilteracceptall;
	}

	if (pcap->ibss == 1) {
		pBSSEntry->mode = IW_MODE_ADHOC;
	} else {
		pBSSEntry->mode = IW_MODE_INFRA;
	}

	/* process variable IE */
	while (bytesleftforcurrentbeacon >= 2) {
		elemID = (enum ieeetypes_elementid) (*((u8 *) pcurrentptr));
		elemlen = *((u8 *) pcurrentptr + 1);

		if (bytesleftforcurrentbeacon < elemlen) {
			lbs_pr_debug(1, "InterpretIE: error in processing IE, "
			       "bytes left < IE length\n");
			bytesleftforcurrentbeacon = 0;
			continue;
		}

		switch (elemID) {

		case SSID:
			pBSSEntry->ssid.ssidlength = elemlen;
			memcpy(pBSSEntry->ssid.ssid, (pcurrentptr + 2),
			       elemlen);
			lbs_pr_debug(1, "ssid: %32s", pBSSEntry->ssid.ssid);
			break;

		case SUPPORTED_RATES:
			memcpy(pBSSEntry->datarates, (pcurrentptr + 2),
			       elemlen);
			memmove(pBSSEntry->libertas_supported_rates, (pcurrentptr + 2),
				elemlen);
			ratesize = elemlen;
			founddatarateie = 1;
			break;

		case EXTRA_IE:
			lbs_pr_debug(1, "InterpretIE: EXTRA_IE Found!\n");
			pBSSEntry->extra_ie = 1;
			break;

		case FH_PARAM_SET:
			pFH = (struct ieeetypes_fhparamset *) pcurrentptr;
			memmove(&pBSSEntry->phyparamset.fhparamset, pFH,
				sizeof(struct ieeetypes_fhparamset));
			pBSSEntry->phyparamset.fhparamset.dwelltime
			    =
			    le16_to_cpu(pBSSEntry->phyparamset.fhparamset.
					     dwelltime);
			break;

		case DS_PARAM_SET:
			pDS = (struct ieeetypes_dsparamset *) pcurrentptr;

			pBSSEntry->channel = pDS->currentchan;

			memcpy(&pBSSEntry->phyparamset.dsparamset, pDS,
			       sizeof(struct ieeetypes_dsparamset));
			break;

		case CF_PARAM_SET:
			pCF = (struct ieeetypes_cfparamset *) pcurrentptr;

			memcpy(&pBSSEntry->ssparamset.cfparamset, pCF,
			       sizeof(struct ieeetypes_cfparamset));
			break;

		case IBSS_PARAM_SET:
			pibss = (struct ieeetypes_ibssparamset *) pcurrentptr;
			pBSSEntry->atimwindow =
			    le32_to_cpu(pibss->atimwindow);

			memmove(&pBSSEntry->ssparamset.ibssparamset, pibss,
				sizeof(struct ieeetypes_ibssparamset));

			pBSSEntry->ssparamset.ibssparamset.atimwindow
			    =
			    le16_to_cpu(pBSSEntry->ssparamset.ibssparamset.
					     atimwindow);
			break;

			/* Handle Country Info IE */
		case COUNTRY_INFO:
			pcountryinfo =
			    (struct ieeetypes_countryinfoset *) pcurrentptr;

			if (pcountryinfo->len <
			    sizeof(pcountryinfo->countrycode)
			    || pcountryinfo->len > 254) {
				lbs_pr_debug(1, "InterpretIE: 11D- Err "
				       "CountryInfo len =%d min=%zd max=254\n",
				       pcountryinfo->len,
				       sizeof(pcountryinfo->countrycode));
				LEAVE();
				return -1;
			}

			memcpy(&pBSSEntry->countryinfo,
			       pcountryinfo, pcountryinfo->len + 2);
			lbs_dbg_hex("InterpretIE: 11D- CountryInfo:",
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

				pRate = (u8 *) pBSSEntry->datarates;
				pRate += ratesize;
				memmove(pRate, (pcurrentptr + 2), bytestocopy);

				pRate = (u8 *) pBSSEntry->libertas_supported_rates;

				pRate += ratesize;
				memmove(pRate, (pcurrentptr + 2), bytestocopy);
			}
			break;

		case VENDOR_SPECIFIC_221:
#define IE_ID_LEN_FIELDS_BYTES 2
			pIe = (struct IE_WPA *)pcurrentptr;

			if (memcmp(pIe->oui, oui01, sizeof(oui01)))
				break;

			pBSSEntry->wpa_ie_len = min_t(size_t,
				elemlen + IE_ID_LEN_FIELDS_BYTES,
				sizeof(pBSSEntry->wpa_ie));
			memcpy(pBSSEntry->wpa_ie, pcurrentptr,
				pBSSEntry->wpa_ie_len);
			lbs_dbg_hex("InterpretIE: Resp WPA_IE",
				pBSSEntry->wpa_ie, elemlen);
			break;
		case WPA2_IE:
			pIe = (struct IE_WPA *)pcurrentptr;

			pBSSEntry->rsn_ie_len = min_t(size_t,
				elemlen + IE_ID_LEN_FIELDS_BYTES,
				sizeof(pBSSEntry->rsn_ie));
			memcpy(pBSSEntry->rsn_ie, pcurrentptr,
				pBSSEntry->rsn_ie_len);
			lbs_dbg_hex("InterpretIE: Resp WPA2_IE",
				pBSSEntry->rsn_ie, elemlen);
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

	return 0;
}

/**
 *  @brief Compare two SSIDs
 *
 *  @param ssid1    A pointer to ssid to compare
 *  @param ssid2    A pointer to ssid to compare
 *
 *  @return         0--ssid is same, otherwise is different
 */
int libertas_SSID_cmp(struct WLAN_802_11_SSID *ssid1, struct WLAN_802_11_SSID *ssid2)
{
	if (!ssid1 || !ssid2)
		return -1;

	if (ssid1->ssidlength != ssid2->ssidlength)
		return -1;

	return memcmp(ssid1->ssid, ssid2->ssid, ssid1->ssidlength);
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
int libertas_find_BSSID_in_list(wlan_adapter * adapter, u8 * bssid, u8 mode)
{
	int ret = -ENETUNREACH;
	int i;

	if (!bssid)
		return -EFAULT;

	lbs_pr_debug(1, "FindBSSID: Num of BSSIDs = %d\n",
	       adapter->numinscantable);

	/* Look through the scan table for a compatible match. The ret return
	 *   variable will be equal to the index in the scan table (greater
	 *   than zero) if the network is compatible.  The loop will continue
	 *   past a matched bssid that is not compatible in case there is an
	 *   AP with multiple SSIDs assigned to the same BSSID
	 */
	for (i = 0; ret < 0 && i < adapter->numinscantable; i++) {
		if (!memcmp(adapter->scantable[i].macaddress, bssid, ETH_ALEN)) {
			switch (mode) {
			case IW_MODE_INFRA:
			case IW_MODE_ADHOC:
				ret = is_network_compatible(adapter, i, mode);
				break;
			default:
				ret = i;
				break;
			}
		}
	}

	return ret;
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
int libertas_find_SSID_in_list(wlan_adapter * adapter,
		   struct WLAN_802_11_SSID *ssid, u8 * bssid, u8 mode)
{
	int net = -ENETUNREACH;
	u8 bestrssi = 0;
	int i;
	int j;

	lbs_pr_debug(1, "Num of Entries in Table = %d\n", adapter->numinscantable);

	for (i = 0; i < adapter->numinscantable; i++) {
		if (!libertas_SSID_cmp(&adapter->scantable[i].ssid, ssid) &&
		    (!bssid ||
		     !memcmp(adapter->scantable[i].
			     macaddress, bssid, ETH_ALEN))) {
			switch (mode) {
			case IW_MODE_INFRA:
			case IW_MODE_ADHOC:
				j = is_network_compatible(adapter, i, mode);

				if (j >= 0) {
					if (bssid) {
						return i;
					}

					if (SCAN_RSSI
					    (adapter->scantable[i].rssi)
					    > bestrssi) {
						bestrssi =
						    SCAN_RSSI(adapter->
							      scantable[i].
							      rssi);
						net = i;
					}
				} else {
					if (net == -ENETUNREACH) {
						net = j;
					}
				}
				break;
			case IW_MODE_AUTO:
			default:
				if (SCAN_RSSI(adapter->scantable[i].rssi)
				    > bestrssi) {
					bestrssi =
					    SCAN_RSSI(adapter->scantable[i].
						      rssi);
					net = i;
				}
				break;
			}
		}
	}

	return net;
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
int libertas_find_best_SSID_in_list(wlan_adapter * adapter, u8 mode)
{
	int bestnet = -ENETUNREACH;
	u8 bestrssi = 0;
	int i;

	ENTER();

	lbs_pr_debug(1, "Num of BSSIDs = %d\n", adapter->numinscantable);

	for (i = 0; i < adapter->numinscantable; i++) {
		switch (mode) {
		case IW_MODE_INFRA:
		case IW_MODE_ADHOC:
			if (is_network_compatible(adapter, i, mode) >= 0) {
				if (SCAN_RSSI(adapter->scantable[i].rssi) >
				    bestrssi) {
					bestrssi =
					    SCAN_RSSI(adapter->scantable[i].
						      rssi);
					bestnet = i;
				}
			}
			break;
		case IW_MODE_AUTO:
		default:
			if (SCAN_RSSI(adapter->scantable[i].rssi) > bestrssi) {
				bestrssi =
				    SCAN_RSSI(adapter->scantable[i].rssi);
				bestnet = i;
			}
			break;
		}
	}

	LEAVE();
	return bestnet;
}

/**
 *  @brief Find the AP with specific ssid in the scan list
 *
 *  @param priv         A pointer to wlan_private structure
 *  @param pSSID        A pointer to AP's ssid
 *
 *  @return             0--success, otherwise--fail
 */
int libertas_find_best_network_SSID(wlan_private * priv,
                                    struct WLAN_802_11_SSID *pSSID,
                                    u8 preferred_mode, u8 *out_mode)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;
	struct bss_descriptor *preqbssid;
	int i;

	ENTER();

	memset(pSSID, 0, sizeof(struct WLAN_802_11_SSID));

	wlan_scan_networks(priv, NULL);
	if (adapter->surpriseremoved)
		return -1;
	wait_event_interruptible(adapter->cmd_pending, !adapter->nr_cmd_pending);

	i = libertas_find_best_SSID_in_list(adapter, preferred_mode);
	if (i < 0) {
		ret = -1;
		goto out;
	}

	preqbssid = &adapter->scantable[i];
	memcpy(pSSID, &preqbssid->ssid,
	       sizeof(struct WLAN_802_11_SSID));
	*out_mode = preqbssid->mode;

	if (!pSSID->ssidlength) {
		ret = -1;
	}

out:
	LEAVE();
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
	union iwreq_data wrqu;

	ENTER();

	if (!wlan_scan_networks(priv, NULL)) {
		memset(&wrqu, 0, sizeof(union iwreq_data));
		wireless_send_event(priv->wlan_dev.netdev, SIOCGIWSCAN, &wrqu,
				    NULL);
	}

	if (adapter->surpriseremoved)
		return -1;

	LEAVE();
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
int libertas_send_specific_SSID_scan(wlan_private * priv,
			 struct WLAN_802_11_SSID *prequestedssid,
			 u8 keeppreviousscan)
{
	wlan_adapter *adapter = priv->adapter;
	struct wlan_ioctl_user_scan_cfg scancfg;

	ENTER();

	if (prequestedssid == NULL) {
		return -1;
	}

	memset(&scancfg, 0x00, sizeof(scancfg));

	memcpy(scancfg.specificSSID, prequestedssid->ssid,
	       prequestedssid->ssidlength);
	scancfg.keeppreviousscan = keeppreviousscan;

	wlan_scan_networks(priv, &scancfg);
	if (adapter->surpriseremoved)
		return -1;
	wait_event_interruptible(adapter->cmd_pending, !adapter->nr_cmd_pending);

	LEAVE();
	return 0;
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
int libertas_send_specific_BSSID_scan(wlan_private * priv, u8 * bssid, u8 keeppreviousscan)
{
	struct wlan_ioctl_user_scan_cfg scancfg;

	ENTER();

	if (bssid == NULL) {
		return -1;
	}

	memset(&scancfg, 0x00, sizeof(scancfg));
	memcpy(scancfg.specificBSSID, bssid, sizeof(scancfg.specificBSSID));
	scancfg.keeppreviousscan = keeppreviousscan;

	wlan_scan_networks(priv, &scancfg);
	if (priv->adapter->surpriseremoved)
		return -1;
	wait_event_interruptible(priv->adapter->cmd_pending,
		!priv->adapter->nr_cmd_pending);

	LEAVE();
	return 0;
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
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;
	char *current_ev = extra;
	char *end_buf = extra + IW_SCAN_MAX_DATA;
	struct chan_freq_power *cfp;
	struct bss_descriptor *pscantable;
	char *current_val;	/* For rates */
	struct iw_event iwe;	/* Temporary buffer */
	int i;
	int j;
	int rate;
#define PERFECT_RSSI ((u8)50)
#define WORST_RSSI   ((u8)0)
#define RSSI_DIFF    ((u8)(PERFECT_RSSI - WORST_RSSI))
	u8 rssi;

	u8 buf[16 + 256 * 2];
	u8 *ptr;

	ENTER();

	/*
	 * if there's either commands in the queue or one being
	 * processed return -EAGAIN for iwlist to retry later.
	 */
    if (adapter->nr_cmd_pending)
		return -EAGAIN;

	if (adapter->connect_status == libertas_connected)
		lbs_pr_debug(1, "Current ssid: %32s\n",
		       adapter->curbssparams.ssid.ssid);

	lbs_pr_debug(1, "Scan: Get: numinscantable = %d\n",
	       adapter->numinscantable);

	/* The old API using SIOCGIWAPLIST had a hard limit of IW_MAX_AP.
	 * The new API using SIOCGIWSCAN is only limited by buffer size
	 * WE-14 -> WE-16 the buffer is limited to IW_SCAN_MAX_DATA bytes
	 * which is 4096.
	 */
	for (i = 0; i < adapter->numinscantable; i++) {
		if ((current_ev + MAX_SCAN_CELL_SIZE) >= end_buf) {
			lbs_pr_debug(1, "i=%d break out: current_ev=%p end_buf=%p "
			       "MAX_SCAN_CELL_SIZE=%zd\n",
			       i, current_ev, end_buf, MAX_SCAN_CELL_SIZE);
			break;
		}

		pscantable = &adapter->scantable[i];

		lbs_pr_debug(1, "i=%d  ssid: %32s\n", i, pscantable->ssid.ssid);

		cfp =
		    libertas_find_cfp_by_band_and_channel(adapter, 0,
						 pscantable->channel);
		if (!cfp) {
			lbs_pr_debug(1, "Invalid channel number %d\n",
			       pscantable->channel);
			continue;
		}

		if (!ssid_valid(&adapter->scantable[i].ssid)) {
			continue;
		}

		/* First entry *MUST* be the AP MAC address */
		iwe.cmd = SIOCGIWAP;
		iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
		memcpy(iwe.u.ap_addr.sa_data,
		       &adapter->scantable[i].macaddress, ETH_ALEN);

		iwe.len = IW_EV_ADDR_LEN;
		current_ev =
		    iwe_stream_add_event(current_ev, end_buf, &iwe, iwe.len);

		//Add the ESSID
		iwe.u.data.length = adapter->scantable[i].ssid.ssidlength;

		if (iwe.u.data.length > 32) {
			iwe.u.data.length = 32;
		}

		iwe.cmd = SIOCGIWESSID;
		iwe.u.data.flags = 1;
		iwe.len = IW_EV_POINT_LEN + iwe.u.data.length;
		current_ev = iwe_stream_add_point(current_ev, end_buf, &iwe,
						  adapter->scantable[i].ssid.
						  ssid);

		//Add mode
		iwe.cmd = SIOCGIWMODE;
		iwe.u.mode = adapter->scantable[i].mode;
		iwe.len = IW_EV_UINT_LEN;
		current_ev =
		    iwe_stream_add_event(current_ev, end_buf, &iwe, iwe.len);

		//frequency
		iwe.cmd = SIOCGIWFREQ;
		iwe.u.freq.m = (long)cfp->freq * 100000;
		iwe.u.freq.e = 1;
		iwe.len = IW_EV_FREQ_LEN;
		current_ev =
		    iwe_stream_add_event(current_ev, end_buf, &iwe, iwe.len);

		/* Add quality statistics */
		iwe.cmd = IWEVQUAL;
		iwe.u.qual.updated = IW_QUAL_ALL_UPDATED;
		iwe.u.qual.level = SCAN_RSSI(adapter->scantable[i].rssi);

		rssi = iwe.u.qual.level - MRVDRV_NF_DEFAULT_SCAN_VALUE;
		iwe.u.qual.qual =
		    (100 * RSSI_DIFF * RSSI_DIFF - (PERFECT_RSSI - rssi) *
		     (15 * (RSSI_DIFF) + 62 * (PERFECT_RSSI - rssi))) /
		    (RSSI_DIFF * RSSI_DIFF);
		if (iwe.u.qual.qual > 100)
			iwe.u.qual.qual = 100;
		else if (iwe.u.qual.qual < 1)
			iwe.u.qual.qual = 0;

		if (adapter->NF[TYPE_BEACON][TYPE_NOAVG] == 0) {
			iwe.u.qual.noise = MRVDRV_NF_DEFAULT_SCAN_VALUE;
		} else {
			iwe.u.qual.noise =
			    CAL_NF(adapter->NF[TYPE_BEACON][TYPE_NOAVG]);
		}
		if ((adapter->mode == IW_MODE_ADHOC) &&
		    !libertas_SSID_cmp(&adapter->curbssparams.ssid,
			     &adapter->scantable[i].ssid)
		    && adapter->adhoccreate) {
			ret = libertas_prepare_and_send_command(priv,
						    cmd_802_11_rssi,
						    0,
						    cmd_option_waitforrsp,
						    0, NULL);

			if (!ret) {
				iwe.u.qual.level =
				    CAL_RSSI(adapter->SNR[TYPE_RXPD][TYPE_AVG] /
					     AVG_SCALE,
					     adapter->NF[TYPE_RXPD][TYPE_AVG] /
					     AVG_SCALE);
			}
		}
		iwe.len = IW_EV_QUAL_LEN;
		current_ev =
		    iwe_stream_add_event(current_ev, end_buf, &iwe, iwe.len);

		/* Add encryption capability */
		iwe.cmd = SIOCGIWENCODE;
		if (adapter->scantable[i].privacy) {
			iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
		} else {
			iwe.u.data.flags = IW_ENCODE_DISABLED;
		}
		iwe.u.data.length = 0;
		iwe.len = IW_EV_POINT_LEN + iwe.u.data.length;
		current_ev = iwe_stream_add_point(current_ev, end_buf, &iwe,
						  adapter->scantable->ssid.
						  ssid);

		current_val = current_ev + IW_EV_LCP_LEN;

		iwe.cmd = SIOCGIWRATE;

		iwe.u.bitrate.fixed = 0;
		iwe.u.bitrate.disabled = 0;
		iwe.u.bitrate.value = 0;

		/* Bit rate given in 500 kb/s units (+ 0x80) */
		for (j = 0; j < sizeof(adapter->scantable[i].libertas_supported_rates);
		     j++) {
			if (adapter->scantable[i].libertas_supported_rates[j] == 0) {
				break;
			}
			rate =
			    (adapter->scantable[i].libertas_supported_rates[j] & 0x7F) *
			    500000;
			if (rate > iwe.u.bitrate.value) {
				iwe.u.bitrate.value = rate;
			}

			iwe.u.bitrate.value =
			    (adapter->scantable[i].libertas_supported_rates[j]
			     & 0x7f) * 500000;
			iwe.len = IW_EV_PARAM_LEN;
			current_ev =
			    iwe_stream_add_value(current_ev, current_val,
						 end_buf, &iwe, iwe.len);

		}
		if ((adapter->scantable[i].mode == IW_MODE_ADHOC)
		    && !libertas_SSID_cmp(&adapter->curbssparams.ssid,
				&adapter->scantable[i].ssid)
		    && adapter->adhoccreate) {
			iwe.u.bitrate.value = 22 * 500000;
		}
		iwe.len = IW_EV_PARAM_LEN;
		current_ev =
		    iwe_stream_add_value(current_ev, current_val, end_buf, &iwe,
					 iwe.len);

		/* Add new value to event */
		current_val = current_ev + IW_EV_LCP_LEN;

		if (adapter->scantable[i].rsn_ie[0] == WPA2_IE) {
			memset(&iwe, 0, sizeof(iwe));
			memset(buf, 0, sizeof(buf));
			memcpy(buf, adapter->scantable[i].rsn_ie,
					adapter->scantable[i].rsn_ie_len);
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = adapter->scantable[i].rsn_ie_len;
			iwe.len = IW_EV_POINT_LEN + iwe.u.data.length;
			current_ev = iwe_stream_add_point(current_ev, end_buf,
					&iwe, buf);
		}
		if (adapter->scantable[i].wpa_ie[0] == WPA_IE) {
			memset(&iwe, 0, sizeof(iwe));
			memset(buf, 0, sizeof(buf));
			memcpy(buf, adapter->scantable[i].wpa_ie,
					adapter->scantable[i].wpa_ie_len);
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = adapter->scantable[i].wpa_ie_len;
			iwe.len = IW_EV_POINT_LEN + iwe.u.data.length;
			current_ev = iwe_stream_add_point(current_ev, end_buf,
					&iwe, buf);
		}


		if (adapter->scantable[i].extra_ie != 0) {
			memset(&iwe, 0, sizeof(iwe));
			memset(buf, 0, sizeof(buf));
			ptr = buf;
			ptr += sprintf(ptr, "extra_ie");
			iwe.u.data.length = strlen(buf);

			lbs_pr_debug(1, "iwe.u.data.length %d\n",
			       iwe.u.data.length);
			lbs_pr_debug(1, "BUF: %s \n", buf);

			iwe.cmd = IWEVCUSTOM;
			iwe.len = IW_EV_POINT_LEN + iwe.u.data.length;
			current_ev =
			    iwe_stream_add_point(current_ev, end_buf, &iwe,
						 buf);
		}

		current_val = current_ev + IW_EV_LCP_LEN;

		/*
		 * Check if we added any event
		 */
		if ((current_val - current_ev) > IW_EV_LCP_LEN)
			current_ev = current_val;
	}

	dwrq->length = (current_ev - extra);
	dwrq->flags = 0;

	LEAVE();
	return 0;
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

	ENTER();

	pscancfg = pdata_buf;

	/* Set fixed field variables in scan command */
	pscan->bsstype = pscancfg->bsstype;
	memcpy(pscan->BSSID, pscancfg->specificBSSID, sizeof(pscan->BSSID));
	memcpy(pscan->tlvbuffer, pscancfg->tlvbuffer, pscancfg->tlvbufferlen);

	cmd->command = cpu_to_le16(cmd_802_11_scan);

	/* size is equal to the sizeof(fixed portions) + the TLV len + header */
	cmd->size = cpu_to_le16(sizeof(pscan->bsstype)
				     + sizeof(pscan->BSSID)
				     + pscancfg->tlvbufferlen + S_DS_GEN);

	lbs_pr_debug(1, "SCAN_CMD: command=%x, size=%x, seqnum=%x\n",
	       cmd->command, cmd->size, cmd->seqnum);
	LEAVE();
	return 0;
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
	struct bss_descriptor newbssentry;
	struct mrvlietypes_data *ptlv;
	struct mrvlietypes_tsftimestamp *ptsftlv;
	u8 *pbssinfo;
	u16 scanrespsize;
	int bytesleft;
	int numintable;
	int bssIdx;
	int idx;
	int tlvbufsize;
	u64 tsfval;

	ENTER();

	pscan = &resp->params.scanresp;

	if (pscan->nr_sets > MRVDRV_MAX_BSSID_LIST) {
        lbs_pr_debug(1,
		       "SCAN_RESP: Invalid number of AP returned (%d)!!\n",
		       pscan->nr_sets);
		LEAVE();
		return -1;
	}

	bytesleft = le16_to_cpu(pscan->bssdescriptsize);
	lbs_pr_debug(1, "SCAN_RESP: bssdescriptsize %d\n", bytesleft);

	scanrespsize = le16_to_cpu(resp->size);
	lbs_pr_debug(1, "SCAN_RESP: returned %d AP before parsing\n",
	       pscan->nr_sets);

	numintable = adapter->numinscantable;
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
		/* Zero out the newbssentry we are about to store info in */
		memset(&newbssentry, 0x00, sizeof(newbssentry));

		/* Process the data fields and IEs returned for this BSS */
		if ((InterpretBSSDescriptionWithIE(&newbssentry,
						   &pbssinfo,
						   &bytesleft) ==
		     0)
		    && CHECK_SSID_IS_VALID(&newbssentry.ssid)) {

            lbs_pr_debug(1,
			       "SCAN_RESP: BSSID = %02x:%02x:%02x:%02x:%02x:%02x\n",
			       newbssentry.macaddress[0],
			       newbssentry.macaddress[1],
			       newbssentry.macaddress[2],
			       newbssentry.macaddress[3],
			       newbssentry.macaddress[4],
			       newbssentry.macaddress[5]);

			/*
			 * Search the scan table for the same bssid
			 */
			for (bssIdx = 0; bssIdx < numintable; bssIdx++) {
				if (memcmp(newbssentry.macaddress,
					   adapter->scantable[bssIdx].
					   macaddress,
					   sizeof(newbssentry.macaddress)) ==
				    0) {
					/*
					 * If the SSID matches as well, it is a duplicate of
					 *   this entry.  Keep the bssIdx set to this
					 *   entry so we replace the old contents in the table
					 */
					if ((newbssentry.ssid.ssidlength ==
					     adapter->scantable[bssIdx].ssid.
					     ssidlength)
					    &&
					    (memcmp
					     (newbssentry.ssid.ssid,
					      adapter->scantable[bssIdx].ssid.
					      ssid,
					      newbssentry.ssid.ssidlength) ==
					     0)) {
                        lbs_pr_debug(1,
						       "SCAN_RESP: Duplicate of index: %d\n",
						       bssIdx);
						break;
					}
				}
			}
			/*
			 * If the bssIdx is equal to the number of entries in the table,
			 *   the new entry was not a duplicate; append it to the scan
			 *   table
			 */
			if (bssIdx == numintable) {
				/* Range check the bssIdx, keep it limited to the last entry */
				if (bssIdx == MRVDRV_MAX_BSSID_LIST) {
					bssIdx--;
				} else {
					numintable++;
				}
			}

			/*
			 * If the TSF TLV was appended to the scan results, save the
			 *   this entries TSF value in the networktsf field.  The
			 *   networktsf is the firmware's TSF value at the time the
			 *   beacon or probe response was received.
			 */
			if (ptsftlv) {
				memcpy(&tsfval, &ptsftlv->tsftable[idx],
				       sizeof(tsfval));
				tsfval = le64_to_cpu(tsfval);

				memcpy(&newbssentry.networktsf,
				       &tsfval, sizeof(newbssentry.networktsf));
			}

			/* Copy the locally created newbssentry to the scan table */
			memcpy(&adapter->scantable[bssIdx],
			       &newbssentry,
			       sizeof(adapter->scantable[bssIdx]));

		} else {

			/* error parsing/interpreting the scan response, skipped */
			lbs_pr_debug(1, "SCAN_RESP: "
			       "InterpretBSSDescriptionWithIE returned ERROR\n");
		}
	}

	lbs_pr_debug(1, "SCAN_RESP: Scanned %2d APs, %d valid, %d total\n",
	       pscan->nr_sets, numintable - adapter->numinscantable,
	       numintable);

	/* Update the total number of BSSIDs in the scan table */
	adapter->numinscantable = numintable;

	LEAVE();
	return 0;
}
