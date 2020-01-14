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

const struct nvkm_secboot_func
gp102_secboot = {
	.dtor = gm200_secboot_dtor,
	.oneinit = gm200_secboot_oneinit,
	.fini = gm200_secboot_fini,
	.run_blob = gm200_secboot_run_blob,
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
