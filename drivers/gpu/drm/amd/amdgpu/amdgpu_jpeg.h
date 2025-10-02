/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_JPEG_H__
#define __AMDGPU_JPEG_H__

#include "amdgpu_ras.h"
#include "amdgpu_cs.h"

#define AMDGPU_MAX_JPEG_INSTANCES	4
#define AMDGPU_MAX_JPEG_RINGS           10
#define AMDGPU_MAX_JPEG_RINGS_4_0_3     8

#define JPEG_REG_RANGE_START            0x4000
#define JPEG_REG_RANGE_END              0x41c2
#define JPEG_ATOMIC_RANGE_START         0x4120
#define JPEG_ATOMIC_RANGE_END           0x412A


#define AMDGPU_JPEG_HARVEST_JPEG0 (1 << 0)
#define AMDGPU_JPEG_HARVEST_JPEG1 (1 << 1)

#define WREG32_SOC15_JPEG_DPG_MODE(inst_idx, offset, value, indirect)			\
	do {										\
		if (!indirect) {							\
			WREG32_SOC15(JPEG, GET_INST(JPEG, inst_idx),			\
				     mmUVD_DPG_LMA_DATA, value);			\
			WREG32_SOC15(							\
				JPEG, GET_INST(JPEG, inst_idx),				\
				mmUVD_DPG_LMA_CTL,					\
				(UVD_DPG_LMA_CTL__READ_WRITE_MASK |			\
				 offset << UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT |	\
				 indirect << UVD_DPG_LMA_CTL__SRAM_SEL__SHIFT));	\
		} else {								\
			*adev->jpeg.inst[inst_idx].dpg_sram_curr_addr++ =		\
				offset;							\
			*adev->jpeg.inst[inst_idx].dpg_sram_curr_addr++ =		\
				value;							\
		}									\
	} while (0)

#define RREG32_SOC15_JPEG_DPG_MODE(inst_idx, offset, mask_en)					\
	({											\
		WREG32_SOC15(JPEG, inst_idx, mmUVD_DPG_LMA_CTL,					\
			(0x0 << UVD_DPG_LMA_CTL__READ_WRITE__SHIFT |				\
			mask_en << UVD_DPG_LMA_CTL__MASK_EN__SHIFT |				\
			offset << UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT));			\
		RREG32_SOC15(JPEG, inst_idx, mmUVD_DPG_LMA_DATA);				\
	})

#define WREG32_SOC24_JPEG_DPG_MODE(inst_idx, offset, value, indirect)		\
	do {									\
		WREG32_SOC15(JPEG, GET_INST(JPEG, inst_idx),			\
			     regUVD_DPG_LMA_DATA, value);			\
		WREG32_SOC15(JPEG, GET_INST(JPEG, inst_idx),			\
			     regUVD_DPG_LMA_MASK, 0xFFFFFFFF);			\
		WREG32_SOC15(							\
			JPEG, GET_INST(JPEG, inst_idx),				\
			regUVD_DPG_LMA_CTL,					\
			(UVD_DPG_LMA_CTL__READ_WRITE_MASK |			\
			 offset << UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT |	\
			 indirect << UVD_DPG_LMA_CTL__SRAM_SEL__SHIFT));	\
	} while (0)

#define RREG32_SOC24_JPEG_DPG_MODE(inst_idx, offset, mask_en)			\
	do {									\
		WREG32_SOC15(JPEG, GET_INST(JPEG, inst_idx),			\
			regUVD_DPG_LMA_MASK, 0xFFFFFFFF);			\
		WREG32_SOC15(JPEG, GET_INST(JPEG, inst_idx),			\
			regUVD_DPG_LMA_CTL,					\
			(UVD_DPG_LMA_CTL__MASK_EN_MASK |			\
			offset << UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT));	\
		RREG32_SOC15(JPEG, inst_idx, regUVD_DPG_LMA_DATA);		\
	} while (0)

#define ADD_SOC24_JPEG_TO_DPG_SRAM(inst_idx, offset, value, indirect)		\
	do {									\
		*adev->jpeg.inst[inst_idx].dpg_sram_curr_addr++ = offset;	\
		*adev->jpeg.inst[inst_idx].dpg_sram_curr_addr++ = value;	\
	} while (0)

