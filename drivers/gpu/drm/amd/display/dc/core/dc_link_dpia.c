// SPDX-License-Identifier: MIT
/*
 * Copyright 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dc.h"
#include "dc_link_dpia.h"
#include "inc/core_status.h"
#include "dc_link.h"
#include "dc_link_dp.h"
#include "dpcd_defs.h"
#include "link_hwss.h"
#include "inc/link_dpcd.h"
#include "dm_helpers.h"
#include "dmub/inc/dmub_cmd.h"
#include "inc/link_dpcd.h"

#define DC_LOGGER \
	link->ctx->logger

enum dc_status dpcd_get_tunneling_device_data(struct dc_link *link)
{
	enum dc_status status = DC_OK;
	uint8_t dpcd_dp_tun_data[3] = {0};
	uint8_t dpcd_topology_data[DPCD_USB4_TOPOLOGY_ID_LEN] = {0};
	uint8_t i = 0;

	status = core_link_read_dpcd(link,
			DP_TUNNELING_CAPABILITIES_SUPPORT,
			dpcd_dp_tun_data,
			sizeof(dpcd_dp_tun_data));

	status = core_link_read_dpcd(link,
			DP_USB4_ROUTER_TOPOLOGY_ID,
			dpcd_topology_data,
			sizeof(dpcd_topology_data));

	link->dpcd_caps.usb4_dp_tun_info.dp_tun_cap.raw =
			dpcd_dp_tun_data[DP_TUNNELING_CAPABILITIES_SUPPORT -
					 DP_TUNNELING_CAPABILITIES_SUPPORT];
	link->dpcd_caps.usb4_dp_tun_info.dpia_info.raw =
			dpcd_dp_tun_data[DP_IN_ADAPTER_INFO - DP_TUNNELING_CAPABILITIES_SUPPORT];
	link->dpcd_caps.usb4_dp_tun_info.usb4_driver_id =
			dpcd_dp_tun_data[DP_USB4_DRIVER_ID - DP_TUNNELING_CAPABILITIES_SUPPORT];

	for (i = 0; i < DPCD_USB4_TOPOLOGY_ID_LEN; i++)
		link->dpcd_caps.usb4_dp_tun_info.usb4_topology_id[i] = dpcd_topology_data[i];

	return status;
}

/* Configure link as prescribed in link_setting; set LTTPR mode; and
 * Initialize link training settings.
 * Abort link training if sink unplug detected.
 *
 * @param link DPIA link being trained.
 * @param[in] link_setting Lane count, link rate and downspread control.
 * @param[out] lt_settings Link settings and drive settings (voltage swing and pre-emphasis).
 */
static enum link_training_result dpia_configure_link(struct dc_link *link,
		const struct dc_link_settings *link_setting,
		struct link_training_settings *lt_settings)
{
	enum dc_status status;
	bool fec_enable;

	DC_LOG_HW_LINK_TRAINING("%s\n DPIA(%d) configuring\n - LTTPR mode(%d)\n",
				__func__,
				link->link_id.enum_id - ENUM_ID_1,
				link->lttpr_mode);

	dp_decide_training_settings(link,
		link_setting,
		lt_settings);

	status = dpcd_configure_channel_coding(link, lt_settings);
	if (status != DC_OK && !link->hpd_status)
		return LINK_TRAINING_ABORT;

	/* Configure lttpr mode */
	status = dpcd_configure_lttpr_mode(link, lt_settings);
	if (status != DC_OK && !link->hpd_status)
		return LINK_TRAINING_ABORT;

	/* Set link rate, lane count and spread. */
	status = dpcd_set_link_settings(link, lt_settings);
	if (status != DC_OK && !link->hpd_status)
		return LINK_TRAINING_ABORT;

	if (link->preferred_training_settings.fec_enable)
		fec_enable = *link->preferred_training_settings.fec_enable;
	else
		fec_enable = true;
	status = dp_set_fec_ready(link, fec_enable);
	if (status != DC_OK && !link->hpd_status)
		return LINK_TRAINING_ABORT;

	return LINK_TRAINING_SUCCESS;
}

static enum dc_status core_link_send_set_config(struct dc_link *link,
	uint8_t msg_type,
	uint8_t msg_data)
{
	struct set_config_cmd_payload payload;
	enum set_config_status set_config_result = SET_CONFIG_PENDING;

	/* prepare set_config payload */
	payload.msg_type = msg_type;
	payload.msg_data = msg_data;

	if (!link->ddc->ddc_pin && !link->aux_access_disabled &&
	    (dm_helpers_dmub_set_config_sync(link->ctx, link,
					     &payload, &set_config_result) == -1)) {
		return DC_ERROR_UNEXPECTED;
	}

	/* set_config should return ACK if successful */
	return (set_config_result == SET_CONFIG_ACK_RECEIVED) ? DC_OK : DC_ERROR_UNEXPECTED;
}

