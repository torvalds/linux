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

/* The firmware needs certain bits masked out of the beacon-derviced capability
 * field when associating/joining to BSSs.
 */
#define CAPINFO_MASK	(~(0xda00))

/**
 *  @brief This function finds common rates between rate1 and card rates.
 *
 * It will fill common rates in rate1 as output if found.
 *
 * NOTE: Setting the MSB of the basic rates need to be taken
 *   care, either before or after calling this function
 *
 *  @param priv     A pointer to struct lbs_private structure
 *  @param rate1       the buffer which keeps input and output
 *  @param rate1_size  the size of rate1 buffer; new size of buffer on return
 *
 *  @return            0 or -1
 */
static int get_common_rates(struct lbs_private *priv,
	u8 *rates,
	u16 *rates_size)
{
	u8 *card_rates = lbs_bg_rates;
	size_t num_card_rates = sizeof(lbs_bg_rates);
	int ret = 0, i, j;
	u8 tmp[30];
	size_t tmp_size = 0;

	/* For each rate in card_rates that exists in rate1, copy to tmp */
	for (i = 0; card_rates[i] && (i < num_card_rates); i++) {
		for (j = 0; rates[j] && (j < *rates_size); j++) {
			if (rates[j] == card_rates[i])
				tmp[tmp_size++] = card_rates[i];
		}
	}

	lbs_deb_hex(LBS_DEB_JOIN, "AP rates    ", rates, *rates_size);
	lbs_deb_hex(LBS_DEB_JOIN, "card rates  ", card_rates, num_card_rates);
	lbs_deb_hex(LBS_DEB_JOIN, "common rates", tmp, tmp_size);
	lbs_deb_join("TX data rate 0x%02x\n", priv->cur_rate);

	if (!priv->auto_rate) {
		for (i = 0; i < tmp_size; i++) {
			if (tmp[i] == priv->cur_rate)
				goto done;
		}
		lbs_pr_alert("Previously set fixed data rate %#x isn't "
		       "compatible with the network.\n", priv->cur_rate);
		ret = -1;
		goto done;
	}
	ret = 0;

done:
	memset(rates, 0, *rates_size);
	*rates_size = min_t(int, tmp_size, *rates_size);
	memcpy(rates, tmp, *rates_size);
	return ret;
}


/**
 *  @brief Sets the MSB on basic rates as the firmware requires
 *
 * Scan through an array and set the MSB for basic data rates.
 *
 *  @param rates     buffer of data rates
 *  @param len       size of buffer
 */
static void lbs_set_basic_rate_flags(u8 *rates, size_t len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (rates[i] == 0x02 || rates[i] == 0x04 ||
		    rates[i] == 0x0b || rates[i] == 0x16)
			rates[i] |= 0x80;
	}
}

/**
 *  @brief Unsets the MSB on basic rates
 *
 * Scan through an array and unset the MSB for basic data rates.
 *
 *  @param rates     buffer of data rates
 *  @param len       size of buffer
 */
void lbs_unset_basic_rate_flags(u8 *rates, size_t len)
{
	int i;

	for (i = 0; i < len; i++)
		rates[i] &= 0x7f;
}


/**
 *  @brief Associate to a specific BSS discovered in a scan
 *
 *  @param priv      A pointer to struct lbs_private structure
 *  @param pbssdesc  Pointer to the BSS descriptor to associate with.
 *
 *  @return          0-success, otherwise fail
 */
