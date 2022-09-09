/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014-2015, Intel Corporation.
 */

#ifndef __NVDIMM_PFN_H
#define __NVDIMM_PFN_H

#include <linux/types.h>
#include <linux/mmzone.h>

#define PFN_SIG_LEN 16
#define PFN_SIG "NVDIMM_PFN_INFO\0"
#define DAX_SIG "NVDIMM_DAX_INFO\0"

struct nd_pfn_sb {
	u8 signature[PFN_SIG_LEN];
	u8 uuid[16];
	u8 parent_uuid[16];
	__le32 flags;
	__le16 version_major;
	__le16 version_minor;
	__le64 dataoff; /* relative to namespace_base + start_pad */
	__le64 npfns;
	__le32 mode;
	/* minor-version-1 additions for section alignment */
	/**
	 * @start_pad: Deprecated attribute to pad start-misaligned namespaces
	 *
	 * start_pad is deprecated because the original definition did
	 * not comprehend that dataoff is relative to the base address
	 * of the namespace not the start_pad adjusted base. The result
	 * is that the dax path is broken, but the block-I/O path is
	 * not. The kernel will no longer create namespaces using start
	 * padding, but it still supports block-I/O for legacy
	 * configurations mainly to allow a backup, reconfigure the
	 * namespace, and restore flow to repair dax operation.
	 */
	__le32 start_pad;
	__le32 end_trunc;
	/* minor-version-2 record the base alignment of the mapping */
	__le32 align;
	/* minor-version-3 guarantee the padding and flags are zero */
	/* minor-version-4 record the page size and struct page size */
	__le32 page_size;
	__le16 page_struct_size;
	u8 padding[3994];
	__le64 checksum;
};

#endif /* __NVDIMM_PFN_H */
