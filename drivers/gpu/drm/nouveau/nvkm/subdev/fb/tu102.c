/*
 * Copyright 2018 Red Hat Inc.
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

bool
tu102_fb_vpr_scrub_required(struct nvkm_fb *fb)
{
	return (nvkm_rd32(fb->subdev.device, 0x1fa80c) & 0x00000010) != 0;
}

static const struct nvkm_fb_func
tu102_fb = {
	.dtor = gf100_fb_dtor,
	.oneinit = gp102_fb_oneinit,
	.init = gm200_fb_init,
	.init_page = gv100_fb_init_page,
	.init_unkn = gp100_fb_init_unkn,
	.sysmem.flush_page_init = gf100_fb_sysmem_flush_page_init,
	.vidmem.size = gp102_fb_vidmem_size,
	.vpr.scrub_required = tu102_fb_vpr_scrub_required,
	.vpr.scrub = gp102_fb_vpr_scrub,
	.ram_new = gp102_ram_new,
	.default_bigpage = 16,
};

int
tu102_fb_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_fb **pfb)
{
	return gf100_fb_new_(&tu102_fb, device, type, inst, pfb);
}

MODULE_FIRMWARE("nvidia/tu102/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/tu104/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/tu106/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/tu116/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/tu117/nvdec/scrubber.bin");
