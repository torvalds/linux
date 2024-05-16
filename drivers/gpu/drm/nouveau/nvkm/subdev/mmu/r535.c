/*
 * Copyright 2023 Red Hat Inc.
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
#include "vmm.h"

#include <nvrm/nvtypes.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/class/cl90f1.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/ctrl/ctrl90f1.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/nvos.h>

static int
r535_mmu_promote_vmm(struct nvkm_vmm *vmm)
{
	NV_VASPACE_ALLOCATION_PARAMETERS *args;
	int ret;

	ret = nvkm_gsp_client_device_ctor(vmm->mmu->subdev.device->gsp,
					  &vmm->rm.client, &vmm->rm.device);
	if (ret)
		return ret;

	args = nvkm_gsp_rm_alloc_get(&vmm->rm.device.object, 0x90f10000, FERMI_VASPACE_A,
				     sizeof(*args), &vmm->rm.object);
	if (IS_ERR(args))
		return PTR_ERR(args);

	args->index = NV_VASPACE_ALLOCATION_INDEX_GPU_NEW;

	ret = nvkm_gsp_rm_alloc_wr(&vmm->rm.object, args);
	if (ret)
		return ret;

	{
		NV90F1_CTRL_VASPACE_COPY_SERVER_RESERVED_PDES_PARAMS *ctrl;

		mutex_lock(&vmm->mutex.vmm);
		ret = nvkm_vmm_get_locked(vmm, true, false, false, 0x1d, 32, 0x20000000,
					  &vmm->rm.rsvd);
		mutex_unlock(&vmm->mutex.vmm);
		if (ret)
			return ret;

		ctrl = nvkm_gsp_rm_ctrl_get(&vmm->rm.object,
					    NV90F1_CTRL_CMD_VASPACE_COPY_SERVER_RESERVED_PDES,
					    sizeof(*ctrl));
		if (IS_ERR(ctrl))
			return PTR_ERR(ctrl);

		ctrl->pageSize = 0x20000000;
		ctrl->virtAddrLo = vmm->rm.rsvd->addr;
		ctrl->virtAddrHi = vmm->rm.rsvd->addr + vmm->rm.rsvd->size - 1;
		ctrl->numLevelsToCopy = vmm->pd->pde[0]->pde[0] ? 3 : 2;
		ctrl->levels[0].physAddress = vmm->pd->pt[0]->addr;
		ctrl->levels[0].size = 0x20;
		ctrl->levels[0].aperture = 1;
		ctrl->levels[0].pageShift = 0x2f;
		ctrl->levels[1].physAddress = vmm->pd->pde[0]->pt[0]->addr;
		ctrl->levels[1].size = 0x1000;
		ctrl->levels[1].aperture = 1;
		ctrl->levels[1].pageShift = 0x26;
		if (vmm->pd->pde[0]->pde[0]) {
			ctrl->levels[2].physAddress = vmm->pd->pde[0]->pde[0]->pt[0]->addr;
			ctrl->levels[2].size = 0x1000;
			ctrl->levels[2].aperture = 1;
			ctrl->levels[2].pageShift = 0x1d;
		}

		ret = nvkm_gsp_rm_ctrl_wr(&vmm->rm.object, ctrl);
	}

	return ret;
}

static void
r535_mmu_dtor(struct nvkm_mmu *mmu)
{
	kfree(mmu->func);
}

int
r535_mmu_new(const struct nvkm_mmu_func *hw,
	     struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	     struct nvkm_mmu **pmmu)
{
	struct nvkm_mmu_func *rm;
	int ret;

	if (!(rm = kzalloc(sizeof(*rm), GFP_KERNEL)))
		return -ENOMEM;

	rm->dtor = r535_mmu_dtor;
	rm->dma_bits = hw->dma_bits;
	rm->mmu = hw->mmu;
	rm->mem = hw->mem;
	rm->vmm = hw->vmm;
	rm->kind = hw->kind;
	rm->kind_sys = hw->kind_sys;
	rm->promote_vmm = r535_mmu_promote_vmm;

	ret = nvkm_mmu_new_(rm, device, type, inst, pmmu);
	if (ret)
		kfree(rm);

	return ret;
}
