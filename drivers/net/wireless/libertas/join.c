/**
  *  Functions implementing wlan infrastructure and adhoc join routines,
  *  IOCTL handlers as well as command preperation and response routines
  *  for sending adhoc start, adhoc join, and association commands
  *  to the firmware.
  */
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>

#include <net/iw_handler.h>

#include "host.h"
#include "decl.h"
#include "join.h"
#include "dev.h"

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
	lbs_pr_debug(1, "Tx datarate is set to 0x%X\n", adapter->datarate);

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
int wlan_associate(wlan_private * priv, struct bss_descriptor * pbssdesc)
{
	wlan_adapter *adapter = priv->adapter;
	int ret;

	ENTER();

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_authenticate,
				    0, cmd_option_waitforrsp,
				    0, pbssdesc->macaddress);

	if (ret) {
		LEAVE();
		return ret;
	}

	/* set preamble to firmware */
	if (adapter->capinfo.shortpreamble && pbssdesc->cap.shortpreamble)
		adapter->preamble = cmd_type_short_preamble;
	else
		adapter->preamble = cmd_type_long_preamble;

	libertas_set_radio_control(priv);

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_associate,
				    0, cmd_option_waitforrsp, 0, pbssdesc);

	LEAVE();
	return ret;
}

/**
 *  @brief Start an Adhoc Network
 *
 *  @param priv         A pointer to wlan_private structure
 *  @param adhocssid    The ssid of the Adhoc Network
 *  @return             0--success, -1--fail
 */
int libertas_start_adhoc_network(wlan_private * priv, struct WLAN_802_11_SSID *adhocssid)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;

	adapter->adhoccreate = 1;

	if (!adapter->capinfo.shortpreamble) {
		lbs_pr_debug(1, "AdhocStart: Long preamble\n");
		adapter->preamble = cmd_type_long_preamble;
	} else {
		lbs_pr_debug(1, "AdhocStart: Short preamble\n");
		adapter->preamble = cmd_type_short_preamble;
	}

	libertas_set_radio_control(priv);

	lbs_pr_debug(1, "Adhoc channel = %d\n", adapter->adhocchannel);
	lbs_pr_debug(1, "curbssparams.channel = %d\n",
	       adapter->curbssparams.channel);
	lbs_pr_debug(1, "curbssparams.band = %d\n", adapter->curbssparams.band);

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_ad_hoc_start,
				    0, cmd_option_waitforrsp, 0, adhocssid);

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
int libertas_join_adhoc_network(wlan_private * priv, struct bss_descriptor * pbssdesc)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;

	lbs_pr_debug(1, "libertas_join_adhoc_network: CurBss.ssid =%s\n",
	       adapter->curbssparams.ssid.ssid);
	lbs_pr_debug(1, "libertas_join_adhoc_network: CurBss.ssid_len =%u\n",
	       adapter->curbssparams.ssid.ssidlength);
	lbs_pr_debug(1, "libertas_join_adhoc_network: ssid =%s\n", pbssdesc->ssid.ssid);
	lbs_pr_debug(1, "libertas_join_adhoc_network: ssid len =%u\n",
	       pbssdesc->ssid.ssidlength);

	/* check if the requested SSID is already joined */
	if (adapter->curbssparams.ssid.ssidlength
	    && !libertas_SSID_cmp(&pbssdesc->ssid, &adapter->curbssparams.ssid)
	    && (adapter->mode == IW_MODE_ADHOC)) {

        lbs_pr_debug(1,
		       "ADHOC_J_CMD: New ad-hoc SSID is the same as current, "
		       "not attempting to re-join");

		return -1;
	}

	/*Use shortpreamble only when both creator and card supports
	   short preamble */
	if (!pbssdesc->cap.shortpreamble || !adapter->capinfo.shortpreamble) {
		lbs_pr_debug(1, "AdhocJoin: Long preamble\n");
		adapter->preamble = cmd_type_long_preamble;
	} else {
		lbs_pr_debug(1, "AdhocJoin: Short preamble\n");
		adapter->preamble = cmd_type_short_preamble;
	}

	libertas_set_radio_control(priv);

	lbs_pr_debug(1, "curbssparams.channel = %d\n",
	       adapter->curbssparams.channel);
	lbs_pr_debug(1, "curbssparams.band = %c\n", adapter->curbssparams.band);

	adapter->adhoccreate = 0;

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_ad_hoc_join,
				    0, cmd_option_waitforrsp,
				    OID_802_11_SSID, pbssdesc);

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
		lbs_pr_debug(1, "AUTH_CMD: invalid auth alg 0x%X\n",
		             adapter->secinfo.auth_mode);
		goto out;
	}

	memcpy(pauthenticate->macaddr, bssid, ETH_ALEN);

	lbs_pr_debug(1, "AUTH_CMD: Bssid is : %x:%x:%x:%x:%x:%x\n",
	       bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
	ret = 0;

out:
	return ret;
}