/* Build SET_CONFIG message data payload for specified message type. */
static uint8_t dpia_build_set_config_data(enum dpia_set_config_type type,
		struct dc_link *link,
		struct link_training_settings *lt_settings)
{
	union dpia_set_config_data data;

	data.raw = 0;

	switch (type) {
	case DPIA_SET_CFG_SET_LINK:
		data.set_link.mode = link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT ? 1 : 0;
		break;
	case DPIA_SET_CFG_SET_PHY_TEST_MODE:
		break;
	case DPIA_SET_CFG_SET_VSPE:
		/* Assume all lanes have same drive settings. */
		data.set_vspe.swing = lt_settings->lane_settings[0].VOLTAGE_SWING;
		data.set_vspe.pre_emph = lt_settings->lane_settings[0].PRE_EMPHASIS;
		data.set_vspe.max_swing_reached =
			lt_settings->lane_settings[0].VOLTAGE_SWING ==
			VOLTAGE_SWING_MAX_LEVEL ? 1 : 0;
		data.set_vspe.max_pre_emph_reached =
			lt_settings->lane_settings[0].PRE_EMPHASIS ==
			PRE_EMPHASIS_MAX_LEVEL ? 1 : 0;
		break;
	default:
		ASSERT(false); /* Message type not supported by helper function. */
		break;
	}

	return data.raw;
}

/* Convert DC training pattern to DPIA training stage. */
static enum dpia_set_config_ts convert_trng_ptn_to_trng_stg(enum dc_dp_training_pattern tps)
{
	enum dpia_set_config_ts ts;

	switch (tps) {
	case DP_TRAINING_PATTERN_SEQUENCE_1:
		ts = DPIA_TS_TPS1;
		break;
	case DP_TRAINING_PATTERN_SEQUENCE_2:
		ts = DPIA_TS_TPS2;
		break;
	case DP_TRAINING_PATTERN_SEQUENCE_3:
		ts = DPIA_TS_TPS3;
		break;
	case DP_TRAINING_PATTERN_SEQUENCE_4:
		ts = DPIA_TS_TPS4;
		break;
	default:
		ASSERT(false); /* TPS not supported by helper function. */
		break;
	}

	return ts;
}

