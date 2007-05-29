/**
  *  Functions implementing wlan infrastructure and adhoc join routines,
  *  IOCTL handlers as well as command preperation and response routines
  *  for sending adhoc start, adhoc join, and association commands
  *  to the firmware.
  */
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/etherdevice.h>

#include <net/iw_handler.h>

#include "host.h"
#include "decl.h"
#include "join.h"
#include "dev.h"
#include "assoc.h"

#define AD_HOC_CAP_PRIVACY_ON 1

/**
 *  @brief This function finds out the common rates between rate1 and rate2.
 *
 * It will fill common rates in rate1 as output if found.
 *
 * NOTE: Setting the MSB of the basic rates need to be taken
 *   care, either before or after calling this function
 *
 *  @param adapter     A pointer to wlan_adapter structure
 *  @param rate1       the buffer which keeps input and output
 *  @param rate1_size  the size of rate1 buffer
 *  @param rate2       the buffer which keeps rate2
 *  @param rate2_size  the size of rate2 buffer.
 *
 *  @return            0 or -1
 */
static int get_common_rates(wlan_adapter * adapter, u8 * rate1,
			    int rate1_size, u8 * rate2, int rate2_size)
{
	u8 *ptr = rate1;
	int ret = 0;
	u8 tmp[30];
	int i;

	memset(&tmp, 0, sizeof(tmp));
	memcpy(&tmp, rate1, min_t(size_t, rate1_size, sizeof(tmp)));
	memset(rate1, 0, rate1_size);

	/* Mask the top bit of the original values */
	for (i = 0; tmp[i] && i < sizeof(tmp); i++)
		tmp[i] &= 0x7F;

	for (i = 0; rate2[i] && i < rate2_size; i++) {
		/* Check for Card Rate in tmp, excluding the top bit */
		if (strchr(tmp, rate2[i] & 0x7F)) {
			/* values match, so copy the Card Rate to rate1 */
			*rate1++ = rate2[i];
		}
	}

	lbs_dbg_hex("rate1 (AP) rates:", tmp, sizeof(tmp));
	lbs_dbg_hex("rate2 (Card) rates:", rate2, rate2_size);
	lbs_dbg_hex("Common rates:", ptr, rate1_size);
	lbs_deb_join("Tx datarate is set to 0x%X\n", adapter->datarate);

	if (!adapter->is_datarate_auto) {
		while (*ptr) {
			if ((*ptr & 0x7f) == adapter->datarate) {
				ret = 0;
				goto done;
			}
			ptr++;
		}
		lbs_pr_alert( "Previously set fixed data rate %#x isn't "
		       "compatible with the network.\n", adapter->datarate);

		ret = -1;
		goto done;
	}

	ret = 0;
done:
	return ret;
}

int libertas_send_deauth(wlan_private * priv)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;

	if (adapter->mode == IW_MODE_INFRA &&
	    adapter->connect_status == libertas_connected)
		ret = libertas_send_deauthentication(priv);
	else
		ret = -ENOTSUPP;

	return ret;
}

/**
 *  @brief Associate to a specific BSS discovered in a scan
 *
 *  @param priv      A pointer to wlan_private structure
 *  @param pbssdesc  Pointer to the BSS descriptor to associate with.
 *
 *  @return          0-success, otherwise fail
 */
int wlan_associate(wlan_private * priv, struct assoc_request * assoc_req)
{
	wlan_adapter *adapter = priv->adapter;
	int ret;

	lbs_deb_enter(LBS_DEB_JOIN);

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_authenticate,
				    0, cmd_option_waitforrsp,
				    0, assoc_req->bss.bssid);

	if (ret)
		goto done;

	/* set preamble to firmware */
	if (adapter->capinfo.shortpreamble && assoc_req->bss.cap.shortpreamble)
		adapter->preamble = cmd_type_short_preamble;
	else
		adapter->preamble = cmd_type_long_preamble;

	libertas_set_radio_control(priv);

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_associate,
				    0, cmd_option_waitforrsp, 0, assoc_req);

