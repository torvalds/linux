/*
 * Copyright 2021 Red Hat Inc.
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
#include "gf100.h"
#include "ram.h"

#include <subdev/gsp.h>
#include <engine/nvdec.h>

u64
ga102_fb_vidmem_size(struct nvkm_fb *fb)
{
	return (u64)nvkm_rd32(fb->subdev.device, 0x1183a4) << 20;
}

static int
ga102_fb_oneinit(struct nvkm_fb *fb)
{
	struct nvkm_subdev *subdev = &fb->subdev;

	nvkm_falcon_fw_ctor_hs_v2(&ga102_flcn_fw, "mem-unlock", subdev, "nvdec/scrubber",
				  0, &subdev->device->nvdec[0]->falcon, &fb->vpr_scrubber);

	return gf100_fb_oneinit(fb);
}

static const struct nvkm_fb_func
ga102_fb = {
	.dtor = gf100_fb_dtor,
	.oneinit = ga102_fb_oneinit,
	.init = gm200_fb_init,
	.init_page = gv100_fb_init_page,
	.init_unkn = gp100_fb_init_unkn,
	.sysmem.flush_page_init = gf100_fb_sysmem_flush_page_init,
	.vidmem.size = ga102_fb_vidmem_size,
	.ram_new = gp102_ram_new,
	.default_bigpage = 16,
	.vpr.scrub_required = tu102_fb_vpr_scrub_required,
	.vpr.scrub = gp102_fb_vpr_scrub,
};

int
ga102_fb_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_fb **pfb)
{
	if (nvkm_gsp_rm(device->gsp))
		return r535_fb_new(&ga102_fb, device, type, inst, pfb);

	return gf100_fb_new_(&ga102_fb, device, type, inst, pfb);
}

MODULE_FIRMWARE("nvidia/ga102/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/ga103/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/ga104/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/ga106/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/ga107/nvdec/scrubber.bin");
