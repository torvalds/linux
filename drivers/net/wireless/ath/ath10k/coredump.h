/*
 * Copyright (c) 2011-2017 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _COREDUMP_H_
#define _COREDUMP_H_

#include "core.h"

#define ATH10K_FW_CRASH_DUMP_VERSION 1

/**
 * enum ath10k_fw_crash_dump_type - types of data in the dump file
 * @ATH10K_FW_CRASH_DUMP_REGDUMP: Register crash dump in binary format
 */
enum ath10k_fw_crash_dump_type {
	ATH10K_FW_CRASH_DUMP_REGISTERS = 0,
	ATH10K_FW_CRASH_DUMP_CE_DATA = 1,

	/* contains multiple struct ath10k_dump_ram_data_hdr */
	ATH10K_FW_CRASH_DUMP_RAM_DATA = 2,

	ATH10K_FW_CRASH_DUMP_MAX,
};

struct ath10k_tlv_dump_data {
	/* see ath10k_fw_crash_dump_type above */
	__le32 type;

	/* in bytes */
	__le32 tlv_len;

	/* pad to 32-bit boundaries as needed */
	u8 tlv_data[];
} __packed;

struct ath10k_dump_file_data {
	/* dump file information */

	/* "ATH10K-FW-DUMP" */
	char df_magic[16];

	__le32 len;

	/* file dump version */
	__le32 version;

	/* some info we can get from ath10k struct that might help */

	guid_t guid;

	__le32 chip_id;

	/* 0 for now, in place for later hardware */
	__le32 bus_type;

	__le32 target_version;
	__le32 fw_version_major;
	__le32 fw_version_minor;
	__le32 fw_version_release;
	__le32 fw_version_build;
	__le32 phy_capability;
	__le32 hw_min_tx_power;
	__le32 hw_max_tx_power;
	__le32 ht_cap_info;
	__le32 vht_cap_info;
	__le32 num_rf_chains;

	/* firmware version string */
	char fw_ver[ETHTOOL_FWVERS_LEN];

	/* Kernel related information */

	/* time-of-day stamp */
	__le64 tv_sec;

	/* time-of-day stamp, nano-seconds */
	__le64 tv_nsec;

	/* LINUX_VERSION_CODE */
	__le32 kernel_ver_code;

	/* VERMAGIC_STRING */
	char kernel_ver[64];

	/* room for growth w/out changing binary format */
	u8 unused[128];

	/* struct ath10k_tlv_dump_data + more */
	u8 data[0];
} __packed;

struct ath10k_dump_ram_data_hdr {
	/* enum ath10k_mem_region_type */
	__le32 region_type;

	__le32 start;

	/* length of payload data, not including this header */
	__le32 length;

	u8 data[0];
};

/* magic number to fill the holes not copied due to sections in regions */
#define ATH10K_MAGIC_NOT_COPIED		0xAA

/* part of user space ABI */
enum ath10k_mem_region_type {
	ATH10K_MEM_REGION_TYPE_REG	= 1,
	ATH10K_MEM_REGION_TYPE_DRAM	= 2,
	ATH10K_MEM_REGION_TYPE_AXI	= 3,
	ATH10K_MEM_REGION_TYPE_IRAM1	= 4,
	ATH10K_MEM_REGION_TYPE_IRAM2	= 5,
	ATH10K_MEM_REGION_TYPE_IOSRAM	= 6,
	ATH10K_MEM_REGION_TYPE_IOREG	= 7,
};

/* Define a section of the region which should be copied. As not all parts
 * of the memory is possible to copy, for example some of the registers can
 * be like that, sections can be used to define what is safe to copy.
 *
 * To minimize the size of the array, the list must obey the format:
 * '{start0,stop0},{start1,stop1},{start2,stop2}....' The values below must
 * also obey to 'start0 < stop0 < start1 < stop1 < start2 < ...', otherwise
 * we may encouter error in the dump processing.
 */
struct ath10k_mem_section {
	u32 start;
	u32 end;
};

/* One region of a memory layout. If the sections field is null entire
 * region is copied. If sections is non-null only the areas specified in
 * sections are copied and rest of the areas are filled with
 * ATH10K_MAGIC_NOT_COPIED.
 */
struct ath10k_mem_region {
	enum ath10k_mem_region_type type;
	u32 start;
	u32 len;

	const char *name;

	struct {
		const struct ath10k_mem_section *sections;
		u32 size;
	} section_table;
};

/* Contains the memory layout of a hardware version identified with the
 * hardware id, split into regions.
 */
struct ath10k_hw_mem_layout {
	u32 hw_id;

	struct {
		const struct ath10k_mem_region *regions;
		int size;
	} region_table;
};

/* FIXME: where to put this? */
extern unsigned long ath10k_coredump_mask;

#ifdef CONFIG_DEV_COREDUMP

int ath10k_coredump_submit(struct ath10k *ar);
struct ath10k_fw_crash_data *ath10k_coredump_new(struct ath10k *ar);
int ath10k_coredump_create(struct ath10k *ar);
int ath10k_coredump_register(struct ath10k *ar);
void ath10k_coredump_unregister(struct ath10k *ar);
void ath10k_coredump_destroy(struct ath10k *ar);

const struct ath10k_hw_mem_layout *ath10k_coredump_get_mem_layout(struct ath10k *ar);

#else /* CONFIG_DEV_COREDUMP */

static inline int ath10k_coredump_submit(struct ath10k *ar)
{
	return 0;
}

static inline struct ath10k_fw_crash_data *ath10k_coredump_new(struct ath10k *ar)
{
	return NULL;
}

static inline int ath10k_coredump_create(struct ath10k *ar)
{
	return 0;
}

static inline int ath10k_coredump_register(struct ath10k *ar)
{
	return 0;
}

static inline void ath10k_coredump_unregister(struct ath10k *ar)
{
}

static inline void ath10k_coredump_destroy(struct ath10k *ar)
{
}

static inline const struct ath10k_hw_mem_layout *
ath10k_coredump_get_mem_layout(struct ath10k *ar)
{
	return NULL;
}

#endif /* CONFIG_DEV_COREDUMP */

#endif /* _COREDUMP_H_ */
