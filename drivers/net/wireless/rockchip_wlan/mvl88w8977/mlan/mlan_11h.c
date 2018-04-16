/** @file mlan_11h.c
 *
 *  @brief This file contains functions for 802.11H.
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

/*************************************************************
Change Log:
    03/26/2009: initial version
************************************************************/

#include "mlan.h"
#include "mlan_join.h"
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_ioctl.h"
#include "mlan_11h.h"
#include "mlan_11n.h"
#ifdef UAP_SUPPORT
#include "mlan_uap.h"
#endif

/********************************************************
			Local Variables
********************************************************/

/** Default IBSS DFS recovery interval (in TBTTs); used for adhoc start */
#define WLAN_11H_DEFAULT_DFS_RECOVERY_INTERVAL   100

/** Default 11h power constraint used to offset the maximum transmit power */
#define WLAN_11H_TPC_POWERCONSTRAINT  0

/** 11h TPC Power capability minimum setting, sent in TPC_INFO command to fw */
#define WLAN_11H_TPC_POWERCAPABILITY_MIN     5

/** 11h TPC Power capability maximum setting, sent in TPC_INFO command to fw */
#define WLAN_11H_TPC_POWERCAPABILITY_MAX     20

/** Regulatory requirement for the duration of a channel availability check */
#define WLAN_11H_CHANNEL_AVAIL_CHECK_DURATION    60000	/* in ms */

/** Starting Frequency for 11A band */
#define START_FREQ_11A_BAND     5000	/* in MHz */

/** DFS Channel Move Time */
#define DFS_CHAN_MOVE_TIME  10	/* in sec */

/** Regulatory requirement for the duration of a non-occupancy period */
#define WLAN_11H_NON_OCCUPANCY_PERIOD    1800	/* in sec (30mins) */

/** Maximum allowable age (seconds) on DFS report data */
#define MAX_DFS_REPORT_USABLE_AGE_SEC  (120)	/* 2 minutes */

/** Minimum delay for CHAN_SW IE to broadcast by FW */
#define MIN_RDH_CHAN_SW_IE_PERIOD_MSEC  (400)	/* 4 beacons @ 100ms */

/** Maximum delay for CHAN_SW IE to broadcast by FW */
#define MAX_RDH_CHAN_SW_IE_PERIOD_MSEC  (3000)	/* 5 beacons @ 600ms */

/** Maximum retries on selecting new random channel */
#define MAX_RANDOM_CHANNEL_RETRIES  (20)

/** Maximum retries on selecting new random non-dfs channel */
#define MAX_SWITCH_CHANNEL_RETRIES  (30)

/** Value for undetermined priv_curr_idx on first entry to new RDH stage */
#define RDH_STAGE_FIRST_ENTRY_PRIV_IDX  (0xff)

/** Region codes 0x10, 0x20:  channels 1 thru 11 supported */
static const
IEEEtypes_SupportChan_Subband_t wlan_11h_2_4G_region_FCC = { 1, 11 };

/** Region codes 0x30, 0x32, 0x41, 0x50:  channels 1 thru 13 supported */
static const
IEEEtypes_SupportChan_Subband_t wlan_11h_2_4G_region_EU = { 1, 13 };

/** Region code 0x40:  only channel 14 supported */
static const
IEEEtypes_SupportChan_Subband_t wlan_11h_2_4G_region_JPN40 = { 14, 1 };

/** JPN sub-band config : Start Channel = 8, NumChans = 3 */
static const
IEEEtypes_SupportChan_Subband_t wlan_11h_JPN_bottom_band = { 8, 3 };

/** U-NII sub-band config : Start Channel = 36, NumChans = 4 */
static const
IEEEtypes_SupportChan_Subband_t wlan_11h_unii_lower_band = { 36, 4 };

/** U-NII sub-band config : Start Channel = 52, NumChans = 4 */
static const
IEEEtypes_SupportChan_Subband_t wlan_11h_unii_middle_band = { 52, 4 };

/** U-NII sub-band config : Start Channel = 100, NumChans = 11 */
static const
IEEEtypes_SupportChan_Subband_t wlan_11h_unii_mid_upper_band = { 100, 11 };

/** U-NII sub-band config : Start Channel = 149, NumChans = 5 */
static const
IEEEtypes_SupportChan_Subband_t wlan_11h_unii_upper_band = { 149, 5 };

/** Internally passed structure used to send a CMD_802_11_TPC_INFO command */
typedef struct {
	t_u8 chan;
	       /**< Channel to which the power constraint applies */
	t_u8 power_constraint;
			   /**< Local power constraint to send to firmware */
} wlan_11h_tpc_info_param_t;

/********************************************************
			Global Variables
********************************************************/

/********************************************************
			Local Functions
********************************************************/

/**
 *  @brief Utility function to get a random number based on the underlying OS
 *
 *  @param pmadapter Pointer to mlan_adapter
 *  @return random integer
 */
static t_u32
wlan_11h_get_random_num(pmlan_adapter pmadapter)
{
	t_u32 sec, usec;

	ENTER();
	pmadapter->callbacks.moal_get_system_time(pmadapter->pmoal_handle, &sec,
						  &usec);
	sec = (sec & 0xFFFF) + (sec >> 16);
	usec = (usec & 0xFFFF) + (usec >> 16);

	LEAVE();
	return (usec << 16) | sec;
}

/**
 *  @brief Convert an IEEE formatted IE to 16-bit ID/Len Marvell
 *         proprietary format
 *
 *  @param pmadapter Pointer to mlan_adapter
 *  @param pout_buf Output parameter: Buffer to output Marvell formatted IE
 *  @param pin_ie   Pointer to IEEE IE to be converted to Marvell format
 *
 *  @return         Number of bytes output to pout_buf parameter return
 */
static t_u32
wlan_11h_convert_ieee_to_mrvl_ie(mlan_adapter *pmadapter,
				 t_u8 *pout_buf, const t_u8 *pin_ie)
{
	MrvlIEtypesHeader_t mrvl_ie_hdr;
	t_u8 *ptmp_buf = pout_buf;

	ENTER();
	/* Assign the Element Id and Len to the Marvell struct attributes */
	mrvl_ie_hdr.type = wlan_cpu_to_le16(pin_ie[0]);
	mrvl_ie_hdr.len = wlan_cpu_to_le16(pin_ie[1]);

	/* If the element ID is zero, return without doing any copying */
	if (!mrvl_ie_hdr.type) {
		LEAVE();
		return 0;
	}

	/* Copy the header to the buffer pointer */
	memcpy(pmadapter, ptmp_buf, &mrvl_ie_hdr, sizeof(mrvl_ie_hdr));

	/* Increment the temp buffer pointer by the size appended */
	ptmp_buf += sizeof(mrvl_ie_hdr);

	/* Append the data section of the IE; length given by the IEEE IE length */
	memcpy(pmadapter, ptmp_buf, pin_ie + 2, pin_ie[1]);

	LEAVE();
	/* Return the number of bytes appended to pout_buf */
	return sizeof(mrvl_ie_hdr) + pin_ie[1];
}

#ifdef STA_SUPPORT
/**
 *  @brief Setup the IBSS DFS element passed to the firmware in adhoc start
 *         and join commands
 *
 *  The DFS Owner and recovery fields are set to be our MAC address and
 *    a predetermined constant recovery value.  If we are joining an adhoc
 *    network, these values are replaced with the existing IBSS values.
 *    They are valid only when starting a new IBSS.
 *
 *  The IBSS DFS Element is variable in size based on the number of
 *    channels supported in our current region.
 *
 *  @param priv Private driver information structure
 *  @param pdfs Output parameter: Pointer to the IBSS DFS element setup by
 *              this function.
 *
 *  @return
 *    - Length of the returned element in pdfs output parameter
 *    - 0 if returned element is not setup
 */
static t_u32
wlan_11h_set_ibss_dfs_ie(mlan_private *priv, IEEEtypes_IBSS_DFS_t *pdfs)
{
	t_u8 num_chans = 0;
	MeasRptBasicMap_t initial_map;
	mlan_adapter *adapter = priv->adapter;

	ENTER();

	memset(adapter, pdfs, 0x00, sizeof(IEEEtypes_IBSS_DFS_t));

	/*
	 * A basic measurement report is included with each channel in the
	 *   map field.  Initial value for the map for each supported channel
	 *   is with only the unmeasured bit set.
	 */
	memset(adapter, &initial_map, 0x00, sizeof(initial_map));
	initial_map.unmeasured = 1;

	/* Set the DFS Owner and recovery interval fields */
	memcpy(adapter, pdfs->dfs_owner, priv->curr_addr,
	       sizeof(pdfs->dfs_owner));
	pdfs->dfs_recovery_interval = WLAN_11H_DEFAULT_DFS_RECOVERY_INTERVAL;

	for (; (num_chans < adapter->parsed_region_chan.no_of_chan)
	     && (num_chans < WLAN_11H_MAX_IBSS_DFS_CHANNELS); num_chans++) {
		pdfs->channel_map[num_chans].channel_number =
			adapter->parsed_region_chan.chan_pwr[num_chans].chan;

		/*
		 * Set the initial map field with a basic measurement
		 */
		pdfs->channel_map[num_chans].rpt_map = initial_map;
	}

	/*
	 * If we have an established channel map, include it and return
	 *   a valid DFS element
	 */
	if (num_chans) {
		PRINTM(MINFO, "11h: Added %d channels to IBSS DFS Map\n",
		       num_chans);

		pdfs->element_id = IBSS_DFS;
		pdfs->len =
			(sizeof(pdfs->dfs_owner) +
			 sizeof(pdfs->dfs_recovery_interval)
			 + num_chans * sizeof(IEEEtypes_ChannelMap_t));

		LEAVE();
		return pdfs->len + sizeof(pdfs->len) + sizeof(pdfs->element_id);
	}

	/* Ensure the element is zeroed out for an invalid return */
	memset(adapter, pdfs, 0x00, sizeof(IEEEtypes_IBSS_DFS_t));

	LEAVE();
	return 0;
}
#endif

/**
 *  @brief Setup the Supported Channel IE sent in association requests
 *
 *  The Supported Channels IE is required to be sent when the spectrum
 *    management capability (11h) is enabled.  The element contains a
 *    starting channel and number of channels tuple for each sub-band
 *    the STA supports.  This information is based on the operating region.
 *
 *  @param priv      Private driver information structure
 *  @param band      Band in use
 *  @param psup_chan Output parameter: Pointer to the Supported Chan element
 *                   setup by this function.
 *
 *  @return
 *    - Length of the returned element in psup_chan output parameter
 *    - 0 if returned element is not setup
 */
static
	t_u16
wlan_11h_set_supp_channels_ie(mlan_private *priv,
			      t_u8 band,
			      IEEEtypes_SupportedChannels_t *psup_chan)
{
	t_u16 num_subbands = 0;
	t_u16 ret_len = 0;
	t_u8 cfp_bg, cfp_a;

	ENTER();
	memset(priv->adapter, psup_chan, 0x00,
	       sizeof(IEEEtypes_SupportedChannels_t));

	cfp_bg = cfp_a = priv->adapter->region_code;
	if (!priv->adapter->region_code) {
		/* Invalid region code, use CFP code */
		cfp_bg = priv->adapter->cfp_code_bg;
		cfp_a = priv->adapter->cfp_code_a;
	}

	if ((band & BAND_B) || (band & BAND_G)) {
		/*
		 * Channels are contiguous in 2.4GHz, usually only one subband.
		 */
		switch (cfp_bg) {
		case 0x10:	/* USA FCC   */
		case 0x20:	/* Canada IC */
		default:
			psup_chan->subband[num_subbands++] =
				wlan_11h_2_4G_region_FCC;
			break;
		case 0x30:	/* Europe ETSI */
		case 0x41:	/* Japan  */
		case 0x50:	/* China  */
			psup_chan->subband[num_subbands++] =
				wlan_11h_2_4G_region_EU;
			break;
		case 0x40:	/* Japan  */
			psup_chan->subband[num_subbands++] =
				wlan_11h_2_4G_region_JPN40;
			break;
		case 0xff:	/* Japan special */
			psup_chan->subband[num_subbands++] =
				wlan_11h_2_4G_region_EU;
			psup_chan->subband[num_subbands++] =
				wlan_11h_2_4G_region_JPN40;
			break;
		}
	} else if (band & BAND_A) {
		/*
		 * Set the supported channel elements based on the region code,
		 * incrementing num_subbands for each sub-band we append to the
		 * element.
		 */
		switch (cfp_a) {
		case 0x10:	/* USA FCC   */
		case 0x20:	/* Canada IC */
		case 0x30:	/* Europe ETSI */
		default:
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_lower_band;
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_middle_band;
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_mid_upper_band;
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_upper_band;
			break;
		case 0x50:	/* China */
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_lower_band;
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_middle_band;
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_upper_band;
			break;
		case 0x40:	/* Japan */
		case 0x41:	/* Japan */
		case 0xff:	/* Japan special */
			psup_chan->subband[num_subbands++] =
				wlan_11h_JPN_bottom_band;
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_lower_band;
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_middle_band;
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_mid_upper_band;
			break;
		case 0x1:	/* Low band (5150-5250 MHz) channels */
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_lower_band;
			break;
		case 0x2:	/* Lower middle band (5250-5350 MHz) channels */
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_middle_band;
			break;
		case 0x3:	/* Upper middle band (5470-5725 MHz) channels */
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_mid_upper_band;
			break;
		case 0x4:	/* High band (5725-5850 MHz) channels */
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_upper_band;
			break;
		case 0x5:	/* Low band (5150-5250 MHz) and High band (5725-5850 MHz) channels */
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_lower_band;
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_upper_band;
			break;
		case 0x6:	/* Low band (5150-5250 MHz) and Lower middle band (5250-5350 MHz) and High band (5725-5850 MHz) channels */
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_lower_band;
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_middle_band;
			psup_chan->subband[num_subbands++] =
				wlan_11h_unii_upper_band;
			break;
		}
	}

	/*
	 * If we have setup any supported subbands in the element, return a
	 *    valid IE along with its size, else return 0.
	 */
	if (num_subbands) {
		psup_chan->element_id = SUPPORTED_CHANNELS;
		psup_chan->len =
			num_subbands * sizeof(IEEEtypes_SupportChan_Subband_t);

		ret_len = (t_u16)(psup_chan->len
				  + sizeof(psup_chan->len) +
				  sizeof(psup_chan->element_id));

		HEXDUMP("11h: SupChan", (t_u8 *)psup_chan, ret_len);
	}

	LEAVE();
	return ret_len;
}

