/**
  * This file contains functions for 802.11D.
  */
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/wireless.h>

#include "host.h"
#include "decl.h"
#include "11d.h"
#include "dev.h"
#include "wext.h"

#define TX_PWR_DEFAULT	10

static struct region_code_mapping region_code_mapping[] = {
	{"US ", 0x10},		/* US FCC      */
	{"CA ", 0x10},		/* IC Canada   */
	{"SG ", 0x10},		/* Singapore   */
	{"EU ", 0x30},		/* ETSI        */
	{"AU ", 0x30},		/* Australia   */
	{"KR ", 0x30},		/* Republic Of Korea */
	{"ES ", 0x31},		/* Spain       */
	{"FR ", 0x32},		/* France      */
	{"JP ", 0x40},		/* Japan       */
};

/* Following 2 structure defines the supported channels */
static struct chan_freq_power channel_freq_power_UN_BG[] = {
	{1, 2412, TX_PWR_DEFAULT},
	{2, 2417, TX_PWR_DEFAULT},
	{3, 2422, TX_PWR_DEFAULT},
	{4, 2427, TX_PWR_DEFAULT},
	{5, 2432, TX_PWR_DEFAULT},
	{6, 2437, TX_PWR_DEFAULT},
	{7, 2442, TX_PWR_DEFAULT},
	{8, 2447, TX_PWR_DEFAULT},
	{9, 2452, TX_PWR_DEFAULT},
	{10, 2457, TX_PWR_DEFAULT},
	{11, 2462, TX_PWR_DEFAULT},
	{12, 2467, TX_PWR_DEFAULT},
	{13, 2472, TX_PWR_DEFAULT},
	{14, 2484, TX_PWR_DEFAULT}
};

static u8 wlan_region_2_code(u8 * region)
{
	u8 i;
	u8 size = sizeof(region_code_mapping)/
		  sizeof(struct region_code_mapping);

	for (i = 0; region[i] && i < COUNTRY_CODE_LEN; i++)
		region[i] = toupper(region[i]);

	for (i = 0; i < size; i++) {
		if (!memcmp(region, region_code_mapping[i].region,
			    COUNTRY_CODE_LEN))
			return (region_code_mapping[i].code);
	}

	/* default is US */
	return (region_code_mapping[0].code);
}

static u8 *wlan_code_2_region(u8 code)
{
	u8 i;
	u8 size = sizeof(region_code_mapping)
		  / sizeof(struct region_code_mapping);
	for (i = 0; i < size; i++) {
		if (region_code_mapping[i].code == code)
			return (region_code_mapping[i].region);
	}
	/* default is US */
	return (region_code_mapping[0].region);
}

/**
 *  @brief This function finds the nrchan-th chan after the firstchan
 *  @param band       band
 *  @param firstchan  first channel number
 *  @param nrchan   number of channels
 *  @return 	      the nrchan-th chan number
*/
static u8 wlan_get_chan_11d(u8 band, u8 firstchan, u8 nrchan, u8 * chan)
/*find the nrchan-th chan after the firstchan*/
{
	u8 i;
	struct chan_freq_power *cfp;
	u8 cfp_no;

	cfp = channel_freq_power_UN_BG;
	cfp_no = sizeof(channel_freq_power_UN_BG) /
	    sizeof(struct chan_freq_power);

	for (i = 0; i < cfp_no; i++) {
		if ((cfp + i)->channel == firstchan) {
			lbs_pr_debug(1, "firstchan found\n");
			break;
		}
	}

	if (i < cfp_no) {
		/*if beyond the boundary */
		if (i + nrchan < cfp_no) {
			*chan = (cfp + i + nrchan)->channel;
			return 1;
		}
	}

	return 0;
}

/**
 *  @brief This function Checks if chan txpwr is learned from AP/IBSS
 *  @param chan                 chan number
 *  @param parsed_region_chan   pointer to parsed_region_chan_11d
 *  @return 	                TRUE; FALSE
*/
static u8 wlan_channel_known_11d(u8 chan,
			  struct parsed_region_chan_11d * parsed_region_chan)
{
	struct chan_power_11d *chanpwr = parsed_region_chan->chanpwr;
	u8 nr_chan = parsed_region_chan->nr_chan;
	u8 i = 0;

