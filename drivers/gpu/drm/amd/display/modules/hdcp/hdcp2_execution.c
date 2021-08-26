/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#include <linux/delay.h>

#include "hdcp.h"

static inline enum mod_hdcp_status check_receiver_id_list_ready(struct mod_hdcp *hdcp)
{
	uint8_t is_ready = 0;

	if (is_dp_hdcp(hdcp))
		is_ready = HDCP_2_2_DP_RXSTATUS_READY(hdcp->auth.msg.hdcp2.rxstatus_dp) ? 1 : 0;
	else
		is_ready = (HDCP_2_2_HDMI_RXSTATUS_READY(hdcp->auth.msg.hdcp2.rxstatus[1]) &&
				(HDCP_2_2_HDMI_RXSTATUS_MSG_SZ_HI(hdcp->auth.msg.hdcp2.rxstatus[1]) << 8 |
						hdcp->auth.msg.hdcp2.rxstatus[0])) ? 1 : 0;
	return is_ready ? MOD_HDCP_STATUS_SUCCESS :
			MOD_HDCP_STATUS_HDCP2_RX_ID_LIST_NOT_READY;
}

static inline enum mod_hdcp_status check_hdcp2_capable(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;

	if (is_dp_hdcp(hdcp))
		status = (hdcp->auth.msg.hdcp2.rxcaps_dp[0] == HDCP_2_2_RX_CAPS_VERSION_VAL) &&
				HDCP_2_2_DP_HDCP_CAPABLE(hdcp->auth.msg.hdcp2.rxcaps_dp[2]) ?
				MOD_HDCP_STATUS_SUCCESS :
				MOD_HDCP_STATUS_HDCP2_NOT_CAPABLE;
	else
		status = (hdcp->auth.msg.hdcp2.hdcp2version_hdmi & HDCP_2_2_HDMI_SUPPORT_MASK) ?
				MOD_HDCP_STATUS_SUCCESS :
				MOD_HDCP_STATUS_HDCP2_NOT_CAPABLE;
	return status;
}

static inline enum mod_hdcp_status check_reauthentication_request(
		struct mod_hdcp *hdcp)
{
	uint8_t ret = 0;

	if (is_dp_hdcp(hdcp))
		ret = HDCP_2_2_DP_RXSTATUS_REAUTH_REQ(hdcp->auth.msg.hdcp2.rxstatus_dp) ?
				MOD_HDCP_STATUS_HDCP2_REAUTH_REQUEST :
				MOD_HDCP_STATUS_SUCCESS;
	else
		ret = HDCP_2_2_HDMI_RXSTATUS_REAUTH_REQ(hdcp->auth.msg.hdcp2.rxstatus[1]) ?
				MOD_HDCP_STATUS_HDCP2_REAUTH_REQUEST :
				MOD_HDCP_STATUS_SUCCESS;
	return ret;
}

static inline enum mod_hdcp_status check_link_integrity_failure_dp(
		struct mod_hdcp *hdcp)
{
	return HDCP_2_2_DP_RXSTATUS_LINK_FAILED(hdcp->auth.msg.hdcp2.rxstatus_dp) ?
			MOD_HDCP_STATUS_HDCP2_REAUTH_LINK_INTEGRITY_FAILURE :
			MOD_HDCP_STATUS_SUCCESS;
}

static enum mod_hdcp_status check_ake_cert_available(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;
	uint16_t size;

	if (is_dp_hdcp(hdcp)) {
		status = MOD_HDCP_STATUS_SUCCESS;
	} else {
		status = mod_hdcp_read_rxstatus(hdcp);
		if (status == MOD_HDCP_STATUS_SUCCESS) {
			size = HDCP_2_2_HDMI_RXSTATUS_MSG_SZ_HI(hdcp->auth.msg.hdcp2.rxstatus[1]) << 8 |
			       hdcp->auth.msg.hdcp2.rxstatus[0];
			status = (size == sizeof(hdcp->auth.msg.hdcp2.ake_cert)) ?
					MOD_HDCP_STATUS_SUCCESS :
					MOD_HDCP_STATUS_HDCP2_AKE_CERT_PENDING;
		}
	}
	return status;
}