done:
	lbs_deb_leave_args(LBS_DEB_JOIN, "ret %d", ret);
	return ret;
}

/**
 *  @brief Start an Adhoc Network
 *
 *  @param priv         A pointer to wlan_private structure
 *  @param adhocssid    The ssid of the Adhoc Network
 *  @return             0--success, -1--fail
 */
int libertas_start_adhoc_network(wlan_private * priv, struct assoc_request * assoc_req)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;

	adapter->adhoccreate = 1;

	if (!adapter->capinfo.shortpreamble) {
		lbs_deb_join("AdhocStart: Long preamble\n");
		adapter->preamble = cmd_type_long_preamble;
	} else {
		lbs_deb_join("AdhocStart: Short preamble\n");
		adapter->preamble = cmd_type_short_preamble;
	}

	libertas_set_radio_control(priv);

	lbs_deb_join("AdhocStart: channel = %d\n", assoc_req->channel);
	lbs_deb_join("AdhocStart: band = %d\n", assoc_req->band);

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_ad_hoc_start,
				    0, cmd_option_waitforrsp, 0, assoc_req);

	return ret;
}

/**
 *  @brief Join an adhoc network found in a previous scan
 *
 *  @param priv         A pointer to wlan_private structure
 *  @param pbssdesc     Pointer to a BSS descriptor found in a previous scan
 *                      to attempt to join
 *
 *  @return             0--success, -1--fail
 */
int libertas_join_adhoc_network(wlan_private * priv, struct assoc_request * assoc_req)
{
	wlan_adapter *adapter = priv->adapter;
	struct bss_descriptor * bss = &assoc_req->bss;
	int ret = 0;

	lbs_deb_join("%s: Current SSID '%s', ssid length %u\n",
	             __func__,
	             escape_essid(adapter->curbssparams.ssid,
	                          adapter->curbssparams.ssid_len),
	             adapter->curbssparams.ssid_len);
	lbs_deb_join("%s: requested ssid '%s', ssid length %u\n",
	             __func__, escape_essid(bss->ssid, bss->ssid_len),
	             bss->ssid_len);

	/* check if the requested SSID is already joined */
	if (adapter->curbssparams.ssid_len
	    && !libertas_ssid_cmp(adapter->curbssparams.ssid,
	                          adapter->curbssparams.ssid_len,
	                          bss->ssid, bss->ssid_len)
	    && (adapter->mode == IW_MODE_ADHOC)) {
		lbs_deb_join(
		       "ADHOC_J_CMD: New ad-hoc SSID is the same as current, "
		       "not attempting to re-join");
		return -1;
	}

	/*Use shortpreamble only when both creator and card supports
	   short preamble */
	if (!bss->cap.shortpreamble || !adapter->capinfo.shortpreamble) {
		lbs_deb_join("AdhocJoin: Long preamble\n");
		adapter->preamble = cmd_type_long_preamble;
	} else {
		lbs_deb_join("AdhocJoin: Short preamble\n");
		adapter->preamble = cmd_type_short_preamble;
	}

	libertas_set_radio_control(priv);

	lbs_deb_join("AdhocJoin: channel = %d\n", assoc_req->channel);
	lbs_deb_join("AdhocJoin: band = %c\n", assoc_req->band);

	adapter->adhoccreate = 0;

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_ad_hoc_join,
				    0, cmd_option_waitforrsp,
				    OID_802_11_SSID, assoc_req);

	return ret;
}

int libertas_stop_adhoc_network(wlan_private * priv)
{
	return libertas_prepare_and_send_command(priv, cmd_802_11_ad_hoc_stop,
				     0, cmd_option_waitforrsp, 0, NULL);
}

/**
 *  @brief Send Deauthentication Request
 *
 *  @param priv      A pointer to wlan_private structure
 *  @return          0--success, -1--fail
 */
int libertas_send_deauthentication(wlan_private * priv)
{
	return libertas_prepare_and_send_command(priv, cmd_802_11_deauthenticate,
				     0, cmd_option_waitforrsp, 0, NULL);
}

