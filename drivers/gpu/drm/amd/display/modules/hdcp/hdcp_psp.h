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

#ifndef MODULES_HDCP_HDCP_PSP_H_
#define MODULES_HDCP_HDCP_PSP_H_

/*
 * NOTE: These parameters are a one-to-one copy of the
 * parameters required by PSP
 */
enum bgd_security_hdcp_encryption_level {
	HDCP_ENCRYPTION_LEVEL__INVALID = 0,
	HDCP_ENCRYPTION_LEVEL__OFF,
	HDCP_ENCRYPTION_LEVEL__ON
};

enum ta_dtm_command {
	TA_DTM_COMMAND__UNUSED_1 = 1,
	TA_DTM_COMMAND__TOPOLOGY_UPDATE_V2,
	TA_DTM_COMMAND__TOPOLOGY_ASSR_ENABLE
};

/* DTM related enumerations */
/**********************************************************/

enum ta_dtm_status {
	TA_DTM_STATUS__SUCCESS = 0x00,
	TA_DTM_STATUS__GENERIC_FAILURE = 0x01,
	TA_DTM_STATUS__INVALID_PARAMETER = 0x02,
	TA_DTM_STATUS__NULL_POINTER = 0x3
};

/* input/output structures for DTM commands */
/**********************************************************/
/**
 * Input structures
 */
enum ta_dtm_hdcp_version_max_supported {
	TA_DTM_HDCP_VERSION_MAX_SUPPORTED__NONE = 0,
	TA_DTM_HDCP_VERSION_MAX_SUPPORTED__1_x = 10,
	TA_DTM_HDCP_VERSION_MAX_SUPPORTED__2_0 = 20,
	TA_DTM_HDCP_VERSION_MAX_SUPPORTED__2_1 = 21,
	TA_DTM_HDCP_VERSION_MAX_SUPPORTED__2_2 = 22,
	TA_DTM_HDCP_VERSION_MAX_SUPPORTED__2_3 = 23
};

struct ta_dtm_topology_update_input_v2 {
	/* display handle is unique across the driver and is used to identify a display */
	/* for all security interfaces which reference displays such as HDCP */
	uint32_t display_handle;
	uint32_t is_active;
	uint32_t is_miracast;
	uint32_t controller;
	uint32_t ddc_line;
	uint32_t dig_be;
	uint32_t dig_fe;
	uint32_t dp_mst_vcid;
	uint32_t is_assr;
	uint32_t max_hdcp_supported_version;
};

struct ta_dtm_topology_assr_enable {
	uint32_t display_topology_dig_be_index;
};

/**
 * Output structures
 */

/* No output structures yet */

union ta_dtm_cmd_input {
	struct ta_dtm_topology_update_input_v2 topology_update_v2;
	struct ta_dtm_topology_assr_enable topology_assr_enable;
};

union ta_dtm_cmd_output {
	uint32_t reserved;
};

struct ta_dtm_shared_memory {
	uint32_t cmd_id;
	uint32_t resp_id;
	enum ta_dtm_status dtm_status;
	uint32_t reserved;
	union ta_dtm_cmd_input dtm_in_message;
	union ta_dtm_cmd_output dtm_out_message;
};

int psp_cmd_submit_buf(struct psp_context *psp, struct amdgpu_firmware_info *ucode, struct psp_gfx_cmd_resp *cmd,
		uint64_t fence_mc_addr);

enum ta_hdcp_command {
	TA_HDCP_COMMAND__INITIALIZE,
	TA_HDCP_COMMAND__HDCP1_CREATE_SESSION,
	TA_HDCP_COMMAND__HDCP1_DESTROY_SESSION,
	TA_HDCP_COMMAND__HDCP1_FIRST_PART_AUTHENTICATION,
	TA_HDCP_COMMAND__HDCP1_SECOND_PART_AUTHENTICATION,
	TA_HDCP_COMMAND__HDCP1_ENABLE_ENCRYPTION,
	TA_HDCP_COMMAND__HDCP1_ENABLE_DP_STREAM_ENCRYPTION,
	TA_HDCP_COMMAND__HDCP1_GET_ENCRYPTION_STATUS,
};


