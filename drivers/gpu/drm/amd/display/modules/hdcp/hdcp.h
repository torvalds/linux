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

#ifndef HDCP_H_
#define HDCP_H_

#include "mod_hdcp.h"
#include "hdcp_log.h"

#include <drm/drm_hdcp.h>
#include <drm/drm_dp_helper.h>

enum mod_hdcp_trans_input_result {
	UNKNOWN = 0,
	PASS,
	FAIL
};

struct mod_hdcp_transition_input_hdcp1 {
	uint8_t bksv_read;
	uint8_t bksv_validation;
	uint8_t create_session;
	uint8_t an_write;
	uint8_t aksv_write;
	uint8_t ainfo_write;
	uint8_t bcaps_read;
	uint8_t r0p_read;
	uint8_t rx_validation;
	uint8_t encryption;
	uint8_t link_maintenance;
	uint8_t ready_check;
	uint8_t bstatus_read;
	uint8_t max_cascade_check;
	uint8_t max_devs_check;
	uint8_t device_count_check;
	uint8_t ksvlist_read;
	uint8_t vp_read;
	uint8_t ksvlist_vp_validation;

	uint8_t hdcp_capable_dp;
	uint8_t binfo_read_dp;
	uint8_t r0p_available_dp;
	uint8_t link_integrity_check;
	uint8_t reauth_request_check;
	uint8_t stream_encryption_dp;
};

struct mod_hdcp_transition_input_hdcp2 {
	uint8_t hdcp2version_read;
	uint8_t hdcp2_capable_check;
	uint8_t create_session;
	uint8_t ake_init_prepare;
	uint8_t ake_init_write;
	uint8_t rxstatus_read;
	uint8_t ake_cert_available;
	uint8_t ake_cert_read;
	uint8_t ake_cert_validation;
	uint8_t stored_km_write;
	uint8_t no_stored_km_write;
	uint8_t h_prime_available;
	uint8_t h_prime_read;
	uint8_t pairing_available;
	uint8_t pairing_info_read;
	uint8_t h_prime_validation;
	uint8_t lc_init_prepare;
	uint8_t lc_init_write;
	uint8_t l_prime_available_poll;
	uint8_t l_prime_read;
	uint8_t l_prime_validation;
	uint8_t eks_prepare;
	uint8_t eks_write;
	uint8_t enable_encryption;
	uint8_t reauth_request_check;
	uint8_t rx_id_list_read;
	uint8_t device_count_check;
	uint8_t rx_id_list_validation;
	uint8_t repeater_auth_ack_write;
	uint8_t prepare_stream_manage;
	uint8_t stream_manage_write;
	uint8_t stream_ready_available;
	uint8_t stream_ready_read;
	uint8_t stream_ready_validation;

	uint8_t rx_caps_read_dp;
	uint8_t content_stream_type_write;
	uint8_t link_integrity_check_dp;
	uint8_t stream_encryption_dp;
};

union mod_hdcp_transition_input {
	struct mod_hdcp_transition_input_hdcp1 hdcp1;
	struct mod_hdcp_transition_input_hdcp2 hdcp2;
};

struct mod_hdcp_message_hdcp1 {
	uint8_t		an[8];
	uint8_t		aksv[5];
	uint8_t		ainfo;
	uint8_t		bksv[5];
	uint16_t	r0p;
	uint8_t		bcaps;
	uint16_t	bstatus;
	uint8_t		ksvlist[635];
	uint16_t	ksvlist_size;
	uint8_t		vp[20];

	uint16_t	binfo_dp;
};

struct mod_hdcp_message_hdcp2 {
	uint8_t		hdcp2version_hdmi;
	uint8_t		rxcaps_dp[3];
	uint8_t		rxstatus[2];

