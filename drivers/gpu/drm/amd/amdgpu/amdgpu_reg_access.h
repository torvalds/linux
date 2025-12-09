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
 */

#ifndef __AMDGPU_REG_ACCESS_H__
#define __AMDGPU_REG_ACCESS_H__

#include <linux/types.h>
#include <linux/spinlock.h>

struct amdgpu_device;

typedef uint32_t (*amdgpu_rreg_t)(struct amdgpu_device *, uint32_t);
typedef void (*amdgpu_wreg_t)(struct amdgpu_device *, uint32_t, uint32_t);
typedef uint32_t (*amdgpu_rreg_ext_t)(struct amdgpu_device *, uint64_t);
typedef void (*amdgpu_wreg_ext_t)(struct amdgpu_device *, uint64_t, uint32_t);
typedef uint64_t (*amdgpu_rreg64_t)(struct amdgpu_device *, uint32_t);
typedef void (*amdgpu_wreg64_t)(struct amdgpu_device *, uint32_t, uint64_t);
typedef uint64_t (*amdgpu_rreg64_ext_t)(struct amdgpu_device *, uint64_t);
typedef void (*amdgpu_wreg64_ext_t)(struct amdgpu_device *, uint64_t, uint64_t);

typedef uint32_t (*amdgpu_block_rreg_t)(struct amdgpu_device *, uint32_t,
					uint32_t);
typedef void (*amdgpu_block_wreg_t)(struct amdgpu_device *, uint32_t, uint32_t,
				    uint32_t);

struct amdgpu_reg_ind {
	spinlock_t lock;
	amdgpu_rreg_t rreg;
	amdgpu_wreg_t wreg;
};

struct amdgpu_reg_ind_blk {
	spinlock_t lock;
	amdgpu_block_rreg_t rreg;
	amdgpu_block_wreg_t wreg;
};

struct amdgpu_reg_pcie_ind {
	spinlock_t lock;
	amdgpu_rreg_t rreg;
	amdgpu_wreg_t wreg;
	amdgpu_rreg_ext_t rreg_ext;
	amdgpu_wreg_ext_t wreg_ext;
	amdgpu_rreg64_t rreg64;
	amdgpu_wreg64_t wreg64;
	amdgpu_rreg64_ext_t rreg64_ext;
	amdgpu_wreg64_ext_t wreg64_ext;
	amdgpu_rreg_t port_rreg;
	amdgpu_wreg_t port_wreg;
};

struct amdgpu_reg_access {
	struct amdgpu_reg_ind smc;
	struct amdgpu_reg_ind uvd_ctx;
	struct amdgpu_reg_ind didt;
	struct amdgpu_reg_ind gc_cac;
	struct amdgpu_reg_ind se_cac;
	struct amdgpu_reg_ind_blk audio_endpt;
	struct amdgpu_reg_pcie_ind pcie;
};

void amdgpu_reg_access_init(struct amdgpu_device *adev);
uint32_t amdgpu_reg_smc_rd32(struct amdgpu_device *adev, uint32_t reg);
void amdgpu_reg_smc_wr32(struct amdgpu_device *adev, uint32_t reg, uint32_t v);
uint32_t amdgpu_reg_uvd_ctx_rd32(struct amdgpu_device *adev, uint32_t reg);
void amdgpu_reg_uvd_ctx_wr32(struct amdgpu_device *adev, uint32_t reg, uint32_t v);
uint32_t amdgpu_reg_didt_rd32(struct amdgpu_device *adev, uint32_t reg);
void amdgpu_reg_didt_wr32(struct amdgpu_device *adev, uint32_t reg, uint32_t v);
uint32_t amdgpu_reg_gc_cac_rd32(struct amdgpu_device *adev, uint32_t reg);
void amdgpu_reg_gc_cac_wr32(struct amdgpu_device *adev, uint32_t reg,
			    uint32_t v);
uint32_t amdgpu_reg_se_cac_rd32(struct amdgpu_device *adev, uint32_t reg);
void amdgpu_reg_se_cac_wr32(struct amdgpu_device *adev, uint32_t reg,
			    uint32_t v);