	lbs_dbg_hex("11D:parsed_region_chan:", (char *)chanpwr,
		sizeof(struct chan_power_11d) * nr_chan);

	for (i = 0; i < nr_chan; i++) {
		if (chan == chanpwr[i].chan) {
			lbs_pr_debug(1, "11D: Found Chan:%d\n", chan);
			return 1;
		}
	}

	lbs_pr_debug(1, "11D: Not Find Chan:%d\n", chan);
	return 0;
}

u32 libertas_chan_2_freq(u8 chan, u8 band)
{
	struct chan_freq_power *cf;
	u16 cnt;
	u16 i;
	u32 freq = 0;

	cf = channel_freq_power_UN_BG;
	cnt =
	    sizeof(channel_freq_power_UN_BG) /
	    sizeof(struct chan_freq_power);

	for (i = 0; i < cnt; i++) {
		if (chan == cf[i].channel)
			freq = cf[i].freq;
	}

	return freq;
}

static int generate_domain_info_11d(struct parsed_region_chan_11d
				  *parsed_region_chan,
				  struct wlan_802_11d_domain_reg * domaininfo)
{
	u8 nr_subband = 0;

	u8 nr_chan = parsed_region_chan->nr_chan;
	u8 nr_parsedchan = 0;

	u8 firstchan = 0, nextchan = 0, maxpwr = 0;

	u8 i, flag = 0;

	memcpy(domaininfo->countrycode, parsed_region_chan->countrycode,
	       COUNTRY_CODE_LEN);

	lbs_pr_debug(1, "11D:nrchan=%d\n", nr_chan);
	lbs_dbg_hex("11D:parsed_region_chan:", (char *)parsed_region_chan,
		sizeof(struct parsed_region_chan_11d));

	for (i = 0; i < nr_chan; i++) {
		if (!flag) {
			flag = 1;
			nextchan = firstchan =
			    parsed_region_chan->chanpwr[i].chan;
			maxpwr = parsed_region_chan->chanpwr[i].pwr;
			nr_parsedchan = 1;
			continue;
		}

		if (parsed_region_chan->chanpwr[i].chan == nextchan + 1 &&
		    parsed_region_chan->chanpwr[i].pwr == maxpwr) {
			nextchan++;
			nr_parsedchan++;
		} else {
			domaininfo->subband[nr_subband].firstchan = firstchan;
			domaininfo->subband[nr_subband].nrchan =
			    nr_parsedchan;
			domaininfo->subband[nr_subband].maxtxpwr = maxpwr;
			nr_subband++;
			nextchan = firstchan =
			    parsed_region_chan->chanpwr[i].chan;
			maxpwr = parsed_region_chan->chanpwr[i].pwr;
		}
	}

	if (flag) {
		domaininfo->subband[nr_subband].firstchan = firstchan;
		domaininfo->subband[nr_subband].nrchan = nr_parsedchan;
		domaininfo->subband[nr_subband].maxtxpwr = maxpwr;
		nr_subband++;
	}
	domaininfo->nr_subband = nr_subband;

	lbs_pr_debug(1, "nr_subband=%x\n", domaininfo->nr_subband);
	lbs_dbg_hex("11D:domaininfo:", (char *)domaininfo,
		COUNTRY_CODE_LEN + 1 +
		sizeof(struct ieeetypes_subbandset) * nr_subband);
	return 0;
}

