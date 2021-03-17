/*
 * Copyright 2016 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "gf100.h"
#include "ram.h"

#include <core/firmware.h>
#include <core/memory.h>
#include <nvfw/fw.h>
#include <nvfw/hs.h>
#include <engine/nvdec.h>

int
gp102_fb_vpr_scrub(struct nvkm_fb *fb)
{
	struct nvkm_subdev *subdev = &fb->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_falcon *falcon = &device->nvdec[0]->falcon;
	struct nvkm_blob *blob = &fb->vpr_scrubber;
	const struct nvfw_bin_hdr *hsbin_hdr;
	const struct nvfw_hs_header *fw_hdr;
	const struct nvfw_hs_load_header *lhdr;
	void *scrub_data;
	u32 patch_loc, patch_sig;
	int ret;

	nvkm_falcon_get(falcon, subdev);

	hsbin_hdr = nvfw_bin_hdr(subdev, blob->data);
	fw_hdr = nvfw_hs_header(subdev, blob->data + hsbin_hdr->header_offset);
	lhdr = nvfw_hs_load_header(subdev, blob->data + fw_hdr->hdr_offset);
	scrub_data = blob->data + hsbin_hdr->data_offset;

	patch_loc = *(u32 *)(blob->data + fw_hdr->patch_loc);
	patch_sig = *(u32 *)(blob->data + fw_hdr->patch_sig);
	if (falcon->debug) {
		memcpy(scrub_data + patch_loc,
		       blob->data + fw_hdr->sig_dbg_offset + patch_sig,
		       fw_hdr->sig_dbg_size);
	} else {
		memcpy(scrub_data + patch_loc,
		       blob->data + fw_hdr->sig_prod_offset + patch_sig,
		       fw_hdr->sig_prod_size);
	}

	nvkm_falcon_reset(falcon);
	nvkm_falcon_bind_context(falcon, NULL);

	nvkm_falcon_load_imem(falcon, scrub_data, lhdr->non_sec_code_off,
			      lhdr->non_sec_code_size,
			      lhdr->non_sec_code_off >> 8, 0, false);
	nvkm_falcon_load_imem(falcon, scrub_data + lhdr->apps[0],
			      ALIGN(lhdr->apps[0], 0x100),
			      lhdr->apps[1],
			      lhdr->apps[0] >> 8, 0, true);
	nvkm_falcon_load_dmem(falcon, scrub_data + lhdr->data_dma_base, 0,
			      lhdr->data_size, 0);

	nvkm_falcon_set_start_addr(falcon, 0x0);
	nvkm_falcon_start(falcon);

	ret = nvkm_falcon_wait_for_halt(falcon, 500);
	if (ret < 0) {
		ret = -ETIMEDOUT;
		goto end;
	}

	/* put nvdec in clean state - without reset it will remain in HS mode */
	nvkm_falcon_reset(falcon);
end:
	nvkm_falcon_put(falcon, subdev);
	return ret;
}

bool
gp102_fb_vpr_scrub_required(struct nvkm_fb *fb)
{
	struct nvkm_device *device = fb->subdev.device;
	nvkm_wr32(device, 0x100cd0, 0x2);
	return (nvkm_rd32(device, 0x100cd0) & 0x00000010) != 0;
}

static const struct nvkm_fb_func
gp102_fb = {
	.dtor = gf100_fb_dtor,
	.oneinit = gf100_fb_oneinit,
	.init = gp100_fb_init,
	.init_remapper = gp100_fb_init_remapper,
	.init_page = gm200_fb_init_page,
	.vpr.scrub_required = gp102_fb_vpr_scrub_required,
	.vpr.scrub = gp102_fb_vpr_scrub,
	.ram_new = gp100_ram_new,
};

int
gp102_fb_new_(const struct nvkm_fb_func *func, struct nvkm_device *device,
	      int index, struct nvkm_fb **pfb)
{
	int ret = gf100_fb_new_(func, device, index, pfb);
	if (ret)
		return ret;

	nvkm_firmware_load_blob(&(*pfb)->subdev, "nvdec/scrubber", "", 0,
				&(*pfb)->vpr_scrubber);
	return 0;
}

int
gp102_fb_new(struct nvkm_device *device, int index, struct nvkm_fb **pfb)
{
	return gp102_fb_new_(&gp102_fb, device, index, pfb);
}

MODULE_FIRMWARE("nvidia/gp102/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/gp104/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/gp106/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/gp107/nvdec/scrubber.bin");
MODULE_FIRMWARE("nvidia/gp108/nvdec/scrubber.bin");
