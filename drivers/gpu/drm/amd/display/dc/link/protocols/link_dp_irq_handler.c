/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

/* FILE POLICY AND INTENDED USAGE:
 * This file implements DP HPD short pulse handling sequence according to DP
 * specifications
 *
 */

#include "link_dp_irq_handler.h"
#include "link_dpcd.h"
#include "link_dp_training.h"
#include "link_dp_capability.h"
#include "link_edp_panel_control.h"
#include "link/accessories/link_dp_trace.h"
#include "link/link_dpms.h"
#include "dm_helpers.h"

#define DC_LOGGER \
	link->ctx->logger
#define DC_LOGGER_INIT(logger)

bool dp_parse_link_loss_status(
	struct dc_link *link,
	union hpd_irq_data *hpd_irq_dpcd_data)
{
	uint8_t irq_reg_rx_power_state = 0;
	enum dc_status dpcd_result = DC_ERROR_UNEXPECTED;
	union lane_status lane_status;
	uint32_t lane;
	bool sink_status_changed;
	bool return_code;

	sink_status_changed = false;
	return_code = false;

	if (link->cur_link_settings.lane_count == 0)
		return return_code;

	/*1. Check that Link Status changed, before re-training.*/

	/*parse lane status*/
	for (lane = 0; lane < link->cur_link_settings.lane_count; lane++) {
		/* check status of lanes 0,1
		 * changed DpcdAddress_Lane01Status (0x202)
		 */
		lane_status.raw = dp_get_nibble_at_index(
			&hpd_irq_dpcd_data->bytes.lane01_status.raw,
			lane);

		if (!lane_status.bits.CHANNEL_EQ_DONE_0 ||
			!lane_status.bits.CR_DONE_0 ||
			!lane_status.bits.SYMBOL_LOCKED_0) {
			/* if one of the channel equalization, clock
			 * recovery or symbol lock is dropped
			 * consider it as (link has been
			 * dropped) dp sink status has changed
			 */
			sink_status_changed = true;
			break;
		}
	}

	/* Check interlane align.*/
	if (link_dp_get_encoding_format(&link->cur_link_settings) == DP_128b_132b_ENCODING &&
			(!hpd_irq_dpcd_data->bytes.lane_status_updated.bits.EQ_INTERLANE_ALIGN_DONE_128b_132b ||
			 !hpd_irq_dpcd_data->bytes.lane_status_updated.bits.CDS_INTERLANE_ALIGN_DONE_128b_132b)) {
		sink_status_changed = true;
	} else if (!hpd_irq_dpcd_data->bytes.lane_status_updated.bits.INTERLANE_ALIGN_DONE) {
		sink_status_changed = true;
	}

	if (sink_status_changed) {

		DC_LOG_HW_HPD_IRQ("%s: Link Status changed.\n", __func__);

		return_code = true;

		/*2. Check that we can handle interrupt: Not in FS DOS,
		 *  Not in "Display Timeout" state, Link is trained.
		 */
		dpcd_result = core_link_read_dpcd(link,
			DP_SET_POWER,
			&irq_reg_rx_power_state,
			sizeof(irq_reg_rx_power_state));

		if (dpcd_result != DC_OK) {
			DC_LOG_HW_HPD_IRQ("%s: DPCD read failed to obtain power state.\n",
				__func__);
		} else {
			if (irq_reg_rx_power_state != DP_SET_POWER_D0)
				return_code = false;
		}
	}

	return return_code;
}

