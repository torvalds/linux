/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_FW_INFO_H
#define PVR_FW_INFO_H

#include <linux/bits.h>
#include <linux/sizes.h>
#include <linux/types.h>

/*
 * Firmware binary block unit in bytes.
 * Raw data stored in FW binary will be aligned to this size.
 */
#define FW_BLOCK_SIZE SZ_4K

/* Maximum number of entries in firmware layout table. */
#define PVR_FW_INFO_MAX_NUM_ENTRIES 8

enum pvr_fw_section_id {
	META_CODE = 0,
	META_PRIVATE_DATA,
	META_COREMEM_CODE,
	META_COREMEM_DATA,
	MIPS_CODE,
	MIPS_EXCEPTIONS_CODE,
	MIPS_BOOT_CODE,
	MIPS_PRIVATE_DATA,
	MIPS_BOOT_DATA,
	MIPS_STACK,
	RISCV_UNCACHED_CODE,
	RISCV_CACHED_CODE,
	RISCV_PRIVATE_DATA,
	RISCV_COREMEM_CODE,
	RISCV_COREMEM_DATA,
};

enum pvr_fw_section_type {
	NONE = 0,
	FW_CODE,
	FW_DATA,
	FW_COREMEM_CODE,
	FW_COREMEM_DATA,
};

/*
 * FW binary format with FW info attached:
 *
 *          Contents        Offset
 *     +-----------------+
 *     |                 |    0
 *     |                 |
 *     | Original binary |
 *     |      file       |
 *     |   (.ldr/.elf)   |
 *     |                 |
 *     |                 |
 *     +-----------------+
 *     |   Device info   |  FILE_SIZE - 4K - device_info_size
 *     +-----------------+
 *     | FW info header  |  FILE_SIZE - 4K
 *     +-----------------+
 *     |                 |
 *     | FW layout table |
 *     |                 |
 *     +-----------------+
 *                          FILE_SIZE
 */

#define PVR_FW_INFO_VERSION 3

#define PVR_FW_FLAGS_OPEN_SOURCE BIT(0)

/** struct pvr_fw_info_header - Firmware header */
struct pvr_fw_info_header {
	/** @info_version: FW info header version. */
	u32 info_version;
	/** @header_len: Header length. */
	u32 header_len;
	/** @layout_entry_num: Number of entries in the layout table. */
	u32 layout_entry_num;
	/** @layout_entry_size: Size of an entry in the layout table. */
	u32 layout_entry_size;
	/** @bvnc: GPU ID supported by firmware. */
	aligned_u64 bvnc;
	/** @fw_page_size: Page size of processor on which firmware executes. */
	u32 fw_page_size;
	/** @flags: Compatibility flags. */
	u32 flags;
	/** @fw_version_major: Firmware major version number. */
	u16 fw_version_major;
	/** @fw_version_minor: Firmware minor version number. */
	u16 fw_version_minor;
	/** @fw_version_build: Firmware build number. */
	u32 fw_version_build;
	/** @device_info_size: Size of device info structure. */
	u32 device_info_size;
	/** @padding: Padding. */
	u32 padding;
};

/**
 * struct pvr_fw_layout_entry - Entry in firmware layout table, describing a
 *                              section of the firmware image
 */
struct pvr_fw_layout_entry {
	/** @id: Section ID. */
	enum pvr_fw_section_id id;
	/** @type: Section type. */
	enum pvr_fw_section_type type;
	/** @base_addr: Base address of section in FW address space. */
	u32 base_addr;
	/** @max_size: Maximum size of section, in bytes. */
	u32 max_size;
	/** @alloc_size: Allocation size of section, in bytes. */
	u32 alloc_size;
	/** @alloc_offset: Allocation offset of section. */
	u32 alloc_offset;
};

/**
 * struct pvr_fw_device_info_header - Device information header.
 */
struct pvr_fw_device_info_header {
	/** @brn_mask_size: BRN mask size (in u64s). */
	u64 brn_mask_size;
	/** @ern_mask_size: ERN mask size (in u64s). */
	u64 ern_mask_size;
	/** @feature_mask_size: Feature mask size (in u64s). */
	u64 feature_mask_size;
	/** @feature_param_size: Feature parameter size (in u64s). */
	u64 feature_param_size;
};

#endif /* PVR_FW_INFO_H */