/* Write training pattern to DPCD. */
static enum dc_status dpcd_set_lt_pattern(struct dc_link *link,
	enum dc_dp_training_pattern pattern,
	uint32_t hop)
{
	union dpcd_training_pattern dpcd_pattern = { {0} };
	uint32_t dpcd_tps_offset = DP_TRAINING_PATTERN_SET;
	enum dc_status status;

	if (hop != DPRX)
		dpcd_tps_offset = DP_TRAINING_PATTERN_SET_PHY_REPEATER1 +
			((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (hop - 1));

	/* DpcdAddress_TrainingPatternSet */
	dpcd_pattern.v1_4.TRAINING_PATTERN_SET =
		dc_dp_training_pattern_to_dpcd_training_pattern(link, pattern);

	dpcd_pattern.v1_4.SCRAMBLING_DISABLE =
		dc_dp_initialize_scrambling_data_symbols(link, pattern);

	if (hop != DPRX) {
		DC_LOG_HW_LINK_TRAINING("%s\n LTTPR Repeater ID: %d\n 0x%X pattern = %x\n",
					__func__,
					hop,
					dpcd_tps_offset,
					dpcd_pattern.v1_4.TRAINING_PATTERN_SET);
	} else {
		DC_LOG_HW_LINK_TRAINING("%s\n 0x%X pattern = %x\n",
					__func__,
					dpcd_tps_offset,
					dpcd_pattern.v1_4.TRAINING_PATTERN_SET);
	}

	status = core_link_write_dpcd(link,
				      dpcd_tps_offset,
				      &dpcd_pattern.raw,
				      sizeof(dpcd_pattern.raw));

	return status;
}

/* Execute clock recovery phase of link training for specified hop in display
 * path.in non-transparent mode:
 * - Driver issues both DPCD and SET_CONFIG transactions.
 * - TPS1 is transmitted for any hops downstream of DPOA.
 * - Drive (VS/PE) only transmitted for the hop immediately downstream of DPOA.
 * - CR for the first hop (DPTX-to-DPIA) is assumed to be successful.
 *
 * @param link DPIA link being trained.
 * @param lt_settings link_setting and drive settings (voltage swing and pre-emphasis).
 * @param hop The Hop in display path. DPRX = 0.
 */
static enum link_training_result dpia_training_cr_non_transparent(struct dc_link *link,
		struct link_training_settings *lt_settings,
		uint32_t hop)
{
	enum link_training_result result = LINK_TRAINING_CR_FAIL_LANE0;
	uint8_t repeater_cnt = 0; /* Number of hops/repeaters in display path. */
	enum dc_status status;
	uint32_t retries_cr = 0; /* Number of consecutive attempts with same VS or PE. */
	uint32_t retry_count = 0;
	/* From DP spec, CR read interval is always 100us. */
	uint32_t wait_time_microsec = TRAINING_AUX_RD_INTERVAL;
	struct link_training_settings req_settings;
	enum dc_lane_count lane_count = lt_settings->link_settings.lane_count;
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX] = { { {0} } };
	union lane_align_status_updated dpcd_lane_status_updated = { {0} };
	uint8_t set_cfg_data;
	enum dpia_set_config_ts ts;

	repeater_cnt = dp_convert_to_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt);

	/* Cap of LINK_TRAINING_MAX_CR_RETRY attempts at clock recovery.
	 * Fix inherited from perform_clock_recovery_sequence() -
	 * the DP equivalent of this function:
	 * Required for Synaptics MST hub which can put the LT in
	 * infinite loop by switching the VS between level 0 and level 1
	 * continuously.
	 */
	while ((retries_cr < LINK_TRAINING_MAX_RETRY_COUNT) &&
	       (retry_count < LINK_TRAINING_MAX_CR_RETRY)) {
		/* DPTX-to-DPIA */
		if (hop == repeater_cnt) {
			/* Send SET_CONFIG(SET_LINK:LC,LR,LTTPR) to notify DPOA that
			 * non-transparent link training has started.
			 * This also enables the transmission of clk_sync packets.
			 */
			set_cfg_data = dpia_build_set_config_data(DPIA_SET_CFG_SET_LINK,
					link,
					lt_settings);
			status = core_link_send_set_config(link,
					DPIA_SET_CFG_SET_LINK,
					set_cfg_data);
			/* CR for this hop is considered successful as long as
			 * SET_CONFIG message is acknowledged by DPOA.
			 */
			if (status == DC_OK)
				result = LINK_TRAINING_SUCCESS;
			else
				result = LINK_TRAINING_ABORT;
			break;
		}

		/* DPOA-to-x */
		/* Instruct DPOA to transmit TPS1 then update DPCD. */
		if (retry_count == 0) {
			ts = convert_trng_ptn_to_trng_stg(lt_settings->pattern_for_cr);
			status = core_link_send_set_config(link,
					DPIA_SET_CFG_SET_TRAINING,
					ts);
			if (status != DC_OK) {
				result = LINK_TRAINING_ABORT;
				break;
			}
			status = dpcd_set_lt_pattern(link, lt_settings->pattern_for_cr, hop);
			if (status != DC_OK) {
				result = LINK_TRAINING_ABORT;
				break;
			}
		}

		/* Update DPOA drive settings then DPCD. DPOA does only adjusts
		 * drive settings for hops immediately downstream.
		 */
		if (hop == repeater_cnt - 1) {
			set_cfg_data = dpia_build_set_config_data(DPIA_SET_CFG_SET_VSPE,
					link,
					lt_settings);
			status = core_link_send_set_config(link,
					DPIA_SET_CFG_SET_VSPE,
					set_cfg_data);
			if (status != DC_OK) {
				result = LINK_TRAINING_ABORT;
				break;
			}
		}
		status = dpcd_set_lane_settings(link, lt_settings, hop);
		if (status != DC_OK) {
			result = LINK_TRAINING_ABORT;
			break;
		}

		dp_wait_for_training_aux_rd_interval(link, wait_time_microsec);

		/* Read status and adjustment requests from DPCD. */
		status = dp_get_lane_status_and_drive_settings(link,
				lt_settings,
				dpcd_lane_status,
				&dpcd_lane_status_updated,
				&req_settings,
				hop);
		if (status != DC_OK) {
			result = LINK_TRAINING_ABORT;
			break;
		}

		/* Check if clock recovery successful. */
		if (dp_is_cr_done(lane_count, dpcd_lane_status)) {
			result = LINK_TRAINING_SUCCESS;
			break;
		}

		result = dp_get_cr_failure(lane_count, dpcd_lane_status);

		if (dp_is_max_vs_reached(lt_settings))
			break;

		/* Count number of attempts with same drive settings.
		 * Note: settings are the same for all lanes,
		 * so comparing first lane is sufficient.
		 */
		if (lt_settings->lane_settings[0].VOLTAGE_SWING ==
			req_settings.lane_settings[0].VOLTAGE_SWING &&
			lt_settings->lane_settings[0].PRE_EMPHASIS ==
			req_settings.lane_settings[0].PRE_EMPHASIS)
			retries_cr++;
		else
			retries_cr = 0;

		/* Update VS/PE. */
		dp_update_drive_settings(lt_settings, req_settings);
		retry_count++;
	}

	/* Abort link training if clock recovery failed due to HPD unplug. */
	if (!link->hpd_status)
		result = LINK_TRAINING_ABORT;

	DC_LOG_HW_LINK_TRAINING("%s\n DPIA(%d) clock recovery\n"
		" -hop(%d)\n - result(%d)\n - retries(%d)\n",
		__func__,
		link->link_id.enum_id - ENUM_ID_1,
		hop,
		result,
		retry_count);

	return result;
}

