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

#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_vcn.h"
#include "amdgpu_pm.h"
#include "soc15.h"
#include "soc15d.h"
#include "vcn_v2_0.h"

#include "vcn/vcn_3_0_0_offset.h"
#include "vcn/vcn_3_0_0_sh_mask.h"
#include "ivsrcid/vcn/irqsrcs_vcn_2_0.h"

#define mmUVD_CONTEXT_ID_INTERNAL_OFFSET			0x27
#define mmUVD_GPCOM_VCPU_CMD_INTERNAL_OFFSET			0x0f
#define mmUVD_GPCOM_VCPU_DATA0_INTERNAL_OFFSET			0x10
#define mmUVD_GPCOM_VCPU_DATA1_INTERNAL_OFFSET			0x11
#define mmUVD_NO_OP_INTERNAL_OFFSET				0x29
#define mmUVD_GP_SCRATCH8_INTERNAL_OFFSET			0x66
#define mmUVD_SCRATCH9_INTERNAL_OFFSET				0xc01d

#define mmUVD_LMI_RBC_IB_VMID_INTERNAL_OFFSET			0x431
#define mmUVD_LMI_RBC_IB_64BIT_BAR_LOW_INTERNAL_OFFSET		0x3b4
#define mmUVD_LMI_RBC_IB_64BIT_BAR_HIGH_INTERNAL_OFFSET 	0x3b5
#define mmUVD_RBC_IB_SIZE_INTERNAL_OFFSET			0x25c

#define VCN_INSTANCES_SIENNA_CICHLID	 				2

static void vcn_v3_0_set_dec_ring_funcs(struct amdgpu_device *adev);
static void vcn_v3_0_set_enc_ring_funcs(struct amdgpu_device *adev);
static void vcn_v3_0_set_irq_funcs(struct amdgpu_device *adev);
static int vcn_v3_0_set_powergating_state(void *handle,
			enum amd_powergating_state state);

static int amdgpu_ih_clientid_vcns[] = {
	SOC15_IH_CLIENTID_VCN,
	SOC15_IH_CLIENTID_VCN1
};

/**
 * vcn_v3_0_early_init - set function pointers
 *
 * @handle: amdgpu_device pointer
 *
 * Set ring and irq function pointers
 */
static int vcn_v3_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	if (adev->asic_type == CHIP_SIENNA_CICHLID) {
		u32 harvest;
		int i;

		adev->vcn.num_vcn_inst = VCN_INSTANCES_SIENNA_CICHLID;
		for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
			harvest = RREG32_SOC15(VCN, i, mmCC_UVD_HARVESTING);
			if (harvest & CC_UVD_HARVESTING__UVD_DISABLE_MASK)
				adev->vcn.harvest_config |= 1 << i;
		}

		if (adev->vcn.harvest_config == (AMDGPU_VCN_HARVEST_VCN0 |
			 AMDGPU_VCN_HARVEST_VCN1))
			/* both instances are harvested, disable the block */
			return -ENOENT;
	} else
		adev->vcn.num_vcn_inst = 1;

	adev->vcn.num_enc_rings = 2;

	vcn_v3_0_set_dec_ring_funcs(adev);
	vcn_v3_0_set_enc_ring_funcs(adev);
	vcn_v3_0_set_irq_funcs(adev);

	return 0;
}

/**
 * vcn_v3_0_sw_init - sw init for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Load firmware and sw initialization
 */
