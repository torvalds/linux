/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _INTEL_GSC_UC_HECI_CMD_SUBMIT_H_
#define _INTEL_GSC_UC_HECI_CMD_SUBMIT_H_

#include <linux/types.h>

struct intel_gsc_uc;
struct intel_gsc_mtl_header {
	u32 validity_marker;
#define GSC_HECI_VALIDITY_MARKER 0xA578875A

	u8 heci_client_id;
#define HECI_MEADDRESS_PXP 17
#define HECI_MEADDRESS_HDCP 18

	u8 reserved1;

	u16 header_version;
#define MTL_GSC_HEADER_VERSION 1

	u64 host_session_handle;
	u64 gsc_message_handle;

	u32 message_size; /* lower 20 bits only, upper 12 are reserved */

	/*
	 * Flags mask:
	 * Bit 0: Pending
	 * Bit 1: Session Cleanup;
	 * Bits 2-15: Flags
	 * Bits 16-31: Extension Size
	 */
	u32 flags;

	u32 status;
} __packed;

int intel_gsc_uc_heci_cmd_submit_packet(struct intel_gsc_uc *gsc,
					u64 addr_in, u32 size_in,
					u64 addr_out, u32 size_out);
#endif
