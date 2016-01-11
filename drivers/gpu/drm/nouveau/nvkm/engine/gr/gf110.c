/*
 * Copyright 2013 Red Hat Inc.
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
#include "ctxgf100.h"

#include <nvif/class.h>

/*******************************************************************************
 * PGRAPH register lists
 ******************************************************************************/

static const struct gf100_gr_init
gf110_gr_init_sm_0[] = {
	{ 0x419e00,   1, 0x04, 0x00000000 },
	{ 0x419ea0,   1, 0x04, 0x00000000 },
	{ 0x419ea4,   1, 0x04, 0x00000100 },
	{ 0x419ea8,   1, 0x04, 0x00001100 },
	{ 0x419eac,   1, 0x04, 0x11100f02 },
	{ 0x419eb0,   1, 0x04, 0x00000003 },
	{ 0x419eb4,   4, 0x04, 0x00000000 },
	{ 0x419ec8,   1, 0x04, 0x06060618 },
	{ 0x419ed0,   1, 0x04, 0x0eff0e38 },
	{ 0x419ed4,   1, 0x04, 0x011104f1 },
	{ 0x419edc,   1, 0x04, 0x00000000 },
	{ 0x419f00,   1, 0x04, 0x00000000 },
	{ 0x419f2c,   1, 0x04, 0x00000000 },
	{}
};

static const struct gf100_gr_pack
gf110_gr_pack_mmio[] = {
	{ gf100_gr_init_main_0 },
	{ gf100_gr_init_fe_0 },
	{ gf100_gr_init_pri_0 },
	{ gf100_gr_init_rstr2d_0 },
	{ gf100_gr_init_pd_0 },
	{ gf100_gr_init_ds_0 },
	{ gf100_gr_init_scc_0 },
	{ gf100_gr_init_prop_0 },
	{ gf100_gr_init_gpc_unk_0 },
	{ gf100_gr_init_setup_0 },
	{ gf100_gr_init_crstr_0 },
	{ gf108_gr_init_setup_1 },
	{ gf100_gr_init_zcull_0 },
	{ gf100_gr_init_gpm_0 },
	{ gf100_gr_init_gpc_unk_1 },
	{ gf100_gr_init_gcc_0 },
	{ gf100_gr_init_tpccs_0 },
	{ gf100_gr_init_tex_0 },
	{ gf100_gr_init_pe_0 },
	{ gf100_gr_init_l1c_0 },
	{ gf100_gr_init_wwdx_0 },
	{ gf100_gr_init_tpccs_1 },
	{ gf100_gr_init_mpc_0 },
	{ gf110_gr_init_sm_0 },
	{ gf100_gr_init_be_0 },
	{ gf100_gr_init_fe_1 },
	{ gf100_gr_init_pe_1 },
	{}
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

static const struct gf100_gr_func
gf110_gr = {
	.init = gf100_gr_init,
	.mmio = gf110_gr_pack_mmio,
	.fecs.ucode = &gf100_gr_fecs_ucode,
	.gpccs.ucode = &gf100_gr_gpccs_ucode,
	.grctx = &gf110_grctx,
	.sclass = {
		{ -1, -1, FERMI_TWOD_A },
		{ -1, -1, FERMI_MEMORY_TO_MEMORY_FORMAT_A },
		{ -1, -1, FERMI_A, &gf100_fermi },
		{ -1, -1, FERMI_B, &gf100_fermi },
		{ -1, -1, FERMI_C, &gf100_fermi },
		{ -1, -1, FERMI_COMPUTE_A },
		{ -1, -1, FERMI_COMPUTE_B },
		{}
	}
};

int
gf110_gr_new(struct nvkm_device *device, int index, struct nvkm_gr **pgr)
{
	return gf100_gr_new_(&gf110_gr, device, index, pgr);
}
