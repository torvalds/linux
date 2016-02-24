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

int  gf100_grctx_mmio_data(struct gf100_grctx *, u32 size, u32 align, u32 access);
void gf100_grctx_mmio_item(struct gf100_grctx *, u32 addr, u32 data, int s, int);

#define mmio_vram(a,b,c,d) gf100_grctx_mmio_data((a), (b), (c), (d))
#define mmio_refn(a,b,c,d,e) gf100_grctx_mmio_item((a), (b), (c), (d), (e))
#define mmio_skip(a,b,c) mmio_refn((a), (b), (c), -1, -1)
#define mmio_wr32(a,b,c) mmio_refn((a), (b), (c),  0, -1)

struct gf100_grctx_func {
	/* main context generation function */
	void  (*main)(struct gf100_gr *, struct gf100_grctx *);
	/* context-specific modify-on-first-load list generation function */
	void  (*unkn)(struct gf100_gr *);
	/* mmio context data */
	const struct gf100_gr_pack *hub;
	const struct gf100_gr_pack *gpc;
	const struct gf100_gr_pack *zcull;
	const struct gf100_gr_pack *tpc;
	const struct gf100_gr_pack *ppc;
	/* indirect context data, generated with icmds/mthds */
	const struct gf100_gr_pack *icmd;
	const struct gf100_gr_pack *mthd;
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
};

extern const struct gf100_grctx_func gf100_grctx;
int  gf100_grctx_generate(struct gf100_gr *);
void gf100_grctx_generate_main(struct gf100_gr *, struct gf100_grctx *);
void gf100_grctx_generate_bundle(struct gf100_grctx *);
void gf100_grctx_generate_pagepool(struct gf100_grctx *);
void gf100_grctx_generate_attrib(struct gf100_grctx *);
void gf100_grctx_generate_unkn(struct gf100_gr *);
void gf100_grctx_generate_tpcid(struct gf100_gr *);
void gf100_grctx_generate_r406028(struct gf100_gr *);
void gf100_grctx_generate_r4060a8(struct gf100_gr *);
void gf100_grctx_generate_r418bb8(struct gf100_gr *);
void gf100_grctx_generate_r406800(struct gf100_gr *);

extern const struct gf100_grctx_func gf108_grctx;
void gf108_grctx_generate_attrib(struct gf100_grctx *);
void gf108_grctx_generate_unkn(struct gf100_gr *);

extern const struct gf100_grctx_func gf104_grctx;
extern const struct gf100_grctx_func gf110_grctx;

extern const struct gf100_grctx_func gf117_grctx;
void gf117_grctx_generate_attrib(struct gf100_grctx *);

extern const struct gf100_grctx_func gf119_grctx;

extern const struct gf100_grctx_func gk104_grctx;
extern const struct gf100_grctx_func gk20a_grctx;
void gk104_grctx_generate_main(struct gf100_gr *, struct gf100_grctx *);
void gk104_grctx_generate_bundle(struct gf100_grctx *);
void gk104_grctx_generate_pagepool(struct gf100_grctx *);
void gk104_grctx_generate_unkn(struct gf100_gr *);
void gk104_grctx_generate_r418bb8(struct gf100_gr *);
void gk104_grctx_generate_rop_active_fbps(struct gf100_gr *);


void gm107_grctx_generate_bundle(struct gf100_grctx *);
void gm107_grctx_generate_pagepool(struct gf100_grctx *);
void gm107_grctx_generate_attrib(struct gf100_grctx *);

extern const struct gf100_grctx_func gk110_grctx;
extern const struct gf100_grctx_func gk110b_grctx;
extern const struct gf100_grctx_func gk208_grctx;

extern const struct gf100_grctx_func gm107_grctx;
void gm107_grctx_generate_bundle(struct gf100_grctx *);
void gm107_grctx_generate_pagepool(struct gf100_grctx *);
void gm107_grctx_generate_attrib(struct gf100_grctx *);

extern const struct gf100_grctx_func gm200_grctx;
void gm200_grctx_generate_main(struct gf100_gr *, struct gf100_grctx *);
void gm200_grctx_generate_tpcid(struct gf100_gr *);
void gm200_grctx_generate_405b60(struct gf100_gr *);

extern const struct gf100_grctx_func gm206_grctx;
extern const struct gf100_grctx_func gm20b_grctx;

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

extern const struct gf100_gr_pack gf100_grctx_pack_gpc[];
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

extern const struct gf100_gr_init gk104_grctx_init_memfmt_0[];
extern const struct gf100_gr_init gk104_grctx_init_ds_0[];
extern const struct gf100_gr_init gk104_grctx_init_scc_0[];

extern const struct gf100_gr_init gk104_grctx_init_gpm_0[];

extern const struct gf100_gr_init gk104_grctx_init_pes_0[];

extern const struct gf100_gr_pack gk104_grctx_pack_hub[];
extern const struct gf100_gr_pack gk104_grctx_pack_gpc[];
extern const struct gf100_gr_pack gk104_grctx_pack_tpc[];
extern const struct gf100_gr_pack gk104_grctx_pack_ppc[];
extern const struct gf100_gr_pack gk104_grctx_pack_icmd[];
extern const struct gf100_gr_init gk104_grctx_init_a097_0[];

extern const struct gf100_gr_pack gk110_grctx_pack_icmd[];

extern const struct gf100_gr_pack gk110_grctx_pack_mthd[];

extern const struct gf100_gr_pack gk110_grctx_pack_hub[];
extern const struct gf100_gr_init gk110_grctx_init_pri_0[];
extern const struct gf100_gr_init gk110_grctx_init_cwd_0[];

extern const struct gf100_gr_pack gk110_grctx_pack_gpc[];
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

extern const struct gf100_gr_pack gm200_grctx_pack_icmd[];

extern const struct gf100_gr_pack gm200_grctx_pack_mthd[];
#endif
