/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _ABI_GSC_PXP_COMMANDS_ABI_H
#define _ABI_GSC_PXP_COMMANDS_ABI_H

#include <linux/sizes.h>
#include <linux/types.h>

/* Heci client ID for PXP commands */
#define HECI_MEADDRESS_PXP 17

#define PXP_APIVER(x, y) (((x) & 0xFFFF) << 16 | ((y) & 0xFFFF))

/*
 * A PXP sub-section in an HECI packet can be up to 64K big in each direction.
 * This does not include the top-level GSC header.
 */
#define PXP_MAX_PACKET_SIZE SZ_64K

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
	PXP_STATUS_PLATFCONFIG_FIXED_KF1_NOT_SUPPORTED = 0x1037,
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

#define PXP43_CMDID_INVALIDATE_STREAM_KEY 0x00000007
#define PXP43_CMDID_INIT_SESSION 0x00000036
#define PXP43_CMDID_NEW_HUC_AUTH 0x0000003F /* MTL+ */

/* PXP-Input-Packet: HUC Auth-only */
struct pxp43_new_huc_auth_in {
	struct pxp_cmd_header header;
	u64 huc_base_address;
	u32 huc_size;
} __packed;

/* PXP-Output-Packet: HUC Load and Authentication or Auth-only */
struct pxp43_huc_auth_out {
	struct pxp_cmd_header header;
} __packed;

/* PXP-Input-Packet: Init PXP session */
struct pxp43_create_arb_in {
	struct pxp_cmd_header header;
		/* header.stream_id fields for vesion 4.3 of Init PXP session: */
		#define PXP43_INIT_SESSION_VALID BIT(0)
		#define PXP43_INIT_SESSION_APPTYPE BIT(1)
		#define PXP43_INIT_SESSION_APPID GENMASK(17, 2)
	u32 protection_mode;
		#define PXP43_INIT_SESSION_PROTECTION_ARB 0x2
	u32 sub_session_id;
	u32 init_flags;
	u32 rsvd[12];
} __packed;

/* PXP-Input-Packet: Init PXP session */
struct pxp43_create_arb_out {
	struct pxp_cmd_header header;
	u32 rsvd[8];
} __packed;

/* PXP-Input-Packet: Invalidate Stream Key */
struct pxp43_inv_stream_key_in {
	struct pxp_cmd_header header;
	u32 rsvd[3];
} __packed;

/* PXP-Output-Packet: Invalidate Stream Key */
struct pxp43_inv_stream_key_out {
	struct pxp_cmd_header header;
	u32 rsvd;
} __packed;
#endif