static int vcn_v3_0_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	int i, j, r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_vcn_sw_init(adev);
	if (r)
		return r;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		const struct common_firmware_header *hdr;
		hdr = (const struct common_firmware_header *)adev->vcn.fw->data;
		adev->firmware.ucode[AMDGPU_UCODE_ID_VCN].ucode_id = AMDGPU_UCODE_ID_VCN;
		adev->firmware.ucode[AMDGPU_UCODE_ID_VCN].fw = adev->vcn.fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(hdr->ucode_size_bytes), PAGE_SIZE);

		if (adev->vcn.num_vcn_inst == VCN_INSTANCES_SIENNA_CICHLID) {
			adev->firmware.ucode[AMDGPU_UCODE_ID_VCN1].ucode_id = AMDGPU_UCODE_ID_VCN1;
			adev->firmware.ucode[AMDGPU_UCODE_ID_VCN1].fw = adev->vcn.fw;
			adev->firmware.fw_size +=
				ALIGN(le32_to_cpu(hdr->ucode_size_bytes), PAGE_SIZE);
		}
		DRM_INFO("PSP loading VCN firmware\n");
	}

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		adev->vcn.internal.context_id = mmUVD_CONTEXT_ID_INTERNAL_OFFSET;
		adev->vcn.internal.ib_vmid = mmUVD_LMI_RBC_IB_VMID_INTERNAL_OFFSET;
		adev->vcn.internal.ib_bar_low = mmUVD_LMI_RBC_IB_64BIT_BAR_LOW_INTERNAL_OFFSET;
		adev->vcn.internal.ib_bar_high = mmUVD_LMI_RBC_IB_64BIT_BAR_HIGH_INTERNAL_OFFSET;
		adev->vcn.internal.ib_size = mmUVD_RBC_IB_SIZE_INTERNAL_OFFSET;
		adev->vcn.internal.gp_scratch8 = mmUVD_GP_SCRATCH8_INTERNAL_OFFSET;

		adev->vcn.internal.scratch9 = mmUVD_SCRATCH9_INTERNAL_OFFSET;
		adev->vcn.inst[i].external.scratch9 = SOC15_REG_OFFSET(VCN, i, mmUVD_SCRATCH9);
		adev->vcn.internal.data0 = mmUVD_GPCOM_VCPU_DATA0_INTERNAL_OFFSET;
		adev->vcn.inst[i].external.data0 = SOC15_REG_OFFSET(VCN, i, mmUVD_GPCOM_VCPU_DATA0);
		adev->vcn.internal.data1 = mmUVD_GPCOM_VCPU_DATA1_INTERNAL_OFFSET;
		adev->vcn.inst[i].external.data1 = SOC15_REG_OFFSET(VCN, i, mmUVD_GPCOM_VCPU_DATA1);
		adev->vcn.internal.cmd = mmUVD_GPCOM_VCPU_CMD_INTERNAL_OFFSET;
		adev->vcn.inst[i].external.cmd = SOC15_REG_OFFSET(VCN, i, mmUVD_GPCOM_VCPU_CMD);
		adev->vcn.internal.nop = mmUVD_NO_OP_INTERNAL_OFFSET;
		adev->vcn.inst[i].external.nop = SOC15_REG_OFFSET(VCN, i, mmUVD_NO_OP);

		/* VCN DEC TRAP */
		r = amdgpu_irq_add_id(adev, amdgpu_ih_clientid_vcns[i],
				VCN_2_0__SRCID__UVD_SYSTEM_MESSAGE_INTERRUPT, &adev->vcn.inst[i].irq);
		if (r)
			return r;

		ring = &adev->vcn.inst[i].ring_dec;
		ring->use_doorbell = true;
		ring->doorbell_index = (adev->doorbell_index.vcn.vcn_ring0_1 << 1) + 8 * i;
		sprintf(ring->name, "vcn_dec_%d", i);
		r = amdgpu_ring_init(adev, ring, 512, &adev->vcn.inst[i].irq, 0,
				     AMDGPU_RING_PRIO_DEFAULT);
		if (r)
			return r;

		for (j = 0; j < adev->vcn.num_enc_rings; ++j) {
			/* VCN ENC TRAP */
			r = amdgpu_irq_add_id(adev, amdgpu_ih_clientid_vcns[i],
				j + VCN_2_0__SRCID__UVD_ENC_GENERAL_PURPOSE, &adev->vcn.inst[i].irq);
			if (r)
				return r;

			ring = &adev->vcn.inst[i].ring_enc[j];
			ring->use_doorbell = true;
			ring->doorbell_index = (adev->doorbell_index.vcn.vcn_ring0_1 << 1) + 2 + j + 8 * i;
			sprintf(ring->name, "vcn_enc_%d.%d", i, j);
			r = amdgpu_ring_init(adev, ring, 512, &adev->vcn.inst[i].irq, 0,
					     AMDGPU_RING_PRIO_DEFAULT);
			if (r)
				return r;
		}
	}

	return 0;
}