/**
 *  @brief Prepare CMD_802_11_TPC_ADAPT_REQ firmware command
 *
 *  @param priv      Private driver information structure
 *  @param pcmd_ptr  Output parameter: Pointer to the command being prepared
 *                   for the firmware
 *  @param pinfo_buf HostCmd_DS_802_11_TPC_ADAPT_REQ passed as void data block
 *
 *  @return          MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_11h_cmd_tpc_request(mlan_private *priv,
			 HostCmd_DS_COMMAND *pcmd_ptr, const t_void *pinfo_buf)
{
	ENTER();

	memcpy(priv->adapter, &pcmd_ptr->params.tpc_req, pinfo_buf,
	       sizeof(HostCmd_DS_802_11_TPC_ADAPT_REQ));

	pcmd_ptr->params.tpc_req.req.timeout =
		wlan_cpu_to_le16(pcmd_ptr->params.tpc_req.req.timeout);

	/* Converted to little endian in wlan_11h_cmd_process */
	pcmd_ptr->size = sizeof(HostCmd_DS_802_11_TPC_ADAPT_REQ) + S_DS_GEN;

	HEXDUMP("11h: 11_TPC_ADAPT_REQ:", (t_u8 *)pcmd_ptr,
		(t_u32)pcmd_ptr->size);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Prepare CMD_802_11_TPC_INFO firmware command
 *
 *  @param priv      Private driver information structure
 *  @param pcmd_ptr  Output parameter: Pointer to the command being prepared
 *                   for the firmware
 *  @param pinfo_buf wlan_11h_tpc_info_param_t passed as void data block
 *
 *  @return          MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_11h_cmd_tpc_info(mlan_private *priv,
		      HostCmd_DS_COMMAND *pcmd_ptr, const t_void *pinfo_buf)
{
	HostCmd_DS_802_11_TPC_INFO *ptpc_info = &pcmd_ptr->params.tpc_info;
	MrvlIEtypes_LocalPowerConstraint_t *pconstraint =
		&ptpc_info->local_constraint;
	MrvlIEtypes_PowerCapability_t *pcap = &ptpc_info->power_cap;

	wlan_11h_device_state_t *pstate = &priv->adapter->state_11h;
	const wlan_11h_tpc_info_param_t *ptpc_info_param =
		(wlan_11h_tpc_info_param_t *)pinfo_buf;

	ENTER();

	pcap->min_power = pstate->min_tx_power_capability;
	pcap->max_power = pstate->max_tx_power_capability;
	pcap->header.len = wlan_cpu_to_le16(2);
	pcap->header.type = wlan_cpu_to_le16(TLV_TYPE_POWER_CAPABILITY);

	pconstraint->chan = ptpc_info_param->chan;
	pconstraint->constraint = ptpc_info_param->power_constraint;
	pconstraint->header.type = wlan_cpu_to_le16(TLV_TYPE_POWER_CONSTRAINT);
	pconstraint->header.len = wlan_cpu_to_le16(2);

	/* Converted to little endian in wlan_11h_cmd_process */
	pcmd_ptr->size = sizeof(HostCmd_DS_802_11_TPC_INFO) + S_DS_GEN;

	HEXDUMP("11h: TPC INFO", (t_u8 *)pcmd_ptr, (t_u32)pcmd_ptr->size);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief  Prepare CMD_802_11_CHAN_SW_ANN firmware command
 *
 *  @param priv      Private driver information structure
 *  @param pcmd_ptr  Output parameter: Pointer to the command being
 *                   prepared to for firmware
 *  @param pinfo_buf HostCmd_DS_802_11_CHAN_SW_ANN passed as void data block
 *
 *  @return          MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_11h_cmd_chan_sw_ann(mlan_private *priv,
			 HostCmd_DS_COMMAND *pcmd_ptr, const t_void *pinfo_buf)
{
	const HostCmd_DS_802_11_CHAN_SW_ANN *pch_sw_ann =
		(HostCmd_DS_802_11_CHAN_SW_ANN *)pinfo_buf;

	ENTER();

	/* Converted to little endian in wlan_11h_cmd_process */
	pcmd_ptr->size = sizeof(HostCmd_DS_802_11_CHAN_SW_ANN) + S_DS_GEN;

	memcpy(priv->adapter, &pcmd_ptr->params.chan_sw_ann, pch_sw_ann,
	       sizeof(HostCmd_DS_802_11_CHAN_SW_ANN));

	PRINTM(MINFO, "11h: ChSwAnn: %#x-%u, Seq=%u, Ret=%u\n",
	       pcmd_ptr->command, pcmd_ptr->size, pcmd_ptr->seq_num,
	       pcmd_ptr->result);
	PRINTM(MINFO, "11h: ChSwAnn: Ch=%d, Cnt=%d, Mode=%d\n",
	       pch_sw_ann->new_chan, pch_sw_ann->switch_count,
	       pch_sw_ann->switch_mode);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief  Prepare CMD_CHAN_REPORT_REQUEST firmware command
 *
 *  @param priv      Private driver information structure
 *  @param pcmd_ptr  Output parameter: Pointer to the command being
 *                   prepared to for firmware
 *  @param pinfo_buf HostCmd_DS_CHAN_RPT_REQ passed as void data block
 *
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_PENDING
 */
static mlan_status
wlan_11h_cmd_chan_rpt_req(mlan_private *priv,
			  HostCmd_DS_COMMAND *pcmd_ptr, const t_void *pinfo_buf)
{
	const HostCmd_DS_CHAN_RPT_REQ *pchan_rpt_req =
		(HostCmd_DS_CHAN_RPT_REQ *)pinfo_buf;
	wlan_dfs_device_state_t *pstate_dfs = &priv->adapter->state_dfs;
	MrvlIEtypes_ChanRpt11hBasic_t *ptlv_basic;
	t_bool is_cancel_req = MFALSE;

	/*
	 * pchan_rpt_req->millisec_dwell_time would be zero if the chan_rpt_req
	 * is to cancel current ongoing report
	 */
	if (pchan_rpt_req->millisec_dwell_time == 0)
		is_cancel_req = MTRUE;

	ENTER();
	if (pstate_dfs->dfs_check_pending && !is_cancel_req) {
		PRINTM(MERROR,
		       "11h: ChanRptReq - previous CMD_CHAN_REPORT_REQUEST has"
		       " not returned its result yet (as EVENT_CHANNEL_READY)."
		       "  This command will be dropped.\n");
		LEAVE();
		return MLAN_STATUS_PENDING;
	}

	/* Converted to little endian in wlan_11h_cmd_process */
	pcmd_ptr->size = sizeof(HostCmd_DS_CHAN_RPT_REQ) + S_DS_GEN;

	memcpy(priv->adapter, &pcmd_ptr->params.chan_rpt_req, pchan_rpt_req,
	       sizeof(HostCmd_DS_CHAN_RPT_REQ));
	pcmd_ptr->params.chan_rpt_req.chan_desc.startFreq =
		wlan_cpu_to_le16(pchan_rpt_req->chan_desc.startFreq);
	pcmd_ptr->params.chan_rpt_req.millisec_dwell_time =
		wlan_cpu_to_le32(pchan_rpt_req->millisec_dwell_time);

	/* if DFS channel, add BASIC report TLV, and set radar bit */
	if (!is_cancel_req &&
	    wlan_11h_radar_detect_required(priv,
					   pchan_rpt_req->chan_desc.chanNum)) {
		ptlv_basic =
			(MrvlIEtypes_ChanRpt11hBasic_t *)(((t_u8 *)(pcmd_ptr)) +
							  pcmd_ptr->size);
		ptlv_basic->Header.type =
			wlan_cpu_to_le16(TLV_TYPE_CHANRPT_11H_BASIC);
		ptlv_basic->Header.len =
			wlan_cpu_to_le16(sizeof(MeasRptBasicMap_t));
		memset(priv->adapter, &ptlv_basic->map, 0,
		       sizeof(MeasRptBasicMap_t));
		ptlv_basic->map.radar = 1;
		pcmd_ptr->size += sizeof(MrvlIEtypes_ChanRpt11hBasic_t);
	}

	/* update dfs sturcture.
	 * dfs_check_pending is set when we receive CMD_RESP == SUCCESS */
	pstate_dfs->dfs_check_pending = MFALSE;
	pstate_dfs->dfs_radar_found = MFALSE;
	pstate_dfs->dfs_check_priv = MNULL;

	if (!is_cancel_req)
		pstate_dfs->dfs_check_channel =
			pchan_rpt_req->chan_desc.chanNum;

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Set the local power capability and constraint TLV
 *
 *  @param ppbuffer                The buffer to add these two TLVs
 *  @param channel                 Channel to which the power constraint applies
 *  @param power_constraint        Power constraint to be applied on the channel
 *  @param min_tx_power_capability Min. Tx Power in Power Capability IE
 *  @param max_tx_power_capability Max. Tx Power in Power Capability IE
 *
 *  @return                        The len increased
 */
static t_u32
wlan_11h_set_local_power_constraint_tlv(t_u8 **ppbuffer,
					t_u8 channel,
					t_u8 power_constraint,
					t_u8 min_tx_power_capability,
					t_u8 max_tx_power_capability)
{
	MrvlIEtypes_PowerCapability_t *pcap;
	MrvlIEtypes_LocalPowerConstraint_t *pconstraint;
	t_u8 *start_ptr = MNULL;

	ENTER();

	/* Null Checks */
	if ((ppbuffer == MNULL) || (((t_u8 *)(*ppbuffer)) == MNULL)) {
		LEAVE();
		return 0;
	}

	start_ptr = (t_u8 *)(*ppbuffer);

	PRINTM(MINFO,
	       "11h: Set local power constraint = %d channel=%d min_tx_pwr=%d max_tx_pwr=%d\n",
	       power_constraint, channel, min_tx_power_capability,
	       max_tx_power_capability);

	pcap = (MrvlIEtypes_PowerCapability_t *)*ppbuffer;
	pcap->header.type = wlan_cpu_to_le16(TLV_TYPE_POWER_CAPABILITY);
	pcap->header.len = wlan_cpu_to_le16(2);
	pcap->min_power = min_tx_power_capability;
	pcap->max_power = max_tx_power_capability;
	*ppbuffer += sizeof(MrvlIEtypesHeader_t) + 2;

	pconstraint = (MrvlIEtypes_LocalPowerConstraint_t *)*ppbuffer;
	pconstraint->header.type = wlan_cpu_to_le16(TLV_TYPE_POWER_CONSTRAINT);
	pconstraint->header.len = wlan_cpu_to_le16(2);
	pconstraint->chan = channel;
	pconstraint->constraint = power_constraint;
	*ppbuffer += sizeof(MrvlIEtypesHeader_t) + 2;

	LEAVE();
	return (t_u32)(*ppbuffer - start_ptr);
}

/**
 *  @brief  Utility function to process a join to an infrastructure BSS
 *
 *  @param priv          Private driver information structure
 *  @param ppbuffer      Output parameter: Pointer to the TLV output buffer,
 *                       modified on return to point after the appended 11h TLVs
 *  @param band          Band on which we are joining the BSS
 *  @param channel       Channel on which we are joining the BSS
 *  @param p11h_bss_info Pointer to the 11h BSS information for this network
 *                       that was parsed out of the scan response.
 *
 *  @return              Integer number of bytes appended to the TLV output
 *                       buffer (ppbuffer)
 */
static t_u32
wlan_11h_process_infra_join(mlan_private *priv,
			    t_u8 **ppbuffer,
			    t_u8 band,
			    t_u32 channel, wlan_11h_bss_info_t *p11h_bss_info)
{
	MrvlIEtypesHeader_t ie_header;
	IEEEtypes_SupportedChannels_t sup_chan_ie;
	t_u32 ret_len = 0;
	t_u16 sup_chan_len = 0;

	ENTER();

	/* Null Checks */
	if ((ppbuffer == MNULL) || (((t_u8 *)(*ppbuffer)) == MNULL)) {
		LEAVE();
		return 0;
	}

	ret_len +=
		wlan_11h_set_local_power_constraint_tlv(ppbuffer, (t_u8)channel,
							(t_u8)p11h_bss_info->
							power_constraint.
							local_constraint,
							(t_u8)priv->adapter->
							state_11h.
							min_tx_power_capability,
							(t_u8)priv->adapter->
							state_11h.
							max_tx_power_capability);

	/* Setup the Supported Channels IE */
	sup_chan_len = wlan_11h_set_supp_channels_ie(priv, band, &sup_chan_ie);

	/*
	 * If we returned a valid Supported Channels IE, wrap and append it
	 */
	if (sup_chan_len) {
		/* Wrap the supported channels IE with a passthrough TLV type */
		ie_header.type = wlan_cpu_to_le16(TLV_TYPE_PASSTHROUGH);
		ie_header.len = wlan_cpu_to_le16(sup_chan_len);
		memcpy(priv->adapter, *ppbuffer, &ie_header, sizeof(ie_header));

		/*
		 * Increment the return size and the return buffer
		 * pointer param
		 */
		*ppbuffer += sizeof(ie_header);
		ret_len += sizeof(ie_header);

		/*
		 * Copy the supported channels IE to the output buf,
		 * advance pointer
		 */
		memcpy(priv->adapter, *ppbuffer, &sup_chan_ie, sup_chan_len);
		*ppbuffer += sup_chan_len;
		ret_len += sup_chan_len;
	}

	LEAVE();
	return ret_len;
}

/**
 *  @brief Utility function to process a start or join to an adhoc network
 *
 *  Add the elements to the TLV buffer needed in the start/join adhoc commands:
 *       - IBSS DFS IE
 *       - Quiet IE
 *
 *  Also send the local constraint to the firmware in a TPC_INFO command.
 *
 *  @param priv          Private driver information structure
 *  @param ppbuffer      Output parameter: Pointer to the TLV output buffer,
 *                       modified on return to point after the appended 11h TLVs
 *  @param channel       Channel on which we are starting/joining the IBSS
 *  @param p11h_bss_info Pointer to the 11h BSS information for this network
 *                       that was parsed out of the scan response.  NULL
 *                       indicates we are starting the adhoc network
 *
 *  @return              Integer number of bytes appended to the TLV output
 *                       buffer (ppbuffer)
 */
static t_u32
wlan_11h_process_adhoc(mlan_private *priv,
		       t_u8 **ppbuffer,
		       t_u32 channel, wlan_11h_bss_info_t *p11h_bss_info)
{
	IEEEtypes_IBSS_DFS_t dfs_elem;
	t_u32 size_appended;
	t_u32 ret_len = 0;
	t_s8 local_constraint = 0;
	mlan_adapter *adapter = priv->adapter;

	ENTER();

#ifdef STA_SUPPORT
	/* Format our own IBSS DFS Element.  Include our channel map fields */
	wlan_11h_set_ibss_dfs_ie(priv, &dfs_elem);
#endif

	if (p11h_bss_info) {
		/*
		 * Copy the DFS Owner/Recovery Interval from the BSS
		 * we are joining
		 */
		memcpy(adapter, dfs_elem.dfs_owner,
		       p11h_bss_info->ibss_dfs.dfs_owner,
		       sizeof(dfs_elem.dfs_owner));
		dfs_elem.dfs_recovery_interval =
			p11h_bss_info->ibss_dfs.dfs_recovery_interval;
	}

	/* Append the dfs element to the TLV buffer */
	size_appended = wlan_11h_convert_ieee_to_mrvl_ie(adapter,
							 (t_u8 *)*ppbuffer,
							 (t_u8 *)&dfs_elem);

	HEXDUMP("11h: IBSS-DFS", (t_u8 *)*ppbuffer, size_appended);
	*ppbuffer += size_appended;
	ret_len += size_appended;

	/*
	 * Check to see if we are joining a network.  Join is indicated by the
	 *   BSS Info pointer being valid (not NULL)
	 */
	if (p11h_bss_info) {
		/*
		 * If there was a quiet element, include it in
		 * adhoc join command
		 */
		if (p11h_bss_info->quiet.element_id == QUIET) {
			size_appended
				=
				wlan_11h_convert_ieee_to_mrvl_ie(adapter,
								 (t_u8 *)
								 *ppbuffer,
								 (t_u8 *)
								 &p11h_bss_info->
								 quiet);
			HEXDUMP("11h: Quiet", (t_u8 *)*ppbuffer, size_appended);
			*ppbuffer += size_appended;
			ret_len += size_appended;
		}

		/* Copy the local constraint from the network */
		local_constraint =
			p11h_bss_info->power_constraint.local_constraint;
	} else {
		/*
		 * If we are the adhoc starter, we can add a quiet element
		 */
		if (adapter->state_11h.quiet_ie.quiet_period) {
			size_appended =
				wlan_11h_convert_ieee_to_mrvl_ie(adapter,
								 (t_u8 *)
								 *ppbuffer,
								 (t_u8 *)
								 &adapter->
								 state_11h.
								 quiet_ie);
			HEXDUMP("11h: Quiet", (t_u8 *)*ppbuffer, size_appended);
			*ppbuffer += size_appended;
			ret_len += size_appended;
		}
		/* Use the local_constraint configured in the driver state */
		local_constraint = adapter->state_11h.usr_def_power_constraint;
	}

	PRINTM(MINFO, "WEILIE 1: ppbuffer = %p\n", *ppbuffer);

	ret_len +=
		wlan_11h_set_local_power_constraint_tlv(ppbuffer, (t_u8)channel,
							(t_u8)local_constraint,
							(t_u8)priv->adapter->
							state_11h.
							min_tx_power_capability,
							(t_u8)priv->adapter->
							state_11h.
							max_tx_power_capability);
	PRINTM(MINFO, "WEILIE 2: ppbuffer = %p\n", *ppbuffer);

	LEAVE();
	return ret_len;
}

/**
 *  @brief Return whether the driver has enabled 11h for the interface
 *
 *  Association/Join commands are dynamic in that they enable 11h in the
 *    driver/firmware when they are detected in the existing BSS.
 *
 *  @param priv  Private driver information structure
 *
 *  @return
 *    - MTRUE if 11h is enabled
 *    - MFALSE otherwise
 */
static t_bool
wlan_11h_is_enabled(mlan_private *priv)
{
	ENTER();
	LEAVE();
	return priv->intf_state_11h.is_11h_enabled;
}

/**
 *  @brief Return whether the device has activated slave radar detection.
 *
 *  @param priv  Private driver information structure
 *
 *  @return
 *    - MTRUE if slave radar detection is enabled in firmware
 *    - MFALSE otherwise
 */
static t_bool
wlan_11h_is_slave_radar_det_active(mlan_private *priv)
{
	ENTER();
	LEAVE();
	return priv->adapter->state_11h.is_slave_radar_det_active;
}

/**
 *  @brief Return whether the slave interface is active, and on DFS channel.
 *  priv is assumed to already be a dfs slave interface, doesn't check this.
 *
 *  @param priv  Private driver information structure
 *
 *  @return
 *    - MTRUE if priv is slave, and meets both conditions
 *    - MFALSE otherwise
 */
static t_bool
wlan_11h_is_slave_active_on_dfs_chan(mlan_private *priv)
{
	t_bool ret = MFALSE;

	ENTER();
	if ((priv->media_connected == MTRUE) &&
	    (priv->curr_bss_params.band & BAND_A) &&
	    wlan_11h_radar_detect_required(priv,
					   priv->curr_bss_params.bss_descriptor.
					   channel))
		ret = MTRUE;

	LEAVE();
	return ret;
}

/**
 *  @brief Return whether the master interface is active, and on DFS channel.
 *  priv is assumed to already be a dfs master interface, doesn't check this.
 *
 *  @param priv  Private driver information structure
 *
 *  @return
 *    - MTRUE if priv is master, and meets both conditions
 *    - MFALSE otherwise
 */
static t_bool
wlan_11h_is_master_active_on_dfs_chan(mlan_private *priv)
{
	t_bool ret = MFALSE;

	ENTER();
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_STA) {
		/* Ad-hoc creator */
		if (((priv->media_connected == MTRUE)
		     || (priv->adhoc_state == ADHOC_STARTING)) &&
		    (priv->adapter->adhoc_start_band & BAND_A) &&
		    wlan_11h_radar_detect_required(priv, priv->adhoc_channel))
			ret = MTRUE;
	} else if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
		/* UAP */
#ifdef UAP_SUPPORT
		if ((priv->uap_bss_started == MTRUE) &&
		    (priv->uap_state_chan_cb.bandcfg.chanBand == BAND_5GHZ) &&
		    wlan_11h_radar_detect_required(priv,
						   priv->uap_state_chan_cb.
						   channel))
			ret = MTRUE;
#endif
	}
	LEAVE();
	return ret;
}

/**
 *  @brief Determine if priv is DFS Master interface
 *
 *  @param priv Pointer to mlan_private
 *
 *  @return MTRUE or MFALSE
 */
static t_bool
wlan_11h_is_dfs_master(mlan_private *priv)
{
	t_bool ret = MFALSE;

	ENTER();
	/* UAP: all are master */
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP)
		ret = MTRUE;

	/* STA: only ad-hoc creator is master */
	else if ((GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_STA) &&
		 (priv->bss_mode == MLAN_BSS_MODE_IBSS) &&
		 (priv->adhoc_state == ADHOC_STARTED ||
		  priv->adhoc_state == ADHOC_STARTING))
		ret = MTRUE;

	/* all other cases = slave interface */
	LEAVE();
	return ret;
}

/* Need this as function to pass to wlan_count_priv_cond() */
/**
 *  @brief Determine if priv is DFS Slave interface
 *
 *  @param priv Pointer to mlan_private
 *
 *  @return MTRUE or MFALSE
 */

static t_bool
wlan_11h_is_dfs_slave(mlan_private *priv)
{
	t_bool ret = MFALSE;
	ENTER();
	ret = !wlan_11h_is_dfs_master(priv);
	LEAVE();
	return ret;
}

