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

static const struct nvkm_falcon_func
tu102_sec2_flcn = {
	.debug = 0x408,
	.fbif = 0x600,
	.load_imem = nvkm_falcon_v1_load_imem,
	.load_dmem = nvkm_falcon_v1_load_dmem,
	.read_dmem = nvkm_falcon_v1_read_dmem,
	.emem_addr = 0x01000000,
	.bind_context = gp102_sec2_flcn_bind_context,
	.wait_for_halt = nvkm_falcon_v1_wait_for_halt,
	.clear_interrupt = nvkm_falcon_v1_clear_interrupt,
	.set_start_addr = nvkm_falcon_v1_set_start_addr,
	.start = nvkm_falcon_v1_start,
	.enable = nvkm_falcon_v1_enable,
	.disable = nvkm_falcon_v1_disable,
	.cmdq = { 0xc00, 0xc04, 8 },
	.msgq = { 0xc80, 0xc84, 8 },
};

static const struct nvkm_sec2_func
tu102_sec2 = {
	.flcn = &tu102_sec2_flcn,
	.unit_acr = 0x07,
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
tu102_sec2_new(struct nvkm_device *device, int index, struct nvkm_sec2 **psec2)
{
	/* TOP info wasn't updated on Turing to reflect the PRI
	 * address change for some reason.  We override it here.
	 */
	return nvkm_sec2_new_(tu102_sec2_fwif, device, index, 0x840000, psec2);
}
