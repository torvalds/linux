/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#include "hdcp.h"

static inline enum mod_hdcp_status validate_bksv(struct mod_hdcp *hdcp)
{
	uint64_t n = 0;
	uint8_t count = 0;

	memcpy(&n, hdcp->auth.msg.hdcp1.bksv, sizeof(uint64_t));

	while (n) {
		count++;
		n &= (n - 1);
	}
	return (count == 20) ? MOD_HDCP_STATUS_SUCCESS :
			MOD_HDCP_STATUS_HDCP1_INVALID_BKSV;
}

static inline enum mod_hdcp_status check_ksv_ready(struct mod_hdcp *hdcp)
{
	if (is_dp_hdcp(hdcp))
		return (hdcp->auth.msg.hdcp1.bstatus & DP_BSTATUS_READY) ?
				MOD_HDCP_STATUS_SUCCESS :
				MOD_HDCP_STATUS_HDCP1_KSV_LIST_NOT_READY;
	return (hdcp->auth.msg.hdcp1.bcaps & DRM_HDCP_DDC_BCAPS_KSV_FIFO_READY) ?
			MOD_HDCP_STATUS_SUCCESS :
			MOD_HDCP_STATUS_HDCP1_KSV_LIST_NOT_READY;
}

static inline enum mod_hdcp_status check_hdcp_capable_dp(struct mod_hdcp *hdcp)
{
	return (hdcp->auth.msg.hdcp1.bcaps & DP_BCAPS_HDCP_CAPABLE) ?
			MOD_HDCP_STATUS_SUCCESS :
			MOD_HDCP_STATUS_HDCP1_NOT_CAPABLE;
}

static inline enum mod_hdcp_status check_r0p_available_dp(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;
	if (is_dp_hdcp(hdcp)) {
		status = (hdcp->auth.msg.hdcp1.bstatus &
				DP_BSTATUS_R0_PRIME_READY) ?
			MOD_HDCP_STATUS_SUCCESS :
			MOD_HDCP_STATUS_HDCP1_R0_PRIME_PENDING;
	} else {
		status = MOD_HDCP_STATUS_INVALID_OPERATION;
	}
	return status;
}

static inline enum mod_hdcp_status check_link_integrity_dp(
		struct mod_hdcp *hdcp)
{
	return (hdcp->auth.msg.hdcp1.bstatus &
			DP_BSTATUS_LINK_FAILURE) ?
			MOD_HDCP_STATUS_HDCP1_LINK_INTEGRITY_FAILURE :
			MOD_HDCP_STATUS_SUCCESS;
}

static inline enum mod_hdcp_status check_no_reauthentication_request_dp(
		struct mod_hdcp *hdcp)
{
	return (hdcp->auth.msg.hdcp1.bstatus & DP_BSTATUS_REAUTH_REQ) ?
			MOD_HDCP_STATUS_HDCP1_REAUTH_REQUEST_ISSUED :
			MOD_HDCP_STATUS_SUCCESS;
}

static inline enum mod_hdcp_status check_no_max_cascade(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp))
		status = DRM_HDCP_MAX_CASCADE_EXCEEDED(hdcp->auth.msg.hdcp1.binfo_dp >> 8)
				 ? MOD_HDCP_STATUS_HDCP1_MAX_CASCADE_EXCEEDED_FAILURE
				 : MOD_HDCP_STATUS_SUCCESS;
	else
		status = DRM_HDCP_MAX_CASCADE_EXCEEDED(hdcp->auth.msg.hdcp1.bstatus >> 8)
				 ? MOD_HDCP_STATUS_HDCP1_MAX_CASCADE_EXCEEDED_FAILURE
				 : MOD_HDCP_STATUS_SUCCESS;
	return status;
}

static inline enum mod_hdcp_status check_no_max_devs(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp))
		status = DRM_HDCP_MAX_DEVICE_EXCEEDED(hdcp->auth.msg.hdcp1.binfo_dp) ?
				MOD_HDCP_STATUS_HDCP1_MAX_DEVS_EXCEEDED_FAILURE :
				MOD_HDCP_STATUS_SUCCESS;
	else
		status = DRM_HDCP_MAX_DEVICE_EXCEEDED(hdcp->auth.msg.hdcp1.bstatus) ?
				MOD_HDCP_STATUS_HDCP1_MAX_DEVS_EXCEEDED_FAILURE :
				MOD_HDCP_STATUS_SUCCESS;
	return status;
}

static inline uint8_t get_device_count(struct mod_hdcp *hdcp)
{
	return is_dp_hdcp(hdcp) ?
			DRM_HDCP_NUM_DOWNSTREAM(hdcp->auth.msg.hdcp1.binfo_dp) :
			DRM_HDCP_NUM_DOWNSTREAM(hdcp->auth.msg.hdcp1.bstatus);
}