/**
 *  @brief This function checks if interface is active.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *
 *  @return             MTRUE or MFALSE
 */
t_bool
wlan_is_intf_active(mlan_private *pmpriv)
{
	t_bool ret = MFALSE;
	ENTER();

#ifdef UAP_SUPPORT
	if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_UAP)
		/*
		 * NOTE: UAP's media_connected == true only after first STA
		 * associated. Need different variable to tell if UAP
		 * has been started.
		 */
		ret = pmpriv->uap_bss_started;
	else
#endif
	if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_STA)
		ret = pmpriv->media_connected;

	LEAVE();
	return ret;
}

/**
 *  @brief This function gets current radar detect flags
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             11H MIB setting for radar detect
 */
static t_u32
wlan_11h_get_current_radar_detect_flags(mlan_adapter *pmadapter)
{
	t_u32 radar_det_flags = 0;

	ENTER();
	if (pmadapter->state_11h.is_master_radar_det_active)
		radar_det_flags |= MASTER_RADAR_DET_MASK;
	if (pmadapter->state_11h.is_slave_radar_det_active)
		radar_det_flags |= SLAVE_RADAR_DET_MASK;

	PRINTM(MINFO, "%s: radar_det_state_curr=0x%x\n",
	       __func__, radar_det_flags);

	LEAVE();
	return radar_det_flags;
}

/**
 *  @brief This function checks if radar detect flags have/should be changed.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pnew_state   Output param with new state, if return MTRUE.
 *
 *  @return             MTRUE (need update) or MFALSE (no change in flags)
 */
static t_bool
wlan_11h_check_radar_det_state(mlan_adapter *pmadapter, OUT t_u32 *pnew_state)
{
	t_u32 radar_det_state_new = 0;
	t_bool ret;

	ENTER();
	PRINTM(MINFO, "%s: master_radar_det_pending=%d, "
	       " slave_radar_det_pending=%d\n", __func__,
	       pmadapter->state_11h.master_radar_det_enable_pending,
	       pmadapter->state_11h.slave_radar_det_enable_pending);

	/* new state comes from evaluating interface states & pending starts */
	if (pmadapter->state_11h.master_radar_det_enable_pending ||
	    (wlan_count_priv_cond(pmadapter,
				  wlan_11h_is_master_active_on_dfs_chan,
				  wlan_11h_is_dfs_master) > 0))
		radar_det_state_new |= MASTER_RADAR_DET_MASK;
	if (pmadapter->state_11h.slave_radar_det_enable_pending ||
	    (wlan_count_priv_cond(pmadapter,
				  wlan_11h_is_slave_active_on_dfs_chan,
				  wlan_11h_is_dfs_slave) > 0))
		radar_det_state_new |= SLAVE_RADAR_DET_MASK;

	PRINTM(MINFO, "%s: radar_det_state_new=0x%x\n",
	       __func__, radar_det_state_new);

	/* now compare flags with current state */
	ret = (wlan_11h_get_current_radar_detect_flags(pmadapter)
	       != radar_det_state_new) ? MTRUE : MFALSE;
	if (ret)
		*pnew_state = radar_det_state_new;

	LEAVE();
	return ret;
}

