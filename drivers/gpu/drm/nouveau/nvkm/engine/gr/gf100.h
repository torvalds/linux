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
#include <engine/falcon.h>

struct nvkm_acr_lsfw;

#define GPC_MAX 32
#define TPC_MAX_PER_GPC 8
#define TPC_MAX (GPC_MAX * TPC_MAX_PER_GPC)

#define ROP_BCAST(r)      (0x408800 + (r))
#define ROP_UNIT(u, r)    (0x410000 + (u) * 0x400 + (r))
#define GPC_BCAST(r)      (0x418000 + (r))
#define GPC_UNIT(t, r)    (0x500000 + (t) * 0x8000 + (r))
#define PPC_UNIT(t, m, r) (0x503000 + (t) * 0x8000 + (m) * 0x200 + (r))
#define TPC_UNIT(t, m, r) (0x504000 + (t) * 0x8000 + (m) * 0x800 + (r))

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

struct gf100_gr_zbc_stencil {
	u32 format;
	u32 ds;
	u32 l2;
};

struct gf100_gr {
	const struct gf100_gr_func *func;
	struct nvkm_gr base;

	struct {
		struct nvkm_falcon falcon;
		struct nvkm_blob inst;
		struct nvkm_blob data;

		struct mutex mutex;
		u32 disable;
	} fecs;

	struct {
		struct nvkm_falcon falcon;
		struct nvkm_blob inst;
		struct nvkm_blob data;
	} gpccs;

	bool firmware;

	/*
	 * Used if the register packs are loaded from NVIDIA fw instead of
	 * using hardcoded arrays. To be allocated with vzalloc().
	 */
	struct gf100_gr_pack *sw_nonctx;
	struct gf100_gr_pack *sw_nonctx1;
	struct gf100_gr_pack *sw_nonctx2;
	struct gf100_gr_pack *sw_nonctx3;
	struct gf100_gr_pack *sw_nonctx4;
	struct gf100_gr_pack *sw_ctx;
	struct gf100_gr_pack *bundle;
	struct gf100_gr_pack *bundle_veid;
	struct gf100_gr_pack *bundle64;
	struct gf100_gr_pack *method;

	struct gf100_gr_zbc_color zbc_color[NVKM_LTC_MAX_ZBC_COLOR_CNT];
	struct gf100_gr_zbc_depth zbc_depth[NVKM_LTC_MAX_ZBC_DEPTH_CNT];
	struct gf100_gr_zbc_stencil zbc_stencil[NVKM_LTC_MAX_ZBC_DEPTH_CNT];

	u8 rop_nr;
	u8 gpc_nr;
	u8 tpc_nr[GPC_MAX];
	u8 tpc_max;
	u8 tpc_total;
	u8 ppc_nr[GPC_MAX];
	u8 ppc_mask[GPC_MAX];
	u8 ppc_tpc_mask[GPC_MAX][4];
	u8 ppc_tpc_nr[GPC_MAX][4];
	u8 ppc_tpc_min;
	u8 ppc_tpc_max;
	u8 ppc_total;

	struct nvkm_memory *pagepool;
	struct nvkm_memory *bundle_cb;
	struct nvkm_memory *attrib_cb;
	struct nvkm_memory *unknown;

	u8 screen_tile_row_offset;
	u8 tile[TPC_MAX];

	struct {
		u8 gpc;
		u8 tpc;
	} sm[TPC_MAX];
	u8 sm_nr;

	u32  size;
	u32 *data;
	u32 size_zcull;
	u32 size_pm;
};

int gf100_gr_fecs_bind_pointer(struct gf100_gr *, u32 inst);
int gf100_gr_fecs_wfi_golden_save(struct gf100_gr *, u32 inst);

struct gf100_gr_func_zbc {
	void (*clear_color)(struct gf100_gr *, int zbc);
	void (*clear_depth)(struct gf100_gr *, int zbc);
	int (*stencil_get)(struct gf100_gr *, int format,
			   const u32 ds, const u32 l2);
	void (*clear_stencil)(struct gf100_gr *, int zbc);
};