int lbs_associate(struct lbs_private *priv, struct assoc_request *assoc_req)
{
	int ret;

	lbs_deb_enter(LBS_DEB_ASSOC);

	ret = lbs_prepare_and_send_command(priv, CMD_802_11_AUTHENTICATE,
				    0, CMD_OPTION_WAITFORRSP,
				    0, assoc_req->bss.bssid);

	if (ret)
		goto done;

	/* set preamble to firmware */
	if (   (priv->capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
	    && (assoc_req->bss.capability & WLAN_CAPABILITY_SHORT_PREAMBLE))
		priv->preamble = CMD_TYPE_SHORT_PREAMBLE;
	else
		priv->preamble = CMD_TYPE_LONG_PREAMBLE;

	lbs_set_radio_control(priv);

	ret = lbs_prepare_and_send_command(priv, CMD_802_11_ASSOCIATE,
				    0, CMD_OPTION_WAITFORRSP, 0, assoc_req);

done:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

/**
 *  @brief Start an Adhoc Network
 *
 *  @param priv         A pointer to struct lbs_private structure
 *  @param adhocssid    The ssid of the Adhoc Network
 *  @return             0--success, -1--fail
 */
int lbs_start_adhoc_network(struct lbs_private *priv,
	struct assoc_request *assoc_req)
{
	int ret = 0;

	priv->adhoccreate = 1;

	if (priv->capability & WLAN_CAPABILITY_SHORT_PREAMBLE) {
		lbs_deb_join("AdhocStart: Short preamble\n");
		priv->preamble = CMD_TYPE_SHORT_PREAMBLE;
	} else {
		lbs_deb_join("AdhocStart: Long preamble\n");
		priv->preamble = CMD_TYPE_LONG_PREAMBLE;
	}

	lbs_set_radio_control(priv);

	lbs_deb_join("AdhocStart: channel = %d\n", assoc_req->channel);
	lbs_deb_join("AdhocStart: band = %d\n", assoc_req->band);

	ret = lbs_prepare_and_send_command(priv, CMD_802_11_AD_HOC_START,
				    0, CMD_OPTION_WAITFORRSP, 0, assoc_req);

	return ret;
}

/**
 *  @brief Join an adhoc network found in a previous scan
 *
 *  @param priv         A pointer to struct lbs_private structure
 *  @param pbssdesc     Pointer to a BSS descriptor found in a previous scan
 *                      to attempt to join
 *
 *  @return             0--success, -1--fail
 */
int lbs_join_adhoc_network(struct lbs_private *priv,
	struct assoc_request *assoc_req)
{
	struct bss_descriptor * bss = &assoc_req->bss;
	int ret = 0;

	lbs_deb_join("%s: Current SSID '%s', ssid length %u\n",
	             __func__,
	             escape_essid(priv->curbssparams.ssid,
	                          priv->curbssparams.ssid_len),
	             priv->curbssparams.ssid_len);
	lbs_deb_join("%s: requested ssid '%s', ssid length %u\n",
	             __func__, escape_essid(bss->ssid, bss->ssid_len),
	             bss->ssid_len);

	/* check if the requested SSID is already joined */
	if (   priv->curbssparams.ssid_len
	    && !lbs_ssid_cmp(priv->curbssparams.ssid,
	                          priv->curbssparams.ssid_len,
	                          bss->ssid, bss->ssid_len)
	    && (priv->mode == IW_MODE_ADHOC)
	    && (priv->connect_status == LBS_CONNECTED)) {
		union iwreq_data wrqu;

		lbs_deb_join("ADHOC_J_CMD: New ad-hoc SSID is the same as "
		             "current, not attempting to re-join");

		/* Send the re-association event though, because the association
		 * request really was successful, even if just a null-op.
		 */
		memset(&wrqu, 0, sizeof(wrqu));
		memcpy(wrqu.ap_addr.sa_data, priv->curbssparams.bssid,
		       ETH_ALEN);
		wrqu.ap_addr.sa_family = ARPHRD_ETHER;
		wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);
		goto out;
	}

	/* Use shortpreamble only when both creator and card supports
	   short preamble */
	if (   !(bss->capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
	    || !(priv->capability & WLAN_CAPABILITY_SHORT_PREAMBLE)) {
		lbs_deb_join("AdhocJoin: Long preamble\n");
		priv->preamble = CMD_TYPE_LONG_PREAMBLE;
	} else {
		lbs_deb_join("AdhocJoin: Short preamble\n");
		priv->preamble = CMD_TYPE_SHORT_PREAMBLE;
	}

	lbs_set_radio_control(priv);

	lbs_deb_join("AdhocJoin: channel = %d\n", assoc_req->channel);
	lbs_deb_join("AdhocJoin: band = %c\n", assoc_req->band);

	priv->adhoccreate = 0;

	ret = lbs_prepare_and_send_command(priv, CMD_802_11_AD_HOC_JOIN,
				    0, CMD_OPTION_WAITFORRSP,
				    OID_802_11_SSID, assoc_req);

out:
	return ret;
}

int lbs_stop_adhoc_network(struct lbs_private *priv)
{
	return lbs_prepare_and_send_command(priv, CMD_802_11_AD_HOC_STOP,
				     0, CMD_OPTION_WAITFORRSP, 0, NULL);
}

/**
 *  @brief Send Deauthentication Request
 *
 *  @param priv      A pointer to struct lbs_private structure
 *  @return          0--success, -1--fail
 */
int lbs_send_deauthentication(struct lbs_private *priv)
{
	return lbs_prepare_and_send_command(priv, CMD_802_11_DEAUTHENTICATE,
				     0, CMD_OPTION_WAITFORRSP, 0, NULL);
}

/**
 *  @brief This function prepares command of authenticate.
 *
 *  @param priv      A pointer to struct lbs_private structure
 *  @param cmd       A pointer to cmd_ds_command structure
 *  @param pdata_buf Void cast of pointer to a BSSID to authenticate with
 *
 *  @return         0 or -1
 */
int lbs_cmd_80211_authenticate(struct lbs_private *priv,
				 struct cmd_ds_command *cmd,
				 void *pdata_buf)
{
	struct cmd_ds_802_11_authenticate *pauthenticate = &cmd->params.auth;
	int ret = -1;
	u8 *bssid = pdata_buf;
	DECLARE_MAC_BUF(mac);

	lbs_deb_enter(LBS_DEB_JOIN);

	cmd->command = cpu_to_le16(CMD_802_11_AUTHENTICATE);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_authenticate)
	                        + S_DS_GEN);

	/* translate auth mode to 802.11 defined wire value */
	switch (priv->secinfo.auth_mode) {
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
		             priv->secinfo.auth_mode);
		goto out;
	}

	memcpy(pauthenticate->macaddr, bssid, ETH_ALEN);

	lbs_deb_join("AUTH_CMD: BSSID %s, auth 0x%x\n",
	             print_mac(mac, bssid), pauthenticate->authtype);
	ret = 0;

