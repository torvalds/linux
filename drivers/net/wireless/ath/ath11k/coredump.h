/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _ATH11K_COREDUMP_H_
#define _ATH11K_COREDUMP_H_

#define ATH11K_FW_CRASH_DUMP_V2      2

enum ath11k_fw_crash_dump_type {
	FW_CRASH_DUMP_PAGING_DATA,
	FW_CRASH_DUMP_RDDM_DATA,
	FW_CRASH_DUMP_REMOTE_MEM_DATA,
	FW_CRASH_DUMP_PAGEABLE_DATA,
	FW_CRASH_DUMP_M3_DUMP,
	FW_CRASH_DUMP_NONE,

	/* keep last */
	FW_CRASH_DUMP_TYPE_MAX,
};

#define COREDUMP_TLV_HDR_SIZE 8

struct ath11k_tlv_dump_data {
	/* see ath11k_fw_crash_dump_type above */
	__le32 type;

	/* in bytes */
	__le32 tlv_len;

	/* pad to 32-bit boundaries as needed */
	u8 tlv_data[];
} __packed;

struct ath11k_dump_file_data {
	/* "ATH11K-FW-DUMP" */
	char df_magic[16];
	/* total dump len in bytes */
	__le32 len;
	/* file dump version */
	__le32 version;
	/* pci device id */
	__le32 chip_id;
	/* qrtr instance id */
	__le32 qrtr_id;
	/* pci domain id */
	__le32 bus_id;
	guid_t guid;
	/* time-of-day stamp */
	__le64 tv_sec;
	/* time-of-day stamp, nano-seconds */
	__le64 tv_nsec;
	/* room for growth w/out changing binary format */
	u8 unused[128];
	u8 data[];
} __packed;

#ifdef CONFIG_DEV_COREDUMP
enum ath11k_fw_crash_dump_type ath11k_coredump_get_dump_type(int type);
void ath11k_coredump_upload(struct work_struct *work);
void ath11k_coredump_collect(struct ath11k_base *ab);
#else
static inline enum
ath11k_fw_crash_dump_type ath11k_coredump_get_dump_type(int type)
{
	return FW_CRASH_DUMP_TYPE_MAX;
}

static inline void ath11k_coredump_upload(struct work_struct *work)
{
}

static inline void ath11k_coredump_collect(struct ath11k_base *ab)
{
}
#endif

#endif
