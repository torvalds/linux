/** @file mlan_11d.c
 *
 *  @brief This file contains functions for 802.11D.
 *
 *  Copyright (C) 2008-2017, Marvell International Ltd.
 *
 *  This software file (the "File") is distributed by Marvell International
 *  Ltd. under the terms of the GNU General Public License Version 2, June 1991
 *  (the "License").  You may use, redistribute and/or modify this File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 *
 */
/********************************************************
Change log:
    10/21/2008: initial version
********************************************************/

#include "mlan.h"
#include "mlan_join.h"
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_11h.h"

/********************************************************
			Local Variables
********************************************************/

#ifdef STA_SUPPORT
/** Region code mapping */
typedef struct _region_code_mapping {
    /** Region */
	t_u8 region[COUNTRY_CODE_LEN];
    /** Code */
	t_u8 code;
} region_code_mapping_t;

/** Region code mapping table */
static region_code_mapping_t region_code_mapping[] = {
	{"US ", 0x10},		/* US FCC      */
	{"CA ", 0x20},		/* IC Canada   */
	{"SG ", 0x10},		/* Singapore   */
	{"EU ", 0x30},		/* ETSI        */
	{"AU ", 0x30},		/* Australia   */
	{"KR ", 0x30},		/* Republic Of Korea */
	{"FR ", 0x32},		/* France      */
	{"JP ", 0x40},		/* Japan       */
	{"JP ", 0x41},		/* Japan       */
	{"CN ", 0x50},		/* China       */
	{"JP ", 0xFE},		/* Japan       */
	{"JP ", 0xFF},		/* Japan special */
};

/** Default Tx power */
#define TX_PWR_DEFAULT  10

/** Universal region code */
#define UNIVERSAL_REGION_CODE   0xff

