/*
 * Copyright 2010 Red Hat Inc.
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
 * Authors: Ben Skeggs
 */
#ifndef __NVC0_GR_H__
#define __NVC0_GR_H__
#include <engine/gr.h>

#include <subdev/ltc.h>

#define GPC_MAX 32
#define TPC_MAX (GPC_MAX * 8)

#define ROP_BCAST(r)      (0x408800 + (r))
#define ROP_UNIT(u, r)    (0x410000 + (u) * 0x400 + (r))
#define GPC_BCAST(r)      (0x418000 + (r))
#define GPC_UNIT(t, r)    (0x500000 + (t) * 0x8000 + (r))
#define PPC_UNIT(t, m, r) (0x503000 + (t) * 0x8000 + (m) * 0x200 + (r))
#define TPC_UNIT(t, m, r) (0x504000 + (t) * 0x8000 + (m) * 0x800 + (r))

struct gf100_gr_data {
	u32 size;
	u32 align;
	u32 access;
};

struct gf100_gr_mmio {
	u32 addr;
	u32 data;
	u32 shift;
	int buffer;
};

struct gf100_gr_fuc {
	u32 *data;
	u32  size;
};

struct gf100_gr_zbc_color {
	u32 format;
	u32 ds[4];
	u32 l2[4];
};

struct gf100_gr_zbc_depth {
	u32 format;
	u32 ds;
	u32 l2;
};

struct gf100_gr_priv {
	struct nvkm_gr base;

	struct gf100_gr_fuc fuc409c;
	struct gf100_gr_fuc fuc409d;
	struct gf100_gr_fuc fuc41ac;
	struct gf100_gr_fuc fuc41ad;
	bool firmware;

	struct gf100_gr_zbc_color zbc_color[NVKM_LTC_MAX_ZBC_CNT];
	struct gf100_gr_zbc_depth zbc_depth[NVKM_LTC_MAX_ZBC_CNT];

	u8 rop_nr;
	u8 gpc_nr;
	u8 tpc_nr[GPC_MAX];
	u8 tpc_total;
	u8 ppc_nr[GPC_MAX];
	u8 ppc_tpc_nr[GPC_MAX][4];

	struct nvkm_gpuobj *unk4188b4;
	struct nvkm_gpuobj *unk4188b8;

	struct gf100_gr_data mmio_data[4];
	struct gf100_gr_mmio mmio_list[4096/8];
	u32  size;
	u32 *data;

	u8 magic_not_rop_nr;
};

struct gf100_gr_chan {
	struct nvkm_gr_chan base;

	struct nvkm_gpuobj *mmio;
	struct nvkm_vma mmio_vma;
	int mmio_nr;
	struct {
		struct nvkm_gpuobj *mem;
		struct nvkm_vma vma;
	} data[4];
};

int  gf100_gr_context_ctor(struct nvkm_object *, struct nvkm_object *,
			     struct nvkm_oclass *, void *, u32,
			     struct nvkm_object **);
void gf100_gr_context_dtor(struct nvkm_object *);

void gf100_gr_ctxctl_debug(struct gf100_gr_priv *);

u64  gf100_gr_units(struct nvkm_gr *);
int  gf100_gr_ctor(struct nvkm_object *, struct nvkm_object *,
		     struct nvkm_oclass *, void *data, u32 size,
		     struct nvkm_object **);
void gf100_gr_dtor(struct nvkm_object *);
int  gf100_gr_init(struct nvkm_object *);
void gf100_gr_zbc_init(struct gf100_gr_priv *);

int  gk104_gr_ctor(struct nvkm_object *, struct nvkm_object *,
		     struct nvkm_oclass *, void *data, u32 size,
		     struct nvkm_object **);
int  gk104_gr_init(struct nvkm_object *);

int  gm204_gr_init(struct nvkm_object *);

extern struct nvkm_ofuncs gf100_fermi_ofuncs;

extern struct nvkm_oclass gf100_gr_sclass[];
extern struct nvkm_omthds gf100_gr_9097_omthds[];
extern struct nvkm_omthds gf100_gr_90c0_omthds[];
extern struct nvkm_oclass gf110_gr_sclass[];
extern struct nvkm_oclass gk110_gr_sclass[];
extern struct nvkm_oclass gm204_gr_sclass[];

struct gf100_gr_init {
	u32 addr;
	u8  count;
	u8  pitch;
	u32 data;
};

struct gf100_gr_pack {
	const struct gf100_gr_init *init;
	u32 type;
};

#define pack_for_each_init(init, pack, head)                                   \
	for (pack = head; pack && pack->init; pack++)                          \
		  for (init = pack->init; init && init->count; init++)

struct gf100_gr_ucode {
	struct gf100_gr_fuc code;
	struct gf100_gr_fuc data;
};

extern struct gf100_gr_ucode gf100_gr_fecs_ucode;
extern struct gf100_gr_ucode gf100_gr_gpccs_ucode;

extern struct gf100_gr_ucode gk110_gr_fecs_ucode;
extern struct gf100_gr_ucode gk110_gr_gpccs_ucode;

struct gf100_gr_oclass {
	struct nvkm_oclass base;
	struct nvkm_oclass **cclass;
	struct nvkm_oclass *sclass;
	const struct gf100_gr_pack *mmio;
	struct {
		struct gf100_gr_ucode *ucode;
	} fecs;
	struct {
		struct gf100_gr_ucode *ucode;
	} gpccs;
	int ppc_nr;
};