/* Execute clock recovery phase of link training in transparent LTTPR mode:
 * - Driver only issues DPCD transactions and leaves USB4 tunneling (SET_CONFIG) messages to DPIA.
 * - Driver writes TPS1 to DPCD to kick off training.
 * - Clock recovery (CR) for link is handled by DPOA, which reports result to DPIA on completion.
 * - DPIA communicates result to driver by updating CR status when driver reads DPCD.
 *
 * @param link DPIA link being trained.
 * @param lt_settings link_setting and drive settings (voltage swing and pre-emphasis).
 */
static enum link_training_result dpia_training_cr_transparent(struct dc_link *link,
		struct link_training_settings *lt_settings)
{
	enum link_training_result result = LINK_TRAINING_CR_FAIL_LANE0;
	enum dc_status status;
	uint32_t retries_cr = 0; /* Number of consecutive attempts with same VS or PE. */
	uint32_t retry_count = 0;
	uint32_t wait_time_microsec = lt_settings->cr_pattern_time;
	struct link_training_settings req_settings;
	enum dc_lane_count lane_count = lt_settings->link_settings.lane_count;
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX] = { { {0} } };
	union lane_align_status_updated dpcd_lane_status_updated = { {0} };

	/* Cap of LINK_TRAINING_MAX_CR_RETRY attempts at clock recovery.
	 * Fix inherited from perform_clock_recovery_sequence() -
	 * the DP equivalent of this function:
	 * Required for Synaptics MST hub which can put the LT in
	 * infinite loop by switching the VS between level 0 and level 1
	 * continuously.
	 */
	while ((retries_cr < LINK_TRAINING_MAX_RETRY_COUNT) &&
	       (retry_count < LINK_TRAINING_MAX_CR_RETRY)) {
		/* Write TPS1 (not VS or PE) to DPCD to start CR phase.
		 * DPIA sends SET_CONFIG(SET_LINK) to notify DPOA to
		 * start link training.
		 */
		if (retry_count == 0) {
			status = dpcd_set_lt_pattern(link, lt_settings->pattern_for_cr, DPRX);
			if (status != DC_OK) {
				result = LINK_TRAINING_ABORT;
				break;
			}
		}

		dp_wait_for_training_aux_rd_interval(link, wait_time_microsec);

		/* Read status and adjustment requests from DPCD. */
		status = dp_get_lane_status_and_drive_settings(link,
				lt_settings,
				dpcd_lane_status,
				&dpcd_lane_status_updated,
				&req_settings,
				DPRX);
		if (status != DC_OK) {
			result = LINK_TRAINING_ABORT;
			break;
		}

		/* Check if clock recovery successful. */
		if (dp_is_cr_done(lane_count, dpcd_lane_status)) {
			result = LINK_TRAINING_SUCCESS;
			break;
		}

		result = dp_get_cr_failure(lane_count, dpcd_lane_status);

		if (dp_is_max_vs_reached(lt_settings))
			break;

		/* Count number of attempts with same drive settings.
		 * Note: settings are the same for all lanes,
		 * so comparing first lane is sufficient.
		 */
		if (lt_settings->lane_settings[0].VOLTAGE_SWING ==
			req_settings.lane_settings[0].VOLTAGE_SWING &&
			lt_settings->lane_settings[0].PRE_EMPHASIS ==
			req_settings.lane_settings[0].PRE_EMPHASIS)
			retries_cr++;
		else
			retries_cr = 0;

		/* Update VS/PE. */
		dp_update_drive_settings(lt_settings, req_settings);
		retry_count++;
	}

	/* Abort link training if clock recovery failed due to HPD unplug. */
	if (!link->hpd_status)
		result = LINK_TRAINING_ABORT;

	DC_LOG_HW_LINK_TRAINING("%s\n DPIA(%d) clock recovery\n"
		" -hop(%d)\n - result(%d)\n - retries(%d)\n",
		__func__,
		link->link_id.enum_id - ENUM_ID_1,
		DPRX,
		result,
		retry_count);

	return result;
}

