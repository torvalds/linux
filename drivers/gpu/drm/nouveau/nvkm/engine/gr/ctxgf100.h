/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_GRCTX_NVC0_H__
#define __NVKM_GRCTX_NVC0_H__
#include "gf100.h"

struct gf100_grctx {
	struct gf100_gr *gr;
	struct gf100_gr_data *data;
	struct gf100_gr_mmio *mmio;
	int buffer_nr;
	u64 buffer[4];
	u64 addr;
};

int  gf100_grctx_mmio_data(struct gf100_grctx *, u32 size, u32 align, bool priv);
void gf100_grctx_mmio_item(struct gf100_grctx *, u32 addr, u32 data, int s, int);

#define mmio_vram(a,b,c,d) gf100_grctx_mmio_data((a), (b), (c), (d))
#define mmio_refn(a,b,c,d,e) gf100_grctx_mmio_item((a), (b), (c), (d), (e))
#define mmio_skip(a,b,c) mmio_refn((a), (b), (c), -1, -1)
#define mmio_wr32(a,b,c) mmio_refn((a), (b), (c),  0, -1)

struct gf100_grctx_func {
	void (*unkn88c)(struct gf100_gr *, bool on);
	/* main context generation function */
	void  (*main)(struct gf100_gr *, struct gf100_grctx *);
	/* context-specific modify-on-first-load list generation function */
	void  (*unkn)(struct gf100_gr *);
	/* mmio context data */
	const struct gf100_gr_pack *hub;
	const struct gf100_gr_pack *gpc_0;
	const struct gf100_gr_pack *gpc_1;
	const struct gf100_gr_pack *zcull;
	const struct gf100_gr_pack *tpc;
	const struct gf100_gr_pack *ppc;
	/* indirect context data, generated with icmds/mthds */
	const struct gf100_gr_pack *icmd;
	const struct gf100_gr_pack *mthd;
	const struct gf100_gr_pack *sw_veid_bundle_init;
	/* bundle circular buffer */
	void (*bundle)(struct gf100_grctx *);
	u32 bundle_size;
	u32 bundle_min_gpm_fifo_depth;
	u32 bundle_token_limit;
	/* pagepool */
	void (*pagepool)(struct gf100_grctx *);
	u32 pagepool_size;
	/* attribute(/alpha) circular buffer */
	void (*attrib)(struct gf100_grctx *);
	u32 attrib_nr_max;
	u32 attrib_nr;
	u32 alpha_nr_max;
	u32 alpha_nr;
	u32 gfxp_nr;
	/* other patch buffer stuff */
	void (*patch_ltc)(struct gf100_grctx *);
	/* floorsweeping */
	void (*sm_id)(struct gf100_gr *, int gpc, int tpc, int sm);
	void (*tpc_nr)(struct gf100_gr *, int gpc);
	bool skip_pd_num_tpc_per_gpc;
	void (*r4060a8)(struct gf100_gr *);
	void (*rop_mapping)(struct gf100_gr *);
	void (*alpha_beta_tables)(struct gf100_gr *);
	void (*max_ways_evict)(struct gf100_gr *);
	void (*dist_skip_table)(struct gf100_gr *);
	void (*r406500)(struct gf100_gr *);
	void (*gpc_tpc_nr)(struct gf100_gr *);
	void (*r419f78)(struct gf100_gr *);
	void (*tpc_mask)(struct gf100_gr *);
	void (*smid_config)(struct gf100_gr *);
	/* misc other things */
	void (*r400088)(struct gf100_gr *, bool);
	void (*r419cb8)(struct gf100_gr *);
	void (*r418800)(struct gf100_gr *);
	void (*r419eb0)(struct gf100_gr *);
	void (*r419e00)(struct gf100_gr *);
	void (*r418e94)(struct gf100_gr *);
	void (*r419a3c)(struct gf100_gr *);
	void (*r408840)(struct gf100_gr *);
	void (*r419c0c)(struct gf100_gr *);
};

extern const struct gf100_grctx_func gf100_grctx;
int  gf100_grctx_generate(struct gf100_gr *);
void gf100_grctx_generate_main(struct gf100_gr *, struct gf100_grctx *);
void gf100_grctx_generate_bundle(struct gf100_grctx *);
void gf100_grctx_generate_pagepool(struct gf100_grctx *);
void gf100_grctx_generate_attrib(struct gf100_grctx *);
void gf100_grctx_generate_unkn(struct gf100_gr *);
void gf100_grctx_generate_floorsweep(struct gf100_gr *);
void gf100_grctx_generate_sm_id(struct gf100_gr *, int, int, int);
void gf100_grctx_generate_tpc_nr(struct gf100_gr *, int);
void gf100_grctx_generate_r4060a8(struct gf100_gr *);
void gf100_grctx_generate_rop_mapping(struct gf100_gr *);
void gf100_grctx_generate_alpha_beta_tables(struct gf100_gr *);
void gf100_grctx_generate_max_ways_evict(struct gf100_gr *);
void gf100_grctx_generate_r419cb8(struct gf100_gr *);

