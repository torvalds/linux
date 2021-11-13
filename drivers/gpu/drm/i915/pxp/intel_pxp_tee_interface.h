/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_TEE_INTERFACE_H__
#define __INTEL_PXP_TEE_INTERFACE_H__

#include <linux/types.h>

#define PXP_TEE_APIVER 0x40002
#define PXP_TEE_ARB_CMDID 0x1e
#define PXP_TEE_ARB_PROTECTION_MODE 0x2

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

#endif /* __INTEL_PXP_TEE_INTERFACE_H__ */