/**
 *  @brief This function generates parsed_region_chan from Domain Info learned from AP/IBSS
 *  @param region_chan          pointer to struct region_channel
 *  @param *parsed_region_chan  pointer to parsed_region_chan_11d
 *  @return 	                N/A
*/
static void wlan_generate_parsed_region_chan_11d(struct region_channel * region_chan,
					  struct parsed_region_chan_11d *
					  parsed_region_chan)
{
	u8 i;
	struct chan_freq_power *cfp;

	if (region_chan == NULL) {
		lbs_pr_debug(1, "11D: region_chan is NULL\n");
		return;
	}

	cfp = region_chan->CFP;
	if (cfp == NULL) {
		lbs_pr_debug(1, "11D: cfp equal NULL \n");
		return;
	}

	parsed_region_chan->band = region_chan->band;
	parsed_region_chan->region = region_chan->region;
	memcpy(parsed_region_chan->countrycode,
	       wlan_code_2_region(region_chan->region), COUNTRY_CODE_LEN);

	lbs_pr_debug(1, "11D: region[0x%x] band[%d]\n", parsed_region_chan->region,
	       parsed_region_chan->band);

	for (i = 0; i < region_chan->nrcfp; i++, cfp++) {
		parsed_region_chan->chanpwr[i].chan = cfp->channel;
		parsed_region_chan->chanpwr[i].pwr = cfp->maxtxpower;
		lbs_pr_debug(1, "11D: Chan[%d] Pwr[%d]\n",
		       parsed_region_chan->chanpwr[i].chan,
		       parsed_region_chan->chanpwr[i].pwr);
	}
	parsed_region_chan->nr_chan = region_chan->nrcfp;

	lbs_pr_debug(1, "11D: nrchan[%d]\n", parsed_region_chan->nr_chan);

	return;
}

/**
 *  @brief generate parsed_region_chan from Domain Info learned from AP/IBSS
 *  @param region               region ID
 *  @param band                 band
 *  @param chan                 chan
 *  @return 	                TRUE;FALSE
*/
static u8 wlan_region_chan_supported_11d(u8 region, u8 band, u8 chan)
{
	struct chan_freq_power *cfp;
	int cfp_no;
	u8 idx;

	ENTER();

	cfp = libertas_get_region_cfp_table(region, band, &cfp_no);
	if (cfp == NULL)
		return 0;

	for (idx = 0; idx < cfp_no; idx++) {
		if (chan == (cfp + idx)->channel) {
			/* If Mrvl Chip Supported? */
			if ((cfp + idx)->unsupported) {
				return 0;
			} else {
				return 1;
			}
		}
	}

	/*chan is not in the region table */
	LEAVE();
	return 0;
}

/**
 *  @brief This function checks if chan txpwr is learned from AP/IBSS
 *  @param chan                 chan number
 *  @param parsed_region_chan   pointer to parsed_region_chan_11d
 *  @return 	                0
*/
static int parse_domain_info_11d(struct ieeetypes_countryinfofullset*
				 countryinfo,
				 u8 band,
				 struct parsed_region_chan_11d *
				 parsed_region_chan)
{
	u8 nr_subband, nrchan;
	u8 lastchan, firstchan;
	u8 region;
	u8 curchan = 0;

	u8 idx = 0;		/*chan index in parsed_region_chan */

	u8 j, i;

	ENTER();

	/*validation Rules:
	   1. valid region Code
	   2. First Chan increment
	   3. channel range no overlap
	   4. channel is valid?
	   5. channel is supported by region?
	   6. Others
	 */

	lbs_dbg_hex("CountryInfo:", (u8 *) countryinfo, 30);

	if ((*(countryinfo->countrycode)) == 0
	    || (countryinfo->len <= COUNTRY_CODE_LEN)) {
		/* No region Info or Wrong region info: treat as No 11D info */
		LEAVE();
		return 0;
	}

	/*Step1: check region_code */
	parsed_region_chan->region = region =
	    wlan_region_2_code(countryinfo->countrycode);

	lbs_pr_debug(1, "regioncode=%x\n", (u8) parsed_region_chan->region);
	lbs_dbg_hex("CountryCode:", (char *)countryinfo->countrycode,
		COUNTRY_CODE_LEN);

	parsed_region_chan->band = band;

	memcpy(parsed_region_chan->countrycode, countryinfo->countrycode,
	       COUNTRY_CODE_LEN);

	nr_subband = (countryinfo->len - COUNTRY_CODE_LEN) /
	    sizeof(struct ieeetypes_subbandset);

	for (j = 0, lastchan = 0; j < nr_subband; j++) {

		if (countryinfo->subband[j].firstchan <= lastchan) {
			/*Step2&3. Check First Chan Num increment and no overlap */
			lbs_pr_debug(1, "11D: Chan[%d>%d] Overlap\n",
			       countryinfo->subband[j].firstchan, lastchan);
			continue;
		}

		firstchan = countryinfo->subband[j].firstchan;
		nrchan = countryinfo->subband[j].nrchan;

		for (i = 0; idx < MAX_NO_OF_CHAN && i < nrchan; i++) {
			/*step4: channel is supported? */

			if (!wlan_get_chan_11d(band, firstchan, i, &curchan)) {
				/* Chan is not found in UN table */
				lbs_pr_debug(1, "chan is not supported: %d \n", i);
				break;
			}

			lastchan = curchan;

			if (wlan_region_chan_supported_11d
			    (region, band, curchan)) {
				/*step5: Check if curchan is supported by mrvl in region */
				parsed_region_chan->chanpwr[idx].chan = curchan;
				parsed_region_chan->chanpwr[idx].pwr =
				    countryinfo->subband[j].maxtxpwr;
				idx++;
			} else {
				/*not supported and ignore the chan */
				lbs_pr_debug(1,
				       "11D:i[%d] chan[%d] unsupported in region[%x] band[%d]\n",
				       i, curchan, region, band);
			}
		}

		/*Step6: Add other checking if any */

	}

	parsed_region_chan->nr_chan = idx;

	lbs_pr_debug(1, "nrchan=%x\n", parsed_region_chan->nr_chan);
	lbs_dbg_hex("11D:parsed_region_chan:", (u8 *) parsed_region_chan,
		2 + COUNTRY_CODE_LEN + sizeof(struct parsed_region_chan_11d) * idx);

	LEAVE();
	return 0;
}

