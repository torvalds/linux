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

static u8 lbs_region_2_code(u8 *region)
{
	u8 i;

	for (i = 0; region[i] && i < COUNTRY_CODE_LEN; i++)
		region[i] = toupper(region[i]);

	for (i = 0; i < ARRAY_SIZE(region_code_mapping); i++) {
		if (!memcmp(region, region_code_mapping[i].region,
			    COUNTRY_CODE_LEN))
			return (region_code_mapping[i].code);
	}

	/* default is US */
	return (region_code_mapping[0].code);
}

static u8 *lbs_code_2_region(u8 code)
{
	u8 i;

	for (i = 0; i < ARRAY_SIZE(region_code_mapping); i++) {
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
static u8 lbs_get_chan_11d(u8 firstchan, u8 nrchan, u8 *chan)
/*find the nrchan-th chan after the firstchan*/
{
	u8 i;
	struct chan_freq_power *cfp;
	u8 cfp_no;

	cfp = channel_freq_power_UN_BG;
	cfp_no = ARRAY_SIZE(channel_freq_power_UN_BG);

	for (i = 0; i < cfp_no; i++) {
		if ((cfp + i)->channel == firstchan) {
			lbs_deb_11d("firstchan found\n");
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
static u8 lbs_channel_known_11d(u8 chan,
			  struct parsed_region_chan_11d * parsed_region_chan)
{
	struct chan_power_11d *chanpwr = parsed_region_chan->chanpwr;
	u8 nr_chan = parsed_region_chan->nr_chan;
	u8 i = 0;

	lbs_deb_hex(LBS_DEB_11D, "parsed_region_chan", (char *)chanpwr,
		sizeof(struct chan_power_11d) * nr_chan);

	for (i = 0; i < nr_chan; i++) {
		if (chan == chanpwr[i].chan) {
			lbs_deb_11d("found chan %d\n", chan);
			return 1;
		}
	}

	lbs_deb_11d("chan %d not found\n", chan);
	return 0;
}

u32 lbs_chan_2_freq(u8 chan)
{
	struct chan_freq_power *cf;
	u16 i;
	u32 freq = 0;

	cf = channel_freq_power_UN_BG;

	for (i = 0; i < ARRAY_SIZE(channel_freq_power_UN_BG); i++) {
		if (chan == cf[i].channel)
			freq = cf[i].freq;
	}

	return freq;
}

static int generate_domain_info_11d(struct parsed_region_chan_11d
				  *parsed_region_chan,
				  struct lbs_802_11d_domain_reg *domaininfo)
{
	u8 nr_subband = 0;

	u8 nr_chan = parsed_region_chan->nr_chan;
	u8 nr_parsedchan = 0;

	u8 firstchan = 0, nextchan = 0, maxpwr = 0;

	u8 i, flag = 0;

	memcpy(domaininfo->countrycode, parsed_region_chan->countrycode,
	       COUNTRY_CODE_LEN);

	lbs_deb_11d("nrchan %d\n", nr_chan);
	lbs_deb_hex(LBS_DEB_11D, "parsed_region_chan", (char *)parsed_region_chan,
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

	lbs_deb_11d("nr_subband=%x\n", domaininfo->nr_subband);
	lbs_deb_hex(LBS_DEB_11D, "domaininfo", (char *)domaininfo,
		COUNTRY_CODE_LEN + 1 +
		sizeof(struct ieee_subbandset) * nr_subband);
	return 0;
}

/**
 *  @brief This function generates parsed_region_chan from Domain Info learned from AP/IBSS
 *  @param region_chan          pointer to struct region_channel
 *  @param *parsed_region_chan  pointer to parsed_region_chan_11d
 *  @return 	                N/A
*/
static void lbs_generate_parsed_region_chan_11d(struct region_channel *region_chan,
					  struct parsed_region_chan_11d *
					  parsed_region_chan)
{
	u8 i;
	struct chan_freq_power *cfp;

	if (region_chan == NULL) {
		lbs_deb_11d("region_chan is NULL\n");
		return;
	}

	cfp = region_chan->CFP;
	if (cfp == NULL) {
		lbs_deb_11d("cfp is NULL \n");
		return;
	}

	parsed_region_chan->band = region_chan->band;
	parsed_region_chan->region = region_chan->region;
	memcpy(parsed_region_chan->countrycode,
	       lbs_code_2_region(region_chan->region), COUNTRY_CODE_LEN);

	lbs_deb_11d("region 0x%x, band %d\n", parsed_region_chan->region,
	       parsed_region_chan->band);

	for (i = 0; i < region_chan->nrcfp; i++, cfp++) {
		parsed_region_chan->chanpwr[i].chan = cfp->channel;
		parsed_region_chan->chanpwr[i].pwr = cfp->maxtxpower;
		lbs_deb_11d("chan %d, pwr %d\n",
		       parsed_region_chan->chanpwr[i].chan,
		       parsed_region_chan->chanpwr[i].pwr);
	}
	parsed_region_chan->nr_chan = region_chan->nrcfp;

	lbs_deb_11d("nrchan %d\n", parsed_region_chan->nr_chan);

	return;
}

/**
 *  @brief generate parsed_region_chan from Domain Info learned from AP/IBSS
 *  @param region               region ID
 *  @param band                 band
 *  @param chan                 chan
 *  @return 	                TRUE;FALSE
*/
static u8 lbs_region_chan_supported_11d(u8 region, u8 chan)
{
	struct chan_freq_power *cfp;
	int cfp_no;
	u8 idx;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_11D);

	cfp = lbs_get_region_cfp_table(region, &cfp_no);
	if (cfp == NULL)
		return 0;

	for (idx = 0; idx < cfp_no; idx++) {
		if (chan == (cfp + idx)->channel) {
			/* If Mrvl Chip Supported? */
			if ((cfp + idx)->unsupported) {
				ret = 0;
			} else {
				ret = 1;
			}
			goto done;
		}
	}

	/*chan is not in the region table */

done:
	lbs_deb_leave_args(LBS_DEB_11D, "ret %d", ret);
	return ret;
}

/**
 *  @brief This function checks if chan txpwr is learned from AP/IBSS
 *  @param chan                 chan number
 *  @param parsed_region_chan   pointer to parsed_region_chan_11d
 *  @return 	                0
*/
static int parse_domain_info_11d(struct ieee_ie_country_info_full_set *countryinfo,
				 u8 band,
				 struct parsed_region_chan_11d *parsed_region_chan)
{
	u8 nr_subband, nrchan;
	u8 lastchan, firstchan;
	u8 region;
	u8 curchan = 0;

	u8 idx = 0;		/*chan index in parsed_region_chan */

	u8 j, i;

	lbs_deb_enter(LBS_DEB_11D);

	/*validation Rules:
	   1. valid region Code
	   2. First Chan increment
	   3. channel range no overlap
	   4. channel is valid?
	   5. channel is supported by region?
	   6. Others
	 */

	lbs_deb_hex(LBS_DEB_11D, "countryinfo", (u8 *) countryinfo, 30);

	if ((*(countryinfo->countrycode)) == 0
	    || (countryinfo->header.len <= COUNTRY_CODE_LEN)) {
		/* No region Info or Wrong region info: treat as No 11D info */
		goto done;
	}

	/*Step1: check region_code */
	parsed_region_chan->region = region =
	    lbs_region_2_code(countryinfo->countrycode);

	lbs_deb_11d("regioncode=%x\n", (u8) parsed_region_chan->region);
	lbs_deb_hex(LBS_DEB_11D, "countrycode", (char *)countryinfo->countrycode,
		COUNTRY_CODE_LEN);

	parsed_region_chan->band = band;

	memcpy(parsed_region_chan->countrycode, countryinfo->countrycode,
	       COUNTRY_CODE_LEN);

	nr_subband = (countryinfo->header.len - COUNTRY_CODE_LEN) /
	    sizeof(struct ieee_subbandset);

	for (j = 0, lastchan = 0; j < nr_subband; j++) {

		if (countryinfo->subband[j].firstchan <= lastchan) {
			/*Step2&3. Check First Chan Num increment and no overlap */
			lbs_deb_11d("chan %d>%d, overlap\n",
			       countryinfo->subband[j].firstchan, lastchan);
			continue;
		}

		firstchan = countryinfo->subband[j].firstchan;
		nrchan = countryinfo->subband[j].nrchan;

		for (i = 0; idx < MAX_NO_OF_CHAN && i < nrchan; i++) {
			/*step4: channel is supported? */

			if (!lbs_get_chan_11d(firstchan, i, &curchan)) {
				/* Chan is not found in UN table */
				lbs_deb_11d("chan is not supported: %d \n", i);
				break;
			}

			lastchan = curchan;

			if (lbs_region_chan_supported_11d(region, curchan)) {
				/*step5: Check if curchan is supported by mrvl in region */
				parsed_region_chan->chanpwr[idx].chan = curchan;
				parsed_region_chan->chanpwr[idx].pwr =
				    countryinfo->subband[j].maxtxpwr;
				idx++;
			} else {
				/*not supported and ignore the chan */
				lbs_deb_11d(
				       "i %d, chan %d unsupported in region %x, band %d\n",
				       i, curchan, region, band);
			}
		}

		/*Step6: Add other checking if any */

	}

	parsed_region_chan->nr_chan = idx;

	lbs_deb_11d("nrchan=%x\n", parsed_region_chan->nr_chan);
	lbs_deb_hex(LBS_DEB_11D, "parsed_region_chan", (u8 *) parsed_region_chan,
		2 + COUNTRY_CODE_LEN + sizeof(struct parsed_region_chan_11d) * idx);

done:
	lbs_deb_enter(LBS_DEB_11D);
	return 0;
}

/**
 *  @brief This function calculates the scan type for channels
 *  @param chan                 chan number
 *  @param parsed_region_chan   pointer to parsed_region_chan_11d
 *  @return 	                PASSIVE if chan is unknown; ACTIVE if chan is known
*/
u8 lbs_get_scan_type_11d(u8 chan,
			  struct parsed_region_chan_11d * parsed_region_chan)
{
	u8 scan_type = CMD_SCAN_TYPE_PASSIVE;

	lbs_deb_enter(LBS_DEB_11D);

	if (lbs_channel_known_11d(chan, parsed_region_chan)) {
		lbs_deb_11d("found, do active scan\n");
		scan_type = CMD_SCAN_TYPE_ACTIVE;
	} else {
		lbs_deb_11d("not found, do passive scan\n");
	}

	lbs_deb_leave_args(LBS_DEB_11D, "ret scan_type %d", scan_type);
	return scan_type;

}

void lbs_init_11d(struct lbs_private *priv)
{
	priv->enable11d = 0;
	memset(&(priv->parsed_region_chan), 0,
	       sizeof(struct parsed_region_chan_11d));
	return;
}

/**
 *  @brief This function sets DOMAIN INFO to FW
 *  @param priv       pointer to struct lbs_private
 *  @return 	      0; -1
*/
static int set_domain_info_11d(struct lbs_private *priv)
{
	int ret;

	if (!priv->enable11d) {
		lbs_deb_11d("dnld domain Info with 11d disabled\n");
		return 0;
	}

	ret = lbs_prepare_and_send_command(priv, CMD_802_11D_DOMAIN_INFO,
				    CMD_ACT_SET,
				    CMD_OPTION_WAITFORRSP, 0, NULL);
	if (ret)
		lbs_deb_11d("fail to dnld domain info\n");

	return ret;
}

/**
 *  @brief This function setups scan channels
 *  @param priv       pointer to struct lbs_private
 *  @param band       band
 *  @return 	      0
*/
int lbs_set_universaltable(struct lbs_private *priv, u8 band)
{
	u16 size = sizeof(struct chan_freq_power);
	u16 i = 0;

	memset(priv->universal_channel, 0,
	       sizeof(priv->universal_channel));

	priv->universal_channel[i].nrcfp =
	    sizeof(channel_freq_power_UN_BG) / size;
	lbs_deb_11d("BG-band nrcfp %d\n",
	       priv->universal_channel[i].nrcfp);

	priv->universal_channel[i].CFP = channel_freq_power_UN_BG;
	priv->universal_channel[i].valid = 1;
	priv->universal_channel[i].region = UNIVERSAL_REGION_CODE;
	priv->universal_channel[i].band = band;
	i++;

	return 0;
}

/**
 *  @brief This function implements command CMD_802_11D_DOMAIN_INFO
 *  @param priv       pointer to struct lbs_private
 *  @param cmd        pointer to cmd buffer
 *  @param cmdno      cmd ID
 *  @param cmdOption  cmd action
 *  @return 	      0
*/
int lbs_cmd_802_11d_domain_info(struct lbs_private *priv,
				 struct cmd_ds_command *cmd, u16 cmdno,
				 u16 cmdoption)
{
	struct cmd_ds_802_11d_domain_info *pdomaininfo =
	    &cmd->params.domaininfo;
	struct mrvl_ie_domain_param_set *domain = &pdomaininfo->domain;
	u8 nr_subband = priv->domainreg.nr_subband;

	lbs_deb_enter(LBS_DEB_11D);

	lbs_deb_11d("nr_subband=%x\n", nr_subband);

	cmd->command = cpu_to_le16(cmdno);
	pdomaininfo->action = cpu_to_le16(cmdoption);
	if (cmdoption == CMD_ACT_GET) {
		cmd->size =
		    cpu_to_le16(sizeof(pdomaininfo->action) + S_DS_GEN);
		lbs_deb_hex(LBS_DEB_11D, "802_11D_DOMAIN_INFO", (u8 *) cmd,
			le16_to_cpu(cmd->size));
		goto done;
	}

	domain->header.type = cpu_to_le16(TLV_TYPE_DOMAIN);
	memcpy(domain->countrycode, priv->domainreg.countrycode,
	       sizeof(domain->countrycode));

	domain->header.len =
	    cpu_to_le16(nr_subband * sizeof(struct ieee_subbandset) +
			     sizeof(domain->countrycode));

	if (nr_subband) {
		memcpy(domain->subband, priv->domainreg.subband,
		       nr_subband * sizeof(struct ieee_subbandset));

		cmd->size = cpu_to_le16(sizeof(pdomaininfo->action) +
					     le16_to_cpu(domain->header.len) +
					     sizeof(struct mrvl_ie_header) +
					     S_DS_GEN);
	} else {
		cmd->size =
		    cpu_to_le16(sizeof(pdomaininfo->action) + S_DS_GEN);
	}

	lbs_deb_hex(LBS_DEB_11D, "802_11D_DOMAIN_INFO", (u8 *) cmd, le16_to_cpu(cmd->size));

done:
	lbs_deb_enter(LBS_DEB_11D);
	return 0;
}

/**
 *  @brief This function parses countryinfo from AP and download country info to FW
 *  @param priv    pointer to struct lbs_private
 *  @param resp    pointer to command response buffer
 *  @return 	   0; -1
 */
int lbs_ret_802_11d_domain_info(struct cmd_ds_command *resp)
{
	struct cmd_ds_802_11d_domain_info *domaininfo = &resp->params.domaininforesp;
	struct mrvl_ie_domain_param_set *domain = &domaininfo->domain;
	u16 action = le16_to_cpu(domaininfo->action);
	s16 ret = 0;
	u8 nr_subband = 0;

	lbs_deb_enter(LBS_DEB_11D);

	lbs_deb_hex(LBS_DEB_11D, "domain info resp", (u8 *) resp,
		(int)le16_to_cpu(resp->size));

	nr_subband = (le16_to_cpu(domain->header.len) - COUNTRY_CODE_LEN) /
		      sizeof(struct ieee_subbandset);

	lbs_deb_11d("domain info resp: nr_subband %d\n", nr_subband);

	if (nr_subband > MRVDRV_MAX_SUBBAND_802_11D) {
		lbs_deb_11d("Invalid Numrer of Subband returned!!\n");
		return -1;
	}

	switch (action) {
	case CMD_ACT_SET:	/*Proc Set action */
		break;

	case CMD_ACT_GET:
		break;
	default:
		lbs_deb_11d("Invalid action:%d\n", domaininfo->action);
		ret = -1;
		break;
	}

	lbs_deb_leave_args(LBS_DEB_11D, "ret %d", ret);
	return ret;
}

/**
 *  @brief This function parses countryinfo from AP and download country info to FW
 *  @param priv    pointer to struct lbs_private
 *  @return 	   0; -1
 */
int lbs_parse_dnld_countryinfo_11d(struct lbs_private *priv,
                                        struct bss_descriptor * bss)
{
	int ret;

	lbs_deb_enter(LBS_DEB_11D);
	if (priv->enable11d) {
		memset(&priv->parsed_region_chan, 0,
		       sizeof(struct parsed_region_chan_11d));
		ret = parse_domain_info_11d(&bss->countryinfo, 0,
					       &priv->parsed_region_chan);

		if (ret == -1) {
			lbs_deb_11d("error parsing domain_info from AP\n");
			goto done;
		}

		memset(&priv->domainreg, 0,
		       sizeof(struct lbs_802_11d_domain_reg));
		generate_domain_info_11d(&priv->parsed_region_chan,
				      &priv->domainreg);

		ret = set_domain_info_11d(priv);

		if (ret) {
			lbs_deb_11d("error setting domain info\n");
			goto done;
		}
	}
	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_11D, "ret %d", ret);
	return ret;
}

/**
 *  @brief This function generates 11D info from user specified regioncode and download to FW
 *  @param priv    pointer to struct lbs_private
 *  @return 	   0; -1
 */
int lbs_create_dnld_countryinfo_11d(struct lbs_private *priv)
{
	int ret;
	struct region_channel *region_chan;
	u8 j;

	lbs_deb_enter(LBS_DEB_11D);
	lbs_deb_11d("curbssparams.band %d\n", priv->curbssparams.band);

	if (priv->enable11d) {
		/* update parsed_region_chan_11; dnld domaininf to FW */

		for (j = 0; j < ARRAY_SIZE(priv->region_channel); j++) {
			region_chan = &priv->region_channel[j];

			lbs_deb_11d("%d region_chan->band %d\n", j,
			       region_chan->band);

			if (!region_chan || !region_chan->valid
			    || !region_chan->CFP)
				continue;
			if (region_chan->band != priv->curbssparams.band)
				continue;
			break;
		}

		if (j >= ARRAY_SIZE(priv->region_channel)) {
			lbs_deb_11d("region_chan not found, band %d\n",
			       priv->curbssparams.band);
			ret = -1;
			goto done;
		}

		memset(&priv->parsed_region_chan, 0,
		       sizeof(struct parsed_region_chan_11d));
		lbs_generate_parsed_region_chan_11d(region_chan,
						     &priv->
						     parsed_region_chan);

		memset(&priv->domainreg, 0,
		       sizeof(struct lbs_802_11d_domain_reg));
		generate_domain_info_11d(&priv->parsed_region_chan,
					 &priv->domainreg);

		ret = set_domain_info_11d(priv);

		if (ret) {
			lbs_deb_11d("error setting domain info\n");
			goto done;
		}

	}
	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_11D, "ret %d", ret);
	return ret;
}