/**
 * vcn_v3_0_sw_fini - sw fini for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * VCN suspend and free up sw allocation
 */
static int vcn_v3_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = amdgpu_vcn_suspend(adev);
	if (r)
		return r;

	r = amdgpu_vcn_sw_fini(adev);

	return r;
}

/**
 * vcn_v3_0_hw_init - start and test VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
static int vcn_v3_0_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring;
	int i, j, r;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		ring = &adev->vcn.inst[i].ring_dec;

		adev->nbio.funcs->vcn_doorbell_range(adev, ring->use_doorbell,
						     ring->doorbell_index, i);

		r = amdgpu_ring_test_helper(ring);
		if (r)
			goto done;

		for (j = 0; j < adev->vcn.num_enc_rings; ++j) {
			ring = &adev->vcn.inst[i].ring_enc[j];
			r = amdgpu_ring_test_helper(ring);
			if (r)
				goto done;
		}
	}

done:
	if (!r)
		DRM_INFO("VCN decode and encode initialized successfully.\n");

	return r;
}

/**
 * vcn_v3_0_hw_fini - stop the hardware block
 *
 * @handle: amdgpu_device pointer
 *
 * Stop the VCN block, mark ring as not ready any more
 */
static int vcn_v3_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring;
	int i, j;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		ring = &adev->vcn.inst[i].ring_dec;

		if (RREG32_SOC15(VCN, i, mmUVD_STATUS))
			vcn_v3_0_set_powergating_state(adev, AMD_PG_STATE_GATE);

		ring->sched.ready = false;

		for (j = 0; j < adev->vcn.num_enc_rings; ++j) {
			ring = &adev->vcn.inst[i].ring_enc[j];
			ring->sched.ready = false;
		}
	}

	return 0;
}

/**
 * vcn_v3_0_suspend - suspend VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * HW fini and suspend VCN block
 */
static int vcn_v3_0_suspend(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = vcn_v3_0_hw_fini(adev);
	if (r)
		return r;

	r = amdgpu_vcn_suspend(adev);

	return r;
}

/**
 * vcn_v3_0_resume - resume VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Resume firmware and hw init VCN block
 */
static int vcn_v3_0_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	r = vcn_v3_0_hw_init(adev);

	return r;
}

/**
 * vcn_v3_0_mc_resume - memory controller programming
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Let the VCN memory controller know it's offsets
 */
static void vcn_v3_0_mc_resume(struct amdgpu_device *adev, int inst)
{
	uint32_t size = AMDGPU_GPU_PAGE_ALIGN(adev->vcn.fw->size + 4);
	uint32_t offset;

	/* cache window 0: fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN].tmr_mc_addr_lo));
		WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN].tmr_mc_addr_hi));
		WREG32_SOC15(VCN, inst, mmUVD_VCPU_CACHE_OFFSET0, 0);
		offset = 0;
	} else {
		WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			lower_32_bits(adev->vcn.inst[inst].gpu_addr));
		WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			upper_32_bits(adev->vcn.inst[inst].gpu_addr));
		offset = size;
		/* No signed header for now from firmware
		WREG32_SOC15(VCN, inst, mmUVD_VCPU_CACHE_OFFSET0,
			AMDGPU_UVD_FIRMWARE_OFFSET >> 3);
		*/
		WREG32_SOC15(UVD, inst, mmUVD_VCPU_CACHE_OFFSET0, 0);
	}
	WREG32_SOC15(VCN, inst, mmUVD_VCPU_CACHE_SIZE0, size);

	/* cache window 1: stack */
	WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst[inst].gpu_addr + offset));
	WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst[inst].gpu_addr + offset));
	WREG32_SOC15(VCN, inst, mmUVD_VCPU_CACHE_OFFSET1, 0);
	WREG32_SOC15(VCN, inst, mmUVD_VCPU_CACHE_SIZE1, AMDGPU_VCN_STACK_SIZE);

	/* cache window 2: context */
	WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst[inst].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(VCN, inst, mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst[inst].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(VCN, inst, mmUVD_VCPU_CACHE_OFFSET2, 0);
	WREG32_SOC15(VCN, inst, mmUVD_VCPU_CACHE_SIZE2, AMDGPU_VCN_CONTEXT_SIZE);
}