/**
 *  @brief This function prepares command of authenticate.
 *
 *  @param priv      A pointer to wlan_private structure
 *  @param cmd       A pointer to cmd_ds_command structure
 *  @param pdata_buf Void cast of pointer to a BSSID to authenticate with
 *
 *  @return         0 or -1
 */
int libertas_cmd_80211_authenticate(wlan_private * priv,
				 struct cmd_ds_command *cmd,
				 void *pdata_buf)
{
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ds_802_11_authenticate *pauthenticate = &cmd->params.auth;
	int ret = -1;
	u8 *bssid = pdata_buf;

	lbs_deb_enter(LBS_DEB_JOIN);

	cmd->command = cpu_to_le16(cmd_802_11_authenticate);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_authenticate)
	                        + S_DS_GEN);

	/* translate auth mode to 802.11 defined wire value */
	switch (adapter->secinfo.auth_mode) {
	case IW_AUTH_ALG_OPEN_SYSTEM:
		pauthenticate->authtype = 0x00;
		break;
	case IW_AUTH_ALG_SHARED_KEY:
		pauthenticate->authtype = 0x01;
		break;
	case IW_AUTH_ALG_LEAP:
		pauthenticate->authtype = 0x80;
		break;
	default:
		lbs_deb_join("AUTH_CMD: invalid auth alg 0x%X\n",
		             adapter->secinfo.auth_mode);
		goto out;
	}

	memcpy(pauthenticate->macaddr, bssid, ETH_ALEN);

	lbs_deb_join("AUTH_CMD: BSSID is : " MAC_FMT " auth=0x%X\n",
	             MAC_ARG(bssid), pauthenticate->authtype);
	ret = 0;

out:
	lbs_deb_leave_args(LBS_DEB_JOIN, "ret %d", ret);
	return ret;
}

int libertas_cmd_80211_deauthenticate(wlan_private * priv,
				   struct cmd_ds_command *cmd)
{
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ds_802_11_deauthenticate *dauth = &cmd->params.deauth;

	lbs_deb_enter(LBS_DEB_JOIN);

	cmd->command = cpu_to_le16(cmd_802_11_deauthenticate);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_deauthenticate) +
			     S_DS_GEN);

	/* set AP MAC address */
	memmove(dauth->macaddr, adapter->curbssparams.bssid, ETH_ALEN);

	/* Reason code 3 = Station is leaving */
#define REASON_CODE_STA_LEAVING 3
	dauth->reasoncode = cpu_to_le16(REASON_CODE_STA_LEAVING);

	lbs_deb_leave(LBS_DEB_JOIN);
	return 0;
}

int libertas_cmd_80211_associate(wlan_private * priv,
			      struct cmd_ds_command *cmd, void *pdata_buf)
{
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ds_802_11_associate *passo = &cmd->params.associate;
	int ret = 0;
	struct assoc_request * assoc_req = pdata_buf;
	struct bss_descriptor * bss = &assoc_req->bss;
	u8 *card_rates;
	u8 *pos;
	int card_rates_size;
	u16 tmpcap, tmplen;
	struct mrvlietypes_ssidparamset *ssid;
	struct mrvlietypes_phyparamset *phy;
	struct mrvlietypes_ssparamset *ss;
	struct mrvlietypes_ratesparamset *rates;
	struct mrvlietypes_rsnparamset *rsn;

	lbs_deb_enter(LBS_DEB_JOIN);

	pos = (u8 *) passo;

	if (!adapter) {
		ret = -1;
		goto done;
	}

	cmd->command = cpu_to_le16(cmd_802_11_associate);

	memcpy(passo->peerstaaddr, bss->bssid, sizeof(passo->peerstaaddr));
	pos += sizeof(passo->peerstaaddr);

	/* set the listen interval */
	passo->listeninterval = cpu_to_le16(adapter->listeninterval);

	pos += sizeof(passo->capinfo);
	pos += sizeof(passo->listeninterval);
	pos += sizeof(passo->bcnperiod);
	pos += sizeof(passo->dtimperiod);