int libertas_cmd_80211_deauthenticate(wlan_private * priv,
				   struct cmd_ds_command *cmd)
{
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ds_802_11_deauthenticate *dauth = &cmd->params.deauth;

	ENTER();

	cmd->command = cpu_to_le16(cmd_802_11_deauthenticate);
	cmd->size =
	    cpu_to_le16(sizeof(struct cmd_ds_802_11_deauthenticate) +
			     S_DS_GEN);

	/* set AP MAC address */
	memmove(dauth->macaddr, adapter->curbssparams.bssid,
		ETH_ALEN);

	/* Reason code 3 = Station is leaving */
#define REASON_CODE_STA_LEAVING 3
	dauth->reasoncode = cpu_to_le16(REASON_CODE_STA_LEAVING);

	LEAVE();
	return 0;
}

int libertas_cmd_80211_associate(wlan_private * priv,
			      struct cmd_ds_command *cmd, void *pdata_buf)
{
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ds_802_11_associate *passo = &cmd->params.associate;
	int ret = 0;
	struct bss_descriptor *pbssdesc;
	u8 *card_rates;
	u8 *pos;
	int card_rates_size;
	u16 tmpcap;
	struct mrvlietypes_ssidparamset *ssid;
	struct mrvlietypes_phyparamset *phy;
	struct mrvlietypes_ssparamset *ss;
	struct mrvlietypes_ratesparamset *rates;
	struct mrvlietypes_rsnparamset *rsn;

	ENTER();

	pbssdesc = pdata_buf;
	pos = (u8 *) passo;

	if (!adapter) {
		ret = -1;
		goto done;
	}

	cmd->command = cpu_to_le16(cmd_802_11_associate);

	/* Save so we know which BSS Desc to use in the response handler */
	adapter->pattemptedbssdesc = pbssdesc;

	memcpy(passo->peerstaaddr,
	       pbssdesc->macaddress, sizeof(passo->peerstaaddr));
	pos += sizeof(passo->peerstaaddr);

	/* set the listen interval */
	passo->listeninterval = adapter->listeninterval;

	pos += sizeof(passo->capinfo);
	pos += sizeof(passo->listeninterval);
	pos += sizeof(passo->bcnperiod);
	pos += sizeof(passo->dtimperiod);

	ssid = (struct mrvlietypes_ssidparamset *) pos;
	ssid->header.type = cpu_to_le16(TLV_TYPE_SSID);
	ssid->header.len = pbssdesc->ssid.ssidlength;
	memcpy(ssid->ssid, pbssdesc->ssid.ssid, ssid->header.len);
	pos += sizeof(ssid->header) + ssid->header.len;
	ssid->header.len = cpu_to_le16(ssid->header.len);

