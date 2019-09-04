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

#ifndef MOD_HDCP_H_
#define MOD_HDCP_H_

#include "os_types.h"
#include "signal_types.h"

/* Forward Declarations */
struct mod_hdcp;

#define MAX_NUM_OF_DISPLAYS 6
#define MAX_NUM_OF_ATTEMPTS 4
#define MAX_NUM_OF_ERROR_TRACE 10

/* detailed return status */
enum mod_hdcp_status {
	MOD_HDCP_STATUS_SUCCESS = 0,
	MOD_HDCP_STATUS_FAILURE,
	MOD_HDCP_STATUS_RESET_NEEDED,
	MOD_HDCP_STATUS_DISPLAY_OUT_OF_BOUND,
	MOD_HDCP_STATUS_DISPLAY_NOT_FOUND,
	MOD_HDCP_STATUS_INVALID_STATE,
	MOD_HDCP_STATUS_NOT_IMPLEMENTED,
	MOD_HDCP_STATUS_INTERNAL_POLICY_FAILURE,
	MOD_HDCP_STATUS_UPDATE_TOPOLOGY_FAILURE,
	MOD_HDCP_STATUS_CREATE_PSP_SERVICE_FAILURE,
	MOD_HDCP_STATUS_DESTROY_PSP_SERVICE_FAILURE,
	MOD_HDCP_STATUS_HDCP1_CREATE_SESSION_FAILURE,
	MOD_HDCP_STATUS_HDCP1_DESTROY_SESSION_FAILURE,
	MOD_HDCP_STATUS_HDCP1_VALIDATE_ENCRYPTION_FAILURE,
	MOD_HDCP_STATUS_HDCP1_NOT_HDCP_REPEATER,
	MOD_HDCP_STATUS_HDCP1_NOT_CAPABLE,
	MOD_HDCP_STATUS_HDCP1_R0_PRIME_PENDING,
	MOD_HDCP_STATUS_HDCP1_VALIDATE_RX_FAILURE,
	MOD_HDCP_STATUS_HDCP1_KSV_LIST_NOT_READY,
	MOD_HDCP_STATUS_HDCP1_VALIDATE_KSV_LIST_FAILURE,
	MOD_HDCP_STATUS_HDCP1_ENABLE_ENCRYPTION,
	MOD_HDCP_STATUS_HDCP1_ENABLE_STREAM_ENCRYPTION_FAILURE,
	MOD_HDCP_STATUS_HDCP1_MAX_CASCADE_EXCEEDED_FAILURE,
	MOD_HDCP_STATUS_HDCP1_MAX_DEVS_EXCEEDED_FAILURE,
	MOD_HDCP_STATUS_HDCP1_DEVICE_COUNT_MISMATCH_FAILURE,
	MOD_HDCP_STATUS_HDCP1_LINK_INTEGRITY_FAILURE,
	MOD_HDCP_STATUS_HDCP1_REAUTH_REQUEST_ISSUED,
	MOD_HDCP_STATUS_HDCP1_LINK_MAINTENANCE_FAILURE,
	MOD_HDCP_STATUS_HDCP1_INVALID_BKSV,
	MOD_HDCP_STATUS_DDC_FAILURE, /* TODO: specific errors */
	MOD_HDCP_STATUS_INVALID_OPERATION,
	MOD_HDCP_STATUS_HDCP2_NOT_CAPABLE,
	MOD_HDCP_STATUS_HDCP2_CREATE_SESSION_FAILURE,
	MOD_HDCP_STATUS_HDCP2_DESTROY_SESSION_FAILURE,
	MOD_HDCP_STATUS_HDCP2_PREP_AKE_INIT_FAILURE,
	MOD_HDCP_STATUS_HDCP2_AKE_CERT_PENDING,
	MOD_HDCP_STATUS_HDCP2_H_PRIME_PENDING,
	MOD_HDCP_STATUS_HDCP2_PAIRING_INFO_PENDING,
	MOD_HDCP_STATUS_HDCP2_VALIDATE_AKE_CERT_FAILURE,
	MOD_HDCP_STATUS_HDCP2_AKE_CERT_REVOKED,
	MOD_HDCP_STATUS_HDCP2_VALIDATE_H_PRIME_FAILURE,
	MOD_HDCP_STATUS_HDCP2_VALIDATE_PAIRING_INFO_FAILURE,
	MOD_HDCP_STATUS_HDCP2_PREP_LC_INIT_FAILURE,
	MOD_HDCP_STATUS_HDCP2_L_PRIME_PENDING,
	MOD_HDCP_STATUS_HDCP2_VALIDATE_L_PRIME_FAILURE,
	MOD_HDCP_STATUS_HDCP2_PREP_EKS_FAILURE,
	MOD_HDCP_STATUS_HDCP2_ENABLE_ENCRYPTION_FAILURE,
	MOD_HDCP_STATUS_HDCP2_RX_ID_LIST_NOT_READY,
	MOD_HDCP_STATUS_HDCP2_VALIDATE_RX_ID_LIST_FAILURE,
	MOD_HDCP_STATUS_HDCP2_RX_ID_LIST_REVOKED,
	MOD_HDCP_STATUS_HDCP2_ENABLE_STREAM_ENCRYPTION,
	MOD_HDCP_STATUS_HDCP2_STREAM_READY_PENDING,
	MOD_HDCP_STATUS_HDCP2_VALIDATE_STREAM_READY_FAILURE,
	MOD_HDCP_STATUS_HDCP2_PREPARE_STREAM_MANAGEMENT_FAILURE,
	MOD_HDCP_STATUS_HDCP2_REAUTH_REQUEST,
	MOD_HDCP_STATUS_HDCP2_REAUTH_LINK_INTEGRITY_FAILURE,
	MOD_HDCP_STATUS_HDCP2_DEVICE_COUNT_MISMATCH_FAILURE,
};