	ssid = (struct mrvlietypes_ssidparamset *) pos;
	ssid->header.type = cpu_to_le16(TLV_TYPE_SSID);
	tmplen = bss->ssid_len;
	ssid->header.len = cpu_to_le16(tmplen);
	memcpy(ssid->ssid, bss->ssid, tmplen);
	pos += sizeof(ssid->header) + tmplen;

	phy = (struct mrvlietypes_phyparamset *) pos;
	phy->header.type = cpu_to_le16(TLV_TYPE_PHY_DS);
	tmplen = sizeof(phy->fh_ds.dsparamset);
	phy->header.len = cpu_to_le16(tmplen);
	memcpy(&phy->fh_ds.dsparamset,
	       &bss->phyparamset.dsparamset.currentchan,
	       tmplen);
	pos += sizeof(phy->header) + tmplen;

	ss = (struct mrvlietypes_ssparamset *) pos;
	ss->header.type = cpu_to_le16(TLV_TYPE_CF);
	tmplen = sizeof(ss->cf_ibss.cfparamset);
	ss->header.len = cpu_to_le16(tmplen);
	pos += sizeof(ss->header) + tmplen;

	rates = (struct mrvlietypes_ratesparamset *) pos;
	rates->header.type = cpu_to_le16(TLV_TYPE_RATES);

	memcpy(&rates->rates, &bss->libertas_supported_rates, WLAN_SUPPORTED_RATES);

	card_rates = libertas_supported_rates;
	card_rates_size = sizeof(libertas_supported_rates);

	if (get_common_rates(adapter, rates->rates, WLAN_SUPPORTED_RATES,
			     card_rates, card_rates_size)) {
		ret = -1;
		goto done;
	}

	tmplen = min_t(size_t, strlen(rates->rates), WLAN_SUPPORTED_RATES);
	adapter->curbssparams.numofrates = tmplen;

	pos += sizeof(rates->header) + tmplen;
	rates->header.len = cpu_to_le16(tmplen);

	if (assoc_req->secinfo.WPAenabled || assoc_req->secinfo.WPA2enabled) {
		rsn = (struct mrvlietypes_rsnparamset *) pos;
		/* WPA_IE or WPA2_IE */
		rsn->header.type = cpu_to_le16((u16) assoc_req->wpa_ie[0]);
		tmplen = (u16) assoc_req->wpa_ie[1];
		rsn->header.len = cpu_to_le16(tmplen);
		memcpy(rsn->rsnie, &assoc_req->wpa_ie[2], tmplen);
		lbs_dbg_hex("ASSOC_CMD: RSN IE", (u8 *) rsn,
			sizeof(rsn->header) + tmplen);
		pos += sizeof(rsn->header) + tmplen;
	}

	/* update curbssparams */
	adapter->curbssparams.channel = bss->phyparamset.dsparamset.currentchan;

	/* Copy the infra. association rates into Current BSS state structure */
	memcpy(&adapter->curbssparams.datarates, &rates->rates,
	       min_t(size_t, sizeof(adapter->curbssparams.datarates),
		     cpu_to_le16(rates->header.len)));

	lbs_deb_join("ASSOC_CMD: rates->header.len = %d\n",
		     cpu_to_le16(rates->header.len));

	/* set IBSS field */
	if (bss->mode == IW_MODE_INFRA) {
#define CAPINFO_ESS_MODE 1
		passo->capinfo.ess = CAPINFO_ESS_MODE;
	}

	if (libertas_parse_dnld_countryinfo_11d(priv, bss)) {
		ret = -1;
		goto done;
	}

	cmd->size = cpu_to_le16((u16) (pos - (u8 *) passo) + S_DS_GEN);

	/* set the capability info at last */
	memcpy(&tmpcap, &bss->cap, sizeof(passo->capinfo));
	tmpcap &= CAPINFO_MASK;
	lbs_deb_join("ASSOC_CMD: tmpcap=%4X CAPINFO_MASK=%4X\n",
		     tmpcap, CAPINFO_MASK);
	memcpy(&passo->capinfo, &tmpcap, sizeof(passo->capinfo));

done:
	lbs_deb_leave_args(LBS_DEB_JOIN, "ret %d", ret);
	return ret;
}