out:
	lbs_deb_leave_args(LBS_DEB_JOIN, "ret %d", ret);
	return ret;
}

int lbs_cmd_80211_deauthenticate(struct lbs_private *priv,
				   struct cmd_ds_command *cmd)
{
	struct cmd_ds_802_11_deauthenticate *dauth = &cmd->params.deauth;

	lbs_deb_enter(LBS_DEB_JOIN);

	cmd->command = cpu_to_le16(CMD_802_11_DEAUTHENTICATE);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_deauthenticate) +
			     S_DS_GEN);

	/* set AP MAC address */
	memmove(dauth->macaddr, priv->curbssparams.bssid, ETH_ALEN);

	/* Reason code 3 = Station is leaving */
#define REASON_CODE_STA_LEAVING 3
	dauth->reasoncode = cpu_to_le16(REASON_CODE_STA_LEAVING);

	lbs_deb_leave(LBS_DEB_JOIN);
	return 0;
}

int lbs_cmd_80211_associate(struct lbs_private *priv,
			      struct cmd_ds_command *cmd, void *pdata_buf)
{
	struct cmd_ds_802_11_associate *passo = &cmd->params.associate;
	int ret = 0;
	struct assoc_request * assoc_req = pdata_buf;
	struct bss_descriptor * bss = &assoc_req->bss;
	u8 *pos;
	u16 tmpcap, tmplen;
	struct mrvlietypes_ssidparamset *ssid;
	struct mrvlietypes_phyparamset *phy;
	struct mrvlietypes_ssparamset *ss;
	struct mrvlietypes_ratesparamset *rates;
	struct mrvlietypes_rsnparamset *rsn;

	lbs_deb_enter(LBS_DEB_ASSOC);

	pos = (u8 *) passo;

	if (!priv) {
		ret = -1;
		goto done;
	}

	cmd->command = cpu_to_le16(CMD_802_11_ASSOCIATE);

	memcpy(passo->peerstaaddr, bss->bssid, sizeof(passo->peerstaaddr));
	pos += sizeof(passo->peerstaaddr);

	/* set the listen interval */
	passo->listeninterval = cpu_to_le16(MRVDRV_DEFAULT_LISTEN_INTERVAL);