/* Following two structures define the supported channels */
/** Channels for 802.11b/g */
static chan_freq_power_t channel_freq_power_UN_BG[] = {
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

/** Channels for 802.11a/j */
static chan_freq_power_t channel_freq_power_UN_AJ[] = {
	{8, 5040, TX_PWR_DEFAULT},
	{12, 5060, TX_PWR_DEFAULT},
	{16, 5080, TX_PWR_DEFAULT},
	{34, 5170, TX_PWR_DEFAULT},
	{38, 5190, TX_PWR_DEFAULT},
	{42, 5210, TX_PWR_DEFAULT},
	{46, 5230, TX_PWR_DEFAULT},
	{36, 5180, TX_PWR_DEFAULT},
	{40, 5200, TX_PWR_DEFAULT},
	{44, 5220, TX_PWR_DEFAULT},
	{48, 5240, TX_PWR_DEFAULT},
	{52, 5260, TX_PWR_DEFAULT},
	{56, 5280, TX_PWR_DEFAULT},
	{60, 5300, TX_PWR_DEFAULT},
	{64, 5320, TX_PWR_DEFAULT},
	{100, 5500, TX_PWR_DEFAULT},
	{104, 5520, TX_PWR_DEFAULT},
	{108, 5540, TX_PWR_DEFAULT},
	{112, 5560, TX_PWR_DEFAULT},
	{116, 5580, TX_PWR_DEFAULT},
	{120, 5600, TX_PWR_DEFAULT},
	{124, 5620, TX_PWR_DEFAULT},
	{128, 5640, TX_PWR_DEFAULT},
	{132, 5660, TX_PWR_DEFAULT},
	{136, 5680, TX_PWR_DEFAULT},
	{140, 5700, TX_PWR_DEFAULT},
	{149, 5745, TX_PWR_DEFAULT},
	{153, 5765, TX_PWR_DEFAULT},
	{157, 5785, TX_PWR_DEFAULT},
	{161, 5805, TX_PWR_DEFAULT},
	{165, 5825, TX_PWR_DEFAULT},
/*  {240, 4920, TX_PWR_DEFAULT},
    {244, 4940, TX_PWR_DEFAULT},
    {248, 4960, TX_PWR_DEFAULT},
    {252, 4980, TX_PWR_DEFAULT},
channels for 11J JP 10M channel gap */
};
#endif /* STA_SUPPORT */

/********************************************************
			Global Variables
********************************************************/

/********************************************************
			Local Functions
********************************************************/
#ifdef STA_SUPPORT
/**
 *  @brief This function converts integer code to region string
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param code         Region code
 *
 *  @return             Region string
 */
static t_u8 *
wlan_11d_code_2_region(pmlan_adapter pmadapter, t_u8 code)
{
	t_u8 i;

	ENTER();

	/* Look for code in mapping table */
	for (i = 0; i < NELEMENTS(region_code_mapping); i++) {
		if (region_code_mapping[i].code == code) {
			LEAVE();
			return region_code_mapping[i].region;
		}
	}

	LEAVE();
	/* Default is US */
	return region_code_mapping[0].region;
}

/**
 *  @brief This function Checks if channel txpwr is learned from AP/IBSS
 *
 *  @param pmadapter            A pointer to mlan_adapter structure
 *  @param band                 Band number
 *  @param chan                 Channel number
 *  @param parsed_region_chan   Pointer to parsed_region_chan_11d_t
 *
 *  @return                     MTRUE or MFALSE
 */
static t_u8
wlan_11d_channel_known(pmlan_adapter pmadapter,
		       t_u8 band,
		       t_u8 chan, parsed_region_chan_11d_t *parsed_region_chan)
{
	chan_power_11d_t *pchan_pwr = parsed_region_chan->chan_pwr;
	t_u8 no_of_chan = parsed_region_chan->no_of_chan;
	t_u8 i = 0;
	t_u8 ret = MFALSE;
	mlan_private *pmpriv;

	ENTER();

	HEXDUMP("11D: parsed_region_chan", (t_u8 *)pchan_pwr,
		sizeof(chan_power_11d_t) * no_of_chan);

	/* Search channel */
	for (i = 0; i < no_of_chan; i++) {
		if (chan == pchan_pwr[i].chan && band == pchan_pwr[i].band) {
			PRINTM(MINFO, "11D: Found channel:%d (band:%d)\n", chan,
			       band);
			ret = MTRUE;

			if (band & BAND_A) {
				/* If chan is a DFS channel, we need to see an AP on it */
				pmpriv = wlan_get_priv(pmadapter,
						       MLAN_BSS_ROLE_STA);
				if (pmpriv &&
				    wlan_11h_radar_detect_required(pmpriv,
								   chan)) {
					PRINTM(MINFO,
					       "11H: DFS channel %d, and ap_seen=%d\n",
					       chan, pchan_pwr[i].ap_seen);
					ret = pchan_pwr[i].ap_seen;
				}
			}

			LEAVE();
			return ret;
		}
	}

	PRINTM(MINFO, "11D: Could not find channel:%d (band:%d)\n", chan, band);
	LEAVE();
	return ret;
}

/**
 *  @brief This function generates parsed_region_chan from Domain Info
 *           learned from AP/IBSS
 *
 *  @param pmadapter            Pointer to mlan_adapter structure
 *  @param region_chan          Pointer to region_chan_t
 *  @param parsed_region_chan   Pointer to parsed_region_chan_11d_t
 *
 *  @return                     N/A
 */
static t_void
wlan_11d_generate_parsed_region_chan(pmlan_adapter pmadapter,
				     region_chan_t *region_chan,
				     parsed_region_chan_11d_t
				     *parsed_region_chan)
{
	chan_freq_power_t *cfp;
	t_u8 i;

	ENTER();

	/* Region channel must be provided */
	if (!region_chan) {
		PRINTM(MWARN, "11D: region_chan is MNULL\n");
		LEAVE();
		return;
	}

	/* Get channel-frequency-power trio */
	cfp = region_chan->pcfp;
	if (!cfp) {
		PRINTM(MWARN, "11D: cfp equal MNULL\n");
		LEAVE();
		return;
	}

	/* Set channel, band and power */
	for (i = 0; i < region_chan->num_cfp; i++, cfp++) {
		parsed_region_chan->chan_pwr[i].chan = (t_u8)cfp->channel;
		parsed_region_chan->chan_pwr[i].band = region_chan->band;
		parsed_region_chan->chan_pwr[i].pwr = (t_u8)cfp->max_tx_power;
		PRINTM(MINFO, "11D: Chan[%d] Band[%d] Pwr[%d]\n",
		       parsed_region_chan->chan_pwr[i].chan,
		       parsed_region_chan->chan_pwr[i].band,
		       parsed_region_chan->chan_pwr[i].pwr);
	}
	parsed_region_chan->no_of_chan = region_chan->num_cfp;

	PRINTM(MINFO, "11D: no_of_chan[%d]\n", parsed_region_chan->no_of_chan);

	LEAVE();
	return;
}

/**
 *  @brief This function generates domain_info from parsed_region_chan
 *
 *  @param pmadapter            Pointer to mlan_adapter structure
 *  @param parsed_region_chan   Pointer to parsed_region_chan_11d_t
 *
 *  @return                     MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_11d_generate_domain_info(pmlan_adapter pmadapter,
			      parsed_region_chan_11d_t *parsed_region_chan)
{
	t_u8 no_of_sub_band = 0;
	t_u8 no_of_chan = parsed_region_chan->no_of_chan;
	t_u8 no_of_parsed_chan = 0;
	t_u8 first_chan = 0, next_chan = 0, max_pwr = 0;
	t_u8 i, flag = MFALSE;
	wlan_802_11d_domain_reg_t *domain_info = &pmadapter->domain_reg;

	ENTER();

	/* Should be only place that clear domain_reg (besides init) */
	memset(pmadapter, domain_info, 0, sizeof(wlan_802_11d_domain_reg_t));

	/* Set country code */
	memcpy(pmadapter, domain_info->country_code,
	       wlan_11d_code_2_region(pmadapter, (t_u8)pmadapter->region_code),
	       COUNTRY_CODE_LEN);

	PRINTM(MINFO, "11D: Number of channel = %d\n", no_of_chan);
	HEXDUMP("11D: parsed_region_chan", (t_u8 *)parsed_region_chan,
		sizeof(parsed_region_chan_11d_t));

	/* Set channel and power */
	for (i = 0; i < no_of_chan; i++) {
		if (!flag) {
			flag = MTRUE;
			next_chan = first_chan =
				parsed_region_chan->chan_pwr[i].chan;
			max_pwr = parsed_region_chan->chan_pwr[i].pwr;
			no_of_parsed_chan = 1;
			continue;
		}

		if (parsed_region_chan->chan_pwr[i].chan == next_chan + 1 &&
		    parsed_region_chan->chan_pwr[i].pwr == max_pwr) {
			next_chan++;
			no_of_parsed_chan++;
		} else {
			domain_info->sub_band[no_of_sub_band].first_chan =
				first_chan;
			domain_info->sub_band[no_of_sub_band].no_of_chan =
				no_of_parsed_chan;
			domain_info->sub_band[no_of_sub_band].max_tx_pwr =
				max_pwr;
			no_of_sub_band++;
			no_of_parsed_chan = 1;
			next_chan = first_chan =
				parsed_region_chan->chan_pwr[i].chan;
			max_pwr = parsed_region_chan->chan_pwr[i].pwr;
		}
	}

	if (flag) {
		domain_info->sub_band[no_of_sub_band].first_chan = first_chan;
		domain_info->sub_band[no_of_sub_band].no_of_chan =
			no_of_parsed_chan;
		domain_info->sub_band[no_of_sub_band].max_tx_pwr = max_pwr;
		no_of_sub_band++;
	}
	domain_info->no_of_sub_band = no_of_sub_band;

	PRINTM(MINFO, "11D: Number of sub-band =0x%x\n",
	       domain_info->no_of_sub_band);
	HEXDUMP("11D: domain_info", (t_u8 *)domain_info,
		COUNTRY_CODE_LEN + 1 +
		sizeof(IEEEtypes_SubbandSet_t) * no_of_sub_band);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function updates the channel power table with the channel
 *            present in BSSDescriptor.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pbss_desc    A pointer to BSSDescriptor_t
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_11d_update_chan_pwr_table(mlan_private *pmpriv, BSSDescriptor_t *pbss_desc)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	parsed_region_chan_11d_t *parsed_region_chan =
		&pmadapter->parsed_region_chan;
	t_u16 i;
	t_u8 tx_power = 0;
	t_u8 chan;

	ENTER();

	chan = pbss_desc->phy_param_set.ds_param_set.current_chan;

	tx_power = wlan_get_txpwr_of_chan_from_cfp(pmpriv, chan);

	if (!tx_power) {
		PRINTM(MMSG, "11D: Invalid channel\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	/* Check whether the channel already exists in channel power table of
	   parsed region */
	for (i = 0; ((i < parsed_region_chan->no_of_chan) &&
		     (i < MAX_NO_OF_CHAN)); i++) {
		if (parsed_region_chan->chan_pwr[i].chan == chan
		    && parsed_region_chan->chan_pwr[i].band ==
		    pbss_desc->bss_band) {
			/* Channel already exists, use minimum of existing and
			   tx_power */
			parsed_region_chan->chan_pwr[i].pwr =
				MIN(parsed_region_chan->chan_pwr[i].pwr,
				    tx_power);
			parsed_region_chan->chan_pwr[i].ap_seen = MTRUE;
			break;
		}
	}

	if (i == parsed_region_chan->no_of_chan && i < MAX_NO_OF_CHAN) {
		/* Channel not found. Update the channel in the channel-power
		   table */
		parsed_region_chan->chan_pwr[i].chan = chan;
		parsed_region_chan->chan_pwr[i].band =
			(t_u8)pbss_desc->bss_band;
		parsed_region_chan->chan_pwr[i].pwr = tx_power;
		parsed_region_chan->chan_pwr[i].ap_seen = MTRUE;
		parsed_region_chan->no_of_chan++;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function finds the no_of_chan-th chan after the first_chan
 *
 *  @param pmadapter  A pointer to mlan_adapter structure
 *  @param band       Band
 *  @param first_chan First channel number
 *  @param no_of_chan Number of channels
 *  @param chan       Pointer to the returned no_of_chan-th chan number
 *
 *  @return           MTRUE or MFALSE
 */
static t_u8
wlan_11d_get_chan(pmlan_adapter pmadapter, t_u8 band, t_u8 first_chan,
		  t_u8 no_of_chan, t_u8 *chan)
{
	chan_freq_power_t *cfp = MNULL;
	t_u8 i;
	t_u8 cfp_no = 0;

	ENTER();
	if (band & (BAND_B | BAND_G | BAND_GN)) {
		cfp = channel_freq_power_UN_BG;
		cfp_no = NELEMENTS(channel_freq_power_UN_BG);
	} else if (band & (BAND_A | BAND_AN)) {
		cfp = channel_freq_power_UN_AJ;
		cfp_no = NELEMENTS(channel_freq_power_UN_AJ);
	} else {
		PRINTM(MERROR, "11D: Wrong Band[%d]\n", band);
		LEAVE();
		return MFALSE;
	}
	/* Locate the first_chan */
	for (i = 0; i < cfp_no; i++) {
		if (cfp && ((cfp + i)->channel == first_chan)) {
			PRINTM(MINFO, "11D: first_chan found\n");
			break;
		}
	}

	if (i < cfp_no) {
		/* Check if beyond the boundary */
		if (i + no_of_chan < cfp_no) {
			/* Get first_chan + no_of_chan */
			*chan = (t_u8)(cfp + i + no_of_chan)->channel;
			LEAVE();
			return MTRUE;
		}
	}

	LEAVE();
	return MFALSE;
}

/**
 *  @brief This function processes the country info present in BSSDescriptor.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pbss_desc     A pointer to BSSDescriptor_t
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_11d_process_country_info(mlan_private *pmpriv, BSSDescriptor_t *pbss_desc)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	parsed_region_chan_11d_t region_chan;
	parsed_region_chan_11d_t *parsed_region_chan =
		&pmadapter->parsed_region_chan;
	t_u16 i, j, num_chan_added = 0;

	ENTER();

	memset(pmadapter, &region_chan, 0, sizeof(parsed_region_chan_11d_t));

	/* Parse 11D country info */
	if (wlan_11d_parse_domain_info(pmadapter, &pbss_desc->country_info,
				       (t_u8)pbss_desc->bss_band,
				       &region_chan) != MLAN_STATUS_SUCCESS) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	if (parsed_region_chan->no_of_chan != 0) {
		/*
		 * Check if the channel number already exists in the
		 * chan-power table of parsed_region_chan
		 */
		for (i = 0; (i < region_chan.no_of_chan && i < MAX_NO_OF_CHAN);
		     i++) {
			for (j = 0;
			     (j < parsed_region_chan->no_of_chan &&
			      j < MAX_NO_OF_CHAN); j++) {
				/*
				 * Channel already exists, update the tx power
				 * with new tx power, since country IE is valid
				 * here.
				 */
				if (region_chan.chan_pwr[i].chan ==
				    parsed_region_chan->chan_pwr[j].chan &&
				    region_chan.chan_pwr[i].band ==
				    parsed_region_chan->chan_pwr[j].band) {
					parsed_region_chan->chan_pwr[j].pwr =
						region_chan.chan_pwr[i].pwr;
					break;
				}
			}

			if (j == parsed_region_chan->no_of_chan &&
			    (j + num_chan_added) < MAX_NO_OF_CHAN) {
				/*
				 * Channel does not exist in the channel power
				 * table, update this new chan and tx_power
				 * to the channel power table
				 */
				parsed_region_chan->
					chan_pwr[parsed_region_chan->
						 no_of_chan +
						 num_chan_added].chan =
					region_chan.chan_pwr[i].chan;
				parsed_region_chan->
					chan_pwr[parsed_region_chan->
						 no_of_chan +
						 num_chan_added].band =
					region_chan.chan_pwr[i].band;
				parsed_region_chan->
					chan_pwr[parsed_region_chan->
						 no_of_chan +
						 num_chan_added].pwr =
					region_chan.chan_pwr[i].pwr;
				parsed_region_chan->
					chan_pwr[parsed_region_chan->
						 no_of_chan +
						 num_chan_added].ap_seen =
					MFALSE;
				num_chan_added++;
			}
		}
		parsed_region_chan->no_of_chan += num_chan_added;
	} else {
		/* Parsed region is empty, copy the first one */
		memcpy(pmadapter, parsed_region_chan,
		       &region_chan, sizeof(parsed_region_chan_11d_t));
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This helper function copies chan_power_11d_t element
 *
 *  @param chan_dst   Pointer to destination of chan_power
 *  @param chan_src   Pointer to source of chan_power
 *
 *  @return           N/A
 */
static t_void
wlan_11d_copy_chan_power(chan_power_11d_t *chan_dst, chan_power_11d_t *chan_src)
{
	ENTER();

	chan_dst->chan = chan_src->chan;
	chan_dst->band = chan_src->band;
	chan_dst->pwr = chan_src->pwr;
	chan_dst->ap_seen = chan_src->ap_seen;

	LEAVE();
	return;
}

/**
 *  @brief This function sorts parsed_region_chan in ascending
 *  channel number.
 *
 *  @param parsed_region_chan   Pointer to parsed_region_chan_11d_t
 *
 *  @return                     N/A
 */
static t_void
wlan_11d_sort_parsed_region_chan(parsed_region_chan_11d_t *parsed_region_chan)
{
	int i, j;
	chan_power_11d_t temp;
	chan_power_11d_t *pchan_power = parsed_region_chan->chan_pwr;

	ENTER();

	PRINTM(MINFO, "11D: Number of channel = %d\n",
	       parsed_region_chan->no_of_chan);

	/* Use insertion sort method */
	for (i = 1; i < parsed_region_chan->no_of_chan; i++) {
		wlan_11d_copy_chan_power(&temp, pchan_power + i);
		for (j = i; j > 0 && (pchan_power + j - 1)->chan > temp.chan;
		     j--)
			wlan_11d_copy_chan_power(pchan_power + j,
						 pchan_power + j - 1);
		wlan_11d_copy_chan_power(pchan_power + j, &temp);
	}

	HEXDUMP("11D: parsed_region_chan", (t_u8 *)parsed_region_chan,
		sizeof(parsed_region_chan_11d_t));

	LEAVE();
	return;
}
#endif /* STA_SUPPORT */

/**
 *  @brief This function sends domain info to FW
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pioctl_buf   A pointer to MLAN IOCTL Request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_11d_send_domain_info(mlan_private *pmpriv, t_void *pioctl_buf)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	/* Send cmd to FW to set domain info */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_802_11D_DOMAIN_INFO,
			       HostCmd_ACT_GEN_SET,
			       0, (t_void *)pioctl_buf, MNULL);
	if (ret)
		PRINTM(MERROR, "11D: Failed to download domain Info\n");

	LEAVE();
	return ret;
}

/**
 *  @brief This function overwrites domain_info
 *
 *  @param pmadapter        Pointer to mlan_adapter structure
 *  @param band             Intended operating band
 *  @param country_code     Intended country code
 *  @param num_sub_band     Count of tuples in list below
 *  @param sub_band_list    List of sub_band tuples
 *
 *  @return                 MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_11d_set_domain_info(mlan_private *pmpriv,
			 t_u8 band,
			 t_u8 country_code[COUNTRY_CODE_LEN],
			 t_u8 num_sub_band,
			 IEEEtypes_SubbandSet_t *sub_band_list)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	wlan_802_11d_domain_reg_t *pdomain = &pmadapter->domain_reg;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	memset(pmadapter, pdomain, 0, sizeof(wlan_802_11d_domain_reg_t));
	memcpy(pmadapter, pdomain->country_code, country_code,
	       COUNTRY_CODE_LEN);
	pdomain->band = band;
	pdomain->no_of_sub_band = num_sub_band;
	memcpy(pmadapter, pdomain->sub_band, sub_band_list,
	       MIN(MRVDRV_MAX_SUBBAND_802_11D,
		   num_sub_band) * sizeof(IEEEtypes_SubbandSet_t));

	LEAVE();
	return ret;
}

/********************************************************
			Global functions
********************************************************/

/**
 *  @brief This function gets if priv is a station (STA)
 *
 *  @param pmpriv       Pointer to mlan_private structure
 *
 *  @return             MTRUE or MFALSE
 */
t_bool
wlan_is_station(mlan_private *pmpriv)
{
	ENTER();
	LEAVE();
	return (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_STA) ? MTRUE : MFALSE;
}

/**
 *  @brief This function gets if 11D is enabled
 *
 *  @param pmpriv       Pointer to mlan_private structure
 *
 *  @return             MTRUE or MFALSE
 */
t_bool
wlan_11d_is_enabled(mlan_private *pmpriv)
{
	ENTER();
	LEAVE();
	return (pmpriv->state_11d.enable_11d == ENABLE_11D) ? MTRUE : MFALSE;
}

/**
 *  @brief Initialize interface variable for 11D
 *
 *  @param pmpriv       Pointer to mlan_private structure
 *
 *  @return             N/A
 */
t_void
wlan_11d_priv_init(mlan_private *pmpriv)
{
	wlan_802_11d_state_t *state = &pmpriv->state_11d;

	ENTER();

	/* Start in disabled mode */
	state->enable_11d = DISABLE_11D;
	if (!pmpriv->adapter->init_para.cfg_11d)
		state->user_enable_11d = DEFAULT_11D_STATE;
	else
		state->user_enable_11d =
			(pmpriv->adapter->init_para.cfg_11d ==
			 MLAN_INIT_PARA_DISABLED) ? DISABLE_11D : ENABLE_11D;

	LEAVE();
	return;
}

/**
 *  @brief Initialize device variable for 11D
 *
 *  @param pmadapter    Pointer to mlan_adapter structure
 *
 *  @return             N/A
 */
t_void
wlan_11d_init(mlan_adapter *pmadapter)
{
	ENTER();

#ifdef STA_SUPPORT
	memset(pmadapter, &(pmadapter->parsed_region_chan), 0,
	       sizeof(parsed_region_chan_11d_t));
	memset(pmadapter, &(pmadapter->universal_channel), 0,
	       sizeof(region_chan_t));
#endif
	memset(pmadapter, &(pmadapter->domain_reg), 0,
	       sizeof(wlan_802_11d_domain_reg_t));

	LEAVE();
	return;
}

/**
 *  @brief This function enable/disable 11D
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pioctl_buf   A pointer to MLAN IOCTL Request buffer
 *  @param flag         11D status
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11d_enable(mlan_private *pmpriv, t_void *pioctl_buf, state_11d_t flag)
{
#ifdef STA_SUPPORT
	mlan_adapter *pmadapter = pmpriv->adapter;
#endif
	mlan_status ret = MLAN_STATUS_SUCCESS;
	state_11d_t enable = flag;

	ENTER();

	/* Send cmd to FW to enable/disable 11D function */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_802_11_SNMP_MIB,
			       HostCmd_ACT_GEN_SET,
			       Dot11D_i, (t_void *)pioctl_buf, &enable);

	if (ret) {
		PRINTM(MERROR, "11D: Failed to %s 11D\n",
		       (flag) ? "enable" : "disable");
	}
#ifdef STA_SUPPORT
	else {
		/* clear parsed table regardless of flag */
		memset(pmadapter, &(pmadapter->parsed_region_chan), 0,
		       sizeof(parsed_region_chan_11d_t));
	}
#endif

	LEAVE();
	return ret;
}

/**
 *  @brief This function implements command CMD_802_11D_DOMAIN_INFO
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pcmd         A pointer to HostCmd_DS_COMMAND structure of
 *                        command buffer
 *  @param cmd_action   Command action
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_802_11d_domain_info(mlan_private *pmpriv,
			     HostCmd_DS_COMMAND *pcmd, t_u16 cmd_action)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	HostCmd_DS_802_11D_DOMAIN_INFO *pdomain_info =
		&pcmd->params.domain_info;
	MrvlIEtypes_DomainParamSet_t *domain = &pdomain_info->domain;
	t_u8 no_of_sub_band = pmadapter->domain_reg.no_of_sub_band;

	ENTER();

	PRINTM(MINFO, "11D: number of sub-band=0x%x\n", no_of_sub_band);

	pcmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11D_DOMAIN_INFO);
	pdomain_info->action = wlan_cpu_to_le16(cmd_action);
	if (cmd_action == HostCmd_ACT_GEN_GET) {
		/* Dump domain info */
		pcmd->size =
			wlan_cpu_to_le16(sizeof(pdomain_info->action) +
					 S_DS_GEN);
		HEXDUMP("11D: 802_11D_DOMAIN_INFO", (t_u8 *)pcmd,
			wlan_le16_to_cpu(pcmd->size));
		LEAVE();
		return MLAN_STATUS_SUCCESS;
	}

	/* Set domain info fields */
	domain->header.type = wlan_cpu_to_le16(TLV_TYPE_DOMAIN);
	memcpy(pmadapter, domain->country_code,
	       pmadapter->domain_reg.country_code,
	       sizeof(domain->country_code));

	domain->header.len =
		((no_of_sub_band * sizeof(IEEEtypes_SubbandSet_t)) +
		 sizeof(domain->country_code));

	if (no_of_sub_band) {
		memcpy(pmadapter, domain->sub_band,
		       pmadapter->domain_reg.sub_band,
		       MIN(MRVDRV_MAX_SUBBAND_802_11D,
			   no_of_sub_band) * sizeof(IEEEtypes_SubbandSet_t));

		pcmd->size = wlan_cpu_to_le16(sizeof(pdomain_info->action) +
					      domain->header.len +
					      sizeof(MrvlIEtypesHeader_t) +
					      S_DS_GEN);
	} else {
		pcmd->size =
			wlan_cpu_to_le16(sizeof(pdomain_info->action) +
					 S_DS_GEN);
	}
	domain->header.len = wlan_cpu_to_le16(domain->header.len);

	HEXDUMP("11D: 802_11D_DOMAIN_INFO", (t_u8 *)pcmd,
		wlan_le16_to_cpu(pcmd->size));

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handle response of CMD_802_11D_DOMAIN_INFO
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         Pointer to command response buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ret_802_11d_domain_info(mlan_private *pmpriv, HostCmd_DS_COMMAND *resp)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	HostCmd_DS_802_11D_DOMAIN_INFO_RSP *domain_info =
		&resp->params.domain_info_resp;
	MrvlIEtypes_DomainParamSet_t *domain = &domain_info->domain;
	t_u16 action = wlan_le16_to_cpu(domain_info->action);
	t_u8 no_of_sub_band = 0;

	ENTER();

	/* Dump domain info response data */
	HEXDUMP("11D: DOMAIN Info Rsp Data", (t_u8 *)resp, resp->size);

	no_of_sub_band =
		(t_u8)((wlan_le16_to_cpu(domain->header.len) - COUNTRY_CODE_LEN)
		       / sizeof(IEEEtypes_SubbandSet_t));

	PRINTM(MINFO, "11D Domain Info Resp: number of sub-band=%d\n",
	       no_of_sub_band);

	if (no_of_sub_band > MRVDRV_MAX_SUBBAND_802_11D) {
		PRINTM(MWARN, "11D: Invalid number of subbands %d returned!!\n",
		       no_of_sub_band);
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	switch (action) {
	case HostCmd_ACT_GEN_SET:	/* Proc Set Action */
		break;
	case HostCmd_ACT_GEN_GET:
		break;
	default:
		PRINTM(MERROR, "11D: Invalid Action:%d\n", domain_info->action);
		ret = MLAN_STATUS_FAILURE;
		break;
	}

	LEAVE();
	return ret;
}

