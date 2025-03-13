// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013--2024 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/dma-mapping.h>
#include <linux/gfp_types.h>
#include <linux/math64.h>
#include <linux/sizes.h>
#include <linux/types.h>

#include "ipu6.h"
#include "ipu6-bus.h"
#include "ipu6-cpd.h"
#include "ipu6-dma.h"

/* 15 entries + header*/
#define MAX_PKG_DIR_ENT_CNT		16
/* 2 qword per entry/header */
#define PKG_DIR_ENT_LEN			2
/* PKG_DIR size in bytes */
#define PKG_DIR_SIZE			((MAX_PKG_DIR_ENT_CNT) *	\
					 (PKG_DIR_ENT_LEN) * sizeof(u64))
/* _IUPKDR_ */
#define PKG_DIR_HDR_MARK		0x5f4955504b44525fULL

/* $CPD */
#define CPD_HDR_MARK			0x44504324

#define MAX_MANIFEST_SIZE		(SZ_2K * sizeof(u32))
#define MAX_METADATA_SIZE		SZ_64K

#define MAX_COMPONENT_ID		127
#define MAX_COMPONENT_VERSION		0xffff

#define MANIFEST_IDX	0
#define METADATA_IDX	1
#define MODULEDATA_IDX	2
/*
 * PKG_DIR Entry (type == id)
 * 63:56        55      54:48   47:32   31:24   23:0
 * Rsvd         Rsvd    Type    Version Rsvd    Size
 */
#define PKG_DIR_SIZE_MASK	GENMASK_ULL(23, 0)
#define PKG_DIR_VERSION_MASK	GENMASK_ULL(47, 32)
#define PKG_DIR_TYPE_MASK	GENMASK_ULL(54, 48)

static inline const struct ipu6_cpd_ent *ipu6_cpd_get_entry(const void *cpd,
							    u8 idx)
{
	const struct ipu6_cpd_hdr *cpd_hdr = cpd;
	const struct ipu6_cpd_ent *ent;

	ent = (const struct ipu6_cpd_ent *)((const u8 *)cpd + cpd_hdr->hdr_len);
	return ent + idx;
}

#define ipu6_cpd_get_manifest(cpd) ipu6_cpd_get_entry(cpd, MANIFEST_IDX)
#define ipu6_cpd_get_metadata(cpd) ipu6_cpd_get_entry(cpd, METADATA_IDX)
#define ipu6_cpd_get_moduledata(cpd) ipu6_cpd_get_entry(cpd, MODULEDATA_IDX)

static const struct ipu6_cpd_metadata_cmpnt_hdr *
ipu6_cpd_metadata_get_cmpnt(struct ipu6_device *isp, const void *metadata,
			    unsigned int metadata_size, u8 idx)
{
	size_t extn_size = sizeof(struct ipu6_cpd_metadata_extn);
	size_t cmpnt_count = metadata_size - extn_size;

	cmpnt_count = div_u64(cmpnt_count, isp->cpd_metadata_cmpnt_size);

	if (idx > MAX_COMPONENT_ID || idx >= cmpnt_count) {
		dev_err(&isp->pdev->dev, "Component index out of range (%d)\n",
			idx);
		return ERR_PTR(-EINVAL);
	}

	return metadata + extn_size + idx * isp->cpd_metadata_cmpnt_size;
}

static u32 ipu6_cpd_metadata_cmpnt_version(struct ipu6_device *isp,
					   const void *metadata,
					   unsigned int metadata_size, u8 idx)
{
	const struct ipu6_cpd_metadata_cmpnt_hdr *cmpnt;

	cmpnt = ipu6_cpd_metadata_get_cmpnt(isp, metadata, metadata_size, idx);
	if (IS_ERR(cmpnt))
		return PTR_ERR(cmpnt);

	return cmpnt->ver;
}

static int ipu6_cpd_metadata_get_cmpnt_id(struct ipu6_device *isp,
					  const void *metadata,
					  unsigned int metadata_size, u8 idx)
{
	const struct ipu6_cpd_metadata_cmpnt_hdr *cmpnt;

	cmpnt = ipu6_cpd_metadata_get_cmpnt(isp, metadata, metadata_size, idx);
	if (IS_ERR(cmpnt))
		return PTR_ERR(cmpnt);

	return cmpnt->id;
}

