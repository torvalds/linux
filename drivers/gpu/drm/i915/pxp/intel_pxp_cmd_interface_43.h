/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2022, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_FW_INTERFACE_43_H__
#define __INTEL_PXP_FW_INTERFACE_43_H__

#include <linux/types.h>
#include "intel_pxp_cmd_interface_cmn.h"

/* PXP-Cmd-Op definitions */
#define PXP43_CMDID_START_HUC_AUTH 0x0000003A
#define PXP43_CMDID_NEW_HUC_AUTH 0x0000003F /* MTL+ */
#define PXP43_CMDID_INIT_SESSION 0x00000036

/* PXP-Packet sizes for MTL's GSCCS-HECI instruction is spec'd at 65K before page alignment*/
#define PXP43_MAX_HECI_INOUT_SIZE (PAGE_ALIGN(SZ_64K + SZ_1K))

/* PXP-Packet size for MTL's NEW_HUC_AUTH instruction */
#define PXP43_HUC_AUTH_INOUT_SIZE (SZ_4K)

/* PXP-Input-Packet: HUC Load and Authentication */
struct pxp43_start_huc_auth_in {
	struct pxp_cmd_header header;
	__le64 huc_base_address;
} __packed;

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

#endif /* __INTEL_PXP_FW_INTERFACE_43_H__ */
