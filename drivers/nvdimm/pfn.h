/*
 * Copyright (c) 2014-2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __NVDIMM_PFN_H
#define __NVDIMM_PFN_H

#include <linux/types.h>

#define PFN_SIG_LEN 16
#define PFN_SIG "NVDIMM_PFN_INFO\0"

struct nd_pfn_sb {
	u8 signature[PFN_SIG_LEN];
	u8 uuid[16];
	u8 parent_uuid[16];
	__le32 flags;
	__le16 version_major;
	__le16 version_minor;
	__le64 dataoff;
	__le64 npfns;
	__le32 mode;
	u8 padding[4012];
	__le64 checksum;
};
#endif /* __NVDIMM_PFN_H */