extern const struct gf100_grctx_func gf108_grctx;
void gf108_grctx_generate_attrib(struct gf100_grctx *);
void gf108_grctx_generate_unkn(struct gf100_gr *);

extern const struct gf100_grctx_func gf104_grctx;
extern const struct gf100_grctx_func gf110_grctx;

extern const struct gf100_grctx_func gf117_grctx;
void gf117_grctx_generate_attrib(struct gf100_grctx *);
void gf117_grctx_generate_rop_mapping(struct gf100_gr *);
void gf117_grctx_generate_dist_skip_table(struct gf100_gr *);

extern const struct gf100_grctx_func gf119_grctx;

extern const struct gf100_grctx_func gk104_grctx;
void gk104_grctx_generate_alpha_beta_tables(struct gf100_gr *);
void gk104_grctx_generate_gpc_tpc_nr(struct gf100_gr *);

extern const struct gf100_grctx_func gk20a_grctx;
void gk104_grctx_generate_bundle(struct gf100_grctx *);
void gk104_grctx_generate_pagepool(struct gf100_grctx *);
void gk104_grctx_generate_patch_ltc(struct gf100_grctx *);
void gk104_grctx_generate_unkn(struct gf100_gr *);
void gk104_grctx_generate_r418800(struct gf100_gr *);

extern const struct gf100_grctx_func gk110_grctx;
void gk110_grctx_generate_r419eb0(struct gf100_gr *);
void gk110_grctx_generate_r419f78(struct gf100_gr *);

extern const struct gf100_grctx_func gk110b_grctx;
extern const struct gf100_grctx_func gk208_grctx;

extern const struct gf100_grctx_func gm107_grctx;
void gm107_grctx_generate_bundle(struct gf100_grctx *);
void gm107_grctx_generate_pagepool(struct gf100_grctx *);
void gm107_grctx_generate_attrib(struct gf100_grctx *);
void gm107_grctx_generate_sm_id(struct gf100_gr *, int, int, int);

extern const struct gf100_grctx_func gm200_grctx;
void gm200_grctx_generate_dist_skip_table(struct gf100_gr *);
void gm200_grctx_generate_r406500(struct gf100_gr *);
void gm200_grctx_generate_tpc_mask(struct gf100_gr *);
void gm200_grctx_generate_smid_config(struct gf100_gr *);
void gm200_grctx_generate_r419a3c(struct gf100_gr *);

extern const struct gf100_grctx_func gm20b_grctx;

extern const struct gf100_grctx_func gp100_grctx;
void gp100_grctx_generate_pagepool(struct gf100_grctx *);
void gp100_grctx_generate_smid_config(struct gf100_gr *);

extern const struct gf100_grctx_func gp102_grctx;
void gp102_grctx_generate_attrib(struct gf100_grctx *);

extern const struct gf100_grctx_func gp104_grctx;

extern const struct gf100_grctx_func gp107_grctx;

extern const struct gf100_grctx_func gv100_grctx;

extern const struct gf100_grctx_func tu102_grctx;
void gv100_grctx_unkn88c(struct gf100_gr *, bool);
void gv100_grctx_generate_unkn(struct gf100_gr *);
extern const struct gf100_gr_init gv100_grctx_init_sw_veid_bundle_init_0[];
void gv100_grctx_generate_attrib(struct gf100_grctx *);
void gv100_grctx_generate_rop_mapping(struct gf100_gr *);
void gv100_grctx_generate_r400088(struct gf100_gr *, bool);

/* context init value lists */

extern const struct gf100_gr_pack gf100_grctx_pack_icmd[];

extern const struct gf100_gr_pack gf100_grctx_pack_mthd[];
extern const struct gf100_gr_init gf100_grctx_init_902d_0[];
extern const struct gf100_gr_init gf100_grctx_init_9039_0[];
extern const struct gf100_gr_init gf100_grctx_init_90c0_0[];

extern const struct gf100_gr_pack gf100_grctx_pack_hub[];
extern const struct gf100_gr_init gf100_grctx_init_main_0[];
extern const struct gf100_gr_init gf100_grctx_init_fe_0[];
extern const struct gf100_gr_init gf100_grctx_init_pri_0[];
extern const struct gf100_gr_init gf100_grctx_init_memfmt_0[];
extern const struct gf100_gr_init gf100_grctx_init_rstr2d_0[];
extern const struct gf100_gr_init gf100_grctx_init_scc_0[];