struct mod_hdcp_displayport {
	uint8_t rev;
	uint8_t assr_supported;
};

struct mod_hdcp_hdmi {
	uint8_t reserved;
};
enum mod_hdcp_operation_mode {
	MOD_HDCP_MODE_OFF,
	MOD_HDCP_MODE_DEFAULT,
	MOD_HDCP_MODE_DP,
	MOD_HDCP_MODE_DP_MST
};

enum mod_hdcp_display_state {
	MOD_HDCP_DISPLAY_INACTIVE = 0,
	MOD_HDCP_DISPLAY_ACTIVE,
	MOD_HDCP_DISPLAY_ACTIVE_AND_ADDED,
	MOD_HDCP_DISPLAY_ENCRYPTION_ENABLED
};

struct mod_hdcp_ddc {
	void *handle;
	struct {
		bool (*read_i2c)(void *handle,
				uint32_t address,
				uint8_t offset,
				uint8_t *data,
				uint32_t size);
		bool (*write_i2c)(void *handle,
				uint32_t address,
				const uint8_t *data,
				uint32_t size);
		bool (*read_dpcd)(void *handle,
				uint32_t address,
				uint8_t *data,
				uint32_t size);
		bool (*write_dpcd)(void *handle,
				uint32_t address,
				const uint8_t *data,
				uint32_t size);
	} funcs;
};

struct mod_hdcp_psp {
	void *handle;
	void *funcs;
};

struct mod_hdcp_display_adjustment {
	uint8_t disable			: 1;
	uint8_t reserved		: 7;
};

struct mod_hdcp_link_adjustment_hdcp1 {
	uint8_t disable			: 1;
	uint8_t postpone_encryption	: 1;
	uint8_t reserved		: 6;
};

enum mod_hdcp_force_hdcp_type {
	MOD_HDCP_FORCE_TYPE_MAX = 0,
	MOD_HDCP_FORCE_TYPE_0,
	MOD_HDCP_FORCE_TYPE_1
};

struct mod_hdcp_link_adjustment_hdcp2 {
	uint8_t disable			: 1;
	uint8_t force_type		: 2;
	uint8_t force_no_stored_km	: 1;
	uint8_t increase_h_prime_timeout: 1;
	uint8_t reserved		: 3;
};

struct mod_hdcp_link_adjustment {
	uint8_t auth_delay;
	struct mod_hdcp_link_adjustment_hdcp1 hdcp1;
	struct mod_hdcp_link_adjustment_hdcp2 hdcp2;
};

