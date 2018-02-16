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

#include "hdp/hdp_4_0_offset.h"
#include "hdp/hdp_4_0_sh_mask.h"
#include "gc/gc_9_0_sh_mask.h"
#include "dce/dce_12_0_offset.h"
#include "dce/dce_12_0_sh_mask.h"
#include "vega10_enum.h"
#include "mmhub/mmhub_1_0_offset.h"
#include "athub/athub_1_0_offset.h"

#include "soc15.h"
#include "soc15_common.h"
#include "umc/umc_6_0_sh_mask.h"

#include "gfxhub_v1_0.h"
#include "mmhub_v1_0.h"

#define mmDF_CS_AON0_DramBaseAddress0                                                                  0x0044
#define mmDF_CS_AON0_DramBaseAddress0_BASE_IDX                                                         0
//DF_CS_AON0_DramBaseAddress0
#define DF_CS_AON0_DramBaseAddress0__AddrRngVal__SHIFT                                                        0x0
#define DF_CS_AON0_DramBaseAddress0__LgcyMmioHoleEn__SHIFT                                                    0x1
#define DF_CS_AON0_DramBaseAddress0__IntLvNumChan__SHIFT                                                      0x4
#define DF_CS_AON0_DramBaseAddress0__IntLvAddrSel__SHIFT                                                      0x8
#define DF_CS_AON0_DramBaseAddress0__DramBaseAddr__SHIFT                                                      0xc
#define DF_CS_AON0_DramBaseAddress0__AddrRngVal_MASK                                                          0x00000001L
#define DF_CS_AON0_DramBaseAddress0__LgcyMmioHoleEn_MASK                                                      0x00000002L
#define DF_CS_AON0_DramBaseAddress0__IntLvNumChan_MASK                                                        0x000000F0L
#define DF_CS_AON0_DramBaseAddress0__IntLvAddrSel_MASK                                                        0x00000700L
#define DF_CS_AON0_DramBaseAddress0__DramBaseAddr_MASK                                                        0xFFFFF000L

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

static int gmc_v9_0_process_interrupt(struct amdgpu_device *adev,
				struct amdgpu_irq_src *source,
				struct amdgpu_iv_entry *entry)
{
	struct amdgpu_vmhub *hub = &adev->vmhub[entry->vmid_src];
	uint32_t status = 0;
	u64 addr;

	addr = (u64)entry->src_data[0] << 12;
	addr |= ((u64)entry->src_data[1] & 0xf) << 44;

	if (!amdgpu_sriov_vf(adev)) {
		status = RREG32(hub->vm_l2_pro_fault_status);
		WREG32_P(hub->vm_l2_pro_fault_cntl, 1, ~1);
	}