/**
 *  @brief Prepare ioctl for add/remove CHAN_SW IE - RADAR_DETECTED event handling
 *
 *  @param pmadapter        Pointer to mlan_adapter
 *  @param pioctl_req       Pointer to completed mlan_ioctl_req (allocated inside)
 *  @param is_adding_ie     CHAN_SW IE is to be added (MTRUE), or removed (MFALSE)
 *
 *  @return                 MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_11h_prepare_custom_ie_chansw(IN mlan_adapter *pmadapter,
				  OUT mlan_ioctl_req **ppioctl_req,
				  IN t_bool is_adding_ie)
{
	mlan_ioctl_req *pioctl_req = MNULL;
	mlan_ds_misc_cfg *pds_misc_cfg = MNULL;
	custom_ie *pcust_chansw_ie = MNULL;
	IEEEtypes_ChanSwitchAnn_t *pchansw_ie = MNULL;
	mlan_status ret;

	ENTER();

	if (pmadapter == MNULL || ppioctl_req == MNULL) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	/* allocate buffer for mlan_ioctl_req and mlan_ds_misc_cfg */
	/* FYI - will be freed as part of cmd_response handler */
	ret = pmadapter->callbacks.moal_malloc(pmadapter->pmoal_handle,
					       sizeof(mlan_ioctl_req) +
					       sizeof(mlan_ds_misc_cfg),
					       MLAN_MEM_DEF,
					       (t_u8 **)&pioctl_req);
	if ((ret != MLAN_STATUS_SUCCESS) || !pioctl_req) {
		PRINTM(MERROR, "%s(): Could not allocate ioctl req\n",
		       __func__);
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	pds_misc_cfg = (mlan_ds_misc_cfg *)((t_u8 *)pioctl_req +
					    sizeof(mlan_ioctl_req));

	/* prepare mlan_ioctl_req */
	memset(pmadapter, pioctl_req, 0x00, sizeof(mlan_ioctl_req));
	pioctl_req->req_id = MLAN_IOCTL_MISC_CFG;
	pioctl_req->action = MLAN_ACT_SET;
	pioctl_req->pbuf = (t_u8 *)pds_misc_cfg;
	pioctl_req->buf_len = sizeof(mlan_ds_misc_cfg);

	/* prepare mlan_ds_misc_cfg */
	memset(pmadapter, pds_misc_cfg, 0x00, sizeof(mlan_ds_misc_cfg));
	pds_misc_cfg->sub_command = MLAN_OID_MISC_CUSTOM_IE;
	pds_misc_cfg->param.cust_ie.type = TLV_TYPE_MGMT_IE;
	pds_misc_cfg->param.cust_ie.len = (sizeof(custom_ie) - MAX_IE_SIZE);

	/* configure custom_ie api settings */
	pcust_chansw_ie =
		(custom_ie *)&pds_misc_cfg->param.cust_ie.ie_data_list[0];
	pcust_chansw_ie->ie_index = 0xffff;	/* Auto index */
	pcust_chansw_ie->ie_length = sizeof(IEEEtypes_ChanSwitchAnn_t);
	pcust_chansw_ie->mgmt_subtype_mask = (is_adding_ie)
		? MBIT(8) | MBIT(5)	/* add IE for BEACON | PROBE_RSP */
		: 0;		/* remove IE */

	/* prepare CHAN_SW IE inside ioctl */
	pchansw_ie = (IEEEtypes_ChanSwitchAnn_t *)pcust_chansw_ie->ie_buffer;
	pchansw_ie->element_id = CHANNEL_SWITCH_ANN;
	pchansw_ie->len =
		sizeof(IEEEtypes_ChanSwitchAnn_t) - sizeof(IEEEtypes_Header_t);
	pchansw_ie->chan_switch_mode = 1;	/* STA should not transmit */
	pchansw_ie->new_channel_num = pmadapter->state_rdh.new_channel;

	pchansw_ie->chan_switch_count = pmadapter->dfs_cs_count;
	PRINTM(MCMD_D, "New Channel = %d Channel switch count = %d\n",
	       pmadapter->state_rdh.new_channel, pchansw_ie->chan_switch_count);

	pds_misc_cfg->param.cust_ie.len += pcust_chansw_ie->ie_length;
	DBG_HEXDUMP(MCMD_D, "11h: custom_ie containing CHAN_SW IE",
		    (t_u8 *)pcust_chansw_ie, pds_misc_cfg->param.cust_ie.len);

	/* assign output pointer before returning */
	*ppioctl_req = pioctl_req;
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

#ifdef UAP_SUPPORT
/** Bits 2,3 of band config define the band width */
#define UAP_BAND_WIDTH_MASK 0x0C

/**
 *  @brief Check if start channel 165 is allowed to operate in
 *  previous uAP channel's band config
 *
 *  @param start_chn     Random Start channel choosen after radar detection
 *  @param uap_band_cfg  Private driver uAP band configuration information structure
 *
 *  @return MFALSE if the channel is not allowed in given band
 */
static t_bool
wlan_11h_is_band_valid(t_u8 start_chn, Band_Config_t uap_band_cfg)
{

	/* if band width is not 20MHZ (either 40 or 80MHz)
	 * return MFALSE, 165 is not allowed in bands other than 20MHZ
	 */
	if (start_chn == 165 && (uap_band_cfg.chanWidth != CHAN_BW_20MHZ)) {
		return MFALSE;
	}
	return MTRUE;
}

/**
 *  @brief Retrieve a randomly selected starting channel if needed for 11h
 *
 *  If 11h is enabled and 5GHz band is selected in band_config
 *    return a random channel in A band, else one from BG band.
 *
 *  @param priv          Private driver information structure
 *  @param uap_band_cfg  Private driver information structure
 *
 *  @return      Starting channel
 */
static t_u8
wlan_11h_get_uap_start_channel(mlan_private *priv, Band_Config_t uap_band_cfg)
{
	t_u8 start_chn;
	mlan_adapter *adapter = priv->adapter;
	t_u32 region;
	t_u32 rand_entry;
	region_chan_t *chn_tbl;
	t_u8 rand_tries = 0;

	/*TODO:  right now mostly a copy of wlan_11h_get_adhoc_start_channel.
	 *       Improve to be more specfic to UAP, e.g.
	 *       1. take into account COUNTRY_CODE -> region_code
	 *       2. check domain_info for value channels
	 */
	ENTER();

	/*
	 * Set start_chn to the Default.
	 * Used if 11h is disabled or the band
	 * does not require 11h support.
	 */
	start_chn = DEFAULT_AD_HOC_CHANNEL;

	/*
	 * Check that we are looking for a channel in the A Band
	 */
	if (uap_band_cfg.chanBand == BAND_5GHZ) {
		/*
		 * Set default to the A Band default.
		 * Used if random selection fails
		 * or if 11h is not enabled
		 */
		start_chn = DEFAULT_AD_HOC_CHANNEL_A;

		/*
		 * Check that 11h is enabled in the driver
		 */
		if (wlan_11h_is_enabled(priv)) {
			/*
			 * Search the region_channel tables for a channel table
			 * that is marked for the A Band.
			 */
			for (region = 0; (region < MAX_REGION_CHANNEL_NUM);
			     region++) {
				chn_tbl = &adapter->region_channel[region];

				/* Check if table is valid and marked for A Band */
				if (chn_tbl->valid
				    && chn_tbl->region == adapter->region_code
				    && chn_tbl->band & BAND_A) {
					/*
					 * Set the start channel.  Get a random
					 * number and use it to pick an entry
					 * in the table between 0 and the number
					 * of channels in the table (NumCFP).
					 */
					rand_entry =
						wlan_11h_get_random_num(adapter)
						% chn_tbl->num_cfp;
					start_chn =
						(t_u8)chn_tbl->pcfp[rand_entry].
						channel;
					/* Loop until a non-dfs channel is found with compatible band
					 * bounded by chn_tbl->num_cfp entries in the channel table
					 */
					while ((wlan_11h_is_channel_under_nop
						(adapter, start_chn) ||
						((adapter->state_rdh.stage ==
						  RDH_GET_INFO_CHANNEL) &&
						 wlan_11h_radar_detect_required
						 (priv, start_chn)) ||
						!(wlan_11h_is_band_valid
						  (start_chn, uap_band_cfg))) &&
					       (++rand_tries <
						chn_tbl->num_cfp)) {
						rand_entry++;
						rand_entry =
							rand_entry %
							chn_tbl->num_cfp;
						start_chn =
							(t_u8)chn_tbl->
							pcfp[rand_entry].
							channel;
						PRINTM(MINFO,
						       "start chan=%d rand_entry=%d\n",
						       start_chn, rand_entry);
					}

					if (rand_tries == chn_tbl->num_cfp) {
						PRINTM(MERROR,
						       "Failed to get UAP start channel\n");
						start_chn = 0;
					}
				}
			}
		}
	}

	PRINTM(MCMD_D, "11h: UAP Get Start Channel %d\n", start_chn);
	LEAVE();
	return start_chn;
}
#endif /* UAP_SUPPORT */

#ifdef DEBUG_LEVEL1
static const char *DFS_TS_REPR_STRINGS[] = { "",
	"NOP_start",
	"CAC_completed"
};
#endif

/**
 *  @brief Search for a dfs timestamp in the list with desired channel.
 *
 *  Assumes there will only be one timestamp per channel in the list.
 *
 *  @param pmadapter  Pointer to mlan_adapter
 *  @param channel    Channel number
 *
 *  @return           Pointer to timestamp if found, or MNULL
 */
static wlan_dfs_timestamp_t *
wlan_11h_find_dfs_timestamp(mlan_adapter *pmadapter, t_u8 channel)
{
	wlan_dfs_timestamp_t *pts = MNULL, *pts_found = MNULL;

	ENTER();
	pts = (wlan_dfs_timestamp_t *)util_peek_list(pmadapter->pmoal_handle,
						     &pmadapter->state_dfs.
						     dfs_ts_head, MNULL, MNULL);

	while (pts &&
	       pts !=
	       (wlan_dfs_timestamp_t *)&pmadapter->state_dfs.dfs_ts_head) {
		PRINTM(MINFO,
		       "dfs_timestamp(@ %p) - chan=%d, repr=%d(%s),"
		       " time(sec.usec)=%lu.%06lu\n", pts, pts->channel,
		       pts->represents, DFS_TS_REPR_STRINGS[pts->represents],
		       pts->ts_sec, pts->ts_usec);

		if (pts->channel == channel) {
			pts_found = pts;
			break;
		}
		pts = pts->pnext;
	}

	LEAVE();
	return pts_found;
}

/**
 *  @brief Removes dfs timestamp from list.
 *
 *  @param pmadapter  Pointer to mlan_adapter
 *  @param pdfs_ts    Pointer to dfs_timestamp to remove
 */
static t_void
wlan_11h_remove_dfs_timestamp(mlan_adapter *pmadapter,
			      wlan_dfs_timestamp_t *pdfs_ts)
{
	ENTER();
	/* dequeue and delete timestamp */
	util_unlink_list(pmadapter->pmoal_handle,
			 &pmadapter->state_dfs.dfs_ts_head,
			 (pmlan_linked_list)pdfs_ts, MNULL, MNULL);
	pmadapter->callbacks.moal_mfree(pmadapter->pmoal_handle,
					(t_u8 *)pdfs_ts);
	LEAVE();
}

/**
 *  @brief Add a dfs timestamp to the list
 *
 *  Assumes there will only be one timestamp per channel in the list,
 *  and that timestamp modes (represents) are mutually exclusive.
 *
 *  @param pmadapter  Pointer to mlan_adapter
 *  @param repr       Timestamp 'represents' value (see _dfs_timestamp_repr_e)
 *  @param channel    Channel number
 *
 *  @return           Pointer to timestamp if found, or MNULL
 */
static mlan_status
wlan_11h_add_dfs_timestamp(mlan_adapter *pmadapter, t_u8 repr, t_u8 channel)
{
	wlan_dfs_timestamp_t *pdfs_ts = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	pdfs_ts = wlan_11h_find_dfs_timestamp(pmadapter, channel);

	if (!pdfs_ts) {
		/* need to allocate new timestamp */
		ret = pmadapter->callbacks.moal_malloc(pmadapter->pmoal_handle,
						       sizeof
						       (wlan_dfs_timestamp_t),
						       MLAN_MEM_DEF,
						       (t_u8 **)&pdfs_ts);
		if ((ret != MLAN_STATUS_SUCCESS) || !pdfs_ts) {
			PRINTM(MERROR, "%s(): Could not allocate dfs_ts\n",
			       __func__);
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}

		memset(pmadapter, (t_u8 *)pdfs_ts, 0,
		       sizeof(wlan_dfs_timestamp_t));

		util_enqueue_list_tail(pmadapter->pmoal_handle,
				       &pmadapter->state_dfs.dfs_ts_head,
				       (pmlan_linked_list)pdfs_ts, MNULL,
				       MNULL);
		pdfs_ts->channel = channel;
	}
	/* (else, use existing timestamp for channel; see assumptions above) */

	/* update params */
	pmadapter->callbacks.moal_get_system_time(pmadapter->pmoal_handle,
						  &pdfs_ts->ts_sec,
						  &pdfs_ts->ts_usec);
	pdfs_ts->represents = repr;

	PRINTM(MCMD_D, "11h: add/update dfs_timestamp - chan=%d, repr=%d(%s),"
	       " time(sec.usec)=%lu.%06lu\n", pdfs_ts->channel,
	       pdfs_ts->represents, DFS_TS_REPR_STRINGS[pdfs_ts->represents],
	       pdfs_ts->ts_sec, pdfs_ts->ts_usec);

	LEAVE();
	return ret;
}

/********************************************************
			Global functions
********************************************************/

/**
 *  @brief Return whether the device has activated master radar detection.
 *
 *  @param priv  Private driver information structure
 *
 *  @return
 *    - MTRUE if master radar detection is enabled in firmware
 *    - MFALSE otherwise
 */
t_bool
wlan_11h_is_master_radar_det_active(mlan_private *priv)
{
	ENTER();
	LEAVE();
	return priv->adapter->state_11h.is_master_radar_det_active;
}

/**
 *  @brief Configure master radar detection.
 *  Call wlan_11h_check_update_radar_det_state() afterwards
 *    to push this to firmware.
 *
 *  @param priv  Private driver information structure
 *  @param enable Whether to enable or disable master radar detection
 *
 *  @return  MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 *
 *  @sa wlan_11h_check_update_radar_det_state
 */
mlan_status
wlan_11h_config_master_radar_det(mlan_private *priv, t_bool enable)
{
	mlan_status ret = MLAN_STATUS_FAILURE;

	/* Force disable master radar detection on in-AP interfaces */
	if (priv->adapter->dfs_repeater)
		enable = MFALSE;

	ENTER();
	if (wlan_11h_is_dfs_master(priv) &&
	    priv->adapter->init_para.dfs_master_radar_det_en) {
		priv->adapter->state_11h.master_radar_det_enable_pending =
			enable;
		ret = MLAN_STATUS_SUCCESS;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Configure slave radar detection.
 *  Call wlan_11h_check_update_radar_det_state() afterwards
 *    to push this to firmware.
 *
 *  @param priv  Private driver information structure
 *  @param enable Whether to enable or disable slave radar detection
 *
 *  @return  MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 *
 *  @sa wlan_11h_check_update_radar_det_state
 */
mlan_status
wlan_11h_config_slave_radar_det(mlan_private *priv, t_bool enable)
{
	mlan_status ret = MLAN_STATUS_FAILURE;

	/* Force disable radar detection on STA interfaces */
	if (priv->adapter->dfs_repeater)
		enable = MFALSE;

	ENTER();
	if (wlan_11h_is_dfs_slave(priv) &&
	    priv->adapter->init_para.dfs_slave_radar_det_en) {
		priv->adapter->state_11h.slave_radar_det_enable_pending =
			enable;
		ret = MLAN_STATUS_SUCCESS;
	}
	LEAVE();
	return ret;
}

/**
 *  @brief Checks all interfaces and determines if radar_detect flag states
 *         have/should be changed.  If so, sends SNMP_MIB 11H command to FW.
 *         Call this function on any interface enable/disable/channel change.
 *
 *  @param pmpriv  Pointer to mlan_private structure
 *
 *  @return        MLAN_STATUS_SUCCESS (update or not)
 *              or MLAN_STATUS_FAILURE (cmd failure)
 *
 *  @sa    wlan_11h_check_radar_det_state
 */
mlan_status
wlan_11h_check_update_radar_det_state(mlan_private *pmpriv)
{
	t_u32 new_radar_det_state = 0;
	t_u32 mib_11h = 0;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (wlan_11h_check_radar_det_state(pmpriv->adapter,
					   &new_radar_det_state)) {
		PRINTM(MCMD_D, "%s: radar_det_state being updated.\n",
		       __func__);

		mib_11h |= new_radar_det_state;
		/* keep priv's existing 11h state */
		if (pmpriv->intf_state_11h.is_11h_active)
			mib_11h |= ENABLE_11H_MASK;

		/* Send cmd to FW to enable/disable 11h function in firmware */
		ret = wlan_prepare_cmd(pmpriv,
				       HostCmd_CMD_802_11_SNMP_MIB,
				       HostCmd_ACT_GEN_SET,
				       Dot11H_i, MNULL, &mib_11h);
		if (ret)
			ret = MLAN_STATUS_FAILURE;
	}

	/* updated state sent OR no change, thus no longer pending */
	pmpriv->adapter->state_11h.master_radar_det_enable_pending = MFALSE;
	pmpriv->adapter->state_11h.slave_radar_det_enable_pending = MFALSE;

	LEAVE();
	return ret;
}

/**
 *  @brief Query 11h firmware enabled state.
 *
 *  Return whether the firmware currently has 11h extensions enabled
 *
 *  @param priv  Private driver information structure
 *
 *  @return
 *    - MTRUE if 11h has been activated in the firmware
 *    - MFALSE otherwise
 *
 *  @sa wlan_11h_activate
 */
t_bool
wlan_11h_is_active(mlan_private *priv)
{
	ENTER();
	LEAVE();
	return priv->intf_state_11h.is_11h_active;
}

/**
 *  @brief Enable the transmit interface and record the state.
 *
 *  @param priv  Private driver information structure
 *
 *  @return      N/A
 */
t_void
wlan_11h_tx_enable(mlan_private *priv)
{
	ENTER();
	if (priv->intf_state_11h.tx_disabled) {
		if (priv->media_connected == MTRUE) {
			wlan_recv_event(priv, MLAN_EVENT_ID_FW_START_TX, MNULL);
			priv->intf_state_11h.tx_disabled = MFALSE;
		}
	}
	LEAVE();
}

/**
 *  @brief Disable the transmit interface and record the state.
 *
 *  @param priv  Private driver information structure
 *
 *  @return      N/A
 */
t_void
wlan_11h_tx_disable(mlan_private *priv)
{
	ENTER();
	if (!priv->intf_state_11h.tx_disabled) {
		if (priv->media_connected == MTRUE) {
			priv->intf_state_11h.tx_disabled = MTRUE;
			wlan_recv_event(priv, MLAN_EVENT_ID_FW_STOP_TX, MNULL);
		}
	}
	LEAVE();
}

/**
 *  @brief Enable or Disable the 11h extensions in the firmware
 *
 *  @param priv         Private driver information structure
 *  @param pioctl_buf   A pointer to MLAN IOCTL Request buffer
 *  @param flag         Enable 11h if MTRUE, disable otherwise
 *
 *  @return      MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11h_activate(mlan_private *priv, t_void *pioctl_buf, t_bool flag)
{
	t_u32 enable = flag & ENABLE_11H_MASK;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	/* add bits for master/slave radar detect into enable. */
	enable |= wlan_11h_get_current_radar_detect_flags(priv->adapter);

	/* Whenever repeater mode is on make sure
	 * we do not enable master or slave radar det mode.
	 * HW will not detect radar in dfs_repeater mode.
	 */
	if (priv->adapter->dfs_repeater) {
		enable &= ~(MASTER_RADAR_DET_MASK | SLAVE_RADAR_DET_MASK);
	}

	/*
	 * Send cmd to FW to enable/disable 11h function in firmware
	 */
	ret = wlan_prepare_cmd(priv,
			       HostCmd_CMD_802_11_SNMP_MIB,
			       HostCmd_ACT_GEN_SET,
			       Dot11H_i, (t_void *)pioctl_buf, &enable);
	if (ret)
		ret = MLAN_STATUS_FAILURE;
	else
		/* Set boolean flag in driver 11h state */
		priv->intf_state_11h.is_11h_active = flag;

	PRINTM(MINFO, "11h: %s\n", flag ? "Activate" : "Deactivate");

	LEAVE();
	return ret;
}

/**
 *  @brief Initialize the 11h parameters and enable 11h when starting an IBSS
 *
 *  @param adapter mlan_adapter structure
 *
 *  @return      N/A
 */
t_void
wlan_11h_init(mlan_adapter *adapter)
{
	wlan_11h_device_state_t *pstate_11h = &adapter->state_11h;
	IEEEtypes_Quiet_t *pquiet = &adapter->state_11h.quiet_ie;
	wlan_dfs_device_state_t *pstate_dfs = &adapter->state_dfs;
	wlan_radar_det_hndlg_state_t *pstate_rdh = &adapter->state_rdh;
#ifdef DFS_TESTING_SUPPORT
	wlan_dfs_testing_settings_t *pdfs_test = &adapter->dfs_test_params;
#endif

	ENTER();

	/* Initialize 11H struct */
	pstate_11h->usr_def_power_constraint = WLAN_11H_TPC_POWERCONSTRAINT;
	pstate_11h->min_tx_power_capability = WLAN_11H_TPC_POWERCAPABILITY_MIN;
	pstate_11h->max_tx_power_capability = WLAN_11H_TPC_POWERCAPABILITY_MAX;

	pstate_11h->recvd_chanswann_event = MFALSE;
	pstate_11h->master_radar_det_enable_pending = MFALSE;
	pstate_11h->slave_radar_det_enable_pending = MFALSE;
	pstate_11h->is_master_radar_det_active = MFALSE;
	pstate_11h->is_slave_radar_det_active = MFALSE;

	/*Initialize quiet_ie */
	memset(adapter, pquiet, 0, sizeof(IEEEtypes_Quiet_t));
	pquiet->element_id = QUIET;
	pquiet->len =
		(sizeof(pquiet->quiet_count) + sizeof(pquiet->quiet_period)
		 + sizeof(pquiet->quiet_duration)
		 + sizeof(pquiet->quiet_offset));

	/* Initialize DFS struct */
	pstate_dfs->dfs_check_pending = MFALSE;
	pstate_dfs->dfs_radar_found = MFALSE;
	pstate_dfs->dfs_check_channel = 0;
	pstate_dfs->dfs_report_time_sec = 0;
	util_init_list((pmlan_linked_list)&pstate_dfs->dfs_ts_head);

	/* Initialize RDH struct */
	pstate_rdh->stage = RDH_OFF;
	pstate_rdh->priv_list_count = 0;
	pstate_rdh->priv_curr_idx = 0;
	pstate_rdh->curr_channel = 0;
	pstate_rdh->new_channel = 0;
	memset(adapter, &(pstate_rdh->uap_band_cfg), 0,
	       sizeof(pstate_rdh->uap_band_cfg));
	pstate_rdh->max_bcn_dtim_ms = 0;
	memset(adapter, pstate_rdh->priv_list, 0,
	       sizeof(pstate_rdh->priv_list));

	/* Initialize dfs channel switch count */
#define DFS_CS_COUNT 5
	adapter->dfs_cs_count = DFS_CS_COUNT;

#ifdef DFS_TESTING_SUPPORT
	/* Initialize DFS testing struct */
	pdfs_test->user_cac_period_msec = 0;
	pdfs_test->user_nop_period_sec = 0;
	pdfs_test->no_channel_change_on_radar = MFALSE;
	pdfs_test->fixed_new_channel_on_radar = 0;
#endif

	LEAVE();
}

/**
 *  @brief Cleanup for the 11h parameters that allocated memory, etc.
 *
 *  @param adapter mlan_adapter structure
 *
 *  @return      N/A
 */
t_void
wlan_11h_cleanup(mlan_adapter *adapter)
{
	wlan_dfs_device_state_t *pstate_dfs = &adapter->state_dfs;
	wlan_dfs_timestamp_t *pdfs_ts;

	ENTER();

	/* cleanup dfs_timestamp list */
	pdfs_ts = (wlan_dfs_timestamp_t *)util_peek_list(adapter->pmoal_handle,
							 &pstate_dfs->
							 dfs_ts_head, MNULL,
							 MNULL);
	while (pdfs_ts) {
		util_unlink_list(adapter->pmoal_handle,
				 &pstate_dfs->dfs_ts_head,
				 (pmlan_linked_list)pdfs_ts, MNULL, MNULL);
		adapter->callbacks.moal_mfree(adapter->pmoal_handle,
					      (t_u8 *)pdfs_ts);

		pdfs_ts =
			(wlan_dfs_timestamp_t *)util_peek_list(adapter->
							       pmoal_handle,
							       &pstate_dfs->
							       dfs_ts_head,
							       MNULL, MNULL);
	}

	LEAVE();
}

/**
 *  @brief Initialize the 11h parameters and enable 11h when starting an IBSS
 *
 *  @param pmpriv Pointer to mlan_private structure
 *
 *  @return      N/A
 */
t_void
wlan_11h_priv_init(mlan_private *pmpriv)
{
	wlan_11h_interface_state_t *pistate_11h = &pmpriv->intf_state_11h;

	ENTER();

	pistate_11h->is_11h_enabled = MTRUE;
	pistate_11h->is_11h_active = MFALSE;
	pistate_11h->adhoc_auto_sel_chan = MTRUE;
	pistate_11h->tx_disabled = MFALSE;
	pistate_11h->dfs_slave_csa_chan = 0;
	pistate_11h->dfs_slave_csa_expire_at_sec = 0;

	LEAVE();
}

/**
 *  @brief Retrieve a randomly selected starting channel if needed for 11h
 *
 *  If 11h is enabled and an A-Band channel start band preference
 *    configured in the driver, the start channel must be random in order
 *    to meet with
 *
 *  @param priv  Private driver information structure
 *
 *  @return      Starting channel
 */
t_u8
wlan_11h_get_adhoc_start_channel(mlan_private *priv)
{
	t_u8 start_chn;
	mlan_adapter *adapter = priv->adapter;
	t_u32 region;
	t_u32 rand_entry;
	region_chan_t *chn_tbl;
	t_u8 rand_tries = 0;

	ENTER();

	/*
	 * Set start_chn to the Default.  Used if 11h is disabled or the band
	 *   does not require 11h support.
	 */
	start_chn = DEFAULT_AD_HOC_CHANNEL;

	/*
	 * Check that we are looking for a channel in the A Band
	 */
	if ((adapter->adhoc_start_band & BAND_A)
	    || (adapter->adhoc_start_band & BAND_AN)
		) {
		/*
		 * Set default to the A Band default.
		 * Used if random selection fails
		 * or if 11h is not enabled
		 */
		start_chn = DEFAULT_AD_HOC_CHANNEL_A;

		/*
		 * Check that 11h is enabled in the driver
		 */
		if (wlan_11h_is_enabled(priv)) {
			/*
			 * Search the region_channel tables for a channel table
			 *   that is marked for the A Band.
			 */
			for (region = 0; (region < MAX_REGION_CHANNEL_NUM);
			     region++) {
				chn_tbl = &adapter->region_channel[region];

				/* Check if table is valid and marked for A Band */
				if (chn_tbl->valid
				    && chn_tbl->region == adapter->region_code
				    && chn_tbl->band & BAND_A) {
					/*
					 * Set the start channel.  Get a random
					 * number and use it to pick an entry
					 * in the table between 0 and the number
					 * of channels in the table (NumCFP).
					 */
					do {
						rand_entry =
							wlan_11h_get_random_num
							(adapter) %
							chn_tbl->num_cfp;
						start_chn =
							(t_u8)chn_tbl->
							pcfp[rand_entry].
							channel;
					} while ((wlan_11h_is_channel_under_nop
						  (adapter, start_chn) ||
						  ((adapter->state_rdh.stage ==
						    RDH_GET_INFO_CHANNEL) &&
						   wlan_11h_radar_detect_required
						   (priv, start_chn)))
						 && (++rand_tries <
						     MAX_RANDOM_CHANNEL_RETRIES));
				}
			}
		}
	}

	PRINTM(MINFO, "11h: %s: AdHoc Channel set to %u\n",
	       wlan_11h_is_enabled(priv) ? "Enabled" : "Disabled", start_chn);

	LEAVE();
	return start_chn;
}

/**
 *  @brief Retrieve channel closed for operation by Channel Switch Announcement
 *
 *  After receiving CSA, we must not transmit in any form on the original
 *    channel for a certain duration.  This checks the time, and returns
 *    the channel if valid.
 *
 *  @param priv  Private driver information structure
 *
 *  @return      Closed channel, else 0
 */
t_u8
wlan_11h_get_csa_closed_channel(mlan_private *priv)
{
	t_u32 sec, usec;

	ENTER();

	if (!priv->intf_state_11h.dfs_slave_csa_chan) {
		LEAVE();
		return 0;
	}

	/* have csa channel, check if expired or not */
	priv->adapter->callbacks.moal_get_system_time(priv->adapter->
						      pmoal_handle, &sec,
						      &usec);
	if (sec > priv->intf_state_11h.dfs_slave_csa_expire_at_sec) {
		/* expired:  remove channel from blacklist table, and clear vars */
		wlan_set_chan_blacklist(priv, BAND_A,
					priv->intf_state_11h.dfs_slave_csa_chan,
					MFALSE);
		priv->intf_state_11h.dfs_slave_csa_chan = 0;
		priv->intf_state_11h.dfs_slave_csa_expire_at_sec = 0;
	}

	LEAVE();
	return priv->intf_state_11h.dfs_slave_csa_chan;
}

/**
 *  @brief Check if the current region's regulations require the input channel
 *         to be scanned for radar.
 *
 *  Based on statically defined requirements for sub-bands per regulatory
 *    agency requirements.
 *
 *  Used in adhoc start to determine if channel availability check is required
 *
 *  @param priv    Private driver information structure
 *  @param channel Channel to determine radar detection requirements
 *
 *  @return
 *    - MTRUE if radar detection is required
 *    - MFALSE otherwise
 */
/**  @sa wlan_11h_issue_radar_detect
 */
t_bool
wlan_11h_radar_detect_required(mlan_private *priv, t_u8 channel)
{
	t_bool required = MFALSE;

	ENTER();

	/*
	 * No checks for 11h or measurement code being enabled is placed here
	 * since regulatory requirements exist whether we support them or not.
	 */

	required = wlan_get_cfp_radar_detect(priv, channel);

	if (!priv->adapter->region_code)
		PRINTM(MINFO, "11h: Radar detection in CFP code[BG:%#x, A:%#x] "
		       "is %srequired for channel %d\n",
		       priv->adapter->cfp_code_bg, priv->adapter->cfp_code_a,
		       (required ? "" : "not "), channel);
	else
		PRINTM(MINFO, "11h: Radar detection in region %#02x "
		       "is %srequired for channel %d\n",
		       priv->adapter->region_code, (required ? "" : "not "),
		       channel);

	if (required == MTRUE && priv->media_connected == MTRUE
	    && priv->curr_bss_params.bss_descriptor.channel == channel) {
		required = MFALSE;

		PRINTM(MINFO, "11h: Radar detection not required. "
		       "Already operating on the channel\n");
	}

	LEAVE();
	return required;
}

/**
 *  @brief Perform a radar measurement if required on given channel
 *
 *  Check to see if the provided channel requires a channel availability
 *    check (60 second radar detection measurement).  If required, perform
 *    measurement, stalling calling thread until the measurement completes
 *    and then report result.
 *
 *  Used when starting an adhoc or AP network.
 *
 *  @param priv         Private driver information structure
 *  @param pioctl_req   Pointer to IOCTL request buffer
 *  @param channel      Channel on which to perform radar measurement
 *  @param bandcfg      Channel Band config structure
 *
 *  @return
 *    - MTRUE  if radar measurement request was successfully issued
 *    - MFALSE if radar detection is not required
 *    - < 0 for error during radar detection (if performed)
 *
 *  @sa wlan_11h_radar_detect_required
 */
t_s32
wlan_11h_issue_radar_detect(mlan_private *priv,
			    pmlan_ioctl_req pioctl_req,
			    t_u8 channel, Band_Config_t bandcfg)
{
	t_s32 ret;
	HostCmd_DS_CHAN_RPT_REQ chan_rpt_req;
	mlan_adapter *pmadapter = priv->adapter;
	mlan_ds_11h_cfg *ds_11hcfg = MNULL;

	ENTER();

	ret = wlan_11h_radar_detect_required(priv, channel);
	if (ret) {
		/* Prepare and issue CMD_CHAN_RPT_REQ. */
		memset(priv->adapter, &chan_rpt_req, 0x00,
		       sizeof(chan_rpt_req));

		chan_rpt_req.chan_desc.startFreq = START_FREQ_11A_BAND;

		if (pmadapter->chanrpt_param_bandcfg) {
			chan_rpt_req.chan_desc.bandcfg = bandcfg;
		} else {
			*((t_u8 *)&chan_rpt_req.chan_desc.bandcfg) =
				(t_u8)bandcfg.chanWidth;
		}

		chan_rpt_req.chan_desc.chanNum = channel;
		chan_rpt_req.millisec_dwell_time =
			WLAN_11H_CHANNEL_AVAIL_CHECK_DURATION;

		/* ETSI new requirement for ch 120, 124 and 128 */
		if (wlan_is_etsi_country(pmadapter, pmadapter->country_code)) {
			if (channel == 120 || channel == 124 || channel == 128) {
				chan_rpt_req.millisec_dwell_time =
					WLAN_11H_CHANNEL_AVAIL_CHECK_DURATION *
					10;
			}
			if (channel == 116 &&
			    ((bandcfg.chanWidth == CHAN_BW_40MHZ)
			    )) {
				chan_rpt_req.millisec_dwell_time =
					WLAN_11H_CHANNEL_AVAIL_CHECK_DURATION *
					10;
			}
		}

		/* Save dwell time information to be used later in moal */
		if (pioctl_req) {
			ds_11hcfg = (mlan_ds_11h_cfg *)pioctl_req->pbuf;
			if (!ds_11hcfg->param.chan_rpt_req.host_based) {
				ds_11hcfg->param.chan_rpt_req.
					millisec_dwell_time =
					chan_rpt_req.millisec_dwell_time;
			}
		}
#ifdef DFS_TESTING_SUPPORT
		if (priv->adapter->dfs_test_params.user_cac_period_msec) {
			PRINTM(MCMD_D,
			       "dfs_testing - user CAC period=%d (msec)\n",
			       priv->adapter->dfs_test_params.
			       user_cac_period_msec);
			chan_rpt_req.millisec_dwell_time =
				priv->adapter->dfs_test_params.
				user_cac_period_msec;
		}
#endif

		PRINTM(MMSG, "11h: issuing DFS Radar check for channel=%d."
		       "  Please wait for response...\n", channel);

		ret = wlan_prepare_cmd(priv, HostCmd_CMD_CHAN_REPORT_REQUEST,
				       HostCmd_ACT_GEN_SET, 0,
				       (t_void *)pioctl_req,
				       (t_void *)&chan_rpt_req);
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Checks if a radar measurement was performed on channel,
 *         and if so, whether radar was detected on it.
 *
 *  Used when starting an adhoc network.
 *
 *  @param priv         Private driver information structure
 *  @param chan         Channel to check upon
 *
 *  @return
 *    - MLAN_STATUS_SUCCESS if no radar on channel
 *    - MLAN_STATUS_FAILURE if radar was found on channel
 *    - (TBD??) MLAN_STATUS_PENDING if radar report NEEDS TO BE REISSUED
 *
 *  @sa wlan_11h_issue_radar_detect
 *  @sa wlan_11h_process_start
 */
mlan_status
wlan_11h_check_chan_report(mlan_private *priv, t_u8 chan)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	wlan_dfs_device_state_t *pstate_dfs = &priv->adapter->state_dfs;
	t_u32 sec, usec;

	ENTER();

	/* check report we hold is valid or not */
	priv->adapter->callbacks.moal_get_system_time(priv->adapter->
						      pmoal_handle, &sec,
						      &usec);

	PRINTM(MINFO, "11h: %s()\n", __func__);
	PRINTM(MINFO, "- sec_now=%d, sec_report=%d.\n",
	       sec, pstate_dfs->dfs_report_time_sec);
	PRINTM(MINFO, "- rpt_channel=%d, rpt_radar=%d.\n",
	       pstate_dfs->dfs_check_channel, pstate_dfs->dfs_radar_found);

	if ((!pstate_dfs->dfs_check_pending) &&
	    (chan == pstate_dfs->dfs_check_channel) &&
	    ((sec - pstate_dfs->dfs_report_time_sec) <
	     MAX_DFS_REPORT_USABLE_AGE_SEC)) {
		/* valid and not out-dated, check if radar */
		if (pstate_dfs->dfs_radar_found) {
			PRINTM(MMSG, "Radar was detected on channel %d.\n",
			       chan);
			ret = MLAN_STATUS_FAILURE;
		}
	} else {
		/* When Cache is not valid. This is required during extending cache
		 * validity during bss_stop
		 */
		pstate_dfs->dfs_check_channel = 0;

		/*TODO:  reissue report request if not pending.
		 *       BUT HOW to make the code wait for it???
		 * For now, just fail since we don't have the info. */

		ret = MLAN_STATUS_PENDING;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Process an TLV buffer for a pending BSS Adhoc start command.
 *
 *  Activate 11h functionality in the firmware if driver has is enabled
 *    for 11h (configured by the application via IOCTL).
 *
 *  @param priv          Private driver information structure
 *  @param ppbuffer      Output parameter: Pointer to the TLV output buffer,
 *                       modified on return to point after the appended 11h TLVs
 *  @param pcap_info     Pointer to the capability info for the BSS to join
 *  @param channel       Channel on which we are starting the IBSS
 *  @param p11h_bss_info Input/Output parameter: Pointer to the 11h BSS
 *                       information for this network that we are establishing.
 *                       11h sensed flag set on output if warranted.
 *
 *  @return
 *      - MLAN_STATUS_SUCCESS if 11h is disabled
 *      - Integer number of bytes appended to the TLV output buffer (ppbuffer)
 *      - < 0 for error (e.g. radar detected on channel)
 */
t_s32
wlan_11h_process_start(mlan_private *priv,
		       t_u8 **ppbuffer,
		       IEEEtypes_CapInfo_t *pcap_info,
		       t_u32 channel, wlan_11h_bss_info_t *p11h_bss_info)
{
	mlan_adapter *adapter = priv->adapter;
	t_s32 ret = MLAN_STATUS_SUCCESS;
	t_bool is_dfs_chan = MFALSE;

	ENTER();
	if (wlan_11h_is_enabled(priv)
	    && ((adapter->adhoc_start_band & BAND_A)
		|| (adapter->adhoc_start_band & BAND_AN)
	    )
		) {
		if (!wlan_11d_is_enabled(priv)) {
			/* No use having 11h enabled without 11d enabled */
			wlan_11d_enable(priv, MNULL, ENABLE_11D);
#ifdef STA_SUPPORT
			wlan_11d_create_dnld_countryinfo(priv,
							 adapter->
							 adhoc_start_band);
#endif
		}

		/*
		 * Activate 11h functions in firmware,
		 * turns on capability bit
		 */
		wlan_11h_activate(priv, MNULL, MTRUE);
		pcap_info->spectrum_mgmt = MTRUE;

		/* If using a DFS channel, enable radar detection. */
		is_dfs_chan = wlan_11h_radar_detect_required(priv, channel);
		if (is_dfs_chan) {
			if (!wlan_11h_is_master_radar_det_active(priv))
				wlan_11h_config_master_radar_det(priv, MTRUE);
		}
		wlan_11h_check_update_radar_det_state(priv);

		/* Set flag indicating this BSS we are starting is using 11h */
		p11h_bss_info->sensed_11h = MTRUE;

		if (is_dfs_chan) {
			/* check if this channel is under NOP */
			if (wlan_11h_is_channel_under_nop(adapter, channel))
				ret = MLAN_STATUS_FAILURE;
			/* check last channel report, if this channel is free of radar */
			if (ret == MLAN_STATUS_SUCCESS)
				ret = wlan_11h_check_chan_report(priv, channel);
		}
		if (ret == MLAN_STATUS_SUCCESS)
			ret = wlan_11h_process_adhoc(priv, ppbuffer, channel,
						     MNULL);
		else
			ret = MLAN_STATUS_FAILURE;
	} else {
		/* Deactivate 11h functions in the firmware */
		wlan_11h_activate(priv, MNULL, MFALSE);
		pcap_info->spectrum_mgmt = MFALSE;
		wlan_11h_check_update_radar_det_state(priv);
	}
	LEAVE();
	return ret;
}

/**
 *  @brief Process an TLV buffer for a pending BSS Join command for
 *         both adhoc and infra networks
 *
 *  The TLV command processing for a BSS join for either adhoc or
 *    infrastructure network is performed with this function.  The
 *    capability bits are inspected for the IBSS flag and the appropriate
 *    local routines are called to setup the necessary TLVs.
 *
 *  Activate 11h functionality in the firmware if the spectrum management
 *    capability bit is found in the network information for the BSS we are
 *    joining.
 *
 *  @param priv          Private driver information structure
 *  @param ppbuffer      Output parameter: Pointer to the TLV output buffer,
 *                       modified on return to point after the appended 11h TLVs
 *  @param pcap_info     Pointer to the capability info for the BSS to join
 *  @param band          Band on which we are joining the BSS
 *  @param channel       Channel on which we are joining the BSS
 *  @param p11h_bss_info Pointer to the 11h BSS information for this
 *                       network that was parsed out of the scan response.
 *
 *  @return              Integer number of bytes appended to the TLV output
 *                       buffer (ppbuffer), MLAN_STATUS_FAILURE (-1),
 *                       or MLAN_STATUS_SUCCESS (0)
 */
t_s32
wlan_11h_process_join(mlan_private *priv,
		      t_u8 **ppbuffer,
		      IEEEtypes_CapInfo_t *pcap_info,
		      t_u8 band,
		      t_u32 channel, wlan_11h_bss_info_t *p11h_bss_info)
{
	t_s32 ret = 0;

	ENTER();

	if (priv->media_connected == MTRUE) {
		if (wlan_11h_is_active(priv) == p11h_bss_info->sensed_11h) {
			/*
			 * Assume DFS parameters are the same for roaming as long as
			 * the current & next APs have the same spectrum mgmt capability
			 * bit setting
			 */
			ret = MLAN_STATUS_SUCCESS;

		} else {
			/* No support for roaming between DFS/non-DFS yet */
			ret = MLAN_STATUS_FAILURE;
		}

		LEAVE();
		return ret;
	}

	if (p11h_bss_info->sensed_11h) {
		if (!wlan_11d_is_enabled(priv)) {
			/* No use having 11h enabled without 11d enabled */
			wlan_11d_enable(priv, MNULL, ENABLE_11D);
#ifdef STA_SUPPORT
			wlan_11d_parse_dnld_countryinfo(priv,
							priv->
							pattempted_bss_desc);
#endif
		}
		/*
		 * Activate 11h functions in firmware,
		 * turns on capability bit
		 */
		wlan_11h_activate(priv, MNULL, MTRUE);
		pcap_info->spectrum_mgmt = MTRUE;

		/* If using a DFS channel, enable radar detection. */
		if ((band & BAND_A) &&
		    wlan_11h_radar_detect_required(priv, channel)) {
			if (!wlan_11h_is_slave_radar_det_active(priv))
				wlan_11h_config_slave_radar_det(priv, MTRUE);
		}
		wlan_11h_check_update_radar_det_state(priv);

		if (pcap_info->ibss) {
			PRINTM(MINFO, "11h: Adhoc join: Sensed\n");
			ret = wlan_11h_process_adhoc(priv, ppbuffer, channel,
						     p11h_bss_info);
		} else {
			PRINTM(MINFO, "11h: Infra join: Sensed\n");
			ret = wlan_11h_process_infra_join(priv, ppbuffer, band,
							  channel,
							  p11h_bss_info);
		}
	} else {
		/* Deactivate 11h functions in the firmware */
		wlan_11h_activate(priv, MNULL, MFALSE);
		pcap_info->spectrum_mgmt = MFALSE;
		wlan_11h_check_update_radar_det_state(priv);
	}

	LEAVE();
	return ret;
}

/**
 *
 *  @brief  Prepare the HostCmd_DS_Command structure for an 11h command.
 *
 *  Use the Command field to determine if the command being set up is for
 *     11h and call one of the local command handlers accordingly for:
 *
 *        - HostCmd_CMD_802_11_TPC_ADAPT_REQ
 *        - HostCmd_CMD_802_11_TPC_INFO
 *        - HostCmd_CMD_802_11_CHAN_SW_ANN
 */
/**       - HostCmd_CMD_CHAN_REPORT_REQUEST
 */
/**
 *  @param priv      Private driver information structure
 *  @param pcmd_ptr  Output parameter: Pointer to the command being prepared
 *                   for the firmware
 *  @param pinfo_buf Void buffer pass through with data necessary for a
 *                   specific command type
 */
/**  @return          MLAN_STATUS_SUCCESS, MLAN_STATUS_FAILURE
 *                    or MLAN_STATUS_PENDING
 */
/**  @sa wlan_11h_cmd_tpc_request
 *  @sa wlan_11h_cmd_tpc_info
 *  @sa wlan_11h_cmd_chan_sw_ann
 */
/** @sa wlan_11h_cmd_chan_report_req
 */
mlan_status
wlan_11h_cmd_process(mlan_private *priv,
		     HostCmd_DS_COMMAND *pcmd_ptr, const t_void *pinfo_buf)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	switch (pcmd_ptr->command) {
	case HostCmd_CMD_802_11_TPC_ADAPT_REQ:
		ret = wlan_11h_cmd_tpc_request(priv, pcmd_ptr, pinfo_buf);
		break;
	case HostCmd_CMD_802_11_TPC_INFO:
		ret = wlan_11h_cmd_tpc_info(priv, pcmd_ptr, pinfo_buf);
		break;
	case HostCmd_CMD_802_11_CHAN_SW_ANN:
		ret = wlan_11h_cmd_chan_sw_ann(priv, pcmd_ptr, pinfo_buf);
		break;
	case HostCmd_CMD_CHAN_REPORT_REQUEST:
		ret = wlan_11h_cmd_chan_rpt_req(priv, pcmd_ptr, pinfo_buf);
		break;
	default:
		ret = MLAN_STATUS_FAILURE;
	}

	pcmd_ptr->command = wlan_cpu_to_le16(pcmd_ptr->command);
	pcmd_ptr->size = wlan_cpu_to_le16(pcmd_ptr->size);

	LEAVE();
	return ret;
}

/**
 *  @brief Handle the command response from the firmware if from an 11h command
 *
 *  Use the Command field to determine if the command response being
 *    is for 11h.  Call the local command response handler accordingly for:
 *
 *        - HostCmd_CMD_802_11_TPC_ADAPT_REQ
 *        - HostCmd_CMD_802_11_TPC_INFO
 *        - HostCmd_CMD_802_11_CHAN_SW_ANN
 */
/**       - HostCmd_CMD_CHAN_REPORT_REQUEST
 */
/**
 *  @param priv  Private driver information structure
 *  @param resp  HostCmd_DS_COMMAND struct returned from the firmware
 *               command
 *
 *  @return      MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11h_cmdresp_process(mlan_private *priv, const HostCmd_DS_COMMAND *resp)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	switch (resp->command) {
	case HostCmd_CMD_802_11_TPC_ADAPT_REQ:
		HEXDUMP("11h: TPC REQUEST Rsp:", (t_u8 *)resp,
			(t_u32)resp->size);
		memcpy(priv->adapter, priv->adapter->curr_cmd->pdata_buf,
		       &resp->params.tpc_req,
		       sizeof(HostCmd_DS_802_11_TPC_ADAPT_REQ));
		break;

	case HostCmd_CMD_802_11_TPC_INFO:
		HEXDUMP("11h: TPC INFO Rsp Data:", (t_u8 *)resp,
			(t_u32)resp->size);
		break;

	case HostCmd_CMD_802_11_CHAN_SW_ANN:
		PRINTM(MINFO, "11h: Ret ChSwAnn: Sz=%u, Seq=%u, Ret=%u\n",
		       resp->size, resp->seq_num, resp->result);
		break;

	case HostCmd_CMD_CHAN_REPORT_REQUEST:
		priv->adapter->state_dfs.dfs_check_priv = priv;
		priv->adapter->state_dfs.dfs_check_pending = MTRUE;

		if (resp->params.chan_rpt_req.millisec_dwell_time == 0) {
			/* from wlan_11h_ioctl_dfs_cancel_chan_report */
			priv->adapter->state_dfs.dfs_check_pending = MFALSE;
			priv->adapter->state_dfs.dfs_check_priv = MNULL;
			priv->adapter->state_dfs.dfs_check_channel = 0;
			PRINTM(MINFO, "11h: Cancelling Chan Report \n");
		} else {
			PRINTM(MERROR,
			       "11h: Ret ChanRptReq.  Set dfs_check_pending and wait"
			       " for EVENT_CHANNEL_REPORT.\n");
		}

		break;

	default:
		ret = MLAN_STATUS_FAILURE;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Process an element from a scan response, copy relevant info for 11h
 *
 *  @param pmadapter Pointer to mlan_adapter
 *  @param p11h_bss_info Output parameter: Pointer to the 11h BSS information
 *                       for the network that is being processed
 *  @param pelement      Pointer to the current IE we are inspecting for 11h
 *                       relevance
 *
 *  @return              MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11h_process_bss_elem(mlan_adapter *pmadapter,
			  wlan_11h_bss_info_t *p11h_bss_info,
			  const t_u8 *pelement)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u8 element_len = *((t_u8 *)pelement + 1);

	ENTER();
	switch (*pelement) {
	case POWER_CONSTRAINT:
		PRINTM(MINFO, "11h: Power Constraint IE Found\n");
		p11h_bss_info->sensed_11h = MTRUE;
		memcpy(pmadapter, &p11h_bss_info->power_constraint, pelement,
		       MIN((element_len + sizeof(IEEEtypes_Header_t)),
			   sizeof(IEEEtypes_PowerConstraint_t)));
		p11h_bss_info->power_constraint.len =
			MIN(element_len, (sizeof(IEEEtypes_PowerConstraint_t)
					  - sizeof(IEEEtypes_Header_t)));
		break;

	case POWER_CAPABILITY:
		PRINTM(MINFO, "11h: Power Capability IE Found\n");
		p11h_bss_info->sensed_11h = MTRUE;
		memcpy(pmadapter, &p11h_bss_info->power_capability, pelement,
		       MIN((element_len + sizeof(IEEEtypes_Header_t)),
			   sizeof(IEEEtypes_PowerCapability_t)));
		p11h_bss_info->power_capability.len =
			MIN(element_len, (sizeof(IEEEtypes_PowerCapability_t)
					  - sizeof(IEEEtypes_Header_t)));
		break;

	case TPC_REPORT:
		PRINTM(MINFO, "11h: Tpc Report IE Found\n");
		p11h_bss_info->sensed_11h = MTRUE;
		memcpy(pmadapter, &p11h_bss_info->tpc_report, pelement,
		       MIN((element_len + sizeof(IEEEtypes_Header_t)),
			   sizeof(IEEEtypes_TPCReport_t)));
		p11h_bss_info->tpc_report.len =
			MIN(element_len, (sizeof(IEEEtypes_TPCReport_t)
					  - sizeof(IEEEtypes_Header_t)));
		break;

	case CHANNEL_SWITCH_ANN:
		PRINTM(MINFO, "11h: Channel Switch Ann IE Found\n");
		p11h_bss_info->sensed_11h = MTRUE;
		memcpy(pmadapter, &p11h_bss_info->chan_switch_ann, pelement,
		       MIN((element_len + sizeof(IEEEtypes_Header_t)),
			   sizeof(IEEEtypes_ChanSwitchAnn_t)));
		p11h_bss_info->chan_switch_ann.len =
			MIN(element_len, (sizeof(IEEEtypes_ChanSwitchAnn_t)
					  - sizeof(IEEEtypes_Header_t)));
		break;

	case QUIET:
		PRINTM(MINFO, "11h: Quiet IE Found\n");
		p11h_bss_info->sensed_11h = MTRUE;
		memcpy(pmadapter, &p11h_bss_info->quiet, pelement,
		       MIN((element_len + sizeof(IEEEtypes_Header_t)),
			   sizeof(IEEEtypes_Quiet_t)));
		p11h_bss_info->quiet.len =
			MIN(element_len, (sizeof(IEEEtypes_Quiet_t)
					  - sizeof(IEEEtypes_Header_t)));
		break;

	case IBSS_DFS:
		PRINTM(MINFO, "11h: Ibss Dfs IE Found\n");
		p11h_bss_info->sensed_11h = MTRUE;
		memcpy(pmadapter, &p11h_bss_info->ibss_dfs, pelement,
		       MIN((element_len + sizeof(IEEEtypes_Header_t)),
			   sizeof(IEEEtypes_IBSS_DFS_t)));
		p11h_bss_info->ibss_dfs.len =
			MIN(element_len, (sizeof(IEEEtypes_IBSS_DFS_t)
					  - sizeof(IEEEtypes_Header_t)));
		break;

	case SUPPORTED_CHANNELS:
	case TPC_REQUEST:
		/*
		 * These elements are not in beacons/probe responses.
		 * Included here to cover set of enumerated 11h elements.
		 */
		break;

	default:
		ret = MLAN_STATUS_FAILURE;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Driver handling for CHANNEL_SWITCH_ANN event
 *
 *  @param priv Pointer to mlan_private
 *
 *  @return MLAN_STATUS_SUCCESS, MLAN_STATUS_FAILURE or MLAN_STATUS_PENDING
 */
mlan_status
wlan_11h_handle_event_chanswann(mlan_private *priv)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
#ifdef STA_SUPPORT
	mlan_deauth_param deauth_param;
#endif
	t_u32 sec, usec;

	ENTER();
	priv->adapter->state_11h.recvd_chanswann_event = MTRUE;

	/* unlikely:  clean up previous csa if still on-going */
	if (priv->intf_state_11h.dfs_slave_csa_chan) {
		wlan_set_chan_blacklist(priv, BAND_A,
					priv->intf_state_11h.dfs_slave_csa_chan,
					MFALSE);
	}

	/* record channel and time of occurence */
	priv->intf_state_11h.dfs_slave_csa_chan =
		priv->curr_bss_params.bss_descriptor.channel;
	priv->adapter->callbacks.moal_get_system_time(priv->adapter->
						      pmoal_handle, &sec,
						      &usec);
	priv->intf_state_11h.dfs_slave_csa_expire_at_sec =
		sec + DFS_CHAN_MOVE_TIME;

#ifdef STA_SUPPORT
	/* do directed deauth.  recvd_chanswann_event flag will cause different reason code */
	PRINTM(MINFO, "11h: handle_event_chanswann() - sending deauth\n");
	memcpy(priv->adapter, deauth_param.mac_addr,
	       &priv->curr_bss_params.bss_descriptor.mac_address,
	       MLAN_MAC_ADDR_LENGTH);
	deauth_param.reason_code = DEF_DEAUTH_REASON_CODE;
	ret = wlan_disconnect(priv, MNULL, &deauth_param);

	/* clear region table so next scan will be all passive */
	PRINTM(MINFO, "11h: handle_event_chanswann() - clear region table\n");
	wlan_11d_clear_parsedtable(priv);

	/* add channel to blacklist table */
	PRINTM(MINFO,
	       "11h: handle_event_chanswann() - scan blacklist csa channel\n");
	wlan_set_chan_blacklist(priv, BAND_A,
				priv->intf_state_11h.dfs_slave_csa_chan, MTRUE);
#endif

	priv->adapter->state_11h.recvd_chanswann_event = MFALSE;
	LEAVE();
	return ret;
}

#ifdef DFS_TESTING_SUPPORT
/**
 *  @brief 802.11h DFS Testing configuration
 *
 *  @param pmadapter    Pointer to mlan_adapter
 *  @param pioctl_req   Pointer to mlan_ioctl_req
 *
 *  @return MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11h_ioctl_dfs_testing(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_ds_11h_cfg *ds_11hcfg = MNULL;
	mlan_ds_11h_dfs_testing *dfs_test = MNULL;
	wlan_dfs_testing_settings_t *pdfs_test_params = MNULL;

	ENTER();

	ds_11hcfg = (mlan_ds_11h_cfg *)pioctl_req->pbuf;
	dfs_test = &ds_11hcfg->param.dfs_testing;
	pdfs_test_params = &pmadapter->dfs_test_params;

	if (pioctl_req->action == MLAN_ACT_GET) {
		dfs_test->usr_cac_period_msec =
			pdfs_test_params->user_cac_period_msec;
		dfs_test->usr_nop_period_sec =
			pdfs_test_params->user_nop_period_sec;
		dfs_test->usr_no_chan_change =
			pdfs_test_params->no_channel_change_on_radar;
		dfs_test->usr_fixed_new_chan =
			pdfs_test_params->fixed_new_channel_on_radar;
	} else {
		pdfs_test_params->user_cac_period_msec =
			dfs_test->usr_cac_period_msec;
		pdfs_test_params->user_nop_period_sec =
			dfs_test->usr_nop_period_sec;
		pdfs_test_params->no_channel_change_on_radar =
			dfs_test->usr_no_chan_change;
		pdfs_test_params->fixed_new_channel_on_radar =
			dfs_test->usr_fixed_new_chan;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief 802.11h IOCTL to handle channel NOP status check
 *  @brief If given channel is under NOP, return a new non-dfs
 *  @brief channel
 *
 *  @param pmadapter    Pointer to mlan_adapter
 *  @param pioctl_req   Pointer to mlan_ioctl_req
 *
 *  @return MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11h_ioctl_get_channel_nop_info(pmlan_adapter pmadapter,
				    pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_11h_cfg *ds_11hcfg = MNULL;
	t_s32 ret = MLAN_STATUS_FAILURE;
	mlan_ds_11h_chan_nop_info *ch_nop_info = MNULL;

	ENTER();

	if (pioctl_req) {
		ds_11hcfg = (mlan_ds_11h_cfg *)pioctl_req->pbuf;
		ch_nop_info = &ds_11hcfg->param.ch_nop_info;

		if (pioctl_req->action == MLAN_ACT_GET) {
			ch_nop_info->chan_under_nop =
				wlan_11h_is_channel_under_nop(pmadapter,
							      ch_nop_info->
							      curr_chan);
			if (ch_nop_info->chan_under_nop) {
				wlan_11h_switch_non_dfs_chan(pmpriv,
							     &ch_nop_info->
							     new_chan.channel);
				if (ch_nop_info->chan_width == CHAN_BW_40MHZ)
					wlan_11h_update_bandcfg(&ch_nop_info->
								new_chan.
								bandcfg,
								ch_nop_info->
								new_chan.
								channel);
			}
		}
		ret = MLAN_STATUS_SUCCESS;
	}

	LEAVE();
	return ret;
}
#endif /* DFS_TESTING_SUPPORT */

/**
 *  @brief 802.11h DFS Channel Switch Count Configuration
 *
 *  @param pmadapter    Pointer to mlan_adapter
 *  @param pioctl_req   Pointer to mlan_ioctl_req
 *
 *  @return MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11h_ioctl_chan_switch_count(pmlan_adapter pmadapter,
				 pmlan_ioctl_req pioctl_req)
{
	mlan_ds_11h_cfg *ds_11hcfg = MNULL;
	t_s32 ret = MLAN_STATUS_FAILURE;

	ENTER();

	if (pioctl_req) {
		ds_11hcfg = (mlan_ds_11h_cfg *)pioctl_req->pbuf;

		if (pioctl_req->action == MLAN_ACT_GET) {
			ds_11hcfg->param.cs_count = pmadapter->dfs_cs_count;
		} else {
			pmadapter->dfs_cs_count = ds_11hcfg->param.cs_count;
		}
		ret = MLAN_STATUS_SUCCESS;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief 802.11h DFS cancel chan report
 *
 *  @param priv         Pointer to mlan_private
 *  @param pioctl_req   Pointer to mlan_ioctl_req
 *
 *  @return MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11h_ioctl_dfs_cancel_chan_report(mlan_private *priv,
				      pmlan_ioctl_req pioctl_req)
{
	mlan_ds_11h_cfg *ds_11hcfg = MNULL;
	HostCmd_DS_CHAN_RPT_REQ *chan_rpt_req = MNULL;
	t_s32 ret;

	ENTER();

	ds_11hcfg = (mlan_ds_11h_cfg *)pioctl_req->pbuf;

	chan_rpt_req =
		(HostCmd_DS_CHAN_RPT_REQ *)&ds_11hcfg->param.chan_rpt_req;

	ret = wlan_prepare_cmd(priv, HostCmd_CMD_CHAN_REPORT_REQUEST,
			       HostCmd_ACT_GEN_SET, 0,
			       (t_void *)pioctl_req, (t_void *)chan_rpt_req);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Check if channel is under NOP (Non-Occupancy Period)
 *  If so, the channel should not be used until the period expires.
 *
 *  @param pmadapter  Pointer to mlan_adapter
 *  @param channel    Channel number
 *
 *  @return MTRUE or MFALSE
 */
t_bool
wlan_11h_is_channel_under_nop(mlan_adapter *pmadapter, t_u8 channel)
{
	wlan_dfs_timestamp_t *pdfs_ts = MNULL;
	t_u32 now_sec, now_usec;
	t_bool ret = MFALSE;

	ENTER();
	pdfs_ts = wlan_11h_find_dfs_timestamp(pmadapter, channel);

	if (pdfs_ts && (pdfs_ts->channel == channel)
	    && (pdfs_ts->represents == DFS_TS_REPR_NOP_START)) {
		/* found NOP_start timestamp entry on channel */
		pmadapter->callbacks.moal_get_system_time(pmadapter->
							  pmoal_handle,
							  &now_sec, &now_usec);
#ifdef DFS_TESTING_SUPPORT
		if (pmadapter->dfs_test_params.user_nop_period_sec) {
			PRINTM(MCMD_D,
			       "dfs_testing - user NOP period=%d (sec)\n",
			       pmadapter->dfs_test_params.user_nop_period_sec);
			if ((now_sec - pdfs_ts->ts_sec) <=
			    pmadapter->dfs_test_params.user_nop_period_sec) {
				ret = MTRUE;
			}
		} else
#endif
		{
			if ((now_sec - pdfs_ts->ts_sec) <=
			    WLAN_11H_NON_OCCUPANCY_PERIOD)
				ret = MTRUE;
		}

		/* if entry is expired, remove it */
		if (!ret)
			wlan_11h_remove_dfs_timestamp(pmadapter, pdfs_ts);
		else
			PRINTM(MMSG,
			       "11h: channel %d is under NOP - can't use.\n",
			       channel);
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Driver handling for CHANNEL_REPORT_RDY event
 *  This event will have the channel report data appended.
 *
 *  @param priv     Pointer to mlan_private
 *  @param pevent   Pointer to mlan_event
 *
 *  @return MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11h_handle_event_chanrpt_ready(mlan_private *priv, mlan_event *pevent)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	HostCmd_DS_CHAN_RPT_RSP *pchan_rpt_rsp;
	MrvlIEtypes_Data_t *ptlv;
	MeasRptBasicMap_t *pmeas_rpt_basic;
	t_u8 *pbuffer;
	t_s32 evt_len;
	t_u16 tlv_len;
	t_u32 sec, usec;
	wlan_dfs_device_state_t *pstate_dfs = &priv->adapter->state_dfs;

	ENTER();
	pchan_rpt_rsp = (HostCmd_DS_CHAN_RPT_RSP *)&pevent->event_buf;
	DBG_HEXDUMP(MCMD_D, "11h: Event ChanRptReady (HostCmd_DS_CHAN_RPT_RSP)",
		    (t_u8 *)pchan_rpt_rsp, pevent->event_len);

	if (wlan_le32_to_cpu(pchan_rpt_rsp->cmd_result) ==
	    MLAN_CMD_RESULT_SUCCESS) {
		pbuffer = (t_u8 *)&pchan_rpt_rsp->tlv_buffer;
		evt_len = pevent->event_len;
		evt_len -=
			sizeof(HostCmd_DS_CHAN_RPT_RSP) -
			sizeof(pchan_rpt_rsp->tlv_buffer);

		while (evt_len >= sizeof(MrvlIEtypesHeader_t)) {
			ptlv = (MrvlIEtypes_Data_t *)pbuffer;
			tlv_len = wlan_le16_to_cpu(ptlv->header.len);

			switch (wlan_le16_to_cpu(ptlv->header.type)) {
			case TLV_TYPE_CHANRPT_11H_BASIC:
				pmeas_rpt_basic =
					(MeasRptBasicMap_t *)&ptlv->data;
				if (pmeas_rpt_basic->radar) {
					pstate_dfs->dfs_radar_found = MTRUE;
					PRINTM(MMSG,
					       "RADAR Detected on channel %d!\n",
					       pstate_dfs->dfs_check_channel);
					/* add channel to NOP list */
					wlan_11h_add_dfs_timestamp(priv->
								   adapter,
								   DFS_TS_REPR_NOP_START,
								   pstate_dfs->
								   dfs_check_channel);
				}
				break;

			default:
				break;
			}

			pbuffer += (tlv_len + sizeof(ptlv->header));
			evt_len -= (tlv_len + sizeof(ptlv->header));
			evt_len = (evt_len > 0) ? evt_len : 0;
		}
	} else {
		ret = MLAN_STATUS_FAILURE;
	}

	/* Update DFS structure. */
	priv->adapter->callbacks.moal_get_system_time(priv->adapter->
						      pmoal_handle, &sec,
						      &usec);
	pstate_dfs->dfs_report_time_sec = sec;
	pstate_dfs->dfs_check_pending = MFALSE;
	pstate_dfs->dfs_check_priv = MNULL;

	LEAVE();
	return ret;
}

/**
 *  @brief Check if RADAR_DETECTED handling is blocking data tx
 *
 *  @param pmadapter    Pointer to mlan_adapter
 *
 *  @return MTRUE or MFALSE
 */
t_bool
wlan_11h_radar_detected_tx_blocked(mlan_adapter *pmadapter)
{
	if (pmadapter->state_rdh.tx_block)
		return MTRUE;
	switch (pmadapter->state_rdh.stage) {
	case RDH_OFF:
	case RDH_CHK_INTFS:
	case RDH_STOP_TRAFFIC:
		return MFALSE;
	}
	return MTRUE;
}

/**
 *  @brief Callback for RADAR_DETECTED event driver handling
 *
 *  @param priv    Void pointer to mlan_private
 *
 *  @return MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11h_radar_detected_callback(t_void *priv)
{
	mlan_status ret;
	ENTER();
	ret = wlan_11h_radar_detected_handling(((mlan_private *)(priv))->
					       adapter, (mlan_private *)priv);
	LEAVE();
	return ret;
}

/**
 *  @brief Function for handling sta disconnect event in dfs_repeater mode
 *
 *  @param pmadapter	pointer to mlan_adapter
 *
 *  @return NONE
 */
void
wlan_dfs_rep_disconnect(mlan_adapter *pmadapter)
{
	mlan_private *priv_list[MLAN_MAX_BSS_NUM];
	mlan_private *pmpriv = MNULL;
	t_u8 pcount, i;

	memset(pmadapter, priv_list, 0x00, sizeof(priv_list));
	pcount = wlan_get_privs_by_cond(pmadapter, wlan_is_intf_active,
					priv_list);

	/* Stop all the active BSSes */
	for (i = 0; i < pcount; i++) {
		pmpriv = priv_list[i];

		if (GET_BSS_ROLE(pmpriv) != MLAN_BSS_ROLE_UAP)
			continue;

		if (wlan_11h_radar_detect_required(pmpriv,
						   pmadapter->dfsr_channel)) {
			wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_BSS_STOP,
					 HostCmd_ACT_GEN_SET, 0, MNULL, MNULL);
		}
	}
}

/**
 *  @brief Function for handling sta BW change event in dfs_repeater mode
 *
 *  @param pmadapter	pointer to mlan_adapter
 *
 *  @return NONE
 */
void
wlan_dfs_rep_bw_change(mlan_adapter *pmadapter)
{
	mlan_private *priv_list[MLAN_MAX_BSS_NUM];
	mlan_private *pmpriv = MNULL;
	t_u8 pcount, i;

	memset(pmadapter, priv_list, 0x00, sizeof(priv_list));
	pcount = wlan_get_privs_by_cond(pmadapter, wlan_is_intf_active,
					priv_list);
	if (pcount == 1) {
		pmpriv = priv_list[0];
		if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_STA) {
			PRINTM(MMSG, "dfs-repeater: BW change detected\n"
			       "no active priv's, skip event handling.\n");
			return;
		}
	}

	/* Stop all the active BSSes */
	for (i = 0; i < pcount; i++) {
		pmpriv = priv_list[i];

		if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_UAP) {

			/* Check if uAPs running on non-dfs channel. If they do
			 * then there is no need to restart the uAPs
			 */
			if (!wlan_11h_radar_detect_required(pmpriv,
							    pmadapter->
							    dfsr_channel))
				return;

			wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_BSS_STOP,
					 HostCmd_ACT_GEN_SET, 0, MNULL, MNULL);
		}
	}

	/* Start all old active BSSes */
	for (i = 0; i < pcount; i++) {
		pmpriv = priv_list[i];

		if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_UAP) {
			wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_BSS_START,
					 HostCmd_ACT_GEN_SET, 0, MNULL, MNULL);
		}
	}
}