#ifdef STA_SUPPORT
/**
 *  @brief This function parses country information for region channel
 *
 *  @param pmadapter            Pointer to mlan_adapter structure
 *  @param country_info         Country information
 *  @param band                 Chan band
 *  @param parsed_region_chan   Pointer to parsed_region_chan_11d_t
 *
 *  @return                     MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11d_parse_domain_info(pmlan_adapter pmadapter,
			   IEEEtypes_CountryInfoFullSet_t *country_info,
			   t_u8 band,
			   parsed_region_chan_11d_t *parsed_region_chan)
{
	t_u8 no_of_sub_band, no_of_chan;
	t_u8 last_chan, first_chan, cur_chan = 0;
	t_u8 idx = 0;
	t_u8 j, i;

	ENTER();

	/*
	 * Validation Rules:
	 *    1. Valid Region Code
	 *    2. First Chan increment
	 *    3. Channel range no overlap
	 *    4. Channel is valid?
	 *    5. Channel is supported by Region?
	 *    6. Others
	 */

	HEXDUMP("country_info", (t_u8 *)country_info, 30);

	/* Step 1: Check region_code */
	if (!(*(country_info->country_code)) ||
	    (country_info->len <= COUNTRY_CODE_LEN)) {
		/* No region info or wrong region info: treat as no 11D info */
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	no_of_sub_band = (country_info->len - COUNTRY_CODE_LEN) /
		sizeof(IEEEtypes_SubbandSet_t);

	for (j = 0, last_chan = 0; j < no_of_sub_band; j++) {

		if (country_info->sub_band[j].first_chan <= last_chan) {
			/* Step2&3: Check First Chan Num increment and no overlap */
			PRINTM(MINFO, "11D: Chan[%d>%d] Overlap\n",
			       country_info->sub_band[j].first_chan, last_chan);
			continue;
		}

		first_chan = country_info->sub_band[j].first_chan;
		no_of_chan = country_info->sub_band[j].no_of_chan;

		for (i = 0; idx < MAX_NO_OF_CHAN && i < no_of_chan; i++) {
			/* Step 4 : Channel is supported? */
			if (wlan_11d_get_chan
			    (pmadapter, band, first_chan, i,
			     &cur_chan) == MFALSE) {
				/* Chan is not found in UN table */
				PRINTM(MWARN,
				       "11D: channel is not supported: %d\n",
				       i);
				break;
			}

			last_chan = cur_chan;

			/* Step 5: We don't need to check if cur_chan is
			   supported by mrvl in region */
			parsed_region_chan->chan_pwr[idx].chan = cur_chan;
			parsed_region_chan->chan_pwr[idx].band = band;
			parsed_region_chan->chan_pwr[idx].pwr =
				country_info->sub_band[j].max_tx_pwr;
			idx++;
		}

		/* Step 6: Add other checking if any */
	}

	parsed_region_chan->no_of_chan = idx;

	PRINTM(MINFO, "11D: number of channel=0x%x\n",
	       parsed_region_chan->no_of_chan);
	HEXDUMP("11D: parsed_region_chan", (t_u8 *)parsed_region_chan->chan_pwr,
		sizeof(chan_power_11d_t) * idx);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function converts channel to frequency
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param chan         Channel number
 *  @param band         Band
 *
 *  @return             Channel frequency
 */
t_u32
wlan_11d_chan_2_freq(pmlan_adapter pmadapter, t_u8 chan, t_u8 band)
{
	chan_freq_power_t *cf;
	t_u16 cnt;
	t_u16 i;
	t_u32 freq = 0;

	ENTER();

	/* Get channel-frequency-power trios */
	if (band & (BAND_A | BAND_AN)) {
		cf = channel_freq_power_UN_AJ;
		cnt = NELEMENTS(channel_freq_power_UN_AJ);
	} else {
		cf = channel_freq_power_UN_BG;
		cnt = NELEMENTS(channel_freq_power_UN_BG);
	}

	/* Locate channel and return corresponding frequency */
	for (i = 0; i < cnt; i++) {
		if (chan == cf[i].channel)
			freq = cf[i].freq;
	}

	LEAVE();
	return freq;
}

/**
 *  @brief This function setups scan channels
 *
 *  @param pmpriv       Pointer to mlan_private structure
 *  @param band         Band
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_11d_set_universaltable(mlan_private *pmpriv, t_u8 band)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	t_u16 i = 0;

	ENTER();

	memset(pmadapter, pmadapter->universal_channel, 0,
	       sizeof(pmadapter->universal_channel));

	if (band & (BAND_B | BAND_G | BAND_GN))
		/* If band B, G or N */
	{
		/* Set channel-frequency-power */
		pmadapter->universal_channel[i].num_cfp =
			NELEMENTS(channel_freq_power_UN_BG);
		PRINTM(MINFO, "11D: BG-band num_cfp=%d\n",
		       pmadapter->universal_channel[i].num_cfp);

		pmadapter->universal_channel[i].pcfp = channel_freq_power_UN_BG;
		pmadapter->universal_channel[i].valid = MTRUE;

		/* Set region code */
		pmadapter->universal_channel[i].region = UNIVERSAL_REGION_CODE;

		/* Set band */
		if (band & BAND_GN)
			pmadapter->universal_channel[i].band = BAND_G;
		else
			pmadapter->universal_channel[i].band =
				(band & BAND_G) ? BAND_G : BAND_B;
		i++;
	}

	if (band & (BAND_A | BAND_AN)) {
		/* If band A */

		/* Set channel-frequency-power */
		pmadapter->universal_channel[i].num_cfp =
			NELEMENTS(channel_freq_power_UN_AJ);
		PRINTM(MINFO, "11D: AJ-band num_cfp=%d\n",
		       pmadapter->universal_channel[i].num_cfp);

		pmadapter->universal_channel[i].pcfp = channel_freq_power_UN_AJ;

		pmadapter->universal_channel[i].valid = MTRUE;

		/* Set region code */
		pmadapter->universal_channel[i].region = UNIVERSAL_REGION_CODE;

		/* Set band */
		pmadapter->universal_channel[i].band = BAND_A;
		i++;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function calculates the scan type for channels
 *
 *  @param pmadapter            A pointer to mlan_adapter structure
 *  @param band                 Band number
 *  @param chan                 Chan number
 *  @param parsed_region_chan   Pointer to parsed_region_chan_11d_t
 *
 *  @return                     PASSIVE if chan is unknown; ACTIVE
 *                              if chan is known
 */
t_u8
wlan_11d_get_scan_type(pmlan_adapter pmadapter,
		       t_u8 band,
		       t_u8 chan, parsed_region_chan_11d_t *parsed_region_chan)
{
	t_u8 scan_type = MLAN_SCAN_TYPE_PASSIVE;

	ENTER();

	if (wlan_11d_channel_known(pmadapter, band, chan, parsed_region_chan)) {
		/* Channel found */
		PRINTM(MINFO, "11D: Channel found and doing Active Scan\n");
		scan_type = MLAN_SCAN_TYPE_ACTIVE;
	} else
		PRINTM(MINFO,
		       "11D: Channel not found and doing Passive Scan\n");

	LEAVE();
	return scan_type;
}

/**
 *  @brief This function clears the parsed region table, if 11D is enabled
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11d_clear_parsedtable(mlan_private *pmpriv)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (wlan_11d_is_enabled(pmpriv))
		memset(pmadapter, &(pmadapter->parsed_region_chan), 0,
		       sizeof(parsed_region_chan_11d_t));
	else
		ret = MLAN_STATUS_FAILURE;

	LEAVE();
	return ret;
}

/**
 *  @brief This function generates 11D info from user specified regioncode
 *         and download to FW
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param band         Band to create
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11d_create_dnld_countryinfo(mlan_private *pmpriv, t_u8 band)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_adapter *pmadapter = pmpriv->adapter;
	region_chan_t *region_chan;
	parsed_region_chan_11d_t parsed_region_chan;
	t_u8 j;

	ENTER();

	/* Only valid if 11D is enabled */
	if (wlan_11d_is_enabled(pmpriv)) {

		PRINTM(MINFO, "11D: Band[%d]\n", band);

		/* Update parsed_region_chan; download domain info to FW */

		/* Find region channel */
		for (j = 0; j < MAX_REGION_CHANNEL_NUM; j++) {
			region_chan = &pmadapter->region_channel[j];

			PRINTM(MINFO, "11D: [%d] region_chan->Band[%d]\n", j,
			       region_chan->band);

			if (!region_chan || !region_chan->valid ||
			    !region_chan->pcfp)
				continue;
			switch (region_chan->band) {
			case BAND_A:
				switch (band) {
				case BAND_A:
				case BAND_AN:
				case BAND_A | BAND_AN:
					break;
				default:
					continue;
				}
				break;
			case BAND_B:
			case BAND_G:
				switch (band) {
				case BAND_B:
				case BAND_G:
				case BAND_G | BAND_B:
				case BAND_GN:
				case BAND_G | BAND_GN:
				case BAND_B | BAND_G | BAND_GN:
					break;
				default:
					continue;
				}
				break;
			default:
				continue;
			}
			break;
		}

		/* Check if region channel found */
		if (j >= MAX_REGION_CHANNEL_NUM) {
			PRINTM(MERROR, "11D: region_chan not found. Band[%d]\n",
			       band);
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}

		/* Generate parsed region channel info from region channel */
		memset(pmadapter, &parsed_region_chan, 0,
		       sizeof(parsed_region_chan_11d_t));
		wlan_11d_generate_parsed_region_chan(pmadapter, region_chan,
						     &parsed_region_chan);

		/* Generate domain info from parsed region channel info */
		wlan_11d_generate_domain_info(pmadapter, &parsed_region_chan);

		/* Set domain info */
		ret = wlan_11d_send_domain_info(pmpriv, MNULL);
		if (ret) {
			PRINTM(MERROR,
			       "11D: Error sending domain info to FW\n");
		}
	}

	LEAVE();
	return ret;
}

/**
 *  @brief This function parses country info from AP and
 *           download country info to FW
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pbss_desc     A pointer to BSS descriptor
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11d_parse_dnld_countryinfo(mlan_private *pmpriv,
				BSSDescriptor_t *pbss_desc)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_adapter *pmadapter = pmpriv->adapter;
	parsed_region_chan_11d_t region_chan;
	parsed_region_chan_11d_t bssdesc_region_chan;
	t_u32 i, j;

	ENTER();

	/* Only valid if 11D is enabled */
	if (wlan_11d_is_enabled(pmpriv)) {

		memset(pmadapter, &region_chan, 0,
		       sizeof(parsed_region_chan_11d_t));
		memset(pmadapter, &bssdesc_region_chan, 0,
		       sizeof(parsed_region_chan_11d_t));

		memcpy(pmadapter, &region_chan,
		       &pmadapter->parsed_region_chan,
		       sizeof(parsed_region_chan_11d_t));

		if (pbss_desc) {
			/* Parse domain info if available */
			ret = wlan_11d_parse_domain_info(pmadapter,
							 &pbss_desc->
							 country_info,
							 (t_u8)pbss_desc->
							 bss_band,
							 &bssdesc_region_chan);

			if (ret == MLAN_STATUS_SUCCESS) {
				/* Update the channel-power table */
				for (i = 0;
				     ((i < bssdesc_region_chan.no_of_chan)
				      && (i < MAX_NO_OF_CHAN)); i++) {

					for (j = 0;
					     ((j < region_chan.no_of_chan)
					      && (j < MAX_NO_OF_CHAN)); j++) {
						/*
						 * Channel already exists, use minimum
						 * of existing tx power and tx_power
						 * received from country info of the
						 * current AP
						 */
						if (region_chan.chan_pwr[i].
						    chan ==
						    bssdesc_region_chan.
						    chan_pwr[j].chan &&
						    region_chan.chan_pwr[i].
						    band ==
						    bssdesc_region_chan.
						    chan_pwr[j].band) {
							region_chan.chan_pwr[j].
								pwr =
								MIN(region_chan.
								    chan_pwr[j].
								    pwr,
								    bssdesc_region_chan.
								    chan_pwr[i].
								    pwr);
							break;
						}
					}
				}
			}
		}

		/* Generate domain info */
		wlan_11d_generate_domain_info(pmadapter, &region_chan);

		/* Set domain info */
		ret = wlan_11d_send_domain_info(pmpriv, MNULL);
		if (ret) {
			PRINTM(MERROR,
			       "11D: Error sending domain info to FW\n");
		}
	}

	LEAVE();
	return ret;
}