struct gf100_gr_func {
	int (*nonstall)(struct gf100_gr *);
	struct nvkm_intr *(*oneinit_intr)(struct gf100_gr *, enum nvkm_intr_type *);
	void (*oneinit_tiles)(struct gf100_gr *);
	int (*oneinit_sm_id)(struct gf100_gr *);
	int (*init)(struct gf100_gr *);
	void (*init_419bd8)(struct gf100_gr *);
	void (*init_gpc_mmu)(struct gf100_gr *);
	void (*init_r405a14)(struct gf100_gr *);
	void (*init_bios)(struct gf100_gr *);
	void (*init_vsc_stream_master)(struct gf100_gr *);
	void (*init_zcull)(struct gf100_gr *);
	void (*init_num_active_ltcs)(struct gf100_gr *);
	void (*init_rop_active_fbps)(struct gf100_gr *);
	void (*init_bios_2)(struct gf100_gr *);
	void (*init_swdx_pes_mask)(struct gf100_gr *);
	void (*init_fs)(struct gf100_gr *);
	void (*init_fecs_exceptions)(struct gf100_gr *);
	void (*init_40a790)(struct gf100_gr *);
	void (*init_ds_hww_esr_2)(struct gf100_gr *);
	void (*init_40601c)(struct gf100_gr *);
	void (*init_sked_hww_esr)(struct gf100_gr *);
	void (*init_419cc0)(struct gf100_gr *);
	void (*init_419eb4)(struct gf100_gr *);
	void (*init_419c9c)(struct gf100_gr *);
	void (*init_ppc_exceptions)(struct gf100_gr *);
	void (*init_tex_hww_esr)(struct gf100_gr *, int gpc, int tpc);
	void (*init_504430)(struct gf100_gr *, int gpc, int tpc);
	void (*init_shader_exceptions)(struct gf100_gr *, int gpc, int tpc);
	void (*init_rop_exceptions)(struct gf100_gr *);
	void (*init_exception2)(struct gf100_gr *);
	void (*init_400054)(struct gf100_gr *);
	void (*init_4188a4)(struct gf100_gr *);
	void (*trap_mp)(struct gf100_gr *, int gpc, int tpc);
	void (*set_hww_esr_report_mask)(struct gf100_gr *);
	const struct gf100_gr_pack *mmio;
	struct {
		struct gf100_gr_ucode *ucode;
		void (*reset)(struct gf100_gr *);
	} fecs;
	struct {
		struct gf100_gr_ucode *ucode;
		void (*reset)(struct gf100_gr *);
	} gpccs;
	int (*rops)(struct gf100_gr *);
	int gpc_nr;
	int tpc_nr;
	int ppc_nr;
	const struct gf100_grctx_func *grctx;
	const struct nvkm_therm_clkgate_pack *clkgate_pack;
	const struct gf100_gr_func_zbc *zbc;
	struct nvkm_sclass sclass[];
};

int gf100_gr_rops(struct gf100_gr *);
void gf100_gr_oneinit_tiles(struct gf100_gr *);
int gf100_gr_oneinit_sm_id(struct gf100_gr *);
int gf100_gr_init(struct gf100_gr *);
void gf100_gr_init_vsc_stream_master(struct gf100_gr *);
void gf100_gr_init_zcull(struct gf100_gr *);
void gf100_gr_init_num_active_ltcs(struct gf100_gr *);
void gf100_gr_init_fecs_exceptions(struct gf100_gr *);
void gf100_gr_init_40601c(struct gf100_gr *);
void gf100_gr_init_419cc0(struct gf100_gr *);
void gf100_gr_init_419eb4(struct gf100_gr *);
void gf100_gr_init_tex_hww_esr(struct gf100_gr *, int, int);
void gf100_gr_init_shader_exceptions(struct gf100_gr *, int, int);
void gf100_gr_init_rop_exceptions(struct gf100_gr *);
void gf100_gr_init_exception2(struct gf100_gr *);
void gf100_gr_init_400054(struct gf100_gr *);
void gf100_gr_init_num_tpc_per_gpc(struct gf100_gr *, bool, bool);
extern const struct gf100_gr_func_zbc gf100_gr_zbc;
void gf100_gr_fecs_reset(struct gf100_gr *);

void gf117_gr_init_zcull(struct gf100_gr *);

void gk104_gr_init_vsc_stream_master(struct gf100_gr *);
void gk104_gr_init_rop_active_fbps(struct gf100_gr *);
void gk104_gr_init_ppc_exceptions(struct gf100_gr *);
void gk104_gr_init_sked_hww_esr(struct gf100_gr *);

void gk110_gr_init_419eb4(struct gf100_gr *);

void gm107_gr_init_504430(struct gf100_gr *, int, int);
void gm107_gr_init_shader_exceptions(struct gf100_gr *, int, int);
void gm107_gr_init_400054(struct gf100_gr *);

int gk20a_gr_init(struct gf100_gr *);
int gk20a_gr_av_to_init_(struct nvkm_blob *, u8 count, u32 pitch, struct gf100_gr_pack **);
int gk20a_gr_av_to_init(struct nvkm_blob *, struct gf100_gr_pack **);
int gk20a_gr_aiv_to_init(struct nvkm_blob *, struct gf100_gr_pack **);
int gk20a_gr_av_to_method(struct nvkm_blob *, struct gf100_gr_pack **);

void gm200_gr_oneinit_tiles(struct gf100_gr *);
int gm200_gr_oneinit_sm_id(struct gf100_gr *);
int gm200_gr_rops(struct gf100_gr *);
void gm200_gr_init_num_active_ltcs(struct gf100_gr *);
void gm200_gr_init_ds_hww_esr_2(struct gf100_gr *);

void gp100_gr_init_rop_active_fbps(struct gf100_gr *);
void gp100_gr_init_fecs_exceptions(struct gf100_gr *);
void gp100_gr_init_shader_exceptions(struct gf100_gr *, int, int);
void gp100_gr_zbc_clear_color(struct gf100_gr *, int);
void gp100_gr_zbc_clear_depth(struct gf100_gr *, int);
extern const struct gf100_gr_func_zbc gp100_gr_zbc;