/**
 *  @brief Update band config for the new channel
 *
 *  @param uap_band_cfg  uap's old channel's band configuration
 *  @param new_channel   new channel that the device is switching to
 *
 *  @return MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE or MLAN_STATUS_PENDING
 */
void
wlan_11h_update_bandcfg(IN Band_Config_t *uap_band_cfg, IN t_u8 new_channel)
{
	t_u8 chan_offset;
	ENTER();

	/* Update the channel offset for 20MHz, 40MHz and 80MHz
	 * Clear the channel bandwidth for 20MHz
	 * since channel switch could be happening from 40/80MHz to 20MHz
	 */
	chan_offset = wlan_get_second_channel_offset(new_channel);
	uap_band_cfg->chan2Offset = chan_offset;

	if (!chan_offset) {	/* 40MHz/80MHz */
		PRINTM(MCMD_D, "20MHz channel, clear channel bandwidth\n");
		uap_band_cfg->chanWidth = CHAN_BW_20MHZ;
	}
	LEAVE();
}

/**
 * @brief Get priv current index -- this is used to enter correct rdh_state during radar handling
 *
 * @param pmpriv           Pointer to mlan_private
 * @param pstate_rdh       Pointer to radar detected state handler
 *
 * @return                 MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_11h_get_priv_curr_idx(mlan_private *pmpriv,
			   wlan_radar_det_hndlg_state_t *pstate_rdh)
{
	t_bool found = MFALSE;
	ENTER();

	PRINTM(MINFO, "%s:pmpriv =%p\n", __func__, pmpriv);
	while ((++pstate_rdh->priv_curr_idx) < pstate_rdh->priv_list_count) {
		if (pmpriv == pstate_rdh->priv_list[pstate_rdh->priv_curr_idx]) {
			PRINTM(MINFO, "found matching priv: priv_idx=%d\n",
			       pstate_rdh->priv_curr_idx);
			found = MTRUE;
			break;
		}
	}
	return (found == MTRUE) ? MLAN_STATUS_SUCCESS : MLAN_STATUS_FAILURE;
}

/**
 *  @brief Driver handling for RADAR_DETECTED event
 *
 *  @param pmadapter    Pointer to mlan_adapter
 *  @param pmpriv       Pointer to mlan_private
 *
 *  @return MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE or MLAN_STATUS_PENDING
 */
