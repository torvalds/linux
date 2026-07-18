/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_SYSCTRL_ABI_H_
#define _XE_SYSCTRL_ABI_H_

#include <linux/types.h>

/**
 * DOC: System Controller ABI
 *
 * This header defines the Application Binary Interface (ABI) used by
 * drm/xe to communicate with System Controller firmware on Intel Xe3p
 * discrete GPU platforms.
 *
 * System Controller (sysctrl) is a firmware-managed entity on Intel
 * dGPUs responsible for certain low-level platform management
 * functions.
 *
 * Communication protocol:
 *
 * Communication uses a mailbox interface with messages composed of:
 *
 * - Application message header (struct xe_sysctrl_app_msg_hdr)
 *   containing group_id, command, and version
 * - Variable-length, command-specific payload
 *
 * Message header format:
 *
 * The 32-bit application message header is packed as:
 *
 * - Bits [7:0]   : Group ID identifying command group
 * - Bits [15:8]  : Command identifier within group
 * - Bits [23:16] : Command version for interface compatibility
 * - Bits [31:24] : Reserved, must be zero
 *
 * This header defines firmware ABI message formats and constants shared
 * between driver and System Controller firmware.
 */

/**
 * struct xe_sysctrl_app_msg_hdr - Application layer message header
 * @data: 32-bit header data
 *
 * Header structure for application-level messages.
 */
struct xe_sysctrl_app_msg_hdr {
	u32 data;
} __packed;

#define SYSCTRL_HDR_GROUP_ID_MASK	GENMASK(7, 0)
#define SYSCTRL_HDR_COMMAND_MASK	GENMASK(14, 8)
#define SYSCTRL_HDR_COMMAND_MAX		0x7f
#define SYSCTRL_HDR_IS_RESPONSE		BIT(15)
#define SYSCTRL_HDR_RESERVED_MASK	GENMASK(23, 16)
#define SYSCTRL_HDR_RESULT_MASK		GENMASK(31, 24)

#define APP_HDR_GROUP_ID_MASK		GENMASK(7, 0)
#define APP_HDR_COMMAND_MASK		GENMASK(15, 8)
#define APP_HDR_VERSION_MASK		GENMASK(23, 16)
#define APP_HDR_RESERVED_MASK		GENMASK(31, 24)

#endif