static bool handle_hpd_irq_psr_sink(struct dc_link *link)
{
	union dpcd_psr_configuration psr_configuration = {0};

	if (!link->psr_settings.psr_feature_enabled)
		return false;

	dm_helpers_dp_read_dpcd(
		link->ctx,
		link,
		368,/*DpcdAddress_PSR_Enable_Cfg*/
		&psr_configuration.raw,
		sizeof(psr_configuration.raw));

	if (psr_configuration.bits.ENABLE) {
		unsigned char dpcdbuf[3] = {0};
		union psr_error_status psr_error_status;
		union psr_sink_psr_status psr_sink_psr_status;

		dm_helpers_dp_read_dpcd(
			link->ctx,
			link,
			0x2006, /*DpcdAddress_PSR_Error_Status*/
			(unsigned char *) dpcdbuf,
			sizeof(dpcdbuf));

		/*DPCD 2006h   ERROR STATUS*/
		psr_error_status.raw = dpcdbuf[0];
		/*DPCD 2008h   SINK PANEL SELF REFRESH STATUS*/
		psr_sink_psr_status.raw = dpcdbuf[2];

		if (psr_error_status.bits.LINK_CRC_ERROR ||
				psr_error_status.bits.RFB_STORAGE_ERROR ||
				psr_error_status.bits.VSC_SDP_ERROR) {
			bool allow_active;

			/* Acknowledge and clear error bits */
			dm_helpers_dp_write_dpcd(
				link->ctx,
				link,
				8198,/*DpcdAddress_PSR_Error_Status*/
				&psr_error_status.raw,
				sizeof(psr_error_status.raw));

			/* PSR error, disable and re-enable PSR */
			if (link->psr_settings.psr_allow_active) {
				allow_active = false;
				edp_set_psr_allow_active(link, &allow_active, true, false, NULL);
				allow_active = true;
				edp_set_psr_allow_active(link, &allow_active, true, false, NULL);
			}

			return true;
		} else if (psr_sink_psr_status.bits.SINK_SELF_REFRESH_STATUS ==
				PSR_SINK_STATE_ACTIVE_DISPLAY_FROM_SINK_RFB){
			/* No error is detect, PSR is active.
			 * We should return with IRQ_HPD handled without
			 * checking for loss of sync since PSR would have
			 * powered down main link.
			 */
			return true;
		}
	}
	return false;
}

static void handle_hpd_irq_replay_sink(struct dc_link *link)
{
	union dpcd_replay_configuration replay_configuration = {0};
	/*AMD Replay version reuse DP_PSR_ERROR_STATUS for REPLAY_ERROR status.*/
	union psr_error_status replay_error_status = {0};

	if (!link->replay_settings.replay_feature_enabled)
		return;

	dm_helpers_dp_read_dpcd(
		link->ctx,
		link,
		DP_SINK_PR_REPLAY_STATUS,
		&replay_configuration.raw,
		sizeof(replay_configuration.raw));

	dm_helpers_dp_read_dpcd(
		link->ctx,
		link,
		DP_PSR_ERROR_STATUS,
		&replay_error_status.raw,
		sizeof(replay_error_status.raw));

	link->replay_settings.config.replay_error_status.bits.LINK_CRC_ERROR =
		replay_error_status.bits.LINK_CRC_ERROR;
	link->replay_settings.config.replay_error_status.bits.DESYNC_ERROR =
		replay_configuration.bits.DESYNC_ERROR_STATUS;
	link->replay_settings.config.replay_error_status.bits.STATE_TRANSITION_ERROR =
		replay_configuration.bits.STATE_TRANSITION_ERROR_STATUS;

	if (link->replay_settings.config.replay_error_status.bits.LINK_CRC_ERROR ||
		link->replay_settings.config.replay_error_status.bits.DESYNC_ERROR ||
		link->replay_settings.config.replay_error_status.bits.STATE_TRANSITION_ERROR) {
		bool allow_active;

		if (link->replay_settings.config.replay_error_status.bits.DESYNC_ERROR)
			link->replay_settings.config.received_desync_error_hpd = 1;

		if (link->replay_settings.config.force_disable_desync_error_check)
			return;

		/* Acknowledge and clear configuration bits */
		dm_helpers_dp_write_dpcd(
			link->ctx,
			link,
			DP_SINK_PR_REPLAY_STATUS,
			&replay_configuration.raw,
			sizeof(replay_configuration.raw));

		/* Acknowledge and clear error bits */
		dm_helpers_dp_write_dpcd(
			link->ctx,
			link,
			DP_PSR_ERROR_STATUS,/*DpcdAddress_REPLAY_Error_Status*/
			&replay_error_status.raw,
			sizeof(replay_error_status.raw));

		/* Replay error, disable and re-enable Replay */
		if (link->replay_settings.replay_allow_active) {
			allow_active = false;
			edp_set_replay_allow_active(link, &allow_active, true, false, NULL);
			allow_active = true;
			edp_set_replay_allow_active(link, &allow_active, true, false, NULL);
		}
	}
}

void dp_handle_link_loss(struct dc_link *link)
{
	struct pipe_ctx *pipes[MAX_PIPES];
	struct dc_state *state = link->dc->current_state;
	uint8_t count;
	int i;

	link_get_master_pipes_with_dpms_on(link, state, &count, pipes);

	for (i = 0; i < count; i++)
		link_set_dpms_off(pipes[i]);

	for (i = count - 1; i >= 0; i--) {
		// Always use max settings here for DP 1.4a LL Compliance CTS
		if (link->skip_fallback_on_link_loss) {
			pipes[i]->link_config.dp_link_settings.lane_count =
					link->verified_link_cap.lane_count;
			pipes[i]->link_config.dp_link_settings.link_rate =
					link->verified_link_cap.link_rate;
			pipes[i]->link_config.dp_link_settings.link_spread =
					link->verified_link_cap.link_spread;
		}
		link_set_dpms_on(link->dc->current_state, pipes[i]);
	}
}

