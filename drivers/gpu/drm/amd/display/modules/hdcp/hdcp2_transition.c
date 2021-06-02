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

#include "hdcp.h"

enum mod_hdcp_status mod_hdcp_hdcp2_transition(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input,
		struct mod_hdcp_output *output)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;
	struct mod_hdcp_connection *conn = &hdcp->connection;
	struct mod_hdcp_link_adjustment *adjust = &hdcp->connection.link.adjust;

	switch (current_state(hdcp)) {
	case H2_A0_KNOWN_HDCP2_CAPABLE_RX:
		if (input->hdcp2version_read != PASS ||
				input->hdcp2_capable_check != PASS) {
			adjust->hdcp2.disable = 1;
			callback_in_ms(0, output);
			set_state_id(hdcp, output, HDCP_INITIALIZED);
		} else {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, H2_A1_SEND_AKE_INIT);
		}
		break;
	case H2_A1_SEND_AKE_INIT:
		if (input->create_session != PASS ||
				input->ake_init_prepare != PASS) {
			/* out of sync with psp state */
			adjust->hdcp2.disable = 1;
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (input->ake_init_write != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		set_watchdog_in_ms(hdcp, 100, output);
		callback_in_ms(0, output);
		set_state_id(hdcp, output, H2_A1_VALIDATE_AKE_CERT);
		break;
	case H2_A1_VALIDATE_AKE_CERT:
		if (input->ake_cert_available != PASS) {
			if (event_ctx->event ==
					MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) {
				/* 1A-08: consider ake timeout a failure */
				/* some hdmi receivers are not ready for HDCP
				 * immediately after video becomes active,
				 * delay 1s before retry on first HDCP message
				 * timeout.
				 */
				fail_and_restart_in_ms(1000, &status, output);
			} else {
				/* continue ake cert polling*/
				callback_in_ms(10, output);
				increment_stay_counter(hdcp);
			}
			break;
		} else if (input->ake_cert_read != PASS ||
				input->ake_cert_validation != PASS) {
			/*
			 * 1A-09: consider invalid ake cert a failure
			 * 1A-10: consider receiver id listed in SRM a failure
			 */
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		if (conn->is_km_stored &&
				!adjust->hdcp2.force_no_stored_km) {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, H2_A1_SEND_STORED_KM);
		} else {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, H2_A1_SEND_NO_STORED_KM);
		}
		break;
	case H2_A1_SEND_NO_STORED_KM:
		if (input->no_stored_km_write != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		if (adjust->hdcp2.increase_h_prime_timeout)
			set_watchdog_in_ms(hdcp, 2000, output);
		else
			set_watchdog_in_ms(hdcp, 1000, output);
		callback_in_ms(0, output);
		set_state_id(hdcp, output, H2_A1_READ_H_PRIME);
		break;
	case H2_A1_READ_H_PRIME:
		if (input->h_prime_available != PASS) {
			if (event_ctx->event ==
					MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) {
				/* 1A-11-3: consider h' timeout a failure */
				fail_and_restart_in_ms(1000, &status, output);
			} else {
				/* continue h' polling */
				callback_in_ms(100, output);
				increment_stay_counter(hdcp);
			}
			break;
		} else if (input->h_prime_read != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		set_watchdog_in_ms(hdcp, 200, output);
		callback_in_ms(0, output);
		set_state_id(hdcp, output, H2_A1_READ_PAIRING_INFO_AND_VALIDATE_H_PRIME);
		break;
	case H2_A1_READ_PAIRING_INFO_AND_VALIDATE_H_PRIME:
		if (input->pairing_available != PASS) {
			if (event_ctx->event ==
					MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) {
				/* 1A-12: consider pairing info timeout
				 * a failure
				 */
				fail_and_restart_in_ms(0, &status, output);
			} else {
				/* continue pairing info polling */
				callback_in_ms(20, output);
				increment_stay_counter(hdcp);
			}
			break;
		} else if (input->pairing_info_read != PASS ||
				input->h_prime_validation != PASS) {
			/* 1A-11-1: consider invalid h' a failure */
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		callback_in_ms(0, output);
		set_state_id(hdcp, output, H2_A2_LOCALITY_CHECK);
		break;
	case H2_A1_SEND_STORED_KM:
		if (input->stored_km_write != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		set_watchdog_in_ms(hdcp, 200, output);
		callback_in_ms(0, output);
		set_state_id(hdcp, output, H2_A1_VALIDATE_H_PRIME);
		break;
	case H2_A1_VALIDATE_H_PRIME:
		if (input->h_prime_available != PASS) {
			if (event_ctx->event ==
					MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) {
				/* 1A-11-2: consider h' timeout a failure */
				fail_and_restart_in_ms(1000, &status, output);
			} else {
				/* continue h' polling */
				callback_in_ms(20, output);
				increment_stay_counter(hdcp);
			}
			break;
		} else if (input->h_prime_read != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (input->h_prime_validation != PASS) {
			/* 1A-11-1: consider invalid h' a failure */
			adjust->hdcp2.force_no_stored_km = 1;
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		callback_in_ms(0, output);
		set_state_id(hdcp, output, H2_A2_LOCALITY_CHECK);
		break;
	case H2_A2_LOCALITY_CHECK:
		if (hdcp->state.stay_count > 10 ||
				input->lc_init_prepare != PASS ||
				input->lc_init_write != PASS ||
				input->l_prime_available_poll != PASS ||
				input->l_prime_read != PASS) {
			/*
			 * 1A-05: consider disconnection after LC init a failure
			 * 1A-13-1: consider invalid l' a failure
			 * 1A-13-2: consider l' timeout a failure
			 */
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (input->l_prime_validation != PASS) {
			callback_in_ms(0, output);
			increment_stay_counter(hdcp);
			break;
		}
		callback_in_ms(0, output);
		set_state_id(hdcp, output, H2_A3_EXCHANGE_KS_AND_TEST_FOR_REPEATER);
		break;
	case H2_A3_EXCHANGE_KS_AND_TEST_FOR_REPEATER:
		if (input->eks_prepare != PASS ||
				input->eks_write != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		if (conn->is_repeater) {
			set_watchdog_in_ms(hdcp, 3000, output);
			callback_in_ms(0, output);
			set_state_id(hdcp, output, H2_A6_WAIT_FOR_RX_ID_LIST);
		} else {
			/* some CTS equipment requires a delay GREATER than
			 * 200 ms, so delay 210 ms instead of 200 ms
			 */
			callback_in_ms(210, output);
			set_state_id(hdcp, output, H2_ENABLE_ENCRYPTION);
		}
		break;
	case H2_ENABLE_ENCRYPTION:
		if (input->rxstatus_read != PASS ||
				input->reauth_request_check != PASS) {
			/*
			 * 1A-07: restart hdcp on REAUTH_REQ
			 * 1B-08: restart hdcp on REAUTH_REQ
			 */
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (event_ctx->rx_id_list_ready && conn->is_repeater) {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, H2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK);
			break;
		} else if (input->enable_encryption != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		callback_in_ms(0, output);
		set_state_id(hdcp, output, H2_A5_AUTHENTICATED);
		HDCP_FULL_DDC_TRACE(hdcp);
		break;
	case H2_A5_AUTHENTICATED:
		if (input->rxstatus_read == FAIL ||
				input->reauth_request_check == FAIL) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (event_ctx->rx_id_list_ready && conn->is_repeater) {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, H2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK);
			break;
		}
		callback_in_ms(500, output);
		increment_stay_counter(hdcp);
		break;
	case H2_A6_WAIT_FOR_RX_ID_LIST:
		if (input->rxstatus_read != PASS ||
				input->reauth_request_check != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (!event_ctx->rx_id_list_ready) {
			if (event_ctx->event == MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) {
				/* 1B-02: consider rx id list timeout a failure */
				/* some CTS equipment's actual timeout
				 * measurement is slightly greater than 3000 ms.
				 * Delay 100 ms to ensure it is fully timeout
				 * before re-authentication.
				 */
				fail_and_restart_in_ms(100, &status, output);
			} else {
				callback_in_ms(300, output);
				increment_stay_counter(hdcp);
			}
			break;
		}
		callback_in_ms(0, output);
		set_state_id(hdcp, output, H2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK);
		break;
	case H2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK:
		if (input->rxstatus_read != PASS ||
				input->reauth_request_check != PASS ||
				input->rx_id_list_read != PASS ||
				input->device_count_check != PASS ||
				input->rx_id_list_validation != PASS ||
				input->repeater_auth_ack_write != PASS) {
			/* 1B-03: consider invalid v' a failure
			 * 1B-04: consider MAX_DEVS_EXCEEDED a failure
			 * 1B-05: consider MAX_CASCADE_EXCEEDED a failure
			 * 1B-06: consider invalid seq_num_V a failure
			 * 1B-09: consider seq_num_V rollover a failure
			 */
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		callback_in_ms(0, output);
		set_state_id(hdcp, output, H2_A9_SEND_STREAM_MANAGEMENT);
		break;
	case H2_A9_SEND_STREAM_MANAGEMENT:
		if (input->rxstatus_read != PASS ||
				input->reauth_request_check != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (event_ctx->rx_id_list_ready && conn->is_repeater) {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, H2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK);
			break;
		} else if (input->prepare_stream_manage != PASS ||
				input->stream_manage_write != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		set_watchdog_in_ms(hdcp, 100, output);
		callback_in_ms(0, output);
		set_state_id(hdcp, output, H2_A9_VALIDATE_STREAM_READY);
		break;
	case H2_A9_VALIDATE_STREAM_READY:
		if (input->rxstatus_read != PASS ||
				input->reauth_request_check != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (event_ctx->rx_id_list_ready && conn->is_repeater) {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, H2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK);
			break;
		} else if (input->stream_ready_available != PASS) {
			if (event_ctx->event == MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) {
				/* 1B-10-2: restart content stream management on
				 * stream ready timeout
				 */
				hdcp->auth.count.stream_management_retry_count++;
				callback_in_ms(0, output);
				set_state_id(hdcp, output, H2_A9_SEND_STREAM_MANAGEMENT);
			} else {
				callback_in_ms(10, output);
				increment_stay_counter(hdcp);
			}
			break;
		} else if (input->stream_ready_read != PASS ||
				input->stream_ready_validation != PASS) {
			/*
			 * 1B-10-1: restart content stream management
			 * on invalid M'
			 */
			if (hdcp->auth.count.stream_management_retry_count > 10) {
				fail_and_restart_in_ms(0, &status, output);
			} else {
				hdcp->auth.count.stream_management_retry_count++;
				callback_in_ms(0, output);
				set_state_id(hdcp, output, H2_A9_SEND_STREAM_MANAGEMENT);
			}
			break;
		}
		callback_in_ms(200, output);
		set_state_id(hdcp, output, H2_ENABLE_ENCRYPTION);
		break;
	default:
		status = MOD_HDCP_STATUS_INVALID_STATE;
		fail_and_restart_in_ms(0, &status, output);
		break;
	}

	return status;
}

enum mod_hdcp_status mod_hdcp_hdcp2_dp_transition(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		struct mod_hdcp_transition_input_hdcp2 *input,
		struct mod_hdcp_output *output)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;
	struct mod_hdcp_connection *conn = &hdcp->connection;
	struct mod_hdcp_link_adjustment *adjust = &hdcp->connection.link.adjust;

	switch (current_state(hdcp)) {
	case D2_A0_DETERMINE_RX_HDCP_CAPABLE:
		if (input->rx_caps_read_dp != PASS ||
				input->hdcp2_capable_check != PASS) {
			adjust->hdcp2.disable = 1;
			callback_in_ms(0, output);
			set_state_id(hdcp, output, HDCP_INITIALIZED);
		} else {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, D2_A1_SEND_AKE_INIT);
		}
		break;
	case D2_A1_SEND_AKE_INIT:
		if (input->create_session != PASS ||
				input->ake_init_prepare != PASS) {
			/* out of sync with psp state */
			adjust->hdcp2.disable = 1;
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (input->ake_init_write != PASS) {
			/* possibly display not ready */
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		callback_in_ms(100, output);
		set_state_id(hdcp, output, D2_A1_VALIDATE_AKE_CERT);
		break;
	case D2_A1_VALIDATE_AKE_CERT:
		if (input->ake_cert_read != PASS ||
				input->ake_cert_validation != PASS) {
			/*
			 * 1A-08: consider invalid ake cert a failure
			 * 1A-09: consider receiver id listed in SRM a failure
			 */
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		if (conn->is_km_stored &&
				!adjust->hdcp2.force_no_stored_km) {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, D2_A1_SEND_STORED_KM);
		} else {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, D2_A1_SEND_NO_STORED_KM);
		}
		break;
	case D2_A1_SEND_NO_STORED_KM:
		if (input->no_stored_km_write != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		if (adjust->hdcp2.increase_h_prime_timeout)
			set_watchdog_in_ms(hdcp, 2000, output);
		else
			set_watchdog_in_ms(hdcp, 1000, output);
		set_state_id(hdcp, output, D2_A1_READ_H_PRIME);
		break;
	case D2_A1_READ_H_PRIME:
		if (input->h_prime_available != PASS) {
			if (event_ctx->event ==
					MOD_HDCP_EVENT_WATCHDOG_TIMEOUT)
				/* 1A-10-3: consider h' timeout a failure */
				fail_and_restart_in_ms(1000, &status, output);
			else
				increment_stay_counter(hdcp);
			break;
		} else if (input->h_prime_read != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		set_watchdog_in_ms(hdcp, 200, output);
		set_state_id(hdcp, output, D2_A1_READ_PAIRING_INFO_AND_VALIDATE_H_PRIME);
		break;
	case D2_A1_READ_PAIRING_INFO_AND_VALIDATE_H_PRIME:
		if (input->pairing_available != PASS) {
			if (event_ctx->event ==
					MOD_HDCP_EVENT_WATCHDOG_TIMEOUT)
				/*
				 * 1A-11: consider pairing info timeout
				 * a failure
				 */
				fail_and_restart_in_ms(0, &status, output);
			else
				increment_stay_counter(hdcp);
			break;
		} else if (input->pairing_info_read != PASS ||
				input->h_prime_validation != PASS) {
			/* 1A-10-1: consider invalid h' a failure */
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		callback_in_ms(0, output);
		set_state_id(hdcp, output, D2_A2_LOCALITY_CHECK);
		break;
	case D2_A1_SEND_STORED_KM:
		if (input->stored_km_write != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		set_watchdog_in_ms(hdcp, 200, output);
		set_state_id(hdcp, output, D2_A1_VALIDATE_H_PRIME);
		break;
	case D2_A1_VALIDATE_H_PRIME:
		if (input->h_prime_available != PASS) {
			if (event_ctx->event ==
					MOD_HDCP_EVENT_WATCHDOG_TIMEOUT)
				/* 1A-10-2: consider h' timeout a failure */
				fail_and_restart_in_ms(1000, &status, output);
			else
				increment_stay_counter(hdcp);
			break;
		} else if (input->h_prime_read != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (input->h_prime_validation != PASS) {
			/* 1A-10-1: consider invalid h' a failure */
			adjust->hdcp2.force_no_stored_km = 1;
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		callback_in_ms(0, output);
		set_state_id(hdcp, output, D2_A2_LOCALITY_CHECK);
		break;
	case D2_A2_LOCALITY_CHECK:
		if (hdcp->state.stay_count > 10 ||
				input->lc_init_prepare != PASS ||
				input->lc_init_write != PASS ||
				input->l_prime_read != PASS) {
			/* 1A-12: consider invalid l' a failure */
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (input->l_prime_validation != PASS) {
			callback_in_ms(0, output);
			increment_stay_counter(hdcp);
			break;
		}
		callback_in_ms(0, output);
		set_state_id(hdcp, output, D2_A34_EXCHANGE_KS_AND_TEST_FOR_REPEATER);
		break;
	case D2_A34_EXCHANGE_KS_AND_TEST_FOR_REPEATER:
		if (input->eks_prepare != PASS ||
				input->eks_write != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		if (conn->is_repeater) {
			set_watchdog_in_ms(hdcp, 3000, output);
			set_state_id(hdcp, output, D2_A6_WAIT_FOR_RX_ID_LIST);
		} else {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, D2_SEND_CONTENT_STREAM_TYPE);
		}
		break;
	case D2_SEND_CONTENT_STREAM_TYPE:
		if (input->rxstatus_read != PASS ||
				input->reauth_request_check != PASS ||
				input->link_integrity_check_dp != PASS ||
				input->content_stream_type_write != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		callback_in_ms(210, output);
		set_state_id(hdcp, output, D2_ENABLE_ENCRYPTION);
		break;
	case D2_ENABLE_ENCRYPTION:
		if (input->rxstatus_read != PASS ||
				input->reauth_request_check != PASS ||
				input->link_integrity_check_dp != PASS) {
			/*
			 * 1A-07: restart hdcp on REAUTH_REQ
			 * 1B-08: restart hdcp on REAUTH_REQ
			 */
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (event_ctx->rx_id_list_ready && conn->is_repeater) {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, D2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK);
			break;
		} else if (input->enable_encryption != PASS ||
				(is_dp_mst_hdcp(hdcp) && input->stream_encryption_dp != PASS)) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		set_state_id(hdcp, output, D2_A5_AUTHENTICATED);
		HDCP_FULL_DDC_TRACE(hdcp);
		break;
	case D2_A5_AUTHENTICATED:
		if (input->rxstatus_read == FAIL ||
				input->reauth_request_check == FAIL) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (input->link_integrity_check_dp == FAIL) {
			if (hdcp->connection.hdcp2_retry_count >= 1)
				adjust->hdcp2.force_type = MOD_HDCP_FORCE_TYPE_0;
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (event_ctx->rx_id_list_ready && conn->is_repeater) {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, D2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK);
			break;
		}
		increment_stay_counter(hdcp);
		break;
	case D2_A6_WAIT_FOR_RX_ID_LIST:
		if (input->rxstatus_read != PASS ||
				input->reauth_request_check != PASS ||
				input->link_integrity_check_dp != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (!event_ctx->rx_id_list_ready) {
			if (event_ctx->event == MOD_HDCP_EVENT_WATCHDOG_TIMEOUT)
				/* 1B-02: consider rx id list timeout a failure */
				fail_and_restart_in_ms(0, &status, output);
			else
				increment_stay_counter(hdcp);
			break;
		}
		callback_in_ms(0, output);
		set_state_id(hdcp, output, D2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK);
		break;
	case D2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK:
		if (input->rxstatus_read != PASS ||
				input->reauth_request_check != PASS ||
				input->link_integrity_check_dp != PASS ||
				input->rx_id_list_read != PASS ||
				input->device_count_check != PASS ||
				input->rx_id_list_validation != PASS ||
				input->repeater_auth_ack_write != PASS) {
			/*
			 * 1B-03: consider invalid v' a failure
			 * 1B-04: consider MAX_DEVS_EXCEEDED a failure
			 * 1B-05: consider MAX_CASCADE_EXCEEDED a failure
			 * 1B-06: consider invalid seq_num_V a failure
			 * 1B-09: consider seq_num_V rollover a failure
			 */
			fail_and_restart_in_ms(0, &status, output);
			break;
		}
		callback_in_ms(0, output);
		set_state_id(hdcp, output, D2_A9_SEND_STREAM_MANAGEMENT);
		break;
	case D2_A9_SEND_STREAM_MANAGEMENT:
		if (input->rxstatus_read != PASS ||
				input->reauth_request_check != PASS ||
				input->link_integrity_check_dp != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (event_ctx->rx_id_list_ready) {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, D2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK);
			break;
		} else if (input->prepare_stream_manage != PASS ||
				input->stream_manage_write != PASS) {
			if (event_ctx->event == MOD_HDCP_EVENT_CALLBACK)
				fail_and_restart_in_ms(0, &status, output);
			else
				increment_stay_counter(hdcp);
			break;
		}
		callback_in_ms(100, output);
		set_state_id(hdcp, output, D2_A9_VALIDATE_STREAM_READY);
		break;
	case D2_A9_VALIDATE_STREAM_READY:
		if (input->rxstatus_read != PASS ||
				input->reauth_request_check != PASS ||
				input->link_integrity_check_dp != PASS) {
			fail_and_restart_in_ms(0, &status, output);
			break;
		} else if (event_ctx->rx_id_list_ready) {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, D2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK);
			break;
		} else if (input->stream_ready_read != PASS ||
				input->stream_ready_validation != PASS) {
			/*
			 * 1B-10-1: restart content stream management
			 * on invalid M'
			 * 1B-10-2: consider stream ready timeout a failure
			 */
			if (hdcp->auth.count.stream_management_retry_count > 10) {
				fail_and_restart_in_ms(0, &status, output);
			} else if (event_ctx->event == MOD_HDCP_EVENT_CALLBACK) {
				hdcp->auth.count.stream_management_retry_count++;
				callback_in_ms(0, output);
				set_state_id(hdcp, output, D2_A9_SEND_STREAM_MANAGEMENT);
			} else {
				increment_stay_counter(hdcp);
			}
			break;
		}
		callback_in_ms(200, output);
		set_state_id(hdcp, output, D2_ENABLE_ENCRYPTION);
		break;
	default:
		status = MOD_HDCP_STATUS_INVALID_STATE;
		fail_and_restart_in_ms(0, &status, output);
		break;
	}
	return status;
}