static inline enum mod_hdcp_status check_device_count(struct mod_hdcp *hdcp)
{
	/* device count must be greater than or equal to tracked hdcp displays */
	return (get_device_count(hdcp) < get_added_display_count(hdcp)) ?
			MOD_HDCP_STATUS_HDCP1_DEVICE_COUNT_MISMATCH_FAILURE :
			MOD_HDCP_STATUS_SUCCESS;
}

static enum mod_hdcp_status wait_for_active_rx(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp1 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(mod_hdcp_read_bksv,
			&input->bksv_read, &status,
			hdcp, "bksv_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_read_bcaps,
			&input->bcaps_read, &status,
			hdcp, "bcaps_read"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status exchange_ksvs(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp1 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(mod_hdcp_add_display_topology,
			&input->add_topology, &status,
			hdcp, "add_topology"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp1_create_session,
			&input->create_session, &status,
			hdcp, "create_session"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_write_an,
			&input->an_write, &status,
			hdcp, "an_write"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_write_aksv,
			&input->aksv_write, &status,
			hdcp, "aksv_write"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_read_bksv,
			&input->bksv_read, &status,
			hdcp, "bksv_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(validate_bksv,
			&input->bksv_validation, &status,
			hdcp, "bksv_validation"))
		goto out;
	if (hdcp->auth.msg.hdcp1.ainfo) {
		if (!mod_hdcp_execute_and_set(mod_hdcp_write_ainfo,
				&input->ainfo_write, &status,
				hdcp, "ainfo_write"))
			goto out;
	}
out:
	return status;
}

static enum mod_hdcp_status computations_validate_rx_test_for_repeater(
		struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp1 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(mod_hdcp_read_r0p,
			&input->r0p_read, &status,
			hdcp, "r0p_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp1_validate_rx,
			&input->rx_validation, &status,
			hdcp, "rx_validation"))
		goto out;
	if (hdcp->connection.is_repeater) {
		if (!hdcp->connection.link.adjust.hdcp1.postpone_encryption)
			if (!mod_hdcp_execute_and_set(
					mod_hdcp_hdcp1_enable_encryption,
					&input->encryption, &status,
					hdcp, "encryption"))
				goto out;
	} else {
		if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp1_enable_encryption,
				&input->encryption, &status,
				hdcp, "encryption"))
			goto out;
		if (is_dp_mst_hdcp(hdcp))
			if (!mod_hdcp_execute_and_set(
					mod_hdcp_hdcp1_enable_dp_stream_encryption,
					&input->stream_encryption_dp, &status,
					hdcp, "stream_encryption_dp"))
				goto out;
	}
out:
	return status;
}

