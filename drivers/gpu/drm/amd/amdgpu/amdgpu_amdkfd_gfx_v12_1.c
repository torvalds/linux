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

#include "amdgpu.h"
#include "amdgpu_amdkfd.h"
#include "gc/gc_12_1_0_offset.h"
#include "gc/gc_12_1_0_sh_mask.h"
#include "soc_v1_0.h"
#include <uapi/linux/kfd_ioctl.h>

static void lock_srbm(struct amdgpu_device *adev, uint32_t mec, uint32_t pipe,
			uint32_t queue, uint32_t vmid, uint32_t inst)
{
	mutex_lock(&adev->srbm_mutex);
	amdgpu_gfx_select_me_pipe_q(adev, mec, pipe, queue, vmid, inst);
}

static void unlock_srbm(struct amdgpu_device *adev, uint32_t inst)
{
	amdgpu_gfx_select_me_pipe_q(adev, 0, 0, 0, 0, inst);
	mutex_unlock(&adev->srbm_mutex);
}

static void acquire_queue(struct amdgpu_device *adev, uint32_t pipe_id,
				uint32_t queue_id, uint32_t inst)
{
	uint32_t mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
	uint32_t pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

	lock_srbm(adev, mec, pipe, queue_id, 0, inst);
}

static void release_queue(struct amdgpu_device *adev, uint32_t inst)
{
	unlock_srbm(adev, inst);
}

static int init_interrupts_v12_1(struct amdgpu_device *adev, uint32_t pipe_id, uint32_t inst)
{
	uint32_t mec;
	uint32_t pipe;

	mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
	pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

	lock_srbm(adev, mec, pipe, 0, 0, inst);

	WREG32_SOC15(GC, GET_INST(GC, inst), regCPC_INT_CNTL,
		CP_INT_CNTL_RING0__TIME_STAMP_INT_ENABLE_MASK |
		CP_INT_CNTL_RING0__OPCODE_ERROR_INT_ENABLE_MASK);

	unlock_srbm(adev, inst);

	return 0;
}

static uint32_t get_sdma_rlc_reg_offset(struct amdgpu_device *adev,
				unsigned int engine_id,
				unsigned int queue_id)
{
	uint32_t sdma_engine_reg_base = 0;
	uint32_t sdma_rlc_reg_offset;
	uint32_t dev_inst = GET_INST(SDMA0, engine_id);

	switch (dev_inst % adev->sdma.num_inst_per_xcc) {
	case 0:
		sdma_engine_reg_base = SOC15_REG_OFFSET(SDMA0,
				dev_inst / adev->sdma.num_inst_per_xcc,
				regSDMA0_SDMA_QUEUE0_RB_CNTL) - regSDMA0_SDMA_QUEUE0_RB_CNTL;
		break;
	case 1:
		sdma_engine_reg_base = SOC15_REG_OFFSET(SDMA1,
				dev_inst / adev->sdma.num_inst_per_xcc,
				regSDMA1_SDMA_QUEUE0_RB_CNTL) - regSDMA0_SDMA_QUEUE0_RB_CNTL;
		break;
	default:
		WARN(1, "Invalid SDMA engine id %d\n", engine_id);
		break;
	}

	sdma_rlc_reg_offset = sdma_engine_reg_base
		+ queue_id * (regSDMA0_SDMA_QUEUE1_RB_CNTL - regSDMA0_SDMA_QUEUE0_RB_CNTL);

	pr_debug("RLC register offset for SDMA%d RLC%d: 0x%x\n", engine_id,
			queue_id, sdma_rlc_reg_offset);

	return sdma_rlc_reg_offset;
}

static int hqd_dump_v12_1(struct amdgpu_device *adev,
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

	acquire_queue(adev, pipe_id, queue_id, inst);

	for (reg = SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regCP_MQD_BASE_ADDR);
	     reg <= SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regCP_HQD_PQ_WPTR_HI); reg++)
		DUMP_REG(reg);

	release_queue(adev, inst);

	WARN_ON_ONCE(i != HQD_N_REGS);
	*n_regs = i;

	return 0;
}

static int hqd_sdma_dump_v12_1(struct amdgpu_device *adev,
			     uint32_t engine_id, uint32_t queue_id,
			     uint32_t (**dump)[2], uint32_t *n_regs)
{
	uint32_t sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev,
			engine_id, queue_id);
	uint32_t i = 0, reg;

	const uint32_t first_reg = regSDMA0_SDMA_QUEUE0_RB_CNTL;
	const uint32_t last_reg = regSDMA0_SDMA_QUEUE0_CONTEXT_STATUS;
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

static int wave_control_execute_v12_1(struct amdgpu_device *adev,
					uint32_t gfx_index_val,
					uint32_t sq_cmd, uint32_t inst)
{
	uint32_t data = 0;

	mutex_lock(&adev->grbm_idx_mutex);

	WREG32(SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regGRBM_GFX_INDEX), gfx_index_val);
	WREG32(SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regSQ_CMD), sq_cmd);

	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		INSTANCE_BROADCAST_WRITES, 1);
	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		SA_BROADCAST_WRITES, 1);
	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		SE_BROADCAST_WRITES, 1);

	WREG32(SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regGRBM_GFX_INDEX), data);
	mutex_unlock(&adev->grbm_idx_mutex);

	return 0;
}

