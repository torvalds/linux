/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef __RAS_MP1_H__
#define __RAS_MP1_H__
#include "ras.h"

#define MAX_UE_BANKS_PER_QUERY  12
#define MAX_CE_BANKS_PER_QUERY  12

enum ras_mp1_feature_id {
	RAS_MP1_FEATURE_PMFW_EEPROM,
	RAS_MP1_FEATURE_MAX
};

enum ras_mp1_msg_id {
	RAS_MP1_MSG_QueryValidMcaCount,
	RAS_MP1_MSG_McaBankDumpDW,
	RAS_MP1_MSG_ClearMcaOnRead,
	RAS_MP1_MSG_QueryValidMcaCeCount,
	RAS_MP1_MSG_McaBankCeDumpDW,
	RAS_MP1_MSG_GetRasTableVersion,
	RAS_MP1_MSG_GetRmaStatus,
	RAS_MP1_MSG_GetBadPageCount,
	RAS_MP1_MSG_GetBadPageMcaAddr,
	RAS_MP1_MSG_GetBadPagePaAddr,
	RAS_MP1_MSG_SetTimestamp,
	RAS_MP1_MSG_GetTimestamp,
	RAS_MP1_MSG_GetRasPolicy,
	RAS_MP1_MSG_GetBadPageIpId,
	RAS_MP1_MSG_EraseRasTable,
	RAS_MP1_MSG_MAX
};

struct eeprom_err_record {
	union {
		struct {
			u32 timestamp_low;
			u32 timestamp_high;
		};
		u64 timestamp;
	};

	union {
		struct {
			u32 mca_addr_low;
			u32 mca_addr_high;
		};
		u64 mca_addr;
	};

	union {
		struct {
			u32 ipid_low;
			u32 ipid_high;
		};
		u64 ipid;
	};
};

enum ras_err_type;
struct ras_mp1_ip_func {
	int (*get_valid_bank_count)(struct ras_core_context *ras_core,
			enum ras_err_type type, u32 *count);
	int (*dump_valid_bank)(struct ras_core_context *ras_core,
		enum ras_err_type type, u32 idx, u64 *regs, u32 regs_sz);
	int (*set_debug_mode)(struct ras_core_context *ras_core, bool enable);

	/* The following is used for firmware management of EEPROM */
	int (*get_table_version)(struct ras_core_context *ras_core,
			u32 *table_version);
	bool (*rma_detected)(struct ras_core_context *ras_core);
	int (*set_timestamp)(struct ras_core_context *ras_core,
			u64 timestamp);
	int (*reset_ras_table)(struct ras_core_context *ras_core,
			u32 *result);
	int (*get_record_count)(struct ras_core_context *ras_core,
			u32 *count);
	int (*get_record)(struct ras_core_context *ras_core,
			u32 idx, struct eeprom_err_record *rec);
};

struct ras_mp1 {
	uint32_t mp1_ip_version;
	const struct ras_mp1_ip_func *ip_func;
	const struct ras_mp1_sys_func *sys_func;
	struct mutex op_mutex;
};

int ras_mp1_sw_init(struct ras_core_context *ras_core);
int ras_mp1_sw_fini(struct ras_core_context *ras_core);

int ras_mp1_get_bank_count(struct ras_core_context *ras_core,
			    enum ras_err_type type, u32 *count);

int ras_mp1_dump_bank(struct ras_core_context *ras_core,
		u32 ecc_type, u32 idx, u64 *regs, u32 regs_sz);

int ras_mp1_set_debug_mode(struct ras_core_context *ras_core, bool enable);
int ras_mp1_get_table_version(struct ras_core_context *ras_core,
		u32 *table_version);
bool ras_mp1_rma_detected(struct ras_core_context *ras_core);
int ras_mp1_set_timestamp(struct ras_core_context *ras_core,
		u64 timestamp);
int ras_mp1_reset_ras_table(struct ras_core_context *ras_core,
		u32 *result);
int ras_mp1_get_record_count(struct ras_core_context *ras_core, u32 *count);
int ras_mp1_get_record(struct ras_core_context *ras_core,
		u32 idx, struct eeprom_err_record *rec);
int ras_mp1_check_feature_status(struct ras_core_context *ras_core,
		enum ras_mp1_feature_id feature_id, u32 *status);
#endif
