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
#ifndef __GF100_GR_H__
#define __GF100_GR_H__
#define gf100_gr(p) container_of((p), struct gf100_gr, base)
#include "priv.h"

#include <core/gpuobj.h>
#include <subdev/ltc.h>
#include <subdev/mmu.h>

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

struct gf100_gr {
	const struct gf100_gr_func *func;
	struct nvkm_gr base;

	struct gf100_gr_fuc fuc409c;
	struct gf100_gr_fuc fuc409d;
	struct gf100_gr_fuc fuc41ac;
	struct gf100_gr_fuc fuc41ad;
	bool firmware;

	/*
	 * Used if the register packs are loaded from NVIDIA fw instead of
	 * using hardcoded arrays. To be allocated with vzalloc().
	 */
	struct gf100_gr_pack *fuc_sw_nonctx;
	struct gf100_gr_pack *fuc_sw_ctx;
	struct gf100_gr_pack *fuc_bundle;
	struct gf100_gr_pack *fuc_method;

	struct gf100_gr_zbc_color zbc_color[NVKM_LTC_MAX_ZBC_CNT];
	struct gf100_gr_zbc_depth zbc_depth[NVKM_LTC_MAX_ZBC_CNT];

	u8 rop_nr;
	u8 gpc_nr;
	u8 tpc_nr[GPC_MAX];
	u8 tpc_total;
	u8 ppc_nr[GPC_MAX];
	u8 ppc_mask[GPC_MAX];
	u8 ppc_tpc_nr[GPC_MAX][4];

	struct nvkm_memory *unk4188b4;
	struct nvkm_memory *unk4188b8;

	struct gf100_gr_data mmio_data[4];
	struct gf100_gr_mmio mmio_list[4096/8];
	u32  size;
	u32 *data;

	u8 magic_not_rop_nr;
};

int gf100_gr_ctor(const struct gf100_gr_func *, struct nvkm_device *,
		  int, struct gf100_gr *);
int gf100_gr_new_(const struct gf100_gr_func *, struct nvkm_device *,
		  int, struct nvkm_gr **);
void *gf100_gr_dtor(struct nvkm_gr *);

struct gf100_gr_func {
	void (*dtor)(struct gf100_gr *);
	int (*init)(struct gf100_gr *);
	void (*init_gpc_mmu)(struct gf100_gr *);
	void (*set_hww_esr_report_mask)(struct gf100_gr *);
	const struct gf100_gr_pack *mmio;
	struct {
		struct gf100_gr_ucode *ucode;
	} fecs;
	struct {
		struct gf100_gr_ucode *ucode;
	} gpccs;
	int ppc_nr;
	const struct gf100_grctx_func *grctx;
	struct nvkm_sclass sclass[];
};

int gf100_gr_init(struct gf100_gr *);

int gk104_gr_init(struct gf100_gr *);

int gk20a_gr_new_(const struct gf100_gr_func *, struct nvkm_device *,
		  int, struct nvkm_gr **);
int gk20a_gr_init(struct gf100_gr *);

int gm200_gr_init(struct gf100_gr *);

#define gf100_gr_chan(p) container_of((p), struct gf100_gr_chan, object)

struct gf100_gr_chan {
	struct nvkm_object object;
	struct gf100_gr *gr;

	struct nvkm_memory *mmio;
	struct nvkm_vma mmio_vma;
	int mmio_nr;

	struct {
		struct nvkm_memory *mem;
		struct nvkm_vma vma;
	} data[4];
};

void gf100_gr_ctxctl_debug(struct gf100_gr *);

void gf100_gr_dtor_fw(struct gf100_gr_fuc *);
int  gf100_gr_ctor_fw(struct gf100_gr *, const char *,
		      struct gf100_gr_fuc *);
u64  gf100_gr_units(struct nvkm_gr *);
void gf100_gr_zbc_init(struct gf100_gr *);

extern const struct nvkm_object_func gf100_fermi;

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

int  gf100_gr_wait_idle(struct gf100_gr *);
void gf100_gr_mmio(struct gf100_gr *, const struct gf100_gr_pack *);
void gf100_gr_icmd(struct gf100_gr *, const struct gf100_gr_pack *);
void gf100_gr_mthd(struct gf100_gr *, const struct gf100_gr_pack *);
int  gf100_gr_init_ctxctl(struct gf100_gr *);

/* external bundles loading functions */
int gk20a_gr_av_to_init(struct gf100_gr *, const char *,
			struct gf100_gr_pack **);
int gk20a_gr_aiv_to_init(struct gf100_gr *, const char *,
			 struct gf100_gr_pack **);
int gk20a_gr_av_to_method(struct gf100_gr *, const char *,
			  struct gf100_gr_pack **);

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
void gm107_gr_init_bios(struct gf100_gr *);

extern const struct gf100_gr_pack gm200_gr_pack_mmio[];
#endif
