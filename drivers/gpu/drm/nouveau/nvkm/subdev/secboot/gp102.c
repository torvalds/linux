/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
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

#include "ls_ucode.h"
#include "hs_ucode.h"
#include <subdev/mc.h>
#include <subdev/timer.h>
#include <engine/falcon.h>
#include <engine/nvdec.h>

static bool
gp102_secboot_scrub_required(struct nvkm_secboot *sb)
{
	struct nvkm_subdev *subdev = &sb->subdev;
	struct nvkm_device *device = subdev->device;
	u32 reg;

	nvkm_wr32(device, 0x100cd0, 0x2);
	reg = nvkm_rd32(device, 0x100cd0);

	return (reg & BIT(4));
}

static int
gp102_run_secure_scrub(struct nvkm_secboot *sb)
{
	struct nvkm_subdev *subdev = &sb->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_engine *engine;
	struct nvkm_falcon *falcon;
	void *scrub_image;
	struct fw_bin_header *hsbin_hdr;
	struct hsf_fw_header *fw_hdr;
	struct hsf_load_header *lhdr;
	void *scrub_data;
	int ret;

	nvkm_debug(subdev, "running VPR scrubber binary on NVDEC...\n");

	engine = nvkm_engine_ref(&device->nvdec[0]->engine);
	if (IS_ERR(engine))
		return PTR_ERR(engine);
	falcon = device->nvdec[0]->falcon;

	nvkm_falcon_get(falcon, &sb->subdev);

	scrub_image = hs_ucode_load_blob(subdev, falcon, "nvdec/scrubber");
	if (IS_ERR(scrub_image))
		return PTR_ERR(scrub_image);

	nvkm_falcon_reset(falcon);
	nvkm_falcon_bind_context(falcon, NULL);

	hsbin_hdr = scrub_image;
	fw_hdr = scrub_image + hsbin_hdr->header_offset;
	lhdr = scrub_image + fw_hdr->hdr_offset;
	scrub_data = scrub_image + hsbin_hdr->data_offset;

	nvkm_falcon_load_imem(falcon, scrub_data, lhdr->non_sec_code_off,
			      lhdr->non_sec_code_size,
			      lhdr->non_sec_code_off >> 8, 0, false);
	nvkm_falcon_load_imem(falcon, scrub_data + lhdr->apps[0],
			      ALIGN(lhdr->apps[0], 0x100),
			      lhdr->apps[1],
			      lhdr->apps[0] >> 8, 0, true);
	nvkm_falcon_load_dmem(falcon, scrub_data + lhdr->data_dma_base, 0,
			      lhdr->data_size, 0);

	kfree(scrub_image);

	nvkm_falcon_set_start_addr(falcon, 0x0);
	nvkm_falcon_start(falcon);

	ret = nvkm_falcon_wait_for_halt(falcon, 500);
	if (ret < 0) {
		nvkm_error(subdev, "failed to run VPR scrubber binary!\n");
		ret = -ETIMEDOUT;
		goto end;
	}

	/* put nvdec in clean state - without reset it will remain in HS mode */
	nvkm_falcon_reset(falcon);

	if (gp102_secboot_scrub_required(sb)) {
		nvkm_error(subdev, "VPR scrubber binary failed!\n");
		ret = -EINVAL;
		goto end;
	}

	nvkm_debug(subdev, "VPR scrub successfully completed\n");

end:
	nvkm_falcon_put(falcon, &sb->subdev);
	nvkm_engine_unref(&engine);
	return ret;
}

static int
gp102_secboot_run_blob(struct nvkm_secboot *sb, struct nvkm_gpuobj *blob,
		       struct nvkm_falcon *falcon)
{
	int ret;

	/* make sure the VPR region is unlocked */
	if (gp102_secboot_scrub_required(sb)) {
		ret = gp102_run_secure_scrub(sb);
		if (ret)
			return ret;
	}

	return gm200_secboot_run_blob(sb, blob, falcon);
}

const struct nvkm_secboot_func
gp102_secboot = {
	.dtor = gm200_secboot_dtor,
	.oneinit = gm200_secboot_oneinit,
	.fini = gm200_secboot_fini,
	.run_blob = gp102_secboot_run_blob,
};

int
gp102_secboot_new(struct nvkm_device *device, int index,
		  struct nvkm_secboot **psb)
{
	int ret;
	struct gm200_secboot *gsb;
	struct nvkm_acr *acr;

	acr = acr_r367_new(NVKM_SECBOOT_FALCON_SEC2,
			   BIT(NVKM_SECBOOT_FALCON_FECS) |
			   BIT(NVKM_SECBOOT_FALCON_GPCCS) |
			   BIT(NVKM_SECBOOT_FALCON_SEC2));
	if (IS_ERR(acr))
		return PTR_ERR(acr);

	gsb = kzalloc(sizeof(*gsb), GFP_KERNEL);
	if (!gsb) {
		psb = NULL;
		return -ENOMEM;
	}
	*psb = &gsb->base;

	ret = nvkm_secboot_ctor(&gp102_secboot, acr, device, index, &gsb->base);
	if (ret)
		return ret;

	return 0;
}

MODULE_FIRMWARE("nvidia/gp102/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp102/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gp102/acr/ucode_load.bin");
MODULE_FIRMWARE("nvidia/gp102/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gp102/gr/sw_method_init.bin");
MODULE_FIRMWARE("nvidia/gp102/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/gp102/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/gp102/sec2/image.bin");
MODULE_FIRMWARE("nvidia/gp102/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/gp102/sec2/desc-1.bin");
MODULE_FIRMWARE("nvidia/gp102/sec2/image-1.bin");
MODULE_FIRMWARE("nvidia/gp102/sec2/sig-1.bin");
MODULE_FIRMWARE("nvidia/gp104/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp104/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gp104/acr/ucode_load.bin");
MODULE_FIRMWARE("nvidia/gp104/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gp104/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gp104/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gp104/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gp104/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gp104/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gp104/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gp104/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gp104/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gp104/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gp104/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gp104/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gp104/gr/sw_method_init.bin");
MODULE_FIRMWARE("nvidia/gp104/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/image.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/desc-1.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/image-1.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/sig-1.bin");
MODULE_FIRMWARE("nvidia/gp106/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp106/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gp106/acr/ucode_load.bin");
MODULE_FIRMWARE("nvidia/gp106/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gp106/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gp106/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gp106/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gp106/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gp106/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gp106/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gp106/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gp106/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gp106/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gp106/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gp106/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gp106/gr/sw_method_init.bin");
MODULE_FIRMWARE("nvidia/gp106/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/image.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/desc-1.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/image-1.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/sig-1.bin");
MODULE_FIRMWARE("nvidia/gp107/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp107/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gp107/acr/ucode_load.bin");
MODULE_FIRMWARE("nvidia/gp107/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gp107/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gp107/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gp107/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gp107/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gp107/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gp107/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gp107/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gp107/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gp107/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gp107/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gp107/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gp107/gr/sw_method_init.bin");
MODULE_FIRMWARE("nvidia/gp107/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/image.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/desc-1.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/image-1.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/sig-1.bin");