/**
 *  @brief This function calculates the scan type for channels
 *  @param chan                 chan number
 *  @param parsed_region_chan   pointer to parsed_region_chan_11d
 *  @return 	                PASSIVE if chan is unknown; ACTIVE if chan is known
*/
u8 libertas_get_scan_type_11d(u8 chan,
			  struct parsed_region_chan_11d * parsed_region_chan)
{
	u8 scan_type = cmd_scan_type_passive;

	ENTER();

	if (wlan_channel_known_11d(chan, parsed_region_chan)) {
		lbs_pr_debug(1, "11D: Found and do Active Scan\n");
		scan_type = cmd_scan_type_active;
	} else {
		lbs_pr_debug(1, "11D: Not Find and do Passive Scan\n");
	}

	LEAVE();
	return scan_type;

}

void libertas_init_11d(wlan_private * priv)
{
	priv->adapter->enable11d = 0;
	memset(&(priv->adapter->parsed_region_chan), 0,
	       sizeof(struct parsed_region_chan_11d));
	return;
}

static int wlan_enable_11d(wlan_private * priv, u8 flag)
{
	int ret;

	priv->adapter->enable11d = flag;

	/* send cmd to FW to enable/disable 11D function in FW */
	ret = libertas_prepare_and_send_command(priv,
				    cmd_802_11_snmp_mib,
				    cmd_act_set,
				    cmd_option_waitforrsp,
				    OID_802_11D_ENABLE,
				    &priv->adapter->enable11d);
	if (ret)
		lbs_pr_debug(1, "11D: Fail to enable 11D \n");

	return 0;
}

/**
 *  @brief This function sets DOMAIN INFO to FW
 *  @param priv       pointer to wlan_private
 *  @return 	      0; -1
*/
static int set_domain_info_11d(wlan_private * priv)
{
	int ret;

	if (!priv->adapter->enable11d) {
		lbs_pr_debug(1, "11D: dnld domain Info with 11d disabled\n");
		return 0;
	}

	ret = libertas_prepare_and_send_command(priv, cmd_802_11d_domain_info,
				    cmd_act_set,
				    cmd_option_waitforrsp, 0, NULL);
	if (ret)
		lbs_pr_debug(1, "11D: Fail to dnld domain Info\n");

	return ret;
}