	phy = (struct mrvlietypes_phyparamset *) pos;
	phy->header.type = cpu_to_le16(TLV_TYPE_PHY_DS);
	phy->header.len = sizeof(phy->fh_ds.dsparamset);
	memcpy(&phy->fh_ds.dsparamset,
	       &pbssdesc->phyparamset.dsparamset.currentchan,
	       sizeof(phy->fh_ds.dsparamset));
	pos += sizeof(phy->header) + phy->header.len;
	phy->header.len = cpu_to_le16(phy->header.len);

	ss = (struct mrvlietypes_ssparamset *) pos;
	ss->header.type = cpu_to_le16(TLV_TYPE_CF);
	ss->header.len = sizeof(ss->cf_ibss.cfparamset);
	pos += sizeof(ss->header) + ss->header.len;
	ss->header.len = cpu_to_le16(ss->header.len);

	rates = (struct mrvlietypes_ratesparamset *) pos;
	rates->header.type = cpu_to_le16(TLV_TYPE_RATES);

	memcpy(&rates->rates, &pbssdesc->libertas_supported_rates, WLAN_SUPPORTED_RATES);

	card_rates = libertas_supported_rates;
	card_rates_size = sizeof(libertas_supported_rates);

	if (get_common_rates(adapter, rates->rates, WLAN_SUPPORTED_RATES,
			     card_rates, card_rates_size)) {
		ret = -1;
		goto done;
	}

	rates->header.len = min_t(size_t, strlen(rates->rates), WLAN_SUPPORTED_RATES);
	adapter->curbssparams.numofrates = rates->header.len;

	pos += sizeof(rates->header) + rates->header.len;
	rates->header.len = cpu_to_le16(rates->header.len);

	if (adapter->secinfo.WPAenabled || adapter->secinfo.WPA2enabled) {
		rsn = (struct mrvlietypes_rsnparamset *) pos;
		rsn->header.type = (u16) adapter->wpa_ie[0];	/* WPA_IE or WPA2_IE */
		rsn->header.type = cpu_to_le16(rsn->header.type);
		rsn->header.len = (u16) adapter->wpa_ie[1];
		memcpy(rsn->rsnie, &adapter->wpa_ie[2], rsn->header.len);
		lbs_dbg_hex("ASSOC_CMD: RSN IE", (u8 *) rsn,
			sizeof(rsn->header) + rsn->header.len);
		pos += sizeof(rsn->header) + rsn->header.len;
		rsn->header.len = cpu_to_le16(rsn->header.len);
	}

	/* update curbssparams */
	adapter->curbssparams.channel =
	    (pbssdesc->phyparamset.dsparamset.currentchan);

	/* Copy the infra. association rates into Current BSS state structure */
	memcpy(&adapter->curbssparams.datarates, &rates->rates,
	       min_t(size_t, sizeof(adapter->curbssparams.datarates), rates->header.len));

	lbs_pr_debug(1, "ASSOC_CMD: rates->header.len = %d\n", rates->header.len);

	/* set IBSS field */
	if (pbssdesc->mode == IW_MODE_INFRA) {
#define CAPINFO_ESS_MODE 1
		passo->capinfo.ess = CAPINFO_ESS_MODE;
	}

	if (libertas_parse_dnld_countryinfo_11d(priv)) {
		ret = -1;
		goto done;
	}

	cmd->size = cpu_to_le16((u16) (pos - (u8 *) passo) + S_DS_GEN);

	/* set the capability info at last */
	memcpy(&tmpcap, &pbssdesc->cap, sizeof(passo->capinfo));
	tmpcap &= CAPINFO_MASK;
	lbs_pr_debug(1, "ASSOC_CMD: tmpcap=%4X CAPINFO_MASK=%4X\n",
	       tmpcap, CAPINFO_MASK);
	tmpcap = cpu_to_le16(tmpcap);
	memcpy(&passo->capinfo, &tmpcap, sizeof(passo->capinfo));

      done:
	LEAVE();
	return ret;
}