/* HDCP related enumerations */
/**********************************************************/
#define TA_HDCP__INVALID_SESSION 0xFFFF
#define TA_HDCP__HDCP1_AN_SIZE 8
#define TA_HDCP__HDCP1_KSV_SIZE 5
#define TA_HDCP__HDCP1_KSV_LIST_MAX_ENTRIES 127
#define TA_HDCP__HDCP1_V_PRIME_SIZE 20

enum ta_hdcp_status {
	TA_HDCP_STATUS__SUCCESS = 0x00,
	TA_HDCP_STATUS__GENERIC_FAILURE = 0x01,
	TA_HDCP_STATUS__NULL_POINTER = 0x02,
	TA_HDCP_STATUS__FAILED_ALLOCATING_SESSION = 0x03,
	TA_HDCP_STATUS__FAILED_SETUP_TX = 0x04,
	TA_HDCP_STATUS__INVALID_PARAMETER = 0x05,
	TA_HDCP_STATUS__VHX_ERROR = 0x06,
	TA_HDCP_STATUS__SESSION_NOT_CLOSED_PROPERLY = 0x07,
	TA_HDCP_STATUS__SRM_FAILURE = 0x08,
	TA_HDCP_STATUS__MST_AUTHENTICATED_ALREADY_STARTED = 0x09,
	TA_HDCP_STATUS__AKE_SEND_CERT_FAILURE = 0x0A,
	TA_HDCP_STATUS__AKE_NO_STORED_KM_FAILURE = 0x0B,
	TA_HDCP_STATUS__AKE_SEND_HPRIME_FAILURE = 0x0C,
	TA_HDCP_STATUS__LC_SEND_LPRIME_FAILURE = 0x0D,
	TA_HDCP_STATUS__SKE_SEND_EKS_FAILURE = 0x0E,
	TA_HDCP_STATUS__REPAUTH_SEND_RXIDLIST_FAILURE = 0x0F,
	TA_HDCP_STATUS__REPAUTH_STREAM_READY_FAILURE = 0x10,
	TA_HDCP_STATUS__ASD_GENERIC_FAILURE = 0x11,
	TA_HDCP_STATUS__UNWRAP_SECRET_FAILURE = 0x12,
	TA_HDCP_STATUS__ENABLE_ENCR_FAILURE = 0x13,
	TA_HDCP_STATUS__DISABLE_ENCR_FAILURE = 0x14,
	TA_HDCP_STATUS__NOT_ENOUGH_MEMORY_FAILURE = 0x15,
	TA_HDCP_STATUS__UNKNOWN_MESSAGE = 0x16,
	TA_HDCP_STATUS__TOO_MANY_STREAM = 0x17
};

enum ta_hdcp_authentication_status {
	TA_HDCP_AUTHENTICATION_STATUS__NOT_STARTED = 0x00,
	TA_HDCP_AUTHENTICATION_STATUS__HDCP1_FIRST_PART_FAILED = 0x01,
	TA_HDCP_AUTHENTICATION_STATUS__HDCP1_FIRST_PART_COMPLETE = 0x02,
	TA_HDCP_AUTHENTICATION_STATUS__HDCP1_SECOND_PART_FAILED = 0x03,
	TA_HDCP_AUTHENTICATION_STATUS__HDCP1_AUTHENTICATED = 0x04,
	TA_HDCP_AUTHENTICATION_STATUS__HDCP1_KSV_VALIDATION_FAILED = 0x09
};


/* input/output structures for HDCP commands */
/**********************************************************/
struct ta_hdcp_cmd_hdcp1_create_session_input {
	uint8_t display_handle;
};

struct ta_hdcp_cmd_hdcp1_create_session_output {
	uint32_t session_handle;
	uint8_t an_primary[TA_HDCP__HDCP1_AN_SIZE];
	uint8_t aksv_primary[TA_HDCP__HDCP1_KSV_SIZE];
	uint8_t ainfo_primary;
	uint8_t an_secondary[TA_HDCP__HDCP1_AN_SIZE];
	uint8_t aksv_secondary[TA_HDCP__HDCP1_KSV_SIZE];
	uint8_t ainfo_secondary;
};