mlan_status
wlan_11h_radar_detected_handling(mlan_adapter *pmadapter, mlan_private *pmpriv)
{
#ifdef DEBUG_LEVEL1
	const char *rdh_stage_str[] = {
		"RDH_OFF",
		"RDH_CHK_INTFS",
		"RDH_STOP_TRAFFIC",
		"RDH_GET_INFO_CHANNEL",
		"RDH_GET_INFO_BEACON_DTIM",
		"RDH_SET_CUSTOM_IE",
		"RDH_REM_CUSTOM_IE",
		"RDH_STOP_INTFS",
		"RDH_SET_NEW_CHANNEL",
		"RDH_RESTART_INTFS",
		"RDH_RESTART_TRAFFIC"
	};
#endif

	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 i;
	wlan_radar_det_hndlg_state_t *pstate_rdh = &pmadapter->state_rdh;

	ENTER();

	if (!pmpriv) {
		PRINTM(MERROR, "Invalid radar priv -- Exit radar handling\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	switch (pstate_rdh->stage) {
	case RDH_CHK_INTFS:
		PRINTM(MCMD_D, "%s(): stage(%d)=%s\n",
		       __func__, pstate_rdh->stage,
		       rdh_stage_str[pstate_rdh->stage]);

		/* get active interfaces */
		memset(pmadapter, pstate_rdh->priv_list, 0x00,
		       sizeof(pstate_rdh->priv_list));
		pstate_rdh->priv_list_count = wlan_get_privs_by_cond(pmadapter,
								     wlan_is_intf_active,
								     pstate_rdh->
								     priv_list);
		PRINTM(MCMD_D, "%s():  priv_list_count = %d\n", __func__,
		       pstate_rdh->priv_list_count);
		for (i = 0; i < pstate_rdh->priv_list_count; i++)
			PRINTM(MINFO, "%s():  priv_list[%d] = %p\n",
			       __func__, i, pstate_rdh->priv_list[i]);

		if (pstate_rdh->priv_list_count == 0) {
			/* no interfaces active... nothing to do */
			PRINTM(MMSG, "11h: Radar Detected - no active priv's,"
			       " skip event handling.\n");
			pstate_rdh->stage = RDH_OFF;
			PRINTM(MCMD_D, "%s(): finished - stage(%d)=%s\n",
			       __func__, pstate_rdh->stage,
			       rdh_stage_str[pstate_rdh->stage]);
			break;	/* EXIT CASE */
		}

		/* else: start handling */
		pstate_rdh->curr_channel = 0;
		pstate_rdh->new_channel = 0;
		memset(pmadapter, &(pstate_rdh->uap_band_cfg), 0,
		       sizeof(pstate_rdh->uap_band_cfg));
		pstate_rdh->max_bcn_dtim_ms = 0;
		pstate_rdh->priv_curr_idx = RDH_STAGE_FIRST_ENTRY_PRIV_IDX;
		pstate_rdh->stage = RDH_STOP_TRAFFIC;
		/* FALL THROUGH TO NEXT STAGE */

	case RDH_STOP_TRAFFIC:
		PRINTM(MCMD_D, "%s(): stage(%d)=%s\n",
		       __func__, pstate_rdh->stage,
		       rdh_stage_str[pstate_rdh->stage]);

		PRINTM(MMSG,
		       "11h: Radar Detected - stopping host tx traffic.\n");
		for (i = 0; i < pstate_rdh->priv_list_count; i++)
			wlan_11h_tx_disable(pstate_rdh->priv_list[i]);

		pstate_rdh->priv_curr_idx = RDH_STAGE_FIRST_ENTRY_PRIV_IDX;
		pstate_rdh->stage = RDH_GET_INFO_CHANNEL;
		/* FALL THROUGH TO NEXT STAGE */

	case RDH_GET_INFO_CHANNEL:
		PRINTM(MCMD_D, "%s(): stage(%d)=%s, priv_idx=%d\n",
		       __func__, pstate_rdh->stage,
		       rdh_stage_str[pstate_rdh->stage],
		       pstate_rdh->priv_curr_idx);

		/* here, prefer STA info over UAP info - one less CMD to send */
		if (pstate_rdh->priv_curr_idx == RDH_STAGE_FIRST_ENTRY_PRIV_IDX) {
#ifdef UAP_SUPPORT
			if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_UAP) {
				ret = wlan_11h_get_priv_curr_idx(pmpriv,
								 pstate_rdh);
				if (ret != MLAN_STATUS_SUCCESS) {
					PRINTM(MERROR,
					       "Unable to locate pmpriv in current active priv_list\n");
					break;	/* EXIT CASE */
				}

				/* send cmd to get first UAP's info */
				pmpriv->uap_state_chan_cb.pioctl_req_curr =
					MNULL;
				pmpriv->uap_state_chan_cb.get_chan_callback =
					wlan_11h_radar_detected_callback;
				ret = wlan_uap_get_channel(pmpriv);
				break;	/* EXIT CASE */
			} else
#endif
			{
				/* Assume all STAs on same channel, find first STA */
				MASSERT(pstate_rdh->priv_list_count > 0);
				for (i = 0; i < pstate_rdh->priv_list_count;
				     i++) {
					pmpriv = pstate_rdh->priv_list[i];
					if (GET_BSS_ROLE(pmpriv) ==
					    MLAN_BSS_ROLE_STA)
						break;
				}
				/* STA info kept in driver, just copy */
				pstate_rdh->curr_channel =
					pmpriv->curr_bss_params.bss_descriptor.
					channel;
			}
		}
#ifdef UAP_SUPPORT
		else if (pstate_rdh->priv_curr_idx <
			 pstate_rdh->priv_list_count) {
			/* repeat entry: UAP return with info */
			pstate_rdh->curr_channel =
				pmpriv->uap_state_chan_cb.channel;
			pstate_rdh->uap_band_cfg =
				pmpriv->uap_state_chan_cb.bandcfg;
			PRINTM(MCMD_D,
			       "%s(): uap_band_cfg=0x%02x curr_chan=%d, curr_idx=%d bss_role=%d\n",
			       __func__, pstate_rdh->uap_band_cfg,
			       pstate_rdh->curr_channel,
			       pstate_rdh->priv_curr_idx, GET_BSS_ROLE(pmpriv));
		}
#endif

		/* add channel to NOP list */
		wlan_11h_add_dfs_timestamp(pmadapter, DFS_TS_REPR_NOP_START,
					   pstate_rdh->curr_channel);

		/* choose new channel (!= curr channel) and move on */
#ifdef UAP_SUPPORT
		if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_UAP)
			pstate_rdh->new_channel =
				wlan_11h_get_uap_start_channel(pmpriv,
							       pmpriv->
							       uap_state_chan_cb.
							       bandcfg);
		else
#endif
			pstate_rdh->new_channel =
				wlan_11h_get_adhoc_start_channel(pmpriv);

		if (!pstate_rdh->new_channel || (pstate_rdh->new_channel == pstate_rdh->curr_channel)) {	/* report error */
			PRINTM(MERROR,
			       "%s():  ERROR - Failed to choose new_chan"
			       " (!= curr_chan) !!\n", __func__);
#ifdef UAP_SUPPORT
			if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_UAP) {
				ret = wlan_prepare_cmd(pmpriv,
						       HOST_CMD_APCMD_BSS_STOP,
						       HostCmd_ACT_GEN_SET, 0,
						       MNULL, MNULL);
				PRINTM(MERROR,
				       "STOP UAP and exit radar handling...\n");
				pstate_rdh->stage = RDH_OFF;
				break;	/* leads to exit case */
			}
#endif
		}