int libertas_cmd_80211_ad_hoc_start(wlan_private * priv,
				 struct cmd_ds_command *cmd, void *pdata_buf)
{
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ds_802_11_ad_hoc_start *adhs = &cmd->params.ads;
	int ret = 0;
	int cmdappendsize = 0;
	int i;
	struct assoc_request * assoc_req = pdata_buf;

	lbs_deb_enter(LBS_DEB_JOIN);

	if (!adapter) {
		ret = -1;
		goto done;
	}

	cmd->command = cpu_to_le16(cmd_802_11_ad_hoc_start);

	/*
	 * Fill in the parameters for 2 data structures:
	 *   1. cmd_ds_802_11_ad_hoc_start command
	 *   2. adapter->scantable[i]
	 *
	 * Driver will fill up SSID, bsstype,IBSS param, Physical Param,
	 *   probe delay, and cap info.
	 *
	 * Firmware will fill up beacon period, DTIM, Basic rates
	 *   and operational rates.
	 */

	memset(adhs->SSID, 0, IW_ESSID_MAX_SIZE);
	memcpy(adhs->SSID, assoc_req->ssid, assoc_req->ssid_len);

	lbs_deb_join("ADHOC_S_CMD: SSID '%s', ssid length %u\n",
	             escape_essid(assoc_req->ssid, assoc_req->ssid_len),
	             assoc_req->ssid_len);

	/* set the BSS type */
	adhs->bsstype = cmd_bss_type_ibss;
	adapter->mode = IW_MODE_ADHOC;
	adhs->beaconperiod = cpu_to_le16(adapter->beaconperiod);

	/* set Physical param set */
#define DS_PARA_IE_ID   3
#define DS_PARA_IE_LEN  1

	adhs->phyparamset.dsparamset.elementid = DS_PARA_IE_ID;
	adhs->phyparamset.dsparamset.len = DS_PARA_IE_LEN;

	WARN_ON(!assoc_req->channel);

	lbs_deb_join("ADHOC_S_CMD: Creating ADHOC on channel %d\n",
		     assoc_req->channel);

	adhs->phyparamset.dsparamset.currentchan = assoc_req->channel;

	/* set IBSS param set */
#define IBSS_PARA_IE_ID   6
#define IBSS_PARA_IE_LEN  2

	adhs->ssparamset.ibssparamset.elementid = IBSS_PARA_IE_ID;
	adhs->ssparamset.ibssparamset.len = IBSS_PARA_IE_LEN;
	adhs->ssparamset.ibssparamset.atimwindow = cpu_to_le16(adapter->atimwindow);

	/* set capability info */
	adhs->cap.ess = 0;
	adhs->cap.ibss = 1;

	/* probedelay */
	adhs->probedelay = cpu_to_le16(cmd_scan_probe_delay_time);

	/* set up privacy in adapter->scantable[i] */
	if (assoc_req->secinfo.wep_enabled) {
		lbs_deb_join("ADHOC_S_CMD: WEP enabled, setting privacy on\n");
		adhs->cap.privacy = AD_HOC_CAP_PRIVACY_ON;
	} else {
		lbs_deb_join("ADHOC_S_CMD: WEP disabled, setting privacy off\n");
	}

	memset(adhs->datarate, 0, sizeof(adhs->datarate));

	if (adapter->adhoc_grate_enabled) {
		memcpy(adhs->datarate, libertas_adhoc_rates_g,
		       min(sizeof(adhs->datarate), sizeof(libertas_adhoc_rates_g)));
	} else {
		memcpy(adhs->datarate, libertas_adhoc_rates_b,
		       min(sizeof(adhs->datarate), sizeof(libertas_adhoc_rates_b)));
	}

	/* Find the last non zero */
	for (i = 0; i < sizeof(adhs->datarate) && adhs->datarate[i]; i++) ;

	adapter->curbssparams.numofrates = i;

	/* Copy the ad-hoc creating rates into Current BSS state structure */
	memcpy(&adapter->curbssparams.datarates,
	       &adhs->datarate, adapter->curbssparams.numofrates);

