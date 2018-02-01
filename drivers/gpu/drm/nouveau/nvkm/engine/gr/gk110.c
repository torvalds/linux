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
#include "gk104.h"
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

static const struct nvkm_therm_clkgate_init
gk110_clkgate_blcg_init_sked_0[] = {
	{ 0x407000, 1, 0x00004041 },
	{}
};

static const struct nvkm_therm_clkgate_init
gk110_clkgate_blcg_init_gpc_gcc_0[] = {
	{ 0x419020, 1, 0x00000042 },
	{ 0x419038, 1, 0x00000042 },
	{}
};

static const struct nvkm_therm_clkgate_init
gk110_clkgate_blcg_init_gpc_l1c_0[] = {
	{ 0x419cd4, 2, 0x00004042 },
	{}
};

static const struct nvkm_therm_clkgate_init
gk110_clkgate_blcg_init_gpc_mp_0[] = {
	{ 0x419fd0, 1, 0x00004043 },
	{ 0x419fd8, 1, 0x00004049 },
	{ 0x419fe0, 2, 0x00004042 },
	{ 0x419ff0, 1, 0x00000046 },
	{ 0x419ff8, 1, 0x00004042 },
	{ 0x419f90, 1, 0x00004042 },
	{}
};

static const struct nvkm_therm_clkgate_init
gk110_clkgate_slcg_init_main_0[] = {
	{ 0x4041f4, 1, 0x00000000 },
	{ 0x409894, 1, 0x00000000 },
	{}
};

static const struct nvkm_therm_clkgate_init
gk110_clkgate_slcg_init_unk_0[] = {
	{ 0x406004, 1, 0x00000000 },
	{}
};

static const struct nvkm_therm_clkgate_init
gk110_clkgate_slcg_init_sked_0[] = {
	{ 0x407004, 1, 0x00000000 },
	{}
};

static const struct nvkm_therm_clkgate_init
gk110_clkgate_slcg_init_gpc_ctxctl_0[] = {
	{ 0x41a894, 1, 0x00000000 },
	{}
};

static const struct nvkm_therm_clkgate_init
gk110_clkgate_slcg_init_gpc_unk_0[] = {
	{ 0x418504, 1, 0x00000000 },
	{ 0x41860c, 1, 0x00000000 },
	{ 0x41868c, 1, 0x00000000 },
	{}
};

static const struct nvkm_therm_clkgate_init
gk110_clkgate_slcg_init_gpc_esetup_0[] = {
	{ 0x41882c, 1, 0x00000000 },
	{}
};

static const struct nvkm_therm_clkgate_init
gk110_clkgate_slcg_init_gpc_zcull_0[] = {
	{ 0x418974, 1, 0x00000000 },
	{}
};

static const struct nvkm_therm_clkgate_init
gk110_clkgate_slcg_init_gpc_l1c_0[] = {
	{ 0x419cd8, 2, 0x00000000 },
	{}
};

static const struct nvkm_therm_clkgate_init
gk110_clkgate_slcg_init_gpc_unk_1[] = {
	{ 0x419c74, 1, 0x00000000 },
	{}
};

static const struct nvkm_therm_clkgate_init
gk110_clkgate_slcg_init_gpc_mp_0[] = {
	{ 0x419fd4, 1, 0x00004a4a },
	{ 0x419fdc, 1, 0x00000014 },
	{ 0x419fe4, 1, 0x00000000 },
	{ 0x419ff4, 1, 0x00001724 },
	{}
};

static const struct nvkm_therm_clkgate_init
gk110_clkgate_slcg_init_gpc_ppc_0[] = {
	{ 0x41be2c, 1, 0x00000000 },
	{}
};

static const struct nvkm_therm_clkgate_init
gk110_clkgate_slcg_init_pcounter_0[] = {
	{ 0x1be018, 1, 0x000001ff },
	{ 0x1bc018, 1, 0x000001ff },
	{ 0x1b8018, 1, 0x000001ff },
	{ 0x1b4124, 1, 0x00000000 },
	{}
};

static const struct nvkm_therm_clkgate_pack
gk110_clkgate_pack[] = {
	{ gk104_clkgate_blcg_init_main_0 },
	{ gk104_clkgate_blcg_init_rstr2d_0 },
	{ gk104_clkgate_blcg_init_unk_0 },
	{ gk104_clkgate_blcg_init_gcc_0 },
	{ gk110_clkgate_blcg_init_sked_0 },
	{ gk104_clkgate_blcg_init_unk_1 },
	{ gk104_clkgate_blcg_init_gpc_ctxctl_0 },
	{ gk104_clkgate_blcg_init_gpc_unk_0 },
	{ gk104_clkgate_blcg_init_gpc_esetup_0 },
	{ gk104_clkgate_blcg_init_gpc_tpbus_0 },
	{ gk104_clkgate_blcg_init_gpc_zcull_0 },
	{ gk104_clkgate_blcg_init_gpc_tpconf_0 },
	{ gk104_clkgate_blcg_init_gpc_unk_1 },
	{ gk110_clkgate_blcg_init_gpc_gcc_0 },
	{ gk104_clkgate_blcg_init_gpc_ffb_0 },
	{ gk104_clkgate_blcg_init_gpc_tex_0 },
	{ gk104_clkgate_blcg_init_gpc_poly_0 },
	{ gk110_clkgate_blcg_init_gpc_l1c_0 },
	{ gk104_clkgate_blcg_init_gpc_unk_2 },
	{ gk110_clkgate_blcg_init_gpc_mp_0 },
	{ gk104_clkgate_blcg_init_gpc_ppc_0 },
	{ gk104_clkgate_blcg_init_rop_zrop_0 },
	{ gk104_clkgate_blcg_init_rop_0 },
	{ gk104_clkgate_blcg_init_rop_crop_0 },
	{ gk104_clkgate_blcg_init_pxbar_0 },
	{ gk110_clkgate_slcg_init_main_0 },
	{ gk110_clkgate_slcg_init_unk_0 },
	{ gk110_clkgate_slcg_init_sked_0 },
	{ gk110_clkgate_slcg_init_gpc_ctxctl_0 },
	{ gk110_clkgate_slcg_init_gpc_unk_0 },
	{ gk110_clkgate_slcg_init_gpc_esetup_0 },
	{ gk110_clkgate_slcg_init_gpc_zcull_0 },
	{ gk110_clkgate_slcg_init_gpc_l1c_0 },
	{ gk110_clkgate_slcg_init_gpc_unk_1 },
	{ gk110_clkgate_slcg_init_gpc_mp_0 },
	{ gk110_clkgate_slcg_init_gpc_ppc_0 },
	{ gk110_clkgate_slcg_init_pcounter_0 },
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
	.init_gpc_mmu = gf100_gr_init_gpc_mmu,
	.init_rop_active_fbps = gk104_gr_init_rop_active_fbps,
	.init_ppc_exceptions = gk104_gr_init_ppc_exceptions,
	.mmio = gk110_gr_pack_mmio,
	.fecs.ucode = &gk110_gr_fecs_ucode,
	.gpccs.ucode = &gk110_gr_gpccs_ucode,
	.rops = gf100_gr_rops,
	.ppc_nr = 2,
	.grctx = &gk110_grctx,
	.clkgate_pack = gk110_clkgate_pack,
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
