/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#include "amdgpu.h"
#include "amdgpu_amdkfd.h"
#include "gc/gc_12_0_0_offset.h"
#include "gc/gc_12_0_0_sh_mask.h"
#include "soc24.h"
#include <uapi/linux/kfd_ioctl.h>

static void lock_srbm(struct amdgpu_device *adev, uint32_t mec, uint32_t pipe,
			uint32_t queue, uint32_t vmid)
{
	mutex_lock(&adev->srbm_mutex);
	soc24_grbm_select(adev, mec, pipe, queue, vmid);
}

static void unlock_srbm(struct amdgpu_device *adev)
{
	soc24_grbm_select(adev, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);
}

static void acquire_queue(struct amdgpu_device *adev, uint32_t pipe_id,
				uint32_t queue_id)
{
	uint32_t mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
	uint32_t pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

	lock_srbm(adev, mec, pipe, queue_id, 0);
}

static void release_queue(struct amdgpu_device *adev)
{
	unlock_srbm(adev);
}

static int init_interrupts_v12(struct amdgpu_device *adev, uint32_t pipe_id, uint32_t inst)
{
	uint32_t mec;
	uint32_t pipe;

	mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
	pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

	lock_srbm(adev, mec, pipe, 0, 0);

	WREG32_SOC15(GC, 0, regCPC_INT_CNTL,
		CP_INT_CNTL_RING0__TIME_STAMP_INT_ENABLE_MASK |
		CP_INT_CNTL_RING0__OPCODE_ERROR_INT_ENABLE_MASK);

	unlock_srbm(adev);

	return 0;
}

static uint32_t get_sdma_rlc_reg_offset(struct amdgpu_device *adev,
				unsigned int engine_id,
				unsigned int queue_id)
{
	uint32_t sdma_engine_reg_base = 0;
	uint32_t sdma_rlc_reg_offset;

	switch (engine_id) {
	case 0:
		sdma_engine_reg_base = SOC15_REG_OFFSET(SDMA0, 0,
				regSDMA0_QUEUE0_RB_CNTL) - regSDMA0_QUEUE0_RB_CNTL;
		break;
	case 1:
		sdma_engine_reg_base = SOC15_REG_OFFSET(SDMA1, 0,
				regSDMA1_QUEUE0_RB_CNTL) - regSDMA0_QUEUE0_RB_CNTL;
		break;
	default:
		BUG();
	}

	sdma_rlc_reg_offset = sdma_engine_reg_base
		+ queue_id * (regSDMA0_QUEUE1_RB_CNTL - regSDMA0_QUEUE0_RB_CNTL);

	pr_debug("RLC register offset for SDMA%d RLC%d: 0x%x\n", engine_id,
			queue_id, sdma_rlc_reg_offset);

	return sdma_rlc_reg_offset;
}

static int hqd_dump_v12(struct amdgpu_device *adev,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t (**dump)[2], uint32_t *n_regs, uint32_t inst)
{
	uint32_t i = 0, reg;
#define HQD_N_REGS 56
#define DUMP_REG(addr) do {				\
		if (WARN_ON_ONCE(i >= HQD_N_REGS))	\
			break;				\
		(*dump)[i][0] = (addr) << 2;		\
		(*dump)[i++][1] = RREG32(addr);		\
	} while (0)

	*dump = kmalloc(HQD_N_REGS*2*sizeof(uint32_t), GFP_KERNEL);
	if (*dump == NULL)
		return -ENOMEM;

	acquire_queue(adev, pipe_id, queue_id);

	for (reg = SOC15_REG_OFFSET(GC, 0, regCP_MQD_BASE_ADDR);
	     reg <= SOC15_REG_OFFSET(GC, 0, regCP_HQD_PQ_WPTR_HI); reg++)
		DUMP_REG(reg);

	release_queue(adev);

	WARN_ON_ONCE(i != HQD_N_REGS);
	*n_regs = i;

	return 0;
}

static int hqd_sdma_dump_v12(struct amdgpu_device *adev,
			     uint32_t engine_id, uint32_t queue_id,
			     uint32_t (**dump)[2], uint32_t *n_regs)
{
	uint32_t sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev,
			engine_id, queue_id);
	uint32_t i = 0, reg;

	const uint32_t first_reg = regSDMA0_QUEUE0_RB_CNTL;
	const uint32_t last_reg = regSDMA0_QUEUE0_CONTEXT_STATUS;
#undef HQD_N_REGS
#define HQD_N_REGS (last_reg - first_reg + 1)

	*dump = kmalloc(HQD_N_REGS*2*sizeof(uint32_t), GFP_KERNEL);
	if (*dump == NULL)
		return -ENOMEM;

	for (reg = first_reg;
	     reg <= last_reg; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);

	WARN_ON_ONCE(i != HQD_N_REGS);
	*n_regs = i;

	return 0;
}