/* Execute clock recovery phase of link training for specified hop in display
 * path.
 *
 * @param link DPIA link being trained.
 * @param lt_settings link_setting and drive settings (voltage swing and pre-emphasis).
 * @param hop The Hop in display path. DPRX = 0.
 */
static enum link_training_result dpia_training_cr_phase(struct dc_link *link,
		struct link_training_settings *lt_settings,
		uint32_t hop)
{
	enum link_training_result result = LINK_TRAINING_CR_FAIL_LANE0;

	if (link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT)
		result = dpia_training_cr_non_transparent(link, lt_settings, hop);
	else
		result = dpia_training_cr_transparent(link, lt_settings);

	return result;
}

/* Return status read interval during equalization phase. */
static uint32_t dpia_get_eq_aux_rd_interval(const struct dc_link *link,
		const struct link_training_settings *lt_settings,
		uint32_t hop)
{
	uint32_t wait_time_microsec;

	if (hop == DPRX)
		wait_time_microsec = lt_settings->eq_pattern_time;
	else
		wait_time_microsec =
				dp_translate_training_aux_read_interval(
					link->dpcd_caps.lttpr_caps.aux_rd_interval[hop - 1]);

#if defined(CONFIG_DRM_AMD_DC_DCN)
	/* Check debug option for extending aux read interval. */
	if (link->dc->debug.dpia_debug.bits.extend_aux_rd_interval)
		wait_time_microsec = DPIA_DEBUG_EXTENDED_AUX_RD_INTERVAL_US;
#endif

	return wait_time_microsec;
}

/* Execute equalization phase of link training for specified hop in display
 * path in non-transparent mode:
 * - driver issues both DPCD and SET_CONFIG transactions.
 * - TPSx is transmitted for any hops downstream of DPOA.
 * - Drive (VS/PE) only transmitted for the hop immediately downstream of DPOA.
 * - EQ for the first hop (DPTX-to-DPIA) is assumed to be successful.
 * - DPRX EQ only reported successful when both DPRX and DPIA requirements
 * (clk sync packets sent) fulfilled.
 *
 * @param link DPIA link being trained.
 * @param lt_settings link_setting and drive settings (voltage swing and pre-emphasis).
 * @param hop The Hop in display path. DPRX = 0.
 */