static enum mod_hdcp_status check_h_prime_available(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;
	uint8_t size;

	status = mod_hdcp_read_rxstatus(hdcp);
	if (status != MOD_HDCP_STATUS_SUCCESS)
		goto out;

	if (is_dp_hdcp(hdcp)) {
		status = HDCP_2_2_DP_RXSTATUS_H_PRIME(hdcp->auth.msg.hdcp2.rxstatus_dp) ?
				MOD_HDCP_STATUS_SUCCESS :
				MOD_HDCP_STATUS_HDCP2_H_PRIME_PENDING;
	} else {
		size = HDCP_2_2_HDMI_RXSTATUS_MSG_SZ_HI(hdcp->auth.msg.hdcp2.rxstatus[1]) << 8 |
		       hdcp->auth.msg.hdcp2.rxstatus[0];
		status = (size == sizeof(hdcp->auth.msg.hdcp2.ake_h_prime)) ?
				MOD_HDCP_STATUS_SUCCESS :
				MOD_HDCP_STATUS_HDCP2_H_PRIME_PENDING;
	}
out:
	return status;
}

static enum mod_hdcp_status check_pairing_info_available(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;
	uint8_t size;

	status = mod_hdcp_read_rxstatus(hdcp);
	if (status != MOD_HDCP_STATUS_SUCCESS)
		goto out;

	if (is_dp_hdcp(hdcp)) {
		status = HDCP_2_2_DP_RXSTATUS_PAIRING(hdcp->auth.msg.hdcp2.rxstatus_dp) ?
				MOD_HDCP_STATUS_SUCCESS :
				MOD_HDCP_STATUS_HDCP2_PAIRING_INFO_PENDING;
	} else {
		size = HDCP_2_2_HDMI_RXSTATUS_MSG_SZ_HI(hdcp->auth.msg.hdcp2.rxstatus[1]) << 8 |
		       hdcp->auth.msg.hdcp2.rxstatus[0];
		status = (size == sizeof(hdcp->auth.msg.hdcp2.ake_pairing_info)) ?
				MOD_HDCP_STATUS_SUCCESS :
				MOD_HDCP_STATUS_HDCP2_PAIRING_INFO_PENDING;
	}
out:
	return status;
}

static enum mod_hdcp_status poll_l_prime_available(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;
	uint8_t size;
	uint16_t max_wait = 20; // units of ms
	uint16_t num_polls = 5;
	uint16_t wait_time = max_wait / num_polls;

	if (is_dp_hdcp(hdcp))
		status = MOD_HDCP_STATUS_INVALID_OPERATION;
	else
		for (; num_polls; num_polls--) {
			msleep(wait_time);

			status = mod_hdcp_read_rxstatus(hdcp);
			if (status != MOD_HDCP_STATUS_SUCCESS)
				break;

			size = HDCP_2_2_HDMI_RXSTATUS_MSG_SZ_HI(hdcp->auth.msg.hdcp2.rxstatus[1]) << 8 |
			       hdcp->auth.msg.hdcp2.rxstatus[0];
			status = (size == sizeof(hdcp->auth.msg.hdcp2.lc_l_prime)) ?
					MOD_HDCP_STATUS_SUCCESS :
					MOD_HDCP_STATUS_HDCP2_L_PRIME_PENDING;
			if (status == MOD_HDCP_STATUS_SUCCESS)
				break;
		}
	return status;
}

static enum mod_hdcp_status check_stream_ready_available(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status;
	uint8_t size;

	if (is_dp_hdcp(hdcp)) {
		status = MOD_HDCP_STATUS_INVALID_OPERATION;
	} else {
		status = mod_hdcp_read_rxstatus(hdcp);
		if (status != MOD_HDCP_STATUS_SUCCESS)
			goto out;
		size = HDCP_2_2_HDMI_RXSTATUS_MSG_SZ_HI(hdcp->auth.msg.hdcp2.rxstatus[1]) << 8 |
		       hdcp->auth.msg.hdcp2.rxstatus[0];
		status = (size == sizeof(hdcp->auth.msg.hdcp2.repeater_auth_stream_ready)) ?
				MOD_HDCP_STATUS_SUCCESS :
				MOD_HDCP_STATUS_HDCP2_STREAM_READY_PENDING;
	}
out:
	return status;
}

static inline uint8_t get_device_count(struct mod_hdcp *hdcp)
{
	return HDCP_2_2_DEV_COUNT_LO(hdcp->auth.msg.hdcp2.rx_id_list[2]) +
			(HDCP_2_2_DEV_COUNT_HI(hdcp->auth.msg.hdcp2.rx_id_list[1]) << 4);
}

