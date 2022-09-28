/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020-2022, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_TEE_INTERFACE_H__
#define __INTEL_PXP_TEE_INTERFACE_H__

#include <linux/types.h>

#define PXP_TEE_APIVER 0x40002
#define PXP_TEE_43_APIVER 0x00040003
#define PXP_TEE_ARB_CMDID 0x1e
#define PXP_TEE_ARB_PROTECTION_MODE 0x2
#define PXP_TEE_43_START_HUC_AUTH   0x0000003A

/*
 * there are a lot of status codes for PXP, but we only define the ones we
 * actually can handle in the driver. other failure codes will be printed to
 * error msg for debug.
 */
enum pxp_status {
	PXP_STATUS_SUCCESS = 0x0,
	PXP_STATUS_OP_NOT_PERMITTED = 0x4013
};

/* PXP TEE message header */
struct pxp_tee_cmd_header {
	u32 api_version;
	u32 command_id;
	u32 status;
	/* Length of the message (excluding the header) */
	u32 buffer_len;
} __packed;

/* PXP TEE message input to create a arbitrary session */
struct pxp_tee_create_arb_in {
	struct pxp_tee_cmd_header header;
	u32 protection_mode;
	u32 session_id;
} __packed;

/* PXP TEE message output to create a arbitrary session */
struct pxp_tee_create_arb_out {
	struct pxp_tee_cmd_header header;
} __packed;

struct pxp_tee_start_huc_auth_in {
	struct pxp_tee_cmd_header header;
	__le64                    huc_base_address;
};

struct pxp_tee_start_huc_auth_out {
	struct pxp_tee_cmd_header header;
};

#endif /* __INTEL_PXP_TEE_INTERFACE_H__ */
