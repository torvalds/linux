/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */


#include "acr.h"
#include "gm200.h"

#include <core/gpuobj.h>
#include <subdev/fb.h>
#include <engine/falcon.h>
#include <subdev/mc.h>

/**
 * gm200_secboot_run_blob() - run the given high-secure blob
 *
 */
int
gm200_secboot_run_blob(struct nvkm_secboot *sb, struct nvkm_gpuobj *blob)
{
	struct gm200_secboot *gsb = gm200_secboot(sb);
	struct nvkm_subdev *subdev = &gsb->base.subdev;
	struct nvkm_falcon *falcon = gsb->base.boot_falcon;
	struct nvkm_vma vma;
	int ret;

	ret = nvkm_falcon_get(falcon, subdev);
	if (ret)
		return ret;

	/* Map the HS firmware so the HS bootloader can see it */
	ret = nvkm_gpuobj_map(blob, gsb->vm, NV_MEM_ACCESS_RW, &vma);
	if (ret) {
		nvkm_falcon_put(falcon, subdev);
		return ret;
	}

	/* Reset and set the falcon up */
	ret = nvkm_falcon_reset(falcon);
	if (ret)
		goto end;
	nvkm_falcon_bind_context(falcon, gsb->inst);

	/* Load the HS bootloader into the falcon's IMEM/DMEM */
	ret = sb->acr->func->load(sb->acr, &gsb->base, blob, vma.offset);
	if (ret)
		goto end;

	/* Disable interrupts as we will poll for the HALT bit */
	nvkm_mc_intr_mask(sb->subdev.device, falcon->owner->index, false);

	/* Set default error value in mailbox register */
	nvkm_falcon_wr32(falcon, 0x040, 0xdeada5a5);

	/* Start the HS bootloader */
	nvkm_falcon_set_start_addr(falcon, sb->acr->start_address);
	nvkm_falcon_start(falcon);
	ret = nvkm_falcon_wait_for_halt(falcon, 100);
	if (ret)
		goto end;

	/* If mailbox register contains an error code, then ACR has failed */
	ret = nvkm_falcon_rd32(falcon, 0x040);
	if (ret) {
		nvkm_error(subdev, "ACR boot failed, ret 0x%08x", ret);
		ret = -EINVAL;
		goto end;
	}

end:
	/* Reenable interrupts */
	nvkm_mc_intr_mask(sb->subdev.device, falcon->owner->index, true);

	/* We don't need the ACR firmware anymore */
	nvkm_gpuobj_unmap(&vma);
	nvkm_falcon_put(falcon, subdev);

	return ret;
}

int
gm200_secboot_oneinit(struct nvkm_secboot *sb)
{
	struct gm200_secboot *gsb = gm200_secboot(sb);
	struct nvkm_device *device = sb->subdev.device;
	struct nvkm_vm *vm;
	const u64 vm_area_len = 600 * 1024;
	int ret;

	/* Allocate instance block and VM */
	ret = nvkm_gpuobj_new(device, 0x1000, 0, true, NULL, &gsb->inst);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x8000, 0, true, NULL, &gsb->pgd);
	if (ret)
		return ret;

	ret = nvkm_vm_new(device, 0, vm_area_len, 0, NULL, &vm);
	if (ret)
		return ret;

	atomic_inc(&vm->engref[NVKM_SUBDEV_PMU]);

	ret = nvkm_vm_ref(vm, &gsb->vm, gsb->pgd);
	nvkm_vm_ref(NULL, &vm, NULL);
	if (ret)
		return ret;

	nvkm_kmap(gsb->inst);
	nvkm_wo32(gsb->inst, 0x200, lower_32_bits(gsb->pgd->addr));
	nvkm_wo32(gsb->inst, 0x204, upper_32_bits(gsb->pgd->addr));
	nvkm_wo32(gsb->inst, 0x208, lower_32_bits(vm_area_len - 1));
	nvkm_wo32(gsb->inst, 0x20c, upper_32_bits(vm_area_len - 1));
	nvkm_done(gsb->inst);

	if (sb->acr->func->oneinit) {
		ret = sb->acr->func->oneinit(sb->acr, sb);
		if (ret)
			return ret;
	}

	return 0;
}

int
gm200_secboot_fini(struct nvkm_secboot *sb, bool suspend)
{
	int ret = 0;

	if (sb->acr->func->fini)
		ret = sb->acr->func->fini(sb->acr, sb, suspend);

	return ret;
}

void *
gm200_secboot_dtor(struct nvkm_secboot *sb)
{
	struct gm200_secboot *gsb = gm200_secboot(sb);

	sb->acr->func->dtor(sb->acr);

	nvkm_vm_ref(NULL, &gsb->vm, gsb->pgd);
	nvkm_gpuobj_del(&gsb->pgd);
	nvkm_gpuobj_del(&gsb->inst);

	return gsb;
}


static const struct nvkm_secboot_func
gm200_secboot = {
	.dtor = gm200_secboot_dtor,
	.oneinit = gm200_secboot_oneinit,
	.fini = gm200_secboot_fini,
	.run_blob = gm200_secboot_run_blob,
};

int
gm200_secboot_new(struct nvkm_device *device, int index,
		  struct nvkm_secboot **psb)
{
	int ret;
	struct gm200_secboot *gsb;
	struct nvkm_acr *acr;

	acr = acr_r361_new(BIT(NVKM_SECBOOT_FALCON_FECS) |
			   BIT(NVKM_SECBOOT_FALCON_GPCCS));
	if (IS_ERR(acr))
		return PTR_ERR(acr);

	gsb = kzalloc(sizeof(*gsb), GFP_KERNEL);
	if (!gsb) {
		psb = NULL;
		return -ENOMEM;
	}
	*psb = &gsb->base;

	ret = nvkm_secboot_ctor(&gm200_secboot, acr, device, index, &gsb->base);
	if (ret)
		return ret;

	return 0;
}


MODULE_FIRMWARE("nvidia/gm200/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gm200/acr/ucode_load.bin");
MODULE_FIRMWARE("nvidia/gm200/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/sw_method_init.bin");

MODULE_FIRMWARE("nvidia/gm204/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gm204/acr/ucode_load.bin");
MODULE_FIRMWARE("nvidia/gm204/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/sw_method_init.bin");

MODULE_FIRMWARE("nvidia/gm206/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gm206/acr/ucode_load.bin");
MODULE_FIRMWARE("nvidia/gm206/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/sw_method_init.bin");

MODULE_FIRMWARE("nvidia/gp100/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp100/acr/ucode_load.bin");
MODULE_FIRMWARE("nvidia/gp100/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/sw_method_init.bin");