static enum link_training_result dpia_training_eq_non_transparent(struct dc_link *link,
		struct link_training_settings *lt_settings,
		uint32_t hop)
{
	enum link_training_result result = LINK_TRAINING_EQ_FAIL_EQ;
	uint8_t repeater_cnt = 0; /* Number of hops/repeaters in display path. */
	uint32_t retries_eq = 0;
	enum dc_status status;
	enum dc_dp_training_pattern tr_pattern;
	uint32_t wait_time_microsec;
	struct link_training_settings req_settings;
	enum dc_lane_count lane_count = lt_settings->link_settings.lane_count;
	union lane_align_status_updated dpcd_lane_status_updated = { {0} };
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX] = { { {0} } };
	uint8_t set_cfg_data;
	enum dpia_set_config_ts ts;

	/* Training pattern is TPS4 for repeater;
	 * TPS2/3/4 for DPRX depending on what it supports.
	 */
	if (hop == DPRX)
		tr_pattern = lt_settings->pattern_for_eq;
	else
		tr_pattern = DP_TRAINING_PATTERN_SEQUENCE_4;

	repeater_cnt = dp_convert_to_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt);

	for (retries_eq = 0; retries_eq < LINK_TRAINING_MAX_RETRY_COUNT; retries_eq++) {
		/* DPTX-to-DPIA equalization always successful. */
		if (hop == repeater_cnt) {
			result = LINK_TRAINING_SUCCESS;
			break;
		}

		/* Instruct DPOA to transmit TPSn then update DPCD. */
		if (retries_eq == 0) {
			ts = convert_trng_ptn_to_trng_stg(tr_pattern);
			status = core_link_send_set_config(link,
					DPIA_SET_CFG_SET_TRAINING,
					ts);
			if (status != DC_OK) {
				result = LINK_TRAINING_ABORT;
				break;
			}
			status = dpcd_set_lt_pattern(link, tr_pattern, hop);
			if (status != DC_OK) {
				result = LINK_TRAINING_ABORT;
				break;
			}
		}

		/* Update DPOA drive settings then DPCD. DPOA only adjusts
		 * drive settings for hop immediately downstream.
		 */
		if (hop == repeater_cnt - 1) {
			set_cfg_data = dpia_build_set_config_data(DPIA_SET_CFG_SET_VSPE,
								  link,
								  lt_settings);
			status = core_link_send_set_config(link,
							   DPIA_SET_CFG_SET_VSPE,
							   set_cfg_data);
			if (status != DC_OK) {
				result = LINK_TRAINING_ABORT;
				break;
			}
		}
		status = dpcd_set_lane_settings(link, lt_settings, hop);
		if (status != DC_OK) {
			result = LINK_TRAINING_ABORT;
			break;
		}

		/* Extend wait time on second equalisation attempt on final hop to
		 * ensure clock sync packets have been sent.
		 */
		if (hop == DPRX && retries_eq == 1)
			wait_time_microsec = max(wait_time_microsec, (uint32_t)DPIA_CLK_SYNC_DELAY);
		else
			wait_time_microsec = dpia_get_eq_aux_rd_interval(link, lt_settings, hop);

		dp_wait_for_training_aux_rd_interval(link, wait_time_microsec);

		/* Read status and adjustment requests from DPCD. */
		status = dp_get_lane_status_and_drive_settings(link,
				lt_settings,
				dpcd_lane_status,
				&dpcd_lane_status_updated,
				&req_settings,
				hop);
		if (status != DC_OK) {
			result = LINK_TRAINING_ABORT;
			break;
		}

		/* CR can still fail during EQ phase. Fail training if CR fails. */
		if (!dp_is_cr_done(lane_count, dpcd_lane_status)) {
			result = LINK_TRAINING_EQ_FAIL_CR;
			break;
		}

		if (dp_is_ch_eq_done(lane_count, dpcd_lane_status) &&
		    dp_is_symbol_locked(link->cur_link_settings.lane_count, dpcd_lane_status) &&
		    dp_is_interlane_aligned(dpcd_lane_status_updated)) {
			result =  LINK_TRAINING_SUCCESS;
			break;
		}

		/* Update VS/PE. */
		dp_update_drive_settings(lt_settings, req_settings);
	}

	/* Abort link training if equalization failed due to HPD unplug. */
	if (!link->hpd_status)
		result = LINK_TRAINING_ABORT;

	DC_LOG_HW_LINK_TRAINING("%s\n DPIA(%d) equalization\n"
		" - hop(%d)\n - result(%d)\n - retries(%d)\n",
		__func__,
		link->link_id.enum_id - ENUM_ID_1,
		hop,
		result,
		retries_eq);

	return result;
}

/* Execute equalization phase of link training for specified hop in display
 * path in transparent LTTPR mode:
 * - driver only issues DPCD transactions leaves USB4 tunneling (SET_CONFIG) messages to DPIA.
 * - driver writes TPSx to DPCD to notify DPIA that is in equalization phase.
 * - equalization (EQ) for link is handled by DPOA, which reports result to DPIA on completion.
 * - DPIA communicates result to driver by updating EQ status when driver reads DPCD.
 *
 * @param link DPIA link being trained.
 * @param lt_settings link_setting and drive settings (voltage swing and pre-emphasis).
 * @param hop The Hop in display path. DPRX = 0.
 */