static int vcn_v3_0_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	uint32_t rb_bufsz, tmp;
	int i, j, k, r;

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_uvd(adev, true);

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		/* set VCN status busy */
		tmp = RREG32_SOC15(VCN, i, mmUVD_STATUS) | UVD_STATUS__UVD_BUSY;
		WREG32_SOC15(VCN, i, mmUVD_STATUS, tmp);

		/* enable VCPU clock */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_VCPU_CNTL),
			UVD_VCPU_CNTL__CLK_EN_MASK, ~UVD_VCPU_CNTL__CLK_EN_MASK);

		/* disable master interrupt */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_MASTINT_EN), 0,
			~UVD_MASTINT_EN__VCPU_EN_MASK);

		/* setup mmUVD_LMI_CTRL */
		tmp = RREG32_SOC15(VCN, i, mmUVD_LMI_CTRL);
		WREG32_SOC15(VCN, i, mmUVD_LMI_CTRL, tmp |
			UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK	|
			UVD_LMI_CTRL__MASK_MC_URGENT_MASK |
			UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
			UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK);

		/* setup mmUVD_MPC_CNTL */
		tmp = RREG32_SOC15(VCN, i, mmUVD_MPC_CNTL);
		tmp &= ~UVD_MPC_CNTL__REPLACEMENT_MODE_MASK;
		tmp |= 0x2 << UVD_MPC_CNTL__REPLACEMENT_MODE__SHIFT;
		WREG32_SOC15(VCN, i, mmUVD_MPC_CNTL, tmp);

		/* setup UVD_MPC_SET_MUXA0 */
		WREG32_SOC15(VCN, i, mmUVD_MPC_SET_MUXA0,
			((0x1 << UVD_MPC_SET_MUXA0__VARA_1__SHIFT) |
			(0x2 << UVD_MPC_SET_MUXA0__VARA_2__SHIFT) |
			(0x3 << UVD_MPC_SET_MUXA0__VARA_3__SHIFT) |
			(0x4 << UVD_MPC_SET_MUXA0__VARA_4__SHIFT)));

		/* setup UVD_MPC_SET_MUXB0 */
		WREG32_SOC15(VCN, i, mmUVD_MPC_SET_MUXB0,
			((0x1 << UVD_MPC_SET_MUXB0__VARB_1__SHIFT) |
			(0x2 << UVD_MPC_SET_MUXB0__VARB_2__SHIFT) |
			(0x3 << UVD_MPC_SET_MUXB0__VARB_3__SHIFT) |
			(0x4 << UVD_MPC_SET_MUXB0__VARB_4__SHIFT)));

		/* setup mmUVD_MPC_SET_MUX */
		WREG32_SOC15(VCN, i, mmUVD_MPC_SET_MUX,
			((0x0 << UVD_MPC_SET_MUX__SET_0__SHIFT) |
			(0x1 << UVD_MPC_SET_MUX__SET_1__SHIFT) |
			(0x2 << UVD_MPC_SET_MUX__SET_2__SHIFT)));

		vcn_v3_0_mc_resume(adev, i);

		/* VCN global tiling registers */
		WREG32_SOC15(VCN, i, mmUVD_GFX10_ADDR_CONFIG,
			adev->gfx.config.gb_addr_config);

		/* enable LMI MC and UMC channels */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_LMI_CTRL2), 0,
			~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);

		tmp = RREG32_SOC15(VCN, i, mmUVD_SOFT_RESET);
		tmp &= ~UVD_SOFT_RESET__LMI_SOFT_RESET_MASK;
		tmp &= ~UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK;
		WREG32_SOC15(VCN, i, mmUVD_SOFT_RESET, tmp);

		/* unblock VCPU register access */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_RB_ARB_CTRL), 0,
			~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

		/* release VCPU reset to boot */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_VCPU_CNTL), 0,
			~UVD_VCPU_CNTL__BLK_RST_MASK);

		for (j = 0; j < 10; ++j) {
			uint32_t status;

			for (k = 0; k < 100; ++k) {
				status = RREG32_SOC15(VCN, i, mmUVD_STATUS);
				if (status & 2)
					break;
				mdelay(10);
			}
			r = 0;
			if (status & 2)
				break;

			DRM_ERROR("VCN[%d] decode not responding, trying to reset the VCPU!!!\n", i);
			WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_VCPU_CNTL),
				UVD_VCPU_CNTL__BLK_RST_MASK,
				~UVD_VCPU_CNTL__BLK_RST_MASK);
			mdelay(10);
			WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_VCPU_CNTL), 0,
				~UVD_VCPU_CNTL__BLK_RST_MASK);

			mdelay(10);
			r = -1;
		}

		if (r) {
			DRM_ERROR("VCN[%d] decode not responding, giving up!!!\n", i);
			return r;
		}

		/* enable master interrupt */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_MASTINT_EN),
			UVD_MASTINT_EN__VCPU_EN_MASK,
			~UVD_MASTINT_EN__VCPU_EN_MASK);

		/* clear the busy bit of VCN_STATUS */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_STATUS), 0,
			~(2 << UVD_STATUS__VCPU_REPORT__SHIFT));

		WREG32_SOC15(VCN, i, mmUVD_LMI_RBC_RB_VMID, 0);

		ring = &adev->vcn.inst[i].ring_dec;
		/* force RBC into idle state */
		rb_bufsz = order_base_2(ring->ring_size);
		tmp = REG_SET_FIELD(0, UVD_RBC_RB_CNTL, RB_BUFSZ, rb_bufsz);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_BLKSZ, 1);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_FETCH, 1);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_UPDATE, 1);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_RPTR_WR_EN, 1);
		WREG32_SOC15(VCN, i, mmUVD_RBC_RB_CNTL, tmp);

		/* programm the RB_BASE for ring buffer */
		WREG32_SOC15(VCN, i, mmUVD_LMI_RBC_RB_64BIT_BAR_LOW,
			lower_32_bits(ring->gpu_addr));
		WREG32_SOC15(VCN, i, mmUVD_LMI_RBC_RB_64BIT_BAR_HIGH,
			upper_32_bits(ring->gpu_addr));

		/* Initialize the ring buffer's read and write pointers */
		WREG32_SOC15(VCN, i, mmUVD_RBC_RB_RPTR, 0);

		ring->wptr = RREG32_SOC15(VCN, i, mmUVD_RBC_RB_RPTR);
		WREG32_SOC15(VCN, i, mmUVD_RBC_RB_WPTR,
			lower_32_bits(ring->wptr));
		ring = &adev->vcn.inst[i].ring_enc[0];
		WREG32_SOC15(VCN, i, mmUVD_RB_RPTR, lower_32_bits(ring->wptr));
		WREG32_SOC15(VCN, i, mmUVD_RB_WPTR, lower_32_bits(ring->wptr));
		WREG32_SOC15(VCN, i, mmUVD_RB_BASE_LO, ring->gpu_addr);
		WREG32_SOC15(VCN, i, mmUVD_RB_BASE_HI, upper_32_bits(ring->gpu_addr));
		WREG32_SOC15(VCN, i, mmUVD_RB_SIZE, ring->ring_size / 4);

		ring = &adev->vcn.inst[i].ring_enc[1];
		WREG32_SOC15(VCN, i, mmUVD_RB_RPTR2, lower_32_bits(ring->wptr));
		WREG32_SOC15(VCN, i, mmUVD_RB_WPTR2, lower_32_bits(ring->wptr));
		WREG32_SOC15(VCN, i, mmUVD_RB_BASE_LO2, ring->gpu_addr);
		WREG32_SOC15(VCN, i, mmUVD_RB_BASE_HI2, upper_32_bits(ring->gpu_addr));
		WREG32_SOC15(VCN, i, mmUVD_RB_SIZE2, ring->ring_size / 4);
	}

	return 0;
}

