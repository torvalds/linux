/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _ABI_GUC_LIC_ABI_H_
#define _ABI_GUC_LIC_ABI_H_

#include <linux/types.h>

/**
 * enum guc_lic_type - Log Init Config KLV IDs.
 */
enum guc_lic_type {
	/**
	 * @GUC_LIC_TYPE_GUC_SW_VERSION: GuC firmware version. Value
	 * is a 32 bit number represented by guc_sw_version.
	 */
	GUC_LIC_TYPE_GUC_SW_VERSION = 0x1,
	/**
	 * @GUC_LIC_TYPE_GUC_DEVICE_ID: GuC device id. Value is a 32
	 * bit.
	 */
	GUC_LIC_TYPE_GUC_DEVICE_ID = 0x2,
	/**
	 * @GUC_LIC_TYPE_TSC_FREQUENCY: GuC timestamp counter
	 * frequency. Value is a 32 bit number representing frequency in
	 * kHz. This timestamp is utilized in log entries, timer and
	 * for engine utilization tracking.
	 */
	GUC_LIC_TYPE_TSC_FREQUENCY = 0x3,
	/**
	 * @GUC_LIC_TYPE_GMD_ID: HW GMD ID. Value is a 32 bit number
	 * representing graphics, media and display HW architecture IDs.
	 */
	GUC_LIC_TYPE_GMD_ID = 0x4,
	/**
	 * @GUC_LIC_TYPE_BUILD_PLATFORM_ID: GuC build platform ID.
	 * Value is 32 bits.
	 */
	GUC_LIC_TYPE_BUILD_PLATFORM_ID = 0x5,
};

/**
 * struct guc_lic - GuC LIC (Log-Init-Config) structure.
 *
 * This is populated by the GUC at log init time and is located in the log
 * buffer memory allocation.
 */
struct guc_lic {
	/**
	 * @magic: A magic number set by GuC to identify that this
	 * structure contains valid information: magic = GUC_LIC_MAGIC.
	 */
	u32 magic;
#define GUC_LIC_MAGIC			0x8086900D
	/**
	 * @version: The version of the this structure.
	 * Major and minor version number are represented as bit fields.
	 */
	u32 version;
#define GUC_LIC_VERSION_MASK_MAJOR		GENMASK(31, 16)
#define GUC_LIC_VERSION_MASK_MINOR		GENMASK(15, 0)

#define GUC_LIC_VERSION_MAJOR	1u
#define GUC_LIC_VERSION_MINOR	0u

	/** @data_count: Number of dwords the `data` array contains. */
	u32 data_count;
	/**
	 * @data: Array of dwords representing a list of LIC KLVs of
	 * type guc_klv_generic with keys represented by guc_lic_type
	 */
	u32 data[] __counted_by(data_count);
} __packed;

#endif
