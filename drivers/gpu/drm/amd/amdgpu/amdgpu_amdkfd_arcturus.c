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
 */
#include <linux/module.h>
#include <linux/fdtable.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_amdkfd_arcturus.h"
#include "amdgpu_reset.h"
#include "sdma0/sdma0_4_2_2_offset.h"
#include "sdma0/sdma0_4_2_2_sh_mask.h"
#include "sdma1/sdma1_4_2_2_offset.h"
#include "sdma1/sdma1_4_2_2_sh_mask.h"
#include "sdma2/sdma2_4_2_2_offset.h"
#include "sdma2/sdma2_4_2_2_sh_mask.h"
#include "sdma3/sdma3_4_2_2_offset.h"
#include "sdma3/sdma3_4_2_2_sh_mask.h"
#include "sdma4/sdma4_4_2_2_offset.h"
#include "sdma4/sdma4_4_2_2_sh_mask.h"
#include "sdma5/sdma5_4_2_2_offset.h"
#include "sdma5/sdma5_4_2_2_sh_mask.h"
#include "sdma6/sdma6_4_2_2_offset.h"
#include "sdma6/sdma6_4_2_2_sh_mask.h"
#include "sdma7/sdma7_4_2_2_offset.h"
#include "sdma7/sdma7_4_2_2_sh_mask.h"
#include "v9_structs.h"
#include "soc15.h"
#include "soc15d.h"
#include "amdgpu_amdkfd_gfx_v9.h"
#include "gfxhub_v1_0.h"
#include "mmhub_v9_4.h"
#include "gc/gc_9_0_offset.h"
#include "gc/gc_9_0_sh_mask.h"

#define HQD_N_REGS 56
#define DUMP_REG(addr) do {				\
		if (WARN_ON_ONCE(i >= HQD_N_REGS))	\
			break;				\
		(*dump)[i][0] = (addr) << 2;		\
		(*dump)[i++][1] = RREG32(addr);		\
	} while (0)

static inline struct v9_sdma_mqd *get_sdma_mqd(void *mqd)
{
	return (struct v9_sdma_mqd *)mqd;
}

static uint32_t get_sdma_rlc_reg_offset(struct amdgpu_device *adev,
				unsigned int engine_id,
				unsigned int queue_id)
{
	uint32_t sdma_engine_reg_base = 0;
	uint32_t sdma_rlc_reg_offset;

	switch (engine_id) {
	default:
		dev_warn(adev->dev,
			 "Invalid sdma engine id (%d), using engine id 0\n",
			 engine_id);
		fallthrough;
	case 0:
		sdma_engine_reg_base = SOC15_REG_OFFSET(SDMA0, 0,
				mmSDMA0_RLC0_RB_CNTL) - mmSDMA0_RLC0_RB_CNTL;
		break;
	case 1:
		sdma_engine_reg_base = SOC15_REG_OFFSET(SDMA1, 0,
				mmSDMA1_RLC0_RB_CNTL) - mmSDMA1_RLC0_RB_CNTL;
		break;
	case 2:
		sdma_engine_reg_base = SOC15_REG_OFFSET(SDMA2, 0,
				mmSDMA2_RLC0_RB_CNTL) - mmSDMA2_RLC0_RB_CNTL;
		break;
	case 3:
		sdma_engine_reg_base = SOC15_REG_OFFSET(SDMA3, 0,
				mmSDMA3_RLC0_RB_CNTL) - mmSDMA3_RLC0_RB_CNTL;
		break;
	case 4:
		sdma_engine_reg_base = SOC15_REG_OFFSET(SDMA4, 0,
				mmSDMA4_RLC0_RB_CNTL) - mmSDMA4_RLC0_RB_CNTL;
		break;
	case 5:
		sdma_engine_reg_base = SOC15_REG_OFFSET(SDMA5, 0,
				mmSDMA5_RLC0_RB_CNTL) - mmSDMA5_RLC0_RB_CNTL;
		break;
	case 6:
		sdma_engine_reg_base = SOC15_REG_OFFSET(SDMA6, 0,
				mmSDMA6_RLC0_RB_CNTL) - mmSDMA6_RLC0_RB_CNTL;
		break;
	case 7:
		sdma_engine_reg_base = SOC15_REG_OFFSET(SDMA7, 0,
				mmSDMA7_RLC0_RB_CNTL) - mmSDMA7_RLC0_RB_CNTL;
		break;
	}

	sdma_rlc_reg_offset = sdma_engine_reg_base
		+ queue_id * (mmSDMA0_RLC1_RB_CNTL - mmSDMA0_RLC0_RB_CNTL);

	pr_debug("RLC register offset for SDMA%d RLC%d: 0x%x\n", engine_id,
			queue_id, sdma_rlc_reg_offset);

	return sdma_rlc_reg_offset;
}