/* returns TRAP_EN, EXCP_EN and EXCP_REPLACE. */
static uint32_t kgd_gfx_v12_1_enable_debug_trap(struct amdgpu_device *adev,
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
static uint32_t kgd_gfx_v12_1_disable_debug_trap(struct amdgpu_device *adev,
						bool keep_trap_enabled,
						uint32_t vmid)
{
	uint32_t data = 0;

	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, TRAP_EN, 1);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_EN, 0);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_REPLACE, 0);

	return data;
}

static int kgd_gfx_v12_1_validate_trap_override_request(struct amdgpu_device *adev,
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
static uint32_t kgd_gfx_v12_1_set_wave_launch_trap_override(struct amdgpu_device *adev,
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
static uint32_t kgd_gfx_v12_1_set_wave_launch_mode(struct amdgpu_device *adev,
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
static uint32_t kgd_gfx_v12_1_set_address_watch(struct amdgpu_device *adev,
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
	watch_address_high = upper_32_bits(watch_address) & 0x1ffffff;

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

	WREG32_XCC((SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regTCP_WATCH0_ADDR_H) +
			(watch_id * TCP_WATCH_STRIDE)),
			watch_address_high, inst);

	WREG32_XCC((SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regTCP_WATCH0_ADDR_L) +
			(watch_id * TCP_WATCH_STRIDE)),
			watch_address_low, inst);

	return watch_address_cntl;
}

static uint32_t kgd_gfx_v12_1_clear_address_watch(struct amdgpu_device *adev,
					uint32_t watch_id)
{
	return 0;
}

static uint32_t kgd_gfx_v12_1_hqd_sdma_get_doorbell(struct amdgpu_device *adev,
						 int engine, int queue)
{
	return 0;
}

static void lock_spi_csq_mutexes(struct amdgpu_device *adev)
{
	mutex_lock(&adev->srbm_mutex);
	mutex_lock(&adev->grbm_idx_mutex);

}

static void unlock_spi_csq_mutexes(struct amdgpu_device *adev)
{
	mutex_unlock(&adev->grbm_idx_mutex);
	mutex_unlock(&adev->srbm_mutex);
}

/**
 * get_wave_count: Read device registers to get number of waves in flight for
 * a particular queue. The method also returns the doorbell offset associated
 * with the queue.
 *
 * @adev: Handle of device whose registers are to be read
 * @queue_idx: Index of queue in the queue-map bit-field
 * @queue_cnt: Stores the wave count and doorbell offset for an active queue
 * @inst: xcc's instance number on a multi-XCC setup
 */
static void get_wave_count(struct amdgpu_device *adev, int queue_idx,
		struct kfd_cu_occupancy *queue_cnt, uint32_t inst)
{
	int pipe_idx;
	int queue_slot;
	unsigned int reg_val;
	unsigned int wave_cnt;
	/*
	 * Program GRBM with appropriate MEID, PIPEID, QUEUEID and VMID
	 * parameters to read out waves in flight. Get doorbell offset if there are
	 * non-zero waves in flight.
	 */
	pipe_idx = queue_idx / adev->gfx.mec.num_queue_per_pipe;
	queue_slot = queue_idx % adev->gfx.mec.num_queue_per_pipe;
	amdgpu_gfx_select_me_pipe_q(adev, 1, pipe_idx, queue_slot, 0, inst);
	reg_val = RREG32_SOC15_IP(GC, SOC15_REG_OFFSET(GC, GET_INST(GC, inst),
				  regSPI_CSQ_WF_ACTIVE_COUNT_0) + queue_slot);
	wave_cnt = reg_val & SPI_CSQ_WF_ACTIVE_COUNT_0__COUNT_MASK;
	if (wave_cnt != 0) {
		queue_cnt->wave_cnt += wave_cnt;
		queue_cnt->doorbell_off =
			(RREG32_SOC15(GC, GET_INST(GC, inst), regCP_HQD_PQ_DOORBELL_CONTROL) &
			 CP_HQD_PQ_DOORBELL_CONTROL__DOORBELL_OFFSET_MASK) >>
			 CP_HQD_PQ_DOORBELL_CONTROL__DOORBELL_OFFSET__SHIFT;
	}
}

/**
 * kgd_gfx_v12_1_get_cu_occupancy: Reads relevant registers associated with
 * each shader engine and aggregates the number of waves that are in flight
 * for the process whose pasid is provided as a parameter. The process could
 * have ZERO or more queues running and submitting waves to compute units.
 *
 * @adev: Handle of device from which to get number of waves in flight
 * @cu_occupancy: Array that gets filled with wave_cnt and doorbell offset
 *		  for comparison later.
 * @max_waves_per_cu: Output parameter updated with maximum number of waves
 *                    possible per Compute Unit
 * @inst: xcc's instance number on a multi-XCC setup
 *
 * Note: It's possible that the device has too many queues (oversubscription)
 * in which case a VMID could be remapped to a different PASID. This could lead
 * to an inaccurate wave count. Following is a high-level sequence:
 *    Time T1: vmid = getVmid(); vmid is associated with Pasid P1
 *    Time T2: passId = getPasId(vmid); vmid is associated with Pasid P2
 * In the sequence above wave count obtained from time T1 will be incorrectly
 * lost or added to total wave count.
 *
 * The registers that provide the waves in flight are:
 *
 *  SPI_CSQ_WF_ACTIVE_STATUS - bit-map of queues per pipe. The bit is ON if a
 *  queue is slotted, OFF if there is no queue. A process could have ZERO or
 *  more queues slotted and submitting waves to be run on compute units. Even
 *  when there is a queue it is possible there could be zero wave fronts, this
 *  can happen when queue is waiting on top-of-pipe events - e.g. waitRegMem
 *  command
 *
 *  For each bit that is ON from above:
 *
 *    Read (SPI_CSQ_WF_ACTIVE_COUNT_0 + queue_idx) register. It provides the
 *    number of waves that are in flight for the queue at specified index. The
 *    index ranges from 0 to 7.
 *
 *    If non-zero waves are in flight, store the corresponding doorbell offset
 *    of the queue, along with the wave count.
 *
 *    Determine if the queue belongs to the process by comparing the doorbell
 *    offset against the process's queues. If it matches, aggregate the wave
 *    count for the process.
 *
 *  Reading registers referenced above involves programming GRBM appropriately
 */
static void kgd_gfx_v12_1_get_cu_occupancy(struct amdgpu_device *adev,
				struct kfd_cu_occupancy *cu_occupancy,
				int *max_waves_per_cu, uint32_t inst)
{
	int qidx;
	int se_idx;
	int se_cnt;
	int queue_map;
	int max_queue_cnt;
	DECLARE_BITMAP(cp_queue_bitmap, AMDGPU_MAX_QUEUES);

	lock_spi_csq_mutexes(adev);
	amdgpu_gfx_select_me_pipe_q(adev, 1, 0, 0, 0, inst);

	/*
	 * Iterate through the shader engines and arrays of the device
	 * to get number of waves in flight
	 */
	bitmap_complement(cp_queue_bitmap, adev->gfx.mec_bitmap[0].queue_bitmap,
			  AMDGPU_MAX_QUEUES);
	max_queue_cnt = adev->gfx.mec.num_pipe_per_mec *
			adev->gfx.mec.num_queue_per_pipe;
	se_cnt = adev->gfx.config.max_shader_engines;
	for (se_idx = 0; se_idx < se_cnt; se_idx++) {
		amdgpu_gfx_select_se_sh(adev, se_idx, 0, 0xffffffff, inst);
		queue_map = RREG32_SOC15(GC, GET_INST(GC, inst),
					 regSPI_CSQ_WF_ACTIVE_STATUS);

		for (qidx = 0; qidx < max_queue_cnt; qidx++) {
			/* Skip queues that are not associated with
			 * compute functions
			 */
			if (!test_bit(qidx, cp_queue_bitmap))
				continue;

			if (!(queue_map & (1 << qidx)))
				continue;

			/* Get number of waves in flight and aggregate them */
			get_wave_count(adev, qidx, &cu_occupancy[qidx], inst);
		}
	}

	amdgpu_gfx_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff, inst);
	amdgpu_gfx_select_me_pipe_q(adev, 0, 0, 0, 0, inst);
	unlock_spi_csq_mutexes(adev);

	/* Update the output parameters and return */
	*max_waves_per_cu = adev->gfx.cu_info.simd_per_cu *
				adev->gfx.cu_info.max_waves_per_simd;
}

const struct kfd2kgd_calls gfx_v12_1_kfd2kgd = {
	.init_interrupts = init_interrupts_v12_1,
	.hqd_dump = hqd_dump_v12_1,
	.hqd_sdma_dump = hqd_sdma_dump_v12_1,
	.wave_control_execute = wave_control_execute_v12_1,
	.get_atc_vmid_pasid_mapping_info = NULL,
	.enable_debug_trap = kgd_gfx_v12_1_enable_debug_trap,
	.disable_debug_trap = kgd_gfx_v12_1_disable_debug_trap,
	.validate_trap_override_request = kgd_gfx_v12_1_validate_trap_override_request,
	.set_wave_launch_trap_override = kgd_gfx_v12_1_set_wave_launch_trap_override,
	.set_wave_launch_mode = kgd_gfx_v12_1_set_wave_launch_mode,
	.set_address_watch = kgd_gfx_v12_1_set_address_watch,
	.clear_address_watch = kgd_gfx_v12_1_clear_address_watch,
	.hqd_sdma_get_doorbell = kgd_gfx_v12_1_hqd_sdma_get_doorbell,
	.get_cu_occupancy = kgd_gfx_v12_1_get_cu_occupancy
};