	if (printk_ratelimit()) {
		dev_err(adev->dev,
			"[%s] VMC page fault (src_id:%u ring:%u vmid:%u pas_id:%u)\n",
			entry->vmid_src ? "mmhub" : "gfxhub",
			entry->src_id, entry->ring_id, entry->vmid,
			entry->pas_id);
		dev_err(adev->dev, "  at page 0x%016llx from %d\n",
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
	adev->mc.vm_fault.num_types = 1;
	adev->mc.vm_fault.funcs = &gmc_v9_0_irq_funcs;
}

static uint32_t gmc_v9_0_get_invalidate_req(unsigned int vmid)
{
	u32 req = 0;

	/* invalidate using legacy mode on vmid*/
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ,
			    PER_VMID_INVALIDATE_REQ, 1 << vmid);
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ, FLUSH_TYPE, 0);
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
 * gmc_v9_0_gart_flush_gpu_tlb - gart tlb flush callback
 *
 * @adev: amdgpu_device pointer
 * @vmid: vm instance to flush
 *
 * Flush the TLB for the requested page table.
 */
static void gmc_v9_0_gart_flush_gpu_tlb(struct amdgpu_device *adev,
					uint32_t vmid)
{
	/* Use register 17 for GART */
	const unsigned eng = 17;
	unsigned i, j;

	/* flush hdp cache */
	adev->nbio_funcs->hdp_flush(adev);

	spin_lock(&adev->mc.invalidate_lock);

	for (i = 0; i < AMDGPU_MAX_VMHUBS; ++i) {
		struct amdgpu_vmhub *hub = &adev->vmhub[i];
		u32 tmp = gmc_v9_0_get_invalidate_req(vmid);

		WREG32_NO_KIQ(hub->vm_inv_eng0_req + eng, tmp);

		/* Busy wait for ACK.*/
		for (j = 0; j < 100; j++) {
			tmp = RREG32_NO_KIQ(hub->vm_inv_eng0_ack + eng);
			tmp &= 1 << vmid;
			if (tmp)
				break;
			cpu_relax();
		}
		if (j < 100)
			continue;

		/* Wait for ACK with a delay.*/
		for (j = 0; j < adev->usec_timeout; j++) {
			tmp = RREG32_NO_KIQ(hub->vm_inv_eng0_ack + eng);
			tmp &= 1 << vmid;
			if (tmp)
				break;
			udelay(1);
		}
		if (j < adev->usec_timeout)
			continue;

		DRM_ERROR("Timeout waiting for VM flush ACK!\n");
	}

	spin_unlock(&adev->mc.invalidate_lock);
}

/**
 * gmc_v9_0_gart_set_pte_pde - update the page tables using MMIO
 *
 * @adev: amdgpu_device pointer
 * @cpu_pt_addr: cpu address of the page table
 * @gpu_page_idx: entry in the page table to update
 * @addr: dst addr to write into pte/pde
 * @flags: access flags
 *
 * Update the page tables using the CPU.
 */
static int gmc_v9_0_gart_set_pte_pde(struct amdgpu_device *adev,
					void *cpu_pt_addr,
					uint32_t gpu_page_idx,
					uint64_t addr,
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
	if (!(*flags & AMDGPU_PDE_PTE))
		*addr = adev->vm_manager.vram_base_offset + *addr -
			adev->mc.vram_start;
	BUG_ON(*addr & 0xFFFF00000000003FULL);

	if (!adev->mc.translate_further)
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

static const struct amdgpu_gart_funcs gmc_v9_0_gart_funcs = {
	.flush_gpu_tlb = gmc_v9_0_gart_flush_gpu_tlb,
	.set_pte_pde = gmc_v9_0_gart_set_pte_pde,
	.get_invalidate_req = gmc_v9_0_get_invalidate_req,
	.get_vm_pte_flags = gmc_v9_0_get_vm_pte_flags,
	.get_vm_pde = gmc_v9_0_get_vm_pde
};

static void gmc_v9_0_set_gart_funcs(struct amdgpu_device *adev)
{
	if (adev->gart.gart_funcs == NULL)
		adev->gart.gart_funcs = &gmc_v9_0_gart_funcs;
}

static int gmc_v9_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gmc_v9_0_set_gart_funcs(adev);
	gmc_v9_0_set_irq_funcs(adev);

	adev->mc.shared_aperture_start = 0x2000000000000000ULL;
	adev->mc.shared_aperture_end =
		adev->mc.shared_aperture_start + (4ULL << 30) - 1;
	adev->mc.private_aperture_start =
		adev->mc.shared_aperture_end + 1;
	adev->mc.private_aperture_end =
		adev->mc.private_aperture_start + (4ULL << 30) - 1;

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

static int gmc_v9_0_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	/*
	 * The latest engine allocation on gfx9 is:
	 * Engine 0, 1: idle
	 * Engine 2, 3: firmware
	 * Engine 4~13: amdgpu ring, subject to change when ring number changes
	 * Engine 14~15: idle
	 * Engine 16: kfd tlb invalidation
	 * Engine 17: Gart flushes
	 */
	unsigned vm_inv_eng[AMDGPU_MAX_VMHUBS] = { 4, 4 };
	unsigned i;
	int r;

	for(i = 0; i < adev->num_rings; ++i) {
		struct amdgpu_ring *ring = adev->rings[i];
		unsigned vmhub = ring->funcs->vmhub;

		ring->vm_inv_eng = vm_inv_eng[vmhub]++;
		dev_info(adev->dev, "ring %u(%s) uses VM inv eng %u on hub %u\n",
			 ring->idx, ring->name, ring->vm_inv_eng,
			 ring->funcs->vmhub);
	}

	/* Engine 16 is used for KFD and 17 for GART flushes */
	for(i = 0; i < AMDGPU_MAX_VMHUBS; ++i)
		BUG_ON(vm_inv_eng[i] > 16);

	if (adev->asic_type == CHIP_VEGA10) {
		r = gmc_v9_0_ecc_available(adev);
		if (r == 1) {
			DRM_INFO("ECC is active.\n");
		} else if (r == 0) {
			DRM_INFO("ECC is not present.\n");
		} else {
			DRM_ERROR("gmc_v9_0_ecc_available() failed. r: %d\n", r);
			return r;
		}
	}

	return amdgpu_irq_get(adev, &adev->mc.vm_fault, 0);
}

static void gmc_v9_0_vram_gtt_location(struct amdgpu_device *adev,
					struct amdgpu_mc *mc)
{
	u64 base = 0;
	if (!amdgpu_sriov_vf(adev))
		base = mmhub_v1_0_get_fb_location(adev);
	amdgpu_device_vram_location(adev, &adev->mc, base);
	amdgpu_device_gart_location(adev, mc);
	/* base offset of vram pages */
	if (adev->flags & AMD_IS_APU)
		adev->vm_manager.vram_base_offset = gfxhub_v1_0_get_mc_fb_offset(adev);
	else
		adev->vm_manager.vram_base_offset = 0;
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
	u32 tmp;
	int chansize, numchan;
	int r;