int kgd_arcturus_hqd_sdma_load(struct amdgpu_device *adev, void *mqd,
			     uint32_t __user *wptr, struct mm_struct *mm)
{
	struct v9_sdma_mqd *m;
	uint32_t sdma_rlc_reg_offset;
	unsigned long end_jiffies;
	uint32_t data;
	uint64_t data64;
	uint64_t __user *wptr64 = (uint64_t __user *)wptr;

	m = get_sdma_mqd(mqd);
	sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev, m->sdma_engine_id,
					    m->sdma_queue_id);

	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL,
		m->sdmax_rlcx_rb_cntl & (~SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK));

	end_jiffies = msecs_to_jiffies(2000) + jiffies;
	while (true) {
		data = RREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_CONTEXT_STATUS);
		if (data & SDMA0_RLC0_CONTEXT_STATUS__IDLE_MASK)
			break;
		if (time_after(jiffies, end_jiffies)) {
			pr_err("SDMA RLC not idle in %s\n", __func__);
			return -ETIME;
		}
		usleep_range(500, 1000);
	}

	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_DOORBELL_OFFSET,
	       m->sdmax_rlcx_doorbell_offset);

	data = REG_SET_FIELD(m->sdmax_rlcx_doorbell, SDMA0_RLC0_DOORBELL,
			     ENABLE, 1);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_DOORBELL, data);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR,
				m->sdmax_rlcx_rb_rptr);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR_HI,
				m->sdmax_rlcx_rb_rptr_hi);

	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_MINOR_PTR_UPDATE, 1);
	if (read_user_wptr(mm, wptr64, data64)) {
		WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_WPTR,
		       lower_32_bits(data64));
		WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_WPTR_HI,
		       upper_32_bits(data64));
	} else {
		WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_WPTR,
		       m->sdmax_rlcx_rb_rptr);
		WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_WPTR_HI,
		       m->sdmax_rlcx_rb_rptr_hi);
	}
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_MINOR_PTR_UPDATE, 0);

	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_BASE, m->sdmax_rlcx_rb_base);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_BASE_HI,
			m->sdmax_rlcx_rb_base_hi);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR_ADDR_LO,
			m->sdmax_rlcx_rb_rptr_addr_lo);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR_ADDR_HI,
			m->sdmax_rlcx_rb_rptr_addr_hi);

	data = REG_SET_FIELD(m->sdmax_rlcx_rb_cntl, SDMA0_RLC0_RB_CNTL,
			     RB_ENABLE, 1);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL, data);

	return 0;
}

int kgd_arcturus_hqd_sdma_dump(struct amdgpu_device *adev,
			     uint32_t engine_id, uint32_t queue_id,
			     uint32_t (**dump)[2], uint32_t *n_regs)
{
	uint32_t sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev,
			engine_id, queue_id);
	uint32_t i = 0, reg;
