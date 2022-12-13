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

/* PXP-Input-Packet: HUC-Authentication */
struct pxp43_start_huc_auth_in {
	struct pxp_cmd_header header;
	__le64 huc_base_address;
} __packed;

/* PXP-Output-Packet: HUC-Authentication */
struct pxp43_start_huc_auth_out {
	struct pxp_cmd_header header;
} __packed;

#endif /* __INTEL_PXP_FW_INTERFACE_43_H__ */