static int vcn_v3_0_stop(struct amdgpu_device *adev)
{
	uint32_t tmp;
	int i, r = 0;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		/* wait for vcn idle */
		SOC15_WAIT_ON_RREG(VCN, i, mmUVD_STATUS, UVD_STATUS__IDLE, 0x7, r);
		if (r)
			return r;

		tmp = UVD_LMI_STATUS__VCPU_LMI_WRITE_CLEAN_MASK |
			UVD_LMI_STATUS__READ_CLEAN_MASK |
			UVD_LMI_STATUS__WRITE_CLEAN_MASK |
			UVD_LMI_STATUS__WRITE_CLEAN_RAW_MASK;
		SOC15_WAIT_ON_RREG(VCN, i, mmUVD_LMI_STATUS, tmp, tmp, r);
		if (r)
			return r;

		/* disable LMI UMC channel */
		tmp = RREG32_SOC15(VCN, i, mmUVD_LMI_CTRL2);
		tmp |= UVD_LMI_CTRL2__STALL_ARB_UMC_MASK;
		WREG32_SOC15(VCN, i, mmUVD_LMI_CTRL2, tmp);
		tmp = UVD_LMI_STATUS__UMC_READ_CLEAN_RAW_MASK|
			UVD_LMI_STATUS__UMC_WRITE_CLEAN_RAW_MASK;
		SOC15_WAIT_ON_RREG(VCN, i, mmUVD_LMI_STATUS, tmp, tmp, r);
		if (r)
			return r;

		/* block VCPU register access */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_RB_ARB_CTRL),
			UVD_RB_ARB_CTRL__VCPU_DIS_MASK,
			~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

		/* reset VCPU */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_VCPU_CNTL),
			UVD_VCPU_CNTL__BLK_RST_MASK,
			~UVD_VCPU_CNTL__BLK_RST_MASK);

		/* disable VCPU clock */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, mmUVD_VCPU_CNTL), 0,
			~(UVD_VCPU_CNTL__CLK_EN_MASK));

		/* apply soft reset */
		tmp = RREG32_SOC15(VCN, i, mmUVD_SOFT_RESET);
		tmp |= UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK;
		WREG32_SOC15(VCN, i, mmUVD_SOFT_RESET, tmp);
		tmp = RREG32_SOC15(VCN, i, mmUVD_SOFT_RESET);
		tmp |= UVD_SOFT_RESET__LMI_SOFT_RESET_MASK;
		WREG32_SOC15(VCN, i, mmUVD_SOFT_RESET, tmp);

		/* clear status */
		WREG32_SOC15(VCN, i, mmUVD_STATUS, 0);
	}

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_uvd(adev, false);

	return 0;
}