struct mod_hdcp_error {
	enum mod_hdcp_status status;
	uint8_t state_id;
};

struct mod_hdcp_trace {
	struct mod_hdcp_error errors[MAX_NUM_OF_ERROR_TRACE];
	uint8_t error_count;
};

enum mod_hdcp_encryption_status {
	MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF = 0,
	MOD_HDCP_ENCRYPTION_STATUS_HDCP1_ON,
	MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE0_ON,
	MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON,
	MOD_HDCP_ENCRYPTION_STATUS_HDCP2_ON
};

/* per link events dm has to notify to hdcp module */
enum mod_hdcp_event {
	MOD_HDCP_EVENT_CALLBACK = 0,
	MOD_HDCP_EVENT_WATCHDOG_TIMEOUT,
	MOD_HDCP_EVENT_CPIRQ
};

/* output flags from module requesting timer operations */
struct mod_hdcp_output {
	uint8_t callback_needed;
	uint8_t callback_stop;
	uint8_t watchdog_timer_needed;
	uint8_t watchdog_timer_stop;
	uint16_t callback_delay;
	uint16_t watchdog_timer_delay;
};

/* used to represent per display info */
struct mod_hdcp_display {
	enum mod_hdcp_display_state state;
	uint8_t index;
	uint8_t controller;
	uint8_t dig_fe;
	union {
		uint8_t vc_id;
	};
	struct mod_hdcp_display_adjustment adjust;
};

/* used to represent per link info */
/* in case a link has multiple displays, they share the same link info */
struct mod_hdcp_link {
	enum mod_hdcp_operation_mode mode;
	uint8_t dig_be;
	uint8_t ddc_line;
	union {
		struct mod_hdcp_displayport dp;
		struct mod_hdcp_hdmi hdmi;
	};
	struct mod_hdcp_link_adjustment adjust;
};

/* a query structure for a display's hdcp information */
struct mod_hdcp_display_query {
	const struct mod_hdcp_display *display;
	const struct mod_hdcp_link *link;
	const struct mod_hdcp_trace *trace;
	enum mod_hdcp_encryption_status encryption_status;
};

/* contains values per on external display configuration change */
struct mod_hdcp_config {
	struct mod_hdcp_psp psp;
	struct mod_hdcp_ddc ddc;
	uint8_t index;
};

struct mod_hdcp;

/* dm allocates memory of mod_hdcp per dc_link on dm init based on memory size*/
size_t mod_hdcp_get_memory_size(void);

/* called per link on link creation */
enum mod_hdcp_status mod_hdcp_setup(struct mod_hdcp *hdcp,
		struct mod_hdcp_config *config);

/* called per link on link destroy */
enum mod_hdcp_status mod_hdcp_teardown(struct mod_hdcp *hdcp);

/* called per display on cp_desired set to true */
enum mod_hdcp_status mod_hdcp_add_display(struct mod_hdcp *hdcp,
		struct mod_hdcp_link *link, struct mod_hdcp_display *display,
		struct mod_hdcp_output *output);

/* called per display on cp_desired set to false */
enum mod_hdcp_status mod_hdcp_remove_display(struct mod_hdcp *hdcp,
		uint8_t index, struct mod_hdcp_output *output);

/* called to query hdcp information on a specific index */
enum mod_hdcp_status mod_hdcp_query_display(struct mod_hdcp *hdcp,
		uint8_t index, struct mod_hdcp_display_query *query);

/* called per link on connectivity change */
enum mod_hdcp_status mod_hdcp_reset_connection(struct mod_hdcp *hdcp,
		struct mod_hdcp_output *output);

/* called per link on events (i.e. callback, watchdog, CP_IRQ) */
enum mod_hdcp_status mod_hdcp_process_event(struct mod_hdcp *hdcp,
		enum mod_hdcp_event event, struct mod_hdcp_output *output);

/* called to convert enum mod_hdcp_status to c string */
char *mod_hdcp_status_to_str(int32_t status);

/* called to convert state id to c string */
char *mod_hdcp_state_id_to_str(int32_t id);

/* called to convert signal type to operation mode */
enum mod_hdcp_operation_mode mod_hdcp_signal_type_to_operation_mode(
		enum signal_type signal);
#endif /* MOD_HDCP_H_ */
