/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef __SOC15_H__
#define __SOC15_H__

#include "nbio_v6_1.h"
#include "nbio_v7_0.h"
#include "nbio_v7_4.h"

extern const struct amdgpu_ip_block_version vega10_common_ip_block;

#define SOC15_FLUSH_GPU_TLB_NUM_WREG		6
#define SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT	3

struct soc15_reg_golden {
	u32	hwip;
	u32	instance;
	u32	segment;
	u32	reg;
	u32	and_mask;
	u32	or_mask;
};

struct soc15_reg_rlcg {
	u32	hwip;
	u32	instance;
	u32	segment;
	u32	reg;
};

struct soc15_reg {
	uint32_t hwip;
	uint32_t inst;
	uint32_t seg;
	uint32_t reg_offset;
};

struct soc15_reg_entry {
	uint32_t hwip;
	uint32_t inst;
	uint32_t seg;
	uint32_t reg_offset;
	uint32_t reg_value;
	uint32_t se_num;
	uint32_t instance;
};

struct soc15_allowed_register_entry {
	uint32_t hwip;
	uint32_t inst;
	uint32_t seg;
	uint32_t reg_offset;
	bool grbm_indexed;
};

struct soc15_ras_field_entry {
	const char *name;
	uint32_t hwip;
	uint32_t inst;
	uint32_t seg;
	uint32_t reg_offset;
	uint32_t sec_count_mask;
	uint32_t sec_count_shift;
	uint32_t ded_count_mask;
	uint32_t ded_count_shift;
};

#define SOC15_REG_ENTRY(ip, inst, reg)	ip##_HWIP, inst, reg##_BASE_IDX, reg

#define SOC15_REG_ENTRY_OFFSET(entry)	(adev->reg_offset[entry.hwip][entry.inst][entry.seg] + entry.reg_offset)

#define SOC15_REG_GOLDEN_VALUE(ip, inst, reg, and_mask, or_mask) \
	{ ip##_HWIP, inst, reg##_BASE_IDX, reg, and_mask, or_mask }

#define SOC15_REG_FIELD(reg, field) reg##__##field##_MASK, reg##__##field##__SHIFT

#define SOC15_REG_FIELD_VAL(val, mask, shift)	(((val) & mask) >> shift)

#define SOC15_RAS_REG_FIELD_VAL(val, entry, field) SOC15_REG_FIELD_VAL((val), (entry).field##_count_mask, (entry).field##_count_shift)

void soc15_grbm_select(struct amdgpu_device *adev,
		    u32 me, u32 pipe, u32 queue, u32 vmid, int xcc_id);
void soc15_set_virt_ops(struct amdgpu_device *adev);

void soc15_program_register_sequence(struct amdgpu_device *adev,
					     const struct soc15_reg_golden *registers,
					     const u32 array_size);

int vega10_reg_base_init(struct amdgpu_device *adev);
int vega20_reg_base_init(struct amdgpu_device *adev);
int arct_reg_base_init(struct amdgpu_device *adev);
int aldebaran_reg_base_init(struct amdgpu_device *adev);
void aqua_vanjaram_ip_map_init(struct amdgpu_device *adev);
u64 aqua_vanjaram_encode_ext_smn_addressing(int ext_id);
int aqua_vanjaram_init_soc_config(struct amdgpu_device *adev);

void vega10_doorbell_index_init(struct amdgpu_device *adev);
void vega20_doorbell_index_init(struct amdgpu_device *adev);
void aqua_vanjaram_doorbell_index_init(struct amdgpu_device *adev);
#endif