extern const struct gf100_gr_pack gf100_grctx_pack_gpc_0[];
extern const struct gf100_gr_pack gf100_grctx_pack_gpc_1[];
extern const struct gf100_gr_init gf100_grctx_init_gpc_unk_0[];
extern const struct gf100_gr_init gf100_grctx_init_prop_0[];
extern const struct gf100_gr_init gf100_grctx_init_gpc_unk_1[];
extern const struct gf100_gr_init gf100_grctx_init_zcull_0[];
extern const struct gf100_gr_init gf100_grctx_init_crstr_0[];
extern const struct gf100_gr_init gf100_grctx_init_gpm_0[];
extern const struct gf100_gr_init gf100_grctx_init_gcc_0[];

extern const struct gf100_gr_pack gf100_grctx_pack_zcull[];

extern const struct gf100_gr_pack gf100_grctx_pack_tpc[];
extern const struct gf100_gr_init gf100_grctx_init_pe_0[];
extern const struct gf100_gr_init gf100_grctx_init_wwdx_0[];
extern const struct gf100_gr_init gf100_grctx_init_mpc_0[];
extern const struct gf100_gr_init gf100_grctx_init_tpccs_0[];

extern const struct gf100_gr_init gf104_grctx_init_tex_0[];
extern const struct gf100_gr_init gf104_grctx_init_l1c_0[];
extern const struct gf100_gr_init gf104_grctx_init_sm_0[];

extern const struct gf100_gr_init gf108_grctx_init_9097_0[];

extern const struct gf100_gr_init gf108_grctx_init_gpm_0[];

extern const struct gf100_gr_init gf108_grctx_init_pe_0[];
extern const struct gf100_gr_init gf108_grctx_init_wwdx_0[];
extern const struct gf100_gr_init gf108_grctx_init_tpccs_0[];

extern const struct gf100_gr_init gf110_grctx_init_9197_0[];
extern const struct gf100_gr_init gf110_grctx_init_9297_0[];

extern const struct gf100_gr_pack gf119_grctx_pack_icmd[];

extern const struct gf100_gr_pack gf119_grctx_pack_mthd[];

extern const struct gf100_gr_init gf119_grctx_init_fe_0[];
extern const struct gf100_gr_init gf119_grctx_init_be_0[];

extern const struct gf100_gr_init gf119_grctx_init_prop_0[];
extern const struct gf100_gr_init gf119_grctx_init_gpc_unk_1[];
extern const struct gf100_gr_init gf119_grctx_init_crstr_0[];

extern const struct gf100_gr_init gf119_grctx_init_sm_0[];

extern const struct gf100_gr_init gf117_grctx_init_pe_0[];

extern const struct gf100_gr_init gf117_grctx_init_wwdx_0[];

extern const struct gf100_gr_pack gf117_grctx_pack_gpc_1[];

extern const struct gf100_gr_init gk104_grctx_init_memfmt_0[];
extern const struct gf100_gr_init gk104_grctx_init_ds_0[];
extern const struct gf100_gr_init gk104_grctx_init_scc_0[];

extern const struct gf100_gr_init gk104_grctx_init_gpm_0[];

extern const struct gf100_gr_init gk104_grctx_init_pes_0[];

extern const struct gf100_gr_pack gk104_grctx_pack_hub[];
extern const struct gf100_gr_pack gk104_grctx_pack_tpc[];
extern const struct gf100_gr_pack gk104_grctx_pack_ppc[];
extern const struct gf100_gr_pack gk104_grctx_pack_icmd[];
extern const struct gf100_gr_init gk104_grctx_init_a097_0[];

extern const struct gf100_gr_pack gk110_grctx_pack_icmd[];

extern const struct gf100_gr_pack gk110_grctx_pack_mthd[];

extern const struct gf100_gr_pack gk110_grctx_pack_hub[];
extern const struct gf100_gr_init gk110_grctx_init_pri_0[];
extern const struct gf100_gr_init gk110_grctx_init_cwd_0[];

extern const struct gf100_gr_pack gk110_grctx_pack_gpc_0[];
extern const struct gf100_gr_pack gk110_grctx_pack_gpc_1[];
extern const struct gf100_gr_init gk110_grctx_init_gpc_unk_2[];

extern const struct gf100_gr_init gk110_grctx_init_tex_0[];
extern const struct gf100_gr_init gk110_grctx_init_mpc_0[];
extern const struct gf100_gr_init gk110_grctx_init_l1c_0[];

extern const struct gf100_gr_pack gk110_grctx_pack_ppc[];

extern const struct gf100_gr_init gk208_grctx_init_rstr2d_0[];

extern const struct gf100_gr_init gk208_grctx_init_prop_0[];
extern const struct gf100_gr_init gk208_grctx_init_crstr_0[];

extern const struct gf100_gr_init gm107_grctx_init_gpc_unk_0[];
extern const struct gf100_gr_init gm107_grctx_init_wwdx_0[];
#endif
