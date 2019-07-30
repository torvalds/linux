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
#include <linux/firmware.h>
#include <drm/drm_cache.h>
#include "amdgpu.h"
#include "gmc_v9_0.h"
#include "amdgpu_atomfirmware.h"
#include "amdgpu_gem.h"

#include "hdp/hdp_4_0_offset.h"
#include "hdp/hdp_4_0_sh_mask.h"
#include "gc/gc_9_0_sh_mask.h"
#include "dce/dce_12_0_offset.h"
#include "dce/dce_12_0_sh_mask.h"
#include "vega10_enum.h"
#include "mmhub/mmhub_1_0_offset.h"
#include "athub/athub_1_0_offset.h"
#include "oss/osssys_4_0_offset.h"

#include "soc15.h"
#include "soc15_common.h"
#include "umc/umc_6_0_sh_mask.h"

#include "gfxhub_v1_0.h"
#include "mmhub_v1_0.h"
#include "gfxhub_v1_1.h"

#include "ivsrcid/vmc/irqsrcs_vmc_1_0.h"

/* add these here since we already include dce12 headers and these are for DCN */
#define mmHUBP0_DCSURF_PRI_VIEWPORT_DIMENSION                                                          0x055d
#define mmHUBP0_DCSURF_PRI_VIEWPORT_DIMENSION_BASE_IDX                                                 2
#define HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION__PRI_VIEWPORT_WIDTH__SHIFT                                        0x0
#define HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION__PRI_VIEWPORT_HEIGHT__SHIFT                                       0x10
#define HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION__PRI_VIEWPORT_WIDTH_MASK                                          0x00003FFFL
#define HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION__PRI_VIEWPORT_HEIGHT_MASK                                         0x3FFF0000L

/* XXX Move this macro to VEGA10 header file, which is like vid.h for VI.*/
#define AMDGPU_NUM_OF_VMIDS			8

static const u32 golden_settings_vega10_hdp[] =
{
	0xf64, 0x0fffffff, 0x00000000,
	0xf65, 0x0fffffff, 0x00000000,
	0xf66, 0x0fffffff, 0x00000000,
	0xf67, 0x0fffffff, 0x00000000,
	0xf68, 0x0fffffff, 0x00000000,
	0xf6a, 0x0fffffff, 0x00000000,
	0xf6b, 0x0fffffff, 0x00000000,
	0xf6c, 0x0fffffff, 0x00000000,
	0xf6d, 0x0fffffff, 0x00000000,
	0xf6e, 0x0fffffff, 0x00000000,
};

static const struct soc15_reg_golden golden_settings_mmhub_1_0_0[] =
{
	SOC15_REG_GOLDEN_VALUE(MMHUB, 0, mmDAGB1_WRCLI2, 0x00000007, 0xfe5fe0fa),
	SOC15_REG_GOLDEN_VALUE(MMHUB, 0, mmMMEA1_DRAM_WR_CLI2GRP_MAP0, 0x00000030, 0x55555565)
};

static const struct soc15_reg_golden golden_settings_athub_1_0_0[] =
{
	SOC15_REG_GOLDEN_VALUE(ATHUB, 0, mmRPB_ARB_CNTL, 0x0000ff00, 0x00000800),
	SOC15_REG_GOLDEN_VALUE(ATHUB, 0, mmRPB_ARB_CNTL2, 0x00ff00ff, 0x00080008)
};

/* Ecc related register addresses, (BASE + reg offset) */
/* Universal Memory Controller caps (may be fused). */
/* UMCCH:UmcLocalCap */
#define UMCLOCALCAPS_ADDR0	(0x00014306 + 0x00000000)
#define UMCLOCALCAPS_ADDR1	(0x00014306 + 0x00000800)
#define UMCLOCALCAPS_ADDR2	(0x00014306 + 0x00001000)
#define UMCLOCALCAPS_ADDR3	(0x00014306 + 0x00001800)
#define UMCLOCALCAPS_ADDR4	(0x00054306 + 0x00000000)
#define UMCLOCALCAPS_ADDR5	(0x00054306 + 0x00000800)
#define UMCLOCALCAPS_ADDR6	(0x00054306 + 0x00001000)
#define UMCLOCALCAPS_ADDR7	(0x00054306 + 0x00001800)
#define UMCLOCALCAPS_ADDR8	(0x00094306 + 0x00000000)
#define UMCLOCALCAPS_ADDR9	(0x00094306 + 0x00000800)
#define UMCLOCALCAPS_ADDR10	(0x00094306 + 0x00001000)
#define UMCLOCALCAPS_ADDR11	(0x00094306 + 0x00001800)
#define UMCLOCALCAPS_ADDR12	(0x000d4306 + 0x00000000)
#define UMCLOCALCAPS_ADDR13	(0x000d4306 + 0x00000800)
#define UMCLOCALCAPS_ADDR14	(0x000d4306 + 0x00001000)
#define UMCLOCALCAPS_ADDR15	(0x000d4306 + 0x00001800)

/* Universal Memory Controller Channel config. */
/* UMCCH:UMC_CONFIG */
#define UMCCH_UMC_CONFIG_ADDR0	(0x00014040 + 0x00000000)
#define UMCCH_UMC_CONFIG_ADDR1	(0x00014040 + 0x00000800)
#define UMCCH_UMC_CONFIG_ADDR2	(0x00014040 + 0x00001000)
#define UMCCH_UMC_CONFIG_ADDR3	(0x00014040 + 0x00001800)
#define UMCCH_UMC_CONFIG_ADDR4	(0x00054040 + 0x00000000)
#define UMCCH_UMC_CONFIG_ADDR5	(0x00054040 + 0x00000800)
#define UMCCH_UMC_CONFIG_ADDR6	(0x00054040 + 0x00001000)
#define UMCCH_UMC_CONFIG_ADDR7	(0x00054040 + 0x00001800)
#define UMCCH_UMC_CONFIG_ADDR8	(0x00094040 + 0x00000000)
#define UMCCH_UMC_CONFIG_ADDR9	(0x00094040 + 0x00000800)
#define UMCCH_UMC_CONFIG_ADDR10	(0x00094040 + 0x00001000)
#define UMCCH_UMC_CONFIG_ADDR11	(0x00094040 + 0x00001800)
#define UMCCH_UMC_CONFIG_ADDR12	(0x000d4040 + 0x00000000)
#define UMCCH_UMC_CONFIG_ADDR13	(0x000d4040 + 0x00000800)
#define UMCCH_UMC_CONFIG_ADDR14	(0x000d4040 + 0x00001000)
#define UMCCH_UMC_CONFIG_ADDR15	(0x000d4040 + 0x00001800)