/**
 *  @brief This function prepares domain info from scan table and
 *         downloads the domain info command to the FW.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11d_prepare_dnld_domain_info_cmd(mlan_private *pmpriv)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_adapter *pmadapter = pmpriv->adapter;
	IEEEtypes_CountryInfoFullSet_t *pcountry_full = MNULL;
	t_u32 idx;

	ENTER();

	/* Only valid if 11D is enabled */
	if (wlan_11d_is_enabled(pmpriv) && pmadapter->num_in_scan_table != 0) {
		for (idx = 0; idx < pmadapter->num_in_scan_table; idx++) {
			pcountry_full =
				&pmadapter->pscan_table[idx].country_info;

			ret = wlan_11d_update_chan_pwr_table(pmpriv,
							     &pmadapter->
							     pscan_table[idx]);

			if (*(pcountry_full->country_code) != 0 &&
			    (pcountry_full->len > COUNTRY_CODE_LEN)) {
				/* Country info found in the BSS Descriptor */
				ret = wlan_11d_process_country_info(pmpriv,
								    &pmadapter->
								    pscan_table
								    [idx]);
			}
		}

		/* Sort parsed_region_chan in ascending channel number */
		wlan_11d_sort_parsed_region_chan(&pmadapter->
						 parsed_region_chan);

		/* Check if connected */
		if (pmpriv->media_connected == MTRUE) {
			ret = wlan_11d_parse_dnld_countryinfo(pmpriv,
							      &pmpriv->
							      curr_bss_params.
							      bss_descriptor);
		} else {
			ret = wlan_11d_parse_dnld_countryinfo(pmpriv, MNULL);
		}
	}

	LEAVE();
	return ret;
}

