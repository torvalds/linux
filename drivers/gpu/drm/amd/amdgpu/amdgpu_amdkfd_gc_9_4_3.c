/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include "amdgpu_amdkfd_gfx_v9.h"
#include "gc/gc_9_4_3_offset.h"
#include "gc/gc_9_4_3_sh_mask.h"
#include "athub/athub_1_8_0_offset.h"
#include "athub/athub_1_8_0_sh_mask.h"
#include "oss/osssys_4_4_2_offset.h"
#include "oss/osssys_4_4_2_sh_mask.h"
#include "v9_structs.h"
#include "soc15.h"
#include "sdma/sdma_4_4_2_offset.h"
#include "sdma/sdma_4_4_2_sh_mask.h"

static inline struct v9_sdma_mqd *get_sdma_mqd(void *mqd)
{
	return (struct v9_sdma_mqd *)mqd;
}

static uint32_t get_sdma_rlc_reg_offset(struct amdgpu_device *adev,
					unsigned int engine_id,
					unsigned int queue_id)
{
	uint32_t sdma_engine_reg_base =
		SOC15_REG_OFFSET(SDMA0, GET_INST(SDMA0, engine_id),
				 regSDMA_RLC0_RB_CNTL) -
		regSDMA_RLC0_RB_CNTL;
	uint32_t retval = sdma_engine_reg_base +
		  queue_id * (regSDMA_RLC1_RB_CNTL - regSDMA_RLC0_RB_CNTL);

	pr_debug("RLC register offset for SDMA%d RLC%d: 0x%x\n", engine_id,
							queue_id, retval);
	return retval;
}

static int kgd_gfx_v9_4_3_hqd_sdma_load(struct amdgpu_device *adev, void *mqd,
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

	WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_CNTL,
		m->sdmax_rlcx_rb_cntl & (~SDMA_RLC0_RB_CNTL__RB_ENABLE_MASK));

	end_jiffies = msecs_to_jiffies(2000) + jiffies;
	while (true) {
		data = RREG32(sdma_rlc_reg_offset + regSDMA_RLC0_CONTEXT_STATUS);
		if (data & SDMA_RLC0_CONTEXT_STATUS__IDLE_MASK)
			break;
		if (time_after(jiffies, end_jiffies)) {
			pr_err("SDMA RLC not idle in %s\n", __func__);
			return -ETIME;
		}
		usleep_range(500, 1000);
	}

	WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_DOORBELL_OFFSET,
		m->sdmax_rlcx_doorbell_offset);

	data = REG_SET_FIELD(m->sdmax_rlcx_doorbell, SDMA_RLC0_DOORBELL,
				ENABLE, 1);
	WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_DOORBELL, data);
	WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_RPTR,
					m->sdmax_rlcx_rb_rptr);
	WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_RPTR_HI,
					m->sdmax_rlcx_rb_rptr_hi);

	WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_MINOR_PTR_UPDATE, 1);
	if (read_user_wptr(mm, wptr64, data64)) {
		WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_WPTR,
			lower_32_bits(data64));
		WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_WPTR_HI,
			upper_32_bits(data64));
	} else {
		WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_WPTR,
			m->sdmax_rlcx_rb_rptr);
		WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_WPTR_HI,
			m->sdmax_rlcx_rb_rptr_hi);
	}
	WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_MINOR_PTR_UPDATE, 0);

	WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_BASE, m->sdmax_rlcx_rb_base);
	WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_BASE_HI,
			m->sdmax_rlcx_rb_base_hi);
	WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_RPTR_ADDR_LO,
			m->sdmax_rlcx_rb_rptr_addr_lo);
	WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_RPTR_ADDR_HI,
			m->sdmax_rlcx_rb_rptr_addr_hi);

	data = REG_SET_FIELD(m->sdmax_rlcx_rb_cntl, SDMA_RLC0_RB_CNTL,
				RB_ENABLE, 1);
	WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_CNTL, data);

	return 0;
}

static int kgd_gfx_v9_4_3_hqd_sdma_dump(struct amdgpu_device *adev,
				 uint32_t engine_id, uint32_t queue_id,
				 uint32_t (**dump)[2], uint32_t *n_regs)
{
	uint32_t sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev,
							engine_id, queue_id);
	uint32_t i = 0, reg;
