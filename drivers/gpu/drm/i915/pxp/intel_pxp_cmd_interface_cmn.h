/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2022, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_FW_INTERFACE_CMN_H__
#define __INTEL_PXP_FW_INTERFACE_CMN_H__

#include <linux/types.h>

#define PXP_APIVER(x, y) (((x) & 0xFFFF) << 16 | ((y) & 0xFFFF))

/*
 * there are a lot of status codes for PXP, but we only define the cross-API
 * common ones that we actually can handle in the kernel driver. Other failure
 * codes should be printed to error msg for debug.
 */
enum pxp_status {
	PXP_STATUS_SUCCESS = 0x0,
	PXP_STATUS_ERROR_API_VERSION = 0x1002,
	PXP_STATUS_OP_NOT_PERMITTED = 0x4013
};

/* Common PXP FW message header */
struct pxp_cmd_header {
	u32 api_version;
	u32 command_id;
	union {
		u32 status; /* out */
		u32 stream_id; /* in */
	};
	/* Length of the message (excluding the header) */
	u32 buffer_len;
} __packed;

#endif /* __INTEL_PXP_FW_INTERFACE_CMN_H__ */
