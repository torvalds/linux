/*
 * Copyright 2017 Red Hat Inc.
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
#include "gm200.h"
#include "acr.h"

int
gp108_secboot_new(struct nvkm_device *device, int index,
		  struct nvkm_secboot **psb)
{
	struct gm200_secboot *gsb;
	struct nvkm_acr *acr;

	acr = acr_r370_new(NVKM_SECBOOT_FALCON_SEC2,
			   BIT(NVKM_SECBOOT_FALCON_FECS) |
			   BIT(NVKM_SECBOOT_FALCON_GPCCS) |
			   BIT(NVKM_SECBOOT_FALCON_SEC2));
	if (IS_ERR(acr))
		return PTR_ERR(acr);

	if (!(gsb = kzalloc(sizeof(*gsb), GFP_KERNEL))) {
		acr->func->dtor(acr);
		return -ENOMEM;
	}
	*psb = &gsb->base;

	return nvkm_secboot_ctor(&gp102_secboot, acr, device, index, &gsb->base);
}

MODULE_FIRMWARE("nvidia/gp108/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp108/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gp108/acr/ucode_load.bin");
MODULE_FIRMWARE("nvidia/gp108/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gp108/gr/sw_method_init.bin");
MODULE_FIRMWARE("nvidia/gp108/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/gp108/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/gp108/sec2/image.bin");
MODULE_FIRMWARE("nvidia/gp108/sec2/sig.bin");
