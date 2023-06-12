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
	PXP_STATUS_NOT_READY = 0x100e,
	PXP_STATUS_PLATFCONFIG_KF1_NOVERIF = 0x101a,
	PXP_STATUS_PLATFCONFIG_KF1_BAD = 0x101f,
	PXP_STATUS_OP_NOT_PERMITTED = 0x4013
};

/* Common PXP FW message header */
struct pxp_cmd_header {
	u32 api_version;
	u32 command_id;
	union {
		u32 status; /* out */
		u32 stream_id; /* in */
#define PXP_CMDHDR_EXTDATA_SESSION_VALID GENMASK(0, 0)
#define PXP_CMDHDR_EXTDATA_APP_TYPE GENMASK(1, 1)
#define PXP_CMDHDR_EXTDATA_SESSION_ID GENMASK(17, 2)
	};
	/* Length of the message (excluding the header) */
	u32 buffer_len;
} __packed;

#endif /* __INTEL_PXP_FW_INTERFACE_CMN_H__ */