#undef HQD_N_REGS
#define HQD_N_REGS (19+6+7+10)

	*dump = kmalloc_array(HQD_N_REGS, sizeof(**dump), GFP_KERNEL);
	if (*dump == NULL)
		return -ENOMEM;

	for (reg = mmSDMA0_RLC0_RB_CNTL; reg <= mmSDMA0_RLC0_DOORBELL; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);
	for (reg = mmSDMA0_RLC0_STATUS; reg <= mmSDMA0_RLC0_CSA_ADDR_HI; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);
	for (reg = mmSDMA0_RLC0_IB_SUB_REMAIN;
	     reg <= mmSDMA0_RLC0_MINOR_PTR_UPDATE; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);
	for (reg = mmSDMA0_RLC0_MIDCMD_DATA0;
	     reg <= mmSDMA0_RLC0_MIDCMD_CNTL; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);

	WARN_ON_ONCE(i != HQD_N_REGS);
	*n_regs = i;

	return 0;
}

bool kgd_arcturus_hqd_sdma_is_occupied(struct amdgpu_device *adev,
				void *mqd)
{
	struct v9_sdma_mqd *m;
	uint32_t sdma_rlc_reg_offset;
	uint32_t sdma_rlc_rb_cntl;

	m = get_sdma_mqd(mqd);
	sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev, m->sdma_engine_id,
					    m->sdma_queue_id);

	sdma_rlc_rb_cntl = RREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL);

	if (sdma_rlc_rb_cntl & SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK)
		return true;

	return false;
}

int kgd_arcturus_hqd_sdma_destroy(struct amdgpu_device *adev, void *mqd,
				unsigned int utimeout)
{
	struct v9_sdma_mqd *m;
	uint32_t sdma_rlc_reg_offset;
	uint32_t temp;
	unsigned long end_jiffies = (utimeout * HZ / 1000) + jiffies;

	m = get_sdma_mqd(mqd);
	sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev, m->sdma_engine_id,
					    m->sdma_queue_id);

	temp = RREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL);
	temp = temp & ~SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK;
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL, temp);

	while (true) {
		temp = RREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_CONTEXT_STATUS);
		if (temp & SDMA0_RLC0_CONTEXT_STATUS__IDLE_MASK)
			break;
		if (time_after(jiffies, end_jiffies)) {
			pr_err("SDMA RLC not idle in %s\n", __func__);
			return -ETIME;
		}
		usleep_range(500, 1000);
	}

	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_DOORBELL, 0);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL,
		RREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL) |
		SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK);

	m->sdmax_rlcx_rb_rptr = RREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR);
	m->sdmax_rlcx_rb_rptr_hi =
		RREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR_HI);

	return 0;
}

/*
 * Helper used to suspend/resume gfx pipe for image post process work to set
 * barrier behaviour.
 */
static int suspend_resume_compute_scheduler(struct amdgpu_device *adev, bool suspend)
{
	int i, r = 0;

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		struct amdgpu_ring *ring = &adev->gfx.compute_ring[i];

		if (!amdgpu_ring_sched_ready(ring))
			continue;

		/* stop secheduler and drain ring. */
		if (suspend) {
			drm_sched_stop(&ring->sched, NULL);
			r = amdgpu_fence_wait_empty(ring);
			if (r)
				goto out;
		} else {
			drm_sched_start(&ring->sched, false);
		}
	}

out:
	/* return on resume or failure to drain rings. */
	if (!suspend || r)
		return r;

	return amdgpu_device_ip_wait_for_idle(adev, AMD_IP_BLOCK_TYPE_GFX);
}

static void set_barrier_auto_waitcnt(struct amdgpu_device *adev, bool enable_waitcnt)
{
	uint32_t data;

	WRITE_ONCE(adev->barrier_has_auto_waitcnt, enable_waitcnt);

	if (!down_read_trylock(&adev->reset_domain->sem))
		return;

	amdgpu_amdkfd_suspend(adev, false);

	if (suspend_resume_compute_scheduler(adev, true))
		goto out;

	data = RREG32(SOC15_REG_OFFSET(GC, 0, mmSQ_CONFIG));
	data = REG_SET_FIELD(data, SQ_CONFIG, DISABLE_BARRIER_WAITCNT,
						!enable_waitcnt);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmSQ_CONFIG), data);

out:
	suspend_resume_compute_scheduler(adev, false);

	amdgpu_amdkfd_resume(adev, false);

	up_read(&adev->reset_domain->sem);
}