	pos += sizeof(passo->capability);
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
	memcpy(&rates->rates, &bss->rates, MAX_RATES);
	tmplen = MAX_RATES;
	if (get_common_rates(priv, rates->rates, &tmplen)) {
		ret = -1;
		goto done;
	}
	pos += sizeof(rates->header) + tmplen;
	rates->header.len = cpu_to_le16(tmplen);
	lbs_deb_assoc("ASSOC_CMD: num rates %u\n", tmplen);

	/* Copy the infra. association rates into Current BSS state structure */
	memset(&priv->curbssparams.rates, 0, sizeof(priv->curbssparams.rates));
	memcpy(&priv->curbssparams.rates, &rates->rates, tmplen);

	/* Set MSB on basic rates as the firmware requires, but _after_
	 * copying to current bss rates.
	 */
	lbs_set_basic_rate_flags(rates->rates, tmplen);

	if (assoc_req->secinfo.WPAenabled || assoc_req->secinfo.WPA2enabled) {
		rsn = (struct mrvlietypes_rsnparamset *) pos;
		/* WPA_IE or WPA2_IE */
		rsn->header.type = cpu_to_le16((u16) assoc_req->wpa_ie[0]);
		tmplen = (u16) assoc_req->wpa_ie[1];
		rsn->header.len = cpu_to_le16(tmplen);
		memcpy(rsn->rsnie, &assoc_req->wpa_ie[2], tmplen);
		lbs_deb_hex(LBS_DEB_JOIN, "ASSOC_CMD: RSN IE", (u8 *) rsn,
			sizeof(rsn->header) + tmplen);
		pos += sizeof(rsn->header) + tmplen;
	}

	/* update curbssparams */
	priv->curbssparams.channel = bss->phyparamset.dsparamset.currentchan;

	if (lbs_parse_dnld_countryinfo_11d(priv, bss)) {
		ret = -1;
		goto done;
	}

	cmd->size = cpu_to_le16((u16) (pos - (u8 *) passo) + S_DS_GEN);

	/* set the capability info */
	tmpcap = (bss->capability & CAPINFO_MASK);
	if (bss->mode == IW_MODE_INFRA)
		tmpcap |= WLAN_CAPABILITY_ESS;
	passo->capability = cpu_to_le16(tmpcap);
	lbs_deb_assoc("ASSOC_CMD: capability 0x%04x\n", tmpcap);

done:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

int lbs_cmd_80211_ad_hoc_start(struct lbs_private *priv,
				 struct cmd_ds_command *cmd, void *pdata_buf)
{
	struct cmd_ds_802_11_ad_hoc_start *adhs = &cmd->params.ads;
	int ret = 0;
	int cmdappendsize = 0;
	struct assoc_request * assoc_req = pdata_buf;
	u16 tmpcap = 0;
	size_t ratesize = 0;

	lbs_deb_enter(LBS_DEB_JOIN);

	if (!priv) {
		ret = -1;
		goto done;
	}

	cmd->command = cpu_to_le16(CMD_802_11_AD_HOC_START);

	/*
	 * Fill in the parameters for 2 data structures:
	 *   1. cmd_ds_802_11_ad_hoc_start command
	 *   2. priv->scantable[i]
	 *
	 * Driver will fill up SSID, bsstype,IBSS param, Physical Param,
	 *   probe delay, and cap info.
	 *
	 * Firmware will fill up beacon period, DTIM, Basic rates
	 *   and operational rates.
	 */

	memset(adhs->ssid, 0, IW_ESSID_MAX_SIZE);
	memcpy(adhs->ssid, assoc_req->ssid, assoc_req->ssid_len);

	lbs_deb_join("ADHOC_S_CMD: SSID '%s', ssid length %u\n",
	             escape_essid(assoc_req->ssid, assoc_req->ssid_len),
	             assoc_req->ssid_len);

	/* set the BSS type */
	adhs->bsstype = CMD_BSS_TYPE_IBSS;
	priv->mode = IW_MODE_ADHOC;
	if (priv->beacon_period == 0)
		priv->beacon_period = MRVDRV_BEACON_INTERVAL;
	adhs->beaconperiod = cpu_to_le16(priv->beacon_period);

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
	adhs->ssparamset.ibssparamset.atimwindow = 0;