static enum mod_hdcp_status check_device_count(struct mod_hdcp *hdcp)
{
	/* Avoid device count == 0 to do authentication */
	if (0 == get_device_count(hdcp)) {
		return MOD_HDCP_STATUS_HDCP1_DEVICE_COUNT_MISMATCH_FAILURE;
	}

	/* Some MST display may choose to report the internal panel as an HDCP RX.   */
	/* To update this condition with 1(because the immediate repeater's internal */
	/* panel is possibly not included in DEVICE_COUNT) + get_device_count(hdcp). */
	/* Device count must be greater than or equal to tracked hdcp displays.      */
	return ((1 + get_device_count(hdcp)) < get_active_display_count(hdcp)) ?
			MOD_HDCP_STATUS_HDCP2_DEVICE_COUNT_MISMATCH_FAILURE :
			MOD_HDCP_STATUS_SUCCESS;
}

static uint8_t process_rxstatus(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input,
		enum mod_hdcp_status *status)
{
	if (!mod_hdcp_execute_and_set(mod_hdcp_read_rxstatus,
			&input->rxstatus_read, status,
			hdcp, "rxstatus_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(check_reauthentication_request,
			&input->reauth_request_check, status,
			hdcp, "reauth_request_check"))
		goto out;
	if (is_dp_hdcp(hdcp)) {
		if (!mod_hdcp_execute_and_set(check_link_integrity_failure_dp,
				&input->link_integrity_check_dp, status,
				hdcp, "link_integrity_check_dp"))
			goto out;
	}
	if (hdcp->connection.is_repeater)
		if (check_receiver_id_list_ready(hdcp) ==
				MOD_HDCP_STATUS_SUCCESS) {
			HDCP_INPUT_PASS_TRACE(hdcp, "rx_id_list_ready");
			event_ctx->rx_id_list_ready = 1;
			if (is_dp_hdcp(hdcp))
				hdcp->auth.msg.hdcp2.rx_id_list_size =
						sizeof(hdcp->auth.msg.hdcp2.rx_id_list);
			else
				hdcp->auth.msg.hdcp2.rx_id_list_size =
					HDCP_2_2_HDMI_RXSTATUS_MSG_SZ_HI(hdcp->auth.msg.hdcp2.rxstatus[1]) << 8 |
					hdcp->auth.msg.hdcp2.rxstatus[0];
		}
out:
	return (*status == MOD_HDCP_STATUS_SUCCESS);
}