static enum link_training_result dpia_training_eq_transparent(struct dc_link *link,
		struct link_training_settings *lt_settings)
{
	enum link_training_result result = LINK_TRAINING_EQ_FAIL_EQ;
	uint32_t retries_eq = 0;
	enum dc_status status;
	enum dc_dp_training_pattern tr_pattern = lt_settings->pattern_for_eq;
	uint32_t wait_time_microsec;
	struct link_training_settings req_settings;
	enum dc_lane_count lane_count = lt_settings->link_settings.lane_count;
	union lane_align_status_updated dpcd_lane_status_updated = { {0} };
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX] = { { {0} } };

	wait_time_microsec = dpia_get_eq_aux_rd_interval(link, lt_settings, DPRX);

	for (retries_eq = 0; retries_eq < LINK_TRAINING_MAX_RETRY_COUNT; retries_eq++) {
		if (retries_eq == 0) {
			status = dpcd_set_lt_pattern(link, tr_pattern, DPRX);
			if (status != DC_OK) {
				result = LINK_TRAINING_ABORT;
				break;
			}
		}

		dp_wait_for_training_aux_rd_interval(link, wait_time_microsec);

		/* Read status and adjustment requests from DPCD. */
		status = dp_get_lane_status_and_drive_settings(link,
				lt_settings,
				dpcd_lane_status,
				&dpcd_lane_status_updated,
				&req_settings,
				DPRX);
		if (status != DC_OK) {
			result = LINK_TRAINING_ABORT;
			break;
		}

		/* CR can still fail during EQ phase. Fail training if CR fails. */
		if (!dp_is_cr_done(lane_count, dpcd_lane_status)) {
			result = LINK_TRAINING_EQ_FAIL_CR;
			break;
		}

		if (dp_is_ch_eq_done(lane_count, dpcd_lane_status) &&
		    dp_is_symbol_locked(link->cur_link_settings.lane_count, dpcd_lane_status) &&
		    dp_is_interlane_aligned(dpcd_lane_status_updated)) {
			result =  LINK_TRAINING_SUCCESS;
			break;
		}

		/* Update VS/PE. */
		dp_update_drive_settings(lt_settings, req_settings);
	}

	/* Abort link training if equalization failed due to HPD unplug. */
	if (!link->hpd_status)
		result = LINK_TRAINING_ABORT;

	DC_LOG_HW_LINK_TRAINING("%s\n DPIA(%d) equalization\n"
		" - hop(%d)\n - result(%d)\n - retries(%d)\n",
		__func__,
		link->link_id.enum_id - ENUM_ID_1,
		DPRX,
		result,
		retries_eq);

	return result;
}

/* Execute equalization phase of link training for specified hop in display
 * path.
 *
 * @param link DPIA link being trained.
 * @param lt_settings link_setting and drive settings (voltage swing and pre-emphasis).
 * @param hop The Hop in display path. DPRX = 0.
 */
static enum link_training_result dpia_training_eq_phase(struct dc_link *link,
		struct link_training_settings *lt_settings,
		uint32_t hop)
{
	enum link_training_result result = LINK_TRAINING_EQ_FAIL_EQ;

	if (link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT)
		result = dpia_training_eq_non_transparent(link, lt_settings, hop);
	else
		result = dpia_training_eq_transparent(link, lt_settings);

	return result;
}

/* End training of specified hop in display path. */
static enum dc_status dpcd_clear_lt_pattern(struct dc_link *link, uint32_t hop)
{
	union dpcd_training_pattern dpcd_pattern = { {0} };
	uint32_t dpcd_tps_offset = DP_TRAINING_PATTERN_SET;
	enum dc_status status;

	if (hop != DPRX)
		dpcd_tps_offset = DP_TRAINING_PATTERN_SET_PHY_REPEATER1 +
			((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (hop - 1));

	status = core_link_write_dpcd(link,
			dpcd_tps_offset,
			&dpcd_pattern.raw,
			sizeof(dpcd_pattern.raw));

	return status;
}

/* End training of specified hop in display path.
 *
 * In transparent LTTPR mode:
 * - driver clears training pattern for the specified hop in DPCD.
 * In non-transparent LTTPR mode:
 * - in addition to clearing training pattern, driver issues USB4 tunneling
 * (SET_CONFIG) messages to notify DPOA when training is done for first hop
 * (DPTX-to-DPIA) and last hop (DPRX).
 *
 * @param link DPIA link being trained.
 * @param hop The Hop in display path. DPRX = 0.
 */
static enum link_training_result dpia_training_end(struct dc_link *link,
		uint32_t hop)
{
	enum link_training_result result = LINK_TRAINING_SUCCESS;
	uint8_t repeater_cnt = 0; /* Number of hops/repeaters in display path. */
	enum dc_status status;

	if (link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT) {
		repeater_cnt = dp_convert_to_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt);

		if (hop == repeater_cnt) { /* DPTX-to-DPIA */
			/* Send SET_CONFIG(SET_TRAINING:0xff) to notify DPOA that
			 * DPTX-to-DPIA hop trained. No DPCD write needed for first hop.
			 */
			status = core_link_send_set_config(link,
					DPIA_SET_CFG_SET_TRAINING,
					DPIA_TS_UFP_DONE);
			if (status != DC_OK)
				result = LINK_TRAINING_ABORT;
		} else { /* DPOA-to-x */
			/* Write 0x0 to TRAINING_PATTERN_SET */
			status = dpcd_clear_lt_pattern(link, hop);
			if (status != DC_OK)
				result = LINK_TRAINING_ABORT;
		}

		/* Notify DPOA that non-transparent link training of DPRX done. */
		if (hop == DPRX && result != LINK_TRAINING_ABORT) {
			status = core_link_send_set_config(link,
					DPIA_SET_CFG_SET_TRAINING,
					DPIA_TS_DPRX_DONE);
			if (status != DC_OK)
				result = LINK_TRAINING_ABORT;
		}

	} else { /* non-LTTPR or transparent LTTPR. */
		/* Write 0x0 to TRAINING_PATTERN_SET */
		status = dpcd_clear_lt_pattern(link, hop);
		if (status != DC_OK)
			result = LINK_TRAINING_ABORT;
	}