static int wave_control_execute_v12(struct amdgpu_device *adev,
					uint32_t gfx_index_val,
					uint32_t sq_cmd, uint32_t inst)
{
	uint32_t data = 0;

	mutex_lock(&adev->grbm_idx_mutex);

	WREG32(SOC15_REG_OFFSET(GC, 0, regGRBM_GFX_INDEX), gfx_index_val);
	WREG32(SOC15_REG_OFFSET(GC, 0, regSQ_CMD), sq_cmd);

	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		INSTANCE_BROADCAST_WRITES, 1);
	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		SA_BROADCAST_WRITES, 1);
	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		SE_BROADCAST_WRITES, 1);

	WREG32(SOC15_REG_OFFSET(GC, 0, regGRBM_GFX_INDEX), data);
	mutex_unlock(&adev->grbm_idx_mutex);

	return 0;
}

/* returns TRAP_EN, EXCP_EN and EXCP_REPLACE. */
static uint32_t kgd_gfx_v12_enable_debug_trap(struct amdgpu_device *adev,
					    bool restore_dbg_registers,
					    uint32_t vmid)
{
	uint32_t data = 0;

	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, TRAP_EN, 1);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_EN, 0);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_REPLACE, 0);

	return data;
}

/* returns TRAP_EN, EXCP_EN and EXCP_REPLACE. */
static uint32_t kgd_gfx_v12_disable_debug_trap(struct amdgpu_device *adev,
						bool keep_trap_enabled,
						uint32_t vmid)
{
	uint32_t data = 0;

	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, TRAP_EN, 1);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_EN, 0);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_REPLACE, 0);

	return data;
}

static int kgd_gfx_v12_validate_trap_override_request(struct amdgpu_device *adev,
							uint32_t trap_override,
							uint32_t *trap_mask_supported)
{
	*trap_mask_supported &= KFD_DBG_TRAP_MASK_FP_INVALID |
				KFD_DBG_TRAP_MASK_FP_INPUT_DENORMAL |
				KFD_DBG_TRAP_MASK_FP_DIVIDE_BY_ZERO |
				KFD_DBG_TRAP_MASK_FP_OVERFLOW |
				KFD_DBG_TRAP_MASK_FP_UNDERFLOW |
				KFD_DBG_TRAP_MASK_FP_INEXACT |
				KFD_DBG_TRAP_MASK_INT_DIVIDE_BY_ZERO |
				KFD_DBG_TRAP_MASK_DBG_ADDRESS_WATCH |
				KFD_DBG_TRAP_MASK_DBG_MEMORY_VIOLATION |
				KFD_DBG_TRAP_MASK_TRAP_ON_WAVE_START |
				KFD_DBG_TRAP_MASK_TRAP_ON_WAVE_END;


	if (trap_override != KFD_DBG_TRAP_OVERRIDE_OR &&
			trap_override != KFD_DBG_TRAP_OVERRIDE_REPLACE)
		return -EPERM;

	return 0;
}

static uint32_t trap_mask_map_sw_to_hw(uint32_t mask)
{
	uint32_t trap_on_start = (mask & KFD_DBG_TRAP_MASK_TRAP_ON_WAVE_START) ? 1 : 0;
	uint32_t trap_on_end = (mask & KFD_DBG_TRAP_MASK_TRAP_ON_WAVE_END) ? 1 : 0;
	uint32_t excp_en = mask & (KFD_DBG_TRAP_MASK_FP_INVALID |
			KFD_DBG_TRAP_MASK_FP_INPUT_DENORMAL |
			KFD_DBG_TRAP_MASK_FP_DIVIDE_BY_ZERO |
			KFD_DBG_TRAP_MASK_FP_OVERFLOW |
			KFD_DBG_TRAP_MASK_FP_UNDERFLOW |
			KFD_DBG_TRAP_MASK_FP_INEXACT |
			KFD_DBG_TRAP_MASK_INT_DIVIDE_BY_ZERO |
			KFD_DBG_TRAP_MASK_DBG_ADDRESS_WATCH |
			KFD_DBG_TRAP_MASK_DBG_MEMORY_VIOLATION);
	uint32_t ret;

	ret = REG_SET_FIELD(0, SPI_GDBG_PER_VMID_CNTL, EXCP_EN, excp_en);
	ret = REG_SET_FIELD(ret, SPI_GDBG_PER_VMID_CNTL, TRAP_ON_START, trap_on_start);
	ret = REG_SET_FIELD(ret, SPI_GDBG_PER_VMID_CNTL, TRAP_ON_END, trap_on_end);

	return ret;
}