/**
 *  @brief This function checks country code and maps it when needed
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pcountry_code Pointer to the country code string
 *
 *  @return             Pointer to the mapped country code string
 */
static t_u8 *
wlan_11d_map_country_code(IN pmlan_adapter pmadapter, IN t_u8 *pcountry_code)
{
	/* Since firmware can only recognize EU as ETSI domain and there is no memory left
	 * for some devices to convert it in firmware, driver need to convert it before
	 * passing country code to firmware through tlv
	 */

	if (wlan_is_etsi_country(pmadapter, pcountry_code))
		return ("EU ");
	else
		return pcountry_code;
}

/**
 *  @brief This function sets up domain_reg and downloads CMD to FW
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   Pointer to the IOCTL request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11d_cfg_domain_info(IN pmlan_adapter pmadapter,
			 IN mlan_ioctl_req *pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_11d_domain_info *domain_info = MNULL;
	mlan_ds_11d_cfg *cfg_11d = MNULL;
	t_u8 cfp_bg = 0, cfp_a = 0;

	ENTER();

	if (pmadapter->otp_region && pmadapter->otp_region->force_reg) {
		PRINTM(MERROR,
		       "ForceRegionRule is set in the on-chip OTP memory\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	cfg_11d = (mlan_ds_11d_cfg *)pioctl_req->pbuf;
	domain_info = &cfg_11d->param.domain_info;
	memcpy(pmadapter, pmadapter->country_code, domain_info->country_code,
	       COUNTRY_CODE_LEN);
	wlan_11d_set_domain_info(pmpriv, domain_info->band,
				 wlan_11d_map_country_code(pmadapter,
							   domain_info->
							   country_code),
				 domain_info->no_of_sub_band,
				 (IEEEtypes_SubbandSet_t *)domain_info->
				 sub_band);
	ret = wlan_11d_send_domain_info(pmpriv, pioctl_req);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	/* Update region code and table based on country code */
	if (wlan_misc_country_2_cfp_table_code(pmadapter,
					       domain_info->country_code,
					       &cfp_bg, &cfp_a)) {
		PRINTM(MIOCTL, "Country code %c%c not found!\n",
		       domain_info->country_code[0],
		       domain_info->country_code[1]);
		goto done;
	}
	pmadapter->cfp_code_bg = cfp_bg;
	pmadapter->cfp_code_a = cfp_a;
	if (cfp_bg && cfp_a && (cfp_bg == cfp_a))
		pmadapter->region_code = cfp_a;
	else
		pmadapter->region_code = 0;
	if (wlan_set_regiontable(pmpriv, pmadapter->region_code,
				 pmadapter->config_bands | pmadapter->
				 adhoc_start_band)) {
		PRINTM(MIOCTL, "Fail to set regiontabl\n");
		goto done;
	}