/* Universal Memory Controller Channel Ecc config. */
/* UMCCH:EccCtrl */
#define UMCCH_ECCCTRL_ADDR0	(0x00014053 + 0x00000000)
#define UMCCH_ECCCTRL_ADDR1	(0x00014053 + 0x00000800)
#define UMCCH_ECCCTRL_ADDR2	(0x00014053 + 0x00001000)
#define UMCCH_ECCCTRL_ADDR3	(0x00014053 + 0x00001800)
#define UMCCH_ECCCTRL_ADDR4	(0x00054053 + 0x00000000)
#define UMCCH_ECCCTRL_ADDR5	(0x00054053 + 0x00000800)
#define UMCCH_ECCCTRL_ADDR6	(0x00054053 + 0x00001000)
#define UMCCH_ECCCTRL_ADDR7	(0x00054053 + 0x00001800)
#define UMCCH_ECCCTRL_ADDR8	(0x00094053 + 0x00000000)
#define UMCCH_ECCCTRL_ADDR9	(0x00094053 + 0x00000800)
#define UMCCH_ECCCTRL_ADDR10	(0x00094053 + 0x00001000)
#define UMCCH_ECCCTRL_ADDR11	(0x00094053 + 0x00001800)
#define UMCCH_ECCCTRL_ADDR12	(0x000d4053 + 0x00000000)
#define UMCCH_ECCCTRL_ADDR13	(0x000d4053 + 0x00000800)
#define UMCCH_ECCCTRL_ADDR14	(0x000d4053 + 0x00001000)
#define UMCCH_ECCCTRL_ADDR15	(0x000d4053 + 0x00001800)

static const uint32_t ecc_umclocalcap_addrs[] = {
	UMCLOCALCAPS_ADDR0,
	UMCLOCALCAPS_ADDR1,
	UMCLOCALCAPS_ADDR2,
	UMCLOCALCAPS_ADDR3,
	UMCLOCALCAPS_ADDR4,
	UMCLOCALCAPS_ADDR5,
	UMCLOCALCAPS_ADDR6,
	UMCLOCALCAPS_ADDR7,
	UMCLOCALCAPS_ADDR8,
	UMCLOCALCAPS_ADDR9,
	UMCLOCALCAPS_ADDR10,
	UMCLOCALCAPS_ADDR11,
	UMCLOCALCAPS_ADDR12,
	UMCLOCALCAPS_ADDR13,
	UMCLOCALCAPS_ADDR14,
	UMCLOCALCAPS_ADDR15,
};

static const uint32_t ecc_umcch_umc_config_addrs[] = {
	UMCCH_UMC_CONFIG_ADDR0,
	UMCCH_UMC_CONFIG_ADDR1,
	UMCCH_UMC_CONFIG_ADDR2,
	UMCCH_UMC_CONFIG_ADDR3,
	UMCCH_UMC_CONFIG_ADDR4,
	UMCCH_UMC_CONFIG_ADDR5,
	UMCCH_UMC_CONFIG_ADDR6,
	UMCCH_UMC_CONFIG_ADDR7,
	UMCCH_UMC_CONFIG_ADDR8,
	UMCCH_UMC_CONFIG_ADDR9,
	UMCCH_UMC_CONFIG_ADDR10,
	UMCCH_UMC_CONFIG_ADDR11,
	UMCCH_UMC_CONFIG_ADDR12,
	UMCCH_UMC_CONFIG_ADDR13,
	UMCCH_UMC_CONFIG_ADDR14,
	UMCCH_UMC_CONFIG_ADDR15,
};

static const uint32_t ecc_umcch_eccctrl_addrs[] = {
	UMCCH_ECCCTRL_ADDR0,
	UMCCH_ECCCTRL_ADDR1,
	UMCCH_ECCCTRL_ADDR2,
	UMCCH_ECCCTRL_ADDR3,
	UMCCH_ECCCTRL_ADDR4,
	UMCCH_ECCCTRL_ADDR5,
	UMCCH_ECCCTRL_ADDR6,
	UMCCH_ECCCTRL_ADDR7,
	UMCCH_ECCCTRL_ADDR8,
	UMCCH_ECCCTRL_ADDR9,
	UMCCH_ECCCTRL_ADDR10,
	UMCCH_ECCCTRL_ADDR11,
	UMCCH_ECCCTRL_ADDR12,
	UMCCH_ECCCTRL_ADDR13,
	UMCCH_ECCCTRL_ADDR14,
	UMCCH_ECCCTRL_ADDR15,
};

static int gmc_v9_0_vm_fault_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *src,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	struct amdgpu_vmhub *hub;
	u32 tmp, reg, bits, i, j;

	bits = VM_CONTEXT1_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		VM_CONTEXT1_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		VM_CONTEXT1_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		VM_CONTEXT1_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		VM_CONTEXT1_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		VM_CONTEXT1_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		VM_CONTEXT1_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		for (j = 0; j < AMDGPU_MAX_VMHUBS; j++) {
			hub = &adev->vmhub[j];
			for (i = 0; i < 16; i++) {
				reg = hub->vm_context0_cntl + i;
				tmp = RREG32(reg);
				tmp &= ~bits;
				WREG32(reg, tmp);
			}
		}
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		for (j = 0; j < AMDGPU_MAX_VMHUBS; j++) {
			hub = &adev->vmhub[j];
			for (i = 0; i < 16; i++) {
				reg = hub->vm_context0_cntl + i;
				tmp = RREG32(reg);
				tmp |= bits;
				WREG32(reg, tmp);
			}
		}
	default:
		break;
	}

	return 0;
}

