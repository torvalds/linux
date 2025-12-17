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

#ifndef __RAS_ACA_H__
#define __RAS_ACA_H__
#include "ras.h"

#define MAX_SOCKET_NUM_PER_HIVE 8
#define MAX_AID_NUM_PER_SOCKET 4
#define MAX_XCD_NUM_PER_AID 2
#define MAX_ACA_RAS_BLOCK  20

#define ACA_ERROR__UE_MASK			(0x1 << RAS_ERR_TYPE__UE)
#define ACA_ERROR__CE_MASK			(0x1 << RAS_ERR_TYPE__CE)
#define ACA_ERROR__DE_MASK			(0x1 << RAS_ERR_TYPE__DE)

enum ras_aca_reg_idx {
	ACA_REG_IDX__CTL		= 0,
	ACA_REG_IDX__STATUS		= 1,
	ACA_REG_IDX__ADDR		= 2,
	ACA_REG_IDX__MISC0		= 3,
	ACA_REG_IDX__CONFG		= 4,
	ACA_REG_IDX__IPID		= 5,
	ACA_REG_IDX__SYND		= 6,
	ACA_REG_IDX__DESTAT		= 8,
	ACA_REG_IDX__DEADDR		= 9,
	ACA_REG_IDX__CTL_MASK	= 10,
	ACA_REG_MAX_COUNT		= 16,
};

struct ras_core_context;
struct aca_block;

struct aca_bank_reg {
	u32 ecc_type;
	u64 seq_no;
	u64 regs[ACA_REG_MAX_COUNT];
};

enum aca_ecc_hwip {
	ACA_ECC_HWIP__UNKNOWN = -1,
	ACA_ECC_HWIP__PSP = 0,
	ACA_ECC_HWIP__UMC,
	ACA_ECC_HWIP__SMU,
	ACA_ECC_HWIP__PCS_XGMI,
	ACA_ECC_HWIP_COUNT,
};

struct aca_ecc_info {
	int die_id;
	int socket_id;
	int xcd_id;
	int hwid;
	int mcatype;
	uint64_t status;
	uint64_t ipid;
	uint64_t addr;
};

struct aca_bank_ecc {
	struct aca_ecc_info bank_info;
	u32 ce_count;
	u32 ue_count;
	u32 de_count;
};

struct aca_ecc_count {
	u32 new_ce_count;
	u32 total_ce_count;
	u32 new_ue_count;
	u32 total_ue_count;
	u32 new_de_count;
	u32 total_de_count;
};

struct aca_xcd_ecc {
	struct aca_ecc_count ecc_err;
};

struct aca_aid_ecc {
	union {
		struct aca_xcd {
			struct aca_xcd_ecc xcd[MAX_XCD_NUM_PER_AID];
			u32 xcd_num;
		} xcd;
		struct aca_ecc_count ecc_err;
	};
};

struct aca_socket_ecc {
	struct aca_aid_ecc aid[MAX_AID_NUM_PER_SOCKET];
	u32 aid_num;
};

struct aca_block_ecc {
	struct aca_socket_ecc socket[MAX_SOCKET_NUM_PER_HIVE];
	u32 socket_num_per_hive;
};

struct aca_bank_hw_ops {
	bool (*bank_match)(struct aca_block *ras_blk, void *data);
	int (*bank_parse)(struct ras_core_context *ras_core,
			struct aca_block *aca_blk, void *data, void *buf);
};

struct aca_block_info {
	char name[32];
	u32 ras_block_id;
	enum aca_ecc_hwip hwip;
	struct aca_bank_hw_ops bank_ops;
	u32 mask;
};

struct aca_block {
	const struct aca_block_info  *blk_info;
	struct aca_block_ecc ecc;
};

struct ras_aca_ip_func {
	uint32_t block_num;
	const struct aca_block_info **block_info;
};

struct ras_aca {
	uint32_t aca_ip_version;
	const struct ras_aca_ip_func *ip_func;
	struct mutex  aca_lock;
	struct mutex  bank_op_lock;
	struct aca_block aca_blk[MAX_ACA_RAS_BLOCK];
	uint32_t ue_updated_mark;
};

int ras_aca_sw_init(struct ras_core_context *ras_core);
int ras_aca_sw_fini(struct ras_core_context *ras_core);
int ras_aca_hw_init(struct ras_core_context *ras_core);
int ras_aca_hw_fini(struct ras_core_context *ras_core);
int ras_aca_get_block_ecc_count(struct ras_core_context *ras_core, u32 blk, void *data);
int ras_aca_clear_block_new_ecc_count(struct ras_core_context *ras_core, u32 blk);
int ras_aca_clear_all_blocks_ecc_count(struct ras_core_context *ras_core);
int ras_aca_update_ecc(struct ras_core_context *ras_core, u32 ecc_type, void *data);
void ras_aca_mark_fatal_flag(struct ras_core_context *ras_core);
void ras_aca_clear_fatal_flag(struct ras_core_context *ras_core);
#endif
