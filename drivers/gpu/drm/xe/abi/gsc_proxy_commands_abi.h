/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _ABI_GSC_PROXY_COMMANDS_ABI_H
#define _ABI_GSC_PROXY_COMMANDS_ABI_H

#include <linux/types.h>

/* Heci client ID for proxy commands */
#define HECI_MEADDRESS_PROXY 10

/* FW-defined proxy header */
struct xe_gsc_proxy_header {
	/*
	 * hdr:
	 * Bits 0-7: type of the proxy message (see enum xe_gsc_proxy_type)
	 * Bits 8-15: rsvd
	 * Bits 16-31: length in bytes of the payload following the proxy header
	 */
	u32 hdr;
#define GSC_PROXY_TYPE		 GENMASK(7, 0)
#define GSC_PROXY_PAYLOAD_LENGTH GENMASK(31, 16)

	u32 source;		/* Source of the Proxy message */
	u32 destination;	/* Destination of the Proxy message */
#define GSC_PROXY_ADDRESSING_KMD  0x10000
#define GSC_PROXY_ADDRESSING_GSC  0x20000
#define GSC_PROXY_ADDRESSING_CSME 0x30000

	u32 status;		/* Command status */
} __packed;

/* FW-defined proxy types */
enum xe_gsc_proxy_type {
	GSC_PROXY_MSG_TYPE_PROXY_INVALID = 0,
	GSC_PROXY_MSG_TYPE_PROXY_QUERY = 1,
	GSC_PROXY_MSG_TYPE_PROXY_PAYLOAD = 2,
	GSC_PROXY_MSG_TYPE_PROXY_END = 3,
	GSC_PROXY_MSG_TYPE_PROXY_NOTIFICATION = 4,
};

#endif