#undef HQD_N_REGS
#define HQD_N_REGS (19+6+7+12)
#define DUMP_REG(addr) do {				\
		if (WARN_ON_ONCE(i >= HQD_N_REGS))      \
			break;				\
		(*dump)[i][0] = (addr) << 2;            \
		(*dump)[i++][1] = RREG32(addr);         \
	} while (0)

	*dump = kmalloc_array(HQD_N_REGS * 2, sizeof(uint32_t), GFP_KERNEL);
	if (*dump == NULL)
		return -ENOMEM;

	for (reg = regSDMA_RLC0_RB_CNTL; reg <= regSDMA_RLC0_DOORBELL; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);
	for (reg = regSDMA_RLC0_STATUS; reg <= regSDMA_RLC0_CSA_ADDR_HI; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);
	for (reg = regSDMA_RLC0_IB_SUB_REMAIN;
	     reg <= regSDMA_RLC0_MINOR_PTR_UPDATE; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);
	for (reg = regSDMA_RLC0_MIDCMD_DATA0;
	     reg <= regSDMA_RLC0_MIDCMD_CNTL; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);

	WARN_ON_ONCE(i != HQD_N_REGS);
	*n_regs = i;

	return 0;
}

static bool kgd_gfx_v9_4_3_hqd_sdma_is_occupied(struct amdgpu_device *adev, void *mqd)
{
	struct v9_sdma_mqd *m;
	uint32_t sdma_rlc_reg_offset;
	uint32_t sdma_rlc_rb_cntl;

	m = get_sdma_mqd(mqd);
	sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev, m->sdma_engine_id,
							m->sdma_queue_id);

	sdma_rlc_rb_cntl = RREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_CNTL);

	if (sdma_rlc_rb_cntl & SDMA_RLC0_RB_CNTL__RB_ENABLE_MASK)
		return true;

	return false;
}

static int kgd_gfx_v9_4_3_hqd_sdma_destroy(struct amdgpu_device *adev, void *mqd,
				    unsigned int utimeout)
{
	struct v9_sdma_mqd *m;
	uint32_t sdma_rlc_reg_offset;
	uint32_t temp;
	unsigned long end_jiffies = (utimeout * HZ / 1000) + jiffies;

	m = get_sdma_mqd(mqd);
	sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev, m->sdma_engine_id,
							m->sdma_queue_id);

	temp = RREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_CNTL);
	temp = temp & ~SDMA_RLC0_RB_CNTL__RB_ENABLE_MASK;
	WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_CNTL, temp);

	while (true) {
		temp = RREG32(sdma_rlc_reg_offset + regSDMA_RLC0_CONTEXT_STATUS);
		if (temp & SDMA_RLC0_CONTEXT_STATUS__IDLE_MASK)
			break;
		if (time_after(jiffies, end_jiffies)) {
			pr_err("SDMA RLC not idle in %s\n", __func__);
			return -ETIME;
		}
		usleep_range(500, 1000);
	}

	WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_DOORBELL, 0);
	WREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_CNTL,
		RREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_CNTL) |
		SDMA_RLC0_RB_CNTL__RB_ENABLE_MASK);

	m->sdmax_rlcx_rb_rptr =
			RREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_RPTR);
	m->sdmax_rlcx_rb_rptr_hi =
			RREG32(sdma_rlc_reg_offset + regSDMA_RLC0_RB_RPTR_HI);

	return 0;
}