static uint32_t trap_mask_map_hw_to_sw(uint32_t mask)
{
	uint32_t ret = REG_GET_FIELD(mask, SPI_GDBG_PER_VMID_CNTL, EXCP_EN);

	if (REG_GET_FIELD(mask, SPI_GDBG_PER_VMID_CNTL, TRAP_ON_START))
		ret |= KFD_DBG_TRAP_MASK_TRAP_ON_WAVE_START;

	if (REG_GET_FIELD(mask, SPI_GDBG_PER_VMID_CNTL, TRAP_ON_END))
		ret |= KFD_DBG_TRAP_MASK_TRAP_ON_WAVE_END;

	return ret;
}

/* returns TRAP_EN, EXCP_EN and EXCP_REPLACE. */
static uint32_t kgd_gfx_v12_set_wave_launch_trap_override(struct amdgpu_device *adev,
					uint32_t vmid,
					uint32_t trap_override,
					uint32_t trap_mask_bits,
					uint32_t trap_mask_request,
					uint32_t *trap_mask_prev,
					uint32_t kfd_dbg_trap_cntl_prev)

{
	uint32_t data = 0;

	*trap_mask_prev = trap_mask_map_hw_to_sw(kfd_dbg_trap_cntl_prev);

	data = (trap_mask_bits & trap_mask_request) | (*trap_mask_prev & ~trap_mask_request);
	data = trap_mask_map_sw_to_hw(data);

	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, TRAP_EN, 1);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_REPLACE, trap_override);

	return data;
}

/* returns STALL_VMID or LAUNCH_MODE. */
static uint32_t kgd_gfx_v12_set_wave_launch_mode(struct amdgpu_device *adev,
					uint8_t wave_launch_mode,
					uint32_t vmid)
{
	uint32_t data = 0;
	bool is_stall_mode = wave_launch_mode == 4;

	if (is_stall_mode)
		data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, STALL_VMID,
									1);
	else
		data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, LAUNCH_MODE,
							wave_launch_mode);

	return data;
}

#define TCP_WATCH_STRIDE (regTCP_WATCH1_ADDR_H - regTCP_WATCH0_ADDR_H)
static uint32_t kgd_gfx_v12_set_address_watch(struct amdgpu_device *adev,
					uint64_t watch_address,
					uint32_t watch_address_mask,
					uint32_t watch_id,
					uint32_t watch_mode,
					uint32_t debug_vmid,
					uint32_t inst)
{
	uint32_t watch_address_high;
	uint32_t watch_address_low;
	uint32_t watch_address_cntl;

	watch_address_cntl = 0;
	watch_address_low = lower_32_bits(watch_address);
	watch_address_high = upper_32_bits(watch_address) & 0xffff;

	watch_address_cntl = REG_SET_FIELD(watch_address_cntl,
			TCP_WATCH0_CNTL,
			MODE,
			watch_mode);

	watch_address_cntl = REG_SET_FIELD(watch_address_cntl,
			TCP_WATCH0_CNTL,
			MASK,
			watch_address_mask >> 7);

	watch_address_cntl = REG_SET_FIELD(watch_address_cntl,
			TCP_WATCH0_CNTL,
			VALID,
			1);

	WREG32_RLC((SOC15_REG_OFFSET(GC, 0, regTCP_WATCH0_ADDR_H) +
			(watch_id * TCP_WATCH_STRIDE)),
			watch_address_high);

	WREG32_RLC((SOC15_REG_OFFSET(GC, 0, regTCP_WATCH0_ADDR_L) +
			(watch_id * TCP_WATCH_STRIDE)),
			watch_address_low);

	return watch_address_cntl;
}

static uint32_t kgd_gfx_v12_clear_address_watch(struct amdgpu_device *adev,
					uint32_t watch_id)
{
	return 0;
}

static uint32_t kgd_gfx_v12_hqd_sdma_get_doorbell(struct amdgpu_device *adev,
						 int engine, int queue)
{
	return 0;
}

const struct kfd2kgd_calls gfx_v12_kfd2kgd = {
	.init_interrupts = init_interrupts_v12,
	.hqd_dump = hqd_dump_v12,
	.hqd_sdma_dump = hqd_sdma_dump_v12,
	.wave_control_execute = wave_control_execute_v12,
	.get_atc_vmid_pasid_mapping_info = NULL,
	.enable_debug_trap = kgd_gfx_v12_enable_debug_trap,
	.disable_debug_trap = kgd_gfx_v12_disable_debug_trap,
	.validate_trap_override_request = kgd_gfx_v12_validate_trap_override_request,
	.set_wave_launch_trap_override = kgd_gfx_v12_set_wave_launch_trap_override,
	.set_wave_launch_mode = kgd_gfx_v12_set_wave_launch_mode,
	.set_address_watch = kgd_gfx_v12_set_address_watch,
	.clear_address_watch = kgd_gfx_v12_clear_address_watch,
	.hqd_sdma_get_doorbell = kgd_gfx_v12_hqd_sdma_get_doorbell
};