/**
 *  @brief This function setups scan channels
 *  @param priv       pointer to wlan_private
 *  @param band       band
 *  @return 	      0
*/
int libertas_set_universaltable(wlan_private * priv, u8 band)
{
	wlan_adapter *adapter = priv->adapter;
	u16 size = sizeof(struct chan_freq_power);
	u16 i = 0;

	memset(adapter->universal_channel, 0,
	       sizeof(adapter->universal_channel));

	adapter->universal_channel[i].nrcfp =
	    sizeof(channel_freq_power_UN_BG) / size;
	lbs_pr_debug(1, "11D: BG-band nrcfp=%d\n",
	       adapter->universal_channel[i].nrcfp);

	adapter->universal_channel[i].CFP = channel_freq_power_UN_BG;
	adapter->universal_channel[i].valid = 1;
	adapter->universal_channel[i].region = UNIVERSAL_REGION_CODE;
	adapter->universal_channel[i].band = band;
	i++;

	return 0;
}

/**
 *  @brief This function implements command CMD_802_11D_DOMAIN_INFO
 *  @param priv       pointer to wlan_private
 *  @param cmd        pointer to cmd buffer
 *  @param cmdno      cmd ID
 *  @param cmdOption  cmd action
 *  @return 	      0
*/
int libertas_cmd_802_11d_domain_info(wlan_private * priv,
				 struct cmd_ds_command *cmd, u16 cmdno,
				 u16 cmdoption)
{
	struct cmd_ds_802_11d_domain_info *pdomaininfo =
	    &cmd->params.domaininfo;
	struct mrvlietypes_domainparamset *domain = &pdomaininfo->domain;
	wlan_adapter *adapter = priv->adapter;
	u8 nr_subband = adapter->domainreg.nr_subband;

	ENTER();

	lbs_pr_debug(1, "nr_subband=%x\n", nr_subband);

	cmd->command = cpu_to_le16(cmdno);
	pdomaininfo->action = cpu_to_le16(cmdoption);
	if (cmdoption == cmd_act_get) {
		cmd->size =
		    cpu_to_le16(sizeof(pdomaininfo->action) + S_DS_GEN);
		lbs_dbg_hex("11D: 802_11D_DOMAIN_INFO:", (u8 *) cmd,
			(int)(cmd->size));
		LEAVE();
		return 0;
	}

	domain->header.type = cpu_to_le16(TLV_TYPE_DOMAIN);
	memcpy(domain->countrycode, adapter->domainreg.countrycode,
	       sizeof(domain->countrycode));

	domain->header.len =
	    cpu_to_le16(nr_subband * sizeof(struct ieeetypes_subbandset) +
			     sizeof(domain->countrycode));

	if (nr_subband) {
		memcpy(domain->subband, adapter->domainreg.subband,
		       nr_subband * sizeof(struct ieeetypes_subbandset));

		cmd->size = cpu_to_le16(sizeof(pdomaininfo->action) +
					     domain->header.len +
					     sizeof(struct mrvlietypesheader) +
					     S_DS_GEN);
	} else {
		cmd->size =
		    cpu_to_le16(sizeof(pdomaininfo->action) + S_DS_GEN);
	}

	lbs_dbg_hex("11D:802_11D_DOMAIN_INFO:", (u8 *) cmd, (int)(cmd->size));

	LEAVE();

	return 0;
}

/**
 *  @brief This function implements private cmd: enable/disable 11D
 *  @param priv    pointer to wlan_private
 *  @param wrq     pointer to user data
 *  @return 	   0 or -1
 */
int libertas_cmd_enable_11d(wlan_private * priv, struct iwreq *wrq)
{
	int data = 0;
	int *val;

	ENTER();
	data = SUBCMD_DATA(wrq);

	lbs_pr_debug(1, "enable 11D: %s\n",
	       (data == 1) ? "enable" : "Disable");

	wlan_enable_11d(priv, data);
	val = (int *)wrq->u.name;
	*val = priv->adapter->enable11d;

	LEAVE();
	return 0;
}

/**
 *  @brief This function parses countryinfo from AP and download country info to FW
 *  @param priv    pointer to wlan_private
 *  @param resp    pointer to command response buffer
 *  @return 	   0; -1
 */
int libertas_ret_802_11d_domain_info(wlan_private * priv,
				 struct cmd_ds_command *resp)
{
	struct cmd_ds_802_11d_domain_info
	*domaininfo = &resp->params.domaininforesp;
	struct mrvlietypes_domainparamset *domain = &domaininfo->domain;
	u16 action = le16_to_cpu(domaininfo->action);
	s16 ret = 0;
	u8 nr_subband = 0;

