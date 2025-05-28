/*
 * Copyright 2022 Red Hat Inc.
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

static const struct nvkm_falcon_func
ga100_gsp_flcn = {
	.disable = gm200_flcn_disable,
	.enable = gm200_flcn_enable,
	.addr2 = 0x1000,
	.riscv_irqmask = 0x2b4,
	.reset_eng = gp102_flcn_reset_eng,
	.reset_wait_mem_scrubbing = gm200_flcn_reset_wait_mem_scrubbing,
	.bind_inst = gm200_flcn_bind_inst,
	.bind_stat = gm200_flcn_bind_stat,
	.bind_intr = true,
	.imem_pio = &gm200_flcn_imem_pio,
	.dmem_pio = &gm200_flcn_dmem_pio,
	.riscv_active = tu102_flcn_riscv_active,
	.intr_retrigger = ga100_flcn_intr_retrigger,
};

static const struct nvkm_gsp_func
ga100_gsp = {
	.flcn = &ga100_gsp_flcn,
	.fwsec = &tu102_gsp_fwsec,

	.sig_section = ".fwsignature_ga100",

	.booter.ctor = tu102_gsp_booter_ctor,

	.dtor = r535_gsp_dtor,
	.oneinit = tu102_gsp_oneinit,
	.init = tu102_gsp_init,
	.fini = tu102_gsp_fini,
	.reset = tu102_gsp_reset,

	.rm.gpu = &ga100_gpu,
};

static struct nvkm_gsp_fwif
ga100_gsps[] = {
	{  1, tu102_gsp_load, &ga100_gsp, &r570_rm_tu102, "570.144" },
	{  0, tu102_gsp_load, &ga100_gsp, &r535_rm_tu102, "535.113.01" },
	{ -1, gv100_gsp_nofw, &gv100_gsp },
	{}
};

int
ga100_gsp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_gsp **pgsp)
{
	return nvkm_gsp_new_(ga100_gsps, device, type, inst, pgsp);
}

NVKM_GSP_FIRMWARE_BOOTER(ga100, 535.113.01);
NVKM_GSP_FIRMWARE_BOOTER(ga100, 570.144);