	uint8_t		ake_init[12];
	uint8_t		ake_cert[534];
	uint8_t		ake_no_stored_km[129];
	uint8_t		ake_stored_km[33];
	uint8_t		ake_h_prime[33];
	uint8_t		ake_pairing_info[17];
	uint8_t		lc_init[9];
	uint8_t		lc_l_prime[33];
	uint8_t		ske_eks[25];
	uint8_t		rx_id_list[177]; // 22 + 5 * 31
	uint16_t	rx_id_list_size;
	uint8_t		repeater_auth_ack[17];
	uint8_t		repeater_auth_stream_manage[68]; // 6 + 2 * 31
	uint16_t	stream_manage_size;
	uint8_t		repeater_auth_stream_ready[33];
	uint8_t		rxstatus_dp;
	uint8_t		content_stream_type_dp[2];
};

union mod_hdcp_message {
	struct mod_hdcp_message_hdcp1 hdcp1;
	struct mod_hdcp_message_hdcp2 hdcp2;
};

struct mod_hdcp_auth_counters {
	uint8_t stream_management_retry_count;
};

/* contains values per connection */
struct mod_hdcp_connection {
	struct mod_hdcp_link link;
	uint8_t is_repeater;
	uint8_t is_km_stored;
	uint8_t is_hdcp1_revoked;
	uint8_t is_hdcp2_revoked;
	struct mod_hdcp_trace trace;
	uint8_t hdcp1_retry_count;
	uint8_t hdcp2_retry_count;
};

/* contains values per authentication cycle */
struct mod_hdcp_authentication {
	uint32_t id;
	union mod_hdcp_message msg;
	union mod_hdcp_transition_input trans_input;
	struct mod_hdcp_auth_counters count;
};

/* contains values per state change */
struct mod_hdcp_state {
	uint8_t id;
	uint32_t stay_count;
};

/* per event in a state */
struct mod_hdcp_event_context {
	enum mod_hdcp_event event;
	uint8_t rx_id_list_ready;
	uint8_t unexpected_event;
};

struct mod_hdcp {
	/* per link */
	struct mod_hdcp_config config;
	/* per connection */
	struct mod_hdcp_connection connection;
	/* per displays */
	struct mod_hdcp_display displays[MAX_NUM_OF_DISPLAYS];
	/* per authentication attempt */
	struct mod_hdcp_authentication auth;
	/* per state in an authentication */
	struct mod_hdcp_state state;
	/* reserved memory buffer */
	uint8_t buf[2025];
};

enum mod_hdcp_initial_state_id {
	HDCP_UNINITIALIZED = 0x0,
	HDCP_INITIAL_STATE_START = HDCP_UNINITIALIZED,
	HDCP_INITIALIZED,
	HDCP_CP_NOT_DESIRED,
	HDCP_INITIAL_STATE_END = HDCP_CP_NOT_DESIRED
};

enum mod_hdcp_hdcp1_state_id {
	HDCP1_STATE_START = HDCP_INITIAL_STATE_END,
	H1_A0_WAIT_FOR_ACTIVE_RX,
	H1_A1_EXCHANGE_KSVS,
	H1_A2_COMPUTATIONS_A3_VALIDATE_RX_A6_TEST_FOR_REPEATER,
	H1_A45_AUTHENTICATED,
	H1_A8_WAIT_FOR_READY,
	H1_A9_READ_KSV_LIST,
	HDCP1_STATE_END = H1_A9_READ_KSV_LIST
};

enum mod_hdcp_hdcp1_dp_state_id {
	HDCP1_DP_STATE_START = HDCP1_STATE_END,
	D1_A0_DETERMINE_RX_HDCP_CAPABLE,
	D1_A1_EXCHANGE_KSVS,
	D1_A23_WAIT_FOR_R0_PRIME,
	D1_A2_COMPUTATIONS_A3_VALIDATE_RX_A5_TEST_FOR_REPEATER,
	D1_A4_AUTHENTICATED,
	D1_A6_WAIT_FOR_READY,
	D1_A7_READ_KSV_LIST,
	HDCP1_DP_STATE_END = D1_A7_READ_KSV_LIST,
};

