/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
 */
#ifndef __LABEL_H__
#define __LABEL_H__

#include <linux/ndctl.h>
#include <linux/sizes.h>
#include <linux/uuid.h>
#include <linux/io.h>

enum {
	NSINDEX_SIG_LEN = 16,
	NSINDEX_ALIGN = 256,
	NSINDEX_SEQ_MASK = 0x3,
	NSLABEL_UUID_LEN = 16,
	NSLABEL_NAME_LEN = 64,
	NSLABEL_FLAG_ROLABEL = 0x1,  /* read-only label */
	NSLABEL_FLAG_LOCAL = 0x2,    /* DIMM-local namespace */
	NSLABEL_FLAG_BTT = 0x4,      /* namespace contains a BTT */
	NSLABEL_FLAG_UPDATING = 0x8, /* label being updated */
	BTT_ALIGN = 4096,            /* all btt structures */
	BTTINFO_SIG_LEN = 16,
	BTTINFO_UUID_LEN = 16,
	BTTINFO_FLAG_ERROR = 0x1,    /* error state (read-only) */
	BTTINFO_MAJOR_VERSION = 1,
	ND_LABEL_MIN_SIZE = 256 * 4, /* see sizeof_namespace_index() */
	ND_LABEL_ID_SIZE = 50,
	ND_NSINDEX_INIT = 0x1,
};

/**
 * struct nd_namespace_index - label set superblock
 * @sig: NAMESPACE_INDEX\0
 * @flags: placeholder
 * @seq: sequence number for this index
 * @myoff: offset of this index in label area
 * @mysize: size of this index struct
 * @otheroff: offset of other index
 * @labeloff: offset of first label slot
 * @nslot: total number of label slots
 * @major: label area major version
 * @minor: label area minor version
 * @checksum: fletcher64 of all fields
 * @free[0]: bitmap, nlabel bits
 *
 * The size of free[] is rounded up so the total struct size is a
 * multiple of NSINDEX_ALIGN bytes.  Any bits this allocates beyond
 * nlabel bits must be zero.
 */
struct nd_namespace_index {
	u8 sig[NSINDEX_SIG_LEN];
	u8 flags[3];
	u8 labelsize;
	__le32 seq;
	__le64 myoff;
	__le64 mysize;
	__le64 otheroff;
	__le64 labeloff;
	__le32 nslot;
	__le16 major;
	__le16 minor;
	__le64 checksum;
	u8 free[];
};

/**
 * struct nd_namespace_label - namespace superblock
 * @uuid: UUID per RFC 4122
 * @name: optional name (NULL-terminated)
 * @flags: see NSLABEL_FLAG_*
 * @nlabel: num labels to describe this ns
 * @position: labels position in set
 * @isetcookie: interleave set cookie
 * @lbasize: LBA size in bytes or 0 for pmem
 * @dpa: DPA of NVM range on this DIMM
 * @rawsize: size of namespace
 * @slot: slot of this label in label area
 * @unused: must be zero
 */
struct nd_namespace_label {
	u8 uuid[NSLABEL_UUID_LEN];
	u8 name[NSLABEL_NAME_LEN];
	__le32 flags;
	__le16 nlabel;
	__le16 position;
	__le64 isetcookie;
	__le64 lbasize;
	__le64 dpa;
	__le64 rawsize;
	__le32 slot;
	/*
	 * Accessing fields past this point should be gated by a
	 * namespace_label_has() check.
	 */
	u8 align;
	u8 reserved[3];
	guid_t type_guid;
	guid_t abstraction_guid;
	u8 reserved2[88];
	__le64 checksum;
};

#define NVDIMM_BTT_GUID "8aed63a2-29a2-4c66-8b12-f05d15d3922a"
#define NVDIMM_BTT2_GUID "18633bfc-1735-4217-8ac9-17239282d3f8"
#define NVDIMM_PFN_GUID "266400ba-fb9f-4677-bcb0-968f11d0d225"
#define NVDIMM_DAX_GUID "97a86d9c-3cdd-4eda-986f-5068b4f80088"

/**
 * struct nd_label_id - identifier string for dpa allocation
 * @id: "{blk|pmem}-<namespace uuid>"
 */
struct nd_label_id {
	char id[ND_LABEL_ID_SIZE];
};

/*
 * If the 'best' index is invalid, so is the 'next' index.  Otherwise,
 * the next index is MOD(index+1, 2)
 */
static inline int nd_label_next_nsindex(int index)
{
	if (index < 0)
		return -1;

	return (index + 1) % 2;
}

struct nvdimm_drvdata;
int nd_label_data_init(struct nvdimm_drvdata *ndd);
size_t sizeof_namespace_index(struct nvdimm_drvdata *ndd);
int nd_label_active_count(struct nvdimm_drvdata *ndd);
struct nd_namespace_label *nd_label_active(struct nvdimm_drvdata *ndd, int n);
u32 nd_label_alloc_slot(struct nvdimm_drvdata *ndd);
bool nd_label_free_slot(struct nvdimm_drvdata *ndd, u32 slot);
u32 nd_label_nfree(struct nvdimm_drvdata *ndd);
struct nd_region;
struct nd_namespace_pmem;
struct nd_namespace_blk;
int nd_pmem_namespace_label_update(struct nd_region *nd_region,
		struct nd_namespace_pmem *nspm, resource_size_t size);
int nd_blk_namespace_label_update(struct nd_region *nd_region,
		struct nd_namespace_blk *nsblk, resource_size_t size);
#endif /* __LABEL_H__ */
