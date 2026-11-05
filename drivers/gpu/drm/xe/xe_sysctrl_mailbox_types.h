/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_SYSCTRL_MAILBOX_TYPES_H_
#define _XE_SYSCTRL_MAILBOX_TYPES_H_

#include <linux/types.h>

#include "abi/xe_sysctrl_abi.h"

/**
 * enum xe_sysctrl_group - System Controller command groups
 *
 * @XE_SYSCTRL_GROUP_GFSP: GFSP group
 * @XE_SYSCTRL_GROUP_CORE: Core group
 */
enum xe_sysctrl_group {
	XE_SYSCTRL_GROUP_GFSP			= 0x01,
	XE_SYSCTRL_GROUP_CORE			= 0xFF,
};

/**
 * enum xe_sysctrl_gfsp_cmd - Commands supported by GFSP group
 *
 * @XE_SYSCTRL_CMD_GET_SOC_ERROR: Retrieve basic error information
 * @XE_SYSCTRL_CMD_GET_COUNTER: Get error counter value
 * @XE_SYSCTRL_CMD_CLEAR_COUNTER: Clear error counter value
 * @XE_SYSCTRL_CMD_GET_THRESHOLD: Retrieve error threshold
 * @XE_SYSCTRL_CMD_SET_THRESHOLD: Set error threshold
 * @XE_SYSCTRL_CMD_GET_PENDING_EVENT: Retrieve pending event
 * @XE_SYSCTRL_CMD_GET_HEALTH: Retrieve gpu health
 * @XE_SYSCTRL_CMD_SET_HEALTH: Set gpu health
 */
enum xe_sysctrl_gfsp_cmd {
	XE_SYSCTRL_CMD_GET_SOC_ERROR		= 0x01,
	XE_SYSCTRL_CMD_GET_COUNTER		= 0x03,
	XE_SYSCTRL_CMD_CLEAR_COUNTER		= 0x04,
	XE_SYSCTRL_CMD_GET_THRESHOLD		= 0x05,
	XE_SYSCTRL_CMD_SET_THRESHOLD		= 0x06,
	XE_SYSCTRL_CMD_GET_PENDING_EVENT	= 0x07,
	XE_SYSCTRL_CMD_GET_HEALTH		= 0x0B,
	XE_SYSCTRL_CMD_SET_HEALTH		= 0x0C,
};

/**
 * enum xe_sysctrl_core_cmd - Commands supported by Core group
 *
 * @XE_SYSCTRL_CMD_GET_APP_STATUS_BY_ID: Retrieve application status by ID
 */
enum xe_sysctrl_core_cmd {
	XE_SYSCTRL_CMD_GET_APP_STATUS_BY_ID		= 0x05,
};

/**
 * struct xe_sysctrl_app_status_req - Get application status request
 *
 * @app_id: Application ID for which to retrieve status
 */
struct xe_sysctrl_app_status_req {
	u8 app_id;
} __packed;

/**
 * struct xe_sysctrl_app_status_resp - Get application status response
 * @flags: Application status flags interpreted by xe_sysctrl_check_app_status()
 */
struct xe_sysctrl_app_status_resp {
	u32 flags;
} __packed;

/**
 * enum xe_sysctrl_fw_status - System Controller firmware application lifecycle states
 *
 * @XE_SYSCTRL_FIRMWARE_APP_INVALID: app_id is not recognized by firmware
 * @XE_SYSCTRL_FIRMWARE_APP_NOT_LOADED: application is known but has not yet booted
 * @XE_SYSCTRL_FIRMWARE_APP_BOOTED: boot sequence completed, post-boot init pending
 * @XE_SYSCTRL_FIRMWARE_APP_INITIALIZED: application fully operational
 * @XE_SYSCTRL_FIRMWARE_COMM_FAILURE: communication with System Controller firmware failed
 */
enum xe_sysctrl_fw_status {
	XE_SYSCTRL_FIRMWARE_APP_INVALID,
	XE_SYSCTRL_FIRMWARE_APP_NOT_LOADED,
	XE_SYSCTRL_FIRMWARE_APP_BOOTED,
	XE_SYSCTRL_FIRMWARE_APP_INITIALIZED,
	XE_SYSCTRL_FIRMWARE_COMM_FAILURE,
};

/**
 * struct xe_sysctrl_mailbox_command - System Controller mailbox command
 */
struct xe_sysctrl_mailbox_command {
	/** @header: Application message header containing command information */
	struct xe_sysctrl_app_msg_hdr header;

	/** @data_in: Pointer to input payload data (can be NULL if no input data) */
	void *data_in;

	/** @data_in_len: Size of input payload in bytes (0 if no input data) */
	size_t data_in_len;

	/** @data_out: Pointer to output buffer for response data (can be NULL if no response) */
	void *data_out;

	/** @data_out_len: Size of output buffer in bytes (0 if no response expected) */
	size_t data_out_len;

	/** @timeout_ms: Response timeout in ms, or 0 for %XE_SYSCTRL_MB_DEFAULT_TIMEOUT_MS */
	unsigned int timeout_ms;
};

/* Modify as needed */
#define XE_SYSCTRL_FLOOD_LIMIT		16

#define XE_SYSCTRL_MB_FRAME_SIZE	16
#define XE_SYSCTRL_MB_MAX_FRAMES	64
#define XE_SYSCTRL_MB_MAX_MESSAGE_SIZE	\
	(XE_SYSCTRL_MB_FRAME_SIZE * XE_SYSCTRL_MB_MAX_FRAMES)

#define XE_SYSCTRL_MB_DEFAULT_TIMEOUT_MS	500

#endif
