// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 - 2025 Intel Corporation
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/gfp_types.h>
#include <linux/pci.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "ipu7.h"
#include "ipu7-cpd.h"

/* $CPD */
#define CPD_HDR_MARK		0x44504324

/* Maximum size is 4K DWORDs or 16KB */
#define MAX_MANIFEST_SIZE	(SZ_4K * sizeof(u32))

#define CPD_MANIFEST_IDX	0
#define CPD_BINARY_START_IDX	1U
#define CPD_METADATA_START_IDX	2U
#define CPD_BINARY_NUM		2U /* ISYS + PSYS */
/*
 * Entries include:
 * 1 manifest entry.
 * 1 metadata entry for each sub system(ISYS and PSYS).
 * 1 binary entry for each sub system(ISYS and PSYS).
 */
#define CPD_ENTRY_NUM		(CPD_BINARY_NUM * 2U + 1U)

#define CPD_METADATA_ATTR	0xa
#define CPD_METADATA_IPL	0x1c
#define ONLINE_METADATA_SIZE	128U
#define ONLINE_METADATA_LINES	6U

struct ipu7_cpd_hdr {
	u32 hdr_mark;
	u32 ent_cnt;
	u8 hdr_ver;
	u8 ent_ver;
	u8 hdr_len;
	u8 rsvd;
	u8 partition_name[4];
	u32 crc32;
} __packed;

struct ipu7_cpd_ent {
	u8 name[12];
	u32 offset;
	u32 len;
	u8 rsvd[4];
} __packed;

struct ipu7_cpd_metadata_hdr {
	u32 type;
	u32 len;
} __packed;

struct ipu7_cpd_metadata_attr {
	struct ipu7_cpd_metadata_hdr hdr;
	u8 compression_type;
	u8 encryption_type;
	u8 rsvd[2];
	u32 uncompressed_size;
	u32 compressed_size;
	u32 module_id;
	u8 hash[48];
} __packed;

struct ipu7_cpd_metadata_ipl {
	struct ipu7_cpd_metadata_hdr hdr;
	u32 param[4];
	u8 rsvd[8];
} __packed;

struct ipu7_cpd_metadata {
	struct ipu7_cpd_metadata_attr attr;
	struct ipu7_cpd_metadata_ipl ipl;
} __packed;

static inline struct ipu7_cpd_ent *ipu7_cpd_get_entry(const void *cpd, int idx)
{
	const struct ipu7_cpd_hdr *cpd_hdr = cpd;

	return ((struct ipu7_cpd_ent *)((u8 *)cpd + cpd_hdr->hdr_len)) + idx;
}

#define ipu7_cpd_get_manifest(cpd) ipu7_cpd_get_entry(cpd, 0)

static struct ipu7_cpd_metadata *ipu7_cpd_get_metadata(const void *cpd, int idx)
{
	struct ipu7_cpd_ent *cpd_ent =
		ipu7_cpd_get_entry(cpd, CPD_METADATA_START_IDX + idx * 2);

	return (struct ipu7_cpd_metadata *)((u8 *)cpd + cpd_ent->offset);
}

static int ipu7_cpd_validate_cpd(struct ipu7_device *isp,
				 const void *cpd, unsigned long data_size)
{
	const struct ipu7_cpd_hdr *cpd_hdr = cpd;
	struct device *dev = &isp->pdev->dev;
	struct ipu7_cpd_ent *ent;
	unsigned int i;
	u8 len;

	len = cpd_hdr->hdr_len;

	/* Ensure cpd hdr is within moduledata */
	if (data_size < len) {
		dev_err(dev, "Invalid CPD moduledata size\n");
		return -EINVAL;
	}

	/* Check for CPD file marker */
	if (cpd_hdr->hdr_mark != CPD_HDR_MARK) {
		dev_err(dev, "Invalid CPD header marker\n");
		return -EINVAL;
	}

	/* Sanity check for CPD entry header */
	if (cpd_hdr->ent_cnt != CPD_ENTRY_NUM) {
		dev_err(dev, "Invalid CPD entry number %d\n",
			cpd_hdr->ent_cnt);
		return -EINVAL;
	}
	if ((data_size - len) / sizeof(*ent) < cpd_hdr->ent_cnt) {
		dev_err(dev, "Invalid CPD entry headers\n");
		return -EINVAL;
	}

	/* Ensure that all entries are within moduledata */
	ent = (struct ipu7_cpd_ent *)(((u8 *)cpd_hdr) + len);
	for (i = 0; i < cpd_hdr->ent_cnt; i++) {
		if (data_size < ent->offset ||
		    data_size - ent->offset < ent->len) {
			dev_err(dev, "Invalid CPD entry %d\n", i);
			return -EINVAL;
		}
		ent++;
	}

	return 0;
}