static int kgd_gfx_v9_4_3_set_pasid_vmid_mapping(struct amdgpu_device *adev,
			u32 pasid, unsigned int vmid, uint32_t xcc_inst)
{
	unsigned long timeout;
	unsigned int reg;
	unsigned int phy_inst = GET_INST(GC, xcc_inst);
	/* Every two XCCs share one AID */
	unsigned int aid = phy_inst / 2;

	/*
	 * We have to assume that there is no outstanding mapping.
	 * The ATC_VMID_PASID_MAPPING_UPDATE_STATUS bit could be 0 because
	 * a mapping is in progress or because a mapping finished
	 * and the SW cleared it.
	 * So the protocol is to always wait & clear.
	 */
	uint32_t pasid_mapping = (pasid == 0) ? 0 : (uint32_t)pasid |
			ATC_VMID0_PASID_MAPPING__VALID_MASK;

	WREG32(SOC15_REG_OFFSET(ATHUB, 0,
		regATC_VMID0_PASID_MAPPING) + vmid, pasid_mapping);

	timeout = jiffies + msecs_to_jiffies(10);
	while (!(RREG32(SOC15_REG_OFFSET(ATHUB, 0,
			regATC_VMID_PASID_MAPPING_UPDATE_STATUS)) &
			(1U << vmid))) {
		if (time_after(jiffies, timeout)) {
			pr_err("Fail to program VMID-PASID mapping\n");
			return -ETIME;
		}
		cpu_relax();
	}

	WREG32(SOC15_REG_OFFSET(ATHUB, 0,
		regATC_VMID_PASID_MAPPING_UPDATE_STATUS),
		1U << vmid);

	reg = RREG32(SOC15_REG_OFFSET(OSSSYS, 0, regIH_VMID_LUT_INDEX));
	/* Every 4 numbers is a cycle. 1st is AID, 2nd and 3rd are XCDs,
	 * and the 4th is reserved. Therefore "aid * 4 + (xcc_inst % 2) + 1"
	 * programs _LUT for XCC and "aid * 4" for AID where the XCC connects
	 * to.
	 */
	WREG32(SOC15_REG_OFFSET(OSSSYS, 0, regIH_VMID_LUT_INDEX),
		aid * 4 + (phy_inst % 2) + 1);
	WREG32(SOC15_REG_OFFSET(OSSSYS, 0, regIH_VMID_0_LUT) + vmid,
		pasid_mapping);
	WREG32(SOC15_REG_OFFSET(OSSSYS, 0, regIH_VMID_LUT_INDEX),
		aid * 4);
	WREG32(SOC15_REG_OFFSET(OSSSYS, 0, regIH_VMID_0_LUT_MM) + vmid,
		pasid_mapping);
	WREG32(SOC15_REG_OFFSET(OSSSYS, 0, regIH_VMID_LUT_INDEX), reg);

	return 0;
}

static inline struct v9_mqd *get_mqd(void *mqd)
{
	return (struct v9_mqd *)mqd;
}

