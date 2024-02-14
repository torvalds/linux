/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _ABI_GSC_MKHI_COMMANDS_ABI_H
#define _ABI_GSC_MKHI_COMMANDS_ABI_H

#include <linux/types.h>

/* Heci client ID for MKHI commands */
#define HECI_MEADDRESS_MKHI 7

/* Generic MKHI header */
struct gsc_mkhi_header {
	u8  group_id;
	u8  command;
	u8  reserved;
	u8  result;
} __packed;

/* GFX_SRV commands */
#define MKHI_GROUP_ID_GFX_SRV 0x30

#define MKHI_GFX_SRV_GET_HOST_COMPATIBILITY_VERSION (0x42)

struct gsc_get_compatibility_version_in {
	struct gsc_mkhi_header header;
} __packed;

struct gsc_get_compatibility_version_out {
	struct gsc_mkhi_header header;
	u16 proj_major;
	u16 compat_major;
	u16 compat_minor;
	u16 reserved[5];
} __packed;

#endif