/**
 * vega10_ih_prescreen_iv - prescreen an interrupt vector
 *
 * @adev: amdgpu_device pointer
 *
 * Returns true if the interrupt vector should be further processed.
 */
static bool gmc_v9_0_prescreen_iv(struct amdgpu_device *adev,
				  struct amdgpu_iv_entry *entry,
				  uint64_t addr)
{
	struct amdgpu_vm *vm;
	u64 key;
	int r;

	/* No PASID, can't identify faulting process */
	if (!entry->pasid)
		return true;

	/* Not a retry fault */
	if (!(entry->src_data[1] & 0x80))
		return true;

	/* Track retry faults in per-VM fault FIFO. */
	spin_lock(&adev->vm_manager.pasid_lock);
	vm = idr_find(&adev->vm_manager.pasid_idr, entry->pasid);
	if (!vm) {
		/* VM not found, process it normally */
		spin_unlock(&adev->vm_manager.pasid_lock);
		return true;
	}

	key = AMDGPU_VM_FAULT(entry->pasid, addr);
	r = amdgpu_vm_add_fault(vm->fault_hash, key);

	/* Hash table is full or the fault is already being processed,
	 * ignore further page faults
	 */
	if (r != 0) {
		spin_unlock(&adev->vm_manager.pasid_lock);
		return false;
	}
	/* No locking required with single writer and single reader */
	r = kfifo_put(&vm->faults, key);
	if (!r) {
		/* FIFO is full. Ignore it until there is space */
		amdgpu_vm_clear_fault(vm->fault_hash, key);
		spin_unlock(&adev->vm_manager.pasid_lock);
		return false;
	}

	spin_unlock(&adev->vm_manager.pasid_lock);
	/* It's the first fault for this address, process it normally */
	return true;
}

static int gmc_v9_0_process_interrupt(struct amdgpu_device *adev,
				struct amdgpu_irq_src *source,
				struct amdgpu_iv_entry *entry)
{
	struct amdgpu_vmhub *hub = &adev->vmhub[entry->vmid_src];
	bool retry_fault = !!(entry->src_data[1] & 0x80);
	uint32_t status = 0;
	u64 addr;

	addr = (u64)entry->src_data[0] << 12;
	addr |= ((u64)entry->src_data[1] & 0xf) << 44;

	if (!gmc_v9_0_prescreen_iv(adev, entry, addr))
		return 1; /* This also prevents sending it to KFD */

	if (!amdgpu_sriov_vf(adev)) {
		status = RREG32(hub->vm_l2_pro_fault_status);
		WREG32_P(hub->vm_l2_pro_fault_cntl, 1, ~1);
	}

	if (printk_ratelimit()) {
		struct amdgpu_task_info task_info;

		memset(&task_info, 0, sizeof(struct amdgpu_task_info));
		amdgpu_vm_get_task_info(adev, entry->pasid, &task_info);

		dev_err(adev->dev,
			"[%s] %s page fault (src_id:%u ring:%u vmid:%u "
			"pasid:%u, for process %s pid %d thread %s pid %d)\n",
			entry->vmid_src ? "mmhub" : "gfxhub",
			retry_fault ? "retry" : "no-retry",
			entry->src_id, entry->ring_id, entry->vmid,
			entry->pasid, task_info.process_name, task_info.tgid,
			task_info.task_name, task_info.pid);
		dev_err(adev->dev, "  in page starting at address 0x%016llx from %d\n",
			addr, entry->client_id);
		if (!amdgpu_sriov_vf(adev))
			dev_err(adev->dev,
				"VM_L2_PROTECTION_FAULT_STATUS:0x%08X\n",
				status);
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs gmc_v9_0_irq_funcs = {
	.set = gmc_v9_0_vm_fault_interrupt_state,
	.process = gmc_v9_0_process_interrupt,
};

static void gmc_v9_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->gmc.vm_fault.num_types = 1;
	adev->gmc.vm_fault.funcs = &gmc_v9_0_irq_funcs;
}

static uint32_t gmc_v9_0_get_invalidate_req(unsigned int vmid,
					uint32_t flush_type)
{
	u32 req = 0;

	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ,
			    PER_VMID_INVALIDATE_REQ, 1 << vmid);
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ, FLUSH_TYPE, flush_type);
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PTES, 1);
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE0, 1);
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE1, 1);
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE2, 1);
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ, INVALIDATE_L1_PTES, 1);
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ,
			    CLEAR_PROTECTION_FAULT_STATUS_ADDR,	0);

	return req;
}

/*
 * GART
 * VMID 0 is the physical GPU addresses as used by the kernel.
 * VMIDs 1-15 are used for userspace clients and are handled
 * by the amdgpu vm/hsa code.
 */

/**
 * gmc_v9_0_flush_gpu_tlb - tlb flush with certain type
 *
 * @adev: amdgpu_device pointer
 * @vmid: vm instance to flush
 * @flush_type: the flush type
 *
 * Flush the TLB for the requested page table using certain type.
 */
static void gmc_v9_0_flush_gpu_tlb(struct amdgpu_device *adev,
				uint32_t vmid, uint32_t flush_type)
{
	const unsigned eng = 17;
	unsigned i, j;

	for (i = 0; i < AMDGPU_MAX_VMHUBS; ++i) {
		struct amdgpu_vmhub *hub = &adev->vmhub[i];
		u32 tmp = gmc_v9_0_get_invalidate_req(vmid, flush_type);

		/* This is necessary for a HW workaround under SRIOV as well
		 * as GFXOFF under bare metal
		 */
		if (adev->gfx.kiq.ring.sched.ready &&
		    (amdgpu_sriov_runtime(adev) || !amdgpu_sriov_vf(adev)) &&
		    !adev->in_gpu_reset) {
			uint32_t req = hub->vm_inv_eng0_req + eng;
			uint32_t ack = hub->vm_inv_eng0_ack + eng;

			amdgpu_virt_kiq_reg_write_reg_wait(adev, req, ack, tmp,
							   1 << vmid);
			continue;
		}

		spin_lock(&adev->gmc.invalidate_lock);
		WREG32_NO_KIQ(hub->vm_inv_eng0_req + eng, tmp);
		for (j = 0; j < adev->usec_timeout; j++) {
			tmp = RREG32_NO_KIQ(hub->vm_inv_eng0_ack + eng);
			if (tmp & (1 << vmid))
				break;
			udelay(1);
		}
		spin_unlock(&adev->gmc.invalidate_lock);
		if (j < adev->usec_timeout)
			continue;

		DRM_ERROR("Timeout waiting for VM flush ACK!\n");
	}
}