done:
	LEAVE();
	return ret;
}
#endif /* STA_SUPPORT */

#if defined(UAP_SUPPORT)
/**
 *  @brief This function handles domain info data from UAP interface.
 *         Checks conditions, sets up domain_reg, then downloads CMD.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param band         Band interface is operating on
 *  @param domain_tlv   Pointer to domain_info tlv
 *  @param pioctl_buf   Pointer to the IOCTL buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11d_handle_uap_domain_info(mlan_private *pmpriv,
				t_u8 band, t_u8 *domain_tlv, t_void *pioctl_buf)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_adapter *pmadapter = pmpriv->adapter;
	MrvlIEtypes_DomainParamSet_t *pdomain_tlv;
	t_u8 num_sub_band = 0;
	t_u8 cfp_bg = 0, cfp_a = 0;

	ENTER();

	pdomain_tlv = (MrvlIEtypes_DomainParamSet_t *)domain_tlv;

	/* update region code & table based on country string */
	if (wlan_misc_country_2_cfp_table_code(pmadapter,
					       pdomain_tlv->country_code,
					       &cfp_bg,
					       &cfp_a) == MLAN_STATUS_SUCCESS) {
		pmadapter->cfp_code_bg = cfp_bg;
		pmadapter->cfp_code_a = cfp_a;
		if (cfp_bg && cfp_a && (cfp_bg == cfp_a))
			pmadapter->region_code = cfp_a;
		else
			pmadapter->region_code = 0;
		if (wlan_set_regiontable(pmpriv, pmadapter->region_code,
					 pmadapter->config_bands | pmadapter->
					 adhoc_start_band)) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}

	memcpy(pmadapter, pmadapter->country_code, pdomain_tlv->country_code,
	       COUNTRY_CODE_LEN);
	num_sub_band =
		((pdomain_tlv->header.len -
		  COUNTRY_CODE_LEN) / sizeof(IEEEtypes_SubbandSet_t));

	/* TODO: don't just clobber pmadapter->domain_reg.
	 *       Add some checking or merging between STA & UAP domain_info
	 */
	wlan_11d_set_domain_info(pmpriv, band, pdomain_tlv->country_code,
				 num_sub_band, pdomain_tlv->sub_band);
	ret = wlan_11d_send_domain_info(pmpriv, pioctl_buf);

done:
	LEAVE();
	return ret;
}
#endif
