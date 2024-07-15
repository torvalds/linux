/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_UC_FW_ABI_H
#define _XE_UC_FW_ABI_H

#include <linux/build_bug.h>
#include <linux/types.h>

/**
 * DOC: CSS-based Firmware Layout
 *
 * The CSS-based firmware structure is used for GuC releases on all platforms
 * and for HuC releases up to DG1. Starting from DG2/MTL the HuC uses the GSC
 * layout instead.
 * The CSS firmware layout looks like this::
 *
 *      +======================================================================+
 *      |  Firmware blob                                                       |
 *      +===============+===============+============+============+============+
 *      |  CSS header   |     uCode     |  RSA key   |  modulus   |  exponent  |
 *      +===============+===============+============+============+============+
 *       <-header size->                 <---header size continued ----------->
 *       <--- size ----------------------------------------------------------->
 *                                       <-key size->
 *                                                    <-mod size->
 *                                                                 <-exp size->
 *
 * The firmware may or may not have modulus key and exponent data. The header,
 * uCode and RSA signature are must-have components that will be used by driver.
 * Length of each components, which is all in dwords, can be found in header.
 * In the case that modulus and exponent are not present in fw, a.k.a truncated
 * image, the length value still appears in header.
 *
 * Driver will do some basic fw size validation based on the following rules:
 *
 * 1. Header, uCode and RSA are must-have components.
 * 2. All firmware components, if they present, are in the sequence illustrated
 *    in the layout table above.
 * 3. Length info of each component can be found in header, in dwords.
 * 4. Modulus and exponent key are not required by driver. They may not appear
 *    in fw. So driver will load a truncated firmware in this case.
 */

struct uc_css_header {
	u32 module_type;
	/*
	 * header_size includes all non-uCode bits, including css_header, rsa
	 * key, modulus key and exponent data.
	 */
	u32 header_size_dw;
	u32 header_version;
	u32 module_id;
	u32 module_vendor;
	u32 date;
#define CSS_DATE_DAY			(0xFF << 0)
#define CSS_DATE_MONTH			(0xFF << 8)
#define CSS_DATE_YEAR			(0xFFFF << 16)
	u32 size_dw; /* uCode plus header_size_dw */
	u32 key_size_dw;
	u32 modulus_size_dw;
	u32 exponent_size_dw;
	u32 time;
#define CSS_TIME_HOUR			(0xFF << 0)
#define CSS_DATE_MIN			(0xFF << 8)
#define CSS_DATE_SEC			(0xFFFF << 16)
	char username[8];
	char buildnumber[12];
	u32 sw_version;
#define CSS_SW_VERSION_UC_MAJOR		(0xFF << 16)
#define CSS_SW_VERSION_UC_MINOR		(0xFF << 8)
#define CSS_SW_VERSION_UC_PATCH		(0xFF << 0)
	union {
		u32 submission_version; /* only applies to GuC */
		u32 reserved2;
	};
	u32 reserved0[12];
	union {
		u32 private_data_size; /* only applies to GuC */
		u32 reserved1;
	};
	u32 header_info;
} __packed;
static_assert(sizeof(struct uc_css_header) == 128);

/**
 * DOC: GSC-based Firmware Layout
 *
 * The GSC-based firmware structure is used for GSC releases on all platforms
 * and for HuC releases starting from DG2/MTL. Older HuC releases use the
 * CSS-based layout instead. Differently from the CSS headers, the GSC headers
 * uses a directory + entries structure (i.e., there is array of addresses
 * pointing to specific header extensions identified by a name). Although the
 * header structures are the same, some of the entries are specific to GSC while
 * others are specific to HuC. The manifest header entry, which includes basic
 * information about the binary (like the version) is always present, but it is
 * named differently based on the binary type.
 *
 * The HuC binary starts with a Code Partition Directory (CPD) header. The
 * entries we're interested in for use in the driver are:
 *
 * 1. "HUCP.man": points to the manifest header for the HuC.
 * 2. "huc_fw": points to the FW code. On platforms that support load via DMA
 *    and 2-step HuC authentication (i.e. MTL+) this is a full CSS-based binary,
 *    while if the GSC is the one doing the load (which only happens on DG2)
 *    this section only contains the uCode.
 *
 * The GSC-based HuC firmware layout looks like this::
 *
 *	+================================================+
 *	|  CPD Header                                    |
 *	+================================================+
 *	|  CPD entries[]                                 |
 *	|      entry1                                    |
 *	|      ...                                       |
 *	|      entryX                                    |
 *	|          "HUCP.man"                            |
 *	|           ...                                  |
 *	|           offset  >----------------------------|------o
 *	|      ...                                       |      |
 *	|      entryY                                    |      |
 *	|          "huc_fw"                              |      |
 *	|           ...                                  |      |
 *	|           offset  >----------------------------|----------o
 *	+================================================+      |   |
 *	                                                        |   |
 *	+================================================+      |   |
 *	|  Manifest Header                               |<-----o   |
 *	|      ...                                       |          |
 *	|      FW version                                |          |
 *	|      ...                                       |          |
 *	+================================================+          |
 *	                                                            |
 *	+================================================+          |
 *	|  FW binary                                     |<---------o
 *	|      CSS (MTL+ only)                           |
 *	|      uCode                                     |
 *	|      RSA Key (MTL+ only)                       |
 *	|      ...                                       |
 *	+================================================+
 *
 * The GSC binary starts instead with a layout header, which contains the
 * locations of the various partitions of the binary. The one we're interested
 * in is the boot1 partition, where we can find a BPDT header followed by
 * entries, one of which points to the RBE sub-section of the partition, which
 * contains the CPD. The GSC blob does not contain a CSS-based binary, so we
 * only need to look for the manifest, which is under the "RBEP.man" CPD entry.
 * Note that we have no need to find where the actual FW code is inside the
 * image because the GSC ROM will itself parse the headers to find it and load
 * it.
 * The GSC firmware header layout looks like this::
 *
 *	+================================================+
 *	|  Layout Pointers                               |
 *	|      ...                                       |
 *	|      Boot1 offset  >---------------------------|------o
 *	|      ...                                       |      |
 *	+================================================+      |
 *	                                                        |
 *	+================================================+      |
 *	|  BPDT header                                   |<-----o
 *	+================================================+
 *	|  BPDT entries[]                                |
 *	|      entry1                                    |
 *	|      ...                                       |
 *	|      entryX                                    |
 *	|          type == GSC_RBE                       |
 *	|          offset  >-----------------------------|------o
 *	|      ...                                       |      |
 *	+================================================+      |
 *	                                                        |
 *	+================================================+      |
 *	|  CPD Header                                    |<-----o
 *	+================================================+
 *	|  CPD entries[]                                 |
 *	|      entry1                                    |
 *	|      ...                                       |
 *	|      entryX                                    |
 *	|          "RBEP.man"                            |
 *	|           ...                                  |
 *	|           offset  >----------------------------|------o
 *	|      ...                                       |      |
 *	+================================================+      |
 *	                                                        |
 *	+================================================+      |
 *	| Manifest Header                                |<-----o
 *	|  ...                                           |
 *	|  FW version                                    |
 *	|  ...                                           |
 *	|  Security version                              |
 *	|  ...                                           |
 *	+================================================+
 */

