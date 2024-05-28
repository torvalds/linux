/*
 * Copyright (C) 2021  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __AMDGPU_MCA_H__
#define __AMDGPU_MCA_H__

#include "amdgpu_ras.h"

#define MCA_MAX_REGS_COUNT	(16)

#define MCA_REG_FIELD(x, h, l)			(((x) & GENMASK_ULL(h, l)) >> l)
#define MCA_REG__STATUS__VAL(x)			MCA_REG_FIELD(x, 63, 63)
#define MCA_REG__STATUS__OVERFLOW(x)		MCA_REG_FIELD(x, 62, 62)
#define MCA_REG__STATUS__UC(x)			MCA_REG_FIELD(x, 61, 61)
#define MCA_REG__STATUS__EN(x)			MCA_REG_FIELD(x, 60, 60)
#define MCA_REG__STATUS__MISCV(x)		MCA_REG_FIELD(x, 59, 59)
#define MCA_REG__STATUS__ADDRV(x)		MCA_REG_FIELD(x, 58, 58)
#define MCA_REG__STATUS__PCC(x)			MCA_REG_FIELD(x, 57, 57)
#define MCA_REG__STATUS__ERRCOREIDVAL(x)	MCA_REG_FIELD(x, 56, 56)
#define MCA_REG__STATUS__TCC(x)			MCA_REG_FIELD(x, 55, 55)
#define MCA_REG__STATUS__SYNDV(x)		MCA_REG_FIELD(x, 53, 53)
#define MCA_REG__STATUS__CECC(x)		MCA_REG_FIELD(x, 46, 46)
#define MCA_REG__STATUS__UECC(x)		MCA_REG_FIELD(x, 45, 45)
#define MCA_REG__STATUS__DEFERRED(x)		MCA_REG_FIELD(x, 44, 44)
#define MCA_REG__STATUS__POISON(x)		MCA_REG_FIELD(x, 43, 43)
#define MCA_REG__STATUS__SCRUB(x)		MCA_REG_FIELD(x, 40, 40)
#define MCA_REG__STATUS__ERRCOREID(x)		MCA_REG_FIELD(x, 37, 32)
#define MCA_REG__STATUS__ADDRLSB(x)		MCA_REG_FIELD(x, 29, 24)
#define MCA_REG__STATUS__ERRORCODEEXT(x)	MCA_REG_FIELD(x, 21, 16)
#define MCA_REG__STATUS__ERRORCODE(x)		MCA_REG_FIELD(x, 15, 0)

#define MCA_REG__MISC0__ERRCNT(x)		MCA_REG_FIELD(x, 43, 32)

#define MCA_REG__SYND__ERRORINFORMATION(x)	MCA_REG_FIELD(x, 17, 0)

enum amdgpu_mca_ip {
	AMDGPU_MCA_IP_UNKNOW = -1,
	AMDGPU_MCA_IP_PSP = 0,
	AMDGPU_MCA_IP_SDMA,
	AMDGPU_MCA_IP_GC,
	AMDGPU_MCA_IP_SMU,
	AMDGPU_MCA_IP_MP5,
	AMDGPU_MCA_IP_UMC,
	AMDGPU_MCA_IP_PCS_XGMI,
	AMDGPU_MCA_IP_COUNT,
};

enum amdgpu_mca_error_type {
	AMDGPU_MCA_ERROR_TYPE_UE = 0,
	AMDGPU_MCA_ERROR_TYPE_CE,
	AMDGPU_MCA_ERROR_TYPE_DE,
};

struct amdgpu_mca_ras_block {
	struct amdgpu_ras_block_object ras_block;
};

struct amdgpu_mca_ras {
	struct ras_common_if *ras_if;
	struct amdgpu_mca_ras_block *ras;
};

struct amdgpu_mca {
	struct amdgpu_mca_ras mp0;
	struct amdgpu_mca_ras mp1;
	struct amdgpu_mca_ras mpio;
	const struct amdgpu_mca_smu_funcs *mca_funcs;
};

enum mca_reg_idx {
	MCA_REG_IDX_STATUS		= 1,
	MCA_REG_IDX_ADDR		= 2,
	MCA_REG_IDX_MISC0		= 3,
	MCA_REG_IDX_IPID		= 5,
	MCA_REG_IDX_SYND		= 6,
	MCA_REG_IDX_COUNT		= 16,
};

struct mca_bank_info {
	int socket_id;
	int aid;
	int hwid;
	int mcatype;
};

struct mca_bank_entry {
	int idx;
	enum amdgpu_mca_error_type type;
	enum amdgpu_mca_ip ip;
	struct mca_bank_info info;
	uint64_t regs[MCA_MAX_REGS_COUNT];
};

struct mca_bank_node {
	struct mca_bank_entry entry;
	struct list_head node;
};

struct mca_bank_set {
	int nr_entries;
	struct list_head list;
};

struct amdgpu_mca_smu_funcs {
	int max_ue_count;
	int max_ce_count;
	int (*mca_set_debug_mode)(struct amdgpu_device *adev, bool enable);
	int (*mca_get_ras_mca_set)(struct amdgpu_device *adev, enum amdgpu_ras_block blk, enum amdgpu_mca_error_type type,
				   struct mca_bank_set *mca_set);
	int (*mca_parse_mca_error_count)(struct amdgpu_device *adev, enum amdgpu_ras_block blk, enum amdgpu_mca_error_type type,
					 struct mca_bank_entry *entry, uint32_t *count);
	int (*mca_get_valid_mca_count)(struct amdgpu_device *adev, enum amdgpu_mca_error_type type,
				       uint32_t *count);
	int (*mca_get_mca_entry)(struct amdgpu_device *adev, enum amdgpu_mca_error_type type,
				 int idx, struct mca_bank_entry *entry);
};

void amdgpu_mca_query_correctable_error_count(struct amdgpu_device *adev,
					      uint64_t mc_status_addr,
					      unsigned long *error_count);

void amdgpu_mca_query_uncorrectable_error_count(struct amdgpu_device *adev,
						uint64_t mc_status_addr,
						unsigned long *error_count);

void amdgpu_mca_reset_error_count(struct amdgpu_device *adev,
				  uint64_t mc_status_addr);

void amdgpu_mca_query_ras_error_count(struct amdgpu_device *adev,
				      uint64_t mc_status_addr,
				      void *ras_error_status);
int amdgpu_mca_mp0_ras_sw_init(struct amdgpu_device *adev);
int amdgpu_mca_mp1_ras_sw_init(struct amdgpu_device *adev);
int amdgpu_mca_mpio_ras_sw_init(struct amdgpu_device *adev);

void amdgpu_mca_smu_init_funcs(struct amdgpu_device *adev, const struct amdgpu_mca_smu_funcs *mca_funcs);
int amdgpu_mca_smu_set_debug_mode(struct amdgpu_device *adev, bool enable);
int amdgpu_mca_smu_get_valid_mca_count(struct amdgpu_device *adev, enum amdgpu_mca_error_type type, uint32_t *count);
int amdgpu_mca_smu_get_mca_set_error_count(struct amdgpu_device *adev, enum amdgpu_ras_block blk,
					   enum amdgpu_mca_error_type type, uint32_t *total);
int amdgpu_mca_smu_get_error_count(struct amdgpu_device *adev, enum amdgpu_ras_block blk,
				   enum amdgpu_mca_error_type type, uint32_t *count);
int amdgpu_mca_smu_parse_mca_error_count(struct amdgpu_device *adev, enum amdgpu_ras_block blk,
					 enum amdgpu_mca_error_type type, struct mca_bank_entry *entry, uint32_t *count);
int amdgpu_mca_smu_get_mca_set(struct amdgpu_device *adev, enum amdgpu_ras_block blk,
			       enum amdgpu_mca_error_type type, struct mca_bank_set *mca_set);
int amdgpu_mca_smu_get_mca_entry(struct amdgpu_device *adev, enum amdgpu_mca_error_type type,
				 int idx, struct mca_bank_entry *entry);

void amdgpu_mca_smu_debugfs_init(struct amdgpu_device *adev, struct dentry *root);

void amdgpu_mca_bank_set_init(struct mca_bank_set *mca_set);
int amdgpu_mca_bank_set_add_entry(struct mca_bank_set *mca_set, struct mca_bank_entry *entry);
void amdgpu_mca_bank_set_release(struct mca_bank_set *mca_set);
int amdgpu_mca_smu_log_ras_error(struct amdgpu_device *adev, enum amdgpu_ras_block blk, enum amdgpu_mca_error_type type, struct ras_err_data *err_data);

#endif
