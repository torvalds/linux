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
#include "priv.h"
#include <subdev/acr.h>

static const struct nvkm_acr_lsf_func
gp102_sec2_acr_0 = {
};

const struct nvkm_sec2_func
gp102_sec2 = {
};

MODULE_FIRMWARE("nvidia/gp102/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/gp102/sec2/image.bin");
MODULE_FIRMWARE("nvidia/gp102/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/image.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/image.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/image.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/sig.bin");

const struct nvkm_acr_lsf_func
gp102_sec2_acr_1 = {
};

int
gp102_sec2_load(struct nvkm_sec2 *sec2, int ver,
		const struct nvkm_sec2_fwif *fwif)
{
	return nvkm_acr_lsfw_load_sig_image_desc_v1(&sec2->engine.subdev,
						    sec2->falcon,
						    NVKM_ACR_LSF_SEC2, "sec2/",
						    ver, fwif->acr);
}

MODULE_FIRMWARE("nvidia/gp102/sec2/desc-1.bin");
MODULE_FIRMWARE("nvidia/gp102/sec2/image-1.bin");
MODULE_FIRMWARE("nvidia/gp102/sec2/sig-1.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/desc-1.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/image-1.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/sig-1.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/desc-1.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/image-1.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/sig-1.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/desc-1.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/image-1.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/sig-1.bin");

static const struct nvkm_sec2_fwif
gp102_sec2_fwif[] = {
	{ 1, gp102_sec2_load, &gp102_sec2, &gp102_sec2_acr_1 },
	{ 0, gp102_sec2_load, &gp102_sec2, &gp102_sec2_acr_0 },
	{}
};

int
gp102_sec2_new(struct nvkm_device *device, int index, struct nvkm_sec2 **psec2)
{
	return nvkm_sec2_new_(gp102_sec2_fwif, device, index, 0, psec2);
}