	adev->mc.vram_width = amdgpu_atomfirmware_get_vram_width(adev);
	if (!adev->mc.vram_width) {
		/* hbm memory channel size */
		chansize = 128;

		tmp = RREG32_SOC15(DF, 0, mmDF_CS_AON0_DramBaseAddress0);
		tmp &= DF_CS_AON0_DramBaseAddress0__IntLvNumChan_MASK;
		tmp >>= DF_CS_AON0_DramBaseAddress0__IntLvNumChan__SHIFT;
		switch (tmp) {
		case 0:
		default:
			numchan = 1;
			break;
		case 1:
			numchan = 2;
			break;
		case 2:
			numchan = 0;
			break;
		case 3:
			numchan = 4;
			break;
		case 4:
			numchan = 0;
			break;
		case 5:
			numchan = 8;
			break;
		case 6:
			numchan = 0;
			break;
		case 7:
			numchan = 16;
			break;
		case 8:
			numchan = 2;
			break;
		}
		adev->mc.vram_width = numchan * chansize;
	}

	/* size in MB on si */
	adev->mc.mc_vram_size =
		adev->nbio_funcs->get_memsize(adev) * 1024ULL * 1024ULL;
	adev->mc.real_vram_size = adev->mc.mc_vram_size;

	if (!(adev->flags & AMD_IS_APU)) {
		r = amdgpu_device_resize_fb_bar(adev);
		if (r)
			return r;
	}
	adev->mc.aper_base = pci_resource_start(adev->pdev, 0);
	adev->mc.aper_size = pci_resource_len(adev->pdev, 0);

	/* In case the PCI BAR is larger than the actual amount of vram */
	adev->mc.visible_vram_size = adev->mc.aper_size;
	if (adev->mc.visible_vram_size > adev->mc.real_vram_size)
		adev->mc.visible_vram_size = adev->mc.real_vram_size;

	/* set the gart size */
	if (amdgpu_gart_size == -1) {
		switch (adev->asic_type) {
		case CHIP_VEGA10:  /* all engines support GPUVM */
		default:
			adev->mc.gart_size = 256ULL << 20;
			break;
		case CHIP_RAVEN:   /* DCE SG support */
			adev->mc.gart_size = 1024ULL << 20;
			break;
		}
	} else {
		adev->mc.gart_size = (u64)amdgpu_gart_size << 20;
	}

	gmc_v9_0_vram_gtt_location(adev, &adev->mc);