enum mod_hdcp_hdcp2_state_id {
	HDCP2_STATE_START = HDCP1_DP_STATE_END,
	H2_A0_KNOWN_HDCP2_CAPABLE_RX,
	H2_A1_SEND_AKE_INIT,
	H2_A1_VALIDATE_AKE_CERT,
	H2_A1_SEND_NO_STORED_KM,
	H2_A1_READ_H_PRIME,
	H2_A1_READ_PAIRING_INFO_AND_VALIDATE_H_PRIME,
	H2_A1_SEND_STORED_KM,
	H2_A1_VALIDATE_H_PRIME,
	H2_A2_LOCALITY_CHECK,
	H2_A3_EXCHANGE_KS_AND_TEST_FOR_REPEATER,
	H2_ENABLE_ENCRYPTION,
	H2_A5_AUTHENTICATED,
	H2_A6_WAIT_FOR_RX_ID_LIST,
	H2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK,
	H2_A9_SEND_STREAM_MANAGEMENT,
	H2_A9_VALIDATE_STREAM_READY,
	HDCP2_STATE_END = H2_A9_VALIDATE_STREAM_READY,
};

enum mod_hdcp_hdcp2_dp_state_id {
	HDCP2_DP_STATE_START = HDCP2_STATE_END,
	D2_A0_DETERMINE_RX_HDCP_CAPABLE,
	D2_A1_SEND_AKE_INIT,
	D2_A1_VALIDATE_AKE_CERT,
	D2_A1_SEND_NO_STORED_KM,
	D2_A1_READ_H_PRIME,
	D2_A1_READ_PAIRING_INFO_AND_VALIDATE_H_PRIME,
	D2_A1_SEND_STORED_KM,
	D2_A1_VALIDATE_H_PRIME,
	D2_A2_LOCALITY_CHECK,
	D2_A34_EXCHANGE_KS_AND_TEST_FOR_REPEATER,
	D2_SEND_CONTENT_STREAM_TYPE,
	D2_ENABLE_ENCRYPTION,
	D2_A5_AUTHENTICATED,
	D2_A6_WAIT_FOR_RX_ID_LIST,
	D2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK,
	D2_A9_SEND_STREAM_MANAGEMENT,
	D2_A9_VALIDATE_STREAM_READY,
	HDCP2_DP_STATE_END = D2_A9_VALIDATE_STREAM_READY,
	HDCP_STATE_END = HDCP2_DP_STATE_END,
};

/* hdcp1 executions and transitions */
typedef enum mod_hdcp_status (*mod_hdcp_action)(struct mod_hdcp *hdcp);
uint8_t mod_hdcp_execute_and_set(
		mod_hdcp_action func, uint8_t *flag,
		enum mod_hdcp_status *status, struct mod_hdcp *hdcp, char *str);
enum mod_hdcp_status mod_hdcp_hdcp1_execution(struct mod_hdcp *hdcp,
	struct mod_hdcp_event_context *event_ctx,
	struct mod_hdcp_transition_input_hdcp1 *input);
enum mod_hdcp_status mod_hdcp_hdcp1_dp_execution(struct mod_hdcp *hdcp,
	struct mod_hdcp_event_context *event_ctx,
	struct mod_hdcp_transition_input_hdcp1 *input);
enum mod_hdcp_status mod_hdcp_hdcp1_transition(struct mod_hdcp *hdcp,
	struct mod_hdcp_event_context *event_ctx,
	struct mod_hdcp_transition_input_hdcp1 *input,
	struct mod_hdcp_output *output);
enum mod_hdcp_status mod_hdcp_hdcp1_dp_transition(struct mod_hdcp *hdcp,
	struct mod_hdcp_event_context *event_ctx,
	struct mod_hdcp_transition_input_hdcp1 *input,
	struct mod_hdcp_output *output);

/* hdcp2 executions and transitions */
enum mod_hdcp_status mod_hdcp_hdcp2_execution(struct mod_hdcp *hdcp,
	struct mod_hdcp_event_context *event_ctx,
	struct mod_hdcp_transition_input_hdcp2 *input);
enum mod_hdcp_status mod_hdcp_hdcp2_dp_execution(struct mod_hdcp *hdcp,
	struct mod_hdcp_event_context *event_ctx,
	struct mod_hdcp_transition_input_hdcp2 *input);
enum mod_hdcp_status mod_hdcp_hdcp2_transition(struct mod_hdcp *hdcp,
	struct mod_hdcp_event_context *event_ctx,
	struct mod_hdcp_transition_input_hdcp2 *input,
	struct mod_hdcp_output *output);