static enum mod_hdcp_status known_hdcp2_capable_rx(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(mod_hdcp_read_hdcp2version,
			&input->hdcp2version_read, &status,
			hdcp, "hdcp2version_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(check_hdcp2_capable,
			&input->hdcp2_capable_check, &status,
			hdcp, "hdcp2_capable"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status send_ake_init(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp2_create_session,
			&input->create_session, &status,
			hdcp, "create_session"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp2_prepare_ake_init,
			&input->ake_init_prepare, &status,
			hdcp, "ake_init_prepare"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_write_ake_init,
			&input->ake_init_write, &status,
			hdcp, "ake_init_write"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status validate_ake_cert(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;


	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK &&
			event_ctx->event != MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (is_hdmi_dvi_sl_hdcp(hdcp))
		if (!mod_hdcp_execute_and_set(check_ake_cert_available,
				&input->ake_cert_available, &status,
				hdcp, "ake_cert_available"))
			goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_read_ake_cert,
			&input->ake_cert_read, &status,
			hdcp, "ake_cert_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp2_validate_ake_cert,
			&input->ake_cert_validation, &status,
			hdcp, "ake_cert_validation"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status send_no_stored_km(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(mod_hdcp_write_no_stored_km,
			&input->no_stored_km_write, &status,
			hdcp, "no_stored_km_write"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status read_h_prime(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK &&
			event_ctx->event != MOD_HDCP_EVENT_CPIRQ &&
			event_ctx->event != MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(check_h_prime_available,
			&input->h_prime_available, &status,
			hdcp, "h_prime_available"))
		goto out;

	if (!mod_hdcp_execute_and_set(mod_hdcp_read_h_prime,
			&input->h_prime_read, &status,
			hdcp, "h_prime_read"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status read_pairing_info_and_validate_h_prime(
		struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK &&
			event_ctx->event != MOD_HDCP_EVENT_CPIRQ &&
			event_ctx->event != MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(check_pairing_info_available,
			&input->pairing_available, &status,
			hdcp, "pairing_available"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_read_pairing_info,
			&input->pairing_info_read, &status,
			hdcp, "pairing_info_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp2_validate_h_prime,
			&input->h_prime_validation, &status,
			hdcp, "h_prime_validation"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status send_stored_km(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(mod_hdcp_write_stored_km,
			&input->stored_km_write, &status,
			hdcp, "stored_km_write"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status validate_h_prime(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK &&
			event_ctx->event != MOD_HDCP_EVENT_CPIRQ &&
			event_ctx->event != MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(check_h_prime_available,
			&input->h_prime_available, &status,
			hdcp, "h_prime_available"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_read_h_prime,
			&input->h_prime_read, &status,
			hdcp, "h_prime_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp2_validate_h_prime,
			&input->h_prime_validation, &status,
			hdcp, "h_prime_validation"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status locality_check(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp2_prepare_lc_init,
			&input->lc_init_prepare, &status,
			hdcp, "lc_init_prepare"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_write_lc_init,
			&input->lc_init_write, &status,
			 hdcp, "lc_init_write"))
		goto out;
	if (is_dp_hdcp(hdcp))
		msleep(16);
	else
		if (!mod_hdcp_execute_and_set(poll_l_prime_available,
				&input->l_prime_available_poll, &status,
				hdcp, "l_prime_available_poll"))
			goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_read_l_prime,
			&input->l_prime_read, &status,
			hdcp, "l_prime_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp2_validate_l_prime,
			&input->l_prime_validation, &status,
			hdcp, "l_prime_validation"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status exchange_ks_and_test_for_repeater(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp2_prepare_eks,
			&input->eks_prepare, &status,
			hdcp, "eks_prepare"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_write_eks,
			&input->eks_write, &status,
			hdcp, "eks_write"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status enable_encryption(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK &&
			event_ctx->event != MOD_HDCP_EVENT_CPIRQ) {
		event_ctx->unexpected_event = 1;
		goto out;
	}
	if (event_ctx->event == MOD_HDCP_EVENT_CPIRQ) {
		process_rxstatus(hdcp, event_ctx, input, &status);
		goto out;
	}

	if (is_hdmi_dvi_sl_hdcp(hdcp)) {
		if (!process_rxstatus(hdcp, event_ctx, input, &status))
			goto out;
		if (event_ctx->rx_id_list_ready)
			goto out;
	}
	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp2_enable_encryption,
			&input->enable_encryption, &status,
			hdcp, "enable_encryption"))
		goto out;
	if (is_dp_mst_hdcp(hdcp)) {
		if (!mod_hdcp_execute_and_set(
				mod_hdcp_hdcp2_enable_dp_stream_encryption,
				&input->stream_encryption_dp, &status,
				hdcp, "stream_encryption_dp"))
			goto out;
	}
out:
	return status;
}

static enum mod_hdcp_status authenticated(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK &&
			event_ctx->event != MOD_HDCP_EVENT_CPIRQ) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	process_rxstatus(hdcp, event_ctx, input, &status);

	if (status != MOD_HDCP_STATUS_SUCCESS)
		mod_hdcp_save_current_encryption_states(hdcp);
out:
	return status;
}

static enum mod_hdcp_status wait_for_rx_id_list(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK &&
			event_ctx->event != MOD_HDCP_EVENT_CPIRQ &&
			event_ctx->event != MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!process_rxstatus(hdcp, event_ctx, input, &status))
		goto out;
	if (!event_ctx->rx_id_list_ready) {
		status = MOD_HDCP_STATUS_HDCP2_RX_ID_LIST_NOT_READY;
		goto out;
	}
out:
	return status;
}

static enum mod_hdcp_status verify_rx_id_list_and_send_ack(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK &&
			event_ctx->event != MOD_HDCP_EVENT_CPIRQ) {
		event_ctx->unexpected_event = 1;
		goto out;
	}
	if (event_ctx->event == MOD_HDCP_EVENT_CPIRQ) {
		process_rxstatus(hdcp, event_ctx, input, &status);
		goto out;
	}

	if (!mod_hdcp_execute_and_set(mod_hdcp_read_rx_id_list,
			&input->rx_id_list_read,
			&status, hdcp, "receiver_id_list_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(check_device_count,
			&input->device_count_check,
			&status, hdcp, "device_count_check"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp2_validate_rx_id_list,
			&input->rx_id_list_validation,
			&status, hdcp, "rx_id_list_validation"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_write_repeater_auth_ack,
			&input->repeater_auth_ack_write,
			&status, hdcp, "repeater_auth_ack_write"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status send_stream_management(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK &&
			event_ctx->event != MOD_HDCP_EVENT_CPIRQ) {
		event_ctx->unexpected_event = 1;
		goto out;
	}
	if (event_ctx->event == MOD_HDCP_EVENT_CPIRQ) {
		process_rxstatus(hdcp, event_ctx, input, &status);
		goto out;
	}

	if (is_hdmi_dvi_sl_hdcp(hdcp)) {
		if (!process_rxstatus(hdcp, event_ctx, input, &status))
			goto out;
		if (event_ctx->rx_id_list_ready)
			goto out;
	}
	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp2_prepare_stream_management,
			&input->prepare_stream_manage,
			&status, hdcp, "prepare_stream_manage"))
		goto out;

	if (!mod_hdcp_execute_and_set(mod_hdcp_write_stream_manage,
			&input->stream_manage_write,
			&status, hdcp, "stream_manage_write"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status validate_stream_ready(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK &&
			event_ctx->event != MOD_HDCP_EVENT_CPIRQ &&
			event_ctx->event != MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) {
		event_ctx->unexpected_event = 1;
		goto out;
	}
	if (event_ctx->event == MOD_HDCP_EVENT_CPIRQ) {
		process_rxstatus(hdcp, event_ctx, input, &status);
		goto out;
	}

	if (is_hdmi_dvi_sl_hdcp(hdcp)) {
		if (!process_rxstatus(hdcp, event_ctx, input, &status))
			goto out;
		if (event_ctx->rx_id_list_ready) {
			goto out;
		}
	}
	if (is_hdmi_dvi_sl_hdcp(hdcp))
		if (!mod_hdcp_execute_and_set(check_stream_ready_available,
				&input->stream_ready_available,
				&status, hdcp, "stream_ready_available"))
			goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_read_stream_ready,
			&input->stream_ready_read,
			&status, hdcp, "stream_ready_read"))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_hdcp2_validate_stream_ready,
			&input->stream_ready_validation,
			&status, hdcp, "stream_ready_validation"))
		goto out;

out:
	return status;
}

static enum mod_hdcp_status determine_rx_hdcp_capable_dp(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!mod_hdcp_execute_and_set(mod_hdcp_read_rxcaps,
			&input->rx_caps_read_dp,
			&status, hdcp, "rx_caps_read_dp"))
		goto out;
	if (!mod_hdcp_execute_and_set(check_hdcp2_capable,
			&input->hdcp2_capable_check, &status,
			hdcp, "hdcp2_capable_check"))
		goto out;
out:
	return status;
}

static enum mod_hdcp_status send_content_stream_type_dp(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK &&
			event_ctx->event != MOD_HDCP_EVENT_CPIRQ) {
		event_ctx->unexpected_event = 1;
		goto out;
	}

	if (!process_rxstatus(hdcp, event_ctx, input, &status))
		goto out;
	if (!mod_hdcp_execute_and_set(mod_hdcp_write_content_type,
			&input->content_stream_type_write, &status,
			hdcp, "content_stream_type_write"))
		goto out;
out:
	return status;
}

enum mod_hdcp_status mod_hdcp_hdcp2_execution(struct mod_hdcp *hdcp,
	struct mod_hdcp_event_context *event_ctx,
	struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	switch (current_state(hdcp)) {
	case H2_A0_KNOWN_HDCP2_CAPABLE_RX:
		status = known_hdcp2_capable_rx(hdcp, event_ctx, input);
		break;
	case H2_A1_SEND_AKE_INIT:
		status = send_ake_init(hdcp, event_ctx, input);
		break;
	case H2_A1_VALIDATE_AKE_CERT:
		status = validate_ake_cert(hdcp, event_ctx, input);
		break;
	case H2_A1_SEND_NO_STORED_KM:
		status = send_no_stored_km(hdcp, event_ctx, input);
		break;
	case H2_A1_READ_H_PRIME:
		status = read_h_prime(hdcp, event_ctx, input);
		break;
	case H2_A1_READ_PAIRING_INFO_AND_VALIDATE_H_PRIME:
		status = read_pairing_info_and_validate_h_prime(hdcp,
				event_ctx, input);
		break;
	case H2_A1_SEND_STORED_KM:
		status = send_stored_km(hdcp, event_ctx, input);
		break;
	case H2_A1_VALIDATE_H_PRIME:
		status = validate_h_prime(hdcp, event_ctx, input);
		break;
	case H2_A2_LOCALITY_CHECK:
		status = locality_check(hdcp, event_ctx, input);
		break;
	case H2_A3_EXCHANGE_KS_AND_TEST_FOR_REPEATER:
		status = exchange_ks_and_test_for_repeater(hdcp, event_ctx, input);
		break;
	case H2_ENABLE_ENCRYPTION:
		status = enable_encryption(hdcp, event_ctx, input);
		break;
	case H2_A5_AUTHENTICATED:
		status = authenticated(hdcp, event_ctx, input);
		break;
	case H2_A6_WAIT_FOR_RX_ID_LIST:
		status = wait_for_rx_id_list(hdcp, event_ctx, input);
		break;
	case H2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK:
		status = verify_rx_id_list_and_send_ack(hdcp, event_ctx, input);
		break;
	case H2_A9_SEND_STREAM_MANAGEMENT:
		status = send_stream_management(hdcp, event_ctx, input);
		break;
	case H2_A9_VALIDATE_STREAM_READY:
		status = validate_stream_ready(hdcp, event_ctx, input);
		break;
	default:
		status = MOD_HDCP_STATUS_INVALID_STATE;
		break;
	}

	return status;
}

enum mod_hdcp_status mod_hdcp_hdcp2_dp_execution(struct mod_hdcp *hdcp,
	struct mod_hdcp_event_context *event_ctx,
	struct mod_hdcp_transition_input_hdcp2 *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	switch (current_state(hdcp)) {
	case D2_A0_DETERMINE_RX_HDCP_CAPABLE:
		status = determine_rx_hdcp_capable_dp(hdcp, event_ctx, input);
		break;
	case D2_A1_SEND_AKE_INIT:
		status = send_ake_init(hdcp, event_ctx, input);
		break;
	case D2_A1_VALIDATE_AKE_CERT:
		status = validate_ake_cert(hdcp, event_ctx, input);
		break;
	case D2_A1_SEND_NO_STORED_KM:
		status = send_no_stored_km(hdcp, event_ctx, input);
		break;
	case D2_A1_READ_H_PRIME:
		status = read_h_prime(hdcp, event_ctx, input);
		break;
	case D2_A1_READ_PAIRING_INFO_AND_VALIDATE_H_PRIME:
		status = read_pairing_info_and_validate_h_prime(hdcp,
				event_ctx, input);
		break;
	case D2_A1_SEND_STORED_KM:
		status = send_stored_km(hdcp, event_ctx, input);
		break;
	case D2_A1_VALIDATE_H_PRIME:
		status = validate_h_prime(hdcp, event_ctx, input);
		break;
	case D2_A2_LOCALITY_CHECK:
		status = locality_check(hdcp, event_ctx, input);
		break;
	case D2_A34_EXCHANGE_KS_AND_TEST_FOR_REPEATER:
		status = exchange_ks_and_test_for_repeater(hdcp,
				event_ctx, input);
		break;
	case D2_SEND_CONTENT_STREAM_TYPE:
		status = send_content_stream_type_dp(hdcp, event_ctx, input);
		break;
	case D2_ENABLE_ENCRYPTION:
		status = enable_encryption(hdcp, event_ctx, input);
		break;
	case D2_A5_AUTHENTICATED:
		status = authenticated(hdcp, event_ctx, input);
		break;
	case D2_A6_WAIT_FOR_RX_ID_LIST:
		status = wait_for_rx_id_list(hdcp, event_ctx, input);
		break;
	case D2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK:
		status = verify_rx_id_list_and_send_ack(hdcp, event_ctx, input);
		break;
	case D2_A9_SEND_STREAM_MANAGEMENT:
		status = send_stream_management(hdcp, event_ctx, input);
		break;
	case D2_A9_VALIDATE_STREAM_READY:
		status = validate_stream_ready(hdcp, event_ctx, input);
		break;
	default:
		status = MOD_HDCP_STATUS_INVALID_STATE;
		break;
	}

	return status;
}