	lbs_deb_join("ADHOC_S_CMD: rates=%02x %02x %02x %02x \n",
	       adhs->datarate[0], adhs->datarate[1],
	       adhs->datarate[2], adhs->datarate[3]);

	lbs_deb_join("ADHOC_S_CMD: AD HOC Start command is ready\n");

	if (libertas_create_dnld_countryinfo_11d(priv)) {
		lbs_deb_join("ADHOC_S_CMD: dnld_countryinfo_11d failed\n");
		ret = -1;
		goto done;
	}

	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_ad_hoc_start) +
				S_DS_GEN + cmdappendsize);

	ret = 0;
done:
	lbs_deb_leave_args(LBS_DEB_JOIN, "ret %d", ret);
	return ret;
}

int libertas_cmd_80211_ad_hoc_stop(wlan_private * priv,
				struct cmd_ds_command *cmd)
{
	cmd->command = cpu_to_le16(cmd_802_11_ad_hoc_stop);
	cmd->size = cpu_to_le16(S_DS_GEN);

	return 0;
}

int libertas_cmd_80211_ad_hoc_join(wlan_private * priv,
				struct cmd_ds_command *cmd, void *pdata_buf)
{
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ds_802_11_ad_hoc_join *padhocjoin = &cmd->params.adj;
	struct assoc_request * assoc_req = pdata_buf;
	struct bss_descriptor *bss = &assoc_req->bss;
	int cmdappendsize = 0;
	int ret = 0;
	u8 *card_rates;
	int card_rates_size;
	u16 tmpcap;
	int i;

	lbs_deb_enter(LBS_DEB_JOIN);

	cmd->command = cpu_to_le16(cmd_802_11_ad_hoc_join);

	padhocjoin->bssdescriptor.bsstype = cmd_bss_type_ibss;

	padhocjoin->bssdescriptor.beaconperiod = cpu_to_le16(bss->beaconperiod);

	memcpy(&padhocjoin->bssdescriptor.BSSID, &bss->bssid, ETH_ALEN);
	memcpy(&padhocjoin->bssdescriptor.SSID, &bss->ssid, bss->ssid_len);

	memcpy(&padhocjoin->bssdescriptor.phyparamset,
	       &bss->phyparamset, sizeof(union ieeetypes_phyparamset));

	memcpy(&padhocjoin->bssdescriptor.ssparamset,
	       &bss->ssparamset, sizeof(union IEEEtypes_ssparamset));

	memcpy(&tmpcap, &bss->cap, sizeof(struct ieeetypes_capinfo));
	tmpcap &= CAPINFO_MASK;

	lbs_deb_join("ADHOC_J_CMD: tmpcap=%4X CAPINFO_MASK=%4X\n",
	       tmpcap, CAPINFO_MASK);
	memcpy(&padhocjoin->bssdescriptor.cap, &tmpcap,
	       sizeof(struct ieeetypes_capinfo));

	/* information on BSSID descriptor passed to FW */
	lbs_deb_join(
	       "ADHOC_J_CMD: BSSID = " MAC_FMT ", SSID = '%s'\n",
	       MAC_ARG(padhocjoin->bssdescriptor.BSSID),
	       padhocjoin->bssdescriptor.SSID);

	/* failtimeout */
	padhocjoin->failtimeout = cpu_to_le16(MRVDRV_ASSOCIATION_TIME_OUT);

	/* probedelay */
	padhocjoin->probedelay = cpu_to_le16(cmd_scan_probe_delay_time);

	/* Copy Data rates from the rates recorded in scan response */
	memset(padhocjoin->bssdescriptor.datarates, 0,
	       sizeof(padhocjoin->bssdescriptor.datarates));
	memcpy(padhocjoin->bssdescriptor.datarates, bss->datarates,
	       min(sizeof(padhocjoin->bssdescriptor.datarates),
		   sizeof(bss->datarates)));

	card_rates = libertas_supported_rates;
	card_rates_size = sizeof(libertas_supported_rates);

	adapter->curbssparams.channel = bss->channel;

