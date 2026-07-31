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
 */
enum xe_sysctrl_group {
	XE_SYSCTRL_GROUP_GFSP			= 0x01,
};

/**
 * enum xe_sysctrl_gfsp_cmd - Commands supported by GFSP group
 *
 * @XE_SYSCTRL_CMD_GET_SOC_ERROR: Retrieve basic error information
 * @XE_SYSCTRL_CMD_GET_COUNTER: Get error counter value
 * @XE_SYSCTRL_CMD_CLEAR_COUNTER: Clear error counter value
 * @XE_SYSCTRL_CMD_GET_PENDING_EVENT: Retrieve pending event
 * @XE_SYSCTRL_CMD_GET_HEALTH: Retrieve gpu health
 * @XE_SYSCTRL_CMD_SET_HEALTH: Set gpu health
 */
enum xe_sysctrl_gfsp_cmd {
	XE_SYSCTRL_CMD_GET_SOC_ERROR		= 0x01,
	XE_SYSCTRL_CMD_GET_COUNTER		= 0x03,
	XE_SYSCTRL_CMD_CLEAR_COUNTER		= 0x04,
	XE_SYSCTRL_CMD_GET_PENDING_EVENT	= 0x07,
	XE_SYSCTRL_CMD_GET_HEALTH		= 0x0B,
	XE_SYSCTRL_CMD_SET_HEALTH		= 0x0C,
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
};

/* Modify as needed */
#define XE_SYSCTRL_FLOOD_LIMIT		16

#define XE_SYSCTRL_MB_FRAME_SIZE	16
#define XE_SYSCTRL_MB_MAX_FRAMES	64
#define XE_SYSCTRL_MB_MAX_MESSAGE_SIZE	\
	(XE_SYSCTRL_MB_FRAME_SIZE * XE_SYSCTRL_MB_MAX_FRAMES)

#define XE_SYSCTRL_MB_DEFAULT_TIMEOUT_MS	500

#endif