	return 0;
}

static int gmc_v9_0_gart_init(struct amdgpu_device *adev)
{
	int r;

	if (adev->gart.robj) {
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

static int gmc_v9_0_sw_init(void *handle)
{
	int r;
	int dma_bits;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gfxhub_v1_0_init(adev);
	mmhub_v1_0_init(adev);

	spin_lock_init(&adev->mc.invalidate_lock);

	switch (adev->asic_type) {
	case CHIP_RAVEN:
		adev->mc.vram_type = AMDGPU_VRAM_TYPE_UNKNOWN;
		if (adev->rev_id == 0x0 || adev->rev_id == 0x1) {
			amdgpu_vm_adjust_size(adev, 256 * 1024, 9, 3, 48);
		} else {
			/* vm_size is 128TB + 512GB for legacy 3-level page support */
			amdgpu_vm_adjust_size(adev, 128 * 1024 + 512, 9, 2, 48);
			adev->mc.translate_further =
				adev->vm_manager.num_level > 1;
		}
		break;
	case CHIP_VEGA10:
		/* XXX Don't know how to get VRAM type yet. */
		adev->mc.vram_type = AMDGPU_VRAM_TYPE_HBM;
		/*
		 * To fulfill 4-level page support,
		 * vm size is 256TB (48bit), maximum size of Vega10,
		 * block size 512 (9bit)
		 */
		amdgpu_vm_adjust_size(adev, 256 * 1024, 9, 3, 48);
		break;
	default:
		break;
	}

	/* This interrupt is VMC page fault.*/
	r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_VMC, 0,
				&adev->mc.vm_fault);
	r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_UTCL2, 0,
				&adev->mc.vm_fault);

	if (r)
		return r;

	/* Set the internal MC address mask
	 * This is the max address of the GPU's
	 * internal address space.
	 */
	adev->mc.mc_mask = 0xffffffffffffULL; /* 48 bit MC */

	/*
	 * It needs to reserve 8M stolen memory for vega10
	 * TODO: Figure out how to avoid that...
	 */
	adev->mc.stolen_size = 8 * 1024 * 1024;

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

	r = gmc_v9_0_mc_init(adev);
	if (r)
		return r;

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

/**
 * gmc_v9_0_gart_fini - vm fini callback
 *
 * @adev: amdgpu_device pointer
 *
 * Tears down the driver GART/VM setup (CIK).
 */
static void gmc_v9_0_gart_fini(struct amdgpu_device *adev)
{
	amdgpu_gart_table_vram_free(adev);
	amdgpu_gart_fini(adev);
}

static int gmc_v9_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_gem_force_release(adev);
	amdgpu_vm_manager_fini(adev);
	gmc_v9_0_gart_fini(adev);
	amdgpu_bo_fini(adev);

	return 0;
}

static void gmc_v9_0_init_golden_registers(struct amdgpu_device *adev)
{

	switch (adev->asic_type) {
	case CHIP_VEGA10:
		soc15_program_register_sequence(adev,
						golden_settings_mmhub_1_0_0,
						ARRAY_SIZE(golden_settings_mmhub_1_0_0));
		soc15_program_register_sequence(adev,
						golden_settings_athub_1_0_0,
						ARRAY_SIZE(golden_settings_athub_1_0_0));
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

	if (adev->gart.robj == NULL) {
		dev_err(adev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}
	r = amdgpu_gart_table_vram_pin(adev);
	if (r)
		return r;

	switch (adev->asic_type) {
	case CHIP_RAVEN:
		mmhub_v1_0_initialize_power_gating(adev);
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
	adev->nbio_funcs->hdp_flush(adev);

	if (amdgpu_vm_fault_stop == AMDGPU_VM_FAULT_STOP_ALWAYS)
		value = false;
	else
		value = true;

	gfxhub_v1_0_set_fault_enable_default(adev, value);
	mmhub_v1_0_set_fault_enable_default(adev, value);
	gmc_v9_0_gart_flush_gpu_tlb(adev, 0);

	DRM_INFO("PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(adev->mc.gart_size >> 20),
		 (unsigned long long)adev->gart.table_addr);
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

	amdgpu_irq_put(adev, &adev->mc.vm_fault, 0);
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
