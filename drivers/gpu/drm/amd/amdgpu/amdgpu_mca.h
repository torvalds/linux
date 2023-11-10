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

enum amdgpu_mca_ip {
	AMDGPU_MCA_IP_UNKNOW = -1,
	AMDGPU_MCA_IP_PSP = 0,
	AMDGPU_MCA_IP_SDMA,
	AMDGPU_MCA_IP_GC,
	AMDGPU_MCA_IP_SMU,
	AMDGPU_MCA_IP_MP5,
	AMDGPU_MCA_IP_UMC,
	AMDGPU_MCA_IP_COUNT,
};

enum amdgpu_mca_error_type {
	AMDGPU_MCA_ERROR_TYPE_UE = 0,
	AMDGPU_MCA_ERROR_TYPE_CE,
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

struct amdgpu_mca_smu_funcs {
	int max_ue_count;
	int max_ce_count;
	int (*mca_set_debug_mode)(struct amdgpu_device *adev, bool enable);
	int (*mca_get_error_count)(struct amdgpu_device *adev, enum amdgpu_ras_block blk,
				   enum amdgpu_mca_error_type type, uint32_t *count);
	int (*mca_get_valid_mca_count)(struct amdgpu_device *adev, enum amdgpu_mca_error_type type,
				       uint32_t *count);
	int (*mca_get_mca_entry)(struct amdgpu_device *adev, enum amdgpu_mca_error_type type,
				 int idx, struct mca_bank_entry *entry);
	int (*mca_get_ras_mca_idx_array)(struct amdgpu_device *adev, enum amdgpu_ras_block blk,
					 enum amdgpu_mca_error_type type, int *idx_array, int *idx_array_size);
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
int amdgpu_mca_smu_get_error_count(struct amdgpu_device *adev, enum amdgpu_ras_block blk,
				   enum amdgpu_mca_error_type type, uint32_t *count);
int amdgpu_mca_smu_get_mca_entry(struct amdgpu_device *adev, enum amdgpu_mca_error_type type,
				 int idx, struct mca_bank_entry *entry);

void amdgpu_mca_smu_debugfs_init(struct amdgpu_device *adev, struct dentry *root);

#endif