static int ipu6_cpd_parse_module_data(struct ipu6_device *isp,
				      const void *module_data,
				      unsigned int module_data_size,
				      dma_addr_t dma_addr_module_data,
				      u64 *pkg_dir, const void *metadata,
				      unsigned int metadata_size)
{
	const struct ipu6_cpd_module_data_hdr *module_data_hdr;
	const struct ipu6_cpd_hdr *dir_hdr;
	const struct ipu6_cpd_ent *dir_ent;
	unsigned int i;
	u8 len;

	if (!module_data)
		return -EINVAL;

	module_data_hdr = module_data;
	dir_hdr = module_data + module_data_hdr->hdr_len;
	len = dir_hdr->hdr_len;
	dir_ent = (const struct ipu6_cpd_ent *)(((u8 *)dir_hdr) + len);

	pkg_dir[0] = PKG_DIR_HDR_MARK;
	/* pkg_dir entry count = component count + pkg_dir header */
	pkg_dir[1] = dir_hdr->ent_cnt + 1;

	for (i = 0; i < dir_hdr->ent_cnt; i++, dir_ent++) {
		u64 *p = &pkg_dir[PKG_DIR_ENT_LEN *  (1 + i)];
		int ver, id;

		*p++ = dma_addr_module_data + dir_ent->offset;
		id = ipu6_cpd_metadata_get_cmpnt_id(isp, metadata,
						    metadata_size, i);
		if (id < 0 || id > MAX_COMPONENT_ID) {
			dev_err(&isp->pdev->dev, "Invalid CPD component id\n");
			return -EINVAL;
		}

		ver = ipu6_cpd_metadata_cmpnt_version(isp, metadata,
						      metadata_size, i);
		if (ver < 0 || ver > MAX_COMPONENT_VERSION) {
			dev_err(&isp->pdev->dev,
				"Invalid CPD component version\n");
			return -EINVAL;
		}

		*p = FIELD_PREP(PKG_DIR_SIZE_MASK, dir_ent->len) |
			FIELD_PREP(PKG_DIR_TYPE_MASK, id) |
			FIELD_PREP(PKG_DIR_VERSION_MASK, ver);
	}

	return 0;
}

int ipu6_cpd_create_pkg_dir(struct ipu6_bus_device *adev, const void *src)
{
	dma_addr_t dma_addr_src = sg_dma_address(adev->fw_sgt.sgl);
	const struct ipu6_cpd_ent *ent, *man_ent, *met_ent;
	struct ipu6_device *isp = adev->isp;
	unsigned int man_sz, met_sz;
	void *pkg_dir_pos;
	int ret;

	man_ent = ipu6_cpd_get_manifest(src);
	man_sz = man_ent->len;

	met_ent = ipu6_cpd_get_metadata(src);
	met_sz = met_ent->len;

	adev->pkg_dir_size = PKG_DIR_SIZE + man_sz + met_sz;
	adev->pkg_dir = ipu6_dma_alloc(adev, adev->pkg_dir_size,
				       &adev->pkg_dir_dma_addr, GFP_KERNEL, 0);
	if (!adev->pkg_dir)
		return -ENOMEM;

	/*
	 * pkg_dir entry/header:
	 * qword | 63:56 | 55   | 54:48 | 47:32 | 31:24 | 23:0
	 * N         Address/Offset/"_IUPKDR_"
	 * N + 1 | rsvd  | rsvd | type  | ver   | rsvd  | size
	 *
	 * We can ignore other fields that size in N + 1 qword as they
	 * are 0 anyway. Just setting size for now.
	 */

	ent = ipu6_cpd_get_moduledata(src);

	ret = ipu6_cpd_parse_module_data(isp, src + ent->offset,
					 ent->len, dma_addr_src + ent->offset,
					 adev->pkg_dir, src + met_ent->offset,
					 met_ent->len);
	if (ret) {
		dev_err(&isp->pdev->dev, "Failed to parse module data\n");
		ipu6_dma_free(adev, adev->pkg_dir_size,
			      adev->pkg_dir, adev->pkg_dir_dma_addr, 0);
		return ret;
	}

	/* Copy manifest after pkg_dir */
	pkg_dir_pos = adev->pkg_dir + PKG_DIR_ENT_LEN * MAX_PKG_DIR_ENT_CNT;
	memcpy(pkg_dir_pos, src + man_ent->offset, man_sz);

	/* Copy metadata after manifest */
	pkg_dir_pos += man_sz;
	memcpy(pkg_dir_pos, src + met_ent->offset, met_sz);

	ipu6_dma_sync_single(adev, adev->pkg_dir_dma_addr,
			     adev->pkg_dir_size);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu6_cpd_create_pkg_dir, "INTEL_IPU6");

void ipu6_cpd_free_pkg_dir(struct ipu6_bus_device *adev)
{
	ipu6_dma_free(adev, adev->pkg_dir_size, adev->pkg_dir,
		      adev->pkg_dir_dma_addr, 0);
}
EXPORT_SYMBOL_NS_GPL(ipu6_cpd_free_pkg_dir, "INTEL_IPU6");