struct amdgpu_hwip_reg_entry;

enum amdgpu_jpeg_caps {
	AMDGPU_JPEG_RRMT_ENABLED,
};

#define AMDGPU_JPEG_CAPS(caps) BIT(AMDGPU_JPEG_##caps)

struct amdgpu_jpeg_reg{
	unsigned jpeg_pitch[AMDGPU_MAX_JPEG_RINGS];
};

struct amdgpu_jpeg_inst {
	struct amdgpu_ring ring_dec[AMDGPU_MAX_JPEG_RINGS];
	struct amdgpu_irq_src irq;
	struct amdgpu_irq_src ras_poison_irq;
	struct amdgpu_jpeg_reg external;
	struct amdgpu_bo	*dpg_sram_bo;
	struct dpg_pause_state	pause_state;
	void			*dpg_sram_cpu_addr;
	uint64_t		dpg_sram_gpu_addr;
	uint32_t		*dpg_sram_curr_addr;
	uint8_t aid_id;
};

struct amdgpu_jpeg_ras {
	struct amdgpu_ras_block_object ras_block;
};

struct amdgpu_jpeg {
	uint8_t	num_jpeg_inst;
	struct amdgpu_jpeg_inst inst[AMDGPU_MAX_JPEG_INSTANCES];
	unsigned num_jpeg_rings;
	struct amdgpu_jpeg_reg internal;
	unsigned harvest_config;
	struct delayed_work idle_work;
	enum amd_powergating_state cur_state;
	struct mutex jpeg_pg_lock;
	atomic_t total_submission_cnt;
	struct ras_common_if	*ras_if;
	struct amdgpu_jpeg_ras	*ras;

	uint16_t inst_mask;
	uint8_t num_inst_per_aid;
	bool	indirect_sram;
	uint32_t supported_reset;
	uint32_t caps;
	u32 *ip_dump;
	u32 reg_count;
	const struct amdgpu_hwip_reg_entry *reg_list;
};

int amdgpu_jpeg_sw_init(struct amdgpu_device *adev);
int amdgpu_jpeg_sw_fini(struct amdgpu_device *adev);
int amdgpu_jpeg_suspend(struct amdgpu_device *adev);
int amdgpu_jpeg_resume(struct amdgpu_device *adev);

void amdgpu_jpeg_ring_begin_use(struct amdgpu_ring *ring);
void amdgpu_jpeg_ring_end_use(struct amdgpu_ring *ring);

int amdgpu_jpeg_dec_ring_test_ring(struct amdgpu_ring *ring);
int amdgpu_jpeg_dec_ring_test_ib(struct amdgpu_ring *ring, long timeout);

int amdgpu_jpeg_process_poison_irq(struct amdgpu_device *adev,
				struct amdgpu_irq_src *source,
				struct amdgpu_iv_entry *entry);
int amdgpu_jpeg_ras_late_init(struct amdgpu_device *adev,
				struct ras_common_if *ras_block);
int amdgpu_jpeg_ras_sw_init(struct amdgpu_device *adev);
int amdgpu_jpeg_psp_update_sram(struct amdgpu_device *adev, int inst_idx,
			       enum AMDGPU_UCODE_ID ucode_id);
void amdgpu_debugfs_jpeg_sched_mask_init(struct amdgpu_device *adev);
int amdgpu_jpeg_sysfs_reset_mask_init(struct amdgpu_device *adev);
void amdgpu_jpeg_sysfs_reset_mask_fini(struct amdgpu_device *adev);
int amdgpu_jpeg_reg_dump_init(struct amdgpu_device *adev,
			       const struct amdgpu_hwip_reg_entry *reg, u32 count);
void amdgpu_jpeg_dump_ip_state(struct amdgpu_ip_block *ip_block);
void amdgpu_jpeg_print_ip_state(struct amdgpu_ip_block *ip_block, struct drm_printer *p);
int amdgpu_jpeg_dec_parse_cs(struct amdgpu_cs_parser *parser,
			     struct amdgpu_job *job,
			     struct amdgpu_ib *ib);

#endif /*__AMDGPU_JPEG_H__*/