/**
 * vcn_v3_0_dec_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t vcn_v3_0_dec_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(VCN, ring->me, mmUVD_RBC_RB_RPTR);
}

/**
 * vcn_v3_0_dec_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t vcn_v3_0_dec_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell)
		return adev->wb.wb[ring->wptr_offs];
	else
		return RREG32_SOC15(VCN, ring->me, mmUVD_RBC_RB_WPTR);
}

/**
 * vcn_v3_0_dec_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void vcn_v3_0_dec_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		adev->wb.wb[ring->wptr_offs] = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
	} else {
		WREG32_SOC15(VCN, ring->me, mmUVD_RBC_RB_WPTR, lower_32_bits(ring->wptr));
	}
}

static const struct amdgpu_ring_funcs vcn_v3_0_dec_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_DEC,
	.align_mask = 0xf,
	.vmhub = AMDGPU_MMHUB_0,
	.get_rptr = vcn_v3_0_dec_ring_get_rptr,
	.get_wptr = vcn_v3_0_dec_ring_get_wptr,
	.set_wptr = vcn_v3_0_dec_ring_set_wptr,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 6 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 8 +
		8 + /* vcn_v2_0_dec_ring_emit_vm_flush */
		14 + 14 + /* vcn_v2_0_dec_ring_emit_fence x2 vm fence */
		6,
	.emit_ib_size = 8, /* vcn_v2_0_dec_ring_emit_ib */
	.emit_ib = vcn_v2_0_dec_ring_emit_ib,
	.emit_fence = vcn_v2_0_dec_ring_emit_fence,
	.emit_vm_flush = vcn_v2_0_dec_ring_emit_vm_flush,
	.test_ring = amdgpu_vcn_dec_ring_test_ring,
	.test_ib = amdgpu_vcn_dec_ring_test_ib,
	.insert_nop = vcn_v2_0_dec_ring_insert_nop,
	.insert_start = vcn_v2_0_dec_ring_insert_start,
	.insert_end = vcn_v2_0_dec_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = vcn_v2_0_dec_ring_emit_wreg,
	.emit_reg_wait = vcn_v2_0_dec_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