static uint64_t gmc_v9_0_emit_flush_gpu_tlb(struct amdgpu_ring *ring,
					    unsigned vmid, uint64_t pd_addr)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_vmhub *hub = &adev->vmhub[ring->funcs->vmhub];
	uint32_t req = gmc_v9_0_get_invalidate_req(vmid, 0);
	unsigned eng = ring->vm_inv_eng;

	amdgpu_ring_emit_wreg(ring, hub->ctx0_ptb_addr_lo32 + (2 * vmid),
			      lower_32_bits(pd_addr));

	amdgpu_ring_emit_wreg(ring, hub->ctx0_ptb_addr_hi32 + (2 * vmid),
			      upper_32_bits(pd_addr));

	amdgpu_ring_emit_reg_write_reg_wait(ring, hub->vm_inv_eng0_req + eng,
					    hub->vm_inv_eng0_ack + eng,
					    req, 1 << vmid);

	return pd_addr;
}

static void gmc_v9_0_emit_pasid_mapping(struct amdgpu_ring *ring, unsigned vmid,
					unsigned pasid)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t reg;

	if (ring->funcs->vmhub == AMDGPU_GFXHUB)
		reg = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_VMID_0_LUT) + vmid;
	else
		reg = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_VMID_0_LUT_MM) + vmid;

	amdgpu_ring_emit_wreg(ring, reg, pasid);
}

/**
 * gmc_v9_0_set_pte_pde - update the page tables using MMIO
 *
 * @adev: amdgpu_device pointer
 * @cpu_pt_addr: cpu address of the page table
 * @gpu_page_idx: entry in the page table to update
 * @addr: dst addr to write into pte/pde
 * @flags: access flags
 *
 * Update the page tables using the CPU.
 */
static int gmc_v9_0_set_pte_pde(struct amdgpu_device *adev, void *cpu_pt_addr,
				uint32_t gpu_page_idx, uint64_t addr,
				uint64_t flags)
{
	void __iomem *ptr = (void *)cpu_pt_addr;
	uint64_t value;

	/*
	 * PTE format on VEGA 10:
	 * 63:59 reserved
	 * 58:57 mtype
	 * 56 F
	 * 55 L
	 * 54 P
	 * 53 SW
	 * 52 T
	 * 50:48 reserved
	 * 47:12 4k physical page base address
	 * 11:7 fragment
	 * 6 write
	 * 5 read
	 * 4 exe
	 * 3 Z
	 * 2 snooped
	 * 1 system
	 * 0 valid
	 *
	 * PDE format on VEGA 10:
	 * 63:59 block fragment size
	 * 58:55 reserved
	 * 54 P
	 * 53:48 reserved
	 * 47:6 physical base address of PD or PTE
	 * 5:3 reserved
	 * 2 C
	 * 1 system
	 * 0 valid
	 */

	/*
	 * The following is for PTE only. GART does not have PDEs.
	*/
	value = addr & 0x0000FFFFFFFFF000ULL;
	value |= flags;
	writeq(value, ptr + (gpu_page_idx * 8));
	return 0;
}

static uint64_t gmc_v9_0_get_vm_pte_flags(struct amdgpu_device *adev,
						uint32_t flags)

{
	uint64_t pte_flag = 0;

	if (flags & AMDGPU_VM_PAGE_EXECUTABLE)
		pte_flag |= AMDGPU_PTE_EXECUTABLE;
	if (flags & AMDGPU_VM_PAGE_READABLE)
		pte_flag |= AMDGPU_PTE_READABLE;
	if (flags & AMDGPU_VM_PAGE_WRITEABLE)
		pte_flag |= AMDGPU_PTE_WRITEABLE;

	switch (flags & AMDGPU_VM_MTYPE_MASK) {
	case AMDGPU_VM_MTYPE_DEFAULT:
		pte_flag |= AMDGPU_PTE_MTYPE(MTYPE_NC);
		break;
	case AMDGPU_VM_MTYPE_NC:
		pte_flag |= AMDGPU_PTE_MTYPE(MTYPE_NC);
		break;
	case AMDGPU_VM_MTYPE_WC:
		pte_flag |= AMDGPU_PTE_MTYPE(MTYPE_WC);
		break;
	case AMDGPU_VM_MTYPE_CC:
		pte_flag |= AMDGPU_PTE_MTYPE(MTYPE_CC);
		break;
	case AMDGPU_VM_MTYPE_UC:
		pte_flag |= AMDGPU_PTE_MTYPE(MTYPE_UC);
		break;
	default:
		pte_flag |= AMDGPU_PTE_MTYPE(MTYPE_NC);
		break;
	}

	if (flags & AMDGPU_VM_PAGE_PRT)
		pte_flag |= AMDGPU_PTE_PRT;

	return pte_flag;
}

static void gmc_v9_0_get_vm_pde(struct amdgpu_device *adev, int level,
				uint64_t *addr, uint64_t *flags)
{
	if (!(*flags & AMDGPU_PDE_PTE) && !(*flags & AMDGPU_PTE_SYSTEM))
		*addr = adev->vm_manager.vram_base_offset + *addr -
			adev->gmc.vram_start;
	BUG_ON(*addr & 0xFFFF00000000003FULL);

	if (!adev->gmc.translate_further)
		return;

	if (level == AMDGPU_VM_PDB1) {
		/* Set the block fragment size */
		if (!(*flags & AMDGPU_PDE_PTE))
			*flags |= AMDGPU_PDE_BFS(0x9);

	} else if (level == AMDGPU_VM_PDB0) {
		if (*flags & AMDGPU_PDE_PTE)
			*flags &= ~AMDGPU_PDE_PTE;
		else
			*flags |= AMDGPU_PTE_TF;
	}
}