	/* set capability info */
	tmpcap = WLAN_CAPABILITY_IBSS;
	if (assoc_req->secinfo.wep_enabled) {
		lbs_deb_join("ADHOC_S_CMD: WEP enabled, setting privacy on\n");
		tmpcap |= WLAN_CAPABILITY_PRIVACY;
	} else {
		lbs_deb_join("ADHOC_S_CMD: WEP disabled, setting privacy off\n");
	}
	adhs->capability = cpu_to_le16(tmpcap);

	/* probedelay */
	adhs->probedelay = cpu_to_le16(CMD_SCAN_PROBE_DELAY_TIME);

	memset(adhs->rates, 0, sizeof(adhs->rates));
	ratesize = min(sizeof(adhs->rates), sizeof(lbs_bg_rates));
	memcpy(adhs->rates, lbs_bg_rates, ratesize);

	/* Copy the ad-hoc creating rates into Current BSS state structure */
	memset(&priv->curbssparams.rates, 0, sizeof(priv->curbssparams.rates));
	memcpy(&priv->curbssparams.rates, &adhs->rates, ratesize);

	/* Set MSB on basic rates as the firmware requires, but _after_
	 * copying to current bss rates.
	 */
	lbs_set_basic_rate_flags(adhs->rates, ratesize);

	lbs_deb_join("ADHOC_S_CMD: rates=%02x %02x %02x %02x \n",
	       adhs->rates[0], adhs->rates[1], adhs->rates[2], adhs->rates[3]);

	lbs_deb_join("ADHOC_S_CMD: AD HOC Start command is ready\n");

	if (lbs_create_dnld_countryinfo_11d(priv)) {
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

int lbs_cmd_80211_ad_hoc_stop(struct lbs_private *priv,
				struct cmd_ds_command *cmd)
{
	cmd->command = cpu_to_le16(CMD_802_11_AD_HOC_STOP);
	cmd->size = cpu_to_le16(S_DS_GEN);

	return 0;
}

int lbs_cmd_80211_ad_hoc_join(struct lbs_private *priv,
				struct cmd_ds_command *cmd, void *pdata_buf)
{
	struct cmd_ds_802_11_ad_hoc_join *join_cmd = &cmd->params.adj;
	struct assoc_request * assoc_req = pdata_buf;
	struct bss_descriptor *bss = &assoc_req->bss;
	int cmdappendsize = 0;
	int ret = 0;
	u16 ratesize = 0;
	DECLARE_MAC_BUF(mac);

	lbs_deb_enter(LBS_DEB_JOIN);

	cmd->command = cpu_to_le16(CMD_802_11_AD_HOC_JOIN);

	join_cmd->bss.type = CMD_BSS_TYPE_IBSS;
	join_cmd->bss.beaconperiod = cpu_to_le16(bss->beaconperiod);

	memcpy(&join_cmd->bss.bssid, &bss->bssid, ETH_ALEN);
	memcpy(&join_cmd->bss.ssid, &bss->ssid, bss->ssid_len);

	memcpy(&join_cmd->bss.phyparamset, &bss->phyparamset,
	       sizeof(union ieeetypes_phyparamset));

	memcpy(&join_cmd->bss.ssparamset, &bss->ssparamset,
	       sizeof(union IEEEtypes_ssparamset));

	join_cmd->bss.capability = cpu_to_le16(bss->capability & CAPINFO_MASK);
	lbs_deb_join("ADHOC_J_CMD: tmpcap=%4X CAPINFO_MASK=%4X\n",
	       bss->capability, CAPINFO_MASK);

	/* information on BSSID descriptor passed to FW */
	lbs_deb_join(
	       "ADHOC_J_CMD: BSSID = %s, SSID = '%s'\n",
	       print_mac(mac, join_cmd->bss.bssid),
	       join_cmd->bss.ssid);

	/* failtimeout */
	join_cmd->failtimeout = cpu_to_le16(MRVDRV_ASSOCIATION_TIME_OUT);

	/* probedelay */
	join_cmd->probedelay = cpu_to_le16(CMD_SCAN_PROBE_DELAY_TIME);

	priv->curbssparams.channel = bss->channel;

	/* Copy Data rates from the rates recorded in scan response */
	memset(join_cmd->bss.rates, 0, sizeof(join_cmd->bss.rates));
	ratesize = min_t(u16, sizeof(join_cmd->bss.rates), MAX_RATES);
	memcpy(join_cmd->bss.rates, bss->rates, ratesize);
	if (get_common_rates(priv, join_cmd->bss.rates, &ratesize)) {
		lbs_deb_join("ADHOC_J_CMD: get_common_rates returns error.\n");
		ret = -1;
		goto done;
	}

	/* Copy the ad-hoc creating rates into Current BSS state structure */
	memset(&priv->curbssparams.rates, 0, sizeof(priv->curbssparams.rates));
	memcpy(&priv->curbssparams.rates, join_cmd->bss.rates, ratesize);

	/* Set MSB on basic rates as the firmware requires, but _after_
	 * copying to current bss rates.
	 */
	lbs_set_basic_rate_flags(join_cmd->bss.rates, ratesize);

	join_cmd->bss.ssparamset.ibssparamset.atimwindow =
	    cpu_to_le16(bss->atimwindow);

	if (assoc_req->secinfo.wep_enabled) {
		u16 tmp = le16_to_cpu(join_cmd->bss.capability);
		tmp |= WLAN_CAPABILITY_PRIVACY;
		join_cmd->bss.capability = cpu_to_le16(tmp);
	}

	if (priv->psmode == LBS802_11POWERMODEMAX_PSP) {
		/* wake up first */
		__le32 Localpsmode;

		Localpsmode = cpu_to_le32(LBS802_11POWERMODECAM);
		ret = lbs_prepare_and_send_command(priv,
					    CMD_802_11_PS_MODE,
					    CMD_ACT_SET,
					    0, 0, &Localpsmode);

		if (ret) {
			ret = -1;
			goto done;
		}
	}

	if (lbs_parse_dnld_countryinfo_11d(priv, bss)) {
		ret = -1;
		goto done;
	}

	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_ad_hoc_join) +
				S_DS_GEN + cmdappendsize);

done:
	lbs_deb_leave_args(LBS_DEB_JOIN, "ret %d", ret);
	return ret;
}

int lbs_ret_80211_associate(struct lbs_private *priv,
			      struct cmd_ds_command *resp)
{
	int ret = 0;
	union iwreq_data wrqu;
	struct ieeetypes_assocrsp *passocrsp;
	struct bss_descriptor * bss;
	u16 status_code;

