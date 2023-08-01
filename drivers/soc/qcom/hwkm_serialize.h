/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __HWKM_SERIALIZE_H_
#define __HWKM_SERIALIZE_H_

#include <linux/hwkm.h>

/* Command lengths (words) */
#define NIST_KEYGEN_CMD_WORDS 4
#define SYSTEM_KDF_CMD_MIN_WORDS 4
#define SYSTEM_KDF_CMD_MAX_WORDS 29
#define KEYSLOT_CLEAR_CMD_WORDS 2
#define WRAP_EXPORT_CMD_WORDS 5
#define SET_TPKEY_CMD_WORDS 2
#define QFPROM_RDWR_CMD_WORDS 2

/* Response lengths (words) */
#define NIST_KEYGEN_RSP_WORDS 2
#define SYSTEM_KDF_RSP_WORDS 2
#define KEYSLOT_CLEAR_RSP_WORDS 2
#define UNWRAP_IMPORT_RSP_WORDS 2
#define WRAP_EXPORT_RSP_WORDS 19
#define SET_TPKEY_RSP_WORDS 2
#define QFPROM_RDWR_RSP_WORDS 2

/* Field lengths (words) */
#define OPERATION_INFO_WORDS 1
#define KEY_POLICY_WORDS 2
#define BSVE_WORDS 3
#define MAX_SWC_WORDS 16
#define KEY_BLOB_WORDS 17

#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
#define UNWRAP_IMPORT_CMD_WORDS 25	/* Command lengths (words) */
#define KEYSLOT_RDWR_CMD_WORDS 20	/* Command lengths (words) */
#define KEYSLOT_RDWR_RSP_WORDS 21	/* Response lengths (words) */
#define RESPONSE_KEY_WORDS 16		/* Field lengths (words) */
#endif
#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1)
#define UNWRAP_IMPORT_CMD_WORDS 19	/* Command lengths (words) */
#define KEYSLOT_RDWR_CMD_WORDS 12	/* Command lengths (words) */
#define KEYSLOT_RDWR_RSP_WORDS 12	/* Response lengths (words) */
#define RESPONSE_KEY_WORDS 8		/* Field lengths (words) */
#endif

/* Field lengths (bytes) */
#define UNWRAP_CMD_LENGTH (UNWRAP_IMPORT_CMD_WORDS * sizeof(uint32_t))
#define UNWRAP_RSP_LENGTH (UNWRAP_IMPORT_RSP_WORDS * sizeof(uint32_t))
#define OPERATION_INFO_LENGTH (OPERATION_INFO_WORDS * sizeof(uint32_t))
#define KEY_POLICY_LENGTH (KEY_POLICY_WORDS * sizeof(uint32_t))
#define MAX_BSVE_LENGTH (BSVE_WORDS * sizeof(uint32_t))
#define MAX_SWC_LENGTH (MAX_SWC_WORDS * sizeof(uint32_t))
#define RESPONSE_KEY_LENGTH (RESPONSE_KEY_WORDS * sizeof(uint32_t))
#define KEY_BLOB_LENGTH (KEY_BLOB_WORDS * sizeof(uint32_t))

/* Command indices */
#define COMMAND_KEY_POLICY_IDX 1
#define COMMAND_KEY_VALUE_IDX 3
#define COMMAND_WRAPPED_KEY_IDX 1
#define COMMAND_KEY_WRAP_BSVE_IDX 1

/* Response indices */
#define RESPONSE_ERR_IDX 1
#define RESPONSE_KEY_POLICY_IDX 2
#define RESPONSE_KEY_VALUE_IDX 4
#define RESPONSE_WRAPPED_KEY_IDX 2

struct hwkm_serialized_policy {
	unsigned dbg_qfprom_key_rd_iv_sel:1;		// [0]
	unsigned reserved0:1;				// [1]
	unsigned wrap_with_tpkey:1;			// [2]
	unsigned hw_destination:4;			// [3:6]
	unsigned reserved1:1;				// [7]
	unsigned propagate_sec_level_to_child_keys:1;	// [8]
	unsigned security_level:2;			// [9:10]
	unsigned swap_export_allowed:1;			// [11]
	unsigned wrap_export_allowed:1;			// [12]
	unsigned key_type:3;				// [13:15]
	unsigned kdf_depth:8;				// [16:23]
	unsigned decrypt_allowed:1;			// [24]
	unsigned encrypt_allowed:1;			// [25]
	unsigned alg_allowed:6;				// [26:31]
	unsigned key_management_by_tz_secure_allowed:1;	// [32]
	unsigned key_management_by_nonsecure_allowed:1;	// [33]
	unsigned key_management_by_modem_allowed:1;	// [34]
	unsigned key_management_by_spu_allowed:1;	// [35]
	unsigned reserved2:28;				// [36:63]
} __packed;

struct hwkm_kdf_bsve {
	unsigned mks:8;				// [0:7]
	unsigned key_policy_version_en:1;	// [8]
	unsigned apps_secure_en:1;		// [9]
	unsigned msa_secure_en:1;		// [10]
	unsigned lcm_fuse_row_en:1;		// [11]
	unsigned boot_stage_otp_en:1;		// [12]
	unsigned swc_en:1;			// [13]
	u64 fuse_region_sha_digest_en:64;	// [14:78]
	unsigned child_key_policy_en:1;		// [79]
	unsigned mks_en:1;			// [80]
	unsigned reserved:16;			// [81:95]
} __packed;

struct hwkm_wrapping_bsve {
	unsigned key_policy_version_en:1;      // [0]
	unsigned apps_secure_en:1;             // [1]
	unsigned msa_secure_en:1;              // [2]
	unsigned lcm_fuse_row_en:1;            // [3]
	unsigned boot_stage_otp_en:1;          // [4]
	unsigned swc_en:1;                     // [5]
	u64 fuse_region_sha_digest_en:64; // [6:69]
	unsigned child_key_policy_en:1;        // [70]
	unsigned mks_en:1;                     // [71]
	unsigned reserved:24;                  // [72:95]
} __packed;

struct hwkm_operation_info {
	unsigned op:4;		// [0-3]
	unsigned irq_en:1;	// [4]
	unsigned slot1_desc:8;	// [5,12]
	unsigned slot2_desc:8;	// [13,20]
	unsigned op_flag:1;	// [21]
	unsigned context_len:5;	// [22-26]
	unsigned len:5;		// [27-31]
} __packed;

#endif /* __HWKM_SERIALIZE_H_ */