	if (get_common_rates(adapter, padhocjoin->bssdescriptor.datarates,
			     sizeof(padhocjoin->bssdescriptor.datarates),
			     card_rates, card_rates_size)) {
		lbs_deb_join("ADHOC_J_CMD: get_common_rates returns error.\n");
		ret = -1;
		goto done;
	}

	/* Find the last non zero */
	for (i = 0; i < sizeof(padhocjoin->bssdescriptor.datarates)
	     && padhocjoin->bssdescriptor.datarates[i]; i++) ;

	adapter->curbssparams.numofrates = i;

	/*
	 * Copy the adhoc joining rates to Current BSS State structure
	 */
	memcpy(adapter->curbssparams.datarates,
	       padhocjoin->bssdescriptor.datarates,
	       adapter->curbssparams.numofrates);

	padhocjoin->bssdescriptor.ssparamset.ibssparamset.atimwindow =
	    cpu_to_le16(bss->atimwindow);

	if (assoc_req->secinfo.wep_enabled) {
		padhocjoin->bssdescriptor.cap.privacy = AD_HOC_CAP_PRIVACY_ON;
	}

	if (adapter->psmode == wlan802_11powermodemax_psp) {
		/* wake up first */
		__le32 Localpsmode;

		Localpsmode = cpu_to_le32(wlan802_11powermodecam);
		ret = libertas_prepare_and_send_command(priv,
					    cmd_802_11_ps_mode,
					    cmd_act_set,
					    0, 0, &Localpsmode);

		if (ret) {
			ret = -1;
			goto done;
		}
	}

	if (libertas_parse_dnld_countryinfo_11d(priv, bss)) {
		ret = -1;
		goto done;
	}

	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_ad_hoc_join) +
				S_DS_GEN + cmdappendsize);

done:
	lbs_deb_leave_args(LBS_DEB_JOIN, "ret %d", ret);
	return ret;
}

int libertas_ret_80211_associate(wlan_private * priv,
			      struct cmd_ds_command *resp)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;
	union iwreq_data wrqu;
	struct ieeetypes_assocrsp *passocrsp;
	struct bss_descriptor * bss;

	lbs_deb_enter(LBS_DEB_JOIN);

	if (!adapter->in_progress_assoc_req) {
		lbs_deb_join("ASSOC_RESP: no in-progress association request\n");
		ret = -1;
		goto done;
	}
	bss = &adapter->in_progress_assoc_req->bss;

	passocrsp = (struct ieeetypes_assocrsp *) & resp->params;

	if (le16_to_cpu(passocrsp->statuscode)) {
		libertas_mac_event_disconnected(priv);

		lbs_deb_join("ASSOC_RESP: Association failed, status code = %d\n",
			     le16_to_cpu(passocrsp->statuscode));

		ret = -1;
		goto done;
	}

	lbs_dbg_hex("ASSOC_RESP:", (void *)&resp->params,
		le16_to_cpu(resp->size) - S_DS_GEN);

	/* Send a Media Connected event, according to the Spec */
	adapter->connect_status = libertas_connected;

	lbs_deb_join("ASSOC_RESP: assocated to '%s'\n",
	             escape_essid(bss->ssid, bss->ssid_len));

	/* Update current SSID and BSSID */
	memcpy(&adapter->curbssparams.ssid, &bss->ssid, IW_ESSID_MAX_SIZE);
	adapter->curbssparams.ssid_len = bss->ssid_len;
	memcpy(adapter->curbssparams.bssid, bss->bssid, ETH_ALEN);

	lbs_deb_join("ASSOC_RESP: currentpacketfilter is %x\n",
	       adapter->currentpacketfilter);

	adapter->SNR[TYPE_RXPD][TYPE_AVG] = 0;
	adapter->NF[TYPE_RXPD][TYPE_AVG] = 0;

	memset(adapter->rawSNR, 0x00, sizeof(adapter->rawSNR));
	memset(adapter->rawNF, 0x00, sizeof(adapter->rawNF));
	adapter->nextSNRNF = 0;
	adapter->numSNRNF = 0;

	netif_carrier_on(priv->dev);
	netif_wake_queue(priv->dev);

	netif_carrier_on(priv->mesh_dev);
	netif_wake_queue(priv->mesh_dev);

	lbs_deb_join("ASSOC_RESP: Associated \n");

	memcpy(wrqu.ap_addr.sa_data, adapter->curbssparams.bssid, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);