	ENTER();

	lbs_dbg_hex("11D DOMAIN Info Rsp Data:", (u8 *) resp,
		(int)le16_to_cpu(resp->size));

	nr_subband = (domain->header.len - 3) / sizeof(struct ieeetypes_subbandset);
	/* countrycode 3 bytes */

	lbs_pr_debug(1, "11D Domain Info Resp: nr_subband=%d\n", nr_subband);

	if (nr_subband > MRVDRV_MAX_SUBBAND_802_11D) {
		lbs_pr_debug(1, "Invalid Numrer of Subband returned!!\n");
		return -1;
	}

	switch (action) {
	case cmd_act_set:	/*Proc Set action */
		break;

	case cmd_act_get:
		break;
	default:
		lbs_pr_debug(1, "Invalid action:%d\n", domaininfo->action);
		ret = -1;
		break;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief This function parses countryinfo from AP and download country info to FW
 *  @param priv    pointer to wlan_private
 *  @return 	   0; -1
 */
int libertas_parse_dnld_countryinfo_11d(wlan_private * priv)
{
	int ret;
	wlan_adapter *adapter = priv->adapter;

	ENTER();
	if (priv->adapter->enable11d) {
		memset(&adapter->parsed_region_chan, 0,
		       sizeof(struct parsed_region_chan_11d));
		ret = parse_domain_info_11d(&adapter->pattemptedbssdesc->
					       countryinfo, 0,
					       &adapter->parsed_region_chan);

		if (ret == -1) {
			lbs_pr_debug(1, "11D: Err Parse domain_info from AP..\n");
			LEAVE();
			return ret;
		}

		memset(&adapter->domainreg, 0,
		       sizeof(struct wlan_802_11d_domain_reg));
		generate_domain_info_11d(&adapter->parsed_region_chan,
				      &adapter->domainreg);

		ret = set_domain_info_11d(priv);

		if (ret) {
			lbs_pr_debug(1, "11D: Err set domainInfo to FW\n");
			LEAVE();
			return ret;
		}
	}
	LEAVE();
	return 0;
}

/**
 *  @brief This function generates 11D info from user specified regioncode and download to FW
 *  @param priv    pointer to wlan_private
 *  @return 	   0; -1
 */
int libertas_create_dnld_countryinfo_11d(wlan_private * priv)
{
	int ret;
	wlan_adapter *adapter = priv->adapter;
	struct region_channel *region_chan;
	u8 j;

	ENTER();
	lbs_pr_debug(1, "11D:curbssparams.band[%d]\n", adapter->curbssparams.band);

	if (priv->adapter->enable11d) {
		/* update parsed_region_chan_11; dnld domaininf to FW */

		for (j = 0; j < sizeof(adapter->region_channel) /
		     sizeof(adapter->region_channel[0]); j++) {
			region_chan = &adapter->region_channel[j];

			lbs_pr_debug(1, "11D:[%d] region_chan->band[%d]\n", j,
			       region_chan->band);

			if (!region_chan || !region_chan->valid
			    || !region_chan->CFP)
				continue;
			if (region_chan->band != adapter->curbssparams.band)
				continue;
			break;
		}

		if (j >= sizeof(adapter->region_channel) /
		    sizeof(adapter->region_channel[0])) {
			lbs_pr_debug(1, "11D:region_chan not found. band[%d]\n",
			       adapter->curbssparams.band);
			LEAVE();
			return -1;
		}

		memset(&adapter->parsed_region_chan, 0,
		       sizeof(struct parsed_region_chan_11d));
		wlan_generate_parsed_region_chan_11d(region_chan,
						     &adapter->
						     parsed_region_chan);

		memset(&adapter->domainreg, 0,
		       sizeof(struct wlan_802_11d_domain_reg));
		generate_domain_info_11d(&adapter->parsed_region_chan,
					 &adapter->domainreg);

		ret = set_domain_info_11d(priv);

		if (ret) {
			lbs_pr_debug(1, "11D: Err set domainInfo to FW\n");
			LEAVE();
			return ret;
		}

	}

	LEAVE();
	return 0;
}
