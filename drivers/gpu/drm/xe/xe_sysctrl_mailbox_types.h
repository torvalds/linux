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
 * @XE_SYSCTRL_CMD_GET_PENDING_EVENT: Retrieve pending event
 */
enum xe_sysctrl_gfsp_cmd {
	XE_SYSCTRL_CMD_GET_PENDING_EVENT	= 0x07,
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

#define XE_SYSCTRL_MB_FRAME_SIZE	16
#define XE_SYSCTRL_MB_MAX_FRAMES	64
#define XE_SYSCTRL_MB_MAX_MESSAGE_SIZE	\
	(XE_SYSCTRL_MB_FRAME_SIZE * XE_SYSCTRL_MB_MAX_FRAMES)

#define XE_SYSCTRL_MB_DEFAULT_TIMEOUT_MS	500

#endif
