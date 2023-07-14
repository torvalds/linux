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

const struct gf100_gr_init
gf119_gr_init_pd_0[] = {
	{ 0x406024,   1, 0x04, 0x00000000 },
	{ 0x4064f0,   3, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf119_gr_init_ds_0[] = {
	{ 0x405844,   1, 0x04, 0x00ffffff },
	{ 0x405850,   1, 0x04, 0x00000000 },
	{ 0x405900,   1, 0x04, 0x00002834 },
	{ 0x405908,   1, 0x04, 0x00000000 },
	{ 0x405928,   2, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf119_gr_init_prop_0[] = {
	{ 0x418408,   1, 0x04, 0x00000000 },
	{ 0x4184a0,   3, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf119_gr_init_gpm_0[] = {
	{ 0x418c04,   1, 0x04, 0x00000000 },
	{ 0x418c64,   2, 0x04, 0x00000000 },
	{ 0x418c88,   1, 0x04, 0x00000000 },
	{ 0x418cb4,   2, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf119_gr_init_gpc_unk_1[] = {
	{ 0x418d00,   1, 0x04, 0x00000000 },
	{ 0x418d28,   2, 0x04, 0x00000000 },
	{ 0x418f00,   1, 0x04, 0x00000000 },
	{ 0x418f08,   1, 0x04, 0x00000000 },
	{ 0x418f20,   2, 0x04, 0x00000000 },
	{ 0x418e00,   1, 0x04, 0x00000003 },
	{ 0x418e08,   1, 0x04, 0x00000000 },
	{ 0x418e1c,   2, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf119_gr_init_tex_0[] = {
	{ 0x419ab0,   1, 0x04, 0x00000000 },
	{ 0x419ac8,   1, 0x04, 0x00000000 },
	{ 0x419ab8,   1, 0x04, 0x000000e7 },
	{ 0x419abc,   2, 0x04, 0x00000000 },
	{ 0x419ab4,   1, 0x04, 0x00000000 },
	{}
};

static const struct gf100_gr_init
gf119_gr_init_pe_0[] = {
	{ 0x41980c,   1, 0x04, 0x00000010 },
	{ 0x419810,   1, 0x04, 0x00000000 },
	{ 0x419814,   1, 0x04, 0x00000004 },
	{ 0x419844,   1, 0x04, 0x00000000 },
	{ 0x41984c,   1, 0x04, 0x0000a918 },
	{ 0x419850,   4, 0x04, 0x00000000 },
	{ 0x419880,   1, 0x04, 0x00000002 },
	{}
};

static const struct gf100_gr_init
gf119_gr_init_wwdx_0[] = {
	{ 0x419bd4,   1, 0x04, 0x00800000 },
	{ 0x419bdc,   1, 0x04, 0x00000000 },
	{ 0x419bf8,   2, 0x04, 0x00000000 },
	{}
};

static const struct gf100_gr_init
gf119_gr_init_tpccs_1[] = {
	{ 0x419d2c,   1, 0x04, 0x00000000 },
	{ 0x419d48,   2, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf119_gr_init_sm_0[] = {
	{ 0x419e00,   1, 0x04, 0x00000000 },
	{ 0x419ea0,   1, 0x04, 0x00000000 },
	{ 0x419ea4,   1, 0x04, 0x00000100 },
	{ 0x419ea8,   1, 0x04, 0x02001100 },
	{ 0x419eac,   1, 0x04, 0x11100702 },
	{ 0x419eb0,   1, 0x04, 0x00000003 },
	{ 0x419eb4,   4, 0x04, 0x00000000 },
	{ 0x419ec8,   1, 0x04, 0x0e063818 },
	{ 0x419ecc,   1, 0x04, 0x0e060e06 },
	{ 0x419ed0,   1, 0x04, 0x00003818 },
	{ 0x419ed4,   1, 0x04, 0x011104f1 },
	{ 0x419edc,   1, 0x04, 0x00000000 },
	{ 0x419f00,   1, 0x04, 0x00000000 },
	{ 0x419f2c,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gf119_gr_init_fe_1[] = {
	{ 0x40402c,   1, 0x04, 0x00000000 },
	{ 0x4040f0,   1, 0x04, 0x00000000 },
	{ 0x404174,   1, 0x04, 0x00000000 },
	{}
};

static const struct gf100_gr_pack
gf119_gr_pack_mmio[] = {
	{ gf100_gr_init_main_0 },
	{ gf100_gr_init_fe_0 },
	{ gf100_gr_init_pri_0 },
	{ gf100_gr_init_rstr2d_0 },
	{ gf119_gr_init_pd_0 },
	{ gf119_gr_init_ds_0 },
	{ gf100_gr_init_scc_0 },
	{ gf119_gr_init_prop_0 },
	{ gf108_gr_init_gpc_unk_0 },
	{ gf100_gr_init_setup_0 },
	{ gf100_gr_init_crstr_0 },
	{ gf108_gr_init_setup_1 },
	{ gf100_gr_init_zcull_0 },
	{ gf119_gr_init_gpm_0 },
	{ gf119_gr_init_gpc_unk_1 },
	{ gf100_gr_init_gcc_0 },
	{ gf100_gr_init_tpccs_0 },
	{ gf119_gr_init_tex_0 },
	{ gf119_gr_init_pe_0 },
	{ gf100_gr_init_l1c_0 },
	{ gf119_gr_init_wwdx_0 },
	{ gf119_gr_init_tpccs_1 },
	{ gf100_gr_init_mpc_0 },
	{ gf119_gr_init_sm_0 },
	{ gf100_gr_init_be_0 },
	{ gf119_gr_init_fe_1 },
	{}
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

static const struct gf100_gr_func
gf119_gr = {
	.oneinit_tiles = gf100_gr_oneinit_tiles,
	.oneinit_sm_id = gf100_gr_oneinit_sm_id,
	.init = gf100_gr_init,
	.init_gpc_mmu = gf100_gr_init_gpc_mmu,
	.init_vsc_stream_master = gf100_gr_init_vsc_stream_master,
	.init_zcull = gf100_gr_init_zcull,
	.init_num_active_ltcs = gf100_gr_init_num_active_ltcs,
	.init_fecs_exceptions = gf100_gr_init_fecs_exceptions,
	.init_40601c = gf100_gr_init_40601c,
	.init_419cc0 = gf100_gr_init_419cc0,
	.init_419eb4 = gf100_gr_init_419eb4,
	.init_tex_hww_esr = gf100_gr_init_tex_hww_esr,
	.init_shader_exceptions = gf100_gr_init_shader_exceptions,
	.init_rop_exceptions = gf100_gr_init_rop_exceptions,
	.init_exception2 = gf100_gr_init_exception2,
	.init_400054 = gf100_gr_init_400054,
	.trap_mp = gf100_gr_trap_mp,
	.mmio = gf119_gr_pack_mmio,
	.fecs.ucode = &gf100_gr_fecs_ucode,
	.fecs.reset = gf100_gr_fecs_reset,
	.gpccs.ucode = &gf100_gr_gpccs_ucode,
	.rops = gf100_gr_rops,
	.grctx = &gf119_grctx,
	.zbc = &gf100_gr_zbc,
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

static const struct gf100_gr_fwif
gf119_gr_fwif[] = {
	{ -1, gf100_gr_load, &gf119_gr },
	{ -1, gf100_gr_nofw, &gf119_gr },
	{}
};

int
gf119_gr_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_gr **pgr)
{
	return gf100_gr_new_(gf119_gr_fwif, device, type, inst, pgr);
}