/*
 * restore_dbg_registers is ignored here but is a general interface requirement
 * for devices that support GFXOFF and where the RLC save/restore list
 * does not support hw registers for debugging i.e. the driver has to manually
 * initialize the debug mode registers after it has disabled GFX off during the
 * debug session.
 */
static uint32_t kgd_arcturus_enable_debug_trap(struct amdgpu_device *adev,
				bool restore_dbg_registers,
				uint32_t vmid)
{
	mutex_lock(&adev->grbm_idx_mutex);

	kgd_gfx_v9_set_wave_launch_stall(adev, vmid, true);

	set_barrier_auto_waitcnt(adev, true);

	WREG32(SOC15_REG_OFFSET(GC, 0, mmSPI_GDBG_TRAP_MASK), 0);

	kgd_gfx_v9_set_wave_launch_stall(adev, vmid, false);

	mutex_unlock(&adev->grbm_idx_mutex);

	return 0;
}

/*
 * keep_trap_enabled is ignored here but is a general interface requirement
 * for devices that support multi-process debugging where the performance
 * overhead from trap temporary setup needs to be bypassed when the debug
 * session has ended.
 */
static uint32_t kgd_arcturus_disable_debug_trap(struct amdgpu_device *adev,
					bool keep_trap_enabled,
					uint32_t vmid)
{

	mutex_lock(&adev->grbm_idx_mutex);

	kgd_gfx_v9_set_wave_launch_stall(adev, vmid, true);

	set_barrier_auto_waitcnt(adev, false);

	WREG32(SOC15_REG_OFFSET(GC, 0, mmSPI_GDBG_TRAP_MASK), 0);

	kgd_gfx_v9_set_wave_launch_stall(adev, vmid, false);

	mutex_unlock(&adev->grbm_idx_mutex);

	return 0;
}
const struct kfd2kgd_calls arcturus_kfd2kgd = {
	.program_sh_mem_settings = kgd_gfx_v9_program_sh_mem_settings,
	.set_pasid_vmid_mapping = kgd_gfx_v9_set_pasid_vmid_mapping,
	.init_interrupts = kgd_gfx_v9_init_interrupts,
	.hqd_load = kgd_gfx_v9_hqd_load,
	.hiq_mqd_load = kgd_gfx_v9_hiq_mqd_load,
	.hqd_sdma_load = kgd_arcturus_hqd_sdma_load,
	.hqd_dump = kgd_gfx_v9_hqd_dump,
	.hqd_sdma_dump = kgd_arcturus_hqd_sdma_dump,
	.hqd_is_occupied = kgd_gfx_v9_hqd_is_occupied,
	.hqd_sdma_is_occupied = kgd_arcturus_hqd_sdma_is_occupied,
	.hqd_destroy = kgd_gfx_v9_hqd_destroy,
	.hqd_sdma_destroy = kgd_arcturus_hqd_sdma_destroy,
	.wave_control_execute = kgd_gfx_v9_wave_control_execute,
	.get_atc_vmid_pasid_mapping_info =
				kgd_gfx_v9_get_atc_vmid_pasid_mapping_info,
	.set_vm_context_page_table_base =
				kgd_gfx_v9_set_vm_context_page_table_base,
	.enable_debug_trap = kgd_arcturus_enable_debug_trap,
	.disable_debug_trap = kgd_arcturus_disable_debug_trap,
	.validate_trap_override_request = kgd_gfx_v9_validate_trap_override_request,
	.set_wave_launch_trap_override = kgd_gfx_v9_set_wave_launch_trap_override,
	.set_wave_launch_mode = kgd_gfx_v9_set_wave_launch_mode,
	.set_address_watch = kgd_gfx_v9_set_address_watch,
	.clear_address_watch = kgd_gfx_v9_clear_address_watch,
	.get_iq_wait_times = kgd_gfx_v9_get_iq_wait_times,
	.build_grace_period_packet_info = kgd_gfx_v9_build_grace_period_packet_info,
	.get_cu_occupancy = kgd_gfx_v9_get_cu_occupancy,
	.program_trap_handler_settings = kgd_gfx_v9_program_trap_handler_settings
};