done:
	lbs_deb_leave_args(LBS_DEB_JOIN, "ret %d", ret);
	return ret;
}

int libertas_ret_80211_disassociate(wlan_private * priv,
				 struct cmd_ds_command *resp)
{
	lbs_deb_enter(LBS_DEB_JOIN);

	libertas_mac_event_disconnected(priv);

	lbs_deb_leave(LBS_DEB_JOIN);
	return 0;
}

int libertas_ret_80211_ad_hoc_start(wlan_private * priv,
				 struct cmd_ds_command *resp)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;
	u16 command = le16_to_cpu(resp->command);
	u16 result = le16_to_cpu(resp->result);
	struct cmd_ds_802_11_ad_hoc_result *padhocresult;
	union iwreq_data wrqu;
	struct bss_descriptor *bss;

	lbs_deb_enter(LBS_DEB_JOIN);

	padhocresult = &resp->params.result;

	lbs_deb_join("ADHOC_RESP: size = %d\n", le16_to_cpu(resp->size));
	lbs_deb_join("ADHOC_RESP: command = %x\n", command);
	lbs_deb_join("ADHOC_RESP: result = %x\n", result);

	if (!adapter->in_progress_assoc_req) {
		lbs_deb_join("ADHOC_RESP: no in-progress association request\n");
		ret = -1;
		goto done;
	}
	bss = &adapter->in_progress_assoc_req->bss;

	/*
	 * Join result code 0 --> SUCCESS
	 */
	if (result) {
		lbs_deb_join("ADHOC_RESP: failed\n");
		if (adapter->connect_status == libertas_connected) {
			libertas_mac_event_disconnected(priv);
		}
		ret = -1;
		goto done;
	}

	/*
	 * Now the join cmd should be successful
	 * If BSSID has changed use SSID to compare instead of BSSID
	 */
	lbs_deb_join("ADHOC_RESP: associated to '%s'\n",
	             escape_essid(bss->ssid, bss->ssid_len));

	/* Send a Media Connected event, according to the Spec */
	adapter->connect_status = libertas_connected;

	if (command == cmd_ret_802_11_ad_hoc_start) {
		/* Update the created network descriptor with the new BSSID */
		memcpy(bss->bssid, padhocresult->BSSID, ETH_ALEN);
	}

	/* Set the BSSID from the joined/started descriptor */
	memcpy(&adapter->curbssparams.bssid, bss->bssid, ETH_ALEN);

	/* Set the new SSID to current SSID */
	memcpy(&adapter->curbssparams.ssid, &bss->ssid, IW_ESSID_MAX_SIZE);
	adapter->curbssparams.ssid_len = bss->ssid_len;

	netif_carrier_on(priv->dev);
	netif_wake_queue(priv->dev);

	netif_carrier_on(priv->mesh_dev);
	netif_wake_queue(priv->mesh_dev);

	memset(&wrqu, 0, sizeof(wrqu));
	memcpy(wrqu.ap_addr.sa_data, adapter->curbssparams.bssid, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);

	lbs_deb_join("ADHOC_RESP: - Joined/Started Ad Hoc\n");
	lbs_deb_join("ADHOC_RESP: channel = %d\n", adapter->curbssparams.channel);
	lbs_deb_join("ADHOC_RESP: BSSID = " MAC_FMT "\n",
	       MAC_ARG(padhocresult->BSSID));

done:
	lbs_deb_leave_args(LBS_DEB_JOIN, "ret %d", ret);
	return ret;
}

int libertas_ret_80211_ad_hoc_stop(wlan_private * priv,
				struct cmd_ds_command *resp)
{
	lbs_deb_enter(LBS_DEB_JOIN);

	libertas_mac_event_disconnected(priv);

	lbs_deb_leave(LBS_DEB_JOIN);
	return 0;
}
