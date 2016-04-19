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

#include <subdev/timer.h>

#include <nvif/class.h>

/*******************************************************************************
 * PGRAPH register lists
 ******************************************************************************/

const struct gf100_gr_init
gk110_gr_init_fe_0[] = {
	{ 0x40415c,   1, 0x04, 0x00000000 },
	{ 0x404170,   1, 0x04, 0x00000000 },
	{ 0x4041b4,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gk110_gr_init_ds_0[] = {
	{ 0x405844,   1, 0x04, 0x00ffffff },
	{ 0x405850,   1, 0x04, 0x00000000 },
	{ 0x405900,   1, 0x04, 0x0000ff00 },
	{ 0x405908,   1, 0x04, 0x00000000 },
	{ 0x405928,   2, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gk110_gr_init_sked_0[] = {
	{ 0x407010,   1, 0x04, 0x00000000 },
	{ 0x407040,   1, 0x04, 0x80440424 },
	{ 0x407048,   1, 0x04, 0x0000000a },
	{}
};

const struct gf100_gr_init
gk110_gr_init_cwd_0[] = {
	{ 0x405b44,   1, 0x04, 0x00000000 },
	{ 0x405b50,   1, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gk110_gr_init_gpc_unk_1[] = {
	{ 0x418d00,   1, 0x04, 0x00000000 },
	{ 0x418d28,   2, 0x04, 0x00000000 },
	{ 0x418f00,   1, 0x04, 0x00000400 },
	{ 0x418f08,   1, 0x04, 0x00000000 },
	{ 0x418f20,   2, 0x04, 0x00000000 },
	{ 0x418e00,   1, 0x04, 0x00000000 },
	{ 0x418e08,   1, 0x04, 0x00000000 },
	{ 0x418e1c,   2, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gk110_gr_init_tex_0[] = {
	{ 0x419ab0,   1, 0x04, 0x00000000 },
	{ 0x419ac8,   1, 0x04, 0x00000000 },
	{ 0x419ab8,   1, 0x04, 0x000000e7 },
	{ 0x419aec,   1, 0x04, 0x00000000 },
	{ 0x419abc,   2, 0x04, 0x00000000 },
	{ 0x419ab4,   1, 0x04, 0x00000000 },
	{ 0x419aa8,   2, 0x04, 0x00000000 },
	{}
};

static const struct gf100_gr_init
gk110_gr_init_l1c_0[] = {
	{ 0x419c98,   1, 0x04, 0x00000000 },
	{ 0x419ca8,   1, 0x04, 0x00000000 },
	{ 0x419cb0,   1, 0x04, 0x01000000 },
	{ 0x419cb4,   1, 0x04, 0x00000000 },
	{ 0x419cb8,   1, 0x04, 0x00b08bea },
	{ 0x419c84,   1, 0x04, 0x00010384 },
	{ 0x419cbc,   1, 0x04, 0x281b3646 },
	{ 0x419cc0,   2, 0x04, 0x00000000 },
	{ 0x419c80,   1, 0x04, 0x00020230 },
	{ 0x419ccc,   2, 0x04, 0x00000000 },
	{}
};

const struct gf100_gr_init
gk110_gr_init_sm_0[] = {
	{ 0x419e00,   1, 0x04, 0x00000080 },
	{ 0x419ea0,   1, 0x04, 0x00000000 },
	{ 0x419ee4,   1, 0x04, 0x00000000 },
	{ 0x419ea4,   1, 0x04, 0x00000100 },
	{ 0x419ea8,   1, 0x04, 0x00000000 },
	{ 0x419eb4,   1, 0x04, 0x00000000 },
	{ 0x419ebc,   2, 0x04, 0x00000000 },
	{ 0x419edc,   1, 0x04, 0x00000000 },
	{ 0x419f00,   1, 0x04, 0x00000000 },
	{ 0x419ed0,   1, 0x04, 0x00003234 },
	{ 0x419f74,   1, 0x04, 0x00015555 },
	{ 0x419f80,   4, 0x04, 0x00000000 },
	{}
};

static const struct gf100_gr_pack
gk110_gr_pack_mmio[] = {
	{ gk104_gr_init_main_0 },
	{ gk110_gr_init_fe_0 },
	{ gf100_gr_init_pri_0 },
	{ gf100_gr_init_rstr2d_0 },
	{ gf119_gr_init_pd_0 },
	{ gk110_gr_init_ds_0 },
	{ gf100_gr_init_scc_0 },
	{ gk110_gr_init_sked_0 },
	{ gk110_gr_init_cwd_0 },
	{ gf119_gr_init_prop_0 },
	{ gf108_gr_init_gpc_unk_0 },
	{ gf100_gr_init_setup_0 },
	{ gf100_gr_init_crstr_0 },
	{ gf108_gr_init_setup_1 },
	{ gf100_gr_init_zcull_0 },
	{ gf119_gr_init_gpm_0 },
	{ gk110_gr_init_gpc_unk_1 },
	{ gf100_gr_init_gcc_0 },
	{ gk104_gr_init_tpccs_0 },
	{ gk110_gr_init_tex_0 },
	{ gk104_gr_init_pe_0 },
	{ gk110_gr_init_l1c_0 },
	{ gf100_gr_init_mpc_0 },
	{ gk110_gr_init_sm_0 },
	{ gf117_gr_init_pes_0 },
	{ gf117_gr_init_wwdx_0 },
	{ gf117_gr_init_cbm_0 },
	{ gk104_gr_init_be_0 },
	{ gf100_gr_init_fe_1 },
	{}
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

#include "fuc/hubgk110.fuc3.h"

struct gf100_gr_ucode
gk110_gr_fecs_ucode = {
	.code.data = gk110_grhub_code,
	.code.size = sizeof(gk110_grhub_code),
	.data.data = gk110_grhub_data,
	.data.size = sizeof(gk110_grhub_data),
};

#include "fuc/gpcgk110.fuc3.h"

struct gf100_gr_ucode
gk110_gr_gpccs_ucode = {
	.code.data = gk110_grgpc_code,
	.code.size = sizeof(gk110_grgpc_code),
	.data.data = gk110_grgpc_data,
	.data.size = sizeof(gk110_grgpc_data),
};

static const struct gf100_gr_func
gk110_gr = {
	.init = gk104_gr_init,
	.init_rop_active_fbps = gk104_gr_init_rop_active_fbps,
	.mmio = gk110_gr_pack_mmio,
	.fecs.ucode = &gk110_gr_fecs_ucode,
	.gpccs.ucode = &gk110_gr_gpccs_ucode,
	.rops = gf100_gr_rops,
	.ppc_nr = 2,
	.grctx = &gk110_grctx,
	.sclass = {
		{ -1, -1, FERMI_TWOD_A },
		{ -1, -1, KEPLER_INLINE_TO_MEMORY_B },
		{ -1, -1, KEPLER_B, &gf100_fermi },
		{ -1, -1, KEPLER_COMPUTE_B },
		{}
	}
};

int
gk110_gr_new(struct nvkm_device *device, int index, struct nvkm_gr **pgr)
{
	return gf100_gr_new_(&gk110_gr, device, index, pgr);
}
