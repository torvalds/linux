/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _INTEL_GSC_BINARY_HEADERS_H_
#define _INTEL_GSC_BINARY_HEADERS_H_

#include <linux/types.h>

/* Code partition directory (CPD) structures */
struct intel_gsc_cpd_header_v2 {
	u32 header_marker;
#define INTEL_GSC_CPD_HEADER_MARKER 0x44504324

	u32 num_of_entries;
	u8 header_version;
	u8 entry_version;
	u8 header_length; /* in bytes */
	u8 flags;
	u32 partition_name;
	u32 crc32;
} __packed;

struct intel_gsc_cpd_entry {
	u8 name[12];

	/*
	 * Bits 0-24: offset from the beginning of the code partition
	 * Bit 25: huffman compressed
	 * Bits 26-31: reserved
	 */
	u32 offset;
#define INTEL_GSC_CPD_ENTRY_OFFSET_MASK GENMASK(24, 0)
#define INTEL_GSC_CPD_ENTRY_HUFFMAN_COMP BIT(25)

	/*
	 * Module/Item length, in bytes. For Huffman-compressed modules, this
	 * refers to the uncompressed size. For software-compressed modules,
	 * this refers to the compressed size.
	 */
	u32 length;

	u8 reserved[4];
} __packed;

struct intel_gsc_version {
	u16 major;
	u16 minor;
	u16 hotfix;
	u16 build;
} __packed;

struct intel_gsc_manifest_header {
	u32 header_type; /* 0x4 for manifest type */
	u32 header_length; /* in dwords */
	u32 header_version;
	u32 flags;
	u32 vendor;
	u32 date;
	u32 size; /* In dwords, size of entire manifest (header + extensions) */
	u32 header_id;
	u32 internal_data;
	struct intel_gsc_version fw_version;
	u32 security_version;
	struct intel_gsc_version meu_kit_version;
	u32 meu_manifest_version;
	u8 general_data[4];
	u8 reserved3[56];
	u32 modulus_size; /* in dwords */
	u32 exponent_size; /* in dwords */
} __packed;

#endif