	lbs_deb_enter(LBS_DEB_ASSOC);

	if (!priv->in_progress_assoc_req) {
		lbs_deb_assoc("ASSOC_RESP: no in-progress assoc request\n");
		ret = -1;
		goto done;
	}
	bss = &priv->in_progress_assoc_req->bss;

	passocrsp = (struct ieeetypes_assocrsp *) & resp->params;

	/*
	 * Older FW versions map the IEEE 802.11 Status Code in the association
	 * response to the following values returned in passocrsp->statuscode:
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

	status_code = le16_to_cpu(passocrsp->statuscode);
	switch (status_code) {
	case 0x00:
		break;
	case 0x01:
		lbs_deb_assoc("ASSOC_RESP: invalid parameters\n");
		break;
	case 0x02:
		lbs_deb_assoc("ASSOC_RESP: internal timer "
			"expired while waiting for the AP\n");
		break;
	case 0x03:
		lbs_deb_assoc("ASSOC_RESP: association "
			"refused by AP\n");
		break;
	case 0x04:
		lbs_deb_assoc("ASSOC_RESP: authentication "
			"refused by AP\n");
		break;
	default:
		lbs_deb_assoc("ASSOC_RESP: failure reason 0x%02x "
			" unknown\n", status_code);
		break;
	}

	if (status_code) {
		lbs_mac_event_disconnected(priv);
		ret = -1;
		goto done;
	}

	lbs_deb_hex(LBS_DEB_ASSOC, "ASSOC_RESP", (void *)&resp->params,
		le16_to_cpu(resp->size) - S_DS_GEN);

	/* Send a Media Connected event, according to the Spec */
	priv->connect_status = LBS_CONNECTED;

	/* Update current SSID and BSSID */
	memcpy(&priv->curbssparams.ssid, &bss->ssid, IW_ESSID_MAX_SIZE);
	priv->curbssparams.ssid_len = bss->ssid_len;
	memcpy(priv->curbssparams.bssid, bss->bssid, ETH_ALEN);

