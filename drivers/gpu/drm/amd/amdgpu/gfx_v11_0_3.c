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
 *
 */

#include "amdgpu.h"
#include "soc21.h"
#include "gc/gc_11_0_3_offset.h"
#include "gc/gc_11_0_3_sh_mask.h"
#include "ivsrcid/gfx/irqsrcs_gfx_11_0_0.h"
#include "soc15.h"
#include "soc15d.h"
#include "gfx_v11_0.h"


static int gfx_v11_0_3_rlc_gc_fed_irq(struct amdgpu_device *adev,
				  struct amdgpu_irq_src *source,
				  struct amdgpu_iv_entry *entry)
{
	uint32_t rlc_status0 = 0, rlc_status1 = 0;
	struct ras_common_if *ras_if = NULL;
	struct ras_dispatch_if ih_data = {
		.entry = entry,
	};

	rlc_status0 = RREG32(SOC15_REG_OFFSET(GC, 0, regRLC_RLCS_FED_STATUS_0));
	rlc_status1 = RREG32(SOC15_REG_OFFSET(GC, 0, regRLC_RLCS_FED_STATUS_1));

	if (!rlc_status0 && !rlc_status1) {
		dev_warn(adev->dev, "RLC_GC_FED irq is generated, but rlc_status0 and rlc_status1 are empty!\n");
		return 0;
	}

	/* Use RLC_RLCS_FED_STATUS_0/1 to distinguish FED error block. */
	if (REG_GET_FIELD(rlc_status0, RLC_RLCS_FED_STATUS_0, SDMA0_FED_ERR) ||
	    REG_GET_FIELD(rlc_status0, RLC_RLCS_FED_STATUS_0, SDMA1_FED_ERR))
		ras_if = adev->sdma.ras_if;
	else
		ras_if = adev->gfx.ras_if;

	if (!ras_if) {
		dev_err(adev->dev, "Gfx or sdma ras block not initialized, rlc_status0:0x%x.\n",
				rlc_status0);
		return -EINVAL;
	}

	dev_warn(adev->dev, "RLC %s FED IRQ\n", ras_if->name);

	if (!amdgpu_sriov_vf(adev)) {
		ih_data.head = *ras_if;
		amdgpu_ras_interrupt_dispatch(adev, &ih_data);
	} else {
		if (adev->virt.ops && adev->virt.ops->ras_poison_handler)
			adev->virt.ops->ras_poison_handler(adev);
		else
			dev_warn(adev->dev,
				"No ras_poison_handler interface in SRIOV for %s!\n", ras_if->name);
	}

	return 0;
}

static int gfx_v11_0_3_poison_consumption_handler(struct amdgpu_device *adev,
					struct amdgpu_iv_entry *entry)
{
	/* Workaround: when vmid and pasid are both zero, trigger gpu reset in KGD. */
	if (entry && (entry->client_id == SOC21_IH_CLIENTID_GFX) &&
	    (entry->src_id == GFX_11_0_0__SRCID__RLC_GC_FED_INTERRUPT) &&
	     !entry->vmid && !entry->pasid) {
		uint32_t rlc_status0 = 0;

		rlc_status0 = RREG32_SOC15(GC, 0, regRLC_RLCS_FED_STATUS_0);

		if (REG_GET_FIELD(rlc_status0, RLC_RLCS_FED_STATUS_0, SDMA0_FED_ERR) ||
		    REG_GET_FIELD(rlc_status0, RLC_RLCS_FED_STATUS_0, SDMA1_FED_ERR)) {
			struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

			ras->gpu_reset_flags |= AMDGPU_RAS_GPU_RESET_MODE2_RESET;
		}

		amdgpu_ras_reset_gpu(adev);
	}

	return 0;
}

struct amdgpu_gfx_ras gfx_v11_0_3_ras = {
	.rlc_gc_fed_irq = gfx_v11_0_3_rlc_gc_fed_irq,
	.poison_consumption_handler = gfx_v11_0_3_poison_consumption_handler,
};