int libertas_cmd_80211_ad_hoc_start(wlan_private * priv,
				 struct cmd_ds_command *cmd, void *pssid)
{
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ds_802_11_ad_hoc_start *adhs = &cmd->params.ads;
	int ret = 0;
	int cmdappendsize = 0;
	int i;
	u16 tmpcap;
	struct bss_descriptor *pbssdesc;
	struct WLAN_802_11_SSID *ssid = pssid;

	ENTER();

	if (!adapter) {
		ret = -1;
		goto done;
	}

	cmd->command = cpu_to_le16(cmd_802_11_ad_hoc_start);

	pbssdesc = &adapter->curbssparams.bssdescriptor;
	adapter->pattemptedbssdesc = pbssdesc;

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

	memcpy(adhs->SSID, ssid->ssid, ssid->ssidlength);

	lbs_pr_debug(1, "ADHOC_S_CMD: SSID = %s\n", adhs->SSID);

	memset(pbssdesc->ssid.ssid, 0, IW_ESSID_MAX_SIZE);
	memcpy(pbssdesc->ssid.ssid, ssid->ssid, ssid->ssidlength);

	pbssdesc->ssid.ssidlength = ssid->ssidlength;

	/* set the BSS type */
	adhs->bsstype = cmd_bss_type_ibss;
	pbssdesc->mode = IW_MODE_ADHOC;
	adhs->beaconperiod = adapter->beaconperiod;

	/* set Physical param set */
#define DS_PARA_IE_ID   3
#define DS_PARA_IE_LEN  1

	adhs->phyparamset.dsparamset.elementid = DS_PARA_IE_ID;
	adhs->phyparamset.dsparamset.len = DS_PARA_IE_LEN;

	WARN_ON(!adapter->adhocchannel);

	lbs_pr_debug(1, "ADHOC_S_CMD: Creating ADHOC on channel %d\n",
	       adapter->adhocchannel);

	adapter->curbssparams.channel = adapter->adhocchannel;

	pbssdesc->channel = adapter->adhocchannel;
	adhs->phyparamset.dsparamset.currentchan = adapter->adhocchannel;

	memcpy(&pbssdesc->phyparamset,
	       &adhs->phyparamset, sizeof(union ieeetypes_phyparamset));

	/* set IBSS param set */
#define IBSS_PARA_IE_ID   6
#define IBSS_PARA_IE_LEN  2

	adhs->ssparamset.ibssparamset.elementid = IBSS_PARA_IE_ID;
	adhs->ssparamset.ibssparamset.len = IBSS_PARA_IE_LEN;
	adhs->ssparamset.ibssparamset.atimwindow = adapter->atimwindow;
	memcpy(&pbssdesc->ssparamset,
	       &adhs->ssparamset, sizeof(union IEEEtypes_ssparamset));

	/* set capability info */
	adhs->cap.ess = 0;
	adhs->cap.ibss = 1;
	pbssdesc->cap.ibss = 1;

	/* probedelay */
	adhs->probedelay = cpu_to_le16(cmd_scan_probe_delay_time);