uint32_t amdgpu_reg_audio_endpt_rd32(struct amdgpu_device *adev, uint32_t block,
				     uint32_t reg);
void amdgpu_reg_audio_endpt_wr32(struct amdgpu_device *adev, uint32_t block,
				 uint32_t reg, uint32_t v);
uint32_t amdgpu_reg_pcie_rd32(struct amdgpu_device *adev, uint32_t reg);
void amdgpu_reg_pcie_wr32(struct amdgpu_device *adev, uint32_t reg, uint32_t v);
uint32_t amdgpu_reg_pcie_ext_rd32(struct amdgpu_device *adev, uint64_t reg);
void amdgpu_reg_pcie_ext_wr32(struct amdgpu_device *adev, uint64_t reg,
			      uint32_t v);
uint64_t amdgpu_reg_pcie_rd64(struct amdgpu_device *adev, uint32_t reg);
void amdgpu_reg_pcie_wr64(struct amdgpu_device *adev, uint32_t reg, uint64_t v);
uint64_t amdgpu_reg_pcie_ext_rd64(struct amdgpu_device *adev, uint64_t reg);
void amdgpu_reg_pcie_ext_wr64(struct amdgpu_device *adev, uint64_t reg,
			      uint64_t v);
uint32_t amdgpu_reg_pciep_rd32(struct amdgpu_device *adev, uint32_t reg);
void amdgpu_reg_pciep_wr32(struct amdgpu_device *adev, uint32_t reg,
			   uint32_t v);

uint32_t amdgpu_device_rreg(struct amdgpu_device *adev, uint32_t reg,
			    uint32_t acc_flags);
uint32_t amdgpu_device_xcc_rreg(struct amdgpu_device *adev, uint32_t reg,
				uint32_t acc_flags, uint32_t xcc_id);
void amdgpu_device_wreg(struct amdgpu_device *adev, uint32_t reg, uint32_t v,
			uint32_t acc_flags);
void amdgpu_device_xcc_wreg(struct amdgpu_device *adev, uint32_t reg,
			    uint32_t v, uint32_t acc_flags, uint32_t xcc_id);
void amdgpu_mm_wreg_mmio_rlc(struct amdgpu_device *adev, uint32_t reg,
			     uint32_t v, uint32_t xcc_id);
void amdgpu_mm_wreg8(struct amdgpu_device *adev, uint32_t offset,
		     uint8_t value);
uint8_t amdgpu_mm_rreg8(struct amdgpu_device *adev, uint32_t offset);

u32 amdgpu_device_indirect_rreg(struct amdgpu_device *adev, u32 reg_addr);
u32 amdgpu_device_indirect_rreg_ext(struct amdgpu_device *adev, u64 reg_addr);
u64 amdgpu_device_indirect_rreg64(struct amdgpu_device *adev, u32 reg_addr);
u64 amdgpu_device_indirect_rreg64_ext(struct amdgpu_device *adev, u64 reg_addr);
void amdgpu_device_indirect_wreg(struct amdgpu_device *adev, u32 reg_addr,
				 u32 reg_data);
void amdgpu_device_indirect_wreg_ext(struct amdgpu_device *adev, u64 reg_addr,
				     u32 reg_data);
void amdgpu_device_indirect_wreg64(struct amdgpu_device *adev, u32 reg_addr,
				   u64 reg_data);
void amdgpu_device_indirect_wreg64_ext(struct amdgpu_device *adev, u64 reg_addr,
				       u64 reg_data);

u32 amdgpu_device_pcie_port_rreg(struct amdgpu_device *adev, u32 reg);
void amdgpu_device_pcie_port_wreg(struct amdgpu_device *adev, u32 reg, u32 v);

uint32_t amdgpu_device_wait_on_rreg(struct amdgpu_device *adev, uint32_t inst,
				    uint32_t reg_addr, char reg_name[],
				    uint32_t expected_value, uint32_t mask);

#endif /* __AMDGPU_REG_ACCESS_H__ */