static void read_dpcd204h_on_irq_hpd(struct dc_link *link, union hpd_irq_data *irq_data)
{
	enum dc_status retval;
	union lane_align_status_updated dpcd_lane_status_updated = {0};

	retval = core_link_read_dpcd(
			link,
			DP_LANE_ALIGN_STATUS_UPDATED,
			&dpcd_lane_status_updated.raw,
			sizeof(union lane_align_status_updated));

	if (retval == DC_OK) {
		irq_data->bytes.lane_status_updated.bits.EQ_INTERLANE_ALIGN_DONE_128b_132b =
				dpcd_lane_status_updated.bits.EQ_INTERLANE_ALIGN_DONE_128b_132b;
		irq_data->bytes.lane_status_updated.bits.CDS_INTERLANE_ALIGN_DONE_128b_132b =
				dpcd_lane_status_updated.bits.CDS_INTERLANE_ALIGN_DONE_128b_132b;
	}
}

enum dc_status dp_read_hpd_rx_irq_data(
	struct dc_link *link,
	union hpd_irq_data *irq_data)
{
	static enum dc_status retval;

	/* The HW reads 16 bytes from 200h on HPD,
	 * but if we get an AUX_DEFER, the HW cannot retry
	 * and this causes the CTS tests 4.3.2.1 - 3.2.4 to
	 * fail, so we now explicitly read 6 bytes which is
	 * the req from the above mentioned test cases.
	 *
	 * For DP 1.4 we need to read those from 2002h range.
	 */
	if (link->dpcd_caps.dpcd_rev.raw < DPCD_REV_14)
		retval = core_link_read_dpcd(
			link,
			DP_SINK_COUNT,
			irq_data->raw,
			sizeof(union hpd_irq_data));
	else {
		/* Read 14 bytes in a single read and then copy only the required fields.
		 * This is more efficient than doing it in two separate AUX reads. */

		uint8_t tmp[DP_SINK_STATUS_ESI - DP_SINK_COUNT_ESI + 1] = {0};

		retval = core_link_read_dpcd(
			link,
			DP_SINK_COUNT_ESI,
			tmp,
			sizeof(tmp));

		if (retval != DC_OK)
			return retval;

		irq_data->bytes.sink_cnt.raw = tmp[DP_SINK_COUNT_ESI - DP_SINK_COUNT_ESI];
		irq_data->bytes.device_service_irq.raw = tmp[DP_DEVICE_SERVICE_IRQ_VECTOR_ESI0 - DP_SINK_COUNT_ESI];
		irq_data->bytes.lane01_status.raw = tmp[DP_LANE0_1_STATUS_ESI - DP_SINK_COUNT_ESI];
		irq_data->bytes.lane23_status.raw = tmp[DP_LANE2_3_STATUS_ESI - DP_SINK_COUNT_ESI];
		irq_data->bytes.lane_status_updated.raw = tmp[DP_LANE_ALIGN_STATUS_UPDATED_ESI - DP_SINK_COUNT_ESI];
		irq_data->bytes.sink_status.raw = tmp[DP_SINK_STATUS_ESI - DP_SINK_COUNT_ESI];

		/*
		 * This display doesn't have correct values in DPCD200Eh.
		 * Read and check DPCD204h instead.
		 */
		if (link->wa_flags.read_dpcd204h_on_irq_hpd)
			read_dpcd204h_on_irq_hpd(link, irq_data);
	}

	return retval;
}

/*************************Short Pulse IRQ***************************/
bool dp_should_allow_hpd_rx_irq(const struct dc_link *link)
{
	/*
	 * Don't handle RX IRQ unless one of following is met:
	 * 1) The link is established (cur_link_settings != unknown)
	 * 2) We know we're dealing with a branch device, SST or MST
	 */

	if ((link->cur_link_settings.lane_count != LANE_COUNT_UNKNOWN) ||
		is_dp_branch_device(link))
		return true;

	return false;
}