	/* set up privacy in adapter->scantable[i] */
	if (adapter->secinfo.wep_enabled) {
		lbs_pr_debug(1, "ADHOC_S_CMD: WEP enabled, setting privacy on\n");
		pbssdesc->privacy = wlan802_11privfilter8021xWEP;
		adhs->cap.privacy = AD_HOC_CAP_PRIVACY_ON;
	} else {
		lbs_pr_debug(1, "ADHOC_S_CMD: WEP disabled, setting privacy off\n");
		pbssdesc->privacy = wlan802_11privfilteracceptall;
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

	lbs_pr_debug(1, "ADHOC_S_CMD: rates=%02x %02x %02x %02x \n",
	       adhs->datarate[0], adhs->datarate[1],
	       adhs->datarate[2], adhs->datarate[3]);

	lbs_pr_debug(1, "ADHOC_S_CMD: AD HOC Start command is ready\n");

	if (libertas_create_dnld_countryinfo_11d(priv)) {
		lbs_pr_debug(1, "ADHOC_S_CMD: dnld_countryinfo_11d failed\n");
		ret = -1;
		goto done;
	}

	cmd->size =
	    cpu_to_le16(sizeof(struct cmd_ds_802_11_ad_hoc_start)
			     + S_DS_GEN + cmdappendsize);

	memcpy(&tmpcap, &adhs->cap, sizeof(u16));
	tmpcap = cpu_to_le16(tmpcap);
	memcpy(&adhs->cap, &tmpcap, sizeof(u16));

	ret = 0;
done:
	LEAVE();
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
	struct bss_descriptor *pbssdesc = pdata_buf;
	int cmdappendsize = 0;
	int ret = 0;
	u8 *card_rates;
	int card_rates_size;
	u16 tmpcap;
	int i;

	ENTER();

	adapter->pattemptedbssdesc = pbssdesc;

	cmd->command = cpu_to_le16(cmd_802_11_ad_hoc_join);

	padhocjoin->bssdescriptor.bsstype = cmd_bss_type_ibss;

	padhocjoin->bssdescriptor.beaconperiod = pbssdesc->beaconperiod;

	memcpy(&padhocjoin->bssdescriptor.BSSID,
	       &pbssdesc->macaddress, ETH_ALEN);

	memcpy(&padhocjoin->bssdescriptor.SSID,
	       &pbssdesc->ssid.ssid, pbssdesc->ssid.ssidlength);

	memcpy(&padhocjoin->bssdescriptor.phyparamset,
	       &pbssdesc->phyparamset, sizeof(union ieeetypes_phyparamset));

	memcpy(&padhocjoin->bssdescriptor.ssparamset,
	       &pbssdesc->ssparamset, sizeof(union IEEEtypes_ssparamset));

	memcpy(&tmpcap, &pbssdesc->cap, sizeof(struct ieeetypes_capinfo));
	tmpcap &= CAPINFO_MASK;

	lbs_pr_debug(1, "ADHOC_J_CMD: tmpcap=%4X CAPINFO_MASK=%4X\n",
	       tmpcap, CAPINFO_MASK);
	memcpy(&padhocjoin->bssdescriptor.cap, &tmpcap,
	       sizeof(struct ieeetypes_capinfo));

	/* information on BSSID descriptor passed to FW */
    lbs_pr_debug(1,
	       "ADHOC_J_CMD: BSSID = %2x-%2x-%2x-%2x-%2x-%2x, SSID = %s\n",
	       padhocjoin->bssdescriptor.BSSID[0],
	       padhocjoin->bssdescriptor.BSSID[1],
	       padhocjoin->bssdescriptor.BSSID[2],
	       padhocjoin->bssdescriptor.BSSID[3],
	       padhocjoin->bssdescriptor.BSSID[4],
	       padhocjoin->bssdescriptor.BSSID[5],
	       padhocjoin->bssdescriptor.SSID);

	/* failtimeout */
	padhocjoin->failtimeout = cpu_to_le16(MRVDRV_ASSOCIATION_TIME_OUT);

	/* probedelay */
	padhocjoin->probedelay =
	    cpu_to_le16(cmd_scan_probe_delay_time);

	/* Copy Data rates from the rates recorded in scan response */
	memset(padhocjoin->bssdescriptor.datarates, 0,
	       sizeof(padhocjoin->bssdescriptor.datarates));
	memcpy(padhocjoin->bssdescriptor.datarates, pbssdesc->datarates,
	       min(sizeof(padhocjoin->bssdescriptor.datarates),
		   sizeof(pbssdesc->datarates)));

	card_rates = libertas_supported_rates;
	card_rates_size = sizeof(libertas_supported_rates);

	adapter->curbssparams.channel = pbssdesc->channel;

	if (get_common_rates(adapter, padhocjoin->bssdescriptor.datarates,
			     sizeof(padhocjoin->bssdescriptor.datarates),
			     card_rates, card_rates_size)) {
		lbs_pr_debug(1, "ADHOC_J_CMD: get_common_rates returns error.\n");
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
	    cpu_to_le16(pbssdesc->atimwindow);

	if (adapter->secinfo.wep_enabled) {
		padhocjoin->bssdescriptor.cap.privacy = AD_HOC_CAP_PRIVACY_ON;
	}

	if (adapter->psmode == wlan802_11powermodemax_psp) {
		/* wake up first */
		enum WLAN_802_11_POWER_MODE Localpsmode;

		Localpsmode = wlan802_11powermodecam;
		ret = libertas_prepare_and_send_command(priv,
					    cmd_802_11_ps_mode,
					    cmd_act_set,
					    0, 0, &Localpsmode);

		if (ret) {
			ret = -1;
			goto done;
		}
	}

	if (libertas_parse_dnld_countryinfo_11d(priv)) {
		ret = -1;
		goto done;
	}

	cmd->size =
	    cpu_to_le16(sizeof(struct cmd_ds_802_11_ad_hoc_join)
			     + S_DS_GEN + cmdappendsize);

	memcpy(&tmpcap, &padhocjoin->bssdescriptor.cap,
	       sizeof(struct ieeetypes_capinfo));
	tmpcap = cpu_to_le16(tmpcap);

	memcpy(&padhocjoin->bssdescriptor.cap,
	       &tmpcap, sizeof(struct ieeetypes_capinfo));

      done:
	LEAVE();
	return ret;
}

int libertas_ret_80211_associate(wlan_private * priv,
			      struct cmd_ds_command *resp)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;
	union iwreq_data wrqu;
	struct ieeetypes_assocrsp *passocrsp;
	struct bss_descriptor *pbssdesc;

	ENTER();

	passocrsp = (struct ieeetypes_assocrsp *) & resp->params;

	if (passocrsp->statuscode) {

		libertas_mac_event_disconnected(priv);

        lbs_pr_debug(1,
		       "ASSOC_RESP: Association failed, status code = %d\n",
		       passocrsp->statuscode);

		ret = -1;
		goto done;
	}

	lbs_dbg_hex("ASSOC_RESP:", (void *)&resp->params,
		le16_to_cpu(resp->size) - S_DS_GEN);

	/* Send a Media Connected event, according to the Spec */
	adapter->connect_status = libertas_connected;

	/* Set the attempted BSSID Index to current */
	pbssdesc = adapter->pattemptedbssdesc;

	lbs_pr_debug(1, "ASSOC_RESP: %s\n", pbssdesc->ssid.ssid);

	/* Set the new SSID to current SSID */
	memcpy(&adapter->curbssparams.ssid,
	       &pbssdesc->ssid, sizeof(struct WLAN_802_11_SSID));

	/* Set the new BSSID (AP's MAC address) to current BSSID */
	memcpy(adapter->curbssparams.bssid,
	       pbssdesc->macaddress, ETH_ALEN);

	/* Make a copy of current BSSID descriptor */
	memcpy(&adapter->curbssparams.bssdescriptor,
	       pbssdesc, sizeof(struct bss_descriptor));

	lbs_pr_debug(1, "ASSOC_RESP: currentpacketfilter is %x\n",
	       adapter->currentpacketfilter);

	adapter->SNR[TYPE_RXPD][TYPE_AVG] = 0;
	adapter->NF[TYPE_RXPD][TYPE_AVG] = 0;

	memset(adapter->rawSNR, 0x00, sizeof(adapter->rawSNR));
	memset(adapter->rawNF, 0x00, sizeof(adapter->rawNF));
	adapter->nextSNRNF = 0;
	adapter->numSNRNF = 0;

	netif_carrier_on(priv->wlan_dev.netdev);
	netif_wake_queue(priv->wlan_dev.netdev);

	lbs_pr_debug(1, "ASSOC_RESP: Associated \n");

	memcpy(wrqu.ap_addr.sa_data, adapter->curbssparams.bssid, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->wlan_dev.netdev, SIOCGIWAP, &wrqu, NULL);

      done:
	LEAVE();
	return ret;
}

int libertas_ret_80211_disassociate(wlan_private * priv,
				 struct cmd_ds_command *resp)
{
	ENTER();