static int ipu7_cpd_validate_metadata(struct ipu7_device *isp,
				      const void *cpd, int idx)
{
	const struct ipu7_cpd_ent *cpd_ent =
		ipu7_cpd_get_entry(cpd, CPD_METADATA_START_IDX + idx * 2);
	const struct ipu7_cpd_metadata *metadata =
		ipu7_cpd_get_metadata(cpd, idx);
	struct device *dev = &isp->pdev->dev;

	/* Sanity check for metadata size */
	if (cpd_ent->len != sizeof(struct ipu7_cpd_metadata)) {
		dev_err(dev, "Invalid metadata size\n");
		return -EINVAL;
	}

	/* Validate type and length of metadata sections */
	if (metadata->attr.hdr.type != CPD_METADATA_ATTR) {
		dev_err(dev, "Invalid metadata attr type (%d)\n",
			metadata->attr.hdr.type);
		return -EINVAL;
	}
	if (metadata->attr.hdr.len != sizeof(struct ipu7_cpd_metadata_attr)) {
		dev_err(dev, "Invalid metadata attr size (%d)\n",
			metadata->attr.hdr.len);
		return -EINVAL;
	}
	if (metadata->ipl.hdr.type != CPD_METADATA_IPL) {
		dev_err(dev, "Invalid metadata ipl type (%d)\n",
			metadata->ipl.hdr.type);
		return -EINVAL;
	}
	if (metadata->ipl.hdr.len != sizeof(struct ipu7_cpd_metadata_ipl)) {
		dev_err(dev, "Invalid metadata ipl size (%d)\n",
			metadata->ipl.hdr.len);
		return -EINVAL;
	}

	return 0;
}

int ipu7_cpd_validate_cpd_file(struct ipu7_device *isp, const void *cpd_file,
			       unsigned long cpd_file_size)
{
	struct device *dev = &isp->pdev->dev;
	struct ipu7_cpd_ent *ent;
	unsigned int i;
	int ret;
	char *buf;

	ret = ipu7_cpd_validate_cpd(isp, cpd_file, cpd_file_size);
	if (ret) {
		dev_err(dev, "Invalid CPD in file\n");
		return -EINVAL;
	}

	/* Sanity check for manifest size */
	ent = ipu7_cpd_get_manifest(cpd_file);
	if (ent->len > MAX_MANIFEST_SIZE) {
		dev_err(dev, "Invalid manifest size\n");
		return -EINVAL;
	}

	/* Validate metadata */
	for (i = 0; i < CPD_BINARY_NUM; i++) {
		ret = ipu7_cpd_validate_metadata(isp, cpd_file, i);
		if (ret) {
			dev_err(dev, "Invalid metadata%d\n", i);
			return ret;
		}
	}

	/* Get fw binary version. */
	buf = kmalloc(ONLINE_METADATA_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	for (i = 0; i < CPD_BINARY_NUM; i++) {
		char *lines[ONLINE_METADATA_LINES];
		char *info = buf;
		unsigned int l;

		ent = ipu7_cpd_get_entry(cpd_file,
					 CPD_BINARY_START_IDX + i * 2U);
		memcpy(info, (u8 *)cpd_file + ent->offset + ent->len -
		       ONLINE_METADATA_SIZE, ONLINE_METADATA_SIZE);
		for (l = 0; l < ONLINE_METADATA_LINES; l++) {
			lines[l] = strsep((char **)&info, "\n");
			if (!lines[l])
				break;
		}
		if (l < ONLINE_METADATA_LINES) {
			dev_err(dev, "Failed to parse fw binary%d info.\n", i);
			continue;
		}
		dev_info(dev, "FW binary%d info:\n", i);
		dev_info(dev, "Name: %s\n", lines[1]);
		dev_info(dev, "Version: %s\n", lines[2]);
		dev_info(dev, "Timestamp: %s\n", lines[3]);
		dev_info(dev, "Commit: %s\n", lines[4]);
	}
	kfree(buf);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu7_cpd_validate_cpd_file, "INTEL_IPU7");

int ipu7_cpd_copy_binary(const void *cpd, const char *name,
			 void *code_region, u32 *entry)
{
	unsigned int i;

	for (i = 0; i < CPD_BINARY_NUM; i++) {
		const struct ipu7_cpd_ent *binary =
			ipu7_cpd_get_entry(cpd, CPD_BINARY_START_IDX + i * 2U);
		const struct ipu7_cpd_metadata *metadata =
			ipu7_cpd_get_metadata(cpd, i);

		if (!strncmp(binary->name, name, sizeof(binary->name))) {
			memcpy(code_region + metadata->ipl.param[0],
			       cpd + binary->offset, binary->len);
			*entry = metadata->ipl.param[2];
			return 0;
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL_NS_GPL(ipu7_cpd_copy_binary, "INTEL_IPU7");
