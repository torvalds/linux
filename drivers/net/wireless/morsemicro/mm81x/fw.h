/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2026 Morse Micro
 */

#ifndef _MM81X_FW_H_
#define _MM81X_FW_H_

#include <linux/firmware.h>
#include <linux/completion.h>
#include <linux/elf.h>
#include "command_defs.h"
#include "yaps_hw.h"

#define BCF_DATABASE_SIZE (1024)
#define MM81X_FW_DIR "morsemicro/mm81x"
#define MM81X_FW_EXT ".bin"

#define MM81X_FW_VER_MAX HOST_CMD_SEMVER_MAJOR
#define MM81X_FW_VER_MIN 56

/* FW_CAPABILITIES_FLAGS_WIDTH = ceil(MM81X_CAPS_MAX_HW_LEN / 32) */
#define FW_CAPABILITIES_FLAGS_WIDTH (4)

struct mm81x_elf32_ehdr {
	unsigned char e_ident[EI_NIDENT];
	__le16 e_type;
	__le16 e_machine;
	__le32 e_version;
	__le32 e_entry;
	__le32 e_phoff;
	__le32 e_shoff;
	__le32 e_flags;
	__le16 e_ehsize;
	__le16 e_phentsize;
	__le16 e_phnum;
	__le16 e_shentsize;
	__le16 e_shnum;
	__le16 e_shstrndx;
} __packed;

struct mm81x_elf32_shdr {
	__le32 sh_name;
	__le32 sh_type;
	__le32 sh_flags;
	__le32 sh_addr;
	__le32 sh_offset;
	__le32 sh_size;
	__le32 sh_link;
	__le32 sh_info;
	__le32 sh_addralign;
	__le32 sh_entsize;
} __packed;

struct mm81x_elf32_phdr {
	__le32 p_type;
	__le32 p_offset;
	__le32 p_vaddr;
	__le32 p_paddr;
	__le32 p_filesz;
	__le32 p_memsz;
	__le32 p_flags;
	__le32 p_align;
} __packed;

enum mm81x_fw_info_tlv_type {
	MM81X_FW_INFO_TLV_BCF_ADDR = 1,
};

struct mm81x_fw_info_tlv {
	__le16 type;
	__le16 length;
	u8 val[];
} __packed;

enum mm81x_fw_ext_host_tbl_tag {
	/* The S1G capability tag */
	MM81X_FW_HOST_TABLE_TAG_S1G_CAPABILITIES = 0,
	MM81X_FW_HOST_TABLE_TAG_PAGER_BYPASS_TX_STATUS = 1,
	MM81X_FW_HOST_TABLE_TAG_INSERT_SKB_CHECKSUM = 2,
	MM81X_FW_HOST_TABLE_TAG_YAPS_TABLE = 3,
	MM81X_FW_HOST_TABLE_TAG_PAGER_PKT_MEMORY = 4,
	MM81X_FW_HOST_TABLE_TAG_PAGER_BYPASS_CMD_RESP = 5,
};

struct ext_host_tbl_tlv_hdr {
	/* The tag used to identify which capability this represents */
	__le16 tag;
	/* The length of the capability structure including this header */
	__le16 length;
} __packed;

struct ext_host_tbl_s1g_caps {
	struct ext_host_tbl_tlv_hdr header;
	__le32 flags[FW_CAPABILITIES_FLAGS_WIDTH];
	/*
	 * The minimum A-MPDU start spacing required by firmware.
	 * Value | Description
	 * ------|------------
	 * 0     | No restriction
	 * 1     | 1/4 us
	 * 2     | 1/2 us
	 * 3     | 1 us
	 * 4     | 2 us
	 * 5     | 4 us
	 * 6     | 8 us
	 * 7     | 16 us
	 */
	u8 ampdu_mss;
	u8 beamformee_sts_capability;
	u8 number_sounding_dimensions;
	/*
	 * The maximum A-MPDU length. This is the exponent value such that
	 * (2^(13 + exponent) - 1) is the length
	 */
	u8 maximum_ampdu_length;
	/*
	 * Offset to apply to the specification's MMSS table to signal further
	 * minimum MPDU start spacing.
	 */
	u8 mm81x_mmss_offset;
} __packed;

struct ext_host_tbl_insert_skb_checksum {
	struct ext_host_tbl_tlv_hdr header;
	u8 insert_and_validate_checksum;
};

struct ext_host_tbl_yaps_table {
	struct ext_host_tbl_tlv_hdr header;
	struct mm81x_yaps_hw_table yaps_table;
} __packed;

struct ext_host_tbl {
	__le32 ext_host_tbl_length;
	u8 dev_mac_addr[6];
	u8 ext_host_table_data_tlvs[];
} __packed;

int mm81x_fw_init(struct mm81x *mors, bool reset);
int mm81x_fw_parse_ext_host_tbl(struct mm81x *mors);

#endif /* !_MM81X_FW_H_ */