struct ta_hdcp_cmd_hdcp1_destroy_session_input {
	uint32_t session_handle;
};

struct ta_hdcp_cmd_hdcp1_first_part_authentication_input {
	uint32_t session_handle;
	uint8_t bksv_primary[TA_HDCP__HDCP1_KSV_SIZE];
	uint8_t bksv_secondary[TA_HDCP__HDCP1_KSV_SIZE];
	uint8_t bcaps;
	uint16_t r0_prime_primary;
	uint16_t r0_prime_secondary;
};

struct ta_hdcp_cmd_hdcp1_first_part_authentication_output {
	enum ta_hdcp_authentication_status authentication_status;
};

struct ta_hdcp_cmd_hdcp1_second_part_authentication_input {
	uint32_t session_handle;
	uint16_t bstatus_binfo;
	uint8_t ksv_list[TA_HDCP__HDCP1_KSV_LIST_MAX_ENTRIES][TA_HDCP__HDCP1_KSV_SIZE];
	uint32_t ksv_list_size;
	uint8_t pj_prime;
	uint8_t v_prime[TA_HDCP__HDCP1_V_PRIME_SIZE];
};

struct ta_hdcp_cmd_hdcp1_second_part_authentication_output {
	enum ta_hdcp_authentication_status authentication_status;
};

struct ta_hdcp_cmd_hdcp1_enable_encryption_input {
	uint32_t session_handle;
};

struct ta_hdcp_cmd_hdcp1_enable_dp_stream_encryption_input {
	uint32_t session_handle;
	uint32_t display_handle;
};

struct ta_hdcp_cmd_hdcp1_get_encryption_status_input {
	uint32_t session_handle;
};

struct ta_hdcp_cmd_hdcp1_get_encryption_status_output {
	uint32_t protection_level;
};

/**********************************************************/
/* Common input structure for HDCP callbacks */
union ta_hdcp_cmd_input {
	struct ta_hdcp_cmd_hdcp1_create_session_input hdcp1_create_session;
	struct ta_hdcp_cmd_hdcp1_destroy_session_input hdcp1_destroy_session;
	struct ta_hdcp_cmd_hdcp1_first_part_authentication_input hdcp1_first_part_authentication;
	struct ta_hdcp_cmd_hdcp1_second_part_authentication_input hdcp1_second_part_authentication;
	struct ta_hdcp_cmd_hdcp1_enable_encryption_input hdcp1_enable_encryption;
	struct ta_hdcp_cmd_hdcp1_enable_dp_stream_encryption_input hdcp1_enable_dp_stream_encryption;
	struct ta_hdcp_cmd_hdcp1_get_encryption_status_input hdcp1_get_encryption_status;
};

/* Common output structure for HDCP callbacks */
union ta_hdcp_cmd_output {
	struct ta_hdcp_cmd_hdcp1_create_session_output hdcp1_create_session;
	struct ta_hdcp_cmd_hdcp1_first_part_authentication_output hdcp1_first_part_authentication;
	struct ta_hdcp_cmd_hdcp1_second_part_authentication_output hdcp1_second_part_authentication;
	struct ta_hdcp_cmd_hdcp1_get_encryption_status_output hdcp1_get_encryption_status;
};
/**********************************************************/

struct ta_hdcp_shared_memory {
	uint32_t cmd_id;
	enum ta_hdcp_status hdcp_status;
	uint32_t reserved;
	union ta_hdcp_cmd_input in_msg;
	union ta_hdcp_cmd_output out_msg;
};

enum psp_status {
	PSP_STATUS__SUCCESS = 0,
	PSP_STATUS__ERROR_INVALID_PARAMS,
	PSP_STATUS__ERROR_GENERIC,
	PSP_STATUS__ERROR_OUT_OF_MEMORY,
	PSP_STATUS__ERROR_UNSUPPORTED_FEATURE
};

#endif /* MODULES_HDCP_HDCP_PSP_H_ */