static const struct amdgpu_gmc_funcs gmc_v9_0_gmc_funcs = {
	.flush_gpu_tlb = gmc_v9_0_flush_gpu_tlb,
	.emit_flush_gpu_tlb = gmc_v9_0_emit_flush_gpu_tlb,
	.emit_pasid_mapping = gmc_v9_0_emit_pasid_mapping,
	.set_pte_pde = gmc_v9_0_set_pte_pde,
	.get_vm_pte_flags = gmc_v9_0_get_vm_pte_flags,
	.get_vm_pde = gmc_v9_0_get_vm_pde
};

static void gmc_v9_0_set_gmc_funcs(struct amdgpu_device *adev)
{
	adev->gmc.gmc_funcs = &gmc_v9_0_gmc_funcs;
}

static int gmc_v9_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gmc_v9_0_set_gmc_funcs(adev);
	gmc_v9_0_set_irq_funcs(adev);

	adev->gmc.shared_aperture_start = 0x2000000000000000ULL;
	adev->gmc.shared_aperture_end =
		adev->gmc.shared_aperture_start + (4ULL << 30) - 1;
	adev->gmc.private_aperture_start = 0x1000000000000000ULL;
	adev->gmc.private_aperture_end =
		adev->gmc.private_aperture_start + (4ULL << 30) - 1;

	return 0;
}

static int gmc_v9_0_ecc_available(struct amdgpu_device *adev)
{
	uint32_t reg_val;
	uint32_t reg_addr;
	uint32_t field_val;
	size_t i;
	uint32_t fv2;
	size_t lost_sheep;

	DRM_DEBUG("ecc: gmc_v9_0_ecc_available()\n");

	lost_sheep = 0;
	for (i = 0; i < ARRAY_SIZE(ecc_umclocalcap_addrs); ++i) {
		reg_addr = ecc_umclocalcap_addrs[i];
		DRM_DEBUG("ecc: "
			  "UMCCH_UmcLocalCap[%zu]: reg_addr: 0x%08x\n",
			  i, reg_addr);
		reg_val = RREG32(reg_addr);
		field_val = REG_GET_FIELD(reg_val, UMCCH0_0_UmcLocalCap,
					  EccDis);
		DRM_DEBUG("ecc: "
			  "reg_val: 0x%08x, "
			  "EccDis: 0x%08x, ",
			  reg_val, field_val);
		if (field_val) {
			DRM_ERROR("ecc: UmcLocalCap:EccDis is set.\n");
			++lost_sheep;
		}
	}

	for (i = 0; i < ARRAY_SIZE(ecc_umcch_umc_config_addrs); ++i) {
		reg_addr = ecc_umcch_umc_config_addrs[i];
		DRM_DEBUG("ecc: "
			  "UMCCH0_0_UMC_CONFIG[%zu]: reg_addr: 0x%08x",
			  i, reg_addr);
		reg_val = RREG32(reg_addr);
		field_val = REG_GET_FIELD(reg_val, UMCCH0_0_UMC_CONFIG,
					  DramReady);
		DRM_DEBUG("ecc: "
			  "reg_val: 0x%08x, "
			  "DramReady: 0x%08x\n",
			  reg_val, field_val);

		if (!field_val) {
			DRM_ERROR("ecc: UMC_CONFIG:DramReady is not set.\n");
			++lost_sheep;
		}
	}

	for (i = 0; i < ARRAY_SIZE(ecc_umcch_eccctrl_addrs); ++i) {
		reg_addr = ecc_umcch_eccctrl_addrs[i];
		DRM_DEBUG("ecc: "
			  "UMCCH_EccCtrl[%zu]: reg_addr: 0x%08x, ",
			  i, reg_addr);
		reg_val = RREG32(reg_addr);
		field_val = REG_GET_FIELD(reg_val, UMCCH0_0_EccCtrl,
					  WrEccEn);
		fv2 = REG_GET_FIELD(reg_val, UMCCH0_0_EccCtrl,
				    RdEccEn);
		DRM_DEBUG("ecc: "
			  "reg_val: 0x%08x, "
			  "WrEccEn: 0x%08x, "
			  "RdEccEn: 0x%08x\n",
			  reg_val, field_val, fv2);

		if (!field_val) {
			DRM_DEBUG("ecc: WrEccEn is not set\n");
			++lost_sheep;
		}
		if (!fv2) {
			DRM_DEBUG("ecc: RdEccEn is not set\n");
			++lost_sheep;
		}
	}

	DRM_DEBUG("ecc: lost_sheep: %zu\n", lost_sheep);
	return lost_sheep == 0;
}

static bool gmc_v9_0_keep_stolen_memory(struct amdgpu_device *adev)
{

	/*
	 * TODO:
	 * Currently there is a bug where some memory client outside
	 * of the driver writes to first 8M of VRAM on S3 resume,
	 * this overrides GART which by default gets placed in first 8M and
	 * causes VM_FAULTS once GTT is accessed.
	 * Keep the stolen memory reservation until the while this is not solved.
	 * Also check code in gmc_v9_0_get_vbios_fb_size and gmc_v9_0_late_init
	 */
	switch (adev->asic_type) {
	case CHIP_VEGA10:
		return true;
	case CHIP_RAVEN:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
	default:
		return false;
	}
}

static int gmc_v9_0_allocate_vm_inv_eng(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	unsigned vm_inv_engs[AMDGPU_MAX_VMHUBS] =
		{GFXHUB_FREE_VM_INV_ENGS_BITMAP, MMHUB_FREE_VM_INV_ENGS_BITMAP};
	unsigned i;
	unsigned vmhub, inv_eng;

	for (i = 0; i < adev->num_rings; ++i) {
		ring = adev->rings[i];
		vmhub = ring->funcs->vmhub;

		inv_eng = ffs(vm_inv_engs[vmhub]);
		if (!inv_eng) {
			dev_err(adev->dev, "no VM inv eng for ring %s\n",
				ring->name);
			return -EINVAL;
		}

		ring->vm_inv_eng = inv_eng - 1;
		vm_inv_engs[vmhub] &= ~(1 << ring->vm_inv_eng);

		dev_info(adev->dev, "ring %s uses VM inv eng %u on hub %u\n",
			 ring->name, ring->vm_inv_eng, ring->funcs->vmhub);
	}

	return 0;
}

