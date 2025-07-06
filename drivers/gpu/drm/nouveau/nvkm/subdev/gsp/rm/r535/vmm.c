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
#include <subdev/mmu/vmm.h>

#include <nvhw/drf.h>
#include "nvrm/vmm.h"

void
r535_mmu_vaspace_del(struct nvkm_vmm *vmm)
{
	if (vmm->rm.external) {
		NV0080_CTRL_DMA_UNSET_PAGE_DIRECTORY_PARAMS *ctrl;

		ctrl = nvkm_gsp_rm_ctrl_get(&vmm->rm.device.object,
					    NV0080_CTRL_CMD_DMA_UNSET_PAGE_DIRECTORY,
					    sizeof(*ctrl));
		if (!IS_ERR(ctrl)) {
			ctrl->hVASpace = vmm->rm.object.handle;

			WARN_ON(nvkm_gsp_rm_ctrl_wr(&vmm->rm.device.object, ctrl));
		}

		vmm->rm.external = false;
	}

	nvkm_gsp_rm_free(&vmm->rm.object);
	nvkm_gsp_device_dtor(&vmm->rm.device);
	nvkm_gsp_client_dtor(&vmm->rm.client);

	nvkm_vmm_put(vmm, &vmm->rm.rsvd);
}

int
r535_mmu_vaspace_new(struct nvkm_vmm *vmm, u32 handle, bool external)
{
	NV_VASPACE_ALLOCATION_PARAMETERS *args;
	int ret;

	ret = nvkm_gsp_client_device_ctor(vmm->mmu->subdev.device->gsp,
					  &vmm->rm.client, &vmm->rm.device);
	if (ret)
		return ret;

	args = nvkm_gsp_rm_alloc_get(&vmm->rm.device.object, handle, FERMI_VASPACE_A,
				     sizeof(*args), &vmm->rm.object);
	if (IS_ERR(args))
		return PTR_ERR(args);

	args->index = NV_VASPACE_ALLOCATION_INDEX_GPU_NEW;
	if (external)
		args->flags = NV_VASPACE_ALLOCATION_FLAGS_IS_EXTERNALLY_OWNED;

	ret = nvkm_gsp_rm_alloc_wr(&vmm->rm.object, args);
	if (ret)
		return ret;

	if (!external) {
		NV90F1_CTRL_VASPACE_COPY_SERVER_RESERVED_PDES_PARAMS *ctrl;
		u8 page_shift = 29; /* 512MiB */
		const u64 page_size = BIT_ULL(page_shift);
		const struct nvkm_vmm_page *page;
		const struct nvkm_vmm_desc *desc;
		struct nvkm_vmm_pt *pd = vmm->pd;

		for (page = vmm->func->page; page->shift; page++) {
			if (page->shift == page_shift)
				break;
		}

		if (WARN_ON(!page->shift))
			return -EINVAL;

		mutex_lock(&vmm->mutex.vmm);
		ret = nvkm_vmm_get_locked(vmm, true, false, false, page_shift, 32, page_size,
					  &vmm->rm.rsvd);
		mutex_unlock(&vmm->mutex.vmm);
		if (ret)
			return ret;

		/* Some parts of RM expect the server-reserved area to be in a specific location. */
		if (WARN_ON(vmm->rm.rsvd->addr != SPLIT_VAS_SERVER_RM_MANAGED_VA_START ||
			    vmm->rm.rsvd->size != SPLIT_VAS_SERVER_RM_MANAGED_VA_SIZE))
			return -EINVAL;

		ctrl = nvkm_gsp_rm_ctrl_get(&vmm->rm.object,
					    NV90F1_CTRL_CMD_VASPACE_COPY_SERVER_RESERVED_PDES,
					    sizeof(*ctrl));
		if (IS_ERR(ctrl))
			return PTR_ERR(ctrl);

		ctrl->pageSize = page_size;
		ctrl->virtAddrLo = vmm->rm.rsvd->addr;
		ctrl->virtAddrHi = vmm->rm.rsvd->addr + vmm->rm.rsvd->size - 1;

		for (desc = page->desc; desc->bits; desc++) {
			ctrl->numLevelsToCopy++;
			page_shift += desc->bits;
		}
		desc--;

		for (int i = 0; i < ctrl->numLevelsToCopy; i++, desc--) {
			page_shift -= desc->bits;

			ctrl->levels[i].physAddress = pd->pt[0]->addr;
			ctrl->levels[i].size = BIT_ULL(desc->bits) * desc->size;
			ctrl->levels[i].aperture = 1;
			ctrl->levels[i].pageShift = page_shift;

			pd = pd->pde[0];
		}

		ret = nvkm_gsp_rm_ctrl_wr(&vmm->rm.object, ctrl);
	} else {
		NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_PARAMS *ctrl;

		ctrl = nvkm_gsp_rm_ctrl_get(&vmm->rm.device.object,
					    NV0080_CTRL_CMD_DMA_SET_PAGE_DIRECTORY,
					    sizeof(*ctrl));
		if (IS_ERR(ctrl))
			return PTR_ERR(ctrl);

		ctrl->physAddress = vmm->pd->pt[0]->addr;
		ctrl->numEntries = 1 << vmm->func->page[0].desc->bits;
		ctrl->flags = NVDEF(NV0080_CTRL_DMA_SET_PAGE_DIRECTORY, FLAGS, APERTURE, VIDMEM);
		ctrl->hVASpace = vmm->rm.object.handle;

		ret = nvkm_gsp_rm_ctrl_wr(&vmm->rm.device.object, ctrl);
		if (ret == 0)
			vmm->rm.external = true;
	}

	return ret;
}

static int
r535_mmu_promote_vmm(struct nvkm_vmm *vmm)
{
	return r535_mmu_vaspace_new(vmm, NVKM_RM_VASPACE, true);
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