void gf100_gr_mmio(struct gf100_gr_priv *, const struct gf100_gr_pack *);
void gf100_gr_icmd(struct gf100_gr_priv *, const struct gf100_gr_pack *);
void gf100_gr_mthd(struct gf100_gr_priv *, const struct gf100_gr_pack *);
int  gf100_gr_init_ctxctl(struct gf100_gr_priv *);

/* register init value lists */

extern const struct gf100_gr_init gf100_gr_init_main_0[];
extern const struct gf100_gr_init gf100_gr_init_fe_0[];
extern const struct gf100_gr_init gf100_gr_init_pri_0[];
extern const struct gf100_gr_init gf100_gr_init_rstr2d_0[];
extern const struct gf100_gr_init gf100_gr_init_pd_0[];
extern const struct gf100_gr_init gf100_gr_init_ds_0[];
extern const struct gf100_gr_init gf100_gr_init_scc_0[];
extern const struct gf100_gr_init gf100_gr_init_prop_0[];
extern const struct gf100_gr_init gf100_gr_init_gpc_unk_0[];
extern const struct gf100_gr_init gf100_gr_init_setup_0[];
extern const struct gf100_gr_init gf100_gr_init_crstr_0[];
extern const struct gf100_gr_init gf100_gr_init_setup_1[];
extern const struct gf100_gr_init gf100_gr_init_zcull_0[];
extern const struct gf100_gr_init gf100_gr_init_gpm_0[];
extern const struct gf100_gr_init gf100_gr_init_gpc_unk_1[];
extern const struct gf100_gr_init gf100_gr_init_gcc_0[];
extern const struct gf100_gr_init gf100_gr_init_tpccs_0[];
extern const struct gf100_gr_init gf100_gr_init_tex_0[];
extern const struct gf100_gr_init gf100_gr_init_pe_0[];
extern const struct gf100_gr_init gf100_gr_init_l1c_0[];
extern const struct gf100_gr_init gf100_gr_init_wwdx_0[];
extern const struct gf100_gr_init gf100_gr_init_tpccs_1[];
extern const struct gf100_gr_init gf100_gr_init_mpc_0[];
extern const struct gf100_gr_init gf100_gr_init_be_0[];
extern const struct gf100_gr_init gf100_gr_init_fe_1[];
extern const struct gf100_gr_init gf100_gr_init_pe_1[];

extern const struct gf100_gr_init gf104_gr_init_ds_0[];
extern const struct gf100_gr_init gf104_gr_init_tex_0[];
extern const struct gf100_gr_init gf104_gr_init_sm_0[];

extern const struct gf100_gr_init gf108_gr_init_gpc_unk_0[];
extern const struct gf100_gr_init gf108_gr_init_setup_1[];

extern const struct gf100_gr_init gf119_gr_init_pd_0[];
extern const struct gf100_gr_init gf119_gr_init_ds_0[];
extern const struct gf100_gr_init gf119_gr_init_prop_0[];
extern const struct gf100_gr_init gf119_gr_init_gpm_0[];
extern const struct gf100_gr_init gf119_gr_init_gpc_unk_1[];
extern const struct gf100_gr_init gf119_gr_init_tex_0[];
extern const struct gf100_gr_init gf119_gr_init_sm_0[];
extern const struct gf100_gr_init gf119_gr_init_fe_1[];

extern const struct gf100_gr_init gf117_gr_init_pes_0[];
extern const struct gf100_gr_init gf117_gr_init_wwdx_0[];
extern const struct gf100_gr_init gf117_gr_init_cbm_0[];

extern const struct gf100_gr_init gk104_gr_init_main_0[];
extern const struct gf100_gr_init gk104_gr_init_tpccs_0[];
extern const struct gf100_gr_init gk104_gr_init_pe_0[];
extern const struct gf100_gr_init gk104_gr_init_be_0[];
extern const struct gf100_gr_pack gk104_gr_pack_mmio[];

extern const struct gf100_gr_init gk110_gr_init_fe_0[];
extern const struct gf100_gr_init gk110_gr_init_ds_0[];
extern const struct gf100_gr_init gk110_gr_init_sked_0[];
extern const struct gf100_gr_init gk110_gr_init_cwd_0[];
extern const struct gf100_gr_init gk110_gr_init_gpc_unk_1[];
extern const struct gf100_gr_init gk110_gr_init_tex_0[];
extern const struct gf100_gr_init gk110_gr_init_sm_0[];

extern const struct gf100_gr_init gk208_gr_init_gpc_unk_0[];

extern const struct gf100_gr_init gm107_gr_init_scc_0[];
extern const struct gf100_gr_init gm107_gr_init_prop_0[];
extern const struct gf100_gr_init gm107_gr_init_setup_1[];
extern const struct gf100_gr_init gm107_gr_init_zcull_0[];
extern const struct gf100_gr_init gm107_gr_init_gpc_unk_1[];
extern const struct gf100_gr_init gm107_gr_init_tex_0[];
extern const struct gf100_gr_init gm107_gr_init_l1c_0[];
extern const struct gf100_gr_init gm107_gr_init_wwdx_0[];
extern const struct gf100_gr_init gm107_gr_init_cbm_0[];
void gm107_gr_init_bios(struct gf100_gr_priv *);

extern const struct gf100_gr_pack gm204_gr_pack_mmio[];
#endif