static int gmc_v9_0_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	if (!gmc_v9_0_keep_stolen_memory(adev))
		amdgpu_bo_late_init(adev);

	r = gmc_v9_0_allocate_vm_inv_eng(adev);
	if (r)
		return r;

	if (adev->asic_type == CHIP_VEGA10 && !amdgpu_sriov_vf(adev)) {
		r = gmc_v9_0_ecc_available(adev);
		if (r == 1) {
			DRM_INFO("ECC is active.\n");
		} else if (r == 0) {
			DRM_INFO("ECC is not present.\n");
			adev->df_funcs->enable_ecc_force_par_wr_rmw(adev, false);
		} else {
			DRM_ERROR("gmc_v9_0_ecc_available() failed. r: %d\n", r);
			return r;
		}
	}

	return amdgpu_irq_get(adev, &adev->gmc.vm_fault, 0);
}

static void gmc_v9_0_vram_gtt_location(struct amdgpu_device *adev,
					struct amdgpu_gmc *mc)
{
	u64 base = 0;
	if (!amdgpu_sriov_vf(adev))
		base = mmhub_v1_0_get_fb_location(adev);
	/* add the xgmi offset of the physical node */
	base += adev->gmc.xgmi.physical_node_id * adev->gmc.xgmi.node_segment_size;
	amdgpu_gmc_vram_location(adev, &adev->gmc, base);
	amdgpu_gmc_gart_location(adev, mc);
	if (!amdgpu_sriov_vf(adev))
		amdgpu_gmc_agp_location(adev, mc);
	/* base offset of vram pages */
	adev->vm_manager.vram_base_offset = gfxhub_v1_0_get_mc_fb_offset(adev);

	/* XXX: add the xgmi offset of the physical node? */
	adev->vm_manager.vram_base_offset +=
		adev->gmc.xgmi.physical_node_id * adev->gmc.xgmi.node_segment_size;
}

/**
 * gmc_v9_0_mc_init - initialize the memory controller driver params
 *
 * @adev: amdgpu_device pointer
 *
 * Look up the amount of vram, vram width, and decide how to place
 * vram and gart within the GPU's physical address space.
 * Returns 0 for success.
 */
static int gmc_v9_0_mc_init(struct amdgpu_device *adev)
{
	int chansize, numchan;
	int r;

	if (amdgpu_emu_mode != 1)
		adev->gmc.vram_width = amdgpu_atomfirmware_get_vram_width(adev);
	if (!adev->gmc.vram_width) {
		/* hbm memory channel size */
		if (adev->flags & AMD_IS_APU)
			chansize = 64;
		else
			chansize = 128;

		numchan = adev->df_funcs->get_hbm_channel_number(adev);
		adev->gmc.vram_width = numchan * chansize;
	}

	/* size in MB on si */
	adev->gmc.mc_vram_size =
		adev->nbio_funcs->get_memsize(adev) * 1024ULL * 1024ULL;
	adev->gmc.real_vram_size = adev->gmc.mc_vram_size;

	if (!(adev->flags & AMD_IS_APU)) {
		r = amdgpu_device_resize_fb_bar(adev);
		if (r)
			return r;
	}
	adev->gmc.aper_base = pci_resource_start(adev->pdev, 0);
	adev->gmc.aper_size = pci_resource_len(adev->pdev, 0);

#ifdef CONFIG_X86_64
	if (adev->flags & AMD_IS_APU) {
		adev->gmc.aper_base = gfxhub_v1_0_get_mc_fb_offset(adev);
		adev->gmc.aper_size = adev->gmc.real_vram_size;
	}
#endif
	/* In case the PCI BAR is larger than the actual amount of vram */
	adev->gmc.visible_vram_size = adev->gmc.aper_size;
	if (adev->gmc.visible_vram_size > adev->gmc.real_vram_size)
		adev->gmc.visible_vram_size = adev->gmc.real_vram_size;

	/* set the gart size */
	if (amdgpu_gart_size == -1) {
		switch (adev->asic_type) {
		case CHIP_VEGA10:  /* all engines support GPUVM */
		case CHIP_VEGA12:  /* all engines support GPUVM */
		case CHIP_VEGA20:
		default:
			adev->gmc.gart_size = 512ULL << 20;
			break;
		case CHIP_RAVEN:   /* DCE SG support */
			adev->gmc.gart_size = 1024ULL << 20;
			break;
		}
	} else {
		adev->gmc.gart_size = (u64)amdgpu_gart_size << 20;
	}

	gmc_v9_0_vram_gtt_location(adev, &adev->gmc);

	return 0;
}

static int gmc_v9_0_gart_init(struct amdgpu_device *adev)
{
	int r;

	if (adev->gart.bo) {
		WARN(1, "VEGA10 PCIE GART already initialized\n");
		return 0;
	}
	/* Initialize common gart structure */
	r = amdgpu_gart_init(adev);
	if (r)
		return r;
	adev->gart.table_size = adev->gart.num_gpu_pages * 8;
	adev->gart.gart_pte_flags = AMDGPU_PTE_MTYPE(MTYPE_UC) |
				 AMDGPU_PTE_EXECUTABLE;
	return amdgpu_gart_table_vram_alloc(adev);
}

