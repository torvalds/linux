/*
 * Copyright 2019 Red Hat Inc.
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
#include "priv.h"
#include <subdev/acr.h>
#include <subdev/gsp.h>

#include <nvfw/sec2.h>

static const struct nvkm_falcon_func
tu102_sec2_flcn = {
	.disable = gm200_flcn_disable,
	.enable = gm200_flcn_enable,
	.reset_pmc = true,
	.reset_eng = gp102_flcn_reset_eng,
	.reset_wait_mem_scrubbing = gm200_flcn_reset_wait_mem_scrubbing,
	.debug = 0x408,
	.bind_inst = gm200_flcn_bind_inst,
	.bind_stat = gm200_flcn_bind_stat,
	.bind_intr = true,
	.imem_pio = &gm200_flcn_imem_pio,
	.dmem_pio = &gm200_flcn_dmem_pio,
	.emem_addr = 0x01000000,
	.emem_pio = &gp102_flcn_emem_pio,
	.start = nvkm_falcon_v1_start,
	.cmdq = { 0xc00, 0xc04, 8 },
	.msgq = { 0xc80, 0xc84, 8 },
};

static const struct nvkm_sec2_func
tu102_sec2 = {
	.flcn = &tu102_sec2_flcn,
	.unit_unload = NV_SEC2_UNIT_V2_UNLOAD,
	.unit_acr = NV_SEC2_UNIT_V2_ACR,
	.intr = gp102_sec2_intr,
	.initmsg = gp102_sec2_initmsg,
};

MODULE_FIRMWARE("nvidia/tu102/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/tu102/sec2/image.bin");
MODULE_FIRMWARE("nvidia/tu102/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/tu104/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/tu104/sec2/image.bin");
MODULE_FIRMWARE("nvidia/tu104/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/tu106/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/tu106/sec2/image.bin");
MODULE_FIRMWARE("nvidia/tu106/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/tu116/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/tu116/sec2/image.bin");
MODULE_FIRMWARE("nvidia/tu116/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/tu117/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/tu117/sec2/image.bin");
MODULE_FIRMWARE("nvidia/tu117/sec2/sig.bin");

static const struct nvkm_sec2_fwif
tu102_sec2_fwif[] = {
	{  0, gp102_sec2_load, &tu102_sec2, &gp102_sec2_acr_1 },
	{ -1, gp102_sec2_nofw, &tu102_sec2 }
};

int
tu102_sec2_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_sec2 **psec2)
{
	/* TOP info wasn't updated on Turing to reflect the PRI
	 * address change for some reason.  We override it here.
	 */
	const u32 addr = 0x840000;

	if (nvkm_gsp_rm(device->gsp))
		return r535_sec2_new(&tu102_sec2, device, type, inst, addr, psec2);

	return nvkm_sec2_new_(tu102_sec2_fwif, device, type, inst, addr, psec2);
}