	DC_LOG_HW_LINK_TRAINING("%s\n DPIA(%d) end\n - hop(%d)\n - result(%d)\n - LTTPR mode(%d)\n",
				__func__,
				link->link_id.enum_id - ENUM_ID_1,
				hop,
				result,
				link->lttpr_mode);

	return result;
}

/* When aborting training of specified hop in display path, clean up by:
 * - Attempting to clear DPCD TRAINING_PATTERN_SET, LINK_BW_SET and LANE_COUNT_SET.
 * - Sending SET_CONFIG(SET_LINK) with lane count and link rate set to 0.
 *
 * @param link DPIA link being trained.
 * @param hop The Hop in display path. DPRX = 0.
 */
static void dpia_training_abort(struct dc_link *link, uint32_t hop)
{
	uint8_t data = 0;
	uint32_t dpcd_tps_offset = DP_TRAINING_PATTERN_SET;

	DC_LOG_HW_LINK_TRAINING("%s\n DPIA(%d) aborting\n - LTTPR mode(%d)\n - HPD(%d)\n",
				__func__,
				link->link_id.enum_id - ENUM_ID_1,
				link->lttpr_mode,
				link->hpd_status);

	/* Abandon clean-up if sink unplugged. */
	if (!link->hpd_status)
		return;

	if (hop != DPRX)
		dpcd_tps_offset = DP_TRAINING_PATTERN_SET_PHY_REPEATER1 +
			((DP_REPEATER_CONFIGURATION_AND_STATUS_SIZE) * (hop - 1));

	core_link_write_dpcd(link, dpcd_tps_offset, &data, 1);
	core_link_write_dpcd(link, DP_LINK_BW_SET, &data, 1);
	core_link_write_dpcd(link, DP_LANE_COUNT_SET, &data, 1);
	core_link_send_set_config(link, DPIA_SET_CFG_SET_LINK, data);
}

enum link_training_result dc_link_dpia_perform_link_training(struct dc_link *link,
	const struct dc_link_settings *link_setting,
	bool skip_video_pattern)
{
	enum link_training_result result;
	struct link_training_settings lt_settings;
	uint8_t repeater_cnt = 0; /* Number of hops/repeaters in display path. */
	int8_t repeater_id; /* Current hop. */

	/* Configure link as prescribed in link_setting and set LTTPR mode. */
	result = dpia_configure_link(link, link_setting, &lt_settings);
	if (result != LINK_TRAINING_SUCCESS)
		return result;

	if (link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT)
		repeater_cnt = dp_convert_to_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt);

	/* Train each hop in turn starting with the one closest to DPTX.
	 * In transparent or non-LTTPR mode, train only the final hop (DPRX).
	 */
	for (repeater_id = repeater_cnt; repeater_id >= 0; repeater_id--) {
		/* Clock recovery. */
		result = dpia_training_cr_phase(link, &lt_settings, repeater_id);
		if (result != LINK_TRAINING_SUCCESS)
			break;

		/* Equalization. */
		result = dpia_training_eq_phase(link, &lt_settings, repeater_id);
		if (result != LINK_TRAINING_SUCCESS)
			break;

		/* Stop training hop. */
		result = dpia_training_end(link, repeater_id);
		if (result != LINK_TRAINING_SUCCESS)
			break;
	}

	/* Double-check link status if training successful; gracefully abort
	 * training of current hop if training failed due to message tunneling
	 * failure; end training of hop if training ended conventionally and
	 * falling back to lower bandwidth settings possible.
	 */
	if (result == LINK_TRAINING_SUCCESS) {
		msleep(5);
		result = dp_check_link_loss_status(link, &lt_settings);
	} else if (result == LINK_TRAINING_ABORT) {
		dpia_training_abort(link, repeater_id);
	} else {
		dpia_training_end(link, repeater_id);
	}
	return result;
}