/**
 * vcn_v3_0_enc_ring_get_rptr - get enc read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware enc read pointer
 */
static uint64_t vcn_v3_0_enc_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->vcn.inst[ring->me].ring_enc[0])
		return RREG32_SOC15(VCN, ring->me, mmUVD_RB_RPTR);
	else
		return RREG32_SOC15(VCN, ring->me, mmUVD_RB_RPTR2);
}

/**
 * vcn_v3_0_enc_ring_get_wptr - get enc write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware enc write pointer
 */
static uint64_t vcn_v3_0_enc_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->vcn.inst[ring->me].ring_enc[0]) {
		if (ring->use_doorbell)
			return adev->wb.wb[ring->wptr_offs];
		else
			return RREG32_SOC15(VCN, ring->me, mmUVD_RB_WPTR);
	} else {
		if (ring->use_doorbell)
			return adev->wb.wb[ring->wptr_offs];
		else
			return RREG32_SOC15(VCN, ring->me, mmUVD_RB_WPTR2);
	}
}

/**
 * vcn_v3_0_enc_ring_set_wptr - set enc write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the enc write pointer to the hardware
 */
static void vcn_v3_0_enc_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->vcn.inst[ring->me].ring_enc[0]) {
		if (ring->use_doorbell) {
			adev->wb.wb[ring->wptr_offs] = lower_32_bits(ring->wptr);
			WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
		} else {
			WREG32_SOC15(VCN, ring->me, mmUVD_RB_WPTR, lower_32_bits(ring->wptr));
		}
	} else {
		if (ring->use_doorbell) {
			adev->wb.wb[ring->wptr_offs] = lower_32_bits(ring->wptr);
			WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
		} else {
			WREG32_SOC15(VCN, ring->me, mmUVD_RB_WPTR2, lower_32_bits(ring->wptr));
		}
	}
}

static const struct amdgpu_ring_funcs vcn_v3_0_enc_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_ENC,
	.align_mask = 0x3f,
	.nop = VCN_ENC_CMD_NO_OP,
	.vmhub = AMDGPU_MMHUB_0,
	.get_rptr = vcn_v3_0_enc_ring_get_rptr,
	.get_wptr = vcn_v3_0_enc_ring_get_wptr,
	.set_wptr = vcn_v3_0_enc_ring_set_wptr,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 4 +
		4 + /* vcn_v2_0_enc_ring_emit_vm_flush */
		5 + 5 + /* vcn_v2_0_enc_ring_emit_fence x2 vm fence */
		1, /* vcn_v2_0_enc_ring_insert_end */
	.emit_ib_size = 5, /* vcn_v2_0_enc_ring_emit_ib */
	.emit_ib = vcn_v2_0_enc_ring_emit_ib,
	.emit_fence = vcn_v2_0_enc_ring_emit_fence,
	.emit_vm_flush = vcn_v2_0_enc_ring_emit_vm_flush,
	.test_ring = amdgpu_vcn_enc_ring_test_ring,
	.test_ib = amdgpu_vcn_enc_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.insert_end = vcn_v2_0_enc_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = vcn_v2_0_enc_ring_emit_wreg,
	.emit_reg_wait = vcn_v2_0_enc_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

static void vcn_v3_0_set_dec_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		adev->vcn.inst[i].ring_dec.funcs = &vcn_v3_0_dec_ring_vm_funcs;
		adev->vcn.inst[i].ring_dec.me = i;
		DRM_INFO("VCN(%d) decode is enabled in VM mode\n", i);
	}
}