static unsigned gmc_v9_0_get_vbios_fb_size(struct amdgpu_device *adev)
{
	u32 d1vga_control = RREG32_SOC15(DCE, 0, mmD1VGA_CONTROL);
	unsigned size;

	/*
	 * TODO Remove once GART corruption is resolved
	 * Check related code in gmc_v9_0_sw_fini
	 * */
	if (gmc_v9_0_keep_stolen_memory(adev))
		return 9 * 1024 * 1024;

	if (REG_GET_FIELD(d1vga_control, D1VGA_CONTROL, D1VGA_MODE_ENABLE)) {
		size = 9 * 1024 * 1024; /* reserve 8MB for vga emulator and 1 MB for FB */
	} else {
		u32 viewport;

		switch (adev->asic_type) {
		case CHIP_RAVEN:
			viewport = RREG32_SOC15(DCE, 0, mmHUBP0_DCSURF_PRI_VIEWPORT_DIMENSION);
			size = (REG_GET_FIELD(viewport,
					      HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION, PRI_VIEWPORT_HEIGHT) *
				REG_GET_FIELD(viewport,
					      HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION, PRI_VIEWPORT_WIDTH) *
				4);
			break;
		case CHIP_VEGA10:
		case CHIP_VEGA12:
		case CHIP_VEGA20:
		default:
			viewport = RREG32_SOC15(DCE, 0, mmSCL0_VIEWPORT_SIZE);
			size = (REG_GET_FIELD(viewport, SCL0_VIEWPORT_SIZE, VIEWPORT_HEIGHT) *
				REG_GET_FIELD(viewport, SCL0_VIEWPORT_SIZE, VIEWPORT_WIDTH) *
				4);
			break;
		}
	}
	/* return 0 if the pre-OS buffer uses up most of vram */
	if ((adev->gmc.real_vram_size - size) < (8 * 1024 * 1024))
		return 0;

	return size;
}

static int gmc_v9_0_sw_init(void *handle)
{
	int r;
	int dma_bits;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gfxhub_v1_0_init(adev);
	mmhub_v1_0_init(adev);

	spin_lock_init(&adev->gmc.invalidate_lock);

	adev->gmc.vram_type = amdgpu_atomfirmware_get_vram_type(adev);
	switch (adev->asic_type) {
	case CHIP_RAVEN:
		if (adev->rev_id == 0x0 || adev->rev_id == 0x1) {
			amdgpu_vm_adjust_size(adev, 256 * 1024, 9, 3, 48);
		} else {
			/* vm_size is 128TB + 512GB for legacy 3-level page support */
			amdgpu_vm_adjust_size(adev, 128 * 1024 + 512, 9, 2, 48);
			adev->gmc.translate_further =
				adev->vm_manager.num_level > 1;
		}
		break;
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
		/*
		 * To fulfill 4-level page support,
		 * vm size is 256TB (48bit), maximum size of Vega10,
		 * block size 512 (9bit)
		 */
		/* sriov restrict max_pfn below AMDGPU_GMC_HOLE */
		if (amdgpu_sriov_vf(adev))
			amdgpu_vm_adjust_size(adev, 256 * 1024, 9, 3, 47);
		else
			amdgpu_vm_adjust_size(adev, 256 * 1024, 9, 3, 48);
		break;
	default:
		break;
	}

	/* This interrupt is VMC page fault.*/
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VMC, VMC_1_0__SRCID__VM_FAULT,
				&adev->gmc.vm_fault);
	if (r)
		return r;

	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_UTCL2, UTCL2_1_0__SRCID__FAULT,
				&adev->gmc.vm_fault);

	if (r)
		return r;

	/* Set the internal MC address mask
	 * This is the max address of the GPU's
	 * internal address space.
	 */
	adev->gmc.mc_mask = 0xffffffffffffULL; /* 48 bit MC */

	/* set DMA mask + need_dma32 flags.
	 * PCIE - can handle 44-bits.
	 * IGP - can handle 44-bits
	 * PCI - dma32 for legacy pci gart, 44 bits on vega10
	 */
	adev->need_dma32 = false;
	dma_bits = adev->need_dma32 ? 32 : 44;
	r = pci_set_dma_mask(adev->pdev, DMA_BIT_MASK(dma_bits));
	if (r) {
		adev->need_dma32 = true;
		dma_bits = 32;
		printk(KERN_WARNING "amdgpu: No suitable DMA available.\n");
	}
	r = pci_set_consistent_dma_mask(adev->pdev, DMA_BIT_MASK(dma_bits));
	if (r) {
		pci_set_consistent_dma_mask(adev->pdev, DMA_BIT_MASK(32));
		printk(KERN_WARNING "amdgpu: No coherent DMA available.\n");
	}
	adev->need_swiotlb = drm_get_max_iomem() > ((u64)1 << dma_bits);

	if (adev->gmc.xgmi.supported) {
		r = gfxhub_v1_1_get_xgmi_info(adev);
		if (r)
			return r;
	}

	r = gmc_v9_0_mc_init(adev);
	if (r)
		return r;

	adev->gmc.stolen_size = gmc_v9_0_get_vbios_fb_size(adev);

	/* Memory manager */
	r = amdgpu_bo_init(adev);
	if (r)
		return r;

	r = gmc_v9_0_gart_init(adev);
	if (r)
		return r;

	/*
	 * number of VMs
	 * VMID 0 is reserved for System
	 * amdgpu graphics/compute will use VMIDs 1-7
	 * amdkfd will use VMIDs 8-15
	 */
	adev->vm_manager.id_mgr[AMDGPU_GFXHUB].num_ids = AMDGPU_NUM_OF_VMIDS;
	adev->vm_manager.id_mgr[AMDGPU_MMHUB].num_ids = AMDGPU_NUM_OF_VMIDS;

	amdgpu_vm_manager_init(adev);

	return 0;
}

static int gmc_v9_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_gem_force_release(adev);
	amdgpu_vm_manager_fini(adev);

	if (gmc_v9_0_keep_stolen_memory(adev))
		amdgpu_bo_free_kernel(&adev->stolen_vga_memory, NULL, NULL);

	amdgpu_gart_table_vram_free(adev);
	amdgpu_bo_fini(adev);
	amdgpu_gart_fini(adev);

	return 0;
}