	libertas_mac_event_disconnected(priv);

	LEAVE();
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
	struct bss_descriptor *pbssdesc;

	ENTER();

	padhocresult = &resp->params.result;

	lbs_pr_debug(1, "ADHOC_S_RESP: size = %d\n", le16_to_cpu(resp->size));
	lbs_pr_debug(1, "ADHOC_S_RESP: command = %x\n", command);
	lbs_pr_debug(1, "ADHOC_S_RESP: result = %x\n", result);

	pbssdesc = adapter->pattemptedbssdesc;

	/*
	 * Join result code 0 --> SUCCESS
	 */
	if (result) {
		lbs_pr_debug(1, "ADHOC_RESP failed\n");
		if (adapter->connect_status == libertas_connected) {
			libertas_mac_event_disconnected(priv);
		}

		memset(&adapter->curbssparams.bssdescriptor,
		       0x00, sizeof(adapter->curbssparams.bssdescriptor));

		LEAVE();
		return -1;
	}

	/*
	 * Now the join cmd should be successful
	 * If BSSID has changed use SSID to compare instead of BSSID
	 */
	lbs_pr_debug(1, "ADHOC_J_RESP  %s\n", pbssdesc->ssid.ssid);

	/* Send a Media Connected event, according to the Spec */
	adapter->connect_status = libertas_connected;