bool dp_handle_hpd_rx_irq(struct dc_link *link,
		union hpd_irq_data *out_hpd_irq_dpcd_data, bool *out_link_loss,
		bool defer_handling, bool *has_left_work)
{
	union hpd_irq_data hpd_irq_dpcd_data = {0};
	union device_service_irq device_service_clear = {0};
	enum dc_status result;
	bool status = false;

	if (out_link_loss)
		*out_link_loss = false;

	if (has_left_work)
		*has_left_work = false;
	/* For use cases related to down stream connection status change,
	 * PSR and device auto test, refer to function handle_sst_hpd_irq
	 * in DAL2.1*/

	DC_LOG_HW_HPD_IRQ("%s: Got short pulse HPD on link %d\n",
		__func__, link->link_index);


	 /* All the "handle_hpd_irq_xxx()" methods
		 * should be called only after
		 * dal_dpsst_ls_read_hpd_irq_data
		 * Order of calls is important too
		 */
	result = dp_read_hpd_rx_irq_data(link, &hpd_irq_dpcd_data);
	if (out_hpd_irq_dpcd_data)
		*out_hpd_irq_dpcd_data = hpd_irq_dpcd_data;

	if (result != DC_OK) {
		DC_LOG_HW_HPD_IRQ("%s: DPCD read failed to obtain irq data\n",
			__func__);
		return false;
	}

	if (hpd_irq_dpcd_data.bytes.device_service_irq.bits.AUTOMATED_TEST) {
		// Workaround for DP 1.4a LL Compliance CTS as USB4 has to share encoders unlike DP and USBC
		if (link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA)
			link->skip_fallback_on_link_loss = true;

		device_service_clear.bits.AUTOMATED_TEST = 1;
		core_link_write_dpcd(
			link,
			DP_DEVICE_SERVICE_IRQ_VECTOR,
			&device_service_clear.raw,
			sizeof(device_service_clear.raw));
		device_service_clear.raw = 0;
		if (defer_handling && has_left_work)
			*has_left_work = true;
		else
			dc_link_dp_handle_automated_test(link);
		return false;
	}

	if (!dp_should_allow_hpd_rx_irq(link)) {
		DC_LOG_HW_HPD_IRQ("%s: skipping HPD handling on %d\n",
			__func__, link->link_index);
		return false;
	}

	if (handle_hpd_irq_psr_sink(link))
		/* PSR-related error was detected and handled */
		return true;

	handle_hpd_irq_replay_sink(link);

	/* If PSR-related error handled, Main link may be off,
	 * so do not handle as a normal sink status change interrupt.
	 */

	if (hpd_irq_dpcd_data.bytes.device_service_irq.bits.UP_REQ_MSG_RDY) {
		if (defer_handling && has_left_work)
			*has_left_work = true;
		return true;
	}

	/* check if we have MST msg and return since we poll for it */
	if (hpd_irq_dpcd_data.bytes.device_service_irq.bits.DOWN_REP_MSG_RDY) {
		if (defer_handling && has_left_work)
			*has_left_work = true;
		return false;
	}

	/* For now we only handle 'Downstream port status' case.
	 * If we got sink count changed it means
	 * Downstream port status changed,
	 * then DM should call DC to do the detection.
	 * NOTE: Do not handle link loss on eDP since it is internal link*/
	if ((link->connector_signal != SIGNAL_TYPE_EDP) &&
			dp_parse_link_loss_status(
					link,
					&hpd_irq_dpcd_data)) {
		/* Connectivity log: link loss */
		CONN_DATA_LINK_LOSS(link,
					hpd_irq_dpcd_data.raw,
					sizeof(hpd_irq_dpcd_data),
					"Status: ");

		if (defer_handling && has_left_work)
			*has_left_work = true;
		else
			dp_handle_link_loss(link);

		status = false;
		if (out_link_loss)
			*out_link_loss = true;

		dp_trace_link_loss_increment(link);
	}

	if (link->type == dc_connection_sst_branch &&
		hpd_irq_dpcd_data.bytes.sink_cnt.bits.SINK_COUNT
			!= link->dpcd_sink_count)
		status = true;

	/* reasons for HPD RX:
	 * 1. Link Loss - ie Re-train the Link
	 * 2. MST sideband message
	 * 3. Automated Test - ie. Internal Commit
	 * 4. CP (copy protection) - (not interesting for DM???)
	 * 5. DRR
	 * 6. Downstream Port status changed
	 * -ie. Detect - this the only one
	 * which is interesting for DM because
	 * it must call dc_link_detect.
	 */
	return status;
}