	lbs_deb_assoc("ASSOC_RESP: currentpacketfilter is 0x%x\n",
		priv->currentpacketfilter);

	priv->SNR[TYPE_RXPD][TYPE_AVG] = 0;
	priv->NF[TYPE_RXPD][TYPE_AVG] = 0;

	memset(priv->rawSNR, 0x00, sizeof(priv->rawSNR));
	memset(priv->rawNF, 0x00, sizeof(priv->rawNF));
	priv->nextSNRNF = 0;
	priv->numSNRNF = 0;

	netif_carrier_on(priv->dev);
	if (!priv->tx_pending_len)
		netif_wake_queue(priv->dev);

	memcpy(wrqu.ap_addr.sa_data, priv->curbssparams.bssid, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);

done:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

int lbs_ret_80211_disassociate(struct lbs_private *priv,
				 struct cmd_ds_command *resp)
{
	lbs_deb_enter(LBS_DEB_JOIN);

	lbs_mac_event_disconnected(priv);

	lbs_deb_leave(LBS_DEB_JOIN);
	return 0;
}

int lbs_ret_80211_ad_hoc_start(struct lbs_private *priv,
				 struct cmd_ds_command *resp)
{
	int ret = 0;
	u16 command = le16_to_cpu(resp->command);
	u16 result = le16_to_cpu(resp->result);
	struct cmd_ds_802_11_ad_hoc_result *padhocresult;
	union iwreq_data wrqu;
	struct bss_descriptor *bss;
	DECLARE_MAC_BUF(mac);

	lbs_deb_enter(LBS_DEB_JOIN);

	padhocresult = &resp->params.result;

	lbs_deb_join("ADHOC_RESP: size = %d\n", le16_to_cpu(resp->size));
	lbs_deb_join("ADHOC_RESP: command = %x\n", command);
	lbs_deb_join("ADHOC_RESP: result = %x\n", result);

	if (!priv->in_progress_assoc_req) {
		lbs_deb_join("ADHOC_RESP: no in-progress association request\n");
		ret = -1;
		goto done;
	}
	bss = &priv->in_progress_assoc_req->bss;

	/*
	 * Join result code 0 --> SUCCESS
	 */
	if (result) {
		lbs_deb_join("ADHOC_RESP: failed\n");
		if (priv->connect_status == LBS_CONNECTED) {
			lbs_mac_event_disconnected(priv);
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
	priv->connect_status = LBS_CONNECTED;

	if (command == CMD_RET(CMD_802_11_AD_HOC_START)) {
		/* Update the created network descriptor with the new BSSID */
		memcpy(bss->bssid, padhocresult->bssid, ETH_ALEN);
	}

	/* Set the BSSID from the joined/started descriptor */
	memcpy(&priv->curbssparams.bssid, bss->bssid, ETH_ALEN);

	/* Set the new SSID to current SSID */
	memcpy(&priv->curbssparams.ssid, &bss->ssid, IW_ESSID_MAX_SIZE);
	priv->curbssparams.ssid_len = bss->ssid_len;

	netif_carrier_on(priv->dev);
	if (!priv->tx_pending_len)
		netif_wake_queue(priv->dev);

	memset(&wrqu, 0, sizeof(wrqu));
	memcpy(wrqu.ap_addr.sa_data, priv->curbssparams.bssid, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);

	lbs_deb_join("ADHOC_RESP: - Joined/Started Ad Hoc\n");
	lbs_deb_join("ADHOC_RESP: channel = %d\n", priv->curbssparams.channel);
	lbs_deb_join("ADHOC_RESP: BSSID = %s\n",
		     print_mac(mac, padhocresult->bssid));

done:
	lbs_deb_leave_args(LBS_DEB_JOIN, "ret %d", ret);
	return ret;
}

int lbs_ret_80211_ad_hoc_stop(struct lbs_private *priv,
				struct cmd_ds_command *resp)
{
	lbs_deb_enter(LBS_DEB_JOIN);

	lbs_mac_event_disconnected(priv);

	lbs_deb_leave(LBS_DEB_JOIN);
	return 0;
}