void gp102_gr_init_swdx_pes_mask(struct gf100_gr *);
extern const struct gf100_gr_func_zbc gp102_gr_zbc;
int gp102_gr_zbc_stencil_get(struct gf100_gr *, int, const u32, const u32);
void gp102_gr_zbc_clear_stencil(struct gf100_gr *, int);

extern const struct gf100_gr_func gp107_gr;

int gv100_gr_oneinit_sm_id(struct gf100_gr *);
u32 gv100_gr_nonpes_aware_tpc(struct gf100_gr *gr, u32 gpc, u32 tpc);
void gv100_gr_init_419bd8(struct gf100_gr *);
void gv100_gr_init_504430(struct gf100_gr *, int, int);
void gv100_gr_init_shader_exceptions(struct gf100_gr *, int, int);
void gv100_gr_init_4188a4(struct gf100_gr *);
void gv100_gr_trap_mp(struct gf100_gr *, int, int);

int tu102_gr_av_to_init_veid(struct nvkm_blob *, struct gf100_gr_pack **);
void tu102_gr_init_zcull(struct gf100_gr *);
void tu102_gr_init_fs(struct gf100_gr *);
void tu102_gr_init_fecs_exceptions(struct gf100_gr *);

#define gf100_gr_chan(p) container_of((p), struct gf100_gr_chan, object)
#include <core/object.h>

struct gf100_gr_chan {
	struct nvkm_object object;
	struct gf100_gr *gr;
	struct nvkm_vmm *vmm;

	struct nvkm_vma *pagepool;
	struct nvkm_vma *bundle_cb;
	struct nvkm_vma *attrib_cb;
	struct nvkm_vma *unknown;

	struct nvkm_memory *mmio;
	struct nvkm_vma *mmio_vma;
	int mmio_nr;
};

void gf100_gr_ctxctl_debug(struct gf100_gr *);

u64  gf100_gr_units(struct nvkm_gr *);
void gf100_gr_zbc_init(struct gf100_gr *);

extern const struct nvkm_object_func gf100_fermi;

struct gf100_gr_init {
	u32 addr;
	u8  count;
	u32 pitch;
	u64 data;
};

struct gf100_gr_pack {
	const struct gf100_gr_init *init;
	u32 type;
};

#define pack_for_each_init(init, pack, head)                                   \
	for (pack = head; pack && pack->init; pack++)                          \
		  for (init = pack->init; init && init->count; init++)

struct gf100_gr_ucode {
	struct nvkm_blob code;
	struct nvkm_blob data;
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
void gf100_gr_init_gpc_mmu(struct gf100_gr *);
void gf100_gr_trap_mp(struct gf100_gr *, int, int);
extern const struct nvkm_bitfield gf100_mp_global_error[];
extern const struct nvkm_enum gf100_mp_warp_error[];

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
extern const struct gf100_gr_init gk104_gr_init_gpc_unk_2[];
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

void gm200_gr_init_gpc_mmu(struct gf100_gr *);

struct gf100_gr_fwif {
	int version;
	int (*load)(struct gf100_gr *, int ver, const struct gf100_gr_fwif *);
	const struct gf100_gr_func *func;
	const struct nvkm_acr_lsf_func *fecs;
	const struct nvkm_acr_lsf_func *gpccs;
};

int gf100_gr_load(struct gf100_gr *, int, const struct gf100_gr_fwif *);
int gf100_gr_nofw(struct gf100_gr *, int, const struct gf100_gr_fwif *);

int gk20a_gr_load_sw(struct gf100_gr *, const char *path, int ver);
int gk20a_gr_load_net(struct gf100_gr *, const char *, const char *, int,
		      int (*)(struct nvkm_blob *, struct gf100_gr_pack **),
		      struct gf100_gr_pack **);

int gm200_gr_nofw(struct gf100_gr *, int, const struct gf100_gr_fwif *);
int gm200_gr_load(struct gf100_gr *, int, const struct gf100_gr_fwif *);
extern const struct nvkm_acr_lsf_func gm200_gr_gpccs_acr;
extern const struct nvkm_acr_lsf_func gm200_gr_fecs_acr;

extern const struct nvkm_acr_lsf_func gm20b_gr_fecs_acr;
void gm20b_gr_acr_bld_write(struct nvkm_acr *, u32, struct nvkm_acr_lsfw *);
void gm20b_gr_acr_bld_patch(struct nvkm_acr *, u32, s64);

extern const struct nvkm_acr_lsf_func gp108_gr_gpccs_acr;
extern const struct nvkm_acr_lsf_func gp108_gr_fecs_acr;
void gp108_gr_acr_bld_write(struct nvkm_acr *, u32, struct nvkm_acr_lsfw *);
void gp108_gr_acr_bld_patch(struct nvkm_acr *, u32, s64);

int gf100_gr_new_(const struct gf100_gr_fwif *, struct nvkm_device *, enum nvkm_subdev_type, int,
		  struct nvkm_gr **);
int r535_gr_new(const struct gf100_gr_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		struct nvkm_gr **);
#endif