#ifdef DFS_TESTING_SUPPORT
		if (!pmadapter->dfs_test_params.no_channel_change_on_radar &&
		    pmadapter->dfs_test_params.fixed_new_channel_on_radar) {
			PRINTM(MCMD_D, "dfs_testing - user fixed new_chan=%d\n",
			       pmadapter->dfs_test_params.
			       fixed_new_channel_on_radar);
			pstate_rdh->new_channel =
				pmadapter->dfs_test_params.
				fixed_new_channel_on_radar;
		}
		/* applies to DFS with ECSA support */
		if (pmadapter->dfs_test_params.no_channel_change_on_radar) {
			pstate_rdh->new_channel = pstate_rdh->curr_channel;
		}
#endif
		PRINTM(MCMD_D, "%s():  curr_chan=%d, new_chan=%d\n",
		       __func__, pstate_rdh->curr_channel,
		       pstate_rdh->new_channel);

		pstate_rdh->priv_curr_idx = RDH_STAGE_FIRST_ENTRY_PRIV_IDX;
		pstate_rdh->stage = RDH_GET_INFO_BEACON_DTIM;
		/* FALL THROUGH TO NEXT STAGE */

	case RDH_GET_INFO_BEACON_DTIM:
		PRINTM(MCMD_D, "%s(): stage(%d)=%s, priv_idx=%d\n",
		       __func__, pstate_rdh->stage,
		       rdh_stage_str[pstate_rdh->stage],
		       pstate_rdh->priv_curr_idx);

#ifdef UAP_SUPPORT
		/* check all intfs in this stage to find longest period */
		/* UAP intf callback returning with info */
		if (pstate_rdh->priv_curr_idx < pstate_rdh->priv_list_count) {
			t_u16 bcn_dtim_msec;
			pmpriv = pstate_rdh->priv_list[pstate_rdh->
						       priv_curr_idx];
			PRINTM(MCMD_D, "%s():  uap.bcn_pd=%d, uap.dtim_pd=%d\n",
			       __func__,
			       pmpriv->uap_state_chan_cb.beacon_period,
			       pmpriv->uap_state_chan_cb.dtim_period);
			bcn_dtim_msec =
				(pmpriv->uap_state_chan_cb.beacon_period *
				 pmpriv->uap_state_chan_cb.dtim_period);
			if (bcn_dtim_msec > pstate_rdh->max_bcn_dtim_ms)
				pstate_rdh->max_bcn_dtim_ms = bcn_dtim_msec;
		}
#endif

		/* check next intf */
		while ((++pstate_rdh->priv_curr_idx) <
		       pstate_rdh->priv_list_count) {
			pmpriv = pstate_rdh->priv_list[pstate_rdh->
						       priv_curr_idx];

#ifdef UAP_SUPPORT
			if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_UAP) {
				pmpriv->uap_state_chan_cb.pioctl_req_curr =
					MNULL;
				pmpriv->uap_state_chan_cb.get_chan_callback =
					wlan_11h_radar_detected_callback;
				ret = wlan_uap_get_beacon_dtim(pmpriv);
				break;	/* leads to exit case */
			} else
#endif
			{	/* get STA info from driver and compare here */
				t_u16 bcn_pd_msec = 100;
				t_u16 dtim_pd_msec = 1;
				t_u16 bcn_dtim_msec;

				/* adhoc creator */
				if (wlan_11h_is_dfs_master(pmpriv)) {
					bcn_pd_msec = pmpriv->beacon_period;
				} else {
					bcn_pd_msec =
						pmpriv->curr_bss_params.
						bss_descriptor.beacon_period;
					/* if (priv->bss_mode != MLAN_BSS_MODE_IBSS) */
					/* TODO: mlan_scan.c needs to parse TLV 0x05 (TIM) for dtim_period */
				}
				PRINTM(MCMD_D,
				       "%s():  sta.bcn_pd=%d, sta.dtim_pd=%d\n",
				       __func__, bcn_pd_msec, dtim_pd_msec);
				bcn_dtim_msec = (bcn_pd_msec * dtim_pd_msec);
				if (bcn_dtim_msec > pstate_rdh->max_bcn_dtim_ms)
					pstate_rdh->max_bcn_dtim_ms =
						bcn_dtim_msec;
			}
		}

		if (pstate_rdh->priv_curr_idx < pstate_rdh->priv_list_count)
			break;	/* EXIT CASE (for UAP) */
		/* else */
		pstate_rdh->priv_curr_idx = RDH_STAGE_FIRST_ENTRY_PRIV_IDX;
		pstate_rdh->stage = RDH_SET_CUSTOM_IE;
		/* FALL THROUGH TO NEXT STAGE */

	case RDH_SET_CUSTOM_IE:
		PRINTM(MCMD_D, "%s(): stage(%d)=%s, priv_idx=%d\n",
		       __func__, pstate_rdh->stage,
		       rdh_stage_str[pstate_rdh->stage],
		       pstate_rdh->priv_curr_idx);

		/* add CHAN_SW IE - firmware will accept on any interface, and apply to all */
		if (pstate_rdh->priv_curr_idx == RDH_STAGE_FIRST_ENTRY_PRIV_IDX) {
			mlan_ioctl_req *pioctl_req = MNULL;

			ret = wlan_11h_prepare_custom_ie_chansw(pmadapter,
								&pioctl_req,
								MTRUE);
			if ((ret != MLAN_STATUS_SUCCESS) || !pioctl_req) {
				PRINTM(MERROR,
				       "%s(): Error in preparing CHAN_SW IE.\n",
				       __func__);
				break;	/* EXIT CASE */
			}

			PRINTM(MMSG,
			       "11h: Radar Detected - adding CHAN_SW IE to interfaces.\n");
			ret = wlan_11h_get_priv_curr_idx(pmpriv, pstate_rdh);
			if (ret != MLAN_STATUS_SUCCESS) {
				PRINTM(MERROR,
				       "Unable to locate pmpriv in current active priv_list\n");
				break;	/* EXIT CASE */
			}

			pioctl_req->bss_index = pmpriv->bss_index;
			ret = wlan_misc_ioctl_custom_ie_list(pmadapter,
							     pioctl_req,
							     MFALSE);
			if (ret != MLAN_STATUS_SUCCESS &&
			    ret != MLAN_STATUS_PENDING) {
				PRINTM(MERROR,
				       "%s(): Could not set IE for priv=%p [priv_bss_idx=%d]!\n",
				       __func__, pmpriv, pmpriv->bss_index);
				/* TODO: how to handle this error case??  ignore & continue? */
			}
			/* free ioctl buffer memory before we leave */
			pmadapter->callbacks.moal_mfree(pmadapter->pmoal_handle,
							(t_u8 *)pioctl_req);
			break;	/* EXIT CASE */
		}
		/* else */
		pstate_rdh->priv_curr_idx = RDH_STAGE_FIRST_ENTRY_PRIV_IDX;
		pstate_rdh->stage = RDH_REM_CUSTOM_IE;
		/* FALL THROUGH TO NEXT STAGE */

	case RDH_REM_CUSTOM_IE:
		PRINTM(MCMD_D, "%s(): stage(%d)=%s, priv_idx=%d\n",
		       __func__, pstate_rdh->stage,
		       rdh_stage_str[pstate_rdh->stage],
		       pstate_rdh->priv_curr_idx);

		/* remove CHAN_SW IE - firmware will accept on any interface,
		   and apply to all */
		if (pstate_rdh->priv_curr_idx == RDH_STAGE_FIRST_ENTRY_PRIV_IDX) {
			mlan_ioctl_req *pioctl_req = MNULL;

			/*
			 * first entry to this stage, do delay
			 * DFS requires a minimum of 5 chances for clients to hear this IE.
			 * Use delay:  5 beacons <= (BCN_DTIM_MSEC*5) <= 3 seconds).
			 */
			t_u16 delay_ms = MAX(MIN_RDH_CHAN_SW_IE_PERIOD_MSEC,
					     MIN((4 *
						  pstate_rdh->max_bcn_dtim_ms),
						 MAX_RDH_CHAN_SW_IE_PERIOD_MSEC));
			PRINTM(MMSG,
			       "11h: Radar Detected - delay %d ms for FW to"
			       " broadcast CHAN_SW IE.\n", delay_ms);
			wlan_mdelay(pmadapter, delay_ms);
			PRINTM(MMSG,
			       "11h: Radar Detected - delay over, removing"
			       " CHAN_SW IE from interfaces.\n");

			ret = wlan_11h_prepare_custom_ie_chansw(pmadapter,
								&pioctl_req,
								MFALSE);
			if ((ret != MLAN_STATUS_SUCCESS) || !pioctl_req) {
				PRINTM(MERROR,
				       "%s(): Error in preparing CHAN_SW IE.\n",
				       __func__);
				break;	/* EXIT CASE */
			}

			ret = wlan_11h_get_priv_curr_idx(pmpriv, pstate_rdh);
			if (ret != MLAN_STATUS_SUCCESS) {
				PRINTM(MERROR,
				       "Unable to locate pmpriv in current active priv_list\n");
				break;	/* EXIT CASE */
			}

			pioctl_req->bss_index = pmpriv->bss_index;
			ret = wlan_misc_ioctl_custom_ie_list(pmadapter,
							     pioctl_req,
							     MFALSE);
			if (ret != MLAN_STATUS_SUCCESS &&
			    ret != MLAN_STATUS_PENDING) {
				PRINTM(MERROR,
				       "%s(): Could not remove IE for priv=%p [priv_bss_idx=%d]!\n",
				       __func__, pmpriv, pmpriv->bss_index);
				/* TODO: hiow to handle this error case??  ignore & continue? */
			}
			/* free ioctl buffer memory before we leave */
			pmadapter->callbacks.moal_mfree(pmadapter->pmoal_handle,
							(t_u8 *)pioctl_req);
			break;	/* EXIT CASE */
		}
		/* else */
		pstate_rdh->priv_curr_idx = RDH_STAGE_FIRST_ENTRY_PRIV_IDX;
		pstate_rdh->stage = RDH_STOP_INTFS;
		/* FALL THROUGH TO NEXT STAGE */

	case RDH_STOP_INTFS:
		PRINTM(MCMD_D, "%s(): stage(%d)=%s, priv_idx=%d\n",
		       __func__, pstate_rdh->stage,
		       rdh_stage_str[pstate_rdh->stage],
		       pstate_rdh->priv_curr_idx);

		/* issues one cmd (DEAUTH/ADHOC_STOP/BSS_STOP) to each intf */
		while ((++pstate_rdh->priv_curr_idx) <
		       pstate_rdh->priv_list_count) {
			pmpriv = pstate_rdh->priv_list[pstate_rdh->
						       priv_curr_idx];
#ifdef UAP_SUPPORT
			if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_UAP) {
				ret = wlan_prepare_cmd(pmpriv,
						       HOST_CMD_APCMD_BSS_STOP,
						       HostCmd_ACT_GEN_SET, 0,
						       MNULL, MNULL);
				break;	/* leads to exit case */
			}