static int kgd_gfx_v9_4_3_hqd_load(struct amdgpu_device *adev, void *mqd,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t __user *wptr, uint32_t wptr_shift,
			uint32_t wptr_mask, struct mm_struct *mm, uint32_t inst)
{
	struct v9_mqd *m;
	uint32_t *mqd_hqd;
	uint32_t reg, hqd_base, hqd_end, data;

	m = get_mqd(mqd);

	kgd_gfx_v9_acquire_queue(adev, pipe_id, queue_id, inst);

	/* HQD registers extend to CP_HQD_AQL_DISPATCH_ID_HI */
	mqd_hqd = &m->cp_mqd_base_addr_lo;
	hqd_base = SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regCP_MQD_BASE_ADDR);
	hqd_end = SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regCP_HQD_AQL_DISPATCH_ID_HI);

	for (reg = hqd_base; reg <= hqd_end; reg++)
		WREG32_RLC(reg, mqd_hqd[reg - hqd_base]);


	/* Activate doorbell logic before triggering WPTR poll. */
	data = REG_SET_FIELD(m->cp_hqd_pq_doorbell_control,
			     CP_HQD_PQ_DOORBELL_CONTROL, DOORBELL_EN, 1);
	WREG32_RLC(SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regCP_HQD_PQ_DOORBELL_CONTROL),
				data);

	if (wptr) {
		/* Don't read wptr with get_user because the user
		 * context may not be accessible (if this function
		 * runs in a work queue). Instead trigger a one-shot
		 * polling read from memory in the CP. This assumes
		 * that wptr is GPU-accessible in the queue's VMID via
		 * ATC or SVM. WPTR==RPTR before starting the poll so
		 * the CP starts fetching new commands from the right
		 * place.
		 *
		 * Guessing a 64-bit WPTR from a 32-bit RPTR is a bit
		 * tricky. Assume that the queue didn't overflow. The
		 * number of valid bits in the 32-bit RPTR depends on
		 * the queue size. The remaining bits are taken from
		 * the saved 64-bit WPTR. If the WPTR wrapped, add the
		 * queue size.
		 */
		uint32_t queue_size =
			2 << REG_GET_FIELD(m->cp_hqd_pq_control,
					   CP_HQD_PQ_CONTROL, QUEUE_SIZE);
		uint64_t guessed_wptr = m->cp_hqd_pq_rptr & (queue_size - 1);

		if ((m->cp_hqd_pq_wptr_lo & (queue_size - 1)) < guessed_wptr)
			guessed_wptr += queue_size;
		guessed_wptr += m->cp_hqd_pq_wptr_lo & ~(queue_size - 1);
		guessed_wptr += (uint64_t)m->cp_hqd_pq_wptr_hi << 32;

		WREG32_RLC(SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regCP_HQD_PQ_WPTR_LO),
		       lower_32_bits(guessed_wptr));
		WREG32_RLC(SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regCP_HQD_PQ_WPTR_HI),
		       upper_32_bits(guessed_wptr));
		WREG32_RLC(SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regCP_HQD_PQ_WPTR_POLL_ADDR),
		       lower_32_bits((uintptr_t)wptr));
		WREG32_RLC(SOC15_REG_OFFSET(GC, GET_INST(GC, inst),
			regCP_HQD_PQ_WPTR_POLL_ADDR_HI),
			upper_32_bits((uintptr_t)wptr));
		WREG32(SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regCP_PQ_WPTR_POLL_CNTL1),
		       (uint32_t)kgd_gfx_v9_get_queue_mask(adev, pipe_id,
			       queue_id));
	}

	/* Start the EOP fetcher */
	WREG32_RLC(SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regCP_HQD_EOP_RPTR),
	       REG_SET_FIELD(m->cp_hqd_eop_rptr,
			     CP_HQD_EOP_RPTR, INIT_FETCHER, 1));

	data = REG_SET_FIELD(m->cp_hqd_active, CP_HQD_ACTIVE, ACTIVE, 1);
	WREG32_RLC(SOC15_REG_OFFSET(GC, GET_INST(GC, inst), regCP_HQD_ACTIVE), data);

	kgd_gfx_v9_release_queue(adev, inst);

	return 0;
}

const struct kfd2kgd_calls gc_9_4_3_kfd2kgd = {
	.program_sh_mem_settings = kgd_gfx_v9_program_sh_mem_settings,
	.set_pasid_vmid_mapping = kgd_gfx_v9_4_3_set_pasid_vmid_mapping,
	.init_interrupts = kgd_gfx_v9_init_interrupts,
	.hqd_load = kgd_gfx_v9_4_3_hqd_load,
	.hiq_mqd_load = kgd_gfx_v9_hiq_mqd_load,
	.hqd_sdma_load = kgd_gfx_v9_4_3_hqd_sdma_load,
	.hqd_dump = kgd_gfx_v9_hqd_dump,
	.hqd_sdma_dump = kgd_gfx_v9_4_3_hqd_sdma_dump,
	.hqd_is_occupied = kgd_gfx_v9_hqd_is_occupied,
	.hqd_sdma_is_occupied = kgd_gfx_v9_4_3_hqd_sdma_is_occupied,
	.hqd_destroy = kgd_gfx_v9_hqd_destroy,
	.hqd_sdma_destroy = kgd_gfx_v9_4_3_hqd_sdma_destroy,
	.wave_control_execute = kgd_gfx_v9_wave_control_execute,
	.get_atc_vmid_pasid_mapping_info =
				kgd_gfx_v9_get_atc_vmid_pasid_mapping_info,
	.set_vm_context_page_table_base =
				kgd_gfx_v9_set_vm_context_page_table_base,
	.program_trap_handler_settings =
				kgd_gfx_v9_program_trap_handler_settings
};