	if (command == cmd_ret_802_11_ad_hoc_start) {
		/* Update the created network descriptor with the new BSSID */
		memcpy(pbssdesc->macaddress,
		       padhocresult->BSSID, ETH_ALEN);
	} else {

		/* Make a copy of current BSSID descriptor, only needed for join since
		 *   the current descriptor is already being used for adhoc start
		 */
		memmove(&adapter->curbssparams.bssdescriptor,
			pbssdesc, sizeof(struct bss_descriptor));
	}

	/* Set the BSSID from the joined/started descriptor */
	memcpy(&adapter->curbssparams.bssid,
	       pbssdesc->macaddress, ETH_ALEN);

	/* Set the new SSID to current SSID */
	memcpy(&adapter->curbssparams.ssid,
	       &pbssdesc->ssid, sizeof(struct WLAN_802_11_SSID));

	netif_carrier_on(priv->wlan_dev.netdev);
	netif_wake_queue(priv->wlan_dev.netdev);

	memset(&wrqu, 0, sizeof(wrqu));
	memcpy(wrqu.ap_addr.sa_data, adapter->curbssparams.bssid, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->wlan_dev.netdev, SIOCGIWAP, &wrqu, NULL);

	lbs_pr_debug(1, "ADHOC_RESP: - Joined/Started Ad Hoc\n");
	lbs_pr_debug(1, "ADHOC_RESP: channel = %d\n", adapter->adhocchannel);
	lbs_pr_debug(1, "ADHOC_RESP: BSSID = %02x:%02x:%02x:%02x:%02x:%02x\n",
	       padhocresult->BSSID[0], padhocresult->BSSID[1],
	       padhocresult->BSSID[2], padhocresult->BSSID[3],
	       padhocresult->BSSID[4], padhocresult->BSSID[5]);

	LEAVE();
	return ret;
}

int libertas_ret_80211_ad_hoc_stop(wlan_private * priv,
				struct cmd_ds_command *resp)
{
	ENTER();

	libertas_mac_event_disconnected(priv);

	LEAVE();
	return 0;
}