enum mod_hdcp_status mod_hdcp_hdcp2_dp_transition(struct mod_hdcp *hdcp,
	struct mod_hdcp_event_context *event_ctx,
	struct mod_hdcp_transition_input_hdcp2 *input,
	struct mod_hdcp_output *output);

/* log functions */
void mod_hdcp_dump_binary_message(uint8_t *msg, uint32_t msg_size,
		uint8_t *buf, uint32_t buf_size);
/* TODO: add adjustment log */

/* psp functions */
enum mod_hdcp_status mod_hdcp_add_display_to_topology(
		struct mod_hdcp *hdcp, struct mod_hdcp_display *display);
enum mod_hdcp_status mod_hdcp_remove_display_from_topology(
		struct mod_hdcp *hdcp, uint8_t index);
enum mod_hdcp_status mod_hdcp_hdcp1_create_session(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp1_destroy_session(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp1_validate_rx(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp1_enable_encryption(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp1_validate_ksvlist_vp(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp1_enable_dp_stream_encryption(
	struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp1_link_maintenance(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp1_get_link_encryption_status(struct mod_hdcp *hdcp,
							       enum mod_hdcp_encryption_status *encryption_status);
enum mod_hdcp_status mod_hdcp_hdcp2_create_session(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp2_destroy_session(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp2_prepare_ake_init(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp2_validate_ake_cert(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp2_validate_h_prime(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp2_prepare_lc_init(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp2_validate_l_prime(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp2_prepare_eks(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp2_enable_encryption(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp2_validate_rx_id_list(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp2_enable_dp_stream_encryption(
		struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp2_prepare_stream_management(
		struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp2_validate_stream_ready(
		struct mod_hdcp *hdcp);

/* ddc functions */
enum mod_hdcp_status mod_hdcp_read_bksv(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_bcaps(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_bstatus(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_r0p(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_ksvlist(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_vp(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_binfo(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_aksv(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_ainfo(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_an(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_hdcp2version(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_rxcaps(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_rxstatus(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_ake_cert(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_h_prime(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_pairing_info(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_l_prime(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_rx_id_list(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_stream_ready(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_ake_init(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_no_stored_km(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_stored_km(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_lc_init(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_eks(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_repeater_auth_ack(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_stream_manage(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_content_type(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_clear_cp_irq_status(struct mod_hdcp *hdcp);

/* hdcp version helpers */
static inline uint8_t is_dp_hdcp(struct mod_hdcp *hdcp)
{
	return (hdcp->connection.link.mode == MOD_HDCP_MODE_DP);
}

static inline uint8_t is_dp_mst_hdcp(struct mod_hdcp *hdcp)
{
	return (hdcp->connection.link.mode == MOD_HDCP_MODE_DP &&
			hdcp->connection.link.dp.mst_enabled);
}

static inline uint8_t is_hdmi_dvi_sl_hdcp(struct mod_hdcp *hdcp)
{
	return (hdcp->connection.link.mode == MOD_HDCP_MODE_DEFAULT);
}

/* hdcp state helpers */
static inline uint8_t current_state(struct mod_hdcp *hdcp)
{
	return hdcp->state.id;
}

static inline void set_state_id(struct mod_hdcp *hdcp,
		struct mod_hdcp_output *output, uint8_t id)
{
	memset(&hdcp->state, 0, sizeof(hdcp->state));
	hdcp->state.id = id;
	/* callback timer should be reset per state */
	output->callback_stop = 1;
	output->watchdog_timer_stop = 1;
	HDCP_NEXT_STATE_TRACE(hdcp, id, output);
}

static inline uint8_t is_in_hdcp1_states(struct mod_hdcp *hdcp)
{
	return (current_state(hdcp) > HDCP1_STATE_START &&
			current_state(hdcp) <= HDCP1_STATE_END);
}

static inline uint8_t is_in_hdcp1_dp_states(struct mod_hdcp *hdcp)
{
	return (current_state(hdcp) > HDCP1_DP_STATE_START &&
			current_state(hdcp) <= HDCP1_DP_STATE_END);
}

static inline uint8_t is_in_hdcp2_states(struct mod_hdcp *hdcp)
{
	return (current_state(hdcp) > HDCP2_STATE_START &&
			current_state(hdcp) <= HDCP2_STATE_END);
}

static inline uint8_t is_in_hdcp2_dp_states(struct mod_hdcp *hdcp)
{
	return (current_state(hdcp) > HDCP2_DP_STATE_START &&
			current_state(hdcp) <= HDCP2_DP_STATE_END);
}

static inline uint8_t is_hdcp1(struct mod_hdcp *hdcp)
{
	return (is_in_hdcp1_states(hdcp) || is_in_hdcp1_dp_states(hdcp));
}

static inline uint8_t is_hdcp2(struct mod_hdcp *hdcp)
{
	return (is_in_hdcp2_states(hdcp) || is_in_hdcp2_dp_states(hdcp));
}

static inline uint8_t is_in_cp_not_desired_state(struct mod_hdcp *hdcp)
{
	return current_state(hdcp) == HDCP_CP_NOT_DESIRED;
}

static inline uint8_t is_in_initialized_state(struct mod_hdcp *hdcp)
{
	return current_state(hdcp) == HDCP_INITIALIZED;
}

/* transition operation helpers */
static inline void increment_stay_counter(struct mod_hdcp *hdcp)
{
	hdcp->state.stay_count++;
}

static inline void fail_and_restart_in_ms(uint16_t time,
		enum mod_hdcp_status *status,
		struct mod_hdcp_output *output)
{
	output->callback_needed = 1;
	output->callback_delay = time;
	output->watchdog_timer_needed = 0;
	output->watchdog_timer_delay = 0;
	*status = MOD_HDCP_STATUS_RESET_NEEDED;
}

static inline void callback_in_ms(uint16_t time, struct mod_hdcp_output *output)
{
	output->callback_needed = 1;
	output->callback_delay = time;
}

static inline void set_watchdog_in_ms(struct mod_hdcp *hdcp, uint16_t time,
		struct mod_hdcp_output *output)
{
	output->watchdog_timer_needed = 1;
	output->watchdog_timer_delay = time;
}

/* connection topology helpers */
static inline uint8_t is_display_active(struct mod_hdcp_display *display)
{
	return display->state >= MOD_HDCP_DISPLAY_ACTIVE;
}

static inline uint8_t is_display_encryption_enabled(struct mod_hdcp_display *display)
{
	return display->state >= MOD_HDCP_DISPLAY_ENCRYPTION_ENABLED;
}

static inline uint8_t get_active_display_count(struct mod_hdcp *hdcp)
{
	uint8_t active_count = 0;
	uint8_t i;

	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++)
		if (is_display_active(&hdcp->displays[i]))
			active_count++;
	return active_count;
}

static inline struct mod_hdcp_display *get_first_active_display(
		struct mod_hdcp *hdcp)
{
	uint8_t i;
	struct mod_hdcp_display *display = NULL;

	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++)
		if (is_display_active(&hdcp->displays[i])) {
			display = &hdcp->displays[i];
			break;
		}
	return display;
}

static inline struct mod_hdcp_display *get_active_display_at_index(
		struct mod_hdcp *hdcp, uint8_t index)
{
	uint8_t i;
	struct mod_hdcp_display *display = NULL;

	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++)
		if (hdcp->displays[i].index == index &&
				is_display_active(&hdcp->displays[i])) {
			display = &hdcp->displays[i];
			break;
		}
	return display;
}

static inline struct mod_hdcp_display *get_empty_display_container(
		struct mod_hdcp *hdcp)
{
	uint8_t i;
	struct mod_hdcp_display *display = NULL;

	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++)
		if (!is_display_active(&hdcp->displays[i])) {
			display = &hdcp->displays[i];
			break;
		}
	return display;
}

static inline void reset_retry_counts(struct mod_hdcp *hdcp)
{
	hdcp->connection.hdcp1_retry_count = 0;
	hdcp->connection.hdcp2_retry_count = 0;
}

#endif /* HDCP_H_ */