static int ipu6_cpd_validate_cpd(struct ipu6_device *isp, const void *cpd,
				 unsigned long cpd_size,
				 unsigned long data_size)
{
	const struct ipu6_cpd_hdr *cpd_hdr = cpd;
	const struct ipu6_cpd_ent *ent;
	unsigned int i;
	u8 len;

	len = cpd_hdr->hdr_len;

	/* Ensure cpd hdr is within moduledata */
	if (cpd_size < len) {
		dev_err(&isp->pdev->dev, "Invalid CPD moduledata size\n");
		return -EINVAL;
	}

	/* Sanity check for CPD header */
	if ((cpd_size - len) / sizeof(*ent) < cpd_hdr->ent_cnt) {
		dev_err(&isp->pdev->dev, "Invalid CPD header\n");
		return -EINVAL;
	}

	/* Ensure that all entries are within moduledata */
	ent = (const struct ipu6_cpd_ent *)(((const u8 *)cpd_hdr) + len);
	for (i = 0; i < cpd_hdr->ent_cnt; i++, ent++) {
		if (data_size < ent->offset ||
		    data_size - ent->offset < ent->len) {
			dev_err(&isp->pdev->dev, "Invalid CPD entry (%d)\n", i);
			return -EINVAL;
		}
	}

	return 0;
}

static int ipu6_cpd_validate_moduledata(struct ipu6_device *isp,
					const void *moduledata,
					u32 moduledata_size)
{
	const struct ipu6_cpd_module_data_hdr *mod_hdr = moduledata;
	int ret;

	/* Ensure moduledata hdr is within moduledata */
	if (moduledata_size < sizeof(*mod_hdr) ||
	    moduledata_size < mod_hdr->hdr_len) {
		dev_err(&isp->pdev->dev, "Invalid CPD moduledata size\n");
		return -EINVAL;
	}

	dev_dbg(&isp->pdev->dev, "FW version: %x\n", mod_hdr->fw_pkg_date);
	ret = ipu6_cpd_validate_cpd(isp, moduledata + mod_hdr->hdr_len,
				    moduledata_size - mod_hdr->hdr_len,
				    moduledata_size);
	if (ret) {
		dev_err(&isp->pdev->dev, "Invalid CPD in moduledata\n");
		return ret;
	}

	return 0;
}

static int ipu6_cpd_validate_metadata(struct ipu6_device *isp,
				      const void *metadata, u32 meta_size)
{
	const struct ipu6_cpd_metadata_extn *extn = metadata;

	/* Sanity check for metadata size */
	if (meta_size < sizeof(*extn) || meta_size > MAX_METADATA_SIZE) {
		dev_err(&isp->pdev->dev, "Invalid CPD metadata\n");
		return -EINVAL;
	}

	/* Validate extension and image types */
	if (extn->extn_type != IPU6_CPD_METADATA_EXTN_TYPE_IUNIT ||
	    extn->img_type != IPU6_CPD_METADATA_IMAGE_TYPE_MAIN_FIRMWARE) {
		dev_err(&isp->pdev->dev,
			"Invalid CPD metadata descriptor img_type (%d)\n",
			extn->img_type);
		return -EINVAL;
	}

	/* Validate metadata size multiple of metadata components */
	if ((meta_size - sizeof(*extn)) % isp->cpd_metadata_cmpnt_size) {
		dev_err(&isp->pdev->dev, "Invalid CPD metadata size\n");
		return -EINVAL;
	}

	return 0;
}

int ipu6_cpd_validate_cpd_file(struct ipu6_device *isp, const void *cpd_file,
			       unsigned long cpd_file_size)
{
	const struct ipu6_cpd_hdr *hdr = cpd_file;
	const struct ipu6_cpd_ent *ent;
	int ret;

	ret = ipu6_cpd_validate_cpd(isp, cpd_file, cpd_file_size,
				    cpd_file_size);
	if (ret) {
		dev_err(&isp->pdev->dev, "Invalid CPD in file\n");
		return ret;
	}

	/* Check for CPD file marker */
	if (hdr->hdr_mark != CPD_HDR_MARK) {
		dev_err(&isp->pdev->dev, "Invalid CPD header\n");
		return -EINVAL;
	}

	/* Sanity check for manifest size */
	ent = ipu6_cpd_get_manifest(cpd_file);
	if (ent->len > MAX_MANIFEST_SIZE) {
		dev_err(&isp->pdev->dev, "Invalid CPD manifest size\n");
		return -EINVAL;
	}

	/* Validate metadata */
	ent = ipu6_cpd_get_metadata(cpd_file);
	ret = ipu6_cpd_validate_metadata(isp, cpd_file + ent->offset, ent->len);
	if (ret) {
		dev_err(&isp->pdev->dev, "Invalid CPD metadata\n");
		return ret;
	}

	/* Validate moduledata */
	ent = ipu6_cpd_get_moduledata(cpd_file);
	ret = ipu6_cpd_validate_moduledata(isp, cpd_file + ent->offset,
					   ent->len);
	if (ret)
		dev_err(&isp->pdev->dev, "Invalid CPD moduledata\n");

	return ret;
}