struct gsc_version {
	u16 major;
	u16 minor;
	u16 hotfix;
	u16 build;
} __packed;

struct gsc_partition {
	u32 offset;
	u32 size;
} __packed;

struct gsc_layout_pointers {
	u8 rom_bypass_vector[16];

	/* size of this header section, not including ROM bypass vector */
	u16 size;

	/*
	 * bit0: Backup copy of layout pointers exists
	 * bits1-15: reserved
	 */
	u8 flags;

	u8 reserved;

	u32 crc32;

	struct gsc_partition datap;
	struct gsc_partition boot1;
	struct gsc_partition boot2;
	struct gsc_partition boot3;
	struct gsc_partition boot4;
	struct gsc_partition boot5;
	struct gsc_partition temp_pages;
} __packed;

/* Boot partition structures */
struct gsc_bpdt_header {
	u32 signature;
#define GSC_BPDT_HEADER_SIGNATURE 0x000055AA

	u16 descriptor_count; /* num of entries after the header */

	u8 version;
	u8 configuration;

	u32 crc32;

	u32 build_version;
	struct gsc_version tool_version;
} __packed;

struct gsc_bpdt_entry {
	/*
	 * Bits 0-15: BPDT entry type
	 * Bits 16-17: reserved
	 * Bit 18: code sub-partition
	 * Bits 19-31: reserved
	 */
	u32 type;
#define GSC_BPDT_ENTRY_TYPE_MASK GENMASK(15, 0)
#define GSC_BPDT_ENTRY_TYPE_GSC_RBE 0x1

	u32 sub_partition_offset; /* from the base of the BPDT header */
	u32 sub_partition_size;
} __packed;

/* Code partition directory (CPD) structures */
struct gsc_cpd_header_v2 {
	u32 header_marker;
#define GSC_CPD_HEADER_MARKER 0x44504324

	u32 num_of_entries;
	u8 header_version;
	u8 entry_version;
	u8 header_length; /* in bytes */
	u8 flags;
	u32 partition_name;
	u32 crc32;
} __packed;

struct gsc_cpd_entry {
	u8 name[12];

	/*
	 * Bits 0-24: offset from the beginning of the code partition
	 * Bit 25: huffman compressed
	 * Bits 26-31: reserved
	 */
	u32 offset;
#define GSC_CPD_ENTRY_OFFSET_MASK GENMASK(24, 0)
#define GSC_CPD_ENTRY_HUFFMAN_COMP BIT(25)

	/*
	 * Module/Item length, in bytes. For Huffman-compressed modules, this
	 * refers to the uncompressed size. For software-compressed modules,
	 * this refers to the compressed size.
	 */
	u32 length;

	u8 reserved[4];
} __packed;

struct gsc_manifest_header {
	u32 header_type; /* 0x4 for manifest type */
	u32 header_length; /* in dwords */
	u32 header_version;
	u32 flags;
	u32 vendor;
	u32 date;
	u32 size; /* In dwords, size of entire manifest (header + extensions) */
	u32 header_id;
	u32 internal_data;
	struct gsc_version fw_version;
	u32 security_version;
	struct gsc_version meu_kit_version;
	u32 meu_manifest_version;
	u8 general_data[4];
	u8 reserved3[56];
	u32 modulus_size; /* in dwords */
	u32 exponent_size; /* in dwords */
} __packed;

#endif