static void gmc_v9_0_init_golden_registers(struct amdgpu_device *adev)
{

	switch (adev->asic_type) {
	case CHIP_VEGA10:
	case CHIP_VEGA20:
		soc15_program_register_sequence(adev,
						golden_settings_mmhub_1_0_0,
						ARRAY_SIZE(golden_settings_mmhub_1_0_0));
		soc15_program_register_sequence(adev,
						golden_settings_athub_1_0_0,
						ARRAY_SIZE(golden_settings_athub_1_0_0));
		break;
	case CHIP_VEGA12:
		break;
	case CHIP_RAVEN:
		soc15_program_register_sequence(adev,
						golden_settings_athub_1_0_0,
						ARRAY_SIZE(golden_settings_athub_1_0_0));
		break;
	default:
		break;
	}
}

/**
 * gmc_v9_0_gart_enable - gart enable
 *
 * @adev: amdgpu_device pointer
 */
static int gmc_v9_0_gart_enable(struct amdgpu_device *adev)
{
	int r;
	bool value;
	u32 tmp;

	amdgpu_device_program_register_sequence(adev,
						golden_settings_vega10_hdp,
						ARRAY_SIZE(golden_settings_vega10_hdp));

	if (adev->gart.bo == NULL) {
		dev_err(adev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}
	r = amdgpu_gart_table_vram_pin(adev);
	if (r)
		return r;

	switch (adev->asic_type) {
	case CHIP_RAVEN:
		mmhub_v1_0_update_power_gating(adev, true);
		break;
	default:
		break;
	}

	r = gfxhub_v1_0_gart_enable(adev);
	if (r)
		return r;

	r = mmhub_v1_0_gart_enable(adev);
	if (r)
		return r;

	WREG32_FIELD15(HDP, 0, HDP_MISC_CNTL, FLUSH_INVALIDATE_CACHE, 1);

	tmp = RREG32_SOC15(HDP, 0, mmHDP_HOST_PATH_CNTL);
	WREG32_SOC15(HDP, 0, mmHDP_HOST_PATH_CNTL, tmp);

	/* After HDP is initialized, flush HDP.*/
	adev->nbio_funcs->hdp_flush(adev, NULL);

	if (amdgpu_vm_fault_stop == AMDGPU_VM_FAULT_STOP_ALWAYS)
		value = false;
	else
		value = true;

	gfxhub_v1_0_set_fault_enable_default(adev, value);
	mmhub_v1_0_set_fault_enable_default(adev, value);
	gmc_v9_0_flush_gpu_tlb(adev, 0, 0);

	DRM_INFO("PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(adev->gmc.gart_size >> 20),
		 (unsigned long long)amdgpu_bo_gpu_offset(adev->gart.bo));
	adev->gart.ready = true;
	return 0;
}

static int gmc_v9_0_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* The sequence of these two function calls matters.*/
	gmc_v9_0_init_golden_registers(adev);

	if (adev->mode_info.num_crtc) {
		/* Lockout access through VGA aperture*/
		WREG32_FIELD15(DCE, 0, VGA_HDP_CONTROL, VGA_MEMORY_DISABLE, 1);

		/* disable VGA render */
		WREG32_FIELD15(DCE, 0, VGA_RENDER_CONTROL, VGA_VSTATUS_CNTL, 0);
	}

	r = gmc_v9_0_gart_enable(adev);

	return r;
}

/**
 * gmc_v9_0_gart_disable - gart disable
 *
 * @adev: amdgpu_device pointer
 *
 * This disables all VM page table.
 */
static void gmc_v9_0_gart_disable(struct amdgpu_device *adev)
{
	gfxhub_v1_0_gart_disable(adev);
	mmhub_v1_0_gart_disable(adev);
	amdgpu_gart_table_vram_unpin(adev);
}

static int gmc_v9_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev)) {
		/* full access mode, so don't touch any GMC register */
		DRM_DEBUG("For SRIOV client, shouldn't do anything.\n");
		return 0;
	}

	amdgpu_irq_put(adev, &adev->gmc.vm_fault, 0);
	gmc_v9_0_gart_disable(adev);

	return 0;
}

static int gmc_v9_0_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return gmc_v9_0_hw_fini(adev);
}

static int gmc_v9_0_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = gmc_v9_0_hw_init(adev);
	if (r)
		return r;

	amdgpu_vmid_reset_all(adev);

	return 0;
}

static bool gmc_v9_0_is_idle(void *handle)
{
	/* MC is always ready in GMC v9.*/
	return true;
}

static int gmc_v9_0_wait_for_idle(void *handle)
{
	/* There is no need to wait for MC idle in GMC v9.*/
	return 0;
}

static int gmc_v9_0_soft_reset(void *handle)
{
	/* XXX for emulation.*/
	return 0;
}

static int gmc_v9_0_set_clockgating_state(void *handle,
					enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return mmhub_v1_0_set_clockgating(adev, state);
}

static void gmc_v9_0_get_clockgating_state(void *handle, u32 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	mmhub_v1_0_get_clockgating(adev, flags);
}

static int gmc_v9_0_set_powergating_state(void *handle,
					enum amd_powergating_state state)
{
	return 0;
}

const struct amd_ip_funcs gmc_v9_0_ip_funcs = {
	.name = "gmc_v9_0",
	.early_init = gmc_v9_0_early_init,
	.late_init = gmc_v9_0_late_init,
	.sw_init = gmc_v9_0_sw_init,
	.sw_fini = gmc_v9_0_sw_fini,
	.hw_init = gmc_v9_0_hw_init,
	.hw_fini = gmc_v9_0_hw_fini,
	.suspend = gmc_v9_0_suspend,
	.resume = gmc_v9_0_resume,
	.is_idle = gmc_v9_0_is_idle,
	.wait_for_idle = gmc_v9_0_wait_for_idle,
	.soft_reset = gmc_v9_0_soft_reset,
	.set_clockgating_state = gmc_v9_0_set_clockgating_state,
	.set_powergating_state = gmc_v9_0_set_powergating_state,
	.get_clockgating_state = gmc_v9_0_get_clockgating_state,
};

const struct amdgpu_ip_block_version gmc_v9_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_GMC,
	.major = 9,
	.minor = 0,
	.rev = 0,
	.funcs = &gmc_v9_0_ip_funcs,
};