static void vcn_v3_0_set_enc_ring_funcs(struct amdgpu_device *adev)
{
	int i, j;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		for (j = 0; j < adev->vcn.num_enc_rings; ++j) {
			adev->vcn.inst[i].ring_enc[j].funcs = &vcn_v3_0_enc_ring_vm_funcs;
			adev->vcn.inst[i].ring_enc[j].me = i;
		}
		DRM_INFO("VCN(%d) encode is enabled in VM mode\n", i);
	}
}

static bool vcn_v3_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, ret = 1;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		ret &= (RREG32_SOC15(VCN, i, mmUVD_STATUS) == UVD_STATUS__IDLE);
	}

	return ret;
}

static int vcn_v3_0_wait_for_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, ret = 0;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		SOC15_WAIT_ON_RREG(VCN, i, mmUVD_STATUS, UVD_STATUS__IDLE,
			UVD_STATUS__IDLE, ret);
		if (ret)
			return ret;
	}

	return ret;
}

static int vcn_v3_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	return 0;
}

static int vcn_v3_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int ret;

	if(state == adev->vcn.cur_state)
		return 0;

	if (state == AMD_PG_STATE_GATE)
		ret = vcn_v3_0_stop(adev);
	else
		ret = vcn_v3_0_start(adev);

	if(!ret)
		adev->vcn.cur_state = state;

	return ret;
}

static int vcn_v3_0_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}

static int vcn_v3_0_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	uint32_t ip_instance;

	switch (entry->client_id) {
	case SOC15_IH_CLIENTID_VCN:
		ip_instance = 0;
		break;
	case SOC15_IH_CLIENTID_VCN1:
		ip_instance = 1;
		break;
	default:
		DRM_ERROR("Unhandled client id: %d\n", entry->client_id);
		return 0;
	}

	DRM_DEBUG("IH: VCN TRAP\n");

	switch (entry->src_id) {
	case VCN_2_0__SRCID__UVD_SYSTEM_MESSAGE_INTERRUPT:
		amdgpu_fence_process(&adev->vcn.inst[ip_instance].ring_dec);
		break;
	case VCN_2_0__SRCID__UVD_ENC_GENERAL_PURPOSE:
		amdgpu_fence_process(&adev->vcn.inst[ip_instance].ring_enc[0]);
		break;
	case VCN_2_0__SRCID__UVD_ENC_LOW_LATENCY:
		amdgpu_fence_process(&adev->vcn.inst[ip_instance].ring_enc[1]);
		break;
	default:
		DRM_ERROR("Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs vcn_v3_0_irq_funcs = {
	.set = vcn_v3_0_set_interrupt_state,
	.process = vcn_v3_0_process_interrupt,
};

static void vcn_v3_0_set_irq_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		adev->vcn.inst[i].irq.num_types = adev->vcn.num_enc_rings + 1;
		adev->vcn.inst[i].irq.funcs = &vcn_v3_0_irq_funcs;
	}
}

static const struct amd_ip_funcs vcn_v3_0_ip_funcs = {
	.name = "vcn_v3_0",
	.early_init = vcn_v3_0_early_init,
	.late_init = NULL,
	.sw_init = vcn_v3_0_sw_init,
	.sw_fini = vcn_v3_0_sw_fini,
	.hw_init = vcn_v3_0_hw_init,
	.hw_fini = vcn_v3_0_hw_fini,
	.suspend = vcn_v3_0_suspend,
	.resume = vcn_v3_0_resume,
	.is_idle = vcn_v3_0_is_idle,
	.wait_for_idle = vcn_v3_0_wait_for_idle,
	.check_soft_reset = NULL,
	.pre_soft_reset = NULL,
	.soft_reset = NULL,
	.post_soft_reset = NULL,
	.set_clockgating_state = vcn_v3_0_set_clockgating_state,
	.set_powergating_state = vcn_v3_0_set_powergating_state,
};

const struct amdgpu_ip_block_version vcn_v3_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_VCN,
	.major = 3,
	.minor = 0,
	.rev = 0,
	.funcs = &vcn_v3_0_ip_funcs,
};