#endif
#ifdef STA_SUPPORT
			if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_STA) {
				if (wlan_11h_is_dfs_master(pmpriv)) {
					/* Save ad-hoc creator state before stop clears it */
					pmpriv->adhoc_state_prev =
						pmpriv->adhoc_state;
				}
				if (pmpriv->media_connected == MTRUE) {
					wlan_disconnect(pmpriv, MNULL, MNULL);
					break;	/* leads to exit case */
				}
			}
#endif
		}

		if (pstate_rdh->priv_curr_idx < pstate_rdh->priv_list_count ||
		    ret == MLAN_STATUS_FAILURE)
			break;	/* EXIT CASE */
		/* else */
		pstate_rdh->priv_curr_idx = RDH_STAGE_FIRST_ENTRY_PRIV_IDX;
		pstate_rdh->stage = RDH_SET_NEW_CHANNEL;

#ifdef DFS_TESTING_SUPPORT
		if (pmadapter->dfs_test_params.no_channel_change_on_radar) {
			PRINTM(MCMD_D,
			       "dfs_testing - no channel change on radar."
			       "  Overwrite new_chan = curr_chan.\n");
			pstate_rdh->new_channel = pstate_rdh->curr_channel;
			pstate_rdh->priv_curr_idx =
				RDH_STAGE_FIRST_ENTRY_PRIV_IDX;
			pstate_rdh->stage = RDH_RESTART_INTFS;
			goto rdh_restart_intfs;	/* skip next stage */
		}
#endif
		/* FALL THROUGH TO NEXT STAGE */

	case RDH_SET_NEW_CHANNEL:
		PRINTM(MCMD_D, "%s(): stage(%d)=%s, priv_idx=%d\n",
		       __func__, pstate_rdh->stage,
		       rdh_stage_str[pstate_rdh->stage],
		       pstate_rdh->priv_curr_idx);

		/* only set new channel for UAP intfs */
		while ((++pstate_rdh->priv_curr_idx) <
		       pstate_rdh->priv_list_count) {
			pmpriv = pstate_rdh->priv_list[pstate_rdh->
						       priv_curr_idx];
#ifdef UAP_SUPPORT
			if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_UAP) {

				pmpriv->uap_state_chan_cb.pioctl_req_curr =
					MNULL;
				pmpriv->uap_state_chan_cb.get_chan_callback =
					wlan_11h_radar_detected_callback;

				/* DFS only in 5GHz */
				wlan_11h_update_bandcfg(&pstate_rdh->
							uap_band_cfg,
							pstate_rdh->
							new_channel);
				PRINTM(MCMD_D,
				       "RDH_SET_NEW_CHANNEL: uAP band config = 0x%x channel=%d\n",
				       pstate_rdh->uap_band_cfg,
				       pstate_rdh->new_channel);

				ret = wlan_uap_set_channel(pmpriv,
							   pstate_rdh->
							   uap_band_cfg,
							   pstate_rdh->
							   new_channel);
				break;	/* leads to exit case */
			}
#endif
		}

		if (pstate_rdh->priv_curr_idx < pstate_rdh->priv_list_count ||
		    ret == MLAN_STATUS_FAILURE)
			break;	/* EXIT CASE (for UAP) */
		/* else */
		pstate_rdh->priv_curr_idx = RDH_STAGE_FIRST_ENTRY_PRIV_IDX;
		pstate_rdh->stage = RDH_RESTART_INTFS;
		/* FALL THROUGH TO NEXT STAGE */

	case RDH_RESTART_INTFS:
#ifdef DFS_TESTING_SUPPORT
rdh_restart_intfs:
#endif
		PRINTM(MCMD_D, "%s(): stage(%d)=%s, priv_idx=%d\n",
		       __func__, pstate_rdh->stage,
		       rdh_stage_str[pstate_rdh->stage],
		       pstate_rdh->priv_curr_idx);

		/* can only restart master intfs */
		while ((++pstate_rdh->priv_curr_idx) <
		       pstate_rdh->priv_list_count) {
			pmpriv = pstate_rdh->priv_list[pstate_rdh->
						       priv_curr_idx];
#ifdef UAP_SUPPORT
			if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_UAP) {
				if (wlan_11h_radar_detect_required(pmpriv,
								   pstate_rdh->
								   new_channel))
				{
					/* Radar detection is required for this channel,
					   make sure 11h is activated in the firmware */
					ret = wlan_11h_activate(pmpriv, MNULL,
								MTRUE);
					ret = wlan_11h_config_master_radar_det
						(pmpriv, MTRUE);
					ret = wlan_11h_check_update_radar_det_state(pmpriv);
				}
				ret = wlan_prepare_cmd(pmpriv,
						       HOST_CMD_APCMD_BSS_START,
						       HostCmd_ACT_GEN_SET, 0,
						       MNULL, MNULL);
				break;	/* leads to exit case */
			}
#endif
#ifdef STA_SUPPORT
			if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_STA) {
				/* Check previous state to find former
				 * Ad-hoc creator interface. Set new
				 * state to Starting, so it'll be seen
				 * as a DFS master. */
				if (pmpriv->adhoc_state_prev == ADHOC_STARTED) {
					pmpriv->adhoc_state = ADHOC_STARTING;
					pmpriv->adhoc_state_prev = ADHOC_IDLE;
				}
				if (wlan_11h_is_dfs_master(pmpriv)) {
					/* set new adhoc channel here */
					pmpriv->adhoc_channel =
						pstate_rdh->new_channel;
					if (wlan_11h_radar_detect_required
					    (pmpriv, pstate_rdh->new_channel)) {
						/* Radar detection is required for this channel,
						   make sure 11h is activated in the firmware */
						ret = wlan_11h_activate(pmpriv,
									MNULL,
									MTRUE);
						if (ret)
							break;
						ret = wlan_11h_config_master_radar_det(pmpriv, MTRUE);
						if (ret)
							break;
						ret = wlan_11h_check_update_radar_det_state(pmpriv);
						if (ret)
							break;
					}
					ret = wlan_prepare_cmd(pmpriv,
							       HostCmd_CMD_802_11_AD_HOC_START,
							       HostCmd_ACT_GEN_SET,
							       0, MNULL,
							       &pmpriv->
							       adhoc_last_start_ssid);
					break;	/* leads to exit case */
				}

				/* NOTE:  DON'T reconnect slave STA intfs - infra/adhoc_joiner
				 *   Do we want to return to same AP/network (on radar channel)?
				 *   If want to connect back, depend on either:
				 *     1. driver's reassoc thread
				 *     2. wpa_supplicant, or other user-space app
				 */
			}
#endif
		}

		if (pstate_rdh->priv_curr_idx < pstate_rdh->priv_list_count ||
		    ret == MLAN_STATUS_FAILURE)
			break;	/* EXIT CASE (for UAP) */
		/* else */
		pstate_rdh->priv_curr_idx = RDH_STAGE_FIRST_ENTRY_PRIV_IDX;
		pstate_rdh->stage = RDH_RESTART_TRAFFIC;
		/* FALL THROUGH TO NEXT STAGE */

	case RDH_RESTART_TRAFFIC:
		PRINTM(MCMD_D, "%s(): stage(%d)=%s\n",
		       __func__, pstate_rdh->stage,
		       rdh_stage_str[pstate_rdh->stage]);
		/* remove custome ie */
		if (pmadapter->ecsa_enable) {
			mlan_ioctl_req *pioctl_req = MNULL;
			ret = wlan_11h_prepare_custom_ie_chansw(pmadapter,
								&pioctl_req,
								MFALSE);
			if ((ret != MLAN_STATUS_SUCCESS) || !pioctl_req) {
				PRINTM(MERROR,
				       "%s(): Error in preparing CHAN_SW IE.\n",
				       __func__);
				break;	/* EXIT CASE */
			}

			pmpriv = pstate_rdh->priv_list[0];
			pstate_rdh->priv_curr_idx = 0;
			pioctl_req->bss_index = pmpriv->bss_index;
			ret = wlan_misc_ioctl_custom_ie_list(pmadapter,
							     pioctl_req,
							     MFALSE);
			if (ret != MLAN_STATUS_SUCCESS &&
			    ret != MLAN_STATUS_PENDING) {
				PRINTM(MERROR,
				       "%s(): Could not set IE for priv=%p [priv_bss_idx=%d]!\n",
				       __func__, pmpriv, pmpriv->bss_index);
				/* TODO: hiow to handle this error case??  ignore & continue? */
			}
			/* free ioctl buffer memory before we leave */
			pmadapter->callbacks.moal_mfree(pmadapter->pmoal_handle,
							(t_u8 *)pioctl_req);
		}
		/* continue traffic for reactivated interfaces */
		PRINTM(MMSG,
		       "11h: Radar Detected - restarting host tx traffic.\n");
		for (i = 0; i < pstate_rdh->priv_list_count; i++)
			wlan_11h_tx_enable(pstate_rdh->priv_list[i]);

		pstate_rdh->stage = RDH_OFF;	/* DONE! */
		PRINTM(MCMD_D, "%s(): finished - stage(%d)=%s\n",
		       __func__, pstate_rdh->stage,
		       rdh_stage_str[pstate_rdh->stage]);

		break;

	default:
		pstate_rdh->stage = RDH_OFF;	/* cancel RDH to unblock Tx packets */
		break;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief DFS Event Preprocessing.
 *  Operates directly on pmadapter variables.
 *
 *  1. EVENT_RADAR_DETECTED comes from firmware without specific
 *     bss_num/bss_type.  Find it an appropriate interface and
 *     update event_cause field in event_buf.
 *
 *  @param pmadapter    Pointer to mlan_adapter
 *
 *  @return    MLAN_STATUS_SUCCESS (update successful)
 *          or MLAN_STATUS_FAILURE (no change)
 */
mlan_status
wlan_11h_dfs_event_preprocessing(mlan_adapter *pmadapter)
{
	mlan_status ret = MLAN_STATUS_FAILURE;
	mlan_private *pmpriv = MNULL;
	mlan_private *priv_list[MLAN_MAX_BSS_NUM];

	ENTER();
	switch (pmadapter->event_cause & EVENT_ID_MASK) {
	case EVENT_RADAR_DETECTED:
		/* find active intf:  prefer dfs_master over dfs_slave */
		if (wlan_get_privs_by_two_cond(pmadapter,
					       wlan_11h_is_master_active_on_dfs_chan,
					       wlan_11h_is_dfs_master,
					       MTRUE, priv_list)) {
			pmpriv = priv_list[0];
			PRINTM(MINFO, "%s: found dfs_master priv=%p\n",
			       __func__, pmpriv);
		} else if (wlan_get_privs_by_two_cond(pmadapter,
						      wlan_11h_is_slave_active_on_dfs_chan,
						      wlan_11h_is_dfs_slave,
						      MTRUE, priv_list)) {
			pmpriv = priv_list[0];
			PRINTM(MINFO, "%s: found dfs_slave priv=%p\n",
			       __func__, pmpriv);
		} else if (pmadapter->state_dfs.dfs_check_pending) {
			pmpriv = (mlan_private *)(pmadapter->state_dfs.
						  dfs_check_priv);
			PRINTM(MINFO, "%s: found dfs priv=%p\n", __func__,
			       pmpriv);
		}

		/* update event_cause if we found an appropriate priv */
		if (pmpriv) {
			pmlan_buffer pmevbuf = pmadapter->pmlan_buffer_event;
			t_u32 new_event_cause =
				pmadapter->event_cause & EVENT_ID_MASK;
			new_event_cause |=
				((GET_BSS_NUM(pmpriv) & 0xff) << 16) |
				((pmpriv->bss_type & 0xff) << 24);
			PRINTM(MINFO, "%s: priv - bss_num=%d, bss_type=%d\n",
			       __func__, GET_BSS_NUM(pmpriv), pmpriv->bss_type);
			memcpy(pmadapter, pmevbuf->pbuf + pmevbuf->data_offset,
			       &new_event_cause, sizeof(new_event_cause));
			ret = MLAN_STATUS_SUCCESS;
		} else {
			PRINTM(MERROR,
			       "Failed to find dfs master/slave priv\n");
			ret = MLAN_STATUS_FAILURE;
		}
		break;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief try to switch to a non-dfs channel
 *
 *  @param priv    Void pointer to mlan_private
 *
 *  @param chan    pointer to channel
 *
 *  @return MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE or MLAN_STATUS_PENDING
 */
mlan_status
wlan_11h_switch_non_dfs_chan(mlan_private *priv, t_u8 *chan)
{
	mlan_status ret = MLAN_STATUS_FAILURE;
	t_u32 i;
	t_u32 rand_entry;
	t_u8 def_chan;
	t_u8 rand_tries = 0;
	region_chan_t *chn_tbl = MNULL;
	pmlan_adapter pmadapter = priv->adapter;

	ENTER();

#ifdef DFS_TESTING_SUPPORT
	if (!pmadapter->dfs_test_params.no_channel_change_on_radar &&
	    pmadapter->dfs_test_params.fixed_new_channel_on_radar) {
		PRINTM(MCMD_D, "dfs_testing - user fixed new_chan=%d\n",
		       pmadapter->dfs_test_params.fixed_new_channel_on_radar);
		*chan = pmadapter->dfs_test_params.fixed_new_channel_on_radar;

		LEAVE();
		return MLAN_STATUS_SUCCESS;
	}
#endif

	/*get the channel table first */
	for (i = 0; i < MAX_REGION_CHANNEL_NUM; i++) {
		if (pmadapter->region_channel[i].band == BAND_A
		    && pmadapter->region_channel[i].valid) {
			chn_tbl = &pmadapter->region_channel[i];
			break;
		}
	}

	if (!chn_tbl || !chn_tbl->pcfp)
		goto done;

	do {
		rand_entry =
			wlan_11h_get_random_num(pmadapter) % chn_tbl->num_cfp;
		def_chan = (t_u8)chn_tbl->pcfp[rand_entry].channel;
		rand_tries++;
	} while ((wlan_11h_is_channel_under_nop(pmadapter, def_chan) ||
		  chn_tbl->pcfp[rand_entry].passive_scan_or_radar_detect ==
		  MTRUE) && (rand_tries < MAX_SWITCH_CHANNEL_RETRIES));

	/* meet max retries, use the lowest non-dfs channel */
	if (rand_tries == MAX_SWITCH_CHANNEL_RETRIES) {
		for (i = 0; i < chn_tbl->num_cfp; i++) {
			if (chn_tbl->pcfp[i].passive_scan_or_radar_detect ==
			    MFALSE &&
			    !wlan_11h_is_channel_under_nop(pmadapter,
							   (t_u8)chn_tbl->
							   pcfp[i].channel)) {
				def_chan = (t_u8)chn_tbl->pcfp[i].channel;
				break;
			}
		}
		if (i == chn_tbl->num_cfp)
			goto done;
	}

	*chan = def_chan;
	ret = MLAN_STATUS_SUCCESS;
done:
	LEAVE();
	return ret;
}