static enum mod_hdcp_status authenticated(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp1 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp1_link_maintenance,
			&input->link_maintenance, &status,
			hdcp, "link_maintenance"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status wait_for_ready(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp1 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK &&
			event_ctx->event != MOD_HDCP_EVENT_CPIRQ &&
			event_ctx->event != MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (is_dp_hdcp(hdcp)) {
		if (!mod_hdcp_execute_and_set(mod_hdcp_read_bstatus,
				&input->bstatus_read, &status,
				hdcp, "bstatus_read"))
			goto out;
		if (!mod_hdcp_execute_and_set(check_link_integrity_dp,
				&input->link_integiry_check, &status,
				hdcp, "link_integiry_check"))
			goto out;
		if (!mod_hdcp_execute_and_set(check_no_reauthentication_request_dp,
				&input->reauth_request_check, &status,
				hdcp, "reauth_request_check"))
			goto out;
	} else {
		if (!mod_hdcp_execute_and_set(mod_hdcp_read_bcaps,
				&input->bcaps_read, &status,
				hdcp, "bcaps_read"))
			goto out;
	}
	if (!mod_hdcp_execute_and_set(check_ksv_ready,
			&input->ready_check, &status,
			hdcp, "ready_check"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status read_ksv_list(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp1 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;
	uint8_t device_count;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (is_dp_hdcp(hdcp)) {
		if (!mod_hdcp_execute_and_set(mod_hdcp_read_binfo,
				&input->binfo_read_dp, &status,
				hdcp, "binfo_read_dp"))
			goto out;
	} else {
		if (!mod_hdcp_execute_and_set(mod_hdcp_read_bstatus,
				&input->bstatus_read, &status,
				hdcp, "bstatus_read"))
			goto out;
	}
	if (!mod_hdcp_execute_and_set(check_no_max_cascade,
			&input->max_cascade_check, &status,
			hdcp, "max_cascade_check"))
		goto out;
	if (!mod_hdcp_execute_and_set(check_no_max_devs,
			&input->max_devs_check, &status,
			hdcp, "max_devs_check"))
		goto out;
	if (!mod_hdcp_execute_and_set(check_device_count,
			&input->device_count_check, &status,
			hdcp, "device_count_check"))
		goto out;
	device_count = get_device_count(hdcp);
	hdcp->auth.msg.hdcp1.ksvlist_size = device_count*5;
	if (!mod_hdcp_execute_and_set(mod_hdcp_read_ksvlist,
			&input->ksvlist_read, &status,
			hdcp, "ksvlist_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_read_vp,
			&input->vp_read, &status,
			hdcp, "vp_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp1_validate_ksvlist_vp,
			&input->ksvlist_vp_validation, &status,
			hdcp, "ksvlist_vp_validation"))
		goto out;
	if (input->encryption != PASS)
		if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp1_enable_encryption,
				&input->encryption, &status,
				hdcp, "encryption"))
			goto out;
	if (is_dp_mst_hdcp(hdcp))
		if (!mod_hdcp_execute_and_set(
				mod_hdcp_hdcp1_enable_dp_stream_encryption,
				&input->stream_encryption_dp, &status,
				hdcp, "stream_encryption_dp"))
			goto out;
out:
	return status;
}

static enum mod_hdcp_status determine_rx_hdcp_capable_dp(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp1 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(mod_hdcp_read_bcaps,
			&input->bcaps_read, &status,
			hdcp, "bcaps_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(check_hdcp_capable_dp,
			&input->hdcp_capable_dp, &status,
			hdcp, "hdcp_capable_dp"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status wait_for_r0_prime_dp(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp1 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CPIRQ &&
			event_ctx->event != MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(mod_hdcp_read_bstatus,
			&input->bstatus_read, &status,
			hdcp, "bstatus_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(check_r0p_available_dp,
			&input->r0p_available_dp, &status,
			hdcp, "r0p_available_dp"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status authenticated_dp(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp1 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CPIRQ) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(mod_hdcp_read_bstatus,
			&input->bstatus_read, &status,
			hdcp, "bstatus_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(check_link_integrity_dp,
			&input->link_integiry_check, &status,
			hdcp, "link_integiry_check"))
		goto out;
	if (!mod_hdcp_execute_and_set(check_no_reauthentication_request_dp,
			&input->reauth_request_check, &status,
			hdcp, "reauth_request_check"))
		goto out;
out:
	return status;
}

uint8_t mod_hdcp_execute_and_set(
		mod_hdcp_action func, uint8_t *flag,
		enum mod_hdcp_status *status, struct mod_hdcp *hdcp, char *str)
{
	*status = func(hdcp);
	if (*status == MOD_HDCP_STATUS_SUCCESS && *flag != PASS) {
		HDCP_INPUT_PASS_TRACE(hdcp, str);
		*flag = PASS;
	} else if (*status != MOD_HDCP_STATUS_SUCCESS && *flag != FAIL) {
		HDCP_INPUT_FAIL_TRACE(hdcp, str);
		*flag = FAIL;
	}
	return (*status == MOD_HDCP_STATUS_SUCCESS);
}

enum mod_hdcp_status mod_hdcp_hdcp1_execution(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp1 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	switch (current_state(hdcp)) {
	case H1_A0_WAIT_FOR_ACTIVE_RX:
		status = wait_for_active_rx(hdcp, event_ctx, input);
		break;
	case H1_A1_EXCHANGE_KSVS:
		status = exchange_ksvs(hdcp, event_ctx, input);
		break;
	case H1_A2_COMPUTATIONS_A3_VALIDATE_RX_A6_TEST_FOR_REPEATER:
		status = computations_validate_rx_test_for_repeater(hdcp,
				event_ctx, input);
		break;
	case H1_A45_AUTHENTICATED:
		status = authenticated(hdcp, event_ctx, input);
		break;
	case H1_A8_WAIT_FOR_READY:
		status = wait_for_ready(hdcp, event_ctx, input);
		break;
	case H1_A9_READ_KSV_LIST:
		status = read_ksv_list(hdcp, event_ctx, input);
		break;
	default:
		status = MOD_HDCP_STATUS_INVALID_STATE;
		break;
	}

	return status;
}

extern enum mod_hdcp_status mod_hdcp_hdcp1_dp_execution(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp1 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	switch (current_state(hdcp)) {
	case D1_A0_DETERMINE_RX_HDCP_CAPABLE:
		status = determine_rx_hdcp_capable_dp(hdcp, event_ctx, input);
		break;
	case D1_A1_EXCHANGE_KSVS:
		status = exchange_ksvs(hdcp, event_ctx, input);
		break;
	case D1_A23_WAIT_FOR_R0_PRIME:
		status = wait_for_r0_prime_dp(hdcp, event_ctx, input);
		break;
	case D1_A2_COMPUTATIONS_A3_VALIDATE_RX_A5_TEST_FOR_REPEATER:
		status = computations_validate_rx_test_for_repeater(
				hdcp, event_ctx, input);
		break;
	case D1_A4_AUTHENTICATED:
		status = authenticated_dp(hdcp, event_ctx, input);
		break;
	case D1_A6_WAIT_FOR_READY:
		status = wait_for_ready(hdcp, event_ctx, input);
		break;
	case D1_A7_READ_KSV_LIST:
		status = read_ksv_list(hdcp, event_ctx, input);
		break;
	default:
		status = MOD_HDCP_STATUS_INVALID_STATE;
		break;
	}

	return status;
}
